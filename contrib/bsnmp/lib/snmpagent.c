/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 * $Begemot: bsnmp/lib/snmpagent.c,v 1.20 2005/10/04 11:21:33 brandt_h Exp $
 *
 * SNMP Agent functions
 */
#include <sys/types.h>
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
#include <inttypes.h>
#endif
#include <string.h>

#include "asn1.h"
#include "snmp.h"
#include "snmppriv.h"
#include "snmpagent.h"

static void snmp_debug_func(const char *fmt, ...);

void (*snmp_debug)(const char *fmt, ...) = snmp_debug_func;

struct snmp_node *tree;
u_int  tree_size;

/*
 * Structure to hold dependencies during SET processing
 * The last two members of this structure must be the
 * dependency visible by the user and the user data.
 */
struct depend {
	TAILQ_ENTRY(depend) link;
	size_t	len;		/* size of data part */
	snmp_depop_t	func;
	struct snmp_dependency dep;
#if defined(__GNUC__) && __GNUC__ < 3
	u_char	data[0];
#else
	u_char	data[];
#endif
};
TAILQ_HEAD(depend_list, depend);

/*
 * Set context
 */
struct context {
	struct snmp_context	ctx;
	struct depend_list	dlist;
	const struct snmp_node	*node[SNMP_MAX_BINDINGS];
	struct snmp_scratch	scratch[SNMP_MAX_BINDINGS];
	struct depend		*depend;
};

#define	TR(W)	(snmp_trace & SNMP_TRACE_##W)
u_int snmp_trace = 0;

static char oidbuf[ASN_OIDSTRLEN];

/*
 * Allocate a context
 */
struct snmp_context *
snmp_init_context(void)
{
	struct context *context;

	if ((context = malloc(sizeof(*context))) == NULL)
		return (NULL);

	memset(context, 0, sizeof(*context));
	TAILQ_INIT(&context->dlist);

	return (&context->ctx);
}

/*
 * Find a variable for SET/GET and the first GETBULK pass.
 * Return the node pointer. If the search fails, set the errp to
 * the correct SNMPv2 GET exception code.
 */
static struct snmp_node *
find_node(const struct snmp_value *value, enum snmp_syntax *errp)
{
	struct snmp_node *tp;

	if (TR(FIND))
		snmp_debug("find: searching %s",
		    asn_oid2str_r(&value->var, oidbuf));

	/*
	 * If we have an exact match (the entry in the table is a
	 * sub-oid from the variable) we have found what we are for.
	 * If the table oid is higher than the variable, there is no match.
	 */
	for (tp = tree; tp < tree + tree_size; tp++) {
		if (asn_is_suboid(&tp->oid, &value->var))
			goto found;
		if (asn_compare_oid(&tp->oid, &value->var) >= 0)
			break;
	}

	if (TR(FIND))
		snmp_debug("find: no match");
	*errp = SNMP_SYNTAX_NOSUCHOBJECT;
	return (NULL);

  found:
	/* leafs must have a 0 instance identifier */
	if (tp->type == SNMP_NODE_LEAF &&
	    (value->var.len != tp->oid.len + 1 ||
	     value->var.subs[tp->oid.len] != 0)) {
		if (TR(FIND))
			snmp_debug("find: bad leaf index");
		*errp = SNMP_SYNTAX_NOSUCHINSTANCE;
		return (NULL);
	}
	if (TR(FIND))
		snmp_debug("find: found %s",
		    asn_oid2str_r(&value->var, oidbuf));
	return (tp);
}

static struct snmp_node *
find_subnode(const struct snmp_value *value)
{
	struct snmp_node *tp;

	for (tp = tree; tp < tree + tree_size; tp++) {
		if (asn_is_suboid(&value->var, &tp->oid))
			return (tp);
	}
	return (NULL);
}

