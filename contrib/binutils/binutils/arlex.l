%{
/* arlex.l - Strange script language lexer */

/* Copyright 1992, 1997, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.

This file is part of GNU Binutils.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */


/* Contributed by Steve Chamberlain <sac@cygnus.com>.  */

#define DONTDECLARE_MALLOC
#include "ansidecl.h"
#include "libiberty.h"
#include "arparse.h"

extern int yylex (void);

int linenumber;
%}

%option nounput

%a 10000
%o 25000

%%

"ADDLIB"   	{ return ADDLIB; }
"ADDMOD"   	{ return ADDMOD; }
"CLEAR"   	{ return CLEAR; }
"CREATE"   	{ return CREATE; }
"DELETE"   	{ return DELETE; }
"DIRECTORY"   	{ return DIRECTORY; }
"END"   	{ return END; }
"EXTRACT"   	{ return EXTRACT; }
"FULLDIR"   	{ return FULLDIR; }
"HELP"   	{ return HELP; }
"LIST"		{ return LIST; }
"OPEN"   	{ return OPEN; }
"REPLACE"   	{ return REPLACE; }
"VERBOSE"   	{ return VERBOSE; }
"SAVE"   	{ return SAVE; }
"addlib"   	{ return ADDLIB; }
"addmod"   	{ return ADDMOD; }
"clear"   	{ return CLEAR; }
"create"   	{ return CREATE; }
"delete"   	{ return DELETE; }
"directory"   	{ return DIRECTORY; }
"end"   	{ return END; }
"extract"   	{ return EXTRACT; }
"fulldir"   	{ return FULLDIR; }
"help"   	{ return HELP; }
"list"		{ return LIST; }
"open"   	{ return OPEN; }
"replace"   	{ return REPLACE; }
"verbose"   	{ return VERBOSE; }
"save"   	{ return SAVE; }
"+\n"           { linenumber ++; }
"("             { return '('; }
")"             { return ')'; }
","             { return ','; }
[A-Za-z0-9/\\$:.\-\_]+  { 	
		yylval.name =  xstrdup (yytext);
		return FILENAME;
		}
"*".* 		{ }
";".* 		{ }
" "		{ }
"\n"	 	 { linenumber ++; return NEWLINE; }	

%%
#ifndef yywrap
/* Needed for lex, though not flex. */
int yywrap(void) { return 1; }
#endif
