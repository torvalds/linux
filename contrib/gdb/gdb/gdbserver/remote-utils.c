/* Remote utility routines for the remote server for GDB.
   Copyright 1986, 1989, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004
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
#include "terminal.h"
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>

int remote_debug = 0;
struct ui_file *gdb_stdlog;

static int remote_desc;

/* FIXME headerize? */
extern int using_threads;
extern int debug_threads;

/* Open a connection to a remote debugger.
   NAME is the filename used for communication.  */

void
remote_open (char *name)
{
  int save_fcntl_flags;
  
  if (!strchr (name, ':'))
    {
      remote_desc = open (name, O_RDWR);
      if (remote_desc < 0)
	perror_with_name ("Could not open remote device");

#ifdef HAVE_TERMIOS
      {
	struct termios termios;
	tcgetattr (remote_desc, &termios);

	termios.c_iflag = 0;
	termios.c_oflag = 0;
	termios.c_lflag = 0;
	termios.c_cflag &= ~(CSIZE | PARENB);
	termios.c_cflag |= CLOCAL | CS8;
	termios.c_cc[VMIN] = 1;
	termios.c_cc[VTIME] = 0;

	tcsetattr (remote_desc, TCSANOW, &termios);
      }
#endif

#ifdef HAVE_TERMIO
      {
	struct termio termio;
	ioctl (remote_desc, TCGETA, &termio);

	termio.c_iflag = 0;
	termio.c_oflag = 0;
	termio.c_lflag = 0;
	termio.c_cflag &= ~(CSIZE | PARENB);
	termio.c_cflag |= CLOCAL | CS8;
	termio.c_cc[VMIN] = 1;
	termio.c_cc[VTIME] = 0;

	ioctl (remote_desc, TCSETA, &termio);
      }
#endif

#ifdef HAVE_SGTTY
      {
	struct sgttyb sg;

	ioctl (remote_desc, TIOCGETP, &sg);
	sg.sg_flags = RAW;
	ioctl (remote_desc, TIOCSETP, &sg);
      }
#endif

      fprintf (stderr, "Remote debugging using %s\n", name);
    }
  else
    {
      char *port_str;
      int port;
      struct sockaddr_in sockaddr;
      int tmp;
      int tmp_desc;

      port_str = strchr (name, ':');

      port = atoi (port_str + 1);

      tmp_desc = socket (PF_INET, SOCK_STREAM, 0);
      if (tmp_desc < 0)
	perror_with_name ("Can't open socket");

      /* Allow rapid reuse of this port. */
      tmp = 1;
      setsockopt (tmp_desc, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp,
		  sizeof (tmp));

      sockaddr.sin_family = PF_INET;
      sockaddr.sin_port = htons (port);
      sockaddr.sin_addr.s_addr = INADDR_ANY;

      if (bind (tmp_desc, (struct sockaddr *) &sockaddr, sizeof (sockaddr))
	  || listen (tmp_desc, 1))
	perror_with_name ("Can't bind address");

      fprintf (stderr, "Listening on port %d\n", port);

      tmp = sizeof (sockaddr);
      remote_desc = accept (tmp_desc, (struct sockaddr *) &sockaddr, &tmp);
      if (remote_desc == -1)
	perror_with_name ("Accept failed");

      /* Enable TCP keep alive process. */
      tmp = 1;
      setsockopt (tmp_desc, SOL_SOCKET, SO_KEEPALIVE, (char *) &tmp, sizeof (tmp));

      /* Tell TCP not to delay small packets.  This greatly speeds up
         interactive response. */
      tmp = 1;
      setsockopt (remote_desc, IPPROTO_TCP, TCP_NODELAY,
		  (char *) &tmp, sizeof (tmp));

      close (tmp_desc);		/* No longer need this */

      signal (SIGPIPE, SIG_IGN);	/* If we don't do this, then gdbserver simply
					   exits when the remote side dies.  */

      /* Convert IP address to string.  */
      fprintf (stderr, "Remote debugging from host %s\n", 
         inet_ntoa (sockaddr.sin_addr));
    }

#if defined(F_SETFL) && defined (FASYNC)
  save_fcntl_flags = fcntl (remote_desc, F_GETFL, 0);
  fcntl (remote_desc, F_SETFL, save_fcntl_flags | FASYNC);
#if defined (F_SETOWN)
  fcntl (remote_desc, F_SETOWN, getpid ());
#endif
#endif
  disable_async_io ();
}

void
remote_close (void)
{
  close (remote_desc);
}

/* Convert hex digit A to a number.  */

