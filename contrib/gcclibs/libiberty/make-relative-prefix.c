/* Relative (relocatable) prefix support.
   Copyright (C) 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002 Free Software Foundation, Inc.

This file is part of libiberty.

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
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.  */

/*

@deftypefn Extension {const char*} make_relative_prefix (const char *@var{progname}, const char *@var{bin_prefix}, const char *@var{prefix})

Given three paths @var{progname}, @var{bin_prefix}, @var{prefix},
return the path that is in the same position relative to
@var{progname}'s directory as @var{prefix} is relative to
@var{bin_prefix}.  That is, a string starting with the directory
portion of @var{progname}, followed by a relative pathname of the
difference between @var{bin_prefix} and @var{prefix}.

If @var{progname} does not contain any directory separators,
@code{make_relative_prefix} will search @env{PATH} to find a program
named @var{progname}.  Also, if @var{progname} is a symbolic link,
the symbolic link will be resolved.

For example, if @var{bin_prefix} is @code{/alpha/beta/gamma/gcc/delta},
@var{prefix} is @code{/alpha/beta/gamma/omega/}, and @var{progname} is
@code{/red/green/blue/gcc}, then this function will return
@code{/red/green/blue/../../omega/}.

The return value is normally allocated via @code{malloc}.  If no
relative prefix can be found, return @code{NULL}.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <string.h>

#include "ansidecl.h"
#include "libiberty.h"

#ifndef R_OK
#define R_OK 4
#define W_OK 2
#define X_OK 1
#endif

#ifndef DIR_SEPARATOR
#  define DIR_SEPARATOR '/'
#endif

#if defined (_WIN32) || defined (__MSDOS__) \
    || defined (__DJGPP__) || defined (__OS2__)
#  define HAVE_DOS_BASED_FILE_SYSTEM
#  define HAVE_HOST_EXECUTABLE_SUFFIX
#  define HOST_EXECUTABLE_SUFFIX ".exe"
#  ifndef DIR_SEPARATOR_2 
#    define DIR_SEPARATOR_2 '\\'
#  endif
#  define PATH_SEPARATOR ';'
#else
#  define PATH_SEPARATOR ':'
#endif

#ifndef DIR_SEPARATOR_2
#  define IS_DIR_SEPARATOR(ch) ((ch) == DIR_SEPARATOR)
#else
#  define IS_DIR_SEPARATOR(ch) \
	(((ch) == DIR_SEPARATOR) || ((ch) == DIR_SEPARATOR_2))
#endif

#define DIR_UP ".."

static char *save_string (const char *, int);
static char **split_directories	(const char *, int *);
static void free_split_directories (char **);

static char *
save_string (const char *s, int len)
{
  char *result = (char *) malloc (len + 1);

  memcpy (result, s, len);
  result[len] = 0;
  return result;
}

/* Split a filename into component directories.  */

static char **
split_directories (const char *name, int *ptr_num_dirs)
{
  int num_dirs = 0;
  char **dirs;
  const char *p, *q;
  int ch;

  /* Count the number of directories.  Special case MSDOS disk names as part
     of the initial directory.  */
  p = name;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  if (name[1] == ':' && IS_DIR_SEPARATOR (name[2]))
    {
      p += 3;
      num_dirs++;
    }
#endif /* HAVE_DOS_BASED_FILE_SYSTEM */

  while ((ch = *p++) != '\0')
    {
      if (IS_DIR_SEPARATOR (ch))
	{
	  num_dirs++;
	  while (IS_DIR_SEPARATOR (*p))
	    p++;
	}
    }

  dirs = (char **) malloc (sizeof (char *) * (num_dirs + 2));
  if (dirs == NULL)
    return NULL;

  /* Now copy the directory parts.  */
  num_dirs = 0;
  p = name;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  if (name[1] == ':' && IS_DIR_SEPARATOR (name[2]))
    {
      dirs[num_dirs++] = save_string (p, 3);
      if (dirs[num_dirs - 1] == NULL)
	{
	  free (dirs);
	  return NULL;
	}
      p += 3;
    }
#endif /* HAVE_DOS_BASED_FILE_SYSTEM */

  q = p;
  while ((ch = *p++) != '\0')
    {
      if (IS_DIR_SEPARATOR (ch))
	{
	  while (IS_DIR_SEPARATOR (*p))
	    p++;

	  dirs[num_dirs++] = save_string (q, p - q);
	  if (dirs[num_dirs - 1] == NULL)
	    {
	      dirs[num_dirs] = NULL;
	      free_split_directories (dirs);
	      return NULL;
	    }
	  q = p;
	}
    }

  if (p - 1 - q > 0)
    dirs[num_dirs++] = save_string (q, p - 1 - q);
  dirs[num_dirs] = NULL;

  if (dirs[num_dirs - 1] == NULL)
    {
      free_split_directories (dirs);
      return NULL;
    }

  if (ptr_num_dirs)
    *ptr_num_dirs = num_dirs;
  return dirs;
}

/* Release storage held by split directories.  */

static void
free_split_directories (char **dirs)
{
  int i = 0;

  while (dirs[i] != NULL)
    free (dirs[i++]);

  free ((char *) dirs);
}

