/* Scheme/Guile language support routines for GDB, the GNU debugger.

   Copyright 1995, 1996, 1998, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "parser-defs.h"
#include "language.h"
#include "value.h"
#include "c-lang.h"
#include "scm-lang.h"
#include "scm-tags.h"
#include "source.h"
#include "gdb_string.h"
#include "gdbcore.h"
#include "infcall.h"

extern void _initialize_scheme_language (void);
static struct value *evaluate_subexp_scm (struct type *, struct expression *,
				      int *, enum noside);
static struct value *scm_lookup_name (char *);
static int in_eval_c (void);
static void scm_printstr (struct ui_file * stream, char *string,
			  unsigned int length, int width,
			  int force_ellipses);

extern struct type **const (c_builtin_types[]);

struct type *builtin_type_scm;

void
scm_printchar (int c, struct ui_file *stream)
{
  fprintf_filtered (stream, "#\\%c", c);
}

static void
scm_printstr (struct ui_file *stream, char *string, unsigned int length,
	      int width, int force_ellipses)
{
  fprintf_filtered (stream, "\"%s\"", string);
}

int
is_scmvalue_type (struct type *type)
{
  if (TYPE_CODE (type) == TYPE_CODE_INT
      && TYPE_NAME (type) && strcmp (TYPE_NAME (type), "SCM") == 0)
    {
      return 1;
    }
  return 0;
}

/* Get the INDEX'th SCM value, assuming SVALUE is the address
   of the 0'th one.  */

LONGEST
scm_get_field (LONGEST svalue, int index)
{
  char buffer[20];
  read_memory (SCM2PTR (svalue) + index * TYPE_LENGTH (builtin_type_scm),
	       buffer, TYPE_LENGTH (builtin_type_scm));
  return extract_signed_integer (buffer, TYPE_LENGTH (builtin_type_scm));
}

/* Unpack a value of type TYPE in buffer VALADDR as an integer
   (if CONTEXT == TYPE_CODE_IN), a pointer (CONTEXT == TYPE_CODE_PTR),
   or Boolean (CONTEXT == TYPE_CODE_BOOL).  */

LONGEST
scm_unpack (struct type *type, const char *valaddr, enum type_code context)
{
  if (is_scmvalue_type (type))
    {
      LONGEST svalue = extract_signed_integer (valaddr, TYPE_LENGTH (type));
      if (context == TYPE_CODE_BOOL)
	{
	  if (svalue == SCM_BOOL_F)
	    return 0;
	  else
	    return 1;
	}
      switch (7 & (int) svalue)
	{
	case 2:
	case 6:		/* fixnum */
	  return svalue >> 2;
	case 4:		/* other immediate value */
	  if (SCM_ICHRP (svalue))	/* character */
	    return SCM_ICHR (svalue);
	  else if (SCM_IFLAGP (svalue))
	    {
	      switch ((int) svalue)
		{
#ifndef SICP
		case SCM_EOL:
#endif
		case SCM_BOOL_F:
		  return 0;
		case SCM_BOOL_T:
		  return 1;
		}
	    }
	  error ("Value can't be converted to integer.");
	default:
	  return svalue;
	}
    }
  else
    return unpack_long (type, valaddr);
}

/* True if we're correctly in Guile's eval.c (the evaluator and apply). */

static int
in_eval_c (void)
{
  struct symtab_and_line cursal = get_current_source_symtab_and_line ();
  
  if (cursal.symtab && cursal.symtab->filename)
    {
      char *filename = cursal.symtab->filename;
      int len = strlen (filename);
      if (len >= 6 && strcmp (filename + len - 6, "eval.c") == 0)
	return 1;
    }
  return 0;
}

/* Lookup a value for the variable named STR.
   First lookup in Scheme context (using the scm_lookup_cstr inferior
   function), then try lookup_symbol for compiled variables. */

