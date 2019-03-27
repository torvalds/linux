/* exclude.c -- exclude file names

   Copyright (C) 1992, 1993, 1994, 1997, 1999, 2000, 2001, 2002, 2003 Free
   Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Paul Eggert <eggert@twinsun.com>  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdbool.h>

#include <ctype.h>
#include <errno.h>
#ifndef errno
extern int errno;
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exclude.h"
#include "fnmatch.h"
#include "unlocked-io.h"
#include "xalloc.h"

#if STDC_HEADERS || (! defined isascii && ! HAVE_ISASCII)
# define IN_CTYPE_DOMAIN(c) true
#else
# define IN_CTYPE_DOMAIN(c) isascii (c)
#endif

static inline bool
is_space (unsigned char c)
{
  return IN_CTYPE_DOMAIN (c) && isspace (c);
}

/* Verify a requirement at compile-time (unlike assert, which is runtime).  */
#define verify(name, assertion) struct name { char a[(assertion) ? 1 : -1]; }

/* Non-GNU systems lack these options, so we don't need to check them.  */
#ifndef FNM_CASEFOLD
# define FNM_CASEFOLD 0
#endif
#ifndef FNM_LEADING_DIR
# define FNM_LEADING_DIR 0
#endif

verify (EXCLUDE_macros_do_not_collide_with_FNM_macros,
	(((EXCLUDE_ANCHORED | EXCLUDE_INCLUDE | EXCLUDE_WILDCARDS)
	  & (FNM_PATHNAME | FNM_NOESCAPE | FNM_PERIOD | FNM_LEADING_DIR
	     | FNM_CASEFOLD))
	 == 0));

/* An exclude pattern-options pair.  The options are fnmatch options
   ORed with EXCLUDE_* options.  */

struct patopts
  {
    char const *pattern;
    int options;
  };

/* An exclude list, of pattern-options pairs.  */

struct exclude
  {
    struct patopts *exclude;
    size_t exclude_alloc;
    size_t exclude_count;
  };

/* Return a newly allocated and empty exclude list.  */

struct exclude *
new_exclude (void)
{
  return xzalloc (sizeof *new_exclude ());
}

/* Free the storage associated with an exclude list.  */

void
free_exclude (struct exclude *ex)
{
  free (ex->exclude);
  free (ex);
}

/* Return zero if PATTERN matches F, obeying OPTIONS, except that
   (unlike fnmatch) wildcards are disabled in PATTERN.  */

static int
fnmatch_no_wildcards (char const *pattern, char const *f, int options)
{
  if (! (options & FNM_LEADING_DIR))
    return ((options & FNM_CASEFOLD)
	    ? strcasecmp (pattern, f)
	    : strcmp (pattern, f));
  else
    {
      size_t patlen = strlen (pattern);
      int r = ((options & FNM_CASEFOLD)
		? strncasecmp (pattern, f, patlen)
		: strncmp (pattern, f, patlen));
      if (! r)
	{
	  r = f[patlen];
	  if (r == '/')
	    r = 0;
	}
      return r;
    }
}

/* Return true if EX excludes F.  */

bool
excluded_filename (struct exclude const *ex, char const *f)
{
  size_t exclude_count = ex->exclude_count;

  /* If no options are given, the default is to include.  */
  if (exclude_count == 0)
    return false;
  else
    {
      struct patopts const *exclude = ex->exclude;
      size_t i;

      /* Otherwise, the default is the opposite of the first option.  */
      bool excluded = !! (exclude[0].options & EXCLUDE_INCLUDE);

      /* Scan through the options, seeing whether they change F from
	 excluded to included or vice versa.  */
      for (i = 0;  i < exclude_count;  i++)
	{
	  char const *pattern = exclude[i].pattern;
	  int options = exclude[i].options;
	  if (excluded == !! (options & EXCLUDE_INCLUDE))
	    {
	      int (*matcher) (char const *, char const *, int) =
		(options & EXCLUDE_WILDCARDS
		 ? fnmatch
		 : fnmatch_no_wildcards);
	      bool matched = ((*matcher) (pattern, f, options) == 0);
	      char const *p;

	      if (! (options & EXCLUDE_ANCHORED))
		for (p = f; *p && ! matched; p++)
		  if (*p == '/' && p[1] != '/')
		    matched = ((*matcher) (pattern, p + 1, options) == 0);

	      excluded ^= matched;
	    }
	}

      return excluded;
    }
}

/* Append to EX the exclusion PATTERN with OPTIONS.  */

void
add_exclude (struct exclude *ex, char const *pattern, int options)
{
  struct patopts *patopts;

  if (ex->exclude_count == ex->exclude_alloc)
    ex->exclude = x2nrealloc (ex->exclude, &ex->exclude_alloc,
			      sizeof *ex->exclude);

  patopts = &ex->exclude[ex->exclude_count++];
  patopts->pattern = pattern;
  patopts->options = options;
}

/* Use ADD_FUNC to append to EX the patterns in FILENAME, each with
   OPTIONS.  LINE_END terminates each pattern in the file.  If
   LINE_END is a space character, ignore trailing spaces and empty
   lines in FILE.  Return -1 on failure, 0 on success.  */

int
add_exclude_file (void (*add_func) (struct exclude *, char const *, int),
		  struct exclude *ex, char const *filename, int options,
		  char line_end)
{
  bool use_stdin = filename[0] == '-' && !filename[1];
  FILE *in;
  char *buf = NULL;
  char *p;
  char const *pattern;
  char const *lim;
  size_t buf_alloc = 0;
  size_t buf_count = 0;
  int c;
  int e = 0;

  if (use_stdin)
    in = stdin;
  else if (! (in = fopen (filename, "r")))
    return -1;

  while ((c = getc (in)) != EOF)
    {
      if (buf_count == buf_alloc)
	buf = x2realloc (buf, &buf_alloc);
      buf[buf_count++] = c;
    }

  if (ferror (in))
    e = errno;

  if (!use_stdin && fclose (in) != 0)
    e = errno;

  buf = xrealloc (buf, buf_count + 1);
  buf[buf_count] = line_end;
  lim = buf + buf_count + ! (buf_count == 0 || buf[buf_count - 1] == line_end);
  pattern = buf;

  for (p = buf; p < lim; p++)
    if (*p == line_end)
      {
	char *pattern_end = p;

	if (is_space (line_end))
	  {
	    for (; ; pattern_end--)
	      if (pattern_end == pattern)
		goto next_pattern;
	      else if (! is_space (pattern_end[-1]))
		break;
	  }

	*pattern_end = '\0';
	(*add_func) (ex, pattern, options);

      next_pattern:
	pattern = p + 1;
      }

  errno = e;
  return e ? -1 : 0;
}
