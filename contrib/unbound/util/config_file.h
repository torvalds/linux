/*
 * util/config_file.h - reads and stores the config file for unbound.
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
 * This file contains functions for the config file.
 */

#ifndef UTIL_CONFIG_FILE_H
#define UTIL_CONFIG_FILE_H
struct config_stub;
struct config_auth;
struct config_view;
struct config_strlist;
struct config_str2list;
struct config_str3list;
struct config_strbytelist;
struct module_qstate;
struct sock_list;
struct ub_packed_rrset_key;
struct regional;

/** List head for strlist processing, used for append operation. */
struct config_strlist_head {
	/** first in list of text items */
	struct config_strlist* first;
	/** last in list of text items */
	struct config_strlist* last;
};

/**
 * The configuration options.
 * Strings are malloced.
 */
struct config_file {
	/** verbosity level as specified in the config file */
	int verbosity;

	/** statistics interval (in seconds) */
	int stat_interval;
	/** if false, statistics values are reset after printing them */
	int stat_cumulative;
	/** if true, the statistics are kept in greater detail */
	int stat_extended;

	/** number of threads to create */
	int num_threads;

	/** port on which queries are answered. */
	int port;
	/** do ip4 query support. */
	int do_ip4;
	/** do ip6 query support. */
	int do_ip6;
	/** prefer ip6 upstream queries. */
	int prefer_ip6;
	/** do udp query support. */
	int do_udp;
	/** do tcp query support. */
	int do_tcp;
	/** tcp upstream queries (no UDP upstream queries) */
	int tcp_upstream;
	/** udp upstream enabled when no UDP downstream is enabled (do_udp no)*/
	int udp_upstream_without_downstream;
	/** maximum segment size of tcp socket which queries are answered */
	int tcp_mss;
	/** maximum segment size of tcp socket for outgoing queries */
	int outgoing_tcp_mss;
	/** tcp idle timeout, in msec */
	int tcp_idle_timeout;
	/** do edns tcp keepalive */
	int do_tcp_keepalive;
	/** tcp keepalive timeout, in msec */
	int tcp_keepalive_timeout;

	/** private key file for dnstcp-ssl service (enabled if not NULL) */
	char* ssl_service_key;
	/** public key file for dnstcp-ssl service */
	char* ssl_service_pem;
	/** port on which to provide ssl service */
	int ssl_port;
	/** if outgoing tcp connections use SSL */
	int ssl_upstream;
	/** cert bundle for outgoing connections */
	char* tls_cert_bundle;
	/** should the system certificate store get added to the cert bundle */
	int tls_win_cert;
	/** additional tls ports */
	struct config_strlist* tls_additional_port;

	/** outgoing port range number of ports (per thread) */
	int outgoing_num_ports;
	/** number of outgoing tcp buffers per (per thread) */
	size_t outgoing_num_tcp;
	/** number of incoming tcp buffers per (per thread) */
	size_t incoming_num_tcp;
	/** allowed udp port numbers, array with 0 if not allowed */
	int* outgoing_avail_ports;

	/** EDNS buffer size to use */
	size_t edns_buffer_size;
	/** number of bytes buffer size for DNS messages */
	size_t msg_buffer_size;
	/** size of the message cache */
	size_t msg_cache_size;
	/** slabs in the message cache. */
	size_t msg_cache_slabs;
	/** number of queries every thread can service */
	size_t num_queries_per_thread;
	/** number of msec to wait before items can be jostled out */
	size_t jostle_time;
	/** size of the rrset cache */
	size_t rrset_cache_size;
	/** slabs in the rrset cache */
	size_t rrset_cache_slabs;
	/** host cache ttl in seconds */
	int host_ttl;
	/** number of slabs in the infra host cache */
	size_t infra_cache_slabs;
	/** max number of hosts in the infra cache */
	size_t infra_cache_numhosts;
	/** min value for infra cache rtt */
	int infra_cache_min_rtt;
	/** delay close of udp-timeouted ports, if 0 no delayclose. in msec */
	int delay_close;

	/** the target fetch policy for the iterator */
	char* target_fetch_policy;
	/** percent*10, how many times in 1000 to pick low rtt destinations */
	int low_rtt_permil;
	/** what time in msec is a low rtt destination */
	int low_rtt;

