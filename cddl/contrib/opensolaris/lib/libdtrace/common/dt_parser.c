/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2013, Joyent Inc. All rights reserved.
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * DTrace D Language Parser
 *
 * The D Parser is a lex/yacc parser consisting of the lexer dt_lex.l, the
 * parsing grammar dt_grammar.y, and this file, dt_parser.c, which handles
 * the construction of the parse tree nodes and their syntactic validation.
 * The parse tree is constructed of dt_node_t structures (see <dt_parser.h>)
 * that are built in two passes: (1) the "create" pass, where the parse tree
 * nodes are allocated by calls from the grammar to dt_node_*() subroutines,
 * and (2) the "cook" pass, where nodes are coalesced, assigned D types, and
 * validated according to the syntactic rules of the language.
 *
 * All node allocations are performed using dt_node_alloc().  All node frees
 * during the parsing phase are performed by dt_node_free(), which frees node-
 * internal state but does not actually free the nodes.  All final node frees
 * are done as part of the end of dt_compile() or as part of destroying
 * persistent identifiers or translators which have embedded nodes.
 *
 * The dt_node_* routines that implement pass (1) may allocate new nodes.  The
 * dt_cook_* routines that implement pass (2) may *not* allocate new nodes.
 * They may free existing nodes using dt_node_free(), but they may not actually
 * deallocate any dt_node_t's.  Currently dt_cook_op2() is an exception to this
 * rule: see the comments therein for how this issue is resolved.
 *
 * The dt_cook_* routines are responsible for (at minimum) setting the final
 * node type (dn_ctfp/dn_type) and attributes (dn_attr).  If dn_ctfp/dn_type
 * are set manually (i.e. not by one of the type assignment functions), then
 * the DT_NF_COOKED flag must be set manually on the node.
 *
 * The cooking pass can be applied to the same parse tree more than once (used
 * in the case of a comma-separated list of probe descriptions).  As such, the
 * cook routines must not perform any parse tree transformations which would
 * be invalid if the tree were subsequently cooked using a different context.
 *
 * The dn_ctfp and dn_type fields form the type of the node.  This tuple can
 * take on the following set of values, which form our type invariants:
 *
 * 1. dn_ctfp = NULL, dn_type = CTF_ERR
 *
 *    In this state, the node has unknown type and is not yet cooked.  The
 *    DT_NF_COOKED flag is not yet set on the node.
 *
 * 2. dn_ctfp = DT_DYN_CTFP(dtp), dn_type = DT_DYN_TYPE(dtp)
 *
 *    In this state, the node is a dynamic D type.  This means that generic
 *    operations are not valid on this node and only code that knows how to
 *    examine the inner details of the node can operate on it.  A <DYN> node
 *    must have dn_ident set to point to an identifier describing the object
 *    and its type.  The DT_NF_REF flag is set for all nodes of type <DYN>.
 *    At present, the D compiler uses the <DYN> type for:
 *
 *    - associative arrays that do not yet have a value type defined
 *    - translated data (i.e. the result of the xlate operator)
 *    - aggregations
 *
 * 3. dn_ctfp = DT_STR_CTFP(dtp), dn_type = DT_STR_TYPE(dtp)
 *
 *    In this state, the node is of type D string.  The string type is really
 *    a char[0] typedef, but requires special handling throughout the compiler.
 *
 * 4. dn_ctfp != NULL, dn_type = any other type ID
 *
 *    In this state, the node is of some known D/CTF type.  The normal libctf
 *    APIs can be used to learn more about the type name or structure.  When
 *    the type is assigned, the DT_NF_SIGNED, DT_NF_REF, and DT_NF_BITFIELD
 *    flags cache the corresponding attributes of the underlying CTF type.
 */

#include <sys/param.h>
#include <sys/sysmacros.h>
#include <limits.h>
#include <setjmp.h>
#include <strings.h>
#include <assert.h>
#ifdef illumos
#include <alloca.h>
#endif
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#include <dt_impl.h>
#include <dt_grammar.h>
#include <dt_module.h>
#include <dt_provider.h>
#include <dt_string.h>
#include <dt_as.h>

dt_pcb_t *yypcb;	/* current control block for parser */
dt_node_t *yypragma;	/* lex token list for control lines */
char yyintprefix;	/* int token macro prefix (+/-) */
char yyintsuffix[4];	/* int token suffix string [uU][lL] */
int yyintdecimal;	/* int token format flag (1=decimal, 0=octal/hex) */

static const char *
opstr(int op)
{
	switch (op) {
	case DT_TOK_COMMA:	return (",");
	case DT_TOK_ELLIPSIS:	return ("...");
	case DT_TOK_ASGN:	return ("=");
	case DT_TOK_ADD_EQ:	return ("+=");
	case DT_TOK_SUB_EQ:	return ("-=");
	case DT_TOK_MUL_EQ:	return ("*=");
	case DT_TOK_DIV_EQ:	return ("/=");
	case DT_TOK_MOD_EQ:	return ("%=");
	case DT_TOK_AND_EQ:	return ("&=");
	case DT_TOK_XOR_EQ:	return ("^=");
	case DT_TOK_OR_EQ:	return ("|=");
	case DT_TOK_LSH_EQ:	return ("<<=");
	case DT_TOK_RSH_EQ:	return (">>=");
	case DT_TOK_QUESTION:	return ("?");
	case DT_TOK_COLON:	return (":");
	case DT_TOK_LOR:	return ("||");
	case DT_TOK_LXOR:	return ("^^");
	case DT_TOK_LAND:	return ("&&");
	case DT_TOK_BOR:	return ("|");
	case DT_TOK_XOR:	return ("^");
	case DT_TOK_BAND:	return ("&");
	case DT_TOK_EQU:	return ("==");
	case DT_TOK_NEQ:	return ("!=");
	case DT_TOK_LT:		return ("<");
	case DT_TOK_LE:		return ("<=");
	case DT_TOK_GT:		return (">");
	case DT_TOK_GE:		return (">=");
	case DT_TOK_LSH:	return ("<<");
	case DT_TOK_RSH:	return (">>");
	case DT_TOK_ADD:	return ("+");
	case DT_TOK_SUB:	return ("-");
	case DT_TOK_MUL:	return ("*");
	case DT_TOK_DIV:	return ("/");
	case DT_TOK_MOD:	return ("%");
	case DT_TOK_LNEG:	return ("!");
	case DT_TOK_BNEG:	return ("~");
	case DT_TOK_ADDADD:	return ("++");
	case DT_TOK_PREINC:	return ("++");
	case DT_TOK_POSTINC:	return ("++");
	case DT_TOK_SUBSUB:	return ("--");
	case DT_TOK_PREDEC:	return ("--");
	case DT_TOK_POSTDEC:	return ("--");
	case DT_TOK_IPOS:	return ("+");
	case DT_TOK_INEG:	return ("-");
	case DT_TOK_DEREF:	return ("*");
	case DT_TOK_ADDROF:	return ("&");
	case DT_TOK_OFFSETOF:	return ("offsetof");
	case DT_TOK_SIZEOF:	return ("sizeof");
	case DT_TOK_STRINGOF:	return ("stringof");
	case DT_TOK_XLATE:	return ("xlate");
	case DT_TOK_LPAR:	return ("(");
	case DT_TOK_RPAR:	return (")");
	case DT_TOK_LBRAC:	return ("[");
	case DT_TOK_RBRAC:	return ("]");
	case DT_TOK_PTR:	return ("->");
	case DT_TOK_DOT:	return (".");
	case DT_TOK_STRING:	return ("<string>");
	case DT_TOK_IDENT:	return ("<ident>");
	case DT_TOK_TNAME:	return ("<type>");
	case DT_TOK_INT:	return ("<int>");
	default:		return ("<?>");
	}
}

int
dt_type_lookup(const char *s, dtrace_typeinfo_t *tip)
{
	static const char delimiters[] = " \t\n\r\v\f*`";
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	const char *p, *q, *r, *end, *obj;

	for (p = s, end = s + strlen(s); *p != '\0'; p = q) {
		while (isspace(*p))
			p++;	/* skip leading whitespace prior to token */

		if (p == end || (q = strpbrk(p + 1, delimiters)) == NULL)
			break;	/* empty string or single token remaining */

		if (*q == '`') {
			char *object = alloca((size_t)(q - p) + 1);
			char *type = alloca((size_t)(end - s) + 1);

			/*
			 * Copy from the start of the token (p) to the location
			 * backquote (q) to extract the nul-terminated object.
			 */
			bcopy(p, object, (size_t)(q - p));
			object[(size_t)(q - p)] = '\0';

			/*
			 * Copy the original string up to the start of this
			 * token (p) into type, and then concatenate everything
			 * after q.  This is the type name without the object.
			 */
			bcopy(s, type, (size_t)(p - s));
			bcopy(q + 1, type + (size_t)(p - s), strlen(q + 1) + 1);

			/*
			 * There may be at most three delimeters. The second
			 * delimeter is usually used to distinguish the type
			 * within a given module, however, there could be a link
			 * map id on the scene in which case that delimeter
			 * would be the third. We determine presence of the lmid
			 * if it rouglhly meets the from LM[0-9]
			 */
			if ((r = strchr(q + 1, '`')) != NULL &&
			    ((r = strchr(r + 1, '`')) != NULL)) {
				if (strchr(r + 1, '`') != NULL)
					return (dt_set_errno(dtp,
					    EDT_BADSCOPE));
				if (q[1] != 'L' || q[2] != 'M')
					return (dt_set_errno(dtp,
					    EDT_BADSCOPE));
			}

			return (dtrace_lookup_by_type(dtp, object, type, tip));
		}
	}

	if (yypcb->pcb_idepth != 0)
		obj = DTRACE_OBJ_CDEFS;
	else
		obj = DTRACE_OBJ_EVERY;

	return (dtrace_lookup_by_type(dtp, obj, s, tip));
}

/*
 * When we parse type expressions or parse an expression with unary "&", we
 * need to find a type that is a pointer to a previously known type.
 * Unfortunately CTF is limited to a per-container view, so ctf_type_pointer()
 * alone does not suffice for our needs.  We provide a more intelligent wrapper
 * for the compiler that attempts to compute a pointer to either the given type
 * or its base (that is, we try both "foo_t *" and "struct foo *"), and also
 * to potentially construct the required type on-the-fly.
 */
int
dt_type_pointer(dtrace_typeinfo_t *tip)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	ctf_file_t *ctfp = tip->dtt_ctfp;
	ctf_id_t type = tip->dtt_type;
	ctf_id_t base = ctf_type_resolve(ctfp, type);
	uint_t bflags = tip->dtt_flags;

	dt_module_t *dmp;
	ctf_id_t ptr;

	if ((ptr = ctf_type_pointer(ctfp, type)) != CTF_ERR ||
	    (ptr = ctf_type_pointer(ctfp, base)) != CTF_ERR) {
		tip->dtt_type = ptr;
		return (0);
	}

	if (yypcb->pcb_idepth != 0)
		dmp = dtp->dt_cdefs;
	else
		dmp = dtp->dt_ddefs;

	if (ctfp != dmp->dm_ctfp && ctfp != ctf_parent_file(dmp->dm_ctfp) &&
	    (type = ctf_add_type(dmp->dm_ctfp, ctfp, type)) == CTF_ERR) {
		dtp->dt_ctferr = ctf_errno(dmp->dm_ctfp);
		return (dt_set_errno(dtp, EDT_CTF));
	}

	ptr = ctf_add_pointer(dmp->dm_ctfp, CTF_ADD_ROOT, type);

	if (ptr == CTF_ERR || ctf_update(dmp->dm_ctfp) == CTF_ERR) {
		dtp->dt_ctferr = ctf_errno(dmp->dm_ctfp);
		return (dt_set_errno(dtp, EDT_CTF));
	}

	tip->dtt_object = dmp->dm_name;
	tip->dtt_ctfp = dmp->dm_ctfp;
	tip->dtt_type = ptr;
	tip->dtt_flags = bflags;

	return (0);
}

const char *
dt_type_name(ctf_file_t *ctfp, ctf_id_t type, char *buf, size_t len)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;

	if (ctfp == DT_FPTR_CTFP(dtp) && type == DT_FPTR_TYPE(dtp))
		(void) snprintf(buf, len, "function pointer");
	else if (ctfp == DT_FUNC_CTFP(dtp) && type == DT_FUNC_TYPE(dtp))
		(void) snprintf(buf, len, "function");
	else if (ctfp == DT_DYN_CTFP(dtp) && type == DT_DYN_TYPE(dtp))
		(void) snprintf(buf, len, "dynamic variable");
	else if (ctfp == NULL)
		(void) snprintf(buf, len, "<none>");
	else if (ctf_type_name(ctfp, type, buf, len) == NULL)
		(void) snprintf(buf, len, "unknown");

	return (buf);
}

/*
 * Perform the "usual arithmetic conversions" to determine which of the two
 * input operand types should be promoted and used as a result type.  The
 * rules for this are described in ISOC[6.3.1.8] and K&R[A6.5].
 */
static void
dt_type_promote(dt_node_t *lp, dt_node_t *rp, ctf_file_t **ofp, ctf_id_t *otype)
{
	ctf_file_t *lfp = lp->dn_ctfp;
	ctf_id_t ltype = lp->dn_type;

	ctf_file_t *rfp = rp->dn_ctfp;
	ctf_id_t rtype = rp->dn_type;

	ctf_id_t lbase = ctf_type_resolve(lfp, ltype);
	uint_t lkind = ctf_type_kind(lfp, lbase);

	ctf_id_t rbase = ctf_type_resolve(rfp, rtype);
	uint_t rkind = ctf_type_kind(rfp, rbase);

	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	ctf_encoding_t le, re;
	uint_t lrank, rrank;

	assert(lkind == CTF_K_INTEGER || lkind == CTF_K_ENUM);
	assert(rkind == CTF_K_INTEGER || rkind == CTF_K_ENUM);

	if (lkind == CTF_K_ENUM) {
		lfp = DT_INT_CTFP(dtp);
		ltype = lbase = DT_INT_TYPE(dtp);
	}

	if (rkind == CTF_K_ENUM) {
		rfp = DT_INT_CTFP(dtp);
		rtype = rbase = DT_INT_TYPE(dtp);
	}

	if (ctf_type_encoding(lfp, lbase, &le) == CTF_ERR) {
		yypcb->pcb_hdl->dt_ctferr = ctf_errno(lfp);
		longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
	}

	if (ctf_type_encoding(rfp, rbase, &re) == CTF_ERR) {
		yypcb->pcb_hdl->dt_ctferr = ctf_errno(rfp);
		longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
	}

	/*
	 * Compute an integer rank based on the size and unsigned status.
	 * If rank is identical, pick the "larger" of the equivalent types
	 * which we define as having a larger base ctf_id_t.  If rank is
	 * different, pick the type with the greater rank.
	 */
	lrank = le.cte_bits + ((le.cte_format & CTF_INT_SIGNED) == 0);
	rrank = re.cte_bits + ((re.cte_format & CTF_INT_SIGNED) == 0);

	if (lrank == rrank) {
		if (lbase - rbase < 0)
			goto return_rtype;
		else
			goto return_ltype;
	} else if (lrank > rrank) {
		goto return_ltype;
	} else
		goto return_rtype;

return_ltype:
	*ofp = lfp;
	*otype = ltype;
	return;

return_rtype:
	*ofp = rfp;
	*otype = rtype;
}

void
dt_node_promote(dt_node_t *lp, dt_node_t *rp, dt_node_t *dnp)
{
	dt_type_promote(lp, rp, &dnp->dn_ctfp, &dnp->dn_type);
	dt_node_type_assign(dnp, dnp->dn_ctfp, dnp->dn_type, B_FALSE);
	dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));
}

const char *
dt_node_name(const dt_node_t *dnp, char *buf, size_t len)
{
	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	const char *prefix = "", *suffix = "";
	const dtrace_syminfo_t *dts;
	char *s;

	switch (dnp->dn_kind) {
	case DT_NODE_INT:
		(void) snprintf(buf, len, "integer constant 0x%llx",
		    (u_longlong_t)dnp->dn_value);
		break;
	case DT_NODE_STRING:
		s = strchr2esc(dnp->dn_string, strlen(dnp->dn_string));
		(void) snprintf(buf, len, "string constant \"%s\"",
		    s != NULL ? s : dnp->dn_string);
		free(s);
		break;
	case DT_NODE_IDENT:
		(void) snprintf(buf, len, "identifier %s", dnp->dn_string);
		break;
	case DT_NODE_VAR:
	case DT_NODE_FUNC:
	case DT_NODE_AGG:
	case DT_NODE_INLINE:
		switch (dnp->dn_ident->di_kind) {
		case DT_IDENT_FUNC:
		case DT_IDENT_AGGFUNC:
		case DT_IDENT_ACTFUNC:
			suffix = "( )";
			break;
		case DT_IDENT_AGG:
			prefix = "@";
			break;
		}
		(void) snprintf(buf, len, "%s %s%s%s",
		    dt_idkind_name(dnp->dn_ident->di_kind),
		    prefix, dnp->dn_ident->di_name, suffix);
		break;
	case DT_NODE_SYM:
		dts = dnp->dn_ident->di_data;
		(void) snprintf(buf, len, "symbol %s`%s",
		    dts->dts_object, dts->dts_name);
		break;
	case DT_NODE_TYPE:
		(void) snprintf(buf, len, "type %s",
		    dt_node_type_name(dnp, n1, sizeof (n1)));
		break;
	case DT_NODE_OP1:
	case DT_NODE_OP2:
	case DT_NODE_OP3:
		(void) snprintf(buf, len, "operator %s", opstr(dnp->dn_op));
		break;
	case DT_NODE_DEXPR:
	case DT_NODE_DFUNC:
		if (dnp->dn_expr)
			return (dt_node_name(dnp->dn_expr, buf, len));
		(void) snprintf(buf, len, "%s", "statement");
		break;
	case DT_NODE_PDESC:
		if (dnp->dn_desc->dtpd_id == 0) {
			(void) snprintf(buf, len,
			    "probe description %s:%s:%s:%s",
			    dnp->dn_desc->dtpd_provider, dnp->dn_desc->dtpd_mod,
			    dnp->dn_desc->dtpd_func, dnp->dn_desc->dtpd_name);
		} else {
			(void) snprintf(buf, len, "probe description %u",
			    dnp->dn_desc->dtpd_id);
		}
		break;
	case DT_NODE_CLAUSE:
		(void) snprintf(buf, len, "%s", "clause");
		break;
	case DT_NODE_MEMBER:
		(void) snprintf(buf, len, "member %s", dnp->dn_membname);
		break;
	case DT_NODE_XLATOR:
		(void) snprintf(buf, len, "translator <%s> (%s)",
		    dt_type_name(dnp->dn_xlator->dx_dst_ctfp,
			dnp->dn_xlator->dx_dst_type, n1, sizeof (n1)),
		    dt_type_name(dnp->dn_xlator->dx_src_ctfp,
			dnp->dn_xlator->dx_src_type, n2, sizeof (n2)));
		break;
	case DT_NODE_PROG:
		(void) snprintf(buf, len, "%s", "program");
		break;
	default:
		(void) snprintf(buf, len, "node <%u>", dnp->dn_kind);
		break;
	}

	return (buf);
}

/*
 * dt_node_xalloc() can be used to create new parse nodes from any libdtrace
 * caller.  The caller is responsible for assigning dn_link appropriately.
 */
dt_node_t *
dt_node_xalloc(dtrace_hdl_t *dtp, int kind)
{
	dt_node_t *dnp = dt_alloc(dtp, sizeof (dt_node_t));

	if (dnp == NULL)
		return (NULL);

	dnp->dn_ctfp = NULL;
	dnp->dn_type = CTF_ERR;
	dnp->dn_kind = (uchar_t)kind;
	dnp->dn_flags = 0;
	dnp->dn_op = 0;
	dnp->dn_line = -1;
	dnp->dn_reg = -1;
	dnp->dn_attr = _dtrace_defattr;
	dnp->dn_list = NULL;
	dnp->dn_link = NULL;
	bzero(&dnp->dn_u, sizeof (dnp->dn_u));

	return (dnp);
}

/*
 * dt_node_alloc() is used to create new parse nodes from the parser.  It
 * assigns the node location based on the current lexer line number and places
 * the new node on the default allocation list.  If allocation fails, we
 * automatically longjmp the caller back to the enclosing compilation call.
 */
