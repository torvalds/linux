/* Source-language-related definitions for GDB.

   Copyright 1991, 1992, 1993, 1994, 1995, 1998, 1999, 2000, 2003,
   2004 Free Software Foundation, Inc.

   Contributed by the Department of Computer Science at the State University
   of New York at Buffalo.

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

#if !defined (LANGUAGE_H)
#define LANGUAGE_H 1

/* Forward decls for prototypes */
struct value;
struct objfile;
struct expression;
struct ui_file;

/* enum exp_opcode;     ANSI's `wisdom' didn't include forward enum decls. */

/* This used to be included to configure GDB for one or more specific
   languages.  Now it is left out to configure for all of them.  FIXME.  */
/* #include "lang_def.h" */
#define	_LANG_c
#define	_LANG_m2
#define  _LANG_fortran
#define  _LANG_pascal

#define MAX_FORTRAN_DIMS  7	/* Maximum number of F77 array dims */

/* range_mode ==
   range_mode_auto:   range_check set automatically to default of language.
   range_mode_manual: range_check set manually by user.  */

extern enum range_mode
  {
    range_mode_auto, range_mode_manual
  }
range_mode;

/* range_check ==
   range_check_on:    Ranges are checked in GDB expressions, producing errors.
   range_check_warn:  Ranges are checked, producing warnings.
   range_check_off:   Ranges are not checked in GDB expressions.  */

extern enum range_check
  {
    range_check_off, range_check_warn, range_check_on
  }
range_check;

/* type_mode ==
   type_mode_auto:   type_check set automatically to default of language
   type_mode_manual: type_check set manually by user. */

extern enum type_mode
  {
    type_mode_auto, type_mode_manual
  }
type_mode;

/* type_check ==
   type_check_on:    Types are checked in GDB expressions, producing errors.
   type_check_warn:  Types are checked, producing warnings.
   type_check_off:   Types are not checked in GDB expressions.  */

extern enum type_check
  {
    type_check_off, type_check_warn, type_check_on
  }
type_check;

/* case_mode ==
   case_mode_auto:   case_sensitivity set upon selection of scope 
   case_mode_manual: case_sensitivity set only by user.  */

extern enum case_mode
  {
    case_mode_auto, case_mode_manual
  }
case_mode;

/* case_sensitivity ==
   case_sensitive_on:   Case sensitivity in name matching is used
   case_sensitive_off:  Case sensitivity in name matching is not used  */

extern enum case_sensitivity
  {
    case_sensitive_on, case_sensitive_off
  }
case_sensitivity;

/* Information for doing language dependent formatting of printed values. */

struct language_format_info
  {
    /* The format that can be passed directly to standard C printf functions
       to generate a completely formatted value in the format appropriate for
       the language. */

    char *la_format;

    /* The prefix to be used when directly printing a value, or constructing
       a standard C printf format.  This generally is everything up to the
       conversion specification (the part introduced by the '%' character
       and terminated by the conversion specifier character). */

    char *la_format_prefix;

    /* The conversion specifier.  This is generally everything after the
       field width and precision, typically only a single character such
       as 'o' for octal format or 'x' for hexadecimal format. */

    char *la_format_specifier;

    /* The suffix to be used when directly printing a value, or constructing
       a standard C printf format.  This generally is everything after the
       conversion specification (the part introduced by the '%' character
       and terminated by the conversion specifier character). */

    char *la_format_suffix;	/* Suffix for custom format string */
  };

/* Structure tying together assorted information about a language.  */

