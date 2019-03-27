/*
 * dns64/dns64.c - DNS64 module
 *
 * Copyright (c) 2009, Viagénie. All rights reserved.
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
 * Neither the name of Viagénie nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains a module that performs DNS64 query processing.
 */

#include "config.h"
#include "dns64/dns64.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "util/config_file.h"
#include "util/data/msgreply.h"
#include "util/fptr_wlist.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/storage/dnstree.h"
#include "util/data/dname.h"
#include "sldns/str2wire.h"

/******************************************************************************
 *                                                                            *
 *                             STATIC CONSTANTS                               *
 *                                                                            *
 ******************************************************************************/

/**
 * This is the default DNS64 prefix that is used whent he dns64 module is listed
 * in module-config but when the dns64-prefix variable is not present.
 */
static const char DEFAULT_DNS64_PREFIX[] = "64:ff9b::/96";

/**
 * Maximum length of a domain name in a PTR query in the .in-addr.arpa tree.
 */
#define MAX_PTR_QNAME_IPV4 30

/**
 * Per-query module-specific state. This is usually a dynamically-allocated
 * structure, but in our case we only need to store one variable describing the
 * state the query is in. So we repurpose the minfo pointer by storing an
 * integer in there.
 */
enum dns64_qstate {
    DNS64_INTERNAL_QUERY,    /**< Internally-generated query, no DNS64
                                  processing. */
    DNS64_NEW_QUERY,         /**< Query for which we're the first module in
                                  line. */
    DNS64_SUBQUERY_FINISHED  /**< Query for which we generated a sub-query, and
                                  for which this sub-query is finished. */
};


/******************************************************************************
 *                                                                            *
 *                                 STRUCTURES                                 *
 *                                                                            *
 ******************************************************************************/

/**
 * This structure contains module configuration information. One instance of
 * this structure exists per instance of the module. Normally there is only one
 * instance of the module.
 */
struct dns64_env {
    /**
     * DNS64 prefix address. We're using a full sockaddr instead of just an
     * in6_addr because we can reuse Unbound's generic string parsing functions.
     * It will always contain a sockaddr_in6, and only the sin6_addr member will
     * ever be used.
     */
    struct sockaddr_storage prefix_addr;

    /**
     * This is always sizeof(sockaddr_in6).
     */
    socklen_t prefix_addrlen;

    /**
     * This is the CIDR length of the prefix. It needs to be between 0 and 96.
     */
    int prefix_net;

    /**
     * Tree of names for which AAAA is ignored. always synthesize from A.
     */
    rbtree_type ignore_aaaa;
};


/******************************************************************************
 *                                                                            *
 *                             UTILITY FUNCTIONS                              *
 *                                                                            *
 ******************************************************************************/

/**
 * Generic macro for swapping two variables.
 *
 * \param t Type of the variables. (e.g. int)
 * \param a First variable.
 * \param b Second variable.
 *
 * \warning Do not attempt something foolish such as swap(int,a++,b++)!
 */
#define swap(t,a,b) do {t x = a; a = b; b = x;} while(0)

/**
 * Reverses a string.
 *
 * \param begin Points to the first character of the string.
 * \param end   Points one past the last character of the string.
 */
static void
reverse(char* begin, char* end)
{
    while ( begin < --end ) {
        swap(char, *begin, *end);
        ++begin;
    }
}

/**
 * Convert an unsigned integer to a string. The point of this function is that
 * of being faster than sprintf().
 *
 * \param n The number to be converted.
 * \param s The result will be written here. Must be large enough, be careful!
 *
 * \return The number of characters written.
 */
static int
uitoa(unsigned n, char* s)
{
    char* ss = s;
    do {
        *ss++ = '0' + n % 10;
    } while (n /= 10);
    reverse(s, ss);
    return ss - s;
}

/**
 * Extract an IPv4 address embedded in the IPv6 address \a ipv6 at offset \a
 * offset (in bits). Note that bits are not necessarily aligned on bytes so we
 * need to be careful.
 *
 * \param ipv6   IPv6 address represented as a 128-bit array in big-endian
 *               order.
 * \param offset Index of the MSB of the IPv4 address embedded in the IPv6
 *               address.
 */
