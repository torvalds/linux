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

#include "apr_memcache.h"
#include "apr_poll.h"
#include "apr_version.h"
#include <stdlib.h>

#define BUFFER_SIZE 512
struct apr_memcache_conn_t
{
    char *buffer;
    apr_size_t blen;
    apr_pool_t *p;
    apr_pool_t *tp;
    apr_socket_t *sock;
    apr_bucket_brigade *bb;
    apr_bucket_brigade *tb;
    apr_memcache_server_t *ms;
};                                                          

/* Strings for Client Commands */

#define MC_EOL "\r\n"
#define MC_EOL_LEN (sizeof(MC_EOL)-1)

#define MC_WS " "
#define MC_WS_LEN (sizeof(MC_WS)-1)

#define MC_GET "get "
#define MC_GET_LEN (sizeof(MC_GET)-1)

#define MC_SET "set "
#define MC_SET_LEN (sizeof(MC_SET)-1)

#define MC_ADD "add "
#define MC_ADD_LEN (sizeof(MC_ADD)-1)

#define MC_REPLACE "replace "
#define MC_REPLACE_LEN (sizeof(MC_REPLACE)-1)

#define MC_DELETE "delete "
#define MC_DELETE_LEN (sizeof(MC_DELETE)-1)

#define MC_INCR "incr "
#define MC_INCR_LEN (sizeof(MC_INCR)-1)

#define MC_DECR "decr "
#define MC_DECR_LEN (sizeof(MC_DECR)-1)

#define MC_VERSION "version"
#define MC_VERSION_LEN (sizeof(MC_VERSION)-1)

#define MC_STATS "stats"
#define MC_STATS_LEN (sizeof(MC_STATS)-1)

#define MC_QUIT "quit"
#define MC_QUIT_LEN (sizeof(MC_QUIT)-1)

/* Strings for Server Replies */

#define MS_STORED "STORED"
#define MS_STORED_LEN (sizeof(MS_STORED)-1)

#define MS_NOT_STORED "NOT_STORED"
#define MS_NOT_STORED_LEN (sizeof(MS_NOT_STORED)-1)

#define MS_DELETED "DELETED"
#define MS_DELETED_LEN (sizeof(MS_DELETED)-1)

#define MS_NOT_FOUND "NOT_FOUND"
#define MS_NOT_FOUND_LEN (sizeof(MS_NOT_FOUND)-1)

#define MS_VALUE "VALUE"
#define MS_VALUE_LEN (sizeof(MS_VALUE)-1)

#define MS_ERROR "ERROR"
#define MS_ERROR_LEN (sizeof(MS_ERROR)-1)

#define MS_VERSION "VERSION"
#define MS_VERSION_LEN (sizeof(MS_VERSION)-1)

#define MS_STAT "STAT"
#define MS_STAT_LEN (sizeof(MS_STAT)-1)

#define MS_END "END"
#define MS_END_LEN (sizeof(MS_END)-1)

/** Server and Query Structure for a multiple get */
struct cache_server_query_t {
    apr_memcache_server_t* ms;
    apr_memcache_conn_t* conn;
    struct iovec* query_vec;
    apr_int32_t query_vec_count;
};

#define MULT_GET_TIMEOUT 50000

static apr_status_t make_server_dead(apr_memcache_t *mc, apr_memcache_server_t *ms)
{
#if APR_HAS_THREADS
    apr_thread_mutex_lock(ms->lock);
#endif
    ms->status = APR_MC_SERVER_DEAD;
    ms->btime = apr_time_now();
#if APR_HAS_THREADS
    apr_thread_mutex_unlock(ms->lock);
#endif
    return APR_SUCCESS;
}

static apr_status_t make_server_live(apr_memcache_t *mc, apr_memcache_server_t *ms)
{
    ms->status = APR_MC_SERVER_LIVE; 
    return APR_SUCCESS;
}


APU_DECLARE(apr_status_t) apr_memcache_add_server(apr_memcache_t *mc, apr_memcache_server_t *ms)
{
    apr_status_t rv = APR_SUCCESS;

    if(mc->ntotal >= mc->nalloc) {
        return APR_ENOMEM;
    }

    mc->live_servers[mc->ntotal] = ms;
    mc->ntotal++;
    make_server_live(mc, ms);
    return rv;
}

static apr_status_t mc_version_ping(apr_memcache_server_t *ms);

APU_DECLARE(apr_memcache_server_t *) 
apr_memcache_find_server_hash(apr_memcache_t *mc, const apr_uint32_t hash)
{
    if (mc->server_func) {
        return mc->server_func(mc->server_baton, mc, hash);
    }
    else {
        return apr_memcache_find_server_hash_default(NULL, mc, hash);
    }
}   

APU_DECLARE(apr_memcache_server_t *) 
apr_memcache_find_server_hash_default(void *baton, apr_memcache_t *mc,
                                      const apr_uint32_t hash)
{
    apr_memcache_server_t *ms = NULL;
    apr_uint32_t h = hash ? hash : 1;
    apr_uint32_t i = 0;
    apr_time_t curtime = 0;
   
    if(mc->ntotal == 0) {
        return NULL;
    }

    do {
        ms = mc->live_servers[h % mc->ntotal];
        if(ms->status == APR_MC_SERVER_LIVE) {
            break;
        }
        else {
            if (curtime == 0) {
                curtime = apr_time_now();
            }
#if APR_HAS_THREADS
            apr_thread_mutex_lock(ms->lock);
#endif
            /* Try the dead server, every 5 seconds */
            if (curtime - ms->btime >  apr_time_from_sec(5)) {
                ms->btime = curtime;
                if (mc_version_ping(ms) == APR_SUCCESS) {
                    make_server_live(mc, ms);
#if APR_HAS_THREADS
                    apr_thread_mutex_unlock(ms->lock);
#endif
                    break;
                }
            }
#if APR_HAS_THREADS
            apr_thread_mutex_unlock(ms->lock);
#endif
        }
        h++;
        i++;
    } while(i < mc->ntotal);

    if (i == mc->ntotal) {
        ms = NULL;
    }

    return ms;
}

