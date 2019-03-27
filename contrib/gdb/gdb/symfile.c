/* Generic symbol file reading for the GNU debugger, GDB.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#include "defs.h"
#include "bfdlink.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcore.h"
#include "frame.h"
#include "target.h"
#include "value.h"
#include "symfile.h"
#include "objfiles.h"
#include "source.h"
#include "gdbcmd.h"
#include "breakpoint.h"
#include "language.h"
#include "complaints.h"
#include "demangle.h"
#include "inferior.h"		/* for write_pc */
#include "filenames.h"		/* for DOSish file names */
#include "gdb-stabs.h"
#include "gdb_obstack.h"
#include "completer.h"
#include "bcache.h"
#include "hashtab.h"
#include "readline/readline.h"
#include "gdb_assert.h"
#include "block.h"

#include <sys/types.h>
#include <fcntl.h>
#include "gdb_string.h"
#include "gdb_stat.h"
#include <ctype.h>
#include <time.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef HPUXHPPA

/* Some HP-UX related globals to clear when a new "main"
   symbol file is loaded. HP-specific.  */

extern int hp_som_som_object_present;
extern int hp_cxx_exception_support_initialized;
#define RESET_HP_UX_GLOBALS() do {\
                                    hp_som_som_object_present = 0;             /* indicates HP-compiled code */        \
                                    hp_cxx_exception_support_initialized = 0;  /* must reinitialize exception stuff */ \
                              } while (0)
#endif

int (*ui_load_progress_hook) (const char *section, unsigned long num);
void (*show_load_progress) (const char *section,
			    unsigned long section_sent, 
			    unsigned long section_size, 
			    unsigned long total_sent, 
			    unsigned long total_size);
void (*pre_add_symbol_hook) (char *);
void (*post_add_symbol_hook) (void);
void (*target_new_objfile_hook) (struct objfile *);

static void clear_symtab_users_cleanup (void *ignore);

/* Global variables owned by this file */
int readnow_symbol_files;	/* Read full symbols immediately */

/* External variables and functions referenced. */

extern void report_transfer_performance (unsigned long, time_t, time_t);

/* Functions this file defines */

#if 0
static int simple_read_overlay_region_table (void);
static void simple_free_overlay_region_table (void);
#endif

static void set_initial_language (void);

static void load_command (char *, int);

static void symbol_file_add_main_1 (char *args, int from_tty, int flags);

static void add_symbol_file_command (char *, int);

static void add_shared_symbol_files_command (char *, int);

static void reread_separate_symbols (struct objfile *objfile);

static void cashier_psymtab (struct partial_symtab *);

bfd *symfile_bfd_open (char *);

int get_section_index (struct objfile *, char *);

static void find_sym_fns (struct objfile *);

static void decrement_reading_symtab (void *);

static void overlay_invalidate_all (void);

static int overlay_is_mapped (struct obj_section *);

void list_overlays_command (char *, int);

void map_overlay_command (char *, int);

void unmap_overlay_command (char *, int);

static void overlay_auto_command (char *, int);

static void overlay_manual_command (char *, int);

static void overlay_off_command (char *, int);

static void overlay_load_command (char *, int);

static void overlay_command (char *, int);

static void simple_free_overlay_table (void);

static void read_target_long_array (CORE_ADDR, unsigned int *, int);

static int simple_read_overlay_table (void);

static int simple_overlay_update_1 (struct obj_section *);

static void add_filename_language (char *ext, enum language lang);

static void set_ext_lang_command (char *args, int from_tty);

static void info_ext_lang_command (char *args, int from_tty);

static char *find_separate_debug_file (struct objfile *objfile);

static void init_filename_language_table (void);

void _initialize_symfile (void);

/* List of all available sym_fns.  On gdb startup, each object file reader
   calls add_symtab_fns() to register information on each format it is
   prepared to read. */

static struct sym_fns *symtab_fns = NULL;

/* Flag for whether user will be reloading symbols multiple times.
   Defaults to ON for VxWorks, otherwise OFF.  */

#ifdef SYMBOL_RELOADING_DEFAULT
int symbol_reloading = SYMBOL_RELOADING_DEFAULT;
#else
int symbol_reloading = 0;
#endif

/* If non-zero, shared library symbols will be added automatically
   when the inferior is created, new libraries are loaded, or when
   attaching to the inferior.  This is almost always what users will
   want to have happen; but for very large programs, the startup time
   will be excessive, and so if this is a problem, the user can clear
   this flag and then add the shared library symbols as needed.  Note
   that there is a potential for confusion, since if the shared
   library symbols are not loaded, commands like "info fun" will *not*
   report all the functions that are actually present. */

int auto_solib_add = 1;

/* For systems that support it, a threshold size in megabytes.  If
   automatically adding a new library's symbol table to those already
   known to the debugger would cause the total shared library symbol
   size to exceed this threshhold, then the shlib's symbols are not
   added.  The threshold is ignored if the user explicitly asks for a
   shlib to be added, such as when using the "sharedlibrary"
   command. */

int auto_solib_limit;


/* This compares two partial symbols by names, using strcmp_iw_ordered
   for the comparison.  */

static int
compare_psymbols (const void *s1p, const void *s2p)
{
  struct partial_symbol *const *s1 = s1p;
  struct partial_symbol *const *s2 = s2p;

  return strcmp_iw_ordered (SYMBOL_NATURAL_NAME (*s1),
			    SYMBOL_NATURAL_NAME (*s2));
}

void
sort_pst_symbols (struct partial_symtab *pst)
{
  /* Sort the global list; don't sort the static list */

  qsort (pst->objfile->global_psymbols.list + pst->globals_offset,
	 pst->n_global_syms, sizeof (struct partial_symbol *),
	 compare_psymbols);
}

/* Make a null terminated copy of the string at PTR with SIZE characters in
   the obstack pointed to by OBSTACKP .  Returns the address of the copy.
   Note that the string at PTR does not have to be null terminated, I.E. it
   may be part of a larger string and we are only saving a substring. */

char *
obsavestring (const char *ptr, int size, struct obstack *obstackp)
{
  char *p = (char *) obstack_alloc (obstackp, size + 1);
  /* Open-coded memcpy--saves function call time.  These strings are usually
     short.  FIXME: Is this really still true with a compiler that can
     inline memcpy? */
  {
    const char *p1 = ptr;
    char *p2 = p;
    const char *end = ptr + size;
    while (p1 != end)
      *p2++ = *p1++;
  }
  p[size] = 0;
  return p;
}

/* Concatenate strings S1, S2 and S3; return the new string.  Space is found
   in the obstack pointed to by OBSTACKP.  */

char *
obconcat (struct obstack *obstackp, const char *s1, const char *s2,
	  const char *s3)
{
  int len = strlen (s1) + strlen (s2) + strlen (s3) + 1;
  char *val = (char *) obstack_alloc (obstackp, len);
  strcpy (val, s1);
  strcat (val, s2);
  strcat (val, s3);
  return val;
}

/* True if we are nested inside psymtab_to_symtab. */

int currently_reading_symtab = 0;

static void
decrement_reading_symtab (void *dummy)
{
  currently_reading_symtab--;
}

/* Get the symbol table that corresponds to a partial_symtab.
   This is fast after the first time you do it.  In fact, there
   is an even faster macro PSYMTAB_TO_SYMTAB that does the fast
   case inline.  */

struct symtab *
psymtab_to_symtab (struct partial_symtab *pst)
{
  /* If it's been looked up before, return it. */
  if (pst->symtab)
    return pst->symtab;

  /* If it has not yet been read in, read it.  */
  if (!pst->readin)
    {
      struct cleanup *back_to = make_cleanup (decrement_reading_symtab, NULL);
      currently_reading_symtab++;
      (*pst->read_symtab) (pst);
      do_cleanups (back_to);
    }

  return pst->symtab;
}

/* Remember the lowest-addressed loadable section we've seen.  
   This function is called via bfd_map_over_sections. 

   In case of equal vmas, the section with the largest size becomes the
   lowest-addressed loadable section.

   If the vmas and sizes are equal, the last section is considered the
   lowest-addressed loadable section.  */

void
find_lowest_section (bfd *abfd, asection *sect, void *obj)
{
  asection **lowest = (asection **) obj;

  if (0 == (bfd_get_section_flags (abfd, sect) & SEC_LOAD))
    return;
  if (!*lowest)
    *lowest = sect;		/* First loadable section */
  else if (bfd_section_vma (abfd, *lowest) > bfd_section_vma (abfd, sect))
    *lowest = sect;		/* A lower loadable section */
  else if (bfd_section_vma (abfd, *lowest) == bfd_section_vma (abfd, sect)
	   && (bfd_section_size (abfd, (*lowest))
	       <= bfd_section_size (abfd, sect)))
    *lowest = sect;
}

/* Create a new section_addr_info, with room for NUM_SECTIONS.  */

struct section_addr_info *
alloc_section_addr_info (size_t num_sections)
{
  struct section_addr_info *sap;
  size_t size;

  size = (sizeof (struct section_addr_info)
	  +  sizeof (struct other_sections) * (num_sections - 1));
  sap = (struct section_addr_info *) xmalloc (size);
  memset (sap, 0, size);
  sap->num_sections = num_sections;

  return sap;
}

/* Build (allocate and populate) a section_addr_info struct from
   an existing section table. */

extern struct section_addr_info *
build_section_addr_info_from_section_table (const struct section_table *start,
                                            const struct section_table *end)
{
  struct section_addr_info *sap;
  const struct section_table *stp;
  int oidx;

  sap = alloc_section_addr_info (end - start);

  for (stp = start, oidx = 0; stp != end; stp++)
    {
      if (bfd_get_section_flags (stp->bfd, 
				 stp->the_bfd_section) & (SEC_ALLOC | SEC_LOAD)
	  && oidx < end - start)
	{
	  sap->other[oidx].addr = stp->addr;
	  sap->other[oidx].name 
	    = xstrdup (bfd_section_name (stp->bfd, stp->the_bfd_section));
	  sap->other[oidx].sectindex = stp->the_bfd_section->index;
	  oidx++;
	}
    }

  return sap;
}


/* Free all memory allocated by build_section_addr_info_from_section_table. */

extern void
free_section_addr_info (struct section_addr_info *sap)
{
  int idx;

  for (idx = 0; idx < sap->num_sections; idx++)
    if (sap->other[idx].name)
      xfree (sap->other[idx].name);
  xfree (sap);
}


/* Initialize OBJFILE's sect_index_* members.  */
static void
init_objfile_sect_indices (struct objfile *objfile)
{
  asection *sect;
  int i;
  
  sect = bfd_get_section_by_name (objfile->obfd, ".text");
  if (sect) 
    objfile->sect_index_text = sect->index;

  sect = bfd_get_section_by_name (objfile->obfd, ".data");
  if (sect) 
    objfile->sect_index_data = sect->index;

  sect = bfd_get_section_by_name (objfile->obfd, ".bss");
  if (sect) 
    objfile->sect_index_bss = sect->index;

  sect = bfd_get_section_by_name (objfile->obfd, ".rodata");
  if (sect) 
    objfile->sect_index_rodata = sect->index;

  /* This is where things get really weird...  We MUST have valid
     indices for the various sect_index_* members or gdb will abort.
     So if for example, there is no ".text" section, we have to
     accomodate that.  Except when explicitly adding symbol files at
     some address, section_offsets contains nothing but zeros, so it
     doesn't matter which slot in section_offsets the individual
     sect_index_* members index into.  So if they are all zero, it is
     safe to just point all the currently uninitialized indices to the
     first slot. */

  for (i = 0; i < objfile->num_sections; i++)
    {
      if (ANOFFSET (objfile->section_offsets, i) != 0)
	{
	  break;
	}
    }
  if (i == objfile->num_sections)
    {
      if (objfile->sect_index_text == -1)
	objfile->sect_index_text = 0;
      if (objfile->sect_index_data == -1)
	objfile->sect_index_data = 0;
      if (objfile->sect_index_bss == -1)
	objfile->sect_index_bss = 0;
      if (objfile->sect_index_rodata == -1)
	objfile->sect_index_rodata = 0;
    }
}


/* Parse the user's idea of an offset for dynamic linking, into our idea
   of how to represent it for fast symbol reading.  This is the default 
   version of the sym_fns.sym_offsets function for symbol readers that
   don't need to do anything special.  It allocates a section_offsets table
   for the objectfile OBJFILE and stuffs ADDR into all of the offsets.  */

void
default_symfile_offsets (struct objfile *objfile,
			 struct section_addr_info *addrs)
{
  int i;

  objfile->num_sections = bfd_count_sections (objfile->obfd);
  objfile->section_offsets = (struct section_offsets *)
    obstack_alloc (&objfile->objfile_obstack, 
		   SIZEOF_N_SECTION_OFFSETS (objfile->num_sections));
  memset (objfile->section_offsets, 0, 
	  SIZEOF_N_SECTION_OFFSETS (objfile->num_sections));

  /* Now calculate offsets for section that were specified by the
     caller. */
  for (i = 0; i < addrs->num_sections && addrs->other[i].name; i++)
    {
      struct other_sections *osp ;

      osp = &addrs->other[i] ;
      if (osp->addr == 0)
  	continue;

      /* Record all sections in offsets */
      /* The section_offsets in the objfile are here filled in using
         the BFD index. */
      (objfile->section_offsets)->offsets[osp->sectindex] = osp->addr;
    }

  /* Remember the bfd indexes for the .text, .data, .bss and
     .rodata sections. */
  init_objfile_sect_indices (objfile);
}


/* Process a symbol file, as either the main file or as a dynamically
   loaded file.

   OBJFILE is where the symbols are to be read from.

   ADDRS is the list of section load addresses.  If the user has given
   an 'add-symbol-file' command, then this is the list of offsets and
   addresses he or she provided as arguments to the command; or, if
   we're handling a shared library, these are the actual addresses the
   sections are loaded at, according to the inferior's dynamic linker
   (as gleaned by GDB's shared library code).  We convert each address
   into an offset from the section VMA's as it appears in the object
   file, and then call the file's sym_offsets function to convert this
   into a format-specific offset table --- a `struct section_offsets'.
   If ADDRS is non-zero, OFFSETS must be zero.

   OFFSETS is a table of section offsets already in the right
   format-specific representation.  NUM_OFFSETS is the number of
   elements present in OFFSETS->offsets.  If OFFSETS is non-zero, we
   assume this is the proper table the call to sym_offsets described
   above would produce.  Instead of calling sym_offsets, we just dump
   it right into objfile->section_offsets.  (When we're re-reading
   symbols from an objfile, we don't have the original load address
   list any more; all we have is the section offset table.)  If
   OFFSETS is non-zero, ADDRS must be zero.

   MAINLINE is nonzero if this is the main symbol file, or zero if
   it's an extra symbol file such as dynamically loaded code.

   VERBO is nonzero if the caller has printed a verbose message about
   the symbol reading (and complaints can be more terse about it).  */

