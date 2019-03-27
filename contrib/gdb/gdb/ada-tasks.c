/* file ada-tasks.c: Ada tasking control for GDB
   Copyright 1997 Free Software Foundation, Inc.
   Contributed by Ada Core Technologies, Inc
.
   This file is part of GDB.

   [$Id: ada-tasks.c,v 1.7 2003/06/17 20:58:32 ciceron Exp $]
   Authors: Roch-Alexandre Nomine Beguin, Arnaud Charlet <charlet@gnat.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

*/

#include <ctype.h>
#include "defs.h"
#include "command.h"
#include "value.h"
#include "language.h"
#include "inferior.h"
#include "symtab.h"
#include "target.h"
#include "regcache.h"
#include "gdbcore.h"

#if (defined(__alpha__) && defined(__osf__) && !defined(__alpha_vxworks))
#include <sys/procfs.h>
#endif

#if (defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET))
#include "gregset.h"
#endif

#include "ada-lang.h"

/* FIXME: move all this conditional compilation in description
   files or in configure.in */

#if defined (VXWORKS_TARGET)
#define THREAD_TO_PID(tid,lwpid) (tid)

#elif defined (linux)
#define THREAD_TO_PID(tid,lwpid) (0)

#elif (defined (sun) && defined (__SVR4))
#define THREAD_TO_PID thread_to_pid

#elif defined (sgi) || defined (__WIN32__) || defined (hpux)
#define THREAD_TO_PID(tid,lwpid) ((int)lwpid)

#else
#define THREAD_TO_PID(tid,lwpid) (0)
#endif

#if defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET)
#define THREAD_FETCH_REGISTERS dec_thread_fetch_registers
#define GET_CURRENT_THREAD dec_thread_get_current_thread
extern int dec_thread_get_registers (gdb_gregset_t *, gdb_fpregset_t *);
#endif

#if defined (_AIX)
#define THREAD_FETCH_REGISTERS aix_thread_fetch_registers
#define GET_CURRENT_THREAD aix_thread_get_current_thread
#endif

#if defined(VXWORKS_TARGET)
#define GET_CURRENT_THREAD() ((void*)inferior_pid)
#define THREAD_FETCH_REGISTERS() (-1)

#elif defined (sun) && defined (__SVR4)
#define GET_CURRENT_THREAD solaris_thread_get_current_thread
#define THREAD_FETCH_REGISTERS() (-1)
extern void *GET_CURRENT_THREAD ();

#elif defined (_AIX) || (defined(__alpha__) && defined(__osf__))
extern void *GET_CURRENT_THREAD ();

#elif defined (__WIN32__) || defined (hpux)
#define GET_CURRENT_THREAD() (inferior_pid)
#define THREAD_FETCH_REGISTERS() (-1)

#else
#define GET_CURRENT_THREAD() (NULL)
#define THREAD_FETCH_REGISTERS() (-1)
#endif

#define KNOWN_TASKS_NAME "system__tasking__debug__known_tasks"

#define READ_MEMORY(addr, var) read_memory (addr, (char*) &var, sizeof (var))
/* external declarations */

/* Global visible variables */

struct task_entry *task_list = NULL;
int ada__tasks_check_symbol_table = 1;
void *pthread_kern_addr = NULL;

#if (defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET))
gdb_gregset_t gregset_saved;
gdb_fpregset_t fpregset_saved;
#endif

/* The maximum number of tasks known to the Ada runtime */
const int MAX_NUMBER_OF_KNOWN_TASKS = 1000;

/* the current task */
int current_task = -1, current_task_id = -1, current_task_index;
void *current_thread, *current_lwp;

char *ada_task_states[] = {
  "Unactivated",
  "Runnable",
  "Terminated",
  "Child Activation Wait",
  "Accept Statement",
  "Waiting on entry call",
  "Async Select Wait",
  "Delay Sleep",
  "Child Termination Wait",
  "Wait Child in Term Alt",
  "",
  "",
  "",
  "",
  "Asynchronous Hold"
};

/* Global internal types */

