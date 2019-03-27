/* Native-dependent code for FreeBSD/amd64.

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
#include "inferior.h"
#include "regcache.h"

#include "gdb_assert.h"
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <machine/reg.h>

#ifdef HAVE_SYS_PROCFS_H
#include <sys/procfs.h>
#endif

#ifndef HAVE_GREGSET_T
typedef struct reg gregset_t;
#endif

#ifndef HAVE_FPREGSET_T
typedef struct fpreg fpregset_t;
#endif

#include "gregset.h"
#include "amd64-tdep.h"
#include "amd64-nat.h"


/* Offset to the gregset_t location where REG is stored.  */
#define REG_OFFSET(reg) offsetof (gregset_t, reg)

/* At reg_offset[REGNUM] you'll find the offset to the gregset_t
   location where the GDB register REGNUM is stored.  Unsupported
   registers are marked with `-1'.  */
static int reg_offset[] =
{
  REG_OFFSET (r_rax),
  REG_OFFSET (r_rbx),
  REG_OFFSET (r_rcx),
  REG_OFFSET (r_rdx),
  REG_OFFSET (r_rsi),
  REG_OFFSET (r_rdi),
  REG_OFFSET (r_rbp),
  REG_OFFSET (r_rsp),
  REG_OFFSET (r_r8),
  REG_OFFSET (r_r9),
  REG_OFFSET (r_r10),
  REG_OFFSET (r_r11),
  REG_OFFSET (r_r12),
  REG_OFFSET (r_r13),
  REG_OFFSET (r_r14),
  REG_OFFSET (r_r15),
  REG_OFFSET (r_rip),
  REG_OFFSET (r_rflags),
  REG_OFFSET (r_cs),
  REG_OFFSET (r_ss),
  -1,
  -1,
  -1,
  -1
};


/* Mapping between the general-purpose registers in FreeBSD/amd64
   `struct reg' format and GDB's register cache layout for
   FreeBSD/i386.

   Note that most FreeBSD/amd64 registers are 64-bit, while the
   FreeBSD/i386 registers are all 32-bit, but since we're
   little-endian we get away with that.  */

/* From <machine/reg.h>.  */
static int amd64fbsd32_r_reg_offset[I386_NUM_GREGS] =
{
  14 * 8, 13 * 8,		/* %eax, %ecx */
  12 * 8, 11 * 8,		/* %edx, %ebx */
  20 * 8, 10 * 8,		/* %esp, %ebp */
  9 * 8, 8 * 8,			/* %esi, %edi */
  17 * 8, 19 * 8,		/* %eip, %eflags */
  18 * 8, 21 * 8,		/* %cs, %ss */
  -1, -1, -1, -1		/* %ds, %es, %fs, %gs */
};


/* Transfering the registers between GDB, inferiors and core files.  */

/* Fill GDB's register array with the general-purpose register values
   in *GREGSETP.  */

void
supply_gregset (gregset_t *gregsetp)
{
  amd64_supply_native_gregset (current_regcache, gregsetp, -1);
}

/* Fill register REGNUM (if it is a general-purpose register) in
   *GREGSETPS with the value in GDB's register array.  If REGNUM is -1,
   do this for all registers.  */

void
fill_gregset (gregset_t *gregsetp, int regnum)
{
  amd64_collect_native_gregset (current_regcache, gregsetp, regnum);
}

/* Fill GDB's register array with the floating-point register values
   in *FPREGSETP.  */

void
supply_fpregset (fpregset_t *fpregsetp)
{
  amd64_supply_fxsave (current_regcache, -1, fpregsetp);
}

/* Fill register REGNUM (if it is a floating-point register) in
   *FPREGSETP with the value in GDB's register array.  If REGNUM is -1,
   do this for all registers.  */

void
fill_fpregset (fpregset_t *fpregsetp, int regnum)
{
  amd64_fill_fxsave ((char *) fpregsetp, regnum);
}


/* Provide a prototype to silence -Wmissing-prototypes.  */
void _initialize_amd64fbsd_nat (void);

void
_initialize_amd64fbsd_nat (void)
{
  int offset;

  amd64_native_gregset32_reg_offset = amd64fbsd32_r_reg_offset;
  amd64_native_gregset64_reg_offset = reg_offset;

  /* To support the recognition of signal handlers, i386bsd-tdep.c
     hardcodes some constants.  Inclusion of this file means that we
     are compiling a native debugger, which means that we can use the
     system header files and sysctl(3) to get at the relevant
     information.  */

#define SC_REG_OFFSET amd64fbsd_sc_reg_offset

  /* We only check the program counter, stack pointer and frame
     pointer since these members of `struct sigcontext' are essential
     for providing backtraces.  */

#define SC_RIP_OFFSET SC_REG_OFFSET[AMD64_RIP_REGNUM]
#define SC_RSP_OFFSET SC_REG_OFFSET[AMD64_RSP_REGNUM]
#define SC_RBP_OFFSET SC_REG_OFFSET[AMD64_RBP_REGNUM]

  /* Override the default value for the offset of the program counter
     in the sigcontext structure.  */
  offset = offsetof (struct sigcontext, sc_rip);

  if (SC_RIP_OFFSET != offset)
    {
      warning ("\
offsetof (struct sigcontext, sc_rip) yields %d instead of %d.\n\
Please report this to <bug-gdb@gnu.org>.", 
	       offset, SC_RIP_OFFSET);
    }

  SC_RIP_OFFSET = offset;

  /* Likewise for the stack pointer.  */
  offset = offsetof (struct sigcontext, sc_rsp);

  if (SC_RSP_OFFSET != offset)
    {
      warning ("\
offsetof (struct sigcontext, sc_rsp) yields %d instead of %d.\n\
Please report this to <bug-gdb@gnu.org>.",
	       offset, SC_RSP_OFFSET);
    }

  SC_RSP_OFFSET = offset;

  /* And the frame pointer.  */
  offset = offsetof (struct sigcontext, sc_rbp);

  if (SC_RBP_OFFSET != offset)
    {
      warning ("\
offsetof (struct sigcontext, sc_rbp) yields %d instead of %d.\n\
Please report this to <bug-gdb@gnu.org>.",
	       offset, SC_RBP_OFFSET);
    }

  SC_RBP_OFFSET = offset;

  /* FreeBSD provides a kern.proc.sigtramp sysctl that we can use to
     locate the sigtramp.  That way we can still recognize a sigtramp
     if its location is changed in a new kernel. */
  {
    int mib[4];
    struct kinfo_sigtramp kst;
    size_t len;

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_SIGTRAMP;
    mib[3] = getpid();
    len = sizeof (kst);
    if (sysctl (mib, sizeof(mib) / sizeof(mib[0]), &kst, &len, NULL, 0) == 0)
      {
	amd64fbsd_sigtramp_start_addr = kst.ksigtramp_start;
	amd64fbsd_sigtramp_end_addr = kst.ksigtramp_end;
      }
  }
}
