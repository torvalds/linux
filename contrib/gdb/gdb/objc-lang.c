/* Objective-C language support routines for GDB, the GNU debugger.

   Copyright 2002, 2003, 2004 Free Software Foundation, Inc.

   Contributed by Apple Computer, Inc.
   Written by Michael Snyder.

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
#include "objc-lang.h"
#include "complaints.h"
#include "value.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdb_string.h"		/* for strchr */
#include "target.h"		/* for target_has_execution */
#include "gdbcore.h"
#include "gdbcmd.h"
#include "frame.h"
#include "gdb_regex.h"
#include "regcache.h"
#include "block.h"
#include "infcall.h"
#include "valprint.h"
#include "gdb_assert.h"

#include <ctype.h>

struct objc_object {
  CORE_ADDR isa;
};

struct objc_class {
  CORE_ADDR isa; 
  CORE_ADDR super_class; 
  CORE_ADDR name;               
  long version;
  long info;
  long instance_size;
  CORE_ADDR ivars;
  CORE_ADDR methods;
  CORE_ADDR cache;
  CORE_ADDR protocols;
};

struct objc_super {
  CORE_ADDR receiver;
  CORE_ADDR class;
};

struct objc_method {
  CORE_ADDR name;
  CORE_ADDR types;
  CORE_ADDR imp;
};

/* Lookup a structure type named "struct NAME", visible in lexical
   block BLOCK.  If NOERR is nonzero, return zero if NAME is not
   suitably defined.  */

struct symbol *
lookup_struct_typedef (char *name, struct block *block, int noerr)
{
  struct symbol *sym;

  sym = lookup_symbol (name, block, STRUCT_DOMAIN, 0, 
		       (struct symtab **) NULL);

  if (sym == NULL)
    {
      if (noerr)
	return 0;
      else 
	error ("No struct type named %s.", name);
    }
  if (TYPE_CODE (SYMBOL_TYPE (sym)) != TYPE_CODE_STRUCT)
    {
      if (noerr)
	return 0;
      else
	error ("This context has class, union or enum %s, not a struct.", 
	       name);
    }
  return sym;
}

CORE_ADDR 
lookup_objc_class (char *classname)
{
  struct value * function, *classval;

  if (! target_has_execution)
    {
      /* Can't call into inferior to lookup class.  */
      return 0;
    }

  if (lookup_minimal_symbol("objc_lookUpClass", 0, 0))
    function = find_function_in_inferior("objc_lookUpClass");
  else if (lookup_minimal_symbol ("objc_lookup_class", 0, 0))
    function = find_function_in_inferior("objc_lookup_class");
  else
    {
      complaint (&symfile_complaints, "no way to lookup Objective-C classes");
      return 0;
    }

  classval = value_string (classname, strlen (classname) + 1);
  classval = value_coerce_array (classval);
  return (CORE_ADDR) value_as_long (call_function_by_hand (function, 
							   1, &classval));
}

CORE_ADDR
lookup_child_selector (char *selname)
{
  struct value * function, *selstring;

  if (! target_has_execution)
    {
      /* Can't call into inferior to lookup selector.  */
      return 0;
    }

  if (lookup_minimal_symbol("sel_getUid", 0, 0))
    function = find_function_in_inferior("sel_getUid");
  else if (lookup_minimal_symbol ("sel_get_any_uid", 0, 0))
    function = find_function_in_inferior("sel_get_any_uid");
  else
    {
      complaint (&symfile_complaints, "no way to lookup Objective-C selectors");
      return 0;
    }

  selstring = value_coerce_array (value_string (selname, 
						strlen (selname) + 1));
  return value_as_long (call_function_by_hand (function, 1, &selstring));
}

struct value * 
value_nsstring (char *ptr, int len)
{
  struct value *stringValue[3];
  struct value *function, *nsstringValue;
  struct symbol *sym;
  struct type *type;

  if (!target_has_execution)
    return 0;		/* Can't call into inferior to create NSString.  */

  sym = lookup_struct_typedef("NSString", 0, 1);
  if (sym == NULL)
    sym = lookup_struct_typedef("NXString", 0, 1);
  if (sym == NULL)
    type = lookup_pointer_type(builtin_type_void);
  else
    type = lookup_pointer_type(SYMBOL_TYPE (sym));

  stringValue[2] = value_string(ptr, len);
  stringValue[2] = value_coerce_array(stringValue[2]);
  /* _NSNewStringFromCString replaces "istr" after Lantern2A.  */
  if (lookup_minimal_symbol("_NSNewStringFromCString", 0, 0))
    {
      function = find_function_in_inferior("_NSNewStringFromCString");
      nsstringValue = call_function_by_hand(function, 1, &stringValue[2]);
    }
  else if (lookup_minimal_symbol("istr", 0, 0))
    {
      function = find_function_in_inferior("istr");
      nsstringValue = call_function_by_hand(function, 1, &stringValue[2]);
    }
  else if (lookup_minimal_symbol("+[NSString stringWithCString:]", 0, 0))
    {
      function = find_function_in_inferior("+[NSString stringWithCString:]");
      stringValue[0] = value_from_longest 
	(builtin_type_long, lookup_objc_class ("NSString"));
      stringValue[1] = value_from_longest 
	(builtin_type_long, lookup_child_selector ("stringWithCString:"));
      nsstringValue = call_function_by_hand(function, 3, &stringValue[0]);
    }
  else
    error ("NSString: internal error -- no way to create new NSString");

  VALUE_TYPE(nsstringValue) = type;
  return nsstringValue;
}

/* Objective-C name demangling.  */

char *
objc_demangle (const char *mangled, int options)
{
  char *demangled, *cp;

  if (mangled[0] == '_' &&
     (mangled[1] == 'i' || mangled[1] == 'c') &&
      mangled[2] == '_')
    {
      cp = demangled = xmalloc(strlen(mangled) + 2);

      if (mangled[1] == 'i')
	*cp++ = '-';		/* for instance method */
      else
	*cp++ = '+';		/* for class    method */

      *cp++ = '[';		/* opening left brace  */
      strcpy(cp, mangled+3);	/* tack on the rest of the mangled name */

      while (*cp && *cp == '_')
	cp++;			/* skip any initial underbars in class name */

      cp = strchr(cp, '_');
      if (!cp)	                /* find first non-initial underbar */
	{
	  xfree(demangled);	/* not mangled name */
	  return NULL;
	}
      if (cp[1] == '_') {	/* easy case: no category name     */
	*cp++ = ' ';		/* replace two '_' with one ' '    */
	strcpy(cp, mangled + (cp - demangled) + 2);
      }
      else {
	*cp++ = '(';		/* less easy case: category name */
	cp = strchr(cp, '_');
	if (!cp)
	  {
	    xfree(demangled);	/* not mangled name */
	    return NULL;
	  }
	*cp++ = ')';
	*cp++ = ' ';		/* overwriting 1st char of method name...  */
	strcpy(cp, mangled + (cp - demangled));	/* get it back */
      }

      while (*cp && *cp == '_')
	cp++;			/* skip any initial underbars in method name */

      for (; *cp; cp++)
	if (*cp == '_')
	  *cp = ':';		/* replace remaining '_' with ':' */

      *cp++ = ']';		/* closing right brace */
      *cp++ = 0;		/* string terminator */
      return demangled;
    }
  else
    return NULL;	/* Not an objc mangled name.  */
}

