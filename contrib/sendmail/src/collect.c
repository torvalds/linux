/*
 * Copyright (c) 1998-2006, 2008 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: collect.c,v 8.287 2013-11-22 20:51:55 ca Exp $")

static void	eatfrom __P((char *volatile, ENVELOPE *));
static void	collect_doheader __P((ENVELOPE *));
static SM_FILE_T *collect_dfopen __P((ENVELOPE *));
static SM_FILE_T *collect_eoh __P((ENVELOPE *, int, int));

/*
**  COLLECT_EOH -- end-of-header processing in collect()
**
**	Called by collect() when it encounters the blank line
**	separating the header from the message body, or when it
**	encounters EOF in a message that contains only a header.
**
**	Parameters:
**		e -- envelope
**		numhdrs -- number of headers
**		hdrslen -- length of headers
**
**	Results:
**		NULL, or handle to open data file
**
**	Side Effects:
**		end-of-header check ruleset is invoked.
**		envelope state is updated.
**		headers may be added and deleted.
**		selects the queue.
**		opens the data file.
*/

static SM_FILE_T *
collect_eoh(e, numhdrs, hdrslen)
	ENVELOPE *e;
	int numhdrs;
	int hdrslen;
{
	char hnum[16];
	char hsize[16];

	/* call the end-of-header check ruleset */
	(void) sm_snprintf(hnum, sizeof(hnum), "%d", numhdrs);
	(void) sm_snprintf(hsize, sizeof(hsize), "%d", hdrslen);
	if (tTd(30, 10))
		sm_dprintf("collect: rscheck(\"check_eoh\", \"%s $| %s\")\n",
			   hnum, hsize);
	(void) rscheck("check_eoh", hnum, hsize, e, RSF_UNSTRUCTURED|RSF_COUNT,
			3, NULL, e->e_id, NULL, NULL);

	/*
	**  Process the header,
	**  select the queue, open the data file.
	*/

	collect_doheader(e);
	return collect_dfopen(e);
}

/*
**  COLLECT_DOHEADER -- process header in collect()
**
**	Called by collect() after it has finished parsing the header,
**	but before it selects the queue and creates the data file.
**	The results of processing the header will affect queue selection.
**
**	Parameters:
**		e -- envelope
**
**	Results:
**		none.
**
**	Side Effects:
**		envelope state is updated.
**		headers may be added and deleted.
*/

static void
collect_doheader(e)
	ENVELOPE *e;
{
	/*
	**  Find out some information from the headers.
	**	Examples are who is the from person & the date.
	*/

	eatheader(e, true, false);

	if (GrabTo && e->e_sendqueue == NULL)
		usrerr("No recipient addresses found in header");

	/*
	**  If we have a Return-Receipt-To:, turn it into a DSN.
	*/

	if (RrtImpliesDsn && hvalue("return-receipt-to", e->e_header) != NULL)
	{
		ADDRESS *q;

		for (q = e->e_sendqueue; q != NULL; q = q->q_next)
			if (!bitset(QHASNOTIFY, q->q_flags))
				q->q_flags |= QHASNOTIFY|QPINGONSUCCESS;
	}

	/*
	**  Add an appropriate recipient line if we have none.
	*/

	if (hvalue("to", e->e_header) != NULL ||
	    hvalue("cc", e->e_header) != NULL ||
	    hvalue("apparently-to", e->e_header) != NULL)
	{
		/* have a valid recipient header -- delete Bcc: headers */
		e->e_flags |= EF_DELETE_BCC;
	}
	else if (hvalue("bcc", e->e_header) == NULL)
	{
		/* no valid recipient headers */
		register ADDRESS *q;
		char *hdr = NULL;

		/* create a recipient field */
		switch (NoRecipientAction)
		{
		  case NRA_ADD_APPARENTLY_TO:
			hdr = "Apparently-To";
			break;

		  case NRA_ADD_TO:
			hdr = "To";
			break;

		  case NRA_ADD_BCC:
			addheader("Bcc", " ", 0, e, true);
			break;

		  case NRA_ADD_TO_UNDISCLOSED:
			addheader("To", "undisclosed-recipients:;", 0, e, true);
			break;
		}

		if (hdr != NULL)
		{
			for (q = e->e_sendqueue; q != NULL; q = q->q_next)
			{
				if (q->q_alias != NULL)
					continue;
				if (tTd(30, 3))
					sm_dprintf("Adding %s: %s\n",
						hdr, q->q_paddr);
				addheader(hdr, q->q_paddr, 0, e, true);
			}
		}
	}
}

