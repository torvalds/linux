/* BFD back-end for linux flavored sparc a.out binaries.
   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1999, 2000, 2001, 2002,
   2003, 2004, 2006, 2007 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */

#define TARGET_PAGE_SIZE	4096
#define ZMAGIC_DISK_BLOCK_SIZE	1024
#define SEGMENT_SIZE		TARGET_PAGE_SIZE
#define TEXT_START_ADDR		0x0
#define N_SHARED_LIB(x)		0

#define MACHTYPE_OK(mtype) ((mtype) == M_SPARC || (mtype) == M_UNKNOWN)

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "aout/aout64.h"
#include "aout/stab_gnu.h"
#include "aout/ar.h"
#include "libaout.h"           /* BFD a.out internal data structures */

#define DEFAULT_ARCH bfd_arch_sparc
/* Do not "beautify" the CONCAT* macro args.  Traditional C will not
   remove whitespace added here, and thus will fail to concatenate
   the tokens.  */
#define MY(OP) CONCAT2 (sparclinux_,OP)
#define TARGETNAME "a.out-sparc-linux"

extern const bfd_target MY(vec);

/* We always generate QMAGIC files in preference to ZMAGIC files.  It
   would be possible to make this a linker option, if that ever
   becomes important.  */

static void MY_final_link_callback
  PARAMS ((bfd *, file_ptr *, file_ptr *, file_ptr *));

static bfd_boolean sparclinux_bfd_final_link
  PARAMS ((bfd *abfd, struct bfd_link_info *info));

static bfd_boolean
sparclinux_bfd_final_link (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  obj_aout_subformat (abfd) = q_magic_format;
  return NAME(aout,final_link) (abfd, info, MY_final_link_callback);
}

#define MY_bfd_final_link sparclinux_bfd_final_link

/* Set the machine type correctly.  */

static bfd_boolean sparclinux_write_object_contents PARAMS ((bfd *abfd));

static bfd_boolean
sparclinux_write_object_contents (abfd)
     bfd *abfd;
{
  struct external_exec exec_bytes;
  struct internal_exec *execp = exec_hdr (abfd);

  N_SET_MACHTYPE (*execp, M_SPARC);

  obj_reloc_entry_size (abfd) = RELOC_STD_SIZE;

  WRITE_HEADERS(abfd, execp);

  return TRUE;
}

#define MY_write_object_contents sparclinux_write_object_contents
/* Code to link against Linux a.out shared libraries.  */

/* See if a symbol name is a reference to the global offset table.  */

#ifndef GOT_REF_PREFIX
#define GOT_REF_PREFIX  "__GOT_"
#endif

#define IS_GOT_SYM(name)  (CONST_STRNEQ (name, GOT_REF_PREFIX))

/* See if a symbol name is a reference to the procedure linkage table.  */

#ifndef PLT_REF_PREFIX
#define PLT_REF_PREFIX  "__PLT_"
#endif

#define IS_PLT_SYM(name)  (CONST_STRNEQ (name, PLT_REF_PREFIX))

/* This string is used to generate specialized error messages.  */

#ifndef NEEDS_SHRLIB
#define NEEDS_SHRLIB "__NEEDS_SHRLIB_"
#endif

/* This special symbol is a set vector that contains a list of
   pointers to fixup tables.  It will be present in any dynamically
   linked file.  The linker generated fixup table should also be added
   to the list, and it should always appear in the second slot (the
   first one is a dummy with a magic number that is defined in
   crt0.o).  */

#ifndef SHARABLE_CONFLICTS
#define SHARABLE_CONFLICTS "__SHARABLE_CONFLICTS__"
#endif

/* We keep a list of fixups.  The terminology is a bit strange, but
   each fixup contains two 32 bit numbers.  A regular fixup contains
   an address and a pointer, and at runtime we should store the
   address at the location pointed to by the pointer.  A builtin fixup
   contains two pointers, and we should read the address using one
   pointer and store it at the location pointed to by the other
   pointer.  Builtin fixups come into play when we have duplicate
   __GOT__ symbols for the same variable.  The builtin fixup will copy
   the GOT pointer from one over into the other.  */

struct fixup
{
  struct fixup *next;
  struct linux_link_hash_entry *h;
  bfd_vma value;

