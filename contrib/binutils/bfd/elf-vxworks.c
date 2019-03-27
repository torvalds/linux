/* VxWorks support for ELF
   Copyright 2005, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* This file provides routines used by all VxWorks targets.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf-bfd.h"
#include "elf-vxworks.h"

/* Return true if symbol NAME, as defined by ABFD, is one of the special
   __GOTT_BASE__ or __GOTT_INDEX__ symbols.  */

static bfd_boolean
elf_vxworks_gott_symbol_p (bfd *abfd, const char *name)
{
  char leading;

  leading = bfd_get_symbol_leading_char (abfd);
  if (leading)
    {
      if (*name != leading)
	return FALSE;
      name++;
    }
  return (strcmp (name, "__GOTT_BASE__") == 0
	  || strcmp (name, "__GOTT_INDEX__") == 0);
}

/* Tweak magic VxWorks symbols as they are loaded.  */
bfd_boolean
elf_vxworks_add_symbol_hook (bfd *abfd,
			     struct bfd_link_info *info,
			     Elf_Internal_Sym *sym,
			     const char **namep,
			     flagword *flagsp,
			     asection **secp ATTRIBUTE_UNUSED,
			     bfd_vma *valp ATTRIBUTE_UNUSED)
{
  /* Ideally these "magic" symbols would be exported by libc.so.1
     which would be found via a DT_NEEDED tag, and then handled
     specially by the linker at runtime.  Except shared libraries
     don't even link to libc.so.1 by default...
     If the symbol is imported from, or will be put in a shared library,
     give the symbol weak binding to get the desired samantics.
     This transformation will be undone in
     elf_i386_vxworks_link_output_symbol_hook. */
  if ((info->shared || abfd->flags & DYNAMIC)
      && elf_vxworks_gott_symbol_p (abfd, *namep))
    {
      sym->st_info = ELF_ST_INFO (STB_WEAK, ELF_ST_TYPE (sym->st_info));
      *flagsp |= BSF_WEAK;
    }

  return TRUE;
}

/* Perform VxWorks-specific handling of the create_dynamic_sections hook.
   When creating an executable, set *SRELPLT2_OUT to the .rel(a).plt.unloaded
   section.  */

bfd_boolean
elf_vxworks_create_dynamic_sections (bfd *dynobj, struct bfd_link_info *info,
				     asection **srelplt2_out)
{
  struct elf_link_hash_table *htab;
  const struct elf_backend_data *bed;
  asection *s;

  htab = elf_hash_table (info);
  bed = get_elf_backend_data (dynobj);

  if (!info->shared)
    {
      s = bfd_make_section_with_flags (dynobj,
				       bed->default_use_rela_p
				       ? ".rela.plt.unloaded"
				       : ".rel.plt.unloaded",
				       SEC_HAS_CONTENTS | SEC_IN_MEMORY
				       | SEC_READONLY | SEC_LINKER_CREATED);
      if (s == NULL
	  || !bfd_set_section_alignment (dynobj, s, bed->s->log_file_align))
	return FALSE;

      *srelplt2_out = s;
    }

  /* Mark the GOT and PLT symbols as having relocations; they might
     not, but we won't know for sure until we build the GOT in
     finish_dynamic_symbol.  Also make sure that the GOT symbol
     is entered into the dynamic symbol table; the loader uses it
     to initialize __GOTT_BASE__[__GOTT_INDEX__].  */
  if (htab->hgot)
    {
      htab->hgot->indx = -2;
      htab->hgot->other &= ~ELF_ST_VISIBILITY (-1);
      htab->hgot->forced_local = 0;
      if (!bfd_elf_link_record_dynamic_symbol (info, htab->hgot))
	return FALSE;
    }
  if (htab->hplt)
    {
      htab->hplt->indx = -2;
      htab->hplt->type = STT_FUNC;
    }

  return TRUE;
}

