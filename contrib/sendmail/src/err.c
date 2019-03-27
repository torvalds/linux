/*
 * Copyright (c) 1998-2003, 2010 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: err.c,v 8.206 2013-11-22 20:51:55 ca Exp $")

#if LDAPMAP
# include <lber.h>
# include <ldap.h>			/* for LDAP error codes */
#endif /* LDAPMAP */

static void	putoutmsg __P((char *, bool, bool));
static void	puterrmsg __P((char *));
static char	*fmtmsg __P((char *, const char *, const char *, const char *,
			     int, const char *, va_list));

/*
**  FATAL_ERROR -- handle a fatal exception
**
**	This function is installed as the default exception handler
**	in the main sendmail process, and in all child processes
**	that we create.  Its job is to handle exceptions that are not
**	handled at a lower level.
**
**	The theory is that unhandled exceptions will be 'fatal' class
**	exceptions (with an "F:" prefix), such as the out-of-memory
**	exception "F:sm.heap".  As such, they are handled by exiting
**	the process in exactly the same way that xalloc() in Sendmail 8.10
**	exits the process when it fails due to lack of memory:
**	we call syserr with a message beginning with "!".
**
**	Parameters:
**		exc -- exception which is terminating this process
**
**	Returns:
**		none
*/

void
fatal_error(exc)
	SM_EXC_T *exc;
{
	static char buf[256];
	SM_FILE_T f;

	/*
	**  This function may be called when the heap is exhausted.
	**  The following code writes the message for 'exc' into our
	**  static buffer without allocating memory or raising exceptions.
	*/

	sm_strio_init(&f, buf, sizeof(buf));
	sm_exc_write(exc, &f);
	(void) sm_io_flush(&f, SM_TIME_DEFAULT);

	/*
	**  Terminate the process after logging an error and cleaning up.
	**  Problems:
	**  - syserr decides what class of error this is by looking at errno.
	**    That's no good; we should look at the exc structure.
	**  - The cleanup code should be moved out of syserr
	**    and into individual exception handlers
	**    that are part of the module they clean up after.
	*/

	errno = ENOMEM;
	syserr("!%s", buf);
}

/*
**  SYSERR -- Print error message.
**
**	Prints an error message via sm_io_printf to the diagnostic output.
**
**	If the first character of the syserr message is `!' it will
**	log this as an ALERT message and exit immediately.  This can
**	leave queue files in an indeterminate state, so it should not
**	be used lightly.
**
**	If the first character of the syserr message is '!' or '@'
**	then syserr knows that the process is about to be terminated,
**	so the SMTP reply code defaults to 421.  Otherwise, the
**	reply code defaults to 451 or 554, depending on errno.
**
**	Parameters:
**		fmt -- the format string.  An optional '!', '@', or '+',
**			followed by an optional three-digit SMTP
**			reply code, followed by message text.
**		(others) -- parameters
**
**	Returns:
**		none
**		Raises E:mta.quickabort if QuickAbort is set.
**
**	Side Effects:
**		increments Errors.
**		sets ExitStat.
*/

char		MsgBuf[BUFSIZ*2];	/* text of most recent message */
static char	HeldMessageBuf[sizeof(MsgBuf)];	/* for held messages */

#if NAMED_BIND && !defined(NO_DATA)
# define NO_DATA	NO_ADDRESS
#endif /* NAMED_BIND && !defined(NO_DATA) */

void
/*VARARGS1*/
#ifdef __STDC__
syserr(const char *fmt, ...)
#else /* __STDC__ */
syserr(fmt, va_alist)
	const char *fmt;
	va_dcl
