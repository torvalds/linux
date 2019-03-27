/* Protoize program - Original version by Ron Guilmette (rfg@segfault.us.com).
   Copyright (C) 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "intl.h"
#include "cppdefault.h"

#include <setjmp.h>
#include <signal.h>
#if ! defined( SIGCHLD ) && defined( SIGCLD )
#  define SIGCHLD SIGCLD
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "version.h"

/* Include getopt.h for the sake of getopt_long.  */
#include "getopt.h"

/* Macro to see if the path elements match.  */
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
#define IS_SAME_PATH_CHAR(a,b) (TOUPPER (a) == TOUPPER (b))
#else
#define IS_SAME_PATH_CHAR(a,b) ((a) == (b))
#endif

/* Macro to see if the paths match.  */
#define IS_SAME_PATH(a,b) (FILENAME_CMP (a, b) == 0)

/* Suffix for aux-info files.  */
#ifdef __MSDOS__
#define AUX_INFO_SUFFIX "X"
#else
#define AUX_INFO_SUFFIX ".X"
#endif

/* Suffix for saved files.  */
#ifdef __MSDOS__
#define SAVE_SUFFIX "sav"
#else
#define SAVE_SUFFIX ".save"
#endif

/* Suffix for renamed C++ files.  */
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
#define CPLUS_FILE_SUFFIX "cc"
#else
#define CPLUS_FILE_SUFFIX "C"
#endif

static void usage (void) ATTRIBUTE_NORETURN;
static void aux_info_corrupted (void) ATTRIBUTE_NORETURN;
static void declare_source_confusing (const char *) ATTRIBUTE_NORETURN;
static const char *shortpath (const char *, const char *);
static void notice (const char *, ...) ATTRIBUTE_PRINTF_1;
static char *savestring (const char *, unsigned int);
static char *dupnstr (const char *, size_t);
static int safe_read (int, void *, int);
static void safe_write (int, void *, int, const char *);
static void save_pointers (void);
static void restore_pointers (void);
static int is_id_char (int);
static int in_system_include_dir (const char *);
static int directory_specified_p (const char *);
static int file_excluded_p (const char *);
static char *unexpand_if_needed (const char *);
static char *abspath (const char *, const char *);
static void check_aux_info (int);
static const char *find_corresponding_lparen (const char *);
static int referenced_file_is_newer (const char *, time_t);
static void save_def_or_dec (const char *, int);
static void munge_compile_params (const char *);
static int gen_aux_info_file (const char *);
static void process_aux_info_file (const char *, int, int);
static int identify_lineno (const char *);
static void check_source (int, const char *);
static const char *seek_to_line (int);
static const char *forward_to_next_token_char (const char *);
static void output_bytes (const char *, size_t);
static void output_string (const char *);
static void output_up_to (const char *);
static int other_variable_style_function (const char *);
static const char *find_rightmost_formals_list (const char *);
static void do_cleaning (char *, const char *);
static const char *careful_find_l_paren (const char *);
static void do_processing (void);

/* Look for these where the `const' qualifier is intentionally cast aside.  */
#define NONCONST

/* Define a default place to find the SYSCALLS.X file.  */

#ifndef UNPROTOIZE

#ifndef STANDARD_EXEC_PREFIX
#define STANDARD_EXEC_PREFIX "/usr/local/lib/gcc-lib/"
#endif /* !defined STANDARD_EXEC_PREFIX */

static const char * const standard_exec_prefix = STANDARD_EXEC_PREFIX;
static const char * const target_machine = DEFAULT_TARGET_MACHINE;
static const char * const target_version = DEFAULT_TARGET_VERSION;

#endif /* !defined (UNPROTOIZE) */

/* Suffix of aux_info files.  */

static const char * const aux_info_suffix = AUX_INFO_SUFFIX;

/* String to attach to filenames for saved versions of original files.  */

static const char * const save_suffix = SAVE_SUFFIX;

#ifndef UNPROTOIZE

/* String to attach to C filenames renamed to C++.  */

static const char * const cplus_suffix = CPLUS_FILE_SUFFIX;

/* File name of the file which contains descriptions of standard system
   routines.  Note that we never actually do anything with this file per se,
   but we do read in its corresponding aux_info file.  */

static const char syscalls_filename[] = "SYSCALLS.c";

/* Default place to find the above file.  */

static const char * default_syscalls_dir;

/* Variable to hold the complete absolutized filename of the SYSCALLS.c.X
   file.  */

static char * syscalls_absolute_filename;

#endif /* !defined (UNPROTOIZE) */

/* Type of the structure that holds information about macro unexpansions.  */

struct unexpansion_struct {
  const char *const expanded;
  const char *const contracted;
};
typedef struct unexpansion_struct unexpansion;

/* A table of conversions that may need to be made for some (stupid) older
   operating systems where these types are preprocessor macros rather than
   typedefs (as they really ought to be).

   WARNING: The contracted forms must be as small (or smaller) as the
   expanded forms, or else havoc will ensue.  */

static const unexpansion unexpansions[] = {
  { "struct _iobuf", "FILE" },
  { 0, 0 }
};

/* The number of "primary" slots in the hash tables for filenames and for
   function names.  This can be as big or as small as you like, except that
   it must be a power of two.  */

#define HASH_TABLE_SIZE		(1 << 9)

/* Bit mask to use when computing hash values.  */

static const int hash_mask = (HASH_TABLE_SIZE - 1);


/* Datatype for lists of directories or filenames.  */
struct string_list
{
  const char *name;
  struct string_list *next;
};

static struct string_list *string_list_cons (const char *,
					     struct string_list *);

/* List of directories in which files should be converted.  */

struct string_list *directory_list;

/* List of file names which should not be converted.
   A file is excluded if the end of its name, following a /,
   matches one of the names in this list.  */

struct string_list *exclude_list;

/* The name of the other style of variable-number-of-parameters functions
   (i.e. the style that we want to leave unconverted because we don't yet
   know how to convert them to this style.  This string is used in warning
   messages.  */

/* Also define here the string that we can search for in the parameter lists
   taken from the .X files which will unambiguously indicate that we have
   found a varargs style function.  */

#ifdef UNPROTOIZE
static const char * const other_var_style = "stdarg";
#else /* !defined (UNPROTOIZE) */
static const char * const other_var_style = "varargs";
static const char *varargs_style_indicator = "va_alist";
#endif /* !defined (UNPROTOIZE) */

/* The following two types are used to create hash tables.  In this program,
   there are two hash tables which are used to store and quickly lookup two
   different classes of strings.  The first type of strings stored in the
   first hash table are absolute filenames of files which protoize needs to
   know about.  The second type of strings (stored in the second hash table)
   are function names.  It is this second class of strings which really
   inspired the use of the hash tables, because there may be a lot of them.  */

typedef struct hash_table_entry_struct hash_table_entry;

/* Do some typedefs so that we don't have to write "struct" so often.  */

typedef struct def_dec_info_struct def_dec_info;
typedef struct file_info_struct file_info;
typedef struct f_list_chain_item_struct f_list_chain_item;

#ifndef UNPROTOIZE
static int is_syscalls_file (const file_info *);
static void rename_c_file (const hash_table_entry *);
static const def_dec_info *find_extern_def (const def_dec_info *,
					    const def_dec_info *);
static const def_dec_info *find_static_definition (const def_dec_info *);
static void connect_defs_and_decs (const hash_table_entry *);
static void add_local_decl (const def_dec_info *, const char *);
static void add_global_decls (const file_info *, const char *);
#endif /* ! UNPROTOIZE */
static int needs_to_be_converted (const file_info *);
static void visit_each_hash_node (const hash_table_entry *,
				  void (*)(const hash_table_entry *));
static hash_table_entry *add_symbol (hash_table_entry *, const char *);
static hash_table_entry *lookup (hash_table_entry *, const char *);
static void free_def_dec (def_dec_info *);
static file_info *find_file (const char *, int);
static void reverse_def_dec_list (const hash_table_entry *);
static void edit_fn_declaration (const def_dec_info *, const char *);
static int edit_formals_lists (const char *, unsigned int,
			       const def_dec_info *);
static void edit_fn_definition (const def_dec_info *, const char *);
static void scan_for_missed_items (const file_info *);
static void edit_file (const hash_table_entry *);

/* In the struct below, note that the "_info" field has two different uses
   depending on the type of hash table we are in (i.e. either the filenames
   hash table or the function names hash table).  In the filenames hash table
   the info fields of the entries point to the file_info struct which is
   associated with each filename (1 per filename).  In the function names
   hash table, the info field points to the head of a singly linked list of
   def_dec_info entries which are all defs or decs of the function whose
   name is pointed to by the "symbol" field.  Keeping all of the defs/decs
   for a given function name on a special list specifically for that function
   name makes it quick and easy to find out all of the important information
   about a given (named) function.  */

struct hash_table_entry_struct {
  hash_table_entry *		hash_next;	/* -> to secondary entries */
  const char *			symbol;		/* -> to the hashed string */
  union {
    const def_dec_info *	_ddip;
    file_info *			_fip;
  } _info;
};
#define ddip _info._ddip
#define fip _info._fip

/* Define a type specifically for our two hash tables.  */

typedef hash_table_entry hash_table[HASH_TABLE_SIZE];

/* The following struct holds all of the important information about any
   single filename (e.g. file) which we need to know about.  */

struct file_info_struct {
  const hash_table_entry *	hash_entry; /* -> to associated hash entry */
  const def_dec_info *		defs_decs;  /* -> to chain of defs/decs */
  time_t			mtime;      /* Time of last modification.  */
};

/* Due to the possibility that functions may return pointers to functions,
   (which may themselves have their own parameter lists) and due to the
   fact that returned pointers-to-functions may be of type "pointer-to-
   function-returning-pointer-to-function" (ad nauseum) we have to keep
   an entire chain of ANSI style formal parameter lists for each function.

   Normally, for any given function, there will only be one formals list
   on the chain, but you never know.

   Note that the head of each chain of formals lists is pointed to by the
   `f_list_chain' field of the corresponding def_dec_info record.

   For any given chain, the item at the head of the chain is the *leftmost*
   parameter list seen in the actual C language function declaration.  If
   there are other members of the chain, then these are linked in left-to-right
   order from the head of the chain.  */

struct f_list_chain_item_struct {
  const f_list_chain_item *	chain_next;	/* -> to next item on chain */
  const char *			formals_list;	/* -> to formals list string */
};

/* The following struct holds all of the important information about any
   single function definition or declaration which we need to know about.
   Note that for unprotoize we don't need to know very much because we
   never even create records for stuff that we don't intend to convert
   (like for instance defs and decs which are already in old K&R format
   and "implicit" function declarations).  */

struct def_dec_info_struct {
  const def_dec_info *	next_in_file;	/* -> to rest of chain for file */
  file_info *        	file;		/* -> file_info for containing file */
  int        		line;		/* source line number of def/dec */
  const char *		ansi_decl;	/* -> left end of ansi decl */
  hash_table_entry *	hash_entry;	/* -> hash entry for function name */
  unsigned int        	is_func_def;	/* = 0 means this is a declaration */
  const def_dec_info *	next_for_func;	/* -> to rest of chain for func name */
  unsigned int		f_list_count;	/* count of formals lists we expect */
  char			prototyped;	/* = 0 means already prototyped */
#ifndef UNPROTOIZE
  const f_list_chain_item * f_list_chain;	/* -> chain of formals lists */
  const def_dec_info *	definition;	/* -> def/dec containing related def */
  char	        	is_static;	/* = 0 means visibility is "extern"  */
  char			is_implicit;	/* != 0 for implicit func decl's */
  char			written;	/* != 0 means written for implicit */
#else /* !defined (UNPROTOIZE) */
  const char *		formal_names;	/* -> to list of names of formals */
  const char *		formal_decls;	/* -> to string of formal declarations */
#endif /* !defined (UNPROTOIZE) */
};

/* Pointer to the tail component of the filename by which this program was
   invoked.  Used everywhere in error and warning messages.  */

static const char *pname;

/* Error counter.  Will be nonzero if we should give up at the next convenient
   stopping point.  */

static int errors = 0;

/* Option flags.  */
/* ??? The variables are not marked static because some of them have
   the same names as gcc variables declared in options.h.  */
/* ??? These comments should say what the flag mean as well as the options
   that set them.  */

/* File name to use for running gcc.  Allows GCC 2 to be named
   something other than gcc.  */
static const char *compiler_file_name = "gcc";

int version_flag = 0;		/* Print our version number.  */
int quiet_flag = 0;		/* Don't print messages normally.  */
int nochange_flag = 0;		/* Don't convert, just say what files
				   we would have converted.  */
int nosave_flag = 0;		/* Don't save the old version.  */
int keep_flag = 0;		/* Don't delete the .X files.  */
static const char ** compile_params = 0;	/* Option string for gcc.  */
#ifdef UNPROTOIZE
static const char *indent_string = "     ";	/* Indentation for newly
						   inserted parm decls.  */
#else /* !defined (UNPROTOIZE) */
int local_flag = 0;		/* Insert new local decls (when?).  */
int global_flag = 0;		/* set by -g option */
int cplusplus_flag = 0;		/* Rename converted files to *.C.  */
static const char *nondefault_syscalls_dir = 0; /* Dir to look for
						   SYSCALLS.c.X in.  */
#endif /* !defined (UNPROTOIZE) */

/* An index into the compile_params array where we should insert the source
   file name when we are ready to exec the C compiler.  A zero value indicates
   that we have not yet called munge_compile_params.  */

static int input_file_name_index = 0;

/* An index into the compile_params array where we should insert the filename
   for the aux info file, when we run the C compiler.  */
static int aux_info_file_name_index = 0;

/* Count of command line arguments which were "filename" arguments.  */

static int n_base_source_files = 0;

/* Points to a malloc'ed list of pointers to all of the filenames of base
   source files which were specified on the command line.  */

static const char **base_source_filenames;

/* Line number of the line within the current aux_info file that we
   are currently processing.  Used for error messages in case the prototypes
   info file is corrupted somehow.  */

static int current_aux_info_lineno;

/* Pointer to the name of the source file currently being converted.  */

static const char *convert_filename;

/* Pointer to relative root string (taken from aux_info file) which indicates
   where directory the user was in when he did the compilation step that
   produced the containing aux_info file.  */

static const char *invocation_filename;

/* Pointer to the base of the input buffer that holds the original text for the
   source file currently being converted.  */

static const char *orig_text_base;

/* Pointer to the byte just beyond the end of the input buffer that holds the
   original text for the source file currently being converted.  */

static const char *orig_text_limit;

/* Pointer to the base of the input buffer that holds the cleaned text for the
   source file currently being converted.  */

static const char *clean_text_base;

/* Pointer to the byte just beyond the end of the input buffer that holds the
   cleaned text for the source file currently being converted.  */

static const char *clean_text_limit;

/* Pointer to the last byte in the cleaned text buffer that we have already
   (virtually) copied to the output buffer (or decided to ignore).  */

static const char * clean_read_ptr;

/* Pointer to the base of the output buffer that holds the replacement text
   for the source file currently being converted.  */

static char *repl_text_base;

/* Pointer to the byte just beyond the end of the output buffer that holds the
   replacement text for the source file currently being converted.  */

static char *repl_text_limit;

/* Pointer to the last byte which has been stored into the output buffer.
   The next byte to be stored should be stored just past where this points
   to.  */

static char * repl_write_ptr;

/* Pointer into the cleaned text buffer for the source file we are currently
   converting.  This points to the first character of the line that we last
   did a "seek_to_line" to (see below).  */

static const char *last_known_line_start;

/* Number of the line (in the cleaned text buffer) that we last did a
   "seek_to_line" to.  Will be one if we just read a new source file
   into the cleaned text buffer.  */

static int last_known_line_number;

/* The filenames hash table.  */

static hash_table filename_primary;

/* The function names hash table.  */

static hash_table function_name_primary;

/* The place to keep the recovery address which is used only in cases where
   we get hopelessly confused by something in the cleaned original text.  */

static jmp_buf source_confusion_recovery;

/* A pointer to the current directory filename (used by abspath).  */

static char *cwd_buffer;

/* A place to save the read pointer until we are sure that an individual
   attempt at editing will succeed.  */

static const char * saved_clean_read_ptr;

/* A place to save the write pointer until we are sure that an individual
   attempt at editing will succeed.  */

static char * saved_repl_write_ptr;

/* Translate and output an error message.  */
static void
notice (const char *cmsgid, ...)
{
  va_list ap;
  
  va_start (ap, cmsgid);
  vfprintf (stderr, _(cmsgid), ap);
  va_end (ap);
}


/* Make a copy of a string INPUT with size SIZE.  */

static char *
savestring (const char *input, unsigned int size)
{
  char *output = xmalloc (size + 1);
  strcpy (output, input);
  return output;
}


/* Make a duplicate of the first N bytes of a given string in a newly
   allocated area.  */

static char *
dupnstr (const char *s, size_t n)
{
  char *ret_val = xmalloc (n + 1);

  strncpy (ret_val, s, n);
  ret_val[n] = '\0';
  return ret_val;
}

/* Read LEN bytes at PTR from descriptor DESC, for file FILENAME,
   retrying if necessary.  Return the actual number of bytes read.  */