static uint32_t
extract_ipv4(const uint8_t ipv6[16], const int offset)
{
    uint32_t ipv4 = (uint32_t)ipv6[offset/8+0] << (24 + (offset%8))
                  | (uint32_t)ipv6[offset/8+1] << (16 + (offset%8))
                  | (uint32_t)ipv6[offset/8+2] << ( 8 + (offset%8))
                  | (uint32_t)ipv6[offset/8+3] << ( 0 + (offset%8));
    if (offset/8+4 < 16)
        ipv4 |= (uint32_t)ipv6[offset/8+4] >> (8 - offset%8);
    return ipv4;
}

/**
 * Builds the PTR query name corresponding to an IPv4 address. For example,
 * given the number 3,464,175,361, this will build the string
 * "\03206\03123\0231\011\07in-addr\04arpa".
 *
 * \param ipv4 IPv4 address represented as an unsigned 32-bit number.
 * \param ptr  The result will be written here. Must be large enough, be
 *             careful!
 *
 * \return The number of characters written.
 */
static size_t
ipv4_to_ptr(uint32_t ipv4, char ptr[MAX_PTR_QNAME_IPV4])
{
    static const char IPV4_PTR_SUFFIX[] = "\07in-addr\04arpa";
    int i;
    char* c = ptr;

    for (i = 0; i < 4; ++i) {
        *c = uitoa((unsigned int)(ipv4 % 256), c + 1);
        c += *c + 1;
        ipv4 /= 256;
    }

    memmove(c, IPV4_PTR_SUFFIX, sizeof(IPV4_PTR_SUFFIX));

    return c + sizeof(IPV4_PTR_SUFFIX) - ptr;
}

/**
 * Converts an IPv6-related domain name string from a PTR query into an IPv6
 * address represented as a 128-bit array.
 *
 * \param ptr  The domain name. (e.g. "\011[...]\010\012\016\012\03ip6\04arpa")
 * \param ipv6 The result will be written here, in network byte order.
 *
 * \return 1 on success, 0 on failure.
 */
static int
ptr_to_ipv6(const char* ptr, uint8_t ipv6[16])
{
    int i;

    for (i = 0; i < 64; i++) {
        int x;

        if (ptr[i++] != 1)
            return 0;

        if (ptr[i] >= '0' && ptr[i] <= '9') {
            x = ptr[i] - '0';
        } else if (ptr[i] >= 'a' && ptr[i] <= 'f') {
            x = ptr[i] - 'a' + 10;
        } else if (ptr[i] >= 'A' && ptr[i] <= 'F') {
            x = ptr[i] - 'A' + 10;
        } else {
            return 0;
        }

        ipv6[15-i/4] |= x << (2 * ((i-1) % 4));
    }

    return 1;
}

/**
 * Synthesize an IPv6 address based on an IPv4 address and the DNS64 prefix.
 *
 * \param prefix_addr DNS64 prefix address.
 * \param prefix_net  CIDR length of the DNS64 prefix. Must be between 0 and 96.
 * \param a           IPv4 address.
 * \param aaaa        IPv6 address. The result will be written here.
 */
static void
synthesize_aaaa(const uint8_t prefix_addr[16], int prefix_net,
        const uint8_t a[4], uint8_t aaaa[16])
{
    memcpy(aaaa, prefix_addr, 16);
    aaaa[prefix_net/8+0] |= a[0] >> (0+prefix_net%8);
    aaaa[prefix_net/8+1] |= a[0] << (8-prefix_net%8);
    aaaa[prefix_net/8+1] |= a[1] >> (0+prefix_net%8);
    aaaa[prefix_net/8+2] |= a[1] << (8-prefix_net%8);
    aaaa[prefix_net/8+2] |= a[2] >> (0+prefix_net%8);
    aaaa[prefix_net/8+3] |= a[2] << (8-prefix_net%8);
    aaaa[prefix_net/8+3] |= a[3] >> (0+prefix_net%8);
    if (prefix_net/8+4 < 16)  /* <-- my beautiful symmetry is destroyed! */
    aaaa[prefix_net/8+4] |= a[3] << (8-prefix_net%8);
}


