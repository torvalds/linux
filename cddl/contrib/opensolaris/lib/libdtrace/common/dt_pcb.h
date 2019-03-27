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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_DT_PCB_H
#define	_DT_PCB_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <dtrace.h>
#include <setjmp.h>
#include <stdio.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <dt_parser.h>
#include <dt_regset.h>
#include <dt_inttab.h>
#include <dt_strtab.h>
#include <dt_decl.h>
#include <dt_as.h>

typedef struct dt_pcb {
	dtrace_hdl_t *pcb_hdl;	/* pointer to library handle */
	struct dt_pcb *pcb_prev; /* pointer to previous pcb in stack */
	FILE *pcb_fileptr;	/* pointer to input file (or NULL) */
	char *pcb_filetag;	/* optional file name string (or NULL) */
	const char *pcb_string;	/* pointer to input string (or NULL) */
	const char *pcb_strptr;	/* pointer to input position */
	size_t pcb_strlen;	/* length of pcb_string */
	int pcb_sargc;		/* number of script arguments (if any) */
	char *const *pcb_sargv;	/* script argument strings (if any) */
	ushort_t *pcb_sflagv;	/* script argument flags (DT_IDFLG_* bits) */
	dt_scope_t pcb_dstack;	/* declaration processing stack */
	dt_node_t *pcb_list;	/* list of allocated parse tree nodes */
	dt_node_t *pcb_hold;	/* parse tree nodes on hold until end of defn */
	dt_node_t *pcb_root;	/* root of current parse tree */
	dt_idstack_t pcb_globals; /* stack of global identifier hash tables */
	dt_idhash_t *pcb_locals; /* current hash table of local identifiers */
	dt_idhash_t *pcb_idents; /* current hash table of ambiguous idents */
	dt_idhash_t *pcb_pragmas; /* current hash table of pending pragmas */
	dt_inttab_t *pcb_inttab; /* integer table for constant references */
	dt_strtab_t *pcb_strtab; /* string table for string references */
	dt_regset_t *pcb_regs;	/* register set for code generation */
	dt_irlist_t pcb_ir;	/* list of unrelocated IR instructions */
	uint_t pcb_asvidx;	/* assembler vartab index (see dt_as.c) */
	ulong_t **pcb_asxrefs;	/* assembler imported xlators (see dt_as.c) */
	uint_t pcb_asxreflen;	/* assembler xlator map length (see dt_as.c) */
	const dtrace_probedesc_t *pcb_pdesc; /* probedesc for current context */
	struct dt_probe *pcb_probe; /* probe associated with current context */
	dtrace_probeinfo_t pcb_pinfo; /* info associated with current context */
	dtrace_attribute_t pcb_amin; /* stability minimum for compilation */
	dt_node_t *pcb_dret;	/* node containing return type for assembler */
	dtrace_difo_t *pcb_difo; /* intermediate DIF object made by assembler */
	dtrace_prog_t *pcb_prog; /* intermediate program made by compiler */
	dtrace_stmtdesc_t *pcb_stmt; /* intermediate stmt made by compiler */
	dtrace_ecbdesc_t *pcb_ecbdesc; /* intermediate ecbdesc made by cmplr */
	jmp_buf pcb_jmpbuf;	/* setjmp(3C) buffer for error return */
	const char *pcb_region;	/* optional region name for yyerror() suffix */
	dtrace_probespec_t pcb_pspec; /* probe description evaluation context */
	uint_t pcb_cflags;	/* optional compilation flags (see dtrace.h) */
	uint_t pcb_idepth;	/* preprocessor #include nesting depth */
	yystate_t pcb_yystate;	/* lex/yacc parsing state (see yybegin()) */
	int pcb_context;	/* yyparse() rules context (DT_CTX_* value) */
	int pcb_token;		/* token to be returned by yylex() (if != 0) */
	int pcb_cstate;		/* state to be restored by lexer at state end */
	int pcb_braces;		/* number of open curly braces in lexer */
	int pcb_brackets;	/* number of open square brackets in lexer */
	int pcb_parens;		/* number of open parentheses in lexer */
} dt_pcb_t;

extern void dt_pcb_push(dtrace_hdl_t *, dt_pcb_t *);
extern void dt_pcb_pop(dtrace_hdl_t *, int);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_PCB_H */
