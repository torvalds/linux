/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2013 Joyent, Inc. All rights reserved.
 */

#ifndef	_DT_PARSER_H
#define	_DT_PARSER_H

#include <sys/types.h>
#include <sys/dtrace.h>

#include <libctf.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <dt_errtags.h>
#include <dt_ident.h>
#include <dt_decl.h>
#include <dt_xlator.h>
#include <dt_list.h>

typedef struct dt_node {
	ctf_file_t *dn_ctfp;	/* CTF type container for node's type */
	ctf_id_t dn_type;	/* CTF type reference for node's type */
	uchar_t dn_kind;	/* node kind (DT_NODE_*, defined below) */
	uchar_t dn_flags;	/* node flags (DT_NF_*, defined below) */
	ushort_t dn_op;		/* operator (DT_TOK_*, defined by lex) */
	int dn_line;		/* line number for error messages */
	int dn_reg;		/* register allocated by cg */
	dtrace_attribute_t dn_attr; /* node stability attributes */

	/*
	 * D compiler nodes, as is the usual style, contain a union of the
	 * different sub-elements required by the various kinds of nodes.
	 * These sub-elements are accessed using the macros defined below.
	 */
	union {
		struct {
			uintmax_t _value;	/* integer value */
			char *_string;		/* string value */
		} _const;

		struct {
			dt_ident_t *_ident;	/* identifier reference */
			struct dt_node *_links[3]; /* child node pointers */
		} _nodes;

		struct {
			struct dt_node *_descs;	/* list of descriptions */
			struct dt_node *_pred;	/* predicate expression */
			struct dt_node *_acts;	/* action statement list */
			dt_idhash_t *_locals;	/* local variable hash */
			dtrace_attribute_t _attr; /* context attributes */
		} _clause;

		struct {
			char *_spec;		/* specifier string (if any) */
			dtrace_probedesc_t *_desc; /* final probe description */
		} _pdesc;

		struct {
			char *_name;		/* string name of member */
			struct dt_node *_expr;	/* expression node pointer */
			dt_xlator_t *_xlator;	/* translator reference */
			uint_t _id;		/* member identifier */
		} _member;

		struct {
			dt_xlator_t *_xlator;	/* translator reference */
			struct dt_node *_xmemb;	/* individual xlator member */
			struct dt_node *_membs;	/* list of member nodes */
		} _xlator;

		struct {
			char *_name;		/* string name of provider */
			struct dt_provider *_pvp; /* provider references */
			struct dt_node *_probes;  /* list of probe nodes */
			int _redecl;		/* provider redeclared */
		} _provider;

		struct {
			struct dt_node *_conditional;
			struct dt_node *_body;
			struct dt_node *_alternate_body;
		} _conditional;
	} dn_u;

	struct dt_node *dn_list; /* parse tree list link */
	struct dt_node *dn_link; /* allocation list link */
} dt_node_t;

#define	dn_value	dn_u._const._value	/* DT_NODE_INT */
#define	dn_string	dn_u._const._string	/* STRING, IDENT, TYPE */
#define	dn_ident	dn_u._nodes._ident	/* VAR,SYM,FUN,AGG,INL,PROBE */
#define	dn_args		dn_u._nodes._links[0]	/* DT_NODE_VAR, FUNC */
#define	dn_child	dn_u._nodes._links[0]	/* DT_NODE_OP1 */
#define	dn_left		dn_u._nodes._links[0]	/* DT_NODE_OP2, OP3 */
#define	dn_right	dn_u._nodes._links[1]	/* DT_NODE_OP2, OP3 */
#define	dn_expr		dn_u._nodes._links[2]	/* DT_NODE_OP3, DEXPR */
#define	dn_aggfun	dn_u._nodes._links[0]	/* DT_NODE_AGG */
#define	dn_aggtup	dn_u._nodes._links[1]	/* DT_NODE_AGG */
#define	dn_pdescs	dn_u._clause._descs	/* DT_NODE_CLAUSE */
#define	dn_pred		dn_u._clause._pred	/* DT_NODE_CLAUSE */
#define	dn_acts		dn_u._clause._acts	/* DT_NODE_CLAUSE */
#define	dn_locals	dn_u._clause._locals	/* DT_NODE_CLAUSE */
#define	dn_ctxattr	dn_u._clause._attr	/* DT_NODE_CLAUSE */
#define	dn_spec		dn_u._pdesc._spec	/* DT_NODE_PDESC */
#define	dn_desc		dn_u._pdesc._desc	/* DT_NODE_PDESC */
#define	dn_membname	dn_u._member._name	/* DT_NODE_MEMBER */
#define	dn_membexpr	dn_u._member._expr	/* DT_NODE_MEMBER */
#define	dn_membxlator	dn_u._member._xlator	/* DT_NODE_MEMBER */
#define	dn_membid	dn_u._member._id	/* DT_NODE_MEMBER */
#define	dn_xlator	dn_u._xlator._xlator	/* DT_NODE_XLATOR */
#define	dn_xmember	dn_u._xlator._xmemb	/* DT_NODE_XLATOR */
#define	dn_members	dn_u._xlator._membs	/* DT_NODE_XLATOR */
#define	dn_provname	dn_u._provider._name	/* DT_NODE_PROVIDER */
#define	dn_provider	dn_u._provider._pvp	/* DT_NODE_PROVIDER */
#define	dn_provred	dn_u._provider._redecl	/* DT_NODE_PROVIDER */
#define	dn_probes	dn_u._provider._probes	/* DT_NODE_PROVIDER */