#endif /* __STDC__ */
{
	register char *p;
	int save_errno = errno;
	bool panic, exiting, keep;
	char *user;
	char *enhsc;
	char *errtxt;
	struct passwd *pw;
	char ubuf[80];
	SM_VA_LOCAL_DECL

	panic = exiting = keep = false;
	switch (*fmt)
	{
	  case '!':
		++fmt;
		panic = exiting = true;
		break;
	  case '@':
		++fmt;
		exiting = true;
		break;
	  case '+':
		++fmt;
		keep = true;
		break;
	  default:
		break;
	}

	/* format and output the error message */
	if (exiting)
	{
		/*
		**  Since we are terminating the process,
		**  we are aborting the entire SMTP session,
		**  rather than just the current transaction.
		*/

		p = "421";
		enhsc = "4.0.0";
	}
	else if (save_errno == 0)
	{
		p = "554";
		enhsc = "5.0.0";
	}
	else
	{
		p = "451";
		enhsc = "4.0.0";
	}
	SM_VA_START(ap, fmt);
	errtxt = fmtmsg(MsgBuf, (char *) NULL, p, enhsc, save_errno, fmt, ap);
	SM_VA_END(ap);
	puterrmsg(MsgBuf);

	/* save this message for mailq printing */
	if (!panic && CurEnv != NULL && (!keep || CurEnv->e_message == NULL))
	{
		char *nmsg = sm_rpool_strdup_x(CurEnv->e_rpool, errtxt);

		if (CurEnv->e_rpool == NULL && CurEnv->e_message != NULL)
			sm_free(CurEnv->e_message);
		CurEnv->e_message = nmsg;
	}

	/* determine exit status if not already set */
	if (ExitStat == EX_OK)
	{
		if (save_errno == 0)
			ExitStat = EX_SOFTWARE;
		else
			ExitStat = EX_OSERR;
		if (tTd(54, 1))
			sm_dprintf("syserr: ExitStat = %d\n", ExitStat);
	}

	pw = sm_getpwuid(RealUid);
	if (pw != NULL)
		user = pw->pw_name;
	else
	{
		user = ubuf;
		(void) sm_snprintf(ubuf, sizeof(ubuf), "UID%d", (int) RealUid);
	}

	if (LogLevel > 0)
		sm_syslog(panic ? LOG_ALERT : LOG_CRIT,
			  CurEnv == NULL ? NOQID : CurEnv->e_id,
			  "SYSERR(%s): %.900s",
			  user, errtxt);
	switch (save_errno)
	{
	  case EBADF:
	  case ENFILE:
	  case EMFILE:
	  case ENOTTY:
#ifdef EFBIG
	  case EFBIG:
#endif /* EFBIG */
#ifdef ESPIPE
	  case ESPIPE:
#endif /* ESPIPE */
#ifdef EPIPE
	  case EPIPE:
#endif /* EPIPE */
#ifdef ENOBUFS
	  case ENOBUFS:
#endif /* ENOBUFS */
#ifdef ESTALE
	  case ESTALE:
#endif /* ESTALE */
		printopenfds(true);
		mci_dump_all(smioout, true);
		break;
	}
	if (panic)
	{
#if XLA
		xla_all_end();
#endif /* XLA */
		sync_queue_time();
		if (tTd(0, 1))
			abort();
		exit(EX_OSERR);
	}
	errno = 0;
	if (QuickAbort)
		sm_exc_raisenew_x(&EtypeQuickAbort, 2);
}
/*
**  USRERR -- Signal user error.
**
**	This is much like syserr except it is for user errors.
**
**	Parameters:
**		fmt -- the format string.  If it does not begin with
**			a three-digit SMTP reply code, 550 is assumed.
**		(others) -- sm_io_printf strings
**
**	Returns:
**		none
**		Raises E:mta.quickabort if QuickAbort is set.
**
**	Side Effects:
**		increments Errors.
*/

/*VARARGS1*/
void
#ifdef __STDC__
usrerr(const char *fmt, ...)
#else /* __STDC__ */
usrerr(fmt, va_alist)
	const char *fmt;
	va_dcl
#endif /* __STDC__ */
{
	char *enhsc;
	char *errtxt;
	SM_VA_LOCAL_DECL

	if (fmt[0] == '5' || fmt[0] == '6')
		enhsc = "5.0.0";
	else if (fmt[0] == '4' || fmt[0] == '8')
		enhsc = "4.0.0";
	else if (fmt[0] == '2')
		enhsc = "2.0.0";
	else
		enhsc = NULL;
	SM_VA_START(ap, fmt);
	errtxt = fmtmsg(MsgBuf, CurEnv->e_to, "550", enhsc, 0, fmt, ap);
	SM_VA_END(ap);

	if (SuprErrs)
		return;

	/* save this message for mailq printing */
	switch (MsgBuf[0])
	{
	  case '4':
	  case '8':
		if (CurEnv->e_message != NULL)
			break;

		/* FALLTHROUGH */

	  case '5':
	  case '6':
		if (CurEnv->e_rpool == NULL && CurEnv->e_message != NULL)
			sm_free(CurEnv->e_message);
		if (MsgBuf[0] == '6')
		{
			char buf[MAXLINE];

			(void) sm_snprintf(buf, sizeof(buf),
					   "Postmaster warning: %.*s",
					   (int) sizeof(buf) - 22, errtxt);
			CurEnv->e_message =
				sm_rpool_strdup_x(CurEnv->e_rpool, buf);
		}
		else
		{
			CurEnv->e_message =
				sm_rpool_strdup_x(CurEnv->e_rpool, errtxt);
		}
		break;
	}

	puterrmsg(MsgBuf);
	if (LogLevel > 3 && LogUsrErrs)
		sm_syslog(LOG_NOTICE, CurEnv->e_id, "%.900s", errtxt);
	if (QuickAbort)
		sm_exc_raisenew_x(&EtypeQuickAbort, 1);
}
/*
**  USRERRENH -- Signal user error.
**
**	Same as usrerr but with enhanced status code.
**
**	Parameters:
**		enhsc -- the enhanced status code.
**		fmt -- the format string.  If it does not begin with
**			a three-digit SMTP reply code, 550 is assumed.
**		(others) -- sm_io_printf strings
**
**	Returns:
**		none
**		Raises E:mta.quickabort if QuickAbort is set.
**
**	Side Effects:
**		increments Errors.
*/

