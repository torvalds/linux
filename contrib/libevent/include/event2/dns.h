/*
 * Copyright (c) 2006-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The original DNS code is due to Adam Langley with heavy
 * modifications by Nick Mathewson.  Adam put his DNS software in the
 * public domain.  You can find his original copyright below.  Please,
 * aware that the code as part of Libevent is governed by the 3-clause
 * BSD license above.
 *
 * This software is Public Domain. To view a copy of the public domain dedication,
 * visit http://creativecommons.org/licenses/publicdomain/ or send a letter to
 * Creative Commons, 559 Nathan Abbott Way, Stanford, California 94305, USA.
 *
 * I ask and expect, but do not require, that all derivative works contain an
 * attribution similar to:
 *     Parts developed by Adam Langley <agl@imperialviolet.org>
 *
 * You may wish to replace the word "Parts" with something else depending on
 * the amount of original code.
 *
 * (Derivative works does not include programs which link against, run or include
 * the source verbatim in their source distributions)
 */

/** @file event2/dns.h
 *
 * Welcome, gentle reader
 *
 * Async DNS lookups are really a whole lot harder than they should be,
 * mostly stemming from the fact that the libc resolver has never been
 * very good at them. Before you use this library you should see if libc
 * can do the job for you with the modern async call getaddrinfo_a
 * (see http://www.imperialviolet.org/page25.html#e498). Otherwise,
 * please continue.
 *
 * The library keeps track of the state of nameservers and will avoid
 * them when they go down. Otherwise it will round robin between them.
 *
 * Quick start guide:
 *   #include "evdns.h"
 *   void callback(int result, char type, int count, int ttl,
 *		 void *addresses, void *arg);
 *   evdns_resolv_conf_parse(DNS_OPTIONS_ALL, "/etc/resolv.conf");
 *   evdns_resolve("www.hostname.com", 0, callback, NULL);
 *
 * When the lookup is complete the callback function is called. The
 * first argument will be one of the DNS_ERR_* defines in evdns.h.
 * Hopefully it will be DNS_ERR_NONE, in which case type will be
 * DNS_IPv4_A, count will be the number of IP addresses, ttl is the time
 * which the data can be cached for (in seconds), addresses will point
 * to an array of uint32_t's and arg will be whatever you passed to
 * evdns_resolve.
 *
 * Searching:
 *
 * In order for this library to be a good replacement for glibc's resolver it
 * supports searching. This involves setting a list of default domains, in
 * which names will be queried for. The number of dots in the query name
 * determines the order in which this list is used.
 *
 * Searching appears to be a single lookup from the point of view of the API,
 * although many DNS queries may be generated from a single call to
 * evdns_resolve. Searching can also drastically slow down the resolution
 * of names.
 *
 * To disable searching:
 *   1. Never set it up. If you never call evdns_resolv_conf_parse or
 *   evdns_search_add then no searching will occur.
 *
 *   2. If you do call evdns_resolv_conf_parse then don't pass
 *   DNS_OPTION_SEARCH (or DNS_OPTIONS_ALL, which implies it).
 *
 *   3. When calling evdns_resolve, pass the DNS_QUERY_NO_SEARCH flag.
 *
 * The order of searches depends on the number of dots in the name. If the
 * number is greater than the ndots setting then the names is first tried
 * globally. Otherwise each search domain is appended in turn.
 *
 * The ndots setting can either be set from a resolv.conf, or by calling
 * evdns_search_ndots_set.
 *
 * For example, with ndots set to 1 (the default) and a search domain list of
 * ["myhome.net"]:
 *  Query: www
 *  Order: www.myhome.net, www.
 *
 *  Query: www.abc
 *  Order: www.abc., www.abc.myhome.net
 *
 * Internals:
 *
 * Requests are kept in two queues. The first is the inflight queue. In
 * this queue requests have an allocated transaction id and nameserver.
 * They will soon be transmitted if they haven't already been.
 *
 * The second is the waiting queue. The size of the inflight ring is
 * limited and all other requests wait in waiting queue for space. This
 * bounds the number of concurrent requests so that we don't flood the
 * nameserver. Several algorithms require a full walk of the inflight
 * queue and so bounding its size keeps thing going nicely under huge
 * (many thousands of requests) loads.
 *
 * If a nameserver loses too many requests it is considered down and we
 * try not to use it. After a while we send a probe to that nameserver
 * (a lookup for google.com) and, if it replies, we consider it working
 * again. If the nameserver fails a probe we wait longer to try again
 * with the next probe.
 */