/* DT_NODE_IF: */
#define	dn_conditional		dn_u._conditional._conditional
#define	dn_body			dn_u._conditional._body
#define	dn_alternate_body	dn_u._conditional._alternate_body

#define	DT_NODE_FREE	0	/* unused node (waiting to be freed) */
#define	DT_NODE_INT	1	/* integer value */
#define	DT_NODE_STRING	2	/* string value */
#define	DT_NODE_IDENT	3	/* identifier */
#define	DT_NODE_VAR	4	/* variable reference */
#define	DT_NODE_SYM	5	/* symbol reference */
#define	DT_NODE_TYPE	6	/* type reference or formal parameter */
#define	DT_NODE_FUNC	7	/* function call */
#define	DT_NODE_OP1	8	/* unary operator */
#define	DT_NODE_OP2	9	/* binary operator */
#define	DT_NODE_OP3	10	/* ternary operator */
#define	DT_NODE_DEXPR	11	/* D expression action */
#define	DT_NODE_DFUNC	12	/* D function action */
#define	DT_NODE_AGG	13	/* aggregation */
#define	DT_NODE_PDESC	14	/* probe description */
#define	DT_NODE_CLAUSE	15	/* clause definition */
#define	DT_NODE_INLINE	16	/* inline definition */
#define	DT_NODE_MEMBER	17	/* member definition */
#define	DT_NODE_XLATOR	18	/* translator definition */
#define	DT_NODE_PROBE	19	/* probe definition */
#define	DT_NODE_PROVIDER 20	/* provider definition */
#define	DT_NODE_PROG	21	/* program translation unit */
#define	DT_NODE_IF	22	/* if statement */

#define	DT_NF_SIGNED	0x01	/* data is a signed quantity (else unsigned) */
#define	DT_NF_COOKED	0x02	/* data is a known type (else still cooking) */
#define	DT_NF_REF	0x04	/* pass by reference (array, struct, union) */
#define	DT_NF_LVALUE	0x08	/* node is an l-value according to ANSI-C */
#define	DT_NF_WRITABLE	0x10	/* node is writable (can be modified) */
#define	DT_NF_BITFIELD	0x20	/* node is an integer bitfield */
#define	DT_NF_USERLAND	0x40	/* data is a userland address */

#define	DT_TYPE_NAMELEN	128	/* reasonable size for ctf_type_name() */

extern int dt_node_is_integer(const dt_node_t *);
extern int dt_node_is_float(const dt_node_t *);
extern int dt_node_is_scalar(const dt_node_t *);
extern int dt_node_is_arith(const dt_node_t *);
extern int dt_node_is_vfptr(const dt_node_t *);
extern int dt_node_is_dynamic(const dt_node_t *);
extern int dt_node_is_stack(const dt_node_t *);
extern int dt_node_is_symaddr(const dt_node_t *);
extern int dt_node_is_usymaddr(const dt_node_t *);
extern int dt_node_is_string(const dt_node_t *);
extern int dt_node_is_strcompat(const dt_node_t *);
extern int dt_node_is_pointer(const dt_node_t *);
extern int dt_node_is_void(const dt_node_t *);
extern int dt_node_is_ptrcompat(const dt_node_t *, const dt_node_t *,
	ctf_file_t **, ctf_id_t *);
extern int dt_node_is_argcompat(const dt_node_t *, const dt_node_t *);
extern int dt_node_is_posconst(const dt_node_t *);
extern int dt_node_is_actfunc(const dt_node_t *);

