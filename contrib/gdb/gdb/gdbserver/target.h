/* Target operations for the remote server for GDB.
   Copyright 2002, 2003, 2004
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

#ifndef TARGET_H
#define TARGET_H

/* This structure describes how to resume a particular thread (or
   all threads) based on the client's request.  If thread is -1, then
   this entry applies to all threads.  These are generally passed around
   as an array, and terminated by a thread == -1 entry.  */

struct thread_resume
{
  int thread;

  /* If non-zero, leave this thread stopped.  */
  int leave_stopped;

  /* If non-zero, we want to single-step.  */
  int step;

  /* If non-zero, send this signal when we resume.  */
  int sig;
};

struct target_ops
{
  /* Start a new process.

     PROGRAM is a path to the program to execute.
     ARGS is a standard NULL-terminated array of arguments,
     to be passed to the inferior as ``argv''.

     Returns the new PID on success, -1 on failure.  Registers the new
     process with the process list.  */

  int (*create_inferior) (char *program, char **args);

  /* Attach to a running process.

     PID is the process ID to attach to, specified by the user
     or a higher layer.  */

  int (*attach) (int pid);

  /* Kill all inferiors.  */

  void (*kill) (void);

  /* Detach from all inferiors.  */

  void (*detach) (void);

  /* Return 1 iff the thread with process ID PID is alive.  */

  int (*thread_alive) (int pid);

  /* Resume the inferior process.  */

  void (*resume) (struct thread_resume *resume_info);

  /* Wait for the inferior process to change state.

     STATUSP will be filled in with a response code to send to GDB.

     Returns the signal which caused the process to stop.  */

  unsigned char (*wait) (char *status);

  /* Fetch registers from the inferior process.

     If REGNO is -1, fetch all registers; otherwise, fetch at least REGNO.  */

  void (*fetch_registers) (int regno);

  /* Store registers to the inferior process.

     If REGNO is -1, store all registers; otherwise, store at least REGNO.  */

  void (*store_registers) (int regno);

  /* Read memory from the inferior process.  This should generally be
     called through read_inferior_memory, which handles breakpoint shadowing.

     Read LEN bytes at MEMADDR into a buffer at MYADDR.
  
     Returns 0 on success and errno on failure.  */

  int (*read_memory) (CORE_ADDR memaddr, char *myaddr, int len);

  /* Write memory to the inferior process.  This should generally be
     called through write_inferior_memory, which handles breakpoint shadowing.

     Write LEN bytes from the buffer at MYADDR to MEMADDR.

     Returns 0 on success and errno on failure.  */

  int (*write_memory) (CORE_ADDR memaddr, const char *myaddr, int len);

  /* Query GDB for the values of any symbols we're interested in.
     This function is called whenever we receive a "qSymbols::"
     query, which corresponds to every time more symbols (might)
     become available.  NULL if we aren't interested in any
     symbols.  */

  void (*look_up_symbols) (void);

  /* Send a signal to the inferior process, however is appropriate.  */
  void (*send_signal) (int);

  /* Read auxiliary vector data from the inferior process.

     Read LEN bytes at OFFSET into a buffer at MYADDR.  */

  int (*read_auxv) (CORE_ADDR offset, char *myaddr, unsigned int len);
};

extern struct target_ops *the_target;

void set_target_ops (struct target_ops *);

#define create_inferior(program, args) \
  (*the_target->create_inferior) (program, args)

#define myattach(pid) \
  (*the_target->attach) (pid)

#define kill_inferior() \
  (*the_target->kill) ()

#define detach_inferior() \
  (*the_target->detach) ()

#define mythread_alive(pid) \
  (*the_target->thread_alive) (pid)

#define fetch_inferior_registers(regno) \
  (*the_target->fetch_registers) (regno)

#define store_inferior_registers(regno) \
  (*the_target->store_registers) (regno)

unsigned char mywait (char *statusp, int connected_wait);

int read_inferior_memory (CORE_ADDR memaddr, char *myaddr, int len);

int write_inferior_memory (CORE_ADDR memaddr, const char *myaddr, int len);

void set_desired_inferior (int id);

#endif /* TARGET_H */
