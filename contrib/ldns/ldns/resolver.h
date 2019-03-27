/*
 * resolver.h
 *
 * DNS Resolver definitions
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Defines the  ldns_resolver structure, a stub resolver that can send queries and parse answers.
 *
 */

#ifndef LDNS_RESOLVER_H
#define LDNS_RESOLVER_H

#include <ldns/error.h>
#include <ldns/common.h>
#include <ldns/rr.h>
#include <ldns/tsig.h>
#include <ldns/rdata.h>
#include <ldns/packet.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default location of the resolv.conf file */
#define LDNS_RESOLV_CONF	"/etc/resolv.conf"
/** Default location of the hosts file */
#define LDNS_RESOLV_HOSTS	"/etc/hosts"

#define LDNS_RESOLV_KEYWORD     -1
#define LDNS_RESOLV_DEFDOMAIN	0
#define LDNS_RESOLV_NAMESERVER	1
#define LDNS_RESOLV_SEARCH	2
#define LDNS_RESOLV_SORTLIST	3
#define LDNS_RESOLV_OPTIONS	4
#define LDNS_RESOLV_ANCHOR	5
#define LDNS_RESOLV_KEYWORDS    6

#define LDNS_RESOLV_INETANY		0
#define LDNS_RESOLV_INET		1
#define LDNS_RESOLV_INET6		2

#define LDNS_RESOLV_RTT_INF             0       /* infinity */
#define LDNS_RESOLV_RTT_MIN             1       /* reachable */

/**
 * DNS stub resolver structure
 */
struct ldns_struct_resolver
{
	/**  Port to send queries to */
	uint16_t _port;

	/** Array of nameservers to query (IP addresses or dnames) */
	ldns_rdf **_nameservers;
	/** Number of nameservers in \c _nameservers */
	size_t _nameserver_count; /* how many do we have */

	/**  Round trip time; 0 -> infinity. Unit: ms? */
	size_t *_rtt;

	/**  Whether or not to be recursive */
	bool _recursive;

	/**  Print debug information */
	bool _debug;

	/**  Default domain to add to non fully qualified domain names */
	ldns_rdf *_domain;

	/**  Searchlist array, add the names in this array if a query cannot be found */
	ldns_rdf **_searchlist;

	/** Number of entries in the searchlist array */
	size_t _searchlist_count;

	/**  Number of times to retry before giving up */
	uint8_t _retry;
	/**  Time to wait before retrying */
	uint8_t _retrans;
	/**  Use new fallback mechanism (try EDNS, then do TCP) */
	bool _fallback;

	/**  Whether to do DNSSEC */
	bool _dnssec;
	/**  Whether to set the CD bit on DNSSEC requests */
	bool _dnssec_cd;
	/** Optional trust anchors for complete DNSSEC validation */
	ldns_rr_list * _dnssec_anchors;
	/**  Whether to use tcp or udp (tcp if the value is true)*/
	bool _usevc;
	/**  Whether to ignore the tc bit */
	bool _igntc;
	/**  Whether to use ip6: 0->does not matter, 1 is IPv4, 2 is IPv6 */
	uint8_t _ip6;
	/**  If true append the default domain */
	bool _defnames;
	/**  If true apply the search list */
	bool _dnsrch;
	/**  Timeout for socket connections */
	struct timeval _timeout;
	/**  Only try the first nameserver, and return with an error directly if it fails */
	bool _fail;
	/**  Randomly choose a nameserver */
	bool _random;
	/** Keep some things to make AXFR possible */
	int _socket;
	/** Count the number of LDNS_RR_TYPE_SOA RRs we have seen so far
	 * (the second one signifies the end of the AXFR)
	 */
	int _axfr_soa_count;
	/* when axfring we get complete packets from the server
	   but we want to give the caller 1 rr at a time, so
	   keep the current pkt */
        /** Packet currently handled when doing part of an AXFR */
	ldns_pkt *_cur_axfr_pkt;
	/** Counter for within the AXFR packets */
	uint16_t _axfr_i;
	/* EDNS0 available buffer size */
	uint16_t _edns_udp_size;
	/* serial for IXFR */
	uint32_t _serial;

