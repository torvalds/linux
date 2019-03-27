/* libthread_db helper functions for the remote server for GDB.
   Copyright 2002
   Free Software Foundation, Inc.

   Contributed by MontaVista Software.

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

#include "server.h"

/* This file is currently tied to GNU/Linux.  It should scale well to
   another libthread_db implementation, with the approriate gdbserver
   hooks, but for now this means we can use GNU/Linux's target data.  */

#include "linux-low.h"

/* Correct for all GNU/Linux targets (for quite some time).  */
#define GDB_GREGSET_T elf_gregset_t
#define GDB_FPREGSET_T elf_fpregset_t

#ifndef HAVE_ELF_FPREGSET_T
/* Make sure we have said types.  Not all platforms bring in <linux/elf.h>
   via <sys/procfs.h>.  */
#ifdef HAVE_LINUX_ELF_H   
#include <linux/elf.h>    
#endif
#endif

#include "../gdb_proc_service.h"

typedef struct ps_prochandle *gdb_ps_prochandle_t;
typedef void *gdb_ps_read_buf_t;
typedef const void *gdb_ps_write_buf_t;
typedef size_t gdb_ps_size_t;

/* FIXME redo this right */
#if 0
#ifndef HAVE_LINUX_REGSETS
#error HAVE_LINUX_REGSETS required!
#else
static struct regset_info *
gregset_info(void)
{
  int i = 0;

  while (target_regsets[i].size != -1)
    {
      if (target_regsets[i].type == GENERAL_REGS)
	break;
      i++;
    }

  return &target_regsets[i];
}

static struct regset_info *
fpregset_info(void)
{
  int i = 0;

  while (target_regsets[i].size != -1)
    {
      if (target_regsets[i].type == FP_REGS)
	break;
      i++;
    }

  return &target_regsets[i];
}
#endif
#endif

/* Search for the symbol named NAME within the object named OBJ within
   the target process PH.  If the symbol is found the address of the
   symbol is stored in SYM_ADDR.  */

ps_err_e
ps_pglobal_lookup (gdb_ps_prochandle_t ph, const char *obj,
		   const char *name, paddr_t *sym_addr)
{
  CORE_ADDR addr;

  if (look_up_one_symbol (name, &addr) == 0)
    return PS_NOSYM;

  *sym_addr = (paddr_t) (unsigned long) addr;
  return PS_OK;
}

/* Read SIZE bytes from the target process PH at address ADDR and copy
   them into BUF.  */

ps_err_e
ps_pdread (gdb_ps_prochandle_t ph, paddr_t addr,
	   gdb_ps_read_buf_t buf, gdb_ps_size_t size)
{
  read_inferior_memory (addr, buf, size);
  return PS_OK;
}

/* Write SIZE bytes from BUF into the target process PH at address ADDR.  */

ps_err_e
ps_pdwrite (gdb_ps_prochandle_t ph, paddr_t addr,
	    gdb_ps_write_buf_t buf, gdb_ps_size_t size)
{
  return write_inferior_memory (addr, buf, size);
}

/* Get the general registers of LWP LWPID within the target process PH
   and store them in GREGSET.  */

ps_err_e
ps_lgetregs (gdb_ps_prochandle_t ph, lwpid_t lwpid, prgregset_t gregset)
{
#if 0
  struct thread_info *reg_inferior, *save_inferior;
  void *regcache;

  reg_inferior = (struct thread_info *) find_inferior_id (&all_threads,
							  lwpid);
  if (reg_inferior == NULL)
    return PS_ERR;

  save_inferior = current_inferior;
  current_inferior = reg_inferior;

  regcache = new_register_cache ();
  the_target->fetch_registers (0, regcache);
  gregset_info()->fill_function (gregset, regcache);
  free_register_cache (regcache);

  current_inferior = save_inferior;
  return PS_OK;
#endif
  /* FIXME */
  return PS_ERR;
}

/* Set the general registers of LWP LWPID within the target process PH
   from GREGSET.  */

ps_err_e
ps_lsetregs (gdb_ps_prochandle_t ph, lwpid_t lwpid, const prgregset_t gregset)
{
#if 0
  struct thread_info *reg_inferior, *save_inferior;
  void *regcache;

  reg_inferior = (struct thread_info *) find_inferior_id (&all_threads, lwpid);
  if (reg_inferior == NULL)
    return PS_ERR;

  save_inferior = current_inferior;
  current_inferior = reg_inferior;

  regcache = new_register_cache ();
  gregset_info()->store_function (gregset, regcache);
  the_target->store_registers (0, regcache);
  free_register_cache (regcache);

  current_inferior = save_inferior;

  return PS_OK;
#endif
  /* FIXME */
  return PS_ERR;
}

/* Get the floating-point registers of LWP LWPID within the target
   process PH and store them in FPREGSET.  */

ps_err_e
ps_lgetfpregs (gdb_ps_prochandle_t ph, lwpid_t lwpid,
	       gdb_prfpregset_t *fpregset)
{
#if 0
  struct thread_info *reg_inferior, *save_inferior;
  void *regcache;

  reg_inferior = (struct thread_info *) find_inferior_id (&all_threads, lwpid);
  if (reg_inferior == NULL)
    return PS_ERR;

  save_inferior = current_inferior;
  current_inferior = reg_inferior;

  regcache = new_register_cache ();
  the_target->fetch_registers (0, regcache);
  fpregset_info()->fill_function (fpregset, regcache);
  free_register_cache (regcache);

  current_inferior = save_inferior;

  return PS_OK;
#endif
  /* FIXME */
  return PS_ERR;
}

/* Set the floating-point registers of LWP LWPID within the target
   process PH from FPREGSET.  */

ps_err_e
ps_lsetfpregs (gdb_ps_prochandle_t ph, lwpid_t lwpid,
	       const gdb_prfpregset_t *fpregset)
{
#if 0
  struct thread_info *reg_inferior, *save_inferior;
  void *regcache;

  reg_inferior = (struct thread_info *) find_inferior_id (&all_threads, lwpid);
  if (reg_inferior == NULL)
    return PS_ERR;

  save_inferior = current_inferior;
  current_inferior = reg_inferior;

  regcache = new_register_cache ();
  fpregset_info()->store_function (fpregset, regcache);
  the_target->store_registers (0, regcache);
  free_register_cache (regcache);

  current_inferior = save_inferior;

  return PS_OK;
#endif
  /* FIXME */
  return PS_ERR;
}

/* Return overall process id of the target PH.  Special for GNU/Linux
   -- not used on Solaris.  */

pid_t
ps_getpid (gdb_ps_prochandle_t ph)
{
  return ph->pid;
}