	/** automatic interface for incoming messages. Uses ipv6 remapping,
	 * and recvmsg/sendmsg ancillary data to detect interfaces, boolean */
	int if_automatic;
	/** SO_RCVBUF size to set on port 53 UDP socket */
	size_t so_rcvbuf;
	/** SO_SNDBUF size to set on port 53 UDP socket */
	size_t so_sndbuf;
	/** SO_REUSEPORT requested on port 53 sockets */
	int so_reuseport;
	/** IP_TRANSPARENT socket option requested on port 53 sockets */
	int ip_transparent;
	/** IP_FREEBIND socket option request on port 53 sockets */
	int ip_freebind;

	/** number of interfaces to open. If 0 default all interfaces. */
	int num_ifs;
	/** interface description strings (IP addresses) */
	char **ifs;

	/** number of outgoing interfaces to open. 
	 * If 0 default all interfaces. */
	int num_out_ifs;
	/** outgoing interface description strings (IP addresses) */
	char **out_ifs;

	/** the root hints */
	struct config_strlist* root_hints;
	/** the stub definitions, linked list */
	struct config_stub* stubs;
	/** the forward zone definitions, linked list */
	struct config_stub* forwards;
	/** the auth zone definitions, linked list */
	struct config_auth* auths;
	/** the views definitions, linked list */
	struct config_view* views;
	/** list of donotquery addresses, linked list */
	struct config_strlist* donotqueryaddrs;
#ifdef CLIENT_SUBNET
	/** list of servers we send edns-client-subnet option to and 
	 * accept option from, linked list */
	struct config_strlist* client_subnet;
	/** list of zones we send edns-client-subnet option for */
	struct config_strlist* client_subnet_zone;
	/** opcode assigned by IANA for edns0-client-subnet option */
	uint16_t client_subnet_opcode;
	/** Do not check whitelist if incoming query contains an ECS record */
	int client_subnet_always_forward;
	/** Subnet length we are willing to give up privacy for */
	uint8_t max_client_subnet_ipv4;
	uint8_t max_client_subnet_ipv6;
#endif
	/** list of access control entries, linked list */
	struct config_str2list* acls;
	/** use default localhost donotqueryaddr entries */
	int donotquery_localhost;

	/** list of tcp connection limitss, linked list */
	struct config_str2list* tcp_connection_limits;

	/** harden against very small edns buffer sizes */
	int harden_short_bufsize;
	/** harden against very large query sizes */
	int harden_large_queries;
	/** harden against spoofed glue (out of zone data) */
	int harden_glue;
	/** harden against receiving no DNSSEC data for trust anchor */
	int harden_dnssec_stripped;
	/** harden against queries that fall under known nxdomain names */
	int harden_below_nxdomain;
	/** harden the referral path, query for NS,A,AAAA and validate */
	int harden_referral_path;
	/** harden against algorithm downgrade */
	int harden_algo_downgrade;
	/** use 0x20 bits in query as random ID bits */
	int use_caps_bits_for_id;
	/** 0x20 whitelist, domains that do not use capsforid */
	struct config_strlist* caps_whitelist;
	/** strip away these private addrs from answers, no DNS Rebinding */
	struct config_strlist* private_address;
	/** allow domain (and subdomains) to use private address space */
	struct config_strlist* private_domain;
	/** what threshold for unwanted action. */
	size_t unwanted_threshold;
	/** the number of seconds maximal TTL used for RRsets and messages */
	int max_ttl;
	/** the number of seconds minimum TTL used for RRsets and messages */
	int min_ttl;
	/** the number of seconds maximal negative TTL for SOA in auth */
	int max_negative_ttl;
	/** if prefetching of messages should be performed. */
	int prefetch;
	/** if prefetching of DNSKEYs should be performed. */
	int prefetch_key;

	/** chrootdir, if not "" or chroot will be done */
	char* chrootdir;
	/** username to change to, if not "". */
	char* username;
	/** working directory */
	char* directory;
	/** filename to log to. */
	char* logfile;
	/** pidfile to write pid to. */
	char* pidfile;

	/** should log messages be sent to syslogd */
	int use_syslog;
	/** log timestamp in ascii UTC */
	int log_time_ascii;
	/** log queries with one line per query */
	int log_queries;
	/** log replies with one line per reply */
	int log_replies;
	/** log every local-zone hit **/
	int log_local_actions;
	/** log servfails with a reason */
	int log_servfail;
	/** log identity to report */
	char* log_identity;

	/** do not report identity (id.server, hostname.bind) */
	int hide_identity;
	/** do not report version (version.server, version.bind) */
	int hide_version;
	/** do not report trustanchor (trustanchor.unbound) */
	int hide_trustanchor;
	/** identity, hostname is returned if "". */
	char* identity;
	/** version, package version returned if "". */
	char* version;

	/** the module configuration string */
	char* module_conf;
	