  /* Nonzero if this is a jump instruction that needs to be fixed,
     zero if this is just a pointer */
  char jump;

  char builtin;
};

/* We don't need a special hash table entry structure, but we do need
   to keep some information between linker passes, so we use a special
   hash table.  */

struct linux_link_hash_entry
{
  struct aout_link_hash_entry root;
};

struct linux_link_hash_table
{
  struct aout_link_hash_table root;

  /* First dynamic object found in link.  */
  bfd *dynobj;

  /* Number of fixups.  */
  size_t fixup_count;

  /* Number of builtin fixups.  */
  size_t local_builtins;

  /* List of fixups.  */
  struct fixup *fixup_list;
};

static struct bfd_hash_entry *linux_link_hash_newfunc
  PARAMS ((struct bfd_hash_entry *, struct bfd_hash_table *, const char *));
static struct bfd_link_hash_table *linux_link_hash_table_create
  PARAMS ((bfd *));
static struct fixup *new_fixup
  PARAMS ((struct bfd_link_info *, struct linux_link_hash_entry *,
	  bfd_vma, int));
static bfd_boolean linux_link_create_dynamic_sections
  PARAMS ((bfd *, struct bfd_link_info *));
static bfd_boolean linux_add_one_symbol
  PARAMS ((struct bfd_link_info *, bfd *, const char *, flagword, asection *,
	  bfd_vma, const char *, bfd_boolean, bfd_boolean,
	  struct bfd_link_hash_entry **));
static bfd_boolean linux_tally_symbols
  PARAMS ((struct linux_link_hash_entry *, PTR));
static bfd_boolean linux_finish_dynamic_link
  PARAMS ((bfd *, struct bfd_link_info *));

/* Routine to create an entry in an Linux link hash table.  */

static struct bfd_hash_entry *
linux_link_hash_newfunc (entry, table, string)
     struct bfd_hash_entry *entry;
     struct bfd_hash_table *table;
     const char *string;
{
  struct linux_link_hash_entry *ret = (struct linux_link_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == (struct linux_link_hash_entry *) NULL)
    ret = ((struct linux_link_hash_entry *)
	   bfd_hash_allocate (table, sizeof (struct linux_link_hash_entry)));
  if (ret == NULL)
    return (struct bfd_hash_entry *) ret;

  /* Call the allocation method of the superclass.  */
  ret = ((struct linux_link_hash_entry *)
	 NAME(aout,link_hash_newfunc) ((struct bfd_hash_entry *) ret,
				       table, string));
  if (ret != NULL)
    {
      /* Set local fields; there aren't any.  */
    }

  return (struct bfd_hash_entry *) ret;
}

/* Create a Linux link hash table.  */

static struct bfd_link_hash_table *
linux_link_hash_table_create (abfd)
     bfd *abfd;
{
  struct linux_link_hash_table *ret;
  bfd_size_type amt = sizeof (struct linux_link_hash_table);

  ret = (struct linux_link_hash_table *) bfd_malloc (amt);
  if (ret == (struct linux_link_hash_table *) NULL)
    return (struct bfd_link_hash_table *) NULL;
  if (!NAME(aout,link_hash_table_init) (&ret->root, abfd,
					linux_link_hash_newfunc,
					sizeof (struct linux_link_hash_entry)))
    {
      free (ret);
      return (struct bfd_link_hash_table *) NULL;
    }

  ret->dynobj = NULL;
  ret->fixup_count = 0;
  ret->local_builtins = 0;
  ret->fixup_list = NULL;

  return &ret->root.root;
}

/* Look up an entry in a Linux link hash table.  */

#define linux_link_hash_lookup(table, string, create, copy, follow) \
  ((struct linux_link_hash_entry *) \
   aout_link_hash_lookup (&(table)->root, (string), (create), (copy),\
			  (follow)))

/* Traverse a Linux link hash table.  */

#define linux_link_hash_traverse(table, func, info)		       \
  (aout_link_hash_traverse					       \
   (&(table)->root,						       \
    (bfd_boolean (*) PARAMS ((struct aout_link_hash_entry *, PTR))) (func), \
    (info)))

/* Get the Linux link hash table from the info structure.  This is
   just a cast.  */

#define linux_hash_table(p) ((struct linux_link_hash_table *) ((p)->hash))

