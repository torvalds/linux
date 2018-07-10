/* vi: set sw=4 ts=4: */
/*
 * Adapted from ash applet code
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Copyright (c) 1989, 1991, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 1997-2005 Herbert Xu <herbert@gondor.apana.org.au>
 * was re-ported from NetBSD and debianized.
 *
 * Copyright (c) 2010 Denys Vlasenko
 * Split from ash.c
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#ifndef SHELL_COMMON_H
#define SHELL_COMMON_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

extern const char defifsvar[] ALIGN1; /* "IFS= \t\n" */
#define defifs (defifsvar + 4)

extern const char defoptindvar[] ALIGN1; /* "OPTIND=1" */

int FAST_FUNC is_well_formed_var_name(const char *s, char terminator);

/* Builtins */

enum {
	BUILTIN_READ_SILENT = 1 << 0,
	BUILTIN_READ_RAW    = 1 << 1,
};
//TODO? do not provide bashisms if not asked for:
//#if !ENABLE_HUSH_BASH_COMPAT && !ENABLE_ASH_BASH_COMPAT
//#define shell_builtin_read(setvar,argv,ifs,read_flags,n,p,t,u,d)
//	shell_builtin_read(setvar,argv,ifs,read_flags)
//#endif
const char* FAST_FUNC
shell_builtin_read(void FAST_FUNC (*setvar)(const char *name, const char *val),
	char       **argv,
	const char *ifs,
	int        read_flags,
	const char *opt_n,
	const char *opt_p,
	const char *opt_t,
	const char *opt_u,
	const char *opt_d
);

int FAST_FUNC
shell_builtin_ulimit(char **argv);

POP_SAVED_FUNCTION_VISIBILITY

#endif
