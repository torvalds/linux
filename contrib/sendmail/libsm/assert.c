/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: assert.c,v 1.27 2013-11-22 20:51:42 ca Exp $")

/*
**  Abnormal program termination and assertion checking.
**  For documentation, see assert.html.
*/

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <sm/assert.h>
#include <sm/exc.h>
#include <sm/io.h>
#include <sm/varargs.h>

/*
**  Debug categories that are used to guard expensive assertion checks.
*/

SM_DEBUG_T SmExpensiveAssert = SM_DEBUG_INITIALIZER("sm_check_assert",
	"@(#)$Debug: sm_check_assert - check assertions $");

SM_DEBUG_T SmExpensiveRequire = SM_DEBUG_INITIALIZER("sm_check_require",
	"@(#)$Debug: sm_check_require - check function preconditions $");

SM_DEBUG_T SmExpensiveEnsure = SM_DEBUG_INITIALIZER("sm_check_ensure",
	"@(#)$Debug: sm_check_ensure - check function postconditions $");

/*
**  Debug category: send self SIGSTOP on fatal error,
**  so that you can run a debugger on the stopped process.
*/

SM_DEBUG_T SmAbortStop = SM_DEBUG_INITIALIZER("sm_abort_stop",
	"@(#)$Debug: sm_abort_stop - stop process on fatal error $");

/*
**  SM_ABORT_DEFAULTHANDLER -- Default procedure for abnormal program
**				termination.
**
**	The goal is to display an error message without disturbing the
**	process state too much, then dump core.
**
**	Parameters:
**		filename -- filename (can be NULL).
**		lineno -- line number.
**		msg -- message.
**
**	Returns:
**		doesn't return.
*/

static void
sm_abort_defaulthandler __P((
	const char *filename,
	int lineno,
	const char *msg));

static void
sm_abort_defaulthandler(filename, lineno, msg)
	const char *filename;
	int lineno;
	const char *msg;
{
	if (filename != NULL)
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "%s:%d: %s\n", filename,
			      lineno, msg);
	else
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "%s\n", msg);
	sm_io_flush(smioerr, SM_TIME_DEFAULT);
#ifdef SIGSTOP
	if (sm_debug_active(&SmAbortStop, 1))
		kill(getpid(), SIGSTOP);
#endif /* SIGSTOP */
	abort();
}

/*
**  This is the action to be taken to cause abnormal program termination.
*/

static SM_ABORT_HANDLER_T SmAbortHandler = sm_abort_defaulthandler;

/*
**  SM_ABORT_SETHANDLER -- Set handler for SM_ABORT()
**
**	This allows you to set a handler function for causing abnormal
**	program termination; it is called when a logic bug is detected.
**
**	Parameters:
**		f -- handler.
**
**	Returns:
**		none.
*/

void
sm_abort_sethandler(f)
	SM_ABORT_HANDLER_T f;
{
	if (f == NULL)
		SmAbortHandler = sm_abort_defaulthandler;
	else
		SmAbortHandler = f;
}

/*
**  SM_ABORT -- Call it when you have detected a logic bug.
**
**	Parameters:
**		fmt -- format string.
**		... -- arguments.
**
**	Returns:
**		doesn't.
*/

void SM_DEAD_D
#if SM_VA_STD
sm_abort(char *fmt, ...)
#else /* SM_VA_STD */
sm_abort(fmt, va_alist)
	char *fmt;
	va_dcl
#endif /* SM_VA_STD */
{
	char msg[128];
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, fmt);
	sm_vsnprintf(msg, sizeof msg, fmt, ap);
	SM_VA_END(ap);
	sm_abort_at(NULL, 0, msg);
}

/*
**  SM_ABORT_AT -- Initiate abnormal program termination.
**
**	This is the low level function that is called to initiate abnormal
**	program termination.  It prints an error message and terminates the
**	program.  It is called by sm_abort and by the assertion macros.
**	If filename != NULL then filename and lineno specify the line of source
**	code at which the bug was detected.
**
**	Parameters:
**		filename -- filename (can be NULL).
**		lineno -- line number.
**		msg -- message.
**
**	Returns:
**		doesn't.
*/

void SM_DEAD_D
sm_abort_at(filename, lineno, msg)
	const char *filename;
	int lineno;
	const char *msg;
{
	SM_TRY
		(*SmAbortHandler)(filename, lineno, msg);
	SM_EXCEPT(exc, "*")
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			      "exception raised by abort handler:\n");
		sm_exc_print(exc, smioerr);
		sm_io_flush(smioerr, SM_TIME_DEFAULT);
	SM_END_TRY

	/*
	**  SmAbortHandler isn't supposed to return.
	**  Since it has, let's make sure that the program is terminated.
	*/

	abort();
}