/*VARARGS2*/
void
#ifdef __STDC__
usrerrenh(char *enhsc, const char *fmt, ...)
#else /* __STDC__ */
usrerrenh(enhsc, fmt, va_alist)
	char *enhsc;
	const char *fmt;
	va_dcl
#endif /* __STDC__ */
{
	char *errtxt;
	SM_VA_LOCAL_DECL

	if (enhsc == NULL || *enhsc == '\0')
	{
		if (fmt[0] == '5' || fmt[0] == '6')
			enhsc = "5.0.0";
		else if (fmt[0] == '4' || fmt[0] == '8')
			enhsc = "4.0.0";
		else if (fmt[0] == '2')
			enhsc = "2.0.0";
	}
	SM_VA_START(ap, fmt);
	errtxt = fmtmsg(MsgBuf, CurEnv->e_to, "550", enhsc, 0, fmt, ap);
	SM_VA_END(ap);

	if (SuprErrs)
		return;

	/* save this message for mailq printing */
	switch (MsgBuf[0])
	{
	  case '4':
	  case '8':
		if (CurEnv->e_message != NULL)
			break;

		/* FALLTHROUGH */

	  case '5':
	  case '6':
		if (CurEnv->e_rpool == NULL && CurEnv->e_message != NULL)
			sm_free(CurEnv->e_message);
		if (MsgBuf[0] == '6')
		{
			char buf[MAXLINE];

			(void) sm_snprintf(buf, sizeof(buf),
					   "Postmaster warning: %.*s",
					   (int) sizeof(buf) - 22, errtxt);
			CurEnv->e_message =
				sm_rpool_strdup_x(CurEnv->e_rpool, buf);
		}
		else
		{
			CurEnv->e_message =
				sm_rpool_strdup_x(CurEnv->e_rpool, errtxt);
		}
		break;
	}

	puterrmsg(MsgBuf);
	if (LogLevel > 3 && LogUsrErrs)
		sm_syslog(LOG_NOTICE, CurEnv->e_id, "%.900s", errtxt);
	if (QuickAbort)
		sm_exc_raisenew_x(&EtypeQuickAbort, 1);
}

/*
**  MESSAGE -- print message (not necessarily an error)
**
**	Parameters:
**		msg -- the message (sm_io_printf fmt) -- it can begin with
**			an SMTP reply code.  If not, 050 is assumed.
**		(others) -- sm_io_printf arguments
**
**	Returns:
**		none
**
**	Side Effects:
**		none.
*/

/*VARARGS1*/
void
#ifdef __STDC__
message(const char *msg, ...)
#else /* __STDC__ */
message(msg, va_alist)
	const char *msg;
	va_dcl
#endif /* __STDC__ */
{
	char *errtxt;
	SM_VA_LOCAL_DECL

	errno = 0;
	SM_VA_START(ap, msg);
	errtxt = fmtmsg(MsgBuf, CurEnv->e_to, "050", (char *) NULL, 0, msg, ap);
	SM_VA_END(ap);
	putoutmsg(MsgBuf, false, false);

	/* save this message for mailq printing */
	switch (MsgBuf[0])
	{
	  case '4':
	  case '8':
		if (CurEnv->e_message != NULL)
			break;
		/* FALLTHROUGH */

	  case '5':
		if (CurEnv->e_rpool == NULL && CurEnv->e_message != NULL)
			sm_free(CurEnv->e_message);
		CurEnv->e_message = sm_rpool_strdup_x(CurEnv->e_rpool, errtxt);
		break;
	}
}