static char *ada_long_task_states[] = {
  "Unactivated",
  "Runnable",
  "Terminated",
  "Waiting for child activation",
  "Blocked in accept statement",
  "Waiting on entry call",
  "Asynchronous Selective Wait",
  "Delay Sleep",
  "Waiting for children termination",
  "Waiting for children in terminate alternative",
  "",
  "",
  "",
  "",
  "Asynchronous Hold"
};

/* Global internal variables */

static int highest_task_num = 0;
int thread_support = 0;		/* 1 if the thread library in use is supported */
static int gdbtk_task_initialization = 0;

static int
add_task_entry (void *p_task_id, int index)
{
  struct task_entry *new_task_entry = NULL;
  struct task_entry *pt;

  highest_task_num++;
  new_task_entry = xmalloc (sizeof (struct task_entry));
  new_task_entry->task_num = highest_task_num;
  new_task_entry->task_id = p_task_id;
  new_task_entry->known_tasks_index = index;
  new_task_entry->next_task = NULL;
  pt = task_list;
  if (pt)
    {
      while (pt->next_task)
	pt = pt->next_task;
      pt->next_task = new_task_entry;
      pt->stack_per = 0;
    }
  else
    task_list = new_task_entry;
  return new_task_entry->task_num;
}

int
get_entry_number (void *p_task_id)
{
  struct task_entry *pt;

  pt = task_list;
  while (pt != NULL)
    {
      if (pt->task_id == p_task_id)
	return pt->task_num;
      pt = pt->next_task;
    }
  return 0;
}

static struct task_entry *
get_thread_entry_vptr (void *thread)
{
  struct task_entry *pt;

  pt = task_list;
  while (pt != NULL)
    {
      if (pt->thread == thread)
	return pt;
      pt = pt->next_task;
    }
  return 0;
}

static struct task_entry *
get_entry_vptr (int p_task_num)
{
  struct task_entry *pt;

  pt = task_list;
  while (pt)
    {
      if (pt->task_num == p_task_num)
	return pt;
      pt = pt->next_task;
    }
  return NULL;
}

void
init_task_list (void)
{
  struct task_entry *pt, *old_pt;

  pt = task_list;
  while (pt)
    {
      old_pt = pt;
      pt = pt->next_task;
      xfree (old_pt);
    };
  task_list = NULL;
  highest_task_num = 0;
}

int
valid_task_id (int task)
{
  return get_entry_vptr (task) != NULL;
}

void *
get_self_id (void)
{
  struct value *val;
  void *self_id;
  int result;
  struct task_entry *ent;
  extern int do_not_insert_breakpoints;

#if !((defined(sun) && defined(__SVR4)) || defined(VXWORKS_TARGET) || defined(__WIN32__))
  if (thread_support)
#endif
    {
      ent = get_thread_entry_vptr (GET_CURRENT_THREAD ());
      return ent ? ent->task_id : 0;
    }

  /* FIXME: calling a function in the inferior with a multithreaded application
     is not reliable, so return NULL if there is no safe way to get the current
     task */
  return NULL;
}

int
get_current_task (void)
{
  int result;

  /* FIXME: language_ada should be defined in defs.h */
  /*  if (current_language->la_language != language_ada) return -1; */

  result = get_entry_number (get_self_id ());

  /* return -1 if not found */
  return result == 0 ? -1 : result;
}

/* Print detailed information about specified task */

