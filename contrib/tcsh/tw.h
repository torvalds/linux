/* $Header: /p/tcsh/cvsroot/tcsh/tw.h,v 3.25 2006/01/12 18:15:25 christos Exp $ */
/*
 * tw.h: TwENEX functions headers
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _h_tw
#define _h_tw

#define TW_PATH		0x1000
#define TW_ZERO		0x0fff

#define TW_NONE		0x0000
#define TW_COMMAND	0x0001
#define TW_VARIABLE	0x0002
#define TW_LOGNAME	0x0003
#define TW_FILE		0x0004
#define TW_DIRECTORY	0x0005
#define TW_VARLIST	0x0006
#define TW_USER		0x0007
#define TW_COMPLETION	0x0008
#define TW_ALIAS	0x0009
#define TW_SHELLVAR	0x000a
#define TW_ENVVAR	0x000b
#define TW_BINDING	0x000c
#define TW_WORDLIST	0x000d
#define TW_LIMIT	0x000e
#define TW_SIGNAL	0x000f
#define TW_JOB		0x0010
#define TW_EXPLAIN	0x0011
#define TW_TEXT		0x0012
#define TW_GRPNAME	0x0013

#define TW_EXEC_CHK	0x01
#define TW_DIR_CHK	0x02
#define TW_TEXT_CHK	0x04

#define TW_DIR_OK	0x10
#define TW_PAT_OK	0x20
#define TW_IGN_OK	0x40

#ifndef TRUE
# define TRUE		1
#endif
#ifndef FALSE
# define FALSE		0
#endif
#define ON		1
#define OFF		0
#define ESC             CTL_ESC('\033')

#define is_set(var)	adrof(var)
#define ismetahash(a)	(ismeta(a) && (a) != '#')

#define SEARCHLIST "HPATH"	/* Env. param for helpfile searchlist */
#define DEFAULTLIST ":/usr/man/cat1:/usr/man/cat8:/usr/man/cat6:/usr/local/man/cat1:/usr/local/man/cat8:/usr/local/man/cat6"	/* if no HPATH */

typedef enum {
    LIST, LIST_ALL, RECOGNIZE, RECOGNIZE_ALL, RECOGNIZE_SCROLL, 
    PRINT_HELP, SPELL, GLOB, GLOB_EXPAND, VARS_EXPAND, PATH_NORMALIZE,
    COMMAND_NORMALIZE
} COMMAND;

struct scroll_tab_list {
	Char *element;
	struct scroll_tab_list *next;
} ;

extern struct scroll_tab_list *scroll_tab;
extern int curchoice;

extern int non_unique_match;

extern int match_unique_match;

extern int InsideCompletion;

extern struct varent completions;

extern int color_context_ls;

#include "tw.decls.h"

#endif /* _h_tw */
