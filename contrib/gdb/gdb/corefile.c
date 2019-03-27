/* Core dump and executable file functions above target vector, for GDB.

   Copyright 1986, 1987, 1989, 1991, 1992, 1993, 1994, 1996, 1997,
   1998, 1999, 2000, 2001, 2003 Free Software Foundation, Inc.

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
#include "gdb_string.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include "inferior.h"
#include "symtab.h"
#include "command.h"
#include "gdbcmd.h"
#include "bfd.h"
#include "target.h"
#include "gdbcore.h"
#include "dis-asm.h"
#include "gdb_stat.h"
#include "completer.h"

/* Local function declarations.  */

extern void _initialize_core (void);
static void call_extra_exec_file_hooks (char *filename);

/* You can have any number of hooks for `exec_file_command' command to call.
   If there's only one hook, it is set in exec_file_display hook.
   If there are two or more hooks, they are set in exec_file_extra_hooks[],
   and exec_file_display_hook is set to a function that calls all of them.
   This extra complexity is needed to preserve compatibility with
   old code that assumed that only one hook could be set, and which called
   exec_file_display_hook directly.  */

typedef void (*hook_type) (char *);

hook_type exec_file_display_hook;	/* the original hook */
static hook_type *exec_file_extra_hooks;	/* array of additional hooks */
static int exec_file_hook_count = 0;	/* size of array */

/* Binary file diddling handle for the core file.  */

bfd *core_bfd = NULL;


/* Backward compatability with old way of specifying core files.  */

void
core_file_command (char *filename, int from_tty)
{
  struct target_ops *t;

  dont_repeat ();		/* Either way, seems bogus. */

  t = find_core_target ();
  if (t == NULL)
    error ("GDB can't read core files on this machine.");

  if (!filename)
    (t->to_detach) (filename, from_tty);
  else
    (t->to_open) (filename, from_tty);
}


/* If there are two or more functions that wish to hook into exec_file_command,
 * this function will call all of the hook functions. */

static void
call_extra_exec_file_hooks (char *filename)
{
  int i;

  for (i = 0; i < exec_file_hook_count; i++)
    (*exec_file_extra_hooks[i]) (filename);
}

/* Call this to specify the hook for exec_file_command to call back.
   This is called from the x-window display code.  */

void
specify_exec_file_hook (void (*hook) (char *))
{
  hook_type *new_array;

  if (exec_file_display_hook != NULL)
    {
      /* There's already a hook installed.  Arrange to have both it
       * and the subsequent hooks called. */
      if (exec_file_hook_count == 0)
	{
	  /* If this is the first extra hook, initialize the hook array. */
	  exec_file_extra_hooks = (hook_type *) xmalloc (sizeof (hook_type));
	  exec_file_extra_hooks[0] = exec_file_display_hook;
	  exec_file_display_hook = call_extra_exec_file_hooks;
	  exec_file_hook_count = 1;
	}

      /* Grow the hook array by one and add the new hook to the end.
         Yes, it's inefficient to grow it by one each time but since
         this is hardly ever called it's not a big deal.  */
      exec_file_hook_count++;
      new_array =
	(hook_type *) xrealloc (exec_file_extra_hooks,
				exec_file_hook_count * sizeof (hook_type));
      exec_file_extra_hooks = new_array;
      exec_file_extra_hooks[exec_file_hook_count - 1] = hook;
    }
  else
    exec_file_display_hook = hook;
}

/* The exec file must be closed before running an inferior.
   If it is needed again after the inferior dies, it must
   be reopened.  */

void
close_exec_file (void)
{
#if 0				/* FIXME */
  if (exec_bfd)
    bfd_tempclose (exec_bfd);
#endif
}

void
reopen_exec_file (void)
{
#if 0				/* FIXME */
  if (exec_bfd)
    bfd_reopen (exec_bfd);
#else
  char *filename;
  int res;
  struct stat st;
  long mtime;

  /* Don't do anything if the current target isn't exec. */
  if (exec_bfd == NULL || strcmp (target_shortname, "exec") != 0)
    return;

  /* If the timestamp of the exec file has changed, reopen it. */
  filename = xstrdup (bfd_get_filename (exec_bfd));
  make_cleanup (xfree, filename);
  mtime = bfd_get_mtime (exec_bfd);
  res = stat (filename, &st);

  if (mtime && mtime != st.st_mtime)
    {
      exec_open (filename, 0);
    }
#endif
}