/* Print the character C on STREAM as part of the contents of a
   literal string whose delimiter is QUOTER.  Note that that format
   for printing characters and strings is language specific.  */

static void
objc_emit_char (int c, struct ui_file *stream, int quoter)
{

  c &= 0xFF;			/* Avoid sign bit follies.  */

  if (PRINT_LITERAL_FORM (c))
    {
      if (c == '\\' || c == quoter)
	{
	  fputs_filtered ("\\", stream);
	}
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

static void
objc_printchar (int c, struct ui_file *stream)
{
  fputs_filtered ("'", stream);
  objc_emit_char (c, stream, '\'');
  fputs_filtered ("'", stream);
}

/* Print the character string STRING, printing at most LENGTH
   characters.  Printing stops early if the number hits print_max;
   repeat counts are printed as appropriate.  Print ellipses at the
   end if we had to stop before printing LENGTH characters, or if
   FORCE_ELLIPSES.  */

static void
objc_printstr (struct ui_file *stream, char *string, 
	       unsigned int length, int width, int force_ellipses)
{
  unsigned int i;
  unsigned int things_printed = 0;
  int in_quotes = 0;
  int need_comma = 0;

  /* If the string was not truncated due to `set print elements', and
     the last byte of it is a null, we don't print that, in
     traditional C style.  */
  if ((!force_ellipses) && length > 0 && string[length-1] == '\0')
    length--;

  if (length == 0)
    {
      fputs_filtered ("\"\"", stream);
      return;
    }

  for (i = 0; i < length && things_printed < print_max; ++i)
    {
      /* Position of the character we are examining to see whether it
	 is repeated.  */
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
		fputs_filtered ("\\\", ", stream);
	      else
		fputs_filtered ("\", ", stream);
	      in_quotes = 0;
	    }
	  objc_printchar (string[i], stream);
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
	  objc_emit_char (string[i], stream, '"');
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

/* Create a fundamental C type using default reasonable for the
   current target.

   Some object/debugging file formats (DWARF version 1, COFF, etc) do
   not define fundamental types such as "int" or "double".  Others
   (stabs or DWARF version 2, etc) do define fundamental types.  For
   the formats which don't provide fundamental types, gdb can create
   such types using this function.

   FIXME: Some compilers distinguish explicitly signed integral types
   (signed short, signed int, signed long) from "regular" integral
   types (short, int, long) in the debugging information.  There is
   some disagreement as to how useful this feature is.  In particular,
   gcc does not support this.  Also, only some debugging formats allow
   the distinction to be passed on to a debugger.  For now, we always
   just use "short", "int", or "long" as the type name, for both the
   implicit and explicitly signed types.  This also makes life easier
   for the gdb test suite since we don't have to account for the
   differences in output depending upon what the compiler and
   debugging format support.  We will probably have to re-examine the
   issue when gdb starts taking it's fundamental type information
   directly from the debugging information supplied by the compiler.
   fnf@cygnus.com */

static struct type *
objc_create_fundamental_type (struct objfile *objfile, int typeid)
{
  struct type *type = NULL;

  switch (typeid)
    {
      default:
	/* FIXME: For now, if we are asked to produce a type not in
	   this language, create the equivalent of a C integer type
	   with the name "<?type?>".  When all the dust settles from
	   the type reconstruction work, this should probably become
	   an error.  */
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
      case FT_CHAR:
	type = init_type (TYPE_CODE_INT,
			  TARGET_CHAR_BIT / TARGET_CHAR_BIT,
			  0, "char", objfile);
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
			  0, "int", objfile); /* FIXME -fnf */
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
			  0, "long", objfile); /* FIXME -fnf */
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
      }
  return (type);
}

/* Determine if we are currently in the Objective-C dispatch function.
   If so, get the address of the method function that the dispatcher
   would call and use that as the function to step into instead. Also
   skip over the trampoline for the function (if any).  This is better
   for the user since they are only interested in stepping into the
   method function anyway.  */
static CORE_ADDR 
objc_skip_trampoline (CORE_ADDR stop_pc)
{
  CORE_ADDR real_stop_pc;
  CORE_ADDR method_stop_pc;
  
  real_stop_pc = SKIP_TRAMPOLINE_CODE (stop_pc);

  if (real_stop_pc != 0)
    find_objc_msgcall (real_stop_pc, &method_stop_pc);
  else
    find_objc_msgcall (stop_pc, &method_stop_pc);

  if (method_stop_pc)
    {
      real_stop_pc = SKIP_TRAMPOLINE_CODE (method_stop_pc);
      if (real_stop_pc == 0)
	real_stop_pc = method_stop_pc;
    }

  return real_stop_pc;
}


/* Table mapping opcodes into strings for printing operators
   and precedences of the operators.  */

static const struct op_print objc_op_print_tab[] =
  {
    {",",  BINOP_COMMA, PREC_COMMA, 0},
    {"=",  BINOP_ASSIGN, PREC_ASSIGN, 1},
    {"||", BINOP_LOGICAL_OR, PREC_LOGICAL_OR, 0},
    {"&&", BINOP_LOGICAL_AND, PREC_LOGICAL_AND, 0},
    {"|",  BINOP_BITWISE_IOR, PREC_BITWISE_IOR, 0},
    {"^",  BINOP_BITWISE_XOR, PREC_BITWISE_XOR, 0},
    {"&",  BINOP_BITWISE_AND, PREC_BITWISE_AND, 0},
    {"==", BINOP_EQUAL, PREC_EQUAL, 0},
    {"!=", BINOP_NOTEQUAL, PREC_EQUAL, 0},
    {"<=", BINOP_LEQ, PREC_ORDER, 0},
    {">=", BINOP_GEQ, PREC_ORDER, 0},
    {">",  BINOP_GTR, PREC_ORDER, 0},
    {"<",  BINOP_LESS, PREC_ORDER, 0},
    {">>", BINOP_RSH, PREC_SHIFT, 0},
    {"<<", BINOP_LSH, PREC_SHIFT, 0},
    {"+",  BINOP_ADD, PREC_ADD, 0},
    {"-",  BINOP_SUB, PREC_ADD, 0},
    {"*",  BINOP_MUL, PREC_MUL, 0},
    {"/",  BINOP_DIV, PREC_MUL, 0},
    {"%",  BINOP_REM, PREC_MUL, 0},
    {"@",  BINOP_REPEAT, PREC_REPEAT, 0},
    {"-",  UNOP_NEG, PREC_PREFIX, 0},
    {"!",  UNOP_LOGICAL_NOT, PREC_PREFIX, 0},
    {"~",  UNOP_COMPLEMENT, PREC_PREFIX, 0},
    {"*",  UNOP_IND, PREC_PREFIX, 0},
    {"&",  UNOP_ADDR, PREC_PREFIX, 0},
    {"sizeof ", UNOP_SIZEOF, PREC_PREFIX, 0},
    {"++", UNOP_PREINCREMENT, PREC_PREFIX, 0},
    {"--", UNOP_PREDECREMENT, PREC_PREFIX, 0},
    {NULL, OP_NULL, PREC_NULL, 0}
};

struct type ** const (objc_builtin_types[]) = 
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

const struct language_defn objc_language_defn = {
  "objective-c",		/* Language name */
  language_objc,
  objc_builtin_types,
  range_check_off,
  type_check_off,
  case_sensitive_on,
  &exp_descriptor_standard,
  objc_parse,
  objc_error,
  objc_printchar,		/* Print a character constant */
  objc_printstr,		/* Function to print string constant */
  objc_emit_char,
  objc_create_fundamental_type,	/* Create fundamental type in this language */
  c_print_type,			/* Print a type using appropriate syntax */
  c_val_print,			/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  objc_skip_trampoline, 	/* Language specific skip_trampoline */
  value_of_this,		/* value_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  objc_demangle,		/* Language specific symbol demangler */
  {"",     "",    "",  ""},	/* Binary format info */
  {"0%lo",  "0",   "o", ""},	/* Octal format info */
  {"%ld",   "",    "d", ""},	/* Decimal format info */
  {"0x%lx", "0x",  "x", ""},	/* Hex format info */
  objc_op_print_tab,		/* Expression operators for printing */
  1,				/* C-style arrays */
  0,				/* String lower bound */
  &builtin_type_char,		/* Type of string elements */
  default_word_break_characters,
  LANG_MAGIC
};

/*
 * ObjC:
 * Following functions help construct Objective-C message calls 
 */

struct selname		/* For parsing Objective-C.  */
  {
    struct selname *next;
    char *msglist_sel;
    int msglist_len;
  };

static int msglist_len;
static struct selname *selname_chain;
static char *msglist_sel;

void
start_msglist(void)
{
  struct selname *new = 
    (struct selname *) xmalloc (sizeof (struct selname));

  new->next = selname_chain;
  new->msglist_len = msglist_len;
  new->msglist_sel = msglist_sel;
  msglist_len = 0;
  msglist_sel = (char *)xmalloc(1);
  *msglist_sel = 0;
  selname_chain = new;
}

void
add_msglist(struct stoken *str, int addcolon)
{
  char *s, *p;
  int len, plen;

  if (str == 0) {		/* Unnamed arg, or...  */
    if (addcolon == 0) {	/* variable number of args.  */
      msglist_len++;
      return;
    }
    p = "";
    plen = 0;
  } else {
    p = str->ptr;
    plen = str->length;
  }
  len = plen + strlen(msglist_sel) + 2;
  s = (char *)xmalloc(len);
  strcpy(s, msglist_sel);
  strncat(s, p, plen);
  xfree(msglist_sel);
  msglist_sel = s;
  if (addcolon) {
    s[len-2] = ':';
    s[len-1] = 0;
    msglist_len++;
  } else
    s[len-2] = '\0';
}

int
end_msglist(void)
{
  int val = msglist_len;
  struct selname *sel = selname_chain;
  char *p = msglist_sel;
  CORE_ADDR selid;

  selname_chain = sel->next;
  msglist_len = sel->msglist_len;
  msglist_sel = sel->msglist_sel;
  selid = lookup_child_selector(p);
  if (!selid)
    error("Can't find selector \"%s\"", p);
  write_exp_elt_longcst (selid);
  xfree(p);
  write_exp_elt_longcst (val);	/* Number of args */
  xfree(sel);

  return val;
}

/*
 * Function: specialcmp (char *a, char *b)
 *
 * Special strcmp: treats ']' and ' ' as end-of-string.
 * Used for qsorting lists of objc methods (either by class or selector).
 */

static int
specialcmp (char *a, char *b)
{
  while (*a && *a != ' ' && *a != ']' && *b && *b != ' ' && *b != ']')
    {
      if (*a != *b)
	return *a - *b;
      a++, b++;
    }
  if (*a && *a != ' ' && *a != ']')
    return  1;		/* a is longer therefore greater */
  if (*b && *b != ' ' && *b != ']')
    return -1;		/* a is shorter therefore lesser */
  return    0;		/* a and b are identical */
}

/*
 * Function: compare_selectors (const void *, const void *)
 *
 * Comparison function for use with qsort.  Arguments are symbols or
 * msymbols Compares selector part of objc method name alphabetically.
 */

static int
compare_selectors (const void *a, const void *b)
{
  char *aname, *bname;

  aname = SYMBOL_PRINT_NAME (*(struct symbol **) a);
  bname = SYMBOL_PRINT_NAME (*(struct symbol **) b);
  if (aname == NULL || bname == NULL)
    error ("internal: compare_selectors(1)");

  aname = strchr(aname, ' ');
  bname = strchr(bname, ' ');
  if (aname == NULL || bname == NULL)
    error ("internal: compare_selectors(2)");

  return specialcmp (aname+1, bname+1);
}

/*
 * Function: selectors_info (regexp, from_tty)
 *
 * Implements the "Info selectors" command.  Takes an optional regexp
 * arg.  Lists all objective c selectors that match the regexp.  Works
 * by grepping thru all symbols for objective c methods.  Output list
 * is sorted and uniqued. 
 */

static void
selectors_info (char *regexp, int from_tty)
{
  struct objfile	*objfile;
  struct minimal_symbol *msymbol;
  char                  *name;
  char                  *val;
  int                    matches = 0;
  int                    maxlen  = 0;
  int                    ix;
  char                   myregexp[2048];
  char                   asel[256];
  struct symbol        **sym_arr;
  int                    plusminus = 0;

  if (regexp == NULL)
    strcpy(myregexp, ".*]");	/* Null input, match all objc methods.  */
  else
    {
      if (*regexp == '+' || *regexp == '-')
	{ /* User wants only class methods or only instance methods.  */
	  plusminus = *regexp++;
	  while (*regexp == ' ' || *regexp == '\t')
	    regexp++;
	}
      if (*regexp == '\0')
	strcpy(myregexp, ".*]");
      else
	{
	  strcpy(myregexp, regexp);
	  if (myregexp[strlen(myregexp) - 1] == '$') /* end of selector */
	    myregexp[strlen(myregexp) - 1] = ']';    /* end of method name */
	  else
	    strcat(myregexp, ".*]");
	}
    }

  if (regexp != NULL)
    {
      val = re_comp (myregexp);
      if (val != 0)
	error ("Invalid regexp (%s): %s", val, regexp);
    }

  /* First time thru is JUST to get max length and count.  */
  ALL_MSYMBOLS (objfile, msymbol)
    {
      QUIT;
      name = SYMBOL_NATURAL_NAME (msymbol);
      if (name &&
	 (name[0] == '-' || name[0] == '+') &&
	  name[1] == '[')		/* Got a method name.  */
	{
	  /* Filter for class/instance methods.  */
	  if (plusminus && name[0] != plusminus)
	    continue;
	  /* Find selector part.  */
	  name = (char *) strchr(name+2, ' ');
	  if (regexp == NULL || re_exec(++name) != 0)
	    { 
	      char *mystart = name;
	      char *myend   = (char *) strchr(mystart, ']');
	      
	      if (myend && (myend - mystart > maxlen))
		maxlen = myend - mystart;	/* Get longest selector.  */
	      matches++;
	    }
	}
    }
  if (matches)
    {
      printf_filtered ("Selectors matching \"%s\":\n\n", 
		       regexp ? regexp : "*");

      sym_arr = alloca (matches * sizeof (struct symbol *));
      matches = 0;
      ALL_MSYMBOLS (objfile, msymbol)
	{
	  QUIT;
	  name = SYMBOL_NATURAL_NAME (msymbol);
	  if (name &&
	     (name[0] == '-' || name[0] == '+') &&
	      name[1] == '[')		/* Got a method name.  */
	    {
	      /* Filter for class/instance methods.  */
	      if (plusminus && name[0] != plusminus)
		continue;
	      /* Find selector part.  */
	      name = (char *) strchr(name+2, ' ');
	      if (regexp == NULL || re_exec(++name) != 0)
		sym_arr[matches++] = (struct symbol *) msymbol;
	    }
	}

      qsort (sym_arr, matches, sizeof (struct minimal_symbol *), 
	     compare_selectors);
      /* Prevent compare on first iteration.  */
      asel[0] = 0;
      for (ix = 0; ix < matches; ix++)	/* Now do the output.  */
	{
	  char *p = asel;

	  QUIT;
	  name = SYMBOL_NATURAL_NAME (sym_arr[ix]);
	  name = strchr (name, ' ') + 1;
	  if (p[0] && specialcmp(name, p) == 0)
	    continue;		/* Seen this one already (not unique).  */

	  /* Copy selector part.  */
	  while (*name && *name != ']')
	    *p++ = *name++;
	  *p++ = '\0';
	  /* Print in columns.  */
	  puts_filtered_tabular(asel, maxlen + 1, 0);
	}
      begin_line();
    }
  else
    printf_filtered ("No selectors matching \"%s\"\n", regexp ? regexp : "*");
}

/*
 * Function: compare_classes (const void *, const void *)
 *
 * Comparison function for use with qsort.  Arguments are symbols or
 * msymbols Compares class part of objc method name alphabetically. 
 */

static int
compare_classes (const void *a, const void *b)
{
  char *aname, *bname;

  aname = SYMBOL_PRINT_NAME (*(struct symbol **) a);
  bname = SYMBOL_PRINT_NAME (*(struct symbol **) b);
  if (aname == NULL || bname == NULL)
    error ("internal: compare_classes(1)");

  return specialcmp (aname+1, bname+1);
}

/*
 * Function: classes_info(regexp, from_tty)
 *
 * Implements the "info classes" command for objective c classes.
 * Lists all objective c classes that match the optional regexp.
 * Works by grepping thru the list of objective c methods.  List will
 * be sorted and uniqued (since one class may have many methods).
 * BUGS: will not list a class that has no methods. 
 */

static void
classes_info (char *regexp, int from_tty)
{
  struct objfile	*objfile;
  struct minimal_symbol *msymbol;
  char                  *name;
  char                  *val;
  int                    matches = 0;
  int                    maxlen  = 0;
  int                    ix;
  char                   myregexp[2048];
  char                   aclass[256];
  struct symbol        **sym_arr;

  if (regexp == NULL)
    strcpy(myregexp, ".* ");	/* Null input: match all objc classes.  */
  else
    {
      strcpy(myregexp, regexp);
      if (myregexp[strlen(myregexp) - 1] == '$')
	/* In the method name, the end of the class name is marked by ' '.  */
	myregexp[strlen(myregexp) - 1] = ' ';
      else
	strcat(myregexp, ".* ");
    }

  if (regexp != NULL)
    {
      val = re_comp (myregexp);
      if (val != 0)
	error ("Invalid regexp (%s): %s", val, regexp);
    }

  /* First time thru is JUST to get max length and count.  */
  ALL_MSYMBOLS (objfile, msymbol)
    {
      QUIT;
      name = SYMBOL_NATURAL_NAME (msymbol);
      if (name &&
	 (name[0] == '-' || name[0] == '+') &&
	  name[1] == '[')			/* Got a method name.  */
	if (regexp == NULL || re_exec(name+2) != 0)
	  { 
	    /* Compute length of classname part.  */
	    char *mystart = name + 2;
	    char *myend   = (char *) strchr(mystart, ' ');
	    
	    if (myend && (myend - mystart > maxlen))
	      maxlen = myend - mystart;
	    matches++;
	  }
    }
  if (matches)
    {
      printf_filtered ("Classes matching \"%s\":\n\n", 
		       regexp ? regexp : "*");
      sym_arr = alloca (matches * sizeof (struct symbol *));
      matches = 0;
      ALL_MSYMBOLS (objfile, msymbol)
	{
	  QUIT;
	  name = SYMBOL_NATURAL_NAME (msymbol);
	  if (name &&
	     (name[0] == '-' || name[0] == '+') &&
	      name[1] == '[')			/* Got a method name.  */
	    if (regexp == NULL || re_exec(name+2) != 0)
		sym_arr[matches++] = (struct symbol *) msymbol;
	}

      qsort (sym_arr, matches, sizeof (struct minimal_symbol *), 
	     compare_classes);
      /* Prevent compare on first iteration.  */
      aclass[0] = 0;
      for (ix = 0; ix < matches; ix++)	/* Now do the output.  */
	{
	  char *p = aclass;

	  QUIT;
	  name = SYMBOL_NATURAL_NAME (sym_arr[ix]);
	  name += 2;
	  if (p[0] && specialcmp(name, p) == 0)
	    continue;	/* Seen this one already (not unique).  */

	  /* Copy class part of method name.  */
	  while (*name && *name != ' ')
	    *p++ = *name++;
	  *p++ = '\0';
	  /* Print in columns.  */
	  puts_filtered_tabular(aclass, maxlen + 1, 0);
	}
      begin_line();
    }
  else
    printf_filtered ("No classes matching \"%s\"\n", regexp ? regexp : "*");
}

/* 
 * Function: find_imps (char *selector, struct symbol **sym_arr)
 *
 * Input:  a string representing a selector
 *         a pointer to an array of symbol pointers
 *         possibly a pointer to a symbol found by the caller.
 *
 * Output: number of methods that implement that selector.  Side
 * effects: The array of symbol pointers is filled with matching syms.
 *
 * By analogy with function "find_methods" (symtab.c), builds a list
 * of symbols matching the ambiguous input, so that "decode_line_2"
 * (symtab.c) can list them and ask the user to choose one or more.
 * In this case the matches are objective c methods
 * ("implementations") matching an objective c selector.
 *
 * Note that it is possible for a normal (c-style) function to have
 * the same name as an objective c selector.  To prevent the selector
 * from eclipsing the function, we allow the caller (decode_line_1) to
 * search for such a function first, and if it finds one, pass it in
 * to us.  We will then integrate it into the list.  We also search
 * for one here, among the minsyms.
 *
 * NOTE: if NUM_DEBUGGABLE is non-zero, the sym_arr will be divided
 *       into two parts: debuggable (struct symbol) syms, and
 *       non_debuggable (struct minimal_symbol) syms.  The debuggable
 *       ones will come first, before NUM_DEBUGGABLE (which will thus
 *       be the index of the first non-debuggable one). 
 */

/*
 * Function: total_number_of_imps (char *selector);
 *
 * Input:  a string representing a selector 
 * Output: number of methods that implement that selector.
 *
 * By analogy with function "total_number_of_methods", this allows
 * decode_line_1 (symtab.c) to detect if there are objective c methods
 * matching the input, and to allocate an array of pointers to them
 * which can be manipulated by "decode_line_2" (also in symtab.c).
 */

char * 
parse_selector (char *method, char **selector)
{
  char *s1 = NULL;
  char *s2 = NULL;
  int found_quote = 0;

  char *nselector = NULL;

  gdb_assert (selector != NULL);

  s1 = method;

  while (isspace (*s1))
    s1++;
  if (*s1 == '\'') 
    {
      found_quote = 1;
      s1++;
    }
  while (isspace (*s1))
    s1++;
   
  nselector = s1;
  s2 = s1;

  for (;;) {
    if (isalnum (*s2) || (*s2 == '_') || (*s2 == ':'))
      *s1++ = *s2;
    else if (isspace (*s2))
      ;
    else if ((*s2 == '\0') || (*s2 == '\''))
      break;
    else
      return NULL;
    s2++;
  }
  *s1++ = '\0';

  while (isspace (*s2))
    s2++;
  if (found_quote)
    {
      if (*s2 == '\'') 
	s2++;
      while (isspace (*s2))
	s2++;
    }

  if (selector != NULL)
    *selector = nselector;

  return s2;
}

char * 
parse_method (char *method, char *type, char **class, 
	      char **category, char **selector)
{
  char *s1 = NULL;
  char *s2 = NULL;
  int found_quote = 0;

  char ntype = '\0';
  char *nclass = NULL;
  char *ncategory = NULL;
  char *nselector = NULL;

  gdb_assert (type != NULL);
  gdb_assert (class != NULL);
  gdb_assert (category != NULL);
  gdb_assert (selector != NULL);
  
  s1 = method;

  while (isspace (*s1))
    s1++;
  if (*s1 == '\'') 
    {
      found_quote = 1;
      s1++;
    }
  while (isspace (*s1))
    s1++;
  
  if ((s1[0] == '+') || (s1[0] == '-'))
    ntype = *s1++;

  while (isspace (*s1))
    s1++;

  if (*s1 != '[')
    return NULL;
  s1++;

  nclass = s1;
  while (isalnum (*s1) || (*s1 == '_'))
    s1++;
  
  s2 = s1;
  while (isspace (*s2))
    s2++;
  
  if (*s2 == '(')
    {
      s2++;
      while (isspace (*s2))
	s2++;
      ncategory = s2;
      while (isalnum (*s2) || (*s2 == '_'))
	s2++;
      *s2++ = '\0';
    }

  /* Truncate the class name now that we're not using the open paren.  */
  *s1++ = '\0';

  nselector = s2;
  s1 = s2;

  for (;;) {
    if (isalnum (*s2) || (*s2 == '_') || (*s2 == ':'))
      *s1++ = *s2;
    else if (isspace (*s2))
      ;
    else if (*s2 == ']')
      break;
    else
      return NULL;
    s2++;
  }
  *s1++ = '\0';
  s2++;

  while (isspace (*s2))
    s2++;
  if (found_quote)
    {
      if (*s2 != '\'') 
	return NULL;
      s2++;
      while (isspace (*s2))
	s2++;
    }

  if (type != NULL)
    *type = ntype;
  if (class != NULL)
    *class = nclass;
  if (category != NULL)
    *category = ncategory;
  if (selector != NULL)
    *selector = nselector;

  return s2;
}

static void
find_methods (struct symtab *symtab, char type, 
	      const char *class, const char *category, 
	      const char *selector, struct symbol **syms, 
	      unsigned int *nsym, unsigned int *ndebug)
{
  struct objfile *objfile = NULL;
  struct minimal_symbol *msymbol = NULL;
  struct block *block = NULL;
  struct symbol *sym = NULL;

  char *symname = NULL;

  char ntype = '\0';
  char *nclass = NULL;
  char *ncategory = NULL;
  char *nselector = NULL;

  unsigned int csym = 0;
  unsigned int cdebug = 0;

  static char *tmp = NULL;
  static unsigned int tmplen = 0;

  gdb_assert (nsym != NULL);
  gdb_assert (ndebug != NULL);

  if (symtab)
    block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (symtab), STATIC_BLOCK);

  ALL_MSYMBOLS (objfile, msymbol)
    {
      QUIT;

      if ((msymbol->type != mst_text) && (msymbol->type != mst_file_text))
	/* Not a function or method.  */
	continue;

      if (symtab)
	if ((SYMBOL_VALUE_ADDRESS (msymbol) <  BLOCK_START (block)) ||
	    (SYMBOL_VALUE_ADDRESS (msymbol) >= BLOCK_END (block)))
	  /* Not in the specified symtab.  */
	  continue;

      symname = SYMBOL_NATURAL_NAME (msymbol);
      if (symname == NULL)
	continue;

      if ((symname[0] != '-' && symname[0] != '+') || (symname[1] != '['))
	/* Not a method name.  */
	continue;
      
      while ((strlen (symname) + 1) >= tmplen)
	{
	  tmplen = (tmplen == 0) ? 1024 : tmplen * 2;
	  tmp = xrealloc (tmp, tmplen);
	}
      strcpy (tmp, symname);

      if (parse_method (tmp, &ntype, &nclass, &ncategory, &nselector) == NULL)
	continue;
      
      if ((type != '\0') && (ntype != type))
	continue;

      if ((class != NULL) 
	  && ((nclass == NULL) || (strcmp (class, nclass) != 0)))
	continue;

      if ((category != NULL) && 
	  ((ncategory == NULL) || (strcmp (category, ncategory) != 0)))
	continue;

      if ((selector != NULL) && 
	  ((nselector == NULL) || (strcmp (selector, nselector) != 0)))
	continue;

      sym = find_pc_function (SYMBOL_VALUE_ADDRESS (msymbol));
      if (sym != NULL)
        {
          const char *newsymname = SYMBOL_NATURAL_NAME (sym);
	  
          if (strcmp (symname, newsymname) == 0)
            {
              /* Found a high-level method sym: swap it into the
                 lower part of sym_arr (below num_debuggable).  */
              if (syms != NULL)
                {
                  syms[csym] = syms[cdebug];
                  syms[cdebug] = sym;
                }
              csym++;
              cdebug++;
            }
          else
            {
              warning (
"debugging symbol \"%s\" does not match minimal symbol (\"%s\"); ignoring",
                       newsymname, symname);
              if (syms != NULL)
                syms[csym] = (struct symbol *) msymbol;
              csym++;
            }
        }
      else 
	{
	  /* Found a non-debuggable method symbol.  */
	  if (syms != NULL)
	    syms[csym] = (struct symbol *) msymbol;
	  csym++;
	}
    }

  if (nsym != NULL)
    *nsym = csym;
  if (ndebug != NULL)
    *ndebug = cdebug;
}