/******************************************************************************
 *                                                                            *
 *                           DNS64 MODULE FUNCTIONS                           *
 *                                                                            *
 ******************************************************************************/

/**
 * insert ignore_aaaa element into the tree
 * @param dns64_env: module env.
 * @param str: string with domain name.
 * @return false on failure.
 */
static int
dns64_insert_ignore_aaaa(struct dns64_env* dns64_env, char* str)
{
	/* parse and insert element */
	struct name_tree_node* node;
	node = (struct name_tree_node*)calloc(1, sizeof(*node));
	if(!node) {
		log_err("out of memory");
		return 0;
	}
	node->name = sldns_str2wire_dname(str, &node->len);
	if(!node->name) {
		free(node);
		log_err("cannot parse dns64-ignore-aaaa: %s", str);
		return 0;
	}
	node->labs = dname_count_labels(node->name);
	node->dclass = LDNS_RR_CLASS_IN;
	if(!name_tree_insert(&dns64_env->ignore_aaaa, node,
		node->name, node->len, node->labs, node->dclass)) {
		/* ignore duplicate element */
		free(node->name);
		free(node);
		return 1;
	}
	return 1;
}

/**
 * This function applies the configuration found in the parsed configuration
 * file \a cfg to this instance of the dns64 module. Currently only the DNS64
 * prefix (a.k.a. Pref64) is configurable.
 *
 * \param dns64_env Module-specific global parameters.
 * \param cfg       Parsed configuration file.
 */
static int
dns64_apply_cfg(struct dns64_env* dns64_env, struct config_file* cfg)
{
    struct config_strlist* s;
    verbose(VERB_ALGO, "dns64-prefix: %s", cfg->dns64_prefix);
    if (!netblockstrtoaddr(cfg->dns64_prefix ? cfg->dns64_prefix :
                DEFAULT_DNS64_PREFIX, 0, &dns64_env->prefix_addr,
                &dns64_env->prefix_addrlen, &dns64_env->prefix_net)) {
        log_err("cannot parse dns64-prefix netblock: %s", cfg->dns64_prefix);
        return 0;
    }
    if (!addr_is_ip6(&dns64_env->prefix_addr, dns64_env->prefix_addrlen)) {
        log_err("dns64_prefix is not IPv6: %s", cfg->dns64_prefix);
        return 0;
    }
    if (dns64_env->prefix_net < 0 || dns64_env->prefix_net > 96) {
        log_err("dns64-prefix length it not between 0 and 96: %s",
                cfg->dns64_prefix);
        return 0;
    }
    for(s = cfg->dns64_ignore_aaaa; s; s = s->next) {
	    if(!dns64_insert_ignore_aaaa(dns64_env, s->str))
		    return 0;
    }
    name_tree_init_parents(&dns64_env->ignore_aaaa);
    return 1;
}

/**
 * Initializes this instance of the dns64 module.
 *
 * \param env Global state of all module instances.
 * \param id  This instance's ID number.
 */
int
dns64_init(struct module_env* env, int id)
{
    struct dns64_env* dns64_env =
        (struct dns64_env*)calloc(1, sizeof(struct dns64_env));
    if (!dns64_env) {
        log_err("malloc failure");
        return 0;
    }
    env->modinfo[id] = (void*)dns64_env;
    name_tree_init(&dns64_env->ignore_aaaa);
    if (!dns64_apply_cfg(dns64_env, env->cfg)) {
        log_err("dns64: could not apply configuration settings.");
        return 0;
    }
    return 1;
}

/** free ignore AAAA elements */
static void
free_ignore_aaaa_node(rbnode_type* node, void* ATTR_UNUSED(arg))
{
	struct name_tree_node* n = (struct name_tree_node*)node;
	if(!n) return;
	free(n->name);
	free(n);
}

/**
 * Deinitializes this instance of the dns64 module.
 *
 * \param env Global state of all module instances.
 * \param id  This instance's ID number.
 */
