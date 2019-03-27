/* Pexecute test program,
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@airs.com>.

   This file is part of GNU libiberty.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. 
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <sys/types.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifndef WIFSIGNALED
#define WIFSIGNALED(S) (((S) & 0xff) != 0 && ((S) & 0xff) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(S) ((S) & 0x7f)
#endif
#ifndef WIFEXITED
#define WIFEXITED(S) (((S) & 0xff) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(S) (((S) & 0xff00) >> 8)
#endif
#ifndef WSTOPSIG
#define WSTOPSIG WEXITSTATUS
#endif
#ifndef WCOREDUMP
#define WCOREDUMP(S) ((S) & WCOREFLG)
#endif
#ifndef WCOREFLG
#define WCOREFLG 0200
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

/* When this program is run with no arguments, it runs some tests of
   the libiberty pexecute functions.  As a test program, it simply
   invokes itself with various arguments.

   argv[1]:
     *empty string*      Run tests, exit with success status
     exit                Exit success
     error               Exit error
     abort               Abort
     echo                Echo remaining arguments, exit success
     echoerr             Echo next arg to stdout, next to stderr, repeat
     copy                Copy stdin to stdout
     write               Write stdin to file named in next argument
*/

static void fatal_error (int, const char *, int) ATTRIBUTE_NORETURN;
static void error (int, const char *);
static void check_line (int, FILE *, const char *);
static void do_cmd (int, char **) ATTRIBUTE_NORETURN;

/* The number of errors we have seen.  */

static int error_count;

/* Print a fatal error and exit.  LINE is the line number where we
   detected the error, ERRMSG is the error message to print, and ERR
   is 0 or an errno value to print.  */

static void
fatal_error (int line, const char *errmsg, int err)
{
  fprintf (stderr, "test-pexecute:%d: %s", line, errmsg);
  if (errno != 0)
    fprintf (stderr, ": %s", xstrerror (err));
  fprintf (stderr, "\n");
  exit (EXIT_FAILURE);
}

#define FATAL_ERROR(ERRMSG, ERR) fatal_error (__LINE__, ERRMSG, ERR)

/* Print an error message and bump the error count.  LINE is the line
   number where we detected the error, ERRMSG is the error to
   print.  */

static void
error (int line, const char *errmsg)
{
  fprintf (stderr, "test-pexecute:%d: %s\n", line, errmsg);
  ++error_count;
}

#define ERROR(ERRMSG) error (__LINE__, ERRMSG)

/* Check a line in a file.  */

static void
check_line (int line, FILE *e, const char *str)
{
  const char *p;
  int c;
  char buf[1000];

  p = str;
  while (1)
    {
      c = getc (e);

      if (*p == '\0')
	{
	  if (c != '\n')
	    {
	      snprintf (buf, sizeof buf, "got '%c' when expecting newline", c);
	      fatal_error (line, buf, 0);
	    }
	  c = getc (e);
	  if (c != EOF)
	    {
	      snprintf (buf, sizeof buf, "got '%c' when expecting EOF", c);
	      fatal_error (line, buf, 0);
	    }
	  return;
	}

      if (c != *p)
	{
	  snprintf (buf, sizeof buf, "expected '%c', got '%c'", *p, c);
	  fatal_error (line, buf, 0);
	}

      ++p;
    }
}

#define CHECK_LINE(E, STR) check_line (__LINE__, E, STR)

/* Main function for the pexecute tester.  Run the tests.  */

