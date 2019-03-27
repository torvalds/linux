/*
 * unbound.h - unbound validating resolver public API
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
 * This file contains functions to resolve DNS queries and 
 * validate the answers. Synchronously and asynchronously.
 *
 * Several ways to use this interface from an application wishing
 * to perform (validated) DNS lookups.
 *
 * All start with
 *	ctx = ub_ctx_create();
 *	err = ub_ctx_add_ta(ctx, "...");
 *	err = ub_ctx_add_ta(ctx, "...");
 *	... some lookups
 *	... call ub_ctx_delete(ctx); when you want to stop.
 *
 * Application not threaded. Blocking.
 *	int err = ub_resolve(ctx, "www.example.com", ...
 *	if(err) fprintf(stderr, "lookup error: %s\n", ub_strerror(err));
 *	... use the answer
 *
 * Application not threaded. Non-blocking ('asynchronous').
 *      err = ub_resolve_async(ctx, "www.example.com", ... my_callback);
 *	... application resumes processing ...
 *	... and when either ub_poll(ctx) is true
 *	... or when the file descriptor ub_fd(ctx) is readable,
 *	... or whenever, the app calls ...
 *	ub_process(ctx);
 *	... if no result is ready, the app resumes processing above,
 *	... or process() calls my_callback() with results.
 *
 *      ... if the application has nothing more to do, wait for answer
 *      ub_wait(ctx); 
 *
 * Application threaded. Blocking.
 *	Blocking, same as above. The current thread does the work.
 *	Multiple threads can use the *same context*, each does work and uses
 *	shared cache data from the context.
 *
 * Application threaded. Non-blocking ('asynchronous').
 *	... setup threaded-asynchronous config option
 *	err = ub_ctx_async(ctx, 1);
 *	... same as async for non-threaded
 *	... the callbacks are called in the thread that calls process(ctx)
 *
 * Openssl needs to have locking in place, and the application must set
 * it up, because a mere library cannot do this, use the calls
 * CRYPTO_set_id_callback and CRYPTO_set_locking_callback.
 *
 * If no threading is compiled in, the above async example uses fork(2) to
 * create a process to perform the work. The forked process exits when the 
 * calling process exits, or ctx_delete() is called.
 * Otherwise, for asynchronous with threading, a worker thread is created.
 *
 * The blocking calls use shared ctx-cache when threaded. Thus
 * ub_resolve() and ub_resolve_async() && ub_wait() are
 * not the same. The first makes the current thread do the work, setting
 * up buffers, etc, to perform the work (but using shared cache data).
 * The second calls another worker thread (or process) to perform the work.
 * And no buffers need to be set up, but a context-switch happens.
 */
#ifndef _UB_UNBOUND_H
#define _UB_UNBOUND_H