#if _FFR_PROXY
/*
**  EMESSAGE -- print message (not necessarily an error)
**	(same as message() but requires reply code and enhanced status code)
**
**	Parameters:
**		replycode -- SMTP reply code.
**		enhsc -- enhanced status code.
**		msg -- the message (sm_io_printf fmt) -- it can begin with
**			an SMTP reply code.  If not, 050 is assumed.
**		(others) -- sm_io_printf arguments
**
**	Returns:
**		none
**
**	Side Effects:
**		none.
*/

/*VARARGS3*/
void
# ifdef __STDC__
emessage(const char *replycode, const char *enhsc, const char *msg, ...)
# else /* __STDC__ */
emessage(replycode, enhsc, msg, va_alist)
	const char *replycode;
	const char *enhsc;
	const char *msg;
	va_dcl
# endif /* __STDC__ */
{
	char *errtxt;
	SM_VA_LOCAL_DECL

	errno = 0;
	SM_VA_START(ap, msg);
	errtxt = fmtmsg(MsgBuf, CurEnv->e_to, replycode, enhsc, 0, msg, ap);
	SM_VA_END(ap);
	putoutmsg(MsgBuf, false, false);

	/* save this message for mailq printing */
	switch (MsgBuf[0])
	{
	  case '4':
	  case '8':
		if (CurEnv->e_message != NULL)
			break;
		/* FALLTHROUGH */

	  case '5':
		if (CurEnv->e_rpool == NULL && CurEnv->e_message != NULL)
			sm_free(CurEnv->e_message);
		CurEnv->e_message = sm_rpool_strdup_x(CurEnv->e_rpool, errtxt);
		break;
	}
}

/*
**  EXTSC -- check and extract a status codes
**
**	Parameters:
**		msg -- string with possible enhanced status code.
**		delim -- delim for enhanced status code.
**		replycode -- pointer to storage for SMTP reply code;
**			must be != NULL and have space for at least
**			4 characters.
**		enhsc -- pointer to storage for enhanced status code;
**			must be != NULL and have space for at least
**			10 characters ([245].[0-9]{1,3}.[0-9]{1,3})
**
**	Returns:
**		-1  -- no SMTP reply code.
**		>=3 -- offset of error text in msg.
**		(<=4  -- no enhanced status code)
*/

int
extsc(msg, delim, replycode, enhsc)
	const char *msg;
	int delim;
	char *replycode;
	char *enhsc;
{
	int offset;

	SM_REQUIRE(replycode != NULL);
	SM_REQUIRE(enhsc != NULL);
	replycode[0] = '\0';
	enhsc[0] = '\0';
	if (msg == NULL)
		return -1;
	if (!ISSMTPREPLY(msg))
		return -1;
	sm_strlcpy(replycode, msg, 4);
	if (msg[3] == '\0')
		return 3;
	offset = 4;
	if (isenhsc(msg + 4, delim))
		offset = extenhsc(msg + 4, delim, enhsc) + 4;
	return offset;
}
#endif /* _FFR_PROXY */

/*
**  NMESSAGE -- print message (not necessarily an error)
**
**	Just like "message" except it never puts the to... tag on.
**
**	Parameters:
**		msg -- the message (sm_io_printf fmt) -- if it begins
**			with a three digit SMTP reply code, that is used,
**			otherwise 050 is assumed.
**		(others) -- sm_io_printf arguments
**
**	Returns:
**		none
**
**	Side Effects:
**		none.
*/

/*VARARGS1*/
void
#ifdef __STDC__
nmessage(const char *msg, ...)
#else /* __STDC__ */
nmessage(msg, va_alist)
	const char *msg;
	va_dcl
#endif /* __STDC__ */
{
	char *errtxt;
	SM_VA_LOCAL_DECL

	errno = 0;
	SM_VA_START(ap, msg);
	errtxt = fmtmsg(MsgBuf, (char *) NULL, "050",
			(char *) NULL, 0, msg, ap);
	SM_VA_END(ap);
	putoutmsg(MsgBuf, false, false);

	/* save this message for mailq printing */
	switch (MsgBuf[0])
	{
	  case '4':
	  case '8':
		if (CurEnv->e_message != NULL)
			break;
		/* FALLTHROUGH */

	  case '5':
		if (CurEnv->e_rpool == NULL && CurEnv->e_message != NULL)
			sm_free(CurEnv->e_message);
		CurEnv->e_message = sm_rpool_strdup_x(CurEnv->e_rpool, errtxt);
		break;
	}
}
/*
**  PUTOUTMSG -- output error message to transcript and channel
**
**	Parameters:
**		msg -- message to output (in SMTP format).
**		holdmsg -- if true, don't output a copy of the message to
**			our output channel.
**		heldmsg -- if true, this is a previously held message;
**			don't log it to the transcript file.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Outputs msg to the transcript.
**		If appropriate, outputs it to the channel.
**		Deletes SMTP reply code number as appropriate.
*/

