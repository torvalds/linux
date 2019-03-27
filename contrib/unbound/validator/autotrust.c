/*
 * validator/autotrust.c - RFC5011 trust anchor management for unbound.
 *
 * Copyright (c) 2009, NLnet Labs. All rights reserved.
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
 * Contains autotrust implementation. The implementation was taken from 
 * the autotrust daemon (BSD licensed), written by Matthijs Mekking.
 * It was modified to fit into unbound. The state table process is the same.
 */
#include "config.h"
#include "validator/autotrust.h"
#include "validator/val_anchor.h"
#include "validator/val_utils.h"
#include "validator/val_sigcrypt.h"
#include "util/data/dname.h"
#include "util/data/packed_rrset.h"
#include "util/log.h"
#include "util/module.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "util/regional.h"
#include "util/random.h"
#include "util/data/msgparse.h"
#include "services/mesh.h"
#include "services/cache/rrset.h"
#include "validator/val_kcache.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "sldns/str2wire.h"
#include "sldns/keyraw.h"
#include "sldns/rrdef.h"
#include <stdarg.h>
#include <ctype.h>

/** number of times a key must be seen before it can become valid */
#define MIN_PENDINGCOUNT 2

/** Event: Revoked */
static void do_revoked(struct module_env* env, struct autr_ta* anchor, int* c);

struct autr_global_data* autr_global_create(void)
{
	struct autr_global_data* global;
	global = (struct autr_global_data*)malloc(sizeof(*global));
	if(!global) 
		return NULL;
	rbtree_init(&global->probe, &probetree_cmp);
	return global;
}

void autr_global_delete(struct autr_global_data* global)
{
	if(!global)
		return;
	/* elements deleted by parent */
	memset(global, 0, sizeof(*global));
	free(global);
}

int probetree_cmp(const void* x, const void* y)
{
	struct trust_anchor* a = (struct trust_anchor*)x;
	struct trust_anchor* b = (struct trust_anchor*)y;
	log_assert(a->autr && b->autr);
	if(a->autr->next_probe_time < b->autr->next_probe_time)
		return -1;
	if(a->autr->next_probe_time > b->autr->next_probe_time)
		return 1;
	/* time is equal, sort on trust point identity */
	return anchor_cmp(x, y);
}

size_t 
autr_get_num_anchors(struct val_anchors* anchors)
{
	size_t res = 0;
	if(!anchors)
		return 0;
	lock_basic_lock(&anchors->lock);
	if(anchors->autr)
		res = anchors->autr->probe.count;
	lock_basic_unlock(&anchors->lock);
	return res;
}

/** Position in string */
static int
position_in_string(char *str, const char* sub)
{
	char* pos = strstr(str, sub);
	if(pos)
		return (int)(pos-str)+(int)strlen(sub);
	return -1;
}

/** Debug routine to print pretty key information */
static void
verbose_key(struct autr_ta* ta, enum verbosity_value level, 
	const char* format, ...) ATTR_FORMAT(printf, 3, 4);

/** 
 * Implementation of debug pretty key print 
 * @param ta: trust anchor key with DNSKEY data.
 * @param level: verbosity level to print at.
 * @param format: printf style format string.
 */
static void
verbose_key(struct autr_ta* ta, enum verbosity_value level, 
	const char* format, ...) 
{
	va_list args;
	va_start(args, format);
	if(verbosity >= level) {
		char* str = sldns_wire2str_dname(ta->rr, ta->dname_len);
		int keytag = (int)sldns_calc_keytag_raw(sldns_wirerr_get_rdata(
			ta->rr, ta->rr_len, ta->dname_len),
			sldns_wirerr_get_rdatalen(ta->rr, ta->rr_len,
			ta->dname_len));
		char msg[MAXSYSLOGMSGLEN];
		vsnprintf(msg, sizeof(msg), format, args);
		verbose(level, "%s key %d %s", str?str:"??", keytag, msg);
		free(str);
	}
	va_end(args);
}

/** 
 * Parse comments 
 * @param str: to parse
 * @param ta: trust key autotrust metadata
 * @return false on failure.
 */
static int
parse_comments(char* str, struct autr_ta* ta)
{
        int len = (int)strlen(str), pos = 0, timestamp = 0;
        char* comment = (char*) malloc(sizeof(char)*len+1);
        char* comments = comment;
	if(!comment) {
		log_err("malloc failure in parse");
                return 0;
	}
	/* skip over whitespace and data at start of line */
        while (*str != '\0' && *str != ';')
                str++;
        if (*str == ';')
                str++;
        /* copy comments */
        while (*str != '\0')
        {
                *comments = *str;
                comments++;
                str++;
        }
        *comments = '\0';

        comments = comment;

        /* read state */
        pos = position_in_string(comments, "state=");
        if (pos >= (int) strlen(comments))
        {
		log_err("parse error");
                free(comment);
                return 0;
        }
        if (pos <= 0)
                ta->s = AUTR_STATE_VALID;
        else
        {
                int s = (int) comments[pos] - '0';
                switch(s)
                {
                        case AUTR_STATE_START:
                        case AUTR_STATE_ADDPEND:
                        case AUTR_STATE_VALID:
                        case AUTR_STATE_MISSING:
                        case AUTR_STATE_REVOKED:
                        case AUTR_STATE_REMOVED:
                                ta->s = s;
                                break;
                        default:
				verbose_key(ta, VERB_OPS, "has undefined "
					"state, considered NewKey");
                                ta->s = AUTR_STATE_START;
                                break;
                }
        }
        /* read pending count */
        pos = position_in_string(comments, "count=");
        if (pos >= (int) strlen(comments))
        {
		log_err("parse error");
                free(comment);
                return 0;
        }
        if (pos <= 0)
                ta->pending_count = 0;
        else
        {
                comments += pos;
                ta->pending_count = (uint8_t)atoi(comments);
        }

        /* read last change */
        pos = position_in_string(comments, "lastchange=");
        if (pos >= (int) strlen(comments))
        {
		log_err("parse error");
                free(comment);
                return 0;
        }
        if (pos >= 0)
        {
                comments += pos;
                timestamp = atoi(comments);
        }
        if (pos < 0 || !timestamp)
		ta->last_change = 0;
        else
                ta->last_change = (time_t)timestamp;

        free(comment);
        return 1;
}

/** Check if a line contains data (besides comments) */
static int
str_contains_data(char* str, char comment)
{
        while (*str != '\0') {
                if (*str == comment || *str == '\n')
                        return 0;
                if (*str != ' ' && *str != '\t')
                        return 1;
                str++;
        }
        return 0;
}

/** Get DNSKEY flags
 * rdata without rdatalen in front of it. */
static int
dnskey_flags(uint16_t t, uint8_t* rdata, size_t len)
{
	uint16_t f;
	if(t != LDNS_RR_TYPE_DNSKEY)
		return 0;
	if(len < 2)
		return 0;
	memmove(&f, rdata, 2);
	f = ntohs(f);
	return (int)f;
}

/** Check if KSK DNSKEY.
 * pass rdata without rdatalen in front of it */
static int
rr_is_dnskey_sep(uint16_t t, uint8_t* rdata, size_t len)
{
	return (dnskey_flags(t, rdata, len)&DNSKEY_BIT_SEP);
}

/** Check if TA is KSK DNSKEY */
static int
ta_is_dnskey_sep(struct autr_ta* ta)
{
	return (dnskey_flags(
		sldns_wirerr_get_type(ta->rr, ta->rr_len, ta->dname_len),
		sldns_wirerr_get_rdata(ta->rr, ta->rr_len, ta->dname_len),
		sldns_wirerr_get_rdatalen(ta->rr, ta->rr_len, ta->dname_len)
		) & DNSKEY_BIT_SEP);
}

/** Check if REVOKED DNSKEY
 * pass rdata without rdatalen in front of it */
static int
rr_is_dnskey_revoked(uint16_t t, uint8_t* rdata, size_t len)
{
	return (dnskey_flags(t, rdata, len)&LDNS_KEY_REVOKE_KEY);
}

/** create ta */
static struct autr_ta*
autr_ta_create(uint8_t* rr, size_t rr_len, size_t dname_len)
{
	struct autr_ta* ta = (struct autr_ta*)calloc(1, sizeof(*ta));
	if(!ta) {
		free(rr);
		return NULL;
	}
	ta->rr = rr;
	ta->rr_len = rr_len;
	ta->dname_len = dname_len;
	return ta;
}

/** create tp */
static struct trust_anchor*
autr_tp_create(struct val_anchors* anchors, uint8_t* own, size_t own_len,
	uint16_t dc)
{
	struct trust_anchor* tp = (struct trust_anchor*)calloc(1, sizeof(*tp));
	if(!tp) return NULL;
	tp->name = memdup(own, own_len);
	if(!tp->name) {
		free(tp);
		return NULL;
	}
	tp->namelen = own_len;
	tp->namelabs = dname_count_labels(tp->name);
	tp->node.key = tp;
	tp->dclass = dc;
	tp->autr = (struct autr_point_data*)calloc(1, sizeof(*tp->autr));
	if(!tp->autr) {
		free(tp->name);
		free(tp);
		return NULL;
	}
	tp->autr->pnode.key = tp;

	lock_basic_lock(&anchors->lock);
	if(!rbtree_insert(anchors->tree, &tp->node)) {
		lock_basic_unlock(&anchors->lock);
		log_err("trust anchor presented twice");
		free(tp->name);
		free(tp->autr);
		free(tp);
		return NULL;
	}
	if(!rbtree_insert(&anchors->autr->probe, &tp->autr->pnode)) {
		(void)rbtree_delete(anchors->tree, tp);
		lock_basic_unlock(&anchors->lock);
		log_err("trust anchor in probetree twice");
		free(tp->name);
		free(tp->autr);
		free(tp);
		return NULL;
	}
	lock_basic_unlock(&anchors->lock);
	lock_basic_init(&tp->lock);
	lock_protect(&tp->lock, tp, sizeof(*tp));
	lock_protect(&tp->lock, tp->autr, sizeof(*tp->autr));
	return tp;
}

/** delete assembled rrsets */
static void
autr_rrset_delete(struct ub_packed_rrset_key* r)
{
	if(r) {
		free(r->rk.dname);
		free(r->entry.data);
		free(r);
	}
}

