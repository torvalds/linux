%{
/*
 * Sub-Lexical Analyzer for macro invokation in 
 * the Aic7xxx SCSI Host adapter sequencer assembler.
 *
 * Copyright (c) 2001 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aicasm/aicasm_macro_scan.l#8 $
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <inttypes.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#ifdef __linux__
#include "../queue.h"
#else
#include <sys/queue.h>
#endif

#include "aicasm.h"
#include "aicasm_symbol.h"
#include "aicasm_macro_gram.h"

#define MAX_STR_CONST 4096
static char string_buf[MAX_STR_CONST];
static char *string_buf_ptr;
static int  parren_count;
static char buf[255];
%}

WORD		[A-Za-z_][-A-Za-z_0-9]*
SPACE		[ \t]+
MCARG		[^(), \t]+

%x ARGLIST

%%
\n			{
				++yylineno;
			}
\r			;
<ARGLIST>{SPACE}	;
<ARGLIST>\(		{
				parren_count++;
				if (parren_count == 1) {
					string_buf_ptr = string_buf;
					return ('(');
				}
				*string_buf_ptr++ = '(';
			}
<ARGLIST>\)		{
				if (parren_count == 1) {
					if (string_buf_ptr != string_buf) {
						/*
						 * Return an argument and
						 * rescan this parren so we
						 * can return it as well.
						 */
						*string_buf_ptr = '\0';
						mmlval.str = string_buf;
						string_buf_ptr = string_buf;
						unput(')');
						return T_ARG;
					}
					BEGIN INITIAL;
					return (')');
				}
				parren_count--;
				*string_buf_ptr++ = ')';
			}
<ARGLIST>{MCARG}	{
				char *yptr;

				yptr = mmtext;
				while (*yptr)
					*string_buf_ptr++ = *yptr++;
			}
<ARGLIST>\,		{
				if (string_buf_ptr != string_buf) {
					/*
					 * Return an argument and
					 * rescan this comma so we
					 * can return it as well.
					 */
					*string_buf_ptr = '\0';
					mmlval.str = string_buf;
					string_buf_ptr = string_buf;
					unput(',');
					return T_ARG;
				}
				return ',';
			}
{WORD}[(]		{
				/* May be a symbol or a macro invocation. */
				mmlval.sym = symtable_get(mmtext);
				if (mmlval.sym->type != MACRO) {
					stop("Expecting Macro Name",
					     EX_DATAERR);
				}
				unput('(');
				parren_count = 0;
				BEGIN ARGLIST;
				return T_SYMBOL;
			}
.			{ 
				snprintf(buf, sizeof(buf), "Invalid character "
					 "'%c'", mmtext[0]);
				stop(buf, EX_DATAERR);
			}
%%

int
mmwrap()
{
	stop("EOF encountered in macro call", EX_DATAERR);
}
