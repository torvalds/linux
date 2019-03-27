/* Fortran language support routines for GDB, the GNU debugger.
   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Motorola.  Adapted from the C parser by Farooq Butt
   (fmbutt@engage.sps.mot.com).

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
#include "gdb_string.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "parser-defs.h"
#include "language.h"
#include "f-lang.h"
#include "valprint.h"
#include "value.h"

/* The built-in types of F77.  FIXME: integer*4 is missing, plain
   logical is missing (builtin_type_logical is logical*4).  */

struct type *builtin_type_f_character;
struct type *builtin_type_f_logical;
struct type *builtin_type_f_logical_s1;
struct type *builtin_type_f_logical_s2;
struct type *builtin_type_f_integer;
struct type *builtin_type_f_integer_s2;
struct type *builtin_type_f_real;
struct type *builtin_type_f_real_s8;
struct type *builtin_type_f_real_s16;
struct type *builtin_type_f_complex_s8;
struct type *builtin_type_f_complex_s16;
struct type *builtin_type_f_complex_s32;
struct type *builtin_type_f_void;

/* Following is dubious stuff that had been in the xcoff reader. */

struct saved_fcn
  {
    long line_offset;		/* Line offset for function */
    struct saved_fcn *next;
  };


struct saved_bf_symnum
  {
    long symnum_fcn;		/* Symnum of function (i.e. .function directive) */
    long symnum_bf;		/* Symnum of .bf for this function */
    struct saved_bf_symnum *next;
  };

typedef struct saved_fcn SAVED_FUNCTION, *SAVED_FUNCTION_PTR;
typedef struct saved_bf_symnum SAVED_BF, *SAVED_BF_PTR;

/* Local functions */

extern void _initialize_f_language (void);
#if 0
static void clear_function_list (void);
static long get_bf_for_fcn (long);
static void clear_bf_list (void);
static void patch_all_commons_by_name (char *, CORE_ADDR, int);
static SAVED_F77_COMMON_PTR find_first_common_named (char *);
static void add_common_entry (struct symbol *);
static void add_common_block (char *, CORE_ADDR, int, char *);
static SAVED_FUNCTION *allocate_saved_function_node (void);
static SAVED_BF_PTR allocate_saved_bf_node (void);
static COMMON_ENTRY_PTR allocate_common_entry_node (void);
static SAVED_F77_COMMON_PTR allocate_saved_f77_common_node (void);
static void patch_common_entries (SAVED_F77_COMMON_PTR, CORE_ADDR, int);
#endif

static struct type *f_create_fundamental_type (struct objfile *, int);
static void f_printstr (struct ui_file * stream, char *string,
			unsigned int length, int width,
			int force_ellipses);
static void f_printchar (int c, struct ui_file * stream);
static void f_emit_char (int c, struct ui_file * stream, int quoter);

/* Print the character C on STREAM as part of the contents of a literal
   string whose delimiter is QUOTER.  Note that that format for printing
   characters and strings is language specific.
   FIXME:  This is a copy of the same function from c-exp.y.  It should
   be replaced with a true F77 version.  */

static void
f_emit_char (int c, struct ui_file *stream, int quoter)
{
  c &= 0xFF;			/* Avoid sign bit follies */

  if (PRINT_LITERAL_FORM (c))
    {
      if (c == '\\' || c == quoter)
	fputs_filtered ("\\", stream);
      fprintf_filtered (stream, "%c", c);
    }
  else
    {
      switch (c)
	{
	case '\n':
	  fputs_filtered ("\\n", stream);
	  break;
	case '\b':
	  fputs_filtered ("\\b", stream);
	  break;
	case '\t':
	  fputs_filtered ("\\t", stream);
	  break;
	case '\f':
	  fputs_filtered ("\\f", stream);
	  break;
	case '\r':
	  fputs_filtered ("\\r", stream);
	  break;
	case '\033':
	  fputs_filtered ("\\e", stream);
	  break;
	case '\007':
	  fputs_filtered ("\\a", stream);
	  break;
	default:
	  fprintf_filtered (stream, "\\%.3o", (unsigned int) c);
	  break;
	}
    }
}

