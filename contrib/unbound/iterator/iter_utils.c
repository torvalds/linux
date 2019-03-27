/*
 * iterator/iter_utils.c - iterative resolver module utility functions.
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
 * This file contains functions to assist the iterator module.
 * Configuration options. Forward zones. 
 */
#include "config.h"
#include "iterator/iter_utils.h"
#include "iterator/iterator.h"
#include "iterator/iter_hints.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_donotq.h"
#include "iterator/iter_delegpt.h"
#include "iterator/iter_priv.h"
#include "services/cache/infra.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "util/net_help.h"
#include "util/module.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/regional.h"
#include "util/data/msgparse.h"
#include "util/data/dname.h"
#include "util/random.h"
#include "util/fptr_wlist.h"
#include "validator/val_anchor.h"
#include "validator/val_kcache.h"
#include "validator/val_kentry.h"
#include "validator/val_utils.h"
#include "validator/val_sigcrypt.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"

/** time when nameserver glue is said to be 'recent' */
#define SUSPICION_RECENT_EXPIRY 86400
/** penalty to validation failed blacklisted IPs */
#define BLACKLIST_PENALTY (USEFUL_SERVER_TOP_TIMEOUT*4)

/** fillup fetch policy array */
static void
fetch_fill(struct iter_env* ie, const char* str)
{
	char* s = (char*)str, *e;
	int i;
	for(i=0; i<ie->max_dependency_depth+1; i++) {
		ie->target_fetch_policy[i] = strtol(s, &e, 10);
		if(s == e)
			fatal_exit("cannot parse fetch policy number %s", s);
		s = e;
	}
}

/** Read config string that represents the target fetch policy */
static int
read_fetch_policy(struct iter_env* ie, const char* str)
{
	int count = cfg_count_numbers(str);
	if(count < 1) {
		log_err("Cannot parse target fetch policy: \"%s\"", str);
		return 0;
	}
	ie->max_dependency_depth = count - 1;
	ie->target_fetch_policy = (int*)calloc(
		(size_t)ie->max_dependency_depth+1, sizeof(int));
	if(!ie->target_fetch_policy) {
		log_err("alloc fetch policy: out of memory");
		return 0;
	}
	fetch_fill(ie, str);
	return 1;
}

/** apply config caps whitelist items to name tree */
static int
caps_white_apply_cfg(rbtree_type* ntree, struct config_file* cfg)
{
	struct config_strlist* p;
	for(p=cfg->caps_whitelist; p; p=p->next) {
		struct name_tree_node* n;
		size_t len;
		uint8_t* nm = sldns_str2wire_dname(p->str, &len);
		if(!nm) {
			log_err("could not parse %s", p->str);
			return 0;
		}
		n = (struct name_tree_node*)calloc(1, sizeof(*n));
		if(!n) {
			log_err("out of memory");
			free(nm);
			return 0;
		}
		n->node.key = n;
		n->name = nm;
		n->len = len;
		n->labs = dname_count_labels(nm);
		n->dclass = LDNS_RR_CLASS_IN;
		if(!name_tree_insert(ntree, n, nm, len, n->labs, n->dclass)) {
			/* duplicate element ignored, idempotent */
			free(n->name);
			free(n);
		}
	}
	name_tree_init_parents(ntree);
	return 1;
}

int 
iter_apply_cfg(struct iter_env* iter_env, struct config_file* cfg)
{
	int i;
	/* target fetch policy */
	if(!read_fetch_policy(iter_env, cfg->target_fetch_policy))
		return 0;
	for(i=0; i<iter_env->max_dependency_depth+1; i++)
		verbose(VERB_QUERY, "target fetch policy for level %d is %d",
			i, iter_env->target_fetch_policy[i]);
	
	if(!iter_env->donotq)
		iter_env->donotq = donotq_create();
	if(!iter_env->donotq || !donotq_apply_cfg(iter_env->donotq, cfg)) {
		log_err("Could not set donotqueryaddresses");
		return 0;
	}
	if(!iter_env->priv)
		iter_env->priv = priv_create();
	if(!iter_env->priv || !priv_apply_cfg(iter_env->priv, cfg)) {
		log_err("Could not set private addresses");
		return 0;
	}
	if(cfg->caps_whitelist) {
		if(!iter_env->caps_white)
			iter_env->caps_white = rbtree_create(name_tree_compare);
		if(!iter_env->caps_white || !caps_white_apply_cfg(
			iter_env->caps_white, cfg)) {
			log_err("Could not set capsforid whitelist");
			return 0;
		}

	}
	iter_env->supports_ipv6 = cfg->do_ip6;
	iter_env->supports_ipv4 = cfg->do_ip4;
	return 1;
}