static void
snmp_pdu_create_response(const struct snmp_pdu *pdu, struct snmp_pdu *resp)
{
	memset(resp, 0, sizeof(*resp));
	strcpy(resp->community, pdu->community);
	resp->version = pdu->version;
	if (pdu->flags & SNMP_MSG_AUTODISCOVER)
		resp->type = SNMP_PDU_REPORT; /* RFC 3414.4 */
	else
		resp->type = SNMP_PDU_RESPONSE;
	resp->request_id = pdu->request_id;
	resp->version = pdu->version;

	if (resp->version != SNMP_V3)
		return;

	memcpy(&resp->engine, &pdu->engine, sizeof(pdu->engine));
	memcpy(&resp->user, &pdu->user, sizeof(pdu->user));
	snmp_pdu_init_secparams(resp);
	resp->identifier = pdu->identifier;
	resp->security_model = pdu->security_model;
	resp->context_engine_len = pdu->context_engine_len;
	memcpy(resp->context_engine, pdu->context_engine,
	    resp->context_engine_len);
	strlcpy(resp->context_name, pdu->context_name,
	    sizeof(resp->context_name));
}

/*
 * Execute a GET operation. The tree is rooted at the global 'root'.
 * Build the response PDU on the fly. If the return code is SNMP_RET_ERR
 * the pdu error status and index will be set.
 */
enum snmp_ret
snmp_get(struct snmp_pdu *pdu, struct asn_buf *resp_b,
    struct snmp_pdu *resp, void *data)
{
	int ret;
	u_int i;
	struct snmp_node *tp;
	enum snmp_syntax except;
	struct context context;
	enum asn_err err;

	memset(&context, 0, sizeof(context));
	context.ctx.data = data;

	snmp_pdu_create_response(pdu, resp);

	if (snmp_pdu_encode_header(resp_b, resp) != SNMP_CODE_OK)
		/* cannot even encode header - very bad */
		return (SNMP_RET_IGN);

