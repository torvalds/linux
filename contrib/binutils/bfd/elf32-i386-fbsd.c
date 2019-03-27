/* Intel IA-32 specific support for 32-bit ELF on FreeBSD.
   Copyright 2002 Free Software Foundation, Inc.

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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#define TARGET_LITTLE_SYM	bfd_elf32_i386_freebsd_vec
#define TARGET_LITTLE_NAME	"elf32-i386-freebsd"
#define ELF_ARCH		bfd_arch_i386
#define ELF_MACHINE_CODE	EM_386
#define ELF_MAXPAGESIZE		0x1000

#include "bfd.h"
#include "sysdep.h"
#include "elf-bfd.h"

/* The kernel recognizes executables as valid only if they carry a
   "FreeBSD" label in the ELF header.  So we put this label on all
   executables and (for simplicity) also all other object files.  */

static void elf_i386_post_process_headers
  PARAMS ((bfd *, struct bfd_link_info *));

static void
elf_i386_post_process_headers (abfd, link_info)
     bfd * abfd;
     struct bfd_link_info * link_info ATTRIBUTE_UNUSED;
{
  Elf_Internal_Ehdr * i_ehdrp;	/* ELF file header, internal form.  */

  i_ehdrp = elf_elfheader (abfd);

  /* Put an ABI label supported by FreeBSD >= 4.1.  */
  i_ehdrp->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;
#ifdef OLD_FREEBSD_ABI_LABEL
  /* The ABI label supported by FreeBSD <= 4.0 is quite nonstandard.  */
  memcpy (&i_ehdrp->e_ident[EI_ABIVERSION], "FreeBSD", 8);
#endif
}

#define elf_backend_post_process_headers	elf_i386_post_process_headers

#include "elf32-i386.c"