void
syms_from_objfile (struct objfile *objfile,
                   struct section_addr_info *addrs,
                   struct section_offsets *offsets,
                   int num_offsets,
		   int mainline,
                   int verbo)
{
  struct section_addr_info *local_addr = NULL;
  struct cleanup *old_chain;

  gdb_assert (! (addrs && offsets));

  init_entry_point_info (objfile);
  find_sym_fns (objfile);

  if (objfile->sf == NULL)
    return;	/* No symbols. */

  /* Make sure that partially constructed symbol tables will be cleaned up
     if an error occurs during symbol reading.  */
  old_chain = make_cleanup_free_objfile (objfile);

  /* If ADDRS and OFFSETS are both NULL, put together a dummy address
     list.  We now establish the convention that an addr of zero means
     no load address was specified. */
  if (! addrs && ! offsets)
    {
      local_addr 
	= alloc_section_addr_info (bfd_count_sections (objfile->obfd));
      make_cleanup (xfree, local_addr);
      addrs = local_addr;
    }

  /* Now either addrs or offsets is non-zero.  */

  if (mainline)
    {
      /* We will modify the main symbol table, make sure that all its users
         will be cleaned up if an error occurs during symbol reading.  */
      make_cleanup (clear_symtab_users_cleanup, 0 /*ignore*/);

      /* Since no error yet, throw away the old symbol table.  */

      if (symfile_objfile != NULL)
	{
	  free_objfile (symfile_objfile);
	  symfile_objfile = NULL;
	}

      /* Currently we keep symbols from the add-symbol-file command.
         If the user wants to get rid of them, they should do "symbol-file"
         without arguments first.  Not sure this is the best behavior
         (PR 2207).  */

      (*objfile->sf->sym_new_init) (objfile);
    }

  /* Convert addr into an offset rather than an absolute address.
     We find the lowest address of a loaded segment in the objfile,
     and assume that <addr> is where that got loaded.

     We no longer warn if the lowest section is not a text segment (as
     happens for the PA64 port.  */
  if (!mainline && addrs && addrs->other[0].name)
    {
      asection *lower_sect;
      asection *sect;
      CORE_ADDR lower_offset;
      int i;

      /* Find lowest loadable section to be used as starting point for 
         continguous sections. FIXME!! won't work without call to find
	 .text first, but this assumes text is lowest section. */
      lower_sect = bfd_get_section_by_name (objfile->obfd, ".text");
      if (lower_sect == NULL)
	bfd_map_over_sections (objfile->obfd, find_lowest_section,
			       &lower_sect);
      if (lower_sect == NULL)
	warning ("no loadable sections found in added symbol-file %s",
		 objfile->name);
      else 
	if ((bfd_get_section_flags (objfile->obfd, lower_sect) & SEC_CODE) == 0)
	  warning ("Lowest section in %s is %s at %s",
		   objfile->name,
		   bfd_section_name (objfile->obfd, lower_sect),
		   paddr (bfd_section_vma (objfile->obfd, lower_sect)));
      if (lower_sect != NULL)
 	lower_offset = bfd_section_vma (objfile->obfd, lower_sect);
      else
 	lower_offset = 0;
 
      /* Calculate offsets for the loadable sections.
 	 FIXME! Sections must be in order of increasing loadable section
 	 so that contiguous sections can use the lower-offset!!!
 
         Adjust offsets if the segments are not contiguous.
         If the section is contiguous, its offset should be set to
 	 the offset of the highest loadable section lower than it
 	 (the loadable section directly below it in memory).
 	 this_offset = lower_offset = lower_addr - lower_orig_addr */

        for (i = 0; i < addrs->num_sections && addrs->other[i].name; i++)
          {
            if (addrs->other[i].addr != 0)
              {
                sect = bfd_get_section_by_name (objfile->obfd,
                                                addrs->other[i].name);
                if (sect)
                  {
                    addrs->other[i].addr
                      -= bfd_section_vma (objfile->obfd, sect);
                    lower_offset = addrs->other[i].addr;
                    /* This is the index used by BFD. */
                    addrs->other[i].sectindex = sect->index ;
                  }
                else
                  {
                    warning ("section %s not found in %s",
                             addrs->other[i].name, 
                             objfile->name);
                    addrs->other[i].addr = 0;
                  }
              }
            else
              addrs->other[i].addr = lower_offset;
          }
    }

  /* Initialize symbol reading routines for this objfile, allow complaints to
     appear for this new file, and record how verbose to be, then do the
     initial symbol reading for this file. */

  (*objfile->sf->sym_init) (objfile);
  clear_complaints (&symfile_complaints, 1, verbo);

  if (addrs)
    (*objfile->sf->sym_offsets) (objfile, addrs);
  else
    {
      size_t size = SIZEOF_N_SECTION_OFFSETS (num_offsets);

      /* Just copy in the offset table directly as given to us.  */
      objfile->num_sections = num_offsets;
      objfile->section_offsets
        = ((struct section_offsets *)
           obstack_alloc (&objfile->objfile_obstack, size));
      memcpy (objfile->section_offsets, offsets, size);

      init_objfile_sect_indices (objfile);
    }

#ifndef DEPRECATED_IBM6000_TARGET
  /* This is a SVR4/SunOS specific hack, I think.  In any event, it
     screws RS/6000.  sym_offsets should be doing this sort of thing,
     because it knows the mapping between bfd sections and
     section_offsets.  */
  /* This is a hack.  As far as I can tell, section offsets are not
     target dependent.  They are all set to addr with a couple of
     exceptions.  The exceptions are sysvr4 shared libraries, whose
     offsets are kept in solib structures anyway and rs6000 xcoff
     which handles shared libraries in a completely unique way.

     Section offsets are built similarly, except that they are built
     by adding addr in all cases because there is no clear mapping
     from section_offsets into actual sections.  Note that solib.c
     has a different algorithm for finding section offsets.

     These should probably all be collapsed into some target
     independent form of shared library support.  FIXME.  */

  if (addrs)
    {
      struct obj_section *s;

 	/* Map section offsets in "addr" back to the object's 
 	   sections by comparing the section names with bfd's 
 	   section names.  Then adjust the section address by
 	   the offset. */ /* for gdb/13815 */
 
      ALL_OBJFILE_OSECTIONS (objfile, s)
	{
	  CORE_ADDR s_addr = 0;
	  int i;

 	    for (i = 0; 
	         !s_addr && i < addrs->num_sections && addrs->other[i].name;
		 i++)
 	      if (strcmp (bfd_section_name (s->objfile->obfd, 
					    s->the_bfd_section), 
			  addrs->other[i].name) == 0)
 	        s_addr = addrs->other[i].addr; /* end added for gdb/13815 */
 
	  s->addr -= s->offset;
	  s->addr += s_addr;
	  s->endaddr -= s->offset;
	  s->endaddr += s_addr;
	  s->offset += s_addr;
	}
    }
#endif /* not DEPRECATED_IBM6000_TARGET */

  (*objfile->sf->sym_read) (objfile, mainline);

  /* Don't allow char * to have a typename (else would get caddr_t).
     Ditto void *.  FIXME: Check whether this is now done by all the
     symbol readers themselves (many of them now do), and if so remove
     it from here.  */

  TYPE_NAME (lookup_pointer_type (builtin_type_char)) = 0;
  TYPE_NAME (lookup_pointer_type (builtin_type_void)) = 0;

  /* Mark the objfile has having had initial symbol read attempted.  Note
     that this does not mean we found any symbols... */

  objfile->flags |= OBJF_SYMS;

  /* Discard cleanups as symbol reading was successful.  */

  discard_cleanups (old_chain);
}

/* Perform required actions after either reading in the initial
   symbols for a new objfile, or mapping in the symbols from a reusable
   objfile. */

void
new_symfile_objfile (struct objfile *objfile, int mainline, int verbo)
{

  /* If this is the main symbol file we have to clean up all users of the
     old main symbol file. Otherwise it is sufficient to fixup all the
     breakpoints that may have been redefined by this symbol file.  */
  if (mainline)
    {
      /* OK, make it the "real" symbol file.  */
      symfile_objfile = objfile;

      clear_symtab_users ();
    }
  else
    {
      breakpoint_re_set ();
    }

  /* We're done reading the symbol file; finish off complaints.  */
  clear_complaints (&symfile_complaints, 0, verbo);
}

/* Process a symbol file, as either the main file or as a dynamically
   loaded file.

   NAME is the file name (which will be tilde-expanded and made
   absolute herein) (but we don't free or modify NAME itself).

   FROM_TTY says how verbose to be.

   MAINLINE specifies whether this is the main symbol file, or whether
   it's an extra symbol file such as dynamically loaded code.

   ADDRS, OFFSETS, and NUM_OFFSETS are as described for
   syms_from_objfile, above.  ADDRS is ignored when MAINLINE is
   non-zero.

   Upon success, returns a pointer to the objfile that was added.
   Upon failure, jumps back to command level (never returns). */
static struct objfile *
symbol_file_add_with_addrs_or_offsets (char *name, int from_tty,
                                       struct section_addr_info *addrs,
                                       struct section_offsets *offsets,
                                       int num_offsets,
                                       int mainline, int flags)
{
  struct objfile *objfile;
  struct partial_symtab *psymtab;
  char *debugfile;
  bfd *abfd;
  struct section_addr_info *orig_addrs;
  struct cleanup *my_cleanups;

  /* Open a bfd for the file, and give user a chance to burp if we'd be
     interactively wiping out any existing symbols.  */

  abfd = symfile_bfd_open (name);

  if ((have_full_symbols () || have_partial_symbols ())
      && mainline
      && from_tty
      && !query ("Load new symbol table from \"%s\"? ", name))
    error ("Not confirmed.");

  objfile = allocate_objfile (abfd, flags);

  orig_addrs = alloc_section_addr_info (bfd_count_sections (abfd));
  my_cleanups = make_cleanup (xfree, orig_addrs);
  if (addrs)
    {
      int i;
      orig_addrs->num_sections = addrs->num_sections;
      for (i = 0; i < addrs->num_sections; i++)
	orig_addrs->other[i] = addrs->other[i];
    }

  /* We either created a new mapped symbol table, mapped an existing
     symbol table file which has not had initial symbol reading
     performed, or need to read an unmapped symbol table. */
  if (from_tty || info_verbose)
    {
      if (pre_add_symbol_hook)
	pre_add_symbol_hook (name);
      else
	{
	  printf_unfiltered ("Reading symbols from %s...", name);
	  wrap_here ("");
	  gdb_flush (gdb_stdout);
	}
    }
  syms_from_objfile (objfile, addrs, offsets, num_offsets,
		     mainline, from_tty);

  /* We now have at least a partial symbol table.  Check to see if the
     user requested that all symbols be read on initial access via either
     the gdb startup command line or on a per symbol file basis.  Expand
     all partial symbol tables for this objfile if so. */

  if ((flags & OBJF_READNOW) || readnow_symbol_files)
    {
      if (from_tty || info_verbose)
	{
	  printf_unfiltered ("expanding to full symbols...");
	  wrap_here ("");
	  gdb_flush (gdb_stdout);
	}

      for (psymtab = objfile->psymtabs;
	   psymtab != NULL;
	   psymtab = psymtab->next)
	{
	  psymtab_to_symtab (psymtab);
	}
    }

  debugfile = find_separate_debug_file (objfile);
  if (debugfile)
    {
      if (addrs != NULL)
	{
	  objfile->separate_debug_objfile
            = symbol_file_add (debugfile, from_tty, orig_addrs, 0, flags);
	}
      else
	{
	  objfile->separate_debug_objfile
            = symbol_file_add (debugfile, from_tty, NULL, 0, flags);
	}
      objfile->separate_debug_objfile->separate_debug_objfile_backlink
        = objfile;
      
      /* Put the separate debug object before the normal one, this is so that
         usage of the ALL_OBJFILES_SAFE macro will stay safe. */
      put_objfile_before (objfile->separate_debug_objfile, objfile);
      
      xfree (debugfile);
    }
  
  if (!have_partial_symbols () && !have_full_symbols ())
    {
      wrap_here ("");
      printf_unfiltered ("(no debugging symbols found)...");
      wrap_here ("");
    }

  if (from_tty || info_verbose)
    {
      if (post_add_symbol_hook)
	post_add_symbol_hook ();
      else
	{
	  printf_unfiltered ("done.\n");
	}
    }

  /* We print some messages regardless of whether 'from_tty ||
     info_verbose' is true, so make sure they go out at the right
     time.  */
  gdb_flush (gdb_stdout);

  do_cleanups (my_cleanups);

  if (objfile->sf == NULL)
    return objfile;	/* No symbols. */

  new_symfile_objfile (objfile, mainline, from_tty);

  if (target_new_objfile_hook)
    target_new_objfile_hook (objfile);

  return (objfile);
}


/* Process a symbol file, as either the main file or as a dynamically
   loaded file.  See symbol_file_add_with_addrs_or_offsets's comments
   for details.  */
struct objfile *
symbol_file_add (char *name, int from_tty, struct section_addr_info *addrs,
		 int mainline, int flags)
{
  return symbol_file_add_with_addrs_or_offsets (name, from_tty, addrs, 0, 0, 
                                                mainline, flags);
}


/* Call symbol_file_add() with default values and update whatever is
   affected by the loading of a new main().
   Used when the file is supplied in the gdb command line
   and by some targets with special loading requirements.
   The auxiliary function, symbol_file_add_main_1(), has the flags
   argument for the switches that can only be specified in the symbol_file
   command itself.  */
   
void
symbol_file_add_main (char *args, int from_tty)
{
  symbol_file_add_main_1 (args, from_tty, 0);
}

static void
symbol_file_add_main_1 (char *args, int from_tty, int flags)
{
  symbol_file_add (args, from_tty, NULL, 1, flags);

#ifdef HPUXHPPA
  RESET_HP_UX_GLOBALS ();
#endif

  /* Getting new symbols may change our opinion about
     what is frameless.  */
  reinit_frame_cache ();

  set_initial_language ();
}