/*
**  COLLECT_DFOPEN -- open the message data file
**
**	Called by collect() after it has finished processing the header.
**	Queue selection occurs at this point, possibly based on the
**	envelope's recipient list and on header information.
**
**	Parameters:
**		e -- envelope
**
**	Results:
**		NULL, or a pointer to an open data file,
**		into which the message body will be written by collect().
**
**	Side Effects:
**		Calls syserr, sets EF_FATALERRS and returns NULL
**		if there is insufficient disk space.
**		Aborts process if data file could not be opened.
**		Otherwise, the queue is selected,
**		e->e_{dfino,dfdev,msgsize,flags} are updated,
**		and a pointer to an open data file is returned.
*/

static SM_FILE_T *
collect_dfopen(e)
	ENVELOPE *e;
{
	MODE_T oldumask = 0;
	int dfd;
	struct stat stbuf;
	SM_FILE_T *df;
	char *dfname;

	if (!setnewqueue(e))
		return NULL;

	dfname = queuename(e, DATAFL_LETTER);
	if (bitset(S_IWGRP, QueueFileMode))
		oldumask = umask(002);
	df = bfopen(dfname, QueueFileMode, DataFileBufferSize,
		    SFF_OPENASROOT);
	if (bitset(S_IWGRP, QueueFileMode))
		(void) umask(oldumask);
	if (df == NULL)
	{
		syserr("@Cannot create %s", dfname);
		e->e_flags |= EF_NO_BODY_RETN;
		flush_errors(true);
		finis(false, true, ExitStat);
		/* NOTREACHED */
	}
	dfd = sm_io_getinfo(df, SM_IO_WHAT_FD, NULL);
	if (dfd < 0 || fstat(dfd, &stbuf) < 0)
		e->e_dfino = -1;
	else
	{
		e->e_dfdev = stbuf.st_dev;
		e->e_dfino = stbuf.st_ino;
	}
	e->e_flags |= EF_HAS_DF;
	return df;
}

/*
**  COLLECT -- read & parse message header & make temp file.
**
**	Creates a temporary file name and copies the standard
**	input to that file.  Leading UNIX-style "From" lines are
**	stripped off (after important information is extracted).
**
**	Parameters:
**		fp -- file to read.
**		smtpmode -- if set, we are running SMTP: give an RFC821
**			style message to say we are ready to collect
**			input, and never ignore a single dot to mean
**			end of message.
**		hdrp -- the location to stash the header.
**		e -- the current envelope.
**		rsetsize -- reset e_msgsize?
**
**	Returns:
**		none.
**
**	Side Effects:
**		If successful,
**		- Data file is created and filled, and e->e_dfp is set.
**		- The from person may be set.
**		If the "enough disk space" check fails,
**		- syserr is called.
**		- e->e_dfp is NULL.
**		- e->e_flags & EF_FATALERRS is set.
**		- collect() returns.
**		If data file cannot be created, the process is terminated.
*/

/* values for input state machine */
#define IS_NORM		0	/* middle of line */
#define IS_BOL		1	/* beginning of line */
#define IS_DOT		2	/* read a dot at beginning of line */
#define IS_DOTCR	3	/* read ".\r" at beginning of line */
#define IS_CR		4	/* read a carriage return */

/* values for message state machine */
#define MS_UFROM	0	/* reading Unix from line */
#define MS_HEADER	1	/* reading message header */
#define MS_BODY		2	/* reading message body */
#define MS_DISCARD	3	/* discarding rest of message */