/* FIXME:  This is a copy of the same function from c-exp.y.  It should
   be replaced with a true F77version. */

static void
f_printchar (int c, struct ui_file *stream)
{
  fputs_filtered ("'", stream);
  LA_EMIT_CHAR (c, stream, '\'');
  fputs_filtered ("'", stream);
}

/* Print the character string STRING, printing at most LENGTH characters.
   Printing stops early if the number hits print_max; repeat counts
   are printed as appropriate.  Print ellipses at the end if we
   had to stop before printing LENGTH characters, or if FORCE_ELLIPSES.
   FIXME:  This is a copy of the same function from c-exp.y.  It should
   be replaced with a true F77 version. */

static void
f_printstr (struct ui_file *stream, char *string, unsigned int length,
	    int width, int force_ellipses)
{
  unsigned int i;
  unsigned int things_printed = 0;
  int in_quotes = 0;
  int need_comma = 0;

  if (length == 0)
    {
      fputs_filtered ("''", gdb_stdout);
      return;
    }

  for (i = 0; i < length && things_printed < print_max; ++i)
    {
      /* Position of the character we are examining
         to see whether it is repeated.  */
      unsigned int rep1;
      /* Number of repetitions we have detected so far.  */
      unsigned int reps;

      QUIT;

      if (need_comma)
	{
	  fputs_filtered (", ", stream);
	  need_comma = 0;
	}

      rep1 = i + 1;
      reps = 1;
      while (rep1 < length && string[rep1] == string[i])
	{
	  ++rep1;
	  ++reps;
	}

      if (reps > repeat_count_threshold)
	{
	  if (in_quotes)
	    {
	      if (inspect_it)
		fputs_filtered ("\\', ", stream);
	      else
		fputs_filtered ("', ", stream);
	      in_quotes = 0;
	    }
	  f_printchar (string[i], stream);
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
		fputs_filtered ("\\'", stream);
	      else
		fputs_filtered ("'", stream);
	      in_quotes = 1;
	    }
	  LA_EMIT_CHAR (string[i], stream, '"');
	  ++things_printed;
	}
    }

  /* Terminate the quotes if necessary.  */
  if (in_quotes)
    {
      if (inspect_it)
	fputs_filtered ("\\'", stream);
      else
	fputs_filtered ("'", stream);
    }

  if (force_ellipses || i < length)
    fputs_filtered ("...", stream);
}

/* FIXME:  This is a copy of c_create_fundamental_type(), before
   all the non-C types were stripped from it.  Needs to be fixed
   by an experienced F77 programmer. */

