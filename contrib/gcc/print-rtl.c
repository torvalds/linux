/* Print RTL for GCC.
   Copyright (C) 1987, 1988, 1992, 1997, 1998, 1999, 2000, 2002, 2003,
   2004, 2005
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

/* This file is compiled twice: once for the generator programs,
   once for the compiler.  */
#ifdef GENERATOR_FILE
#include "bconfig.h"
#else
#include "config.h"
#endif

#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"

/* These headers all define things which are not available in
   generator programs.  */
#ifndef GENERATOR_FILE
#include "tree.h"
#include "real.h"
#include "flags.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#endif

static FILE *outfile;

static int sawclose = 0;

static int indent;

static void print_rtx (rtx);

/* String printed at beginning of each RTL when it is dumped.
   This string is set to ASM_COMMENT_START when the RTL is dumped in
   the assembly output file.  */
const char *print_rtx_head = "";

/* Nonzero means suppress output of instruction numbers and line number
   notes in debugging dumps.
   This must be defined here so that programs like gencodes can be linked.  */
int flag_dump_unnumbered = 0;

/* Nonzero means use simplified format without flags, modes, etc.  */
int flag_simple = 0;

/* Nonzero if we are dumping graphical description.  */
int dump_for_graph;

#ifndef GENERATOR_FILE
static void
print_decl_name (FILE *outfile, tree node)
{
  if (DECL_NAME (node))
    fputs (IDENTIFIER_POINTER (DECL_NAME (node)), outfile);
  else
    {
      if (TREE_CODE (node) == LABEL_DECL && LABEL_DECL_UID (node) != -1)
	fprintf (outfile, "L." HOST_WIDE_INT_PRINT_DEC, LABEL_DECL_UID (node));
      else
        {
          char c = TREE_CODE (node) == CONST_DECL ? 'C' : 'D';
	  fprintf (outfile, "%c.%u", c, DECL_UID (node));
        }
    }
}

void
print_mem_expr (FILE *outfile, tree expr)
{
  if (TREE_CODE (expr) == COMPONENT_REF)
    {
      if (TREE_OPERAND (expr, 0))
	print_mem_expr (outfile, TREE_OPERAND (expr, 0));
      else
	fputs (" <variable>", outfile);
      fputc ('.', outfile);
      print_decl_name (outfile, TREE_OPERAND (expr, 1));
    }
  else if (TREE_CODE (expr) == INDIRECT_REF)
    {
      fputs (" (*", outfile);
      print_mem_expr (outfile, TREE_OPERAND (expr, 0));
      fputs (")", outfile);
    }
  else if (TREE_CODE (expr) == ALIGN_INDIRECT_REF)
    {
      fputs (" (A*", outfile);
      print_mem_expr (outfile, TREE_OPERAND (expr, 0));
      fputs (")", outfile);
    }
  else if (TREE_CODE (expr) == MISALIGNED_INDIRECT_REF)
    {
      fputs (" (M*", outfile);
      print_mem_expr (outfile, TREE_OPERAND (expr, 0));
      fputs (")", outfile);
    }
  else if (TREE_CODE (expr) == RESULT_DECL)
    fputs (" <result>", outfile);
  else
    {
      fputc (' ', outfile);
      print_decl_name (outfile, expr);
    }
}
#endif

/* Print IN_RTX onto OUTFILE.  This is the recursive part of printing.  */