/** filter out unsuitable targets
 * @param iter_env: iterator environment with ipv6-support flag.
 * @param env: module environment with infra cache.
 * @param name: zone name
 * @param namelen: length of name
 * @param qtype: query type (host order).
 * @param now: current time
 * @param a: address in delegation point we are examining.
 * @return an integer that signals the target suitability.
 *	as follows:
 *	-1: The address should be omitted from the list.
 *	    Because:
 *		o The address is bogus (DNSSEC validation failure).
 *		o Listed as donotquery
 *		o is ipv6 but no ipv6 support (in operating system).
 *		o is ipv4 but no ipv4 support (in operating system).
 *		o is lame
 *	Otherwise, an rtt in milliseconds.
 *	0 .. USEFUL_SERVER_TOP_TIMEOUT-1
 *		The roundtrip time timeout estimate. less than 2 minutes.
 *		Note that util/rtt.c has a MIN_TIMEOUT of 50 msec, thus
 *		values 0 .. 49 are not used, unless that is changed.
 *	USEFUL_SERVER_TOP_TIMEOUT
 *		This value exactly is given for unresponsive blacklisted.
 *	USEFUL_SERVER_TOP_TIMEOUT+1
 *		For non-blacklisted servers: huge timeout, but has traffic.
 *	USEFUL_SERVER_TOP_TIMEOUT*1 ..
 *		parent-side lame servers get this penalty. A dispreferential
 *		server. (lame in delegpt).
 *	USEFUL_SERVER_TOP_TIMEOUT*2 ..
 *		dnsseclame servers get penalty
 *	USEFUL_SERVER_TOP_TIMEOUT*3 ..
 *		recursion lame servers get penalty
 *	UNKNOWN_SERVER_NICENESS 
 *		If no information is known about the server, this is
 *		returned. 376 msec or so.
 *	+BLACKLIST_PENALTY (of USEFUL_TOP_TIMEOUT*4) for dnssec failed IPs.
 *
 * When a final value is chosen that is dnsseclame ; dnsseclameness checking
 * is turned off (so we do not discard the reply).
 * When a final value is chosen that is recursionlame; RD bit is set on query.
 * Because of the numbers this means recursionlame also have dnssec lameness
 * checking turned off. 
 */
static int
iter_filter_unsuitable(struct iter_env* iter_env, struct module_env* env,
	uint8_t* name, size_t namelen, uint16_t qtype, time_t now, 
	struct delegpt_addr* a)
{
	int rtt, lame, reclame, dnsseclame;
	if(a->bogus)
		return -1; /* address of server is bogus */
	if(donotq_lookup(iter_env->donotq, &a->addr, a->addrlen)) {
		log_addr(VERB_ALGO, "skip addr on the donotquery list",
			&a->addr, a->addrlen);
		return -1; /* server is on the donotquery list */
	}
	if(!iter_env->supports_ipv6 && addr_is_ip6(&a->addr, a->addrlen)) {
		return -1; /* there is no ip6 available */
	}
	if(!iter_env->supports_ipv4 && !addr_is_ip6(&a->addr, a->addrlen)) {
		return -1; /* there is no ip4 available */
	}
	/* check lameness - need zone , class info */
	if(infra_get_lame_rtt(env->infra_cache, &a->addr, a->addrlen, 
		name, namelen, qtype, &lame, &dnsseclame, &reclame, 
		&rtt, now)) {
		log_addr(VERB_ALGO, "servselect", &a->addr, a->addrlen);
		verbose(VERB_ALGO, "   rtt=%d%s%s%s%s", rtt,
			lame?" LAME":"",
			dnsseclame?" DNSSEC_LAME":"",
			reclame?" REC_LAME":"",
			a->lame?" ADDR_LAME":"");
		if(lame)
			return -1; /* server is lame */
		else if(rtt >= USEFUL_SERVER_TOP_TIMEOUT)
			/* server is unresponsive,
			 * we used to return TOP_TIMEOUT, but fairly useless,
			 * because if == TOP_TIMEOUT is dropped because
			 * blacklisted later, instead, remove it here, so
			 * other choices (that are not blacklisted) can be
			 * tried */
			return -1;
		/* select remainder from worst to best */
		else if(reclame)
			return rtt+USEFUL_SERVER_TOP_TIMEOUT*3; /* nonpref */
		else if(dnsseclame || a->dnsseclame)
			return rtt+USEFUL_SERVER_TOP_TIMEOUT*2; /* nonpref */
		else if(a->lame)
			return rtt+USEFUL_SERVER_TOP_TIMEOUT+1; /* nonpref */
		else	return rtt;
	}
	/* no server information present */
	if(a->dnsseclame)
		return UNKNOWN_SERVER_NICENESS+USEFUL_SERVER_TOP_TIMEOUT*2; /* nonpref */
	else if(a->lame)
		return USEFUL_SERVER_TOP_TIMEOUT+1+UNKNOWN_SERVER_NICENESS; /* nonpref */
	return UNKNOWN_SERVER_NICENESS;
}

/** lookup RTT information, and also store fastest rtt (if any) */
static int
iter_fill_rtt(struct iter_env* iter_env, struct module_env* env,
	uint8_t* name, size_t namelen, uint16_t qtype, time_t now, 
	struct delegpt* dp, int* best_rtt, struct sock_list* blacklist)
{
	int got_it = 0;
	struct delegpt_addr* a;
	if(dp->bogus)
		return 0; /* NS bogus, all bogus, nothing found */
	for(a=dp->result_list; a; a = a->next_result) {
		a->sel_rtt = iter_filter_unsuitable(iter_env, env, 
			name, namelen, qtype, now, a);
		if(a->sel_rtt != -1) {
			if(sock_list_find(blacklist, &a->addr, a->addrlen))
				a->sel_rtt += BLACKLIST_PENALTY;

			if(!got_it) {
				*best_rtt = a->sel_rtt;
				got_it = 1;
			} else if(a->sel_rtt < *best_rtt) {
				*best_rtt = a->sel_rtt;
			}
		}
	}
	return got_it;
}

/** filter the address list, putting best targets at front,
 * returns number of best targets (or 0, no suitable targets) */
static int
iter_filter_order(struct iter_env* iter_env, struct module_env* env,
	uint8_t* name, size_t namelen, uint16_t qtype, time_t now, 
	struct delegpt* dp, int* selected_rtt, int open_target, 
	struct sock_list* blacklist, time_t prefetch)
{
	int got_num = 0, low_rtt = 0, swap_to_front, rtt_band = RTT_BAND;
	struct delegpt_addr* a, *n, *prev=NULL;

