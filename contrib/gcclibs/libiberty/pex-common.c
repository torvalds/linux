/* Common code for executing a program in a sub-process.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@airs.com>.

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

#include "config.h"
#include "libiberty.h"
#include "pex-common.h"

#include <stdio.h>
#include <errno.h>
#ifdef NEED_DECLARATION_ERRNO
extern int errno;
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

extern int mkstemps (char *, int);

/* This file contains subroutines for the program execution routines
   (pex_init, pex_run, etc.).  This file is compiled on all
   systems.  */

static void pex_add_remove (struct pex_obj *, const char *, int);
static int pex_get_status_and_time (struct pex_obj *, int, const char **,
				    int *);

/* Initialize a pex_obj structure.  */

struct pex_obj *
pex_init_common (int flags, const char *pname, const char *tempbase,
		 const struct pex_funcs *funcs)
{
  struct pex_obj *obj;

  obj = XNEW (struct pex_obj);
  obj->flags = flags;
  obj->pname = pname;
  obj->tempbase = tempbase;
  obj->next_input = STDIN_FILE_NO;
  obj->next_input_name = NULL;
  obj->next_input_name_allocated = 0;
  obj->count = 0;
  obj->children = NULL;
  obj->status = NULL;
  obj->time = NULL;
  obj->number_waited = 0;
  obj->input_file = NULL;
  obj->read_output = NULL;
  obj->remove_count = 0;
  obj->remove = NULL;
  obj->funcs = funcs;
  obj->sysdep = NULL;
  return obj;
}

/* Add a file to be removed when we are done.  */

static void
pex_add_remove (struct pex_obj *obj, const char *name, int allocated)
{
  char *add;

  ++obj->remove_count;
  obj->remove = XRESIZEVEC (char *, obj->remove, obj->remove_count);
  if (allocated)
    add = (char *) name;
  else
    add = xstrdup (name);
  obj->remove[obj->remove_count - 1] = add;
}

/* Generate a temporary file name based on OBJ, FLAGS, and NAME.
   Return NULL if we were unable to reserve a temporary filename.

   If non-NULL, the result is either allocated with malloc, or the
   same pointer as NAME.  */
static char *
temp_file (struct pex_obj *obj, int flags, char *name)
{
  if (name == NULL)
    {
      if (obj->tempbase == NULL)
        {
          name = make_temp_file (NULL);
        }
      else
        {
          int len = strlen (obj->tempbase);
          int out;

          if (len >= 6
              && strcmp (obj->tempbase + len - 6, "XXXXXX") == 0)
            name = xstrdup (obj->tempbase);
          else
            name = concat (obj->tempbase, "XXXXXX", NULL);

          out = mkstemps (name, 0);
          if (out < 0)
            {
              free (name);
              return NULL;
            }

          /* This isn't obj->funcs->close because we got the
             descriptor from mkstemps, not from a function in
             obj->funcs.  Calling close here is just like what
             make_temp_file does.  */
          close (out);
        }
    }
  else if ((flags & PEX_SUFFIX) != 0)
    {
      if (obj->tempbase == NULL)
        name = make_temp_file (name);
      else
        name = concat (obj->tempbase, name, NULL);
    }

  return name;
}


/* As for pex_run (), but permits the environment for the child process
   to be specified. */

