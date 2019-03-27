/* C language support routines for GDB, the GNU debugger.
   Copyright 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.

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
#include "c-lang.h"
#include "valprint.h"
#include "macroscope.h"
#include "gdb_assert.h"
#include "charset.h"
#include "gdb_string.h"
#include "demangle.h"
#include "cp-support.h"

extern void _initialize_c_language (void);
static void c_emit_char (int c, struct ui_file * stream, int quoter);

/* Print the character C on STREAM as part of the contents of a literal
   string whose delimiter is QUOTER.  Note that that format for printing
   characters and strings is language specific. */

static void
c_emit_char (int c, struct ui_file *stream, int quoter)
{
  const char *escape;
  int host_char;

  c &= 0xFF;			/* Avoid sign bit follies */

  escape = c_target_char_has_backslash_escape (c);
  if (escape)
    {
      if (quoter == '"' && strcmp (escape, "0") == 0)
	/* Print nulls embedded in double quoted strings as \000 to
	   prevent ambiguity.  */
	fprintf_filtered (stream, "\\000");
      else
	fprintf_filtered (stream, "\\%s", escape);
    }
  else if (target_char_to_host (c, &host_char)
           && host_char_print_literally (host_char))
    {
      if (host_char == '\\' || host_char == quoter)
        fputs_filtered ("\\", stream);
      fprintf_filtered (stream, "%c", host_char);
    }
  else
    fprintf_filtered (stream, "\\%.3o", (unsigned int) c);
}

void
c_printchar (int c, struct ui_file *stream)
{
  fputc_filtered ('\'', stream);
  LA_EMIT_CHAR (c, stream, '\'');
  fputc_filtered ('\'', stream);
}

/* Print the character string STRING, printing at most LENGTH characters.
   LENGTH is -1 if the string is nul terminated.  Each character is WIDTH bytes
   long.  Printing stops early if the number hits print_max; repeat counts are
   printed as appropriate.  Print ellipses at the end if we had to stop before
   printing LENGTH characters, or if FORCE_ELLIPSES.  */

void
c_printstr (struct ui_file *stream, char *string, unsigned int length,
	    int width, int force_ellipses)
{
  unsigned int i;
  unsigned int things_printed = 0;
  int in_quotes = 0;
  int need_comma = 0;

  /* If the string was not truncated due to `set print elements', and
     the last byte of it is a null, we don't print that, in traditional C
     style.  */
  if (!force_ellipses
      && length > 0
      && (extract_unsigned_integer (string + (length - 1) * width, width)
          == '\0'))
    length--;

  if (length == 0)
    {
      fputs_filtered ("\"\"", stream);
      return;
    }

  for (i = 0; i < length && things_printed < print_max; ++i)
    {
      /* Position of the character we are examining
         to see whether it is repeated.  */
      unsigned int rep1;
      /* Number of repetitions we have detected so far.  */
      unsigned int reps;
      unsigned long current_char;

      QUIT;

      if (need_comma)
	{
	  fputs_filtered (", ", stream);
	  need_comma = 0;
	}

      current_char = extract_unsigned_integer (string + i * width, width);

      rep1 = i + 1;
      reps = 1;
      while (rep1 < length
	     && extract_unsigned_integer (string + rep1 * width, width)
	     == current_char)
	{
	  ++rep1;
	  ++reps;
	}

      if (reps > repeat_count_threshold)
	{
	  if (in_quotes)
	    {
	      if (inspect_it)
		fputs_filtered ("\\\", ", stream);
	      else
		fputs_filtered ("\", ", stream);
	      in_quotes = 0;
	    }
	  LA_PRINT_CHAR (current_char, stream);
	  fprintf_filtered (stream, " <repeats %u times>", reps);
	  i = rep1 - 1;
	  things_printed += repeat_count_threshold;
	  need_comma = 1;
	}
      else
	{
	  if (!in_quotes)
	    {
	      if (inspect_it)
		fputs_filtered ("\\\"", stream);
	      else
		fputs_filtered ("\"", stream);
	      in_quotes = 1;
	    }
	  LA_EMIT_CHAR (current_char, stream, '"');
	  ++things_printed;
	}
    }

  /* Terminate the quotes if necessary.  */
  if (in_quotes)
    {
      if (inspect_it)
	fputs_filtered ("\\\"", stream);
      else
	fputs_filtered ("\"", stream);
    }

  if (force_ellipses || i < length)
    fputs_filtered ("...", stream);
}