/* Store the information for a new fixup.  */

static struct fixup *
new_fixup (info, h, value, builtin)
     struct bfd_link_info *info;
     struct linux_link_hash_entry *h;
     bfd_vma value;
     int builtin;
{
  struct fixup *f;

  f = (struct fixup *) bfd_hash_allocate (&info->hash->table,
					  sizeof (struct fixup));
  if (f == NULL)
    return f;
  f->next = linux_hash_table (info)->fixup_list;
  linux_hash_table (info)->fixup_list = f;
  f->h = h;
  f->value = value;
  f->builtin = builtin;
  f->jump = 0;
  ++linux_hash_table (info)->fixup_count;
  return f;
}

/* We come here once we realize that we are going to link to a shared
   library.  We need to create a special section that contains the
   fixup table, and we ultimately need to add a pointer to this into
   the set vector for SHARABLE_CONFLICTS.  At this point we do not
   know the size of the section, but that's OK - we just need to
   create it for now.  */

static bfd_boolean
linux_link_create_dynamic_sections (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info ATTRIBUTE_UNUSED;
{
  flagword flags;
  register asection *s;

  /* Note that we set the SEC_IN_MEMORY flag.  */
  flags = SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY;

  /* We choose to use the name ".linux-dynamic" for the fixup table.
     Why not?  */
  s = bfd_make_section_with_flags (abfd, ".linux-dynamic", flags);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, 2))
    return FALSE;
  s->size = 0;
  s->contents = 0;

  return TRUE;
}

/* Function to add a single symbol to the linker hash table.  This is
   a wrapper around _bfd_generic_link_add_one_symbol which handles the
   tweaking needed for dynamic linking support.  */

static bfd_boolean
linux_add_one_symbol (info, abfd, name, flags, section, value, string,
		      copy, collect, hashp)
     struct bfd_link_info *info;
     bfd *abfd;
     const char *name;
     flagword flags;
     asection *section;
     bfd_vma value;
     const char *string;
     bfd_boolean copy;
     bfd_boolean collect;
     struct bfd_link_hash_entry **hashp;
{
  struct linux_link_hash_entry *h;
  bfd_boolean insert;

  /* Look up and see if we already have this symbol in the hash table.
     If we do, and the defining entry is from a shared library, we
     need to create the dynamic sections.

     FIXME: What if abfd->xvec != info->hash->creator?  We may want to
     be able to link Linux a.out and ELF objects together, but serious
     confusion is possible.  */

  insert = FALSE;

  if (! info->relocatable
      && linux_hash_table (info)->dynobj == NULL
      && strcmp (name, SHARABLE_CONFLICTS) == 0
      && (flags & BSF_CONSTRUCTOR) != 0
      && abfd->xvec == info->hash->creator)
    {
      if (! linux_link_create_dynamic_sections (abfd, info))
	return FALSE;
      linux_hash_table (info)->dynobj = abfd;
      insert = TRUE;
    }

  if (bfd_is_abs_section (section)
      && abfd->xvec == info->hash->creator)
    {
      h = linux_link_hash_lookup (linux_hash_table (info), name, FALSE,
				  FALSE, FALSE);
      if (h != NULL
	  && (h->root.root.type == bfd_link_hash_defined
	      || h->root.root.type == bfd_link_hash_defweak))
	{
	  struct fixup *f;

	  if (hashp != NULL)
	    *hashp = (struct bfd_link_hash_entry *) h;

	  f = new_fixup (info, h, value, ! IS_PLT_SYM (name));
	  if (f == NULL)
	    return FALSE;
	  f->jump = IS_PLT_SYM (name);

	  return TRUE;
	}
    }

  /* Do the usual procedure for adding a symbol.  */
  if (! _bfd_generic_link_add_one_symbol (info, abfd, name, flags, section,
					  value, string, copy, collect,
					  hashp))
    return FALSE;

