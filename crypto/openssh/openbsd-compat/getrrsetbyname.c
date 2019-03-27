/* $OpenBSD: getrrsetbyname.c,v 1.11 2007/10/11 18:36:41 jakob Exp $ */

/*
 * Copyright (c) 2001 Jakob Schlyter. All rights reserved.
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
 * Portions Copyright (c) 1999-2001 Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * INTERNET SOFTWARE CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* OPENBSD ORIGINAL: lib/libc/net/getrrsetbyname.c */

#include "includes.h"

#if !defined (HAVE_GETRRSETBYNAME) && !defined (HAVE_LDNS)

#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "getrrsetbyname.h"

#if defined(HAVE_DECL_H_ERRNO) && !HAVE_DECL_H_ERRNO
extern int h_errno;
#endif

/* We don't need multithread support here */
#ifdef _THREAD_PRIVATE
# undef _THREAD_PRIVATE
#endif
#define _THREAD_PRIVATE(a,b,c) (c)

#ifndef HAVE__RES_EXTERN
struct __res_state _res;
#endif

/* Necessary functions and macros */

/*
 * Inline versions of get/put short/long.  Pointer is advanced.
 *
 * These macros demonstrate the property of C whereby it can be
 * portable or it can be elegant but rarely both.
 */

#ifndef INT32SZ
# define INT32SZ	4
#endif
#ifndef INT16SZ
# define INT16SZ	2
#endif

#ifndef GETSHORT
#define GETSHORT(s, cp) { \
	register u_char *t_cp = (u_char *)(cp); \
	(s) = ((u_int16_t)t_cp[0] << 8) \
	    | ((u_int16_t)t_cp[1]) \
	    ; \
	(cp) += INT16SZ; \
}
#endif

#ifndef GETLONG
#define GETLONG(l, cp) { \
	register u_char *t_cp = (u_char *)(cp); \
	(l) = ((u_int32_t)t_cp[0] << 24) \
	    | ((u_int32_t)t_cp[1] << 16) \
	    | ((u_int32_t)t_cp[2] << 8) \
	    | ((u_int32_t)t_cp[3]) \
	    ; \
	(cp) += INT32SZ; \
}
#endif

/*
 * Routines to insert/extract short/long's.
 */

#ifndef HAVE__GETSHORT
static u_int16_t
_getshort(msgp)
	register const u_char *msgp;
{
	register u_int16_t u;

	GETSHORT(u, msgp);
	return (u);
}
#elif defined(HAVE_DECL__GETSHORT) && (HAVE_DECL__GETSHORT == 0)
u_int16_t _getshort(register const u_char *);
#endif

#ifndef HAVE__GETLONG
static u_int32_t
_getlong(msgp)
	register const u_char *msgp;
{
	register u_int32_t u;

	GETLONG(u, msgp);
	return (u);
}
#elif defined(HAVE_DECL__GETLONG) && (HAVE_DECL__GETLONG == 0)
u_int32_t _getlong(register const u_char *);
#endif

/* ************** */

#define ANSWER_BUFFER_SIZE 0xffff

struct dns_query {
	char			*name;
	u_int16_t		type;
	u_int16_t		class;
	struct dns_query	*next;
};

struct dns_rr {
	char			*name;
	u_int16_t		type;
	u_int16_t		class;
	u_int16_t		ttl;
	u_int16_t		size;
	void			*rdata;
	struct dns_rr		*next;
};

struct dns_response {
	HEADER			header;
	struct dns_query	*query;
	struct dns_rr		*answer;
	struct dns_rr		*authority;
	struct dns_rr		*additional;
};

static struct dns_response *parse_dns_response(const u_char *, int);
static struct dns_query *parse_dns_qsection(const u_char *, int,
    const u_char **, int);
static struct dns_rr *parse_dns_rrsection(const u_char *, int, const u_char **,
    int);

static void free_dns_query(struct dns_query *);
static void free_dns_rr(struct dns_rr *);
static void free_dns_response(struct dns_response *);

static int count_dns_rr(struct dns_rr *, u_int16_t, u_int16_t);

int
getrrsetbyname(const char *hostname, unsigned int rdclass,
    unsigned int rdtype, unsigned int flags,
    struct rrsetinfo **res)
{
	struct __res_state *_resp = _THREAD_PRIVATE(_res, _res, &_res);
	int result;
	struct rrsetinfo *rrset = NULL;
	struct dns_response *response = NULL;
	struct dns_rr *rr;
	struct rdatainfo *rdata;
	int length;
	unsigned int index_ans, index_sig;
	u_char answer[ANSWER_BUFFER_SIZE];

	/* check for invalid class and type */
	if (rdclass > 0xffff || rdtype > 0xffff) {
		result = ERRSET_INVAL;
		goto fail;
	}

	/* don't allow queries of class or type ANY */
	if (rdclass == 0xff || rdtype == 0xff) {
		result = ERRSET_INVAL;
		goto fail;
	}