void autr_point_delete(struct trust_anchor* tp)
{
	if(!tp)
		return;
	lock_unprotect(&tp->lock, tp);
	lock_unprotect(&tp->lock, tp->autr);
	lock_basic_destroy(&tp->lock);
	autr_rrset_delete(tp->ds_rrset);
	autr_rrset_delete(tp->dnskey_rrset);
	if(tp->autr) {
		struct autr_ta* p = tp->autr->keys, *np;
		while(p) {
			np = p->next;
			free(p->rr);
			free(p);
			p = np;
		}
		free(tp->autr->file);
		free(tp->autr);
	}
	free(tp->name);
	free(tp);
}

/** find or add a new trust point for autotrust */
static struct trust_anchor*
find_add_tp(struct val_anchors* anchors, uint8_t* rr, size_t rr_len,
	size_t dname_len)
{
	struct trust_anchor* tp;
	tp = anchor_find(anchors, rr, dname_count_labels(rr), dname_len,
		sldns_wirerr_get_class(rr, rr_len, dname_len));
	if(tp) {
		if(!tp->autr) {
			log_err("anchor cannot be with and without autotrust");
			lock_basic_unlock(&tp->lock);
			return NULL;
		}
		return tp;
	}
	tp = autr_tp_create(anchors, rr, dname_len, sldns_wirerr_get_class(rr,
		rr_len, dname_len));
	if(!tp)	
		return NULL;
	lock_basic_lock(&tp->lock);
	return tp;
}

/** Add trust anchor from RR */
static struct autr_ta*
add_trustanchor_frm_rr(struct val_anchors* anchors, uint8_t* rr, size_t rr_len,
        size_t dname_len, struct trust_anchor** tp)
{
	struct autr_ta* ta = autr_ta_create(rr, rr_len, dname_len);
	if(!ta) 
		return NULL;
	*tp = find_add_tp(anchors, rr, rr_len, dname_len);
	if(!*tp) {
		free(ta->rr);
		free(ta);
		return NULL;
	}
	/* add ta to tp */
	ta->next = (*tp)->autr->keys;
	(*tp)->autr->keys = ta;
	lock_basic_unlock(&(*tp)->lock);
	return ta;
}

/**
 * Add new trust anchor from a string in file.
 * @param anchors: all anchors
 * @param str: string with anchor and comments, if any comments.
 * @param tp: trust point returned.
 * @param origin: what to use for @
 * @param origin_len: length of origin
 * @param prev: previous rr name
 * @param prev_len: length of prev
 * @param skip: if true, the result is NULL, but not an error, skip it.
 * @return new key in trust point.
 */
static struct autr_ta*
add_trustanchor_frm_str(struct val_anchors* anchors, char* str, 
	struct trust_anchor** tp, uint8_t* origin, size_t origin_len,
	uint8_t** prev, size_t* prev_len, int* skip)
{
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t rr_len = sizeof(rr), dname_len;
	uint8_t* drr;
	int lstatus;
        if (!str_contains_data(str, ';')) {
		*skip = 1;
                return NULL; /* empty line */
	}
	if(0 != (lstatus = sldns_str2wire_rr_buf(str, rr, &rr_len, &dname_len,
		0, origin, origin_len, *prev, *prev_len)))
	{
		log_err("ldns error while converting string to RR at%d: %s: %s",
			LDNS_WIREPARSE_OFFSET(lstatus),
			sldns_get_errorstr_parse(lstatus), str);
		return NULL;
	}
	free(*prev);
	*prev = memdup(rr, dname_len);
	*prev_len = dname_len;
	if(!*prev) {
		log_err("malloc failure in add_trustanchor");
		return NULL;
	}
	if(sldns_wirerr_get_type(rr, rr_len, dname_len)!=LDNS_RR_TYPE_DNSKEY &&
		sldns_wirerr_get_type(rr, rr_len, dname_len)!=LDNS_RR_TYPE_DS) {
		*skip = 1;
		return NULL; /* only DS and DNSKEY allowed */
	}
	drr = memdup(rr, rr_len);
	if(!drr) {
		log_err("malloc failure in add trustanchor");
		return NULL;
	}
	return add_trustanchor_frm_rr(anchors, drr, rr_len, dname_len, tp);
}

/** 
 * Load single anchor 
 * @param anchors: all points.
 * @param str: comments line
 * @param fname: filename
 * @param origin: the $ORIGIN.
 * @param origin_len: length of origin
 * @param prev: passed to ldns.
 * @param prev_len: length of prev
 * @param skip: if true, the result is NULL, but not an error, skip it.
 * @return false on failure, otherwise the tp read.
 */
static struct trust_anchor*
load_trustanchor(struct val_anchors* anchors, char* str, const char* fname,
	uint8_t* origin, size_t origin_len, uint8_t** prev, size_t* prev_len,
	int* skip)
{
	struct autr_ta* ta = NULL;
	struct trust_anchor* tp = NULL;

	ta = add_trustanchor_frm_str(anchors, str, &tp, origin, origin_len,
		prev, prev_len, skip);
	if(!ta)
		return NULL;
	lock_basic_lock(&tp->lock);
	if(!parse_comments(str, ta)) {
		lock_basic_unlock(&tp->lock);
		return NULL;
	}
	if(!tp->autr->file) {
		tp->autr->file = strdup(fname);
		if(!tp->autr->file) {
			lock_basic_unlock(&tp->lock);
			log_err("malloc failure");
			return NULL;
		}
	}
	lock_basic_unlock(&tp->lock);
        return tp;
}

/** iterator for DSes from keylist. return true if a next element exists */
static int
assemble_iterate_ds(struct autr_ta** list, uint8_t** rr, size_t* rr_len,
	size_t* dname_len)
{
	while(*list) {
		if(sldns_wirerr_get_type((*list)->rr, (*list)->rr_len,
			(*list)->dname_len) == LDNS_RR_TYPE_DS) {
			*rr = (*list)->rr;
			*rr_len = (*list)->rr_len;
			*dname_len = (*list)->dname_len;
			*list = (*list)->next;
			return 1;
		}
		*list = (*list)->next;
	}
	return 0;
}

/** iterator for DNSKEYs from keylist. return true if a next element exists */
static int
assemble_iterate_dnskey(struct autr_ta** list, uint8_t** rr, size_t* rr_len,
	size_t* dname_len)
{
	while(*list) {
		if(sldns_wirerr_get_type((*list)->rr, (*list)->rr_len,
		   (*list)->dname_len) != LDNS_RR_TYPE_DS &&
			((*list)->s == AUTR_STATE_VALID || 
			 (*list)->s == AUTR_STATE_MISSING)) {
			*rr = (*list)->rr;
			*rr_len = (*list)->rr_len;
			*dname_len = (*list)->dname_len;
			*list = (*list)->next;
			return 1;
		}
		*list = (*list)->next;
	}
	return 0;
}

/** see if iterator-list has any elements in it, or it is empty */
static int
assemble_iterate_hasfirst(int iter(struct autr_ta**, uint8_t**, size_t*,
	size_t*), struct autr_ta* list)
{
	uint8_t* rr = NULL;
	size_t rr_len = 0, dname_len = 0;
	return iter(&list, &rr, &rr_len, &dname_len);
}

/** number of elements in iterator list */
static size_t
assemble_iterate_count(int iter(struct autr_ta**, uint8_t**, size_t*,
	size_t*), struct autr_ta* list)
{
	uint8_t* rr = NULL;
	size_t i = 0, rr_len = 0, dname_len = 0;
	while(iter(&list, &rr, &rr_len, &dname_len)) {
		i++;
	}
	return i;
}

/**
 * Create a ub_packed_rrset_key allocated on the heap.
 * It therefore does not have the correct ID value, and cannot be used
 * inside the cache.  It can be used in storage outside of the cache.
 * Keys for the cache have to be obtained from alloc.h .
 * @param iter: iterator over the elements in the list.  It filters elements.
 * @param list: the list.
 * @return key allocated or NULL on failure.
 */
static struct ub_packed_rrset_key* 
ub_packed_rrset_heap_key(int iter(struct autr_ta**, uint8_t**, size_t*,
	size_t*), struct autr_ta* list)
{
	uint8_t* rr = NULL;
	size_t rr_len = 0, dname_len = 0;
	struct ub_packed_rrset_key* k;
	if(!iter(&list, &rr, &rr_len, &dname_len))
		return NULL;
	k = (struct ub_packed_rrset_key*)calloc(1, sizeof(*k));
	if(!k)
		return NULL;
	k->rk.type = htons(sldns_wirerr_get_type(rr, rr_len, dname_len));
	k->rk.rrset_class = htons(sldns_wirerr_get_class(rr, rr_len, dname_len));
	k->rk.dname_len = dname_len;
	k->rk.dname = memdup(rr, dname_len);
	if(!k->rk.dname) {
		free(k);
		return NULL;
	}
	return k;
}

/**
 * Create packed_rrset data on the heap.
 * @param iter: iterator over the elements in the list.  It filters elements.
 * @param list: the list.
 * @return data allocated or NULL on failure.
 */
static struct packed_rrset_data* 
packed_rrset_heap_data(int iter(struct autr_ta**, uint8_t**, size_t*,
	size_t*), struct autr_ta* list)
{
	uint8_t* rr = NULL;
	size_t rr_len = 0, dname_len = 0;
	struct packed_rrset_data* data;
	size_t count=0, rrsig_count=0, len=0, i, total;
	uint8_t* nextrdata;
	struct autr_ta* list_i;
	time_t ttl = 0;

	list_i = list;
	while(iter(&list_i, &rr, &rr_len, &dname_len)) {
		if(sldns_wirerr_get_type(rr, rr_len, dname_len) ==
			LDNS_RR_TYPE_RRSIG)
			rrsig_count++;
		else	count++;
		/* sizeof the rdlength + rdatalen */
		len += 2 + sldns_wirerr_get_rdatalen(rr, rr_len, dname_len);
		ttl = (time_t)sldns_wirerr_get_ttl(rr, rr_len, dname_len);
	}
	if(count == 0 && rrsig_count == 0)
		return NULL;

	/* allocate */
	total = count + rrsig_count;
	len += sizeof(*data) + total*(sizeof(size_t) + sizeof(time_t) + 
		sizeof(uint8_t*));
	data = (struct packed_rrset_data*)calloc(1, len);
	if(!data)
		return NULL;

	/* fill it */
	data->ttl = ttl;
	data->count = count;
	data->rrsig_count = rrsig_count;
	data->rr_len = (size_t*)((uint8_t*)data +
		sizeof(struct packed_rrset_data));
	data->rr_data = (uint8_t**)&(data->rr_len[total]);
	data->rr_ttl = (time_t*)&(data->rr_data[total]);
	nextrdata = (uint8_t*)&(data->rr_ttl[total]);

	/* fill out len, ttl, fields */
	list_i = list;
	i = 0;
	while(iter(&list_i, &rr, &rr_len, &dname_len)) {
		data->rr_ttl[i] = (time_t)sldns_wirerr_get_ttl(rr, rr_len,
			dname_len);
		if(data->rr_ttl[i] < data->ttl)
			data->ttl = data->rr_ttl[i];
		data->rr_len[i] = 2 /* the rdlength */ +
			sldns_wirerr_get_rdatalen(rr, rr_len, dname_len);
		i++;
	}

	/* fixup rest of ptrs */
	for(i=0; i<total; i++) {
		data->rr_data[i] = nextrdata;
		nextrdata += data->rr_len[i];
	}

	/* copy data in there */
	list_i = list;
	i = 0;
	while(iter(&list_i, &rr, &rr_len, &dname_len)) {
		log_assert(data->rr_data[i]);
		memmove(data->rr_data[i],
			sldns_wirerr_get_rdatawl(rr, rr_len, dname_len),
			data->rr_len[i]);
		i++;
	}

	if(data->rrsig_count && data->count == 0) {
		data->count = data->rrsig_count; /* rrset type is RRSIG */
		data->rrsig_count = 0;
	}
	return data;
}