void
dns64_deinit(struct module_env* env, int id)
{
    struct dns64_env* dns64_env;
    if (!env)
        return;
    dns64_env = (struct dns64_env*)env->modinfo[id];
    if(dns64_env) {
	    traverse_postorder(&dns64_env->ignore_aaaa, free_ignore_aaaa_node,
	    	NULL);
    }
    free(env->modinfo[id]);
    env->modinfo[id] = NULL;
}

/**
 * Handle PTR queries for IPv6 addresses. If the address belongs to the DNS64
 * prefix, we must do a PTR query for the corresponding IPv4 address instead.
 *
 * \param qstate Query state structure.
 * \param id     This module instance's ID number.
 *
 * \return The new state of the query.
 */
static enum module_ext_state
handle_ipv6_ptr(struct module_qstate* qstate, int id)
{
    struct dns64_env* dns64_env = (struct dns64_env*)qstate->env->modinfo[id];
    struct module_qstate* subq = NULL;
    struct query_info qinfo;
    struct sockaddr_in6 sin6;

    /* Convert the PTR query string to an IPv6 address. */
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    if (!ptr_to_ipv6((char*)qstate->qinfo.qname, sin6.sin6_addr.s6_addr))
        return module_wait_module;  /* Let other module handle this. */

    /*
     * If this IPv6 address is not part of our DNS64 prefix, then we don't need
     * to do anything. Let another module handle the query.
     */
    if (addr_in_common((struct sockaddr_storage*)&sin6, 128,
                &dns64_env->prefix_addr, dns64_env->prefix_net,
                (socklen_t)sizeof(sin6)) != dns64_env->prefix_net)
        return module_wait_module;

    verbose(VERB_ALGO, "dns64: rewrite PTR record");

    /*
     * Create a new PTR query info for the domain name corresponding to the IPv4
     * address corresponding to the IPv6 address corresponding to the original
     * PTR query domain name.
     */
    qinfo = qstate->qinfo;
    if (!(qinfo.qname = regional_alloc(qstate->region, MAX_PTR_QNAME_IPV4)))
        return module_error;
    qinfo.qname_len = ipv4_to_ptr(extract_ipv4(sin6.sin6_addr.s6_addr,
                dns64_env->prefix_net), (char*)qinfo.qname);

    /* Create the new sub-query. */
    fptr_ok(fptr_whitelist_modenv_attach_sub(qstate->env->attach_sub));
    if(!(*qstate->env->attach_sub)(qstate, &qinfo, qstate->query_flags, 0, 0,
                &subq))
        return module_error;
    if (subq) {
        subq->curmod = id;
        subq->ext_state[id] = module_state_initial;
        subq->minfo[id] = NULL;
    }

    return module_wait_subquery;
}

static enum module_ext_state
generate_type_A_query(struct module_qstate* qstate, int id)
{
	struct module_qstate* subq = NULL;
	struct query_info qinfo;

	verbose(VERB_ALGO, "dns64: query A record");

	/* Create a new query info. */
	qinfo = qstate->qinfo;
	qinfo.qtype = LDNS_RR_TYPE_A;

	/* Start the sub-query. */
	fptr_ok(fptr_whitelist_modenv_attach_sub(qstate->env->attach_sub));
	if(!(*qstate->env->attach_sub)(qstate, &qinfo, qstate->query_flags, 0,
				       0, &subq))
	{
		verbose(VERB_ALGO, "dns64: sub-query creation failed");
		return module_error;
	}
	if (subq) {
		subq->curmod = id;
		subq->ext_state[id] = module_state_initial;
		subq->minfo[id] = NULL;
	}

	return module_wait_subquery;
}

/**
 * See if query name is in the always synth config.
 * The ignore-aaaa list has names for which the AAAA for the domain is
 * ignored and the A is always used to create the answer.
 * @param qstate: query state.
 * @param id: module id.
 * @return true if the name is covered by ignore-aaaa.
 */
