/* Mainly the interface between cpplib and the C front ends.
   Copyright (C) 1987, 1988, 1989, 1992, 1994, 1995, 1996, 1997
   1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#include "real.h"
#include "rtl.h"
#include "tree.h"
#include "input.h"
#include "output.h"
#include "c-tree.h"
#include "c-common.h"
#include "flags.h"
#include "timevar.h"
#include "cpplib.h"
#include "c-pragma.h"
#include "toplev.h"
#include "intl.h"
#include "tm_p.h"
#include "splay-tree.h"
#include "debug.h"

/* We may keep statistics about how long which files took to compile.  */
static int header_time, body_time;
static splay_tree file_info_tree;

int pending_lang_change; /* If we need to switch languages - C++ only */
int c_header_level;	 /* depth in C headers - C++ only */

/* If we need to translate characters received.  This is tri-state:
   0 means use only the untranslated string; 1 means use only
   the translated string; -1 means chain the translated string
   to the untranslated one.  */
int c_lex_string_translate = 1;

/* True if strings should be passed to the caller of c_lex completely
   unmolested (no concatenation, no translation).  */
bool c_lex_return_raw_strings = false;

static tree interpret_integer (const cpp_token *, unsigned int);
static tree interpret_float (const cpp_token *, unsigned int);
static enum integer_type_kind narrowest_unsigned_type
	(unsigned HOST_WIDE_INT, unsigned HOST_WIDE_INT, unsigned int);
static enum integer_type_kind narrowest_signed_type
	(unsigned HOST_WIDE_INT, unsigned HOST_WIDE_INT, unsigned int);
static enum cpp_ttype lex_string (const cpp_token *, tree *, bool);
static tree lex_charconst (const cpp_token *);
static void update_header_times (const char *);
static int dump_one_header (splay_tree_node, void *);
static void cb_line_change (cpp_reader *, const cpp_token *, int);
static void cb_ident (cpp_reader *, unsigned int, const cpp_string *);
static void cb_def_pragma (cpp_reader *, unsigned int);
static void cb_define (cpp_reader *, unsigned int, cpp_hashnode *);
static void cb_undef (cpp_reader *, unsigned int, cpp_hashnode *);

void
init_c_lex (void)
{
  struct cpp_callbacks *cb;
  struct c_fileinfo *toplevel;

  /* The get_fileinfo data structure must be initialized before
     cpp_read_main_file is called.  */
  toplevel = get_fileinfo ("<top level>");
  if (flag_detailed_statistics)
    {
      header_time = 0;
      body_time = get_run_time ();
      toplevel->time = body_time;
    }

  cb = cpp_get_callbacks (parse_in);

  cb->line_change = cb_line_change;
  cb->ident = cb_ident;
  cb->def_pragma = cb_def_pragma;
  cb->valid_pch = c_common_valid_pch;
  cb->read_pch = c_common_read_pch;

  /* Set the debug callbacks if we can use them.  */
  if (debug_info_level == DINFO_LEVEL_VERBOSE
      && (write_symbols == DWARF2_DEBUG
	  || write_symbols == VMS_AND_DWARF2_DEBUG))
    {
      cb->define = cb_define;
      cb->undef = cb_undef;
    }
}

struct c_fileinfo *
get_fileinfo (const char *name)
{
  splay_tree_node n;
  struct c_fileinfo *fi;

  if (!file_info_tree)
    file_info_tree = splay_tree_new ((splay_tree_compare_fn) strcmp,
				     0,
				     (splay_tree_delete_value_fn) free);

  n = splay_tree_lookup (file_info_tree, (splay_tree_key) name);
  if (n)
    return (struct c_fileinfo *) n->value;

  fi = XNEW (struct c_fileinfo);
  fi->time = 0;
  fi->interface_only = 0;
  fi->interface_unknown = 1;
  splay_tree_insert (file_info_tree, (splay_tree_key) name,
		     (splay_tree_value) fi);
  return fi;
}

static void
update_header_times (const char *name)
{
  /* Changing files again.  This means currently collected time
     is charged against header time, and body time starts back at 0.  */
  if (flag_detailed_statistics)
    {
      int this_time = get_run_time ();
      struct c_fileinfo *file = get_fileinfo (name);
      header_time += this_time - body_time;
      file->time += this_time - body_time;
      body_time = this_time;
    }
}

static int
dump_one_header (splay_tree_node n, void * ARG_UNUSED (dummy))
{
  print_time ((const char *) n->key,
	      ((struct c_fileinfo *) n->value)->time);
  return 0;
}

