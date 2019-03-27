/* Stack protector support.
   Copyright (C) 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* As a special exception, if you link this library with files compiled with
   GCC to produce an executable, this does not cause the resulting executable
   to be covered by the GNU General Public License. This exception does not
   however invalidate any other reasons why the executable file might be
   covered by the GNU General Public License.  */

#include "config.h"
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_PATHS_H
# include <paths.h>
#endif
#ifndef _PATH_TTY
# define _PATH_TTY "/dev/tty"
#endif
#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#endif

#include <stdlib.h>

void *__stack_chk_guard = 0;

static void __attribute__ ((constructor))
__guard_setup (void)
{
  unsigned char *p;
  int fd;

  if (__stack_chk_guard != 0)
    return;

  fd = open ("/dev/urandom", O_RDONLY);
  if (fd != -1)
    {
      ssize_t size = read (fd, &__stack_chk_guard,
                           sizeof (__stack_chk_guard));
      close (fd);
      if (size == sizeof(__stack_chk_guard) && __stack_chk_guard != 0)
        return;
    }

  /* If a random generator can't be used, the protector switches the guard
     to the "terminator canary".  */
  p = (unsigned char *) &__stack_chk_guard;
  p[sizeof(__stack_chk_guard)-1] = 255;
  p[sizeof(__stack_chk_guard)-2] = '\n';
  p[0] = 0;
}

static void
fail (const char *msg1, size_t msg1len, const char *msg3)
{
#ifdef __GNU_LIBRARY__
  extern char * __progname;
#else
  static const char __progname[] = "";
#endif
  int fd;

  /* Print error message directly to the tty.  This avoids Bad Things
     happening if stderr is redirected.  */
  fd = open (_PATH_TTY, O_WRONLY);
  if (fd != -1)
    {
      static const char msg2[] = " terminated\n";
      size_t progname_len, len;
      char *buf, *p;

      progname_len = strlen (__progname);
      len = msg1len + progname_len + sizeof(msg2)-1 + 1;
      p = buf = alloca (len);

      memcpy (p, msg1, msg1len);
      p += msg1len;
      memcpy (p, __progname, progname_len);
      p += progname_len;
      memcpy (p, msg2, sizeof(msg2));

      while (len > 0)
        {
          ssize_t wrote = write (fd, buf, len);
          if (wrote < 0)
            break;
          buf += wrote;
          len -= wrote;
        }
      close (fd);
    }

#ifdef HAVE_SYSLOG_H
  /* Only send the error to syslog if there was no tty available.  */
  else
    syslog (LOG_CRIT, "%s", msg3);
#endif /* HAVE_SYSLOG_H */

  /* Try very hard to exit.  Note that signals may be blocked preventing
     the first two options from working.  The use of volatile is here to
     prevent optimizers from "knowing" that __builtin_trap is called first,
     and that it doesn't return, and so "obviously" the rest of the code
     is dead.  */
  {
    volatile int state;
    for (state = 0; ; state++)
      switch (state)
        {
        case 0:
          __builtin_trap ();
          break;
        case 1:
          *(volatile int *)-1L = 0;
          break;
        case 2:
          _exit (127);
          break;
        }
  }
}

void
__stack_chk_fail (void)
{
  const char *msg = "*** stack smashing detected ***: ";
  fail (msg, strlen (msg), "stack smashing detected: terminated");
}

void
__chk_fail (void)
{
  const char *msg = "*** buffer overflow detected ***: ";
  fail (msg, strlen (msg), "buffer overflow detected: terminated");
}

#ifdef HAVE_HIDDEN_VISIBILITY
void
__attribute__((visibility ("hidden")))
__stack_chk_fail_local (void)
{
  __stack_chk_fail ();
}
#endif