	/* don't allow flags yet, unimplemented */
	if (flags) {
		result = ERRSET_INVAL;
		goto fail;
	}

	/* initialize resolver */
	if ((_resp->options & RES_INIT) == 0 && res_init() == -1) {
		result = ERRSET_FAIL;
		goto fail;
	}

#ifdef DEBUG
	_resp->options |= RES_DEBUG;
#endif /* DEBUG */

#ifdef RES_USE_DNSSEC
	/* turn on DNSSEC if EDNS0 is configured */
	if (_resp->options & RES_USE_EDNS0)
		_resp->options |= RES_USE_DNSSEC;
#endif /* RES_USE_DNSEC */

	/* make query */
	length = res_query(hostname, (signed int) rdclass, (signed int) rdtype,
	    answer, sizeof(answer));
	if (length < 0) {
		switch(h_errno) {
		case HOST_NOT_FOUND:
			result = ERRSET_NONAME;
			goto fail;
		case NO_DATA:
			result = ERRSET_NODATA;
			goto fail;
		default:
			result = ERRSET_FAIL;
			goto fail;
		}
	}

	/* parse result */
	response = parse_dns_response(answer, length);
	if (response == NULL) {
		result = ERRSET_FAIL;
		goto fail;
	}

	if (response->header.qdcount != 1) {
		result = ERRSET_FAIL;
		goto fail;
	}

	/* initialize rrset */
	rrset = calloc(1, sizeof(struct rrsetinfo));
	if (rrset == NULL) {
		result = ERRSET_NOMEMORY;
		goto fail;
	}
	rrset->rri_rdclass = response->query->class;
	rrset->rri_rdtype = response->query->type;
	rrset->rri_ttl = response->answer->ttl;
	rrset->rri_nrdatas = response->header.ancount;

#ifdef HAVE_HEADER_AD
	/* check for authenticated data */
	if (response->header.ad == 1)
		rrset->rri_flags |= RRSET_VALIDATED;
#endif

	/* copy name from answer section */
	rrset->rri_name = strdup(response->answer->name);
	if (rrset->rri_name == NULL) {
		result = ERRSET_NOMEMORY;
		goto fail;
	}

	/* count answers */
	rrset->rri_nrdatas = count_dns_rr(response->answer, rrset->rri_rdclass,
	    rrset->rri_rdtype);
	rrset->rri_nsigs = count_dns_rr(response->answer, rrset->rri_rdclass,
	    T_RRSIG);

	/* allocate memory for answers */
	rrset->rri_rdatas = calloc(rrset->rri_nrdatas,
	    sizeof(struct rdatainfo));
	if (rrset->rri_rdatas == NULL) {
		result = ERRSET_NOMEMORY;
		goto fail;
	}

	/* allocate memory for signatures */
	if (rrset->rri_nsigs > 0) {
		rrset->rri_sigs = calloc(rrset->rri_nsigs, sizeof(struct rdatainfo));
		if (rrset->rri_sigs == NULL) {
			result = ERRSET_NOMEMORY;
			goto fail;
		}
	}

	/* copy answers & signatures */
	for (rr = response->answer, index_ans = 0, index_sig = 0;
	    rr; rr = rr->next) {

		rdata = NULL;

		if (rr->class == rrset->rri_rdclass &&
		    rr->type  == rrset->rri_rdtype)
			rdata = &rrset->rri_rdatas[index_ans++];

		if (rr->class == rrset->rri_rdclass &&
		    rr->type  == T_RRSIG)
			rdata = &rrset->rri_sigs[index_sig++];

		if (rdata) {
			rdata->rdi_length = rr->size;
			rdata->rdi_data   = malloc(rr->size);

			if (rdata->rdi_data == NULL) {
				result = ERRSET_NOMEMORY;
				goto fail;
			}
			memcpy(rdata->rdi_data, rr->rdata, rr->size);
		}
	}
	free_dns_response(response);

	*res = rrset;
	return (ERRSET_SUCCESS);

fail:
	if (rrset != NULL)
		freerrset(rrset);
	if (response != NULL)
		free_dns_response(response);
	return (result);
}

void
freerrset(struct rrsetinfo *rrset)
{
	u_int16_t i;

	if (rrset == NULL)
		return;

	if (rrset->rri_rdatas) {
		for (i = 0; i < rrset->rri_nrdatas; i++) {
			if (rrset->rri_rdatas[i].rdi_data == NULL)
				break;
			free(rrset->rri_rdatas[i].rdi_data);
		}
		free(rrset->rri_rdatas);
	}

	if (rrset->rri_sigs) {
		for (i = 0; i < rrset->rri_nsigs; i++) {
			if (rrset->rri_sigs[i].rdi_data == NULL)
				break;
			free(rrset->rri_sigs[i].rdi_data);
		}
		free(rrset->rri_sigs);
	}

	if (rrset->rri_name)
		free(rrset->rri_name);
	free(rrset);
}

/*
 * DNS response parsing routines
 */
