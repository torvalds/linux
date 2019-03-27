/* Generate code from machine description to extract operands from insn as rtl.
   Copyright (C) 1987, 1991, 1992, 1993, 1997, 1998, 1999, 2000, 2003,
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


#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "errors.h"
#include "gensupport.h"
#include "vec.h"
#include "vecprim.h"

/* This structure contains all the information needed to describe one
   set of extractions methods.  Each method may be used by more than
   one pattern if the operands are in the same place.

   The string for each operand describes that path to the operand and
   contains `0' through `9' when going into an expression and `a' through
   `z' when going into a vector.  We assume here that only the first operand
   of an rtl expression is a vector.  genrecog.c makes the same assumption
   (and uses the same representation) and it is currently true.  */

typedef char *locstr;

struct extraction
{
  unsigned int op_count;
  unsigned int dup_count;
  locstr *oplocs;
  locstr *duplocs;
  int *dupnums;
  struct code_ptr *insns;
  struct extraction *next;
};

/* Holds a single insn code that uses an extraction method.  */
struct code_ptr
{
  int insn_code;
  struct code_ptr *next;
};

/* All extractions needed for this machine description.  */
static struct extraction *extractions;

/* All insn codes for old-style peepholes.  */
static struct code_ptr *peepholes;

/* This structure is used by gen_insn and walk_rtx to accumulate the
   data that will be used to produce an extractions structure.  */

DEF_VEC_P(locstr);
DEF_VEC_ALLOC_P(locstr,heap);

struct accum_extract
{
  VEC(locstr,heap) *oplocs;
  VEC(locstr,heap) *duplocs;
  VEC(int,heap)    *dupnums;
  VEC(char,heap)   *pathstr;
};

/* Forward declarations.  */
static void walk_rtx (rtx, struct accum_extract *);

static void
gen_insn (rtx insn, int insn_code_number)
{
  int i;
  unsigned int op_count, dup_count, j;
  struct extraction *p;
  struct code_ptr *link;
  struct accum_extract acc;

  acc.oplocs  = VEC_alloc (locstr,heap, 10);
  acc.duplocs = VEC_alloc (locstr,heap, 10);
  acc.dupnums = VEC_alloc (int,heap,    10);
  acc.pathstr = VEC_alloc (char,heap,   20);

  /* Walk the insn's pattern, remembering at all times the path
     down to the walking point.  */

  if (XVECLEN (insn, 1) == 1)
    walk_rtx (XVECEXP (insn, 1, 0), &acc);
  else
    for (i = XVECLEN (insn, 1) - 1; i >= 0; i--)
      {
	VEC_safe_push (char,heap, acc.pathstr, 'a' + i);
	walk_rtx (XVECEXP (insn, 1, i), &acc);
	VEC_pop (char, acc.pathstr);
      }

  link = XNEW (struct code_ptr);
  link->insn_code = insn_code_number;

  /* See if we find something that already had this extraction method.  */

  op_count = VEC_length (locstr, acc.oplocs);
  dup_count = VEC_length (locstr, acc.duplocs);
  gcc_assert (dup_count == VEC_length (int, acc.dupnums));

  for (p = extractions; p; p = p->next)
    {
      if (p->op_count != op_count || p->dup_count != dup_count)
	continue;

      for (j = 0; j < op_count; j++)
	{
	  char *a = p->oplocs[j];
	  char *b = VEC_index (locstr, acc.oplocs, j);
	  if (a != b && (!a || !b || strcmp (a, b)))
	    break;
	}

      if (j != op_count)
	continue;

      for (j = 0; j < dup_count; j++)
	if (p->dupnums[j] != VEC_index (int, acc.dupnums, j)
	    || strcmp (p->duplocs[j], VEC_index (locstr, acc.duplocs, j)))
	  break;

      if (j != dup_count)
	continue;

      /* This extraction is the same as ours.  Just link us in.  */
      link->next = p->insns;
      p->insns = link;
      goto done;
    }

  /* Otherwise, make a new extraction method.  We stash the arrays
     after the extraction structure in memory.  */

  p = xmalloc (sizeof (struct extraction)
	       + op_count*sizeof (char *)
	       + dup_count*sizeof (char *)
	       + dup_count*sizeof (int));
  p->op_count = op_count;
  p->dup_count = dup_count;
  p->next = extractions;
  extractions = p;
  p->insns = link;
  link->next = 0;

  p->oplocs = (char **)((char *)p + sizeof (struct extraction));
  p->duplocs = p->oplocs + op_count;
  p->dupnums = (int *)(p->duplocs + dup_count);

  memcpy(p->oplocs,  VEC_address(locstr,acc.oplocs),   op_count*sizeof(locstr));
  memcpy(p->duplocs, VEC_address(locstr,acc.duplocs), dup_count*sizeof(locstr));
  memcpy(p->dupnums, VEC_address(int,   acc.dupnums), dup_count*sizeof(int));

 done:
  VEC_free (locstr,heap, acc.oplocs);
  VEC_free (locstr,heap, acc.duplocs);
  VEC_free (int,heap,    acc.dupnums);
  VEC_free (char,heap,   acc.pathstr);
}

