/* Handle HP ELF shared libraries for GDB, the GNU Debugger.

   Copyright 1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation,
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
   Boston, MA 02111-1307, USA.

   HP in their infinite stupidity choose not to use standard ELF dynamic
   linker interfaces.  They also choose not to make their ELF dymamic
   linker interfaces compatible with the SOM dynamic linker.  The
   net result is we can not use either of the existing somsolib.c or
   solib.c.  What a crock.

   Even more disgusting.  This file depends on functions provided only
   in certain PA64 libraries.  Thus this file is supposed to only be
   used native.  When will HP ever learn that they need to provide the
   same functionality in all their libraries!  */

#include <dlfcn.h>
#include <elf.h>
#include <elf_hp.h>

#include "defs.h"

#include "frame.h"
#include "bfd.h"
#include "libhppa.h"
#include "gdbcore.h"
#include "symtab.h"
#include "breakpoint.h"
#include "symfile.h"
#include "objfiles.h"
#include "inferior.h"
#include "gdb-stabs.h"
#include "gdb_stat.h"
#include "gdbcmd.h"
#include "language.h"
#include "regcache.h"
#include "exec.h"

#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

static CORE_ADDR bfd_lookup_symbol (bfd *, char *);
/* This lives in hppa-tdep.c. */
extern struct unwind_table_entry *find_unwind_entry (CORE_ADDR pc);

/* These ought to be defined in some public interface, but aren't.  They
   identify dynamic linker events.  */
#define DLD_CB_LOAD     1
#define DLD_CB_UNLOAD   0

/* A structure to keep track of all the known shared objects.  */
struct so_list
  {
    bfd *abfd;
    char *name;
    struct so_list *next;
    struct objfile *objfile;
    CORE_ADDR pa64_solib_desc_addr;
    struct load_module_desc pa64_solib_desc;
    struct section_table *sections;
    struct section_table *sections_end;
    int loaded;
  };

static struct so_list *so_list_head;

/* This is the cumulative size in bytes of the symbol tables of all
   shared objects on the so_list_head list.  (When we say size, here
   we mean of the information before it is brought into memory and
   potentially expanded by GDB.)  When adding a new shlib, this value
   is compared against a threshold size, held by auto_solib_limit (in
   megabytes).  If adding symbols for the new shlib would cause the
   total size to exceed the threshold, then the new shlib's symbols
   are not loaded. */
static LONGEST pa64_solib_total_st_size;

/* When the threshold is reached for any shlib, we refuse to add
   symbols for subsequent shlibs, even if those shlibs' symbols would
   be small enough to fit under the threshold.  Although this may
   result in one, early large shlib preventing the loading of later,
   smaller shlibs' symbols, it allows us to issue one informational
   message.  The alternative, to issue a message for each shlib whose
   symbols aren't loaded, could be a big annoyance where the threshold
   is exceeded due to a very large number of shlibs. */
static int pa64_solib_st_size_threshold_exceeded;

/* When adding fields, be sure to clear them in _initialize_pa64_solib. */
typedef struct
  {
    CORE_ADDR dld_flags_addr;
    LONGEST dld_flags;
    struct bfd_section *dyninfo_sect;
    int have_read_dld_descriptor;
    int is_valid;
    CORE_ADDR load_map;
    CORE_ADDR load_map_addr;
    struct load_module_desc dld_desc;
  }
dld_cache_t;

static dld_cache_t dld_cache;

static void pa64_sharedlibrary_info_command (char *, int);

static void pa64_solib_sharedlibrary_command (char *, int);

static void *pa64_target_read_memory (void *, CORE_ADDR, size_t, int);

static int read_dld_descriptor (struct target_ops *, int readsyms);

static int read_dynamic_info (asection *, dld_cache_t *);

static void add_to_solist (int, char *, int, struct load_module_desc *,
			   CORE_ADDR, struct target_ops *);

/* When examining the shared library for debugging information we have to
   look for HP debug symbols, stabs and dwarf2 debug symbols.  */
static char *pa64_debug_section_names[] = {
  ".debug_header", ".debug_gntt", ".debug_lntt", ".debug_slt", ".debug_vt",
  ".stabs", ".stabstr", ".debug_info", ".debug_abbrev", ".debug_aranges",
  ".debug_macinfo", ".debug_line", ".debug_loc", ".debug_pubnames",
  ".debug_str", NULL
};

/* Return a ballbark figure for the amount of memory GDB will need to
   allocate to read in the debug symbols from FILENAME.  */
