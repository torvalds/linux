/* cond.c - conditional assembly pseudo-ops, and .include
   Copyright 1990, 1991, 1992, 1993, 1995, 1997, 1998, 2000, 2001, 2002,
   2003, 2006 Free Software Foundation, Inc.

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

#include "as.h"
#include "sb.h"
#include "macro.h"

#include "obstack.h"

/* This is allocated to grow and shrink as .ifdef/.endif pairs are
   scanned.  */
struct obstack cond_obstack;

struct file_line {
  char *file;
  unsigned int line;
};

/* We push one of these structures for each .if, and pop it at the
   .endif.  */

struct conditional_frame {
  /* The source file & line number of the "if".  */
  struct file_line if_file_line;
  /* The source file & line of the "else".  */
  struct file_line else_file_line;
  /* The previous conditional.  */
  struct conditional_frame *previous_cframe;
  /* Have we seen an else yet?  */
  int else_seen;
  /* Whether we are currently ignoring input.  */
  int ignoring;
  /* Whether a conditional at a higher level is ignoring input.
     Set also when a branch of an "if .. elseif .." tree has matched
     to prevent further matches.  */
  int dead_tree;
  /* Macro nesting level at which this conditional was created.  */
  int macro_nest;
};

static void initialize_cframe (struct conditional_frame *cframe);
static char *get_mri_string (int, int *);

static struct conditional_frame *current_cframe = NULL;

/* Performs the .ifdef (test_defined == 1) and
   the .ifndef (test_defined == 0) pseudo op.  */

void
s_ifdef (int test_defined)
{
  /* Points to name of symbol.  */
  char *name;
  /* Points to symbol.  */
  symbolS *symbolP;
  struct conditional_frame cframe;
  char c;

  /* Leading whitespace is part of operand.  */
  SKIP_WHITESPACE ();
  name = input_line_pointer;

  if (!is_name_beginner (*name))
    {
      as_bad (_("invalid identifier for \".ifdef\""));
      obstack_1grow (&cond_obstack, 0);
      ignore_rest_of_line ();
      return;
    }

  c = get_symbol_end ();
  symbolP = symbol_find (name);
  *input_line_pointer = c;

  initialize_cframe (&cframe);
  
  if (cframe.dead_tree)
    cframe.ignoring = 1;
  else
    {
      int is_defined;

      /* Use the same definition of 'defined' as .equiv so that a symbol
	 which has been referenced but not yet given a value/address is
	 considered to be undefined.  */
      is_defined =
	symbolP != NULL
	&& (S_IS_DEFINED (symbolP) || symbol_equated_p (symbolP))
	&& S_GET_SEGMENT (symbolP) != reg_section;

      cframe.ignoring = ! (test_defined ^ is_defined);
    }

  current_cframe = ((struct conditional_frame *)
		    obstack_copy (&cond_obstack, &cframe,
				  sizeof (cframe)));

  if (LISTING_SKIP_COND ()
      && cframe.ignoring
      && (cframe.previous_cframe == NULL
	  || ! cframe.previous_cframe->ignoring))
    listing_list (2);

  demand_empty_rest_of_line ();
}