/* Tweak magic VxWorks symbols as they are written to the output file.  */
bfd_boolean
elf_vxworks_link_output_symbol_hook (struct bfd_link_info *info
				       ATTRIBUTE_UNUSED,
				     const char *name,
				     Elf_Internal_Sym *sym,
				     asection *input_sec ATTRIBUTE_UNUSED,
				     struct elf_link_hash_entry *h)
{
  /* Reverse the effects of the hack in elf_vxworks_add_symbol_hook.  */
  if (h
      && h->root.type == bfd_link_hash_undefweak
      && elf_vxworks_gott_symbol_p (h->root.u.undef.abfd, name))
    sym->st_info = ELF_ST_INFO (STB_GLOBAL, ELF_ST_TYPE (sym->st_info));

  return TRUE;
}


/* Copy relocations into the output file.  Fixes up relocations againt PLT
   entries, then calls the generic routine.  */

bfd_boolean
elf_vxworks_emit_relocs (bfd *output_bfd,
			 asection *input_section,
			 Elf_Internal_Shdr *input_rel_hdr,
			 Elf_Internal_Rela *internal_relocs,
			 struct elf_link_hash_entry **rel_hash)
{
  const struct elf_backend_data *bed;
  Elf_Internal_Rela *irela;
  Elf_Internal_Rela *irelaend;
  int j;

  bed = get_elf_backend_data (output_bfd);

  irela = internal_relocs;
  irelaend = irela + (NUM_SHDR_ENTRIES (input_rel_hdr)
		      * bed->s->int_rels_per_ext_rel);
  while (irela < irelaend)
    {
      if ((output_bfd->flags & (DYNAMIC|EXEC_P))
	  && *rel_hash
	  && (*rel_hash)->def_dynamic
	  && !(*rel_hash)->def_regular
	  && ((*rel_hash)->root.type == bfd_link_hash_defined
	      || (*rel_hash)->root.type == bfd_link_hash_defweak)
	  && (*rel_hash)->root.u.def.section->output_section != NULL)
	{
	  /* This is a relocation from an executable or shared library
	     against a symbol in a different shared library.  We are
	     creating a definition in the output file but it does not come
	     from any of our normal (.o) files. ie. a PLT stub.
	     Normally this would be a relocation against against SHN_UNDEF
	     with the VMA of the PLT stub.  This upsets the VxWorks loader.
	     Convert it to a section-relative relocation.
	     This gets some other symbols (for instance .dynbss),
	     but is conservatively correct.  */
	  for (j = 0; j < bed->s->int_rels_per_ext_rel; j++)
	    {
	      asection *sec = (*rel_hash)->root.u.def.section;
	      int this_idx = sec->output_section->target_index;

	      irela[j].r_info = ELF32_R_INFO (this_idx,
		  ELF32_R_TYPE (irela[j].r_info));
	      irela[j].r_addend += (*rel_hash)->root.u.def.value;
	      irela[j].r_addend += sec->output_offset;
	    }
	  /* Stop the generic routine adjusting this entry.  */
	  *rel_hash = NULL;
	}
      irela += bed->s->int_rels_per_ext_rel;
      rel_hash++;
    }
  return _bfd_elf_link_output_relocs (output_bfd, input_section,
				      input_rel_hdr, internal_relocs,
				      rel_hash);
}


/* Set the sh_link and sh_info fields on the static plt relocation secton.  */

void
elf_vxworks_final_write_processing (bfd *abfd,
				    bfd_boolean linker ATTRIBUTE_UNUSED)
{
  asection * sec;
  struct bfd_elf_section_data *d;

  sec = bfd_get_section_by_name (abfd, ".rel.plt.unloaded");
  if (!sec)
    sec = bfd_get_section_by_name (abfd, ".rela.plt.unloaded");
  if (!sec)
    return;
  d = elf_section_data (sec);
  d->this_hdr.sh_link = elf_tdata (abfd)->symtab_section;
  sec = bfd_get_section_by_name (abfd, ".plt");
  if (sec)
    d->this_hdr.sh_info = elf_section_data (sec)->this_idx;
}
