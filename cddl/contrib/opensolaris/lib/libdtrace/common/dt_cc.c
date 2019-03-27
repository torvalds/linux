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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2016 by Delphix. All rights reserved.
 * Copyright (c) 2013, Joyent Inc. All rights reserved.
 * Copyright 2015 Gary Mills
 */

/*
 * DTrace D Language Compiler
 *
 * The code in this source file implements the main engine for the D language
 * compiler.  The driver routine for the compiler is dt_compile(), below.  The
 * compiler operates on either stdio FILEs or in-memory strings as its input
 * and can produce either dtrace_prog_t structures from a D program or a single
 * dtrace_difo_t structure from a D expression.  Multiple entry points are
 * provided as wrappers around dt_compile() for the various input/output pairs.
 * The compiler itself is implemented across the following source files:
 *
 * dt_lex.l - lex scanner
 * dt_grammar.y - yacc grammar
 * dt_parser.c - parse tree creation and semantic checking
 * dt_decl.c - declaration stack processing
 * dt_xlator.c - D translator lookup and creation
 * dt_ident.c - identifier and symbol table routines
 * dt_pragma.c - #pragma processing and D pragmas
 * dt_printf.c - D printf() and printa() argument checking and processing
 * dt_cc.c - compiler driver and dtrace_prog_t construction
 * dt_cg.c - DIF code generator
 * dt_as.c - DIF assembler
 * dt_dof.c - dtrace_prog_t -> DOF conversion
 *
 * Several other source files provide collections of utility routines used by
 * these major files.  The compiler itself is implemented in multiple passes:
 *
 * (1) The input program is scanned and parsed by dt_lex.l and dt_grammar.y
 *     and parse tree nodes are constructed using the routines in dt_parser.c.
 *     This node construction pass is described further in dt_parser.c.
 *
 * (2) The parse tree is "cooked" by assigning each clause a context (see the
 *     routine dt_setcontext(), below) based on its probe description and then
 *     recursively descending the tree performing semantic checking.  The cook
 *     routines are also implemented in dt_parser.c and described there.
 *
 * (3) For actions that are DIF expression statements, the DIF code generator
 *     and assembler are invoked to create a finished DIFO for the statement.
 *
 * (4) The dtrace_prog_t data structures for the program clauses and actions
 *     are built, containing pointers to any DIFOs created in step (3).
 *
 * (5) The caller invokes a routine in dt_dof.c to convert the finished program
 *     into DOF format for use in anonymous tracing or enabling in the kernel.
 *
 * In the implementation, steps 2-4 are intertwined in that they are performed
 * in order for each clause as part of a loop that executes over the clauses.
 *
 * The D compiler currently implements nearly no optimization.  The compiler
 * implements integer constant folding as part of pass (1), and a set of very
 * simple peephole optimizations as part of pass (3).  As with any C compiler,
 * a large number of optimizations are possible on both the intermediate data
 * structures and the generated DIF code.  These possibilities should be
 * investigated in the context of whether they will have any substantive effect
 * on the overall DTrace probe effect before they are undertaken.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>

#include <assert.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ucontext.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <dt_module.h>
#include <dt_program.h>
#include <dt_provider.h>
#include <dt_printf.h>
#include <dt_pid.h>
#include <dt_grammar.h>
#include <dt_ident.h>
#include <dt_string.h>
#include <dt_impl.h>

static const dtrace_diftype_t dt_void_rtype = {
	DIF_TYPE_CTF, CTF_K_INTEGER, 0, 0, 0
};

static const dtrace_diftype_t dt_int_rtype = {
	DIF_TYPE_CTF, CTF_K_INTEGER, 0, 0, sizeof (uint64_t)
};

static void *dt_compile(dtrace_hdl_t *, int, dtrace_probespec_t, void *,
    uint_t, int, char *const[], FILE *, const char *);

/*ARGSUSED*/
static int
dt_idreset(dt_idhash_t *dhp, dt_ident_t *idp, void *ignored)
{
	idp->di_flags &= ~(DT_IDFLG_REF | DT_IDFLG_MOD |
	    DT_IDFLG_DIFR | DT_IDFLG_DIFW);
	return (0);
}

/*ARGSUSED*/
static int
dt_idpragma(dt_idhash_t *dhp, dt_ident_t *idp, void *ignored)
{
	yylineno = idp->di_lineno;
	xyerror(D_PRAGMA_UNUSED, "unused #pragma %s\n", (char *)idp->di_iarg);
	return (0);
}

