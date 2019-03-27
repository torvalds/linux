/*
 * Copyright (c) 1995 - 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <config.h>

#include "roken.h"
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif
#ifdef HAVE_RESOLV_H
#include <resolv.h>
#endif
#ifdef HAVE_DNS_H
#include <dns.h>
#endif
#include "resolve.h"

#include <assert.h>

#ifdef _AIX /* AIX have broken res_nsearch() in 5.1 (5.0 also ?) */
#undef HAVE_RES_NSEARCH
#endif

#define DECL(X) {#X, rk_ns_t_##X}

static struct stot{
    const char *name;
    int type;
}stot[] = {
    DECL(a),
    DECL(aaaa),
    DECL(ns),
    DECL(cname),
    DECL(soa),
    DECL(ptr),
    DECL(mx),
    DECL(txt),
    DECL(afsdb),
    DECL(sig),
    DECL(key),
    DECL(srv),
    DECL(naptr),
    DECL(sshfp),
    DECL(ds),
    {NULL, 	0}
};

int _resolve_debug = 0;

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_dns_string_to_type(const char *name)
{
    struct stot *p = stot;
    for(p = stot; p->name; p++)
	if(strcasecmp(name, p->name) == 0)
	    return p->type;
    return -1;
}

ROKEN_LIB_FUNCTION const char * ROKEN_LIB_CALL
rk_dns_type_to_string(int type)
{
    struct stot *p = stot;
    for(p = stot; p->name; p++)
	if(type == p->type)
	    return p->name;
    return NULL;
}

#if ((defined(HAVE_RES_SEARCH) || defined(HAVE_RES_NSEARCH)) && defined(HAVE_DN_EXPAND)) || defined(HAVE_WINDNS)