	/** files with trusted DS and DNSKEYs in zonefile format, list */
	struct config_strlist* trust_anchor_file_list;
	/** list of trustanchor keys, linked list */
	struct config_strlist* trust_anchor_list;
	/** files with 5011 autotrust tracked keys */
	struct config_strlist* auto_trust_anchor_file_list;
	/** files with trusted DNSKEYs in named.conf format, list */
	struct config_strlist* trusted_keys_file_list;
	/** DLV anchor file */
	char* dlv_anchor_file;
	/** DLV anchor inline */
	struct config_strlist* dlv_anchor_list;
	/** insecure domain list */
	struct config_strlist* domain_insecure;
	/** send key tag query */
	int trust_anchor_signaling;
	/** enable root key sentinel */
	int root_key_sentinel;

	/** if not 0, this value is the validation date for RRSIGs */
	int32_t val_date_override;
	/** the minimum for signature clock skew */
	int32_t val_sig_skew_min;
	/** the maximum for signature clock skew */
	int32_t val_sig_skew_max;
	/** this value sets the number of seconds before revalidating bogus */
	int bogus_ttl; 
	/** should validator clean additional section for secure msgs */
	int val_clean_additional;
	/** log bogus messages by the validator */
	int val_log_level;
	/** squelch val_log_level to log - this is library goes to callback */
	int val_log_squelch;
	/** should validator allow bogus messages to go through */
	int val_permissive_mode;
	/** use cached NSEC records to synthesise (negative) answers */
	int aggressive_nsec;
	/** ignore the CD flag in incoming queries and refuse them bogus data */
	int ignore_cd;
	/** serve expired entries and prefetch them */
	int serve_expired;
	/** serve expired entries until TTL after expiration */
	int serve_expired_ttl;
	/** reset serve expired TTL after failed update attempt */
	int serve_expired_ttl_reset;
	/** nsec3 maximum iterations per key size, string */
	char* val_nsec3_key_iterations;
	/** autotrust add holddown time, in seconds */
	unsigned int add_holddown;
	/** autotrust del holddown time, in seconds */
	unsigned int del_holddown;
	/** autotrust keep_missing time, in seconds. 0 is forever. */
	unsigned int keep_missing;
	/** permit small holddown values, allowing 5011 rollover very fast */
	int permit_small_holddown;

	/** size of the key cache */
	size_t key_cache_size;
	/** slabs in the key cache. */
	size_t key_cache_slabs;
	/** size of the neg cache */
	size_t neg_cache_size;

	/** local zones config */
	struct config_str2list* local_zones;
	/** local zones nodefault list */
	struct config_strlist* local_zones_nodefault;
	/** do not add any default local zone */
	int local_zones_disable_default;
	/** local data RRs configured */
	struct config_strlist* local_data;
	/** local zone override types per netblock */
	struct config_str3list* local_zone_overrides;
	/** unblock lan zones (reverse lookups for AS112 zones) */
	int unblock_lan_zones;
	/** insecure lan zones (don't validate AS112 zones) */
	int insecure_lan_zones;
	/** list of zonename, tagbitlist */
	struct config_strbytelist* local_zone_tags;
	/** list of aclname, tagbitlist */
	struct config_strbytelist* acl_tags;
	/** list of aclname, tagname, localzonetype */
	struct config_str3list* acl_tag_actions;
	/** list of aclname, tagname, redirectdata */
	struct config_str3list* acl_tag_datas;
	/** list of aclname, view*/
	struct config_str2list* acl_view;
	/** list of IP-netblock, tagbitlist */
	struct config_strbytelist* respip_tags;
	/** list of response-driven access control entries, linked list */
	struct config_str2list* respip_actions;
	/** RRs configured for response-driven access controls */
	struct config_str2list* respip_data;
	/** tag list, array with tagname[i] is malloced string */
	char** tagname;
	/** number of items in the taglist */
	int num_tags;

	/** remote control section. enable toggle. */
	int remote_control_enable;
	/** the interfaces the remote control should listen on */
	struct config_strlist_head control_ifs;
	/** if the use-cert option is set */
	int control_use_cert;
	/** port number for the control port */
	int control_port;
	/** private key file for server */
	char* server_key_file;
	/** certificate file for server */
	char* server_cert_file;
	/** private key file for unbound-control */
	char* control_key_file;
	/** certificate file for unbound-control */
	char* control_cert_file;

	/** Python script file */
	char* python_script;

	/** Use systemd socket activation. */
	int use_systemd;

	/** daemonize, i.e. fork into the background. */
	int do_daemonize;

	/* minimal response when positive answer */
	int minimal_responses;

