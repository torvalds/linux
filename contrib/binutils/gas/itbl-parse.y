/* itbl-parse.y
   Copyright 1997, 2002, 2003, 2005, 2006, 2007 Free Software Foundation, Inc.

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

/* 

Yacc grammar for instruction table entries.

=======================================================================
Original Instruction table specification document:

	    MIPS Coprocessor Table Specification
	    ====================================

This document describes the format of the MIPS coprocessor table.  The
table specifies a list of valid functions, data registers and control
registers that can be used in coprocessor instructions.  This list,
together with the coprocessor instruction classes listed below,
specifies the complete list of coprocessor instructions that will
be recognized and assembled by the GNU assembler.  In effect,
this makes the GNU assembler table-driven, where the table is
specified by the programmer.

The table is an ordinary text file that the GNU assembler reads when
it starts.  Using the information in the table, the assembler
generates an internal list of valid coprocessor registers and
functions.  The assembler uses this internal list in addition to the
standard MIPS registers and instructions which are built-in to the 
assembler during code generation.

To specify the coprocessor table when invoking the GNU assembler, use
the command line option "--itbl file", where file is the
complete name of the table, including path and extension.

Examples:

	    gas -t cop.tbl test.s -o test.o
	    gas -t /usr/local/lib/cop.tbl test.s -o test.o
	    gas --itbl d:\gnu\data\cop.tbl test.s -o test.o

Only one table may be supplied during a single invocation of
the assembler.


Instruction classes
===================

Below is a list of the valid coprocessor instruction classes for
any given coprocessor "z".  These instructions are already recognized
by the assembler, and are listed here only for reference.

Class   format	    	    	      instructions
-------------------------------------------------
Class1:
	op base rt offset
	    	    	    	    	    	    	    LWCz rt,offset (base)
	    	    	    	    	    	    	    SWCz rt,offset (base)
Class2:
	COPz sub rt rd 0
	    	    	    	    	    	    	    MTCz rt,rd
	    	    	    	    	    	    	    MFCz rt,rd
	    	    	    	    	    	    	    CTCz rt,rd
	    	    	    	    	    	    	    CFCz rt,rd
Class3:
	COPz CO cofun
	    	    	    	    	    	    	    COPz cofun
Class4:
	COPz BC br offset
	    	    	    	    	    	    	    BCzT offset
	    	    	    	    	    	    	    BCzF offset
Class5:
	COPz sub rt rd 0
	    	    	    	    	    	    	    DMFCz rt,rd
	    	    	    	    	    	    	    DMTCz rt,rd
Class6:
	op base rt offset
	    	    	    	    	    	    	    LDCz rt,offset (base)
	    	    	    	    	    	    	    SDCz rt,offset (base)
Class7:
	COPz BC br offset
	    	    	    	    	    	    	    BCzTL offset
	    	    	    	    	    	    	    BCzFL offset

The coprocessor table defines coprocessor-specific registers that can
be used with all of the above classes of instructions, where
appropriate.  It also defines additional coprocessor-specific
functions for Class3 (COPz cofun) instructions, Thus, the table allows
the programmer to use convenient mnemonics and operands for these
functions, instead of the COPz mmenmonic and cofun operand.

The names of the MIPS general registers and their aliases are defined
by the assembler and will be recognized as valid register names by the
assembler when used (where allowed) in coprocessor instructions.
However, the names and values of all coprocessor data and control
register mnemonics must be specified in the coprocessor table.


Table Grammar
=============

Here is the grammar for the coprocessor table:

	    table -> entry*

	    entry -> [z entrydef] [comment] '\n'

	    entrydef -> type name val
	    entrydef -> 'insn' name val funcdef ; type of entry (instruction)

	    z -> 'p'['0'..'3']	    	     ; processor number 
	    type -> ['dreg' | 'creg' | 'greg' ]	     ; type of entry (register)
	; 'dreg', 'creg' or 'greg' specifies a data, control, or general
	;	    register mnemonic, respectively
	    name -> [ltr|dec]*	    	     ; mnemonic of register/function
	    val -> [dec|hex]	    	     ; register/function number (integer constant)

	    funcdef -> frange flags fields
	    	    	    	; bitfield range for opcode
	    	    	    	; list of fields' formats
	    fields -> field*
	    field -> [','] ftype frange flags
	    flags -> ['*' flagexpr]
	    flagexpr -> '[' flagexpr ']'
	    flagexpr -> val '|' flagexpr 
	    ftype -> [ type | 'immed' | 'addr' ]
	; 'immed' specifies an immediate value; see grammar for "val" above
	    	; 'addr' specifies a C identifier; name of symbol to be resolved at 
	;	    link time
	    frange -> ':' val '-' val	; starting to ending bit positions, where
	    	    	    	; where 0 is least significant bit
	    frange -> (null)	    	; default range of 31-0 will be assumed

	    comment -> [';'|'#'] [char]*
	    char -> any printable character
	    ltr -> ['a'..'z'|'A'..'Z'] 
	    dec -> ['0'..'9']*	    	    	    	    	     ; value in decimal
	    hex -> '0x'['0'..'9' | 'a'..'f' | 'A'..'F']*	; value in hexadecimal 


Examples
========

Example 1:

The table:

	    p1 dreg d1 1	     ; data register "d1" for COP1 has value 1
	    p1 creg c3 3	     ; ctrl register "c3" for COP1 has value 3
	    p3 func fill 0x1f:24-20	      ; function "fill" for COP3 has value 31 and 
	    	    	; no fields

will allow the assembler to accept the following coprocessor instructions:

	    LWC1 d1,0x100 ($2)
	    fill

Here, the general purpose register "$2", and instruction "LWC1", are standard 
mnemonics built-in to the MIPS assembler.  


Example 2:

The table:

	    p3 dreg d3 3	     ; data register "d3" for COP3 has value 3
	    p3 creg c2 22	     ; control register "c2" for COP3 has value 22
	    p3 func fee 0x1f:24-20 dreg:17-13 creg:12-8 immed:7-0 
	    	; function "fee" for COP3 has value 31, and 3 fields 
	    	; consisting of a data register, a control register, 
	    	; and an immediate value.

will allow the assembler to accept the following coprocessor instruction:

	    fee d3,c2,0x1

and will emit the object code:

	    31-26  25 24-20 19-18  17-13 12-8  7-0
	    COPz   CO fun	    	      dreg  creg  immed
	    010011 1  11111 00	     00011 10110 00000001 

	    0x4ff07601


Example 3:

The table:

	    p3 dreg d3 3	     ; data register "d3" for COP3 has value 3
	    p3 creg c2 22	     ; control register "c2" for COP3 has value 22
	    p3 func fuu 0x01f00001 dreg:17-13 creg:12-8

will allow the assembler to accept the following coprocessor
instruction:

	    fuu d3,c2

and will emit the object code:

	    31-26  25 24-20 19-18  17-13 12-8  7-0
	    COPz   CO fun	    	      dreg  creg  
	    010011 1  11111 00	     00011 10110 00000001 

	    0x4ff07601

In this way, the programmer can force arbitrary bits of an instruction
to have predefined values.

=======================================================================
Additional notes:

Encoding of ranges:
To handle more than one bit position range within an instruction,
use 0s to mask out the ranges which don't apply.
May decide to modify the syntax to allow commas separate multiple 
ranges within an instruction (range','range).

Changes in grammar:
	The number of parms argument to the function entry
was deleted from the original format such that we now count the fields.

----
FIXME! should really change lexical analyzer 
to recognize 'dreg' etc. in context sensitive way.
Currently function names or mnemonics may be incorrectly parsed as keywords

FIXME! hex is ambiguous with any digit

*/