void
dump_time_statistics (void)
{
  struct c_fileinfo *file = get_fileinfo (input_filename);
  int this_time = get_run_time ();
  file->time += this_time - body_time;

  fprintf (stderr, "\n******\n");
  print_time ("header files (total)", header_time);
  print_time ("main file (total)", this_time - body_time);
  fprintf (stderr, "ratio = %g : 1\n",
	   (double) header_time / (double) (this_time - body_time));
  fprintf (stderr, "\n******\n");

  splay_tree_foreach (file_info_tree, dump_one_header, 0);
}

static void
cb_ident (cpp_reader * ARG_UNUSED (pfile),
	  unsigned int ARG_UNUSED (line),
	  const cpp_string * ARG_UNUSED (str))
{
#ifdef ASM_OUTPUT_IDENT
  if (!flag_no_ident)
    {
      /* Convert escapes in the string.  */
      cpp_string cstr = { 0, 0 };
      if (cpp_interpret_string (pfile, str, 1, &cstr, false))
	{
	  ASM_OUTPUT_IDENT (asm_out_file, (const char *) cstr.text);
	  free ((void *) cstr.text);
	}
    }
#endif
}

/* Called at the start of every non-empty line.  TOKEN is the first
   lexed token on the line.  Used for diagnostic line numbers.  */
static void
cb_line_change (cpp_reader * ARG_UNUSED (pfile), const cpp_token *token,
		int parsing_args)
{
  if (token->type != CPP_EOF && !parsing_args)
#ifdef USE_MAPPED_LOCATION
    input_location = token->src_loc;
#else
    {
      source_location loc = token->src_loc;
      const struct line_map *map = linemap_lookup (&line_table, loc);
      input_line = SOURCE_LINE (map, loc);
    }
#endif
}

void
fe_file_change (const struct line_map *new_map)
{
  if (new_map == NULL)
    return;

  if (new_map->reason == LC_ENTER)
    {
      /* Don't stack the main buffer on the input stack;
	 we already did in compile_file.  */
      if (!MAIN_FILE_P (new_map))
	{
#ifdef USE_MAPPED_LOCATION
	  int included_at = LAST_SOURCE_LINE_LOCATION (new_map - 1);

	  input_location = included_at;
	  push_srcloc (new_map->start_location);
#else
	  int included_at = LAST_SOURCE_LINE (new_map - 1);

	  input_line = included_at;
	  push_srcloc (new_map->to_file, 1);
#endif
	  (*debug_hooks->start_source_file) (included_at, new_map->to_file);
#ifndef NO_IMPLICIT_EXTERN_C
	  if (c_header_level)
	    ++c_header_level;
	  else if (new_map->sysp == 2)
	    {
	      c_header_level = 1;
	      ++pending_lang_change;
	    }
#endif
	}
    }
  else if (new_map->reason == LC_LEAVE)
    {
#ifndef NO_IMPLICIT_EXTERN_C
      if (c_header_level && --c_header_level == 0)
	{
	  if (new_map->sysp == 2)
	    warning (0, "badly nested C headers from preprocessor");
	  --pending_lang_change;
	}
#endif
      pop_srcloc ();

      (*debug_hooks->end_source_file) (new_map->to_line);
    }

  update_header_times (new_map->to_file);
  in_system_header = new_map->sysp != 0;
#ifdef USE_MAPPED_LOCATION
  input_location = new_map->start_location;
#else
  input_filename = new_map->to_file;
  input_line = new_map->to_line;
#endif
}

static void
cb_def_pragma (cpp_reader *pfile, source_location loc)
{
  /* Issue a warning message if we have been asked to do so.  Ignore
     unknown pragmas in system headers unless an explicit
     -Wunknown-pragmas has been given.  */
  if (warn_unknown_pragmas > in_system_header)
    {
      const unsigned char *space, *name;
      const cpp_token *s;
#ifndef USE_MAPPED_LOCATION
      location_t fe_loc;
      const struct line_map *map = linemap_lookup (&line_table, loc);
      fe_loc.file = map->to_file;
      fe_loc.line = SOURCE_LINE (map, loc);
#else
      location_t fe_loc = loc;
#endif

      space = name = (const unsigned char *) "";
      s = cpp_get_token (pfile);
      if (s->type != CPP_EOF)
	{
	  space = cpp_token_as_text (pfile, s);
	  s = cpp_get_token (pfile);
	  if (s->type == CPP_NAME)
	    name = cpp_token_as_text (pfile, s);
	}

      warning (OPT_Wunknown_pragmas, "%Hignoring #pragma %s %s",
	       &fe_loc, space, name);
    }
}