void
symbol_file_clear (int from_tty)
{
  if ((have_full_symbols () || have_partial_symbols ())
      && from_tty
      && !query ("Discard symbol table from `%s'? ",
		 symfile_objfile->name))
    error ("Not confirmed.");
    free_all_objfiles ();

    /* solib descriptors may have handles to objfiles.  Since their
       storage has just been released, we'd better wipe the solib
       descriptors as well.
     */
#if defined(SOLIB_RESTART)
    SOLIB_RESTART ();
#endif

    symfile_objfile = NULL;
    if (from_tty)
      printf_unfiltered ("No symbol file now.\n");
#ifdef HPUXHPPA
    RESET_HP_UX_GLOBALS ();
#endif
}

static char *
get_debug_link_info (struct objfile *objfile, unsigned long *crc32_out)
{
  asection *sect;
  bfd_size_type debuglink_size;
  unsigned long crc32;
  char *contents;
  int crc_offset;
  unsigned char *p;
  
  sect = bfd_get_section_by_name (objfile->obfd, ".gnu_debuglink");

  if (sect == NULL)
    return NULL;

  debuglink_size = bfd_section_size (objfile->obfd, sect);
  
  contents = xmalloc (debuglink_size);
  bfd_get_section_contents (objfile->obfd, sect, contents,
			    (file_ptr)0, (bfd_size_type)debuglink_size);

  /* Crc value is stored after the filename, aligned up to 4 bytes. */
  crc_offset = strlen (contents) + 1;
  crc_offset = (crc_offset + 3) & ~3;

  crc32 = bfd_get_32 (objfile->obfd, (bfd_byte *) (contents + crc_offset));
  
  *crc32_out = crc32;
  return contents;
}

static int
separate_debug_file_exists (const char *name, unsigned long crc)
{
  unsigned long file_crc = 0;
  int fd;
  char buffer[8*1024];
  int count;

  fd = open (name, O_RDONLY | O_BINARY);
  if (fd < 0)
    return 0;

  while ((count = read (fd, buffer, sizeof (buffer))) > 0)
    file_crc = gnu_debuglink_crc32 (file_crc, buffer, count);

  close (fd);

  return crc == file_crc;
}

static char *debug_file_directory = NULL;

#if ! defined (DEBUG_SUBDIRECTORY)
#define DEBUG_SUBDIRECTORY ".debug"
#endif

static char *
find_separate_debug_file (struct objfile *objfile)
{
  asection *sect;
  char *basename;
  char *dir;
  char *debugfile;
  char *name_copy;
  bfd_size_type debuglink_size;
  unsigned long crc32;
  int i;

  basename = get_debug_link_info (objfile, &crc32);

  if (basename == NULL)
    return NULL;
  
  dir = xstrdup (objfile->name);

  /* Strip off the final filename part, leaving the directory name,
     followed by a slash.  Objfile names should always be absolute and
     tilde-expanded, so there should always be a slash in there
     somewhere.  */
  for (i = strlen(dir) - 1; i >= 0; i--)
    {
      if (IS_DIR_SEPARATOR (dir[i]))
	break;
    }
  gdb_assert (i >= 0 && IS_DIR_SEPARATOR (dir[i]));
  dir[i+1] = '\0';
  
  debugfile = alloca (strlen (debug_file_directory) + 1
                      + strlen (dir)
                      + strlen (DEBUG_SUBDIRECTORY)
                      + strlen ("/")
                      + strlen (basename) 
                      + 1);

  /* First try in the same directory as the original file.  */
  strcpy (debugfile, dir);
  strcat (debugfile, basename);

  if (separate_debug_file_exists (debugfile, crc32))
    {
      xfree (basename);
      xfree (dir);
      return xstrdup (debugfile);
    }
  
  /* Then try in the subdirectory named DEBUG_SUBDIRECTORY.  */
  strcpy (debugfile, dir);
  strcat (debugfile, DEBUG_SUBDIRECTORY);
  strcat (debugfile, "/");
  strcat (debugfile, basename);

  if (separate_debug_file_exists (debugfile, crc32))
    {
      xfree (basename);
      xfree (dir);
      return xstrdup (debugfile);
    }
  
  /* Then try in the global debugfile directory.  */
  strcpy (debugfile, debug_file_directory);
  strcat (debugfile, "/");
  strcat (debugfile, dir);
  strcat (debugfile, basename);

  if (separate_debug_file_exists (debugfile, crc32))
    {
      xfree (basename);
      xfree (dir);
      return xstrdup (debugfile);
    }
  
  xfree (basename);
  xfree (dir);
  return NULL;
}


/* This is the symbol-file command.  Read the file, analyze its
   symbols, and add a struct symtab to a symtab list.  The syntax of
   the command is rather bizarre--(1) buildargv implements various
   quoting conventions which are undocumented and have little or
   nothing in common with the way things are quoted (or not quoted)
   elsewhere in GDB, (2) options are used, which are not generally
   used in GDB (perhaps "set mapped on", "set readnow on" would be
   better), (3) the order of options matters, which is contrary to GNU
   conventions (because it is confusing and inconvenient).  */
/* Note: ezannoni 2000-04-17. This function used to have support for
   rombug (see remote-os9k.c). It consisted of a call to target_link()
   (target.c) to get the address of the text segment from the target,
   and pass that to symbol_file_add(). This is no longer supported. */

void
symbol_file_command (char *args, int from_tty)
{
  char **argv;
  char *name = NULL;
  struct cleanup *cleanups;
  int flags = OBJF_USERLOADED;

  dont_repeat ();

  if (args == NULL)
    {
      symbol_file_clear (from_tty);
    }
  else
    {
      if ((argv = buildargv (args)) == NULL)
	{
	  nomem (0);
	}
      cleanups = make_cleanup_freeargv (argv);
      while (*argv != NULL)
	{
	  if (strcmp (*argv, "-readnow") == 0)
	    flags |= OBJF_READNOW;
	  else if (**argv == '-')
	    error ("unknown option `%s'", *argv);
	  else
	    {
	      name = *argv;
	      
	      symbol_file_add_main_1 (name, from_tty, flags);
	    }
	  argv++;
	}

      if (name == NULL)
	{
	  error ("no symbol file name was specified");
	}
      do_cleanups (cleanups);
    }
}

/* Set the initial language.

   A better solution would be to record the language in the psymtab when reading
   partial symbols, and then use it (if known) to set the language.  This would
   be a win for formats that encode the language in an easily discoverable place,
   such as DWARF.  For stabs, we can jump through hoops looking for specially
   named symbols or try to intuit the language from the specific type of stabs
   we find, but we can't do that until later when we read in full symbols.
   FIXME.  */

static void
set_initial_language (void)
{
  struct partial_symtab *pst;
  enum language lang = language_unknown;

  pst = find_main_psymtab ();
  if (pst != NULL)
    {
      if (pst->filename != NULL)
	{
	  lang = deduce_language_from_filename (pst->filename);
	}
      if (lang == language_unknown)
	{
	  /* Make C the default language */
	  lang = language_c;
	}
      set_language (lang);
      expected_language = current_language;	/* Don't warn the user */
    }
}

/* Open file specified by NAME and hand it off to BFD for preliminary
   analysis.  Result is a newly initialized bfd *, which includes a newly
   malloc'd` copy of NAME (tilde-expanded and made absolute).
   In case of trouble, error() is called.  */

bfd *
symfile_bfd_open (char *name)
{
  bfd *sym_bfd;
  int desc;
  char *absolute_name;



  name = tilde_expand (name);	/* Returns 1st new malloc'd copy */

  /* Look down path for it, allocate 2nd new malloc'd copy.  */
  desc = openp (getenv ("PATH"), 1, name, O_RDONLY | O_BINARY, 0, &absolute_name);
#if defined(__GO32__) || defined(_WIN32) || defined (__CYGWIN__)
  if (desc < 0)
    {
      char *exename = alloca (strlen (name) + 5);
      strcat (strcpy (exename, name), ".exe");
      desc = openp (getenv ("PATH"), 1, exename, O_RDONLY | O_BINARY,
		    0, &absolute_name);
    }
#endif
  if (desc < 0)
    {
      make_cleanup (xfree, name);
      perror_with_name (name);
    }
  xfree (name);			/* Free 1st new malloc'd copy */
  name = absolute_name;		/* Keep 2nd malloc'd copy in bfd */
  /* It'll be freed in free_objfile(). */

  sym_bfd = bfd_fdopenr (name, gnutarget, desc);
  if (!sym_bfd)
    {
      close (desc);
      make_cleanup (xfree, name);
      error ("\"%s\": can't open to read symbols: %s.", name,
	     bfd_errmsg (bfd_get_error ()));
    }
  bfd_set_cacheable (sym_bfd, 1);

  if (!bfd_check_format (sym_bfd, bfd_object))
    {
      /* FIXME: should be checking for errors from bfd_close (for one thing,
         on error it does not free all the storage associated with the
         bfd).  */
      bfd_close (sym_bfd);	/* This also closes desc */
      make_cleanup (xfree, name);
      error ("\"%s\": can't read symbols: %s.", name,
	     bfd_errmsg (bfd_get_error ()));
    }
  return (sym_bfd);
}

/* Return the section index for the given section name. Return -1 if
   the section was not found. */
int
get_section_index (struct objfile *objfile, char *section_name)
{
  asection *sect = bfd_get_section_by_name (objfile->obfd, section_name);
  if (sect)
    return sect->index;
  else
    return -1;
}

/* Link a new symtab_fns into the global symtab_fns list.  Called on gdb
   startup by the _initialize routine in each object file format reader,
   to register information about each format the the reader is prepared
   to handle. */

void
add_symtab_fns (struct sym_fns *sf)
{
  sf->next = symtab_fns;
  symtab_fns = sf;
}


/* Initialize to read symbols from the symbol file sym_bfd.  It either
   returns or calls error().  The result is an initialized struct sym_fns
   in the objfile structure, that contains cached information about the
   symbol file.  */

static void
find_sym_fns (struct objfile *objfile)
{
  struct sym_fns *sf;
  enum bfd_flavour our_flavour = bfd_get_flavour (objfile->obfd);
  char *our_target = bfd_get_target (objfile->obfd);

  if (our_flavour == bfd_target_srec_flavour
      || our_flavour == bfd_target_ihex_flavour
      || our_flavour == bfd_target_tekhex_flavour)
    return;	/* No symbols. */

  for (sf = symtab_fns; sf != NULL; sf = sf->next)
    {
      if (our_flavour == sf->sym_flavour)
	{
	  objfile->sf = sf;
	  return;
	}
    }
  error ("I'm sorry, Dave, I can't do that.  Symbol format `%s' unknown.",
	 bfd_get_target (objfile->obfd));
}

/* This function runs the load command of our current target.  */

static void
load_command (char *arg, int from_tty)
{
  if (arg == NULL)
    arg = get_exec_file (1);
  target_load (arg, from_tty);

  /* After re-loading the executable, we don't really know which
     overlays are mapped any more.  */
  overlay_cache_invalid = 1;
}

/* This version of "load" should be usable for any target.  Currently
   it is just used for remote targets, not inftarg.c or core files,
   on the theory that only in that case is it useful.

   Avoiding xmodem and the like seems like a win (a) because we don't have
   to worry about finding it, and (b) On VMS, fork() is very slow and so
   we don't want to run a subprocess.  On the other hand, I'm not sure how
   performance compares.  */

static int download_write_size = 512;
static int validate_download = 0;

/* Callback service function for generic_load (bfd_map_over_sections).  */

static void
add_section_size_callback (bfd *abfd, asection *asec, void *data)
{
  bfd_size_type *sum = data;

  *sum += bfd_get_section_size (asec);
}

/* Opaque data for load_section_callback.  */
struct load_section_data {
  unsigned long load_offset;
  unsigned long write_count;
  unsigned long data_count;
  bfd_size_type total_size;
};

/* Callback service function for generic_load (bfd_map_over_sections).  */

static void
load_section_callback (bfd *abfd, asection *asec, void *data)
{
  struct load_section_data *args = data;

  if (bfd_get_section_flags (abfd, asec) & SEC_LOAD)
    {
      bfd_size_type size = bfd_get_section_size (asec);
      if (size > 0)
	{
	  char *buffer;
	  struct cleanup *old_chain;
	  CORE_ADDR lma = bfd_section_lma (abfd, asec) + args->load_offset;
	  bfd_size_type block_size;
	  int err;
	  const char *sect_name = bfd_get_section_name (abfd, asec);
	  bfd_size_type sent;

	  if (download_write_size > 0 && size > download_write_size)
	    block_size = download_write_size;
	  else
	    block_size = size;

	  buffer = xmalloc (size);
	  old_chain = make_cleanup (xfree, buffer);

	  /* Is this really necessary?  I guess it gives the user something
	     to look at during a long download.  */
	  ui_out_message (uiout, 0, "Loading section %s, size 0x%s lma 0x%s\n",
			  sect_name, paddr_nz (size), paddr_nz (lma));

	  bfd_get_section_contents (abfd, asec, buffer, 0, size);

	  sent = 0;
	  do
	    {
	      int len;
	      bfd_size_type this_transfer = size - sent;

	      if (this_transfer >= block_size)
		this_transfer = block_size;
	      len = target_write_memory_partial (lma, buffer,
						 this_transfer, &err);
	      if (err)
		break;
	      if (validate_download)
		{
		  /* Broken memories and broken monitors manifest
		     themselves here when bring new computers to
		     life.  This doubles already slow downloads.  */
		  /* NOTE: cagney/1999-10-18: A more efficient
		     implementation might add a verify_memory()
		     method to the target vector and then use
		     that.  remote.c could implement that method
		     using the ``qCRC'' packet.  */
		  char *check = xmalloc (len);
		  struct cleanup *verify_cleanups = 
		    make_cleanup (xfree, check);

		  if (target_read_memory (lma, check, len) != 0)
		    error ("Download verify read failed at 0x%s",
			   paddr (lma));
		  if (memcmp (buffer, check, len) != 0)
		    error ("Download verify compare failed at 0x%s",
			   paddr (lma));
		  do_cleanups (verify_cleanups);
		}
	      args->data_count += len;
	      lma += len;
	      buffer += len;
	      args->write_count += 1;
	      sent += len;
	      if (quit_flag
		  || (ui_load_progress_hook != NULL
		      && ui_load_progress_hook (sect_name, sent)))
		error ("Canceled the download");

	      if (show_load_progress != NULL)
		show_load_progress (sect_name, sent, size, 
				    args->data_count, args->total_size);
	    }
	  while (sent < size);

	  if (err != 0)
	    error ("Memory access error while loading section %s.", sect_name);

	  do_cleanups (old_chain);
	}
    }
}

