/* Instruction scheduling pass.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com) Enhanced by,
   and currently maintained by, Jim Wilson (wilson@cygnus.com)

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "obstack.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "real.h"
#include "sched-int.h"
#include "tree-pass.h"

static char *safe_concat (char *, char *, const char *);
static void print_exp (char *, rtx, int);
static void print_value (char *, rtx, int);
static void print_pattern (char *, rtx, int);

#define BUF_LEN 2048

static char *
safe_concat (char *buf, char *cur, const char *str)
{
  char *end = buf + BUF_LEN - 2;	/* Leave room for null.  */
  int c;

  if (cur > end)
    {
      *end = '\0';
      return end;
    }

  while (cur < end && (c = *str++) != '\0')
    *cur++ = c;

  *cur = '\0';
  return cur;
}

/* This recognizes rtx, I classified as expressions.  These are always
   represent some action on values or results of other expression, that
   may be stored in objects representing values.  */

static void
print_exp (char *buf, rtx x, int verbose)
{
  char tmp[BUF_LEN];
  const char *st[4];
  char *cur = buf;
  const char *fun = (char *) 0;
  const char *sep;
  rtx op[4];
  int i;

  for (i = 0; i < 4; i++)
    {
      st[i] = (char *) 0;
      op[i] = NULL_RTX;
    }

  switch (GET_CODE (x))
    {
    case PLUS:
      op[0] = XEXP (x, 0);
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) < 0)
	{
	  st[1] = "-";
	  op[1] = GEN_INT (-INTVAL (XEXP (x, 1)));
	}
      else
	{
	  st[1] = "+";
	  op[1] = XEXP (x, 1);
	}
      break;
    case LO_SUM:
      op[0] = XEXP (x, 0);
      st[1] = "+low(";
      op[1] = XEXP (x, 1);
      st[2] = ")";
      break;
    case MINUS:
      op[0] = XEXP (x, 0);
      st[1] = "-";
      op[1] = XEXP (x, 1);
      break;
    case COMPARE:
      fun = "cmp";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case NEG:
      st[0] = "-";
      op[0] = XEXP (x, 0);
      break;
    case MULT:
      op[0] = XEXP (x, 0);
      st[1] = "*";
      op[1] = XEXP (x, 1);
      break;
    case DIV:
      op[0] = XEXP (x, 0);
      st[1] = "/";
      op[1] = XEXP (x, 1);
      break;
    case UDIV:
      fun = "udiv";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case MOD:
      op[0] = XEXP (x, 0);
      st[1] = "%";
      op[1] = XEXP (x, 1);
      break;
    case UMOD:
      fun = "umod";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case SMIN:
      fun = "smin";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case SMAX:
      fun = "smax";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case UMIN:
      fun = "umin";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case UMAX:
      fun = "umax";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case NOT:
      st[0] = "!";
      op[0] = XEXP (x, 0);
      break;
    case AND:
      op[0] = XEXP (x, 0);
      st[1] = "&";
      op[1] = XEXP (x, 1);
      break;
    case IOR:
      op[0] = XEXP (x, 0);
      st[1] = "|";
      op[1] = XEXP (x, 1);
      break;
    case XOR:
      op[0] = XEXP (x, 0);
      st[1] = "^";
      op[1] = XEXP (x, 1);
      break;
    case ASHIFT:
      op[0] = XEXP (x, 0);
      st[1] = "<<";
      op[1] = XEXP (x, 1);
      break;
    case LSHIFTRT:
      op[0] = XEXP (x, 0);
      st[1] = " 0>>";
      op[1] = XEXP (x, 1);
      break;
    case ASHIFTRT:
      op[0] = XEXP (x, 0);
      st[1] = ">>";
      op[1] = XEXP (x, 1);
      break;
    case ROTATE:
      op[0] = XEXP (x, 0);
      st[1] = "<-<";
      op[1] = XEXP (x, 1);
      break;
    case ROTATERT:
      op[0] = XEXP (x, 0);
      st[1] = ">->";
      op[1] = XEXP (x, 1);
      break;
    case ABS:
      fun = "abs";
      op[0] = XEXP (x, 0);
      break;
    case SQRT:
      fun = "sqrt";
      op[0] = XEXP (x, 0);
      break;
    case FFS:
      fun = "ffs";
      op[0] = XEXP (x, 0);
      break;
    case EQ:
      op[0] = XEXP (x, 0);
      st[1] = "==";
      op[1] = XEXP (x, 1);
      break;
    case NE:
      op[0] = XEXP (x, 0);
      st[1] = "!=";
      op[1] = XEXP (x, 1);
      break;
    case GT:
      op[0] = XEXP (x, 0);
      st[1] = ">";
      op[1] = XEXP (x, 1);
      break;
    case GTU:
      fun = "gtu";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case LT:
      op[0] = XEXP (x, 0);
      st[1] = "<";
      op[1] = XEXP (x, 1);
      break;
    case LTU:
      fun = "ltu";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case GE:
      op[0] = XEXP (x, 0);
      st[1] = ">=";
      op[1] = XEXP (x, 1);
      break;
    case GEU:
      fun = "geu";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case LE:
      op[0] = XEXP (x, 0);
      st[1] = "<=";
      op[1] = XEXP (x, 1);
      break;
    case LEU:
      fun = "leu";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      break;
    case SIGN_EXTRACT:
      fun = (verbose) ? "sign_extract" : "sxt";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      op[2] = XEXP (x, 2);
      break;
    case ZERO_EXTRACT:
      fun = (verbose) ? "zero_extract" : "zxt";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      op[2] = XEXP (x, 2);
      break;
    case SIGN_EXTEND:
      fun = (verbose) ? "sign_extend" : "sxn";
      op[0] = XEXP (x, 0);
      break;
    case ZERO_EXTEND:
      fun = (verbose) ? "zero_extend" : "zxn";
      op[0] = XEXP (x, 0);
      break;
    case FLOAT_EXTEND:
      fun = (verbose) ? "float_extend" : "fxn";
      op[0] = XEXP (x, 0);
      break;
    case TRUNCATE:
      fun = (verbose) ? "trunc" : "trn";
      op[0] = XEXP (x, 0);
      break;
    case FLOAT_TRUNCATE:
      fun = (verbose) ? "float_trunc" : "ftr";
      op[0] = XEXP (x, 0);
      break;
    case FLOAT:
      fun = (verbose) ? "float" : "flt";
      op[0] = XEXP (x, 0);
      break;
    case UNSIGNED_FLOAT:
      fun = (verbose) ? "uns_float" : "ufl";
      op[0] = XEXP (x, 0);
      break;
    case FIX:
      fun = "fix";
      op[0] = XEXP (x, 0);
      break;
    case UNSIGNED_FIX:
      fun = (verbose) ? "uns_fix" : "ufx";
      op[0] = XEXP (x, 0);
      break;
    case PRE_DEC:
      st[0] = "--";
      op[0] = XEXP (x, 0);
      break;
    case PRE_INC:
      st[0] = "++";
      op[0] = XEXP (x, 0);
      break;
    case POST_DEC:
      op[0] = XEXP (x, 0);
      st[1] = "--";
      break;
    case POST_INC:
      op[0] = XEXP (x, 0);
      st[1] = "++";
      break;
    case CALL:
      st[0] = "call ";
      op[0] = XEXP (x, 0);
      if (verbose)
	{
	  st[1] = " argc:";
	  op[1] = XEXP (x, 1);
	}
      break;
    case IF_THEN_ELSE:
      st[0] = "{(";
      op[0] = XEXP (x, 0);
      st[1] = ")?";
      op[1] = XEXP (x, 1);
      st[2] = ":";
      op[2] = XEXP (x, 2);
      st[3] = "}";
      break;
    case TRAP_IF:
      fun = "trap_if";
      op[0] = TRAP_CONDITION (x);
      break;
    case PREFETCH:
      fun = "prefetch";
      op[0] = XEXP (x, 0);
      op[1] = XEXP (x, 1);
      op[2] = XEXP (x, 2);
      break;
    case UNSPEC:
    case UNSPEC_VOLATILE:
      {
	cur = safe_concat (buf, cur, "unspec");
	if (GET_CODE (x) == UNSPEC_VOLATILE)
	  cur = safe_concat (buf, cur, "/v");
	cur = safe_concat (buf, cur, "[");
	sep = "";
	for (i = 0; i < XVECLEN (x, 0); i++)
	  {
	    print_pattern (tmp, XVECEXP (x, 0, i), verbose);
	    cur = safe_concat (buf, cur, sep);
	    cur = safe_concat (buf, cur, tmp);
	    sep = ",";
	  }
	cur = safe_concat (buf, cur, "] ");
	sprintf (tmp, "%d", XINT (x, 1));
	cur = safe_concat (buf, cur, tmp);
      }
      break;
    default:
      /* If (verbose) debug_rtx (x);  */
      st[0] = GET_RTX_NAME (GET_CODE (x));
      break;
    }

  /* Print this as a function?  */
  if (fun)
    {
      cur = safe_concat (buf, cur, fun);
      cur = safe_concat (buf, cur, "(");
    }

  for (i = 0; i < 4; i++)
    {
      if (st[i])
	cur = safe_concat (buf, cur, st[i]);

      if (op[i])
	{
	  if (fun && i != 0)
	    cur = safe_concat (buf, cur, ",");

	  print_value (tmp, op[i], verbose);
	  cur = safe_concat (buf, cur, tmp);
	}
    }

  if (fun)
    cur = safe_concat (buf, cur, ")");
}		/* print_exp */