static LONGEST
pa64_solib_sizeof_symbol_table (char *filename)
{
  bfd *abfd;
  int i;
  int desc;
  char *absolute_name;
  LONGEST st_size = (LONGEST) 0;
  asection *sect;

  /* We believe that filename was handed to us by the dynamic linker, and
     is therefore always an absolute path.  */
  desc = openp (getenv ("PATH"), 1, filename, O_RDONLY | O_BINARY,
		0, &absolute_name);
  if (desc < 0)
    {
      perror_with_name (filename);
    }
  filename = absolute_name;

  abfd = bfd_fdopenr (filename, gnutarget, desc);
  if (!abfd)
    {
      close (desc);
      make_cleanup (xfree, filename);
      error ("\"%s\": can't open to read symbols: %s.", filename,
	     bfd_errmsg (bfd_get_error ()));
    }

  if (!bfd_check_format (abfd, bfd_object))
    {
      bfd_close (abfd);
      make_cleanup (xfree, filename);
      error ("\"%s\": can't read symbols: %s.", filename,
	     bfd_errmsg (bfd_get_error ()));
    }

  /* Sum the sizes of the various sections that compose debug info. */
  for (i = 0; pa64_debug_section_names[i] != NULL; i++)
    {
      asection *sect;

      sect = bfd_get_section_by_name (abfd, pa64_debug_section_names[i]);
      if (sect)
	st_size += (LONGEST)bfd_section_size (abfd, sect);
    }

  bfd_close (abfd);
  xfree (filename);

  /* Unfortunately, just summing the sizes of various debug info
     sections isn't a very accurate measurement of how much heap
     space the debugger will need to hold them.  It also doesn't
     account for space needed by linker (aka "minimal") symbols.

     Anecdotal evidence suggests that just summing the sizes of
     debug-info-related sections understates the heap space needed
     to represent it internally by about an order of magnitude.

     Since it's not exactly brain surgery we're doing here, rather
     than attempt to more accurately measure the size of a shlib's
     symbol table in GDB's heap, we'll just apply a 10x fudge-
     factor to the debug info sections' size-sum.  No, this doesn't
     account for minimal symbols in non-debuggable shlibs.  But it
     all roughly washes out in the end.  */
  return st_size * (LONGEST) 10;
}

/* Add a shared library to the objfile list and load its symbols into
   GDB's symbol table.  */
static void
pa64_solib_add_solib_objfile (struct so_list *so, char *name, int from_tty,
			      CORE_ADDR text_addr)
{
  bfd *tmp_bfd;
  asection *sec;
  obj_private_data_t *obj_private;
  struct section_addr_info *section_addrs;
  struct cleanup *my_cleanups;

  /* We need the BFD so that we can look at its sections.  We open up the
     file temporarily, then close it when we are done.  */
  tmp_bfd = bfd_openr (name, gnutarget);
  if (tmp_bfd == NULL)
    {
      perror_with_name (name);
      return;
    }

  if (!bfd_check_format (tmp_bfd, bfd_object))
    {
      bfd_close (tmp_bfd);
      error ("\"%s\" is not an object file: %s", name,
	     bfd_errmsg (bfd_get_error ()));
    }


  /* Undo some braindamage from symfile.c.

     First, symfile.c will subtract the VMA of the first .text section
     in the shared library that it finds.  Undo that.  */
  sec = bfd_get_section_by_name (tmp_bfd, ".text");
  text_addr += bfd_section_vma (tmp_bfd, sec);

  /* Now find the true lowest section in the shared library.  */
  sec = NULL;
  bfd_map_over_sections (tmp_bfd, find_lowest_section, &sec);

  if (sec)
    {
      /* Subtract out the VMA of the lowest section.  */
      text_addr -= bfd_section_vma (tmp_bfd, sec);

      /* ??? Add back in the filepos of that lowest section. */
      text_addr += sec->filepos;
    }

  section_addrs = alloc_section_addr_info (bfd_count_sections (tmp_bfd));
  my_cleanups = make_cleanup (xfree, section_addrs);

  /* We are done with the temporary bfd.  Get rid of it and make sure
     nobody else can us it.  */
  bfd_close (tmp_bfd);
  tmp_bfd = NULL;

  /* Now let the generic code load up symbols for this library.  */
  section_addrs->other[0].addr = text_addr;
  section_addrs->other[0].name = ".text";
  so->objfile = symbol_file_add (name, from_tty, section_addrs, 0, OBJF_SHARED);
  so->abfd = so->objfile->obfd;

  /* Mark this as a shared library and save private data.  */
  so->objfile->flags |= OBJF_SHARED;

  if (so->objfile->obj_private == NULL)
    {
      obj_private = (obj_private_data_t *)
	obstack_alloc (&so->objfile->objfile_obstack,
		       sizeof (obj_private_data_t));
      obj_private->unwind_info = NULL;
      obj_private->so_info = NULL;
      so->objfile->obj_private = obj_private;
    }

  obj_private = (obj_private_data_t *) so->objfile->obj_private;
  obj_private->so_info = so;
  obj_private->dp = so->pa64_solib_desc.linkage_ptr;
  do_cleanups (my_cleanups);
}

/* Load debugging information for a shared library.  TARGET may be
   NULL if we are not attaching to a process or reading a core file.  */