static void
dns_free_rr(struct rk_resource_record *rr)
{
    if(rr->domain)
	free(rr->domain);
    if(rr->u.data)
	free(rr->u.data);
    free(rr);
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_dns_free_data(struct rk_dns_reply *r)
{
    struct rk_resource_record *rr;
    if(r->q.domain)
	free(r->q.domain);
    for(rr = r->head; rr;){
	struct rk_resource_record *tmp = rr;
	rr = rr->next;
	dns_free_rr(tmp);
    }
    free (r);
}

#ifndef HAVE_WINDNS

static int
parse_record(const unsigned char *data, const unsigned char *end_data,
	     const unsigned char **pp, struct rk_resource_record **ret_rr)
{
    struct rk_resource_record *rr;
    int type, class, ttl;
    unsigned size;
    int status;
    char host[MAXDNAME];
    const unsigned char *p = *pp;

    *ret_rr = NULL;

    status = dn_expand(data, end_data, p, host, sizeof(host));
    if(status < 0)
	return -1;
    if (p + status + 10 > end_data)
	return -1;

    p += status;
    type = (p[0] << 8) | p[1];
    p += 2;
    class = (p[0] << 8) | p[1];
    p += 2;
    ttl = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    p += 4;
    size = (p[0] << 8) | p[1];
    p += 2;

    if (p + size > end_data)
	return -1;

    rr = calloc(1, sizeof(*rr));
    if(rr == NULL)
	return -1;
    rr->domain = strdup(host);
    if(rr->domain == NULL) {
	dns_free_rr(rr);
	return -1;
    }
    rr->type = type;
    rr->class = class;
    rr->ttl = ttl;
    rr->size = size;
    switch(type){
    case rk_ns_t_ns:
    case rk_ns_t_cname:
    case rk_ns_t_ptr:
	status = dn_expand(data, end_data, p, host, sizeof(host));
	if(status < 0) {
	    dns_free_rr(rr);
	    return -1;
	}
	rr->u.txt = strdup(host);
	if(rr->u.txt == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}
	break;
    case rk_ns_t_mx:
    case rk_ns_t_afsdb:{
	size_t hostlen;

	status = dn_expand(data, end_data, p + 2, host, sizeof(host));
	if(status < 0){
	    dns_free_rr(rr);
	    return -1;
	}
	if ((size_t)status + 2 > size) {
	    dns_free_rr(rr);
	    return -1;
	}

	hostlen = strlen(host);
	rr->u.mx = (struct mx_record*)malloc(sizeof(struct mx_record) +
						hostlen);
	if(rr->u.mx == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}
	rr->u.mx->preference = (p[0] << 8) | p[1];
	strlcpy(rr->u.mx->domain, host, hostlen + 1);
	break;
    }
    case rk_ns_t_srv:{
	size_t hostlen;
	status = dn_expand(data, end_data, p + 6, host, sizeof(host));
	if(status < 0){
	    dns_free_rr(rr);
	    return -1;
	}
	if ((size_t)status + 6 > size) {
	    dns_free_rr(rr);
	    return -1;
	}

	hostlen = strlen(host);
	rr->u.srv =
	    (struct srv_record*)malloc(sizeof(struct srv_record) +
				       hostlen);
	if(rr->u.srv == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}
	rr->u.srv->priority = (p[0] << 8) | p[1];
	rr->u.srv->weight = (p[2] << 8) | p[3];
	rr->u.srv->port = (p[4] << 8) | p[5];
	strlcpy(rr->u.srv->target, host, hostlen + 1);
	break;
    }
    case rk_ns_t_txt:{
	if(size == 0 || size < (unsigned)(*p + 1)) {
	    dns_free_rr(rr);
	    return -1;
	}
	rr->u.txt = (char*)malloc(*p + 1);
	if(rr->u.txt == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}
	strncpy(rr->u.txt, (const char*)(p + 1), *p);
	rr->u.txt[*p] = '\0';
	break;
    }
    case rk_ns_t_key : {
	size_t key_len;

	if (size < 4) {
	    dns_free_rr(rr);
	    return -1;
	}

	key_len = size - 4;
	rr->u.key = malloc (sizeof(*rr->u.key) + key_len - 1);
	if (rr->u.key == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}

	rr->u.key->flags     = (p[0] << 8) | p[1];
	rr->u.key->protocol  = p[2];
	rr->u.key->algorithm = p[3];
	rr->u.key->key_len   = key_len;
	memcpy (rr->u.key->key_data, p + 4, key_len);
	break;
    }
    case rk_ns_t_sig : {
	size_t sig_len, hostlen;

	if(size <= 18) {
	    dns_free_rr(rr);
	    return -1;
	}
	status = dn_expand (data, end_data, p + 18, host, sizeof(host));
	if (status < 0) {
	    dns_free_rr(rr);
	    return -1;
	}
	if ((size_t)status + 18 > size) {
	    dns_free_rr(rr);
	    return -1;
	}

	/* the signer name is placed after the sig_data, to make it
           easy to free this structure; the size calculation below
           includes the zero-termination if the structure itself.
	   don't you just love C?
	*/
	sig_len = size - 18 - status;
	hostlen = strlen(host);
	rr->u.sig = malloc(sizeof(*rr->u.sig)
			      + hostlen + sig_len);
	if (rr->u.sig == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}
	rr->u.sig->type           = (p[0] << 8) | p[1];
	rr->u.sig->algorithm      = p[2];
	rr->u.sig->labels         = p[3];
	rr->u.sig->orig_ttl       = (p[4] << 24) | (p[5] << 16)
	    | (p[6] << 8) | p[7];
	rr->u.sig->sig_expiration = (p[8] << 24) | (p[9] << 16)
	    | (p[10] << 8) | p[11];
	rr->u.sig->sig_inception  = (p[12] << 24) | (p[13] << 16)
	    | (p[14] << 8) | p[15];
	rr->u.sig->key_tag        = (p[16] << 8) | p[17];
	rr->u.sig->sig_len        = sig_len;
	memcpy (rr->u.sig->sig_data, p + 18 + status, sig_len);
	rr->u.sig->signer         = &rr->u.sig->sig_data[sig_len];
	strlcpy(rr->u.sig->signer, host, hostlen + 1);
	break;
    }

    case rk_ns_t_cert : {
	size_t cert_len;

	if (size < 5) {
	    dns_free_rr(rr);
	    return -1;
	}

	cert_len = size - 5;
	rr->u.cert = malloc (sizeof(*rr->u.cert) + cert_len - 1);
	if (rr->u.cert == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}

	rr->u.cert->type      = (p[0] << 8) | p[1];
	rr->u.cert->tag       = (p[2] << 8) | p[3];
	rr->u.cert->algorithm = p[4];
	rr->u.cert->cert_len  = cert_len;
	memcpy (rr->u.cert->cert_data, p + 5, cert_len);
	break;
    }
    case rk_ns_t_sshfp : {
	size_t sshfp_len;

	if (size < 2) {
	    dns_free_rr(rr);
	    return -1;
	}

	sshfp_len = size - 2;

	rr->u.sshfp = malloc (sizeof(*rr->u.sshfp) + sshfp_len - 1);
	if (rr->u.sshfp == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}

	rr->u.sshfp->algorithm = p[0];
	rr->u.sshfp->type      = p[1];
	rr->u.sshfp->sshfp_len  = sshfp_len;
	memcpy (rr->u.sshfp->sshfp_data, p + 2, sshfp_len);
	break;
    }
    case rk_ns_t_ds: {
	size_t digest_len;

	if (size < 4) {
	    dns_free_rr(rr);
	    return -1;
	}

	digest_len = size - 4;

	rr->u.ds = malloc (sizeof(*rr->u.ds) + digest_len - 1);
	if (rr->u.ds == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}

	rr->u.ds->key_tag     = (p[0] << 8) | p[1];
	rr->u.ds->algorithm   = p[2];
	rr->u.ds->digest_type = p[3];
	rr->u.ds->digest_len  = digest_len;
	memcpy (rr->u.ds->digest_data, p + 4, digest_len);
	break;
    }
    default:
	rr->u.data = (unsigned char*)malloc(size);
	if(size != 0 && rr->u.data == NULL) {
	    dns_free_rr(rr);
	    return -1;
	}
	if (size)
	    memcpy(rr->u.data, p, size);
    }
    *pp = p + size;
    *ret_rr = rr;

    return 0;
}

#ifndef TEST_RESOLVE
static
#endif
struct rk_dns_reply*
parse_reply(const unsigned char *data, size_t len)
{
    const unsigned char *p;
    int status;
    size_t i;
    char host[MAXDNAME];
    const unsigned char *end_data = data + len;
    struct rk_dns_reply *r;
    struct rk_resource_record **rr;

    r = calloc(1, sizeof(*r));
    if (r == NULL)
	return NULL;

    p = data;

    r->h.id = (p[0] << 8) | p[1];
    r->h.flags = 0;
    if (p[2] & 0x01)
	r->h.flags |= rk_DNS_HEADER_RESPONSE_FLAG;
    r->h.opcode = (p[2] >> 1) & 0xf;
    if (p[2] & 0x20)
	r->h.flags |= rk_DNS_HEADER_AUTHORITIVE_ANSWER;
    if (p[2] & 0x40)
	r->h.flags |= rk_DNS_HEADER_TRUNCATED_MESSAGE;
    if (p[2] & 0x80)
	r->h.flags |= rk_DNS_HEADER_RECURSION_DESIRED;
    if (p[3] & 0x01)
	r->h.flags |= rk_DNS_HEADER_RECURSION_AVAILABLE;
    if (p[3] & 0x04)
	r->h.flags |= rk_DNS_HEADER_AUTHORITIVE_ANSWER;
    if (p[3] & 0x08)
	r->h.flags |= rk_DNS_HEADER_CHECKING_DISABLED;
    r->h.response_code = (p[3] >> 4) & 0xf;
    r->h.qdcount = (p[4] << 8) | p[5];
    r->h.ancount = (p[6] << 8) | p[7];
    r->h.nscount = (p[8] << 8) | p[9];
    r->h.arcount = (p[10] << 8) | p[11];

    p += 12;

    if(r->h.qdcount != 1) {
	free(r);
	return NULL;
    }
    status = dn_expand(data, end_data, p, host, sizeof(host));
    if(status < 0){
	rk_dns_free_data(r);
	return NULL;
    }
    r->q.domain = strdup(host);
    if(r->q.domain == NULL) {
	rk_dns_free_data(r);
	return NULL;
    }
    if (p + status + 4 > end_data) {
	rk_dns_free_data(r);
	return NULL;
    }
    p += status;
    r->q.type = (p[0] << 8 | p[1]);
    p += 2;
    r->q.class = (p[0] << 8 | p[1]);
    p += 2;

    rr = &r->head;
    for(i = 0; i < r->h.ancount; i++) {
	if(parse_record(data, end_data, &p, rr) != 0) {
	    rk_dns_free_data(r);
	    return NULL;
	}
	rr = &(*rr)->next;
    }
    for(i = 0; i < r->h.nscount; i++) {
	if(parse_record(data, end_data, &p, rr) != 0) {
	    rk_dns_free_data(r);
	    return NULL;
	}
	rr = &(*rr)->next;
    }
    for(i = 0; i < r->h.arcount; i++) {
	if(parse_record(data, end_data, &p, rr) != 0) {
	    rk_dns_free_data(r);
	    return NULL;
	}
	rr = &(*rr)->next;
    }
    *rr = NULL;
    return r;
}

#ifdef HAVE_RES_NSEARCH
#ifdef HAVE_RES_NDESTROY
#define rk_res_free(x) res_ndestroy(x)
#else
#define rk_res_free(x) res_nclose(x)
#endif
#endif

#if defined(HAVE_DNS_SEARCH)
#define resolve_search(h,n,c,t,r,l) \
    	((int)dns_search(h,n,c,t,r,l,(struct sockaddr *)&from,&fromsize))
#define resolve_free_handle(h) dns_free(h)
#elif defined(HAVE_RES_NSEARCH)
#define resolve_search(h,n,c,t,r,l) res_nsearch(h,n,c,t,r,l)
#define resolve_free_handle(h) rk_res_free(h);
#else
#define resolve_search(h,n,c,t,r,l) res_search(n,c,t,r,l)
#define handle 0
#define resolve_free_handle(h)
#endif


static struct rk_dns_reply *
dns_lookup_int(const char *domain, int rr_class, int rr_type)
{
    struct rk_dns_reply *r;
    void *reply = NULL;
    int size, len;
#if defined(HAVE_DNS_SEARCH)
    struct sockaddr_storage from;
    uint32_t fromsize = sizeof(from);
    dns_handle_t handle;

    handle = dns_open(NULL);
    if (handle == NULL)
	return NULL;
#elif defined(HAVE_RES_NSEARCH)
    struct __res_state state;
    struct __res_state *handle = &state;

    memset(&state, 0, sizeof(state));
    if(res_ninit(handle))
	return NULL; /* is this the best we can do? */
#endif

    len = 1500;
    while(1) {
	if (reply) {
	    free(reply);
	    reply = NULL;
	}
	if (_resolve_debug) {
#if defined(HAVE_DNS_SEARCH)
	    dns_set_debug(handle, 1);
#elif defined(HAVE_RES_NSEARCH)
	    state.options |= RES_DEBUG;
#endif
	    fprintf(stderr, "dns_lookup(%s, %d, %s), buffer size %d\n", domain,
		    rr_class, rk_dns_type_to_string(rr_type), len);
	}
	reply = malloc(len);
	if (reply == NULL) {
	    resolve_free_handle(handle);
	    return NULL;
	}

	size = resolve_search(handle, domain, rr_class, rr_type, reply, len);

	if (_resolve_debug) {
	    fprintf(stderr, "dns_lookup(%s, %d, %s) --> %d\n",
		    domain, rr_class, rk_dns_type_to_string(rr_type), size);
	}
	if (size > len) {
	    /* resolver thinks it know better, go for it */
	    len = size;
	} else if (size > 0) {
	    /* got a good reply */
	    break;
	} else if (size <= 0 && len < rk_DNS_MAX_PACKET_SIZE) {
	    len *= 2;
	    if (len > rk_DNS_MAX_PACKET_SIZE)
		len = rk_DNS_MAX_PACKET_SIZE;
	} else {
	    /* the end, leave */
	    resolve_free_handle(handle);
	    free(reply);
	    return NULL;
	}
    }

    len = min(len, size);
    r = parse_reply(reply, len);
    free(reply);

    resolve_free_handle(handle);

    return r;
}

ROKEN_LIB_FUNCTION struct rk_dns_reply * ROKEN_LIB_CALL
rk_dns_lookup(const char *domain, const char *type_name)
{
    int type;

    type = rk_dns_string_to_type(type_name);
    if(type == -1) {
	if(_resolve_debug)
	    fprintf(stderr, "dns_lookup: unknown resource type: `%s'\n",
		    type_name);
	return NULL;
    }
    return dns_lookup_int(domain, rk_ns_c_in, type);
}

#endif	/* !HAVE_WINDNS */

static int
compare_srv(const void *a, const void *b)
{
    const struct rk_resource_record *const* aa = a, *const* bb = b;

    if((*aa)->u.srv->priority == (*bb)->u.srv->priority)
	return ((*aa)->u.srv->weight - (*bb)->u.srv->weight);
    return ((*aa)->u.srv->priority - (*bb)->u.srv->priority);
}

/* try to rearrange the srv-records by the algorithm in RFC2782 */
ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_dns_srv_order(struct rk_dns_reply *r)
{
    struct rk_resource_record **srvs, **ss, **headp;
    struct rk_resource_record *rr;
    int num_srv = 0;

    rk_random_init();

    for(rr = r->head; rr; rr = rr->next)
	if(rr->type == rk_ns_t_srv)
	    num_srv++;

    if(num_srv == 0)
	return;

    srvs = malloc(num_srv * sizeof(*srvs));
    if(srvs == NULL)
	return; /* XXX not much to do here */

    /* unlink all srv-records from the linked list and put them in
       a vector */
    for(ss = srvs, headp = &r->head; *headp; )
	if((*headp)->type == rk_ns_t_srv) {
	    *ss = *headp;
	    *headp = (*headp)->next;
	    (*ss)->next = NULL;
	    ss++;
	} else
	    headp = &(*headp)->next;

    /* sort them by priority and weight */
    qsort(srvs, num_srv, sizeof(*srvs), compare_srv);

    headp = &r->head;

    for(ss = srvs; ss < srvs + num_srv; ) {
	int sum, rnd, count;
	struct rk_resource_record **ee, **tt;
	/* find the last record with the same priority and count the
           sum of all weights */
	for(sum = 0, tt = ss; tt < srvs + num_srv; tt++) {
	    assert(*tt != NULL);
	    if((*tt)->u.srv->priority != (*ss)->u.srv->priority)
		break;
	    sum += (*tt)->u.srv->weight;
	}
	ee = tt;
	/* ss is now the first record of this priority and ee is the
           first of the next */
	while(ss < ee) {
	    rnd = rk_random() % (sum + 1);
	    for(count = 0, tt = ss; ; tt++) {
		if(*tt == NULL)
		    continue;
		count += (*tt)->u.srv->weight;
		if(count >= rnd)
		    break;
	    }

	    assert(tt < ee);

	    /* insert the selected record at the tail (of the head) of
               the list */
	    (*tt)->next = *headp;
	    *headp = *tt;
	    headp = &(*tt)->next;
	    sum -= (*tt)->u.srv->weight;
	    *tt = NULL;
	    while(ss < ee && *ss == NULL)
		ss++;
	}
    }

    free(srvs);
    return;
}

#ifdef HAVE_WINDNS

#include <WinDNS.h>

static struct rk_resource_record *
parse_dns_record(PDNS_RECORD pRec)
{
    struct rk_resource_record * rr;

    if (pRec == NULL)
	return NULL;

    rr = calloc(1, sizeof(*rr));

    rr->domain = strdup(pRec->pName);
    rr->type = pRec->wType;
    rr->class = 0;
    rr->ttl = pRec->dwTtl;
    rr->size = 0;

    switch (rr->type) {
    case rk_ns_t_ns:
    case rk_ns_t_cname:
    case rk_ns_t_ptr:
	rr->u.txt = strdup(pRec->Data.NS.pNameHost);
	if(rr->u.txt == NULL) {
	    dns_free_rr(rr);
	    return NULL;
	}
	break;

    case rk_ns_t_mx:
    case rk_ns_t_afsdb:{
	size_t hostlen = strnlen(pRec->Data.MX.pNameExchange, DNS_MAX_NAME_LENGTH);

	rr->u.mx = (struct mx_record *)malloc(sizeof(struct mx_record) +
					      hostlen);
	if (rr->u.mx == NULL) {
	    dns_free_rr(rr);
	    return NULL;
	}

	strcpy_s(rr->u.mx->domain, hostlen + 1, pRec->Data.MX.pNameExchange);
	rr->u.mx->preference = pRec->Data.MX.wPreference;
	break;
    }

    case rk_ns_t_srv:{
	size_t hostlen = strnlen(pRec->Data.SRV.pNameTarget, DNS_MAX_NAME_LENGTH);

	rr->u.srv =
	    (struct srv_record*)malloc(sizeof(struct srv_record) +
				       hostlen);
	if(rr->u.srv == NULL) {
	    dns_free_rr(rr);
	    return NULL;
	}

	rr->u.srv->priority = pRec->Data.SRV.wPriority;
	rr->u.srv->weight = pRec->Data.SRV.wWeight;
	rr->u.srv->port = pRec->Data.SRV.wPort;
	strcpy_s(rr->u.srv->target, hostlen + 1, pRec->Data.SRV.pNameTarget);

	break;
    }

    case rk_ns_t_txt:{
	size_t len;

	if (pRec->Data.TXT.dwStringCount == 0) {
	    rr->u.txt = strdup("");
	    break;
	}

	len = strnlen(pRec->Data.TXT.pStringArray[0], DNS_MAX_TEXT_STRING_LENGTH);

	rr->u.txt = (char *)malloc(len + 1);
	strcpy_s(rr->u.txt, len + 1, pRec->Data.TXT.pStringArray[0]);

	break;
    }

    case rk_ns_t_key : {
	size_t key_len;

	if (pRec->wDataLength < 4) {
	    dns_free_rr(rr);
	    return NULL;
	}

	key_len = pRec->wDataLength - 4;
	rr->u.key = malloc (sizeof(*rr->u.key) + key_len - 1);
	if (rr->u.key == NULL) {
	    dns_free_rr(rr);
	    return NULL;
	}

	rr->u.key->flags     = pRec->Data.KEY.wFlags;
	rr->u.key->protocol  = pRec->Data.KEY.chProtocol;
	rr->u.key->algorithm = pRec->Data.KEY.chAlgorithm;
	rr->u.key->key_len   = key_len;
	memcpy_s (rr->u.key->key_data, key_len,
		  pRec->Data.KEY.Key, key_len);
	break;
    }

    case rk_ns_t_sig : {
	size_t sig_len, hostlen;

	if(pRec->wDataLength <= 18) {
	    dns_free_rr(rr);
	    return NULL;
	}

	sig_len = pRec->wDataLength;

	hostlen = strnlen(pRec->Data.SIG.pNameSigner, DNS_MAX_NAME_LENGTH);

	rr->u.sig = malloc(sizeof(*rr->u.sig)
			      + hostlen + sig_len);
	if (rr->u.sig == NULL) {
	    dns_free_rr(rr);
	    return NULL;
	}
	rr->u.sig->type           = pRec->Data.SIG.wTypeCovered;
	rr->u.sig->algorithm      = pRec->Data.SIG.chAlgorithm;
	rr->u.sig->labels         = pRec->Data.SIG.chLabelCount;
	rr->u.sig->orig_ttl       = pRec->Data.SIG.dwOriginalTtl;
	rr->u.sig->sig_expiration = pRec->Data.SIG.dwExpiration;
	rr->u.sig->sig_inception  = pRec->Data.SIG.dwTimeSigned;
	rr->u.sig->key_tag        = pRec->Data.SIG.wKeyTag;
	rr->u.sig->sig_len        = sig_len;
	memcpy_s (rr->u.sig->sig_data, sig_len,
		  pRec->Data.SIG.Signature, sig_len);
	rr->u.sig->signer         = &rr->u.sig->sig_data[sig_len];
	strcpy_s(rr->u.sig->signer, hostlen + 1, pRec->Data.SIG.pNameSigner);
	break;
    }

#ifdef DNS_TYPE_DS
    case rk_ns_t_ds: {
	rr->u.ds = malloc (sizeof(*rr->u.ds) + pRec->Data.DS.wDigestLength - 1);
	if (rr->u.ds == NULL) {
	    dns_free_rr(rr);
	    return NULL;
	}

	rr->u.ds->key_tag     = pRec->Data.DS.wKeyTag;
	rr->u.ds->algorithm   = pRec->Data.DS.chAlgorithm;
	rr->u.ds->digest_type = pRec->Data.DS.chDigestType;
	rr->u.ds->digest_len  = pRec->Data.DS.wDigestLength;
	memcpy_s (rr->u.ds->digest_data, pRec->Data.DS.wDigestLength,
		  pRec->Data.DS.Digest, pRec->Data.DS.wDigestLength);
	break;
    }
#endif

    default:
	dns_free_rr(rr);
	return NULL;
    }

    rr->next = parse_dns_record(pRec->pNext);
    return rr;
}

ROKEN_LIB_FUNCTION struct rk_dns_reply * ROKEN_LIB_CALL
rk_dns_lookup(const char *domain, const char *type_name)
{
    DNS_STATUS status;
    int type;
    PDNS_RECORD pRec = NULL;
    struct rk_dns_reply * r = NULL;

    __try {

	type = rk_dns_string_to_type(type_name);
	if(type == -1) {
	    if(_resolve_debug)
		fprintf(stderr, "dns_lookup: unknown resource type: `%s'\n",
			type_name);
	    return NULL;
	}

	status = DnsQuery_UTF8(domain, type, DNS_QUERY_STANDARD, NULL,
			       &pRec, NULL);
	if (status != ERROR_SUCCESS)
	    return NULL;

	r = calloc(1, sizeof(*r));
	r->q.domain = strdup(domain);
	r->q.type = type;
	r->q.class = 0;

	r->head = parse_dns_record(pRec);

	if (r->head == NULL) {
	    rk_dns_free_data(r);
	    return NULL;
	} else {
	    return r;
	}

    } __finally {

	if (pRec)
	    DnsRecordListFree(pRec, DnsFreeRecordList);

    }
}
#endif	/* HAVE_WINDNS */

#else /* NOT defined(HAVE_RES_SEARCH) && defined(HAVE_DN_EXPAND) */

ROKEN_LIB_FUNCTION struct rk_dns_reply * ROKEN_LIB_CALL
rk_dns_lookup(const char *domain, const char *type_name)
{
    return NULL;
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_dns_free_data(struct rk_dns_reply *r)
{
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rk_dns_srv_order(struct rk_dns_reply *r)
{
}

#endif
