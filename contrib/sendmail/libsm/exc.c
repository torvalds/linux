/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: exc.c,v 1.50 2013-11-22 20:51:42 ca Exp $")

/*
**  exception handling
**  For documentation, see exc.html
*/

#include <ctype.h>
#include <string.h>

#include <sm/errstring.h>
#include <sm/exc.h>
#include <sm/heap.h>
#include <sm/string.h>
#include <sm/varargs.h>
#include <sm/io.h>

const char SmExcMagic[] = "sm_exc";
const char SmExcTypeMagic[] = "sm_exc_type";

/*
**  SM_ETYPE_PRINTF -- printf for exception types.
**
**	Parameters:
**		exc -- exception.
**		stream -- file for output.
**
**	Returns:
**		none.
*/

/*
**  A simple formatted print function that can be used as the print function
**  by most exception types.  It prints the printcontext string, interpreting
**  occurrences of %0 through %9 as references to the argument vector.
**  If exception argument 3 is an int or long, then %3 will print the
**  argument in decimal, and %o3 or %x3 will print it in octal or hex.
*/

void
sm_etype_printf(exc, stream)
	SM_EXC_T *exc;
	SM_FILE_T *stream;
{
	size_t n = strlen(exc->exc_type->etype_argformat);
	const char *p, *s;
	char format;

	for (p = exc->exc_type->etype_printcontext; *p != '\0'; ++p)
	{
		if (*p != '%')
		{
			(void) sm_io_putc(stream, SM_TIME_DEFAULT, *p);
			continue;
		}
		++p;
		if (*p == '\0')
		{
			(void) sm_io_putc(stream, SM_TIME_DEFAULT, '%');
			break;
		}
		if (*p == '%')
		{
			(void) sm_io_putc(stream, SM_TIME_DEFAULT, '%');
			continue;
		}
		format = '\0';
		if (isalpha(*p))
		{
			format = *p++;
			if (*p == '\0')
			{
				(void) sm_io_putc(stream, SM_TIME_DEFAULT, '%');
				(void) sm_io_putc(stream, SM_TIME_DEFAULT,
						  format);
				break;
			}
		}
		if (isdigit(*p))
		{
			size_t i = *p - '0';
			if (i < n)
			{
				switch (exc->exc_type->etype_argformat[i])
				{
				  case 's':
				  case 'r':
					s = exc->exc_argv[i].v_str;
					if (s == NULL)
						s = "(null)";
					sm_io_fputs(stream, SM_TIME_DEFAULT, s);
					continue;
				  case 'i':
					sm_io_fprintf(stream,
						SM_TIME_DEFAULT,
						format == 'o' ? "%o"
						: format == 'x' ? "%x"
								: "%d",
						exc->exc_argv[i].v_int);
					continue;
				  case 'l':
					sm_io_fprintf(stream,
						SM_TIME_DEFAULT,
						format == 'o' ? "%lo"
						: format == 'x' ? "%lx"
								: "%ld",
						exc->exc_argv[i].v_long);
					continue;
				  case 'e':
					sm_exc_write(exc->exc_argv[i].v_exc,
						stream);
					continue;
				}
			}
		}
		(void) sm_io_putc(stream, SM_TIME_DEFAULT, '%');
		if (format)
			(void) sm_io_putc(stream, SM_TIME_DEFAULT, format);
		(void) sm_io_putc(stream, SM_TIME_DEFAULT, *p);
	}
}

/*
**  Standard exception types.
*/

/*
**  SM_ETYPE_OS_PRINT -- Print OS related exception.
**
**	Parameters:
**		exc -- exception.
**		stream -- file for output.
**
**	Returns:
**		none.
*/

static void
sm_etype_os_print __P((
	SM_EXC_T *exc,
	SM_FILE_T *stream));

static void
sm_etype_os_print(exc, stream)
	SM_EXC_T *exc;
	SM_FILE_T *stream;
{
	int err = exc->exc_argv[0].v_int;
	char *syscall = exc->exc_argv[1].v_str;
	char *sysargs = exc->exc_argv[2].v_str;

	if (sysargs)
		sm_io_fprintf(stream, SM_TIME_DEFAULT, "%s: %s failed: %s",
			      sysargs, syscall, sm_errstring(err));
	else
		sm_io_fprintf(stream, SM_TIME_DEFAULT, "%s failed: %s", syscall,
			      sm_errstring(err));
}