static void
info_task (char *arg, int from_tty)
{
  void *temp_task;
  struct task_entry *pt, *pt2;
  void *self_id, *caller;
  struct task_fields atcb, atcb2;
  struct entry_call call;
  int bounds[2];
  char image[256];
  int num;

  /* FIXME: language_ada should be defined in defs.h */
  /*  if (current_language->la_language != language_ada) 
     { 
     printf_filtered ("The current language does not support tasks.\n"); 
     return; 
     } 
   */
  pt = get_entry_vptr (atoi (arg));
  if (pt == NULL)
    {
      printf_filtered ("Task %s not found.\n", arg);
      return;
    }

  temp_task = pt->task_id;

  /* read the atcb in the inferior */
  READ_MEMORY ((CORE_ADDR) temp_task, atcb);

  /* print the Ada task id */
  printf_filtered ("Ada Task: %p\n", temp_task);

  /* print the name of the task */
  if (atcb.image.P_ARRAY != NULL)
    {
      READ_MEMORY ((CORE_ADDR) EXTRACT_ADDRESS (atcb.image.P_BOUNDS), bounds);
      bounds[1] = EXTRACT_INT (bounds[1]);
      read_memory ((CORE_ADDR) EXTRACT_ADDRESS (atcb.image.P_ARRAY),
		   (char *) &image, bounds[1]);
      printf_filtered ("Name: %.*s\n", bounds[1], image);
    }
  else
    printf_filtered ("<no name>\n");

  /* print the thread id */

  if ((long) pt->thread < 65536)
    printf_filtered ("Thread: %ld\n", (long int) pt->thread);
  else
    printf_filtered ("Thread: %p\n", pt->thread);

  if ((long) pt->lwp != 0)
    {
      if ((long) pt->lwp < 65536)
	printf_filtered ("LWP: %ld\n", (long int) pt->lwp);
      else
	printf_filtered ("LWP: %p\n", pt->lwp);
    }

  /* print the parent gdb task id */
  num = get_entry_number (EXTRACT_ADDRESS (atcb.parent));
  if (num != 0)
    {
      printf_filtered ("Parent: %d", num);
      pt2 = get_entry_vptr (num);
      READ_MEMORY ((CORE_ADDR) pt2->task_id, atcb2);

      /* print the name of the task */
      if (atcb2.image.P_ARRAY != NULL)
	{
	  READ_MEMORY ((CORE_ADDR) EXTRACT_ADDRESS (atcb2.image.P_BOUNDS),
		       bounds);
	  bounds[1] = EXTRACT_INT (bounds[1]);
	  read_memory ((CORE_ADDR) EXTRACT_ADDRESS (atcb2.image.P_ARRAY),
		       (char *) &image, bounds[1]);
	  printf_filtered (" (%.*s)\n", bounds[1], image);
	}
      else
	printf_filtered ("\n");
    }
  else
    printf_filtered ("No parent\n");

  /* print the base priority of the task */
  printf_filtered ("Base Priority: %d\n", EXTRACT_INT (atcb.priority));

  /* print the current state of the task */

  /* check if this task is accepting a rendezvous */
  if (atcb.call == NULL)
    caller = NULL;
  else
    {
      READ_MEMORY ((CORE_ADDR) EXTRACT_ADDRESS (atcb.call), call);
      caller = EXTRACT_ADDRESS (call.self);
    }

  if (caller != NULL)
    {
      num = get_entry_number (caller);
      printf_filtered ("Accepting rendezvous with %d", num);

      if (num != 0)
	{
	  pt2 = get_entry_vptr (num);
	  READ_MEMORY ((CORE_ADDR) pt2->task_id, atcb2);

	  /* print the name of the task */
	  if (atcb2.image.P_ARRAY != NULL)
	    {
	      READ_MEMORY ((CORE_ADDR) EXTRACT_ADDRESS (atcb2.image.P_BOUNDS),
			   bounds);
	      bounds[1] = EXTRACT_INT (bounds[1]);
	      read_memory ((CORE_ADDR) EXTRACT_ADDRESS (atcb2.image.P_ARRAY),
			   (char *) &image, bounds[1]);
	      printf_filtered (" (%.*s)\n", bounds[1], image);
	    }
	  else
	    printf_filtered ("\n");
	}
      else
	printf_filtered ("\n");
    }
  else
    printf_filtered ("State: %s\n", ada_long_task_states[atcb.state]);
}

#if 0

/* A useful function that shows the alignment of all the fields in the
   tasks_fields structure
 */

