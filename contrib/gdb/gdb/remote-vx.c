/* Memory-access and commands for remote VxWorks processes, for GDB.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1997, 1998, 1999,
   2000, 2001, 2002 Free Software Foundation, Inc.

   Contributed by Wind River Systems and Cygnus Support.

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
#include "gdbcore.h"
#include "command.h"
#include "symtab.h"
#include "complaints.h"
#include "gdbcmd.h"
#include "bfd.h"		/* Required by objfiles.h.  */
#include "symfile.h"
#include "objfiles.h"
#include "gdb-stabs.h"
#include "regcache.h"

#include "gdb_string.h"
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#define malloc bogon_malloc	/* Sun claims "char *malloc()" not void * */
#define free bogon_free		/* Sun claims "int free()" not void */
#define realloc bogon_realloc	/* Sun claims "char *realloc()", not void * */
#include <rpc/rpc.h>
#undef malloc
#undef free
#undef realloc
#include <sys/time.h>		/* UTek's <rpc/rpc.h> doesn't #incl this */
#include <netdb.h>
#include "vx-share/ptrace.h"
#include "vx-share/xdr_ptrace.h"
#include "vx-share/xdr_ld.h"
#include "vx-share/xdr_rdb.h"
#include "vx-share/dbgRpcLib.h"

#include <symtab.h>

/* Maximum number of bytes to transfer in a single
   PTRACE_{READ,WRITE}DATA request.  */
#define VX_MEMXFER_MAX 4096

extern void vx_read_register ();
extern void vx_write_register ();
extern void symbol_file_command ();
extern enum stop_kind stop_soon;	/* for wait_for_inferior */

static int net_step ();
static int net_ptrace_clnt_call ();	/* Forward decl */
static enum clnt_stat net_clnt_call ();		/* Forward decl */

/* Target ops structure for accessing memory and such over the net */

static struct target_ops vx_ops;

/* Target ops structure for accessing VxWorks child processes over the net */

static struct target_ops vx_run_ops;

/* Saved name of target host and called function for "info files".
   Both malloc'd.  */

static char *vx_host;
static char *vx_running;	/* Called function */

/* Nonzero means target that is being debugged remotely has a floating
   point processor.  */

int target_has_fp;

/* Default error message when the network is forking up.  */

static const char rpcerr[] = "network target debugging:  rpc error";

CLIENT *pClient;		/* client used in net debugging */
static int ptraceSock = RPC_ANYSOCK;

enum clnt_stat net_clnt_call ();
static void parse_args ();

static struct timeval rpcTimeout =
{10, 0};

static char *skip_white_space ();
static char *find_white_space ();

/* Tell the VxWorks target system to download a file.
   The load addresses of the text, data, and bss segments are
   stored in *pTextAddr, *pDataAddr, and *pBssAddr (respectively).
   Returns 0 for success, -1 for failure.  */

static int
net_load (char *filename, CORE_ADDR *pTextAddr, CORE_ADDR *pDataAddr,
	  CORE_ADDR *pBssAddr)
{
  enum clnt_stat status;
  struct ldfile ldstruct;
  struct timeval load_timeout;

  memset ((char *) &ldstruct, '\0', sizeof (ldstruct));

  /* We invoke clnt_call () here directly, instead of through
     net_clnt_call (), because we need to set a large timeout value.
     The load on the target side can take quite a while, easily
     more than 10 seconds.  The user can kill this call by typing
     CTRL-C if there really is a problem with the load.  

     Do not change the tv_sec value without checking -- select() imposes
     a limit of 10**8 on it for no good reason that I can see...  */

  load_timeout.tv_sec = 99999999;	/* A large number, effectively inf. */
  load_timeout.tv_usec = 0;

  status = clnt_call (pClient, VX_LOAD, xdr_wrapstring, &filename, xdr_ldfile,
		      &ldstruct, load_timeout);

  if (status == RPC_SUCCESS)
    {
      if (*ldstruct.name == 0)	/* load failed on VxWorks side */
	return -1;
      *pTextAddr = ldstruct.txt_addr;
      *pDataAddr = ldstruct.data_addr;
      *pBssAddr = ldstruct.bss_addr;
      return 0;
    }
  else
    return -1;
}

/* returns 0 if successful, errno if RPC failed or VxWorks complains. */

static int
net_break (int addr, u_long procnum)
{
  enum clnt_stat status;
  int break_status;
  Rptrace ptrace_in;		/* XXX This is stupid.  It doesn't need to be a ptrace
				   structure.  How about something smaller? */

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  break_status = 0;

  ptrace_in.addr = addr;
  ptrace_in.pid = PIDGET (inferior_ptid);

  status = net_clnt_call (procnum, xdr_rptrace, &ptrace_in, xdr_int,
			  &break_status);

  if (status != RPC_SUCCESS)
    return errno;

  if (break_status == -1)
    return ENOMEM;
  return break_status;		/* probably (FIXME) zero */
}

/* returns 0 if successful, errno otherwise */

static int
vx_insert_breakpoint (int addr)
{
  return net_break (addr, VX_BREAK_ADD);
}

/* returns 0 if successful, errno otherwise */

static int
vx_remove_breakpoint (int addr)
{
  return net_break (addr, VX_BREAK_DELETE);
}