static void
putoutmsg(msg, holdmsg, heldmsg)
	char *msg;
	bool holdmsg;
	bool heldmsg;
{
	char msgcode = msg[0];
	char *errtxt = msg;
	char *id;

	/* display for debugging */
	if (tTd(54, 8))
		sm_dprintf("--- %s%s%s\n", msg, holdmsg ? " (hold)" : "",
			heldmsg ? " (held)" : "");

	/* map warnings to something SMTP can handle */
	if (msgcode == '6')
		msg[0] = '5';
	else if (msgcode == '8')
		msg[0] = '4';
	id = (CurEnv != NULL) ? CurEnv->e_id : NULL;

	/* output to transcript if serious */
	if (!heldmsg && CurEnv != NULL && CurEnv->e_xfp != NULL &&
	    strchr("45", msg[0]) != NULL)
		(void) sm_io_fprintf(CurEnv->e_xfp, SM_TIME_DEFAULT, "%s\n",
				     msg);

	if (LogLevel > 14 && (OpMode == MD_SMTP || OpMode == MD_DAEMON))
		sm_syslog(LOG_INFO, id,
			  "--- %s%s%s", msg, holdmsg ? " (hold)" : "",
			  heldmsg ? " (held)" : "");

	if (msgcode == '8')
		msg[0] = '0';

	/* output to channel if appropriate */
	if (!Verbose && msg[0] == '0')
		return;
	if (holdmsg)
	{
		/* save for possible future display */
		msg[0] = msgcode;
		if (HeldMessageBuf[0] == '5' && msgcode == '4')
			return;
		(void) sm_strlcpy(HeldMessageBuf, msg, sizeof(HeldMessageBuf));
		return;
	}

	(void) sm_io_flush(smioout, SM_TIME_DEFAULT);

	if (OutChannel == NULL)
		return;

	/* find actual text of error (after SMTP status codes) */
	if (ISSMTPREPLY(errtxt))
	{
		int l;

		errtxt += 4;
		l = isenhsc(errtxt, ' ');
		if (l <= 0)
			l = isenhsc(errtxt, '\0');
		if (l > 0)
			errtxt += l + 1;
	}

	/* if DisConnected, OutChannel now points to the transcript */
	if (!DisConnected &&
	    (OpMode == MD_SMTP || OpMode == MD_DAEMON || OpMode == MD_ARPAFTP))
		(void) sm_io_fprintf(OutChannel, SM_TIME_DEFAULT, "%s\r\n",
				     msg);
	else
		(void) sm_io_fprintf(OutChannel, SM_TIME_DEFAULT, "%s\n",
				     errtxt);
	if (TrafficLogFile != NULL)
		(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
				     "%05d >>> %s\n", (int) CurrentPid,
				     (OpMode == MD_SMTP || OpMode == MD_DAEMON)
					? msg : errtxt);
#if !PIPELINING
	/* XXX can't flush here for SMTP pipelining */
	if (msg[3] == ' ')
		(void) sm_io_flush(OutChannel, SM_TIME_DEFAULT);
	if (!sm_io_error(OutChannel) || DisConnected)
		return;

	/*
	**  Error on output -- if reporting lost channel, just ignore it.
	**  Also, ignore errors from QUIT response (221 message) -- some
	**	rude servers don't read result.
	*/

	if (InChannel == NULL || sm_io_eof(InChannel) ||
	    sm_io_error(InChannel) || strncmp(msg, "221", 3) == 0)
		return;

	/* can't call syserr, 'cause we are using MsgBuf */
	HoldErrs = true;
	if (LogLevel > 0)
		sm_syslog(LOG_CRIT, id,
			  "SYSERR: putoutmsg (%s): error on output channel sending \"%s\": %s",
			  CURHOSTNAME,
			  shortenstring(msg, MAXSHORTSTR), sm_errstring(errno));
#endif /* !PIPELINING */
}
/*
**  PUTERRMSG -- like putoutmsg, but does special processing for error messages
**
**	Parameters:
**		msg -- the message to output.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets the fatal error bit in the envelope as appropriate.
*/

