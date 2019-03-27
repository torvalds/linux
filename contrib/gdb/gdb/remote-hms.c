/* Remote debugging interface for Renesas HMS Monitor Version 1.0
   Copyright 1995, 1996, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.  Written by Steve Chamberlain
   (sac@cygnus.com).

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

static void hms_open (char *args, int from_tty);
static void
hms_supply_register (char *regname, int regnamelen, char *val, int vallen)
{
  int regno;

  if (regnamelen != 2)
    return;
  if (regname[0] != 'P')
    return;
  /* We scan off all the registers in one go */

  val = monitor_supply_register (PC_REGNUM, val);
  /* Skip the ccr string */
  while (*val != '=' && *val)
    val++;

  val = monitor_supply_register (CCR_REGNUM, val + 1);

  /* Skip up to rest of regs */
  while (*val != '=' && *val)
    val++;

  for (regno = 0; regno < 7; regno++)
    {
      val = monitor_supply_register (regno, val + 1);
    }
}

/*
 * This array of registers needs to match the indexes used by GDB. The
 * whole reason this exists is because the various ROM monitors use
 * different names than GDB does, and don't support all the
 * registers either. So, typing "info reg sp" becomes a "r30".
 */

static char *hms_regnames[] =
{
  "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "CCR", "PC", "", "", "", ""
};

/*
 * Define the monitor command strings. Since these are passed directly
 * through to a printf style function, we need can include formatting
 * strings. We also need a CR or LF on the end.
 */

static struct target_ops hms_ops;

static char *hms_inits[] =
{"\003",			/* Resets the prompt, and clears repeated cmds */
 NULL};

static struct monitor_ops hms_cmds;

static void
init_hms_cmds (void)
{
  hms_cmds.flags = MO_CLR_BREAK_USES_ADDR | MO_FILL_USES_ADDR | MO_GETMEM_NEEDS_RANGE;
  hms_cmds.init = hms_inits;	/* Init strings */
  hms_cmds.cont = "g\r";	/* continue command */
  hms_cmds.step = "s\r";	/* single step */
  hms_cmds.stop = "\003";	/* ^C interrupts the program */
  hms_cmds.set_break = "b %x\r";	/* set a breakpoint */
  hms_cmds.clr_break = "b - %x\r";	/* clear a breakpoint */
  hms_cmds.clr_all_break = "b -\r";	/* clear all breakpoints */
  hms_cmds.fill = "f %x %x %x\r";	/* fill (start end val) */
  hms_cmds.setmem.cmdb = "m.b %x=%x\r";		/* setmem.cmdb (addr, value) */
  hms_cmds.setmem.cmdw = "m.w %x=%x\r";		/* setmem.cmdw (addr, value) */
  hms_cmds.setmem.cmdl = NULL;	/* setmem.cmdl (addr, value) */
  hms_cmds.setmem.cmdll = NULL;	/* setmem.cmdll (addr, value) */
  hms_cmds.setmem.resp_delim = NULL;	/* setreg.resp_delim */
  hms_cmds.setmem.term = NULL;	/* setreg.term */
  hms_cmds.setmem.term_cmd = NULL;	/* setreg.term_cmd */
  hms_cmds.getmem.cmdb = "m.b %x %x\r";		/* getmem.cmdb (addr, addr) */
  hms_cmds.getmem.cmdw = "m.w %x %x\r";		/* getmem.cmdw (addr, addr) */
  hms_cmds.getmem.cmdl = NULL;	/* getmem.cmdl (addr, addr) */
  hms_cmds.getmem.cmdll = NULL;	/* getmem.cmdll (addr, addr) */
  hms_cmds.getmem.resp_delim = ": ";	/* getmem.resp_delim */
  hms_cmds.getmem.term = ">";	/* getmem.term */
  hms_cmds.getmem.term_cmd = "\003";	/* getmem.term_cmd */
  hms_cmds.setreg.cmd = "r %s=%x\r";	/* setreg.cmd (name, value) */
  hms_cmds.setreg.resp_delim = NULL;	/* setreg.resp_delim */
  hms_cmds.setreg.term = NULL;	/* setreg.term */
  hms_cmds.setreg.term_cmd = NULL;	/* setreg.term_cmd */
  hms_cmds.getreg.cmd = "r %s\r";	/* getreg.cmd (name) */
  hms_cmds.getreg.resp_delim = " (";	/* getreg.resp_delim */
  hms_cmds.getreg.term = ":";	/* getreg.term */
  hms_cmds.getreg.term_cmd = "\003";	/* getreg.term_cmd */
  hms_cmds.dump_registers = "r\r";	/* dump_registers */
  hms_cmds.register_pattern = "\\(\\w+\\)=\\([0-9a-fA-F]+\\)";	/* register_pattern */
  hms_cmds.supply_register = hms_supply_register;	/* supply_register */
  hms_cmds.load_routine = NULL;	/* load_routine (defaults to SRECs) */
  hms_cmds.load = "tl\r";	/* download command */
  hms_cmds.loadresp = NULL;	/* load response */
  hms_cmds.prompt = ">";	/* monitor command prompt */
  hms_cmds.line_term = "\r";	/* end-of-command delimitor */
  hms_cmds.cmd_end = NULL;	/* optional command terminator */
  hms_cmds.target = &hms_ops;	/* target operations */
  hms_cmds.stopbits = SERIAL_1_STOPBITS;	/* number of stop bits */
  hms_cmds.regnames = hms_regnames;	/* registers names */
  hms_cmds.magic = MONITOR_OPS_MAGIC;	/* magic */
}				/* init_hms-cmds */

static void
hms_open (char *args, int from_tty)
{
  monitor_open (args, &hms_cmds, from_tty);
}

int write_dos_tick_delay;

extern initialize_file_ftype _initialize_remote_hms; /* -Wmissing-prototypes */

void
_initialize_remote_hms (void)
{
  init_hms_cmds ();
  init_monitor_ops (&hms_ops);

  hms_ops.to_shortname = "hms";
  hms_ops.to_longname = "Renesas Microsystems H8/300 debug monitor";
  hms_ops.to_doc = "Debug via the HMS monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  hms_ops.to_open = hms_open;
  /* By trial and error I've found that this delay doesn't break things */
  write_dos_tick_delay = 1;
  add_target (&hms_ops);
}
