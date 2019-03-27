/* List lines of source files for GDB, the GNU debugger.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
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
#include "expression.h"
#include "language.h"
#include "command.h"
#include "source.h"
#include "gdbcmd.h"
#include "frame.h"
#include "value.h"

#include <sys/types.h>
#include "gdb_string.h"
#include "gdb_stat.h"
#include <fcntl.h>
#include "gdbcore.h"
#include "gdb_regex.h"
#include "symfile.h"
#include "objfiles.h"
#include "annotate.h"
#include "gdbtypes.h"
#include "linespec.h"
#include "filenames.h"		/* for DOSish file names */
#include "completer.h"
#include "ui-out.h"
#include "readline/readline.h"

#ifdef CRLF_SOURCE_FILES

/* Define CRLF_SOURCE_FILES in an xm-*.h file if source files on the
   host use \r\n rather than just \n.  Defining CRLF_SOURCE_FILES is
   much faster than defining LSEEK_NOT_LINEAR.  */

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define OPEN_MODE (O_RDONLY | O_BINARY)
#define FDOPEN_MODE FOPEN_RB

#else /* ! defined (CRLF_SOURCE_FILES) */

#define OPEN_MODE O_RDONLY
#define FDOPEN_MODE FOPEN_RT

#endif /* ! defined (CRLF_SOURCE_FILES) */

/* Prototypes for exported functions. */

void _initialize_source (void);

/* Prototypes for local functions. */

static int get_filename_and_charpos (struct symtab *, char **);

static void reverse_search_command (char *, int);

static void forward_search_command (char *, int);

static void line_info (char *, int);

static void source_info (char *, int);

static void show_directories (char *, int);

/* Path of directories to search for source files.
   Same format as the PATH environment variable's value.  */

char *source_path;

/* Symtab of default file for listing lines of.  */

static struct symtab *current_source_symtab;

/* Default next line to list.  */

static int current_source_line;

/* Default number of lines to print with commands like "list".
   This is based on guessing how many long (i.e. more than chars_per_line
   characters) lines there will be.  To be completely correct, "list"
   and friends should be rewritten to count characters and see where
   things are wrapping, but that would be a fair amount of work.  */

int lines_to_list = 10;

/* Line number of last line printed.  Default for various commands.
   current_source_line is usually, but not always, the same as this.  */

static int last_line_listed;

/* First line number listed by last listing command.  */

static int first_line_listed;

/* Saves the name of the last source file visited and a possible error code.
   Used to prevent repeating annoying "No such file or directories" msgs */

static struct symtab *last_source_visited = NULL;
static int last_source_error = 0;

/* Return the first line listed by print_source_lines.
   Used by command interpreters to request listing from
   a previous point. */

int
get_first_line_listed (void)
{
  return first_line_listed;
}

/* Return the default number of lines to print with commands like the
   cli "list".  The caller of print_source_lines must use this to
   calculate the end line and use it in the call to print_source_lines
   as it does not automatically use this value. */

int
get_lines_to_list (void)
{
  return lines_to_list;
}

/* Return the current source file for listing and next line to list.
   NOTE: The returned sal pc and end fields are not valid. */
   
struct symtab_and_line
get_current_source_symtab_and_line (void)
{
  struct symtab_and_line cursal;

  cursal.symtab = current_source_symtab;
  cursal.line = current_source_line;
  cursal.pc = 0;
  cursal.end = 0;
  
  return cursal;
}

/* If the current source file for listing is not set, try and get a default.
   Usually called before get_current_source_symtab_and_line() is called.
   It may err out if a default cannot be determined.
   We must be cautious about where it is called, as it can recurse as the
   process of determining a new default may call the caller!
   Use get_current_source_symtab_and_line only to get whatever
   we have without erroring out or trying to get a default. */
   
void
set_default_source_symtab_and_line (void)
{
  struct symtab_and_line cursal;

  if (!have_full_symbols () && !have_partial_symbols ())
    error ("No symbol table is loaded.  Use the \"file\" command.");

  /* Pull in a current source symtab if necessary */
  if (current_source_symtab == 0)
    select_source_symtab (0);
}

/* Return the current default file for listing and next line to list
   (the returned sal pc and end fields are not valid.)
   and set the current default to whatever is in SAL.
   NOTE: The returned sal pc and end fields are not valid. */
   
struct symtab_and_line
set_current_source_symtab_and_line (const struct symtab_and_line *sal)
{
  struct symtab_and_line cursal;
  
  cursal.symtab = current_source_symtab;
  cursal.line = current_source_line;

  current_source_symtab = sal->symtab;
  current_source_line = sal->line;
  cursal.pc = 0;
  cursal.end = 0;
  
  return cursal;
}

/* Reset any information stored about a default file and line to print. */

void
clear_current_source_symtab_and_line (void)
{
  current_source_symtab = 0;
  current_source_line = 0;
}

/* Set the source file default for the "list" command to be S.

   If S is NULL, and we don't have a default, find one.  This
   should only be called when the user actually tries to use the
   default, since we produce an error if we can't find a reasonable
   default.  Also, since this can cause symbols to be read, doing it
   before we need to would make things slower than necessary.  */

