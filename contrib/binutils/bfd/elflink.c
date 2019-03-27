/* ELF linking support for BFD.
   Copyright 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "libbfd.h"
#define ARCH_SIZE 0
#include "elf-bfd.h"
#include "safe-ctype.h"
#include "libiberty.h"
#include "objalloc.h"

/* Define a symbol in a dynamic linkage section.  */

struct elf_link_hash_entry *
_bfd_elf_define_linkage_sym (bfd *abfd,
			     struct bfd_link_info *info,
			     asection *sec,
			     const char *name)
{
  struct elf_link_hash_entry *h;
  struct bfd_link_hash_entry *bh;
  const struct elf_backend_data *bed;

  h = elf_link_hash_lookup (elf_hash_table (info), name, FALSE, FALSE, FALSE);
  if (h != NULL)
    {
      /* Zap symbol defined in an as-needed lib that wasn't linked.
	 This is a symptom of a larger problem:  Absolute symbols
	 defined in shared libraries can't be overridden, because we
	 lose the link to the bfd which is via the symbol section.  */
      h->root.type = bfd_link_hash_new;
    }

  bh = &h->root;
  if (!_bfd_generic_link_add_one_symbol (info, abfd, name, BSF_GLOBAL,
					 sec, 0, NULL, FALSE,
					 get_elf_backend_data (abfd)->collect,
					 &bh))
    return NULL;
  h = (struct elf_link_hash_entry *) bh;
  h->def_regular = 1;
  h->type = STT_OBJECT;
  h->other = (h->other & ~ELF_ST_VISIBILITY (-1)) | STV_HIDDEN;

  bed = get_elf_backend_data (abfd);
  (*bed->elf_backend_hide_symbol) (info, h, TRUE);
  return h;
}

bfd_boolean
_bfd_elf_create_got_section (bfd *abfd, struct bfd_link_info *info)
{
  flagword flags;
  asection *s;
  struct elf_link_hash_entry *h;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  int ptralign;

  /* This function may be called more than once.  */
  s = bfd_get_section_by_name (abfd, ".got");
  if (s != NULL && (s->flags & SEC_LINKER_CREATED) != 0)
    return TRUE;

  switch (bed->s->arch_size)
    {
    case 32:
      ptralign = 2;
      break;

    case 64:
      ptralign = 3;
      break;

    default:
      bfd_set_error (bfd_error_bad_value);
      return FALSE;
    }

  flags = bed->dynamic_sec_flags;

  s = bfd_make_section_with_flags (abfd, ".got", flags);
  if (s == NULL
      || !bfd_set_section_alignment (abfd, s, ptralign))
    return FALSE;

  if (bed->want_got_plt)
    {
      s = bfd_make_section_with_flags (abfd, ".got.plt", flags);
      if (s == NULL
	  || !bfd_set_section_alignment (abfd, s, ptralign))
	return FALSE;
    }

  if (bed->want_got_sym)
    {
      /* Define the symbol _GLOBAL_OFFSET_TABLE_ at the start of the .got
	 (or .got.plt) section.  We don't do this in the linker script
	 because we don't want to define the symbol if we are not creating
	 a global offset table.  */
      h = _bfd_elf_define_linkage_sym (abfd, info, s, "_GLOBAL_OFFSET_TABLE_");
      elf_hash_table (info)->hgot = h;
      if (h == NULL)
	return FALSE;
    }

  /* The first bit of the global offset table is the header.  */
  s->size += bed->got_header_size;

  return TRUE;
}

/* Create a strtab to hold the dynamic symbol names.  */
static bfd_boolean
_bfd_elf_link_create_dynstrtab (bfd *abfd, struct bfd_link_info *info)
{
  struct elf_link_hash_table *hash_table;

  hash_table = elf_hash_table (info);
  if (hash_table->dynobj == NULL)
    hash_table->dynobj = abfd;

  if (hash_table->dynstr == NULL)
    {
      hash_table->dynstr = _bfd_elf_strtab_init ();
      if (hash_table->dynstr == NULL)
	return FALSE;
    }
  return TRUE;
}

/* Create some sections which will be filled in with dynamic linking
   information.  ABFD is an input file which requires dynamic sections
   to be created.  The dynamic sections take up virtual memory space
   when the final executable is run, so we need to create them before
   addresses are assigned to the output sections.  We work out the
   actual contents and size of these sections later.  */

bfd_boolean
_bfd_elf_link_create_dynamic_sections (bfd *abfd, struct bfd_link_info *info)
{
  flagword flags;
  register asection *s;
  const struct elf_backend_data *bed;

  if (! is_elf_hash_table (info->hash))
    return FALSE;

  if (elf_hash_table (info)->dynamic_sections_created)
    return TRUE;

  if (!_bfd_elf_link_create_dynstrtab (abfd, info))
    return FALSE;

  abfd = elf_hash_table (info)->dynobj;
  bed = get_elf_backend_data (abfd);

  flags = bed->dynamic_sec_flags;

  /* A dynamically linked executable has a .interp section, but a
     shared library does not.  */
  if (info->executable)
    {
      s = bfd_make_section_with_flags (abfd, ".interp",
				       flags | SEC_READONLY);
      if (s == NULL)
	return FALSE;
    }

  /* Create sections to hold version informations.  These are removed
     if they are not needed.  */
  s = bfd_make_section_with_flags (abfd, ".gnu.version_d",
				   flags | SEC_READONLY);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
    return FALSE;

  s = bfd_make_section_with_flags (abfd, ".gnu.version",
				   flags | SEC_READONLY);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, 1))
    return FALSE;

  s = bfd_make_section_with_flags (abfd, ".gnu.version_r",
				   flags | SEC_READONLY);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
    return FALSE;

  s = bfd_make_section_with_flags (abfd, ".dynsym",
				   flags | SEC_READONLY);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
    return FALSE;

  s = bfd_make_section_with_flags (abfd, ".dynstr",
				   flags | SEC_READONLY);
  if (s == NULL)
    return FALSE;

  s = bfd_make_section_with_flags (abfd, ".dynamic", flags);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
    return FALSE;

  /* The special symbol _DYNAMIC is always set to the start of the
     .dynamic section.  We could set _DYNAMIC in a linker script, but we
     only want to define it if we are, in fact, creating a .dynamic
     section.  We don't want to define it if there is no .dynamic
     section, since on some ELF platforms the start up code examines it
     to decide how to initialize the process.  */
  if (!_bfd_elf_define_linkage_sym (abfd, info, s, "_DYNAMIC"))
    return FALSE;

  if (info->emit_hash)
    {
      s = bfd_make_section_with_flags (abfd, ".hash", flags | SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
	return FALSE;
      elf_section_data (s)->this_hdr.sh_entsize = bed->s->sizeof_hash_entry;
    }

  if (info->emit_gnu_hash)
    {
      s = bfd_make_section_with_flags (abfd, ".gnu.hash",
				       flags | SEC_READONLY);
      if (s == NULL
	  || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
	return FALSE;
      /* For 64-bit ELF, .gnu.hash is a non-uniform entity size section:
	 4 32-bit words followed by variable count of 64-bit words, then
	 variable count of 32-bit words.  */
      if (bed->s->arch_size == 64)
	elf_section_data (s)->this_hdr.sh_entsize = 0;
      else
	elf_section_data (s)->this_hdr.sh_entsize = 4;
    }

  /* Let the backend create the rest of the sections.  This lets the
     backend set the right flags.  The backend will normally create
     the .got and .plt sections.  */
  if (! (*bed->elf_backend_create_dynamic_sections) (abfd, info))
    return FALSE;

  elf_hash_table (info)->dynamic_sections_created = TRUE;

  return TRUE;
}

/* Create dynamic sections when linking against a dynamic object.  */

bfd_boolean
_bfd_elf_create_dynamic_sections (bfd *abfd, struct bfd_link_info *info)
{
  flagword flags, pltflags;
  struct elf_link_hash_entry *h;
  asection *s;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  /* We need to create .plt, .rel[a].plt, .got, .got.plt, .dynbss, and
     .rel[a].bss sections.  */
  flags = bed->dynamic_sec_flags;

  pltflags = flags;
  if (bed->plt_not_loaded)
    /* We do not clear SEC_ALLOC here because we still want the OS to
       allocate space for the section; it's just that there's nothing
       to read in from the object file.  */
    pltflags &= ~ (SEC_CODE | SEC_LOAD | SEC_HAS_CONTENTS);
  else
    pltflags |= SEC_ALLOC | SEC_CODE | SEC_LOAD;
  if (bed->plt_readonly)
    pltflags |= SEC_READONLY;

  s = bfd_make_section_with_flags (abfd, ".plt", pltflags);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, bed->plt_alignment))
    return FALSE;

  /* Define the symbol _PROCEDURE_LINKAGE_TABLE_ at the start of the
     .plt section.  */
  if (bed->want_plt_sym)
    {
      h = _bfd_elf_define_linkage_sym (abfd, info, s,
				       "_PROCEDURE_LINKAGE_TABLE_");
      elf_hash_table (info)->hplt = h;
      if (h == NULL)
	return FALSE;
    }

  s = bfd_make_section_with_flags (abfd,
				   (bed->default_use_rela_p
				    ? ".rela.plt" : ".rel.plt"),
				   flags | SEC_READONLY);
  if (s == NULL
      || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
    return FALSE;

  if (! _bfd_elf_create_got_section (abfd, info))
    return FALSE;

  if (bed->want_dynbss)
    {
      /* The .dynbss section is a place to put symbols which are defined
	 by dynamic objects, are referenced by regular objects, and are
	 not functions.  We must allocate space for them in the process
	 image and use a R_*_COPY reloc to tell the dynamic linker to
	 initialize them at run time.  The linker script puts the .dynbss
	 section into the .bss section of the final image.  */
      s = bfd_make_section_with_flags (abfd, ".dynbss",
				       (SEC_ALLOC
					| SEC_LINKER_CREATED));
      if (s == NULL)
	return FALSE;

      /* The .rel[a].bss section holds copy relocs.  This section is not
	 normally needed.  We need to create it here, though, so that the
	 linker will map it to an output section.  We can't just create it
	 only if we need it, because we will not know whether we need it
	 until we have seen all the input files, and the first time the
	 main linker code calls BFD after examining all the input files
	 (size_dynamic_sections) the input sections have already been
	 mapped to the output sections.  If the section turns out not to
	 be needed, we can discard it later.  We will never need this
	 section when generating a shared object, since they do not use
	 copy relocs.  */
      if (! info->shared)
	{
	  s = bfd_make_section_with_flags (abfd,
					   (bed->default_use_rela_p
					    ? ".rela.bss" : ".rel.bss"),
					   flags | SEC_READONLY);
	  if (s == NULL
	      || ! bfd_set_section_alignment (abfd, s, bed->s->log_file_align))
	    return FALSE;
	}
    }

  return TRUE;
}

/* Record a new dynamic symbol.  We record the dynamic symbols as we
   read the input files, since we need to have a list of all of them
   before we can determine the final sizes of the output sections.
   Note that we may actually call this function even though we are not
   going to output any dynamic symbols; in some cases we know that a
   symbol should be in the dynamic symbol table, but only if there is
   one.  */

bfd_boolean
bfd_elf_link_record_dynamic_symbol (struct bfd_link_info *info,
				    struct elf_link_hash_entry *h)
{
  if (h->dynindx == -1)
    {
      struct elf_strtab_hash *dynstr;
      char *p;
      const char *name;
      bfd_size_type indx;

      /* XXX: The ABI draft says the linker must turn hidden and
	 internal symbols into STB_LOCAL symbols when producing the
	 DSO. However, if ld.so honors st_other in the dynamic table,
	 this would not be necessary.  */
      switch (ELF_ST_VISIBILITY (h->other))
	{
	case STV_INTERNAL:
	case STV_HIDDEN:
	  if (h->root.type != bfd_link_hash_undefined
	      && h->root.type != bfd_link_hash_undefweak)
	    {
	      h->forced_local = 1;
	      if (!elf_hash_table (info)->is_relocatable_executable)
		return TRUE;
	    }

	default:
	  break;
	}

      h->dynindx = elf_hash_table (info)->dynsymcount;
      ++elf_hash_table (info)->dynsymcount;

      dynstr = elf_hash_table (info)->dynstr;
      if (dynstr == NULL)
	{
	  /* Create a strtab to hold the dynamic symbol names.  */
	  elf_hash_table (info)->dynstr = dynstr = _bfd_elf_strtab_init ();
	  if (dynstr == NULL)
	    return FALSE;
	}

      /* We don't put any version information in the dynamic string
	 table.  */
      name = h->root.root.string;
      p = strchr (name, ELF_VER_CHR);
      if (p != NULL)
	/* We know that the p points into writable memory.  In fact,
	   there are only a few symbols that have read-only names, being
	   those like _GLOBAL_OFFSET_TABLE_ that are created specially
	   by the backends.  Most symbols will have names pointing into
	   an ELF string table read from a file, or to objalloc memory.  */
	*p = 0;

      indx = _bfd_elf_strtab_add (dynstr, name, p != NULL);

      if (p != NULL)
	*p = ELF_VER_CHR;

      if (indx == (bfd_size_type) -1)
	return FALSE;
      h->dynstr_index = indx;
    }

  return TRUE;
}

/* Mark a symbol dynamic.  */

void
bfd_elf_link_mark_dynamic_symbol (struct bfd_link_info *info,
				  struct elf_link_hash_entry *h,
				  Elf_Internal_Sym *sym)
{
  struct bfd_elf_dynamic_list *d = info->dynamic_list;

  /* It may be called more than once on the same H.  */
  if(h->dynamic || info->relocatable)
    return;

  if ((info->dynamic_data
       && (h->type == STT_OBJECT
	   || (sym != NULL
	       && ELF_ST_TYPE (sym->st_info) == STT_OBJECT)))
      || (d != NULL 
	  && h->root.type == bfd_link_hash_new
	  && (*d->match) (&d->head, NULL, h->root.root.string)))
    h->dynamic = 1;
}

/* Record an assignment to a symbol made by a linker script.  We need
   this in case some dynamic object refers to this symbol.  */

bfd_boolean
bfd_elf_record_link_assignment (bfd *output_bfd,
				struct bfd_link_info *info,
				const char *name,
				bfd_boolean provide,
				bfd_boolean hidden)
{
  struct elf_link_hash_entry *h;
  struct elf_link_hash_table *htab;

  if (!is_elf_hash_table (info->hash))
    return TRUE;

  htab = elf_hash_table (info);
  h = elf_link_hash_lookup (htab, name, !provide, TRUE, FALSE);
  if (h == NULL)
    return provide;

  /* Since we're defining the symbol, don't let it seem to have not
     been defined.  record_dynamic_symbol and size_dynamic_sections
     may depend on this.  */
  if (h->root.type == bfd_link_hash_undefweak
      || h->root.type == bfd_link_hash_undefined)
    {
      h->root.type = bfd_link_hash_new;
      if (h->root.u.undef.next != NULL || htab->root.undefs_tail == &h->root)
	bfd_link_repair_undef_list (&htab->root);
    }
  else if (h->root.type == bfd_link_hash_new)
    {
      bfd_elf_link_mark_dynamic_symbol (info, h, NULL);
      h->non_elf = 0;
    }
  else if (h->root.type == bfd_link_hash_indirect)
    {
      const struct elf_backend_data *bed = get_elf_backend_data (output_bfd);
      struct elf_link_hash_entry *hv = h;
      do
	hv = (struct elf_link_hash_entry *) hv->root.u.i.link;
      while (hv->root.type == bfd_link_hash_indirect
	     || hv->root.type == bfd_link_hash_warning);
      h->root.type = bfd_link_hash_undefined;
      hv->root.type = bfd_link_hash_indirect;
      hv->root.u.i.link = (struct bfd_link_hash_entry *) h;
      (*bed->elf_backend_copy_indirect_symbol) (info, h, hv);
    }
  else if (h->root.type == bfd_link_hash_warning)
    {
      abort ();
    }

  /* If this symbol is being provided by the linker script, and it is
     currently defined by a dynamic object, but not by a regular
     object, then mark it as undefined so that the generic linker will
     force the correct value.  */
  if (provide
      && h->def_dynamic
      && !h->def_regular)
    h->root.type = bfd_link_hash_undefined;

  /* If this symbol is not being provided by the linker script, and it is
     currently defined by a dynamic object, but not by a regular object,
     then clear out any version information because the symbol will not be
     associated with the dynamic object any more.  */
  if (!provide
      && h->def_dynamic
      && !h->def_regular)
    h->verinfo.verdef = NULL;

  h->def_regular = 1;

  if (provide && hidden)
    {
      const struct elf_backend_data *bed = get_elf_backend_data (output_bfd);

      h->other = (h->other & ~ELF_ST_VISIBILITY (-1)) | STV_HIDDEN;
      (*bed->elf_backend_hide_symbol) (info, h, TRUE);
    }

  /* STV_HIDDEN and STV_INTERNAL symbols must be STB_LOCAL in shared objects
     and executables.  */
  if (!info->relocatable
      && h->dynindx != -1
      && (ELF_ST_VISIBILITY (h->other) == STV_HIDDEN
	  || ELF_ST_VISIBILITY (h->other) == STV_INTERNAL))
    h->forced_local = 1;

  if ((h->def_dynamic
       || h->ref_dynamic
       || info->shared
       || (info->executable && elf_hash_table (info)->is_relocatable_executable))
      && h->dynindx == -1)
    {
      if (! bfd_elf_link_record_dynamic_symbol (info, h))
	return FALSE;

      /* If this is a weak defined symbol, and we know a corresponding
	 real symbol from the same dynamic object, make sure the real
	 symbol is also made into a dynamic symbol.  */
      if (h->u.weakdef != NULL
	  && h->u.weakdef->dynindx == -1)
	{
	  if (! bfd_elf_link_record_dynamic_symbol (info, h->u.weakdef))
	    return FALSE;
	}
    }

  return TRUE;
}

/* Record a new local dynamic symbol.  Returns 0 on failure, 1 on
   success, and 2 on a failure caused by attempting to record a symbol
   in a discarded section, eg. a discarded link-once section symbol.  */

int
bfd_elf_link_record_local_dynamic_symbol (struct bfd_link_info *info,
					  bfd *input_bfd,
					  long input_indx)
{
  bfd_size_type amt;
  struct elf_link_local_dynamic_entry *entry;
  struct elf_link_hash_table *eht;
  struct elf_strtab_hash *dynstr;
  unsigned long dynstr_index;
  char *name;
  Elf_External_Sym_Shndx eshndx;
  char esym[sizeof (Elf64_External_Sym)];

  if (! is_elf_hash_table (info->hash))
    return 0;

  /* See if the entry exists already.  */
  for (entry = elf_hash_table (info)->dynlocal; entry ; entry = entry->next)
    if (entry->input_bfd == input_bfd && entry->input_indx == input_indx)
      return 1;

  amt = sizeof (*entry);
  entry = bfd_alloc (input_bfd, amt);
  if (entry == NULL)
    return 0;

  /* Go find the symbol, so that we can find it's name.  */
  if (!bfd_elf_get_elf_syms (input_bfd, &elf_tdata (input_bfd)->symtab_hdr,
			     1, input_indx, &entry->isym, esym, &eshndx))
    {
      bfd_release (input_bfd, entry);
      return 0;
    }

  if (entry->isym.st_shndx != SHN_UNDEF
      && (entry->isym.st_shndx < SHN_LORESERVE
	  || entry->isym.st_shndx > SHN_HIRESERVE))
    {
      asection *s;

      s = bfd_section_from_elf_index (input_bfd, entry->isym.st_shndx);
      if (s == NULL || bfd_is_abs_section (s->output_section))
	{
	  /* We can still bfd_release here as nothing has done another
	     bfd_alloc.  We can't do this later in this function.  */
	  bfd_release (input_bfd, entry);
	  return 2;
	}
    }

  name = (bfd_elf_string_from_elf_section
	  (input_bfd, elf_tdata (input_bfd)->symtab_hdr.sh_link,
	   entry->isym.st_name));

  dynstr = elf_hash_table (info)->dynstr;
  if (dynstr == NULL)
    {
      /* Create a strtab to hold the dynamic symbol names.  */
      elf_hash_table (info)->dynstr = dynstr = _bfd_elf_strtab_init ();
      if (dynstr == NULL)
	return 0;
    }

  dynstr_index = _bfd_elf_strtab_add (dynstr, name, FALSE);
  if (dynstr_index == (unsigned long) -1)
    return 0;
  entry->isym.st_name = dynstr_index;

  eht = elf_hash_table (info);

  entry->next = eht->dynlocal;
  eht->dynlocal = entry;
  entry->input_bfd = input_bfd;
  entry->input_indx = input_indx;
  eht->dynsymcount++;

  /* Whatever binding the symbol had before, it's now local.  */
  entry->isym.st_info
    = ELF_ST_INFO (STB_LOCAL, ELF_ST_TYPE (entry->isym.st_info));

  /* The dynindx will be set at the end of size_dynamic_sections.  */

  return 1;
}

/* Return the dynindex of a local dynamic symbol.  */

long
_bfd_elf_link_lookup_local_dynindx (struct bfd_link_info *info,
				    bfd *input_bfd,
				    long input_indx)
{
  struct elf_link_local_dynamic_entry *e;

  for (e = elf_hash_table (info)->dynlocal; e ; e = e->next)
    if (e->input_bfd == input_bfd && e->input_indx == input_indx)
      return e->dynindx;
  return -1;
}

/* This function is used to renumber the dynamic symbols, if some of
   them are removed because they are marked as local.  This is called
   via elf_link_hash_traverse.  */

static bfd_boolean
elf_link_renumber_hash_table_dynsyms (struct elf_link_hash_entry *h,
				      void *data)
{
  size_t *count = data;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (h->forced_local)
    return TRUE;

  if (h->dynindx != -1)
    h->dynindx = ++(*count);

  return TRUE;
}


/* Like elf_link_renumber_hash_table_dynsyms, but just number symbols with
   STB_LOCAL binding.  */

static bfd_boolean
elf_link_renumber_local_hash_table_dynsyms (struct elf_link_hash_entry *h,
					    void *data)
{
  size_t *count = data;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (!h->forced_local)
    return TRUE;

  if (h->dynindx != -1)
    h->dynindx = ++(*count);

  return TRUE;
}

/* Return true if the dynamic symbol for a given section should be
   omitted when creating a shared library.  */
bfd_boolean
_bfd_elf_link_omit_section_dynsym (bfd *output_bfd ATTRIBUTE_UNUSED,
				   struct bfd_link_info *info,
				   asection *p)
{
  struct elf_link_hash_table *htab;

  switch (elf_section_data (p)->this_hdr.sh_type)
    {
    case SHT_PROGBITS:
    case SHT_NOBITS:
      /* If sh_type is yet undecided, assume it could be
	 SHT_PROGBITS/SHT_NOBITS.  */
    case SHT_NULL:
      htab = elf_hash_table (info);
      if (p == htab->tls_sec)
	return FALSE;

      if (htab->text_index_section != NULL)
	return p != htab->text_index_section && p != htab->data_index_section;

      if (strcmp (p->name, ".got") == 0
	  || strcmp (p->name, ".got.plt") == 0
	  || strcmp (p->name, ".plt") == 0)
	{
	  asection *ip;

	  if (htab->dynobj != NULL
	      && (ip = bfd_get_section_by_name (htab->dynobj, p->name)) != NULL
	      && (ip->flags & SEC_LINKER_CREATED)
	      && ip->output_section == p)
	    return TRUE;
	}
      return FALSE;

      /* There shouldn't be section relative relocations
	 against any other section.  */
    default:
      return TRUE;
    }
}

/* Assign dynsym indices.  In a shared library we generate a section
   symbol for each output section, which come first.  Next come symbols
   which have been forced to local binding.  Then all of the back-end
   allocated local dynamic syms, followed by the rest of the global
   symbols.  */

static unsigned long
_bfd_elf_link_renumber_dynsyms (bfd *output_bfd,
				struct bfd_link_info *info,
				unsigned long *section_sym_count)
{
  unsigned long dynsymcount = 0;

  if (info->shared || elf_hash_table (info)->is_relocatable_executable)
    {
      const struct elf_backend_data *bed = get_elf_backend_data (output_bfd);
      asection *p;
      for (p = output_bfd->sections; p ; p = p->next)
	if ((p->flags & SEC_EXCLUDE) == 0
	    && (p->flags & SEC_ALLOC) != 0
	    && !(*bed->elf_backend_omit_section_dynsym) (output_bfd, info, p))
	  elf_section_data (p)->dynindx = ++dynsymcount;
	else
	  elf_section_data (p)->dynindx = 0;
    }
  *section_sym_count = dynsymcount;

  elf_link_hash_traverse (elf_hash_table (info),
			  elf_link_renumber_local_hash_table_dynsyms,
			  &dynsymcount);

  if (elf_hash_table (info)->dynlocal)
    {
      struct elf_link_local_dynamic_entry *p;
      for (p = elf_hash_table (info)->dynlocal; p ; p = p->next)
	p->dynindx = ++dynsymcount;
    }

  elf_link_hash_traverse (elf_hash_table (info),
			  elf_link_renumber_hash_table_dynsyms,
			  &dynsymcount);

  /* There is an unused NULL entry at the head of the table which
     we must account for in our count.  Unless there weren't any
     symbols, which means we'll have no table at all.  */
  if (dynsymcount != 0)
    ++dynsymcount;

  elf_hash_table (info)->dynsymcount = dynsymcount;
  return dynsymcount;
}

/* This function is called when we want to define a new symbol.  It
   handles the various cases which arise when we find a definition in
   a dynamic object, or when there is already a definition in a
   dynamic object.  The new symbol is described by NAME, SYM, PSEC,
   and PVALUE.  We set SYM_HASH to the hash table entry.  We set
   OVERRIDE if the old symbol is overriding a new definition.  We set
   TYPE_CHANGE_OK if it is OK for the type to change.  We set
   SIZE_CHANGE_OK if it is OK for the size to change.  By OK to
   change, we mean that we shouldn't warn if the type or size does
   change.  We set POLD_ALIGNMENT if an old common symbol in a dynamic
   object is overridden by a regular object.  */

bfd_boolean
_bfd_elf_merge_symbol (bfd *abfd,
		       struct bfd_link_info *info,
		       const char *name,
		       Elf_Internal_Sym *sym,
		       asection **psec,
		       bfd_vma *pvalue,
		       unsigned int *pold_alignment,
		       struct elf_link_hash_entry **sym_hash,
		       bfd_boolean *skip,
		       bfd_boolean *override,
		       bfd_boolean *type_change_ok,
		       bfd_boolean *size_change_ok)
{
  asection *sec, *oldsec;
  struct elf_link_hash_entry *h;
  struct elf_link_hash_entry *flip;
  int bind;
  bfd *oldbfd;
  bfd_boolean newdyn, olddyn, olddef, newdef, newdyncommon, olddyncommon;
  bfd_boolean newweak, oldweak;
  const struct elf_backend_data *bed;

  *skip = FALSE;
  *override = FALSE;

  sec = *psec;
  bind = ELF_ST_BIND (sym->st_info);

  /* Silently discard TLS symbols from --just-syms.  There's no way to
     combine a static TLS block with a new TLS block for this executable.  */
  if (ELF_ST_TYPE (sym->st_info) == STT_TLS
      && sec->sec_info_type == ELF_INFO_TYPE_JUST_SYMS)
    {
      *skip = TRUE;
      return TRUE;
    }

  if (! bfd_is_und_section (sec))
    h = elf_link_hash_lookup (elf_hash_table (info), name, TRUE, FALSE, FALSE);
  else
    h = ((struct elf_link_hash_entry *)
	 bfd_wrapped_link_hash_lookup (abfd, info, name, TRUE, FALSE, FALSE));
  if (h == NULL)
    return FALSE;
  *sym_hash = h;

  bed = get_elf_backend_data (abfd);

  /* This code is for coping with dynamic objects, and is only useful
     if we are doing an ELF link.  */
  if (!(*bed->relocs_compatible) (abfd->xvec, info->hash->creator))
    return TRUE;

  /* For merging, we only care about real symbols.  */

  while (h->root.type == bfd_link_hash_indirect
	 || h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* We have to check it for every instance since the first few may be
     refereences and not all compilers emit symbol type for undefined
     symbols.  */
  bfd_elf_link_mark_dynamic_symbol (info, h, sym);

  /* If we just created the symbol, mark it as being an ELF symbol.
     Other than that, there is nothing to do--there is no merge issue
     with a newly defined symbol--so we just return.  */

  if (h->root.type == bfd_link_hash_new)
    {
      h->non_elf = 0;
      return TRUE;
    }

  /* OLDBFD and OLDSEC are a BFD and an ASECTION associated with the
     existing symbol.  */

  switch (h->root.type)
    {
    default:
      oldbfd = NULL;
      oldsec = NULL;
      break;

    case bfd_link_hash_undefined:
    case bfd_link_hash_undefweak:
      oldbfd = h->root.u.undef.abfd;
      oldsec = NULL;
      break;

    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      oldbfd = h->root.u.def.section->owner;
      oldsec = h->root.u.def.section;
      break;

    case bfd_link_hash_common:
      oldbfd = h->root.u.c.p->section->owner;
      oldsec = h->root.u.c.p->section;
      break;
    }

  /* In cases involving weak versioned symbols, we may wind up trying
     to merge a symbol with itself.  Catch that here, to avoid the
     confusion that results if we try to override a symbol with
     itself.  The additional tests catch cases like
     _GLOBAL_OFFSET_TABLE_, which are regular symbols defined in a
     dynamic object, which we do want to handle here.  */
  if (abfd == oldbfd
      && ((abfd->flags & DYNAMIC) == 0
	  || !h->def_regular))
    return TRUE;

  /* NEWDYN and OLDDYN indicate whether the new or old symbol,
     respectively, is from a dynamic object.  */

  newdyn = (abfd->flags & DYNAMIC) != 0;

  olddyn = FALSE;
  if (oldbfd != NULL)
    olddyn = (oldbfd->flags & DYNAMIC) != 0;
  else if (oldsec != NULL)
    {
      /* This handles the special SHN_MIPS_{TEXT,DATA} section
	 indices used by MIPS ELF.  */
      olddyn = (oldsec->symbol->flags & BSF_DYNAMIC) != 0;
    }

  /* NEWDEF and OLDDEF indicate whether the new or old symbol,
     respectively, appear to be a definition rather than reference.  */

  newdef = !bfd_is_und_section (sec) && !bfd_is_com_section (sec);

  olddef = (h->root.type != bfd_link_hash_undefined
	    && h->root.type != bfd_link_hash_undefweak
	    && h->root.type != bfd_link_hash_common);

  /* When we try to create a default indirect symbol from the dynamic
     definition with the default version, we skip it if its type and
     the type of existing regular definition mismatch.  We only do it
     if the existing regular definition won't be dynamic.  */
  if (pold_alignment == NULL
      && !info->shared
      && !info->export_dynamic
      && !h->ref_dynamic
      && newdyn
      && newdef
      && !olddyn
      && (olddef || h->root.type == bfd_link_hash_common)
      && ELF_ST_TYPE (sym->st_info) != h->type
      && ELF_ST_TYPE (sym->st_info) != STT_NOTYPE
      && h->type != STT_NOTYPE
      && !(bed->is_function_type (ELF_ST_TYPE (sym->st_info))
	   && bed->is_function_type (h->type)))
    {
      *skip = TRUE;
      return TRUE;
    }

  /* Check TLS symbol.  We don't check undefined symbol introduced by
     "ld -u".  */
  if ((ELF_ST_TYPE (sym->st_info) == STT_TLS || h->type == STT_TLS)
      && ELF_ST_TYPE (sym->st_info) != h->type
      && oldbfd != NULL)
    {
      bfd *ntbfd, *tbfd;
      bfd_boolean ntdef, tdef;
      asection *ntsec, *tsec;

      if (h->type == STT_TLS)
	{
	  ntbfd = abfd;
	  ntsec = sec;
	  ntdef = newdef;
	  tbfd = oldbfd;
	  tsec = oldsec;
	  tdef = olddef;
	}
      else
	{
	  ntbfd = oldbfd;
	  ntsec = oldsec;
	  ntdef = olddef;
	  tbfd = abfd;
	  tsec = sec;
	  tdef = newdef;
	}

      if (tdef && ntdef)
	(*_bfd_error_handler)
	  (_("%s: TLS definition in %B section %A mismatches non-TLS definition in %B section %A"),
	   tbfd, tsec, ntbfd, ntsec, h->root.root.string);
      else if (!tdef && !ntdef)
	(*_bfd_error_handler)
	  (_("%s: TLS reference in %B mismatches non-TLS reference in %B"),
	   tbfd, ntbfd, h->root.root.string);
      else if (tdef)
	(*_bfd_error_handler)
	  (_("%s: TLS definition in %B section %A mismatches non-TLS reference in %B"),
	   tbfd, tsec, ntbfd, h->root.root.string);
      else
	(*_bfd_error_handler)
	  (_("%s: TLS reference in %B mismatches non-TLS definition in %B section %A"),
	   tbfd, ntbfd, ntsec, h->root.root.string);

      bfd_set_error (bfd_error_bad_value);
      return FALSE;
    }

  /* We need to remember if a symbol has a definition in a dynamic
     object or is weak in all dynamic objects. Internal and hidden
     visibility will make it unavailable to dynamic objects.  */
  if (newdyn && !h->dynamic_def)
    {
      if (!bfd_is_und_section (sec))
	h->dynamic_def = 1;
      else
	{
	  /* Check if this symbol is weak in all dynamic objects. If it
	     is the first time we see it in a dynamic object, we mark
	     if it is weak. Otherwise, we clear it.  */
	  if (!h->ref_dynamic)
	    {
	      if (bind == STB_WEAK)
		h->dynamic_weak = 1;
	    }
	  else if (bind != STB_WEAK)
	    h->dynamic_weak = 0;
	}
    }

  /* If the old symbol has non-default visibility, we ignore the new
     definition from a dynamic object.  */
  if (newdyn
      && ELF_ST_VISIBILITY (h->other) != STV_DEFAULT
      && !bfd_is_und_section (sec))
    {
      *skip = TRUE;
      /* Make sure this symbol is dynamic.  */
      h->ref_dynamic = 1;
      /* A protected symbol has external availability. Make sure it is
	 recorded as dynamic.

	 FIXME: Should we check type and size for protected symbol?  */
      if (ELF_ST_VISIBILITY (h->other) == STV_PROTECTED)
	return bfd_elf_link_record_dynamic_symbol (info, h);
      else
	return TRUE;
    }
  else if (!newdyn
	   && ELF_ST_VISIBILITY (sym->st_other) != STV_DEFAULT
	   && h->def_dynamic)
    {
      /* If the new symbol with non-default visibility comes from a
	 relocatable file and the old definition comes from a dynamic
	 object, we remove the old definition.  */
      if ((*sym_hash)->root.type == bfd_link_hash_indirect)
	{
	  /* Handle the case where the old dynamic definition is
	     default versioned.  We need to copy the symbol info from
	     the symbol with default version to the normal one if it
	     was referenced before.  */
	  if (h->ref_regular)
	    {
	      const struct elf_backend_data *bed
		= get_elf_backend_data (abfd);
	      struct elf_link_hash_entry *vh = *sym_hash;
	      vh->root.type = h->root.type;
	      h->root.type = bfd_link_hash_indirect;
	      (*bed->elf_backend_copy_indirect_symbol) (info, vh, h);
	      /* Protected symbols will override the dynamic definition
		 with default version.  */
	      if (ELF_ST_VISIBILITY (sym->st_other) == STV_PROTECTED)
		{
		  h->root.u.i.link = (struct bfd_link_hash_entry *) vh;
		  vh->dynamic_def = 1;
		  vh->ref_dynamic = 1;
		}
	      else
		{
		  h->root.type = vh->root.type;
		  vh->ref_dynamic = 0;
		  /* We have to hide it here since it was made dynamic
		     global with extra bits when the symbol info was
		     copied from the old dynamic definition.  */
		  (*bed->elf_backend_hide_symbol) (info, vh, TRUE);
		}
	      h = vh;
	    }
	  else
	    h = *sym_hash;
	}

      if ((h->root.u.undef.next || info->hash->undefs_tail == &h->root)
	  && bfd_is_und_section (sec))
	{
	  /* If the new symbol is undefined and the old symbol was
	     also undefined before, we need to make sure
	     _bfd_generic_link_add_one_symbol doesn't mess
	     up the linker hash table undefs list.  Since the old
	     definition came from a dynamic object, it is still on the
	     undefs list.  */
	  h->root.type = bfd_link_hash_undefined;
	  h->root.u.undef.abfd = abfd;
	}
      else
	{
	  h->root.type = bfd_link_hash_new;
	  h->root.u.undef.abfd = NULL;
	}

      if (h->def_dynamic)
	{
	  h->def_dynamic = 0;
	  h->ref_dynamic = 1;
	  h->dynamic_def = 1;
	}
      /* FIXME: Should we check type and size for protected symbol?  */
      h->size = 0;
      h->type = 0;
      return TRUE;
    }

  /* Differentiate strong and weak symbols.  */
  newweak = bind == STB_WEAK;
  oldweak = (h->root.type == bfd_link_hash_defweak
	     || h->root.type == bfd_link_hash_undefweak);

  /* If a new weak symbol definition comes from a regular file and the
     old symbol comes from a dynamic library, we treat the new one as
     strong.  Similarly, an old weak symbol definition from a regular
     file is treated as strong when the new symbol comes from a dynamic
     library.  Further, an old weak symbol from a dynamic library is
     treated as strong if the new symbol is from a dynamic library.
     This reflects the way glibc's ld.so works.

     Do this before setting *type_change_ok or *size_change_ok so that
     we warn properly when dynamic library symbols are overridden.  */

  if (newdef && !newdyn && olddyn)
    newweak = FALSE;
  if (olddef && newdyn)
    oldweak = FALSE;

  /* Allow changes between different types of funciton symbol.  */
  if (bed->is_function_type (ELF_ST_TYPE (sym->st_info))
      && bed->is_function_type (h->type))
    *type_change_ok = TRUE;

  /* It's OK to change the type if either the existing symbol or the
     new symbol is weak.  A type change is also OK if the old symbol
     is undefined and the new symbol is defined.  */

  if (oldweak
      || newweak
      || (newdef
	  && h->root.type == bfd_link_hash_undefined))
    *type_change_ok = TRUE;

  /* It's OK to change the size if either the existing symbol or the
     new symbol is weak, or if the old symbol is undefined.  */

  if (*type_change_ok
      || h->root.type == bfd_link_hash_undefined)
    *size_change_ok = TRUE;

  /* NEWDYNCOMMON and OLDDYNCOMMON indicate whether the new or old
     symbol, respectively, appears to be a common symbol in a dynamic
     object.  If a symbol appears in an uninitialized section, and is
     not weak, and is not a function, then it may be a common symbol
     which was resolved when the dynamic object was created.  We want
     to treat such symbols specially, because they raise special
     considerations when setting the symbol size: if the symbol
     appears as a common symbol in a regular object, and the size in
     the regular object is larger, we must make sure that we use the
     larger size.  This problematic case can always be avoided in C,
     but it must be handled correctly when using Fortran shared
     libraries.

     Note that if NEWDYNCOMMON is set, NEWDEF will be set, and
     likewise for OLDDYNCOMMON and OLDDEF.

     Note that this test is just a heuristic, and that it is quite
     possible to have an uninitialized symbol in a shared object which
     is really a definition, rather than a common symbol.  This could
     lead to some minor confusion when the symbol really is a common
     symbol in some regular object.  However, I think it will be
     harmless.  */

  if (newdyn
      && newdef
      && !newweak
      && (sec->flags & SEC_ALLOC) != 0
      && (sec->flags & SEC_LOAD) == 0
      && sym->st_size > 0
      && !bed->is_function_type (ELF_ST_TYPE (sym->st_info)))
    newdyncommon = TRUE;
  else
    newdyncommon = FALSE;

  if (olddyn
      && olddef
      && h->root.type == bfd_link_hash_defined
      && h->def_dynamic
      && (h->root.u.def.section->flags & SEC_ALLOC) != 0
      && (h->root.u.def.section->flags & SEC_LOAD) == 0
      && h->size > 0
      && !bed->is_function_type (h->type))
    olddyncommon = TRUE;
  else
    olddyncommon = FALSE;

  /* We now know everything about the old and new symbols.  We ask the
     backend to check if we can merge them.  */
  if (bed->merge_symbol
      && !bed->merge_symbol (info, sym_hash, h, sym, psec, pvalue,
			     pold_alignment, skip, override,
			     type_change_ok, size_change_ok,
			     &newdyn, &newdef, &newdyncommon, &newweak,
			     abfd, &sec,
			     &olddyn, &olddef, &olddyncommon, &oldweak,
			     oldbfd, &oldsec))
    return FALSE;

  /* If both the old and the new symbols look like common symbols in a
     dynamic object, set the size of the symbol to the larger of the
     two.  */

  if (olddyncommon
      && newdyncommon
      && sym->st_size != h->size)
    {
      /* Since we think we have two common symbols, issue a multiple
	 common warning if desired.  Note that we only warn if the
	 size is different.  If the size is the same, we simply let
	 the old symbol override the new one as normally happens with
	 symbols defined in dynamic objects.  */

      if (! ((*info->callbacks->multiple_common)
	     (info, h->root.root.string, oldbfd, bfd_link_hash_common,
	      h->size, abfd, bfd_link_hash_common, sym->st_size)))
	return FALSE;

      if (sym->st_size > h->size)
	h->size = sym->st_size;

      *size_change_ok = TRUE;
    }

  /* If we are looking at a dynamic object, and we have found a
     definition, we need to see if the symbol was already defined by
     some other object.  If so, we want to use the existing
     definition, and we do not want to report a multiple symbol
     definition error; we do this by clobbering *PSEC to be
     bfd_und_section_ptr.

     We treat a common symbol as a definition if the symbol in the
     shared library is a function, since common symbols always
     represent variables; this can cause confusion in principle, but
     any such confusion would seem to indicate an erroneous program or
     shared library.  We also permit a common symbol in a regular
     object to override a weak symbol in a shared object.  */

  if (newdyn
      && newdef
      && (olddef
	  || (h->root.type == bfd_link_hash_common
	      && (newweak
		  || bed->is_function_type (ELF_ST_TYPE (sym->st_info))))))
    {
      *override = TRUE;
      newdef = FALSE;
      newdyncommon = FALSE;

      *psec = sec = bfd_und_section_ptr;
      *size_change_ok = TRUE;

      /* If we get here when the old symbol is a common symbol, then
	 we are explicitly letting it override a weak symbol or
	 function in a dynamic object, and we don't want to warn about
	 a type change.  If the old symbol is a defined symbol, a type
	 change warning may still be appropriate.  */

      if (h->root.type == bfd_link_hash_common)
	*type_change_ok = TRUE;
    }

  /* Handle the special case of an old common symbol merging with a
     new symbol which looks like a common symbol in a shared object.
     We change *PSEC and *PVALUE to make the new symbol look like a
     common symbol, and let _bfd_generic_link_add_one_symbol do the
     right thing.  */

  if (newdyncommon
      && h->root.type == bfd_link_hash_common)
    {
      *override = TRUE;
      newdef = FALSE;
      newdyncommon = FALSE;
      *pvalue = sym->st_size;
      *psec = sec = bed->common_section (oldsec);
      *size_change_ok = TRUE;
    }

  /* Skip weak definitions of symbols that are already defined.  */
  if (newdef && olddef && newweak)
    *skip = TRUE;

  /* If the old symbol is from a dynamic object, and the new symbol is
     a definition which is not from a dynamic object, then the new
     symbol overrides the old symbol.  Symbols from regular files
     always take precedence over symbols from dynamic objects, even if
     they are defined after the dynamic object in the link.

     As above, we again permit a common symbol in a regular object to
     override a definition in a shared object if the shared object
     symbol is a function or is weak.  */

  flip = NULL;
  if (!newdyn
      && (newdef
	  || (bfd_is_com_section (sec)
	      && (oldweak
		  || bed->is_function_type (h->type))))
      && olddyn
      && olddef
      && h->def_dynamic)
    {
      /* Change the hash table entry to undefined, and let
	 _bfd_generic_link_add_one_symbol do the right thing with the
	 new definition.  */

      h->root.type = bfd_link_hash_undefined;
      h->root.u.undef.abfd = h->root.u.def.section->owner;
      *size_change_ok = TRUE;

      olddef = FALSE;
      olddyncommon = FALSE;

      /* We again permit a type change when a common symbol may be
	 overriding a function.  */

      if (bfd_is_com_section (sec))
	*type_change_ok = TRUE;

      if ((*sym_hash)->root.type == bfd_link_hash_indirect)
	flip = *sym_hash;
      else
	/* This union may have been set to be non-NULL when this symbol
	   was seen in a dynamic object.  We must force the union to be
	   NULL, so that it is correct for a regular symbol.  */
	h->verinfo.vertree = NULL;
    }

  /* Handle the special case of a new common symbol merging with an
     old symbol that looks like it might be a common symbol defined in
     a shared object.  Note that we have already handled the case in
     which a new common symbol should simply override the definition
     in the shared library.  */

  if (! newdyn
      && bfd_is_com_section (sec)
      && olddyncommon)
    {
      /* It would be best if we could set the hash table entry to a
	 common symbol, but we don't know what to use for the section
	 or the alignment.  */
      if (! ((*info->callbacks->multiple_common)
	     (info, h->root.root.string, oldbfd, bfd_link_hash_common,
	      h->size, abfd, bfd_link_hash_common, sym->st_size)))
	return FALSE;

      /* If the presumed common symbol in the dynamic object is
	 larger, pretend that the new symbol has its size.  */

      if (h->size > *pvalue)
	*pvalue = h->size;

      /* We need to remember the alignment required by the symbol
	 in the dynamic object.  */
      BFD_ASSERT (pold_alignment);
      *pold_alignment = h->root.u.def.section->alignment_power;

      olddef = FALSE;
      olddyncommon = FALSE;

      h->root.type = bfd_link_hash_undefined;
      h->root.u.undef.abfd = h->root.u.def.section->owner;

      *size_change_ok = TRUE;
      *type_change_ok = TRUE;

      if ((*sym_hash)->root.type == bfd_link_hash_indirect)
	flip = *sym_hash;
      else
	h->verinfo.vertree = NULL;
    }

  if (flip != NULL)
    {
      /* Handle the case where we had a versioned symbol in a dynamic
	 library and now find a definition in a normal object.  In this
	 case, we make the versioned symbol point to the normal one.  */
      const struct elf_backend_data *bed = get_elf_backend_data (abfd);
      flip->root.type = h->root.type;
      flip->root.u.undef.abfd = h->root.u.undef.abfd;
      h->root.type = bfd_link_hash_indirect;
      h->root.u.i.link = (struct bfd_link_hash_entry *) flip;
      (*bed->elf_backend_copy_indirect_symbol) (info, flip, h);
      if (h->def_dynamic)
	{
	  h->def_dynamic = 0;
	  flip->ref_dynamic = 1;
	}
    }

  return TRUE;
}

/* This function is called to create an indirect symbol from the
   default for the symbol with the default version if needed. The
   symbol is described by H, NAME, SYM, PSEC, VALUE, and OVERRIDE.  We
   set DYNSYM if the new indirect symbol is dynamic.  */

bfd_boolean
_bfd_elf_add_default_symbol (bfd *abfd,
			     struct bfd_link_info *info,
			     struct elf_link_hash_entry *h,
			     const char *name,
			     Elf_Internal_Sym *sym,
			     asection **psec,
			     bfd_vma *value,
			     bfd_boolean *dynsym,
			     bfd_boolean override)
{
  bfd_boolean type_change_ok;
  bfd_boolean size_change_ok;
  bfd_boolean skip;
  char *shortname;
  struct elf_link_hash_entry *hi;
  struct bfd_link_hash_entry *bh;
  const struct elf_backend_data *bed;
  bfd_boolean collect;
  bfd_boolean dynamic;
  char *p;
  size_t len, shortlen;
  asection *sec;

  /* If this symbol has a version, and it is the default version, we
     create an indirect symbol from the default name to the fully
     decorated name.  This will cause external references which do not
     specify a version to be bound to this version of the symbol.  */
  p = strchr (name, ELF_VER_CHR);
  if (p == NULL || p[1] != ELF_VER_CHR)
    return TRUE;

  if (override)
    {
      /* We are overridden by an old definition. We need to check if we
	 need to create the indirect symbol from the default name.  */
      hi = elf_link_hash_lookup (elf_hash_table (info), name, TRUE,
				 FALSE, FALSE);
      BFD_ASSERT (hi != NULL);
      if (hi == h)
	return TRUE;
      while (hi->root.type == bfd_link_hash_indirect
	     || hi->root.type == bfd_link_hash_warning)
	{
	  hi = (struct elf_link_hash_entry *) hi->root.u.i.link;
	  if (hi == h)
	    return TRUE;
	}
    }

  bed = get_elf_backend_data (abfd);
  collect = bed->collect;
  dynamic = (abfd->flags & DYNAMIC) != 0;

  shortlen = p - name;
  shortname = bfd_hash_allocate (&info->hash->table, shortlen + 1);
  if (shortname == NULL)
    return FALSE;
  memcpy (shortname, name, shortlen);
  shortname[shortlen] = '\0';

  /* We are going to create a new symbol.  Merge it with any existing
     symbol with this name.  For the purposes of the merge, act as
     though we were defining the symbol we just defined, although we
     actually going to define an indirect symbol.  */
  type_change_ok = FALSE;
  size_change_ok = FALSE;
  sec = *psec;
  if (!_bfd_elf_merge_symbol (abfd, info, shortname, sym, &sec, value,
			      NULL, &hi, &skip, &override,
			      &type_change_ok, &size_change_ok))
    return FALSE;

  if (skip)
    goto nondefault;

  if (! override)
    {
      bh = &hi->root;
      if (! (_bfd_generic_link_add_one_symbol
	     (info, abfd, shortname, BSF_INDIRECT, bfd_ind_section_ptr,
	      0, name, FALSE, collect, &bh)))
	return FALSE;
      hi = (struct elf_link_hash_entry *) bh;
    }
  else
    {
      /* In this case the symbol named SHORTNAME is overriding the
	 indirect symbol we want to add.  We were planning on making
	 SHORTNAME an indirect symbol referring to NAME.  SHORTNAME
	 is the name without a version.  NAME is the fully versioned
	 name, and it is the default version.

	 Overriding means that we already saw a definition for the
	 symbol SHORTNAME in a regular object, and it is overriding
	 the symbol defined in the dynamic object.

	 When this happens, we actually want to change NAME, the
	 symbol we just added, to refer to SHORTNAME.  This will cause
	 references to NAME in the shared object to become references
	 to SHORTNAME in the regular object.  This is what we expect
	 when we override a function in a shared object: that the
	 references in the shared object will be mapped to the
	 definition in the regular object.  */

      while (hi->root.type == bfd_link_hash_indirect
	     || hi->root.type == bfd_link_hash_warning)
	hi = (struct elf_link_hash_entry *) hi->root.u.i.link;

      h->root.type = bfd_link_hash_indirect;
      h->root.u.i.link = (struct bfd_link_hash_entry *) hi;
      if (h->def_dynamic)
	{
	  h->def_dynamic = 0;
	  hi->ref_dynamic = 1;
	  if (hi->ref_regular
	      || hi->def_regular)
	    {
	      if (! bfd_elf_link_record_dynamic_symbol (info, hi))
		return FALSE;
	    }
	}

      /* Now set HI to H, so that the following code will set the
	 other fields correctly.  */
      hi = h;
    }

  /* Check if HI is a warning symbol.  */
  if (hi->root.type == bfd_link_hash_warning)
    hi = (struct elf_link_hash_entry *) hi->root.u.i.link;

  /* If there is a duplicate definition somewhere, then HI may not
     point to an indirect symbol.  We will have reported an error to
     the user in that case.  */

  if (hi->root.type == bfd_link_hash_indirect)
    {
      struct elf_link_hash_entry *ht;

      ht = (struct elf_link_hash_entry *) hi->root.u.i.link;
      (*bed->elf_backend_copy_indirect_symbol) (info, ht, hi);

      /* See if the new flags lead us to realize that the symbol must
	 be dynamic.  */
      if (! *dynsym)
	{
	  if (! dynamic)
	    {
	      if (info->shared
		  || hi->ref_dynamic)
		*dynsym = TRUE;
	    }
	  else
	    {
	      if (hi->ref_regular)
		*dynsym = TRUE;
	    }
	}
    }

  /* We also need to define an indirection from the nondefault version
     of the symbol.  */

nondefault:
  len = strlen (name);
  shortname = bfd_hash_allocate (&info->hash->table, len);
  if (shortname == NULL)
    return FALSE;
  memcpy (shortname, name, shortlen);
  memcpy (shortname + shortlen, p + 1, len - shortlen);

  /* Once again, merge with any existing symbol.  */
  type_change_ok = FALSE;
  size_change_ok = FALSE;
  sec = *psec;
  if (!_bfd_elf_merge_symbol (abfd, info, shortname, sym, &sec, value,
			      NULL, &hi, &skip, &override,
			      &type_change_ok, &size_change_ok))
    return FALSE;

  if (skip)
    return TRUE;

  if (override)
    {
      /* Here SHORTNAME is a versioned name, so we don't expect to see
	 the type of override we do in the case above unless it is
	 overridden by a versioned definition.  */
      if (hi->root.type != bfd_link_hash_defined
	  && hi->root.type != bfd_link_hash_defweak)
	(*_bfd_error_handler)
	  (_("%B: unexpected redefinition of indirect versioned symbol `%s'"),
	   abfd, shortname);
    }
  else
    {
      bh = &hi->root;
      if (! (_bfd_generic_link_add_one_symbol
	     (info, abfd, shortname, BSF_INDIRECT,
	      bfd_ind_section_ptr, 0, name, FALSE, collect, &bh)))
	return FALSE;
      hi = (struct elf_link_hash_entry *) bh;

      /* If there is a duplicate definition somewhere, then HI may not
	 point to an indirect symbol.  We will have reported an error
	 to the user in that case.  */

      if (hi->root.type == bfd_link_hash_indirect)
	{
	  (*bed->elf_backend_copy_indirect_symbol) (info, h, hi);

	  /* See if the new flags lead us to realize that the symbol
	     must be dynamic.  */
	  if (! *dynsym)
	    {
	      if (! dynamic)
		{
		  if (info->shared
		      || hi->ref_dynamic)
		    *dynsym = TRUE;
		}
	      else
		{
		  if (hi->ref_regular)
		    *dynsym = TRUE;
		}
	    }
	}
    }

  return TRUE;
}

/* This routine is used to export all defined symbols into the dynamic
   symbol table.  It is called via elf_link_hash_traverse.  */

bfd_boolean
_bfd_elf_export_symbol (struct elf_link_hash_entry *h, void *data)
{
  struct elf_info_failed *eif = data;

  /* Ignore this if we won't export it.  */
  if (!eif->info->export_dynamic && !h->dynamic)
    return TRUE;

  /* Ignore indirect symbols.  These are added by the versioning code.  */
  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (h->dynindx == -1
      && (h->def_regular
	  || h->ref_regular))
    {
      struct bfd_elf_version_tree *t;
      struct bfd_elf_version_expr *d;

      for (t = eif->verdefs; t != NULL; t = t->next)
	{
	  if (t->globals.list != NULL)
	    {
	      d = (*t->match) (&t->globals, NULL, h->root.root.string);
	      if (d != NULL)
		goto doit;
	    }

	  if (t->locals.list != NULL)
	    {
	      d = (*t->match) (&t->locals, NULL, h->root.root.string);
	      if (d != NULL)
		return TRUE;
	    }
	}

      if (!eif->verdefs)
	{
	doit:
	  if (! bfd_elf_link_record_dynamic_symbol (eif->info, h))
	    {
	      eif->failed = TRUE;
	      return FALSE;
	    }
	}
    }

  return TRUE;
}

/* Look through the symbols which are defined in other shared
   libraries and referenced here.  Update the list of version
   dependencies.  This will be put into the .gnu.version_r section.
   This function is called via elf_link_hash_traverse.  */

bfd_boolean
_bfd_elf_link_find_version_dependencies (struct elf_link_hash_entry *h,
					 void *data)
{
  struct elf_find_verdep_info *rinfo = data;
  Elf_Internal_Verneed *t;
  Elf_Internal_Vernaux *a;
  bfd_size_type amt;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* We only care about symbols defined in shared objects with version
     information.  */
  if (!h->def_dynamic
      || h->def_regular
      || h->dynindx == -1
      || h->verinfo.verdef == NULL)
    return TRUE;

  /* See if we already know about this version.  */
  for (t = elf_tdata (rinfo->output_bfd)->verref; t != NULL; t = t->vn_nextref)
    {
      if (t->vn_bfd != h->verinfo.verdef->vd_bfd)
	continue;

      for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
	if (a->vna_nodename == h->verinfo.verdef->vd_nodename)
	  return TRUE;

      break;
    }

  /* This is a new version.  Add it to tree we are building.  */

  if (t == NULL)
    {
      amt = sizeof *t;
      t = bfd_zalloc (rinfo->output_bfd, amt);
      if (t == NULL)
	{
	  rinfo->failed = TRUE;
	  return FALSE;
	}

      t->vn_bfd = h->verinfo.verdef->vd_bfd;
      t->vn_nextref = elf_tdata (rinfo->output_bfd)->verref;
      elf_tdata (rinfo->output_bfd)->verref = t;
    }

  amt = sizeof *a;
  a = bfd_zalloc (rinfo->output_bfd, amt);

  /* Note that we are copying a string pointer here, and testing it
     above.  If bfd_elf_string_from_elf_section is ever changed to
     discard the string data when low in memory, this will have to be
     fixed.  */
  a->vna_nodename = h->verinfo.verdef->vd_nodename;

  a->vna_flags = h->verinfo.verdef->vd_flags;
  a->vna_nextptr = t->vn_auxptr;

  h->verinfo.verdef->vd_exp_refno = rinfo->vers;
  ++rinfo->vers;

  a->vna_other = h->verinfo.verdef->vd_exp_refno + 1;

  t->vn_auxptr = a;

  return TRUE;
}

/* Figure out appropriate versions for all the symbols.  We may not
   have the version number script until we have read all of the input
   files, so until that point we don't know which symbols should be
   local.  This function is called via elf_link_hash_traverse.  */

bfd_boolean
_bfd_elf_link_assign_sym_version (struct elf_link_hash_entry *h, void *data)
{
  struct elf_assign_sym_version_info *sinfo;
  struct bfd_link_info *info;
  const struct elf_backend_data *bed;
  struct elf_info_failed eif;
  char *p;
  bfd_size_type amt;

  sinfo = data;
  info = sinfo->info;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* Fix the symbol flags.  */
  eif.failed = FALSE;
  eif.info = info;
  if (! _bfd_elf_fix_symbol_flags (h, &eif))
    {
      if (eif.failed)
	sinfo->failed = TRUE;
      return FALSE;
    }

  /* We only need version numbers for symbols defined in regular
     objects.  */
  if (!h->def_regular)
    return TRUE;

  bed = get_elf_backend_data (sinfo->output_bfd);
  p = strchr (h->root.root.string, ELF_VER_CHR);
  if (p != NULL && h->verinfo.vertree == NULL)
    {
      struct bfd_elf_version_tree *t;
      bfd_boolean hidden;

      hidden = TRUE;

      /* There are two consecutive ELF_VER_CHR characters if this is
	 not a hidden symbol.  */
      ++p;
      if (*p == ELF_VER_CHR)
	{
	  hidden = FALSE;
	  ++p;
	}

      /* If there is no version string, we can just return out.  */
      if (*p == '\0')
	{
	  if (hidden)
	    h->hidden = 1;
	  return TRUE;
	}

      /* Look for the version.  If we find it, it is no longer weak.  */
      for (t = sinfo->verdefs; t != NULL; t = t->next)
	{
	  if (strcmp (t->name, p) == 0)
	    {
	      size_t len;
	      char *alc;
	      struct bfd_elf_version_expr *d;

	      len = p - h->root.root.string;
	      alc = bfd_malloc (len);
	      if (alc == NULL)
		return FALSE;
	      memcpy (alc, h->root.root.string, len - 1);
	      alc[len - 1] = '\0';
	      if (alc[len - 2] == ELF_VER_CHR)
		alc[len - 2] = '\0';

	      h->verinfo.vertree = t;
	      t->used = TRUE;
	      d = NULL;

	      if (t->globals.list != NULL)
		d = (*t->match) (&t->globals, NULL, alc);

	      /* See if there is anything to force this symbol to
		 local scope.  */
	      if (d == NULL && t->locals.list != NULL)
		{
		  d = (*t->match) (&t->locals, NULL, alc);
		  if (d != NULL
		      && h->dynindx != -1
		      && ! info->export_dynamic)
		    (*bed->elf_backend_hide_symbol) (info, h, TRUE);
		}

	      free (alc);
	      break;
	    }
	}

      /* If we are building an application, we need to create a
	 version node for this version.  */
      if (t == NULL && info->executable)
	{
	  struct bfd_elf_version_tree **pp;
	  int version_index;

	  /* If we aren't going to export this symbol, we don't need
	     to worry about it.  */
	  if (h->dynindx == -1)
	    return TRUE;

	  amt = sizeof *t;
	  t = bfd_zalloc (sinfo->output_bfd, amt);
	  if (t == NULL)
	    {
	      sinfo->failed = TRUE;
	      return FALSE;
	    }

	  t->name = p;
	  t->name_indx = (unsigned int) -1;
	  t->used = TRUE;

	  version_index = 1;
	  /* Don't count anonymous version tag.  */
	  if (sinfo->verdefs != NULL && sinfo->verdefs->vernum == 0)
	    version_index = 0;
	  for (pp = &sinfo->verdefs; *pp != NULL; pp = &(*pp)->next)
	    ++version_index;
	  t->vernum = version_index;

	  *pp = t;

	  h->verinfo.vertree = t;
	}
      else if (t == NULL)
	{
	  /* We could not find the version for a symbol when
	     generating a shared archive.  Return an error.  */
	  (*_bfd_error_handler)
	    (_("%B: version node not found for symbol %s"),
	     sinfo->output_bfd, h->root.root.string);
	  bfd_set_error (bfd_error_bad_value);
	  sinfo->failed = TRUE;
	  return FALSE;
	}

      if (hidden)
	h->hidden = 1;
    }

  /* If we don't have a version for this symbol, see if we can find
     something.  */
  if (h->verinfo.vertree == NULL && sinfo->verdefs != NULL)
    {
      struct bfd_elf_version_tree *t;
      struct bfd_elf_version_tree *local_ver;
      struct bfd_elf_version_expr *d;

      /* See if can find what version this symbol is in.  If the
	 symbol is supposed to be local, then don't actually register
	 it.  */
      local_ver = NULL;
      for (t = sinfo->verdefs; t != NULL; t = t->next)
	{
	  if (t->globals.list != NULL)
	    {
	      bfd_boolean matched;

	      matched = FALSE;
	      d = NULL;
	      while ((d = (*t->match) (&t->globals, d,
				       h->root.root.string)) != NULL)
		if (d->symver)
		  matched = TRUE;
		else
		  {
		    /* There is a version without definition.  Make
		       the symbol the default definition for this
		       version.  */
		    h->verinfo.vertree = t;
		    local_ver = NULL;
		    d->script = 1;
		    break;
		  }
	      if (d != NULL)
		break;
	      else if (matched)
		/* There is no undefined version for this symbol. Hide the
		   default one.  */
		(*bed->elf_backend_hide_symbol) (info, h, TRUE);
	    }

	  if (t->locals.list != NULL)
	    {
	      d = NULL;
	      while ((d = (*t->match) (&t->locals, d,
				       h->root.root.string)) != NULL)
		{
		  local_ver = t;
		  /* If the match is "*", keep looking for a more
		     explicit, perhaps even global, match.
		     XXX: Shouldn't this be !d->wildcard instead?  */
		  if (d->pattern[0] != '*' || d->pattern[1] != '\0')
		    break;
		}

	      if (d != NULL)
		break;
	    }
	}

      if (local_ver != NULL)
	{
	  h->verinfo.vertree = local_ver;
	  if (h->dynindx != -1
	      && ! info->export_dynamic)
	    {
	      (*bed->elf_backend_hide_symbol) (info, h, TRUE);
	    }
	}
    }

  return TRUE;
}

/* Read and swap the relocs from the section indicated by SHDR.  This
   may be either a REL or a RELA section.  The relocations are
   translated into RELA relocations and stored in INTERNAL_RELOCS,
   which should have already been allocated to contain enough space.
   The EXTERNAL_RELOCS are a buffer where the external form of the
   relocations should be stored.

   Returns FALSE if something goes wrong.  */

static bfd_boolean
elf_link_read_relocs_from_section (bfd *abfd,
				   asection *sec,
				   Elf_Internal_Shdr *shdr,
				   void *external_relocs,
				   Elf_Internal_Rela *internal_relocs)
{
  const struct elf_backend_data *bed;
  void (*swap_in) (bfd *, const bfd_byte *, Elf_Internal_Rela *);
  const bfd_byte *erela;
  const bfd_byte *erelaend;
  Elf_Internal_Rela *irela;
  Elf_Internal_Shdr *symtab_hdr;
  size_t nsyms;

  /* Position ourselves at the start of the section.  */
  if (bfd_seek (abfd, shdr->sh_offset, SEEK_SET) != 0)
    return FALSE;

  /* Read the relocations.  */
  if (bfd_bread (external_relocs, shdr->sh_size, abfd) != shdr->sh_size)
    return FALSE;

  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  nsyms = symtab_hdr->sh_size / symtab_hdr->sh_entsize;

  bed = get_elf_backend_data (abfd);

  /* Convert the external relocations to the internal format.  */
  if (shdr->sh_entsize == bed->s->sizeof_rel)
    swap_in = bed->s->swap_reloc_in;
  else if (shdr->sh_entsize == bed->s->sizeof_rela)
    swap_in = bed->s->swap_reloca_in;
  else
    {
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }

  erela = external_relocs;
  erelaend = erela + shdr->sh_size;
  irela = internal_relocs;
  while (erela < erelaend)
    {
      bfd_vma r_symndx;

      (*swap_in) (abfd, erela, irela);
      r_symndx = ELF32_R_SYM (irela->r_info);
      if (bed->s->arch_size == 64)
	r_symndx >>= 24;
      if ((size_t) r_symndx >= nsyms)
	{
	  (*_bfd_error_handler)
	    (_("%B: bad reloc symbol index (0x%lx >= 0x%lx)"
	       " for offset 0x%lx in section `%A'"),
	     abfd, sec,
	     (unsigned long) r_symndx, (unsigned long) nsyms, irela->r_offset);
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}
      irela += bed->s->int_rels_per_ext_rel;
      erela += shdr->sh_entsize;
    }

  return TRUE;
}

/* Read and swap the relocs for a section O.  They may have been
   cached.  If the EXTERNAL_RELOCS and INTERNAL_RELOCS arguments are
   not NULL, they are used as buffers to read into.  They are known to
   be large enough.  If the INTERNAL_RELOCS relocs argument is NULL,
   the return value is allocated using either malloc or bfd_alloc,
   according to the KEEP_MEMORY argument.  If O has two relocation
   sections (both REL and RELA relocations), then the REL_HDR
   relocations will appear first in INTERNAL_RELOCS, followed by the
   REL_HDR2 relocations.  */

Elf_Internal_Rela *
_bfd_elf_link_read_relocs (bfd *abfd,
			   asection *o,
			   void *external_relocs,
			   Elf_Internal_Rela *internal_relocs,
			   bfd_boolean keep_memory)
{
  Elf_Internal_Shdr *rel_hdr;
  void *alloc1 = NULL;
  Elf_Internal_Rela *alloc2 = NULL;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (elf_section_data (o)->relocs != NULL)
    return elf_section_data (o)->relocs;

  if (o->reloc_count == 0)
    return NULL;

  rel_hdr = &elf_section_data (o)->rel_hdr;

  if (internal_relocs == NULL)
    {
      bfd_size_type size;

      size = o->reloc_count;
      size *= bed->s->int_rels_per_ext_rel * sizeof (Elf_Internal_Rela);
      if (keep_memory)
	internal_relocs = bfd_alloc (abfd, size);
      else
	internal_relocs = alloc2 = bfd_malloc (size);
      if (internal_relocs == NULL)
	goto error_return;
    }

  if (external_relocs == NULL)
    {
      bfd_size_type size = rel_hdr->sh_size;

      if (elf_section_data (o)->rel_hdr2)
	size += elf_section_data (o)->rel_hdr2->sh_size;
      alloc1 = bfd_malloc (size);
      if (alloc1 == NULL)
	goto error_return;
      external_relocs = alloc1;
    }

  if (!elf_link_read_relocs_from_section (abfd, o, rel_hdr,
					  external_relocs,
					  internal_relocs))
    goto error_return;
  if (elf_section_data (o)->rel_hdr2
      && (!elf_link_read_relocs_from_section
	  (abfd, o,
	   elf_section_data (o)->rel_hdr2,
	   ((bfd_byte *) external_relocs) + rel_hdr->sh_size,
	   internal_relocs + (NUM_SHDR_ENTRIES (rel_hdr)
			      * bed->s->int_rels_per_ext_rel))))
    goto error_return;

  /* Cache the results for next time, if we can.  */
  if (keep_memory)
    elf_section_data (o)->relocs = internal_relocs;

  if (alloc1 != NULL)
    free (alloc1);

  /* Don't free alloc2, since if it was allocated we are passing it
     back (under the name of internal_relocs).  */

  return internal_relocs;

 error_return:
  if (alloc1 != NULL)
    free (alloc1);
  if (alloc2 != NULL)
    free (alloc2);
  return NULL;
}

/* Compute the size of, and allocate space for, REL_HDR which is the
   section header for a section containing relocations for O.  */

bfd_boolean
_bfd_elf_link_size_reloc_section (bfd *abfd,
				  Elf_Internal_Shdr *rel_hdr,
				  asection *o)
{
  bfd_size_type reloc_count;
  bfd_size_type num_rel_hashes;

  /* Figure out how many relocations there will be.  */
  if (rel_hdr == &elf_section_data (o)->rel_hdr)
    reloc_count = elf_section_data (o)->rel_count;
  else
    reloc_count = elf_section_data (o)->rel_count2;

  num_rel_hashes = o->reloc_count;
  if (num_rel_hashes < reloc_count)
    num_rel_hashes = reloc_count;

  /* That allows us to calculate the size of the section.  */
  rel_hdr->sh_size = rel_hdr->sh_entsize * reloc_count;

  /* The contents field must last into write_object_contents, so we
     allocate it with bfd_alloc rather than malloc.  Also since we
     cannot be sure that the contents will actually be filled in,
     we zero the allocated space.  */
  rel_hdr->contents = bfd_zalloc (abfd, rel_hdr->sh_size);
  if (rel_hdr->contents == NULL && rel_hdr->sh_size != 0)
    return FALSE;

  /* We only allocate one set of hash entries, so we only do it the
     first time we are called.  */
  if (elf_section_data (o)->rel_hashes == NULL
      && num_rel_hashes)
    {
      struct elf_link_hash_entry **p;

      p = bfd_zmalloc (num_rel_hashes * sizeof (struct elf_link_hash_entry *));
      if (p == NULL)
	return FALSE;

      elf_section_data (o)->rel_hashes = p;
    }

  return TRUE;
}

/* Copy the relocations indicated by the INTERNAL_RELOCS (which
   originated from the section given by INPUT_REL_HDR) to the
   OUTPUT_BFD.  */

bfd_boolean
_bfd_elf_link_output_relocs (bfd *output_bfd,
			     asection *input_section,
			     Elf_Internal_Shdr *input_rel_hdr,
			     Elf_Internal_Rela *internal_relocs,
			     struct elf_link_hash_entry **rel_hash
			       ATTRIBUTE_UNUSED)
{
  Elf_Internal_Rela *irela;
  Elf_Internal_Rela *irelaend;
  bfd_byte *erel;
  Elf_Internal_Shdr *output_rel_hdr;
  asection *output_section;
  unsigned int *rel_countp = NULL;
  const struct elf_backend_data *bed;
  void (*swap_out) (bfd *, const Elf_Internal_Rela *, bfd_byte *);

  output_section = input_section->output_section;
  output_rel_hdr = NULL;

  if (elf_section_data (output_section)->rel_hdr.sh_entsize
      == input_rel_hdr->sh_entsize)
    {
      output_rel_hdr = &elf_section_data (output_section)->rel_hdr;
      rel_countp = &elf_section_data (output_section)->rel_count;
    }
  else if (elf_section_data (output_section)->rel_hdr2
	   && (elf_section_data (output_section)->rel_hdr2->sh_entsize
	       == input_rel_hdr->sh_entsize))
    {
      output_rel_hdr = elf_section_data (output_section)->rel_hdr2;
      rel_countp = &elf_section_data (output_section)->rel_count2;
    }
  else
    {
      (*_bfd_error_handler)
	(_("%B: relocation size mismatch in %B section %A"),
	 output_bfd, input_section->owner, input_section);
      bfd_set_error (bfd_error_wrong_object_format);
      return FALSE;
    }

  bed = get_elf_backend_data (output_bfd);
  if (input_rel_hdr->sh_entsize == bed->s->sizeof_rel)
    swap_out = bed->s->swap_reloc_out;
  else if (input_rel_hdr->sh_entsize == bed->s->sizeof_rela)
    swap_out = bed->s->swap_reloca_out;
  else
    abort ();

  erel = output_rel_hdr->contents;
  erel += *rel_countp * input_rel_hdr->sh_entsize;
  irela = internal_relocs;
  irelaend = irela + (NUM_SHDR_ENTRIES (input_rel_hdr)
		      * bed->s->int_rels_per_ext_rel);
  while (irela < irelaend)
    {
      (*swap_out) (output_bfd, irela, erel);
      irela += bed->s->int_rels_per_ext_rel;
      erel += input_rel_hdr->sh_entsize;
    }

  /* Bump the counter, so that we know where to add the next set of
     relocations.  */
  *rel_countp += NUM_SHDR_ENTRIES (input_rel_hdr);

  return TRUE;
}

/* Make weak undefined symbols in PIE dynamic.  */

bfd_boolean
_bfd_elf_link_hash_fixup_symbol (struct bfd_link_info *info,
				 struct elf_link_hash_entry *h)
{
  if (info->pie
      && h->dynindx == -1
      && h->root.type == bfd_link_hash_undefweak)
    return bfd_elf_link_record_dynamic_symbol (info, h);

  return TRUE;
}

/* Fix up the flags for a symbol.  This handles various cases which
   can only be fixed after all the input files are seen.  This is
   currently called by both adjust_dynamic_symbol and
   assign_sym_version, which is unnecessary but perhaps more robust in
   the face of future changes.  */

bfd_boolean
_bfd_elf_fix_symbol_flags (struct elf_link_hash_entry *h,
			   struct elf_info_failed *eif)
{
  const struct elf_backend_data *bed = NULL;

  /* If this symbol was mentioned in a non-ELF file, try to set
     DEF_REGULAR and REF_REGULAR correctly.  This is the only way to
     permit a non-ELF file to correctly refer to a symbol defined in
     an ELF dynamic object.  */
  if (h->non_elf)
    {
      while (h->root.type == bfd_link_hash_indirect)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;

      if (h->root.type != bfd_link_hash_defined
	  && h->root.type != bfd_link_hash_defweak)
	{
	  h->ref_regular = 1;
	  h->ref_regular_nonweak = 1;
	}
      else
	{
	  if (h->root.u.def.section->owner != NULL
	      && (bfd_get_flavour (h->root.u.def.section->owner)
		  == bfd_target_elf_flavour))
	    {
	      h->ref_regular = 1;
	      h->ref_regular_nonweak = 1;
	    }
	  else
	    h->def_regular = 1;
	}

      if (h->dynindx == -1
	  && (h->def_dynamic
	      || h->ref_dynamic))
	{
	  if (! bfd_elf_link_record_dynamic_symbol (eif->info, h))
	    {
	      eif->failed = TRUE;
	      return FALSE;
	    }
	}
    }
  else
    {
      /* Unfortunately, NON_ELF is only correct if the symbol
	 was first seen in a non-ELF file.  Fortunately, if the symbol
	 was first seen in an ELF file, we're probably OK unless the
	 symbol was defined in a non-ELF file.  Catch that case here.
	 FIXME: We're still in trouble if the symbol was first seen in
	 a dynamic object, and then later in a non-ELF regular object.  */
      if ((h->root.type == bfd_link_hash_defined
	   || h->root.type == bfd_link_hash_defweak)
	  && !h->def_regular
	  && (h->root.u.def.section->owner != NULL
	      ? (bfd_get_flavour (h->root.u.def.section->owner)
		 != bfd_target_elf_flavour)
	      : (bfd_is_abs_section (h->root.u.def.section)
		 && !h->def_dynamic)))
	h->def_regular = 1;
    }

  /* Backend specific symbol fixup.  */
  if (elf_hash_table (eif->info)->dynobj)
    {
      bed = get_elf_backend_data (elf_hash_table (eif->info)->dynobj);
      if (bed->elf_backend_fixup_symbol
	  && !(*bed->elf_backend_fixup_symbol) (eif->info, h))
	return FALSE;
    }

  /* If this is a final link, and the symbol was defined as a common
     symbol in a regular object file, and there was no definition in
     any dynamic object, then the linker will have allocated space for
     the symbol in a common section but the DEF_REGULAR
     flag will not have been set.  */
  if (h->root.type == bfd_link_hash_defined
      && !h->def_regular
      && h->ref_regular
      && !h->def_dynamic
      && (h->root.u.def.section->owner->flags & DYNAMIC) == 0)
    h->def_regular = 1;

  /* If -Bsymbolic was used (which means to bind references to global
     symbols to the definition within the shared object), and this
     symbol was defined in a regular object, then it actually doesn't
     need a PLT entry.  Likewise, if the symbol has non-default
     visibility.  If the symbol has hidden or internal visibility, we
     will force it local.  */
  if (h->needs_plt
      && eif->info->shared
      && is_elf_hash_table (eif->info->hash)
      && (SYMBOLIC_BIND (eif->info, h)
	  || ELF_ST_VISIBILITY (h->other) != STV_DEFAULT)
      && h->def_regular)
    {
      bfd_boolean force_local;

      force_local = (ELF_ST_VISIBILITY (h->other) == STV_INTERNAL
		     || ELF_ST_VISIBILITY (h->other) == STV_HIDDEN);
      (*bed->elf_backend_hide_symbol) (eif->info, h, force_local);
    }

  /* If a weak undefined symbol has non-default visibility, we also
     hide it from the dynamic linker.  */
  if (ELF_ST_VISIBILITY (h->other) != STV_DEFAULT
      && h->root.type == bfd_link_hash_undefweak)
    {
      const struct elf_backend_data *bed;
      bed = get_elf_backend_data (elf_hash_table (eif->info)->dynobj);
      (*bed->elf_backend_hide_symbol) (eif->info, h, TRUE);
    }

  /* If this is a weak defined symbol in a dynamic object, and we know
     the real definition in the dynamic object, copy interesting flags
     over to the real definition.  */
  if (h->u.weakdef != NULL)
    {
      struct elf_link_hash_entry *weakdef;

      weakdef = h->u.weakdef;
      if (h->root.type == bfd_link_hash_indirect)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;

      BFD_ASSERT (h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak);
      BFD_ASSERT (weakdef->root.type == bfd_link_hash_defined
		  || weakdef->root.type == bfd_link_hash_defweak);
      BFD_ASSERT (weakdef->def_dynamic);

      /* If the real definition is defined by a regular object file,
	 don't do anything special.  See the longer description in
	 _bfd_elf_adjust_dynamic_symbol, below.  */
      if (weakdef->def_regular)
	h->u.weakdef = NULL;
      else
	(*bed->elf_backend_copy_indirect_symbol) (eif->info, weakdef,
						  h);
    }

  return TRUE;
}

/* Make the backend pick a good value for a dynamic symbol.  This is
   called via elf_link_hash_traverse, and also calls itself
   recursively.  */

bfd_boolean
_bfd_elf_adjust_dynamic_symbol (struct elf_link_hash_entry *h, void *data)
{
  struct elf_info_failed *eif = data;
  bfd *dynobj;
  const struct elf_backend_data *bed;

  if (! is_elf_hash_table (eif->info->hash))
    return FALSE;

  if (h->root.type == bfd_link_hash_warning)
    {
      h->got = elf_hash_table (eif->info)->init_got_offset;
      h->plt = elf_hash_table (eif->info)->init_plt_offset;

      /* When warning symbols are created, they **replace** the "real"
	 entry in the hash table, thus we never get to see the real
	 symbol in a hash traversal.  So look at it now.  */
      h = (struct elf_link_hash_entry *) h->root.u.i.link;
    }

  /* Ignore indirect symbols.  These are added by the versioning code.  */
  if (h->root.type == bfd_link_hash_indirect)
    return TRUE;

  /* Fix the symbol flags.  */
  if (! _bfd_elf_fix_symbol_flags (h, eif))
    return FALSE;

  /* If this symbol does not require a PLT entry, and it is not
     defined by a dynamic object, or is not referenced by a regular
     object, ignore it.  We do have to handle a weak defined symbol,
     even if no regular object refers to it, if we decided to add it
     to the dynamic symbol table.  FIXME: Do we normally need to worry
     about symbols which are defined by one dynamic object and
     referenced by another one?  */
  if (!h->needs_plt
      && (h->def_regular
	  || !h->def_dynamic
	  || (!h->ref_regular
	      && (h->u.weakdef == NULL || h->u.weakdef->dynindx == -1))))
    {
      h->plt = elf_hash_table (eif->info)->init_plt_offset;
      return TRUE;
    }

  /* If we've already adjusted this symbol, don't do it again.  This
     can happen via a recursive call.  */
  if (h->dynamic_adjusted)
    return TRUE;

  /* Don't look at this symbol again.  Note that we must set this
     after checking the above conditions, because we may look at a
     symbol once, decide not to do anything, and then get called
     recursively later after REF_REGULAR is set below.  */
  h->dynamic_adjusted = 1;

  /* If this is a weak definition, and we know a real definition, and
     the real symbol is not itself defined by a regular object file,
     then get a good value for the real definition.  We handle the
     real symbol first, for the convenience of the backend routine.

     Note that there is a confusing case here.  If the real definition
     is defined by a regular object file, we don't get the real symbol
     from the dynamic object, but we do get the weak symbol.  If the
     processor backend uses a COPY reloc, then if some routine in the
     dynamic object changes the real symbol, we will not see that
     change in the corresponding weak symbol.  This is the way other
     ELF linkers work as well, and seems to be a result of the shared
     library model.

     I will clarify this issue.  Most SVR4 shared libraries define the
     variable _timezone and define timezone as a weak synonym.  The
     tzset call changes _timezone.  If you write
       extern int timezone;
       int _timezone = 5;
       int main () { tzset (); printf ("%d %d\n", timezone, _timezone); }
     you might expect that, since timezone is a synonym for _timezone,
     the same number will print both times.  However, if the processor
     backend uses a COPY reloc, then actually timezone will be copied
     into your process image, and, since you define _timezone
     yourself, _timezone will not.  Thus timezone and _timezone will
     wind up at different memory locations.  The tzset call will set
     _timezone, leaving timezone unchanged.  */

  if (h->u.weakdef != NULL)
    {
      /* If we get to this point, we know there is an implicit
	 reference by a regular object file via the weak symbol H.
	 FIXME: Is this really true?  What if the traversal finds
	 H->U.WEAKDEF before it finds H?  */
      h->u.weakdef->ref_regular = 1;

      if (! _bfd_elf_adjust_dynamic_symbol (h->u.weakdef, eif))
	return FALSE;
    }

  /* If a symbol has no type and no size and does not require a PLT
     entry, then we are probably about to do the wrong thing here: we
     are probably going to create a COPY reloc for an empty object.
     This case can arise when a shared object is built with assembly
     code, and the assembly code fails to set the symbol type.  */
  if (h->size == 0
      && h->type == STT_NOTYPE
      && !h->needs_plt)
    (*_bfd_error_handler)
      (_("warning: type and size of dynamic symbol `%s' are not defined"),
       h->root.root.string);

  dynobj = elf_hash_table (eif->info)->dynobj;
  bed = get_elf_backend_data (dynobj);
  if (! (*bed->elf_backend_adjust_dynamic_symbol) (eif->info, h))
    {
      eif->failed = TRUE;
      return FALSE;
    }

  return TRUE;
}

/* Adjust the dynamic symbol, H, for copy in the dynamic bss section,
   DYNBSS.  */

bfd_boolean
_bfd_elf_adjust_dynamic_copy (struct elf_link_hash_entry *h,
			      asection *dynbss)
{
  unsigned int power_of_two;
  bfd_vma mask;
  asection *sec = h->root.u.def.section;

  /* The section aligment of definition is the maximum alignment
     requirement of symbols defined in the section.  Since we don't
     know the symbol alignment requirement, we start with the
     maximum alignment and check low bits of the symbol address
     for the minimum alignment.  */
  power_of_two = bfd_get_section_alignment (sec->owner, sec);
  mask = ((bfd_vma) 1 << power_of_two) - 1;
  while ((h->root.u.def.value & mask) != 0)
    {
       mask >>= 1;
       --power_of_two;
    }

  if (power_of_two > bfd_get_section_alignment (dynbss->owner,
						dynbss))
    {
      /* Adjust the section alignment if needed.  */
      if (! bfd_set_section_alignment (dynbss->owner, dynbss,
				       power_of_two))
	return FALSE;
    }

  /* We make sure that the symbol will be aligned properly.  */
  dynbss->size = BFD_ALIGN (dynbss->size, mask + 1);

  /* Define the symbol as being at this point in DYNBSS.  */
  h->root.u.def.section = dynbss;
  h->root.u.def.value = dynbss->size;

  /* Increment the size of DYNBSS to make room for the symbol.  */
  dynbss->size += h->size;

  return TRUE;
}

/* Adjust all external symbols pointing into SEC_MERGE sections
   to reflect the object merging within the sections.  */

bfd_boolean
_bfd_elf_link_sec_merge_syms (struct elf_link_hash_entry *h, void *data)
{
  asection *sec;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if ((h->root.type == bfd_link_hash_defined
       || h->root.type == bfd_link_hash_defweak)
      && ((sec = h->root.u.def.section)->flags & SEC_MERGE)
      && sec->sec_info_type == ELF_INFO_TYPE_MERGE)
    {
      bfd *output_bfd = data;

      h->root.u.def.value =
	_bfd_merged_section_offset (output_bfd,
				    &h->root.u.def.section,
				    elf_section_data (sec)->sec_info,
				    h->root.u.def.value);
    }

  return TRUE;
}

/* Returns false if the symbol referred to by H should be considered
   to resolve local to the current module, and true if it should be
   considered to bind dynamically.  */

bfd_boolean
_bfd_elf_dynamic_symbol_p (struct elf_link_hash_entry *h,
			   struct bfd_link_info *info,
			   bfd_boolean ignore_protected)
{
  bfd_boolean binding_stays_local_p;
  const struct elf_backend_data *bed;
  struct elf_link_hash_table *hash_table;

  if (h == NULL)
    return FALSE;

  while (h->root.type == bfd_link_hash_indirect
	 || h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* If it was forced local, then clearly it's not dynamic.  */
  if (h->dynindx == -1)
    return FALSE;
  if (h->forced_local)
    return FALSE;

  /* Identify the cases where name binding rules say that a
     visible symbol resolves locally.  */
  binding_stays_local_p = info->executable || SYMBOLIC_BIND (info, h);

  switch (ELF_ST_VISIBILITY (h->other))
    {
    case STV_INTERNAL:
    case STV_HIDDEN:
      return FALSE;

    case STV_PROTECTED:
      hash_table = elf_hash_table (info);
      if (!is_elf_hash_table (hash_table))
	return FALSE;

      bed = get_elf_backend_data (hash_table->dynobj);

      /* Proper resolution for function pointer equality may require
	 that these symbols perhaps be resolved dynamically, even though
	 we should be resolving them to the current module.  */
      if (!ignore_protected || !bed->is_function_type (h->type))
	binding_stays_local_p = TRUE;
      break;

    default:
      break;
    }

  /* If it isn't defined locally, then clearly it's dynamic.  */
  if (!h->def_regular)
    return TRUE;

  /* Otherwise, the symbol is dynamic if binding rules don't tell
     us that it remains local.  */
  return !binding_stays_local_p;
}

/* Return true if the symbol referred to by H should be considered
   to resolve local to the current module, and false otherwise.  Differs
   from (the inverse of) _bfd_elf_dynamic_symbol_p in the treatment of
   undefined symbols and weak symbols.  */

bfd_boolean
_bfd_elf_symbol_refs_local_p (struct elf_link_hash_entry *h,
			      struct bfd_link_info *info,
			      bfd_boolean local_protected)
{
  const struct elf_backend_data *bed;
  struct elf_link_hash_table *hash_table;

  /* If it's a local sym, of course we resolve locally.  */
  if (h == NULL)
    return TRUE;

  /* Common symbols that become definitions don't get the DEF_REGULAR
     flag set, so test it first, and don't bail out.  */
  if (ELF_COMMON_DEF_P (h))
    /* Do nothing.  */;
  /* If we don't have a definition in a regular file, then we can't
     resolve locally.  The sym is either undefined or dynamic.  */
  else if (!h->def_regular)
    return FALSE;

  /* Forced local symbols resolve locally.  */
  if (h->forced_local)
    return TRUE;

  /* As do non-dynamic symbols.  */
  if (h->dynindx == -1)
    return TRUE;

  /* At this point, we know the symbol is defined and dynamic.  In an
     executable it must resolve locally, likewise when building symbolic
     shared libraries.  */
  if (info->executable || SYMBOLIC_BIND (info, h))
    return TRUE;

  /* Now deal with defined dynamic symbols in shared libraries.  Ones
     with default visibility might not resolve locally.  */
  if (ELF_ST_VISIBILITY (h->other) == STV_DEFAULT)
    return FALSE;

  /* However, STV_HIDDEN or STV_INTERNAL ones must be local.  */
  if (ELF_ST_VISIBILITY (h->other) != STV_PROTECTED)
    return TRUE;

  hash_table = elf_hash_table (info);
  if (!is_elf_hash_table (hash_table))
    return TRUE;

  bed = get_elf_backend_data (hash_table->dynobj);

  /* STV_PROTECTED non-function symbols are local.  */
  if (!bed->is_function_type (h->type))
    return TRUE;

  /* Function pointer equality tests may require that STV_PROTECTED
     symbols be treated as dynamic symbols, even when we know that the
     dynamic linker will resolve them locally.  */
  return local_protected;
}

/* Caches some TLS segment info, and ensures that the TLS segment vma is
   aligned.  Returns the first TLS output section.  */

struct bfd_section *
_bfd_elf_tls_setup (bfd *obfd, struct bfd_link_info *info)
{
  struct bfd_section *sec, *tls;
  unsigned int align = 0;

  for (sec = obfd->sections; sec != NULL; sec = sec->next)
    if ((sec->flags & SEC_THREAD_LOCAL) != 0)
      break;
  tls = sec;

  for (; sec != NULL && (sec->flags & SEC_THREAD_LOCAL) != 0; sec = sec->next)
    if (sec->alignment_power > align)
      align = sec->alignment_power;

  elf_hash_table (info)->tls_sec = tls;

  /* Ensure the alignment of the first section is the largest alignment,
     so that the tls segment starts aligned.  */
  if (tls != NULL)
    tls->alignment_power = align;

  return tls;
}

/* Return TRUE iff this is a non-common, definition of a non-function symbol.  */
static bfd_boolean
is_global_data_symbol_definition (bfd *abfd ATTRIBUTE_UNUSED,
				  Elf_Internal_Sym *sym)
{
  const struct elf_backend_data *bed;

  /* Local symbols do not count, but target specific ones might.  */
  if (ELF_ST_BIND (sym->st_info) != STB_GLOBAL
      && ELF_ST_BIND (sym->st_info) < STB_LOOS)
    return FALSE;

  bed = get_elf_backend_data (abfd);
  /* Function symbols do not count.  */
  if (bed->is_function_type (ELF_ST_TYPE (sym->st_info)))
    return FALSE;

  /* If the section is undefined, then so is the symbol.  */
  if (sym->st_shndx == SHN_UNDEF)
    return FALSE;

  /* If the symbol is defined in the common section, then
     it is a common definition and so does not count.  */
  if (bed->common_definition (sym))
    return FALSE;

  /* If the symbol is in a target specific section then we
     must rely upon the backend to tell us what it is.  */
  if (sym->st_shndx >= SHN_LORESERVE && sym->st_shndx < SHN_ABS)
    /* FIXME - this function is not coded yet:

       return _bfd_is_global_symbol_definition (abfd, sym);

       Instead for now assume that the definition is not global,
       Even if this is wrong, at least the linker will behave
       in the same way that it used to do.  */
    return FALSE;

  return TRUE;
}

/* Search the symbol table of the archive element of the archive ABFD
   whose archive map contains a mention of SYMDEF, and determine if
   the symbol is defined in this element.  */
static bfd_boolean
elf_link_is_defined_archive_symbol (bfd * abfd, carsym * symdef)
{
  Elf_Internal_Shdr * hdr;
  bfd_size_type symcount;
  bfd_size_type extsymcount;
  bfd_size_type extsymoff;
  Elf_Internal_Sym *isymbuf;
  Elf_Internal_Sym *isym;
  Elf_Internal_Sym *isymend;
  bfd_boolean result;

  abfd = _bfd_get_elt_at_filepos (abfd, symdef->file_offset);
  if (abfd == NULL)
    return FALSE;

  if (! bfd_check_format (abfd, bfd_object))
    return FALSE;

  /* If we have already included the element containing this symbol in the
     link then we do not need to include it again.  Just claim that any symbol
     it contains is not a definition, so that our caller will not decide to
     (re)include this element.  */
  if (abfd->archive_pass)
    return FALSE;

  /* Select the appropriate symbol table.  */
  if ((abfd->flags & DYNAMIC) == 0 || elf_dynsymtab (abfd) == 0)
    hdr = &elf_tdata (abfd)->symtab_hdr;
  else
    hdr = &elf_tdata (abfd)->dynsymtab_hdr;

  symcount = hdr->sh_size / get_elf_backend_data (abfd)->s->sizeof_sym;

  /* The sh_info field of the symtab header tells us where the
     external symbols start.  We don't care about the local symbols.  */
  if (elf_bad_symtab (abfd))
    {
      extsymcount = symcount;
      extsymoff = 0;
    }
  else
    {
      extsymcount = symcount - hdr->sh_info;
      extsymoff = hdr->sh_info;
    }

  if (extsymcount == 0)
    return FALSE;

  /* Read in the symbol table.  */
  isymbuf = bfd_elf_get_elf_syms (abfd, hdr, extsymcount, extsymoff,
				  NULL, NULL, NULL);
  if (isymbuf == NULL)
    return FALSE;

  /* Scan the symbol table looking for SYMDEF.  */
  result = FALSE;
  for (isym = isymbuf, isymend = isymbuf + extsymcount; isym < isymend; isym++)
    {
      const char *name;

      name = bfd_elf_string_from_elf_section (abfd, hdr->sh_link,
					      isym->st_name);
      if (name == NULL)
	break;

      if (strcmp (name, symdef->name) == 0)
	{
	  result = is_global_data_symbol_definition (abfd, isym);
	  break;
	}
    }

  free (isymbuf);

  return result;
}

/* Add an entry to the .dynamic table.  */

bfd_boolean
_bfd_elf_add_dynamic_entry (struct bfd_link_info *info,
			    bfd_vma tag,
			    bfd_vma val)
{
  struct elf_link_hash_table *hash_table;
  const struct elf_backend_data *bed;
  asection *s;
  bfd_size_type newsize;
  bfd_byte *newcontents;
  Elf_Internal_Dyn dyn;

  hash_table = elf_hash_table (info);
  if (! is_elf_hash_table (hash_table))
    return FALSE;

  bed = get_elf_backend_data (hash_table->dynobj);
  s = bfd_get_section_by_name (hash_table->dynobj, ".dynamic");
  BFD_ASSERT (s != NULL);

  newsize = s->size + bed->s->sizeof_dyn;
  newcontents = bfd_realloc (s->contents, newsize);
  if (newcontents == NULL)
    return FALSE;

  dyn.d_tag = tag;
  dyn.d_un.d_val = val;
  bed->s->swap_dyn_out (hash_table->dynobj, &dyn, newcontents + s->size);

  s->size = newsize;
  s->contents = newcontents;

  return TRUE;
}

/* Add a DT_NEEDED entry for this dynamic object if DO_IT is true,
   otherwise just check whether one already exists.  Returns -1 on error,
   1 if a DT_NEEDED tag already exists, and 0 on success.  */

static int
elf_add_dt_needed_tag (bfd *abfd,
		       struct bfd_link_info *info,
		       const char *soname,
		       bfd_boolean do_it)
{
  struct elf_link_hash_table *hash_table;
  bfd_size_type oldsize;
  bfd_size_type strindex;

  if (!_bfd_elf_link_create_dynstrtab (abfd, info))
    return -1;

  hash_table = elf_hash_table (info);
  oldsize = _bfd_elf_strtab_size (hash_table->dynstr);
  strindex = _bfd_elf_strtab_add (hash_table->dynstr, soname, FALSE);
  if (strindex == (bfd_size_type) -1)
    return -1;

  if (oldsize == _bfd_elf_strtab_size (hash_table->dynstr))
    {
      asection *sdyn;
      const struct elf_backend_data *bed;
      bfd_byte *extdyn;

      bed = get_elf_backend_data (hash_table->dynobj);
      sdyn = bfd_get_section_by_name (hash_table->dynobj, ".dynamic");
      if (sdyn != NULL)
	for (extdyn = sdyn->contents;
	     extdyn < sdyn->contents + sdyn->size;
	     extdyn += bed->s->sizeof_dyn)
	  {
	    Elf_Internal_Dyn dyn;

	    bed->s->swap_dyn_in (hash_table->dynobj, extdyn, &dyn);
	    if (dyn.d_tag == DT_NEEDED
		&& dyn.d_un.d_val == strindex)
	      {
		_bfd_elf_strtab_delref (hash_table->dynstr, strindex);
		return 1;
	      }
	  }
    }

  if (do_it)
    {
      if (!_bfd_elf_link_create_dynamic_sections (hash_table->dynobj, info))
	return -1;

      if (!_bfd_elf_add_dynamic_entry (info, DT_NEEDED, strindex))
	return -1;
    }
  else
    /* We were just checking for existence of the tag.  */
    _bfd_elf_strtab_delref (hash_table->dynstr, strindex);

  return 0;
}

/* Sort symbol by value and section.  */
static int
elf_sort_symbol (const void *arg1, const void *arg2)
{
  const struct elf_link_hash_entry *h1;
  const struct elf_link_hash_entry *h2;
  bfd_signed_vma vdiff;

  h1 = *(const struct elf_link_hash_entry **) arg1;
  h2 = *(const struct elf_link_hash_entry **) arg2;
  vdiff = h1->root.u.def.value - h2->root.u.def.value;
  if (vdiff != 0)
    return vdiff > 0 ? 1 : -1;
  else
    {
      long sdiff = h1->root.u.def.section->id - h2->root.u.def.section->id;
      if (sdiff != 0)
	return sdiff > 0 ? 1 : -1;
    }
  return 0;
}

/* This function is used to adjust offsets into .dynstr for
   dynamic symbols.  This is called via elf_link_hash_traverse.  */

static bfd_boolean
elf_adjust_dynstr_offsets (struct elf_link_hash_entry *h, void *data)
{
  struct elf_strtab_hash *dynstr = data;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (h->dynindx != -1)
    h->dynstr_index = _bfd_elf_strtab_offset (dynstr, h->dynstr_index);
  return TRUE;
}

/* Assign string offsets in .dynstr, update all structures referencing
   them.  */

static bfd_boolean
elf_finalize_dynstr (bfd *output_bfd, struct bfd_link_info *info)
{
  struct elf_link_hash_table *hash_table = elf_hash_table (info);
  struct elf_link_local_dynamic_entry *entry;
  struct elf_strtab_hash *dynstr = hash_table->dynstr;
  bfd *dynobj = hash_table->dynobj;
  asection *sdyn;
  bfd_size_type size;
  const struct elf_backend_data *bed;
  bfd_byte *extdyn;

  _bfd_elf_strtab_finalize (dynstr);
  size = _bfd_elf_strtab_size (dynstr);

  bed = get_elf_backend_data (dynobj);
  sdyn = bfd_get_section_by_name (dynobj, ".dynamic");
  BFD_ASSERT (sdyn != NULL);

  /* Update all .dynamic entries referencing .dynstr strings.  */
  for (extdyn = sdyn->contents;
       extdyn < sdyn->contents + sdyn->size;
       extdyn += bed->s->sizeof_dyn)
    {
      Elf_Internal_Dyn dyn;

      bed->s->swap_dyn_in (dynobj, extdyn, &dyn);
      switch (dyn.d_tag)
	{
	case DT_STRSZ:
	  dyn.d_un.d_val = size;
	  break;
	case DT_NEEDED:
	case DT_SONAME:
	case DT_RPATH:
	case DT_RUNPATH:
	case DT_FILTER:
	case DT_AUXILIARY:
	  dyn.d_un.d_val = _bfd_elf_strtab_offset (dynstr, dyn.d_un.d_val);
	  break;
	default:
	  continue;
	}
      bed->s->swap_dyn_out (dynobj, &dyn, extdyn);
    }

  /* Now update local dynamic symbols.  */
  for (entry = hash_table->dynlocal; entry ; entry = entry->next)
    entry->isym.st_name = _bfd_elf_strtab_offset (dynstr,
						  entry->isym.st_name);

  /* And the rest of dynamic symbols.  */
  elf_link_hash_traverse (hash_table, elf_adjust_dynstr_offsets, dynstr);

  /* Adjust version definitions.  */
  if (elf_tdata (output_bfd)->cverdefs)
    {
      asection *s;
      bfd_byte *p;
      bfd_size_type i;
      Elf_Internal_Verdef def;
      Elf_Internal_Verdaux defaux;

      s = bfd_get_section_by_name (dynobj, ".gnu.version_d");
      p = s->contents;
      do
	{
	  _bfd_elf_swap_verdef_in (output_bfd, (Elf_External_Verdef *) p,
				   &def);
	  p += sizeof (Elf_External_Verdef);
	  if (def.vd_aux != sizeof (Elf_External_Verdef))
	    continue;
	  for (i = 0; i < def.vd_cnt; ++i)
	    {
	      _bfd_elf_swap_verdaux_in (output_bfd,
					(Elf_External_Verdaux *) p, &defaux);
	      defaux.vda_name = _bfd_elf_strtab_offset (dynstr,
							defaux.vda_name);
	      _bfd_elf_swap_verdaux_out (output_bfd,
					 &defaux, (Elf_External_Verdaux *) p);
	      p += sizeof (Elf_External_Verdaux);
	    }
	}
      while (def.vd_next);
    }

  /* Adjust version references.  */
  if (elf_tdata (output_bfd)->verref)
    {
      asection *s;
      bfd_byte *p;
      bfd_size_type i;
      Elf_Internal_Verneed need;
      Elf_Internal_Vernaux needaux;

      s = bfd_get_section_by_name (dynobj, ".gnu.version_r");
      p = s->contents;
      do
	{
	  _bfd_elf_swap_verneed_in (output_bfd, (Elf_External_Verneed *) p,
				    &need);
	  need.vn_file = _bfd_elf_strtab_offset (dynstr, need.vn_file);
	  _bfd_elf_swap_verneed_out (output_bfd, &need,
				     (Elf_External_Verneed *) p);
	  p += sizeof (Elf_External_Verneed);
	  for (i = 0; i < need.vn_cnt; ++i)
	    {
	      _bfd_elf_swap_vernaux_in (output_bfd,
					(Elf_External_Vernaux *) p, &needaux);
	      needaux.vna_name = _bfd_elf_strtab_offset (dynstr,
							 needaux.vna_name);
	      _bfd_elf_swap_vernaux_out (output_bfd,
					 &needaux,
					 (Elf_External_Vernaux *) p);
	      p += sizeof (Elf_External_Vernaux);
	    }
	}
      while (need.vn_next);
    }

  return TRUE;
}

/* Return TRUE iff relocations for INPUT are compatible with OUTPUT.
   The default is to only match when the INPUT and OUTPUT are exactly
   the same target.  */

bfd_boolean
_bfd_elf_default_relocs_compatible (const bfd_target *input,
				    const bfd_target *output)
{
  return input == output;
}

/* Return TRUE iff relocations for INPUT are compatible with OUTPUT.
   This version is used when different targets for the same architecture
   are virtually identical.  */

bfd_boolean
_bfd_elf_relocs_compatible (const bfd_target *input,
			    const bfd_target *output)
{
  const struct elf_backend_data *obed, *ibed;

  if (input == output)
    return TRUE;

  ibed = xvec_get_elf_backend_data (input);
  obed = xvec_get_elf_backend_data (output);

  if (ibed->arch != obed->arch)
    return FALSE;

  /* If both backends are using this function, deem them compatible.  */
  return ibed->relocs_compatible == obed->relocs_compatible;
}

/* Add symbols from an ELF object file to the linker hash table.  */

static bfd_boolean
elf_link_add_object_symbols (bfd *abfd, struct bfd_link_info *info)
{
  Elf_Internal_Shdr *hdr;
  bfd_size_type symcount;
  bfd_size_type extsymcount;
  bfd_size_type extsymoff;
  struct elf_link_hash_entry **sym_hash;
  bfd_boolean dynamic;
  Elf_External_Versym *extversym = NULL;
  Elf_External_Versym *ever;
  struct elf_link_hash_entry *weaks;
  struct elf_link_hash_entry **nondeflt_vers = NULL;
  bfd_size_type nondeflt_vers_cnt = 0;
  Elf_Internal_Sym *isymbuf = NULL;
  Elf_Internal_Sym *isym;
  Elf_Internal_Sym *isymend;
  const struct elf_backend_data *bed;
  bfd_boolean add_needed;
  struct elf_link_hash_table *htab;
  bfd_size_type amt;
  void *alloc_mark = NULL;
  struct bfd_hash_entry **old_table = NULL;
  unsigned int old_size = 0;
  unsigned int old_count = 0;
  void *old_tab = NULL;
  void *old_hash;
  void *old_ent;
  struct bfd_link_hash_entry *old_undefs = NULL;
  struct bfd_link_hash_entry *old_undefs_tail = NULL;
  long old_dynsymcount = 0;
  size_t tabsize = 0;
  size_t hashsize = 0;

  htab = elf_hash_table (info);
  bed = get_elf_backend_data (abfd);

  if ((abfd->flags & DYNAMIC) == 0)
    dynamic = FALSE;
  else
    {
      dynamic = TRUE;

      /* You can't use -r against a dynamic object.  Also, there's no
	 hope of using a dynamic object which does not exactly match
	 the format of the output file.  */
      if (info->relocatable
	  || !is_elf_hash_table (htab)
	  || htab->root.creator != abfd->xvec)
	{
	  if (info->relocatable)
	    bfd_set_error (bfd_error_invalid_operation);
	  else
	    bfd_set_error (bfd_error_wrong_format);
	  goto error_return;
	}
    }

  /* As a GNU extension, any input sections which are named
     .gnu.warning.SYMBOL are treated as warning symbols for the given
     symbol.  This differs from .gnu.warning sections, which generate
     warnings when they are included in an output file.  */
  if (info->executable)
    {
      asection *s;

      for (s = abfd->sections; s != NULL; s = s->next)
	{
	  const char *name;

	  name = bfd_get_section_name (abfd, s);
	  if (CONST_STRNEQ (name, ".gnu.warning."))
	    {
	      char *msg;
	      bfd_size_type sz;

	      name += sizeof ".gnu.warning." - 1;

	      /* If this is a shared object, then look up the symbol
		 in the hash table.  If it is there, and it is already
		 been defined, then we will not be using the entry
		 from this shared object, so we don't need to warn.
		 FIXME: If we see the definition in a regular object
		 later on, we will warn, but we shouldn't.  The only
		 fix is to keep track of what warnings we are supposed
		 to emit, and then handle them all at the end of the
		 link.  */
	      if (dynamic)
		{
		  struct elf_link_hash_entry *h;

		  h = elf_link_hash_lookup (htab, name, FALSE, FALSE, TRUE);

		  /* FIXME: What about bfd_link_hash_common?  */
		  if (h != NULL
		      && (h->root.type == bfd_link_hash_defined
			  || h->root.type == bfd_link_hash_defweak))
		    {
		      /* We don't want to issue this warning.  Clobber
			 the section size so that the warning does not
			 get copied into the output file.  */
		      s->size = 0;
		      continue;
		    }
		}

	      sz = s->size;
	      msg = bfd_alloc (abfd, sz + 1);
	      if (msg == NULL)
		goto error_return;

	      if (! bfd_get_section_contents (abfd, s, msg, 0, sz))
		goto error_return;

	      msg[sz] = '\0';

	      if (! (_bfd_generic_link_add_one_symbol
		     (info, abfd, name, BSF_WARNING, s, 0, msg,
		      FALSE, bed->collect, NULL)))
		goto error_return;

	      if (! info->relocatable)
		{
		  /* Clobber the section size so that the warning does
		     not get copied into the output file.  */
		  s->size = 0;

		  /* Also set SEC_EXCLUDE, so that symbols defined in
		     the warning section don't get copied to the output.  */
		  s->flags |= SEC_EXCLUDE;
		}
	    }
	}
    }

  add_needed = TRUE;
  if (! dynamic)
    {
      /* If we are creating a shared library, create all the dynamic
	 sections immediately.  We need to attach them to something,
	 so we attach them to this BFD, provided it is the right
	 format.  FIXME: If there are no input BFD's of the same
	 format as the output, we can't make a shared library.  */
      if (info->shared
	  && is_elf_hash_table (htab)
	  && htab->root.creator == abfd->xvec
	  && !htab->dynamic_sections_created)
	{
	  if (! _bfd_elf_link_create_dynamic_sections (abfd, info))
	    goto error_return;
	}
    }
  else if (!is_elf_hash_table (htab))
    goto error_return;
  else
    {
      asection *s;
      const char *soname = NULL;
      struct bfd_link_needed_list *rpath = NULL, *runpath = NULL;
      int ret;

      /* ld --just-symbols and dynamic objects don't mix very well.
	 ld shouldn't allow it.  */
      if ((s = abfd->sections) != NULL
	  && s->sec_info_type == ELF_INFO_TYPE_JUST_SYMS)
	abort ();

      /* If this dynamic lib was specified on the command line with
	 --as-needed in effect, then we don't want to add a DT_NEEDED
	 tag unless the lib is actually used.  Similary for libs brought
	 in by another lib's DT_NEEDED.  When --no-add-needed is used
	 on a dynamic lib, we don't want to add a DT_NEEDED entry for
	 any dynamic library in DT_NEEDED tags in the dynamic lib at
	 all.  */
      add_needed = (elf_dyn_lib_class (abfd)
		    & (DYN_AS_NEEDED | DYN_DT_NEEDED
		       | DYN_NO_NEEDED)) == 0;

      s = bfd_get_section_by_name (abfd, ".dynamic");
      if (s != NULL)
	{
	  bfd_byte *dynbuf;
	  bfd_byte *extdyn;
	  int elfsec;
	  unsigned long shlink;

	  if (!bfd_malloc_and_get_section (abfd, s, &dynbuf))
	    goto error_free_dyn;

	  elfsec = _bfd_elf_section_from_bfd_section (abfd, s);
	  if (elfsec == -1)
	    goto error_free_dyn;
	  shlink = elf_elfsections (abfd)[elfsec]->sh_link;

	  for (extdyn = dynbuf;
	       extdyn < dynbuf + s->size;
	       extdyn += bed->s->sizeof_dyn)
	    {
	      Elf_Internal_Dyn dyn;

	      bed->s->swap_dyn_in (abfd, extdyn, &dyn);
	      if (dyn.d_tag == DT_SONAME)
		{
		  unsigned int tagv = dyn.d_un.d_val;
		  soname = bfd_elf_string_from_elf_section (abfd, shlink, tagv);
		  if (soname == NULL)
		    goto error_free_dyn;
		}
	      if (dyn.d_tag == DT_NEEDED)
		{
		  struct bfd_link_needed_list *n, **pn;
		  char *fnm, *anm;
		  unsigned int tagv = dyn.d_un.d_val;

		  amt = sizeof (struct bfd_link_needed_list);
		  n = bfd_alloc (abfd, amt);
		  fnm = bfd_elf_string_from_elf_section (abfd, shlink, tagv);
		  if (n == NULL || fnm == NULL)
		    goto error_free_dyn;
		  amt = strlen (fnm) + 1;
		  anm = bfd_alloc (abfd, amt);
		  if (anm == NULL)
		    goto error_free_dyn;
		  memcpy (anm, fnm, amt);
		  n->name = anm;
		  n->by = abfd;
		  n->next = NULL;
		  for (pn = &htab->needed; *pn != NULL; pn = &(*pn)->next)
		    ;
		  *pn = n;
		}
	      if (dyn.d_tag == DT_RUNPATH)
		{
		  struct bfd_link_needed_list *n, **pn;
		  char *fnm, *anm;
		  unsigned int tagv = dyn.d_un.d_val;

		  amt = sizeof (struct bfd_link_needed_list);
		  n = bfd_alloc (abfd, amt);
		  fnm = bfd_elf_string_from_elf_section (abfd, shlink, tagv);
		  if (n == NULL || fnm == NULL)
		    goto error_free_dyn;
		  amt = strlen (fnm) + 1;
		  anm = bfd_alloc (abfd, amt);
		  if (anm == NULL)
		    goto error_free_dyn;
		  memcpy (anm, fnm, amt);
		  n->name = anm;
		  n->by = abfd;
		  n->next = NULL;
		  for (pn = & runpath;
		       *pn != NULL;
		       pn = &(*pn)->next)
		    ;
		  *pn = n;
		}
	      /* Ignore DT_RPATH if we have seen DT_RUNPATH.  */
	      if (!runpath && dyn.d_tag == DT_RPATH)
		{
		  struct bfd_link_needed_list *n, **pn;
		  char *fnm, *anm;
		  unsigned int tagv = dyn.d_un.d_val;

		  amt = sizeof (struct bfd_link_needed_list);
		  n = bfd_alloc (abfd, amt);
		  fnm = bfd_elf_string_from_elf_section (abfd, shlink, tagv);
		  if (n == NULL || fnm == NULL)
		    goto error_free_dyn;
		  amt = strlen (fnm) + 1;
		  anm = bfd_alloc (abfd, amt);
		  if (anm == NULL)
		    {
		    error_free_dyn:
		      free (dynbuf);
		      goto error_return;
		    }
		  memcpy (anm, fnm, amt);
		  n->name = anm;
		  n->by = abfd;
		  n->next = NULL;
		  for (pn = & rpath;
		       *pn != NULL;
		       pn = &(*pn)->next)
		    ;
		  *pn = n;
		}
	    }

	  free (dynbuf);
	}

      /* DT_RUNPATH overrides DT_RPATH.  Do _NOT_ bfd_release, as that
	 frees all more recently bfd_alloc'd blocks as well.  */
      if (runpath)
	rpath = runpath;

      if (rpath)
	{
	  struct bfd_link_needed_list **pn;
	  for (pn = &htab->runpath; *pn != NULL; pn = &(*pn)->next)
	    ;
	  *pn = rpath;
	}

      /* We do not want to include any of the sections in a dynamic
	 object in the output file.  We hack by simply clobbering the
	 list of sections in the BFD.  This could be handled more
	 cleanly by, say, a new section flag; the existing
	 SEC_NEVER_LOAD flag is not the one we want, because that one
	 still implies that the section takes up space in the output
	 file.  */
      bfd_section_list_clear (abfd);

      /* Find the name to use in a DT_NEEDED entry that refers to this
	 object.  If the object has a DT_SONAME entry, we use it.
	 Otherwise, if the generic linker stuck something in
	 elf_dt_name, we use that.  Otherwise, we just use the file
	 name.  */
      if (soname == NULL || *soname == '\0')
	{
	  soname = elf_dt_name (abfd);
	  if (soname == NULL || *soname == '\0')
	    soname = bfd_get_filename (abfd);
	}

      /* Save the SONAME because sometimes the linker emulation code
	 will need to know it.  */
      elf_dt_name (abfd) = soname;

      ret = elf_add_dt_needed_tag (abfd, info, soname, add_needed);
      if (ret < 0)
	goto error_return;

      /* If we have already included this dynamic object in the
	 link, just ignore it.  There is no reason to include a
	 particular dynamic object more than once.  */
      if (ret > 0)
	return TRUE;
    }

  /* If this is a dynamic object, we always link against the .dynsym
     symbol table, not the .symtab symbol table.  The dynamic linker
     will only see the .dynsym symbol table, so there is no reason to
     look at .symtab for a dynamic object.  */

  if (! dynamic || elf_dynsymtab (abfd) == 0)
    hdr = &elf_tdata (abfd)->symtab_hdr;
  else
    hdr = &elf_tdata (abfd)->dynsymtab_hdr;

  symcount = hdr->sh_size / bed->s->sizeof_sym;

  /* The sh_info field of the symtab header tells us where the
     external symbols start.  We don't care about the local symbols at
     this point.  */
  if (elf_bad_symtab (abfd))
    {
      extsymcount = symcount;
      extsymoff = 0;
    }
  else
    {
      extsymcount = symcount - hdr->sh_info;
      extsymoff = hdr->sh_info;
    }

  sym_hash = NULL;
  if (extsymcount != 0)
    {
      isymbuf = bfd_elf_get_elf_syms (abfd, hdr, extsymcount, extsymoff,
				      NULL, NULL, NULL);
      if (isymbuf == NULL)
	goto error_return;

      /* We store a pointer to the hash table entry for each external
	 symbol.  */
      amt = extsymcount * sizeof (struct elf_link_hash_entry *);
      sym_hash = bfd_alloc (abfd, amt);
      if (sym_hash == NULL)
	goto error_free_sym;
      elf_sym_hashes (abfd) = sym_hash;
    }

  if (dynamic)
    {
      /* Read in any version definitions.  */
      if (!_bfd_elf_slurp_version_tables (abfd,
					  info->default_imported_symver))
	goto error_free_sym;

      /* Read in the symbol versions, but don't bother to convert them
	 to internal format.  */
      if (elf_dynversym (abfd) != 0)
	{
	  Elf_Internal_Shdr *versymhdr;

	  versymhdr = &elf_tdata (abfd)->dynversym_hdr;
	  extversym = bfd_malloc (versymhdr->sh_size);
	  if (extversym == NULL)
	    goto error_free_sym;
	  amt = versymhdr->sh_size;
	  if (bfd_seek (abfd, versymhdr->sh_offset, SEEK_SET) != 0
	      || bfd_bread (extversym, amt, abfd) != amt)
	    goto error_free_vers;
	}
    }

  /* If we are loading an as-needed shared lib, save the symbol table
     state before we start adding symbols.  If the lib turns out
     to be unneeded, restore the state.  */
  if ((elf_dyn_lib_class (abfd) & DYN_AS_NEEDED) != 0)
    {
      unsigned int i;
      size_t entsize;

      for (entsize = 0, i = 0; i < htab->root.table.size; i++)
	{
	  struct bfd_hash_entry *p;
	  struct elf_link_hash_entry *h;

	  for (p = htab->root.table.table[i]; p != NULL; p = p->next)
	    {
	      h = (struct elf_link_hash_entry *) p;
	      entsize += htab->root.table.entsize;
	      if (h->root.type == bfd_link_hash_warning)
		entsize += htab->root.table.entsize;
	    }
	}

      tabsize = htab->root.table.size * sizeof (struct bfd_hash_entry *);
      hashsize = extsymcount * sizeof (struct elf_link_hash_entry *);
      old_tab = bfd_malloc (tabsize + entsize + hashsize);
      if (old_tab == NULL)
	goto error_free_vers;

      /* Remember the current objalloc pointer, so that all mem for
	 symbols added can later be reclaimed.  */
      alloc_mark = bfd_hash_allocate (&htab->root.table, 1);
      if (alloc_mark == NULL)
	goto error_free_vers;

      /* Make a special call to the linker "notice" function to
	 tell it that we are about to handle an as-needed lib.  */
      if (!(*info->callbacks->notice) (info, NULL, abfd, NULL,
				       notice_as_needed))
	return FALSE;


      /* Clone the symbol table and sym hashes.  Remember some
	 pointers into the symbol table, and dynamic symbol count.  */
      old_hash = (char *) old_tab + tabsize;
      old_ent = (char *) old_hash + hashsize;
      memcpy (old_tab, htab->root.table.table, tabsize);
      memcpy (old_hash, sym_hash, hashsize);
      old_undefs = htab->root.undefs;
      old_undefs_tail = htab->root.undefs_tail;
      old_table = htab->root.table.table;
      old_size = htab->root.table.size;
      old_count = htab->root.table.count;
      old_dynsymcount = htab->dynsymcount;

      for (i = 0; i < htab->root.table.size; i++)
	{
	  struct bfd_hash_entry *p;
	  struct elf_link_hash_entry *h;

	  for (p = htab->root.table.table[i]; p != NULL; p = p->next)
	    {
	      memcpy (old_ent, p, htab->root.table.entsize);
	      old_ent = (char *) old_ent + htab->root.table.entsize;
	      h = (struct elf_link_hash_entry *) p;
	      if (h->root.type == bfd_link_hash_warning)
		{
		  memcpy (old_ent, h->root.u.i.link, htab->root.table.entsize);
		  old_ent = (char *) old_ent + htab->root.table.entsize;
		}
	    }
	}
    }

  weaks = NULL;
  ever = extversym != NULL ? extversym + extsymoff : NULL;
  for (isym = isymbuf, isymend = isymbuf + extsymcount;
       isym < isymend;
       isym++, sym_hash++, ever = (ever != NULL ? ever + 1 : NULL))
    {
      int bind;
      bfd_vma value;
      asection *sec, *new_sec;
      flagword flags;
      const char *name;
      struct elf_link_hash_entry *h;
      bfd_boolean definition;
      bfd_boolean size_change_ok;
      bfd_boolean type_change_ok;
      bfd_boolean new_weakdef;
      bfd_boolean override;
      bfd_boolean common;
      unsigned int old_alignment;
      bfd *old_bfd;

      override = FALSE;

      flags = BSF_NO_FLAGS;
      sec = NULL;
      value = isym->st_value;
      *sym_hash = NULL;
      common = bed->common_definition (isym);

      bind = ELF_ST_BIND (isym->st_info);
      if (bind == STB_LOCAL)
	{
	  /* This should be impossible, since ELF requires that all
	     global symbols follow all local symbols, and that sh_info
	     point to the first global symbol.  Unfortunately, Irix 5
	     screws this up.  */
	  continue;
	}
      else if (bind == STB_GLOBAL)
	{
	  if (isym->st_shndx != SHN_UNDEF && !common)
	    flags = BSF_GLOBAL;
	}
      else if (bind == STB_WEAK)
	flags = BSF_WEAK;
      else
	{
	  /* Leave it up to the processor backend.  */
	}

      if (isym->st_shndx == SHN_UNDEF)
	sec = bfd_und_section_ptr;
      else if (isym->st_shndx < SHN_LORESERVE
	       || isym->st_shndx > SHN_HIRESERVE)
	{
	  sec = bfd_section_from_elf_index (abfd, isym->st_shndx);
	  if (sec == NULL)
	    sec = bfd_abs_section_ptr;
	  else if (sec->kept_section)
	    {
	      /* Symbols from discarded section are undefined.  We keep
		 its visibility.  */
	      sec = bfd_und_section_ptr;
	      isym->st_shndx = SHN_UNDEF;
	    }
	  else if ((abfd->flags & (EXEC_P | DYNAMIC)) != 0)
	    value -= sec->vma;
	}
      else if (isym->st_shndx == SHN_ABS)
	sec = bfd_abs_section_ptr;
      else if (isym->st_shndx == SHN_COMMON)
	{
	  sec = bfd_com_section_ptr;
	  /* What ELF calls the size we call the value.  What ELF
	     calls the value we call the alignment.  */
	  value = isym->st_size;
	}
      else
	{
	  /* Leave it up to the processor backend.  */
	}

      name = bfd_elf_string_from_elf_section (abfd, hdr->sh_link,
					      isym->st_name);
      if (name == NULL)
	goto error_free_vers;

      if (isym->st_shndx == SHN_COMMON
	  && ELF_ST_TYPE (isym->st_info) == STT_TLS
	  && !info->relocatable)
	{
	  asection *tcomm = bfd_get_section_by_name (abfd, ".tcommon");

	  if (tcomm == NULL)
	    {
	      tcomm = bfd_make_section_with_flags (abfd, ".tcommon",
						   (SEC_ALLOC
						    | SEC_IS_COMMON
						    | SEC_LINKER_CREATED
						    | SEC_THREAD_LOCAL));
	      if (tcomm == NULL)
		goto error_free_vers;
	    }
	  sec = tcomm;
	}
      else if (bed->elf_add_symbol_hook)
	{
	  if (! (*bed->elf_add_symbol_hook) (abfd, info, isym, &name, &flags,
					     &sec, &value))
	    goto error_free_vers;

	  /* The hook function sets the name to NULL if this symbol
	     should be skipped for some reason.  */
	  if (name == NULL)
	    continue;
	}

      /* Sanity check that all possibilities were handled.  */
      if (sec == NULL)
	{
	  bfd_set_error (bfd_error_bad_value);
	  goto error_free_vers;
	}

      if (bfd_is_und_section (sec)
	  || bfd_is_com_section (sec))
	definition = FALSE;
      else
	definition = TRUE;

      size_change_ok = FALSE;
      type_change_ok = bed->type_change_ok;
      old_alignment = 0;
      old_bfd = NULL;
      new_sec = sec;

      if (is_elf_hash_table (htab))
	{
	  Elf_Internal_Versym iver;
	  unsigned int vernum = 0;
	  bfd_boolean skip;

	  if (ever == NULL)
	    {
	      if (info->default_imported_symver)
		/* Use the default symbol version created earlier.  */
		iver.vs_vers = elf_tdata (abfd)->cverdefs;
	      else
		iver.vs_vers = 0;
	    }
	  else
	    _bfd_elf_swap_versym_in (abfd, ever, &iver);

	  vernum = iver.vs_vers & VERSYM_VERSION;

	  /* If this is a hidden symbol, or if it is not version
	     1, we append the version name to the symbol name.
	     However, we do not modify a non-hidden absolute symbol
	     if it is not a function, because it might be the version
	     symbol itself.  FIXME: What if it isn't?  */
	  if ((iver.vs_vers & VERSYM_HIDDEN) != 0
	      || (vernum > 1
		  && (!bfd_is_abs_section (sec)
		      || bed->is_function_type (ELF_ST_TYPE (isym->st_info)))))
	    {
	      const char *verstr;
	      size_t namelen, verlen, newlen;
	      char *newname, *p;

	      if (isym->st_shndx != SHN_UNDEF)
		{
		  if (vernum > elf_tdata (abfd)->cverdefs)
		    verstr = NULL;
		  else if (vernum > 1)
		    verstr =
		      elf_tdata (abfd)->verdef[vernum - 1].vd_nodename;
		  else
		    verstr = "";

		  if (verstr == NULL)
		    {
		      (*_bfd_error_handler)
			(_("%B: %s: invalid version %u (max %d)"),
			 abfd, name, vernum,
			 elf_tdata (abfd)->cverdefs);
		      bfd_set_error (bfd_error_bad_value);
		      goto error_free_vers;
		    }
		}
	      else
		{
		  /* We cannot simply test for the number of
		     entries in the VERNEED section since the
		     numbers for the needed versions do not start
		     at 0.  */
		  Elf_Internal_Verneed *t;

		  verstr = NULL;
		  for (t = elf_tdata (abfd)->verref;
		       t != NULL;
		       t = t->vn_nextref)
		    {
		      Elf_Internal_Vernaux *a;

		      for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
			{
			  if (a->vna_other == vernum)
			    {
			      verstr = a->vna_nodename;
			      break;
			    }
			}
		      if (a != NULL)
			break;
		    }
		  if (verstr == NULL)
		    {
		      (*_bfd_error_handler)
			(_("%B: %s: invalid needed version %d"),
			 abfd, name, vernum);
		      bfd_set_error (bfd_error_bad_value);
		      goto error_free_vers;
		    }
		}

	      namelen = strlen (name);
	      verlen = strlen (verstr);
	      newlen = namelen + verlen + 2;
	      if ((iver.vs_vers & VERSYM_HIDDEN) == 0
		  && isym->st_shndx != SHN_UNDEF)
		++newlen;

	      newname = bfd_hash_allocate (&htab->root.table, newlen);
	      if (newname == NULL)
		goto error_free_vers;
	      memcpy (newname, name, namelen);
	      p = newname + namelen;
	      *p++ = ELF_VER_CHR;
	      /* If this is a defined non-hidden version symbol,
		 we add another @ to the name.  This indicates the
		 default version of the symbol.  */
	      if ((iver.vs_vers & VERSYM_HIDDEN) == 0
		  && isym->st_shndx != SHN_UNDEF)
		*p++ = ELF_VER_CHR;
	      memcpy (p, verstr, verlen + 1);

	      name = newname;
	    }

	  if (!_bfd_elf_merge_symbol (abfd, info, name, isym, &sec,
				      &value, &old_alignment,
				      sym_hash, &skip, &override,
				      &type_change_ok, &size_change_ok))
	    goto error_free_vers;

	  if (skip)
	    continue;

	  if (override)
	    definition = FALSE;

	  h = *sym_hash;
	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  /* Remember the old alignment if this is a common symbol, so
	     that we don't reduce the alignment later on.  We can't
	     check later, because _bfd_generic_link_add_one_symbol
	     will set a default for the alignment which we want to
	     override. We also remember the old bfd where the existing
	     definition comes from.  */
	  switch (h->root.type)
	    {
	    default:
	      break;

	    case bfd_link_hash_defined:
	    case bfd_link_hash_defweak:
	      old_bfd = h->root.u.def.section->owner;
	      break;

	    case bfd_link_hash_common:
	      old_bfd = h->root.u.c.p->section->owner;
	      old_alignment = h->root.u.c.p->alignment_power;
	      break;
	    }

	  if (elf_tdata (abfd)->verdef != NULL
	      && ! override
	      && vernum > 1
	      && definition)
	    h->verinfo.verdef = &elf_tdata (abfd)->verdef[vernum - 1];
	}

      if (! (_bfd_generic_link_add_one_symbol
	     (info, abfd, name, flags, sec, value, NULL, FALSE, bed->collect,
	      (struct bfd_link_hash_entry **) sym_hash)))
	goto error_free_vers;

      h = *sym_hash;
      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;
      *sym_hash = h;

      new_weakdef = FALSE;
      if (dynamic
	  && definition
	  && (flags & BSF_WEAK) != 0
	  && !bed->is_function_type (ELF_ST_TYPE (isym->st_info))
	  && is_elf_hash_table (htab)
	  && h->u.weakdef == NULL)
	{
	  /* Keep a list of all weak defined non function symbols from
	     a dynamic object, using the weakdef field.  Later in this
	     function we will set the weakdef field to the correct
	     value.  We only put non-function symbols from dynamic
	     objects on this list, because that happens to be the only
	     time we need to know the normal symbol corresponding to a
	     weak symbol, and the information is time consuming to
	     figure out.  If the weakdef field is not already NULL,
	     then this symbol was already defined by some previous
	     dynamic object, and we will be using that previous
	     definition anyhow.  */

	  h->u.weakdef = weaks;
	  weaks = h;
	  new_weakdef = TRUE;
	}

      /* Set the alignment of a common symbol.  */
      if ((common || bfd_is_com_section (sec))
	  && h->root.type == bfd_link_hash_common)
	{
	  unsigned int align;

	  if (common)
	    align = bfd_log2 (isym->st_value);
	  else
	    {
	      /* The new symbol is a common symbol in a shared object.
		 We need to get the alignment from the section.  */
	      align = new_sec->alignment_power;
	    }
	  if (align > old_alignment
	      /* Permit an alignment power of zero if an alignment of one
		 is specified and no other alignments have been specified.  */
	      || (isym->st_value == 1 && old_alignment == 0))
	    h->root.u.c.p->alignment_power = align;
	  else
	    h->root.u.c.p->alignment_power = old_alignment;
	}

      if (is_elf_hash_table (htab))
	{
	  bfd_boolean dynsym;

	  /* Check the alignment when a common symbol is involved. This
	     can change when a common symbol is overridden by a normal
	     definition or a common symbol is ignored due to the old
	     normal definition. We need to make sure the maximum
	     alignment is maintained.  */
	  if ((old_alignment || common)
	      && h->root.type != bfd_link_hash_common)
	    {
	      unsigned int common_align;
	      unsigned int normal_align;
	      unsigned int symbol_align;
	      bfd *normal_bfd;
	      bfd *common_bfd;

	      symbol_align = ffs (h->root.u.def.value) - 1;
	      if (h->root.u.def.section->owner != NULL
		  && (h->root.u.def.section->owner->flags & DYNAMIC) == 0)
		{
		  normal_align = h->root.u.def.section->alignment_power;
		  if (normal_align > symbol_align)
		    normal_align = symbol_align;
		}
	      else
		normal_align = symbol_align;

	      if (old_alignment)
		{
		  common_align = old_alignment;
		  common_bfd = old_bfd;
		  normal_bfd = abfd;
		}
	      else
		{
		  common_align = bfd_log2 (isym->st_value);
		  common_bfd = abfd;
		  normal_bfd = old_bfd;
		}

	      if (normal_align < common_align)
		{
		  /* PR binutils/2735 */
		  if (normal_bfd == NULL)
		    (*_bfd_error_handler)
		      (_("Warning: alignment %u of common symbol `%s' in %B"
			 " is greater than the alignment (%u) of its section %A"),
		       common_bfd, h->root.u.def.section,
		       1 << common_align, name, 1 << normal_align);
		  else
		    (*_bfd_error_handler)
		      (_("Warning: alignment %u of symbol `%s' in %B"
			 " is smaller than %u in %B"),
		       normal_bfd, common_bfd,
		       1 << normal_align, name, 1 << common_align);
		}
	    }

	  /* Remember the symbol size if it isn't undefined.  */
	  if ((isym->st_size != 0 && isym->st_shndx != SHN_UNDEF)
	      && (definition || h->size == 0))
	    {
	      if (h->size != 0
		  && h->size != isym->st_size
		  && ! size_change_ok)
		(*_bfd_error_handler)
		  (_("Warning: size of symbol `%s' changed"
		     " from %lu in %B to %lu in %B"),
		   old_bfd, abfd,
		   name, (unsigned long) h->size,
		   (unsigned long) isym->st_size);

	      h->size = isym->st_size;
	    }

	  /* If this is a common symbol, then we always want H->SIZE
	     to be the size of the common symbol.  The code just above
	     won't fix the size if a common symbol becomes larger.  We
	     don't warn about a size change here, because that is
	     covered by --warn-common.  Allow changed between different
	     function types.  */
	  if (h->root.type == bfd_link_hash_common)
	    h->size = h->root.u.c.size;

	  if (ELF_ST_TYPE (isym->st_info) != STT_NOTYPE
	      && (definition || h->type == STT_NOTYPE))
	    {
	      if (h->type != STT_NOTYPE
		  && h->type != ELF_ST_TYPE (isym->st_info)
		  && ! type_change_ok)
		(*_bfd_error_handler)
		  (_("Warning: type of symbol `%s' changed"
		     " from %d to %d in %B"),
		   abfd, name, h->type, ELF_ST_TYPE (isym->st_info));

	      h->type = ELF_ST_TYPE (isym->st_info);
	    }

	  /* If st_other has a processor-specific meaning, specific
	     code might be needed here. We never merge the visibility
	     attribute with the one from a dynamic object.  */
	  if (bed->elf_backend_merge_symbol_attribute)
	    (*bed->elf_backend_merge_symbol_attribute) (h, isym, definition,
							dynamic);

	  /* If this symbol has default visibility and the user has requested
	     we not re-export it, then mark it as hidden.  */
	  if (definition && !dynamic
	      && (abfd->no_export
		  || (abfd->my_archive && abfd->my_archive->no_export))
	      && ELF_ST_VISIBILITY (isym->st_other) != STV_INTERNAL)
	    isym->st_other = (STV_HIDDEN
			      | (isym->st_other & ~ELF_ST_VISIBILITY (-1)));

	  if (ELF_ST_VISIBILITY (isym->st_other) != 0 && !dynamic)
	    {
	      unsigned char hvis, symvis, other, nvis;

	      /* Only merge the visibility. Leave the remainder of the
		 st_other field to elf_backend_merge_symbol_attribute.  */
	      other = h->other & ~ELF_ST_VISIBILITY (-1);

	      /* Combine visibilities, using the most constraining one.  */
	      hvis   = ELF_ST_VISIBILITY (h->other);
	      symvis = ELF_ST_VISIBILITY (isym->st_other);
	      if (! hvis)
		nvis = symvis;
	      else if (! symvis)
		nvis = hvis;
	      else
		nvis = hvis < symvis ? hvis : symvis;

	      h->other = other | nvis;
	    }

	  /* Set a flag in the hash table entry indicating the type of
	     reference or definition we just found.  Keep a count of
	     the number of dynamic symbols we find.  A dynamic symbol
	     is one which is referenced or defined by both a regular
	     object and a shared object.  */
	  dynsym = FALSE;
	  if (! dynamic)
	    {
	      if (! definition)
		{
		  h->ref_regular = 1;
		  if (bind != STB_WEAK)
		    h->ref_regular_nonweak = 1;
		}
	      else
		h->def_regular = 1;
	      if (! info->executable
		  || h->def_dynamic
		  || h->ref_dynamic)
		dynsym = TRUE;
	    }
	  else
	    {
	      if (! definition)
		h->ref_dynamic = 1;
	      else
		h->def_dynamic = 1;
	      if (h->def_regular
		  || h->ref_regular
		  || (h->u.weakdef != NULL
		      && ! new_weakdef
		      && h->u.weakdef->dynindx != -1))
		dynsym = TRUE;
	    }

	  if (definition && (sec->flags & SEC_DEBUGGING))
	    {
	      /* We don't want to make debug symbol dynamic.  */
	      (*bed->elf_backend_hide_symbol) (info, h, TRUE);
	      dynsym = FALSE;
	    }

	  /* Check to see if we need to add an indirect symbol for
	     the default name.  */
	  if (definition || h->root.type == bfd_link_hash_common)
	    if (!_bfd_elf_add_default_symbol (abfd, info, h, name, isym,
					      &sec, &value, &dynsym,
					      override))
	      goto error_free_vers;

	  if (definition && !dynamic)
	    {
	      char *p = strchr (name, ELF_VER_CHR);
	      if (p != NULL && p[1] != ELF_VER_CHR)
		{
		  /* Queue non-default versions so that .symver x, x@FOO
		     aliases can be checked.  */
		  if (!nondeflt_vers)
		    {
		      amt = ((isymend - isym + 1)
			     * sizeof (struct elf_link_hash_entry *));
		      nondeflt_vers = bfd_malloc (amt);
		    }
		  nondeflt_vers[nondeflt_vers_cnt++] = h;
		}
	    }

	  if (dynsym && h->dynindx == -1)
	    {
	      if (! bfd_elf_link_record_dynamic_symbol (info, h))
		goto error_free_vers;
	      if (h->u.weakdef != NULL
		  && ! new_weakdef
		  && h->u.weakdef->dynindx == -1)
		{
		  if (!bfd_elf_link_record_dynamic_symbol (info, h->u.weakdef))
		    goto error_free_vers;
		}
	    }
	  else if (dynsym && h->dynindx != -1)
	    /* If the symbol already has a dynamic index, but
	       visibility says it should not be visible, turn it into
	       a local symbol.  */
	    switch (ELF_ST_VISIBILITY (h->other))
	      {
	      case STV_INTERNAL:
	      case STV_HIDDEN:
		(*bed->elf_backend_hide_symbol) (info, h, TRUE);
		dynsym = FALSE;
		break;
	      }

	  if (!add_needed
	      && definition
	      && dynsym
	      && h->ref_regular)
	    {
	      int ret;
	      const char *soname = elf_dt_name (abfd);

	      /* A symbol from a library loaded via DT_NEEDED of some
		 other library is referenced by a regular object.
		 Add a DT_NEEDED entry for it.  Issue an error if
		 --no-add-needed is used.  */
	      if ((elf_dyn_lib_class (abfd) & DYN_NO_NEEDED) != 0)
		{
		  bfd_boolean looks_soish;
		  const char *print_name;
		  int print_len;
		  size_t len, lend = 0;

		  looks_soish = FALSE;
		  print_name = soname;
		  print_len = strlen(soname);
		  if (strncmp(soname, "lib", 3) == 0)
		    {
		      len = print_len;
		      if (len > 5 && strcmp(soname + len - 2, ".a") == 0)
			lend = len - 5;
		      else
			{
			  while (len > 6 && (ISDIGIT(soname[len - 1]) ||
					     soname[len - 1] == '.'))
			    len--;
			  if (strncmp(soname + len - 3, ".so", 3) == 0)
			    lend = len - 6;
			}
		      if (lend != 0)
			{
			  print_name = soname + 3;
			  print_len = lend;
			  looks_soish = TRUE;
		    	}
		    }

		  (*_bfd_error_handler)
		    (_("undefined reference to symbol `%s' (try adding -l%s%.*s)"),
		    name, looks_soish? "" : ":", print_len, print_name);
		  bfd_set_error (bfd_error_bad_value);
		  goto error_free_vers;
		}

	      elf_dyn_lib_class (abfd) &= ~DYN_AS_NEEDED;

	      add_needed = TRUE;
	      ret = elf_add_dt_needed_tag (abfd, info, soname, add_needed);
	      if (ret < 0)
		goto error_free_vers;

	      BFD_ASSERT (ret == 0);
	    }
	}
    }

  if (extversym != NULL)
    {
      free (extversym);
      extversym = NULL;
    }

  if (isymbuf != NULL)
    {
      free (isymbuf);
      isymbuf = NULL;
    }

  if ((elf_dyn_lib_class (abfd) & DYN_AS_NEEDED) != 0)
    {
      unsigned int i;

      /* Restore the symbol table.  */
      if (bed->as_needed_cleanup)
	(*bed->as_needed_cleanup) (abfd, info);
      old_hash = (char *) old_tab + tabsize;
      old_ent = (char *) old_hash + hashsize;
      sym_hash = elf_sym_hashes (abfd);
      htab->root.table.table = old_table;
      htab->root.table.size = old_size;
      htab->root.table.count = old_count;
      memcpy (htab->root.table.table, old_tab, tabsize);
      memcpy (sym_hash, old_hash, hashsize);
      htab->root.undefs = old_undefs;
      htab->root.undefs_tail = old_undefs_tail;
      for (i = 0; i < htab->root.table.size; i++)
	{
	  struct bfd_hash_entry *p;
	  struct elf_link_hash_entry *h;

	  for (p = htab->root.table.table[i]; p != NULL; p = p->next)
	    {
	      h = (struct elf_link_hash_entry *) p;
	      if (h->root.type == bfd_link_hash_warning)
		h = (struct elf_link_hash_entry *) h->root.u.i.link;
	      if (h->dynindx >= old_dynsymcount)
		_bfd_elf_strtab_delref (htab->dynstr, h->dynstr_index);

	      memcpy (p, old_ent, htab->root.table.entsize);
	      old_ent = (char *) old_ent + htab->root.table.entsize;
	      h = (struct elf_link_hash_entry *) p;
	      if (h->root.type == bfd_link_hash_warning)
		{
		  memcpy (h->root.u.i.link, old_ent, htab->root.table.entsize);
		  old_ent = (char *) old_ent + htab->root.table.entsize;
		}
	    }
	}

      /* Make a special call to the linker "notice" function to
	 tell it that symbols added for crefs may need to be removed.  */
      if (!(*info->callbacks->notice) (info, NULL, abfd, NULL,
				       notice_not_needed))
	return FALSE;

      free (old_tab);
      objalloc_free_block ((struct objalloc *) htab->root.table.memory,
			   alloc_mark);
      if (nondeflt_vers != NULL)
	free (nondeflt_vers);
      return TRUE;
    }

  if (old_tab != NULL)
    {
      if (!(*info->callbacks->notice) (info, NULL, abfd, NULL,
				       notice_needed))
	return FALSE;
      free (old_tab);
      old_tab = NULL;
    }

  /* Now that all the symbols from this input file are created, handle
     .symver foo, foo@BAR such that any relocs against foo become foo@BAR.  */
  if (nondeflt_vers != NULL)
    {
      bfd_size_type cnt, symidx;

      for (cnt = 0; cnt < nondeflt_vers_cnt; ++cnt)
	{
	  struct elf_link_hash_entry *h = nondeflt_vers[cnt], *hi;
	  char *shortname, *p;

	  p = strchr (h->root.root.string, ELF_VER_CHR);
	  if (p == NULL
	      || (h->root.type != bfd_link_hash_defined
		  && h->root.type != bfd_link_hash_defweak))
	    continue;

	  amt = p - h->root.root.string;
	  shortname = bfd_malloc (amt + 1);
	  memcpy (shortname, h->root.root.string, amt);
	  shortname[amt] = '\0';

	  hi = (struct elf_link_hash_entry *)
	       bfd_link_hash_lookup (&htab->root, shortname,
				     FALSE, FALSE, FALSE);
	  if (hi != NULL
	      && hi->root.type == h->root.type
	      && hi->root.u.def.value == h->root.u.def.value
	      && hi->root.u.def.section == h->root.u.def.section)
	    {
	      (*bed->elf_backend_hide_symbol) (info, hi, TRUE);
	      hi->root.type = bfd_link_hash_indirect;
	      hi->root.u.i.link = (struct bfd_link_hash_entry *) h;
	      (*bed->elf_backend_copy_indirect_symbol) (info, h, hi);
	      sym_hash = elf_sym_hashes (abfd);
	      if (sym_hash)
		for (symidx = 0; symidx < extsymcount; ++symidx)
		  if (sym_hash[symidx] == hi)
		    {
		      sym_hash[symidx] = h;
		      break;
		    }
	    }
	  free (shortname);
	}
      free (nondeflt_vers);
      nondeflt_vers = NULL;
    }

  /* Now set the weakdefs field correctly for all the weak defined
     symbols we found.  The only way to do this is to search all the
     symbols.  Since we only need the information for non functions in
     dynamic objects, that's the only time we actually put anything on
     the list WEAKS.  We need this information so that if a regular
     object refers to a symbol defined weakly in a dynamic object, the
     real symbol in the dynamic object is also put in the dynamic
     symbols; we also must arrange for both symbols to point to the
     same memory location.  We could handle the general case of symbol
     aliasing, but a general symbol alias can only be generated in
     assembler code, handling it correctly would be very time
     consuming, and other ELF linkers don't handle general aliasing
     either.  */
  if (weaks != NULL)
    {
      struct elf_link_hash_entry **hpp;
      struct elf_link_hash_entry **hppend;
      struct elf_link_hash_entry **sorted_sym_hash;
      struct elf_link_hash_entry *h;
      size_t sym_count;

      /* Since we have to search the whole symbol list for each weak
	 defined symbol, search time for N weak defined symbols will be
	 O(N^2). Binary search will cut it down to O(NlogN).  */
      amt = extsymcount * sizeof (struct elf_link_hash_entry *);
      sorted_sym_hash = bfd_malloc (amt);
      if (sorted_sym_hash == NULL)
	goto error_return;
      sym_hash = sorted_sym_hash;
      hpp = elf_sym_hashes (abfd);
      hppend = hpp + extsymcount;
      sym_count = 0;
      for (; hpp < hppend; hpp++)
	{
	  h = *hpp;
	  if (h != NULL
	      && h->root.type == bfd_link_hash_defined
	      && !bed->is_function_type (h->type))
	    {
	      *sym_hash = h;
	      sym_hash++;
	      sym_count++;
	    }
	}

      qsort (sorted_sym_hash, sym_count,
	     sizeof (struct elf_link_hash_entry *),
	     elf_sort_symbol);

      while (weaks != NULL)
	{
	  struct elf_link_hash_entry *hlook;
	  asection *slook;
	  bfd_vma vlook;
	  long ilook;
	  size_t i, j, idx;

	  hlook = weaks;
	  weaks = hlook->u.weakdef;
	  hlook->u.weakdef = NULL;

	  BFD_ASSERT (hlook->root.type == bfd_link_hash_defined
		      || hlook->root.type == bfd_link_hash_defweak
		      || hlook->root.type == bfd_link_hash_common
		      || hlook->root.type == bfd_link_hash_indirect);
	  slook = hlook->root.u.def.section;
	  vlook = hlook->root.u.def.value;

	  ilook = -1;
	  i = 0;
	  j = sym_count;
	  while (i < j)
	    {
	      bfd_signed_vma vdiff;
	      idx = (i + j) / 2;
	      h = sorted_sym_hash [idx];
	      vdiff = vlook - h->root.u.def.value;
	      if (vdiff < 0)
		j = idx;
	      else if (vdiff > 0)
		i = idx + 1;
	      else
		{
		  long sdiff = slook->id - h->root.u.def.section->id;
		  if (sdiff < 0)
		    j = idx;
		  else if (sdiff > 0)
		    i = idx + 1;
		  else
		    {
		      ilook = idx;
		      break;
		    }
		}
	    }

	  /* We didn't find a value/section match.  */
	  if (ilook == -1)
	    continue;

	  for (i = ilook; i < sym_count; i++)
	    {
	      h = sorted_sym_hash [i];

	      /* Stop if value or section doesn't match.  */
	      if (h->root.u.def.value != vlook
		  || h->root.u.def.section != slook)
		break;
	      else if (h != hlook)
		{
		  hlook->u.weakdef = h;

		  /* If the weak definition is in the list of dynamic
		     symbols, make sure the real definition is put
		     there as well.  */
		  if (hlook->dynindx != -1 && h->dynindx == -1)
		    {
		      if (! bfd_elf_link_record_dynamic_symbol (info, h))
			goto error_return;
		    }

		  /* If the real definition is in the list of dynamic
		     symbols, make sure the weak definition is put
		     there as well.  If we don't do this, then the
		     dynamic loader might not merge the entries for the
		     real definition and the weak definition.  */
		  if (h->dynindx != -1 && hlook->dynindx == -1)
		    {
		      if (! bfd_elf_link_record_dynamic_symbol (info, hlook))
			goto error_return;
		    }
		  break;
		}
	    }
	}

      free (sorted_sym_hash);
    }

  if (bed->check_directives)
    (*bed->check_directives) (abfd, info);

  /* If this object is the same format as the output object, and it is
     not a shared library, then let the backend look through the
     relocs.

     This is required to build global offset table entries and to
     arrange for dynamic relocs.  It is not required for the
     particular common case of linking non PIC code, even when linking
     against shared libraries, but unfortunately there is no way of
     knowing whether an object file has been compiled PIC or not.
     Looking through the relocs is not particularly time consuming.
     The problem is that we must either (1) keep the relocs in memory,
     which causes the linker to require additional runtime memory or
     (2) read the relocs twice from the input file, which wastes time.
     This would be a good case for using mmap.

     I have no idea how to handle linking PIC code into a file of a
     different format.  It probably can't be done.  */
  if (! dynamic
      && is_elf_hash_table (htab)
      && bed->check_relocs != NULL
      && (*bed->relocs_compatible) (abfd->xvec, htab->root.creator))
    {
      asection *o;

      for (o = abfd->sections; o != NULL; o = o->next)
	{
	  Elf_Internal_Rela *internal_relocs;
	  bfd_boolean ok;

	  if ((o->flags & SEC_RELOC) == 0
	      || o->reloc_count == 0
	      || ((info->strip == strip_all || info->strip == strip_debugger)
		  && (o->flags & SEC_DEBUGGING) != 0)
	      || bfd_is_abs_section (o->output_section))
	    continue;

	  internal_relocs = _bfd_elf_link_read_relocs (abfd, o, NULL, NULL,
						       info->keep_memory);
	  if (internal_relocs == NULL)
	    goto error_return;

	  ok = (*bed->check_relocs) (abfd, info, o, internal_relocs);

	  if (elf_section_data (o)->relocs != internal_relocs)
	    free (internal_relocs);

	  if (! ok)
	    goto error_return;
	}
    }

  /* If this is a non-traditional link, try to optimize the handling
     of the .stab/.stabstr sections.  */
  if (! dynamic
      && ! info->traditional_format
      && is_elf_hash_table (htab)
      && (info->strip != strip_all && info->strip != strip_debugger))
    {
      asection *stabstr;

      stabstr = bfd_get_section_by_name (abfd, ".stabstr");
      if (stabstr != NULL)
	{
	  bfd_size_type string_offset = 0;
	  asection *stab;

	  for (stab = abfd->sections; stab; stab = stab->next)
	    if (CONST_STRNEQ (stab->name, ".stab")
		&& (!stab->name[5] ||
		    (stab->name[5] == '.' && ISDIGIT (stab->name[6])))
		&& (stab->flags & SEC_MERGE) == 0
		&& !bfd_is_abs_section (stab->output_section))
	      {
		struct bfd_elf_section_data *secdata;

		secdata = elf_section_data (stab);
		if (! _bfd_link_section_stabs (abfd, &htab->stab_info, stab,
					       stabstr, &secdata->sec_info,
					       &string_offset))
		  goto error_return;
		if (secdata->sec_info)
		  stab->sec_info_type = ELF_INFO_TYPE_STABS;
	    }
	}
    }

  if (is_elf_hash_table (htab) && add_needed)
    {
      /* Add this bfd to the loaded list.  */
      struct elf_link_loaded_list *n;

      n = bfd_alloc (abfd, sizeof (struct elf_link_loaded_list));
      if (n == NULL)
	goto error_return;
      n->abfd = abfd;
      n->next = htab->loaded;
      htab->loaded = n;
    }

  return TRUE;

 error_free_vers:
  if (old_tab != NULL)
    free (old_tab);
  if (nondeflt_vers != NULL)
    free (nondeflt_vers);
  if (extversym != NULL)
    free (extversym);
 error_free_sym:
  if (isymbuf != NULL)
    free (isymbuf);
 error_return:
  return FALSE;
}

/* Return the linker hash table entry of a symbol that might be
   satisfied by an archive symbol.  Return -1 on error.  */

struct elf_link_hash_entry *
_bfd_elf_archive_symbol_lookup (bfd *abfd,
				struct bfd_link_info *info,
				const char *name)
{
  struct elf_link_hash_entry *h;
  char *p, *copy;
  size_t len, first;

  h = elf_link_hash_lookup (elf_hash_table (info), name, FALSE, FALSE, FALSE);
  if (h != NULL)
    return h;

  /* If this is a default version (the name contains @@), look up the
     symbol again with only one `@' as well as without the version.
     The effect is that references to the symbol with and without the
     version will be matched by the default symbol in the archive.  */

  p = strchr (name, ELF_VER_CHR);
  if (p == NULL || p[1] != ELF_VER_CHR)
    return h;

  /* First check with only one `@'.  */
  len = strlen (name);
  copy = bfd_alloc (abfd, len);
  if (copy == NULL)
    return (struct elf_link_hash_entry *)(intptr_t)-1;

  first = p - name + 1;
  memcpy (copy, name, first);
  memcpy (copy + first, name + first + 1, len - first);

  h = elf_link_hash_lookup (elf_hash_table (info), copy, FALSE, FALSE, FALSE);
  if (h == NULL)
    {
      /* We also need to check references to the symbol without the
	 version.  */
      copy[first - 1] = '\0';
      h = elf_link_hash_lookup (elf_hash_table (info), copy,
				FALSE, FALSE, FALSE);
    }

  bfd_release (abfd, copy);
  return h;
}

/* Add symbols from an ELF archive file to the linker hash table.  We
   don't use _bfd_generic_link_add_archive_symbols because of a
   problem which arises on UnixWare.  The UnixWare libc.so is an
   archive which includes an entry libc.so.1 which defines a bunch of
   symbols.  The libc.so archive also includes a number of other
   object files, which also define symbols, some of which are the same
   as those defined in libc.so.1.  Correct linking requires that we
   consider each object file in turn, and include it if it defines any
   symbols we need.  _bfd_generic_link_add_archive_symbols does not do
   this; it looks through the list of undefined symbols, and includes
   any object file which defines them.  When this algorithm is used on
   UnixWare, it winds up pulling in libc.so.1 early and defining a
   bunch of symbols.  This means that some of the other objects in the
   archive are not included in the link, which is incorrect since they
   precede libc.so.1 in the archive.

   Fortunately, ELF archive handling is simpler than that done by
   _bfd_generic_link_add_archive_symbols, which has to allow for a.out
   oddities.  In ELF, if we find a symbol in the archive map, and the
   symbol is currently undefined, we know that we must pull in that
   object file.

   Unfortunately, we do have to make multiple passes over the symbol
   table until nothing further is resolved.  */

static bfd_boolean
elf_link_add_archive_symbols (bfd *abfd, struct bfd_link_info *info)
{
  symindex c;
  bfd_boolean *defined = NULL;
  bfd_boolean *included = NULL;
  carsym *symdefs;
  bfd_boolean loop;
  bfd_size_type amt;
  const struct elf_backend_data *bed;
  struct elf_link_hash_entry * (*archive_symbol_lookup)
    (bfd *, struct bfd_link_info *, const char *);

  if (! bfd_has_map (abfd))
    {
      /* An empty archive is a special case.  */
      if (bfd_openr_next_archived_file (abfd, NULL) == NULL)
	return TRUE;
      bfd_set_error (bfd_error_no_armap);
      return FALSE;
    }

  /* Keep track of all symbols we know to be already defined, and all
     files we know to be already included.  This is to speed up the
     second and subsequent passes.  */
  c = bfd_ardata (abfd)->symdef_count;
  if (c == 0)
    return TRUE;
  amt = c;
  amt *= sizeof (bfd_boolean);
  defined = bfd_zmalloc (amt);
  included = bfd_zmalloc (amt);
  if (defined == NULL || included == NULL)
    goto error_return;

  symdefs = bfd_ardata (abfd)->symdefs;
  bed = get_elf_backend_data (abfd);
  archive_symbol_lookup = bed->elf_backend_archive_symbol_lookup;

  do
    {
      file_ptr last;
      symindex i;
      carsym *symdef;
      carsym *symdefend;

      loop = FALSE;
      last = -1;

      symdef = symdefs;
      symdefend = symdef + c;
      for (i = 0; symdef < symdefend; symdef++, i++)
	{
	  struct elf_link_hash_entry *h;
	  bfd *element;
	  struct bfd_link_hash_entry *undefs_tail;
	  symindex mark;

	  if (defined[i] || included[i])
	    continue;
	  if (symdef->file_offset == last)
	    {
	      included[i] = TRUE;
	      continue;
	    }

	  h = archive_symbol_lookup (abfd, info, symdef->name);
	  if (h == (struct elf_link_hash_entry *)(intptr_t)-1)
	    goto error_return;

	  if (h == NULL)
	    continue;

	  if (h->root.type == bfd_link_hash_common)
	    {
	      /* We currently have a common symbol.  The archive map contains
		 a reference to this symbol, so we may want to include it.  We
		 only want to include it however, if this archive element
		 contains a definition of the symbol, not just another common
		 declaration of it.

		 Unfortunately some archivers (including GNU ar) will put
		 declarations of common symbols into their archive maps, as
		 well as real definitions, so we cannot just go by the archive
		 map alone.  Instead we must read in the element's symbol
		 table and check that to see what kind of symbol definition
		 this is.  */
	      if (! elf_link_is_defined_archive_symbol (abfd, symdef))
		continue;
	    }
	  else if (h->root.type != bfd_link_hash_undefined)
	    {
	      if (h->root.type != bfd_link_hash_undefweak)
		defined[i] = TRUE;
	      continue;
	    }

	  /* We need to include this archive member.  */
	  element = _bfd_get_elt_at_filepos (abfd, symdef->file_offset);
	  if (element == NULL)
	    goto error_return;

	  if (! bfd_check_format (element, bfd_object))
	    goto error_return;

	  /* Doublecheck that we have not included this object
	     already--it should be impossible, but there may be
	     something wrong with the archive.  */
	  if (element->archive_pass != 0)
	    {
	      bfd_set_error (bfd_error_bad_value);
	      goto error_return;
	    }
	  element->archive_pass = 1;

	  undefs_tail = info->hash->undefs_tail;

	  if (! (*info->callbacks->add_archive_element) (info, element,
							 symdef->name))
	    goto error_return;
	  if (! bfd_link_add_symbols (element, info))
	    goto error_return;

	  /* If there are any new undefined symbols, we need to make
	     another pass through the archive in order to see whether
	     they can be defined.  FIXME: This isn't perfect, because
	     common symbols wind up on undefs_tail and because an
	     undefined symbol which is defined later on in this pass
	     does not require another pass.  This isn't a bug, but it
	     does make the code less efficient than it could be.  */
	  if (undefs_tail != info->hash->undefs_tail)
	    loop = TRUE;

	  /* Look backward to mark all symbols from this object file
	     which we have already seen in this pass.  */
	  mark = i;
	  do
	    {
	      included[mark] = TRUE;
	      if (mark == 0)
		break;
	      --mark;
	    }
	  while (symdefs[mark].file_offset == symdef->file_offset);

	  /* We mark subsequent symbols from this object file as we go
	     on through the loop.  */
	  last = symdef->file_offset;
	}
    }
  while (loop);

  free (defined);
  free (included);

  return TRUE;

 error_return:
  if (defined != NULL)
    free (defined);
  if (included != NULL)
    free (included);
  return FALSE;
}

/* Given an ELF BFD, add symbols to the global hash table as
   appropriate.  */

bfd_boolean
bfd_elf_link_add_symbols (bfd *abfd, struct bfd_link_info *info)
{
  switch (bfd_get_format (abfd))
    {
    case bfd_object:
      return elf_link_add_object_symbols (abfd, info);
    case bfd_archive:
      return elf_link_add_archive_symbols (abfd, info);
    default:
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }
}

/* This function will be called though elf_link_hash_traverse to store
   all hash value of the exported symbols in an array.  */

static bfd_boolean
elf_collect_hash_codes (struct elf_link_hash_entry *h, void *data)
{
  unsigned long **valuep = data;
  const char *name;
  char *p;
  unsigned long ha;
  char *alc = NULL;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* Ignore indirect symbols.  These are added by the versioning code.  */
  if (h->dynindx == -1)
    return TRUE;

  name = h->root.root.string;
  p = strchr (name, ELF_VER_CHR);
  if (p != NULL)
    {
      alc = bfd_malloc (p - name + 1);
      memcpy (alc, name, p - name);
      alc[p - name] = '\0';
      name = alc;
    }

  /* Compute the hash value.  */
  ha = bfd_elf_hash (name);

  /* Store the found hash value in the array given as the argument.  */
  *(*valuep)++ = ha;

  /* And store it in the struct so that we can put it in the hash table
     later.  */
  h->u.elf_hash_value = ha;

  if (alc != NULL)
    free (alc);

  return TRUE;
}

struct collect_gnu_hash_codes
{
  bfd *output_bfd;
  const struct elf_backend_data *bed;
  unsigned long int nsyms;
  unsigned long int maskbits;
  unsigned long int *hashcodes;
  unsigned long int *hashval;
  unsigned long int *indx;
  unsigned long int *counts;
  bfd_vma *bitmask;
  bfd_byte *contents;
  long int min_dynindx;
  unsigned long int bucketcount;
  unsigned long int symindx;
  long int local_indx;
  long int shift1, shift2;
  unsigned long int mask;
};

/* This function will be called though elf_link_hash_traverse to store
   all hash value of the exported symbols in an array.  */

static bfd_boolean
elf_collect_gnu_hash_codes (struct elf_link_hash_entry *h, void *data)
{
  struct collect_gnu_hash_codes *s = data;
  const char *name;
  char *p;
  unsigned long ha;
  char *alc = NULL;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* Ignore indirect symbols.  These are added by the versioning code.  */
  if (h->dynindx == -1)
    return TRUE;

  /* Ignore also local symbols and undefined symbols.  */
  if (! (*s->bed->elf_hash_symbol) (h))
    return TRUE;

  name = h->root.root.string;
  p = strchr (name, ELF_VER_CHR);
  if (p != NULL)
    {
      alc = bfd_malloc (p - name + 1);
      memcpy (alc, name, p - name);
      alc[p - name] = '\0';
      name = alc;
    }

  /* Compute the hash value.  */
  ha = bfd_elf_gnu_hash (name);

  /* Store the found hash value in the array for compute_bucket_count,
     and also for .dynsym reordering purposes.  */
  s->hashcodes[s->nsyms] = ha;
  s->hashval[h->dynindx] = ha;
  ++s->nsyms;
  if (s->min_dynindx < 0 || s->min_dynindx > h->dynindx)
    s->min_dynindx = h->dynindx;

  if (alc != NULL)
    free (alc);

  return TRUE;
}

/* This function will be called though elf_link_hash_traverse to do
   final dynaminc symbol renumbering.  */

static bfd_boolean
elf_renumber_gnu_hash_syms (struct elf_link_hash_entry *h, void *data)
{
  struct collect_gnu_hash_codes *s = data;
  unsigned long int bucket;
  unsigned long int val;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* Ignore indirect symbols.  */
  if (h->dynindx == -1)
    return TRUE;

  /* Ignore also local symbols and undefined symbols.  */
  if (! (*s->bed->elf_hash_symbol) (h))
    {
      if (h->dynindx >= s->min_dynindx)
	h->dynindx = s->local_indx++;
      return TRUE;
    }

  bucket = s->hashval[h->dynindx] % s->bucketcount;
  val = (s->hashval[h->dynindx] >> s->shift1)
	& ((s->maskbits >> s->shift1) - 1);
  s->bitmask[val] |= ((bfd_vma) 1) << (s->hashval[h->dynindx] & s->mask);
  s->bitmask[val]
    |= ((bfd_vma) 1) << ((s->hashval[h->dynindx] >> s->shift2) & s->mask);
  val = s->hashval[h->dynindx] & ~(unsigned long int) 1;
  if (s->counts[bucket] == 1)
    /* Last element terminates the chain.  */
    val |= 1;
  bfd_put_32 (s->output_bfd, val,
	      s->contents + (s->indx[bucket] - s->symindx) * 4);
  --s->counts[bucket];
  h->dynindx = s->indx[bucket]++;
  return TRUE;
}

/* Return TRUE if symbol should be hashed in the `.gnu.hash' section.  */

bfd_boolean
_bfd_elf_hash_symbol (struct elf_link_hash_entry *h)
{
  return !(h->forced_local
	   || h->root.type == bfd_link_hash_undefined
	   || h->root.type == bfd_link_hash_undefweak
	   || ((h->root.type == bfd_link_hash_defined
		|| h->root.type == bfd_link_hash_defweak)
	       && h->root.u.def.section->output_section == NULL));
}

/* Array used to determine the number of hash table buckets to use
   based on the number of symbols there are.  If there are fewer than
   3 symbols we use 1 bucket, fewer than 17 symbols we use 3 buckets,
   fewer than 37 we use 17 buckets, and so forth.  We never use more
   than 32771 buckets.  */

static const size_t elf_buckets[] =
{
  1, 3, 17, 37, 67, 97, 131, 197, 263, 521, 1031, 2053, 4099, 8209,
  16411, 32771, 0
};

/* Compute bucket count for hashing table.  We do not use a static set
   of possible tables sizes anymore.  Instead we determine for all
   possible reasonable sizes of the table the outcome (i.e., the
   number of collisions etc) and choose the best solution.  The
   weighting functions are not too simple to allow the table to grow
   without bounds.  Instead one of the weighting factors is the size.
   Therefore the result is always a good payoff between few collisions
   (= short chain lengths) and table size.  */
static size_t
compute_bucket_count (struct bfd_link_info *info, unsigned long int *hashcodes,
		      unsigned long int nsyms, int gnu_hash)
{
  size_t dynsymcount = elf_hash_table (info)->dynsymcount;
  size_t best_size = 0;
  unsigned long int i;
  bfd_size_type amt;

  /* We have a problem here.  The following code to optimize the table
     size requires an integer type with more the 32 bits.  If
     BFD_HOST_U_64_BIT is set we know about such a type.  */
#ifdef BFD_HOST_U_64_BIT
  if (info->optimize)
    {
      size_t minsize;
      size_t maxsize;
      BFD_HOST_U_64_BIT best_chlen = ~((BFD_HOST_U_64_BIT) 0);
      bfd *dynobj = elf_hash_table (info)->dynobj;
      const struct elf_backend_data *bed = get_elf_backend_data (dynobj);
      unsigned long int *counts;

      /* Possible optimization parameters: if we have NSYMS symbols we say
	 that the hashing table must at least have NSYMS/4 and at most
	 2*NSYMS buckets.  */
      minsize = nsyms / 4;
      if (minsize == 0)
	minsize = 1;
      best_size = maxsize = nsyms * 2;
      if (gnu_hash)
	{
	  if (minsize < 2)
	    minsize = 2;
	  if ((best_size & 31) == 0)
	    ++best_size;
	}

      /* Create array where we count the collisions in.  We must use bfd_malloc
	 since the size could be large.  */
      amt = maxsize;
      amt *= sizeof (unsigned long int);
      counts = bfd_malloc (amt);
      if (counts == NULL)
	return 0;

      /* Compute the "optimal" size for the hash table.  The criteria is a
	 minimal chain length.  The minor criteria is (of course) the size
	 of the table.  */
      for (i = minsize; i < maxsize; ++i)
	{
	  /* Walk through the array of hashcodes and count the collisions.  */
	  BFD_HOST_U_64_BIT max;
	  unsigned long int j;
	  unsigned long int fact;

	  if (gnu_hash && (i & 31) == 0)
	    continue;

	  memset (counts, '\0', i * sizeof (unsigned long int));

	  /* Determine how often each hash bucket is used.  */
	  for (j = 0; j < nsyms; ++j)
	    ++counts[hashcodes[j] % i];

	  /* For the weight function we need some information about the
	     pagesize on the target.  This is information need not be 100%
	     accurate.  Since this information is not available (so far) we
	     define it here to a reasonable default value.  If it is crucial
	     to have a better value some day simply define this value.  */
# ifndef BFD_TARGET_PAGESIZE
#  define BFD_TARGET_PAGESIZE	(4096)
# endif

	  /* We in any case need 2 + DYNSYMCOUNT entries for the size values
	     and the chains.  */
	  max = (2 + dynsymcount) * bed->s->sizeof_hash_entry;

# if 1
	  /* Variant 1: optimize for short chains.  We add the squares
	     of all the chain lengths (which favors many small chain
	     over a few long chains).  */
	  for (j = 0; j < i; ++j)
	    max += counts[j] * counts[j];

	  /* This adds penalties for the overall size of the table.  */
	  fact = i / (BFD_TARGET_PAGESIZE / bed->s->sizeof_hash_entry) + 1;
	  max *= fact * fact;
# else
	  /* Variant 2: Optimize a lot more for small table.  Here we
	     also add squares of the size but we also add penalties for
	     empty slots (the +1 term).  */
	  for (j = 0; j < i; ++j)
	    max += (1 + counts[j]) * (1 + counts[j]);

	  /* The overall size of the table is considered, but not as
	     strong as in variant 1, where it is squared.  */
	  fact = i / (BFD_TARGET_PAGESIZE / bed->s->sizeof_hash_entry) + 1;
	  max *= fact;
# endif

	  /* Compare with current best results.  */
	  if (max < best_chlen)
	    {
	      best_chlen = max;
	      best_size = i;
	    }
	}

      free (counts);
    }
  else
#endif /* defined (BFD_HOST_U_64_BIT) */
    {
      /* This is the fallback solution if no 64bit type is available or if we
	 are not supposed to spend much time on optimizations.  We select the
	 bucket count using a fixed set of numbers.  */
      for (i = 0; elf_buckets[i] != 0; i++)
	{
	  best_size = elf_buckets[i];
	  if (nsyms < elf_buckets[i + 1])
	    break;
	}
      if (gnu_hash && best_size < 2)
	best_size = 2;
    }

  return best_size;
}

/* Set up the sizes and contents of the ELF dynamic sections.  This is
   called by the ELF linker emulation before_allocation routine.  We
   must set the sizes of the sections before the linker sets the
   addresses of the various sections.  */

bfd_boolean
bfd_elf_size_dynamic_sections (bfd *output_bfd,
			       const char *soname,
			       const char *rpath,
			       const char *filter_shlib,
			       const char * const *auxiliary_filters,
			       struct bfd_link_info *info,
			       asection **sinterpptr,
			       struct bfd_elf_version_tree *verdefs)
{
  bfd_size_type soname_indx;
  bfd *dynobj;
  const struct elf_backend_data *bed;
  struct elf_assign_sym_version_info asvinfo;

  *sinterpptr = NULL;

  soname_indx = (bfd_size_type) -1;

  if (!is_elf_hash_table (info->hash))
    return TRUE;

  bed = get_elf_backend_data (output_bfd);
  elf_tdata (output_bfd)->relro = info->relro;
  if (info->execstack)
    elf_tdata (output_bfd)->stack_flags = PF_R | PF_W | PF_X;
  else if (info->noexecstack)
    elf_tdata (output_bfd)->stack_flags = PF_R | PF_W;
  else
    {
      bfd *inputobj;
      asection *notesec = NULL;
      int exec = 0;

      for (inputobj = info->input_bfds;
	   inputobj;
	   inputobj = inputobj->link_next)
	{
	  asection *s;

	  if (inputobj->flags & (DYNAMIC | BFD_LINKER_CREATED))
	    continue;
	  s = bfd_get_section_by_name (inputobj, ".note.GNU-stack");
	  if (s)
	    {
	      if (s->flags & SEC_CODE)
		exec = PF_X;
	      notesec = s;
	    }
	  else if (bed->default_execstack)
	    exec = PF_X;
	}
      if (notesec)
	{
	  elf_tdata (output_bfd)->stack_flags = PF_R | PF_W | exec;
	  if (exec && info->relocatable
	      && notesec->output_section != bfd_abs_section_ptr)
	    notesec->output_section->flags |= SEC_CODE;
	}
    }

  /* Any syms created from now on start with -1 in
     got.refcount/offset and plt.refcount/offset.  */
  elf_hash_table (info)->init_got_refcount
    = elf_hash_table (info)->init_got_offset;
  elf_hash_table (info)->init_plt_refcount
    = elf_hash_table (info)->init_plt_offset;

  /* The backend may have to create some sections regardless of whether
     we're dynamic or not.  */
  if (bed->elf_backend_always_size_sections
      && ! (*bed->elf_backend_always_size_sections) (output_bfd, info))
    return FALSE;

  if (! _bfd_elf_maybe_strip_eh_frame_hdr (info))
    return FALSE;

  dynobj = elf_hash_table (info)->dynobj;

  /* If there were no dynamic objects in the link, there is nothing to
     do here.  */
  if (dynobj == NULL)
    return TRUE;

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      struct elf_info_failed eif;
      struct elf_link_hash_entry *h;
      asection *dynstr;
      struct bfd_elf_version_tree *t;
      struct bfd_elf_version_expr *d;
      asection *s;
      bfd_boolean all_defined;

      *sinterpptr = bfd_get_section_by_name (dynobj, ".interp");
      BFD_ASSERT (*sinterpptr != NULL || !info->executable);

      if (soname != NULL)
	{
	  soname_indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr,
					     soname, TRUE);
	  if (soname_indx == (bfd_size_type) -1
	      || !_bfd_elf_add_dynamic_entry (info, DT_SONAME, soname_indx))
	    return FALSE;
	}

      if (info->symbolic)
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_SYMBOLIC, 0))
	    return FALSE;
	  info->flags |= DF_SYMBOLIC;
	}

      if (rpath != NULL)
	{
	  bfd_size_type indx;

	  indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr, rpath,
				      TRUE);
	  if (indx == (bfd_size_type) -1
	      || !_bfd_elf_add_dynamic_entry (info, DT_RPATH, indx))
	    return FALSE;

	  if  (info->new_dtags)
	    {
	      _bfd_elf_strtab_addref (elf_hash_table (info)->dynstr, indx);
	      if (!_bfd_elf_add_dynamic_entry (info, DT_RUNPATH, indx))
		return FALSE;
	    }
	}

      if (filter_shlib != NULL)
	{
	  bfd_size_type indx;

	  indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr,
				      filter_shlib, TRUE);
	  if (indx == (bfd_size_type) -1
	      || !_bfd_elf_add_dynamic_entry (info, DT_FILTER, indx))
	    return FALSE;
	}

      if (auxiliary_filters != NULL)
	{
	  const char * const *p;

	  for (p = auxiliary_filters; *p != NULL; p++)
	    {
	      bfd_size_type indx;

	      indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr,
					  *p, TRUE);
	      if (indx == (bfd_size_type) -1
		  || !_bfd_elf_add_dynamic_entry (info, DT_AUXILIARY, indx))
		return FALSE;
	    }
	}

      eif.info = info;
      eif.verdefs = verdefs;
      eif.failed = FALSE;

      /* If we are supposed to export all symbols into the dynamic symbol
	 table (this is not the normal case), then do so.  */
      if (info->export_dynamic
	  || (info->executable && info->dynamic))
	{
	  elf_link_hash_traverse (elf_hash_table (info),
				  _bfd_elf_export_symbol,
				  &eif);
	  if (eif.failed)
	    return FALSE;
	}

      /* Make all global versions with definition.  */
      for (t = verdefs; t != NULL; t = t->next)
	for (d = t->globals.list; d != NULL; d = d->next)
	  if (!d->symver && d->symbol)
	    {
	      const char *verstr, *name;
	      size_t namelen, verlen, newlen;
	      char *newname, *p;
	      struct elf_link_hash_entry *newh;

	      name = d->symbol;
	      namelen = strlen (name);
	      verstr = t->name;
	      verlen = strlen (verstr);
	      newlen = namelen + verlen + 3;

	      newname = bfd_malloc (newlen);
	      if (newname == NULL)
		return FALSE;
	      memcpy (newname, name, namelen);

	      /* Check the hidden versioned definition.  */
	      p = newname + namelen;
	      *p++ = ELF_VER_CHR;
	      memcpy (p, verstr, verlen + 1);
	      newh = elf_link_hash_lookup (elf_hash_table (info),
					   newname, FALSE, FALSE,
					   FALSE);
	      if (newh == NULL
		  || (newh->root.type != bfd_link_hash_defined
		      && newh->root.type != bfd_link_hash_defweak))
		{
		  /* Check the default versioned definition.  */
		  *p++ = ELF_VER_CHR;
		  memcpy (p, verstr, verlen + 1);
		  newh = elf_link_hash_lookup (elf_hash_table (info),
					       newname, FALSE, FALSE,
					       FALSE);
		}
	      free (newname);

	      /* Mark this version if there is a definition and it is
		 not defined in a shared object.  */
	      if (newh != NULL
		  && !newh->def_dynamic
		  && (newh->root.type == bfd_link_hash_defined
		      || newh->root.type == bfd_link_hash_defweak))
		d->symver = 1;
	    }

      /* Attach all the symbols to their version information.  */
      asvinfo.output_bfd = output_bfd;
      asvinfo.info = info;
      asvinfo.verdefs = verdefs;
      asvinfo.failed = FALSE;

      elf_link_hash_traverse (elf_hash_table (info),
			      _bfd_elf_link_assign_sym_version,
			      &asvinfo);
      if (asvinfo.failed)
	return FALSE;

      if (!info->allow_undefined_version)
	{
	  /* Check if all global versions have a definition.  */
	  all_defined = TRUE;
	  for (t = verdefs; t != NULL; t = t->next)
	    for (d = t->globals.list; d != NULL; d = d->next)
	      if (!d->symver && !d->script)
		{
		  (*_bfd_error_handler)
		    (_("%s: undefined version: %s"),
		     d->pattern, t->name);
		  all_defined = FALSE;
		}

	  if (!all_defined)
	    {
	      bfd_set_error (bfd_error_bad_value);
	      return FALSE;
	    }
	}

      /* Find all symbols which were defined in a dynamic object and make
	 the backend pick a reasonable value for them.  */
      elf_link_hash_traverse (elf_hash_table (info),
			      _bfd_elf_adjust_dynamic_symbol,
			      &eif);
      if (eif.failed)
	return FALSE;

      /* Add some entries to the .dynamic section.  We fill in some of the
	 values later, in bfd_elf_final_link, but we must add the entries
	 now so that we know the final size of the .dynamic section.  */

      /* If there are initialization and/or finalization functions to
	 call then add the corresponding DT_INIT/DT_FINI entries.  */
      h = (info->init_function
	   ? elf_link_hash_lookup (elf_hash_table (info),
				   info->init_function, FALSE,
				   FALSE, FALSE)
	   : NULL);
      if (h != NULL
	  && (h->ref_regular
	      || h->def_regular))
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_INIT, 0))
	    return FALSE;
	}
      h = (info->fini_function
	   ? elf_link_hash_lookup (elf_hash_table (info),
				   info->fini_function, FALSE,
				   FALSE, FALSE)
	   : NULL);
      if (h != NULL
	  && (h->ref_regular
	      || h->def_regular))
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_FINI, 0))
	    return FALSE;
	}

      s = bfd_get_section_by_name (output_bfd, ".preinit_array");
      if (s != NULL && s->linker_has_input)
	{
	  /* DT_PREINIT_ARRAY is not allowed in shared library.  */
	  if (! info->executable)
	    {
	      bfd *sub;
	      asection *o;

	      for (sub = info->input_bfds; sub != NULL;
		   sub = sub->link_next)
		if (bfd_get_flavour (sub) == bfd_target_elf_flavour)
		  for (o = sub->sections; o != NULL; o = o->next)
		    if (elf_section_data (o)->this_hdr.sh_type
			== SHT_PREINIT_ARRAY)
		      {
			(*_bfd_error_handler)
			  (_("%B: .preinit_array section is not allowed in DSO"),
			   sub);
			break;
		      }

	      bfd_set_error (bfd_error_nonrepresentable_section);
	      return FALSE;
	    }

	  if (!_bfd_elf_add_dynamic_entry (info, DT_PREINIT_ARRAY, 0)
	      || !_bfd_elf_add_dynamic_entry (info, DT_PREINIT_ARRAYSZ, 0))
	    return FALSE;
	}
      s = bfd_get_section_by_name (output_bfd, ".init_array");
      if (s != NULL && s->linker_has_input)
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_INIT_ARRAY, 0)
	      || !_bfd_elf_add_dynamic_entry (info, DT_INIT_ARRAYSZ, 0))
	    return FALSE;
	}
      s = bfd_get_section_by_name (output_bfd, ".fini_array");
      if (s != NULL && s->linker_has_input)
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_FINI_ARRAY, 0)
	      || !_bfd_elf_add_dynamic_entry (info, DT_FINI_ARRAYSZ, 0))
	    return FALSE;
	}

      dynstr = bfd_get_section_by_name (dynobj, ".dynstr");
      /* If .dynstr is excluded from the link, we don't want any of
	 these tags.  Strictly, we should be checking each section
	 individually;  This quick check covers for the case where
	 someone does a /DISCARD/ : { *(*) }.  */
      if (dynstr != NULL && dynstr->output_section != bfd_abs_section_ptr)
	{
	  bfd_size_type strsize;

	  strsize = _bfd_elf_strtab_size (elf_hash_table (info)->dynstr);
	  if ((info->emit_hash
	       && !_bfd_elf_add_dynamic_entry (info, DT_HASH, 0))
	      || (info->emit_gnu_hash
		  && !_bfd_elf_add_dynamic_entry (info, DT_GNU_HASH, 0))
	      || !_bfd_elf_add_dynamic_entry (info, DT_STRTAB, 0)
	      || !_bfd_elf_add_dynamic_entry (info, DT_SYMTAB, 0)
	      || !_bfd_elf_add_dynamic_entry (info, DT_STRSZ, strsize)
	      || !_bfd_elf_add_dynamic_entry (info, DT_SYMENT,
					      bed->s->sizeof_sym))
	    return FALSE;
	}
    }

  /* The backend must work out the sizes of all the other dynamic
     sections.  */
  if (bed->elf_backend_size_dynamic_sections
      && ! (*bed->elf_backend_size_dynamic_sections) (output_bfd, info))
    return FALSE;

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      unsigned long section_sym_count;
      asection *s;

      /* Set up the version definition section.  */
      s = bfd_get_section_by_name (dynobj, ".gnu.version_d");
      BFD_ASSERT (s != NULL);

      /* We may have created additional version definitions if we are
	 just linking a regular application.  */
      verdefs = asvinfo.verdefs;

      /* Skip anonymous version tag.  */
      if (verdefs != NULL && verdefs->vernum == 0)
	verdefs = verdefs->next;

      if (verdefs == NULL && !info->create_default_symver)
	s->flags |= SEC_EXCLUDE;
      else
	{
	  unsigned int cdefs;
	  bfd_size_type size;
	  struct bfd_elf_version_tree *t;
	  bfd_byte *p;
	  Elf_Internal_Verdef def;
	  Elf_Internal_Verdaux defaux;
	  struct bfd_link_hash_entry *bh;
	  struct elf_link_hash_entry *h;
	  const char *name;

	  cdefs = 0;
	  size = 0;

	  /* Make space for the base version.  */
	  size += sizeof (Elf_External_Verdef);
	  size += sizeof (Elf_External_Verdaux);
	  ++cdefs;

	  /* Make space for the default version.  */
	  if (info->create_default_symver)
	    {
	      size += sizeof (Elf_External_Verdef);
	      ++cdefs;
	    }

	  for (t = verdefs; t != NULL; t = t->next)
	    {
	      struct bfd_elf_version_deps *n;

	      size += sizeof (Elf_External_Verdef);
	      size += sizeof (Elf_External_Verdaux);
	      ++cdefs;

	      for (n = t->deps; n != NULL; n = n->next)
		size += sizeof (Elf_External_Verdaux);
	    }

	  s->size = size;
	  s->contents = bfd_alloc (output_bfd, s->size);
	  if (s->contents == NULL && s->size != 0)
	    return FALSE;

	  /* Fill in the version definition section.  */

	  p = s->contents;

	  def.vd_version = VER_DEF_CURRENT;
	  def.vd_flags = VER_FLG_BASE;
	  def.vd_ndx = 1;
	  def.vd_cnt = 1;
	  if (info->create_default_symver)
	    {
	      def.vd_aux = 2 * sizeof (Elf_External_Verdef);
	      def.vd_next = sizeof (Elf_External_Verdef);
	    }
	  else
	    {
	      def.vd_aux = sizeof (Elf_External_Verdef);
	      def.vd_next = (sizeof (Elf_External_Verdef)
			     + sizeof (Elf_External_Verdaux));
	    }

	  if (soname_indx != (bfd_size_type) -1)
	    {
	      _bfd_elf_strtab_addref (elf_hash_table (info)->dynstr,
				      soname_indx);
	      def.vd_hash = bfd_elf_hash (soname);
	      defaux.vda_name = soname_indx;
	      name = soname;
	    }
	  else
	    {
	      bfd_size_type indx;

	      name = lbasename (output_bfd->filename);
	      def.vd_hash = bfd_elf_hash (name);
	      indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr,
					  name, FALSE);
	      if (indx == (bfd_size_type) -1)
		return FALSE;
	      defaux.vda_name = indx;
	    }
	  defaux.vda_next = 0;

	  _bfd_elf_swap_verdef_out (output_bfd, &def,
				    (Elf_External_Verdef *) p);
	  p += sizeof (Elf_External_Verdef);
	  if (info->create_default_symver)
	    {
	      /* Add a symbol representing this version.  */
	      bh = NULL;
	      if (! (_bfd_generic_link_add_one_symbol
		     (info, dynobj, name, BSF_GLOBAL, bfd_abs_section_ptr,
		      0, NULL, FALSE,
		      get_elf_backend_data (dynobj)->collect, &bh)))
		return FALSE;
	      h = (struct elf_link_hash_entry *) bh;
	      h->non_elf = 0;
	      h->def_regular = 1;
	      h->type = STT_OBJECT;
	      h->verinfo.vertree = NULL;

	      if (! bfd_elf_link_record_dynamic_symbol (info, h))
		return FALSE;

	      /* Create a duplicate of the base version with the same
		 aux block, but different flags.  */
	      def.vd_flags = 0;
	      def.vd_ndx = 2;
	      def.vd_aux = sizeof (Elf_External_Verdef);
	      if (verdefs)
		def.vd_next = (sizeof (Elf_External_Verdef)
			       + sizeof (Elf_External_Verdaux));
	      else
		def.vd_next = 0;
	      _bfd_elf_swap_verdef_out (output_bfd, &def,
					(Elf_External_Verdef *) p);
	      p += sizeof (Elf_External_Verdef);
	    }
	  _bfd_elf_swap_verdaux_out (output_bfd, &defaux,
				     (Elf_External_Verdaux *) p);
	  p += sizeof (Elf_External_Verdaux);

	  for (t = verdefs; t != NULL; t = t->next)
	    {
	      unsigned int cdeps;
	      struct bfd_elf_version_deps *n;

	      cdeps = 0;
	      for (n = t->deps; n != NULL; n = n->next)
		++cdeps;

	      /* Add a symbol representing this version.  */
	      bh = NULL;
	      if (! (_bfd_generic_link_add_one_symbol
		     (info, dynobj, t->name, BSF_GLOBAL, bfd_abs_section_ptr,
		      0, NULL, FALSE,
		      get_elf_backend_data (dynobj)->collect, &bh)))
		return FALSE;
	      h = (struct elf_link_hash_entry *) bh;
	      h->non_elf = 0;
	      h->def_regular = 1;
	      h->type = STT_OBJECT;
	      h->verinfo.vertree = t;

	      if (! bfd_elf_link_record_dynamic_symbol (info, h))
		return FALSE;

	      def.vd_version = VER_DEF_CURRENT;
	      def.vd_flags = 0;
	      if (t->globals.list == NULL
		  && t->locals.list == NULL
		  && ! t->used)
		def.vd_flags |= VER_FLG_WEAK;
	      def.vd_ndx = t->vernum + (info->create_default_symver ? 2 : 1);
	      def.vd_cnt = cdeps + 1;
	      def.vd_hash = bfd_elf_hash (t->name);
	      def.vd_aux = sizeof (Elf_External_Verdef);
	      def.vd_next = 0;
	      if (t->next != NULL)
		def.vd_next = (sizeof (Elf_External_Verdef)
			       + (cdeps + 1) * sizeof (Elf_External_Verdaux));

	      _bfd_elf_swap_verdef_out (output_bfd, &def,
					(Elf_External_Verdef *) p);
	      p += sizeof (Elf_External_Verdef);

	      defaux.vda_name = h->dynstr_index;
	      _bfd_elf_strtab_addref (elf_hash_table (info)->dynstr,
				      h->dynstr_index);
	      defaux.vda_next = 0;
	      if (t->deps != NULL)
		defaux.vda_next = sizeof (Elf_External_Verdaux);
	      t->name_indx = defaux.vda_name;

	      _bfd_elf_swap_verdaux_out (output_bfd, &defaux,
					 (Elf_External_Verdaux *) p);
	      p += sizeof (Elf_External_Verdaux);

	      for (n = t->deps; n != NULL; n = n->next)
		{
		  if (n->version_needed == NULL)
		    {
		      /* This can happen if there was an error in the
			 version script.  */
		      defaux.vda_name = 0;
		    }
		  else
		    {
		      defaux.vda_name = n->version_needed->name_indx;
		      _bfd_elf_strtab_addref (elf_hash_table (info)->dynstr,
					      defaux.vda_name);
		    }
		  if (n->next == NULL)
		    defaux.vda_next = 0;
		  else
		    defaux.vda_next = sizeof (Elf_External_Verdaux);

		  _bfd_elf_swap_verdaux_out (output_bfd, &defaux,
					     (Elf_External_Verdaux *) p);
		  p += sizeof (Elf_External_Verdaux);
		}
	    }

	  if (!_bfd_elf_add_dynamic_entry (info, DT_VERDEF, 0)
	      || !_bfd_elf_add_dynamic_entry (info, DT_VERDEFNUM, cdefs))
	    return FALSE;

	  elf_tdata (output_bfd)->cverdefs = cdefs;
	}

      if ((info->new_dtags && info->flags) || (info->flags & DF_STATIC_TLS))
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_FLAGS, info->flags))
	    return FALSE;
	}
      else if (info->flags & DF_BIND_NOW)
	{
	  if (!_bfd_elf_add_dynamic_entry (info, DT_BIND_NOW, 0))
	    return FALSE;
	}

      if (info->flags_1)
	{
	  if (info->executable)
	    info->flags_1 &= ~ (DF_1_INITFIRST
				| DF_1_NODELETE
				| DF_1_NOOPEN);
	  if (!_bfd_elf_add_dynamic_entry (info, DT_FLAGS_1, info->flags_1))
	    return FALSE;
	}

      /* Work out the size of the version reference section.  */

      s = bfd_get_section_by_name (dynobj, ".gnu.version_r");
      BFD_ASSERT (s != NULL);
      {
	struct elf_find_verdep_info sinfo;

	sinfo.output_bfd = output_bfd;
	sinfo.info = info;
	sinfo.vers = elf_tdata (output_bfd)->cverdefs;
	if (sinfo.vers == 0)
	  sinfo.vers = 1;
	sinfo.failed = FALSE;

	elf_link_hash_traverse (elf_hash_table (info),
				_bfd_elf_link_find_version_dependencies,
				&sinfo);

	if (elf_tdata (output_bfd)->verref == NULL)
	  s->flags |= SEC_EXCLUDE;
	else
	  {
	    Elf_Internal_Verneed *t;
	    unsigned int size;
	    unsigned int crefs;
	    bfd_byte *p;

	    /* Build the version definition section.  */
	    size = 0;
	    crefs = 0;
	    for (t = elf_tdata (output_bfd)->verref;
		 t != NULL;
		 t = t->vn_nextref)
	      {
		Elf_Internal_Vernaux *a;

		size += sizeof (Elf_External_Verneed);
		++crefs;
		for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
		  size += sizeof (Elf_External_Vernaux);
	      }

	    s->size = size;
	    s->contents = bfd_alloc (output_bfd, s->size);
	    if (s->contents == NULL)
	      return FALSE;

	    p = s->contents;
	    for (t = elf_tdata (output_bfd)->verref;
		 t != NULL;
		 t = t->vn_nextref)
	      {
		unsigned int caux;
		Elf_Internal_Vernaux *a;
		bfd_size_type indx;

		caux = 0;
		for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
		  ++caux;

		t->vn_version = VER_NEED_CURRENT;
		t->vn_cnt = caux;
		indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr,
					    elf_dt_name (t->vn_bfd) != NULL
					    ? elf_dt_name (t->vn_bfd)
					    : lbasename (t->vn_bfd->filename),
					    FALSE);
		if (indx == (bfd_size_type) -1)
		  return FALSE;
		t->vn_file = indx;
		t->vn_aux = sizeof (Elf_External_Verneed);
		if (t->vn_nextref == NULL)
		  t->vn_next = 0;
		else
		  t->vn_next = (sizeof (Elf_External_Verneed)
				+ caux * sizeof (Elf_External_Vernaux));

		_bfd_elf_swap_verneed_out (output_bfd, t,
					   (Elf_External_Verneed *) p);
		p += sizeof (Elf_External_Verneed);

		for (a = t->vn_auxptr; a != NULL; a = a->vna_nextptr)
		  {
		    a->vna_hash = bfd_elf_hash (a->vna_nodename);
		    indx = _bfd_elf_strtab_add (elf_hash_table (info)->dynstr,
						a->vna_nodename, FALSE);
		    if (indx == (bfd_size_type) -1)
		      return FALSE;
		    a->vna_name = indx;
		    if (a->vna_nextptr == NULL)
		      a->vna_next = 0;
		    else
		      a->vna_next = sizeof (Elf_External_Vernaux);

		    _bfd_elf_swap_vernaux_out (output_bfd, a,
					       (Elf_External_Vernaux *) p);
		    p += sizeof (Elf_External_Vernaux);
		  }
	      }

	    if (!_bfd_elf_add_dynamic_entry (info, DT_VERNEED, 0)
		|| !_bfd_elf_add_dynamic_entry (info, DT_VERNEEDNUM, crefs))
	      return FALSE;

	    elf_tdata (output_bfd)->cverrefs = crefs;
	  }
      }

      if ((elf_tdata (output_bfd)->cverrefs == 0
	   && elf_tdata (output_bfd)->cverdefs == 0)
	  || _bfd_elf_link_renumber_dynsyms (output_bfd, info,
					     &section_sym_count) == 0)
	{
	  s = bfd_get_section_by_name (dynobj, ".gnu.version");
	  s->flags |= SEC_EXCLUDE;
	}
    }
  return TRUE;
}

/* Find the first non-excluded output section.  We'll use its
   section symbol for some emitted relocs.  */
void
_bfd_elf_init_1_index_section (bfd *output_bfd, struct bfd_link_info *info)
{
  asection *s;

  for (s = output_bfd->sections; s != NULL; s = s->next)
    if ((s->flags & (SEC_EXCLUDE | SEC_ALLOC)) == SEC_ALLOC
	&& !_bfd_elf_link_omit_section_dynsym (output_bfd, info, s))
      {
	elf_hash_table (info)->text_index_section = s;
	break;
      }
}

/* Find two non-excluded output sections, one for code, one for data.
   We'll use their section symbols for some emitted relocs.  */
void
_bfd_elf_init_2_index_sections (bfd *output_bfd, struct bfd_link_info *info)
{
  asection *s;

  for (s = output_bfd->sections; s != NULL; s = s->next)
    if (((s->flags & (SEC_EXCLUDE | SEC_ALLOC | SEC_READONLY))
	 == (SEC_ALLOC | SEC_READONLY))
	&& !_bfd_elf_link_omit_section_dynsym (output_bfd, info, s))
      {
	elf_hash_table (info)->text_index_section = s;
	break;
      }

  for (s = output_bfd->sections; s != NULL; s = s->next)
    if (((s->flags & (SEC_EXCLUDE | SEC_ALLOC | SEC_READONLY)) == SEC_ALLOC)
	&& !_bfd_elf_link_omit_section_dynsym (output_bfd, info, s))
      {
	elf_hash_table (info)->data_index_section = s;
	break;
      }

  if (elf_hash_table (info)->text_index_section == NULL)
    elf_hash_table (info)->text_index_section
      = elf_hash_table (info)->data_index_section;
}

bfd_boolean
bfd_elf_size_dynsym_hash_dynstr (bfd *output_bfd, struct bfd_link_info *info)
{
  const struct elf_backend_data *bed;

  if (!is_elf_hash_table (info->hash))
    return TRUE;

  bed = get_elf_backend_data (output_bfd);
  (*bed->elf_backend_init_index_section) (output_bfd, info);

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      bfd *dynobj;
      asection *s;
      bfd_size_type dynsymcount;
      unsigned long section_sym_count;
      unsigned int dtagcount;

      dynobj = elf_hash_table (info)->dynobj;

      /* Assign dynsym indicies.  In a shared library we generate a
	 section symbol for each output section, which come first.
	 Next come all of the back-end allocated local dynamic syms,
	 followed by the rest of the global symbols.  */

      dynsymcount = _bfd_elf_link_renumber_dynsyms (output_bfd, info,
						    &section_sym_count);

      /* Work out the size of the symbol version section.  */
      s = bfd_get_section_by_name (dynobj, ".gnu.version");
      BFD_ASSERT (s != NULL);
      if (dynsymcount != 0
	  && (s->flags & SEC_EXCLUDE) == 0)
	{
	  s->size = dynsymcount * sizeof (Elf_External_Versym);
	  s->contents = bfd_zalloc (output_bfd, s->size);
	  if (s->contents == NULL)
	    return FALSE;

	  if (!_bfd_elf_add_dynamic_entry (info, DT_VERSYM, 0))
	    return FALSE;
	}

      /* Set the size of the .dynsym and .hash sections.  We counted
	 the number of dynamic symbols in elf_link_add_object_symbols.
	 We will build the contents of .dynsym and .hash when we build
	 the final symbol table, because until then we do not know the
	 correct value to give the symbols.  We built the .dynstr
	 section as we went along in elf_link_add_object_symbols.  */
      s = bfd_get_section_by_name (dynobj, ".dynsym");
      BFD_ASSERT (s != NULL);
      s->size = dynsymcount * bed->s->sizeof_sym;

      if (dynsymcount != 0)
	{
	  s->contents = bfd_alloc (output_bfd, s->size);
	  if (s->contents == NULL)
	    return FALSE;

	  /* The first entry in .dynsym is a dummy symbol.
	     Clear all the section syms, in case we don't output them all.  */
	  ++section_sym_count;
	  memset (s->contents, 0, section_sym_count * bed->s->sizeof_sym);
	}

      elf_hash_table (info)->bucketcount = 0;

      /* Compute the size of the hashing table.  As a side effect this
	 computes the hash values for all the names we export.  */
      if (info->emit_hash)
	{
	  unsigned long int *hashcodes;
	  unsigned long int *hashcodesp;
	  bfd_size_type amt;
	  unsigned long int nsyms;
	  size_t bucketcount;
	  size_t hash_entry_size;

	  /* Compute the hash values for all exported symbols.  At the same
	     time store the values in an array so that we could use them for
	     optimizations.  */
	  amt = dynsymcount * sizeof (unsigned long int);
	  hashcodes = bfd_malloc (amt);
	  if (hashcodes == NULL)
	    return FALSE;
	  hashcodesp = hashcodes;

	  /* Put all hash values in HASHCODES.  */
	  elf_link_hash_traverse (elf_hash_table (info),
				  elf_collect_hash_codes, &hashcodesp);

	  nsyms = hashcodesp - hashcodes;
	  bucketcount
	    = compute_bucket_count (info, hashcodes, nsyms, 0);
	  free (hashcodes);

	  if (bucketcount == 0)
	    return FALSE;

	  elf_hash_table (info)->bucketcount = bucketcount;

	  s = bfd_get_section_by_name (dynobj, ".hash");
	  BFD_ASSERT (s != NULL);
	  hash_entry_size = elf_section_data (s)->this_hdr.sh_entsize;
	  s->size = ((2 + bucketcount + dynsymcount) * hash_entry_size);
	  s->contents = bfd_zalloc (output_bfd, s->size);
	  if (s->contents == NULL)
	    return FALSE;

	  bfd_put (8 * hash_entry_size, output_bfd, bucketcount, s->contents);
	  bfd_put (8 * hash_entry_size, output_bfd, dynsymcount,
		   s->contents + hash_entry_size);
	}

      if (info->emit_gnu_hash)
	{
	  size_t i, cnt;
	  unsigned char *contents;
	  struct collect_gnu_hash_codes cinfo;
	  bfd_size_type amt;
	  size_t bucketcount;

	  memset (&cinfo, 0, sizeof (cinfo));

	  /* Compute the hash values for all exported symbols.  At the same
	     time store the values in an array so that we could use them for
	     optimizations.  */
	  amt = dynsymcount * 2 * sizeof (unsigned long int);
	  cinfo.hashcodes = bfd_malloc (amt);
	  if (cinfo.hashcodes == NULL)
	    return FALSE;

	  cinfo.hashval = cinfo.hashcodes + dynsymcount;
	  cinfo.min_dynindx = -1;
	  cinfo.output_bfd = output_bfd;
	  cinfo.bed = bed;

	  /* Put all hash values in HASHCODES.  */
	  elf_link_hash_traverse (elf_hash_table (info),
				  elf_collect_gnu_hash_codes, &cinfo);

	  bucketcount
	    = compute_bucket_count (info, cinfo.hashcodes, cinfo.nsyms, 1);

	  if (bucketcount == 0)
	    {
	      free (cinfo.hashcodes);
	      return FALSE;
	    }

	  s = bfd_get_section_by_name (dynobj, ".gnu.hash");
	  BFD_ASSERT (s != NULL);

	  if (cinfo.nsyms == 0)
	    {
	      /* Empty .gnu.hash section is special.  */
	      BFD_ASSERT (cinfo.min_dynindx == -1);
	      free (cinfo.hashcodes);
	      s->size = 5 * 4 + bed->s->arch_size / 8;
	      contents = bfd_zalloc (output_bfd, s->size);
	      if (contents == NULL)
		return FALSE;
	      s->contents = contents;
	      /* 1 empty bucket.  */
	      bfd_put_32 (output_bfd, 1, contents);
	      /* SYMIDX above the special symbol 0.  */
	      bfd_put_32 (output_bfd, 1, contents + 4);
	      /* Just one word for bitmask.  */
	      bfd_put_32 (output_bfd, 1, contents + 8);
	      /* Only hash fn bloom filter.  */
	      bfd_put_32 (output_bfd, 0, contents + 12);
	      /* No hashes are valid - empty bitmask.  */
	      bfd_put (bed->s->arch_size, output_bfd, 0, contents + 16);
	      /* No hashes in the only bucket.  */
	      bfd_put_32 (output_bfd, 0,
			  contents + 16 + bed->s->arch_size / 8);
	    }
	  else
	    {
	      unsigned long int maskwords, maskbitslog2;
	      BFD_ASSERT (cinfo.min_dynindx != -1);

	      maskbitslog2 = bfd_log2 (cinfo.nsyms) + 1;
	      if (maskbitslog2 < 3)
		maskbitslog2 = 5;
	      else if ((1 << (maskbitslog2 - 2)) & cinfo.nsyms)
		maskbitslog2 = maskbitslog2 + 3;
	      else
		maskbitslog2 = maskbitslog2 + 2;
	      if (bed->s->arch_size == 64)
		{
		  if (maskbitslog2 == 5)
		    maskbitslog2 = 6;
		  cinfo.shift1 = 6;
		}
	      else
		cinfo.shift1 = 5;
	      cinfo.mask = (1 << cinfo.shift1) - 1;
	      cinfo.shift2 = maskbitslog2;
	      cinfo.maskbits = 1 << maskbitslog2;
	      maskwords = 1 << (maskbitslog2 - cinfo.shift1);
	      amt = bucketcount * sizeof (unsigned long int) * 2;
	      amt += maskwords * sizeof (bfd_vma);
	      cinfo.bitmask = bfd_malloc (amt);
	      if (cinfo.bitmask == NULL)
		{
		  free (cinfo.hashcodes);
		  return FALSE;
		}

	      cinfo.counts = (void *) (cinfo.bitmask + maskwords);
	      cinfo.indx = cinfo.counts + bucketcount;
	      cinfo.symindx = dynsymcount - cinfo.nsyms;
	      memset (cinfo.bitmask, 0, maskwords * sizeof (bfd_vma));

	      /* Determine how often each hash bucket is used.  */
	      memset (cinfo.counts, 0, bucketcount * sizeof (cinfo.counts[0]));
	      for (i = 0; i < cinfo.nsyms; ++i)
		++cinfo.counts[cinfo.hashcodes[i] % bucketcount];

	      for (i = 0, cnt = cinfo.symindx; i < bucketcount; ++i)
		if (cinfo.counts[i] != 0)
		  {
		    cinfo.indx[i] = cnt;
		    cnt += cinfo.counts[i];
		  }
	      BFD_ASSERT (cnt == dynsymcount);
	      cinfo.bucketcount = bucketcount;
	      cinfo.local_indx = cinfo.min_dynindx;

	      s->size = (4 + bucketcount + cinfo.nsyms) * 4;
	      s->size += cinfo.maskbits / 8;
	      contents = bfd_zalloc (output_bfd, s->size);
	      if (contents == NULL)
		{
		  free (cinfo.bitmask);
		  free (cinfo.hashcodes);
		  return FALSE;
		}

	      s->contents = contents;
	      bfd_put_32 (output_bfd, bucketcount, contents);
	      bfd_put_32 (output_bfd, cinfo.symindx, contents + 4);
	      bfd_put_32 (output_bfd, maskwords, contents + 8);
	      bfd_put_32 (output_bfd, cinfo.shift2, contents + 12);
	      contents += 16 + cinfo.maskbits / 8;

	      for (i = 0; i < bucketcount; ++i)
		{
		  if (cinfo.counts[i] == 0)
		    bfd_put_32 (output_bfd, 0, contents);
		  else
		    bfd_put_32 (output_bfd, cinfo.indx[i], contents);
		  contents += 4;
		}

	      cinfo.contents = contents;

	      /* Renumber dynamic symbols, populate .gnu.hash section.  */
	      elf_link_hash_traverse (elf_hash_table (info),
				      elf_renumber_gnu_hash_syms, &cinfo);

	      contents = s->contents + 16;
	      for (i = 0; i < maskwords; ++i)
		{
		  bfd_put (bed->s->arch_size, output_bfd, cinfo.bitmask[i],
			   contents);
		  contents += bed->s->arch_size / 8;
		}

	      free (cinfo.bitmask);
	      free (cinfo.hashcodes);
	    }
	}

      s = bfd_get_section_by_name (dynobj, ".dynstr");
      BFD_ASSERT (s != NULL);

      elf_finalize_dynstr (output_bfd, info);

      s->size = _bfd_elf_strtab_size (elf_hash_table (info)->dynstr);

      for (dtagcount = 0; dtagcount <= info->spare_dynamic_tags; ++dtagcount)
	if (!_bfd_elf_add_dynamic_entry (info, DT_NULL, 0))
	  return FALSE;
    }

  return TRUE;
}

/* Final phase of ELF linker.  */

/* A structure we use to avoid passing large numbers of arguments.  */

struct elf_final_link_info
{
  /* General link information.  */
  struct bfd_link_info *info;
  /* Output BFD.  */
  bfd *output_bfd;
  /* Symbol string table.  */
  struct bfd_strtab_hash *symstrtab;
  /* .dynsym section.  */
  asection *dynsym_sec;
  /* .hash section.  */
  asection *hash_sec;
  /* symbol version section (.gnu.version).  */
  asection *symver_sec;
  /* Buffer large enough to hold contents of any section.  */
  bfd_byte *contents;
  /* Buffer large enough to hold external relocs of any section.  */
  void *external_relocs;
  /* Buffer large enough to hold internal relocs of any section.  */
  Elf_Internal_Rela *internal_relocs;
  /* Buffer large enough to hold external local symbols of any input
     BFD.  */
  bfd_byte *external_syms;
  /* And a buffer for symbol section indices.  */
  Elf_External_Sym_Shndx *locsym_shndx;
  /* Buffer large enough to hold internal local symbols of any input
     BFD.  */
  Elf_Internal_Sym *internal_syms;
  /* Array large enough to hold a symbol index for each local symbol
     of any input BFD.  */
  long *indices;
  /* Array large enough to hold a section pointer for each local
     symbol of any input BFD.  */
  asection **sections;
  /* Buffer to hold swapped out symbols.  */
  bfd_byte *symbuf;
  /* And one for symbol section indices.  */
  Elf_External_Sym_Shndx *symshndxbuf;
  /* Number of swapped out symbols in buffer.  */
  size_t symbuf_count;
  /* Number of symbols which fit in symbuf.  */
  size_t symbuf_size;
  /* And same for symshndxbuf.  */
  size_t shndxbuf_size;
};

/* This struct is used to pass information to elf_link_output_extsym.  */

struct elf_outext_info
{
  bfd_boolean failed;
  bfd_boolean localsyms;
  struct elf_final_link_info *finfo;
};


/* Support for evaluating a complex relocation.

   Complex relocations are generalized, self-describing relocations.  The
   implementation of them consists of two parts: complex symbols, and the
   relocations themselves. 

   The relocations are use a reserved elf-wide relocation type code (R_RELC
   external / BFD_RELOC_RELC internal) and an encoding of relocation field
   information (start bit, end bit, word width, etc) into the addend.  This
   information is extracted from CGEN-generated operand tables within gas.

   Complex symbols are mangled symbols (BSF_RELC external / STT_RELC
   internal) representing prefix-notation expressions, including but not
   limited to those sorts of expressions normally encoded as addends in the
   addend field.  The symbol mangling format is:

   <node> := <literal>
          |  <unary-operator> ':' <node>
          |  <binary-operator> ':' <node> ':' <node>
	  ;

   <literal> := 's' <digits=N> ':' <N character symbol name>
             |  'S' <digits=N> ':' <N character section name>
	     |  '#' <hexdigits>
	     ;

   <binary-operator> := as in C
   <unary-operator> := as in C, plus "0-" for unambiguous negation.  */

static void
set_symbol_value (bfd *                         bfd_with_globals,
		  struct elf_final_link_info *  finfo,    
		  int                           symidx,
		  bfd_vma                       val)
{
  bfd_boolean                    is_local;
  Elf_Internal_Sym *             sym;
  struct elf_link_hash_entry **  sym_hashes;
  struct elf_link_hash_entry *   h;

  sym_hashes = elf_sym_hashes (bfd_with_globals);
  sym = finfo->internal_syms + symidx;  
  is_local = ELF_ST_BIND(sym->st_info) == STB_LOCAL;
  
  if (is_local)
    {
      /* It is a local symbol: move it to the
	 "absolute" section and give it a value.  */
      sym->st_shndx = SHN_ABS;
      sym->st_value = val;
    }
  else 
    {
      /* It is a global symbol: set its link type
	 to "defined" and give it a value.  */
      h = sym_hashes [symidx];	  
      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;
      h->root.type = bfd_link_hash_defined;
      h->root.u.def.value = val;
      h->root.u.def.section = bfd_abs_section_ptr;
    }
}

static bfd_boolean 
resolve_symbol (const char *                  name,
		bfd *                         input_bfd,
		struct elf_final_link_info *  finfo,
		bfd_vma *                     result,
		size_t                        locsymcount)
{
  Elf_Internal_Sym *            sym;
  struct bfd_link_hash_entry *  global_entry;
  const char *                  candidate = NULL;
  Elf_Internal_Shdr *           symtab_hdr;
  asection *                    sec = NULL;
  size_t                        i;
  
  symtab_hdr = & elf_tdata (input_bfd)->symtab_hdr;

  for (i = 0; i < locsymcount; ++ i)
    {
      sym = finfo->internal_syms + i;
      sec = finfo->sections [i];

      if (ELF_ST_BIND (sym->st_info) != STB_LOCAL)
	continue;

      candidate = bfd_elf_string_from_elf_section (input_bfd,
						   symtab_hdr->sh_link,
						   sym->st_name);
#ifdef DEBUG
      printf ("Comparing string: '%s' vs. '%s' = 0x%x\n", 
	      name, candidate, (unsigned int)sym->st_value);
#endif
      if (candidate && strcmp (candidate, name) == 0)
	{
	  * result = sym->st_value;

	  if (sym->st_shndx > SHN_UNDEF && 
	      sym->st_shndx < SHN_LORESERVE)
	    {
#ifdef DEBUG
	      printf ("adjusting for sec '%s' @ 0x%x + 0x%x\n",
		      sec->output_section->name, 
		      (unsigned int)sec->output_section->vma, 
		      (unsigned int)sec->output_offset);
#endif
	      * result += sec->output_offset + sec->output_section->vma;
	    }
#ifdef DEBUG
	  printf ("Found symbol with effective value %8.8x\n", (unsigned int)* result);
#endif
	  return TRUE;
	}
    }

  /* Hmm, haven't found it yet. perhaps it is a global.  */
  global_entry = bfd_link_hash_lookup (finfo->info->hash, name, FALSE, FALSE, TRUE);
  if (!global_entry)
    return FALSE;
  
  if (global_entry->type == bfd_link_hash_defined
      || global_entry->type == bfd_link_hash_defweak)
    {
      * result = global_entry->u.def.value 
	+ global_entry->u.def.section->output_section->vma 
	+ global_entry->u.def.section->output_offset;
#ifdef DEBUG
      printf ("Found GLOBAL symbol '%s' with value %8.8x\n",
	      global_entry->root.string, (unsigned int)*result);
#endif
      return TRUE;
    } 

  if (global_entry->type == bfd_link_hash_common)
    {
      *result = global_entry->u.def.value +
	bfd_com_section_ptr->output_section->vma +
	bfd_com_section_ptr->output_offset;
#ifdef DEBUG
      printf ("Found COMMON symbol '%s' with value %8.8x\n",
	      global_entry->root.string, (unsigned int)*result);
#endif
      return TRUE;
    }
  
  return FALSE;
}

static bfd_boolean
resolve_section (const char *  name,
		 asection *    sections,
		 bfd_vma *     result)
{
  asection *    curr;
  unsigned int  len;

  for (curr = sections; curr; curr = curr->next)    
    if (strcmp (curr->name, name) == 0)
      {
	*result = curr->vma;
	return TRUE;
      }

  /* Hmm. still haven't found it. try pseudo-section names.  */
  for (curr = sections; curr; curr = curr->next)    
    {
      len = strlen (curr->name);
      if (len > strlen (name)) 
	continue;

      if (strncmp (curr->name, name, len) == 0)
	{
	  if (strncmp (".end", name + len, 4) == 0)
	    {
	      *result = curr->vma + curr->size;
	      return TRUE;
	    }

	  /* Insert more pseudo-section names here, if you like.  */
	}
    }
  
  return FALSE;
}

static void
undefined_reference (const char *  reftype,
		     const char *  name)
{
  _bfd_error_handler (_("undefined %s reference in complex symbol: %s"), reftype, name);
}

static bfd_boolean
eval_symbol (bfd_vma *                     result,
	     char *                        sym,
	     char **                       advanced,
	     bfd *                         input_bfd,
	     struct elf_final_link_info *  finfo,
	     bfd_vma                       addr,
	     bfd_vma                       section_offset,
	     size_t                        locsymcount,
	     int                           signed_p)
{
  int           len;
  int           symlen;
  bfd_vma       a;
  bfd_vma       b;
  const int     bufsz = 4096;
  char          symbuf [bufsz];
  const char *  symend;
  bfd_boolean   symbol_is_section = FALSE;

  len = strlen (sym);
  symend = sym + len;

  if (len < 1 || len > bufsz)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }
  
  switch (* sym)
    {
    case '.':
      * result = addr + section_offset;
      * advanced = sym + 1;
      return TRUE;

    case '#':
      ++ sym;
      * result = strtoul (sym, advanced, 16);
      return TRUE;

    case 'S':
      symbol_is_section = TRUE;
    case 's':      
      ++ sym;
      symlen = strtol (sym, &sym, 10);
      ++ sym; /* Skip the trailing ':'.  */

      if ((symend < sym) || ((symlen + 1) > bufsz))
	{
	  bfd_set_error (bfd_error_invalid_operation);
	  return FALSE;
	}

      memcpy (symbuf, sym, symlen);
      symbuf [symlen] = '\0';
      * advanced = sym + symlen;
      
      /* Is it always possible, with complex symbols, that gas "mis-guessed" 
	 the symbol as a section, or vice-versa. so we're pretty liberal in our
	 interpretation here; section means "try section first", not "must be a
	 section", and likewise with symbol.  */

      if (symbol_is_section) 
	{
	  if ((resolve_section (symbuf, finfo->output_bfd->sections, result) != TRUE)
	      && (resolve_symbol (symbuf, input_bfd, finfo, result, locsymcount) != TRUE))
	    {
	      undefined_reference ("section", symbuf);
	      return FALSE;
	    }
	} 
      else 
	{
	  if ((resolve_symbol (symbuf, input_bfd, finfo, result, locsymcount) != TRUE)
	      && (resolve_section (symbuf, finfo->output_bfd->sections,
				   result) != TRUE))
	    {
	      undefined_reference ("symbol", symbuf);
	      return FALSE;
	    }
	}

      return TRUE;
      
      /* All that remains are operators.  */

#define UNARY_OP(op)						\
  if (strncmp (sym, #op, strlen (#op)) == 0)			\
    {								\
      sym += strlen (#op);					\
      if (* sym == ':')						\
        ++ sym;							\
      if (eval_symbol (& a, sym, & sym, input_bfd, finfo, addr, \
                       section_offset, locsymcount, signed_p)   \
	                                             != TRUE)	\
        return FALSE;						\
      if (signed_p)                                             \
        * result = op ((signed)a);         			\
      else                                                      \
        * result = op a;                                        \
      * advanced = sym; 					\
      return TRUE;						\
    }

#define BINARY_OP(op)						\
  if (strncmp (sym, #op, strlen (#op)) == 0)			\
    {								\
      sym += strlen (#op);					\
      if (* sym == ':')						\
        ++ sym;							\
      if (eval_symbol (& a, sym, & sym, input_bfd, finfo, addr, \
                       section_offset, locsymcount, signed_p)   \
                                                     != TRUE)	\
        return FALSE;						\
      ++ sym;							\
      if (eval_symbol (& b, sym, & sym, input_bfd, finfo, addr, \
                       section_offset, locsymcount, signed_p)   \
                                                     != TRUE)	\
        return FALSE;						\
      if (signed_p)                                             \
        * result = ((signed) a) op ((signed) b);	        \
      else                                                      \
        * result = a op b;                                      \
      * advanced = sym;						\
      return TRUE;						\
    }

    default:
      UNARY_OP  (0-);
      BINARY_OP (<<);
      BINARY_OP (>>);
      BINARY_OP (==);
      BINARY_OP (!=);
      BINARY_OP (<=);
      BINARY_OP (>=);
      BINARY_OP (&&);
      BINARY_OP (||);
      UNARY_OP  (~);
      UNARY_OP  (!);
      BINARY_OP (*);
      BINARY_OP (/);
      BINARY_OP (%);
      BINARY_OP (^);
      BINARY_OP (|);
      BINARY_OP (&);
      BINARY_OP (+);
      BINARY_OP (-);
      BINARY_OP (<);
      BINARY_OP (>);
#undef UNARY_OP
#undef BINARY_OP
      _bfd_error_handler (_("unknown operator '%c' in complex symbol"), * sym);
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }
}

/* Entry point to evaluator, called from elf_link_input_bfd.  */

static bfd_boolean
evaluate_complex_relocation_symbols (bfd * input_bfd,
				     struct elf_final_link_info * finfo,
				     size_t locsymcount)
{
  const struct elf_backend_data * bed;
  Elf_Internal_Shdr *             symtab_hdr;
  struct elf_link_hash_entry **   sym_hashes;
  asection *                      reloc_sec;
  bfd_boolean                     result = TRUE;

  /* For each section, we're going to check and see if it has any
     complex relocations, and we're going to evaluate any of them
     we can.  */

  if (finfo->info->relocatable)
    return TRUE;

  symtab_hdr = & elf_tdata (input_bfd)->symtab_hdr;
  sym_hashes = elf_sym_hashes (input_bfd);
  bed = get_elf_backend_data (input_bfd);

  for (reloc_sec = input_bfd->sections; reloc_sec; reloc_sec = reloc_sec->next)
    {
      Elf_Internal_Rela * internal_relocs;
      unsigned long i;

      /* This section was omitted from the link.  */
      if (! reloc_sec->linker_mark)
	continue;

      /* Only process sections containing relocs.  */
      if ((reloc_sec->flags & SEC_RELOC) == 0)
	continue;

      if (reloc_sec->reloc_count == 0)
	continue;

      /* Read in the relocs for this section.  */
      internal_relocs
	= _bfd_elf_link_read_relocs (input_bfd, reloc_sec, NULL,
				     (Elf_Internal_Rela *) NULL,
				     FALSE);
      if (internal_relocs == NULL)
	continue;

      for (i = reloc_sec->reloc_count; i--;)
	{
	  Elf_Internal_Rela * rel;
	  char * sym_name;
	  bfd_vma index;
	  Elf_Internal_Sym * sym;
	  bfd_vma result;
	  bfd_vma section_offset;
	  bfd_vma addr;
	  int signed_p = 0;

	  rel = internal_relocs + i;
	  section_offset = reloc_sec->output_section->vma
	    + reloc_sec->output_offset;
	  addr = rel->r_offset;

	  index = ELF32_R_SYM (rel->r_info);
	  if (bed->s->arch_size == 64)
	    index >>= 24;

	  if (index == STN_UNDEF)
	    continue;

	  if (index < locsymcount)
	    {
	      /* The symbol is local.  */
	      sym = finfo->internal_syms + index;

	      /* We're only processing STT_RELC or STT_SRELC type symbols.  */
	      if ((ELF_ST_TYPE (sym->st_info) != STT_RELC) &&
		  (ELF_ST_TYPE (sym->st_info) != STT_SRELC))
		continue;

	      sym_name = bfd_elf_string_from_elf_section
		(input_bfd, symtab_hdr->sh_link, sym->st_name);

	      signed_p = (ELF_ST_TYPE (sym->st_info) == STT_SRELC);
	    }
	  else
	    {
	      /* The symbol is global.  */
	      struct elf_link_hash_entry * h;

	      if (elf_bad_symtab (input_bfd))
		continue;

	      h = sym_hashes [index - locsymcount];
	      while (   h->root.type == bfd_link_hash_indirect
		     || h->root.type == bfd_link_hash_warning)
		h = (struct elf_link_hash_entry *) h->root.u.i.link;

	      if (h->type != STT_RELC && h->type != STT_SRELC)
		continue;

	      signed_p = (h->type == STT_SRELC);
	      sym_name = (char *) h->root.root.string;
	    }
#ifdef DEBUG
	  printf ("Encountered a complex symbol!");
	  printf (" (input_bfd %s, section %s, reloc %ld\n",
		  input_bfd->filename, reloc_sec->name, i);
	  printf (" symbol: idx  %8.8lx, name %s\n",
		  index, sym_name);
	  printf (" reloc : info %8.8lx, addr %8.8lx\n",
		  rel->r_info, addr);
	  printf (" Evaluating '%s' ...\n ", sym_name);
#endif
	  if (eval_symbol (& result, sym_name, & sym_name, input_bfd, 
			   finfo, addr, section_offset, locsymcount,
			   signed_p))
	    /* Symbol evaluated OK.  Update to absolute value.  */
	    set_symbol_value (input_bfd, finfo, index, result);

	  else
	    result = FALSE;
	}

      if (internal_relocs != elf_section_data (reloc_sec)->relocs)
	free (internal_relocs);
    }

  /* If nothing went wrong, then we adjusted 
     everything we wanted to adjust.  */
  return result;
}

static void
put_value (bfd_vma        size,
	   unsigned long  chunksz,
	   bfd *          input_bfd,
	   bfd_vma        x,
	   bfd_byte *     location)
{
  location += (size - chunksz);

  for (; size; size -= chunksz, location -= chunksz, x >>= (chunksz * 8)) 
    {
      switch (chunksz)
	{
	default:
	case 0:
	  abort ();
	case 1:
	  bfd_put_8 (input_bfd, x, location);
	  break;
	case 2:
	  bfd_put_16 (input_bfd, x, location);
	  break;
	case 4:
	  bfd_put_32 (input_bfd, x, location);
	  break;
	case 8:
#ifdef BFD64
	  bfd_put_64 (input_bfd, x, location);
#else
	  abort ();
#endif
	  break;
	}
    }
}

static bfd_vma 
get_value (bfd_vma        size,
	   unsigned long  chunksz,
	   bfd *          input_bfd,
	   bfd_byte *     location)
{
  bfd_vma x = 0;

  for (; size; size -= chunksz, location += chunksz) 
    {
      switch (chunksz)
	{
	default:
	case 0:
	  abort ();
	case 1:
	  x = (x << (8 * chunksz)) | bfd_get_8 (input_bfd, location);
	  break;
	case 2:
	  x = (x << (8 * chunksz)) | bfd_get_16 (input_bfd, location);
	  break;
	case 4:
	  x = (x << (8 * chunksz)) | bfd_get_32 (input_bfd, location);
	  break;
	case 8:
#ifdef BFD64
	  x = (x << (8 * chunksz)) | bfd_get_64 (input_bfd, location);
#else
	  abort ();
#endif
	  break;
	}
    }
  return x;
}

static void 
decode_complex_addend
    (unsigned long * start,   /* in bits */
     unsigned long * oplen,   /* in bits */
     unsigned long * len,     /* in bits */
     unsigned long * wordsz,  /* in bytes */
     unsigned long * chunksz,  /* in bytes */
     unsigned long * lsb0_p,
     unsigned long * signed_p,
     unsigned long * trunc_p,
     unsigned long encoded)
{
  * start     =  encoded        & 0x3F;
  * len       = (encoded >>  6) & 0x3F;
  * oplen     = (encoded >> 12) & 0x3F;
  * wordsz    = (encoded >> 18) & 0xF;
  * chunksz   = (encoded >> 22) & 0xF;
  * lsb0_p    = (encoded >> 27) & 1;
  * signed_p  = (encoded >> 28) & 1;
  * trunc_p   = (encoded >> 29) & 1;
}

void
bfd_elf_perform_complex_relocation
    (bfd *                   output_bfd ATTRIBUTE_UNUSED,
     struct bfd_link_info *  info,
     bfd *                   input_bfd,
     asection *              input_section,
     bfd_byte *              contents,
     Elf_Internal_Rela *     rel,
     Elf_Internal_Sym *      local_syms,
     asection **             local_sections)
{
  const struct elf_backend_data * bed;
  Elf_Internal_Shdr * symtab_hdr;
  asection * sec;
  bfd_vma relocation = 0, shift, x;
  bfd_vma r_symndx;
  bfd_vma mask;
  unsigned long start, oplen, len, wordsz, 
    chunksz, lsb0_p, signed_p, trunc_p;

  /*  Perform this reloc, since it is complex.
      (this is not to say that it necessarily refers to a complex
      symbol; merely that it is a self-describing CGEN based reloc.
      i.e. the addend has the complete reloc information (bit start, end,
      word size, etc) encoded within it.).  */ 
  r_symndx = ELF32_R_SYM (rel->r_info);
  bed = get_elf_backend_data (input_bfd);
  if (bed->s->arch_size == 64)
    r_symndx >>= 24;

#ifdef DEBUG
  printf ("Performing complex relocation %ld...\n", r_symndx);
#endif

  symtab_hdr = & elf_tdata (input_bfd)->symtab_hdr;
  if (r_symndx < symtab_hdr->sh_info)
    {
      /* The symbol is local.  */
      Elf_Internal_Sym * sym;

      sym = local_syms + r_symndx;
      sec = local_sections [r_symndx];
      relocation = sym->st_value;
      if (sym->st_shndx > SHN_UNDEF && 
	  sym->st_shndx < SHN_LORESERVE)
	relocation += (sec->output_offset +
		       sec->output_section->vma);
    }
  else
    {
      /* The symbol is global.  */
      struct elf_link_hash_entry **sym_hashes;
      struct elf_link_hash_entry * h;

      sym_hashes = elf_sym_hashes (input_bfd);
      h = sym_hashes [r_symndx];

      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;

      if (h->root.type == bfd_link_hash_defined
	  || h->root.type == bfd_link_hash_defweak)
	{
	  sec = h->root.u.def.section;
	  relocation = h->root.u.def.value;

	  if (! bfd_is_abs_section (sec))
	    relocation += (sec->output_section->vma 
			   + sec->output_offset); 
	}
      if (h->root.type == bfd_link_hash_undefined
	  && !((*info->callbacks->undefined_symbol)
	       (info, h->root.root.string, input_bfd,
		input_section, rel->r_offset,
		info->unresolved_syms_in_objects == RM_GENERATE_ERROR
		|| ELF_ST_VISIBILITY (h->other))))
	return;
    }

  decode_complex_addend (& start, & oplen, & len, & wordsz, 
			 & chunksz, & lsb0_p, & signed_p, 
			 & trunc_p, rel->r_addend);

  mask = (((1L << (len - 1)) - 1) << 1) | 1;

  if (lsb0_p)
    shift = (start + 1) - len;
  else
    shift = (8 * wordsz) - (start + len);

  x = get_value (wordsz, chunksz, input_bfd, contents + rel->r_offset);	  

#ifdef DEBUG
  printf ("Doing complex reloc: "
	  "lsb0? %ld, signed? %ld, trunc? %ld, wordsz %ld, "
	  "chunksz %ld, start %ld, len %ld, oplen %ld\n"
	  "    dest: %8.8lx, mask: %8.8lx, reloc: %8.8lx\n",
	  lsb0_p, signed_p, trunc_p, wordsz, chunksz, start, len,
	  oplen, x, mask,  relocation);
#endif

  if (! trunc_p)
    {
      /* Now do an overflow check.  */
      if (bfd_check_overflow ((signed_p ? 
			       complain_overflow_signed : 
			       complain_overflow_unsigned),
			      len, 0, (8 * wordsz), 
			      relocation) == bfd_reloc_overflow)
	(*_bfd_error_handler) 
	  ("%s (%s + 0x%lx): relocation overflow: 0x%lx %sdoes not fit "
	   "within 0x%lx", 
	   input_bfd->filename, input_section->name, rel->r_offset,
	   relocation, (signed_p ? "(signed) " : ""), mask);
    }
	  
  /* Do the deed.  */
  x = (x & ~(mask << shift)) | ((relocation & mask) << shift);

#ifdef DEBUG
  printf ("           relocation: %8.8lx\n"
	  "         shifted mask: %8.8lx\n"
	  " shifted/masked reloc: %8.8lx\n"
	  "               result: %8.8lx\n",
	  relocation, (mask << shift), 
	  ((relocation & mask) << shift), x);
#endif
  put_value (wordsz, chunksz, input_bfd, x, contents + rel->r_offset);
}

/* When performing a relocatable link, the input relocations are
   preserved.  But, if they reference global symbols, the indices
   referenced must be updated.  Update all the relocations in
   REL_HDR (there are COUNT of them), using the data in REL_HASH.  */

static void
elf_link_adjust_relocs (bfd *abfd,
			Elf_Internal_Shdr *rel_hdr,
			unsigned int count,
			struct elf_link_hash_entry **rel_hash)
{
  unsigned int i;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  bfd_byte *erela;
  void (*swap_in) (bfd *, const bfd_byte *, Elf_Internal_Rela *);
  void (*swap_out) (bfd *, const Elf_Internal_Rela *, bfd_byte *);
  bfd_vma r_type_mask;
  int r_sym_shift;

  if (rel_hdr->sh_entsize == bed->s->sizeof_rel)
    {
      swap_in = bed->s->swap_reloc_in;
      swap_out = bed->s->swap_reloc_out;
    }
  else if (rel_hdr->sh_entsize == bed->s->sizeof_rela)
    {
      swap_in = bed->s->swap_reloca_in;
      swap_out = bed->s->swap_reloca_out;
    }
  else
    abort ();

  if (bed->s->int_rels_per_ext_rel > MAX_INT_RELS_PER_EXT_REL)
    abort ();

  if (bed->s->arch_size == 32)
    {
      r_type_mask = 0xff;
      r_sym_shift = 8;
    }
  else
    {
      r_type_mask = 0xffffffff;
      r_sym_shift = 32;
    }

  erela = rel_hdr->contents;
  for (i = 0; i < count; i++, rel_hash++, erela += rel_hdr->sh_entsize)
    {
      Elf_Internal_Rela irela[MAX_INT_RELS_PER_EXT_REL];
      unsigned int j;

      if (*rel_hash == NULL)
	continue;

      BFD_ASSERT ((*rel_hash)->indx >= 0);

      (*swap_in) (abfd, erela, irela);
      for (j = 0; j < bed->s->int_rels_per_ext_rel; j++)
	irela[j].r_info = ((bfd_vma) (*rel_hash)->indx << r_sym_shift
			   | (irela[j].r_info & r_type_mask));
      (*swap_out) (abfd, irela, erela);
    }
}

struct elf_link_sort_rela
{
  union {
    bfd_vma offset;
    bfd_vma sym_mask;
  } u;
  enum elf_reloc_type_class type;
  /* We use this as an array of size int_rels_per_ext_rel.  */
  Elf_Internal_Rela rela[1];
};

static int
elf_link_sort_cmp1 (const void *A, const void *B)
{
  const struct elf_link_sort_rela *a = A;
  const struct elf_link_sort_rela *b = B;
  int relativea, relativeb;

  relativea = a->type == reloc_class_relative;
  relativeb = b->type == reloc_class_relative;

  if (relativea < relativeb)
    return 1;
  if (relativea > relativeb)
    return -1;
  if ((a->rela->r_info & a->u.sym_mask) < (b->rela->r_info & b->u.sym_mask))
    return -1;
  if ((a->rela->r_info & a->u.sym_mask) > (b->rela->r_info & b->u.sym_mask))
    return 1;
  if (a->rela->r_offset < b->rela->r_offset)
    return -1;
  if (a->rela->r_offset > b->rela->r_offset)
    return 1;
  return 0;
}

static int
elf_link_sort_cmp2 (const void *A, const void *B)
{
  const struct elf_link_sort_rela *a = A;
  const struct elf_link_sort_rela *b = B;
  int copya, copyb;

  if (a->u.offset < b->u.offset)
    return -1;
  if (a->u.offset > b->u.offset)
    return 1;
  copya = (a->type == reloc_class_copy) * 2 + (a->type == reloc_class_plt);
  copyb = (b->type == reloc_class_copy) * 2 + (b->type == reloc_class_plt);
  if (copya < copyb)
    return -1;
  if (copya > copyb)
    return 1;
  if (a->rela->r_offset < b->rela->r_offset)
    return -1;
  if (a->rela->r_offset > b->rela->r_offset)
    return 1;
  return 0;
}

static size_t
elf_link_sort_relocs (bfd *abfd, struct bfd_link_info *info, asection **psec)
{
  asection *dynamic_relocs;
  asection *rela_dyn;
  asection *rel_dyn;
  bfd_size_type count, size;
  size_t i, ret, sort_elt, ext_size;
  bfd_byte *sort, *s_non_relative, *p;
  struct elf_link_sort_rela *sq;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  int i2e = bed->s->int_rels_per_ext_rel;
  void (*swap_in) (bfd *, const bfd_byte *, Elf_Internal_Rela *);
  void (*swap_out) (bfd *, const Elf_Internal_Rela *, bfd_byte *);
  struct bfd_link_order *lo;
  bfd_vma r_sym_mask;
  bfd_boolean use_rela;

  /* Find a dynamic reloc section.  */
  rela_dyn = bfd_get_section_by_name (abfd, ".rela.dyn");
  rel_dyn  = bfd_get_section_by_name (abfd, ".rel.dyn");
  if (rela_dyn != NULL && rela_dyn->size > 0
      && rel_dyn != NULL && rel_dyn->size > 0)
    {
      bfd_boolean use_rela_initialised = FALSE;

      /* This is just here to stop gcc from complaining.
	 It's initialization checking code is not perfect.  */
      use_rela = TRUE;

      /* Both sections are present.  Examine the sizes
	 of the indirect sections to help us choose.  */
      for (lo = rela_dyn->map_head.link_order; lo != NULL; lo = lo->next)
	if (lo->type == bfd_indirect_link_order)
	  {
	    asection *o = lo->u.indirect.section;

	    if ((o->size % bed->s->sizeof_rela) == 0)
	      {
		if ((o->size % bed->s->sizeof_rel) == 0)
		  /* Section size is divisible by both rel and rela sizes.
		     It is of no help to us.  */
		  ;
		else
		  {
		    /* Section size is only divisible by rela.  */
		    if (use_rela_initialised && (use_rela == FALSE))
		      {
			_bfd_error_handler
			  (_("%B: Unable to sort relocs - they are in more than one size"), abfd);
			bfd_set_error (bfd_error_invalid_operation);
			return 0;
		      }
		    else
		      {
			use_rela = TRUE;
			use_rela_initialised = TRUE;
		      }
		  }
	      }
	    else if ((o->size % bed->s->sizeof_rel) == 0)
	      {
		/* Section size is only divisible by rel.  */
		if (use_rela_initialised && (use_rela == TRUE))
		  {
		    _bfd_error_handler
		      (_("%B: Unable to sort relocs - they are in more than one size"), abfd);
		    bfd_set_error (bfd_error_invalid_operation);
		    return 0;
		  }
		else
		  {
		    use_rela = FALSE;
		    use_rela_initialised = TRUE;
		  }
	      }
	    else
	      {
		/* The section size is not divisible by either - something is wrong.  */
		_bfd_error_handler
		  (_("%B: Unable to sort relocs - they are of an unknown size"), abfd);
		bfd_set_error (bfd_error_invalid_operation);
		return 0;
	      }
	  }

      for (lo = rel_dyn->map_head.link_order; lo != NULL; lo = lo->next)
	if (lo->type == bfd_indirect_link_order)
	  {
	    asection *o = lo->u.indirect.section;

	    if ((o->size % bed->s->sizeof_rela) == 0)
	      {
		if ((o->size % bed->s->sizeof_rel) == 0)
		  /* Section size is divisible by both rel and rela sizes.
		     It is of no help to us.  */
		  ;
		else
		  {
		    /* Section size is only divisible by rela.  */
		    if (use_rela_initialised && (use_rela == FALSE))
		      {
			_bfd_error_handler
			  (_("%B: Unable to sort relocs - they are in more than one size"), abfd);
			bfd_set_error (bfd_error_invalid_operation);
			return 0;
		      }
		    else
		      {
			use_rela = TRUE;
			use_rela_initialised = TRUE;
		      }
		  }
	      }
	    else if ((o->size % bed->s->sizeof_rel) == 0)
	      {
		/* Section size is only divisible by rel.  */
		if (use_rela_initialised && (use_rela == TRUE))
		  {
		    _bfd_error_handler
		      (_("%B: Unable to sort relocs - they are in more than one size"), abfd);
		    bfd_set_error (bfd_error_invalid_operation);
		    return 0;
		  }
		else
		  {
		    use_rela = FALSE;
		    use_rela_initialised = TRUE;
		  }
	      }
	    else
	      {
		/* The section size is not divisible by either - something is wrong.  */
		_bfd_error_handler
		  (_("%B: Unable to sort relocs - they are of an unknown size"), abfd);
		bfd_set_error (bfd_error_invalid_operation);
		return 0;
	      }
	  }

      if (! use_rela_initialised)
	/* Make a guess.  */
	use_rela = TRUE;
    }
  else if (rela_dyn != NULL && rela_dyn->size > 0)
    use_rela = TRUE;
  else if (rel_dyn != NULL && rel_dyn->size > 0)
    use_rela = FALSE;
  else
    return 0;

  if (use_rela)
    {
      dynamic_relocs = rela_dyn;
      ext_size = bed->s->sizeof_rela;
      swap_in = bed->s->swap_reloca_in;
      swap_out = bed->s->swap_reloca_out;
    }
  else
    {
      dynamic_relocs = rel_dyn;
      ext_size = bed->s->sizeof_rel;
      swap_in = bed->s->swap_reloc_in;
      swap_out = bed->s->swap_reloc_out;
    }

  size = 0;
  for (lo = dynamic_relocs->map_head.link_order; lo != NULL; lo = lo->next)
    if (lo->type == bfd_indirect_link_order)
      size += lo->u.indirect.section->size;

  if (size != dynamic_relocs->size)
    return 0;

  sort_elt = (sizeof (struct elf_link_sort_rela)
	      + (i2e - 1) * sizeof (Elf_Internal_Rela));

  count = dynamic_relocs->size / ext_size;
  sort = bfd_zmalloc (sort_elt * count);

  if (sort == NULL)
    {
      (*info->callbacks->warning)
	(info, _("Not enough memory to sort relocations"), 0, abfd, 0, 0);
      return 0;
    }

  if (bed->s->arch_size == 32)
    r_sym_mask = ~(bfd_vma) 0xff;
  else
    r_sym_mask = ~(bfd_vma) 0xffffffff;

  for (lo = dynamic_relocs->map_head.link_order; lo != NULL; lo = lo->next)
    if (lo->type == bfd_indirect_link_order)
      {
	bfd_byte *erel, *erelend;
	asection *o = lo->u.indirect.section;

	if (o->contents == NULL && o->size != 0)
	  {
	    /* This is a reloc section that is being handled as a normal
	       section.  See bfd_section_from_shdr.  We can't combine
	       relocs in this case.  */
	    free (sort);
	    return 0;
	  }
	erel = o->contents;
	erelend = o->contents + o->size;
	p = sort + o->output_offset / ext_size * sort_elt;

	while (erel < erelend)
	  {
	    struct elf_link_sort_rela *s = (struct elf_link_sort_rela *) p;

	    (*swap_in) (abfd, erel, s->rela);
	    s->type = (*bed->elf_backend_reloc_type_class) (s->rela);
	    s->u.sym_mask = r_sym_mask;
	    p += sort_elt;
	    erel += ext_size;
	  }
      }

  qsort (sort, count, sort_elt, elf_link_sort_cmp1);

  for (i = 0, p = sort; i < count; i++, p += sort_elt)
    {
      struct elf_link_sort_rela *s = (struct elf_link_sort_rela *) p;
      if (s->type != reloc_class_relative)
	break;
    }
  ret = i;
  s_non_relative = p;

  sq = (struct elf_link_sort_rela *) s_non_relative;
  for (; i < count; i++, p += sort_elt)
    {
      struct elf_link_sort_rela *sp = (struct elf_link_sort_rela *) p;
      if (((sp->rela->r_info ^ sq->rela->r_info) & r_sym_mask) != 0)
	sq = sp;
      sp->u.offset = sq->rela->r_offset;
    }

  qsort (s_non_relative, count - ret, sort_elt, elf_link_sort_cmp2);

  for (lo = dynamic_relocs->map_head.link_order; lo != NULL; lo = lo->next)
    if (lo->type == bfd_indirect_link_order)
      {
	bfd_byte *erel, *erelend;
	asection *o = lo->u.indirect.section;

	erel = o->contents;
	erelend = o->contents + o->size;
	p = sort + o->output_offset / ext_size * sort_elt;
	while (erel < erelend)
	  {
	    struct elf_link_sort_rela *s = (struct elf_link_sort_rela *) p;
	    (*swap_out) (abfd, s->rela, erel);
	    p += sort_elt;
	    erel += ext_size;
	  }
      }

  free (sort);
  *psec = dynamic_relocs;
  return ret;
}

/* Flush the output symbols to the file.  */

static bfd_boolean
elf_link_flush_output_syms (struct elf_final_link_info *finfo,
			    const struct elf_backend_data *bed)
{
  if (finfo->symbuf_count > 0)
    {
      Elf_Internal_Shdr *hdr;
      file_ptr pos;
      bfd_size_type amt;

      hdr = &elf_tdata (finfo->output_bfd)->symtab_hdr;
      pos = hdr->sh_offset + hdr->sh_size;
      amt = finfo->symbuf_count * bed->s->sizeof_sym;
      if (bfd_seek (finfo->output_bfd, pos, SEEK_SET) != 0
	  || bfd_bwrite (finfo->symbuf, amt, finfo->output_bfd) != amt)
	return FALSE;

      hdr->sh_size += amt;
      finfo->symbuf_count = 0;
    }

  return TRUE;
}

/* Add a symbol to the output symbol table.  */

static bfd_boolean
elf_link_output_sym (struct elf_final_link_info *finfo,
		     const char *name,
		     Elf_Internal_Sym *elfsym,
		     asection *input_sec,
		     struct elf_link_hash_entry *h)
{
  bfd_byte *dest;
  Elf_External_Sym_Shndx *destshndx;
  bfd_boolean (*output_symbol_hook)
    (struct bfd_link_info *, const char *, Elf_Internal_Sym *, asection *,
     struct elf_link_hash_entry *);
  const struct elf_backend_data *bed;

  bed = get_elf_backend_data (finfo->output_bfd);
  output_symbol_hook = bed->elf_backend_link_output_symbol_hook;
  if (output_symbol_hook != NULL)
    {
      if (! (*output_symbol_hook) (finfo->info, name, elfsym, input_sec, h))
	return FALSE;
    }

  if (name == NULL || *name == '\0')
    elfsym->st_name = 0;
  else if (input_sec->flags & SEC_EXCLUDE)
    elfsym->st_name = 0;
  else
    {
      elfsym->st_name = (unsigned long) _bfd_stringtab_add (finfo->symstrtab,
							    name, TRUE, FALSE);
      if (elfsym->st_name == (unsigned long) -1)
	return FALSE;
    }

  if (finfo->symbuf_count >= finfo->symbuf_size)
    {
      if (! elf_link_flush_output_syms (finfo, bed))
	return FALSE;
    }

  dest = finfo->symbuf + finfo->symbuf_count * bed->s->sizeof_sym;
  destshndx = finfo->symshndxbuf;
  if (destshndx != NULL)
    {
      if (bfd_get_symcount (finfo->output_bfd) >= finfo->shndxbuf_size)
	{
	  bfd_size_type amt;

	  amt = finfo->shndxbuf_size * sizeof (Elf_External_Sym_Shndx);
	  finfo->symshndxbuf = destshndx = bfd_realloc (destshndx, amt * 2);
	  if (destshndx == NULL)
	    return FALSE;
	  memset ((char *) destshndx + amt, 0, amt);
	  finfo->shndxbuf_size *= 2;
	}
      destshndx += bfd_get_symcount (finfo->output_bfd);
    }

  bed->s->swap_symbol_out (finfo->output_bfd, elfsym, dest, destshndx);
  finfo->symbuf_count += 1;
  bfd_get_symcount (finfo->output_bfd) += 1;

  return TRUE;
}

/* Return TRUE if the dynamic symbol SYM in ABFD is supported.  */

static bfd_boolean
check_dynsym (bfd *abfd, Elf_Internal_Sym *sym)
{
  if (sym->st_shndx > SHN_HIRESERVE)
    {
      /* The gABI doesn't support dynamic symbols in output sections
         beyond 64k.  */
      (*_bfd_error_handler)
	(_("%B: Too many sections: %d (>= %d)"),
	 abfd, bfd_count_sections (abfd), SHN_LORESERVE);
      bfd_set_error (bfd_error_nonrepresentable_section);
      return FALSE;
    }
  return TRUE;
}

/* For DSOs loaded in via a DT_NEEDED entry, emulate ld.so in
   allowing an unsatisfied unversioned symbol in the DSO to match a
   versioned symbol that would normally require an explicit version.
   We also handle the case that a DSO references a hidden symbol
   which may be satisfied by a versioned symbol in another DSO.  */

static bfd_boolean
elf_link_check_versioned_symbol (struct bfd_link_info *info,
				 const struct elf_backend_data *bed,
				 struct elf_link_hash_entry *h)
{
  bfd *abfd;
  struct elf_link_loaded_list *loaded;

  if (!is_elf_hash_table (info->hash))
    return FALSE;

  switch (h->root.type)
    {
    default:
      abfd = NULL;
      break;

    case bfd_link_hash_undefined:
    case bfd_link_hash_undefweak:
      abfd = h->root.u.undef.abfd;
      if ((abfd->flags & DYNAMIC) == 0
	  || (elf_dyn_lib_class (abfd) & DYN_DT_NEEDED) == 0)
	return FALSE;
      break;

    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      abfd = h->root.u.def.section->owner;
      break;

    case bfd_link_hash_common:
      abfd = h->root.u.c.p->section->owner;
      break;
    }
  BFD_ASSERT (abfd != NULL);

  for (loaded = elf_hash_table (info)->loaded;
       loaded != NULL;
       loaded = loaded->next)
    {
      bfd *input;
      Elf_Internal_Shdr *hdr;
      bfd_size_type symcount;
      bfd_size_type extsymcount;
      bfd_size_type extsymoff;
      Elf_Internal_Shdr *versymhdr;
      Elf_Internal_Sym *isym;
      Elf_Internal_Sym *isymend;
      Elf_Internal_Sym *isymbuf;
      Elf_External_Versym *ever;
      Elf_External_Versym *extversym;

      input = loaded->abfd;

      /* We check each DSO for a possible hidden versioned definition.  */
      if (input == abfd
	  || (input->flags & DYNAMIC) == 0
	  || elf_dynversym (input) == 0)
	continue;

      hdr = &elf_tdata (input)->dynsymtab_hdr;

      symcount = hdr->sh_size / bed->s->sizeof_sym;
      if (elf_bad_symtab (input))
	{
	  extsymcount = symcount;
	  extsymoff = 0;
	}
      else
	{
	  extsymcount = symcount - hdr->sh_info;
	  extsymoff = hdr->sh_info;
	}

      if (extsymcount == 0)
	continue;

      isymbuf = bfd_elf_get_elf_syms (input, hdr, extsymcount, extsymoff,
				      NULL, NULL, NULL);
      if (isymbuf == NULL)
	return FALSE;

      /* Read in any version definitions.  */
      versymhdr = &elf_tdata (input)->dynversym_hdr;
      extversym = bfd_malloc (versymhdr->sh_size);
      if (extversym == NULL)
	goto error_ret;

      if (bfd_seek (input, versymhdr->sh_offset, SEEK_SET) != 0
	  || (bfd_bread (extversym, versymhdr->sh_size, input)
	      != versymhdr->sh_size))
	{
	  free (extversym);
	error_ret:
	  free (isymbuf);
	  return FALSE;
	}

      ever = extversym + extsymoff;
      isymend = isymbuf + extsymcount;
      for (isym = isymbuf; isym < isymend; isym++, ever++)
	{
	  const char *name;
	  Elf_Internal_Versym iver;
	  unsigned short version_index;

	  if (ELF_ST_BIND (isym->st_info) == STB_LOCAL
	      || isym->st_shndx == SHN_UNDEF)
	    continue;

	  name = bfd_elf_string_from_elf_section (input,
						  hdr->sh_link,
						  isym->st_name);
	  if (strcmp (name, h->root.root.string) != 0)
	    continue;

	  _bfd_elf_swap_versym_in (input, ever, &iver);

	  if ((iver.vs_vers & VERSYM_HIDDEN) == 0)
	    {
	      /* If we have a non-hidden versioned sym, then it should
		 have provided a definition for the undefined sym.  */
	      abort ();
	    }

	  version_index = iver.vs_vers & VERSYM_VERSION;
	  if (version_index == 1 || version_index == 2)
	    {
	      /* This is the base or first version.  We can use it.  */
	      free (extversym);
	      free (isymbuf);
	      return TRUE;
	    }
	}

      free (extversym);
      free (isymbuf);
    }

  return FALSE;
}

/* Add an external symbol to the symbol table.  This is called from
   the hash table traversal routine.  When generating a shared object,
   we go through the symbol table twice.  The first time we output
   anything that might have been forced to local scope in a version
   script.  The second time we output the symbols that are still
   global symbols.  */

static bfd_boolean
elf_link_output_extsym (struct elf_link_hash_entry *h, void *data)
{
  struct elf_outext_info *eoinfo = data;
  struct elf_final_link_info *finfo = eoinfo->finfo;
  bfd_boolean strip;
  Elf_Internal_Sym sym;
  asection *input_sec;
  const struct elf_backend_data *bed;

  if (h->root.type == bfd_link_hash_warning)
    {
      h = (struct elf_link_hash_entry *) h->root.u.i.link;
      if (h->root.type == bfd_link_hash_new)
	return TRUE;
    }

  /* Decide whether to output this symbol in this pass.  */
  if (eoinfo->localsyms)
    {
      if (!h->forced_local)
	return TRUE;
    }
  else
    {
      if (h->forced_local)
	return TRUE;
    }

  bed = get_elf_backend_data (finfo->output_bfd);

  if (h->root.type == bfd_link_hash_undefined)
    {
      /* If we have an undefined symbol reference here then it must have
	 come from a shared library that is being linked in.  (Undefined
	 references in regular files have already been handled).  */
      bfd_boolean ignore_undef = FALSE;

      /* Some symbols may be special in that the fact that they're
	 undefined can be safely ignored - let backend determine that.  */
      if (bed->elf_backend_ignore_undef_symbol)
	ignore_undef = bed->elf_backend_ignore_undef_symbol (h);

      /* If we are reporting errors for this situation then do so now.  */
      if (ignore_undef == FALSE
	  && h->ref_dynamic
	  && ! h->ref_regular
	  && ! elf_link_check_versioned_symbol (finfo->info, bed, h)
	  && finfo->info->unresolved_syms_in_shared_libs != RM_IGNORE)
	{
	  if (! (finfo->info->callbacks->undefined_symbol
		 (finfo->info, h->root.root.string, h->root.u.undef.abfd,
		  NULL, 0, finfo->info->unresolved_syms_in_shared_libs == RM_GENERATE_ERROR)))
	    {
	      eoinfo->failed = TRUE;
	      return FALSE;
	    }
	}
    }

  /* We should also warn if a forced local symbol is referenced from
     shared libraries.  */
  if (! finfo->info->relocatable
      && (! finfo->info->shared)
      && h->forced_local
      && h->ref_dynamic
      && !h->dynamic_def
      && !h->dynamic_weak
      && ! elf_link_check_versioned_symbol (finfo->info, bed, h))
    {
      (*_bfd_error_handler)
	(_("%B: %s symbol `%s' in %B is referenced by DSO"),
	 finfo->output_bfd,
	 h->root.u.def.section == bfd_abs_section_ptr
	 ? finfo->output_bfd : h->root.u.def.section->owner,
	 ELF_ST_VISIBILITY (h->other) == STV_INTERNAL
	 ? "internal"
	 : ELF_ST_VISIBILITY (h->other) == STV_HIDDEN
	 ? "hidden" : "local",
	 h->root.root.string);
      eoinfo->failed = TRUE;
      return FALSE;
    }

  /* We don't want to output symbols that have never been mentioned by
     a regular file, or that we have been told to strip.  However, if
     h->indx is set to -2, the symbol is used by a reloc and we must
     output it.  */
  if (h->indx == -2)
    strip = FALSE;
  else if ((h->def_dynamic
	    || h->ref_dynamic
	    || h->root.type == bfd_link_hash_new)
	   && !h->def_regular
	   && !h->ref_regular)
    strip = TRUE;
  else if (finfo->info->strip == strip_all)
    strip = TRUE;
  else if (finfo->info->strip == strip_some
	   && bfd_hash_lookup (finfo->info->keep_hash,
			       h->root.root.string, FALSE, FALSE) == NULL)
    strip = TRUE;
  else if (finfo->info->strip_discarded
	   && (h->root.type == bfd_link_hash_defined
	       || h->root.type == bfd_link_hash_defweak)
	   && elf_discarded_section (h->root.u.def.section))
    strip = TRUE;
  else
    strip = FALSE;

  /* If we're stripping it, and it's not a dynamic symbol, there's
     nothing else to do unless it is a forced local symbol.  */
  if (strip
      && h->dynindx == -1
      && !h->forced_local)
    return TRUE;

  sym.st_value = 0;
  sym.st_size = h->size;
  sym.st_other = h->other;
  if (h->forced_local)
    sym.st_info = ELF_ST_INFO (STB_LOCAL, h->type);
  else if (h->root.type == bfd_link_hash_undefweak
	   || h->root.type == bfd_link_hash_defweak)
    sym.st_info = ELF_ST_INFO (STB_WEAK, h->type);
  else
    sym.st_info = ELF_ST_INFO (STB_GLOBAL, h->type);

  switch (h->root.type)
    {
    default:
    case bfd_link_hash_new:
    case bfd_link_hash_warning:
      abort ();
      return FALSE;

    case bfd_link_hash_undefined:
    case bfd_link_hash_undefweak:
      input_sec = bfd_und_section_ptr;
      sym.st_shndx = SHN_UNDEF;
      break;

    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      {
	input_sec = h->root.u.def.section;
	if (input_sec->output_section != NULL)
	  {
	    sym.st_shndx =
	      _bfd_elf_section_from_bfd_section (finfo->output_bfd,
						 input_sec->output_section);
	    if (sym.st_shndx == SHN_BAD)
	      {
		(*_bfd_error_handler)
		  (_("%B: could not find output section %A for input section %A"),
		   finfo->output_bfd, input_sec->output_section, input_sec);
		eoinfo->failed = TRUE;
		return FALSE;
	      }

	    /* ELF symbols in relocatable files are section relative,
	       but in nonrelocatable files they are virtual
	       addresses.  */
	    sym.st_value = h->root.u.def.value + input_sec->output_offset;
	    if (! finfo->info->relocatable)
	      {
		sym.st_value += input_sec->output_section->vma;
		if (h->type == STT_TLS)
		  {
		    /* STT_TLS symbols are relative to PT_TLS segment
		       base.  */
		    BFD_ASSERT (elf_hash_table (finfo->info)->tls_sec != NULL);
		    sym.st_value -= elf_hash_table (finfo->info)->tls_sec->vma;
		  }
	      }
	  }
	else
	  {
	    BFD_ASSERT (input_sec->owner == NULL
			|| (input_sec->owner->flags & DYNAMIC) != 0);
	    sym.st_shndx = SHN_UNDEF;
	    input_sec = bfd_und_section_ptr;
	  }
      }
      break;

    case bfd_link_hash_common:
      input_sec = h->root.u.c.p->section;
      sym.st_shndx = bed->common_section_index (input_sec);
      sym.st_value = 1 << h->root.u.c.p->alignment_power;
      break;

    case bfd_link_hash_indirect:
      /* These symbols are created by symbol versioning.  They point
	 to the decorated version of the name.  For example, if the
	 symbol foo@@GNU_1.2 is the default, which should be used when
	 foo is used with no version, then we add an indirect symbol
	 foo which points to foo@@GNU_1.2.  We ignore these symbols,
	 since the indirected symbol is already in the hash table.  */
      return TRUE;
    }

  /* Give the processor backend a chance to tweak the symbol value,
     and also to finish up anything that needs to be done for this
     symbol.  FIXME: Not calling elf_backend_finish_dynamic_symbol for
     forced local syms when non-shared is due to a historical quirk.  */
  if ((h->dynindx != -1
       || h->forced_local)
      && ((finfo->info->shared
	   && (ELF_ST_VISIBILITY (h->other) == STV_DEFAULT
	       || h->root.type != bfd_link_hash_undefweak))
	  || !h->forced_local)
      && elf_hash_table (finfo->info)->dynamic_sections_created)
    {
      if (! ((*bed->elf_backend_finish_dynamic_symbol)
	     (finfo->output_bfd, finfo->info, h, &sym)))
	{
	  eoinfo->failed = TRUE;
	  return FALSE;
	}
    }

  /* If we are marking the symbol as undefined, and there are no
     non-weak references to this symbol from a regular object, then
     mark the symbol as weak undefined; if there are non-weak
     references, mark the symbol as strong.  We can't do this earlier,
     because it might not be marked as undefined until the
     finish_dynamic_symbol routine gets through with it.  */
  if (sym.st_shndx == SHN_UNDEF
      && h->ref_regular
      && (ELF_ST_BIND (sym.st_info) == STB_GLOBAL
	  || ELF_ST_BIND (sym.st_info) == STB_WEAK))
    {
      int bindtype;

      if (h->ref_regular_nonweak)
	bindtype = STB_GLOBAL;
      else
	bindtype = STB_WEAK;
      sym.st_info = ELF_ST_INFO (bindtype, ELF_ST_TYPE (sym.st_info));
    }

  /* If a non-weak symbol with non-default visibility is not defined
     locally, it is a fatal error.  */
  if (! finfo->info->relocatable
      && ELF_ST_VISIBILITY (sym.st_other) != STV_DEFAULT
      && ELF_ST_BIND (sym.st_info) != STB_WEAK
      && h->root.type == bfd_link_hash_undefined
      && !h->def_regular)
    {
      (*_bfd_error_handler)
	(_("%B: %s symbol `%s' isn't defined"),
	 finfo->output_bfd,
	 ELF_ST_VISIBILITY (sym.st_other) == STV_PROTECTED
	 ? "protected"
	 : ELF_ST_VISIBILITY (sym.st_other) == STV_INTERNAL
	 ? "internal" : "hidden",
	 h->root.root.string);
      eoinfo->failed = TRUE;
      return FALSE;
    }

  /* If this symbol should be put in the .dynsym section, then put it
     there now.  We already know the symbol index.  We also fill in
     the entry in the .hash section.  */
  if (h->dynindx != -1
      && elf_hash_table (finfo->info)->dynamic_sections_created)
    {
      bfd_byte *esym;

      sym.st_name = h->dynstr_index;
      esym = finfo->dynsym_sec->contents + h->dynindx * bed->s->sizeof_sym;
      if (! check_dynsym (finfo->output_bfd, &sym))
	{
	  eoinfo->failed = TRUE;
	  return FALSE;
	}
      bed->s->swap_symbol_out (finfo->output_bfd, &sym, esym, 0);

      if (finfo->hash_sec != NULL)
	{
	  size_t hash_entry_size;
	  bfd_byte *bucketpos;
	  bfd_vma chain;
	  size_t bucketcount;
	  size_t bucket;

	  bucketcount = elf_hash_table (finfo->info)->bucketcount;
	  bucket = h->u.elf_hash_value % bucketcount;

	  hash_entry_size
	    = elf_section_data (finfo->hash_sec)->this_hdr.sh_entsize;
	  bucketpos = ((bfd_byte *) finfo->hash_sec->contents
		       + (bucket + 2) * hash_entry_size);
	  chain = bfd_get (8 * hash_entry_size, finfo->output_bfd, bucketpos);
	  bfd_put (8 * hash_entry_size, finfo->output_bfd, h->dynindx, bucketpos);
	  bfd_put (8 * hash_entry_size, finfo->output_bfd, chain,
		   ((bfd_byte *) finfo->hash_sec->contents
		    + (bucketcount + 2 + h->dynindx) * hash_entry_size));
	}

      if (finfo->symver_sec != NULL && finfo->symver_sec->contents != NULL)
	{
	  Elf_Internal_Versym iversym;
	  Elf_External_Versym *eversym;

	  if (!h->def_regular)
	    {
	      if (h->verinfo.verdef == NULL)
		iversym.vs_vers = 0;
	      else
		iversym.vs_vers = h->verinfo.verdef->vd_exp_refno + 1;
	    }
	  else
	    {
	      if (h->verinfo.vertree == NULL)
		iversym.vs_vers = 1;
	      else
		iversym.vs_vers = h->verinfo.vertree->vernum + 1;
	      if (finfo->info->create_default_symver)
		iversym.vs_vers++;
	    }

	  if (h->hidden)
	    iversym.vs_vers |= VERSYM_HIDDEN;

	  eversym = (Elf_External_Versym *) finfo->symver_sec->contents;
	  eversym += h->dynindx;
	  _bfd_elf_swap_versym_out (finfo->output_bfd, &iversym, eversym);
	}
    }

  /* If we're stripping it, then it was just a dynamic symbol, and
     there's nothing else to do.  */
  if (strip || (input_sec->flags & SEC_EXCLUDE) != 0)
    return TRUE;

  h->indx = bfd_get_symcount (finfo->output_bfd);

  if (! elf_link_output_sym (finfo, h->root.root.string, &sym, input_sec, h))
    {
      eoinfo->failed = TRUE;
      return FALSE;
    }

  return TRUE;
}

/* Return TRUE if special handling is done for relocs in SEC against
   symbols defined in discarded sections.  */

static bfd_boolean
elf_section_ignore_discarded_relocs (asection *sec)
{
  const struct elf_backend_data *bed;

  switch (sec->sec_info_type)
    {
    case ELF_INFO_TYPE_STABS:
    case ELF_INFO_TYPE_EH_FRAME:
      return TRUE;
    default:
      break;
    }

  bed = get_elf_backend_data (sec->owner);
  if (bed->elf_backend_ignore_discarded_relocs != NULL
      && (*bed->elf_backend_ignore_discarded_relocs) (sec))
    return TRUE;

  return FALSE;
}

/* Return a mask saying how ld should treat relocations in SEC against
   symbols defined in discarded sections.  If this function returns
   COMPLAIN set, ld will issue a warning message.  If this function
   returns PRETEND set, and the discarded section was link-once and the
   same size as the kept link-once section, ld will pretend that the
   symbol was actually defined in the kept section.  Otherwise ld will
   zero the reloc (at least that is the intent, but some cooperation by
   the target dependent code is needed, particularly for REL targets).  */

unsigned int
_bfd_elf_default_action_discarded (asection *sec)
{
  if (sec->flags & SEC_DEBUGGING)
    return PRETEND;

  if (strcmp (".eh_frame", sec->name) == 0)
    return 0;

  if (strcmp (".gcc_except_table", sec->name) == 0)
    return 0;

  return COMPLAIN | PRETEND;
}

/* Find a match between a section and a member of a section group.  */

static asection *
match_group_member (asection *sec, asection *group,
		    struct bfd_link_info *info)
{
  asection *first = elf_next_in_group (group);
  asection *s = first;

  while (s != NULL)
    {
      if (bfd_elf_match_symbols_in_sections (s, sec, info))
	return s;

      s = elf_next_in_group (s);
      if (s == first)
	break;
    }

  return NULL;
}

/* Check if the kept section of a discarded section SEC can be used
   to replace it.  Return the replacement if it is OK.  Otherwise return
   NULL.  */

asection *
_bfd_elf_check_kept_section (asection *sec, struct bfd_link_info *info)
{
  asection *kept;

  kept = sec->kept_section;
  if (kept != NULL)
    {
      if ((kept->flags & SEC_GROUP) != 0)
	kept = match_group_member (sec, kept, info);
      if (kept != NULL && sec->size != kept->size)
	kept = NULL;
      sec->kept_section = kept;
    }
  return kept;
}

/* Link an input file into the linker output file.  This function
   handles all the sections and relocations of the input file at once.
   This is so that we only have to read the local symbols once, and
   don't have to keep them in memory.  */

static bfd_boolean
elf_link_input_bfd (struct elf_final_link_info *finfo, bfd *input_bfd)
{
  int (*relocate_section)
    (bfd *, struct bfd_link_info *, bfd *, asection *, bfd_byte *,
     Elf_Internal_Rela *, Elf_Internal_Sym *, asection **);
  bfd *output_bfd;
  Elf_Internal_Shdr *symtab_hdr;
  size_t locsymcount;
  size_t extsymoff;
  Elf_Internal_Sym *isymbuf;
  Elf_Internal_Sym *isym;
  Elf_Internal_Sym *isymend;
  long *pindex;
  asection **ppsection;
  asection *o;
  const struct elf_backend_data *bed;
  struct elf_link_hash_entry **sym_hashes;

  output_bfd = finfo->output_bfd;
  bed = get_elf_backend_data (output_bfd);
  relocate_section = bed->elf_backend_relocate_section;

  /* If this is a dynamic object, we don't want to do anything here:
     we don't want the local symbols, and we don't want the section
     contents.  */
  if ((input_bfd->flags & DYNAMIC) != 0)
    return TRUE;

  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  if (elf_bad_symtab (input_bfd))
    {
      locsymcount = symtab_hdr->sh_size / bed->s->sizeof_sym;
      extsymoff = 0;
    }
  else
    {
      locsymcount = symtab_hdr->sh_info;
      extsymoff = symtab_hdr->sh_info;
    }

  /* Read the local symbols.  */
  isymbuf = (Elf_Internal_Sym *) symtab_hdr->contents;
  if (isymbuf == NULL && locsymcount != 0)
    {
      isymbuf = bfd_elf_get_elf_syms (input_bfd, symtab_hdr, locsymcount, 0,
				      finfo->internal_syms,
				      finfo->external_syms,
				      finfo->locsym_shndx);
      if (isymbuf == NULL)
	return FALSE;
    }
  /* evaluate_complex_relocation_symbols looks for symbols in
     finfo->internal_syms.  */
  else if (isymbuf != NULL && locsymcount != 0)
    {
      bfd_elf_get_elf_syms (input_bfd, symtab_hdr, locsymcount, 0,
			    finfo->internal_syms,
			    finfo->external_syms,
			    finfo->locsym_shndx);
    }

  /* Find local symbol sections and adjust values of symbols in
     SEC_MERGE sections.  Write out those local symbols we know are
     going into the output file.  */
  isymend = isymbuf + locsymcount;
  for (isym = isymbuf, pindex = finfo->indices, ppsection = finfo->sections;
       isym < isymend;
       isym++, pindex++, ppsection++)
    {
      asection *isec;
      const char *name;
      Elf_Internal_Sym osym;

      *pindex = -1;

      if (elf_bad_symtab (input_bfd))
	{
	  if (ELF_ST_BIND (isym->st_info) != STB_LOCAL)
	    {
	      *ppsection = NULL;
	      continue;
	    }
	}

      if (isym->st_shndx == SHN_UNDEF)
	isec = bfd_und_section_ptr;
      else if (isym->st_shndx < SHN_LORESERVE
	       || isym->st_shndx > SHN_HIRESERVE)
	{
	  isec = bfd_section_from_elf_index (input_bfd, isym->st_shndx);
	  if (isec
	      && isec->sec_info_type == ELF_INFO_TYPE_MERGE
	      && ELF_ST_TYPE (isym->st_info) != STT_SECTION)
	    isym->st_value =
	      _bfd_merged_section_offset (output_bfd, &isec,
					  elf_section_data (isec)->sec_info,
					  isym->st_value);
	}
      else if (isym->st_shndx == SHN_ABS)
	isec = bfd_abs_section_ptr;
      else if (isym->st_shndx == SHN_COMMON)
	isec = bfd_com_section_ptr;
      else
	{
	  /* Don't attempt to output symbols with st_shnx in the
	     reserved range other than SHN_ABS and SHN_COMMON.  */
	  *ppsection = NULL;
	  continue;
	}

      *ppsection = isec;

      /* Don't output the first, undefined, symbol.  */
      if (ppsection == finfo->sections)
	continue;

      if (ELF_ST_TYPE (isym->st_info) == STT_SECTION)
	{
	  /* We never output section symbols.  Instead, we use the
	     section symbol of the corresponding section in the output
	     file.  */
	  continue;
	}

      /* If we are stripping all symbols, we don't want to output this
	 one.  */
      if (finfo->info->strip == strip_all)
	continue;

      /* If we are discarding all local symbols, we don't want to
	 output this one.  If we are generating a relocatable output
	 file, then some of the local symbols may be required by
	 relocs; we output them below as we discover that they are
	 needed.  */
      if (finfo->info->discard == discard_all)
	continue;

      /* If this symbol is defined in a section which we are
	 discarding, we don't need to keep it.  */
      if (isym->st_shndx != SHN_UNDEF
	  && (isym->st_shndx < SHN_LORESERVE || isym->st_shndx > SHN_HIRESERVE)
	  && (isec == NULL
	      || bfd_section_removed_from_list (output_bfd,
						isec->output_section)))
	continue;

      /* Get the name of the symbol.  */
      name = bfd_elf_string_from_elf_section (input_bfd, symtab_hdr->sh_link,
					      isym->st_name);
      if (name == NULL)
	return FALSE;

      /* See if we are discarding symbols with this name.  */
      if ((finfo->info->strip == strip_some
	   && (bfd_hash_lookup (finfo->info->keep_hash, name, FALSE, FALSE)
	       == NULL))
	  || (((finfo->info->discard == discard_sec_merge
		&& (isec->flags & SEC_MERGE) && ! finfo->info->relocatable)
	       || finfo->info->discard == discard_l)
	      && bfd_is_local_label_name (input_bfd, name)))
	continue;

      /* If we get here, we are going to output this symbol.  */

      osym = *isym;

      /* Adjust the section index for the output file.  */
      osym.st_shndx = _bfd_elf_section_from_bfd_section (output_bfd,
							 isec->output_section);
      if (osym.st_shndx == SHN_BAD)
	return FALSE;

      *pindex = bfd_get_symcount (output_bfd);

      /* ELF symbols in relocatable files are section relative, but
	 in executable files they are virtual addresses.  Note that
	 this code assumes that all ELF sections have an associated
	 BFD section with a reasonable value for output_offset; below
	 we assume that they also have a reasonable value for
	 output_section.  Any special sections must be set up to meet
	 these requirements.  */
      osym.st_value += isec->output_offset;
      if (! finfo->info->relocatable)
	{
	  osym.st_value += isec->output_section->vma;
	  if (ELF_ST_TYPE (osym.st_info) == STT_TLS)
	    {
	      /* STT_TLS symbols are relative to PT_TLS segment base.  */
	      BFD_ASSERT (elf_hash_table (finfo->info)->tls_sec != NULL);
	      osym.st_value -= elf_hash_table (finfo->info)->tls_sec->vma;
	    }
	}

      if (! elf_link_output_sym (finfo, name, &osym, isec, NULL))
	return FALSE;
    }

  if (! evaluate_complex_relocation_symbols (input_bfd, finfo, locsymcount))
    return FALSE;

  /* Relocate the contents of each section.  */
  sym_hashes = elf_sym_hashes (input_bfd);
  for (o = input_bfd->sections; o != NULL; o = o->next)
    {
      bfd_byte *contents;

      if (! o->linker_mark)
	{
	  /* This section was omitted from the link.  */
	  continue;
	}

      if ((o->flags & SEC_HAS_CONTENTS) == 0
	  || (o->size == 0 && (o->flags & SEC_RELOC) == 0))
	continue;

      if ((o->flags & SEC_LINKER_CREATED) != 0)
	{
	  /* Section was created by _bfd_elf_link_create_dynamic_sections
	     or somesuch.  */
	  continue;
	}

      /* Get the contents of the section.  They have been cached by a
	 relaxation routine.  Note that o is a section in an input
	 file, so the contents field will not have been set by any of
	 the routines which work on output files.  */
      if (elf_section_data (o)->this_hdr.contents != NULL)
	contents = elf_section_data (o)->this_hdr.contents;
      else
	{
	  bfd_size_type amt = o->rawsize ? o->rawsize : o->size;

	  contents = finfo->contents;
	  if (! bfd_get_section_contents (input_bfd, o, contents, 0, amt))
	    return FALSE;
	}

      if ((o->flags & SEC_RELOC) != 0)
	{
	  Elf_Internal_Rela *internal_relocs;
	  bfd_vma r_type_mask;
	  int r_sym_shift;
	  int ret;

	  /* Get the swapped relocs.  */
	  internal_relocs
	    = _bfd_elf_link_read_relocs (input_bfd, o, finfo->external_relocs,
					 finfo->internal_relocs, FALSE);
	  if (internal_relocs == NULL
	      && o->reloc_count > 0)
	    return FALSE;

	  if (bed->s->arch_size == 32)
	    {
	      r_type_mask = 0xff;
	      r_sym_shift = 8;
	    }
	  else
	    {
	      r_type_mask = 0xffffffff;
	      r_sym_shift = 32;
	    }

	  /* Run through the relocs looking for any against symbols
	     from discarded sections and section symbols from
	     removed link-once sections.  Complain about relocs
	     against discarded sections.  Zero relocs against removed
	     link-once sections.  */
	  if (!elf_section_ignore_discarded_relocs (o))
	    {
	      Elf_Internal_Rela *rel, *relend;
	      unsigned int action = (*bed->action_discarded) (o);

	      rel = internal_relocs;
	      relend = rel + o->reloc_count * bed->s->int_rels_per_ext_rel;
	      for ( ; rel < relend; rel++)
		{
		  unsigned long r_symndx = rel->r_info >> r_sym_shift;
		  asection **ps, *sec;
		  struct elf_link_hash_entry *h = NULL;
		  const char *sym_name;

		  if (r_symndx == STN_UNDEF)
		    continue;

		  if (r_symndx >= locsymcount
		      || (elf_bad_symtab (input_bfd)
			  && finfo->sections[r_symndx] == NULL))
		    {
		      h = sym_hashes[r_symndx - extsymoff];

		      /* Badly formatted input files can contain relocs that
			 reference non-existant symbols.  Check here so that
			 we do not seg fault.  */
		      if (h == NULL)
			{
			  char buffer [32];

			  sprintf_vma (buffer, rel->r_info);
			  (*_bfd_error_handler)
			    (_("error: %B contains a reloc (0x%s) for section %A "
			       "that references a non-existent global symbol"),
			     input_bfd, o, buffer);
			  bfd_set_error (bfd_error_bad_value);
			  return FALSE;
			}

		      while (h->root.type == bfd_link_hash_indirect
			     || h->root.type == bfd_link_hash_warning)
			h = (struct elf_link_hash_entry *) h->root.u.i.link;

		      if (h->root.type != bfd_link_hash_defined
			  && h->root.type != bfd_link_hash_defweak)
			continue;

		      ps = &h->root.u.def.section;
		      sym_name = h->root.root.string;
		    }
		  else
		    {
		      Elf_Internal_Sym *sym = isymbuf + r_symndx;
		      ps = &finfo->sections[r_symndx];
		      sym_name = bfd_elf_sym_name (input_bfd,
						   symtab_hdr,
						   sym, *ps);
		    }

		  /* Complain if the definition comes from a
		     discarded section.  */
		  if ((sec = *ps) != NULL && elf_discarded_section (sec))
		    {
		      BFD_ASSERT (r_symndx != 0);
		      if (action & COMPLAIN)
			(*finfo->info->callbacks->einfo)
			  (_("%X`%s' referenced in section `%A' of %B: "
			     "defined in discarded section `%A' of %B\n"),
			   sym_name, o, input_bfd, sec, sec->owner);

		      /* Try to do the best we can to support buggy old
			 versions of gcc.  Pretend that the symbol is
			 really defined in the kept linkonce section.
			 FIXME: This is quite broken.  Modifying the
			 symbol here means we will be changing all later
			 uses of the symbol, not just in this section.  */
		      if (action & PRETEND)
			{
			  asection *kept;

			  kept = _bfd_elf_check_kept_section (sec,
							      finfo->info);
			  if (kept != NULL)
			    {
			      *ps = kept;
			      continue;
			    }
			}
		    }
		}
	    }

	  /* Relocate the section by invoking a back end routine.

	     The back end routine is responsible for adjusting the
	     section contents as necessary, and (if using Rela relocs
	     and generating a relocatable output file) adjusting the
	     reloc addend as necessary.

	     The back end routine does not have to worry about setting
	     the reloc address or the reloc symbol index.

	     The back end routine is given a pointer to the swapped in
	     internal symbols, and can access the hash table entries
	     for the external symbols via elf_sym_hashes (input_bfd).

	     When generating relocatable output, the back end routine
	     must handle STB_LOCAL/STT_SECTION symbols specially.  The
	     output symbol is going to be a section symbol
	     corresponding to the output section, which will require
	     the addend to be adjusted.  */

	  ret = (*relocate_section) (output_bfd, finfo->info,
				     input_bfd, o, contents,
				     internal_relocs,
				     isymbuf,
				     finfo->sections);
	  if (!ret)
	    return FALSE;

	  if (ret == 2
	      || finfo->info->relocatable
	      || finfo->info->emitrelocations)
	    {
	      Elf_Internal_Rela *irela;
	      Elf_Internal_Rela *irelaend;
	      bfd_vma last_offset;
	      struct elf_link_hash_entry **rel_hash;
	      struct elf_link_hash_entry **rel_hash_list;
	      Elf_Internal_Shdr *input_rel_hdr, *input_rel_hdr2;
	      unsigned int next_erel;
	      bfd_boolean rela_normal;

	      input_rel_hdr = &elf_section_data (o)->rel_hdr;
	      rela_normal = (bed->rela_normal
			     && (input_rel_hdr->sh_entsize
				 == bed->s->sizeof_rela));

	      /* Adjust the reloc addresses and symbol indices.  */

	      irela = internal_relocs;
	      irelaend = irela + o->reloc_count * bed->s->int_rels_per_ext_rel;
	      rel_hash = (elf_section_data (o->output_section)->rel_hashes
			  + elf_section_data (o->output_section)->rel_count
			  + elf_section_data (o->output_section)->rel_count2);
	      rel_hash_list = rel_hash;
	      last_offset = o->output_offset;
	      if (!finfo->info->relocatable)
		last_offset += o->output_section->vma;
	      for (next_erel = 0; irela < irelaend; irela++, next_erel++)
		{
		  unsigned long r_symndx;
		  asection *sec;
		  Elf_Internal_Sym sym;

		  if (next_erel == bed->s->int_rels_per_ext_rel)
		    {
		      rel_hash++;
		      next_erel = 0;
		    }

		  irela->r_offset = _bfd_elf_section_offset (output_bfd,
							     finfo->info, o,
							     irela->r_offset);
		  if (irela->r_offset >= (bfd_vma) -2)
		    {
		      /* This is a reloc for a deleted entry or somesuch.
			 Turn it into an R_*_NONE reloc, at the same
			 offset as the last reloc.  elf_eh_frame.c and
			 bfd_elf_discard_info rely on reloc offsets
			 being ordered.  */
		      irela->r_offset = last_offset;
		      irela->r_info = 0;
		      irela->r_addend = 0;
		      continue;
		    }

		  irela->r_offset += o->output_offset;

		  /* Relocs in an executable have to be virtual addresses.  */
		  if (!finfo->info->relocatable)
		    irela->r_offset += o->output_section->vma;

		  last_offset = irela->r_offset;

		  r_symndx = irela->r_info >> r_sym_shift;
		  if (r_symndx == STN_UNDEF)
		    continue;

		  if (r_symndx >= locsymcount
		      || (elf_bad_symtab (input_bfd)
			  && finfo->sections[r_symndx] == NULL))
		    {
		      struct elf_link_hash_entry *rh;
		      unsigned long indx;

		      /* This is a reloc against a global symbol.  We
			 have not yet output all the local symbols, so
			 we do not know the symbol index of any global
			 symbol.  We set the rel_hash entry for this
			 reloc to point to the global hash table entry
			 for this symbol.  The symbol index is then
			 set at the end of bfd_elf_final_link.  */
		      indx = r_symndx - extsymoff;
		      rh = elf_sym_hashes (input_bfd)[indx];
		      while (rh->root.type == bfd_link_hash_indirect
			     || rh->root.type == bfd_link_hash_warning)
			rh = (struct elf_link_hash_entry *) rh->root.u.i.link;

		      /* Setting the index to -2 tells
			 elf_link_output_extsym that this symbol is
			 used by a reloc.  */
		      BFD_ASSERT (rh->indx < 0);
		      rh->indx = -2;

		      *rel_hash = rh;

		      continue;
		    }

		  /* This is a reloc against a local symbol.  */

		  *rel_hash = NULL;
		  sym = isymbuf[r_symndx];
		  sec = finfo->sections[r_symndx];
		  if (ELF_ST_TYPE (sym.st_info) == STT_SECTION)
		    {
		      /* I suppose the backend ought to fill in the
			 section of any STT_SECTION symbol against a
			 processor specific section.  */
		      r_symndx = 0;
		      if (bfd_is_abs_section (sec))
			;
		      else if (sec == NULL || sec->owner == NULL)
			{
			  bfd_set_error (bfd_error_bad_value);
			  return FALSE;
			}
		      else
			{
			  asection *osec = sec->output_section;

			  /* If we have discarded a section, the output
			     section will be the absolute section.  In
			     case of discarded SEC_MERGE sections, use
			     the kept section.  relocate_section should
			     have already handled discarded linkonce
			     sections.  */
			  if (bfd_is_abs_section (osec)
			      && sec->kept_section != NULL
			      && sec->kept_section->output_section != NULL)
			    {
			      osec = sec->kept_section->output_section;
			      irela->r_addend -= osec->vma;
			    }

			  if (!bfd_is_abs_section (osec))
			    {
			      r_symndx = osec->target_index;
			      if (r_symndx == 0)
				{
				  struct elf_link_hash_table *htab;
				  asection *oi;

				  htab = elf_hash_table (finfo->info);
				  oi = htab->text_index_section;
				  if ((osec->flags & SEC_READONLY) == 0
				      && htab->data_index_section != NULL)
				    oi = htab->data_index_section;

				  if (oi != NULL)
				    {
				      irela->r_addend += osec->vma - oi->vma;
				      r_symndx = oi->target_index;
				    }
				}

			      BFD_ASSERT (r_symndx != 0);
			    }
			}

		      /* Adjust the addend according to where the
			 section winds up in the output section.  */
		      if (rela_normal)
			irela->r_addend += sec->output_offset;
		    }
		  else
		    {
		      if (finfo->indices[r_symndx] == -1)
			{
			  unsigned long shlink;
			  const char *name;
			  asection *osec;

			  if (finfo->info->strip == strip_all)
			    {
			      /* You can't do ld -r -s.  */
			      bfd_set_error (bfd_error_invalid_operation);
			      return FALSE;
			    }

			  /* This symbol was skipped earlier, but
			     since it is needed by a reloc, we
			     must output it now.  */
			  shlink = symtab_hdr->sh_link;
			  name = (bfd_elf_string_from_elf_section
				  (input_bfd, shlink, sym.st_name));
			  if (name == NULL)
			    return FALSE;

			  osec = sec->output_section;
			  sym.st_shndx =
			    _bfd_elf_section_from_bfd_section (output_bfd,
							       osec);
			  if (sym.st_shndx == SHN_BAD)
			    return FALSE;

			  sym.st_value += sec->output_offset;
			  if (! finfo->info->relocatable)
			    {
			      sym.st_value += osec->vma;
			      if (ELF_ST_TYPE (sym.st_info) == STT_TLS)
				{
				  /* STT_TLS symbols are relative to PT_TLS
				     segment base.  */
				  BFD_ASSERT (elf_hash_table (finfo->info)
					      ->tls_sec != NULL);
				  sym.st_value -= (elf_hash_table (finfo->info)
						   ->tls_sec->vma);
				}
			    }

			  finfo->indices[r_symndx]
			    = bfd_get_symcount (output_bfd);

			  if (! elf_link_output_sym (finfo, name, &sym, sec,
						     NULL))
			    return FALSE;
			}

		      r_symndx = finfo->indices[r_symndx];
		    }

		  irela->r_info = ((bfd_vma) r_symndx << r_sym_shift
				   | (irela->r_info & r_type_mask));
		}

	      /* Swap out the relocs.  */
	      if (input_rel_hdr->sh_size != 0
		  && !bed->elf_backend_emit_relocs (output_bfd, o,
						    input_rel_hdr,
						    internal_relocs,
						    rel_hash_list))
		return FALSE;

	      input_rel_hdr2 = elf_section_data (o)->rel_hdr2;
	      if (input_rel_hdr2 && input_rel_hdr2->sh_size != 0)
		{
		  internal_relocs += (NUM_SHDR_ENTRIES (input_rel_hdr)
				      * bed->s->int_rels_per_ext_rel);
		  rel_hash_list += NUM_SHDR_ENTRIES (input_rel_hdr);
		  if (!bed->elf_backend_emit_relocs (output_bfd, o,
						     input_rel_hdr2,
						     internal_relocs,
						     rel_hash_list))
		    return FALSE;
		}
	    }
	}

      /* Write out the modified section contents.  */
      if (bed->elf_backend_write_section
	  && (*bed->elf_backend_write_section) (output_bfd, finfo->info, o,
						contents))
	{
	  /* Section written out.  */
	}
      else switch (o->sec_info_type)
	{
	case ELF_INFO_TYPE_STABS:
	  if (! (_bfd_write_section_stabs
		 (output_bfd,
		  &elf_hash_table (finfo->info)->stab_info,
		  o, &elf_section_data (o)->sec_info, contents)))
	    return FALSE;
	  break;
	case ELF_INFO_TYPE_MERGE:
	  if (! _bfd_write_merged_section (output_bfd, o,
					   elf_section_data (o)->sec_info))
	    return FALSE;
	  break;
	case ELF_INFO_TYPE_EH_FRAME:
	  {
	    if (! _bfd_elf_write_section_eh_frame (output_bfd, finfo->info,
						   o, contents))
	      return FALSE;
	  }
	  break;
	default:
	  {
	    if (! (o->flags & SEC_EXCLUDE)
		&& ! bfd_set_section_contents (output_bfd, o->output_section,
					       contents,
					       (file_ptr) o->output_offset,
					       o->size))
	      return FALSE;
	  }
	  break;
	}
    }

  return TRUE;
}

/* Generate a reloc when linking an ELF file.  This is a reloc
   requested by the linker, and does not come from any input file.  This
   is used to build constructor and destructor tables when linking
   with -Ur.  */

static bfd_boolean
elf_reloc_link_order (bfd *output_bfd,
		      struct bfd_link_info *info,
		      asection *output_section,
		      struct bfd_link_order *link_order)
{
  reloc_howto_type *howto;
  long indx;
  bfd_vma offset;
  bfd_vma addend;
  struct elf_link_hash_entry **rel_hash_ptr;
  Elf_Internal_Shdr *rel_hdr;
  const struct elf_backend_data *bed = get_elf_backend_data (output_bfd);
  Elf_Internal_Rela irel[MAX_INT_RELS_PER_EXT_REL];
  bfd_byte *erel;
  unsigned int i;

  howto = bfd_reloc_type_lookup (output_bfd, link_order->u.reloc.p->reloc);
  if (howto == NULL)
    {
      bfd_set_error (bfd_error_bad_value);
      return FALSE;
    }

  addend = link_order->u.reloc.p->addend;

  /* Figure out the symbol index.  */
  rel_hash_ptr = (elf_section_data (output_section)->rel_hashes
		  + elf_section_data (output_section)->rel_count
		  + elf_section_data (output_section)->rel_count2);
  if (link_order->type == bfd_section_reloc_link_order)
    {
      indx = link_order->u.reloc.p->u.section->target_index;
      BFD_ASSERT (indx != 0);
      *rel_hash_ptr = NULL;
    }
  else
    {
      struct elf_link_hash_entry *h;

      /* Treat a reloc against a defined symbol as though it were
	 actually against the section.  */
      h = ((struct elf_link_hash_entry *)
	   bfd_wrapped_link_hash_lookup (output_bfd, info,
					 link_order->u.reloc.p->u.name,
					 FALSE, FALSE, TRUE));
      if (h != NULL
	  && (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak))
	{
	  asection *section;

	  section = h->root.u.def.section;
	  indx = section->output_section->target_index;
	  *rel_hash_ptr = NULL;
	  /* It seems that we ought to add the symbol value to the
	     addend here, but in practice it has already been added
	     because it was passed to constructor_callback.  */
	  addend += section->output_section->vma + section->output_offset;
	}
      else if (h != NULL)
	{
	  /* Setting the index to -2 tells elf_link_output_extsym that
	     this symbol is used by a reloc.  */
	  h->indx = -2;
	  *rel_hash_ptr = h;
	  indx = 0;
	}
      else
	{
	  if (! ((*info->callbacks->unattached_reloc)
		 (info, link_order->u.reloc.p->u.name, NULL, NULL, 0)))
	    return FALSE;
	  indx = 0;
	}
    }

  /* If this is an inplace reloc, we must write the addend into the
     object file.  */
  if (howto->partial_inplace && addend != 0)
    {
      bfd_size_type size;
      bfd_reloc_status_type rstat;
      bfd_byte *buf;
      bfd_boolean ok;
      const char *sym_name;

      size = bfd_get_reloc_size (howto);
      buf = bfd_zmalloc (size);
      if (buf == NULL)
	return FALSE;
      rstat = _bfd_relocate_contents (howto, output_bfd, addend, buf);
      switch (rstat)
	{
	case bfd_reloc_ok:
	  break;

	default:
	case bfd_reloc_outofrange:
	  abort ();

	case bfd_reloc_overflow:
	  if (link_order->type == bfd_section_reloc_link_order)
	    sym_name = bfd_section_name (output_bfd,
					 link_order->u.reloc.p->u.section);
	  else
	    sym_name = link_order->u.reloc.p->u.name;
	  if (! ((*info->callbacks->reloc_overflow)
		 (info, NULL, sym_name, howto->name, addend, NULL,
		  NULL, (bfd_vma) 0)))
	    {
	      free (buf);
	      return FALSE;
	    }
	  break;
	}
      ok = bfd_set_section_contents (output_bfd, output_section, buf,
				     link_order->offset, size);
      free (buf);
      if (! ok)
	return FALSE;
    }

  /* The address of a reloc is relative to the section in a
     relocatable file, and is a virtual address in an executable
     file.  */
  offset = link_order->offset;
  if (! info->relocatable)
    offset += output_section->vma;

  for (i = 0; i < bed->s->int_rels_per_ext_rel; i++)
    {
      irel[i].r_offset = offset;
      irel[i].r_info = 0;
      irel[i].r_addend = 0;
    }
  if (bed->s->arch_size == 32)
    irel[0].r_info = ELF32_R_INFO (indx, howto->type);
  else
    irel[0].r_info = ELF64_R_INFO (indx, howto->type);

  rel_hdr = &elf_section_data (output_section)->rel_hdr;
  erel = rel_hdr->contents;
  if (rel_hdr->sh_type == SHT_REL)
    {
      erel += (elf_section_data (output_section)->rel_count
	       * bed->s->sizeof_rel);
      (*bed->s->swap_reloc_out) (output_bfd, irel, erel);
    }
  else
    {
      irel[0].r_addend = addend;
      erel += (elf_section_data (output_section)->rel_count
	       * bed->s->sizeof_rela);
      (*bed->s->swap_reloca_out) (output_bfd, irel, erel);
    }

  ++elf_section_data (output_section)->rel_count;

  return TRUE;
}


/* Get the output vma of the section pointed to by the sh_link field.  */

static bfd_vma
elf_get_linked_section_vma (struct bfd_link_order *p)
{
  Elf_Internal_Shdr **elf_shdrp;
  asection *s;
  int elfsec;

  s = p->u.indirect.section;
  elf_shdrp = elf_elfsections (s->owner);
  elfsec = _bfd_elf_section_from_bfd_section (s->owner, s);
  elfsec = elf_shdrp[elfsec]->sh_link;
  /* PR 290:
     The Intel C compiler generates SHT_IA_64_UNWIND with
     SHF_LINK_ORDER.  But it doesn't set the sh_link or
     sh_info fields.  Hence we could get the situation
     where elfsec is 0.  */
  if (elfsec == 0)
    {
      const struct elf_backend_data *bed
	= get_elf_backend_data (s->owner);
      if (bed->link_order_error_handler)
	bed->link_order_error_handler
	  (_("%B: warning: sh_link not set for section `%A'"), s->owner, s);
      return 0;
    }
  else
    {
      s = elf_shdrp[elfsec]->bfd_section;
      return s->output_section->vma + s->output_offset;
    }
}


/* Compare two sections based on the locations of the sections they are
   linked to.  Used by elf_fixup_link_order.  */

static int
compare_link_order (const void * a, const void * b)
{
  bfd_vma apos;
  bfd_vma bpos;

  apos = elf_get_linked_section_vma (*(struct bfd_link_order **)a);
  bpos = elf_get_linked_section_vma (*(struct bfd_link_order **)b);
  if (apos < bpos)
    return -1;
  return apos > bpos;
}


/* Looks for sections with SHF_LINK_ORDER set.  Rearranges them into the same
   order as their linked sections.  Returns false if this could not be done
   because an output section includes both ordered and unordered
   sections.  Ideally we'd do this in the linker proper.  */

static bfd_boolean
elf_fixup_link_order (bfd *abfd, asection *o)
{
  int seen_linkorder;
  int seen_other;
  int n;
  struct bfd_link_order *p;
  bfd *sub;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  unsigned elfsec;
  struct bfd_link_order **sections;
  asection *s, *other_sec, *linkorder_sec;
  bfd_vma offset;

  other_sec = NULL;
  linkorder_sec = NULL;
  seen_other = 0;
  seen_linkorder = 0;
  for (p = o->map_head.link_order; p != NULL; p = p->next)
    {
      if (p->type == bfd_indirect_link_order)
	{
	  s = p->u.indirect.section;
	  sub = s->owner;
	  if (bfd_get_flavour (sub) == bfd_target_elf_flavour
	      && elf_elfheader (sub)->e_ident[EI_CLASS] == bed->s->elfclass
	      && (elfsec = _bfd_elf_section_from_bfd_section (sub, s))
	      && elfsec < elf_numsections (sub)
	      && elf_elfsections (sub)[elfsec]->sh_flags & SHF_LINK_ORDER)
	    {
	      seen_linkorder++;
	      linkorder_sec = s;
	    }
	  else
	    {
	      seen_other++;
	      other_sec = s;
	    }
	}
      else
	seen_other++;

      if (seen_other && seen_linkorder)
	{
	  if (other_sec && linkorder_sec)
	    (*_bfd_error_handler) (_("%A has both ordered [`%A' in %B] and unordered [`%A' in %B] sections"),
				   o, linkorder_sec,
				   linkorder_sec->owner, other_sec,
				   other_sec->owner);
	  else
	    (*_bfd_error_handler) (_("%A has both ordered and unordered sections"),
				   o);
	  bfd_set_error (bfd_error_bad_value);
	  return FALSE;
	}
    }

  if (!seen_linkorder)
    return TRUE;

  sections = (struct bfd_link_order **)
    xmalloc (seen_linkorder * sizeof (struct bfd_link_order *));
  seen_linkorder = 0;

  for (p = o->map_head.link_order; p != NULL; p = p->next)
    {
      sections[seen_linkorder++] = p;
    }
  /* Sort the input sections in the order of their linked section.  */
  qsort (sections, seen_linkorder, sizeof (struct bfd_link_order *),
	 compare_link_order);

  /* Change the offsets of the sections.  */
  offset = 0;
  for (n = 0; n < seen_linkorder; n++)
    {
      s = sections[n]->u.indirect.section;
      offset &= ~(bfd_vma)((1 << s->alignment_power) - 1);
      s->output_offset = offset;
      sections[n]->offset = offset;
      offset += sections[n]->size;
    }

  return TRUE;
}


/* Do the final step of an ELF link.  */

bfd_boolean
bfd_elf_final_link (bfd *abfd, struct bfd_link_info *info)
{
  bfd_boolean dynamic;
  bfd_boolean emit_relocs;
  bfd *dynobj;
  struct elf_final_link_info finfo;
  register asection *o;
  register struct bfd_link_order *p;
  register bfd *sub;
  bfd_size_type max_contents_size;
  bfd_size_type max_external_reloc_size;
  bfd_size_type max_internal_reloc_count;
  bfd_size_type max_sym_count;
  bfd_size_type max_sym_shndx_count;
  file_ptr off;
  Elf_Internal_Sym elfsym;
  unsigned int i;
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Shdr *symtab_shndx_hdr;
  Elf_Internal_Shdr *symstrtab_hdr;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  struct elf_outext_info eoinfo;
  bfd_boolean merged;
  size_t relativecount = 0;
  asection *reldyn = 0;
  bfd_size_type amt;
  asection *attr_section = NULL;
  bfd_vma attr_size = 0;
  const char *std_attrs_section;

  if (! is_elf_hash_table (info->hash))
    return FALSE;

  if (info->shared)
    abfd->flags |= DYNAMIC;

  dynamic = elf_hash_table (info)->dynamic_sections_created;
  dynobj = elf_hash_table (info)->dynobj;

  emit_relocs = (info->relocatable
		 || info->emitrelocations);

  finfo.info = info;
  finfo.output_bfd = abfd;
  finfo.symstrtab = _bfd_elf_stringtab_init ();
  if (finfo.symstrtab == NULL)
    return FALSE;

  if (! dynamic)
    {
      finfo.dynsym_sec = NULL;
      finfo.hash_sec = NULL;
      finfo.symver_sec = NULL;
    }
  else
    {
      finfo.dynsym_sec = bfd_get_section_by_name (dynobj, ".dynsym");
      finfo.hash_sec = bfd_get_section_by_name (dynobj, ".hash");
      BFD_ASSERT (finfo.dynsym_sec != NULL);
      finfo.symver_sec = bfd_get_section_by_name (dynobj, ".gnu.version");
      /* Note that it is OK if symver_sec is NULL.  */
    }

  finfo.contents = NULL;
  finfo.external_relocs = NULL;
  finfo.internal_relocs = NULL;
  finfo.external_syms = NULL;
  finfo.locsym_shndx = NULL;
  finfo.internal_syms = NULL;
  finfo.indices = NULL;
  finfo.sections = NULL;
  finfo.symbuf = NULL;
  finfo.symshndxbuf = NULL;
  finfo.symbuf_count = 0;
  finfo.shndxbuf_size = 0;

  /* The object attributes have been merged.  Remove the input
     sections from the link, and set the contents of the output
     secton.  */
  std_attrs_section = get_elf_backend_data (abfd)->obj_attrs_section;
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((std_attrs_section && strcmp (o->name, std_attrs_section) == 0)
	  || strcmp (o->name, ".gnu.attributes") == 0)
	{
	  for (p = o->map_head.link_order; p != NULL; p = p->next)
	    {
	      asection *input_section;

	      if (p->type != bfd_indirect_link_order)
		continue;
	      input_section = p->u.indirect.section;
	      /* Hack: reset the SEC_HAS_CONTENTS flag so that
		 elf_link_input_bfd ignores this section.  */
	      input_section->flags &= ~SEC_HAS_CONTENTS;
	    }
	    
	  attr_size = bfd_elf_obj_attr_size (abfd);
	  if (attr_size)
	    {
	      bfd_set_section_size (abfd, o, attr_size);
	      attr_section = o;
	      /* Skip this section later on.  */
	      o->map_head.link_order = NULL;
	    }
	  else
	    o->flags |= SEC_EXCLUDE;
	}
    }

  /* Count up the number of relocations we will output for each output
     section, so that we know the sizes of the reloc sections.  We
     also figure out some maximum sizes.  */
  max_contents_size = 0;
  max_external_reloc_size = 0;
  max_internal_reloc_count = 0;
  max_sym_count = 0;
  max_sym_shndx_count = 0;
  merged = FALSE;
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      struct bfd_elf_section_data *esdo = elf_section_data (o);
      o->reloc_count = 0;

      for (p = o->map_head.link_order; p != NULL; p = p->next)
	{
	  unsigned int reloc_count = 0;
	  struct bfd_elf_section_data *esdi = NULL;
	  unsigned int *rel_count1;

	  if (p->type == bfd_section_reloc_link_order
	      || p->type == bfd_symbol_reloc_link_order)
	    reloc_count = 1;
	  else if (p->type == bfd_indirect_link_order)
	    {
	      asection *sec;

	      sec = p->u.indirect.section;
	      esdi = elf_section_data (sec);

	      /* Mark all sections which are to be included in the
		 link.  This will normally be every section.  We need
		 to do this so that we can identify any sections which
		 the linker has decided to not include.  */
	      sec->linker_mark = TRUE;

	      if (sec->flags & SEC_MERGE)
		merged = TRUE;

	      if (info->relocatable || info->emitrelocations)
		reloc_count = sec->reloc_count;
	      else if (bed->elf_backend_count_relocs)
		{
		  Elf_Internal_Rela * relocs;

		  relocs = _bfd_elf_link_read_relocs (sec->owner, sec,
						      NULL, NULL,
						      info->keep_memory);

		  if (relocs != NULL)
		    {
		      reloc_count
			= (*bed->elf_backend_count_relocs) (sec, relocs);

		      if (elf_section_data (sec)->relocs != relocs)
			free (relocs);
		    }
		}

	      if (sec->rawsize > max_contents_size)
		max_contents_size = sec->rawsize;
	      if (sec->size > max_contents_size)
		max_contents_size = sec->size;

	      /* We are interested in just local symbols, not all
		 symbols.  */
	      if (bfd_get_flavour (sec->owner) == bfd_target_elf_flavour
		  && (sec->owner->flags & DYNAMIC) == 0)
		{
		  size_t sym_count;

		  if (elf_bad_symtab (sec->owner))
		    sym_count = (elf_tdata (sec->owner)->symtab_hdr.sh_size
				 / bed->s->sizeof_sym);
		  else
		    sym_count = elf_tdata (sec->owner)->symtab_hdr.sh_info;

		  if (sym_count > max_sym_count)
		    max_sym_count = sym_count;

		  if (sym_count > max_sym_shndx_count
		      && elf_symtab_shndx (sec->owner) != 0)
		    max_sym_shndx_count = sym_count;

		  if ((sec->flags & SEC_RELOC) != 0)
		    {
		      size_t ext_size;

		      ext_size = elf_section_data (sec)->rel_hdr.sh_size;
		      if (ext_size > max_external_reloc_size)
			max_external_reloc_size = ext_size;
		      if (sec->reloc_count > max_internal_reloc_count)
			max_internal_reloc_count = sec->reloc_count;
		    }
		}
	    }

	  if (reloc_count == 0)
	    continue;

	  o->reloc_count += reloc_count;

	  /* MIPS may have a mix of REL and RELA relocs on sections.
	     To support this curious ABI we keep reloc counts in
	     elf_section_data too.  We must be careful to add the
	     relocations from the input section to the right output
	     count.  FIXME: Get rid of one count.  We have
	     o->reloc_count == esdo->rel_count + esdo->rel_count2.  */
	  rel_count1 = &esdo->rel_count;
	  if (esdi != NULL)
	    {
	      bfd_boolean same_size;
	      bfd_size_type entsize1;

	      entsize1 = esdi->rel_hdr.sh_entsize;
	      BFD_ASSERT (entsize1 == bed->s->sizeof_rel
			  || entsize1 == bed->s->sizeof_rela);
	      same_size = !o->use_rela_p == (entsize1 == bed->s->sizeof_rel);

	      if (!same_size)
		rel_count1 = &esdo->rel_count2;

	      if (esdi->rel_hdr2 != NULL)
		{
		  bfd_size_type entsize2 = esdi->rel_hdr2->sh_entsize;
		  unsigned int alt_count;
		  unsigned int *rel_count2;

		  BFD_ASSERT (entsize2 != entsize1
			      && (entsize2 == bed->s->sizeof_rel
				  || entsize2 == bed->s->sizeof_rela));

		  rel_count2 = &esdo->rel_count2;
		  if (!same_size)
		    rel_count2 = &esdo->rel_count;

		  /* The following is probably too simplistic if the
		     backend counts output relocs unusually.  */
		  BFD_ASSERT (bed->elf_backend_count_relocs == NULL);
		  alt_count = NUM_SHDR_ENTRIES (esdi->rel_hdr2);
		  *rel_count2 += alt_count;
		  reloc_count -= alt_count;
		}
	    }
	  *rel_count1 += reloc_count;
	}

      if (o->reloc_count > 0)
	o->flags |= SEC_RELOC;
      else
	{
	  /* Explicitly clear the SEC_RELOC flag.  The linker tends to
	     set it (this is probably a bug) and if it is set
	     assign_section_numbers will create a reloc section.  */
	  o->flags &=~ SEC_RELOC;
	}

      /* If the SEC_ALLOC flag is not set, force the section VMA to
	 zero.  This is done in elf_fake_sections as well, but forcing
	 the VMA to 0 here will ensure that relocs against these
	 sections are handled correctly.  */
      if ((o->flags & SEC_ALLOC) == 0
	  && ! o->user_set_vma)
	o->vma = 0;
    }

  if (! info->relocatable && merged)
    elf_link_hash_traverse (elf_hash_table (info),
			    _bfd_elf_link_sec_merge_syms, abfd);

  /* Figure out the file positions for everything but the symbol table
     and the relocs.  We set symcount to force assign_section_numbers
     to create a symbol table.  */
  bfd_get_symcount (abfd) = info->strip == strip_all ? 0 : 1;
  BFD_ASSERT (! abfd->output_has_begun);
  if (! _bfd_elf_compute_section_file_positions (abfd, info))
    goto error_return;

  /* Set sizes, and assign file positions for reloc sections.  */
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) != 0)
	{
	  if (!(_bfd_elf_link_size_reloc_section
		(abfd, &elf_section_data (o)->rel_hdr, o)))
	    goto error_return;

	  if (elf_section_data (o)->rel_hdr2
	      && !(_bfd_elf_link_size_reloc_section
		   (abfd, elf_section_data (o)->rel_hdr2, o)))
	    goto error_return;
	}

      /* Now, reset REL_COUNT and REL_COUNT2 so that we can use them
	 to count upwards while actually outputting the relocations.  */
      elf_section_data (o)->rel_count = 0;
      elf_section_data (o)->rel_count2 = 0;
    }

  _bfd_elf_assign_file_positions_for_relocs (abfd);

  /* We have now assigned file positions for all the sections except
     .symtab and .strtab.  We start the .symtab section at the current
     file position, and write directly to it.  We build the .strtab
     section in memory.  */
  bfd_get_symcount (abfd) = 0;
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  /* sh_name is set in prep_headers.  */
  symtab_hdr->sh_type = SHT_SYMTAB;
  /* sh_flags, sh_addr and sh_size all start off zero.  */
  symtab_hdr->sh_entsize = bed->s->sizeof_sym;
  /* sh_link is set in assign_section_numbers.  */
  /* sh_info is set below.  */
  /* sh_offset is set just below.  */
  symtab_hdr->sh_addralign = 1 << bed->s->log_file_align;

  off = elf_tdata (abfd)->next_file_pos;
  off = _bfd_elf_assign_file_position_for_section (symtab_hdr, off, TRUE);

  /* Note that at this point elf_tdata (abfd)->next_file_pos is
     incorrect.  We do not yet know the size of the .symtab section.
     We correct next_file_pos below, after we do know the size.  */

  /* Allocate a buffer to hold swapped out symbols.  This is to avoid
     continuously seeking to the right position in the file.  */
  if (! info->keep_memory || max_sym_count < 20)
    finfo.symbuf_size = 20;
  else
    finfo.symbuf_size = max_sym_count;
  amt = finfo.symbuf_size;
  amt *= bed->s->sizeof_sym;
  finfo.symbuf = bfd_malloc (amt);
  if (finfo.symbuf == NULL)
    goto error_return;
  if (elf_numsections (abfd) > SHN_LORESERVE)
    {
      /* Wild guess at number of output symbols.  realloc'd as needed.  */
      amt = 2 * max_sym_count + elf_numsections (abfd) + 1000;
      finfo.shndxbuf_size = amt;
      amt *= sizeof (Elf_External_Sym_Shndx);
      finfo.symshndxbuf = bfd_zmalloc (amt);
      if (finfo.symshndxbuf == NULL)
	goto error_return;
    }

  /* Start writing out the symbol table.  The first symbol is always a
     dummy symbol.  */
  if (info->strip != strip_all
      || emit_relocs)
    {
      elfsym.st_value = 0;
      elfsym.st_size = 0;
      elfsym.st_info = 0;
      elfsym.st_other = 0;
      elfsym.st_shndx = SHN_UNDEF;
      if (! elf_link_output_sym (&finfo, NULL, &elfsym, bfd_und_section_ptr,
				 NULL))
	goto error_return;
    }

  /* Output a symbol for each section.  We output these even if we are
     discarding local symbols, since they are used for relocs.  These
     symbols have no names.  We store the index of each one in the
     index field of the section, so that we can find it again when
     outputting relocs.  */
  if (info->strip != strip_all
      || emit_relocs)
    {
      elfsym.st_size = 0;
      elfsym.st_info = ELF_ST_INFO (STB_LOCAL, STT_SECTION);
      elfsym.st_other = 0;
      elfsym.st_value = 0;
      for (i = 1; i < elf_numsections (abfd); i++)
	{
	  o = bfd_section_from_elf_index (abfd, i);
	  if (o != NULL)
	    {
	      o->target_index = bfd_get_symcount (abfd);
	      elfsym.st_shndx = i;
	      if (!info->relocatable)
		elfsym.st_value = o->vma;
	      if (!elf_link_output_sym (&finfo, NULL, &elfsym, o, NULL))
		goto error_return;
	    }
	  if (i == SHN_LORESERVE - 1)
	    i += SHN_HIRESERVE + 1 - SHN_LORESERVE;
	}
    }

  /* Allocate some memory to hold information read in from the input
     files.  */
  if (max_contents_size != 0)
    {
      finfo.contents = bfd_malloc (max_contents_size);
      if (finfo.contents == NULL)
	goto error_return;
    }

  if (max_external_reloc_size != 0)
    {
      finfo.external_relocs = bfd_malloc (max_external_reloc_size);
      if (finfo.external_relocs == NULL)
	goto error_return;
    }

  if (max_internal_reloc_count != 0)
    {
      amt = max_internal_reloc_count * bed->s->int_rels_per_ext_rel;
      amt *= sizeof (Elf_Internal_Rela);
      finfo.internal_relocs = bfd_malloc (amt);
      if (finfo.internal_relocs == NULL)
	goto error_return;
    }

  if (max_sym_count != 0)
    {
      amt = max_sym_count * bed->s->sizeof_sym;
      finfo.external_syms = bfd_malloc (amt);
      if (finfo.external_syms == NULL)
	goto error_return;

      amt = max_sym_count * sizeof (Elf_Internal_Sym);
      finfo.internal_syms = bfd_malloc (amt);
      if (finfo.internal_syms == NULL)
	goto error_return;

      amt = max_sym_count * sizeof (long);
      finfo.indices = bfd_malloc (amt);
      if (finfo.indices == NULL)
	goto error_return;

      amt = max_sym_count * sizeof (asection *);
      finfo.sections = bfd_malloc (amt);
      if (finfo.sections == NULL)
	goto error_return;
    }

  if (max_sym_shndx_count != 0)
    {
      amt = max_sym_shndx_count * sizeof (Elf_External_Sym_Shndx);
      finfo.locsym_shndx = bfd_malloc (amt);
      if (finfo.locsym_shndx == NULL)
	goto error_return;
    }

  if (elf_hash_table (info)->tls_sec)
    {
      bfd_vma base, end = 0;
      asection *sec;

      for (sec = elf_hash_table (info)->tls_sec;
	   sec && (sec->flags & SEC_THREAD_LOCAL);
	   sec = sec->next)
	{
	  bfd_size_type size = sec->size;

	  if (size == 0
	      && (sec->flags & SEC_HAS_CONTENTS) == 0)
	    {
	      struct bfd_link_order *o = sec->map_tail.link_order;
	      if (o != NULL)
		size = o->offset + o->size;
	    }
	  end = sec->vma + size;
	}
      base = elf_hash_table (info)->tls_sec->vma;
      end = align_power (end, elf_hash_table (info)->tls_sec->alignment_power);
      elf_hash_table (info)->tls_size = end - base;
    }

  /* Reorder SHF_LINK_ORDER sections.  */
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if (!elf_fixup_link_order (abfd, o))
	return FALSE;
    }

  /* Since ELF permits relocations to be against local symbols, we
     must have the local symbols available when we do the relocations.
     Since we would rather only read the local symbols once, and we
     would rather not keep them in memory, we handle all the
     relocations for a single input file at the same time.

     Unfortunately, there is no way to know the total number of local
     symbols until we have seen all of them, and the local symbol
     indices precede the global symbol indices.  This means that when
     we are generating relocatable output, and we see a reloc against
     a global symbol, we can not know the symbol index until we have
     finished examining all the local symbols to see which ones we are
     going to output.  To deal with this, we keep the relocations in
     memory, and don't output them until the end of the link.  This is
     an unfortunate waste of memory, but I don't see a good way around
     it.  Fortunately, it only happens when performing a relocatable
     link, which is not the common case.  FIXME: If keep_memory is set
     we could write the relocs out and then read them again; I don't
     know how bad the memory loss will be.  */

  for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
    sub->output_has_begun = FALSE;
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      for (p = o->map_head.link_order; p != NULL; p = p->next)
	{
	  if (p->type == bfd_indirect_link_order
	      && (bfd_get_flavour ((sub = p->u.indirect.section->owner))
		  == bfd_target_elf_flavour)
	      && elf_elfheader (sub)->e_ident[EI_CLASS] == bed->s->elfclass)
	    {
	      if (! sub->output_has_begun)
		{
		  if (! elf_link_input_bfd (&finfo, sub))
		    goto error_return;
		  sub->output_has_begun = TRUE;
		}
	    }
	  else if (p->type == bfd_section_reloc_link_order
		   || p->type == bfd_symbol_reloc_link_order)
	    {
	      if (! elf_reloc_link_order (abfd, info, o, p))
		goto error_return;
	    }
	  else
	    {
	      if (! _bfd_default_link_order (abfd, info, o, p))
		goto error_return;
	    }
	}
    }

  /* Free symbol buffer if needed.  */
  if (!info->reduce_memory_overheads)
    {
      for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
	if (bfd_get_flavour (sub) == bfd_target_elf_flavour
	    && elf_tdata (sub)->symbuf)
	  {
	    free (elf_tdata (sub)->symbuf);
	    elf_tdata (sub)->symbuf = NULL;
	  }
    }

  /* Output any global symbols that got converted to local in a
     version script or due to symbol visibility.  We do this in a
     separate step since ELF requires all local symbols to appear
     prior to any global symbols.  FIXME: We should only do this if
     some global symbols were, in fact, converted to become local.
     FIXME: Will this work correctly with the Irix 5 linker?  */
  eoinfo.failed = FALSE;
  eoinfo.finfo = &finfo;
  eoinfo.localsyms = TRUE;
  elf_link_hash_traverse (elf_hash_table (info), elf_link_output_extsym,
			  &eoinfo);
  if (eoinfo.failed)
    return FALSE;

  /* If backend needs to output some local symbols not present in the hash
     table, do it now.  */
  if (bed->elf_backend_output_arch_local_syms)
    {
      typedef bfd_boolean (*out_sym_func)
	(void *, const char *, Elf_Internal_Sym *, asection *,
	 struct elf_link_hash_entry *);

      if (! ((*bed->elf_backend_output_arch_local_syms)
	     (abfd, info, &finfo, (out_sym_func) elf_link_output_sym)))
	return FALSE;
    }

  /* That wrote out all the local symbols.  Finish up the symbol table
     with the global symbols. Even if we want to strip everything we
     can, we still need to deal with those global symbols that got
     converted to local in a version script.  */

  /* The sh_info field records the index of the first non local symbol.  */
  symtab_hdr->sh_info = bfd_get_symcount (abfd);

  if (dynamic
      && finfo.dynsym_sec->output_section != bfd_abs_section_ptr)
    {
      Elf_Internal_Sym sym;
      bfd_byte *dynsym = finfo.dynsym_sec->contents;
      long last_local = 0;

      /* Write out the section symbols for the output sections.  */
      if (info->shared || elf_hash_table (info)->is_relocatable_executable)
	{
	  asection *s;

	  sym.st_size = 0;
	  sym.st_name = 0;
	  sym.st_info = ELF_ST_INFO (STB_LOCAL, STT_SECTION);
	  sym.st_other = 0;

	  for (s = abfd->sections; s != NULL; s = s->next)
	    {
	      int indx;
	      bfd_byte *dest;
	      long dynindx;

	      dynindx = elf_section_data (s)->dynindx;
	      if (dynindx <= 0)
		continue;
	      indx = elf_section_data (s)->this_idx;
	      BFD_ASSERT (indx > 0);
	      sym.st_shndx = indx;
	      if (! check_dynsym (abfd, &sym))
		return FALSE;
	      sym.st_value = s->vma;
	      dest = dynsym + dynindx * bed->s->sizeof_sym;
	      if (last_local < dynindx)
		last_local = dynindx;
	      bed->s->swap_symbol_out (abfd, &sym, dest, 0);
	    }
	}

      /* Write out the local dynsyms.  */
      if (elf_hash_table (info)->dynlocal)
	{
	  struct elf_link_local_dynamic_entry *e;
	  for (e = elf_hash_table (info)->dynlocal; e ; e = e->next)
	    {
	      asection *s;
	      bfd_byte *dest;

	      sym.st_size = e->isym.st_size;
	      sym.st_other = e->isym.st_other;

	      /* Copy the internal symbol as is.
		 Note that we saved a word of storage and overwrote
		 the original st_name with the dynstr_index.  */
	      sym = e->isym;

	      if (e->isym.st_shndx != SHN_UNDEF
		  && (e->isym.st_shndx < SHN_LORESERVE
		      || e->isym.st_shndx > SHN_HIRESERVE))
		{
		  s = bfd_section_from_elf_index (e->input_bfd,
						  e->isym.st_shndx);

		  sym.st_shndx =
		    elf_section_data (s->output_section)->this_idx;
		  if (! check_dynsym (abfd, &sym))
		    return FALSE;
		  sym.st_value = (s->output_section->vma
				  + s->output_offset
				  + e->isym.st_value);
		}

	      if (last_local < e->dynindx)
		last_local = e->dynindx;

	      dest = dynsym + e->dynindx * bed->s->sizeof_sym;
	      bed->s->swap_symbol_out (abfd, &sym, dest, 0);
	    }
	}

      elf_section_data (finfo.dynsym_sec->output_section)->this_hdr.sh_info =
	last_local + 1;
    }

  /* We get the global symbols from the hash table.  */
  eoinfo.failed = FALSE;
  eoinfo.localsyms = FALSE;
  eoinfo.finfo = &finfo;
  elf_link_hash_traverse (elf_hash_table (info), elf_link_output_extsym,
			  &eoinfo);
  if (eoinfo.failed)
    return FALSE;

  /* If backend needs to output some symbols not present in the hash
     table, do it now.  */
  if (bed->elf_backend_output_arch_syms)
    {
      typedef bfd_boolean (*out_sym_func)
	(void *, const char *, Elf_Internal_Sym *, asection *,
	 struct elf_link_hash_entry *);

      if (! ((*bed->elf_backend_output_arch_syms)
	     (abfd, info, &finfo, (out_sym_func) elf_link_output_sym)))
	return FALSE;
    }

  /* Flush all symbols to the file.  */
  if (! elf_link_flush_output_syms (&finfo, bed))
    return FALSE;

  /* Now we know the size of the symtab section.  */
  off += symtab_hdr->sh_size;

  symtab_shndx_hdr = &elf_tdata (abfd)->symtab_shndx_hdr;
  if (symtab_shndx_hdr->sh_name != 0)
    {
      symtab_shndx_hdr->sh_type = SHT_SYMTAB_SHNDX;
      symtab_shndx_hdr->sh_entsize = sizeof (Elf_External_Sym_Shndx);
      symtab_shndx_hdr->sh_addralign = sizeof (Elf_External_Sym_Shndx);
      amt = bfd_get_symcount (abfd) * sizeof (Elf_External_Sym_Shndx);
      symtab_shndx_hdr->sh_size = amt;

      off = _bfd_elf_assign_file_position_for_section (symtab_shndx_hdr,
						       off, TRUE);

      if (bfd_seek (abfd, symtab_shndx_hdr->sh_offset, SEEK_SET) != 0
	  || (bfd_bwrite (finfo.symshndxbuf, amt, abfd) != amt))
	return FALSE;
    }


  /* Finish up and write out the symbol string table (.strtab)
     section.  */
  symstrtab_hdr = &elf_tdata (abfd)->strtab_hdr;
  /* sh_name was set in prep_headers.  */
  symstrtab_hdr->sh_type = SHT_STRTAB;
  symstrtab_hdr->sh_flags = 0;
  symstrtab_hdr->sh_addr = 0;
  symstrtab_hdr->sh_size = _bfd_stringtab_size (finfo.symstrtab);
  symstrtab_hdr->sh_entsize = 0;
  symstrtab_hdr->sh_link = 0;
  symstrtab_hdr->sh_info = 0;
  /* sh_offset is set just below.  */
  symstrtab_hdr->sh_addralign = 1;

  off = _bfd_elf_assign_file_position_for_section (symstrtab_hdr, off, TRUE);
  elf_tdata (abfd)->next_file_pos = off;

  if (bfd_get_symcount (abfd) > 0)
    {
      if (bfd_seek (abfd, symstrtab_hdr->sh_offset, SEEK_SET) != 0
	  || ! _bfd_stringtab_emit (abfd, finfo.symstrtab))
	return FALSE;
    }

  /* Adjust the relocs to have the correct symbol indices.  */
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) == 0)
	continue;

      elf_link_adjust_relocs (abfd, &elf_section_data (o)->rel_hdr,
			      elf_section_data (o)->rel_count,
			      elf_section_data (o)->rel_hashes);
      if (elf_section_data (o)->rel_hdr2 != NULL)
	elf_link_adjust_relocs (abfd, elf_section_data (o)->rel_hdr2,
				elf_section_data (o)->rel_count2,
				(elf_section_data (o)->rel_hashes
				 + elf_section_data (o)->rel_count));

      /* Set the reloc_count field to 0 to prevent write_relocs from
	 trying to swap the relocs out itself.  */
      o->reloc_count = 0;
    }

  if (dynamic && info->combreloc && dynobj != NULL)
    relativecount = elf_link_sort_relocs (abfd, info, &reldyn);

  /* If we are linking against a dynamic object, or generating a
     shared library, finish up the dynamic linking information.  */
  if (dynamic)
    {
      bfd_byte *dyncon, *dynconend;

      /* Fix up .dynamic entries.  */
      o = bfd_get_section_by_name (dynobj, ".dynamic");
      BFD_ASSERT (o != NULL);

      dyncon = o->contents;
      dynconend = o->contents + o->size;
      for (; dyncon < dynconend; dyncon += bed->s->sizeof_dyn)
	{
	  Elf_Internal_Dyn dyn;
	  const char *name;
	  unsigned int type;

	  bed->s->swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    default:
	      continue;
	    case DT_NULL:
	      if (relativecount > 0 && dyncon + bed->s->sizeof_dyn < dynconend)
		{
		  switch (elf_section_data (reldyn)->this_hdr.sh_type)
		    {
		    case SHT_REL: dyn.d_tag = DT_RELCOUNT; break;
		    case SHT_RELA: dyn.d_tag = DT_RELACOUNT; break;
		    default: continue;
		    }
		  dyn.d_un.d_val = relativecount;
		  relativecount = 0;
		  break;
		}
	      continue;

	    case DT_INIT:
	      name = info->init_function;
	      goto get_sym;
	    case DT_FINI:
	      name = info->fini_function;
	    get_sym:
	      {
		struct elf_link_hash_entry *h;

		h = elf_link_hash_lookup (elf_hash_table (info), name,
					  FALSE, FALSE, TRUE);
		if (h != NULL
		    && (h->root.type == bfd_link_hash_defined
			|| h->root.type == bfd_link_hash_defweak))
		  {
		    dyn.d_un.d_val = h->root.u.def.value;
		    o = h->root.u.def.section;
		    if (o->output_section != NULL)
		      dyn.d_un.d_val += (o->output_section->vma
					 + o->output_offset);
		    else
		      {
			/* The symbol is imported from another shared
			   library and does not apply to this one.  */
			dyn.d_un.d_val = 0;
		      }
		    break;
		  }
	      }
	      continue;

	    case DT_PREINIT_ARRAYSZ:
	      name = ".preinit_array";
	      goto get_size;
	    case DT_INIT_ARRAYSZ:
	      name = ".init_array";
	      goto get_size;
	    case DT_FINI_ARRAYSZ:
	      name = ".fini_array";
	    get_size:
	      o = bfd_get_section_by_name (abfd, name);
	      if (o == NULL)
		{
		  (*_bfd_error_handler)
		    (_("%B: could not find output section %s"), abfd, name);
		  goto error_return;
		}
	      if (o->size == 0)
		(*_bfd_error_handler)
		  (_("warning: %s section has zero size"), name);
	      dyn.d_un.d_val = o->size;
	      break;

	    case DT_PREINIT_ARRAY:
	      name = ".preinit_array";
	      goto get_vma;
	    case DT_INIT_ARRAY:
	      name = ".init_array";
	      goto get_vma;
	    case DT_FINI_ARRAY:
	      name = ".fini_array";
	      goto get_vma;

	    case DT_HASH:
	      name = ".hash";
	      goto get_vma;
	    case DT_GNU_HASH:
	      name = ".gnu.hash";
	      goto get_vma;
	    case DT_STRTAB:
	      name = ".dynstr";
	      goto get_vma;
	    case DT_SYMTAB:
	      name = ".dynsym";
	      goto get_vma;
	    case DT_VERDEF:
	      name = ".gnu.version_d";
	      goto get_vma;
	    case DT_VERNEED:
	      name = ".gnu.version_r";
	      goto get_vma;
	    case DT_VERSYM:
	      name = ".gnu.version";
	    get_vma:
	      o = bfd_get_section_by_name (abfd, name);
	      if (o == NULL)
		{
		  (*_bfd_error_handler)
		    (_("%B: could not find output section %s"), abfd, name);
		  goto error_return;
		}
	      dyn.d_un.d_ptr = o->vma;
	      break;

	    case DT_REL:
	    case DT_RELA:
	    case DT_RELSZ:
	    case DT_RELASZ:
	      if (dyn.d_tag == DT_REL || dyn.d_tag == DT_RELSZ)
		type = SHT_REL;
	      else
		type = SHT_RELA;
	      dyn.d_un.d_val = 0;
	      for (i = 1; i < elf_numsections (abfd); i++)
		{
		  Elf_Internal_Shdr *hdr;

		  hdr = elf_elfsections (abfd)[i];
		  if (hdr->sh_type == type
		      && (hdr->sh_flags & SHF_ALLOC) != 0)
		    {
		      if (dyn.d_tag == DT_RELSZ || dyn.d_tag == DT_RELASZ)
			dyn.d_un.d_val += hdr->sh_size;
		      else
			{
			  if (dyn.d_un.d_val == 0
			      || hdr->sh_addr < dyn.d_un.d_val)
			    dyn.d_un.d_val = hdr->sh_addr;
			}
		    }
		}
	      break;
	    }
	  bed->s->swap_dyn_out (dynobj, &dyn, dyncon);
	}
    }

  /* If we have created any dynamic sections, then output them.  */
  if (dynobj != NULL)
    {
      if (! (*bed->elf_backend_finish_dynamic_sections) (abfd, info))
	goto error_return;

      /* Check for DT_TEXTREL (late, in case the backend removes it).  */
      if (info->warn_shared_textrel && info->shared)
	{
	  bfd_byte *dyncon, *dynconend;

	  /* Fix up .dynamic entries.  */
	  o = bfd_get_section_by_name (dynobj, ".dynamic");
	  BFD_ASSERT (o != NULL);

	  dyncon = o->contents;
	  dynconend = o->contents + o->size;
	  for (; dyncon < dynconend; dyncon += bed->s->sizeof_dyn)
	    {
	      Elf_Internal_Dyn dyn;

	      bed->s->swap_dyn_in (dynobj, dyncon, &dyn);

	      if (dyn.d_tag == DT_TEXTREL)
		{
		 info->callbacks->einfo 
		    (_("%P: warning: creating a DT_TEXTREL in a shared object.\n"));
		  break;
		}
	    }
	}

      for (o = dynobj->sections; o != NULL; o = o->next)
	{
	  if ((o->flags & SEC_HAS_CONTENTS) == 0
	      || o->size == 0
	      || o->output_section == bfd_abs_section_ptr)
	    continue;
	  if ((o->flags & SEC_LINKER_CREATED) == 0)
	    {
	      /* At this point, we are only interested in sections
		 created by _bfd_elf_link_create_dynamic_sections.  */
	      continue;
	    }
	  if (elf_hash_table (info)->stab_info.stabstr == o)
	    continue;
	  if (elf_hash_table (info)->eh_info.hdr_sec == o)
	    continue;
	  if ((elf_section_data (o->output_section)->this_hdr.sh_type
	       != SHT_STRTAB)
	      || strcmp (bfd_get_section_name (abfd, o), ".dynstr") != 0)
	    {
	      if (! bfd_set_section_contents (abfd, o->output_section,
					      o->contents,
					      (file_ptr) o->output_offset,
					      o->size))
		goto error_return;
	    }
	  else
	    {
	      /* The contents of the .dynstr section are actually in a
		 stringtab.  */
	      off = elf_section_data (o->output_section)->this_hdr.sh_offset;
	      if (bfd_seek (abfd, off, SEEK_SET) != 0
		  || ! _bfd_elf_strtab_emit (abfd,
					     elf_hash_table (info)->dynstr))
		goto error_return;
	    }
	}
    }

  if (info->relocatable)
    {
      bfd_boolean failed = FALSE;

      bfd_map_over_sections (abfd, bfd_elf_set_group_contents, &failed);
      if (failed)
	goto error_return;
    }

  /* If we have optimized stabs strings, output them.  */
  if (elf_hash_table (info)->stab_info.stabstr != NULL)
    {
      if (! _bfd_write_stab_strings (abfd, &elf_hash_table (info)->stab_info))
	goto error_return;
    }

  if (info->eh_frame_hdr)
    {
      if (! _bfd_elf_write_section_eh_frame_hdr (abfd, info))
	goto error_return;
    }

  if (finfo.symstrtab != NULL)
    _bfd_stringtab_free (finfo.symstrtab);
  if (finfo.contents != NULL)
    free (finfo.contents);
  if (finfo.external_relocs != NULL)
    free (finfo.external_relocs);
  if (finfo.internal_relocs != NULL)
    free (finfo.internal_relocs);
  if (finfo.external_syms != NULL)
    free (finfo.external_syms);
  if (finfo.locsym_shndx != NULL)
    free (finfo.locsym_shndx);
  if (finfo.internal_syms != NULL)
    free (finfo.internal_syms);
  if (finfo.indices != NULL)
    free (finfo.indices);
  if (finfo.sections != NULL)
    free (finfo.sections);
  if (finfo.symbuf != NULL)
    free (finfo.symbuf);
  if (finfo.symshndxbuf != NULL)
    free (finfo.symshndxbuf);
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) != 0
	  && elf_section_data (o)->rel_hashes != NULL)
	free (elf_section_data (o)->rel_hashes);
    }

  elf_tdata (abfd)->linker = TRUE;

  if (attr_section)
    {
      bfd_byte *contents = bfd_malloc (attr_size);
      if (contents == NULL)
	goto error_return;
      bfd_elf_set_obj_attr_contents (abfd, contents, attr_size);
      bfd_set_section_contents (abfd, attr_section, contents, 0, attr_size);
      free (contents);
    }

  return TRUE;

 error_return:
  if (finfo.symstrtab != NULL)
    _bfd_stringtab_free (finfo.symstrtab);
  if (finfo.contents != NULL)
    free (finfo.contents);
  if (finfo.external_relocs != NULL)
    free (finfo.external_relocs);
  if (finfo.internal_relocs != NULL)
    free (finfo.internal_relocs);
  if (finfo.external_syms != NULL)
    free (finfo.external_syms);
  if (finfo.locsym_shndx != NULL)
    free (finfo.locsym_shndx);
  if (finfo.internal_syms != NULL)
    free (finfo.internal_syms);
  if (finfo.indices != NULL)
    free (finfo.indices);
  if (finfo.sections != NULL)
    free (finfo.sections);
  if (finfo.symbuf != NULL)
    free (finfo.symbuf);
  if (finfo.symshndxbuf != NULL)
    free (finfo.symshndxbuf);
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) != 0
	  && elf_section_data (o)->rel_hashes != NULL)
	free (elf_section_data (o)->rel_hashes);
    }

  return FALSE;
}

/* Garbage collect unused sections.  */

/* Default gc_mark_hook.  */

asection *
_bfd_elf_gc_mark_hook (asection *sec,
		       struct bfd_link_info *info ATTRIBUTE_UNUSED,
		       Elf_Internal_Rela *rel ATTRIBUTE_UNUSED,
		       struct elf_link_hash_entry *h,
		       Elf_Internal_Sym *sym)
{
  if (h != NULL)
    {
      switch (h->root.type)
	{
	case bfd_link_hash_defined:
	case bfd_link_hash_defweak:
	  return h->root.u.def.section;

	case bfd_link_hash_common:
	  return h->root.u.c.p->section;

	default:
	  break;
	}
    }
  else
    return bfd_section_from_elf_index (sec->owner, sym->st_shndx);

  return NULL;
}

/* The mark phase of garbage collection.  For a given section, mark
   it and any sections in this section's group, and all the sections
   which define symbols to which it refers.  */

bfd_boolean
_bfd_elf_gc_mark (struct bfd_link_info *info,
		  asection *sec,
		  elf_gc_mark_hook_fn gc_mark_hook)
{
  bfd_boolean ret;
  bfd_boolean is_eh;
  asection *group_sec;

  sec->gc_mark = 1;

  /* Mark all the sections in the group.  */
  group_sec = elf_section_data (sec)->next_in_group;
  if (group_sec && !group_sec->gc_mark)
    if (!_bfd_elf_gc_mark (info, group_sec, gc_mark_hook))
      return FALSE;

  /* Look through the section relocs.  */
  ret = TRUE;
  is_eh = strcmp (sec->name, ".eh_frame") == 0;
  if ((sec->flags & SEC_RELOC) != 0 && sec->reloc_count > 0)
    {
      Elf_Internal_Rela *relstart, *rel, *relend;
      Elf_Internal_Shdr *symtab_hdr;
      struct elf_link_hash_entry **sym_hashes;
      size_t nlocsyms;
      size_t extsymoff;
      bfd *input_bfd = sec->owner;
      const struct elf_backend_data *bed = get_elf_backend_data (input_bfd);
      Elf_Internal_Sym *isym = NULL;
      int r_sym_shift;

      symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
      sym_hashes = elf_sym_hashes (input_bfd);

      /* Read the local symbols.  */
      if (elf_bad_symtab (input_bfd))
	{
	  nlocsyms = symtab_hdr->sh_size / bed->s->sizeof_sym;
	  extsymoff = 0;
	}
      else
	extsymoff = nlocsyms = symtab_hdr->sh_info;

      isym = (Elf_Internal_Sym *) symtab_hdr->contents;
      if (isym == NULL && nlocsyms != 0)
	{
	  isym = bfd_elf_get_elf_syms (input_bfd, symtab_hdr, nlocsyms, 0,
				       NULL, NULL, NULL);
	  if (isym == NULL)
	    return FALSE;
	}

      /* Read the relocations.  */
      relstart = _bfd_elf_link_read_relocs (input_bfd, sec, NULL, NULL,
					    info->keep_memory);
      if (relstart == NULL)
	{
	  ret = FALSE;
	  goto out1;
	}
      relend = relstart + sec->reloc_count * bed->s->int_rels_per_ext_rel;

      if (bed->s->arch_size == 32)
	r_sym_shift = 8;
      else
	r_sym_shift = 32;

      for (rel = relstart; rel < relend; rel++)
	{
	  unsigned long r_symndx;
	  asection *rsec;
	  struct elf_link_hash_entry *h;

	  r_symndx = rel->r_info >> r_sym_shift;
	  if (r_symndx == 0)
	    continue;

	  if (r_symndx >= nlocsyms
	      || ELF_ST_BIND (isym[r_symndx].st_info) != STB_LOCAL)
	    {
	      h = sym_hashes[r_symndx - extsymoff];
	      while (h->root.type == bfd_link_hash_indirect
		     || h->root.type == bfd_link_hash_warning)
		h = (struct elf_link_hash_entry *) h->root.u.i.link;
	      rsec = (*gc_mark_hook) (sec, info, rel, h, NULL);
	    }
	  else
	    {
	      rsec = (*gc_mark_hook) (sec, info, rel, NULL, &isym[r_symndx]);
	    }

	  if (rsec && !rsec->gc_mark)
	    {
	      if (bfd_get_flavour (rsec->owner) != bfd_target_elf_flavour)
		rsec->gc_mark = 1;
	      else if (is_eh)
		rsec->gc_mark_from_eh = 1;
	      else if (!_bfd_elf_gc_mark (info, rsec, gc_mark_hook))
		{
		  ret = FALSE;
		  goto out2;
		}
	    }
	}

    out2:
      if (elf_section_data (sec)->relocs != relstart)
	free (relstart);
    out1:
      if (isym != NULL && symtab_hdr->contents != (unsigned char *) isym)
	{
	  if (! info->keep_memory)
	    free (isym);
	  else
	    symtab_hdr->contents = (unsigned char *) isym;
	}
    }

  return ret;
}

/* Sweep symbols in swept sections.  Called via elf_link_hash_traverse.  */

struct elf_gc_sweep_symbol_info
{
  struct bfd_link_info *info;
  void (*hide_symbol) (struct bfd_link_info *, struct elf_link_hash_entry *,
		       bfd_boolean);
};

static bfd_boolean
elf_gc_sweep_symbol (struct elf_link_hash_entry *h, void *data)
{
  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if ((h->root.type == bfd_link_hash_defined
       || h->root.type == bfd_link_hash_defweak)
      && !h->root.u.def.section->gc_mark
      && !(h->root.u.def.section->owner->flags & DYNAMIC))
    {
      struct elf_gc_sweep_symbol_info *inf = data;
      (*inf->hide_symbol) (inf->info, h, TRUE);
    }

  return TRUE;
}

/* The sweep phase of garbage collection.  Remove all garbage sections.  */

typedef bfd_boolean (*gc_sweep_hook_fn)
  (bfd *, struct bfd_link_info *, asection *, const Elf_Internal_Rela *);

static bfd_boolean
elf_gc_sweep (bfd *abfd, struct bfd_link_info *info)
{
  bfd *sub;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  gc_sweep_hook_fn gc_sweep_hook = bed->gc_sweep_hook;
  unsigned long section_sym_count;
  struct elf_gc_sweep_symbol_info sweep_info;

  for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
    {
      asection *o;

      if (bfd_get_flavour (sub) != bfd_target_elf_flavour)
	continue;

      for (o = sub->sections; o != NULL; o = o->next)
	{
	  /* Keep debug and special sections.  */
	  if ((o->flags & (SEC_DEBUGGING | SEC_LINKER_CREATED)) != 0
	      || elf_section_data (o)->this_hdr.sh_type == SHT_NOTE
	      || (o->flags & (SEC_ALLOC | SEC_LOAD | SEC_RELOC)) == 0)
	    o->gc_mark = 1;

	  if (o->gc_mark)
	    continue;

	  /* Skip sweeping sections already excluded.  */
	  if (o->flags & SEC_EXCLUDE)
	    continue;

	  /* Since this is early in the link process, it is simple
	     to remove a section from the output.  */
	  o->flags |= SEC_EXCLUDE;

	  if (info->print_gc_sections && o->size != 0)
	    _bfd_error_handler (_("Removing unused section '%s' in file '%B'"), sub, o->name);

	  /* But we also have to update some of the relocation
	     info we collected before.  */
	  if (gc_sweep_hook
	      && (o->flags & SEC_RELOC) != 0
	      && o->reloc_count > 0
	      && !bfd_is_abs_section (o->output_section))
	    {
	      Elf_Internal_Rela *internal_relocs;
	      bfd_boolean r;

	      internal_relocs
		= _bfd_elf_link_read_relocs (o->owner, o, NULL, NULL,
					     info->keep_memory);
	      if (internal_relocs == NULL)
		return FALSE;

	      r = (*gc_sweep_hook) (o->owner, info, o, internal_relocs);

	      if (elf_section_data (o)->relocs != internal_relocs)
		free (internal_relocs);

	      if (!r)
		return FALSE;
	    }
	}
    }

  /* Remove the symbols that were in the swept sections from the dynamic
     symbol table.  GCFIXME: Anyone know how to get them out of the
     static symbol table as well?  */
  sweep_info.info = info;
  sweep_info.hide_symbol = bed->elf_backend_hide_symbol;
  elf_link_hash_traverse (elf_hash_table (info), elf_gc_sweep_symbol,
			  &sweep_info);

  _bfd_elf_link_renumber_dynsyms (abfd, info, &section_sym_count);
  return TRUE;
}

/* Propagate collected vtable information.  This is called through
   elf_link_hash_traverse.  */

static bfd_boolean
elf_gc_propagate_vtable_entries_used (struct elf_link_hash_entry *h, void *okp)
{
  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* Those that are not vtables.  */
  if (h->vtable == NULL || h->vtable->parent == NULL)
    return TRUE;

  /* Those vtables that do not have parents, we cannot merge.  */
  if (h->vtable->parent == (struct elf_link_hash_entry *) -1)
    return TRUE;

  /* If we've already been done, exit.  */
  if (h->vtable->used && h->vtable->used[-1])
    return TRUE;

  /* Make sure the parent's table is up to date.  */
  elf_gc_propagate_vtable_entries_used (h->vtable->parent, okp);

  if (h->vtable->used == NULL)
    {
      /* None of this table's entries were referenced.  Re-use the
	 parent's table.  */
      h->vtable->used = h->vtable->parent->vtable->used;
      h->vtable->size = h->vtable->parent->vtable->size;
    }
  else
    {
      size_t n;
      bfd_boolean *cu, *pu;

      /* Or the parent's entries into ours.  */
      cu = h->vtable->used;
      cu[-1] = TRUE;
      pu = h->vtable->parent->vtable->used;
      if (pu != NULL)
	{
	  const struct elf_backend_data *bed;
	  unsigned int log_file_align;

	  bed = get_elf_backend_data (h->root.u.def.section->owner);
	  log_file_align = bed->s->log_file_align;
	  n = h->vtable->parent->vtable->size >> log_file_align;
	  while (n--)
	    {
	      if (*pu)
		*cu = TRUE;
	      pu++;
	      cu++;
	    }
	}
    }

  return TRUE;
}

static bfd_boolean
elf_gc_smash_unused_vtentry_relocs (struct elf_link_hash_entry *h, void *okp)
{
  asection *sec;
  bfd_vma hstart, hend;
  Elf_Internal_Rela *relstart, *relend, *rel;
  const struct elf_backend_data *bed;
  unsigned int log_file_align;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  /* Take care of both those symbols that do not describe vtables as
     well as those that are not loaded.  */
  if (h->vtable == NULL || h->vtable->parent == NULL)
    return TRUE;

  BFD_ASSERT (h->root.type == bfd_link_hash_defined
	      || h->root.type == bfd_link_hash_defweak);

  sec = h->root.u.def.section;
  hstart = h->root.u.def.value;
  hend = hstart + h->size;

  relstart = _bfd_elf_link_read_relocs (sec->owner, sec, NULL, NULL, TRUE);
  if (!relstart)
    return *(bfd_boolean *) okp = FALSE;
  bed = get_elf_backend_data (sec->owner);
  log_file_align = bed->s->log_file_align;

  relend = relstart + sec->reloc_count * bed->s->int_rels_per_ext_rel;

  for (rel = relstart; rel < relend; ++rel)
    if (rel->r_offset >= hstart && rel->r_offset < hend)
      {
	/* If the entry is in use, do nothing.  */
	if (h->vtable->used
	    && (rel->r_offset - hstart) < h->vtable->size)
	  {
	    bfd_vma entry = (rel->r_offset - hstart) >> log_file_align;
	    if (h->vtable->used[entry])
	      continue;
	  }
	/* Otherwise, kill it.  */
	rel->r_offset = rel->r_info = rel->r_addend = 0;
      }

  return TRUE;
}

/* Mark sections containing dynamically referenced symbols.  When
   building shared libraries, we must assume that any visible symbol is
   referenced.  */

bfd_boolean
bfd_elf_gc_mark_dynamic_ref_symbol (struct elf_link_hash_entry *h, void *inf)
{
  struct bfd_link_info *info = (struct bfd_link_info *) inf;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if ((h->root.type == bfd_link_hash_defined
       || h->root.type == bfd_link_hash_defweak)
      && (h->ref_dynamic
	  || (!info->executable
	      && h->def_regular
	      && ELF_ST_VISIBILITY (h->other) != STV_INTERNAL
	      && ELF_ST_VISIBILITY (h->other) != STV_HIDDEN)))
    h->root.u.def.section->flags |= SEC_KEEP;

  return TRUE;
}

/* Do mark and sweep of unused sections.  */

bfd_boolean
bfd_elf_gc_sections (bfd *abfd, struct bfd_link_info *info)
{
  bfd_boolean ok = TRUE;
  bfd *sub;
  elf_gc_mark_hook_fn gc_mark_hook;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  if (!bed->can_gc_sections
      || info->relocatable
      || info->emitrelocations
      || !is_elf_hash_table (info->hash))
    {
      (*_bfd_error_handler)(_("Warning: gc-sections option ignored"));
      return TRUE;
    }

  /* Apply transitive closure to the vtable entry usage info.  */
  elf_link_hash_traverse (elf_hash_table (info),
			  elf_gc_propagate_vtable_entries_used,
			  &ok);
  if (!ok)
    return FALSE;

  /* Kill the vtable relocations that were not used.  */
  elf_link_hash_traverse (elf_hash_table (info),
			  elf_gc_smash_unused_vtentry_relocs,
			  &ok);
  if (!ok)
    return FALSE;

  /* Mark dynamically referenced symbols.  */
  if (elf_hash_table (info)->dynamic_sections_created)
    elf_link_hash_traverse (elf_hash_table (info),
			    bed->gc_mark_dynamic_ref,
			    info);

  /* Grovel through relocs to find out who stays ...  */
  gc_mark_hook = bed->gc_mark_hook;
  for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
    {
      asection *o;

      if (bfd_get_flavour (sub) != bfd_target_elf_flavour)
	continue;

      for (o = sub->sections; o != NULL; o = o->next)
	if ((o->flags & (SEC_EXCLUDE | SEC_KEEP)) == SEC_KEEP && !o->gc_mark)
	  if (!_bfd_elf_gc_mark (info, o, gc_mark_hook))
	    return FALSE;
    }

  /* Allow the backend to mark additional target specific sections.  */
  if (bed->gc_mark_extra_sections)
    bed->gc_mark_extra_sections(info, gc_mark_hook);

  /* ... again for sections marked from eh_frame.  */
  for (sub = info->input_bfds; sub != NULL; sub = sub->link_next)
    {
      asection *o;

      if (bfd_get_flavour (sub) != bfd_target_elf_flavour)
	continue;

      /* Keep .gcc_except_table.* if the associated .text.* (or the
	 associated .gnu.linkonce.t.* if .text.* doesn't exist) is
	 marked.  This isn't very nice, but the proper solution,
	 splitting .eh_frame up and using comdat doesn't pan out
	 easily due to needing special relocs to handle the
	 difference of two symbols in separate sections.
	 Don't keep code sections referenced by .eh_frame.  */
#define TEXT_PREFIX			".text."
#define TEXT_PREFIX2			".gnu.linkonce.t."
#define GCC_EXCEPT_TABLE_PREFIX		".gcc_except_table."
      for (o = sub->sections; o != NULL; o = o->next)
	if (!o->gc_mark && o->gc_mark_from_eh && (o->flags & SEC_CODE) == 0)
	  {
	    if (CONST_STRNEQ (o->name, GCC_EXCEPT_TABLE_PREFIX))
	      {
		char *fn_name;
		const char *sec_name;
		asection *fn_text;
		unsigned o_name_prefix_len , fn_name_prefix_len, tmp;

		o_name_prefix_len = strlen (GCC_EXCEPT_TABLE_PREFIX);
		sec_name = o->name + o_name_prefix_len;
		fn_name_prefix_len = strlen (TEXT_PREFIX);
		tmp = strlen (TEXT_PREFIX2);
		if (tmp > fn_name_prefix_len)
		  fn_name_prefix_len = tmp;
		fn_name
		  = bfd_malloc (fn_name_prefix_len + strlen (sec_name) + 1);
		if (fn_name == NULL)
		  return FALSE;

		/* Try the first prefix.  */
		sprintf (fn_name, "%s%s", TEXT_PREFIX, sec_name);
		fn_text = bfd_get_section_by_name (sub, fn_name);

		/* Try the second prefix.  */
		if (fn_text == NULL)
		  {
		    sprintf (fn_name, "%s%s", TEXT_PREFIX2, sec_name);
		    fn_text = bfd_get_section_by_name (sub, fn_name);
		  }

		free (fn_name);
		if (fn_text == NULL || !fn_text->gc_mark)
		  continue;
	      }

	    /* If not using specially named exception table section,
	       then keep whatever we are using.  */
	    if (!_bfd_elf_gc_mark (info, o, gc_mark_hook))
	      return FALSE;
	  }
    }

  /* ... and mark SEC_EXCLUDE for those that go.  */
  return elf_gc_sweep (abfd, info);
}

/* Called from check_relocs to record the existence of a VTINHERIT reloc.  */

bfd_boolean
bfd_elf_gc_record_vtinherit (bfd *abfd,
			     asection *sec,
			     struct elf_link_hash_entry *h,
			     bfd_vma offset)
{
  struct elf_link_hash_entry **sym_hashes, **sym_hashes_end;
  struct elf_link_hash_entry **search, *child;
  bfd_size_type extsymcount;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);

  /* The sh_info field of the symtab header tells us where the
     external symbols start.  We don't care about the local symbols at
     this point.  */
  extsymcount = elf_tdata (abfd)->symtab_hdr.sh_size / bed->s->sizeof_sym;
  if (!elf_bad_symtab (abfd))
    extsymcount -= elf_tdata (abfd)->symtab_hdr.sh_info;

  sym_hashes = elf_sym_hashes (abfd);
  sym_hashes_end = sym_hashes + extsymcount;

  /* Hunt down the child symbol, which is in this section at the same
     offset as the relocation.  */
  for (search = sym_hashes; search != sym_hashes_end; ++search)
    {
      if ((child = *search) != NULL
	  && (child->root.type == bfd_link_hash_defined
	      || child->root.type == bfd_link_hash_defweak)
	  && child->root.u.def.section == sec
	  && child->root.u.def.value == offset)
	goto win;
    }

  (*_bfd_error_handler) ("%B: %A+%lu: No symbol found for INHERIT",
			 abfd, sec, (unsigned long) offset);
  bfd_set_error (bfd_error_invalid_operation);
  return FALSE;

 win:
  if (!child->vtable)
    {
      child->vtable = bfd_zalloc (abfd, sizeof (*child->vtable));
      if (!child->vtable)
	return FALSE;
    }
  if (!h)
    {
      /* This *should* only be the absolute section.  It could potentially
	 be that someone has defined a non-global vtable though, which
	 would be bad.  It isn't worth paging in the local symbols to be
	 sure though; that case should simply be handled by the assembler.  */

      child->vtable->parent = (struct elf_link_hash_entry *) -1;
    }
  else
    child->vtable->parent = h;

  return TRUE;
}

/* Called from check_relocs to record the existence of a VTENTRY reloc.  */

bfd_boolean
bfd_elf_gc_record_vtentry (bfd *abfd ATTRIBUTE_UNUSED,
			   asection *sec ATTRIBUTE_UNUSED,
			   struct elf_link_hash_entry *h,
			   bfd_vma addend)
{
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  unsigned int log_file_align = bed->s->log_file_align;

  if (!h->vtable)
    {
      h->vtable = bfd_zalloc (abfd, sizeof (*h->vtable));
      if (!h->vtable)
	return FALSE;
    }

  if (addend >= h->vtable->size)
    {
      size_t size, bytes, file_align;
      bfd_boolean *ptr = h->vtable->used;

      /* While the symbol is undefined, we have to be prepared to handle
	 a zero size.  */
      file_align = 1 << log_file_align;
      if (h->root.type == bfd_link_hash_undefined)
	size = addend + file_align;
      else
	{
	  size = h->size;
	  if (addend >= size)
	    {
	      /* Oops!  We've got a reference past the defined end of
		 the table.  This is probably a bug -- shall we warn?  */
	      size = addend + file_align;
	    }
	}
      size = (size + file_align - 1) & -file_align;

      /* Allocate one extra entry for use as a "done" flag for the
	 consolidation pass.  */
      bytes = ((size >> log_file_align) + 1) * sizeof (bfd_boolean);

      if (ptr)
	{
	  ptr = bfd_realloc (ptr - 1, bytes);

	  if (ptr != NULL)
	    {
	      size_t oldbytes;

	      oldbytes = (((h->vtable->size >> log_file_align) + 1)
			  * sizeof (bfd_boolean));
	      memset (((char *) ptr) + oldbytes, 0, bytes - oldbytes);
	    }
	}
      else
	ptr = bfd_zmalloc (bytes);

      if (ptr == NULL)
	return FALSE;

      /* And arrange for that done flag to be at index -1.  */
      h->vtable->used = ptr + 1;
      h->vtable->size = size;
    }

  h->vtable->used[addend >> log_file_align] = TRUE;

  return TRUE;
}

struct alloc_got_off_arg {
  bfd_vma gotoff;
  unsigned int got_elt_size;
};

/* We need a special top-level link routine to convert got reference counts
   to real got offsets.  */

static bfd_boolean
elf_gc_allocate_got_offsets (struct elf_link_hash_entry *h, void *arg)
{
  struct alloc_got_off_arg *gofarg = arg;

  if (h->root.type == bfd_link_hash_warning)
    h = (struct elf_link_hash_entry *) h->root.u.i.link;

  if (h->got.refcount > 0)
    {
      h->got.offset = gofarg->gotoff;
      gofarg->gotoff += gofarg->got_elt_size;
    }
  else
    h->got.offset = (bfd_vma) -1;

  return TRUE;
}

/* And an accompanying bit to work out final got entry offsets once
   we're done.  Should be called from final_link.  */

bfd_boolean
bfd_elf_gc_common_finalize_got_offsets (bfd *abfd,
					struct bfd_link_info *info)
{
  bfd *i;
  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
  bfd_vma gotoff;
  unsigned int got_elt_size = bed->s->arch_size / 8;
  struct alloc_got_off_arg gofarg;

  if (! is_elf_hash_table (info->hash))
    return FALSE;

  /* The GOT offset is relative to the .got section, but the GOT header is
     put into the .got.plt section, if the backend uses it.  */
  if (bed->want_got_plt)
    gotoff = 0;
  else
    gotoff = bed->got_header_size;

  /* Do the local .got entries first.  */
  for (i = info->input_bfds; i; i = i->link_next)
    {
      bfd_signed_vma *local_got;
      bfd_size_type j, locsymcount;
      Elf_Internal_Shdr *symtab_hdr;

      if (bfd_get_flavour (i) != bfd_target_elf_flavour)
	continue;

      local_got = elf_local_got_refcounts (i);
      if (!local_got)
	continue;

      symtab_hdr = &elf_tdata (i)->symtab_hdr;
      if (elf_bad_symtab (i))
	locsymcount = symtab_hdr->sh_size / bed->s->sizeof_sym;
      else
	locsymcount = symtab_hdr->sh_info;

      for (j = 0; j < locsymcount; ++j)
	{
	  if (local_got[j] > 0)
	    {
	      local_got[j] = gotoff;
	      gotoff += got_elt_size;
	    }
	  else
	    local_got[j] = (bfd_vma) -1;
	}
    }

  /* Then the global .got entries.  .plt refcounts are handled by
     adjust_dynamic_symbol  */
  gofarg.gotoff = gotoff;
  gofarg.got_elt_size = got_elt_size;
  elf_link_hash_traverse (elf_hash_table (info),
			  elf_gc_allocate_got_offsets,
			  &gofarg);
  return TRUE;
}

/* Many folk need no more in the way of final link than this, once
   got entry reference counting is enabled.  */

bfd_boolean
bfd_elf_gc_common_final_link (bfd *abfd, struct bfd_link_info *info)
{
  if (!bfd_elf_gc_common_finalize_got_offsets (abfd, info))
    return FALSE;

  /* Invoke the regular ELF backend linker to do all the work.  */
  return bfd_elf_final_link (abfd, info);
}

bfd_boolean
bfd_elf_reloc_symbol_deleted_p (bfd_vma offset, void *cookie)
{
  struct elf_reloc_cookie *rcookie = cookie;

  if (rcookie->bad_symtab)
    rcookie->rel = rcookie->rels;

  for (; rcookie->rel < rcookie->relend; rcookie->rel++)
    {
      unsigned long r_symndx;

      if (! rcookie->bad_symtab)
	if (rcookie->rel->r_offset > offset)
	  return FALSE;
      if (rcookie->rel->r_offset != offset)
	continue;

      r_symndx = rcookie->rel->r_info >> rcookie->r_sym_shift;
      if (r_symndx == SHN_UNDEF)
	return TRUE;

      if (r_symndx >= rcookie->locsymcount
	  || ELF_ST_BIND (rcookie->locsyms[r_symndx].st_info) != STB_LOCAL)
	{
	  struct elf_link_hash_entry *h;

	  h = rcookie->sym_hashes[r_symndx - rcookie->extsymoff];

	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  if ((h->root.type == bfd_link_hash_defined
	       || h->root.type == bfd_link_hash_defweak)
	      && elf_discarded_section (h->root.u.def.section))
	    return TRUE;
	  else
	    return FALSE;
	}
      else
	{
	  /* It's not a relocation against a global symbol,
	     but it could be a relocation against a local
	     symbol for a discarded section.  */
	  asection *isec;
	  Elf_Internal_Sym *isym;

	  /* Need to: get the symbol; get the section.  */
	  isym = &rcookie->locsyms[r_symndx];
	  if (isym->st_shndx < SHN_LORESERVE || isym->st_shndx > SHN_HIRESERVE)
	    {
	      isec = bfd_section_from_elf_index (rcookie->abfd, isym->st_shndx);
	      if (isec != NULL && elf_discarded_section (isec))
		return TRUE;
	    }
	}
      return FALSE;
    }
  return FALSE;
}

/* Discard unneeded references to discarded sections.
   Returns TRUE if any section's size was changed.  */
/* This function assumes that the relocations are in sorted order,
   which is true for all known assemblers.  */

bfd_boolean
bfd_elf_discard_info (bfd *output_bfd, struct bfd_link_info *info)
{
  struct elf_reloc_cookie cookie;
  asection *stab, *eh;
  Elf_Internal_Shdr *symtab_hdr;
  const struct elf_backend_data *bed;
  bfd *abfd;
  unsigned int count;
  bfd_boolean ret = FALSE;

  if (info->traditional_format
      || !is_elf_hash_table (info->hash))
    return FALSE;

  for (abfd = info->input_bfds; abfd != NULL; abfd = abfd->link_next)
    {
      if (bfd_get_flavour (abfd) != bfd_target_elf_flavour)
	continue;

      bed = get_elf_backend_data (abfd);

      if ((abfd->flags & DYNAMIC) != 0)
	continue;

      eh = NULL;
      if (!info->relocatable)
	{
	  eh = bfd_get_section_by_name (abfd, ".eh_frame");
	  if (eh != NULL
	      && (eh->size == 0
		  || bfd_is_abs_section (eh->output_section)))
	    eh = NULL;
	}

      stab = bfd_get_section_by_name (abfd, ".stab");
      if (stab != NULL
	  && (stab->size == 0
	      || bfd_is_abs_section (stab->output_section)
	      || stab->sec_info_type != ELF_INFO_TYPE_STABS))
	stab = NULL;

      if (stab == NULL
	  && eh == NULL
	  && bed->elf_backend_discard_info == NULL)
	continue;

      symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
      cookie.abfd = abfd;
      cookie.sym_hashes = elf_sym_hashes (abfd);
      cookie.bad_symtab = elf_bad_symtab (abfd);
      if (cookie.bad_symtab)
	{
	  cookie.locsymcount = symtab_hdr->sh_size / bed->s->sizeof_sym;
	  cookie.extsymoff = 0;
	}
      else
	{
	  cookie.locsymcount = symtab_hdr->sh_info;
	  cookie.extsymoff = symtab_hdr->sh_info;
	}

      if (bed->s->arch_size == 32)
	cookie.r_sym_shift = 8;
      else
	cookie.r_sym_shift = 32;

      cookie.locsyms = (Elf_Internal_Sym *) symtab_hdr->contents;
      if (cookie.locsyms == NULL && cookie.locsymcount != 0)
	{
	  cookie.locsyms = bfd_elf_get_elf_syms (abfd, symtab_hdr,
						 cookie.locsymcount, 0,
						 NULL, NULL, NULL);
	  if (cookie.locsyms == NULL)
	    {
	      info->callbacks->einfo (_("%P%X: can not read symbols: %E\n"));
	      return FALSE;
	    }
	}

      if (stab != NULL)
	{
	  cookie.rels = NULL;
	  count = stab->reloc_count;
	  if (count != 0)
	    cookie.rels = _bfd_elf_link_read_relocs (abfd, stab, NULL, NULL,
						     info->keep_memory);
	  if (cookie.rels != NULL)
	    {
	      cookie.rel = cookie.rels;
	      cookie.relend = cookie.rels;
	      cookie.relend += count * bed->s->int_rels_per_ext_rel;
	      if (_bfd_discard_section_stabs (abfd, stab,
					      elf_section_data (stab)->sec_info,
					      bfd_elf_reloc_symbol_deleted_p,
					      &cookie))
		ret = TRUE;
	      if (elf_section_data (stab)->relocs != cookie.rels)
		free (cookie.rels);
	    }
	}

      if (eh != NULL)
	{
	  cookie.rels = NULL;
	  count = eh->reloc_count;
	  if (count != 0)
	    cookie.rels = _bfd_elf_link_read_relocs (abfd, eh, NULL, NULL,
						     info->keep_memory);
	  cookie.rel = cookie.rels;
	  cookie.relend = cookie.rels;
	  if (cookie.rels != NULL)
	    cookie.relend += count * bed->s->int_rels_per_ext_rel;

	  if (_bfd_elf_discard_section_eh_frame (abfd, info, eh,
						 bfd_elf_reloc_symbol_deleted_p,
						 &cookie))
	    ret = TRUE;

	  if (cookie.rels != NULL
	      && elf_section_data (eh)->relocs != cookie.rels)
	    free (cookie.rels);
	}

      if (bed->elf_backend_discard_info != NULL
	  && (*bed->elf_backend_discard_info) (abfd, &cookie, info))
	ret = TRUE;

      if (cookie.locsyms != NULL
	  && symtab_hdr->contents != (unsigned char *) cookie.locsyms)
	{
	  if (! info->keep_memory)
	    free (cookie.locsyms);
	  else
	    symtab_hdr->contents = (unsigned char *) cookie.locsyms;
	}
    }

  if (info->eh_frame_hdr
      && !info->relocatable
      && _bfd_elf_discard_section_eh_frame_hdr (output_bfd, info))
    ret = TRUE;

  return ret;
}

void
_bfd_elf_section_already_linked (bfd *abfd, struct bfd_section *sec,
				 struct bfd_link_info *info)
{
  flagword flags;
  const char *name, *p;
  struct bfd_section_already_linked *l;
  struct bfd_section_already_linked_hash_entry *already_linked_list;

  if (sec->output_section == bfd_abs_section_ptr)
    return;

  flags = sec->flags;

  /* Return if it isn't a linkonce section.  A comdat group section
     also has SEC_LINK_ONCE set.  */
  if ((flags & SEC_LINK_ONCE) == 0)
    return;

  /* Don't put group member sections on our list of already linked
     sections.  They are handled as a group via their group section.  */
  if (elf_sec_group (sec) != NULL)
    return;

  /* FIXME: When doing a relocatable link, we may have trouble
     copying relocations in other sections that refer to local symbols
     in the section being discarded.  Those relocations will have to
     be converted somehow; as of this writing I'm not sure that any of
     the backends handle that correctly.

     It is tempting to instead not discard link once sections when
     doing a relocatable link (technically, they should be discarded
     whenever we are building constructors).  However, that fails,
     because the linker winds up combining all the link once sections
     into a single large link once section, which defeats the purpose
     of having link once sections in the first place.

     Also, not merging link once sections in a relocatable link
     causes trouble for MIPS ELF, which relies on link once semantics
     to handle the .reginfo section correctly.  */

  name = bfd_get_section_name (abfd, sec);

  if (CONST_STRNEQ (name, ".gnu.linkonce.")
      && (p = strchr (name + sizeof (".gnu.linkonce.") - 1, '.')) != NULL)
    p++;
  else
    p = name;

  already_linked_list = bfd_section_already_linked_table_lookup (p);

  for (l = already_linked_list->entry; l != NULL; l = l->next)
    {
      /* We may have 2 different types of sections on the list: group
	 sections and linkonce sections.  Match like sections.  */
      if ((flags & SEC_GROUP) == (l->sec->flags & SEC_GROUP)
	  && strcmp (name, l->sec->name) == 0
	  && bfd_coff_get_comdat_section (l->sec->owner, l->sec) == NULL)
	{
	  /* The section has already been linked.  See if we should
	     issue a warning.  */
	  switch (flags & SEC_LINK_DUPLICATES)
	    {
	    default:
	      abort ();

	    case SEC_LINK_DUPLICATES_DISCARD:
	      break;

	    case SEC_LINK_DUPLICATES_ONE_ONLY:
	      (*_bfd_error_handler)
		(_("%B: ignoring duplicate section `%A'"),
		 abfd, sec);
	      break;

	    case SEC_LINK_DUPLICATES_SAME_SIZE:
	      if (sec->size != l->sec->size)
		(*_bfd_error_handler)
		  (_("%B: duplicate section `%A' has different size"),
		   abfd, sec);
	      break;

	    case SEC_LINK_DUPLICATES_SAME_CONTENTS:
	      if (sec->size != l->sec->size)
		(*_bfd_error_handler)
		  (_("%B: duplicate section `%A' has different size"),
		   abfd, sec);
	      else if (sec->size != 0)
		{
		  bfd_byte *sec_contents, *l_sec_contents = NULL;

		  if (!bfd_malloc_and_get_section (abfd, sec, &sec_contents))
		    (*_bfd_error_handler)
		      (_("%B: warning: could not read contents of section `%A'"),
		       abfd, sec);
		  else if (!bfd_malloc_and_get_section (l->sec->owner, l->sec,
							&l_sec_contents))
		    (*_bfd_error_handler)
		      (_("%B: warning: could not read contents of section `%A'"),
		       l->sec->owner, l->sec);
		  else if (memcmp (sec_contents, l_sec_contents, sec->size) != 0)
		    (*_bfd_error_handler)
		      (_("%B: warning: duplicate section `%A' has different contents"),
		       abfd, sec);

		  if (sec_contents)
		    free (sec_contents);
		  if (l_sec_contents)
		    free (l_sec_contents);
		}
	      break;
	    }

	  /* Set the output_section field so that lang_add_section
	     does not create a lang_input_section structure for this
	     section.  Since there might be a symbol in the section
	     being discarded, we must retain a pointer to the section
	     which we are really going to use.  */
	  sec->output_section = bfd_abs_section_ptr;
	  sec->kept_section = l->sec;

	  if (flags & SEC_GROUP)
	    {
	      asection *first = elf_next_in_group (sec);
	      asection *s = first;

	      while (s != NULL)
		{
		  s->output_section = bfd_abs_section_ptr;
		  /* Record which group discards it.  */
		  s->kept_section = l->sec;
		  s = elf_next_in_group (s);
		  /* These lists are circular.  */
		  if (s == first)
		    break;
		}
	    }

	  return;
	}
    }

  /* A single member comdat group section may be discarded by a
     linkonce section and vice versa.  */

  if ((flags & SEC_GROUP) != 0)
    {
      asection *first = elf_next_in_group (sec);

      if (first != NULL && elf_next_in_group (first) == first)
	/* Check this single member group against linkonce sections.  */
	for (l = already_linked_list->entry; l != NULL; l = l->next)
	  if ((l->sec->flags & SEC_GROUP) == 0
	      && bfd_coff_get_comdat_section (l->sec->owner, l->sec) == NULL
	      && bfd_elf_match_symbols_in_sections (l->sec, first, info))
	    {
	      first->output_section = bfd_abs_section_ptr;
	      first->kept_section = l->sec;
	      sec->output_section = bfd_abs_section_ptr;
	      break;
	    }
    }
  else
    /* Check this linkonce section against single member groups.  */
    for (l = already_linked_list->entry; l != NULL; l = l->next)
      if (l->sec->flags & SEC_GROUP)
	{
	  asection *first = elf_next_in_group (l->sec);

	  if (first != NULL
	      && elf_next_in_group (first) == first
	      && bfd_elf_match_symbols_in_sections (first, sec, info))
	    {
	      sec->output_section = bfd_abs_section_ptr;
	      sec->kept_section = first;
	      break;
	    }
	}

  /* This is the first section with this name.  Record it.  */
  bfd_section_already_linked_table_insert (already_linked_list, sec);
}

bfd_boolean
_bfd_elf_common_definition (Elf_Internal_Sym *sym)
{
  return sym->st_shndx == SHN_COMMON;
}

unsigned int
_bfd_elf_common_section_index (asection *sec ATTRIBUTE_UNUSED)
{
  return SHN_COMMON;
}

asection *
_bfd_elf_common_section (asection *sec ATTRIBUTE_UNUSED)
{
  return bfd_com_section_ptr;
}
