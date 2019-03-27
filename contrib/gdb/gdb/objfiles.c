/* GDB routines for manipulating objfiles.

   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004 Free Software Foundation, Inc.

   Contributed by Cygnus Support, using pieces from other GDB modules.

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

/* This file contains support routines for creating, manipulating, and
   destroying objfile structures. */

#include "defs.h"
#include "bfd.h"		/* Binary File Description */
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "gdb-stabs.h"
#include "target.h"
#include "bcache.h"

#include "gdb_assert.h"
#include <sys/types.h>
#include "gdb_stat.h"
#include <fcntl.h>
#include "gdb_obstack.h"
#include "gdb_string.h"
#include "hashtab.h"

#include "breakpoint.h"
#include "block.h"
#include "dictionary.h"

/* Prototypes for local functions */

static void objfile_alloc_data (struct objfile *objfile);
static void objfile_free_data (struct objfile *objfile);

/* Externally visible variables that are owned by this module.
   See declarations in objfile.h for more info. */

struct objfile *object_files;	/* Linked list of all objfiles */
struct objfile *current_objfile;	/* For symbol file being read in */
struct objfile *symfile_objfile;	/* Main symbol table loaded from */
struct objfile *rt_common_objfile;	/* For runtime common symbols */

/* Locate all mappable sections of a BFD file. 
   objfile_p_char is a char * to get it through
   bfd_map_over_sections; we cast it back to its proper type.  */

#ifndef TARGET_KEEP_SECTION
#define TARGET_KEEP_SECTION(ASECT)	0
#endif

/* Called via bfd_map_over_sections to build up the section table that
   the objfile references.  The objfile contains pointers to the start
   of the table (objfile->sections) and to the first location after
   the end of the table (objfile->sections_end). */

static void
add_to_objfile_sections (struct bfd *abfd, struct bfd_section *asect,
			 void *objfile_p_char)
{
  struct objfile *objfile = (struct objfile *) objfile_p_char;
  struct obj_section section;
  flagword aflag;

  aflag = bfd_get_section_flags (abfd, asect);

  if (!(aflag & SEC_ALLOC) && !(TARGET_KEEP_SECTION (asect)))
    return;

  if (0 == bfd_section_size (abfd, asect))
    return;
  section.offset = 0;
  section.objfile = objfile;
  section.the_bfd_section = asect;
  section.ovly_mapped = 0;
  section.addr = bfd_section_vma (abfd, asect);
  section.endaddr = section.addr + bfd_section_size (abfd, asect);
  obstack_grow (&objfile->objfile_obstack, (char *) &section, sizeof (section));
  objfile->sections_end = (struct obj_section *) (((unsigned long) objfile->sections_end) + 1);
}

/* Builds a section table for OBJFILE.
   Returns 0 if OK, 1 on error (in which case bfd_error contains the
   error).

   Note that while we are building the table, which goes into the
   psymbol obstack, we hijack the sections_end pointer to instead hold
   a count of the number of sections.  When bfd_map_over_sections
   returns, this count is used to compute the pointer to the end of
   the sections table, which then overwrites the count.

   Also note that the OFFSET and OVLY_MAPPED in each table entry
   are initialized to zero.

   Also note that if anything else writes to the psymbol obstack while
   we are building the table, we're pretty much hosed. */

int
build_objfile_section_table (struct objfile *objfile)
{
  /* objfile->sections can be already set when reading a mapped symbol
     file.  I believe that we do need to rebuild the section table in
     this case (we rebuild other things derived from the bfd), but we
     can't free the old one (it's in the objfile_obstack).  So we just
     waste some memory.  */

  objfile->sections_end = 0;
  bfd_map_over_sections (objfile->obfd, add_to_objfile_sections, (char *) objfile);
  objfile->sections = (struct obj_section *)
    obstack_finish (&objfile->objfile_obstack);
  objfile->sections_end = objfile->sections + (unsigned long) objfile->sections_end;
  return (0);
}

