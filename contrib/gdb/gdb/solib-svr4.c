/* Handle SVR4 shared libraries for GDB, the GNU Debugger.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1998, 1999,
   2000, 2001, 2003, 2004
   Free Software Foundation, Inc.

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

#include "elf/external.h"
#include "elf/common.h"
#include "elf/mips.h"

#include "symtab.h"
#include "bfd.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdbcore.h"
#include "target.h"
#include "inferior.h"

#include "solist.h"
#include "solib-svr4.h"

#include "bfd-target.h"
#include "exec.h"

#ifndef SVR4_FETCH_LINK_MAP_OFFSETS
#define SVR4_FETCH_LINK_MAP_OFFSETS() svr4_fetch_link_map_offsets ()
#endif

static struct link_map_offsets *svr4_fetch_link_map_offsets (void);
static struct link_map_offsets *legacy_fetch_link_map_offsets (void);
static int svr4_have_link_map_offsets (void);

/* fetch_link_map_offsets_gdbarch_data is a handle used to obtain the
   architecture specific link map offsets fetching function.  */

static struct gdbarch_data *fetch_link_map_offsets_gdbarch_data;

/* legacy_svr4_fetch_link_map_offsets_hook is a pointer to a function
   which is used to fetch link map offsets.  It will only be set
   by solib-legacy.c, if at all. */

struct link_map_offsets *(*legacy_svr4_fetch_link_map_offsets_hook)(void) = 0;

/* Link map info to include in an allocated so_list entry */

struct lm_info
  {
    /* Pointer to copy of link map from inferior.  The type is char *
       rather than void *, so that we may use byte offsets to find the
       various fields without the need for a cast.  */
    char *lm;
  };

/* On SVR4 systems, a list of symbols in the dynamic linker where
   GDB can try to place a breakpoint to monitor shared library
   events.

   If none of these symbols are found, or other errors occur, then
   SVR4 systems will fall back to using a symbol as the "startup
   mapping complete" breakpoint address.  */

static char *solib_break_names[] =
{
  "r_debug_state",
  "_r_debug_state",
  "_dl_debug_state",
  "rtld_db_dlactivity",
  "_rtld_debug_state",

  /* On the 64-bit PowerPC, the linker symbol with the same name as
     the C function points to a function descriptor, not to the entry
     point.  The linker symbol whose name is the C function name
     prefixed with a '.' points to the function's entry point.  So
     when we look through this table, we ignore symbols that point
     into the data section (thus skipping the descriptor's symbol),
     and eventually try this one, giving us the real entry point
     address.  */
  ".r_debug_state",
  "._dl_debug_state",

  NULL
};

#define BKPT_AT_SYMBOL 1

#if defined (BKPT_AT_SYMBOL)
static char *bkpt_names[] =
{
#ifdef SOLIB_BKPT_NAME
  SOLIB_BKPT_NAME,		/* Prefer configured name if it exists. */
#endif
  "_start",
  "__start",
  "main",
  NULL
};
#endif

static char *main_name_list[] =
{
  "main_$main",
  NULL
};

/* Macro to extract an address from a solib structure.  When GDB is
   configured for some 32-bit targets (e.g. Solaris 2.7 sparc), BFD is
   configured to handle 64-bit targets, so CORE_ADDR is 64 bits.  We
   have to extract only the significant bits of addresses to get the
   right address when accessing the core file BFD.

   Assume that the address is unsigned.  */

#define SOLIB_EXTRACT_ADDRESS(MEMBER) \
	extract_unsigned_integer (&(MEMBER), sizeof (MEMBER))

/* local data declarations */

/* link map access functions */

static CORE_ADDR
LM_ADDR (struct so_list *so)
{
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();

  return (CORE_ADDR) extract_signed_integer (so->lm_info->lm + lmo->l_addr_offset, 
					     lmo->l_addr_size);
}

static CORE_ADDR
LM_NEXT (struct so_list *so)
{
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();

  /* Assume that the address is unsigned.  */
  return extract_unsigned_integer (so->lm_info->lm + lmo->l_next_offset,
				   lmo->l_next_size);
}

static CORE_ADDR
LM_NAME (struct so_list *so)
{
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();

  /* Assume that the address is unsigned.  */
  return extract_unsigned_integer (so->lm_info->lm + lmo->l_name_offset,
				   lmo->l_name_size);
}

static int
IGNORE_FIRST_LINK_MAP_ENTRY (struct so_list *so)
{
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();

  /* Assume that the address is unsigned.  */
  return extract_unsigned_integer (so->lm_info->lm + lmo->l_prev_offset,
				   lmo->l_prev_size) == 0;
}

static CORE_ADDR debug_base;	/* Base of dynamic linker structures */
static CORE_ADDR breakpoint_addr;	/* Address where end bkpt is set */

/* Local function prototypes */

static int match_main (char *);

static CORE_ADDR bfd_lookup_symbol (bfd *, char *, flagword);

/*

   LOCAL FUNCTION

   bfd_lookup_symbol -- lookup the value for a specific symbol

   SYNOPSIS

   CORE_ADDR bfd_lookup_symbol (bfd *abfd, char *symname, flagword sect_flags)

   DESCRIPTION

   An expensive way to lookup the value of a single symbol for
   bfd's that are only temporary anyway.  This is used by the
   shared library support to find the address of the debugger
   interface structures in the shared library.

   If SECT_FLAGS is non-zero, only match symbols in sections whose
   flags include all those in SECT_FLAGS.

   Note that 0 is specifically allowed as an error return (no
   such symbol).
 */

static CORE_ADDR
bfd_lookup_symbol (bfd *abfd, char *symname, flagword sect_flags)
{
  long storage_needed;
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
	  if (strcmp (sym->name, symname) == 0
              && (sym->section->flags & sect_flags) == sect_flags)
	    {
	      /* Bfd symbols are section relative. */
	      symaddr = sym->value + sym->section->vma;
	      break;
	    }
	}
      do_cleanups (back_to);
    }

  if (symaddr)
    return symaddr;

  /* On FreeBSD, the dynamic linker is stripped by default.  So we'll
     have to check the dynamic string table too.  */

  storage_needed = bfd_get_dynamic_symtab_upper_bound (abfd);

  if (storage_needed > 0)
    {
      symbol_table = (asymbol **) xmalloc (storage_needed);
      back_to = make_cleanup (xfree, symbol_table);
      number_of_symbols = bfd_canonicalize_dynamic_symtab (abfd, symbol_table);

      for (i = 0; i < number_of_symbols; i++)
	{
	  sym = *symbol_table++;

	  if (strcmp (sym->name, symname) == 0
              && (sym->section->flags & sect_flags) == sect_flags)
	    {
	      /* Bfd symbols are section relative. */
	      symaddr = sym->value + sym->section->vma;
	      break;
	    }
	}
      do_cleanups (back_to);
    }

  return symaddr;
}

