/* Remote debugging interface for DINK32 (PowerPC) ROM monitor for
   GDB, the GNU debugger.
   Copyright 1997, 1999, 2000, 2001 Free Software Foundation, Inc.

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
#include "gdbcore.h"
#include "target.h"
#include "monitor.h"
#include "serial.h"
#include "symfile.h" /* For generic_load() */
#include "inferior.h" /* For write_pc() */
#include "regcache.h"

static void dink32_open (char *args, int from_tty);

static void
dink32_supply_register (char *regname, int regnamelen, char *val, int vallen)
{
  int regno = 0;

  if (regnamelen < 2 || regnamelen > 4)
    return;

  switch (regname[0])
    {
    case 'R':
      if (regname[1] < '0' || regname[1] > '9')
	return;
      if (regnamelen == 2)
	regno = regname[1] - '0';
      else if (regnamelen == 3 && regname[2] >= '0' && regname[2] <= '9')
	regno = (regname[1] - '0') * 10 + (regname[2] - '0');
      else
	return;
      break;
    case 'F':
      if (regname[1] != 'R' || regname[2] < '0' || regname[2] > '9')
	return;
      if (regnamelen == 3)
	regno = 32 + regname[2] - '0';
      else if (regnamelen == 4 && regname[3] >= '0' && regname[3] <= '9')
	regno = 32 + (regname[2] - '0') * 10 + (regname[3] - '0');
      else
	return;
      break;
    case 'I':
      if (regnamelen != 2 || regname[1] != 'P')
	return;
      regno = 64;
      break;
    case 'M':
      if (regnamelen != 3 || regname[1] != 'S' || regname[2] != 'R')
	return;
      regno = 65;
      break;
    case 'C':
      if (regnamelen != 2 || regname[1] != 'R')
	return;
      regno = 66;
      break;
    case 'S':
      if (regnamelen != 4 || regname[1] != 'P' || regname[2] != 'R')
	return;
      else if (regname[3] == '8')
	regno = 67;
      else if (regname[3] == '9')
	regno = 68;
      else if (regname[3] == '1')
	regno = 69;
      else if (regname[3] == '0')
	regno = 70;
      else
	return;
      break;
    default:
      return;
    }

  monitor_supply_register (regno, val);
}

/* This array of registers needs to match the indexes used by GDB. The
   whole reason this exists is because the various ROM monitors use
   different names than GDB does, and don't support all the registers
   either.  */

static char *dink32_regnames[] =
{
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
  "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",

  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
  "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
  "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
  "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",

  "srr0", "msr", "cr", "lr", "ctr", "xer", "xer"
};

static struct target_ops dink32_ops;

static char *dink32_inits[] =
{"\r", NULL};

static struct monitor_ops dink32_cmds;

static void
dink32_open (char *args, int from_tty)
{
  monitor_open (args, &dink32_cmds, from_tty);
}

extern initialize_file_ftype _initialize_dink32_rom; /* -Wmissing-prototypes */

void
_initialize_dink32_rom (void)
{
  dink32_cmds.flags = MO_HEX_PREFIX | MO_GETMEM_NEEDS_RANGE | MO_FILL_USES_ADDR | MO_HANDLE_NL | MO_32_REGS_PAIRED | MO_SETREG_INTERACTIVE | MO_SETMEM_INTERACTIVE | MO_GETMEM_16_BOUNDARY | MO_CLR_BREAK_1_BASED | MO_SREC_ACK | MO_SREC_ACK_ROTATE;
  dink32_cmds.init = dink32_inits;
  dink32_cmds.cont = "go +\r";
  dink32_cmds.step = "tr +\r";
  dink32_cmds.set_break = "bp 0x%x\r";
  dink32_cmds.clr_break = "bp %d\r";
#if 0				/* Would need to follow strict alignment rules.. */
  dink32_cmds.fill = "mf %x %x %x\r";
#endif
  dink32_cmds.setmem.cmdb = "mm -b %x\r";
  dink32_cmds.setmem.cmdw = "mm -w %x\r";
  dink32_cmds.setmem.cmdl = "mm %x\r";
  dink32_cmds.setmem.term = " ?  ";
  dink32_cmds.getmem.cmdb = "md %x\r";
  dink32_cmds.getmem.resp_delim = "        ";
  dink32_cmds.setreg.cmd = "rm %s\r";
  dink32_cmds.setreg.term = " ?  ";
  dink32_cmds.getreg.cmd = "rd %s\r";
  dink32_cmds.getreg.resp_delim = ": ";
  dink32_cmds.dump_registers = "rd r\r";
  dink32_cmds.register_pattern = "\\(\\w+\\) +=\\([0-9a-fA-F]+\\b\\)";
  dink32_cmds.supply_register = dink32_supply_register;
  /* S-record download, via "keyboard port".  */
  dink32_cmds.load = "dl -k\r";
  dink32_cmds.loadresp = "Set Input Port : set to Keyboard Port\r";
  dink32_cmds.prompt = "DINK32_603 >>";
  dink32_cmds.line_term = "\r";
  dink32_cmds.target = &dink32_ops;
  dink32_cmds.stopbits = SERIAL_1_STOPBITS;
  dink32_cmds.regnames = dink32_regnames;
  dink32_cmds.magic = MONITOR_OPS_MAGIC;

  init_monitor_ops (&dink32_ops);

  dink32_ops.to_shortname = "dink32";
  dink32_ops.to_longname = "DINK32 monitor";
  dink32_ops.to_doc = "Debug using the DINK32 monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  dink32_ops.to_open = dink32_open;

  add_target (&dink32_ops);
}