/* Prints rtxes, I customarily classified as values.  They're constants,
   registers, labels, symbols and memory accesses.  */

static void
print_value (char *buf, rtx x, int verbose)
{
  char t[BUF_LEN];
  char *cur = buf;

  switch (GET_CODE (x))
    {
    case CONST_INT:
      sprintf (t, HOST_WIDE_INT_PRINT_HEX, INTVAL (x));
      cur = safe_concat (buf, cur, t);
      break;
    case CONST_DOUBLE:
      if (FLOAT_MODE_P (GET_MODE (x)))
	real_to_decimal (t, CONST_DOUBLE_REAL_VALUE (x), sizeof (t), 0, 1);
      else
	sprintf (t,
		 "<" HOST_WIDE_INT_PRINT_HEX "," HOST_WIDE_INT_PRINT_HEX ">",
		 (unsigned HOST_WIDE_INT) CONST_DOUBLE_LOW (x),
		 (unsigned HOST_WIDE_INT) CONST_DOUBLE_HIGH (x));
      cur = safe_concat (buf, cur, t);
      break;
    case CONST_STRING:
      cur = safe_concat (buf, cur, "\"");
      cur = safe_concat (buf, cur, XSTR (x, 0));
      cur = safe_concat (buf, cur, "\"");
      break;
    case SYMBOL_REF:
      cur = safe_concat (buf, cur, "`");
      cur = safe_concat (buf, cur, XSTR (x, 0));
      cur = safe_concat (buf, cur, "'");
      break;
    case LABEL_REF:
      sprintf (t, "L%d", INSN_UID (XEXP (x, 0)));
      cur = safe_concat (buf, cur, t);
      break;
    case CONST:
      print_value (t, XEXP (x, 0), verbose);
      cur = safe_concat (buf, cur, "const(");
      cur = safe_concat (buf, cur, t);
      cur = safe_concat (buf, cur, ")");
      break;
    case HIGH:
      print_value (t, XEXP (x, 0), verbose);
      cur = safe_concat (buf, cur, "high(");
      cur = safe_concat (buf, cur, t);
      cur = safe_concat (buf, cur, ")");
      break;
    case REG:
      if (REGNO (x) < FIRST_PSEUDO_REGISTER)
	{
	  int c = reg_names[REGNO (x)][0];
	  if (ISDIGIT (c))
	    cur = safe_concat (buf, cur, "%");

	  cur = safe_concat (buf, cur, reg_names[REGNO (x)]);
	}
      else
	{
	  sprintf (t, "r%d", REGNO (x));
	  cur = safe_concat (buf, cur, t);
	}
      if (verbose
#ifdef INSN_SCHEDULING
	  && !current_sched_info
#endif
	 )
	{
	  sprintf (t, ":%s", GET_MODE_NAME (GET_MODE (x)));
	  cur = safe_concat (buf, cur, t);
	}
      break;
    case SUBREG:
      print_value (t, SUBREG_REG (x), verbose);
      cur = safe_concat (buf, cur, t);
      sprintf (t, "#%d", SUBREG_BYTE (x));
      cur = safe_concat (buf, cur, t);
      break;
    case SCRATCH:
      cur = safe_concat (buf, cur, "scratch");
      break;
    case CC0:
      cur = safe_concat (buf, cur, "cc0");
      break;
    case PC:
      cur = safe_concat (buf, cur, "pc");
      break;
    case MEM:
      print_value (t, XEXP (x, 0), verbose);
      cur = safe_concat (buf, cur, "[");
      cur = safe_concat (buf, cur, t);
      cur = safe_concat (buf, cur, "]");
      break;
    default:
      print_exp (t, x, verbose);
      cur = safe_concat (buf, cur, t);
      break;
    }
}				/* print_value */