char *find_imps (struct symtab *symtab, struct block *block,
		 char *method, struct symbol **syms, 
		 unsigned int *nsym, unsigned int *ndebug)
{
  char type = '\0';
  char *class = NULL;
  char *category = NULL;
  char *selector = NULL;

  unsigned int csym = 0;
  unsigned int cdebug = 0;

  unsigned int ncsym = 0;
  unsigned int ncdebug = 0;

  char *buf = NULL;
  char *tmp = NULL;

  gdb_assert (nsym != NULL);
  gdb_assert (ndebug != NULL);

  if (nsym != NULL)
    *nsym = 0;
  if (ndebug != NULL)
    *ndebug = 0;

  buf = (char *) alloca (strlen (method) + 1);
  strcpy (buf, method);
  tmp = parse_method (buf, &type, &class, &category, &selector);

  if (tmp == NULL) {
    
    struct symbol *sym = NULL;
    struct minimal_symbol *msym = NULL;
    
    strcpy (buf, method);
    tmp = parse_selector (buf, &selector);
    
    if (tmp == NULL)
      return NULL;
    
    sym = lookup_symbol (selector, block, VAR_DOMAIN, 0, NULL);
    if (sym != NULL) 
      {
	if (syms)
	  syms[csym] = sym;
	csym++;
	cdebug++;
      }

    if (sym == NULL)
      msym = lookup_minimal_symbol (selector, 0, 0);

    if (msym != NULL) 
      {
	if (syms)
	  syms[csym] = (struct symbol *)msym;
	csym++;
      }
  }

  if (syms != NULL)
    find_methods (symtab, type, class, category, selector, 
		  syms + csym, &ncsym, &ncdebug);
  else
    find_methods (symtab, type, class, category, selector, 
		  NULL, &ncsym, &ncdebug);

  /* If we didn't find any methods, just return.  */
  if (ncsym == 0 && ncdebug == 0)
    return method;

  /* Take debug symbols from the second batch of symbols and swap them
   * with debug symbols from the first batch.  Repeat until either the
   * second section is out of debug symbols or the first section is
   * full of debug symbols.  Either way we have all debug symbols
   * packed to the beginning of the buffer.  
   */

  if (syms != NULL) 
    {
      while ((cdebug < csym) && (ncdebug > 0))
	{
	  struct symbol *s = NULL;
	  /* First non-debugging symbol.  */
	  unsigned int i = cdebug;
	  /* Last of second batch of debug symbols.  */
	  unsigned int j = csym + ncdebug - 1;

	  s = syms[j];
	  syms[j] = syms[i];
	  syms[i] = s;

	  /* We've moved a symbol from the second debug section to the
             first one.  */
	  cdebug++;
	  ncdebug--;
	}
    }

  csym += ncsym;
  cdebug += ncdebug;

  if (nsym != NULL)
    *nsym = csym;
  if (ndebug != NULL)
    *ndebug = cdebug;

  if (syms == NULL)
    return method + (tmp - buf);

  if (csym > 1)
    {
      /* Sort debuggable symbols.  */
      if (cdebug > 1)
	qsort (syms, cdebug, sizeof (struct minimal_symbol *), 
	       compare_classes);
      
      /* Sort minimal_symbols.  */
      if ((csym - cdebug) > 1)
	qsort (&syms[cdebug], csym - cdebug, 
	       sizeof (struct minimal_symbol *), compare_classes);
    }
  /* Terminate the sym_arr list.  */
  syms[csym] = 0;

  return method + (tmp - buf);
}