static int
safe_read (int desc, void *ptr, int len)
{
  int left = len;
  while (left > 0) {
    int nchars = read (desc, ptr, left);
    if (nchars < 0)
      {
#ifdef EINTR
	if (errno == EINTR)
	  continue;
#endif
	return nchars;
      }
    if (nchars == 0)
      break;
    /* Arithmetic on void pointers is a gcc extension.  */
    ptr = (char *) ptr + nchars;
    left -= nchars;
  }
  return len - left;
}

/* Write LEN bytes at PTR to descriptor DESC,
   retrying if necessary, and treating any real error as fatal.  */

static void
safe_write (int desc, void *ptr, int len, const char *out_fname)
{
  while (len > 0) {
    int written = write (desc, ptr, len);
    if (written < 0)
      {
	int errno_val = errno;
#ifdef EINTR
	if (errno_val == EINTR)
	  continue;
#endif
	notice ("%s: error writing file '%s': %s\n",
		pname, shortpath (NULL, out_fname), xstrerror (errno_val));
	return;
      }
    /* Arithmetic on void pointers is a gcc extension.  */
    ptr = (char *) ptr + written;
    len -= written;
  }
}

/* Get setup to recover in case the edit we are about to do goes awry.  */

static void
save_pointers (void)
{
  saved_clean_read_ptr = clean_read_ptr;
  saved_repl_write_ptr = repl_write_ptr;
}

/* Call this routine to recover our previous state whenever something looks
   too confusing in the source code we are trying to edit.  */

static void
restore_pointers (void)
{
  clean_read_ptr = saved_clean_read_ptr;
  repl_write_ptr = saved_repl_write_ptr;
}

/* Return true if the given character is a valid identifier character.  */

static int
is_id_char (int ch)
{
  return (ISIDNUM (ch) || (ch == '$'));
}

/* Give a message indicating the proper way to invoke this program and then
   exit with nonzero status.  */

static void
usage (void)
{
#ifdef UNPROTOIZE
  notice ("%s: usage '%s [ -VqfnkN ] [ -i <istring> ] [ filename ... ]'\n",
	  pname, pname);
#else /* !defined (UNPROTOIZE) */
  notice ("%s: usage '%s [ -VqfnkNlgC ] [ -B <dirname> ] [ filename ... ]'\n",
	  pname, pname);
#endif /* !defined (UNPROTOIZE) */
  exit (FATAL_EXIT_CODE);
}

/* Return true if the given filename (assumed to be an absolute filename)
   designates a file residing anywhere beneath any one of the "system"
   include directories.  */

static int
in_system_include_dir (const char *path)
{
  const struct default_include *p;

  gcc_assert (IS_ABSOLUTE_PATH (path));

  for (p = cpp_include_defaults; p->fname; p++)
    if (!strncmp (path, p->fname, strlen (p->fname))
	&& IS_DIR_SEPARATOR (path[strlen (p->fname)]))
      return 1;
  return 0;
}

#if 0
/* Return true if the given filename designates a file that the user has
   read access to and for which the user has write access to the containing
   directory.  */

static int
file_could_be_converted (const char *path)
{
  char *const dir_name = alloca (strlen (path) + 1);

  if (access (path, R_OK))
    return 0;

  {
    char *dir_last_slash;

    strcpy (dir_name, path);
    dir_last_slash = strrchr (dir_name, DIR_SEPARATOR);
#ifdef DIR_SEPARATOR_2
    {
      char *slash;

      slash = strrchr (dir_last_slash ? dir_last_slash : dir_name,
		       DIR_SEPARATOR_2);
      if (slash)
	dir_last_slash = slash;
    }
#endif
    gcc_assert (dir_last_slash);
    *dir_last_slash = '\0';
  }

  if (access (path, W_OK))
    return 0;

  return 1;
}

/* Return true if the given filename designates a file that we are allowed
   to modify.  Files which we should not attempt to modify are (a) "system"
   include files, and (b) files which the user doesn't have write access to,
   and (c) files which reside in directories which the user doesn't have
   write access to.  Unless requested to be quiet, give warnings about
   files that we will not try to convert for one reason or another.  An
   exception is made for "system" include files, which we never try to
   convert and for which we don't issue the usual warnings.  */

static int
file_normally_convertible (const char *path)
{
  char *const dir_name = alloca (strlen (path) + 1);

  if (in_system_include_dir (path))
    return 0;

  {
    char *dir_last_slash;

    strcpy (dir_name, path);
    dir_last_slash = strrchr (dir_name, DIR_SEPARATOR);
#ifdef DIR_SEPARATOR_2
    {
      char *slash;

      slash = strrchr (dir_last_slash ? dir_last_slash : dir_name,
		       DIR_SEPARATOR_2);
      if (slash)
	dir_last_slash = slash;
    }
#endif
    gcc_assert (dir_last_slash);
    *dir_last_slash = '\0';
  }

  if (access (path, R_OK))
    {
      if (!quiet_flag)
	notice ("%s: warning: no read access for file '%s'\n",
		pname, shortpath (NULL, path));
      return 0;
    }

  if (access (path, W_OK))
    {
      if (!quiet_flag)
	notice ("%s: warning: no write access for file '%s'\n",
		pname, shortpath (NULL, path));
      return 0;
    }

  if (access (dir_name, W_OK))
    {
      if (!quiet_flag)
	notice ("%s: warning: no write access for dir containing '%s'\n",
		pname, shortpath (NULL, path));
      return 0;
    }

  return 1;
}
#endif /* 0 */

#ifndef UNPROTOIZE

/* Return true if the given file_info struct refers to the special SYSCALLS.c.X
   file.  Return false otherwise.  */

static int
is_syscalls_file (const file_info *fi_p)
{
  char const *f = fi_p->hash_entry->symbol;
  size_t fl = strlen (f), sysl = sizeof (syscalls_filename) - 1;
  return sysl <= fl  &&  strcmp (f + fl - sysl, syscalls_filename) == 0;
}

#endif /* !defined (UNPROTOIZE) */

/* Check to see if this file will need to have anything done to it on this
   run.  If there is nothing in the given file which both needs conversion
   and for which we have the necessary stuff to do the conversion, return
   false.  Otherwise, return true.

   Note that (for protoize) it is only valid to call this function *after*
   the connections between declarations and definitions have all been made
   by connect_defs_and_decs.  */

static int
needs_to_be_converted (const file_info *file_p)
{
  const def_dec_info *ddp;

#ifndef UNPROTOIZE

  if (is_syscalls_file (file_p))
    return 0;

#endif /* !defined (UNPROTOIZE) */

  for (ddp = file_p->defs_decs; ddp; ddp = ddp->next_in_file)

    if (

#ifndef UNPROTOIZE

      /* ... and if we a protoizing and this function is in old style ...  */
      !ddp->prototyped
      /* ... and if this a definition or is a decl with an associated def ...  */
      && (ddp->is_func_def || (!ddp->is_func_def && ddp->definition))

#else /* defined (UNPROTOIZE) */

      /* ... and if we are unprotoizing and this function is in new style ...  */
      ddp->prototyped

#endif /* defined (UNPROTOIZE) */
      )
	  /* ... then the containing file needs converting.  */
	  return -1;
  return 0;
}

/* Return 1 if the file name NAME is in a directory
   that should be converted.  */

static int
directory_specified_p (const char *name)
{
  struct string_list *p;

  for (p = directory_list; p; p = p->next)
    if (!strncmp (name, p->name, strlen (p->name))
	&& IS_DIR_SEPARATOR (name[strlen (p->name)]))
      {
	const char *q = name + strlen (p->name) + 1;

	/* If there are more slashes, it's in a subdir, so
	   this match doesn't count.  */
	while (*q++)
	  if (IS_DIR_SEPARATOR (*(q-1)))
	    goto lose;
	return 1;

      lose: ;
      }

  return 0;
}

/* Return 1 if the file named NAME should be excluded from conversion.  */

static int
file_excluded_p (const char *name)
{
  struct string_list *p;
  int len = strlen (name);

  for (p = exclude_list; p; p = p->next)
    if (!strcmp (name + len - strlen (p->name), p->name)
	&& IS_DIR_SEPARATOR (name[len - strlen (p->name) - 1]))
      return 1;

  return 0;
}

/* Construct a new element of a string_list.
   STRING is the new element value, and REST holds the remaining elements.  */

static struct string_list *
string_list_cons (const char *string, struct string_list *rest)
{
  struct string_list *temp = xmalloc (sizeof (struct string_list));

  temp->next = rest;
  temp->name = string;
  return temp;
}

/* ??? The GNU convention for mentioning function args in its comments
   is to capitalize them.  So change "hash_tab_p" to HASH_TAB_P below.
   Likewise for all the other functions.  */

/* Given a hash table, apply some function to each node in the table. The
   table to traverse is given as the "hash_tab_p" argument, and the
   function to be applied to each node in the table is given as "func"
   argument.  */

static void
visit_each_hash_node (const hash_table_entry *hash_tab_p,
		      void (*func) (const hash_table_entry *))
{
  const hash_table_entry *primary;

  for (primary = hash_tab_p; primary < &hash_tab_p[HASH_TABLE_SIZE]; primary++)
    if (primary->symbol)
      {
	hash_table_entry *second;

	(*func)(primary);
	for (second = primary->hash_next; second; second = second->hash_next)
	  (*func) (second);
      }
}

/* Initialize all of the fields of a new hash table entry, pointed
   to by the "p" parameter.  Note that the space to hold the entry
   is assumed to have already been allocated before this routine is
   called.  */

static hash_table_entry *
add_symbol (hash_table_entry *p, const char *s)
{
  p->hash_next = NULL;
  p->symbol = xstrdup (s);
  p->ddip = NULL;
  p->fip = NULL;
  return p;
}

/* Look for a particular function name or filename in the particular
   hash table indicated by "hash_tab_p".  If the name is not in the
   given hash table, add it.  Either way, return a pointer to the
   hash table entry for the given name.  */

static hash_table_entry *
lookup (hash_table_entry *hash_tab_p, const char *search_symbol)
{
  int hash_value = 0;
  const char *search_symbol_char_p = search_symbol;
  hash_table_entry *p;

  while (*search_symbol_char_p)
    hash_value += *search_symbol_char_p++;
  hash_value &= hash_mask;
  p = &hash_tab_p[hash_value];
  if (! p->symbol)
      return add_symbol (p, search_symbol);
  if (!strcmp (p->symbol, search_symbol))
    return p;
  while (p->hash_next)
    {
      p = p->hash_next;
      if (!strcmp (p->symbol, search_symbol))
	return p;
    }
  p->hash_next = xmalloc (sizeof (hash_table_entry));
  p = p->hash_next;
  return add_symbol (p, search_symbol);
}

/* Throw a def/dec record on the junk heap.

   Also, since we are not using this record anymore, free up all of the
   stuff it pointed to.  */

static void
free_def_dec (def_dec_info *p)
{
  free ((NONCONST void *) p->ansi_decl);

#ifndef UNPROTOIZE
  {
    const f_list_chain_item * curr;
    const f_list_chain_item * next;

    for (curr = p->f_list_chain; curr; curr = next)
      {
	next = curr->chain_next;
	free ((NONCONST void *) curr);
      }
  }
#endif /* !defined (UNPROTOIZE) */

  free (p);
}

/* Unexpand as many macro symbols as we can find.

   If the given line must be unexpanded, make a copy of it in the heap and
   return a pointer to the unexpanded copy.  Otherwise return NULL.  */

static char *
unexpand_if_needed (const char *aux_info_line)
{
  static char *line_buf = 0;
  static int line_buf_size = 0;
  const unexpansion *unexp_p;
  int got_unexpanded = 0;
  const char *s;
  char *copy_p = line_buf;

  if (line_buf == 0)
    {
      line_buf_size = 1024;
      line_buf = xmalloc (line_buf_size);
    }

  copy_p = line_buf;

  /* Make a copy of the input string in line_buf, expanding as necessary.  */

  for (s = aux_info_line; *s != '\n'; )
    {
      for (unexp_p = unexpansions; unexp_p->expanded; unexp_p++)
	{
	  const char *in_p = unexp_p->expanded;
	  size_t len = strlen (in_p);

	  if (*s == *in_p && !strncmp (s, in_p, len) && !is_id_char (s[len]))
	    {
	      int size = strlen (unexp_p->contracted);
	      got_unexpanded = 1;
	      if (copy_p + size - line_buf >= line_buf_size)
		{
		  int offset = copy_p - line_buf;
		  line_buf_size *= 2;
		  line_buf_size += size;
		  line_buf = xrealloc (line_buf, line_buf_size);
		  copy_p = line_buf + offset;
		}
	      strcpy (copy_p, unexp_p->contracted);
	      copy_p += size;

	      /* Assume that there will not be another replacement required
	         within the text just replaced.  */

	      s += len;
	      goto continue_outer;
	    }
	}
      if (copy_p - line_buf == line_buf_size)
	{
	  int offset = copy_p - line_buf;
	  line_buf_size *= 2;
	  line_buf = xrealloc (line_buf, line_buf_size);
	  copy_p = line_buf + offset;
	}
      *copy_p++ = *s++;
continue_outer: ;
    }
  if (copy_p + 2 - line_buf >= line_buf_size)
    {
      int offset = copy_p - line_buf;
      line_buf_size *= 2;
      line_buf = xrealloc (line_buf, line_buf_size);
      copy_p = line_buf + offset;
    }
  *copy_p++ = '\n';
  *copy_p = '\0';

  return (got_unexpanded ? savestring (line_buf, copy_p - line_buf) : 0);
}

/* Return the absolutized filename for the given relative
   filename.  Note that if that filename is already absolute, it may
   still be returned in a modified form because this routine also
   eliminates redundant slashes and single dots and eliminates double
   dots to get a shortest possible filename from the given input
   filename.  The absolutization of relative filenames is made by
   assuming that the given filename is to be taken as relative to
   the first argument (cwd) or to the current directory if cwd is
   NULL.  */

static char *
abspath (const char *cwd, const char *rel_filename)
{
  /* Setup the current working directory as needed.  */
  const char *const cwd2 = (cwd) ? cwd : cwd_buffer;
  char *const abs_buffer = alloca (strlen (cwd2) + strlen (rel_filename) + 2);
  char *endp = abs_buffer;
  char *outp, *inp;

  /* Copy the  filename (possibly preceded by the current working
     directory name) into the absolutization buffer.  */

  {
    const char *src_p;

    if (! IS_ABSOLUTE_PATH (rel_filename))
      {
	src_p = cwd2;
	while ((*endp++ = *src_p++))
	  continue;
	*(endp-1) = DIR_SEPARATOR;     		/* overwrite null */
      }
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
    else if (IS_DIR_SEPARATOR (rel_filename[0]))
      {
	/* A path starting with a directory separator is considered absolute
	   for dos based filesystems, but it's really not -- it's just the
	   convention used throughout GCC and it works. However, in this
	   case, we still need to prepend the drive spec from cwd_buffer.  */
	*endp++ = cwd2[0];
	*endp++ = cwd2[1];
      }
#endif
    src_p = rel_filename;
    while ((*endp++ = *src_p++))
      continue;
  }

  /* Now make a copy of abs_buffer into abs_buffer, shortening the
     filename (by taking out slashes and dots) as we go.  */

  outp = inp = abs_buffer;
  *outp++ = *inp++;        	/* copy first slash */
#if defined (apollo) || defined (_WIN32) || defined (__INTERIX)
  if (IS_DIR_SEPARATOR (inp[0]))
    *outp++ = *inp++;        	/* copy second slash */
#endif
  for (;;)
    {
      if (!inp[0])
	break;
      else if (IS_DIR_SEPARATOR (inp[0]) && IS_DIR_SEPARATOR (outp[-1]))
	{
	  inp++;
	  continue;
	}
      else if (inp[0] == '.' && IS_DIR_SEPARATOR (outp[-1]))
	{
	  if (!inp[1])
	    break;
	  else if (IS_DIR_SEPARATOR (inp[1]))
	    {
	      inp += 2;
	      continue;
	    }
	  else if ((inp[1] == '.') && (inp[2] == 0
	                               || IS_DIR_SEPARATOR (inp[2])))
	    {
	      inp += (IS_DIR_SEPARATOR (inp[2])) ? 3 : 2;
	      outp -= 2;
	      while (outp >= abs_buffer && ! IS_DIR_SEPARATOR (*outp))
	      	outp--;
	      if (outp < abs_buffer)
		{
		  /* Catch cases like /.. where we try to backup to a
		     point above the absolute root of the logical file
		     system.  */

		  notice ("%s: invalid file name: %s\n",
			  pname, rel_filename);
		  exit (FATAL_EXIT_CODE);
		}
	      *++outp = '\0';
	      continue;
	    }
	}
      *outp++ = *inp++;
    }

  /* On exit, make sure that there is a trailing null, and make sure that
     the last character of the returned string is *not* a slash.  */

  *outp = '\0';
  if (IS_DIR_SEPARATOR (outp[-1]))
    *--outp  = '\0';

  /* Make a copy (in the heap) of the stuff left in the absolutization
     buffer and return a pointer to the copy.  */

  return savestring (abs_buffer, outp - abs_buffer);
}