void
generic_load (char *args, int from_tty)
{
  asection *s;
  bfd *loadfile_bfd;
  time_t start_time, end_time;	/* Start and end times of download */
  char *filename;
  struct cleanup *old_cleanups;
  char *offptr;
  struct load_section_data cbdata;
  CORE_ADDR entry;

  cbdata.load_offset = 0;	/* Offset to add to vma for each section. */
  cbdata.write_count = 0;	/* Number of writes needed. */
  cbdata.data_count = 0;	/* Number of bytes written to target memory. */
  cbdata.total_size = 0;	/* Total size of all bfd sectors. */

  /* Parse the input argument - the user can specify a load offset as
     a second argument. */
  filename = xmalloc (strlen (args) + 1);
  old_cleanups = make_cleanup (xfree, filename);
  strcpy (filename, args);
  offptr = strchr (filename, ' ');
  if (offptr != NULL)
    {
      char *endptr;

      cbdata.load_offset = strtoul (offptr, &endptr, 0);
      if (offptr == endptr)
	error ("Invalid download offset:%s\n", offptr);
      *offptr = '\0';
    }
  else
    cbdata.load_offset = 0;

  /* Open the file for loading. */
  loadfile_bfd = bfd_openr (filename, gnutarget);
  if (loadfile_bfd == NULL)
    {
      perror_with_name (filename);
      return;
    }

  /* FIXME: should be checking for errors from bfd_close (for one thing,
     on error it does not free all the storage associated with the
     bfd).  */
  make_cleanup_bfd_close (loadfile_bfd);

  if (!bfd_check_format (loadfile_bfd, bfd_object))
    {
      error ("\"%s\" is not an object file: %s", filename,
	     bfd_errmsg (bfd_get_error ()));
    }

  bfd_map_over_sections (loadfile_bfd, add_section_size_callback, 
			 (void *) &cbdata.total_size);

  start_time = time (NULL);

  bfd_map_over_sections (loadfile_bfd, load_section_callback, &cbdata);

  end_time = time (NULL);

  entry = bfd_get_start_address (loadfile_bfd);
  ui_out_text (uiout, "Start address ");
  ui_out_field_fmt (uiout, "address", "0x%s", paddr_nz (entry));
  ui_out_text (uiout, ", load size ");
  ui_out_field_fmt (uiout, "load-size", "%lu", cbdata.data_count);
  ui_out_text (uiout, "\n");
  /* We were doing this in remote-mips.c, I suspect it is right
     for other targets too.  */
  write_pc (entry);

  /* FIXME: are we supposed to call symbol_file_add or not?  According
     to a comment from remote-mips.c (where a call to symbol_file_add
     was commented out), making the call confuses GDB if more than one
     file is loaded in.  Some targets do (e.g., remote-vx.c) but
     others don't (or didn't - perhaphs they have all been deleted).  */

  print_transfer_performance (gdb_stdout, cbdata.data_count, 
			      cbdata.write_count, end_time - start_time);

  do_cleanups (old_cleanups);
}

/* Report how fast the transfer went. */

/* DEPRECATED: cagney/1999-10-18: report_transfer_performance is being
   replaced by print_transfer_performance (with a very different
   function signature). */

void
report_transfer_performance (unsigned long data_count, time_t start_time,
			     time_t end_time)
{
  print_transfer_performance (gdb_stdout, data_count, 
			      end_time - start_time, 0);
}

void
print_transfer_performance (struct ui_file *stream,
			    unsigned long data_count,
			    unsigned long write_count,
			    unsigned long time_count)
{
  ui_out_text (uiout, "Transfer rate: ");
  if (time_count > 0)
    {
      ui_out_field_fmt (uiout, "transfer-rate", "%lu", 
			(data_count * 8) / time_count);
      ui_out_text (uiout, " bits/sec");
    }
  else
    {
      ui_out_field_fmt (uiout, "transferred-bits", "%lu", (data_count * 8));
      ui_out_text (uiout, " bits in <1 sec");    
    }
  if (write_count > 0)
    {
      ui_out_text (uiout, ", ");
      ui_out_field_fmt (uiout, "write-rate", "%lu", data_count / write_count);
      ui_out_text (uiout, " bytes/write");
    }
  ui_out_text (uiout, ".\n");
}

/* This function allows the addition of incrementally linked object files.
   It does not modify any state in the target, only in the debugger.  */
/* Note: ezannoni 2000-04-13 This function/command used to have a
   special case syntax for the rombug target (Rombug is the boot
   monitor for Microware's OS-9 / OS-9000, see remote-os9k.c). In the
   rombug case, the user doesn't need to supply a text address,
   instead a call to target_link() (in target.c) would supply the
   value to use. We are now discontinuing this type of ad hoc syntax. */

static void
add_symbol_file_command (char *args, int from_tty)
{
  char *filename = NULL;
  int flags = OBJF_USERLOADED;
  char *arg;
  int expecting_option = 0;
  int section_index = 0;
  int argcnt = 0;
  int sec_num = 0;
  int i;
  int expecting_sec_name = 0;
  int expecting_sec_addr = 0;

  struct sect_opt
  {
    char *name;
    char *value;
  };

  struct section_addr_info *section_addrs;
  struct sect_opt *sect_opts = NULL;
  size_t num_sect_opts = 0;
  struct cleanup *my_cleanups = make_cleanup (null_cleanup, NULL);

  num_sect_opts = 16;
  sect_opts = (struct sect_opt *) xmalloc (num_sect_opts 
					   * sizeof (struct sect_opt));

  dont_repeat ();

  if (args == NULL)
    error ("add-symbol-file takes a file name and an address");

  /* Make a copy of the string that we can safely write into. */
  args = xstrdup (args);

  while (*args != '\000')
    {
      /* Any leading spaces? */
      while (isspace (*args))
	args++;

      /* Point arg to the beginning of the argument. */
      arg = args;

      /* Move args pointer over the argument. */
      while ((*args != '\000') && !isspace (*args))
	args++;

      /* If there are more arguments, terminate arg and
         proceed past it. */
      if (*args != '\000')
	*args++ = '\000';

      /* Now process the argument. */
      if (argcnt == 0)
	{
	  /* The first argument is the file name. */
	  filename = tilde_expand (arg);
	  make_cleanup (xfree, filename);
	}
      else
	if (argcnt == 1)
	  {
	    /* The second argument is always the text address at which
               to load the program. */
	    sect_opts[section_index].name = ".text";
	    sect_opts[section_index].value = arg;
	    if (++section_index > num_sect_opts) 
	      {
		num_sect_opts *= 2;
		sect_opts = ((struct sect_opt *) 
			     xrealloc (sect_opts,
				       num_sect_opts 
				       * sizeof (struct sect_opt)));
	      }
	  }
	else
	  {
	    /* It's an option (starting with '-') or it's an argument
	       to an option */

	    if (*arg == '-')
	      {
		if (strcmp (arg, "-readnow") == 0)
		  flags |= OBJF_READNOW;
		else if (strcmp (arg, "-s") == 0)
		  {
		    expecting_sec_name = 1;
		    expecting_sec_addr = 1;
		  }
	      }
	    else
	      {
		if (expecting_sec_name)
		  {
		    sect_opts[section_index].name = arg;
		    expecting_sec_name = 0;
		  }
		else
		  if (expecting_sec_addr)
		    {
		      sect_opts[section_index].value = arg;
		      expecting_sec_addr = 0;
		      if (++section_index > num_sect_opts) 
			{
			  num_sect_opts *= 2;
			  sect_opts = ((struct sect_opt *) 
				       xrealloc (sect_opts,
						 num_sect_opts 
						 * sizeof (struct sect_opt)));
			}
		    }
		  else
		    error ("USAGE: add-symbol-file <filename> <textaddress> [-mapped] [-readnow] [-s <secname> <addr>]*");
	      }
	  }
      argcnt++;
    }

  /* Print the prompt for the query below. And save the arguments into
     a sect_addr_info structure to be passed around to other
     functions.  We have to split this up into separate print
     statements because local_hex_string returns a local static
     string. */
 
  printf_unfiltered ("add symbol table from file \"%s\" at\n", filename);
  section_addrs = alloc_section_addr_info (section_index);
  make_cleanup (xfree, section_addrs);
  for (i = 0; i < section_index; i++)
    {
      CORE_ADDR addr;
      char *val = sect_opts[i].value;
      char *sec = sect_opts[i].name;
 
      addr = parse_and_eval_address (val);

      /* Here we store the section offsets in the order they were
         entered on the command line. */
      section_addrs->other[sec_num].name = sec;
      section_addrs->other[sec_num].addr = addr;
      printf_unfiltered ("\t%s_addr = %s\n",
		       sec, 
		       local_hex_string ((unsigned long)addr));
      sec_num++;

      /* The object's sections are initialized when a 
	 call is made to build_objfile_section_table (objfile).
	 This happens in reread_symbols. 
	 At this point, we don't know what file type this is,
	 so we can't determine what section names are valid.  */
    }

  if (from_tty && (!query ("%s", "")))
    error ("Not confirmed.");

  symbol_file_add (filename, from_tty, section_addrs, 0, flags);

  /* Getting new symbols may change our opinion about what is
     frameless.  */
  reinit_frame_cache ();
  do_cleanups (my_cleanups);
}

static void
add_shared_symbol_files_command (char *args, int from_tty)
{
#ifdef ADD_SHARED_SYMBOL_FILES
  ADD_SHARED_SYMBOL_FILES (args, from_tty);
#else
  error ("This command is not available in this configuration of GDB.");
#endif
}

/* Re-read symbols if a symbol-file has changed.  */
void
reread_symbols (void)
{
  struct objfile *objfile;
  long new_modtime;
  int reread_one = 0;
  struct stat new_statbuf;
  int res;

  /* With the addition of shared libraries, this should be modified,
     the load time should be saved in the partial symbol tables, since
     different tables may come from different source files.  FIXME.
     This routine should then walk down each partial symbol table
     and see if the symbol table that it originates from has been changed */

  for (objfile = object_files; objfile; objfile = objfile->next)
    {
      if (objfile->obfd)
	{
#ifdef DEPRECATED_IBM6000_TARGET
	  /* If this object is from a shared library, then you should
	     stat on the library name, not member name. */

	  if (objfile->obfd->my_archive)
	    res = stat (objfile->obfd->my_archive->filename, &new_statbuf);
	  else
#endif
	    res = stat (objfile->name, &new_statbuf);
	  if (res != 0)
	    {
	      /* FIXME, should use print_sys_errmsg but it's not filtered. */
	      printf_unfiltered ("`%s' has disappeared; keeping its symbols.\n",
			       objfile->name);
	      continue;
	    }
	  new_modtime = new_statbuf.st_mtime;
	  if (new_modtime != objfile->mtime)
	    {
	      struct cleanup *old_cleanups;
	      struct section_offsets *offsets;
	      int num_offsets;
	      char *obfd_filename;

	      printf_unfiltered ("`%s' has changed; re-reading symbols.\n",
			       objfile->name);

	      /* There are various functions like symbol_file_add,
	         symfile_bfd_open, syms_from_objfile, etc., which might
	         appear to do what we want.  But they have various other
	         effects which we *don't* want.  So we just do stuff
	         ourselves.  We don't worry about mapped files (for one thing,
	         any mapped file will be out of date).  */

	      /* If we get an error, blow away this objfile (not sure if
	         that is the correct response for things like shared
	         libraries).  */
	      old_cleanups = make_cleanup_free_objfile (objfile);
	      /* We need to do this whenever any symbols go away.  */
	      make_cleanup (clear_symtab_users_cleanup, 0 /*ignore*/);

	      /* Clean up any state BFD has sitting around.  We don't need
	         to close the descriptor but BFD lacks a way of closing the
	         BFD without closing the descriptor.  */
	      obfd_filename = bfd_get_filename (objfile->obfd);
	      if (!bfd_close (objfile->obfd))
		error ("Can't close BFD for %s: %s", objfile->name,
		       bfd_errmsg (bfd_get_error ()));
	      objfile->obfd = bfd_openr (obfd_filename, gnutarget);
	      if (objfile->obfd == NULL)
		error ("Can't open %s to read symbols.", objfile->name);
	      /* bfd_openr sets cacheable to true, which is what we want.  */
	      if (!bfd_check_format (objfile->obfd, bfd_object))
		error ("Can't read symbols from %s: %s.", objfile->name,
		       bfd_errmsg (bfd_get_error ()));

	      /* Save the offsets, we will nuke them with the rest of the
	         objfile_obstack.  */
	      num_offsets = objfile->num_sections;
	      offsets = ((struct section_offsets *) 
			 alloca (SIZEOF_N_SECTION_OFFSETS (num_offsets)));
	      memcpy (offsets, objfile->section_offsets, 
		      SIZEOF_N_SECTION_OFFSETS (num_offsets));

	      /* Nuke all the state that we will re-read.  Much of the following
	         code which sets things to NULL really is necessary to tell
	         other parts of GDB that there is nothing currently there.  */

	      /* FIXME: Do we have to free a whole linked list, or is this
	         enough?  */
	      if (objfile->global_psymbols.list)
		xmfree (objfile->md, objfile->global_psymbols.list);
	      memset (&objfile->global_psymbols, 0,
		      sizeof (objfile->global_psymbols));
	      if (objfile->static_psymbols.list)
		xmfree (objfile->md, objfile->static_psymbols.list);
	      memset (&objfile->static_psymbols, 0,
		      sizeof (objfile->static_psymbols));

	      /* Free the obstacks for non-reusable objfiles */
	      bcache_xfree (objfile->psymbol_cache);
	      objfile->psymbol_cache = bcache_xmalloc ();
	      bcache_xfree (objfile->macro_cache);
	      objfile->macro_cache = bcache_xmalloc ();
	      if (objfile->demangled_names_hash != NULL)
		{
		  htab_delete (objfile->demangled_names_hash);
		  objfile->demangled_names_hash = NULL;
		}
	      obstack_free (&objfile->objfile_obstack, 0);
	      objfile->sections = NULL;
	      objfile->symtabs = NULL;
	      objfile->psymtabs = NULL;
	      objfile->free_psymtabs = NULL;
	      objfile->cp_namespace_symtab = NULL;
	      objfile->msymbols = NULL;
	      objfile->sym_private = NULL;
	      objfile->minimal_symbol_count = 0;
	      memset (&objfile->msymbol_hash, 0,
		      sizeof (objfile->msymbol_hash));
	      memset (&objfile->msymbol_demangled_hash, 0,
		      sizeof (objfile->msymbol_demangled_hash));
	      objfile->fundamental_types = NULL;
	      clear_objfile_data (objfile);
	      if (objfile->sf != NULL)
		{
		  (*objfile->sf->sym_finish) (objfile);
		}

	      /* We never make this a mapped file.  */
	      objfile->md = NULL;
	      objfile->psymbol_cache = bcache_xmalloc ();
	      objfile->macro_cache = bcache_xmalloc ();
	      /* obstack_init also initializes the obstack so it is
	         empty.  We could use obstack_specify_allocation but
	         gdb_obstack.h specifies the alloc/dealloc
	         functions.  */
	      obstack_init (&objfile->objfile_obstack);
	      if (build_objfile_section_table (objfile))
		{
		  error ("Can't find the file sections in `%s': %s",
			 objfile->name, bfd_errmsg (bfd_get_error ()));
		}
              terminate_minimal_symbol_table (objfile);

	      /* We use the same section offsets as from last time.  I'm not
	         sure whether that is always correct for shared libraries.  */
	      objfile->section_offsets = (struct section_offsets *)
		obstack_alloc (&objfile->objfile_obstack, 
			       SIZEOF_N_SECTION_OFFSETS (num_offsets));
	      memcpy (objfile->section_offsets, offsets, 
		      SIZEOF_N_SECTION_OFFSETS (num_offsets));
	      objfile->num_sections = num_offsets;

	      /* What the hell is sym_new_init for, anyway?  The concept of
	         distinguishing between the main file and additional files
	         in this way seems rather dubious.  */
	      if (objfile == symfile_objfile)
		{
		  (*objfile->sf->sym_new_init) (objfile);
#ifdef HPUXHPPA
		  RESET_HP_UX_GLOBALS ();
#endif
		}

	      (*objfile->sf->sym_init) (objfile);
	      clear_complaints (&symfile_complaints, 1, 1);
	      /* The "mainline" parameter is a hideous hack; I think leaving it
	         zero is OK since dbxread.c also does what it needs to do if
	         objfile->global_psymbols.size is 0.  */
	      (*objfile->sf->sym_read) (objfile, 0);
	      if (!have_partial_symbols () && !have_full_symbols ())
		{
		  wrap_here ("");
		  printf_unfiltered ("(no debugging symbols found)\n");
		  wrap_here ("");
		}
	      objfile->flags |= OBJF_SYMS;

	      /* We're done reading the symbol file; finish off complaints.  */
	      clear_complaints (&symfile_complaints, 0, 1);

	      /* Getting new symbols may change our opinion about what is
	         frameless.  */

	      reinit_frame_cache ();

	      /* Discard cleanups as symbol reading was successful.  */
	      discard_cleanups (old_cleanups);

	      /* If the mtime has changed between the time we set new_modtime
	         and now, we *want* this to be out of date, so don't call stat
	         again now.  */
	      objfile->mtime = new_modtime;
	      reread_one = 1;
              reread_separate_symbols (objfile);
	    }
	}
    }

  if (reread_one)
    clear_symtab_users ();
}