#ifdef HANDLE_SVR4_EXEC_EMULATORS

/*
   Solaris BCP (the part of Solaris which allows it to run SunOS4
   a.out files) throws in another wrinkle. Solaris does not fill
   in the usual a.out link map structures when running BCP programs,
   the only way to get at them is via groping around in the dynamic
   linker.
   The dynamic linker and it's structures are located in the shared
   C library, which gets run as the executable's "interpreter" by
   the kernel.

   Note that we can assume nothing about the process state at the time
   we need to find these structures.  We may be stopped on the first
   instruction of the interpreter (C shared library), the first
   instruction of the executable itself, or somewhere else entirely
   (if we attached to the process for example).
 */

static char *debug_base_symbols[] =
{
  "r_debug",			/* Solaris 2.3 */
  "_r_debug",			/* Solaris 2.1, 2.2 */
  NULL
};

static int look_for_base (int, CORE_ADDR);

/*

   LOCAL FUNCTION

   look_for_base -- examine file for each mapped address segment

   SYNOPSYS

   static int look_for_base (int fd, CORE_ADDR baseaddr)

   DESCRIPTION

   This function is passed to proc_iterate_over_mappings, which
   causes it to get called once for each mapped address space, with
   an open file descriptor for the file mapped to that space, and the
   base address of that mapped space.

   Our job is to find the debug base symbol in the file that this
   fd is open on, if it exists, and if so, initialize the dynamic
   linker structure base address debug_base.

   Note that this is a computationally expensive proposition, since
   we basically have to open a bfd on every call, so we specifically
   avoid opening the exec file.
 */

static int
look_for_base (int fd, CORE_ADDR baseaddr)
{
  bfd *interp_bfd;
  CORE_ADDR address = 0;
  char **symbolp;

  /* If the fd is -1, then there is no file that corresponds to this
     mapped memory segment, so skip it.  Also, if the fd corresponds
     to the exec file, skip it as well. */

  if (fd == -1
      || (exec_bfd != NULL
	  && fdmatch (fileno ((FILE *) (exec_bfd->iostream)), fd)))
    {
      return (0);
    }

  /* Try to open whatever random file this fd corresponds to.  Note that
     we have no way currently to find the filename.  Don't gripe about
     any problems we might have, just fail. */

  if ((interp_bfd = bfd_fdopenr ("unnamed", gnutarget, fd)) == NULL)
    {
      return (0);
    }
  if (!bfd_check_format (interp_bfd, bfd_object))
    {
      /* FIXME-leak: on failure, might not free all memory associated with
         interp_bfd.  */
      bfd_close (interp_bfd);
      return (0);
    }

  /* Now try to find our debug base symbol in this file, which we at
     least know to be a valid ELF executable or shared library. */

  for (symbolp = debug_base_symbols; *symbolp != NULL; symbolp++)
    {
      address = bfd_lookup_symbol (interp_bfd, *symbolp, 0);
      if (address != 0)
	{
	  break;
	}
    }
  if (address == 0)
    {
      /* FIXME-leak: on failure, might not free all memory associated with
         interp_bfd.  */
      bfd_close (interp_bfd);
      return (0);
    }

  /* Eureka!  We found the symbol.  But now we may need to relocate it
     by the base address.  If the symbol's value is less than the base
     address of the shared library, then it hasn't yet been relocated
     by the dynamic linker, and we have to do it ourself.  FIXME: Note
     that we make the assumption that the first segment that corresponds
     to the shared library has the base address to which the library
     was relocated. */

  if (address < baseaddr)
    {
      address += baseaddr;
    }
  debug_base = address;
  /* FIXME-leak: on failure, might not free all memory associated with
     interp_bfd.  */
  bfd_close (interp_bfd);
  return (1);
}
#endif /* HANDLE_SVR4_EXEC_EMULATORS */

/*

   LOCAL FUNCTION

   elf_locate_base -- locate the base address of dynamic linker structs
   for SVR4 elf targets.

   SYNOPSIS

   CORE_ADDR elf_locate_base (void)

   DESCRIPTION

   For SVR4 elf targets the address of the dynamic linker's runtime
   structure is contained within the dynamic info section in the
   executable file.  The dynamic section is also mapped into the
   inferior address space.  Because the runtime loader fills in the
   real address before starting the inferior, we have to read in the
   dynamic info section from the inferior address space.
   If there are any errors while trying to find the address, we
   silently return 0, otherwise the found address is returned.

 */