/* Given a filename (and possibly a directory name from which the filename
   is relative) return a string which is the shortest possible
   equivalent for the corresponding full (absolutized) filename.  The
   shortest possible equivalent may be constructed by converting the
   absolutized filename to be a relative filename (i.e. relative to
   the actual current working directory).  However if a relative filename
   is longer, then the full absolute filename is returned.

   KNOWN BUG:

   Note that "simple-minded" conversion of any given type of filename (either
   relative or absolute) may not result in a valid equivalent filename if any
   subpart of the original filename is actually a symbolic link.  */

static const char *
shortpath (const char *cwd, const char *filename)
{
  char *rel_buffer;
  char *rel_buf_p;
  char *cwd_p = cwd_buffer;
  char *path_p;
  int unmatched_slash_count = 0;
  size_t filename_len = strlen (filename);

  path_p = abspath (cwd, filename);
  rel_buf_p = rel_buffer = xmalloc (filename_len);

  while (*cwd_p && IS_SAME_PATH_CHAR (*cwd_p, *path_p))
    {
      cwd_p++;
      path_p++;
    }
  if (!*cwd_p && (!*path_p || IS_DIR_SEPARATOR (*path_p)))
    {
      /* whole pwd matched */
      if (!*path_p)        	/* input *is* the current path! */
	return ".";
      else
	return ++path_p;
    }
  else
    {
      if (*path_p)
	{
	  --cwd_p;
	  --path_p;
	  while (! IS_DIR_SEPARATOR (*cwd_p))     /* backup to last slash */
	    {
	      --cwd_p;
	      --path_p;
	    }
	  cwd_p++;
	  path_p++;
	  unmatched_slash_count++;
	}

      /* Find out how many directory levels in cwd were *not* matched.  */
      while (*cwd_p++)
	if (IS_DIR_SEPARATOR (*(cwd_p-1)))
	  unmatched_slash_count++;

      /* Now we know how long the "short name" will be.
	 Reject it if longer than the input.  */
      if (unmatched_slash_count * 3 + strlen (path_p) >= filename_len)
	return filename;

      /* For each of them, put a `../' at the beginning of the short name.  */
      while (unmatched_slash_count--)
	{
	  /* Give up if the result gets to be longer
	     than the absolute path name.  */
	  if (rel_buffer + filename_len <= rel_buf_p + 3)
	    return filename;
	  *rel_buf_p++ = '.';
	  *rel_buf_p++ = '.';
	  *rel_buf_p++ = DIR_SEPARATOR;
	}

      /* Then tack on the unmatched part of the desired file's name.  */
      do
	{
	  if (rel_buffer + filename_len <= rel_buf_p)
	    return filename;
	}
      while ((*rel_buf_p++ = *path_p++));

      --rel_buf_p;
      if (IS_DIR_SEPARATOR (*(rel_buf_p-1)))
	*--rel_buf_p = '\0';
      return rel_buffer;
    }
}

/* Lookup the given filename in the hash table for filenames.  If it is a
   new one, then the hash table info pointer will be null.  In this case,
   we create a new file_info record to go with the filename, and we initialize
   that record with some reasonable values.  */

/* FILENAME was const, but that causes a warning on AIX when calling stat.
   That is probably a bug in AIX, but might as well avoid the warning.  */

static file_info *
find_file (const char *filename, int do_not_stat)
{
  hash_table_entry *hash_entry_p;

  hash_entry_p = lookup (filename_primary, filename);
  if (hash_entry_p->fip)
    return hash_entry_p->fip;
  else
    {
      struct stat stat_buf;
      file_info *file_p = xmalloc (sizeof (file_info));

      /* If we cannot get status on any given source file, give a warning
	 and then just set its time of last modification to infinity.  */

      if (do_not_stat)
	stat_buf.st_mtime = (time_t) 0;
      else
	{
	  if (stat (filename, &stat_buf) == -1)
	    {
	      int errno_val = errno;
	      notice ("%s: %s: can't get status: %s\n",
		      pname, shortpath (NULL, filename),
		      xstrerror (errno_val));
	      stat_buf.st_mtime = (time_t) -1;
	    }
	}

      hash_entry_p->fip = file_p;
      file_p->hash_entry = hash_entry_p;
      file_p->defs_decs = NULL;
      file_p->mtime = stat_buf.st_mtime;
      return file_p;
    }
}

/* Generate a fatal error because some part of the aux_info file is
   messed up.  */

static void
aux_info_corrupted (void)
{
  notice ("\n%s: fatal error: aux info file corrupted at line %d\n",
	  pname, current_aux_info_lineno);
  exit (FATAL_EXIT_CODE);
}

/* ??? This comment is vague.  Say what the condition is for.  */
/* Check to see that a condition is true.  This is kind of like an assert.  */

static void
check_aux_info (int cond)
{
  if (! cond)
    aux_info_corrupted ();
}

/* Given a pointer to the closing right parenthesis for a particular formals
   list (in an aux_info file) find the corresponding left parenthesis and
   return a pointer to it.  */

static const char *
find_corresponding_lparen (const char *p)
{
  const char *q;
  int paren_depth;

  for (paren_depth = 1, q = p-1; paren_depth; q--)
    {
      switch (*q)
	{
	case ')':
	  paren_depth++;
	  break;
	case '(':
	  paren_depth--;
	  break;
	}
    }
  return ++q;
}

/* Given a line from  an aux info file, and a time at which the aux info
   file it came from was created, check to see if the item described in
   the line comes from a file which has been modified since the aux info
   file was created.  If so, return nonzero, else return zero.  */

static int
referenced_file_is_newer (const char *l, time_t aux_info_mtime)
{
  const char *p;
  file_info *fi_p;
  char *filename;

  check_aux_info (l[0] == '/');
  check_aux_info (l[1] == '*');
  check_aux_info (l[2] == ' ');

  {
    const char *filename_start = p = l + 3;

    while (*p != ':'
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
	   || (*p == ':' && *p && *(p+1) && IS_DIR_SEPARATOR (*(p+1)))
#endif
	   )
      p++;
    filename = alloca ((size_t) (p - filename_start) + 1);
    strncpy (filename, filename_start, (size_t) (p - filename_start));
    filename[p-filename_start] = '\0';
  }

  /* Call find_file to find the file_info record associated with the file
     which contained this particular def or dec item.  Note that this call
     may cause a new file_info record to be created if this is the first time
     that we have ever known about this particular file.  */

  fi_p = find_file (abspath (invocation_filename, filename), 0);

  return (fi_p->mtime > aux_info_mtime);
}

/* Given a line of info from the aux_info file, create a new
   def_dec_info record to remember all of the important information about
   a function definition or declaration.

   Link this record onto the list of such records for the particular file in
   which it occurred in proper (descending) line number order (for now).

   If there is an identical record already on the list for the file, throw
   this one away.  Doing so takes care of the (useless and troublesome)
   duplicates which are bound to crop up due to multiple inclusions of any
   given individual header file.

   Finally, link the new def_dec record onto the list of such records
   pertaining to this particular function name.  */

static void
save_def_or_dec (const char *l, int is_syscalls)
{
  const char *p;
  const char *semicolon_p;
  def_dec_info *def_dec_p = xmalloc (sizeof (def_dec_info));

#ifndef UNPROTOIZE
  def_dec_p->written = 0;
#endif /* !defined (UNPROTOIZE) */

  /* Start processing the line by picking off 5 pieces of information from
     the left hand end of the line.  These are filename, line number,
     new/old/implicit flag (new = ANSI prototype format), definition or
     declaration flag, and extern/static flag).  */

  check_aux_info (l[0] == '/');
  check_aux_info (l[1] == '*');
  check_aux_info (l[2] == ' ');

  {
    const char *filename_start = p = l + 3;
    char *filename;

    while (*p != ':'
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
	   || (*p == ':' && *p && *(p+1) && IS_DIR_SEPARATOR (*(p+1)))
#endif
	   )
      p++;
    filename = alloca ((size_t) (p - filename_start) + 1);
    strncpy (filename, filename_start, (size_t) (p - filename_start));
    filename[p-filename_start] = '\0';

    /* Call find_file to find the file_info record associated with the file
       which contained this particular def or dec item.  Note that this call
       may cause a new file_info record to be created if this is the first time
       that we have ever known about this particular file.

       Note that we started out by forcing all of the base source file names
       (i.e. the names of the aux_info files with the .X stripped off) into the
       filenames hash table, and we simultaneously setup file_info records for
       all of these base file names (even if they may be useless later).
       The file_info records for all of these "base" file names (properly)
       act as file_info records for the "original" (i.e. un-included) files
       which were submitted to gcc for compilation (when the -aux-info
       option was used).  */

    def_dec_p->file = find_file (abspath (invocation_filename, filename), is_syscalls);
  }

  {
    const char *line_number_start = ++p;
    char line_number[10];

    while (*p != ':'
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
	   || (*p == ':' && *p && *(p+1) && IS_DIR_SEPARATOR (*(p+1)))
#endif
	   )
      p++;
    strncpy (line_number, line_number_start, (size_t) (p - line_number_start));
    line_number[p-line_number_start] = '\0';
    def_dec_p->line = atoi (line_number);
  }

  /* Check that this record describes a new-style, old-style, or implicit
     definition or declaration.  */

  p++;	/* Skip over the `:'.  */
  check_aux_info ((*p == 'N') || (*p == 'O') || (*p == 'I'));

  /* Is this a new style (ANSI prototyped) definition or declaration? */

  def_dec_p->prototyped = (*p == 'N');

#ifndef UNPROTOIZE

  /* Is this an implicit declaration? */

  def_dec_p->is_implicit = (*p == 'I');

#endif /* !defined (UNPROTOIZE) */

  p++;

  check_aux_info ((*p == 'C') || (*p == 'F'));

  /* Is this item a function definition (F) or a declaration (C).  Note that
     we treat item taken from the syscalls file as though they were function
     definitions regardless of what the stuff in the file says.  */

  def_dec_p->is_func_def = ((*p++ == 'F') || is_syscalls);

#ifndef UNPROTOIZE
  def_dec_p->definition = 0;	/* Fill this in later if protoizing.  */
#endif /* !defined (UNPROTOIZE) */

  check_aux_info (*p++ == ' ');
  check_aux_info (*p++ == '*');
  check_aux_info (*p++ == '/');
  check_aux_info (*p++ == ' ');

#ifdef UNPROTOIZE
  check_aux_info ((!strncmp (p, "static", 6)) || (!strncmp (p, "extern", 6)));
#else /* !defined (UNPROTOIZE) */
  if (!strncmp (p, "static", 6))
    def_dec_p->is_static = -1;
  else if (!strncmp (p, "extern", 6))
    def_dec_p->is_static = 0;
  else
    check_aux_info (0);	/* Didn't find either `extern' or `static'.  */
#endif /* !defined (UNPROTOIZE) */

  {
    const char *ansi_start = p;

    p += 6;	/* Pass over the "static" or "extern".  */

    /* We are now past the initial stuff.  Search forward from here to find
       the terminating semicolon that should immediately follow the entire
       ANSI format function declaration.  */

    while (*++p != ';')
      continue;

    semicolon_p = p;

    /* Make a copy of the ansi declaration part of the line from the aux_info
       file.  */

    def_dec_p->ansi_decl
      = dupnstr (ansi_start, (size_t) ((semicolon_p+1) - ansi_start));

    /* Backup and point at the final right paren of the final argument list.  */

    p--;

#ifndef UNPROTOIZE
    def_dec_p->f_list_chain = NULL;
#endif /* !defined (UNPROTOIZE) */

    while (p != ansi_start && (p[-1] == ' ' || p[-1] == '\t')) p--;
    if (*p != ')')
      {
	free_def_dec (def_dec_p);
	return;
      }
  }

  /* Now isolate a whole set of formal argument lists, one-by-one.  Normally,
     there will only be one list to isolate, but there could be more.  */

  def_dec_p->f_list_count = 0;

  for (;;)
    {
      const char *left_paren_p = find_corresponding_lparen (p);
#ifndef UNPROTOIZE
      {
	f_list_chain_item *cip = xmalloc (sizeof (f_list_chain_item));

	cip->formals_list
	  = dupnstr (left_paren_p + 1, (size_t) (p - (left_paren_p+1)));

	/* Add the new chain item at the head of the current list.  */

	cip->chain_next = def_dec_p->f_list_chain;
	def_dec_p->f_list_chain = cip;
      }
#endif /* !defined (UNPROTOIZE) */
      def_dec_p->f_list_count++;

      p = left_paren_p - 2;

      /* p must now point either to another right paren, or to the last
	 character of the name of the function that was declared/defined.
	 If p points to another right paren, then this indicates that we
	 are dealing with multiple formals lists.  In that case, there
	 really should be another right paren preceding this right paren.  */

      if (*p != ')')
	break;
      else
	check_aux_info (*--p == ')');
    }


  {
    const char *past_fn = p + 1;

    check_aux_info (*past_fn == ' ');

    /* Scan leftwards over the identifier that names the function.  */

    while (is_id_char (*p))
      p--;
    p++;

    /* p now points to the leftmost character of the function name.  */

    {
      char *fn_string = alloca (past_fn - p + 1);

      strncpy (fn_string, p, (size_t) (past_fn - p));
      fn_string[past_fn-p] = '\0';
      def_dec_p->hash_entry = lookup (function_name_primary, fn_string);
    }
  }

  /* Look at all of the defs and decs for this function name that we have
     collected so far.  If there is already one which is at the same
     line number in the same file, then we can discard this new def_dec_info
     record.

     As an extra assurance that any such pair of (nominally) identical
     function declarations are in fact identical, we also compare the
     ansi_decl parts of the lines from the aux_info files just to be on
     the safe side.

     This comparison will fail if (for instance) the user was playing
     messy games with the preprocessor which ultimately causes one
     function declaration in one header file to look differently when
     that file is included by two (or more) other files.  */

  {
    const def_dec_info *other;

    for (other = def_dec_p->hash_entry->ddip; other; other = other->next_for_func)
      {
	if (def_dec_p->line == other->line && def_dec_p->file == other->file)
	  {
	    if (strcmp (def_dec_p->ansi_decl, other->ansi_decl))
	      {
	        notice ("%s:%d: declaration of function '%s' takes different forms\n",
			def_dec_p->file->hash_entry->symbol,
			def_dec_p->line,
			def_dec_p->hash_entry->symbol);
	        exit (FATAL_EXIT_CODE);
	      }
	    free_def_dec (def_dec_p);
	    return;
	  }
      }
  }

#ifdef UNPROTOIZE

  /* If we are doing unprotoizing, we must now setup the pointers that will
     point to the K&R name list and to the K&R argument declarations list.

     Note that if this is only a function declaration, then we should not
     expect to find any K&R style formals list following the ANSI-style
     formals list.  This is because GCC knows that such information is
     useless in the case of function declarations (function definitions
     are a different story however).

     Since we are unprotoizing, we don't need any such lists anyway.
     All we plan to do is to delete all characters between ()'s in any
     case.  */

  def_dec_p->formal_names = NULL;
  def_dec_p->formal_decls = NULL;

  if (def_dec_p->is_func_def)
    {
      p = semicolon_p;
      check_aux_info (*++p == ' ');
      check_aux_info (*++p == '/');
      check_aux_info (*++p == '*');
      check_aux_info (*++p == ' ');
      check_aux_info (*++p == '(');

      {
	const char *kr_names_start = ++p;   /* Point just inside '('.  */

	while (*p++ != ')')
	  continue;
	p--;		/* point to closing right paren */

	/* Make a copy of the K&R parameter names list.  */

	def_dec_p->formal_names
	  = dupnstr (kr_names_start, (size_t) (p - kr_names_start));
      }

      check_aux_info (*++p == ' ');
      p++;

      /* p now points to the first character of the K&R style declarations
	 list (if there is one) or to the star-slash combination that ends
	 the comment in which such lists get embedded.  */

      /* Make a copy of the K&R formal decls list and set the def_dec record
	 to point to it.  */

      if (*p == '*')		/* Are there no K&R declarations? */
	{
	  check_aux_info (*++p == '/');
	  def_dec_p->formal_decls = "";
	}
      else
	{
	  const char *kr_decls_start = p;

	  while (p[0] != '*' || p[1] != '/')
	    p++;
	  p--;

	  check_aux_info (*p == ' ');

	  def_dec_p->formal_decls
	    = dupnstr (kr_decls_start, (size_t) (p - kr_decls_start));
	}

      /* Handle a special case.  If we have a function definition marked as
	 being in "old" style, and if its formal names list is empty, then
	 it may actually have the string "void" in its real formals list
	 in the original source code.  Just to make sure, we will get setup
	 to convert such things anyway.

	 This kludge only needs to be here because of an insurmountable
	 problem with generating .X files.  */

      if (!def_dec_p->prototyped && !*def_dec_p->formal_names)
	def_dec_p->prototyped = 1;
    }

  /* Since we are unprotoizing, if this item is already in old (K&R) style,
     we can just ignore it.  If that is true, throw away the itme now.  */

  if (!def_dec_p->prototyped)
    {
      free_def_dec (def_dec_p);
      return;
    }

#endif /* defined (UNPROTOIZE) */

  /* Add this record to the head of the list of records pertaining to this
     particular function name.  */

  def_dec_p->next_for_func = def_dec_p->hash_entry->ddip;
  def_dec_p->hash_entry->ddip = def_dec_p;

  /* Add this new def_dec_info record to the sorted list of def_dec_info
     records for this file.  Note that we don't have to worry about duplicates
     (caused by multiple inclusions of header files) here because we have
     already eliminated duplicates above.  */

  if (!def_dec_p->file->defs_decs)
    {
      def_dec_p->file->defs_decs = def_dec_p;
      def_dec_p->next_in_file = NULL;
    }
  else
    {
      int line = def_dec_p->line;
      const def_dec_info *prev = NULL;
      const def_dec_info *curr = def_dec_p->file->defs_decs;
      const def_dec_info *next = curr->next_in_file;

      while (next && (line < curr->line))
	{
	  prev = curr;
	  curr = next;
	  next = next->next_in_file;
	}
      if (line >= curr->line)
	{
	  def_dec_p->next_in_file = curr;
	  if (prev)
	    ((NONCONST def_dec_info *) prev)->next_in_file = def_dec_p;
	  else
	    def_dec_p->file->defs_decs = def_dec_p;
	}
      else	/* assert (next == NULL); */
	{
	  ((NONCONST def_dec_info *) curr)->next_in_file = def_dec_p;
	  /* assert (next == NULL); */
	  def_dec_p->next_in_file = next;
	}
    }
}