/* #define callback for DWARF and DWARF2 debug info.  */
static void
cb_define (cpp_reader *pfile, source_location loc, cpp_hashnode *node)
{
  const struct line_map *map = linemap_lookup (&line_table, loc);
  (*debug_hooks->define) (SOURCE_LINE (map, loc),
			  (const char *) cpp_macro_definition (pfile, node));
}

/* #undef callback for DWARF and DWARF2 debug info.  */
static void
cb_undef (cpp_reader * ARG_UNUSED (pfile), source_location loc,
	  cpp_hashnode *node)
{
  const struct line_map *map = linemap_lookup (&line_table, loc);
  (*debug_hooks->undef) (SOURCE_LINE (map, loc),
			 (const char *) NODE_NAME (node));
}

/* Read a token and return its type.  Fill *VALUE with its value, if
   applicable.  Fill *CPP_FLAGS with the token's flags, if it is
   non-NULL.  */

enum cpp_ttype
c_lex_with_flags (tree *value, location_t *loc, unsigned char *cpp_flags)
{
  static bool no_more_pch;
  const cpp_token *tok;
  enum cpp_ttype type;
  unsigned char add_flags = 0;

  timevar_push (TV_CPP);
 retry:
  tok = cpp_get_token (parse_in);
  type = tok->type;

 retry_after_at:
#ifdef USE_MAPPED_LOCATION
  *loc = tok->src_loc;
#else
  *loc = input_location;
#endif
  switch (type)
    {
    case CPP_PADDING:
      goto retry;

    case CPP_NAME:
      *value = HT_IDENT_TO_GCC_IDENT (HT_NODE (tok->val.node));
      break;

    case CPP_NUMBER:
      {
	unsigned int flags = cpp_classify_number (parse_in, tok);

	switch (flags & CPP_N_CATEGORY)
	  {
	  case CPP_N_INVALID:
	    /* cpplib has issued an error.  */
	    *value = error_mark_node;
	    errorcount++;
	    break;

	  case CPP_N_INTEGER:
	    /* C++ uses '0' to mark virtual functions as pure.
	       Set PURE_ZERO to pass this information to the C++ parser.  */
	    if (tok->val.str.len == 1 && *tok->val.str.text == '0')
	      add_flags = PURE_ZERO;
	    *value = interpret_integer (tok, flags);
	    break;

	  case CPP_N_FLOATING:
	    *value = interpret_float (tok, flags);
	    break;

	  default:
	    gcc_unreachable ();
	  }
      }
      break;

    case CPP_ATSIGN:
      /* An @ may give the next token special significance in Objective-C.  */
      if (c_dialect_objc ())
	{
	  location_t atloc = input_location;

	retry_at:
	  tok = cpp_get_token (parse_in);
	  type = tok->type;
	  switch (type)
	    {
	    case CPP_PADDING:
	      goto retry_at;

	    case CPP_STRING:
	    case CPP_WSTRING:
	      type = lex_string (tok, value, true);
	      break;

	    case CPP_NAME:
	      *value = HT_IDENT_TO_GCC_IDENT (HT_NODE (tok->val.node));
	      if (objc_is_reserved_word (*value))
		{
		  type = CPP_AT_NAME;
		  break;
		}
	      /* FALLTHROUGH */

	    default:
	      /* ... or not.  */
	      error ("%Hstray %<@%> in program", &atloc);
	      goto retry_after_at;
	    }
	  break;
	}

      /* FALLTHROUGH */
    case CPP_HASH:
    case CPP_PASTE:
      {
	unsigned char name[4];

	*cpp_spell_token (parse_in, tok, name, true) = 0;

	error ("stray %qs in program", name);
      }

      goto retry;

    case CPP_OTHER:
      {
	cppchar_t c = tok->val.str.text[0];

	if (c == '"' || c == '\'')
	  error ("missing terminating %c character", (int) c);
	else if (ISGRAPH (c))
	  error ("stray %qc in program", (int) c);
	else
	  error ("stray %<\\%o%> in program", (int) c);
      }
      goto retry;

    case CPP_CHAR:
    case CPP_WCHAR:
      *value = lex_charconst (tok);
      break;

    case CPP_STRING:
    case CPP_WSTRING:
      if (!c_lex_return_raw_strings)
	{
	  type = lex_string (tok, value, false);
	  break;
	}
      *value = build_string (tok->val.str.len, (char *) tok->val.str.text);
      break;
      
    case CPP_PRAGMA:
      *value = build_int_cst (NULL, tok->val.pragma);
      break;

      /* These tokens should not be visible outside cpplib.  */
    case CPP_HEADER_NAME:
    case CPP_COMMENT:
    case CPP_MACRO_ARG:
      gcc_unreachable ();

    default:
      *value = NULL_TREE;
      break;
    }

  if (cpp_flags)
    *cpp_flags = tok->flags | add_flags;

  if (!no_more_pch)
    {
      no_more_pch = true;
      c_common_no_more_pch ();
    }

  timevar_pop (TV_CPP);

  return type;
}

