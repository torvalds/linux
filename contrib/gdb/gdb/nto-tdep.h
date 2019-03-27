/* nto-tdep.h - QNX Neutrino target header.

   Copyright 2003 Free Software Foundation, Inc.

   Contributed by QNX Software Systems Ltd.

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

#ifndef _NTO_TDEP_H
#define _NTO_TDEP_H

#include "defs.h"
#include "solist.h"

/* Generic functions in nto-tdep.c.  */

extern void nto_init_solib_absolute_prefix (void);

char **nto_parse_redirection (char *start_argv[], char **in,
			      char **out, char **err);

int proc_iterate_over_mappings (int (*func) (int, CORE_ADDR));

void nto_relocate_section_addresses (struct so_list *, struct section_table *);

int nto_map_arch_to_cputype (const char *);

int nto_find_and_open_solib (char *, unsigned, char **);

/* Dummy function for initializing nto_target_ops on targets which do
   not define a particular regset.  */
void nto_dummy_supply_regset (char *regs);

/* Target operations defined for Neutrino targets (<target>-nto-tdep.c).  */

struct nto_target_ops
{
  int nto_internal_debugging;
  unsigned nto_cpuinfo_flags;
  int nto_cpuinfo_valid;

  int (*nto_regset_id) (int);
  void (*nto_supply_gregset) (char *);
  void (*nto_supply_fpregset) (char *);
  void (*nto_supply_altregset) (char *);
  void (*nto_supply_regset) (int, char *);
  int (*nto_register_area) (int, int, unsigned *);
  int (*nto_regset_fill) (int, char *);
  struct link_map_offsets *(*nto_fetch_link_map_offsets) (void);
};

extern struct nto_target_ops current_nto_target;

/* For 'maintenance debug nto-debug' command.  */
#define nto_internal_debugging \
	(current_nto_target.nto_internal_debugging)

/* The CPUINFO flags from the remote.  Currently used by
   i386 for fxsave but future proofing other hosts.
   This is initialized in procfs_attach or nto_start_remote
   depending on our host/target.  It would only be invalid
   if we were talking to an older pdebug which didn't support
   the cpuinfo message.  */
#define nto_cpuinfo_flags \
	(current_nto_target.nto_cpuinfo_flags)

/* True if successfully retrieved cpuinfo from remote.  */
#define nto_cpuinfo_valid \
	(current_nto_target.nto_cpuinfo_valid)

/* Given a register, return an id that represents the Neutrino
   regset it came from.  If reg == -1 update all regsets.  */
#define nto_regset_id(reg) \
	(*current_nto_target.nto_regset_id) (reg)

#define nto_supply_gregset(regs) \
	(*current_nto_target.nto_supply_gregset) (regs)

#define nto_supply_fpregset(regs) \
	(*current_nto_target.nto_supply_fpregset) (regs)

#define nto_supply_altregset(regs) \
	(*current_nto_target.nto_supply_altregset) (regs)

/* Given a regset, tell gdb about registers stored in data.  */
#define nto_supply_regset(regset, data) \
	(*current_nto_target.nto_supply_regset) (regset, data)

/* Given a register and regset, calculate the offset into the regset
   and stuff it into the last argument.  If regno is -1, calculate the
   size of the entire regset.  Returns length of data, -1 if unknown
   regset, 0 if unknown register.  */
#define nto_register_area(reg, regset, off) \
	(*current_nto_target.nto_register_area) (reg, regset, off)

/* Build the Neutrino register set info into the data buffer.  
   Return -1 if unknown regset, 0 otherwise.  */
#define nto_regset_fill(regset, data) \
	(*current_nto_target.nto_regset_fill) (regset, data)

/* Gives the fetch_link_map_offsets function exposure outside of
   solib-svr4.c so that we can override relocate_section_addresses().  */
#define nto_fetch_link_map_offsets() \
	(*current_nto_target.nto_fetch_link_map_offsets) ()

/* Keep this consistant with neutrino syspage.h.  */
enum
{
  CPUTYPE_X86,
  CPUTYPE_PPC,
  CPUTYPE_MIPS,
  CPUTYPE_SPARE,
  CPUTYPE_ARM,
  CPUTYPE_SH,
  CPUTYPE_UNKNOWN
};

enum
{
  OSTYPE_QNX4,
  OSTYPE_NTO
};

/* These correspond to the DSMSG_* versions in dsmsgs.h. */
enum
{
  NTO_REG_GENERAL,
  NTO_REG_FLOAT,
  NTO_REG_SYSTEM,
  NTO_REG_ALT,
  NTO_REG_END
};

typedef char qnx_reg64[8];

typedef struct _debug_regs
{
  qnx_reg64 padding[1024];
} nto_regset_t;

#endif
