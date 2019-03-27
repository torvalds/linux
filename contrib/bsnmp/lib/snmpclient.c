/*
 * Copyright (c) 2004-2005
 *	Hartmut Brandt.
 *	All rights reserved.
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *         Kendy Kutzner
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/lib/snmpclient.c,v 1.36 2005/10/06 07:14:58 brandt_h Exp $
 *
 * Support functions for SNMP clients.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#include <limits.h>
#ifdef HAVE_ERR_H
#include <err.h>
#endif

#include "support.h"
#include "asn1.h"
#include "snmp.h"
#include "snmpclient.h"
#include "snmppriv.h"

/* global context */
struct snmp_client snmp_client;

/* List of all outstanding requests */
struct sent_pdu {
	int		reqid;
	struct snmp_pdu	*pdu;
	struct timeval	time;
	u_int		retrycount;
	snmp_send_cb_f	callback;
	void		*arg;
	void		*timeout_id;
	LIST_ENTRY(sent_pdu) entries;
};
LIST_HEAD(sent_pdu_list, sent_pdu);

static struct sent_pdu_list sent_pdus;

/*
 * Prototype table entry. All C-structure produced by the table function must
 * start with these two fields. This relies on the fact, that all TAILQ_ENTRY
 * are compatible with each other in the sense implied by ANSI-C.
 */
struct entry {
	TAILQ_ENTRY(entry)	link;
	uint64_t		found;
};
TAILQ_HEAD(table, entry);

/*
 * working list entry. This list is used to hold the Index part of the
 * table row's. The entry list and the work list parallel each other.
 */
struct work {
	TAILQ_ENTRY(work)	link;
	struct asn_oid		index;
};
TAILQ_HEAD(worklist, work);

/*
 * Table working data
 */
struct tabwork {
	const struct snmp_table *descr;
	struct table	*table;
	struct worklist	worklist;
	uint32_t	last_change;
	int		first;
	u_int		iter;
	snmp_table_cb_f	callback;
	void		*arg;
	struct snmp_pdu	pdu;
};

/*
 * Set the error string
 */
static void
seterr(struct snmp_client *sc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(sc->error, sizeof(sc->error), fmt, ap);
	va_end(ap);
}

/*
 * Free the entire table and work list. If table is NULL only the worklist
 * is freed.
 */
static void
table_free(struct tabwork *work, int all)
{
	struct work *w;
	struct entry *e;
	const struct snmp_table_entry *d;
	u_int i;

	while ((w = TAILQ_FIRST(&work->worklist)) != NULL) {
		TAILQ_REMOVE(&work->worklist, w, link);
		free(w);
	}

	if (all == 0)
		return;

	while ((e = TAILQ_FIRST(work->table)) != NULL) {
		for (i = 0; work->descr->entries[i].syntax != SNMP_SYNTAX_NULL;
		    i++) {
			d = &work->descr->entries[i];
			if (d->syntax == SNMP_SYNTAX_OCTETSTRING &&
			    (e->found & ((uint64_t)1 << i)))
				free(*(void **)(void *)
				    ((u_char *)e + d->offset));
		}
		TAILQ_REMOVE(work->table, e, link);
		free(e);
	}
}

/*
 * Find the correct table entry for the given variable. If non exists,
 * create one.
 */
static struct entry *
table_find(struct tabwork *work, const struct asn_oid *var)
{
	struct entry *e, *e1;
	struct work *w, *w1;
	u_int i, p, j;
	size_t len;
	u_char *ptr;
	struct asn_oid oid;

	/* get index */
	asn_slice_oid(&oid, var, work->descr->table.len + 2, var->len);

	e = TAILQ_FIRST(work->table);
	w = TAILQ_FIRST(&work->worklist);
	while (e != NULL) {
		if (asn_compare_oid(&w->index, &oid) == 0)
			return (e);
		e = TAILQ_NEXT(e, link);
		w = TAILQ_NEXT(w, link);
	}

	/* Not found create new one */
	if ((e = malloc(work->descr->entry_size)) == NULL) {
		seterr(&snmp_client, "no memory for table entry");
		return (NULL);
	}
	if ((w = malloc(sizeof(*w))) == NULL) {
		seterr(&snmp_client, "no memory for table entry");
		free(e);
		return (NULL);
	}
	w->index = oid;
	memset(e, 0, work->descr->entry_size);

	/* decode index */
	p = work->descr->table.len + 2;
	for (i = 0; i < work->descr->index_size; i++) {
		switch (work->descr->entries[i].syntax) {

		  case SNMP_SYNTAX_INTEGER:
			if (var->len < p + 1) {
				seterr(&snmp_client, "bad index: need integer");
				goto err;
			}
			if (var->subs[p] > INT32_MAX) {
				seterr(&snmp_client,
				    "bad index: integer too large");
				goto err;
			}
			*(int32_t *)(void *)((u_char *)e +
			    work->descr->entries[i].offset) = var->subs[p++];
			break;

		  case SNMP_SYNTAX_OCTETSTRING:
			if (var->len < p + 1) {
				seterr(&snmp_client,
				    "bad index: need string length");
				goto err;
			}
			len = var->subs[p++];
			if (var->len < p + len) {
				seterr(&snmp_client,
				    "bad index: string too short");
				goto err;
			}
			if ((ptr = malloc(len + 1)) == NULL) {
				seterr(&snmp_client,
				    "no memory for index string");
				goto err;
			}
			for (j = 0; j < len; j++) {
				if (var->subs[p] > UCHAR_MAX) {
					seterr(&snmp_client,
					    "bad index: char too large");
					free(ptr);
					goto err;
				}
				ptr[j] = var->subs[p++];
			}
			ptr[j] = '\0';
			*(u_char **)(void *)((u_char *)e +
			    work->descr->entries[i].offset) = ptr;
			*(size_t *)(void *)((u_char *)e +
			    work->descr->entries[i].offset + sizeof(u_char *))
			    = len;
			break;

		  case SNMP_SYNTAX_OID:
			if (var->len < p + 1) {
				seterr(&snmp_client,
				    "bad index: need oid length");
				goto err;
			}
			oid.len = var->subs[p++];
			if (var->len < p + oid.len) {
				seterr(&snmp_client,
				    "bad index: oid too short");
				goto err;
			}
			for (j = 0; j < oid.len; j++)
				oid.subs[j] = var->subs[p++];
			*(struct asn_oid *)(void *)((u_char *)e +
			    work->descr->entries[i].offset) = oid;
			break;

		  case SNMP_SYNTAX_IPADDRESS:
			if (var->len < p + 4) {
				seterr(&snmp_client,
				    "bad index: need ip-address");
				goto err;
			}
			for (j = 0; j < 4; j++) {
				if (var->subs[p] > 0xff) {
					seterr(&snmp_client,
					    "bad index: ipaddress too large");
					goto err;
				}
				((u_char *)e +
				    work->descr->entries[i].offset)[j] =
				    var->subs[p++];
			}
			break;

		  case SNMP_SYNTAX_GAUGE:
			if (var->len < p + 1) {
				seterr(&snmp_client,
				    "bad index: need unsigned");
				goto err;
			}
			if (var->subs[p] > UINT32_MAX) {
				seterr(&snmp_client,
				    "bad index: unsigned too large");
				goto err;
			}
			*(uint32_t *)(void *)((u_char *)e +
			    work->descr->entries[i].offset) = var->subs[p++];
			break;

		  case SNMP_SYNTAX_COUNTER:
		  case SNMP_SYNTAX_TIMETICKS:
		  case SNMP_SYNTAX_COUNTER64:
		  case SNMP_SYNTAX_NULL:
		  case SNMP_SYNTAX_NOSUCHOBJECT:
		  case SNMP_SYNTAX_NOSUCHINSTANCE:
		  case SNMP_SYNTAX_ENDOFMIBVIEW:
			abort();
		}
		e->found |= (uint64_t)1 << i;
	}

	/* link into the correct place */
	e1 = TAILQ_FIRST(work->table);
	w1 = TAILQ_FIRST(&work->worklist);
	while (e1 != NULL) {
		if (asn_compare_oid(&w1->index, &w->index) > 0)
			break;
		e1 = TAILQ_NEXT(e1, link);
		w1 = TAILQ_NEXT(w1, link);
	}
	if (e1 == NULL) {
		TAILQ_INSERT_TAIL(work->table, e, link);
		TAILQ_INSERT_TAIL(&work->worklist, w, link);
	} else {
		TAILQ_INSERT_BEFORE(e1, e, link);
		TAILQ_INSERT_BEFORE(w1, w, link);
	}

	return (e);

  err:
	/*
	 * Error happend. Free all octet string index parts and the entry
	 * itself.
	 */
	for (i = 0; i < work->descr->index_size; i++) {
		if (work->descr->entries[i].syntax == SNMP_SYNTAX_OCTETSTRING &&
		    (e->found & ((uint64_t)1 << i)))
			free(*(void **)(void *)((u_char *)e +
			    work->descr->entries[i].offset));
	}
	free(e);
	free(w);
	return (NULL);
}