/* Set up the vector COMPILE_PARAMS which is the argument list for running GCC.
   Also set input_file_name_index and aux_info_file_name_index
   to the indices of the slots where the file names should go.  */

/* We initialize the vector by  removing -g, -O, -S, -c, and -o options,
   and adding '-aux-info AUXFILE -S  -o /dev/null INFILE' at the end.  */

static void
munge_compile_params (const char *params_list)
{
  /* Build up the contents in a temporary vector
     that is so big that to has to be big enough.  */
  const char **temp_params
    = alloca ((strlen (params_list) + 8) * sizeof (char *));
  int param_count = 0;
  const char *param;
  struct stat st;

  temp_params[param_count++] = compiler_file_name;
  for (;;)
    {
      while (ISSPACE ((const unsigned char)*params_list))
	params_list++;
      if (!*params_list)
	break;
      param = params_list;
      while (*params_list && !ISSPACE ((const unsigned char)*params_list))
	params_list++;
      if (param[0] != '-')
	temp_params[param_count++]
	  = dupnstr (param, (size_t) (params_list - param));
      else
	{
	  switch (param[1])
	    {
	    case 'g':
	    case 'O':
	    case 'S':
	    case 'c':
	      break;		/* Don't copy these.  */
	    case 'o':
	      while (ISSPACE ((const unsigned char)*params_list))
		params_list++;
	      while (*params_list
		     && !ISSPACE ((const unsigned char)*params_list))
		params_list++;
	      break;
	    default:
	      temp_params[param_count++]
		= dupnstr (param, (size_t) (params_list - param));
	    }
	}
      if (!*params_list)
	break;
    }
  temp_params[param_count++] = "-aux-info";

  /* Leave room for the aux-info file name argument.  */
  aux_info_file_name_index = param_count;
  temp_params[param_count++] = NULL;

  temp_params[param_count++] = "-S";
  temp_params[param_count++] = "-o";

  if ((stat (HOST_BIT_BUCKET, &st) == 0)
      && (!S_ISDIR (st.st_mode))
      && (access (HOST_BIT_BUCKET, W_OK) == 0))
    temp_params[param_count++] = HOST_BIT_BUCKET;
  else
    /* FIXME: This is hardly likely to be right, if HOST_BIT_BUCKET is not
       writable.  But until this is rejigged to use make_temp_file(), this
       is the best we can do.  */
    temp_params[param_count++] = "/dev/null";

  /* Leave room for the input file name argument.  */
  input_file_name_index = param_count;
  temp_params[param_count++] = NULL;
  /* Terminate the list.  */
  temp_params[param_count++] = NULL;

  /* Make a copy of the compile_params in heap space.  */

  compile_params = xmalloc (sizeof (char *) * (param_count+1));
  memcpy (compile_params, temp_params, sizeof (char *) * param_count);
}

/* Do a recompilation for the express purpose of generating a new aux_info
   file to go with a specific base source file.

   The result is a boolean indicating success.  */

static int
gen_aux_info_file (const char *base_filename)
{
  if (!input_file_name_index)
    munge_compile_params ("");

  /* Store the full source file name in the argument vector.  */
  compile_params[input_file_name_index] = shortpath (NULL, base_filename);
  /* Add .X to source file name to get aux-info file name.  */
  compile_params[aux_info_file_name_index] =
    concat (compile_params[input_file_name_index], aux_info_suffix, NULL);

  if (!quiet_flag)
    notice ("%s: compiling '%s'\n",
	    pname, compile_params[input_file_name_index]);

  {
    char *errmsg_fmt, *errmsg_arg;
    int wait_status, pid;

    pid = pexecute (compile_params[0], (char * const *) compile_params,
		    pname, NULL, &errmsg_fmt, &errmsg_arg,
		    PEXECUTE_FIRST | PEXECUTE_LAST | PEXECUTE_SEARCH);

    if (pid == -1)
      {
	int errno_val = errno;
	fprintf (stderr, "%s: ", pname);
	fprintf (stderr, errmsg_fmt, errmsg_arg);
	fprintf (stderr, ": %s\n", xstrerror (errno_val));
	return 0;
      }

    pid = pwait (pid, &wait_status, 0);
    if (pid == -1)
      {
	notice ("%s: wait: %s\n", pname, xstrerror (errno));
	return 0;
      }
    if (WIFSIGNALED (wait_status))
      {
	notice ("%s: subprocess got fatal signal %d\n",
		pname, WTERMSIG (wait_status));
	return 0;
      }
    if (WIFEXITED (wait_status))
      {
	if (WEXITSTATUS (wait_status) != 0)
	  {
	    notice ("%s: %s exited with status %d\n",
		    pname, compile_params[0], WEXITSTATUS (wait_status));
	    return 0;
	  }
	return 1;
      }
    gcc_unreachable ();
  }
}

/* Read in all of the information contained in a single aux_info file.
   Save all of the important stuff for later.  */

static void
process_aux_info_file (const char *base_source_filename, int keep_it,
		       int is_syscalls)
{
  size_t base_len = strlen (base_source_filename);
  char * aux_info_filename = alloca (base_len + strlen (aux_info_suffix) + 1);
  char *aux_info_base;
  char *aux_info_limit;
  char *aux_info_relocated_name;
  const char *aux_info_second_line;
  time_t aux_info_mtime;
  size_t aux_info_size;
  int must_create;

  /* Construct the aux_info filename from the base source filename.  */

  strcpy (aux_info_filename, base_source_filename);
  strcat (aux_info_filename, aux_info_suffix);

  /* Check that the aux_info file exists and is readable.  If it does not
     exist, try to create it (once only).  */

  /* If file doesn't exist, set must_create.
     Likewise if it exists and we can read it but it is obsolete.
     Otherwise, report an error.  */
  must_create = 0;

  /* Come here with must_create set to 1 if file is out of date.  */
start_over: ;

  if (access (aux_info_filename, R_OK) == -1)
    {
      if (errno == ENOENT)
	{
	  if (is_syscalls)
	    {
	      notice ("%s: warning: missing SYSCALLS file '%s'\n",
		      pname, aux_info_filename);
	      return;
	    }
	  must_create = 1;
	}
      else
	{
	  int errno_val = errno;
	  notice ("%s: can't read aux info file '%s': %s\n",
		  pname, shortpath (NULL, aux_info_filename),
		  xstrerror (errno_val));
	  errors++;
	  return;
	}
    }
#if 0 /* There is code farther down to take care of this.  */
  else
    {
      struct stat s1, s2;
      stat (aux_info_file_name, &s1);
      stat (base_source_file_name, &s2);
      if (s2.st_mtime > s1.st_mtime)
	must_create = 1;
    }
#endif /* 0 */

  /* If we need a .X file, create it, and verify we can read it.  */
  if (must_create)
    {
      if (!gen_aux_info_file (base_source_filename))
	{
	  errors++;
	  return;
	}
      if (access (aux_info_filename, R_OK) == -1)
	{
	  int errno_val = errno;
	  notice ("%s: can't read aux info file '%s': %s\n",
		  pname, shortpath (NULL, aux_info_filename),
		  xstrerror (errno_val));
	  errors++;
	  return;
	}
    }

  {
    struct stat stat_buf;

    /* Get some status information about this aux_info file.  */

    if (stat (aux_info_filename, &stat_buf) == -1)
      {
	int errno_val = errno;
	notice ("%s: can't get status of aux info file '%s': %s\n",
		pname, shortpath (NULL, aux_info_filename),
		xstrerror (errno_val));
	errors++;
	return;
      }

    /* Check on whether or not this aux_info file is zero length.  If it is,
       then just ignore it and return.  */

    if ((aux_info_size = stat_buf.st_size) == 0)
      return;

    /* Get the date/time of last modification for this aux_info file and
       remember it.  We will have to check that any source files that it
       contains information about are at least this old or older.  */

    aux_info_mtime = stat_buf.st_mtime;

    if (!is_syscalls)
      {
	/* Compare mod time with the .c file; update .X file if obsolete.
	   The code later on can fail to check the .c file
	   if it did not directly define any functions.  */

	if (stat (base_source_filename, &stat_buf) == -1)
	  {
	    int errno_val = errno;
	    notice ("%s: can't get status of aux info file '%s': %s\n",
		    pname, shortpath (NULL, base_source_filename),
		    xstrerror (errno_val));
	    errors++;
	    return;
	  }
	if (stat_buf.st_mtime > aux_info_mtime)
	  {
	    must_create = 1;
	    goto start_over;
	  }
      }
  }

  {
    int aux_info_file;
    int fd_flags;

    /* Open the aux_info file.  */

    fd_flags = O_RDONLY;
#ifdef O_BINARY
    /* Use binary mode to avoid having to deal with different EOL characters.  */
    fd_flags |= O_BINARY;
#endif
    if ((aux_info_file = open (aux_info_filename, fd_flags, 0444 )) == -1)
      {
	int errno_val = errno;
	notice ("%s: can't open aux info file '%s' for reading: %s\n",
		pname, shortpath (NULL, aux_info_filename),
		xstrerror (errno_val));
	return;
      }

    /* Allocate space to hold the aux_info file in memory.  */

    aux_info_base = xmalloc (aux_info_size + 1);
    aux_info_limit = aux_info_base + aux_info_size;
    *aux_info_limit = '\0';

    /* Read the aux_info file into memory.  */

    if (safe_read (aux_info_file, aux_info_base, aux_info_size) !=
	(int) aux_info_size)
      {
	int errno_val = errno;
	notice ("%s: error reading aux info file '%s': %s\n",
		pname, shortpath (NULL, aux_info_filename),
		xstrerror (errno_val));
	free (aux_info_base);
	close (aux_info_file);
	return;
      }

    /* Close the aux info file.  */

    if (close (aux_info_file))
      {
	int errno_val = errno;
	notice ("%s: error closing aux info file '%s': %s\n",
		pname, shortpath (NULL, aux_info_filename),
		xstrerror (errno_val));
	free (aux_info_base);
	close (aux_info_file);
	return;
      }
  }

  /* Delete the aux_info file (unless requested not to).  If the deletion
     fails for some reason, don't even worry about it.  */

  if (must_create && !keep_it)
    if (unlink (aux_info_filename) == -1)
      {
	int errno_val = errno;
	notice ("%s: can't delete aux info file '%s': %s\n",
		pname, shortpath (NULL, aux_info_filename),
		xstrerror (errno_val));
      }

  /* Save a pointer into the first line of the aux_info file which
     contains the filename of the directory from which the compiler
     was invoked when the associated source file was compiled.
     This information is used later to help create complete
     filenames out of the (potentially) relative filenames in
     the aux_info file.  */

  {
    char *p = aux_info_base;

    while (*p != ':'
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
	   || (*p == ':' && *p && *(p+1) && IS_DIR_SEPARATOR (*(p+1)))
#endif
	   )
      p++;
    p++;
    while (*p == ' ')
      p++;
    invocation_filename = p;	/* Save a pointer to first byte of path.  */
    while (*p != ' ')
      p++;
    *p++ = DIR_SEPARATOR;
    *p++ = '\0';
    while (*p++ != '\n')
      continue;
    aux_info_second_line = p;
    aux_info_relocated_name = 0;
    if (! IS_ABSOLUTE_PATH (invocation_filename))
      {
	/* INVOCATION_FILENAME is relative;
	   append it to BASE_SOURCE_FILENAME's dir.  */
	char *dir_end;
	aux_info_relocated_name = xmalloc (base_len + (p-invocation_filename));
	strcpy (aux_info_relocated_name, base_source_filename);
	dir_end = strrchr (aux_info_relocated_name, DIR_SEPARATOR);
#ifdef DIR_SEPARATOR_2
	{
	  char *slash;

	  slash = strrchr (dir_end ? dir_end : aux_info_relocated_name,
			   DIR_SEPARATOR_2);
	  if (slash)
	    dir_end = slash;
	}
#endif
	if (dir_end)
	  dir_end++;
	else
	  dir_end = aux_info_relocated_name;
	strcpy (dir_end, invocation_filename);
	invocation_filename = aux_info_relocated_name;
      }
  }


  {
    const char *aux_info_p;

    /* Do a pre-pass on the lines in the aux_info file, making sure that all
       of the source files referenced in there are at least as old as this
       aux_info file itself.  If not, go back and regenerate the aux_info
       file anew.  Don't do any of this for the syscalls file.  */

    if (!is_syscalls)
      {
	current_aux_info_lineno = 2;

	for (aux_info_p = aux_info_second_line; *aux_info_p; )
	  {
	    if (referenced_file_is_newer (aux_info_p, aux_info_mtime))
	      {
		free (aux_info_base);
		free (aux_info_relocated_name);
		if (keep_it && unlink (aux_info_filename) == -1)
		  {
		    int errno_val = errno;
	            notice ("%s: can't delete file '%s': %s\n",
			    pname, shortpath (NULL, aux_info_filename),
			    xstrerror (errno_val));
	            return;
	          }
		must_create = 1;
	        goto start_over;
	      }

	    /* Skip over the rest of this line to start of next line.  */

	    while (*aux_info_p != '\n')
	      aux_info_p++;
	    aux_info_p++;
	    current_aux_info_lineno++;
	  }
      }

    /* Now do the real pass on the aux_info lines.  Save their information in
       the in-core data base.  */

    current_aux_info_lineno = 2;

    for (aux_info_p = aux_info_second_line; *aux_info_p;)
      {
	char *unexpanded_line = unexpand_if_needed (aux_info_p);

	if (unexpanded_line)
	  {
	    save_def_or_dec (unexpanded_line, is_syscalls);
	    free (unexpanded_line);
	  }
	else
	  save_def_or_dec (aux_info_p, is_syscalls);

	/* Skip over the rest of this line and get to start of next line.  */

	while (*aux_info_p != '\n')
	  aux_info_p++;
	aux_info_p++;
	current_aux_info_lineno++;
      }
  }

  free (aux_info_base);
  free (aux_info_relocated_name);
}

#ifndef UNPROTOIZE

/* Check an individual filename for a .c suffix.  If the filename has this
   suffix, rename the file such that its suffix is changed to .C.  This
   function implements the -C option.  */

static void
rename_c_file (const hash_table_entry *hp)
{
  const char *filename = hp->symbol;
  int last_char_index = strlen (filename) - 1;
  char *const new_filename = alloca (strlen (filename)
				     + strlen (cplus_suffix) + 1);

  /* Note that we don't care here if the given file was converted or not.  It
     is possible that the given file was *not* converted, simply because there
     was nothing in it which actually required conversion.  Even in this case,
     we want to do the renaming.  Note that we only rename files with the .c
     suffix (except for the syscalls file, which is left alone).  */

  if (filename[last_char_index] != 'c' || filename[last_char_index-1] != '.'
      || IS_SAME_PATH (syscalls_absolute_filename, filename))
    return;

  strcpy (new_filename, filename);
  strcpy (&new_filename[last_char_index], cplus_suffix);

  if (rename (filename, new_filename) == -1)
    {
      int errno_val = errno;
      notice ("%s: warning: can't rename file '%s' to '%s': %s\n",
	      pname, shortpath (NULL, filename),
	      shortpath (NULL, new_filename), xstrerror (errno_val));
      errors++;
      return;
    }
}

#endif /* !defined (UNPROTOIZE) */

/* Take the list of definitions and declarations attached to a particular
   file_info node and reverse the order of the list.  This should get the
   list into an order such that the item with the lowest associated line
   number is nearest the head of the list.  When these lists are originally
   built, they are in the opposite order.  We want to traverse them in
   normal line number order later (i.e. lowest to highest) so reverse the
   order here.  */

static void
reverse_def_dec_list (const hash_table_entry *hp)
{
  file_info *file_p = hp->fip;
  def_dec_info *prev = NULL;
  def_dec_info *current = (def_dec_info *) file_p->defs_decs;

  if (!current)
    return;        		/* no list to reverse */

  prev = current;
  if (! (current = (def_dec_info *) current->next_in_file))
    return;        		/* can't reverse a single list element */

  prev->next_in_file = NULL;

  while (current)
    {
      def_dec_info *next = (def_dec_info *) current->next_in_file;

      current->next_in_file = prev;
      prev = current;
      current = next;
    }

  file_p->defs_decs = prev;
}