static int
dns64_always_synth_for_qname(struct module_qstate* qstate, int id)
{
	struct dns64_env* dns64_env = (struct dns64_env*)qstate->env->modinfo[id];
	int labs = dname_count_labels(qstate->qinfo.qname);
	struct name_tree_node* node = name_tree_lookup(&dns64_env->ignore_aaaa,
		qstate->qinfo.qname, qstate->qinfo.qname_len, labs,
		qstate->qinfo.qclass);
	return (node != NULL);
}

/**
 * Handles the "pass" event for a query. This event is received when a new query
 * is received by this module. The query may have been generated internally by
 * another module, in which case we don't want to do any special processing
 * (this is an interesting discussion topic),  or it may be brand new, e.g.
 * received over a socket, in which case we do want to apply DNS64 processing.
 *
 * \param qstate A structure representing the state of the query that has just
 *               received the "pass" event.
 * \param id     This module's instance ID.
 *
 * \return The new state of the query.
 */
static enum module_ext_state
handle_event_pass(struct module_qstate* qstate, int id)
{
	if ((uintptr_t)qstate->minfo[id] == DNS64_NEW_QUERY
            && qstate->qinfo.qtype == LDNS_RR_TYPE_PTR
            && qstate->qinfo.qname_len == 74
            && !strcmp((char*)&qstate->qinfo.qname[64], "\03ip6\04arpa"))
        /* Handle PTR queries for IPv6 addresses. */
        return handle_ipv6_ptr(qstate, id);

	if (qstate->env->cfg->dns64_synthall &&
	    (uintptr_t)qstate->minfo[id] == DNS64_NEW_QUERY
	    && qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA)
		return generate_type_A_query(qstate, id);

	if(dns64_always_synth_for_qname(qstate, id) &&
	    (uintptr_t)qstate->minfo[id] == DNS64_NEW_QUERY
	    && !(qstate->query_flags & BIT_CD)
	    && qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA) {
		verbose(VERB_ALGO, "dns64: ignore-aaaa and synthesize anyway");
		return generate_type_A_query(qstate, id);
	}

	/* We are finished when our sub-query is finished. */
	if ((uintptr_t)qstate->minfo[id] == DNS64_SUBQUERY_FINISHED)
		return module_finished;

	/* Otherwise, pass request to next module. */
	verbose(VERB_ALGO, "dns64: pass to next module");
	return module_wait_module;
}

/**
 * Handles the "done" event for a query. We need to analyze the response and
 * maybe issue a new sub-query for the A record.
 *
 * \param qstate A structure representing the state of the query that has just
 *               received the "pass" event.
 * \param id     This module's instance ID.
 *
 * \return The new state of the query.
 */
static enum module_ext_state
handle_event_moddone(struct module_qstate* qstate, int id)
{
    /*
     * In many cases we have nothing special to do. From most to least common:
     *
     *   - An internal query.
     *   - A query for a record type other than AAAA.
     *   - CD FLAG was set on querier
     *   - An AAAA query for which an error was returned.(qstate.return_rcode)
     *     -> treated as servfail thus synthesize (sec 5.1.3 6147), thus
     *        synthesize in (sec 5.1.2 of RFC6147).
     *   - A successful AAAA query with an answer.
     */
	if((enum dns64_qstate)qstate->minfo[id] != DNS64_INTERNAL_QUERY
            && qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA
	    && !(qstate->query_flags & BIT_CD)
	    && !(qstate->return_msg &&
		    qstate->return_msg->rep &&
		    reply_find_answer_rrset(&qstate->qinfo,
			    qstate->return_msg->rep)))
		/* not internal, type AAAA, not CD, and no answer RRset,
		 * So, this is a AAAA noerror/nodata answer */
		return generate_type_A_query(qstate, id);

	if((enum dns64_qstate)qstate->minfo[id] != DNS64_INTERNAL_QUERY
	    && qstate->qinfo.qtype == LDNS_RR_TYPE_AAAA
	    && !(qstate->query_flags & BIT_CD)
	    && dns64_always_synth_for_qname(qstate, id)) {
		/* if it is not internal, AAAA, not CD and listed domain,
		 * generate from A record and ignore AAAA */
		verbose(VERB_ALGO, "dns64: ignore-aaaa and synthesize anyway");
		return generate_type_A_query(qstate, id);
	}

	/* do nothing */
	return module_finished;
}