static struct type *
f_create_fundamental_type (struct objfile *objfile, int typeid)
{
  struct type *type = NULL;

  switch (typeid)
    {
    case FT_VOID:
      type = init_type (TYPE_CODE_VOID,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "VOID", objfile);
      break;
    case FT_BOOLEAN:
      type = init_type (TYPE_CODE_BOOL,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "boolean", objfile);
      break;
    case FT_STRING:
      type = init_type (TYPE_CODE_STRING,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "string", objfile);
      break;
    case FT_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "character", objfile);
      break;
    case FT_SIGNED_CHAR:
      type = init_type (TYPE_CODE_INT,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			0, "integer*1", objfile);
      break;
    case FT_UNSIGNED_CHAR:
      type = init_type (TYPE_CODE_BOOL,
			TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "logical*1", objfile);
      break;
    case FT_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			0, "integer*2", objfile);
      break;
    case FT_SIGNED_SHORT:
      type = init_type (TYPE_CODE_INT,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			0, "short", objfile);	/* FIXME-fnf */
      break;
    case FT_UNSIGNED_SHORT:
      type = init_type (TYPE_CODE_BOOL,
			TARGET_SHORT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "logical*2", objfile);
      break;
    case FT_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "integer*4", objfile);
      break;
    case FT_SIGNED_INTEGER:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "integer", objfile);		/* FIXME -fnf */
      break;
    case FT_UNSIGNED_INTEGER:
      type = init_type (TYPE_CODE_BOOL,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, "logical*4", objfile);
      break;
    case FT_FIXED_DECIMAL:
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "fixed decimal", objfile);
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
			0, "real", objfile);
      break;
    case FT_DBL_PREC_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "real*8", objfile);
      break;
    case FT_FLOAT_DECIMAL:
      type = init_type (TYPE_CODE_FLT,
			TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "floating decimal", objfile);
      break;
    case FT_EXT_PREC_FLOAT:
      type = init_type (TYPE_CODE_FLT,
			TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "real*16", objfile);
      break;
    case FT_COMPLEX:
      type = init_type (TYPE_CODE_COMPLEX,
			2 * TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
			0, "complex*8", objfile);
      TYPE_TARGET_TYPE (type) = builtin_type_f_real;
      break;
    case FT_DBL_PREC_COMPLEX:
      type = init_type (TYPE_CODE_COMPLEX,
			2 * TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "complex*16", objfile);
      TYPE_TARGET_TYPE (type) = builtin_type_f_real_s8;
      break;
    case FT_EXT_PREC_COMPLEX:
      type = init_type (TYPE_CODE_COMPLEX,
			2 * TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
			0, "complex*32", objfile);
      TYPE_TARGET_TYPE (type) = builtin_type_f_real_s16;
      break;
    default:
      /* FIXME:  For now, if we are asked to produce a type not in this
         language, create the equivalent of a C integer type with the
         name "<?type?>".  When all the dust settles from the type
         reconstruction work, this should probably become an error. */
      type = init_type (TYPE_CODE_INT,
			TARGET_INT_BIT / TARGET_CHAR_BIT,
			0, "<?type?>", objfile);
      warning ("internal error: no F77 fundamental type %d", typeid);
      break;
    }
  return (type);
}


/* Table of operators and their precedences for printing expressions.  */

static const struct op_print f_op_print_tab[] =
{
  {"+", BINOP_ADD, PREC_ADD, 0},
  {"+", UNOP_PLUS, PREC_PREFIX, 0},
  {"-", BINOP_SUB, PREC_ADD, 0},
  {"-", UNOP_NEG, PREC_PREFIX, 0},
  {"*", BINOP_MUL, PREC_MUL, 0},
  {"/", BINOP_DIV, PREC_MUL, 0},
  {"DIV", BINOP_INTDIV, PREC_MUL, 0},
  {"MOD", BINOP_REM, PREC_MUL, 0},
  {"=", BINOP_ASSIGN, PREC_ASSIGN, 1},
  {".OR.", BINOP_LOGICAL_OR, PREC_LOGICAL_OR, 0},
  {".AND.", BINOP_LOGICAL_AND, PREC_LOGICAL_AND, 0},
  {".NOT.", UNOP_LOGICAL_NOT, PREC_PREFIX, 0},
  {".EQ.", BINOP_EQUAL, PREC_EQUAL, 0},
  {".NE.", BINOP_NOTEQUAL, PREC_EQUAL, 0},
  {".LE.", BINOP_LEQ, PREC_ORDER, 0},
  {".GE.", BINOP_GEQ, PREC_ORDER, 0},
  {".GT.", BINOP_GTR, PREC_ORDER, 0},
  {".LT.", BINOP_LESS, PREC_ORDER, 0},
  {"**", UNOP_IND, PREC_PREFIX, 0},
  {"@", BINOP_REPEAT, PREC_REPEAT, 0},
  {NULL, 0, 0, 0}
};

struct type **const (f_builtin_types[]) =
{
  &builtin_type_f_character,
    &builtin_type_f_logical,
    &builtin_type_f_logical_s1,
    &builtin_type_f_logical_s2,
    &builtin_type_f_integer,
    &builtin_type_f_integer_s2,
    &builtin_type_f_real,
    &builtin_type_f_real_s8,
    &builtin_type_f_real_s16,
    &builtin_type_f_complex_s8,
    &builtin_type_f_complex_s16,
#if 0
    &builtin_type_f_complex_s32,
#endif
    &builtin_type_f_void,
    0
};