	/* RRSet roundrobin */
	int rrset_roundrobin;

	/* maximum UDP response size */
	size_t max_udp_size;

	/* DNS64 prefix */
	char* dns64_prefix;

	/* Synthetize all AAAA record despite the presence of an authoritative one */
	int dns64_synthall;
	/** ignore AAAAs for these domain names and use A record anyway */
	struct config_strlist* dns64_ignore_aaaa;

	/** true to enable dnstap support */
	int dnstap;
	/** dnstap socket path */
	char* dnstap_socket_path;
	/** true to send "identity" via dnstap */
	int dnstap_send_identity;
	/** true to send "version" via dnstap */
	int dnstap_send_version;
	/** dnstap "identity", hostname is used if "". */
	char* dnstap_identity;
	/** dnstap "version", package version is used if "". */
	char* dnstap_version;

	/** true to log dnstap RESOLVER_QUERY message events */
	int dnstap_log_resolver_query_messages;
	/** true to log dnstap RESOLVER_RESPONSE message events */
	int dnstap_log_resolver_response_messages;
	/** true to log dnstap CLIENT_QUERY message events */
	int dnstap_log_client_query_messages;
	/** true to log dnstap CLIENT_RESPONSE message events */
	int dnstap_log_client_response_messages;
	/** true to log dnstap FORWARDER_QUERY message events */
	int dnstap_log_forwarder_query_messages;
	/** true to log dnstap FORWARDER_RESPONSE message events */
	int dnstap_log_forwarder_response_messages;

	/** true to disable DNSSEC lameness check in iterator */
	int disable_dnssec_lame_check;

	/** ratelimit for ip addresses. 0 is off, otherwise qps (unless overridden) */
	int ip_ratelimit;
	/** number of slabs for ip_ratelimit cache */
	size_t ip_ratelimit_slabs;
	/** memory size in bytes for ip_ratelimit cache */
	size_t ip_ratelimit_size;
	/** ip_ratelimit factor, 0 blocks all, 10 allows 1/10 of traffic */
	int ip_ratelimit_factor;

	/** ratelimit for domains. 0 is off, otherwise qps (unless overridden) */
	int ratelimit;
	/** number of slabs for ratelimit cache */
	size_t ratelimit_slabs;
	/** memory size in bytes for ratelimit cache */
	size_t ratelimit_size;
	/** ratelimits for domain (exact match) */
	struct config_str2list* ratelimit_for_domain;
	/** ratelimits below domain */
	struct config_str2list* ratelimit_below_domain;
	/** ratelimit factor, 0 blocks all, 10 allows 1/10 of traffic */
	int ratelimit_factor;
	/** minimise outgoing QNAME and hide original QTYPE if possible */
	int qname_minimisation;
	/** minimise QNAME in strict mode, minimise according to RFC.
	 *  Do not apply fallback */
	int qname_minimisation_strict;
	/** SHM data - true if shm is enabled */
	int shm_enable;
	/** SHM data - key for the shm */
	int shm_key;

	/** DNSCrypt */
	/** true to enable dnscrypt */
	int dnscrypt;
	/** port on which to provide dnscrypt service */
	int dnscrypt_port;
	/** provider name 2.dnscrypt-cert.example.com */
	char* dnscrypt_provider;
	/** dnscrypt secret keys 1.key */
	struct config_strlist* dnscrypt_secret_key;
	/** dnscrypt provider certs 1.cert */
	struct config_strlist* dnscrypt_provider_cert;
	/** dnscrypt provider certs 1.cert which have been rotated and should not be
	* advertised through DNS's providername TXT record but are required to be
	* able to handle existing traffic using the old cert. */
	struct config_strlist* dnscrypt_provider_cert_rotated;
	/** memory size in bytes for dnscrypt shared secrets cache */
	size_t dnscrypt_shared_secret_cache_size;
	/** number of slabs for dnscrypt shared secrets cache */
	size_t dnscrypt_shared_secret_cache_slabs;
	/** memory size in bytes for dnscrypt nonces cache */
	size_t dnscrypt_nonce_cache_size;
	/** number of slabs for dnscrypt nonces cache */
	size_t dnscrypt_nonce_cache_slabs;
	/** IPsec module */
#ifdef USE_IPSECMOD
	/** false to bypass the IPsec module */
	int ipsecmod_enabled;
	/** whitelisted domains for ipsecmod */
	struct config_strlist* ipsecmod_whitelist;
	/** path to external hook */
	char* ipsecmod_hook;
	/** true to proceed even with a bogus IPSECKEY */
	int ipsecmod_ignore_bogus;
	/** max TTL for the A/AAAA records that call the hook */
	int ipsecmod_max_ttl;
	/** false to proceed even when ipsecmod_hook fails */
	int ipsecmod_strict;
#endif

