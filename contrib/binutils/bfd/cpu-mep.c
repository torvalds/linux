/* BFD support for the Toshiba Media Engine Processor.
   Copyright (C) 2001, 2002, 2004, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

#define MA(x, n, def, y) { 32, 32, 8, bfd_arch_mep, x, "mep", n, \
	2, def, bfd_default_compatible, bfd_default_scan, y }

static const bfd_arch_info_type bfd_h1_arch = MA (bfd_mach_mep_h1, "h1", FALSE, NULL);
const bfd_arch_info_type bfd_mep_arch = MA (bfd_mach_mep, "mep", TRUE, & bfd_h1_arch);