/* This is declared in c-lang.h but it is silly to import that file for what
   is already just a hack. */
extern int c_value_print (struct value *, struct ui_file *, int,
			  enum val_prettyprint);

const struct language_defn f_language_defn =
{
  "fortran",
  language_fortran,
  f_builtin_types,
  range_check_on,
  type_check_on,
  case_sensitive_off,
  &exp_descriptor_standard,
  f_parse,			/* parser */
  f_error,			/* parser error function */
  f_printchar,			/* Print character constant */
  f_printstr,			/* function to print string constant */
  f_emit_char,			/* Function to print a single character */
  f_create_fundamental_type,	/* Create fundamental type in this language */
  f_print_type,			/* Print a type using appropriate syntax */
  f_val_print,			/* Print a value using appropriate syntax */
  c_value_print,		/* FIXME */
  NULL,				/* Language specific skip_trampoline */
  value_of_this,		/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  {"", "", "", ""},		/* Binary format info */
  {"0%o", "0", "o", ""},	/* Octal format info */
  {"%d", "", "d", ""},		/* Decimal format info */
  {"0x%x", "0x", "x", ""},	/* Hex format info */
  f_op_print_tab,		/* expression operators for printing */
  0,				/* arrays are first-class (not c-style) */
  1,				/* String lower bound */
  &builtin_type_f_character,	/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

static void
build_fortran_types (void)
{
  builtin_type_f_void =
    init_type (TYPE_CODE_VOID, 1,
	       0,
	       "VOID", (struct objfile *) NULL);

  builtin_type_f_character =
    init_type (TYPE_CODE_INT, TARGET_CHAR_BIT / TARGET_CHAR_BIT,
	       0,
	       "character", (struct objfile *) NULL);

  builtin_type_f_logical_s1 =
    init_type (TYPE_CODE_BOOL, TARGET_CHAR_BIT / TARGET_CHAR_BIT,
	       TYPE_FLAG_UNSIGNED,
	       "logical*1", (struct objfile *) NULL);

  builtin_type_f_integer_s2 =
    init_type (TYPE_CODE_INT, TARGET_SHORT_BIT / TARGET_CHAR_BIT,
	       0,
	       "integer*2", (struct objfile *) NULL);

  builtin_type_f_logical_s2 =
    init_type (TYPE_CODE_BOOL, TARGET_SHORT_BIT / TARGET_CHAR_BIT,
	       TYPE_FLAG_UNSIGNED,
	       "logical*2", (struct objfile *) NULL);

  builtin_type_f_integer =
    init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
	       0,
	       "integer", (struct objfile *) NULL);

  builtin_type_f_logical =
    init_type (TYPE_CODE_BOOL, TARGET_INT_BIT / TARGET_CHAR_BIT,
	       TYPE_FLAG_UNSIGNED,
	       "logical*4", (struct objfile *) NULL);

  builtin_type_f_real =
    init_type (TYPE_CODE_FLT, TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
	       0,
	       "real", (struct objfile *) NULL);

  builtin_type_f_real_s8 =
    init_type (TYPE_CODE_FLT, TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0,
	       "real*8", (struct objfile *) NULL);

  builtin_type_f_real_s16 =
    init_type (TYPE_CODE_FLT, TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0,
	       "real*16", (struct objfile *) NULL);

  builtin_type_f_complex_s8 =
    init_type (TYPE_CODE_COMPLEX, 2 * TARGET_FLOAT_BIT / TARGET_CHAR_BIT,
	       0,
	       "complex*8", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (builtin_type_f_complex_s8) = builtin_type_f_real;

  builtin_type_f_complex_s16 =
    init_type (TYPE_CODE_COMPLEX, 2 * TARGET_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0,
	       "complex*16", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (builtin_type_f_complex_s16) = builtin_type_f_real_s8;

  /* We have a new size == 4 double floats for the
     complex*32 data type */

  builtin_type_f_complex_s32 =
    init_type (TYPE_CODE_COMPLEX, 2 * TARGET_LONG_DOUBLE_BIT / TARGET_CHAR_BIT,
	       0,
	       "complex*32", (struct objfile *) NULL);
  TYPE_TARGET_TYPE (builtin_type_f_complex_s32) = builtin_type_f_real_s16;
}

void
_initialize_f_language (void)
{
  build_fortran_types ();

  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_character);
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_logical); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_logical_s1); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_logical_s2); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_integer); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_integer_s2); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_real); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_real_s8); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_real_s16); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_complex_s8); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_complex_s16); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_complex_s32); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_f_void); 
  DEPRECATED_REGISTER_GDBARCH_SWAP (builtin_type_string); 
  deprecated_register_gdbarch_swap (NULL, 0, build_fortran_types);

  builtin_type_string =
    init_type (TYPE_CODE_STRING, TARGET_CHAR_BIT / TARGET_CHAR_BIT,
	       0,
	       "character string", (struct objfile *) NULL);

  add_language (&f_language_defn);
}