struct language_defn
  {
    /* Name of the language */

    char *la_name;

    /* its symtab language-enum (defs.h) */

    enum language la_language;

    /* Its builtin types.  This is a vector ended by a NULL pointer.  These
       types can be specified by name in parsing types in expressions,
       regardless of whether the program being debugged actually defines
       such a type.  */

    struct type **const *la_builtin_type_vector;

    /* Default range checking */

    enum range_check la_range_check;

    /* Default type checking */

    enum type_check la_type_check;

    /* Default case sensitivity */
    enum case_sensitivity la_case_sensitivity;

    /* Definitions related to expression printing, prefixifying, and
       dumping */

    const struct exp_descriptor *la_exp_desc;

    /* Parser function. */

    int (*la_parser) (void);

    /* Parser error function */

    void (*la_error) (char *);

    void (*la_printchar) (int ch, struct ui_file * stream);

    void (*la_printstr) (struct ui_file * stream, char *string,
			 unsigned int length, int width,
			 int force_ellipses);

    void (*la_emitchar) (int ch, struct ui_file * stream, int quoter);

    struct type *(*la_fund_type) (struct objfile *, int);

    /* Print a type using syntax appropriate for this language. */

    void (*la_print_type) (struct type *, char *, struct ui_file *, int,
			   int);

    /* Print a value using syntax appropriate for this language. */

    int (*la_val_print) (struct type *, char *, int, CORE_ADDR,
			 struct ui_file *, int, int, int,
			 enum val_prettyprint);

    /* Print a top-level value using syntax appropriate for this language. */

    int (*la_value_print) (struct value *, struct ui_file *,
			   int, enum val_prettyprint);

    /* PC is possibly an unknown languages trampoline.
       If that PC falls in a trampoline belonging to this language,
       return the address of the first pc in the real function, or 0
       if it isn't a language tramp for this language.  */
    CORE_ADDR (*skip_trampoline) (CORE_ADDR pc);

    /* Now come some hooks for lookup_symbol.  */

    /* If this is non-NULL, lookup_symbol will do the 'field_of_this'
       check, using this function to find the value of this.  */

    /* FIXME: carlton/2003-05-19: Audit all the language_defn structs
       to make sure we're setting this appropriately: I'm sure it
       could be NULL in more languages.  */

    struct value *(*la_value_of_this) (int complain);

    /* This is a function that lookup_symbol will call when it gets to
       the part of symbol lookup where C looks up static and global
       variables.  */

    struct symbol *(*la_lookup_symbol_nonlocal) (const char *,
						 const char *,
						 const struct block *,
						 const domain_enum,
						 struct symtab **);

    /* Find the definition of the type with the given name.  */
    struct type *(*la_lookup_transparent_type) (const char *);

    /* Return demangled language symbol, or NULL.  */
    char *(*la_demangle) (const char *mangled, int options);

    /* Base 2 (binary) formats. */

    struct language_format_info la_binary_format;

    /* Base 8 (octal) formats. */

    struct language_format_info la_octal_format;

    /* Base 10 (decimal) formats */

    struct language_format_info la_decimal_format;

    /* Base 16 (hexadecimal) formats */

    struct language_format_info la_hex_format;

    /* Table for printing expressions */

    const struct op_print *la_op_print_tab;

    /* Zero if the language has first-class arrays.  True if there are no
       array values, and array objects decay to pointers, as in C. */

    char c_style_arrays;

    /* Index to use for extracting the first element of a string. */
    char string_lower_bound;

    /* Type of elements of strings. */
    struct type **string_char_type;

    /* The list of characters forming word boundaries.  */
    char *(*la_word_break_characters) (void);

    /* Add fields above this point, so the magic number is always last. */
    /* Magic number for compat checking */

    long la_magic;

  };

#define LANG_MAGIC	910823L

/* Pointer to the language_defn for our current language.  This pointer
   always points to *some* valid struct; it can be used without checking
   it for validity.

   The current language affects expression parsing and evaluation
   (FIXME: it might be cleaner to make the evaluation-related stuff
   separate exp_opcodes for each different set of semantics.  We
   should at least think this through more clearly with respect to
   what happens if the language is changed between parsing and
   evaluation) and printing of things like types and arrays.  It does
   *not* affect symbol-reading-- each source file in a symbol-file has
   its own language and we should keep track of that regardless of the
   language when symbols are read.  If we want some manual setting for
   the language of symbol files (e.g. detecting when ".c" files are
   C++), it should be a separate setting from the current_language.  */

extern const struct language_defn *current_language;

/* Pointer to the language_defn expected by the user, e.g. the language
   of main(), or the language we last mentioned in a message, or C.  */

extern const struct language_defn *expected_language;

/* language_mode == 
   language_mode_auto:   current_language automatically set upon selection
   of scope (e.g. stack frame)
   language_mode_manual: current_language set only by user.  */

extern enum language_mode
  {
    language_mode_auto, language_mode_manual
  }
language_mode;

/* These macros define the behaviour of the expression 
   evaluator.  */

/* Should we strictly type check expressions? */
#define STRICT_TYPE (type_check != type_check_off)

/* Should we range check values against the domain of their type? */
#define RANGE_CHECK (range_check != range_check_off)

/* "cast" really means conversion */
/* FIXME -- should be a setting in language_defn */
#define CAST_IS_CONVERSION (current_language->la_language == language_c  || \
			    current_language->la_language == language_cplus || \
			    current_language->la_language == language_objc)

extern void language_info (int);

extern enum language set_language (enum language);


/* This page contains functions that return things that are
   specific to languages.  Each of these functions is based on
   the current setting of working_lang, which the user sets
   with the "set language" command. */

#define create_fundamental_type(objfile,typeid) \
  (current_language->la_fund_type(objfile, typeid))

#define LA_PRINT_TYPE(type,varstring,stream,show,level) \
  (current_language->la_print_type(type,varstring,stream,show,level))

#define LA_VAL_PRINT(type,valaddr,offset,addr,stream,fmt,deref,recurse,pretty) \
  (current_language->la_val_print(type,valaddr,offset,addr,stream,fmt,deref, \
				  recurse,pretty))
#define LA_VALUE_PRINT(val,stream,fmt,pretty) \
  (current_language->la_value_print(val,stream,fmt,pretty))

/* Return a format string for printf that will print a number in one of
   the local (language-specific) formats.  Result is static and is
   overwritten by the next call.  Takes printf options like "08" or "l"
   (to produce e.g. %08x or %lx).  */

#define local_binary_format() \
  (current_language->la_binary_format.la_format)