	/* fillup sel_rtt and find best rtt in the bunch */
	got_num = iter_fill_rtt(iter_env, env, name, namelen, qtype, now, dp, 
		&low_rtt, blacklist);
	if(got_num == 0) 
		return 0;
	if(low_rtt >= USEFUL_SERVER_TOP_TIMEOUT &&
		(delegpt_count_missing_targets(dp) > 0 || open_target > 0)) {
		verbose(VERB_ALGO, "Bad choices, trying to get more choice");
		return 0; /* we want more choice. The best choice is a bad one.
			     return 0 to force the caller to fetch more */
	}

	if(env->cfg->low_rtt_permil != 0 && prefetch == 0 &&
		low_rtt < env->cfg->low_rtt &&
		ub_random_max(env->rnd, 1000) < env->cfg->low_rtt_permil) {
		/* the query is not prefetch, but for a downstream client,
		 * there is a low_rtt (fast) server.  We choose that x% of the
		 * time */
		/* pick rtt numbers from 0..LOWBAND_RTT */
		rtt_band = env->cfg->low_rtt - low_rtt;
	}

	got_num = 0;
	a = dp->result_list;
	while(a) {
		/* skip unsuitable targets */
		if(a->sel_rtt == -1) {
			prev = a;
			a = a->next_result;
			continue;
		}
		/* classify the server address and determine what to do */
		swap_to_front = 0;
		if(a->sel_rtt >= low_rtt && a->sel_rtt - low_rtt <= rtt_band) {
			got_num++;
			swap_to_front = 1;
		} else if(a->sel_rtt<low_rtt && low_rtt-a->sel_rtt<=rtt_band) {
			got_num++;
			swap_to_front = 1;
		}
		/* swap to front if necessary, or move to next result */
		if(swap_to_front && prev) {
			n = a->next_result;
			prev->next_result = n;
			a->next_result = dp->result_list;
			dp->result_list = a;
			a = n;
		} else {
			prev = a;
			a = a->next_result;
		}
	}
	*selected_rtt = low_rtt;

	if (env->cfg->prefer_ip6) {
		int got_num6 = 0;
		int low_rtt6 = 0;
		int i;
		int attempt = -1; /* filter to make sure addresses have
		  less attempts on them than the first, to force round
		  robin when all the IPv6 addresses fail */
		int num4ok = 0; /* number ip4 at low attempt count */
		int num4_lowrtt = 0;
		prev = NULL;
		a = dp->result_list;
		for(i = 0; i < got_num; i++) {
			swap_to_front = 0;
			if(a->addr.ss_family != AF_INET6 && attempt == -1) {
				/* if we only have ip4 at low attempt count,
				 * then ip6 is failing, and we need to
				 * select one of the remaining IPv4 addrs */
				attempt = a->attempts;
				num4ok++;
				num4_lowrtt = a->sel_rtt;
			} else if(a->addr.ss_family != AF_INET6 && attempt == a->attempts) {
				num4ok++;
				if(num4_lowrtt == 0 || a->sel_rtt < num4_lowrtt) {
					num4_lowrtt = a->sel_rtt;
				}
			}
			if(a->addr.ss_family == AF_INET6) {
				if(attempt == -1) {
					attempt = a->attempts;
				} else if(a->attempts > attempt) {
					break;
				}
				got_num6++;
				swap_to_front = 1;
				if(low_rtt6 == 0 || a->sel_rtt < low_rtt6) {
					low_rtt6 = a->sel_rtt;
				}
			}
			/* swap to front if IPv6, or move to next result */
			if(swap_to_front && prev) {
				n = a->next_result;
				prev->next_result = n;
				a->next_result = dp->result_list;
				dp->result_list = a;
				a = n;
			} else {
				prev = a;
				a = a->next_result;
			}
		}
		if(got_num6 > 0) {
			got_num = got_num6;
			*selected_rtt = low_rtt6;
		} else if(num4ok > 0) {
			got_num = num4ok;
			*selected_rtt = num4_lowrtt;
		}
	}
	return got_num;
}

struct delegpt_addr* 
iter_server_selection(struct iter_env* iter_env, 
	struct module_env* env, struct delegpt* dp, 
	uint8_t* name, size_t namelen, uint16_t qtype, int* dnssec_lame,
	int* chase_to_rd, int open_target, struct sock_list* blacklist,
	time_t prefetch)
{
	int sel;
	int selrtt;
	struct delegpt_addr* a, *prev;
	int num = iter_filter_order(iter_env, env, name, namelen, qtype,
		*env->now, dp, &selrtt, open_target, blacklist, prefetch);

	if(num == 0)
		return NULL;
	verbose(VERB_ALGO, "selrtt %d", selrtt);
	if(selrtt > BLACKLIST_PENALTY) {
		if(selrtt-BLACKLIST_PENALTY > USEFUL_SERVER_TOP_TIMEOUT*3) {
			verbose(VERB_ALGO, "chase to "
				"blacklisted recursion lame server");
			*chase_to_rd = 1;
		}
		if(selrtt-BLACKLIST_PENALTY > USEFUL_SERVER_TOP_TIMEOUT*2) {
			verbose(VERB_ALGO, "chase to "
				"blacklisted dnssec lame server");
			*dnssec_lame = 1;
		}
	} else {
		if(selrtt > USEFUL_SERVER_TOP_TIMEOUT*3) {
			verbose(VERB_ALGO, "chase to recursion lame server");
			*chase_to_rd = 1;
		}
		if(selrtt > USEFUL_SERVER_TOP_TIMEOUT*2) {
			verbose(VERB_ALGO, "chase to dnssec lame server");
			*dnssec_lame = 1;
		}
		if(selrtt == USEFUL_SERVER_TOP_TIMEOUT) {
			verbose(VERB_ALGO, "chase to blacklisted lame server");
			return NULL;
		}
	}

	if(num == 1) {
		a = dp->result_list;
		if(++a->attempts < OUTBOUND_MSG_RETRY)
			return a;
		dp->result_list = a->next_result;
		return a;
	}

	/* randomly select a target from the list */
	log_assert(num > 1);
	/* grab secure random number, to pick unexpected server.
	 * also we need it to be threadsafe. */
	sel = ub_random_max(env->rnd, num); 
	a = dp->result_list;
	prev = NULL;
	while(sel > 0 && a) {
		prev = a;
		a = a->next_result;
		sel--;
	}
	if(!a)  /* robustness */
		return NULL;
	if(++a->attempts < OUTBOUND_MSG_RETRY)
		return a;
	/* remove it from the delegation point result list */
	if(prev)
		prev->next_result = a->next_result;
	else	dp->result_list = a->next_result;
	return a;
}