/* Given a pointer to an initialized bfd (ABFD) and some flag bits
   allocate a new objfile struct, fill it in as best we can, link it
   into the list of all known objfiles, and return a pointer to the
   new objfile struct.

   The FLAGS word contains various bits (OBJF_*) that can be taken as
   requests for specific operations.  Other bits like OBJF_SHARED are
   simply copied through to the new objfile flags member. */

/* NOTE: carlton/2003-02-04: This function is called with args NULL, 0
   by jv-lang.c, to create an artificial objfile used to hold
   information about dynamically-loaded Java classes.  Unfortunately,
   that branch of this function doesn't get tested very frequently, so
   it's prone to breakage.  (E.g. at one time the name was set to NULL
   in that situation, which broke a loop over all names in the dynamic
   library loader.)  If you change this function, please try to leave
   things in a consistent state even if abfd is NULL.  */

struct objfile *
allocate_objfile (bfd *abfd, int flags)
{
  struct objfile *objfile = NULL;
  struct objfile *last_one = NULL;

  /* If we don't support mapped symbol files, didn't ask for the file to be
     mapped, or failed to open the mapped file for some reason, then revert
     back to an unmapped objfile. */

  if (objfile == NULL)
    {
      objfile = (struct objfile *) xmalloc (sizeof (struct objfile));
      memset (objfile, 0, sizeof (struct objfile));
      objfile->md = NULL;
      objfile->psymbol_cache = bcache_xmalloc ();
      objfile->macro_cache = bcache_xmalloc ();
      /* We could use obstack_specify_allocation here instead, but
	 gdb_obstack.h specifies the alloc/dealloc functions.  */
      obstack_init (&objfile->objfile_obstack);
      terminate_minimal_symbol_table (objfile);
    }

  objfile_alloc_data (objfile);

  /* Update the per-objfile information that comes from the bfd, ensuring
     that any data that is reference is saved in the per-objfile data
     region. */

  objfile->obfd = abfd;
  if (objfile->name != NULL)
    {
      xmfree (objfile->md, objfile->name);
    }
  if (abfd != NULL)
    {
      objfile->name = mstrsave (objfile->md, bfd_get_filename (abfd));
      objfile->mtime = bfd_get_mtime (abfd);

      /* Build section table.  */

      if (build_objfile_section_table (objfile))
	{
	  error ("Can't find the file sections in `%s': %s",
		 objfile->name, bfd_errmsg (bfd_get_error ()));
	}
    }
  else
    {
      objfile->name = mstrsave (objfile->md, "<<anonymous objfile>>");
    }

  /* Initialize the section indexes for this objfile, so that we can
     later detect if they are used w/o being properly assigned to. */

  objfile->sect_index_text = -1;
  objfile->sect_index_data = -1;
  objfile->sect_index_bss = -1;
  objfile->sect_index_rodata = -1;

  /* We don't yet have a C++-specific namespace symtab.  */

  objfile->cp_namespace_symtab = NULL;

  /* Add this file onto the tail of the linked list of other such files. */

  objfile->next = NULL;
  if (object_files == NULL)
    object_files = objfile;
  else
    {
      for (last_one = object_files;
	   last_one->next;
	   last_one = last_one->next);
      last_one->next = objfile;
    }

  /* Save passed in flag bits. */
  objfile->flags |= flags;

  return (objfile);
}

/* Initialize entry point information for this objfile. */

