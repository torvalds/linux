/* Subroutines for insn-output.c for NetWare.
   Contributed by Jan Beulich (jbeulich@novell.com)
   Copyright (C) 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "output.h"
#include "tree.h"
#include "flags.h"
#include "tm_p.h"
#include "toplev.h"

void
nwld_named_section_asm_out_constructor (rtx symbol, int priority)
{
#if !SUPPORTS_INIT_PRIORITY
  const char section[] = ".ctors"TARGET_SUB_SECTION_SEPARATOR;
#else
  char section[20];

  sprintf (section,
	   ".ctors"TARGET_SUB_SECTION_SEPARATOR"%.5u",
	   /* Invert the numbering so the linker puts us in the proper
	      order; constructors are run from right to left, and the
	      linker sorts in increasing order.  */
	   MAX_INIT_PRIORITY - priority);
#endif

  switch_to_section (get_section (section, 0, NULL));
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);
}

void
nwld_named_section_asm_out_destructor (rtx symbol, int priority)
{
#if !SUPPORTS_INIT_PRIORITY
  const char section[] = ".dtors"TARGET_SUB_SECTION_SEPARATOR;
#else
  char section[20];

  sprintf (section, ".dtors"TARGET_SUB_SECTION_SEPARATOR"%.5u",
	   /* Invert the numbering so the linker puts us in the proper
	      order; destructors are run from left to right, and the
	      linker sorts in increasing order.  */
	   MAX_INIT_PRIORITY - priority);
#endif

  switch_to_section (get_section (section, 0, NULL));
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);
}