/* Handle separate debug info for OBJFILE, which has just been
   re-read:
   - If we had separate debug info before, but now we don't, get rid
     of the separated objfile.
   - If we didn't have separated debug info before, but now we do,
     read in the new separated debug info file.
   - If the debug link points to a different file, toss the old one
     and read the new one.
   This function does *not* handle the case where objfile is still
   using the same separate debug info file, but that file's timestamp
   has changed.  That case should be handled by the loop in
   reread_symbols already.  */
static void
reread_separate_symbols (struct objfile *objfile)
{
  char *debug_file;
  unsigned long crc32;

  /* Does the updated objfile's debug info live in a
     separate file?  */
  debug_file = find_separate_debug_file (objfile);

  if (objfile->separate_debug_objfile)
    {
      /* There are two cases where we need to get rid of
         the old separated debug info objfile:
         - if the new primary objfile doesn't have
         separated debug info, or
         - if the new primary objfile has separate debug
         info, but it's under a different filename.
 
         If the old and new objfiles both have separate
         debug info, under the same filename, then we're
         okay --- if the separated file's contents have
         changed, we will have caught that when we
         visited it in this function's outermost
         loop.  */
      if (! debug_file
          || strcmp (debug_file, objfile->separate_debug_objfile->name) != 0)
        free_objfile (objfile->separate_debug_objfile);
    }

  /* If the new objfile has separate debug info, and we
     haven't loaded it already, do so now.  */
  if (debug_file
      && ! objfile->separate_debug_objfile)
    {
      /* Use the same section offset table as objfile itself.
         Preserve the flags from objfile that make sense.  */
      objfile->separate_debug_objfile
        = (symbol_file_add_with_addrs_or_offsets
           (debug_file,
            info_verbose, /* from_tty: Don't override the default. */
            0, /* No addr table.  */
            objfile->section_offsets, objfile->num_sections,
            0, /* Not mainline.  See comments about this above.  */
            objfile->flags & (OBJF_REORDERED | OBJF_SHARED | OBJF_READNOW
                              | OBJF_USERLOADED)));
      objfile->separate_debug_objfile->separate_debug_objfile_backlink
        = objfile;
    }
}





typedef struct
{
  char *ext;
  enum language lang;
}
filename_language;

static filename_language *filename_language_table;
static int fl_table_size, fl_table_next;

static void
add_filename_language (char *ext, enum language lang)
{
  if (fl_table_next >= fl_table_size)
    {
      fl_table_size += 10;
      filename_language_table = 
	xrealloc (filename_language_table,
		  fl_table_size * sizeof (*filename_language_table));
    }

  filename_language_table[fl_table_next].ext = xstrdup (ext);
  filename_language_table[fl_table_next].lang = lang;
  fl_table_next++;
}

static char *ext_args;

static void
set_ext_lang_command (char *args, int from_tty)
{
  int i;
  char *cp = ext_args;
  enum language lang;

  /* First arg is filename extension, starting with '.' */
  if (*cp != '.')
    error ("'%s': Filename extension must begin with '.'", ext_args);

  /* Find end of first arg.  */
  while (*cp && !isspace (*cp))
    cp++;

  if (*cp == '\0')
    error ("'%s': two arguments required -- filename extension and language",
	   ext_args);

  /* Null-terminate first arg */
  *cp++ = '\0';

  /* Find beginning of second arg, which should be a source language.  */
  while (*cp && isspace (*cp))
    cp++;

  if (*cp == '\0')
    error ("'%s': two arguments required -- filename extension and language",
	   ext_args);

  /* Lookup the language from among those we know.  */
  lang = language_enum (cp);

  /* Now lookup the filename extension: do we already know it?  */
  for (i = 0; i < fl_table_next; i++)
    if (0 == strcmp (ext_args, filename_language_table[i].ext))
      break;

  if (i >= fl_table_next)
    {
      /* new file extension */
      add_filename_language (ext_args, lang);
    }
  else
    {
      /* redefining a previously known filename extension */

      /* if (from_tty) */
      /*   query ("Really make files of type %s '%s'?", */
      /*          ext_args, language_str (lang));           */

      xfree (filename_language_table[i].ext);
      filename_language_table[i].ext = xstrdup (ext_args);
      filename_language_table[i].lang = lang;
    }
}

static void
info_ext_lang_command (char *args, int from_tty)
{
  int i;

  printf_filtered ("Filename extensions and the languages they represent:");
  printf_filtered ("\n\n");
  for (i = 0; i < fl_table_next; i++)
    printf_filtered ("\t%s\t- %s\n",
		     filename_language_table[i].ext,
		     language_str (filename_language_table[i].lang));
}

static void
init_filename_language_table (void)
{
  if (fl_table_size == 0)	/* protect against repetition */
    {
      fl_table_size = 20;
      fl_table_next = 0;
      filename_language_table =
	xmalloc (fl_table_size * sizeof (*filename_language_table));
      add_filename_language (".c", language_c);
      add_filename_language (".C", language_cplus);
      add_filename_language (".cc", language_cplus);
      add_filename_language (".cp", language_cplus);
      add_filename_language (".cpp", language_cplus);
      add_filename_language (".cxx", language_cplus);
      add_filename_language (".c++", language_cplus);
      add_filename_language (".java", language_java);
      add_filename_language (".class", language_java);
      add_filename_language (".m", language_objc);
      add_filename_language (".f", language_fortran);
      add_filename_language (".F", language_fortran);
      add_filename_language (".s", language_asm);
      add_filename_language (".S", language_asm);
      add_filename_language (".pas", language_pascal);
      add_filename_language (".p", language_pascal);
      add_filename_language (".pp", language_pascal);
    }
}

enum language
deduce_language_from_filename (char *filename)
{
  int i;
  char *cp;

  if (filename != NULL)
    if ((cp = strrchr (filename, '.')) != NULL)
      for (i = 0; i < fl_table_next; i++)
	if (strcmp (cp, filename_language_table[i].ext) == 0)
	  return filename_language_table[i].lang;

  return language_unknown;
}

/* allocate_symtab:

   Allocate and partly initialize a new symbol table.  Return a pointer
   to it.  error() if no space.

   Caller must set these fields:
   LINETABLE(symtab)
   symtab->blockvector
   symtab->dirname
   symtab->free_code
   symtab->free_ptr
   possibly free_named_symtabs (symtab->filename);
 */

struct symtab *
allocate_symtab (char *filename, struct objfile *objfile)
{
  struct symtab *symtab;

  symtab = (struct symtab *)
    obstack_alloc (&objfile->objfile_obstack, sizeof (struct symtab));
  memset (symtab, 0, sizeof (*symtab));
  symtab->filename = obsavestring (filename, strlen (filename),
				   &objfile->objfile_obstack);
  symtab->fullname = NULL;
  symtab->language = deduce_language_from_filename (filename);
  symtab->debugformat = obsavestring ("unknown", 7,
				      &objfile->objfile_obstack);

  /* Hook it to the objfile it comes from */

  symtab->objfile = objfile;
  symtab->next = objfile->symtabs;
  objfile->symtabs = symtab;

  /* FIXME: This should go away.  It is only defined for the Z8000,
     and the Z8000 definition of this macro doesn't have anything to
     do with the now-nonexistent EXTRA_SYMTAB_INFO macro, it's just
     here for convenience.  */
#ifdef INIT_EXTRA_SYMTAB_INFO
  INIT_EXTRA_SYMTAB_INFO (symtab);
#endif

  return (symtab);
}

struct partial_symtab *
allocate_psymtab (char *filename, struct objfile *objfile)
{
  struct partial_symtab *psymtab;

  if (objfile->free_psymtabs)
    {
      psymtab = objfile->free_psymtabs;
      objfile->free_psymtabs = psymtab->next;
    }
  else
    psymtab = (struct partial_symtab *)
      obstack_alloc (&objfile->objfile_obstack,
		     sizeof (struct partial_symtab));

  memset (psymtab, 0, sizeof (struct partial_symtab));
  psymtab->filename = obsavestring (filename, strlen (filename),
				    &objfile->objfile_obstack);
  psymtab->symtab = NULL;

  /* Prepend it to the psymtab list for the objfile it belongs to.
     Psymtabs are searched in most recent inserted -> least recent
     inserted order. */

  psymtab->objfile = objfile;
  psymtab->next = objfile->psymtabs;
  objfile->psymtabs = psymtab;
#if 0
  {
    struct partial_symtab **prev_pst;
    psymtab->objfile = objfile;
    psymtab->next = NULL;
    prev_pst = &(objfile->psymtabs);
    while ((*prev_pst) != NULL)
      prev_pst = &((*prev_pst)->next);
    (*prev_pst) = psymtab;
  }
#endif

  return (psymtab);
}

void
discard_psymtab (struct partial_symtab *pst)
{
  struct partial_symtab **prev_pst;

  /* From dbxread.c:
     Empty psymtabs happen as a result of header files which don't
     have any symbols in them.  There can be a lot of them.  But this
     check is wrong, in that a psymtab with N_SLINE entries but
     nothing else is not empty, but we don't realize that.  Fixing
     that without slowing things down might be tricky.  */

  /* First, snip it out of the psymtab chain */

  prev_pst = &(pst->objfile->psymtabs);
  while ((*prev_pst) != pst)
    prev_pst = &((*prev_pst)->next);
  (*prev_pst) = pst->next;

  /* Next, put it on a free list for recycling */

  pst->next = pst->objfile->free_psymtabs;
  pst->objfile->free_psymtabs = pst;
}


/* Reset all data structures in gdb which may contain references to symbol
   table data.  */

void
clear_symtab_users (void)
{
  /* Someday, we should do better than this, by only blowing away
     the things that really need to be blown.  */
  clear_value_history ();
  clear_displays ();
  clear_internalvars ();
  breakpoint_re_set ();
  set_default_breakpoint (0, 0, 0, 0);
  clear_current_source_symtab_and_line ();
  clear_pc_function_cache ();
  if (target_new_objfile_hook)
    target_new_objfile_hook (NULL);
}

static void
clear_symtab_users_cleanup (void *ignore)
{
  clear_symtab_users ();
}

/* clear_symtab_users_once:

   This function is run after symbol reading, or from a cleanup.
   If an old symbol table was obsoleted, the old symbol table
   has been blown away, but the other GDB data structures that may 
   reference it have not yet been cleared or re-directed.  (The old
   symtab was zapped, and the cleanup queued, in free_named_symtab()
   below.)

   This function can be queued N times as a cleanup, or called
   directly; it will do all the work the first time, and then will be a
   no-op until the next time it is queued.  This works by bumping a
   counter at queueing time.  Much later when the cleanup is run, or at
   the end of symbol processing (in case the cleanup is discarded), if
   the queued count is greater than the "done-count", we do the work
   and set the done-count to the queued count.  If the queued count is
   less than or equal to the done-count, we just ignore the call.  This
   is needed because reading a single .o file will often replace many
   symtabs (one per .h file, for example), and we don't want to reset
   the breakpoints N times in the user's face.

   The reason we both queue a cleanup, and call it directly after symbol
   reading, is because the cleanup protects us in case of errors, but is
   discarded if symbol reading is successful.  */