void
select_source_symtab (struct symtab *s)
{
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  struct partial_symtab *ps;
  struct partial_symtab *cs_pst = 0;
  struct objfile *ofp;

  if (s)
    {
      current_source_symtab = s;
      current_source_line = 1;
      return;
    }

  if (current_source_symtab)
    return;

  /* Make the default place to list be the function `main'
     if one exists.  */
  if (lookup_symbol (main_name (), 0, VAR_DOMAIN, 0, NULL))
    {
      sals = decode_line_spec (main_name (), 1);
      sal = sals.sals[0];
      xfree (sals.sals);
      current_source_symtab = sal.symtab;
      current_source_line = max (sal.line - (lines_to_list - 1), 1);
      if (current_source_symtab)
	return;
    }

  /* All right; find the last file in the symtab list (ignoring .h's).  */

  current_source_line = 1;

  for (ofp = object_files; ofp != NULL; ofp = ofp->next)
    {
      for (s = ofp->symtabs; s; s = s->next)
	{
	  char *name = s->filename;
	  int len = strlen (name);
	  if (!(len > 2 && (DEPRECATED_STREQ (&name[len - 2], ".h"))))
	    {
	      current_source_symtab = s;
	    }
	}
    }
  if (current_source_symtab)
    return;

  /* Howabout the partial symbol tables? */

  for (ofp = object_files; ofp != NULL; ofp = ofp->next)
    {
      for (ps = ofp->psymtabs; ps != NULL; ps = ps->next)
	{
	  char *name = ps->filename;
	  int len = strlen (name);
	  if (!(len > 2 && (DEPRECATED_STREQ (&name[len - 2], ".h"))))
	    {
	      cs_pst = ps;
	    }
	}
    }
  if (cs_pst)
    {
      if (cs_pst->readin)
	{
	  internal_error (__FILE__, __LINE__,
			  "select_source_symtab: "
			  "readin pst found and no symtabs.");
	}
      else
	{
	  current_source_symtab = PSYMTAB_TO_SYMTAB (cs_pst);
	}
    }
  if (current_source_symtab)
    return;

  error ("Can't find a default source file");
}

static void
show_directories (char *ignore, int from_tty)
{
  puts_filtered ("Source directories searched: ");
  puts_filtered (source_path);
  puts_filtered ("\n");
}

/* Forget what we learned about line positions in source files, and
   which directories contain them; must check again now since files
   may be found in a different directory now.  */

void
forget_cached_source_info (void)
{
  struct symtab *s;
  struct objfile *objfile;
  struct partial_symtab *pst;

  for (objfile = object_files; objfile != NULL; objfile = objfile->next)
    {
      for (s = objfile->symtabs; s != NULL; s = s->next)
	{
	  if (s->line_charpos != NULL)
	    {
	      xmfree (objfile->md, s->line_charpos);
	      s->line_charpos = NULL;
	    }
	  if (s->fullname != NULL)
	    {
	      xmfree (objfile->md, s->fullname);
	      s->fullname = NULL;
	    }
	}

      ALL_OBJFILE_PSYMTABS (objfile, pst)
      {
	if (pst->fullname != NULL)
	  {
	    xfree (pst->fullname);
	    pst->fullname = NULL;
	  }
      }
    }
}

void
init_source_path (void)
{
  char buf[20];

  sprintf (buf, "$cdir%c$cwd", DIRNAME_SEPARATOR);
  source_path = xstrdup (buf);
  forget_cached_source_info ();
}

void
init_last_source_visited (void)
{
  last_source_visited = NULL;
}

/* Add zero or more directories to the front of the source path.  */

void
directory_command (char *dirname, int from_tty)
{
  dont_repeat ();
  /* FIXME, this goes to "delete dir"... */
  if (dirname == 0)
    {
      if (from_tty && query ("Reinitialize source path to empty? "))
	{
	  xfree (source_path);
	  init_source_path ();
	}
    }
  else
    {
      mod_path (dirname, &source_path);
      last_source_visited = NULL;
    }
  if (from_tty)
    show_directories ((char *) 0, from_tty);
  forget_cached_source_info ();
}

/* Add zero or more directories to the front of an arbitrary path.  */

void
mod_path (char *dirname, char **which_path)
{
  add_path (dirname, which_path, 1);
}

/* Workhorse of mod_path.  Takes an extra argument to determine
   if dirname should be parsed for separators that indicate multiple
   directories.  This allows for interfaces that pre-parse the dirname
   and allow specification of traditional separator characters such
   as space or tab. */

