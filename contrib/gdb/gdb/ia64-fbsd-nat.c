/*
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "defs.h"
#include "inferior.h"
#include "regcache.h"

#include <sys/types.h>
#include <sys/ptrace.h>
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

#define	FPREG_SUPPLIES(r)  ((r) >= IA64_FR0_REGNUM && (r) <= IA64_FR127_REGNUM)
#define	GREG_SUPPLIES(r)   (!FPREG_SUPPLIES(r))

void
fetch_inferior_registers (int regno)
{
  union {
    fpregset_t fpr;
    gregset_t r;
  } regs;

  if (regno == -1 || GREG_SUPPLIES(regno))
    {
      if (ptrace (PT_GETREGS, PIDGET(inferior_ptid),
		  (PTRACE_ARG3_TYPE)&regs.r, 0) == -1)
	perror_with_name ("Couldn't get registers");
      supply_gregset (&regs.r);
    }

  if (regno == -1 || FPREG_SUPPLIES(regno))
    {
      if (ptrace (PT_GETFPREGS, PIDGET(inferior_ptid),
		  (PTRACE_ARG3_TYPE)&regs.fpr, 0) == -1)
	perror_with_name ("Couldn't get FP registers");
      supply_fpregset (&regs.fpr);
    }
}

void
store_inferior_registers (int regno)
{
  union {
    fpregset_t fpr;
    gregset_t r;
  } regs;

  if (regno == -1 || GREG_SUPPLIES(regno))
    {
      if (ptrace (PT_GETREGS, PIDGET(inferior_ptid),
		  (PTRACE_ARG3_TYPE)&regs.r, 0) == -1)
	perror_with_name ("Couldn't get registers");
      fill_gregset (&regs.r, regno);
      if (ptrace (PT_SETREGS, PIDGET(inferior_ptid),
		  (PTRACE_ARG3_TYPE)&regs.r, 0) == -1)
	perror_with_name ("Couldn't get registers");
      if (regno != -1)
	return;
    }

  if (regno == -1 || FPREG_SUPPLIES(regno))
    {
      if (ptrace (PT_GETFPREGS, PIDGET(inferior_ptid),
		  (PTRACE_ARG3_TYPE)&regs.fpr, 0) == -1)
	perror_with_name ("Couldn't get FP registers");
      fill_fpregset (&regs.fpr, regno);
      if (ptrace (PT_SETFPREGS, PIDGET(inferior_ptid),
		  (PTRACE_ARG3_TYPE)&regs.fpr, 0) == -1)
	perror_with_name ("Couldn't get FP registers");
      if (regno != -1)
	return;
    }
}

LONGEST ia64_fbsd_xfer_dirty (struct target_ops *ops, enum target_object obj,
			      const char *annex, void *rbuf, const void *wbuf,
			      ULONGEST ofs, LONGEST len)
{
  if (len != 8)
    return (-1);
  if (rbuf != NULL) {
    if (ptrace (PT_GETKSTACK, PIDGET(inferior_ptid), (PTRACE_ARG3_TYPE)rbuf,
		ofs >> 3) == -1) {
      perror_with_name ("Couldn't read dirty register");
      return (-1);
    }
  } else {
    if (ptrace (PT_SETKSTACK, PIDGET(inferior_ptid), (PTRACE_ARG3_TYPE)wbuf,
		ofs >> 3) == -1) {
      perror_with_name ("Couldn't write dirty register");
      return (-1);
    }
  }
  return (len);
}

void
_initialize_ia64_fbsd_nat (void)
{
}