static dt_node_t *
dt_node_alloc(int kind)
{
	dt_node_t *dnp = dt_node_xalloc(yypcb->pcb_hdl, kind);

	if (dnp == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	dnp->dn_line = yylineno;
	dnp->dn_link = yypcb->pcb_list;
	yypcb->pcb_list = dnp;

	return (dnp);
}

void
dt_node_free(dt_node_t *dnp)
{
	uchar_t kind = dnp->dn_kind;

	dnp->dn_kind = DT_NODE_FREE;

	switch (kind) {
	case DT_NODE_STRING:
	case DT_NODE_IDENT:
	case DT_NODE_TYPE:
		free(dnp->dn_string);
		dnp->dn_string = NULL;
		break;

	case DT_NODE_VAR:
	case DT_NODE_FUNC:
	case DT_NODE_PROBE:
		if (dnp->dn_ident != NULL) {
			if (dnp->dn_ident->di_flags & DT_IDFLG_ORPHAN)
				dt_ident_destroy(dnp->dn_ident);
			dnp->dn_ident = NULL;
		}
		dt_node_list_free(&dnp->dn_args);
		break;

	case DT_NODE_OP1:
		if (dnp->dn_child != NULL) {
			dt_node_free(dnp->dn_child);
			dnp->dn_child = NULL;
		}
		break;

	case DT_NODE_OP3:
		if (dnp->dn_expr != NULL) {
			dt_node_free(dnp->dn_expr);
			dnp->dn_expr = NULL;
		}
		/*FALLTHRU*/
	case DT_NODE_OP2:
		if (dnp->dn_left != NULL) {
			dt_node_free(dnp->dn_left);
			dnp->dn_left = NULL;
		}
		if (dnp->dn_right != NULL) {
			dt_node_free(dnp->dn_right);
			dnp->dn_right = NULL;
		}
		break;

	case DT_NODE_DEXPR:
	case DT_NODE_DFUNC:
		if (dnp->dn_expr != NULL) {
			dt_node_free(dnp->dn_expr);
			dnp->dn_expr = NULL;
		}
		break;

	case DT_NODE_AGG:
		if (dnp->dn_aggfun != NULL) {
			dt_node_free(dnp->dn_aggfun);
			dnp->dn_aggfun = NULL;
		}
		dt_node_list_free(&dnp->dn_aggtup);
		break;

	case DT_NODE_PDESC:
		free(dnp->dn_spec);
		dnp->dn_spec = NULL;
		free(dnp->dn_desc);
		dnp->dn_desc = NULL;
		break;

	case DT_NODE_CLAUSE:
		if (dnp->dn_pred != NULL)
			dt_node_free(dnp->dn_pred);
		if (dnp->dn_locals != NULL)
			dt_idhash_destroy(dnp->dn_locals);
		dt_node_list_free(&dnp->dn_pdescs);
		dt_node_list_free(&dnp->dn_acts);
		break;

	case DT_NODE_MEMBER:
		free(dnp->dn_membname);
		dnp->dn_membname = NULL;
		if (dnp->dn_membexpr != NULL) {
			dt_node_free(dnp->dn_membexpr);
			dnp->dn_membexpr = NULL;
		}
		break;

	case DT_NODE_PROVIDER:
		dt_node_list_free(&dnp->dn_probes);
		free(dnp->dn_provname);
		dnp->dn_provname = NULL;
		break;

	case DT_NODE_PROG:
		dt_node_list_free(&dnp->dn_list);
		break;
	}
}

void
dt_node_attr_assign(dt_node_t *dnp, dtrace_attribute_t attr)
{
	if ((yypcb->pcb_cflags & DTRACE_C_EATTR) &&
	    (dt_attr_cmp(attr, yypcb->pcb_amin) < 0)) {
		char a[DTRACE_ATTR2STR_MAX];
		char s[BUFSIZ];

		dnerror(dnp, D_ATTR_MIN, "attributes for %s (%s) are less than "
		    "predefined minimum\n", dt_node_name(dnp, s, sizeof (s)),
		    dtrace_attr2str(attr, a, sizeof (a)));
	}

	dnp->dn_attr = attr;
}

void
dt_node_type_assign(dt_node_t *dnp, ctf_file_t *fp, ctf_id_t type,
    boolean_t user)
{
	ctf_id_t base = ctf_type_resolve(fp, type);
	uint_t kind = ctf_type_kind(fp, base);
	ctf_encoding_t e;

	dnp->dn_flags &=
	    ~(DT_NF_SIGNED | DT_NF_REF | DT_NF_BITFIELD | DT_NF_USERLAND);

	if (kind == CTF_K_INTEGER && ctf_type_encoding(fp, base, &e) == 0) {
		size_t size = e.cte_bits / NBBY;

		if (size > 8 || (e.cte_bits % NBBY) != 0 || (size & (size - 1)))
			dnp->dn_flags |= DT_NF_BITFIELD;

		if (e.cte_format & CTF_INT_SIGNED)
			dnp->dn_flags |= DT_NF_SIGNED;
	}

	if (kind == CTF_K_FLOAT && ctf_type_encoding(fp, base, &e) == 0) {
		if (e.cte_bits / NBBY > sizeof (uint64_t))
			dnp->dn_flags |= DT_NF_REF;
	}

	if (kind == CTF_K_STRUCT || kind == CTF_K_UNION ||
	    kind == CTF_K_FORWARD ||
	    kind == CTF_K_ARRAY || kind == CTF_K_FUNCTION)
		dnp->dn_flags |= DT_NF_REF;
	else if (yypcb != NULL && fp == DT_DYN_CTFP(yypcb->pcb_hdl) &&
	    type == DT_DYN_TYPE(yypcb->pcb_hdl))
		dnp->dn_flags |= DT_NF_REF;

	if (user)
		dnp->dn_flags |= DT_NF_USERLAND;

	dnp->dn_flags |= DT_NF_COOKED;
	dnp->dn_ctfp = fp;
	dnp->dn_type = type;
}

void
dt_node_type_propagate(const dt_node_t *src, dt_node_t *dst)
{
	assert(src->dn_flags & DT_NF_COOKED);
	dst->dn_flags = src->dn_flags & ~DT_NF_LVALUE;
	dst->dn_ctfp = src->dn_ctfp;
	dst->dn_type = src->dn_type;
}

const char *
dt_node_type_name(const dt_node_t *dnp, char *buf, size_t len)
{
	if (dt_node_is_dynamic(dnp) && dnp->dn_ident != NULL) {
		(void) snprintf(buf, len, "%s",
		    dt_idkind_name(dt_ident_resolve(dnp->dn_ident)->di_kind));
		return (buf);
	}

	if (dnp->dn_flags & DT_NF_USERLAND) {
		size_t n = snprintf(buf, len, "userland ");
		len = len > n ? len - n : 0;
		(void) dt_type_name(dnp->dn_ctfp, dnp->dn_type, buf + n, len);
		return (buf);
	}

	return (dt_type_name(dnp->dn_ctfp, dnp->dn_type, buf, len));
}

size_t
dt_node_type_size(const dt_node_t *dnp)
{
	ctf_id_t base;
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;

	if (dnp->dn_kind == DT_NODE_STRING)
		return (strlen(dnp->dn_string) + 1);

	if (dt_node_is_dynamic(dnp) && dnp->dn_ident != NULL)
		return (dt_ident_size(dnp->dn_ident));

	base = ctf_type_resolve(dnp->dn_ctfp, dnp->dn_type);

	if (ctf_type_kind(dnp->dn_ctfp, base) == CTF_K_FORWARD)
		return (0);

	/*
	 * Here we have a 32-bit user pointer that is being used with a 64-bit
	 * kernel. When we're using it and its tagged as a userland reference --
	 * then we need to keep it as a 32-bit pointer. However, if we are
	 * referring to it as a kernel address, eg. being used after a copyin()
	 * then we need to make sure that we actually return the kernel's size
	 * of a pointer, 8 bytes.
	 */
	if (ctf_type_kind(dnp->dn_ctfp, base) == CTF_K_POINTER &&
	    ctf_getmodel(dnp->dn_ctfp) == CTF_MODEL_ILP32 &&
	    !(dnp->dn_flags & DT_NF_USERLAND) &&
	    dtp->dt_conf.dtc_ctfmodel == CTF_MODEL_LP64)
			return (8);

	return (ctf_type_size(dnp->dn_ctfp, dnp->dn_type));
}

/*
 * Determine if the specified parse tree node references an identifier of the
 * specified kind, and if so return a pointer to it; otherwise return NULL.
 * This function resolves the identifier itself, following through any inlines.
 */
dt_ident_t *
dt_node_resolve(const dt_node_t *dnp, uint_t idkind)
{
	dt_ident_t *idp;

	switch (dnp->dn_kind) {
	case DT_NODE_VAR:
	case DT_NODE_SYM:
	case DT_NODE_FUNC:
	case DT_NODE_AGG:
	case DT_NODE_INLINE:
	case DT_NODE_PROBE:
		idp = dt_ident_resolve(dnp->dn_ident);
		return (idp->di_kind == idkind ? idp : NULL);
	}

	if (dt_node_is_dynamic(dnp)) {
		idp = dt_ident_resolve(dnp->dn_ident);
		return (idp->di_kind == idkind ? idp : NULL);
	}

	return (NULL);
}

size_t
dt_node_sizeof(const dt_node_t *dnp)
{
	dtrace_syminfo_t *sip;
	GElf_Sym sym;
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;

	/*
	 * The size of the node as used for the sizeof() operator depends on
	 * the kind of the node.  If the node is a SYM, the size is obtained
	 * from the symbol table; if it is not a SYM, the size is determined
	 * from the node's type.  This is slightly different from C's sizeof()
	 * operator in that (for example) when applied to a function, sizeof()
	 * will evaluate to the length of the function rather than the size of
	 * the function type.
	 */
	if (dnp->dn_kind != DT_NODE_SYM)
		return (dt_node_type_size(dnp));

	sip = dnp->dn_ident->di_data;

	if (dtrace_lookup_by_name(dtp, sip->dts_object,
	    sip->dts_name, &sym, NULL) == -1)
		return (0);

	return (sym.st_size);
}

int
dt_node_is_integer(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_id_t type;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	type = ctf_type_resolve(fp, dnp->dn_type);
	kind = ctf_type_kind(fp, type);

	if (kind == CTF_K_INTEGER &&
	    ctf_type_encoding(fp, type, &e) == 0 && IS_VOID(e))
		return (0); /* void integer */

	return (kind == CTF_K_INTEGER || kind == CTF_K_ENUM);
}

int
dt_node_is_float(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_id_t type;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	type = ctf_type_resolve(fp, dnp->dn_type);
	kind = ctf_type_kind(fp, type);

	return (kind == CTF_K_FLOAT &&
	    ctf_type_encoding(dnp->dn_ctfp, type, &e) == 0 && (
	    e.cte_format == CTF_FP_SINGLE || e.cte_format == CTF_FP_DOUBLE ||
	    e.cte_format == CTF_FP_LDOUBLE));
}

int
dt_node_is_scalar(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_id_t type;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	type = ctf_type_resolve(fp, dnp->dn_type);
	kind = ctf_type_kind(fp, type);

	if (kind == CTF_K_INTEGER &&
	    ctf_type_encoding(fp, type, &e) == 0 && IS_VOID(e))
		return (0); /* void cannot be used as a scalar */

	return (kind == CTF_K_INTEGER || kind == CTF_K_ENUM ||
	    kind == CTF_K_POINTER);
}

int
dt_node_is_arith(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_id_t type;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	type = ctf_type_resolve(fp, dnp->dn_type);
	kind = ctf_type_kind(fp, type);

	if (kind == CTF_K_INTEGER)
		return (ctf_type_encoding(fp, type, &e) == 0 && !IS_VOID(e));
	else
		return (kind == CTF_K_ENUM);
}

int
dt_node_is_vfptr(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_id_t type;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	type = ctf_type_resolve(fp, dnp->dn_type);
	if (ctf_type_kind(fp, type) != CTF_K_POINTER)
		return (0); /* type is not a pointer */

	type = ctf_type_resolve(fp, ctf_type_reference(fp, type));
	kind = ctf_type_kind(fp, type);

	return (kind == CTF_K_FUNCTION || (kind == CTF_K_INTEGER &&
	    ctf_type_encoding(fp, type, &e) == 0 && IS_VOID(e)));
}

int
dt_node_is_dynamic(const dt_node_t *dnp)
{
	if (dnp->dn_kind == DT_NODE_VAR &&
	    (dnp->dn_ident->di_flags & DT_IDFLG_INLINE)) {
		const dt_idnode_t *inp = dnp->dn_ident->di_iarg;
		return (inp->din_root ? dt_node_is_dynamic(inp->din_root) : 0);
	}

	return (dnp->dn_ctfp == DT_DYN_CTFP(yypcb->pcb_hdl) &&
	    dnp->dn_type == DT_DYN_TYPE(yypcb->pcb_hdl));
}

int
dt_node_is_string(const dt_node_t *dnp)
{
	return (dnp->dn_ctfp == DT_STR_CTFP(yypcb->pcb_hdl) &&
	    dnp->dn_type == DT_STR_TYPE(yypcb->pcb_hdl));
}

int
dt_node_is_stack(const dt_node_t *dnp)
{
	return (dnp->dn_ctfp == DT_STACK_CTFP(yypcb->pcb_hdl) &&
	    dnp->dn_type == DT_STACK_TYPE(yypcb->pcb_hdl));
}

int
dt_node_is_symaddr(const dt_node_t *dnp)
{
	return (dnp->dn_ctfp == DT_SYMADDR_CTFP(yypcb->pcb_hdl) &&
	    dnp->dn_type == DT_SYMADDR_TYPE(yypcb->pcb_hdl));
}

int
dt_node_is_usymaddr(const dt_node_t *dnp)
{
	return (dnp->dn_ctfp == DT_USYMADDR_CTFP(yypcb->pcb_hdl) &&
	    dnp->dn_type == DT_USYMADDR_TYPE(yypcb->pcb_hdl));
}

int
dt_node_is_strcompat(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_arinfo_t r;
	ctf_id_t base;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	base = ctf_type_resolve(fp, dnp->dn_type);
	kind = ctf_type_kind(fp, base);

	if (kind == CTF_K_POINTER &&
	    (base = ctf_type_reference(fp, base)) != CTF_ERR &&
	    (base = ctf_type_resolve(fp, base)) != CTF_ERR &&
	    ctf_type_encoding(fp, base, &e) == 0 && IS_CHAR(e))
		return (1); /* promote char pointer to string */

	if (kind == CTF_K_ARRAY && ctf_array_info(fp, base, &r) == 0 &&
	    (base = ctf_type_resolve(fp, r.ctr_contents)) != CTF_ERR &&
	    ctf_type_encoding(fp, base, &e) == 0 && IS_CHAR(e))
		return (1); /* promote char array to string */

	return (0);
}

int
dt_node_is_pointer(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	uint_t kind;

	assert(dnp->dn_flags & DT_NF_COOKED);

	if (dt_node_is_string(dnp))
		return (0); /* string are pass-by-ref but act like structs */

	kind = ctf_type_kind(fp, ctf_type_resolve(fp, dnp->dn_type));
	return (kind == CTF_K_POINTER || kind == CTF_K_ARRAY);
}

int
dt_node_is_void(const dt_node_t *dnp)
{
	ctf_file_t *fp = dnp->dn_ctfp;
	ctf_encoding_t e;
	ctf_id_t type;

	if (dt_node_is_dynamic(dnp))
		return (0); /* <DYN> is an alias for void but not the same */

	if (dt_node_is_stack(dnp))
		return (0);

	if (dt_node_is_symaddr(dnp) || dt_node_is_usymaddr(dnp))
		return (0);

	type = ctf_type_resolve(fp, dnp->dn_type);

	return (ctf_type_kind(fp, type) == CTF_K_INTEGER &&
	    ctf_type_encoding(fp, type, &e) == 0 && IS_VOID(e));
}

int
dt_node_is_ptrcompat(const dt_node_t *lp, const dt_node_t *rp,
    ctf_file_t **fpp, ctf_id_t *tp)
{
	ctf_file_t *lfp = lp->dn_ctfp;
	ctf_file_t *rfp = rp->dn_ctfp;

	ctf_id_t lbase = CTF_ERR, rbase = CTF_ERR;
	ctf_id_t lref = CTF_ERR, rref = CTF_ERR;

	int lp_is_void, rp_is_void, lp_is_int, rp_is_int, compat;
	uint_t lkind, rkind;
	ctf_encoding_t e;
	ctf_arinfo_t r;

	assert(lp->dn_flags & DT_NF_COOKED);
	assert(rp->dn_flags & DT_NF_COOKED);

	if (dt_node_is_dynamic(lp) || dt_node_is_dynamic(rp))
		return (0); /* fail if either node is a dynamic variable */

	lp_is_int = dt_node_is_integer(lp);
	rp_is_int = dt_node_is_integer(rp);

	if (lp_is_int && rp_is_int)
		return (0); /* fail if both nodes are integers */

	if (lp_is_int && (lp->dn_kind != DT_NODE_INT || lp->dn_value != 0))
		return (0); /* fail if lp is an integer that isn't 0 constant */

	if (rp_is_int && (rp->dn_kind != DT_NODE_INT || rp->dn_value != 0))
		return (0); /* fail if rp is an integer that isn't 0 constant */

	if ((lp_is_int == 0 && rp_is_int == 0) && (
	    (lp->dn_flags & DT_NF_USERLAND) ^ (rp->dn_flags & DT_NF_USERLAND)))
		return (0); /* fail if only one pointer is a userland address */

	/*
	 * Resolve the left-hand and right-hand types to their base type, and
	 * then resolve the referenced type as well (assuming the base type
	 * is CTF_K_POINTER or CTF_K_ARRAY).  Otherwise [lr]ref = CTF_ERR.
	 */
	if (!lp_is_int) {
		lbase = ctf_type_resolve(lfp, lp->dn_type);
		lkind = ctf_type_kind(lfp, lbase);

		if (lkind == CTF_K_POINTER) {
			lref = ctf_type_resolve(lfp,
			    ctf_type_reference(lfp, lbase));
		} else if (lkind == CTF_K_ARRAY &&
		    ctf_array_info(lfp, lbase, &r) == 0) {
			lref = ctf_type_resolve(lfp, r.ctr_contents);
		}
	}

	if (!rp_is_int) {
		rbase = ctf_type_resolve(rfp, rp->dn_type);
		rkind = ctf_type_kind(rfp, rbase);

		if (rkind == CTF_K_POINTER) {
			rref = ctf_type_resolve(rfp,
			    ctf_type_reference(rfp, rbase));
		} else if (rkind == CTF_K_ARRAY &&
		    ctf_array_info(rfp, rbase, &r) == 0) {
			rref = ctf_type_resolve(rfp, r.ctr_contents);
		}
	}

	/*
	 * We know that one or the other type may still be a zero-valued
	 * integer constant.  To simplify the code below, set the integer
	 * type variables equal to the non-integer types and proceed.
	 */
	if (lp_is_int) {
		lbase = rbase;
		lkind = rkind;
		lref = rref;
		lfp = rfp;
	} else if (rp_is_int) {
		rbase = lbase;
		rkind = lkind;
		rref = lref;
		rfp = lfp;
	}

	lp_is_void = ctf_type_encoding(lfp, lref, &e) == 0 && IS_VOID(e);
	rp_is_void = ctf_type_encoding(rfp, rref, &e) == 0 && IS_VOID(e);

	/*
	 * The types are compatible if both are pointers to the same type, or
	 * if either pointer is a void pointer.  If they are compatible, set
	 * tp to point to the more specific pointer type and return it.
	 */
	compat = (lkind == CTF_K_POINTER || lkind == CTF_K_ARRAY) &&
	    (rkind == CTF_K_POINTER || rkind == CTF_K_ARRAY) &&
	    (lp_is_void || rp_is_void || ctf_type_compat(lfp, lref, rfp, rref));

	if (compat) {
		if (fpp != NULL)
			*fpp = rp_is_void ? lfp : rfp;
		if (tp != NULL)
			*tp = rp_is_void ? lbase : rbase;
	}

	return (compat);
}

/*
 * The rules for checking argument types against parameter types are described
 * in the ANSI-C spec (see K&R[A7.3.2] and K&R[A7.17]).  We use the same rule
 * set to determine whether associative array arguments match the prototype.
 */
int
dt_node_is_argcompat(const dt_node_t *lp, const dt_node_t *rp)
{
	ctf_file_t *lfp = lp->dn_ctfp;
	ctf_file_t *rfp = rp->dn_ctfp;

	assert(lp->dn_flags & DT_NF_COOKED);
	assert(rp->dn_flags & DT_NF_COOKED);

	if (dt_node_is_integer(lp) && dt_node_is_integer(rp))
		return (1); /* integer types are compatible */

	if (dt_node_is_strcompat(lp) && dt_node_is_strcompat(rp))
		return (1); /* string types are compatible */

	if (dt_node_is_stack(lp) && dt_node_is_stack(rp))
		return (1); /* stack types are compatible */

	if (dt_node_is_symaddr(lp) && dt_node_is_symaddr(rp))
		return (1); /* symaddr types are compatible */

	if (dt_node_is_usymaddr(lp) && dt_node_is_usymaddr(rp))
		return (1); /* usymaddr types are compatible */

	switch (ctf_type_kind(lfp, ctf_type_resolve(lfp, lp->dn_type))) {
	case CTF_K_FUNCTION:
	case CTF_K_STRUCT:
	case CTF_K_UNION:
		return (ctf_type_compat(lfp, lp->dn_type, rfp, rp->dn_type));
	default:
		return (dt_node_is_ptrcompat(lp, rp, NULL, NULL));
	}
}

/*
 * We provide dt_node_is_posconst() as a convenience routine for callers who
 * wish to verify that an argument is a positive non-zero integer constant.
 */
int
dt_node_is_posconst(const dt_node_t *dnp)
{
	return (dnp->dn_kind == DT_NODE_INT && dnp->dn_value != 0 && (
	    (dnp->dn_flags & DT_NF_SIGNED) == 0 || (int64_t)dnp->dn_value > 0));
}

int
dt_node_is_actfunc(const dt_node_t *dnp)
{
	return (dnp->dn_kind == DT_NODE_FUNC &&
	    dnp->dn_ident->di_kind == DT_IDENT_ACTFUNC);
}

/*
 * The original rules for integer constant typing are described in K&R[A2.5.1].
 * However, since we support long long, we instead use the rules from ISO C99
 * clause 6.4.4.1 since that is where long longs are formally described.  The
 * rules require us to know whether the constant was specified in decimal or
 * in octal or hex, which we do by looking at our lexer's 'yyintdecimal' flag.
 * The type of an integer constant is the first of the corresponding list in
 * which its value can be represented:
 *
 * unsuffixed decimal:   int, long, long long
 * unsuffixed oct/hex:   int, unsigned int, long, unsigned long,
 *                       long long, unsigned long long
 * suffix [uU]:          unsigned int, unsigned long, unsigned long long
 * suffix [lL] decimal:  long, long long
 * suffix [lL] oct/hex:  long, unsigned long, long long, unsigned long long
 * suffix [uU][Ll]:      unsigned long, unsigned long long
 * suffix ll/LL decimal: long long
 * suffix ll/LL oct/hex: long long, unsigned long long
 * suffix [uU][ll/LL]:   unsigned long long
 *
 * Given that our lexer has already validated the suffixes by regexp matching,
 * there is an obvious way to concisely encode these rules: construct an array
 * of the types in the order int, unsigned int, long, unsigned long, long long,
 * unsigned long long.  Compute an integer array starting index based on the
 * suffix (e.g. none = 0, u = 1, ull = 5), and compute an increment based on
 * the specifier (dec/oct/hex) and suffix (u).  Then iterate from the starting
 * index to the end, advancing using the increment, and searching until we
 * find a limit that matches or we run out of choices (overflow).  To make it
 * even faster, we precompute the table of type information in dtrace_open().
 */
dt_node_t *
dt_node_int(uintmax_t value)
{
	dt_node_t *dnp = dt_node_alloc(DT_NODE_INT);
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;

	int n = (yyintdecimal | (yyintsuffix[0] == 'u')) + 1;
	int i = 0;

	const char *p;
	char c;

	dnp->dn_op = DT_TOK_INT;
	dnp->dn_value = value;

	for (p = yyintsuffix; (c = *p) != '\0'; p++) {
		if (c == 'U' || c == 'u')
			i += 1;
		else if (c == 'L' || c == 'l')
			i += 2;
	}

	for (; i < sizeof (dtp->dt_ints) / sizeof (dtp->dt_ints[0]); i += n) {
		if (value <= dtp->dt_ints[i].did_limit) {
			dt_node_type_assign(dnp,
			    dtp->dt_ints[i].did_ctfp,
			    dtp->dt_ints[i].did_type, B_FALSE);

			/*
			 * If a prefix character is present in macro text, add
			 * in the corresponding operator node (see dt_lex.l).
			 */
			switch (yyintprefix) {
			case '+':
				return (dt_node_op1(DT_TOK_IPOS, dnp));
			case '-':
				return (dt_node_op1(DT_TOK_INEG, dnp));
			default:
				return (dnp);
			}
		}
	}

	xyerror(D_INT_OFLOW, "integer constant 0x%llx cannot be represented "
	    "in any built-in integral type\n", (u_longlong_t)value);
	/*NOTREACHED*/
	return (NULL);		/* keep gcc happy */
}

dt_node_t *
dt_node_string(char *string)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *dnp;

	if (string == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	dnp = dt_node_alloc(DT_NODE_STRING);
	dnp->dn_op = DT_TOK_STRING;
	dnp->dn_string = string;
	dt_node_type_assign(dnp, DT_STR_CTFP(dtp), DT_STR_TYPE(dtp), B_FALSE);

	return (dnp);
}

dt_node_t *
dt_node_ident(char *name)
{
	dt_ident_t *idp;
	dt_node_t *dnp;

	if (name == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	/*
	 * If the identifier is an inlined integer constant, then create an INT
	 * node that is a clone of the inline parse tree node and return that
	 * immediately, allowing this inline to be used in parsing contexts
	 * that require constant expressions (e.g. scalar array sizes).
	 */
	if ((idp = dt_idstack_lookup(&yypcb->pcb_globals, name)) != NULL &&
	    (idp->di_flags & DT_IDFLG_INLINE)) {
		dt_idnode_t *inp = idp->di_iarg;

		if (inp->din_root != NULL &&
		    inp->din_root->dn_kind == DT_NODE_INT) {
			free(name);

			dnp = dt_node_alloc(DT_NODE_INT);
			dnp->dn_op = DT_TOK_INT;
			dnp->dn_value = inp->din_root->dn_value;
			dt_node_type_propagate(inp->din_root, dnp);

			return (dnp);
		}
	}

	dnp = dt_node_alloc(DT_NODE_IDENT);
	dnp->dn_op = name[0] == '@' ? DT_TOK_AGG : DT_TOK_IDENT;
	dnp->dn_string = name;

	return (dnp);
}

/*
 * Create an empty node of type corresponding to the given declaration.
 * Explicit references to user types (C or D) are assigned the default
 * stability; references to other types are _dtrace_typattr (Private).
 */
dt_node_t *
dt_node_type(dt_decl_t *ddp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dtrace_typeinfo_t dtt;
	dt_node_t *dnp;
	char *name = NULL;
	int err;

	/*
	 * If 'ddp' is NULL, we get a decl by popping the decl stack.  This
	 * form of dt_node_type() is used by parameter rules in dt_grammar.y.
	 */
	if (ddp == NULL)
		ddp = dt_decl_pop_param(&name);

	err = dt_decl_type(ddp, &dtt);
	dt_decl_free(ddp);

	if (err != 0) {
		free(name);
		longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
	}

	dnp = dt_node_alloc(DT_NODE_TYPE);
	dnp->dn_op = DT_TOK_IDENT;
	dnp->dn_string = name;

	dt_node_type_assign(dnp, dtt.dtt_ctfp, dtt.dtt_type, dtt.dtt_flags);

	if (dtt.dtt_ctfp == dtp->dt_cdefs->dm_ctfp ||
	    dtt.dtt_ctfp == dtp->dt_ddefs->dm_ctfp)
		dt_node_attr_assign(dnp, _dtrace_defattr);
	else
		dt_node_attr_assign(dnp, _dtrace_typattr);

	return (dnp);
}

/*
 * Create a type node corresponding to a varargs (...) parameter by just
 * assigning it type CTF_ERR.  The decl processing code will handle this.
 */
dt_node_t *
dt_node_vatype(void)
{
	dt_node_t *dnp = dt_node_alloc(DT_NODE_TYPE);

	dnp->dn_op = DT_TOK_IDENT;
	dnp->dn_ctfp = yypcb->pcb_hdl->dt_cdefs->dm_ctfp;
	dnp->dn_type = CTF_ERR;
	dnp->dn_attr = _dtrace_defattr;

	return (dnp);
}

/*
 * Instantiate a decl using the contents of the current declaration stack.  As
 * we do not currently permit decls to be initialized, this function currently
 * returns NULL and no parse node is created.  When this function is called,
 * the topmost scope's ds_ident pointer will be set to NULL (indicating no
 * init_declarator rule was matched) or will point to the identifier to use.
 */
dt_node_t *
dt_node_decl(void)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_scope_t *dsp = &yypcb->pcb_dstack;
	dt_dclass_t class = dsp->ds_class;
	dt_decl_t *ddp = dt_decl_top();

	dt_module_t *dmp;
	dtrace_typeinfo_t dtt;
	ctf_id_t type;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	if (dt_decl_type(ddp, &dtt) != 0)
		longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);

	/*
	 * If we have no declaration identifier, then this is either a spurious
	 * declaration of an intrinsic type (e.g. "extern int;") or declaration
	 * or redeclaration of a struct, union, or enum type or tag.
	 */
	if (dsp->ds_ident == NULL) {
		if (ddp->dd_kind != CTF_K_STRUCT &&
		    ddp->dd_kind != CTF_K_UNION && ddp->dd_kind != CTF_K_ENUM)
			xyerror(D_DECL_USELESS, "useless declaration\n");

		dt_dprintf("type %s added as id %ld\n", dt_type_name(
		    ddp->dd_ctfp, ddp->dd_type, n1, sizeof (n1)), ddp->dd_type);

		return (NULL);
	}

	if (strchr(dsp->ds_ident, '`') != NULL) {
		xyerror(D_DECL_SCOPE, "D scoping operator may not be used in "
		    "a declaration name (%s)\n", dsp->ds_ident);
	}

	/*
	 * If we are nested inside of a C include file, add the declaration to
	 * the C definition module; otherwise use the D definition module.
	 */
	if (yypcb->pcb_idepth != 0)
		dmp = dtp->dt_cdefs;
	else
		dmp = dtp->dt_ddefs;

	/*
	 * If we see a global or static declaration of a function prototype,
	 * treat this as equivalent to a D extern declaration.
	 */
	if (ctf_type_kind(dtt.dtt_ctfp, dtt.dtt_type) == CTF_K_FUNCTION &&
	    (class == DT_DC_DEFAULT || class == DT_DC_STATIC))
		class = DT_DC_EXTERN;

	switch (class) {
	case DT_DC_AUTO:
	case DT_DC_REGISTER:
	case DT_DC_STATIC:
		xyerror(D_DECL_BADCLASS, "specified storage class not "
		    "appropriate in D\n");
		/*NOTREACHED*/

	case DT_DC_EXTERN: {
		dtrace_typeinfo_t ott;
		dtrace_syminfo_t dts;
		GElf_Sym sym;

		int exists = dtrace_lookup_by_name(dtp,
		    dmp->dm_name, dsp->ds_ident, &sym, &dts) == 0;

		if (exists && (dtrace_symbol_type(dtp, &sym, &dts, &ott) != 0 ||
		    ctf_type_cmp(dtt.dtt_ctfp, dtt.dtt_type,
		    ott.dtt_ctfp, ott.dtt_type) != 0)) {
			xyerror(D_DECL_IDRED, "identifier redeclared: %s`%s\n"
			    "\t current: %s\n\tprevious: %s\n",
			    dmp->dm_name, dsp->ds_ident,
			    dt_type_name(dtt.dtt_ctfp, dtt.dtt_type,
				n1, sizeof (n1)),
			    dt_type_name(ott.dtt_ctfp, ott.dtt_type,
				n2, sizeof (n2)));
		} else if (!exists && dt_module_extern(dtp, dmp,
		    dsp->ds_ident, &dtt) == NULL) {
			xyerror(D_UNKNOWN,
			    "failed to extern %s: %s\n", dsp->ds_ident,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));
		} else {
			dt_dprintf("extern %s`%s type=<%s>\n",
			    dmp->dm_name, dsp->ds_ident,
			    dt_type_name(dtt.dtt_ctfp, dtt.dtt_type,
				n1, sizeof (n1)));
		}
		break;
	}

	case DT_DC_TYPEDEF:
		if (dt_idstack_lookup(&yypcb->pcb_globals, dsp->ds_ident)) {
			xyerror(D_DECL_IDRED, "global variable identifier "
			    "redeclared: %s\n", dsp->ds_ident);
		}

		if (ctf_lookup_by_name(dmp->dm_ctfp,
		    dsp->ds_ident) != CTF_ERR) {
			xyerror(D_DECL_IDRED,
			    "typedef redeclared: %s\n", dsp->ds_ident);
		}

		/*
		 * If the source type for the typedef is not defined in the
		 * target container or its parent, copy the type to the target
		 * container and reset dtt_ctfp and dtt_type to the copy.
		 */
		if (dtt.dtt_ctfp != dmp->dm_ctfp &&
		    dtt.dtt_ctfp != ctf_parent_file(dmp->dm_ctfp)) {

			dtt.dtt_type = ctf_add_type(dmp->dm_ctfp,
			    dtt.dtt_ctfp, dtt.dtt_type);
			dtt.dtt_ctfp = dmp->dm_ctfp;

			if (dtt.dtt_type == CTF_ERR ||
			    ctf_update(dtt.dtt_ctfp) == CTF_ERR) {
				xyerror(D_UNKNOWN, "failed to copy typedef %s "
				    "source type: %s\n", dsp->ds_ident,
				    ctf_errmsg(ctf_errno(dtt.dtt_ctfp)));
			}
		}

		type = ctf_add_typedef(dmp->dm_ctfp,
		    CTF_ADD_ROOT, dsp->ds_ident, dtt.dtt_type);

		if (type == CTF_ERR || ctf_update(dmp->dm_ctfp) == CTF_ERR) {
			xyerror(D_UNKNOWN, "failed to typedef %s: %s\n",
			    dsp->ds_ident, ctf_errmsg(ctf_errno(dmp->dm_ctfp)));
		}

		dt_dprintf("typedef %s added as id %ld\n", dsp->ds_ident, type);
		break;

	default: {
		ctf_encoding_t cte;
		dt_idhash_t *dhp;
		dt_ident_t *idp;
		dt_node_t idn;
		int assc, idkind;
		uint_t id, kind;
		ushort_t idflags;

		switch (class) {
		case DT_DC_THIS:
			dhp = yypcb->pcb_locals;
			idflags = DT_IDFLG_LOCAL;
			idp = dt_idhash_lookup(dhp, dsp->ds_ident);
			break;
		case DT_DC_SELF:
			dhp = dtp->dt_tls;
			idflags = DT_IDFLG_TLS;
			idp = dt_idhash_lookup(dhp, dsp->ds_ident);
			break;
		default:
			dhp = dtp->dt_globals;
			idflags = 0;
			idp = dt_idstack_lookup(
			    &yypcb->pcb_globals, dsp->ds_ident);
			break;
		}

		if (ddp->dd_kind == CTF_K_ARRAY && ddp->dd_node == NULL) {
			xyerror(D_DECL_ARRNULL,
			    "array declaration requires array dimension or "
			    "tuple signature: %s\n", dsp->ds_ident);
		}

		if (idp != NULL && idp->di_gen == 0) {
			xyerror(D_DECL_IDRED, "built-in identifier "
			    "redeclared: %s\n", idp->di_name);
		}

		if (dtrace_lookup_by_type(dtp, DTRACE_OBJ_CDEFS,
		    dsp->ds_ident, NULL) == 0 ||
		    dtrace_lookup_by_type(dtp, DTRACE_OBJ_DDEFS,
		    dsp->ds_ident, NULL) == 0) {
			xyerror(D_DECL_IDRED, "typedef identifier "
			    "redeclared: %s\n", dsp->ds_ident);
		}

		/*
		 * Cache some attributes of the decl to make the rest of this
		 * code simpler: if the decl is an array which is subscripted
		 * by a type rather than an integer, then it's an associative
		 * array (assc).  We then expect to match either DT_IDENT_ARRAY
		 * for associative arrays or DT_IDENT_SCALAR for anything else.
		 */
		assc = ddp->dd_kind == CTF_K_ARRAY &&
		    ddp->dd_node->dn_kind == DT_NODE_TYPE;

		idkind = assc ? DT_IDENT_ARRAY : DT_IDENT_SCALAR;

		/*
		 * Create a fake dt_node_t on the stack so we can determine the
		 * type of any matching identifier by assigning to this node.
		 * If the pre-existing ident has its di_type set, propagate
		 * the type by hand so as not to trigger a prototype check for
		 * arrays (yet); otherwise we use dt_ident_cook() on the ident
		 * to ensure it is fully initialized before looking at it.
		 */
		bzero(&idn, sizeof (dt_node_t));

		if (idp != NULL && idp->di_type != CTF_ERR)
			dt_node_type_assign(&idn, idp->di_ctfp, idp->di_type,
			    B_FALSE);
		else if (idp != NULL)
			(void) dt_ident_cook(&idn, idp, NULL);

		if (assc) {
			if (class == DT_DC_THIS) {
				xyerror(D_DECL_LOCASSC, "associative arrays "
				    "may not be declared as local variables:"
				    " %s\n", dsp->ds_ident);
			}

			if (dt_decl_type(ddp->dd_next, &dtt) != 0)
				longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
		}

		if (idp != NULL && (idp->di_kind != idkind ||
		    ctf_type_cmp(dtt.dtt_ctfp, dtt.dtt_type,
		    idn.dn_ctfp, idn.dn_type) != 0)) {
			xyerror(D_DECL_IDRED, "identifier redeclared: %s\n"
			    "\t current: %s %s\n\tprevious: %s %s\n",
			    dsp->ds_ident, dt_idkind_name(idkind),
			    dt_type_name(dtt.dtt_ctfp,
			    dtt.dtt_type, n1, sizeof (n1)),
			    dt_idkind_name(idp->di_kind),
			    dt_node_type_name(&idn, n2, sizeof (n2)));

		} else if (idp != NULL && assc) {
			const dt_idsig_t *isp = idp->di_data;
			dt_node_t *dnp = ddp->dd_node;
			int argc = 0;

			for (; dnp != NULL; dnp = dnp->dn_list, argc++) {
				const dt_node_t *pnp = &isp->dis_args[argc];

				if (argc >= isp->dis_argc)
					continue; /* tuple length mismatch */

				if (ctf_type_cmp(dnp->dn_ctfp, dnp->dn_type,
				    pnp->dn_ctfp, pnp->dn_type) == 0)
					continue;

				xyerror(D_DECL_IDRED,
				    "identifier redeclared: %s\n"
				    "\t current: %s, key #%d of type %s\n"
				    "\tprevious: %s, key #%d of type %s\n",
				    dsp->ds_ident,
				    dt_idkind_name(idkind), argc + 1,
				    dt_node_type_name(dnp, n1, sizeof (n1)),
				    dt_idkind_name(idp->di_kind), argc + 1,
				    dt_node_type_name(pnp, n2, sizeof (n2)));
			}

			if (isp->dis_argc != argc) {
				xyerror(D_DECL_IDRED,
				    "identifier redeclared: %s\n"
				    "\t current: %s of %s, tuple length %d\n"
				    "\tprevious: %s of %s, tuple length %d\n",
				    dsp->ds_ident, dt_idkind_name(idkind),
				    dt_type_name(dtt.dtt_ctfp, dtt.dtt_type,
				    n1, sizeof (n1)), argc,
				    dt_idkind_name(idp->di_kind),
				    dt_node_type_name(&idn, n2, sizeof (n2)),
				    isp->dis_argc);
			}

		} else if (idp == NULL) {
			type = ctf_type_resolve(dtt.dtt_ctfp, dtt.dtt_type);
			kind = ctf_type_kind(dtt.dtt_ctfp, type);

			switch (kind) {
			case CTF_K_INTEGER:
				if (ctf_type_encoding(dtt.dtt_ctfp, type,
				    &cte) == 0 && IS_VOID(cte)) {
					xyerror(D_DECL_VOIDOBJ, "cannot have "
					    "void object: %s\n", dsp->ds_ident);
				}
				break;
			case CTF_K_STRUCT:
			case CTF_K_UNION:
				if (ctf_type_size(dtt.dtt_ctfp, type) != 0)
					break; /* proceed to declaring */
				/*FALLTHRU*/
			case CTF_K_FORWARD:
				xyerror(D_DECL_INCOMPLETE,
				    "incomplete struct/union/enum %s: %s\n",
				    dt_type_name(dtt.dtt_ctfp, dtt.dtt_type,
				    n1, sizeof (n1)), dsp->ds_ident);
				/*NOTREACHED*/
			}

			if (dt_idhash_nextid(dhp, &id) == -1) {
				xyerror(D_ID_OFLOW, "cannot create %s: limit "
				    "on number of %s variables exceeded\n",
				    dsp->ds_ident, dt_idhash_name(dhp));
			}

			dt_dprintf("declare %s %s variable %s, id=%u\n",
			    dt_idhash_name(dhp), dt_idkind_name(idkind),
			    dsp->ds_ident, id);

			idp = dt_idhash_insert(dhp, dsp->ds_ident, idkind,
			    idflags | DT_IDFLG_WRITE | DT_IDFLG_DECL, id,
			    _dtrace_defattr, 0, assc ? &dt_idops_assc :
			    &dt_idops_thaw, NULL, dtp->dt_gen);

			if (idp == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			dt_ident_type_assign(idp, dtt.dtt_ctfp, dtt.dtt_type);

			/*
			 * If we are declaring an associative array, use our
			 * fake parse node to cook the new assoc identifier.
			 * This will force the ident code to instantiate the
			 * array type signature corresponding to the list of
			 * types pointed to by ddp->dd_node.  We also reset
			 * the identifier's attributes based upon the result.
			 */
			if (assc) {
				idp->di_attr =
				    dt_ident_cook(&idn, idp, &ddp->dd_node);
			}
		}
	}

	} /* end of switch */

	free(dsp->ds_ident);
	dsp->ds_ident = NULL;

	return (NULL);
}