void
add_path (char *dirname, char **which_path, int parse_separators)
{
  char *old = *which_path;
  int prefix = 0;

  if (dirname == 0)
    return;

  dirname = xstrdup (dirname);
  make_cleanup (xfree, dirname);

  do
    {
      char *name = dirname;
      char *p;
      struct stat st;

      {
	char *separator = NULL;
	char *space = NULL;
	char *tab = NULL;

	if (parse_separators)
	  {
	    separator = strchr (name, DIRNAME_SEPARATOR);
	    space = strchr (name, ' ');
	    tab = strchr (name, '\t');
	  }

	if (separator == 0 && space == 0 && tab == 0)
	  p = dirname = name + strlen (name);
	else
	  {
	    p = 0;
	    if (separator != 0 && (p == 0 || separator < p))
	      p = separator;
	    if (space != 0 && (p == 0 || space < p))
	      p = space;
	    if (tab != 0 && (p == 0 || tab < p))
	      p = tab;
	    dirname = p + 1;
	    while (*dirname == DIRNAME_SEPARATOR
		   || *dirname == ' '
		   || *dirname == '\t')
	      ++dirname;
	  }
      }

      if (!(IS_DIR_SEPARATOR (*name) && p <= name + 1)	 /* "/" */
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      /* On MS-DOS and MS-Windows, h:\ is different from h: */
	  && !(p == name + 3 && name[1] == ':') 	 /* "d:/" */
#endif
	  && IS_DIR_SEPARATOR (p[-1]))
	/* Sigh. "foo/" => "foo" */
	--p;
      *p = '\0';

      while (p > name && p[-1] == '.')
	{
	  if (p - name == 1)
	    {
	      /* "." => getwd ().  */
	      name = current_directory;
	      goto append;
	    }
	  else if (p > name + 1 && IS_DIR_SEPARATOR (p[-2]))
	    {
	      if (p - name == 2)
		{
		  /* "/." => "/".  */
		  *--p = '\0';
		  goto append;
		}
	      else
		{
		  /* "...foo/." => "...foo".  */
		  p -= 2;
		  *p = '\0';
		  continue;
		}
	    }
	  else
	    break;
	}

      if (name[0] == '~')
	name = tilde_expand (name);
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
      else if (IS_ABSOLUTE_PATH (name) && p == name + 2) /* "d:" => "d:." */
	name = concat (name, ".", NULL);
#endif
      else if (!IS_ABSOLUTE_PATH (name) && name[0] != '$')
	name = concat (current_directory, SLASH_STRING, name, NULL);
      else
	name = savestring (name, p - name);
      make_cleanup (xfree, name);

      /* Unless it's a variable, check existence.  */
      if (name[0] != '$')
	{
	  /* These are warnings, not errors, since we don't want a
	     non-existent directory in a .gdbinit file to stop processing
	     of the .gdbinit file.

	     Whether they get added to the path is more debatable.  Current
	     answer is yes, in case the user wants to go make the directory
	     or whatever.  If the directory continues to not exist/not be
	     a directory/etc, then having them in the path should be
	     harmless.  */
	  if (stat (name, &st) < 0)
	    {
	      int save_errno = errno;
	      fprintf_unfiltered (gdb_stderr, "Warning: ");
	      print_sys_errmsg (name, save_errno);
	    }
	  else if ((st.st_mode & S_IFMT) != S_IFDIR)
	    warning ("%s is not a directory.", name);
	}

    append:
      {
	unsigned int len = strlen (name);

	p = *which_path;
	while (1)
	  {
	    /* FIXME: strncmp loses in interesting ways on MS-DOS and
	       MS-Windows because of case-insensitivity and two different
	       but functionally identical slash characters.  We need a
	       special filesystem-dependent file-name comparison function.

	       Actually, even on Unix I would use realpath() or its work-
	       alike before comparing.  Then all the code above which
	       removes excess slashes and dots could simply go away.  */
	    if (!strncmp (p, name, len)
		&& (p[len] == '\0' || p[len] == DIRNAME_SEPARATOR))
	      {
		/* Found it in the search path, remove old copy */
		if (p > *which_path)
		  p--;		/* Back over leading separator */
		if (prefix > p - *which_path)
		  goto skip_dup;	/* Same dir twice in one cmd */
		strcpy (p, &p[len + 1]);	/* Copy from next \0 or  : */
	      }
	    p = strchr (p, DIRNAME_SEPARATOR);
	    if (p != 0)
	      ++p;
	    else
	      break;
	  }
	if (p == 0)
	  {
	    char tinybuf[2];

	    tinybuf[0] = DIRNAME_SEPARATOR;
	    tinybuf[1] = '\0';

	    /* If we have already tacked on a name(s) in this command, be sure they stay 
	       on the front as we tack on some more.  */
	    if (prefix)
	      {
		char *temp, c;

		c = old[prefix];
		old[prefix] = '\0';
		temp = concat (old, tinybuf, name, NULL);
		old[prefix] = c;
		*which_path = concat (temp, "", &old[prefix], NULL);
		prefix = strlen (temp);
		xfree (temp);
	      }
	    else
	      {
		*which_path = concat (name, (old[0] ? tinybuf : old), old, NULL);
		prefix = strlen (name);
	      }
	    xfree (old);
	    old = *which_path;
	  }
      }
    skip_dup:;
    }
  while (*dirname != '\0');
}


static void
source_info (char *ignore, int from_tty)
{
  struct symtab *s = current_source_symtab;

  if (!s)
    {
      printf_filtered ("No current source file.\n");
      return;
    }
  printf_filtered ("Current source file is %s\n", s->filename);
  if (s->dirname)
    printf_filtered ("Compilation directory is %s\n", s->dirname);
  if (s->fullname)
    printf_filtered ("Located in %s\n", s->fullname);
  if (s->nlines)
    printf_filtered ("Contains %d line%s.\n", s->nlines,
		     s->nlines == 1 ? "" : "s");

  printf_filtered ("Source language is %s.\n", language_str (s->language));
  printf_filtered ("Compiled with %s debugging format.\n", s->debugformat);
  printf_filtered ("%s preprocessor macro info.\n",
                   s->macro_table ? "Includes" : "Does not include");
}


/* Return True if the file NAME exists and is a regular file */
static int
is_regular_file (const char *name)
{
  struct stat st;
  const int status = stat (name, &st);

  /* Stat should never fail except when the file does not exist.
     If stat fails, analyze the source of error and return True
     unless the file does not exist, to avoid returning false results
     on obscure systems where stat does not work as expected.
   */
  if (status != 0)
    return (errno != ENOENT);

  return S_ISREG (st.st_mode);
}

