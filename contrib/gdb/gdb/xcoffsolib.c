/* Shared library support for RS/6000 (xcoff) object files, for GDB.
   Copyright 1991, 1992, 1995, 1996, 1999, 2000, 2001
   Free Software Foundation, Inc.
   Contributed by IBM Corporation.

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
#include "bfd.h"
#include "xcoffsolib.h"
#include "inferior.h"
#include "gdbcmd.h"
#include "symfile.h"
#include "frame.h"
#include "gdb_regex.h"


/* If ADDR lies in a shared library, return its name.
   Note that returned name points to static data whose content is overwritten
   by each call.  */

char *
xcoff_solib_address (CORE_ADDR addr)
{
  static char *buffer = NULL;
  struct vmap *vp = vmap;

  /* The first vmap entry is for the exec file.  */

  if (vp == NULL)
    return NULL;
  for (vp = vp->nxt; vp; vp = vp->nxt)
    if (vp->tstart <= addr && addr < vp->tend)
      {
	xfree (buffer);
	xasprintf (&buffer, "%s%s%s%s",
			    vp->name,
			    *vp->member ? "(" : "",
			    vp->member,
			    *vp->member ? ")" : "");
	return buffer;
      }
  return NULL;
}

static void solib_info (char *, int);
static void sharedlibrary_command (char *pattern, int from_tty);

static void
solib_info (char *args, int from_tty)
{
  struct vmap *vp = vmap;

  /* Check for new shared libraries loaded with load ().  */
  if (! ptid_equal (inferior_ptid, null_ptid))
    xcoff_relocate_symtab (PIDGET (inferior_ptid));

  if (vp == NULL || vp->nxt == NULL)
    {
      printf_unfiltered ("No shared libraries loaded at this time.\n");
      return;
    }

  /* Skip over the first vmap, it is the main program, always loaded.  */
  vp = vp->nxt;

  printf_unfiltered ("\
Text Range		Data Range		Syms	Shared Object Library\n");

  for (; vp != NULL; vp = vp->nxt)
    {
      printf_unfiltered ("0x%s-0x%s	0x%s-0x%s	%s	%s%s%s%s\n",
			 paddr (vp->tstart),paddr (vp->tend),
			 paddr (vp->dstart), paddr (vp->dend),
			 vp->loaded ? "Yes" : "No ",
			 vp->name,
			 *vp->member ? "(" : "",
			 vp->member,
			 *vp->member ? ")" : "");
    }
}

static void
sharedlibrary_command (char *pattern, int from_tty)
{
  dont_repeat ();

  /* Check for new shared libraries loaded with load ().  */
  if (! ptid_equal (inferior_ptid, null_ptid))
    xcoff_relocate_symtab (PIDGET (inferior_ptid));

  if (pattern)
    {
      char *re_err = re_comp (pattern);

      if (re_err)
	error ("Invalid regexp: %s", re_err);
    }

  /* Walk the list of currently loaded shared libraries, and read
     symbols for any that match the pattern --- or any whose symbols
     aren't already loaded, if no pattern was given.  */
  {
    int any_matches = 0;
    int loaded_any_symbols = 0;
    struct vmap *vp = vmap;

    if (!vp)
      return;

    /* skip over the first vmap, it is the main program, always loaded. */
    for (vp = vp->nxt; vp; vp = vp->nxt)
      if (! pattern
	    || re_exec (vp->name)
	    || (*vp->member && re_exec (vp->member)))
	{
	  any_matches = 1;

	  if (vp->loaded)
	    {
	      if (from_tty)
		printf_unfiltered ("Symbols already loaded for %s\n",
				   vp->name);
	    }
	  else
	    {
	      if (vmap_add_symbols (vp))
		loaded_any_symbols = 1;
	    }
	}

    if (from_tty && pattern && ! any_matches)
      printf_unfiltered
	("No loaded shared libraries match the pattern `%s'.\n", pattern);

    if (loaded_any_symbols)
      {
	/* Getting new symbols may change our opinion about what is
	   frameless.  */
	reinit_frame_cache ();
      }
  }
}

/* LOCAL FUNCTION

   no_shared_libraries -- handle command to explicitly discard symbols
   from shared libraries.

   DESCRIPTION

   Implements the command "nosharedlibrary", which discards symbols
   that have been auto-loaded from shared libraries.  Symbols from
   shared libraries that were added by explicit request of the user
   are not discarded.  Also called from remote.c.  */

void
no_shared_libraries (char *ignored, int from_tty)
{
  /* FIXME */
}

void
_initialize_xcoffsolib (void)
{
  add_com ("sharedlibrary", class_files, sharedlibrary_command,
	   "Load shared object library symbols for files matching REGEXP.");
  add_info ("sharedlibrary", solib_info,
	    "Status of loaded shared object libraries");

  add_show_from_set
    (add_set_cmd ("auto-solib-add", class_support, var_boolean,
		  (char *) &auto_solib_add,
		  "Set autoloading of shared library symbols.\n\
If \"on\", symbols from all shared object libraries will be loaded\n\
automatically when the inferior begins execution, when the dynamic linker\n\
informs gdb that a new library has been loaded, or when attaching to the\n\
inferior.  Otherwise, symbols must be loaded manually, using `sharedlibrary'.",
		  &setlist),
     &showlist);
}