#if 0
/* FIXME:  As free_named_symtabs is currently a big noop this function
   is no longer needed.  */
static void clear_symtab_users_once (void);

static int clear_symtab_users_queued;
static int clear_symtab_users_done;

static void
clear_symtab_users_once (void)
{
  /* Enforce once-per-`do_cleanups'-semantics */
  if (clear_symtab_users_queued <= clear_symtab_users_done)
    return;
  clear_symtab_users_done = clear_symtab_users_queued;

  clear_symtab_users ();
}
#endif

/* Delete the specified psymtab, and any others that reference it.  */

static void
cashier_psymtab (struct partial_symtab *pst)
{
  struct partial_symtab *ps, *pprev = NULL;
  int i;

  /* Find its previous psymtab in the chain */
  for (ps = pst->objfile->psymtabs; ps; ps = ps->next)
    {
      if (ps == pst)
	break;
      pprev = ps;
    }

  if (ps)
    {
      /* Unhook it from the chain.  */
      if (ps == pst->objfile->psymtabs)
	pst->objfile->psymtabs = ps->next;
      else
	pprev->next = ps->next;

      /* FIXME, we can't conveniently deallocate the entries in the
         partial_symbol lists (global_psymbols/static_psymbols) that
         this psymtab points to.  These just take up space until all
         the psymtabs are reclaimed.  Ditto the dependencies list and
         filename, which are all in the objfile_obstack.  */

      /* We need to cashier any psymtab that has this one as a dependency... */
    again:
      for (ps = pst->objfile->psymtabs; ps; ps = ps->next)
	{
	  for (i = 0; i < ps->number_of_dependencies; i++)
	    {
	      if (ps->dependencies[i] == pst)
		{
		  cashier_psymtab (ps);
		  goto again;	/* Must restart, chain has been munged. */
		}
	    }
	}
    }
}

/* If a symtab or psymtab for filename NAME is found, free it along
   with any dependent breakpoints, displays, etc.
   Used when loading new versions of object modules with the "add-file"
   command.  This is only called on the top-level symtab or psymtab's name;
   it is not called for subsidiary files such as .h files.

   Return value is 1 if we blew away the environment, 0 if not.
   FIXME.  The return value appears to never be used.

   FIXME.  I think this is not the best way to do this.  We should
   work on being gentler to the environment while still cleaning up
   all stray pointers into the freed symtab.  */

int
free_named_symtabs (char *name)
{
#if 0
  /* FIXME:  With the new method of each objfile having it's own
     psymtab list, this function needs serious rethinking.  In particular,
     why was it ever necessary to toss psymtabs with specific compilation
     unit filenames, as opposed to all psymtabs from a particular symbol
     file?  -- fnf
     Well, the answer is that some systems permit reloading of particular
     compilation units.  We want to blow away any old info about these
     compilation units, regardless of which objfiles they arrived in. --gnu.  */

  struct symtab *s;
  struct symtab *prev;
  struct partial_symtab *ps;
  struct blockvector *bv;
  int blewit = 0;

  /* We only wack things if the symbol-reload switch is set.  */
  if (!symbol_reloading)
    return 0;

  /* Some symbol formats have trouble providing file names... */
  if (name == 0 || *name == '\0')
    return 0;

  /* Look for a psymtab with the specified name.  */

again2:
  for (ps = partial_symtab_list; ps; ps = ps->next)
    {
      if (strcmp (name, ps->filename) == 0)
	{
	  cashier_psymtab (ps);	/* Blow it away...and its little dog, too.  */
	  goto again2;		/* Must restart, chain has been munged */
	}
    }

  /* Look for a symtab with the specified name.  */

  for (s = symtab_list; s; s = s->next)
    {
      if (strcmp (name, s->filename) == 0)
	break;
      prev = s;
    }

  if (s)
    {
      if (s == symtab_list)
	symtab_list = s->next;
      else
	prev->next = s->next;

      /* For now, queue a delete for all breakpoints, displays, etc., whether
         or not they depend on the symtab being freed.  This should be
         changed so that only those data structures affected are deleted.  */

      /* But don't delete anything if the symtab is empty.
         This test is necessary due to a bug in "dbxread.c" that
         causes empty symtabs to be created for N_SO symbols that
         contain the pathname of the object file.  (This problem
         has been fixed in GDB 3.9x).  */

      bv = BLOCKVECTOR (s);
      if (BLOCKVECTOR_NBLOCKS (bv) > 2
	  || BLOCK_NSYMS (BLOCKVECTOR_BLOCK (bv, GLOBAL_BLOCK))
	  || BLOCK_NSYMS (BLOCKVECTOR_BLOCK (bv, STATIC_BLOCK)))
	{
	  complaint (&symfile_complaints, "Replacing old symbols for `%s'",
		     name);
	  clear_symtab_users_queued++;
	  make_cleanup (clear_symtab_users_once, 0);
	  blewit = 1;
	}
      else
	{
	  complaint (&symfile_complaints, "Empty symbol table found for `%s'",
		     name);
	}

      free_symtab (s);
    }
  else
    {
      /* It is still possible that some breakpoints will be affected
         even though no symtab was found, since the file might have
         been compiled without debugging, and hence not be associated
         with a symtab.  In order to handle this correctly, we would need
         to keep a list of text address ranges for undebuggable files.
         For now, we do nothing, since this is a fairly obscure case.  */
      ;
    }

  /* FIXME, what about the minimal symbol table? */
  return blewit;
#else
  return (0);
#endif
}

/* Allocate and partially fill a partial symtab.  It will be
   completely filled at the end of the symbol list.

   FILENAME is the name of the symbol-file we are reading from. */

struct partial_symtab *
start_psymtab_common (struct objfile *objfile,
		      struct section_offsets *section_offsets, char *filename,
		      CORE_ADDR textlow, struct partial_symbol **global_syms,
		      struct partial_symbol **static_syms)
{
  struct partial_symtab *psymtab;

  psymtab = allocate_psymtab (filename, objfile);
  psymtab->section_offsets = section_offsets;
  psymtab->textlow = textlow;
  psymtab->texthigh = psymtab->textlow;		/* default */
  psymtab->globals_offset = global_syms - objfile->global_psymbols.list;
  psymtab->statics_offset = static_syms - objfile->static_psymbols.list;
  return (psymtab);
}

/* Add a symbol with a long value to a psymtab.
   Since one arg is a struct, we pass in a ptr and deref it (sigh).  
   Return the partial symbol that has been added.  */

/* NOTE: carlton/2003-09-11: The reason why we return the partial
   symbol is so that callers can get access to the symbol's demangled
   name, which they don't have any cheap way to determine otherwise.
   (Currenly, dwarf2read.c is the only file who uses that information,
   though it's possible that other readers might in the future.)
   Elena wasn't thrilled about that, and I don't blame her, but we
   couldn't come up with a better way to get that information.  If
   it's needed in other situations, we could consider breaking up
   SYMBOL_SET_NAMES to provide access to the demangled name lookup
   cache.  */

const struct partial_symbol *
add_psymbol_to_list (char *name, int namelength, domain_enum domain,
		     enum address_class class,
		     struct psymbol_allocation_list *list, long val,	/* Value as a long */
		     CORE_ADDR coreaddr,	/* Value as a CORE_ADDR */
		     enum language language, struct objfile *objfile)
{
  struct partial_symbol *psym;
  char *buf = alloca (namelength + 1);
  /* psymbol is static so that there will be no uninitialized gaps in the
     structure which might contain random data, causing cache misses in
     bcache. */
  static struct partial_symbol psymbol;

  /* Create local copy of the partial symbol */
  memcpy (buf, name, namelength);
  buf[namelength] = '\0';
  /* val and coreaddr are mutually exclusive, one of them *will* be zero */
  if (val != 0)
    {
      SYMBOL_VALUE (&psymbol) = val;
    }
  else
    {
      SYMBOL_VALUE_ADDRESS (&psymbol) = coreaddr;
    }
  SYMBOL_SECTION (&psymbol) = 0;
  SYMBOL_LANGUAGE (&psymbol) = language;
  PSYMBOL_DOMAIN (&psymbol) = domain;
  PSYMBOL_CLASS (&psymbol) = class;

  SYMBOL_SET_NAMES (&psymbol, buf, namelength, objfile);

  /* Stash the partial symbol away in the cache */
  psym = deprecated_bcache (&psymbol, sizeof (struct partial_symbol),
			    objfile->psymbol_cache);

  /* Save pointer to partial symbol in psymtab, growing symtab if needed. */
  if (list->next >= list->list + list->size)
    {
      extend_psymbol_list (list, objfile);
    }
  *list->next++ = psym;
  OBJSTAT (objfile, n_psyms++);

  return psym;
}

/* Add a symbol with a long value to a psymtab. This differs from
 * add_psymbol_to_list above in taking both a mangled and a demangled
 * name. */

void
add_psymbol_with_dem_name_to_list (char *name, int namelength, char *dem_name,
				   int dem_namelength, domain_enum domain,
				   enum address_class class,
				   struct psymbol_allocation_list *list, long val,	/* Value as a long */
				   CORE_ADDR coreaddr,	/* Value as a CORE_ADDR */
				   enum language language,
				   struct objfile *objfile)
{
  struct partial_symbol *psym;
  char *buf = alloca (namelength + 1);
  /* psymbol is static so that there will be no uninitialized gaps in the
     structure which might contain random data, causing cache misses in
     bcache. */
  static struct partial_symbol psymbol;

  /* Create local copy of the partial symbol */

  memcpy (buf, name, namelength);
  buf[namelength] = '\0';
  DEPRECATED_SYMBOL_NAME (&psymbol) = deprecated_bcache (buf, namelength + 1,
							 objfile->psymbol_cache);

  buf = alloca (dem_namelength + 1);
  memcpy (buf, dem_name, dem_namelength);
  buf[dem_namelength] = '\0';

  switch (language)
    {
    case language_c:
    case language_cplus:
      SYMBOL_CPLUS_DEMANGLED_NAME (&psymbol) =
	deprecated_bcache (buf, dem_namelength + 1, objfile->psymbol_cache);
      break;
      /* FIXME What should be done for the default case? Ignoring for now. */
    }

  /* val and coreaddr are mutually exclusive, one of them *will* be zero */
  if (val != 0)
    {
      SYMBOL_VALUE (&psymbol) = val;
    }
  else
    {
      SYMBOL_VALUE_ADDRESS (&psymbol) = coreaddr;
    }
  SYMBOL_SECTION (&psymbol) = 0;
  SYMBOL_LANGUAGE (&psymbol) = language;
  PSYMBOL_DOMAIN (&psymbol) = domain;
  PSYMBOL_CLASS (&psymbol) = class;
  SYMBOL_INIT_LANGUAGE_SPECIFIC (&psymbol, language);

  /* Stash the partial symbol away in the cache */
  psym = deprecated_bcache (&psymbol, sizeof (struct partial_symbol),
			    objfile->psymbol_cache);

  /* Save pointer to partial symbol in psymtab, growing symtab if needed. */
  if (list->next >= list->list + list->size)
    {
      extend_psymbol_list (list, objfile);
    }
  *list->next++ = psym;
  OBJSTAT (objfile, n_psyms++);
}

/* Initialize storage for partial symbols.  */

void
init_psymbol_list (struct objfile *objfile, int total_symbols)
{
  /* Free any previously allocated psymbol lists.  */

  if (objfile->global_psymbols.list)
    {
      xmfree (objfile->md, objfile->global_psymbols.list);
    }
  if (objfile->static_psymbols.list)
    {
      xmfree (objfile->md, objfile->static_psymbols.list);
    }

  /* Current best guess is that approximately a twentieth
     of the total symbols (in a debugging file) are global or static
     oriented symbols */

  objfile->global_psymbols.size = total_symbols / 10;
  objfile->static_psymbols.size = total_symbols / 10;

  if (objfile->global_psymbols.size > 0)
    {
      objfile->global_psymbols.next =
	objfile->global_psymbols.list = (struct partial_symbol **)
	xmmalloc (objfile->md, (objfile->global_psymbols.size
				* sizeof (struct partial_symbol *)));
    }
  if (objfile->static_psymbols.size > 0)
    {
      objfile->static_psymbols.next =
	objfile->static_psymbols.list = (struct partial_symbol **)
	xmmalloc (objfile->md, (objfile->static_psymbols.size
				* sizeof (struct partial_symbol *)));
    }
}

/* OVERLAYS:
   The following code implements an abstraction for debugging overlay sections.

   The target model is as follows:
   1) The gnu linker will permit multiple sections to be mapped into the
   same VMA, each with its own unique LMA (or load address).
   2) It is assumed that some runtime mechanism exists for mapping the
   sections, one by one, from the load address into the VMA address.
   3) This code provides a mechanism for gdb to keep track of which 
   sections should be considered to be mapped from the VMA to the LMA.
   This information is used for symbol lookup, and memory read/write.
   For instance, if a section has been mapped then its contents 
   should be read from the VMA, otherwise from the LMA.

   Two levels of debugger support for overlays are available.  One is
   "manual", in which the debugger relies on the user to tell it which
   overlays are currently mapped.  This level of support is
   implemented entirely in the core debugger, and the information about
   whether a section is mapped is kept in the objfile->obj_section table.

   The second level of support is "automatic", and is only available if
   the target-specific code provides functionality to read the target's
   overlay mapping table, and translate its contents for the debugger
   (by updating the mapped state information in the obj_section tables).

   The interface is as follows:
   User commands:
   overlay map <name>   -- tell gdb to consider this section mapped
   overlay unmap <name> -- tell gdb to consider this section unmapped
   overlay list         -- list the sections that GDB thinks are mapped
   overlay read-target  -- get the target's state of what's mapped
   overlay off/manual/auto -- set overlay debugging state
   Functional interface:
   find_pc_mapped_section(pc):    if the pc is in the range of a mapped
   section, return that section.
   find_pc_overlay(pc):       find any overlay section that contains 
   the pc, either in its VMA or its LMA
   overlay_is_mapped(sect):       true if overlay is marked as mapped
   section_is_overlay(sect):      true if section's VMA != LMA
   pc_in_mapped_range(pc,sec):    true if pc belongs to section's VMA
   pc_in_unmapped_range(...):     true if pc belongs to section's LMA
   sections_overlap(sec1, sec2):  true if mapped sec1 and sec2 ranges overlap
   overlay_mapped_address(...):   map an address from section's LMA to VMA
   overlay_unmapped_address(...): map an address from section's VMA to LMA
   symbol_overlayed_address(...): Return a "current" address for symbol:
   either in VMA or LMA depending on whether
   the symbol's section is currently mapped
 */