static void 
print_object_command (char *args, int from_tty)
{
  struct value *object, *function, *description;
  CORE_ADDR string_addr, object_addr;
  int i = 0;
  char c = -1;

  if (!args || !*args)
    error (
"The 'print-object' command requires an argument (an Objective-C object)");

  {
    struct expression *expr = parse_expression (args);
    struct cleanup *old_chain = 
      make_cleanup (free_current_contents, &expr);
    int pc = 0;

    object = expr->language_defn->la_exp_desc->evaluate_exp 
      (builtin_type_void_data_ptr, expr, &pc, EVAL_NORMAL);
    do_cleanups (old_chain);
  }

  /* Validate the address for sanity.  */
  object_addr = value_as_long (object);
  read_memory (object_addr, &c, 1);

  function = find_function_in_inferior ("_NSPrintForDebugger");
  if (function == NULL)
    error ("Unable to locate _NSPrintForDebugger in child process");

  description = call_function_by_hand (function, 1, &object);

  string_addr = value_as_long (description);
  if (string_addr == 0)
    error ("object returns null description");

  read_memory (string_addr + i++, &c, 1);
  if (c != '\0')
    do
      { /* Read and print characters up to EOS.  */
	QUIT;
	printf_filtered ("%c", c);
	read_memory (string_addr + i++, &c, 1);
      } while (c != 0);
  else
    printf_filtered("<object returns empty description>");
  printf_filtered ("\n");
}

