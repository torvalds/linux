/* <proc_service.h> implementation.

   Copyright 1999, 2000, 2002 Free Software Foundation, Inc.

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

#include "gdb_proc_service.h"
#include <sys/procfs.h>

#include "inferior.h"
#include "symtab.h"
#include "target.h"

/* Prototypes for supply_gregset etc.  */
#include "gregset.h"


/* Fix-up some broken systems.  */

/* The prototypes in <proc_service.h> are slightly different on older
   systems.  Compensate for the discrepancies.  */

#ifdef PROC_SERVICE_IS_OLD
typedef const struct ps_prochandle *gdb_ps_prochandle_t;
typedef char *gdb_ps_read_buf_t;
typedef char *gdb_ps_write_buf_t;
typedef int gdb_ps_size_t;
#else
typedef struct ps_prochandle *gdb_ps_prochandle_t;
typedef void *gdb_ps_read_buf_t;
typedef const void *gdb_ps_write_buf_t;
typedef size_t gdb_ps_size_t;
#endif


/* Building process ids.  */

#define BUILD_LWP(lwp, pid)	ptid_build (pid, lwp, 0)


/* Helper functions.  */

/* Transfer LEN bytes of memory between BUF and address ADDR in the
   process specified by PH.  If WRITE, transfer them to the process,
   else transfer them from the process.  Returns PS_OK for success,
   PS_ERR on failure.

   This is a helper function for ps_pdread, ps_pdwrite, ps_ptread and
   ps_ptwrite.  */

static ps_err_e
ps_xfer_memory (const struct ps_prochandle *ph, paddr_t addr,
		char *buf, size_t len, int write)
{
  struct cleanup *old_chain = save_inferior_ptid ();
  int ret;

  inferior_ptid = pid_to_ptid (ph->pid);

  if (write)
    ret = target_write_memory (addr, buf, len);
  else
    ret = target_read_memory (addr, buf, len);

  do_cleanups (old_chain);

  return (ret == 0 ? PS_OK : PS_ERR);
}


/* Stop the target process PH.  */

ps_err_e
ps_pstop (gdb_ps_prochandle_t ph)
{
  /* The process is always stopped when under control of GDB.  */
  return PS_OK;
}

/* Resume the target process PH.  */

ps_err_e
ps_pcontinue (gdb_ps_prochandle_t ph)
{
  /* Pretend we did successfully continue the process.  GDB will take
     care of it later on.  */
  return PS_OK;
}

/* Stop the lightweight process LWPID within the target process PH.  */

ps_err_e
ps_lstop (gdb_ps_prochandle_t ph, lwpid_t lwpid)
{
  /* All lightweight processes are stopped when under control of GDB.  */
  return PS_OK;
}

/* Resume the lightweight process (LWP) LWPID within the target
   process PH.  */

ps_err_e
ps_lcontinue (gdb_ps_prochandle_t ph, lwpid_t lwpid)
{
  /* Pretend we did successfully continue LWPID.  GDB will take care
     of it later on.  */
  return PS_OK;
}

/* Get the size of the architecture-dependent extra state registers
   for LWP LWPID within the target process PH and return it in
   *XREGSIZE.  */

ps_err_e
ps_lgetxregsize (gdb_ps_prochandle_t ph, lwpid_t lwpid, int *xregsize)
{
  /* FIXME: Not supported yet.  */
  return PS_OK;
}

/* Get the extra state registers of LWP LWPID within the target
   process PH and store them in XREGSET.  */

ps_err_e
ps_lgetxregs (gdb_ps_prochandle_t ph, lwpid_t lwpid, caddr_t xregset)
{
  /* FIXME: Not supported yet.  */
  return PS_OK;
}

/* Set the extra state registers of LWP LWPID within the target
   process PH from XREGSET.  */

ps_err_e
ps_lsetxregs (gdb_ps_prochandle_t ph, lwpid_t lwpid, caddr_t xregset)
{
  /* FIXME: Not supported yet.  */
  return PS_OK;
}

/* Log (additional) diognostic information.  */

void
ps_plog (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vfprintf_filtered (gdb_stderr, fmt, args);
}

/* Search for the symbol named NAME within the object named OBJ within
   the target process PH.  If the symbol is found the address of the
   symbol is stored in SYM_ADDR.  */