void
init_entry_point_info (struct objfile *objfile)
{
  /* Save startup file's range of PC addresses to help blockframe.c
     decide where the bottom of the stack is.  */

  if (bfd_get_file_flags (objfile->obfd) & EXEC_P)
    {
      /* Executable file -- record its entry point so we'll recognize
         the startup file because it contains the entry point.  */
      objfile->ei.entry_point = bfd_get_start_address (objfile->obfd);
    }
  else
    {
      /* Examination of non-executable.o files.  Short-circuit this stuff.  */
      objfile->ei.entry_point = INVALID_ENTRY_POINT;
    }
  objfile->ei.deprecated_entry_file_lowpc = INVALID_ENTRY_LOWPC;
  objfile->ei.deprecated_entry_file_highpc = INVALID_ENTRY_HIGHPC;
  objfile->ei.entry_func_lowpc = INVALID_ENTRY_LOWPC;
  objfile->ei.entry_func_highpc = INVALID_ENTRY_HIGHPC;
  objfile->ei.main_func_lowpc = INVALID_ENTRY_LOWPC;
  objfile->ei.main_func_highpc = INVALID_ENTRY_HIGHPC;
}

/* Get current entry point address.  */

CORE_ADDR
entry_point_address (void)
{
  return symfile_objfile ? symfile_objfile->ei.entry_point : 0;
}

/* Create the terminating entry of OBJFILE's minimal symbol table.
   If OBJFILE->msymbols is zero, allocate a single entry from
   OBJFILE->objfile_obstack; otherwise, just initialize
   OBJFILE->msymbols[OBJFILE->minimal_symbol_count].  */
void
terminate_minimal_symbol_table (struct objfile *objfile)
{
  if (! objfile->msymbols)
    objfile->msymbols = ((struct minimal_symbol *)
                         obstack_alloc (&objfile->objfile_obstack,
                                        sizeof (objfile->msymbols[0])));

  {
    struct minimal_symbol *m
      = &objfile->msymbols[objfile->minimal_symbol_count];

    memset (m, 0, sizeof (*m));
    /* Don't rely on these enumeration values being 0's.  */
    MSYMBOL_TYPE (m) = mst_unknown;
    SYMBOL_INIT_LANGUAGE_SPECIFIC (m, language_unknown);
  }
}


/* Put one object file before a specified on in the global list.
   This can be used to make sure an object file is destroyed before
   another when using ALL_OBJFILES_SAFE to free all objfiles. */
void
put_objfile_before (struct objfile *objfile, struct objfile *before_this)
{
  struct objfile **objp;

  unlink_objfile (objfile);
  
  for (objp = &object_files; *objp != NULL; objp = &((*objp)->next))
    {
      if (*objp == before_this)
	{
	  objfile->next = *objp;
	  *objp = objfile;
	  return;
	}
    }
  
  internal_error (__FILE__, __LINE__,
		  "put_objfile_before: before objfile not in list");
}

/* Put OBJFILE at the front of the list.  */

void
objfile_to_front (struct objfile *objfile)
{
  struct objfile **objp;
  for (objp = &object_files; *objp != NULL; objp = &((*objp)->next))
    {
      if (*objp == objfile)
	{
	  /* Unhook it from where it is.  */
	  *objp = objfile->next;
	  /* Put it in the front.  */
	  objfile->next = object_files;
	  object_files = objfile;
	  break;
	}
    }
}

/* Unlink OBJFILE from the list of known objfiles, if it is found in the
   list.

   It is not a bug, or error, to call this function if OBJFILE is not known
   to be in the current list.  This is done in the case of mapped objfiles,
   for example, just to ensure that the mapped objfile doesn't appear twice
   in the list.  Since the list is threaded, linking in a mapped objfile
   twice would create a circular list.

   If OBJFILE turns out to be in the list, we zap it's NEXT pointer after
   unlinking it, just to ensure that we have completely severed any linkages
   between the OBJFILE and the list. */

void
unlink_objfile (struct objfile *objfile)
{
  struct objfile **objpp;

  for (objpp = &object_files; *objpp != NULL; objpp = &((*objpp)->next))
    {
      if (*objpp == objfile)
	{
	  *objpp = (*objpp)->next;
	  objfile->next = NULL;
	  return;
	}
    }

  internal_error (__FILE__, __LINE__,
		  "unlink_objfile: objfile already unlinked");
}


