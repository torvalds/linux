%{
/*
 * Lexical Analyzer for the Aic7xxx SCSI Host adapter sequencer assembler.
 *
 * Copyright (c) 1997, 1998, 2000 Justin T. Gibbs.
 * Copyright (c) 2001, 2002 Adaptec Inc.
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
 * $Id: //depot/aic7xxx/aic7xxx/aicasm/aicasm_scan.l#20 $
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
#include "aicasm_gram.h"

/* This is used for macro body capture too, so err on the large size. */
#define MAX_STR_CONST 4096
static char string_buf[MAX_STR_CONST];
static char *string_buf_ptr;
static int  parren_count;
static int  quote_count;
static char buf[255];
%}

PATH		([/]*[-A-Za-z0-9_.])+
WORD		[A-Za-z_][-A-Za-z_0-9]*
SPACE		[ \t]+
MCARG		[^(), \t]+
MBODY		((\\[^\n])*[^\n\\]*)+

%x COMMENT
%x CEXPR
%x INCLUDE
%x STRING
%x MACRODEF
%x MACROARGLIST
%x MACROCALLARGS
%x MACROBODY

%%
\n			{ ++yylineno; }
\r			;
"/*"			{ BEGIN COMMENT;  /* Enter comment eating state */ }
<COMMENT>"/*"		{ fprintf(stderr, "Warning! Comment within comment."); }
<COMMENT>\n		{ ++yylineno; }
<COMMENT>[^*/\n]*	;
<COMMENT>"*"+[^*/\n]*	;
<COMMENT>"/"+[^*/\n]*	;
<COMMENT>"*"+"/"	{ BEGIN INITIAL; }
if[ \t]*\(		{
				string_buf_ptr = string_buf;
				parren_count = 1;
				BEGIN CEXPR;
				return T_IF;
			}
<CEXPR>\(		{	*string_buf_ptr++ = '('; parren_count++; }
<CEXPR>\)		{
				parren_count--;
				if (parren_count == 0) {
					/* All done */
					BEGIN INITIAL;
					*string_buf_ptr = '\0';
					yylval.sym = symtable_get(string_buf);
					return T_CEXPR;
				} else {
					*string_buf_ptr++ = ')';
				}
			}
<CEXPR>\n		{ ++yylineno; }
<CEXPR>\r		;
<CEXPR>[^()\n]+	{
				char *yptr;

				yptr = yytext;
				while (*yptr != '\0') {
					/* Remove duplicate spaces */
					if (*yptr == '\t')
						*yptr = ' ';
					if (*yptr == ' '
					 && string_buf_ptr != string_buf
					 && string_buf_ptr[-1] == ' ')
						yptr++;
					else 
						*string_buf_ptr++ = *yptr++;
				}
			}
else			{ return T_ELSE; }
VERSION			{ return T_VERSION; }
PREFIX			{ return T_PREFIX; }
PATCH_ARG_LIST		{ return T_PATCH_ARG_LIST; }
\"			{
				string_buf_ptr = string_buf;
				BEGIN STRING;
			}
<STRING>[^"]+		{
				char *yptr;

				yptr = yytext;
				while (*yptr)
					*string_buf_ptr++ = *yptr++;
			}
<STRING>\"		{
				/* All done */
				BEGIN INITIAL;
				*string_buf_ptr = '\0';
				yylval.str = string_buf;
				return T_STRING;
			}
{SPACE}			 ;

	/* Register/SCB/SRAM definition keywords */
export			{ return T_EXPORT; }
register		{ return T_REGISTER; }
const			{ yylval.value = FALSE; return T_CONST; }
download		{ return T_DOWNLOAD; }
address			{ return T_ADDRESS; }
count			{ return T_COUNT; }
access_mode		{ return T_ACCESS_MODE; }
modes			{ return T_MODES; }
RW|RO|WO		{
				 if (strcmp(yytext, "RW") == 0)
					yylval.value = RW;
				 else if (strcmp(yytext, "RO") == 0)
					yylval.value = RO;
				 else
					yylval.value = WO;
				 return T_MODE;
			}
field			{ return T_FIELD; }
enum			{ return T_ENUM; }
mask			{ return T_MASK; }
alias			{ return T_ALIAS; }
size			{ return T_SIZE; }
scb			{ return T_SCB; }
scratch_ram		{ return T_SRAM; }
accumulator		{ return T_ACCUM; }
mode_pointer		{ return T_MODE_PTR; }
allones			{ return T_ALLONES; }
allzeros		{ return T_ALLZEROS; }
none			{ return T_NONE; }
sindex			{ return T_SINDEX; }
A			{ return T_A; }

	/* Instruction Formatting */
