/* Main code for remote server for GDB.
   Copyright 1989, 1993, 1994, 1995, 1997, 1998, 1999, 2000, 2002, 2003, 2004
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

#include "server.h"

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int cont_thread;
int general_thread;
int step_thread;
int thread_from_wait;
int old_thread_from_wait;
int extended_protocol;
int server_waiting;

jmp_buf toplevel;

/* The PID of the originally created or attached inferior.  Used to
   send signals to the process when GDB sends us an asynchronous interrupt
   (user hitting Control-C in the client), and to wait for the child to exit
   when no longer debugging it.  */

int signal_pid;

static unsigned char
start_inferior (char *argv[], char *statusptr)
{
  signal (SIGTTOU, SIG_DFL);
  signal (SIGTTIN, SIG_DFL);

  signal_pid = create_inferior (argv[0], argv);

  fprintf (stderr, "Process %s created; pid = %d\n", argv[0],
	   signal_pid);

  signal (SIGTTOU, SIG_IGN);
  signal (SIGTTIN, SIG_IGN);
  tcsetpgrp (fileno (stderr), signal_pid);

  /* Wait till we are at 1st instruction in program, return signal number.  */
  return mywait (statusptr, 0);
}

static int
attach_inferior (int pid, char *statusptr, unsigned char *sigptr)
{
  /* myattach should return -1 if attaching is unsupported,
     0 if it succeeded, and call error() otherwise.  */

  if (myattach (pid) != 0)
    return -1;

  fprintf (stderr, "Attached; pid = %d\n", pid);

  /* FIXME - It may be that we should get the SIGNAL_PID from the
     attach function, so that it can be the main thread instead of
     whichever we were told to attach to.  */
  signal_pid = pid;

  *sigptr = mywait (statusptr, 0);

  return 0;
}

extern int remote_debug;

/* Handle all of the extended 'q' packets.  */
void
handle_query (char *own_buf)
{
  static struct inferior_list_entry *thread_ptr;

  if (strcmp ("qSymbol::", own_buf) == 0)
    {
      if (the_target->look_up_symbols != NULL)
	(*the_target->look_up_symbols) ();

      strcpy (own_buf, "OK");
      return;
    }

  if (strcmp ("qfThreadInfo", own_buf) == 0)
    {
      thread_ptr = all_threads.head;
      sprintf (own_buf, "m%x", thread_ptr->id);
      thread_ptr = thread_ptr->next;
      return;
    }

  if (strcmp ("qsThreadInfo", own_buf) == 0)
    {
      if (thread_ptr != NULL)
	{
	  sprintf (own_buf, "m%x", thread_ptr->id);
	  thread_ptr = thread_ptr->next;
	  return;
	}
      else
	{
	  sprintf (own_buf, "l");
	  return;
	}
    }

  if (the_target->read_auxv != NULL
      && strncmp ("qPart:auxv:read::", own_buf, 17) == 0)
    {
      char data[(PBUFSIZ - 1) / 2];
      CORE_ADDR ofs;
      unsigned int len;
      int n;
      decode_m_packet (&own_buf[17], &ofs, &len); /* "OFS,LEN" */
      if (len > sizeof data)
	len = sizeof data;
      n = (*the_target->read_auxv) (ofs, data, len);
      if (n == 0)
	write_ok (own_buf);
      else if (n < 0)
	write_enn (own_buf);
      else
	convert_int_to_ascii (data, own_buf, n);
      return;
    }

  /* Otherwise we didn't know what packet it was.  Say we didn't
     understand it.  */
  own_buf[0] = 0;
}

