/*
 * services/cache/infra.h - infrastructure cache, server rtt and capabilities
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains the infrastructure cache, as well as rate limiting.
 * Note that there are two sorts of rate-limiting here:
 *  - Pre-cache, per-query rate limiting (query ratelimits)
 *  - Post-cache, per-domain name rate limiting (infra-ratelimits)
 */

#ifndef SERVICES_CACHE_INFRA_H
#define SERVICES_CACHE_INFRA_H
#include "util/storage/lruhash.h"
#include "util/storage/dnstree.h"
#include "util/rtt.h"
#include "util/netevent.h"
#include "util/data/msgreply.h"
struct slabhash;
struct config_file;

/**
 * Host information kept for every server, per zone.
 */
struct infra_key {
	/** the host address. */
	struct sockaddr_storage addr;
	/** length of addr. */
	socklen_t addrlen;
	/** zone name in wireformat */
	uint8_t* zonename;
	/** length of zonename */
	size_t namelen;
	/** hash table entry, data of type infra_data. */
	struct lruhash_entry entry;
};

/**
 * Host information encompasses host capabilities and retransmission timeouts.
 * And lameness information (notAuthoritative, noEDNS, Recursive)
 */
struct infra_data {
	/** TTL value for this entry. absolute time. */
	time_t ttl;

	/** time in seconds (absolute) when probing re-commences, 0 disabled */
	time_t probedelay;
	/** round trip times for timeout calculation */
	struct rtt_info rtt;

	/** edns version that the host supports, -1 means no EDNS */
	int edns_version;
	/** if the EDNS lameness is already known or not.
	 * EDNS lame is when EDNS queries or replies are dropped, 
	 * and cause a timeout */
	uint8_t edns_lame_known;

	/** is the host lame (does not serve the zone authoritatively),
	 * or is the host dnssec lame (does not serve DNSSEC data) */
	uint8_t isdnsseclame;
	/** is the host recursion lame (not AA, but RA) */
	uint8_t rec_lame;
	/** the host is lame (not authoritative) for A records */
	uint8_t lame_type_A;
	/** the host is lame (not authoritative) for other query types */
	uint8_t lame_other;

	/** timeouts counter for type A */
	uint8_t timeout_A;
	/** timeouts counter for type AAAA */
	uint8_t timeout_AAAA;
	/** timeouts counter for others */
	uint8_t timeout_other;
};

/**
 * Infra cache 
 */
struct infra_cache {
	/** The hash table with hosts */
	struct slabhash* hosts;
	/** TTL value for host information, in seconds */
	int host_ttl;
	/** hash table with query rates per name: rate_key, rate_data */
	struct slabhash* domain_rates;
	/** ratelimit settings for domains, struct domain_limit_data */
	rbtree_type domain_limits;
	/** hash table with query rates per client ip: ip_rate_key, ip_rate_data */
	struct slabhash* client_ip_rates;
};

/** ratelimit, unless overridden by domain_limits, 0 is off */
extern int infra_dp_ratelimit;

/**
 * ratelimit settings for domains
 */
struct domain_limit_data {
	/** key for rbtree, must be first in struct, name of domain */
	struct name_tree_node node;
	/** ratelimit for exact match with this name, -1 if not set */
	int lim;
	/** ratelimit for names below this name, -1 if not set */
	int below;
};

/**
 * key for ratelimit lookups, a domain name
 */
struct rate_key {
	/** lruhash key entry */
	struct lruhash_entry entry;
	/** domain name in uncompressed wireformat */
	uint8_t* name;
	/** length of name */
	size_t namelen;
};

/** ip ratelimit, 0 is off */
extern int infra_ip_ratelimit;

/**
 * key for ip_ratelimit lookups, a source IP.
 */
struct ip_rate_key {
	/** lruhash key entry */
	struct lruhash_entry entry;
	/** client ip information */
	struct sockaddr_storage addr;
	/** length of address */
	socklen_t addrlen;
};

/** number of seconds to track qps rate */
#define RATE_WINDOW 2

/**
 * Data for ratelimits per domain name
 * It is incremented when a non-cache-lookup happens for that domain name.
 * The name is the delegation point we have for the name.
 * If a new delegation point is found (a referral reply), the previous
 * delegation point is decremented, and the new one is charged with the query.
 */
struct rate_data {
	/** queries counted, for that second. 0 if not in use. */
	int qps[RATE_WINDOW];
	/** what the timestamp is of the qps array members, counter is
	 * valid for that timestamp.  Usually now and now-1. */
	time_t timestamp[RATE_WINDOW];
};

#define ip_rate_data rate_data