/* Open a file named STRING, searching path PATH (dir names sep by some char)
   using mode MODE and protection bits PROT in the calls to open.

   If TRY_CWD_FIRST, try to open ./STRING before searching PATH.
   (ie pretend the first element of PATH is ".").  This also indicates
   that a slash in STRING disables searching of the path (this is
   so that "exec-file ./foo" or "symbol-file ./foo" insures that you
   get that particular version of foo or an error message).

   If FILENAME_OPENED is non-null, set it to a newly allocated string naming
   the actual file opened (this string will always start with a "/").  We
   have to take special pains to avoid doubling the "/" between the directory
   and the file, sigh!  Emacs gets confuzzed by this when we print the
   source file name!!! 

   If a file is found, return the descriptor.
   Otherwise, return -1, with errno set for the last name we tried to open.  */

/*  >>>> This should only allow files of certain types,
    >>>>  eg executable, non-directory */
int
openp (const char *path, int try_cwd_first, const char *string,
       int mode, int prot,
       char **filename_opened)
{
  int fd;
  char *filename;
  const char *p;
  const char *p1;
  int len;
  int alloclen;

  if (!path)
    path = ".";

#if defined(_WIN32) || defined(__CYGWIN__)
  mode |= O_BINARY;
#endif

  if (try_cwd_first || IS_ABSOLUTE_PATH (string))
    {
      int i;

      if (is_regular_file (string))
	{
	  filename = alloca (strlen (string) + 1);
	  strcpy (filename, string);
	  fd = open (filename, mode, prot);
	  if (fd >= 0)
	    goto done;
	}
      else
	{
	  filename = NULL;
	  fd = -1;
	}

      for (i = 0; string[i]; i++)
	if (IS_DIR_SEPARATOR (string[i]))
	  goto done;
    }

  /* ./foo => foo */
  while (string[0] == '.' && IS_DIR_SEPARATOR (string[1]))
    string += 2;

  alloclen = strlen (path) + strlen (string) + 2;
  filename = alloca (alloclen);
  fd = -1;
  for (p = path; p; p = p1 ? p1 + 1 : 0)
    {
      p1 = strchr (p, DIRNAME_SEPARATOR);
      if (p1)
	len = p1 - p;
      else
	len = strlen (p);

      if (len == 4 && p[0] == '$' && p[1] == 'c'
	  && p[2] == 'w' && p[3] == 'd')
	{
	  /* Name is $cwd -- insert current directory name instead.  */
	  int newlen;

	  /* First, realloc the filename buffer if too short. */
	  len = strlen (current_directory);
	  newlen = len + strlen (string) + 2;
	  if (newlen > alloclen)
	    {
	      alloclen = newlen;
	      filename = alloca (alloclen);
	    }
	  strcpy (filename, current_directory);
	}
      else
	{
	  /* Normal file name in path -- just use it.  */
	  strncpy (filename, p, len);
	  filename[len] = 0;
	}

      /* Remove trailing slashes */
      while (len > 0 && IS_DIR_SEPARATOR (filename[len - 1]))
	filename[--len] = 0;

      strcat (filename + len, SLASH_STRING);
      strcat (filename, string);

      if (is_regular_file (filename))
      {
        fd = open (filename, mode);
        if (fd >= 0)
          break;
      }
    }

done:
  if (filename_opened)
    {
      /* If a file was opened, canonicalize its filename. Use xfullpath
         rather than gdb_realpath to avoid resolving the basename part
         of filenames when the associated file is a symbolic link. This
         fixes a potential inconsistency between the filenames known to
         GDB and the filenames it prints in the annotations.  */
      if (fd < 0)
	*filename_opened = NULL;
      else if (IS_ABSOLUTE_PATH (filename))
	*filename_opened = xfullpath (filename);
      else
	{
	  /* Beware the // my son, the Emacs barfs, the botch that catch... */

	  char *f = concat (current_directory,
           IS_DIR_SEPARATOR (current_directory[strlen (current_directory) - 1])
				     ? "" : SLASH_STRING,
				     filename, NULL);
	  *filename_opened = xfullpath (f);
	  xfree (f);
	}
    }

  return fd;
}


/* This is essentially a convenience, for clients that want the behaviour
   of openp, using source_path, but that really don't want the file to be
   opened but want instead just to know what the full pathname is (as
   qualified against source_path).

   The current working directory is searched first.

   If the file was found, this function returns 1, and FULL_PATHNAME is
   set to the fully-qualified pathname.

   Else, this functions returns 0, and FULL_PATHNAME is set to NULL.
 */
int
source_full_path_of (char *filename, char **full_pathname)
{
  int fd;

  fd = openp (source_path, 1, filename, O_RDONLY, 0, full_pathname);
  if (fd < 0)
    {
      *full_pathname = NULL;
      return 0;
    }

  close (fd);
  return 1;
}


/* Open a source file given a symtab S.  Returns a file descriptor or
   negative number for error.  */

