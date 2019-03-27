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

#ifndef APR_MEMCACHE_H
#define APR_MEMCACHE_H

/**
 * @file apr_memcache.h
 * @brief Client interface for memcached
 * @remark To use this interface you must have a separate memcached
 * server running. See the memcached website at http://www.danga.com/memcached/
 * for more information.
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_time.h"
#include "apr_strings.h"
#include "apr_network_io.h"
#include "apr_ring.h"
#include "apr_buckets.h"
#include "apr_reslist.h"
#include "apr_hash.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup APR_Util_MC Memcached Client Routines
 * @ingroup APR_Util
 * @{
 */

/** Specifies the status of a memcached server */
typedef enum
{
    APR_MC_SERVER_LIVE, /**< Server is alive and responding to requests */
    APR_MC_SERVER_DEAD  /**< Server is not responding to requests */
} apr_memcache_server_status_t;

/** Opaque memcache client connection object */
typedef struct apr_memcache_conn_t apr_memcache_conn_t;

/** Memcache Server Info Object */
typedef struct apr_memcache_server_t apr_memcache_server_t;
struct apr_memcache_server_t
{
    const char *host; /**< Hostname of this Server */
    apr_port_t port; /**< Port of this Server */
    apr_memcache_server_status_t status; /**< @see apr_memcache_server_status_t */
#if APR_HAS_THREADS || defined(DOXYGEN)
    apr_reslist_t *conns; /**< Resource list of actual client connections */
#else
    apr_memcache_conn_t *conn;
#endif
    apr_pool_t *p; /** Pool to use for private allocations */
#if APR_HAS_THREADS
    apr_thread_mutex_t *lock;
#endif
    apr_time_t btime;
};

/* Custom hash callback function prototype, user for server selection.
* @param baton user selected baton
* @param data data to hash
* @param data_len length of data
*/
typedef apr_uint32_t (*apr_memcache_hash_func)(void *baton,
                                               const char *data,
                                               const apr_size_t data_len);

typedef struct apr_memcache_t apr_memcache_t;

/* Custom Server Select callback function prototype.
* @param baton user selected baton
* @param mc memcache instance, use mc->live_servers to select a node
* @param hash hash of the selected key.
*/
typedef apr_memcache_server_t* (*apr_memcache_server_func)(void *baton,
                                                 apr_memcache_t *mc,
                                                 const apr_uint32_t hash);

/** Container for a set of memcached servers */
struct apr_memcache_t
{
    apr_uint32_t flags; /**< Flags, Not currently used */
    apr_uint16_t nalloc; /**< Number of Servers Allocated */
    apr_uint16_t ntotal; /**< Number of Servers Added */
    apr_memcache_server_t **live_servers; /**< Array of Servers */
    apr_pool_t *p; /** Pool to use for allocations */
    void *hash_baton;
    apr_memcache_hash_func hash_func;
    void *server_baton;
    apr_memcache_server_func server_func;
};

/** Returned Data from a multiple get */
typedef struct
{
    apr_status_t status;
    const char* key;
    apr_size_t len;
    char *data;
    apr_uint16_t flags;
} apr_memcache_value_t;

/**
 * Creates a crc32 hash used to split keys between servers
 * @param mc The memcache client object to use
 * @param data Data to be hashed
 * @param data_len Length of the data to use
 * @return crc32 hash of data
 * @remark The crc32 hash is not compatible with old memcached clients.
 */
APU_DECLARE(apr_uint32_t) apr_memcache_hash(apr_memcache_t *mc,
                                            const char *data,
                                            const apr_size_t data_len);

/**
 * Pure CRC32 Hash. Used by some clients.
 */
APU_DECLARE(apr_uint32_t) apr_memcache_hash_crc32(void *baton,
                                                  const char *data,
                                                  const apr_size_t data_len);

/**
 * hash compatible with the standard Perl Client.
 */
APU_DECLARE(apr_uint32_t) apr_memcache_hash_default(void *baton,
                                                    const char *data,
                                                    const apr_size_t data_len);

/**
 * Picks a server based on a hash
 * @param mc The memcache client object to use
 * @param hash Hashed value of a Key
 * @return server that controls specified hash
 * @see apr_memcache_hash
 */
APU_DECLARE(apr_memcache_server_t *) apr_memcache_find_server_hash(apr_memcache_t *mc,
                                                                   const apr_uint32_t hash);

/**
 * server selection compatible with the standard Perl Client.
 */
APU_DECLARE(apr_memcache_server_t *) apr_memcache_find_server_hash_default(void *baton,
                                                                           apr_memcache_t *mc, 
                                                                           const apr_uint32_t hash);