static void
pa64_solib_load_symbols (struct so_list *so, char *name, int from_tty,
			 CORE_ADDR text_addr, struct target_ops *target)
{
  struct section_table *p;
  asection *sec;
  int status;
  char buf[4];
  CORE_ADDR presumed_data_start;

  if (text_addr == 0)
    text_addr = so->pa64_solib_desc.text_base;

  pa64_solib_add_solib_objfile (so, name, from_tty, text_addr);

  /* Now we need to build a section table for this library since
     we might be debugging a core file from a dynamically linked
     executable in which the libraries were not privately mapped.  */
  if (build_section_table (so->abfd,
			   &so->sections,
			   &so->sections_end))
    {
      error ("Unable to build section table for shared library\n.");
      return;
    }

  (so->objfile->section_offsets)->offsets[SECT_OFF_TEXT (so->objfile)]
    = so->pa64_solib_desc.text_base;
  (so->objfile->section_offsets)->offsets[SECT_OFF_DATA (so->objfile)]
    = so->pa64_solib_desc.data_base;

  /* Relocate all the sections based on where they got loaded.  */
  for (p = so->sections; p < so->sections_end; p++)
    {
      if (p->the_bfd_section->flags & SEC_CODE)
	{
	  p->addr += ANOFFSET (so->objfile->section_offsets, SECT_OFF_TEXT (so->objfile));
	  p->endaddr += ANOFFSET (so->objfile->section_offsets, SECT_OFF_TEXT (so->objfile));
	}
      else if (p->the_bfd_section->flags & SEC_DATA)
	{
	  p->addr += ANOFFSET (so->objfile->section_offsets, SECT_OFF_DATA (so->objfile));
	  p->endaddr += ANOFFSET (so->objfile->section_offsets, SECT_OFF_DATA (so->objfile));
	}
    }

  /* Now see if we need to map in the text and data for this shared
     library (for example debugging a core file which does not use
     private shared libraries.). 

     Carefully peek at the first text address in the library.  If the
     read succeeds, then the libraries were privately mapped and were
     included in the core dump file.

     If the peek failed, then the libraries were not privately mapped
     and are not in the core file, we'll have to read them in ourselves.  */
  status = target_read_memory (text_addr, buf, 4);
  if (status != 0)
    {
      int new, old;
      
      new = so->sections_end - so->sections;

      old = target_resize_to_sections (target, new);
      
      /* Copy over the old data before it gets clobbered.  */
      memcpy ((char *) (target->to_sections + old),
	      so->sections,
	      ((sizeof (struct section_table)) * new));
    }
}


/* Add symbols from shared libraries into the symtab list, unless the
   size threshold specified by auto_solib_limit (in megabytes) would
   be exceeded.  */

void
pa64_solib_add (char *arg_string, int from_tty, struct target_ops *target, int readsyms)
{
  struct minimal_symbol *msymbol;
  CORE_ADDR addr;
  asection *shlib_info;
  int status;
  unsigned int dld_flags;
  char buf[4], *re_err;
  int threshold_warning_given = 0;
  int dll_index;
  struct load_module_desc dll_desc;
  char *dll_path;

  /* First validate our arguments.  */
  if ((re_err = re_comp (arg_string ? arg_string : ".")) != NULL)
    {
      error ("Invalid regexp: %s", re_err);
    }

  /* If we're debugging a core file, or have attached to a running
     process, then pa64_solib_create_inferior_hook will not have been
     called.

     We need to first determine if we're dealing with a dynamically
     linked executable.  If not, then return without an error or warning.

     We also need to examine __dld_flags to determine if the shared library
     list is valid and to determine if the libraries have been privately
     mapped.  */
  if (symfile_objfile == NULL)
    return;

  /* First see if the objfile was dynamically linked.  */
  shlib_info = bfd_get_section_by_name (symfile_objfile->obfd, ".dynamic");
  if (!shlib_info)
    return;

  /* It's got a .dynamic section, make sure it's not empty.  */
  if (bfd_section_size (symfile_objfile->obfd, shlib_info) == 0)
    return;

  /* Read in the load map pointer if we have not done so already.  */
  if (! dld_cache.have_read_dld_descriptor)
    if (! read_dld_descriptor (target, readsyms))
      return;

  /* If the libraries were not mapped private, warn the user.  */
  if ((dld_cache.dld_flags & DT_HP_DEBUG_PRIVATE) == 0)
    warning ("The shared libraries were not privately mapped; setting a\nbreakpoint in a shared library will not work until you rerun the program.\n");

  /* For each shaerd library, add it to the shared library list.  */
  for (dll_index = 1; ; dll_index++)
    {
      /* Read in the load module descriptor.  */
      if (dlgetmodinfo (dll_index, &dll_desc, sizeof (dll_desc),
			pa64_target_read_memory, 0, dld_cache.load_map)
	  == 0)
	return;

      /* Get the name of the shared library.  */
      dll_path = (char *)dlgetname (&dll_desc, sizeof (dll_desc),
			    pa64_target_read_memory,
			    0, dld_cache.load_map);

      if (!dll_path)
	error ("pa64_solib_add, unable to read shared library path.");

      add_to_solist (from_tty, dll_path, readsyms, &dll_desc, 0, target);
    }
}


/* This hook gets called just before the first instruction in the
   inferior process is executed.

   This is our opportunity to set magic flags in the inferior so
   that GDB can be notified when a shared library is mapped in and
   to tell the dynamic linker that a private copy of the library is
   needed (so GDB can set breakpoints in the library).

   We need to set two flag bits in this routine.

     DT_HP_DEBUG_PRIVATE to indicate that shared libraries should be
     mapped private.

     DT_HP_DEBUG_CALLBACK to indicate that we want the dynamic linker to
     call the breakpoint routine for significant events.  */

