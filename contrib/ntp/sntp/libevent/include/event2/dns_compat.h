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
#ifndef EVENT2_DNS_COMPAT_H_INCLUDED_
#define EVENT2_DNS_COMPAT_H_INCLUDED_

/** @file event2/dns_compat.h

  Potentially non-threadsafe versions of the functions in dns.h: provided
  only for backwards compatibility.


 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>

/**
  Initialize the asynchronous DNS library.

  This function initializes support for non-blocking name resolution by
  calling evdns_resolv_conf_parse() on UNIX and
  evdns_config_windows_nameservers() on Windows.

  @deprecated This function is deprecated because it always uses the current
    event base, and is easily confused by multiple calls to event_init(), and
    so is not safe for multithreaded use.  Additionally, it allocates a global
    structure that only one thread can use. The replacement is
    evdns_base_new().

  @return 0 if successful, or -1 if an error occurred
  @see evdns_shutdown()
 */
int evdns_init(void);

struct evdns_base;
/**
   Return the global evdns_base created by event_init() and used by the other
   deprecated functions.

   @deprecated This function is deprecated because use of the global
     evdns_base is error-prone.
 */
struct evdns_base *evdns_get_global_base(void);

/**
  Shut down the asynchronous DNS resolver and terminate all active requests.

  If the 'fail_requests' option is enabled, all active requests will return
  an empty result with the error flag set to DNS_ERR_SHUTDOWN. Otherwise,
  the requests will be silently discarded.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_shutdown().

  @param fail_requests if zero, active requests will be aborted; if non-zero,
		active requests will return DNS_ERR_SHUTDOWN.
  @see evdns_init()
 */
void evdns_shutdown(int fail_requests);

/**
  Add a nameserver.

  The address should be an IPv4 address in network byte order.
  The type of address is chosen so that it matches in_addr.s_addr.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_nameserver_add().

  @param address an IP address in network byte order
  @return 0 if successful, or -1 if an error occurred
  @see evdns_nameserver_ip_add()
 */
int evdns_nameserver_add(unsigned long int address);

/**
  Get the number of configured nameservers.

  This returns the number of configured nameservers (not necessarily the
  number of running nameservers).  This is useful for double-checking
  whether our calls to the various nameserver configuration functions
  have been successful.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_count_nameservers().

  @return the number of configured nameservers
  @see evdns_nameserver_add()
 */
int evdns_count_nameservers(void);

/**
  Remove all configured nameservers, and suspend all pending resolves.

  Resolves will not necessarily be re-attempted until evdns_resume() is called.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_clear_nameservers_and_suspend().

  @return 0 if successful, or -1 if an error occurred
  @see evdns_resume()
 */
int evdns_clear_nameservers_and_suspend(void);

/**
  Resume normal operation and continue any suspended resolve requests.

  Re-attempt resolves left in limbo after an earlier call to
  evdns_clear_nameservers_and_suspend().

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_resume().

  @return 0 if successful, or -1 if an error occurred
  @see evdns_clear_nameservers_and_suspend()
 */
int evdns_resume(void);

/**
  Add a nameserver.

  This wraps the evdns_nameserver_add() function by parsing a string as an IP
  address and adds it as a nameserver.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_nameserver_ip_add().

  @return 0 if successful, or -1 if an error occurred
  @see evdns_nameserver_add()
 */
int evdns_nameserver_ip_add(const char *ip_as_string);

/**
  Lookup an A record for a given name.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_resolve_ipv4().

  @param name a DNS hostname
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return 0 if successful, or -1 if an error occurred
  @see evdns_resolve_ipv6(), evdns_resolve_reverse(), evdns_resolve_reverse_ipv6()
 */
int evdns_resolve_ipv4(const char *name, int flags, evdns_callback_type callback, void *ptr);

/**
  Lookup an AAAA record for a given name.

  @param name a DNS hostname
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return 0 if successful, or -1 if an error occurred
  @see evdns_resolve_ipv4(), evdns_resolve_reverse(), evdns_resolve_reverse_ipv6()
 */
int evdns_resolve_ipv6(const char *name, int flags, evdns_callback_type callback, void *ptr);

struct in_addr;
struct in6_addr;

/**
  Lookup a PTR record for a given IP address.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_resolve_reverse().

  @param in an IPv4 address
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return 0 if successful, or -1 if an error occurred
  @see evdns_resolve_reverse_ipv6()
 */
int evdns_resolve_reverse(const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr);

/**
  Lookup a PTR record for a given IPv6 address.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_resolve_reverse_ipv6().

  @param in an IPv6 address
  @param flags either 0, or DNS_QUERY_NO_SEARCH to disable searching for this query.
  @param callback a callback function to invoke when the request is completed
  @param ptr an argument to pass to the callback function
  @return 0 if successful, or -1 if an error occurred
  @see evdns_resolve_reverse_ipv6()
 */
int evdns_resolve_reverse_ipv6(const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr);

/**
  Set the value of a configuration option.

  The currently available configuration options are:

    ndots, timeout, max-timeouts, max-inflight, and attempts

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_set_option().

  @param option the name of the configuration option to be modified
  @param val the value to be set
  @param flags Ignored.
  @return 0 if successful, or -1 if an error occurred
 */
int evdns_set_option(const char *option, const char *val, int flags);

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

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_resolv_conf_parse().

  @param flags any of DNS_OPTION_NAMESERVERS|DNS_OPTION_SEARCH|DNS_OPTION_MISC|
    DNS_OPTIONS_ALL
  @param filename the path to the resolv.conf file
  @return 0 if successful, or various positive error codes if an error
    occurred (see above)
  @see resolv.conf(3), evdns_config_windows_nameservers()
 */
int evdns_resolv_conf_parse(int flags, const char *const filename);

/**
  Clear the list of search domains.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_search_clear().
 */
void evdns_search_clear(void);

/**
  Add a domain to the list of search domains

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_search_add().

  @param domain the domain to be added to the search list
 */
void evdns_search_add(const char *domain);

/**
  Set the 'ndots' parameter for searches.

  Sets the number of dots which, when found in a name, causes
  the first query to be without any search domain.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which evdns_base it applies to.  The recommended
    function is evdns_base_search_ndots_set().

  @param ndots the new ndots parameter
 */
void evdns_search_ndots_set(const int ndots);

/**
   As evdns_server_new_with_base.

  @deprecated This function is deprecated because it does not allow the
    caller to specify which even_base it uses.  The recommended
    function is evdns_add_server_port_with_base().

*/
struct evdns_server_port *evdns_add_server_port(evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data);

#ifdef _WIN32
int evdns_config_windows_nameservers(void);
#define EVDNS_CONFIG_WINDOWS_NAMESERVERS_IMPLEMENTED
#endif

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_EVENT_COMPAT_H_INCLUDED_ */