/**
 * This is the module's main() function. It gets called each time a query
 * receives an event which we may need to handle. We respond by updating the
 * state of the query.
 *
 * \param qstate   Structure containing the state of the query.
 * \param event    Event that has just been received.
 * \param id       This module's instance ID.
 * \param outbound State of a DNS query on an authoritative server. We never do
 *                 our own queries ourselves (other modules do it for us), so
 *                 this is unused.
 */
void
dns64_operate(struct module_qstate* qstate, enum module_ev event, int id,
		struct outbound_entry* outbound)
{
	(void)outbound;
	verbose(VERB_QUERY, "dns64[module %d] operate: extstate:%s event:%s",
			id, strextstate(qstate->ext_state[id]),
			strmodulevent(event));
	log_query_info(VERB_QUERY, "dns64 operate: query", &qstate->qinfo);

	switch(event) {
		case module_event_new:
			/* Tag this query as being new and fall through. */
			qstate->minfo[id] = (void*)DNS64_NEW_QUERY;
  			/* fallthrough */
		case module_event_pass:
			qstate->ext_state[id] = handle_event_pass(qstate, id);
			break;
		case module_event_moddone:
			qstate->ext_state[id] = handle_event_moddone(qstate, id);
			break;
		default:
			qstate->ext_state[id] = module_finished;
			break;
	}
}

static void
dns64_synth_aaaa_data(const struct ub_packed_rrset_key* fk, 
		      const struct packed_rrset_data* fd, 
		      struct ub_packed_rrset_key *dk, 
		      struct packed_rrset_data **dd_out, struct regional *region, 
		      struct dns64_env* dns64_env )
{
	struct packed_rrset_data *dd;
	size_t i;
	/*
	 * Create synthesized AAAA RR set data. We need to allocated extra memory
	 * for the RRs themselves. Each RR has a length, TTL, pointer to wireformat
	 * data, 2 bytes of data length, and 16 bytes of IPv6 address.
	 */
	if(fd->count > RR_COUNT_MAX) {
		*dd_out = NULL;
		return; /* integer overflow protection in alloc */
	}
	if (!(dd = *dd_out = regional_alloc(region,
		  sizeof(struct packed_rrset_data)
		  + fd->count * (sizeof(size_t) + sizeof(time_t) +
			     sizeof(uint8_t*) + 2 + 16)))) {
		log_err("out of memory");
		return;
	}

	/* Copy attributes from A RR set. */
	dd->ttl = fd->ttl;
	dd->count = fd->count;
	dd->rrsig_count = 0;
	dd->trust = fd->trust;
	dd->security = fd->security;

	/*
	 * Synthesize AAAA records. Adjust pointers in structure.
	 */
	dd->rr_len =
	    (size_t*)((uint8_t*)dd + sizeof(struct packed_rrset_data));
	dd->rr_data = (uint8_t**)&dd->rr_len[dd->count];
	dd->rr_ttl = (time_t*)&dd->rr_data[dd->count];
	for(i = 0; i < fd->count; ++i) {
		if (fd->rr_len[i] != 6 || fd->rr_data[i][0] != 0
		    || fd->rr_data[i][1] != 4) {
			*dd_out = NULL;
			return;
		}
		dd->rr_len[i] = 18;
		dd->rr_data[i] =
		    (uint8_t*)&dd->rr_ttl[dd->count] + 18*i;
		dd->rr_data[i][0] = 0;
		dd->rr_data[i][1] = 16;
		synthesize_aaaa(
				((struct sockaddr_in6*)&dns64_env->prefix_addr)->sin6_addr.s6_addr,
				dns64_env->prefix_net, &fd->rr_data[i][2],
				&dd->rr_data[i][2] );
		dd->rr_ttl[i] = fd->rr_ttl[i];
	}

	/*
	 * Create synthesized AAAA RR set key. This is mostly just bookkeeping,
	 * nothing interesting here.
	 */
	if(!dk) {
		log_err("no key");
		*dd_out = NULL;
		return;
	}