	/* Optional tsig key for signing queries,
	outgoing messages are signed if and only if both are set
	*/
	/** Name of the key to use with TSIG, if _tsig_keyname and _tsig_keydata both contain values, outgoing messages are automatically signed with TSIG. */
	char *_tsig_keyname;
	/** Secret key data to use with TSIG, if _tsig_keyname and _tsig_keydata both contain values, outgoing messages are automatically signed with TSIG. */
	char *_tsig_keydata;
	/** TSIG signing algorithm */
	char *_tsig_algorithm;

	/** Source address to query from */
	ldns_rdf *_source;
};
typedef struct ldns_struct_resolver ldns_resolver;

/* prototypes */
/* read access functions */

/**
 * Get the port the resolver should use
 * \param[in] r the resolver
 * \return the port number
 */
uint16_t ldns_resolver_port(const ldns_resolver *r);

/**
 * Get the source address the resolver should use
 * \param[in] r the resolver
 * \return the source rdf
 */
ldns_rdf *ldns_resolver_source(const ldns_resolver *r);

/**
 * Is the resolver set to recurse
 * \param[in] r the resolver
 * \return true if so, otherwise false
 */
bool ldns_resolver_recursive(const ldns_resolver *r);

/**
 * Get the debug status of the resolver
 * \param[in] r the resolver
 * \return true if so, otherwise false
 */
bool ldns_resolver_debug(const ldns_resolver *r);

/**
 * Get the number of retries
 * \param[in] r the resolver
 * \return the number of retries
 */
uint8_t ldns_resolver_retry(const ldns_resolver *r);

/**
 * Get the retransmit interval
 * \param[in] r the resolver
 * \return the retransmit interval
 */
uint8_t ldns_resolver_retrans(const ldns_resolver *r);

/**
 * Get the truncation fallback status
 * \param[in] r the resolver
 * \return whether the truncation fallback mechanism is used
 */
bool ldns_resolver_fallback(const ldns_resolver *r);

/**
 * Does the resolver use ip6 or ip4
 * \param[in] r the resolver
 * \return 0: both, 1: ip4, 2:ip6
 */
uint8_t ldns_resolver_ip6(const ldns_resolver *r);

/**
 * Get the resolver's udp size
 * \param[in] r the resolver
 * \return the udp mesg size
 */
uint16_t ldns_resolver_edns_udp_size(const ldns_resolver *r);
/**
 * Does the resolver use tcp or udp
 * \param[in] r the resolver
 * \return true: tcp, false: udp
 */
bool ldns_resolver_usevc(const ldns_resolver *r);
/**
 * Does the resolver only try the first nameserver
 * \param[in] r the resolver
 * \return true: yes, fail, false: no, try the others
 */
bool ldns_resolver_fail(const ldns_resolver *r);
/**
 * Does the resolver apply default domain name
 * \param[in] r the resolver
 * \return true: yes, false: no
 */
bool ldns_resolver_defnames(const ldns_resolver *r);
/**
 * Does the resolver apply search list
 * \param[in] r the resolver
 * \return true: yes, false: no
 */
bool ldns_resolver_dnsrch(const ldns_resolver *r);
/**
 * Does the resolver do DNSSEC
 * \param[in] r the resolver
 * \return true: yes, false: no
 */
bool ldns_resolver_dnssec(const ldns_resolver *r);
/**
 * Does the resolver set the CD bit
 * \param[in] r the resolver
 * \return true: yes, false: no
 */
bool ldns_resolver_dnssec_cd(const ldns_resolver *r);
/**
 * Get the resolver's DNSSEC anchors
 * \param[in] r the resolver
 * \return an rr_list containg trusted DNSSEC anchors
 */
ldns_rr_list * ldns_resolver_dnssec_anchors(const ldns_resolver *r);
/**
 * Does the resolver ignore the TC bit (truncated)
 * \param[in] r the resolver
 * \return true: yes, false: no
 */