int
open_source_file (struct symtab *s)
{
  char *path = source_path;
  const char *p;
  int result;
  char *fullname;

  /* Quick way out if we already know its full name */
  if (s->fullname)
    {
      result = open (s->fullname, OPEN_MODE);
      if (result >= 0)
	return result;
      /* Didn't work -- free old one, try again. */
      xmfree (s->objfile->md, s->fullname);
      s->fullname = NULL;
    }

  if (s->dirname != NULL)
    {
      /* Replace a path entry of  $cdir  with the compilation directory name */
#define	cdir_len	5
      /* We cast strstr's result in case an ANSIhole has made it const,
         which produces a "required warning" when assigned to a nonconst. */
      p = (char *) strstr (source_path, "$cdir");
      if (p && (p == path || p[-1] == DIRNAME_SEPARATOR)
	  && (p[cdir_len] == DIRNAME_SEPARATOR || p[cdir_len] == '\0'))
	{
	  int len;

	  path = (char *)
	    alloca (strlen (source_path) + 1 + strlen (s->dirname) + 1);
	  len = p - source_path;
	  strncpy (path, source_path, len);	/* Before $cdir */
	  strcpy (path + len, s->dirname);	/* new stuff */
	  strcat (path + len, source_path + len + cdir_len);	/* After $cdir */
	}
    }

  result = openp (path, 0, s->filename, OPEN_MODE, 0, &s->fullname);
  if (result < 0)
    {
      /* Didn't work.  Try using just the basename. */
      p = lbasename (s->filename);
      if (p != s->filename)
	result = openp (path, 0, p, OPEN_MODE, 0, &s->fullname);
    }

  if (result >= 0)
    {
      fullname = s->fullname;
      s->fullname = mstrsave (s->objfile->md, s->fullname);
      xfree (fullname);
    }
  return result;
}

/* Return the path to the source file associated with symtab.  Returns NULL
   if no symtab.  */

char *
symtab_to_filename (struct symtab *s)
{
  int fd;

  if (!s)
    return NULL;

  /* If we've seen the file before, just return fullname. */

  if (s->fullname)
    return s->fullname;

  /* Try opening the file to setup fullname */

  fd = open_source_file (s);
  if (fd < 0)
    return s->filename;		/* File not found.  Just use short name */

  /* Found the file.  Cleanup and return the full name */

  close (fd);
  return s->fullname;
}


/* Create and initialize the table S->line_charpos that records
   the positions of the lines in the source file, which is assumed
   to be open on descriptor DESC.
   All set S->nlines to the number of such lines.  */

void
find_source_lines (struct symtab *s, int desc)
{
  struct stat st;
  char *data, *p, *end;
  int nlines = 0;
  int lines_allocated = 1000;
  int *line_charpos;
  long mtime = 0;
  int size;

  line_charpos = (int *) xmmalloc (s->objfile->md,
				   lines_allocated * sizeof (int));
  if (fstat (desc, &st) < 0)
    perror_with_name (s->filename);

  if (s && s->objfile && s->objfile->obfd)
    mtime = bfd_get_mtime (s->objfile->obfd);
  else if (exec_bfd)
    mtime = bfd_get_mtime (exec_bfd);

  if (mtime && mtime < st.st_mtime)
    {
      warning ("Source file is more recent than executable.\n");
    }

#ifdef LSEEK_NOT_LINEAR
  {
    char c;

    /* Have to read it byte by byte to find out where the chars live */

    line_charpos[0] = lseek (desc, 0, SEEK_CUR);
    nlines = 1;
    while (myread (desc, &c, 1) > 0)
      {
	if (c == '\n')
	  {
	    if (nlines == lines_allocated)
	      {
		lines_allocated *= 2;
		line_charpos =
		  (int *) xmrealloc (s->objfile->md, (char *) line_charpos,
				     sizeof (int) * lines_allocated);
	      }
	    line_charpos[nlines++] = lseek (desc, 0, SEEK_CUR);
	  }
      }
  }
#else /* lseek linear.  */
  {
    struct cleanup *old_cleanups;

    /* st_size might be a large type, but we only support source files whose 
       size fits in an int.  */
    size = (int) st.st_size;

    /* Use malloc, not alloca, because this may be pretty large, and we may
       run into various kinds of limits on stack size.  */
    data = (char *) xmalloc (size);
    old_cleanups = make_cleanup (xfree, data);

    /* Reassign `size' to result of read for systems where \r\n -> \n.  */
    size = myread (desc, data, size);
    if (size < 0)
      perror_with_name (s->filename);
    end = data + size;
    p = data;
    line_charpos[0] = 0;
    nlines = 1;
    while (p != end)
      {
	if (*p++ == '\n'
	/* A newline at the end does not start a new line.  */
	    && p != end)
	  {
	    if (nlines == lines_allocated)
	      {
		lines_allocated *= 2;
		line_charpos =
		  (int *) xmrealloc (s->objfile->md, (char *) line_charpos,
				     sizeof (int) * lines_allocated);
	      }
	    line_charpos[nlines++] = p - data;
	  }
      }
    do_cleanups (old_cleanups);
  }
#endif /* lseek linear.  */
  s->nlines = nlines;
  s->line_charpos =
    (int *) xmrealloc (s->objfile->md, (char *) line_charpos,
		       nlines * sizeof (int));

}

/* Return the character position of a line LINE in symtab S.
   Return 0 if anything is invalid.  */

#if 0				/* Currently unused */

int
source_line_charpos (struct symtab *s, int line)
{
  if (!s)
    return 0;
  if (!s->line_charpos || line <= 0)
    return 0;
  if (line > s->nlines)
    line = s->nlines;
  return s->line_charpos[line - 1];
}