/*
 * Assign the value
 */
static int
table_value(const struct snmp_table *descr, struct entry *e,
    const struct snmp_value *b)
{
	u_int i;
	u_char *ptr;

	for (i = descr->index_size;
	    descr->entries[i].syntax != SNMP_SYNTAX_NULL; i++)
		if (descr->entries[i].subid ==
		    b->var.subs[descr->table.len + 1])
			break;
	if (descr->entries[i].syntax == SNMP_SYNTAX_NULL)
		return (0);

	/* check syntax */
	if (b->syntax != descr->entries[i].syntax) {
		seterr(&snmp_client, "bad syntax (%u instead of %u)", b->syntax,
		    descr->entries[i].syntax);
		return (-1);
	}

	switch (b->syntax) {

	  case SNMP_SYNTAX_INTEGER:
		*(int32_t *)(void *)((u_char *)e + descr->entries[i].offset) =
		    b->v.integer;
		break;

	  case SNMP_SYNTAX_OCTETSTRING:
		if ((ptr = malloc(b->v.octetstring.len + 1)) == NULL) {
			seterr(&snmp_client, "no memory for string");
			return (-1);
		}
		memcpy(ptr, b->v.octetstring.octets, b->v.octetstring.len);
		ptr[b->v.octetstring.len] = '\0';
		*(u_char **)(void *)((u_char *)e + descr->entries[i].offset) =
		    ptr;
		*(size_t *)(void *)((u_char *)e + descr->entries[i].offset +
		    sizeof(u_char *)) = b->v.octetstring.len;
		break;

	  case SNMP_SYNTAX_OID:
		*(struct asn_oid *)(void *)((u_char *)e + descr->entries[i].offset) =
		    b->v.oid;
		break;

	  case SNMP_SYNTAX_IPADDRESS:
		memcpy((u_char *)e + descr->entries[i].offset,
		    b->v.ipaddress, 4);
		break;

	  case SNMP_SYNTAX_COUNTER:
	  case SNMP_SYNTAX_GAUGE:
	  case SNMP_SYNTAX_TIMETICKS:
		*(uint32_t *)(void *)((u_char *)e + descr->entries[i].offset) =
		    b->v.uint32;
		break;

	  case SNMP_SYNTAX_COUNTER64:
		*(uint64_t *)(void *)((u_char *)e + descr->entries[i].offset) =
		    b->v.counter64;
		break;

	  case SNMP_SYNTAX_NULL:
	  case SNMP_SYNTAX_NOSUCHOBJECT:
	  case SNMP_SYNTAX_NOSUCHINSTANCE:
	  case SNMP_SYNTAX_ENDOFMIBVIEW:
		abort();
	}
	e->found |= (uint64_t)1 << i;

	return (0);
}

/*
 * Initialize the first PDU to send
 */
static void
table_init_pdu(const struct snmp_table *descr, struct snmp_pdu *pdu)
{
	if (snmp_client.version == SNMP_V1)
		snmp_pdu_create(pdu, SNMP_PDU_GETNEXT);
	else {
		snmp_pdu_create(pdu, SNMP_PDU_GETBULK);
		pdu->error_index = 10;
	}
	if (descr->last_change.len != 0) {
		pdu->bindings[pdu->nbindings].syntax = SNMP_SYNTAX_NULL;
		pdu->bindings[pdu->nbindings].var = descr->last_change;
		pdu->nbindings++;
		if (pdu->version != SNMP_V1)
			pdu->error_status++;
	}
	pdu->bindings[pdu->nbindings].var = descr->table;
	pdu->bindings[pdu->nbindings].syntax = SNMP_SYNTAX_NULL;
	pdu->nbindings++;
}

/*
 * Return code:
 *	0  - End Of Table
 * 	-1 - Error
 *	-2 - Last change changed - again
 *	+1 - ok, continue
 */
static int
table_check_response(struct tabwork *work, const struct snmp_pdu *resp)
{
	const struct snmp_value *b;
	struct entry *e;

	if (resp->error_status != SNMP_ERR_NOERROR) {
		if (snmp_client.version == SNMP_V1 &&
		    resp->error_status == SNMP_ERR_NOSUCHNAME &&
		    resp->error_index ==
		    (work->descr->last_change.len == 0) ? 1 : 2)
			/* EOT */
			return (0);
		/* Error */
		seterr(&snmp_client, "error fetching table: status=%d index=%d",
		    resp->error_status, resp->error_index);
		return (-1);
	}

	for (b = resp->bindings; b < resp->bindings + resp->nbindings; b++) {
		if (work->descr->last_change.len != 0 && b == resp->bindings) {
			if (!asn_is_suboid(&work->descr->last_change, &b->var) ||
			    b->var.len != work->descr->last_change.len + 1 ||
			    b->var.subs[work->descr->last_change.len] != 0) {
				seterr(&snmp_client,
				    "last_change: bad response");
				return (-1);
			}
			if (b->syntax != SNMP_SYNTAX_TIMETICKS) {
				seterr(&snmp_client,
				    "last_change: bad syntax %u", b->syntax);
				return (-1);
			}
			if (work->first) {
				work->last_change = b->v.uint32;
				work->first = 0;

			} else if (work->last_change != b->v.uint32) {
				if (++work->iter >= work->descr->max_iter) {
					seterr(&snmp_client,
					    "max iteration count exceeded");
					return (-1);
				}
				table_free(work, 1);
				return (-2);
			}

			continue;
		}
		if (!asn_is_suboid(&work->descr->table, &b->var) ||
		    b->syntax == SNMP_SYNTAX_ENDOFMIBVIEW)
			return (0);

		if ((e = table_find(work, &b->var)) == NULL)
			return (-1);
		if (table_value(work->descr, e, b))
			return (-1);
	}
	return (+1);
}