/* Destroy an objfile and all the symtabs and psymtabs under it.  Note
   that as much as possible is allocated on the objfile_obstack 
   so that the memory can be efficiently freed.

   Things which we do NOT free because they are not in malloc'd memory
   or not in memory specific to the objfile include:

   objfile -> sf

   FIXME:  If the objfile is using reusable symbol information (via mmalloc),
   then we need to take into account the fact that more than one process
   may be using the symbol information at the same time (when mmalloc is
   extended to support cooperative locking).  When more than one process
   is using the mapped symbol info, we need to be more careful about when
   we free objects in the reusable area. */

void
free_objfile (struct objfile *objfile)
{
  if (objfile->separate_debug_objfile)
    {
      free_objfile (objfile->separate_debug_objfile);
    }
  
  if (objfile->separate_debug_objfile_backlink)
    {
      /* We freed the separate debug file, make sure the base objfile
	 doesn't reference it.  */
      objfile->separate_debug_objfile_backlink->separate_debug_objfile = NULL;
    }
  
  /* First do any symbol file specific actions required when we are
     finished with a particular symbol file.  Note that if the objfile
     is using reusable symbol information (via mmalloc) then each of
     these routines is responsible for doing the correct thing, either
     freeing things which are valid only during this particular gdb
     execution, or leaving them to be reused during the next one. */

  if (objfile->sf != NULL)
    {
      (*objfile->sf->sym_finish) (objfile);
    }

  /* We always close the bfd. */

  if (objfile->obfd != NULL)
    {
      char *name = bfd_get_filename (objfile->obfd);
      if (!bfd_close (objfile->obfd))
	warning ("cannot close \"%s\": %s",
		 name, bfd_errmsg (bfd_get_error ()));
      xfree (name);
    }

  /* Remove it from the chain of all objfiles. */

  unlink_objfile (objfile);

  /* If we are going to free the runtime common objfile, mark it
     as unallocated.  */

  if (objfile == rt_common_objfile)
    rt_common_objfile = NULL;

  /* Before the symbol table code was redone to make it easier to
     selectively load and remove information particular to a specific
     linkage unit, gdb used to do these things whenever the monolithic
     symbol table was blown away.  How much still needs to be done
     is unknown, but we play it safe for now and keep each action until
     it is shown to be no longer needed. */

  /* I *think* all our callers call clear_symtab_users.  If so, no need
     to call this here.  */
  clear_pc_function_cache ();

  /* The last thing we do is free the objfile struct itself. */

  objfile_free_data (objfile);
  if (objfile->name != NULL)
    {
      xmfree (objfile->md, objfile->name);
    }
  if (objfile->global_psymbols.list)
    xmfree (objfile->md, objfile->global_psymbols.list);
  if (objfile->static_psymbols.list)
    xmfree (objfile->md, objfile->static_psymbols.list);
  /* Free the obstacks for non-reusable objfiles */
  bcache_xfree (objfile->psymbol_cache);
  bcache_xfree (objfile->macro_cache);
  if (objfile->demangled_names_hash)
    htab_delete (objfile->demangled_names_hash);
  obstack_free (&objfile->objfile_obstack, 0);
  xmfree (objfile->md, objfile);
  objfile = NULL;
}

static void
do_free_objfile_cleanup (void *obj)
{
  free_objfile (obj);
}

struct cleanup *
make_cleanup_free_objfile (struct objfile *obj)
{
  return make_cleanup (do_free_objfile_cleanup, obj);
}

/* Free all the object files at once and clean up their users.  */

void
free_all_objfiles (void)
{
  struct objfile *objfile, *temp;

  ALL_OBJFILES_SAFE (objfile, temp)
  {
    free_objfile (objfile);
  }
  clear_symtab_users ();
}

/* Relocate OBJFILE to NEW_OFFSETS.  There should be OBJFILE->NUM_SECTIONS
   entries in new_offsets.  */