dt_node_t *
dt_node_func(dt_node_t *dnp, dt_node_t *args)
{
	dt_ident_t *idp;

	if (dnp->dn_kind != DT_NODE_IDENT) {
		xyerror(D_FUNC_IDENT,
		    "function designator is not of function type\n");
	}

	idp = dt_idstack_lookup(&yypcb->pcb_globals, dnp->dn_string);

	if (idp == NULL) {
		xyerror(D_FUNC_UNDEF,
		    "undefined function name: %s\n", dnp->dn_string);
	}

	if (idp->di_kind != DT_IDENT_FUNC &&
	    idp->di_kind != DT_IDENT_AGGFUNC &&
	    idp->di_kind != DT_IDENT_ACTFUNC) {
		xyerror(D_FUNC_IDKIND, "%s '%s' may not be referenced as a "
		    "function\n", dt_idkind_name(idp->di_kind), idp->di_name);
	}

	free(dnp->dn_string);
	dnp->dn_string = NULL;

	dnp->dn_kind = DT_NODE_FUNC;
	dnp->dn_flags &= ~DT_NF_COOKED;
	dnp->dn_ident = idp;
	dnp->dn_args = args;
	dnp->dn_list = NULL;

	return (dnp);
}

/*
 * The offsetof() function is special because it takes a type name as an
 * argument.  It does not actually construct its own node; after looking up the
 * structure or union offset, we just return an integer node with the offset.
 */
dt_node_t *
dt_node_offsetof(dt_decl_t *ddp, char *s)
{
	dtrace_typeinfo_t dtt;
	dt_node_t dn;
	char *name;
	int err;

	ctf_membinfo_t ctm;
	ctf_id_t type;
	uint_t kind;

	name = alloca(strlen(s) + 1);
	(void) strcpy(name, s);
	free(s);

	err = dt_decl_type(ddp, &dtt);
	dt_decl_free(ddp);

	if (err != 0)
		longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);

	type = ctf_type_resolve(dtt.dtt_ctfp, dtt.dtt_type);
	kind = ctf_type_kind(dtt.dtt_ctfp, type);

	if (kind != CTF_K_STRUCT && kind != CTF_K_UNION) {
		xyerror(D_OFFSETOF_TYPE,
		    "offsetof operand must be a struct or union type\n");
	}

	if (ctf_member_info(dtt.dtt_ctfp, type, name, &ctm) == CTF_ERR) {
		xyerror(D_UNKNOWN, "failed to determine offset of %s: %s\n",
		    name, ctf_errmsg(ctf_errno(dtt.dtt_ctfp)));
	}

	bzero(&dn, sizeof (dn));
	dt_node_type_assign(&dn, dtt.dtt_ctfp, ctm.ctm_type, B_FALSE);

	if (dn.dn_flags & DT_NF_BITFIELD) {
		xyerror(D_OFFSETOF_BITFIELD,
		    "cannot take offset of a bit-field: %s\n", name);
	}

	return (dt_node_int(ctm.ctm_offset / NBBY));
}

dt_node_t *
dt_node_op1(int op, dt_node_t *cp)
{
	dt_node_t *dnp;

	if (cp->dn_kind == DT_NODE_INT) {
		switch (op) {
		case DT_TOK_INEG:
			/*
			 * If we're negating an unsigned integer, zero out any
			 * extra top bits to truncate the value to the size of
			 * the effective type determined by dt_node_int().
			 */
			cp->dn_value = -cp->dn_value;
			if (!(cp->dn_flags & DT_NF_SIGNED)) {
				cp->dn_value &= ~0ULL >>
				    (64 - dt_node_type_size(cp) * NBBY);
			}
			/*FALLTHRU*/
		case DT_TOK_IPOS:
			return (cp);
		case DT_TOK_BNEG:
			cp->dn_value = ~cp->dn_value;
			return (cp);
		case DT_TOK_LNEG:
			cp->dn_value = !cp->dn_value;
			return (cp);
		}
	}

	/*
	 * If sizeof is applied to a type_name or string constant, we can
	 * transform 'cp' into an integer constant in the node construction
	 * pass so that it can then be used for arithmetic in this pass.
	 */
	if (op == DT_TOK_SIZEOF &&
	    (cp->dn_kind == DT_NODE_STRING || cp->dn_kind == DT_NODE_TYPE)) {
		dtrace_hdl_t *dtp = yypcb->pcb_hdl;
		size_t size = dt_node_type_size(cp);

		if (size == 0) {
			xyerror(D_SIZEOF_TYPE, "cannot apply sizeof to an "
			    "operand of unknown size\n");
		}

		dt_node_type_assign(cp, dtp->dt_ddefs->dm_ctfp,
		    ctf_lookup_by_name(dtp->dt_ddefs->dm_ctfp, "size_t"),
		    B_FALSE);

		cp->dn_kind = DT_NODE_INT;
		cp->dn_op = DT_TOK_INT;
		cp->dn_value = size;

		return (cp);
	}

	dnp = dt_node_alloc(DT_NODE_OP1);
	assert(op <= USHRT_MAX);
	dnp->dn_op = (ushort_t)op;
	dnp->dn_child = cp;

	return (dnp);
}

/*
 * If an integer constant is being cast to another integer type, we can
 * perform the cast as part of integer constant folding in this pass. We must
 * take action when the integer is being cast to a smaller type or if it is
 * changing signed-ness. If so, we first shift rp's bits bits high (losing
 * excess bits if narrowing) and then shift them down with either a logical
 * shift (unsigned) or arithmetic shift (signed).
 */
static void
dt_cast(dt_node_t *lp, dt_node_t *rp)
{
	size_t srcsize = dt_node_type_size(rp);
	size_t dstsize = dt_node_type_size(lp);

	if (dstsize < srcsize) {
		int n = (sizeof (uint64_t) - dstsize) * NBBY;
		rp->dn_value <<= n;
		rp->dn_value >>= n;
	} else if (dstsize > srcsize) {
		int n = (sizeof (uint64_t) - srcsize) * NBBY;
		int s = (dstsize - srcsize) * NBBY;

		rp->dn_value <<= n;
		if (rp->dn_flags & DT_NF_SIGNED) {
			rp->dn_value = (intmax_t)rp->dn_value >> s;
			rp->dn_value >>= n - s;
		} else {
			rp->dn_value >>= n;
		}
	}
}

