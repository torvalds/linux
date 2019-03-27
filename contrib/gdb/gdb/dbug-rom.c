/* Remote debugging interface to dBUG ROM monitor for GDB, the GNU debugger.
   Copyright 1996, 1998, 1999, 2000, 2001 Free Software Foundation, Inc.

   Written by Stan Shebs of Cygnus Support.

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

/* dBUG is a monitor supplied on various Motorola boards, including
   m68k, ColdFire, and PowerPC-based designs.  The code here assumes
   the ColdFire, and (as of 9/25/96) has only been tested with a
   ColdFire IDP board.  */

#include "defs.h"
#include "gdbcore.h"
#include "target.h"
#include "monitor.h"
#include "serial.h"
#include "regcache.h"

#include "m68k-tdep.h"

static void dbug_open (char *args, int from_tty);

static void
dbug_supply_register (char *regname, int regnamelen, char *val, int vallen)
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

/* This array of registers needs to match the indexes used by GDB. The
   whole reason this exists is because the various ROM monitors use
   different names than GDB does, and don't support all the registers
   either. So, typing "info reg sp" becomes an "A7". */

static const char *
dbug_regname (int index)
{
  static char *regnames[] =
  {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
    "SR", "PC"
    /* no float registers */
  };

  if ((index >= (sizeof (regnames) / sizeof (regnames[0]))) 
      || (index < 0) || (index >= NUM_REGS))
    return NULL;
  else
    return regnames[index];

}

static struct target_ops dbug_ops;
static struct monitor_ops dbug_cmds;

static char *dbug_inits[] =
{"\r", NULL};


static void
init_dbug_cmds (void)
{
  dbug_cmds.flags = MO_CLR_BREAK_USES_ADDR | MO_GETMEM_NEEDS_RANGE | MO_FILL_USES_ADDR;
  dbug_cmds.init = dbug_inits;	/* Init strings */
  dbug_cmds.cont = "go\r";	/* continue command */
  dbug_cmds.step = "trace\r";	/* single step */
  dbug_cmds.stop = NULL;	/* interrupt command */
  dbug_cmds.set_break = "br %x\r";	/* set a breakpoint */
  dbug_cmds.clr_break = "br -r %x\r";	/* clear a breakpoint */
  dbug_cmds.clr_all_break = "br -r\r";	/* clear all breakpoints */
  dbug_cmds.fill = "bf.b %x %x %x\r";	/* fill (start end val) */
  dbug_cmds.setmem.cmdb = "mm.b %x %x\r";	/* setmem.cmdb (addr, value) */
  dbug_cmds.setmem.cmdw = "mm.w %x %x\r";	/* setmem.cmdw (addr, value) */
  dbug_cmds.setmem.cmdl = "mm.l %x %x\r";	/* setmem.cmdl (addr, value) */
  dbug_cmds.setmem.cmdll = NULL;	/* setmem.cmdll (addr, value) */
  dbug_cmds.setmem.resp_delim = NULL;	/* setmem.resp_delim */
  dbug_cmds.setmem.term = NULL;	/* setmem.term */
  dbug_cmds.setmem.term_cmd = NULL;	/* setmem.term_cmd */
  dbug_cmds.getmem.cmdb = "md.b %x %x\r";	/* getmem.cmdb (addr, addr2) */
  dbug_cmds.getmem.cmdw = "md.w %x %x\r";	/* getmem.cmdw (addr, addr2) */
  dbug_cmds.getmem.cmdl = "md.l %x %x\r";	/* getmem.cmdl (addr, addr2) */
  dbug_cmds.getmem.cmdll = NULL;	/* getmem.cmdll (addr, addr2) */
  dbug_cmds.getmem.resp_delim = ":";	/* getmem.resp_delim */
  dbug_cmds.getmem.term = NULL;	/* getmem.term */
  dbug_cmds.getmem.term_cmd = NULL;	/* getmem.term_cmd */
  dbug_cmds.setreg.cmd = "rm %s %x\r";	/* setreg.cmd (name, value) */
  dbug_cmds.setreg.resp_delim = NULL;	/* setreg.resp_delim */
  dbug_cmds.setreg.term = NULL;	/* setreg.term */
  dbug_cmds.setreg.term_cmd = NULL;	/* setreg.term_cmd */
  dbug_cmds.getreg.cmd = "rd %s\r";	/* getreg.cmd (name) */
  dbug_cmds.getreg.resp_delim = ":";	/* getreg.resp_delim */
  dbug_cmds.getreg.term = NULL;	/* getreg.term */
  dbug_cmds.getreg.term_cmd = NULL;	/* getreg.term_cmd */
  dbug_cmds.dump_registers = "rd\r";	/* dump_registers */
  dbug_cmds.register_pattern = "\\(\\w+\\) +:\\([0-9a-fA-F]+\\b\\)";	/* register_pattern */
  dbug_cmds.supply_register = dbug_supply_register;	/* supply_register */
  dbug_cmds.load_routine = NULL;	/* load_routine (defaults to SRECs) */
  dbug_cmds.load = "dl\r";	/* download command */
  dbug_cmds.loadresp = "\n";	/* load response */
  dbug_cmds.prompt = "dBUG>";	/* monitor command prompt */
  dbug_cmds.line_term = "\r";	/* end-of-line terminator */
  dbug_cmds.cmd_end = NULL;	/* optional command terminator */
  dbug_cmds.target = &dbug_ops;	/* target operations */
  dbug_cmds.stopbits = SERIAL_1_STOPBITS;	/* number of stop bits */
  dbug_cmds.regnames = NULL;	/* registers names */
  dbug_cmds.regname = dbug_regname;
  dbug_cmds.magic = MONITOR_OPS_MAGIC;	/* magic */
}				/* init_debug_ops */

static void
dbug_open (char *args, int from_tty)
{
  monitor_open (args, &dbug_cmds, from_tty);
}

extern initialize_file_ftype _initialize_dbug_rom; /* -Wmissing-prototypes */

void
_initialize_dbug_rom (void)
{
  init_dbug_cmds ();
  init_monitor_ops (&dbug_ops);

  dbug_ops.to_shortname = "dbug";
  dbug_ops.to_longname = "dBUG monitor";
  dbug_ops.to_doc = "Debug via the dBUG monitor.\n\
Specify the serial device it is connected to (e.g. /dev/ttya).";
  dbug_ops.to_open = dbug_open;

  add_target (&dbug_ops);
}
