/* Handle HP SOM shared libraries for GDB, the GNU Debugger.

   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001, 2002,
   2003, 2004 Free Software Foundation, Inc.

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

   Written by the Center for Software Science at the Univerity of Utah
   and by Cygnus Support.  */


#include "defs.h"

#include "frame.h"
#include "bfd.h"
#include "som.h"
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
#include "gdb_assert.h"
#include "exec.h"

#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Uncomment this to turn on some debugging output.
 */

/* #define SOLIB_DEBUG
 */

/* This lives in hppa-tdep.c. */
extern struct unwind_table_entry *find_unwind_entry (CORE_ADDR pc);

/* These ought to be defined in some public interface, but aren't.  They
   define the meaning of the various bits in the distinguished __dld_flags
   variable that is declared in every debuggable a.out on HP-UX, and that
   is shared between the debugger and the dynamic linker.
 */
#define DLD_FLAGS_MAPPRIVATE    0x1
#define DLD_FLAGS_HOOKVALID     0x2
#define DLD_FLAGS_LISTVALID     0x4
#define DLD_FLAGS_BOR_ENABLE    0x8

/* TODO:

   * Support for hpux8 dynamic linker.  */

/* The basic structure which describes a dynamically loaded object.  This
   data structure is private to the dynamic linker and isn't found in
   any HPUX include file.  */

struct som_solib_mapped_entry
  {
    /* The name of the library.  */
    char *name;

    /* Version of this structure (it is expected to change again in hpux10).  */
    unsigned char struct_version;

    /* Binding mode for this library.  */
    unsigned char bind_mode;

    /* Version of this library.  */
    short library_version;

    /* Start of text address,
     * link-time text location (length of text area),
     * end of text address.  */
    CORE_ADDR text_addr;
    CORE_ADDR text_link_addr;
    CORE_ADDR text_end;

    /* Start of data, start of bss and end of data.  */
    CORE_ADDR data_start;
    CORE_ADDR bss_start;
    CORE_ADDR data_end;

    /* Value of linkage pointer (%r19).  */
    CORE_ADDR got_value;

    /* Next entry.  */
    struct som_solib_mapped_entry *next;

    /* There are other fields, but I don't have information as to what is
       contained in them.  */

    /* For versions from HPUX-10.30 and up */

    /* Address in target of offset from thread-local register of
     * start of this thread's data.  I.e., the first thread-local
     * variable in this shared library starts at *(tsd_start_addr)
     * from that area pointed to by cr27 (mpsfu_hi).
     *
     * We do the indirection as soon as we read it, so from then
     * on it's the offset itself.
     */
    CORE_ADDR tsd_start_addr;

    /* Following this are longwords holding:

     * ?, ?, ?, ptr to -1, ptr to-1, ptr to lib name (leaf name),
     * ptr to __data_start, ptr to __data_end
     */


  };

/* A structure to keep track of all the known shared objects.  */
struct so_list
  {
    struct som_solib_mapped_entry som_solib;
    struct objfile *objfile;
    bfd *abfd;
    struct section_table *sections;
    struct section_table *sections_end;
/* elz: added this field to store the address in target space (in the
   library) of the library descriptor (handle) which we read into
   som_solib_mapped_entry structure */
    CORE_ADDR solib_addr;
    struct so_list *next;

  };

static struct so_list *so_list_head;


/* This is the cumulative size in bytes of the symbol tables of all
   shared objects on the so_list_head list.  (When we say size, here
   we mean of the information before it is brought into memory and
   potentially expanded by GDB.)  When adding a new shlib, this value
   is compared against the threshold size, held by auto_solib_limit
   (in megabytes).  If adding symbols for the new shlib would cause
   the total size to exceed the threshold, then the new shlib's
   symbols are not loaded.  */
static LONGEST som_solib_total_st_size;

/* When the threshold is reached for any shlib, we refuse to add
   symbols for subsequent shlibs, even if those shlibs' symbols would
   be small enough to fit under the threshold.  (Although this may
   result in one, early large shlib preventing the loading of later,
   smalller shlibs' symbols, it allows us to issue one informational
   message.  The alternative, to issue a message for each shlib whose
   symbols aren't loaded, could be a big annoyance where the threshold
   is exceeded due to a very large number of shlibs.)
 */
static int som_solib_st_size_threshold_exceeded;

/* These addresses should be filled in by som_solib_create_inferior_hook.
   They are also used elsewhere in this module.
 */
typedef struct
  {
    CORE_ADDR address;
    struct unwind_table_entry *unwind;
  }
addr_and_unwind_t;

/* When adding fields, be sure to clear them in _initialize_som_solib. */
static struct
  {
    int is_valid;
    addr_and_unwind_t hook;
    addr_and_unwind_t hook_stub;
    addr_and_unwind_t load;
    addr_and_unwind_t load_stub;
    addr_and_unwind_t unload;
    addr_and_unwind_t unload2;
    addr_and_unwind_t unload_stub;
  }
dld_cache;



static void som_sharedlibrary_info_command (char *, int);

static void som_solib_sharedlibrary_command (char *, int);

static LONGEST
som_solib_sizeof_symbol_table (char *filename)
{
  bfd *abfd;
  int desc;
  char *absolute_name;
  LONGEST st_size = (LONGEST) 0;
  asection *sect;

  /* We believe that filename was handed to us by the dynamic linker, and
     is therefore always an absolute path.
   */
  desc = openp (getenv ("PATH"), 1, filename, O_RDONLY | O_BINARY, 0, &absolute_name);
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

  if (!bfd_check_format (abfd, bfd_object))	/* Reads in section info */
    {
      bfd_close (abfd);		/* This also closes desc */
      make_cleanup (xfree, filename);
      error ("\"%s\": can't read symbols: %s.", filename,
	     bfd_errmsg (bfd_get_error ()));
    }

  /* Sum the sizes of the various sections that compose debug info. */

  /* This contains non-DOC information. */
  sect = bfd_get_section_by_name (abfd, "$DEBUG$");
  if (sect)
    st_size += (LONGEST) bfd_section_size (abfd, sect);

  /* This contains DOC information. */
  sect = bfd_get_section_by_name (abfd, "$PINFO$");
  if (sect)
    st_size += (LONGEST) bfd_section_size (abfd, sect);

  bfd_close (abfd);		/* This also closes desc */
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
     all roughly washes out in the end.
   */
  return st_size * (LONGEST) 10;
}


