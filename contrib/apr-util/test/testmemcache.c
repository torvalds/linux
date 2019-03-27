/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "testutil.h"
#include "apr.h"
#include "apu.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "apr_hash.h"
#include "apr_memcache.h"
#include "apr_network_io.h"

#if APR_HAVE_STDLIB_H
#include <stdlib.h>		/* for exit() */
#endif

#define HOST "localhost"
#define PORT 11211

/* the total number of items to use for set/get testing */
#define TDATA_SIZE 3000

/* some smaller subset of TDATA_SIZE used for multiget testing */
#define TDATA_SET 100

/* our custom hash function just returns this all the time */
#define HASH_FUNC_RESULT 510

/* all keys will be prefixed with this */
const char prefix[] = "testmemcache";

/* text for values we store */
const char txt[] =
"Lorem ipsum dolor sit amet, consectetuer adipiscing elit. Duis at"
"lacus in ligula hendrerit consectetuer. Vestibulum tristique odio"
"iaculis leo. In massa arcu, ultricies a, laoreet nec, hendrerit non,"
"neque. Nulla sagittis sapien ac risus. Morbi ligula dolor, vestibulum"
"nec, viverra id, placerat dapibus, arcu. Curabitur egestas feugiat"
"tellus. Donec dignissim. Nunc ante. Curabitur id lorem. In mollis"
"tortor sit amet eros auctor dapibus. Proin nulla sem, tristique in,"
"convallis id, iaculis feugiat cras amet.";

/*
 * this datatype is for our custom server determination function. this might
 * be useful if you don't want to rely on simply hashing keys to determine
 * where a key belongs, but instead want to write something fancy, or use some
 * other kind of configuration data, i.e. a hash plus some data about a 
 * namespace, or whatever. see my_server_func, and test_memcache_user_funcs
 * for the examples.
 */
typedef struct {
  const char *someval;
  apr_uint32_t which_server;
} my_hash_server_baton;


/* this could do something fancy and return some hash result. 
 * for simplicity, just return the same value, so we can test it later on.
 * if you wanted to use some external hashing library or functions for
 * consistent hashing, for example, this would be a good place to do it.
 */
static apr_uint32_t my_hash_func(void *baton, const char *data,
                                 apr_size_t data_len)
{

  return HASH_FUNC_RESULT;
}

/*
 * a fancy function to determine which server to use given some kind of data
 * and a hash value. this example actually ignores the hash value itself
 * and pulls some number from the *baton, which is a struct that has some 
 * kind of meaningful stuff in it.
 */
static apr_memcache_server_t *my_server_func(void *baton, 
                                             apr_memcache_t *mc,
                                             const apr_uint32_t hash)
{
  apr_memcache_server_t *ms = NULL;
  my_hash_server_baton *mhsb = (my_hash_server_baton *)baton;

  if(mc->ntotal == 0) {
    return NULL;
  } 

  if(mc->ntotal < mhsb->which_server) {
    return NULL;
  }

  ms = mc->live_servers[mhsb->which_server - 1];

  return ms;
}

apr_uint16_t firsttime = 0;
static int randval(apr_uint32_t high)
{
    apr_uint32_t i = 0;
    double d = 0;

    if (firsttime == 0) {
	srand((unsigned) (getpid()));
	firsttime = 1;
    }

    d = (double) rand() / ((double) RAND_MAX + 1);
    i = (int) (d * (high - 0 + 1));

    return i > 0 ? i : 1;
}

/*
 * general test to make sure we can create the memcache struct and add
 * some servers, but not more than we tell it we can add
 */