/*
 * Check table consistency
 */
static int
table_check_cons(struct tabwork *work)
{
	struct entry *e;

	TAILQ_FOREACH(e, work->table, link)
		if ((e->found & work->descr->req_mask) !=
		    work->descr->req_mask) {
			if (work->descr->last_change.len == 0) {
				if (++work->iter >= work->descr->max_iter) {
					seterr(&snmp_client,
					    "max iteration count exceeded");
					return (-1);
				}
				return (-2);
			}
			seterr(&snmp_client, "inconsistency detected %llx %llx",
			    e->found, work->descr->req_mask);
			return (-1);
		}
	return (0);
}

/*
 * Fetch a table. Returns 0 if ok, -1 on errors.
 * This is the synchronous variant.
 */
int
snmp_table_fetch(const struct snmp_table *descr, void *list)
{
	struct snmp_pdu resp;
	struct tabwork work;
	int ret;

	work.descr = descr;
	work.table = (struct table *)list;
	work.iter = 0;
	TAILQ_INIT(work.table);
	TAILQ_INIT(&work.worklist);
	work.callback = NULL;
	work.arg = NULL;

  again:
	/*
	 * We come to this label when the code detects that the table
	 * has changed while fetching it.
	 */
	work.first = 1;
	work.last_change = 0;
	table_init_pdu(descr, &work.pdu);

	for (;;) {
		if (snmp_dialog(&work.pdu, &resp)) {
			table_free(&work, 1);
			return (-1);
		}
		if ((ret = table_check_response(&work, &resp)) == 0) {
			snmp_pdu_free(&resp);
			break;
		}
		if (ret == -1) {
			snmp_pdu_free(&resp);
			table_free(&work, 1);
			return (-1);
		}
		if (ret == -2) {
			snmp_pdu_free(&resp);
			goto again;
		}

		work.pdu.bindings[work.pdu.nbindings - 1].var =
		    resp.bindings[resp.nbindings - 1].var;

		snmp_pdu_free(&resp);
	}

	if ((ret = table_check_cons(&work)) == -1) {
		table_free(&work, 1);
		return (-1);
	}
	if (ret == -2) {
		table_free(&work, 1);
		goto again;
	}
	/*
	 * Free index list
	 */
	table_free(&work, 0);
	return (0);
}

/*
 * Callback for table
 */
static void
table_cb(struct snmp_pdu *req __unused, struct snmp_pdu *resp, void *arg)
{
	struct tabwork *work = arg;
	int ret;

	if (resp == NULL) {
		/* timeout */
		seterr(&snmp_client, "no response to fetch table request");
		table_free(work, 1);
		work->callback(work->table, work->arg, -1);
		free(work);
		return;
	}

	if ((ret = table_check_response(work, resp)) == 0) {
		/* EOT */
		snmp_pdu_free(resp);

		if ((ret = table_check_cons(work)) == -1) {
			/* error happend */
			table_free(work, 1);
			work->callback(work->table, work->arg, -1);
			free(work);
			return;
		}
		if (ret == -2) {
			/* restart */
  again:
			table_free(work, 1);
			work->first = 1;
			work->last_change = 0;
			table_init_pdu(work->descr, &work->pdu);
			if (snmp_pdu_send(&work->pdu, table_cb, work) == -1) {
				work->callback(work->table, work->arg, -1);
				free(work);
				return;
			}
			return;
		}
		/*
		 * Free index list
		 */
		table_free(work, 0);
		work->callback(work->table, work->arg, 0);
		free(work);
		return;
	}

	if (ret == -1) {
		/* error */
		snmp_pdu_free(resp);
		table_free(work, 1);
		work->callback(work->table, work->arg, -1);
		free(work);
		return;
	}

	if (ret == -2) {
		/* again */
		snmp_pdu_free(resp);
		goto again;
	}

	/* next part */

	work->pdu.bindings[work->pdu.nbindings - 1].var =
	    resp->bindings[resp->nbindings - 1].var;

	snmp_pdu_free(resp);

	if (snmp_pdu_send(&work->pdu, table_cb, work) == -1) {
		table_free(work, 1);
		work->callback(work->table, work->arg, -1);
		free(work);
		return;
	}
}

int
snmp_table_fetch_async(const struct snmp_table *descr, void *list,
    snmp_table_cb_f func, void *arg)
{
	struct tabwork *work;

	if ((work = malloc(sizeof(*work))) == NULL) {
		seterr(&snmp_client, "%s", strerror(errno));
		return (-1);
	}

	work->descr = descr;
	work->table = (struct table *)list;
	work->iter = 0;
	TAILQ_INIT(work->table);
	TAILQ_INIT(&work->worklist);

	work->callback = func;
	work->arg = arg;

	/*
	 * Start by sending the first PDU
	 */
	work->first = 1;
	work->last_change = 0;
	table_init_pdu(descr, &work->pdu);

	if (snmp_pdu_send(&work->pdu, table_cb, work) == -1) {
		free(work);
		work = NULL;
		return (-1);
	}
	return (0);
}

/*
 * Append an index to an oid
 */