#define local_binary_format_prefix() \
  (current_language->la_binary_format.la_format_prefix)
#define local_binary_format_specifier() \
  (current_language->la_binary_format.la_format_specifier)
#define local_binary_format_suffix() \
  (current_language->la_binary_format.la_format_suffix)

#define local_octal_format() \
  (current_language->la_octal_format.la_format)
#define local_octal_format_prefix() \
  (current_language->la_octal_format.la_format_prefix)
#define local_octal_format_specifier() \
  (current_language->la_octal_format.la_format_specifier)
#define local_octal_format_suffix() \
  (current_language->la_octal_format.la_format_suffix)

#define local_decimal_format() \
  (current_language->la_decimal_format.la_format)
#define local_decimal_format_prefix() \
  (current_language->la_decimal_format.la_format_prefix)
#define local_decimal_format_specifier() \
  (current_language->la_decimal_format.la_format_specifier)
#define local_decimal_format_suffix() \
  (current_language->la_decimal_format.la_format_suffix)

#define local_hex_format() \
  (current_language->la_hex_format.la_format)
#define local_hex_format_prefix() \
  (current_language->la_hex_format.la_format_prefix)
#define local_hex_format_specifier() \
  (current_language->la_hex_format.la_format_specifier)
#define local_hex_format_suffix() \
  (current_language->la_hex_format.la_format_suffix)

#define LA_PRINT_CHAR(ch, stream) \
  (current_language->la_printchar(ch, stream))
#define LA_PRINT_STRING(stream, string, length, width, force_ellipses) \
  (current_language->la_printstr(stream, string, length, width, force_ellipses))
#define LA_EMIT_CHAR(ch, stream, quoter) \
  (current_language->la_emitchar(ch, stream, quoter))

/* Test a character to decide whether it can be printed in literal form
   or needs to be printed in another representation.  For example,
   in C the literal form of the character with octal value 141 is 'a'
   and the "other representation" is '\141'.  The "other representation"
   is program language dependent. */

#define PRINT_LITERAL_FORM(c)		\
  ((c) >= 0x20				\
   && ((c) < 0x7F || (c) >= 0xA0)	\
   && (!sevenbit_strings || (c) < 0x80))

/* Return a format string for printf that will print a number in one of
   the local (language-specific) formats.  Result is static and is
   overwritten by the next call.  Takes printf options like "08" or "l"
   (to produce e.g. %08x or %lx).  */

extern char *local_decimal_format_custom (char *);	/* language.c */

extern char *local_octal_format_custom (char *);	/* language.c */

extern char *local_hex_format_custom (char *);	/* language.c */

#if 0
/* FIXME: cagney/2000-03-04: This function does not appear to be used.
   It can be deleted once 5.0 has been released. */
/* Return a string that contains the hex digits of the number.  No preceeding
   "0x" */

extern char *longest_raw_hex_string (LONGEST);
#endif

/* Return a string that contains a number formatted in one of the local
   (language-specific) formats.  Result is static and is overwritten by
   the next call.  Takes printf options like "08l" or "l".  */

extern char *local_hex_string (LONGEST);	/* language.c */

extern char *local_hex_string_custom (LONGEST, char *);	/* language.c */

/* Type predicates */

extern int simple_type (struct type *);

extern int ordered_type (struct type *);

extern int same_type (struct type *, struct type *);

extern int integral_type (struct type *);

extern int numeric_type (struct type *);

extern int character_type (struct type *);

extern int boolean_type (struct type *);

extern int float_type (struct type *);

extern int pointer_type (struct type *);

extern int structured_type (struct type *);

/* Checks Binary and Unary operations for semantic type correctness */
/* FIXME:  Does not appear to be used */
#define unop_type_check(v,o) binop_type_check((v),NULL,(o))

extern void binop_type_check (struct value *, struct value *, int);

/* Error messages */

extern void op_error (const char *lhs, enum exp_opcode,
		      const char *rhs);

extern void type_error (const char *, ...) ATTR_FORMAT (printf, 1, 2);

extern void range_error (const char *, ...) ATTR_FORMAT (printf, 1, 2);

/* Data:  Does this value represent "truth" to the current language?  */

extern int value_true (struct value *);

extern struct type *lang_bool_type (void);

/* The type used for Boolean values in the current language. */
#define LA_BOOL_TYPE lang_bool_type ()

/* Misc:  The string representing a particular enum language.  */

extern enum language language_enum (char *str);

extern const struct language_defn *language_def (enum language);

extern char *language_str (enum language);

/* Add a language to the set known by GDB (at initialization time).  */

extern void add_language (const struct language_defn *);

extern enum language get_frame_language (void);	/* In stack.c */

/* Check for a language-specific trampoline. */

extern CORE_ADDR skip_language_trampoline (CORE_ADDR pc);

/* Return demangled language symbol, or NULL.  */
extern char *language_demangle (const struct language_defn *current_language, 
				const char *mangled, int options);

/* Splitting strings into words.  */
extern char *default_word_break_characters (void);

#endif /* defined (LANGUAGE_H) */
