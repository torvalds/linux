/* readpass.c: The opiereadpass() library function.

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

        History:

	Modified by cmetz for OPIE 2.31. Use usleep() to delay after setting
		the terminal attributes; this might help certain buggy
		systems.
	Modified by cmetz for OPIE 2.3. Use TCSAFLUSH always.
	Modified by cmetz for OPIE 2.22. Replaced echo w/ flags.
               Really use FUNCTION.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
               Flush extraneous characters up to eol. Handle gobs of possible
               erase and kill keys if on a terminal. To do so, use RAW
               terminal I/O and handle echo ourselves. (should also help
               DOS et al portability). Fixed include order. Re-did MSDOS
	       and OS/2 includes. Set up VMIN and VTIME. Added some non-UNIX
	       portability cruft. Limit backspacing and killing. In terminal
               mode, eat random other control characters. Added eof handling.
        Created at NRL for OPIE 2.2 from opiesubr.c. Change opiestrip_crlf to
               opiestripcrlf. Don't strip to seven bits. 
*/
#include "opie_cfg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>	/* ANSI C standard library */

#ifdef unix
#include <fcntl.h>      /* POSIX file control function headers */
#include <termios.h>    /* POSIX Terminal I/O functions */ 
#if HAVE_UNISTD_H
#include <unistd.h>     /* POSIX standard definitions */
#endif /* HAVE_UNISTD_H */
#include <signal.h>
#include <setjmp.h>
#endif /* unix */

#ifdef __MSDOS__
#include <dos.h>
#endif /* __MSDOS__ */

#ifdef __OS2__
#define INCL_KBD
#include <os2.h>
#include <io.h>
#endif /* __OS2__ */

#include "opie.h"

#define CONTROL(x) (x - 64)

char *bsseq = "\b \b";

#ifdef unix
static jmp_buf jmpbuf;

static VOIDRET catch FUNCTION((i), int i)
{
  longjmp(jmpbuf, 1);
}
#endif /* unix */

char *opiereadpass FUNCTION((buf, len, flags), char *buf AND int len AND int flags)
{
#ifdef unix
  struct termios attr, orig_attr;
#endif /* unix */
  char erase[5];
  char kill[4];
  char eof[4];

  memset(erase, 0, sizeof(erase));
  memset(kill, 0, sizeof(kill));
  memset(eof, 0, sizeof(eof));

  /* This section was heavily rewritten by rja following the model of code
     samples circa page 151 of the POSIX Programmer's Guide by Donald Lewine,
     ISBN 0-937175-73-0. That book is Copyright 1991 by O'Reilly &
     Associates, Inc. All Rights Reserved. I recommend the book to anyone
     trying to write portable software. rja */

#ifdef unix
  if (setjmp(jmpbuf))
    goto error;

  signal(SIGINT, catch);
#endif /* unix */

  /* Flush any pending output */
  fflush(stderr);
  fflush(stdout);

#ifdef unix
  /* Get original terminal attributes */
  if (isatty(0)) {
    if (tcgetattr(0, &orig_attr))
      return NULL;

    /* copy terminal settings into attr */
    memcpy(&attr, &orig_attr, sizeof(struct termios));

    attr.c_lflag &= ~(ECHO | ICANON);
    attr.c_lflag |= ISIG;

    attr.c_cc[VMIN] = 1;
    attr.c_cc[VTIME] = 0;

    erase[0] = CONTROL('H');
    erase[1] = 127;

#ifdef CERASE
    {
    char *e = erase;

    while(*e)
      if (*(e++) == CERASE)
        break;

    if (!*e)
      *e = CERASE;
    }
#endif /* CERASE */
#ifdef VERASE
    {
    char *e = erase;

    while(*e)
      if (*(e++) == attr.c_cc[VERASE])
        break;

    if (!*e)
      *e = attr.c_cc[VERASE];
    }
#endif /* VERASE */

    kill[0] = CONTROL('U');
#ifdef CKILL
    {
    char *e = kill;

    while(*e)
      if (*(e++) == CKILL)
        break;

    if (!*e)
      *e = CKILL;
    }
#endif /* CKILL */
#ifdef VKILL
    {
    char *e = kill;

    while(*e)
      if (*(e++) == attr.c_cc[VKILL])
        break;

    if (!*e)
      *e = attr.c_cc[VKILL];
    }
#endif /* VKILL */

    eof[0] = CONTROL('D');
#ifdef CEOF
    {
    char *e = eof;

    while(*e)
      if (*(e++) == CEOF)
        break;

    if (!*e)
      *e = CEOF;
    }
#endif /* CEOF */
#ifdef VEOF
    {
    char *e = eof;

    while(*e)
      if (*(e++) == attr.c_cc[VEOF])
        break;

    if (!*e)
      *e = VEOF;
    }
#endif /* VEOF */

#if HAVE_USLEEP
    usleep(1);
#endif /* HAVE_USLEEP */

    if (tcsetattr(0, TCSAFLUSH, &attr))
      goto error;

#if HAVE_USLEEP
    usleep(1);
#endif /* HAVE_USLEEP */
  }
#else /* unix */
  erase[0] = CONTROL('H');
  erase[1] = 127;
  kill[0] = CONTROL('U');
  eof[0] = CONTROL('D');
  eof[1] = CONTROL('Z');
#endif /* unix */

  {
  char *c = buf, *end = buf + len, *e;
#ifdef __OS2__
  KBDKEYINFO keyInfo;
#endif /* __OS2__ */

loop:
#ifdef unix
  if (read(0, c, 1) != 1)
    goto error;
#endif /* unix */
#ifdef MSDOS
  *c = bdos(7, 0, 0);
#endif /* MSDOS */
#ifdef __OS2__
  KbdCharIn(&keyInfo, 0, 0);
  *c = keyInfo.chChar;
#endif /* __OS2__ */

  if ((*c == '\r') || (*c == '\n')) {
    *c = 0;
    goto restore;
  }

  e = eof;
  while(*e)
    if (*(e++) == *c)
      goto error;

  e = erase;
  while(*e)
    if (*(e++) == *c) {
      if (c <= buf)
	goto beep;

      if (flags & 1)
        write(1, bsseq, sizeof(bsseq) - 1);
      c--;
      goto loop;
    }

  e = kill;
  while(*e)
    if (*(e++) == *c) {
      if (c <= buf)
	goto beep;

      if (flags & 1)
        while(c-- > buf)
          write(1, bsseq, sizeof(bsseq) - 1);

      c = buf;
      goto loop;
    }

  if (c < end) {
    if (*c < 32)
      goto beep;
    if (flags & 1)
      write(1, c, 1);
    c++;
  } else {
  beep:
    *c = CONTROL('G');
    write(1, c, 1);
  }

  goto loop;
  }

restore:
#ifdef unix
  /* Restore previous tty modes */
  if (isatty(0))
    if (tcsetattr(0, TCSAFLUSH, &orig_attr))
      return NULL;

  signal(SIGINT, SIG_DFL);
#endif /* unix */

  /* After the secret key is taken from the keyboard, the line feed is
     written to standard error instead of standard output.  That means that
     anyone using the program from a terminal won't notice, but capturing
     standard output will get the key words without a newline in front of
     them. */
  if (!(flags & 4)) {
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  return buf;

error:
  *buf = 0;
  buf = NULL;
  goto restore;
}