#include "as.h"
#include "itbl-lex.h"
#include "itbl-ops.h"

/* #define DEBUG */

#ifdef DEBUG
#ifndef DBG_LVL
#define DBG_LVL 1
#endif
#else
#define DBG_LVL 0
#endif

#if DBG_LVL >= 1
#define DBG(x) printf x
#else
#define DBG(x) 
#endif

#if DBG_LVL >= 2
#define DBGL2(x) printf x
#else
#define DBGL2(x) 
#endif

static int sbit, ebit;
static struct itbl_entry *insn=0;
static int yyerror (const char *);

%}

%union 
  {
    char *str;
    int num;
    int processor;
    unsigned long val;
  }

%token	    DREG CREG GREG IMMED ADDR INSN NUM ID NL PNUM
%type	    <val> value flags flagexpr
%type	    <num> number NUM ftype regtype pnum PNUM
%type	    <str> ID name

%start insntbl

%%

insntbl:
	entrys
	;

entrys:
	entry entrys
	|
	;

entry:
	pnum regtype name value NL
	  {
	    DBG (("line %d: entry pnum=%d type=%d name=%s value=x%x\n", 
	    	    insntbl_line, $1, $2, $3, $4));
	    itbl_add_reg ($1, $2, $3, $4);
	  }
	| pnum INSN name value range flags
	  {
	    DBG (("line %d: entry pnum=%d type=INSN name=%s value=x%x",
	    	    insntbl_line, $1, $3, $4));
	    DBG ((" sbit=%d ebit=%d flags=0x%x\n", sbit, ebit, $6));
	    insn=itbl_add_insn ($1, $3, $4, sbit, ebit, $6);
	  }
	fieldspecs NL
	  {}
	| NL
	| error NL
	;