/* Returns the narrowest C-visible unsigned type, starting with the
   minimum specified by FLAGS, that can fit HIGH:LOW, or itk_none if
   there isn't one.  */

static enum integer_type_kind
narrowest_unsigned_type (unsigned HOST_WIDE_INT low,
			 unsigned HOST_WIDE_INT high,
			 unsigned int flags)
{
  enum integer_type_kind itk;

  if ((flags & CPP_N_WIDTH) == CPP_N_SMALL)
    itk = itk_unsigned_int;
  else if ((flags & CPP_N_WIDTH) == CPP_N_MEDIUM)
    itk = itk_unsigned_long;
  else
    itk = itk_unsigned_long_long;

  for (; itk < itk_none; itk += 2 /* skip unsigned types */)
    {
      tree upper = TYPE_MAX_VALUE (integer_types[itk]);

      if ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (upper) > high
	  || ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (upper) == high
	      && TREE_INT_CST_LOW (upper) >= low))
	return itk;
    }

  return itk_none;
}

/* Ditto, but narrowest signed type.  */
static enum integer_type_kind
narrowest_signed_type (unsigned HOST_WIDE_INT low,
		       unsigned HOST_WIDE_INT high, unsigned int flags)
{
  enum integer_type_kind itk;

  if ((flags & CPP_N_WIDTH) == CPP_N_SMALL)
    itk = itk_int;
  else if ((flags & CPP_N_WIDTH) == CPP_N_MEDIUM)
    itk = itk_long;
  else
    itk = itk_long_long;


  for (; itk < itk_none; itk += 2 /* skip signed types */)
    {
      tree upper = TYPE_MAX_VALUE (integer_types[itk]);

      if ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (upper) > high
	  || ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (upper) == high
	      && TREE_INT_CST_LOW (upper) >= low))
	return itk;
    }

  return itk_none;
}

/* Interpret TOKEN, an integer with FLAGS as classified by cpplib.  */
static tree
interpret_integer (const cpp_token *token, unsigned int flags)
{
  tree value, type;
  enum integer_type_kind itk;
  cpp_num integer;
  cpp_options *options = cpp_get_options (parse_in);

  integer = cpp_interpret_integer (parse_in, token, flags);
  integer = cpp_num_sign_extend (integer, options->precision);

  /* The type of a constant with a U suffix is straightforward.  */
  if (flags & CPP_N_UNSIGNED)
    itk = narrowest_unsigned_type (integer.low, integer.high, flags);
  else
    {
      /* The type of a potentially-signed integer constant varies
	 depending on the base it's in, the standard in use, and the
	 length suffixes.  */
      enum integer_type_kind itk_u
	= narrowest_unsigned_type (integer.low, integer.high, flags);
      enum integer_type_kind itk_s
	= narrowest_signed_type (integer.low, integer.high, flags);

      /* In both C89 and C99, octal and hex constants may be signed or
	 unsigned, whichever fits tighter.  We do not warn about this
	 choice differing from the traditional choice, as the constant
	 is probably a bit pattern and either way will work.  */
      if ((flags & CPP_N_RADIX) != CPP_N_DECIMAL)
	itk = MIN (itk_u, itk_s);
      else
	{
	  /* In C99, decimal constants are always signed.
	     In C89, decimal constants that don't fit in long have
	     undefined behavior; we try to make them unsigned long.
	     In GCC's extended C89, that last is true of decimal
	     constants that don't fit in long long, too.  */

	  itk = itk_s;
	  if (itk_s > itk_u && itk_s > itk_long)
	    {
	      if (!flag_isoc99)
		{
		  if (itk_u < itk_unsigned_long)
		    itk_u = itk_unsigned_long;
		  itk = itk_u;
		  warning (0, "this decimal constant is unsigned only in ISO C90");
		}
	      else
		warning (OPT_Wtraditional,
			 "this decimal constant would be unsigned in ISO C90");
	    }
	}
    }

  if (itk == itk_none)
    /* cpplib has already issued a warning for overflow.  */
    type = ((flags & CPP_N_UNSIGNED)
	    ? widest_unsigned_literal_type_node
	    : widest_integer_literal_type_node);
  else
    type = integer_types[itk];

  if (itk > itk_unsigned_long
      && (flags & CPP_N_WIDTH) != CPP_N_LARGE
      && !in_system_header && !flag_isoc99)
    pedwarn ("integer constant is too large for %qs type",
	     (flags & CPP_N_UNSIGNED) ? "unsigned long" : "long");

  value = build_int_cst_wide (type, integer.low, integer.high);

  /* Convert imaginary to a complex type.  */
  if (flags & CPP_N_IMAGINARY)
    value = build_complex (NULL_TREE, build_int_cst (type, 0), value);

  return value;
}

