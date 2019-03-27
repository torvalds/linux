/* Remote debugging interface for ABug Rom monitor for GDB, the GNU debugger.
   Copyright 1995, 1996, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.

   Written by Rob Savoye of Cygnus Support

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
#include "regcache.h"

#include "m68k-tdep.h"

/* Prototypes for local functions. */

static void abug_open (char *args, int from_tty);

static void
abug_supply_register (char *regname, int regnamelen, char *val, int vallen)
{
  int regno;

  if (regnamelen != 2)
    return;

  switch (regname[0])
    {
    case 'S':
      if (regname[1] != 'R')
	return;
      regno = PS_REGNUM;
      break;
    case 'P':
      if (regname[1] != 'C')
	return;
      regno = PC_REGNUM;
      break;
    case 'D':
      if (regname[1] < '0' || regname[1] > '7')
	return;
      regno = regname[1] - '0' + M68K_D0_REGNUM;
      break;
    case 'A':
      if (regname[1] < '0' || regname[1] > '7')
	return;
      regno = regname[1] - '0' + M68K_A0_REGNUM;
      break;
    default:
      return;
    }

  monitor_supply_register (regno, val);
}

/*
 * This array of registers needs to match the indexes used by GDB. The
 * whole reason this exists is because the various ROM monitors use
 * different names than GDB does, and don't support all the
 * registers either. So, typing "info reg sp" becomes an "A7".
 */

static const char *
abug_regname (int index)
{
  static char *regnames[] =
  {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
    "PC",
  };

  if ((index >= (sizeof (regnames) / sizeof (regnames[0]))) 
       || (index < 0) || (index >= NUM_REGS))
    return NULL;
  else
    return regnames[index];
}

/*
 * Define the monitor command strings. Since these are passed directly
 * through to a printf style function, we need can include formatting
 * strings. We also need a CR or LF on the end.
 */

static struct target_ops abug_ops;

static char *abug_inits[] =
{"\r", NULL};

static struct monitor_ops abug_cmds;

static void
init_abug_cmds (void)
{
  abug_cmds.flags = MO_CLR_BREAK_USES_ADDR;
  abug_cmds.init = abug_inits;	/* Init strings */
  abug_cmds.cont = "g\r";	/* continue command */
  abug_cmds.step = "t\r";	/* single step */
  abug_cmds.stop = NULL;	/* interrupt command */
  abug_cmds.set_break = "br %x\r";	/* set a breakpoint */
  abug_cmds.clr_break = "nobr %x\r";	/* clear a breakpoint */
  abug_cmds.clr_all_break = "nobr\r";	/* clear all breakpoints */
  abug_cmds.fill = "bf %x:%x %x;b\r";	/* fill (start count val) */
  abug_cmds.setmem.cmdb = "ms %x %02x\r";	/* setmem.cmdb (addr, value) */
  abug_cmds.setmem.cmdw = "ms %x %04x\r";	/* setmem.cmdw (addr, value) */
  abug_cmds.setmem.cmdl = "ms %x %08x\r";	/* setmem.cmdl (addr, value) */
  abug_cmds.setmem.cmdll = NULL;	/* setmem.cmdll (addr, value) */
  abug_cmds.setmem.resp_delim = NULL;	/* setreg.resp_delim */
  abug_cmds.setmem.term = NULL;	/* setreg.term */
  abug_cmds.setmem.term_cmd = NULL;	/* setreg.term_cmd */
  abug_cmds.getmem.cmdb = "md %x:%x;b\r";	/* getmem.cmdb (addr, len) */
  abug_cmds.getmem.cmdw = "md %x:%x;b\r";	/* getmem.cmdw (addr, len) */
  abug_cmds.getmem.cmdl = "md %x:%x;b\r";	/* getmem.cmdl (addr, len) */
  abug_cmds.getmem.cmdll = NULL;	/* getmem.cmdll (addr, len) */
  abug_cmds.getmem.resp_delim = " ";	/* getmem.resp_delim */
  abug_cmds.getmem.term = NULL;	/* getmem.term */
  abug_cmds.getmem.term_cmd = NULL;	/* getmem.term_cmd */
  abug_cmds.setreg.cmd = "rm %s %x\r";	/* setreg.cmd (name, value) */
  abug_cmds.setreg.resp_delim = "=";	/* setreg.resp_delim */
  abug_cmds.setreg.term = "? ";	/* setreg.term */
  abug_cmds.setreg.term_cmd = ".\r";	/* setreg.term_cmd */
  abug_cmds.getreg.cmd = "rm %s\r";	/* getreg.cmd (name) */
  abug_cmds.getreg.resp_delim = "=";	/* getreg.resp_delim */
  abug_cmds.getreg.term = "? ";	/* getreg.term */
  abug_cmds.getreg.term_cmd = ".\r";	/* getreg.term_cmd */
  abug_cmds.dump_registers = "rd\r";	/* dump_registers */
  abug_cmds.register_pattern = "\\(\\w+\\) +=\\([0-9a-fA-F]+\\b\\)";	/* register_pattern */
  abug_cmds.supply_register = abug_supply_register;	/* supply_register */
  abug_cmds.load_routine = NULL;	/* load_routine (defaults to SRECs) */
  abug_cmds.load = "lo 0\r";	/* download command */
  abug_cmds.loadresp = "\n";	/* load response */
  abug_cmds.prompt = "135Bug>";	/* monitor command prompt */
  abug_cmds.line_term = "\r";	/* end-of-line terminator */
  abug_cmds.cmd_end = NULL;	/* optional command terminator */
  abug_cmds.target = &abug_ops;	/* target operations */
  abug_cmds.stopbits = SERIAL_1_STOPBITS;	/* number of stop bits */
  abug_cmds.regnames = NULL;	/* registers names */
  abug_cmds.regname = abug_regname;
  abug_cmds.magic = MONITOR_OPS_MAGIC;	/* magic */
};

static void
abug_open (char *args, int from_tty)
{
  monitor_open (args, &abug_cmds, from_tty);
}

extern initialize_file_ftype _initialize_abug_rom; /* -Wmissing-prototypes */

void
_initialize_abug_rom (void)
{
  init_abug_cmds ();
  init_monitor_ops (&abug_ops);

  abug_ops.to_shortname = "abug";
  abug_ops.to_longname = "ABug monitor";
  abug_ops.to_doc = "Debug via the ABug monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  abug_ops.to_open = abug_open;

  add_target (&abug_ops);
}