/**
 * Assemble the trust anchors into DS and DNSKEY packed rrsets.
 * Uses only VALID and MISSING DNSKEYs.
 * Read the sldns_rrs and builds packed rrsets
 * @param tp: the trust point. Must be locked.
 * @return false on malloc failure.
 */
static int 
autr_assemble(struct trust_anchor* tp)
{
	struct ub_packed_rrset_key* ubds=NULL, *ubdnskey=NULL;

	/* make packed rrset keys - malloced with no ID number, they
	 * are not in the cache */
	/* make packed rrset data (if there is a key) */
	if(assemble_iterate_hasfirst(assemble_iterate_ds, tp->autr->keys)) {
		ubds = ub_packed_rrset_heap_key(
			assemble_iterate_ds, tp->autr->keys);
		if(!ubds)
			goto error_cleanup;
		ubds->entry.data = packed_rrset_heap_data(
			assemble_iterate_ds, tp->autr->keys);
		if(!ubds->entry.data)
			goto error_cleanup;
	}

	/* make packed DNSKEY data */
	if(assemble_iterate_hasfirst(assemble_iterate_dnskey, tp->autr->keys)) {
		ubdnskey = ub_packed_rrset_heap_key(
			assemble_iterate_dnskey, tp->autr->keys);
		if(!ubdnskey)
			goto error_cleanup;
		ubdnskey->entry.data = packed_rrset_heap_data(
			assemble_iterate_dnskey, tp->autr->keys);
		if(!ubdnskey->entry.data) {
		error_cleanup:
			autr_rrset_delete(ubds);
			autr_rrset_delete(ubdnskey);
			return 0;
		}
	}

	/* we have prepared the new keys so nothing can go wrong any more.
	 * And we are sure we cannot be left without trustanchor after
	 * any errors. Put in the new keys and remove old ones. */

	/* free the old data */
	autr_rrset_delete(tp->ds_rrset);
	autr_rrset_delete(tp->dnskey_rrset);

	/* assign the data to replace the old */
	tp->ds_rrset = ubds;
	tp->dnskey_rrset = ubdnskey;
	tp->numDS = assemble_iterate_count(assemble_iterate_ds,
		tp->autr->keys);
	tp->numDNSKEY = assemble_iterate_count(assemble_iterate_dnskey,
		tp->autr->keys);
	return 1;
}

/** parse integer */
static unsigned int
parse_int(char* line, int* ret)
{
	char *e;
	unsigned int x = (unsigned int)strtol(line, &e, 10);
	if(line == e) {
		*ret = -1; /* parse error */
		return 0; 
	}
	*ret = 1; /* matched */
	return x;
}

/** parse id sequence for anchor */
static struct trust_anchor*
parse_id(struct val_anchors* anchors, char* line)
{
	struct trust_anchor *tp;
	int r;
	uint16_t dclass;
	uint8_t* dname;
	size_t dname_len;
	/* read the owner name */
	char* next = strchr(line, ' ');
	if(!next)
		return NULL;
	next[0] = 0;
	dname = sldns_str2wire_dname(line, &dname_len);
	if(!dname)
		return NULL;

	/* read the class */
	dclass = parse_int(next+1, &r);
	if(r == -1) {
		free(dname);
		return NULL;
	}

	/* find the trust point */
	tp = autr_tp_create(anchors, dname, dname_len, dclass);
	free(dname);
	return tp;
}

/** 
 * Parse variable from trustanchor header 
 * @param line: to parse
 * @param anchors: the anchor is added to this, if "id:" is seen.
 * @param anchor: the anchor as result value or previously returned anchor
 * 	value to read the variable lines into.
 * @return: 0 no match, -1 failed syntax error, +1 success line read.
 * 	+2 revoked trust anchor file.
 */
static int
parse_var_line(char* line, struct val_anchors* anchors, 
	struct trust_anchor** anchor)
{
	struct trust_anchor* tp = *anchor;
	int r = 0;
	if(strncmp(line, ";;id: ", 6) == 0) {
		*anchor = parse_id(anchors, line+6);
		if(!*anchor) return -1;
		else return 1;
	} else if(strncmp(line, ";;REVOKED", 9) == 0) {
		if(tp) {
			log_err("REVOKED statement must be at start of file");
			return -1;
		}
		return 2;
	} else if(strncmp(line, ";;last_queried: ", 16) == 0) {
		if(!tp) return -1;
		lock_basic_lock(&tp->lock);
		tp->autr->last_queried = (time_t)parse_int(line+16, &r);
		lock_basic_unlock(&tp->lock);
	} else if(strncmp(line, ";;last_success: ", 16) == 0) {
		if(!tp) return -1;
		lock_basic_lock(&tp->lock);
		tp->autr->last_success = (time_t)parse_int(line+16, &r);
		lock_basic_unlock(&tp->lock);
	} else if(strncmp(line, ";;next_probe_time: ", 19) == 0) {
		if(!tp) return -1;
		lock_basic_lock(&anchors->lock);
		lock_basic_lock(&tp->lock);
		(void)rbtree_delete(&anchors->autr->probe, tp);
		tp->autr->next_probe_time = (time_t)parse_int(line+19, &r);
		(void)rbtree_insert(&anchors->autr->probe, &tp->autr->pnode);
		lock_basic_unlock(&tp->lock);
		lock_basic_unlock(&anchors->lock);
	} else if(strncmp(line, ";;query_failed: ", 16) == 0) {
		if(!tp) return -1;
		lock_basic_lock(&tp->lock);
		tp->autr->query_failed = (uint8_t)parse_int(line+16, &r);
		lock_basic_unlock(&tp->lock);
	} else if(strncmp(line, ";;query_interval: ", 18) == 0) {
		if(!tp) return -1;
		lock_basic_lock(&tp->lock);
		tp->autr->query_interval = (time_t)parse_int(line+18, &r);
		lock_basic_unlock(&tp->lock);
	} else if(strncmp(line, ";;retry_time: ", 14) == 0) {
		if(!tp) return -1;
		lock_basic_lock(&tp->lock);
		tp->autr->retry_time = (time_t)parse_int(line+14, &r);
		lock_basic_unlock(&tp->lock);
	}
	return r;
}

/** handle origin lines */
static int
handle_origin(char* line, uint8_t** origin, size_t* origin_len)
{
	size_t len = 0;
	while(isspace((unsigned char)*line))
		line++;
	if(strncmp(line, "$ORIGIN", 7) != 0)
		return 0;
	free(*origin);
	line += 7;
	while(isspace((unsigned char)*line))
		line++;
	*origin = sldns_str2wire_dname(line, &len);
	*origin_len = len;
	if(!*origin)
		log_warn("malloc failure or parse error in $ORIGIN");
	return 1;
}

/** Read one line and put multiline RRs onto one line string */
static int
read_multiline(char* buf, size_t len, FILE* in, int* linenr)
{
	char* pos = buf;
	size_t left = len;
	int depth = 0;
	buf[len-1] = 0;
	while(left > 0 && fgets(pos, (int)left, in) != NULL) {
		size_t i, poslen = strlen(pos);
		(*linenr)++;

		/* check what the new depth is after the line */
		/* this routine cannot handle braces inside quotes,
		   say for TXT records, but this routine only has to read keys */
		for(i=0; i<poslen; i++) {
			if(pos[i] == '(') {
				depth++;
			} else if(pos[i] == ')') {
				if(depth == 0) {
					log_err("mismatch: too many ')'");
					return -1;
				}
				depth--;
			} else if(pos[i] == ';') {
				break;
			}
		}

		/* normal oneline or last line: keeps newline and comments */
		if(depth == 0) {
			return 1;
		}

		/* more lines expected, snip off comments and newline */
		if(poslen>0) 
			pos[poslen-1] = 0; /* strip newline */
		if(strchr(pos, ';')) 
			strchr(pos, ';')[0] = 0; /* strip comments */

		/* move to paste other lines behind this one */
		poslen = strlen(pos);
		pos += poslen;
		left -= poslen;
		/* the newline is changed into a space */
		if(left <= 2 /* space and eos */) {
			log_err("line too long");
			return -1;
		}
		pos[0] = ' ';
		pos[1] = 0;
		pos += 1;
		left -= 1;
	}
	if(depth != 0) {
		log_err("mismatch: too many '('");
		return -1;
	}
	if(pos != buf)
		return 1;
	return 0;
}