/* Start an inferior process and sets inferior_ptid to its pid.
   EXEC_FILE is the file to run.
   ALLARGS is a string containing the arguments to the program.
   ENV is the environment vector to pass.
   Returns process id.  Errors reported with error().
   On VxWorks, we ignore exec_file.  */

static void
vx_create_inferior (char *exec_file, char *args, char **env)
{
  enum clnt_stat status;
  arg_array passArgs;
  TASK_START taskStart;

  memset ((char *) &passArgs, '\0', sizeof (passArgs));
  memset ((char *) &taskStart, '\0', sizeof (taskStart));

  /* parse arguments, put them in passArgs */

  parse_args (args, &passArgs);

  if (passArgs.arg_array_len == 0)
    error ("You must specify a function name to run, and arguments if any");

  status = net_clnt_call (PROCESS_START, xdr_arg_array, &passArgs,
			  xdr_TASK_START, &taskStart);

  if ((status != RPC_SUCCESS) || (taskStart.status == -1))
    error ("Can't create process on remote target machine");

  /* Save the name of the running function */
  vx_running = savestring (passArgs.arg_array_val[0],
			   strlen (passArgs.arg_array_val[0]));

  push_target (&vx_run_ops);
  inferior_ptid = pid_to_ptid (taskStart.pid);

  /* We will get a trace trap after one instruction.
     Insert breakpoints and continue.  */

  init_wait_for_inferior ();

  /* Set up the "saved terminal modes" of the inferior
     based on what modes we are starting it with.  */
  target_terminal_init ();

  /* Install inferior's terminal modes.  */
  target_terminal_inferior ();

  stop_soon = STOP_QUIETLY;
  wait_for_inferior ();		/* Get the task spawn event */
  stop_soon = NO_STOP_QUIETLY;

  /* insert_step_breakpoint ();  FIXME, do we need this?  */
  proceed (-1, TARGET_SIGNAL_DEFAULT, 0);
}

/* Fill ARGSTRUCT in argc/argv form with the arguments from the
   argument string ARGSTRING.  */

static void
parse_args (char *arg_string, arg_array *arg_struct)
{
  int arg_count = 0;	/* number of arguments */
  int arg_index = 0;
  char *p0;

  memset ((char *) arg_struct, '\0', sizeof (arg_array));

  /* first count how many arguments there are */

  p0 = arg_string;
  while (*p0 != '\0')
    {
      if (*(p0 = skip_white_space (p0)) == '\0')
	break;
      p0 = find_white_space (p0);
      arg_count++;
    }

  arg_struct->arg_array_len = arg_count;
  arg_struct->arg_array_val = (char **) xmalloc ((arg_count + 1)
						 * sizeof (char *));

  /* now copy argument strings into arg_struct.  */

  while (*(arg_string = skip_white_space (arg_string)))
    {
      p0 = find_white_space (arg_string);
      arg_struct->arg_array_val[arg_index++] = savestring (arg_string,
							   p0 - arg_string);
      arg_string = p0;
    }

  arg_struct->arg_array_val[arg_count] = NULL;
}

/* Advance a string pointer across whitespace and return a pointer
   to the first non-white character.  */

static char *
skip_white_space (char *p)
{
  while (*p == ' ' || *p == '\t')
    p++;
  return p;
}

/* Search for the first unquoted whitespace character in a string.
   Returns a pointer to the character, or to the null terminator
   if no whitespace is found.  */

static char *
find_white_space (char *p)
{
  int c;

  while ((c = *p) != ' ' && c != '\t' && c)
    {
      if (c == '\'' || c == '"')
	{
	  while (*++p != c && *p)
	    {
	      if (*p == '\\')
		p++;
	    }
	  if (!*p)
	    break;
	}
      p++;
    }
  return p;
}

/* Poll the VxWorks target system for an event related
   to the debugged task.
   Returns -1 if remote wait failed, task status otherwise.  */

static int
net_wait (RDB_EVENT *pEvent)
{
  int pid;
  enum clnt_stat status;

  memset ((char *) pEvent, '\0', sizeof (RDB_EVENT));

  pid = PIDGET (inferior_ptid);
  status = net_clnt_call (PROCESS_WAIT, xdr_int, &pid, xdr_RDB_EVENT,
			  pEvent);

  /* return (status == RPC_SUCCESS)? pEvent->status: -1; */
  if (status == RPC_SUCCESS)
    return ((pEvent->status) ? 1 : 0);
  else if (status == RPC_TIMEDOUT)
    return (1);
  else
    return (-1);
}

/* Suspend the remote task.
   Returns -1 if suspend fails on target system, 0 otherwise.  */

static int
net_quit (void)
{
  int pid;
  int quit_status;
  enum clnt_stat status;

  quit_status = 0;

  /* don't let rdbTask suspend itself by passing a pid of 0 */

  if ((pid = PIDGET (inferior_ptid)) == 0)
    return -1;

  status = net_clnt_call (VX_TASK_SUSPEND, xdr_int, &pid, xdr_int,
			  &quit_status);

  return (status == RPC_SUCCESS) ? quit_status : -1;
}

/* Read a register or registers from the remote system.  */

