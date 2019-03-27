/* BFD back-end for 64-bit a.out files.
   Copyright 1990, 1991, 1992, 1994 Free Software Foundation, Inc.
   Written by Cygnus Support.

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

#define ARCH_SIZE 64

/* aoutx.h requires definitions for BMAGIC and QMAGIC.  */
#ifndef BMAGIC
#define BMAGIC 0
#endif
#ifndef QMAGIC
#define QMAGIC 0
#endif

#include "aoutx.h"