/* Overlay debugging state: */

enum overlay_debugging_state overlay_debugging = ovly_off;
int overlay_cache_invalid = 0;	/* True if need to refresh mapped state */

/* Target vector for refreshing overlay mapped state */
static void simple_overlay_update (struct obj_section *);
void (*target_overlay_update) (struct obj_section *) = simple_overlay_update;

/* Function: section_is_overlay (SECTION)
   Returns true if SECTION has VMA not equal to LMA, ie. 
   SECTION is loaded at an address different from where it will "run".  */

int
section_is_overlay (asection *section)
{
  /* FIXME: need bfd *, so we can use bfd_section_lma methods. */

  if (overlay_debugging)
    if (section && section->lma != 0 &&
	section->vma != section->lma)
      return 1;

  return 0;
}

/* Function: overlay_invalidate_all (void)
   Invalidate the mapped state of all overlay sections (mark it as stale).  */

static void
overlay_invalidate_all (void)
{
  struct objfile *objfile;
  struct obj_section *sect;

  ALL_OBJSECTIONS (objfile, sect)
    if (section_is_overlay (sect->the_bfd_section))
    sect->ovly_mapped = -1;
}

/* Function: overlay_is_mapped (SECTION)
   Returns true if section is an overlay, and is currently mapped. 
   Private: public access is thru function section_is_mapped.

   Access to the ovly_mapped flag is restricted to this function, so
   that we can do automatic update.  If the global flag
   OVERLAY_CACHE_INVALID is set (by wait_for_inferior), then call
   overlay_invalidate_all.  If the mapped state of the particular
   section is stale, then call TARGET_OVERLAY_UPDATE to refresh it.  */

static int
overlay_is_mapped (struct obj_section *osect)
{
  if (osect == 0 || !section_is_overlay (osect->the_bfd_section))
    return 0;

  switch (overlay_debugging)
    {
    default:
    case ovly_off:
      return 0;			/* overlay debugging off */
    case ovly_auto:		/* overlay debugging automatic */
      /* Unles there is a target_overlay_update function, 
         there's really nothing useful to do here (can't really go auto)  */
      if (target_overlay_update)
	{
	  if (overlay_cache_invalid)
	    {
	      overlay_invalidate_all ();
	      overlay_cache_invalid = 0;
	    }
	  if (osect->ovly_mapped == -1)
	    (*target_overlay_update) (osect);
	}
      /* fall thru to manual case */
    case ovly_on:		/* overlay debugging manual */
      return osect->ovly_mapped == 1;
    }
}

/* Function: section_is_mapped
   Returns true if section is an overlay, and is currently mapped.  */

int
section_is_mapped (asection *section)
{
  struct objfile *objfile;
  struct obj_section *osect;

  if (overlay_debugging)
    if (section && section_is_overlay (section))
      ALL_OBJSECTIONS (objfile, osect)
	if (osect->the_bfd_section == section)
	return overlay_is_mapped (osect);

  return 0;
}

/* Function: pc_in_unmapped_range
   If PC falls into the lma range of SECTION, return true, else false.  */

CORE_ADDR
pc_in_unmapped_range (CORE_ADDR pc, asection *section)
{
  /* FIXME: need bfd *, so we can use bfd_section_lma methods. */

  int size;

  if (overlay_debugging)
    if (section && section_is_overlay (section))
      {
	size = bfd_get_section_size (section);
	if (section->lma <= pc && pc < section->lma + size)
	  return 1;
      }
  return 0;
}

/* Function: pc_in_mapped_range
   If PC falls into the vma range of SECTION, return true, else false.  */

CORE_ADDR
pc_in_mapped_range (CORE_ADDR pc, asection *section)
{
  /* FIXME: need bfd *, so we can use bfd_section_vma methods. */

  int size;

  if (overlay_debugging)
    if (section && section_is_overlay (section))
      {
	size = bfd_get_section_size (section);
	if (section->vma <= pc && pc < section->vma + size)
	  return 1;
      }
  return 0;
}


/* Return true if the mapped ranges of sections A and B overlap, false
   otherwise.  */
static int
sections_overlap (asection *a, asection *b)
{
  /* FIXME: need bfd *, so we can use bfd_section_vma methods. */

  CORE_ADDR a_start = a->vma;
  CORE_ADDR a_end = a->vma + bfd_get_section_size (a);
  CORE_ADDR b_start = b->vma;
  CORE_ADDR b_end = b->vma + bfd_get_section_size (b);

  return (a_start < b_end && b_start < a_end);
}

/* Function: overlay_unmapped_address (PC, SECTION)
   Returns the address corresponding to PC in the unmapped (load) range.
   May be the same as PC.  */

CORE_ADDR
overlay_unmapped_address (CORE_ADDR pc, asection *section)
{
  /* FIXME: need bfd *, so we can use bfd_section_lma methods. */

  if (overlay_debugging)
    if (section && section_is_overlay (section) &&
	pc_in_mapped_range (pc, section))
      return pc + section->lma - section->vma;

  return pc;
}

/* Function: overlay_mapped_address (PC, SECTION)
   Returns the address corresponding to PC in the mapped (runtime) range.
   May be the same as PC.  */

CORE_ADDR
overlay_mapped_address (CORE_ADDR pc, asection *section)
{
  /* FIXME: need bfd *, so we can use bfd_section_vma methods. */

  if (overlay_debugging)
    if (section && section_is_overlay (section) &&
	pc_in_unmapped_range (pc, section))
      return pc + section->vma - section->lma;

  return pc;
}


/* Function: symbol_overlayed_address 
   Return one of two addresses (relative to the VMA or to the LMA),
   depending on whether the section is mapped or not.  */

CORE_ADDR
symbol_overlayed_address (CORE_ADDR address, asection *section)
{
  if (overlay_debugging)
    {
      /* If the symbol has no section, just return its regular address. */
      if (section == 0)
	return address;
      /* If the symbol's section is not an overlay, just return its address */
      if (!section_is_overlay (section))
	return address;
      /* If the symbol's section is mapped, just return its address */
      if (section_is_mapped (section))
	return address;
      /*
       * HOWEVER: if the symbol is in an overlay section which is NOT mapped,
       * then return its LOADED address rather than its vma address!!
       */
      return overlay_unmapped_address (address, section);
    }
  return address;
}

/* Function: find_pc_overlay (PC) 
   Return the best-match overlay section for PC:
   If PC matches a mapped overlay section's VMA, return that section.
   Else if PC matches an unmapped section's VMA, return that section.
   Else if PC matches an unmapped section's LMA, return that section.  */

asection *
find_pc_overlay (CORE_ADDR pc)
{
  struct objfile *objfile;
  struct obj_section *osect, *best_match = NULL;

  if (overlay_debugging)
    ALL_OBJSECTIONS (objfile, osect)
      if (section_is_overlay (osect->the_bfd_section))
      {
	if (pc_in_mapped_range (pc, osect->the_bfd_section))
	  {
	    if (overlay_is_mapped (osect))
	      return osect->the_bfd_section;
	    else
	      best_match = osect;
	  }
	else if (pc_in_unmapped_range (pc, osect->the_bfd_section))
	  best_match = osect;
      }
  return best_match ? best_match->the_bfd_section : NULL;
}

/* Function: find_pc_mapped_section (PC)
   If PC falls into the VMA address range of an overlay section that is 
   currently marked as MAPPED, return that section.  Else return NULL.  */

asection *
find_pc_mapped_section (CORE_ADDR pc)
{
  struct objfile *objfile;
  struct obj_section *osect;

  if (overlay_debugging)
    ALL_OBJSECTIONS (objfile, osect)
      if (pc_in_mapped_range (pc, osect->the_bfd_section) &&
	  overlay_is_mapped (osect))
      return osect->the_bfd_section;

  return NULL;
}

/* Function: list_overlays_command
   Print a list of mapped sections and their PC ranges */

void
list_overlays_command (char *args, int from_tty)
{
  int nmapped = 0;
  struct objfile *objfile;
  struct obj_section *osect;

  if (overlay_debugging)
    ALL_OBJSECTIONS (objfile, osect)
      if (overlay_is_mapped (osect))
      {
	const char *name;
	bfd_vma lma, vma;
	int size;

	vma = bfd_section_vma (objfile->obfd, osect->the_bfd_section);
	lma = bfd_section_lma (objfile->obfd, osect->the_bfd_section);
	size = bfd_get_section_size (osect->the_bfd_section);
	name = bfd_section_name (objfile->obfd, osect->the_bfd_section);

	printf_filtered ("Section %s, loaded at ", name);
	print_address_numeric (lma, 1, gdb_stdout);
	puts_filtered (" - ");
	print_address_numeric (lma + size, 1, gdb_stdout);
	printf_filtered (", mapped at ");
	print_address_numeric (vma, 1, gdb_stdout);
	puts_filtered (" - ");
	print_address_numeric (vma + size, 1, gdb_stdout);
	puts_filtered ("\n");

	nmapped++;
      }
  if (nmapped == 0)
    printf_filtered ("No sections are mapped.\n");
}

/* Function: map_overlay_command
   Mark the named section as mapped (ie. residing at its VMA address).  */

void
map_overlay_command (char *args, int from_tty)
{
  struct objfile *objfile, *objfile2;
  struct obj_section *sec, *sec2;
  asection *bfdsec;

  if (!overlay_debugging)
    error ("\
Overlay debugging not enabled.  Use either the 'overlay auto' or\n\
the 'overlay manual' command.");

  if (args == 0 || *args == 0)
    error ("Argument required: name of an overlay section");

  /* First, find a section matching the user supplied argument */
  ALL_OBJSECTIONS (objfile, sec)
    if (!strcmp (bfd_section_name (objfile->obfd, sec->the_bfd_section), args))
    {
      /* Now, check to see if the section is an overlay. */
      bfdsec = sec->the_bfd_section;
      if (!section_is_overlay (bfdsec))
	continue;		/* not an overlay section */

      /* Mark the overlay as "mapped" */
      sec->ovly_mapped = 1;

      /* Next, make a pass and unmap any sections that are
         overlapped by this new section: */
      ALL_OBJSECTIONS (objfile2, sec2)
	if (sec2->ovly_mapped
            && sec != sec2
            && sec->the_bfd_section != sec2->the_bfd_section
            && sections_overlap (sec->the_bfd_section,
                                 sec2->the_bfd_section))
	{
	  if (info_verbose)
	    printf_unfiltered ("Note: section %s unmapped by overlap\n",
			     bfd_section_name (objfile->obfd,
					       sec2->the_bfd_section));
	  sec2->ovly_mapped = 0;	/* sec2 overlaps sec: unmap sec2 */
	}
      return;
    }
  error ("No overlay section called %s", args);
}

/* Function: unmap_overlay_command
   Mark the overlay section as unmapped 
   (ie. resident in its LMA address range, rather than the VMA range).  */

void
unmap_overlay_command (char *args, int from_tty)
{
  struct objfile *objfile;
  struct obj_section *sec;

  if (!overlay_debugging)
    error ("\
Overlay debugging not enabled.  Use either the 'overlay auto' or\n\
the 'overlay manual' command.");

  if (args == 0 || *args == 0)
    error ("Argument required: name of an overlay section");

  /* First, find a section matching the user supplied argument */
  ALL_OBJSECTIONS (objfile, sec)
    if (!strcmp (bfd_section_name (objfile->obfd, sec->the_bfd_section), args))
    {
      if (!sec->ovly_mapped)
	error ("Section %s is not mapped", args);
      sec->ovly_mapped = 0;
      return;
    }
  error ("No overlay section called %s", args);
}

/* Function: overlay_auto_command
   A utility command to turn on overlay debugging.
   Possibly this should be done via a set/show command. */

static void
overlay_auto_command (char *args, int from_tty)
{
  overlay_debugging = ovly_auto;
  enable_overlay_breakpoints ();
  if (info_verbose)
    printf_unfiltered ("Automatic overlay debugging enabled.");
}

/* Function: overlay_manual_command
   A utility command to turn on overlay debugging.
   Possibly this should be done via a set/show command. */

static void
overlay_manual_command (char *args, int from_tty)
{
  overlay_debugging = ovly_on;
  disable_overlay_breakpoints ();
  if (info_verbose)
    printf_unfiltered ("Overlay debugging enabled.");
}

/* Function: overlay_off_command
   A utility command to turn on overlay debugging.
   Possibly this should be done via a set/show command. */

static void
overlay_off_command (char *args, int from_tty)
{
  overlay_debugging = ovly_off;
  disable_overlay_breakpoints ();
  if (info_verbose)
    printf_unfiltered ("Overlay debugging disabled.");
}

static void
overlay_load_command (char *args, int from_tty)
{
  if (target_overlay_update)
    (*target_overlay_update) (NULL);
  else
    error ("This target does not know how to read its overlay state.");
}

/* Function: overlay_command
   A place-holder for a mis-typed command */

/* Command list chain containing all defined "overlay" subcommands. */
struct cmd_list_element *overlaylist;

static void
overlay_command (char *args, int from_tty)
{
  printf_unfiltered
    ("\"overlay\" must be followed by the name of an overlay command.\n");
  help_list (overlaylist, "overlay ", -1, gdb_stdout);
}


/* Target Overlays for the "Simplest" overlay manager:

   This is GDB's default target overlay layer.  It works with the 
   minimal overlay manager supplied as an example by Cygnus.  The 
   entry point is via a function pointer "target_overlay_update", 
   so targets that use a different runtime overlay manager can 
   substitute their own overlay_update function and take over the
   function pointer.

   The overlay_update function pokes around in the target's data structures
   to see what overlays are mapped, and updates GDB's overlay mapping with
   this information.

   In this simple implementation, the target data structures are as follows:
   unsigned _novlys;            /# number of overlay sections #/
   unsigned _ovly_table[_novlys][4] = {
   {VMA, SIZE, LMA, MAPPED},    /# one entry per overlay section #/
   {..., ...,  ..., ...},
   }
   unsigned _novly_regions;     /# number of overlay regions #/
   unsigned _ovly_region_table[_novly_regions][3] = {
   {VMA, SIZE, MAPPED_TO_LMA},  /# one entry per overlay region #/
   {..., ...,  ...},
   }
   These functions will attempt to update GDB's mappedness state in the
   symbol section table, based on the target's mappedness state.

   To do this, we keep a cached copy of the target's _ovly_table, and
   attempt to detect when the cached copy is invalidated.  The main
   entry point is "simple_overlay_update(SECT), which looks up SECT in
   the cached table and re-reads only the entry for that section from
   the target (whenever possible).
 */