/* Create a fundamental C type using default reasonable for the current
   target machine.

   Some object/debugging file formats (DWARF version 1, COFF, etc) do not
   define fundamental types such as "int" or "double".  Others (stabs or
   DWARF version 2, etc) do define fundamental types.  For the formats which
   don't provide fundamental types, gdb can create such types using this
   function.

   FIXME:  Some compilers distinguish explicitly signed integral types
   (signed short, signed int, signed long) from "regular" integral types
   (short, int, long) in the debugging information.  There is some dis-
   agreement as to how useful this feature is.  In particular, gcc does
   not support this.  Also, only some debugging formats allow the
   distinction to be passed on to a debugger.  For now, we always just
   use "short", "int", or "long" as the type name, for both the implicit
   and explicitly signed types.  This also makes life easier for the
   gdb test suite since we don't have to account for the differences
   in output depending upon what the compiler and debugging format
   support.  We will probably have to re-examine the issue when gdb
   starts taking it's fundamental type information directly from the
   debugging information supplied by the compiler.  fnf@cygnus.com */

struct type *
c_create_fundamental_type (struct objfile *objfile, int typeid)
{
  struct type *type = NULL;

  switch (typeid)
    {
    default:
      /* FIXME:  For now, if we are asked to produce a type not in this
         language, create the equivalent of a C integer type with the
         name "<?type?>".  When all the dust settles from the type
         reconstruction work, this should probably become an error. */
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "<?type?>", objfile);
      warning ("internal error: no C/C++ fundamental type %d", typeid);
      break;
    case FT_VOID:
      type = init_type (TYPE_CODE_VOID,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "void", objfile);
      break;
    case FT_BOOLEAN:
      type = init_type (TYPE_CODE_BOOL,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "bool", objfile);
      break;
    case FT_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_NOSIGN, "char", objfile);
      break;
    case FT_SIGNED_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "signed char", objfile);
      break;
    case FT_UNSIGNED_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned char", objfile);
      break;
    case FT_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			0, "short", objfile);
      break;
    case FT_SIGNED_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			0, "short", objfile);	/* FIXME-fnf */
      break;
    case FT_UNSIGNED_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned short", objfile);
      break;
    case FT_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "int", objfile);
      break;
    case FT_SIGNED_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "int", objfile);	/* FIXME -fnf */
      break;
    case FT_UNSIGNED_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned int", objfile);
      break;
    case FT_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_BIT / TARGET_CHAR_BIT,
			0, "long", objfile);
      break;
    case FT_SIGNED_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_BIT / TARGET_CHAR_BIT,
			0, "long", objfile);	/* FIXME -fnf */
      break;
    case FT_UNSIGNED_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned long", objfile);
      break;
    case FT_LONG_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
			0, "long long", objfile);
      break;
    case FT_SIGNED_LONG_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
			0, "signed long long", objfile);
      break;
    case FT_UNSIGNED_LONG_LONG:
      type = init_type (TYPE_CODE_INT,
			TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "unsigned long long", objfile);
      break;
    case FT_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
			0, "float", objfile);
      break;
    case FT_DBL_PREC_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "double", objfile);
      break;
    case FT_EXT_PREC_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "long double", objfile);
      break;
    case FT_COMPLEX:
      type = init_type (TYPE_CODE_FLT,
			2 * TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
			0, "complex float", objfile);
      TYPE_TARGET_TYPE (type)
	= init_type (TYPE_CODE_FLT, TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
		     0, "float", objfile);
      break;
    case FT_DBL_PREC_COMPLEX:
      type = init_type (TYPE_CODE_FLT,
			2 * TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "complex double", objfile);
      TYPE_TARGET_TYPE (type)
	= init_type (TYPE_CODE_FLT, TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
		     0, "double", objfile);
      break;
    case FT_EXT_PREC_COMPLEX:
      type = init_type (TYPE_CODE_FLT,
			2 * TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "complex long double", objfile);
      TYPE_TARGET_TYPE (type)
	= init_type (TYPE_CODE_FLT, TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
		     0, "long double", objfile);
      break;
    case FT_TEMPLATE_ARG:
      type = init_type (TYPE_CODE_TEMPLATE_ARG,
			0,
			0, "<template arg>", objfile);
      break;
    }
  return (type);
}

/* Preprocessing and parsing C and C++ expressions.  */


/* When we find that lexptr (the global var defined in parse.c) is
   pointing at a macro invocation, we expand the invocation, and call
   scan_macro_expansion to save the old lexptr here and point lexptr
   into the expanded text.  When we reach the end of that, we call
   end_macro_expansion to pop back to the value we saved here.  The
   macro expansion code promises to return only fully-expanded text,
   so we don't need to "push" more than one level.

   This is disgusting, of course.  It would be cleaner to do all macro
   expansion beforehand, and then hand that to lexptr.  But we don't
   really know where the expression ends.  Remember, in a command like

     (gdb) break *ADDRESS if CONDITION

   we evaluate ADDRESS in the scope of the current frame, but we
   evaluate CONDITION in the scope of the breakpoint's location.  So
   it's simply wrong to try to macro-expand the whole thing at once.  */