const char *
pex_run_in_environment (struct pex_obj *obj, int flags, const char *executable,
       	                char * const * argv, char * const * env,
                        const char *orig_outname, const char *errname,
                  	int *err)
{
  const char *errmsg;
  int in, out, errdes;
  char *outname;
  int outname_allocated;
  int p[2];
  int toclose;
  long pid;

  in = -1;
  out = -1;
  errdes = -1;
  outname = (char *) orig_outname;
  outname_allocated = 0;

  /* If the user called pex_input_file, close the file now.  */
  if (obj->input_file)
    {
      if (fclose (obj->input_file) == EOF)
        {
          errmsg = "closing pipeline input file";
          goto error_exit;
        }
      obj->input_file = NULL;
    }

  /* Set IN.  */

  if (obj->next_input_name != NULL)
    {
      /* We have to make sure that the previous process has completed
	 before we try to read the file.  */
      if (!pex_get_status_and_time (obj, 0, &errmsg, err))
	goto error_exit;

      in = obj->funcs->open_read (obj, obj->next_input_name,
				  (flags & PEX_BINARY_INPUT) != 0);
      if (in < 0)
	{
	  *err = errno;
	  errmsg = "open temporary file";
	  goto error_exit;
	}
      if (obj->next_input_name_allocated)
	{
	  free (obj->next_input_name);
	  obj->next_input_name_allocated = 0;
	}
      obj->next_input_name = NULL;
    }
  else
    {
      in = obj->next_input;
      if (in < 0)
	{
	  *err = 0;
	  errmsg = "pipeline already complete";
	  goto error_exit;
	}
    }

  /* Set OUT and OBJ->NEXT_INPUT/OBJ->NEXT_INPUT_NAME.  */

  if ((flags & PEX_LAST) != 0)
    {
      if (outname == NULL)
	out = STDOUT_FILE_NO;
      else if ((flags & PEX_SUFFIX) != 0)
	{
	  outname = concat (obj->tempbase, outname, NULL);
	  outname_allocated = 1;
	}
      obj->next_input = -1;
    }
  else if ((obj->flags & PEX_USE_PIPES) == 0)
    {
      outname = temp_file (obj, flags, outname);
      if (! outname)
        {
          *err = 0;
          errmsg = "could not create temporary file";
          goto error_exit;
        }

      if (outname != orig_outname)
        outname_allocated = 1;

      if ((obj->flags & PEX_SAVE_TEMPS) == 0)
	{
	  pex_add_remove (obj, outname, outname_allocated);
	  outname_allocated = 0;
	}

      /* Hand off ownership of outname to the next stage.  */
      obj->next_input_name = outname;
      obj->next_input_name_allocated = outname_allocated;
      outname_allocated = 0;
    }
  else
    {
      if (obj->funcs->pipe (obj, p, (flags & PEX_BINARY_OUTPUT) != 0) < 0)
	{
	  *err = errno;
	  errmsg = "pipe";
	  goto error_exit;
	}

      out = p[WRITE_PORT];
      obj->next_input = p[READ_PORT];
    }

  if (out < 0)
    {
      out = obj->funcs->open_write (obj, outname,
				    (flags & PEX_BINARY_OUTPUT) != 0);
      if (out < 0)
	{
	  *err = errno;
	  errmsg = "open temporary output file";
	  goto error_exit;
	}
    }

  if (outname_allocated)
    {
      free (outname);
      outname_allocated = 0;
    }

  /* Set ERRDES.  */

  if (errname == NULL)
    errdes = STDERR_FILE_NO;
  else
    {
      /* We assume that stderr is in text mode--it certainly shouldn't
	 be controlled by PEX_BINARY_OUTPUT.  If necessary, we can add
	 a PEX_BINARY_STDERR flag.  */
      errdes = obj->funcs->open_write (obj, errname, 0);
      if (errdes < 0)
	{
	  *err = errno;
	  errmsg = "open error file";
	  goto error_exit;
	}
    }

  /* If we are using pipes, the child process has to close the next
     input pipe.  */

  if ((obj->flags & PEX_USE_PIPES) == 0)
    toclose = -1;
  else
    toclose = obj->next_input;

  /* Run the program.  */

  pid = obj->funcs->exec_child (obj, flags, executable, argv, env,
				in, out, errdes, toclose, &errmsg, err);
  if (pid < 0)
    goto error_exit;

  ++obj->count;
  obj->children = XRESIZEVEC (long, obj->children, obj->count);
  obj->children[obj->count - 1] = pid;

  return NULL;

 error_exit:
  if (in >= 0 && in != STDIN_FILE_NO)
    obj->funcs->close (obj, in);
  if (out >= 0 && out != STDOUT_FILE_NO)
    obj->funcs->close (obj, out);
  if (errdes >= 0 && errdes != STDERR_FILE_NO)
    obj->funcs->close (obj, errdes);
  if (outname_allocated)
    free (outname);
  return errmsg;
}

/* Run a program.  */

const char *
pex_run (struct pex_obj *obj, int flags, const char *executable,
       	 char * const * argv, const char *orig_outname, const char *errname,
         int *err)
{
  return pex_run_in_environment (obj, flags, executable, argv, NULL,
				 orig_outname, errname, err);
}

/* Return a FILE pointer for a temporary file to fill with input for
   the pipeline.  */
FILE *
pex_input_file (struct pex_obj *obj, int flags, const char *in_name)
{
  char *name = (char *) in_name;
  FILE *f;

  /* This must be called before the first pipeline stage is run, and
     there must not have been any other input selected.  */
  if (obj->count != 0
      || (obj->next_input >= 0 && obj->next_input != STDIN_FILE_NO)
      || obj->next_input_name)
    {
      errno = EINVAL;
      return NULL;
    }

  name = temp_file (obj, flags, name);
  if (! name)
    return NULL;

  f = fopen (name, (flags & PEX_BINARY_OUTPUT) ? "wb" : "w");
  if (! f)
    {
      free (name);
      return NULL;
    }

  obj->input_file = f;
  obj->next_input_name = name;
  obj->next_input_name_allocated = (name != in_name);

  return f;
}

/* Return a stream for a pipe connected to the standard input of the
   first stage of the pipeline.  */