static void
puterrmsg(msg)
	char *msg;
{
	char msgcode = msg[0];

	/* output the message as usual */
	putoutmsg(msg, HoldErrs, false);

	/* be careful about multiple error messages */
	if (OnlyOneError)
		HoldErrs = true;

	/* signal the error */
	Errors++;

	if (CurEnv == NULL)
		return;

	if (msgcode == '6')
	{
		/* notify the postmaster */
		CurEnv->e_flags |= EF_PM_NOTIFY;
	}
	else if (msgcode == '5' && bitset(EF_GLOBALERRS, CurEnv->e_flags))
	{
		/* mark long-term fatal errors */
		CurEnv->e_flags |= EF_FATALERRS;
	}
}
/*
**  ISENHSC -- check whether a string contains an enhanced status code
**
**	Parameters:
**		s -- string with possible enhanced status code.
**		delim -- delim for enhanced status code.
**
**	Returns:
**		0  -- no enhanced status code.
**		>4 -- length of enhanced status code.
**
**	Side Effects:
**		none.
*/
int
isenhsc(s, delim)
	const char *s;
	int delim;
{
	int l, h;

	if (s == NULL)
		return 0;
	if (!((*s == '2' || *s == '4' || *s == '5') && s[1] == '.'))
		return 0;
	h = 0;
	l = 2;
	while (h < 3 && isascii(s[l + h]) && isdigit(s[l + h]))
		++h;
	if (h == 0 || s[l + h] != '.')
		return 0;
	l += h + 1;
	h = 0;
	while (h < 3 && isascii(s[l + h]) && isdigit(s[l + h]))
		++h;
	if (h == 0 || s[l + h] != delim)
		return 0;
	return l + h;
}
/*
**  EXTENHSC -- check and extract an enhanced status code
**
**	Parameters:
**		s -- string with possible enhanced status code.
**		delim -- delim for enhanced status code.
**		e -- pointer to storage for enhanced status code.
**			must be != NULL and have space for at least
**			10 characters ([245].[0-9]{1,3}.[0-9]{1,3})
**
**	Returns:
**		0  -- no enhanced status code.
**		>4 -- length of enhanced status code.
**
**	Side Effects:
**		fills e with enhanced status code.
*/

int
extenhsc(s, delim, e)
	const char *s;
	int delim;
	char *e;
{
	int l, h;

	if (s == NULL)
		return 0;
	if (!((*s == '2' || *s == '4' || *s == '5') && s[1] == '.'))
		return 0;
	h = 0;
	l = 2;
	e[0] = s[0];
	e[1] = '.';
	while (h < 3 && isascii(s[l + h]) && isdigit(s[l + h]))
	{
		e[l + h] = s[l + h];
		++h;
	}
	if (h == 0 || s[l + h] != '.')
		return 0;
	e[l + h] = '.';
	l += h + 1;
	h = 0;
	while (h < 3 && isascii(s[l + h]) && isdigit(s[l + h]))
	{
		e[l + h] = s[l + h];
		++h;
	}
	if (h == 0 || s[l + h] != delim)
		return 0;
	e[l + h] = '\0';
	return l + h;
}
/*
**  FMTMSG -- format a message into buffer.
**
**	Parameters:
**		eb -- error buffer to get result -- MUST BE MsgBuf.
**		to -- the recipient tag for this message.
**		num -- default three digit SMTP reply code.
**		enhsc -- enhanced status code.
**		en -- the error number to display.
**		fmt -- format of string.
**		ap -- arguments for fmt.
**
**	Returns:
**		pointer to error text beyond status codes.
**
**	Side Effects:
**		none.
*/