void
objfile_relocate (struct objfile *objfile, struct section_offsets *new_offsets)
{
  struct section_offsets *delta =
    ((struct section_offsets *) 
     alloca (SIZEOF_N_SECTION_OFFSETS (objfile->num_sections)));

  {
    int i;
    int something_changed = 0;
    for (i = 0; i < objfile->num_sections; ++i)
      {
	delta->offsets[i] =
	  ANOFFSET (new_offsets, i) - ANOFFSET (objfile->section_offsets, i);
	if (ANOFFSET (delta, i) != 0)
	  something_changed = 1;
      }
    if (!something_changed)
      return;
  }

  /* OK, get all the symtabs.  */
  {
    struct symtab *s;

    ALL_OBJFILE_SYMTABS (objfile, s)
    {
      struct linetable *l;
      struct blockvector *bv;
      int i;

      /* First the line table.  */
      l = LINETABLE (s);
      if (l)
	{
	  for (i = 0; i < l->nitems; ++i)
	    l->item[i].pc += ANOFFSET (delta, s->block_line_section);
	}

      /* Don't relocate a shared blockvector more than once.  */
      if (!s->primary)
	continue;

      bv = BLOCKVECTOR (s);
      for (i = 0; i < BLOCKVECTOR_NBLOCKS (bv); ++i)
	{
	  struct block *b;
	  struct symbol *sym;
	  struct dict_iterator iter;

	  b = BLOCKVECTOR_BLOCK (bv, i);
	  BLOCK_START (b) += ANOFFSET (delta, s->block_line_section);
	  BLOCK_END (b) += ANOFFSET (delta, s->block_line_section);

	  ALL_BLOCK_SYMBOLS (b, iter, sym)
	    {
	      fixup_symbol_section (sym, objfile);

	      /* The RS6000 code from which this was taken skipped
	         any symbols in STRUCT_DOMAIN or UNDEF_DOMAIN.
	         But I'm leaving out that test, on the theory that
	         they can't possibly pass the tests below.  */
	      if ((SYMBOL_CLASS (sym) == LOC_LABEL
		   || SYMBOL_CLASS (sym) == LOC_STATIC
		   || SYMBOL_CLASS (sym) == LOC_INDIRECT)
		  && SYMBOL_SECTION (sym) >= 0)
		{
		  SYMBOL_VALUE_ADDRESS (sym) +=
		    ANOFFSET (delta, SYMBOL_SECTION (sym));
		}
#ifdef MIPS_EFI_SYMBOL_NAME
	      /* Relocate Extra Function Info for ecoff.  */

	      else if (SYMBOL_CLASS (sym) == LOC_CONST
		       && SYMBOL_DOMAIN (sym) == LABEL_DOMAIN
		       && strcmp (DEPRECATED_SYMBOL_NAME (sym), MIPS_EFI_SYMBOL_NAME) == 0)
		ecoff_relocate_efi (sym, ANOFFSET (delta,
						   s->block_line_section));
#endif
	    }
	}
    }
  }

  {
    struct partial_symtab *p;

    ALL_OBJFILE_PSYMTABS (objfile, p)
    {
      p->textlow += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
      p->texthigh += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
    }
  }

  {
    struct partial_symbol **psym;

    for (psym = objfile->global_psymbols.list;
	 psym < objfile->global_psymbols.next;
	 psym++)
      {
	fixup_psymbol_section (*psym, objfile);
	if (SYMBOL_SECTION (*psym) >= 0)
	  SYMBOL_VALUE_ADDRESS (*psym) += ANOFFSET (delta,
						    SYMBOL_SECTION (*psym));
      }
    for (psym = objfile->static_psymbols.list;
	 psym < objfile->static_psymbols.next;
	 psym++)
      {
	fixup_psymbol_section (*psym, objfile);
	if (SYMBOL_SECTION (*psym) >= 0)
	  SYMBOL_VALUE_ADDRESS (*psym) += ANOFFSET (delta,
						    SYMBOL_SECTION (*psym));
      }
  }

  {
    struct minimal_symbol *msym;
    ALL_OBJFILE_MSYMBOLS (objfile, msym)
      if (SYMBOL_SECTION (msym) >= 0)
      SYMBOL_VALUE_ADDRESS (msym) += ANOFFSET (delta, SYMBOL_SECTION (msym));
  }
  /* Relocating different sections by different amounts may cause the symbols
     to be out of order.  */
  msymbols_sort (objfile);

  {
    int i;
    for (i = 0; i < objfile->num_sections; ++i)
      (objfile->section_offsets)->offsets[i] = ANOFFSET (new_offsets, i);
  }

  if (objfile->ei.entry_point != ~(CORE_ADDR) 0)
    {
      /* Relocate ei.entry_point with its section offset, use SECT_OFF_TEXT
	 only as a fallback.  */
      struct obj_section *s;
      s = find_pc_section (objfile->ei.entry_point);
      if (s)
        objfile->ei.entry_point += ANOFFSET (delta, s->the_bfd_section->index);
      else
        objfile->ei.entry_point += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
    }

  {
    struct obj_section *s;
    bfd *abfd;

    abfd = objfile->obfd;

    ALL_OBJFILE_OSECTIONS (objfile, s)
      {
      	int idx = s->the_bfd_section->index;
	
	s->addr += ANOFFSET (delta, idx);
	s->endaddr += ANOFFSET (delta, idx);
      }
  }

  if (objfile->ei.entry_func_lowpc != INVALID_ENTRY_LOWPC)
    {
      objfile->ei.entry_func_lowpc += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
      objfile->ei.entry_func_highpc += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
    }

  if (objfile->ei.deprecated_entry_file_lowpc != INVALID_ENTRY_LOWPC)
    {
      objfile->ei.deprecated_entry_file_lowpc += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
      objfile->ei.deprecated_entry_file_highpc += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
    }

  if (objfile->ei.main_func_lowpc != INVALID_ENTRY_LOWPC)
    {
      objfile->ei.main_func_lowpc += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
      objfile->ei.main_func_highpc += ANOFFSET (delta, SECT_OFF_TEXT (objfile));
    }

  /* Relocate breakpoints as necessary, after things are relocated. */
  breakpoint_re_set ();
}