int
snmp_oid_append(struct asn_oid *oid, const char *fmt, ...)
{
	va_list	va;
	int	size;
	char	*nextptr;
	const u_char *str;
	size_t	len;
	struct in_addr ina;
	int ret;

	va_start(va, fmt);

	size = 0;

	ret = 0;
	while (*fmt != '\0') {
		switch (*fmt++) {
		  case 'i':
			/* just an integer more */
			if (oid->len + 1 > ASN_MAXOIDLEN) {
				warnx("%s: OID too long for integer", __func__);
				ret = -1;
				break;
			}
			oid->subs[oid->len++] = va_arg(va, asn_subid_t);
			break;

		  case 'a':
			/* append an IP address */
			if (oid->len + 4 > ASN_MAXOIDLEN) {
				warnx("%s: OID too long for ip-addr", __func__);
				ret = -1;
				break;
			}
			ina = va_arg(va, struct in_addr);
			ina.s_addr = ntohl(ina.s_addr);
			oid->subs[oid->len++] = (ina.s_addr >> 24) & 0xff;
			oid->subs[oid->len++] = (ina.s_addr >> 16) & 0xff;
			oid->subs[oid->len++] = (ina.s_addr >> 8) & 0xff;
			oid->subs[oid->len++] = (ina.s_addr >> 0) & 0xff;
			break;

		  case 's':
			/* append a null-terminated string,
			 * length is computed */
			str = (const u_char *)va_arg(va, const char *);
			len = strlen((const char *)str);
			if (oid->len + len + 1 > ASN_MAXOIDLEN) {
				warnx("%s: OID too long for string", __func__);
				ret = -1;
				break;
			}
			oid->subs[oid->len++] = len;
			while (len--)
				oid->subs[oid->len++] = *str++;
			break;

		  case '(':
			/* the integer value between ( and ) is stored
			 * in size */
			size = strtol(fmt, &nextptr, 10);
			if (*nextptr != ')')
				abort();
			fmt = ++nextptr;
			break;

		  case 'b':
			/* append `size` characters */
			str = (const u_char *)va_arg(va, const char *);
			if (oid->len + size > ASN_MAXOIDLEN) {
				warnx("%s: OID too long for string", __func__);
				ret = -1;
				break;
			}
			while (size--)
				oid->subs[oid->len++] = *str++;
			break;

		  case 'c':
			/* get size and the octets from the arguments */
			size = va_arg(va, size_t);
			str = va_arg(va, const u_char *);
			if (oid->len + size + 1 > ASN_MAXOIDLEN) {
				warnx("%s: OID too long for string", __func__);
				ret = -1;
				break;
			}
			oid->subs[oid->len++] = size;
			while (size--)
				oid->subs[oid->len++] = *str++;
			break;

		  default:
			abort();
		}
	}
	va_end(va);
	return (ret);
}

/*
 * Initialize a client structure
 */
void
snmp_client_init(struct snmp_client *c)
{
	memset(c, 0, sizeof(*c));

	c->version = SNMP_V2c;
	c->trans = SNMP_TRANS_UDP;
	c->chost = NULL;
	c->cport = NULL;

	strcpy(c->read_community, "public");
	strcpy(c->write_community, "private");

	c->security_model = SNMP_SECMODEL_USM;
	strcpy(c->cname, "");

	c->timeout.tv_sec = 3;
	c->timeout.tv_usec = 0;
	c->retries = 3;
	c->dump_pdus = 0;
	c->txbuflen = c->rxbuflen = 10000;

	c->fd = -1;

	c->max_reqid = INT32_MAX;
	c->min_reqid = 0;
	c->next_reqid = 0;

	c->engine.max_msg_size = 1500; /* XXX */
}


/*
 * Open UDP client socket
 */
static int
open_client_udp(const char *host, const char *port)
{
	int error;
	char *ptr;
	struct addrinfo hints, *res0, *res;

	/* copy host- and portname */
	if (snmp_client.chost == NULL) {
		if ((snmp_client.chost = malloc(1 + sizeof(DEFAULT_HOST)))
		    == NULL) {
			seterr(&snmp_client, "%s", strerror(errno));
			return (-1);
		}
		strcpy(snmp_client.chost, DEFAULT_HOST);
	}
	if (host != NULL) {
		if ((ptr = malloc(1 + strlen(host))) == NULL) {
			seterr(&snmp_client, "%s", strerror(errno));
			return (-1);
		}
		free(snmp_client.chost);
		snmp_client.chost = ptr;
		strcpy(snmp_client.chost, host);
	}
	if (snmp_client.cport == NULL) {
		if ((snmp_client.cport = malloc(1 + sizeof(DEFAULT_PORT)))
		    == NULL) {
			seterr(&snmp_client, "%s", strerror(errno));
			return (-1);
		}
		strcpy(snmp_client.cport, DEFAULT_PORT);
	}
	if (port != NULL) {
		if ((ptr = malloc(1 + strlen(port))) == NULL) {
			seterr(&snmp_client, "%s", strerror(errno));
			return (-1);
		}
		free(snmp_client.cport);
		snmp_client.cport = ptr;
		strcpy(snmp_client.cport, port);
	}

	/* open connection */
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;
	error = getaddrinfo(snmp_client.chost, snmp_client.cport, &hints, &res0);
	if (error != 0) {
		seterr(&snmp_client, "%s: %s", snmp_client.chost,
		    gai_strerror(error));
		return (-1);
	}
	res = res0;
	for (;;) {
		if ((snmp_client.fd = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol)) == -1) {
			if ((res = res->ai_next) == NULL) {
				seterr(&snmp_client, "%s", strerror(errno));
				freeaddrinfo(res0);
				return (-1);
			}
		} else if (connect(snmp_client.fd, res->ai_addr,
		    res->ai_addrlen) == -1) {
			if ((res = res->ai_next) == NULL) {
				seterr(&snmp_client, "%s", strerror(errno));
				freeaddrinfo(res0);
				(void)close(snmp_client.fd);
				snmp_client.fd = -1;
				return (-1);
			}
		} else
			break;
	}
	freeaddrinfo(res0);
	return (0);
}

static void
remove_local(void)
{
	(void)remove(snmp_client.local_path);
}

/*
 * Open local socket
 */
static int
open_client_local(const char *path)
{
	struct sockaddr_un sa;
	char *ptr;
	int stype;

	if (snmp_client.chost == NULL) {
		if ((snmp_client.chost = malloc(1 + sizeof(DEFAULT_LOCAL)))
		    == NULL) {
			seterr(&snmp_client, "%s", strerror(errno));
			return (-1);
		}
		strcpy(snmp_client.chost, DEFAULT_LOCAL);
	}
	if (path != NULL) {
		if ((ptr = malloc(1 + strlen(path))) == NULL) {
			seterr(&snmp_client, "%s", strerror(errno));
			return (-1);
		}
		free(snmp_client.chost);
		snmp_client.chost = ptr;
		strcpy(snmp_client.chost, path);
	}

	if (snmp_client.trans == SNMP_TRANS_LOC_DGRAM)
		stype = SOCK_DGRAM;
	else
		stype = SOCK_STREAM;

	if ((snmp_client.fd = socket(PF_LOCAL, stype, 0)) == -1) {
		seterr(&snmp_client, "%s", strerror(errno));
		return (-1);
	}

	snprintf(snmp_client.local_path, sizeof(snmp_client.local_path),
	    "%s", SNMP_LOCAL_PATH);

	if (mktemp(snmp_client.local_path) == NULL) {
		seterr(&snmp_client, "%s", strerror(errno));
		(void)close(snmp_client.fd);
		snmp_client.fd = -1;
		return (-1);
	}

	sa.sun_family = AF_LOCAL;
	sa.sun_len = sizeof(sa);
	strcpy(sa.sun_path, snmp_client.local_path);

	if (bind(snmp_client.fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		seterr(&snmp_client, "%s", strerror(errno));
		(void)close(snmp_client.fd);
		snmp_client.fd = -1;
		(void)remove(snmp_client.local_path);
		return (-1);
	}
	atexit(remove_local);

	sa.sun_family = AF_LOCAL;
	sa.sun_len = offsetof(struct sockaddr_un, sun_path) +
	    strlen(snmp_client.chost);
	strncpy(sa.sun_path, snmp_client.chost, sizeof(sa.sun_path) - 1);
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(snmp_client.fd, (struct sockaddr *)&sa, sa.sun_len) == -1) {
		seterr(&snmp_client, "%s", strerror(errno));
		(void)close(snmp_client.fd);
		snmp_client.fd = -1;
		(void)remove(snmp_client.local_path);
		return (-1);
	}
	return (0);
}

