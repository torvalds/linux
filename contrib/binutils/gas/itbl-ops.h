/* itbl-ops.h
   Copyright 1997, 1999, 2000, 2003, 2006, 2007 Free Software Foundation, Inc.

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

/* External functions, constants and defines for itbl support */

#ifdef HAVE_ITBL_CPU
#include "itbl-cpu.h"
#endif

/* Defaults for definitions required by generic code */
#ifndef ITBL_NUMBER_OF_PROCESSORS
#define ITBL_NUMBER_OF_PROCESSORS 1
#endif

#ifndef ITBL_MAX_BITPOS
#define ITBL_MAX_BITPOS 31
#endif

#ifndef ITBL_TYPE
#define ITBL_TYPE unsigned long
#endif

#ifndef ITBL_IS_INSN
#define ITBL_IS_INSN(insn) 1
#endif

#ifndef ITBL_DECODE_PNUM
#define ITBL_DECODE_PNUM(insn) 0
#endif

#ifndef ITBL_ENCODE_PNUM
#define ITBL_ENCODE_PNUM(pnum) 0
#endif

typedef ITBL_TYPE t_insn;

/* types of entries */
typedef enum
  {
    e_insn,
    e_dreg,
    e_regtype0 = e_dreg,
    e_creg,
    e_greg,
    e_addr,
    e_nregtypes = e_greg + 1,
    e_immed,
    e_ntypes,
    e_invtype			/* invalid type */
  } e_type;

typedef enum
  {
    e_p0,
    e_nprocs = ITBL_NUMBER_OF_PROCESSORS,
    e_invproc			/* invalid processor */
  } e_processor;

/* 0 means an instruction table was not specified.  */
extern int itbl_have_entries;

/* These routines are visible to the main part of the assembler */

int itbl_parse (char *insntbl);
void itbl_init (void);
char *itbl_get_field (char **s);
unsigned long itbl_assemble (char *name, char *operands);
int itbl_disassemble (char *str, unsigned long insn);
int itbl_parse (char *tbl);	/* parses insn tbl */
int itbl_get_reg_val (char *name, unsigned long *pval);
int itbl_get_val (e_processor processor, e_type type, char *name,
		  unsigned long *pval);
char *itbl_get_name (e_processor processor, e_type type, unsigned long val);

/* These routines are called by the table parser used to build the
   dynamic list of new processor instructions and registers.  */

struct itbl_entry *itbl_add_reg (int yyproc, int yytype,
				 char *regname, int regnum);
struct itbl_entry *itbl_add_insn (int yyproc, char *name,
	     unsigned long value, int sbit, int ebit, unsigned long flags);
struct itbl_field *itbl_add_operand (struct itbl_entry * e, int yytype,
				  int sbit, int ebit, unsigned long flags);