print_align (void)
{
  struct task_fields tf;
  void *tf_base = &(tf);
  void *tf_state = &(tf.state);
  void *tf_entry_num = &(tf.entry_num);
  void *tf_parent = &(tf.parent);
  void *tf_priority = &(tf.priority);
  void *tf_current_priority = &(tf.current_priority);
  void *tf_image = &(tf.image);
  void *tf_call = &(tf.call);
  void *tf_thread = &(tf.thread);
  void *tf_lwp = &(tf.lwp);
  printf_filtered ("\n");
  printf_filtered ("(tf_base = 0x%x)\n", tf_base);
  printf_filtered ("task_fields.entry_num        at %3d (0x%x)\n",
		   tf_entry_num - tf_base, tf_entry_num);
  printf_filtered ("task_fields.state            at %3d (0x%x)\n",
		   tf_state - tf_base, tf_state);
  printf_filtered ("task_fields.parent           at %3d (0x%x)\n",
		   tf_parent - tf_base, tf_parent);
  printf_filtered ("task_fields.priority         at %3d (0x%x)\n",
		   tf_priority - tf_base, tf_priority);
  printf_filtered ("task_fields.current_priority at %3d (0x%x)\n",
		   tf_current_priority - tf_base, tf_current_priority);
  printf_filtered ("task_fields.image            at %3d (0x%x)\n",
		   tf_image - tf_base, tf_image);
  printf_filtered ("task_fields.call             at %3d (0x%x)\n",
		   tf_call - tf_base, tf_call);
  printf_filtered ("task_fields.thread           at %3d (0x%x)\n",
		   tf_thread - tf_base, tf_thread);
  printf_filtered ("task_fields.lwp              at %3d (0x%x)\n",
		   tf_lwp - tf_base, tf_lwp);
  printf_filtered ("\n");
}
#endif

/* Print information about currently known tasks */