static void
print_rtx (rtx in_rtx)
{
  int i = 0;
  int j;
  const char *format_ptr;
  int is_insn;

  if (sawclose)
    {
      if (flag_simple)
	fputc (' ', outfile);
      else
	fprintf (outfile, "\n%s%*s", print_rtx_head, indent * 2, "");
      sawclose = 0;
    }

  if (in_rtx == 0)
    {
      fputs ("(nil)", outfile);
      sawclose = 1;
      return;
    }
  else if (GET_CODE (in_rtx) > NUM_RTX_CODE)
    {
       fprintf (outfile, "(??? bad code %d\n)", GET_CODE (in_rtx));
       sawclose = 1;
       return;
    }

  is_insn = INSN_P (in_rtx);

  /* When printing in VCG format we write INSNs, NOTE, LABEL, and BARRIER
     in separate nodes and therefore have to handle them special here.  */
  if (dump_for_graph
      && (is_insn || NOTE_P (in_rtx)
	  || LABEL_P (in_rtx) || BARRIER_P (in_rtx)))
    {
      i = 3;
      indent = 0;
    }
  else
    {
      /* Print name of expression code.  */
      if (flag_simple && GET_CODE (in_rtx) == CONST_INT)
	fputc ('(', outfile);
      else
	fprintf (outfile, "(%s", GET_RTX_NAME (GET_CODE (in_rtx)));

      if (! flag_simple)
	{
	  if (RTX_FLAG (in_rtx, in_struct))
	    fputs ("/s", outfile);

	  if (RTX_FLAG (in_rtx, volatil))
	    fputs ("/v", outfile);

	  if (RTX_FLAG (in_rtx, unchanging))
	    fputs ("/u", outfile);

	  if (RTX_FLAG (in_rtx, frame_related))
	    fputs ("/f", outfile);

	  if (RTX_FLAG (in_rtx, jump))
	    fputs ("/j", outfile);

	  if (RTX_FLAG (in_rtx, call))
	    fputs ("/c", outfile);

	  if (RTX_FLAG (in_rtx, return_val))
	    fputs ("/i", outfile);

	  /* Print REG_NOTE names for EXPR_LIST and INSN_LIST.  */
	  if (GET_CODE (in_rtx) == EXPR_LIST
	      || GET_CODE (in_rtx) == INSN_LIST)
	    fprintf (outfile, ":%s",
		     GET_REG_NOTE_NAME (GET_MODE (in_rtx)));

	  /* For other rtl, print the mode if it's not VOID.  */
	  else if (GET_MODE (in_rtx) != VOIDmode)
	    fprintf (outfile, ":%s", GET_MODE_NAME (GET_MODE (in_rtx)));
	}
    }

#ifndef GENERATOR_FILE
  if (GET_CODE (in_rtx) == CONST_DOUBLE && FLOAT_MODE_P (GET_MODE (in_rtx)))
    i = 5;
#endif

  /* Get the format string and skip the first elements if we have handled
     them already.  */
  format_ptr = GET_RTX_FORMAT (GET_CODE (in_rtx)) + i;
  for (; i < GET_RTX_LENGTH (GET_CODE (in_rtx)); i++)
    switch (*format_ptr++)
      {
	const char *str;

      case 'T':
	str = XTMPL (in_rtx, i);
	goto string;

      case 'S':
      case 's':
	str = XSTR (in_rtx, i);
      string:

	if (str == 0)
	  fputs (dump_for_graph ? " \\\"\\\"" : " \"\"", outfile);
	else
	  {
	    if (dump_for_graph)
	      fprintf (outfile, " (\\\"%s\\\")", str);
	    else
	      fprintf (outfile, " (\"%s\")", str);
	  }
	sawclose = 1;
	break;

	/* 0 indicates a field for internal use that should not be printed.
	   An exception is the third field of a NOTE, where it indicates
	   that the field has several different valid contents.  */
      case '0':
	if (i == 1 && REG_P (in_rtx))
	  {
	    if (REGNO (in_rtx) != ORIGINAL_REGNO (in_rtx))
	      fprintf (outfile, " [%d]", ORIGINAL_REGNO (in_rtx));
	  }
#ifndef GENERATOR_FILE
	else if (i == 1 && GET_CODE (in_rtx) == SYMBOL_REF)
	  {
	    int flags = SYMBOL_REF_FLAGS (in_rtx);
	    if (flags)
	      fprintf (outfile, " [flags 0x%x]", flags);
	  }
	else if (i == 2 && GET_CODE (in_rtx) == SYMBOL_REF)
	  {
	    tree decl = SYMBOL_REF_DECL (in_rtx);
	    if (decl)
	      print_node_brief (outfile, "", decl, 0);
	  }
#endif
	else if (i == 4 && NOTE_P (in_rtx))
	  {
	    switch (NOTE_LINE_NUMBER (in_rtx))
	      {
	      case NOTE_INSN_EH_REGION_BEG:
	      case NOTE_INSN_EH_REGION_END:
		if (flag_dump_unnumbered)
		  fprintf (outfile, " #");
		else
		  fprintf (outfile, " %d", NOTE_EH_HANDLER (in_rtx));
		sawclose = 1;
		break;

	      case NOTE_INSN_BLOCK_BEG:
	      case NOTE_INSN_BLOCK_END:
#ifndef GENERATOR_FILE
		dump_addr (outfile, " ", NOTE_BLOCK (in_rtx));
#endif
		sawclose = 1;
		break;

	      case NOTE_INSN_BASIC_BLOCK:
		{
#ifndef GENERATOR_FILE
		  basic_block bb = NOTE_BASIC_BLOCK (in_rtx);
		  if (bb != 0)
		    fprintf (outfile, " [bb %d]", bb->index);
#endif
		  break;
	        }

	      case NOTE_INSN_EXPECTED_VALUE:
		indent += 2;
		if (!sawclose)
		  fprintf (outfile, " ");
		print_rtx (NOTE_EXPECTED_VALUE (in_rtx));
		indent -= 2;
		break;

	      case NOTE_INSN_DELETED_LABEL:
		{
		  const char *label = NOTE_DELETED_LABEL_NAME (in_rtx);
		  if (label)
		    fprintf (outfile, " (\"%s\")", label);
		  else
		    fprintf (outfile, " \"\"");
		}
		break;

	      case NOTE_INSN_SWITCH_TEXT_SECTIONS:
		{
#ifndef GENERATOR_FILE
		  basic_block bb = NOTE_BASIC_BLOCK (in_rtx);
		  if (bb != 0)
		    fprintf (outfile, " [bb %d]", bb->index);
#endif
		  break;
		}
		
	      case NOTE_INSN_VAR_LOCATION:
#ifndef GENERATOR_FILE
		fprintf (outfile, " (");
		print_mem_expr (outfile, NOTE_VAR_LOCATION_DECL (in_rtx));
		fprintf (outfile, " ");
		print_rtx (NOTE_VAR_LOCATION_LOC (in_rtx));
		fprintf (outfile, ")");
#endif
		break;

	      default:
		{
		  const char * const str = X0STR (in_rtx, i);

		  if (NOTE_LINE_NUMBER (in_rtx) < 0)
		    ;
		  else if (str == 0)
		    fputs (dump_for_graph ? " \\\"\\\"" : " \"\"", outfile);
		  else
		    {
		      if (dump_for_graph)
		        fprintf (outfile, " (\\\"%s\\\")", str);
		      else
		        fprintf (outfile, " (\"%s\")", str);
		    }
		  break;
		}
	      }
	  }
	break;

      case 'e':
      do_e:
	indent += 2;
	if (!sawclose)
	  fprintf (outfile, " ");
	print_rtx (XEXP (in_rtx, i));
	indent -= 2;
	break;

      case 'E':
      case 'V':
	indent += 2;
	if (sawclose)
	  {
	    fprintf (outfile, "\n%s%*s",
		     print_rtx_head, indent * 2, "");
	    sawclose = 0;
	  }
	fputs (" [", outfile);
	if (NULL != XVEC (in_rtx, i))
	  {
	    indent += 2;
	    if (XVECLEN (in_rtx, i))
	      sawclose = 1;

	    for (j = 0; j < XVECLEN (in_rtx, i); j++)
	      print_rtx (XVECEXP (in_rtx, i, j));

	    indent -= 2;
	  }
	if (sawclose)
	  fprintf (outfile, "\n%s%*s", print_rtx_head, indent * 2, "");

	fputs ("]", outfile);
	sawclose = 1;
	indent -= 2;
	break;

      case 'w':
	if (! flag_simple)
	  fprintf (outfile, " ");
	fprintf (outfile, HOST_WIDE_INT_PRINT_DEC, XWINT (in_rtx, i));
	if (! flag_simple)
	  fprintf (outfile, " [" HOST_WIDE_INT_PRINT_HEX "]",
		   XWINT (in_rtx, i));
	break;

      case 'i':
	if (i == 4 && INSN_P (in_rtx))
	  {
#ifndef GENERATOR_FILE
	    /*  Pretty-print insn locators.  Ignore scoping as it is mostly
		redundant with line number information and do not print anything
		when there is no location information available.  */
	    if (INSN_LOCATOR (in_rtx) && insn_file (in_rtx))
	      fprintf(outfile, " %s:%i", insn_file (in_rtx), insn_line (in_rtx));
#endif
	  }
	else if (i == 6 && NOTE_P (in_rtx))
	  {
	    /* This field is only used for NOTE_INSN_DELETED_LABEL, and
	       other times often contains garbage from INSN->NOTE death.  */
	    if (NOTE_LINE_NUMBER (in_rtx) == NOTE_INSN_DELETED_LABEL)
	      fprintf (outfile, " %d",  XINT (in_rtx, i));
	  }
	else
	  {
	    int value = XINT (in_rtx, i);
	    const char *name;

#ifndef GENERATOR_FILE
	    if (REG_P (in_rtx) && value < FIRST_PSEUDO_REGISTER)
	      fprintf (outfile, " %d %s", REGNO (in_rtx),
		       reg_names[REGNO (in_rtx)]);
	    else if (REG_P (in_rtx)
		     && value <= LAST_VIRTUAL_REGISTER)
	      {
		if (value == VIRTUAL_INCOMING_ARGS_REGNUM)
		  fprintf (outfile, " %d virtual-incoming-args", value);
		else if (value == VIRTUAL_STACK_VARS_REGNUM)
		  fprintf (outfile, " %d virtual-stack-vars", value);
		else if (value == VIRTUAL_STACK_DYNAMIC_REGNUM)
		  fprintf (outfile, " %d virtual-stack-dynamic", value);
		else if (value == VIRTUAL_OUTGOING_ARGS_REGNUM)
		  fprintf (outfile, " %d virtual-outgoing-args", value);
		else if (value == VIRTUAL_CFA_REGNUM)
		  fprintf (outfile, " %d virtual-cfa", value);
		else
		  fprintf (outfile, " %d virtual-reg-%d", value,
			   value-FIRST_VIRTUAL_REGISTER);
	      }
	    else
#endif
	      if (flag_dump_unnumbered
		     && (is_insn || NOTE_P (in_rtx)))
	      fputc ('#', outfile);
	    else
	      fprintf (outfile, " %d", value);

#ifndef GENERATOR_FILE
	    if (REG_P (in_rtx) && REG_ATTRS (in_rtx))
	      {
		fputs (" [", outfile);
		if (ORIGINAL_REGNO (in_rtx) != REGNO (in_rtx))
		  fprintf (outfile, "orig:%i", ORIGINAL_REGNO (in_rtx));
		if (REG_EXPR (in_rtx))
		  print_mem_expr (outfile, REG_EXPR (in_rtx));

		if (REG_OFFSET (in_rtx))
		  fprintf (outfile, "+" HOST_WIDE_INT_PRINT_DEC,
			   REG_OFFSET (in_rtx));
		fputs (" ]", outfile);
	      }
#endif

	    if (is_insn && &INSN_CODE (in_rtx) == &XINT (in_rtx, i)
		&& XINT (in_rtx, i) >= 0
		&& (name = get_insn_name (XINT (in_rtx, i))) != NULL)
	      fprintf (outfile, " {%s}", name);
	    sawclose = 0;
	  }
	break;

      /* Print NOTE_INSN names rather than integer codes.  */

      case 'n':
	if (XINT (in_rtx, i) >= (int) NOTE_INSN_BIAS
	    && XINT (in_rtx, i) < (int) NOTE_INSN_MAX)
	  fprintf (outfile, " %s", GET_NOTE_INSN_NAME (XINT (in_rtx, i)));
	else
	  fprintf (outfile, " %d", XINT (in_rtx, i));
	sawclose = 0;
	break;

      case 'u':
	if (XEXP (in_rtx, i) != NULL)
	  {
	    rtx sub = XEXP (in_rtx, i);
	    enum rtx_code subc = GET_CODE (sub);

	    if (GET_CODE (in_rtx) == LABEL_REF)
	      {
		if (subc == NOTE
		    && NOTE_LINE_NUMBER (sub) == NOTE_INSN_DELETED_LABEL)
		  {
		    if (flag_dump_unnumbered)
		      fprintf (outfile, " [# deleted]");
		    else
		      fprintf (outfile, " [%d deleted]", INSN_UID (sub));
		    sawclose = 0;
		    break;
		  }

		if (subc != CODE_LABEL)
		  goto do_e;
	      }

	    if (flag_dump_unnumbered)
	      fputs (" #", outfile);
	    else
	      fprintf (outfile, " %d", INSN_UID (sub));
	  }
	else
	  fputs (" 0", outfile);
	sawclose = 0;
	break;

      case 'b':
#ifndef GENERATOR_FILE
	if (XBITMAP (in_rtx, i) == NULL)
	  fputs (" {null}", outfile);
	else
	  bitmap_print (outfile, XBITMAP (in_rtx, i), " {", "}");
#endif
	sawclose = 0;
	break;

      case 't':
#ifndef GENERATOR_FILE
	dump_addr (outfile, " ", XTREE (in_rtx, i));
#endif
	break;

      case '*':
	fputs (" Unknown", outfile);
	sawclose = 0;
	break;

      case 'B':
#ifndef GENERATOR_FILE
	if (XBBDEF (in_rtx, i))
	  fprintf (outfile, " %i", XBBDEF (in_rtx, i)->index);
#endif
	break;

      default:
	gcc_unreachable ();
      }

  switch (GET_CODE (in_rtx))
    {
#ifndef GENERATOR_FILE
    case MEM:
      fprintf (outfile, " [" HOST_WIDE_INT_PRINT_DEC, MEM_ALIAS_SET (in_rtx));

      if (MEM_EXPR (in_rtx))
	print_mem_expr (outfile, MEM_EXPR (in_rtx));

      if (MEM_OFFSET (in_rtx))
	fprintf (outfile, "+" HOST_WIDE_INT_PRINT_DEC,
		 INTVAL (MEM_OFFSET (in_rtx)));

      if (MEM_SIZE (in_rtx))
	fprintf (outfile, " S" HOST_WIDE_INT_PRINT_DEC,
		 INTVAL (MEM_SIZE (in_rtx)));

      if (MEM_ALIGN (in_rtx) != 1)
	fprintf (outfile, " A%u", MEM_ALIGN (in_rtx));

      fputc (']', outfile);
      break;

    case CONST_DOUBLE:
      if (FLOAT_MODE_P (GET_MODE (in_rtx)))
	{
	  char s[60];

	  real_to_decimal (s, CONST_DOUBLE_REAL_VALUE (in_rtx),
			   sizeof (s), 0, 1);
	  fprintf (outfile, " %s", s);

	  real_to_hexadecimal (s, CONST_DOUBLE_REAL_VALUE (in_rtx),
			       sizeof (s), 0, 1);
	  fprintf (outfile, " [%s]", s);
	}
      break;
#endif

    case CODE_LABEL:
      fprintf (outfile, " [%d uses]", LABEL_NUSES (in_rtx));
      switch (LABEL_KIND (in_rtx))
	{
	  case LABEL_NORMAL: break;
	  case LABEL_STATIC_ENTRY: fputs (" [entry]", outfile); break;
	  case LABEL_GLOBAL_ENTRY: fputs (" [global entry]", outfile); break;
	  case LABEL_WEAK_ENTRY: fputs (" [weak entry]", outfile); break;
	  default: gcc_unreachable ();
	}
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      if (LABEL_ALIGN_LOG (in_rtx) > 0)
	fprintf (outfile, " [log_align %u skip %u]", LABEL_ALIGN_LOG (in_rtx),
		 LABEL_MAX_SKIP (in_rtx));
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      break;

    default:
      break;
    }

  if (dump_for_graph
      && (is_insn || NOTE_P (in_rtx)
	  || LABEL_P (in_rtx) || BARRIER_P (in_rtx)))
    sawclose = 0;
  else
    {
      fputc (')', outfile);
      sawclose = 1;
    }
}