bool ldns_resolver_igntc(const ldns_resolver *r);
/**
 * Does the resolver randomize the nameserver before usage
 * \param[in] r the resolver
 * \return true: yes, false: no
 */
bool ldns_resolver_random(const ldns_resolver *r);
/**
 * How many nameserver are configured in the resolver
 * \param[in] r the resolver
 * \return number of nameservers
 */
size_t ldns_resolver_nameserver_count(const ldns_resolver *r);
/**
 * What is the default dname to add to relative queries
 * \param[in] r the resolver
 * \return the dname which is added
 */
ldns_rdf *ldns_resolver_domain(const ldns_resolver *r);
/**
 * What is the timeout on socket connections
 * \param[in] r the resolver
 * \return the timeout as struct timeval
 */
struct timeval ldns_resolver_timeout(const ldns_resolver *r);
/**
 * What is the searchlist as used by the resolver
 * \param[in] r the resolver
 * \return a ldns_rdf pointer to a list of the addresses
 */
ldns_rdf** ldns_resolver_searchlist(const ldns_resolver *r);
/**
 * Return the configured nameserver ip address
 * \param[in] r the resolver
 * \return a ldns_rdf pointer to a list of the addresses
 */
ldns_rdf** ldns_resolver_nameservers(const ldns_resolver *r);
/**
 * Return the used round trip times for the nameservers
 * \param[in] r the resolver
 * \return a size_t* pointer to the list.
 * yet)
 */
size_t * ldns_resolver_rtt(const ldns_resolver *r);
/**
 * Return the used round trip time for a specific nameserver
 * \param[in] r the resolver
 * \param[in] pos the index to the nameserver
 * \return the rrt, 0: infinite, >0: undefined (as of * yet)
 */
size_t ldns_resolver_nameserver_rtt(const ldns_resolver *r, size_t pos);
/**
 * Return the tsig keyname as used by the nameserver
 * \param[in] r the resolver
 * \return the name used. Still owned by the resolver - change using
 * ldns_resolver_set_tsig_keyname().
 */
const char *ldns_resolver_tsig_keyname(const ldns_resolver *r);
/**
 * Return the tsig algorithm as used by the nameserver
 * \param[in] r the resolver
 * \return the algorithm used. Still owned by the resolver - change using
 * ldns_resolver_set_tsig_algorithm().
 */
const char *ldns_resolver_tsig_algorithm(const ldns_resolver *r);
/**
 * Return the tsig keydata as used by the nameserver
 * \param[in] r the resolver
 * \return the keydata used. Still owned by the resolver - change using
 * ldns_resolver_set_tsig_keydata().
 */
const char *ldns_resolver_tsig_keydata(const ldns_resolver *r);
/**
 * pop the last nameserver from the resolver.
 * \param[in] r the resolver
 * \return the popped address or NULL if empty
 */
ldns_rdf* ldns_resolver_pop_nameserver(ldns_resolver *r);

/**
 * Return the resolver's searchlist count
 * \param[in] r the resolver
 * \return the searchlist count
 */
size_t ldns_resolver_searchlist_count(const ldns_resolver *r);

/* write access function */
/**
 * Set the port the resolver should use
 * \param[in] r the resolver
 * \param[in] p the port number
 */
void ldns_resolver_set_port(ldns_resolver *r, uint16_t p);

/**
 * Set the source rdf (address) the resolver should use
 * \param[in] r the resolver
 * \param[in] s the source address
 */
void ldns_resolver_set_source(ldns_resolver *r, ldns_rdf *s);

/**
 * Set the resolver recursion
 * \param[in] r the resolver
 * \param[in] b true: set to recurse, false: unset
 */
void ldns_resolver_set_recursive(ldns_resolver *r, bool b);

/**
 * Set the resolver debugging
 * \param[in] r the resolver
 * \param[in] b true: debug on: false debug off
 */
void ldns_resolver_set_debug(ldns_resolver *r, bool b);

/**
 * Incremental the resolver's nameserver count.
 * \param[in] r the resolver
 */