static struct value *
scm_lookup_name (char *str)
{
  struct value *args[3];
  int len = strlen (str);
  struct value *func;
  struct value *val;
  struct symbol *sym;
  args[0] = value_allocate_space_in_inferior (len);
  args[1] = value_from_longest (builtin_type_int, len);
  write_memory (value_as_long (args[0]), str, len);

  if (in_eval_c ()
      && (sym = lookup_symbol ("env",
			       expression_context_block,
			       VAR_DOMAIN, (int *) NULL,
			       (struct symtab **) NULL)) != NULL)
    args[2] = value_of_variable (sym, expression_context_block);
  else
    /* FIXME in this case, we should try lookup_symbol first */
    args[2] = value_from_longest (builtin_type_scm, SCM_EOL);

  func = find_function_in_inferior ("scm_lookup_cstr");
  val = call_function_by_hand (func, 3, args);
  if (!value_logical_not (val))
    return value_ind (val);

  sym = lookup_symbol (str,
		       expression_context_block,
		       VAR_DOMAIN, (int *) NULL,
		       (struct symtab **) NULL);
  if (sym)
    return value_of_variable (sym, NULL);
  error ("No symbol \"%s\" in current context.", str);
}

struct value *
scm_evaluate_string (char *str, int len)
{
  struct value *func;
  struct value *addr = value_allocate_space_in_inferior (len + 1);
  LONGEST iaddr = value_as_long (addr);
  write_memory (iaddr, str, len);
  /* FIXME - should find and pass env */
  write_memory (iaddr + len, "", 1);
  func = find_function_in_inferior ("scm_evstr");
  return call_function_by_hand (func, 1, &addr);
}

static struct value *
evaluate_subexp_scm (struct type *expect_type, struct expression *exp,
		     int *pos, enum noside noside)
{
  enum exp_opcode op = exp->elts[*pos].opcode;
  int len, pc;
  char *str;
  switch (op)
    {
    case OP_NAME:
      pc = (*pos)++;
      len = longest_to_int (exp->elts[pc + 1].longconst);
      (*pos) += 3 + BYTES_TO_EXP_ELEM (len + 1);
      if (noside == EVAL_SKIP)
	goto nosideret;
      str = &exp->elts[pc + 2].string;
      return scm_lookup_name (str);
    case OP_EXPRSTRING:
      pc = (*pos)++;
      len = longest_to_int (exp->elts[pc + 1].longconst);
      (*pos) += 3 + BYTES_TO_EXP_ELEM (len + 1);
      if (noside == EVAL_SKIP)
	goto nosideret;
      str = &exp->elts[pc + 2].string;
      return scm_evaluate_string (str, len);
    default:;
    }
  return evaluate_subexp_standard (expect_type, exp, pos, noside);
nosideret:
  return value_from_longest (builtin_type_long, (LONGEST) 1);
}

const struct exp_descriptor exp_descriptor_scm = 
{
  print_subexp_standard,
  operator_length_standard,
  op_name_standard,
  dump_subexp_body_standard,
  evaluate_subexp_scm
};

const struct language_defn scm_language_defn =
{
  "scheme",			/* Language name */
  language_scm,
  c_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_off,
  &exp_descriptor_scm,
  scm_parse,
  c_error,
  scm_printchar,		/* Print a character constant */
  scm_printstr,			/* Function to print string constant */
  NULL,				/* Function to print a single character */
  NULL,				/* Create fundamental type in this language */
  c_print_type,			/* Print a type using appropriate syntax */
  scm_val_print,		/* Print a value using appropriate syntax */
  scm_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  value_of_this,		/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
  {"#o%lo", "#o", "o", ""},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"#x%lX", "#X", "X", ""},	/* Hex format info */
  NULL,				/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  &builtin_type_char,		/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

void
_initialize_scheme_language (void)
{
  add_language (&scm_language_defn);
  builtin_type_scm = init_type (TYPE_CODE_INT,
				TARGET_LONG_BIT / TARGET_CHAR_BIT,
				0, "SCM", (struct objfile *) NULL);
}