void
pa64_solib_create_inferior_hook (void)
{
  struct minimal_symbol *msymbol;
  unsigned int dld_flags, status;
  asection *shlib_info, *interp_sect;
  char buf[4];
  struct objfile *objfile;
  CORE_ADDR anaddr;

  /* First, remove all the solib event breakpoints.  Their addresses
     may have changed since the last time we ran the program.  */
  remove_solib_event_breakpoints ();

  if (symfile_objfile == NULL)
    return;

  /* First see if the objfile was dynamically linked.  */
  shlib_info = bfd_get_section_by_name (symfile_objfile->obfd, ".dynamic");
  if (!shlib_info)
    return;

  /* It's got a .dynamic section, make sure it's not empty.  */
  if (bfd_section_size (symfile_objfile->obfd, shlib_info) == 0)
    return;

  /* Read in the .dynamic section.  */
  if (! read_dynamic_info (shlib_info, &dld_cache))
    error ("Unable to read the .dynamic section.");

  /* Turn on the flags we care about.  */
  dld_cache.dld_flags |= DT_HP_DEBUG_PRIVATE;
  dld_cache.dld_flags |= DT_HP_DEBUG_CALLBACK;
  status = target_write_memory (dld_cache.dld_flags_addr,
				(char *) &dld_cache.dld_flags,
				sizeof (dld_cache.dld_flags));
  if (status != 0)
    error ("Unable to modify dynamic linker flags.");

  /* Now we have to create a shared library breakpoint in the dynamic
     linker.  This can be somewhat tricky since the symbol is inside
     the dynamic linker (for which we do not have symbols or a base
     load address!   Luckily I wrote this code for solib.c years ago.  */
  interp_sect = bfd_get_section_by_name (exec_bfd, ".interp");
  if (interp_sect)
    {
      unsigned int interp_sect_size;
      char *buf;
      CORE_ADDR load_addr;
      bfd *tmp_bfd;
      CORE_ADDR sym_addr = 0;

      /* Read the contents of the .interp section into a local buffer;
	 the contents specify the dynamic linker this program uses.  */
      interp_sect_size = bfd_section_size (exec_bfd, interp_sect);
      buf = alloca (interp_sect_size);
      bfd_get_section_contents (exec_bfd, interp_sect,
				buf, 0, interp_sect_size);

      /* Now we need to figure out where the dynamic linker was
	 loaded so that we can load its symbols and place a breakpoint
	 in the dynamic linker itself.

	 This address is stored on the stack.  However, I've been unable
	 to find any magic formula to find it for Solaris (appears to
	 be trivial on GNU/Linux).  Therefore, we have to try an alternate
	 mechanism to find the dynamic linker's base address.  */
      tmp_bfd = bfd_openr (buf, gnutarget);
      if (tmp_bfd == NULL)
	goto get_out;

      /* Make sure the dynamic linker's really a useful object.  */
      if (!bfd_check_format (tmp_bfd, bfd_object))
	{
	  warning ("Unable to grok dynamic linker %s as an object file", buf);
	  bfd_close (tmp_bfd);
	  goto get_out;
	}

      /* We find the dynamic linker's base address by examining the
	 current pc (which point at the entry point for the dynamic
	 linker) and subtracting the offset of the entry point. 

	 Also note the breakpoint is the second instruction in the
	 routine.  */
      load_addr = read_pc () - tmp_bfd->start_address;
      sym_addr = bfd_lookup_symbol (tmp_bfd, "__dld_break");
      sym_addr = load_addr + sym_addr + 4;
      
      /* Create the shared library breakpoint.  */
      {
	struct breakpoint *b
	  = create_solib_event_breakpoint (sym_addr);

	/* The breakpoint is actually hard-coded into the dynamic linker,
	   so we don't need to actually insert a breakpoint instruction
	   there.  In fact, the dynamic linker's code is immutable, even to
	   ttrace, so we shouldn't even try to do that.  For cases like
	   this, we have "permanent" breakpoints.  */
	make_breakpoint_permanent (b);
      }

      /* We're done with the temporary bfd.  */
      bfd_close (tmp_bfd);
    }

get_out:
  /* Wipe out all knowledge of old shared libraries since their
     mapping can change from one exec to another!  */
  while (so_list_head)
    {
      struct so_list *temp;

      temp = so_list_head;
      xfree (so_list_head);
      so_list_head = temp->next;
    }
  clear_symtab_users ();
}

/* This operation removes the "hook" between GDB and the dynamic linker,
   which causes the dld to notify GDB of shared library events.

   After this operation completes, the dld will no longer notify GDB of
   shared library events.  To resume notifications, GDB must call
   pa64_solib_create_inferior_hook.

   This operation does not remove any knowledge of shared libraries which
   GDB may already have been notified of.  */

void
pa64_solib_remove_inferior_hook (int pid)
{
  /* Turn off the DT_HP_DEBUG_CALLBACK bit in the dynamic linker flags.  */
  dld_cache.dld_flags &= ~DT_HP_DEBUG_CALLBACK;
  target_write_memory (dld_cache.dld_flags_addr,
		       (char *)&dld_cache.dld_flags,
		       sizeof (dld_cache.dld_flags));
}

