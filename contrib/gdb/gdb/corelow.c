/* Core dump and executable file functions below target vector, for GDB.

   Copyright 1986, 1987, 1989, 1991, 1992, 1993, 1994, 1995, 1996,
   1997, 1998, 1999, 2000, 2001, 2003, 2004 Free Software Foundation,
   Inc.

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
#include "arch-utils.h"
#include "gdb_string.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>		/* needed for F_OK and friends */
#endif
#include "frame.h"		/* required by inferior.h */
#include "inferior.h"
#include "symtab.h"
#include "command.h"
#include "bfd.h"
#include "target.h"
#include "gdbcore.h"
#include "gdbthread.h"
#include "regcache.h"
#include "regset.h"
#include "symfile.h"
#include "exec.h"
#include "readline/readline.h"

#include "gdb_assert.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* List of all available core_fns.  On gdb startup, each core file register
   reader calls add_core_fns() to register information on each core format it
   is prepared to read. */

static struct core_fns *core_file_fns = NULL;

/* The core_fns for a core file handler that is prepared to read the core
   file currently open on core_bfd. */

static struct core_fns *core_vec = NULL;

/* FIXME: kettenis/20031023: Eventually this variable should
   disappear.  */

struct gdbarch *core_gdbarch = NULL;

static void core_files_info (struct target_ops *);

#ifdef SOLIB_ADD
static int solib_add_stub (void *);
#endif

static struct core_fns *sniff_core_bfd (bfd *);

static int gdb_check_format (bfd *);

static void core_open (char *, int);

static void core_detach (char *, int);

static void core_close (int);

static void core_close_cleanup (void *ignore);

static void get_core_registers (int);

static void add_to_thread_list (bfd *, asection *, void *);

static int ignore (CORE_ADDR, char *);

static int core_file_thread_alive (ptid_t tid);

static void init_core_ops (void);

void _initialize_corelow (void);

struct target_ops core_ops;

/* Link a new core_fns into the global core_file_fns list.  Called on gdb
   startup by the _initialize routine in each core file register reader, to
   register information about each format the the reader is prepared to
   handle. */

void
add_core_fns (struct core_fns *cf)
{
  cf->next = core_file_fns;
  core_file_fns = cf;
}

/* The default function that core file handlers can use to examine a
   core file BFD and decide whether or not to accept the job of
   reading the core file. */

int
default_core_sniffer (struct core_fns *our_fns, bfd *abfd)
{
  int result;

  result = (bfd_get_flavour (abfd) == our_fns -> core_flavour);
  return (result);
}

/* Walk through the list of core functions to find a set that can
   handle the core file open on ABFD.  Default to the first one in the
   list if nothing matches.  Returns pointer to set that is
   selected. */

static struct core_fns *
sniff_core_bfd (bfd *abfd)
{
  struct core_fns *cf;
  struct core_fns *yummy = NULL;
  int matches = 0;;

  /* Don't sniff if we have support for register sets in CORE_GDBARCH.  */
  if (core_gdbarch && gdbarch_regset_from_core_section_p (core_gdbarch))
    return NULL;

  for (cf = core_file_fns; cf != NULL; cf = cf->next)
    {
      if (cf->core_sniffer (cf, abfd))
	{
	  yummy = cf;
	  matches++;
	}
    }
  if (matches > 1)
    {
      warning ("\"%s\": ambiguous core format, %d handlers match",
	       bfd_get_filename (abfd), matches);
    }
  else if (matches == 0)
    {
      warning ("\"%s\": no core file handler recognizes format, using default",
	       bfd_get_filename (abfd));
    }
  if (yummy == NULL)
    {
      yummy = core_file_fns;
    }
  return (yummy);
}

/* The default is to reject every core file format we see.  Either
   BFD has to recognize it, or we have to provide a function in the
   core file handler that recognizes it. */

int
default_check_format (bfd *abfd)
{
  return (0);
}

/* Attempt to recognize core file formats that BFD rejects. */

static int
gdb_check_format (bfd *abfd)
{
  struct core_fns *cf;

  for (cf = core_file_fns; cf != NULL; cf = cf->next)
    {
      if (cf->check_format (abfd))
	{
	  return (1);
	}
    }
  return (0);
}

/* Discard all vestiges of any previous core file and mark data and stack
   spaces as empty.  */