ps_err_e
ps_pglobal_lookup (gdb_ps_prochandle_t ph, const char *obj,
		   const char *name, paddr_t *sym_addr)
{
  struct minimal_symbol *ms;

  /* FIXME: kettenis/2000-09-03: What should we do with OBJ?  */
  ms = lookup_minimal_symbol (name, NULL, NULL);
  if (ms == NULL)
    return PS_NOSYM;

  *sym_addr = SYMBOL_VALUE_ADDRESS (ms);
  return PS_OK;
}

/* Read SIZE bytes from the target process PH at address ADDR and copy
   them into BUF.  */

ps_err_e
ps_pdread (gdb_ps_prochandle_t ph, paddr_t addr,
	   gdb_ps_read_buf_t buf, gdb_ps_size_t size)
{
  return ps_xfer_memory (ph, addr, buf, size, 0);
}

/* Write SIZE bytes from BUF into the target process PH at address ADDR.  */

ps_err_e
ps_pdwrite (gdb_ps_prochandle_t ph, paddr_t addr,
	    gdb_ps_write_buf_t buf, gdb_ps_size_t size)
{
  return ps_xfer_memory (ph, addr, (char *) buf, size, 1);
}

/* Read SIZE bytes from the target process PH at address ADDR and copy
   them into BUF.  */

ps_err_e
ps_ptread (gdb_ps_prochandle_t ph, paddr_t addr,
	   gdb_ps_read_buf_t buf, gdb_ps_size_t size)
{
  return ps_xfer_memory (ph, addr, buf, size, 0);
}

/* Write SIZE bytes from BUF into the target process PH at address ADDR.  */

ps_err_e
ps_ptwrite (gdb_ps_prochandle_t ph, paddr_t addr,
	    gdb_ps_write_buf_t buf, gdb_ps_size_t size)
{
  return ps_xfer_memory (ph, addr, (char *) buf, size, 1);
}

/* Get the general registers of LWP LWPID within the target process PH
   and store them in GREGSET.  */

ps_err_e
ps_lgetregs (gdb_ps_prochandle_t ph, lwpid_t lwpid, prgregset_t gregset)
{
  struct cleanup *old_chain = save_inferior_ptid ();

  inferior_ptid = BUILD_LWP (lwpid, ph->pid);

  target_fetch_registers (-1);
  fill_gregset ((gdb_gregset_t *) gregset, -1);

  do_cleanups (old_chain);
  return PS_OK;
}

/* Set the general registers of LWP LWPID within the target process PH
   from GREGSET.  */

ps_err_e
ps_lsetregs (gdb_ps_prochandle_t ph, lwpid_t lwpid, const prgregset_t gregset)
{
  struct cleanup *old_chain = save_inferior_ptid ();

  inferior_ptid = BUILD_LWP (lwpid, ph->pid);

  /* FIXME: We should really make supply_gregset const-correct.  */
  supply_gregset ((gdb_gregset_t *) gregset);
  target_store_registers (-1);

  do_cleanups (old_chain);
  return PS_OK;
}

/* Get the floating-point registers of LWP LWPID within the target
   process PH and store them in FPREGSET.  */

ps_err_e
ps_lgetfpregs (gdb_ps_prochandle_t ph, lwpid_t lwpid,
	       gdb_prfpregset_t *fpregset)
{
  struct cleanup *old_chain = save_inferior_ptid ();

  inferior_ptid = BUILD_LWP (lwpid, ph->pid);

  target_fetch_registers (-1);
  fill_fpregset ((gdb_fpregset_t *) fpregset, -1);

  do_cleanups (old_chain);
  return PS_OK;
}

/* Set the floating-point registers of LWP LWPID within the target
   process PH from FPREGSET.  */

ps_err_e
ps_lsetfpregs (gdb_ps_prochandle_t ph, lwpid_t lwpid,
	       const gdb_prfpregset_t *fpregset)
{
  struct cleanup *old_chain = save_inferior_ptid ();

  inferior_ptid = BUILD_LWP (lwpid, ph->pid);

  /* FIXME: We should really make supply_fpregset const-correct.  */
  supply_fpregset ((gdb_fpregset_t *) fpregset);
  target_store_registers (-1);

  do_cleanups (old_chain);
  return PS_OK;
}

/* Return overall process id of the target PH.  Special for GNU/Linux
   -- not used on Solaris.  */

pid_t
ps_getpid (gdb_ps_prochandle_t ph)
{
  return ph->pid;
}

void
_initialize_proc_service (void)
{
  /* This function solely exists to make sure this module is linked
     into the final binary.  */
}