/* Return the line number of character position POS in symtab S.  */

int
source_charpos_line (struct symtab *s, int chr)
{
  int line = 0;
  int *lnp;

  if (s == 0 || s->line_charpos == 0)
    return 0;
  lnp = s->line_charpos;
  /* Files are usually short, so sequential search is Ok */
  while (line < s->nlines && *lnp <= chr)
    {
      line++;
      lnp++;
    }
  if (line >= s->nlines)
    line = s->nlines;
  return line;
}

#endif /* 0 */


/* Get full pathname and line number positions for a symtab.
   Return nonzero if line numbers may have changed.
   Set *FULLNAME to actual name of the file as found by `openp',
   or to 0 if the file is not found.  */

static int
get_filename_and_charpos (struct symtab *s, char **fullname)
{
  int desc, linenums_changed = 0;

  desc = open_source_file (s);
  if (desc < 0)
    {
      if (fullname)
	*fullname = NULL;
      return 0;
    }
  if (fullname)
    *fullname = s->fullname;
  if (s->line_charpos == 0)
    linenums_changed = 1;
  if (linenums_changed)
    find_source_lines (s, desc);
  close (desc);
  return linenums_changed;
}

/* Print text describing the full name of the source file S
   and the line number LINE and its corresponding character position.
   The text starts with two Ctrl-z so that the Emacs-GDB interface
   can easily find it.

   MID_STATEMENT is nonzero if the PC is not at the beginning of that line.

   Return 1 if successful, 0 if could not find the file.  */

int
identify_source_line (struct symtab *s, int line, int mid_statement,
		      CORE_ADDR pc)
{
  if (s->line_charpos == 0)
    get_filename_and_charpos (s, (char **) NULL);
  if (s->fullname == 0)
    return 0;
  if (line > s->nlines)
    /* Don't index off the end of the line_charpos array.  */
    return 0;
  annotate_source (s->fullname, line, s->line_charpos[line - 1],
		   mid_statement, pc);

  current_source_line = line;
  first_line_listed = line;
  last_line_listed = line;
  current_source_symtab = s;
  return 1;
}


/* Print source lines from the file of symtab S,
   starting with line number LINE and stopping before line number STOPLINE. */

static void print_source_lines_base (struct symtab *s, int line, int stopline,
				     int noerror);
static void
print_source_lines_base (struct symtab *s, int line, int stopline, int noerror)
{
  int c;
  int desc;
  FILE *stream;
  int nlines = stopline - line;

  /* Regardless of whether we can open the file, set current_source_symtab. */
  current_source_symtab = s;
  current_source_line = line;
  first_line_listed = line;

  /* If printing of source lines is disabled, just print file and line number */
  if (ui_out_test_flags (uiout, ui_source_list))
    {
      /* Only prints "No such file or directory" once */
      if ((s != last_source_visited) || (!last_source_error))
	{
	  last_source_visited = s;
	  desc = open_source_file (s);
	}
      else
	{
	  desc = last_source_error;
	  noerror = 1;
	}
    }
  else
    {
      desc = -1;
      noerror = 1;
    }

  if (desc < 0)
    {
      last_source_error = desc;

      if (!noerror)
	{
	  char *name = alloca (strlen (s->filename) + 100);
	  sprintf (name, "%d\t%s", line, s->filename);
	  print_sys_errmsg (name, errno);
	}
      else
	ui_out_field_int (uiout, "line", line);
      ui_out_text (uiout, "\tin ");
      ui_out_field_string (uiout, "file", s->filename);
      ui_out_text (uiout, "\n");

      return;
    }

  last_source_error = 0;

  if (s->line_charpos == 0)
    find_source_lines (s, desc);

  if (line < 1 || line > s->nlines)
    {
      close (desc);
      error ("Line number %d out of range; %s has %d lines.",
	     line, s->filename, s->nlines);
    }

  if (lseek (desc, s->line_charpos[line - 1], 0) < 0)
    {
      close (desc);
      perror_with_name (s->filename);
    }

  stream = fdopen (desc, FDOPEN_MODE);
  clearerr (stream);

  while (nlines-- > 0)
    {
      char buf[20];

      c = fgetc (stream);
      if (c == EOF)
	break;
      last_line_listed = current_source_line;
      sprintf (buf, "%d\t", current_source_line++);
      ui_out_text (uiout, buf);
      do
	{
	  if (c < 040 && c != '\t' && c != '\n' && c != '\r')
	    {
	      sprintf (buf, "^%c", c + 0100);
	      ui_out_text (uiout, buf);
	    }
	  else if (c == 0177)
	    ui_out_text (uiout, "^?");
#ifdef CRLF_SOURCE_FILES
	  else if (c == '\r')
	    {
	      /* Skip a \r character, but only before a \n.  */
	      int c1 = fgetc (stream);

	      if (c1 != '\n')
		printf_filtered ("^%c", c + 0100);
	      if (c1 != EOF)
		ungetc (c1, stream);
	    }
#endif
	  else
	    {
	      sprintf (buf, "%c", c);
	      ui_out_text (uiout, buf);
	    }
	}
      while (c != '\n' && (c = fgetc (stream)) >= 0);
    }

  fclose (stream);
}

/* Show source lines from the file of symtab S, starting with line
   number LINE and stopping before line number STOPLINE.  If this is the
   not the command line version, then the source is shown in the source
   window otherwise it is simply printed */