void
s_if (int arg)
{
  expressionS operand;
  struct conditional_frame cframe;
  int t;
  char *stop = NULL;
  char stopc;

  if (flag_mri)
    stop = mri_comment_field (&stopc);

  /* Leading whitespace is part of operand.  */
  SKIP_WHITESPACE ();

  if (current_cframe != NULL && current_cframe->ignoring)
    {
      operand.X_add_number = 0;
      while (! is_end_of_line[(unsigned char) *input_line_pointer])
	++input_line_pointer;
    }
  else
    {
      expression_and_evaluate (&operand);
      if (operand.X_op != O_constant)
	as_bad (_("non-constant expression in \".if\" statement"));
    }

  switch ((operatorT) arg)
    {
    case O_eq: t = operand.X_add_number == 0; break;
    case O_ne: t = operand.X_add_number != 0; break;
    case O_lt: t = operand.X_add_number < 0; break;
    case O_le: t = operand.X_add_number <= 0; break;
    case O_ge: t = operand.X_add_number >= 0; break;
    case O_gt: t = operand.X_add_number > 0; break;
    default:
      abort ();
      return;
    }

  /* If the above error is signaled, this will dispatch
     using an undefined result.  No big deal.  */
  initialize_cframe (&cframe);
  cframe.ignoring = cframe.dead_tree || ! t;
  current_cframe = ((struct conditional_frame *)
		    obstack_copy (&cond_obstack, &cframe, sizeof (cframe)));

  if (LISTING_SKIP_COND ()
      && cframe.ignoring
      && (cframe.previous_cframe == NULL
	  || ! cframe.previous_cframe->ignoring))
    listing_list (2);

  if (flag_mri)
    mri_comment_end (stop, stopc);

  demand_empty_rest_of_line ();
}

/* Performs the .ifb (test_blank == 1) and
   the .ifnb (test_blank == 0) pseudo op.  */

void
s_ifb (int test_blank)
{
  struct conditional_frame cframe;

  initialize_cframe (&cframe);
  
  if (cframe.dead_tree)
    cframe.ignoring = 1;
  else
    {
      int is_eol;

      SKIP_WHITESPACE ();
      is_eol = is_end_of_line[(unsigned char) *input_line_pointer];
      cframe.ignoring = (test_blank == !is_eol);
    }

  current_cframe = ((struct conditional_frame *)
		    obstack_copy (&cond_obstack, &cframe,
				  sizeof (cframe)));

  if (LISTING_SKIP_COND ()
      && cframe.ignoring
      && (cframe.previous_cframe == NULL
	  || ! cframe.previous_cframe->ignoring))
    listing_list (2);

  ignore_rest_of_line ();
}

/* Get a string for the MRI IFC or IFNC pseudo-ops.  */

static char *
get_mri_string (int terminator, int *len)
{
  char *ret;
  char *s;

  SKIP_WHITESPACE ();
  s = ret = input_line_pointer;
  if (*input_line_pointer == '\'')
    {
      ++s;
      ++input_line_pointer;
      while (! is_end_of_line[(unsigned char) *input_line_pointer])
	{
	  *s++ = *input_line_pointer++;
	  if (s[-1] == '\'')
	    {
	      if (*input_line_pointer != '\'')
		break;
	      ++input_line_pointer;
	    }
	}
      SKIP_WHITESPACE ();
    }
  else
    {
      while (*input_line_pointer != terminator
	     && ! is_end_of_line[(unsigned char) *input_line_pointer])
	++input_line_pointer;
      s = input_line_pointer;
      while (s > ret && (s[-1] == ' ' || s[-1] == '\t'))
	--s;
    }

  *len = s - ret;
  return ret;
}

/* The MRI IFC and IFNC pseudo-ops.  */

void
s_ifc (int arg)
{
  char *stop = NULL;
  char stopc;
  char *s1, *s2;
  int len1, len2;
  int res;
  struct conditional_frame cframe;

  if (flag_mri)
    stop = mri_comment_field (&stopc);

  s1 = get_mri_string (',', &len1);

  if (*input_line_pointer != ',')
    as_bad (_("bad format for ifc or ifnc"));
  else
    ++input_line_pointer;

  s2 = get_mri_string (';', &len2);

  res = len1 == len2 && strncmp (s1, s2, len1) == 0;

  initialize_cframe (&cframe);
  cframe.ignoring = cframe.dead_tree || ! (res ^ arg);
  current_cframe = ((struct conditional_frame *)
		    obstack_copy (&cond_obstack, &cframe, sizeof (cframe)));

  if (LISTING_SKIP_COND ()
      && cframe.ignoring
      && (cframe.previous_cframe == NULL
	  || ! cframe.previous_cframe->ignoring))
    listing_list (2);

  if (flag_mri)
    mri_comment_end (stop, stopc);

  demand_empty_rest_of_line ();
}