void
net_read_registers (char *reg_buf, int len, u_long procnum)
{
  int status;
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  C_bytes out_data;
  char message[100];

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));

  /* Initialize RPC input argument structure.  */

  ptrace_in.pid = PIDGET (inferior_ptid);
  ptrace_in.info.ttype = NOINFO;

  /* Initialize RPC return value structure.  */

  out_data.bytes = reg_buf;
  out_data.len = len;
  ptrace_out.info.more_data = (caddr_t) & out_data;

  /* Call RPC; take an error exit if appropriate.  */

  status = net_ptrace_clnt_call (procnum, &ptrace_in, &ptrace_out);
  if (status)
    error (rpcerr);
  if (ptrace_out.status == -1)
    {
      errno = ptrace_out.errno_num;
      sprintf (message, "reading %s registers", (procnum == PTRACE_GETREGS)
	       ? "general-purpose"
	       : "floating-point");
      perror_with_name (message);
    }
}

/* Write register values to a VxWorks target.  REG_BUF points to a buffer
   containing the raw register values, LEN is the length of REG_BUF in
   bytes, and PROCNUM is the RPC procedure number (PTRACE_SETREGS or
   PTRACE_SETFPREGS).  An error exit is taken if the RPC call fails or
   if an error status is returned by the remote debug server.  This is
   a utility routine used by vx_write_register ().  */

void
net_write_registers (char *reg_buf, int len, u_long procnum)
{
  int status;
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  C_bytes in_data;
  char message[100];

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));

  /* Initialize RPC input argument structure.  */

  in_data.bytes = reg_buf;
  in_data.len = len;

  ptrace_in.pid = PIDGET (inferior_ptid);
  ptrace_in.info.ttype = DATA;
  ptrace_in.info.more_data = (caddr_t) & in_data;

  /* Call RPC; take an error exit if appropriate.  */

  status = net_ptrace_clnt_call (procnum, &ptrace_in, &ptrace_out);
  if (status)
    error (rpcerr);
  if (ptrace_out.status == -1)
    {
      errno = ptrace_out.errno_num;
      sprintf (message, "writing %s registers", (procnum == PTRACE_SETREGS)
	       ? "general-purpose"
	       : "floating-point");
      perror_with_name (message);
    }
}

/* Prepare to store registers.  Since we will store all of them,
   read out their current values now.  */

static void
vx_prepare_to_store (void)
{
  /* Fetch all registers, if any of them are not yet fetched.  */
  deprecated_read_register_bytes (0, NULL, DEPRECATED_REGISTER_BYTES);
}

/* Copy LEN bytes to or from remote inferior's memory starting at MEMADDR
   to debugger memory starting at MYADDR.  WRITE is true if writing to the
   inferior.  TARGET is unused.
   Result is the number of bytes written or read (zero if error).  The
   protocol allows us to return a negative count, indicating that we can't
   handle the current address but can handle one N bytes further, but
   vxworks doesn't give us that information.  */

static int
vx_xfer_memory (CORE_ADDR memaddr, char *myaddr, int len, int write,
		struct mem_attrib *attrib, struct target_ops *target)
{
  int status;
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  C_bytes data;
  enum ptracereq request;
  int nleft, nxfer;

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));

  ptrace_in.pid = PIDGET (inferior_ptid); /* XXX pid unnecessary for READDATA */
  ptrace_in.addr = (int) memaddr;	/* Where from */
  ptrace_in.data = len;		/* How many bytes */

  if (write)
    {
      ptrace_in.info.ttype = DATA;
      ptrace_in.info.more_data = (caddr_t) & data;

      data.bytes = (caddr_t) myaddr;	/* Where from */
      data.len = len;		/* How many bytes (again, for XDR) */
      request = PTRACE_WRITEDATA;
    }
  else
    {
      ptrace_out.info.more_data = (caddr_t) & data;
      request = PTRACE_READDATA;
    }
  /* Loop until the entire request has been satisfied, transferring
     at most VX_MEMXFER_MAX bytes per iteration.  Break from the loop
     if an error status is returned by the remote debug server.  */

  nleft = len;
  status = 0;

  while (nleft > 0 && status == 0)
    {
      nxfer = min (nleft, VX_MEMXFER_MAX);

      ptrace_in.addr = (int) memaddr;
      ptrace_in.data = nxfer;
      data.bytes = (caddr_t) myaddr;
      data.len = nxfer;

      /* Request a block from the remote debug server; if RPC fails,
         report an error and return to debugger command level.  */

      if (net_ptrace_clnt_call (request, &ptrace_in, &ptrace_out))
	error (rpcerr);

      status = ptrace_out.status;
      if (status == 0)
	{
	  memaddr += nxfer;
	  myaddr += nxfer;
	  nleft -= nxfer;
	}
      else
	{
	  /* A target-side error has ocurred.  Set errno to the error
	     code chosen by the target so that a later perror () will
	     say something meaningful.  */

	  errno = ptrace_out.errno_num;
	}
    }

  /* Return the number of bytes transferred.  */

  return (len - nleft);
}

static void
vx_files_info (void)
{
  printf_unfiltered ("\tAttached to host `%s'", vx_host);
  printf_unfiltered (", which has %sfloating point", target_has_fp ? "" : "no ");
  printf_unfiltered (".\n");
}

static void
vx_run_files_info (void)
{
  printf_unfiltered ("\tRunning %s VxWorks process %s",
		     vx_running ? "child" : "attached",
		     local_hex_string (PIDGET (inferior_ptid)));
  if (vx_running)
    printf_unfiltered (", function `%s'", vx_running);
  printf_unfiltered (".\n");
}

