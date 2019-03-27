/* Low level Unix child interface to ptrace, for GDB when running under Unix.
   Copyright 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996,
   1998, 1999, 2000, 2001, 2002
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
#include "frame.h"
#include "inferior.h"
#include "target.h"
#include "gdb_string.h"
#include "regcache.h"

#include "gdb_wait.h"

#include "command.h"

#ifdef USG
#include <sys/types.h>
#endif

#include <sys/param.h>
#include "gdb_dirent.h"
#include <signal.h>
#include <sys/ioctl.h>

#ifdef HAVE_PTRACE_H
#include <ptrace.h>
#else
#ifdef HAVE_SYS_PTRACE_H
#include <sys/ptrace.h>
#endif
#endif

#if !defined (PT_READ_I)
#define PT_READ_I	1	/* Read word from text space */
#endif
#if !defined (PT_READ_D)
#define	PT_READ_D	2	/* Read word from data space */
#endif
#if !defined (PT_READ_U)
#define PT_READ_U	3	/* Read word from kernel user struct */
#endif
#if !defined (PT_WRITE_I)
#define PT_WRITE_I	4	/* Write word to text space */
#endif
#if !defined (PT_WRITE_D)
#define PT_WRITE_D	5	/* Write word to data space */
#endif
#if !defined (PT_WRITE_U)
#define PT_WRITE_U	6	/* Write word to kernel user struct */
#endif
#if !defined (PT_CONTINUE)
#define PT_CONTINUE	7	/* Continue after signal */
#endif
#if !defined (PT_STEP)
#define PT_STEP		9	/* Set flag for single stepping */
#endif
#if !defined (PT_KILL)
#define PT_KILL		8	/* Send child a SIGKILL signal */
#endif

#ifndef PT_ATTACH
#define PT_ATTACH PTRACE_ATTACH
#endif
#ifndef PT_DETACH
#define PT_DETACH PTRACE_DETACH
#endif

#include "gdbcore.h"
#ifndef	NO_SYS_FILE
#include <sys/file.h>
#endif
#if 0
/* Don't think this is used anymore.  On the sequent (not sure whether it's
   dynix or ptx or both), it is included unconditionally by sys/user.h and
   not protected against multiple inclusion.  */
#include "gdb_stat.h"
#endif

#if !defined (FETCH_INFERIOR_REGISTERS)
#include <sys/user.h>		/* Probably need to poke the user structure */
#if defined (KERNEL_U_ADDR_BSD)
#include <a.out.h>		/* For struct nlist */
#endif /* KERNEL_U_ADDR_BSD.  */
#endif /* !FETCH_INFERIOR_REGISTERS */

#if !defined (CHILD_XFER_MEMORY)
static void udot_info (char *, int);
#endif

#if !defined (FETCH_INFERIOR_REGISTERS)
static void fetch_register (int);
static void store_register (int);
#endif

void _initialize_kernel_u_addr (void);
void _initialize_infptrace (void);


/* This function simply calls ptrace with the given arguments.  
   It exists so that all calls to ptrace are isolated in this 
   machine-dependent file. */
int
call_ptrace (int request, int pid, PTRACE_ARG3_TYPE addr, int data)
{
  int pt_status = 0;

#if 0
  int saved_errno;

  printf ("call_ptrace(request=%d, pid=%d, addr=0x%x, data=0x%x)",
	  request, pid, addr, data);
#endif
#if defined(PT_SETTRC)
  /* If the parent can be told to attach to us, try to do it.  */
  if (request == PT_SETTRC)
    {
      errno = 0;
#if !defined (FIVE_ARG_PTRACE)
      pt_status = ptrace (PT_SETTRC, pid, addr, data);
#else
      /* Deal with HPUX 8.0 braindamage.  We never use the
         calls which require the fifth argument.  */
      pt_status = ptrace (PT_SETTRC, pid, addr, data, 0);
#endif
      if (errno)
	perror_with_name ("ptrace");
#if 0
      printf (" = %d\n", pt_status);
#endif
      if (pt_status < 0)
	return pt_status;
      else
	return parent_attach_all (pid, addr, data);
    }
#endif

#if defined(PT_CONTIN1)
  /* On HPUX, PT_CONTIN1 is a form of continue that preserves pending
     signals.  If it's available, use it.  */
  if (request == PT_CONTINUE)
    request = PT_CONTIN1;
#endif

#if defined(PT_SINGLE1)
  /* On HPUX, PT_SINGLE1 is a form of step that preserves pending
     signals.  If it's available, use it.  */
  if (request == PT_STEP)
    request = PT_SINGLE1;
#endif

#if 0
  saved_errno = errno;
  errno = 0;
#endif
#if !defined (FIVE_ARG_PTRACE)
  pt_status = ptrace (request, pid, addr, data);
#else
  /* Deal with HPUX 8.0 braindamage.  We never use the
     calls which require the fifth argument.  */
  pt_status = ptrace (request, pid, addr, data, 0);
#endif

#if 0
  if (errno)
    printf (" [errno = %d]", errno);

  errno = saved_errno;
  printf (" = 0x%x\n", pt_status);
#endif
  return pt_status;
}