static dtrace_stmtdesc_t *
dt_stmt_create(dtrace_hdl_t *dtp, dtrace_ecbdesc_t *edp,
    dtrace_attribute_t descattr, dtrace_attribute_t stmtattr)
{
	dtrace_stmtdesc_t *sdp = dtrace_stmt_create(dtp, edp);

	if (sdp == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	assert(yypcb->pcb_stmt == NULL);
	yypcb->pcb_stmt = sdp;

	sdp->dtsd_descattr = descattr;
	sdp->dtsd_stmtattr = stmtattr;

	return (sdp);
}

static dtrace_actdesc_t *
dt_stmt_action(dtrace_hdl_t *dtp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *new;

	if ((new = dtrace_stmt_action(dtp, sdp)) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	return (new);
}

/*
 * Utility function to determine if a given action description is destructive.
 * The dtdo_destructive bit is set for us by the DIF assembler (see dt_as.c).
 */
static int
dt_action_destructive(const dtrace_actdesc_t *ap)
{
	return (DTRACEACT_ISDESTRUCTIVE(ap->dtad_kind) || (ap->dtad_kind ==
	    DTRACEACT_DIFEXPR && ap->dtad_difo->dtdo_destructive));
}

static void
dt_stmt_append(dtrace_stmtdesc_t *sdp, const dt_node_t *dnp)
{
	dtrace_ecbdesc_t *edp = sdp->dtsd_ecbdesc;
	dtrace_actdesc_t *ap, *tap;
	int commit = 0;
	int speculate = 0;
	int datarec = 0;

	/*
	 * Make sure that the new statement jibes with the rest of the ECB.
	 */
	for (ap = edp->dted_action; ap != NULL; ap = ap->dtad_next) {
		if (ap->dtad_kind == DTRACEACT_COMMIT) {
			if (commit) {
				dnerror(dnp, D_COMM_COMM, "commit( ) may "
				    "not follow commit( )\n");
			}

			if (datarec) {
				dnerror(dnp, D_COMM_DREC, "commit( ) may "
				    "not follow data-recording action(s)\n");
			}

			for (tap = ap; tap != NULL; tap = tap->dtad_next) {
				if (!DTRACEACT_ISAGG(tap->dtad_kind))
					continue;

				dnerror(dnp, D_AGG_COMM, "aggregating actions "
				    "may not follow commit( )\n");
			}

			commit = 1;
			continue;
		}

		if (ap->dtad_kind == DTRACEACT_SPECULATE) {
			if (speculate) {
				dnerror(dnp, D_SPEC_SPEC, "speculate( ) may "
				    "not follow speculate( )\n");
			}

			if (commit) {
				dnerror(dnp, D_SPEC_COMM, "speculate( ) may "
				    "not follow commit( )\n");
			}

			if (datarec) {
				dnerror(dnp, D_SPEC_DREC, "speculate( ) may "
				    "not follow data-recording action(s)\n");
			}

			speculate = 1;
			continue;
		}

		if (DTRACEACT_ISAGG(ap->dtad_kind)) {
			if (speculate) {
				dnerror(dnp, D_AGG_SPEC, "aggregating actions "
				    "may not follow speculate( )\n");
			}

			datarec = 1;
			continue;
		}

		if (speculate) {
			if (dt_action_destructive(ap)) {
				dnerror(dnp, D_ACT_SPEC, "destructive actions "
				    "may not follow speculate( )\n");
			}

			if (ap->dtad_kind == DTRACEACT_EXIT) {
				dnerror(dnp, D_EXIT_SPEC, "exit( ) may not "
				    "follow speculate( )\n");
			}
		}

		/*
		 * Exclude all non data-recording actions.
		 */
		if (dt_action_destructive(ap) ||
		    ap->dtad_kind == DTRACEACT_DISCARD)
			continue;

		if (ap->dtad_kind == DTRACEACT_DIFEXPR &&
		    ap->dtad_difo->dtdo_rtype.dtdt_kind == DIF_TYPE_CTF &&
		    ap->dtad_difo->dtdo_rtype.dtdt_size == 0)
			continue;

		if (commit) {
			dnerror(dnp, D_DREC_COMM, "data-recording actions "
			    "may not follow commit( )\n");
		}

		if (!speculate)
			datarec = 1;
	}

	if (dtrace_stmt_add(yypcb->pcb_hdl, yypcb->pcb_prog, sdp) != 0)
		longjmp(yypcb->pcb_jmpbuf, dtrace_errno(yypcb->pcb_hdl));

	if (yypcb->pcb_stmt == sdp)
		yypcb->pcb_stmt = NULL;
}

/*
 * For the first element of an aggregation tuple or for printa(), we create a
 * simple DIF program that simply returns the immediate value that is the ID
 * of the aggregation itself.  This could be optimized in the future by
 * creating a new in-kernel dtad_kind that just returns an integer.
 */
static void
dt_action_difconst(dtrace_actdesc_t *ap, uint_t id, dtrace_actkind_t kind)
{
	dtrace_hdl_t *dtp = yypcb->pcb_hdl;
	dtrace_difo_t *dp = dt_zalloc(dtp, sizeof (dtrace_difo_t));

	if (dp == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	dp->dtdo_buf = dt_alloc(dtp, sizeof (dif_instr_t) * 2);
	dp->dtdo_inttab = dt_alloc(dtp, sizeof (uint64_t));

	if (dp->dtdo_buf == NULL || dp->dtdo_inttab == NULL) {
		dt_difo_free(dtp, dp);
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
	}

	dp->dtdo_buf[0] = DIF_INSTR_SETX(0, 1); /* setx	DIF_INTEGER[0], %r1 */
	dp->dtdo_buf[1] = DIF_INSTR_RET(1);	/* ret	%r1 */
	dp->dtdo_len = 2;
	dp->dtdo_inttab[0] = id;
	dp->dtdo_intlen = 1;
	dp->dtdo_rtype = dt_int_rtype;

	ap->dtad_difo = dp;
	ap->dtad_kind = kind;
}

static void
dt_action_clear(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dt_ident_t *aid;
	dtrace_actdesc_t *ap;
	dt_node_t *anp;

	char n[DT_TYPE_NAMELEN];
	int argc = 0;

	for (anp = dnp->dn_args; anp != NULL; anp = anp->dn_list)
		argc++; /* count up arguments for error messages below */

	if (argc != 1) {
		dnerror(dnp, D_CLEAR_PROTO,
		    "%s( ) prototype mismatch: %d args passed, 1 expected\n",
		    dnp->dn_ident->di_name, argc);
	}

	anp = dnp->dn_args;
	assert(anp != NULL);

	if (anp->dn_kind != DT_NODE_AGG) {
		dnerror(dnp, D_CLEAR_AGGARG,
		    "%s( ) argument #1 is incompatible with prototype:\n"
		    "\tprototype: aggregation\n\t argument: %s\n",
		    dnp->dn_ident->di_name,
		    dt_node_type_name(anp, n, sizeof (n)));
	}

	aid = anp->dn_ident;

	if (aid->di_gen == dtp->dt_gen && !(aid->di_flags & DT_IDFLG_MOD)) {
		dnerror(dnp, D_CLEAR_AGGBAD,
		    "undefined aggregation: @%s\n", aid->di_name);
	}

	ap = dt_stmt_action(dtp, sdp);
	dt_action_difconst(ap, anp->dn_ident->di_id, DTRACEACT_LIBACT);
	ap->dtad_arg = DT_ACT_CLEAR;
}

static void
dt_action_normalize(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dt_ident_t *aid;
	dtrace_actdesc_t *ap;
	dt_node_t *anp, *normal;
	int denormal = (strcmp(dnp->dn_ident->di_name, "denormalize") == 0);

	char n[DT_TYPE_NAMELEN];
	int argc = 0;

	for (anp = dnp->dn_args; anp != NULL; anp = anp->dn_list)
		argc++; /* count up arguments for error messages below */

	if ((denormal && argc != 1) || (!denormal && argc != 2)) {
		dnerror(dnp, D_NORMALIZE_PROTO,
		    "%s( ) prototype mismatch: %d args passed, %d expected\n",
		    dnp->dn_ident->di_name, argc, denormal ? 1 : 2);
	}

	anp = dnp->dn_args;
	assert(anp != NULL);

	if (anp->dn_kind != DT_NODE_AGG) {
		dnerror(dnp, D_NORMALIZE_AGGARG,
		    "%s( ) argument #1 is incompatible with prototype:\n"
		    "\tprototype: aggregation\n\t argument: %s\n",
		    dnp->dn_ident->di_name,
		    dt_node_type_name(anp, n, sizeof (n)));
	}

	if ((normal = anp->dn_list) != NULL && !dt_node_is_scalar(normal)) {
		dnerror(dnp, D_NORMALIZE_SCALAR,
		    "%s( ) argument #2 must be of scalar type\n",
		    dnp->dn_ident->di_name);
	}

	aid = anp->dn_ident;

	if (aid->di_gen == dtp->dt_gen && !(aid->di_flags & DT_IDFLG_MOD)) {
		dnerror(dnp, D_NORMALIZE_AGGBAD,
		    "undefined aggregation: @%s\n", aid->di_name);
	}

	ap = dt_stmt_action(dtp, sdp);
	dt_action_difconst(ap, anp->dn_ident->di_id, DTRACEACT_LIBACT);

	if (denormal) {
		ap->dtad_arg = DT_ACT_DENORMALIZE;
		return;
	}

	ap->dtad_arg = DT_ACT_NORMALIZE;

	assert(normal != NULL);
	ap = dt_stmt_action(dtp, sdp);
	dt_cg(yypcb, normal);

	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_LIBACT;
	ap->dtad_arg = DT_ACT_NORMALIZE;
}

static void
dt_action_trunc(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dt_ident_t *aid;
	dtrace_actdesc_t *ap;
	dt_node_t *anp, *trunc;

	char n[DT_TYPE_NAMELEN];
	int argc = 0;

	for (anp = dnp->dn_args; anp != NULL; anp = anp->dn_list)
		argc++; /* count up arguments for error messages below */

	if (argc > 2 || argc < 1) {
		dnerror(dnp, D_TRUNC_PROTO,
		    "%s( ) prototype mismatch: %d args passed, %s expected\n",
		    dnp->dn_ident->di_name, argc,
		    argc < 1 ? "at least 1" : "no more than 2");
	}

	anp = dnp->dn_args;
	assert(anp != NULL);
	trunc = anp->dn_list;

	if (anp->dn_kind != DT_NODE_AGG) {
		dnerror(dnp, D_TRUNC_AGGARG,
		    "%s( ) argument #1 is incompatible with prototype:\n"
		    "\tprototype: aggregation\n\t argument: %s\n",
		    dnp->dn_ident->di_name,
		    dt_node_type_name(anp, n, sizeof (n)));
	}

	if (argc == 2) {
		assert(trunc != NULL);
		if (!dt_node_is_scalar(trunc)) {
			dnerror(dnp, D_TRUNC_SCALAR,
			    "%s( ) argument #2 must be of scalar type\n",
			    dnp->dn_ident->di_name);
		}
	}

	aid = anp->dn_ident;

	if (aid->di_gen == dtp->dt_gen && !(aid->di_flags & DT_IDFLG_MOD)) {
		dnerror(dnp, D_TRUNC_AGGBAD,
		    "undefined aggregation: @%s\n", aid->di_name);
	}

	ap = dt_stmt_action(dtp, sdp);
	dt_action_difconst(ap, anp->dn_ident->di_id, DTRACEACT_LIBACT);
	ap->dtad_arg = DT_ACT_TRUNC;

	ap = dt_stmt_action(dtp, sdp);

	if (argc == 1) {
		dt_action_difconst(ap, 0, DTRACEACT_LIBACT);
	} else {
		assert(trunc != NULL);
		dt_cg(yypcb, trunc);
		ap->dtad_difo = dt_as(yypcb);
		ap->dtad_kind = DTRACEACT_LIBACT;
	}

	ap->dtad_arg = DT_ACT_TRUNC;
}

static void
dt_action_printa(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dt_ident_t *aid, *fid;
	dtrace_actdesc_t *ap;
	const char *format;
	dt_node_t *anp, *proto = NULL;

	char n[DT_TYPE_NAMELEN];
	int argc = 0, argr = 0;

	for (anp = dnp->dn_args; anp != NULL; anp = anp->dn_list)
		argc++; /* count up arguments for error messages below */

	switch (dnp->dn_args->dn_kind) {
	case DT_NODE_STRING:
		format = dnp->dn_args->dn_string;
		anp = dnp->dn_args->dn_list;
		argr = 2;
		break;
	case DT_NODE_AGG:
		format = NULL;
		anp = dnp->dn_args;
		argr = 1;
		break;
	default:
		format = NULL;
		anp = dnp->dn_args;
		argr = 1;
	}

	if (argc < argr) {
		dnerror(dnp, D_PRINTA_PROTO,
		    "%s( ) prototype mismatch: %d args passed, %d expected\n",
		    dnp->dn_ident->di_name, argc, argr);
	}

	assert(anp != NULL);

	while (anp != NULL) {
		if (anp->dn_kind != DT_NODE_AGG) {
			dnerror(dnp, D_PRINTA_AGGARG,
			    "%s( ) argument #%d is incompatible with "
			    "prototype:\n\tprototype: aggregation\n"
			    "\t argument: %s\n", dnp->dn_ident->di_name, argr,
			    dt_node_type_name(anp, n, sizeof (n)));
		}

		aid = anp->dn_ident;
		fid = aid->di_iarg;

		if (aid->di_gen == dtp->dt_gen &&
		    !(aid->di_flags & DT_IDFLG_MOD)) {
			dnerror(dnp, D_PRINTA_AGGBAD,
			    "undefined aggregation: @%s\n", aid->di_name);
		}

		/*
		 * If we have multiple aggregations, we must be sure that
		 * their key signatures match.
		 */
		if (proto != NULL) {
			dt_printa_validate(proto, anp);
		} else {
			proto = anp;
		}

		if (format != NULL) {
			yylineno = dnp->dn_line;

			sdp->dtsd_fmtdata =
			    dt_printf_create(yypcb->pcb_hdl, format);
			dt_printf_validate(sdp->dtsd_fmtdata,
			    DT_PRINTF_AGGREGATION, dnp->dn_ident, 1,
			    fid->di_id, ((dt_idsig_t *)aid->di_data)->dis_args);
			format = NULL;
		}

		ap = dt_stmt_action(dtp, sdp);
		dt_action_difconst(ap, anp->dn_ident->di_id, DTRACEACT_PRINTA);

		anp = anp->dn_list;
		argr++;
	}
}

static void
dt_action_printflike(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp,
    dtrace_actkind_t kind)
{
	dt_node_t *anp, *arg1;
	dtrace_actdesc_t *ap = NULL;
	char n[DT_TYPE_NAMELEN], *str;

	assert(DTRACEACT_ISPRINTFLIKE(kind));

	if (dnp->dn_args->dn_kind != DT_NODE_STRING) {
		dnerror(dnp, D_PRINTF_ARG_FMT,
		    "%s( ) argument #1 is incompatible with prototype:\n"
		    "\tprototype: string constant\n\t argument: %s\n",
		    dnp->dn_ident->di_name,
		    dt_node_type_name(dnp->dn_args, n, sizeof (n)));
	}

	arg1 = dnp->dn_args->dn_list;
	yylineno = dnp->dn_line;
	str = dnp->dn_args->dn_string;


	/*
	 * If this is an freopen(), we use an empty string to denote that
	 * stdout should be restored.  For other printf()-like actions, an
	 * empty format string is illegal:  an empty format string would
	 * result in malformed DOF, and the compiler thus flags an empty
	 * format string as a compile-time error.  To avoid propagating the
	 * freopen() special case throughout the system, we simply transpose
	 * an empty string into a sentinel string (DT_FREOPEN_RESTORE) that
	 * denotes that stdout should be restored.
	 */
	if (kind == DTRACEACT_FREOPEN) {
		if (strcmp(str, DT_FREOPEN_RESTORE) == 0) {
			/*
			 * Our sentinel is always an invalid argument to
			 * freopen(), but if it's been manually specified, we
			 * must fail now instead of when the freopen() is
			 * actually evaluated.
			 */
			dnerror(dnp, D_FREOPEN_INVALID,
			    "%s( ) argument #1 cannot be \"%s\"\n",
			    dnp->dn_ident->di_name, DT_FREOPEN_RESTORE);
		}

		if (str[0] == '\0')
			str = DT_FREOPEN_RESTORE;
	}

	sdp->dtsd_fmtdata = dt_printf_create(dtp, str);

	dt_printf_validate(sdp->dtsd_fmtdata, DT_PRINTF_EXACTLEN,
	    dnp->dn_ident, 1, DTRACEACT_AGGREGATION, arg1);

	if (arg1 == NULL) {
		dif_instr_t *dbuf;
		dtrace_difo_t *dp;

		if ((dbuf = dt_alloc(dtp, sizeof (dif_instr_t))) == NULL ||
		    (dp = dt_zalloc(dtp, sizeof (dtrace_difo_t))) == NULL) {
			dt_free(dtp, dbuf);
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
		}

		dbuf[0] = DIF_INSTR_RET(DIF_REG_R0); /* ret %r0 */

		dp->dtdo_buf = dbuf;
		dp->dtdo_len = 1;
		dp->dtdo_rtype = dt_int_rtype;

		ap = dt_stmt_action(dtp, sdp);
		ap->dtad_difo = dp;
		ap->dtad_kind = kind;
		return;
	}

	for (anp = arg1; anp != NULL; anp = anp->dn_list) {
		ap = dt_stmt_action(dtp, sdp);
		dt_cg(yypcb, anp);
		ap->dtad_difo = dt_as(yypcb);
		ap->dtad_kind = kind;
	}
}

static void
dt_action_trace(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	int ctflib;

	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);
	boolean_t istrace = (dnp->dn_ident->di_id == DT_ACT_TRACE);
	const char *act = istrace ?  "trace" : "print";

	if (dt_node_is_void(dnp->dn_args)) {
		dnerror(dnp->dn_args, istrace ? D_TRACE_VOID : D_PRINT_VOID,
		    "%s( ) may not be applied to a void expression\n", act);
	}

	if (dt_node_resolve(dnp->dn_args, DT_IDENT_XLPTR) != NULL) {
		dnerror(dnp->dn_args, istrace ? D_TRACE_DYN : D_PRINT_DYN,
		    "%s( ) may not be applied to a translated pointer\n", act);
	}

	if (dnp->dn_args->dn_kind == DT_NODE_AGG) {
		dnerror(dnp->dn_args, istrace ? D_TRACE_AGG : D_PRINT_AGG,
		    "%s( ) may not be applied to an aggregation%s\n", act,
		    istrace ? "" : " -- did you mean printa()?");
	}

	dt_cg(yypcb, dnp->dn_args);

	/*
	 * The print() action behaves identically to trace(), except that it
	 * stores the CTF type of the argument (if present) within the DOF for
	 * the DIFEXPR action.  To do this, we set the 'dtsd_strdata' to point
	 * to the fully-qualified CTF type ID for the result of the DIF
	 * action.  We use the ID instead of the name to handles complex types
	 * like arrays and function pointers that can't be resolved by
	 * ctf_type_lookup().  This is later processed by dtrace_dof_create()
	 * and turned into a reference into the string table so that we can
	 * get the type information when we process the data after the fact.  In
	 * the case where we are referring to userland CTF data, we also need to
	 * to identify which ctf container in question we care about and encode
	 * that within the name.
	 */
	if (dnp->dn_ident->di_id == DT_ACT_PRINT) {
		dt_node_t *dret;
		size_t n;
		dt_module_t *dmp;

		dret = yypcb->pcb_dret;
		dmp = dt_module_lookup_by_ctf(dtp, dret->dn_ctfp);

		n = snprintf(NULL, 0, "%s`%ld", dmp->dm_name, dret->dn_type) + 1;
		if (dmp->dm_pid != 0) {
			ctflib = dt_module_getlibid(dtp, dmp, dret->dn_ctfp);
			assert(ctflib >= 0);
			n = snprintf(NULL, 0, "%s`%d`%ld", dmp->dm_name,
			    ctflib, dret->dn_type) + 1;
		} else {
			n = snprintf(NULL, 0, "%s`%ld", dmp->dm_name,
			    dret->dn_type) + 1;
		}
		sdp->dtsd_strdata = dt_alloc(dtp, n);
		if (sdp->dtsd_strdata == NULL)
			longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);
		(void) snprintf(sdp->dtsd_strdata, n, "%s`%ld", dmp->dm_name,
		    dret->dn_type);
		if (dmp->dm_pid != 0) {
			(void) snprintf(sdp->dtsd_strdata, n, "%s`%d`%ld",
			    dmp->dm_name, ctflib, dret->dn_type);
		} else {
			(void) snprintf(sdp->dtsd_strdata, n, "%s`%ld",
			    dmp->dm_name, dret->dn_type);
		}
	}

	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_DIFEXPR;
}

static void
dt_action_tracemem(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_node_t *addr = dnp->dn_args;
	dt_node_t *max = dnp->dn_args->dn_list;
	dt_node_t *size;

	char n[DT_TYPE_NAMELEN];

	if (dt_node_is_integer(addr) == 0 && dt_node_is_pointer(addr) == 0) {
		dnerror(addr, D_TRACEMEM_ADDR,
		    "tracemem( ) argument #1 is incompatible with "
		    "prototype:\n\tprototype: pointer or integer\n"
		    "\t argument: %s\n",
		    dt_node_type_name(addr, n, sizeof (n)));
	}

	if (dt_node_is_posconst(max) == 0) {
		dnerror(max, D_TRACEMEM_SIZE, "tracemem( ) argument #2 must "
		    "be a non-zero positive integral constant expression\n");
	}

	if ((size = max->dn_list) != NULL) {
		if (size->dn_list != NULL) {
			dnerror(size, D_TRACEMEM_ARGS, "tracemem ( ) prototype "
			    "mismatch: expected at most 3 args\n");
		}

		if (!dt_node_is_scalar(size)) {
			dnerror(size, D_TRACEMEM_DYNSIZE, "tracemem ( ) "
			    "dynamic size (argument #3) must be of "
			    "scalar type\n");
		}

		dt_cg(yypcb, size);
		ap->dtad_difo = dt_as(yypcb);
		ap->dtad_difo->dtdo_rtype = dt_int_rtype;
		ap->dtad_kind = DTRACEACT_TRACEMEM_DYNSIZE;

		ap = dt_stmt_action(dtp, sdp);
	}

	dt_cg(yypcb, addr);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_TRACEMEM;

	ap->dtad_difo->dtdo_rtype.dtdt_flags |= DIF_TF_BYREF;
	ap->dtad_difo->dtdo_rtype.dtdt_size = max->dn_value;
}

static void
dt_action_stack_args(dtrace_hdl_t *dtp, dtrace_actdesc_t *ap, dt_node_t *arg0)
{
	ap->dtad_kind = DTRACEACT_STACK;

	if (dtp->dt_options[DTRACEOPT_STACKFRAMES] != DTRACEOPT_UNSET) {
		ap->dtad_arg = dtp->dt_options[DTRACEOPT_STACKFRAMES];
	} else {
		ap->dtad_arg = 0;
	}

	if (arg0 != NULL) {
		if (arg0->dn_list != NULL) {
			dnerror(arg0, D_STACK_PROTO, "stack( ) prototype "
			    "mismatch: too many arguments\n");
		}

		if (dt_node_is_posconst(arg0) == 0) {
			dnerror(arg0, D_STACK_SIZE, "stack( ) size must be a "
			    "non-zero positive integral constant expression\n");
		}

		ap->dtad_arg = arg0->dn_value;
	}
}

static void
dt_action_stack(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);
	dt_action_stack_args(dtp, ap, dnp->dn_args);
}