static void
vx_resume (ptid_t ptid, int step, enum target_signal siggnal)
{
  int status;
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  CORE_ADDR cont_addr;

  if (ptid_equal (ptid, minus_one_ptid))
    ptid = inferior_ptid;

  if (siggnal != 0 && siggnal != stop_signal)
    error ("Cannot send signals to VxWorks processes");

  /* Set CONT_ADDR to the address at which we are continuing,
     or to 1 if we are continuing from where the program stopped.
     This conforms to traditional ptrace () usage, but at the same
     time has special meaning for the VxWorks remote debug server.
     If the address is not 1, the server knows that the target
     program is jumping to a new address, which requires special
     handling if there is a breakpoint at the new address.  */

  cont_addr = read_register (PC_REGNUM);
  if (cont_addr == stop_pc)
    cont_addr = 1;

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));

  ptrace_in.pid = PIDGET (ptid);
  ptrace_in.addr = cont_addr;	/* Target side insists on this, or it panics.  */

  if (step)
    status = net_step ();
  else
    status = net_ptrace_clnt_call (PTRACE_CONT, &ptrace_in, &ptrace_out);

  if (status)
    error (rpcerr);
  if (ptrace_out.status == -1)
    {
      errno = ptrace_out.errno_num;
      perror_with_name ("Resuming remote process");
    }
}

static void
vx_mourn_inferior (void)
{
  pop_target ();		/* Pop back to no-child state */
  generic_mourn_inferior ();
}


static void vx_add_symbols (char *, int, CORE_ADDR, CORE_ADDR, CORE_ADDR);

struct find_sect_args
  {
    CORE_ADDR text_start;
    CORE_ADDR data_start;
    CORE_ADDR bss_start;
  };

static void find_sect (bfd *, asection *, void *);

static void
find_sect (bfd *abfd, asection *sect, void *obj)
{
  struct find_sect_args *args = (struct find_sect_args *) obj;

  if (bfd_get_section_flags (abfd, sect) & (SEC_CODE & SEC_READONLY))
    args->text_start = bfd_get_section_vma (abfd, sect);
  else if (bfd_get_section_flags (abfd, sect) & SEC_ALLOC)
    {
      if (bfd_get_section_flags (abfd, sect) & SEC_LOAD)
	{
	  /* Exclude .ctor and .dtor sections which have SEC_CODE set but not
	     SEC_DATA.  */
	  if (bfd_get_section_flags (abfd, sect) & SEC_DATA)
	    args->data_start = bfd_get_section_vma (abfd, sect);
	}
      else
	args->bss_start = bfd_get_section_vma (abfd, sect);
    }
}

static void
vx_add_symbols (char *name, int from_tty, CORE_ADDR text_addr,
		CORE_ADDR data_addr, CORE_ADDR bss_addr)
{
  struct section_offsets *offs;
  struct objfile *objfile;
  struct find_sect_args ss;

  /* It might be nice to suppress the breakpoint_re_set which happens here
     because we are going to do one again after the objfile_relocate.  */
  objfile = symbol_file_add (name, from_tty, NULL, 0, 0);

  /* This is a (slightly cheesy) way of superceding the old symbols.  A less
     cheesy way would be to find the objfile with the same name and
     free_objfile it.  */
  objfile_to_front (objfile);

  offs =
    (struct section_offsets *)
    alloca (SIZEOF_N_SECTION_OFFSETS (objfile->num_sections));
  memcpy (offs, objfile->section_offsets,
          SIZEOF_N_SECTION_OFFSETS (objfile->num_sections));

  ss.text_start = 0;
  ss.data_start = 0;
  ss.bss_start = 0;
  bfd_map_over_sections (objfile->obfd, find_sect, &ss);

  /* Both COFF and b.out frontends use these SECT_OFF_* values.  */
  offs->offsets[SECT_OFF_TEXT (objfile)]  = text_addr - ss.text_start;
  offs->offsets[SECT_OFF_DATA (objfile)] = data_addr - ss.data_start;
  offs->offsets[SECT_OFF_BSS (objfile)] = bss_addr - ss.bss_start;
  objfile_relocate (objfile, offs);
}

/* This function allows the addition of incrementally linked object files.  */

static void
vx_load_command (char *arg_string, int from_tty)
{
  CORE_ADDR text_addr;
  CORE_ADDR data_addr;
  CORE_ADDR bss_addr;

  if (arg_string == 0)
    error ("The load command takes a file name");

  arg_string = tilde_expand (arg_string);
  make_cleanup (xfree, arg_string);

  dont_repeat ();

  /* Refuse to load the module if a debugged task is running.  Doing so
     can have a number of unpleasant consequences to the running task.  */

  if (PIDGET (inferior_ptid) != 0 && target_has_execution)
    {
      if (query ("You may not load a module while the target task is running.\n\
Kill the target task? "))
	target_kill ();
      else
	error ("Load canceled.");
    }

  QUIT;
  immediate_quit++;
  if (net_load (arg_string, &text_addr, &data_addr, &bss_addr) == -1)
    error ("Load failed on target machine");
  immediate_quit--;

  vx_add_symbols (arg_string, from_tty, text_addr, data_addr, bss_addr);

  /* Getting new symbols may change our opinion about what is
     frameless.  */
  reinit_frame_cache ();
}