APU_DECLARE(apr_memcache_server_t *) apr_memcache_find_server(apr_memcache_t *mc, const char *host, apr_port_t port)
{
    int i;

    for (i = 0; i < mc->ntotal; i++) {
        if (strcmp(mc->live_servers[i]->host, host) == 0
            && mc->live_servers[i]->port == port) {

            return mc->live_servers[i];
        }
    }

    return NULL;
}

static apr_status_t ms_find_conn(apr_memcache_server_t *ms, apr_memcache_conn_t **conn) 
{
    apr_status_t rv;
    apr_bucket_alloc_t *balloc;
    apr_bucket *e;

#if APR_HAS_THREADS
    rv = apr_reslist_acquire(ms->conns, (void **)conn);
#else
    *conn = ms->conn;
    rv = APR_SUCCESS;
#endif

    if (rv != APR_SUCCESS) {
        return rv;
    }

    balloc = apr_bucket_alloc_create((*conn)->tp);
    (*conn)->bb = apr_brigade_create((*conn)->tp, balloc);
    (*conn)->tb = apr_brigade_create((*conn)->tp, balloc);

    e = apr_bucket_socket_create((*conn)->sock, balloc);
    APR_BRIGADE_INSERT_TAIL((*conn)->bb, e);

    return rv;
}

static apr_status_t ms_bad_conn(apr_memcache_server_t *ms, apr_memcache_conn_t *conn) 
{
#if APR_HAS_THREADS
    return apr_reslist_invalidate(ms->conns, conn);
#else
    return APR_SUCCESS;
#endif
}

static apr_status_t ms_release_conn(apr_memcache_server_t *ms, apr_memcache_conn_t *conn) 
{
    apr_pool_clear(conn->tp);
#if APR_HAS_THREADS
    return apr_reslist_release(ms->conns, conn);
#else
    return APR_SUCCESS;
#endif
}

APU_DECLARE(apr_status_t) apr_memcache_enable_server(apr_memcache_t *mc, apr_memcache_server_t *ms)
{
    apr_status_t rv = APR_SUCCESS;

    if (ms->status == APR_MC_SERVER_LIVE) {
        return rv;
    }

    rv = make_server_live(mc, ms);
    return rv;
}

APU_DECLARE(apr_status_t) apr_memcache_disable_server(apr_memcache_t *mc, apr_memcache_server_t *ms)
{
    return make_server_dead(mc, ms);
}

static apr_status_t conn_connect(apr_memcache_conn_t *conn)
{
    apr_status_t rv = APR_SUCCESS;
    apr_sockaddr_t *sa;
#if APR_HAVE_SOCKADDR_UN
    apr_int32_t family = conn->ms->host[0] != '/' ? APR_INET : APR_UNIX;
#else
    apr_int32_t family = APR_INET;
#endif

    rv = apr_sockaddr_info_get(&sa, conn->ms->host, family, conn->ms->port, 0, conn->p);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_socket_timeout_set(conn->sock, 1 * APR_USEC_PER_SEC);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_socket_connect(conn->sock, sa);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_socket_timeout_set(conn->sock, -1);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    return rv;
}


static apr_status_t
mc_conn_construct(void **conn_, void *params, apr_pool_t *pool)
{
    apr_status_t rv = APR_SUCCESS;
    apr_memcache_conn_t *conn;
    apr_pool_t *np;
    apr_pool_t *tp;
    apr_memcache_server_t *ms = params;
#if APR_HAVE_SOCKADDR_UN
    apr_int32_t family = ms->host[0] != '/' ? APR_INET : APR_UNIX;
#else
    apr_int32_t family = APR_INET;
#endif

    rv = apr_pool_create(&np, pool);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_pool_create(&tp, np);
    if (rv != APR_SUCCESS) {
        apr_pool_destroy(np);
        return rv;
    }

    conn = apr_palloc(np, sizeof( apr_memcache_conn_t ));

    conn->p = np;
    conn->tp = tp;

    rv = apr_socket_create(&conn->sock, family, SOCK_STREAM, 0, np);

    if (rv != APR_SUCCESS) {
        apr_pool_destroy(np);
        return rv;
    }

    conn->buffer = apr_palloc(conn->p, BUFFER_SIZE);
    conn->blen = 0;
    conn->ms = ms;

    rv = conn_connect(conn);
    if (rv != APR_SUCCESS) {
        apr_pool_destroy(np);
    }
    else {
        *conn_ = conn;
    }
    
    return rv;
}

#if APR_HAS_THREADS
static apr_status_t
mc_conn_destruct(void *conn_, void *params, apr_pool_t *pool)
{
    apr_memcache_conn_t *conn = (apr_memcache_conn_t*)conn_;
    struct iovec vec[2];
    apr_size_t written;
    
    /* send a quit message to the memcached server to be nice about it. */
    vec[0].iov_base = MC_QUIT;
    vec[0].iov_len = MC_QUIT_LEN;

    vec[1].iov_base = MC_EOL;
    vec[1].iov_len = MC_EOL_LEN;
    
    /* Return values not checked, since we just want to make it go away. */
    apr_socket_sendv(conn->sock, vec, 2, &written);
    apr_socket_close(conn->sock);

    apr_pool_destroy(conn->p);
    
    return APR_SUCCESS;
}
#endif