static void test_memcache_create(abts_case * tc, void *data)
{
  apr_pool_t *pool = p;
  apr_status_t rv;
  apr_memcache_t *memcache;
  apr_memcache_server_t *server, *s;
  apr_uint32_t max_servers = 10;
  apr_uint32_t i;
  apr_uint32_t hash;

  rv = apr_memcache_create(pool, max_servers, 0, &memcache);
  ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);
  
  for (i = 1; i <= max_servers; i++) {
    apr_port_t port;
    
    port = PORT + i;
    rv =
      apr_memcache_server_create(pool, HOST, PORT + i, 0, 1, 1, 60, &server);
    ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);
    
    rv = apr_memcache_add_server(memcache, server);
    ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);
    
    s = apr_memcache_find_server(memcache, HOST, port);
    ABTS_PTR_EQUAL(tc, server, s);
    
    rv = apr_memcache_disable_server(memcache, s);
    ABTS_ASSERT(tc, "server disable failed", rv == APR_SUCCESS);
    
    rv = apr_memcache_enable_server(memcache, s);
    ABTS_ASSERT(tc, "server enable failed", rv == APR_SUCCESS);
    
    hash = apr_memcache_hash(memcache, prefix, strlen(prefix));
    ABTS_ASSERT(tc, "hash failed", hash > 0);
    
    s = apr_memcache_find_server_hash(memcache, hash);
    ABTS_PTR_NOTNULL(tc, s);
  }

  rv = apr_memcache_server_create(pool, HOST, PORT, 0, 1, 1, 60, &server);
  ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_add_server(memcache, server);
  ABTS_ASSERT(tc, "server add should have failed", rv != APR_SUCCESS);
  
}

/* install our own custom hashing and server selection routines. */

static int create_test_hash(apr_pool_t *p, apr_hash_t *h)
{
  int i;
  
  for (i = 0; i < TDATA_SIZE; i++) {
    char *k, *v;
    
    k = apr_pstrcat(p, prefix, apr_itoa(p, i), NULL);
    v = apr_pstrndup(p, txt, randval((apr_uint32_t)strlen(txt)));
    
    apr_hash_set(h, k, APR_HASH_KEY_STRING, v);
  }

  return i;
}

static void test_memcache_user_funcs(abts_case * tc, void *data)
{
  apr_pool_t *pool = p;
  apr_status_t rv;
  apr_memcache_t *memcache;
  apr_memcache_server_t *found;
  apr_uint32_t max_servers = 10;
  apr_uint32_t hres;
  apr_uint32_t i;
  my_hash_server_baton *baton = 
    apr_pcalloc(pool, sizeof(my_hash_server_baton));

  rv = apr_memcache_create(pool, max_servers, 0, &memcache);
  ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);

  /* as noted above, install our custom hash function, and call 
   * apr_memcache_hash. the return value should be our predefined number,
   * and our function just ignores the other args, for simplicity.
   */
  memcache->hash_func = my_hash_func;

  hres = apr_memcache_hash(memcache, "whatever", sizeof("whatever") - 1);
  ABTS_INT_EQUAL(tc, HASH_FUNC_RESULT, hres);
  
  /* add some servers */
  for(i = 1; i <= 10; i++) {
    apr_memcache_server_t *ms;

    rv = apr_memcache_server_create(pool, HOST, i, 0, 1, 1, 60, &ms);
    ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);
    
    rv = apr_memcache_add_server(memcache, ms);
    ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);
  }

  /* 
   * set 'which_server' in our server_baton to find the third server 
   * which should have the same port.
   */
  baton->which_server = 3;
  memcache->server_func = my_server_func; 
  memcache->server_baton = baton;
  found = apr_memcache_find_server_hash(memcache, 0);
  ABTS_ASSERT(tc, "wrong server found", found->port == baton->which_server);
}