#ifndef EVENT2_DNS_H_INCLUDED_
#define EVENT2_DNS_H_INCLUDED_

#include <event2/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/* For integer types. */
#include <event2/util.h>

/** Error codes 0-5 are as described in RFC 1035. */
#define DNS_ERR_NONE 0
/** The name server was unable to interpret the query */
#define DNS_ERR_FORMAT 1
/** The name server was unable to process this query due to a problem with the
 * name server */
#define DNS_ERR_SERVERFAILED 2
/** The domain name does not exist */
#define DNS_ERR_NOTEXIST 3
/** The name server does not support the requested kind of query */
#define DNS_ERR_NOTIMPL 4
/** The name server refuses to reform the specified operation for policy
 * reasons */
#define DNS_ERR_REFUSED 5
/** The reply was truncated or ill-formatted */
#define DNS_ERR_TRUNCATED 65
/** An unknown error occurred */
#define DNS_ERR_UNKNOWN 66
/** Communication with the server timed out */
#define DNS_ERR_TIMEOUT 67
/** The request was canceled because the DNS subsystem was shut down. */
#define DNS_ERR_SHUTDOWN 68
/** The request was canceled via a call to evdns_cancel_request */
#define DNS_ERR_CANCEL 69
/** There were no answers and no error condition in the DNS packet.
 * This can happen when you ask for an address that exists, but a record
 * type that doesn't. */
#define DNS_ERR_NODATA 70

#define DNS_IPv4_A 1
#define DNS_PTR 2
#define DNS_IPv6_AAAA 3

#define DNS_QUERY_NO_SEARCH 1

#define DNS_OPTION_SEARCH 1
#define DNS_OPTION_NAMESERVERS 2
#define DNS_OPTION_MISC 4
#define DNS_OPTION_HOSTSFILE 8
#define DNS_OPTIONS_ALL 15

/* Obsolete name for DNS_QUERY_NO_SEARCH */
#define DNS_NO_SEARCH DNS_QUERY_NO_SEARCH

/**
 * The callback that contains the results from a lookup.
 * - result is one of the DNS_ERR_* values (DNS_ERR_NONE for success)
 * - type is either DNS_IPv4_A or DNS_PTR or DNS_IPv6_AAAA
 * - count contains the number of addresses of form type
 * - ttl is the number of seconds the resolution may be cached for.
 * - addresses needs to be cast according to type.  It will be an array of
 *   4-byte sequences for ipv4, or an array of 16-byte sequences for ipv6,
 *   or a nul-terminated string for PTR.
 */
typedef void (*evdns_callback_type) (int result, char type, int count, int ttl, void *addresses, void *arg);

struct evdns_base;
struct event_base;

/** Flag for evdns_base_new: process resolv.conf.  */
#define EVDNS_BASE_INITIALIZE_NAMESERVERS 1
/** Flag for evdns_base_new: Do not prevent the libevent event loop from
 * exiting when we have no active dns requests. */
#define EVDNS_BASE_DISABLE_WHEN_INACTIVE 0x8000

/**
  Initialize the asynchronous DNS library.

  This function initializes support for non-blocking name resolution by
  calling evdns_resolv_conf_parse() on UNIX and
  evdns_config_windows_nameservers() on Windows.

  @param event_base the event base to associate the dns client with
  @param flags any of EVDNS_BASE_INITIALIZE_NAMESERVERS|
    EVDNS_BASE_DISABLE_WHEN_INACTIVE
  @return evdns_base object if successful, or NULL if an error occurred.
  @see evdns_base_free()
 */
EVENT2_EXPORT_SYMBOL
struct evdns_base * evdns_base_new(struct event_base *event_base, int initialize_nameservers);