/*
**  SmEtypeOs represents the failure of a Unix system call.
**  The three arguments are:
**   int errno (eg, ENOENT)
**   char *syscall (eg, "open")
**   char *sysargs (eg, NULL or "/etc/mail/sendmail.cf")
*/

const SM_EXC_TYPE_T SmEtypeOs =
{
	SmExcTypeMagic,
	"E:sm.os",
	"isr",
	sm_etype_os_print,
	NULL,
};

/*
**  SmEtypeErr is a completely generic error which should only be
**  used in applications and test programs.  Libraries should use
**  more specific exception codes.
*/

const SM_EXC_TYPE_T SmEtypeErr =
{
	SmExcTypeMagic,
	"E:sm.err",
	"r",
	sm_etype_printf,
	"%0",
};

/*
**  SM_EXC_VNEW_X -- Construct a new exception object.
**
**	Parameters:
**		etype -- type of exception.
**		ap -- varargs.
**
**	Returns:
**		pointer to exception object.
*/

/*
**  This is an auxiliary function called by sm_exc_new_x and sm_exc_raisenew_x.
**
**  If an exception is raised, then to avoid a storage leak, we must:
**  (a) Free all storage we have allocated.
**  (b) Free all exception arguments in the varargs list.
**  Getting this right is tricky.
**
**  To see why (b) is required, consider the code fragment
**     SM_EXCEPT(exc, "*")
**         sm_exc_raisenew_x(&MyEtype, exc);
**     SM_END_TRY
**  In the normal case, sm_exc_raisenew_x will allocate and raise a new
**  exception E that owns exc.  When E is eventually freed, exc is also freed.
**  In the exceptional case, sm_exc_raisenew_x must free exc before raising
**  an out-of-memory exception so that exc is not leaked.
*/

static SM_EXC_T *sm_exc_vnew_x __P((const SM_EXC_TYPE_T *, va_list SM_NONVOLATILE));

static SM_EXC_T *
sm_exc_vnew_x(etype, ap)
	const SM_EXC_TYPE_T *etype;
	va_list SM_NONVOLATILE ap;
{
	/*
	**  All variables that are modified in the SM_TRY clause and
	**  referenced in the SM_EXCEPT clause must be declared volatile.
	*/

	/* NOTE: Type of si, i, and argc *must* match */
	SM_EXC_T * volatile exc = NULL;
	int volatile si = 0;
	SM_VAL_T * volatile argv = NULL;
	int i, argc;

	SM_REQUIRE_ISA(etype, SmExcTypeMagic);
	argc = strlen(etype->etype_argformat);
	SM_TRY
	{
		/*
		**  Step 1.  Allocate the exception structure.
		**  On failure, scan the varargs list and free all
		**  exception arguments.
		*/

		exc = sm_malloc_x(sizeof(SM_EXC_T));
		exc->sm_magic = SmExcMagic;
		exc->exc_refcount = 1;
		exc->exc_type = etype;
		exc->exc_argv = NULL;

		/*
		**  Step 2.  Allocate the argument vector.
		**  On failure, free exc, scan the varargs list and free all
		**  exception arguments.  On success, scan the varargs list,
		**  and copy the arguments into argv.
		*/

		argv = sm_malloc_x(argc * sizeof(SM_VAL_T));
		exc->exc_argv = argv;
		for (i = 0; i < argc; ++i)
		{
			switch (etype->etype_argformat[i])
			{
			  case 'i':
				argv[i].v_int = SM_VA_ARG(ap, int);
				break;
			  case 'l':
				argv[i].v_long = SM_VA_ARG(ap, long);
				break;
			  case 'e':
				argv[i].v_exc = SM_VA_ARG(ap, SM_EXC_T*);
				break;
			  case 's':
				argv[i].v_str = SM_VA_ARG(ap, char*);
				break;
			  case 'r':
				SM_REQUIRE(etype->etype_argformat[i+1] == '\0');
				argv[i].v_str = SM_VA_ARG(ap, char*);
				break;
			  default:
				sm_abort("sm_exc_vnew_x: bad argformat '%c'",
					etype->etype_argformat[i]);
			}
		}

		/*
		**  Step 3.  Scan argv, and allocate space for all
		**  string arguments.  si is the number of elements
		**  of argv that have been processed so far.
		**  On failure, free exc, argv, all the exception arguments
		**  and all of the strings that have been copied.
		*/

		for (si = 0; si < argc; ++si)
		{
			switch (etype->etype_argformat[si])
			{
			  case 's':
			    {
				char *str = argv[si].v_str;
				if (str != NULL)
				    argv[si].v_str = sm_strdup_x(str);
			    }
			    break;
			  case 'r':
			    {
				char *fmt = argv[si].v_str;
				if (fmt != NULL)
				    argv[si].v_str = sm_vstringf_x(fmt, ap);
			    }
			    break;
			}
		}
	}
	SM_EXCEPT(e, "*")
	{
		if (exc == NULL || argv == NULL)
		{
			/*
			**  Failure in step 1 or step 2.
			**  Scan ap and free all exception arguments.
			*/

			for (i = 0; i < argc; ++i)
			{
				switch (etype->etype_argformat[i])
				{
				  case 'i':
					(void) SM_VA_ARG(ap, int);
					break;
				  case 'l':
					(void) SM_VA_ARG(ap, long);
					break;
				  case 'e':
					sm_exc_free(SM_VA_ARG(ap, SM_EXC_T*));
					break;
				  case 's':
				  case 'r':
					(void) SM_VA_ARG(ap, char*);
					break;
				}
			}
		}
		else
		{
			/*
			**  Failure in step 3.  Scan argv and free
			**  all exception arguments and all string
			**  arguments that have been duplicated.
			**  Then free argv.
			*/

			for (i = 0; i < argc; ++i)
			{
				switch (etype->etype_argformat[i])
				{
				  case 'e':
					sm_exc_free(argv[i].v_exc);
					break;
				  case 's':
				  case 'r':
					if (i < si)
						sm_free(argv[i].v_str);
					break;
				}
			}
			sm_free(argv);
		}
		sm_free(exc);
		sm_exc_raise_x(e);
	}
	SM_END_TRY

	return exc;
}