/**
 * Adds a server to a client object
 * @param mc The memcache client object to use
 * @param server Server to add
 * @remark Adding servers is not thread safe, and should be done once at startup.
 * @warning Changing servers after startup may cause keys to go to
 * different servers.
 */
APU_DECLARE(apr_status_t) apr_memcache_add_server(apr_memcache_t *mc,
                                                  apr_memcache_server_t *server);


/**
 * Finds a Server object based on a hostname/port pair
 * @param mc The memcache client object to use
 * @param host Hostname of the server
 * @param port Port of the server
 * @return Server with matching Hostname and Port, or NULL if none was found.
 */
APU_DECLARE(apr_memcache_server_t *) apr_memcache_find_server(apr_memcache_t *mc,
                                                              const char *host,
                                                              apr_port_t port);

/**
 * Enables a Server for use again
 * @param mc The memcache client object to use
 * @param ms Server to Activate
 */
APU_DECLARE(apr_status_t) apr_memcache_enable_server(apr_memcache_t *mc,
                                                     apr_memcache_server_t *ms);


/**
 * Disable a Server
 * @param mc The memcache client object to use
 * @param ms Server to Disable
 */
APU_DECLARE(apr_status_t) apr_memcache_disable_server(apr_memcache_t *mc,
                                                      apr_memcache_server_t *ms);

/**
 * Creates a new Server Object
 * @param p Pool to use
 * @param host hostname of the server
 * @param port port of the server
 * @param min  minimum number of client sockets to open
 * @param smax soft maximum number of client connections to open
 * @param max  hard maximum number of client connections
 * @param ttl  time to live in microseconds of a client connection
 * @param ns   location of the new server object
 * @see apr_reslist_create
 * @remark min, smax, and max are only used when APR_HAS_THREADS
 */
APU_DECLARE(apr_status_t) apr_memcache_server_create(apr_pool_t *p,
                                                     const char *host,
                                                     apr_port_t port,
                                                     apr_uint32_t min,
                                                     apr_uint32_t smax,
                                                     apr_uint32_t max,
                                                     apr_uint32_t ttl,
                                                     apr_memcache_server_t **ns);
/**
 * Creates a new memcached client object
 * @param p Pool to use
 * @param max_servers maximum number of servers
 * @param flags Not currently used
 * @param mc   location of the new memcache client object
 */
APU_DECLARE(apr_status_t) apr_memcache_create(apr_pool_t *p,
                                              apr_uint16_t max_servers,
                                              apr_uint32_t flags,
                                              apr_memcache_t **mc);

/**
 * Gets a value from the server, allocating the value out of p
 * @param mc client to use
 * @param p Pool to use
 * @param key null terminated string containing the key
 * @param baton location of the allocated value
 * @param len   length of data at baton
 * @param flags any flags set by the client for this key
 * @return 
 */
APU_DECLARE(apr_status_t) apr_memcache_getp(apr_memcache_t *mc, 
                                            apr_pool_t *p,
                                            const char* key,
                                            char **baton,
                                            apr_size_t *len,
                                            apr_uint16_t *flags);


/**
 * Add a key to a hash for a multiget query
 *  if the hash (*value) is NULL it will be created
 * @param data_pool pool from where the hash and their items are created from
 * @param key null terminated string containing the key
 * @param values hash of keys and values that this key will be added to
 * @return
 */
APU_DECLARE(void) apr_memcache_add_multget_key(apr_pool_t *data_pool,
                                               const char* key,
                                               apr_hash_t **values);

/**
 * Gets multiple values from the server, allocating the values out of p
 * @param mc client to use
 * @param temp_pool Pool used for temporary allocations. May be cleared inside this
 *        call.
 * @param data_pool Pool used to allocate data for the returned values.
 * @param values hash of apr_memcache_value_t keyed by strings, contains the
 *        result of the multiget call.
 * @return
 */
APU_DECLARE(apr_status_t) apr_memcache_multgetp(apr_memcache_t *mc,
                                                apr_pool_t *temp_pool,
                                                apr_pool_t *data_pool,
                                                apr_hash_t *values);

/**
 * Sets a value by key on the server
 * @param mc client to use
 * @param key   null terminated string containing the key
 * @param baton data to store on the server
 * @param data_size   length of data at baton
 * @param timeout time in seconds for the data to live on the server
 * @param flags any flags set by the client for this key
 */
APU_DECLARE(apr_status_t) apr_memcache_set(apr_memcache_t *mc,
                                           const char *key,
                                           char *baton,
                                           const apr_size_t data_size,
                                           apr_uint32_t timeout,
                                           apr_uint16_t flags);