int
main (int argc, char **argv)
{
  int trace;
  struct pex_obj *test_pex_tmp;
  int test_pex_status;
  FILE *test_pex_file;
  struct pex_obj *pex1;
  char *subargv[10];
  int status;
  FILE *e;
  int statuses[10];

  trace = 0;
  if (argc > 1 && strcmp (argv[1], "-t") == 0)
    {
      trace = 1;
      --argc;
      ++argv;
    }

  if (argc > 1)
    do_cmd (argc, argv);

#define TEST_PEX_INIT(FLAGS, TEMPBASE)					\
  (((test_pex_tmp = pex_init (FLAGS, "test-pexecute", TEMPBASE))	\
    != NULL)								\
   ? test_pex_tmp							\
   : (FATAL_ERROR ("pex_init failed", 0), NULL))

#define TEST_PEX_RUN(PEXOBJ, FLAGS, EXECUTABLE, ARGV, OUTNAME, ERRNAME)	\
  do									\
    {									\
      int err;								\
      const char *pex_run_err;						\
      if (trace)							\
	fprintf (stderr, "Line %d: running %s %s\n",			\
		 __LINE__, EXECUTABLE, ARGV[0]);			\
      pex_run_err = pex_run (PEXOBJ, FLAGS, EXECUTABLE, ARGV, OUTNAME,	\
			     ERRNAME, &err);				\
      if (pex_run_err != NULL)						\
	FATAL_ERROR (pex_run_err, err);					\
    }									\
  while (0)

#define TEST_PEX_GET_STATUS_1(PEXOBJ)					\
  (pex_get_status (PEXOBJ, 1, &test_pex_status)				\
   ? test_pex_status							\
   : (FATAL_ERROR ("pex_get_status failed", errno), 1))

#define TEST_PEX_GET_STATUS(PEXOBJ, COUNT, VECTOR)			\
  do									\
    {									\
      if (!pex_get_status (PEXOBJ, COUNT, VECTOR))			\
	FATAL_ERROR ("pex_get_status failed", errno);			\
    }									\
  while (0)

#define TEST_PEX_READ_OUTPUT(PEXOBJ)					\
  ((test_pex_file = pex_read_output (PEXOBJ, 0)) != NULL		\
   ? test_pex_file							\
   : (FATAL_ERROR ("pex_read_output failed", errno), NULL))

  remove ("temp.x");
  remove ("temp.y");

  memset (subargv, 0, sizeof subargv);

  subargv[0] = "./test-pexecute";

  pex1 = TEST_PEX_INIT (PEX_USE_PIPES, NULL);
  subargv[1] = "exit";
  subargv[2] = NULL;
  TEST_PEX_RUN (pex1, PEX_LAST, "./test-pexecute", subargv, NULL, NULL);
  status = TEST_PEX_GET_STATUS_1 (pex1);
  if (!WIFEXITED (status) || WEXITSTATUS (status) != EXIT_SUCCESS)
    ERROR ("exit failed");
  pex_free (pex1);

  pex1 = TEST_PEX_INIT (PEX_USE_PIPES, NULL);
  subargv[1] = "error";
  subargv[2] = NULL;
  TEST_PEX_RUN (pex1, PEX_LAST, "./test-pexecute", subargv, NULL, NULL);
  status = TEST_PEX_GET_STATUS_1 (pex1);
  if (!WIFEXITED (status) || WEXITSTATUS (status) != EXIT_FAILURE)
    ERROR ("error test failed");
  pex_free (pex1);

  /* We redirect stderr to a file to avoid an error message which is
     printed on mingw32 when the child calls abort.  */
  pex1 = TEST_PEX_INIT (PEX_USE_PIPES, NULL);
  subargv[1] = "abort";
  subargv[2] = NULL;
  TEST_PEX_RUN (pex1, PEX_LAST, "./test-pexecute", subargv, NULL, "temp.z");
  status = TEST_PEX_GET_STATUS_1 (pex1);
  if (!WIFSIGNALED (status) || WTERMSIG (status) != SIGABRT)
    ERROR ("abort failed");
  pex_free (pex1);
  remove ("temp.z");

  pex1 = TEST_PEX_INIT (PEX_USE_PIPES, "temp");
  subargv[1] = "echo";
  subargv[2] = "foo";
  subargv[3] = NULL;
  TEST_PEX_RUN (pex1, 0, "./test-pexecute", subargv, NULL, NULL);
  e = TEST_PEX_READ_OUTPUT (pex1);
  CHECK_LINE (e, "foo");
  if (TEST_PEX_GET_STATUS_1 (pex1) != 0)
    ERROR ("echo exit status failed");
  pex_free (pex1);

  pex1 = TEST_PEX_INIT (PEX_USE_PIPES, "temp");
  subargv[1] = "echo";
  subargv[2] = "bar";
  subargv[3] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".x", NULL);
  subargv[1] = "copy";
  subargv[2] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".y", NULL);
  e = TEST_PEX_READ_OUTPUT (pex1);
  CHECK_LINE (e, "bar");
  TEST_PEX_GET_STATUS (pex1, 2, statuses);
  if (!WIFEXITED (statuses[0]) || WEXITSTATUS (statuses[0]) != EXIT_SUCCESS
      || !WIFEXITED (statuses[1]) || WEXITSTATUS (statuses[1]) != EXIT_SUCCESS)
    ERROR ("copy exit status failed");
  pex_free (pex1);
  if (fopen ("temp.x", "r") != NULL || fopen ("temp.y", "r") != NULL)
    ERROR ("temporary files exist");

  pex1 = TEST_PEX_INIT (0, "temp");
  subargv[1] = "echo";
  subargv[2] = "bar";
  subargv[3] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".x", NULL);
  subargv[1] = "copy";
  subargv[2] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".y", NULL);
  e = TEST_PEX_READ_OUTPUT (pex1);
  CHECK_LINE (e, "bar");
  TEST_PEX_GET_STATUS (pex1, 2, statuses);
  if (!WIFEXITED (statuses[0]) || WEXITSTATUS (statuses[0]) != EXIT_SUCCESS
      || !WIFEXITED (statuses[1]) || WEXITSTATUS (statuses[1]) != EXIT_SUCCESS)
    ERROR ("copy exit status failed");
  pex_free (pex1);
  if (fopen ("temp.x", "r") != NULL || fopen ("temp.y", "r") != NULL)
    ERROR ("temporary files exist");

  pex1 = TEST_PEX_INIT (PEX_SAVE_TEMPS, "temp");
  subargv[1] = "echo";
  subargv[2] = "quux";
  subargv[3] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".x", NULL);
  subargv[1] = "copy";
  subargv[2] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".y", NULL);
  e = TEST_PEX_READ_OUTPUT (pex1);
  CHECK_LINE (e, "quux");
  TEST_PEX_GET_STATUS (pex1, 2, statuses);
  if (!WIFEXITED (statuses[0]) || WEXITSTATUS (statuses[0]) != EXIT_SUCCESS
      || !WIFEXITED (statuses[1]) || WEXITSTATUS (statuses[1]) != EXIT_SUCCESS)
    ERROR ("copy temp exit status failed");
  e = fopen ("temp.x", "r");
  if (e == NULL)
    FATAL_ERROR ("fopen temp.x failed in copy temp", errno);
  CHECK_LINE (e, "quux");
  fclose (e);
  e = fopen ("temp.y", "r");
  if (e == NULL)
    FATAL_ERROR ("fopen temp.y failed in copy temp", errno);
  CHECK_LINE (e, "quux");
  fclose (e);
  pex_free (pex1);
  remove ("temp.x");
  remove ("temp.y");

  pex1 = TEST_PEX_INIT (PEX_USE_PIPES, "temp");
  subargv[1] = "echoerr";
  subargv[2] = "one";
  subargv[3] = "two";
  subargv[4] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".x", "temp2.x");
  subargv[1] = "write";
  subargv[2] = "temp2.y";
  subargv[3] = NULL;
  TEST_PEX_RUN (pex1, PEX_SUFFIX, "./test-pexecute", subargv, ".y", NULL);
  TEST_PEX_GET_STATUS (pex1, 2, statuses);
  if (!WIFEXITED (statuses[0]) || WEXITSTATUS (statuses[0]) != EXIT_SUCCESS
      || !WIFEXITED (statuses[1]) || WEXITSTATUS (statuses[1]) != EXIT_SUCCESS)
    ERROR ("echoerr exit status failed");
  pex_free (pex1);
  if (fopen ("temp.x", "r") != NULL || fopen ("temp.y", "r") != NULL)
    ERROR ("temporary files exist");
  e = fopen ("temp2.x", "r");
  if (e == NULL)
    FATAL_ERROR ("fopen temp2.x failed in echoerr", errno);
  CHECK_LINE (e, "two");
  fclose (e);
  e = fopen ("temp2.y", "r");
  if (e == NULL)
    FATAL_ERROR ("fopen temp2.y failed in echoerr", errno);
  CHECK_LINE (e, "one");
  fclose (e);
  remove ("temp2.x");
  remove ("temp2.y");

  /* Test the old pexecute interface.  */
  {
    int pid1, pid2;
    char *errmsg_fmt;
    char *errmsg_arg;
    char errbuf1[1000];
    char errbuf2[1000];

    subargv[1] = "echo";
    subargv[2] = "oldpexecute";
    subargv[3] = NULL;
    pid1 = pexecute ("./test-pexecute", subargv, "test-pexecute", "temp",
		     &errmsg_fmt, &errmsg_arg, PEXECUTE_FIRST);
    if (pid1 < 0)
      {
	snprintf (errbuf1, sizeof errbuf1, errmsg_fmt, errmsg_arg);
	snprintf (errbuf2, sizeof errbuf2, "pexecute 1 failed: %s", errbuf1);
	FATAL_ERROR (errbuf2, 0);
      }

    subargv[1] = "write";
    subargv[2] = "temp.y";
    subargv[3] = NULL;
    pid2 = pexecute ("./test-pexecute", subargv, "test-pexecute", "temp",
		     &errmsg_fmt, &errmsg_arg, PEXECUTE_LAST);
    if (pid2 < 0)
      {
	snprintf (errbuf1, sizeof errbuf1, errmsg_fmt, errmsg_arg);
	snprintf (errbuf2, sizeof errbuf2, "pexecute 2 failed: %s", errbuf1);
	FATAL_ERROR (errbuf2, 0);
      }

    if (pwait (pid1, &status, 0) < 0)
      FATAL_ERROR ("write pwait 1 failed", errno);
    if (!WIFEXITED (status) || WEXITSTATUS (status) != EXIT_SUCCESS)
      ERROR ("write exit status 1 failed");

    if (pwait (pid2, &status, 0) < 0)
      FATAL_ERROR ("write pwait 1 failed", errno);
    if (!WIFEXITED (status) || WEXITSTATUS (status) != EXIT_SUCCESS)
      ERROR ("write exit status 2 failed");

    e = fopen ("temp.y", "r");
    if (e == NULL)
      FATAL_ERROR ("fopen temp.y failed in copy temp", errno);
    CHECK_LINE (e, "oldpexecute");
    fclose (e);

    remove ("temp.y");
  }

  if (trace)
    fprintf (stderr, "Exiting with status %d\n", error_count);

  return error_count;
}