APU_DECLARE(apr_status_t) apr_memcache_server_create(apr_pool_t *p, 
                                                     const char *host, apr_port_t port, 
                                                     apr_uint32_t min, apr_uint32_t smax,
                                                     apr_uint32_t max, apr_uint32_t ttl,
                                                     apr_memcache_server_t **ms)
{
    apr_status_t rv = APR_SUCCESS;
    apr_memcache_server_t *server;
    apr_pool_t *np;

    rv = apr_pool_create(&np, p);

    server = apr_palloc(np, sizeof(apr_memcache_server_t));

    server->p = np;
    server->host = apr_pstrdup(np, host);
    server->port = port;
    server->status = APR_MC_SERVER_DEAD;
#if APR_HAS_THREADS
    rv = apr_thread_mutex_create(&server->lock, APR_THREAD_MUTEX_DEFAULT, np);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_reslist_create(&server->conns, 
                               min,                     /* hard minimum */
                               smax,                    /* soft maximum */
                               max,                     /* hard maximum */
                               ttl,                     /* Time to live */
                               mc_conn_construct,       /* Make a New Connection */
                               mc_conn_destruct,        /* Kill Old Connection */
                               server, np);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    apr_reslist_cleanup_order_set(server->conns, APR_RESLIST_CLEANUP_FIRST);
#else
    rv = mc_conn_construct((void**)&(server->conn), server, np);
    if (rv != APR_SUCCESS) {
        return rv;
    }
#endif

    *ms = server;

    return rv;
}

APU_DECLARE(apr_status_t) apr_memcache_create(apr_pool_t *p,
                                              apr_uint16_t max_servers, apr_uint32_t flags,
                                              apr_memcache_t **memcache) 
{
    apr_status_t rv = APR_SUCCESS;
    apr_memcache_t *mc;
    
    mc = apr_palloc(p, sizeof(apr_memcache_t));
    mc->p = p;
    mc->nalloc = max_servers;
    mc->ntotal = 0;
    mc->live_servers = apr_palloc(p, mc->nalloc * sizeof(struct apr_memcache_server_t *));
    mc->hash_func = NULL;
    mc->hash_baton = NULL;
    mc->server_func = NULL;
    mc->server_baton = NULL;
    *memcache = mc;
    return rv;
}


/* The crc32 functions and data was originally written by Spencer
 * Garrett <srg@quick.com> and was gleaned from the PostgreSQL source
 * tree via the files contrib/ltree/crc32.[ch] and from FreeBSD at
 * src/usr.bin/cksum/crc32.c.
 */ 