void
s_elseif (int arg)
{
  if (current_cframe == NULL)
    {
      as_bad (_("\".elseif\" without matching \".if\""));
    }
  else if (current_cframe->else_seen)
    {
      as_bad (_("\".elseif\" after \".else\""));
      as_bad_where (current_cframe->else_file_line.file,
		    current_cframe->else_file_line.line,
		    _("here is the previous \"else\""));
      as_bad_where (current_cframe->if_file_line.file,
		    current_cframe->if_file_line.line,
		    _("here is the previous \"if\""));
    }
  else
    {
      as_where (&current_cframe->else_file_line.file,
		&current_cframe->else_file_line.line);

      current_cframe->dead_tree |= !current_cframe->ignoring;
      current_cframe->ignoring = current_cframe->dead_tree;
    }

  if (current_cframe == NULL || current_cframe->ignoring)
    {
      while (! is_end_of_line[(unsigned char) *input_line_pointer])
	++input_line_pointer;

      if (current_cframe == NULL)
	return;
    }
  else
    {
      expressionS operand;
      int t;

      /* Leading whitespace is part of operand.  */
      SKIP_WHITESPACE ();

      expression_and_evaluate (&operand);
      if (operand.X_op != O_constant)
	as_bad (_("non-constant expression in \".elseif\" statement"));

      switch ((operatorT) arg)
	{
	case O_eq: t = operand.X_add_number == 0; break;
	case O_ne: t = operand.X_add_number != 0; break;
	case O_lt: t = operand.X_add_number < 0; break;
	case O_le: t = operand.X_add_number <= 0; break;
	case O_ge: t = operand.X_add_number >= 0; break;
	case O_gt: t = operand.X_add_number > 0; break;
	default:
	  abort ();
	  return;
	}

      current_cframe->ignoring = current_cframe->dead_tree || ! t;
    }

  if (LISTING_SKIP_COND ()
      && (current_cframe->previous_cframe == NULL
	  || ! current_cframe->previous_cframe->ignoring))
    {
      if (! current_cframe->ignoring)
	listing_list (1);
      else
	listing_list (2);
    }

  demand_empty_rest_of_line ();
}

void
s_endif (int arg ATTRIBUTE_UNUSED)
{
  struct conditional_frame *hold;

  if (current_cframe == NULL)
    {
      as_bad (_("\".endif\" without \".if\""));
    }
  else
    {
      if (LISTING_SKIP_COND ()
	  && current_cframe->ignoring
	  && (current_cframe->previous_cframe == NULL
	      || ! current_cframe->previous_cframe->ignoring))
	listing_list (1);

      hold = current_cframe;
      current_cframe = current_cframe->previous_cframe;
      obstack_free (&cond_obstack, hold);
    }				/* if one pop too many */

  if (flag_mri)
    {
      while (! is_end_of_line[(unsigned char) *input_line_pointer])
	++input_line_pointer;
    }

  demand_empty_rest_of_line ();
}

void
s_else (int arg ATTRIBUTE_UNUSED)
{
  if (current_cframe == NULL)
    {
      as_bad (_("\".else\" without matching \".if\""));
    }
  else if (current_cframe->else_seen)
    {
      as_bad (_("duplicate \"else\""));
      as_bad_where (current_cframe->else_file_line.file,
		    current_cframe->else_file_line.line,
		    _("here is the previous \"else\""));
      as_bad_where (current_cframe->if_file_line.file,
		    current_cframe->if_file_line.line,
		    _("here is the previous \"if\""));
    }
  else
    {
      as_where (&current_cframe->else_file_line.file,
		&current_cframe->else_file_line.line);

      current_cframe->ignoring =
	current_cframe->dead_tree | !current_cframe->ignoring;

      if (LISTING_SKIP_COND ()
	  && (current_cframe->previous_cframe == NULL
	      || ! current_cframe->previous_cframe->ignoring))
	{
	  if (! current_cframe->ignoring)
	    listing_list (1);
	  else
	    listing_list (2);
	}

      current_cframe->else_seen = 1;
    }

  if (flag_mri)
    {
      while (! is_end_of_line[(unsigned char) *input_line_pointer])
	++input_line_pointer;
    }

  demand_empty_rest_of_line ();
}

