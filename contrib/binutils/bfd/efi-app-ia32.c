/* BFD back-end for Intel IA-32 EFI application files.
   Copyright 1999, 2000, 2001, 2002, 2007 Free Software Foundation, Inc.
   Contributed by David Mosberger <davidm@hpl.hp.com>

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

#define TARGET_SYM bfd_efi_app_ia32_vec
#define TARGET_NAME "efi-app-ia32"
#define COFF_IMAGE_WITH_PE
#define COFF_WITH_PE
#define PCRELOFFSET TRUE
#define TARGET_UNDERSCORE '_'
#define COFF_LONG_SECTION_NAMES
#define PEI_TARGET_SUBSYSTEM	IMAGE_SUBSYSTEM_EFI_APPLICATION
#define PEI_FORCE_MINIMUM_ALIGNMENT

#include "coff-i386.c"