void
print_source_lines (struct symtab *s, int line, int stopline, int noerror)
{
  print_source_lines_base (s, line, stopline, noerror);
}

/* Print info on range of pc's in a specified line.  */

static void
line_info (char *arg, int from_tty)
{
  struct symtabs_and_lines sals;
  struct symtab_and_line sal;
  CORE_ADDR start_pc, end_pc;
  int i;

  init_sal (&sal);		/* initialize to zeroes */

  if (arg == 0)
    {
      sal.symtab = current_source_symtab;
      sal.line = last_line_listed;
      sals.nelts = 1;
      sals.sals = (struct symtab_and_line *)
	xmalloc (sizeof (struct symtab_and_line));
      sals.sals[0] = sal;
    }
  else
    {
      sals = decode_line_spec_1 (arg, 0);

      dont_repeat ();
    }

  /* C++  More than one line may have been specified, as when the user
     specifies an overloaded function name. Print info on them all. */
  for (i = 0; i < sals.nelts; i++)
    {
      sal = sals.sals[i];

      if (sal.symtab == 0)
	{
	  printf_filtered ("No line number information available");
	  if (sal.pc != 0)
	    {
	      /* This is useful for "info line *0x7f34".  If we can't tell the
	         user about a source line, at least let them have the symbolic
	         address.  */
	      printf_filtered (" for address ");
	      wrap_here ("  ");
	      print_address (sal.pc, gdb_stdout);
	    }
	  else
	    printf_filtered (".");
	  printf_filtered ("\n");
	}
      else if (sal.line > 0
	       && find_line_pc_range (sal, &start_pc, &end_pc))
	{
	  if (start_pc == end_pc)
	    {
	      printf_filtered ("Line %d of \"%s\"",
			       sal.line, sal.symtab->filename);
	      wrap_here ("  ");
	      printf_filtered (" is at address ");
	      print_address (start_pc, gdb_stdout);
	      wrap_here ("  ");
	      printf_filtered (" but contains no code.\n");
	    }
	  else
	    {
	      printf_filtered ("Line %d of \"%s\"",
			       sal.line, sal.symtab->filename);
	      wrap_here ("  ");
	      printf_filtered (" starts at address ");
	      print_address (start_pc, gdb_stdout);
	      wrap_here ("  ");
	      printf_filtered (" and ends at ");
	      print_address (end_pc, gdb_stdout);
	      printf_filtered (".\n");
	    }

	  /* x/i should display this line's code.  */
	  set_next_address (start_pc);

	  /* Repeating "info line" should do the following line.  */
	  last_line_listed = sal.line + 1;

	  /* If this is the only line, show the source code.  If it could
	     not find the file, don't do anything special.  */
	  if (annotation_level && sals.nelts == 1)
	    identify_source_line (sal.symtab, sal.line, 0, start_pc);
	}
      else
	/* Is there any case in which we get here, and have an address
	   which the user would want to see?  If we have debugging symbols
	   and no line numbers?  */
	printf_filtered ("Line number %d is out of range for \"%s\".\n",
			 sal.line, sal.symtab->filename);
    }
  xfree (sals.sals);
}

/* Commands to search the source file for a regexp.  */

static void
forward_search_command (char *regex, int from_tty)
{
  int c;
  int desc;
  FILE *stream;
  int line;
  char *msg;

  line = last_line_listed + 1;

  msg = (char *) re_comp (regex);
  if (msg)
    error ("%s", msg);

  if (current_source_symtab == 0)
    select_source_symtab (0);

  desc = open_source_file (current_source_symtab);
  if (desc < 0)
    perror_with_name (current_source_symtab->filename);

  if (current_source_symtab->line_charpos == 0)
    find_source_lines (current_source_symtab, desc);

  if (line < 1 || line > current_source_symtab->nlines)
    {
      close (desc);
      error ("Expression not found");
    }

  if (lseek (desc, current_source_symtab->line_charpos[line - 1], 0) < 0)
    {
      close (desc);
      perror_with_name (current_source_symtab->filename);
    }

  stream = fdopen (desc, FDOPEN_MODE);
  clearerr (stream);
  while (1)
    {
      static char *buf = NULL;
      char *p;
      int cursize, newsize;

      cursize = 256;
      buf = xmalloc (cursize);
      p = buf;

      c = getc (stream);
      if (c == EOF)
	break;
      do
	{
	  *p++ = c;
	  if (p - buf == cursize)
	    {
	      newsize = cursize + cursize / 2;
	      buf = xrealloc (buf, newsize);
	      p = buf + cursize;
	      cursize = newsize;
	    }
	}
      while (c != '\n' && (c = getc (stream)) >= 0);

#ifdef CRLF_SOURCE_FILES
      /* Remove the \r, if any, at the end of the line, otherwise
         regular expressions that end with $ or \n won't work.  */
      if (p - buf > 1 && p[-2] == '\r')
	{
	  p--;
	  p[-1] = '\n';
	}
#endif

      /* we now have a source line in buf, null terminate and match */
      *p = 0;
      if (re_exec (buf) > 0)
	{
	  /* Match! */
	  fclose (stream);
	  print_source_lines (current_source_symtab, line, line + 1, 0);
	  set_internalvar (lookup_internalvar ("_"),
			   value_from_longest (builtin_type_int,
					       (LONGEST) line));
	  current_source_line = max (line - lines_to_list / 2, 1);
	  return;
	}
      line++;
    }

  printf_filtered ("Expression not found\n");
  fclose (stream);
}

