/* S390 native-dependent code for GDB, the GNU debugger.
   Copyright 2001, 2003 Free Software Foundation, Inc

   Contributed by D.J. Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
   for IBM Deutschland Entwicklung GmbH, IBM Corporation.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#include "defs.h"
#include "tm.h"
#include "regcache.h"
#include "inferior.h"

#include "s390-tdep.h"

#include <asm/ptrace.h>
#include <sys/ptrace.h>
#include <asm/types.h>
#include <sys/procfs.h>
#include <sys/user.h>
#include <sys/ucontext.h>


/* Map registers to gregset/ptrace offsets.
   These arrays are defined in s390-tdep.c.  */

#ifdef __s390x__
#define regmap_gregset s390x_regmap_gregset
#else
#define regmap_gregset s390_regmap_gregset
#endif

#define regmap_fpregset s390_regmap_fpregset

/* When debugging a 32-bit executable running under a 64-bit kernel,
   we have to fix up the 64-bit registers we get from the kernel
   to make them look like 32-bit registers.  */
#ifdef __s390x__
#define SUBOFF(i) \
	((TARGET_PTR_BIT == 32 \
	  && ((i) == S390_PSWA_REGNUM \
	      || ((i) >= S390_R0_REGNUM && (i) <= S390_R15_REGNUM)))? 4 : 0)
#else
#define SUBOFF(i) 0
#endif


/* Fill GDB's register array with the general-purpose register values
   in *REGP.  */
void
supply_gregset (gregset_t *regp)
{
  int i;
  for (i = 0; i < S390_NUM_REGS; i++)
    if (regmap_gregset[i] != -1)
      regcache_raw_supply (current_regcache, i, 
			   (char *)regp + regmap_gregset[i] + SUBOFF (i));
}

/* Fill register REGNO (if it is a general-purpose register) in
   *REGP with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */
void
fill_gregset (gregset_t *regp, int regno)
{
  int i;
  for (i = 0; i < S390_NUM_REGS; i++)
    if (regmap_gregset[i] != -1)
      if (regno == -1 || regno == i)
	regcache_raw_collect (current_regcache, i, 
			      (char *)regp + regmap_gregset[i] + SUBOFF (i));
}

/* Fill GDB's register array with the floating-point register values
   in *REGP.  */
void
supply_fpregset (fpregset_t *regp)
{
  int i;
  for (i = 0; i < S390_NUM_REGS; i++)
    if (regmap_fpregset[i] != -1)
      regcache_raw_supply (current_regcache, i,
			   ((char *)regp) + regmap_fpregset[i]);
}

/* Fill register REGNO (if it is a general-purpose register) in
   *REGP with the value in GDB's register array.  If REGNO is -1,
   do this for all registers.  */
void
fill_fpregset (fpregset_t *regp, int regno)
{
  int i;
  for (i = 0; i < S390_NUM_REGS; i++)
    if (regmap_fpregset[i] != -1)
      if (regno == -1 || regno == i)
        regcache_raw_collect (current_regcache, i, 
			      ((char *)regp) + regmap_fpregset[i]);
}

/* Find the TID for the current inferior thread to use with ptrace.  */
static int
s390_inferior_tid (void)
{
  /* GNU/Linux LWP ID's are process ID's.  */
  int tid = TIDGET (inferior_ptid);
  if (tid == 0)
    tid = PIDGET (inferior_ptid); /* Not a threaded program.  */

  return tid;
}

/* Fetch all general-purpose registers from process/thread TID and
   store their values in GDB's register cache.  */
static void
fetch_regs (int tid)
{
  gregset_t regs;
  ptrace_area parea;

  parea.len = sizeof (regs);
  parea.process_addr = (addr_t) &regs;
  parea.kernel_addr = offsetof (struct user_regs_struct, psw);
  if (ptrace (PTRACE_PEEKUSR_AREA, tid, (long) &parea) < 0)
    perror_with_name ("Couldn't get registers");

  supply_gregset (&regs);
}

/* Store all valid general-purpose registers in GDB's register cache
   into the process/thread specified by TID.  */
static void
store_regs (int tid, int regnum)
{
  gregset_t regs;
  ptrace_area parea;

  parea.len = sizeof (regs);
  parea.process_addr = (addr_t) &regs;
  parea.kernel_addr = offsetof (struct user_regs_struct, psw);
  if (ptrace (PTRACE_PEEKUSR_AREA, tid, (long) &parea) < 0)
    perror_with_name ("Couldn't get registers");

  fill_gregset (&regs, regnum);

  if (ptrace (PTRACE_POKEUSR_AREA, tid, (long) &parea) < 0)
    perror_with_name ("Couldn't write registers");
}

/* Fetch all floating-point registers from process/thread TID and store
   their values in GDB's register cache.  */
static void
fetch_fpregs (int tid)
{
  fpregset_t fpregs;
  ptrace_area parea;

  parea.len = sizeof (fpregs);
  parea.process_addr = (addr_t) &fpregs;
  parea.kernel_addr = offsetof (struct user_regs_struct, fp_regs);
  if (ptrace (PTRACE_PEEKUSR_AREA, tid, (long) &parea) < 0)
    perror_with_name ("Couldn't get floating point status");

  supply_fpregset (&fpregs);
}