/** infra host cache default hash lookup size */
#define INFRA_HOST_STARTSIZE 32
/** bytes per zonename reserved in the hostcache, dnamelen(zonename.com.) */
#define INFRA_BYTES_NAME 14

/**
 * Create infra cache.
 * @param cfg: config parameters or NULL for defaults.
 * @return: new infra cache, or NULL.
 */
struct infra_cache* infra_create(struct config_file* cfg);

/**
 * Delete infra cache.
 * @param infra: infrastructure cache to delete.
 */
void infra_delete(struct infra_cache* infra);

/**
 * Adjust infra cache to use updated configuration settings.
 * This may clean the cache. Operates a bit like realloc.
 * There may be no threading or use by other threads.
 * @param infra: existing cache. If NULL a new infra cache is returned.
 * @param cfg: config options.
 * @return the new infra cache pointer or NULL on error.
 */
struct infra_cache* infra_adjust(struct infra_cache* infra, 
	struct config_file* cfg);

/**
 * Plain find infra data function (used by the the other functions)
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: domain name of zone.
 * @param namelen: length of domain name.
 * @param wr: if true, writelock, else readlock.
 * @return the entry, could be expired (this is not checked) or NULL.
 */
struct lruhash_entry* infra_lookup_nottl(struct infra_cache* infra,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* name,
	size_t namelen, int wr);

/**
 * Find host information to send a packet. Creates new entry if not found.
 * Lameness is empty. EDNS is 0 (try with first), and rtt is returned for 
 * the first message to it.
 * Use this to send a packet only, because it also locks out others when
 * probing is restricted.
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: domain name of zone.
 * @param namelen: length of domain name.
 * @param timenow: what time it is now.
 * @param edns_vs: edns version it supports, is returned.
 * @param edns_lame_known: if EDNS lame (EDNS is dropped in transit) has
 * 	already been probed, is returned.
 * @param to: timeout to use, is returned.
 * @return: 0 on error.
 */
int infra_host(struct infra_cache* infra, struct sockaddr_storage* addr, 
	socklen_t addrlen, uint8_t* name, size_t namelen,
	time_t timenow, int* edns_vs, uint8_t* edns_lame_known, int* to);

/**
 * Set a host to be lame for the given zone.
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: domain name of zone apex.
 * @param namelen: length of domain name.
 * @param timenow: what time it is now.
 * @param dnsseclame: if true the host is set dnssec lame.
 *	if false, the host is marked lame (not serving the zone).
 * @param reclame: if true host is a recursor not AA server.
 *      if false, dnsseclame or marked lame.
 * @param qtype: the query type for which it is lame.
 * @return: 0 on error.
 */
int infra_set_lame(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* name, size_t namelen, time_t timenow, int dnsseclame,
	int reclame, uint16_t qtype);

/**
 * Update rtt information for the host.
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: zone name
 * @param namelen: zone name length
 * @param qtype: query type.
 * @param roundtrip: estimate of roundtrip time in milliseconds or -1 for 
 * 	timeout.
 * @param orig_rtt: original rtt for the query that timed out (roundtrip==-1).
 * 	ignored if roundtrip != -1.
 * @param timenow: what time it is now.
 * @return: 0 on error. new rto otherwise.
 */
int infra_rtt_update(struct infra_cache* infra, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* name, size_t namelen, int qtype,
	int roundtrip, int orig_rtt, time_t timenow);

/**
 * Update information for the host, store that a TCP transaction works.
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: name of zone
 * @param namelen: length of name
 */
void infra_update_tcp_works(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* name, size_t namelen);

/**
 * Update edns information for the host.
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: name of zone
 * @param namelen: length of name
 * @param edns_version: the version that it publishes.
 * 	If it is known to support EDNS then no-EDNS is not stored over it.
 * @param timenow: what time it is now.
 * @return: 0 on error.
 */
int infra_edns_update(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* name, size_t namelen, int edns_version, time_t timenow);

/**
 * Get Lameness information and average RTT if host is in the cache.
 * This information is to be used for server selection.
 * @param infra: infrastructure cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: zone name.
 * @param namelen: zone name length.
 * @param qtype: the query to be made.
 * @param lame: if function returns true, this returns lameness of the zone.
 * @param dnsseclame: if function returns true, this returns if the zone
 *	is dnssec-lame.
 * @param reclame: if function returns true, this is if it is recursion lame.
 * @param rtt: if function returns true, this returns avg rtt of the server.
 * 	The rtt value is unclamped and reflects recent timeouts.
 * @param timenow: what time it is now.
 * @return if found in cache, or false if not (or TTL bad).
 */