static char *macro_original_text;
static char *macro_expanded_text;


void
scan_macro_expansion (char *expansion)
{
  /* We'd better not be trying to push the stack twice.  */
  gdb_assert (! macro_original_text);
  gdb_assert (! macro_expanded_text);

  /* Save the old lexptr value, so we can return to it when we're done
     parsing the expanded text.  */
  macro_original_text = lexptr;
  lexptr = expansion;

  /* Save the expanded text, so we can free it when we're finished.  */
  macro_expanded_text = expansion;
}


int
scanning_macro_expansion (void)
{
  return macro_original_text != 0;
}


void 
finished_macro_expansion (void)
{
  /* There'd better be something to pop back to, and we better have
     saved a pointer to the start of the expanded text.  */
  gdb_assert (macro_original_text);
  gdb_assert (macro_expanded_text);

  /* Pop back to the original text.  */
  lexptr = macro_original_text;
  macro_original_text = 0;

  /* Free the expanded text.  */
  xfree (macro_expanded_text);
  macro_expanded_text = 0;
}


static void
scan_macro_cleanup (void *dummy)
{
  if (macro_original_text)
    finished_macro_expansion ();
}


/* We set these global variables before calling c_parse, to tell it
   how it to find macro definitions for the expression at hand.  */
macro_lookup_ftype *expression_macro_lookup_func;
void *expression_macro_lookup_baton;


static struct macro_definition *
null_macro_lookup (const char *name, void *baton)
{
  return 0;
}


static int
c_preprocess_and_parse (void)
{
  /* Set up a lookup function for the macro expander.  */
  struct macro_scope *scope = 0;
  struct cleanup *back_to = make_cleanup (free_current_contents, &scope);

  if (expression_context_block)
    scope = sal_macro_scope (find_pc_line (expression_context_pc, 0));
  else
    scope = default_macro_scope ();

  if (scope)
    {
      expression_macro_lookup_func = standard_macro_lookup;
      expression_macro_lookup_baton = (void *) scope;
    }
  else
    {
      expression_macro_lookup_func = null_macro_lookup;
      expression_macro_lookup_baton = 0;      
    }

  gdb_assert (! macro_original_text);
  make_cleanup (scan_macro_cleanup, 0);

  {
    int result = c_parse ();
    do_cleanups (back_to);
    return result;
  }
}



/* Table mapping opcodes into strings for printing operators
   and precedences of the operators.  */

const struct op_print c_op_print_tab[] =
{
  {",", BINOP_COMMA, PREC_COMMA, 0},
  {"=", BINOP_ASSIGN, PREC_ASSIGN, 1},
  {"||", BINOP_LOGICAL_OR, PREC_LOGICAL_OR, 0},
  {"&&", BINOP_LOGICAL_AND, PREC_LOGICAL_AND, 0},
  {"|", BINOP_BITWISE_IOR, PREC_BITWISE_IOR, 0},
  {"^", BINOP_BITWISE_XOR, PREC_BITWISE_XOR, 0},
  {"&", BINOP_BITWISE_AND, PREC_BITWISE_AND, 0},
  {"==", BINOP_EQUAL, PREC_EQUAL, 0},
  {"!=", BINOP_NOTEQUAL, PREC_EQUAL, 0},
  {"<=", BINOP_LEQ, PREC_ORDER, 0},
  {">=", BINOP_GEQ, PREC_ORDER, 0},
  {">", BINOP_GTR, PREC_ORDER, 0},
  {"<", BINOP_LESS, PREC_ORDER, 0},
  {">>", BINOP_RSH, PREC_SHIFT, 0},
  {"<<", BINOP_LSH, PREC_SHIFT, 0},
  {"+", BINOP_ADD, PREC_ADD, 0},
  {"-", BINOP_SUB, PREC_ADD, 0},
  {"*", BINOP_MUL, PREC_MUL, 0},
  {"/", BINOP_DIV, PREC_MUL, 0},
  {"%", BINOP_REM, PREC_MUL, 0},
  {"@", BINOP_REPEAT, PREC_REPEAT, 0},
  {"-", UNOP_NEG, PREC_PREFIX, 0},
  {"!", UNOP_LOGICAL_NOT, PREC_PREFIX, 0},
  {"~", UNOP_COMPLEMENT, PREC_PREFIX, 0},
  {"*", UNOP_IND, PREC_PREFIX, 0},
  {"&", UNOP_ADDR, PREC_PREFIX, 0},
  {"sizeof ", UNOP_SIZEOF, PREC_PREFIX, 0},
  {"++", UNOP_PREINCREMENT, PREC_PREFIX, 0},
  {"--", UNOP_PREDECREMENT, PREC_PREFIX, 0},
  {NULL, 0, 0, 0}
};