struct dns_msg* 
dns_alloc_msg(sldns_buffer* pkt, struct msg_parse* msg, 
	struct regional* region)
{
	struct dns_msg* m = (struct dns_msg*)regional_alloc(region,
		sizeof(struct dns_msg));
	if(!m)
		return NULL;
	memset(m, 0, sizeof(*m));
	if(!parse_create_msg(pkt, msg, NULL, &m->qinfo, &m->rep, region)) {
		log_err("malloc failure: allocating incoming dns_msg");
		return NULL;
	}
	return m;
}

struct dns_msg* 
dns_copy_msg(struct dns_msg* from, struct regional* region)
{
	struct dns_msg* m = (struct dns_msg*)regional_alloc(region,
		sizeof(struct dns_msg));
	if(!m)
		return NULL;
	m->qinfo = from->qinfo;
	if(!(m->qinfo.qname = regional_alloc_init(region, from->qinfo.qname,
		from->qinfo.qname_len)))
		return NULL;
	if(!(m->rep = reply_info_copy(from->rep, NULL, region)))
		return NULL;
	return m;
}

void 
iter_dns_store(struct module_env* env, struct query_info* msgqinf,
	struct reply_info* msgrep, int is_referral, time_t leeway, int pside,
	struct regional* region, uint16_t flags)
{
	if(!dns_cache_store(env, msgqinf, msgrep, is_referral, leeway,
		pside, region, flags))
		log_err("out of memory: cannot store data in cache");
}

int 
iter_ns_probability(struct ub_randstate* rnd, int n, int m)
{
	int sel;
	if(n == m) /* 100% chance */
		return 1;
	/* we do not need secure random numbers here, but
	 * we do need it to be threadsafe, so we use this */
	sel = ub_random_max(rnd, m); 
	return (sel < n);
}

/** detect dependency cycle for query and target */
static int
causes_cycle(struct module_qstate* qstate, uint8_t* name, size_t namelen,
	uint16_t t, uint16_t c)
{
	struct query_info qinf;
	qinf.qname = name;
	qinf.qname_len = namelen;
	qinf.qtype = t;
	qinf.qclass = c;
	qinf.local_alias = NULL;
	fptr_ok(fptr_whitelist_modenv_detect_cycle(
		qstate->env->detect_cycle));
	return (*qstate->env->detect_cycle)(qstate, &qinf, 
		(uint16_t)(BIT_RD|BIT_CD), qstate->is_priming,
		qstate->is_valrec);
}

void 
iter_mark_cycle_targets(struct module_qstate* qstate, struct delegpt* dp)
{
	struct delegpt_ns* ns;
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->resolved)
			continue;
		/* see if this ns as target causes dependency cycle */
		if(causes_cycle(qstate, ns->name, ns->namelen, 
			LDNS_RR_TYPE_AAAA, qstate->qinfo.qclass) ||
		   causes_cycle(qstate, ns->name, ns->namelen, 
			LDNS_RR_TYPE_A, qstate->qinfo.qclass)) {
			log_nametypeclass(VERB_QUERY, "skipping target due "
			 	"to dependency cycle (harden-glue: no may "
				"fix some of the cycles)", 
				ns->name, LDNS_RR_TYPE_A, 
				qstate->qinfo.qclass);
			ns->resolved = 1;
		}
	}
}

void 
iter_mark_pside_cycle_targets(struct module_qstate* qstate, struct delegpt* dp)
{
	struct delegpt_ns* ns;
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->done_pside4 && ns->done_pside6)
			continue;
		/* see if this ns as target causes dependency cycle */
		if(causes_cycle(qstate, ns->name, ns->namelen, 
			LDNS_RR_TYPE_A, qstate->qinfo.qclass)) {
			log_nametypeclass(VERB_QUERY, "skipping target due "
			 	"to dependency cycle", ns->name,
				LDNS_RR_TYPE_A, qstate->qinfo.qclass);
			ns->done_pside4 = 1;
		}
		if(causes_cycle(qstate, ns->name, ns->namelen, 
			LDNS_RR_TYPE_AAAA, qstate->qinfo.qclass)) {
			log_nametypeclass(VERB_QUERY, "skipping target due "
			 	"to dependency cycle", ns->name,
				LDNS_RR_TYPE_AAAA, qstate->qinfo.qclass);
			ns->done_pside6 = 1;
		}
	}
}

