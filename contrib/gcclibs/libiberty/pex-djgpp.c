/* Utilities to execute a program in a subprocess (possibly linked by pipes
   with other subprocesses), and wait for it.  DJGPP specialization.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2005
   Free Software Foundation, Inc.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "pex-common.h"

#include <stdio.h>
#include <errno.h>
#ifdef NEED_DECLARATION_ERRNO
extern int errno;
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <process.h>

/* Use ECHILD if available, otherwise use EINVAL.  */
#ifdef ECHILD
#define PWAIT_ERROR ECHILD
#else
#define PWAIT_ERROR EINVAL
#endif

static int pex_djgpp_open_read (struct pex_obj *, const char *, int);
static int pex_djgpp_open_write (struct pex_obj *, const char *, int);
static long pex_djgpp_exec_child (struct pex_obj *, int, const char *,
				  char * const *, char * const *,
				  int, int, int, int,
				  const char **, int *);
static int pex_djgpp_close (struct pex_obj *, int);
static int pex_djgpp_wait (struct pex_obj *, long, int *, struct pex_time *,
			   int, const char **, int *);

/* The list of functions we pass to the common routines.  */

const struct pex_funcs funcs =
{
  pex_djgpp_open_read,
  pex_djgpp_open_write,
  pex_djgpp_exec_child,
  pex_djgpp_close,
  pex_djgpp_wait,
  NULL, /* pipe */
  NULL, /* fdopenr */
  NULL, /* fdopenw */
  NULL  /* cleanup */
};

/* Return a newly initialized pex_obj structure.  */

struct pex_obj *
pex_init (int flags, const char *pname, const char *tempbase)
{
  /* DJGPP does not support pipes.  */
  flags &= ~ PEX_USE_PIPES;
  return pex_init_common (flags, pname, tempbase, &funcs);
}

/* Open a file for reading.  */

static int
pex_djgpp_open_read (struct pex_obj *obj ATTRIBUTE_UNUSED,
		     const char *name, int binary)
{
  return open (name, O_RDONLY | (binary ? O_BINARY : O_TEXT));
}

/* Open a file for writing.  */

static int
pex_djgpp_open_write (struct pex_obj *obj ATTRIBUTE_UNUSED,
		      const char *name, int binary)
{
  /* Note that we can't use O_EXCL here because gcc may have already
     created the temporary file via make_temp_file.  */
  return open (name,
	       (O_WRONLY | O_CREAT | O_TRUNC
		| (binary ? O_BINARY : O_TEXT)),
	       S_IRUSR | S_IWUSR);
}

/* Close a file.  */

static int
pex_djgpp_close (struct pex_obj *obj ATTRIBUTE_UNUSED, int fd)
{
  return close (fd);
}

/* Execute a child.  */

static long
pex_djgpp_exec_child (struct pex_obj *obj, int flags, const char *executable,
		      char * const * argv, char * const * env,
                      int in, int out, int errdes,
		      int toclose ATTRIBUTE_UNUSED, const char **errmsg,
		      int *err)
{
  int org_in, org_out, org_errdes;
  int status;
  int *statuses;

  org_in = -1;
  org_out = -1;
  org_errdes = -1;

  if (in != STDIN_FILE_NO)
    {
      org_in = dup (STDIN_FILE_NO);
      if (org_in < 0)
	{
	  *err = errno;
	  *errmsg = "dup";
	  return -1;
	}
      if (dup2 (in, STDIN_FILE_NO) < 0)
	{
	  *err = errno;
	  *errmsg = "dup2";
	  return -1;
	}
      if (close (in) < 0)
	{
	  *err = errno;
	  *errmsg = "close";
	  return -1;
	}
    }

  if (out != STDOUT_FILE_NO)
    {
      org_out = dup (STDOUT_FILE_NO);
      if (org_out < 0)
	{
	  *err = errno;
	  *errmsg = "dup";
	  return -1;
	}
      if (dup2 (out, STDOUT_FILE_NO) < 0)
	{
	  *err = errno;
	  *errmsg = "dup2";
	  return -1;
	}
      if (close (out) < 0)
	{
	  *err = errno;
	  *errmsg = "close";
	  return -1;
	}
    }

  if (errdes != STDERR_FILE_NO
      || (flags & PEX_STDERR_TO_STDOUT) != 0)
    {
      org_errdes = dup (STDERR_FILE_NO);
      if (org_errdes < 0)
	{
	  *err = errno;
	  *errmsg = "dup";
	  return -1;
	}
      if (dup2 ((flags & PEX_STDERR_TO_STDOUT) != 0 ? STDOUT_FILE_NO : errdes,
		 STDERR_FILE_NO) < 0)
	{
	  *err = errno;
	  *errmsg = "dup2";
	  return -1;
	}
      if (errdes != STDERR_FILE_NO)
	{
	  if (close (errdes) < 0)
	    {
	      *err = errno;
	      *errmsg = "close";
	      return -1;
	    }
	}
    }

  if (env)
    status = (((flags & PEX_SEARCH) != 0 ? spawnvpe : spawnve)
	      (P_WAIT, executable, argv, env));
  else
    status = (((flags & PEX_SEARCH) != 0 ? spawnvp : spawnv)
  	      (P_WAIT, executable, argv));

  if (status == -1)
    {
      *err = errno;
      *errmsg = ((flags & PEX_SEARCH) != 0) ? "spawnvp" : "spawnv";
    }

  if (in != STDIN_FILE_NO)
    {
      if (dup2 (org_in, STDIN_FILE_NO) < 0)
	{
	  *err = errno;
	  *errmsg = "dup2";
	  return -1;
	}
      if (close (org_in) < 0)
	{
	  *err = errno;
	  *errmsg = "close";
	  return -1;
	}
    }

  if (out != STDOUT_FILE_NO)
    {
      if (dup2 (org_out, STDOUT_FILE_NO) < 0)
	{
	  *err = errno;
	  *errmsg = "dup2";
	  return -1;
	}
      if (close (org_out) < 0)
	{
	  *err = errno;
	  *errmsg = "close";
	  return -1;
	}
    }

  if (errdes != STDERR_FILE_NO
      || (flags & PEX_STDERR_TO_STDOUT) != 0)
    {
      if (dup2 (org_errdes, STDERR_FILE_NO) < 0)
	{
	  *err = errno;
	  *errmsg = "dup2";
	  return -1;
	}
      if (close (org_errdes) < 0)
	{
	  *err = errno;
	  *errmsg = "close";
	  return -1;
	}
    }

  /* Save the exit status for later.  When we are called, obj->count
     is the number of children which have executed before this
     one.  */
  statuses = (int *) obj->sysdep;
  statuses = XRESIZEVEC (int, statuses, obj->count + 1);
  statuses[obj->count] = status;
  obj->sysdep = (void *) statuses;

  return obj->count;
}

/* Wait for a child process to complete.  Actually the child process
   has already completed, and we just need to return the exit
   status.  */

static int
pex_djgpp_wait (struct pex_obj *obj, long pid, int *status,
		struct pex_time *time, int done ATTRIBUTE_UNUSED,
		const char **errmsg ATTRIBUTE_UNUSED,
		int *err ATTRIBUTE_UNUSED)
{
  int *statuses;

  if (time != NULL)
    memset (time, 0, sizeof *time);

  statuses = (int *) obj->sysdep;
  *status = statuses[pid];

  return 0;
}