static CORE_ADDR
elf_locate_base (void)
{
  struct bfd_section *dyninfo_sect;
  int dyninfo_sect_size;
  CORE_ADDR dyninfo_addr;
  char *buf;
  char *bufend;
  int arch_size;

  /* Find the start address of the .dynamic section.  */
  dyninfo_sect = bfd_get_section_by_name (exec_bfd, ".dynamic");
  if (dyninfo_sect == NULL)
    return 0;
  dyninfo_addr = bfd_section_vma (exec_bfd, dyninfo_sect);

  /* Read in .dynamic section, silently ignore errors.  */
  dyninfo_sect_size = bfd_section_size (exec_bfd, dyninfo_sect);
  buf = alloca (dyninfo_sect_size);
  if (target_read_memory (dyninfo_addr, buf, dyninfo_sect_size))
    return 0;

  /* Find the DT_DEBUG entry in the the .dynamic section.
     For mips elf we look for DT_MIPS_RLD_MAP, mips elf apparently has
     no DT_DEBUG entries.  */

  arch_size = bfd_get_arch_size (exec_bfd);
  if (arch_size == -1)	/* failure */
    return 0;

  if (arch_size == 32)
    { /* 32-bit elf */
      for (bufend = buf + dyninfo_sect_size;
	   buf < bufend;
	   buf += sizeof (Elf32_External_Dyn))
	{
	  Elf32_External_Dyn *x_dynp = (Elf32_External_Dyn *) buf;
	  long dyn_tag;
	  CORE_ADDR dyn_ptr;

	  dyn_tag = bfd_h_get_32 (exec_bfd, (bfd_byte *) x_dynp->d_tag);
	  if (dyn_tag == DT_NULL)
	    break;
	  else if (dyn_tag == DT_DEBUG)
	    {
	      dyn_ptr = bfd_h_get_32 (exec_bfd, 
				      (bfd_byte *) x_dynp->d_un.d_ptr);
	      return dyn_ptr;
	    }
	  else if (dyn_tag == DT_MIPS_RLD_MAP)
	    {
	      char *pbuf;
	      int pbuf_size = TARGET_PTR_BIT / HOST_CHAR_BIT;

	      pbuf = alloca (pbuf_size);
	      /* DT_MIPS_RLD_MAP contains a pointer to the address
		 of the dynamic link structure.  */
	      dyn_ptr = bfd_h_get_32 (exec_bfd, 
				      (bfd_byte *) x_dynp->d_un.d_ptr);
	      if (target_read_memory (dyn_ptr, pbuf, pbuf_size))
		return 0;
	      return extract_unsigned_integer (pbuf, pbuf_size);
	    }
	}
    }
  else /* 64-bit elf */
    {
      for (bufend = buf + dyninfo_sect_size;
	   buf < bufend;
	   buf += sizeof (Elf64_External_Dyn))
	{
	  Elf64_External_Dyn *x_dynp = (Elf64_External_Dyn *) buf;
	  long dyn_tag;
	  CORE_ADDR dyn_ptr;

	  dyn_tag = bfd_h_get_64 (exec_bfd, (bfd_byte *) x_dynp->d_tag);
	  if (dyn_tag == DT_NULL)
	    break;
	  else if (dyn_tag == DT_DEBUG)
	    {
	      dyn_ptr = bfd_h_get_64 (exec_bfd, 
				      (bfd_byte *) x_dynp->d_un.d_ptr);
	      return dyn_ptr;
	    }
	  else if (dyn_tag == DT_MIPS_RLD_MAP)
	    {
	      char *pbuf;
	      int pbuf_size = TARGET_PTR_BIT / HOST_CHAR_BIT;

	      pbuf = alloca (pbuf_size);
	      /* DT_MIPS_RLD_MAP contains a pointer to the address
		 of the dynamic link structure.  */
	      dyn_ptr = bfd_h_get_64 (exec_bfd, 
				      (bfd_byte *) x_dynp->d_un.d_ptr);
	      if (target_read_memory (dyn_ptr, pbuf, pbuf_size))
		return 0;
	      return extract_unsigned_integer (pbuf, pbuf_size);
	    }
	}
    }

  /* DT_DEBUG entry not found.  */
  return 0;
}

/*

   LOCAL FUNCTION

   locate_base -- locate the base address of dynamic linker structs

   SYNOPSIS

   CORE_ADDR locate_base (void)

   DESCRIPTION

   For both the SunOS and SVR4 shared library implementations, if the
   inferior executable has been linked dynamically, there is a single
   address somewhere in the inferior's data space which is the key to
   locating all of the dynamic linker's runtime structures.  This
   address is the value of the debug base symbol.  The job of this
   function is to find and return that address, or to return 0 if there
   is no such address (the executable is statically linked for example).

   For SunOS, the job is almost trivial, since the dynamic linker and
   all of it's structures are statically linked to the executable at
   link time.  Thus the symbol for the address we are looking for has
   already been added to the minimal symbol table for the executable's
   objfile at the time the symbol file's symbols were read, and all we
   have to do is look it up there.  Note that we explicitly do NOT want
   to find the copies in the shared library.

   The SVR4 version is a bit more complicated because the address
   is contained somewhere in the dynamic info section.  We have to go
   to a lot more work to discover the address of the debug base symbol.
   Because of this complexity, we cache the value we find and return that
   value on subsequent invocations.  Note there is no copy in the
   executable symbol tables.

 */

static CORE_ADDR
locate_base (void)
{
  /* Check to see if we have a currently valid address, and if so, avoid
     doing all this work again and just return the cached address.  If
     we have no cached address, try to locate it in the dynamic info
     section for ELF executables.  There's no point in doing any of this
     though if we don't have some link map offsets to work with.  */

  if (debug_base == 0 && svr4_have_link_map_offsets ())
    {
      if (exec_bfd != NULL
	  && bfd_get_flavour (exec_bfd) == bfd_target_elf_flavour)
	debug_base = elf_locate_base ();
#ifdef HANDLE_SVR4_EXEC_EMULATORS
      /* Try it the hard way for emulated executables.  */
      else if (!ptid_equal (inferior_ptid, null_ptid) && target_has_execution)
	proc_iterate_over_mappings (look_for_base);
#endif
    }
  return (debug_base);
}

/*

   LOCAL FUNCTION

   first_link_map_member -- locate first member in dynamic linker's map

   SYNOPSIS

   static CORE_ADDR first_link_map_member (void)

   DESCRIPTION

   Find the first element in the inferior's dynamic link map, and
   return its address in the inferior.  This function doesn't copy the
   link map entry itself into our address space; current_sos actually
   does the reading.  */

static CORE_ADDR
first_link_map_member (void)
{
  CORE_ADDR lm = 0;
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();
  char *r_map_buf = xmalloc (lmo->r_map_size);
  struct cleanup *cleanups = make_cleanup (xfree, r_map_buf);

  read_memory (debug_base + lmo->r_map_offset, r_map_buf, lmo->r_map_size);

  /* Assume that the address is unsigned.  */
  lm = extract_unsigned_integer (r_map_buf, lmo->r_map_size);

  /* FIXME:  Perhaps we should validate the info somehow, perhaps by
     checking r_version for a known version number, or r_state for
     RT_CONSISTENT. */

  do_cleanups (cleanups);

  return (lm);
}

/*

  LOCAL FUNCTION

  open_symbol_file_object

  SYNOPSIS

  void open_symbol_file_object (void *from_tty)

  DESCRIPTION

  If no open symbol file, attempt to locate and open the main symbol
  file.  On SVR4 systems, this is the first link map entry.  If its
  name is here, we can open it.  Useful when attaching to a process
  without first loading its symbol file.

  If FROM_TTYP dereferences to a non-zero integer, allow messages to
  be printed.  This parameter is a pointer rather than an int because
  open_symbol_file_object() is called via catch_errors() and
  catch_errors() requires a pointer argument. */

