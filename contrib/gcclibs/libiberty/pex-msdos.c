/* Utilities to execute a program in a subprocess (possibly linked by pipes
   with other subprocesses), and wait for it.  Generic MSDOS specialization.
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
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "safe-ctype.h"
#include <process.h>

/* The structure we keep in obj->sysdep.  */

#define PEX_MSDOS_FILE_COUNT 3

#define PEX_MSDOS_FD_OFFSET 10

struct pex_msdos
{
  /* An array of file names.  We refer to these using file descriptors
     of 10 + array index.  */
  const char *files[PEX_MSDOS_FILE_COUNT];
  /* Exit statuses of programs which have been run.  */
  int *statuses;
};

static int pex_msdos_open (struct pex_obj *, const char *, int);
static int pex_msdos_open (struct pex_obj *, const char *, int);
static int pex_msdos_fdindex (struct pex_msdos *, int);
static long pex_msdos_exec_child (struct pex_obj *, int, const char *,
				  char * const *, char * const *,
				  int, int, int, int,
				  int, const char **, int *);
static int pex_msdos_close (struct pex_obj *, int);
static int pex_msdos_wait (struct pex_obj *, long, int *, struct pex_time *,
			   int, const char **, int *);
static void pex_msdos_cleanup (struct pex_obj *);

/* The list of functions we pass to the common routines.  */

const struct pex_funcs funcs =
{
  pex_msdos_open,
  pex_msdos_open,
  pex_msdos_exec_child,
  pex_msdos_close,
  pex_msdos_wait,
  NULL, /* pipe */
  NULL, /* fdopenr */
  NULL, /* fdopenw */
  pex_msdos_cleanup
};

/* Return a newly initialized pex_obj structure.  */

struct pex_obj *
pex_init (int flags, const char *pname, const char *tempbase)
{
  struct pex_obj *ret;
  int i;

  /* MSDOS does not support pipes.  */
  flags &= ~ PEX_USE_PIPES;

  ret = pex_init_common (flags, pname, tempbase, funcs);

  ret->sysdep = XNEW (struct pex_msdos);
  for (i = 0; i < PEX_MSDOS_FILE_COUNT; ++i)
    ret->files[i] = NULL;
  ret->statuses = NULL;

  return ret;
}

/* Open a file.  FIXME: We ignore the binary argument, since we have
   no way to handle it.  */

static int
pex_msdos_open (struct pex_obj *obj, const char *name,
		int binary ATTRIBUTE_UNUSED)
{
  struct pex_msdos *ms;
  int i;

  ms = (struct pex_msdos *) obj->sysdep;

  for (i = 0; i < PEX_MSDOS_FILE_COUNT; ++i)
    {
      if (ms->files[i] == NULL)
	{
	  ms->files[i] = xstrdup (name);
	  return i + PEX_MSDOS_FD_OFFSET;
	}
    }

  abort ();
}

/* Get the index into msdos->files associated with an open file
   descriptor.  */

static int
pex_msdos_fdindex (struct pex_msdos *ms, int fd)
{
  fd -= PEX_MSDOS_FD_OFFSET;
  if (fd < 0 || fd >= PEX_MSDOS_FILE_COUNT || ms->files[fd] == NULL)
    abort ();
  return fd;
}


/* Close a file.  */

static int
pex_msdos_close (struct pex_obj *obj, int fd)
{
  struct pex_msdos *ms;
  int fdinex;

  ms = (struct pex_msdos *) obj->sysdep;
  fdindex = pe_msdos_fdindex (ms, fd);
  free (ms->files[fdindex]);
  ms->files[fdindex] = NULL;
}

/* Execute a child.  */