void ldns_resolver_incr_nameserver_count(ldns_resolver *r);

/**
 * Decrement the resolver's nameserver count.
 * \param[in] r the resolver
 */
void ldns_resolver_dec_nameserver_count(ldns_resolver *r);

/**
 * Set the resolver's nameserver count directly.
 * \param[in] r the resolver
 * \param[in] c the nameserver count
 */
void ldns_resolver_set_nameserver_count(ldns_resolver *r, size_t c);

/**
 * Set the resolver's nameserver count directly by using an rdf list
 * \param[in] r the resolver
 * \param[in] rd the resolver addresses
 */
void ldns_resolver_set_nameservers(ldns_resolver *r, ldns_rdf **rd);

/**
 * Set the resolver's default domain. This gets appended when no
 * absolute name is given
 * \param[in] r the resolver
 * \param[in] rd the name to append
 */
void ldns_resolver_set_domain(ldns_resolver *r, ldns_rdf *rd);

/**
 * Set the resolver's socket time out when talking to remote hosts
 * \param[in] r the resolver
 * \param[in] timeout the timeout to use
 */
void ldns_resolver_set_timeout(ldns_resolver *r, struct timeval timeout);

/**
 * Push a new rd to the resolver's searchlist
 * \param[in] r the resolver
 * \param[in] rd to push
 */
void ldns_resolver_push_searchlist(ldns_resolver *r, ldns_rdf *rd);

/**
 * Whether the resolver uses the name set with _set_domain
 * \param[in] r the resolver
 * \param[in] b true: use the defaults, false: don't use them
 */
void ldns_resolver_set_defnames(ldns_resolver *r, bool b);

/**
 * Whether the resolver uses a virtual circuit (TCP)
 * \param[in] r the resolver
 * \param[in] b true: use TCP, false: don't use TCP
 */
void ldns_resolver_set_usevc(ldns_resolver *r, bool b);

/**
 * Whether the resolver uses the searchlist
 * \param[in] r the resolver
 * \param[in] b true: use the list, false: don't use the list
 */
void ldns_resolver_set_dnsrch(ldns_resolver *r, bool b);

/**
 * Whether the resolver uses DNSSEC
 * \param[in] r the resolver
 * \param[in] b true: use DNSSEC, false: don't use DNSSEC
 */
void ldns_resolver_set_dnssec(ldns_resolver *r, bool b);

/**
 * Whether the resolver uses the checking disable bit
 * \param[in] r the resolver
 * \param[in] b true: enable , false: don't use TCP
 */
void ldns_resolver_set_dnssec_cd(ldns_resolver *r, bool b);
/**
 * Set the resolver's DNSSEC anchor list directly. RRs should be of type DS or DNSKEY.
 * \param[in] r the resolver
 * \param[in] l the list of RRs to use as trust anchors
 */
void ldns_resolver_set_dnssec_anchors(ldns_resolver *r, ldns_rr_list * l);

/**
 * Push a new trust anchor to the resolver. It must be a DS or DNSKEY rr
 * \param[in] r the resolver.
 * \param[in] rr the RR to add as a trust anchor.
 * \return a status
 */
ldns_status ldns_resolver_push_dnssec_anchor(ldns_resolver *r, ldns_rr *rr);

/**
 * Set the resolver retrans timeout (in seconds)
 * \param[in] r the resolver
 * \param[in] re the retransmission interval in seconds
 */
void ldns_resolver_set_retrans(ldns_resolver *r, uint8_t re);

/**
 * Set whether the resolvers truncation fallback mechanism is used
 * when ldns_resolver_query() is called.
 * \param[in] r the resolver
 * \param[in] fallback whether to use the fallback mechanism
 */
void ldns_resolver_set_fallback(ldns_resolver *r, bool fallback);

/**
 * Set the number of times a resolver should retry a nameserver before the
 * next one is tried.
 * \param[in] r the resolver
 * \param[in] re the number of retries
 */
void ldns_resolver_set_retry(ldns_resolver *r, uint8_t re);