static int
open_symbol_file_object (void *from_ttyp)
{
  CORE_ADDR lm, l_name;
  char *filename;
  int errcode;
  int from_tty = *(int *)from_ttyp;
  struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();
  char *l_name_buf = xmalloc (lmo->l_name_size);
  struct cleanup *cleanups = make_cleanup (xfree, l_name_buf);

  if (symfile_objfile)
    if (!query ("Attempt to reload symbols from process? "))
      return 0;

  if ((debug_base = locate_base ()) == 0)
    return 0;	/* failed somehow... */

  /* First link map member should be the executable.  */
  if ((lm = first_link_map_member ()) == 0)
    return 0;	/* failed somehow... */

  /* Read address of name from target memory to GDB.  */
  read_memory (lm + lmo->l_name_offset, l_name_buf, lmo->l_name_size);

  /* Convert the address to host format.  Assume that the address is
     unsigned.  */
  l_name = extract_unsigned_integer (l_name_buf, lmo->l_name_size);

  /* Free l_name_buf.  */
  do_cleanups (cleanups);

  if (l_name == 0)
    return 0;		/* No filename.  */

  /* Now fetch the filename from target memory.  */
  target_read_string (l_name, &filename, SO_NAME_MAX_PATH_SIZE - 1, &errcode);

  if (errcode)
    {
      warning ("failed to read exec filename from attached file: %s",
	       safe_strerror (errcode));
      return 0;
    }

  make_cleanup (xfree, filename);
  /* Have a pathname: read the symbol file.  */
  symbol_file_add_main (filename, from_tty);

  return 1;
}

/* LOCAL FUNCTION

   current_sos -- build a list of currently loaded shared objects

   SYNOPSIS

   struct so_list *current_sos ()

   DESCRIPTION

   Build a list of `struct so_list' objects describing the shared
   objects currently loaded in the inferior.  This list does not
   include an entry for the main executable file.

   Note that we only gather information directly available from the
   inferior --- we don't examine any of the shared library files
   themselves.  The declaration of `struct so_list' says which fields
   we provide values for.  */

static struct so_list *
svr4_current_sos (void)
{
  CORE_ADDR lm;
  struct so_list *head = 0;
  struct so_list **link_ptr = &head;

  /* Make sure we've looked up the inferior's dynamic linker's base
     structure.  */
  if (! debug_base)
    {
      debug_base = locate_base ();

      /* If we can't find the dynamic linker's base structure, this
	 must not be a dynamically linked executable.  Hmm.  */
      if (! debug_base)
	return 0;
    }

  /* Walk the inferior's link map list, and build our list of
     `struct so_list' nodes.  */
  lm = first_link_map_member ();  
  while (lm)
    {
      struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();
      struct so_list *new
	= (struct so_list *) xmalloc (sizeof (struct so_list));
      struct cleanup *old_chain = make_cleanup (xfree, new);

      memset (new, 0, sizeof (*new));

      new->lm_info = xmalloc (sizeof (struct lm_info));
      make_cleanup (xfree, new->lm_info);

      new->lm_info->lm = xmalloc (lmo->link_map_size);
      make_cleanup (xfree, new->lm_info->lm);
      memset (new->lm_info->lm, 0, lmo->link_map_size);

      read_memory (lm, new->lm_info->lm, lmo->link_map_size);

      lm = LM_NEXT (new);

      /* For SVR4 versions, the first entry in the link map is for the
         inferior executable, so we must ignore it.  For some versions of
         SVR4, it has no name.  For others (Solaris 2.3 for example), it
         does have a name, so we can no longer use a missing name to
         decide when to ignore it. */
      if (IGNORE_FIRST_LINK_MAP_ENTRY (new))
	free_so (new);
      else
	{
	  int errcode;
	  char *buffer;

	  /* Extract this shared object's name.  */
	  target_read_string (LM_NAME (new), &buffer,
			      SO_NAME_MAX_PATH_SIZE - 1, &errcode);
	  if (errcode != 0)
	    {
	      warning ("current_sos: Can't read pathname for load map: %s\n",
		       safe_strerror (errcode));
	    }
	  else
	    {
	      strncpy (new->so_name, buffer, SO_NAME_MAX_PATH_SIZE - 1);
	      new->so_name[SO_NAME_MAX_PATH_SIZE - 1] = '\0';
	      xfree (buffer);
	      strcpy (new->so_original_name, new->so_name);
	    }

	  /* If this entry has no name, or its name matches the name
	     for the main executable, don't include it in the list.  */
	  if (! new->so_name[0]
	      || match_main (new->so_name))
	    free_so (new);
	  else
	    {
	      new->next = 0;
	      *link_ptr = new;
	      link_ptr = &new->next;
	    }
	}

      discard_cleanups (old_chain);
    }

  return head;
}

/* Get the address of the link_map for a given OBJFILE.  Loop through
   the link maps, and return the address of the one corresponding to
   the given objfile.  Note that this function takes into account that
   objfile can be the main executable, not just a shared library.  The
   main executable has always an empty name field in the linkmap.  */