void
collect(fp, smtpmode, hdrp, e, rsetsize)
	SM_FILE_T *fp;
	bool smtpmode;
	HDR **hdrp;
	register ENVELOPE *e;
	bool rsetsize;
{
	register SM_FILE_T *df;
	bool ignrdot;
	int dbto;
	register char *bp;
	int c;
	bool inputerr;
	bool headeronly;
	char *buf;
	int buflen;
	int istate;
	int mstate;
	int hdrslen;
	int numhdrs;
	int afd;
	int old_rd_tmo;
	unsigned char *pbp;
	unsigned char peekbuf[8];
	char bufbuf[MAXLINE];
#if _FFR_REJECT_NUL_BYTE
	bool hasNUL;		/* has at least one NUL input byte */
#endif /* _FFR_REJECT_NUL_BYTE */

	df = NULL;
	ignrdot = smtpmode ? false : IgnrDot;

	/* timeout for I/O functions is in milliseconds */
	dbto = smtpmode ? ((int) TimeOuts.to_datablock * 1000)
			: SM_TIME_FOREVER;
	sm_io_setinfo(fp, SM_IO_WHAT_TIMEOUT, &dbto);
	old_rd_tmo = set_tls_rd_tmo(TimeOuts.to_datablock);
	c = SM_IO_EOF;
	inputerr = false;
	headeronly = hdrp != NULL;
	hdrslen = 0;
	numhdrs = 0;
	HasEightBits = false;
#if _FFR_REJECT_NUL_BYTE
	hasNUL = false;
#endif /* _FFR_REJECT_NUL_BYTE */
	buf = bp = bufbuf;
	buflen = sizeof(bufbuf);
	pbp = peekbuf;
	istate = IS_BOL;
	mstate = SaveFrom ? MS_HEADER : MS_UFROM;

	/*
	**  Tell ARPANET to go ahead.
	*/

	if (smtpmode)
		message("354 Enter mail, end with \".\" on a line by itself");

	/* simulate an I/O timeout when used as sink */
	if (tTd(83, 101))
		sleep(319);

	if (tTd(30, 2))
		sm_dprintf("collect\n");

	/*
	**  Read the message.
	**
	**	This is done using two interleaved state machines.
	**	The input state machine is looking for things like
	**	hidden dots; the message state machine is handling
	**	the larger picture (e.g., header versus body).
	*/

	if (rsetsize)
		e->e_msgsize = 0;
	for (;;)
	{
		if (tTd(30, 35))
			sm_dprintf("top, istate=%d, mstate=%d\n", istate,
				   mstate);
		for (;;)
		{
			if (pbp > peekbuf)
				c = *--pbp;
			else
			{
				while (!sm_io_eof(fp) && !sm_io_error(fp))
				{
					errno = 0;
					c = sm_io_getc(fp, SM_TIME_DEFAULT);
					if (c == SM_IO_EOF && errno == EINTR)
					{
						/* Interrupted, retry */
						sm_io_clearerr(fp);
						continue;
					}

					/* timeout? */
					if (c == SM_IO_EOF && errno == EAGAIN
					    && smtpmode)
					{
						/*
						**  Override e_message in
						**  usrerr() as this is the
						**  reason for failure that
						**  should be logged for
						**  undelivered recipients.
						*/

						e->e_message = NULL;
						errno = 0;
						inputerr = true;
						goto readabort;
					}
					break;
				}
				if (TrafficLogFile != NULL && !headeronly)
				{
					if (istate == IS_BOL)
						(void) sm_io_fprintf(TrafficLogFile,
							SM_TIME_DEFAULT,
							"%05d <<< ",
							(int) CurrentPid);
					if (c == SM_IO_EOF)
						(void) sm_io_fprintf(TrafficLogFile,
							SM_TIME_DEFAULT,
							"[EOF]\n");
					else
						(void) sm_io_putc(TrafficLogFile,
							SM_TIME_DEFAULT,
							c);
				}
#if _FFR_REJECT_NUL_BYTE
				if (c == '\0')
					hasNUL = true;
#endif /* _FFR_REJECT_NUL_BYTE */
				if (c == SM_IO_EOF)
					goto readerr;
				if (SevenBitInput)
					c &= 0x7f;
				else
					HasEightBits |= bitset(0x80, c);
			}
			if (tTd(30, 94))
				sm_dprintf("istate=%d, c=%c (0x%x)\n",
					istate, (char) c, c);
			switch (istate)
			{
			  case IS_BOL:
				if (c == '.')
				{
					istate = IS_DOT;
					continue;
				}
				break;

			  case IS_DOT:
				if (c == '\n' && !ignrdot &&
				    !bitset(EF_NL_NOT_EOL, e->e_flags))
					goto readerr;
				else if (c == '\r' &&
					 !bitset(EF_CRLF_NOT_EOL, e->e_flags))
				{
					istate = IS_DOTCR;
					continue;
				}
				else if (ignrdot ||
					 (c != '.' &&
					  OpMode != MD_SMTP &&
					  OpMode != MD_DAEMON &&
					  OpMode != MD_ARPAFTP))

				{
					SM_ASSERT(pbp < peekbuf +
							sizeof(peekbuf));
					*pbp++ = c;
					c = '.';
				}
				break;

			  case IS_DOTCR:
				if (c == '\n' && !ignrdot)
					goto readerr;
				else
				{
					/* push back the ".\rx" */
					SM_ASSERT(pbp < peekbuf +
							sizeof(peekbuf));
					*pbp++ = c;
					if (OpMode != MD_SMTP &&
					    OpMode != MD_DAEMON &&
					    OpMode != MD_ARPAFTP)
					{
						SM_ASSERT(pbp < peekbuf +
							 sizeof(peekbuf));
						*pbp++ = '\r';
						c = '.';
					}
					else
						c = '\r';
				}
				break;

			  case IS_CR:
				if (c == '\n')
					istate = IS_BOL;
				else
				{
					(void) sm_io_ungetc(fp, SM_TIME_DEFAULT,
							    c);
					c = '\r';
					istate = IS_NORM;
				}
				goto bufferchar;
			}

			if (c == '\r' && !bitset(EF_CRLF_NOT_EOL, e->e_flags))
			{
				istate = IS_CR;
				continue;
			}
			else if (c == '\n' && !bitset(EF_NL_NOT_EOL,
						      e->e_flags))
				istate = IS_BOL;
			else
				istate = IS_NORM;

bufferchar:
			if (!headeronly)
			{
				/* no overflow? */
				if (e->e_msgsize >= 0)
				{
					e->e_msgsize++;
					if (MaxMessageSize > 0 &&
					    !bitset(EF_TOOBIG, e->e_flags) &&
					    e->e_msgsize > MaxMessageSize)
						 e->e_flags |= EF_TOOBIG;
				}
			}
			switch (mstate)
			{
			  case MS_BODY:
				/* just put the character out */
				if (!bitset(EF_TOOBIG, e->e_flags))
					(void) sm_io_putc(df, SM_TIME_DEFAULT,
							  c);

				/* FALLTHROUGH */

			  case MS_DISCARD:
				continue;
			}

			SM_ASSERT(mstate == MS_UFROM || mstate == MS_HEADER);

			/* header -- buffer up */
			if (bp >= &buf[buflen - 2])
			{
				char *obuf;

				/* out of space for header */
				obuf = buf;
				if (buflen < MEMCHUNKSIZE)
					buflen *= 2;
				else
					buflen += MEMCHUNKSIZE;
				if (buflen <= 0)
				{
					sm_syslog(LOG_NOTICE, e->e_id,
						  "header overflow from %s during message collect",
						  CURHOSTNAME);
					errno = 0;
					e->e_flags |= EF_CLRQUEUE;
					e->e_status = "5.6.0";
					usrerrenh(e->e_status,
						  "552 Headers too large");
					goto discard;
				}
				buf = xalloc(buflen);
				memmove(buf, obuf, bp - obuf);
				bp = &buf[bp - obuf];
				if (obuf != bufbuf)
					sm_free(obuf);  /* XXX */
			}

			if (c != '\0')
			{
				*bp++ = c;
				++hdrslen;
				if (!headeronly &&
				    MaxHeadersLength > 0 &&
				    hdrslen > MaxHeadersLength)
				{
					sm_syslog(LOG_NOTICE, e->e_id,
						  "headers too large (%d max) from %s during message collect",
						  MaxHeadersLength,
						  CURHOSTNAME);
					errno = 0;
					e->e_flags |= EF_CLRQUEUE;
					e->e_status = "5.6.0";
					usrerrenh(e->e_status,
						  "552 Headers too large (%d max)",
						  MaxHeadersLength);
  discard:
					mstate = MS_DISCARD;
				}
			}
			if (istate == IS_BOL)
				break;
		}
		*bp = '\0';

nextstate:
		if (tTd(30, 35))
			sm_dprintf("nextstate, istate=%d, mstate=%d, line=\"%s\"\n",
				istate, mstate, buf);
		switch (mstate)
		{
		  case MS_UFROM:
			mstate = MS_HEADER;
#ifndef NOTUNIX
			if (strncmp(buf, "From ", 5) == 0)
			{
				bp = buf;
				eatfrom(buf, e);
				continue;
			}
#endif /* ! NOTUNIX */
			/* FALLTHROUGH */

		  case MS_HEADER:
			if (!isheader(buf))
			{
				mstate = MS_BODY;
				goto nextstate;
			}

			/* check for possible continuation line */
			do
			{
				sm_io_clearerr(fp);
				errno = 0;
				c = sm_io_getc(fp, SM_TIME_DEFAULT);

				/* timeout? */
				if (c == SM_IO_EOF && errno == EAGAIN
				    && smtpmode)
				{
					/*
					**  Override e_message in
					**  usrerr() as this is the
					**  reason for failure that
					**  should be logged for
					**  undelivered recipients.
					*/

					e->e_message = NULL;
					errno = 0;
					inputerr = true;
					goto readabort;
				}
			} while (c == SM_IO_EOF && errno == EINTR);
			if (c != SM_IO_EOF)
				(void) sm_io_ungetc(fp, SM_TIME_DEFAULT, c);
			if (c == ' ' || c == '\t')
			{
				/* yep -- defer this */
				continue;
			}

			SM_ASSERT(bp > buf);

			/* guaranteed by isheader(buf) */
			SM_ASSERT(*(bp - 1) != '\n' || bp > buf + 1);

			/* trim off trailing CRLF or NL */
			if (*--bp != '\n' || *--bp != '\r')
				bp++;
			*bp = '\0';

			if (bitset(H_EOH, chompheader(buf,
						      CHHDR_CHECK | CHHDR_USER,
						      hdrp, e)))
			{
				mstate = MS_BODY;
				goto nextstate;
			}
			numhdrs++;
			break;

		  case MS_BODY:
			if (tTd(30, 1))
				sm_dprintf("EOH\n");

			if (headeronly)
				goto readerr;

			df = collect_eoh(e, numhdrs, hdrslen);
			if (df == NULL)
				e->e_flags |= EF_TOOBIG;

			bp = buf;

			/* toss blank line */
			if ((!bitset(EF_CRLF_NOT_EOL, e->e_flags) &&
				bp[0] == '\r' && bp[1] == '\n') ||
			    (!bitset(EF_NL_NOT_EOL, e->e_flags) &&
				bp[0] == '\n'))
			{
				break;
			}

			/* if not a blank separator, write it out */
			if (!bitset(EF_TOOBIG, e->e_flags))
			{
				while (*bp != '\0')
					(void) sm_io_putc(df, SM_TIME_DEFAULT,
							  *bp++);
			}
			break;
		}
		bp = buf;
	}

readerr:
	if ((sm_io_eof(fp) && smtpmode) || sm_io_error(fp))
	{
		const char *errmsg;

		if (sm_io_eof(fp))
			errmsg = "unexpected close";
		else
			errmsg = sm_errstring(errno);
		if (tTd(30, 1))
			sm_dprintf("collect: premature EOM: %s\n", errmsg);
		if (LogLevel > 1)
			sm_syslog(LOG_WARNING, e->e_id,
				"collect: premature EOM: %s", errmsg);
		inputerr = true;
	}

	if (headeronly)
		goto end;

	if (mstate != MS_BODY)
	{
		/* no body or discard, so we never opened the data file */
		SM_ASSERT(df == NULL);
		df = collect_eoh(e, numhdrs, hdrslen);
	}

	if (df == NULL)
	{
		/* skip next few clauses */
		/* EMPTY */
	}
	else if (sm_io_flush(df, SM_TIME_DEFAULT) != 0 || sm_io_error(df))
	{
		dferror(df, "sm_io_flush||sm_io_error", e);
		flush_errors(true);
		finis(true, true, ExitStat);
		/* NOTREACHED */
	}
	else if (SuperSafe == SAFE_NO ||
		 SuperSafe == SAFE_INTERACTIVE ||
		 (SuperSafe == SAFE_REALLY_POSTMILTER && smtpmode))
	{
		/* skip next few clauses */
		/* EMPTY */
		/* Note: updfs() is not called in this case! */
	}
	else if (sm_io_setinfo(df, SM_BF_COMMIT, NULL) < 0 && errno != EINVAL)
	{
		int save_errno = errno;

		if (save_errno == EEXIST)
		{
			char *dfile;
			struct stat st;
			int dfd;

			dfile = queuename(e, DATAFL_LETTER);
			if (stat(dfile, &st) < 0)
				st.st_size = -1;
			errno = EEXIST;
			syserr("@collect: bfcommit(%s): already on disk, size=%ld",
			       dfile, (long) st.st_size);
			dfd = sm_io_getinfo(df, SM_IO_WHAT_FD, NULL);
			if (dfd >= 0)
				dumpfd(dfd, true, true);
		}
		errno = save_errno;
		dferror(df, "bfcommit", e);
		flush_errors(true);
		finis(save_errno != EEXIST, true, ExitStat);
	}
	else if ((afd = sm_io_getinfo(df, SM_IO_WHAT_FD, NULL)) < 0)
	{
		dferror(df, "sm_io_getinfo", e);
		flush_errors(true);
		finis(true, true, ExitStat);
		/* NOTREACHED */
	}
	else if (fsync(afd) < 0)
	{
		dferror(df, "fsync", e);
		flush_errors(true);
		finis(true, true, ExitStat);
		/* NOTREACHED */
	}
	else if (sm_io_close(df, SM_TIME_DEFAULT) < 0)
	{
		dferror(df, "sm_io_close", e);
		flush_errors(true);
		finis(true, true, ExitStat);
		/* NOTREACHED */
	}
	else
	{
		/* everything is happily flushed to disk */
		df = NULL;

		/* remove from available space in filesystem */
		updfs(e, 0, 1, "collect");
	}

	/* An EOF when running SMTP is an error */
  readabort:
	if (inputerr && (OpMode == MD_SMTP || OpMode == MD_DAEMON))
	{
		char *host;
		char *problem;
		ADDRESS *q;

		host = RealHostName;
		if (host == NULL)
			host = "localhost";

		if (sm_io_eof(fp))
			problem = "unexpected close";
		else if (sm_io_error(fp))
			problem = "I/O error";
		else
			problem = "read timeout";
		if (LogLevel > 0 && sm_io_eof(fp))
			sm_syslog(LOG_NOTICE, e->e_id,
				"collect: %s on connection from %.100s, sender=%s",
				problem, host,
				shortenstring(e->e_from.q_paddr, MAXSHORTSTR));
		if (sm_io_eof(fp))
			usrerr("421 4.4.1 collect: %s on connection from %s, from=%s",
				problem, host,
				shortenstring(e->e_from.q_paddr, MAXSHORTSTR));
		else
			syserr("421 4.4.1 collect: %s on connection from %s, from=%s",
				problem, host,
				shortenstring(e->e_from.q_paddr, MAXSHORTSTR));
		flush_errors(true);

		/* don't return an error indication */
		e->e_to = NULL;
		e->e_flags &= ~EF_FATALERRS;
		e->e_flags |= EF_CLRQUEUE;

		/* Don't send any message notification to sender */
		for (q = e->e_sendqueue; q != NULL; q = q->q_next)
		{
			if (QS_IS_DEAD(q->q_state))
				continue;
			q->q_state = QS_FATALERR;
		}

		(void) sm_io_close(df, SM_TIME_DEFAULT);
		df = NULL;
		finis(true, true, ExitStat);
		/* NOTREACHED */
	}

	/* Log collection information. */
	if (tTd(92, 2))
		sm_dprintf("collect: e_id=%s, EF_LOGSENDER=%d, LogLevel=%d\n",
			e->e_id, bitset(EF_LOGSENDER, e->e_flags), LogLevel);
	if (bitset(EF_LOGSENDER, e->e_flags) && LogLevel > 4)
	{
		logsender(e, e->e_msgid);
		e->e_flags &= ~EF_LOGSENDER;
	}

	/* check for message too large */
	if (bitset(EF_TOOBIG, e->e_flags))
	{
		e->e_flags |= EF_NO_BODY_RETN|EF_CLRQUEUE;
		if (!bitset(EF_FATALERRS, e->e_flags))
		{
			e->e_status = "5.2.3";
			usrerrenh(e->e_status,
				"552 Message exceeds maximum fixed size (%ld)",
				MaxMessageSize);
			if (LogLevel > 6)
				sm_syslog(LOG_NOTICE, e->e_id,
					"message size (%ld) exceeds maximum (%ld)",
					PRT_NONNEGL(e->e_msgsize),
					MaxMessageSize);
		}
	}

	/* check for illegal 8-bit data */
	if (HasEightBits)
	{
		e->e_flags |= EF_HAS8BIT;
		if (!bitset(MM_PASS8BIT|MM_MIME8BIT, MimeMode) &&
		    !bitset(EF_IS_MIME, e->e_flags))
		{
			e->e_status = "5.6.1";
			usrerrenh(e->e_status, "554 Eight bit data not allowed");
		}
	}
	else
	{
		/* if it claimed to be 8 bits, well, it lied.... */
		if (e->e_bodytype != NULL &&
		    sm_strcasecmp(e->e_bodytype, "8BITMIME") == 0)
			e->e_bodytype = "7BIT";
	}

#if _FFR_REJECT_NUL_BYTE
	if (hasNUL && RejectNUL)
	{
		e->e_status = "5.6.1";
		usrerrenh(e->e_status, "554 NUL byte not allowed");
	}
#endif /* _FFR_REJECT_NUL_BYTE */

	if (SuperSafe == SAFE_REALLY && !bitset(EF_FATALERRS, e->e_flags))
	{
		char *dfname = queuename(e, DATAFL_LETTER);
		if ((e->e_dfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, dfname,
					   SM_IO_RDONLY_B, NULL)) == NULL)
		{
			/* we haven't acked receipt yet, so just chuck this */
			syserr("@Cannot reopen %s", dfname);
			finis(true, true, ExitStat);
			/* NOTREACHED */
		}
	}
	else
		e->e_dfp = df;

	/* collect statistics */
	if (OpMode != MD_VERIFY)
	{
		/*
		**  Recalculate e_msgpriority, it is done at in eatheader()
		**  which is called (in 8.12) after the header is collected,
		**  hence e_msgsize is (most likely) incorrect.
		*/

		e->e_msgpriority = e->e_msgsize
				 - e->e_class * WkClassFact
				 + e->e_nrcpts * WkRecipFact;
		markstats(e, (ADDRESS *) NULL, STATS_NORMAL);
	}

  end:
	(void) set_tls_rd_tmo(old_rd_tmo);
}