#if 0
static SAVED_BF_PTR
allocate_saved_bf_node (void)
{
  SAVED_BF_PTR new;

  new = (SAVED_BF_PTR) xmalloc (sizeof (SAVED_BF));
  return (new);
}

static SAVED_FUNCTION *
allocate_saved_function_node (void)
{
  SAVED_FUNCTION *new;

  new = (SAVED_FUNCTION *) xmalloc (sizeof (SAVED_FUNCTION));
  return (new);
}

static SAVED_F77_COMMON_PTR
allocate_saved_f77_common_node (void)
{
  SAVED_F77_COMMON_PTR new;

  new = (SAVED_F77_COMMON_PTR) xmalloc (sizeof (SAVED_F77_COMMON));
  return (new);
}

static COMMON_ENTRY_PTR
allocate_common_entry_node (void)
{
  COMMON_ENTRY_PTR new;

  new = (COMMON_ENTRY_PTR) xmalloc (sizeof (COMMON_ENTRY));
  return (new);
}
#endif

SAVED_F77_COMMON_PTR head_common_list = NULL;	/* Ptr to 1st saved COMMON  */
SAVED_F77_COMMON_PTR tail_common_list = NULL;	/* Ptr to last saved COMMON  */
SAVED_F77_COMMON_PTR current_common = NULL;	/* Ptr to current COMMON */

#if 0
static SAVED_BF_PTR saved_bf_list = NULL;	/* Ptr to (.bf,function) 
						   list */
static SAVED_BF_PTR saved_bf_list_end = NULL;	/* Ptr to above list's end */
static SAVED_BF_PTR current_head_bf_list = NULL;	/* Current head of above list
							 */

static SAVED_BF_PTR tmp_bf_ptr;	/* Generic temporary for use 
				   in macros */

/* The following function simply enters a given common block onto 
   the global common block chain */

static void
add_common_block (char *name, CORE_ADDR offset, int secnum, char *func_stab)
{
  SAVED_F77_COMMON_PTR tmp;
  char *c, *local_copy_func_stab;

  /* If the COMMON block we are trying to add has a blank 
     name (i.e. "#BLNK_COM") then we set it to __BLANK
     because the darn "#" character makes GDB's input 
     parser have fits. */


  if (strcmp (name, BLANK_COMMON_NAME_ORIGINAL) == 0
      || strcmp (name, BLANK_COMMON_NAME_MF77) == 0)
    {

      xfree (name);
      name = alloca (strlen (BLANK_COMMON_NAME_LOCAL) + 1);
      strcpy (name, BLANK_COMMON_NAME_LOCAL);
    }

  tmp = allocate_saved_f77_common_node ();

  local_copy_func_stab = xmalloc (strlen (func_stab) + 1);
  strcpy (local_copy_func_stab, func_stab);

  tmp->name = xmalloc (strlen (name) + 1);

  /* local_copy_func_stab is a stabstring, let us first extract the 
     function name from the stab by NULLing out the ':' character. */


  c = NULL;
  c = strchr (local_copy_func_stab, ':');

  if (c)
    *c = '\0';
  else
    error ("Malformed function STAB found in add_common_block()");


  tmp->owning_function = xmalloc (strlen (local_copy_func_stab) + 1);

  strcpy (tmp->owning_function, local_copy_func_stab);

  strcpy (tmp->name, name);
  tmp->offset = offset;
  tmp->next = NULL;
  tmp->entries = NULL;
  tmp->secnum = secnum;

  current_common = tmp;

  if (head_common_list == NULL)
    {
      head_common_list = tail_common_list = tmp;
    }
  else
    {
      tail_common_list->next = tmp;
      tail_common_list = tmp;
    }
}
#endif