/* Print an rtx on the current line of FILE.  Initially indent IND
   characters.  */

void
print_inline_rtx (FILE *outf, rtx x, int ind)
{
  int oldsaw = sawclose;
  int oldindent = indent;

  sawclose = 0;
  indent = ind;
  outfile = outf;
  print_rtx (x);
  sawclose = oldsaw;
  indent = oldindent;
}

/* Call this function from the debugger to see what X looks like.  */

void
debug_rtx (rtx x)
{
  outfile = stderr;
  sawclose = 0;
  print_rtx (x);
  fprintf (stderr, "\n");
}

/* Count of rtx's to print with debug_rtx_list.
   This global exists because gdb user defined commands have no arguments.  */

int debug_rtx_count = 0;	/* 0 is treated as equivalent to 1 */

/* Call this function to print list from X on.

   N is a count of the rtx's to print. Positive values print from the specified
   rtx on.  Negative values print a window around the rtx.
   EG: -5 prints 2 rtx's on either side (in addition to the specified rtx).  */

void
debug_rtx_list (rtx x, int n)
{
  int i,count;
  rtx insn;

  count = n == 0 ? 1 : n < 0 ? -n : n;

  /* If we are printing a window, back up to the start.  */

  if (n < 0)
    for (i = count / 2; i > 0; i--)
      {
	if (PREV_INSN (x) == 0)
	  break;
	x = PREV_INSN (x);
      }

  for (i = count, insn = x; i > 0 && insn != 0; i--, insn = NEXT_INSN (insn))
    {
      debug_rtx (insn);
      fprintf (stderr, "\n");
    }
}