/**
 * Whether the resolver uses ip6
 * \param[in] r the resolver
 * \param[in] i 0: no pref, 1: ip4, 2: ip6
 */
void ldns_resolver_set_ip6(ldns_resolver *r, uint8_t i);

/**
 * Whether or not to fail after one failed query
 * \param[in] r the resolver
 * \param[in] b true: yes fail, false: continue with next nameserver
 */
void ldns_resolver_set_fail(ldns_resolver *r, bool b);

/**
 * Whether or not to ignore the TC bit
 * \param[in] r the resolver
 * \param[in] b true: yes ignore, false: don't ignore
 */
void ldns_resolver_set_igntc(ldns_resolver *r, bool b);

/**
 * Set maximum udp size
 * \param[in] r the resolver
 * \param[in] s the udp max size
 */
void ldns_resolver_set_edns_udp_size(ldns_resolver *r, uint16_t s);

/**
 * Set the tsig key name
 * \param[in] r the resolver
 * \param[in] tsig_keyname the tsig key name (copied into resolver)
 */
void ldns_resolver_set_tsig_keyname(ldns_resolver *r, const char *tsig_keyname);

/**
 * Set the tsig algorithm
 * \param[in] r the resolver
 * \param[in] tsig_algorithm the tsig algorithm (copied into resolver)
 */
void ldns_resolver_set_tsig_algorithm(ldns_resolver *r, const char *tsig_algorithm);

/**
 * Set the tsig key data
 * \param[in] r the resolver
 * \param[in] tsig_keydata the key data (copied into resolver)
 */
void ldns_resolver_set_tsig_keydata(ldns_resolver *r, const char *tsig_keydata);

/**
 * Set round trip time for all nameservers. Note this currently
 * differentiates between: unreachable and reachable.
 * \param[in] r the resolver
 * \param[in] rtt a list with the times
 */
void ldns_resolver_set_rtt(ldns_resolver *r, size_t *rtt);

/**
 * Set round trip time for a specific nameserver. Note this
 * currently differentiates between: unreachable and reachable.
 * \param[in] r the resolver
 * \param[in] pos the nameserver position
 * \param[in] value the rtt
 */
void ldns_resolver_set_nameserver_rtt(ldns_resolver *r, size_t pos, size_t value);

/**
 * Should the nameserver list be randomized before each use
 * \param[in] r the resolver
 * \param[in] b: true: randomize, false: don't
 */
void ldns_resolver_set_random(ldns_resolver *r, bool b);

/**
 * Push a new nameserver to the resolver. It must be an IP
 * address v4 or v6.
 * \param[in] r the resolver
 * \param[in] n the ip address
 * \return ldns_status a status
 */
ldns_status ldns_resolver_push_nameserver(ldns_resolver *r, const ldns_rdf *n);

/**
 * Push a new nameserver to the resolver. It must be an
 * A or AAAA RR record type
 * \param[in] r the resolver
 * \param[in] rr the resource record
 * \return ldns_status a status
 */
ldns_status ldns_resolver_push_nameserver_rr(ldns_resolver *r, const ldns_rr *rr);

/**
 * Push a new nameserver rr_list to the resolver.
 * \param[in] r the resolver
 * \param[in] rrlist the rr_list to push
 * \return ldns_status a status
 */
ldns_status ldns_resolver_push_nameserver_rr_list(ldns_resolver *r, const ldns_rr_list *rrlist);

/**
 * Send the query for using the resolver and take the search list into account
 * The search algorithm is as follows:
 * If the name is absolute, try it as-is, otherwise apply the search list
 * \param[in] *r operate using this resolver
 * \param[in] *rdf query for this name
 * \param[in] t query for this type (may be 0, defaults to A)
 * \param[in] c query for this class (may be 0, default to IN)
 * \param[in] flags the query flags
 *
 * \return ldns_pkt* a packet with the reply from the nameserver
 */
ldns_pkt* ldns_resolver_search(const ldns_resolver *r, const ldns_rdf *rdf, ldns_rr_type t, ldns_rr_class c, uint16_t flags);


