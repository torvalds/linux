/* Native-dependent code for OpenBSD/amd64.

   Copyright 2003, 2004 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"

#include "gdb_assert.h"

#include "amd64-tdep.h"
#include "amd64-nat.h"

/* Mapping between the general-purpose registers in OpenBSD/amd64
   `struct reg' format and GDB's register cache layout for
   OpenBSD/i386.

   Note that most (if not all) OpenBSD/amd64 registers are 64-bit,
   while the OpenBSD/i386 registers are all 32-bit, but since we're
   little-endian we get away with that.  */

/* From <machine/reg.h>.  */
static int amd64obsd32_r_reg_offset[] =
{
  14 * 8,			/* %eax */
  3 * 8,			/* %ecx */
  2 * 8,			/* %edx */
  13 * 8,			/* %ebx */
  15 * 8,			/* %esp */
  12 * 8,			/* %ebp */
  1 * 8,			/* %esi */
  0 * 8,			/* %edi */
  16 * 8,			/* %eip */
  17 * 8,			/* %eflags */
  18 * 8,			/* %cs */
  19 * 8,			/* %ss */
  20 * 8,			/* %ds */
  21 * 8,			/* %es */
  22 * 8,			/* %fs */
  23 * 8			/* %gs */
};


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_amd64obsd_nat (void);

void
_initialize_amd64obsd_nat (void)
{
  amd64_native_gregset32_reg_offset = amd64obsd32_r_reg_offset;
  amd64_native_gregset32_num_regs = ARRAY_SIZE (amd64obsd32_r_reg_offset);
  amd64_native_gregset64_reg_offset = amd64obsd_r_reg_offset;
}