/* Many places in gdb want to test just to see if we have any partial
   symbols available.  This function returns zero if none are currently
   available, nonzero otherwise. */

int
have_partial_symbols (void)
{
  struct objfile *ofp;

  ALL_OBJFILES (ofp)
  {
    if (ofp->psymtabs != NULL)
      {
	return 1;
      }
  }
  return 0;
}

/* Many places in gdb want to test just to see if we have any full
   symbols available.  This function returns zero if none are currently
   available, nonzero otherwise. */

int
have_full_symbols (void)
{
  struct objfile *ofp;

  ALL_OBJFILES (ofp)
  {
    if (ofp->symtabs != NULL)
      {
	return 1;
      }
  }
  return 0;
}


/* This operations deletes all objfile entries that represent solibs that
   weren't explicitly loaded by the user, via e.g., the add-symbol-file
   command.
 */
void
objfile_purge_solibs (void)
{
  struct objfile *objf;
  struct objfile *temp;

  ALL_OBJFILES_SAFE (objf, temp)
  {
    /* We assume that the solib package has been purged already, or will
       be soon.
     */
    if (!(objf->flags & OBJF_USERLOADED) && (objf->flags & OBJF_SHARED))
      free_objfile (objf);
  }
}


/* Many places in gdb want to test just to see if we have any minimal
   symbols available.  This function returns zero if none are currently
   available, nonzero otherwise. */