FILE *
pex_input_pipe (struct pex_obj *obj, int binary)
{
  int p[2];
  FILE *f;

  /* You must call pex_input_pipe before the first pex_run or pex_one.  */
  if (obj->count > 0)
    goto usage_error;

  /* You must be using pipes.  Implementations that don't support
     pipes clear this flag before calling pex_init_common.  */
  if (! (obj->flags & PEX_USE_PIPES))
    goto usage_error;

  /* If we have somehow already selected other input, that's a
     mistake.  */
  if ((obj->next_input >= 0 && obj->next_input != STDIN_FILE_NO)
      || obj->next_input_name)
    goto usage_error;

  if (obj->funcs->pipe (obj, p, binary != 0) < 0)
    return NULL;

  f = obj->funcs->fdopenw (obj, p[WRITE_PORT], binary != 0);
  if (! f)
    {
      int saved_errno = errno;
      obj->funcs->close (obj, p[READ_PORT]);
      obj->funcs->close (obj, p[WRITE_PORT]);
      errno = saved_errno;
      return NULL;
    }

  obj->next_input = p[READ_PORT];

  return f;

 usage_error:
  errno = EINVAL;
  return NULL;
}

/* Return a FILE pointer for the output of the last program
   executed.  */

FILE *
pex_read_output (struct pex_obj *obj, int binary)
{
  if (obj->next_input_name != NULL)
    {
      const char *errmsg;
      int err;

      /* We have to make sure that the process has completed before we
	 try to read the file.  */
      if (!pex_get_status_and_time (obj, 0, &errmsg, &err))
	{
	  errno = err;
	  return NULL;
	}

      obj->read_output = fopen (obj->next_input_name, binary ? "rb" : "r");

      if (obj->next_input_name_allocated)
	{
	  free (obj->next_input_name);
	  obj->next_input_name_allocated = 0;
	}
      obj->next_input_name = NULL;
    }
  else
    {
      int o;

      o = obj->next_input;
      if (o < 0 || o == STDIN_FILE_NO)
	return NULL;
      obj->read_output = obj->funcs->fdopenr (obj, o, binary);
      obj->next_input = -1;
    }

  return obj->read_output;
}

/* Get the exit status and, if requested, the resource time for all
   the child processes.  Return 0 on failure, 1 on success.  */

static int
pex_get_status_and_time (struct pex_obj *obj, int done, const char **errmsg,
			 int *err)
{
  int ret;
  int i;

  if (obj->number_waited == obj->count)
    return 1;

  obj->status = XRESIZEVEC (int, obj->status, obj->count);
  if ((obj->flags & PEX_RECORD_TIMES) != 0)
    obj->time = XRESIZEVEC (struct pex_time, obj->time, obj->count);

  ret = 1;
  for (i = obj->number_waited; i < obj->count; ++i)
    {
      if (obj->funcs->wait (obj, obj->children[i], &obj->status[i],
			    obj->time == NULL ? NULL : &obj->time[i],
			    done, errmsg, err) < 0)
	ret = 0;
    }
  obj->number_waited = i;

  return ret;
}

/* Get exit status of executed programs.  */

int
pex_get_status (struct pex_obj *obj, int count, int *vector)
{
  if (obj->status == NULL)
    {
      const char *errmsg;
      int err;

      if (!pex_get_status_and_time (obj, 0, &errmsg, &err))
	return 0;
    }

  if (count > obj->count)
    {
      memset (vector + obj->count, 0, (count - obj->count) * sizeof (int));
      count = obj->count;
    }

  memcpy (vector, obj->status, count * sizeof (int));

  return 1;
}

/* Get process times of executed programs.  */

int
pex_get_times (struct pex_obj *obj, int count, struct pex_time *vector)
{
  if (obj->status == NULL)
    {
      const char *errmsg;
      int err;

      if (!pex_get_status_and_time (obj, 0, &errmsg, &err))
	return 0;
    }

  if (obj->time == NULL)
    return 0;

  if (count > obj->count)
    {
      memset (vector + obj->count, 0,
	      (count - obj->count) * sizeof (struct pex_time));
      count = obj->count;
    }

  memcpy (vector, obj->time, count * sizeof (struct pex_time));

  return 1;
}

/* Free a pex_obj structure.  */

void
pex_free (struct pex_obj *obj)
{
  if (obj->next_input >= 0 && obj->next_input != STDIN_FILE_NO)
    obj->funcs->close (obj, obj->next_input);

  /* If the caller forgot to wait for the children, we do it here, to
     avoid zombies.  */
  if (obj->status == NULL)
    {
      const char *errmsg;
      int err;

      obj->flags &= ~ PEX_RECORD_TIMES;
      pex_get_status_and_time (obj, 1, &errmsg, &err);
    }

  if (obj->next_input_name_allocated)
    free (obj->next_input_name);
  if (obj->children != NULL)
    free (obj->children);
  if (obj->status != NULL)
    free (obj->status);
  if (obj->time != NULL)
    free (obj->time);
  if (obj->read_output != NULL)
    fclose (obj->read_output);

  if (obj->remove_count > 0)
    {
      int i;

      for (i = 0; i < obj->remove_count; ++i)
	{
	  remove (obj->remove[i]);
	  free (obj->remove[i]);
	}
      free (obj->remove);
    }

  if (obj->funcs->cleanup != NULL)
    obj->funcs->cleanup (obj);

  free (obj);
}