static int
fromhex (int a)
{
  if (a >= '0' && a <= '9')
    return a - '0';
  else if (a >= 'a' && a <= 'f')
    return a - 'a' + 10;
  else
    error ("Reply contains invalid hex digit");
  return 0;
}

int
unhexify (char *bin, const char *hex, int count)
{
  int i;

  for (i = 0; i < count; i++)
    {
      if (hex[0] == 0 || hex[1] == 0)
        {
          /* Hex string is short, or of uneven length.
             Return the count that has been converted so far. */
          return i;
        }
      *bin++ = fromhex (hex[0]) * 16 + fromhex (hex[1]);
      hex += 2;
    }
  return i;
}

static void
decode_address (CORE_ADDR *addrp, const char *start, int len)
{
  CORE_ADDR addr;
  char ch;
  int i;

  addr = 0;
  for (i = 0; i < len; i++)
    {
      ch = start[i];
      addr = addr << 4;
      addr = addr | (fromhex (ch) & 0x0f);
    }
  *addrp = addr;
}

/* Convert number NIB to a hex digit.  */

static int
tohex (int nib)
{
  if (nib < 10)
    return '0' + nib;
  else
    return 'a' + nib - 10;
}

int
hexify (char *hex, const char *bin, int count)
{
  int i;

  /* May use a length, or a nul-terminated string as input. */
  if (count == 0)
    count = strlen (bin);

  for (i = 0; i < count; i++)
    {
      *hex++ = tohex ((*bin >> 4) & 0xf);
      *hex++ = tohex (*bin++ & 0xf);
    }
  *hex = 0;
  return i;
}

/* Send a packet to the remote machine, with error checking.
   The data of the packet is in BUF.  Returns >= 0 on success, -1 otherwise. */

int
putpkt (char *buf)
{
  int i;
  unsigned char csum = 0;
  char *buf2;
  char buf3[1];
  int cnt = strlen (buf);
  char *p;

  buf2 = malloc (PBUFSIZ);

  /* Copy the packet into buffer BUF2, encapsulating it
     and giving it a checksum.  */

  p = buf2;
  *p++ = '$';

  for (i = 0; i < cnt; i++)
    {
      csum += buf[i];
      *p++ = buf[i];
    }
  *p++ = '#';
  *p++ = tohex ((csum >> 4) & 0xf);
  *p++ = tohex (csum & 0xf);

  *p = '\0';

  /* Send it over and over until we get a positive ack.  */

  do
    {
      int cc;

      if (write (remote_desc, buf2, p - buf2) != p - buf2)
	{
	  perror ("putpkt(write)");
	  return -1;
	}

      if (remote_debug)
	{
	  fprintf (stderr, "putpkt (\"%s\"); [looking for ack]\n", buf2);
	  fflush (stderr);
	}
      cc = read (remote_desc, buf3, 1);
      if (remote_debug)
	{
	  fprintf (stderr, "[received '%c' (0x%x)]\n", buf3[0], buf3[0]);
	  fflush (stderr);
	}

      if (cc <= 0)
	{
	  if (cc == 0)
	    fprintf (stderr, "putpkt(read): Got EOF\n");
	  else
	    perror ("putpkt(read)");

	  free (buf2);
	  return -1;
	}

      /* Check for an input interrupt while we're here.  */
      if (buf3[0] == '\003')
	(*the_target->send_signal) (SIGINT);
    }
  while (buf3[0] != '+');

  free (buf2);
  return 1;			/* Success! */
}

/* Come here when we get an input interrupt from the remote side.  This
   interrupt should only be active while we are waiting for the child to do
   something.  About the only thing that should come through is a ^C, which
   will cause us to send a SIGINT to the child.  */

static void
input_interrupt (int unused)
{
  fd_set readset;
  struct timeval immediate = { 0, 0 };

  /* Protect against spurious interrupts.  This has been observed to
     be a problem under NetBSD 1.4 and 1.5.  */

  FD_ZERO (&readset);
  FD_SET (remote_desc, &readset);
  if (select (remote_desc + 1, &readset, 0, 0, &immediate) > 0)
    {
      int cc;
      char c;
      
      cc = read (remote_desc, &c, 1);

      if (cc != 1 || c != '\003')
	{
	  fprintf (stderr, "input_interrupt, cc = %d c = %d\n", cc, c);
	  return;
	}
      
      (*the_target->send_signal) (SIGINT);
    }
}

void
block_async_io (void)
{
  sigset_t sigio_set;
  sigemptyset (&sigio_set);
  sigaddset (&sigio_set, SIGIO);
  sigprocmask (SIG_BLOCK, &sigio_set, NULL);
}