  /* Insert a pointer to our table in the set vector.  The dynamic
     linker requires this information.  */
  if (insert)
    {
      asection *s;

      /* Here we do our special thing to add the pointer to the
	 dynamic section in the SHARABLE_CONFLICTS set vector.  */
      s = bfd_get_section_by_name (linux_hash_table (info)->dynobj,
				   ".linux-dynamic");
      BFD_ASSERT (s != NULL);

      if (! (_bfd_generic_link_add_one_symbol
	     (info, linux_hash_table (info)->dynobj, SHARABLE_CONFLICTS,
	      BSF_GLOBAL | BSF_CONSTRUCTOR, s, (bfd_vma) 0, NULL,
	      FALSE, FALSE, NULL)))
	return FALSE;
    }

  return TRUE;
}

/* We will crawl the hash table and come here for every global symbol.
   We will examine each entry and see if there are indications that we
   need to add a fixup.  There are two possible cases - one is where
   you have duplicate definitions of PLT or GOT symbols - these will
   have already been caught and added as "builtin" fixups.  If we find
   that the corresponding non PLT/GOT symbol is also present, we
   convert it to a regular fixup instead.

   This function is called via linux_link_hash_traverse.  */

static bfd_boolean
linux_tally_symbols (struct linux_link_hash_entry *h, void * data)
{
  struct bfd_link_info *info = (struct bfd_link_info *) data;
  struct fixup *f, *f1;
  int is_plt;
  struct linux_link_hash_entry *h1, *h2;
  bfd_boolean exists;

  if (h->root.root.type == bfd_link_hash_warning)
    h = (struct linux_link_hash_entry *) h->root.root.u.i.link;

  if (h->root.root.type == bfd_link_hash_undefined
      && CONST_STRNEQ (h->root.root.root.string, NEEDS_SHRLIB))
    {
      const char *name;
      char *p;
      char *alloc = NULL;

      name = h->root.root.root.string + sizeof NEEDS_SHRLIB - 1;
      p = strrchr (name, '_');
      if (p != NULL)
	alloc = (char *) bfd_malloc ((bfd_size_type) strlen (name) + 1);

      if (p == NULL || alloc == NULL)
	(*_bfd_error_handler) (_("Output file requires shared library `%s'\n"),
			       name);
      else
	{
	  strcpy (alloc, name);
	  p = strrchr (alloc, '_');
	  *p++ = '\0';
	  (*_bfd_error_handler)
	    (_("Output file requires shared library `%s.so.%s'\n"),
	     alloc, p);
	  free (alloc);
	}

      abort ();
    }

  /* If this symbol is not a PLT/GOT, we do not even need to look at
     it.  */
  is_plt = IS_PLT_SYM (h->root.root.root.string);

  if (is_plt || IS_GOT_SYM (h->root.root.root.string))
    {
      /* Look up this symbol twice.  Once just as a regular lookup,
	 and then again following all of the indirect links until we
	 reach a real symbol.  */
      h1 = linux_link_hash_lookup (linux_hash_table (info),
				   (h->root.root.root.string
				    + sizeof PLT_REF_PREFIX - 1),
				   FALSE, FALSE, TRUE);
      /* h2 does not follow indirect symbols.  */
      h2 = linux_link_hash_lookup (linux_hash_table (info),
				   (h->root.root.root.string
				    + sizeof PLT_REF_PREFIX - 1),
				   FALSE, FALSE, FALSE);

      /* The real symbol must exist but if it is also an ABS symbol,
	 there is no need to have a fixup.  This is because they both
	 came from the same library.  If on the other hand, we had to
	 use an indirect symbol to get to the real symbol, we add the
	 fixup anyway, since there are cases where these symbols come
	 from different shared libraries */
      if (h1 != NULL
	  && (((h1->root.root.type == bfd_link_hash_defined
		|| h1->root.root.type == bfd_link_hash_defweak)
	       && ! bfd_is_abs_section (h1->root.root.u.def.section))
	      || h2->root.root.type == bfd_link_hash_indirect))
	{
	  /* See if there is a "builtin" fixup already present
	     involving this symbol.  If so, convert it to a regular
	     fixup.  In the end, this relaxes some of the requirements
	     about the order of performing fixups.  */
	  exists = FALSE;
	  for (f1 = linux_hash_table (info)->fixup_list;
	       f1 != NULL;
	       f1 = f1->next)
	    {
	      if ((f1->h != h && f1->h != h1)
		  || (! f1->builtin && ! f1->jump))
		continue;
	      if (f1->h == h1)
		exists = TRUE;
	      if (! exists
		  && bfd_is_abs_section (h->root.root.u.def.section))
		{
		  f = new_fixup (info, h1, f1->h->root.root.u.def.value, 0);
		  f->jump = is_plt;
		}
	      f1->h = h1;
	      f1->jump = is_plt;
	      f1->builtin = 0;
	      exists = TRUE;
	    }
	  if (! exists
	      && bfd_is_abs_section (h->root.root.u.def.section))
	    {
	      f = new_fixup (info, h1, h->root.root.u.def.value, 0);
	      if (f == NULL)
		{
		  /* FIXME: No way to return error.  */
		  abort ();
		}
	      f->jump = is_plt;
	    }
	}

      /* Quick and dirty way of stripping these symbols from the
	 symtab.  */
      if (bfd_is_abs_section (h->root.root.u.def.section))
	h->root.written = TRUE;
    }

  return TRUE;
}