/* The data structure 'methcalls' is used to detect method calls (thru
 * ObjC runtime lib functions objc_msgSend, objc_msgSendSuper, etc.),
 * and ultimately find the method being called. 
 */

struct objc_methcall {
  char *name;
 /* Return instance method to be called.  */
  int (*stop_at) (CORE_ADDR, CORE_ADDR *);
  /* Start of pc range corresponding to method invocation.  */
  CORE_ADDR begin;
  /* End of pc range corresponding to method invocation.  */
  CORE_ADDR end;
};

static int resolve_msgsend (CORE_ADDR pc, CORE_ADDR *new_pc);
static int resolve_msgsend_stret (CORE_ADDR pc, CORE_ADDR *new_pc);
static int resolve_msgsend_super (CORE_ADDR pc, CORE_ADDR *new_pc);
static int resolve_msgsend_super_stret (CORE_ADDR pc, CORE_ADDR *new_pc);

static struct objc_methcall methcalls[] = {
  { "_objc_msgSend", resolve_msgsend, 0, 0},
  { "_objc_msgSend_stret", resolve_msgsend_stret, 0, 0},
  { "_objc_msgSendSuper", resolve_msgsend_super, 0, 0},
  { "_objc_msgSendSuper_stret", resolve_msgsend_super_stret, 0, 0},
  { "_objc_getClass", NULL, 0, 0},
  { "_objc_getMetaClass", NULL, 0, 0}
};