void
unblock_async_io (void)
{
  sigset_t sigio_set;
  sigemptyset (&sigio_set);
  sigaddset (&sigio_set, SIGIO);
  sigprocmask (SIG_UNBLOCK, &sigio_set, NULL);
}

void
enable_async_io (void)
{
  signal (SIGIO, input_interrupt);
}

void
disable_async_io (void)
{
  signal (SIGIO, SIG_IGN);
}

/* Returns next char from remote GDB.  -1 if error.  */

static int
readchar (void)
{
  static char buf[BUFSIZ];
  static int bufcnt = 0;
  static char *bufp;

  if (bufcnt-- > 0)
    return *bufp++ & 0x7f;

  bufcnt = read (remote_desc, buf, sizeof (buf));

  if (bufcnt <= 0)
    {
      if (bufcnt == 0)
	fprintf (stderr, "readchar: Got EOF\n");
      else
	perror ("readchar");

      return -1;
    }

  bufp = buf;
  bufcnt--;
  return *bufp++ & 0x7f;
}

/* Read a packet from the remote machine, with error checking,
   and store it in BUF.  Returns length of packet, or negative if error. */

int
getpkt (char *buf)
{
  char *bp;
  unsigned char csum, c1, c2;
  int c;

  while (1)
    {
      csum = 0;

      while (1)
	{
	  c = readchar ();
	  if (c == '$')
	    break;
	  if (remote_debug)
	    {
	      fprintf (stderr, "[getpkt: discarding char '%c']\n", c);
	      fflush (stderr);
	    }

	  if (c < 0)
	    return -1;
	}

      bp = buf;
      while (1)
	{
	  c = readchar ();
	  if (c < 0)
	    return -1;
	  if (c == '#')
	    break;
	  *bp++ = c;
	  csum += c;
	}
      *bp = 0;

      c1 = fromhex (readchar ());
      c2 = fromhex (readchar ());

      if (csum == (c1 << 4) + c2)
	break;

      fprintf (stderr, "Bad checksum, sentsum=0x%x, csum=0x%x, buf=%s\n",
	       (c1 << 4) + c2, csum, buf);
      write (remote_desc, "-", 1);
    }

  if (remote_debug)
    {
      fprintf (stderr, "getpkt (\"%s\");  [sending ack] \n", buf);
      fflush (stderr);
    }

  write (remote_desc, "+", 1);

  if (remote_debug)
    {
      fprintf (stderr, "[sent ack]\n");
      fflush (stderr);
    }

  return bp - buf;
}

void
write_ok (char *buf)
{
  buf[0] = 'O';
  buf[1] = 'K';
  buf[2] = '\0';
}

void
write_enn (char *buf)
{
  /* Some day, we should define the meanings of the error codes... */
  buf[0] = 'E';
  buf[1] = '0';
  buf[2] = '1';
  buf[3] = '\0';
}

void
convert_int_to_ascii (char *from, char *to, int n)
{
  int nib;
  char ch;
  while (n--)
    {
      ch = *from++;
      nib = ((ch & 0xf0) >> 4) & 0x0f;
      *to++ = tohex (nib);
      nib = ch & 0x0f;
      *to++ = tohex (nib);
    }
  *to++ = 0;
}


void
convert_ascii_to_int (char *from, char *to, int n)
{
  int nib1, nib2;
  while (n--)
    {
      nib1 = fromhex (*from++);
      nib2 = fromhex (*from++);
      *to++ = (((nib1 & 0x0f) << 4) & 0xf0) | (nib2 & 0x0f);
    }
}

static char *
outreg (int regno, char *buf)
{
  if ((regno >> 12) != 0)
    *buf++ = tohex ((regno >> 12) & 0xf);
  if ((regno >> 8) != 0)
    *buf++ = tohex ((regno >> 8) & 0xf);
  *buf++ = tohex ((regno >> 4) & 0xf);
  *buf++ = tohex (regno & 0xf);
  *buf++ = ':';
  collect_register_as_string (regno, buf);
  buf += 2 * register_size (regno);
  *buf++ = ';';

  return buf;
}

void
new_thread_notify (int id)
{
  char own_buf[256];

  /* The `n' response is not yet part of the remote protocol.  Do nothing.  */
  if (1)
    return;

  if (server_waiting == 0)
    return;

  sprintf (own_buf, "n%x", id);
  disable_async_io ();
  putpkt (own_buf);
  enable_async_io ();
}