/**
  Shut down the asynchronous DNS resolver and terminate all active requests.

  If the 'fail_requests' option is enabled, all active requests will return
  an empty result with the error flag set to DNS_ERR_SHUTDOWN. Otherwise,
  the requests will be silently discarded.

  @param evdns_base the evdns base to free
  @param fail_requests if zero, active requests will be aborted; if non-zero,
		active requests will return DNS_ERR_SHUTDOWN.
  @see evdns_base_new()
 */
EVENT2_EXPORT_SYMBOL
void evdns_base_free(struct evdns_base *base, int fail_requests);

/**
   Remove all hosts entries that have been loaded into the event_base via
   evdns_base_load_hosts or via event_base_resolv_conf_parse.

   @param evdns_base the evdns base to remove outdated host addresses from
 */
EVENT2_EXPORT_SYMBOL
void evdns_base_clear_host_addresses(struct evdns_base *base);

/**
  Convert a DNS error code to a string.

  @param err the DNS error code
  @return a string containing an explanation of the error code
*/
EVENT2_EXPORT_SYMBOL
const char *evdns_err_to_string(int err);


/**
  Add a nameserver.

  The address should be an IPv4 address in network byte order.
  The type of address is chosen so that it matches in_addr.s_addr.

  @param base the evdns_base to which to add the name server
  @param address an IP address in network byte order
  @return 0 if successful, or -1 if an error occurred
  @see evdns_base_nameserver_ip_add()
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_nameserver_add(struct evdns_base *base,
    unsigned long int address);

/**
  Get the number of configured nameservers.

  This returns the number of configured nameservers (not necessarily the
  number of running nameservers).  This is useful for double-checking
  whether our calls to the various nameserver configuration functions
  have been successful.

  @param base the evdns_base to which to apply this operation
  @return the number of configured nameservers
  @see evdns_base_nameserver_add()
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_count_nameservers(struct evdns_base *base);

/**
  Remove all configured nameservers, and suspend all pending resolves.

  Resolves will not necessarily be re-attempted until evdns_base_resume() is called.

  @param base the evdns_base to which to apply this operation
  @return 0 if successful, or -1 if an error occurred
  @see evdns_base_resume()
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_clear_nameservers_and_suspend(struct evdns_base *base);


/**
  Resume normal operation and continue any suspended resolve requests.

  Re-attempt resolves left in limbo after an earlier call to
  evdns_base_clear_nameservers_and_suspend().

  @param base the evdns_base to which to apply this operation
  @return 0 if successful, or -1 if an error occurred
  @see evdns_base_clear_nameservers_and_suspend()
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_resume(struct evdns_base *base);

/**
  Add a nameserver by string address.

  This function parses a n IPv4 or IPv6 address from a string and adds it as a
  nameserver.  It supports the following formats:
  - [IPv6Address]:port
  - [IPv6Address]
  - IPv6Address
  - IPv4Address:port
  - IPv4Address

  If no port is specified, it defaults to 53.

  @param base the evdns_base to which to apply this operation
  @return 0 if successful, or -1 if an error occurred
  @see evdns_base_nameserver_add()
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_nameserver_ip_add(struct evdns_base *base,
    const char *ip_as_string);

/**
   Add a nameserver by sockaddr.
 **/
EVENT2_EXPORT_SYMBOL
int
evdns_base_nameserver_sockaddr_add(struct evdns_base *base,
    const struct sockaddr *sa, ev_socklen_t len, unsigned flags);

struct evdns_request;

/**
  Lookup an A record for a given name.

  @param base the evdns_base to which to apply this operation
  @param name a DNS hostname
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return an evdns_request object if successful, or NULL if an error occurred.
  @see evdns_resolve_ipv6(), evdns_resolve_reverse(), evdns_resolve_reverse_ipv6(), evdns_cancel_request()
 */
EVENT2_EXPORT_SYMBOL
struct evdns_request *evdns_base_resolve_ipv4(struct evdns_base *base, const char *name, int flags, evdns_callback_type callback, void *ptr);

/**
  Lookup an AAAA record for a given name.

  @param base the evdns_base to which to apply this operation
  @param name a DNS hostname
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return an evdns_request object if successful, or NULL if an error occurred.
  @see evdns_resolve_ipv4(), evdns_resolve_reverse(), evdns_resolve_reverse_ipv6(), evdns_cancel_request()
 */