	/* cachedb module */
#ifdef USE_CACHEDB
	/** backend DB name */
	char* cachedb_backend;
	/** secret seed for hash key calculation */
	char* cachedb_secret;
#ifdef USE_REDIS
	/** redis server's IP address or host name */
	char* redis_server_host;
	/** redis server's TCP port */
	int redis_server_port;
	/** timeout (in ms) for communication with the redis server */
	int redis_timeout;
#endif
#endif
};

/** from cfg username, after daemonize setup performed */
extern uid_t cfg_uid;
/** from cfg username, after daemonize setup performed */
extern gid_t cfg_gid;
/** debug and enable small timeouts */
extern int autr_permit_small_holddown;

/**
 * Stub config options
 */
struct config_stub {
	/** next in list */
	struct config_stub* next;
	/** domain name (in text) of the stub apex domain */
	char* name;
	/** list of stub nameserver hosts (domain name) */
	struct config_strlist* hosts;
	/** list of stub nameserver addresses (IP address) */
	struct config_strlist* addrs;
	/** if stub-prime is set */
	int isprime;
	/** if forward-first is set (failover to without if fails) */
	int isfirst;
	/** use SSL for queries to this stub */
	int ssl_upstream;
	/*** no cache */
	int no_cache;
};

/**
 * Auth config options
 */
struct config_auth {
	/** next in list */
	struct config_auth* next;
	/** domain name (in text) of the auth apex domain */
	char* name;
	/** list of masters */
	struct config_strlist* masters;
	/** list of urls */
	struct config_strlist* urls;
	/** list of allow-notify */
	struct config_strlist* allow_notify;
	/** zonefile (or NULL) */
	char* zonefile;
	/** provide downstream answers */
	int for_downstream;
	/** provide upstream answers */
	int for_upstream;
	/** fallback to recursion to authorities if zone expired and other
	 * reasons perhaps (like, query bogus) */
	int fallback_enabled;
};

/**
 * View config options
 */
struct config_view {
	/** next in list */
	struct config_view* next;
	/** view name */
	char* name;
	/** local zones */
	struct config_str2list* local_zones;
	/** local data RRs */
	struct config_strlist* local_data;
	/** local zones nodefault list */
	struct config_strlist* local_zones_nodefault;
	/** Fallback to global local_zones when there is no match in the view
	 * view specific tree. 1 for yes, 0 for no */	
	int isfirst;
	/** predefined actions for particular IP address responses */
	struct config_str2list* respip_actions;
	/** data complementing the 'redirect' response IP actions */
	struct config_str2list* respip_data;
};

/**
 * List of strings for config options
 */
struct config_strlist {
	/** next item in list */
	struct config_strlist* next;
	/** config option string */
	char* str;
};

/**
 * List of two strings for config options
 */
struct config_str2list {
	/** next item in list */
	struct config_str2list* next;
	/** first string */
	char* str;
	/** second string */
	char* str2;
};

/**
 * List of three strings for config options
 */
struct config_str3list {
	/** next item in list */
	struct config_str3list* next;
	/** first string */
	char* str;
	/** second string */
	char* str2;
	/** third string */
	char* str3;
};


/**
 * List of string, bytestring for config options
 */
struct config_strbytelist {
	/** next item in list */
	struct config_strbytelist* next;
	/** first string */
	char* str;
	/** second bytestring */
	uint8_t* str2;
	size_t str2len;
};

/**
 * Create config file structure. Filled with default values.
 * @return: the new structure or NULL on memory error.
 */
struct config_file* config_create(void);

/**
 * Create config file structure for library use. Filled with default values.
 * @return: the new structure or NULL on memory error.
 */
struct config_file* config_create_forlib(void);

/**
 * Read the config file from the specified filename.
 * @param config: where options are stored into, must be freshly created.
 * @param filename: name of configfile. If NULL nothing is done.
 * @param chroot: if not NULL, the chroot dir currently in use (for include).
 * @return: false on error. In that case errno is set, ENOENT means 
 * 	file not found.
 */
int config_read(struct config_file* config, const char* filename,
	const char* chroot);

/**
 * Destroy the config file structure.
 * @param config: to delete.
 */
void config_delete(struct config_file* config);

/**
 * Apply config to global constants; this routine is called in single thread.
 * @param config: to apply. Side effect: global constants change.
 */
void config_apply(struct config_file* config);