/* Single step the target program at the source or machine level.
   Takes an error exit if rpc fails.
   Returns -1 if remote single-step operation fails, else 0.  */

static int
net_step (void)
{
  enum clnt_stat status;
  int step_status;
  SOURCE_STEP source_step;

  source_step.taskId = PIDGET (inferior_ptid);

  if (step_range_end)
    {
      source_step.startAddr = step_range_start;
      source_step.endAddr = step_range_end;
    }
  else
    {
      source_step.startAddr = 0;
      source_step.endAddr = 0;
    }

  status = net_clnt_call (VX_SOURCE_STEP, xdr_SOURCE_STEP, &source_step,
			  xdr_int, &step_status);

  if (status == RPC_SUCCESS)
    return step_status;
  else
    error (rpcerr);
}

/* Emulate ptrace using RPC calls to the VxWorks target system.
   Returns nonzero (-1) if RPC status to VxWorks is bad, 0 otherwise.  */

static int
net_ptrace_clnt_call (enum ptracereq request, Rptrace *pPtraceIn,
		      Ptrace_return *pPtraceOut)
{
  enum clnt_stat status;

  status = net_clnt_call (request, xdr_rptrace, pPtraceIn, xdr_ptrace_return,
			  pPtraceOut);

  if (status != RPC_SUCCESS)
    return -1;

  return 0;
}

/* Query the target for the name of the file from which VxWorks was
   booted.  pBootFile is the address of a pointer to the buffer to
   receive the file name; if the pointer pointed to by pBootFile is 
   NULL, memory for the buffer will be allocated by XDR.
   Returns -1 if rpc failed, 0 otherwise.  */

static int
net_get_boot_file (char **pBootFile)
{
  enum clnt_stat status;

  status = net_clnt_call (VX_BOOT_FILE_INQ, xdr_void, (char *) 0,
			  xdr_wrapstring, pBootFile);
  return (status == RPC_SUCCESS) ? 0 : -1;
}

/* Fetch a list of loaded object modules from the VxWorks target
   and store in PLOADTABLE.
   Returns -1 if rpc failed, 0 otherwise
   There's no way to check if the returned loadTable is correct.
   VxWorks doesn't check it.  */

static int
net_get_symbols (ldtabl *pLoadTable)
{
  enum clnt_stat status;

  memset ((char *) pLoadTable, '\0', sizeof (struct ldtabl));

  status = net_clnt_call (VX_STATE_INQ, xdr_void, 0, xdr_ldtabl, pLoadTable);
  return (status == RPC_SUCCESS) ? 0 : -1;
}

/* Look up a symbol in the VxWorks target's symbol table.
   Returns status of symbol read on target side (0=success, -1=fail)
   Returns -1 and complain()s if rpc fails.  */

static int
vx_lookup_symbol (char *name,	/* symbol name */
		  CORE_ADDR *pAddr)
{
  enum clnt_stat status;
  SYMBOL_ADDR symbolAddr;

  *pAddr = 0;
  memset ((char *) &symbolAddr, '\0', sizeof (symbolAddr));

  status = net_clnt_call (VX_SYMBOL_INQ, xdr_wrapstring, &name,
			  xdr_SYMBOL_ADDR, &symbolAddr);
  if (status != RPC_SUCCESS)
    {
      complaint (&symfile_complaints, "Lost contact with VxWorks target");
      return -1;
    }

  *pAddr = symbolAddr.addr;
  return symbolAddr.status;
}

/* Check to see if the VxWorks target has a floating point coprocessor.
   Returns 1 if target has floating point processor, 0 otherwise.
   Calls error() if rpc fails.  */

static int
net_check_for_fp (void)
{
  enum clnt_stat status;
  bool_t fp = 0;		/* true if fp processor is present on target board */

  status = net_clnt_call (VX_FP_INQUIRE, xdr_void, 0, xdr_bool, &fp);
  if (status != RPC_SUCCESS)
    error (rpcerr);

  return (int) fp;
}

/* Establish an RPC connection with the VxWorks target system.
   Calls error () if unable to establish connection.  */

static void
net_connect (char *host)
{
  struct sockaddr_in destAddr;
  struct hostent *destHost;
  unsigned long addr;

  /* Get the internet address for the given host.  Allow a numeric
     IP address or a hostname.  */

  addr = inet_addr (host);
  if (addr == -1)
    {
      destHost = (struct hostent *) gethostbyname (host);
      if (destHost == NULL)
	/* FIXME: Probably should include hostname here in quotes.
	   For example if the user types "target vxworks vx960 " it should
	   say "Invalid host `vx960 '." not just "Invalid hostname".  */
	error ("Invalid hostname.  Couldn't find remote host address.");
      addr = *(unsigned long *) destHost->h_addr;
    }

  memset (&destAddr, '\0', sizeof (destAddr));

  destAddr.sin_addr.s_addr = addr;
  destAddr.sin_family = AF_INET;
  destAddr.sin_port = 0;	/* set to actual port that remote
				   ptrace is listening on.  */

  /* Create a tcp client transport on which to issue
     calls to the remote ptrace server.  */

  ptraceSock = RPC_ANYSOCK;
  pClient = clnttcp_create (&destAddr, RDBPROG, RDBVERS, &ptraceSock, 0, 0);
  /* FIXME, here is where we deal with different version numbers of the
     proto */

  if (pClient == NULL)
    {
      clnt_pcreateerror ("\tnet_connect");
      error ("Couldn't connect to remote target.");
    }
}

