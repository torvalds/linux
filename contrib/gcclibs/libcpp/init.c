/* CPP Library.
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Per Bothner, 1994-95.
   Based on CCCP program by Paul Rubin, June 1986
   Adapted to ANSI C, Richard Stallman, Jan 1987

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"
#include "mkdeps.h"
#include "localedir.h"

static void init_library (void);
static void mark_named_operators (cpp_reader *);
static void read_original_filename (cpp_reader *);
static void read_original_directory (cpp_reader *);
static void post_options (cpp_reader *);

/* If we have designated initializers (GCC >2.7) these tables can be
   initialized, constant data.  Otherwise, they have to be filled in at
   runtime.  */
#if HAVE_DESIGNATED_INITIALIZERS

#define init_trigraph_map()  /* Nothing.  */
#define TRIGRAPH_MAP \
__extension__ const uchar _cpp_trigraph_map[UCHAR_MAX + 1] = {

#define END };
#define s(p, v) [p] = v,

#else

#define TRIGRAPH_MAP uchar _cpp_trigraph_map[UCHAR_MAX + 1] = { 0 }; \
 static void init_trigraph_map (void) { \
 unsigned char *x = _cpp_trigraph_map;

#define END }
#define s(p, v) x[p] = v;

#endif

TRIGRAPH_MAP
  s('=', '#')	s(')', ']')	s('!', '|')
  s('(', '[')	s('\'', '^')	s('>', '}')
  s('/', '\\')	s('<', '{')	s('-', '~')
END

#undef s
#undef END
#undef TRIGRAPH_MAP

/* A set of booleans indicating what CPP features each source language
   requires.  */
struct lang_flags
{
  char c99;
  char cplusplus;
  char extended_numbers;
  char extended_identifiers;
  char std;
  char cplusplus_comments;
  char digraphs;
};

static const struct lang_flags lang_defaults[] =
{ /*              c99 c++ xnum xid std  //   digr  */
  /* GNUC89 */  { 0,  0,  1,   0,  0,   1,   1     },
  /* GNUC99 */  { 1,  0,  1,   0,  0,   1,   1     },
  /* STDC89 */  { 0,  0,  0,   0,  1,   0,   0     },
  /* STDC94 */  { 0,  0,  0,   0,  1,   0,   1     },
  /* STDC99 */  { 1,  0,  1,   0,  1,   1,   1     },
  /* GNUCXX */  { 0,  1,  1,   0,  0,   1,   1     },
  /* CXX98  */  { 0,  1,  1,   0,  1,   1,   1     },
  /* ASM    */  { 0,  0,  1,   0,  0,   1,   0     }
  /* xid should be 1 for GNUC99, STDC99, GNUCXX and CXX98 when no
     longer experimental (when all uses of identifiers in the compiler
     have been audited for correct handling of extended
     identifiers).  */
};

/* Sets internal flags correctly for a given language.  */
void
cpp_set_lang (cpp_reader *pfile, enum c_lang lang)
{
  const struct lang_flags *l = &lang_defaults[(int) lang];

  CPP_OPTION (pfile, lang) = lang;

  CPP_OPTION (pfile, c99)			 = l->c99;
  CPP_OPTION (pfile, cplusplus)			 = l->cplusplus;
  CPP_OPTION (pfile, extended_numbers)		 = l->extended_numbers;
  CPP_OPTION (pfile, extended_identifiers)	 = l->extended_identifiers;
  CPP_OPTION (pfile, std)			 = l->std;
  CPP_OPTION (pfile, trigraphs)			 = l->std;
  CPP_OPTION (pfile, cplusplus_comments)	 = l->cplusplus_comments;
  CPP_OPTION (pfile, digraphs)			 = l->digraphs;
}

/* Initialize library global state.  */
static void
init_library (void)
{
  static int initialized = 0;

  if (! initialized)
    {
      initialized = 1;

      /* Set up the trigraph map.  This doesn't need to do anything if
	 we were compiled with a compiler that supports C99 designated
	 initializers.  */
      init_trigraph_map ();

#ifdef ENABLE_NLS
       (void) bindtextdomain (PACKAGE, LOCALEDIR);
#endif
    }
}

/* Initialize a cpp_reader structure.  */
cpp_reader *
cpp_create_reader (enum c_lang lang, hash_table *table,
		   struct line_maps *line_table)
{
  cpp_reader *pfile;

  /* Initialize this instance of the library if it hasn't been already.  */
  init_library ();

  pfile = XCNEW (cpp_reader);

  cpp_set_lang (pfile, lang);
  /* APPLE LOCAL begin -Wnewline-eof 2001-08-23 --sts */
  /* Suppress warnings about missing newlines at ends of files.  */
  CPP_OPTION (pfile, warn_newline_at_eof) = 0;
  /* APPLE LOCAL end -Wnewline-eof 2001-08-23 --sts */
  CPP_OPTION (pfile, warn_multichar) = 1;
  CPP_OPTION (pfile, discard_comments) = 1;
  CPP_OPTION (pfile, discard_comments_in_macro_exp) = 1;
  CPP_OPTION (pfile, show_column) = 1;
  CPP_OPTION (pfile, tabstop) = 8;
  CPP_OPTION (pfile, operator_names) = 1;
  CPP_OPTION (pfile, warn_trigraphs) = 2;
  CPP_OPTION (pfile, warn_endif_labels) = 1;
  CPP_OPTION (pfile, warn_deprecated) = 1;
  CPP_OPTION (pfile, warn_long_long) = !CPP_OPTION (pfile, c99);
  CPP_OPTION (pfile, dollars_in_ident) = 1;
  CPP_OPTION (pfile, warn_dollars) = 1;
  CPP_OPTION (pfile, warn_variadic_macros) = 1;
  CPP_OPTION (pfile, warn_normalize) = normalized_C;

  /* Default CPP arithmetic to something sensible for the host for the
     benefit of dumb users like fix-header.  */
  CPP_OPTION (pfile, precision) = CHAR_BIT * sizeof (long);
  CPP_OPTION (pfile, char_precision) = CHAR_BIT;
  CPP_OPTION (pfile, wchar_precision) = CHAR_BIT * sizeof (int);
  CPP_OPTION (pfile, int_precision) = CHAR_BIT * sizeof (int);
  CPP_OPTION (pfile, unsigned_char) = 0;
  CPP_OPTION (pfile, unsigned_wchar) = 1;
  CPP_OPTION (pfile, bytes_big_endian) = 1;  /* does not matter */

  /* Default to no charset conversion.  */
  CPP_OPTION (pfile, narrow_charset) = _cpp_default_encoding ();
  CPP_OPTION (pfile, wide_charset) = 0;

  /* Default the input character set to UTF-8.  */
  CPP_OPTION (pfile, input_charset) = _cpp_default_encoding ();

  /* A fake empty "directory" used as the starting point for files
     looked up without a search path.  Name cannot be '/' because we
     don't want to prepend anything at all to filenames using it.  All
     other entries are correct zero-initialized.  */
  pfile->no_search_path.name = (char *) "";

  /* Initialize the line map.  */
  pfile->line_table = line_table;

  /* Initialize lexer state.  */
  pfile->state.save_comments = ! CPP_OPTION (pfile, discard_comments);

  /* Set up static tokens.  */
  pfile->avoid_paste.type = CPP_PADDING;
  pfile->avoid_paste.val.source = NULL;
  pfile->eof.type = CPP_EOF;
  pfile->eof.flags = 0;

  /* Create a token buffer for the lexer.  */
  _cpp_init_tokenrun (&pfile->base_run, 250);
  pfile->cur_run = &pfile->base_run;
  pfile->cur_token = pfile->base_run.base;

  /* Initialize the base context.  */
  pfile->context = &pfile->base_context;
  pfile->base_context.macro = 0;
  pfile->base_context.prev = pfile->base_context.next = 0;

  /* Aligned and unaligned storage.  */
  pfile->a_buff = _cpp_get_buff (pfile, 0);
  pfile->u_buff = _cpp_get_buff (pfile, 0);

  /* The expression parser stack.  */
  _cpp_expand_op_stack (pfile);

  /* Initialize the buffer obstack.  */
  _obstack_begin (&pfile->buffer_ob, 0, 0,
		  (void *(*) (long)) xmalloc,
		  (void (*) (void *)) free);

  _cpp_init_files (pfile);

  _cpp_init_hashtable (pfile, table);

  return pfile;
}

/* Free resources used by PFILE.  Accessing PFILE after this function
   returns leads to undefined behavior.  Returns the error count.  */
void
cpp_destroy (cpp_reader *pfile)
{
  cpp_context *context, *contextn;
  tokenrun *run, *runn;

  free (pfile->op_stack);

  while (CPP_BUFFER (pfile) != NULL)
    _cpp_pop_buffer (pfile);

  if (pfile->out.base)
    free (pfile->out.base);

  if (pfile->macro_buffer)
    {
      free (pfile->macro_buffer);
      pfile->macro_buffer = NULL;
      pfile->macro_buffer_len = 0;
    }

  if (pfile->deps)
    deps_free (pfile->deps);
  obstack_free (&pfile->buffer_ob, 0);

  _cpp_destroy_hashtable (pfile);
  _cpp_cleanup_files (pfile);
  _cpp_destroy_iconv (pfile);

  _cpp_free_buff (pfile->a_buff);
  _cpp_free_buff (pfile->u_buff);
  _cpp_free_buff (pfile->free_buffs);

  for (run = &pfile->base_run; run; run = runn)
    {
      runn = run->next;
      free (run->base);
      if (run != &pfile->base_run)
	free (run);
    }

  for (context = pfile->base_context.next; context; context = contextn)
    {
      contextn = context->next;
      free (context);
    }

  free (pfile);
}

/* This structure defines one built-in identifier.  A node will be
   entered in the hash table under the name NAME, with value VALUE.

   There are two tables of these.  builtin_array holds all the
   "builtin" macros: these are handled by builtin_macro() in
   macro.c.  Builtin is somewhat of a misnomer -- the property of
   interest is that these macros require special code to compute their
   expansions.  The value is a "builtin_type" enumerator.

   operator_array holds the C++ named operators.  These are keywords
   which act as aliases for punctuators.  In C++, they cannot be
   altered through #define, and #if recognizes them as operators.  In
   C, these are not entered into the hash table at all (but see
   <iso646.h>).  The value is a token-type enumerator.  */
struct builtin
{
  const uchar *name;
  unsigned short len;
  unsigned short value;
};

#define B(n, t)    { DSC(n), t }
static const struct builtin builtin_array[] =
{
  B("__TIMESTAMP__",	 BT_TIMESTAMP),
  B("__TIME__",		 BT_TIME),
  B("__DATE__",		 BT_DATE),
  B("__FILE__",		 BT_FILE),
  B("__BASE_FILE__",	 BT_BASE_FILE),
  B("__LINE__",		 BT_SPECLINE),
  B("__INCLUDE_LEVEL__", BT_INCLUDE_LEVEL),
  B("__COUNTER__",	 BT_COUNTER),
  /* Keep builtins not used for -traditional-cpp at the end, and
     update init_builtins() if any more are added.  */
  B("_Pragma",		 BT_PRAGMA),
  B("__STDC__",		 BT_STDC),
};

static const struct builtin operator_array[] =
{
  B("and",	CPP_AND_AND),
  B("and_eq",	CPP_AND_EQ),
  B("bitand",	CPP_AND),
  B("bitor",	CPP_OR),
  B("compl",	CPP_COMPL),
  B("not",	CPP_NOT),
  B("not_eq",	CPP_NOT_EQ),
  B("or",	CPP_OR_OR),
  B("or_eq",	CPP_OR_EQ),
  B("xor",	CPP_XOR),
  B("xor_eq",	CPP_XOR_EQ)
};
#undef B

/* Mark the C++ named operators in the hash table.  */
static void
mark_named_operators (cpp_reader *pfile)
{
  const struct builtin *b;

  for (b = operator_array;
       b < (operator_array + ARRAY_SIZE (operator_array));
       b++)
    {
      cpp_hashnode *hp = cpp_lookup (pfile, b->name, b->len);
      hp->flags |= NODE_OPERATOR;
      hp->is_directive = 0;
      hp->directive_index = b->value;
    }
}

void
cpp_init_special_builtins (cpp_reader *pfile)
{
  const struct builtin *b;
  size_t n = ARRAY_SIZE (builtin_array);

  if (CPP_OPTION (pfile, traditional))
    n -= 2;
  else if (! CPP_OPTION (pfile, stdc_0_in_system_headers)
	   || CPP_OPTION (pfile, std))
    n--;

  for (b = builtin_array; b < builtin_array + n; b++)
    {
      cpp_hashnode *hp = cpp_lookup (pfile, b->name, b->len);
      hp->type = NT_MACRO;
      hp->flags |= NODE_BUILTIN | NODE_WARN;
      hp->value.builtin = (enum builtin_type) b->value;
    }
}

/* Read the builtins table above and enter them, and language-specific
   macros, into the hash table.  HOSTED is true if this is a hosted
   environment.  */
void
cpp_init_builtins (cpp_reader *pfile, int hosted)
{
  cpp_init_special_builtins (pfile);

  if (!CPP_OPTION (pfile, traditional)
      && (! CPP_OPTION (pfile, stdc_0_in_system_headers)
	  || CPP_OPTION (pfile, std)))
    _cpp_define_builtin (pfile, "__STDC__ 1");

  if (CPP_OPTION (pfile, cplusplus))
    _cpp_define_builtin (pfile, "__cplusplus 1");
  else if (CPP_OPTION (pfile, lang) == CLK_ASM)
    _cpp_define_builtin (pfile, "__ASSEMBLER__ 1");
  else if (CPP_OPTION (pfile, lang) == CLK_STDC94)
    _cpp_define_builtin (pfile, "__STDC_VERSION__ 199409L");
  else if (CPP_OPTION (pfile, c99))
    _cpp_define_builtin (pfile, "__STDC_VERSION__ 199901L");

  if (hosted)
    _cpp_define_builtin (pfile, "__STDC_HOSTED__ 1");
  else
    _cpp_define_builtin (pfile, "__STDC_HOSTED__ 0");

  if (CPP_OPTION (pfile, objc))
    _cpp_define_builtin (pfile, "__OBJC__ 1");
}

/* Sanity-checks are dependent on command-line options, so it is
   called as a subroutine of cpp_read_main_file ().  */
#if ENABLE_CHECKING
static void sanity_checks (cpp_reader *);
static void sanity_checks (cpp_reader *pfile)
{
  cppchar_t test = 0;
  size_t max_precision = 2 * CHAR_BIT * sizeof (cpp_num_part);

  /* Sanity checks for assumptions about CPP arithmetic and target
     type precisions made by cpplib.  */
  test--;
  if (test < 1)
    cpp_error (pfile, CPP_DL_ICE, "cppchar_t must be an unsigned type");

  if (CPP_OPTION (pfile, precision) > max_precision)
    cpp_error (pfile, CPP_DL_ICE,
	       "preprocessor arithmetic has maximum precision of %lu bits;"
	       " target requires %lu bits",
	       (unsigned long) max_precision,
	       (unsigned long) CPP_OPTION (pfile, precision));

  if (CPP_OPTION (pfile, precision) < CPP_OPTION (pfile, int_precision))
    cpp_error (pfile, CPP_DL_ICE,
	       "CPP arithmetic must be at least as precise as a target int");

  if (CPP_OPTION (pfile, char_precision) < 8)
    cpp_error (pfile, CPP_DL_ICE, "target char is less than 8 bits wide");

  if (CPP_OPTION (pfile, wchar_precision) < CPP_OPTION (pfile, char_precision))
    cpp_error (pfile, CPP_DL_ICE,
	       "target wchar_t is narrower than target char");

  if (CPP_OPTION (pfile, int_precision) < CPP_OPTION (pfile, char_precision))
    cpp_error (pfile, CPP_DL_ICE,
	       "target int is narrower than target char");

  /* This is assumed in eval_token() and could be fixed if necessary.  */
  if (sizeof (cppchar_t) > sizeof (cpp_num_part))
    cpp_error (pfile, CPP_DL_ICE,
	       "CPP half-integer narrower than CPP character");

  if (CPP_OPTION (pfile, wchar_precision) > BITS_PER_CPPCHAR_T)
    cpp_error (pfile, CPP_DL_ICE,
	       "CPP on this host cannot handle wide character constants over"
	       " %lu bits, but the target requires %lu bits",
	       (unsigned long) BITS_PER_CPPCHAR_T,
	       (unsigned long) CPP_OPTION (pfile, wchar_precision));
}
#else
# define sanity_checks(PFILE)
#endif

/* This is called after options have been parsed, and partially
   processed.  */
void
cpp_post_options (cpp_reader *pfile)
{
  sanity_checks (pfile);

  post_options (pfile);

  /* Mark named operators before handling command line macros.  */
  if (CPP_OPTION (pfile, cplusplus) && CPP_OPTION (pfile, operator_names))
    mark_named_operators (pfile);
}

/* Setup for processing input from the file named FNAME, or stdin if
   it is the empty string.  Return the original filename
   on success (e.g. foo.i->foo.c), or NULL on failure.  */
const char *
cpp_read_main_file (cpp_reader *pfile, const char *fname)
{
  if (CPP_OPTION (pfile, deps.style) != DEPS_NONE)
    {
      if (!pfile->deps)
	pfile->deps = deps_init ();

      /* Set the default target (if there is none already).  */
      deps_add_default_target (pfile->deps, fname);
    }

  pfile->main_file
    = _cpp_find_file (pfile, fname, &pfile->no_search_path, false, 0);
  if (_cpp_find_failed (pfile->main_file))
    return NULL;

  _cpp_stack_file (pfile, pfile->main_file, false);

  /* For foo.i, read the original filename foo.c now, for the benefit
     of the front ends.  */
  if (CPP_OPTION (pfile, preprocessed))
    {
      read_original_filename (pfile);
      fname = pfile->line_table->maps[pfile->line_table->used-1].to_file;
    }
  return fname;
}

/* For preprocessed files, if the first tokens are of the form # NUM.
   handle the directive so we know the original file name.  This will
   generate file_change callbacks, which the front ends must handle
   appropriately given their state of initialization.  */
static void
read_original_filename (cpp_reader *pfile)
{
  const cpp_token *token, *token1;

  /* Lex ahead; if the first tokens are of the form # NUM, then
     process the directive, otherwise back up.  */
  token = _cpp_lex_direct (pfile);
  if (token->type == CPP_HASH)
    {
      pfile->state.in_directive = 1;
      token1 = _cpp_lex_direct (pfile);
      _cpp_backup_tokens (pfile, 1);
      pfile->state.in_directive = 0;

      /* If it's a #line directive, handle it.  */
      if (token1->type == CPP_NUMBER)
	{
	  _cpp_handle_directive (pfile, token->flags & PREV_WHITE);
	  read_original_directory (pfile);
	  return;
	}
    }

  /* Backup as if nothing happened.  */
  _cpp_backup_tokens (pfile, 1);
}

/* For preprocessed files, if the tokens following the first filename
   line is of the form # <line> "/path/name//", handle the
   directive so we know the original current directory.  */
static void
read_original_directory (cpp_reader *pfile)
{
  const cpp_token *hash, *token;

  /* Lex ahead; if the first tokens are of the form # NUM, then
     process the directive, otherwise back up.  */
  hash = _cpp_lex_direct (pfile);
  if (hash->type != CPP_HASH)
    {
      _cpp_backup_tokens (pfile, 1);
      return;
    }

  token = _cpp_lex_direct (pfile);

  if (token->type != CPP_NUMBER)
    {
      _cpp_backup_tokens (pfile, 2);
      return;
    }

  token = _cpp_lex_direct (pfile);

  if (token->type != CPP_STRING
      || ! (token->val.str.len >= 5
	    && token->val.str.text[token->val.str.len-2] == '/'
	    && token->val.str.text[token->val.str.len-3] == '/'))
    {
      _cpp_backup_tokens (pfile, 3);
      return;
    }

  if (pfile->cb.dir_change)
    {
      char *debugdir = (char *) alloca (token->val.str.len - 3);

      memcpy (debugdir, (const char *) token->val.str.text + 1,
	      token->val.str.len - 4);
      debugdir[token->val.str.len - 4] = '\0';

      pfile->cb.dir_change (pfile, debugdir);
    }      
}

/* This is called at the end of preprocessing.  It pops the last
   buffer and writes dependency output, and returns the number of
   errors.

   Maybe it should also reset state, such that you could call
   cpp_start_read with a new filename to restart processing.  */
int
cpp_finish (cpp_reader *pfile, FILE *deps_stream)
{
  /* Warn about unused macros before popping the final buffer.  */
  if (CPP_OPTION (pfile, warn_unused_macros))
    cpp_forall_identifiers (pfile, _cpp_warn_if_unused_macro, NULL);

  /* lex.c leaves the final buffer on the stack.  This it so that
     it returns an unending stream of CPP_EOFs to the client.  If we
     popped the buffer, we'd dereference a NULL buffer pointer and
     segfault.  It's nice to allow the client to do worry-free excess
     cpp_get_token calls.  */
  while (pfile->buffer)
    _cpp_pop_buffer (pfile);

  /* Don't write the deps file if there are errors.  */
  if (CPP_OPTION (pfile, deps.style) != DEPS_NONE
      && deps_stream && pfile->errors == 0)
    {
      deps_write (pfile->deps, deps_stream, 72);

      if (CPP_OPTION (pfile, deps.phony_targets))
	deps_phony_targets (pfile->deps, deps_stream);
    }

  /* Report on headers that could use multiple include guards.  */
  if (CPP_OPTION (pfile, print_include_names))
    _cpp_report_missing_guards (pfile);

  return pfile->errors;
}

static void
post_options (cpp_reader *pfile)
{
  /* -Wtraditional is not useful in C++ mode.  */
  if (CPP_OPTION (pfile, cplusplus))
    CPP_OPTION (pfile, warn_traditional) = 0;

  /* Permanently disable macro expansion if we are rescanning
     preprocessed text.  Read preprocesed source in ISO mode.  */
  if (CPP_OPTION (pfile, preprocessed))
    {
      if (!CPP_OPTION (pfile, directives_only))
	pfile->state.prevent_expansion = 1;
      CPP_OPTION (pfile, traditional) = 0;
    }

  if (CPP_OPTION (pfile, warn_trigraphs) == 2)
    CPP_OPTION (pfile, warn_trigraphs) = !CPP_OPTION (pfile, trigraphs);

  if (CPP_OPTION (pfile, traditional))
    {
      CPP_OPTION (pfile, cplusplus_comments) = 0;

      /* Traditional CPP does not accurately track column information.  */
      CPP_OPTION (pfile, show_column) = 0;
      CPP_OPTION (pfile, trigraphs) = 0;
      CPP_OPTION (pfile, warn_trigraphs) = 0;
    }
}