static void
dt_action_ustack_args(dtrace_hdl_t *dtp, dtrace_actdesc_t *ap, dt_node_t *dnp)
{
	uint32_t nframes = 0;
	uint32_t strsize = 0;	/* default string table size */
	dt_node_t *arg0 = dnp->dn_args;
	dt_node_t *arg1 = arg0 != NULL ? arg0->dn_list : NULL;

	assert(dnp->dn_ident->di_id == DT_ACT_JSTACK ||
	    dnp->dn_ident->di_id == DT_ACT_USTACK);

	if (dnp->dn_ident->di_id == DT_ACT_JSTACK) {
		if (dtp->dt_options[DTRACEOPT_JSTACKFRAMES] != DTRACEOPT_UNSET)
			nframes = dtp->dt_options[DTRACEOPT_JSTACKFRAMES];

		if (dtp->dt_options[DTRACEOPT_JSTACKSTRSIZE] != DTRACEOPT_UNSET)
			strsize = dtp->dt_options[DTRACEOPT_JSTACKSTRSIZE];

		ap->dtad_kind = DTRACEACT_JSTACK;
	} else {
		assert(dnp->dn_ident->di_id == DT_ACT_USTACK);

		if (dtp->dt_options[DTRACEOPT_USTACKFRAMES] != DTRACEOPT_UNSET)
			nframes = dtp->dt_options[DTRACEOPT_USTACKFRAMES];

		ap->dtad_kind = DTRACEACT_USTACK;
	}

	if (arg0 != NULL) {
		if (!dt_node_is_posconst(arg0)) {
			dnerror(arg0, D_USTACK_FRAMES, "ustack( ) argument #1 "
			    "must be a non-zero positive integer constant\n");
		}
		nframes = (uint32_t)arg0->dn_value;
	}

	if (arg1 != NULL) {
		if (arg1->dn_kind != DT_NODE_INT ||
		    ((arg1->dn_flags & DT_NF_SIGNED) &&
		    (int64_t)arg1->dn_value < 0)) {
			dnerror(arg1, D_USTACK_STRSIZE, "ustack( ) argument #2 "
			    "must be a positive integer constant\n");
		}

		if (arg1->dn_list != NULL) {
			dnerror(arg1, D_USTACK_PROTO, "ustack( ) prototype "
			    "mismatch: too many arguments\n");
		}

		strsize = (uint32_t)arg1->dn_value;
	}

	ap->dtad_arg = DTRACE_USTACK_ARG(nframes, strsize);
}

static void
dt_action_ustack(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);
	dt_action_ustack_args(dtp, ap, dnp);
}

static void
dt_action_setopt(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap;
	dt_node_t *arg0, *arg1;

	/*
	 * The prototype guarantees that we are called with either one or
	 * two arguments, and that any arguments that are present are strings.
	 */
	arg0 = dnp->dn_args;
	arg1 = arg0->dn_list;

	ap = dt_stmt_action(dtp, sdp);
	dt_cg(yypcb, arg0);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_LIBACT;
	ap->dtad_arg = DT_ACT_SETOPT;

	ap = dt_stmt_action(dtp, sdp);

	if (arg1 == NULL) {
		dt_action_difconst(ap, 0, DTRACEACT_LIBACT);
	} else {
		dt_cg(yypcb, arg1);
		ap->dtad_difo = dt_as(yypcb);
		ap->dtad_kind = DTRACEACT_LIBACT;
	}

	ap->dtad_arg = DT_ACT_SETOPT;
}

/*ARGSUSED*/
static void
dt_action_symmod_args(dtrace_hdl_t *dtp, dtrace_actdesc_t *ap,
    dt_node_t *dnp, dtrace_actkind_t kind)
{
	assert(kind == DTRACEACT_SYM || kind == DTRACEACT_MOD ||
	    kind == DTRACEACT_USYM || kind == DTRACEACT_UMOD ||
	    kind == DTRACEACT_UADDR);

	dt_cg(yypcb, dnp);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = kind;
	ap->dtad_difo->dtdo_rtype.dtdt_size = sizeof (uint64_t);
}

static void
dt_action_symmod(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp,
    dtrace_actkind_t kind)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);
	dt_action_symmod_args(dtp, ap, dnp->dn_args, kind);
}

/*ARGSUSED*/
static void
dt_action_ftruncate(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	/*
	 * Library actions need a DIFO that serves as an argument.  As
	 * ftruncate() doesn't take an argument, we generate the constant 0
	 * in a DIFO; this constant will be ignored when the ftruncate() is
	 * processed.
	 */
	dt_action_difconst(ap, 0, DTRACEACT_LIBACT);
	ap->dtad_arg = DT_ACT_FTRUNCATE;
}

/*ARGSUSED*/
static void
dt_action_stop(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	ap->dtad_kind = DTRACEACT_STOP;
	ap->dtad_arg = 0;
}

/*ARGSUSED*/
static void
dt_action_breakpoint(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	ap->dtad_kind = DTRACEACT_BREAKPOINT;
	ap->dtad_arg = 0;
}

/*ARGSUSED*/
static void
dt_action_panic(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	ap->dtad_kind = DTRACEACT_PANIC;
	ap->dtad_arg = 0;
}

static void
dt_action_chill(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_args);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_CHILL;
}

static void
dt_action_raise(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_args);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_RAISE;
}

static void
dt_action_exit(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_args);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_EXIT;
	ap->dtad_difo->dtdo_rtype.dtdt_size = sizeof (int);
}

static void
dt_action_speculate(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_args);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_SPECULATE;
}

static void
dt_action_printm(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_node_t *size = dnp->dn_args;
	dt_node_t *addr = dnp->dn_args->dn_list;

	char n[DT_TYPE_NAMELEN];

	if (dt_node_is_posconst(size) == 0) {
		dnerror(size, D_PRINTM_SIZE, "printm( ) argument #1 must "
		    "be a non-zero positive integral constant expression\n");
	}

	if (dt_node_is_pointer(addr) == 0) {
		dnerror(addr, D_PRINTM_ADDR,
		    "printm( ) argument #2 is incompatible with "
		    "prototype:\n\tprototype: pointer\n"
		    "\t argument: %s\n",
		    dt_node_type_name(addr, n, sizeof (n)));
	}

	dt_cg(yypcb, addr);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_PRINTM;

	ap->dtad_difo->dtdo_rtype.dtdt_flags |= DIF_TF_BYREF;
	ap->dtad_difo->dtdo_rtype.dtdt_size = size->dn_value + sizeof(uintptr_t);
}