	dk->rk.dname = (uint8_t*)regional_alloc_init(region,
		     fk->rk.dname, fk->rk.dname_len);

	if(!dk->rk.dname) {
		log_err("out of memory");
		*dd_out = NULL;
		return;
	}

	dk->rk.type = htons(LDNS_RR_TYPE_AAAA);
	memset(&dk->entry, 0, sizeof(dk->entry));
	dk->entry.key = dk;
	dk->entry.hash = rrset_key_hash(&dk->rk);
	dk->entry.data = dd;

}

/**
 * Synthesize an AAAA RR set from an A sub-query's answer and add it to the
 * original empty response.
 *
 * \param id     This module's instance ID.
 * \param super  Original AAAA query.
 * \param qstate A query.
 */
static void
dns64_adjust_a(int id, struct module_qstate* super, struct module_qstate* qstate)
{
	struct dns64_env* dns64_env = (struct dns64_env*)super->env->modinfo[id];
	struct reply_info *rep, *cp;
	size_t i, s;
	struct packed_rrset_data* fd, *dd;
	struct ub_packed_rrset_key* fk, *dk;

	verbose(VERB_ALGO, "converting A answers to AAAA answers");

	log_assert(super->region);
	log_assert(qstate->return_msg);
	log_assert(qstate->return_msg->rep);

	/* If dns64-synthall is enabled, return_msg is not initialized */
	if(!super->return_msg) {
		super->return_msg = (struct dns_msg*)regional_alloc(
		    super->region, sizeof(struct dns_msg));
		if(!super->return_msg)
			return;
		memset(super->return_msg, 0, sizeof(*super->return_msg));
		super->return_msg->qinfo = super->qinfo;
	}

	rep = qstate->return_msg->rep;

	/*
	 * Build the actual reply.
	 */
	cp = construct_reply_info_base(super->region, rep->flags, rep->qdcount,
		rep->ttl, rep->prefetch_ttl, rep->serve_expired_ttl,
		rep->an_numrrsets, rep->ns_numrrsets, rep->ar_numrrsets,
		rep->rrset_count, rep->security);
	if(!cp)
		return;

	/* allocate ub_key structures special or not */
	if(!reply_info_alloc_rrset_keys(cp, NULL, super->region)) {
		return;
	}

	/* copy everything and replace A by AAAA */
	for(i=0; i<cp->rrset_count; i++) {
		fk = rep->rrsets[i];
		dk = cp->rrsets[i];
		fd = (struct packed_rrset_data*)fk->entry.data;
		dk->rk = fk->rk;
		dk->id = fk->id;

		if(i<rep->an_numrrsets && fk->rk.type == htons(LDNS_RR_TYPE_A)) {
			/* also sets dk->entry.hash */
			dns64_synth_aaaa_data(fk, fd, dk, &dd, super->region, dns64_env);
			if(!dd)
				return;
			/* Delete negative AAAA record from cache stored by
			 * the iterator module */
			rrset_cache_remove(super->env->rrset_cache, dk->rk.dname, 
					   dk->rk.dname_len, LDNS_RR_TYPE_AAAA, 
					   LDNS_RR_CLASS_IN, 0);
			/* Delete negative AAAA in msg cache for CNAMEs,
			 * stored by the iterator module */
			if(i != 0) /* if not the first RR */
			    msg_cache_remove(super->env, dk->rk.dname,
				dk->rk.dname_len, LDNS_RR_TYPE_AAAA,
				LDNS_RR_CLASS_IN, 0);
		} else {
			dk->entry.hash = fk->entry.hash;
			dk->rk.dname = (uint8_t*)regional_alloc_init(super->region,
				fk->rk.dname, fk->rk.dname_len);

			if(!dk->rk.dname)
				return;

			s = packed_rrset_sizeof(fd);
			dd = (struct packed_rrset_data*)regional_alloc_init(
				super->region, fd, s);

			if(!dd)
				return;
		}

		packed_rrset_ptr_fixup(dd);
		dk->entry.data = (void*)dd;
	}

	/* Commit changes. */
	super->return_msg->rep = cp;
}