static const apr_uint32_t crc32tab[256] = {
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
  0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
  0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
  0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
  0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
  0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
  0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
  0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
  0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
  0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
  0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
  0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
  0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
  0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
  0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
  0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
  0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
  0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
  0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
  0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
  0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
  0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
  0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
  0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
  0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
  0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
  0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
  0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
  0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
  0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
  0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
  0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
  0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
  0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
  0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
  0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
  0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
  0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
  0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
  0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
  0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
  0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
  0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
  0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
  0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
  0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
  0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

APU_DECLARE(apr_uint32_t) apr_memcache_hash_crc32(void *baton, 
                                                  const char *data,
                                                  const apr_size_t data_len)
{
    apr_uint32_t i;
    apr_uint32_t crc;
    crc = ~0;
    
    for (i = 0; i < data_len; i++)
        crc = (crc >> 8) ^ crc32tab[(crc ^ (data[i])) & 0xff];
    
    return ~crc;
}

APU_DECLARE(apr_uint32_t) apr_memcache_hash_default(void *baton, 
                                                    const char *data,
                                                    const apr_size_t data_len)
{
    /* The default Perl Client doesn't actually use just crc32 -- it shifts it again
     * like this....
     */
    return ((apr_memcache_hash_crc32(baton, data, data_len) >> 16) & 0x7fff);
}

APU_DECLARE(apr_uint32_t) apr_memcache_hash(apr_memcache_t *mc,
                                            const char *data,
                                            const apr_size_t data_len)
{
    if (mc->hash_func) {
        return mc->hash_func(mc->hash_baton, data, data_len);
    }
    else {
        return apr_memcache_hash_default(NULL, data, data_len);
    }
}

static apr_status_t get_server_line(apr_memcache_conn_t *conn)
{
    apr_size_t bsize = BUFFER_SIZE;
    apr_status_t rv = APR_SUCCESS;

    rv = apr_brigade_split_line(conn->tb, conn->bb, APR_BLOCK_READ, BUFFER_SIZE);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_brigade_flatten(conn->tb, conn->buffer, &bsize);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    conn->blen = bsize;
    conn->buffer[bsize] = '\0';

    return apr_brigade_cleanup(conn->tb);
}

static apr_status_t storage_cmd_write(apr_memcache_t *mc,
                                      char *cmd,
                                      const apr_size_t cmd_size,
                                      const char *key,
                                      char *data,
                                      const apr_size_t data_size,
                                      apr_uint32_t timeout,
                                      apr_uint16_t flags)
{
    apr_uint32_t hash;
    apr_memcache_server_t *ms;
    apr_memcache_conn_t *conn;
    apr_status_t rv;
    apr_size_t written;
    struct iovec vec[5];
    apr_size_t klen;

    apr_size_t key_size = strlen(key);

    hash = apr_memcache_hash(mc, key, key_size);

    ms = apr_memcache_find_server_hash(mc, hash);

    if (ms == NULL)
        return APR_NOTFOUND;

    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    /* <command name> <key> <flags> <exptime> <bytes>\r\n<data>\r\n */

    vec[0].iov_base = cmd;
    vec[0].iov_len  = cmd_size;

    vec[1].iov_base = (void*)key;
    vec[1].iov_len  = key_size;

    klen = apr_snprintf(conn->buffer, BUFFER_SIZE, " %u %u %" APR_SIZE_T_FMT " " MC_EOL,
                        flags, timeout, data_size);

    vec[2].iov_base = conn->buffer;
    vec[2].iov_len  = klen;

    vec[3].iov_base = data;
    vec[3].iov_len  = data_size;

    vec[4].iov_base = MC_EOL;
    vec[4].iov_len  = MC_EOL_LEN;

    rv = apr_socket_sendv(conn->sock, vec, 5, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    rv = get_server_line(conn);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    if (strcmp(conn->buffer, MS_STORED MC_EOL) == 0) {
        rv = APR_SUCCESS;
    }
    else if (strcmp(conn->buffer, MS_NOT_STORED MC_EOL) == 0) {
        rv = APR_EEXIST;
    }
    else {
        rv = APR_EGENERAL;
    }

    ms_release_conn(ms, conn);

    return rv;
}

APU_DECLARE(apr_status_t)
apr_memcache_set(apr_memcache_t *mc,
                 const char *key,
                 char *data,
                 const apr_size_t data_size,
                 apr_uint32_t timeout,
                 apr_uint16_t flags)
{
    return storage_cmd_write(mc,
                           MC_SET, MC_SET_LEN,
                           key,
                           data, data_size,
                           timeout, flags);
}

APU_DECLARE(apr_status_t)
apr_memcache_add(apr_memcache_t *mc,
                 const char *key,
                 char *data,
                 const apr_size_t data_size,
                 apr_uint32_t timeout,
                 apr_uint16_t flags)
{
    return storage_cmd_write(mc, 
                           MC_ADD, MC_ADD_LEN,
                           key,
                           data, data_size,
                           timeout, flags);
}

APU_DECLARE(apr_status_t)
apr_memcache_replace(apr_memcache_t *mc,
                 const char *key,
                 char *data,
                 const apr_size_t data_size,
                 apr_uint32_t timeout,
                 apr_uint16_t flags)
{
    return storage_cmd_write(mc,
                           MC_REPLACE, MC_REPLACE_LEN,
                           key,
                           data, data_size,
                           timeout, flags);

}

APU_DECLARE(apr_status_t)
apr_memcache_getp(apr_memcache_t *mc,
                  apr_pool_t *p,
                  const char *key,
                  char **baton,
                  apr_size_t *new_length,
                  apr_uint16_t *flags_)
{
    apr_status_t rv;
    apr_memcache_server_t *ms;
    apr_memcache_conn_t *conn;
    apr_uint32_t hash;
    apr_size_t written;
    apr_size_t klen = strlen(key);
    struct iovec vec[3];

    hash = apr_memcache_hash(mc, key, klen);
    ms = apr_memcache_find_server_hash(mc, hash);
    if (ms == NULL)
        return APR_NOTFOUND;
    
    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    /* get <key>[ <key>[...]]\r\n */
    vec[0].iov_base = MC_GET;
    vec[0].iov_len  = MC_GET_LEN;

    vec[1].iov_base = (void*)key;
    vec[1].iov_len  = klen;

    vec[2].iov_base = MC_EOL;
    vec[2].iov_len  = MC_EOL_LEN;

    rv = apr_socket_sendv(conn->sock, vec, 3, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    rv = get_server_line(conn);
    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    if (strncmp(MS_VALUE, conn->buffer, MS_VALUE_LEN) == 0) {
        char *flags;
        char *length;
        char *last;
        apr_size_t len = 0;

        flags = apr_strtok(conn->buffer, " ", &last);
        flags = apr_strtok(NULL, " ", &last);
        flags = apr_strtok(NULL, " ", &last);

        if (flags_) {
            *flags_ = atoi(flags);
        }

        length = apr_strtok(NULL, " ", &last);
        if (length) {
            len = strtol(length, (char **)NULL, 10);
        }

        if (len == 0 )  {
            *new_length = 0;
            *baton = NULL;
        }
        else {
            apr_bucket_brigade *bbb;
            apr_bucket *e;

            /* eat the trailing \r\n */
            rv = apr_brigade_partition(conn->bb, len+2, &e);

            if (rv != APR_SUCCESS) {
                ms_bad_conn(ms, conn);
                apr_memcache_disable_server(mc, ms);
                return rv;
            }
            
            bbb = apr_brigade_split(conn->bb, e);

            rv = apr_brigade_pflatten(conn->bb, baton, &len, p);

            if (rv != APR_SUCCESS) {
                ms_bad_conn(ms, conn);
                apr_memcache_disable_server(mc, ms);
                return rv;
            }

            rv = apr_brigade_destroy(conn->bb);
            if (rv != APR_SUCCESS) {
                ms_bad_conn(ms, conn);
                apr_memcache_disable_server(mc, ms);
                return rv;
            }

            conn->bb = bbb;

            *new_length = len - 2;
            (*baton)[*new_length] = '\0';
        }
        
        rv = get_server_line(conn);
        if (rv != APR_SUCCESS) {
            ms_bad_conn(ms, conn);
            apr_memcache_disable_server(mc, ms);
            return rv;
        }

        if (strncmp(MS_END, conn->buffer, MS_END_LEN) != 0) {
            rv = APR_EGENERAL;
        }
    }
    else if (strncmp(MS_END, conn->buffer, MS_END_LEN) == 0) {
        rv = APR_NOTFOUND;
    }
    else {
        rv = APR_EGENERAL;
    }

    ms_release_conn(ms, conn);

    return rv;
}

APU_DECLARE(apr_status_t)
apr_memcache_delete(apr_memcache_t *mc,
                    const char *key,
                    apr_uint32_t timeout)
{
    apr_status_t rv;
    apr_memcache_server_t *ms;
    apr_memcache_conn_t *conn;
    apr_uint32_t hash;
    apr_size_t written;
    struct iovec vec[3];
    apr_size_t klen = strlen(key);

    hash = apr_memcache_hash(mc, key, klen);
    ms = apr_memcache_find_server_hash(mc, hash);
    if (ms == NULL)
        return APR_NOTFOUND;
    
    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    /* delete <key> <time>\r\n */
    vec[0].iov_base = MC_DELETE;
    vec[0].iov_len  = MC_DELETE_LEN;

    vec[1].iov_base = (void*)key;
    vec[1].iov_len  = klen;

    klen = apr_snprintf(conn->buffer, BUFFER_SIZE, " %u" MC_EOL, timeout);

    vec[2].iov_base = conn->buffer;
    vec[2].iov_len  = klen;

    rv = apr_socket_sendv(conn->sock, vec, 3, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    rv = get_server_line(conn);
    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    if (strncmp(MS_DELETED, conn->buffer, MS_DELETED_LEN) == 0) {
        rv = APR_SUCCESS;
    }
    else if (strncmp(MS_NOT_FOUND, conn->buffer, MS_NOT_FOUND_LEN) == 0) {
        rv = APR_NOTFOUND;
    }
    else {
        rv = APR_EGENERAL;
    }

    ms_release_conn(ms, conn);

    return rv;
}

static apr_status_t num_cmd_write(apr_memcache_t *mc,
                                      char *cmd,
                                      const apr_uint32_t cmd_size,
                                      const char *key,
                                      const apr_int32_t inc,
                                      apr_uint32_t *new_value)
{
    apr_status_t rv;
    apr_memcache_server_t *ms;
    apr_memcache_conn_t *conn;
    apr_uint32_t hash;
    apr_size_t written;
    struct iovec vec[3];
    apr_size_t klen = strlen(key);

    hash = apr_memcache_hash(mc, key, klen);
    ms = apr_memcache_find_server_hash(mc, hash);
    if (ms == NULL)
        return APR_NOTFOUND;
    
    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    /* <cmd> <key> <value>\r\n */
    vec[0].iov_base = cmd;
    vec[0].iov_len  = cmd_size;

    vec[1].iov_base = (void*)key;
    vec[1].iov_len  = klen;

    klen = apr_snprintf(conn->buffer, BUFFER_SIZE, " %u" MC_EOL, inc);

    vec[2].iov_base = conn->buffer;
    vec[2].iov_len  = klen;

    rv = apr_socket_sendv(conn->sock, vec, 3, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    rv = get_server_line(conn);
    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        apr_memcache_disable_server(mc, ms);
        return rv;
    }

    if (strncmp(MS_ERROR, conn->buffer, MS_ERROR_LEN) == 0) {
        rv = APR_EGENERAL;
    }
    else if (strncmp(MS_NOT_FOUND, conn->buffer, MS_NOT_FOUND_LEN) == 0) {
        rv = APR_NOTFOUND;
    }
    else {
        if (new_value) {
            *new_value = atoi(conn->buffer);
        }
        rv = APR_SUCCESS;
    }

    ms_release_conn(ms, conn);

    return rv;
}

APU_DECLARE(apr_status_t)
apr_memcache_incr(apr_memcache_t *mc,
                    const char *key,
                    apr_int32_t inc,
                    apr_uint32_t *new_value)
{
    return num_cmd_write(mc,
                         MC_INCR,
                         MC_INCR_LEN,
                         key,
                         inc, 
                         new_value);
}


APU_DECLARE(apr_status_t)
apr_memcache_decr(apr_memcache_t *mc,
                    const char *key,
                    apr_int32_t inc,
                    apr_uint32_t *new_value)
{
    return num_cmd_write(mc,
                         MC_DECR,
                         MC_DECR_LEN,
                         key,
                         inc, 
                         new_value);
}



APU_DECLARE(apr_status_t)
apr_memcache_version(apr_memcache_server_t *ms,
                  apr_pool_t *p,
                  char **baton)
{
    apr_status_t rv;
    apr_memcache_conn_t *conn;
    apr_size_t written;
    struct iovec vec[2];

    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    /* version\r\n */
    vec[0].iov_base = MC_VERSION;
    vec[0].iov_len  = MC_VERSION_LEN;

    vec[1].iov_base = MC_EOL;
    vec[1].iov_len  = MC_EOL_LEN;

    rv = apr_socket_sendv(conn->sock, vec, 2, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        return rv;
    }

    rv = get_server_line(conn);
    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        return rv;
    }

    if (strncmp(MS_VERSION, conn->buffer, MS_VERSION_LEN) == 0) {
        *baton = apr_pstrmemdup(p, conn->buffer+MS_VERSION_LEN+1, 
                                conn->blen - MS_VERSION_LEN - 2);
        rv = APR_SUCCESS;
    }
    else {
        rv = APR_EGENERAL;
    }

    ms_release_conn(ms, conn);

    return rv;
}

apr_status_t mc_version_ping(apr_memcache_server_t *ms)
{
    apr_status_t rv;
    apr_size_t written;
    struct iovec vec[2];
    apr_memcache_conn_t *conn;

    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    /* version\r\n */
    vec[0].iov_base = MC_VERSION;
    vec[0].iov_len  = MC_VERSION_LEN;

    vec[1].iov_base = MC_EOL;
    vec[1].iov_len  = MC_EOL_LEN;

    rv = apr_socket_sendv(conn->sock, vec, 2, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        return rv;
    }

    rv = get_server_line(conn);
    ms_release_conn(ms, conn);
    return rv;
}


APU_DECLARE(void) 
apr_memcache_add_multget_key(apr_pool_t *data_pool,
                             const char* key,
                             apr_hash_t **values)
{
    apr_memcache_value_t* value;
    apr_size_t klen = strlen(key);

    /* create the value hash if need be */
    if (!*values) {
        *values = apr_hash_make(data_pool);
    }

    /* init key and add it to the value hash */
    value = apr_pcalloc(data_pool, sizeof(apr_memcache_value_t));

    value->status = APR_NOTFOUND;
    value->key = apr_pstrdup(data_pool, key);

    apr_hash_set(*values, value->key, klen, value);
}

static void mget_conn_result(int serverup,
                             int connup,
                             apr_status_t rv,
                             apr_memcache_t *mc,
                             apr_memcache_server_t *ms,
                             apr_memcache_conn_t *conn,
                             struct cache_server_query_t *server_query,
                             apr_hash_t *values,
                             apr_hash_t *server_queries)
{
    apr_int32_t j;
    apr_memcache_value_t* value;
    
    apr_hash_set(server_queries, &ms, sizeof(ms), NULL);

    if (connup) {
        ms_release_conn(ms, conn);
    } else {
        ms_bad_conn(ms, conn);

        if (!serverup) {
            apr_memcache_disable_server(mc, ms);
        }
    }
    
    for (j = 1; j < server_query->query_vec_count ; j+=2) {
        if (server_query->query_vec[j].iov_base) {
            value = apr_hash_get(values, server_query->query_vec[j].iov_base,
                                 strlen(server_query->query_vec[j].iov_base));
            
            if (value->status == APR_NOTFOUND) {
                value->status = rv;
            }
        }
    }
}

APU_DECLARE(apr_status_t)
apr_memcache_multgetp(apr_memcache_t *mc,
                      apr_pool_t *temp_pool,
                      apr_pool_t *data_pool,
                      apr_hash_t *values)
{
    apr_status_t rv;
    apr_memcache_server_t* ms;
    apr_memcache_conn_t* conn;
    apr_uint32_t hash;
    apr_size_t written;
    apr_size_t klen;

    apr_memcache_value_t* value;
    apr_hash_index_t* value_hash_index;

    /* this is a little over aggresive, but beats multiple loops
     * to figure out how long each vector needs to be per-server.
     */
    apr_int32_t veclen = 2 + 2 * apr_hash_count(values) - 1; /* get <key>[<space><key>...]\r\n */
    apr_int32_t i, j;
    apr_int32_t queries_sent;
    apr_int32_t queries_recvd;

    apr_hash_t * server_queries = apr_hash_make(temp_pool);
    struct cache_server_query_t* server_query;
    apr_hash_index_t * query_hash_index;

    apr_pollset_t* pollset;
    const apr_pollfd_t* activefds;
    apr_pollfd_t* pollfds;


    /* build all the queries */
    value_hash_index = apr_hash_first(temp_pool, values);
    while (value_hash_index) {
        void *v;
        apr_hash_this(value_hash_index, NULL, NULL, &v);
        value = v;
        value_hash_index = apr_hash_next(value_hash_index);
        klen = strlen(value->key);

        hash = apr_memcache_hash(mc, value->key, klen);
        ms = apr_memcache_find_server_hash(mc, hash);
        if (ms == NULL) {
            continue;
        }

        server_query = apr_hash_get(server_queries, &ms, sizeof(ms));

        if (!server_query) {
            rv = ms_find_conn(ms, &conn);

            if (rv != APR_SUCCESS) {
                apr_memcache_disable_server(mc, ms);
                value->status = rv;
                continue;
            }

            server_query = apr_pcalloc(temp_pool,sizeof(struct cache_server_query_t));

            apr_hash_set(server_queries, &ms, sizeof(ms), server_query);

            server_query->ms = ms;
            server_query->conn = conn;
            server_query->query_vec = apr_pcalloc(temp_pool, sizeof(struct iovec)*veclen);

            /* set up the first key */
            server_query->query_vec[0].iov_base = MC_GET;
            server_query->query_vec[0].iov_len  = MC_GET_LEN;

            server_query->query_vec[1].iov_base = (void*)(value->key);
            server_query->query_vec[1].iov_len  = klen;

            server_query->query_vec[2].iov_base = MC_EOL;
            server_query->query_vec[2].iov_len  = MC_EOL_LEN;

            server_query->query_vec_count = 3;
        }
        else {
            j = server_query->query_vec_count - 1;

            server_query->query_vec[j].iov_base = MC_WS;
            server_query->query_vec[j].iov_len  = MC_WS_LEN;
            j++;

            server_query->query_vec[j].iov_base = (void*)(value->key);
            server_query->query_vec[j].iov_len  = klen;
            j++;

            server_query->query_vec[j].iov_base = MC_EOL;
            server_query->query_vec[j].iov_len  = MC_EOL_LEN;
            j++;

           server_query->query_vec_count = j;
        }
    }

    /* create polling structures */
    pollfds = apr_pcalloc(temp_pool, apr_hash_count(server_queries) * sizeof(apr_pollfd_t));
    
    rv = apr_pollset_create(&pollset, apr_hash_count(server_queries), temp_pool, 0);

    if (rv != APR_SUCCESS) {
        query_hash_index = apr_hash_first(temp_pool, server_queries);

        while (query_hash_index) {
            void *v;
            apr_hash_this(query_hash_index, NULL, NULL, &v);
            server_query = v;
            query_hash_index = apr_hash_next(query_hash_index);

            mget_conn_result(TRUE, TRUE, rv, mc, server_query->ms, server_query->conn,
                             server_query, values, server_queries);
        }

        return rv;
    }

    /* send all the queries */
    queries_sent = 0;
    query_hash_index = apr_hash_first(temp_pool, server_queries);

    while (query_hash_index) {
        void *v;
        apr_hash_this(query_hash_index, NULL, NULL, &v);
        server_query = v;
        query_hash_index = apr_hash_next(query_hash_index);

        conn = server_query->conn;
        ms = server_query->ms;

        for (i = 0, rv = APR_SUCCESS; i < veclen && rv == APR_SUCCESS; i += APR_MAX_IOVEC_SIZE) {
            rv = apr_socket_sendv(conn->sock, &(server_query->query_vec[i]),
                                  veclen-i>APR_MAX_IOVEC_SIZE ? APR_MAX_IOVEC_SIZE : veclen-i , &written);
        }

        if (rv != APR_SUCCESS) {
            mget_conn_result(FALSE, FALSE, rv, mc, ms, conn,
                             server_query, values, server_queries);
            continue;
        }

        pollfds[queries_sent].desc_type = APR_POLL_SOCKET;
        pollfds[queries_sent].reqevents = APR_POLLIN;
        pollfds[queries_sent].p = temp_pool;
        pollfds[queries_sent].desc.s = conn->sock;
        pollfds[queries_sent].client_data = (void *)server_query;
        apr_pollset_add (pollset, &pollfds[queries_sent]);

        queries_sent++;
    }

    while (queries_sent) {
        rv = apr_pollset_poll(pollset, MULT_GET_TIMEOUT, &queries_recvd, &activefds);

        if (rv != APR_SUCCESS) {
            /* timeout */
            queries_sent = 0;
            continue;
        }
        for (i = 0; i < queries_recvd; i++) {
            server_query = activefds[i].client_data;
            conn = server_query->conn;
            ms = server_query->ms;

           rv = get_server_line(conn);

           if (rv != APR_SUCCESS) {
               apr_pollset_remove (pollset, &activefds[i]);
               mget_conn_result(FALSE, FALSE, rv, mc, ms, conn,
                                server_query, values, server_queries);
               queries_sent--;
               continue;
           }

           if (strncmp(MS_VALUE, conn->buffer, MS_VALUE_LEN) == 0) {
               char *key;
               char *flags;
               char *length;
               char *last;
               char *data;
               apr_size_t len = 0;

               key = apr_strtok(conn->buffer, " ", &last); /* just the VALUE, ignore */
               key = apr_strtok(NULL, " ", &last);
               flags = apr_strtok(NULL, " ", &last);


               length = apr_strtok(NULL, " ", &last);
               if (length) {
                   len = strtol(length, (char **) NULL, 10);
               }

               value = apr_hash_get(values, key, strlen(key));

               
               if (value) {
                   if (len != 0)  {
                       apr_bucket_brigade *bbb;
                       apr_bucket *e;
                       
                       /* eat the trailing \r\n */
                       rv = apr_brigade_partition(conn->bb, len+2, &e);
                       
                       if (rv != APR_SUCCESS) {
                           apr_pollset_remove (pollset, &activefds[i]);
                           mget_conn_result(FALSE, FALSE, rv, mc, ms, conn,
                                            server_query, values, server_queries);
                           queries_sent--;
                           continue;
                       }
                       
                       bbb = apr_brigade_split(conn->bb, e);
                       
                       rv = apr_brigade_pflatten(conn->bb, &data, &len, data_pool);
                       
                       if (rv != APR_SUCCESS) {
                           apr_pollset_remove (pollset, &activefds[i]);
                           mget_conn_result(FALSE, FALSE, rv, mc, ms, conn,
                                            server_query, values, server_queries);
                           queries_sent--;
                           continue;
                       }
                       
                       rv = apr_brigade_destroy(conn->bb);
                       if (rv != APR_SUCCESS) {
                           apr_pollset_remove (pollset, &activefds[i]);
                           mget_conn_result(FALSE, FALSE, rv, mc, ms, conn,
                                            server_query, values, server_queries);
                           queries_sent--;
                           continue;
                       }
                       
                       conn->bb = bbb;
                       
                       value->len = len - 2;
                       data[value->len] = '\0';
                       value->data = data;
                   }
                   
                   value->status = rv;
                   value->flags = atoi(flags);
                   
                   /* stay on the server */
                   i--;
                   
               }
               else {
                   /* TODO: Server Sent back a key I didn't ask for or my
                    *       hash is corrupt */
               }
           }
           else if (strncmp(MS_END, conn->buffer, MS_END_LEN) == 0) {
               /* this connection is done */
               apr_pollset_remove (pollset, &activefds[i]);
               ms_release_conn(ms, conn);
               apr_hash_set(server_queries, &ms, sizeof(ms), NULL);
               
               queries_sent--;
           }
           else {
               /* unknown reply? */
               rv = APR_EGENERAL;
           }
           
        } /* /for */
    } /* /while */
    
    query_hash_index = apr_hash_first(temp_pool, server_queries);
    while (query_hash_index) {
        void *v;
        apr_hash_this(query_hash_index, NULL, NULL, &v);
        server_query = v;
        query_hash_index = apr_hash_next(query_hash_index);
        
        conn = server_query->conn;
        ms = server_query->ms;
        
        mget_conn_result(TRUE, (rv == APR_SUCCESS), rv, mc, ms, conn,
                         server_query, values, server_queries);
        continue;
    }
    
    apr_pollset_destroy(pollset);
    apr_pool_clear(temp_pool);
    return APR_SUCCESS;
    
}



/**
 * Define all of the strings for stats
 */

#define STAT_pid MS_STAT " pid "
#define STAT_pid_LEN (sizeof(STAT_pid)-1)

#define STAT_uptime MS_STAT " uptime "
#define STAT_uptime_LEN (sizeof(STAT_uptime)-1)

#define STAT_time MS_STAT " time "
#define STAT_time_LEN (sizeof(STAT_time)-1)

#define STAT_version MS_STAT " version "
#define STAT_version_LEN (sizeof(STAT_version)-1)

#define STAT_pointer_size MS_STAT " pointer_size "
#define STAT_pointer_size_LEN (sizeof(STAT_pointer_size)-1)

#define STAT_rusage_user MS_STAT " rusage_user "
#define STAT_rusage_user_LEN (sizeof(STAT_rusage_user)-1)

#define STAT_rusage_system MS_STAT " rusage_system "
#define STAT_rusage_system_LEN (sizeof(STAT_rusage_system)-1)

#define STAT_curr_items MS_STAT " curr_items "
#define STAT_curr_items_LEN (sizeof(STAT_curr_items)-1)

#define STAT_total_items MS_STAT " total_items "
#define STAT_total_items_LEN (sizeof(STAT_total_items)-1)

#define STAT_bytes MS_STAT " bytes "
#define STAT_bytes_LEN (sizeof(STAT_bytes)-1)

#define STAT_curr_connections MS_STAT " curr_connections "
#define STAT_curr_connections_LEN (sizeof(STAT_curr_connections)-1)

#define STAT_total_connections MS_STAT " total_connections "
#define STAT_total_connections_LEN (sizeof(STAT_total_connections)-1)

#define STAT_connection_structures MS_STAT " connection_structures "
#define STAT_connection_structures_LEN (sizeof(STAT_connection_structures)-1)

#define STAT_cmd_get MS_STAT " cmd_get "
#define STAT_cmd_get_LEN (sizeof(STAT_cmd_get)-1)

#define STAT_cmd_set MS_STAT " cmd_set "
#define STAT_cmd_set_LEN (sizeof(STAT_cmd_set)-1)

#define STAT_get_hits MS_STAT " get_hits "
#define STAT_get_hits_LEN (sizeof(STAT_get_hits)-1)

#define STAT_get_misses MS_STAT " get_misses "
#define STAT_get_misses_LEN (sizeof(STAT_get_misses)-1)

#define STAT_evictions MS_STAT " evictions "
#define STAT_evictions_LEN (sizeof(STAT_evictions)-1)

#define STAT_bytes_read MS_STAT " bytes_read "
#define STAT_bytes_read_LEN (sizeof(STAT_bytes_read)-1)

#define STAT_bytes_written MS_STAT " bytes_written "
#define STAT_bytes_written_LEN (sizeof(STAT_bytes_written)-1)

#define STAT_limit_maxbytes MS_STAT " limit_maxbytes "
#define STAT_limit_maxbytes_LEN (sizeof(STAT_limit_maxbytes)-1)

#define STAT_threads MS_STAT " threads "
#define STAT_threads_LEN (sizeof(STAT_threads)-1)

static const char *stat_read_string(apr_pool_t *p, char *buf, apr_size_t len)
{
    /* remove trailing \r\n and null char */
    return apr_pstrmemdup(p, buf, len-2);
}

static apr_uint32_t stat_read_uint32(apr_pool_t *p, char *buf, apr_size_t  len)
{
    buf[len-2] = '\0';
    return atoi(buf);
}

static apr_uint64_t stat_read_uint64(apr_pool_t *p, char *buf, apr_size_t  len)
{
    buf[len-2] = '\0';
    return apr_atoi64(buf);
}

static apr_time_t stat_read_time(apr_pool_t *p, char *buf, apr_size_t  len)
{
    buf[len-2] = '\0';
    return apr_time_from_sec(atoi(buf));
}

static apr_time_t stat_read_rtime(apr_pool_t *p, char *buf, apr_size_t  len)
{
    char *tok;
    char *secs;
    char *usecs;
    const char *sep = ":.";

    buf[len-2] = '\0';

    secs = apr_strtok(buf, sep, &tok);
    usecs = apr_strtok(NULL, sep, &tok);
    if (secs && usecs) {
        return apr_time_make(atoi(secs), atoi(usecs));
    }
    else {
        return apr_time_make(0, 0);
    }
}

/**
 * I got tired of Typing. Meh. 
 *
 * TODO: Convert it to static tables to make it cooler.
 */

#define mc_stat_cmp(name) \
    strncmp(STAT_ ## name, conn->buffer, STAT_ ## name ## _LEN) == 0

#define mc_stat_str(name) \
    stat_read_string(p, conn->buffer + name, \
                     conn->blen - name)

#define mc_stat_uint32(name) \
    stat_read_uint32(p, conn->buffer + name, \
                     conn->blen - name)

#define mc_stat_uint64(name) \
    stat_read_uint64(p, conn->buffer + name, \
                     conn->blen - name)

#define mc_stat_time(name) \
    stat_read_time(p, conn->buffer + name, \
                     conn->blen - name)

#define mc_stat_rtime(name) \
    stat_read_rtime(p, conn->buffer + name, \
                     conn->blen - name)


#define mc_do_stat(name, type) \
    if (mc_stat_cmp(name)) { \
        stats-> name = mc_stat_ ## type ((STAT_ ## name ## _LEN)); \
    } 

static void update_stats(apr_pool_t *p, apr_memcache_conn_t *conn, 
                         apr_memcache_stats_t *stats)
{

    mc_do_stat(version, str)
    else mc_do_stat(pid, uint32)
    else mc_do_stat(uptime, uint32)
    else mc_do_stat(pointer_size, uint32)
    else mc_do_stat(time, time)
    else mc_do_stat(rusage_user, rtime)
    else mc_do_stat(rusage_system, rtime)
    else mc_do_stat(curr_items, uint32)
    else mc_do_stat(total_items, uint32)
    else mc_do_stat(bytes, uint64)
    else mc_do_stat(curr_connections, uint32)
    else mc_do_stat(total_connections, uint32)
    else mc_do_stat(connection_structures, uint32)
    else mc_do_stat(cmd_get, uint32)
    else mc_do_stat(cmd_set, uint32)
    else mc_do_stat(get_hits, uint32)
    else mc_do_stat(get_misses, uint32)
    else mc_do_stat(evictions, uint64)
    else mc_do_stat(bytes_read, uint64)
    else mc_do_stat(bytes_written, uint64)
    else mc_do_stat(limit_maxbytes, uint32)
    else mc_do_stat(threads, uint32)
}

APU_DECLARE(apr_status_t)
apr_memcache_stats(apr_memcache_server_t *ms,
                  apr_pool_t *p,
                  apr_memcache_stats_t **stats) 
{
    apr_memcache_stats_t *ret;
    apr_status_t rv;
    apr_memcache_conn_t *conn;
    apr_size_t written;
    struct iovec vec[2];

    rv = ms_find_conn(ms, &conn);

    if (rv != APR_SUCCESS) {
        return rv;
    }

    /* version\r\n */
    vec[0].iov_base = MC_STATS;
    vec[0].iov_len  = MC_STATS_LEN;

    vec[1].iov_base = MC_EOL;
    vec[1].iov_len  = MC_EOL_LEN;

    rv = apr_socket_sendv(conn->sock, vec, 2, &written);

    if (rv != APR_SUCCESS) {
        ms_bad_conn(ms, conn);
        return rv;
    }

    ret = apr_pcalloc(p, sizeof(apr_memcache_stats_t));

    do {
        rv = get_server_line(conn);
        if (rv != APR_SUCCESS) {
            ms_bad_conn(ms, conn);
            return rv;
        }

        if (strncmp(MS_END, conn->buffer, MS_END_LEN) == 0) {
            rv = APR_SUCCESS;
            break;
        }
        else if (strncmp(MS_STAT, conn->buffer, MS_STAT_LEN) == 0) {
            update_stats(p, conn, ret);
            continue;
        }
        else {
            rv = APR_EGENERAL;
            break;
        }

    } while(1);

    ms_release_conn(ms, conn);

    if (stats) {
        *stats = ret;
    }

    return rv;
}