fieldspecs:
	',' fieldspec fieldspecs
	| fieldspec fieldspecs
	|
	;

ftype:
	regtype 
	  {
	    DBGL2 (("ftype\n"));
	    $$ = $1;
	  }
	| ADDR 
	  {
	    DBGL2 (("addr\n"));
	    $$ = ADDR;
	  }
	| IMMED
	  {
	    DBGL2 (("immed\n"));
	    $$ = IMMED;
	  }
	;

fieldspec:
	ftype range flags
	  {
	    DBG (("line %d: field type=%d sbit=%d ebit=%d, flags=0x%x\n", 
	    	    insntbl_line, $1, sbit, ebit, $3));
	    itbl_add_operand (insn, $1, sbit, ebit, $3);
	  }
	;

flagexpr:
	NUM '|' flagexpr
	  {
	    $$ = $1 | $3;
	  }
	| '[' flagexpr ']'
	  {
	    $$ = $2;
	  }
	| NUM
	  {
	    $$ = $1;
	  }
	;

flags:
	'*' flagexpr
	  {
	    DBGL2 (("flags=%d\n", $2));
	    $$ = $2;
	  }
	| 
	  {
	    $$ = 0;
	  }
	;

range:
	':' NUM '-' NUM
	  {
	    DBGL2 (("range %d %d\n", $2, $4));
	    sbit = $2;
	    ebit = $4;
	  }
	|
	  {
	    sbit = 31;
	    ebit = 0;
	  }
	;
	    
pnum:
	PNUM
	  {
	    DBGL2 (("pnum=%d\n",$1));
	    $$ = $1;
	  }
	;

regtype:
	     DREG
	  {
	    DBGL2 (("dreg\n"));
	    $$ = DREG;
	  }
	| CREG
	  {
	    DBGL2 (("creg\n"));
	    $$ = CREG;
	  }
	| GREG
	  {
	    DBGL2 (("greg\n"));
	    $$ = GREG;
	  }
	;

name:
	ID
	  {
	    DBGL2 (("name=%s\n",$1));
	    $$ = $1; 
	  }
	;

number:
	NUM
	  {
	    DBGL2 (("num=%d\n",$1));
	    $$ = $1;
	  }
	;

value:
	NUM
	  {
	    DBGL2 (("val=x%x\n",$1));
	    $$ = $1;
	  }
	;
%%

static int
yyerror (msg)
     const char *msg;
{
  printf ("line %d: %s\n", insntbl_line, msg);
  return 0;
}