/* Interpret TOKEN, a floating point number with FLAGS as classified
   by cpplib.  */
static tree
interpret_float (const cpp_token *token, unsigned int flags)
{
  tree type;
  tree value;
  REAL_VALUE_TYPE real;
  char *copy;
  size_t copylen;

  /* Default (no suffix) is double.  */
  if (flags & CPP_N_DEFAULT)
    {
      flags ^= CPP_N_DEFAULT;
      flags |= CPP_N_MEDIUM;
    }

  /* Decode type based on width and properties. */
  if (flags & CPP_N_DFLOAT)
    if ((flags & CPP_N_WIDTH) == CPP_N_LARGE)
      type = dfloat128_type_node;
    else if ((flags & CPP_N_WIDTH) == CPP_N_SMALL)
      type = dfloat32_type_node;
    else
      type = dfloat64_type_node;
  else
    if ((flags & CPP_N_WIDTH) == CPP_N_LARGE)
      type = long_double_type_node;
    else if ((flags & CPP_N_WIDTH) == CPP_N_SMALL
	     || flag_single_precision_constant)
      type = float_type_node;
    else
      type = double_type_node;

  /* Copy the constant to a nul-terminated buffer.  If the constant
     has any suffixes, cut them off; REAL_VALUE_ATOF/ REAL_VALUE_HTOF
     can't handle them.  */
  copylen = token->val.str.len;
  if (flags & CPP_N_DFLOAT) 
    copylen -= 2;
  else 
    {
      if ((flags & CPP_N_WIDTH) != CPP_N_MEDIUM)
	/* Must be an F or L suffix.  */
	copylen--;
      if (flags & CPP_N_IMAGINARY)
	/* I or J suffix.  */
	copylen--;
    }

  copy = (char *) alloca (copylen + 1);
  memcpy (copy, token->val.str.text, copylen);
  copy[copylen] = '\0';

  real_from_string3 (&real, copy, TYPE_MODE (type));

  /* Both C and C++ require a diagnostic for a floating constant
     outside the range of representable values of its type.  Since we
     have __builtin_inf* to produce an infinity, it might now be
     appropriate for this to be a mandatory pedwarn rather than
     conditioned on -pedantic.  */
  if (REAL_VALUE_ISINF (real) && pedantic)
    pedwarn ("floating constant exceeds range of %qT", type);

  /* Create a node with determined type and value.  */
  value = build_real (type, real);
  if (flags & CPP_N_IMAGINARY)
    value = build_complex (NULL_TREE, convert (type, integer_zero_node), value);

  return value;
}

/* Convert a series of STRING and/or WSTRING tokens into a tree,
   performing string constant concatenation.  TOK is the first of
   these.  VALP is the location to write the string into.  OBJC_STRING
   indicates whether an '@' token preceded the incoming token.
   Returns the CPP token type of the result (CPP_STRING, CPP_WSTRING,
   or CPP_OBJC_STRING).

   This is unfortunately more work than it should be.  If any of the
   strings in the series has an L prefix, the result is a wide string
   (6.4.5p4).  Whether or not the result is a wide string affects the
   meaning of octal and hexadecimal escapes (6.4.4.4p6,9).  But escape
   sequences do not continue across the boundary between two strings in
   a series (6.4.5p7), so we must not lose the boundaries.  Therefore
   cpp_interpret_string takes a vector of cpp_string structures, which
   we must arrange to provide.  */

