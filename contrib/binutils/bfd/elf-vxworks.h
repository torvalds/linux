/* VxWorks support for ELF
   Copyright 2005 Free Software Foundation, Inc.

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

#include "elf/common.h"
#include "elf/internal.h"

bfd_boolean elf_vxworks_add_symbol_hook
  (bfd *, struct bfd_link_info *, Elf_Internal_Sym *, const char **,
   flagword *, asection **, bfd_vma *);
bfd_boolean elf_vxworks_link_output_symbol_hook
  (struct bfd_link_info *, const char *name, Elf_Internal_Sym *,
   asection *, struct elf_link_hash_entry *);
bfd_boolean elf_vxworks_emit_relocs
  (bfd *, asection *, Elf_Internal_Shdr *, Elf_Internal_Rela *,
   struct elf_link_hash_entry **);
void elf_vxworks_final_write_processing (bfd *, bfd_boolean);
bfd_boolean elf_vxworks_create_dynamic_sections
  (bfd *, struct bfd_link_info *, asection **);