/* Helper subroutine of walk_rtx: given a VEC(locstr), an index, and a
   string, insert the string at the index, which should either already
   exist and be NULL, or not yet exist within the vector.  In the latter
   case the vector is enlarged as appropriate.  */
static void
VEC_safe_set_locstr (VEC(locstr,heap) **vp, unsigned int ix, char *str)
{
  if (ix < VEC_length (locstr, *vp))
    {
      gcc_assert (VEC_index (locstr, *vp, ix) == 0);
      VEC_replace (locstr, *vp, ix, str);
    }
  else
    {
      while (ix > VEC_length (locstr, *vp))
	VEC_safe_push (locstr, heap, *vp, 0);
      VEC_safe_push (locstr, heap, *vp, str);
    }
}

/* Another helper subroutine of walk_rtx: given a VEC(char), convert it
   to a NUL-terminated string in malloc memory.  */
static char *
VEC_char_to_string (VEC(char,heap) *v)
{
  size_t n = VEC_length (char, v);
  char *s = XNEWVEC (char, n + 1);
  memcpy (s, VEC_address (char, v), n);
  s[n] = '\0';
  return s;
}

static void
walk_rtx (rtx x, struct accum_extract *acc)
{
  RTX_CODE code;
  int i, len, base;
  const char *fmt;

  if (x == 0)
    return;

  code = GET_CODE (x);
  switch (code)
    {
    case PC:
    case CC0:
    case CONST_INT:
    case SYMBOL_REF:
      return;

    case MATCH_OPERAND:
    case MATCH_SCRATCH:
      VEC_safe_set_locstr (&acc->oplocs, XINT (x, 0),
			   VEC_char_to_string (acc->pathstr));
      break;

    case MATCH_OPERATOR:
    case MATCH_PARALLEL:
      VEC_safe_set_locstr (&acc->oplocs, XINT (x, 0),
			   VEC_char_to_string (acc->pathstr));

      base = (code == MATCH_OPERATOR ? '0' : 'a');
      for (i = XVECLEN (x, 2) - 1; i >= 0; i--)
	{
	  VEC_safe_push (char,heap, acc->pathstr, base + i);
	  walk_rtx (XVECEXP (x, 2, i), acc);
	  VEC_pop (char, acc->pathstr);
        }
      return;

    case MATCH_DUP:
    case MATCH_PAR_DUP:
    case MATCH_OP_DUP:
      VEC_safe_push (locstr,heap, acc->duplocs,
		     VEC_char_to_string (acc->pathstr));
      VEC_safe_push (int,heap, acc->dupnums, XINT (x, 0));

      if (code == MATCH_DUP)
	break;

      base = (code == MATCH_OP_DUP ? '0' : 'a');
      for (i = XVECLEN (x, 1) - 1; i >= 0; i--)
        {
	  VEC_safe_push (char,heap, acc->pathstr, base + i);
	  walk_rtx (XVECEXP (x, 1, i), acc);
	  VEC_pop (char, acc->pathstr);
        }
      return;

    default:
      break;
    }

  fmt = GET_RTX_FORMAT (code);
  len = GET_RTX_LENGTH (code);
  for (i = 0; i < len; i++)
    {
      if (fmt[i] == 'e' || fmt[i] == 'u')
	{
	  VEC_safe_push (char,heap, acc->pathstr, '0' + i);
	  walk_rtx (XEXP (x, i), acc);
	  VEC_pop (char, acc->pathstr);
	}
      else if (fmt[i] == 'E')
	{
	  int j;
	  for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	    {
	      VEC_safe_push (char,heap, acc->pathstr, 'a' + j);
	      walk_rtx (XVECEXP (x, i, j), acc);
	      VEC_pop (char, acc->pathstr);
	    }
	}
    }
}

/* Given a PATH, representing a path down the instruction's
   pattern from the root to a certain point, output code to
   evaluate to the rtx at that point.  */

static void
print_path (const char *path)
{
  int len = strlen (path);
  int i;

  if (len == 0)
    {
      /* Don't emit "pat", since we may try to take the address of it,
	 which isn't what is intended.  */
      fputs ("PATTERN (insn)", stdout);
      return;
    }

  /* We first write out the operations (XEXP or XVECEXP) in reverse
     order, then write "pat", then the indices in forward order.  */

  for (i = len - 1; i >= 0 ; i--)
    {
      if (ISLOWER (path[i]))
	fputs ("XVECEXP (", stdout);
      else if (ISDIGIT (path[i]))
	fputs ("XEXP (", stdout);
      else
	gcc_unreachable ();
    }

  fputs ("pat", stdout);

  for (i = 0; i < len; i++)
    {
      if (ISLOWER (path[i]))
	printf (", 0, %d)", path[i] - 'a');
      else if (ISDIGIT(path[i]))
	printf (", %d)", path[i] - '0');
      else
	gcc_unreachable ();
    }
}