/* The next step in insn detalization, its pattern recognition.  */

static void
print_pattern (char *buf, rtx x, int verbose)
{
  char t1[BUF_LEN], t2[BUF_LEN], t3[BUF_LEN];

  switch (GET_CODE (x))
    {
    case SET:
      print_value (t1, SET_DEST (x), verbose);
      print_value (t2, SET_SRC (x), verbose);
      sprintf (buf, "%s=%s", t1, t2);
      break;
    case RETURN:
      sprintf (buf, "return");
      break;
    case CALL:
      print_exp (buf, x, verbose);
      break;
    case CLOBBER:
      print_value (t1, XEXP (x, 0), verbose);
      sprintf (buf, "clobber %s", t1);
      break;
    case USE:
      print_value (t1, XEXP (x, 0), verbose);
      sprintf (buf, "use %s", t1);
      break;
    case COND_EXEC:
      if (GET_CODE (COND_EXEC_TEST (x)) == NE
	  && XEXP (COND_EXEC_TEST (x), 1) == const0_rtx)
	print_value (t1, XEXP (COND_EXEC_TEST (x), 0), verbose);
      else if (GET_CODE (COND_EXEC_TEST (x)) == EQ
	       && XEXP (COND_EXEC_TEST (x), 1) == const0_rtx)
	{
	  t1[0] = '!';
	  print_value (t1 + 1, XEXP (COND_EXEC_TEST (x), 0), verbose);
	}
      else
	print_value (t1, COND_EXEC_TEST (x), verbose);
      print_pattern (t2, COND_EXEC_CODE (x), verbose);
      sprintf (buf, "(%s) %s", t1, t2);
      break;
    case PARALLEL:
      {
	int i;

	sprintf (t1, "{");
	for (i = 0; i < XVECLEN (x, 0); i++)
	  {
	    print_pattern (t2, XVECEXP (x, 0, i), verbose);
	    sprintf (t3, "%s%s;", t1, t2);
	    strcpy (t1, t3);
	  }
	sprintf (buf, "%s}", t1);
      }
      break;
    case SEQUENCE:
      /* Should never see SEQUENCE codes until after reorg.  */
      gcc_unreachable ();
    case ASM_INPUT:
      sprintf (buf, "asm {%s}", XSTR (x, 0));
      break;
    case ADDR_VEC:
      break;
    case ADDR_DIFF_VEC:
      print_value (buf, XEXP (x, 0), verbose);
      break;
    case TRAP_IF:
      print_value (t1, TRAP_CONDITION (x), verbose);
      sprintf (buf, "trap_if %s", t1);
      break;
    case UNSPEC:
      {
	int i;

	sprintf (t1, "unspec{");
	for (i = 0; i < XVECLEN (x, 0); i++)
	  {
	    print_pattern (t2, XVECEXP (x, 0, i), verbose);
	    sprintf (t3, "%s%s;", t1, t2);
	    strcpy (t1, t3);
	  }
	sprintf (buf, "%s}", t1);
      }
      break;
    case UNSPEC_VOLATILE:
      {
	int i;

	sprintf (t1, "unspec/v{");
	for (i = 0; i < XVECLEN (x, 0); i++)
	  {
	    print_pattern (t2, XVECEXP (x, 0, i), verbose);
	    sprintf (t3, "%s%s;", t1, t2);
	    strcpy (t1, t3);
	  }
	sprintf (buf, "%s}", t1);
      }
      break;
    default:
      print_value (buf, x, verbose);
    }
}				/* print_pattern */