CORE_ADDR
svr4_fetch_objfile_link_map (struct objfile *objfile)
{
  CORE_ADDR lm;

  if ((debug_base = locate_base ()) == 0)
    return 0;   /* failed somehow... */

  /* Position ourselves on the first link map.  */
  lm = first_link_map_member ();  
  while (lm)
    {
      /* Get info on the layout of the r_debug and link_map structures. */
      struct link_map_offsets *lmo = SVR4_FETCH_LINK_MAP_OFFSETS ();
      int errcode;
      char *buffer;
      struct lm_info objfile_lm_info;
      struct cleanup *old_chain;
      CORE_ADDR name_address;
      char *l_name_buf = xmalloc (lmo->l_name_size);
      old_chain = make_cleanup (xfree, l_name_buf);

      /* Set up the buffer to contain the portion of the link_map
         structure that gdb cares about.  Note that this is not the
         whole link_map structure.  */
      objfile_lm_info.lm = xmalloc (lmo->link_map_size);
      make_cleanup (xfree, objfile_lm_info.lm);
      memset (objfile_lm_info.lm, 0, lmo->link_map_size);

      /* Read the link map into our internal structure.  */
      read_memory (lm, objfile_lm_info.lm, lmo->link_map_size);

      /* Read address of name from target memory to GDB.  */
      read_memory (lm + lmo->l_name_offset, l_name_buf, lmo->l_name_size);

      /* Extract this object's name.  Assume that the address is
         unsigned.  */
      name_address = extract_unsigned_integer (l_name_buf, lmo->l_name_size);
      target_read_string (name_address, &buffer,
      			  SO_NAME_MAX_PATH_SIZE - 1, &errcode);
      make_cleanup (xfree, buffer);
      if (errcode != 0)
    	{
	  warning ("svr4_fetch_objfile_link_map: Can't read pathname for load map: %s\n",
  		   safe_strerror (errcode));
  	}
      else
  	{
	  /* Is this the linkmap for the file we want?  */
	  /* If the file is not a shared library and has no name,
	     we are sure it is the main executable, so we return that.  */
	  if ((buffer && strcmp (buffer, objfile->name) == 0)
              || (!(objfile->flags & OBJF_SHARED) && (strcmp (buffer, "") == 0)))
  	    {
    	      do_cleanups (old_chain);
    	      return lm;
      	    }
  	}
      /* Not the file we wanted, continue checking.  Assume that the
         address is unsigned.  */
      lm = extract_unsigned_integer (objfile_lm_info.lm + lmo->l_next_offset,
				     lmo->l_next_size);
      do_cleanups (old_chain);
    }
  return 0;
}

/* On some systems, the only way to recognize the link map entry for
   the main executable file is by looking at its name.  Return
   non-zero iff SONAME matches one of the known main executable names.  */

static int
match_main (char *soname)
{
  char **mainp;

  for (mainp = main_name_list; *mainp != NULL; mainp++)
    {
      if (strcmp (soname, *mainp) == 0)
	return (1);
    }

  return (0);
}

/* Return 1 if PC lies in the dynamic symbol resolution code of the
   SVR4 run time loader.  */
static CORE_ADDR interp_text_sect_low;
static CORE_ADDR interp_text_sect_high;
static CORE_ADDR interp_plt_sect_low;
static CORE_ADDR interp_plt_sect_high;

static int
svr4_in_dynsym_resolve_code (CORE_ADDR pc)
{
  return ((pc >= interp_text_sect_low && pc < interp_text_sect_high)
	  || (pc >= interp_plt_sect_low && pc < interp_plt_sect_high)
	  || in_plt_section (pc, NULL));
}

/* Given an executable's ABFD and target, compute the entry-point
   address.  */

static CORE_ADDR
exec_entry_point (struct bfd *abfd, struct target_ops *targ)
{
  /* KevinB wrote ... for most targets, the address returned by
     bfd_get_start_address() is the entry point for the start
     function.  But, for some targets, bfd_get_start_address() returns
     the address of a function descriptor from which the entry point
     address may be extracted.  This address is extracted by
     gdbarch_convert_from_func_ptr_addr().  The method
     gdbarch_convert_from_func_ptr_addr() is the merely the identify
     function for targets which don't use function descriptors.  */
  return gdbarch_convert_from_func_ptr_addr (current_gdbarch,
					     bfd_get_start_address (abfd),
					     targ);
}

/*

   LOCAL FUNCTION

   enable_break -- arrange for dynamic linker to hit breakpoint

   SYNOPSIS

   int enable_break (void)

   DESCRIPTION

   Both the SunOS and the SVR4 dynamic linkers have, as part of their
   debugger interface, support for arranging for the inferior to hit
   a breakpoint after mapping in the shared libraries.  This function
   enables that breakpoint.

   For SunOS, there is a special flag location (in_debugger) which we
   set to 1.  When the dynamic linker sees this flag set, it will set
   a breakpoint at a location known only to itself, after saving the
   original contents of that place and the breakpoint address itself,
   in it's own internal structures.  When we resume the inferior, it
   will eventually take a SIGTRAP when it runs into the breakpoint.
   We handle this (in a different place) by restoring the contents of
   the breakpointed location (which is only known after it stops),
   chasing around to locate the shared libraries that have been
   loaded, then resuming.

   For SVR4, the debugger interface structure contains a member (r_brk)
   which is statically initialized at the time the shared library is
   built, to the offset of a function (_r_debug_state) which is guaran-
   teed to be called once before mapping in a library, and again when
   the mapping is complete.  At the time we are examining this member,
   it contains only the unrelocated offset of the function, so we have
   to do our own relocation.  Later, when the dynamic linker actually
   runs, it relocates r_brk to be the actual address of _r_debug_state().

   The debugger interface structure also contains an enumeration which
   is set to either RT_ADD or RT_DELETE prior to changing the mapping,
   depending upon whether or not the library is being mapped or unmapped,
   and then set to RT_CONSISTENT after the library is mapped/unmapped.
 */