/* This is called to set the size of the .linux-dynamic section is.
   It is called by the Linux linker emulation before_allocation
   routine.  We have finished reading all of the input files, and now
   we just scan the hash tables to find out how many additional fixups
   are required.  */

bfd_boolean
bfd_sparclinux_size_dynamic_sections (output_bfd, info)
     bfd *output_bfd;
     struct bfd_link_info *info;
{
  struct fixup *f;
  asection *s;

  if (output_bfd->xvec != &MY(vec))
    return TRUE;

  /* First find the fixups...  */
  linux_link_hash_traverse (linux_hash_table (info),
			    linux_tally_symbols,
			    (PTR) info);

  /* If there are builtin fixups, leave room for a marker.  This is
     used by the dynamic linker so that it knows that all that follow
     are builtin fixups instead of regular fixups.  */
  for (f = linux_hash_table (info)->fixup_list; f != NULL; f = f->next)
    {
      if (f->builtin)
	{
	  ++linux_hash_table (info)->fixup_count;
	  ++linux_hash_table (info)->local_builtins;
	  break;
	}
    }

  if (linux_hash_table (info)->dynobj == NULL)
    {
      if (linux_hash_table (info)->fixup_count > 0)
	abort ();
      return TRUE;
    }

  /* Allocate memory for our fixup table.  We will fill it in later.  */
  s = bfd_get_section_by_name (linux_hash_table (info)->dynobj,
			       ".linux-dynamic");
  if (s != NULL)
    {
      s->size = linux_hash_table (info)->fixup_count + 1;
      s->size *= 8;
      s->contents = (bfd_byte *) bfd_zalloc (output_bfd, s->size);
      if (s->contents == NULL)
	return FALSE;
    }

  return TRUE;
}

/* We come here once we are ready to actually write the fixup table to
   the output file.  Scan the fixup tables and so forth and generate
   the stuff we need.  */

static bfd_boolean
linux_finish_dynamic_link (output_bfd, info)
     bfd *output_bfd;
     struct bfd_link_info *info;
{
  asection *s, *os, *is;
  bfd_byte *fixup_table;
  struct linux_link_hash_entry *h;
  struct fixup *f;
  unsigned int new_addr;
  int section_offset;
  unsigned int fixups_written;

  if (linux_hash_table (info)->dynobj == NULL)
    return TRUE;

  s = bfd_get_section_by_name (linux_hash_table (info)->dynobj,
			       ".linux-dynamic");
  BFD_ASSERT (s != NULL);
  os = s->output_section;
  fixups_written = 0;

#ifdef LINUX_LINK_DEBUG
  printf ("Fixup table file offset: %x  VMA: %x\n",
	  os->filepos + s->output_offset,
	  os->vma + s->output_offset);
#endif

  fixup_table = s->contents;
  bfd_put_32 (output_bfd,
	      (bfd_vma) linux_hash_table (info)->fixup_count, fixup_table);
  fixup_table += 4;