PAD_PAGE		{ return T_PAD_PAGE; }
BEGIN_CRITICAL		{ return T_BEGIN_CS; }
END_CRITICAL		{ return T_END_CS; }
SET_SRC_MODE		{ return T_SET_SRC_MODE; }
SET_DST_MODE		{ return T_SET_DST_MODE; }

	/* Opcodes */
shl			{ return T_SHL; }
shr			{ return T_SHR; }
ror			{ return T_ROR; }
rol			{ return T_ROL; }
mvi			{ return T_MVI; }
mov			{ return T_MOV; }
clr			{ return T_CLR; }
jmp			{ return T_JMP; }
jc			{ return T_JC;	}
jnc			{ return T_JNC;	}
je			{ return T_JE;	}
jne			{ return T_JNE;	}
jz			{ return T_JZ;	}
jnz			{ return T_JNZ;	}
call			{ return T_CALL; }
add			{ return T_ADD; }
adc			{ return T_ADC; }
bmov			{ return T_BMOV; }
inc			{ return T_INC; }
dec			{ return T_DEC; }
stc			{ return T_STC;	}
clc			{ return T_CLC; }
cmp			{ return T_CMP;	}
not			{ return T_NOT;	}
xor			{ return T_XOR;	}
test			{ return T_TEST;}
and			{ return T_AND;	}
or			{ return T_OR;	}
ret			{ return T_RET; }
nop			{ return T_NOP; }

	/* ARP2 16bit extensions */
	/* or16			{ return T_OR16; } */
	/* and16			{ return T_AND16; }*/
	/* xor16			{ return T_XOR16; }*/
	/* add16			{ return T_ADD16; }*/
	/* adc16			{ return T_ADC16; }*/
	/* mvi16			{ return T_MVI16; }*/
	/* test16			{ return T_TEST16; }*/
	/* cmp16			{ return T_CMP16; }*/
	/* cmpxchg			{ return T_CMPXCHG; }*/

	/* Allowed Symbols */
\<\<			{ return T_EXPR_LSHIFT; }
\>\>			{ return T_EXPR_RSHIFT; }
[-+,:()~|&."{};<>[\]/*!=] { return yytext[0]; }

	/* Number processing */
0[0-7]*			{
				yylval.value = strtol(yytext, NULL, 8);
				return T_NUMBER;
			}

0[xX][0-9a-fA-F]+	{
				yylval.value = strtoul(yytext + 2, NULL, 16);
				return T_NUMBER;
			}

[1-9][0-9]*		{
				yylval.value = strtol(yytext, NULL, 10);
				return T_NUMBER;
			}
	/* Include Files */
#include{SPACE}		{
				BEGIN INCLUDE;
				quote_count = 0;
				return T_INCLUDE;
			}
<INCLUDE>[<]		{ return yytext[0]; }
<INCLUDE>[>]		{ BEGIN INITIAL; return yytext[0]; }
<INCLUDE>[\"]		{
				if (quote_count != 0)
					BEGIN INITIAL;
				quote_count++;
				return yytext[0];
			}
<INCLUDE>{PATH}		{
				char *yptr;

				yptr = yytext;
				string_buf_ptr = string_buf;
				while (*yptr)
					*string_buf_ptr++ = *yptr++;
				yylval.str = string_buf;
				*string_buf_ptr = '\0';
				return T_PATH;
			}
<INCLUDE>.		{ stop("Invalid include line", EX_DATAERR); }
#define{SPACE}		{
				BEGIN MACRODEF;
				return T_DEFINE;
			}
<MACRODEF>{WORD}{SPACE}	{ 
				char *yptr;

				/* Strip space and return as a normal symbol */
				yptr = yytext;
				while (*yptr != ' ' && *yptr != '\t')
					yptr++;
				*yptr = '\0';
				yylval.sym = symtable_get(yytext);
				string_buf_ptr = string_buf;
				BEGIN MACROBODY;
				return T_SYMBOL;
			}