void
dead_thread_notify (int id)
{
  char own_buf[256];

  /* The `x' response is not yet part of the remote protocol.  Do nothing.  */
  if (1)
    return;

  sprintf (own_buf, "x%x", id);
  disable_async_io ();
  putpkt (own_buf);
  enable_async_io ();
}

void
prepare_resume_reply (char *buf, char status, unsigned char signo)
{
  int nib, sig;

  *buf++ = status;

  sig = (int)target_signal_from_host (signo);

  nib = ((sig & 0xf0) >> 4);
  *buf++ = tohex (nib);
  nib = sig & 0x0f;
  *buf++ = tohex (nib);

  if (status == 'T')
    {
      const char **regp = gdbserver_expedite_regs;
      while (*regp)
	{
	  buf = outreg (find_regno (*regp), buf);
	  regp ++;
	}

      /* Formerly, if the debugger had not used any thread features we would not
	 burden it with a thread status response.  This was for the benefit of
	 GDB 4.13 and older.  However, in recent GDB versions the check
	 (``if (cont_thread != 0)'') does not have the desired effect because of
	 sillyness in the way that the remote protocol handles specifying a thread.
	 Since thread support relies on qSymbol support anyway, assume GDB can handle
	 threads.  */

      if (using_threads)
	{
	  /* FIXME right place to set this? */
	  thread_from_wait = ((struct inferior_list_entry *)current_inferior)->id;
	  if (debug_threads)
	    fprintf (stderr, "Writing resume reply for %d\n\n", thread_from_wait);
	  /* This if (1) ought to be unnecessary.  But remote_wait in GDB
	     will claim this event belongs to inferior_ptid if we do not
	     specify a thread, and there's no way for gdbserver to know
	     what inferior_ptid is.  */
	  if (1 || old_thread_from_wait != thread_from_wait)
	    {
	      general_thread = thread_from_wait;
	      sprintf (buf, "thread:%x;", thread_from_wait);
	      buf += strlen (buf);
	      old_thread_from_wait = thread_from_wait;
	    }
	}
    }
  /* For W and X, we're done.  */
  *buf++ = 0;
}

void
decode_m_packet (char *from, CORE_ADDR *mem_addr_ptr, unsigned int *len_ptr)
{
  int i = 0, j = 0;
  char ch;
  *mem_addr_ptr = *len_ptr = 0;

  while ((ch = from[i++]) != ',')
    {
      *mem_addr_ptr = *mem_addr_ptr << 4;
      *mem_addr_ptr |= fromhex (ch) & 0x0f;
    }

  for (j = 0; j < 4; j++)
    {
      if ((ch = from[i++]) == 0)
	break;
      *len_ptr = *len_ptr << 4;
      *len_ptr |= fromhex (ch) & 0x0f;
    }
}

void
decode_M_packet (char *from, CORE_ADDR *mem_addr_ptr, unsigned int *len_ptr,
		 char *to)
{
  int i = 0;
  char ch;
  *mem_addr_ptr = *len_ptr = 0;

  while ((ch = from[i++]) != ',')
    {
      *mem_addr_ptr = *mem_addr_ptr << 4;
      *mem_addr_ptr |= fromhex (ch) & 0x0f;
    }

  while ((ch = from[i++]) != ':')
    {
      *len_ptr = *len_ptr << 4;
      *len_ptr |= fromhex (ch) & 0x0f;
    }

  convert_ascii_to_int (&from[i++], to, *len_ptr);
}

int
look_up_one_symbol (const char *name, CORE_ADDR *addrp)
{
  char own_buf[266], *p, *q;
  int len;

  /* Send the request.  */
  strcpy (own_buf, "qSymbol:");
  hexify (own_buf + strlen ("qSymbol:"), name, strlen (name));
  if (putpkt (own_buf) < 0)
    return -1;

  /* FIXME:  Eventually add buffer overflow checking (to getpkt?)  */
  len = getpkt (own_buf);
  if (len < 0)
    return -1;

  if (strncmp (own_buf, "qSymbol:", strlen ("qSymbol:")) != 0)
    {
      /* Malformed response.  */
      if (remote_debug)
	{
	  fprintf (stderr, "Malformed response to qSymbol, ignoring.\n");
	  fflush (stderr);
	}

      return -1;
    }

  p = own_buf + strlen ("qSymbol:");
  q = p;
  while (*q && *q != ':')
    q++;

  /* Make sure we found a value for the symbol.  */
  if (p == q || *q == '\0')
    return 0;

  decode_address (addrp, p, q - p);
  return 1;
}