/**
 * Find username, sets cfg_uid and cfg_gid.
 * @param config: the config structure.
 */
void config_lookup_uid(struct config_file* config);

/**
 * Set the given keyword to the given value.
 * @param config: where to store config
 * @param option: option name, including the ':' character.
 * @param value: value, this string is copied if needed, or parsed.
 * 	The caller owns the value string.
 * @return 0 on error (malloc or syntax error).
 */
int config_set_option(struct config_file* config, const char* option,
	const char* value);

/** 
 * Call print routine for the given option.
 * @param cfg: config.
 * @param opt: option name without trailing :. 
 *	This is different from config_set_option.
 * @param func: print func, called as (str, arg) for every data element.
 * @param arg: user argument for print func.
 * @return false if the option name is not supported (syntax error).
 */
int config_get_option(struct config_file* cfg, const char* opt, 
	void (*func)(char*,void*), void* arg);

/**
 * Get an option and return strlist
 * @param cfg: config file
 * @param opt: option name.
 * @param list: list is returned here. malloced, caller must free it.
 * @return 0=OK, 1=syntax error, 2=malloc failed.
 */
int config_get_option_list(struct config_file* cfg, const char* opt,
	struct config_strlist** list);

/**
 * Get an option and collate results into string
 * @param cfg: config file
 * @param opt: option name.
 * @param str: string. malloced, caller must free it.
 * @return 0=OK, 1=syntax error, 2=malloc failed.
 */
int config_get_option_collate(struct config_file* cfg, const char* opt, 
	char** str);

/**
 * function to print to a file, use as func with config_get_option.
 * @param line: text to print. \n appended.
 * @param arg: pass a FILE*, like stdout.
 */
void config_print_func(char* line, void* arg);

/**
 * function to collate the text strings into a strlist_head.
 * @param line: text to append.
 * @param arg: pass a strlist_head structure. zeroed on start.
 */
void config_collate_func(char* line, void* arg);

/**
 * take a strlist_head list and return a malloc string. separated with newline.
 * @param list: strlist first to collate. zeroes return "".
 * @return NULL on malloc failure. Or if malloc failure happened in strlist.
 */
char* config_collate_cat(struct config_strlist* list);

/**
 * Append text at end of list.
 * @param list: list head. zeroed at start.
 * @param item: new item. malloced by caller. if NULL the insertion fails.
 * @return true on success.
 * on fail the item is free()ed.
 */
int cfg_strlist_append(struct config_strlist_head* list, char* item);

/**
 * Find string in strlist.
 * @param head: pointer to strlist head variable.
 * @param item: the item to search for.
 * @return: the element in the list when found, NULL otherwise.
 */
struct config_strlist* cfg_strlist_find(struct config_strlist* head,
	const char* item);

/**
 * Insert string into strlist.
 * @param head: pointer to strlist head variable.
 * @param item: new item. malloced by caller. If NULL the insertion fails.
 * @return: true on success.
 * on fail, the item is free()d.
 */
int cfg_strlist_insert(struct config_strlist** head, char* item);

/** insert with region for allocation. */
int cfg_region_strlist_insert(struct regional* region,
	struct config_strlist** head, char* item);

/**
 * Insert string into str2list.
 * @param head: pointer to str2list head variable.
 * @param item: new item. malloced by caller. If NULL the insertion fails.
 * @param i2: 2nd string, malloced by caller. If NULL the insertion fails.
 * @return: true on success.
 * on fail, the item and i2 are free()d.
 */
int cfg_str2list_insert(struct config_str2list** head, char* item, char* i2);

/**
 * Insert string into str3list.
 * @param head: pointer to str3list head variable.
 * @param item: new item. malloced by caller. If NULL the insertion fails.
 * @param i2: 2nd string, malloced by caller. If NULL the insertion fails.
 * @param i3: 3rd string, malloced by caller. If NULL the insertion fails.
 * @return: true on success.
 */
int cfg_str3list_insert(struct config_str3list** head, char* item, char* i2,
	char* i3);

/**
 * Insert string into strbytelist.
 * @param head: pointer to strbytelist head variable.
 * @param item: new item. malloced by caller. If NULL the insertion fails.
 * @param i2: 2nd string, malloced by caller. If NULL the insertion fails.
 * @param i2len: length of the i2 bytestring.
 * @return: true on success.
 */
int cfg_strbytelist_insert(struct config_strbytelist** head, char* item,
	uint8_t* i2, size_t i2len);

