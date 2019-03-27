/* Target-specific functions for ARM running under NetBSD.
   Copyright 2002, 2003 Free Software Foundation, Inc.

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
#include "osabi.h"

#include "arm-tdep.h"
#include "nbsd-tdep.h"
#include "solib-svr4.h"

/* Description of the longjmp buffer.  */
#define ARM_NBSD_JB_PC 24
#define ARM_NBSD_JB_ELEMENT_SIZE INT_REGISTER_SIZE

/* For compatibility with previous implemenations of GDB on arm/NetBSD,
   override the default little-endian breakpoint.  */
static const char arm_nbsd_arm_le_breakpoint[] = {0x11, 0x00, 0x00, 0xe6};

static int
arm_netbsd_aout_in_solib_call_trampoline (CORE_ADDR pc, char *name)
{
  if (strcmp (name, "_PROCEDURE_LINKAGE_TABLE_") == 0)
    return 1;

  return 0;
}

static void
arm_netbsd_init_abi_common (struct gdbarch_info info,
			    struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  tdep->lowest_pc = 0x8000;
  tdep->arm_breakpoint = arm_nbsd_arm_le_breakpoint;
  tdep->arm_breakpoint_size = sizeof (arm_nbsd_arm_le_breakpoint);

  tdep->jb_pc = ARM_NBSD_JB_PC;
  tdep->jb_elt_size = ARM_NBSD_JB_ELEMENT_SIZE;
}
  
static void
arm_netbsd_aout_init_abi (struct gdbarch_info info, 
			  struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  arm_netbsd_init_abi_common (info, gdbarch);

  set_gdbarch_in_solib_call_trampoline
    (gdbarch, arm_netbsd_aout_in_solib_call_trampoline);
  tdep->fp_model = ARM_FLOAT_SOFT_FPA;
}

static void
arm_netbsd_elf_init_abi (struct gdbarch_info info, 
			 struct gdbarch *gdbarch)
{
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);

  arm_netbsd_init_abi_common (info, gdbarch);

  set_solib_svr4_fetch_link_map_offsets (gdbarch,
                                nbsd_ilp32_solib_svr4_fetch_link_map_offsets);

  tdep->fp_model = ARM_FLOAT_SOFT_VFP;
}

static enum gdb_osabi
arm_netbsd_aout_osabi_sniffer (bfd *abfd)
{
  if (strcmp (bfd_get_target (abfd), "a.out-arm-netbsd") == 0)
    return GDB_OSABI_NETBSD_AOUT;

  return GDB_OSABI_UNKNOWN;
}

void
_initialize_arm_netbsd_tdep (void)
{
  gdbarch_register_osabi_sniffer (bfd_arch_arm, bfd_target_aout_flavour,
				  arm_netbsd_aout_osabi_sniffer);

  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_NETBSD_AOUT,
                          arm_netbsd_aout_init_abi);
  gdbarch_register_osabi (bfd_arch_arm, 0, GDB_OSABI_NETBSD_ELF,
                          arm_netbsd_elf_init_abi);
}