#if defined (DEBUG_PTRACE) || defined (FIVE_ARG_PTRACE)
/* For the rest of the file, use an extra level of indirection */
/* This lets us breakpoint usefully on call_ptrace. */
#define ptrace call_ptrace
#endif

/* Wait for a process to finish, possibly running a target-specific
   hook before returning.  */

int
ptrace_wait (ptid_t ptid, int *status)
{
  int wstate;

  wstate = wait (status);
  target_post_wait (pid_to_ptid (wstate), *status);
  return wstate;
}

#ifndef KILL_INFERIOR
void
kill_inferior (void)
{
  int status;
  int pid =  PIDGET (inferior_ptid);

  if (pid == 0)
    return;

  /* This once used to call "kill" to kill the inferior just in case
     the inferior was still running.  As others have noted in the past
     (kingdon) there shouldn't be any way to get here if the inferior
     is still running -- else there's a major problem elsewere in gdb
     and it needs to be fixed.

     The kill call causes problems under hpux10, so it's been removed;
     if this causes problems we'll deal with them as they arise.  */
  ptrace (PT_KILL, pid, (PTRACE_ARG3_TYPE) 0, 0);
  ptrace_wait (null_ptid, &status);
  target_mourn_inferior ();
}
#endif /* KILL_INFERIOR */

#ifndef CHILD_RESUME

/* Resume execution of the inferior process.
   If STEP is nonzero, single-step it.
   If SIGNAL is nonzero, give it that signal.  */

void
child_resume (ptid_t ptid, int step, enum target_signal signal)
{
  int pid = PIDGET (ptid);

  errno = 0;

  if (pid == -1)
    /* Resume all threads.  */
    /* I think this only gets used in the non-threaded case, where "resume
       all threads" and "resume inferior_ptid" are the same.  */
    pid = PIDGET (inferior_ptid);

  /* An address of (PTRACE_ARG3_TYPE)1 tells ptrace to continue from where
     it was.  (If GDB wanted it to start some other way, we have already
     written a new PC value to the child.)

     If this system does not support PT_STEP, a higher level function will
     have called single_step() to transmute the step request into a
     continue request (by setting breakpoints on all possible successor
     instructions), so we don't have to worry about that here.  */

  if (step)
    {
      if (SOFTWARE_SINGLE_STEP_P ())
	internal_error (__FILE__, __LINE__, "failed internal consistency check");		/* Make sure this doesn't happen. */
      else
	ptrace (PT_STEP, pid, (PTRACE_ARG3_TYPE) 1,
		target_signal_to_host (signal));
    }
  else
    ptrace (PT_CONTINUE, pid, (PTRACE_ARG3_TYPE) 1,
	    target_signal_to_host (signal));

  if (errno)
    {
      perror_with_name ("ptrace");
    }
}
#endif /* CHILD_RESUME */


#ifdef ATTACH_DETACH
/* Start debugging the process whose number is PID.  */
int
attach (int pid)
{
  errno = 0;
  ptrace (PT_ATTACH, pid, (PTRACE_ARG3_TYPE) 0, 0);
  if (errno)
    perror_with_name ("ptrace");
  attach_flag = 1;
  return pid;
}

/* Stop debugging the process whose number is PID
   and continue it with signal number SIGNAL.
   SIGNAL = 0 means just continue it.  */