/* If we have both a core file and an exec file,
   print a warning if they don't go together.  */

void
validate_files (void)
{
  if (exec_bfd && core_bfd)
    {
      if (!core_file_matches_executable_p (core_bfd, exec_bfd))
	warning ("core file may not match specified executable file.");
      else if (bfd_get_mtime (exec_bfd) > bfd_get_mtime (core_bfd))
	warning ("exec file is newer than core file.");
    }
}

/* Return the name of the executable file as a string.
   ERR nonzero means get error if there is none specified;
   otherwise return 0 in that case.  */

char *
get_exec_file (int err)
{
  if (exec_bfd)
    return bfd_get_filename (exec_bfd);
  if (!err)
    return NULL;

  error ("No executable file specified.\n\
Use the \"file\" or \"exec-file\" command.");
  return NULL;
}


/* Report a memory error with error().  */

void
memory_error (int status, CORE_ADDR memaddr)
{
  struct ui_file *tmp_stream = mem_fileopen ();
  make_cleanup_ui_file_delete (tmp_stream);

  if (status == EIO)
    {
      /* Actually, address between memaddr and memaddr + len
         was out of bounds. */
      fprintf_unfiltered (tmp_stream, "Cannot access memory at address ");
      print_address_numeric (memaddr, 1, tmp_stream);
    }
  else
    {
      fprintf_filtered (tmp_stream, "Error accessing memory address ");
      print_address_numeric (memaddr, 1, tmp_stream);
      fprintf_filtered (tmp_stream, ": %s.",
		       safe_strerror (status));
    }

  error_stream (tmp_stream);
}

/* Same as target_read_memory, but report an error if can't read.  */
void
read_memory (CORE_ADDR memaddr, char *myaddr, int len)
{
  int status;
  status = target_read_memory (memaddr, myaddr, len);
  if (status != 0)
    memory_error (status, memaddr);
}

/* Argument / return result struct for use with
   do_captured_read_memory_integer().  MEMADDR and LEN are filled in
   by gdb_read_memory_integer().  RESULT is the contents that were
   successfully read from MEMADDR of length LEN.  */

struct captured_read_memory_integer_arguments
{
  CORE_ADDR memaddr;
  int len;
  LONGEST result;
};

/* Helper function for gdb_read_memory_integer().  DATA must be a
   pointer to a captured_read_memory_integer_arguments struct. 
   Return 1 if successful.  Note that the catch_errors() interface
   will return 0 if an error occurred while reading memory.  This
   choice of return code is so that we can distinguish between
   success and failure.  */

static int
do_captured_read_memory_integer (void *data)
{
  struct captured_read_memory_integer_arguments *args = (struct captured_read_memory_integer_arguments*) data;
  CORE_ADDR memaddr = args->memaddr;
  int len = args->len;

  args->result = read_memory_integer (memaddr, len);

  return 1;
}

/* Read memory at MEMADDR of length LEN and put the contents in
   RETURN_VALUE.  Return 0 if MEMADDR couldn't be read and non-zero
   if successful.  */

int
safe_read_memory_integer (CORE_ADDR memaddr, int len, LONGEST *return_value)
{
  int status;
  struct captured_read_memory_integer_arguments args;
  args.memaddr = memaddr;
  args.len = len;

  status = catch_errors (do_captured_read_memory_integer, &args,
                        "", RETURN_MASK_ALL);
  if (status)
    *return_value = args.result;

  return status;
}

LONGEST
read_memory_integer (CORE_ADDR memaddr, int len)
{
  char buf[sizeof (LONGEST)];

  read_memory (memaddr, buf, len);
  return extract_signed_integer (buf, len);
}

ULONGEST
read_memory_unsigned_integer (CORE_ADDR memaddr, int len)
{
  char buf[sizeof (ULONGEST)];

  read_memory (memaddr, buf, len);
  return extract_unsigned_integer (buf, len);
}