/**
 * Adds value by key on the server
 * @param mc client to use
 * @param key   null terminated string containing the key
 * @param baton data to store on the server
 * @param data_size   length of data at baton
 * @param timeout time for the data to live on the server
 * @param flags any flags set by the client for this key
 * @return APR_SUCCESS if the key was added, APR_EEXIST if the key 
 * already exists on the server.
 */
APU_DECLARE(apr_status_t) apr_memcache_add(apr_memcache_t *mc,
                                           const char *key,
                                           char *baton,
                                           const apr_size_t data_size,
                                           apr_uint32_t timeout,
                                           apr_uint16_t flags);

/**
 * Replaces value by key on the server
 * @param mc client to use
 * @param key   null terminated string containing the key
 * @param baton data to store on the server
 * @param data_size   length of data at baton
 * @param timeout time for the data to live on the server
 * @param flags any flags set by the client for this key
 * @return APR_SUCCESS if the key was added, APR_EEXIST if the key 
 * did not exist on the server.
 */
APU_DECLARE(apr_status_t) apr_memcache_replace(apr_memcache_t *mc,
                                               const char *key,
                                               char *baton,
                                               const apr_size_t data_size,
                                               apr_uint32_t timeout,
                                               apr_uint16_t flags);
/**
 * Deletes a key from a server
 * @param mc client to use
 * @param key   null terminated string containing the key
 * @param timeout time for the delete to stop other clients from adding
 */
APU_DECLARE(apr_status_t) apr_memcache_delete(apr_memcache_t *mc,
                                              const char *key,
                                              apr_uint32_t timeout);

/**
 * Increments a value
 * @param mc client to use
 * @param key   null terminated string containing the key
 * @param n     number to increment by
 * @param nv    new value after incrementing
 */
APU_DECLARE(apr_status_t) apr_memcache_incr(apr_memcache_t *mc, 
                                            const char *key,
                                            apr_int32_t n,
                                            apr_uint32_t *nv);

/**
 * Decrements a value
 * @param mc client to use
 * @param key   null terminated string containing the key
 * @param n     number to decrement by
 * @param new_value    new value after decrementing
 */
APU_DECLARE(apr_status_t) apr_memcache_decr(apr_memcache_t *mc, 
                                            const char *key,
                                            apr_int32_t n,
                                            apr_uint32_t *new_value);

/**
 * Query a server's version
 * @param ms    server to query
 * @param p     Pool to allocate answer from
 * @param baton location to store server version string
 * @param len   length of the server version string
 */
APU_DECLARE(apr_status_t) apr_memcache_version(apr_memcache_server_t *ms,
                                               apr_pool_t *p,
                                               char **baton);

typedef struct
{
    /** Version string of this server */
    const char *version;
    /** Process id of this server process */
    apr_uint32_t pid;
    /** Number of seconds this server has been running */
    apr_uint32_t uptime;
    /** current UNIX time according to the server */
    apr_time_t time;
    /** The size of a pointer on the current machine */
    apr_uint32_t pointer_size;
    /** Accumulated user time for this process */
    apr_time_t rusage_user;
    /** Accumulated system time for this process */
    apr_time_t rusage_system;
    /** Current number of items stored by the server */
    apr_uint32_t curr_items;
    /** Total number of items stored by this server */
    apr_uint32_t total_items;
    /** Current number of bytes used by this server to store items */
    apr_uint64_t bytes;
    /** Number of open connections */
    apr_uint32_t curr_connections;
    /** Total number of connections opened since the server started running */
    apr_uint32_t total_connections;
    /** Number of connection structures allocated by the server */
    apr_uint32_t connection_structures;
    /** Cumulative number of retrieval requests */
    apr_uint32_t cmd_get;
    /** Cumulative number of storage requests */
    apr_uint32_t cmd_set;
    /** Number of keys that have been requested and found present */
    apr_uint32_t get_hits;
    /** Number of items that have been requested and not found */
    apr_uint32_t get_misses;
    /** Number of items removed from cache because they passed their
        expiration time */
    apr_uint64_t evictions;
    /** Total number of bytes read by this server */
    apr_uint64_t bytes_read;
    /** Total number of bytes sent by this server */
    apr_uint64_t bytes_written;
    /** Number of bytes this server is allowed to use for storage. */
    apr_uint32_t limit_maxbytes;
    /** Number of threads the server is running (if built with threading) */
    apr_uint32_t threads; 
} apr_memcache_stats_t;

/**
 * Query a server for statistics
 * @param ms    server to query
 * @param p     Pool to allocate answer from
 * @param stats location of the new statistics structure
 */
APU_DECLARE(apr_status_t) apr_memcache_stats(apr_memcache_server_t *ms, 
                                             apr_pool_t *p,
                                             apr_memcache_stats_t **stats);


/** @} */

#ifdef __cplusplus
}
#endif

#endif /* APR_MEMCACHE_H */
