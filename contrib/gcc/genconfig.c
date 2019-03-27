/* Generate from machine description:
   - some #define configuration flags.
   Copyright (C) 1987, 1991, 1997, 1998, 1999, 2000, 2003, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "gensupport.h"


/* flags to determine output of machine description dependent #define's.  */
static int max_recog_operands;  /* Largest operand number seen.  */
static int max_dup_operands;    /* Largest number of match_dup in any insn.  */
static int max_clobbers_per_insn;
static int have_cc0_flag;
static int have_cmove_flag;
static int have_cond_exec_flag;
static int have_lo_sum_flag;
static int have_peephole_flag;
static int have_peephole2_flag;

/* Maximum number of insns seen in a split.  */
static int max_insns_per_split = 1;

/* Maximum number of input insns for peephole2.  */
static int max_insns_per_peep2;

static int clobbers_seen_this_insn;
static int dup_operands_seen_this_insn;

static void walk_insn_part (rtx, int, int);
static void gen_insn (rtx);
static void gen_expand (rtx);
static void gen_split (rtx);
static void gen_peephole (rtx);
static void gen_peephole2 (rtx);

/* RECOG_P will be nonzero if this pattern was seen in a context where it will
   be used to recognize, rather than just generate an insn. 

   NON_PC_SET_SRC will be nonzero if this pattern was seen in a SET_SRC
   of a SET whose destination is not (pc).  */

static void
walk_insn_part (rtx part, int recog_p, int non_pc_set_src)
{
  int i, j;
  RTX_CODE code;
  const char *format_ptr;

  if (part == 0)
    return;

  code = GET_CODE (part);
  switch (code)
    {
    case CLOBBER:
      clobbers_seen_this_insn++;
      break;

    case MATCH_OPERAND:
      if (XINT (part, 0) > max_recog_operands)
	max_recog_operands = XINT (part, 0);
      return;

    case MATCH_OP_DUP:
    case MATCH_PAR_DUP:
      ++dup_operands_seen_this_insn;
    case MATCH_SCRATCH:
    case MATCH_PARALLEL:
    case MATCH_OPERATOR:
      if (XINT (part, 0) > max_recog_operands)
	max_recog_operands = XINT (part, 0);
      /* Now scan the rtl's in the vector inside the MATCH_OPERATOR or
	 MATCH_PARALLEL.  */
      break;

    case LABEL_REF:
      if (GET_CODE (XEXP (part, 0)) == MATCH_OPERAND
	  || GET_CODE (XEXP (part, 0)) == MATCH_DUP)
	break;
      return;

    case MATCH_DUP:
      ++dup_operands_seen_this_insn;
      if (XINT (part, 0) > max_recog_operands)
	max_recog_operands = XINT (part, 0);
      return;

    case CC0:
      if (recog_p)
	have_cc0_flag = 1;
      return;

    case LO_SUM:
      if (recog_p)
	have_lo_sum_flag = 1;
      return;

    case SET:
      walk_insn_part (SET_DEST (part), 0, recog_p);
      walk_insn_part (SET_SRC (part), recog_p,
		      GET_CODE (SET_DEST (part)) != PC);
      return;

    case IF_THEN_ELSE:
      /* Only consider this machine as having a conditional move if the
	 two arms of the IF_THEN_ELSE are both MATCH_OPERAND.  Otherwise,
	 we have some specific IF_THEN_ELSE construct (like the doz
	 instruction on the RS/6000) that can't be used in the general
	 context we want it for.  */

      if (recog_p && non_pc_set_src
	  && GET_CODE (XEXP (part, 1)) == MATCH_OPERAND
	  && GET_CODE (XEXP (part, 2)) == MATCH_OPERAND)
	have_cmove_flag = 1;
      break;

    case COND_EXEC:
      if (recog_p)
	have_cond_exec_flag = 1;
      break;

    case REG: case CONST_INT: case SYMBOL_REF:
    case PC:
      return;

    default:
      break;
    }

  format_ptr = GET_RTX_FORMAT (GET_CODE (part));

  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (part)); i++)
    switch (*format_ptr++)
      {
      case 'e':
      case 'u':
	walk_insn_part (XEXP (part, i), recog_p, non_pc_set_src);
	break;
      case 'E':
	if (XVEC (part, i) != NULL)
	  for (j = 0; j < XVECLEN (part, i); j++)
	    walk_insn_part (XVECEXP (part, i, j), recog_p, non_pc_set_src);
	break;
      }
}

static void
gen_insn (rtx insn)
{
  int i;

  /* Walk the insn pattern to gather the #define's status.  */
  clobbers_seen_this_insn = 0;
  dup_operands_seen_this_insn = 0;
  if (XVEC (insn, 1) != 0)
    for (i = 0; i < XVECLEN (insn, 1); i++)
      walk_insn_part (XVECEXP (insn, 1, i), 1, 0);

  if (clobbers_seen_this_insn > max_clobbers_per_insn)
    max_clobbers_per_insn = clobbers_seen_this_insn;
  if (dup_operands_seen_this_insn > max_dup_operands)
    max_dup_operands = dup_operands_seen_this_insn;
}

/* Similar but scan a define_expand.  */