#ifndef UNPROTOIZE

/* Find the (only?) extern definition for a particular function name, starting
   from the head of the linked list of entries for the given name.  If we
   cannot find an extern definition for the given function name, issue a
   warning and scrounge around for the next best thing, i.e. an extern
   function declaration with a prototype attached to it.  Note that we only
   allow such substitutions for extern declarations and never for static
   declarations.  That's because the only reason we allow them at all is
   to let un-prototyped function declarations for system-supplied library
   functions get their prototypes from our own extra SYSCALLS.c.X file which
   contains all of the correct prototypes for system functions.  */

static const def_dec_info *
find_extern_def (const def_dec_info *head, const def_dec_info *user)
{
  const def_dec_info *dd_p;
  const def_dec_info *extern_def_p = NULL;
  int conflict_noted = 0;

  /* Don't act too stupid here.  Somebody may try to convert an entire system
     in one swell fwoop (rather than one program at a time, as should be done)
     and in that case, we may find that there are multiple extern definitions
     of a given function name in the entire set of source files that we are
     converting.  If however one of these definitions resides in exactly the
     same source file as the reference we are trying to satisfy then in that
     case it would be stupid for us to fail to realize that this one definition
     *must* be the precise one we are looking for.

     To make sure that we don't miss an opportunity to make this "same file"
     leap of faith, we do a prescan of the list of records relating to the
     given function name, and we look (on this first scan) *only* for a
     definition of the function which is in the same file as the reference
     we are currently trying to satisfy.  */

  for (dd_p = head; dd_p; dd_p = dd_p->next_for_func)
    if (dd_p->is_func_def && !dd_p->is_static && dd_p->file == user->file)
      return dd_p;

  /* Now, since we have not found a definition in the same file as the
     reference, we scan the list again and consider all possibilities from
     all files.  Here we may get conflicts with the things listed in the
     SYSCALLS.c.X file, but if that happens it only means that the source
     code being converted contains its own definition of a function which
     could have been supplied by libc.a.  In such cases, we should avoid
     issuing the normal warning, and defer to the definition given in the
     user's own code.  */

  for (dd_p = head; dd_p; dd_p = dd_p->next_for_func)
    if (dd_p->is_func_def && !dd_p->is_static)
      {
	if (!extern_def_p)	/* Previous definition? */
	  extern_def_p = dd_p;	/* Remember the first definition found.  */
	else
	  {
	    /* Ignore definition just found if it came from SYSCALLS.c.X.  */

	    if (is_syscalls_file (dd_p->file))
	      continue;

	    /* Quietly replace the definition previously found with the one
	       just found if the previous one was from SYSCALLS.c.X.  */

	    if (is_syscalls_file (extern_def_p->file))
	      {
	        extern_def_p = dd_p;
	        continue;
	      }

	    /* If we get here, then there is a conflict between two function
	       declarations for the same function, both of which came from the
	       user's own code.  */

	    if (!conflict_noted)	/* first time we noticed? */
	      {
		conflict_noted = 1;
		notice ("%s: conflicting extern definitions of '%s'\n",
			pname, head->hash_entry->symbol);
		if (!quiet_flag)
		  {
		    notice ("%s: declarations of '%s' will not be converted\n",
			    pname, head->hash_entry->symbol);
		    notice ("%s: conflict list for '%s' follows:\n",
			    pname, head->hash_entry->symbol);
		    fprintf (stderr, "%s:     %s(%d): %s\n",
			     pname,
			     shortpath (NULL, extern_def_p->file->hash_entry->symbol),
			     extern_def_p->line, extern_def_p->ansi_decl);
		  }
	      }
	    if (!quiet_flag)
	      fprintf (stderr, "%s:     %s(%d): %s\n",
		       pname,
		       shortpath (NULL, dd_p->file->hash_entry->symbol),
		       dd_p->line, dd_p->ansi_decl);
	  }
      }

  /* We want to err on the side of caution, so if we found multiple conflicting
     definitions for the same function, treat this as being that same as if we
     had found no definitions (i.e. return NULL).  */

  if (conflict_noted)
    return NULL;

  if (!extern_def_p)
    {
      /* We have no definitions for this function so do the next best thing.
	 Search for an extern declaration already in prototype form.  */

      for (dd_p = head; dd_p; dd_p = dd_p->next_for_func)
	if (!dd_p->is_func_def && !dd_p->is_static && dd_p->prototyped)
	  {
	    extern_def_p = dd_p;	/* save a pointer to the definition */
	    if (!quiet_flag)
	      notice ("%s: warning: using formals list from %s(%d) for function '%s'\n",
		      pname,
		      shortpath (NULL, dd_p->file->hash_entry->symbol),
		      dd_p->line, dd_p->hash_entry->symbol);
	    break;
	  }

      /* Gripe about unprototyped function declarations that we found no
	 corresponding definition (or other source of prototype information)
	 for.

	 Gripe even if the unprototyped declaration we are worried about
	 exists in a file in one of the "system" include directories.  We
	 can gripe about these because we should have at least found a
	 corresponding (pseudo) definition in the SYSCALLS.c.X file.  If we
	 didn't, then that means that the SYSCALLS.c.X file is missing some
	 needed prototypes for this particular system.  That is worth telling
	 the user about!  */

      if (!extern_def_p)
	{
	  const char *file = user->file->hash_entry->symbol;

	  if (!quiet_flag)
	    if (in_system_include_dir (file))
	      {
		/* Why copy this string into `needed' at all?
		   Why not just use user->ansi_decl without copying?  */
		char *needed = alloca (strlen (user->ansi_decl) + 1);
	        char *p;

	        strcpy (needed, user->ansi_decl);
	        p = strstr (needed, user->hash_entry->symbol)
	            + strlen (user->hash_entry->symbol) + 2;
		/* Avoid having ??? in the string.  */
		*p++ = '?';
		*p++ = '?';
		*p++ = '?';
	        strcpy (p, ");");

	        notice ("%s: %d: '%s' used but missing from SYSCALLS\n",
			shortpath (NULL, file), user->line,
			needed+7);	/* Don't print "extern " */
	      }
#if 0
	    else
	      notice ("%s: %d: warning: no extern definition for '%s'\n",
		      shortpath (NULL, file), user->line,
		      user->hash_entry->symbol);
#endif
	}
    }
  return extern_def_p;
}

/* Find the (only?) static definition for a particular function name in a
   given file.  Here we get the function-name and the file info indirectly
   from the def_dec_info record pointer which is passed in.  */

static const def_dec_info *
find_static_definition (const def_dec_info *user)
{
  const def_dec_info *head = user->hash_entry->ddip;
  const def_dec_info *dd_p;
  int num_static_defs = 0;
  const def_dec_info *static_def_p = NULL;

  for (dd_p = head; dd_p; dd_p = dd_p->next_for_func)
    if (dd_p->is_func_def && dd_p->is_static && (dd_p->file == user->file))
      {
	static_def_p = dd_p;	/* save a pointer to the definition */
	num_static_defs++;
      }
  if (num_static_defs == 0)
    {
      if (!quiet_flag)
	notice ("%s: warning: no static definition for '%s' in file '%s'\n",
		pname, head->hash_entry->symbol,
		shortpath (NULL, user->file->hash_entry->symbol));
    }
  else if (num_static_defs > 1)
    {
      notice ("%s: multiple static defs of '%s' in file '%s'\n",
	      pname, head->hash_entry->symbol,
	      shortpath (NULL, user->file->hash_entry->symbol));
      return NULL;
    }
  return static_def_p;
}

/* Find good prototype style formal argument lists for all of the function
   declarations which didn't have them before now.

   To do this we consider each function name one at a time.  For each function
   name, we look at the items on the linked list of def_dec_info records for
   that particular name.

   Somewhere on this list we should find one (and only one) def_dec_info
   record which represents the actual function definition, and this record
   should have a nice formal argument list already associated with it.

   Thus, all we have to do is to connect up all of the other def_dec_info
   records for this particular function name to the special one which has
   the full-blown formals list.

   Of course it is a little more complicated than just that.  See below for
   more details.  */

static void
connect_defs_and_decs (const hash_table_entry *hp)
{
  const def_dec_info *dd_p;
  const def_dec_info *extern_def_p = NULL;
  int first_extern_reference = 1;

  /* Traverse the list of definitions and declarations for this particular
     function name.  For each item on the list, if it is a function
     definition (either old style or new style) then GCC has already been
     kind enough to produce a prototype for us, and it is associated with
     the item already, so declare the item as its own associated "definition".

     Also, for each item which is only a function declaration, but which
     nonetheless has its own prototype already (obviously supplied by the user)
     declare the item as its own definition.

     Note that when/if there are multiple user-supplied prototypes already
     present for multiple declarations of any given function, these multiple
     prototypes *should* all match exactly with one another and with the
     prototype for the actual function definition.  We don't check for this
     here however, since we assume that the compiler must have already done
     this consistency checking when it was creating the .X files.  */

  for (dd_p = hp->ddip; dd_p; dd_p = dd_p->next_for_func)
    if (dd_p->prototyped)
      ((NONCONST def_dec_info *) dd_p)->definition = dd_p;

  /* Traverse the list of definitions and declarations for this particular
     function name.  For each item on the list, if it is an extern function
     declaration and if it has no associated definition yet, go try to find
     the matching extern definition for the declaration.

     When looking for the matching function definition, warn the user if we
     fail to find one.

     If we find more that one function definition also issue a warning.

     Do the search for the matching definition only once per unique function
     name (and only when absolutely needed) so that we can avoid putting out
     redundant warning messages, and so that we will only put out warning
     messages when there is actually a reference (i.e. a declaration) for
     which we need to find a matching definition.  */

  for (dd_p = hp->ddip; dd_p; dd_p = dd_p->next_for_func)
    if (!dd_p->is_func_def && !dd_p->is_static && !dd_p->definition)
      {
	if (first_extern_reference)
	  {
	    extern_def_p = find_extern_def (hp->ddip, dd_p);
	    first_extern_reference = 0;
	  }
	((NONCONST def_dec_info *) dd_p)->definition = extern_def_p;
      }

  /* Traverse the list of definitions and declarations for this particular
     function name.  For each item on the list, if it is a static function
     declaration and if it has no associated definition yet, go try to find
     the matching static definition for the declaration within the same file.

     When looking for the matching function definition, warn the user if we
     fail to find one in the same file with the declaration, and refuse to
     convert this kind of cross-file static function declaration.  After all,
     this is stupid practice and should be discouraged.

     We don't have to worry about the possibility that there is more than one
     matching function definition in the given file because that would have
     been flagged as an error by the compiler.

     Do the search for the matching definition only once per unique
     function-name/source-file pair (and only when absolutely needed) so that
     we can avoid putting out redundant warning messages, and so that we will
     only put out warning messages when there is actually a reference (i.e. a
     declaration) for which we actually need to find a matching definition.  */

  for (dd_p = hp->ddip; dd_p; dd_p = dd_p->next_for_func)
    if (!dd_p->is_func_def && dd_p->is_static && !dd_p->definition)
      {
	const def_dec_info *dd_p2;
	const def_dec_info *static_def;

	/* We have now found a single static declaration for which we need to
	   find a matching definition.  We want to minimize the work (and the
	   number of warnings), so we will find an appropriate (matching)
	   static definition for this declaration, and then distribute it
	   (as the definition for) any and all other static declarations
	   for this function name which occur within the same file, and which
	   do not already have definitions.

	   Note that a trick is used here to prevent subsequent attempts to
	   call find_static_definition for a given function-name & file
	   if the first such call returns NULL.  Essentially, we convert
	   these NULL return values to -1, and put the -1 into the definition
	   field for each other static declaration from the same file which
	   does not already have an associated definition.
	   This makes these other static declarations look like they are
	   actually defined already when the outer loop here revisits them
	   later on.  Thus, the outer loop will skip over them.  Later, we
	   turn the -1's back to NULL's.  */

	((NONCONST def_dec_info *) dd_p)->definition =
	  (static_def = find_static_definition (dd_p))
	  ? static_def
	  : (const def_dec_info *) -1;

	for (dd_p2 = dd_p->next_for_func; dd_p2; dd_p2 = dd_p2->next_for_func)
	  if (!dd_p2->is_func_def && dd_p2->is_static
	      && !dd_p2->definition && (dd_p2->file == dd_p->file))
	    ((NONCONST def_dec_info *) dd_p2)->definition = dd_p->definition;
      }

  /* Convert any dummy (-1) definitions we created in the step above back to
     NULL's (as they should be).  */

  for (dd_p = hp->ddip; dd_p; dd_p = dd_p->next_for_func)
    if (dd_p->definition == (def_dec_info *) -1)
      ((NONCONST def_dec_info *) dd_p)->definition = NULL;
}

#endif /* !defined (UNPROTOIZE) */

/* Give a pointer into the clean text buffer, return a number which is the
   original source line number that the given pointer points into.  */

static int
identify_lineno (const char *clean_p)
{
  int line_num = 1;
  const char *scan_p;

  for (scan_p = clean_text_base; scan_p <= clean_p; scan_p++)
    if (*scan_p == '\n')
      line_num++;
  return line_num;
}

/* Issue an error message and give up on doing this particular edit.  */

static void
declare_source_confusing (const char *clean_p)
{
  if (!quiet_flag)
    {
      if (clean_p == 0)
	notice ("%s: %d: warning: source too confusing\n",
		shortpath (NULL, convert_filename), last_known_line_number);
      else
	notice ("%s: %d: warning: source too confusing\n",
		shortpath (NULL, convert_filename),
		identify_lineno (clean_p));
    }
  longjmp (source_confusion_recovery, 1);
}

/* Check that a condition which is expected to be true in the original source
   code is in fact true.  If not, issue an error message and give up on
   converting this particular source file.  */

static void
check_source (int cond, const char *clean_p)
{
  if (!cond)
    declare_source_confusing (clean_p);
}

/* If we think of the in-core cleaned text buffer as a memory mapped
   file (with the variable last_known_line_start acting as sort of a
   file pointer) then we can imagine doing "seeks" on the buffer.  The
   following routine implements a kind of "seek" operation for the in-core
   (cleaned) copy of the source file.  When finished, it returns a pointer to
   the start of a given (numbered) line in the cleaned text buffer.

   Note that protoize only has to "seek" in the forward direction on the
   in-core cleaned text file buffers, and it never needs to back up.

   This routine is made a little bit faster by remembering the line number
   (and pointer value) supplied (and returned) from the previous "seek".
   This prevents us from always having to start all over back at the top
   of the in-core cleaned buffer again.  */

static const char *
seek_to_line (int n)
{
  gcc_assert (n >= last_known_line_number);

  while (n > last_known_line_number)
    {
      while (*last_known_line_start != '\n')
	check_source (++last_known_line_start < clean_text_limit, 0);
      last_known_line_start++;
      last_known_line_number++;
    }
  return last_known_line_start;
}

/* Given a pointer to a character in the cleaned text buffer, return a pointer
   to the next non-whitespace character which follows it.  */

static const char *
forward_to_next_token_char (const char *ptr)
{
  for (++ptr; ISSPACE ((const unsigned char)*ptr);
       check_source (++ptr < clean_text_limit, 0))
    continue;
  return ptr;
}

/* Copy a chunk of text of length `len' and starting at `str' to the current
   output buffer.  Note that all attempts to add stuff to the current output
   buffer ultimately go through here.  */

static void
output_bytes (const char *str, size_t len)
{
  if ((repl_write_ptr + 1) + len >= repl_text_limit)
    {
      size_t new_size = (repl_text_limit - repl_text_base) << 1;
      char *new_buf = xrealloc (repl_text_base, new_size);

      repl_write_ptr = new_buf + (repl_write_ptr - repl_text_base);
      repl_text_base = new_buf;
      repl_text_limit = new_buf + new_size;
    }
  memcpy (repl_write_ptr + 1, str, len);
  repl_write_ptr += len;
}

/* Copy all bytes (except the trailing null) of a null terminated string to
   the current output buffer.  */

static void
output_string (const char *str)
{
  output_bytes (str, strlen (str));
}

/* Copy some characters from the original text buffer to the current output
   buffer.

   This routine takes a pointer argument `p' which is assumed to be a pointer
   into the cleaned text buffer.  The bytes which are copied are the `original'
   equivalents for the set of bytes between the last value of `clean_read_ptr'
   and the argument value `p'.

   The set of bytes copied however, comes *not* from the cleaned text buffer,
   but rather from the direct counterparts of these bytes within the original
   text buffer.

   Thus, when this function is called, some bytes from the original text
   buffer (which may include original comments and preprocessing directives)
   will be copied into the  output buffer.

   Note that the request implied when this routine is called includes the
   byte pointed to by the argument pointer `p'.  */

static void
output_up_to (const char *p)
{
  size_t copy_length = (size_t) (p - clean_read_ptr);
  const char *copy_start = orig_text_base+(clean_read_ptr-clean_text_base)+1;

  if (copy_length == 0)
    return;

  output_bytes (copy_start, copy_length);
  clean_read_ptr = p;
}

/* Given a pointer to a def_dec_info record which represents some form of
   definition of a function (perhaps a real definition, or in lieu of that
   perhaps just a declaration with a full prototype) return true if this
   function is one which we should avoid converting.  Return false
   otherwise.  */