void
s_ifeqs (int arg)
{
  char *s1, *s2;
  int len1, len2;
  int res;
  struct conditional_frame cframe;

  s1 = demand_copy_C_string (&len1);

  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_(".ifeqs syntax error"));
      ignore_rest_of_line ();
      return;
    }

  ++input_line_pointer;

  s2 = demand_copy_C_string (&len2);

  res = len1 == len2 && strncmp (s1, s2, len1) == 0;

  initialize_cframe (&cframe);
  cframe.ignoring = cframe.dead_tree || ! (res ^ arg);
  current_cframe = ((struct conditional_frame *)
		    obstack_copy (&cond_obstack, &cframe, sizeof (cframe)));

  if (LISTING_SKIP_COND ()
      && cframe.ignoring
      && (cframe.previous_cframe == NULL
	  || ! cframe.previous_cframe->ignoring))
    listing_list (2);

  demand_empty_rest_of_line ();
}

int
ignore_input (void)
{
  char *s;

  s = input_line_pointer;

  if (NO_PSEUDO_DOT || flag_m68k_mri)
    {
      if (s[-1] != '.')
	--s;
    }
  else
    {
      if (s[-1] != '.')
	return (current_cframe != NULL) && (current_cframe->ignoring);
    }

  /* We cannot ignore certain pseudo ops.  */
  if (((s[0] == 'i'
	|| s[0] == 'I')
       && (!strncasecmp (s, "if", 2)
	   || !strncasecmp (s, "ifdef", 5)
	   || !strncasecmp (s, "ifndef", 6)))
      || ((s[0] == 'e'
	   || s[0] == 'E')
	  && (!strncasecmp (s, "else", 4)
	      || !strncasecmp (s, "endif", 5)
	      || !strncasecmp (s, "endc", 4))))
    return 0;

  return (current_cframe != NULL) && (current_cframe->ignoring);
}

static void
initialize_cframe (struct conditional_frame *cframe)
{
  memset (cframe, 0, sizeof (*cframe));
  as_where (&cframe->if_file_line.file,
	    &cframe->if_file_line.line);
  cframe->previous_cframe = current_cframe;
  cframe->dead_tree = current_cframe != NULL && current_cframe->ignoring;
  cframe->macro_nest = macro_nest;
}

/* Give an error if a conditional is unterminated inside a macro or
   the assembly as a whole.  If NEST is non negative, we are being
   called because of the end of a macro expansion.  If NEST is
   negative, we are being called at the of the input files.  */

void
cond_finish_check (int nest)
{
  if (current_cframe != NULL && current_cframe->macro_nest >= nest)
    {
      if (nest >= 0)
	as_bad (_("end of macro inside conditional"));
      else
	as_bad (_("end of file inside conditional"));
      as_bad_where (current_cframe->if_file_line.file,
		    current_cframe->if_file_line.line,
		    _("here is the start of the unterminated conditional"));
      if (current_cframe->else_seen)
	as_bad_where (current_cframe->else_file_line.file,
		      current_cframe->else_file_line.line,
		      _("here is the \"else\" of the unterminated conditional"));
    }
}

/* This function is called when we exit out of a macro.  We assume
   that any conditionals which began within the macro are correctly
   nested, and just pop them off the stack.  */

void
cond_exit_macro (int nest)
{
  while (current_cframe != NULL && current_cframe->macro_nest >= nest)
    {
      struct conditional_frame *hold;

      hold = current_cframe;
      current_cframe = current_cframe->previous_cframe;
      obstack_free (&cond_obstack, hold);
    }
}