static void
reverse_search_command (char *regex, int from_tty)
{
  int c;
  int desc;
  FILE *stream;
  int line;
  char *msg;

  line = last_line_listed - 1;

  msg = (char *) re_comp (regex);
  if (msg)
    error ("%s", msg);

  if (current_source_symtab == 0)
    select_source_symtab (0);

  desc = open_source_file (current_source_symtab);
  if (desc < 0)
    perror_with_name (current_source_symtab->filename);

  if (current_source_symtab->line_charpos == 0)
    find_source_lines (current_source_symtab, desc);

  if (line < 1 || line > current_source_symtab->nlines)
    {
      close (desc);
      error ("Expression not found");
    }

  if (lseek (desc, current_source_symtab->line_charpos[line - 1], 0) < 0)
    {
      close (desc);
      perror_with_name (current_source_symtab->filename);
    }

  stream = fdopen (desc, FDOPEN_MODE);
  clearerr (stream);
  while (line > 1)
    {
/* FIXME!!!  We walk right off the end of buf if we get a long line!!! */
      char buf[4096];		/* Should be reasonable??? */
      char *p = buf;

      c = getc (stream);
      if (c == EOF)
	break;
      do
	{
	  *p++ = c;
	}
      while (c != '\n' && (c = getc (stream)) >= 0);

#ifdef CRLF_SOURCE_FILES
      /* Remove the \r, if any, at the end of the line, otherwise
         regular expressions that end with $ or \n won't work.  */
      if (p - buf > 1 && p[-2] == '\r')
	{
	  p--;
	  p[-1] = '\n';
	}
#endif

      /* We now have a source line in buf; null terminate and match.  */
      *p = 0;
      if (re_exec (buf) > 0)
	{
	  /* Match! */
	  fclose (stream);
	  print_source_lines (current_source_symtab, line, line + 1, 0);
	  set_internalvar (lookup_internalvar ("_"),
			   value_from_longest (builtin_type_int,
					       (LONGEST) line));
	  current_source_line = max (line - lines_to_list / 2, 1);
	  return;
	}
      line--;
      if (fseek (stream, current_source_symtab->line_charpos[line - 1], 0) < 0)
	{
	  fclose (stream);
	  perror_with_name (current_source_symtab->filename);
	}
    }

  printf_filtered ("Expression not found\n");
  fclose (stream);
  return;
}

void
_initialize_source (void)
{
  struct cmd_list_element *c;
  current_source_symtab = 0;
  init_source_path ();

  /* The intention is to use POSIX Basic Regular Expressions.
     Always use the GNU regex routine for consistency across all hosts.
     Our current GNU regex.c does not have all the POSIX features, so this is
     just an approximation.  */
  re_set_syntax (RE_SYNTAX_GREP);

  c = add_cmd ("directory", class_files, directory_command,
	       "Add directory DIR to beginning of search path for source files.\n\
Forget cached info on source file locations and line positions.\n\
DIR can also be $cwd for the current working directory, or $cdir for the\n\
directory in which the source file was compiled into object code.\n\
With no argument, reset the search path to $cdir:$cwd, the default.",
	       &cmdlist);

  if (dbx_commands)
    add_com_alias ("use", "directory", class_files, 0);

  set_cmd_completer (c, filename_completer);

  add_cmd ("directories", no_class, show_directories,
	   "Current search path for finding source files.\n\
$cwd in the path means the current working directory.\n\
$cdir in the path means the compilation directory of the source file.",
	   &showlist);

  if (xdb_commands)
    {
      add_com_alias ("D", "directory", class_files, 0);
      add_cmd ("ld", no_class, show_directories,
	       "Current search path for finding source files.\n\
$cwd in the path means the current working directory.\n\
$cdir in the path means the compilation directory of the source file.",
	       &cmdlist);
    }

  add_info ("source", source_info,
	    "Information about the current source file.");

  add_info ("line", line_info,
	    concat ("Core addresses of the code for a source line.\n\
Line can be specified as\n\
  LINENUM, to list around that line in current file,\n\
  FILE:LINENUM, to list around that line in that file,\n\
  FUNCTION, to list around beginning of that function,\n\
  FILE:FUNCTION, to distinguish among like-named static functions.\n\
", "\
Default is to describe the last source line that was listed.\n\n\
This sets the default address for \"x\" to the line's first instruction\n\
so that \"x/i\" suffices to start examining the machine code.\n\
The address is also stored as the value of \"$_\".", NULL));

  add_com ("forward-search", class_files, forward_search_command,
	   "Search for regular expression (see regex(3)) from last line listed.\n\
The matching line number is also stored as the value of \"$_\".");
  add_com_alias ("search", "forward-search", class_files, 0);

  add_com ("reverse-search", class_files, reverse_search_command,
	   "Search backward for regular expression (see regex(3)) from last line listed.\n\
The matching line number is also stored as the value of \"$_\".");

  if (xdb_commands)
    {
      add_com_alias ("/", "forward-search", class_files, 0);
      add_com_alias ("?", "reverse-search", class_files, 0);
    }

  add_show_from_set
    (add_set_cmd ("listsize", class_support, var_uinteger,
		  (char *) &lines_to_list,
		  "Set number of source lines gdb will list by default.",
		  &setlist),
     &showlist);
}