/*
**  SM_EXC_NEW_X -- Construct a new exception object.
**
**	Parameters:
**		etype -- type of exception.
**		... -- varargs.
**
**	Returns:
**		pointer to exception object.
*/

SM_EXC_T *
#if SM_VA_STD
sm_exc_new_x(
	const SM_EXC_TYPE_T *etype,
	...)
#else /* SM_VA_STD */
sm_exc_new_x(etype, va_alist)
	const SM_EXC_TYPE_T *etype;
	va_dcl
#endif /* SM_VA_STD */
{
	SM_EXC_T *exc;
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, etype);
	exc = sm_exc_vnew_x(etype, ap);
	SM_VA_END(ap);
	return exc;
}

/*
**  SM_EXC_FREE -- Destroy a reference to an exception object.
**
**	Parameters:
**		exc -- exception object.
**
**	Returns:
**		none.
*/

void
sm_exc_free(exc)
	SM_EXC_T *exc;
{
	if (exc == NULL)
		return;
	SM_REQUIRE(exc->sm_magic == SmExcMagic);
	if (exc->exc_refcount == 0)
		return;
	if (--exc->exc_refcount == 0)
	{
		int i, c;

		for (i = 0; (c = exc->exc_type->etype_argformat[i]) != '\0';
		     ++i)
		{
			switch (c)
			{
			  case 's':
			  case 'r':
				sm_free(exc->exc_argv[i].v_str);
				break;
			  case 'e':
				sm_exc_free(exc->exc_argv[i].v_exc);
				break;
			}
		}
		exc->sm_magic = NULL;
		sm_free(exc->exc_argv);
		sm_free(exc);
	}
}

/*
**  SM_EXC_MATCH -- Match exception category against a glob pattern.
**
**	Parameters:
**		exc -- exception.
**		pattern -- glob pattern.
**
**	Returns:
**		true iff match.
*/

bool
sm_exc_match(exc, pattern)
	SM_EXC_T *exc;
	const char *pattern;
{
	if (exc == NULL)
		return false;
	SM_REQUIRE(exc->sm_magic == SmExcMagic);
	return sm_match(exc->exc_type->etype_category, pattern);
}

