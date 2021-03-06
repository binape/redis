/**
 * Author Zav Deng(binape@126.com)
 **/
#include "server.h"
#include "lzf.h"    /* LZF compression library */
#include "zipmap.h"
#include "endianconv.h"

#include <pthread.h>

static pthread_t thread_id = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static client * current_client = NULL;

static void dumpKeys(client *c, redisDb *db, FILE *fp) {
  dictIterator *di;
  dictEntry *de;

  sds pattern = c->argv[1]->ptr;
  int plen = sdslen(pattern), allkeys;

  di = dictGetSafeIterator(db->dict);

  allkeys = (pattern[0] == '*' && pattern[1] == '\0');
  while((de = dictNext(di)) != NULL) {
    sds key = dictGetKey(de);
    robj * keyobj;

    if (allkeys || stringmatchlen(pattern,plen,key,sdslen(key),0)) {
      keyobj = createStringObject(key,sdslen(key));
      if (expireIfNeeded(c->db,keyobj) == 0) {
	fprintf(fp, "%s\n", (const char *)key);
      }
      decrRefCount(keyobj);
    }
  }

  dictReleaseIterator(di);
}

static void * asyncDumpKeys(void *arg) {

  while (1) {
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&cond, &mutex);

    serverLog(LL_VERBOSE, "Keys dump signal received");
    if (current_client) {
      client * c = current_client;
      redisDb *db = c->db;

      FILE *fp = fopen("keys.dump", "w");
      if (fp) {
	dumpKeys(c, db, fp);
	fflush(fp);
	fclose(fp);
      }
      serverLog(LL_VERBOSE, "Keys dump finished");
      current_client = NULL;
    } else {
      serverLog(LL_VERBOSE, "Client is not ready");
    }
    pthread_mutex_unlock(&mutex);
  }
  return NULL;
}

void akeysCommand(client *client) {
  if (!thread_id) {
    pthread_create(&thread_id, NULL, &asyncDumpKeys, NULL);
  }

  if (thread_id) {
    if (current_client != NULL) {
      addReplyStatus(client, "Keys command is working");
    } else {
      pthread_mutex_lock(&mutex);
      current_client = client;
      pthread_cond_signal(&cond);
      pthread_mutex_unlock(&mutex);
      addReplyStatus(client,"Keys command issued");
    }
  } else {
    addReplyStatus(client,"Keys command failed, thread not created");
  }

}