static void
print_header (void)
{
  /* N.B. Code below avoids putting squiggle braces in column 1 inside
     a string, because this confuses some editors' syntax highlighting
     engines.  */

  puts ("\
/* Generated automatically by the program `genextract'\n\
   from the machine description file `md'.  */\n\
\n\
#include \"config.h\"\n\
#include \"system.h\"\n\
#include \"coretypes.h\"\n\
#include \"tm.h\"\n\
#include \"rtl.h\"\n\
#include \"insn-config.h\"\n\
#include \"recog.h\"\n\
#include \"toplev.h\"\n\
\n\
/* This variable is used as the \"location\" of any missing operand\n\
   whose numbers are skipped by a given pattern.  */\n\
static rtx junk ATTRIBUTE_UNUSED;\n");

  puts ("\
void\n\
insn_extract (rtx insn)\n{\n\
  rtx *ro = recog_data.operand;\n\
  rtx **ro_loc = recog_data.operand_loc;\n\
  rtx pat = PATTERN (insn);\n\
  int i ATTRIBUTE_UNUSED; /* only for peepholes */\n\
\n\
#ifdef ENABLE_CHECKING\n\
  memset (ro, 0xab, sizeof (*ro) * MAX_RECOG_OPERANDS);\n\
  memset (ro_loc, 0xab, sizeof (*ro_loc) * MAX_RECOG_OPERANDS);\n\
#endif\n");

  puts ("\
  switch (INSN_CODE (insn))\n\
    {\n\
    default:\n\
      /* Control reaches here if insn_extract has been called with an\n\
         unrecognizable insn (code -1), or an insn whose INSN_CODE\n\
         corresponds to a DEFINE_EXPAND in the machine description;\n\
         either way, a bug.  */\n\
      if (INSN_CODE (insn) < 0)\n\
        fatal_insn (\"unrecognizable insn:\", insn);\n\
      else\n\
        fatal_insn (\"insn with invalid code number:\", insn);\n");
}

int
main (int argc, char **argv)
{
  rtx desc;
  unsigned int i;
  struct extraction *p;
  struct code_ptr *link;
  const char *name;
  int insn_code_number;
  int line_no;

  progname = "genextract";

  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  /* Read the machine description.  */

  while ((desc = read_md_rtx (&line_no, &insn_code_number)) != NULL)
    {
       if (GET_CODE (desc) == DEFINE_INSN)
	 gen_insn (desc, insn_code_number);

      else if (GET_CODE (desc) == DEFINE_PEEPHOLE)
	{
	  struct code_ptr *link = XNEW (struct code_ptr);

	  link->insn_code = insn_code_number;
	  link->next = peepholes;
	  peepholes = link;
	}
    }

  print_header ();

  /* Write out code to handle peepholes and the insn_codes that it should
     be called for.  */
  if (peepholes)
    {
      for (link = peepholes; link; link = link->next)
	printf ("    case %d:\n", link->insn_code);

      /* The vector in the insn says how many operands it has.
	 And all it contains are operands.  In fact, the vector was
	 created just for the sake of this function.  We need to set the
	 location of the operands for sake of simplifications after
	 extraction, like eliminating subregs.  */
      puts ("      for (i = XVECLEN (pat, 0) - 1; i >= 0; i--)\n"
	    "          ro[i] = *(ro_loc[i] = &XVECEXP (pat, 0, i));\n"
	    "      break;\n");
    }

  /* Write out all the ways to extract insn operands.  */
  for (p = extractions; p; p = p->next)
    {
      for (link = p->insns; link; link = link->next)
	{
	  i = link->insn_code;
	  name = get_insn_name (i);
	  if (name)
	    printf ("    case %d:  /* %s */\n", i, name);
	  else
	    printf ("    case %d:\n", i);
	}

      for (i = 0; i < p->op_count; i++)
	{
	  if (p->oplocs[i] == 0)
	    {
	      printf ("      ro[%d] = const0_rtx;\n", i);
	      printf ("      ro_loc[%d] = &junk;\n", i);
	    }
	  else
	    {
	      printf ("      ro[%d] = *(ro_loc[%d] = &", i, i);
	      print_path (p->oplocs[i]);
	      puts (");");
	    }
	}

      for (i = 0; i < p->dup_count; i++)
	{
	  printf ("      recog_data.dup_loc[%d] = &", i);
	  print_path (p->duplocs[i]);
	  puts (";");
	  printf ("      recog_data.dup_num[%d] = %d;\n", i, p->dupnums[i]);
	}

      puts ("      break;\n");
    }

  puts ("    }\n}");
  fflush (stdout);
  return (ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}