/*
**  DFERROR -- signal error on writing the data file.
**
**	Called by collect().  Collect() always terminates the process
**	immediately after calling dferror(), which means that the SMTP
**	session will be terminated, which means that any error message
**	issued by dferror must be a 421 error, as per RFC 821.
**
**	Parameters:
**		df -- the file pointer for the data file.
**		msg -- detailed message.
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Gives an error message.
**		Arranges for following output to go elsewhere.
*/

void
dferror(df, msg, e)
	SM_FILE_T *volatile df;
	char *msg;
	register ENVELOPE *e;
{
	char *dfname;

	dfname = queuename(e, DATAFL_LETTER);
	setstat(EX_IOERR);
	if (errno == ENOSPC)
	{
#if STAT64 > 0
		struct stat64 st;
#else /* STAT64 > 0 */
		struct stat st;
#endif /* STAT64 > 0 */
		long avail;
		long bsize;

		e->e_flags |= EF_NO_BODY_RETN;

		if (
#if STAT64 > 0
		    fstat64(sm_io_getinfo(df, SM_IO_WHAT_FD, NULL), &st)
#else /* STAT64 > 0 */
		    fstat(sm_io_getinfo(df, SM_IO_WHAT_FD, NULL), &st)
#endif /* STAT64 > 0 */
		    < 0)
		  st.st_size = 0;
		(void) sm_io_reopen(SmFtStdio, SM_TIME_DEFAULT, dfname,
				    SM_IO_WRONLY_B, NULL, df);
		if (st.st_size <= 0)
			(void) sm_io_fprintf(df, SM_TIME_DEFAULT,
				"\n*** Mail could not be accepted");
		else
			(void) sm_io_fprintf(df, SM_TIME_DEFAULT,
				"\n*** Mail of at least %llu bytes could not be accepted\n",
				(ULONGLONG_T) st.st_size);
		(void) sm_io_fprintf(df, SM_TIME_DEFAULT,
			"*** at %s due to lack of disk space for temp file.\n",
			MyHostName);
		avail = freediskspace(qid_printqueue(e->e_qgrp, e->e_qdir),
				      &bsize);
		if (avail > 0)
		{
			if (bsize > 1024)
				avail *= bsize / 1024;
			else if (bsize < 1024)
				avail /= 1024 / bsize;
			(void) sm_io_fprintf(df, SM_TIME_DEFAULT,
				"*** Currently, %ld kilobytes are available for mail temp files.\n",
				avail);
		}
#if 0
		/* Wrong response code; should be 421. */
		e->e_status = "4.3.1";
		usrerrenh(e->e_status, "452 Out of disk space for temp file");
#else /* 0 */
		syserr("421 4.3.1 Out of disk space for temp file");
#endif /* 0 */
	}
	else
		syserr("421 4.3.0 collect: Cannot write %s (%s, uid=%ld, gid=%ld)",
			dfname, msg, (long) geteuid(), (long) getegid());
	if (sm_io_reopen(SmFtStdio, SM_TIME_DEFAULT, SM_PATH_DEVNULL,
			 SM_IO_WRONLY, NULL, df) == NULL)
		sm_syslog(LOG_ERR, e->e_id,
			  "dferror: sm_io_reopen(\"/dev/null\") failed: %s",
			  sm_errstring(errno));
}
/*
**  EATFROM -- chew up a UNIX style from line and process
**
**	This does indeed make some assumptions about the format
**	of UNIX messages.
**
**	Parameters:
**		fm -- the from line.
**		e -- envelope
**
**	Returns:
**		none.
**
**	Side Effects:
**		extracts what information it can from the header,
**		such as the date.
*/

