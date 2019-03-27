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

 /*
  * Copyright (c) 2011, Joyent, Inc. All rights reserved.
  * Copyright (c) 2012 by Delphix. All rights reserved.
  */

#ifndef	_DT_ERRTAGS_H
#define	_DT_ERRTAGS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This enum definition is used to define a set of error tags associated with
 * the D compiler's various error conditions.  The shell script mkerrtags.sh is
 * used to parse this file and create a corresponding dt_errtags.c source file.
 * If you do something other than add a new error tag here, you may need to
 * update the mkerrtags shell script as it is based upon simple regexps.
 */
typedef enum {
	D_UNKNOWN,			/* unknown D compiler error */
	D_SYNTAX,			/* syntax error in input stream */
	D_EMPTY,			/* empty translation unit */
	D_TYPE_ERR,			/* type definition missing */
	D_TYPE_MEMBER,			/* type member not found */
	D_ASRELO,			/* relocation remains against symbol */
	D_CG_EXPR,			/* tracing function called from expr */
	D_CG_DYN,			/* expression returns dynamic result */
	D_ATTR_MIN,			/* attributes less than amin setting */
	D_ID_OFLOW,			/* identifier space overflow */
	D_PDESC_ZERO,			/* probedesc matches zero probes */
	D_PDESC_INVAL,			/* probedesc is not valid */
	D_PRED_SCALAR,			/* predicate must be of scalar type */
	D_FUNC_IDENT,			/* function designator is not ident */
	D_FUNC_UNDEF,			/* function ident is not defined */
	D_FUNC_IDKIND,			/* function ident is of wrong idkind */
	D_OFFSETOF_TYPE,		/* offsetof arg is not sou type */
	D_OFFSETOF_BITFIELD,		/* offsetof applied to field member */
	D_SIZEOF_TYPE,			/* invalid sizeof type */
	D_SIZEOF_BITFIELD,		/* sizeof applied to field member */
	D_STRINGOF_TYPE,		/* invalid stringof type */
	D_OP_IDENT,			/* operand must be an identifier */
	D_OP_INT,			/* operand must be integral type */
	D_OP_SCALAR,			/* operand must be scalar type */
	D_OP_ARITH,			/* operand must be arithmetic type */
	D_OP_WRITE,			/* operand must be writable variable */
	D_OP_LVAL,			/* operand must be lvalue */
	D_OP_INCOMPAT,			/* operand types are not compatible */
	D_OP_VFPTR,			/* operand cannot be void or func ptr */
	D_OP_ARRFUN,			/* operand cannot be array or func */
	D_OP_PTR,			/* operand must be a pointer */
	D_OP_SOU,			/* operand must be struct or union */
	D_OP_INCOMPLETE,		/* operand is an incomplete type */
	D_OP_DYN,			/* operand cannot be of dynamic type */
	D_OP_ACT,			/* operand cannot be action */
	D_AGG_REDEF,			/* aggregation cannot be redefined */
	D_AGG_FUNC,			/* aggregating function required */
	D_AGG_MDIM,			/* aggregation used as multi-dim arr */
	D_ARR_BADREF,			/* access non-array using tuple */
	D_ARR_LOCAL,			/* cannot define local assc array */
	D_DIV_ZERO,			/* division by zero detected */
	D_DEREF_NONPTR,			/* dereference non-pointer type */
	D_DEREF_VOID,			/* dereference void pointer */
	D_DEREF_FUNC,			/* dereference function pointer */
	D_ADDROF_LVAL,			/* unary & applied to non-lvalue */
	D_ADDROF_VAR,			/* unary & applied to variable */
	D_ADDROF_BITFIELD,		/* unary & applied to field member */
	D_XLATE_REDECL,			/* translator redeclared */
	D_XLATE_NOCONV,			/* no conversion for member defined */
	D_XLATE_NONE,			/* no translator for type combo */
	D_XLATE_SOU,			/* dst must be struct or union type */
	D_XLATE_INCOMPAT,		/* translator member type incompat */
	D_XLATE_MEMB,			/* translator member is not valid */
	D_CAST_INVAL,			/* invalid cast expression */
	D_PRAGERR,			/* #pragma error message */
	D_PRAGCTL_INVAL,		/* invalid control directive */
	D_PRAGMA_INVAL,			/* invalid compiler pragma */
	D_PRAGMA_UNUSED,		/* unused compiler pragma */
	D_PRAGMA_MALFORM,		/* malformed #pragma argument list */
	D_PRAGMA_OPTSET,		/* failed to set #pragma option */
	D_PRAGMA_SCOPE,			/* #pragma identifier scope error */
	D_PRAGMA_DEPEND,		/* #pragma dependency not satisfied */
	D_MACRO_UNDEF,			/* macro parameter is not defined */
	D_MACRO_OFLOW,			/* macro parameter integer overflow */
	D_MACRO_UNUSED,			/* macro parameter is never used */
	D_INT_OFLOW,			/* integer constant overflow */
	D_INT_DIGIT,			/* integer digit is not valid */
	D_STR_NL,			/* newline in string literal */
	D_CHR_NL,			/* newline in character constant */
	D_CHR_NULL,			/* empty character constant */
	D_CHR_OFLOW,			/* character constant is too long */
	D_IDENT_BADREF,			/* identifier expected type mismatch */
	D_IDENT_UNDEF,			/* identifier is not known/defined */
	D_IDENT_AMBIG,			/* identifier is ambiguous (var/enum) */
	D_SYM_BADREF,			/* kernel/user symbol ref mismatch */
	D_SYM_NOTYPES,			/* no CTF data available for sym ref */
	D_SYM_MODEL,			/* module/program data model mismatch */
	D_VAR_UNDEF,			/* reference to undefined variable */
	D_VAR_UNSUP,			/* unsupported variable specification */
	D_PROTO_LEN,			/* prototype length mismatch */
	D_PROTO_ARG,			/* prototype argument mismatch */
	D_ARGS_MULTI,			/* description matches unstable set */
	D_ARGS_XLATOR,			/* no args[] translator defined */
	D_ARGS_NONE,			/* no args[] available */
	D_ARGS_TYPE,			/* invalid args[] type */
	D_ARGS_IDX,			/* invalid args[] index */
	D_REGS_IDX,			/* invalid regs[] index */
	D_KEY_TYPE,			/* invalid agg or array key type */
	D_PRINTF_DYN_PROTO,		/* dynamic size argument missing */
	D_PRINTF_DYN_TYPE,		/* dynamic size type mismatch */
	D_PRINTF_AGG_CONV,		/* improper use of %@ conversion */
	D_PRINTF_ARG_PROTO,		/* conversion missing value argument */
	D_PRINTF_ARG_TYPE,		/* conversion arg has wrong type */
	D_PRINTF_ARG_EXTRA,		/* extra arguments specified */
	D_PRINTF_ARG_FMT,		/* format string is not a constant */
	D_PRINTF_FMT_EMPTY,		/* format string is empty */
	D_DECL_CHARATTR,		/* bad attributes for char decl */
	D_DECL_VOIDATTR,		/* bad attributes for void decl */
	D_DECL_SIGNINT,			/* sign/unsign with non-integer decl */
	D_DECL_LONGINT,			/* long with non-arithmetic decl */
	D_DECL_IDENT,			/* old-style declaration or bad type */
	D_DECL_CLASS,			/* more than one storage class given */
	D_DECL_BADCLASS,		/* decl class not supported in D */
	D_DECL_PARMCLASS,		/* invalid class for parameter type */
	D_DECL_COMBO, 			/* bad decl specifier combination */
	D_DECL_ARRSUB,			/* const int required for array size */
	D_DECL_ARRNULL,			/* array decl requires dim or tuple */
	D_DECL_ARRBIG,			/* array size too big */
	D_DECL_IDRED,			/* decl identifier redeclared */
	D_DECL_TYPERED,			/* decl type redeclared */
	D_DECL_MNAME,			/* member name missing */
	D_DECL_SCOPE,			/* scoping operator used in decl */
	D_DECL_BFCONST,			/* bit-field requires const size expr */
	D_DECL_BFSIZE,			/* bit-field size too big for type */
	D_DECL_BFTYPE,			/* bit-field type is not valid */
	D_DECL_ENCONST,			/* enum tag requires const size expr */
	D_DECL_ENOFLOW,			/* enumerator value overflows INT_MAX */
	D_DECL_USELESS,			/* useless external declaration */
	D_DECL_LOCASSC,			/* attempt to decl local assc array */
	D_DECL_VOIDOBJ,			/* attempt to decl void object */
	D_DECL_DYNOBJ,			/* attempt to decl dynamic object */
	D_DECL_INCOMPLETE,		/* declaration uses incomplete type */
	D_DECL_PROTO_VARARGS,		/* varargs not allowed in prototype */
	D_DECL_PROTO_TYPE,		/* type not allowed in prototype */
	D_DECL_PROTO_VOID,		/* void must be sole parameter */
	D_DECL_PROTO_NAME,		/* void parameter may not have a name */
	D_DECL_PROTO_FORM,		/* parameter name has no formal */
	D_COMM_COMM,			/* commit() after commit() */
	D_COMM_DREC,			/* commit() after data action */
	D_SPEC_SPEC,			/* speculate() after speculate() */
	D_SPEC_COMM,			/* speculate() after commit() */
	D_SPEC_DREC,			/* speculate() after data action */
	D_AGG_COMM,			/* aggregating act after commit() */
	D_AGG_SPEC,			/* aggregating act after speculate() */
	D_AGG_NULL,			/* aggregation stmt has null effect */
	D_AGG_SCALAR,			/* aggregating function needs scalar */
	D_ACT_SPEC,			/* destructive action after speculate */
	D_EXIT_SPEC,			/* exit() action after speculate */
	D_DREC_COMM,			/* data action after commit() */
	D_PRINTA_PROTO,			/* printa() prototype mismatch */
	D_PRINTA_AGGARG,		/* aggregation arg type mismatch */
	D_PRINTA_AGGBAD,		/* printa() aggregation not defined */
	D_PRINTA_AGGKEY,		/* printa() aggregation key mismatch */
	D_PRINTA_AGGPROTO,		/* printa() aggregation mismatch */
	D_TRACE_VOID,			/* trace() argument has void type */
	D_TRACE_DYN,			/* trace() argument has dynamic type */
	D_TRACE_AGG,			/* trace() argument is an aggregation */
	D_PRINT_VOID,			/* print() argument has void type */
	D_PRINT_DYN,			/* print() argument has dynamic type */
	D_PRINT_AGG,			/* print() argument is an aggregation */
	D_TRACEMEM_ADDR,		/* tracemem() address bad type */
	D_TRACEMEM_SIZE,		/* tracemem() size bad type */
	D_TRACEMEM_ARGS,		/* tracemem() illegal number of args */
	D_TRACEMEM_DYNSIZE,		/* tracemem() dynamic size bad type */
	D_STACK_PROTO,			/* stack() prototype mismatch */
	D_STACK_SIZE,			/* stack() size argument bad type */
	D_USTACK_FRAMES,		/* ustack() frames arg bad type */
	D_USTACK_STRSIZE,		/* ustack() strsize arg bad type */
	D_USTACK_PROTO,			/* ustack() prototype mismatch */
	D_LQUANT_BASETYPE,		/* lquantize() bad base type */
	D_LQUANT_BASEVAL,		/* lquantize() bad base value */
	D_LQUANT_LIMTYPE,		/* lquantize() bad limit type */
	D_LQUANT_LIMVAL,		/* lquantize() bad limit value */
	D_LQUANT_MISMATCH,		/* lquantize() limit < base */
	D_LQUANT_STEPTYPE,		/* lquantize() bad step type */
	D_LQUANT_STEPVAL,		/* lquantize() bad step value */
	D_LQUANT_STEPLARGE,		/* lquantize() step too large */
	D_LQUANT_STEPSMALL,		/* lquantize() step too small */
	D_QUANT_PROTO,			/* quantize() prototype mismatch */
	D_PROC_OFF,			/* byte offset exceeds function size */
	D_PROC_ALIGN,			/* byte offset has invalid alignment */
	D_PROC_NAME,			/* invalid process probe name */
	D_PROC_GRAB,			/* failed to grab process */
	D_PROC_DYN,			/* process is not dynamically linked */
	D_PROC_LIB,			/* invalid process library name */
	D_PROC_FUNC,			/* no such function in process */
	D_PROC_CREATEFAIL,		/* pid probe creation failed */
	D_PROC_NODEV,			/* fasttrap device is not installed */
	D_PROC_BADPID,			/* user probe pid invalid */
	D_PROC_BADPROV,			/* user probe provider invalid */
	D_PROC_USDT,			/* problem initializing usdt */
	D_CLEAR_PROTO,			/* clear() prototype mismatch */
	D_CLEAR_AGGARG,			/* aggregation arg type mismatch */
	D_CLEAR_AGGBAD,			/* clear() aggregation not defined */
	D_NORMALIZE_PROTO,		/* normalize() prototype mismatch */
	D_NORMALIZE_SCALAR,		/* normalize() value must be scalar */
	D_NORMALIZE_AGGARG,		/* aggregation arg type mismatch */
	D_NORMALIZE_AGGBAD,		/* normalize() aggregation not def. */
	D_TRUNC_PROTO,			/* trunc() prototype mismatch */
	D_TRUNC_SCALAR,			/* trunc() value must be scalar */
	D_TRUNC_AGGARG,			/* aggregation arg type mismatch */
	D_TRUNC_AGGBAD,			/* trunc() aggregation not def. */
	D_PROV_BADNAME,			/* invalid provider name */
	D_PROV_INCOMPAT,		/* provider/probe interface mismatch */
	D_PROV_PRDUP,			/* duplicate probe declaration */
	D_PROV_PRARGLEN,		/* probe argument list too long */
	D_PROV_PRXLATOR,		/* probe argument translator missing */
	D_FREOPEN_INVALID,		/* frename() filename is invalid */
	D_LQUANT_MATCHBASE,		/* lquantize() mismatch on base */
	D_LQUANT_MATCHLIM,		/* lquantize() mismatch on limit */
	D_LQUANT_MATCHSTEP,		/* lquantize() mismatch on step */
	D_LLQUANT_FACTORTYPE,		/* llquantize() bad magnitude type */
	D_LLQUANT_FACTORVAL,		/* llquantize() bad magnitude value */
	D_LLQUANT_FACTORMATCH,		/* llquantize() mismatch on magnitude */
	D_LLQUANT_LOWTYPE,		/* llquantize() bad low mag type */
	D_LLQUANT_LOWVAL,		/* llquantize() bad low mag value */
	D_LLQUANT_LOWMATCH,		/* llquantize() mismatch on low mag */
	D_LLQUANT_HIGHTYPE,		/* llquantize() bad high mag type */
	D_LLQUANT_HIGHVAL,		/* llquantize() bad high mag value */
	D_LLQUANT_HIGHMATCH,		/* llquantize() mismatch on high mag */
	D_LLQUANT_NSTEPTYPE,		/* llquantize() bad # steps type */
	D_LLQUANT_NSTEPVAL,		/* llquantize() bad # steps value */
	D_LLQUANT_NSTEPMATCH,		/* llquantize() mismatch on # steps */
	D_LLQUANT_MAGRANGE,		/* llquantize() bad magnitude range */
	D_LLQUANT_FACTORNSTEPS,		/* llquantize() # steps < factor */
	D_LLQUANT_FACTOREVEN,		/* llquantize() bad # steps/factor */
	D_LLQUANT_FACTORSMALL,		/* llquantize() magnitude too small */
	D_LLQUANT_MAGTOOBIG,		/* llquantize() high mag too large */
	D_NOREG,			/* no available internal registers */
	D_PRINTM_ADDR,			/* printm() memref bad type */
	D_PRINTM_SIZE,			/* printm() size bad type */
} dt_errtag_t;

extern const char *dt_errtag(dt_errtag_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_ERRTAGS_H */