int 
iter_dp_is_useless(struct query_info* qinfo, uint16_t qflags, 
	struct delegpt* dp)
{
	struct delegpt_ns* ns;
	/* check:
	 *      o RD qflag is on.
	 *      o no addresses are provided.
	 *      o all NS items are required glue.
	 * OR
	 *      o RD qflag is on.
	 *      o no addresses are provided.
	 *      o the query is for one of the nameservers in dp,
	 *        and that nameserver is a glue-name for this dp.
	 */
	if(!(qflags&BIT_RD))
		return 0;
	/* either available or unused targets */
	if(dp->usable_list || dp->result_list)
		return 0;
	
	/* see if query is for one of the nameservers, which is glue */
	if( (qinfo->qtype == LDNS_RR_TYPE_A ||
		qinfo->qtype == LDNS_RR_TYPE_AAAA) &&
		dname_subdomain_c(qinfo->qname, dp->name) &&
		delegpt_find_ns(dp, qinfo->qname, qinfo->qname_len))
		return 1;
	
	for(ns = dp->nslist; ns; ns = ns->next) {
		if(ns->resolved) /* skip failed targets */
			continue;
		if(!dname_subdomain_c(ns->name, dp->name))
			return 0; /* one address is not required glue */
	}
	return 1;
}

int
iter_qname_indicates_dnssec(struct module_env* env, struct query_info *qinfo)
{
	struct trust_anchor* a;
	if(!env || !env->anchors || !qinfo || !qinfo->qname)
		return 0;
	/* a trust anchor exists above the name? */
	if((a=anchors_lookup(env->anchors, qinfo->qname, qinfo->qname_len,
		qinfo->qclass))) { 
		if(a->numDS == 0 && a->numDNSKEY == 0) {
			/* insecure trust point */
			lock_basic_unlock(&a->lock);
			return 0;
		}
		lock_basic_unlock(&a->lock);
		return 1;
	}
	/* no trust anchor above it. */
	return 0;
}

int 
iter_indicates_dnssec(struct module_env* env, struct delegpt* dp,
        struct dns_msg* msg, uint16_t dclass)
{
	struct trust_anchor* a;
	/* information not available, !env->anchors can be common */
	if(!env || !env->anchors || !dp || !dp->name)
		return 0;
	/* a trust anchor exists with this name, RRSIGs expected */
	if((a=anchor_find(env->anchors, dp->name, dp->namelabs, dp->namelen,
		dclass))) {
		if(a->numDS == 0 && a->numDNSKEY == 0) {
			/* insecure trust point */
			lock_basic_unlock(&a->lock);
			return 0;
		}
		lock_basic_unlock(&a->lock);
		return 1;
	}
	/* see if DS rrset was given, in AUTH section */
	if(msg && msg->rep &&
		reply_find_rrset_section_ns(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_DS, dclass))
		return 1;
	/* look in key cache */
	if(env->key_cache) {
		struct key_entry_key* kk = key_cache_obtain(env->key_cache,
			dp->name, dp->namelen, dclass, env->scratch, *env->now);
		if(kk) {
			if(query_dname_compare(kk->name, dp->name) == 0) {
			  if(key_entry_isgood(kk) || key_entry_isbad(kk)) {
				regional_free_all(env->scratch);
				return 1;
			  } else if(key_entry_isnull(kk)) {
				regional_free_all(env->scratch);
				return 0;
			  }
			}
			regional_free_all(env->scratch);
		}
	}
	return 0;
}

int 
iter_msg_has_dnssec(struct dns_msg* msg)
{
	size_t i;
	if(!msg || !msg->rep)
		return 0;
	for(i=0; i<msg->rep->an_numrrsets + msg->rep->ns_numrrsets; i++) {
		if(((struct packed_rrset_data*)msg->rep->rrsets[i]->
			entry.data)->rrsig_count > 0)
			return 1;
	}
	/* empty message has no DNSSEC info, with DNSSEC the reply is
	 * not empty (NSEC) */
	return 0;
}

int iter_msg_from_zone(struct dns_msg* msg, struct delegpt* dp,
        enum response_type type, uint16_t dclass)
{
	if(!msg || !dp || !msg->rep || !dp->name)
		return 0;
	/* SOA RRset - always from reply zone */
	if(reply_find_rrset_section_an(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_SOA, dclass) ||
	   reply_find_rrset_section_ns(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_SOA, dclass))
		return 1;
	if(type == RESPONSE_TYPE_REFERRAL) {
		size_t i;
		/* if it adds a single label, i.e. we expect .com,
		 * and referral to example.com. NS ... , then origin zone
		 * is .com. For a referral to sub.example.com. NS ... then
		 * we do not know, since example.com. may be in between. */
		for(i=0; i<msg->rep->an_numrrsets+msg->rep->ns_numrrsets; 
			i++) {
			struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
			if(ntohs(s->rk.type) == LDNS_RR_TYPE_NS &&
				ntohs(s->rk.rrset_class) == dclass) {
				int l = dname_count_labels(s->rk.dname);
				if(l == dp->namelabs + 1 &&
					dname_strict_subdomain(s->rk.dname,
					l, dp->name, dp->namelabs))
					return 1;
			}
		}
		return 0;
	}
	log_assert(type==RESPONSE_TYPE_ANSWER || type==RESPONSE_TYPE_CNAME);
	/* not a referral, and not lame delegation (upwards), so, 
	 * any NS rrset must be from the zone itself */
	if(reply_find_rrset_section_an(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_NS, dclass) ||
	   reply_find_rrset_section_ns(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_NS, dclass))
		return 1;
	/* a DNSKEY set is expected at the zone apex as well */
	/* this is for 'minimal responses' for DNSKEYs */
	if(reply_find_rrset_section_an(msg->rep, dp->name, dp->namelen,
		LDNS_RR_TYPE_DNSKEY, dclass))
		return 1;
	return 0;
}

