%{
/* Copyright 2001, 2003, 2005 Free Software Foundation, Inc.

This file is part of GNU Binutils.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GLD; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#include "sysinfo.h"

#define YY_NO_UNPUT

#ifndef yywrap
static int yywrap (void) { return 1; }
#endif

extern int yylex (void);
%}
%%
"(" { return '(';}
")" { return ')';}
"[" { return '[';}
"]" { return ']';}
" " { ; }
";".* { ; } 
"\t" { ; }
"\n" { ; }
"\""[^\"]*"\"" {
	yylval.s = malloc (yyleng - 1);
	memcpy (yylval.s, yytext + 1, yyleng - 2);
	yylval.s[yyleng - 2] = '\0';
        return NAME;
	}

0x[0-9a-f]+ {
        yylval.i = strtol(yytext,0,16);
	return  NUMBER;
	}

[0-9]+ {
        yylval.i = atoi(yytext);
	return  NUMBER;
	}


"bits" { yylval.i =1 ;return UNIT;}
"bit" { yylval.i = 1; return UNIT;}
"bytes" { yylval.i= 8; return UNIT;}
"byte" { yylval.i = 8; return UNIT;}

"int" { yylval.s = "INT"; return TYPE;}
"barray" { yylval.s = "BARRAY"; return TYPE;}
"chars" { yylval.s = "CHARS"; return TYPE;}
"variable" { yylval.i = 0; return NUMBER;}
"counted" { yylval.i = -4; return NUMBER;}
"addrsize" { yylval.i = -2; return NUMBER; }
"segsize" { yylval.i = -1; return NUMBER; }
"cond" { return COND;}
"repeat" { return REPEAT;}