#ifndef NOTUNIX

static char	*DowList[] =
{
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", NULL
};

static char	*MonthList[] =
{
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	NULL
};

static void
eatfrom(fm, e)
	char *volatile fm;
	register ENVELOPE *e;
{
	register char *p;
	register char **dt;

	if (tTd(30, 2))
		sm_dprintf("eatfrom(%s)\n", fm);

	/* find the date part */
	p = fm;
	while (*p != '\0')
	{
		/* skip a word */
		while (*p != '\0' && *p != ' ')
			p++;
		while (*p == ' ')
			p++;
		if (strlen(p) < 17)
		{
			/* no room for the date */
			return;
		}
		if (!(isascii(*p) && isupper(*p)) ||
		    p[3] != ' ' || p[13] != ':' || p[16] != ':')
			continue;

		/* we have a possible date */
		for (dt = DowList; *dt != NULL; dt++)
			if (strncmp(*dt, p, 3) == 0)
				break;
		if (*dt == NULL)
			continue;

		for (dt = MonthList; *dt != NULL; dt++)
		{
			if (strncmp(*dt, &p[4], 3) == 0)
				break;
		}
		if (*dt != NULL)
			break;
	}

	if (*p != '\0')
	{
		char *q, buf[25];

		/* we have found a date */
		(void) sm_strlcpy(buf, p, sizeof(buf));
		q = arpadate(buf);
		macdefine(&e->e_macro, A_TEMP, 'a', q);
	}
}
#endif /* ! NOTUNIX */