/* This is the main function in rtl visualization mechanism. It
   accepts an rtx and tries to recognize it as an insn, then prints it
   properly in human readable form, resembling assembler mnemonics.
   For every insn it prints its UID and BB the insn belongs too.
   (Probably the last "option" should be extended somehow, since it
   depends now on sched.c inner variables ...)  */

void
print_insn (char *buf, rtx x, int verbose)
{
  char t[BUF_LEN];
  rtx insn = x;

  switch (GET_CODE (x))
    {
    case INSN:
      print_pattern (t, PATTERN (x), verbose);
#ifdef INSN_SCHEDULING
      if (verbose && current_sched_info)
	sprintf (buf, "%s: %s", (*current_sched_info->print_insn) (x, 1),
		 t);
      else
#endif
	sprintf (buf, " %4d %s", INSN_UID (x), t);
      break;
    case JUMP_INSN:
      print_pattern (t, PATTERN (x), verbose);
#ifdef INSN_SCHEDULING
      if (verbose && current_sched_info)
	sprintf (buf, "%s: jump %s", (*current_sched_info->print_insn) (x, 1),
		 t);
      else
#endif
	sprintf (buf, " %4d %s", INSN_UID (x), t);
      break;
    case CALL_INSN:
      x = PATTERN (insn);
      if (GET_CODE (x) == PARALLEL)
	{
	  x = XVECEXP (x, 0, 0);
	  print_pattern (t, x, verbose);
	}
      else
	strcpy (t, "call <...>");
#ifdef INSN_SCHEDULING
      if (verbose && current_sched_info)
	sprintf (buf, "%s: %s", (*current_sched_info->print_insn) (x, 1), t);
      else
#endif
	sprintf (buf, " %4d %s", INSN_UID (insn), t);
      break;
    case CODE_LABEL:
      sprintf (buf, "L%d:", INSN_UID (x));
      break;
    case BARRIER:
      sprintf (buf, "i%4d: barrier", INSN_UID (x));
      break;
    case NOTE:
      if (NOTE_LINE_NUMBER (x) > 0)
	{
	  expanded_location xloc;
	  NOTE_EXPANDED_LOCATION (xloc, x);
	  sprintf (buf, " %4d note \"%s\" %d", INSN_UID (x),
		   xloc.file, xloc.line);
	}
      else
	sprintf (buf, " %4d %s", INSN_UID (x),
		 GET_NOTE_INSN_NAME (NOTE_LINE_NUMBER (x)));
      break;
    default:
      sprintf (buf, "i%4d  <What %s?>", INSN_UID (x),
	       GET_RTX_NAME (GET_CODE (x)));
    }
}				/* print_insn */


