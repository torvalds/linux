/* BFD support for the score processor
   Copyright 2006, 2007 Free Software Foundation, Inc.   
   Contributed by
   Mei Ligang (ligang@sunnorth.com.cn)
   Pei-Lin Tsai (pltsai@sunplus.com)  

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
#include "libbfd.h"

const bfd_arch_info_type
bfd_score_arch =
{
  32,				/* There's 32 bits_per_word.  */
  32,				/* There's 32 bits_per_address.  */
  8,				/* There's 8 bits_per_byte.  */
  bfd_arch_score,		/* One of enum bfd_architecture, defined
				   in archures.c and provided in
				   generated header files.  */
  0,				/* Only 1 machine, but #255 for
				   historical reasons.  */
  "score",			/* The arch_name.  */
  "score",			/* The printable name is the same.  */
  4,				/* Section alignment power; each section
				   is aligned to (only) 2^4 bytes.  */
  TRUE,				/* This is the default "machine", since
				   there's only one.  */
  bfd_default_compatible,	/* A default function for testing
				   "machine" compatibility of two
				   bfd_arch_info_type.  */
  bfd_default_scan,		/* Check if an bfd_arch_info_type is a
				   match.  */
  NULL				/* Pointer to next bfd_arch_info_type in
				   the same family.  */
};