/* Sleep for the specified number of milliseconds 
 * (assumed to be less than 1000).
 * If select () is interrupted, returns immediately;
 * takes an error exit if select () fails for some other reason.
 */

static void
sleep_ms (long ms)
{
  struct timeval select_timeout;
  int status;

  select_timeout.tv_sec = 0;
  select_timeout.tv_usec = ms * 1000;

  status = select (0, (fd_set *) 0, (fd_set *) 0, (fd_set *) 0,
		   &select_timeout);

  if (status < 0 && errno != EINTR)
    perror_with_name ("select");
}

static ptid_t
vx_wait (ptid_t ptid_to_wait_for, struct target_waitstatus *status)
{
  int pid;
  RDB_EVENT rdbEvent;
  int quit_failed;

  do
    {
      /* If CTRL-C is hit during this loop,
         suspend the inferior process.  */

      quit_failed = 0;
      if (quit_flag)
	{
	  quit_failed = (net_quit () == -1);
	  quit_flag = 0;
	}

      /* If a net_quit () or net_wait () call has failed,
         allow the user to break the connection with the target.
         We can't simply error () out of this loop, since the 
         data structures representing the state of the inferior
         are in an inconsistent state.  */

      if (quit_failed || net_wait (&rdbEvent) == -1)
	{
	  terminal_ours ();
	  if (query ("Can't %s.  Disconnect from target system? ",
		     (quit_failed) ? "suspend remote task"
		     : "get status of remote task"))
	    {
	      target_mourn_inferior ();
	      error ("Use the \"target\" command to reconnect.");
	    }
	  else
	    {
	      terminal_inferior ();
	      continue;
	    }
	}

      pid = rdbEvent.taskId;
      if (pid == 0)
	{
	  sleep_ms (200);	/* FIXME Don't kill the network too badly */
	}
      else if (pid != PIDGET (inferior_ptid))
	internal_error (__FILE__, __LINE__,
			"Bad pid for debugged task: %s\n",
			local_hex_string ((unsigned long) pid));
    }
  while (pid == 0);

  /* The mostly likely kind.  */
  status->kind = TARGET_WAITKIND_STOPPED;

  switch (rdbEvent.eventType)
    {
    case EVENT_EXIT:
      status->kind = TARGET_WAITKIND_EXITED;
      /* FIXME is it possible to distinguish between a
         normal vs abnormal exit in VxWorks? */
      status->value.integer = 0;
      break;

    case EVENT_START:
      /* Task was just started. */
      status->value.sig = TARGET_SIGNAL_TRAP;
      break;

    case EVENT_STOP:
      status->value.sig = TARGET_SIGNAL_TRAP;
      /* XXX was it stopped by a signal?  act accordingly */
      break;

    case EVENT_BREAK:		/* Breakpoint was hit. */
      status->value.sig = TARGET_SIGNAL_TRAP;
      break;

    case EVENT_SUSPEND:	/* Task was suspended, probably by ^C. */
      status->value.sig = TARGET_SIGNAL_INT;
      break;

    case EVENT_BUS_ERR:	/* Task made evil nasty reference. */
      status->value.sig = TARGET_SIGNAL_BUS;
      break;

    case EVENT_ZERO_DIV:	/* Division by zero */
      status->value.sig = TARGET_SIGNAL_FPE;
      break;

    case EVENT_SIGNAL:
#ifdef I80960
      status->value.sig = i960_fault_to_signal (rdbEvent.sigType);
#else
      /* Back in the old days, before enum target_signal, this code used
         to add NSIG to the signal number and claim that PRINT_RANDOM_SIGNAL
         would take care of it.  But PRINT_RANDOM_SIGNAL has never been
         defined except on the i960, so I don't really know what we are
         supposed to do on other architectures.  */
      status->value.sig = TARGET_SIGNAL_UNKNOWN;
#endif
      break;
    }				/* switch */
  return pid_to_ptid (pid);
}

static int
symbol_stub (char *arg)
{
  symbol_file_add_main (arg, 0);
  return 1;
}

static int
add_symbol_stub (char *arg)
{
  struct ldfile *pLoadFile = (struct ldfile *) arg;

  printf_unfiltered ("\t%s: ", pLoadFile->name);
  vx_add_symbols (pLoadFile->name, 0, pLoadFile->txt_addr,
		  pLoadFile->data_addr, pLoadFile->bss_addr);
  printf_unfiltered ("ok\n");
  return 1;
}
/* Target command for VxWorks target systems.

   Used in vxgdb.  Takes the name of a remote target machine
   running vxWorks and connects to it to initialize remote network
   debugging.  */