/* Parse vCont packets.  */
void
handle_v_cont (char *own_buf, char *status, unsigned char *signal)
{
  char *p, *q;
  int n = 0, i = 0;
  struct thread_resume *resume_info, default_action;

  /* Count the number of semicolons in the packet.  There should be one
     for every action.  */
  p = &own_buf[5];
  while (p)
    {
      n++;
      p++;
      p = strchr (p, ';');
    }
  /* Allocate room for one extra action, for the default remain-stopped
     behavior; if no default action is in the list, we'll need the extra
     slot.  */
  resume_info = malloc ((n + 1) * sizeof (resume_info[0]));

  default_action.thread = -1;
  default_action.leave_stopped = 1;
  default_action.step = 0;
  default_action.sig = 0;

  p = &own_buf[5];
  i = 0;
  while (*p)
    {
      p++;

      resume_info[i].leave_stopped = 0;

      if (p[0] == 's' || p[0] == 'S')
	resume_info[i].step = 1;
      else if (p[0] == 'c' || p[0] == 'C')
	resume_info[i].step = 0;
      else
	goto err;

      if (p[0] == 'S' || p[0] == 'C')
	{
	  int sig;
	  sig = strtol (p + 1, &q, 16);
	  if (p == q)
	    goto err;
	  p = q;

	  if (!target_signal_to_host_p (sig))
	    goto err;
	  resume_info[i].sig = target_signal_to_host (sig);
	}
      else
	{
	  resume_info[i].sig = 0;
	  p = p + 1;
	}

      if (p[0] == 0)
	{
	  resume_info[i].thread = -1;
	  default_action = resume_info[i];

	  /* Note: we don't increment i here, we'll overwrite this entry
	     the next time through.  */
	}
      else if (p[0] == ':')
	{
	  resume_info[i].thread = strtol (p + 1, &q, 16);
	  if (p == q)
	    goto err;
	  p = q;
	  if (p[0] != ';' && p[0] != 0)
	    goto err;

	  i++;
	}
    }

  resume_info[i] = default_action;

  /* Still used in occasional places in the backend.  */
  if (n == 1 && resume_info[0].thread != -1)
    cont_thread = resume_info[0].thread;
  else
    cont_thread = -1;
  set_desired_inferior (0);

  (*the_target->resume) (resume_info);

  free (resume_info);

  *signal = mywait (status, 1);
  prepare_resume_reply (own_buf, *status, *signal);
  return;

err:
  /* No other way to report an error... */
  strcpy (own_buf, "");
  free (resume_info);
  return;
}

/* Handle all of the extended 'v' packets.  */
void
handle_v_requests (char *own_buf, char *status, unsigned char *signal)
{
  if (strncmp (own_buf, "vCont;", 6) == 0)
    {
      handle_v_cont (own_buf, status, signal);
      return;
    }

  if (strncmp (own_buf, "vCont?", 6) == 0)
    {
      strcpy (own_buf, "vCont;c;C;s;S");
      return;
    }

  /* Otherwise we didn't know what packet it was.  Say we didn't
     understand it.  */
  own_buf[0] = 0;
  return;
}

void
myresume (int step, int sig)
{
  struct thread_resume resume_info[2];
  int n = 0;

  if (step || sig || cont_thread > 0)
    {
      resume_info[0].thread
	= ((struct inferior_list_entry *) current_inferior)->id;
      resume_info[0].step = step;
      resume_info[0].sig = sig;
      resume_info[0].leave_stopped = 0;
      n++;
    }
  resume_info[n].thread = -1;
  resume_info[n].step = 0;
  resume_info[n].sig = 0;
  resume_info[n].leave_stopped = (cont_thread > 0);

  (*the_target->resume) (resume_info);
}

static int attached;

static void
gdbserver_usage (void)
{
  error ("Usage:\tgdbserver COMM PROG [ARGS ...]\n"
	 "\tgdbserver COMM --attach PID\n"
	 "\n"
	 "COMM may either be a tty device (for serial debugging), or \n"
	 "HOST:PORT to listen for a TCP connection.\n");
}