extern dt_node_t *dt_node_int(uintmax_t);
extern dt_node_t *dt_node_string(char *);
extern dt_node_t *dt_node_ident(char *);
extern dt_node_t *dt_node_type(dt_decl_t *);
extern dt_node_t *dt_node_vatype(void);
extern dt_node_t *dt_node_decl(void);
extern dt_node_t *dt_node_func(dt_node_t *, dt_node_t *);
extern dt_node_t *dt_node_offsetof(dt_decl_t *, char *);
extern dt_node_t *dt_node_op1(int, dt_node_t *);
extern dt_node_t *dt_node_op2(int, dt_node_t *, dt_node_t *);
extern dt_node_t *dt_node_op3(dt_node_t *, dt_node_t *, dt_node_t *);
extern dt_node_t *dt_node_statement(dt_node_t *);
extern dt_node_t *dt_node_pdesc_by_name(char *);
extern dt_node_t *dt_node_pdesc_by_id(uintmax_t);
extern dt_node_t *dt_node_clause(dt_node_t *, dt_node_t *, dt_node_t *);
extern dt_node_t *dt_node_inline(dt_node_t *);
extern dt_node_t *dt_node_member(dt_decl_t *, char *, dt_node_t *);
extern dt_node_t *dt_node_xlator(dt_decl_t *, dt_decl_t *, char *, dt_node_t *);
extern dt_node_t *dt_node_probe(char *, int, dt_node_t *, dt_node_t *);
extern dt_node_t *dt_node_provider(char *, dt_node_t *);
extern dt_node_t *dt_node_program(dt_node_t *);
extern dt_node_t *dt_node_if(dt_node_t *, dt_node_t *, dt_node_t *);

extern dt_node_t *dt_node_link(dt_node_t *, dt_node_t *);
extern dt_node_t *dt_node_cook(dt_node_t *, uint_t);

extern dt_node_t *dt_node_xalloc(dtrace_hdl_t *, int);
extern void dt_node_free(dt_node_t *);

extern dtrace_attribute_t dt_node_list_cook(dt_node_t **, uint_t);
extern void dt_node_list_free(dt_node_t **);
extern void dt_node_link_free(dt_node_t **);

extern void dt_node_attr_assign(dt_node_t *, dtrace_attribute_t);
extern void dt_node_type_assign(dt_node_t *, ctf_file_t *, ctf_id_t, boolean_t);
extern void dt_node_type_propagate(const dt_node_t *, dt_node_t *);
extern const char *dt_node_type_name(const dt_node_t *, char *, size_t);
extern size_t dt_node_type_size(const dt_node_t *);

extern dt_ident_t *dt_node_resolve(const dt_node_t *, uint_t);
extern size_t dt_node_sizeof(const dt_node_t *);
extern void dt_node_promote(dt_node_t *, dt_node_t *, dt_node_t *);

extern void dt_node_diftype(dtrace_hdl_t *,
    const dt_node_t *, dtrace_diftype_t *);
extern void dt_node_printr(dt_node_t *, FILE *, int);
extern void dt_printd(dt_node_t *, FILE *, int);
extern const char *dt_node_name(const dt_node_t *, char *, size_t);
extern int dt_node_root(dt_node_t *);

struct dtrace_typeinfo;	/* see <dtrace.h> */
struct dt_pcb;		/* see <dt_impl.h> */

#define	IS_CHAR(e) \
	(((e).cte_format & (CTF_INT_CHAR | CTF_INT_SIGNED)) == \
	(CTF_INT_CHAR | CTF_INT_SIGNED) && (e).cte_bits == NBBY)

#define	IS_VOID(e) \
	((e).cte_offset == 0 && (e).cte_bits == 0)

extern int dt_type_lookup(const char *, struct dtrace_typeinfo *);
extern int dt_type_pointer(struct dtrace_typeinfo *);
extern const char *dt_type_name(ctf_file_t *, ctf_id_t, char *, size_t);

typedef enum {
	YYS_CLAUSE,	/* lex/yacc state for finding program clauses */
	YYS_DEFINE,	/* lex/yacc state for parsing persistent definitions */
	YYS_EXPR,	/* lex/yacc state for parsing D expressions */
	YYS_DONE,	/* lex/yacc state for indicating parse tree is done */
	YYS_CONTROL	/* lex/yacc state for parsing control lines */
} yystate_t;

extern void dnerror(const dt_node_t *, dt_errtag_t, const char *, ...);
extern void dnwarn(const dt_node_t *, dt_errtag_t, const char *, ...);

extern void xyerror(dt_errtag_t, const char *, ...);
extern void xywarn(dt_errtag_t, const char *, ...);
extern void xyvwarn(dt_errtag_t, const char *, va_list);

extern void yyerror(const char *, ...);
extern void yywarn(const char *, ...);
extern void yyvwarn(const char *, va_list);

extern void yylabel(const char *);
extern void yybegin(yystate_t);
extern void yyinit(struct dt_pcb *);

extern int yyparse(void);
extern int yyinput(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_PARSER_H */
