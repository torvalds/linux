/* Remote debugging interface for CPU32Bug Rom monitor for GDB, the GNU debugger.
   Copyright 1995, 1996, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.

   Written by Stu Grossman of Cygnus Support

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

static void cpu32bug_open (char *args, int from_tty);

static void
cpu32bug_supply_register (char *regname, int regnamelen, char *val, int vallen)
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
cpu32bug_regname (int index)
{
  static char *regnames[] =
  {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
    "SR", "PC"
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

static struct target_ops cpu32bug_ops;

static char *cpu32bug_inits[] =
{"\r", NULL};

static struct monitor_ops cpu32bug_cmds;

static void
init_cpu32bug_cmds (void)
{
  cpu32bug_cmds.flags = MO_CLR_BREAK_USES_ADDR;
  cpu32bug_cmds.init = cpu32bug_inits;	/* Init strings */
  cpu32bug_cmds.cont = "g\r";	/* continue command */
  cpu32bug_cmds.step = "t\r";	/* single step */
  cpu32bug_cmds.stop = NULL;	/* interrupt command */
  cpu32bug_cmds.set_break = "br %x\r";	/* set a breakpoint */
  cpu32bug_cmds.clr_break = "nobr %x\r";	/* clear a breakpoint */
  cpu32bug_cmds.clr_all_break = "nobr\r";	/* clear all breakpoints */
  cpu32bug_cmds.fill = "bf %x:%x %x;b\r";	/* fill (start count val) */
  cpu32bug_cmds.setmem.cmdb = "ms %x %02x\r";	/* setmem.cmdb (addr, value) */
  cpu32bug_cmds.setmem.cmdw = "ms %x %04x\r";	/* setmem.cmdw (addr, value) */
  cpu32bug_cmds.setmem.cmdl = "ms %x %08x\r";	/* setmem.cmdl (addr, value) */
  cpu32bug_cmds.setmem.cmdll = NULL;	/* setmem.cmdll (addr, value) */
  cpu32bug_cmds.setmem.resp_delim = NULL;	/* setreg.resp_delim */
  cpu32bug_cmds.setmem.term = NULL;	/* setreg.term */
  cpu32bug_cmds.setmem.term_cmd = NULL;		/* setreg.term_cmd */
  cpu32bug_cmds.getmem.cmdb = "md %x:%x;b\r";	/* getmem.cmdb (addr, len) */
  cpu32bug_cmds.getmem.cmdw = "md %x:%x;b\r";	/* getmem.cmdw (addr, len) */
  cpu32bug_cmds.getmem.cmdl = "md %x:%x;b\r";	/* getmem.cmdl (addr, len) */
  cpu32bug_cmds.getmem.cmdll = NULL;	/* getmem.cmdll (addr, len) */
  cpu32bug_cmds.getmem.resp_delim = " ";	/* getmem.resp_delim */
  cpu32bug_cmds.getmem.term = NULL;	/* getmem.term */
  cpu32bug_cmds.getmem.term_cmd = NULL;		/* getmem.term_cmd */
  cpu32bug_cmds.setreg.cmd = "rs %s %x\r";	/* setreg.cmd (name, value) */
  cpu32bug_cmds.setreg.resp_delim = NULL;	/* setreg.resp_delim */
  cpu32bug_cmds.setreg.term = NULL;	/* setreg.term */
  cpu32bug_cmds.setreg.term_cmd = NULL;		/* setreg.term_cmd */
  cpu32bug_cmds.getreg.cmd = "rs %s\r";		/* getreg.cmd (name) */
  cpu32bug_cmds.getreg.resp_delim = "=";	/* getreg.resp_delim */
  cpu32bug_cmds.getreg.term = NULL;	/* getreg.term */
  cpu32bug_cmds.getreg.term_cmd = NULL;		/* getreg.term_cmd */
  cpu32bug_cmds.dump_registers = "rd\r";	/* dump_registers */
  cpu32bug_cmds.register_pattern = "\\(\\w+\\) +=\\([0-9a-fA-F]+\\b\\)";	/* register_pattern */
  cpu32bug_cmds.supply_register = cpu32bug_supply_register;	/* supply_register */
  cpu32bug_cmds.load_routine = NULL;	/* load_routine (defaults to SRECs) */
  cpu32bug_cmds.load = "lo\r";	/* download command */
  cpu32bug_cmds.loadresp = "\n";	/* load response */
  cpu32bug_cmds.prompt = "CPU32Bug>";	/* monitor command prompt */
  cpu32bug_cmds.line_term = "\r";	/* end-of-line terminator */
  cpu32bug_cmds.cmd_end = NULL;	/* optional command terminator */
  cpu32bug_cmds.target = &cpu32bug_ops;		/* target operations */
  cpu32bug_cmds.stopbits = SERIAL_1_STOPBITS;	/* number of stop bits */
  cpu32bug_cmds.regnames = NULL;	/* registers names */
  cpu32bug_cmds.regname = cpu32bug_regname;
  cpu32bug_cmds.magic = MONITOR_OPS_MAGIC;	/* magic */
};				/* init_cpu32bug_cmds */

static void
cpu32bug_open (char *args, int from_tty)
{
  monitor_open (args, &cpu32bug_cmds, from_tty);
}

extern initialize_file_ftype _initialize_cpu32bug_rom; /* -Wmissing-prototypes */

void
_initialize_cpu32bug_rom (void)
{
  init_cpu32bug_cmds ();
  init_monitor_ops (&cpu32bug_ops);

  cpu32bug_ops.to_shortname = "cpu32bug";
  cpu32bug_ops.to_longname = "CPU32Bug monitor";
  cpu32bug_ops.to_doc = "Debug via the CPU32Bug monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  cpu32bug_ops.to_open = cpu32bug_open;

  add_target (&cpu32bug_ops);
}