/*
 * SNMP_OPEN
 */
int
snmp_open(const char *host, const char *port, const char *readcomm,
    const char *writecomm)
{
	struct timeval tout;

	/* still open ? */
	if (snmp_client.fd != -1) {
		errno = EBUSY;
		seterr(&snmp_client, "%s", strerror(errno));
		return (-1);
	}

	/* copy community strings */
	if (readcomm != NULL)
		strlcpy(snmp_client.read_community, readcomm,
		    sizeof(snmp_client.read_community));
	if (writecomm != NULL)
		strlcpy(snmp_client.write_community, writecomm,
		    sizeof(snmp_client.write_community));

	switch (snmp_client.trans) {

	  case SNMP_TRANS_UDP:
		if (open_client_udp(host, port) != 0)
			return (-1);
		break;

	  case SNMP_TRANS_LOC_DGRAM:
	  case SNMP_TRANS_LOC_STREAM:
		if (open_client_local(host) != 0)
			return (-1);
		break;

	  default:
		seterr(&snmp_client, "bad transport mapping");
		return (-1);
	}
	tout.tv_sec = 0;
	tout.tv_usec = 0;
	if (setsockopt(snmp_client.fd, SOL_SOCKET, SO_SNDTIMEO,
	    &tout, sizeof(struct timeval)) == -1) {
		seterr(&snmp_client, "%s", strerror(errno));
		(void)close(snmp_client.fd);
		snmp_client.fd = -1;
		if (snmp_client.local_path[0] != '\0')
			(void)remove(snmp_client.local_path);
		return (-1);
	}

	/* initialize list */
	LIST_INIT(&sent_pdus);

	return (0);
}


/*
 * SNMP_CLOSE
 *
 * closes connection to snmp server
 * - function cannot fail
 * - clears connection
 * - clears list of sent pdus
 *
 * input:
 *  void
 * return:
 *  void
 */
void
snmp_close(void)
{
	struct sent_pdu *p1;

	if (snmp_client.fd != -1) {
		(void)close(snmp_client.fd);
		snmp_client.fd = -1;
		if (snmp_client.local_path[0] != '\0')
			(void)remove(snmp_client.local_path);
	}
	while(!LIST_EMPTY(&sent_pdus)){
		p1 = LIST_FIRST(&sent_pdus);
		if (p1->timeout_id != NULL)
			snmp_client.timeout_stop(p1->timeout_id);
		LIST_REMOVE(p1, entries);
		free(p1);
	}
	free(snmp_client.chost);
	free(snmp_client.cport);
}

/*
 * initialize a snmp_pdu structure
 */
void
snmp_pdu_create(struct snmp_pdu *pdu, u_int op)
{
	memset(pdu, 0, sizeof(struct snmp_pdu));

	if (op == SNMP_PDU_SET)
		strlcpy(pdu->community, snmp_client.write_community,
		    sizeof(pdu->community));
	else
		strlcpy(pdu->community, snmp_client.read_community,
		    sizeof(pdu->community));

	pdu->type = op;
	pdu->version = snmp_client.version;
	pdu->error_status = 0;
	pdu->error_index = 0;
	pdu->nbindings = 0;

	if (snmp_client.version != SNMP_V3)
		return;

	pdu->identifier = ++snmp_client.identifier;
	pdu->engine.max_msg_size = snmp_client.engine.max_msg_size;
	pdu->flags = 0;
	pdu->security_model = snmp_client.security_model;

	if (snmp_client.security_model == SNMP_SECMODEL_USM) {
		memcpy(&pdu->engine, &snmp_client.engine, sizeof(pdu->engine));
		memcpy(&pdu->user, &snmp_client.user, sizeof(pdu->user));
		snmp_pdu_init_secparams(pdu);
	} else
		seterr(&snmp_client, "unknown security model");

	if (snmp_client.clen > 0) {
		memcpy(pdu->context_engine, snmp_client.cengine,
		    snmp_client.clen);
		pdu->context_engine_len = snmp_client.clen;
	} else {
		memcpy(pdu->context_engine, snmp_client.engine.engine_id,
		    snmp_client.engine.engine_len);
		pdu->context_engine_len = snmp_client.engine.engine_len;
	}

	strlcpy(pdu->context_name, snmp_client.cname,
	    sizeof(pdu->context_name));
}

/* add pairs of (struct asn_oid, enum snmp_syntax) to an existing pdu */
/* added 10/04/02 by kek: check for MAX_BINDINGS */
int
snmp_add_binding(struct snmp_v1_pdu *pdu, ...)
{
	va_list ap;
	const struct asn_oid *oid;
	u_int ret;

	va_start(ap, pdu);

	ret = pdu->nbindings;
	while ((oid = va_arg(ap, const struct asn_oid *)) != NULL) {
		if (pdu->nbindings >= SNMP_MAX_BINDINGS){
			va_end(ap);
			return (-1);
		}
		pdu->bindings[pdu->nbindings].var = *oid;
		pdu->bindings[pdu->nbindings].syntax =
		    va_arg(ap, enum snmp_syntax);
		pdu->nbindings++;
	}
	va_end(ap);
	return (ret);
}


static int32_t
snmp_next_reqid(struct snmp_client * c)
{
	int32_t i;

	i = c->next_reqid;
	if (c->next_reqid >= c->max_reqid)
		c->next_reqid = c->min_reqid;
	else
		c->next_reqid++;
	return (i);
}

/*
 * Send request and return request id.
 */
static int32_t
snmp_send_packet(struct snmp_pdu * pdu)
{
	u_char *buf;
	struct asn_buf b;
	ssize_t ret;

	if ((buf = calloc(1, snmp_client.txbuflen)) == NULL) {
		seterr(&snmp_client, "%s", strerror(errno));
		return (-1);
	}

	pdu->request_id = snmp_next_reqid(&snmp_client);

	b.asn_ptr = buf;
	b.asn_len = snmp_client.txbuflen;
	if (snmp_pdu_encode(pdu, &b)) {
		seterr(&snmp_client, "%s", strerror(errno));
		free(buf);
		return (-1);
	}

	if (snmp_client.dump_pdus)
		snmp_pdu_dump(pdu);

	if ((ret = send(snmp_client.fd, buf, b.asn_ptr - buf, 0)) == -1) {
		seterr(&snmp_client, "%s", strerror(errno));
		free(buf);
		return (-1);
	}
	free(buf);

	return (pdu->request_id);
}

/*
 * to be called when a snmp request timed out
 */