static void
som_solib_add_solib_objfile (struct so_list *so, char *name, int from_tty,
			     CORE_ADDR text_addr)
{
  obj_private_data_t *obj_private;
  struct obj_section *s;

  so->objfile = symbol_file_add (name, from_tty, NULL, 0, OBJF_SHARED);
  so->abfd = so->objfile->obfd;

  /* syms_from_objfile has bizarre section offset code,
     so I do my own right here.  */
  for (s = so->objfile->sections; s < so->objfile->sections_end; s++)
    {
      flagword aflag = bfd_get_section_flags(so->abfd, s->the_bfd_section);
      if (aflag & SEC_CODE)
	{
	  s->addr    += so->som_solib.text_addr - so->som_solib.text_link_addr;
	  s->endaddr += so->som_solib.text_addr - so->som_solib.text_link_addr;
	}
      else if (aflag & SEC_DATA)
	{
	  s->addr    += so->som_solib.data_start;
	  s->endaddr += so->som_solib.data_start;
	}
      else
	;
    }
   
  /* Mark this as a shared library and save private data.
   */
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

  if (!bfd_check_format (so->abfd, bfd_object))
    {
      error ("\"%s\": not in executable format: %s.",
	     name, bfd_errmsg (bfd_get_error ()));
    }
}


static void
som_solib_load_symbols (struct so_list *so, char *name, int from_tty,
			CORE_ADDR text_addr, struct target_ops *target)
{
  struct section_table *p;
  int status;
  char buf[4];
  CORE_ADDR presumed_data_start;

#ifdef SOLIB_DEBUG
  printf ("--Adding symbols for shared library \"%s\"\n", name);
#endif

  som_solib_add_solib_objfile (so, name, from_tty, text_addr);

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
      int old, new;

      new = so->sections_end - so->sections;
      
      old = target_resize_to_sections (target, new);
      
      /* Copy over the old data before it gets clobbered.  */
      memcpy ((char *) (target->to_sections + old),
	      so->sections,
	      ((sizeof (struct section_table)) * new));
    }
}


/* FIXME: cagney/2003-02-01: This just isn't right.  Given an address
   within the target's address space, this converts the value into an
   address within the host's (i.e., GDB's) address space.  Given that
   the host/target address spaces are separate, this can't be right.  */

static void *
hpux_address_to_host_pointer_hack (CORE_ADDR addr)
{
  void *ptr;

  gdb_assert (sizeof (ptr) == TYPE_LENGTH (builtin_type_void_data_ptr));
  ADDRESS_TO_POINTER (builtin_type_void_data_ptr, &ptr, addr);
  return ptr;
}

/* Add symbols from shared libraries into the symtab list, unless the
   size threshold specified by auto_solib_limit (in megabytes) would
   be exceeded.  */