/**
 * check equality of two rrsets 
 * @param k1: rrset
 * @param k2: rrset
 * @return true if equal
 */
static int
rrset_equal(struct ub_packed_rrset_key* k1, struct ub_packed_rrset_key* k2)
{
	struct packed_rrset_data* d1 = (struct packed_rrset_data*)
		k1->entry.data;
	struct packed_rrset_data* d2 = (struct packed_rrset_data*)
		k2->entry.data;
	size_t i, t;
	if(k1->rk.dname_len != k2->rk.dname_len ||
		k1->rk.flags != k2->rk.flags ||
		k1->rk.type != k2->rk.type ||
		k1->rk.rrset_class != k2->rk.rrset_class ||
		query_dname_compare(k1->rk.dname, k2->rk.dname) != 0)
		return 0;
	if(	/* do not check ttl: d1->ttl != d2->ttl || */
		d1->count != d2->count ||
		d1->rrsig_count != d2->rrsig_count ||
		d1->trust != d2->trust ||
		d1->security != d2->security)
		return 0;
	t = d1->count + d1->rrsig_count;
	for(i=0; i<t; i++) {
		if(d1->rr_len[i] != d2->rr_len[i] ||
			/* no ttl check: d1->rr_ttl[i] != d2->rr_ttl[i] ||*/
			memcmp(d1->rr_data[i], d2->rr_data[i], 
				d1->rr_len[i]) != 0)
			return 0;
	}
	return 1;
}

int 
reply_equal(struct reply_info* p, struct reply_info* q, struct regional* region)
{
	size_t i;
	if(p->flags != q->flags ||
		p->qdcount != q->qdcount ||
		/* do not check TTL, this may differ */
		/*
		p->ttl != q->ttl ||
		p->prefetch_ttl != q->prefetch_ttl ||
		*/
		p->security != q->security ||
		p->an_numrrsets != q->an_numrrsets ||
		p->ns_numrrsets != q->ns_numrrsets ||
		p->ar_numrrsets != q->ar_numrrsets ||
		p->rrset_count != q->rrset_count)
		return 0;
	for(i=0; i<p->rrset_count; i++) {
		if(!rrset_equal(p->rrsets[i], q->rrsets[i])) {
			if(!rrset_canonical_equal(region, p->rrsets[i],
				q->rrsets[i])) {
				regional_free_all(region);
				return 0;
			}
			regional_free_all(region);
		}
	}
	return 1;
}

void 
caps_strip_reply(struct reply_info* rep)
{
	size_t i;
	if(!rep) return;
	/* see if message is a referral, in which case the additional and
	 * NS record cannot be removed */
	/* referrals have the AA flag unset (strict check, not elsewhere in
	 * unbound, but for 0x20 this is very convenient). */
	if(!(rep->flags&BIT_AA))
		return;
	/* remove the additional section from the reply */
	if(rep->ar_numrrsets != 0) {
		verbose(VERB_ALGO, "caps fallback: removing additional section");
		rep->rrset_count -= rep->ar_numrrsets;
		rep->ar_numrrsets = 0;
	}
	/* is there an NS set in the authority section to remove? */
	/* the failure case (Cisco firewalls) only has one rrset in authsec */
	for(i=rep->an_numrrsets; i<rep->an_numrrsets+rep->ns_numrrsets; i++) {
		struct ub_packed_rrset_key* s = rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NS) {
			/* remove NS rrset and break from loop (loop limits
			 * have changed) */
			/* move last rrset into this position (there is no
			 * additional section any more) */
			verbose(VERB_ALGO, "caps fallback: removing NS rrset");
			if(i < rep->rrset_count-1)
				rep->rrsets[i]=rep->rrsets[rep->rrset_count-1];
			rep->rrset_count --;
			rep->ns_numrrsets --;
			break;
		}
	}
}

int caps_failed_rcode(struct reply_info* rep)
{
	return !(FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NOERROR ||
		FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NXDOMAIN);
}

void 
iter_store_parentside_rrset(struct module_env* env, 
	struct ub_packed_rrset_key* rrset)
{
	struct rrset_ref ref;
	rrset = packed_rrset_copy_alloc(rrset, env->alloc, *env->now);
	if(!rrset) {
		log_err("malloc failure in store_parentside_rrset");
		return;
	}
	rrset->rk.flags |= PACKED_RRSET_PARENT_SIDE;
	rrset->entry.hash = rrset_key_hash(&rrset->rk);
	ref.key = rrset;
	ref.id = rrset->id;
	/* ignore ret: if it was in the cache, ref updated */
	(void)rrset_cache_update(env->rrset_cache, &ref, env->alloc, *env->now);
}

/** fetch NS record from reply, if any */
static struct ub_packed_rrset_key*
reply_get_NS_rrset(struct reply_info* rep)
{
	size_t i;
	for(i=0; i<rep->rrset_count; i++) {
		if(rep->rrsets[i]->rk.type == htons(LDNS_RR_TYPE_NS)) {
			return rep->rrsets[i];
		}
	}
	return NULL;
}

