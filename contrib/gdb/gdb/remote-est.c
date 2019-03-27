/* Remote debugging interface for EST-300 ICE, for GDB
   Copyright 1995, 1996, 1997, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.

   Written by Steve Chamberlain for Cygnus Support.
   Re-written by Stu Grossman of Cygnus Support

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

static void est_open (char *args, int from_tty);

static void
est_supply_register (char *regname, int regnamelen, char *val, int vallen)
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
 * registers either. So, typing "info reg sp" becomes a "r30".
 */

static const char *
est_regname (int index) 
{
  
  static char *regnames[] =
  {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
    "SR", "PC",
  };
  

  if ((index >= (sizeof (regnames) /  sizeof (regnames[0]))) 
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

static struct target_ops est_ops;

static char *est_inits[] =
{"he\r",			/* Resets the prompt, and clears repeated cmds */
 NULL};

static struct monitor_ops est_cmds;

static void
init_est_cmds (void)
{
  est_cmds.flags = MO_CLR_BREAK_USES_ADDR | MO_FILL_USES_ADDR | MO_NEED_REGDUMP_AFTER_CONT |
    MO_SREC_ACK | MO_SREC_ACK_PLUS;
  est_cmds.init = est_inits;	/* Init strings */
  est_cmds.cont = "go\r";	/* continue command */
  est_cmds.step = "sidr\r";	/* single step */
  est_cmds.stop = "\003";	/* ^C interrupts the program */
  est_cmds.set_break = "sb %x\r";	/* set a breakpoint */
  est_cmds.clr_break = "rb %x\r";	/* clear a breakpoint */
  est_cmds.clr_all_break = "rb\r";	/* clear all breakpoints */
  est_cmds.fill = "bfb %x %x %x\r";	/* fill (start end val) */
  est_cmds.setmem.cmdb = "smb %x %x\r";		/* setmem.cmdb (addr, value) */
  est_cmds.setmem.cmdw = "smw %x %x\r";		/* setmem.cmdw (addr, value) */
  est_cmds.setmem.cmdl = "sml %x %x\r";		/* setmem.cmdl (addr, value) */
  est_cmds.setmem.cmdll = NULL;	/* setmem.cmdll (addr, value) */
  est_cmds.setmem.resp_delim = NULL;	/* setreg.resp_delim */
  est_cmds.setmem.term = NULL;	/* setreg.term */
  est_cmds.setmem.term_cmd = NULL;	/* setreg.term_cmd */
  est_cmds.getmem.cmdb = "dmb %x %x\r";		/* getmem.cmdb (addr, len) */
  est_cmds.getmem.cmdw = "dmw %x %x\r";		/* getmem.cmdw (addr, len) */
  est_cmds.getmem.cmdl = "dml %x %x\r";		/* getmem.cmdl (addr, len) */
  est_cmds.getmem.cmdll = NULL;	/* getmem.cmdll (addr, len) */
  est_cmds.getmem.resp_delim = ": ";	/* getmem.resp_delim */
  est_cmds.getmem.term = NULL;	/* getmem.term */
  est_cmds.getmem.term_cmd = NULL;	/* getmem.term_cmd */
  est_cmds.setreg.cmd = "sr %s %x\r";	/* setreg.cmd (name, value) */
  est_cmds.setreg.resp_delim = NULL;	/* setreg.resp_delim */
  est_cmds.setreg.term = NULL;	/* setreg.term */
  est_cmds.setreg.term_cmd = NULL;	/* setreg.term_cmd */
  est_cmds.getreg.cmd = "dr %s\r";	/* getreg.cmd (name) */
  est_cmds.getreg.resp_delim = " = ";	/* getreg.resp_delim */
  est_cmds.getreg.term = NULL;	/* getreg.term */
  est_cmds.getreg.term_cmd = NULL;	/* getreg.term_cmd */
  est_cmds.dump_registers = "dr\r";	/* dump_registers */
  est_cmds.register_pattern = "\\(\\w+\\) = \\([0-9a-fA-F]+\\)";	/* register_pattern */
  est_cmds.supply_register = est_supply_register;	/* supply_register */
  est_cmds.load_routine = NULL;	/* load_routine (defaults to SRECs) */
  est_cmds.load = "dl\r";	/* download command */
  est_cmds.loadresp = "+";	/* load response */
  est_cmds.prompt = ">BKM>";	/* monitor command prompt */
  est_cmds.line_term = "\r";	/* end-of-line terminator */
  est_cmds.cmd_end = NULL;	/* optional command terminator */
  est_cmds.target = &est_ops;	/* target operations */
  est_cmds.stopbits = SERIAL_1_STOPBITS;	/* number of stop bits */
  est_cmds.regnames = NULL;
  est_cmds.regname = est_regname; /*register names*/
  est_cmds.magic = MONITOR_OPS_MAGIC;	/* magic */
}				/* init_est_cmds */

static void
est_open (char *args, int from_tty)
{
  monitor_open (args, &est_cmds, from_tty);
}

extern initialize_file_ftype _initialize_est; /* -Wmissing-prototypes */

void
_initialize_est (void)
{
  init_est_cmds ();
  init_monitor_ops (&est_ops);

  est_ops.to_shortname = "est";
  est_ops.to_longname = "EST background debug monitor";
  est_ops.to_doc = "Debug via the EST BDM.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  est_ops.to_open = est_open;

  add_target (&est_ops);
}