static char *
fmtmsg(eb, to, num, enhsc, eno, fmt, ap)
	register char *eb;
	const char *to;
	const char *num;
	const char *enhsc;
	int eno;
	const char *fmt;
	SM_VA_LOCAL_DECL
{
	char del;
	int l;
	int spaceleft = sizeof(MsgBuf);
	char *errtxt;

	/* output the reply code */
	if (ISSMTPCODE(fmt))
	{
		num = fmt;
		fmt += 4;
	}
	if (num[3] == '-')
		del = '-';
	else
		del = ' ';
	if (SoftBounce && num[0] == '5')
	{
		/* replace 5 by 4 */
		(void) sm_snprintf(eb, spaceleft, "4%2.2s%c", num + 1, del);
	}
	else
		(void) sm_snprintf(eb, spaceleft, "%3.3s%c", num, del);
	eb += 4;
	spaceleft -= 4;

	if ((l = isenhsc(fmt, ' ' )) > 0 && l < spaceleft - 4)
	{
		/* copy enh.status code including trailing blank */
		l++;
		(void) sm_strlcpy(eb, fmt, l + 1);
		eb += l;
		spaceleft -= l;
		fmt += l;
	}
	else if ((l = isenhsc(enhsc, '\0')) > 0 && l < spaceleft - 4)
	{
		/* copy enh.status code */
		(void) sm_strlcpy(eb, enhsc, l + 1);
		eb[l] = ' ';
		eb[++l] = '\0';
		eb += l;
		spaceleft -= l;
	}
	if (SoftBounce && eb[-l] == '5')
	{
		/* replace 5 by 4 */
		eb[-l] = '4';
	}
	errtxt = eb;

	/* output the file name and line number */
	if (FileName != NULL)
	{
		(void) sm_snprintf(eb, spaceleft, "%s: line %d: ",
				   shortenstring(FileName, 83), LineNumber);
		eb += (l = strlen(eb));
		spaceleft -= l;
	}

	/*
	**  output the "to" address only if it is defined and one of the
	**  following codes is used:
	**  050 internal notices, e.g., alias expansion
	**  250 Ok
	**  252 Cannot VRFY user, but will accept message and attempt delivery
	**  450 Requested mail action not taken: mailbox unavailable
	**  550 Requested action not taken: mailbox unavailable
	**  553 Requested action not taken: mailbox name not allowed
	**
	**  Notice: this still isn't "the right thing", this code shouldn't
	**	(indirectly) depend on CurEnv->e_to.
	*/

	if (to != NULL && to[0] != '\0' &&
	    (strncmp(num, "050", 3) == 0 ||
	     strncmp(num, "250", 3) == 0 ||
	     strncmp(num, "252", 3) == 0 ||
	     strncmp(num, "450", 3) == 0 ||
	     strncmp(num, "550", 3) == 0 ||
	     strncmp(num, "553", 3) == 0))
	{
		(void) sm_strlcpyn(eb, spaceleft, 2,
				   shortenstring(to, MAXSHORTSTR), "... ");
		spaceleft -= strlen(eb);
		while (*eb != '\0')
			*eb++ &= 0177;
	}

	/* output the message */
	(void) sm_vsnprintf(eb, spaceleft, fmt, ap);
	spaceleft -= strlen(eb);
	while (*eb != '\0')
		*eb++ &= 0177;

	/* output the error code, if any */
	if (eno != 0)
		(void) sm_strlcpyn(eb, spaceleft, 2, ": ", sm_errstring(eno));

	return errtxt;
}
/*
**  BUFFER_ERRORS -- arrange to buffer future error messages
**
**	Parameters:
**		none
**
**	Returns:
**		none.
*/

void
buffer_errors()
{
	HeldMessageBuf[0] = '\0';
	HoldErrs = true;
}
/*
**  FLUSH_ERRORS -- flush the held error message buffer
**
**	Parameters:
**		print -- if set, print the message, otherwise just
**			delete it.
**
**	Returns:
**		none.
*/

void
flush_errors(print)
	bool print;
{
	if (print && HeldMessageBuf[0] != '\0')
		putoutmsg(HeldMessageBuf, false, true);
	HeldMessageBuf[0] = '\0';
	HoldErrs = false;
}
/*
**  SM_ERRSTRING -- return string description of error code
**
**	Parameters:
**		errnum -- the error number to translate
**
**	Returns:
**		A string description of errnum.
**
**	Side Effects:
**		none.
*/