/**
 * Generate a response for the original IPv6 PTR query based on an IPv4 PTR
 * sub-query's response.
 *
 * \param qstate IPv4 PTR sub-query.
 * \param super  Original IPv6 PTR query.
 */
static void
dns64_adjust_ptr(struct module_qstate* qstate, struct module_qstate* super)
{
    struct ub_packed_rrset_key* answer;

    verbose(VERB_ALGO, "adjusting PTR reply");

    /* Copy the sub-query's reply to the parent. */
    if (!(super->return_msg = (struct dns_msg*)regional_alloc(super->region,
                    sizeof(struct dns_msg))))
        return;
    super->return_msg->qinfo = super->qinfo;
    super->return_msg->rep = reply_info_copy(qstate->return_msg->rep, NULL,
            super->region);

    /*
     * Adjust the domain name of the answer RR set so that it matches the
     * initial query's domain name.
     */
    answer = reply_find_answer_rrset(&qstate->qinfo, super->return_msg->rep);
    log_assert(answer);
    answer->rk.dname = super->qinfo.qname;
    answer->rk.dname_len = super->qinfo.qname_len;
}

/**
 * This function is called when a sub-query finishes to inform the parent query.
 *
 * We issue two kinds of sub-queries: PTR and A.
 *
 * \param qstate State of the sub-query.
 * \param id     This module's instance ID.
 * \param super  State of the super-query.
 */
void
dns64_inform_super(struct module_qstate* qstate, int id,
		struct module_qstate* super)
{
	log_query_info(VERB_ALGO, "dns64: inform_super, sub is",
		       &qstate->qinfo);
	log_query_info(VERB_ALGO, "super is", &super->qinfo);

	/*
	 * Signal that the sub-query is finished, no matter whether we are
	 * successful or not. This lets the state machine terminate.
	 */
	super->minfo[id] = (void*)DNS64_SUBQUERY_FINISHED;

	/* If there is no successful answer, we're done. */
	if (qstate->return_rcode != LDNS_RCODE_NOERROR
	    || !qstate->return_msg
	    || !qstate->return_msg->rep
	    || !reply_find_answer_rrset(&qstate->qinfo,
					qstate->return_msg->rep))
		return;

	/* Use return code from A query in response to client. */
	if (super->return_rcode != LDNS_RCODE_NOERROR)
		super->return_rcode = qstate->return_rcode;

	/* Generate a response suitable for the original query. */
	if (qstate->qinfo.qtype == LDNS_RR_TYPE_A) {
		dns64_adjust_a(id, super, qstate);
	} else {
		log_assert(qstate->qinfo.qtype == LDNS_RR_TYPE_PTR);
		dns64_adjust_ptr(qstate, super);
	}

	/* Store the generated response in cache. */
	if (!super->no_cache_store &&
		!dns_cache_store(super->env, &super->qinfo, super->return_msg->rep,
		0, 0, 0, NULL, super->query_flags))
		log_err("out of memory");
}

/**
 * Clear module-specific data from query state. Since we do not allocate memory,
 * it's just a matter of setting a pointer to NULL.
 *
 * \param qstate Query state.
 * \param id     This module's instance ID.
 */
void
dns64_clear(struct module_qstate* qstate, int id)
{
    qstate->minfo[id] = NULL;
}

/**
 * Returns the amount of global memory that this module uses, not including
 * per-query data.
 *
 * \param env Module environment.
 * \param id  This module's instance ID.
 */
size_t
dns64_get_mem(struct module_env* env, int id)
{
    struct dns64_env* dns64_env = (struct dns64_env*)env->modinfo[id];
    if (!dns64_env)
        return 0;
    return sizeof(*dns64_env);
}

/**
 * The dns64 function block.
 */
static struct module_func_block dns64_block = {
	"dns64",
	&dns64_init, &dns64_deinit, &dns64_operate, &dns64_inform_super,
	&dns64_clear, &dns64_get_mem
};

/**
 * Function for returning the above function block.
 */
struct module_func_block *
dns64_get_funcblock(void)
{
	return &dns64_block;
}