int autr_read_file(struct val_anchors* anchors, const char* nm)
{
        /* the file descriptor */
        FILE* fd;
        /* keep track of line numbers */
        int line_nr = 0;
        /* single line */
        char line[10240];
	/* trust point being read */
	struct trust_anchor *tp = NULL, *tp2;
	int r;
	/* for $ORIGIN parsing */
	uint8_t *origin=NULL, *prev=NULL;
	size_t origin_len=0, prev_len=0;

        if (!(fd = fopen(nm, "r"))) {
                log_err("unable to open %s for reading: %s", 
			nm, strerror(errno));
                return 0;
        }
        verbose(VERB_ALGO, "reading autotrust anchor file %s", nm);
        while ( (r=read_multiline(line, sizeof(line), fd, &line_nr)) != 0) {
		if(r == -1 || (r = parse_var_line(line, anchors, &tp)) == -1) {
			log_err("could not parse auto-trust-anchor-file "
				"%s line %d", nm, line_nr);
			fclose(fd);
			free(origin);
			free(prev);
			return 0;
		} else if(r == 1) {
			continue;
		} else if(r == 2) {
			log_warn("trust anchor %s has been revoked", nm);
			fclose(fd);
			free(origin);
			free(prev);
			return 1;
		}
        	if (!str_contains_data(line, ';'))
                	continue; /* empty lines allowed */
 		if(handle_origin(line, &origin, &origin_len))
			continue;
		r = 0;
                if(!(tp2=load_trustanchor(anchors, line, nm, origin,
			origin_len, &prev, &prev_len, &r))) {
			if(!r) log_err("failed to load trust anchor from %s "
				"at line %i, skipping", nm, line_nr);
                        /* try to do the rest */
			continue;
                }
		if(tp && tp != tp2) {
			log_err("file %s has mismatching data inside: "
				"the file may only contain keys for one name, "
				"remove keys for other domain names", nm);
        		fclose(fd);
			free(origin);
			free(prev);
			return 0;
		}
		tp = tp2;
        }
        fclose(fd);
	free(origin);
	free(prev);
	if(!tp) {
		log_err("failed to read %s", nm);
		return 0;
	}

	/* now assemble the data into DNSKEY and DS packed rrsets */
	lock_basic_lock(&tp->lock);
	if(!autr_assemble(tp)) {
		lock_basic_unlock(&tp->lock);
		log_err("malloc failure assembling %s", nm);
		return 0;
	}
	lock_basic_unlock(&tp->lock);
	return 1;
}

/** string for a trustanchor state */
static const char*
trustanchor_state2str(autr_state_type s)
{
        switch (s) {
                case AUTR_STATE_START:       return "  START  ";
                case AUTR_STATE_ADDPEND:     return " ADDPEND ";
                case AUTR_STATE_VALID:       return "  VALID  ";
                case AUTR_STATE_MISSING:     return " MISSING ";
                case AUTR_STATE_REVOKED:     return " REVOKED ";
                case AUTR_STATE_REMOVED:     return " REMOVED ";
        }
        return " UNKNOWN ";
}

/** print ID to file */
static int
print_id(FILE* out, char* fname, uint8_t* nm, size_t nmlen, uint16_t dclass)
{
	char* s = sldns_wire2str_dname(nm, nmlen);
	if(!s) {
		log_err("malloc failure in write to %s", fname);
		return 0;
	}
	if(fprintf(out, ";;id: %s %d\n", s, (int)dclass) < 0) {
		log_err("could not write to %s: %s", fname, strerror(errno));
		free(s);
		return 0;
	}
	free(s);
	return 1;
}

static int
autr_write_contents(FILE* out, char* fn, struct trust_anchor* tp)
{
	char tmi[32];
	struct autr_ta* ta;
	char* str;

	/* write pretty header */
	if(fprintf(out, "; autotrust trust anchor file\n") < 0) {
		log_err("could not write to %s: %s", fn, strerror(errno));
		return 0;
	}
	if(tp->autr->revoked) {
		if(fprintf(out, ";;REVOKED\n") < 0 ||
		   fprintf(out, "; The zone has all keys revoked, and is\n"
			"; considered as if it has no trust anchors.\n"
			"; the remainder of the file is the last probe.\n"
			"; to restart the trust anchor, overwrite this file.\n"
			"; with one containing valid DNSKEYs or DSes.\n") < 0) {
		   log_err("could not write to %s: %s", fn, strerror(errno));
		   return 0;
		}
	}
	if(!print_id(out, fn, tp->name, tp->namelen, tp->dclass)) {
		return 0;
	}
	if(fprintf(out, ";;last_queried: %u ;;%s", 
		(unsigned int)tp->autr->last_queried, 
		ctime_r(&(tp->autr->last_queried), tmi)) < 0 ||
	   fprintf(out, ";;last_success: %u ;;%s", 
		(unsigned int)tp->autr->last_success,
		ctime_r(&(tp->autr->last_success), tmi)) < 0 ||
	   fprintf(out, ";;next_probe_time: %u ;;%s", 
		(unsigned int)tp->autr->next_probe_time,
		ctime_r(&(tp->autr->next_probe_time), tmi)) < 0 ||
	   fprintf(out, ";;query_failed: %d\n", (int)tp->autr->query_failed)<0
	   || fprintf(out, ";;query_interval: %d\n", 
	   (int)tp->autr->query_interval) < 0 ||
	   fprintf(out, ";;retry_time: %d\n", (int)tp->autr->retry_time) < 0) {
		log_err("could not write to %s: %s", fn, strerror(errno));
		return 0;
	}

	/* write anchors */
	for(ta=tp->autr->keys; ta; ta=ta->next) {
		/* by default do not store START and REMOVED keys */
		if(ta->s == AUTR_STATE_START)
			continue;
		if(ta->s == AUTR_STATE_REMOVED)
			continue;
		/* only store keys */
		if(sldns_wirerr_get_type(ta->rr, ta->rr_len, ta->dname_len)
			!= LDNS_RR_TYPE_DNSKEY)
			continue;
		str = sldns_wire2str_rr(ta->rr, ta->rr_len);
		if(!str || !str[0]) {
			free(str);
			log_err("malloc failure writing %s", fn);
			return 0;
		}
		str[strlen(str)-1] = 0; /* remove newline */
		if(fprintf(out, "%s ;;state=%d [%s] ;;count=%d "
			";;lastchange=%u ;;%s", str, (int)ta->s, 
			trustanchor_state2str(ta->s), (int)ta->pending_count,
			(unsigned int)ta->last_change, 
			ctime_r(&(ta->last_change), tmi)) < 0) {
		   log_err("could not write to %s: %s", fn, strerror(errno));
		   free(str);
		   return 0;
		}
		free(str);
	}
	return 1;
}

void autr_write_file(struct module_env* env, struct trust_anchor* tp)
{
	FILE* out;
	char* fname = tp->autr->file;
	char tempf[2048];
	log_assert(tp->autr);
	if(!env) {
		log_err("autr_write_file: Module environment is NULL.");
		return;
	}
	/* unique name with pid number and thread number */
	snprintf(tempf, sizeof(tempf), "%s.%d-%d", fname, (int)getpid(),
		env->worker?*(int*)env->worker:0);
	verbose(VERB_ALGO, "autotrust: write to disk: %s", tempf);
	out = fopen(tempf, "w");
	if(!out) {
		fatal_exit("could not open autotrust file for writing, %s: %s",
			tempf, strerror(errno));
		return;
	}
	if(!autr_write_contents(out, tempf, tp)) {
		/* failed to write contents (completely) */
		fclose(out);
		unlink(tempf);
		fatal_exit("could not completely write: %s", fname);
		return;
	}
	if(fflush(out) != 0)
		log_err("could not fflush(%s): %s", fname, strerror(errno));
#ifdef HAVE_FSYNC
	if(fsync(fileno(out)) != 0)
		log_err("could not fsync(%s): %s", fname, strerror(errno));
#else
	FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(out)));
#endif
	if(fclose(out) != 0) {
		fatal_exit("could not complete write: %s: %s",
			fname, strerror(errno));
		unlink(tempf);
		return;
	}
	/* success; overwrite actual file */
	verbose(VERB_ALGO, "autotrust: replaced %s", fname);
#ifdef UB_ON_WINDOWS
	(void)unlink(fname); /* windows does not replace file with rename() */
#endif
	if(rename(tempf, fname) < 0) {
		fatal_exit("rename(%s to %s): %s", tempf, fname, strerror(errno));
	}
}

/** 
 * Verify if dnskey works for trust point 
 * @param env: environment (with time) for verification
 * @param ve: validator environment (with options) for verification.
 * @param tp: trust point to verify with
 * @param rrset: DNSKEY rrset to verify.
 * @param qstate: qstate with region.
 * @return false on failure, true if verification successful.
 */
static int
verify_dnskey(struct module_env* env, struct val_env* ve,
        struct trust_anchor* tp, struct ub_packed_rrset_key* rrset,
	struct module_qstate* qstate)
{
	char* reason = NULL;
	uint8_t sigalg[ALGO_NEEDS_MAX+1];
	int downprot = env->cfg->harden_algo_downgrade;
	enum sec_status sec = val_verify_DNSKEY_with_TA(env, ve, rrset,
		tp->ds_rrset, tp->dnskey_rrset, downprot?sigalg:NULL, &reason,
		qstate);
	/* sigalg is ignored, it returns algorithms signalled to exist, but
	 * in 5011 there are no other rrsets to check.  if downprot is
	 * enabled, then it checks that the DNSKEY is signed with all
	 * algorithms available in the trust store. */
	verbose(VERB_ALGO, "autotrust: validate DNSKEY with anchor: %s",
		sec_status_to_string(sec));
	return sec == sec_status_secure;
}

static int32_t
rrsig_get_expiry(uint8_t* d, size_t len)
{
	/* rrsig: 2(rdlen), 2(type) 1(alg) 1(v) 4(origttl), then 4(expi), (4)incep) */
	if(len < 2+8+4)
		return 0;
	return sldns_read_uint32(d+2+8);
}

/** Find minimum expiration interval from signatures */
static time_t
min_expiry(struct module_env* env, struct packed_rrset_data* dd)
{
	size_t i;
	int32_t t, r = 15 * 24 * 3600; /* 15 days max */
	for(i=dd->count; i<dd->count+dd->rrsig_count; i++) {
		t = rrsig_get_expiry(dd->rr_data[i], dd->rr_len[i]);
		if((int32_t)t - (int32_t)*env->now > 0) {
			t -= (int32_t)*env->now;
			if(t < r)
				r = t;
		}
	}
	return (time_t)r;
}

/** Is rr self-signed revoked key */
static int
rr_is_selfsigned_revoked(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key* dnskey_rrset, size_t i,
	struct module_qstate* qstate)
{
	enum sec_status sec;
	char* reason = NULL;
	verbose(VERB_ALGO, "seen REVOKE flag, check self-signed, rr %d",
		(int)i);
	/* no algorithm downgrade protection necessary, if it is selfsigned
	 * revoked it can be removed. */
	sec = dnskey_verify_rrset(env, ve, dnskey_rrset, dnskey_rrset, i, 
		&reason, LDNS_SECTION_ANSWER, qstate);
	return (sec == sec_status_secure);
}

/** Set fetched value */
static void
seen_trustanchor(struct autr_ta* ta, uint8_t seen)
{
	ta->fetched = seen;
	if(ta->pending_count < 250) /* no numerical overflow, please */
		ta->pending_count++;
}

/** set revoked value */
static void
seen_revoked_trustanchor(struct autr_ta* ta, uint8_t revoked)
{
	ta->revoked = revoked;
}