/**
 * Find stub in config list, also returns prevptr (for deletion).
 * @param pp: call routine with pointer to a pointer to the start of the list,
 * 	if the stub is found, on exit, the value contains a pointer to the
 * 	next pointer that points to the found element (or to the list start
 * 	pointer if it is the first element).
 * @param nm: name of stub to find.
 * @return: pointer to config_stub if found, or NULL if not found.
 */
struct config_stub* cfg_stub_find(struct config_stub*** pp, const char* nm);

/**
 * Delete items in config string list.
 * @param list: list.
 */
void config_delstrlist(struct config_strlist* list);

/**
 * Delete items in config double string list.
 * @param list: list.
 */
void config_deldblstrlist(struct config_str2list* list);

/**
 * Delete items in config triple string list.
 * @param list: list.
 */
void config_deltrplstrlist(struct config_str3list* list);

/** delete stringbytelist */
void config_del_strbytelist(struct config_strbytelist* list);

/**
 * Delete a stub item
 * @param p: stub item
 */
void config_delstub(struct config_stub* p);

/**
 * Delete items in config stub list.
 * @param list: list.
 */
void config_delstubs(struct config_stub* list);

/**
 * Delete an auth item
 * @param p: auth item
 */
void config_delauth(struct config_auth* p);

/**
 * Delete items in config auth list.
 * @param list: list.
 */
void config_delauths(struct config_auth* list);

/**
 * Delete a view item
 * @param p: view item
 */
void config_delview(struct config_view* p);

/**
 * Delete items in config view list.
 * @param list: list.
 */
void config_delviews(struct config_view* list);

/** check if config for remote control turns on IP-address interface
 * with certificates or a named pipe without certificates. */
int options_remote_is_address(struct config_file* cfg);

/**
 * Convert 14digit to time value
 * @param str: string of 14 digits
 * @return time value or 0 for error.
 */
time_t cfg_convert_timeval(const char* str);

/**
 * Count number of values in the string.
 * format ::= (sp num)+ sp
 * num ::= [-](0-9)+
 * sp ::= (space|tab)*
 *
 * @param str: string
 * @return: 0 on parse error, or empty string, else
 *	number of integer values in the string.
 */
int cfg_count_numbers(const char* str);

/**
 * Convert a 'nice' memory or file size into a bytecount
 * From '100k' to 102400. and so on. Understands kKmMgG.
 * k=1024, m=1024*1024, g=1024*1024*1024.
 * @param str: string
 * @param res: result is stored here, size in bytes.
 * @return: true if parsed correctly, or 0 on a parse error (and an error 
 * is logged).
 */
int cfg_parse_memsize(const char* str, size_t* res);

/**
 * Add a tag name to the config.  It is added at the end with a new ID value.
 * @param cfg: the config structure.
 * @param tag: string (which is copied) with the name.
 * @return: false on alloc failure.
 */
int config_add_tag(struct config_file* cfg, const char* tag);

/**
 * Find tag ID in the tag list.
 * @param cfg: the config structure.
 * @param tag: string with tag name to search for.
 * @return: 0..(num_tags-1) with tag ID, or -1 if tagname is not found.
 */
int find_tag_id(struct config_file* cfg, const char* tag);

/**
 * parse taglist from string into bytestring with bitlist.
 * @param cfg: the config structure (with tagnames)
 * @param str: the string to parse.  Parse puts 0 bytes in string. 
 * @param listlen: returns length of in bytes.
 * @return malloced bytes with a bitlist of the tags.  or NULL on parse error
 * or malloc failure.
 */
uint8_t* config_parse_taglist(struct config_file* cfg, char* str,
	size_t* listlen);

/**
 * convert tag bitlist to a malloced string with tag names.  For debug output.
 * @param cfg: the config structure (with tagnames)
 * @param taglist: the tag bitlist.
 * @param len: length of the tag bitlist.
 * @return malloced string or NULL.
 */
char* config_taglist2str(struct config_file* cfg, uint8_t* taglist,
	size_t len);

/**
 * see if two taglists intersect (have tags in common).
 * @param list1: first tag bitlist.
 * @param list1len: length in bytes of first list.
 * @param list2: second tag bitlist.
 * @param list2len: length in bytes of second list.
 * @return true if there are tags in common, 0 if not.
 */
int taglist_intersect(uint8_t* list1, size_t list1len, uint8_t* list2,
	size_t list2len);

/**
 * Parse local-zone directive into two strings and register it in the config.
 * @param cfg: to put it in.
 * @param val: argument strings to local-zone, "example.com nodefault".
 * @return: false on failure
 */
int cfg_parse_local_zone(struct config_file* cfg, const char* val);