EVENT2_EXPORT_SYMBOL
struct evdns_request *evdns_base_resolve_ipv6(struct evdns_base *base, const char *name, int flags, evdns_callback_type callback, void *ptr);

struct in_addr;
struct in6_addr;

/**
  Lookup a PTR record for a given IP address.

  @param base the evdns_base to which to apply this operation
  @param in an IPv4 address
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return an evdns_request object if successful, or NULL if an error occurred.
  @see evdns_resolve_reverse_ipv6(), evdns_cancel_request()
 */
EVENT2_EXPORT_SYMBOL
struct evdns_request *evdns_base_resolve_reverse(struct evdns_base *base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr);


/**
  Lookup a PTR record for a given IPv6 address.

  @param base the evdns_base to which to apply this operation
  @param in an IPv6 address
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return an evdns_request object if successful, or NULL if an error occurred.
  @see evdns_resolve_reverse_ipv6(), evdns_cancel_request()
 */
EVENT2_EXPORT_SYMBOL
struct evdns_request *evdns_base_resolve_reverse_ipv6(struct evdns_base *base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr);

/**
  Cancels a pending DNS resolution request.

  @param base the evdns_base that was used to make the request
  @param req the evdns_request that was returned by calling a resolve function
  @see evdns_base_resolve_ipv4(), evdns_base_resolve_ipv6, evdns_base_resolve_reverse
*/
EVENT2_EXPORT_SYMBOL
void evdns_cancel_request(struct evdns_base *base, struct evdns_request *req);

/**
  Set the value of a configuration option.

  The currently available configuration options are:

    ndots, timeout, max-timeouts, max-inflight, attempts, randomize-case,
    bind-to, initial-probe-timeout, getaddrinfo-allow-skew.

  In versions before Libevent 2.0.3-alpha, the option name needed to end with
  a colon.

  @param base the evdns_base to which to apply this operation
  @param option the name of the configuration option to be modified
  @param val the value to be set
  @return 0 if successful, or -1 if an error occurred
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_set_option(struct evdns_base *base, const char *option, const char *val);


/**
  Parse a resolv.conf file.

  The 'flags' parameter determines what information is parsed from the
  resolv.conf file. See the man page for resolv.conf for the format of this
  file.

  The following directives are not parsed from the file: sortlist, rotate,
  no-check-names, inet6, debug.

  If this function encounters an error, the possible return values are: 1 =
  failed to open file, 2 = failed to stat file, 3 = file too large, 4 = out of
  memory, 5 = short read from file, 6 = no nameservers listed in the file

  @param base the evdns_base to which to apply this operation
  @param flags any of DNS_OPTION_NAMESERVERS|DNS_OPTION_SEARCH|DNS_OPTION_MISC|
    DNS_OPTION_HOSTSFILE|DNS_OPTIONS_ALL
  @param filename the path to the resolv.conf file
  @return 0 if successful, or various positive error codes if an error
    occurred (see above)
  @see resolv.conf(3), evdns_config_windows_nameservers()
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *const filename);

/**
   Load an /etc/hosts-style file from 'hosts_fname' into 'base'.

   If hosts_fname is NULL, add minimal entries for localhost, and nothing
   else.

   Note that only evdns_getaddrinfo uses the /etc/hosts entries.

   This function does not replace previously loaded hosts entries; to do that,
   call evdns_base_clear_host_addresses first.

   Return 0 on success, negative on failure.
*/
EVENT2_EXPORT_SYMBOL
int evdns_base_load_hosts(struct evdns_base *base, const char *hosts_fname);

/**
  Obtain nameserver information using the Windows API.

  Attempt to configure a set of nameservers based on platform settings on
  a win32 host.  Preferentially tries to use GetNetworkParams; if that fails,
  looks in the registry.

  @return 0 if successful, or -1 if an error occurred
  @see evdns_resolv_conf_parse()
 */
#ifdef _WIN32
EVENT2_EXPORT_SYMBOL
int evdns_base_config_windows_nameservers(struct evdns_base *);
#define EVDNS_BASE_CONFIG_WINDOWS_NAMESERVERS_IMPLEMENTED
#endif