static void
vx_open (char *args, int from_tty)
{
  extern int close ();
  char *bootFile;
  extern char *source_path;
  struct ldtabl loadTable;
  struct ldfile *pLoadFile;
  int i;
  extern CLIENT *pClient;
  int symbols_added = 0;

  if (!args)
    error_no_arg ("target machine name");

  target_preopen (from_tty);

  unpush_target (&vx_ops);
  printf_unfiltered ("Attaching remote machine across net...\n");
  gdb_flush (gdb_stdout);

  /* Allow the user to kill the connect attempt by typing ^C.
     Wait until the call to target_has_fp () completes before
     disallowing an immediate quit, since even if net_connect ()
     is successful, the remote debug server might be hung.  */

  immediate_quit++;

  net_connect (args);
  target_has_fp = net_check_for_fp ();
  printf_filtered ("Connected to %s.\n", args);

  immediate_quit--;

  push_target (&vx_ops);

  /* Save a copy of the target host's name.  */
  vx_host = savestring (args, strlen (args));

  /* Find out the name of the file from which the target was booted
     and load its symbol table.  */

  printf_filtered ("Looking in Unix path for all loaded modules:\n");
  bootFile = NULL;
  if (!net_get_boot_file (&bootFile))
    {
      if (*bootFile)
	{
	  printf_filtered ("\t%s: ", bootFile);
	  /* This assumes that the kernel is never relocated.  Hope that is an
	     accurate assumption.  */
	  if (catch_errors
	      (symbol_stub,
	       bootFile,
	       "Error while reading symbols from boot file:\n",
	       RETURN_MASK_ALL))
	    puts_filtered ("ok\n");
	}
      else if (from_tty)
	printf_unfiltered ("VxWorks kernel symbols not loaded.\n");
    }
  else
    error ("Can't retrieve boot file name from target machine.");

  clnt_freeres (pClient, xdr_wrapstring, &bootFile);

  if (net_get_symbols (&loadTable) != 0)
    error ("Can't read loaded modules from target machine");

  i = 0 - 1;
  while (++i < loadTable.tbl_size)
    {
      QUIT;			/* FIXME, avoids clnt_freeres below:  mem leak */
      pLoadFile = &loadTable.tbl_ent[i];
#ifdef WRS_ORIG
      {
	int desc;
	struct cleanup *old_chain;
	char *fullname = NULL;

	desc = openp (source_path, 0, pLoadFile->name, O_RDONLY, 0, &fullname);
	if (desc < 0)
	  perror_with_name (pLoadFile->name);
	old_chain = make_cleanup (close, desc);
	add_file_at_addr (fullname, desc, pLoadFile->txt_addr, pLoadFile->data_addr,
			  pLoadFile->bss_addr);
	do_cleanups (old_chain);
      }
#else
      /* FIXME: Is there something better to search than the PATH? (probably
         not the source path, since source might be in different directories
         than objects.  */

      if (catch_errors (add_symbol_stub, (char *) pLoadFile, (char *) 0,
			RETURN_MASK_ALL))
	symbols_added = 1;
#endif
    }
  printf_filtered ("Done.\n");

  clnt_freeres (pClient, xdr_ldtabl, &loadTable);

  /* Getting new symbols may change our opinion about what is
     frameless.  */
  if (symbols_added)
    reinit_frame_cache ();
}

/* Takes a task started up outside of gdb and ``attaches'' to it.
   This stops it cold in its tracks and allows us to start tracing it.  */

static void
vx_attach (char *args, int from_tty)
{
  unsigned long pid;
  char *cptr = 0;
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  int status;

  if (!args)
    error_no_arg ("process-id to attach");

  pid = strtoul (args, &cptr, 0);
  if ((cptr == args) || (*cptr != '\0'))
    error ("Invalid process-id -- give a single number in decimal or 0xhex");

  if (from_tty)
    printf_unfiltered ("Attaching pid %s.\n",
		       local_hex_string ((unsigned long) pid));

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));
  ptrace_in.pid = pid;

  status = net_ptrace_clnt_call (PTRACE_ATTACH, &ptrace_in, &ptrace_out);
  if (status == -1)
    error (rpcerr);
  if (ptrace_out.status == -1)
    {
      errno = ptrace_out.errno_num;
      perror_with_name ("Attaching remote process");
    }

  /* It worked... */

  inferior_ptid = pid_to_ptid (pid);
  push_target (&vx_run_ops);

  if (vx_running)
    xfree (vx_running);
  vx_running = 0;
}

/* detach_command --
   takes a program previously attached to and detaches it.
   The program resumes execution and will no longer stop
   on signals, etc.  We better not have left any breakpoints
   in the program or it'll die when it hits one.  For this
   to work, it may be necessary for the process to have been
   previously attached.  It *might* work if the program was
   started via the normal ptrace (PTRACE_TRACEME).  */

static void
vx_detach (char *args, int from_tty)
{
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  int signal = 0;
  int status;

  if (args)
    error ("Argument given to VxWorks \"detach\".");

  if (from_tty)
    printf_unfiltered ("Detaching pid %s.\n",
		       local_hex_string (
		         (unsigned long) PIDGET (inferior_ptid)));

  if (args)			/* FIXME, should be possible to leave suspended */
    signal = atoi (args);

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));
  ptrace_in.pid = PIDGET (inferior_ptid);

  status = net_ptrace_clnt_call (PTRACE_DETACH, &ptrace_in, &ptrace_out);
  if (status == -1)
    error (rpcerr);
  if (ptrace_out.status == -1)
    {
      errno = ptrace_out.errno_num;
      perror_with_name ("Detaching VxWorks process");
    }

  inferior_ptid = null_ptid;
  pop_target ();		/* go back to non-executing VxWorks connection */
}