/*
**  SM_EXC_WRITE -- Write exception message to a stream (wo trailing newline).
**
**	Parameters:
**		exc -- exception.
**		stream -- file for output.
**
**	Returns:
**		none.
*/

void
sm_exc_write(exc, stream)
	SM_EXC_T *exc;
	SM_FILE_T *stream;
{
	SM_REQUIRE_ISA(exc, SmExcMagic);
	exc->exc_type->etype_print(exc, stream);
}

/*
**  SM_EXC_PRINT -- Print exception message to a stream (with trailing newline).
**
**	Parameters:
**		exc -- exception.
**		stream -- file for output.
**
**	Returns:
**		none.
*/

void
sm_exc_print(exc, stream)
	SM_EXC_T *exc;
	SM_FILE_T *stream;
{
	SM_REQUIRE_ISA(exc, SmExcMagic);
	exc->exc_type->etype_print(exc, stream);
	(void) sm_io_putc(stream, SM_TIME_DEFAULT, '\n');
}

SM_EXC_HANDLER_T *SmExcHandler = NULL;
static SM_EXC_DEFAULT_HANDLER_T SmExcDefaultHandler = NULL;

/*
**  SM_EXC_NEWTHREAD -- Initialize exception handling for new process/thread.
**
**	Parameters:
**		h -- default exception handler.
**
**	Returns:
**		none.
*/

/*
**  Initialize a new process or a new thread by clearing the
**  exception handler stack and optionally setting a default
**  exception handler function.  Call this at the beginning of main,
**  or in a new process after calling fork, or in a new thread.
**
**  This function is a luxury, not a necessity.
**  If h != NULL then you can get the same effect by
**  wrapping the body of main, or the body of a forked child
**  or a new thread in SM_TRY ... SM_EXCEPT(e,"*") h(e); SM_END_TRY.
*/

void
sm_exc_newthread(h)
	SM_EXC_DEFAULT_HANDLER_T h;
{
	SmExcHandler = NULL;
	SmExcDefaultHandler = h;
}

/*
**  SM_EXC_RAISE_X -- Raise an exception.
**
**	Parameters:
**		exc -- exception.
**
**	Returns:
**		doesn't.
*/

void SM_DEAD_D
sm_exc_raise_x(exc)
	SM_EXC_T *exc;
{
	SM_REQUIRE_ISA(exc, SmExcMagic);

	if (SmExcHandler == NULL)
	{
		if (SmExcDefaultHandler != NULL)
		{
			SM_EXC_DEFAULT_HANDLER_T h;

			/*
			**  If defined, the default handler is expected
			**  to terminate the current thread of execution
			**  using exit() or pthread_exit().
			**  If it instead returns normally, then we fall
			**  through to the default case below.  If it
			**  raises an exception, then sm_exc_raise_x is
			**  re-entered and, because we set SmExcDefaultHandler
			**  to NULL before invoking h, we will again
			**  end up in the default case below.
			*/

			h = SmExcDefaultHandler;
			SmExcDefaultHandler = NULL;
			(*h)(exc);
		}

		/*
		**  No exception handler, so print the error and exit.
		**  To override this behaviour on a program wide basis,
		**  call sm_exc_newthread or put an exception handler in main().
		**
		**  XXX TODO: map the exception category to an exit code
		**  XXX from <sysexits.h>.
		*/

		sm_exc_print(exc, smioerr);
		exit(255);
	}

	if (SmExcHandler->eh_value == NULL)
		SmExcHandler->eh_value = exc;
	else
		sm_exc_free(exc);

	sm_longjmp_nosig(SmExcHandler->eh_context, 1);
}

/*
**  SM_EXC_RAISENEW_X -- shorthand for sm_exc_raise_x(sm_exc_new_x(...))
**
**	Parameters:
**		etype -- type of exception.
**		ap -- varargs.
**
**	Returns:
**		none.
*/

void SM_DEAD_D
#if SM_VA_STD
sm_exc_raisenew_x(
	const SM_EXC_TYPE_T *etype,
	...)
#else
sm_exc_raisenew_x(etype, va_alist)
	const SM_EXC_TYPE_T *etype;
	va_dcl
#endif
{
	SM_EXC_T *exc;
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, etype);
	exc = sm_exc_vnew_x(etype, ap);
	SM_VA_END(ap);
	sm_exc_raise_x(exc);
}
