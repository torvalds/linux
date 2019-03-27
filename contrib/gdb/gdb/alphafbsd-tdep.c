/* Target-dependent code for FreeBSD/Alpha.
   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.

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
#include "value.h"
#include "osabi.h"

#include "alpha-tdep.h"

static int
alphafbsd_use_struct_convention (int gcc_p, struct type *type)
{
  enum type_code code;
  int i;

  /* All aggregate types that won't fit in a register must be returned
     in memory.  */
  if (TYPE_LENGTH (type) > ALPHA_REGISTER_SIZE)
    return 1;

  /* The only aggregate types that can be returned in a register are
     structs and unions.  Arrays must be returned in memory.  */
  code = TYPE_CODE (type);
  if (code != TYPE_CODE_STRUCT && code != TYPE_CODE_UNION)
    return 1;

  /* We need to check if this struct/union is "integer" like.  For
     this to be true, the offset of each adressable subfield must be
     zero.  Note that bit fields are not addressable.  */
  for (i = 0; i < TYPE_NFIELDS (type); i++)
    {
      /* If the field bitsize is non-zero, it isn't adressable.  */
      if (TYPE_FIELD_BITPOS (type, i) != 0
	  && TYPE_FIELD_BITSIZE (type, i) == 0)
	return 1;
    }

  return 0;
}


/* Support for signal handlers.  */

/* Return whether PC is in a BSD sigtramp routine.  */

CORE_ADDR alphafbsd_sigtramp_start = 0x11ffff68;
CORE_ADDR alphafbsd_sigtramp_end = 0x11ffffe0;

static int
alphafbsd_pc_in_sigtramp (CORE_ADDR pc, char *func_name)
{
  return (pc >= alphafbsd_sigtramp_start && pc < alphafbsd_sigtramp_end);
}

static LONGEST
alphafbsd_sigtramp_offset (CORE_ADDR pc)
{
  return pc - alphafbsd_sigtramp_start;
}

/* Assuming NEXT_FRAME is for a frame following a BSD sigtramp
   routine, return the address of the associated sigcontext structure.  */

static CORE_ADDR
alphafbsd_sigcontext_addr (struct frame_info *next_frame)
{
  return frame_unwind_register_unsigned (next_frame, ALPHA_SP_REGNUM) + 24;
}

/* FreeBSD 5.0-RELEASE or later.  */

static void
alphafbsd_init_abi (struct gdbarch_info info,
                    struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  /* Hook into the DWARF CFI frame unwinder.  */
  alpha_dwarf2_init_abi (info, gdbarch);

  /* Hook into the MDEBUG frame unwinder.  */
  alpha_mdebug_init_abi (info, gdbarch);

  set_gdbarch_use_struct_convention (gdbarch, alphafbsd_use_struct_convention);

  set_gdbarch_pc_in_sigtramp (gdbarch, alphafbsd_pc_in_sigtramp);

  tdep->dynamic_sigtramp_offset = alphafbsd_sigtramp_offset;
  tdep->sigcontext_addr = alphafbsd_sigcontext_addr;
  tdep->sc_pc_offset = 288;
  tdep->sc_regs_offset = 24;
  tdep->sc_fpregs_offset = 320;

  tdep->jb_pc = 2;
  tdep->jb_elt_size = 8;
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_alphafbsd_tdep (void);

void
_initialize_alphafbsd_tdep (void)
{
  gdbarch_register_osabi (bfd_arch_alpha, 0, GDB_OSABI_FREEBSD_ELF,
                          alphafbsd_init_abi);
}