<MACRODEF>{WORD}\(	{
				/*
				 * We store the symbol with its opening
				 * parren so we can differentiate macros
				 * that take args from macros with the
				 * same name that do not take args as
				 * is allowed in C.
				 */
				BEGIN MACROARGLIST;
				yylval.sym = symtable_get(yytext);
				unput('(');
				return T_SYMBOL;
			}
<MACROARGLIST>{WORD}	{
				yylval.str = yytext;
				return T_ARG;
			}
<MACROARGLIST>{SPACE}   ;
<MACROARGLIST>[(,]	{
				return yytext[0];
			}
<MACROARGLIST>[)]	{
				string_buf_ptr = string_buf;
				BEGIN MACROBODY;
				return ')';
			}
<MACROARGLIST>.		{
				snprintf(buf, sizeof(buf), "Invalid character "
					 "'%c' in macro argument list",
					 yytext[0]);
				stop(buf, EX_DATAERR);
			}
<MACROCALLARGS>{SPACE}  ;
<MACROCALLARGS>\(	{
				parren_count++;
				if (parren_count == 1)
					return ('(');
				*string_buf_ptr++ = '(';
			}
<MACROCALLARGS>\)	{
				parren_count--;
				if (parren_count == 0) {
					BEGIN INITIAL;
					return (')');
				}
				*string_buf_ptr++ = ')';
			}
<MACROCALLARGS>{MCARG}	{
				char *yptr;

				yptr = yytext;
				while (*yptr)
					*string_buf_ptr++ = *yptr++;
			}
<MACROCALLARGS>\,	{
				if (string_buf_ptr != string_buf) {
					/*
					 * Return an argument and
					 * rescan this comma so we
					 * can return it as well.
					 */
					*string_buf_ptr = '\0';
					yylval.str = string_buf;
					string_buf_ptr = string_buf;
					unput(',');
					return T_ARG;
				}
				return ',';
			}
<MACROBODY>\\\n		{
				/* Eat escaped newlines. */
				++yylineno;
			}
<MACROBODY>\r		;
<MACROBODY>\n		{
				/* Macros end on the first unescaped newline. */
				BEGIN INITIAL;
				*string_buf_ptr = '\0';
				yylval.str = string_buf;
				++yylineno;
				return T_MACROBODY;
			}
<MACROBODY>{MBODY}	{
				char *yptr;
				char c;

				yptr = yytext;
				while (c = *yptr++) {
					/*
					 * Strip carriage returns.
					 */
					if (c == '\r')
						continue;
					*string_buf_ptr++ = c;
				}
			}
{WORD}\(		{
				char *yptr;
				char *ycopy;

				/* May be a symbol or a macro invocation. */
				yylval.sym = symtable_get(yytext);
				if (yylval.sym->type == MACRO) {
					YY_BUFFER_STATE old_state;
					YY_BUFFER_STATE temp_state;

					ycopy = strdup(yytext);
					yptr = ycopy + yyleng;
					while (yptr > ycopy)
						unput(*--yptr);
					old_state = YY_CURRENT_BUFFER;
					temp_state =
					    yy_create_buffer(stdin,
							     YY_BUF_SIZE);
					yy_switch_to_buffer(temp_state);
					mm_switch_to_buffer(old_state);
					mmparse();
					mm_switch_to_buffer(temp_state);
					yy_switch_to_buffer(old_state);
					mm_delete_buffer(temp_state);
					expand_macro(yylval.sym);
				} else {
					if (yylval.sym->type == UNINITIALIZED) {
						/* Try without the '(' */
						symbol_delete(yylval.sym);
						yytext[yyleng-1] = '\0';
						yylval.sym =
						    symtable_get(yytext);
					}
					unput('(');
					return T_SYMBOL;
				}
			}
{WORD}			{
				yylval.sym = symtable_get(yytext);
				if (yylval.sym->type == MACRO) {
					expand_macro(yylval.sym);
				} else {
					return T_SYMBOL;
				}
			}
.			{ 
				snprintf(buf, sizeof(buf), "Invalid character "
					 "'%c'", yytext[0]);
				stop(buf, EX_DATAERR);
			}
%%

typedef struct include {
        YY_BUFFER_STATE  buffer;
        int              lineno;
        char            *filename;
	SLIST_ENTRY(include) links;
}include_t;

SLIST_HEAD(, include) include_stack;

void
include_file(char *file_name, include_type type)
{
	FILE *newfile;
	include_t *include;

	newfile = NULL;
	/* Try the current directory first */
	if (includes_search_curdir != 0 || type == SOURCE_FILE)
		newfile = fopen(file_name, "r");

	if (newfile == NULL && type != SOURCE_FILE) {
                path_entry_t include_dir;
                for (include_dir = search_path.slh_first;
                     include_dir != NULL;                
                     include_dir = include_dir->links.sle_next) {
			char fullname[PATH_MAX];

			if ((include_dir->quoted_includes_only == TRUE)
			 && (type != QUOTED_INCLUDE))
				continue;

			snprintf(fullname, sizeof(fullname),
				 "%s/%s", include_dir->directory, file_name);

			if ((newfile = fopen(fullname, "r")) != NULL)
				break;
                }
        }

	if (newfile == NULL) {
		perror(file_name);
		stop("Unable to open input file", EX_SOFTWARE);
		/* NOTREACHED */
	}

	if (type != SOURCE_FILE) {
		include = (include_t *)malloc(sizeof(include_t));
		if (include == NULL) {
			stop("Unable to allocate include stack entry",
			     EX_SOFTWARE);
			/* NOTREACHED */
		}
		include->buffer = YY_CURRENT_BUFFER;
		include->lineno = yylineno;
		include->filename = yyfilename;
		SLIST_INSERT_HEAD(&include_stack, include, links);
	}
	yy_switch_to_buffer(yy_create_buffer(newfile, YY_BUF_SIZE));
	yylineno = 1;
	yyfilename = strdup(file_name);
}

static void next_substitution(struct symbol *mac_symbol, const char *body_pos,
			      const char **next_match,
			      struct macro_arg **match_marg, regmatch_t *match);

void
expand_macro(struct symbol *macro_symbol)
{
	struct macro_arg *marg;
	struct macro_arg *match_marg;
	const char *body_head;
	const char *body_pos;
	const char *next_match;

	/*
	 * Due to the nature of unput, we must work
	 * backwards through the macro body performing
	 * any expansions.
	 */
	body_head = macro_symbol->info.macroinfo->body;
	body_pos = body_head + strlen(body_head);
	while (body_pos > body_head) {
		regmatch_t match;

		next_match = body_head;
		match_marg = NULL;
		next_substitution(macro_symbol, body_pos, &next_match,
				  &match_marg, &match);

		/* Put back everything up until the replacement. */
		while (body_pos > next_match)
			unput(*--body_pos);

		/* Perform the replacement. */
		if (match_marg != NULL) {
			const char *strp;

			next_match = match_marg->replacement_text;
			strp = next_match + strlen(next_match);
			while (strp > next_match)
				unput(*--strp);

			/* Skip past the unexpanded macro arg. */
			body_pos -= match.rm_eo - match.rm_so;
		}
	}

	/* Cleanup replacement text. */
	STAILQ_FOREACH(marg, &macro_symbol->info.macroinfo->args, links) {
		free(marg->replacement_text);
	}
}

/*
 * Find the next substitution in the macro working backwards from
 * body_pos until the beginning of the macro buffer.  next_match
 * should be initialized to the beginning of the macro buffer prior
 * to calling this routine.
 */
static void
next_substitution(struct symbol *mac_symbol, const char *body_pos,
		  const char **next_match, struct macro_arg **match_marg,
		  regmatch_t *match)
{
	regmatch_t	  matches[2];
	struct macro_arg *marg;
	const char	 *search_pos;
	int		  retval;

	do {
		search_pos = *next_match;

		STAILQ_FOREACH(marg, &mac_symbol->info.macroinfo->args, links) {

			retval = regexec(&marg->arg_regex, search_pos, 2,
					 matches, 0);
			if (retval == 0
			 && (matches[1].rm_eo + search_pos) <= body_pos
			 && (matches[1].rm_eo + search_pos) > *next_match) {
				*match = matches[1];
				*next_match = match->rm_eo + search_pos;
				*match_marg = marg;
			}
		}
	} while (search_pos != *next_match);
}

int
yywrap()
{
	include_t *include;

	yy_delete_buffer(YY_CURRENT_BUFFER);
	(void)fclose(yyin);
	if (yyfilename != NULL)
		free(yyfilename);
	yyfilename = NULL;
	include = include_stack.slh_first;
	if (include != NULL) {
		yy_switch_to_buffer(include->buffer);
		yylineno = include->lineno;
		yyfilename = include->filename;
		SLIST_REMOVE_HEAD(&include_stack, links);
		free(include);
		return (0);
	}
	return (1);
}