void
iter_store_parentside_NS(struct module_env* env, struct reply_info* rep)
{
	struct ub_packed_rrset_key* rrset = reply_get_NS_rrset(rep);
	if(rrset) {
		log_rrset_key(VERB_ALGO, "store parent-side NS", rrset);
		iter_store_parentside_rrset(env, rrset);
	}
}

void iter_store_parentside_neg(struct module_env* env, 
        struct query_info* qinfo, struct reply_info* rep)
{
	/* TTL: NS from referral in iq->deleg_msg,
	 *      or first RR from iq->response,
	 *      or servfail5secs if !iq->response */ 
	time_t ttl = NORR_TTL;
	struct ub_packed_rrset_key* neg;
	struct packed_rrset_data* newd;
	if(rep) {
		struct ub_packed_rrset_key* rrset = reply_get_NS_rrset(rep);
		if(!rrset && rep->rrset_count != 0) rrset = rep->rrsets[0];
		if(rrset) ttl = ub_packed_rrset_ttl(rrset);
	}
	/* create empty rrset to store */
	neg = (struct ub_packed_rrset_key*)regional_alloc(env->scratch,
	                sizeof(struct ub_packed_rrset_key));
	if(!neg) {
		log_err("out of memory in store_parentside_neg");
		return;
	}
	memset(&neg->entry, 0, sizeof(neg->entry));
	neg->entry.key = neg;
	neg->rk.type = htons(qinfo->qtype);
	neg->rk.rrset_class = htons(qinfo->qclass);
	neg->rk.flags = 0;
	neg->rk.dname = regional_alloc_init(env->scratch, qinfo->qname, 
		qinfo->qname_len);
	if(!neg->rk.dname) {
		log_err("out of memory in store_parentside_neg");
		return;
	}
	neg->rk.dname_len = qinfo->qname_len;
	neg->entry.hash = rrset_key_hash(&neg->rk);
	newd = (struct packed_rrset_data*)regional_alloc_zero(env->scratch, 
		sizeof(struct packed_rrset_data) + sizeof(size_t) +
		sizeof(uint8_t*) + sizeof(time_t) + sizeof(uint16_t));
	if(!newd) {
		log_err("out of memory in store_parentside_neg");
		return;
	}
	neg->entry.data = newd;
	newd->ttl = ttl;
	/* entry must have one RR, otherwise not valid in cache.
	 * put in one RR with empty rdata: those are ignored as nameserver */
	newd->count = 1;
	newd->rrsig_count = 0;
	newd->trust = rrset_trust_ans_noAA;
	newd->rr_len = (size_t*)((uint8_t*)newd +
		sizeof(struct packed_rrset_data));
	newd->rr_len[0] = 0 /* zero len rdata */ + sizeof(uint16_t);
	packed_rrset_ptr_fixup(newd);
	newd->rr_ttl[0] = newd->ttl;
	sldns_write_uint16(newd->rr_data[0], 0 /* zero len rdata */);
	/* store it */
	log_rrset_key(VERB_ALGO, "store parent-side negative", neg);
	iter_store_parentside_rrset(env, neg);
}

int 
iter_lookup_parent_NS_from_cache(struct module_env* env, struct delegpt* dp,
	struct regional* region, struct query_info* qinfo)
{
	struct ub_packed_rrset_key* akey;
	akey = rrset_cache_lookup(env->rrset_cache, dp->name, 
		dp->namelen, LDNS_RR_TYPE_NS, qinfo->qclass, 
		PACKED_RRSET_PARENT_SIDE, *env->now, 0);
	if(akey) {
		log_rrset_key(VERB_ALGO, "found parent-side NS in cache", akey);
		dp->has_parent_side_NS = 1;
		/* and mark the new names as lame */
		if(!delegpt_rrset_add_ns(dp, region, akey, 1)) {
			lock_rw_unlock(&akey->entry.lock);
			return 0;
		}
		lock_rw_unlock(&akey->entry.lock);
	}
	return 1;
}

int iter_lookup_parent_glue_from_cache(struct module_env* env,
        struct delegpt* dp, struct regional* region, struct query_info* qinfo)
{
	struct ub_packed_rrset_key* akey;
	struct delegpt_ns* ns;
	size_t num = delegpt_count_targets(dp);
	for(ns = dp->nslist; ns; ns = ns->next) {
		/* get cached parentside A */
		akey = rrset_cache_lookup(env->rrset_cache, ns->name, 
			ns->namelen, LDNS_RR_TYPE_A, qinfo->qclass, 
			PACKED_RRSET_PARENT_SIDE, *env->now, 0);
		if(akey) {
			log_rrset_key(VERB_ALGO, "found parent-side", akey);
			ns->done_pside4 = 1;
			/* a negative-cache-element has no addresses it adds */
			if(!delegpt_add_rrset_A(dp, region, akey, 1))
				log_err("malloc failure in lookup_parent_glue");
			lock_rw_unlock(&akey->entry.lock);
		}
		/* get cached parentside AAAA */
		akey = rrset_cache_lookup(env->rrset_cache, ns->name, 
			ns->namelen, LDNS_RR_TYPE_AAAA, qinfo->qclass, 
			PACKED_RRSET_PARENT_SIDE, *env->now, 0);
		if(akey) {
			log_rrset_key(VERB_ALGO, "found parent-side", akey);
			ns->done_pside6 = 1;
			/* a negative-cache-element has no addresses it adds */
			if(!delegpt_add_rrset_AAAA(dp, region, akey, 1))
				log_err("malloc failure in lookup_parent_glue");
			lock_rw_unlock(&akey->entry.lock);
		}
	}
	/* see if new (but lame) addresses have become available */
	return delegpt_count_targets(dp) != num;
}