/* test non data related commands like stats and version */
static void test_memcache_meta(abts_case * tc, void *data)
{
    apr_pool_t *pool = p;
    apr_memcache_t *memcache;
    apr_memcache_server_t *server;
    apr_memcache_stats_t *stats;
    char *result;
    apr_status_t rv;

    rv = apr_memcache_create(pool, 1, 0, &memcache);
    ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);

    rv = apr_memcache_server_create(pool, HOST, PORT, 0, 1, 1, 60, &server);
    ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);

    rv = apr_memcache_add_server(memcache, server);
    ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);

    rv = apr_memcache_version(server, pool, &result);
    ABTS_PTR_NOTNULL(tc, result);

    rv = apr_memcache_stats(server, p, &stats);
    ABTS_PTR_NOTNULL(tc, stats);

    ABTS_STR_NEQUAL(tc, stats->version, result, 5);

    /* 
     * no way to know exactly what will be in most of these, so
     * just make sure there is something.
     */
    
    ABTS_ASSERT(tc, "pid", stats->pid >= 0);
    ABTS_ASSERT(tc, "time", stats->time >= 0);
    /* ABTS_ASSERT(tc, "pointer_size", stats->pointer_size >= 0); */
    ABTS_ASSERT(tc, "rusage_user", stats->rusage_user >= 0);
    ABTS_ASSERT(tc, "rusage_system", stats->rusage_system >= 0);

    ABTS_ASSERT(tc, "curr_items", stats->curr_items >= 0);
    ABTS_ASSERT(tc, "total_items", stats->total_items >= 0);
    ABTS_ASSERT(tc, "bytes", stats->bytes >= 0);

    ABTS_ASSERT(tc, "curr_connections", stats->curr_connections >= 0);
    ABTS_ASSERT(tc, "total_connections", stats->total_connections >= 0);
    ABTS_ASSERT(tc, "connection_structures", 
                stats->connection_structures >= 0);

    ABTS_ASSERT(tc, "cmd_get", stats->cmd_get >= 0);
    ABTS_ASSERT(tc, "cmd_set", stats->cmd_set >= 0);
    ABTS_ASSERT(tc, "get_hits", stats->get_hits >= 0);
    ABTS_ASSERT(tc, "get_misses", stats->get_misses >= 0);

    /* ABTS_ASSERT(tc, "evictions", stats->evictions >= 0); */

    ABTS_ASSERT(tc, "bytes_read", stats->bytes_read >= 0);
    ABTS_ASSERT(tc, "bytes_written", stats->bytes_written >= 0);
    ABTS_ASSERT(tc, "limit_maxbytes", stats->limit_maxbytes >= 0);

    /* ABTS_ASSERT(tc, "threads", stats->threads >= 0); */
}

/* test add and replace calls */

static void test_memcache_addreplace(abts_case * tc, void *data)
{
 apr_pool_t *pool = p;
 apr_status_t rv;
 apr_memcache_t *memcache;
 apr_memcache_server_t *server;
 apr_hash_t *tdata;
 apr_hash_index_t *hi;
 char *result;
 apr_size_t len;

  rv = apr_memcache_create(pool, 1, 0, &memcache);
  ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_server_create(pool, HOST, PORT, 0, 1, 1, 60, &server);
  ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_add_server(memcache, server);
  ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);
  
  tdata = apr_hash_make(p);
  create_test_hash(pool, tdata);

  for (hi = apr_hash_first(p, tdata); hi; hi = apr_hash_next(hi)) {
    const void *k;
    void *v;
    const char *key;

    apr_hash_this(hi, &k, NULL, &v);
    key = k;

    /* doesn't exist yet, fail */
    rv = apr_memcache_replace(memcache, key, v, strlen(v) - 1, 0, 27);
    ABTS_ASSERT(tc, "replace should have failed", rv != APR_SUCCESS);
    
    /* doesn't exist yet, succeed */
    rv = apr_memcache_add(memcache, key, v, strlen(v), 0, 27);
    ABTS_ASSERT(tc, "add failed", rv == APR_SUCCESS);

    /* exists now, succeed */
    rv = apr_memcache_replace(memcache, key, "new", sizeof("new") - 1, 0, 27);
    ABTS_ASSERT(tc, "replace failed", rv == APR_SUCCESS);

    /* make sure its different */
    rv = apr_memcache_getp(memcache, pool, key, &result, &len, NULL);
    ABTS_ASSERT(tc, "get failed", rv == APR_SUCCESS);
    ABTS_STR_NEQUAL(tc, result, "new", 3);

    /* exists now, fail */
    rv = apr_memcache_add(memcache, key, v, strlen(v), 0, 27);
    ABTS_ASSERT(tc, "add should have failed", rv != APR_SUCCESS);

    /* clean up */
    rv = apr_memcache_delete(memcache, key, 0);
    ABTS_ASSERT(tc, "delete failed", rv == APR_SUCCESS);
  }
}