static void
info_tasks (char *arg, int from_tty)
{
  struct value *val;
  int i, task_number, state;
  void *temp_task, *temp_tasks[MAX_NUMBER_OF_KNOWN_TASKS];
  struct task_entry *pt;
  void *self_id, *caller, *thread_id = NULL;
  struct task_fields atcb;
  struct entry_call call;
  int bounds[2];
  char image[256];
  int size;
  char car;

#if defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET)
  pthreadTeb_t thr;
  gdb_gregset_t regs;
#endif

  static struct symbol *sym;
  static struct minimal_symbol *msym;
  static void *known_tasks_addr = NULL;

  int init_only = gdbtk_task_initialization;
  gdbtk_task_initialization = 0;

  task_number = 0;

  if (PIDGET (inferior_ptid) == 0)
    {
      printf_filtered ("The program is not being run under gdb. ");
      printf_filtered ("Use 'run' or 'attach' first.\n");
      return;
    }

  if (ada__tasks_check_symbol_table)
    {
      thread_support = 0;
#if (defined(__alpha__) && defined(__osf__) & !defined(VXWORKS_TARGET)) || \
    defined (_AIX)
      thread_support = 1;
#endif

      msym = lookup_minimal_symbol (KNOWN_TASKS_NAME, NULL, NULL);
      if (msym != NULL)
	known_tasks_addr = (void *) SYMBOL_VALUE_ADDRESS (msym);
      else
#ifndef VXWORKS_TARGET
	return;
#else
	{
	  if (target_lookup_symbol (KNOWN_TASKS_NAME, &known_tasks_addr) != 0)
	    return;
	}
#endif

      ada__tasks_check_symbol_table = 0;
    }

  if (known_tasks_addr == NULL)
    return;

#if !((defined(sun) && defined(__SVR4)) || defined(VXWORKS_TARGET) || defined(__WIN32__) || defined (hpux))
  if (thread_support)
#endif
    thread_id = GET_CURRENT_THREAD ();

  /* then we get a list of tasks created */

  init_task_list ();

  READ_MEMORY ((CORE_ADDR) known_tasks_addr, temp_tasks);

  for (i = 0; i < MAX_NUMBER_OF_KNOWN_TASKS; i++)
    {
      temp_task = EXTRACT_ADDRESS (temp_tasks[i]);

      if (temp_task != NULL)
	{
	  task_number = get_entry_number (temp_task);
	  if (task_number == 0)
	    task_number = add_task_entry (temp_task, i);
	}
    }

  /* Return without printing anything if this function was called in
     order to init GDBTK tasking. */

  if (init_only)
    return;

  /* print the header */

#if defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET)
  printf_filtered
    ("  ID       TID P-ID Pri Stack  %% State                  Name\n");
#else
  printf_filtered ("  ID       TID P-ID Pri State                  Name\n");
#endif

  /* Now that we have a list of task id's, we can print them */
  pt = task_list;
  while (pt)
    {
      temp_task = pt->task_id;

      /* read the atcb in the inferior */
      READ_MEMORY ((CORE_ADDR) temp_task, atcb);

      /* store the thread id for future use */
      pt->thread = EXTRACT_ADDRESS (atcb.thread);

#if defined (linux)
      pt->lwp = (void *) THREAD_TO_PID (atcb.thread, 0);
#else
      pt->lwp = EXTRACT_ADDRESS (atcb.lwp);
#endif

      /* print a star if this task is the current one */
      if (thread_id)
#if defined (__WIN32__) || defined (SGI) || defined (hpux)
	printf_filtered (pt->lwp == thread_id ? "*" : " ");
#else
	printf_filtered (pt->thread == thread_id ? "*" : " ");
#endif

      /* print the gdb task id */
      printf_filtered ("%3d", pt->task_num);

      /* print the Ada task id */
#ifndef VXWORKS_TARGET
      printf_filtered (" %9lx", (long) temp_task);
#else
#ifdef TARGET_64
      printf_filtered (" %#9lx", (unsigned long) pt->thread & 0x3ffffffffff);
#else
      printf_filtered (" %#9lx", (long) pt->thread);
#endif
#endif

      /* print the parent gdb task id */
      printf_filtered
	(" %4d", get_entry_number (EXTRACT_ADDRESS (atcb.parent)));

      /* print the base priority of the task */
      printf_filtered (" %3d", EXTRACT_INT (atcb.priority));

#if defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET)
      if (pt->task_num == 1 || atcb.state == Terminated)
	{
	  printf_filtered ("  Unknown");
	  goto next;
	}

      read_memory ((CORE_ADDR) atcb.thread, &thr, sizeof (thr));
      current_thread = atcb.thread;
      regs.regs[SP_REGNUM] = 0;
      if (dec_thread_get_registers (&regs, NULL) == 0)
	{
	  pt->stack_per = (100 * ((long) thr.__stack_base -
				  regs.regs[SP_REGNUM])) / thr.__stack_size;
	  /* if the thread is terminated but still there, the
	     stack_base/size values are erroneous. Try to patch it */
	  if (pt->stack_per < 0 || pt->stack_per > 100)
	    pt->stack_per = 0;
	}

      /* print information about stack space used in the thread */
      if (thr.__stack_size < 1024 * 1024)
	{
	  size = thr.__stack_size / 1024;
	  car = 'K';
	}
      else if (thr.__stack_size < 1024 * 1024 * 1024)
	{
	  size = thr.__stack_size / 1024 / 1024;
	  car = 'M';
	}
      else			/* Who knows... */
	{
	  size = thr.__stack_size / 1024 / 1024 / 1024;
	  car = 'G';
	}
      printf_filtered (" %4d%c %2d", size, car, pt->stack_per);
    next:
#endif

      /* print the current state of the task */

      /* check if this task is accepting a rendezvous */
      if (atcb.call == NULL)
	caller = NULL;
      else
	{
	  READ_MEMORY ((CORE_ADDR) EXTRACT_ADDRESS (atcb.call), call);
	  caller = EXTRACT_ADDRESS (call.self);
	}

      if (caller != NULL)
	printf_filtered (" Accepting RV with %-4d",
			 get_entry_number (caller));
      else
	{
	  state = atcb.state;
#if defined (__WIN32__) || defined (SGI) || defined (hpux)
	  if (state == Runnable && (thread_id && pt->lwp == thread_id))
#else
	  if (state == Runnable && (thread_id && pt->thread == thread_id))
#endif
	    /* Replace "Runnable" by "Running" if this is the current task */
	    printf_filtered (" %-22s", "Running");
	  else
	    printf_filtered (" %-22s", ada_task_states[state]);
	}

      /* finally, print the name of the task */
      if (atcb.image.P_ARRAY != NULL)
	{
	  READ_MEMORY ((CORE_ADDR) EXTRACT_ADDRESS (atcb.image.P_BOUNDS),
		       bounds);
	  bounds[1] = EXTRACT_INT (bounds[1]);
	  read_memory ((CORE_ADDR) EXTRACT_ADDRESS (atcb.image.P_ARRAY),
		       (char *) &image, bounds[1]);
	  printf_filtered (" %.*s\n", bounds[1], image);
	}
      else
	printf_filtered (" <no name>\n");

      pt = pt->next_task;
    }
}