static int
other_variable_style_function (const char *ansi_header)
{
#ifdef UNPROTOIZE

  /* See if we have a stdarg function, or a function which has stdarg style
     parameters or a stdarg style return type.  */

  return strstr (ansi_header, "...") != 0;

#else /* !defined (UNPROTOIZE) */

  /* See if we have a varargs function, or a function which has varargs style
     parameters or a varargs style return type.  */

  const char *p;
  int len = strlen (varargs_style_indicator);

  for (p = ansi_header; p; )
    {
      const char *candidate;

      if ((candidate = strstr (p, varargs_style_indicator)) == 0)
	return 0;
      else
	if (!is_id_char (candidate[-1]) && !is_id_char (candidate[len]))
	  return 1;
	else
	  p = candidate + 1;
    }
  return 0;
#endif /* !defined (UNPROTOIZE) */
}

/* Do the editing operation specifically for a function "declaration".  Note
   that editing for function "definitions" are handled in a separate routine
   below.  */

static void
edit_fn_declaration (const def_dec_info *def_dec_p,
		     const char *volatile clean_text_p)
{
  const char *start_formals;
  const char *end_formals;
  const char *function_to_edit = def_dec_p->hash_entry->symbol;
  size_t func_name_len = strlen (function_to_edit);
  const char *end_of_fn_name;

#ifndef UNPROTOIZE

  const f_list_chain_item *this_f_list_chain_item;
  const def_dec_info *definition = def_dec_p->definition;

  /* If we are protoizing, and if we found no corresponding definition for
     this particular function declaration, then just leave this declaration
     exactly as it is.  */

  if (!definition)
    return;

  /* If we are protoizing, and if the corresponding definition that we found
     for this particular function declaration defined an old style varargs
     function, then we want to issue a warning and just leave this function
     declaration unconverted.  */

  if (other_variable_style_function (definition->ansi_decl))
    {
      if (!quiet_flag)
	notice ("%s: %d: warning: varargs function declaration not converted\n",
		shortpath (NULL, def_dec_p->file->hash_entry->symbol),
		def_dec_p->line);
      return;
    }

#endif /* !defined (UNPROTOIZE) */

  /* Setup here to recover from confusing source code detected during this
     particular "edit".  */

  save_pointers ();
  if (setjmp (source_confusion_recovery))
    {
      restore_pointers ();
      notice ("%s: declaration of function '%s' not converted\n",
	      pname, function_to_edit);
      return;
    }

  /* We are editing a function declaration.  The line number we did a seek to
     contains the comma or semicolon which follows the declaration.  Our job
     now is to scan backwards looking for the function name.  This name *must*
     be followed by open paren (ignoring whitespace, of course).  We need to
     replace everything between that open paren and the corresponding closing
     paren.  If we are protoizing, we need to insert the prototype-style
     formals lists.  If we are unprotoizing, we need to just delete everything
     between the pairs of opening and closing parens.  */

  /* First move up to the end of the line.  */

  while (*clean_text_p != '\n')
    check_source (++clean_text_p < clean_text_limit, 0);
  clean_text_p--;  /* Point to just before the newline character.  */

  /* Now we can scan backwards for the function name.  */

  do
    {
      for (;;)
	{
	  /* Scan leftwards until we find some character which can be
	     part of an identifier.  */

	  while (!is_id_char (*clean_text_p))
	    check_source (--clean_text_p > clean_read_ptr, 0);

	  /* Scan backwards until we find a char that cannot be part of an
	     identifier.  */

	  while (is_id_char (*clean_text_p))
	    check_source (--clean_text_p > clean_read_ptr, 0);

	  /* Having found an "id break", see if the following id is the one
	     that we are looking for.  If so, then exit from this loop.  */

	  if (!strncmp (clean_text_p+1, function_to_edit, func_name_len))
	    {
	      char ch = *(clean_text_p + 1 + func_name_len);

	      /* Must also check to see that the name in the source text
	         ends where it should (in order to prevent bogus matches
	         on similar but longer identifiers.  */

	      if (! is_id_char (ch))
	        break;			/* exit from loop */
	    }
	}

      /* We have now found the first perfect match for the function name in
	 our backward search.  This may or may not be the actual function
	 name at the start of the actual function declaration (i.e. we could
	 have easily been mislead).  We will try to avoid getting fooled too
	 often by looking forward for the open paren which should follow the
	 identifier we just found.  We ignore whitespace while hunting.  If
	 the next non-whitespace byte we see is *not* an open left paren,
	 then we must assume that we have been fooled and we start over
	 again accordingly.  Note that there is no guarantee, that even if
	 we do see the open paren, that we are in the right place.
	 Programmers do the strangest things sometimes!  */

      end_of_fn_name = clean_text_p + strlen (def_dec_p->hash_entry->symbol);
      start_formals = forward_to_next_token_char (end_of_fn_name);
    }
  while (*start_formals != '(');

  /* start_of_formals now points to the opening left paren which immediately
     follows the name of the function.  */

  /* Note that there may be several formals lists which need to be modified
     due to the possibility that the return type of this function is a
     pointer-to-function type.  If there are several formals lists, we
     convert them in left-to-right order here.  */

#ifndef UNPROTOIZE
  this_f_list_chain_item = definition->f_list_chain;
#endif /* !defined (UNPROTOIZE) */

  for (;;)
    {
      {
	int depth;

	end_formals = start_formals + 1;
	depth = 1;
	for (; depth; check_source (++end_formals < clean_text_limit, 0))
	  {
	    switch (*end_formals)
	      {
	      case '(':
		depth++;
		break;
	      case ')':
		depth--;
		break;
	      }
	  }
	end_formals--;
      }

      /* end_formals now points to the closing right paren of the formals
	 list whose left paren is pointed to by start_formals.  */

      /* Now, if we are protoizing, we insert the new ANSI-style formals list
	 attached to the associated definition of this function.  If however
	 we are unprotoizing, then we simply delete any formals list which
	 may be present.  */

      output_up_to (start_formals);
#ifndef UNPROTOIZE
      if (this_f_list_chain_item)
	{
	  output_string (this_f_list_chain_item->formals_list);
	  this_f_list_chain_item = this_f_list_chain_item->chain_next;
	}
      else
	{
	  if (!quiet_flag)
	    notice ("%s: warning: too many parameter lists in declaration of '%s'\n",
		    pname, def_dec_p->hash_entry->symbol);
	  check_source (0, end_formals);  /* leave the declaration intact */
	}
#endif /* !defined (UNPROTOIZE) */
      clean_read_ptr = end_formals - 1;

      /* Now see if it looks like there may be another formals list associated
	 with the function declaration that we are converting (following the
	 formals list that we just converted.  */

      {
	const char *another_r_paren = forward_to_next_token_char (end_formals);

	if ((*another_r_paren != ')')
	    || (*(start_formals = forward_to_next_token_char (another_r_paren)) != '('))
	  {
#ifndef UNPROTOIZE
	    if (this_f_list_chain_item)
	      {
		if (!quiet_flag)
		  notice ("\n%s: warning: too few parameter lists in declaration of '%s'\n",
			  pname, def_dec_p->hash_entry->symbol);
		check_source (0, start_formals); /* leave the decl intact */
	      }
#endif /* !defined (UNPROTOIZE) */
	    break;

	  }
      }

      /* There does appear to be yet another formals list, so loop around
	 again, and convert it also.  */
    }
}

/* Edit a whole group of formals lists, starting with the rightmost one
   from some set of formals lists.  This routine is called once (from the
   outside) for each function declaration which is converted.  It is
   recursive however, and it calls itself once for each remaining formal
   list that lies to the left of the one it was originally called to work
   on.  Thus, a whole set gets done in right-to-left order.

   This routine returns nonzero if it thinks that it should not be trying
   to convert this particular function definition (because the name of the
   function doesn't match the one expected).  */

static int
edit_formals_lists (const char *end_formals, unsigned int f_list_count,
		    const def_dec_info *def_dec_p)
{
  const char *start_formals;
  int depth;

  start_formals = end_formals - 1;
  depth = 1;
  for (; depth; check_source (--start_formals > clean_read_ptr, 0))
    {
      switch (*start_formals)
	{
	case '(':
	  depth--;
	  break;
	case ')':
	  depth++;
	  break;
	}
    }
  start_formals++;

  /* start_formals now points to the opening left paren of the formals list.  */

  f_list_count--;

  if (f_list_count)
    {
      const char *next_end;

      /* There should be more formal lists to the left of here.  */

      next_end = start_formals - 1;
      check_source (next_end > clean_read_ptr, 0);
      while (ISSPACE ((const unsigned char)*next_end))
	check_source (--next_end > clean_read_ptr, 0);
      check_source (*next_end == ')', next_end);
      check_source (--next_end > clean_read_ptr, 0);
      check_source (*next_end == ')', next_end);
      if (edit_formals_lists (next_end, f_list_count, def_dec_p))
	return 1;
    }

  /* Check that the function name in the header we are working on is the same
     as the one we would expect to find.  If not, issue a warning and return
     nonzero.  */

  if (f_list_count == 0)
    {
      const char *expected = def_dec_p->hash_entry->symbol;
      const char *func_name_start;
      const char *func_name_limit;
      size_t func_name_len;

      for (func_name_limit = start_formals-1;
	   ISSPACE ((const unsigned char)*func_name_limit); )
	check_source (--func_name_limit > clean_read_ptr, 0);

      for (func_name_start = func_name_limit++;
	   is_id_char (*func_name_start);
	   func_name_start--)
	check_source (func_name_start > clean_read_ptr, 0);
      func_name_start++;
      func_name_len = func_name_limit - func_name_start;
      if (func_name_len == 0)
	check_source (0, func_name_start);
      if (func_name_len != strlen (expected)
	  || strncmp (func_name_start, expected, func_name_len))
	{
	  notice ("%s: %d: warning: found '%s' but expected '%s'\n",
		  shortpath (NULL, def_dec_p->file->hash_entry->symbol),
		  identify_lineno (func_name_start),
		  dupnstr (func_name_start, func_name_len),
		  expected);
	  return 1;
	}
    }

  output_up_to (start_formals);

#ifdef UNPROTOIZE
  if (f_list_count == 0)
    output_string (def_dec_p->formal_names);
#else /* !defined (UNPROTOIZE) */
  {
    unsigned f_list_depth;
    const f_list_chain_item *flci_p = def_dec_p->f_list_chain;

    /* At this point, the current value of f_list count says how many
       links we have to follow through the f_list_chain to get to the
       particular formals list that we need to output next.  */

    for (f_list_depth = 0; f_list_depth < f_list_count; f_list_depth++)
      flci_p = flci_p->chain_next;
    output_string (flci_p->formals_list);
  }
#endif /* !defined (UNPROTOIZE) */

  clean_read_ptr = end_formals - 1;
  return 0;
}

/* Given a pointer to a byte in the clean text buffer which points to
   the beginning of a line that contains a "follower" token for a
   function definition header, do whatever is necessary to find the
   right closing paren for the rightmost formals list of the function
   definition header.  */

static const char *
find_rightmost_formals_list (const char *clean_text_p)
{
  const char *end_formals;

  /* We are editing a function definition.  The line number we did a seek
     to contains the first token which immediately follows the entire set of
     formals lists which are part of this particular function definition
     header.

     Our job now is to scan leftwards in the clean text looking for the
     right-paren which is at the end of the function header's rightmost
     formals list.

     If we ignore whitespace, this right paren should be the first one we
     see which is (ignoring whitespace) immediately followed either by the
     open curly-brace beginning the function body or by an alphabetic
     character (in the case where the function definition is in old (K&R)
     style and there are some declarations of formal parameters).  */

   /* It is possible that the right paren we are looking for is on the
      current line (together with its following token).  Just in case that
      might be true, we start out here by skipping down to the right end of
      the current line before starting our scan.  */

  for (end_formals = clean_text_p; *end_formals != '\n'; end_formals++)
    continue;
  end_formals--;

#ifdef UNPROTOIZE

  /* Now scan backwards while looking for the right end of the rightmost
     formals list associated with this function definition.  */

  {
    char ch;
    const char *l_brace_p;

    /* Look leftward and try to find a right-paren.  */

    while (*end_formals != ')')
      {
	if (ISSPACE ((unsigned char)*end_formals))
	  while (ISSPACE ((unsigned char)*end_formals))
	    check_source (--end_formals > clean_read_ptr, 0);
	else
	  check_source (--end_formals > clean_read_ptr, 0);
      }

    ch = *(l_brace_p = forward_to_next_token_char (end_formals));
    /* Since we are unprotoizing an ANSI-style (prototyped) function
       definition, there had better not be anything (except whitespace)
       between the end of the ANSI formals list and the beginning of the
       function body (i.e. the '{').  */

    check_source (ch == '{', l_brace_p);
  }

#else /* !defined (UNPROTOIZE) */

  /* Now scan backwards while looking for the right end of the rightmost
     formals list associated with this function definition.  */

  while (1)
    {
      char ch;
      const char *l_brace_p;

      /* Look leftward and try to find a right-paren.  */

      while (*end_formals != ')')
	{
	  if (ISSPACE ((const unsigned char)*end_formals))
	    while (ISSPACE ((const unsigned char)*end_formals))
	      check_source (--end_formals > clean_read_ptr, 0);
	  else
	    check_source (--end_formals > clean_read_ptr, 0);
	}

      ch = *(l_brace_p = forward_to_next_token_char (end_formals));

      /* Since it is possible that we found a right paren before the starting
	 '{' of the body which IS NOT the one at the end of the real K&R
	 formals list (say for instance, we found one embedded inside one of
	 the old K&R formal parameter declarations) we have to check to be
	 sure that this is in fact the right paren that we were looking for.

	 The one we were looking for *must* be followed by either a '{' or
	 by an alphabetic character, while others *cannot* validly be followed
	 by such characters.  */

      if ((ch == '{') || ISALPHA ((unsigned char) ch))
	break;

      /* At this point, we have found a right paren, but we know that it is
	 not the one we were looking for, so backup one character and keep
	 looking.  */

      check_source (--end_formals > clean_read_ptr, 0);
    }

#endif /* !defined (UNPROTOIZE) */

  return end_formals;
}

#ifndef UNPROTOIZE

/* Insert into the output file a totally new declaration for a function
   which (up until now) was being called from within the current block
   without having been declared at any point such that the declaration
   was visible (i.e. in scope) at the point of the call.

   We need to add in explicit declarations for all such function calls
   in order to get the full benefit of prototype-based function call
   parameter type checking.  */

static void
add_local_decl (const def_dec_info *def_dec_p, const char *clean_text_p)
{
  const char *start_of_block;
  const char *function_to_edit = def_dec_p->hash_entry->symbol;

  /* Don't insert new local explicit declarations unless explicitly requested
     to do so.  */

  if (!local_flag)
    return;

  /* Setup here to recover from confusing source code detected during this
     particular "edit".  */

  save_pointers ();
  if (setjmp (source_confusion_recovery))
    {
      restore_pointers ();
      notice ("%s: local declaration for function '%s' not inserted\n",
	      pname, function_to_edit);
      return;
    }

  /* We have already done a seek to the start of the line which should
     contain *the* open curly brace which begins the block in which we need
     to insert an explicit function declaration (to replace the implicit one).

     Now we scan that line, starting from the left, until we find the
     open curly brace we are looking for.  Note that there may actually be
     multiple open curly braces on the given line, but we will be happy
     with the leftmost one no matter what.  */

  start_of_block = clean_text_p;
  while (*start_of_block != '{' && *start_of_block != '\n')
    check_source (++start_of_block < clean_text_limit, 0);

  /* Note that the line from the original source could possibly
     contain *no* open curly braces!  This happens if the line contains
     a macro call which expands into a chunk of text which includes a
     block (and that block's associated open and close curly braces).
     In cases like this, we give up, issue a warning, and do nothing.  */

  if (*start_of_block != '{')
    {
      if (!quiet_flag)
	notice ("\n%s: %d: warning: can't add declaration of '%s' into macro call\n",
	  def_dec_p->file->hash_entry->symbol, def_dec_p->line,
	  def_dec_p->hash_entry->symbol);
      return;
    }

  /* Figure out what a nice (pretty) indentation would be for the new
     declaration we are adding.  In order to do this, we must scan forward
     from the '{' until we find the first line which starts with some
     non-whitespace characters (i.e. real "token" material).  */

  {
    const char *ep = forward_to_next_token_char (start_of_block) - 1;
    const char *sp;

    /* Now we have ep pointing at the rightmost byte of some existing indent
       stuff.  At least that is the hope.

       We can now just scan backwards and find the left end of the existing
       indentation string, and then copy it to the output buffer.  */

    for (sp = ep; ISSPACE ((const unsigned char)*sp) && *sp != '\n'; sp--)
      continue;

    /* Now write out the open { which began this block, and any following
       trash up to and including the last byte of the existing indent that
       we just found.  */

    output_up_to (ep);

    /* Now we go ahead and insert the new declaration at this point.

       If the definition of the given function is in the same file that we
       are currently editing, and if its full ANSI declaration normally
       would start with the keyword `extern', suppress the `extern'.  */

    {
      const char *decl = def_dec_p->definition->ansi_decl;

      if ((*decl == 'e') && (def_dec_p->file == def_dec_p->definition->file))
	decl += 7;
      output_string (decl);
    }

    /* Finally, write out a new indent string, just like the preceding one
       that we found.  This will typically include a newline as the first
       character of the indent string.  */

    output_bytes (sp, (size_t) (ep - sp) + 1);
  }
}