/** revoke a trust anchor */
static void
revoke_dnskey(struct autr_ta* ta, int off)
{
	uint16_t flags;
	uint8_t* data;
	if(sldns_wirerr_get_type(ta->rr, ta->rr_len, ta->dname_len) !=
		LDNS_RR_TYPE_DNSKEY)
		return;
	if(sldns_wirerr_get_rdatalen(ta->rr, ta->rr_len, ta->dname_len) < 2)
		return;
	data = sldns_wirerr_get_rdata(ta->rr, ta->rr_len, ta->dname_len);
	flags = sldns_read_uint16(data);
	if (off && (flags&LDNS_KEY_REVOKE_KEY))
		flags ^= LDNS_KEY_REVOKE_KEY; /* flip */
	else
		flags |= LDNS_KEY_REVOKE_KEY;
	sldns_write_uint16(data, flags);
}

/** Compare two RRs skipping the REVOKED bit. Pass rdata(no len) */
static int
dnskey_compare_skip_revbit(uint8_t* a, size_t a_len, uint8_t* b, size_t b_len)
{
	size_t i;
	if(a_len != b_len)
		return -1;
	/* compare RRs RDATA byte for byte. */
	for(i = 0; i < a_len; i++)
	{
		uint8_t rdf1, rdf2;
		rdf1 = a[i];
		rdf2 = b[i];
		if(i==1) {
			/* this is the second part of the flags field */
			rdf1 |= LDNS_KEY_REVOKE_KEY;
			rdf2 |= LDNS_KEY_REVOKE_KEY;
		}
		if (rdf1 < rdf2)	return -1;
		else if (rdf1 > rdf2)	return 1;
        }
	return 0;
}


/** compare trust anchor with rdata, 0 if equal. Pass rdata(no len) */
static int
ta_compare(struct autr_ta* a, uint16_t t, uint8_t* b, size_t b_len)
{
	if(!a) return -1;
	else if(!b) return -1;
	else if(sldns_wirerr_get_type(a->rr, a->rr_len, a->dname_len) != t)
		return (int)sldns_wirerr_get_type(a->rr, a->rr_len,
			a->dname_len) - (int)t;
	else if(t == LDNS_RR_TYPE_DNSKEY) {
		return dnskey_compare_skip_revbit(
			sldns_wirerr_get_rdata(a->rr, a->rr_len, a->dname_len),
			sldns_wirerr_get_rdatalen(a->rr, a->rr_len,
			a->dname_len), b, b_len);
	}
	else if(t == LDNS_RR_TYPE_DS) {
		if(sldns_wirerr_get_rdatalen(a->rr, a->rr_len, a->dname_len) !=
			b_len)
			return -1;
		return memcmp(sldns_wirerr_get_rdata(a->rr,
			a->rr_len, a->dname_len), b, b_len);
	}
	return -1;
}

/** 
 * Find key
 * @param tp: to search in
 * @param t: rr type of the rdata.
 * @param rdata: to look for  (no rdatalen in it)
 * @param rdata_len: length of rdata
 * @param result: returns NULL or the ta key looked for.
 * @return false on malloc failure during search. if true examine result.
 */
static int
find_key(struct trust_anchor* tp, uint16_t t, uint8_t* rdata, size_t rdata_len,
	struct autr_ta** result)
{
	struct autr_ta* ta;
	if(!tp || !rdata) {
		*result = NULL;
		return 0;
	}
	for(ta=tp->autr->keys; ta; ta=ta->next) {
		if(ta_compare(ta, t, rdata, rdata_len) == 0) {
			*result = ta;
			return 1;
		}
	}
	*result = NULL;
	return 1;
}

/** add key and clone RR and tp already locked. rdata without rdlen. */
static struct autr_ta*
add_key(struct trust_anchor* tp, uint32_t ttl, uint8_t* rdata, size_t rdata_len)
{
	struct autr_ta* ta;
	uint8_t* rr;
	size_t rr_len, dname_len;
	uint16_t rrtype = htons(LDNS_RR_TYPE_DNSKEY);
	uint16_t rrclass = htons(LDNS_RR_CLASS_IN);
	uint16_t rdlen = htons(rdata_len);
	dname_len = tp->namelen;
	ttl = htonl(ttl);
	rr_len = dname_len + 10 /* type,class,ttl,rdatalen */ + rdata_len;
	rr = (uint8_t*)malloc(rr_len);
	if(!rr) return NULL;
	memmove(rr, tp->name, tp->namelen);
	memmove(rr+dname_len, &rrtype, 2);
	memmove(rr+dname_len+2, &rrclass, 2);
	memmove(rr+dname_len+4, &ttl, 4);
	memmove(rr+dname_len+8, &rdlen, 2);
	memmove(rr+dname_len+10, rdata, rdata_len);
	ta = autr_ta_create(rr, rr_len, dname_len);
	if(!ta) {
		/* rr freed in autr_ta_create */
		return NULL;
	}
	/* link in, tp already locked */
	ta->next = tp->autr->keys;
	tp->autr->keys = ta;
	return ta;
}

/** get TTL from DNSKEY rrset */
static time_t
key_ttl(struct ub_packed_rrset_key* k)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)k->entry.data;
	return d->ttl;
}

/** update the time values for the trustpoint */
static void
set_tp_times(struct trust_anchor* tp, time_t rrsig_exp_interval, 
	time_t origttl, int* changed)
{
	time_t x, qi = tp->autr->query_interval, rt = tp->autr->retry_time;
	
	/* x = MIN(15days, ttl/2, expire/2) */
	x = 15 * 24 * 3600;
	if(origttl/2 < x)
		x = origttl/2;
	if(rrsig_exp_interval/2 < x)
		x = rrsig_exp_interval/2;
	/* MAX(1hr, x) */
	if(!autr_permit_small_holddown) {
		if(x < 3600)
			tp->autr->query_interval = 3600;
		else	tp->autr->query_interval = x;
	}	else    tp->autr->query_interval = x;

	/* x= MIN(1day, ttl/10, expire/10) */
	x = 24 * 3600;
	if(origttl/10 < x)
		x = origttl/10;
	if(rrsig_exp_interval/10 < x)
		x = rrsig_exp_interval/10;
	/* MAX(1hr, x) */
	if(!autr_permit_small_holddown) {
		if(x < 3600)
			tp->autr->retry_time = 3600;
		else	tp->autr->retry_time = x;
	}	else    tp->autr->retry_time = x;

	if(qi != tp->autr->query_interval || rt != tp->autr->retry_time) {
		*changed = 1;
		verbose(VERB_ALGO, "orig_ttl is %d", (int)origttl);
		verbose(VERB_ALGO, "rrsig_exp_interval is %d", 
			(int)rrsig_exp_interval);
		verbose(VERB_ALGO, "query_interval: %d, retry_time: %d",
			(int)tp->autr->query_interval, 
			(int)tp->autr->retry_time);
	}
}

/** init events to zero */
static void
init_events(struct trust_anchor* tp)
{
	struct autr_ta* ta;
	for(ta=tp->autr->keys; ta; ta=ta->next) {
		ta->fetched = 0;
	}
}

/** check for revoked keys without trusting any other information */
static void
check_contains_revoked(struct module_env* env, struct val_env* ve,
	struct trust_anchor* tp, struct ub_packed_rrset_key* dnskey_rrset,
	int* changed, struct module_qstate* qstate)
{
	struct packed_rrset_data* dd = (struct packed_rrset_data*)
		dnskey_rrset->entry.data;
	size_t i;
	log_assert(ntohs(dnskey_rrset->rk.type) == LDNS_RR_TYPE_DNSKEY);
	for(i=0; i<dd->count; i++) {
		struct autr_ta* ta = NULL;
		if(!rr_is_dnskey_sep(ntohs(dnskey_rrset->rk.type),
			dd->rr_data[i]+2, dd->rr_len[i]-2) ||
			!rr_is_dnskey_revoked(ntohs(dnskey_rrset->rk.type),
			dd->rr_data[i]+2, dd->rr_len[i]-2))
			continue; /* not a revoked KSK */
		if(!find_key(tp, ntohs(dnskey_rrset->rk.type),
			dd->rr_data[i]+2, dd->rr_len[i]-2, &ta)) {
			log_err("malloc failure");
			continue; /* malloc fail in compare*/
		}
		if(!ta)
			continue; /* key not found */
		if(rr_is_selfsigned_revoked(env, ve, dnskey_rrset, i, qstate)) {
			/* checked if there is an rrsig signed by this key. */
			/* same keytag, but stored can be revoked already, so 
			 * compare keytags, with +0 or +128(REVOKE flag) */
			log_assert(dnskey_calc_keytag(dnskey_rrset, i)-128 ==
				sldns_calc_keytag_raw(sldns_wirerr_get_rdata(
				ta->rr, ta->rr_len, ta->dname_len),
				sldns_wirerr_get_rdatalen(ta->rr, ta->rr_len,
				ta->dname_len)) ||
				dnskey_calc_keytag(dnskey_rrset, i) ==
				sldns_calc_keytag_raw(sldns_wirerr_get_rdata(
				ta->rr, ta->rr_len, ta->dname_len),
				sldns_wirerr_get_rdatalen(ta->rr, ta->rr_len,
				ta->dname_len))); /* checks conversion*/
			verbose_key(ta, VERB_ALGO, "is self-signed revoked");
			if(!ta->revoked) 
				*changed = 1;
			seen_revoked_trustanchor(ta, 1);
			do_revoked(env, ta, changed);
		}
	}
}

/** See if a DNSKEY is verified by one of the DSes */
static int
key_matches_a_ds(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key* dnskey_rrset, size_t key_idx,
	struct ub_packed_rrset_key* ds_rrset)
{
	struct packed_rrset_data* dd = (struct packed_rrset_data*)
	                ds_rrset->entry.data;
	size_t ds_idx, num = dd->count;
	int d = val_favorite_ds_algo(ds_rrset);
	char* reason = "";
	for(ds_idx=0; ds_idx<num; ds_idx++) {
		if(!ds_digest_algo_is_supported(ds_rrset, ds_idx) ||
			!ds_key_algo_is_supported(ds_rrset, ds_idx) ||
			ds_get_digest_algo(ds_rrset, ds_idx) != d)
			continue;
		if(ds_get_key_algo(ds_rrset, ds_idx)
		   != dnskey_get_algo(dnskey_rrset, key_idx)
		   || dnskey_calc_keytag(dnskey_rrset, key_idx)
		   != ds_get_keytag(ds_rrset, ds_idx)) {
			continue;
		}
		if(!ds_digest_match_dnskey(env, dnskey_rrset, key_idx,
			ds_rrset, ds_idx)) {
			verbose(VERB_ALGO, "DS match attempt failed");
			continue;
		}
		/* match of hash is sufficient for bootstrap of trust point */
		(void)reason;
		(void)ve;
		return 1;
		/* no need to check RRSIG, DS hash already matched with source
		if(dnskey_verify_rrset(env, ve, dnskey_rrset, 
			dnskey_rrset, key_idx, &reason) == sec_status_secure) {
			return 1;
		} else {
			verbose(VERB_ALGO, "DS match failed because the key "
				"does not verify the keyset: %s", reason);
		}
		*/
	}
	return 0;
}