#ifdef __cplusplus
extern "C" {
#endif

/** the version of this header file */
#define UNBOUND_VERSION_MAJOR @UNBOUND_VERSION_MAJOR@
#define UNBOUND_VERSION_MINOR @UNBOUND_VERSION_MINOR@
#define UNBOUND_VERSION_MICRO @UNBOUND_VERSION_MICRO@

/**
 * The validation context is created to hold the resolver status,
 * validation keys and a small cache (containing messages, rrsets,
 * roundtrip times, trusted keys, lameness information).
 *
 * Its contents are internally defined.
 */
struct ub_ctx;

/**
 * The validation and resolution results.
 * Allocated by the resolver, and need to be freed by the application
 * with ub_resolve_free().
 */
struct ub_result {
	/** The original question, name text string. */
	char* qname;
	/** the type asked for */
	int qtype;
	/** the class asked for */
	int qclass;

	/** 
	 * a list of network order DNS rdata items, terminated with a 
	 * NULL pointer, so that data[0] is the first result entry,
	 * data[1] the second, and the last entry is NULL. 
	 * If there was no data, data[0] is NULL.
	 */
	char** data;

	/** the length in bytes of the data items, len[i] for data[i] */
	int* len;

	/** 
	 * canonical name for the result (the final cname). 
	 * zero terminated string.
	 * May be NULL if no canonical name exists.
	 */
	char* canonname;

	/**
	 * DNS RCODE for the result. May contain additional error code if
	 * there was no data due to an error. 0 (NOERROR) if okay.
	 */
	int rcode;

	/**
	 * The DNS answer packet. Network formatted. Can contain DNSSEC types.
	 */
	void* answer_packet;
	/** length of the answer packet in octets. */
	int answer_len;

	/**
	 * If there is any data, this is true.
	 * If false, there was no data (nxdomain may be true, rcode can be set).
	 */
	int havedata;

	/** 
	 * If there was no data, and the domain did not exist, this is true.
	 * If it is false, and there was no data, then the domain name 
	 * is purported to exist, but the requested data type is not available.
	 */
	int nxdomain;

	/**
	 * True, if the result is validated securely.
	 * False, if validation failed or domain queried has no security info.
	 *
	 * It is possible to get a result with no data (havedata is false),
	 * and secure is true. This means that the non-existence of the data
	 * was cryptographically proven (with signatures).
	 */
	int secure;

	/** 
	 * If the result was not secure (secure==0), and this result is due 
	 * to a security failure, bogus is true.
	 * This means the data has been actively tampered with, signatures
	 * failed, expected signatures were not present, timestamps on 
	 * signatures were out of date and so on.
	 *
	 * If !secure and !bogus, this can happen if the data is not secure 
	 * because security is disabled for that domain name. 
	 * This means the data is from a domain where data is not signed.
	 */
	int bogus;
	
	/**
	 * If the result is bogus this contains a string (zero terminated)
	 * that describes the failure.  There may be other errors as well
	 * as the one described, the description may not be perfectly accurate.
	 * Is NULL if the result is not bogus.
	 */
	char* why_bogus;

	/**
	 * If the query or one of its subqueries was ratelimited. Useful if
	 * ratelimiting is enabled and answer is SERVFAIL.
	 */
	int was_ratelimited;

	/**
	 * TTL for the result, in seconds.  If the security is bogus, then
	 * you also cannot trust this value.
	 */
	int ttl;
};

/**
 * Callback for results of async queries.
 * The readable function definition looks like:
 * void my_callback(void* my_arg, int err, struct ub_result* result);
 * It is called with
 *	void* my_arg: your pointer to a (struct of) data of your choice, 
 *		or NULL.
 *	int err: if 0 all is OK, otherwise an error occured and no results
 *	     are forthcoming.
 *	struct result: pointer to more detailed result structure.
 *		This structure is allocated on the heap and needs to be
 *		freed with ub_resolve_free(result);
 */
typedef void (*ub_callback_type)(void*, int, struct ub_result*);

/**
 * Create a resolving and validation context.
 * The information from /etc/resolv.conf and /etc/hosts is not utilised by
 * default. Use ub_ctx_resolvconf and ub_ctx_hosts to read them.
 * @return a new context. default initialisation.
 * 	returns NULL on error.
 */
struct ub_ctx* ub_ctx_create(void);

/**
 * Destroy a validation context and free all its resources.
 * Outstanding async queries are killed and callbacks are not called for them.
 * @param ctx: context to delete.
 */
void ub_ctx_delete(struct ub_ctx* ctx);

/**
 * Set an option for the context.
 * @param ctx: context.
 * @param opt: option name from the unbound.conf config file format.
 *	(not all settings applicable). The name includes the trailing ':'
 *	for example ub_ctx_set_option(ctx, "logfile:", "mylog.txt");
 * 	This is a power-users interface that lets you specify all sorts
 * 	of options.
 * 	For some specific options, such as adding trust anchors, special
 * 	routines exist.
 * @param val: value of the option.
 * @return: 0 if OK, else error.
 */
int ub_ctx_set_option(struct ub_ctx* ctx, const char* opt, const char* val);

/**
 * Get an option from the context.
 * @param ctx: context.
 * @param opt: option name from the unbound.conf config file format.
 *	(not all settings applicable). The name excludes the trailing ':'
 *	for example ub_ctx_get_option(ctx, "logfile", &result);
 * 	This is a power-users interface that lets you specify all sorts
 * 	of options.
 * @param str: the string is malloced and returned here. NULL on error.
 * 	The caller must free() the string.  In cases with multiple 
 * 	entries (auto-trust-anchor-file), a newline delimited list is 
 * 	returned in the string.
 * @return 0 if OK else an error code (malloc failure, syntax error).
 */
int ub_ctx_get_option(struct ub_ctx* ctx, const char* opt, char** str);

/**
 * setup configuration for the given context.
 * @param ctx: context.
 * @param fname: unbound config file (not all settings applicable).
 * 	This is a power-users interface that lets you specify all sorts
 * 	of options.
 * 	For some specific options, such as adding trust anchors, special
 * 	routines exist.
 * @return: 0 if OK, else error.
 */
int ub_ctx_config(struct ub_ctx* ctx, const char* fname);

/**
 * Set machine to forward DNS queries to, the caching resolver to use. 
 * IP4 or IP6 address. Forwards all DNS requests to that machine, which 
 * is expected to run a recursive resolver. If the proxy is not 
 * DNSSEC-capable, validation may fail. Can be called several times, in 
 * that case the addresses are used as backup servers.
 *
 * To read the list of nameservers from /etc/resolv.conf (from DHCP or so),
 * use the call ub_ctx_resolvconf.
 *
 * @param ctx: context.
 *	At this time it is only possible to set configuration before the
 *	first resolve is done.
 * @param addr: address, IP4 or IP6 in string format.
 * 	If the addr is NULL, forwarding is disabled.
 * @return 0 if OK, else error.
 */
int ub_ctx_set_fwd(struct ub_ctx* ctx, const char* addr);

/**
 * Add a stub zone, with given address to send to.  This is for custom
 * root hints or pointing to a local authoritative dns server.
 * For dns resolvers and the 'DHCP DNS' ip address, use ub_ctx_set_fwd.
 * This is similar to a stub-zone entry in unbound.conf.
 *
 * @param ctx: context.
 *	It is only possible to set configuration before the
 *	first resolve is done.
 * @param zone: name of the zone, string.
 * @param addr: address, IP4 or IP6 in string format.
 * 	The addr is added to the list of stub-addresses if the entry exists.
 * 	If the addr is NULL the stub entry is removed.
 * @param isprime: set to true to set stub-prime to yes for the stub.
 * 	For local authoritative servers, people usually set it to false,
 * 	For root hints it should be set to true.
 * @return 0 if OK, else error.
 */
int ub_ctx_set_stub(struct ub_ctx* ctx, const char* zone, const char* addr,
	int isprime);

/**
 * Read list of nameservers to use from the filename given.
 * Usually "/etc/resolv.conf". Uses those nameservers as caching proxies.
 * If they do not support DNSSEC, validation may fail.
 *
 * Only nameservers are picked up, the searchdomain, ndots and other
 * settings from resolv.conf(5) are ignored.
 *
 * @param ctx: context.
 *	At this time it is only possible to set configuration before the
 *	first resolve is done.
 * @param fname: file name string. If NULL "/etc/resolv.conf" is used.
 * @return 0 if OK, else error.
 */
int ub_ctx_resolvconf(struct ub_ctx* ctx, const char* fname);

/**
 * Read list of hosts from the filename given.
 * Usually "/etc/hosts". 
 * These addresses are not flagged as DNSSEC secure when queried for.
 *
 * @param ctx: context.
 *	At this time it is only possible to set configuration before the
 *	first resolve is done.
 * @param fname: file name string. If NULL "/etc/hosts" is used.
 * @return 0 if OK, else error.
 */
int ub_ctx_hosts(struct ub_ctx* ctx, const char* fname);

/**
 * Add a trust anchor to the given context.
 * The trust anchor is a string, on one line, that holds a valid DNSKEY or
 * DS RR. 
 * @param ctx: context.
 *	At this time it is only possible to add trusted keys before the
 *	first resolve is done.
 * @param ta: string, with zone-format RR on one line.
 * 	[domainname] [TTL optional] [type] [class optional] [rdata contents]
 * @return 0 if OK, else error.
 */
int ub_ctx_add_ta(struct ub_ctx* ctx, const char* ta);

/**
 * Add trust anchors to the given context.
 * Pass name of a file with DS and DNSKEY records (like from dig or drill).
 * @param ctx: context.
 *	At this time it is only possible to add trusted keys before the
 *	first resolve is done.
 * @param fname: filename of file with keyfile with trust anchors.
 * @return 0 if OK, else error.
 */
int ub_ctx_add_ta_file(struct ub_ctx* ctx, const char* fname);

/**
 * Add trust anchor to the given context that is tracked with RFC5011
 * automated trust anchor maintenance.  The file is written to when the
 * trust anchor is changed.
 * Pass the name of a file that was output from eg. unbound-anchor,
 * or you can start it by providing a trusted DNSKEY or DS record on one
 * line in the file.
 * @param ctx: context.
 *	At this time it is only possible to add trusted keys before the
 *	first resolve is done.
 * @param fname: filename of file with trust anchor.
 * @return 0 if OK, else error.
 */
int ub_ctx_add_ta_autr(struct ub_ctx* ctx, const char* fname);

/**
 * Add trust anchors to the given context.
 * Pass the name of a bind-style config file with trusted-keys{}.
 * @param ctx: context.
 *	At this time it is only possible to add trusted keys before the
 *	first resolve is done.
 * @param fname: filename of file with bind-style config entries with trust
 * 	anchors.
 * @return 0 if OK, else error.
 */
int ub_ctx_trustedkeys(struct ub_ctx* ctx, const char* fname);

/**
 * Set debug output (and error output) to the specified stream.
 * Pass NULL to disable. Default is stderr.
 * @param ctx: context.
 * @param out: FILE* out file stream to log to.
 * 	Type void* to avoid stdio dependency of this header file.
 * @return 0 if OK, else error.
 */
int ub_ctx_debugout(struct ub_ctx* ctx, void* out);

/**
 * Set debug verbosity for the context
 * Output is directed to stderr.
 * @param ctx: context.
 * @param d: debug level, 0 is off, 1 is very minimal, 2 is detailed, 
 *	and 3 is lots.
 * @return 0 if OK, else error.
 */
int ub_ctx_debuglevel(struct ub_ctx* ctx, int d);

/**
 * Set a context behaviour for asynchronous action.
 * @param ctx: context.
 * @param dothread: if true, enables threading and a call to resolve_async() 
 *	creates a thread to handle work in the background.
 *	If false, a process is forked to handle work in the background.
 *	Changes to this setting after async() calls have been made have 
 *	no effect (delete and re-create the context to change).
 * @return 0 if OK, else error.
 */
int ub_ctx_async(struct ub_ctx* ctx, int dothread);

/**
 * Poll a context to see if it has any new results
 * Do not poll in a loop, instead extract the fd below to poll for readiness,
 * and then check, or wait using the wait routine.
 * @param ctx: context.
 * @return: 0 if nothing to read, or nonzero if a result is available.
 * 	If nonzero, call ctx_process() to do callbacks.
 */
int ub_poll(struct ub_ctx* ctx);

/**
 * Wait for a context to finish with results. Calls ub_process() after
 * the wait for you. After the wait, there are no more outstanding 
 * asynchronous queries.
 * @param ctx: context.
 * @return: 0 if OK, else error.
 */
int ub_wait(struct ub_ctx* ctx);

/**
 * Get file descriptor. Wait for it to become readable, at this point
 * answers are returned from the asynchronous validating resolver.
 * Then call the ub_process to continue processing.
 * This routine works immediately after context creation, the fd
 * does not change.
 * @param ctx: context.
 * @return: -1 on error, or file descriptor to use select(2) with.
 */
int ub_fd(struct ub_ctx* ctx);

/**
 * Call this routine to continue processing results from the validating
 * resolver (when the fd becomes readable).
 * Will perform necessary callbacks.
 * @param ctx: context
 * @return: 0 if OK, else error.
 */
int ub_process(struct ub_ctx* ctx);

/**
 * Perform resolution and validation of the target name.
 * @param ctx: context.
 *	The context is finalized, and can no longer accept config changes.
 * @param name: domain name in text format (a zero terminated text string).
 * @param rrtype: type of RR in host order, 1 is A (address).
 * @param rrclass: class of RR in host order, 1 is IN (for internet).
 * @param result: the result data is returned in a newly allocated result
 * 	structure. May be NULL on return, return value is set to an error 
 * 	in that case (out of memory).
 * @return 0 if OK, else error.
 */
int ub_resolve(struct ub_ctx* ctx, const char* name, int rrtype, 
	int rrclass, struct ub_result** result);

/**
 * Perform resolution and validation of the target name.
 * Asynchronous, after a while, the callback will be called with your
 * data and the result.
 * @param ctx: context.
 *	If no thread or process has been created yet to perform the
 *	work in the background, it is created now.
 *	The context is finalized, and can no longer accept config changes.
 * @param name: domain name in text format (a string).
 * @param rrtype: type of RR in host order, 1 is A.
 * @param rrclass: class of RR in host order, 1 is IN (for internet).
 * @param mydata: this data is your own data (you can pass NULL),
 * 	and is passed on to the callback function.
 * @param callback: this is called on completion of the resolution.
 * 	It is called as:
 * 	void callback(void* mydata, int err, struct ub_result* result)
 * 	with mydata: the same as passed here, you may pass NULL,
 * 	with err: is 0 when a result has been found.
 * 	with result: a newly allocated result structure.
 *		The result may be NULL, in that case err is set.
 *
 * 	If an error happens during processing, your callback will be called
 * 	with error set to a nonzero value (and result==NULL).
 * @param async_id: if you pass a non-NULL value, an identifier number is
 *	returned for the query as it is in progress. It can be used to 
 *	cancel the query.
 * @return 0 if OK, else error.
 */
int ub_resolve_async(struct ub_ctx* ctx, const char* name, int rrtype, 
	int rrclass, void* mydata, ub_callback_type callback, int* async_id);

/**
 * Cancel an async query in progress.
 * Its callback will not be called.
 *
 * @param ctx: context.
 * @param async_id: which query to cancel.
 * @return 0 if OK, else error.
 * This routine can return an error if the async_id passed does not exist
 * or has already been delivered. If another thread is processing results
 * at the same time, the result may be delivered at the same time and the
 * cancel fails with an error.  Also the cancel can fail due to a system
 * error, no memory or socket failures.
 */
int ub_cancel(struct ub_ctx* ctx, int async_id);

/**
 * Free storage associated with a result structure.
 * @param result: to free
 */
void ub_resolve_free(struct ub_result* result);

/** 
 * Convert error value to a human readable string.
 * @param err: error code from one of the libunbound functions.
 * @return pointer to constant text string, zero terminated.
 */
const char* ub_strerror(int err);

/**
 * Debug routine.  Print the local zone information to debug output.
 * @param ctx: context.  Is finalized by the routine.
 * @return 0 if OK, else error.
 */
int ub_ctx_print_local_zones(struct ub_ctx* ctx);

/**
 * Add a new zone with the zonetype to the local authority info of the 
 * library.
 * @param ctx: context.  Is finalized by the routine.
 * @param zone_name: name of the zone in text, "example.com"
 *	If it already exists, the type is updated.
 * @param zone_type: type of the zone (like for unbound.conf) in text.
 * @return 0 if OK, else error.
 */
int ub_ctx_zone_add(struct ub_ctx* ctx, const char *zone_name, 
	const char *zone_type);

/**
 * Remove zone from local authority info of the library.
 * @param ctx: context.  Is finalized by the routine.
 * @param zone_name: name of the zone in text, "example.com"
 *	If it does not exist, nothing happens.
 * @return 0 if OK, else error.
 */
int ub_ctx_zone_remove(struct ub_ctx* ctx, const char *zone_name);

/**
 * Add localdata to the library local authority info.
 * Similar to local-data config statement.
 * @param ctx: context.  Is finalized by the routine.
 * @param data: the resource record in text format, for example
 *	"www.example.com IN A 127.0.0.1"
 * @return 0 if OK, else error.
 */
int ub_ctx_data_add(struct ub_ctx* ctx, const char *data);

/**
 * Remove localdata from the library local authority info.
 * @param ctx: context.  Is finalized by the routine.
 * @param data: the name to delete all data from, like "www.example.com".
 * @return 0 if OK, else error.
 */
int ub_ctx_data_remove(struct ub_ctx* ctx, const char *data);

/**
 * Get a version string from the libunbound implementation.
 * @return a static constant string with the version number.
 */
const char* ub_version(void);

/** 
 * Some global statistics that are not in struct stats_info,
 * this struct is shared on a shm segment (shm-key in unbound.conf)
 */
struct ub_shm_stat_info {
	int num_threads;

	struct {
		long long now_sec, now_usec;
		long long up_sec, up_usec;
		long long elapsed_sec, elapsed_usec;
	} time;

	struct {
		long long msg;
		long long rrset;
		long long val;
		long long iter;
		long long subnet;
		long long ipsecmod;
		long long respip;
		long long dnscrypt_shared_secret;
		long long dnscrypt_nonce;
	} mem;
};

/** number of qtype that is stored for in array */
#define UB_STATS_QTYPE_NUM 256
/** number of qclass that is stored for in array */
#define UB_STATS_QCLASS_NUM 256
/** number of rcodes in stats */
#define UB_STATS_RCODE_NUM 16
/** number of opcodes in stats */
#define UB_STATS_OPCODE_NUM 16
/** number of histogram buckets */
#define UB_STATS_BUCKET_NUM 40

/** per worker statistics. */
struct ub_server_stats {
	/** number of queries from clients received. */
	long long num_queries;
	/** number of queries that have been dropped/ratelimited by ip. */
	long long num_queries_ip_ratelimited;
	/** number of queries that had a cache-miss. */
	long long num_queries_missed_cache;
	/** number of prefetch queries - cachehits with prefetch */
	long long num_queries_prefetch;

	/**
	 * Sum of the querylistsize of the worker for 
	 * every query that missed cache. To calculate average.
	 */
	long long sum_query_list_size;
	/** max value of query list size reached. */
	long long max_query_list_size;

	/** Extended stats below (bool) */
	int extended;

	/** qtype stats */
	long long qtype[UB_STATS_QTYPE_NUM];
	/** bigger qtype values not in array */
	long long qtype_big;
	/** qclass stats */
	long long qclass[UB_STATS_QCLASS_NUM];
	/** bigger qclass values not in array */
	long long qclass_big;
	/** query opcodes */
	long long qopcode[UB_STATS_OPCODE_NUM];
	/** number of queries over TCP */
	long long qtcp;
	/** number of outgoing queries over TCP */
	long long qtcp_outgoing;
	/** number of queries over (DNS over) TLS */
	long long qtls;
	/** number of queries over IPv6 */
	long long qipv6;
	/** number of queries with QR bit */
	long long qbit_QR;
	/** number of queries with AA bit */
	long long qbit_AA;
	/** number of queries with TC bit */
	long long qbit_TC;
	/** number of queries with RD bit */
	long long qbit_RD;
	/** number of queries with RA bit */
	long long qbit_RA;
	/** number of queries with Z bit */
	long long qbit_Z;
	/** number of queries with AD bit */
	long long qbit_AD;
	/** number of queries with CD bit */
	long long qbit_CD;
	/** number of queries with EDNS OPT record */
	long long qEDNS;
	/** number of queries with EDNS with DO flag */
	long long qEDNS_DO;
	/** answer rcodes */
	long long ans_rcode[UB_STATS_RCODE_NUM];
	/** answers with pseudo rcode 'nodata' */
	long long ans_rcode_nodata;
	/** answers that were secure (AD) */
	long long ans_secure;
	/** answers that were bogus (withheld as SERVFAIL) */
	long long ans_bogus;
	/** rrsets marked bogus by validator */
	long long rrset_bogus;
	/** number of queries that have been ratelimited by domain recursion. */
	long long queries_ratelimited;
	/** unwanted traffic received on server-facing ports */
	long long unwanted_replies;
	/** unwanted traffic received on client-facing ports */
	long long unwanted_queries;
	/** usage of tcp accept list */
	long long tcp_accept_usage;
	/** answers served from expired cache */
	long long zero_ttl_responses;
	/** histogram data exported to array 
	 * if the array is the same size, no data is lost, and
	 * if all histograms are same size (is so by default) then
	 * adding up works well. */
	long long hist[UB_STATS_BUCKET_NUM];
	
	/** number of message cache entries */
	long long msg_cache_count;
	/** number of rrset cache entries */
	long long rrset_cache_count;
	/** number of infra cache entries */
	long long infra_cache_count;
	/** number of key cache entries */
	long long key_cache_count;

	/** number of queries that used dnscrypt */
	long long num_query_dnscrypt_crypted;
	/** number of queries that queried dnscrypt certificates */
	long long num_query_dnscrypt_cert;
	/** number of queries in clear text and not asking for the certificates */
	long long num_query_dnscrypt_cleartext;
	/** number of malformed encrypted queries */
	long long num_query_dnscrypt_crypted_malformed;
	/** number of queries which did not have a shared secret in cache */
	long long num_query_dnscrypt_secret_missed_cache;
	/** number of dnscrypt shared secret cache entries */
	long long shared_secret_cache_count;
	/** number of queries which are replays */
	long long num_query_dnscrypt_replay;
	/** number of dnscrypt nonces cache entries */
	long long nonce_cache_count;
	/** number of queries for unbound's auth_zones, upstream query */
	long long num_query_authzone_up;
	/** number of queries for unbound's auth_zones, downstream answers */
	long long num_query_authzone_down;
	/** number of times neg cache records were used to generate NOERROR
	 * responses. */
	long long num_neg_cache_noerror;
	/** number of times neg cache records were used to generate NXDOMAIN
	 * responses. */
	long long num_neg_cache_nxdomain;
	/** number of queries answered from edns-subnet specific data */
	long long num_query_subnet;
	/** number of queries answered from edns-subnet specific data, and
	 * the answer was from the edns-subnet cache. */
	long long num_query_subnet_cache;
};

/** 
 * Statistics to send over the control pipe when asked
 * This struct is made to be memcopied, sent in binary.
 * shm mapped with (number+1) at num_threads+1, with first as total
 */
struct ub_stats_info {
	/** the thread stats */
	struct ub_server_stats svr;

	/** mesh stats: current number of states */
	long long mesh_num_states;
	/** mesh stats: current number of reply (user) states */
	long long mesh_num_reply_states;
	/** mesh stats: number of reply states overwritten with a new one */
	long long mesh_jostled;
	/** mesh stats: number of incoming queries dropped */
	long long mesh_dropped;
	/** mesh stats: replies sent */
	long long mesh_replies_sent;
	/** mesh stats: sum of waiting times for the replies */
	long long mesh_replies_sum_wait_sec, mesh_replies_sum_wait_usec;
	/** mesh stats: median of waiting times for replies (in sec) */
	double mesh_time_median;
};

#ifdef __cplusplus
}
#endif

#endif /* _UB_UNBOUND_H */