/* Execute one of the special testing commands.  */

static void
do_cmd (int argc, char **argv)
{
  const char *s;

  /* Try to prevent generating a core dump.  */
#ifdef RLIMIT_CORE
 {
   struct rlimit r;

   r.rlim_cur = 0;
   r.rlim_max = 0;
   setrlimit (RLIMIT_CORE, &r);
 }
#endif

  s = argv[1];
  if (strcmp (s, "exit") == 0)
    exit (EXIT_SUCCESS);
  else if (strcmp (s, "echo") == 0)
    {
      int i;

      for (i = 2; i < argc; ++i)
	{
	  if (i > 2)
	    putchar (' ');
	  fputs (argv[i], stdout);
	}
      putchar ('\n');
      exit (EXIT_SUCCESS);
    }
  else if (strcmp (s, "echoerr") == 0)
    {
      int i;

      for (i = 2; i < argc; ++i)
	{
	  if (i > 3)
	    putc (' ', (i & 1) == 0 ? stdout : stderr);
	  fputs (argv[i], (i & 1) == 0 ? stdout : stderr);
	}
      putc ('\n', stdout);
      putc ('\n', stderr);
      exit (EXIT_SUCCESS);
    }
  else if (strcmp (s, "error") == 0)
    exit (EXIT_FAILURE);
  else if (strcmp (s, "abort") == 0)
    abort ();
  else if (strcmp (s, "copy") == 0)
    {
      int c;

      while ((c = getchar ()) != EOF)
	putchar (c);
      exit (EXIT_SUCCESS);
    }
  else if (strcmp (s, "write") == 0)
    {
      FILE *e;
      int c;

      e = fopen (argv[2], "w");
      if (e == NULL)
	FATAL_ERROR ("fopen for write failed", errno);
      while ((c = getchar ()) != EOF)
	putc (c, e);
      if (fclose (e) != 0)
	FATAL_ERROR ("fclose for write failed", errno);
      exit (EXIT_SUCCESS);
    }
  else
    {
      char buf[1000];

      snprintf (buf, sizeof buf, "unrecognized command %s", argv[1]);
      FATAL_ERROR (buf, 0);
    }

  exit (EXIT_FAILURE);
}