static void
core_close (int quitting)
{
  char *name;

  if (core_bfd)
    {
      inferior_ptid = null_ptid;	/* Avoid confusion from thread stuff */

      /* Clear out solib state while the bfd is still open. See
         comments in clear_solib in solib.c. */
#ifdef CLEAR_SOLIB
      CLEAR_SOLIB ();
#endif

      name = bfd_get_filename (core_bfd);
      if (!bfd_close (core_bfd))
	warning ("cannot close \"%s\": %s",
		 name, bfd_errmsg (bfd_get_error ()));
      xfree (name);
      core_bfd = NULL;
      if (core_ops.to_sections)
	{
	  xfree (core_ops.to_sections);
	  core_ops.to_sections = NULL;
	  core_ops.to_sections_end = NULL;
	}
    }
  core_vec = NULL;
  core_gdbarch = NULL;
}

static void
core_close_cleanup (void *ignore)
{
  core_close (0/*ignored*/);
}

#ifdef SOLIB_ADD
/* Stub function for catch_errors around shared library hacking.  FROM_TTYP
   is really an int * which points to from_tty.  */

static int
solib_add_stub (void *from_ttyp)
{
  SOLIB_ADD (NULL, *(int *) from_ttyp, &current_target, auto_solib_add);
  re_enable_breakpoints_in_shlibs ();
  return 0;
}
#endif /* SOLIB_ADD */

/* Look for sections whose names start with `.reg/' so that we can extract the
   list of threads in a core file.  */

static void
add_to_thread_list (bfd *abfd, asection *asect, void *reg_sect_arg)
{
  int thread_id;
  asection *reg_sect = (asection *) reg_sect_arg;

  if (strncmp (bfd_section_name (abfd, asect), ".reg/", 5) != 0)
    return;

  thread_id = atoi (bfd_section_name (abfd, asect) + 5);

  add_thread (pid_to_ptid (thread_id));

/* Warning, Will Robinson, looking at BFD private data! */

  if (reg_sect != NULL
      && asect->filepos == reg_sect->filepos)	/* Did we find .reg? */
    inferior_ptid = pid_to_ptid (thread_id);	/* Yes, make it current */
}

/* This routine opens and sets up the core file bfd.  */

static void
core_open (char *filename, int from_tty)
{
  const char *p;
  int siggy;
  struct cleanup *old_chain;
  char *temp;
  bfd *temp_bfd;
  int ontop;
  int scratch_chan;

  target_preopen (from_tty);
  if (!filename)
    {
      error (core_bfd ?
	     "No core file specified.  (Use `detach' to stop debugging a core file.)"
	     : "No core file specified.");
    }

  filename = tilde_expand (filename);
  if (filename[0] != '/')
    {
      temp = concat (current_directory, "/", filename, NULL);
      xfree (filename);
      filename = temp;
    }

  old_chain = make_cleanup (xfree, filename);

  scratch_chan = open (filename, O_BINARY | ( write_files ? O_RDWR : O_RDONLY ), 0);
  if (scratch_chan < 0)
    perror_with_name (filename);

  temp_bfd = bfd_fdopenr (filename, gnutarget, scratch_chan);
  if (temp_bfd == NULL)
    perror_with_name (filename);

  if (!bfd_check_format (temp_bfd, bfd_core) &&
      !gdb_check_format (temp_bfd))
    {
      /* Do it after the err msg */
      /* FIXME: should be checking for errors from bfd_close (for one thing,
         on error it does not free all the storage associated with the
         bfd).  */
      make_cleanup_bfd_close (temp_bfd);
      error ("\"%s\" is not a core dump: %s",
	     filename, bfd_errmsg (bfd_get_error ()));
    }

  /* Looks semi-reasonable.  Toss the old core file and work on the new.  */

  discard_cleanups (old_chain);	/* Don't free filename any more */
  unpush_target (&core_ops);
  core_bfd = temp_bfd;
  old_chain = make_cleanup (core_close_cleanup, 0 /*ignore*/);

  /* FIXME: kettenis/20031023: This is very dangerous.  The
     CORE_GDBARCH that results from this call may very well be
     different from CURRENT_GDBARCH.  However, its methods may only
     work if it is selected as the current architecture, because they
     rely on swapped data (see gdbarch.c).  We should get rid of that
     swapped data.  */
  core_gdbarch = gdbarch_from_bfd (core_bfd);

  /* Find a suitable core file handler to munch on core_bfd */
  core_vec = sniff_core_bfd (core_bfd);

  validate_files ();

  /* Find the data section */
  if (build_section_table (core_bfd, &core_ops.to_sections,
			   &core_ops.to_sections_end))
    error ("\"%s\": Can't find sections: %s",
	   bfd_get_filename (core_bfd), bfd_errmsg (bfd_get_error ()));

  /* If we have no exec file, try to set the architecture from the
     core file.  We don't do this unconditionally since an exec file
     typically contains more information that helps us determine the
     architecture than a core file.  */
  if (!exec_bfd)
    set_gdbarch_from_file (core_bfd);

  ontop = !push_target (&core_ops);
  discard_cleanups (old_chain);

  p = bfd_core_file_failing_command (core_bfd);
  if (p)
    printf_filtered ("Core was generated by `%s'.\n", p);

  siggy = bfd_core_file_failing_signal (core_bfd);
  if (siggy > 0)
    /* NOTE: target_signal_from_host() converts a target signal value
       into gdb's internal signal value.  Unfortunately gdb's internal
       value is called ``target_signal'' and this function got the
       name ..._from_host(). */
    printf_filtered ("Program terminated with signal %d, %s.\n", siggy,
		     target_signal_to_string (target_signal_from_host (siggy)));

  /* Build up thread list from BFD sections. */

  init_thread_list ();
  bfd_map_over_sections (core_bfd, add_to_thread_list,
			 bfd_get_section_by_name (core_bfd, ".reg"));

  if (ontop)
    {
      /* Fetch all registers from core file.  */
      target_fetch_registers (-1);

      /* Add symbols and section mappings for any shared libraries.  */
#ifdef SOLIB_ADD
      catch_errors (solib_add_stub, &from_tty, (char *) 0,
		    RETURN_MASK_ALL);
#endif

      /* Now, set up the frame cache, and print the top of stack.  */
      flush_cached_frames ();
      select_frame (get_current_frame ());
      print_stack_frame (deprecated_selected_frame,
			 frame_relative_level (deprecated_selected_frame), 1);
    }
  else
    {
      warning (
		"you won't be able to access this core file until you terminate\n\
your %s; do ``info files''", target_longname);
    }
}