#define nmethcalls (sizeof (methcalls) / sizeof (methcalls[0]))

/* The following function, "find_objc_msgsend", fills in the data
 * structure "objc_msgs" by finding the addresses of each of the
 * (currently four) functions that it holds (of which objc_msgSend is
 * the first).  This must be called each time symbols are loaded, in
 * case the functions have moved for some reason.  
 */

static void 
find_objc_msgsend (void)
{
  unsigned int i;
  for (i = 0; i < nmethcalls; i++) {

    struct minimal_symbol *func;

    /* Try both with and without underscore.  */
    func = lookup_minimal_symbol (methcalls[i].name, NULL, NULL);
    if ((func == NULL) && (methcalls[i].name[0] == '_')) {
      func = lookup_minimal_symbol (methcalls[i].name + 1, NULL, NULL);
    }
    if (func == NULL) { 
      methcalls[i].begin = 0;
      methcalls[i].end = 0;
      continue; 
    }
    
    methcalls[i].begin = SYMBOL_VALUE_ADDRESS (func);
    do {
      methcalls[i].end = SYMBOL_VALUE_ADDRESS (++func);
    } while (methcalls[i].begin == methcalls[i].end);
  }
}

/* find_objc_msgcall (replaces pc_off_limits)
 *
 * ALL that this function now does is to determine whether the input
 * address ("pc") is the address of one of the Objective-C message
 * dispatch functions (mainly objc_msgSend or objc_msgSendSuper), and
 * if so, it returns the address of the method that will be called.
 *
 * The old function "pc_off_limits" used to do a lot of other things
 * in addition, such as detecting shared library jump stubs and
 * returning the address of the shlib function that would be called.
 * That functionality has been moved into the SKIP_TRAMPOLINE_CODE and
 * IN_SOLIB_TRAMPOLINE macros, which are resolved in the target-
 * dependent modules.  
 */