struct type **const (c_builtin_types[]) =
{
  &builtin_type_int,
  &builtin_type_long,
  &builtin_type_short,
  &builtin_type_char,
  &builtin_type_float,
  &builtin_type_double,
  &builtin_type_void,
  &builtin_type_long_long,
  &builtin_type_signed_char,
  &builtin_type_unsigned_char,
  &builtin_type_unsigned_short,
  &builtin_type_unsigned_int,
  &builtin_type_unsigned_long,
  &builtin_type_unsigned_long_long,
  &builtin_type_long_double,
  &builtin_type_complex,
  &builtin_type_double_complex,
  0
};

const struct language_defn c_language_defn =
{
  "c",				/* Language name */
  language_c,
  c_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_on,
  &exp_descriptor_standard,
  c_preprocess_and_parse,
  c_error,
  c_printchar,			/* Print a character constant */
  c_printstr,			/* Function to print string constant */
  c_emit_char,			/* Print a single char */
  c_create_fundamental_type,	/* Create fundamental type in this language */
  c_print_type,			/* Print a type using appropriate syntax */
  c_val_print,			/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  NULL,				/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
  {"0%lo", "0", "o", ""},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"0x%lx", "0x", "x", ""},	/* Hex format info */
  c_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  &builtin_type_char,		/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

struct type **const (cplus_builtin_types[]) =
{
  &builtin_type_int,
  &builtin_type_long,
  &builtin_type_short,
  &builtin_type_char,
  &builtin_type_float,
  &builtin_type_double,
  &builtin_type_void,
  &builtin_type_long_long,
  &builtin_type_signed_char,
  &builtin_type_unsigned_char,
  &builtin_type_unsigned_short,
  &builtin_type_unsigned_int,
  &builtin_type_unsigned_long,
  &builtin_type_unsigned_long_long,
  &builtin_type_long_double,
  &builtin_type_complex,
  &builtin_type_double_complex,
  &builtin_type_bool,
  0
};

const struct language_defn cplus_language_defn =
{
  "c++",			/* Language name */
  language_cplus,
  cplus_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_on,
  &exp_descriptor_standard,
  c_preprocess_and_parse,
  c_error,
  c_printchar,			/* Print a character constant */
  c_printstr,			/* Function to print string constant */
  c_emit_char,			/* Print a single char */
  c_create_fundamental_type,	/* Create fundamental type in this language */
  c_print_type,			/* Print a type using appropriate syntax */
  c_val_print,			/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  value_of_this,		/* value_of_this */
  cp_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  cp_lookup_transparent_type,   /* lookup_transparent_type */
  cplus_demangle,		/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
  {"0%lo", "0", "o", ""},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"0x%lx", "0x", "x", ""},	/* Hex format info */
  c_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  &builtin_type_char,		/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

const struct language_defn asm_language_defn =
{
  "asm",			/* Language name */
  language_asm,
  c_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_on,
  &exp_descriptor_standard,
  c_preprocess_and_parse,
  c_error,
  c_printchar,			/* Print a character constant */
  c_printstr,			/* Function to print string constant */
  c_emit_char,			/* Print a single char */
  c_create_fundamental_type,	/* Create fundamental type in this language */
  c_print_type,			/* Print a type using appropriate syntax */
  c_val_print,			/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  NULL,				/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
  {"0%lo", "0", "o", ""},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"0x%lx", "0x", "x", ""},	/* Hex format info */
  c_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  &builtin_type_char,		/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

/* The following language_defn does not represent a real language.
   It just provides a minimal support a-la-C that should allow users
   to do some simple operations when debugging applications that use
   a language currently not supported by GDB.  */

const struct language_defn minimal_language_defn =
{
  "minimal",			/* Language name */
  language_minimal,
  c_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_on,
  &exp_descriptor_standard,
  c_preprocess_and_parse,
  c_error,
  c_printchar,			/* Print a character constant */
  c_printstr,			/* Function to print string constant */
  c_emit_char,			/* Print a single char */
  c_create_fundamental_type,	/* Create fundamental type in this language */
  c_print_type,			/* Print a type using appropriate syntax */
  c_val_print,			/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  NULL,				/* Language specific skip_trampoline */
  NULL,				/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
  {"0%lo", "0", "o", ""},	/* Octal format info */
  {"%ld", "", "d", ""},		/* Decimal format info */
  {"0x%lx", "0x", "x", ""},	/* Hex format info */
  c_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  &builtin_type_char,		/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

void
_initialize_c_language (void)
{
  add_language (&c_language_defn);
  add_language (&cplus_language_defn);
  add_language (&asm_language_defn);
  add_language (&minimal_language_defn);
}