/* This function creates a breakpoint on the dynamic linker hook, which
   is called when e.g., a shl_load or shl_unload call is made.  This
   breakpoint will only trigger when a shl_load call is made.

   If filename is NULL, then loads of any dll will be caught.  Else,
   only loads of the file whose pathname is the string contained by
   filename will be caught.

   Undefined behaviour is guaranteed if this function is called before
   pa64_solib_create_inferior_hook.  */

void
pa64_solib_create_catch_load_hook (int pid, int tempflag, char *filename,
				   char *cond_string)
{
  create_solib_load_event_breakpoint ("", tempflag, filename, cond_string);
}

/* This function creates a breakpoint on the dynamic linker hook, which
   is called when e.g., a shl_load or shl_unload call is made.  This
   breakpoint will only trigger when a shl_unload call is made.

   If filename is NULL, then unloads of any dll will be caught.  Else,
   only unloads of the file whose pathname is the string contained by
   filename will be caught.

   Undefined behaviour is guaranteed if this function is called before
   pa64_solib_create_inferior_hook.  */

void
pa64_solib_create_catch_unload_hook (int pid, int tempflag, char *filename,
				     char *cond_string)
{
  create_solib_unload_event_breakpoint ("", tempflag, filename, cond_string);
}

/* Return nonzero if the dynamic linker has reproted that a library
   has been loaded.  */

int
pa64_solib_have_load_event (int pid)
{
  CORE_ADDR event_kind;

  event_kind = read_register (ARG0_REGNUM);
  return (event_kind == DLD_CB_LOAD);
}

/* Return nonzero if the dynamic linker has reproted that a library
   has been unloaded.  */
int
pa64_solib_have_unload_event (int pid)
{
  CORE_ADDR event_kind;

  event_kind = read_register (ARG0_REGNUM);
  return (event_kind == DLD_CB_UNLOAD);
}

/* Return a pointer to a string indicating the pathname of the most
   recently loaded library.

   The caller is reposible for copying the string before the inferior is
   restarted.  */

char *
pa64_solib_loaded_library_pathname (int pid)
{
  static char dll_path[MAXPATHLEN];
  CORE_ADDR  dll_path_addr = read_register (ARG3_REGNUM);
  read_memory_string (dll_path_addr, dll_path, MAXPATHLEN);
  return dll_path;
}

/* Return a pointer to a string indicating the pathname of the most
   recently unloaded library.

   The caller is reposible for copying the string before the inferior is
   restarted.  */

char *
pa64_solib_unloaded_library_pathname (int pid)
{
  static char dll_path[MAXPATHLEN];
  CORE_ADDR dll_path_addr = read_register (ARG3_REGNUM);
  read_memory_string (dll_path_addr, dll_path, MAXPATHLEN);
  return dll_path;
}

/* Return nonzero if PC is an address inside the dynamic linker.  */

int
pa64_solib_in_dynamic_linker (int pid, CORE_ADDR pc)
{
  asection *shlib_info;

  if (symfile_objfile == NULL)
    return 0;

  if (!dld_cache.have_read_dld_descriptor)
    if (!read_dld_descriptor (&current_target, auto_solib_add))
      return 0;

  return (pc >= dld_cache.dld_desc.text_base
	  && pc < dld_cache.dld_desc.text_base + dld_cache.dld_desc.text_size);
}


/* Return the GOT value for the shared library in which ADDR belongs.  If
   ADDR isn't in any known shared library, return zero.  */

CORE_ADDR
pa64_solib_get_got_by_pc (CORE_ADDR addr)
{
  struct so_list *so_list = so_list_head;
  CORE_ADDR got_value = 0;

  while (so_list)
    {
      if (so_list->pa64_solib_desc.text_base <= addr
	  && ((so_list->pa64_solib_desc.text_base
	       + so_list->pa64_solib_desc.text_size)
	      > addr))
	{
	  got_value = so_list->pa64_solib_desc.linkage_ptr;
	  break;
	}
      so_list = so_list->next;
    }
  return got_value;
}

/* Return the address of the handle of the shared library in which ADDR
   belongs.  If ADDR isn't in any known shared library, return zero. 

   This function is used in hppa_fix_call_dummy in hppa-tdep.c.  */

CORE_ADDR
pa64_solib_get_solib_by_pc (CORE_ADDR addr)
{
  struct so_list *so_list = so_list_head;
  CORE_ADDR retval = 0;

  while (so_list)
    {
      if (so_list->pa64_solib_desc.text_base <= addr
	  && ((so_list->pa64_solib_desc.text_base
	       + so_list->pa64_solib_desc.text_size)
	      > addr))
	{
	  retval = so_list->pa64_solib_desc_addr;
	  break;
	}
      so_list = so_list->next;
    }
  return retval;
}

/* Dump information about all the currently loaded shared libraries.  */