/* vx_kill -- takes a running task and wipes it out.  */

static void
vx_kill (void)
{
  Rptrace ptrace_in;
  Ptrace_return ptrace_out;
  int status;

  printf_unfiltered ("Killing pid %s.\n", local_hex_string ((unsigned long) PIDGET (inferior_ptid)));

  memset ((char *) &ptrace_in, '\0', sizeof (ptrace_in));
  memset ((char *) &ptrace_out, '\0', sizeof (ptrace_out));
  ptrace_in.pid = PIDGET (inferior_ptid);

  status = net_ptrace_clnt_call (PTRACE_KILL, &ptrace_in, &ptrace_out);
  if (status == -1)
    warning (rpcerr);
  else if (ptrace_out.status == -1)
    {
      errno = ptrace_out.errno_num;
      perror_with_name ("Killing VxWorks process");
    }

  /* If it gives good status, the process is *gone*, no events remain.
     If the kill failed, assume the process is gone anyhow.  */
  inferior_ptid = null_ptid;
  pop_target ();		/* go back to non-executing VxWorks connection */
}

/* Clean up from the VxWorks process target as it goes away.  */

static void
vx_proc_close (int quitting)
{
  inferior_ptid = null_ptid;	/* No longer have a process.  */
  if (vx_running)
    xfree (vx_running);
  vx_running = 0;
}

/* Make an RPC call to the VxWorks target.
   Returns RPC status.  */

static enum clnt_stat
net_clnt_call (enum ptracereq procNum, xdrproc_t inProc, char *in,
	       xdrproc_t outProc, char *out)
{
  enum clnt_stat status;

  status = clnt_call (pClient, procNum, inProc, in, outProc, out, rpcTimeout);

  if (status != RPC_SUCCESS)
    clnt_perrno (status);

  return status;
}

/* Clean up before losing control.  */

static void
vx_close (int quitting)
{
  if (pClient)
    clnt_destroy (pClient);	/* The net connection */
  pClient = 0;

  if (vx_host)
    xfree (vx_host);		/* The hostname */
  vx_host = 0;
}

/* A vxprocess target should be started via "run" not "target".  */
static void
vx_proc_open (char *name, int from_tty)
{
  error ("Use the \"run\" command to start a VxWorks process.");
}

static void
init_vx_ops (void)
{
  vx_ops.to_shortname = "vxworks";
  vx_ops.to_longname = "VxWorks target memory via RPC over TCP/IP";
  vx_ops.to_doc = "Use VxWorks target memory.  \n\
Specify the name of the machine to connect to.";
  vx_ops.to_open = vx_open;
  vx_ops.to_close = vx_close;
  vx_ops.to_attach = vx_attach;
  vx_ops.to_xfer_memory = vx_xfer_memory;
  vx_ops.to_files_info = vx_files_info;
  vx_ops.to_load = vx_load_command;
  vx_ops.to_lookup_symbol = vx_lookup_symbol;
  vx_ops.to_create_inferior = vx_create_inferior;
  vx_ops.to_stratum = core_stratum;
  vx_ops.to_has_all_memory = 1;
  vx_ops.to_has_memory = 1;
  vx_ops.to_magic = OPS_MAGIC;	/* Always the last thing */
};

static void
init_vx_run_ops (void)
{
  vx_run_ops.to_shortname = "vxprocess";
  vx_run_ops.to_longname = "VxWorks process";
  vx_run_ops.to_doc = "VxWorks process; started by the \"run\" command.";
  vx_run_ops.to_open = vx_proc_open;
  vx_run_ops.to_close = vx_proc_close;
  vx_run_ops.to_detach = vx_detach;
  vx_run_ops.to_resume = vx_resume;
  vx_run_ops.to_wait = vx_wait;
  vx_run_ops.to_fetch_registers = vx_read_register;
  vx_run_ops.to_store_registers = vx_write_register;
  vx_run_ops.to_prepare_to_store = vx_prepare_to_store;
  vx_run_ops.to_xfer_memory = vx_xfer_memory;
  vx_run_ops.to_files_info = vx_run_files_info;
  vx_run_ops.to_insert_breakpoint = vx_insert_breakpoint;
  vx_run_ops.to_remove_breakpoint = vx_remove_breakpoint;
  vx_run_ops.to_kill = vx_kill;
  vx_run_ops.to_load = vx_load_command;
  vx_run_ops.to_lookup_symbol = vx_lookup_symbol;
  vx_run_ops.to_mourn_inferior = vx_mourn_inferior;
  vx_run_ops.to_stratum = process_stratum;
  vx_run_ops.to_has_memory = 1;
  vx_run_ops.to_has_stack = 1;
  vx_run_ops.to_has_registers = 1;
  vx_run_ops.to_has_execution = 1;
  vx_run_ops.to_magic = OPS_MAGIC;
}

void
_initialize_vx (void)
{
  init_vx_ops ();
  add_target (&vx_ops);
  init_vx_run_ops ();
  add_target (&vx_run_ops);

  add_show_from_set
    (add_set_cmd ("vxworks-timeout", class_support, var_uinteger,
		  (char *) &rpcTimeout.tv_sec,
		  "Set seconds to wait for rpc calls to return.\n\
Set the number of seconds to wait for rpc calls to return.", &setlist),
     &showlist);
}