dt_node_t *
dt_node_op2(int op, dt_node_t *lp, dt_node_t *rp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *dnp;

	/*
	 * First we check for operations that are illegal -- namely those that
	 * might result in integer division by zero, and abort if one is found.
	 */
	if (rp->dn_kind == DT_NODE_INT && rp->dn_value == 0 &&
	    (op == DT_TOK_MOD || op == DT_TOK_DIV ||
	    op == DT_TOK_MOD_EQ || op == DT_TOK_DIV_EQ))
		xyerror(D_DIV_ZERO, "expression contains division by zero\n");

	/*
	 * If both children are immediate values, we can just perform inline
	 * calculation and return a new immediate node with the result.
	 */
	if (lp->dn_kind == DT_NODE_INT && rp->dn_kind == DT_NODE_INT) {
		uintmax_t l = lp->dn_value;
		uintmax_t r = rp->dn_value;

		dnp = dt_node_int(0); /* allocate new integer node for result */

		switch (op) {
		case DT_TOK_LOR:
			dnp->dn_value = l || r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_LXOR:
			dnp->dn_value = (l != 0) ^ (r != 0);
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_LAND:
			dnp->dn_value = l && r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_BOR:
			dnp->dn_value = l | r;
			dt_node_promote(lp, rp, dnp);
			break;
		case DT_TOK_XOR:
			dnp->dn_value = l ^ r;
			dt_node_promote(lp, rp, dnp);
			break;
		case DT_TOK_BAND:
			dnp->dn_value = l & r;
			dt_node_promote(lp, rp, dnp);
			break;
		case DT_TOK_EQU:
			dnp->dn_value = l == r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_NEQ:
			dnp->dn_value = l != r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_LT:
			dt_node_promote(lp, rp, dnp);
			if (dnp->dn_flags & DT_NF_SIGNED)
				dnp->dn_value = (intmax_t)l < (intmax_t)r;
			else
				dnp->dn_value = l < r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_LE:
			dt_node_promote(lp, rp, dnp);
			if (dnp->dn_flags & DT_NF_SIGNED)
				dnp->dn_value = (intmax_t)l <= (intmax_t)r;
			else
				dnp->dn_value = l <= r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_GT:
			dt_node_promote(lp, rp, dnp);
			if (dnp->dn_flags & DT_NF_SIGNED)
				dnp->dn_value = (intmax_t)l > (intmax_t)r;
			else
				dnp->dn_value = l > r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_GE:
			dt_node_promote(lp, rp, dnp);
			if (dnp->dn_flags & DT_NF_SIGNED)
				dnp->dn_value = (intmax_t)l >= (intmax_t)r;
			else
				dnp->dn_value = l >= r;
			dt_node_type_assign(dnp,
			    DT_INT_CTFP(dtp), DT_INT_TYPE(dtp), B_FALSE);
			break;
		case DT_TOK_LSH:
			dnp->dn_value = l << r;
			dt_node_type_propagate(lp, dnp);
			dt_node_attr_assign(rp,
			    dt_attr_min(lp->dn_attr, rp->dn_attr));
			break;
		case DT_TOK_RSH:
			dnp->dn_value = l >> r;
			dt_node_type_propagate(lp, dnp);
			dt_node_attr_assign(rp,
			    dt_attr_min(lp->dn_attr, rp->dn_attr));
			break;
		case DT_TOK_ADD:
			dnp->dn_value = l + r;
			dt_node_promote(lp, rp, dnp);
			break;
		case DT_TOK_SUB:
			dnp->dn_value = l - r;
			dt_node_promote(lp, rp, dnp);
			break;
		case DT_TOK_MUL:
			dnp->dn_value = l * r;
			dt_node_promote(lp, rp, dnp);
			break;
		case DT_TOK_DIV:
			dt_node_promote(lp, rp, dnp);
			if (dnp->dn_flags & DT_NF_SIGNED)
				dnp->dn_value = (intmax_t)l / (intmax_t)r;
			else
				dnp->dn_value = l / r;
			break;
		case DT_TOK_MOD:
			dt_node_promote(lp, rp, dnp);
			if (dnp->dn_flags & DT_NF_SIGNED)
				dnp->dn_value = (intmax_t)l % (intmax_t)r;
			else
				dnp->dn_value = l % r;
			break;
		default:
			dt_node_free(dnp);
			dnp = NULL;
		}

		if (dnp != NULL) {
			dt_node_free(lp);
			dt_node_free(rp);
			return (dnp);
		}
	}

	if (op == DT_TOK_LPAR && rp->dn_kind == DT_NODE_INT &&
	    dt_node_is_integer(lp)) {
		dt_cast(lp, rp);
		dt_node_type_propagate(lp, rp);
		dt_node_attr_assign(rp, dt_attr_min(lp->dn_attr, rp->dn_attr));
		dt_node_free(lp);

		return (rp);
	}

	/*
	 * If no immediate optimizations are available, create an new OP2 node
	 * and glue the left and right children into place and return.
	 */
	dnp = dt_node_alloc(DT_NODE_OP2);
	assert(op <= USHRT_MAX);
	dnp->dn_op = (ushort_t)op;
	dnp->dn_left = lp;
	dnp->dn_right = rp;

	return (dnp);
}

dt_node_t *
dt_node_op3(dt_node_t *expr, dt_node_t *lp, dt_node_t *rp)
{
	dt_node_t *dnp;

	if (expr->dn_kind == DT_NODE_INT)
		return (expr->dn_value != 0 ? lp : rp);

	dnp = dt_node_alloc(DT_NODE_OP3);
	dnp->dn_op = DT_TOK_QUESTION;
	dnp->dn_expr = expr;
	dnp->dn_left = lp;
	dnp->dn_right = rp;

	return (dnp);
}

dt_node_t *
dt_node_statement(dt_node_t *expr)
{
	dt_node_t *dnp;

	if (expr->dn_kind == DT_NODE_AGG)
		return (expr);

	if (expr->dn_kind == DT_NODE_FUNC &&
	    expr->dn_ident->di_kind == DT_IDENT_ACTFUNC)
		dnp = dt_node_alloc(DT_NODE_DFUNC);
	else
		dnp = dt_node_alloc(DT_NODE_DEXPR);

	dnp->dn_expr = expr;
	return (dnp);
}

dt_node_t *
dt_node_if(dt_node_t *pred, dt_node_t *acts, dt_node_t *else_acts)
{
	dt_node_t *dnp = dt_node_alloc(DT_NODE_IF);
	dnp->dn_conditional = pred;
	dnp->dn_body = acts;
	dnp->dn_alternate_body = else_acts;

	return (dnp);
}

dt_node_t *
dt_node_pdesc_by_name(char *spec)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *dnp;

	if (spec == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	dnp = dt_node_alloc(DT_NODE_PDESC);
	dnp->dn_spec = spec;
	dnp->dn_desc = malloc(sizeof (dtrace_probedesc_t));

	if (dnp->dn_desc == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	if (dtrace_xstr2desc(dtp, yypcb->pcb_pspec, dnp->dn_spec,
	    yypcb->pcb_sargc, yypcb->pcb_sargv, dnp->dn_desc) != 0) {
		xyerror(D_PDESC_INVAL, "invalid probe description \"%s\": %s\n",
		    dnp->dn_spec, dtrace_errmsg(dtp, dtrace_errno(dtp)));
	}

	free(dnp->dn_spec);
	dnp->dn_spec = NULL;

	return (dnp);
}

dt_node_t *
dt_node_pdesc_by_id(uintmax_t id)
{
	static const char *const names[] = {
		"providers", "modules", "functions"
	};

	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *dnp = dt_node_alloc(DT_NODE_PDESC);

	if ((dnp->dn_desc = malloc(sizeof (dtrace_probedesc_t))) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	if (id > UINT_MAX) {
		xyerror(D_PDESC_INVAL, "identifier %llu exceeds maximum "
		    "probe id\n", (u_longlong_t)id);
	}

	if (yypcb->pcb_pspec != DTRACE_PROBESPEC_NAME) {
		xyerror(D_PDESC_INVAL, "probe identifier %llu not permitted "
		    "when specifying %s\n", (u_longlong_t)id,
		    names[yypcb->pcb_pspec]);
	}

	if (dtrace_id2desc(dtp, (dtrace_id_t)id, dnp->dn_desc) != 0) {
		xyerror(D_PDESC_INVAL, "invalid probe identifier %llu: %s\n",
		    (u_longlong_t)id, dtrace_errmsg(dtp, dtrace_errno(dtp)));
	}

	return (dnp);
}

dt_node_t *
dt_node_clause(dt_node_t *pdescs, dt_node_t *pred, dt_node_t *acts)
{
	dt_node_t *dnp = dt_node_alloc(DT_NODE_CLAUSE);

	dnp->dn_pdescs = pdescs;
	dnp->dn_pred = pred;
	dnp->dn_acts = acts;

	return (dnp);
}

dt_node_t *
dt_node_inline(dt_node_t *expr)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_scope_t *dsp = &yypcb->pcb_dstack;
	dt_decl_t *ddp = dt_decl_top();

	char n[DT_TYPE_NAMELEN];
	dtrace_typeinfo_t dtt;

	dt_ident_t *idp, *rdp;
	dt_idnode_t *inp;
	dt_node_t *dnp;

	if (dt_decl_type(ddp, &dtt) != 0)
		longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);

	if (dsp->ds_class != DT_DC_DEFAULT) {
		xyerror(D_DECL_BADCLASS, "specified storage class not "
		    "appropriate for inline declaration\n");
	}

	if (dsp->ds_ident == NULL)
		xyerror(D_DECL_USELESS, "inline declaration requires a name\n");

	if ((idp = dt_idstack_lookup(
	    &yypcb->pcb_globals, dsp->ds_ident)) != NULL) {
		xyerror(D_DECL_IDRED, "identifier redefined: %s\n\t current: "
		    "inline definition\n\tprevious: %s %s\n",
		    idp->di_name, dt_idkind_name(idp->di_kind),
		    (idp->di_flags & DT_IDFLG_INLINE) ? "inline" : "");
	}

	/*
	 * If we are declaring an inlined array, verify that we have a tuple
	 * signature, and then recompute 'dtt' as the array's value type.
	 */
	if (ddp->dd_kind == CTF_K_ARRAY) {
		if (ddp->dd_node == NULL) {
			xyerror(D_DECL_ARRNULL, "inline declaration requires "
			    "array tuple signature: %s\n", dsp->ds_ident);
		}

		if (ddp->dd_node->dn_kind != DT_NODE_TYPE) {
			xyerror(D_DECL_ARRNULL, "inline declaration cannot be "
			    "of scalar array type: %s\n", dsp->ds_ident);
		}

		if (dt_decl_type(ddp->dd_next, &dtt) != 0)
			longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
	}

	/*
	 * If the inline identifier is not defined, then create it with the
	 * orphan flag set.  We do not insert the identifier into dt_globals
	 * until we have successfully cooked the right-hand expression, below.
	 */
	dnp = dt_node_alloc(DT_NODE_INLINE);
	dt_node_type_assign(dnp, dtt.dtt_ctfp, dtt.dtt_type, B_FALSE);
	dt_node_attr_assign(dnp, _dtrace_defattr);

	if (dt_node_is_void(dnp)) {
		xyerror(D_DECL_VOIDOBJ,
		    "cannot declare void inline: %s\n", dsp->ds_ident);
	}

	if (ctf_type_kind(dnp->dn_ctfp, ctf_type_resolve(
	    dnp->dn_ctfp, dnp->dn_type)) == CTF_K_FORWARD) {
		xyerror(D_DECL_INCOMPLETE,
		    "incomplete struct/union/enum %s: %s\n",
		    dt_node_type_name(dnp, n, sizeof (n)), dsp->ds_ident);
	}

	if ((inp = malloc(sizeof (dt_idnode_t))) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	bzero(inp, sizeof (dt_idnode_t));

	idp = dnp->dn_ident = dt_ident_create(dsp->ds_ident,
	    ddp->dd_kind == CTF_K_ARRAY ? DT_IDENT_ARRAY : DT_IDENT_SCALAR,
	    DT_IDFLG_INLINE | DT_IDFLG_REF | DT_IDFLG_DECL | DT_IDFLG_ORPHAN, 0,
	    _dtrace_defattr, 0, &dt_idops_inline, inp, dtp->dt_gen);

	if (idp == NULL) {
		free(inp);
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
	}

	/*
	 * If we're inlining an associative array, create a private identifier
	 * hash containing the named parameters and store it in inp->din_hash.
	 * We then push this hash on to the top of the pcb_globals stack.
	 */
	if (ddp->dd_kind == CTF_K_ARRAY) {
		dt_idnode_t *pinp;
		dt_ident_t *pidp;
		dt_node_t *pnp;
		uint_t i = 0;

		for (pnp = ddp->dd_node; pnp != NULL; pnp = pnp->dn_list)
			i++; /* count up parameters for din_argv[] */

		inp->din_hash = dt_idhash_create("inline args", NULL, 0, 0);
		inp->din_argv = calloc(i, sizeof (dt_ident_t *));

		if (inp->din_hash == NULL || inp->din_argv == NULL)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

		/*
		 * Create an identifier for each parameter as a scalar inline,
		 * and store it in din_hash and in position in din_argv[].  The
		 * parameter identifiers also use dt_idops_inline, but we leave
		 * the dt_idnode_t argument 'pinp' zeroed.  This will be filled
		 * in by the code generation pass with references to the args.
		 */
		for (i = 0, pnp = ddp->dd_node;
		    pnp != NULL; pnp = pnp->dn_list, i++) {

			if (pnp->dn_string == NULL)
				continue; /* ignore anonymous parameters */

			if ((pinp = malloc(sizeof (dt_idnode_t))) == NULL)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

			pidp = dt_idhash_insert(inp->din_hash, pnp->dn_string,
			    DT_IDENT_SCALAR, DT_IDFLG_DECL | DT_IDFLG_INLINE, 0,
			    _dtrace_defattr, 0, &dt_idops_inline,
			    pinp, dtp->dt_gen);

			if (pidp == NULL) {
				free(pinp);
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
			}

			inp->din_argv[i] = pidp;
			bzero(pinp, sizeof (dt_idnode_t));
			dt_ident_type_assign(pidp, pnp->dn_ctfp, pnp->dn_type);
		}

		dt_idstack_push(&yypcb->pcb_globals, inp->din_hash);
	}

	/*
	 * Unlike most constructors, we need to explicitly cook the right-hand
	 * side of the inline definition immediately to prevent recursion.  If
	 * the right-hand side uses the inline itself, the cook will fail.
	 */
	expr = dt_node_cook(expr, DT_IDFLG_REF);

	if (ddp->dd_kind == CTF_K_ARRAY)
		dt_idstack_pop(&yypcb->pcb_globals, inp->din_hash);

	/*
	 * Set the type, attributes, and flags for the inline.  If the right-
	 * hand expression has an identifier, propagate its flags.  Then cook
	 * the identifier to fully initialize it: if we're declaring an inline
	 * associative array this will construct a type signature from 'ddp'.
	 */
	if (dt_node_is_dynamic(expr))
		rdp = dt_ident_resolve(expr->dn_ident);
	else if (expr->dn_kind == DT_NODE_VAR || expr->dn_kind == DT_NODE_SYM)
		rdp = expr->dn_ident;
	else
		rdp = NULL;

	if (rdp != NULL) {
		idp->di_flags |= (rdp->di_flags &
		    (DT_IDFLG_WRITE | DT_IDFLG_USER | DT_IDFLG_PRIM));
	}

	idp->di_attr = dt_attr_min(_dtrace_defattr, expr->dn_attr);
	dt_ident_type_assign(idp, dtt.dtt_ctfp, dtt.dtt_type);
	(void) dt_ident_cook(dnp, idp, &ddp->dd_node);

	/*
	 * Store the parse tree nodes for 'expr' inside of idp->di_data ('inp')
	 * so that they will be preserved with this identifier.  Then pop the
	 * inline declaration from the declaration stack and restore the lexer.
	 */
	inp->din_list = yypcb->pcb_list;
	inp->din_root = expr;

	dt_decl_free(dt_decl_pop());
	yybegin(YYS_CLAUSE);

	/*
	 * Finally, insert the inline identifier into dt_globals to make it
	 * visible, and then cook 'dnp' to check its type against 'expr'.
	 */
	dt_idhash_xinsert(dtp->dt_globals, idp);
	return (dt_node_cook(dnp, DT_IDFLG_REF));
}

dt_node_t *
dt_node_member(dt_decl_t *ddp, char *name, dt_node_t *expr)
{
	dtrace_typeinfo_t dtt;
	dt_node_t *dnp;
	int err;

	if (ddp != NULL) {
		err = dt_decl_type(ddp, &dtt);
		dt_decl_free(ddp);

		if (err != 0)
			longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
	}

	dnp = dt_node_alloc(DT_NODE_MEMBER);
	dnp->dn_membname = name;
	dnp->dn_membexpr = expr;

	if (ddp != NULL)
		dt_node_type_assign(dnp, dtt.dtt_ctfp, dtt.dtt_type,
		    dtt.dtt_flags);

	return (dnp);
}

dt_node_t *
dt_node_xlator(dt_decl_t *ddp, dt_decl_t *sdp, char *name, dt_node_t *members)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dtrace_typeinfo_t src, dst;
	dt_node_t sn, dn;
	dt_xlator_t *dxp;
	dt_node_t *dnp;
	int edst, esrc;
	uint_t kind;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	edst = dt_decl_type(ddp, &dst);
	dt_decl_free(ddp);

	esrc = dt_decl_type(sdp, &src);
	dt_decl_free(sdp);

	if (edst != 0 || esrc != 0) {
		free(name);
		longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
	}

	bzero(&sn, sizeof (sn));
	dt_node_type_assign(&sn, src.dtt_ctfp, src.dtt_type, B_FALSE);

	bzero(&dn, sizeof (dn));
	dt_node_type_assign(&dn, dst.dtt_ctfp, dst.dtt_type, B_FALSE);

	if (dt_xlator_lookup(dtp, &sn, &dn, DT_XLATE_EXACT) != NULL) {
		xyerror(D_XLATE_REDECL,
		    "translator from %s to %s has already been declared\n",
		    dt_node_type_name(&sn, n1, sizeof (n1)),
		    dt_node_type_name(&dn, n2, sizeof (n2)));
	}

	kind = ctf_type_kind(dst.dtt_ctfp,
	    ctf_type_resolve(dst.dtt_ctfp, dst.dtt_type));

	if (kind == CTF_K_FORWARD) {
		xyerror(D_XLATE_SOU, "incomplete struct/union/enum %s\n",
		    dt_type_name(dst.dtt_ctfp, dst.dtt_type, n1, sizeof (n1)));
	}

	if (kind != CTF_K_STRUCT && kind != CTF_K_UNION) {
		xyerror(D_XLATE_SOU,
		    "translator output type must be a struct or union\n");
	}

	dxp = dt_xlator_create(dtp, &src, &dst, name, members, yypcb->pcb_list);
	yybegin(YYS_CLAUSE);
	free(name);

	if (dxp == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	dnp = dt_node_alloc(DT_NODE_XLATOR);
	dnp->dn_xlator = dxp;
	dnp->dn_members = members;

	return (dt_node_cook(dnp, DT_IDFLG_REF));
}

dt_node_t *
dt_node_probe(char *s, int protoc, dt_node_t *nargs, dt_node_t *xargs)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	int nargc, xargc;
	dt_node_t *dnp;

	size_t len = strlen(s) + 3; /* +3 for :: and \0 */
	char *name = alloca(len);

	(void) snprintf(name, len, "::%s", s);
	(void) strhyphenate(name);
	free(s);

	if (strchr(name, '`') != NULL) {
		xyerror(D_PROV_BADNAME, "probe name may not "
		    "contain scoping operator: %s\n", name);
	}

	if (strlen(name) - 2 >= DTRACE_NAMELEN) {
		xyerror(D_PROV_BADNAME, "probe name may not exceed %d "
		    "characters: %s\n", DTRACE_NAMELEN - 1, name);
	}

	dnp = dt_node_alloc(DT_NODE_PROBE);

	dnp->dn_ident = dt_ident_create(name, DT_IDENT_PROBE,
	    DT_IDFLG_ORPHAN, DTRACE_IDNONE, _dtrace_defattr, 0,
	    &dt_idops_probe, NULL, dtp->dt_gen);

	nargc = dt_decl_prototype(nargs, nargs,
	    "probe input", DT_DP_VOID | DT_DP_ANON);

	xargc = dt_decl_prototype(xargs, nargs,
	    "probe output", DT_DP_VOID);

	if (nargc > UINT8_MAX) {
		xyerror(D_PROV_PRARGLEN, "probe %s input prototype exceeds %u "
		    "parameters: %d params used\n", name, UINT8_MAX, nargc);
	}

	if (xargc > UINT8_MAX) {
		xyerror(D_PROV_PRARGLEN, "probe %s output prototype exceeds %u "
		    "parameters: %d params used\n", name, UINT8_MAX, xargc);
	}

	if (dnp->dn_ident == NULL || dt_probe_create(dtp,
	    dnp->dn_ident, protoc, nargs, nargc, xargs, xargc) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	return (dnp);
}

dt_node_t *
dt_node_provider(char *name, dt_node_t *probes)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *dnp = dt_node_alloc(DT_NODE_PROVIDER);
	dt_node_t *lnp;
	size_t len;

	dnp->dn_provname = name;
	dnp->dn_probes = probes;

	if (strchr(name, '`') != NULL) {
		dnerror(dnp, D_PROV_BADNAME, "provider name may not "
		    "contain scoping operator: %s\n", name);
	}

	if ((len = strlen(name)) >= DTRACE_PROVNAMELEN) {
		dnerror(dnp, D_PROV_BADNAME, "provider name may not exceed %d "
		    "characters: %s\n", DTRACE_PROVNAMELEN - 1, name);
	}

	if (isdigit(name[len - 1])) {
		dnerror(dnp, D_PROV_BADNAME, "provider name may not "
		    "end with a digit: %s\n", name);
	}

	/*
	 * Check to see if the provider is already defined or visible through
	 * dtrace(7D).  If so, set dn_provred to treat it as a re-declaration.
	 * If not, create a new provider and set its interface-only flag.  This
	 * flag may be cleared later by calls made to dt_probe_declare().
	 */
	if ((dnp->dn_provider = dt_provider_lookup(dtp, name)) != NULL)
		dnp->dn_provred = B_TRUE;
	else if ((dnp->dn_provider = dt_provider_create(dtp, name)) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
	else
		dnp->dn_provider->pv_flags |= DT_PROVIDER_INTF;

	/*
	 * Store all parse nodes created since we consumed the DT_KEY_PROVIDER
	 * token with the provider and then restore our lexing state to CLAUSE.
	 * Note that if dnp->dn_provred is true, we may end up storing dups of
	 * a provider's interface and implementation: we eat this space because
	 * the implementation will likely need to redeclare probe members, and
	 * therefore may result in those member nodes becoming persistent.
	 */
	for (lnp = yypcb->pcb_list; lnp->dn_link != NULL; lnp = lnp->dn_link)
		continue; /* skip to end of allocation list */

	lnp->dn_link = dnp->dn_provider->pv_nodes;
	dnp->dn_provider->pv_nodes = yypcb->pcb_list;

	yybegin(YYS_CLAUSE);
	return (dnp);
}

dt_node_t *
dt_node_program(dt_node_t *lnp)
{
	dt_node_t *dnp = dt_node_alloc(DT_NODE_PROG);
	dnp->dn_list = lnp;
	return (dnp);
}

/*
 * This function provides the underlying implementation of cooking an
 * identifier given its node, a hash of dynamic identifiers, an identifier
 * kind, and a boolean flag indicating whether we are allowed to instantiate
 * a new identifier if the string is not found.  This function is either
 * called from dt_cook_ident(), below, or directly by the various cooking
 * routines that are allowed to instantiate identifiers (e.g. op2 TOK_ASGN).
 */
static void
dt_xcook_ident(dt_node_t *dnp, dt_idhash_t *dhp, uint_t idkind, int create)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	const char *sname = dt_idhash_name(dhp);
	int uref = 0;

	dtrace_attribute_t attr = _dtrace_defattr;
	dt_ident_t *idp;
	dtrace_syminfo_t dts;
	GElf_Sym sym;

	const char *scope, *mark;
	uchar_t dnkind;
	char *name;

	/*
	 * Look for scoping marks in the identifier.  If one is found, set our
	 * scope to either DTRACE_OBJ_KMODS or UMODS or to the first part of
	 * the string that specifies the scope using an explicit module name.
	 * If two marks in a row are found, set 'uref' (user symbol reference).
	 * Otherwise we set scope to DTRACE_OBJ_EXEC, indicating that normal
	 * scope is desired and we should search the specified idhash.
	 */
	if ((name = strrchr(dnp->dn_string, '`')) != NULL) {
		if (name > dnp->dn_string && name[-1] == '`') {
			uref++;
			name[-1] = '\0';
		}

		if (name == dnp->dn_string + uref)
			scope = uref ? DTRACE_OBJ_UMODS : DTRACE_OBJ_KMODS;
		else
			scope = dnp->dn_string;

		*name++ = '\0'; /* leave name pointing after scoping mark */
		dnkind = DT_NODE_VAR;

	} else if (idkind == DT_IDENT_AGG) {
		scope = DTRACE_OBJ_EXEC;
		name = dnp->dn_string + 1;
		dnkind = DT_NODE_AGG;
	} else {
		scope = DTRACE_OBJ_EXEC;
		name = dnp->dn_string;
		dnkind = DT_NODE_VAR;
	}

	/*
	 * If create is set to false, and we fail our idhash lookup, preset
	 * the errno code to EDT_NOVAR for our final error message below.
	 * If we end up calling dtrace_lookup_by_name(), it will reset the
	 * errno appropriately and that error will be reported instead.
	 */
	(void) dt_set_errno(dtp, EDT_NOVAR);
	mark = uref ? "``" : "`";

	if (scope == DTRACE_OBJ_EXEC && (
	    (dhp != dtp->dt_globals &&
	    (idp = dt_idhash_lookup(dhp, name)) != NULL) ||
	    (dhp == dtp->dt_globals &&
	    (idp = dt_idstack_lookup(&yypcb->pcb_globals, name)) != NULL))) {
		/*
		 * Check that we are referencing the ident in the manner that
		 * matches its type if this is a global lookup.  In the TLS or
		 * local case, we don't know how the ident will be used until
		 * the time operator -> is seen; more parsing is needed.
		 */
		if (idp->di_kind != idkind && dhp == dtp->dt_globals) {
			xyerror(D_IDENT_BADREF, "%s '%s' may not be referenced "
			    "as %s\n", dt_idkind_name(idp->di_kind),
			    idp->di_name, dt_idkind_name(idkind));
		}

		/*
		 * Arrays and aggregations are not cooked individually. They
		 * have dynamic types and must be referenced using operator [].
		 * This is handled explicitly by the code for DT_TOK_LBRAC.
		 */
		if (idp->di_kind != DT_IDENT_ARRAY &&
		    idp->di_kind != DT_IDENT_AGG)
			attr = dt_ident_cook(dnp, idp, NULL);
		else {
			dt_node_type_assign(dnp,
			    DT_DYN_CTFP(dtp), DT_DYN_TYPE(dtp), B_FALSE);
			attr = idp->di_attr;
		}

		free(dnp->dn_string);
		dnp->dn_string = NULL;
		dnp->dn_kind = dnkind;
		dnp->dn_ident = idp;
		dnp->dn_flags |= DT_NF_LVALUE;

		if (idp->di_flags & DT_IDFLG_WRITE)
			dnp->dn_flags |= DT_NF_WRITABLE;

		dt_node_attr_assign(dnp, attr);

	} else if (dhp == dtp->dt_globals && scope != DTRACE_OBJ_EXEC &&
	    dtrace_lookup_by_name(dtp, scope, name, &sym, &dts) == 0) {

		dt_module_t *mp = dt_module_lookup_by_name(dtp, dts.dts_object);
		int umod = (mp->dm_flags & DT_DM_KERNEL) == 0;
		static const char *const kunames[] = { "kernel", "user" };

		dtrace_typeinfo_t dtt;
		dtrace_syminfo_t *sip;

		if (uref ^ umod) {
			xyerror(D_SYM_BADREF, "%s module '%s' symbol '%s' may "
			    "not be referenced as a %s symbol\n", kunames[umod],
			    dts.dts_object, dts.dts_name, kunames[uref]);
		}

		if (dtrace_symbol_type(dtp, &sym, &dts, &dtt) != 0) {
			/*
			 * For now, we special-case EDT_DATAMODEL to clarify
			 * that mixed data models are not currently supported.
			 */
			if (dtp->dt_errno == EDT_DATAMODEL) {
				xyerror(D_SYM_MODEL, "cannot use %s symbol "
				    "%s%s%s in a %s D program\n",
				    dt_module_modelname(mp),
				    dts.dts_object, mark, dts.dts_name,
				    dt_module_modelname(dtp->dt_ddefs));
			}

			xyerror(D_SYM_NOTYPES,
			    "no symbolic type information is available for "
			    "%s%s%s: %s\n", dts.dts_object, mark, dts.dts_name,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));
		}

		idp = dt_ident_create(name, DT_IDENT_SYMBOL, 0, 0,
		    _dtrace_symattr, 0, &dt_idops_thaw, NULL, dtp->dt_gen);

		if (idp == NULL)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

		if (mp->dm_flags & DT_DM_PRIMARY)
			idp->di_flags |= DT_IDFLG_PRIM;

		idp->di_next = dtp->dt_externs;
		dtp->dt_externs = idp;

		if ((sip = malloc(sizeof (dtrace_syminfo_t))) == NULL)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

		bcopy(&dts, sip, sizeof (dtrace_syminfo_t));
		idp->di_data = sip;
		idp->di_ctfp = dtt.dtt_ctfp;
		idp->di_type = dtt.dtt_type;

		free(dnp->dn_string);
		dnp->dn_string = NULL;
		dnp->dn_kind = DT_NODE_SYM;
		dnp->dn_ident = idp;
		dnp->dn_flags |= DT_NF_LVALUE;

		dt_node_type_assign(dnp, dtt.dtt_ctfp, dtt.dtt_type,
		    dtt.dtt_flags);
		dt_node_attr_assign(dnp, _dtrace_symattr);

		if (uref) {
			idp->di_flags |= DT_IDFLG_USER;
			dnp->dn_flags |= DT_NF_USERLAND;
		}

	} else if (scope == DTRACE_OBJ_EXEC && create == B_TRUE) {
		uint_t flags = DT_IDFLG_WRITE;
		uint_t id;

		if (dt_idhash_nextid(dhp, &id) == -1) {
			xyerror(D_ID_OFLOW, "cannot create %s: limit on number "
			    "of %s variables exceeded\n", name, sname);
		}

		if (dhp == yypcb->pcb_locals)
			flags |= DT_IDFLG_LOCAL;
		else if (dhp == dtp->dt_tls)
			flags |= DT_IDFLG_TLS;

		dt_dprintf("create %s %s variable %s, id=%u\n",
		    sname, dt_idkind_name(idkind), name, id);

		if (idkind == DT_IDENT_ARRAY || idkind == DT_IDENT_AGG) {
			idp = dt_idhash_insert(dhp, name,
			    idkind, flags, id, _dtrace_defattr, 0,
			    &dt_idops_assc, NULL, dtp->dt_gen);
		} else {
			idp = dt_idhash_insert(dhp, name,
			    idkind, flags, id, _dtrace_defattr, 0,
			    &dt_idops_thaw, NULL, dtp->dt_gen);
		}

		if (idp == NULL)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

		/*
		 * Arrays and aggregations are not cooked individually. They
		 * have dynamic types and must be referenced using operator [].
		 * This is handled explicitly by the code for DT_TOK_LBRAC.
		 */
		if (idp->di_kind != DT_IDENT_ARRAY &&
		    idp->di_kind != DT_IDENT_AGG)
			attr = dt_ident_cook(dnp, idp, NULL);
		else {
			dt_node_type_assign(dnp,
			    DT_DYN_CTFP(dtp), DT_DYN_TYPE(dtp), B_FALSE);
			attr = idp->di_attr;
		}

		free(dnp->dn_string);
		dnp->dn_string = NULL;
		dnp->dn_kind = dnkind;
		dnp->dn_ident = idp;
		dnp->dn_flags |= DT_NF_LVALUE | DT_NF_WRITABLE;

		dt_node_attr_assign(dnp, attr);

	} else if (scope != DTRACE_OBJ_EXEC) {
		xyerror(D_IDENT_UNDEF, "failed to resolve %s%s%s: %s\n",
		    dnp->dn_string, mark, name,
		    dtrace_errmsg(dtp, dtrace_errno(dtp)));
	} else {
		xyerror(D_IDENT_UNDEF, "failed to resolve %s: %s\n",
		    dnp->dn_string, dtrace_errmsg(dtp, dtrace_errno(dtp)));
	}
}