static void
pa64_sharedlibrary_info_command (char *ignore, int from_tty)
{
  struct so_list *so_list = so_list_head;

  if (exec_bfd == NULL)
    {
      printf_unfiltered ("No executable file.\n");
      return;
    }

  if (so_list == NULL)
    {
      printf_unfiltered ("No shared libraries loaded at this time.\n");
      return;
    }

  printf_unfiltered ("Shared Object Libraries\n");
  printf_unfiltered ("   %-19s%-19s%-19s%-19s\n",
		     "  text start", "   text end",
		     "  data start", "   data end");
  while (so_list)
    {
      unsigned int flags;

      printf_unfiltered ("%s", so_list->name);
      if (so_list->objfile == NULL)
	printf_unfiltered ("  (symbols not loaded)");
      if (so_list->loaded == 0)
	printf_unfiltered ("  (shared library unloaded)");
      printf_unfiltered ("  %-18s",
	local_hex_string_custom (so_list->pa64_solib_desc.linkage_ptr,
				 "016l"));
      printf_unfiltered ("\n");
      printf_unfiltered ("%-18s",
	local_hex_string_custom (so_list->pa64_solib_desc.text_base,
				 "016l"));
      printf_unfiltered (" %-18s",
	local_hex_string_custom ((so_list->pa64_solib_desc.text_base
				  + so_list->pa64_solib_desc.text_size),
				 "016l"));
      printf_unfiltered (" %-18s",
	local_hex_string_custom (so_list->pa64_solib_desc.data_base,
				 "016l"));
      printf_unfiltered (" %-18s\n",
	local_hex_string_custom ((so_list->pa64_solib_desc.data_base
				  + so_list->pa64_solib_desc.data_size),
				 "016l"));
      so_list = so_list->next;
    }
}

/* Load up one or more shared libraries as directed by the user.  */

static void
pa64_solib_sharedlibrary_command (char *args, int from_tty)
{
  dont_repeat ();
  pa64_solib_add (args, from_tty, (struct target_ops *) 0, 1);
}

/* Return the name of the shared library containing ADDR or NULL if ADDR
   is not contained in any known shared library.  */

char *
pa64_solib_address (CORE_ADDR addr)
{
  struct so_list *so = so_list_head;

  while (so)
    {
      /* Is this address within this shlib's text range?  If so,
	 return the shlib's name.  */
      if (addr >= so->pa64_solib_desc.text_base
	  && addr < (so->pa64_solib_desc.text_base
		     | so->pa64_solib_desc.text_size))
	return so->name;

      /* Nope, keep looking... */
      so = so->next;
    }

  /* No, we couldn't prove that the address is within a shlib. */
  return NULL;
}

/* We are killing the inferior and restarting the program.  */

void
pa64_solib_restart (void)
{
  struct so_list *sl = so_list_head;

  /* Before the shlib info vanishes, use it to disable any breakpoints
     that may still be active in those shlibs.  */
  disable_breakpoints_in_shlibs (0);

  /* Discard all the shlib descriptors.  */
  while (sl)
    {
      struct so_list *next_sl = sl->next;
      xfree (sl);
      sl = next_sl;
    }
  so_list_head = NULL;

  pa64_solib_total_st_size = (LONGEST) 0;
  pa64_solib_st_size_threshold_exceeded = 0;

  dld_cache.is_valid = 0;
  dld_cache.have_read_dld_descriptor = 0;
  dld_cache.dld_flags_addr = 0;
  dld_cache.load_map = 0;
  dld_cache.load_map_addr = 0;
  dld_cache.dld_desc.data_base = 0;
  dld_cache.dld_flags = 0;
  dld_cache.dyninfo_sect = 0;
}

void
_initialize_pa64_solib (void)
{
  add_com ("sharedlibrary", class_files, pa64_solib_sharedlibrary_command,
	   "Load shared object library symbols for files matching REGEXP.");
  add_info ("sharedlibrary", pa64_sharedlibrary_info_command,
	    "Status of loaded shared object libraries.");

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

  add_show_from_set
    (add_set_cmd ("auto-solib-limit", class_support, var_zinteger,
		  (char *) &auto_solib_limit,
		  "Set threshold (in Mb) for autoloading shared library symbols.\n\
When shared library autoloading is enabled, new libraries will be loaded\n\
only until the total size of shared library symbols exceeds this\n\
threshold in megabytes.  Is ignored when using `sharedlibrary'.",
		  &setlist),
     &showlist);

  /* ??rehrauer: On HP-UX, the kernel parameter MAXDSIZ limits how
     much data space a process can use.  We ought to be reading
     MAXDSIZ and setting auto_solib_limit to some large fraction of
     that value.  If not that, we maybe ought to be setting it smaller
     than the default for MAXDSIZ (that being 64Mb, I believe).
     However, [1] this threshold is only crudely approximated rather
     than actually measured, and [2] 50 Mbytes is too small for
     debugging gdb itself.  Thus, the arbitrary 100 figure.  */
  auto_solib_limit = 100;	/* Megabytes */

  pa64_solib_restart ();
}

/* Get some HPUX-specific data from a shared lib.  */
CORE_ADDR
so_lib_thread_start_addr (struct so_list *so)
{
  return so->pa64_solib_desc.tls_start_addr;
}