/* The following function simply enters a given common entry onto 
   the "current_common" block that has been saved away. */

#if 0
static void
add_common_entry (struct symbol *entry_sym_ptr)
{
  COMMON_ENTRY_PTR tmp;



  /* The order of this list is important, since 
     we expect the entries to appear in decl.
     order when we later issue "info common" calls */

  tmp = allocate_common_entry_node ();

  tmp->next = NULL;
  tmp->symbol = entry_sym_ptr;

  if (current_common == NULL)
    error ("Attempt to add COMMON entry with no block open!");
  else
    {
      if (current_common->entries == NULL)
	{
	  current_common->entries = tmp;
	  current_common->end_of_entries = tmp;
	}
      else
	{
	  current_common->end_of_entries->next = tmp;
	  current_common->end_of_entries = tmp;
	}
    }
}
#endif

/* This routine finds the first encountred COMMON block named "name" */

#if 0
static SAVED_F77_COMMON_PTR
find_first_common_named (char *name)
{

  SAVED_F77_COMMON_PTR tmp;

  tmp = head_common_list;

  while (tmp != NULL)
    {
      if (strcmp (tmp->name, name) == 0)
	return (tmp);
      else
	tmp = tmp->next;
    }
  return (NULL);
}
#endif

/* This routine finds the first encountred COMMON block named "name" 
   that belongs to function funcname */

SAVED_F77_COMMON_PTR
find_common_for_function (char *name, char *funcname)
{

  SAVED_F77_COMMON_PTR tmp;

  tmp = head_common_list;

  while (tmp != NULL)
    {
      if (DEPRECATED_STREQ (tmp->name, name)
	  && DEPRECATED_STREQ (tmp->owning_function, funcname))
	return (tmp);
      else
	tmp = tmp->next;
    }
  return (NULL);
}


#if 0

/* The following function is called to patch up the offsets 
   for the statics contained in the COMMON block named
   "name."  */

static void
patch_common_entries (SAVED_F77_COMMON_PTR blk, CORE_ADDR offset, int secnum)
{
  COMMON_ENTRY_PTR entry;

  blk->offset = offset;		/* Keep this around for future use. */

  entry = blk->entries;

  while (entry != NULL)
    {
      SYMBOL_VALUE (entry->symbol) += offset;
      SYMBOL_SECTION (entry->symbol) = secnum;

      entry = entry->next;
    }
  blk->secnum = secnum;
}

/* Patch all commons named "name" that need patching.Since COMMON
   blocks occur with relative infrequency, we simply do a linear scan on
   the name.  Eventually, the best way to do this will be a
   hashed-lookup.  Secnum is the section number for the .bss section
   (which is where common data lives). */

static void
patch_all_commons_by_name (char *name, CORE_ADDR offset, int secnum)
{

  SAVED_F77_COMMON_PTR tmp;

  /* For blank common blocks, change the canonical reprsentation 
     of a blank name */

  if (strcmp (name, BLANK_COMMON_NAME_ORIGINAL) == 0
      || strcmp (name, BLANK_COMMON_NAME_MF77) == 0)
    {
      xfree (name);
      name = alloca (strlen (BLANK_COMMON_NAME_LOCAL) + 1);
      strcpy (name, BLANK_COMMON_NAME_LOCAL);
    }

  tmp = head_common_list;

  while (tmp != NULL)
    {
      if (COMMON_NEEDS_PATCHING (tmp))
	if (strcmp (tmp->name, name) == 0)
	  patch_common_entries (tmp, offset, secnum);

      tmp = tmp->next;
    }
}
#endif