  /* Fill in fixup table.  */
  for (f = linux_hash_table (info)->fixup_list; f != NULL; f = f->next)
    {
      if (f->builtin)
	continue;

      if (f->h->root.root.type != bfd_link_hash_defined
	  && f->h->root.root.type != bfd_link_hash_defweak)
	{
	  (*_bfd_error_handler)
	    (_("Symbol %s not defined for fixups\n"),
	     f->h->root.root.root.string);
	  continue;
	}

      is = f->h->root.root.u.def.section;
      section_offset = is->output_section->vma + is->output_offset;
      new_addr = f->h->root.root.u.def.value + section_offset;

#ifdef LINUX_LINK_DEBUG
      printf ("Fixup(%d) %s: %x %x\n",f->jump, f->h->root.root.string,
	      new_addr, f->value);
#endif

      if (f->jump)
	{
	  /* Relative address */
	  new_addr = new_addr - (f->value + 5);
	  bfd_put_32 (output_bfd, (bfd_vma) new_addr, fixup_table);
	  fixup_table += 4;
	  bfd_put_32 (output_bfd, f->value + 1, fixup_table);
	  fixup_table += 4;
	}
      else
	{
	  bfd_put_32 (output_bfd, (bfd_vma) new_addr, fixup_table);
	  fixup_table += 4;
	  bfd_put_32 (output_bfd, f->value, fixup_table);
	  fixup_table += 4;
	}
      ++fixups_written;
    }

  if (linux_hash_table (info)->local_builtins != 0)
    {
      /* Special marker so we know to switch to the other type of fixup */
      bfd_put_32 (output_bfd, (bfd_vma) 0, fixup_table);
      fixup_table += 4;
      bfd_put_32 (output_bfd, (bfd_vma) 0, fixup_table);
      fixup_table += 4;
      ++fixups_written;
      for (f = linux_hash_table (info)->fixup_list; f != NULL; f = f->next)
	{
	  if (! f->builtin)
	    continue;

	  if (f->h->root.root.type != bfd_link_hash_defined
	      && f->h->root.root.type != bfd_link_hash_defweak)
	    {
	      (*_bfd_error_handler)
		(_("Symbol %s not defined for fixups\n"),
		 f->h->root.root.root.string);
	      continue;
	    }

	  is = f->h->root.root.u.def.section;
	  section_offset = is->output_section->vma + is->output_offset;
	  new_addr = f->h->root.root.u.def.value + section_offset;

#ifdef LINUX_LINK_DEBUG
	  printf ("Fixup(B) %s: %x %x\n", f->h->root.root.string,
		  new_addr, f->value);
#endif

	  bfd_put_32 (output_bfd, (bfd_vma) new_addr, fixup_table);
	  fixup_table += 4;
	  bfd_put_32 (output_bfd, f->value, fixup_table);
	  fixup_table += 4;
	  ++fixups_written;
	}
    }

  if (linux_hash_table (info)->fixup_count != fixups_written)
    {
      (*_bfd_error_handler) (_("Warning: fixup count mismatch\n"));
      while (linux_hash_table (info)->fixup_count > fixups_written)
	{
	  bfd_put_32 (output_bfd, (bfd_vma) 0, fixup_table);
	  fixup_table += 4;
	  bfd_put_32 (output_bfd, (bfd_vma) 0, fixup_table);
	  fixup_table += 4;
	  ++fixups_written;
	}
    }

  h = linux_link_hash_lookup (linux_hash_table (info),
			      "__BUILTIN_FIXUPS__",
			      FALSE, FALSE, FALSE);

  if (h != NULL
      && (h->root.root.type == bfd_link_hash_defined
	  || h->root.root.type == bfd_link_hash_defweak))
    {
      is = h->root.root.u.def.section;
      section_offset = is->output_section->vma + is->output_offset;
      new_addr = h->root.root.u.def.value + section_offset;

#ifdef LINUX_LINK_DEBUG
      printf ("Builtin fixup table at %x\n", new_addr);
#endif

      bfd_put_32 (output_bfd, (bfd_vma) new_addr, fixup_table);
    }
  else
    bfd_put_32 (output_bfd, (bfd_vma) 0, fixup_table);

  if (bfd_seek (output_bfd, (file_ptr) (os->filepos + s->output_offset),
		SEEK_SET) != 0)
    return FALSE;

  if (bfd_bwrite ((PTR) s->contents, s->size, output_bfd) != s->size)
    return FALSE;

  return TRUE;
}

#define MY_bfd_link_hash_table_create linux_link_hash_table_create
#define MY_add_one_symbol linux_add_one_symbol
#define MY_finish_dynamic_link linux_finish_dynamic_link

#define MY_zmagic_contiguous 1

#include "aout-target.h"