/* Read the dynamic linker's internal shared library descriptor.

   This must happen after dld starts running, so we can't do it in
   read_dynamic_info.  Record the fact that we have loaded the
   descriptor.  If the library is archive bound, then return zero, else
   return nonzero.  */

static int
read_dld_descriptor (struct target_ops *target, int readsyms)
{
  char *dll_path;
  asection *dyninfo_sect;

  /* If necessary call read_dynamic_info to extract the contents of the
     .dynamic section from the shared library.  */
  if (!dld_cache.is_valid) 
    {
      if (symfile_objfile == NULL)
	error ("No object file symbols.");

      dyninfo_sect = bfd_get_section_by_name (symfile_objfile->obfd, 
					      ".dynamic");
      if (!dyninfo_sect) 
	{
	  return 0;
	}

      if (!read_dynamic_info (dyninfo_sect, &dld_cache))
	error ("Unable to read in .dynamic section information.");
    }

  /* Read the load map pointer.  */
  if (target_read_memory (dld_cache.load_map_addr,
			  (char*) &dld_cache.load_map,
			  sizeof(dld_cache.load_map))
      != 0)
    {
      error ("Error while reading in load map pointer.");
    }

  /* Read in the dld load module descriptor */
  if (dlgetmodinfo (-1, 
		    &dld_cache.dld_desc,
		    sizeof(dld_cache.dld_desc), 
		    pa64_target_read_memory, 
		    0, 
		    dld_cache.load_map)
      == 0)
    {
      error ("Error trying to get information about dynamic linker.");
    }

  /* Indicate that we have loaded the dld descriptor.  */
  dld_cache.have_read_dld_descriptor = 1;

  /* Add dld.sl to the list of known shared libraries so that we can
     do unwind, etc. 

     ?!? This may not be correct.  Consider of dld.sl contains symbols
     which are also referenced/defined by the user program or some user
     shared library.  We need to make absolutely sure that we do not
     pollute the namespace from GDB's point of view.  */
  dll_path = dlgetname (&dld_cache.dld_desc, 
			sizeof(dld_cache.dld_desc), 
			pa64_target_read_memory, 
			0, 
			dld_cache.load_map);
  add_to_solist(0, dll_path, readsyms, &dld_cache.dld_desc, 0, target);
  
  return 1;
}

/* Read the .dynamic section and extract the information of interest,
   which is stored in dld_cache.  The routine elf_locate_base in solib.c 
   was used as a model for this.  */

static int
read_dynamic_info (asection *dyninfo_sect, dld_cache_t *dld_cache_p)
{
  char *buf;
  char *bufend;
  CORE_ADDR dyninfo_addr;
  int dyninfo_sect_size;
  CORE_ADDR entry_addr;

  /* Read in .dynamic section, silently ignore errors.  */
  dyninfo_addr = bfd_section_vma (symfile_objfile->obfd, dyninfo_sect);
  dyninfo_sect_size = bfd_section_size (exec_bfd, dyninfo_sect);
  buf = alloca (dyninfo_sect_size);
  if (target_read_memory (dyninfo_addr, buf, dyninfo_sect_size))
    return 0;

  /* Scan the .dynamic section and record the items of interest. 
     In particular, DT_HP_DLD_FLAGS */
  for (bufend = buf + dyninfo_sect_size, entry_addr = dyninfo_addr;
       buf < bufend;
       buf += sizeof (Elf64_Dyn), entry_addr += sizeof (Elf64_Dyn))
    {
      Elf64_Dyn *x_dynp = (Elf64_Dyn*)buf;
      Elf64_Sxword dyn_tag;
      CORE_ADDR	dyn_ptr;
      char *pbuf;

      pbuf = alloca (TARGET_PTR_BIT / HOST_CHAR_BIT);
      dyn_tag = bfd_h_get_64 (symfile_objfile->obfd, 
			      (bfd_byte*) &x_dynp->d_tag);

      /* We can't use a switch here because dyn_tag is 64 bits and HP's
	 lame comiler does not handle 64bit items in switch statements.  */
      if (dyn_tag == DT_NULL)
	break;
      else if (dyn_tag == DT_HP_DLD_FLAGS)
	{
	  /* Set dld_flags_addr and dld_flags in *dld_cache_p */
	  dld_cache_p->dld_flags_addr = entry_addr + offsetof(Elf64_Dyn, d_un);
	  if (target_read_memory (dld_cache_p->dld_flags_addr,
	  			  (char*) &dld_cache_p->dld_flags, 
				  sizeof(dld_cache_p->dld_flags))
	      != 0)
	    {
	      error ("Error while reading in .dynamic section of the program.");
	    }
	}
      else if (dyn_tag == DT_HP_LOAD_MAP)
	{
	  /* Dld will place the address of the load map at load_map_addr
	     after it starts running.  */
	  if (target_read_memory (entry_addr + offsetof(Elf64_Dyn, 
							d_un.d_ptr),
				  (char*) &dld_cache_p->load_map_addr,
				  sizeof(dld_cache_p->load_map_addr))
	      != 0)
	    {
	      error ("Error while reading in .dynamic section of the program.");
	    }
	}
      else 
	{
	  /* tag is not of interest */
	}
    }

  /* Record other information and set is_valid to 1. */
  dld_cache_p->dyninfo_sect = dyninfo_sect;

  /* Verify that we read in required info.  These fields are re-set to zero
     in pa64_solib_restart.  */

  if (dld_cache_p->dld_flags_addr != 0 && dld_cache_p->load_map_addr != 0) 
    dld_cache_p->is_valid = 1;
  else 
    return 0;

  return 1;
}