void
som_solib_add (char *arg_string, int from_tty, struct target_ops *target, int readsyms)
{
  struct minimal_symbol *msymbol;
  struct so_list *so_list_tail;
  CORE_ADDR addr;
  asection *shlib_info;
  int status;
  unsigned int dld_flags;
  char buf[4], *re_err;
  int threshold_warning_given = 0;

  /* First validate our arguments.  */
  re_err = re_comp (arg_string ? arg_string : ".");
  if (re_err != NULL)
    {
      error ("Invalid regexp: %s", re_err);
    }

  /* If we're debugging a core file, or have attached to a running
     process, then som_solib_create_inferior_hook will not have been
     called.

     We need to first determine if we're dealing with a dynamically
     linked executable.  If not, then return without an error or warning.

     We also need to examine __dld_flags to determine if the shared library
     list is valid and to determine if the libraries have been privately
     mapped.  */
  if (symfile_objfile == NULL)
    return;

  /* First see if the objfile was dynamically linked.  */
  shlib_info = bfd_get_section_by_name (symfile_objfile->obfd, "$SHLIB_INFO$");
  if (!shlib_info)
    return;

  /* It's got a $SHLIB_INFO$ section, make sure it's not empty.  */
  if (bfd_section_size (symfile_objfile->obfd, shlib_info) == 0)
    return;

  msymbol = lookup_minimal_symbol ("__dld_flags", NULL, NULL);
  if (msymbol == NULL)
    {
      error ("Unable to find __dld_flags symbol in object file.\n");
      return;
    }

  addr = SYMBOL_VALUE_ADDRESS (msymbol);
  /* Read the current contents.  */
  status = target_read_memory (addr, buf, 4);
  if (status != 0)
    {
      error ("Unable to read __dld_flags\n");
      return;
    }
  dld_flags = extract_unsigned_integer (buf, 4);

  /* __dld_list may not be valid.  If not, then we punt, warning the user if
     we were called as a result of the add-symfile command.
   */
  if ((dld_flags & DLD_FLAGS_LISTVALID) == 0)
    {
      if (from_tty)
	error ("__dld_list is not valid according to __dld_flags.\n");
      return;
    }

  /* If the libraries were not mapped private, warn the user.  */
  if ((dld_flags & DLD_FLAGS_MAPPRIVATE) == 0)
    warning ("The shared libraries were not privately mapped; setting a\nbreakpoint in a shared library will not work until you rerun the program.\n");

  msymbol = lookup_minimal_symbol ("__dld_list", NULL, NULL);
  if (!msymbol)
    {
      /* Older crt0.o files (hpux8) don't have __dld_list as a symbol,
         but the data is still available if you know where to look.  */
      msymbol = lookup_minimal_symbol ("__dld_flags", NULL, NULL);
      if (!msymbol)
	{
	  error ("Unable to find dynamic library list.\n");
	  return;
	}
      addr = SYMBOL_VALUE_ADDRESS (msymbol) - 8;
    }
  else
    addr = SYMBOL_VALUE_ADDRESS (msymbol);

  status = target_read_memory (addr, buf, 4);
  if (status != 0)
    {
      error ("Unable to find dynamic library list.\n");
      return;
    }

  addr = extract_unsigned_integer (buf, 4);

  /* If addr is zero, then we're using an old dynamic loader which
     doesn't maintain __dld_list.  We'll have to use a completely
     different approach to get shared library information.  */
  if (addr == 0)
    goto old_dld;

  /* Using the information in __dld_list is the preferred method
     to get at shared library information.  It doesn't depend on
     any functions in /opt/langtools/lib/end.o and has a chance of working
     with hpux10 when it is released.  */
  status = target_read_memory (addr, buf, 4);
  if (status != 0)
    {
      error ("Unable to find dynamic library list.\n");
      return;
    }

  /* addr now holds the address of the first entry in the dynamic
     library list.  */
  addr = extract_unsigned_integer (buf, 4);

  /* Now that we have a pointer to the dynamic library list, walk
     through it and add the symbols for each library.  */

  so_list_tail = so_list_head;
  /* Find the end of the list of shared objects.  */
  while (so_list_tail && so_list_tail->next)
    so_list_tail = so_list_tail->next;

#ifdef SOLIB_DEBUG
  printf ("--About to read shared library list data\n");
#endif

  /* "addr" will always point to the base of the
   * current data entry describing the current
   * shared library.
   */
  while (1)
    {
      CORE_ADDR name_addr, text_addr;
      unsigned int name_len;
      char *name;
      struct so_list *new_so;
      struct so_list *so_list = so_list_head;
      struct stat statbuf;
      LONGEST st_size;
      int is_main_program;

      if (addr == 0)
	break;

      /* Get a pointer to the name of this library.  */
      status = target_read_memory (addr, buf, 4);
      if (status != 0)
	goto err;

      name_addr = extract_unsigned_integer (buf, 4);
      name_len = 0;
      while (1)
	{
	  target_read_memory (name_addr + name_len, buf, 1);
	  if (status != 0)
	    goto err;

	  name_len++;
	  if (*buf == '\0')
	    break;
	}
      name = alloca (name_len);
      status = target_read_memory (name_addr, name, name_len);
      if (status != 0)
	goto err;

      /* See if we've already loaded something with this name.  */
      while (so_list)
	{
	  if (!strcmp (so_list->som_solib.name, name))
	    break;
	  so_list = so_list->next;
	}

      /* See if the file exists.  If not, give a warning, but don't
         die.  */
      status = stat (name, &statbuf);
      if (status == -1)
	{
	  warning ("Can't find file %s referenced in dld_list.", name);

	  status = target_read_memory (addr + 36, buf, 4);
	  if (status != 0)
	    goto err;

	  addr = (CORE_ADDR) extract_unsigned_integer (buf, 4);
	  continue;
	}

      /* If we've already loaded this one or it's the main program, skip it.  */
      is_main_program = (strcmp (name, symfile_objfile->name) == 0);
      if (so_list || is_main_program)
	{
	  /* This is the "next" pointer in the strcuture.
	   */
	  status = target_read_memory (addr + 36, buf, 4);
	  if (status != 0)
	    goto err;

	  addr = (CORE_ADDR) extract_unsigned_integer (buf, 4);

	  /* Record the main program's symbol table size. */
	  if (is_main_program && !so_list)
	    {
	      st_size = som_solib_sizeof_symbol_table (name);
	      som_solib_total_st_size += st_size;
	    }

	  /* Was this a shlib that we noted but didn't load the symbols for?
	     If so, were we invoked this time from the command-line, via
	     a 'sharedlibrary' or 'add-symbol-file' command?  If yes to
	     both, we'd better load the symbols this time.
	   */
	  if (from_tty && so_list && !is_main_program && (so_list->objfile == NULL))
	    som_solib_load_symbols (so_list,
				    name,
				    from_tty,
				    so_list->som_solib.text_addr,
				    target);

	  continue;
	}

      name = obsavestring (name, name_len - 1,
			   &symfile_objfile->objfile_obstack);

      status = target_read_memory (addr + 8, buf, 4);
      if (status != 0)
	goto err;

      text_addr = extract_unsigned_integer (buf, 4);

      new_so = (struct so_list *) xmalloc (sizeof (struct so_list));
      memset ((char *) new_so, 0, sizeof (struct so_list));
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

      /* Fill in all the entries in GDB's shared library list.
       */

      new_so->solib_addr = addr;
      new_so->som_solib.name = name;
      status = target_read_memory (addr + 4, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.struct_version = extract_unsigned_integer (buf + 3, 1);
      new_so->som_solib.bind_mode = extract_unsigned_integer (buf + 2, 1);
      /* Following is "high water mark", highest version number
       * seen, rather than plain version number.
       */
      new_so->som_solib.library_version = extract_unsigned_integer (buf, 2);
      new_so->som_solib.text_addr = text_addr;

      /* Q: What about longword at "addr + 8"?
       * A: It's read above, out of order, into "text_addr".
       */

      status = target_read_memory (addr + 12, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.text_link_addr = extract_unsigned_integer (buf, 4);

      status = target_read_memory (addr + 16, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.text_end = extract_unsigned_integer (buf, 4);

      status = target_read_memory (addr + 20, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.data_start = extract_unsigned_integer (buf, 4);

      status = target_read_memory (addr + 24, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.bss_start = extract_unsigned_integer (buf, 4);

      status = target_read_memory (addr + 28, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.data_end = extract_unsigned_integer (buf, 4);

      status = target_read_memory (addr + 32, buf, 4);
      if (status != 0)
	goto err;

      new_so->som_solib.got_value = extract_unsigned_integer (buf, 4);

      status = target_read_memory (addr + 36, buf, 4);
      if (status != 0)
	goto err;

      /* FIXME: cagney/2003-02-01: I think som_solib.next should be a
         CORE_ADDR.  */
      new_so->som_solib.next =
	hpux_address_to_host_pointer_hack (extract_unsigned_integer (buf, 4));

      /* Note that we don't re-set "addr" to the next pointer
       * until after we've read the trailing data.
       */

      status = target_read_memory (addr + 40, buf, 4);
      new_so->som_solib.tsd_start_addr = extract_unsigned_integer (buf, 4);
      if (status != 0)
	goto err;

      /* Now indirect via that value!
       */
      status = target_read_memory (new_so->som_solib.tsd_start_addr, buf, 4);
      new_so->som_solib.tsd_start_addr = extract_unsigned_integer (buf, 4);
      if (status != 0)
	goto err;
#ifdef SOLIB_DEBUG
      printf ("\n+ library \"%s\" is described at 0x%x\n", name, addr);
      printf ("  'version' is %d\n", new_so->som_solib.struct_version);
      printf ("  'bind_mode' is %d\n", new_so->som_solib.bind_mode);
      printf ("  'library_version' is %d\n", new_so->som_solib.library_version);
      printf ("  'text_addr' is 0x%x\n", new_so->som_solib.text_addr);
      printf ("  'text_link_addr' is 0x%x\n", new_so->som_solib.text_link_addr);
      printf ("  'text_end' is 0x%x\n", new_so->som_solib.text_end);
      printf ("  'data_start' is 0x%x\n", new_so->som_solib.data_start);
      printf ("  'bss_start' is 0x%x\n", new_so->som_solib.bss_start);
      printf ("  'data_end' is 0x%x\n", new_so->som_solib.data_end);
      printf ("  'got_value' is %x\n", new_so->som_solib.got_value);
      printf ("  'next' is 0x%x\n", new_so->som_solib.next);
      printf ("  'tsd_start_addr' is 0x%x\n", new_so->som_solib.tsd_start_addr);
#endif

      /* Go on to the next shared library descriptor.
       */
      addr = (CORE_ADDR) new_so->som_solib.next;



      /* At this point, we have essentially hooked the shlib into the
         "info share" command.  However, we haven't yet loaded its
         symbol table.  We must now decide whether we ought to, i.e.,
         whether doing so would exceed the symbol table size threshold.

         If the threshold has just now been exceeded, then we'll issue
         a warning message (which explains how to load symbols manually,
         if the user so desires).

         If the threshold has just now or previously been exceeded,
         we'll just add the shlib to the list of object files, but won't
         actually load its symbols.  (This is more useful than it might
         sound, for it allows us to e.g., still load and use the shlibs'
         unwind information for stack tracebacks.)
       */

      /* Note that we DON'T want to preclude the user from using the
         add-symbol-file command!  Thus, we only worry about the threshold
         when we're invoked for other reasons.
       */
      st_size = som_solib_sizeof_symbol_table (name);
      som_solib_st_size_threshold_exceeded =
	!from_tty &&
	auto_solib_limit > 0 &&
	readsyms &&
	((st_size + som_solib_total_st_size) > (auto_solib_limit * (LONGEST) (1024 * 1024)));

      if (som_solib_st_size_threshold_exceeded)
	{
	  if (!threshold_warning_given)
	    warning ("Symbols for some libraries have not been loaded, because\ndoing so would exceed the size threshold specified by auto-solib-limit.\nTo manually load symbols, use the 'sharedlibrary' command.\nTo raise the threshold, set auto-solib-limit to a larger value and rerun\nthe program.\n");
	  threshold_warning_given = 1;

	  /* We'll still make note of this shlib, even if we don't
	     read its symbols.  This allows us to use its unwind
	     information well enough to know how to e.g., correctly
	     do a traceback from a PC within the shlib, even if we
	     can't symbolize those PCs...
	   */
	  som_solib_add_solib_objfile (new_so, name, from_tty, text_addr);
	  continue;
	}

      som_solib_total_st_size += st_size;

      /* This fills in new_so->objfile, among others. */
      som_solib_load_symbols (new_so, name, from_tty, text_addr, target);
    }

#ifdef SOLIB_DEBUG
  printf ("--Done reading shared library data\n");
#endif

  /* Getting new symbols may change our opinion about what is
     frameless.  */
  reinit_frame_cache ();
  return;

old_dld:
  error ("Debugging dynamic executables loaded via the hpux8 dld.sl is not supported.\n");
  return;

err:
  error ("Error while reading dynamic library list.\n");
  return;
}


/* This hook gets called just before the first instruction in the
   inferior process is executed.

   This is our opportunity to set magic flags in the inferior so
   that GDB can be notified when a shared library is mapped in and
   to tell the dynamic linker that a private copy of the library is
   needed (so GDB can set breakpoints in the library).

   __dld_flags is the location of the magic flags; as of this implementation
   there are 3 flags of interest:

   bit 0 when set indicates that private copies of the libraries are needed
   bit 1 when set indicates that the callback hook routine is valid
   bit 2 when set indicates that the dynamic linker should maintain the
   __dld_list structure when loading/unloading libraries.

   Note that shared libraries are not mapped in at this time, so we have
   run the inferior until the libraries are mapped in.  Typically this
   means running until the "_start" is called.  */

void
som_solib_create_inferior_hook (void)
{
  struct minimal_symbol *msymbol;
  unsigned int dld_flags, status, have_endo;
  asection *shlib_info;
  char buf[4];
  struct objfile *objfile;
  CORE_ADDR anaddr;

  /* First, remove all the solib event breakpoints.  Their addresses
     may have changed since the last time we ran the program.  */
  remove_solib_event_breakpoints ();

  if (symfile_objfile == NULL)
    return;

  /* First see if the objfile was dynamically linked.  */
  shlib_info = bfd_get_section_by_name (symfile_objfile->obfd, "$SHLIB_INFO$");
  if (!shlib_info)
    return;

  /* It's got a $SHLIB_INFO$ section, make sure it's not empty.  */
  if (bfd_section_size (symfile_objfile->obfd, shlib_info) == 0)
    return;

  have_endo = 0;
  /* Slam the pid of the process into __d_pid.

     We used to warn when this failed, but that warning is only useful
     on very old HP systems (hpux9 and older).  The warnings are an
     annoyance to users of modern systems and foul up the testsuite as
     well.  As a result, the warnings have been disabled.  */
  msymbol = lookup_minimal_symbol ("__d_pid", NULL, symfile_objfile);
  if (msymbol == NULL)
    goto keep_going;

  anaddr = SYMBOL_VALUE_ADDRESS (msymbol);
  store_unsigned_integer (buf, 4, PIDGET (inferior_ptid));
  status = target_write_memory (anaddr, buf, 4);
  if (status != 0)
    {
      warning ("Unable to write __d_pid");
      warning ("Suggest linking with /opt/langtools/lib/end.o.");
      warning ("GDB will be unable to track shl_load/shl_unload calls");
      goto keep_going;
    }

  /* Get the value of _DLD_HOOK (an export stub) and put it in __dld_hook;
     This will force the dynamic linker to call __d_trap when significant
     events occur.

     Note that the above is the pre-HP-UX 9.0 behaviour.  At 9.0 and above,
     the dld provides an export stub named "__d_trap" as well as the
     function named "__d_trap" itself, but doesn't provide "_DLD_HOOK".
     We'll look first for the old flavor and then the new.
   */
  msymbol = lookup_minimal_symbol ("_DLD_HOOK", NULL, symfile_objfile);
  if (msymbol == NULL)
    msymbol = lookup_minimal_symbol ("__d_trap", NULL, symfile_objfile);
  if (msymbol == NULL)
    {
      warning ("Unable to find _DLD_HOOK symbol in object file.");
      warning ("Suggest linking with /opt/langtools/lib/end.o.");
      warning ("GDB will be unable to track shl_load/shl_unload calls");
      goto keep_going;
    }
  anaddr = SYMBOL_VALUE_ADDRESS (msymbol);
  dld_cache.hook.address = anaddr;

  /* Grrr, this might not be an export symbol!  We have to find the
     export stub.  */
  ALL_OBJFILES (objfile)
  {
    struct unwind_table_entry *u;
    struct minimal_symbol *msymbol2;

    /* What a crock.  */
    msymbol2 = lookup_minimal_symbol_solib_trampoline (DEPRECATED_SYMBOL_NAME (msymbol),
						       objfile);
    /* Found a symbol with the right name.  */
    if (msymbol2)
      {
	struct unwind_table_entry *u;
	/* It must be a shared library trampoline.  */
	if (SYMBOL_TYPE (msymbol2) != mst_solib_trampoline)
	  continue;

	/* It must also be an export stub.  */
	u = find_unwind_entry (SYMBOL_VALUE (msymbol2));
	if (!u || u->stub_unwind.stub_type != EXPORT)
	  continue;

	/* OK.  Looks like the correct import stub.  */
	anaddr = SYMBOL_VALUE (msymbol2);
	dld_cache.hook_stub.address = anaddr;
      }
  }
  store_unsigned_integer (buf, 4, anaddr);

  msymbol = lookup_minimal_symbol ("__dld_hook", NULL, symfile_objfile);
  if (msymbol == NULL)
    {
      warning ("Unable to find __dld_hook symbol in object file.");
      warning ("Suggest linking with /opt/langtools/lib/end.o.");
      warning ("GDB will be unable to track shl_load/shl_unload calls");
      goto keep_going;
    }
  anaddr = SYMBOL_VALUE_ADDRESS (msymbol);
  status = target_write_memory (anaddr, buf, 4);

  /* Now set a shlib_event breakpoint at __d_trap so we can track
     significant shared library events.  */
  msymbol = lookup_minimal_symbol ("__d_trap", NULL, symfile_objfile);
  if (msymbol == NULL)
    {
      warning ("Unable to find __dld_d_trap symbol in object file.");
      warning ("Suggest linking with /opt/langtools/lib/end.o.");
      warning ("GDB will be unable to track shl_load/shl_unload calls");
      goto keep_going;
    }
  create_solib_event_breakpoint (SYMBOL_VALUE_ADDRESS (msymbol));

  /* We have all the support usually found in end.o, so we can track
     shl_load and shl_unload calls.  */
  have_endo = 1;

keep_going:

  /* Get the address of __dld_flags, if no such symbol exists, then we can
     not debug the shared code.  */
  msymbol = lookup_minimal_symbol ("__dld_flags", NULL, NULL);
  if (msymbol == NULL)
    {
      error ("Unable to find __dld_flags symbol in object file.\n");
    }

  anaddr = SYMBOL_VALUE_ADDRESS (msymbol);

  /* Read the current contents.  */
  status = target_read_memory (anaddr, buf, 4);
  if (status != 0)
    {
      error ("Unable to read __dld_flags\n");
    }
  dld_flags = extract_unsigned_integer (buf, 4);

  /* Turn on the flags we care about.  */
  dld_flags |= DLD_FLAGS_MAPPRIVATE;
  if (have_endo)
    dld_flags |= DLD_FLAGS_HOOKVALID;
  store_unsigned_integer (buf, 4, dld_flags);
  status = target_write_memory (anaddr, buf, 4);
  if (status != 0)
    {
      error ("Unable to write __dld_flags\n");
    }

  /* Now find the address of _start and set a breakpoint there. 
     We still need this code for two reasons:

     * Not all sites have /opt/langtools/lib/end.o, so it's not always
     possible to track the dynamic linker's events.

     * At this time no events are triggered for shared libraries
     loaded at startup time (what a crock).  */

  msymbol = lookup_minimal_symbol ("_start", NULL, symfile_objfile);
  if (msymbol == NULL)
    {
      error ("Unable to find _start symbol in object file.\n");
    }

  anaddr = SYMBOL_VALUE_ADDRESS (msymbol);

  /* Make the breakpoint at "_start" a shared library event breakpoint.  */
  create_solib_event_breakpoint (anaddr);

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
   som_solib_create_inferior_hook.

   This operation does not remove any knowledge of shared libraries which
   GDB may already have been notified of.
 */
void
som_solib_remove_inferior_hook (int pid)
{
  CORE_ADDR addr;
  struct minimal_symbol *msymbol;
  int status;
  char dld_flags_buffer[4];
  unsigned int dld_flags_value;
  struct cleanup *old_cleanups = save_inferior_ptid ();

  /* Ensure that we're really operating on the specified process. */
  inferior_ptid = pid_to_ptid (pid);

  /* We won't bother to remove the solib breakpoints from this process.

     In fact, on PA64 the breakpoint is hard-coded into the dld callback,
     and thus we're not supposed to remove it.

     Rather, we'll merely clear the dld_flags bit that enables callbacks.
   */
  msymbol = lookup_minimal_symbol ("__dld_flags", NULL, NULL);

  addr = SYMBOL_VALUE_ADDRESS (msymbol);
  status = target_read_memory (addr, dld_flags_buffer, 4);

  dld_flags_value = extract_unsigned_integer (dld_flags_buffer, 4);

  dld_flags_value &= ~DLD_FLAGS_HOOKVALID;
  store_unsigned_integer (dld_flags_buffer, 4, dld_flags_value);
  status = target_write_memory (addr, dld_flags_buffer, 4);

  do_cleanups (old_cleanups);
}


/* This function creates a breakpoint on the dynamic linker hook, which
   is called when e.g., a shl_load or shl_unload call is made.  This
   breakpoint will only trigger when a shl_load call is made.

   If filename is NULL, then loads of any dll will be caught.  Else,
   only loads of the file whose pathname is the string contained by
   filename will be caught.

   Undefined behaviour is guaranteed if this function is called before
   som_solib_create_inferior_hook.
 */
void
som_solib_create_catch_load_hook (int pid, int tempflag, char *filename,
				  char *cond_string)
{
  create_solib_load_event_breakpoint ("__d_trap", tempflag, filename, cond_string);
}

/* This function creates a breakpoint on the dynamic linker hook, which
   is called when e.g., a shl_load or shl_unload call is made.  This
   breakpoint will only trigger when a shl_unload call is made.

   If filename is NULL, then unloads of any dll will be caught.  Else,
   only unloads of the file whose pathname is the string contained by
   filename will be caught.

   Undefined behaviour is guaranteed if this function is called before
   som_solib_create_inferior_hook.
 */
void
som_solib_create_catch_unload_hook (int pid, int tempflag, char *filename,
				    char *cond_string)
{
  create_solib_unload_event_breakpoint ("__d_trap", tempflag, filename, cond_string);
}

int
som_solib_have_load_event (int pid)
{
  CORE_ADDR event_kind;

  event_kind = read_register (ARG0_REGNUM);
  return (event_kind == SHL_LOAD);
}

int
som_solib_have_unload_event (int pid)
{
  CORE_ADDR event_kind;

  event_kind = read_register (ARG0_REGNUM);
  return (event_kind == SHL_UNLOAD);
}

static char *
som_solib_library_pathname (int pid)
{
  CORE_ADDR dll_handle_address;
  CORE_ADDR dll_pathname_address;
  struct som_solib_mapped_entry dll_descriptor;
  char *p;
  static char dll_pathname[1024];

  /* Read the descriptor of this newly-loaded library. */
  dll_handle_address = read_register (ARG1_REGNUM);
  read_memory (dll_handle_address, (char *) &dll_descriptor, sizeof (dll_descriptor));

  /* We can find a pointer to the dll's pathname within the descriptor. */
  dll_pathname_address = (CORE_ADDR) dll_descriptor.name;

  /* Read the pathname, one byte at a time. */
  p = dll_pathname;
  for (;;)
    {
      char b;
      read_memory (dll_pathname_address++, (char *) &b, 1);
      *p++ = b;
      if (b == '\0')
	break;
    }

  return dll_pathname;
}

char *
som_solib_loaded_library_pathname (int pid)
{
  if (!som_solib_have_load_event (pid))
    error ("Must have a load event to use this query");

  return som_solib_library_pathname (pid);
}

char *
som_solib_unloaded_library_pathname (int pid)
{
  if (!som_solib_have_unload_event (pid))
    error ("Must have an unload event to use this query");

  return som_solib_library_pathname (pid);
}

static void
som_solib_desire_dynamic_linker_symbols (void)
{
  struct objfile *objfile;
  struct unwind_table_entry *u;
  struct minimal_symbol *dld_msymbol;

  /* Do we already know the value of these symbols?  If so, then
     we've no work to do.

     (If you add clauses to this test, be sure to likewise update the
     test within the loop.)
   */
  if (dld_cache.is_valid)
    return;

  ALL_OBJFILES (objfile)
  {
    dld_msymbol = lookup_minimal_symbol ("shl_load", NULL, objfile);
    if (dld_msymbol != NULL)
      {
	dld_cache.load.address = SYMBOL_VALUE (dld_msymbol);
	dld_cache.load.unwind = find_unwind_entry (dld_cache.load.address);
      }

    dld_msymbol = lookup_minimal_symbol_solib_trampoline ("shl_load",
							  objfile);
    if (dld_msymbol != NULL)
      {
	if (SYMBOL_TYPE (dld_msymbol) == mst_solib_trampoline)
	  {
	    u = find_unwind_entry (SYMBOL_VALUE (dld_msymbol));
	    if ((u != NULL) && (u->stub_unwind.stub_type == EXPORT))
	      {
		dld_cache.load_stub.address = SYMBOL_VALUE (dld_msymbol);
		dld_cache.load_stub.unwind = u;
	      }
	  }
      }

    dld_msymbol = lookup_minimal_symbol ("shl_unload", NULL, objfile);
    if (dld_msymbol != NULL)
      {
	dld_cache.unload.address = SYMBOL_VALUE (dld_msymbol);
	dld_cache.unload.unwind = find_unwind_entry (dld_cache.unload.address);

	/* ??rehrauer: I'm not sure exactly what this is, but it appears
	   that on some HPUX 10.x versions, there's two unwind regions to
	   cover the body of "shl_unload", the second being 4 bytes past
	   the end of the first.  This is a large hack to handle that
	   case, but since I don't seem to have any legitimate way to
	   look for this thing via the symbol table...
	 */
	if (dld_cache.unload.unwind != NULL)
	  {
	    u = find_unwind_entry (dld_cache.unload.unwind->region_end + 4);
	    if (u != NULL)
	      {
		dld_cache.unload2.address = u->region_start;
		dld_cache.unload2.unwind = u;
	      }
	  }
      }

    dld_msymbol = lookup_minimal_symbol_solib_trampoline ("shl_unload",
							  objfile);
    if (dld_msymbol != NULL)
      {
	if (SYMBOL_TYPE (dld_msymbol) == mst_solib_trampoline)
	  {
	    u = find_unwind_entry (SYMBOL_VALUE (dld_msymbol));
	    if ((u != NULL) && (u->stub_unwind.stub_type == EXPORT))
	      {
		dld_cache.unload_stub.address = SYMBOL_VALUE (dld_msymbol);
		dld_cache.unload_stub.unwind = u;
	      }
	  }
      }

    /* Did we find everything we were looking for?  If so, stop. */
    if ((dld_cache.load.address != 0)
	&& (dld_cache.load_stub.address != 0)
	&& (dld_cache.unload.address != 0)
	&& (dld_cache.unload_stub.address != 0))
      {
	dld_cache.is_valid = 1;
	break;
      }
  }

  dld_cache.hook.unwind = find_unwind_entry (dld_cache.hook.address);
  dld_cache.hook_stub.unwind = find_unwind_entry (dld_cache.hook_stub.address);

  /* We're prepared not to find some of these symbols, which is why
     this function is a "desire" operation, and not a "require".
   */
}

int
som_solib_in_dynamic_linker (int pid, CORE_ADDR pc)
{
  struct unwind_table_entry *u_pc;

  /* Are we in the dld itself?

     ??rehrauer: Large hack -- We'll assume that any address in a
     shared text region is the dld's text.  This would obviously
     fall down if the user attached to a process, whose shlibs
     weren't mapped to a (writeable) private region.  However, in
     that case the debugger probably isn't able to set the fundamental
     breakpoint in the dld callback anyways, so this hack should be
     safe.
   */
  if ((pc & (CORE_ADDR) 0xc0000000) == (CORE_ADDR) 0xc0000000)
    return 1;

  /* Cache the address of some symbols that are part of the dynamic
     linker, if not already known.
   */
  som_solib_desire_dynamic_linker_symbols ();

  /* Are we in the dld callback?  Or its export stub? */
  u_pc = find_unwind_entry (pc);
  if (u_pc == NULL)
    return 0;

  if ((u_pc == dld_cache.hook.unwind) || (u_pc == dld_cache.hook_stub.unwind))
    return 1;

  /* Or the interface of the dld (i.e., "shl_load" or friends)? */
  if ((u_pc == dld_cache.load.unwind)
      || (u_pc == dld_cache.unload.unwind)
      || (u_pc == dld_cache.unload2.unwind)
      || (u_pc == dld_cache.load_stub.unwind)
      || (u_pc == dld_cache.unload_stub.unwind))
    return 1;

  /* Apparently this address isn't part of the dld's text. */
  return 0;
}


/* Return the GOT value for the shared library in which ADDR belongs.  If
   ADDR isn't in any known shared library, return zero.  */

CORE_ADDR
som_solib_get_got_by_pc (CORE_ADDR addr)
{
  struct so_list *so_list = so_list_head;
  CORE_ADDR got_value = 0;

  while (so_list)
    {
      if (so_list->som_solib.text_addr <= addr
	  && so_list->som_solib.text_end > addr)
	{
	  got_value = so_list->som_solib.got_value;
	  break;
	}
      so_list = so_list->next;
    }
  return got_value;
}

/*  elz:
   Return the address of the handle of the shared library
   in which ADDR belongs.  If
   ADDR isn't in any known shared library, return zero.  */
/* this function is used in hppa_fix_call_dummy in hppa-tdep.c */

CORE_ADDR
som_solib_get_solib_by_pc (CORE_ADDR addr)
{
  struct so_list *so_list = so_list_head;

  while (so_list)
    {
      if (so_list->som_solib.text_addr <= addr
	  && so_list->som_solib.text_end > addr)
	{
	  break;
	}
      so_list = so_list->next;
    }
  if (so_list)
    return so_list->solib_addr;
  else
    return 0;
}


int
som_solib_section_offsets (struct objfile *objfile,
			   struct section_offsets *offsets)
{
  struct so_list *so_list = so_list_head;

  while (so_list)
    {
      /* Oh what a pain!  We need the offsets before so_list->objfile
         is valid.  The BFDs will never match.  Make a best guess.  */
      if (strstr (objfile->name, so_list->som_solib.name))
	{
	  asection *private_section;

	  /* The text offset is easy.  */
	  offsets->offsets[SECT_OFF_TEXT (objfile)]
	    = (so_list->som_solib.text_addr
	       - so_list->som_solib.text_link_addr);
	  offsets->offsets[SECT_OFF_RODATA (objfile)]
	    = ANOFFSET (offsets, SECT_OFF_TEXT (objfile));

	  /* We should look at presumed_dp in the SOM header, but
	     that's not easily available.  This should be OK though.  */
	  private_section = bfd_get_section_by_name (objfile->obfd,
						     "$PRIVATE$");
	  if (!private_section)
	    {
	      warning ("Unable to find $PRIVATE$ in shared library!");
	      offsets->offsets[SECT_OFF_DATA (objfile)] = 0;
	      offsets->offsets[SECT_OFF_BSS (objfile)] = 0;
	      return 1;
	    }
	  offsets->offsets[SECT_OFF_DATA (objfile)]
	    = (so_list->som_solib.data_start - private_section->vma);
	  offsets->offsets[SECT_OFF_BSS (objfile)]
	    = ANOFFSET (offsets, SECT_OFF_DATA (objfile));
	  return 1;
	}
      so_list = so_list->next;
    }
  return 0;
}

/* Dump information about all the currently loaded shared libraries.  */

static void
som_sharedlibrary_info_command (char *ignore, int from_tty)
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
  printf_unfiltered ("    %-12s%-12s%-12s%-12s%-12s%-12s\n",
	 "  flags", "  tstart", "   tend", "  dstart", "   dend", "   dlt");
  while (so_list)
    {
      unsigned int flags;

      flags = so_list->som_solib.struct_version << 24;
      flags |= so_list->som_solib.bind_mode << 16;
      flags |= so_list->som_solib.library_version;
      printf_unfiltered ("%s", so_list->som_solib.name);
      if (so_list->objfile == NULL)
	printf_unfiltered ("  (symbols not loaded)");
      printf_unfiltered ("\n");
      printf_unfiltered ("    %-12s", local_hex_string_custom (flags, "08l"));
      printf_unfiltered ("%-12s",
	     local_hex_string_custom (so_list->som_solib.text_addr, "08l"));
      printf_unfiltered ("%-12s",
	      local_hex_string_custom (so_list->som_solib.text_end, "08l"));
      printf_unfiltered ("%-12s",
	    local_hex_string_custom (so_list->som_solib.data_start, "08l"));
      printf_unfiltered ("%-12s",
	      local_hex_string_custom (so_list->som_solib.data_end, "08l"));
      printf_unfiltered ("%-12s\n",
	     local_hex_string_custom (so_list->som_solib.got_value, "08l"));
      so_list = so_list->next;
    }
}

static void
som_solib_sharedlibrary_command (char *args, int from_tty)
{
  dont_repeat ();
  som_solib_add (args, from_tty, (struct target_ops *) 0, 1);
}



char *
som_solib_address (CORE_ADDR addr)
{
  struct so_list *so = so_list_head;

  while (so)
    {
      /* Is this address within this shlib's text range?  If so,
         return the shlib's name.
       */
      if ((addr >= so->som_solib.text_addr) && (addr <= so->som_solib.text_end))
	return so->som_solib.name;

      /* Nope, keep looking... */
      so = so->next;
    }

  /* No, we couldn't prove that the address is within a shlib. */
  return NULL;
}


void
som_solib_restart (void)
{
  struct so_list *sl = so_list_head;

  /* Before the shlib info vanishes, use it to disable any breakpoints
     that may still be active in those shlibs.
   */
  disable_breakpoints_in_shlibs (0);

  /* Discard all the shlib descriptors.
   */
  while (sl)
    {
      struct so_list *next_sl = sl->next;
      xfree (sl);
      sl = next_sl;
    }
  so_list_head = NULL;

  som_solib_total_st_size = (LONGEST) 0;
  som_solib_st_size_threshold_exceeded = 0;

  dld_cache.is_valid = 0;

  dld_cache.hook.address = 0;
  dld_cache.hook.unwind = NULL;

  dld_cache.hook_stub.address = 0;
  dld_cache.hook_stub.unwind = NULL;

  dld_cache.load.address = 0;
  dld_cache.load.unwind = NULL;

  dld_cache.load_stub.address = 0;
  dld_cache.load_stub.unwind = NULL;

  dld_cache.unload.address = 0;
  dld_cache.unload.unwind = NULL;

  dld_cache.unload2.address = 0;
  dld_cache.unload2.unwind = NULL;

  dld_cache.unload_stub.address = 0;
  dld_cache.unload_stub.unwind = NULL;
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
_initialize_som_solib (void)
{
  add_com ("sharedlibrary", class_files, som_solib_sharedlibrary_command,
	   "Load shared object library symbols for files matching REGEXP.");
  add_info ("sharedlibrary", som_sharedlibrary_info_command,
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

  som_solib_restart ();
}

/* Get some HPUX-specific data from a shared lib.
 */
CORE_ADDR
so_lib_thread_start_addr (struct so_list *so)
{
  return so->som_solib.tsd_start_addr;
}