/**
 * Mark "number" or "low-high" as available or not in ports array.
 * @param str: string in input
 * @param allow: give true if this range is permitted.
 * @param avail: the array from cfg.
 * @param num: size of the array (65536).
 * @return: true if parsed correctly, or 0 on a parse error (and an error 
 * is logged).
 */
int cfg_mark_ports(const char* str, int allow, int* avail, int num);

/**
 * Get a condensed list of ports returned. allocated.
 * @param cfg: config file.
 * @param avail: the available ports array is returned here.
 * @return: number of ports in array or 0 on error.
 */
int cfg_condense_ports(struct config_file* cfg, int** avail);

/**
 * Scan ports available
 * @param avail: the array from cfg.
 * @param num: size of the array (65536).
 * @return the number of ports available for use.
 */
int cfg_scan_ports(int* avail, int num);

/** 
 * Convert a filename to full pathname in original filesys
 * @param fname: the path name to convert.
 *      Must not be null or empty.
 * @param cfg: config struct for chroot and chdir (if set).
 * @param use_chdir: if false, only chroot is applied.
 * @return pointer to malloced buffer which is: [chroot][chdir]fname
 *      or NULL on malloc failure.
 */
char* fname_after_chroot(const char* fname, struct config_file* cfg, 
	int use_chdir);

/**
 * Convert a ptr shorthand into a full reverse-notation PTR record.
 * @param str: input string, "IP name"
 * @return: malloced string "reversed-ip-name PTR name"
 */
char* cfg_ptr_reverse(char* str);

/**
 * Append text to the error info for validation.
 * @param qstate: query state.
 * @param str: copied into query region and appended.
 * Failures to allocate are logged.
 */
void errinf(struct module_qstate* qstate, const char* str);

/**
 * Append text to error info:  from 1.2.3.4
 * @param qstate: query state.
 * @param origin: sock list with origin of trouble. 
 *	Every element added.
 *	If NULL: nothing is added.
 *	if 0len element: 'from cache' is added.
 */
void errinf_origin(struct module_qstate* qstate, struct sock_list *origin);

/**
 * Append text to error info:  for RRset name type class
 * @param qstate: query state.
 * @param rr: rrset_key.
 */
void errinf_rrset(struct module_qstate* qstate, struct ub_packed_rrset_key *rr);

/**
 * Append text to error info:  str dname
 * @param qstate: query state.
 * @param str: explanation string
 * @param dname: the dname.
 */
void errinf_dname(struct module_qstate* qstate, const char* str, 
	uint8_t* dname);

/**
 * Create error info in string.  For validation failures.
 * @param qstate: query state.
 * @return string or NULL on malloc failure (already logged).
 *    This string is malloced and has to be freed by caller.
 */
char* errinf_to_str_bogus(struct module_qstate* qstate);

/**
 * Create error info in string.  For other servfails.
 * @param qstate: query state.
 * @return string or NULL on malloc failure (already logged).
 *    This string is malloced and has to be freed by caller.
 */
char* errinf_to_str_servfail(struct module_qstate* qstate);

/**
 * Used during options parsing
 */
struct config_parser_state {
	/** name of file being parser */
	char* filename;
	/** line number in the file, starts at 1 */
	int line;
	/** number of errors encountered */
	int errors;
	/** the result of parsing is stored here. */
	struct config_file* cfg;
	/** the current chroot dir (or NULL if none) */
	const char* chroot;
};

/** global config parser object used during config parsing */
extern struct config_parser_state* cfg_parser;
/** init lex state */
void init_cfg_parse(void);
/** lex in file */
extern FILE* ub_c_in;
/** lex out file */
extern FILE* ub_c_out;
/** the yacc lex generated parse function */
int ub_c_parse(void);
/** the lexer function */
int ub_c_lex(void);
/** wrap function */
int ub_c_wrap(void);
/** parsing helpers: print error with file and line numbers */
void ub_c_error(const char* msg);
/** parsing helpers: print error with file and line numbers */
void ub_c_error_msg(const char* fmt, ...) ATTR_FORMAT(printf, 1, 2);

#ifdef UB_ON_WINDOWS
/**
 * Obtain registry string (if it exists).
 * @param key: key string
 * @param name: name of value to fetch.
 * @return malloced string with the result or NULL if it did not
 * 	exist on an error (logged with log_err) was encountered.
 */
char* w_lookup_reg_str(const char* key, const char* name);

/** Modify directory in options for module file name */
void w_config_adjust_directory(struct config_file* cfg);
#endif /* UB_ON_WINDOWS */

/** debug option for unit tests. */
extern int fake_dsa, fake_sha1;

#endif /* UTIL_CONFIG_FILE_H */