/* Wrapper for target_read_memory to make dlgetmodinfo happy.  */

static void *
pa64_target_read_memory (void *buffer, CORE_ADDR ptr, size_t bufsiz, int ident)
{
  if (target_read_memory (ptr, buffer, bufsiz) != 0)
    return 0;
  return buffer;
}

/* Called from handle_dynlink_load_event and pa64_solib_add to add
   a shared library to so_list_head list and possibly to read in the
   debug information for the library.  

   If load_module_desc_p is NULL, then the load module descriptor must
   be read from the inferior process at the address load_module_desc_addr.  */

static void
add_to_solist (int from_tty, char *dll_path, int readsyms,
	       struct load_module_desc *load_module_desc_p,
	       CORE_ADDR load_module_desc_addr, struct target_ops *target)
{
  struct so_list *new_so, *so_list_tail;
  int pa64_solib_st_size_threshhold_exceeded;
  LONGEST st_size;

  if (symfile_objfile == NULL)
    return;

  so_list_tail = so_list_head;
  /* Find the end of the list of shared objects.  */
  while (so_list_tail && so_list_tail->next)
    {
      if (strcmp (so_list_tail->name, dll_path) == 0)
	return;
      so_list_tail = so_list_tail->next;
    }

  if (so_list_tail && strcmp (so_list_tail->name, dll_path) == 0)
    return;

  /* Add the shared library to the so_list_head list */
  new_so = (struct so_list *) xmalloc (sizeof (struct so_list));
  memset ((char *)new_so, 0, sizeof (struct so_list));
  if (so_list_head == NULL)
    {
      so_list_head = new_so;
      so_list_tail = new_so;
    }
  else
    {
      so_list_tail->next = new_so;
      so_list_tail = new_so;
    }

  /* Initialize the new_so */
  if (load_module_desc_p)
    {
      new_so->pa64_solib_desc = *load_module_desc_p;
    }
  else
    {
      if (target_read_memory (load_module_desc_addr, 
			      (char*) &new_so->pa64_solib_desc,
			      sizeof(struct load_module_desc))
	  != 0)
      {
	error ("Error while reading in dynamic library %s", dll_path);
      }
    }
  
  new_so->pa64_solib_desc_addr = load_module_desc_addr;
  new_so->loaded = 1;
  new_so->name = obsavestring (dll_path, strlen(dll_path),
			       &symfile_objfile->objfile_obstack);

  /* If we are not going to load the library, tell the user if we
     haven't already and return.  */

  st_size = pa64_solib_sizeof_symbol_table (dll_path);
  pa64_solib_st_size_threshhold_exceeded =
       !from_tty 
    && readsyms
    && (  (st_size + pa64_solib_total_st_size) 
	> (auto_solib_limit * (LONGEST) (1024 * 1024)));
  if (pa64_solib_st_size_threshhold_exceeded)
    {
      pa64_solib_add_solib_objfile (new_so, dll_path, from_tty, 1);
      return;
    } 

  /* Now read in debug info. */
  pa64_solib_total_st_size += st_size;

  /* This fills in new_so->objfile, among others. */
  pa64_solib_load_symbols (new_so, 
			   dll_path,
			   from_tty, 
			   0,
			   target);
  return;
}


/*
   LOCAL FUNCTION

   bfd_lookup_symbol -- lookup the value for a specific symbol

   SYNOPSIS

   CORE_ADDR bfd_lookup_symbol (bfd *abfd, char *symname)

   DESCRIPTION

   An expensive way to lookup the value of a single symbol for
   bfd's that are only temporary anyway.  This is used by the
   shared library support to find the address of the debugger
   interface structures in the shared library.

   Note that 0 is specifically allowed as an error return (no
   such symbol).
 */

static CORE_ADDR
bfd_lookup_symbol (bfd *abfd, char *symname)
{
  unsigned int storage_needed;
  asymbol *sym;
  asymbol **symbol_table;
  unsigned int number_of_symbols;
  unsigned int i;
  struct cleanup *back_to;
  CORE_ADDR symaddr = 0;

  storage_needed = bfd_get_symtab_upper_bound (abfd);

  if (storage_needed > 0)
    {
      symbol_table = (asymbol **) xmalloc (storage_needed);
      back_to = make_cleanup (xfree, symbol_table);
      number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);

      for (i = 0; i < number_of_symbols; i++)
	{
	  sym = *symbol_table++;
	  if (strcmp (sym->name, symname) == 0)
	    {
	      /* Bfd symbols are section relative. */
	      symaddr = sym->value + sym->section->vma;
	      break;
	    }
	}
      do_cleanups (back_to);
    }
  return (symaddr);
}