void
read_memory_string (CORE_ADDR memaddr, char *buffer, int max_len)
{
  char *cp;
  int i;
  int cnt;

  cp = buffer;
  while (1)
    {
      if (cp - buffer >= max_len)
	{
	  buffer[max_len - 1] = '\0';
	  break;
	}
      cnt = max_len - (cp - buffer);
      if (cnt > 8)
	cnt = 8;
      read_memory (memaddr + (int) (cp - buffer), cp, cnt);
      for (i = 0; i < cnt && *cp; i++, cp++)
	;			/* null body */

      if (i < cnt && !*cp)
	break;
    }
}

CORE_ADDR
read_memory_typed_address (CORE_ADDR addr, struct type *type)
{
  char *buf = alloca (TYPE_LENGTH (type));
  read_memory (addr, buf, TYPE_LENGTH (type));
  return extract_typed_address (buf, type);
}

/* Same as target_write_memory, but report an error if can't write.  */
void
write_memory (CORE_ADDR memaddr, char *myaddr, int len)
{
  int status;

  status = target_write_memory (memaddr, myaddr, len);
  if (status != 0)
    memory_error (status, memaddr);
}

/* Store VALUE at ADDR in the inferior as a LEN-byte unsigned integer.  */
void
write_memory_unsigned_integer (CORE_ADDR addr, int len, ULONGEST value)
{
  char *buf = alloca (len);
  store_unsigned_integer (buf, len, value);
  write_memory (addr, buf, len);
}

/* Store VALUE at ADDR in the inferior as a LEN-byte signed integer.  */
void
write_memory_signed_integer (CORE_ADDR addr, int len, LONGEST value)
{
  char *buf = alloca (len);
  store_signed_integer (buf, len, value);
  write_memory (addr, buf, len);
}



#if 0
/* Enable after 4.12.  It is not tested.  */

/* Search code.  Targets can just make this their search function, or
   if the protocol has a less general search function, they can call this
   in the cases it can't handle.  */
void
generic_search (int len, char *data, char *mask, CORE_ADDR startaddr,
		int increment, CORE_ADDR lorange, CORE_ADDR hirange,
		CORE_ADDR *addr_found, char *data_found)
{
  int i;
  CORE_ADDR curaddr = startaddr;

  while (curaddr >= lorange && curaddr < hirange)
    {
      read_memory (curaddr, data_found, len);
      for (i = 0; i < len; ++i)
	if ((data_found[i] & mask[i]) != data[i])
	  goto try_again;
      /* It matches.  */
      *addr_found = curaddr;
      return;

    try_again:
      curaddr += increment;
    }
  *addr_found = (CORE_ADDR) 0;
  return;
}
#endif /* 0 */

/* The current default bfd target.  Points to storage allocated for
   gnutarget_string.  */
char *gnutarget;

/* Same thing, except it is "auto" not NULL for the default case.  */
static char *gnutarget_string;

static void set_gnutarget_command (char *, int, struct cmd_list_element *);

static void
set_gnutarget_command (char *ignore, int from_tty, struct cmd_list_element *c)
{
  if (strcmp (gnutarget_string, "auto") == 0)
    gnutarget = NULL;
  else
    gnutarget = gnutarget_string;
}

/* Set the gnutarget.  */
void
set_gnutarget (char *newtarget)
{
  if (gnutarget_string != NULL)
    xfree (gnutarget_string);
  gnutarget_string = savestring (newtarget, strlen (newtarget));
  set_gnutarget_command (NULL, 0, NULL);
}

void
_initialize_core (void)
{
  struct cmd_list_element *c;
  c = add_cmd ("core-file", class_files, core_file_command,
	       "Use FILE as core dump for examining memory and registers.\n\
No arg means have no core file.  This command has been superseded by the\n\
`target core' and `detach' commands.", &cmdlist);
  set_cmd_completer (c, filename_completer);

  c = add_set_cmd ("gnutarget", class_files, var_string_noescape,
		   (char *) &gnutarget_string,
		   "Set the current BFD target.\n\
Use `set gnutarget auto' to specify automatic detection.",
		   &setlist);
  set_cmd_sfunc (c, set_gnutarget_command);
  add_show_from_set (c, &showlist);

  if (getenv ("GNUTARGET"))
    set_gnutarget (getenv ("GNUTARGET"));
  else
    set_gnutarget ("auto");
}