/* basic tests of the increment and decrement commands */
static void test_memcache_incrdecr(abts_case * tc, void *data)
{
 apr_pool_t *pool = p;
 apr_status_t rv;
 apr_memcache_t *memcache;
 apr_memcache_server_t *server;
 apr_uint32_t new;
 char *result;
 apr_size_t len;
 apr_uint32_t i;

  rv = apr_memcache_create(pool, 1, 0, &memcache);
  ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_server_create(pool, HOST, PORT, 0, 1, 1, 60, &server);
  ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_add_server(memcache, server);
  ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);

  rv = apr_memcache_set(memcache, prefix, "271", sizeof("271") - 1, 0, 27);
  ABTS_ASSERT(tc, "set failed", rv == APR_SUCCESS);
  
  for( i = 1; i <= TDATA_SIZE; i++) {
    apr_uint32_t expect;

    rv = apr_memcache_getp(memcache, pool, prefix, &result, &len, NULL);
    ABTS_ASSERT(tc, "get failed", rv == APR_SUCCESS);

    expect = i + atoi(result);

    rv = apr_memcache_incr(memcache, prefix, i, &new);
    ABTS_ASSERT(tc, "incr failed", rv == APR_SUCCESS);

    ABTS_INT_EQUAL(tc, expect, new);

    rv = apr_memcache_decr(memcache, prefix, i, &new);
    ABTS_ASSERT(tc, "decr failed", rv == APR_SUCCESS);
    ABTS_INT_EQUAL(tc, atoi(result), new);

  }

  rv = apr_memcache_getp(memcache, pool, prefix, &result, &len, NULL);
  ABTS_ASSERT(tc, "get failed", rv == APR_SUCCESS);

  ABTS_INT_EQUAL(tc, 271, atoi(result));

  rv = apr_memcache_delete(memcache, prefix, 0);
  ABTS_ASSERT(tc, "delete failed", rv == APR_SUCCESS);
}

/* test the multiget functionality */
static void test_memcache_multiget(abts_case * tc, void *data)
{
  apr_pool_t *pool = p;
  apr_pool_t *tmppool;
  apr_status_t rv;
  apr_memcache_t *memcache;
  apr_memcache_server_t *server;
  apr_hash_t *tdata, *values;
  apr_hash_index_t *hi;
  apr_uint32_t i;

  rv = apr_memcache_create(pool, 1, 0, &memcache);
  ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_server_create(pool, HOST, PORT, 0, 1, 1, 60, &server);
  ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);
  
  rv = apr_memcache_add_server(memcache, server);
  ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);
  
  values = apr_hash_make(p);
  tdata = apr_hash_make(p);
  
  create_test_hash(pool, tdata);

  for (hi = apr_hash_first(p, tdata); hi; hi = apr_hash_next(hi)) {
    const void *k;
    void *v;
    const char *key;

    apr_hash_this(hi, &k, NULL, &v);
    key = k;

    rv = apr_memcache_set(memcache, key, v, strlen(v), 0, 27);
    ABTS_ASSERT(tc, "set failed", rv == APR_SUCCESS);
  }
  
  rv = apr_pool_create(&tmppool, pool);
  for (i = 0; i < TDATA_SET; i++)
    apr_memcache_add_multget_key(pool,
                                 apr_pstrcat(pool, prefix,
                                             apr_itoa(pool, i), NULL),
                                 &values);
    
  rv = apr_memcache_multgetp(memcache,
                             tmppool,
                             pool,
                             values);
  
  ABTS_ASSERT(tc, "multgetp failed", rv == APR_SUCCESS);
  ABTS_ASSERT(tc, "multgetp returned too few results",
              apr_hash_count(values) == TDATA_SET);
  
  for (hi = apr_hash_first(p, tdata); hi; hi = apr_hash_next(hi)) {
    const void *k;
    const char *key;
    
    apr_hash_this(hi, &k, NULL, NULL);
    key = k;

    rv = apr_memcache_delete(memcache, key, 0);
    ABTS_ASSERT(tc, "delete failed", rv == APR_SUCCESS);
  }
  
}

/* test setting and getting */