/* Given three strings PROGNAME, BIN_PREFIX, PREFIX, return a string that gets
   to PREFIX starting with the directory portion of PROGNAME and a relative
   pathname of the difference between BIN_PREFIX and PREFIX.

   For example, if BIN_PREFIX is /alpha/beta/gamma/gcc/delta, PREFIX is
   /alpha/beta/gamma/omega/, and PROGNAME is /red/green/blue/gcc, then this
   function will return /red/green/blue/../../omega/.

   If no relative prefix can be found, return NULL.  */

char *
make_relative_prefix (const char *progname,
                      const char *bin_prefix, const char *prefix)
{
  char **prog_dirs, **bin_dirs, **prefix_dirs;
  int prog_num, bin_num, prefix_num;
  int i, n, common;
  int needed_len;
  char *ret, *ptr, *full_progname = NULL;

  if (progname == NULL || bin_prefix == NULL || prefix == NULL)
    return NULL;

  /* If there is no full pathname, try to find the program by checking in each
     of the directories specified in the PATH environment variable.  */
  if (lbasename (progname) == progname)
    {
      char *temp;

      temp = getenv ("PATH");
      if (temp)
	{
	  char *startp, *endp, *nstore;
	  size_t prefixlen = strlen (temp) + 1;
	  if (prefixlen < 2)
	    prefixlen = 2;

	  nstore = (char *) alloca (prefixlen + strlen (progname) + 1);

	  startp = endp = temp;
	  while (1)
	    {
	      if (*endp == PATH_SEPARATOR || *endp == 0)
		{
		  if (endp == startp)
		    {
		      nstore[0] = '.';
		      nstore[1] = DIR_SEPARATOR;
		      nstore[2] = '\0';
		    }
		  else
		    {
		      strncpy (nstore, startp, endp - startp);
		      if (! IS_DIR_SEPARATOR (endp[-1]))
			{
			  nstore[endp - startp] = DIR_SEPARATOR;
			  nstore[endp - startp + 1] = 0;
			}
		      else
			nstore[endp - startp] = 0;
		    }
		  strcat (nstore, progname);
		  if (! access (nstore, X_OK)
#ifdef HAVE_HOST_EXECUTABLE_SUFFIX
                      || ! access (strcat (nstore, HOST_EXECUTABLE_SUFFIX), X_OK)
#endif
		      )
		    {
		      progname = nstore;
		      break;
		    }

		  if (*endp == 0)
		    break;
		  endp = startp = endp + 1;
		}
	      else
		endp++;
	    }
	}
    }

  full_progname = lrealpath (progname);
  if (full_progname == NULL)
    return NULL;

  prog_dirs = split_directories (full_progname, &prog_num);
  bin_dirs = split_directories (bin_prefix, &bin_num);
  free (full_progname);
  if (bin_dirs == NULL || prog_dirs == NULL)
    return NULL;

  /* Remove the program name from comparison of directory names.  */
  prog_num--;

  /* If we are still installed in the standard location, we don't need to
     specify relative directories.  Also, if argv[0] still doesn't contain
     any directory specifiers after the search above, then there is not much
     we can do.  */
  if (prog_num == bin_num)
    {
      for (i = 0; i < bin_num; i++)
	{
	  if (strcmp (prog_dirs[i], bin_dirs[i]) != 0)
	    break;
	}

      if (prog_num <= 0 || i == bin_num)
	{
	  free_split_directories (prog_dirs);
	  free_split_directories (bin_dirs);
	  prog_dirs = bin_dirs = (char **) 0;
	  return NULL;
	}
    }

  prefix_dirs = split_directories (prefix, &prefix_num);
  if (prefix_dirs == NULL)
    {
      free_split_directories (prog_dirs);
      free_split_directories (bin_dirs);
      return NULL;
    }

  /* Find how many directories are in common between bin_prefix & prefix.  */
  n = (prefix_num < bin_num) ? prefix_num : bin_num;
  for (common = 0; common < n; common++)
    {
      if (strcmp (bin_dirs[common], prefix_dirs[common]) != 0)
	break;
    }

  /* If there are no common directories, there can be no relative prefix.  */
  if (common == 0)
    {
      free_split_directories (prog_dirs);
      free_split_directories (bin_dirs);
      free_split_directories (prefix_dirs);
      return NULL;
    }

  /* Two passes: first figure out the size of the result string, and
     then construct it.  */
  needed_len = 0;
  for (i = 0; i < prog_num; i++)
    needed_len += strlen (prog_dirs[i]);
  needed_len += sizeof (DIR_UP) * (bin_num - common);
  for (i = common; i < prefix_num; i++)
    needed_len += strlen (prefix_dirs[i]);
  needed_len += 1; /* Trailing NUL.  */

  ret = (char *) malloc (needed_len);
  if (ret == NULL)
    return NULL;

  /* Build up the pathnames in argv[0].  */
  *ret = '\0';
  for (i = 0; i < prog_num; i++)
    strcat (ret, prog_dirs[i]);

  /* Now build up the ..'s.  */
  ptr = ret + strlen(ret);
  for (i = common; i < bin_num; i++)
    {
      strcpy (ptr, DIR_UP);
      ptr += sizeof (DIR_UP) - 1;
      *(ptr++) = DIR_SEPARATOR;
    }
  *ptr = '\0';

  /* Put in directories to move over to prefix.  */
  for (i = common; i < prefix_num; i++)
    strcat (ret, prefix_dirs[i]);

  free_split_directories (prog_dirs);
  free_split_directories (bin_dirs);
  free_split_directories (prefix_dirs);

  return ret;
}