int infra_get_lame_rtt(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen, 
	uint8_t* name, size_t namelen, uint16_t qtype, 
	int* lame, int* dnsseclame, int* reclame, int* rtt, time_t timenow);

/**
 * Get additional (debug) info on timing.
 * @param infra: infra cache.
 * @param addr: host address.
 * @param addrlen: length of addr.
 * @param name: zone name
 * @param namelen: zone name length
 * @param rtt: the rtt_info is copied into here (caller alloced return struct).
 * @param delay: probe delay (if any).
 * @param timenow: what time it is now.
 * @param tA: timeout counter on type A.
 * @param tAAAA: timeout counter on type AAAA.
 * @param tother: timeout counter on type other.
 * @return TTL the infra host element is valid for. If -1: not found in cache.
 *	TTL -2: found but expired.
 */
long long infra_get_host_rto(struct infra_cache* infra,
        struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* name,
	size_t namelen, struct rtt_info* rtt, int* delay, time_t timenow,
	int* tA, int* tAAAA, int* tother);

/**
 * Increment the query rate counter for a delegation point.
 * @param infra: infra cache.
 * @param name: zone name
 * @param namelen: zone name length
 * @param timenow: what time it is now.
 * @return 1 if it could be incremented. 0 if the increment overshot the
 * ratelimit or if in the previous second the ratelimit was exceeded.
 * Failures like alloc failures are not returned (probably as 1).
 */
int infra_ratelimit_inc(struct infra_cache* infra, uint8_t* name,
	size_t namelen, time_t timenow);

/**
 * Decrement the query rate counter for a delegation point.
 * Because the reply received for the delegation point was pleasant,
 * we do not charge this delegation point with it (i.e. it was a referral).
 * Should call it with same second as when inc() was called.
 * @param infra: infra cache.
 * @param name: zone name
 * @param namelen: zone name length
 * @param timenow: what time it is now.
 */
void infra_ratelimit_dec(struct infra_cache* infra, uint8_t* name,
	size_t namelen, time_t timenow);

/**
 * See if the query rate counter for a delegation point is exceeded.
 * So, no queries are going to be allowed.
 * @param infra: infra cache.
 * @param name: zone name
 * @param namelen: zone name length
 * @param timenow: what time it is now.
 * @return true if exceeded.
 */
int infra_ratelimit_exceeded(struct infra_cache* infra, uint8_t* name,
	size_t namelen, time_t timenow);

/** find the maximum rate stored, not too old. 0 if no information. */
int infra_rate_max(void* data, time_t now);

/** find the ratelimit in qps for a domain. 0 if no limit for domain. */
int infra_find_ratelimit(struct infra_cache* infra, uint8_t* name,
	size_t namelen);

/** Update query ratelimit hash and decide
 *  whether or not a query should be dropped.
 *  @param infra: infra cache
 *  @param repinfo: information about client
 *  @param timenow: what time it is now.
 *  @return 1 if it could be incremented. 0 if the increment overshot the
 *  ratelimit and the query should be dropped. */
int infra_ip_ratelimit_inc(struct infra_cache* infra,
	struct comm_reply* repinfo, time_t timenow);

/**
 * Get memory used by the infra cache.
 * @param infra: infrastructure cache.
 * @return memory in use in bytes.
 */
size_t infra_get_mem(struct infra_cache* infra);

/** calculate size for the hashtable, does not count size of lameness,
 * so the hashtable is a fixed number of items */
size_t infra_sizefunc(void* k, void* d);

/** compare two addresses, returns -1, 0, or +1 */
int infra_compfunc(void* key1, void* key2);

/** delete key, and destroy the lock */
void infra_delkeyfunc(void* k, void* arg);

/** delete data and destroy the lameness hashtable */
void infra_deldatafunc(void* d, void* arg);

/** calculate size for the hashtable */
size_t rate_sizefunc(void* k, void* d);

/** compare two names, returns -1, 0, or +1 */
int rate_compfunc(void* key1, void* key2);

/** delete key, and destroy the lock */
void rate_delkeyfunc(void* k, void* arg);

/** delete data */
void rate_deldatafunc(void* d, void* arg);

/* calculate size for the client ip hashtable */
size_t ip_rate_sizefunc(void* k, void* d);

/* compare two addresses */
int ip_rate_compfunc(void* key1, void* key2);

/* delete key, and destroy the lock */
void ip_rate_delkeyfunc(void* d, void* arg);

/* delete data */
#define ip_rate_deldatafunc rate_deldatafunc

#endif /* SERVICES_CACHE_INFRA_H */