static int
enable_break (void)
{
  int success = 0;

#ifdef BKPT_AT_SYMBOL

  struct minimal_symbol *msymbol;
  char **bkpt_namep;
  asection *interp_sect;

  /* First, remove all the solib event breakpoints.  Their addresses
     may have changed since the last time we ran the program.  */
  remove_solib_event_breakpoints ();

  interp_text_sect_low = interp_text_sect_high = 0;
  interp_plt_sect_low = interp_plt_sect_high = 0;

  /* Find the .interp section; if not found, warn the user and drop
     into the old breakpoint at symbol code.  */
  interp_sect = bfd_get_section_by_name (exec_bfd, ".interp");
  if (interp_sect)
    {
      unsigned int interp_sect_size;
      char *buf;
      CORE_ADDR load_addr = 0;
      int load_addr_found = 0;
      struct so_list *inferior_sos;
      bfd *tmp_bfd = NULL;
      struct target_ops *tmp_bfd_target;
      int tmp_fd = -1;
      char *tmp_pathname = NULL;
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

      tmp_fd  = solib_open (buf, &tmp_pathname);
      if (tmp_fd >= 0)
	tmp_bfd = bfd_fdopenr (tmp_pathname, gnutarget, tmp_fd);

      if (tmp_bfd == NULL)
	goto bkpt_at_symbol;

      /* Make sure the dynamic linker's really a useful object.  */
      if (!bfd_check_format (tmp_bfd, bfd_object))
	{
	  warning ("Unable to grok dynamic linker %s as an object file", buf);
	  bfd_close (tmp_bfd);
	  goto bkpt_at_symbol;
	}

      /* Now convert the TMP_BFD into a target.  That way target, as
         well as BFD operations can be used.  Note that closing the
         target will also close the underlying bfd.  */
      tmp_bfd_target = target_bfd_reopen (tmp_bfd);

      /* If the entry in _DYNAMIC for the dynamic linker has already
         been filled in, we can read its base address from there. */
      inferior_sos = svr4_current_sos ();
      if (inferior_sos)
	{
	  /* Connected to a running target.  Update our shared library table. */
	  solib_add (NULL, 0, NULL, auto_solib_add);
	}
      while (inferior_sos)
	{
	  if (strcmp (buf, inferior_sos->so_original_name) == 0)
	    {
	      load_addr_found = 1;
	      load_addr = LM_ADDR (inferior_sos);
	      break;
	    }
	  inferior_sos = inferior_sos->next;
	}

      /* Otherwise we find the dynamic linker's base address by examining
	 the current pc (which should point at the entry point for the
	 dynamic linker) and subtracting the offset of the entry point.  */
      if (!load_addr_found)
	load_addr = (read_pc ()
		     - exec_entry_point (tmp_bfd, tmp_bfd_target));

      /* Record the relocated start and end address of the dynamic linker
         text and plt section for svr4_in_dynsym_resolve_code.  */
      interp_sect = bfd_get_section_by_name (tmp_bfd, ".text");
      if (interp_sect)
	{
	  interp_text_sect_low =
	    bfd_section_vma (tmp_bfd, interp_sect) + load_addr;
	  interp_text_sect_high =
	    interp_text_sect_low + bfd_section_size (tmp_bfd, interp_sect);
	}
      interp_sect = bfd_get_section_by_name (tmp_bfd, ".plt");
      if (interp_sect)
	{
	  interp_plt_sect_low =
	    bfd_section_vma (tmp_bfd, interp_sect) + load_addr;
	  interp_plt_sect_high =
	    interp_plt_sect_low + bfd_section_size (tmp_bfd, interp_sect);
	}

      /* Now try to set a breakpoint in the dynamic linker.  */
      for (bkpt_namep = solib_break_names; *bkpt_namep != NULL; bkpt_namep++)
	{
          /* On ABI's that use function descriptors, there are usually
             two linker symbols associated with each C function: one
             pointing at the actual entry point of the machine code,
             and one pointing at the function's descriptor.  The
             latter symbol has the same name as the C function.

             What we're looking for here is the machine code entry
             point, so we are only interested in symbols in code
             sections.  */
	  sym_addr = bfd_lookup_symbol (tmp_bfd, *bkpt_namep, SEC_CODE);
	  if (sym_addr != 0)
	    break;
	}

      /* We're done with both the temporary bfd and target.  Remember,
         closing the target closes the underlying bfd.  */
      target_close (tmp_bfd_target, 0);

      if (sym_addr != 0)
	{
	  create_solib_event_breakpoint (load_addr + sym_addr);
	  return 1;
	}

      /* For whatever reason we couldn't set a breakpoint in the dynamic
         linker.  Warn and drop into the old code.  */
    bkpt_at_symbol:
      warning ("Unable to find dynamic linker breakpoint function.\nGDB will be unable to debug shared library initializers\nand track explicitly loaded dynamic code.");
    }

  /* Scan through the list of symbols, trying to look up the symbol and
     set a breakpoint there.  Terminate loop when we/if we succeed. */

  breakpoint_addr = 0;
  for (bkpt_namep = bkpt_names; *bkpt_namep != NULL; bkpt_namep++)
    {
      msymbol = lookup_minimal_symbol (*bkpt_namep, NULL, symfile_objfile);
      if ((msymbol != NULL) && (SYMBOL_VALUE_ADDRESS (msymbol) != 0))
	{
	  create_solib_event_breakpoint (SYMBOL_VALUE_ADDRESS (msymbol));
	  return 1;
	}
    }

  /* Nothing good happened.  */
  success = 0;

#endif /* BKPT_AT_SYMBOL */

  return (success);
}

/*

   LOCAL FUNCTION

   special_symbol_handling -- additional shared library symbol handling

   SYNOPSIS

   void special_symbol_handling ()

   DESCRIPTION

   Once the symbols from a shared object have been loaded in the usual
   way, we are called to do any system specific symbol handling that 
   is needed.

   For SunOS4, this consisted of grunging around in the dynamic
   linkers structures to find symbol definitions for "common" symbols
   and adding them to the minimal symbol table for the runtime common
   objfile.

   However, for SVR4, there's nothing to do.

 */

static void
svr4_special_symbol_handling (void)
{
}

/* Relocate the main executable.  This function should be called upon
   stopping the inferior process at the entry point to the program. 
   The entry point from BFD is compared to the PC and if they are
   different, the main executable is relocated by the proper amount. 
   
   As written it will only attempt to relocate executables which
   lack interpreter sections.  It seems likely that only dynamic
   linker executables will get relocated, though it should work
   properly for a position-independent static executable as well.  */

