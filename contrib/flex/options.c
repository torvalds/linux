/* flex - tool to generate fast lexical analyzers */

/*  Copyright (c) 1990 The Regents of the University of California. */
/*  All rights reserved. */

/*  This code is derived from software contributed to Berkeley by */
/*  Vern Paxson. */

/*  The United States Government has rights in this work pursuant */
/*  to contract no. DE-AC03-76SF00098 between the United States */
/*  Department of Energy and the University of California. */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "options.h"

/* Be sure to synchronize these options with those defined in "options.h",
 * the giant switch() statement in "main.c", and the %option processing in
 * "scan.l".
 */


/* The command-line options, passed to scanopt_init() */
optspec_t flexopts[] = {

	{"-7", OPT_7BIT, 0}
	,
	{"--7bit", OPT_7BIT, 0}
	,			/* Generate 7-bit scanner. */
	{"-8", OPT_8BIT, 0}
	,
	{"--8bit", OPT_8BIT, 0}
	,			/* Generate 8-bit scanner. */
	{"--align", OPT_ALIGN, 0}
	,			/* Trade off larger tables for better memory alignment. */
	{"--noalign", OPT_NO_ALIGN, 0}
	,
	{"--always-interactive", OPT_ALWAYS_INTERACTIVE, 0}
	,
	{"--array", OPT_ARRAY, 0}
	,
	{"-b", OPT_BACKUP, 0}
	,
	{"--backup", OPT_BACKUP, 0}
	,			/* Generate backing-up information to lex.backup. */
	{"-B", OPT_BATCH, 0}
	,
	{"--batch", OPT_BATCH, 0}
	,			/* Generate batch scanner (opposite of -I). */
	{"--bison-bridge", OPT_BISON_BRIDGE, 0}
	,			/* Scanner to be called by a bison pure parser. */
	{"--bison-locations", OPT_BISON_BRIDGE_LOCATIONS, 0}
	,			/* Scanner to be called by a bison pure parser. */
	{"-i", OPT_CASE_INSENSITIVE, 0}
	,
	{"--case-insensitive", OPT_CASE_INSENSITIVE, 0}
	,			/* Generate case-insensitive scanner. */
	
		{"-C[aefFmr]", OPT_COMPRESSION,
	 "Specify degree of table compression (default is -Cem)"},
	{"-+", OPT_CPLUSPLUS, 0}
	,
	{"--c++", OPT_CPLUSPLUS, 0}
	,			/* Generate C++ scanner class. */
	{"-d", OPT_DEBUG, 0}
	,
	{"--debug", OPT_DEBUG, 0}
	,			/* Turn on debug mode in generated scanner. */
	{"--nodebug", OPT_NO_DEBUG, 0}
	,
	{"-s", OPT_NO_DEFAULT, 0}
	,
	{"--nodefault", OPT_NO_DEFAULT, 0}
	,			/* Suppress default rule to ECHO unmatched text. */
	{"--default", OPT_DEFAULT, 0}
	,
	{"-c", OPT_DONOTHING, 0}
	,			/* For POSIX lex compatibility. */
	{"-n", OPT_DONOTHING, 0}
	,			/* For POSIX lex compatibility. */
	{"--ecs", OPT_ECS, 0}
	,			/* Construct equivalence classes. */
	{"--noecs", OPT_NO_ECS, 0}
	,
	{"-F", OPT_FAST, 0}
	,
	{"--fast", OPT_FAST, 0}
	,			/* Same as -CFr. */
	{"-f", OPT_FULL, 0}
	,
	{"--full", OPT_FULL, 0}
	,			/* Same as -Cfr. */
	{"--header-file[=FILE]", OPT_HEADER_FILE, 0}
	,
	{"-?", OPT_HELP, 0}
	,
	{"-h", OPT_HELP, 0}
	,
	{"--help", OPT_HELP, 0}
	,			/* Produce this help message. */
	{"-I", OPT_INTERACTIVE, 0}
	,
	{"--interactive", OPT_INTERACTIVE, 0}
	,			/* Generate interactive scanner (opposite of -B). */
	{"-l", OPT_LEX_COMPAT, 0}
	,
	{"--lex-compat", OPT_LEX_COMPAT, 0}
	,			/* Maximal compatibility with original lex. */
	{"-X", OPT_POSIX_COMPAT, 0}
	,
	{"--posix-compat", OPT_POSIX_COMPAT, 0}
	,			/* Maximal compatibility with POSIX lex. */
        {"--preproc=NUM", OPT_PREPROC_LEVEL, 0}
        ,
	{"-L", OPT_NO_LINE, 0}
	,			/* Suppress #line directives in scanner. */
	{"--noline", OPT_NO_LINE, 0}
	,			/* Suppress #line directives in scanner. */
	{"--main", OPT_MAIN, 0}
	,			/* use built-in main() function. */
	{"--nomain", OPT_NO_MAIN, 0}
	,
	{"--meta-ecs", OPT_META_ECS, 0}
	,			/* Construct meta-equivalence classes. */
	{"--nometa-ecs", OPT_NO_META_ECS, 0}
	,
	{"--never-interactive", OPT_NEVER_INTERACTIVE, 0}
	,
	{"-o FILE", OPT_OUTFILE, 0}
	,
	{"--outfile=FILE", OPT_OUTFILE, 0}
	,			/* Write to FILE (default is lex.yy.c) */
	{"-p", OPT_PERF_REPORT, 0}
	,
	{"--perf-report", OPT_PERF_REPORT, 0}
	,			/* Generate performance report to stderr. */
	{"--pointer", OPT_POINTER, 0}
	,
	{"-P PREFIX", OPT_PREFIX, 0}
	,
	{"--prefix=PREFIX", OPT_PREFIX, 0}
	,			/* Use PREFIX (default is yy) */
	{"-Dmacro", OPT_PREPROCDEFINE, 0}
	,			/* Define a preprocessor symbol. */
	{"--read", OPT_READ, 0}
	,			/* Use read(2) instead of stdio. */
	{"-R", OPT_REENTRANT, 0}
	,
	{"--reentrant", OPT_REENTRANT, 0}
	,			/* Generate a reentrant C scanner. */
	{"--noreentrant", OPT_NO_REENTRANT, 0}
	,
	{"--reject", OPT_REJECT, 0}
	,
	{"--noreject", OPT_NO_REJECT, 0}
	,
	{"-S FILE", OPT_SKEL, 0}
	,
	{"--skel=FILE", OPT_SKEL, 0}
	,			/* Use skeleton from FILE */
	{"--stack", OPT_STACK, 0}
	,
	{"--stdinit", OPT_STDINIT, 0}
	,
	{"--nostdinit", OPT_NO_STDINIT, 0}
	,
	{"-t", OPT_STDOUT, 0}
	,
	{"--stdout", OPT_STDOUT, 0}
	,			/* Write generated scanner to stdout. */
	{"-T", OPT_TRACE, 0}
	,
	{"--trace", OPT_TRACE, 0}
	,			/* Flex should run in trace mode. */
	{"--tables-file[=FILE]", OPT_TABLES_FILE, 0}
	,			/* Save tables to FILE */
        {"--tables-verify", OPT_TABLES_VERIFY, 0}
        ,                       /* Tables integrity check */
	{"--nounistd", OPT_NO_UNISTD_H, 0}
	,			/* Do not include unistd.h */
	{"-v", OPT_VERBOSE, 0}
	,
	{"--verbose", OPT_VERBOSE, 0}
	,			/* Write summary of scanner statistics to stdout. */
	{"-V", OPT_VERSION, 0}
	,
	{"--version", OPT_VERSION, 0}
	,			/* Report flex version. */
	{"--warn", OPT_WARN, 0}
	,
	{"-w", OPT_NO_WARN, 0}
	,
	{"--nowarn", OPT_NO_WARN, 0}
	,			/* Suppress warning messages. */
	{"--noansi-definitions", OPT_NO_ANSI_FUNC_DEFS, 0}
	,
	{"--noansi-prototypes", OPT_NO_ANSI_FUNC_PROTOS, 0}
	,
	{"--yyclass=NAME", OPT_YYCLASS, 0}
	,
	{"--yylineno", OPT_YYLINENO, 0}
	,
	{"--noyylineno", OPT_NO_YYLINENO, 0}
	,

	{"--yymore", OPT_YYMORE, 0}
	,
	{"--noyymore", OPT_NO_YYMORE, 0}
	,
	{"--noyywrap", OPT_NO_YYWRAP, 0}
	,
	{"--yywrap", OPT_YYWRAP, 0}
	,

	{"--nounput", OPT_NO_UNPUT, 0}
	,
	{"--noyy_push_state", OPT_NO_YY_PUSH_STATE, 0}
	,
	{"--noyy_pop_state", OPT_NO_YY_POP_STATE, 0}
	,
	{"--noyy_top_state", OPT_NO_YY_TOP_STATE, 0}
	,
	{"--noyy_scan_buffer", OPT_NO_YY_SCAN_BUFFER, 0}
	,
	{"--noyy_scan_bytes", OPT_NO_YY_SCAN_BYTES, 0}
	,
	{"--noyy_scan_string", OPT_NO_YY_SCAN_STRING, 0}
	,
	{"--noyyget_extra", OPT_NO_YYGET_EXTRA, 0}
	,
	{"--noyyset_extra", OPT_NO_YYSET_EXTRA, 0}
	,
	{"--noyyget_leng", OPT_NO_YYGET_LENG, 0}
	,
	{"--noyyget_text", OPT_NO_YYGET_TEXT, 0}
	,
	{"--noyyget_lineno", OPT_NO_YYGET_LINENO, 0}
	,
	{"--noyyset_lineno", OPT_NO_YYSET_LINENO, 0}
	,
	{"--noyyget_in", OPT_NO_YYGET_IN, 0}
	,
	{"--noyyset_in", OPT_NO_YYSET_IN, 0}
	,
	{"--noyyget_out", OPT_NO_YYGET_OUT, 0}
	,
	{"--noyyset_out", OPT_NO_YYSET_OUT, 0}
	,
	{"--noyyget_lval", OPT_NO_YYGET_LVAL, 0}
	,
	{"--noyyset_lval", OPT_NO_YYSET_LVAL, 0}
	,
	{"--noyyget_lloc", OPT_NO_YYGET_LLOC, 0}
	,
	{"--noyyset_lloc", OPT_NO_YYSET_LLOC, 0}
	,

	{0, 0, 0}		/* required final NULL entry. */
};

/* vim:set tabstop=8 softtabstop=4 shiftwidth=4: */