static void
snmp_timeout(void * listentry_ptr)
{
	struct sent_pdu *listentry = listentry_ptr;

#if 0
	warnx("snmp request %i timed out, attempt (%i/%i)",
	    listentry->reqid, listentry->retrycount, snmp_client.retries);
#endif

	listentry->retrycount++;
	if (listentry->retrycount > snmp_client.retries) {
		/* there is no answer at all */
		LIST_REMOVE(listentry, entries);
		listentry->callback(listentry->pdu, NULL, listentry->arg);
		free(listentry);
	} else {
		/* try again */
		/* new request with new request ID */
		listentry->reqid = snmp_send_packet(listentry->pdu);
		listentry->timeout_id =
		    snmp_client.timeout_start(&snmp_client.timeout,
		    snmp_timeout, listentry);
	}
}

int32_t
snmp_pdu_send(struct snmp_pdu *pdu, snmp_send_cb_f func, void *arg)
{
	struct sent_pdu *listentry;
	int32_t id;

	if ((listentry = malloc(sizeof(struct sent_pdu))) == NULL) {
		seterr(&snmp_client, "%s", strerror(errno));
		return (-1);
	}

	/* here we really send */
	if ((id = snmp_send_packet(pdu)) == -1) {
		free(listentry);
		return (-1);
	}

	/* add entry to list of sent PDUs */
	listentry->pdu = pdu;
	if (gettimeofday(&listentry->time, NULL) == -1)
		warn("gettimeofday() failed");

	listentry->reqid = pdu->request_id;
	listentry->callback = func;
	listentry->arg = arg;
	listentry->retrycount=1;
	listentry->timeout_id =
	    snmp_client.timeout_start(&snmp_client.timeout, snmp_timeout,
	    listentry);

	LIST_INSERT_HEAD(&sent_pdus, listentry, entries);

	return (id);
}

/*
 * Receive an SNMP packet.
 *
 * tv controls how we wait for a packet: if tv is a NULL pointer,
 * the receive blocks forever, if tv points to a structure with all
 * members 0 the socket is polled, in all other cases tv specifies the
 * maximum time to wait for a packet.
 *
 * Return:
 *	-1 on errors
 *	0 on timeout
 *	+1 if packet received
 */
static int
snmp_receive_packet(struct snmp_pdu *pdu, struct timeval *tv)
{
	int dopoll, setpoll;
	int flags;
	int saved_errno;
	u_char *buf;
	int ret;
	struct asn_buf abuf;
	int32_t ip;
#ifdef bsdi
	int optlen;
#else
	socklen_t optlen;
#endif

	if ((buf = calloc(1, snmp_client.rxbuflen)) == NULL) {
		seterr(&snmp_client, "%s", strerror(errno));
		return (-1);
	}
	dopoll = setpoll = 0;
	flags = 0;
	if (tv != NULL) {
		/* poll or timeout */
		if (tv->tv_sec != 0 || tv->tv_usec != 0) {
			/* wait with timeout */
			if (setsockopt(snmp_client.fd, SOL_SOCKET, SO_RCVTIMEO,
			    tv, sizeof(*tv)) == -1) {
				seterr(&snmp_client, "setsockopt: %s",
				    strerror(errno));
				free(buf);
				return (-1);
			}
			optlen = sizeof(*tv);
			if (getsockopt(snmp_client.fd, SOL_SOCKET, SO_RCVTIMEO,
			    tv, &optlen) == -1) {
				seterr(&snmp_client, "getsockopt: %s",
				    strerror(errno));
				free(buf);
				return (-1);
			}
			/* at this point tv_sec and tv_usec may appear
			 * as 0. This happens for timeouts lesser than
			 * the clock granularity. The kernel rounds these to
			 * 0 and this would result in a blocking receive.
			 * Instead of an else we check tv_sec and tv_usec
			 * again below and if this rounding happens,
			 * switch to a polling receive. */
		}
		if (tv->tv_sec == 0 && tv->tv_usec == 0) {
			/* poll */
			dopoll = 1;
			if ((flags = fcntl(snmp_client.fd, F_GETFL, 0)) == -1) {
				seterr(&snmp_client, "fcntl: %s",
				    strerror(errno));
				free(buf);
				return (-1);
			}
			if (!(flags & O_NONBLOCK)) {
				setpoll = 1;
				flags |= O_NONBLOCK;
				if (fcntl(snmp_client.fd, F_SETFL, flags) == -1) {
					seterr(&snmp_client, "fcntl: %s",
					    strerror(errno));
					free(buf);
					return (-1);
				}
			}
		}
	}
	ret = recv(snmp_client.fd, buf, snmp_client.rxbuflen, 0);
	saved_errno = errno;
	if (tv != NULL) {
		if (dopoll) {
			if (setpoll) {
				flags &= ~O_NONBLOCK;
				(void)fcntl(snmp_client.fd, F_SETFL, flags);
			}
		} else {
			tv->tv_sec = 0;
			tv->tv_usec = 0;
			(void)setsockopt(snmp_client.fd, SOL_SOCKET, SO_RCVTIMEO,
			    tv, sizeof(*tv));
		}
	}
	if (ret == -1) {
		free(buf);
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return (0);
		seterr(&snmp_client, "recv: %s", strerror(saved_errno));
		return (-1);
	}
	if (ret == 0) {
		/* this happens when we have a streaming socket and the
		 * remote side has closed it */
		free(buf);
		seterr(&snmp_client, "recv: socket closed by peer");
		errno = EPIPE;
		return (-1);
	}

	abuf.asn_ptr = buf;
	abuf.asn_len = ret;

	memset(pdu, 0, sizeof(*pdu));
	if (snmp_client.security_model == SNMP_SECMODEL_USM) {
		memcpy(&pdu->engine, &snmp_client.engine, sizeof(pdu->engine));
		memcpy(&pdu->user, &snmp_client.user, sizeof(pdu->user));
		snmp_pdu_init_secparams(pdu);
	}

	if (SNMP_CODE_OK != (ret = snmp_pdu_decode(&abuf, pdu, &ip))) {
		seterr(&snmp_client, "snmp_decode_pdu: failed %d", ret);
		free(buf);
		return (-1);
	}

	free(buf);
	if (snmp_client.dump_pdus)
		snmp_pdu_dump(pdu);

	snmp_client.engine.engine_time = pdu->engine.engine_time;
	snmp_client.engine.engine_boots = pdu->engine.engine_boots;

	return (+1);
}

static int
snmp_deliver_packet(struct snmp_pdu * resp)
{
	struct sent_pdu *listentry;

	if (resp->type != SNMP_PDU_RESPONSE) {
		warn("ignoring snmp pdu %u", resp->type);
		return (-1);
	}

	LIST_FOREACH(listentry, &sent_pdus, entries)
		if (listentry->reqid == resp->request_id)
			break;
	if (listentry == NULL)
		return (-1);

	LIST_REMOVE(listentry, entries);
	listentry->callback(listentry->pdu, resp, listentry->arg);

	snmp_client.timeout_stop(listentry->timeout_id);

	free(listentry);
	return (0);
}