struct objc_submethod_helper_data {
  int (*f) (CORE_ADDR, CORE_ADDR *);
  CORE_ADDR pc;
  CORE_ADDR *new_pc;
};

static int 
find_objc_msgcall_submethod_helper (void * arg)
{
  struct objc_submethod_helper_data *s = 
    (struct objc_submethod_helper_data *) arg;

  if (s->f (s->pc, s->new_pc) == 0) 
    return 1;
  else 
    return 0;
}

static int 
find_objc_msgcall_submethod (int (*f) (CORE_ADDR, CORE_ADDR *),
			     CORE_ADDR pc, 
			     CORE_ADDR *new_pc)
{
  struct objc_submethod_helper_data s;

  s.f = f;
  s.pc = pc;
  s.new_pc = new_pc;

  if (catch_errors (find_objc_msgcall_submethod_helper,
		    (void *) &s,
		    "Unable to determine target of Objective-C method call (ignoring):\n",
		    RETURN_MASK_ALL) == 0) 
    return 1;
  else 
    return 0;
}

int 
find_objc_msgcall (CORE_ADDR pc, CORE_ADDR *new_pc)
{
  unsigned int i;

  find_objc_msgsend ();
  if (new_pc != NULL)
    {
      *new_pc = 0;
    }

  for (i = 0; i < nmethcalls; i++) 
    if ((pc >= methcalls[i].begin) && (pc < methcalls[i].end)) 
      {
	if (methcalls[i].stop_at != NULL) 
	  return find_objc_msgcall_submethod (methcalls[i].stop_at, 
					      pc, new_pc);
	else 
	  return 0;
      }

  return 0;
}