/* Given a pointer to a file_info record, and a pointer to the beginning
   of a line (in the clean text buffer) which is assumed to contain the
   first "follower" token for the first function definition header in the
   given file, find a good place to insert some new global function
   declarations (which will replace scattered and imprecise implicit ones)
   and then insert the new explicit declaration at that point in the file.  */

static void
add_global_decls (const file_info *file_p, const char *clean_text_p)
{
  const def_dec_info *dd_p;
  const char *scan_p;

  /* Setup here to recover from confusing source code detected during this
     particular "edit".  */

  save_pointers ();
  if (setjmp (source_confusion_recovery))
    {
      restore_pointers ();
      notice ("%s: global declarations for file '%s' not inserted\n",
	      pname, shortpath (NULL, file_p->hash_entry->symbol));
      return;
    }

  /* Start by finding a good location for adding the new explicit function
     declarations.  To do this, we scan backwards, ignoring whitespace
     and comments and other junk until we find either a semicolon, or until
     we hit the beginning of the file.  */

  scan_p = find_rightmost_formals_list (clean_text_p);
  for (;; --scan_p)
    {
      if (scan_p < clean_text_base)
	break;
      check_source (scan_p > clean_read_ptr, 0);
      if (*scan_p == ';')
	break;
    }

  /* scan_p now points either to a semicolon, or to just before the start
     of the whole file.  */

  /* Now scan forward for the first non-whitespace character.  In theory,
     this should be the first character of the following function definition
     header.  We will put in the added declarations just prior to that.  */

  scan_p++;
  while (ISSPACE ((const unsigned char)*scan_p))
    scan_p++;
  scan_p--;

  output_up_to (scan_p);

  /* Now write out full prototypes for all of the things that had been
     implicitly declared in this file (but only those for which we were
     actually able to find unique matching definitions).  Avoid duplicates
     by marking things that we write out as we go.  */

  {
    int some_decls_added = 0;

    for (dd_p = file_p->defs_decs; dd_p; dd_p = dd_p->next_in_file)
      if (dd_p->is_implicit && dd_p->definition && !dd_p->definition->written)
	{
	  const char *decl = dd_p->definition->ansi_decl;

	  /* If the function for which we are inserting a declaration is
	     actually defined later in the same file, then suppress the
	     leading `extern' keyword (if there is one).  */

	  if (*decl == 'e' && (dd_p->file == dd_p->definition->file))
	    decl += 7;

	  output_string ("\n");
	  output_string (decl);
	  some_decls_added = 1;
	  ((NONCONST def_dec_info *) dd_p->definition)->written = 1;
	}
    if (some_decls_added)
      output_string ("\n\n");
  }

  /* Unmark all of the definitions that we just marked.  */

  for (dd_p = file_p->defs_decs; dd_p; dd_p = dd_p->next_in_file)
    if (dd_p->definition)
      ((NONCONST def_dec_info *) dd_p->definition)->written = 0;
}

#endif /* !defined (UNPROTOIZE) */

/* Do the editing operation specifically for a function "definition".  Note
   that editing operations for function "declarations" are handled by a
   separate routine above.  */

static void
edit_fn_definition (const def_dec_info *def_dec_p,
		    const char *volatile clean_text_p)
{
  const char *end_formals;
  const char *function_to_edit = def_dec_p->hash_entry->symbol;

  /* Setup here to recover from confusing source code detected during this
     particular "edit".  */

  save_pointers ();
  if (setjmp (source_confusion_recovery))
    {
      restore_pointers ();
      notice ("%s: definition of function '%s' not converted\n",
	      pname, function_to_edit);
      return;
    }

  end_formals = find_rightmost_formals_list (clean_text_p);

  /* end_of_formals now points to the closing right paren of the rightmost
     formals list which is actually part of the `header' of the function
     definition that we are converting.  */

  /* If the header of this function definition looks like it declares a
     function with a variable number of arguments, and if the way it does
     that is different from that way we would like it (i.e. varargs vs.
     stdarg) then issue a warning and leave the header unconverted.  */

  if (other_variable_style_function (def_dec_p->ansi_decl))
    {
      if (!quiet_flag)
	notice ("%s: %d: warning: definition of %s not converted\n",
		shortpath (NULL, def_dec_p->file->hash_entry->symbol),
		identify_lineno (end_formals),
		other_var_style);
      output_up_to (end_formals);
      return;
    }

  if (edit_formals_lists (end_formals, def_dec_p->f_list_count, def_dec_p))
    {
      restore_pointers ();
      notice ("%s: definition of function '%s' not converted\n",
	      pname, function_to_edit);
      return;
    }

  /* Have to output the last right paren because this never gets flushed by
     edit_formals_list.  */

  output_up_to (end_formals);

#ifdef UNPROTOIZE
  {
    const char *decl_p;
    const char *semicolon_p;
    const char *limit_p;
    const char *scan_p;
    int had_newlines = 0;

    /* Now write out the K&R style formal declarations, one per line.  */

    decl_p = def_dec_p->formal_decls;
    limit_p = decl_p + strlen (decl_p);
    for (;decl_p < limit_p; decl_p = semicolon_p + 2)
      {
	for (semicolon_p = decl_p; *semicolon_p != ';'; semicolon_p++)
	  continue;
	output_string ("\n");
	output_string (indent_string);
	output_bytes (decl_p, (size_t) ((semicolon_p + 1) - decl_p));
      }

    /* If there are no newlines between the end of the formals list and the
       start of the body, we should insert one now.  */

    for (scan_p = end_formals+1; *scan_p != '{'; )
      {
	if (*scan_p == '\n')
	  {
	    had_newlines = 1;
	    break;
	  }
	check_source (++scan_p < clean_text_limit, 0);
      }
    if (!had_newlines)
      output_string ("\n");
  }
#else /* !defined (UNPROTOIZE) */
  /* If we are protoizing, there may be some flotsam & jetsam (like comments
     and preprocessing directives) after the old formals list but before
     the following { and we would like to preserve that stuff while effectively
     deleting the existing K&R formal parameter declarations.  We do so here
     in a rather tricky way.  Basically, we white out any stuff *except*
     the comments/pp-directives in the original text buffer, then, if there
     is anything in this area *other* than whitespace, we output it.  */
  {
    const char *end_formals_orig;
    const char *start_body;
    const char *start_body_orig;
    const char *scan;
    const char *scan_orig;
    int have_flotsam = 0;
    int have_newlines = 0;

    for (start_body = end_formals + 1; *start_body != '{';)
      check_source (++start_body < clean_text_limit, 0);

    end_formals_orig = orig_text_base + (end_formals - clean_text_base);
    start_body_orig = orig_text_base + (start_body - clean_text_base);
    scan = end_formals + 1;
    scan_orig = end_formals_orig + 1;
    for (; scan < start_body; scan++, scan_orig++)
      {
	if (*scan == *scan_orig)
	  {
	    have_newlines |= (*scan_orig == '\n');
	    /* Leave identical whitespace alone.  */
	    if (!ISSPACE ((const unsigned char)*scan_orig))
	      *((NONCONST char *) scan_orig) = ' '; /* identical - so whiteout */
	  }
	else
	  have_flotsam = 1;
      }
    if (have_flotsam)
      output_bytes (end_formals_orig + 1,
		    (size_t) (start_body_orig - end_formals_orig) - 1);
    else
      if (have_newlines)
	output_string ("\n");
      else
	output_string (" ");
    clean_read_ptr = start_body - 1;
  }
#endif /* !defined (UNPROTOIZE) */
}

/* Clean up the clean text buffer.  Do this by converting comments and
   preprocessing directives into spaces.   Also convert line continuations
   into whitespace.  Also, whiteout string and character literals.  */

static void
do_cleaning (char *new_clean_text_base, const char *new_clean_text_limit)
{
  char *scan_p;
  int non_whitespace_since_newline = 0;

  for (scan_p = new_clean_text_base; scan_p < new_clean_text_limit; scan_p++)
    {
      switch (*scan_p)
	{
	case '/':			/* Handle comments.  */
	  if (scan_p[1] != '*')
	    goto regular;
	  non_whitespace_since_newline = 1;
	  scan_p[0] = ' ';
	  scan_p[1] = ' ';
	  scan_p += 2;
	  while (scan_p[1] != '/' || scan_p[0] != '*')
	    {
	      if (!ISSPACE ((const unsigned char)*scan_p))
		*scan_p = ' ';
	      ++scan_p;
	      gcc_assert (scan_p < new_clean_text_limit);
	    }
	  *scan_p++ = ' ';
	  *scan_p = ' ';
	  break;

	case '#':			/* Handle pp directives.  */
	  if (non_whitespace_since_newline)
	    goto regular;
	  *scan_p = ' ';
	  while (scan_p[1] != '\n' || scan_p[0] == '\\')
	    {
	      if (!ISSPACE ((const unsigned char)*scan_p))
		*scan_p = ' ';
	      ++scan_p;
	      gcc_assert (scan_p < new_clean_text_limit);
	    }
	  *scan_p++ = ' ';
	  break;

	case '\'':			/* Handle character literals.  */
	  non_whitespace_since_newline = 1;
	  while (scan_p[1] != '\'' || scan_p[0] == '\\')
	    {
	      if (scan_p[0] == '\\'
		  && !ISSPACE ((const unsigned char) scan_p[1]))
		scan_p[1] = ' ';
	      if (!ISSPACE ((const unsigned char)*scan_p))
		*scan_p = ' ';
	      ++scan_p;
	      gcc_assert (scan_p < new_clean_text_limit);
	    }
	  *scan_p++ = ' ';
	  break;

	case '"':			/* Handle string literals.  */
	  non_whitespace_since_newline = 1;
	  while (scan_p[1] != '"' || scan_p[0] == '\\')
	    {
	      if (scan_p[0] == '\\'
		  && !ISSPACE ((const unsigned char) scan_p[1]))
		scan_p[1] = ' ';
	      if (!ISSPACE ((const unsigned char)*scan_p))
		*scan_p = ' ';
	      ++scan_p;
	      gcc_assert (scan_p < new_clean_text_limit);
	    }
	  if (!ISSPACE ((const unsigned char)*scan_p))
	    *scan_p = ' ';
	  scan_p++;
	  break;

	case '\\':			/* Handle line continuations.  */
	  if (scan_p[1] != '\n')
	    goto regular;
	  *scan_p = ' ';
	  break;

	case '\n':
	  non_whitespace_since_newline = 0;	/* Reset.  */
	  break;

	case ' ':
	case '\v':
	case '\t':
	case '\r':
	case '\f':
	case '\b':
	  break;		/* Whitespace characters.  */

	default:
regular:
	  non_whitespace_since_newline = 1;
	  break;
	}
    }
}

/* Given a pointer to the closing right parenthesis for a particular formals
   list (in the clean text buffer) find the corresponding left parenthesis
   and return a pointer to it.  */

static const char *
careful_find_l_paren (const char *p)
{
  const char *q;
  int paren_depth;

  for (paren_depth = 1, q = p-1; paren_depth; check_source (--q >= clean_text_base, 0))
    {
      switch (*q)
	{
	case ')':
	  paren_depth++;
	  break;
	case '(':
	  paren_depth--;
	  break;
	}
    }
  return ++q;
}

/* Scan the clean text buffer for cases of function definitions that we
   don't really know about because they were preprocessed out when the
   aux info files were created.

   In this version of protoize/unprotoize we just give a warning for each
   one found.  A later version may be able to at least unprotoize such
   missed items.

   Note that we may easily find all function definitions simply by
   looking for places where there is a left paren which is (ignoring
   whitespace) immediately followed by either a left-brace or by an
   upper or lower case letter.  Whenever we find this combination, we
   have also found a function definition header.

   Finding function *declarations* using syntactic clues is much harder.
   I will probably try to do this in a later version though.  */

static void
scan_for_missed_items (const file_info *file_p)
{
  static const char *scan_p;
  const char *limit = clean_text_limit - 3;
  static const char *backup_limit;

  backup_limit = clean_text_base - 1;

  for (scan_p = clean_text_base; scan_p < limit; scan_p++)
    {
      if (*scan_p == ')')
	{
	  static const char *last_r_paren;
	  const char *ahead_p;

	  last_r_paren = scan_p;

	  for (ahead_p = scan_p + 1; ISSPACE ((const unsigned char)*ahead_p); )
	    check_source (++ahead_p < limit, limit);

	  scan_p = ahead_p - 1;

	  if (ISALPHA ((const unsigned char)*ahead_p) || *ahead_p == '{')
	    {
	      const char *last_l_paren;
	      const int lineno = identify_lineno (ahead_p);

	      if (setjmp (source_confusion_recovery))
		continue;

	      /* We know we have a function definition header.  Now skip
	         leftwards over all of its associated formals lists.  */

	      do
		{
		  last_l_paren = careful_find_l_paren (last_r_paren);
		  for (last_r_paren = last_l_paren-1;
		       ISSPACE ((const unsigned char)*last_r_paren); )
		    check_source (--last_r_paren >= backup_limit, backup_limit);
		}
	      while (*last_r_paren == ')');

	      if (is_id_char (*last_r_paren))
		{
		  const char *id_limit = last_r_paren + 1;
		  const char *id_start;
		  size_t id_length;
		  const def_dec_info *dd_p;

		  for (id_start = id_limit-1; is_id_char (*id_start); )
		    check_source (--id_start >= backup_limit, backup_limit);
		  id_start++;
		  backup_limit = id_start;
		  if ((id_length = (size_t) (id_limit - id_start)) == 0)
		    goto not_missed;

		  {
		    char *func_name = alloca (id_length + 1);
		    static const char * const stmt_keywords[]
		      = { "if", "else", "do", "while", "for", "switch", "case", "return", 0 };
		    const char * const *stmt_keyword;

		    strncpy (func_name, id_start, id_length);
		    func_name[id_length] = '\0';

		    /* We must check here to see if we are actually looking at
		       a statement rather than an actual function call.  */

		    for (stmt_keyword = stmt_keywords; *stmt_keyword; stmt_keyword++)
		      if (!strcmp (func_name, *stmt_keyword))
			goto not_missed;

#if 0
		    notice ("%s: found definition of '%s' at %s(%d)\n",
			    pname,
			    func_name,
			    shortpath (NULL, file_p->hash_entry->symbol),
			    identify_lineno (id_start));
#endif				/* 0 */
		    /* We really should check for a match of the function name
		       here also, but why bother.  */

		    for (dd_p = file_p->defs_decs; dd_p; dd_p = dd_p->next_in_file)
		      if (dd_p->is_func_def && dd_p->line == lineno)
			goto not_missed;

		    /* If we make it here, then we did not know about this
		       function definition.  */

		    notice ("%s: %d: warning: '%s' excluded by preprocessing\n",
			    shortpath (NULL, file_p->hash_entry->symbol),
			    identify_lineno (id_start), func_name);
		    notice ("%s: function definition not converted\n",
			    pname);
		  }
		not_missed: ;
	        }
	    }
	}
    }
}

/* Do all editing operations for a single source file (either a "base" file
   or an "include" file).  To do this we read the file into memory, keep a
   virgin copy there, make another cleaned in-core copy of the original file
   (i.e. one in which all of the comments and preprocessing directives have
   been replaced with whitespace), then use these two in-core copies of the
   file to make a new edited in-core copy of the file.  Finally, rename the
   original file (as a way of saving it), and then write the edited version
   of the file from core to a disk file of the same name as the original.

   Note that the trick of making a copy of the original sans comments &
   preprocessing directives make the editing a whole lot easier.  */

