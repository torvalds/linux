/* Intel 386 native support for System V systems (pre-SVR4).

   Copyright 1988, 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1998,
   1999, 2000, 2002 Free Software Foundation, Inc.

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

#ifdef HAVE_PTRACE_H
#include <ptrace.h>
#else
#ifdef HAVE_SYS_PTRACE_H
#include <sys/ptrace.h>
#endif
#endif

#include "frame.h"
#include "inferior.h"
#include "language.h"
#include "gdbcore.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <sys/param.h>
#include <sys/dir.h>
#include <signal.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#ifdef TARGET_HAS_HARDWARE_WATCHPOINTS
#include <sys/debugreg.h>
#endif

#include <sys/file.h>
#include "gdb_stat.h"

#ifdef HAVE_SYS_REG_H
#include <sys/reg.h>
#endif

#include "floatformat.h"

#include "target.h"

#include "i386-tdep.h"


/* Mapping between the general-purpose registers in `struct user'
   format and GDB's register array layout.  */
static int regmap[] =
{
  EAX, ECX, EDX, EBX,
  UESP, EBP, ESI, EDI,
  EIP, EFL, CS, SS,
  DS, ES, FS, GS,
};

/* Support for the user struct.  */

/* Return the address of register REGNUM.  BLOCKEND is the value of
   u.u_ar0, and points to the place where GS is stored.  */

CORE_ADDR
register_u_addr (CORE_ADDR blockend, int regnum)
{
  struct user u;
  CORE_ADDR fpstate;

  if (i386_fp_regnum_p (regnum))
    {
#ifdef KSTKSZ			/* SCO, and others?  */
      blockend += 4 * (SS + 1) - KSTKSZ;
      fpstate = blockend + ((char *) &u.u_fps.u_fpstate - (char *) &u);
      return (fpstate + 0x1c + 10 * (regnum - FP0_REGNUM));
#else
      fpstate = blockend + ((char *) &u.i387.st_space - (char *) &u);
      return (fpstate + 10 * (regnum - FP0_REGNUM));
#endif
    }

  return (blockend + 4 * regmap[regnum]);
}

/* Return the size of the user struct.  */

int
kernel_u_size (void)
{
  return (sizeof (struct user));
}

#ifdef TARGET_HAS_HARDWARE_WATCHPOINTS

#if !defined (offsetof)
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif

/* Record the value of the debug control register.  */
static int debug_control_mirror;

/* Record which address associates with which register.  */
static CORE_ADDR address_lookup[DR_LASTADDR - DR_FIRSTADDR + 1];

static int i386_insert_aligned_watchpoint (int, CORE_ADDR, CORE_ADDR, int,
					   int);

static int i386_insert_nonaligned_watchpoint (int, CORE_ADDR, CORE_ADDR, int,
					      int);

/* Insert a watchpoint.  */

int
i386_insert_watchpoint (int pid, CORE_ADDR addr, int len, int rw)
{
  return i386_insert_aligned_watchpoint (pid, addr, addr, len, rw);
}

static int
i386_insert_aligned_watchpoint (int pid, CORE_ADDR waddr, CORE_ADDR addr,
				int len, int rw)
{
  int i;
  int read_write_bits, len_bits;
  int free_debug_register;
  int register_number;

  /* Look for a free debug register.  */
  for (i = DR_FIRSTADDR; i <= DR_LASTADDR; i++)
    {
      if (address_lookup[i - DR_FIRSTADDR] == 0)
	break;
    }

  /* No more debug registers!  */
  if (i > DR_LASTADDR)
    return -1;

  read_write_bits = (rw & 1) ? DR_RW_READ : DR_RW_WRITE;

  if (len == 1)
    len_bits = DR_LEN_1;
  else if (len == 2)
    {
      if (addr % 2)
	return i386_insert_nonaligned_watchpoint (pid, waddr, addr, len, rw);
      len_bits = DR_LEN_2;
    }

  else if (len == 4)
    {
      if (addr % 4)
	return i386_insert_nonaligned_watchpoint (pid, waddr, addr, len, rw);
      len_bits = DR_LEN_4;
    }
  else
    return i386_insert_nonaligned_watchpoint (pid, waddr, addr, len, rw);

  free_debug_register = i;
  register_number = free_debug_register - DR_FIRSTADDR;
  debug_control_mirror |=
    ((read_write_bits | len_bits)
     << (DR_CONTROL_SHIFT + DR_CONTROL_SIZE * register_number));
  debug_control_mirror |=
    (1 << (DR_LOCAL_ENABLE_SHIFT + DR_ENABLE_SIZE * register_number));
  debug_control_mirror |= DR_LOCAL_SLOWDOWN;
  debug_control_mirror &= ~DR_CONTROL_RESERVED;

  ptrace (6, pid, offsetof (struct user, u_debugreg[DR_CONTROL]),
	  debug_control_mirror);
  ptrace (6, pid, offsetof (struct user, u_debugreg[free_debug_register]),
	  addr);

  /* Record where we came from.  */
  address_lookup[register_number] = addr;
  return 0;
}

static int
i386_insert_nonaligned_watchpoint (int pid, CORE_ADDR waddr, CORE_ADDR addr,
				   int len, int rw)
{
  int align;
  int size;
  int rv;

  static int size_try_array[4][4] =
  {
    { 1, 1, 1, 1 },		/* trying size one */
    { 2, 1, 2, 1 },		/* trying size two */
    { 2, 1, 2, 1 },		/* trying size three */
    { 4, 1, 2, 1 }		/* trying size four */
  };

  rv = 0;
  while (len > 0)
    {
      align = addr % 4;
      /* Four is the maximum length for 386.  */
      size = size_try_array[len > 4 ? 3 : len - 1][align];

      rv = i386_insert_aligned_watchpoint (pid, waddr, addr, size, rw);
      if (rv)
	{
	  i386_remove_watchpoint (pid, waddr, size);
	  return rv;
	}
      addr += size;
      len -= size;
    }
  return rv;
}

/* Remove a watchpoint.  */

int
i386_remove_watchpoint (int pid, CORE_ADDR addr, int len)
{
  int i;
  int register_number;

  for (i = DR_FIRSTADDR; i <= DR_LASTADDR; i++)
    {
      register_number = i - DR_FIRSTADDR;
      if (address_lookup[register_number] == addr)
	{
	  debug_control_mirror &=
	    ~(1 << (DR_LOCAL_ENABLE_SHIFT + DR_ENABLE_SIZE * register_number));
	  address_lookup[register_number] = 0;
	}
    }
  ptrace (6, pid, offsetof (struct user, u_debugreg[DR_CONTROL]),
	  debug_control_mirror);
  ptrace (6, pid, offsetof (struct user, u_debugreg[DR_STATUS]), 0);

  return 0;
}

/* Check if stopped by a watchpoint.  */

CORE_ADDR
i386_stopped_by_watchpoint (int pid)
{
  int i;
  int status;

  status = ptrace (3, pid, offsetof (struct user, u_debugreg[DR_STATUS]), 0);
  ptrace (6, pid, offsetof (struct user, u_debugreg[DR_STATUS]), 0);

  for (i = DR_FIRSTADDR; i <= DR_LASTADDR; i++)
    {
      if (status & (1 << (i - DR_FIRSTADDR)))
	return address_lookup[i - DR_FIRSTADDR];
    }

  return 0;
}

#endif /* TARGET_HAS_HARDWARE_WATCHPOINTS */