static struct dns_response *
parse_dns_response(const u_char *answer, int size)
{
	struct dns_response *resp;
	const u_char *cp;

	/* allocate memory for the response */
	resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return (NULL);

	/* initialize current pointer */
	cp = answer;

	/* copy header */
	memcpy(&resp->header, cp, HFIXEDSZ);
	cp += HFIXEDSZ;

	/* fix header byte order */
	resp->header.qdcount = ntohs(resp->header.qdcount);
	resp->header.ancount = ntohs(resp->header.ancount);
	resp->header.nscount = ntohs(resp->header.nscount);
	resp->header.arcount = ntohs(resp->header.arcount);

	/* there must be at least one query */
	if (resp->header.qdcount < 1) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse query section */
	resp->query = parse_dns_qsection(answer, size, &cp,
	    resp->header.qdcount);
	if (resp->header.qdcount && resp->query == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse answer section */
	resp->answer = parse_dns_rrsection(answer, size, &cp,
	    resp->header.ancount);
	if (resp->header.ancount && resp->answer == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse authority section */
	resp->authority = parse_dns_rrsection(answer, size, &cp,
	    resp->header.nscount);
	if (resp->header.nscount && resp->authority == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse additional section */
	resp->additional = parse_dns_rrsection(answer, size, &cp,
	    resp->header.arcount);
	if (resp->header.arcount && resp->additional == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	return (resp);
}

static struct dns_query *
parse_dns_qsection(const u_char *answer, int size, const u_char **cp, int count)
{
	struct dns_query *head, *curr, *prev;
	int i, length;
	char name[MAXDNAME];

	for (i = 1, head = NULL, prev = NULL; i <= count; i++, prev = curr) {

		/* allocate and initialize struct */
		curr = calloc(1, sizeof(struct dns_query));
		if (curr == NULL) {
			free_dns_query(head);
			return (NULL);
		}
		if (head == NULL)
			head = curr;
		if (prev != NULL)
			prev->next = curr;

		/* name */
		length = dn_expand(answer, answer + size, *cp, name,
		    sizeof(name));
		if (length < 0) {
			free_dns_query(head);
			return (NULL);
		}
		curr->name = strdup(name);
		if (curr->name == NULL) {
			free_dns_query(head);
			return (NULL);
		}
		*cp += length;

		/* type */
		curr->type = _getshort(*cp);
		*cp += INT16SZ;

		/* class */
		curr->class = _getshort(*cp);
		*cp += INT16SZ;
	}

	return (head);
}

static struct dns_rr *
parse_dns_rrsection(const u_char *answer, int size, const u_char **cp,
    int count)
{
	struct dns_rr *head, *curr, *prev;
	int i, length;
	char name[MAXDNAME];

	for (i = 1, head = NULL, prev = NULL; i <= count; i++, prev = curr) {

		/* allocate and initialize struct */
		curr = calloc(1, sizeof(struct dns_rr));
		if (curr == NULL) {
			free_dns_rr(head);
			return (NULL);
		}
		if (head == NULL)
			head = curr;
		if (prev != NULL)
			prev->next = curr;

		/* name */
		length = dn_expand(answer, answer + size, *cp, name,
		    sizeof(name));
		if (length < 0) {
			free_dns_rr(head);
			return (NULL);
		}
		curr->name = strdup(name);
		if (curr->name == NULL) {
			free_dns_rr(head);
			return (NULL);
		}
		*cp += length;

		/* type */
		curr->type = _getshort(*cp);
		*cp += INT16SZ;

		/* class */
		curr->class = _getshort(*cp);
		*cp += INT16SZ;

		/* ttl */
		curr->ttl = _getlong(*cp);
		*cp += INT32SZ;

		/* rdata size */
		curr->size = _getshort(*cp);
		*cp += INT16SZ;

		/* rdata itself */
		curr->rdata = malloc(curr->size);
		if (curr->rdata == NULL) {
			free_dns_rr(head);
			return (NULL);
		}
		memcpy(curr->rdata, *cp, curr->size);
		*cp += curr->size;
	}

	return (head);
}

static void
free_dns_query(struct dns_query *p)
{
	if (p == NULL)
		return;

	if (p->name)
		free(p->name);
	free_dns_query(p->next);
	free(p);
}

static void
free_dns_rr(struct dns_rr *p)
{
	if (p == NULL)
		return;

	if (p->name)
		free(p->name);
	if (p->rdata)
		free(p->rdata);
	free_dns_rr(p->next);
	free(p);
}

static void
free_dns_response(struct dns_response *p)
{
	if (p == NULL)
		return;

	free_dns_query(p->query);
	free_dns_rr(p->answer);
	free_dns_rr(p->authority);
	free_dns_rr(p->additional);
	free(p);
}

static int
count_dns_rr(struct dns_rr *p, u_int16_t class, u_int16_t type)
{
	int n = 0;

	while(p) {
		if (p->class == class && p->type == type)
			n++;
		p = p->next;
	}

	return (n);
}

#endif /*  !defined (HAVE_GETRRSETBYNAME) && !defined (HAVE_LDNS) */