int
snmp_receive(int blocking)
{
	int ret;

	struct timeval tv;
	struct snmp_pdu * resp;

	memset(&tv, 0, sizeof(tv));

	resp = malloc(sizeof(struct snmp_pdu));
	if (resp == NULL) {
		seterr(&snmp_client, "no memory for returning PDU");
		return (-1) ;
	}

	if ((ret = snmp_receive_packet(resp, blocking ? NULL : &tv)) <= 0) {
		free(resp);
		return (ret);
	}
	ret = snmp_deliver_packet(resp);
	snmp_pdu_free(resp);
	free(resp);
	return (ret);
}


/*
 * Check a GETNEXT response. Here we have three possible outcomes: -1 an
 * unexpected error happened. +1 response is ok and is within the table 0
 * response is ok, but is behind the table or error is NOSUCHNAME. The req
 * should point to a template PDU which contains the base OIDs and the
 * syntaxes. This is really only useful to sweep non-sparse tables.
 */
static int
ok_getnext(const struct snmp_pdu * req, const struct snmp_pdu * resp)
{
	u_int i;

	if (resp->version != req->version) {
		warnx("SNMP GETNEXT: response has wrong version");
		return (-1);
	}

	if (resp->error_status == SNMP_ERR_NOSUCHNAME)
		return (0);

	if (resp->error_status != SNMP_ERR_NOERROR) {
		warnx("SNMP GETNEXT: error %d", resp->error_status);
		return (-1);
	}
	if (resp->nbindings != req->nbindings) {
		warnx("SNMP GETNEXT: bad number of bindings in response");
		return (-1);
	}
	for (i = 0; i < req->nbindings; i++) {
		if (!asn_is_suboid(&req->bindings[i].var,
		    &resp->bindings[i].var)) {
			if (i != 0)
				warnx("SNMP GETNEXT: inconsistent table "
				    "response");
			return (0);
		}
		if (resp->version != SNMP_V1 &&
		    resp->bindings[i].syntax == SNMP_SYNTAX_ENDOFMIBVIEW)
			return (0);

		if (resp->bindings[i].syntax != req->bindings[i].syntax) {
			warnx("SNMP GETNEXT: bad syntax in response");
			return (0);
		}
	}
	return (1);
}

/*
 * Check a GET response. Here we have three possible outcomes: -1 an
 * unexpected error happened. +1 response is ok. 0 NOSUCHNAME The req should
 * point to a template PDU which contains the OIDs and the syntaxes. This
 * is only useful for SNMPv1 or single object GETS.
 */
static int
ok_get(const struct snmp_pdu * req, const struct snmp_pdu * resp)
{
	u_int i;

	if (resp->version != req->version) {
		warnx("SNMP GET: response has wrong version");
		return (-1);
	}

	if (resp->error_status == SNMP_ERR_NOSUCHNAME)
		return (0);

	if (resp->error_status != SNMP_ERR_NOERROR) {
		warnx("SNMP GET: error %d", resp->error_status);
		return (-1);
	}

	if (resp->nbindings != req->nbindings) {
		warnx("SNMP GET: bad number of bindings in response");
		return (-1);
	}
	for (i = 0; i < req->nbindings; i++) {
		if (asn_compare_oid(&req->bindings[i].var,
		    &resp->bindings[i].var) != 0) {
			warnx("SNMP GET: bad OID in response");
			return (-1);
		}
		if (snmp_client.version != SNMP_V1 &&
		    (resp->bindings[i].syntax == SNMP_SYNTAX_NOSUCHOBJECT ||
		    resp->bindings[i].syntax == SNMP_SYNTAX_NOSUCHINSTANCE))
			return (0);
		if (resp->bindings[i].syntax != req->bindings[i].syntax) {
			warnx("SNMP GET: bad syntax in response");
			return (-1);
		}
	}
	return (1);
}

/*
 * Check the response to a SET PDU. We check: - the error status must be 0 -
 * the number of bindings must be equal in response and request - the
 * syntaxes must be the same in response and request - the OIDs must be the
 * same in response and request
 */
static int
ok_set(const struct snmp_pdu * req, const struct snmp_pdu * resp)
{
	u_int i;

	if (resp->version != req->version) {
		warnx("SNMP SET: response has wrong version");
		return (-1);
	}

	if (resp->error_status == SNMP_ERR_NOSUCHNAME) {
		warnx("SNMP SET: error %d", resp->error_status);
		return (0);
	}
	if (resp->error_status != SNMP_ERR_NOERROR) {
		warnx("SNMP SET: error %d", resp->error_status);
		return (-1);
	}

	if (resp->nbindings != req->nbindings) {
		warnx("SNMP SET: bad number of bindings in response");
		return (-1);
	}
	for (i = 0; i < req->nbindings; i++) {
		if (asn_compare_oid(&req->bindings[i].var,
		    &resp->bindings[i].var) != 0) {
			warnx("SNMP SET: wrong OID in response to SET");
			return (-1);
		}
		if (resp->bindings[i].syntax != req->bindings[i].syntax) {
			warnx("SNMP SET: bad syntax in response");
			return (-1);
		}
	}
	return (1);
}

/*
 * Simple checks for response PDUs against request PDUs. Return values: 1=ok,
 * 0=nosuchname or similar, -1=failure, -2=no response at all
 */
int
snmp_pdu_check(const struct snmp_pdu *req,
    const struct snmp_pdu *resp)
{
	if (resp == NULL)
		return (-2);

	switch (req->type) {

	  case SNMP_PDU_GET:
		return (ok_get(req, resp));

	  case SNMP_PDU_SET:
		return (ok_set(req, resp));

	  case SNMP_PDU_GETNEXT:
		return (ok_getnext(req, resp));

	}
	errx(1, "%s: bad pdu type %i", __func__, req->type);
}

int
snmp_dialog(struct snmp_v1_pdu *req, struct snmp_v1_pdu *resp)
{
	struct timeval tv = snmp_client.timeout;
	struct timeval end;
	struct snmp_pdu pdu;
	int ret;
	int32_t reqid;
	u_int i;

	/*
	 * Make a copy of the request and replace the syntaxes by NULL
	 * if this is a GET,GETNEXT or GETBULK.
	 */
	pdu = *req;
	if (pdu.type == SNMP_PDU_GET || pdu.type == SNMP_PDU_GETNEXT ||
	    pdu.type == SNMP_PDU_GETBULK) {
		for (i = 0; i < pdu.nbindings; i++)
			pdu.bindings[i].syntax = SNMP_SYNTAX_NULL;
	}

	for (i = 0; i <= snmp_client.retries; i++) {
		(void)gettimeofday(&end, NULL);
		timeradd(&end, &snmp_client.timeout, &end);
		if ((reqid = snmp_send_packet(&pdu)) == -1)
			return (-1);
		for (;;) {
			(void)gettimeofday(&tv, NULL);
			if (timercmp(&end, &tv, <=))
				break;
			timersub(&end, &tv, &tv);
			if ((ret = snmp_receive_packet(resp, &tv)) == 0)
				/* timeout */
				break;

			if (ret > 0) {
				if (reqid == resp->request_id)
					return (0);
				/* not for us */
				(void)snmp_deliver_packet(resp);
			}
			if (ret < 0 && errno == EPIPE)
				/* stream closed */
				return (-1);
		}
	}
	errno = ETIMEDOUT;
	seterr(&snmp_client, "retry count exceeded");
	return (-1);
}