static dt_node_t *
dt_cook_ident(dt_node_t *dnp, uint_t idflags)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;

	if (dnp->dn_op == DT_TOK_AGG)
		dt_xcook_ident(dnp, dtp->dt_aggs, DT_IDENT_AGG, B_FALSE);
	else
		dt_xcook_ident(dnp, dtp->dt_globals, DT_IDENT_SCALAR, B_FALSE);

	return (dt_node_cook(dnp, idflags));
}

/*
 * Since operators [ and -> can instantiate new variables before we know
 * whether the reference is for a read or a write, we need to check read
 * references to determine if the identifier is currently dt_ident_unref().
 * If so, we report that this first access was to an undefined variable.
 */
static dt_node_t *
dt_cook_var(dt_node_t *dnp, uint_t idflags)
{
	dt_ident_t *idp = dnp->dn_ident;

	if ((idflags & DT_IDFLG_REF) && dt_ident_unref(idp)) {
		dnerror(dnp, D_VAR_UNDEF,
		    "%s%s has not yet been declared or assigned\n",
		    (idp->di_flags & DT_IDFLG_LOCAL) ? "this->" :
		    (idp->di_flags & DT_IDFLG_TLS) ? "self->" : "",
		    idp->di_name);
	}

	dt_node_attr_assign(dnp, dt_ident_cook(dnp, idp, &dnp->dn_args));
	return (dnp);
}

/*ARGSUSED*/
static dt_node_t *
dt_cook_func(dt_node_t *dnp, uint_t idflags)
{
	dt_node_attr_assign(dnp,
	    dt_ident_cook(dnp, dnp->dn_ident, &dnp->dn_args));

	return (dnp);
}

static dt_node_t *
dt_cook_op1(dt_node_t *dnp, uint_t idflags)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *cp = dnp->dn_child;

	char n[DT_TYPE_NAMELEN];
	dtrace_typeinfo_t dtt;
	dt_ident_t *idp;

	ctf_encoding_t e;
	ctf_arinfo_t r;
	ctf_id_t type, base;
	uint_t kind;

	if (dnp->dn_op == DT_TOK_PREINC || dnp->dn_op == DT_TOK_POSTINC ||
	    dnp->dn_op == DT_TOK_PREDEC || dnp->dn_op == DT_TOK_POSTDEC)
		idflags = DT_IDFLG_REF | DT_IDFLG_MOD;
	else
		idflags = DT_IDFLG_REF;

	/*
	 * We allow the unary ++ and -- operators to instantiate new scalar
	 * variables if applied to an identifier; otherwise just cook as usual.
	 */
	if (cp->dn_kind == DT_NODE_IDENT && (idflags & DT_IDFLG_MOD))
		dt_xcook_ident(cp, dtp->dt_globals, DT_IDENT_SCALAR, B_TRUE);

	cp = dnp->dn_child = dt_node_cook(cp, 0); /* don't set idflags yet */

	if (cp->dn_kind == DT_NODE_VAR && dt_ident_unref(cp->dn_ident)) {
		if (dt_type_lookup("int64_t", &dtt) != 0)
			xyerror(D_TYPE_ERR, "failed to lookup int64_t\n");

		dt_ident_type_assign(cp->dn_ident, dtt.dtt_ctfp, dtt.dtt_type);
		dt_node_type_assign(cp, dtt.dtt_ctfp, dtt.dtt_type,
		    dtt.dtt_flags);
	}

	if (cp->dn_kind == DT_NODE_VAR)
		cp->dn_ident->di_flags |= idflags;

	switch (dnp->dn_op) {
	case DT_TOK_DEREF:
		/*
		 * If the deref operator is applied to a translated pointer,
		 * we set our output type to the output of the translation.
		 */
		if ((idp = dt_node_resolve(cp, DT_IDENT_XLPTR)) != NULL) {
			dt_xlator_t *dxp = idp->di_data;

			dnp->dn_ident = &dxp->dx_souid;
			dt_node_type_assign(dnp,
			    dnp->dn_ident->di_ctfp, dnp->dn_ident->di_type,
			    cp->dn_flags & DT_NF_USERLAND);
			break;
		}

		type = ctf_type_resolve(cp->dn_ctfp, cp->dn_type);
		kind = ctf_type_kind(cp->dn_ctfp, type);

		if (kind == CTF_K_ARRAY) {
			if (ctf_array_info(cp->dn_ctfp, type, &r) != 0) {
				dtp->dt_ctferr = ctf_errno(cp->dn_ctfp);
				longjmp(yypcb->pcb_jmpbuf, EDT_CTF);
			} else
				type = r.ctr_contents;
		} else if (kind == CTF_K_POINTER) {
			type = ctf_type_reference(cp->dn_ctfp, type);
		} else {
			xyerror(D_DEREF_NONPTR,
			    "cannot dereference non-pointer type\n");
		}

		dt_node_type_assign(dnp, cp->dn_ctfp, type,
		    cp->dn_flags & DT_NF_USERLAND);
		base = ctf_type_resolve(cp->dn_ctfp, type);
		kind = ctf_type_kind(cp->dn_ctfp, base);

		if (kind == CTF_K_INTEGER && ctf_type_encoding(cp->dn_ctfp,
		    base, &e) == 0 && IS_VOID(e)) {
			xyerror(D_DEREF_VOID,
			    "cannot dereference pointer to void\n");
		}

		if (kind == CTF_K_FUNCTION) {
			xyerror(D_DEREF_FUNC,
			    "cannot dereference pointer to function\n");
		}

		if (kind != CTF_K_ARRAY || dt_node_is_string(dnp))
			dnp->dn_flags |= DT_NF_LVALUE; /* see K&R[A7.4.3] */

		/*
		 * If we propagated the l-value bit and the child operand was
		 * a writable D variable or a binary operation of the form
		 * a + b where a is writable, then propagate the writable bit.
		 * This is necessary to permit assignments to scalar arrays,
		 * which are converted to expressions of the form *(a + i).
		 */
		if ((cp->dn_flags & DT_NF_WRITABLE) ||
		    (cp->dn_kind == DT_NODE_OP2 && cp->dn_op == DT_TOK_ADD &&
		    (cp->dn_left->dn_flags & DT_NF_WRITABLE)))
			dnp->dn_flags |= DT_NF_WRITABLE;

		if ((cp->dn_flags & DT_NF_USERLAND) &&
		    (kind == CTF_K_POINTER || (dnp->dn_flags & DT_NF_REF)))
			dnp->dn_flags |= DT_NF_USERLAND;
		break;

	case DT_TOK_IPOS:
	case DT_TOK_INEG:
		if (!dt_node_is_arith(cp)) {
			xyerror(D_OP_ARITH, "operator %s requires an operand "
			    "of arithmetic type\n", opstr(dnp->dn_op));
		}
		dt_node_type_propagate(cp, dnp); /* see K&R[A7.4.4-6] */
		break;

	case DT_TOK_BNEG:
		if (!dt_node_is_integer(cp)) {
			xyerror(D_OP_INT, "operator %s requires an operand of "
			    "integral type\n", opstr(dnp->dn_op));
		}
		dt_node_type_propagate(cp, dnp); /* see K&R[A7.4.4-6] */
		break;

	case DT_TOK_LNEG:
		if (!dt_node_is_scalar(cp)) {
			xyerror(D_OP_SCALAR, "operator %s requires an operand "
			    "of scalar type\n", opstr(dnp->dn_op));
		}
		dt_node_type_assign(dnp, DT_INT_CTFP(dtp), DT_INT_TYPE(dtp),
		    B_FALSE);
		break;

	case DT_TOK_ADDROF:
		if (cp->dn_kind == DT_NODE_VAR || cp->dn_kind == DT_NODE_AGG) {
			xyerror(D_ADDROF_VAR,
			    "cannot take address of dynamic variable\n");
		}

		if (dt_node_is_dynamic(cp)) {
			xyerror(D_ADDROF_VAR,
			    "cannot take address of dynamic object\n");
		}

		if (!(cp->dn_flags & DT_NF_LVALUE)) {
			xyerror(D_ADDROF_LVAL, /* see K&R[A7.4.2] */
			    "unacceptable operand for unary & operator\n");
		}

		if (cp->dn_flags & DT_NF_BITFIELD) {
			xyerror(D_ADDROF_BITFIELD,
			    "cannot take address of bit-field\n");
		}

		dtt = (dtrace_typeinfo_t){
			.dtt_ctfp = cp->dn_ctfp,
			.dtt_type = cp->dn_type,
		};

		if (dt_type_pointer(&dtt) == -1) {
			xyerror(D_TYPE_ERR, "cannot find type for \"&\": %s*\n",
			    dt_node_type_name(cp, n, sizeof (n)));
		}

		dt_node_type_assign(dnp, dtt.dtt_ctfp, dtt.dtt_type,
		    cp->dn_flags & DT_NF_USERLAND);
		break;

	case DT_TOK_SIZEOF:
		if (cp->dn_flags & DT_NF_BITFIELD) {
			xyerror(D_SIZEOF_BITFIELD,
			    "cannot apply sizeof to a bit-field\n");
		}

		if (dt_node_sizeof(cp) == 0) {
			xyerror(D_SIZEOF_TYPE, "cannot apply sizeof to an "
			    "operand of unknown size\n");
		}

		dt_node_type_assign(dnp, dtp->dt_ddefs->dm_ctfp,
		    ctf_lookup_by_name(dtp->dt_ddefs->dm_ctfp, "size_t"),
		    B_FALSE);
		break;

	case DT_TOK_STRINGOF:
		if (!dt_node_is_scalar(cp) && !dt_node_is_pointer(cp) &&
		    !dt_node_is_strcompat(cp)) {
			xyerror(D_STRINGOF_TYPE,
			    "cannot apply stringof to a value of type %s\n",
			    dt_node_type_name(cp, n, sizeof (n)));
		}
		dt_node_type_assign(dnp, DT_STR_CTFP(dtp), DT_STR_TYPE(dtp),
		    cp->dn_flags & DT_NF_USERLAND);
		break;

	case DT_TOK_PREINC:
	case DT_TOK_POSTINC:
	case DT_TOK_PREDEC:
	case DT_TOK_POSTDEC:
		if (dt_node_is_scalar(cp) == 0) {
			xyerror(D_OP_SCALAR, "operator %s requires operand of "
			    "scalar type\n", opstr(dnp->dn_op));
		}

		if (dt_node_is_vfptr(cp)) {
			xyerror(D_OP_VFPTR, "operator %s requires an operand "
			    "of known size\n", opstr(dnp->dn_op));
		}

		if (!(cp->dn_flags & DT_NF_LVALUE)) {
			xyerror(D_OP_LVAL, "operator %s requires modifiable "
			    "lvalue as an operand\n", opstr(dnp->dn_op));
		}

		if (!(cp->dn_flags & DT_NF_WRITABLE)) {
			xyerror(D_OP_WRITE, "operator %s can only be applied "
			    "to a writable variable\n", opstr(dnp->dn_op));
		}

		dt_node_type_propagate(cp, dnp); /* see K&R[A7.4.1] */
		break;

	default:
		xyerror(D_UNKNOWN, "invalid unary op %s\n", opstr(dnp->dn_op));
	}

	dt_node_attr_assign(dnp, cp->dn_attr);
	return (dnp);
}

static void
dt_assign_common(dt_node_t *dnp)
{
	dt_node_t *lp = dnp->dn_left;
	dt_node_t *rp = dnp->dn_right;
	int op = dnp->dn_op;

	if (rp->dn_kind == DT_NODE_INT)
		dt_cast(lp, rp);

	if (!(lp->dn_flags & DT_NF_LVALUE)) {
		xyerror(D_OP_LVAL, "operator %s requires modifiable "
		    "lvalue as an operand\n", opstr(op));
		/* see K&R[A7.17] */
	}

	if (!(lp->dn_flags & DT_NF_WRITABLE)) {
		xyerror(D_OP_WRITE, "operator %s can only be applied "
		    "to a writable variable\n", opstr(op));
	}

	dt_node_type_propagate(lp, dnp); /* see K&R[A7.17] */
	dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));
}