void
detach (int signal)
{
  errno = 0;
  ptrace (PT_DETACH, PIDGET (inferior_ptid), (PTRACE_ARG3_TYPE) 1,
          signal);
  if (errno)
    print_sys_errmsg ("ptrace", errno);
  attach_flag = 0;
}
#endif /* ATTACH_DETACH */

/* Default the type of the ptrace transfer to int.  */
#ifndef PTRACE_XFER_TYPE
#define PTRACE_XFER_TYPE int
#endif

/* KERNEL_U_ADDR is the amount to subtract from u.u_ar0
   to get the offset in the core file of the register values.  */
#if defined (KERNEL_U_ADDR_BSD) && !defined (FETCH_INFERIOR_REGISTERS)
/* Get kernel_u_addr using BSD-style nlist().  */
CORE_ADDR kernel_u_addr;
#endif /* KERNEL_U_ADDR_BSD.  */

void
_initialize_kernel_u_addr (void)
{
#if defined (KERNEL_U_ADDR_BSD) && !defined (FETCH_INFERIOR_REGISTERS)
  struct nlist names[2];

  names[0].n_un.n_name = "_u";
  names[1].n_un.n_name = NULL;
  if (nlist ("/vmunix", names) == 0)
    kernel_u_addr = names[0].n_value;
  else
    internal_error (__FILE__, __LINE__,
		    "Unable to get kernel u area address.");
#endif /* KERNEL_U_ADDR_BSD.  */
}

#if !defined (FETCH_INFERIOR_REGISTERS)

#if !defined (offsetof)
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif

/* U_REGS_OFFSET is the offset of the registers within the u area.  */
#if !defined (U_REGS_OFFSET)
#define U_REGS_OFFSET \
  ptrace (PT_READ_U, PIDGET (inferior_ptid), \
	  (PTRACE_ARG3_TYPE) (offsetof (struct user, u_ar0)), 0) \
    - KERNEL_U_ADDR
#endif

/* Fetch one register.  */

static void
fetch_register (int regno)
{
  /* This isn't really an address.  But ptrace thinks of it as one.  */
  CORE_ADDR regaddr;
  char mess[128];		/* For messages */
  int i;
  unsigned int offset;		/* Offset of registers within the u area.  */
  char buf[MAX_REGISTER_SIZE];
  int tid;

  if (CANNOT_FETCH_REGISTER (regno))
    {
      memset (buf, '\0', DEPRECATED_REGISTER_RAW_SIZE (regno));	/* Supply zeroes */
      supply_register (regno, buf);
      return;
    }

  /* Overload thread id onto process id */
  if ((tid = TIDGET (inferior_ptid)) == 0)
    tid = PIDGET (inferior_ptid);	/* no thread id, just use process id */

  offset = U_REGS_OFFSET;

  regaddr = register_addr (regno, offset);
  for (i = 0; i < DEPRECATED_REGISTER_RAW_SIZE (regno); i += sizeof (PTRACE_XFER_TYPE))
    {
      errno = 0;
      *(PTRACE_XFER_TYPE *) & buf[i] = ptrace (PT_READ_U, tid,
					       (PTRACE_ARG3_TYPE) regaddr, 0);
      regaddr += sizeof (PTRACE_XFER_TYPE);
      if (errno != 0)
	{
	  sprintf (mess, "reading register %s (#%d)", 
		   REGISTER_NAME (regno), regno);
	  perror_with_name (mess);
	}
    }
  supply_register (regno, buf);
}


/* Fetch register values from the inferior.
   If REGNO is negative, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time). */

void
fetch_inferior_registers (int regno)
{
  if (regno >= 0)
    {
      fetch_register (regno);
    }
  else
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	{
	  fetch_register (regno);
	}
    }
}

/* Store one register. */