static void
gen_expand (rtx insn)
{
  int i;

  /* Walk the insn pattern to gather the #define's status.  */

  /* Note that we don't bother recording the number of MATCH_DUPs
     that occur in a gen_expand, because only reload cares about that.  */
  if (XVEC (insn, 1) != 0)
    for (i = 0; i < XVECLEN (insn, 1); i++)
      {
	/* Compute the maximum SETs and CLOBBERS
	   in any one of the sub-insns;
	   don't sum across all of them.  */
	clobbers_seen_this_insn = 0;

	walk_insn_part (XVECEXP (insn, 1, i), 0, 0);

	if (clobbers_seen_this_insn > max_clobbers_per_insn)
	  max_clobbers_per_insn = clobbers_seen_this_insn;
      }
}

/* Similar but scan a define_split.  */

static void
gen_split (rtx split)
{
  int i;

  /* Look through the patterns that are matched
     to compute the maximum operand number.  */
  for (i = 0; i < XVECLEN (split, 0); i++)
    walk_insn_part (XVECEXP (split, 0, i), 1, 0);
  /* Look at the number of insns this insn could split into.  */
  if (XVECLEN (split, 2) > max_insns_per_split)
    max_insns_per_split = XVECLEN (split, 2);
}

static void
gen_peephole (rtx peep)
{
  int i;

  /* Look through the patterns that are matched
     to compute the maximum operand number.  */
  for (i = 0; i < XVECLEN (peep, 0); i++)
    walk_insn_part (XVECEXP (peep, 0, i), 1, 0);
}

static void
gen_peephole2 (rtx peep)
{
  int i, n;

  /* Look through the patterns that are matched
     to compute the maximum operand number.  */
  for (i = XVECLEN (peep, 0) - 1; i >= 0; --i)
    walk_insn_part (XVECEXP (peep, 0, i), 1, 0);

  /* Look at the number of insns this insn can be matched from.  */
  for (i = XVECLEN (peep, 0) - 1, n = 0; i >= 0; --i)
    if (GET_CODE (XVECEXP (peep, 0, i)) != MATCH_DUP
	&& GET_CODE (XVECEXP (peep, 0, i)) != MATCH_SCRATCH)
      n++;
  if (n > max_insns_per_peep2)
    max_insns_per_peep2 = n;
}

int
main (int argc, char **argv)
{
  rtx desc;

  progname = "genconfig";

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  puts ("/* Generated automatically by the program `genconfig'");
  puts ("   from the machine description file `md'.  */\n");
  puts ("#ifndef GCC_INSN_CONFIG_H");
  puts ("#define GCC_INSN_CONFIG_H\n");

  /* Allow at least 30 operands for the sake of asm constructs.  */
  /* ??? We *really* ought to reorganize things such that there
     is no fixed upper bound.  */
  max_recog_operands = 29;  /* We will add 1 later.  */
  max_dup_operands = 1;

  /* Read the machine description.  */

  while (1)
    {
      int line_no, insn_code_number = 0;

      desc = read_md_rtx (&line_no, &insn_code_number);
      if (desc == NULL)
	break;
	
      switch (GET_CODE (desc)) 
	{
  	  case DEFINE_INSN:
	    gen_insn (desc);
	    break;
	  
	  case DEFINE_EXPAND:
	    gen_expand (desc);
	    break;

	  case DEFINE_SPLIT:
	    gen_split (desc);
	    break;

	  case DEFINE_PEEPHOLE2:
	    have_peephole2_flag = 1;
	    gen_peephole2 (desc);
	    break;

	  case DEFINE_PEEPHOLE:
	    have_peephole_flag = 1;
	    gen_peephole (desc);
	    break;

	  default:
	    break;
	}
    }

  printf ("#define MAX_RECOG_OPERANDS %d\n", max_recog_operands + 1);
  printf ("#define MAX_DUP_OPERANDS %d\n", max_dup_operands);

  /* This is conditionally defined, in case the user writes code which emits
     more splits than we can readily see (and knows s/he does it).  */
  printf ("#ifndef MAX_INSNS_PER_SPLIT\n");
  printf ("#define MAX_INSNS_PER_SPLIT %d\n", max_insns_per_split);
  printf ("#endif\n");

  if (have_cc0_flag)
    {
      printf ("#define HAVE_cc0 1\n");
      printf ("#define CC0_P(X) ((X) == cc0_rtx)\n");
    }
  else
    {
      /* We output CC0_P this way to make sure that X is declared
	 somewhere.  */
      printf ("#define CC0_P(X) ((X) ? 0 : 0)\n");
    }

  if (have_cmove_flag)
    printf ("#define HAVE_conditional_move 1\n");

  if (have_cond_exec_flag)
    printf ("#define HAVE_conditional_execution 1\n");

  if (have_lo_sum_flag)
    printf ("#define HAVE_lo_sum 1\n");

  if (have_peephole_flag)
    printf ("#define HAVE_peephole 1\n");

  if (have_peephole2_flag)
    {
      printf ("#define HAVE_peephole2 1\n");
      printf ("#define MAX_INSNS_PER_PEEP2 %d\n", max_insns_per_peep2);
    }

  puts("\n#endif /* GCC_INSN_CONFIG_H */");

  if (ferror (stdout) || fflush (stdout) || fclose (stdout))
    return FATAL_EXIT_CODE;

  return SUCCESS_EXIT_CODE;
}