/** Set update events */
static int
update_events(struct module_env* env, struct val_env* ve, 
	struct trust_anchor* tp, struct ub_packed_rrset_key* dnskey_rrset, 
	int* changed)
{
	struct packed_rrset_data* dd = (struct packed_rrset_data*)
		dnskey_rrset->entry.data;
	size_t i;
	log_assert(ntohs(dnskey_rrset->rk.type) == LDNS_RR_TYPE_DNSKEY);
	init_events(tp);
	for(i=0; i<dd->count; i++) {
		struct autr_ta* ta = NULL;
		if(!rr_is_dnskey_sep(ntohs(dnskey_rrset->rk.type),
			dd->rr_data[i]+2, dd->rr_len[i]-2))
			continue;
		if(rr_is_dnskey_revoked(ntohs(dnskey_rrset->rk.type),
			dd->rr_data[i]+2, dd->rr_len[i]-2)) {
			/* self-signed revoked keys already detected before,
			 * other revoked keys are not 'added' again */
			continue;
		}
		/* is a key of this type supported?. Note rr_list and
		 * packed_rrset are in the same order. */
		if(!dnskey_algo_is_supported(dnskey_rrset, i)) {
			/* skip unknown algorithm key, it is useless to us */
			log_nametypeclass(VERB_DETAIL, "trust point has "
				"unsupported algorithm at", 
				tp->name, LDNS_RR_TYPE_DNSKEY, tp->dclass);
			continue;
		}

		/* is it new? if revocation bit set, find the unrevoked key */
		if(!find_key(tp, ntohs(dnskey_rrset->rk.type),
			dd->rr_data[i]+2, dd->rr_len[i]-2, &ta)) {
			return 0;
		}
		if(!ta) {
			ta = add_key(tp, (uint32_t)dd->rr_ttl[i],
				dd->rr_data[i]+2, dd->rr_len[i]-2);
			*changed = 1;
			/* first time seen, do we have DSes? if match: VALID */
			if(ta && tp->ds_rrset && key_matches_a_ds(env, ve,
				dnskey_rrset, i, tp->ds_rrset)) {
				verbose_key(ta, VERB_ALGO, "verified by DS");
				ta->s = AUTR_STATE_VALID;
			}
		}
		if(!ta) {
			return 0;
		}
		seen_trustanchor(ta, 1);
		verbose_key(ta, VERB_ALGO, "in DNS response");
	}
	set_tp_times(tp, min_expiry(env, dd), key_ttl(dnskey_rrset), changed);
	return 1;
}

/**
 * Check if the holddown time has already exceeded
 * setting: add-holddown: add holddown timer
 * setting: del-holddown: del holddown timer
 * @param env: environment with current time
 * @param ta: trust anchor to check for.
 * @param holddown: the timer value
 * @return number of seconds the holddown has passed.
 */
static time_t
check_holddown(struct module_env* env, struct autr_ta* ta,
	unsigned int holddown)
{
        time_t elapsed;
	if(*env->now < ta->last_change) {
		log_warn("time goes backwards. delaying key holddown");
		return 0;
	}
	elapsed = *env->now - ta->last_change;
        if (elapsed > (time_t)holddown) {
                return elapsed-(time_t)holddown;
        }
	verbose_key(ta, VERB_ALGO, "holddown time " ARG_LL "d seconds to go",
		(long long) ((time_t)holddown-elapsed));
        return 0;
}


/** Set last_change to now */
static void
reset_holddown(struct module_env* env, struct autr_ta* ta, int* changed)
{
	ta->last_change = *env->now;
	*changed = 1;
}

/** Set the state for this trust anchor */
static void
set_trustanchor_state(struct module_env* env, struct autr_ta* ta, int* changed,
	autr_state_type s)
{
	verbose_key(ta, VERB_ALGO, "update: %s to %s",
		trustanchor_state2str(ta->s), trustanchor_state2str(s));
	ta->s = s;
	reset_holddown(env, ta, changed);
}


/** Event: NewKey */
static void
do_newkey(struct module_env* env, struct autr_ta* anchor, int* c)
{
	if (anchor->s == AUTR_STATE_START)
		set_trustanchor_state(env, anchor, c, AUTR_STATE_ADDPEND);
}

/** Event: AddTime */
static void
do_addtime(struct module_env* env, struct autr_ta* anchor, int* c)
{
	/* This not according to RFC, this is 30 days, but the RFC demands 
	 * MAX(30days, TTL expire time of first DNSKEY set with this key),
	 * The value may be too small if a very large TTL was used. */
	time_t exceeded = check_holddown(env, anchor, env->cfg->add_holddown);
	if (exceeded && anchor->s == AUTR_STATE_ADDPEND) {
		verbose_key(anchor, VERB_ALGO, "add-holddown time exceeded "
			ARG_LL "d seconds ago, and pending-count %d",
			(long long)exceeded, anchor->pending_count);
		if(anchor->pending_count >= MIN_PENDINGCOUNT) {
			set_trustanchor_state(env, anchor, c, AUTR_STATE_VALID);
			anchor->pending_count = 0;
			return;
		}
		verbose_key(anchor, VERB_ALGO, "add-holddown time sanity check "
			"failed (pending count: %d)", anchor->pending_count);
	}
}

/** Event: RemTime */
static void
do_remtime(struct module_env* env, struct autr_ta* anchor, int* c)
{
	time_t exceeded = check_holddown(env, anchor, env->cfg->del_holddown);
	if(exceeded && anchor->s == AUTR_STATE_REVOKED) {
		verbose_key(anchor, VERB_ALGO, "del-holddown time exceeded "
			ARG_LL "d seconds ago", (long long)exceeded);
		set_trustanchor_state(env, anchor, c, AUTR_STATE_REMOVED);
	}
}

/** Event: KeyRem */
static void
do_keyrem(struct module_env* env, struct autr_ta* anchor, int* c)
{
	if(anchor->s == AUTR_STATE_ADDPEND) {
		set_trustanchor_state(env, anchor, c, AUTR_STATE_START);
		anchor->pending_count = 0;
	} else if(anchor->s == AUTR_STATE_VALID)
		set_trustanchor_state(env, anchor, c, AUTR_STATE_MISSING);
}

/** Event: KeyPres */
static void
do_keypres(struct module_env* env, struct autr_ta* anchor, int* c)
{
	if(anchor->s == AUTR_STATE_MISSING)
		set_trustanchor_state(env, anchor, c, AUTR_STATE_VALID);
}

/* Event: Revoked */
static void
do_revoked(struct module_env* env, struct autr_ta* anchor, int* c)
{
	if(anchor->s == AUTR_STATE_VALID || anchor->s == AUTR_STATE_MISSING) {
                set_trustanchor_state(env, anchor, c, AUTR_STATE_REVOKED);
		verbose_key(anchor, VERB_ALGO, "old id, prior to revocation");
                revoke_dnskey(anchor, 0);
		verbose_key(anchor, VERB_ALGO, "new id, after revocation");
	}
}

/** Do statestable transition matrix for anchor */
static void
anchor_state_update(struct module_env* env, struct autr_ta* anchor, int* c)
{
	log_assert(anchor);
	switch(anchor->s) {
	/* START */
	case AUTR_STATE_START:
		/* NewKey: ADDPEND */
		if (anchor->fetched)
			do_newkey(env, anchor, c);
		break;
	/* ADDPEND */
	case AUTR_STATE_ADDPEND:
		/* KeyRem: START */
		if (!anchor->fetched)
			do_keyrem(env, anchor, c);
		/* AddTime: VALID */
		else	do_addtime(env, anchor, c);
		break;
	/* VALID */
	case AUTR_STATE_VALID:
		/* RevBit: REVOKED */
		if (anchor->revoked)
			do_revoked(env, anchor, c);
		/* KeyRem: MISSING */
		else if (!anchor->fetched)
			do_keyrem(env, anchor, c);
		else if(!anchor->last_change) {
			verbose_key(anchor, VERB_ALGO, "first seen");
			reset_holddown(env, anchor, c);
		}
		break;
	/* MISSING */
	case AUTR_STATE_MISSING:
		/* RevBit: REVOKED */
		if (anchor->revoked)
			do_revoked(env, anchor, c);
		/* KeyPres */
		else if (anchor->fetched)
			do_keypres(env, anchor, c);
		break;
	/* REVOKED */
	case AUTR_STATE_REVOKED:
		if (anchor->fetched)
			reset_holddown(env, anchor, c);
		/* RemTime: REMOVED */
		else	do_remtime(env, anchor, c);
		break;
	/* REMOVED */
	case AUTR_STATE_REMOVED:
	default:
		break;
	}
}

/** if ZSK init then trust KSKs */
static int
init_zsk_to_ksk(struct module_env* env, struct trust_anchor* tp, int* changed)
{
	/* search for VALID ZSKs */
	struct autr_ta* anchor;
	int validzsk = 0;
	int validksk = 0;
	for(anchor = tp->autr->keys; anchor; anchor = anchor->next) {
		/* last_change test makes sure it was manually configured */
		if(sldns_wirerr_get_type(anchor->rr, anchor->rr_len,
			anchor->dname_len) == LDNS_RR_TYPE_DNSKEY &&
			anchor->last_change == 0 && 
			!ta_is_dnskey_sep(anchor) &&
			anchor->s == AUTR_STATE_VALID)
                        validzsk++;
	}
	if(validzsk == 0)
		return 0;
	for(anchor = tp->autr->keys; anchor; anchor = anchor->next) {
                if (ta_is_dnskey_sep(anchor) && 
			anchor->s == AUTR_STATE_ADDPEND) {
			verbose_key(anchor, VERB_ALGO, "trust KSK from "
				"ZSK(config)");
			set_trustanchor_state(env, anchor, changed, 
				AUTR_STATE_VALID);
			validksk++;
		}
	}
	return validksk;
}