int
snmp_discover_engine(char *passwd)
{
	char cname[SNMP_ADM_STR32_SIZ];
	enum snmp_authentication cap;
	enum snmp_privacy cpp;
	struct snmp_pdu req, resp;

	if (snmp_client.version != SNMP_V3)
		seterr(&snmp_client, "wrong version");

	strlcpy(cname, snmp_client.user.sec_name, sizeof(cname));
	cap = snmp_client.user.auth_proto;
	cpp = snmp_client.user.priv_proto;

	snmp_client.engine.engine_len = 0;
	snmp_client.engine.engine_boots = 0;
	snmp_client.engine.engine_time = 0;
	snmp_client.user.auth_proto = SNMP_AUTH_NOAUTH;
	snmp_client.user.priv_proto = SNMP_PRIV_NOPRIV;
	memset(snmp_client.user.sec_name, 0, sizeof(snmp_client.user.sec_name));

	snmp_pdu_create(&req, SNMP_PDU_GET);

	if (snmp_dialog(&req, &resp) == -1)
		 return (-1);

	if (resp.version != req.version) {
		seterr(&snmp_client, "wrong version");
		return (-1);
	}

	if (resp.error_status != SNMP_ERR_NOERROR) {
		seterr(&snmp_client, "Error %d in responce", resp.error_status);
		return (-1);
	}

	snmp_client.engine.engine_len = resp.engine.engine_len;
	snmp_client.engine.max_msg_size = resp.engine.max_msg_size;
	memcpy(snmp_client.engine.engine_id, resp.engine.engine_id,
	    resp.engine.engine_len);

	strlcpy(snmp_client.user.sec_name, cname,
	    sizeof(snmp_client.user.sec_name));
	snmp_client.user.auth_proto = cap;
	snmp_client.user.priv_proto = cpp;

	if (snmp_client.user.auth_proto == SNMP_AUTH_NOAUTH)
		return (0);

	if (passwd == NULL ||
	    snmp_passwd_to_keys(&snmp_client.user, passwd) != SNMP_CODE_OK ||
	    snmp_get_local_keys(&snmp_client.user, snmp_client.engine.engine_id,
	    snmp_client.engine.engine_len) != SNMP_CODE_OK)
		return (-1);

	if (resp.engine.engine_boots != 0)
		snmp_client.engine.engine_boots = resp.engine.engine_boots;

	if (resp.engine.engine_time != 0) {
		snmp_client.engine.engine_time = resp.engine.engine_time;
		return (0);
	}

	snmp_pdu_free(&req);

	snmp_pdu_create(&req, SNMP_PDU_GET);
	req.engine.engine_boots = 0;
	req.engine.engine_time = 0;

	if (snmp_dialog(&req, &resp) == -1)
		return (-1);

	if (resp.version != req.version) {
		seterr(&snmp_client, "wrong version");
		return (-1);
	}

	if (resp.error_status != SNMP_ERR_NOERROR) {
		seterr(&snmp_client, "Error %d in responce", resp.error_status);
		return (-1);
	}

	snmp_client.engine.engine_boots = resp.engine.engine_boots;
	snmp_client.engine.engine_time = resp.engine.engine_time;

	snmp_pdu_free(&req);
	snmp_pdu_free(&resp);

	return (0);
}

int
snmp_client_set_host(struct snmp_client *cl, const char *h)
{
	char *np;

	if (h == NULL) {
		if (cl->chost != NULL)
			free(cl->chost);
		cl->chost = NULL;
	} else {
		if ((np = malloc(strlen(h) + 1)) == NULL)
			return (-1);
		strcpy(np, h);
		if (cl->chost != NULL)
			free(cl->chost);
		cl->chost = np;
	}
	return (0);
}

int
snmp_client_set_port(struct snmp_client *cl, const char *p)
{
	char *np;

	if (p == NULL) {
		if (cl->cport != NULL)
			free(cl->cport);
		cl->cport = NULL;
	} else {
		if ((np = malloc(strlen(p) + 1)) == NULL)
			return (-1);
		strcpy(np, p);
		if (cl->cport != NULL)
			free(cl->cport);
		cl->cport = np;
	}
	return (0);
}

/*
 * parse a server specification
 *
 * [trans::][community@][server][:port]
 */
int
snmp_parse_server(struct snmp_client *sc, const char *str)
{
	const char *p, *s = str;

	/* look for a double colon */
	for (p = s; *p != '\0'; p++) {
		if (*p == '\\' && p[1] != '\0') {
			p++;
			continue;
		}
		if (*p == ':' && p[1] == ':')
			break;
	}
	if (*p != '\0') {
		if (p > s) {
			if (p - s == 3 && strncmp(s, "udp", 3) == 0)
				sc->trans = SNMP_TRANS_UDP;
			else if (p - s == 6 && strncmp(s, "stream", 6) == 0)
				sc->trans = SNMP_TRANS_LOC_STREAM;
			else if (p - s == 5 && strncmp(s, "dgram", 5) == 0)
				sc->trans = SNMP_TRANS_LOC_DGRAM;
			else {
				seterr(sc, "unknown SNMP transport '%.*s'",
				    (int)(p - s), s);
				return (-1);
			}
		}
		s = p + 2;
	}

	/* look for a @ */
	for (p = s; *p != '\0'; p++) {
		if (*p == '\\' && p[1] != '\0') {
			p++;
			continue;
		}
		if (*p == '@')
			break;
	}

	if (*p != '\0') {
		if (p - s > SNMP_COMMUNITY_MAXLEN) {
			seterr(sc, "community string too long");
			return (-1);
		}
		strncpy(sc->read_community, s, p - s);
		sc->read_community[p - s] = '\0';
		strncpy(sc->write_community, s, p - s);
		sc->write_community[p - s] = '\0';
		s = p + 1;
	}

	/* look for a colon */
	for (p = s; *p != '\0'; p++) {
		if (*p == '\\' && p[1] != '\0') {
			p++;
			continue;
		}
		if (*p == ':')
			break;
	}

	if (*p == ':') {
		if (p > s) {
			/* host:port */
			free(sc->chost);
			if ((sc->chost = malloc(p - s + 1)) == NULL) {
				seterr(sc, "%s", strerror(errno));
				return (-1);
			}
			strncpy(sc->chost, s, p - s);
			sc->chost[p - s] = '\0';
		}
		/* port */
		free(sc->cport);
		if ((sc->cport = strdup(p + 1)) == NULL) {
			seterr(sc, "%s", strerror(errno));
			return (-1);
		}

	} else if (p > s) {
		/* host */
		free(sc->chost);
		if ((sc->chost = strdup(s)) == NULL) {
			seterr(sc, "%s", strerror(errno));
			return (-1);
		}
	}
	return (0);
}