static void
core_detach (char *args, int from_tty)
{
  if (args)
    error ("Too many arguments");
  unpush_target (&core_ops);
  reinit_frame_cache ();
  if (from_tty)
    printf_filtered ("No core file now.\n");
}


/* Try to retrieve registers from a section in core_bfd, and supply
   them to core_vec->core_read_registers, as the register set numbered
   WHICH.

   If inferior_ptid is zero, do the single-threaded thing: look for a
   section named NAME.  If inferior_ptid is non-zero, do the
   multi-threaded thing: look for a section named "NAME/PID", where
   PID is the shortest ASCII decimal representation of inferior_ptid.

   HUMAN_NAME is a human-readable name for the kind of registers the
   NAME section contains, for use in error messages.

   If REQUIRED is non-zero, print an error if the core file doesn't
   have a section by the appropriate name.  Otherwise, just do nothing.  */

static void
get_core_register_section (char *name,
			   int which,
			   char *human_name,
			   int required)
{
  static char *section_name = NULL;
  struct bfd_section *section;
  bfd_size_type size;
  char *contents;

  xfree (section_name);
  if (PIDGET (inferior_ptid))
    section_name = xstrprintf ("%s/%d", name, PIDGET (inferior_ptid));
  else
    section_name = xstrdup (name);

  section = bfd_get_section_by_name (core_bfd, section_name);
  if (! section)
    {
      if (required)
	warning ("Couldn't find %s registers in core file.\n", human_name);
      return;
    }

  size = bfd_section_size (core_bfd, section);
  contents = alloca (size);
  if (! bfd_get_section_contents (core_bfd, section, contents,
				  (file_ptr) 0, size))
    {
      warning ("Couldn't read %s registers from `%s' section in core file.\n",
	       human_name, name);
      return;
    }

  if (core_gdbarch && gdbarch_regset_from_core_section_p (core_gdbarch))
    {
      const struct regset *regset;

      regset = gdbarch_regset_from_core_section (core_gdbarch, name, size);
      if (regset == NULL)
	{
	  if (required)
	    warning ("Couldn't recognize %s registers in core file.\n",
		     human_name);
	  return;
	}

      regset->supply_regset (regset, current_regcache, -1, contents, size);
      return;
    }

  gdb_assert (core_vec);
  core_vec->core_read_registers (contents, size, which,
				 ((CORE_ADDR)
				  bfd_section_vma (core_bfd, section)));
}


/* Get the registers out of a core file.  This is the machine-
   independent part.  Fetch_core_registers is the machine-dependent
   part, typically implemented in the xm-file for each architecture.  */

/* We just get all the registers, so we don't use regno.  */