static enum cpp_ttype
lex_string (const cpp_token *tok, tree *valp, bool objc_string)
{
  tree value;
  bool wide = false;
  size_t concats = 0;
  struct obstack str_ob;
  cpp_string istr;

  /* Try to avoid the overhead of creating and destroying an obstack
     for the common case of just one string.  */
  cpp_string str = tok->val.str;
  cpp_string *strs = &str;

  if (tok->type == CPP_WSTRING)
    wide = true;

 retry:
  tok = cpp_get_token (parse_in);
  switch (tok->type)
    {
    case CPP_PADDING:
      goto retry;
    case CPP_ATSIGN:
      if (c_dialect_objc ())
	{
	  objc_string = true;
	  goto retry;
	}
      /* FALLTHROUGH */

    default:
      break;

    case CPP_WSTRING:
      wide = true;
      /* FALLTHROUGH */

    case CPP_STRING:
      if (!concats)
	{
	  gcc_obstack_init (&str_ob);
	  obstack_grow (&str_ob, &str, sizeof (cpp_string));
	}

      concats++;
      obstack_grow (&str_ob, &tok->val.str, sizeof (cpp_string));
      goto retry;
    }

  /* We have read one more token than we want.  */
  _cpp_backup_tokens (parse_in, 1);
  if (concats)
    strs = XOBFINISH (&str_ob, cpp_string *);

  if (concats && !objc_string && !in_system_header)
    warning (OPT_Wtraditional,
	     "traditional C rejects string constant concatenation");

  if ((c_lex_string_translate
       ? cpp_interpret_string : cpp_interpret_string_notranslate)
      (parse_in, strs, concats + 1, &istr, wide))
    {
      value = build_string (istr.len, (char *) istr.text);
      free ((void *) istr.text);

      if (c_lex_string_translate == -1)
	{
	  int xlated = cpp_interpret_string_notranslate (parse_in, strs,
							 concats + 1,
							 &istr, wide);
	  /* Assume that, if we managed to translate the string above,
	     then the untranslated parsing will always succeed.  */
	  gcc_assert (xlated);

	  if (TREE_STRING_LENGTH (value) != (int) istr.len
	      || 0 != strncmp (TREE_STRING_POINTER (value), (char *) istr.text,
			       istr.len))
	    {
	      /* Arrange for us to return the untranslated string in
		 *valp, but to set up the C type of the translated
		 one.  */
	      *valp = build_string (istr.len, (char *) istr.text);
	      valp = &TREE_CHAIN (*valp);
	    }
	  free ((void *) istr.text);
	}
    }
  else
    {
      /* Callers cannot generally handle error_mark_node in this context,
	 so return the empty string instead.  cpp_interpret_string has
	 issued an error.  */
      if (wide)
	value = build_string (TYPE_PRECISION (wchar_type_node)
			      / TYPE_PRECISION (char_type_node),
			      "\0\0\0");  /* widest supported wchar_t
					     is 32 bits */
      else
	value = build_string (1, "");
    }

  TREE_TYPE (value) = wide ? wchar_array_type_node : char_array_type_node;
  *valp = fix_string_type (value);

  if (concats)
    obstack_free (&str_ob, 0);

  return objc_string ? CPP_OBJC_STRING : wide ? CPP_WSTRING : CPP_STRING;
}

/* Converts a (possibly wide) character constant token into a tree.  */
static tree
lex_charconst (const cpp_token *token)
{
  cppchar_t result;
  tree type, value;
  unsigned int chars_seen;
  int unsignedp;

  result = cpp_interpret_charconst (parse_in, token,
				    &chars_seen, &unsignedp);

  if (token->type == CPP_WCHAR)
    type = wchar_type_node;
  /* In C, a character constant has type 'int'.
     In C++ 'char', but multi-char charconsts have type 'int'.  */
  else if (!c_dialect_cxx () || chars_seen > 1)
    type = integer_type_node;
  else
    type = char_type_node;

  /* Cast to cppchar_signed_t to get correct sign-extension of RESULT
     before possibly widening to HOST_WIDE_INT for build_int_cst.  */
  if (unsignedp || (cppchar_signed_t) result >= 0)
    value = build_int_cst_wide (type, result, 0);
  else
    value = build_int_cst_wide (type, (cppchar_signed_t) result, -1);

  return value;
}