/**
 * Send the query for using the resolver and take the search list into account
 * The search algorithm is as follows:
 * If the name is absolute, try it as-is, otherwise apply the search list
 * \param[out] pkt a packet with the reply from the nameserver
 * \param[in] *r operate using this resolver
 * \param[in] *rdf query for this name
 * \param[in] t query for this type (may be 0, defaults to A)
 * \param[in] c query for this class (may be 0, default to IN)
 * \param[in] flags the query flags
 *
 * \return ldns_status LDNS_STATUS_OK on success
 */
ldns_status ldns_resolver_search_status(ldns_pkt** pkt, ldns_resolver *r, const ldns_rdf *rdf, ldns_rr_type t, ldns_rr_class c, uint16_t flags);

/**
 * Form a query packet from a resolver and name/type/class combo
 * \param[out] **q a pointer to a ldns_pkt pointer (initialized by this function)
 * \param[in] *r operate using this resolver
 * \param[in] *name query for this name
 * \param[in] t query for this type (may be 0, defaults to A)
 * \param[in] c query for this class (may be 0, default to IN)
 * \param[in] f the query flags
 *
 * \return ldns_pkt* a packet with the reply from the nameserver
 */
ldns_status ldns_resolver_prepare_query_pkt(ldns_pkt **q, ldns_resolver *r, const  ldns_rdf *name, ldns_rr_type t, ldns_rr_class c, uint16_t f);

/**
 * Send the query for name as-is
 * \param[out] **answer a pointer to a ldns_pkt pointer (initialized by this function)
 * \param[in] *r operate using this resolver
 * \param[in] *name query for this name
 * \param[in] t query for this type (may be 0, defaults to A)
 * \param[in] c query for this class (may be 0, default to IN)
 * \param[in] flags the query flags
 *
 * \return ldns_status LDNS_STATUS_OK on success
 */
ldns_status ldns_resolver_send(ldns_pkt **answer, ldns_resolver *r, const ldns_rdf *name, ldns_rr_type t, ldns_rr_class c, uint16_t flags);

/**
 * Send the given packet to a nameserver
 * \param[out] **answer a pointer to a ldns_pkt pointer (initialized by this function)
 * \param[in] *r operate using this resolver
 * \param[in] *query_pkt query
 */
ldns_status ldns_resolver_send_pkt(ldns_pkt **answer, ldns_resolver *r, ldns_pkt *query_pkt);

/**
 * Send a query to a nameserver
 * \param[out] pkt a packet with the reply from the nameserver
 * \param[in] *r operate using this resolver
 * \param[in] *name query for this name
 * \param[in] *t query for this type (may be 0, defaults to A)
 * \param[in] *c query for this class (may be 0, default to IN)
 * \param[in] flags the query flags
 *
 * \return ldns_status LDNS_STATUS_OK on success
 * if _defnames is true the default domain will be added
 */
ldns_status ldns_resolver_query_status(ldns_pkt** pkt, ldns_resolver *r, const ldns_rdf *name, ldns_rr_type t, ldns_rr_class c, uint16_t flags);


/**
 * Send a query to a nameserver
 * \param[in] *r operate using this resolver 
 *               (despite the const in the declaration,
 *                the struct is altered as a side-effect)
 * \param[in] *name query for this name
 * \param[in] *t query for this type (may be 0, defaults to A)
 * \param[in] *c query for this class (may be 0, default to IN)
 * \param[in] flags the query flags
 *
 * \return ldns_pkt* a packet with the reply from the nameserver
 * if _defnames is true the default domain will be added
 */
ldns_pkt* ldns_resolver_query(const ldns_resolver *r, const ldns_rdf *name, ldns_rr_type t, ldns_rr_class c, uint16_t flags);


/**
 * Create a new resolver structure
 * \return ldns_resolver* pointer to new structure
 */
ldns_resolver* ldns_resolver_new(void);

/**
 * Clone a resolver
 * \param[in] r the resolver to clone
 * \return ldns_resolver* pointer to new structure
 */