static void test_memcache_setget(abts_case * tc, void *data)
{
    apr_pool_t *pool = p;
    apr_status_t rv;
    apr_memcache_t *memcache;
    apr_memcache_server_t *server;
    apr_hash_t *tdata;
    apr_hash_index_t *hi;
    char *result;
    apr_size_t len;

    rv = apr_memcache_create(pool, 1, 0, &memcache);
    ABTS_ASSERT(tc, "memcache create failed", rv == APR_SUCCESS);

    rv = apr_memcache_server_create(pool, HOST, PORT, 0, 1, 1, 60, &server);
    ABTS_ASSERT(tc, "server create failed", rv == APR_SUCCESS);

    rv = apr_memcache_add_server(memcache, server);
    ABTS_ASSERT(tc, "server add failed", rv == APR_SUCCESS);

    tdata = apr_hash_make(pool);

    create_test_hash(pool, tdata);

    for (hi = apr_hash_first(p, tdata); hi; hi = apr_hash_next(hi)) {
	const void *k;
	void *v;
        const char *key;

	apr_hash_this(hi, &k, NULL, &v);
        key = k;

	rv = apr_memcache_set(memcache, key, v, strlen(v), 0, 27);
	ABTS_ASSERT(tc, "set failed", rv == APR_SUCCESS);
	rv = apr_memcache_getp(memcache, pool, key, &result, &len, NULL);
	ABTS_ASSERT(tc, "get failed", rv == APR_SUCCESS);
    }

    rv = apr_memcache_getp(memcache, pool, "nothere3423", &result, &len, NULL);

    ABTS_ASSERT(tc, "get should have failed", rv != APR_SUCCESS);

    for (hi = apr_hash_first(p, tdata); hi; hi = apr_hash_next(hi)) {
	const void *k;
	const char *key;

	apr_hash_this(hi, &k, NULL, NULL);
	key = k;

	rv = apr_memcache_delete(memcache, key, 0);
	ABTS_ASSERT(tc, "delete failed", rv == APR_SUCCESS);
    }
}

/* use apr_socket stuff to see if there is in fact a memcached server 
 * running on PORT. 
 */
static apr_status_t check_mc(void)
{
  apr_pool_t *pool = p;
  apr_status_t rv;
  apr_socket_t *sock = NULL;
  apr_sockaddr_t *sa;
  struct iovec vec[2];
  apr_size_t written;
  char buf[128];
  apr_size_t len;

  rv = apr_socket_create(&sock, APR_INET, SOCK_STREAM, 0, pool);
  if(rv != APR_SUCCESS) {
    return rv;
  }

  rv = apr_sockaddr_info_get(&sa, HOST, APR_INET, PORT, 0, pool);
  if(rv != APR_SUCCESS) {
    return rv;
  }

  rv = apr_socket_timeout_set(sock, 1 * APR_USEC_PER_SEC);
  if (rv != APR_SUCCESS) {
    return rv;
  }

  rv = apr_socket_connect(sock, sa);
  if (rv != APR_SUCCESS) {
    return rv;
  }

  rv = apr_socket_timeout_set(sock, -1);
  if (rv != APR_SUCCESS) {
    return rv;
  }

  vec[0].iov_base = "version";
  vec[0].iov_len  = sizeof("version") - 1;

  vec[1].iov_base = "\r\n";
  vec[1].iov_len  = sizeof("\r\n") -1;

  rv = apr_socket_sendv(sock, vec, 2, &written);
  if (rv != APR_SUCCESS) {
    return rv;
  }

  len = sizeof(buf);
  rv = apr_socket_recv(sock, buf, &len);
  if(rv != APR_SUCCESS) {
    return rv;
  }

  if(strncmp(buf, "VERSION", sizeof("VERSION")-1) != 0) {
    rv = APR_EGENERAL;
  }

  apr_socket_close(sock);
  return rv;
}

abts_suite *testmemcache(abts_suite * suite)
{
    apr_status_t rv;
    suite = ADD_SUITE(suite);
    /* check for a running memcached on the typical port before 
     * trying to run the tests. succeed if we don't find one.
     */
    rv = check_mc();
    if (rv == APR_SUCCESS) {
      abts_run_test(suite, test_memcache_create, NULL);
      abts_run_test(suite, test_memcache_user_funcs, NULL);
      abts_run_test(suite, test_memcache_meta, NULL);
      abts_run_test(suite, test_memcache_setget, NULL);
      abts_run_test(suite, test_memcache_multiget, NULL);
      abts_run_test(suite, test_memcache_addreplace, NULL);
      abts_run_test(suite, test_memcache_incrdecr, NULL);
    }
    else {
        abts_log_message("Error %d occurred attempting to reach memcached "
                         "on %s:%d.  Skipping apr_memcache tests...",
                         rv, HOST, PORT);
    }

    return suite;
}