static dt_node_t *
dt_cook_op2(dt_node_t *dnp, uint_t idflags)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_node_t *lp = dnp->dn_left;
	dt_node_t *rp = dnp->dn_right;
	int op = dnp->dn_op;

	ctf_membinfo_t m;
	ctf_file_t *ctfp;
	ctf_id_t type;
	int kind, val, uref;
	dt_ident_t *idp;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	/*
	 * The expression E1[E2] is identical by definition to *((E1)+(E2)) so
	 * we convert "[" to "+" and glue on "*" at the end (see K&R[A7.3.1])
	 * unless the left-hand side is an untyped D scalar, associative array,
	 * or aggregation.  In these cases, we proceed to case DT_TOK_LBRAC and
	 * handle associative array and aggregation references there.
	 */
	if (op == DT_TOK_LBRAC) {
		if (lp->dn_kind == DT_NODE_IDENT) {
			dt_idhash_t *dhp;
			uint_t idkind;

			if (lp->dn_op == DT_TOK_AGG) {
				dhp = dtp->dt_aggs;
				idp = dt_idhash_lookup(dhp, lp->dn_string + 1);
				idkind = DT_IDENT_AGG;
			} else {
				dhp = dtp->dt_globals;
				idp = dt_idstack_lookup(
				    &yypcb->pcb_globals, lp->dn_string);
				idkind = DT_IDENT_ARRAY;
			}

			if (idp == NULL || dt_ident_unref(idp))
				dt_xcook_ident(lp, dhp, idkind, B_TRUE);
			else
				dt_xcook_ident(lp, dhp, idp->di_kind, B_FALSE);
		} else {
			lp = dnp->dn_left = dt_node_cook(lp, 0);
		}

		/*
		 * Switch op to '+' for *(E1 + E2) array mode in these cases:
		 * (a) lp is a DT_IDENT_ARRAY variable that has already been
		 *	referenced using [] notation (dn_args != NULL).
		 * (b) lp is a non-ARRAY variable that has already been given
		 *	a type by assignment or declaration (!dt_ident_unref())
		 * (c) lp is neither a variable nor an aggregation
		 */
		if (lp->dn_kind == DT_NODE_VAR) {
			if (lp->dn_ident->di_kind == DT_IDENT_ARRAY) {
				if (lp->dn_args != NULL)
					op = DT_TOK_ADD;
			} else if (!dt_ident_unref(lp->dn_ident)) {
				op = DT_TOK_ADD;
			}
		} else if (lp->dn_kind != DT_NODE_AGG) {
			op = DT_TOK_ADD;
		}
	}

	switch (op) {
	case DT_TOK_BAND:
	case DT_TOK_XOR:
	case DT_TOK_BOR:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		if (!dt_node_is_integer(lp) || !dt_node_is_integer(rp)) {
			xyerror(D_OP_INT, "operator %s requires operands of "
			    "integral type\n", opstr(op));
		}

		dt_node_promote(lp, rp, dnp); /* see K&R[A7.11-13] */
		break;

	case DT_TOK_LSH:
	case DT_TOK_RSH:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		if (!dt_node_is_integer(lp) || !dt_node_is_integer(rp)) {
			xyerror(D_OP_INT, "operator %s requires operands of "
			    "integral type\n", opstr(op));
		}

		dt_node_type_propagate(lp, dnp); /* see K&R[A7.8] */
		dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));
		break;

	case DT_TOK_MOD:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		if (!dt_node_is_integer(lp) || !dt_node_is_integer(rp)) {
			xyerror(D_OP_INT, "operator %s requires operands of "
			    "integral type\n", opstr(op));
		}

		dt_node_promote(lp, rp, dnp); /* see K&R[A7.6] */
		break;

	case DT_TOK_MUL:
	case DT_TOK_DIV:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		if (!dt_node_is_arith(lp) || !dt_node_is_arith(rp)) {
			xyerror(D_OP_ARITH, "operator %s requires operands of "
			    "arithmetic type\n", opstr(op));
		}

		dt_node_promote(lp, rp, dnp); /* see K&R[A7.6] */
		break;

	case DT_TOK_LAND:
	case DT_TOK_LXOR:
	case DT_TOK_LOR:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		if (!dt_node_is_scalar(lp) || !dt_node_is_scalar(rp)) {
			xyerror(D_OP_SCALAR, "operator %s requires operands "
			    "of scalar type\n", opstr(op));
		}

		dt_node_type_assign(dnp, DT_INT_CTFP(dtp), DT_INT_TYPE(dtp),
		    B_FALSE);
		dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));
		break;

	case DT_TOK_LT:
	case DT_TOK_LE:
	case DT_TOK_GT:
	case DT_TOK_GE:
	case DT_TOK_EQU:
	case DT_TOK_NEQ:
		/*
		 * The D comparison operators provide the ability to transform
		 * a right-hand identifier into a corresponding enum tag value
		 * if the left-hand side is an enum type.  To do this, we cook
		 * the left-hand side, and then see if the right-hand side is
		 * an unscoped identifier defined in the enum.  If so, we
		 * convert into an integer constant node with the tag's value.
		 */
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);

		kind = ctf_type_kind(lp->dn_ctfp,
		    ctf_type_resolve(lp->dn_ctfp, lp->dn_type));

		if (kind == CTF_K_ENUM && rp->dn_kind == DT_NODE_IDENT &&
		    strchr(rp->dn_string, '`') == NULL && ctf_enum_value(
		    lp->dn_ctfp, lp->dn_type, rp->dn_string, &val) == 0) {

			if ((idp = dt_idstack_lookup(&yypcb->pcb_globals,
			    rp->dn_string)) != NULL) {
				xyerror(D_IDENT_AMBIG,
				    "ambiguous use of operator %s: %s is "
				    "both a %s enum tag and a global %s\n",
				    opstr(op), rp->dn_string,
				    dt_node_type_name(lp, n1, sizeof (n1)),
				    dt_idkind_name(idp->di_kind));
			}

			free(rp->dn_string);
			rp->dn_string = NULL;
			rp->dn_kind = DT_NODE_INT;
			rp->dn_flags |= DT_NF_COOKED;
			rp->dn_op = DT_TOK_INT;
			rp->dn_value = (intmax_t)val;

			dt_node_type_assign(rp, lp->dn_ctfp, lp->dn_type,
			    B_FALSE);
			dt_node_attr_assign(rp, _dtrace_symattr);
		}

		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		/*
		 * The rules for type checking for the relational operators are
		 * described in the ANSI-C spec (see K&R[A7.9-10]).  We perform
		 * the various tests in order from least to most expensive.  We
		 * also allow derived strings to be compared as a first-class
		 * type (resulting in a strcmp(3C)-style comparison), and we
		 * slightly relax the A7.9 rules to permit void pointer
		 * comparisons as in A7.10.  Our users won't be confused by
		 * this since they understand pointers are just numbers, and
		 * relaxing this constraint simplifies the implementation.
		 */
		if (ctf_type_compat(lp->dn_ctfp, lp->dn_type,
		    rp->dn_ctfp, rp->dn_type))
			/*EMPTY*/;
		else if (dt_node_is_integer(lp) && dt_node_is_integer(rp))
			/*EMPTY*/;
		else if (dt_node_is_strcompat(lp) && dt_node_is_strcompat(rp) &&
		    (dt_node_is_string(lp) || dt_node_is_string(rp)))
			/*EMPTY*/;
		else if (dt_node_is_ptrcompat(lp, rp, NULL, NULL) == 0) {
			xyerror(D_OP_INCOMPAT, "operands have "
			    "incompatible types: \"%s\" %s \"%s\"\n",
			    dt_node_type_name(lp, n1, sizeof (n1)), opstr(op),
			    dt_node_type_name(rp, n2, sizeof (n2)));
		}

		dt_node_type_assign(dnp, DT_INT_CTFP(dtp), DT_INT_TYPE(dtp),
		    B_FALSE);
		dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));
		break;

	case DT_TOK_ADD:
	case DT_TOK_SUB: {
		/*
		 * The rules for type checking for the additive operators are
		 * described in the ANSI-C spec (see K&R[A7.7]).  Pointers and
		 * integers may be manipulated according to specific rules.  In
		 * these cases D permits strings to be treated as pointers.
		 */
		int lp_is_ptr, lp_is_int, rp_is_ptr, rp_is_int;

		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		lp_is_ptr = dt_node_is_string(lp) ||
		    (dt_node_is_pointer(lp) && !dt_node_is_vfptr(lp));
		lp_is_int = dt_node_is_integer(lp);

		rp_is_ptr = dt_node_is_string(rp) ||
		    (dt_node_is_pointer(rp) && !dt_node_is_vfptr(rp));
		rp_is_int = dt_node_is_integer(rp);

		if (lp_is_int && rp_is_int) {
			dt_type_promote(lp, rp, &ctfp, &type);
			uref = 0;
		} else if (lp_is_ptr && rp_is_int) {
			ctfp = lp->dn_ctfp;
			type = lp->dn_type;
			uref = lp->dn_flags & DT_NF_USERLAND;
		} else if (lp_is_int && rp_is_ptr && op == DT_TOK_ADD) {
			ctfp = rp->dn_ctfp;
			type = rp->dn_type;
			uref = rp->dn_flags & DT_NF_USERLAND;
		} else if (lp_is_ptr && rp_is_ptr && op == DT_TOK_SUB &&
		    dt_node_is_ptrcompat(lp, rp, NULL, NULL)) {
			ctfp = dtp->dt_ddefs->dm_ctfp;
			type = ctf_lookup_by_name(ctfp, "ptrdiff_t");
			uref = 0;
		} else {
			xyerror(D_OP_INCOMPAT, "operands have incompatible "
			    "types: \"%s\" %s \"%s\"\n",
			    dt_node_type_name(lp, n1, sizeof (n1)), opstr(op),
			    dt_node_type_name(rp, n2, sizeof (n2)));
		}

		dt_node_type_assign(dnp, ctfp, type, B_FALSE);
		dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));

		if (uref)
			dnp->dn_flags |= DT_NF_USERLAND;
		break;
	}

	case DT_TOK_OR_EQ:
	case DT_TOK_XOR_EQ:
	case DT_TOK_AND_EQ:
	case DT_TOK_LSH_EQ:
	case DT_TOK_RSH_EQ:
	case DT_TOK_MOD_EQ:
		if (lp->dn_kind == DT_NODE_IDENT) {
			dt_xcook_ident(lp, dtp->dt_globals,
			    DT_IDENT_SCALAR, B_TRUE);
		}

		lp = dnp->dn_left =
		    dt_node_cook(lp, DT_IDFLG_REF | DT_IDFLG_MOD);

		rp = dnp->dn_right =
		    dt_node_cook(rp, DT_IDFLG_REF | DT_IDFLG_MOD);

		if (!dt_node_is_integer(lp) || !dt_node_is_integer(rp)) {
			xyerror(D_OP_INT, "operator %s requires operands of "
			    "integral type\n", opstr(op));
		}
		goto asgn_common;

	case DT_TOK_MUL_EQ:
	case DT_TOK_DIV_EQ:
		if (lp->dn_kind == DT_NODE_IDENT) {
			dt_xcook_ident(lp, dtp->dt_globals,
			    DT_IDENT_SCALAR, B_TRUE);
		}

		lp = dnp->dn_left =
		    dt_node_cook(lp, DT_IDFLG_REF | DT_IDFLG_MOD);

		rp = dnp->dn_right =
		    dt_node_cook(rp, DT_IDFLG_REF | DT_IDFLG_MOD);

		if (!dt_node_is_arith(lp) || !dt_node_is_arith(rp)) {
			xyerror(D_OP_ARITH, "operator %s requires operands of "
			    "arithmetic type\n", opstr(op));
		}
		goto asgn_common;

	case DT_TOK_ASGN:
		/*
		 * If the left-hand side is an identifier, attempt to resolve
		 * it as either an aggregation or scalar variable.  We pass
		 * B_TRUE to dt_xcook_ident to indicate that a new variable can
		 * be created if no matching variable exists in the namespace.
		 */
		if (lp->dn_kind == DT_NODE_IDENT) {
			if (lp->dn_op == DT_TOK_AGG) {
				dt_xcook_ident(lp, dtp->dt_aggs,
				    DT_IDENT_AGG, B_TRUE);
			} else {
				dt_xcook_ident(lp, dtp->dt_globals,
				    DT_IDENT_SCALAR, B_TRUE);
			}
		}

		lp = dnp->dn_left = dt_node_cook(lp, 0); /* don't set mod yet */
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		/*
		 * If the left-hand side is an aggregation, verify that we are
		 * assigning it the result of an aggregating function.  Once
		 * we've done so, hide the func node in the aggregation and
		 * return the aggregation itself up to the parse tree parent.
		 * This transformation is legal since the assigned function
		 * cannot change identity across disjoint cooking passes and
		 * the argument list subtree is retained for later cooking.
		 */
		if (lp->dn_kind == DT_NODE_AGG) {
			const char *aname = lp->dn_ident->di_name;
			dt_ident_t *oid = lp->dn_ident->di_iarg;

			if (rp->dn_kind != DT_NODE_FUNC ||
			    rp->dn_ident->di_kind != DT_IDENT_AGGFUNC) {
				xyerror(D_AGG_FUNC,
				    "@%s must be assigned the result of "
				    "an aggregating function\n", aname);
			}

			if (oid != NULL && oid != rp->dn_ident) {
				xyerror(D_AGG_REDEF,
				    "aggregation redefined: @%s\n\t "
				    "current: @%s = %s( )\n\tprevious: @%s = "
				    "%s( ) : line %d\n", aname, aname,
				    rp->dn_ident->di_name, aname, oid->di_name,
				    lp->dn_ident->di_lineno);
			} else if (oid == NULL)
				lp->dn_ident->di_iarg = rp->dn_ident;

			/*
			 * Do not allow multiple aggregation assignments in a
			 * single statement, e.g. (@a = count()) = count();
			 * We produce a message as if the result of aggregating
			 * function does not propagate DT_NF_LVALUE.
			 */
			if (lp->dn_aggfun != NULL) {
				xyerror(D_OP_LVAL, "operator = requires "
				    "modifiable lvalue as an operand\n");
			}

			lp->dn_aggfun = rp;
			lp = dt_node_cook(lp, DT_IDFLG_MOD);

			dnp->dn_left = dnp->dn_right = NULL;
			dt_node_free(dnp);

			return (lp);
		}

		/*
		 * If the right-hand side is a dynamic variable that is the
		 * output of a translator, our result is the translated type.
		 */
		if ((idp = dt_node_resolve(rp, DT_IDENT_XLSOU)) != NULL) {
			ctfp = idp->di_ctfp;
			type = idp->di_type;
			uref = idp->di_flags & DT_IDFLG_USER;
		} else {
			ctfp = rp->dn_ctfp;
			type = rp->dn_type;
			uref = rp->dn_flags & DT_NF_USERLAND;
		}

		/*
		 * If the left-hand side of an assignment statement is a virgin
		 * variable created by this compilation pass, reset the type of
		 * this variable to the type of the right-hand side.
		 */
		if (lp->dn_kind == DT_NODE_VAR &&
		    dt_ident_unref(lp->dn_ident)) {
			dt_node_type_assign(lp, ctfp, type, B_FALSE);
			dt_ident_type_assign(lp->dn_ident, ctfp, type);

			if (uref) {
				lp->dn_flags |= DT_NF_USERLAND;
				lp->dn_ident->di_flags |= DT_IDFLG_USER;
			}
		}

		if (lp->dn_kind == DT_NODE_VAR)
			lp->dn_ident->di_flags |= DT_IDFLG_MOD;

		/*
		 * The rules for type checking for the assignment operators are
		 * described in the ANSI-C spec (see K&R[A7.17]).  We share
		 * most of this code with the argument list checking code.
		 */
		if (!dt_node_is_string(lp)) {
			kind = ctf_type_kind(lp->dn_ctfp,
			    ctf_type_resolve(lp->dn_ctfp, lp->dn_type));

			if (kind == CTF_K_ARRAY || kind == CTF_K_FUNCTION) {
				xyerror(D_OP_ARRFUN, "operator %s may not be "
				    "applied to operand of type \"%s\"\n",
				    opstr(op),
				    dt_node_type_name(lp, n1, sizeof (n1)));
			}
		}

		if (idp != NULL && idp->di_kind == DT_IDENT_XLSOU &&
		    ctf_type_compat(lp->dn_ctfp, lp->dn_type, ctfp, type))
			goto asgn_common;

		if (dt_node_is_argcompat(lp, rp))
			goto asgn_common;

		xyerror(D_OP_INCOMPAT,
		    "operands have incompatible types: \"%s\" %s \"%s\"\n",
		    dt_node_type_name(lp, n1, sizeof (n1)), opstr(op),
		    dt_node_type_name(rp, n2, sizeof (n2)));
		/*NOTREACHED*/

	case DT_TOK_ADD_EQ:
	case DT_TOK_SUB_EQ:
		if (lp->dn_kind == DT_NODE_IDENT) {
			dt_xcook_ident(lp, dtp->dt_globals,
			    DT_IDENT_SCALAR, B_TRUE);
		}

		lp = dnp->dn_left =
		    dt_node_cook(lp, DT_IDFLG_REF | DT_IDFLG_MOD);

		rp = dnp->dn_right =
		    dt_node_cook(rp, DT_IDFLG_REF | DT_IDFLG_MOD);

		if (dt_node_is_string(lp) || dt_node_is_string(rp)) {
			xyerror(D_OP_INCOMPAT, "operands have "
			    "incompatible types: \"%s\" %s \"%s\"\n",
			    dt_node_type_name(lp, n1, sizeof (n1)), opstr(op),
			    dt_node_type_name(rp, n2, sizeof (n2)));
		}

		/*
		 * The rules for type checking for the assignment operators are
		 * described in the ANSI-C spec (see K&R[A7.17]).  To these
		 * rules we add that only writable D nodes can be modified.
		 */
		if (dt_node_is_integer(lp) == 0 ||
		    dt_node_is_integer(rp) == 0) {
			if (!dt_node_is_pointer(lp) || dt_node_is_vfptr(lp)) {
				xyerror(D_OP_VFPTR,
				    "operator %s requires left-hand scalar "
				    "operand of known size\n", opstr(op));
			} else if (dt_node_is_integer(rp) == 0 &&
			    dt_node_is_ptrcompat(lp, rp, NULL, NULL) == 0) {
				xyerror(D_OP_INCOMPAT, "operands have "
				    "incompatible types: \"%s\" %s \"%s\"\n",
				    dt_node_type_name(lp, n1, sizeof (n1)),
				    opstr(op),
				    dt_node_type_name(rp, n2, sizeof (n2)));
			}
		}
asgn_common:
		dt_assign_common(dnp);
		break;

	case DT_TOK_PTR:
		/*
		 * If the left-hand side of operator -> is one of the scoping
		 * keywords, permit a local or thread variable to be created or
		 * referenced.
		 */
		if (lp->dn_kind == DT_NODE_IDENT) {
			dt_idhash_t *dhp = NULL;

			if (strcmp(lp->dn_string, "self") == 0) {
				dhp = dtp->dt_tls;
			} else if (strcmp(lp->dn_string, "this") == 0) {
				dhp = yypcb->pcb_locals;
			}
			if (dhp != NULL) {
				if (rp->dn_kind != DT_NODE_VAR) {
					dt_xcook_ident(rp, dhp,
					    DT_IDENT_SCALAR, B_TRUE);
				}

				if (idflags != 0)
					rp = dt_node_cook(rp, idflags);

				/* avoid freeing rp */
				dnp->dn_right = dnp->dn_left;
				dt_node_free(dnp);
				return (rp);
			}
		}
		/*FALLTHRU*/
	case DT_TOK_DOT:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);

		if (rp->dn_kind != DT_NODE_IDENT) {
			xyerror(D_OP_IDENT, "operator %s must be followed by "
			    "an identifier\n", opstr(op));
		}

		if ((idp = dt_node_resolve(lp, DT_IDENT_XLSOU)) != NULL ||
		    (idp = dt_node_resolve(lp, DT_IDENT_XLPTR)) != NULL) {
			/*
			 * If the left-hand side is a translated struct or ptr,
			 * the type of the left is the translation output type.
			 */
			dt_xlator_t *dxp = idp->di_data;

			if (dt_xlator_member(dxp, rp->dn_string) == NULL) {
				xyerror(D_XLATE_NOCONV,
				    "translator does not define conversion "
				    "for member: %s\n", rp->dn_string);
			}

			ctfp = idp->di_ctfp;
			type = ctf_type_resolve(ctfp, idp->di_type);
			uref = idp->di_flags & DT_IDFLG_USER;
		} else {
			ctfp = lp->dn_ctfp;
			type = ctf_type_resolve(ctfp, lp->dn_type);
			uref = lp->dn_flags & DT_NF_USERLAND;
		}

		kind = ctf_type_kind(ctfp, type);

		if (op == DT_TOK_PTR) {
			if (kind != CTF_K_POINTER) {
				xyerror(D_OP_PTR, "operator %s must be "
				    "applied to a pointer\n", opstr(op));
			}
			type = ctf_type_reference(ctfp, type);
			type = ctf_type_resolve(ctfp, type);
			kind = ctf_type_kind(ctfp, type);
		}

		/*
		 * If we follow a reference to a forward declaration tag,
		 * search the entire type space for the actual definition.
		 */
		while (kind == CTF_K_FORWARD) {
			char *tag = ctf_type_name(ctfp, type, n1, sizeof (n1));
			dtrace_typeinfo_t dtt;

			if (tag != NULL && dt_type_lookup(tag, &dtt) == 0 &&
			    (dtt.dtt_ctfp != ctfp || dtt.dtt_type != type)) {
				ctfp = dtt.dtt_ctfp;
				type = ctf_type_resolve(ctfp, dtt.dtt_type);
				kind = ctf_type_kind(ctfp, type);
			} else {
				xyerror(D_OP_INCOMPLETE,
				    "operator %s cannot be applied to a "
				    "forward declaration: no %s definition "
				    "is available\n", opstr(op), tag);
			}
		}

		if (kind != CTF_K_STRUCT && kind != CTF_K_UNION) {
			if (op == DT_TOK_PTR) {
				xyerror(D_OP_SOU, "operator -> cannot be "
				    "applied to pointer to type \"%s\"; must "
				    "be applied to a struct or union pointer\n",
				    ctf_type_name(ctfp, type, n1, sizeof (n1)));
			} else {
				xyerror(D_OP_SOU, "operator %s cannot be "
				    "applied to type \"%s\"; must be applied "
				    "to a struct or union\n", opstr(op),
				    ctf_type_name(ctfp, type, n1, sizeof (n1)));
			}
		}

		if (ctf_member_info(ctfp, type, rp->dn_string, &m) == CTF_ERR) {
			xyerror(D_TYPE_MEMBER,
			    "%s is not a member of %s\n", rp->dn_string,
			    ctf_type_name(ctfp, type, n1, sizeof (n1)));
		}

		type = ctf_type_resolve(ctfp, m.ctm_type);
		kind = ctf_type_kind(ctfp, type);

		dt_node_type_assign(dnp, ctfp, m.ctm_type, B_FALSE);
		dt_node_attr_assign(dnp, lp->dn_attr);

		if (op == DT_TOK_PTR && (kind != CTF_K_ARRAY ||
		    dt_node_is_string(dnp)))
			dnp->dn_flags |= DT_NF_LVALUE; /* see K&R[A7.3.3] */

		if (op == DT_TOK_DOT && (lp->dn_flags & DT_NF_LVALUE) &&
		    (kind != CTF_K_ARRAY || dt_node_is_string(dnp)))
			dnp->dn_flags |= DT_NF_LVALUE; /* see K&R[A7.3.3] */

		if (lp->dn_flags & DT_NF_WRITABLE)
			dnp->dn_flags |= DT_NF_WRITABLE;

		if (uref && (kind == CTF_K_POINTER ||
		    (dnp->dn_flags & DT_NF_REF)))
			dnp->dn_flags |= DT_NF_USERLAND;
		break;

	case DT_TOK_LBRAC: {
		/*
		 * If op is DT_TOK_LBRAC, we know from the special-case code at
		 * the top that lp is either a D variable or an aggregation.
		 */
		dt_node_t *lnp;

		/*
		 * If the left-hand side is an aggregation, just set dn_aggtup
		 * to the right-hand side and return the cooked aggregation.
		 * This transformation is legal since we are just collapsing
		 * nodes to simplify later processing, and the entire aggtup
		 * parse subtree is retained for subsequent cooking passes.
		 */
		if (lp->dn_kind == DT_NODE_AGG) {
			if (lp->dn_aggtup != NULL) {
				xyerror(D_AGG_MDIM, "improper attempt to "
				    "reference @%s as a multi-dimensional "
				    "array\n", lp->dn_ident->di_name);
			}

			lp->dn_aggtup = rp;
			lp = dt_node_cook(lp, 0);

			dnp->dn_left = dnp->dn_right = NULL;
			dt_node_free(dnp);

			return (lp);
		}

		assert(lp->dn_kind == DT_NODE_VAR);
		idp = lp->dn_ident;

		/*
		 * If the left-hand side is a non-global scalar that hasn't yet
		 * been referenced or modified, it was just created by self->
		 * or this-> and we can convert it from scalar to assoc array.
		 */
		if (idp->di_kind == DT_IDENT_SCALAR && dt_ident_unref(idp) &&
		    (idp->di_flags & (DT_IDFLG_LOCAL | DT_IDFLG_TLS)) != 0) {

			if (idp->di_flags & DT_IDFLG_LOCAL) {
				xyerror(D_ARR_LOCAL,
				    "local variables may not be used as "
				    "associative arrays: %s\n", idp->di_name);
			}

			dt_dprintf("morph variable %s (id %u) from scalar to "
			    "array\n", idp->di_name, idp->di_id);

			dt_ident_morph(idp, DT_IDENT_ARRAY,
			    &dt_idops_assc, NULL);
		}

		if (idp->di_kind != DT_IDENT_ARRAY) {
			xyerror(D_IDENT_BADREF, "%s '%s' may not be referenced "
			    "as %s\n", dt_idkind_name(idp->di_kind),
			    idp->di_name, dt_idkind_name(DT_IDENT_ARRAY));
		}

		/*
		 * Now that we've confirmed our left-hand side is a DT_NODE_VAR
		 * of idkind DT_IDENT_ARRAY, we need to splice the [ node from
		 * the parse tree and leave a cooked DT_NODE_VAR in its place
		 * where dn_args for the VAR node is the right-hand 'rp' tree,
		 * as shown in the parse tree diagram below:
		 *
		 *	  /			    /
		 * [ OP2 "[" ]=dnp		[ VAR ]=dnp
		 *	 /	\	  =>	   |
		 *	/	 \		   +- dn_args -> [ ??? ]=rp
		 * [ VAR ]=lp  [ ??? ]=rp
		 *
		 * Since the final dt_node_cook(dnp) can fail using longjmp we
		 * must perform the transformations as a group first by over-
		 * writing 'dnp' to become the VAR node, so that the parse tree
		 * is guaranteed to be in a consistent state if the cook fails.
		 */
		assert(lp->dn_kind == DT_NODE_VAR);
		assert(lp->dn_args == NULL);

		lnp = dnp->dn_link;
		bcopy(lp, dnp, sizeof (dt_node_t));
		dnp->dn_link = lnp;

		dnp->dn_args = rp;
		dnp->dn_list = NULL;

		dt_node_free(lp);
		return (dt_node_cook(dnp, idflags));
	}

	case DT_TOK_XLATE: {
		dt_xlator_t *dxp;

		assert(lp->dn_kind == DT_NODE_TYPE);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);
		dxp = dt_xlator_lookup(dtp, rp, lp, DT_XLATE_FUZZY);

		if (dxp == NULL) {
			xyerror(D_XLATE_NONE,
			    "cannot translate from \"%s\" to \"%s\"\n",
			    dt_node_type_name(rp, n1, sizeof (n1)),
			    dt_node_type_name(lp, n2, sizeof (n2)));
		}

		dnp->dn_ident = dt_xlator_ident(dxp, lp->dn_ctfp, lp->dn_type);
		dt_node_type_assign(dnp, DT_DYN_CTFP(dtp), DT_DYN_TYPE(dtp),
		    B_FALSE);
		dt_node_attr_assign(dnp,
		    dt_attr_min(rp->dn_attr, dnp->dn_ident->di_attr));
		break;
	}

	case DT_TOK_LPAR: {
		ctf_id_t ltype, rtype;
		uint_t lkind, rkind;

		assert(lp->dn_kind == DT_NODE_TYPE);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		ltype = ctf_type_resolve(lp->dn_ctfp, lp->dn_type);
		lkind = ctf_type_kind(lp->dn_ctfp, ltype);

		rtype = ctf_type_resolve(rp->dn_ctfp, rp->dn_type);
		rkind = ctf_type_kind(rp->dn_ctfp, rtype);

		/*
		 * The rules for casting are loosely explained in K&R[A7.5]
		 * and K&R[A6].  Basically, we can cast to the same type or
		 * same base type, between any kind of scalar values, from
		 * arrays to pointers, and we can cast anything to void.
		 * To these rules D adds casts from scalars to strings.
		 */
		if (ctf_type_compat(lp->dn_ctfp, lp->dn_type,
		    rp->dn_ctfp, rp->dn_type))
			/*EMPTY*/;
		else if (dt_node_is_scalar(lp) &&
		    (dt_node_is_scalar(rp) || rkind == CTF_K_FUNCTION))
			/*EMPTY*/;
		else if (dt_node_is_void(lp))
			/*EMPTY*/;
		else if (lkind == CTF_K_POINTER && dt_node_is_pointer(rp))
			/*EMPTY*/;
		else if (dt_node_is_string(lp) && (dt_node_is_scalar(rp) ||
		    dt_node_is_pointer(rp) || dt_node_is_strcompat(rp)))
			/*EMPTY*/;
		else {
			xyerror(D_CAST_INVAL,
			    "invalid cast expression: \"%s\" to \"%s\"\n",
			    dt_node_type_name(rp, n1, sizeof (n1)),
			    dt_node_type_name(lp, n2, sizeof (n2)));
		}

		dt_node_type_propagate(lp, dnp); /* see K&R[A7.5] */
		dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));

		/*
		 * If it's a pointer then should be able to (attempt to)
		 * assign to it.
		 */
		if (lkind == CTF_K_POINTER)
			dnp->dn_flags |= DT_NF_WRITABLE;

		break;
	}

	case DT_TOK_COMMA:
		lp = dnp->dn_left = dt_node_cook(lp, DT_IDFLG_REF);
		rp = dnp->dn_right = dt_node_cook(rp, DT_IDFLG_REF);

		if (dt_node_is_dynamic(lp) || dt_node_is_dynamic(rp)) {
			xyerror(D_OP_DYN, "operator %s operands "
			    "cannot be of dynamic type\n", opstr(op));
		}

		if (dt_node_is_actfunc(lp) || dt_node_is_actfunc(rp)) {
			xyerror(D_OP_ACT, "operator %s operands "
			    "cannot be actions\n", opstr(op));
		}

		dt_node_type_propagate(rp, dnp); /* see K&R[A7.18] */
		dt_node_attr_assign(dnp, dt_attr_min(lp->dn_attr, rp->dn_attr));
		break;

	default:
		xyerror(D_UNKNOWN, "invalid binary op %s\n", opstr(op));
	}

	/*
	 * Complete the conversion of E1[E2] to *((E1)+(E2)) that we started
	 * at the top of our switch() above (see K&R[A7.3.1]).  Since E2 is
	 * parsed as an argument_expression_list by dt_grammar.y, we can
	 * end up with a comma-separated list inside of a non-associative
	 * array reference.  We check for this and report an appropriate error.
	 */
	if (dnp->dn_op == DT_TOK_LBRAC && op == DT_TOK_ADD) {
		dt_node_t *pnp;

		if (rp->dn_list != NULL) {
			xyerror(D_ARR_BADREF,
			    "cannot access %s as an associative array\n",
			    dt_node_name(lp, n1, sizeof (n1)));
		}

		dnp->dn_op = DT_TOK_ADD;
		pnp = dt_node_op1(DT_TOK_DEREF, dnp);

		/*
		 * Cook callbacks are not typically permitted to allocate nodes.
		 * When we do, we must insert them in the middle of an existing
		 * allocation list rather than having them appended to the pcb
		 * list because the sub-expression may be part of a definition.
		 */
		assert(yypcb->pcb_list == pnp);
		yypcb->pcb_list = pnp->dn_link;

		pnp->dn_link = dnp->dn_link;
		dnp->dn_link = pnp;

		return (dt_node_cook(pnp, DT_IDFLG_REF));
	}

	return (dnp);
}