static long
pex_msdos_exec_child (struct pex_obj *obj, int flags, const char *executable,
		      char * const * argv, char * const * env, int in, int out,
		      int toclose ATTRIBUTE_UNUSED,
		      int errdes ATTRIBUTE_UNUSED, const char **errmsg,
		      int *err)
{
  struct pex_msdos *ms;
  char *temp_base;
  int temp_base_allocated;
  char *rf;
  int inindex;
  char *infile;
  int outindex;
  char *outfile;
  char *scmd;
  FILE *argfile;
  int i;
  int status;

  ms = (struct pex_msdos *) obj->sysdep;

  /* FIXME: I don't know how to redirect stderr, so we ignore ERRDES
     and PEX_STDERR_TO_STDOUT.  */

  temp_base = obj->temp_base;
  if (temp_base != NULL)
    temp_base_allocated = 0;
  else
    {
      temp_base = choose_temp_base ();
      temp_base_allocated = 1;
    }

  rf = concat (temp_base, ".gp", NULL);

  if (temp_base_allocated)
    free (temp_base);

  if (in == STDIN_FILE_NO)
    {
      inindex = -1;
      infile = "";
    }
  else
    {
      inindex = pex_msdos_fdindex (ms, in);
      infile = ms->files[inindex];
    }

  if (out == STDOUT_FILE_NO)
    {
      outindex = -1;
      outfile = "";
    }
  else
    {
      outindex = pex_msdos_fdindex (ms, out);
      outfile = ms->files[outindex];
    }

  scmd = XNEWVEC (char, strlen (program)
		  + ((flags & PEXECUTE_SEARCH) != 0 ? 4 : 0)
		  + strlen (rf)
		  + strlen (infile)
		  + strlen (outfile)
		  + 10);
  sprintf (scmd, "%s%s @%s%s%s%s%s",
	   program,
	   (flags & PEXECUTE_SEARCH) != 0 ? ".exe" : "",
	   rf,
	   inindex != -1 ? " <" : "",
	   infile,
	   outindex != -1 ? " >" : "",
	   outfile);

  argfile = fopen (rf, "w");
  if (argfile == NULL)
    {
      *err = errno;
      free (scmd);
      free (rf);
      *errmsg = "cannot open temporary command file";
      return -1;
    }

  for (i = 1; argv[i] != NULL; ++i)
    {
      char *p;

      for (p = argv[i]; *p != '\0'; ++p)
	{
	  if (*p == '"' || *p == '\'' || *p == '\\' || ISSPACE (*p))
	    putc ('\\', argfile);
	  putc (*p, argfile);
	}
      putc ('\n', argfile);
    }

  fclose (argfile);

  status = system (scmd);

  if (status == -1)
    {
      *err = errno;
      remove (rf);
      free (scmd);
      free (rf);
      *errmsg = "system";
      return -1;
    }

  remove (rf);
  free (scmd);
  free (rf);

  /* Save the exit status for later.  When we are called, obj->count
     is the number of children which have executed before this
     one.  */
  ms->statuses = XRESIZEVEC(int, ms->statuses, obj->count + 1);
  ms->statuses[obj->count] = status;

  return obj->count;
}

/* Wait for a child process to complete.  Actually the child process
   has already completed, and we just need to return the exit
   status.  */

static int
pex_msdos_wait (struct pex_obj *obj, long pid, int *status,
		struct pex_time *time, int done ATTRIBUTE_UNUSED,
		const char **errmsg ATTRIBUTE_UNUSED,
		int *err ATTRIBUTE_UNUSED)
{
  struct pex_msdos *ms;

  ms = (struct pex_msdos *) obj->sysdep;

  if (time != NULL)
    memset (time, 0, sizeof *time);

  *status = ms->statuses[pid];

  return 0;
}

/* Clean up the pex_msdos structure.  */

static void
pex_msdos_cleanup (struct pex_obj  *obj)
{
  struct pex_msdos *ms;
  int i;

  ms = (struct pex_msdos *) obj->sysdep;
  for (i = 0; i < PEX_MSDOS_FILE_COUNT; ++i)
    if (msdos->files[i] != NULL)
      free (msdos->files[i]);
  if (msdos->statuses != NULL)
    free (msdos->statuses);
  free (msdos);
  obj->sysdep = NULL;
}
