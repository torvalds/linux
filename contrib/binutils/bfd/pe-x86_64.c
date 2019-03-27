/* BFD back-end for Intel/AMD x86_64 PECOFF files.
   Copyright 2006, 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
   
   Written by Kai Tietz, OneVision Software GmbH&CoKg.  */

#include "sysdep.h"
#include "bfd.h"

#define TARGET_SYM 		x86_64pe_vec
#define TARGET_NAME 		"pe-x86-64"
#define COFF_WITH_PE
#define COFF_WITH_pex64
#define PCRELOFFSET 		TRUE
#define TARGET_UNDERSCORE 	'_'
#define COFF_LONG_SECTION_NAMES
#define COFF_SUPPORT_GNU_LINKONCE
#define COFF_LONG_FILENAMES

#define COFF_SECTION_ALIGNMENT_ENTRIES \
{ COFF_SECTION_NAME_EXACT_MATCH (".bss"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 4 }, \
{ COFF_SECTION_NAME_EXACT_MATCH (".data"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 4 }, \
{ COFF_SECTION_NAME_EXACT_MATCH (".rdata"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 4 }, \
{ COFF_SECTION_NAME_EXACT_MATCH (".text"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 4 }, \
{ COFF_SECTION_NAME_PARTIAL_MATCH (".idata"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 2 }, \
{ COFF_SECTION_NAME_EXACT_MATCH (".pdata"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 2 }, \
{ COFF_SECTION_NAME_PARTIAL_MATCH (".debug"), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 0 }, \
{ COFF_SECTION_NAME_PARTIAL_MATCH (".gnu.linkonce.wi."), \
  COFF_ALIGNMENT_FIELD_EMPTY, COFF_ALIGNMENT_FIELD_EMPTY, 0 }

#include "coff-x86_64.c"