/**
  Clear the list of search domains.
 */
EVENT2_EXPORT_SYMBOL
void evdns_base_search_clear(struct evdns_base *base);


/**
  Add a domain to the list of search domains

  @param domain the domain to be added to the search list
 */
EVENT2_EXPORT_SYMBOL
void evdns_base_search_add(struct evdns_base *base, const char *domain);


/**
  Set the 'ndots' parameter for searches.

  Sets the number of dots which, when found in a name, causes
  the first query to be without any search domain.

  @param ndots the new ndots parameter
 */
EVENT2_EXPORT_SYMBOL
void evdns_base_search_ndots_set(struct evdns_base *base, const int ndots);

/**
  A callback that is invoked when a log message is generated

  @param is_warning indicates if the log message is a 'warning'
  @param msg the content of the log message
 */
typedef void (*evdns_debug_log_fn_type)(int is_warning, const char *msg);


/**
  Set the callback function to handle DNS log messages.  If this
  callback is not set, evdns log messages are handled with the regular
  Libevent logging system.

  @param fn the callback to be invoked when a log message is generated
 */
EVENT2_EXPORT_SYMBOL
void evdns_set_log_fn(evdns_debug_log_fn_type fn);

/**
   Set a callback that will be invoked to generate transaction IDs.  By
   default, we pick transaction IDs based on the current clock time, which
   is bad for security.

   @param fn the new callback, or NULL to use the default.

   NOTE: This function has no effect in Libevent 2.0.4-alpha and later,
   since Libevent now provides its own secure RNG.
 */
EVENT2_EXPORT_SYMBOL
void evdns_set_transaction_id_fn(ev_uint16_t (*fn)(void));

/**
   Set a callback used to generate random bytes.  By default, we use
   the same function as passed to evdns_set_transaction_id_fn to generate
   bytes two at a time.  If a function is provided here, it's also used
   to generate transaction IDs.

   NOTE: This function has no effect in Libevent 2.0.4-alpha and later,
   since Libevent now provides its own secure RNG.
*/
EVENT2_EXPORT_SYMBOL
void evdns_set_random_bytes_fn(void (*fn)(char *, size_t));

/*
 * Functions used to implement a DNS server.
 */

struct evdns_server_request;
struct evdns_server_question;

/**
   A callback to implement a DNS server.  The callback function receives a DNS
   request.  It should then optionally add a number of answers to the reply
   using the evdns_server_request_add_*_reply functions, before calling either
   evdns_server_request_respond to send the reply back, or
   evdns_server_request_drop to decline to answer the request.

   @param req A newly received request
   @param user_data A pointer that was passed to
      evdns_add_server_port_with_base().
 */
typedef void (*evdns_request_callback_fn_type)(struct evdns_server_request *, void *);
#define EVDNS_ANSWER_SECTION 0
#define EVDNS_AUTHORITY_SECTION 1
#define EVDNS_ADDITIONAL_SECTION 2

#define EVDNS_TYPE_A	   1
#define EVDNS_TYPE_NS	   2
#define EVDNS_TYPE_CNAME   5
#define EVDNS_TYPE_SOA	   6
#define EVDNS_TYPE_PTR	  12
#define EVDNS_TYPE_MX	  15
#define EVDNS_TYPE_TXT	  16
#define EVDNS_TYPE_AAAA	  28

#define EVDNS_QTYPE_AXFR 252
#define EVDNS_QTYPE_ALL	 255

#define EVDNS_CLASS_INET   1

/* flags that can be set in answers; as part of the err parameter */
#define EVDNS_FLAGS_AA	0x400
#define EVDNS_FLAGS_RD	0x080

/** Create a new DNS server port.

    @param base The event base to handle events for the server port.
    @param socket A UDP socket to accept DNS requests.
    @param flags Always 0 for now.
    @param callback A function to invoke whenever we get a DNS request
      on the socket.
    @param user_data Data to pass to the callback.
    @return an evdns_server_port structure for this server port.
 */