static void
store_register (int regno)
{
  /* This isn't really an address.  But ptrace thinks of it as one.  */
  CORE_ADDR regaddr;
  char mess[128];		/* For messages */
  int i;
  unsigned int offset;		/* Offset of registers within the u area.  */
  int tid;
  char buf[MAX_REGISTER_SIZE];

  if (CANNOT_STORE_REGISTER (regno))
    {
      return;
    }

  /* Overload thread id onto process id */
  if ((tid = TIDGET (inferior_ptid)) == 0)
    tid = PIDGET (inferior_ptid);	/* no thread id, just use process id */

  offset = U_REGS_OFFSET;

  regaddr = register_addr (regno, offset);

  /* Put the contents of regno into a local buffer */
  regcache_collect (regno, buf);

  /* Store the local buffer into the inferior a chunk at the time. */
  for (i = 0; i < DEPRECATED_REGISTER_RAW_SIZE (regno); i += sizeof (PTRACE_XFER_TYPE))
    {
      errno = 0;
      ptrace (PT_WRITE_U, tid, (PTRACE_ARG3_TYPE) regaddr,
	      *(PTRACE_XFER_TYPE *) (buf + i));
      regaddr += sizeof (PTRACE_XFER_TYPE);
      if (errno != 0)
	{
	  sprintf (mess, "writing register %s (#%d)", 
		   REGISTER_NAME (regno), regno);
	  perror_with_name (mess);
	}
    }
}

/* Store our register values back into the inferior.
   If REGNO is negative, do this for all registers.
   Otherwise, REGNO specifies which register (so we can save time).  */

void
store_inferior_registers (int regno)
{
  if (regno >= 0)
    {
      store_register (regno);
    }
  else
    {
      for (regno = 0; regno < NUM_REGS; regno++)
	{
	  store_register (regno);
	}
    }
}
#endif /* !defined (FETCH_INFERIOR_REGISTERS).  */


/* Set an upper limit on alloca.  */
#ifndef GDB_MAX_ALLOCA
#define GDB_MAX_ALLOCA 0x1000
#endif

#if !defined (CHILD_XFER_MEMORY)
/* NOTE! I tried using PTRACE_READDATA, etc., to read and write memory
   in the NEW_SUN_PTRACE case.  It ought to be straightforward.  But
   it appears that writing did not write the data that I specified.  I
   cannot understand where it got the data that it actually did write.  */

/* Copy LEN bytes to or from inferior's memory starting at MEMADDR to
   debugger memory starting at MYADDR.  Copy to inferior if WRITE is
   nonzero.  TARGET is ignored.

   Returns the length copied, which is either the LEN argument or
   zero.  This xfer function does not do partial moves, since
   child_ops doesn't allow memory operations to cross below us in the
   target stack anyway.  */