static void
svr4_relocate_main_executable (void)
{
  asection *interp_sect;
  CORE_ADDR pc = read_pc ();

  /* Decide if the objfile needs to be relocated.  As indicated above,
     we will only be here when execution is stopped at the beginning
     of the program.  Relocation is necessary if the address at which
     we are presently stopped differs from the start address stored in
     the executable AND there's no interpreter section.  The condition
     regarding the interpreter section is very important because if
     there *is* an interpreter section, execution will begin there
     instead.  When there is an interpreter section, the start address
     is (presumably) used by the interpreter at some point to start
     execution of the program.

     If there is an interpreter, it is normal for it to be set to an
     arbitrary address at the outset.  The job of finding it is
     handled in enable_break().

     So, to summarize, relocations are necessary when there is no
     interpreter section and the start address obtained from the
     executable is different from the address at which GDB is
     currently stopped.
     
     [ The astute reader will note that we also test to make sure that
       the executable in question has the DYNAMIC flag set.  It is my
       opinion that this test is unnecessary (undesirable even).  It
       was added to avoid inadvertent relocation of an executable
       whose e_type member in the ELF header is not ET_DYN.  There may
       be a time in the future when it is desirable to do relocations
       on other types of files as well in which case this condition
       should either be removed or modified to accomodate the new file
       type.  (E.g, an ET_EXEC executable which has been built to be
       position-independent could safely be relocated by the OS if
       desired.  It is true that this violates the ABI, but the ABI
       has been known to be bent from time to time.)  - Kevin, Nov 2000. ]
     */

  interp_sect = bfd_get_section_by_name (exec_bfd, ".interp");
  if (interp_sect == NULL 
      && (bfd_get_file_flags (exec_bfd) & DYNAMIC) != 0
      && (exec_entry_point (exec_bfd, &exec_ops) != pc))
    {
      struct cleanup *old_chain;
      struct section_offsets *new_offsets;
      int i, changed;
      CORE_ADDR displacement;
      
      /* It is necessary to relocate the objfile.  The amount to
	 relocate by is simply the address at which we are stopped
	 minus the starting address from the executable.

	 We relocate all of the sections by the same amount.  This
	 behavior is mandated by recent editions of the System V ABI. 
	 According to the System V Application Binary Interface,
	 Edition 4.1, page 5-5:

	   ...  Though the system chooses virtual addresses for
	   individual processes, it maintains the segments' relative
	   positions.  Because position-independent code uses relative
	   addressesing between segments, the difference between
	   virtual addresses in memory must match the difference
	   between virtual addresses in the file.  The difference
	   between the virtual address of any segment in memory and
	   the corresponding virtual address in the file is thus a
	   single constant value for any one executable or shared
	   object in a given process.  This difference is the base
	   address.  One use of the base address is to relocate the
	   memory image of the program during dynamic linking.

	 The same language also appears in Edition 4.0 of the System V
	 ABI and is left unspecified in some of the earlier editions.  */

      displacement = pc - exec_entry_point (exec_bfd, &exec_ops);
      changed = 0;

      new_offsets = xcalloc (symfile_objfile->num_sections,
			     sizeof (struct section_offsets));
      old_chain = make_cleanup (xfree, new_offsets);

      for (i = 0; i < symfile_objfile->num_sections; i++)
	{
	  if (displacement != ANOFFSET (symfile_objfile->section_offsets, i))
	    changed = 1;
	  new_offsets->offsets[i] = displacement;
	}

      if (changed)
	objfile_relocate (symfile_objfile, new_offsets);

      do_cleanups (old_chain);
    }
}

/*

   GLOBAL FUNCTION

   svr4_solib_create_inferior_hook -- shared library startup support

   SYNOPSIS

   void svr4_solib_create_inferior_hook()

   DESCRIPTION

   When gdb starts up the inferior, it nurses it along (through the
   shell) until it is ready to execute it's first instruction.  At this
   point, this function gets called via expansion of the macro
   SOLIB_CREATE_INFERIOR_HOOK.

   For SunOS executables, this first instruction is typically the
   one at "_start", or a similar text label, regardless of whether
   the executable is statically or dynamically linked.  The runtime
   startup code takes care of dynamically linking in any shared
   libraries, once gdb allows the inferior to continue.

   For SVR4 executables, this first instruction is either the first
   instruction in the dynamic linker (for dynamically linked
   executables) or the instruction at "start" for statically linked
   executables.  For dynamically linked executables, the system
   first exec's /lib/libc.so.N, which contains the dynamic linker,
   and starts it running.  The dynamic linker maps in any needed
   shared libraries, maps in the actual user executable, and then
   jumps to "start" in the user executable.

   For both SunOS shared libraries, and SVR4 shared libraries, we
   can arrange to cooperate with the dynamic linker to discover the
   names of shared libraries that are dynamically linked, and the
   base addresses to which they are linked.

   This function is responsible for discovering those names and
   addresses, and saving sufficient information about them to allow
   their symbols to be read at a later time.

   FIXME

   Between enable_break() and disable_break(), this code does not
   properly handle hitting breakpoints which the user might have
   set in the startup code or in the dynamic linker itself.  Proper
   handling will probably have to wait until the implementation is
   changed to use the "breakpoint handler function" method.

   Also, what if child has exit()ed?  Must exit loop somehow.
 */

static void
svr4_solib_create_inferior_hook (void)
{
  /* Relocate the main executable if necessary.  */
  svr4_relocate_main_executable ();

  if (!svr4_have_link_map_offsets ())
    {
      warning ("no shared library support for this OS / ABI");
      return;

    }

  if (!enable_break ())
    {
      warning ("shared library handler failed to enable breakpoint");
      return;
    }

#if defined(_SCO_DS)
  /* SCO needs the loop below, other systems should be using the
     special shared library breakpoints and the shared library breakpoint
     service routine.

     Now run the target.  It will eventually hit the breakpoint, at
     which point all of the libraries will have been mapped in and we
     can go groveling around in the dynamic linker structures to find
     out what we need to know about them. */

  clear_proceed_status ();
  stop_soon = STOP_QUIETLY;
  stop_signal = TARGET_SIGNAL_0;
  do
    {
      target_resume (pid_to_ptid (-1), 0, stop_signal);
      wait_for_inferior ();
    }
  while (stop_signal != TARGET_SIGNAL_TRAP);
  stop_soon = NO_STOP_QUIETLY;
#endif /* defined(_SCO_DS) */
}

static void
svr4_clear_solib (void)
{
  debug_base = 0;
}

static void
svr4_free_so (struct so_list *so)
{
  xfree (so->lm_info->lm);
  xfree (so->lm_info);
}


/* Clear any bits of ADDR that wouldn't fit in a target-format
   data pointer.  "Data pointer" here refers to whatever sort of
   address the dynamic linker uses to manage its sections.  At the
   moment, we don't support shared libraries on any processors where
   code and data pointers are different sizes.

   This isn't really the right solution.  What we really need here is
   a way to do arithmetic on CORE_ADDR values that respects the
   natural pointer/address correspondence.  (For example, on the MIPS,
   converting a 32-bit pointer to a 64-bit CORE_ADDR requires you to
   sign-extend the value.  There, simply truncating the bits above
   TARGET_PTR_BIT, as we do below, is no good.)  This should probably
   be a new gdbarch method or something.  */
static CORE_ADDR
svr4_truncate_ptr (CORE_ADDR addr)
{
  if (TARGET_PTR_BIT == sizeof (CORE_ADDR) * 8)
    /* We don't need to truncate anything, and the bit twiddling below
       will fail due to overflow problems.  */
    return addr;
  else
    return addr & (((CORE_ADDR) 1 << TARGET_PTR_BIT) - 1);
}


