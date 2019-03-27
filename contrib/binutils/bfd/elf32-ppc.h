/* PowerPC-specific support for 64-bit ELF.
   Copyright 2003, 2005, 2007 Free Software Foundation, Inc.

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

enum ppc_elf_plt_type {
  PLT_UNSET,
  PLT_OLD,
  PLT_NEW,
  PLT_VXWORKS
};

int ppc_elf_select_plt_layout (bfd *, struct bfd_link_info *,
			       enum ppc_elf_plt_type, int);
asection *ppc_elf_tls_setup (bfd *, struct bfd_link_info *);
bfd_boolean ppc_elf_tls_optimize (bfd *, struct bfd_link_info *);
void ppc_elf_set_sdata_syms (bfd *, struct bfd_link_info *);