int
have_minimal_symbols (void)
{
  struct objfile *ofp;

  ALL_OBJFILES (ofp)
  {
    if (ofp->minimal_symbol_count > 0)
      {
	return 1;
      }
  }
  return 0;
}

/* Returns a section whose range includes PC and SECTION, or NULL if
   none found.  Note the distinction between the return type, struct
   obj_section (which is defined in gdb), and the input type "struct
   bfd_section" (which is a bfd-defined data type).  The obj_section
   contains a pointer to the "struct bfd_section".  */

struct obj_section *
find_pc_sect_section (CORE_ADDR pc, struct bfd_section *section)
{
  struct obj_section *s;
  struct objfile *objfile;

  ALL_OBJSECTIONS (objfile, s)
    if ((section == 0 || section == s->the_bfd_section) &&
	s->addr <= pc && pc < s->endaddr)
      return (s);

  return (NULL);
}

/* Returns a section whose range includes PC or NULL if none found. 
   Backward compatibility, no section.  */

struct obj_section *
find_pc_section (CORE_ADDR pc)
{
  return find_pc_sect_section (pc, find_pc_mapped_section (pc));
}


/* In SVR4, we recognize a trampoline by it's section name. 
   That is, if the pc is in a section named ".plt" then we are in
   a trampoline.  */

int
in_plt_section (CORE_ADDR pc, char *name)
{
  struct obj_section *s;
  int retval = 0;

  s = find_pc_section (pc);

  retval = (s != NULL
	    && s->the_bfd_section->name != NULL
	    && strcmp (s->the_bfd_section->name, ".plt") == 0);
  return (retval);
}

/* Return nonzero if NAME is in the import list of OBJFILE.  Else
   return zero.  */

int
is_in_import_list (char *name, struct objfile *objfile)
{
  int i;

  if (!objfile || !name || !*name)
    return 0;

  for (i = 0; i < objfile->import_list_size; i++)
    if (objfile->import_list[i] && DEPRECATED_STREQ (name, objfile->import_list[i]))
      return 1;
  return 0;
}


/* Keep a registry of per-objfile data-pointers required by other GDB
   modules.  */

struct objfile_data
{
  unsigned index;
};

struct objfile_data_registration
{
  struct objfile_data *data;
  struct objfile_data_registration *next;
};
  
struct objfile_data_registry
{
  struct objfile_data_registration *registrations;
  unsigned num_registrations;
};

static struct objfile_data_registry objfile_data_registry = { NULL, 0 };

const struct objfile_data *
register_objfile_data (void)
{
  struct objfile_data_registration **curr;

  /* Append new registration.  */
  for (curr = &objfile_data_registry.registrations;
       *curr != NULL; curr = &(*curr)->next);

  *curr = XMALLOC (struct objfile_data_registration);
  (*curr)->next = NULL;
  (*curr)->data = XMALLOC (struct objfile_data);
  (*curr)->data->index = objfile_data_registry.num_registrations++;

  return (*curr)->data;
}

static void
objfile_alloc_data (struct objfile *objfile)
{
  gdb_assert (objfile->data == NULL);
  objfile->num_data = objfile_data_registry.num_registrations;
  objfile->data = XCALLOC (objfile->num_data, void *);
}

static void
objfile_free_data (struct objfile *objfile)
{
  gdb_assert (objfile->data != NULL);
  xfree (objfile->data);
  objfile->data = NULL;
}

void
clear_objfile_data (struct objfile *objfile)
{
  gdb_assert (objfile->data != NULL);
  memset (objfile->data, 0, objfile->num_data * sizeof (void *));
}

void
set_objfile_data (struct objfile *objfile, const struct objfile_data *data,
		  void *value)
{
  gdb_assert (data->index < objfile->num_data);
  objfile->data[data->index] = value;
}

void *
objfile_data (struct objfile *objfile, const struct objfile_data *data)
{
  gdb_assert (data->index < objfile->num_data);
  return objfile->data[data->index];
}