int
main (int argc, char *argv[])
{
  char ch, status, *own_buf, mem_buf[2000];
  int i = 0;
  unsigned char signal;
  unsigned int len;
  CORE_ADDR mem_addr;
  int bad_attach;
  int pid;
  char *arg_end;

  if (setjmp (toplevel))
    {
      fprintf (stderr, "Exiting\n");
      exit (1);
    }

  bad_attach = 0;
  pid = 0;
  attached = 0;
  if (argc >= 3 && strcmp (argv[2], "--attach") == 0)
    {
      if (argc == 4
	  && argv[3] != '\0'
	  && (pid = strtoul (argv[3], &arg_end, 10)) != 0
	  && *arg_end == '\0')
	{
	  ;
	}
      else
	bad_attach = 1;
    }

  if (argc < 3 || bad_attach)
    gdbserver_usage();

  initialize_low ();

  own_buf = malloc (PBUFSIZ);

  if (pid == 0)
    {
      /* Wait till we are at first instruction in program.  */
      signal = start_inferior (&argv[2], &status);

      /* We are now stopped at the first instruction of the target process */
    }
  else
    {
      switch (attach_inferior (pid, &status, &signal))
	{
	case -1:
	  error ("Attaching not supported on this target");
	  break;
	default:
	  attached = 1;
	  break;
	}
    }

  while (1)
    {
      remote_open (argv[1]);

    restart:
      setjmp (toplevel);
      while (getpkt (own_buf) > 0)
	{
	  unsigned char sig;
	  i = 0;
	  ch = own_buf[i++];
	  switch (ch)
	    {
	    case 'q':
	      handle_query (own_buf);
	      break;
	    case 'd':
	      remote_debug = !remote_debug;
	      break;
	    case 'D':
	      fprintf (stderr, "Detaching from inferior\n");
	      detach_inferior ();
	      write_ok (own_buf);
	      putpkt (own_buf);
	      remote_close ();

	      /* If we are attached, then we can exit.  Otherwise, we need to
		 hang around doing nothing, until the child is gone.  */
	      if (!attached)
		{
		  int status, ret;

		  do {
		    ret = waitpid (signal_pid, &status, 0);
		    if (WIFEXITED (status) || WIFSIGNALED (status))
		      break;
		  } while (ret != -1 || errno != ECHILD);
		}

	      exit (0);

	    case '!':
	      if (attached == 0)
		{
		  extended_protocol = 1;
		  prepare_resume_reply (own_buf, status, signal);
		}
	      else
		{
		  /* We can not use the extended protocol if we are
		     attached, because we can not restart the running
		     program.  So return unrecognized.  */
		  own_buf[0] = '\0';
		}
	      break;
	    case '?':
	      prepare_resume_reply (own_buf, status, signal);
	      break;
	    case 'H':
	      switch (own_buf[1])
		{
		case 'g':
		  general_thread = strtol (&own_buf[2], NULL, 16);
		  write_ok (own_buf);
		  set_desired_inferior (1);
		  break;
		case 'c':
		  cont_thread = strtol (&own_buf[2], NULL, 16);
		  write_ok (own_buf);
		  break;
		case 's':
		  step_thread = strtol (&own_buf[2], NULL, 16);
		  write_ok (own_buf);
		  break;
		default:
		  /* Silently ignore it so that gdb can extend the protocol
		     without compatibility headaches.  */
		  own_buf[0] = '\0';
		  break;
		}
	      break;
	    case 'g':
	      set_desired_inferior (1);
	      registers_to_string (own_buf);
	      break;
	    case 'G':
	      set_desired_inferior (1);
	      registers_from_string (&own_buf[1]);
	      write_ok (own_buf);
	      break;
	    case 'm':
	      decode_m_packet (&own_buf[1], &mem_addr, &len);
	      if (read_inferior_memory (mem_addr, mem_buf, len) == 0)
		convert_int_to_ascii (mem_buf, own_buf, len);
	      else
		write_enn (own_buf);
	      break;
	    case 'M':
	      decode_M_packet (&own_buf[1], &mem_addr, &len, mem_buf);
	      if (write_inferior_memory (mem_addr, mem_buf, len) == 0)
		write_ok (own_buf);
	      else
		write_enn (own_buf);
	      break;
	    case 'C':
	      convert_ascii_to_int (own_buf + 1, &sig, 1);
	      if (target_signal_to_host_p (sig))
		signal = target_signal_to_host (sig);
	      else
		signal = 0;
	      set_desired_inferior (0);
	      myresume (0, signal);
	      signal = mywait (&status, 1);
	      prepare_resume_reply (own_buf, status, signal);
	      break;
	    case 'S':
	      convert_ascii_to_int (own_buf + 1, &sig, 1);
	      if (target_signal_to_host_p (sig))
		signal = target_signal_to_host (sig);
	      else
		signal = 0;
	      set_desired_inferior (0);
	      myresume (1, signal);
	      signal = mywait (&status, 1);
	      prepare_resume_reply (own_buf, status, signal);
	      break;
	    case 'c':
	      set_desired_inferior (0);
	      myresume (0, 0);
	      signal = mywait (&status, 1);
	      prepare_resume_reply (own_buf, status, signal);
	      break;
	    case 's':
	      set_desired_inferior (0);
	      myresume (1, 0);
	      signal = mywait (&status, 1);
	      prepare_resume_reply (own_buf, status, signal);
	      break;
	    case 'k':
	      fprintf (stderr, "Killing inferior\n");
	      kill_inferior ();
	      /* When using the extended protocol, we start up a new
	         debugging session.   The traditional protocol will
	         exit instead.  */
	      if (extended_protocol)
		{
		  write_ok (own_buf);
		  fprintf (stderr, "GDBserver restarting\n");

		  /* Wait till we are at 1st instruction in prog.  */
		  signal = start_inferior (&argv[2], &status);
		  goto restart;
		  break;
		}
	      else
		{
		  exit (0);
		  break;
		}
	    case 'T':
	      if (mythread_alive (strtol (&own_buf[1], NULL, 16)))
		write_ok (own_buf);
	      else
		write_enn (own_buf);
	      break;
	    case 'R':
	      /* Restarting the inferior is only supported in the
	         extended protocol.  */
	      if (extended_protocol)
		{
		  kill_inferior ();
		  write_ok (own_buf);
		  fprintf (stderr, "GDBserver restarting\n");

		  /* Wait till we are at 1st instruction in prog.  */
		  signal = start_inferior (&argv[2], &status);
		  goto restart;
		  break;
		}
	      else
		{
		  /* It is a request we don't understand.  Respond with an
		     empty packet so that gdb knows that we don't support this
		     request.  */
		  own_buf[0] = '\0';
		  break;
		}
	    case 'v':
	      /* Extended (long) request.  */
	      handle_v_requests (own_buf, &status, &signal);
	      break;
	    default:
	      /* It is a request we don't understand.  Respond with an
	         empty packet so that gdb knows that we don't support this
	         request.  */
	      own_buf[0] = '\0';
	      break;
	    }

	  putpkt (own_buf);

	  if (status == 'W')
	    fprintf (stderr,
		     "\nChild exited with status %d\n", signal);
	  if (status == 'X')
	    fprintf (stderr, "\nChild terminated with signal = 0x%x\n",
		     signal);
	  if (status == 'W' || status == 'X')
	    {
	      if (extended_protocol)
		{
		  fprintf (stderr, "Killing inferior\n");
		  kill_inferior ();
		  write_ok (own_buf);
		  fprintf (stderr, "GDBserver restarting\n");

		  /* Wait till we are at 1st instruction in prog.  */
		  signal = start_inferior (&argv[2], &status);
		  goto restart;
		  break;
		}
	      else
		{
		  fprintf (stderr, "GDBserver exiting\n");
		  exit (0);
		}
	    }
	}

      /* We come here when getpkt fails.

         For the extended remote protocol we exit (and this is the only
         way we gracefully exit!).

         For the traditional remote protocol close the connection,
         and re-open it at the top of the loop.  */
      if (extended_protocol)
	{
	  remote_close ();
	  exit (0);
	}
      else
	{
	  fprintf (stderr, "Remote side has terminated connection.  "
			   "GDBserver will reopen the connection.\n");
	  remote_close ();
	}
    }
}
