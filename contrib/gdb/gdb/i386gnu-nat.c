/* Low level interface to i386 running the GNU Hurd.
   Copyright 1992, 1995, 1996, 1998, 2000, 2001
   Free Software Foundation, Inc.

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
#include "inferior.h"
#include "floatformat.h"
#include "regcache.h"

#include "gdb_assert.h"
#include <errno.h>
#include <stdio.h>

#include <mach.h>
#include <mach_error.h>
#include <mach/message.h>
#include <mach/exception.h>

#include "i386-tdep.h"

#include "gnu-nat.h"
#include "i387-tdep.h"

#ifdef HAVE_SYS_PROCFS_H
# include <sys/procfs.h>
# include "gregset.h"
#endif

/* Offset to the thread_state_t location where REG is stored.  */
#define REG_OFFSET(reg) offsetof (struct i386_thread_state, reg)

/* At REG_OFFSET[N] is the offset to the thread_state_t location where
   the GDB register N is stored.  */
static int reg_offset[] =
{
  REG_OFFSET (eax), REG_OFFSET (ecx), REG_OFFSET (edx), REG_OFFSET (ebx),
  REG_OFFSET (uesp), REG_OFFSET (ebp), REG_OFFSET (esi), REG_OFFSET (edi),
  REG_OFFSET (eip), REG_OFFSET (efl), REG_OFFSET (cs), REG_OFFSET (ss),
  REG_OFFSET (ds), REG_OFFSET (es), REG_OFFSET (fs), REG_OFFSET (gs)
};

#define REG_ADDR(state, regnum) ((char *)(state) + reg_offset[regnum])


/* Get the whole floating-point state of THREAD and record the
   values of the corresponding (pseudo) registers.  */
static void
fetch_fpregs (struct proc *thread)
{
  mach_msg_type_number_t count = i386_FLOAT_STATE_COUNT;
  struct i386_float_state state;
  error_t err;

  err = thread_get_state (thread->port, i386_FLOAT_STATE,
			  (thread_state_t) &state, &count);
  if (err)
    {
      warning ("Couldn't fetch floating-point state from %s",
	       proc_string (thread));
      return;
    }

  if (!state.initialized)
    /* The floating-point state isn't initialized.  */
    {
      int i;

      for (i = FP0_REGNUM; i <= FOP_REGNUM; i++)
	supply_register (i, NULL);

      return;
    }

  /* Supply the floating-point registers.  */
  i387_supply_fsave (current_regcache, -1, state.hw_state);
}

#ifdef HAVE_SYS_PROCFS_H
/* These two calls are used by the core-regset.c code for
   reading ELF core files.  */
void
supply_gregset (gdb_gregset_t *gregs)
{
  int i;
  for (i = 0; i < I386_NUM_GREGS; i++)
    supply_register (i, REG_ADDR (gregs, i));
}

void
supply_fpregset (gdb_fpregset_t *fpregs)
{
  i387_supply_fsave (current_regcache, -1, fpregs);
}
#endif

/* Fetch register REGNO, or all regs if REGNO is -1.  */
void
gnu_fetch_registers (int regno)
{
  struct proc *thread;

  /* Make sure we know about new threads.  */
  inf_update_procs (current_inferior);

  thread = inf_tid_to_thread (current_inferior, PIDGET (inferior_ptid));
  if (!thread)
    error ("Can't fetch registers from thread %d: No such thread",
	   PIDGET (inferior_ptid));

  if (regno < I386_NUM_GREGS || regno == -1)
    {
      thread_state_t state;

      /* This does the dirty work for us.  */
      state = proc_get_state (thread, 0);
      if (!state)
	{
	  warning ("Couldn't fetch registers from %s",
		   proc_string (thread));
	  return;
	}

      if (regno == -1)
	{
	  int i;

	  proc_debug (thread, "fetching all register");

	  for (i = 0; i < I386_NUM_GREGS; i++)
	    supply_register (i, REG_ADDR (state, i));
	  thread->fetched_regs = ~0;
	}
      else
	{
	  proc_debug (thread, "fetching register %s", REGISTER_NAME (regno));

	  supply_register (regno, REG_ADDR (state, regno));
	  thread->fetched_regs |= (1 << regno);
	}
    }

  if (regno >= I386_NUM_GREGS || regno == -1)
    {
      proc_debug (thread, "fetching floating-point registers");

      fetch_fpregs (thread);
    }
}