/*ARGSUSED*/
static dt_node_t *
dt_cook_op3(dt_node_t *dnp, uint_t idflags)
{
	dt_node_t *lp, *rp;
	ctf_file_t *ctfp;
	ctf_id_t type;

	dnp->dn_expr = dt_node_cook(dnp->dn_expr, DT_IDFLG_REF);
	lp = dnp->dn_left = dt_node_cook(dnp->dn_left, DT_IDFLG_REF);
	rp = dnp->dn_right = dt_node_cook(dnp->dn_right, DT_IDFLG_REF);

	if (!dt_node_is_scalar(dnp->dn_expr)) {
		xyerror(D_OP_SCALAR,
		    "operator ?: expression must be of scalar type\n");
	}

	if (dt_node_is_dynamic(lp) || dt_node_is_dynamic(rp)) {
		xyerror(D_OP_DYN,
		    "operator ?: operands cannot be of dynamic type\n");
	}

	/*
	 * The rules for type checking for the ternary operator are complex and
	 * are described in the ANSI-C spec (see K&R[A7.16]).  We implement
	 * the various tests in order from least to most expensive.
	 */
	if (ctf_type_compat(lp->dn_ctfp, lp->dn_type,
	    rp->dn_ctfp, rp->dn_type)) {
		ctfp = lp->dn_ctfp;
		type = lp->dn_type;
	} else if (dt_node_is_integer(lp) && dt_node_is_integer(rp)) {
		dt_type_promote(lp, rp, &ctfp, &type);
	} else if (dt_node_is_strcompat(lp) && dt_node_is_strcompat(rp) &&
	    (dt_node_is_string(lp) || dt_node_is_string(rp))) {
		ctfp = DT_STR_CTFP(yypcb->pcb_hdl);
		type = DT_STR_TYPE(yypcb->pcb_hdl);
	} else if (dt_node_is_ptrcompat(lp, rp, &ctfp, &type) == 0) {
		xyerror(D_OP_INCOMPAT,
		    "operator ?: operands must have compatible types\n");
	}

	if (dt_node_is_actfunc(lp) || dt_node_is_actfunc(rp)) {
		xyerror(D_OP_ACT, "action cannot be "
		    "used in a conditional context\n");
	}

	dt_node_type_assign(dnp, ctfp, type, B_FALSE);
	dt_node_attr_assign(dnp, dt_attr_min(dnp->dn_expr->dn_attr,
	    dt_attr_min(lp->dn_attr, rp->dn_attr)));

	return (dnp);
}

static dt_node_t *
dt_cook_statement(dt_node_t *dnp, uint_t idflags)
{
	dnp->dn_expr = dt_node_cook(dnp->dn_expr, idflags);
	dt_node_attr_assign(dnp, dnp->dn_expr->dn_attr);

	return (dnp);
}

/*
 * If dn_aggfun is set, this node is a collapsed aggregation assignment (see
 * the special case code for DT_TOK_ASGN in dt_cook_op2() above), in which
 * case we cook both the tuple and the function call.  If dn_aggfun is NULL,
 * this node is just a reference to the aggregation's type and attributes.
 */
/*ARGSUSED*/
static dt_node_t *
dt_cook_aggregation(dt_node_t *dnp, uint_t idflags)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;

	if (dnp->dn_aggfun != NULL) {
		dnp->dn_aggfun = dt_node_cook(dnp->dn_aggfun, DT_IDFLG_REF);
		dt_node_attr_assign(dnp, dt_ident_cook(dnp,
		    dnp->dn_ident, &dnp->dn_aggtup));
	} else {
		dt_node_type_assign(dnp, DT_DYN_CTFP(dtp), DT_DYN_TYPE(dtp),
		    B_FALSE);
		dt_node_attr_assign(dnp, dnp->dn_ident->di_attr);
	}

	return (dnp);
}

/*
 * Since D permits new variable identifiers to be instantiated in any program
 * expression, we may need to cook a clause's predicate either before or after
 * the action list depending on the program code in question.  Consider:
 *
 * probe-description-list	probe-description-list
 * /x++/			/x == 0/
 * {				{
 *     trace(x);		    trace(x++);
 * }				}
 *
 * In the left-hand example, the predicate uses operator ++ to instantiate 'x'
 * as a variable of type int64_t.  The predicate must be cooked first because
 * otherwise the statement trace(x) refers to an unknown identifier.  In the
 * right-hand example, the action list uses ++ to instantiate 'x'; the action
 * list must be cooked first because otherwise the predicate x == 0 refers to
 * an unknown identifier.  In order to simplify programming, we support both.
 *
 * When cooking a clause, we cook the action statements before the predicate by
 * default, since it seems more common to create or modify identifiers in the
 * action list.  If cooking fails due to an unknown identifier, we attempt to
 * cook the predicate (i.e. do it first) and then go back and cook the actions.
 * If this, too, fails (or if we get an error other than D_IDENT_UNDEF) we give
 * up and report failure back to the user.  There are five possible paths:
 *
 * cook actions = OK, cook predicate = OK -> OK
 * cook actions = OK, cook predicate = ERR -> ERR
 * cook actions = ERR, cook predicate = ERR -> ERR
 * cook actions = ERR, cook predicate = OK, cook actions = OK -> OK
 * cook actions = ERR, cook predicate = OK, cook actions = ERR -> ERR
 *
 * The programmer can still defeat our scheme by creating circular definition
 * dependencies between predicates and actions, as in this example clause:
 *
 * probe-description-list
 * /x++ && y == 0/
 * {
 * 	trace(x + y++);
 * }
 *
 * but it doesn't seem worth the complexity to handle such rare cases.  The
 * user can simply use the D variable declaration syntax to work around them.
 */
static dt_node_t *
dt_cook_clause(dt_node_t *dnp, uint_t idflags)
{
	volatile int err, tries;
	jmp_buf ojb;

	/*
	 * Before assigning dn_ctxattr, temporarily assign the probe attribute
	 * to 'dnp' itself to force an attribute check and minimum violation.
	 */
	dt_node_attr_assign(dnp, yypcb->pcb_pinfo.dtp_attr);
	dnp->dn_ctxattr = yypcb->pcb_pinfo.dtp_attr;

	bcopy(yypcb->pcb_jmpbuf, ojb, sizeof (jmp_buf));
	tries = 0;

	if (dnp->dn_pred != NULL && (err = setjmp(yypcb->pcb_jmpbuf)) != 0) {
		bcopy(ojb, yypcb->pcb_jmpbuf, sizeof (jmp_buf));
		if (tries++ != 0 || err != EDT_COMPILER || (
		    yypcb->pcb_hdl->dt_errtag != dt_errtag(D_IDENT_UNDEF) &&
		    yypcb->pcb_hdl->dt_errtag != dt_errtag(D_VAR_UNDEF)))
			longjmp(yypcb->pcb_jmpbuf, err);
	}

	if (tries == 0) {
		yylabel("action list");

		dt_node_attr_assign(dnp,
		    dt_node_list_cook(&dnp->dn_acts, idflags));

		bcopy(ojb, yypcb->pcb_jmpbuf, sizeof (jmp_buf));
		yylabel(NULL);
	}

	if (dnp->dn_pred != NULL) {
		yylabel("predicate");

		dnp->dn_pred = dt_node_cook(dnp->dn_pred, idflags);
		dt_node_attr_assign(dnp,
		    dt_attr_min(dnp->dn_attr, dnp->dn_pred->dn_attr));

		if (!dt_node_is_scalar(dnp->dn_pred)) {
			xyerror(D_PRED_SCALAR,
			    "predicate result must be of scalar type\n");
		}

		yylabel(NULL);
	}

	if (tries != 0) {
		yylabel("action list");

		dt_node_attr_assign(dnp,
		    dt_node_list_cook(&dnp->dn_acts, idflags));

		yylabel(NULL);
	}

	return (dnp);
}

/*ARGSUSED*/
static dt_node_t *
dt_cook_inline(dt_node_t *dnp, uint_t idflags)
{
	dt_idnode_t *inp = dnp->dn_ident->di_iarg;
	dt_ident_t *rdp;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	assert(dnp->dn_ident->di_flags & DT_IDFLG_INLINE);
	assert(inp->din_root->dn_flags & DT_NF_COOKED);

	/*
	 * If we are inlining a translation, verify that the inline declaration
	 * type exactly matches the type that is returned by the translation.
	 * Otherwise just use dt_node_is_argcompat() to check the types.
	 */
	if ((rdp = dt_node_resolve(inp->din_root, DT_IDENT_XLSOU)) != NULL ||
	    (rdp = dt_node_resolve(inp->din_root, DT_IDENT_XLPTR)) != NULL) {

		ctf_file_t *lctfp = dnp->dn_ctfp;
		ctf_id_t ltype = ctf_type_resolve(lctfp, dnp->dn_type);

		dt_xlator_t *dxp = rdp->di_data;
		ctf_file_t *rctfp = dxp->dx_dst_ctfp;
		ctf_id_t rtype = dxp->dx_dst_base;

		if (ctf_type_kind(lctfp, ltype) == CTF_K_POINTER) {
			ltype = ctf_type_reference(lctfp, ltype);
			ltype = ctf_type_resolve(lctfp, ltype);
		}

		if (ctf_type_compat(lctfp, ltype, rctfp, rtype) == 0) {
			dnerror(dnp, D_OP_INCOMPAT,
			    "inline %s definition uses incompatible types: "
			    "\"%s\" = \"%s\"\n", dnp->dn_ident->di_name,
			    dt_type_name(lctfp, ltype, n1, sizeof (n1)),
			    dt_type_name(rctfp, rtype, n2, sizeof (n2)));
		}

	} else if (dt_node_is_argcompat(dnp, inp->din_root) == 0) {
		dnerror(dnp, D_OP_INCOMPAT,
		    "inline %s definition uses incompatible types: "
		    "\"%s\" = \"%s\"\n", dnp->dn_ident->di_name,
		    dt_node_type_name(dnp, n1, sizeof (n1)),
		    dt_node_type_name(inp->din_root, n2, sizeof (n2)));
	}

	return (dnp);
}

static dt_node_t *
dt_cook_member(dt_node_t *dnp, uint_t idflags)
{
	dnp->dn_membexpr = dt_node_cook(dnp->dn_membexpr, idflags);
	dt_node_attr_assign(dnp, dnp->dn_membexpr->dn_attr);
	return (dnp);
}

/*ARGSUSED*/
static dt_node_t *
dt_cook_xlator(dt_node_t *dnp, uint_t idflags)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_xlator_t *dxp = dnp->dn_xlator;
	dt_node_t *mnp;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	dtrace_attribute_t attr = _dtrace_maxattr;
	ctf_membinfo_t ctm;

	/*
	 * Before cooking each translator member, we push a reference to the
	 * hash containing translator-local identifiers on to pcb_globals to
	 * temporarily interpose these identifiers in front of other globals.
	 */
	dt_idstack_push(&yypcb->pcb_globals, dxp->dx_locals);

	for (mnp = dnp->dn_members; mnp != NULL; mnp = mnp->dn_list) {
		if (ctf_member_info(dxp->dx_dst_ctfp, dxp->dx_dst_type,
		    mnp->dn_membname, &ctm) == CTF_ERR) {
			xyerror(D_XLATE_MEMB,
			    "translator member %s is not a member of %s\n",
			    mnp->dn_membname, ctf_type_name(dxp->dx_dst_ctfp,
			    dxp->dx_dst_type, n1, sizeof (n1)));
		}

		(void) dt_node_cook(mnp, DT_IDFLG_REF);
		dt_node_type_assign(mnp, dxp->dx_dst_ctfp, ctm.ctm_type,
		    B_FALSE);
		attr = dt_attr_min(attr, mnp->dn_attr);

		if (dt_node_is_argcompat(mnp, mnp->dn_membexpr) == 0) {
			xyerror(D_XLATE_INCOMPAT,
			    "translator member %s definition uses "
			    "incompatible types: \"%s\" = \"%s\"\n",
			    mnp->dn_membname,
			    dt_node_type_name(mnp, n1, sizeof (n1)),
			    dt_node_type_name(mnp->dn_membexpr,
			    n2, sizeof (n2)));
		}
	}

	dt_idstack_pop(&yypcb->pcb_globals, dxp->dx_locals);

	dxp->dx_souid.di_attr = attr;
	dxp->dx_ptrid.di_attr = attr;

	dt_node_type_assign(dnp, DT_DYN_CTFP(dtp), DT_DYN_TYPE(dtp), B_FALSE);
	dt_node_attr_assign(dnp, _dtrace_defattr);

	return (dnp);
}

static void
dt_node_provider_cmp_argv(dt_provider_t *pvp, dt_node_t *pnp, const char *kind,
    uint_t old_argc, dt_node_t *old_argv, uint_t new_argc, dt_node_t *new_argv)
{
	dt_probe_t *prp = pnp->dn_ident->di_data;
	uint_t i;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	if (old_argc != new_argc) {
		dnerror(pnp, D_PROV_INCOMPAT,
		    "probe %s:%s %s prototype mismatch:\n"
		    "\t current: %u arg%s\n\tprevious: %u arg%s\n",
		    pvp->pv_desc.dtvd_name, prp->pr_ident->di_name, kind,
		    new_argc, new_argc != 1 ? "s" : "",
		    old_argc, old_argc != 1 ? "s" : "");
	}

	for (i = 0; i < old_argc; i++,
	    old_argv = old_argv->dn_list, new_argv = new_argv->dn_list) {
		if (ctf_type_cmp(old_argv->dn_ctfp, old_argv->dn_type,
		    new_argv->dn_ctfp, new_argv->dn_type) == 0)
			continue;

		dnerror(pnp, D_PROV_INCOMPAT,
		    "probe %s:%s %s prototype argument #%u mismatch:\n"
		    "\t current: %s\n\tprevious: %s\n",
		    pvp->pv_desc.dtvd_name, prp->pr_ident->di_name, kind, i + 1,
		    dt_node_type_name(new_argv, n1, sizeof (n1)),
		    dt_node_type_name(old_argv, n2, sizeof (n2)));
	}
}

/*
 * Compare a new probe declaration with an existing probe definition (either
 * from a previous declaration or cached from the kernel).  If the existing
 * definition and declaration both have an input and output parameter list,
 * compare both lists.  Otherwise compare only the output parameter lists.
 */
static void
dt_node_provider_cmp(dt_provider_t *pvp, dt_node_t *pnp,
    dt_probe_t *old, dt_probe_t *new)
{
	dt_node_provider_cmp_argv(pvp, pnp, "output",
	    old->pr_xargc, old->pr_xargs, new->pr_xargc, new->pr_xargs);

	if (old->pr_nargs != old->pr_xargs && new->pr_nargs != new->pr_xargs) {
		dt_node_provider_cmp_argv(pvp, pnp, "input",
		    old->pr_nargc, old->pr_nargs, new->pr_nargc, new->pr_nargs);
	}

	if (old->pr_nargs == old->pr_xargs && new->pr_nargs != new->pr_xargs) {
		if (pvp->pv_flags & DT_PROVIDER_IMPL) {
			dnerror(pnp, D_PROV_INCOMPAT,
			    "provider interface mismatch: %s\n"
			    "\t current: probe %s:%s has an output prototype\n"
			    "\tprevious: probe %s:%s has no output prototype\n",
			    pvp->pv_desc.dtvd_name, pvp->pv_desc.dtvd_name,
			    new->pr_ident->di_name, pvp->pv_desc.dtvd_name,
			    old->pr_ident->di_name);
		}

		if (old->pr_ident->di_gen == yypcb->pcb_hdl->dt_gen)
			old->pr_ident->di_flags |= DT_IDFLG_ORPHAN;

		dt_idhash_delete(pvp->pv_probes, old->pr_ident);
		dt_probe_declare(pvp, new);
	}
}

static void
dt_cook_probe(dt_node_t *dnp, dt_provider_t *pvp)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dt_probe_t *prp = dnp->dn_ident->di_data;

	dt_xlator_t *dxp;
	uint_t i;

	char n1[DT_TYPE_NAMELEN];
	char n2[DT_TYPE_NAMELEN];

	if (prp->pr_nargs == prp->pr_xargs)
		return;

	for (i = 0; i < prp->pr_xargc; i++) {
		dt_node_t *xnp = prp->pr_xargv[i];
		dt_node_t *nnp = prp->pr_nargv[prp->pr_mapping[i]];

		if ((dxp = dt_xlator_lookup(dtp,
		    nnp, xnp, DT_XLATE_FUZZY)) != NULL) {
			if (dt_provider_xref(dtp, pvp, dxp->dx_id) != 0)
				longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
			continue;
		}

		if (dt_node_is_argcompat(nnp, xnp))
			continue; /* no translator defined and none required */

		dnerror(dnp, D_PROV_PRXLATOR, "translator for %s:%s output "
		    "argument #%u from %s to %s is not defined\n",
		    pvp->pv_desc.dtvd_name, dnp->dn_ident->di_name, i + 1,
		    dt_node_type_name(nnp, n1, sizeof (n1)),
		    dt_node_type_name(xnp, n2, sizeof (n2)));
	}
}

/*ARGSUSED*/
static dt_node_t *
dt_cook_provider(dt_node_t *dnp, uint_t idflags)
{
	dt_provider_t *pvp = dnp->dn_provider;
	dt_node_t *pnp;

	/*
	 * If we're declaring a provider for the first time and it is unknown
	 * to dtrace(7D), insert the probe definitions into the provider's hash.
	 * If we're redeclaring a known provider, verify the interface matches.
	 */
	for (pnp = dnp->dn_probes; pnp != NULL; pnp = pnp->dn_list) {
		const char *probename = pnp->dn_ident->di_name;
		dt_probe_t *prp = dt_probe_lookup(pvp, probename);

		assert(pnp->dn_kind == DT_NODE_PROBE);

		if (prp != NULL && dnp->dn_provred) {
			dt_node_provider_cmp(pvp, pnp,
			    prp, pnp->dn_ident->di_data);
		} else if (prp == NULL && dnp->dn_provred) {
			dnerror(pnp, D_PROV_INCOMPAT,
			    "provider interface mismatch: %s\n"
			    "\t current: probe %s:%s defined\n"
			    "\tprevious: probe %s:%s not defined\n",
			    dnp->dn_provname, dnp->dn_provname,
			    probename, dnp->dn_provname, probename);
		} else if (prp != NULL) {
			dnerror(pnp, D_PROV_PRDUP, "probe redeclared: %s:%s\n",
			    dnp->dn_provname, probename);
		} else
			dt_probe_declare(pvp, pnp->dn_ident->di_data);

		dt_cook_probe(pnp, pvp);
	}

	return (dnp);
}

/*ARGSUSED*/
static dt_node_t *
dt_cook_none(dt_node_t *dnp, uint_t idflags)
{
	return (dnp);
}

static dt_node_t *(*dt_cook_funcs[])(dt_node_t *, uint_t) = {
	dt_cook_none,		/* DT_NODE_FREE */
	dt_cook_none,		/* DT_NODE_INT */
	dt_cook_none,		/* DT_NODE_STRING */
	dt_cook_ident,		/* DT_NODE_IDENT */
	dt_cook_var,		/* DT_NODE_VAR */
	dt_cook_none,		/* DT_NODE_SYM */
	dt_cook_none,		/* DT_NODE_TYPE */
	dt_cook_func,		/* DT_NODE_FUNC */
	dt_cook_op1,		/* DT_NODE_OP1 */
	dt_cook_op2,		/* DT_NODE_OP2 */
	dt_cook_op3,		/* DT_NODE_OP3 */
	dt_cook_statement,	/* DT_NODE_DEXPR */
	dt_cook_statement,	/* DT_NODE_DFUNC */
	dt_cook_aggregation,	/* DT_NODE_AGG */
	dt_cook_none,		/* DT_NODE_PDESC */
	dt_cook_clause,		/* DT_NODE_CLAUSE */
	dt_cook_inline,		/* DT_NODE_INLINE */
	dt_cook_member,		/* DT_NODE_MEMBER */
	dt_cook_xlator,		/* DT_NODE_XLATOR */
	dt_cook_none,		/* DT_NODE_PROBE */
	dt_cook_provider,	/* DT_NODE_PROVIDER */
	dt_cook_none,		/* DT_NODE_PROG */
	dt_cook_none,		/* DT_NODE_IF */
};

/*
 * Recursively cook the parse tree starting at the specified node.  The idflags
 * parameter is used to indicate the type of reference (r/w) and is applied to
 * the resulting identifier if it is a D variable or D aggregation.
 */