/* This macro adds the symbol-number for the start of the function 
   (the symbol number of the .bf) referenced by symnum_fcn to a 
   list.  This list, in reality should be a FIFO queue but since 
   #line pragmas sometimes cause line ranges to get messed up 
   we simply create a linear list.  This list can then be searched 
   first by a queueing algorithm and upon failure fall back to 
   a linear scan. */

#if 0
#define ADD_BF_SYMNUM(bf_sym,fcn_sym) \
  \
  if (saved_bf_list == NULL) \
{ \
    tmp_bf_ptr = allocate_saved_bf_node(); \
      \
	tmp_bf_ptr->symnum_bf = (bf_sym); \
	  tmp_bf_ptr->symnum_fcn = (fcn_sym);  \
	    tmp_bf_ptr->next = NULL; \
	      \
		current_head_bf_list = saved_bf_list = tmp_bf_ptr; \
		  saved_bf_list_end = tmp_bf_ptr; \
		  } \
else \
{  \
     tmp_bf_ptr = allocate_saved_bf_node(); \
       \
         tmp_bf_ptr->symnum_bf = (bf_sym);  \
	   tmp_bf_ptr->symnum_fcn = (fcn_sym);  \
	     tmp_bf_ptr->next = NULL;  \
	       \
		 saved_bf_list_end->next = tmp_bf_ptr;  \
		   saved_bf_list_end = tmp_bf_ptr; \
		   }
#endif

/* This function frees the entire (.bf,function) list */

#if 0
static void
clear_bf_list (void)
{

  SAVED_BF_PTR tmp = saved_bf_list;
  SAVED_BF_PTR next = NULL;

  while (tmp != NULL)
    {
      next = tmp->next;
      xfree (tmp);
      tmp = next;
    }
  saved_bf_list = NULL;
}
#endif

int global_remote_debug;

#if 0

static long
get_bf_for_fcn (long the_function)
{
  SAVED_BF_PTR tmp;
  int nprobes = 0;

  /* First use a simple queuing algorithm (i.e. look and see if the 
     item at the head of the queue is the one you want)  */

  if (saved_bf_list == NULL)
    internal_error (__FILE__, __LINE__,
		    "cannot get .bf node off empty list");

  if (current_head_bf_list != NULL)
    if (current_head_bf_list->symnum_fcn == the_function)
      {
	if (global_remote_debug)
	  fprintf_unfiltered (gdb_stderr, "*");

	tmp = current_head_bf_list;
	current_head_bf_list = current_head_bf_list->next;
	return (tmp->symnum_bf);
      }

  /* If the above did not work (probably because #line directives were 
     used in the sourcefile and they messed up our internal tables) we now do
     the ugly linear scan */

  if (global_remote_debug)
    fprintf_unfiltered (gdb_stderr, "\ndefaulting to linear scan\n");

  nprobes = 0;
  tmp = saved_bf_list;
  while (tmp != NULL)
    {
      nprobes++;
      if (tmp->symnum_fcn == the_function)
	{
	  if (global_remote_debug)
	    fprintf_unfiltered (gdb_stderr, "Found in %d probes\n", nprobes);
	  current_head_bf_list = tmp->next;
	  return (tmp->symnum_bf);
	}
      tmp = tmp->next;
    }

  return (-1);
}

static SAVED_FUNCTION_PTR saved_function_list = NULL;
static SAVED_FUNCTION_PTR saved_function_list_end = NULL;

static void
clear_function_list (void)
{
  SAVED_FUNCTION_PTR tmp = saved_function_list;
  SAVED_FUNCTION_PTR next = NULL;

  while (tmp != NULL)
    {
      next = tmp->next;
      xfree (tmp);
      tmp = next;
    }

  saved_function_list = NULL;
}
#endif