/* Cached, dynamically allocated copies of the target data structures: */
static unsigned (*cache_ovly_table)[4] = 0;
#if 0
static unsigned (*cache_ovly_region_table)[3] = 0;
#endif
static unsigned cache_novlys = 0;
#if 0
static unsigned cache_novly_regions = 0;
#endif
static CORE_ADDR cache_ovly_table_base = 0;
#if 0
static CORE_ADDR cache_ovly_region_table_base = 0;
#endif
enum ovly_index
  {
    VMA, SIZE, LMA, MAPPED
  };
#define TARGET_LONG_BYTES (TARGET_LONG_BIT / TARGET_CHAR_BIT)

/* Throw away the cached copy of _ovly_table */
static void
simple_free_overlay_table (void)
{
  if (cache_ovly_table)
    xfree (cache_ovly_table);
  cache_novlys = 0;
  cache_ovly_table = NULL;
  cache_ovly_table_base = 0;
}

#if 0
/* Throw away the cached copy of _ovly_region_table */
static void
simple_free_overlay_region_table (void)
{
  if (cache_ovly_region_table)
    xfree (cache_ovly_region_table);
  cache_novly_regions = 0;
  cache_ovly_region_table = NULL;
  cache_ovly_region_table_base = 0;
}
#endif

/* Read an array of ints from the target into a local buffer.
   Convert to host order.  int LEN is number of ints  */
static void
read_target_long_array (CORE_ADDR memaddr, unsigned int *myaddr, int len)
{
  /* FIXME (alloca): Not safe if array is very large. */
  char *buf = alloca (len * TARGET_LONG_BYTES);
  int i;

  read_memory (memaddr, buf, len * TARGET_LONG_BYTES);
  for (i = 0; i < len; i++)
    myaddr[i] = extract_unsigned_integer (TARGET_LONG_BYTES * i + buf,
					  TARGET_LONG_BYTES);
}

/* Find and grab a copy of the target _ovly_table
   (and _novlys, which is needed for the table's size) */
static int
simple_read_overlay_table (void)
{
  struct minimal_symbol *novlys_msym, *ovly_table_msym;

  simple_free_overlay_table ();
  novlys_msym = lookup_minimal_symbol ("_novlys", NULL, NULL);
  if (! novlys_msym)
    {
      error ("Error reading inferior's overlay table: "
             "couldn't find `_novlys' variable\n"
             "in inferior.  Use `overlay manual' mode.");
      return 0;
    }

  ovly_table_msym = lookup_minimal_symbol ("_ovly_table", NULL, NULL);
  if (! ovly_table_msym)
    {
      error ("Error reading inferior's overlay table: couldn't find "
             "`_ovly_table' array\n"
             "in inferior.  Use `overlay manual' mode.");
      return 0;
    }

  cache_novlys = read_memory_integer (SYMBOL_VALUE_ADDRESS (novlys_msym), 4);
  cache_ovly_table
    = (void *) xmalloc (cache_novlys * sizeof (*cache_ovly_table));
  cache_ovly_table_base = SYMBOL_VALUE_ADDRESS (ovly_table_msym);
  read_target_long_array (cache_ovly_table_base,
                          (int *) cache_ovly_table,
                          cache_novlys * 4);

  return 1;			/* SUCCESS */
}

#if 0
/* Find and grab a copy of the target _ovly_region_table
   (and _novly_regions, which is needed for the table's size) */
static int
simple_read_overlay_region_table (void)
{
  struct minimal_symbol *msym;

  simple_free_overlay_region_table ();
  msym = lookup_minimal_symbol ("_novly_regions", NULL, NULL);
  if (msym != NULL)
    cache_novly_regions = read_memory_integer (SYMBOL_VALUE_ADDRESS (msym), 4);
  else
    return 0;			/* failure */
  cache_ovly_region_table = (void *) xmalloc (cache_novly_regions * 12);
  if (cache_ovly_region_table != NULL)
    {
      msym = lookup_minimal_symbol ("_ovly_region_table", NULL, NULL);
      if (msym != NULL)
	{
	  cache_ovly_region_table_base = SYMBOL_VALUE_ADDRESS (msym);
	  read_target_long_array (cache_ovly_region_table_base,
				  (int *) cache_ovly_region_table,
				  cache_novly_regions * 3);
	}
      else
	return 0;		/* failure */
    }
  else
    return 0;			/* failure */
  return 1;			/* SUCCESS */
}
#endif

/* Function: simple_overlay_update_1 
   A helper function for simple_overlay_update.  Assuming a cached copy
   of _ovly_table exists, look through it to find an entry whose vma,
   lma and size match those of OSECT.  Re-read the entry and make sure
   it still matches OSECT (else the table may no longer be valid).
   Set OSECT's mapped state to match the entry.  Return: 1 for
   success, 0 for failure.  */

static int
simple_overlay_update_1 (struct obj_section *osect)
{
  int i, size;
  bfd *obfd = osect->objfile->obfd;
  asection *bsect = osect->the_bfd_section;

  size = bfd_get_section_size (osect->the_bfd_section);
  for (i = 0; i < cache_novlys; i++)
    if (cache_ovly_table[i][VMA] == bfd_section_vma (obfd, bsect)
	&& cache_ovly_table[i][LMA] == bfd_section_lma (obfd, bsect)
	/* && cache_ovly_table[i][SIZE] == size */ )
      {
	read_target_long_array (cache_ovly_table_base + i * TARGET_LONG_BYTES,
				(int *) cache_ovly_table[i], 4);
	if (cache_ovly_table[i][VMA] == bfd_section_vma (obfd, bsect)
	    && cache_ovly_table[i][LMA] == bfd_section_lma (obfd, bsect)
	    /* && cache_ovly_table[i][SIZE] == size */ )
	  {
	    osect->ovly_mapped = cache_ovly_table[i][MAPPED];
	    return 1;
	  }
	else	/* Warning!  Warning!  Target's ovly table has changed! */
	  return 0;
      }
  return 0;
}

/* Function: simple_overlay_update
   If OSECT is NULL, then update all sections' mapped state 
   (after re-reading the entire target _ovly_table). 
   If OSECT is non-NULL, then try to find a matching entry in the 
   cached ovly_table and update only OSECT's mapped state.
   If a cached entry can't be found or the cache isn't valid, then 
   re-read the entire cache, and go ahead and update all sections.  */

static void
simple_overlay_update (struct obj_section *osect)
{
  struct objfile *objfile;

  /* Were we given an osect to look up?  NULL means do all of them. */
  if (osect)
    /* Have we got a cached copy of the target's overlay table? */
    if (cache_ovly_table != NULL)
      /* Does its cached location match what's currently in the symtab? */
      if (cache_ovly_table_base ==
	  SYMBOL_VALUE_ADDRESS (lookup_minimal_symbol ("_ovly_table", NULL, NULL)))
	/* Then go ahead and try to look up this single section in the cache */
	if (simple_overlay_update_1 (osect))
	  /* Found it!  We're done. */
	  return;

  /* Cached table no good: need to read the entire table anew.
     Or else we want all the sections, in which case it's actually
     more efficient to read the whole table in one block anyway.  */

  if (! simple_read_overlay_table ())
    return;

  /* Now may as well update all sections, even if only one was requested. */
  ALL_OBJSECTIONS (objfile, osect)
    if (section_is_overlay (osect->the_bfd_section))
    {
      int i, size;
      bfd *obfd = osect->objfile->obfd;
      asection *bsect = osect->the_bfd_section;

      size = bfd_get_section_size (osect->the_bfd_section);
      for (i = 0; i < cache_novlys; i++)
	if (cache_ovly_table[i][VMA] == bfd_section_vma (obfd, bsect)
	    && cache_ovly_table[i][LMA] == bfd_section_lma (obfd, bsect)
	    /* && cache_ovly_table[i][SIZE] == size */ )
	  { /* obj_section matches i'th entry in ovly_table */
	    osect->ovly_mapped = cache_ovly_table[i][MAPPED];
	    break;		/* finished with inner for loop: break out */
	  }
    }
}

/* Set the output sections and output offsets for section SECTP in
   ABFD.  The relocation code in BFD will read these offsets, so we
   need to be sure they're initialized.  We map each section to itself,
   with no offset; this means that SECTP->vma will be honored.  */

static void
symfile_dummy_outputs (bfd *abfd, asection *sectp, void *dummy)
{
  sectp->output_section = sectp;
  sectp->output_offset = 0;
}

/* Relocate the contents of a debug section SECTP in ABFD.  The
   contents are stored in BUF if it is non-NULL, or returned in a
   malloc'd buffer otherwise.

   For some platforms and debug info formats, shared libraries contain
   relocations against the debug sections (particularly for DWARF-2;
   one affected platform is PowerPC GNU/Linux, although it depends on
   the version of the linker in use).  Also, ELF object files naturally
   have unresolved relocations for their debug sections.  We need to apply
   the relocations in order to get the locations of symbols correct.  */

bfd_byte *
symfile_relocate_debug_section (bfd *abfd, asection *sectp, bfd_byte *buf)
{
  /* We're only interested in debugging sections with relocation
     information.  */
  if ((sectp->flags & SEC_RELOC) == 0)
    return NULL;
  if ((sectp->flags & SEC_DEBUGGING) == 0)
    return NULL;

  /* We will handle section offsets properly elsewhere, so relocate as if
     all sections begin at 0.  */
  bfd_map_over_sections (abfd, symfile_dummy_outputs, NULL);

  return bfd_simple_get_relocated_section_contents (abfd, sectp, buf, NULL);
}

void
_initialize_symfile (void)
{
  struct cmd_list_element *c;

  c = add_cmd ("symbol-file", class_files, symbol_file_command,
	       "Load symbol table from executable file FILE.\n\
The `file' command can also load symbol tables, as well as setting the file\n\
to execute.", &cmdlist);
  set_cmd_completer (c, filename_completer);

  c = add_cmd ("add-symbol-file", class_files, add_symbol_file_command,
	       "Usage: add-symbol-file FILE ADDR [-s <SECT> <SECT_ADDR> -s <SECT> <SECT_ADDR> ...]\n\
Load the symbols from FILE, assuming FILE has been dynamically loaded.\n\
ADDR is the starting address of the file's text.\n\
The optional arguments are section-name section-address pairs and\n\
should be specified if the data and bss segments are not contiguous\n\
with the text.  SECT is a section name to be loaded at SECT_ADDR.",
	       &cmdlist);
  set_cmd_completer (c, filename_completer);

  c = add_cmd ("add-shared-symbol-files", class_files,
	       add_shared_symbol_files_command,
   "Load the symbols from shared objects in the dynamic linker's link map.",
	       &cmdlist);
  c = add_alias_cmd ("assf", "add-shared-symbol-files", class_files, 1,
		     &cmdlist);

  c = add_cmd ("load", class_files, load_command,
	       "Dynamically load FILE into the running program, and record its symbols\n\
for access from GDB.", &cmdlist);
  set_cmd_completer (c, filename_completer);

  add_show_from_set
    (add_set_cmd ("symbol-reloading", class_support, var_boolean,
		  (char *) &symbol_reloading,
	    "Set dynamic symbol table reloading multiple times in one run.",
		  &setlist),
     &showlist);

  add_prefix_cmd ("overlay", class_support, overlay_command,
		  "Commands for debugging overlays.", &overlaylist,
		  "overlay ", 0, &cmdlist);

  add_com_alias ("ovly", "overlay", class_alias, 1);
  add_com_alias ("ov", "overlay", class_alias, 1);

  add_cmd ("map-overlay", class_support, map_overlay_command,
	   "Assert that an overlay section is mapped.", &overlaylist);

  add_cmd ("unmap-overlay", class_support, unmap_overlay_command,
	   "Assert that an overlay section is unmapped.", &overlaylist);

  add_cmd ("list-overlays", class_support, list_overlays_command,
	   "List mappings of overlay sections.", &overlaylist);

  add_cmd ("manual", class_support, overlay_manual_command,
	   "Enable overlay debugging.", &overlaylist);
  add_cmd ("off", class_support, overlay_off_command,
	   "Disable overlay debugging.", &overlaylist);
  add_cmd ("auto", class_support, overlay_auto_command,
	   "Enable automatic overlay debugging.", &overlaylist);
  add_cmd ("load-target", class_support, overlay_load_command,
	   "Read the overlay mapping state from the target.", &overlaylist);

  /* Filename extension to source language lookup table: */
  init_filename_language_table ();
  c = add_set_cmd ("extension-language", class_files, var_string_noescape,
		   (char *) &ext_args,
		   "Set mapping between filename extension and source language.\n\
Usage: set extension-language .foo bar",
		   &setlist);
  set_cmd_cfunc (c, set_ext_lang_command);

  add_info ("extensions", info_ext_lang_command,
	    "All filename extensions associated with a source language.");

  add_show_from_set
    (add_set_cmd ("download-write-size", class_obscure,
		  var_integer, (char *) &download_write_size,
		  "Set the write size used when downloading a program.\n"
		  "Only used when downloading a program onto a remote\n"
		  "target. Specify zero, or a negative value, to disable\n"
		  "blocked writes. The actual size of each transfer is also\n"
		  "limited by the size of the target packet and the memory\n"
		  "cache.\n",
		  &setlist),
     &showlist);

  debug_file_directory = xstrdup (DEBUGDIR);
  c = (add_set_cmd
       ("debug-file-directory", class_support, var_string,
        (char *) &debug_file_directory,
        "Set the directory where separate debug symbols are searched for.\n"
        "Separate debug symbols are first searched for in the same\n"
        "directory as the binary, then in the `" DEBUG_SUBDIRECTORY 
        "' subdirectory,\n"
        "and lastly at the path of the directory of the binary with\n"
        "the global debug-file directory prepended\n",
        &setlist));
  add_show_from_set (c, &showlist);
  set_cmd_completer (c, filename_completer);
}