	for (i = 0; i < pdu->nbindings; i++) {
		resp->bindings[i].var = pdu->bindings[i].var;
		if ((tp = find_node(&pdu->bindings[i], &except)) == NULL) {
			if (pdu->version == SNMP_V1) {
				if (TR(GET))
					snmp_debug("get: nosuchname");
				pdu->error_status = SNMP_ERR_NOSUCHNAME;
				pdu->error_index = i + 1;
				snmp_pdu_free(resp);
				return (SNMP_RET_ERR);
			}
			if (TR(GET))
				snmp_debug("get: exception %u", except);
			resp->bindings[i].syntax = except;

		} else {
			/* call the action to fetch the value. */
			resp->bindings[i].syntax = tp->syntax;
			ret = (*tp->op)(&context.ctx, &resp->bindings[i],
			    tp->oid.len, tp->index, SNMP_OP_GET);
			if (TR(GET))
				snmp_debug("get: action returns %d", ret);

			if (ret == SNMP_ERR_NOSUCHNAME) {
				if (pdu->version == SNMP_V1) {
					pdu->error_status = SNMP_ERR_NOSUCHNAME;
					pdu->error_index = i + 1;
					snmp_pdu_free(resp);
					return (SNMP_RET_ERR);
				}
				if (TR(GET))
					snmp_debug("get: exception noSuchInstance");
				resp->bindings[i].syntax = SNMP_SYNTAX_NOSUCHINSTANCE;

			} else if (ret != SNMP_ERR_NOERROR) {
				pdu->error_status = SNMP_ERR_GENERR;
				pdu->error_index = i + 1;
				snmp_pdu_free(resp);
				return (SNMP_RET_ERR);
			}
		}
		resp->nbindings++;

		err = snmp_binding_encode(resp_b, &resp->bindings[i]);

		if (err == ASN_ERR_EOBUF) {
			pdu->error_status = SNMP_ERR_TOOBIG;
			pdu->error_index = 0;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		if (err != ASN_ERR_OK) {
			if (TR(GET))
				snmp_debug("get: binding encoding: %u", err);
			pdu->error_status = SNMP_ERR_GENERR;
			pdu->error_index = i + 1;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
	}

	if (snmp_fix_encoding(resp_b, resp) != SNMP_CODE_OK) {
		snmp_debug("get: failed to encode PDU");
		return (SNMP_RET_ERR);
	}

	return (SNMP_RET_OK);
}

static struct snmp_node *
next_node(const struct snmp_value *value, int *pnext)
{
	struct snmp_node *tp;

	if (TR(FIND))
		snmp_debug("next: searching %s",
		    asn_oid2str_r(&value->var, oidbuf));

	*pnext = 0;
	for (tp = tree; tp < tree + tree_size; tp++) {
		if (asn_is_suboid(&tp->oid, &value->var)) {
			/* the tree OID is a sub-oid of the requested OID. */
			if (tp->type == SNMP_NODE_LEAF) {
				if (tp->oid.len == value->var.len) {
					/* request for scalar type */
					if (TR(FIND))
						snmp_debug("next: found scalar %s",
						    asn_oid2str_r(&tp->oid, oidbuf));
					return (tp);
				}
				/* try next */
			} else {
				if (TR(FIND))
					snmp_debug("next: found column %s",
					    asn_oid2str_r(&tp->oid, oidbuf));
				return (tp);
			}
		} else if (asn_is_suboid(&value->var, &tp->oid) ||
		    asn_compare_oid(&tp->oid, &value->var) >= 0) {
			if (TR(FIND))
				snmp_debug("next: found %s",
				    asn_oid2str_r(&tp->oid, oidbuf));
			*pnext = 1;
			return (tp);
		}
	}

	if (TR(FIND))
		snmp_debug("next: failed");

	return (NULL);
}

static enum snmp_ret
do_getnext(struct context *context, const struct snmp_value *inb,
    struct snmp_value *outb, struct snmp_pdu *pdu)
{
	const struct snmp_node *tp;
	int ret, next;

	if ((tp = next_node(inb, &next)) == NULL)
		goto eofMib;

	/* retain old variable if we are doing a GETNEXT on an exact
	 * matched leaf only */
	if (tp->type == SNMP_NODE_LEAF || next)
		outb->var = tp->oid;
	else
		outb->var = inb->var;

	for (;;) {
		outb->syntax = tp->syntax;
		if (tp->type == SNMP_NODE_LEAF) {
			/* make a GET operation */
			outb->var.subs[outb->var.len++] = 0;
			ret = (*tp->op)(&context->ctx, outb, tp->oid.len,
			    tp->index, SNMP_OP_GET);
		} else {
			/* make a GETNEXT */
			ret = (*tp->op)(&context->ctx, outb, tp->oid.len,
			     tp->index, SNMP_OP_GETNEXT);
		}
		if (ret != SNMP_ERR_NOSUCHNAME) {
			/* got something */
			if (ret != SNMP_ERR_NOERROR && TR(GETNEXT))
				snmp_debug("getnext: %s returns %u",
				    asn_oid2str(&outb->var), ret);
			break;
		}

		/* object has no data - try next */
		if (++tp == tree + tree_size)
			break;

		if (TR(GETNEXT))
			snmp_debug("getnext: no data - avancing to %s",
			    asn_oid2str(&tp->oid));

		outb->var = tp->oid;
	}

	if (ret == SNMP_ERR_NOSUCHNAME) {
  eofMib:
		outb->var = inb->var;
		if (pdu->version == SNMP_V1) {
			pdu->error_status = SNMP_ERR_NOSUCHNAME;
			return (SNMP_RET_ERR);
		}
		outb->syntax = SNMP_SYNTAX_ENDOFMIBVIEW;

	} else if (ret != SNMP_ERR_NOERROR) {
		pdu->error_status = SNMP_ERR_GENERR;
		return (SNMP_RET_ERR);
	}
	return (SNMP_RET_OK);
}


/*
 * Execute a GETNEXT operation. The tree is rooted at the global 'root'.
 * Build the response PDU on the fly. The return is:
 */
enum snmp_ret
snmp_getnext(struct snmp_pdu *pdu, struct asn_buf *resp_b,
    struct snmp_pdu *resp, void *data)
{
	struct context context;
	u_int i;
	enum asn_err err;
	enum snmp_ret result;

	memset(&context, 0, sizeof(context));
	context.ctx.data = data;

	snmp_pdu_create_response(pdu, resp);

	if (snmp_pdu_encode_header(resp_b, resp))
		return (SNMP_RET_IGN);

	for (i = 0; i < pdu->nbindings; i++) {
		result = do_getnext(&context, &pdu->bindings[i],
		    &resp->bindings[i], pdu);

		if (result != SNMP_RET_OK) {
			pdu->error_index = i + 1;
			snmp_pdu_free(resp);
			return (result);
		}

		resp->nbindings++;

		err = snmp_binding_encode(resp_b, &resp->bindings[i]);

		if (err == ASN_ERR_EOBUF) {
			pdu->error_status = SNMP_ERR_TOOBIG;
			pdu->error_index = 0;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		if (err != ASN_ERR_OK) {
			if (TR(GET))
				snmp_debug("getnext: binding encoding: %u", err);
			pdu->error_status = SNMP_ERR_GENERR;
			pdu->error_index = i + 1;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
	}

	if (snmp_fix_encoding(resp_b, resp) != SNMP_CODE_OK) {
		snmp_debug("getnext: failed to encode PDU");
		return (SNMP_RET_ERR);
	}

	return (SNMP_RET_OK);
}

enum snmp_ret
snmp_getbulk(struct snmp_pdu *pdu, struct asn_buf *resp_b,
    struct snmp_pdu *resp, void *data)
{
	struct context context;
	u_int i;
	int cnt;
	u_int non_rep;
	int eomib;
	enum snmp_ret result;
	enum asn_err err;

	memset(&context, 0, sizeof(context));
	context.ctx.data = data;

	snmp_pdu_create_response(pdu, resp);

	if (snmp_pdu_encode_header(resp_b, resp) != SNMP_CODE_OK)
		/* cannot even encode header - very bad */
		return (SNMP_RET_IGN);

	if ((non_rep = pdu->error_status) > pdu->nbindings)
		non_rep = pdu->nbindings;

	/* non-repeaters */
	for (i = 0; i < non_rep; i++) {
		result = do_getnext(&context, &pdu->bindings[i],
		    &resp->bindings[resp->nbindings], pdu);

		if (result != SNMP_RET_OK) {
			pdu->error_index = i + 1;
			snmp_pdu_free(resp);
			return (result);
		}

		err = snmp_binding_encode(resp_b,
		    &resp->bindings[resp->nbindings++]);

		if (err == ASN_ERR_EOBUF)
			goto done;

		if (err != ASN_ERR_OK) {
			if (TR(GET))
				snmp_debug("getnext: binding encoding: %u", err);
			pdu->error_status = SNMP_ERR_GENERR;
			pdu->error_index = i + 1;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
	}

	if (non_rep == pdu->nbindings)
		goto done;

	/* repeates */
	for (cnt = 0; cnt < pdu->error_index; cnt++) {
		eomib = 1;
		for (i = non_rep; i < pdu->nbindings; i++) {

			if (resp->nbindings == SNMP_MAX_BINDINGS)
				/* PDU is full */
				goto done;

			if (cnt == 0)
				result = do_getnext(&context, &pdu->bindings[i],
				    &resp->bindings[resp->nbindings], pdu);
			else
				result = do_getnext(&context,
				    &resp->bindings[resp->nbindings -
				    (pdu->nbindings - non_rep)],
				    &resp->bindings[resp->nbindings], pdu);

			if (result != SNMP_RET_OK) {
				pdu->error_index = i + 1;
				snmp_pdu_free(resp);
				return (result);
			}
			if (resp->bindings[resp->nbindings].syntax !=
			    SNMP_SYNTAX_ENDOFMIBVIEW)
				eomib = 0;

			err = snmp_binding_encode(resp_b,
			    &resp->bindings[resp->nbindings++]);

			if (err == ASN_ERR_EOBUF)
				goto done;

			if (err != ASN_ERR_OK) {
				if (TR(GET))
					snmp_debug("getnext: binding encoding: %u", err);
				pdu->error_status = SNMP_ERR_GENERR;
				pdu->error_index = i + 1;
				snmp_pdu_free(resp);
				return (SNMP_RET_ERR);
			}
		}
		if (eomib)
			break;
	}

  done:
	if (snmp_fix_encoding(resp_b, resp) != SNMP_CODE_OK) {
		snmp_debug("getnext: failed to encode PDU");
		return (SNMP_RET_ERR);
	}

	return (SNMP_RET_OK);
}

/*
 * Rollback a SET operation. Failed index is 'i'.
 */
static void
rollback(struct context *context, struct snmp_pdu *pdu, u_int i)
{
	struct snmp_value *b;
	const struct snmp_node *np;
	int ret;

	while (i-- > 0) {
		b = &pdu->bindings[i];
		np = context->node[i];

		context->ctx.scratch = &context->scratch[i];

		ret = (*np->op)(&context->ctx, b, np->oid.len, np->index,
		    SNMP_OP_ROLLBACK);

		if (ret != SNMP_ERR_NOERROR) {
			snmp_error("set: rollback failed (%d) on variable %s "
			    "index %u", ret, asn_oid2str(&b->var), i);
			if (pdu->version != SNMP_V1) {
				pdu->error_status = SNMP_ERR_UNDO_FAILED;
				pdu->error_index = 0;
			}
		}
	}
}

/*
 * Commit dependencies.
 */
int
snmp_dep_commit(struct snmp_context *ctx)
{
	struct context *context = (struct context *)ctx;
	int ret;

	TAILQ_FOREACH(context->depend, &context->dlist, link) {
		ctx->dep = &context->depend->dep;

		if (TR(SET))
			snmp_debug("set: dependency commit %s",
			    asn_oid2str(&ctx->dep->obj));

		ret = context->depend->func(ctx, ctx->dep, SNMP_DEPOP_COMMIT);

		if (ret != SNMP_ERR_NOERROR) {
			if (TR(SET))
				snmp_debug("set: dependency failed %d", ret);
			return (ret);
		}
	}
	return (SNMP_ERR_NOERROR);
}

/*
 * Rollback dependencies
 */
int
snmp_dep_rollback(struct snmp_context *ctx)
{
	struct context *context = (struct context *)ctx;
	int ret, ret1;
	char objbuf[ASN_OIDSTRLEN];
	char idxbuf[ASN_OIDSTRLEN];

	ret1 = SNMP_ERR_NOERROR;
	while ((context->depend =
	    TAILQ_PREV(context->depend, depend_list, link)) != NULL) {
		ctx->dep = &context->depend->dep;

		if (TR(SET))
			snmp_debug("set: dependency rollback %s",
			    asn_oid2str(&ctx->dep->obj));

		ret = context->depend->func(ctx, ctx->dep, SNMP_DEPOP_ROLLBACK);

		if (ret != SNMP_ERR_NOERROR) {
			snmp_debug("set: dep rollback returns %u: %s %s", ret,
			    asn_oid2str_r(&ctx->dep->obj, objbuf),
			    asn_oid2str_r(&ctx->dep->idx, idxbuf));
			if (ret1 == SNMP_ERR_NOERROR)
				ret1 = ret;
		}
	}
	return (ret1);
}

void
snmp_dep_finish(struct snmp_context *ctx)
{
	struct context *context = (struct context *)ctx;
	struct depend *d;

	while ((d = TAILQ_FIRST(&context->dlist)) != NULL) {
		ctx->dep = &d->dep;
		(void)d->func(ctx, ctx->dep, SNMP_DEPOP_FINISH);
		TAILQ_REMOVE(&context->dlist, d, link);
		free(d);
	}
}

/*
 * Do a SET operation.
 */
enum snmp_ret
snmp_set(struct snmp_pdu *pdu, struct asn_buf *resp_b,
    struct snmp_pdu *resp, void *data)
{
	int ret;
	u_int i;
	enum asn_err asnerr;
	struct context context;
	const struct snmp_node *np;
	struct snmp_value *b;
	enum snmp_syntax except;

	memset(&context, 0, sizeof(context));
	TAILQ_INIT(&context.dlist);
	context.ctx.data = data;

	snmp_pdu_create_response(pdu, resp);

	if (snmp_pdu_encode_header(resp_b, resp))
		return (SNMP_RET_IGN);

	/*
	 * 1. Find all nodes, check that they are writeable and
	 *    that the syntax is ok, copy over the binding to the response.
	 */
	for (i = 0; i < pdu->nbindings; i++) {
		b = &pdu->bindings[i];

		if ((np = context.node[i] = find_node(b, &except)) == NULL) {
			/* not found altogether or LEAF with wrong index */
			if (TR(SET))
				snmp_debug("set: node not found %s",
				    asn_oid2str_r(&b->var, oidbuf));
			if (pdu->version == SNMP_V1) {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_NOSUCHNAME;
			} else if ((np = find_subnode(b)) != NULL) {
				/* 2. intermediate object */
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_NOT_WRITEABLE;
			} else if (except == SNMP_SYNTAX_NOSUCHOBJECT) {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_NO_ACCESS;
			} else {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_NO_CREATION;
			}
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		/*
		 * 2. write/createable?
		 * Can check this for leafs only, because in v2 we have
		 * to differentiate between NOT_WRITEABLE and NO_CREATION
		 * and only the action routine for COLUMNS knows, whether
		 * a column exists.
		 */
		if (np->type == SNMP_NODE_LEAF &&
		    !(np->flags & SNMP_NODE_CANSET)) {
			if (pdu->version == SNMP_V1) {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_NOSUCHNAME;
			} else {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_NOT_WRITEABLE;
			}
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		/*
		 * 3. Ensure the right syntax
		 */
		if (np->syntax != b->syntax) {
			if (pdu->version == SNMP_V1) {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_BADVALUE; /* v2: wrongType */
			} else {
				pdu->error_index = i + 1;
				pdu->error_status = SNMP_ERR_WRONG_TYPE;
			}
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		/*
		 * 4. Copy binding
		 */
		if (snmp_value_copy(&resp->bindings[i], b)) {
			pdu->error_index = i + 1;
			pdu->error_status = SNMP_ERR_GENERR;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		asnerr = snmp_binding_encode(resp_b, &resp->bindings[i]);
		if (asnerr == ASN_ERR_EOBUF) {
			pdu->error_index = i + 1;
			pdu->error_status = SNMP_ERR_TOOBIG;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		} else if (asnerr != ASN_ERR_OK) {
			pdu->error_index = i + 1;
			pdu->error_status = SNMP_ERR_GENERR;
			snmp_pdu_free(resp);
			return (SNMP_RET_ERR);
		}
		resp->nbindings++;
	}

	context.ctx.code = SNMP_RET_OK;

	/*
	 * 2. Call the SET method for each node. If a SET fails, rollback
	 *    everything. Map error codes depending on the version.
	 */
	for (i = 0; i < pdu->nbindings; i++) {
		b = &pdu->bindings[i];
		np = context.node[i];

		context.ctx.var_index = i + 1;
		context.ctx.scratch = &context.scratch[i];

		ret = (*np->op)(&context.ctx, b, np->oid.len, np->index,
		    SNMP_OP_SET);

		if (TR(SET))
			snmp_debug("set: action %s returns %d", np->name, ret);

		if (pdu->version == SNMP_V1) {
			switch (ret) {
			  case SNMP_ERR_NO_ACCESS:
				ret = SNMP_ERR_NOSUCHNAME;
				break;
			  case SNMP_ERR_WRONG_TYPE:
				/* should no happen */
				ret = SNMP_ERR_BADVALUE;
				break;
			  case SNMP_ERR_WRONG_LENGTH:
				ret = SNMP_ERR_BADVALUE;
				break;
			  case SNMP_ERR_WRONG_ENCODING:
				/* should not happen */
				ret = SNMP_ERR_BADVALUE;
				break;
			  case SNMP_ERR_WRONG_VALUE:
				ret = SNMP_ERR_BADVALUE;
				break;
			  case SNMP_ERR_NO_CREATION:
				ret = SNMP_ERR_NOSUCHNAME;
				break;
			  case SNMP_ERR_INCONS_VALUE:
				ret = SNMP_ERR_BADVALUE;
				break;
			  case SNMP_ERR_RES_UNAVAIL:
				ret = SNMP_ERR_GENERR;
				break;
			  case SNMP_ERR_COMMIT_FAILED:
				ret = SNMP_ERR_GENERR;
				break;
			  case SNMP_ERR_UNDO_FAILED:
				ret = SNMP_ERR_GENERR;
				break;
			  case SNMP_ERR_AUTH_ERR:
				/* should not happen */
				ret = SNMP_ERR_GENERR;
				break;
			  case SNMP_ERR_NOT_WRITEABLE:
				ret = SNMP_ERR_NOSUCHNAME;
				break;
			  case SNMP_ERR_INCONS_NAME:
				ret = SNMP_ERR_BADVALUE;
				break;
			}
		}
		if (ret != SNMP_ERR_NOERROR) {
			pdu->error_index = i + 1;
			pdu->error_status = ret;

			rollback(&context, pdu, i);
			snmp_pdu_free(resp);

			context.ctx.code = SNMP_RET_ERR;

			goto errout;
		}
	}

	/*
	 * 3. Call dependencies
	 */
	if (TR(SET))
		snmp_debug("set: set operations ok");

	if ((ret = snmp_dep_commit(&context.ctx)) != SNMP_ERR_NOERROR) {
		pdu->error_status = ret;
		pdu->error_index = context.ctx.var_index;

		if ((ret = snmp_dep_rollback(&context.ctx)) != SNMP_ERR_NOERROR) {
			if (pdu->version != SNMP_V1) {
				pdu->error_status = SNMP_ERR_UNDO_FAILED;
				pdu->error_index = 0;
			}
		}
		rollback(&context, pdu, i);
		snmp_pdu_free(resp);

		context.ctx.code = SNMP_RET_ERR;

		goto errout;
	}

	/*
	 * 4. Commit and copy values from the original packet to the response.
	 *    This is not the commit operation from RFC 1905 but rather an
	 *    'FREE RESOURCES' operation. It shouldn't fail.
	 */
	if (TR(SET))
		snmp_debug("set: commiting");

	for (i = 0; i < pdu->nbindings; i++) {
		b = &resp->bindings[i];
		np = context.node[i];

		context.ctx.var_index = i + 1;
		context.ctx.scratch = &context.scratch[i];

		ret = (*np->op)(&context.ctx, b, np->oid.len, np->index,
		    SNMP_OP_COMMIT);

		if (ret != SNMP_ERR_NOERROR)
			snmp_error("set: commit failed (%d) on"
			    " variable %s index %u", ret,
			    asn_oid2str_r(&b->var, oidbuf), i);
	}

	if (snmp_fix_encoding(resp_b, resp) != SNMP_CODE_OK) {
		snmp_error("set: fix_encoding failed");
		snmp_pdu_free(resp);
		context.ctx.code = SNMP_RET_IGN;
	}

	/*
	 * Done
	 */
  errout:
	snmp_dep_finish(&context.ctx);

	if (TR(SET))
		snmp_debug("set: returning %d", context.ctx.code);

	return (context.ctx.code);
}
/*
 * Lookup a dependency. If it doesn't exist, create one
 */
struct snmp_dependency *
snmp_dep_lookup(struct snmp_context *ctx, const struct asn_oid *obj,
    const struct asn_oid *idx, size_t len, snmp_depop_t func)
{
	struct context *context;
	struct depend *d;

	context = (struct context *)(void *)
	    ((char *)ctx - offsetof(struct context, ctx));
	if (TR(DEPEND)) {
		snmp_debug("depend: looking for %s", asn_oid2str(obj));
		if (idx)
			snmp_debug("depend: index is %s", asn_oid2str(idx));
	}
	TAILQ_FOREACH(d, &context->dlist, link)
		if (asn_compare_oid(obj, &d->dep.obj) == 0 &&
		    ((idx == NULL && d->dep.idx.len == 0) ||
		     (idx != NULL && asn_compare_oid(idx, &d->dep.idx) == 0))) {
			if(TR(DEPEND))
				snmp_debug("depend: found");
			return (&d->dep);
		}

	if(TR(DEPEND))
		snmp_debug("depend: creating");

	if ((d = malloc(offsetof(struct depend, dep) + len)) == NULL)
		return (NULL);
	memset(&d->dep, 0, len);

	d->dep.obj = *obj;
	if (idx == NULL)
		d->dep.idx.len = 0;
	else
		d->dep.idx = *idx;
	d->len = len;
	d->func = func;

	TAILQ_INSERT_TAIL(&context->dlist, d, link);

	return (&d->dep);
}

/*
 * Make an error response from a PDU. We do this without decoding the
 * variable bindings. This means we can sent the junk back to a caller
 * that has sent us junk in the first place.
 */
enum snmp_ret
snmp_make_errresp(const struct snmp_pdu *pdu, struct asn_buf *pdu_b,
    struct asn_buf *resp_b)
{
	u_char type;
	asn_len_t len;
	struct snmp_pdu resp;
	enum asn_err err;
	enum snmp_code code;

	snmp_pdu_create_response(pdu, &resp);

	if ((code = snmp_pdu_decode_header(pdu_b, &resp)) != SNMP_CODE_OK)
		return (SNMP_RET_IGN);

	if (pdu->version == SNMP_V3) {
		if (resp.user.priv_proto != SNMP_PRIV_NOPRIV &&
		   (asn_get_header(pdu_b, &type, &resp.scoped_len) != ASN_ERR_OK
		   || type != ASN_TYPE_OCTETSTRING)) {
			snmp_error("cannot decode encrypted pdu");
			return (SNMP_RET_IGN);
		}

		if (asn_get_sequence(pdu_b, &len) != ASN_ERR_OK) {
			snmp_error("cannot decode scoped pdu header");
			return (SNMP_RET_IGN);
		}

		len = SNMP_ENGINE_ID_SIZ;
		if (asn_get_octetstring(pdu_b, (u_char *)resp.context_engine,
		    &len) != ASN_ERR_OK) {
			snmp_error("cannot decode msg context engine");
			return (SNMP_RET_IGN);
		}
		resp.context_engine_len = len;
		len = SNMP_CONTEXT_NAME_SIZ;
		if (asn_get_octetstring(pdu_b, (u_char *)resp.context_name,
		    &len) != ASN_ERR_OK) {
			snmp_error("cannot decode msg context name");
			return (SNMP_RET_IGN);
		}
		resp.context_name[len] = '\0';
	}


	if (asn_get_header(pdu_b, &type, &len) != ASN_ERR_OK) {
		snmp_error("cannot get pdu header");
		return (SNMP_RET_IGN);
	}

	if ((type & ~ASN_TYPE_MASK) !=
	    (ASN_TYPE_CONSTRUCTED | ASN_CLASS_CONTEXT)) {
		snmp_error("bad pdu header tag");
		return (SNMP_RET_IGN);
	}

	err = snmp_parse_pdus_hdr(pdu_b, &resp, &len);
	if (ASN_ERR_STOPPED(err))
		return (SNMP_RET_IGN);
	if (pdu_b->asn_len < len)
		return (SNMP_RET_IGN);
	pdu_b->asn_len = len;

	/* now we have the bindings left - construct new message */
	resp.error_status = pdu->error_status;
	resp.error_index = pdu->error_index;
	resp.type = SNMP_PDU_RESPONSE;

	code = snmp_pdu_encode_header(resp_b, &resp);
	if (code != SNMP_CODE_OK)
		return (SNMP_RET_IGN);

	if (pdu_b->asn_len > resp_b->asn_len)
		/* too short */
		return (SNMP_RET_IGN);
	(void)memcpy(resp_b->asn_ptr, pdu_b->asn_cptr, pdu_b->asn_len);
	resp_b->asn_len -= pdu_b->asn_len;
	resp_b->asn_ptr += pdu_b->asn_len;

	code = snmp_fix_encoding(resp_b, &resp);
	if (code != SNMP_CODE_OK)
		return (SNMP_RET_IGN);

	return (SNMP_RET_OK);
}

static void
snmp_debug_func(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}