ldns_resolver* ldns_resolver_clone(ldns_resolver *r);

/**
 * Create a resolver structure from a file like /etc/resolv.conf
 * \param[out] r the new resolver
 * \param[in] fp file pointer to create new resolver from
 *      if NULL use /etc/resolv.conf
 * \return LDNS_STATUS_OK or the error
 */
ldns_status ldns_resolver_new_frm_fp(ldns_resolver **r, FILE *fp);

/**
 * Create a resolver structure from a file like /etc/resolv.conf
 * \param[out] r the new resolver
 * \param[in] fp file pointer to create new resolver from
 *      if NULL use /etc/resolv.conf
 * \param[in] line_nr pointer to an integer containing the current line number (for debugging purposes)
 * \return LDNS_STATUS_OK or the error
 */
ldns_status ldns_resolver_new_frm_fp_l(ldns_resolver **r, FILE *fp, int *line_nr);

/**
 * Configure a resolver by means of a resolv.conf file
 * The file may be NULL in which case there will  be
 * looked the RESOLV_CONF (defaults to /etc/resolv.conf)
 * \param[out] r the new resolver
 * \param[in] filename the filename to use
 * \return LDNS_STATUS_OK or the error
 */
ldns_status ldns_resolver_new_frm_file(ldns_resolver **r, const char *filename);

/**
 * Frees the allocated space for this resolver. Only frees the resolver pionter! You should probably be using _deep_free.
 * \param res resolver to free
 */
void ldns_resolver_free(ldns_resolver *res);

/**
 * Frees the allocated space for this resolver and all it's data
 * \param res resolver to free
 */
void ldns_resolver_deep_free(ldns_resolver *res);

/**
 * Get the next stream of RRs in a AXFR
 * \param[in] resolver the resolver to use. First ldns_axfr_start() must be
 * called
 * \return ldns_rr the next RR from the AXFR stream
 * After you get this returned RR (not NULL: on error), then check if 
 * ldns_axfr_complete() is true to see if the zone transfer has completed.
 */
ldns_rr* ldns_axfr_next(ldns_resolver *resolver);

/**
 * Abort a transfer that is in progress
 * \param[in] resolver the resolver that is used
 */
void ldns_axfr_abort(ldns_resolver *resolver);

/**
 * Returns true if the axfr transfer has completed (i.e. 2 SOA RRs and no errors were encountered
 * \param[in] resolver the resolver that is used
 * \return bool true if axfr transfer was completed without error
 */
bool ldns_axfr_complete(const ldns_resolver *resolver);

/**
 * Returns a pointer to the last ldns_pkt that was sent by the server in the AXFR transfer
 * uasable for instance to get the error code on failure
 * \param[in] res the resolver that was used in the axfr transfer
 * \return ldns_pkt the last packet sent
 */
ldns_pkt *ldns_axfr_last_pkt(const ldns_resolver *res);

/**
 * Get the serial for requesting IXFR.
 * \param[in] r the resolver
 * \param[in] serial serial
 */
void ldns_resolver_set_ixfr_serial(ldns_resolver *r, uint32_t serial);

/**
 * Get the serial for requesting IXFR.
 * \param[in] res the resolver
 * \return uint32_t serial
 */
uint32_t ldns_resolver_get_ixfr_serial(const ldns_resolver *res);

/**
 * Randomize the nameserver list in the resolver
 * \param[in] r the resolver
 */
void ldns_resolver_nameservers_randomize(ldns_resolver *r);

/**
 * Returns true if at least one of the provided keys is a trust anchor
 * \param[in] r the current resolver
 * \param[in] keys the keyset to check
 * \param[out] trusted_keys the subset of trusted keys in the 'keys' rrset
 * \return true if at least one of the provided keys is a configured trust anchor
 */
bool ldns_resolver_trusted_key(const ldns_resolver *r, ldns_rr_list * keys, ldns_rr_list * trusted_keys);

#ifdef __cplusplus
}
#endif

#endif  /* LDNS_RESOLVER_H */
