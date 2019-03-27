/* PowerPC64-specific support for 64-bit ELF.
   Copyright 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

void ppc64_elf_init_stub_bfd
  (bfd *, struct bfd_link_info *);
bfd_boolean ppc64_elf_edit_opd
  (bfd *, struct bfd_link_info *, bfd_boolean, bfd_boolean);
asection *ppc64_elf_tls_setup
  (bfd *, struct bfd_link_info *);
bfd_boolean ppc64_elf_tls_optimize
  (bfd *, struct bfd_link_info *);
bfd_boolean ppc64_elf_edit_toc
  (bfd *, struct bfd_link_info *);
bfd_vma ppc64_elf_toc
  (bfd *);
int ppc64_elf_setup_section_lists
  (bfd *, struct bfd_link_info *, int);
void ppc64_elf_next_toc_section
  (struct bfd_link_info *, asection *);
void ppc64_elf_reinit_toc
  (bfd *, struct bfd_link_info *);
bfd_boolean ppc64_elf_next_input_section
  (struct bfd_link_info *, asection *);
bfd_boolean ppc64_elf_size_stubs
  (bfd *, struct bfd_link_info *, bfd_signed_vma,
   asection *(*) (const char *, asection *), void (*) (void));
bfd_boolean ppc64_elf_build_stubs
  (bfd_boolean, struct bfd_link_info *, char **);
void ppc64_elf_restore_symbols
  (struct bfd_link_info *info);
