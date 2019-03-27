/* MIPS-dependent portions of the RPC protocol
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
#include <rpc/rpc.h>
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
  char mips_greg_packet[MIPS_GREG_PLEN];
  char mips_fpreg_packet[MIPS_FPREG_PLEN];

  /* Get general-purpose registers.  */

  net_read_registers (mips_greg_packet, MIPS_GREG_PLEN, PTRACE_GETREGS);

  /* this code copies the registers obtained by RPC 
     stored in a structure(s) like this :

     Register(s)                Offset(s)
     gp 0-31                    0x00
     hi                 0x80
     lo                 0x84
     sr                 0x88
     pc                 0x8c

     into a stucture like this:

     0x00       GP 0-31
     0x80       SR
     0x84       LO
     0x88       HI
     0x8C       BAD             --- Not available currently
     0x90       CAUSE           --- Not available currently
     0x94       PC
     0x98       FP 0-31
     0x118      FCSR
     0x11C      FIR             --- Not available currently
     0x120      FP              --- Not available currently

     structure is 0x124 (292) bytes in length */

  /* Copy the general registers.  */

  bcopy (&mips_greg_packet[MIPS_R_GP0], &deprecated_registers[0],
	 32 * MIPS_GREG_SIZE);

  /* Copy SR, LO, HI, and PC.  */

  bcopy (&mips_greg_packet[MIPS_R_SR],
	 &deprecated_registers[DEPRECATED_REGISTER_BYTE (PS_REGNUM)], MIPS_GREG_SIZE);
  bcopy (&mips_greg_packet[MIPS_R_LO],
	 &deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->lo)], MIPS_GREG_SIZE);
  bcopy (&mips_greg_packet[MIPS_R_HI],
	 &deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->hi)], MIPS_GREG_SIZE);
  bcopy (&mips_greg_packet[MIPS_R_PC],
	 &deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->pc)], MIPS_GREG_SIZE);

  /* If the target has floating point registers, fetch them.
     Otherwise, zero the floating point register values in
     registers[] for good measure, even though we might not
     need to.  */

  if (target_has_fp)
    {
      net_read_registers (mips_fpreg_packet, MIPS_FPREG_PLEN,
			  PTRACE_GETFPREGS);

      /* Copy the floating point registers.  */

      bcopy (&mips_fpreg_packet[MIPS_R_FP0],
	     &deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	     DEPRECATED_REGISTER_RAW_SIZE (FP0_REGNUM) * 32);

      /* Copy the floating point control/status register (fpcsr).  */

      bcopy (&mips_fpreg_packet[MIPS_R_FPCSR],
	     &deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->fp_control_status)],
	     DEPRECATED_REGISTER_RAW_SIZE (mips_regnum (current_gdbarch)->fp_control_status));
    }
  else
    {
      memset (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	      0, DEPRECATED_REGISTER_RAW_SIZE (FP0_REGNUM) * 32);
      memset (&deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->fp_control_status)],
	      0, DEPRECATED_REGISTER_RAW_SIZE (mips_regnum (current_gdbarch)->fp_control_status));
    }

  /* Mark the register cache valid.  */

  deprecated_registers_fetched ();
}

/* Store a register or registers into the VxWorks target.
   REGNO is the register to store, or -1 for all; currently,
   it is ignored.  FIXME look at regno to improve efficiency.  */

vx_write_register (int regno)
{
  char mips_greg_packet[MIPS_GREG_PLEN];
  char mips_fpreg_packet[MIPS_FPREG_PLEN];

  /* Store general registers.  */

  bcopy (&deprecated_registers[0], &mips_greg_packet[MIPS_R_GP0],
	 32 * MIPS_GREG_SIZE);

  /* Copy SR, LO, HI, and PC.  */

  bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (PS_REGNUM)],
	 &mips_greg_packet[MIPS_R_SR], MIPS_GREG_SIZE);
  bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->lo)],
	 &mips_greg_packet[MIPS_R_LO], MIPS_GREG_SIZE);
  bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->hi)],
	 &mips_greg_packet[MIPS_R_HI], MIPS_GREG_SIZE);
  bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->pc)],
	 &mips_greg_packet[MIPS_R_PC], MIPS_GREG_SIZE);

  net_write_registers (mips_greg_packet, MIPS_GREG_PLEN, PTRACE_SETREGS);

  /* Store floating point registers if the target has them.  */

  if (target_has_fp)
    {
      /* Copy the floating point data registers.  */

      bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (FP0_REGNUM)],
	     &mips_fpreg_packet[MIPS_R_FP0],
	     DEPRECATED_REGISTER_RAW_SIZE (FP0_REGNUM) * 32);

      /* Copy the floating point control/status register (fpcsr).  */

      bcopy (&deprecated_registers[DEPRECATED_REGISTER_BYTE (mips_regnum (current_gdbarch)->fp_control_status)],
	     &mips_fpreg_packet[MIPS_R_FPCSR],
	     DEPRECATED_REGISTER_RAW_SIZE (mips_regnum (current_gdbarch)->fp_control_status));

      net_write_registers (mips_fpreg_packet, MIPS_FPREG_PLEN,
			   PTRACE_SETFPREGS);
    }
}