int
child_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		   struct mem_attrib *attrib, struct target_ops *target)
{
  int i;
  /* Round starting address down to longword boundary.  */
  CORE_ADDR addr = memaddr & -(CORE_ADDR) sizeof (PTRACE_XFER_TYPE);
  /* Round ending address up; get number of longwords that makes.  */
  int count = ((((memaddr + len) - addr) + sizeof (PTRACE_XFER_TYPE) - 1)
	       / sizeof (PTRACE_XFER_TYPE));
  int alloc = count * sizeof (PTRACE_XFER_TYPE);
  PTRACE_XFER_TYPE *buffer;
  struct cleanup *old_chain = NULL;

#ifdef PT_IO
  /* OpenBSD 3.1, NetBSD 1.6 and FreeBSD 5.0 have a new PT_IO request
     that promises to be much more efficient in reading and writing
     data in the traced process's address space.  */

  {
    struct ptrace_io_desc piod;

    /* NOTE: We assume that there are no distinct address spaces for
       instruction and data.  */
    piod.piod_op = write ? PIOD_WRITE_D : PIOD_READ_D;
    piod.piod_offs = (void *) memaddr;
    piod.piod_addr = myaddr;
    piod.piod_len = len;

    if (ptrace (PT_IO, PIDGET (inferior_ptid), (caddr_t) &piod, 0) == -1)
      {
	/* If the PT_IO request is somehow not supported, fallback on
           using PT_WRITE_D/PT_READ_D.  Otherwise we will return zero
           to indicate failure.  */
	if (errno != EINVAL)
	  return 0;
      }
    else
      {
	/* Return the actual number of bytes read or written.  */
	return piod.piod_len;
      }
  }
#endif

  /* Allocate buffer of that many longwords.  */
  if (len < GDB_MAX_ALLOCA)
    {
      buffer = (PTRACE_XFER_TYPE *) alloca (alloc);
    }
  else
    {
      buffer = (PTRACE_XFER_TYPE *) xmalloc (alloc);
      old_chain = make_cleanup (xfree, buffer);
    }

  if (write)
    {
      /* Fill start and end extra bytes of buffer with existing memory
         data.  */
      if (addr != memaddr || len < (int) sizeof (PTRACE_XFER_TYPE))
	{
	  /* Need part of initial word -- fetch it.  */
	  buffer[0] = ptrace (PT_READ_I, PIDGET (inferior_ptid), 
			      (PTRACE_ARG3_TYPE) addr, 0);
	}

      if (count > 1)		/* FIXME, avoid if even boundary.  */
	{
	  buffer[count - 1] =
	    ptrace (PT_READ_I, PIDGET (inferior_ptid),
		    ((PTRACE_ARG3_TYPE)
		     (addr + (count - 1) * sizeof (PTRACE_XFER_TYPE))), 0);
	}

      /* Copy data to be written over corresponding part of buffer.  */
      memcpy ((char *) buffer + (memaddr & (sizeof (PTRACE_XFER_TYPE) - 1)),
	      myaddr, len);

      /* Write the entire buffer.  */
      for (i = 0; i < count; i++, addr += sizeof (PTRACE_XFER_TYPE))
	{
	  errno = 0;
	  ptrace (PT_WRITE_D, PIDGET (inferior_ptid), 
		  (PTRACE_ARG3_TYPE) addr, buffer[i]);
	  if (errno)
	    {
	      /* Using the appropriate one (I or D) is necessary for
	         Gould NP1, at least.  */
	      errno = 0;
	      ptrace (PT_WRITE_I, PIDGET (inferior_ptid), 
		      (PTRACE_ARG3_TYPE) addr, buffer[i]);
	    }
	  if (errno)
	    return 0;
	}
#ifdef CLEAR_INSN_CACHE
      CLEAR_INSN_CACHE ();
#endif
    }
  else
    {
      /* Read all the longwords.  */
      for (i = 0; i < count; i++, addr += sizeof (PTRACE_XFER_TYPE))
	{
	  errno = 0;
	  buffer[i] = ptrace (PT_READ_I, PIDGET (inferior_ptid),
			      (PTRACE_ARG3_TYPE) addr, 0);
	  if (errno)
	    return 0;
	  QUIT;
	}

      /* Copy appropriate bytes out of the buffer.  */
      memcpy (myaddr,
	      (char *) buffer + (memaddr & (sizeof (PTRACE_XFER_TYPE) - 1)),
	      len);
    }

  if (old_chain != NULL)
    do_cleanups (old_chain);
  return len;
}


static void
udot_info (char *dummy1, int dummy2)
{
#if defined (KERNEL_U_SIZE)
  long udot_off;			/* Offset into user struct */
  int udot_val;			/* Value from user struct at udot_off */
  char mess[128];		/* For messages */
#endif

  if (!target_has_execution)
    {
      error ("The program is not being run.");
    }

#if !defined (KERNEL_U_SIZE)

  /* Adding support for this command is easy.  Typically you just add a
     routine, called "kernel_u_size" that returns the size of the user
     struct, to the appropriate *-nat.c file and then add to the native
     config file "#define KERNEL_U_SIZE kernel_u_size()" */
  error ("Don't know how large ``struct user'' is in this version of gdb.");

#else

  for (udot_off = 0; udot_off < KERNEL_U_SIZE; udot_off += sizeof (udot_val))
    {
      if ((udot_off % 24) == 0)
	{
	  if (udot_off > 0)
	    {
	      printf_filtered ("\n");
	    }
	  printf_filtered ("%s:", paddr (udot_off));
	}
      udot_val = ptrace (PT_READ_U, PIDGET (inferior_ptid), (PTRACE_ARG3_TYPE) udot_off, 0);
      if (errno != 0)
	{
	  sprintf (mess, "\nreading user struct at offset 0x%s",
		   paddr_nz (udot_off));
	  perror_with_name (mess);
	}
      /* Avoid using nonportable (?) "*" in print specs */
      printf_filtered (sizeof (int) == 4 ? " 0x%08x" : " 0x%16x", udot_val);
    }
  printf_filtered ("\n");

#endif
}
#endif /* !defined (CHILD_XFER_MEMORY).  */


void
_initialize_infptrace (void)
{
#if !defined (CHILD_XFER_MEMORY)
  add_info ("udot", udot_info,
	    "Print contents of kernel ``struct user'' for current child.");
#endif
}