/* Call this function to print an rtx list from START to END inclusive.  */

void
debug_rtx_range (rtx start, rtx end)
{
  while (1)
    {
      debug_rtx (start);
      fprintf (stderr, "\n");
      if (!start || start == end)
	break;
      start = NEXT_INSN (start);
    }
}

/* Call this function to search an rtx list to find one with insn uid UID,
   and then call debug_rtx_list to print it, using DEBUG_RTX_COUNT.
   The found insn is returned to enable further debugging analysis.  */

rtx
debug_rtx_find (rtx x, int uid)
{
  while (x != 0 && INSN_UID (x) != uid)
    x = NEXT_INSN (x);
  if (x != 0)
    {
      debug_rtx_list (x, debug_rtx_count);
      return x;
    }
  else
    {
      fprintf (stderr, "insn uid %d not found\n", uid);
      return 0;
    }
}

/* External entry point for printing a chain of insns
   starting with RTX_FIRST onto file OUTF.
   A blank line separates insns.

   If RTX_FIRST is not an insn, then it alone is printed, with no newline.  */

void
print_rtl (FILE *outf, rtx rtx_first)
{
  rtx tmp_rtx;

  outfile = outf;
  sawclose = 0;

  if (rtx_first == 0)
    {
      fputs (print_rtx_head, outf);
      fputs ("(nil)\n", outf);
    }
  else
    switch (GET_CODE (rtx_first))
      {
      case INSN:
      case JUMP_INSN:
      case CALL_INSN:
      case NOTE:
      case CODE_LABEL:
      case BARRIER:
	for (tmp_rtx = rtx_first; tmp_rtx != 0; tmp_rtx = NEXT_INSN (tmp_rtx))
	  if (! flag_dump_unnumbered
	      || !NOTE_P (tmp_rtx) || NOTE_LINE_NUMBER (tmp_rtx) < 0)
	    {
	      fputs (print_rtx_head, outfile);
	      print_rtx (tmp_rtx);
	      fprintf (outfile, "\n");
	    }
	break;

      default:
	fputs (print_rtx_head, outfile);
	print_rtx (rtx_first);
      }
}

/* Like print_rtx, except specify a file.  */
/* Return nonzero if we actually printed anything.  */

int
print_rtl_single (FILE *outf, rtx x)
{
  outfile = outf;
  sawclose = 0;
  if (! flag_dump_unnumbered
      || !NOTE_P (x) || NOTE_LINE_NUMBER (x) < 0)
    {
      fputs (print_rtx_head, outfile);
      print_rtx (x);
      putc ('\n', outf);
      return 1;
    }
  return 0;
}


/* Like print_rtl except without all the detail; for example,
   if RTX is a CONST_INT then print in decimal format.  */

void
print_simple_rtl (FILE *outf, rtx x)
{
  flag_simple = 1;
  print_rtl (outf, x);
  flag_simple = 0;
}