dt_node_t *
dt_node_cook(dt_node_t *dnp, uint_t idflags)
{
	int oldlineno = yylineno;

	yylineno = dnp->dn_line;

	assert(dnp->dn_kind <
	    sizeof (dt_cook_funcs) / sizeof (dt_cook_funcs[0]));
	dnp = dt_cook_funcs[dnp->dn_kind](dnp, idflags);
	dnp->dn_flags |= DT_NF_COOKED;

	if (dnp->dn_kind == DT_NODE_VAR || dnp->dn_kind == DT_NODE_AGG)
		dnp->dn_ident->di_flags |= idflags;

	yylineno = oldlineno;
	return (dnp);
}

dtrace_attribute_t
dt_node_list_cook(dt_node_t **pnp, uint_t idflags)
{
	dtrace_attribute_t attr = _dtrace_defattr;
	dt_node_t *dnp, *nnp;

	for (dnp = (pnp != NULL ? *pnp : NULL); dnp != NULL; dnp = nnp) {
		nnp = dnp->dn_list;
		dnp = *pnp = dt_node_cook(dnp, idflags);
		attr = dt_attr_min(attr, dnp->dn_attr);
		dnp->dn_list = nnp;
		pnp = &dnp->dn_list;
	}

	return (attr);
}

void
dt_node_list_free(dt_node_t **pnp)
{
	dt_node_t *dnp, *nnp;

	for (dnp = (pnp != NULL ? *pnp : NULL); dnp != NULL; dnp = nnp) {
		nnp = dnp->dn_list;
		dt_node_free(dnp);
	}

	if (pnp != NULL)
		*pnp = NULL;
}

void
dt_node_link_free(dt_node_t **pnp)
{
	dt_node_t *dnp, *nnp;

	for (dnp = (pnp != NULL ? *pnp : NULL); dnp != NULL; dnp = nnp) {
		nnp = dnp->dn_link;
		dt_node_free(dnp);
	}

	for (dnp = (pnp != NULL ? *pnp : NULL); dnp != NULL; dnp = nnp) {
		nnp = dnp->dn_link;
		free(dnp);
	}

	if (pnp != NULL)
		*pnp = NULL;
}

dt_node_t *
dt_node_link(dt_node_t *lp, dt_node_t *rp)
{
	dt_node_t *dnp;

	if (lp == NULL)
		return (rp);
	else if (rp == NULL)
		return (lp);

	for (dnp = lp; dnp->dn_list != NULL; dnp = dnp->dn_list)
		continue;

	dnp->dn_list = rp;
	return (lp);
}

/*
 * Compute the DOF dtrace_diftype_t representation of a node's type.  This is
 * called from a variety of places in the library so it cannot assume yypcb
 * is valid: any references to handle-specific data must be made through 'dtp'.
 */
void
dt_node_diftype(dtrace_hdl_t *dtp, const dt_node_t *dnp, dtrace_diftype_t *tp)
{
	if (dnp->dn_ctfp == DT_STR_CTFP(dtp) &&
	    dnp->dn_type == DT_STR_TYPE(dtp)) {
		tp->dtdt_kind = DIF_TYPE_STRING;
		tp->dtdt_ckind = CTF_K_UNKNOWN;
	} else {
		tp->dtdt_kind = DIF_TYPE_CTF;
		tp->dtdt_ckind = ctf_type_kind(dnp->dn_ctfp,
		    ctf_type_resolve(dnp->dn_ctfp, dnp->dn_type));
	}

	tp->dtdt_flags = (dnp->dn_flags & DT_NF_REF) ?
	    (dnp->dn_flags & DT_NF_USERLAND) ? DIF_TF_BYUREF :
	    DIF_TF_BYREF : 0;
	tp->dtdt_pad = 0;
	tp->dtdt_size = ctf_type_size(dnp->dn_ctfp, dnp->dn_type);
}

/*
 * Output the parse tree as D.  The "-xtree=8" argument will call this
 * function to print out the program after any syntactic sugar
 * transformations have been applied (e.g. to implement "if").  The
 * resulting output can be used to understand the transformations
 * applied by these features, or to run such a script on a system that
 * does not support these features
 *
 * Note that the output does not express precisely the same program as
 * the input.  In particular:
 *  - Only the clauses are output.  #pragma options, variable
 *    declarations, etc. are excluded.
 *  - Command argument substitution has already been done, so the output
 *    will not contain e.g. $$1, but rather the substituted string.
 */
void
dt_printd(dt_node_t *dnp, FILE *fp, int depth)
{
	dt_node_t *arg;

	switch (dnp->dn_kind) {
	case DT_NODE_INT:
		(void) fprintf(fp, "0x%llx", (u_longlong_t)dnp->dn_value);
		if (!(dnp->dn_flags & DT_NF_SIGNED))
			(void) fprintf(fp, "u");
		break;

	case DT_NODE_STRING: {
		char *escd = strchr2esc(dnp->dn_string, strlen(dnp->dn_string));
		(void) fprintf(fp, "\"%s\"", escd);
		free(escd);
		break;
	}

	case DT_NODE_IDENT:
		(void) fprintf(fp, "%s", dnp->dn_string);
		break;

	case DT_NODE_VAR:
		(void) fprintf(fp, "%s%s",
		    (dnp->dn_ident->di_flags & DT_IDFLG_LOCAL) ? "this->" :
		    (dnp->dn_ident->di_flags & DT_IDFLG_TLS) ? "self->" : "",
		    dnp->dn_ident->di_name);

		if (dnp->dn_args != NULL) {
			(void) fprintf(fp, "[");

			for (arg = dnp->dn_args; arg != NULL;
			    arg = arg->dn_list) {
				dt_printd(arg, fp, 0);
				if (arg->dn_list != NULL)
					(void) fprintf(fp, ", ");
			}

			(void) fprintf(fp, "]");
		}
		break;

	case DT_NODE_SYM: {
		const dtrace_syminfo_t *dts = dnp->dn_ident->di_data;
		(void) fprintf(fp, "%s`%s", dts->dts_object, dts->dts_name);
		break;
	}
	case DT_NODE_FUNC:
		(void) fprintf(fp, "%s(", dnp->dn_ident->di_name);

		for (arg = dnp->dn_args; arg != NULL; arg = arg->dn_list) {
			dt_printd(arg, fp, 0);
			if (arg->dn_list != NULL)
				(void) fprintf(fp, ", ");
		}
		(void) fprintf(fp, ")");
		break;

	case DT_NODE_OP1:
		(void) fprintf(fp, "%s(", opstr(dnp->dn_op));
		dt_printd(dnp->dn_child, fp, 0);
		(void) fprintf(fp, ")");
		break;

	case DT_NODE_OP2:
		(void) fprintf(fp, "(");
		dt_printd(dnp->dn_left, fp, 0);
		if (dnp->dn_op == DT_TOK_LPAR) {
			(void) fprintf(fp, ")");
			dt_printd(dnp->dn_right, fp, 0);
			break;
		}
		if (dnp->dn_op == DT_TOK_PTR || dnp->dn_op == DT_TOK_DOT ||
		    dnp->dn_op == DT_TOK_LBRAC)
			(void) fprintf(fp, "%s", opstr(dnp->dn_op));
		else
			(void) fprintf(fp, " %s ", opstr(dnp->dn_op));
		dt_printd(dnp->dn_right, fp, 0);
		if (dnp->dn_op == DT_TOK_LBRAC) {
			dt_node_t *ln = dnp->dn_right;
			while (ln->dn_list != NULL) {
				(void) fprintf(fp, ", ");
				dt_printd(ln->dn_list, fp, depth);
				ln = ln->dn_list;
			}
			(void) fprintf(fp, "]");
		}
		(void) fprintf(fp, ")");
		break;

	case DT_NODE_OP3:
		(void) fprintf(fp, "(");
		dt_printd(dnp->dn_expr, fp, 0);
		(void) fprintf(fp, " ? ");
		dt_printd(dnp->dn_left, fp, 0);
		(void) fprintf(fp, " : ");
		dt_printd(dnp->dn_right, fp, 0);
		(void) fprintf(fp, ")");
		break;

	case DT_NODE_DEXPR:
	case DT_NODE_DFUNC:
		(void) fprintf(fp, "%*s", depth * 8, "");
		dt_printd(dnp->dn_expr, fp, depth + 1);
		(void) fprintf(fp, ";\n");
		break;

	case DT_NODE_PDESC:
		(void) fprintf(fp, "%s:%s:%s:%s",
		    dnp->dn_desc->dtpd_provider, dnp->dn_desc->dtpd_mod,
		    dnp->dn_desc->dtpd_func, dnp->dn_desc->dtpd_name);
		break;

	case DT_NODE_CLAUSE:
		for (arg = dnp->dn_pdescs; arg != NULL; arg = arg->dn_list) {
			dt_printd(arg, fp, 0);
			if (arg->dn_list != NULL)
				(void) fprintf(fp, ",");
			(void) fprintf(fp, "\n");
		}

		if (dnp->dn_pred != NULL) {
			(void) fprintf(fp, "/");
			dt_printd(dnp->dn_pred, fp, 0);
			(void) fprintf(fp, "/\n");
		}
			(void) fprintf(fp, "{\n");

		for (arg = dnp->dn_acts; arg != NULL; arg = arg->dn_list)
			dt_printd(arg, fp, depth + 1);
		(void) fprintf(fp, "}\n");
		(void) fprintf(fp, "\n");
		break;

	case DT_NODE_IF:
		(void) fprintf(fp, "%*sif (", depth * 8, "");
		dt_printd(dnp->dn_conditional, fp, 0);
		(void) fprintf(fp, ") {\n");

		for (arg = dnp->dn_body; arg != NULL; arg = arg->dn_list)
			dt_printd(arg, fp, depth + 1);
		if (dnp->dn_alternate_body == NULL) {
			(void) fprintf(fp, "%*s}\n", depth * 8, "");
		} else {
			(void) fprintf(fp, "%*s} else {\n", depth * 8, "");
			for (arg = dnp->dn_alternate_body; arg != NULL;
			    arg = arg->dn_list)
				dt_printd(arg, fp, depth + 1);
			(void) fprintf(fp, "%*s}\n", depth * 8, "");
		}

		break;

	default:
		(void) fprintf(fp, "/* bad node %p, kind %d */\n",
		    (void *)dnp, dnp->dn_kind);
	}
}

void
dt_node_printr(dt_node_t *dnp, FILE *fp, int depth)
{
	char n[DT_TYPE_NAMELEN], buf[BUFSIZ], a[8];
	const dtrace_syminfo_t *dts;
	const dt_idnode_t *inp;
	dt_node_t *arg;

	(void) fprintf(fp, "%*s", depth * 2, "");
	(void) dt_attr_str(dnp->dn_attr, a, sizeof (a));

	if (dnp->dn_ctfp != NULL && dnp->dn_type != CTF_ERR &&
	    ctf_type_name(dnp->dn_ctfp, dnp->dn_type, n, sizeof (n)) != NULL) {
		(void) snprintf(buf, BUFSIZ, "type=<%s> attr=%s flags=", n, a);
	} else {
		(void) snprintf(buf, BUFSIZ, "type=<%ld> attr=%s flags=",
		    dnp->dn_type, a);
	}

	if (dnp->dn_flags != 0) {
		n[0] = '\0';
		if (dnp->dn_flags & DT_NF_SIGNED)
			(void) strcat(n, ",SIGN");
		if (dnp->dn_flags & DT_NF_COOKED)
			(void) strcat(n, ",COOK");
		if (dnp->dn_flags & DT_NF_REF)
			(void) strcat(n, ",REF");
		if (dnp->dn_flags & DT_NF_LVALUE)
			(void) strcat(n, ",LVAL");
		if (dnp->dn_flags & DT_NF_WRITABLE)
			(void) strcat(n, ",WRITE");
		if (dnp->dn_flags & DT_NF_BITFIELD)
			(void) strcat(n, ",BITF");
		if (dnp->dn_flags & DT_NF_USERLAND)
			(void) strcat(n, ",USER");
		(void) strcat(buf, n + 1);
	} else
		(void) strcat(buf, "0");

	switch (dnp->dn_kind) {
	case DT_NODE_FREE:
		(void) fprintf(fp, "FREE <node %p>\n", (void *)dnp);
		break;

	case DT_NODE_INT:
		(void) fprintf(fp, "INT 0x%llx (%s)\n",
		    (u_longlong_t)dnp->dn_value, buf);
		break;

	case DT_NODE_STRING:
		(void) fprintf(fp, "STRING \"%s\" (%s)\n", dnp->dn_string, buf);
		break;

	case DT_NODE_IDENT:
		(void) fprintf(fp, "IDENT %s (%s)\n", dnp->dn_string, buf);
		break;

	case DT_NODE_VAR:
		(void) fprintf(fp, "VARIABLE %s%s (%s)\n",
		    (dnp->dn_ident->di_flags & DT_IDFLG_LOCAL) ? "this->" :
		    (dnp->dn_ident->di_flags & DT_IDFLG_TLS) ? "self->" : "",
		    dnp->dn_ident->di_name, buf);

		if (dnp->dn_args != NULL)
			(void) fprintf(fp, "%*s[\n", depth * 2, "");

		for (arg = dnp->dn_args; arg != NULL; arg = arg->dn_list) {
			dt_node_printr(arg, fp, depth + 1);
			if (arg->dn_list != NULL)
				(void) fprintf(fp, "%*s,\n", depth * 2, "");
		}

		if (dnp->dn_args != NULL)
			(void) fprintf(fp, "%*s]\n", depth * 2, "");
		break;

	case DT_NODE_SYM:
		dts = dnp->dn_ident->di_data;
		(void) fprintf(fp, "SYMBOL %s`%s (%s)\n",
		    dts->dts_object, dts->dts_name, buf);
		break;

	case DT_NODE_TYPE:
		if (dnp->dn_string != NULL) {
			(void) fprintf(fp, "TYPE (%s) %s\n",
			    buf, dnp->dn_string);
		} else
			(void) fprintf(fp, "TYPE (%s)\n", buf);
		break;

	case DT_NODE_FUNC:
		(void) fprintf(fp, "FUNC %s (%s)\n",
		    dnp->dn_ident->di_name, buf);

		for (arg = dnp->dn_args; arg != NULL; arg = arg->dn_list) {
			dt_node_printr(arg, fp, depth + 1);
			if (arg->dn_list != NULL)
				(void) fprintf(fp, "%*s,\n", depth * 2, "");
		}
		break;

	case DT_NODE_OP1:
		(void) fprintf(fp, "OP1 %s (%s)\n", opstr(dnp->dn_op), buf);
		dt_node_printr(dnp->dn_child, fp, depth + 1);
		break;

	case DT_NODE_OP2:
		(void) fprintf(fp, "OP2 %s (%s)\n", opstr(dnp->dn_op), buf);
		dt_node_printr(dnp->dn_left, fp, depth + 1);
		dt_node_printr(dnp->dn_right, fp, depth + 1);
		if (dnp->dn_op == DT_TOK_LBRAC) {
			dt_node_t *ln = dnp->dn_right;
			while (ln->dn_list != NULL) {
				dt_node_printr(ln->dn_list, fp, depth + 1);
				ln = ln->dn_list;
			}
		}
		break;

	case DT_NODE_OP3:
		(void) fprintf(fp, "OP3 (%s)\n", buf);
		dt_node_printr(dnp->dn_expr, fp, depth + 1);
		(void) fprintf(fp, "%*s?\n", depth * 2, "");
		dt_node_printr(dnp->dn_left, fp, depth + 1);
		(void) fprintf(fp, "%*s:\n", depth * 2, "");
		dt_node_printr(dnp->dn_right, fp, depth + 1);
		break;

	case DT_NODE_DEXPR:
	case DT_NODE_DFUNC:
		(void) fprintf(fp, "D EXPRESSION attr=%s\n", a);
		dt_node_printr(dnp->dn_expr, fp, depth + 1);
		break;

	case DT_NODE_AGG:
		(void) fprintf(fp, "AGGREGATE @%s attr=%s [\n",
		    dnp->dn_ident->di_name, a);

		for (arg = dnp->dn_aggtup; arg != NULL; arg = arg->dn_list) {
			dt_node_printr(arg, fp, depth + 1);
			if (arg->dn_list != NULL)
				(void) fprintf(fp, "%*s,\n", depth * 2, "");
		}

		if (dnp->dn_aggfun) {
			(void) fprintf(fp, "%*s] = ", depth * 2, "");
			dt_node_printr(dnp->dn_aggfun, fp, depth + 1);
		} else
			(void) fprintf(fp, "%*s]\n", depth * 2, "");

		if (dnp->dn_aggfun)
			(void) fprintf(fp, "%*s)\n", depth * 2, "");
		break;

	case DT_NODE_PDESC:
		(void) fprintf(fp, "PDESC %s:%s:%s:%s [%u]\n",
		    dnp->dn_desc->dtpd_provider, dnp->dn_desc->dtpd_mod,
		    dnp->dn_desc->dtpd_func, dnp->dn_desc->dtpd_name,
		    dnp->dn_desc->dtpd_id);
		break;

	case DT_NODE_CLAUSE:
		(void) fprintf(fp, "CLAUSE attr=%s\n", a);

		for (arg = dnp->dn_pdescs; arg != NULL; arg = arg->dn_list)
			dt_node_printr(arg, fp, depth + 1);

		(void) fprintf(fp, "%*sCTXATTR %s\n", depth * 2, "",
		    dt_attr_str(dnp->dn_ctxattr, a, sizeof (a)));

		if (dnp->dn_pred != NULL) {
			(void) fprintf(fp, "%*sPREDICATE /\n", depth * 2, "");
			dt_node_printr(dnp->dn_pred, fp, depth + 1);
			(void) fprintf(fp, "%*s/\n", depth * 2, "");
		}

		for (arg = dnp->dn_acts; arg != NULL; arg = arg->dn_list)
			dt_node_printr(arg, fp, depth + 1);
		(void) fprintf(fp, "\n");
		break;

	case DT_NODE_INLINE:
		inp = dnp->dn_ident->di_iarg;

		(void) fprintf(fp, "INLINE %s (%s)\n",
		    dnp->dn_ident->di_name, buf);
		dt_node_printr(inp->din_root, fp, depth + 1);
		break;

	case DT_NODE_MEMBER:
		(void) fprintf(fp, "MEMBER %s (%s)\n", dnp->dn_membname, buf);
		if (dnp->dn_membexpr)
			dt_node_printr(dnp->dn_membexpr, fp, depth + 1);
		break;

	case DT_NODE_XLATOR:
		(void) fprintf(fp, "XLATOR (%s)", buf);

		if (ctf_type_name(dnp->dn_xlator->dx_src_ctfp,
		    dnp->dn_xlator->dx_src_type, n, sizeof (n)) != NULL)
			(void) fprintf(fp, " from <%s>", n);

		if (ctf_type_name(dnp->dn_xlator->dx_dst_ctfp,
		    dnp->dn_xlator->dx_dst_type, n, sizeof (n)) != NULL)
			(void) fprintf(fp, " to <%s>", n);

		(void) fprintf(fp, "\n");

		for (arg = dnp->dn_members; arg != NULL; arg = arg->dn_list)
			dt_node_printr(arg, fp, depth + 1);
		break;

	case DT_NODE_PROBE:
		(void) fprintf(fp, "PROBE %s\n", dnp->dn_ident->di_name);
		break;

	case DT_NODE_PROVIDER:
		(void) fprintf(fp, "PROVIDER %s (%s)\n",
		    dnp->dn_provname, dnp->dn_provred ? "redecl" : "decl");
		for (arg = dnp->dn_probes; arg != NULL; arg = arg->dn_list)
			dt_node_printr(arg, fp, depth + 1);
		break;

	case DT_NODE_PROG:
		(void) fprintf(fp, "PROGRAM attr=%s\n", a);
		for (arg = dnp->dn_list; arg != NULL; arg = arg->dn_list)
			dt_node_printr(arg, fp, depth + 1);
		break;

	case DT_NODE_IF:
		(void) fprintf(fp, "IF attr=%s CONDITION:\n", a);

		dt_node_printr(dnp->dn_conditional, fp, depth + 1);

		(void) fprintf(fp, "%*sIF BODY: \n", depth * 2, "");
		for (arg = dnp->dn_body; arg != NULL; arg = arg->dn_list)
			dt_node_printr(arg, fp, depth + 1);

		if (dnp->dn_alternate_body != NULL) {
			(void) fprintf(fp, "%*sIF ELSE: \n", depth * 2, "");
			for (arg = dnp->dn_alternate_body; arg != NULL;
			    arg = arg->dn_list)
				dt_node_printr(arg, fp, depth + 1);
		}

		break;

	default:
		(void) fprintf(fp, "<bad node %p, kind %d>\n",
		    (void *)dnp, dnp->dn_kind);
	}
}

int
dt_node_root(dt_node_t *dnp)
{
	yypcb->pcb_root = dnp;
	return (0);
}

/*PRINTFLIKE3*/
void
dnerror(const dt_node_t *dnp, dt_errtag_t tag, const char *format, ...)
{
	int oldlineno = yylineno;
	va_list ap;

	yylineno = dnp->dn_line;

	va_start(ap, format);
	xyvwarn(tag, format, ap);
	va_end(ap);

	yylineno = oldlineno;
	longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
}

/*PRINTFLIKE3*/
void
dnwarn(const dt_node_t *dnp, dt_errtag_t tag, const char *format, ...)
{
	int oldlineno = yylineno;
	va_list ap;

	yylineno = dnp->dn_line;

	va_start(ap, format);
	xyvwarn(tag, format, ap);
	va_end(ap);

	yylineno = oldlineno;
}

/*PRINTFLIKE2*/
void
xyerror(dt_errtag_t tag, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	xyvwarn(tag, format, ap);
	va_end(ap);

	longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
}

/*PRINTFLIKE2*/
void
xywarn(dt_errtag_t tag, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	xyvwarn(tag, format, ap);
	va_end(ap);
}

void
xyvwarn(dt_errtag_t tag, const char *format, va_list ap)
{
	if (yypcb == NULL)
		return; /* compiler is not currently active: act as a no-op */

	dt_set_errmsg(yypcb->pcb_hdl, dt_errtag(tag), yypcb->pcb_region,
	    yypcb->pcb_filetag, yypcb->pcb_fileptr ? yylineno : 0, format, ap);
}

/*PRINTFLIKE1*/
void
yyerror(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	yyvwarn(format, ap);
	va_end(ap);

	longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
}

/*PRINTFLIKE1*/
void
yywarn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	yyvwarn(format, ap);
	va_end(ap);
}

void
yyvwarn(const char *format, va_list ap)
{
	if (yypcb == NULL)
		return; /* compiler is not currently active: act as a no-op */

	dt_set_errmsg(yypcb->pcb_hdl, dt_errtag(D_SYNTAX), yypcb->pcb_region,
	    yypcb->pcb_filetag, yypcb->pcb_fileptr ? yylineno : 0, format, ap);

	if (strchr(format, '\n') == NULL) {
		dtrace_hdl_t *dtp = yypcb->pcb_hdl;
		size_t len = strlen(dtp->dt_errmsg);
		char *p, *s = dtp->dt_errmsg + len;
		size_t n = sizeof (dtp->dt_errmsg) - len;

		if (yytext[0] == '\0')
			(void) snprintf(s, n, " near end of input");
		else if (yytext[0] == '\n')
			(void) snprintf(s, n, " near end of line");
		else {
			if ((p = strchr(yytext, '\n')) != NULL)
				*p = '\0'; /* crop at newline */
			(void) snprintf(s, n, " near \"%s\"", yytext);
		}
	}
}

void
yylabel(const char *label)
{
	dt_dprintf("set label to <%s>\n", label ? label : "NULL");
	yypcb->pcb_region = label;
}

int
yywrap(void)
{
	return (1); /* indicate that lex should return a zero token for EOF */
}