/** Remove missing trustanchors so the list does not grow forever */
static void
remove_missing_trustanchors(struct module_env* env, struct trust_anchor* tp,
	int* changed)
{
	struct autr_ta* anchor;
	time_t exceeded;
	int valid = 0;
	/* see if we have anchors that are valid */
	for(anchor = tp->autr->keys; anchor; anchor = anchor->next) {
		/* Only do KSKs */
                if (!ta_is_dnskey_sep(anchor))
                        continue;
                if (anchor->s == AUTR_STATE_VALID)
                        valid++;
	}
	/* if there are no SEP Valid anchors, see if we started out with
	 * a ZSK (last-change=0) anchor, which is VALID and there are KSKs
	 * now that can be made valid.  Do this immediately because there
	 * is no guarantee that the ZSKs get announced long enough.  Usually
	 * this is immediately after init with a ZSK trusted, unless the domain
	 * was not advertising any KSKs at all.  In which case we perfectly
	 * track the zero number of KSKs. */
	if(valid == 0) {
		valid = init_zsk_to_ksk(env, tp, changed);
		if(valid == 0)
			return;
	}
	
	for(anchor = tp->autr->keys; anchor; anchor = anchor->next) {
		/* ignore ZSKs if newly added */
		if(anchor->s == AUTR_STATE_START)
			continue;
		/* remove ZSKs if a KSK is present */
                if (!ta_is_dnskey_sep(anchor)) {
			if(valid > 0) {
				verbose_key(anchor, VERB_ALGO, "remove ZSK "
					"[%d key(s) VALID]", valid);
				set_trustanchor_state(env, anchor, changed, 
					AUTR_STATE_REMOVED);
			}
                        continue;
		}
                /* Only do MISSING keys */
                if (anchor->s != AUTR_STATE_MISSING)
                        continue;
		if(env->cfg->keep_missing == 0)
			continue; /* keep forever */

		exceeded = check_holddown(env, anchor, env->cfg->keep_missing);
		/* If keep_missing has exceeded and we still have more than 
		 * one valid KSK: remove missing trust anchor */
                if (exceeded && valid > 0) {
			verbose_key(anchor, VERB_ALGO, "keep-missing time "
				"exceeded " ARG_LL "d seconds ago, [%d key(s) VALID]",
				(long long)exceeded, valid);
			set_trustanchor_state(env, anchor, changed, 
				AUTR_STATE_REMOVED);
		}
	}
}

/** Do the statetable from RFC5011 transition matrix */
static int
do_statetable(struct module_env* env, struct trust_anchor* tp, int* changed)
{
	struct autr_ta* anchor;
	for(anchor = tp->autr->keys; anchor; anchor = anchor->next) {
		/* Only do KSKs */
		if(!ta_is_dnskey_sep(anchor))
			continue;
		anchor_state_update(env, anchor, changed);
	}
	remove_missing_trustanchors(env, tp, changed);
	return 1;
}

/** See if time alone makes ADDPEND to VALID transition */
static void
autr_holddown_exceed(struct module_env* env, struct trust_anchor* tp, int* c)
{
	struct autr_ta* anchor;
	for(anchor = tp->autr->keys; anchor; anchor = anchor->next) {
		if(ta_is_dnskey_sep(anchor) && 
			anchor->s == AUTR_STATE_ADDPEND)
			do_addtime(env, anchor, c);
	}
}

/** cleanup key list */
static void
autr_cleanup_keys(struct trust_anchor* tp)
{
	struct autr_ta* p, **prevp;
	prevp = &tp->autr->keys;
	p = tp->autr->keys;
	while(p) {
		/* do we want to remove this key? */
		if(p->s == AUTR_STATE_START || p->s == AUTR_STATE_REMOVED ||
			sldns_wirerr_get_type(p->rr, p->rr_len, p->dname_len)
			!= LDNS_RR_TYPE_DNSKEY) {
			struct autr_ta* np = p->next;
			/* remove */
			free(p->rr);
			free(p);
			/* snip and go to next item */
			*prevp = np;
			p = np;
			continue;
		}
		/* remove pending counts if no longer pending */
		if(p->s != AUTR_STATE_ADDPEND)
			p->pending_count = 0;
		prevp = &p->next;
		p = p->next;
	}
}

/** calculate next probe time */
static time_t
calc_next_probe(struct module_env* env, time_t wait)
{
	/* make it random, 90-100% */
	time_t rnd, rest;
	if(!autr_permit_small_holddown) {
		if(wait < 3600)
			wait = 3600;
	} else {
		if(wait == 0) wait = 1;
	}
	rnd = wait/10;
	rest = wait-rnd;
	rnd = (time_t)ub_random_max(env->rnd, (long int)rnd);
	return (time_t)(*env->now + rest + rnd);
}

/** what is first probe time (anchors must be locked) */
static time_t
wait_probe_time(struct val_anchors* anchors)
{
	rbnode_type* t = rbtree_first(&anchors->autr->probe);
	if(t != RBTREE_NULL) 
		return ((struct trust_anchor*)t->key)->autr->next_probe_time;
	return 0;
}

/** reset worker timer */
static void
reset_worker_timer(struct module_env* env)
{
	struct timeval tv;
#ifndef S_SPLINT_S
	time_t next = (time_t)wait_probe_time(env->anchors);
	/* in case this is libunbound, no timer */
	if(!env->probe_timer)
		return;
	if(next > *env->now)
		tv.tv_sec = (time_t)(next - *env->now);
	else	tv.tv_sec = 0;
#endif
	tv.tv_usec = 0;
	comm_timer_set(env->probe_timer, &tv);
	verbose(VERB_ALGO, "scheduled next probe in " ARG_LL "d sec", (long long)tv.tv_sec);
}

/** set next probe for trust anchor */
static int
set_next_probe(struct module_env* env, struct trust_anchor* tp,
	struct ub_packed_rrset_key* dnskey_rrset)
{
	struct trust_anchor key, *tp2;
	time_t mold, mnew;
	/* use memory allocated in rrset for temporary name storage */
	key.node.key = &key;
	key.name = dnskey_rrset->rk.dname;
	key.namelen = dnskey_rrset->rk.dname_len;
	key.namelabs = dname_count_labels(key.name);
	key.dclass = tp->dclass;
	lock_basic_unlock(&tp->lock);

	/* fetch tp again and lock anchors, so that we can modify the trees */
	lock_basic_lock(&env->anchors->lock);
	tp2 = (struct trust_anchor*)rbtree_search(env->anchors->tree, &key);
	if(!tp2) {
		verbose(VERB_ALGO, "trustpoint was deleted in set_next_probe");
		lock_basic_unlock(&env->anchors->lock);
		return 0;
	}
	log_assert(tp == tp2);
	lock_basic_lock(&tp->lock);

	/* schedule */
	mold = wait_probe_time(env->anchors);
	(void)rbtree_delete(&env->anchors->autr->probe, tp);
	tp->autr->next_probe_time = calc_next_probe(env, 
		tp->autr->query_interval);
	(void)rbtree_insert(&env->anchors->autr->probe, &tp->autr->pnode);
	mnew = wait_probe_time(env->anchors);

	lock_basic_unlock(&env->anchors->lock);
	verbose(VERB_ALGO, "next probe set in %d seconds", 
		(int)tp->autr->next_probe_time - (int)*env->now);
	if(mold != mnew) {
		reset_worker_timer(env);
	}
	return 1;
}

/** Revoke and Delete a trust point */
static void
autr_tp_remove(struct module_env* env, struct trust_anchor* tp,
	struct ub_packed_rrset_key* dnskey_rrset)
{
	struct trust_anchor* del_tp;
	struct trust_anchor key;
	struct autr_point_data pd;
	time_t mold, mnew;

	log_nametypeclass(VERB_OPS, "trust point was revoked",
		tp->name, LDNS_RR_TYPE_DNSKEY, tp->dclass);
	tp->autr->revoked = 1;

	/* use space allocated for dnskey_rrset to save name of anchor */
	memset(&key, 0, sizeof(key));
	memset(&pd, 0, sizeof(pd));
	key.autr = &pd;
	key.node.key = &key;
	pd.pnode.key = &key;
	pd.next_probe_time = tp->autr->next_probe_time;
	key.name = dnskey_rrset->rk.dname;
	key.namelen = tp->namelen;
	key.namelabs = tp->namelabs;
	key.dclass = tp->dclass;

	/* unlock */
	lock_basic_unlock(&tp->lock);

	/* take from tree. It could be deleted by someone else,hence (void). */
	lock_basic_lock(&env->anchors->lock);
	del_tp = (struct trust_anchor*)rbtree_delete(env->anchors->tree, &key);
	mold = wait_probe_time(env->anchors);
	(void)rbtree_delete(&env->anchors->autr->probe, &key);
	mnew = wait_probe_time(env->anchors);
	anchors_init_parents_locked(env->anchors);
	lock_basic_unlock(&env->anchors->lock);

	/* if !del_tp then the trust point is no longer present in the tree,
	 * it was deleted by someone else, who will write the zonefile and
	 * clean up the structure */
	if(del_tp) {
		/* save on disk */
		del_tp->autr->next_probe_time = 0; /* no more probing for it */
		autr_write_file(env, del_tp);

		/* delete */
		autr_point_delete(del_tp);
	}
	if(mold != mnew) {
		reset_worker_timer(env);
	}
}

int autr_process_prime(struct module_env* env, struct val_env* ve,
	struct trust_anchor* tp, struct ub_packed_rrset_key* dnskey_rrset,
	struct module_qstate* qstate)
{
	int changed = 0;
	log_assert(tp && tp->autr);
	/* autotrust update trust anchors */
	/* the tp is locked, and stays locked unless it is deleted */

	/* we could just catch the anchor here while another thread
	 * is busy deleting it. Just unlock and let the other do its job */
	if(tp->autr->revoked) {
		log_nametypeclass(VERB_ALGO, "autotrust not processed, "
			"trust point revoked", tp->name, 
			LDNS_RR_TYPE_DNSKEY, tp->dclass);
		lock_basic_unlock(&tp->lock);
		return 0; /* it is revoked */
	}

	/* query_dnskeys(): */
	tp->autr->last_queried = *env->now;