const char *
sm_errstring(errnum)
	int errnum;
{
	char *dnsmsg;
	char *bp;
	static char buf[MAXLINE];
#if HASSTRERROR
	char *err;
	char errbuf[30];
#endif /* HASSTRERROR */
#if !HASSTRERROR && !defined(ERRLIST_PREDEFINED)
	extern char *sys_errlist[];
	extern int sys_nerr;
#endif /* !HASSTRERROR && !defined(ERRLIST_PREDEFINED) */

	/*
	**  Handle special network error codes.
	**
	**	These are 4.2/4.3bsd specific; they should be in daemon.c.
	*/

	dnsmsg = NULL;
	switch (errnum)
	{
	  case ETIMEDOUT:
	  case ECONNRESET:
		bp = buf;
#if HASSTRERROR
		err = strerror(errnum);
		if (err == NULL)
		{
			(void) sm_snprintf(errbuf, sizeof(errbuf),
					   "Error %d", errnum);
			err = errbuf;
		}
		(void) sm_strlcpy(bp, err, SPACELEFT(buf, bp));
#else /* HASSTRERROR */
		if (errnum >= 0 && errnum < sys_nerr)
			(void) sm_strlcpy(bp, sys_errlist[errnum],
					  SPACELEFT(buf, bp));
		else
			(void) sm_snprintf(bp, SPACELEFT(buf, bp),
				"Error %d", errnum);
#endif /* HASSTRERROR */
		bp += strlen(bp);
		if (CurHostName != NULL)
		{
			if (errnum == ETIMEDOUT)
			{
				(void) sm_snprintf(bp, SPACELEFT(buf, bp),
					" with ");
				bp += strlen(bp);
			}
			else
			{
				bp = buf;
				(void) sm_snprintf(bp, SPACELEFT(buf, bp),
					"Connection reset by ");
				bp += strlen(bp);
			}
			(void) sm_strlcpy(bp,
					shortenstring(CurHostName, MAXSHORTSTR),
					SPACELEFT(buf, bp));
			bp += strlen(buf);
		}
		if (SmtpPhase != NULL)
		{
			(void) sm_snprintf(bp, SPACELEFT(buf, bp),
				" during %s", SmtpPhase);
		}
		return buf;

	  case EHOSTDOWN:
		if (CurHostName == NULL)
			break;
		(void) sm_snprintf(buf, sizeof(buf), "Host %s is down",
			shortenstring(CurHostName, MAXSHORTSTR));
		return buf;

	  case ECONNREFUSED:
		if (CurHostName == NULL)
			break;
		(void) sm_strlcpyn(buf, sizeof(buf), 2, "Connection refused by ",
			shortenstring(CurHostName, MAXSHORTSTR));
		return buf;

#if NAMED_BIND
	  case HOST_NOT_FOUND + E_DNSBASE:
		dnsmsg = "host not found";
		break;

	  case TRY_AGAIN + E_DNSBASE:
		dnsmsg = "host name lookup failure";
		break;

	  case NO_RECOVERY + E_DNSBASE:
		dnsmsg = "non-recoverable error";
		break;

	  case NO_DATA + E_DNSBASE:
		dnsmsg = "no data known";
		break;
#endif /* NAMED_BIND */

	  case EPERM:
		/* SunOS gives "Not owner" -- this is the POSIX message */
		return "Operation not permitted";

	/*
	**  Error messages used internally in sendmail.
	*/

	  case E_SM_OPENTIMEOUT:
		return "Timeout on file open";

	  case E_SM_NOSLINK:
		return "Symbolic links not allowed";

	  case E_SM_NOHLINK:
		return "Hard links not allowed";

	  case E_SM_REGONLY:
		return "Regular files only";

	  case E_SM_ISEXEC:
		return "Executable files not allowed";

	  case E_SM_WWDIR:
		return "World writable directory";

	  case E_SM_GWDIR:
		return "Group writable directory";

	  case E_SM_FILECHANGE:
		return "File changed after open";

	  case E_SM_WWFILE:
		return "World writable file";

	  case E_SM_GWFILE:
		return "Group writable file";

	  case E_SM_GRFILE:
		return "Group readable file";

	  case E_SM_WRFILE:
		return "World readable file";
	}

	if (dnsmsg != NULL)
	{
		bp = buf;
		bp += sm_strlcpy(bp, "Name server: ", sizeof(buf));
		if (CurHostName != NULL)
		{
			(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2,
				shortenstring(CurHostName, MAXSHORTSTR), ": ");
			bp += strlen(bp);
		}
		(void) sm_strlcpy(bp, dnsmsg, SPACELEFT(buf, bp));
		return buf;
	}

#if LDAPMAP
	if (errnum >= E_LDAPBASE - E_LDAP_SHIM)
		return ldap_err2string(errnum - E_LDAPBASE);
#endif /* LDAPMAP */

#if HASSTRERROR
	err = strerror(errnum);
	if (err == NULL)
	{
		(void) sm_snprintf(buf, sizeof(buf), "Error %d", errnum);
		return buf;
	}
	return err;
#else /* HASSTRERROR */
	if (errnum > 0 && errnum < sys_nerr)
		return sys_errlist[errnum];

	(void) sm_snprintf(buf, sizeof(buf), "Error %d", errnum);
	return buf;
#endif /* HASSTRERROR */
}