extern initialize_file_ftype _initialize_objc_language; /* -Wmissing-prototypes */

void
_initialize_objc_language (void)
{
  add_language (&objc_language_defn);
  add_info ("selectors", selectors_info,    /* INFO SELECTORS command.  */
	    "All Objective-C selectors, or those matching REGEXP.");
  add_info ("classes", classes_info, 	    /* INFO CLASSES   command.  */
	    "All Objective-C classes, or those matching REGEXP.");
  add_com ("print-object", class_vars, print_object_command, 
	   "Ask an Objective-C object to print itself.");
  add_com_alias ("po", "print-object", class_vars, 1);
}

static void 
read_objc_method (CORE_ADDR addr, struct objc_method *method)
{
  method->name  = read_memory_unsigned_integer (addr + 0, 4);
  method->types = read_memory_unsigned_integer (addr + 4, 4);
  method->imp   = read_memory_unsigned_integer (addr + 8, 4);
}

static 
unsigned long read_objc_methlist_nmethods (CORE_ADDR addr)
{
  return read_memory_unsigned_integer (addr + 4, 4);
}

static void 
read_objc_methlist_method (CORE_ADDR addr, unsigned long num, 
			   struct objc_method *method)
{
  gdb_assert (num < read_objc_methlist_nmethods (addr));
  read_objc_method (addr + 8 + (12 * num), method);
}
  
static void 
read_objc_object (CORE_ADDR addr, struct objc_object *object)
{
  object->isa = read_memory_unsigned_integer (addr, 4);
}

static void 
read_objc_super (CORE_ADDR addr, struct objc_super *super)
{
  super->receiver = read_memory_unsigned_integer (addr, 4);
  super->class = read_memory_unsigned_integer (addr + 4, 4);
};

static void 
read_objc_class (CORE_ADDR addr, struct objc_class *class)
{
  class->isa = read_memory_unsigned_integer (addr, 4);
  class->super_class = read_memory_unsigned_integer (addr + 4, 4);
  class->name = read_memory_unsigned_integer (addr + 8, 4);
  class->version = read_memory_unsigned_integer (addr + 12, 4);
  class->info = read_memory_unsigned_integer (addr + 16, 4);
  class->instance_size = read_memory_unsigned_integer (addr + 18, 4);
  class->ivars = read_memory_unsigned_integer (addr + 24, 4);
  class->methods = read_memory_unsigned_integer (addr + 28, 4);
  class->cache = read_memory_unsigned_integer (addr + 32, 4);
  class->protocols = read_memory_unsigned_integer (addr + 36, 4);
}

static CORE_ADDR
find_implementation_from_class (CORE_ADDR class, CORE_ADDR sel)
{
  CORE_ADDR subclass = class;

  while (subclass != 0) 
    {

      struct objc_class class_str;
      unsigned mlistnum = 0;

      read_objc_class (subclass, &class_str);

      for (;;) 
	{
	  CORE_ADDR mlist;
	  unsigned long nmethods;
	  unsigned long i;
      
	  mlist = read_memory_unsigned_integer (class_str.methods + 
						(4 * mlistnum), 4);
	  if (mlist == 0) 
	    break;

	  nmethods = read_objc_methlist_nmethods (mlist);

	  for (i = 0; i < nmethods; i++) 
	    {
	      struct objc_method meth_str;
	      read_objc_methlist_method (mlist, i, &meth_str);

#if 0
	      fprintf (stderr, 
		       "checking method 0x%lx against selector 0x%lx\n", 
		       meth_str.name, sel);
#endif

	      if (meth_str.name == sel) 
		/* FIXME: hppa arch was doing a pointer dereference
		   here. There needs to be a better way to do that.  */
		return meth_str.imp;
	    }
	  mlistnum++;
	}
      subclass = class_str.super_class;
    }

  return 0;
}

static CORE_ADDR
find_implementation (CORE_ADDR object, CORE_ADDR sel)
{
  struct objc_object ostr;

  if (object == 0)
    return 0;
  read_objc_object (object, &ostr);
  if (ostr.isa == 0)
    return 0;

  return find_implementation_from_class (ostr.isa, sel);
}

#define OBJC_FETCH_POINTER_ARGUMENT(argi) \
  FETCH_POINTER_ARGUMENT (get_current_frame (), argi, builtin_type_void_func_ptr)

static int
resolve_msgsend (CORE_ADDR pc, CORE_ADDR *new_pc)
{
  CORE_ADDR object;
  CORE_ADDR sel;
  CORE_ADDR res;

  object = OBJC_FETCH_POINTER_ARGUMENT (0);
  sel = OBJC_FETCH_POINTER_ARGUMENT (1);

  res = find_implementation (object, sel);
  if (new_pc != 0)
    *new_pc = res;
  if (res == 0)
    return 1;
  return 0;
}

static int
resolve_msgsend_stret (CORE_ADDR pc, CORE_ADDR *new_pc)
{
  CORE_ADDR object;
  CORE_ADDR sel;
  CORE_ADDR res;

  object = OBJC_FETCH_POINTER_ARGUMENT (1);
  sel = OBJC_FETCH_POINTER_ARGUMENT (2);

  res = find_implementation (object, sel);
  if (new_pc != 0)
    *new_pc = res;
  if (res == 0)
    return 1;
  return 0;
}

static int
resolve_msgsend_super (CORE_ADDR pc, CORE_ADDR *new_pc)
{
  struct objc_super sstr;

  CORE_ADDR super;
  CORE_ADDR sel;
  CORE_ADDR res;

  super = OBJC_FETCH_POINTER_ARGUMENT (0);
  sel = OBJC_FETCH_POINTER_ARGUMENT (1);

  read_objc_super (super, &sstr);
  if (sstr.class == 0)
    return 0;
  
  res = find_implementation_from_class (sstr.class, sel);
  if (new_pc != 0)
    *new_pc = res;
  if (res == 0)
    return 1;
  return 0;
}

static int
resolve_msgsend_super_stret (CORE_ADDR pc, CORE_ADDR *new_pc)
{
  struct objc_super sstr;

  CORE_ADDR super;
  CORE_ADDR sel;
  CORE_ADDR res;

  super = OBJC_FETCH_POINTER_ARGUMENT (1);
  sel = OBJC_FETCH_POINTER_ARGUMENT (2);

  read_objc_super (super, &sstr);
  if (sstr.class == 0)
    return 0;
  
  res = find_implementation_from_class (sstr.class, sel);
  if (new_pc != 0)
    *new_pc = res;
  if (res == 0)
    return 1;
  return 0;
}
