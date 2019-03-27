/* 68k-dependent portions of the RPC protocol
   used with a VxWorks target 

   Contributed by Wind River Systems.

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

#include <stdio.h>
#include "defs.h"

#include "vx-share/regPacket.h"
#include "frame.h"
#include "inferior.h"
#include "target.h"
#include "gdbcore.h"
#include "command.h"
#include "symtab.h"
#include "symfile.h"
#include "regcache.h"

#include "gdb_string.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

#ifdef _AIX			/* IBM claims "void *malloc()" not char * */
#define malloc bogon_malloc
#endif

#include <rpc/rpc.h>

#ifdef _AIX
#undef malloc
#endif

#include <sys/time.h>		/* UTek's <rpc/rpc.h> doesn't #incl this */
#include <netdb.h>
#include "vx-share/ptrace.h"
#include "vx-share/xdr_ptrace.h"
#include "vx-share/xdr_ld.h"
#include "vx-share/xdr_rdb.h"
#include "vx-share/dbgRpcLib.h"

/* get rid of value.h if possible */
#include <value.h>
#include <symtab.h>

/* Flag set if target has fpu */

extern int target_has_fp;

/* Generic register read/write routines in remote-vx.c.  */

extern void net_read_registers ();
extern void net_write_registers ();

/* Read a register or registers from the VxWorks target.
   REGNO is the register to read, or -1 for all; currently,
   it is ignored.  FIXME look at regno to improve efficiency.  */

void
vx_read_register (int regno)
{
  char mc68k_greg_packet[MC68K_GREG_PLEN];
  char mc68k_fpreg_packet[MC68K_FPREG_PLEN];

  /* Get general-purpose registers.  */

  net_read_registers (mc68k_greg_packet, MC68K_GREG_PLEN, PTRACE_GETREGS);

  bcopy (&mc68k_greg_packet[MC68K_R_D0], deprecated_registers,
	 16 * MC68K_GREG_SIZE);
  bcopy (&mc68k_greg_packet[MC68K_R_SR],
	 &deprecated_registers[DEPRECATED_REGISTER_BYTE (PS_REGNUM)],
	 MC68K_GREG_SIZE);
  bcopy (&mc68k_greg_packet[MC68K_R_PC],
	 &deprecated_registers[DEPRECATED_REGISTER_BYTE (PC_REGNUM)],
	 MC68K_GREG_SIZE);

  /* Get floating-point registers, if the target system has them.
     Otherwise, zero them.  */

  if (target_has_fp)
    {
      net_read_registers (mc68k_fpreg_packet, MC68K_FPREG_PLEN,
			  PTRACE_GETFPREGS);

      bcopy (&mc68k_fpreg_packet[MC68K_R_FP0],
	     &deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	     MC68K_FPREG_SIZE * 8);
      bcopy (&mc68k_fpreg_packet[MC68K_R_FPCR],
	     &deprecated_registers[DEPRECATED_REGISTER_BYTE (FPC_REGNUM)],
	     MC68K_FPREG_PLEN - (MC68K_FPREG_SIZE * 8));
    }
  else
    {
      memset (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	      0, MC68K_FPREG_SIZE * 8);
      memset (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FPC_REGNUM)],
	      0, MC68K_FPREG_PLEN - (MC68K_FPREG_SIZE * 8));
    }

  /* Mark the register cache valid.  */

  deprecated_registers_fetched ();
}

/* Store a register or registers into the VxWorks target.
   REGNO is the register to store, or -1 for all; currently,
   it is ignored.  FIXME look at regno to improve efficiency.  */

void
vx_write_register (int regno)
{
  char mc68k_greg_packet[MC68K_GREG_PLEN];
  char mc68k_fpreg_packet[MC68K_FPREG_PLEN];

  /* Store general-purpose registers.  */

  bcopy (deprecated_registers, &mc68k_greg_packet[MC68K_R_D0],
	 16 * MC68K_GREG_SIZE);
  bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (PS_REGNUM)],
	 &mc68k_greg_packet[MC68K_R_SR], MC68K_GREG_SIZE);
  bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (PC_REGNUM)],
	 &mc68k_greg_packet[MC68K_R_PC], MC68K_GREG_SIZE);

  net_write_registers (mc68k_greg_packet, MC68K_GREG_PLEN, PTRACE_SETREGS);

  /* Store floating point registers if the target has them.  */

  if (target_has_fp)
    {
      bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	     &mc68k_fpreg_packet[MC68K_R_FP0],
	     MC68K_FPREG_SIZE * 8);
      bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FPC_REGNUM)],
	     &mc68k_fpreg_packet[MC68K_R_FPCR],
	     MC68K_FPREG_PLEN - (MC68K_FPREG_SIZE * 8));

      net_write_registers (mc68k_fpreg_packet, MC68K_FPREG_PLEN,
			   PTRACE_SETFPREGS);
    }
}