static void
svr4_relocate_section_addresses (struct so_list *so,
                                 struct section_table *sec)
{
  sec->addr    = svr4_truncate_ptr (sec->addr    + LM_ADDR (so));
  sec->endaddr = svr4_truncate_ptr (sec->endaddr + LM_ADDR (so));
}


/* Fetch a link_map_offsets structure for native targets using struct
   definitions from link.h.  See solib-legacy.c for the function
   which does the actual work.
   
   Note: For non-native targets (i.e. cross-debugging situations),
   a target specific fetch_link_map_offsets() function should be
   defined and registered via set_solib_svr4_fetch_link_map_offsets().  */

static struct link_map_offsets *
legacy_fetch_link_map_offsets (void)
{
  if (legacy_svr4_fetch_link_map_offsets_hook)
    return legacy_svr4_fetch_link_map_offsets_hook ();
  else
    {
      internal_error (__FILE__, __LINE__,
                      "legacy_fetch_link_map_offsets called without legacy "
		      "link_map support enabled.");
      return 0;
    }
}

/* Fetch a link_map_offsets structure using the method registered in the
   architecture vector.  */

static struct link_map_offsets *
svr4_fetch_link_map_offsets (void)
{
  struct link_map_offsets *(*flmo)(void) =
    gdbarch_data (current_gdbarch, fetch_link_map_offsets_gdbarch_data);

  if (flmo == NULL)
    {
      internal_error (__FILE__, __LINE__, 
                      "svr4_fetch_link_map_offsets: fetch_link_map_offsets "
		      "method not defined for this architecture.");
      return 0;
    }
  else
    return (flmo ());
}

/* Return 1 if a link map offset fetcher has been defined, 0 otherwise.  */
static int
svr4_have_link_map_offsets (void)
{
  struct link_map_offsets *(*flmo)(void) =
    gdbarch_data (current_gdbarch, fetch_link_map_offsets_gdbarch_data);
  if (flmo == NULL
      || (flmo == legacy_fetch_link_map_offsets 
          && legacy_svr4_fetch_link_map_offsets_hook == NULL))
    return 0;
  else
    return 1;
}

/* set_solib_svr4_fetch_link_map_offsets() is intended to be called by
   a <arch>_gdbarch_init() function.  It is used to establish an
   architecture specific link_map_offsets fetcher for the architecture
   being defined.  */

void
set_solib_svr4_fetch_link_map_offsets (struct gdbarch *gdbarch,
                                       struct link_map_offsets *(*flmo) (void))
{
  set_gdbarch_data (gdbarch, fetch_link_map_offsets_gdbarch_data, flmo);
}

/* Initialize the architecture-specific link_map_offsets fetcher.
   This is called after <arch>_gdbarch_init() has set up its `struct
   gdbarch' for the new architecture, and is only called if the
   link_map_offsets fetcher isn't already initialized (which is
   usually done by calling set_solib_svr4_fetch_link_map_offsets()
   above in <arch>_gdbarch_init()).  Therefore we attempt to provide a
   reasonable alternative (for native targets anyway) if the
   <arch>_gdbarch_init() fails to call
   set_solib_svr4_fetch_link_map_offsets().  */

static void *
init_fetch_link_map_offsets (struct gdbarch *gdbarch)
{
  return legacy_fetch_link_map_offsets;
}

/* Most OS'es that have SVR4-style ELF dynamic libraries define a
   `struct r_debug' and a `struct link_map' that are binary compatible
   with the origional SVR4 implementation.  */

/* Fetch (and possibly build) an appropriate `struct link_map_offsets'
   for an ILP32 SVR4 system.  */
  
struct link_map_offsets *
svr4_ilp32_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      /* Everything we need is in the first 8 bytes.  */
      lmo.r_debug_size = 8;
      lmo.r_map_offset = 4;
      lmo.r_map_size   = 4;

      /* Everything we need is in the first 20 bytes.  */
      lmo.link_map_size = 20;
      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 4;
      lmo.l_name_offset = 4;
      lmo.l_name_size   = 4;
      lmo.l_next_offset = 12;
      lmo.l_next_size   = 4;
      lmo.l_prev_offset = 16;
      lmo.l_prev_size   = 4;
    }

  return lmp;
}

/* Fetch (and possibly build) an appropriate `struct link_map_offsets'
   for an LP64 SVR4 system.  */
  
struct link_map_offsets *
svr4_lp64_fetch_link_map_offsets (void)
{
  static struct link_map_offsets lmo;
  static struct link_map_offsets *lmp = NULL;

  if (lmp == NULL)
    {
      lmp = &lmo;

      /* Everything we need is in the first 16 bytes.  */
      lmo.r_debug_size = 16;
      lmo.r_map_offset = 8;
      lmo.r_map_size   = 8;

      /* Everything we need is in the first 40 bytes.  */
      lmo.link_map_size = 40;
      lmo.l_addr_offset = 0;
      lmo.l_addr_size   = 8;
      lmo.l_name_offset = 8;
      lmo.l_name_size   = 8;
      lmo.l_next_offset = 24;
      lmo.l_next_size   = 8;
      lmo.l_prev_offset = 32;
      lmo.l_prev_size   = 8;
    }

  return lmp;
}


static struct target_so_ops svr4_so_ops;

extern initialize_file_ftype _initialize_svr4_solib; /* -Wmissing-prototypes */

void
_initialize_svr4_solib (void)
{
  fetch_link_map_offsets_gdbarch_data =
    register_gdbarch_data (init_fetch_link_map_offsets);

  svr4_so_ops.relocate_section_addresses = svr4_relocate_section_addresses;
  svr4_so_ops.free_so = svr4_free_so;
  svr4_so_ops.clear_solib = svr4_clear_solib;
  svr4_so_ops.solib_create_inferior_hook = svr4_solib_create_inferior_hook;
  svr4_so_ops.special_symbol_handling = svr4_special_symbol_handling;
  svr4_so_ops.current_sos = svr4_current_sos;
  svr4_so_ops.open_symbol_file_object = open_symbol_file_object;
  svr4_so_ops.in_dynsym_resolve_code = svr4_in_dynsym_resolve_code;

  /* FIXME: Don't do this here.  *_gdbarch_init() should set so_ops. */
  current_target_so_ops = &svr4_so_ops;
}