static void
get_core_registers (int regno)
{
  int status;

  if (!(core_gdbarch && gdbarch_regset_from_core_section_p (core_gdbarch))
      && (core_vec == NULL || core_vec->core_read_registers == NULL))
    {
      fprintf_filtered (gdb_stderr,
		     "Can't fetch registers from this type of core file\n");
      return;
    }

  get_core_register_section (".reg", 0, "general-purpose", 1);
  get_core_register_section (".reg2", 2, "floating-point", 0);
  get_core_register_section (".reg-xfp", 3, "extended floating-point", 0);

  deprecated_registers_fetched ();
}

static void
core_files_info (struct target_ops *t)
{
  print_section_info (t, core_bfd);
}

static LONGEST
core_xfer_partial (struct target_ops *ops, enum target_object object,
		   const char *annex, void *readbuf,
		   const void *writebuf, ULONGEST offset, LONGEST len)
{
  switch (object)
    {
    case TARGET_OBJECT_MEMORY:
      if (readbuf)
	return (*ops->to_xfer_memory) (offset, readbuf, len, 0/*read*/,
				       NULL, ops);
      if (writebuf)
	return (*ops->to_xfer_memory) (offset, writebuf, len, 1/*write*/,
				       NULL, ops);
      return -1;

    case TARGET_OBJECT_AUXV:
      if (readbuf)
	{
	  /* When the aux vector is stored in core file, BFD
	     represents this with a fake section called ".auxv".  */

	  struct bfd_section *section;
	  bfd_size_type size;
	  char *contents;

	  section = bfd_get_section_by_name (core_bfd, ".auxv");
	  if (section == NULL)
	    return -1;

	  size = bfd_section_size (core_bfd, section);
	  if (offset >= size)
	    return 0;
	  size -= offset;
	  if (size > len)
	    size = len;
	  if (size > 0 &&
	      ! bfd_get_section_contents (core_bfd, section, readbuf,
					  (file_ptr) offset, size))
	    {
	      warning ("Couldn't read NT_AUXV note in core file.");
	      return -1;
	    }

	  return size;
	}
      return -1;

    case TARGET_OBJECT_DIRTY:
      {
	ULONGEST addr;
	addr = *(ULONGEST*)annex + offset;
	if (readbuf)
	  return (*ops->to_xfer_memory) (addr, readbuf, len, 0/*read*/,
					 NULL, ops);
	if (writebuf)
	  return (*ops->to_xfer_memory) (addr, writebuf, len, 1/*write*/,
					 NULL, ops);
	return -1;
      }

    default:
      if (ops->beneath != NULL)
	return ops->beneath->to_xfer_partial (ops->beneath, object, annex,
					      readbuf, writebuf, offset, len);
      return -1;
    }
}


/* If mourn is being called in all the right places, this could be say
   `gdb internal error' (since generic_mourn calls breakpoint_init_inferior).  */

static int
ignore (CORE_ADDR addr, char *contents)
{
  return 0;
}


/* Okay, let's be honest: threads gleaned from a core file aren't
   exactly lively, are they?  On the other hand, if we don't claim
   that each & every one is alive, then we don't get any of them
   to appear in an "info thread" command, which is quite a useful
   behaviour.
 */
static int
core_file_thread_alive (ptid_t tid)
{
  return 1;
}

/* Fill in core_ops with its defined operations and properties.  */

static void
init_core_ops (void)
{
  core_ops.to_shortname = "core";
  core_ops.to_longname = "Local core dump file";
  core_ops.to_doc =
    "Use a core file as a target.  Specify the filename of the core file.";
  core_ops.to_open = core_open;
  core_ops.to_close = core_close;
  core_ops.to_attach = find_default_attach;
  core_ops.to_detach = core_detach;
  core_ops.to_fetch_registers = get_core_registers;
  core_ops.to_xfer_partial = core_xfer_partial;
  core_ops.to_xfer_memory = xfer_memory;
  core_ops.to_files_info = core_files_info;
  core_ops.to_insert_breakpoint = ignore;
  core_ops.to_remove_breakpoint = ignore;
  core_ops.to_create_inferior = find_default_create_inferior;
  core_ops.to_thread_alive = core_file_thread_alive;
  core_ops.to_stratum = core_stratum;
  core_ops.to_has_memory = 1;
  core_ops.to_has_stack = 1;
  core_ops.to_has_registers = 1;
  core_ops.to_magic = OPS_MAGIC;
}

/* non-zero if we should not do the add_target call in
   _initialize_corelow; not initialized (i.e., bss) so that
   the target can initialize it (i.e., data) if appropriate.
   This needs to be set at compile time because we don't know
   for sure whether the target's initialize routine is called
   before us or after us. */
int coreops_suppress_target;

void
_initialize_corelow (void)
{
  init_core_ops ();

  if (!coreops_suppress_target)
    add_target (&core_ops);
}