EVENT2_EXPORT_SYMBOL
struct evdns_server_port *evdns_add_server_port_with_base(struct event_base *base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data);
/** Close down a DNS server port, and free associated structures. */
EVENT2_EXPORT_SYMBOL
void evdns_close_server_port(struct evdns_server_port *port);

/** Sets some flags in a reply we're building.
    Allows setting of the AA or RD flags
 */
EVENT2_EXPORT_SYMBOL
void evdns_server_request_set_flags(struct evdns_server_request *req, int flags);

/* Functions to add an answer to an in-progress DNS reply.
 */
EVENT2_EXPORT_SYMBOL
int evdns_server_request_add_reply(struct evdns_server_request *req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data);
EVENT2_EXPORT_SYMBOL
int evdns_server_request_add_a_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl);
EVENT2_EXPORT_SYMBOL
int evdns_server_request_add_aaaa_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl);
EVENT2_EXPORT_SYMBOL
int evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl);
EVENT2_EXPORT_SYMBOL
int evdns_server_request_add_cname_reply(struct evdns_server_request *req, const char *name, const char *cname, int ttl);

/**
   Send back a response to a DNS request, and free the request structure.
*/
EVENT2_EXPORT_SYMBOL
int evdns_server_request_respond(struct evdns_server_request *req, int err);
/**
   Free a DNS request without sending back a reply.
*/
EVENT2_EXPORT_SYMBOL
int evdns_server_request_drop(struct evdns_server_request *req);
struct sockaddr;
/**
    Get the address that made a DNS request.
 */
EVENT2_EXPORT_SYMBOL
int evdns_server_request_get_requesting_addr(struct evdns_server_request *req, struct sockaddr *sa, int addr_len);

/** Callback for evdns_getaddrinfo. */
typedef void (*evdns_getaddrinfo_cb)(int result, struct evutil_addrinfo *res, void *arg);

struct evdns_base;
struct evdns_getaddrinfo_request;
/** Make a non-blocking getaddrinfo request using the dns_base in 'dns_base'.
 *
 * If we can answer the request immediately (with an error or not!), then we
 * invoke cb immediately and return NULL.  Otherwise we return
 * an evdns_getaddrinfo_request and invoke cb later.
 *
 * When the callback is invoked, we pass as its first argument the error code
 * that getaddrinfo would return (or 0 for no error).  As its second argument,
 * we pass the evutil_addrinfo structures we found (or NULL on error).  We
 * pass 'arg' as the third argument.
 *
 * Limitations:
 *
 * - The AI_V4MAPPED and AI_ALL flags are not currently implemented.
 * - For ai_socktype, we only handle SOCKTYPE_STREAM, SOCKTYPE_UDP, and 0.
 * - For ai_protocol, we only handle IPPROTO_TCP, IPPROTO_UDP, and 0.
 */
EVENT2_EXPORT_SYMBOL
struct evdns_getaddrinfo_request *evdns_getaddrinfo(
    struct evdns_base *dns_base,
    const char *nodename, const char *servname,
    const struct evutil_addrinfo *hints_in,
    evdns_getaddrinfo_cb cb, void *arg);

/* Cancel an in-progress evdns_getaddrinfo.  This MUST NOT be called after the
 * getaddrinfo's callback has been invoked.  The resolves will be canceled,
 * and the callback will be invoked with the error EVUTIL_EAI_CANCEL. */
EVENT2_EXPORT_SYMBOL
void evdns_getaddrinfo_cancel(struct evdns_getaddrinfo_request *req);

/**
   Retrieve the address of the 'idx'th configured nameserver.

   @param base The evdns_base to examine.
   @param idx The index of the nameserver to get the address of.
   @param sa A location to receive the server's address.
   @param len The number of bytes available at sa.

   @return the number of bytes written into sa on success.  On failure, returns
     -1 if idx is greater than the number of configured nameservers, or a
     value greater than 'len' if len was not high enough.
 */
EVENT2_EXPORT_SYMBOL
int evdns_base_get_nameserver_addr(struct evdns_base *base, int idx,
    struct sockaddr *sa, ev_socklen_t len);

#ifdef __cplusplus
}
#endif

#endif  /* !EVENT2_DNS_H_INCLUDED_ */