static void
dt_action_commit(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_args);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_COMMIT;
}

static void
dt_action_discard(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_args);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_kind = DTRACEACT_DISCARD;
}

static void
dt_compile_fun(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	switch (dnp->dn_expr->dn_ident->di_id) {
	case DT_ACT_BREAKPOINT:
		dt_action_breakpoint(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_CHILL:
		dt_action_chill(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_CLEAR:
		dt_action_clear(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_COMMIT:
		dt_action_commit(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_DENORMALIZE:
		dt_action_normalize(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_DISCARD:
		dt_action_discard(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_EXIT:
		dt_action_exit(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_FREOPEN:
		dt_action_printflike(dtp, dnp->dn_expr, sdp, DTRACEACT_FREOPEN);
		break;
	case DT_ACT_FTRUNCATE:
		dt_action_ftruncate(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_MOD:
		dt_action_symmod(dtp, dnp->dn_expr, sdp, DTRACEACT_MOD);
		break;
	case DT_ACT_NORMALIZE:
		dt_action_normalize(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_PANIC:
		dt_action_panic(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_PRINT:
		dt_action_trace(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_PRINTA:
		dt_action_printa(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_PRINTF:
		dt_action_printflike(dtp, dnp->dn_expr, sdp, DTRACEACT_PRINTF);
		break;
	case DT_ACT_PRINTM:
		dt_action_printm(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_RAISE:
		dt_action_raise(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_SETOPT:
		dt_action_setopt(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_SPECULATE:
		dt_action_speculate(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_STACK:
		dt_action_stack(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_STOP:
		dt_action_stop(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_SYM:
		dt_action_symmod(dtp, dnp->dn_expr, sdp, DTRACEACT_SYM);
		break;
	case DT_ACT_SYSTEM:
		dt_action_printflike(dtp, dnp->dn_expr, sdp, DTRACEACT_SYSTEM);
		break;
	case DT_ACT_TRACE:
		dt_action_trace(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_TRACEMEM:
		dt_action_tracemem(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_TRUNC:
		dt_action_trunc(dtp, dnp->dn_expr, sdp);
		break;
	case DT_ACT_UADDR:
		dt_action_symmod(dtp, dnp->dn_expr, sdp, DTRACEACT_UADDR);
		break;
	case DT_ACT_UMOD:
		dt_action_symmod(dtp, dnp->dn_expr, sdp, DTRACEACT_UMOD);
		break;
	case DT_ACT_USYM:
		dt_action_symmod(dtp, dnp->dn_expr, sdp, DTRACEACT_USYM);
		break;
	case DT_ACT_USTACK:
	case DT_ACT_JSTACK:
		dt_action_ustack(dtp, dnp->dn_expr, sdp);
		break;
	default:
		dnerror(dnp->dn_expr, D_UNKNOWN, "tracing function %s( ) is "
		    "not yet supported\n", dnp->dn_expr->dn_ident->di_name);
	}
}

static void
dt_compile_exp(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dtrace_actdesc_t *ap = dt_stmt_action(dtp, sdp);

	dt_cg(yypcb, dnp->dn_expr);
	ap->dtad_difo = dt_as(yypcb);
	ap->dtad_difo->dtdo_rtype = dt_void_rtype;
	ap->dtad_kind = DTRACEACT_DIFEXPR;
}

static void
dt_compile_agg(dtrace_hdl_t *dtp, dt_node_t *dnp, dtrace_stmtdesc_t *sdp)
{
	dt_ident_t *aid, *fid;
	dt_node_t *anp, *incr = NULL;
	dtrace_actdesc_t *ap;
	uint_t n = 1, argmax;
	uint64_t arg = 0;

	/*
	 * If the aggregation has no aggregating function applied to it, then
	 * this statement has no effect.  Flag this as a programming error.
	 */
	if (dnp->dn_aggfun == NULL) {
		dnerror(dnp, D_AGG_NULL, "expression has null effect: @%s\n",
		    dnp->dn_ident->di_name);
	}

	aid = dnp->dn_ident;
	fid = dnp->dn_aggfun->dn_ident;

	if (dnp->dn_aggfun->dn_args != NULL &&
	    dt_node_is_scalar(dnp->dn_aggfun->dn_args) == 0) {
		dnerror(dnp->dn_aggfun, D_AGG_SCALAR, "%s( ) argument #1 must "
		    "be of scalar type\n", fid->di_name);
	}

	/*
	 * The ID of the aggregation itself is implicitly recorded as the first
	 * member of each aggregation tuple so we can distinguish them later.
	 */
	ap = dt_stmt_action(dtp, sdp);
	dt_action_difconst(ap, aid->di_id, DTRACEACT_DIFEXPR);

	for (anp = dnp->dn_aggtup; anp != NULL; anp = anp->dn_list) {
		ap = dt_stmt_action(dtp, sdp);
		n++;

		if (anp->dn_kind == DT_NODE_FUNC) {
			if (anp->dn_ident->di_id == DT_ACT_STACK) {
				dt_action_stack_args(dtp, ap, anp->dn_args);
				continue;
			}

			if (anp->dn_ident->di_id == DT_ACT_USTACK ||
			    anp->dn_ident->di_id == DT_ACT_JSTACK) {
				dt_action_ustack_args(dtp, ap, anp);
				continue;
			}

			switch (anp->dn_ident->di_id) {
			case DT_ACT_UADDR:
				dt_action_symmod_args(dtp, ap,
				    anp->dn_args, DTRACEACT_UADDR);
				continue;

			case DT_ACT_USYM:
				dt_action_symmod_args(dtp, ap,
				    anp->dn_args, DTRACEACT_USYM);
				continue;

			case DT_ACT_UMOD:
				dt_action_symmod_args(dtp, ap,
				    anp->dn_args, DTRACEACT_UMOD);
				continue;

			case DT_ACT_SYM:
				dt_action_symmod_args(dtp, ap,
				    anp->dn_args, DTRACEACT_SYM);
				continue;

			case DT_ACT_MOD:
				dt_action_symmod_args(dtp, ap,
				    anp->dn_args, DTRACEACT_MOD);
				continue;

			default:
				break;
			}
		}

		dt_cg(yypcb, anp);
		ap->dtad_difo = dt_as(yypcb);
		ap->dtad_kind = DTRACEACT_DIFEXPR;
	}

	if (fid->di_id == DTRACEAGG_LQUANTIZE) {
		/*
		 * For linear quantization, we have between two and four
		 * arguments in addition to the expression:
		 *
		 *    arg1 => Base value
		 *    arg2 => Limit value
		 *    arg3 => Quantization level step size (defaults to 1)
		 *    arg4 => Quantization increment value (defaults to 1)
		 */
		dt_node_t *arg1 = dnp->dn_aggfun->dn_args->dn_list;
		dt_node_t *arg2 = arg1->dn_list;
		dt_node_t *arg3 = arg2->dn_list;
		dt_idsig_t *isp;
		uint64_t nlevels, step = 1, oarg;
		int64_t baseval, limitval;

		if (arg1->dn_kind != DT_NODE_INT) {
			dnerror(arg1, D_LQUANT_BASETYPE, "lquantize( ) "
			    "argument #1 must be an integer constant\n");
		}

		baseval = (int64_t)arg1->dn_value;

		if (baseval < INT32_MIN || baseval > INT32_MAX) {
			dnerror(arg1, D_LQUANT_BASEVAL, "lquantize( ) "
			    "argument #1 must be a 32-bit quantity\n");
		}

		if (arg2->dn_kind != DT_NODE_INT) {
			dnerror(arg2, D_LQUANT_LIMTYPE, "lquantize( ) "
			    "argument #2 must be an integer constant\n");
		}

		limitval = (int64_t)arg2->dn_value;

		if (limitval < INT32_MIN || limitval > INT32_MAX) {
			dnerror(arg2, D_LQUANT_LIMVAL, "lquantize( ) "
			    "argument #2 must be a 32-bit quantity\n");
		}

		if (limitval < baseval) {
			dnerror(dnp, D_LQUANT_MISMATCH,
			    "lquantize( ) base (argument #1) must be less "
			    "than limit (argument #2)\n");
		}

		if (arg3 != NULL) {
			if (!dt_node_is_posconst(arg3)) {
				dnerror(arg3, D_LQUANT_STEPTYPE, "lquantize( ) "
				    "argument #3 must be a non-zero positive "
				    "integer constant\n");
			}

			if ((step = arg3->dn_value) > UINT16_MAX) {
				dnerror(arg3, D_LQUANT_STEPVAL, "lquantize( ) "
				    "argument #3 must be a 16-bit quantity\n");
			}
		}

		nlevels = (limitval - baseval) / step;

		if (nlevels == 0) {
			dnerror(dnp, D_LQUANT_STEPLARGE,
			    "lquantize( ) step (argument #3) too large: must "
			    "have at least one quantization level\n");
		}

		if (nlevels > UINT16_MAX) {
			dnerror(dnp, D_LQUANT_STEPSMALL, "lquantize( ) step "
			    "(argument #3) too small: number of quantization "
			    "levels must be a 16-bit quantity\n");
		}

		arg = (step << DTRACE_LQUANTIZE_STEPSHIFT) |
		    (nlevels << DTRACE_LQUANTIZE_LEVELSHIFT) |
		    ((baseval << DTRACE_LQUANTIZE_BASESHIFT) &
		    DTRACE_LQUANTIZE_BASEMASK);

		assert(arg != 0);

		isp = (dt_idsig_t *)aid->di_data;

		if (isp->dis_auxinfo == 0) {
			/*
			 * This is the first time we've seen an lquantize()
			 * for this aggregation; we'll store our argument
			 * as the auxiliary signature information.
			 */
			isp->dis_auxinfo = arg;
		} else if ((oarg = isp->dis_auxinfo) != arg) {
			/*
			 * If we have seen this lquantize() before and the
			 * argument doesn't match the original argument, pick
			 * the original argument apart to concisely report the
			 * mismatch.
			 */
			int obaseval = DTRACE_LQUANTIZE_BASE(oarg);
			int onlevels = DTRACE_LQUANTIZE_LEVELS(oarg);
			int ostep = DTRACE_LQUANTIZE_STEP(oarg);

			if (obaseval != baseval) {
				dnerror(dnp, D_LQUANT_MATCHBASE, "lquantize( ) "
				    "base (argument #1) doesn't match previous "
				    "declaration: expected %d, found %d\n",
				    obaseval, (int)baseval);
			}

			if (onlevels * ostep != nlevels * step) {
				dnerror(dnp, D_LQUANT_MATCHLIM, "lquantize( ) "
				    "limit (argument #2) doesn't match previous"
				    " declaration: expected %d, found %d\n",
				    obaseval + onlevels * ostep,
				    (int)baseval + (int)nlevels * (int)step);
			}

			if (ostep != step) {
				dnerror(dnp, D_LQUANT_MATCHSTEP, "lquantize( ) "
				    "step (argument #3) doesn't match previous "
				    "declaration: expected %d, found %d\n",
				    ostep, (int)step);
			}

			/*
			 * We shouldn't be able to get here -- one of the
			 * parameters must be mismatched if the arguments
			 * didn't match.
			 */
			assert(0);
		}

		incr = arg3 != NULL ? arg3->dn_list : NULL;
		argmax = 5;
	}

	if (fid->di_id == DTRACEAGG_LLQUANTIZE) {
		/*
		 * For log/linear quantizations, we have between one and five
		 * arguments in addition to the expression:
		 *
		 *    arg1 => Factor
		 *    arg2 => Low magnitude
		 *    arg3 => High magnitude
		 *    arg4 => Number of steps per magnitude
		 *    arg5 => Quantization increment value (defaults to 1)
		 */
		dt_node_t *llarg = dnp->dn_aggfun->dn_args->dn_list;
		uint64_t oarg, order, v;
		dt_idsig_t *isp;
		int i;

		struct {
			char *str;		/* string identifier */
			int badtype;		/* error on bad type */
			int badval;		/* error on bad value */
			int mismatch;		/* error on bad match */
			int shift;		/* shift value */
			uint16_t value;		/* value itself */
		} args[] = {
			{ "factor", D_LLQUANT_FACTORTYPE,
			    D_LLQUANT_FACTORVAL, D_LLQUANT_FACTORMATCH,
			    DTRACE_LLQUANTIZE_FACTORSHIFT },
			{ "low magnitude", D_LLQUANT_LOWTYPE,
			    D_LLQUANT_LOWVAL, D_LLQUANT_LOWMATCH,
			    DTRACE_LLQUANTIZE_LOWSHIFT },
			{ "high magnitude", D_LLQUANT_HIGHTYPE,
			    D_LLQUANT_HIGHVAL, D_LLQUANT_HIGHMATCH,
			    DTRACE_LLQUANTIZE_HIGHSHIFT },
			{ "linear steps per magnitude", D_LLQUANT_NSTEPTYPE,
			    D_LLQUANT_NSTEPVAL, D_LLQUANT_NSTEPMATCH,
			    DTRACE_LLQUANTIZE_NSTEPSHIFT },
			{ NULL }
		};

		assert(arg == 0);

		for (i = 0; args[i].str != NULL; i++) {
			if (llarg->dn_kind != DT_NODE_INT) {
				dnerror(llarg, args[i].badtype, "llquantize( ) "
				    "argument #%d (%s) must be an "
				    "integer constant\n", i + 1, args[i].str);
			}

			if ((uint64_t)llarg->dn_value > UINT16_MAX) {
				dnerror(llarg, args[i].badval, "llquantize( ) "
				    "argument #%d (%s) must be an unsigned "
				    "16-bit quantity\n", i + 1, args[i].str);
			}

			args[i].value = (uint16_t)llarg->dn_value;

			assert(!(arg & ((uint64_t)UINT16_MAX <<
			    args[i].shift)));
			arg |= ((uint64_t)args[i].value << args[i].shift);
			llarg = llarg->dn_list;
		}

		assert(arg != 0);

		if (args[0].value < 2) {
			dnerror(dnp, D_LLQUANT_FACTORSMALL, "llquantize( ) "
			    "factor (argument #1) must be two or more\n");
		}

		if (args[1].value >= args[2].value) {
			dnerror(dnp, D_LLQUANT_MAGRANGE, "llquantize( ) "
			    "high magnitude (argument #3) must be greater "
			    "than low magnitude (argument #2)\n");
		}

		if (args[3].value < args[0].value) {
			dnerror(dnp, D_LLQUANT_FACTORNSTEPS, "llquantize( ) "
			    "factor (argument #1) must be less than or "
			    "equal to the number of linear steps per "
			    "magnitude (argument #4)\n");
		}

		for (v = args[0].value; v < args[3].value; v *= args[0].value)
			continue;

		if ((args[3].value % args[0].value) || (v % args[3].value)) {
			dnerror(dnp, D_LLQUANT_FACTOREVEN, "llquantize( ) "
			    "factor (argument #1) must evenly divide the "
			    "number of steps per magnitude (argument #4), "
			    "and the number of steps per magnitude must evenly "
			    "divide a power of the factor\n");
		}

		for (i = 0, order = 1; i <= args[2].value + 1; i++) {
			if (order * args[0].value > order) {
				order *= args[0].value;
				continue;
			}

			dnerror(dnp, D_LLQUANT_MAGTOOBIG, "llquantize( ) "
			    "factor (%d) raised to power of high magnitude "
			    "(%d) plus 1 overflows 64-bits\n", args[0].value,
			    args[2].value);
		}

		isp = (dt_idsig_t *)aid->di_data;

		if (isp->dis_auxinfo == 0) {
			/*
			 * This is the first time we've seen an llquantize()
			 * for this aggregation; we'll store our argument
			 * as the auxiliary signature information.
			 */
			isp->dis_auxinfo = arg;
		} else if ((oarg = isp->dis_auxinfo) != arg) {
			/*
			 * If we have seen this llquantize() before and the
			 * argument doesn't match the original argument, pick
			 * the original argument apart to concisely report the
			 * mismatch.
			 */
			int expected = 0, found = 0;

			for (i = 0; expected == found; i++) {
				assert(args[i].str != NULL);

				expected = (oarg >> args[i].shift) & UINT16_MAX;
				found = (arg >> args[i].shift) & UINT16_MAX;
			}

			dnerror(dnp, args[i - 1].mismatch, "llquantize( ) "
			    "%s (argument #%d) doesn't match previous "
			    "declaration: expected %d, found %d\n",
			    args[i - 1].str, i, expected, found);
		}

		incr = llarg;
		argmax = 6;
	}

	if (fid->di_id == DTRACEAGG_QUANTIZE) {
		incr = dnp->dn_aggfun->dn_args->dn_list;
		argmax = 2;
	}

	if (incr != NULL) {
		if (!dt_node_is_scalar(incr)) {
			dnerror(dnp, D_PROTO_ARG, "%s( ) increment value "
			    "(argument #%d) must be of scalar type\n",
			    fid->di_name, argmax);
		}

		if ((anp = incr->dn_list) != NULL) {
			int argc = argmax;

			for (; anp != NULL; anp = anp->dn_list)
				argc++;

			dnerror(incr, D_PROTO_LEN, "%s( ) prototype "
			    "mismatch: %d args passed, at most %d expected",
			    fid->di_name, argc, argmax);
		}

		ap = dt_stmt_action(dtp, sdp);
		n++;

		dt_cg(yypcb, incr);
		ap->dtad_difo = dt_as(yypcb);
		ap->dtad_difo->dtdo_rtype = dt_void_rtype;
		ap->dtad_kind = DTRACEACT_DIFEXPR;
	}

	assert(sdp->dtsd_aggdata == NULL);
	sdp->dtsd_aggdata = aid;

	ap = dt_stmt_action(dtp, sdp);
	assert(fid->di_kind == DT_IDENT_AGGFUNC);
	assert(DTRACEACT_ISAGG(fid->di_id));
	ap->dtad_kind = fid->di_id;
	ap->dtad_ntuple = n;
	ap->dtad_arg = arg;

	if (dnp->dn_aggfun->dn_args != NULL) {
		dt_cg(yypcb, dnp->dn_aggfun->dn_args);
		ap->dtad_difo = dt_as(yypcb);
	}
}

static void
dt_compile_one_clause(dtrace_hdl_t *dtp, dt_node_t *cnp, dt_node_t *pnp)
{
	dtrace_ecbdesc_t *edp;
	dtrace_stmtdesc_t *sdp;
	dt_node_t *dnp;

	yylineno = pnp->dn_line;
	dt_setcontext(dtp, pnp->dn_desc);
	(void) dt_node_cook(cnp, DT_IDFLG_REF);

	if (DT_TREEDUMP_PASS(dtp, 2))
		dt_node_printr(cnp, stderr, 0);

	if ((edp = dt_ecbdesc_create(dtp, pnp->dn_desc)) == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	assert(yypcb->pcb_ecbdesc == NULL);
	yypcb->pcb_ecbdesc = edp;

	if (cnp->dn_pred != NULL) {
		dt_cg(yypcb, cnp->dn_pred);
		edp->dted_pred.dtpdd_difo = dt_as(yypcb);
	}

	if (cnp->dn_acts == NULL) {
		dt_stmt_append(dt_stmt_create(dtp, edp,
		    cnp->dn_ctxattr, _dtrace_defattr), cnp);
	}

	for (dnp = cnp->dn_acts; dnp != NULL; dnp = dnp->dn_list) {
		assert(yypcb->pcb_stmt == NULL);
		sdp = dt_stmt_create(dtp, edp, cnp->dn_ctxattr, cnp->dn_attr);

		switch (dnp->dn_kind) {
		case DT_NODE_DEXPR:
			if (dnp->dn_expr->dn_kind == DT_NODE_AGG)
				dt_compile_agg(dtp, dnp->dn_expr, sdp);
			else
				dt_compile_exp(dtp, dnp, sdp);
			break;
		case DT_NODE_DFUNC:
			dt_compile_fun(dtp, dnp, sdp);
			break;
		case DT_NODE_AGG:
			dt_compile_agg(dtp, dnp, sdp);
			break;
		default:
			dnerror(dnp, D_UNKNOWN, "internal error -- node kind "
			    "%u is not a valid statement\n", dnp->dn_kind);
		}

		assert(yypcb->pcb_stmt == sdp);
		dt_stmt_append(sdp, dnp);
	}

	assert(yypcb->pcb_ecbdesc == edp);
	dt_ecbdesc_release(dtp, edp);
	dt_endcontext(dtp);
	yypcb->pcb_ecbdesc = NULL;
}

static void
dt_compile_clause(dtrace_hdl_t *dtp, dt_node_t *cnp)
{
	dt_node_t *pnp;

	for (pnp = cnp->dn_pdescs; pnp != NULL; pnp = pnp->dn_list)
		dt_compile_one_clause(dtp, cnp, pnp);
}

static void
dt_compile_xlator(dt_node_t *dnp)
{
	dt_xlator_t *dxp = dnp->dn_xlator;
	dt_node_t *mnp;

	for (mnp = dnp->dn_members; mnp != NULL; mnp = mnp->dn_list) {
		assert(dxp->dx_membdif[mnp->dn_membid] == NULL);
		dt_cg(yypcb, mnp);
		dxp->dx_membdif[mnp->dn_membid] = dt_as(yypcb);
	}
}

void
dt_setcontext(dtrace_hdl_t *dtp, dtrace_probedesc_t *pdp)
{
	const dtrace_pattr_t *pap;
	dt_probe_t *prp;
	dt_provider_t *pvp;
	dt_ident_t *idp;
	char attrstr[8];
	int err;

	/*
	 * Both kernel and pid based providers are allowed to have names
	 * ending with what could be interpreted as a number. We assume it's
	 * a pid and that we may need to dynamically create probes for
	 * that process if:
	 *
	 * (1) The provider doesn't exist, or,
	 * (2) The provider exists and has DTRACE_PRIV_PROC privilege.
	 *
	 * On an error, dt_pid_create_probes() will set the error message
	 * and tag -- we just have to longjmp() out of here.
	 */
	if (isdigit(pdp->dtpd_provider[strlen(pdp->dtpd_provider) - 1]) &&
	    ((pvp = dt_provider_lookup(dtp, pdp->dtpd_provider)) == NULL ||
	    pvp->pv_desc.dtvd_priv.dtpp_flags & DTRACE_PRIV_PROC) &&
	    dt_pid_create_probes(pdp, dtp, yypcb) != 0) {
		longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);
	}

	/*
	 * Call dt_probe_info() to get the probe arguments and attributes.  If
	 * a representative probe is found, set 'pap' to the probe provider's
	 * attributes.  Otherwise set 'pap' to default Unstable attributes.
	 */
	if ((prp = dt_probe_info(dtp, pdp, &yypcb->pcb_pinfo)) == NULL) {
		pap = &_dtrace_prvdesc;
		err = dtrace_errno(dtp);
		bzero(&yypcb->pcb_pinfo, sizeof (dtrace_probeinfo_t));
		yypcb->pcb_pinfo.dtp_attr = pap->dtpa_provider;
		yypcb->pcb_pinfo.dtp_arga = pap->dtpa_args;
	} else {
		pap = &prp->pr_pvp->pv_desc.dtvd_attr;
		err = 0;
	}

	if (err == EDT_NOPROBE && !(yypcb->pcb_cflags & DTRACE_C_ZDEFS)) {
		xyerror(D_PDESC_ZERO, "probe description %s:%s:%s:%s does not "
		    "match any probes\n", pdp->dtpd_provider, pdp->dtpd_mod,
		    pdp->dtpd_func, pdp->dtpd_name);
	}

	if (err != EDT_NOPROBE && err != EDT_UNSTABLE && err != 0)
		xyerror(D_PDESC_INVAL, "%s\n", dtrace_errmsg(dtp, err));

	dt_dprintf("set context to %s:%s:%s:%s [%u] prp=%p attr=%s argc=%d\n",
	    pdp->dtpd_provider, pdp->dtpd_mod, pdp->dtpd_func, pdp->dtpd_name,
	    pdp->dtpd_id, (void *)prp, dt_attr_str(yypcb->pcb_pinfo.dtp_attr,
	    attrstr, sizeof (attrstr)), yypcb->pcb_pinfo.dtp_argc);

	/*
	 * Reset the stability attributes of D global variables that vary
	 * based on the attributes of the provider and context itself.
	 */
	if ((idp = dt_idhash_lookup(dtp->dt_globals, "probeprov")) != NULL)
		idp->di_attr = pap->dtpa_provider;
	if ((idp = dt_idhash_lookup(dtp->dt_globals, "probemod")) != NULL)
		idp->di_attr = pap->dtpa_mod;
	if ((idp = dt_idhash_lookup(dtp->dt_globals, "probefunc")) != NULL)
		idp->di_attr = pap->dtpa_func;
	if ((idp = dt_idhash_lookup(dtp->dt_globals, "probename")) != NULL)
		idp->di_attr = pap->dtpa_name;
	if ((idp = dt_idhash_lookup(dtp->dt_globals, "args")) != NULL)
		idp->di_attr = pap->dtpa_args;

	yypcb->pcb_pdesc = pdp;
	yypcb->pcb_probe = prp;
}

/*
 * Reset context-dependent variables and state at the end of cooking a D probe
 * definition clause.  This ensures that external declarations between clauses
 * do not reference any stale context-dependent data from the previous clause.
 */
void
dt_endcontext(dtrace_hdl_t *dtp)
{
	static const char *const cvars[] = {
		"probeprov", "probemod", "probefunc", "probename", "args", NULL
	};

	dt_ident_t *idp;
	int i;

	for (i = 0; cvars[i] != NULL; i++) {
		if ((idp = dt_idhash_lookup(dtp->dt_globals, cvars[i])) != NULL)
			idp->di_attr = _dtrace_defattr;
	}

	yypcb->pcb_pdesc = NULL;
	yypcb->pcb_probe = NULL;
}

static int
dt_reduceid(dt_idhash_t *dhp, dt_ident_t *idp, dtrace_hdl_t *dtp)
{
	if (idp->di_vers != 0 && idp->di_vers > dtp->dt_vmax)
		dt_idhash_delete(dhp, idp);

	return (0);
}

/*
 * When dtrace_setopt() is called for "version", it calls dt_reduce() to remove
 * any identifiers or translators that have been previously defined as bound to
 * a version greater than the specified version.  Therefore, in our current
 * version implementation, establishing a binding is a one-way transformation.
 * In addition, no versioning is currently provided for types as our .d library
 * files do not define any types and we reserve prefixes DTRACE_ and dtrace_
 * for our exclusive use.  If required, type versioning will require more work.
 */
int
dt_reduce(dtrace_hdl_t *dtp, dt_version_t v)
{
	char s[DT_VERSION_STRMAX];
	dt_xlator_t *dxp, *nxp;

	if (v > dtp->dt_vmax)
		return (dt_set_errno(dtp, EDT_VERSREDUCED));
	else if (v == dtp->dt_vmax)
		return (0); /* no reduction necessary */

	dt_dprintf("reducing api version to %s\n",
	    dt_version_num2str(v, s, sizeof (s)));

	dtp->dt_vmax = v;

	for (dxp = dt_list_next(&dtp->dt_xlators); dxp != NULL; dxp = nxp) {
		nxp = dt_list_next(dxp);
		if ((dxp->dx_souid.di_vers != 0 && dxp->dx_souid.di_vers > v) ||
		    (dxp->dx_ptrid.di_vers != 0 && dxp->dx_ptrid.di_vers > v))
			dt_list_delete(&dtp->dt_xlators, dxp);
	}

	(void) dt_idhash_iter(dtp->dt_macros, (dt_idhash_f *)dt_reduceid, dtp);
	(void) dt_idhash_iter(dtp->dt_aggs, (dt_idhash_f *)dt_reduceid, dtp);
	(void) dt_idhash_iter(dtp->dt_globals, (dt_idhash_f *)dt_reduceid, dtp);
	(void) dt_idhash_iter(dtp->dt_tls, (dt_idhash_f *)dt_reduceid, dtp);

	return (0);
}

/*
 * Fork and exec the cpp(1) preprocessor to run over the specified input file,
 * and return a FILE handle for the cpp output.  We use the /dev/fd filesystem
 * here to simplify the code by leveraging file descriptor inheritance.
 */
static FILE *
dt_preproc(dtrace_hdl_t *dtp, FILE *ifp)
{
	int argc = dtp->dt_cpp_argc;
	char **argv = malloc(sizeof (char *) * (argc + 5));
	FILE *ofp = tmpfile();

#ifdef illumos
	char ipath[20], opath[20]; /* big enough for /dev/fd/ + INT_MAX + \0 */
#endif
	char verdef[32]; /* big enough for -D__SUNW_D_VERSION=0x%08x + \0 */

	struct sigaction act, oact;
	sigset_t mask, omask;

	int wstat, estat;
	pid_t pid;
#ifdef illumos
	off64_t off;
#else
	off_t off = 0;
#endif
	int c;

	if (argv == NULL || ofp == NULL) {
		(void) dt_set_errno(dtp, errno);
		goto err;
	}

	/*
	 * If the input is a seekable file, see if it is an interpreter file.
	 * If we see #!, seek past the first line because cpp will choke on it.
	 * We start cpp just prior to the \n at the end of this line so that
	 * it still sees the newline, ensuring that #line values are correct.
	 */
	if (isatty(fileno(ifp)) == 0 && (off = ftello64(ifp)) != -1) {
		if ((c = fgetc(ifp)) == '#' && (c = fgetc(ifp)) == '!') {
			for (off += 2; c != '\n'; off++) {
				if ((c = fgetc(ifp)) == EOF)
					break;
			}
			if (c == '\n')
				off--; /* start cpp just prior to \n */
		}
		(void) fflush(ifp);
		(void) fseeko64(ifp, off, SEEK_SET);
	}

#ifdef illumos
	(void) snprintf(ipath, sizeof (ipath), "/dev/fd/%d", fileno(ifp));
	(void) snprintf(opath, sizeof (opath), "/dev/fd/%d", fileno(ofp));
#endif

	bcopy(dtp->dt_cpp_argv, argv, sizeof (char *) * argc);

	(void) snprintf(verdef, sizeof (verdef),
	    "-D__SUNW_D_VERSION=0x%08x", dtp->dt_vmax);
	argv[argc++] = verdef;

#ifdef illumos
	switch (dtp->dt_stdcmode) {
	case DT_STDC_XA:
	case DT_STDC_XT:
		argv[argc++] = "-D__STDC__=0";
		break;
	case DT_STDC_XC:
		argv[argc++] = "-D__STDC__=1";
		break;
	}

	argv[argc++] = ipath;
	argv[argc++] = opath;
#else
	argv[argc++] = "-P";
#endif
	argv[argc] = NULL;

	/*
	 * libdtrace must be able to be embedded in other programs that may
	 * include application-specific signal handlers.  Therefore, if we
	 * need to fork to run cpp(1), we must avoid generating a SIGCHLD
	 * that could confuse the containing application.  To do this,
	 * we block SIGCHLD and reset its disposition to SIG_DFL.
	 * We restore our signal state once we are done.
	 */
	(void) sigemptyset(&mask);
	(void) sigaddset(&mask, SIGCHLD);
	(void) sigprocmask(SIG_BLOCK, &mask, &omask);

	bzero(&act, sizeof (act));
	act.sa_handler = SIG_DFL;
	(void) sigaction(SIGCHLD, &act, &oact);

	if ((pid = fork1()) == -1) {
		(void) sigaction(SIGCHLD, &oact, NULL);
		(void) sigprocmask(SIG_SETMASK, &omask, NULL);
		(void) dt_set_errno(dtp, EDT_CPPFORK);
		goto err;
	}

	if (pid == 0) {
#ifndef illumos
		if (isatty(fileno(ifp)) == 0)
			lseek(fileno(ifp), off, SEEK_SET);
		dup2(fileno(ifp), 0);
		dup2(fileno(ofp), 1);
#endif
		(void) execvp(dtp->dt_cpp_path, argv);
		_exit(errno == ENOENT ? 127 : 126);
	}

	do {
		dt_dprintf("waiting for %s (PID %d)\n", dtp->dt_cpp_path,
		    (int)pid);
	} while (waitpid(pid, &wstat, 0) == -1 && errno == EINTR);

	(void) sigaction(SIGCHLD, &oact, NULL);
	(void) sigprocmask(SIG_SETMASK, &omask, NULL);

	dt_dprintf("%s returned exit status 0x%x\n", dtp->dt_cpp_path, wstat);
	estat = WIFEXITED(wstat) ? WEXITSTATUS(wstat) : -1;

	if (estat != 0) {
		switch (estat) {
		case 126:
			(void) dt_set_errno(dtp, EDT_CPPEXEC);
			break;
		case 127:
			(void) dt_set_errno(dtp, EDT_CPPENT);
			break;
		default:
			(void) dt_set_errno(dtp, EDT_CPPERR);
		}
		goto err;
	}

	free(argv);
	(void) fflush(ofp);
	(void) fseek(ofp, 0, SEEK_SET);
	return (ofp);

err:
	free(argv);
	(void) fclose(ofp);
	return (NULL);
}

static void
dt_lib_depend_error(dtrace_hdl_t *dtp, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	dt_set_errmsg(dtp, NULL, NULL, NULL, 0, format, ap);
	va_end(ap);
}

int
dt_lib_depend_add(dtrace_hdl_t *dtp, dt_list_t *dlp, const char *arg)
{
	dt_lib_depend_t *dld;
	const char *end;

	assert(arg != NULL);

	if ((end = strrchr(arg, '/')) == NULL)
		return (dt_set_errno(dtp, EINVAL));

	if ((dld = dt_zalloc(dtp, sizeof (dt_lib_depend_t))) == NULL)
		return (-1);

	if ((dld->dtld_libpath = dt_alloc(dtp, MAXPATHLEN)) == NULL) {
		dt_free(dtp, dld);
		return (-1);
	}

	(void) strlcpy(dld->dtld_libpath, arg, end - arg + 2);
	if ((dld->dtld_library = strdup(arg)) == NULL) {
		dt_free(dtp, dld->dtld_libpath);
		dt_free(dtp, dld);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	dt_list_append(dlp, dld);
	return (0);
}

dt_lib_depend_t *
dt_lib_depend_lookup(dt_list_t *dld, const char *arg)
{
	dt_lib_depend_t *dldn;

	for (dldn = dt_list_next(dld); dldn != NULL;
	    dldn = dt_list_next(dldn)) {
		if (strcmp(dldn->dtld_library, arg) == 0)
			return (dldn);
	}

	return (NULL);
}

/*
 * Go through all the library files, and, if any library dependencies exist for
 * that file, add it to that node's list of dependents. The result of this
 * will be a graph which can then be topologically sorted to produce a
 * compilation order.
 */
static int
dt_lib_build_graph(dtrace_hdl_t *dtp)
{
	dt_lib_depend_t *dld, *dpld;

	for (dld = dt_list_next(&dtp->dt_lib_dep); dld != NULL;
	    dld = dt_list_next(dld)) {
		char *library = dld->dtld_library;

		for (dpld = dt_list_next(&dld->dtld_dependencies); dpld != NULL;
		    dpld = dt_list_next(dpld)) {
			dt_lib_depend_t *dlda;

			if ((dlda = dt_lib_depend_lookup(&dtp->dt_lib_dep,
			    dpld->dtld_library)) == NULL) {
				dt_lib_depend_error(dtp,
				    "Invalid library dependency in %s: %s\n",
				    dld->dtld_library, dpld->dtld_library);

				return (dt_set_errno(dtp, EDT_COMPILER));
			}

			if ((dt_lib_depend_add(dtp, &dlda->dtld_dependents,
			    library)) != 0) {
				return (-1); /* preserve dt_errno */
			}
		}
	}
	return (0);
}

static int
dt_topo_sort(dtrace_hdl_t *dtp, dt_lib_depend_t *dld, int *count)
{
	dt_lib_depend_t *dpld, *dlda, *new;

	dld->dtld_start = ++(*count);

	for (dpld = dt_list_next(&dld->dtld_dependents); dpld != NULL;
	    dpld = dt_list_next(dpld)) {
		dlda = dt_lib_depend_lookup(&dtp->dt_lib_dep,
		    dpld->dtld_library);
		assert(dlda != NULL);

		if (dlda->dtld_start == 0 &&
		    dt_topo_sort(dtp, dlda, count) == -1)
			return (-1);
	}

	if ((new = dt_zalloc(dtp, sizeof (dt_lib_depend_t))) == NULL)
		return (-1);

	if ((new->dtld_library = strdup(dld->dtld_library)) == NULL) {
		dt_free(dtp, new);
		return (dt_set_errno(dtp, EDT_NOMEM));
	}

	new->dtld_start = dld->dtld_start;
	new->dtld_finish = dld->dtld_finish = ++(*count);
	dt_list_prepend(&dtp->dt_lib_dep_sorted, new);

	dt_dprintf("library %s sorted (%d/%d)\n", new->dtld_library,
	    new->dtld_start, new->dtld_finish);

	return (0);
}

static int
dt_lib_depend_sort(dtrace_hdl_t *dtp)
{
	dt_lib_depend_t *dld, *dpld, *dlda;
	int count = 0;

	if (dt_lib_build_graph(dtp) == -1)
		return (-1); /* preserve dt_errno */

	/*
	 * Perform a topological sort of the graph that hangs off
	 * dtp->dt_lib_dep. The result of this process will be a
	 * dependency ordered list located at dtp->dt_lib_dep_sorted.
	 */
	for (dld = dt_list_next(&dtp->dt_lib_dep); dld != NULL;
	    dld = dt_list_next(dld)) {
		if (dld->dtld_start == 0 &&
		    dt_topo_sort(dtp, dld, &count) == -1)
			return (-1); /* preserve dt_errno */;
	}

	/*
	 * Check the graph for cycles. If an ancestor's finishing time is
	 * less than any of its dependent's finishing times then a back edge
	 * exists in the graph and this is a cycle.
	 */
	for (dld = dt_list_next(&dtp->dt_lib_dep); dld != NULL;
	    dld = dt_list_next(dld)) {
		for (dpld = dt_list_next(&dld->dtld_dependents); dpld != NULL;
		    dpld = dt_list_next(dpld)) {
			dlda = dt_lib_depend_lookup(&dtp->dt_lib_dep_sorted,
			    dpld->dtld_library);
			assert(dlda != NULL);

			if (dlda->dtld_finish > dld->dtld_finish) {
				dt_lib_depend_error(dtp,
				    "Cyclic dependency detected: %s => %s\n",
				    dld->dtld_library, dpld->dtld_library);

				return (dt_set_errno(dtp, EDT_COMPILER));
			}
		}
	}

	return (0);
}

static void
dt_lib_depend_free(dtrace_hdl_t *dtp)
{
	dt_lib_depend_t *dld, *dlda;

	while ((dld = dt_list_next(&dtp->dt_lib_dep)) != NULL) {
		while ((dlda = dt_list_next(&dld->dtld_dependencies)) != NULL) {
			dt_list_delete(&dld->dtld_dependencies, dlda);
			dt_free(dtp, dlda->dtld_library);
			dt_free(dtp, dlda->dtld_libpath);
			dt_free(dtp, dlda);
		}
		while ((dlda = dt_list_next(&dld->dtld_dependents)) != NULL) {
			dt_list_delete(&dld->dtld_dependents, dlda);
			dt_free(dtp, dlda->dtld_library);
			dt_free(dtp, dlda->dtld_libpath);
			dt_free(dtp, dlda);
		}
		dt_list_delete(&dtp->dt_lib_dep, dld);
		dt_free(dtp, dld->dtld_library);
		dt_free(dtp, dld->dtld_libpath);
		dt_free(dtp, dld);
	}

	while ((dld = dt_list_next(&dtp->dt_lib_dep_sorted)) != NULL) {
		dt_list_delete(&dtp->dt_lib_dep_sorted, dld);
		dt_free(dtp, dld->dtld_library);
		dt_free(dtp, dld);
	}
}

/*
 * Open all the .d library files found in the specified directory and
 * compile each one of them.  We silently ignore any missing directories and
 * other files found therein.  We only fail (and thereby fail dt_load_libs()) if
 * we fail to compile a library and the error is something other than #pragma D
 * depends_on.  Dependency errors are silently ignored to permit a library
 * directory to contain libraries which may not be accessible depending on our
 * privileges.
 */
static int
dt_load_libs_dir(dtrace_hdl_t *dtp, const char *path)
{
	struct dirent *dp;
	const char *p, *end;
	DIR *dirp;

	char fname[PATH_MAX];
	FILE *fp;
	void *rv;
	dt_lib_depend_t *dld;

	if ((dirp = opendir(path)) == NULL) {
		dt_dprintf("skipping lib dir %s: %s\n", path, strerror(errno));
		return (0);
	}

	/* First, parse each file for library dependencies. */
	while ((dp = readdir(dirp)) != NULL) {
		if ((p = strrchr(dp->d_name, '.')) == NULL || strcmp(p, ".d"))
			continue; /* skip any filename not ending in .d */

		(void) snprintf(fname, sizeof (fname),
		    "%s/%s", path, dp->d_name);

		if ((fp = fopen(fname, "r")) == NULL) {
			dt_dprintf("skipping library %s: %s\n",
			    fname, strerror(errno));
			continue;
		}

		/*
		 * Skip files whose name match an already processed library
		 */
		for (dld = dt_list_next(&dtp->dt_lib_dep); dld != NULL;
		    dld = dt_list_next(dld)) {
			end = strrchr(dld->dtld_library, '/');
			/* dt_lib_depend_add ensures this */
			assert(end != NULL);
			if (strcmp(end + 1, dp->d_name) == 0)
				break;
		}

		if (dld != NULL) {
			dt_dprintf("skipping library %s, already processed "
			    "library with the same name: %s", dp->d_name,
			    dld->dtld_library);
			(void) fclose(fp);
			continue;
		}

		dtp->dt_filetag = fname;
		if (dt_lib_depend_add(dtp, &dtp->dt_lib_dep, fname) != 0) {
			(void) fclose(fp);
			return (-1); /* preserve dt_errno */
		}

		rv = dt_compile(dtp, DT_CTX_DPROG,
		    DTRACE_PROBESPEC_NAME, NULL,
		    DTRACE_C_EMPTY | DTRACE_C_CTL, 0, NULL, fp, NULL);

		if (rv != NULL && dtp->dt_errno &&
		    (dtp->dt_errno != EDT_COMPILER ||
		    dtp->dt_errtag != dt_errtag(D_PRAGMA_DEPEND))) {
			(void) fclose(fp);
			return (-1); /* preserve dt_errno */
		}

		if (dtp->dt_errno)
			dt_dprintf("error parsing library %s: %s\n",
			    fname, dtrace_errmsg(dtp, dtrace_errno(dtp)));

		(void) fclose(fp);
		dtp->dt_filetag = NULL;
	}

	(void) closedir(dirp);

	return (0);
}

/*
 * Perform a topological sorting of all the libraries found across the entire
 * dt_lib_path.  Once sorted, compile each one in topological order to cache its
 * inlines and translators, etc.  We silently ignore any missing directories and
 * other files found therein. We only fail (and thereby fail dt_load_libs()) if
 * we fail to compile a library and the error is something other than #pragma D
 * depends_on.  Dependency errors are silently ignored to permit a library
 * directory to contain libraries which may not be accessible depending on our
 * privileges.
 */
static int
dt_load_libs_sort(dtrace_hdl_t *dtp)
{
	dtrace_prog_t *pgp;
	FILE *fp;
	dt_lib_depend_t *dld;

	/*
	 * Finish building the graph containing the library dependencies
	 * and perform a topological sort to generate an ordered list
	 * for compilation.
	 */
	if (dt_lib_depend_sort(dtp) == -1)
		goto err;

	for (dld = dt_list_next(&dtp->dt_lib_dep_sorted); dld != NULL;
	    dld = dt_list_next(dld)) {

		if ((fp = fopen(dld->dtld_library, "r")) == NULL) {
			dt_dprintf("skipping library %s: %s\n",
			    dld->dtld_library, strerror(errno));
			continue;
		}

		dtp->dt_filetag = dld->dtld_library;
		pgp = dtrace_program_fcompile(dtp, fp, DTRACE_C_EMPTY, 0, NULL);
		(void) fclose(fp);
		dtp->dt_filetag = NULL;

		if (pgp == NULL && (dtp->dt_errno != EDT_COMPILER ||
		    dtp->dt_errtag != dt_errtag(D_PRAGMA_DEPEND)))
			goto err;

		if (pgp == NULL) {
			dt_dprintf("skipping library %s: %s\n",
			    dld->dtld_library,
			    dtrace_errmsg(dtp, dtrace_errno(dtp)));
		} else {
			dld->dtld_loaded = B_TRUE;
			dt_program_destroy(dtp, pgp);
		}
	}

	dt_lib_depend_free(dtp);
	return (0);

err:
	dt_lib_depend_free(dtp);
	return (-1); /* preserve dt_errno */
}

/*
 * Load the contents of any appropriate DTrace .d library files.  These files
 * contain inlines and translators that will be cached by the compiler.  We
 * defer this activity until the first compile to permit libdtrace clients to
 * add their own library directories and so that we can properly report errors.
 */
static int
dt_load_libs(dtrace_hdl_t *dtp)
{
	dt_dirpath_t *dirp;

	if (dtp->dt_cflags & DTRACE_C_NOLIBS)
		return (0); /* libraries already processed */

	dtp->dt_cflags |= DTRACE_C_NOLIBS;

	/*
	 * /usr/lib/dtrace is always at the head of the list. The rest of the
	 * list is specified in the precedence order the user requested. Process
	 * everything other than the head first. DTRACE_C_NOLIBS has already
	 * been spcified so dt_vopen will ensure that there is always one entry
	 * in dt_lib_path.
	 */
	for (dirp = dt_list_next(dt_list_next(&dtp->dt_lib_path));
	    dirp != NULL; dirp = dt_list_next(dirp)) {
		if (dt_load_libs_dir(dtp, dirp->dir_path) != 0) {
			dtp->dt_cflags &= ~DTRACE_C_NOLIBS;
			return (-1); /* errno is set for us */
		}
	}

	/* Handle /usr/lib/dtrace */
	dirp = dt_list_next(&dtp->dt_lib_path);
	if (dt_load_libs_dir(dtp, dirp->dir_path) != 0) {
		dtp->dt_cflags &= ~DTRACE_C_NOLIBS;
		return (-1); /* errno is set for us */
	}

	if (dt_load_libs_sort(dtp) < 0)
		return (-1); /* errno is set for us */

	return (0);
}

static void *
dt_compile(dtrace_hdl_t *dtp, int context, dtrace_probespec_t pspec, void *arg,
    uint_t cflags, int argc, char *const argv[], FILE *fp, const char *s)
{
	dt_node_t *dnp;
	dt_decl_t *ddp;
	dt_pcb_t pcb;
	void *volatile rv;
	int err;

	if ((fp == NULL && s == NULL) || (cflags & ~DTRACE_C_MASK) != 0) {
		(void) dt_set_errno(dtp, EINVAL);
		return (NULL);
	}

	if (dt_list_next(&dtp->dt_lib_path) != NULL && dt_load_libs(dtp) != 0)
		return (NULL); /* errno is set for us */

	if (dtp->dt_globals->dh_nelems != 0)
		(void) dt_idhash_iter(dtp->dt_globals, dt_idreset, NULL);

	if (dtp->dt_tls->dh_nelems != 0)
		(void) dt_idhash_iter(dtp->dt_tls, dt_idreset, NULL);

	if (fp && (cflags & DTRACE_C_CPP) && (fp = dt_preproc(dtp, fp)) == NULL)
		return (NULL); /* errno is set for us */

	dt_pcb_push(dtp, &pcb);

	pcb.pcb_fileptr = fp;
	pcb.pcb_string = s;
	pcb.pcb_strptr = s;
	pcb.pcb_strlen = s ? strlen(s) : 0;
	pcb.pcb_sargc = argc;
	pcb.pcb_sargv = argv;
	pcb.pcb_sflagv = argc ? calloc(argc, sizeof (ushort_t)) : NULL;
	pcb.pcb_pspec = pspec;
	pcb.pcb_cflags = dtp->dt_cflags | cflags;
	pcb.pcb_amin = dtp->dt_amin;
	pcb.pcb_yystate = -1;
	pcb.pcb_context = context;
	pcb.pcb_token = context;

	if (context != DT_CTX_DPROG)
		yybegin(YYS_EXPR);
	else if (cflags & DTRACE_C_CTL)
		yybegin(YYS_CONTROL);
	else
		yybegin(YYS_CLAUSE);

	if ((err = setjmp(yypcb->pcb_jmpbuf)) != 0)
		goto out;

	if (yypcb->pcb_sargc != 0 && yypcb->pcb_sflagv == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	yypcb->pcb_idents = dt_idhash_create("ambiguous", NULL, 0, 0);
	yypcb->pcb_locals = dt_idhash_create("clause local", NULL,
	    DIF_VAR_OTHER_UBASE, DIF_VAR_OTHER_MAX);

	if (yypcb->pcb_idents == NULL || yypcb->pcb_locals == NULL)
		longjmp(yypcb->pcb_jmpbuf, EDT_NOMEM);

	/*
	 * Invoke the parser to evaluate the D source code.  If any errors
	 * occur during parsing, an error function will be called and we
	 * will longjmp back to pcb_jmpbuf to abort.  If parsing succeeds,
	 * we optionally display the parse tree if debugging is enabled.
	 */
	if (yyparse() != 0 || yypcb->pcb_root == NULL)
		xyerror(D_EMPTY, "empty D program translation unit\n");

	yybegin(YYS_DONE);

	if (cflags & DTRACE_C_CTL)
		goto out;

	if (context != DT_CTX_DTYPE && DT_TREEDUMP_PASS(dtp, 1))
		dt_node_printr(yypcb->pcb_root, stderr, 0);

	if (yypcb->pcb_pragmas != NULL)
		(void) dt_idhash_iter(yypcb->pcb_pragmas, dt_idpragma, NULL);

	if (argc > 1 && !(yypcb->pcb_cflags & DTRACE_C_ARGREF) &&
	    !(yypcb->pcb_sflagv[argc - 1] & DT_IDFLG_REF)) {
		xyerror(D_MACRO_UNUSED, "extraneous argument '%s' ($%d is "
		    "not referenced)\n", yypcb->pcb_sargv[argc - 1], argc - 1);
	}

	/*
	 * Perform sugar transformations (for "if" / "else") and replace the
	 * existing clause chain with the new one.
	 */
	if (context == DT_CTX_DPROG) {
		dt_node_t *dnp, *next_dnp;
		dt_node_t *new_list = NULL;

		for (dnp = yypcb->pcb_root->dn_list;
		    dnp != NULL; dnp = next_dnp) {
			/* remove this node from the list */
			next_dnp = dnp->dn_list;
			dnp->dn_list = NULL;

			if (dnp->dn_kind == DT_NODE_CLAUSE)
				dnp = dt_compile_sugar(dtp, dnp);
			/* append node to the new list */
			new_list = dt_node_link(new_list, dnp);
		}
		yypcb->pcb_root->dn_list = new_list;
	}

	/*
	 * If we have successfully created a parse tree for a D program, loop
	 * over the clauses and actions and instantiate the corresponding
	 * libdtrace program.  If we are parsing a D expression, then we
	 * simply run the code generator and assembler on the resulting tree.
	 */
	switch (context) {
	case DT_CTX_DPROG:
		assert(yypcb->pcb_root->dn_kind == DT_NODE_PROG);

		if ((dnp = yypcb->pcb_root->dn_list) == NULL &&
		    !(yypcb->pcb_cflags & DTRACE_C_EMPTY))
			xyerror(D_EMPTY, "empty D program translation unit\n");

		if ((yypcb->pcb_prog = dt_program_create(dtp)) == NULL)
			longjmp(yypcb->pcb_jmpbuf, dtrace_errno(dtp));

		for (; dnp != NULL; dnp = dnp->dn_list) {
			switch (dnp->dn_kind) {
			case DT_NODE_CLAUSE:
				if (DT_TREEDUMP_PASS(dtp, 4))
					dt_printd(dnp, stderr, 0);
				dt_compile_clause(dtp, dnp);
				break;
			case DT_NODE_XLATOR:
				if (dtp->dt_xlatemode == DT_XL_DYNAMIC)
					dt_compile_xlator(dnp);
				break;
			case DT_NODE_PROVIDER:
				(void) dt_node_cook(dnp, DT_IDFLG_REF);
				break;
			}
		}

		yypcb->pcb_prog->dp_xrefs = yypcb->pcb_asxrefs;
		yypcb->pcb_prog->dp_xrefslen = yypcb->pcb_asxreflen;
		yypcb->pcb_asxrefs = NULL;
		yypcb->pcb_asxreflen = 0;

		rv = yypcb->pcb_prog;
		break;

	case DT_CTX_DEXPR:
		(void) dt_node_cook(yypcb->pcb_root, DT_IDFLG_REF);
		dt_cg(yypcb, yypcb->pcb_root);
		rv = dt_as(yypcb);
		break;

	case DT_CTX_DTYPE:
		ddp = (dt_decl_t *)yypcb->pcb_root; /* root is really a decl */
		err = dt_decl_type(ddp, arg);
		dt_decl_free(ddp);

		if (err != 0)
			longjmp(yypcb->pcb_jmpbuf, EDT_COMPILER);

		rv = NULL;
		break;
	}

out:
	if (context != DT_CTX_DTYPE && yypcb->pcb_root != NULL &&
	    DT_TREEDUMP_PASS(dtp, 3))
		dt_node_printr(yypcb->pcb_root, stderr, 0);

	if (dtp->dt_cdefs_fd != -1 && (ftruncate64(dtp->dt_cdefs_fd, 0) == -1 ||
	    lseek64(dtp->dt_cdefs_fd, 0, SEEK_SET) == -1 ||
	    ctf_write(dtp->dt_cdefs->dm_ctfp, dtp->dt_cdefs_fd) == CTF_ERR))
		dt_dprintf("failed to update CTF cache: %s\n", strerror(errno));

	if (dtp->dt_ddefs_fd != -1 && (ftruncate64(dtp->dt_ddefs_fd, 0) == -1 ||
	    lseek64(dtp->dt_ddefs_fd, 0, SEEK_SET) == -1 ||
	    ctf_write(dtp->dt_ddefs->dm_ctfp, dtp->dt_ddefs_fd) == CTF_ERR))
		dt_dprintf("failed to update CTF cache: %s\n", strerror(errno));

	if (yypcb->pcb_fileptr && (cflags & DTRACE_C_CPP))
		(void) fclose(yypcb->pcb_fileptr); /* close dt_preproc() file */

	dt_pcb_pop(dtp, err);
	(void) dt_set_errno(dtp, err);
	return (err ? NULL : rv);
}

dtrace_prog_t *
dtrace_program_strcompile(dtrace_hdl_t *dtp, const char *s,
    dtrace_probespec_t spec, uint_t cflags, int argc, char *const argv[])
{
	return (dt_compile(dtp, DT_CTX_DPROG,
	    spec, NULL, cflags, argc, argv, NULL, s));
}

dtrace_prog_t *
dtrace_program_fcompile(dtrace_hdl_t *dtp, FILE *fp,
    uint_t cflags, int argc, char *const argv[])
{
	return (dt_compile(dtp, DT_CTX_DPROG,
	    DTRACE_PROBESPEC_NAME, NULL, cflags, argc, argv, fp, NULL));
}

int
dtrace_type_strcompile(dtrace_hdl_t *dtp, const char *s, dtrace_typeinfo_t *dtt)
{
	(void) dt_compile(dtp, DT_CTX_DTYPE,
	    DTRACE_PROBESPEC_NONE, dtt, 0, 0, NULL, NULL, s);
	return (dtp->dt_errno ? -1 : 0);
}

int
dtrace_type_fcompile(dtrace_hdl_t *dtp, FILE *fp, dtrace_typeinfo_t *dtt)
{
	(void) dt_compile(dtp, DT_CTX_DTYPE,
	    DTRACE_PROBESPEC_NONE, dtt, 0, 0, NULL, fp, NULL);
	return (dtp->dt_errno ? -1 : 0);
}