/* Store the whole floating-point state into THREAD using information
   from the corresponding (pseudo) registers.  */
static void
store_fpregs (struct proc *thread, int regno)
{
  mach_msg_type_number_t count = i386_FLOAT_STATE_COUNT;
  struct i386_float_state state;
  error_t err;

  err = thread_get_state (thread->port, i386_FLOAT_STATE,
			  (thread_state_t) &state, &count);
  if (err)
    {
      warning ("Couldn't fetch floating-point state from %s",
	       proc_string (thread));
      return;
    }

  /* FIXME: kettenis/2001-07-15: Is this right?  Should we somehow
     take into account DEPRECATED_REGISTER_VALID like the old code did?  */
  i387_fill_fsave (state.hw_state, regno);

  err = thread_set_state (thread->port, i386_FLOAT_STATE,
			  (thread_state_t) &state, i386_FLOAT_STATE_COUNT);
  if (err)
    {
      warning ("Couldn't store floating-point state into %s",
	       proc_string (thread));
      return;
    }
}

/* Store at least register REGNO, or all regs if REGNO == -1.  */
void
gnu_store_registers (int regno)
{
  struct proc *thread;

  /* Make sure we know about new threads.  */
  inf_update_procs (current_inferior);

  thread = inf_tid_to_thread (current_inferior, PIDGET (inferior_ptid));
  if (!thread)
    error ("Couldn't store registers into thread %d: No such thread",
	   PIDGET (inferior_ptid));

  if (regno < I386_NUM_GREGS || regno == -1)
    {
      thread_state_t state;
      thread_state_data_t old_state;
      int was_aborted = thread->aborted;
      int was_valid = thread->state_valid;
      int trace;

      if (!was_aborted && was_valid)
	memcpy (&old_state, &thread->state, sizeof (old_state));

      state = proc_get_state (thread, 1);
      if (!state)
	{
	  warning ("Couldn't store registers into %s", proc_string (thread));
	  return;
	}

      /* Save the T bit.  We might try to restore the %eflags register
         below, but changing the T bit would seriously confuse GDB.  */
      trace = ((struct i386_thread_state *)state)->efl & 0x100;

      if (!was_aborted && was_valid)
	/* See which registers have changed after aborting the thread.  */
	{
	  int check_regno;

	  for (check_regno = 0; check_regno < I386_NUM_GREGS; check_regno++)
	    if ((thread->fetched_regs & (1 << check_regno))
		&& memcpy (REG_ADDR (&old_state, check_regno),
			   REG_ADDR (state, check_regno),
			   DEPRECATED_REGISTER_RAW_SIZE (check_regno)))
	      /* Register CHECK_REGNO has changed!  Ack!  */
	      {
		warning ("Register %s changed after the thread was aborted",
			 REGISTER_NAME (check_regno));
		if (regno >= 0 && regno != check_regno)
		  /* Update GDB's copy of the register.  */
		  supply_register (check_regno, REG_ADDR (state, check_regno));
		else
		  warning ("... also writing this register!  Suspicious...");
	      }
	}

#define fill(state, regno)                                               \
  memcpy (REG_ADDR(state, regno), &deprecated_registers[DEPRECATED_REGISTER_BYTE (regno)],     \
          DEPRECATED_REGISTER_RAW_SIZE (regno))

      if (regno == -1)
	{
	  int i;

	  proc_debug (thread, "storing all registers");

	  for (i = 0; i < I386_NUM_GREGS; i++)
	    if (deprecated_register_valid[i])
	      fill (state, i);
	}
      else
	{
	  proc_debug (thread, "storing register %s", REGISTER_NAME (regno));

	  gdb_assert (deprecated_register_valid[regno]);
	  fill (state, regno);
	}

      /* Restore the T bit.  */
      ((struct i386_thread_state *)state)->efl &= ~0x100;
      ((struct i386_thread_state *)state)->efl |= trace;
    }

#undef fill

  if (regno >= I386_NUM_GREGS || regno == -1)
    {
      proc_debug (thread, "storing floating-point registers");

      store_fpregs (thread, regno);
    }
}