int 
iter_get_next_root(struct iter_hints* hints, struct iter_forwards* fwd, 
	uint16_t* c)
{
	uint16_t c1 = *c, c2 = *c;
	int r1 = hints_next_root(hints, &c1);
	int r2 = forwards_next_root(fwd, &c2);
	if(!r1 && !r2) /* got none, end of list */
		return 0;
	else if(!r1) /* got one, return that */
		*c = c2;
	else if(!r2)
		*c = c1;
	else if(c1 < c2) /* got both take smallest */
		*c = c1;
	else	*c = c2;
	return 1;
}

void
iter_scrub_ds(struct dns_msg* msg, struct ub_packed_rrset_key* ns, uint8_t* z)
{
	/* Only the DS record for the delegation itself is expected.
	 * We allow DS for everything between the bailiwick and the 
	 * zonecut, thus DS records must be at or above the zonecut.
	 * And the DS records must be below the server authority zone.
	 * The answer section is already scrubbed. */
	size_t i = msg->rep->an_numrrsets;
	while(i < (msg->rep->an_numrrsets + msg->rep->ns_numrrsets)) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_DS &&
			(!ns || !dname_subdomain_c(ns->rk.dname, s->rk.dname)
			|| query_dname_compare(z, s->rk.dname) == 0)) {
			log_nametypeclass(VERB_ALGO, "removing irrelevant DS",
				s->rk.dname, ntohs(s->rk.type),
				ntohs(s->rk.rrset_class));
			memmove(msg->rep->rrsets+i, msg->rep->rrsets+i+1,
				sizeof(struct ub_packed_rrset_key*) * 
				(msg->rep->rrset_count-i-1));
			msg->rep->ns_numrrsets--;
			msg->rep->rrset_count--;
			/* stay at same i, but new record */
			continue;
		}
		i++;
	}
}

void iter_dec_attempts(struct delegpt* dp, int d)
{
	struct delegpt_addr* a;
	for(a=dp->target_list; a; a = a->next_target) {
		if(a->attempts >= OUTBOUND_MSG_RETRY) {
			/* add back to result list */
			a->next_result = dp->result_list;
			dp->result_list = a;
		}
		if(a->attempts > d)
			a->attempts -= d;
		else a->attempts = 0;
	}
}

void iter_merge_retry_counts(struct delegpt* dp, struct delegpt* old)
{
	struct delegpt_addr* a, *o, *prev;
	for(a=dp->target_list; a; a = a->next_target) {
		o = delegpt_find_addr(old, &a->addr, a->addrlen);
		if(o) {
			log_addr(VERB_ALGO, "copy attempt count previous dp",
				&a->addr, a->addrlen);
			a->attempts = o->attempts;
		}
	}
	prev = NULL;
	a = dp->usable_list;
	while(a) {
		if(a->attempts >= OUTBOUND_MSG_RETRY) {
			log_addr(VERB_ALGO, "remove from usable list dp",
				&a->addr, a->addrlen);
			/* remove from result list */
			if(prev)
				prev->next_usable = a->next_usable;
			else	dp->usable_list = a->next_usable;
			/* prev stays the same */
			a = a->next_usable;
			continue;
		}
		prev = a;
		a = a->next_usable;
	}
}

int
iter_ds_toolow(struct dns_msg* msg, struct delegpt* dp)
{
	/* if for query example.com, there is example.com SOA or a subdomain
	 * of example.com, then we are too low and need to fetch NS. */
	size_t i;
	/* if we have a DNAME or CNAME we are probably wrong */
	/* if we have a qtype DS in the answer section, its fine */
	for(i=0; i < msg->rep->an_numrrsets; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_DNAME ||
			ntohs(s->rk.type) == LDNS_RR_TYPE_CNAME) {
			/* not the right answer, maybe too low, check the
			 * RRSIG signer name (if there is any) for a hint
			 * that it is from the dp zone anyway */
			uint8_t* sname;
			size_t slen;
			val_find_rrset_signer(s, &sname, &slen);
			if(sname && query_dname_compare(dp->name, sname)==0)
				return 0; /* it is fine, from the right dp */
			return 1;
		}
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_DS)
			return 0; /* fine, we have a DS record */
	}
	for(i=msg->rep->an_numrrsets;
		i < msg->rep->an_numrrsets + msg->rep->ns_numrrsets; i++) {
		struct ub_packed_rrset_key* s = msg->rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_SOA) {
			if(dname_subdomain_c(s->rk.dname, msg->qinfo.qname))
				return 1; /* point is too low */
			if(query_dname_compare(s->rk.dname, dp->name)==0)
				return 0; /* right dp */
		}
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC ||
			ntohs(s->rk.type) == LDNS_RR_TYPE_NSEC3) {
			uint8_t* sname;
			size_t slen;
			val_find_rrset_signer(s, &sname, &slen);
			if(sname && query_dname_compare(dp->name, sname)==0)
				return 0; /* it is fine, from the right dp */
			return 1;
		}
	}
	/* we do not know */
	return 1;
}

int iter_dp_cangodown(struct query_info* qinfo, struct delegpt* dp)
{
	/* no delegation point, do not see how we can go down,
	 * robust check, it should really exist */
	if(!dp) return 0;

	/* see if dp equals the qname, then we cannot go down further */
	if(query_dname_compare(qinfo->qname, dp->name) == 0)
		return 0;
	/* if dp is one label above the name we also cannot go down further */
	if(dname_count_labels(qinfo->qname) == dp->namelabs+1)
		return 0;
	return 1;
}