/* Task list initialization for GDB-Tk.  We basically use info_tasks()
   to initialize our variables, but abort that function before we
   actually print anything. */

int
gdbtk_tcl_tasks_initialize (void)
{
  gdbtk_task_initialization = 1;
  info_tasks ("", gdb_stdout);

  return (task_list != NULL);
}

static void
info_tasks_command (char *arg, int from_tty)
{
  if (arg == NULL || *arg == '\000')
    info_tasks (arg, from_tty);
  else
    info_task (arg, from_tty);
}

/* Switch from one thread to another. */

static void
switch_to_thread (ptid_t ptid)
{
  if (ptid_equal (ptid, inferior_ptid))
    return;

  inferior_ptid = ptid;
  flush_cached_frames ();
  registers_changed ();
  stop_pc = read_pc ();
  select_frame (get_current_frame ());
}

/* Switch to a specified task. */

static int
task_switch (void *tid, void *lwpid)
{
  int res = 0, pid;

  if (thread_support)
    {
      flush_cached_frames ();

      if (current_task != current_task_id)
	{
	  res = THREAD_FETCH_REGISTERS ();
	}
      else
	{
#if (defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET))
	  supply_gregset (&gregset_saved);
	  supply_fpregset (&fpregset_saved);
#endif
	}

      if (res == 0)
	stop_pc = read_pc ();
      select_frame (get_current_frame ());
      return res;
    }

  return -1;
}

static void
task_command (char *tidstr, int from_tty)
{
  int num;
  struct task_entry *e;

  if (!tidstr)
    error ("Please specify a task ID.  Use the \"info tasks\" command to\n"
	   "see the IDs of currently known tasks.");

  num = atoi (tidstr);
  e = get_entry_vptr (num);

  if (e == NULL)
    error ("Task ID %d not known.  Use the \"info tasks\" command to\n"
	   "see the IDs of currently known tasks.", num);

  if (current_task_id == -1)
    {
#if (defined(__alpha__) && defined(__osf__) && !defined(VXWORKS_TARGET))
      fill_gregset (&gregset_saved, -1);
      fill_fpregset (&fpregset_saved, -1);
#endif
      current_task_id = get_current_task ();
    }

  current_task = num;
  current_task_index = e->known_tasks_index;
  current_thread = e->thread;
  current_lwp = e->lwp;
  if (task_switch (e->thread, e->lwp) == 0)
    {
      /* FIXME: find_printable_frame should be defined in frame.h, and
         implemented in ada-lang.c */
      /*      find_printable_frame (deprecated_selected_frame, frame_relative_level (deprecated_selected_frame)); */
      printf_filtered ("[Switching to task %d]\n", num);
      print_stack_frame (deprecated_selected_frame,
			 frame_relative_level (deprecated_selected_frame), 1);
    }
  else
    printf_filtered ("Unable to switch to task %d\n", num);
}

void
_initialize_tasks (void)
{
  static struct cmd_list_element *task_cmd_list = NULL;
  extern struct cmd_list_element *cmdlist;

  add_info ("tasks", info_tasks_command,
	    "Without argument: list all known Ada tasks, with status information.\n"
	    "info tasks n: print detailed information of task n.\n");

  add_prefix_cmd ("task", class_run, task_command,
		  "Use this command to switch between tasks.\n\
 The new task ID must be currently known.", &task_cmd_list, "task ", 1, &cmdlist);
}