/* Emit a slim dump of X (an insn) to the file F, including any register
   note attached to the instruction.  */
void
dump_insn_slim (FILE *f, rtx x)
{
  char t[BUF_LEN + 32];
  rtx note;

  print_insn (t, x, 1);
  fputs (t, f);
  putc ('\n', f);
  if (INSN_P (x) && REG_NOTES (x))
    for (note = REG_NOTES (x); note; note = XEXP (note, 1))
      {
        print_value (t, XEXP (note, 0), 1);
	fprintf (f, "      %s: %s\n",
		 GET_REG_NOTE_NAME (REG_NOTE_KIND (note)), t);
      }
}

/* Emit a slim dump of X (an insn) to stderr.  */
void
debug_insn_slim (rtx x)
{
  dump_insn_slim (stderr, x);
}

/* Provide a slim dump the instruction chain starting at FIRST to F, honoring
   the dump flags given in FLAGS.  Currently, TDF_BLOCKS and TDF_DETAILS
   include more information on the basic blocks.  */
void
print_rtl_slim_with_bb (FILE *f, rtx first, int flags)
{
  basic_block current_bb = NULL;
  rtx insn;

  for (insn = first; NULL != insn; insn = NEXT_INSN (insn))
    {
      if ((flags & TDF_BLOCKS)
	  && (INSN_P (insn) || GET_CODE (insn) == NOTE)
	  && BLOCK_FOR_INSN (insn)
	  && !current_bb)
	{
	  current_bb = BLOCK_FOR_INSN (insn);
	  dump_bb_info (current_bb, true, false, flags, ";; ", f);
	}

      dump_insn_slim (f, insn);

      if ((flags & TDF_BLOCKS)
	  && current_bb
	  && insn == BB_END (current_bb))
	{
	  dump_bb_info (current_bb, false, true, flags, ";; ", f);
	  current_bb = NULL;
	}
    }
}