/* Store all valid floating-point registers in GDB's register cache
   into the process/thread specified by TID.  */
static void
store_fpregs (int tid, int regnum)
{
  fpregset_t fpregs;
  ptrace_area parea;

  parea.len = sizeof (fpregs);
  parea.process_addr = (addr_t) &fpregs;
  parea.kernel_addr = offsetof (struct user_regs_struct, fp_regs);
  if (ptrace (PTRACE_PEEKUSR_AREA, tid, (long) &parea) < 0)
    perror_with_name ("Couldn't get floating point status");

  fill_fpregset (&fpregs, regnum);

  if (ptrace (PTRACE_POKEUSR_AREA, tid, (long) &parea) < 0)
    perror_with_name ("Couldn't write floating point status");
}

/* Fetch register REGNUM from the child process.  If REGNUM is -1, do
   this for all registers.  */
void
fetch_inferior_registers (int regnum)
{
  int tid = s390_inferior_tid ();

  if (regnum == -1 
      || (regnum < S390_NUM_REGS && regmap_gregset[regnum] != -1))
    fetch_regs (tid);

  if (regnum == -1 
      || (regnum < S390_NUM_REGS && regmap_fpregset[regnum] != -1))
    fetch_fpregs (tid);
}

/* Store register REGNUM back into the child process.  If REGNUM is
   -1, do this for all registers.  */
void
store_inferior_registers (int regnum)
{
  int tid = s390_inferior_tid ();

  if (regnum == -1 
      || (regnum < S390_NUM_REGS && regmap_gregset[regnum] != -1))
    store_regs (tid, regnum);

  if (regnum == -1 
      || (regnum < S390_NUM_REGS && regmap_fpregset[regnum] != -1))
    store_fpregs (tid, regnum);
}


/* Hardware-assisted watchpoint handling.  */

/* We maintain a list of all currently active watchpoints in order
   to properly handle watchpoint removal.

   The only thing we actually need is the total address space area
   spanned by the watchpoints.  */

struct watch_area
{
  struct watch_area *next;
  CORE_ADDR lo_addr;
  CORE_ADDR hi_addr;
};

static struct watch_area *watch_base = NULL;

int
s390_stopped_by_watchpoint (void)
{
  per_lowcore_bits per_lowcore;
  ptrace_area parea;

  /* Speed up common case.  */
  if (!watch_base)
    return 0;

  parea.len = sizeof (per_lowcore);
  parea.process_addr = (addr_t) & per_lowcore;
  parea.kernel_addr = offsetof (struct user_regs_struct, per_info.lowcore);
  if (ptrace (PTRACE_PEEKUSR_AREA, s390_inferior_tid (), &parea) < 0)
    perror_with_name ("Couldn't retrieve watchpoint status");

  return per_lowcore.perc_storage_alteration == 1
	 && per_lowcore.perc_store_real_address == 0;
}

static void
s390_fix_watch_points (void)
{
  int tid = s390_inferior_tid ();

  per_struct per_info;
  ptrace_area parea;

  CORE_ADDR watch_lo_addr = (CORE_ADDR)-1, watch_hi_addr = 0;
  struct watch_area *area;

  for (area = watch_base; area; area = area->next)
    {
      watch_lo_addr = min (watch_lo_addr, area->lo_addr);
      watch_hi_addr = max (watch_hi_addr, area->hi_addr);
    }

  parea.len = sizeof (per_info);
  parea.process_addr = (addr_t) & per_info;
  parea.kernel_addr = offsetof (struct user_regs_struct, per_info);
  if (ptrace (PTRACE_PEEKUSR_AREA, tid, &parea) < 0)
    perror_with_name ("Couldn't retrieve watchpoint status");

  if (watch_base)
    {
      per_info.control_regs.bits.em_storage_alteration = 1;
      per_info.control_regs.bits.storage_alt_space_ctl = 1;
    }
  else
    {
      per_info.control_regs.bits.em_storage_alteration = 0;
      per_info.control_regs.bits.storage_alt_space_ctl = 0;
    }
  per_info.starting_addr = watch_lo_addr;
  per_info.ending_addr = watch_hi_addr;

  if (ptrace (PTRACE_POKEUSR_AREA, tid, &parea) < 0)
    perror_with_name ("Couldn't modify watchpoint status");
}

int
s390_insert_watchpoint (CORE_ADDR addr, int len)
{
  struct watch_area *area = xmalloc (sizeof (struct watch_area));
  if (!area)
    return -1; 

  area->lo_addr = addr;
  area->hi_addr = addr + len - 1;
 
  area->next = watch_base;
  watch_base = area;

  s390_fix_watch_points ();
  return 0;
}

int
s390_remove_watchpoint (CORE_ADDR addr, int len)
{
  struct watch_area *area, **parea;

  for (parea = &watch_base; *parea; parea = &(*parea)->next)
    if ((*parea)->lo_addr == addr
	&& (*parea)->hi_addr == addr + len - 1)
      break;

  if (!*parea)
    {
      fprintf_unfiltered (gdb_stderr,
			  "Attempt to remove nonexistent watchpoint.\n");
      return -1;
    }

  area = *parea;
  *parea = area->next;
  xfree (area);

  s390_fix_watch_points ();
  return 0;
}


int
kernel_u_size (void)
{
  return sizeof (struct user);
}