static void
edit_file (const hash_table_entry *hp)
{
  struct stat stat_buf;
  const file_info *file_p = hp->fip;
  char *new_orig_text_base;
  char *new_orig_text_limit;
  char *new_clean_text_base;
  char *new_clean_text_limit;
  size_t orig_size;
  size_t repl_size;
  int first_definition_in_file;

  /* If we are not supposed to be converting this file, or if there is
     nothing in there which needs converting, just skip this file.  */

  if (!needs_to_be_converted (file_p))
    return;

  convert_filename = file_p->hash_entry->symbol;

  /* Convert a file if it is in a directory where we want conversion
     and the file is not excluded.  */

  if (!directory_specified_p (convert_filename)
      || file_excluded_p (convert_filename))
    {
      if (!quiet_flag
#ifdef UNPROTOIZE
	  /* Don't even mention "system" include files unless we are
	     protoizing.  If we are protoizing, we mention these as a
	     gentle way of prodding the user to convert his "system"
	     include files to prototype format.  */
	  && !in_system_include_dir (convert_filename)
#endif /* defined (UNPROTOIZE) */
	  )
	notice ("%s: '%s' not converted\n",
		pname, shortpath (NULL, convert_filename));
      return;
    }

  /* Let the user know what we are up to.  */

  if (nochange_flag)
    notice ("%s: would convert file '%s'\n",
	    pname, shortpath (NULL, convert_filename));
  else
    notice ("%s: converting file '%s'\n",
	    pname, shortpath (NULL, convert_filename));
  fflush (stderr);

  /* Find out the size (in bytes) of the original file.  */

  /* The cast avoids an erroneous warning on AIX.  */
  if (stat (convert_filename, &stat_buf) == -1)
    {
      int errno_val = errno;
      notice ("%s: can't get status for file '%s': %s\n",
	      pname, shortpath (NULL, convert_filename),
	      xstrerror (errno_val));
      return;
    }
  orig_size = stat_buf.st_size;

  /* Allocate a buffer to hold the original text.  */

  orig_text_base = new_orig_text_base = xmalloc (orig_size + 2);
  orig_text_limit = new_orig_text_limit = new_orig_text_base + orig_size;

  /* Allocate a buffer to hold the cleaned-up version of the original text.  */

  clean_text_base = new_clean_text_base = xmalloc (orig_size + 2);
  clean_text_limit = new_clean_text_limit = new_clean_text_base + orig_size;
  clean_read_ptr = clean_text_base - 1;

  /* Allocate a buffer that will hopefully be large enough to hold the entire
     converted output text.  As an initial guess for the maximum size of the
     output buffer, use 125% of the size of the original + some extra.  This
     buffer can be expanded later as needed.  */

  repl_size = orig_size + (orig_size >> 2) + 4096;
  repl_text_base = xmalloc (repl_size + 2);
  repl_text_limit = repl_text_base + repl_size - 1;
  repl_write_ptr = repl_text_base - 1;

  {
    int input_file;
    int fd_flags;

    /* Open the file to be converted in READ ONLY mode.  */

    fd_flags = O_RDONLY;
#ifdef O_BINARY
    /* Use binary mode to avoid having to deal with different EOL characters.  */
    fd_flags |= O_BINARY;
#endif
    if ((input_file = open (convert_filename, fd_flags, 0444)) == -1)
      {
	int errno_val = errno;
	notice ("%s: can't open file '%s' for reading: %s\n",
		pname, shortpath (NULL, convert_filename),
		xstrerror (errno_val));
	return;
      }

    /* Read the entire original source text file into the original text buffer
       in one swell fwoop.  Then figure out where the end of the text is and
       make sure that it ends with a newline followed by a null.  */

    if (safe_read (input_file, new_orig_text_base, orig_size) !=
	(int) orig_size)
      {
	int errno_val = errno;
	close (input_file);
	notice ("\n%s: error reading input file '%s': %s\n",
		pname, shortpath (NULL, convert_filename),
		xstrerror (errno_val));
	return;
      }

    close (input_file);
  }

  if (orig_size == 0 || orig_text_limit[-1] != '\n')
    {
      *new_orig_text_limit++ = '\n';
      orig_text_limit++;
    }

  /* Create the cleaned up copy of the original text.  */

  memcpy (new_clean_text_base, orig_text_base,
	  (size_t) (orig_text_limit - orig_text_base));
  do_cleaning (new_clean_text_base, new_clean_text_limit);

#if 0
  {
    int clean_file;
    size_t clean_size = orig_text_limit - orig_text_base;
    char *const clean_filename = alloca (strlen (convert_filename) + 6 + 1);

    /* Open (and create) the clean file.  */

    strcpy (clean_filename, convert_filename);
    strcat (clean_filename, ".clean");
    if ((clean_file = creat (clean_filename, 0666)) == -1)
      {
	int errno_val = errno;
	notice ("%s: can't create/open clean file '%s': %s\n",
		pname, shortpath (NULL, clean_filename),
		xstrerror (errno_val));
	return;
      }

    /* Write the clean file.  */

    safe_write (clean_file, new_clean_text_base, clean_size, clean_filename);

    close (clean_file);
  }
#endif /* 0 */

  /* Do a simplified scan of the input looking for things that were not
     mentioned in the aux info files because of the fact that they were
     in a region of the source which was preprocessed-out (via #if or
     via #ifdef).  */

  scan_for_missed_items (file_p);

  /* Setup to do line-oriented forward seeking in the clean text buffer.  */

  last_known_line_number = 1;
  last_known_line_start = clean_text_base;

  /* Now get down to business and make all of the necessary edits.  */

  {
    const def_dec_info *def_dec_p;

    first_definition_in_file = 1;
    def_dec_p = file_p->defs_decs;
    for (; def_dec_p; def_dec_p = def_dec_p->next_in_file)
      {
	const char *clean_text_p = seek_to_line (def_dec_p->line);

	/* clean_text_p now points to the first character of the line which
	   contains the `terminator' for the declaration or definition that
	   we are about to process.  */

#ifndef UNPROTOIZE

	if (global_flag && def_dec_p->is_func_def && first_definition_in_file)
	  {
	    add_global_decls (def_dec_p->file, clean_text_p);
	    first_definition_in_file = 0;
	  }

	/* Don't edit this item if it is already in prototype format or if it
	   is a function declaration and we have found no corresponding
	   definition.  */

	if (def_dec_p->prototyped
	    || (!def_dec_p->is_func_def && !def_dec_p->definition))
	  continue;

#endif /* !defined (UNPROTOIZE) */

	if (def_dec_p->is_func_def)
	  edit_fn_definition (def_dec_p, clean_text_p);
	else
#ifndef UNPROTOIZE
	if (def_dec_p->is_implicit)
	  add_local_decl (def_dec_p, clean_text_p);
	else
#endif /* !defined (UNPROTOIZE) */
	  edit_fn_declaration (def_dec_p, clean_text_p);
      }
  }

  /* Finalize things.  Output the last trailing part of the original text.  */

  output_up_to (clean_text_limit - 1);

  /* If this is just a test run, stop now and just deallocate the buffers.  */

  if (nochange_flag)
    {
      free (new_orig_text_base);
      free (new_clean_text_base);
      free (repl_text_base);
      return;
    }

  /* Change the name of the original input file.  This is just a quick way of
     saving the original file.  */

  if (!nosave_flag)
    {
      char *new_filename
	= xmalloc (strlen (convert_filename) + strlen (save_suffix) + 2);

      strcpy (new_filename, convert_filename);
#ifdef __MSDOS__
      /* MSDOS filenames are restricted to 8.3 format, so we save `foo.c'
	 as `foo.<save_suffix>'.  */
      new_filename[(strlen (convert_filename) - 1] = '\0';
#endif
      strcat (new_filename, save_suffix);

      /* Don't overwrite existing file.  */
      if (access (new_filename, F_OK) == 0)
	{
	  if (!quiet_flag)
	    notice ("%s: warning: file '%s' already saved in '%s'\n",
		    pname,
		    shortpath (NULL, convert_filename),
		    shortpath (NULL, new_filename));
	}
      else if (rename (convert_filename, new_filename) == -1)
	{
	  int errno_val = errno;
	  notice ("%s: can't link file '%s' to '%s': %s\n",
		  pname,
		  shortpath (NULL, convert_filename),
		  shortpath (NULL, new_filename),
		  xstrerror (errno_val));
	  return;
	}
    }

  if (unlink (convert_filename) == -1)
    {
      int errno_val = errno;
      /* The file may have already been renamed.  */
      if (errno_val != ENOENT)
	{
	  notice ("%s: can't delete file '%s': %s\n",
		  pname, shortpath (NULL, convert_filename),
		  xstrerror (errno_val));
	  return;
	}
    }

  {
    int output_file;

    /* Open (and create) the output file.  */

    if ((output_file = creat (convert_filename, 0666)) == -1)
      {
	int errno_val = errno;
	notice ("%s: can't create/open output file '%s': %s\n",
		pname, shortpath (NULL, convert_filename),
		xstrerror (errno_val));
	return;
      }
#ifdef O_BINARY
    /* Use binary mode to avoid changing the existing EOL character.  */
    setmode (output_file, O_BINARY);
#endif

    /* Write the output file.  */

    {
      unsigned int out_size = (repl_write_ptr + 1) - repl_text_base;

      safe_write (output_file, repl_text_base, out_size, convert_filename);
    }

    close (output_file);
  }

  /* Deallocate the conversion buffers.  */

  free (new_orig_text_base);
  free (new_clean_text_base);
  free (repl_text_base);

  /* Change the mode of the output file to match the original file.  */

  /* The cast avoids an erroneous warning on AIX.  */
  if (chmod (convert_filename, stat_buf.st_mode) == -1)
    {
      int errno_val = errno;
      notice ("%s: can't change mode of file '%s': %s\n",
	      pname, shortpath (NULL, convert_filename),
	      xstrerror (errno_val));
    }

  /* Note:  We would try to change the owner and group of the output file
     to match those of the input file here, except that may not be a good
     thing to do because it might be misleading.  Also, it might not even
     be possible to do that (on BSD systems with quotas for instance).  */
}

/* Do all of the individual steps needed to do the protoization (or
   unprotoization) of the files referenced in the aux_info files given
   in the command line.  */

static void
do_processing (void)
{
  const char * const *base_pp;
  const char * const * const end_pps
    = &base_source_filenames[n_base_source_files];

#ifndef UNPROTOIZE
  int syscalls_len;
#endif /* !defined (UNPROTOIZE) */

  /* One-by-one, check (and create if necessary), open, and read all of the
     stuff in each aux_info file.  After reading each aux_info file, the
     aux_info_file just read will be automatically deleted unless the
     keep_flag is set.  */

  for (base_pp = base_source_filenames; base_pp < end_pps; base_pp++)
    process_aux_info_file (*base_pp, keep_flag, 0);

#ifndef UNPROTOIZE

  /* Also open and read the special SYSCALLS.c aux_info file which gives us
     the prototypes for all of the standard system-supplied functions.  */

  if (nondefault_syscalls_dir)
    {
      syscalls_absolute_filename
	= xmalloc (strlen (nondefault_syscalls_dir) + 1
		   + sizeof (syscalls_filename));
      strcpy (syscalls_absolute_filename, nondefault_syscalls_dir);
    }
  else
    {
      GET_ENVIRONMENT (default_syscalls_dir, "GCC_EXEC_PREFIX");
      if (!default_syscalls_dir)
	{
	  default_syscalls_dir = standard_exec_prefix;
	}
      syscalls_absolute_filename
	= xmalloc (strlen (default_syscalls_dir) + 0
		   + strlen (target_machine) + 1
		   + strlen (target_version) + 1
		   + sizeof (syscalls_filename));
      strcpy (syscalls_absolute_filename, default_syscalls_dir);
      strcat (syscalls_absolute_filename, target_machine);
      strcat (syscalls_absolute_filename, "/");
      strcat (syscalls_absolute_filename, target_version);
      strcat (syscalls_absolute_filename, "/");
    }

  syscalls_len = strlen (syscalls_absolute_filename);
  if (! IS_DIR_SEPARATOR (*(syscalls_absolute_filename + syscalls_len - 1)))
    {
      *(syscalls_absolute_filename + syscalls_len++) = DIR_SEPARATOR;
      *(syscalls_absolute_filename + syscalls_len) = '\0';
    }
  strcat (syscalls_absolute_filename, syscalls_filename);

  /* Call process_aux_info_file in such a way that it does not try to
     delete the SYSCALLS aux_info file.  */

  process_aux_info_file (syscalls_absolute_filename, 1, 1);

#endif /* !defined (UNPROTOIZE) */

  /* When we first read in all of the information from the aux_info files
     we saved in it descending line number order, because that was likely to
     be faster.  Now however, we want the chains of def & dec records to
     appear in ascending line number order as we get further away from the
     file_info record that they hang from.  The following line causes all of
     these lists to be rearranged into ascending line number order.  */

  visit_each_hash_node (filename_primary, reverse_def_dec_list);

#ifndef UNPROTOIZE

  /* Now do the "real" work.  The following line causes each declaration record
     to be "visited".  For each of these nodes, an attempt is made to match
     up the function declaration with a corresponding function definition,
     which should have a full prototype-format formals list with it.  Once
     these match-ups are made, the conversion of the function declarations
     to prototype format can be made.  */

  visit_each_hash_node (function_name_primary, connect_defs_and_decs);

#endif /* !defined (UNPROTOIZE) */

  /* Now convert each file that can be converted (and needs to be).  */

  visit_each_hash_node (filename_primary, edit_file);

#ifndef UNPROTOIZE

  /* If we are working in cplusplus mode, try to rename all .c files to .C
     files.  Don't panic if some of the renames don't work.  */

  if (cplusplus_flag && !nochange_flag)
    visit_each_hash_node (filename_primary, rename_c_file);

#endif /* !defined (UNPROTOIZE) */
}

static const struct option longopts[] =
{
  {"version", 0, 0, 'V'},
  {"file_name", 0, 0, 'p'},
  {"quiet", 0, 0, 'q'},
  {"silent", 0, 0, 'q'},
  {"force", 0, 0, 'f'},
  {"keep", 0, 0, 'k'},
  {"nosave", 0, 0, 'N'},
  {"nochange", 0, 0, 'n'},
  {"compiler-options", 1, 0, 'c'},
  {"exclude", 1, 0, 'x'},
  {"directory", 1, 0, 'd'},
#ifdef UNPROTOIZE
  {"indent", 1, 0, 'i'},
#else
  {"local", 0, 0, 'l'},
  {"global", 0, 0, 'g'},
  {"c++", 0, 0, 'C'},
  {"syscalls-dir", 1, 0, 'B'},
#endif
  {0, 0, 0, 0}
};

extern int main (int, char **const);

int
main (int argc, char **const argv)
{
  int longind;
  int c;
  const char *params = "";

  pname = strrchr (argv[0], DIR_SEPARATOR);
#ifdef DIR_SEPARATOR_2
  {
    char *slash;

    slash = strrchr (pname ? pname : argv[0], DIR_SEPARATOR_2);
    if (slash)
      pname = slash;
  }
#endif
  pname = pname ? pname+1 : argv[0];

#ifdef SIGCHLD
  /* We *MUST* set SIGCHLD to SIG_DFL so that the wait4() call will
     receive the signal.  A different setting is inheritable */
  signal (SIGCHLD, SIG_DFL);
#endif

  /* Unlock the stdio streams.  */
  unlock_std_streams ();

  gcc_init_libintl ();

  cwd_buffer = getpwd ();
  if (!cwd_buffer)
    {
      notice ("%s: cannot get working directory: %s\n",
	      pname, xstrerror(errno));
      return (FATAL_EXIT_CODE);
    }

  /* By default, convert the files in the current directory.  */
  directory_list = string_list_cons (cwd_buffer, NULL);

  while ((c = getopt_long (argc, argv,
#ifdef UNPROTOIZE
			   "c:d:i:knNp:qvVx:",
#else
			   "B:c:Cd:gklnNp:qvVx:",
#endif
			   longopts, &longind)) != EOF)
    {
      if (c == 0)		/* Long option.  */
	c = longopts[longind].val;
      switch (c)
	{
	case 'p':
	  compiler_file_name = optarg;
	  break;
	case 'd':
	  directory_list
	    = string_list_cons (abspath (NULL, optarg), directory_list);
	  break;
	case 'x':
	  exclude_list = string_list_cons (optarg, exclude_list);
	  break;

	case 'v':
	case 'V':
	  version_flag = 1;
	  break;
	case 'q':
	  quiet_flag = 1;
	  break;
#if 0
	case 'f':
	  force_flag = 1;
	  break;
#endif
	case 'n':
	  nochange_flag = 1;
	  keep_flag = 1;
	  break;
	case 'N':
	  nosave_flag = 1;
	  break;
	case 'k':
	  keep_flag = 1;
	  break;
	case 'c':
	  params = optarg;
	  break;
#ifdef UNPROTOIZE
	case 'i':
	  indent_string = optarg;
	  break;
#else				/* !defined (UNPROTOIZE) */
	case 'l':
	  local_flag = 1;
	  break;
	case 'g':
	  global_flag = 1;
	  break;
	case 'C':
	  cplusplus_flag = 1;
	  break;
	case 'B':
	  nondefault_syscalls_dir = optarg;
	  break;
#endif				/* !defined (UNPROTOIZE) */
	default:
	  usage ();
	}
    }

  /* Set up compile_params based on -p and -c options.  */
  munge_compile_params (params);

  n_base_source_files = argc - optind;

  /* Now actually make a list of the base source filenames.  */

  base_source_filenames
    = xmalloc ((n_base_source_files + 1) * sizeof (char *));
  n_base_source_files = 0;
  for (; optind < argc; optind++)
    {
      const char *path = abspath (NULL, argv[optind]);
      int len = strlen (path);

      if (path[len-1] == 'c' && path[len-2] == '.')
	base_source_filenames[n_base_source_files++] = path;
      else
	{
	  notice ("%s: input file names must have .c suffixes: %s\n",
		  pname, shortpath (NULL, path));
	  errors++;
	}
    }

#ifndef UNPROTOIZE
  /* We are only interested in the very first identifier token in the
     definition of `va_list', so if there is more junk after that first
     identifier token, delete it from the `varargs_style_indicator'.  */
  {
    const char *cp;

    for (cp = varargs_style_indicator; ISIDNUM (*cp); cp++)
      continue;
    if (*cp != 0)
      varargs_style_indicator = savestring (varargs_style_indicator,
					    cp - varargs_style_indicator);
  }
#endif /* !defined (UNPROTOIZE) */

  if (errors)
    usage ();
  else
    {
      if (version_flag)
	fprintf (stderr, "%s: %s\n", pname, version_string);
      do_processing ();
    }

  return (errors ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}