	log_nametypeclass(VERB_ALGO, "autotrust process for",
		tp->name, LDNS_RR_TYPE_DNSKEY, tp->dclass);
	/* see if time alone makes some keys valid */
	autr_holddown_exceed(env, tp, &changed);
	if(changed) {
		verbose(VERB_ALGO, "autotrust: morekeys, reassemble");
		if(!autr_assemble(tp)) {
			log_err("malloc failure assembling autotrust keys");
			return 1; /* unchanged */
		}
	}
	/* did we get any data? */
	if(!dnskey_rrset) {
		verbose(VERB_ALGO, "autotrust: no dnskey rrset");
		/* no update of query_failed, because then we would have
		 * to write to disk. But we cannot because we maybe are
		 * still 'initializing' with DS records, that we cannot write
		 * in the full format (which only contains KSKs). */
		return 1; /* trust point exists */
	}
	/* check for revoked keys to remove immediately */
	check_contains_revoked(env, ve, tp, dnskey_rrset, &changed, qstate);
	if(changed) {
		verbose(VERB_ALGO, "autotrust: revokedkeys, reassemble");
		if(!autr_assemble(tp)) {
			log_err("malloc failure assembling autotrust keys");
			return 1; /* unchanged */
		}
		if(!tp->ds_rrset && !tp->dnskey_rrset) {
			/* no more keys, all are revoked */
			/* this is a success for this probe attempt */
			tp->autr->last_success = *env->now;
			autr_tp_remove(env, tp, dnskey_rrset);
			return 0; /* trust point removed */
		}
	}
	/* verify the dnskey rrset and see if it is valid. */
	if(!verify_dnskey(env, ve, tp, dnskey_rrset, qstate)) {
		verbose(VERB_ALGO, "autotrust: dnskey did not verify.");
		/* only increase failure count if this is not the first prime,
		 * this means there was a previous successful probe */
		if(tp->autr->last_success) {
			tp->autr->query_failed += 1;
			autr_write_file(env, tp);
		}
		return 1; /* trust point exists */
	}

	tp->autr->last_success = *env->now;
	tp->autr->query_failed = 0;

	/* Add new trust anchors to the data structure
	 * - note which trust anchors are seen this probe.
	 * Set trustpoint query_interval and retry_time.
	 * - find minimum rrsig expiration interval
	 */
	if(!update_events(env, ve, tp, dnskey_rrset, &changed)) {
		log_err("malloc failure in autotrust update_events. "
			"trust point unchanged.");
		return 1; /* trust point unchanged, so exists */
	}

	/* - for every SEP key do the 5011 statetable.
	 * - remove missing trustanchors (if veryold and we have new anchors).
	 */
	if(!do_statetable(env, tp, &changed)) {
		log_err("malloc failure in autotrust do_statetable. "
			"trust point unchanged.");
		return 1; /* trust point unchanged, so exists */
	}

	autr_cleanup_keys(tp);
	if(!set_next_probe(env, tp, dnskey_rrset))
		return 0; /* trust point does not exist */
	autr_write_file(env, tp);
	if(changed) {
		verbose(VERB_ALGO, "autotrust: changed, reassemble");
		if(!autr_assemble(tp)) {
			log_err("malloc failure assembling autotrust keys");
			return 1; /* unchanged */
		}
		if(!tp->ds_rrset && !tp->dnskey_rrset) {
			/* no more keys, all are revoked */
			autr_tp_remove(env, tp, dnskey_rrset);
			return 0; /* trust point removed */
		}
	} else verbose(VERB_ALGO, "autotrust: no changes");
	
	return 1; /* trust point exists */
}

/** debug print a trust anchor key */
static void 
autr_debug_print_ta(struct autr_ta* ta)
{
	char buf[32];
	char* str = sldns_wire2str_rr(ta->rr, ta->rr_len);
	if(!str) {
		log_info("out of memory in debug_print_ta");
		return;
	}
	if(str && str[0]) str[strlen(str)-1]=0; /* remove newline */
	ctime_r(&ta->last_change, buf);
	if(buf[0]) buf[strlen(buf)-1]=0; /* remove newline */
	log_info("[%s] %s ;;state:%d ;;pending_count:%d%s%s last:%s",
		trustanchor_state2str(ta->s), str, ta->s, ta->pending_count,
		ta->fetched?" fetched":"", ta->revoked?" revoked":"", buf);
	free(str);
}

/** debug print a trust point */
static void 
autr_debug_print_tp(struct trust_anchor* tp)
{
	struct autr_ta* ta;
	char buf[257];
	if(!tp->autr)
		return;
	dname_str(tp->name, buf);
	log_info("trust point %s : %d", buf, (int)tp->dclass);
	log_info("assembled %d DS and %d DNSKEYs", 
		(int)tp->numDS, (int)tp->numDNSKEY);
	if(tp->ds_rrset) {
		log_packed_rrset(0, "DS:", tp->ds_rrset);
	}
	if(tp->dnskey_rrset) {
		log_packed_rrset(0, "DNSKEY:", tp->dnskey_rrset);
	}
	log_info("file %s", tp->autr->file);
	ctime_r(&tp->autr->last_queried, buf);
	if(buf[0]) buf[strlen(buf)-1]=0; /* remove newline */
	log_info("last_queried: %u %s", (unsigned)tp->autr->last_queried, buf);
	ctime_r(&tp->autr->last_success, buf);
	if(buf[0]) buf[strlen(buf)-1]=0; /* remove newline */
	log_info("last_success: %u %s", (unsigned)tp->autr->last_success, buf);
	ctime_r(&tp->autr->next_probe_time, buf);
	if(buf[0]) buf[strlen(buf)-1]=0; /* remove newline */
	log_info("next_probe_time: %u %s", (unsigned)tp->autr->next_probe_time,
		buf);
	log_info("query_interval: %u", (unsigned)tp->autr->query_interval);
	log_info("retry_time: %u", (unsigned)tp->autr->retry_time);
	log_info("query_failed: %u", (unsigned)tp->autr->query_failed);
		
	for(ta=tp->autr->keys; ta; ta=ta->next) {
		autr_debug_print_ta(ta);
	}
}

void 
autr_debug_print(struct val_anchors* anchors)
{
	struct trust_anchor* tp;
	lock_basic_lock(&anchors->lock);
	RBTREE_FOR(tp, struct trust_anchor*, anchors->tree) {
		lock_basic_lock(&tp->lock);
		autr_debug_print_tp(tp);
		lock_basic_unlock(&tp->lock);
	}
	lock_basic_unlock(&anchors->lock);
}

void probe_answer_cb(void* arg, int ATTR_UNUSED(rcode), 
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(sec),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	/* retry was set before the query was done,
	 * re-querytime is set when query succeeded, but that may not
	 * have reset this timer because the query could have been
	 * handled by another thread. In that case, this callback would
	 * get called after the original timeout is done. 
	 * By not resetting the timer, it may probe more often, but not
	 * less often.
	 * Unless the new lookup resulted in smaller TTLs and thus smaller
	 * timeout values. In that case one old TTL could be mistakenly done.
	 */
	struct module_env* env = (struct module_env*)arg;
	verbose(VERB_ALGO, "autotrust probe answer cb");
	reset_worker_timer(env);
}

/** probe a trust anchor DNSKEY and unlocks tp */
static void
probe_anchor(struct module_env* env, struct trust_anchor* tp)
{
	struct query_info qinfo;
	uint16_t qflags = BIT_RD;
	struct edns_data edns;
	sldns_buffer* buf = env->scratch_buffer;
	qinfo.qname = regional_alloc_init(env->scratch, tp->name, tp->namelen);
	if(!qinfo.qname) {
		log_err("out of memory making 5011 probe");
		return;
	}
	qinfo.qname_len = tp->namelen;
	qinfo.qtype = LDNS_RR_TYPE_DNSKEY;
	qinfo.qclass = tp->dclass;
	qinfo.local_alias = NULL;
	log_query_info(VERB_ALGO, "autotrust probe", &qinfo);
	verbose(VERB_ALGO, "retry probe set in %d seconds", 
		(int)tp->autr->next_probe_time - (int)*env->now);
	edns.edns_present = 1;
	edns.ext_rcode = 0;
	edns.edns_version = 0;
	edns.bits = EDNS_DO;
	edns.opt_list = NULL;
	if(sldns_buffer_capacity(buf) < 65535)
		edns.udp_size = (uint16_t)sldns_buffer_capacity(buf);
	else	edns.udp_size = 65535;

	/* can't hold the lock while mesh_run is processing */
	lock_basic_unlock(&tp->lock);

	/* delete the DNSKEY from rrset and key cache so an active probe
	 * is done. First the rrset so another thread does not use it
	 * to recreate the key entry in a race condition. */
	rrset_cache_remove(env->rrset_cache, qinfo.qname, qinfo.qname_len,
		qinfo.qtype, qinfo.qclass, 0);
	key_cache_remove(env->key_cache, qinfo.qname, qinfo.qname_len, 
		qinfo.qclass);

	if(!mesh_new_callback(env->mesh, &qinfo, qflags, &edns, buf, 0, 
		&probe_answer_cb, env)) {
		log_err("out of memory making 5011 probe");
	}
}

/** fetch first to-probe trust-anchor and lock it and set retrytime */
static struct trust_anchor*
todo_probe(struct module_env* env, time_t* next)
{
	struct trust_anchor* tp;
	rbnode_type* el;
	/* get first one */
	lock_basic_lock(&env->anchors->lock);
	if( (el=rbtree_first(&env->anchors->autr->probe)) == RBTREE_NULL) {
		/* in case of revoked anchors */
		lock_basic_unlock(&env->anchors->lock);
		/* signal that there are no anchors to probe */
		*next = 0;
		return NULL;
	}
	tp = (struct trust_anchor*)el->key;
	lock_basic_lock(&tp->lock);

	/* is it eligible? */
	if((time_t)tp->autr->next_probe_time > *env->now) {
		/* no more to probe */
		*next = (time_t)tp->autr->next_probe_time - *env->now;
		lock_basic_unlock(&tp->lock);
		lock_basic_unlock(&env->anchors->lock);
		return NULL;
	}

	/* reset its next probe time */
	(void)rbtree_delete(&env->anchors->autr->probe, tp);
	tp->autr->next_probe_time = calc_next_probe(env, tp->autr->retry_time);
	(void)rbtree_insert(&env->anchors->autr->probe, &tp->autr->pnode);
	lock_basic_unlock(&env->anchors->lock);

	return tp;
}

time_t 
autr_probe_timer(struct module_env* env)
{
	struct trust_anchor* tp;
	time_t next_probe = 3600;
	int num = 0;
	if(autr_permit_small_holddown) next_probe = 1;
	verbose(VERB_ALGO, "autotrust probe timer callback");
	/* while there are still anchors to probe */
	while( (tp = todo_probe(env, &next_probe)) ) {
		/* make a probe for this anchor */
		probe_anchor(env, tp);
		num++;
	}
	regional_free_all(env->scratch);
	if(next_probe == 0)
		return 0; /* no trust points to probe */
	verbose(VERB_ALGO, "autotrust probe timer %d callbacks done", num);
	return next_probe;
}
