/* itbl-lex.l
   Copyright 1997, 1998, 2001, 2002, 2005, 2006
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

%{
#include "as.h"
#include "itbl-lex.h"
#include <itbl-parse.h>

#ifdef DEBUG
#define DBG(x) printf x
#define MDBG(x) printf x
#else
#define DBG(x)
#define MDBG(x)
#endif

int insntbl_line = 1;
%}

ALNUM	[A-Za-z0-9_]
DIGIT	[0-9]
ALPHA	[A-Za-z_]
HEX	[0-9A-Fa-f]

%%

"creg"|"CREG" {
    return CREG;
  }
"dreg"|"DREG" {
    return DREG;
  }
"greg"|"GREG" {
    return GREG;
  }
"immed"|"IMMED" {
    return IMMED;
  }
"addr"|"ADDR" {
    return ADDR;
  }
"insn"|"INSN" {
    return INSN;
  }
"p"{DIGIT} {
    yytext[yyleng] = 0;
    yylval.processor = strtoul (yytext+1, 0, 0);
    return PNUM;
  }
{DIGIT}+ {
    yytext[yyleng] = 0;
    yylval.num = strtoul (yytext, 0, 0);
    return NUM;
  }
"0x"{HEX}+ {
    yytext[yyleng] = 0;
    yylval.num = strtoul (yytext, 0, 0);
    return NUM;
  }
{ALPHA}{ALNUM}*	{
    yytext[yyleng] = 0;
    yylval.str = strdup (yytext);
    return ID;
  }
";"|"#"	{
    int c;
    while ((c = input ()) !=  EOF) 
      {
        if (c ==  '\n') 
    	{
    		unput (c);
    		break;
    	}
      }
  }
"\n"	{ 
    insntbl_line++; 
    MDBG (("in lex, NL = %d (x%x)\n", NL, NL));
    return NL; 
  }
" "|"\t" { 
  }
. {
    MDBG (("char = %x, %d\n", yytext[0], yytext[0]));
    return yytext[0];
  }
%%

#ifndef yywrap
int 
yywrap () 
  { 
    return 1; 
  }
#endif
