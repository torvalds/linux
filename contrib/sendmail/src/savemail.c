/*
 * Copyright (c) 1998-2003, 2006, 2012, 2013 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: savemail.c,v 8.319 2013-11-22 20:51:56 ca Exp $")

static bool	errbody __P((MCI *, ENVELOPE *, char *));
static bool	pruneroute __P((char *));

/*
**  SAVEMAIL -- Save mail on error
**
**	If mailing back errors, mail it back to the originator
**	together with an error message; otherwise, just put it in
**	dead.letter in the user's home directory (if he exists on
**	this machine).
**
**	Parameters:
**		e -- the envelope containing the message in error.
**		sendbody -- if true, also send back the body of the
**			message; otherwise just send the header.
**
**	Returns:
**		true if savemail panic'ed, (i.e., the data file should
**		be preserved by dropenvelope())
**
**	Side Effects:
**		Saves the letter, by writing or mailing it back to the
**		sender, or by putting it in dead.letter in her home
**		directory.
*/

/* defines for state machine */
#define ESM_REPORT		0	/* report to sender's terminal */
#define ESM_MAIL		1	/* mail back to sender */
#define ESM_QUIET		2	/* mail has already been returned */
#define ESM_DEADLETTER		3	/* save in ~/dead.letter */
#define ESM_POSTMASTER		4	/* return to postmaster */
#define ESM_DEADLETTERDROP	5	/* save in DeadLetterDrop */
#define ESM_PANIC		6	/* call loseqfile() */
#define ESM_DONE		7	/* message is successfully delivered */

bool
savemail(e, sendbody)
	register ENVELOPE *e;
	bool sendbody;
{
	register SM_FILE_T *fp;
	bool panic = false;
	int state;
	auto ADDRESS *q = NULL;
	register char *p;
	MCI mcibuf;
	int flags;
	long sff;
	char buf[MAXLINE + 1];
	char dlbuf[MAXPATHLEN];
	SM_MBDB_T user;


	if (tTd(6, 1))
	{
		sm_dprintf("\nsavemail, errormode = %c, id = %s, ExitStat = %d\n  e_from=",
			e->e_errormode, e->e_id == NULL ? "NONE" : e->e_id,
			ExitStat);
		printaddr(sm_debug_file(), &e->e_from, false);
	}

	if (e->e_id == NULL)
	{
		/* can't return a message with no id */
		return panic;
	}

	/*
	**  In the unhappy event we don't know who to return the mail
	**  to, make someone up.
	*/

	if (e->e_from.q_paddr == NULL)
	{
		e->e_sender = "Postmaster";
		if (parseaddr(e->e_sender, &e->e_from,
			      RF_COPYPARSE|RF_SENDERADDR,
			      '\0', NULL, e, false) == NULL)
		{
			syserr("553 5.3.5 Cannot parse Postmaster!");
			finis(true, true, EX_SOFTWARE);
		}
	}
	e->e_to = NULL;

	/*
	**  Basic state machine.
	**
	**	This machine runs through the following states:
	**
	**	ESM_QUIET	Errors have already been printed iff the
	**			sender is local.
	**	ESM_REPORT	Report directly to the sender's terminal.
	**	ESM_MAIL	Mail response to the sender.
	**	ESM_DEADLETTER	Save response in ~/dead.letter.
	**	ESM_POSTMASTER	Mail response to the postmaster.
	**	ESM_DEADLETTERDROP
	**			If DeadLetterDrop set, save it there.
	**	ESM_PANIC	Save response anywhere possible.
	*/

	/* determine starting state */
	switch (e->e_errormode)
	{
	  case EM_WRITE:
		state = ESM_REPORT;
		break;

	  case EM_BERKNET:
	  case EM_MAIL:
		state = ESM_MAIL;
		break;

	  case EM_PRINT:
	  case '\0':
		state = ESM_QUIET;
		break;

	  case EM_QUIET:
		/* no need to return anything at all */
		return panic;

	  default:
		syserr("554 5.3.0 savemail: bogus errormode x%x",
		       e->e_errormode);
		state = ESM_MAIL;
		break;
	}

	/* if this is already an error response, send to postmaster */
	if (bitset(EF_RESPONSE, e->e_flags))
	{
		if (e->e_parent != NULL &&
		    bitset(EF_RESPONSE, e->e_parent->e_flags))
		{
			/* got an error sending a response -- can it */
			return panic;
		}
		state = ESM_POSTMASTER;
	}

	while (state != ESM_DONE)
	{
		if (tTd(6, 5))
			sm_dprintf("  state %d\n", state);

		switch (state)
		{
		  case ESM_QUIET:
			if (bitnset(M_LOCALMAILER, e->e_from.q_mailer->m_flags))
				state = ESM_DEADLETTER;
			else
				state = ESM_MAIL;
			break;

		  case ESM_REPORT:

			/*
			**  If the user is still logged in on the same terminal,
			**  then write the error messages back to hir (sic).
			*/

#if USE_TTYPATH
			p = ttypath();
#else /* USE_TTYPATH */
			p = NULL;
#endif /* USE_TTYPATH */

			if (p == NULL || sm_io_reopen(SmFtStdio,
						      SM_TIME_DEFAULT,
						      p, SM_IO_WRONLY, NULL,
						      smioout) == NULL)
			{
				state = ESM_MAIL;
				break;
			}

			expand("\201n", buf, sizeof(buf), e);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "\r\nMessage from %s...\r\n", buf);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Errors occurred while sending mail.\r\n");
			if (e->e_xfp != NULL)
			{
				(void) bfrewind(e->e_xfp);
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Transcript follows:\r\n");
				while (sm_io_fgets(e->e_xfp, SM_TIME_DEFAULT,
						   buf, sizeof(buf)) >= 0 &&
				       !sm_io_error(smioout))
					(void) sm_io_fputs(smioout,
							   SM_TIME_DEFAULT,
							   buf);
			}
			else
			{
				syserr("Cannot open %s",
				       queuename(e, XSCRPT_LETTER));
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Transcript of session is unavailable.\r\n");
			}
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Original message will be saved in dead.letter.\r\n");
			state = ESM_DEADLETTER;
			break;

		  case ESM_MAIL:
			/*
			**  If mailing back, do it.
			**	Throw away all further output.  Don't alias,
			**	since this could cause loops, e.g., if joe
			**	mails to joe@x, and for some reason the network
			**	for @x is down, then the response gets sent to
			**	joe@x, which gives a response, etc.  Also force
			**	the mail to be delivered even if a version of
			**	it has already been sent to the sender.
			**
			**  If this is a configuration or local software
			**	error, send to the local postmaster as well,
			**	since the originator can't do anything
			**	about it anyway.  Note that this is a full
			**	copy of the message (intentionally) so that
			**	the Postmaster can forward things along.
			*/

			if (ExitStat == EX_CONFIG || ExitStat == EX_SOFTWARE)
			{
				(void) sendtolist("postmaster", NULLADDR,
						  &e->e_errorqueue, 0, e);
			}
			if (!emptyaddr(&e->e_from))
			{
				char from[TOBUFSIZE];

				if (sm_strlcpy(from, e->e_from.q_paddr,
						sizeof(from)) >= sizeof(from))
				{
					state = ESM_POSTMASTER;
					break;
				}

				if (!DontPruneRoutes)
					(void) pruneroute(from);

				(void) sendtolist(from, NULLADDR,
						  &e->e_errorqueue, 0, e);
			}

			/*
			**  Deliver a non-delivery report to the
			**  Postmaster-designate (not necessarily
			**  Postmaster).  This does not include the
			**  body of the message, for privacy reasons.
			**  You really shouldn't need this.
			*/

			e->e_flags |= EF_PM_NOTIFY;

			/* check to see if there are any good addresses */
			for (q = e->e_errorqueue; q != NULL; q = q->q_next)
			{
				if (QS_IS_SENDABLE(q->q_state))
					break;
			}
			if (q == NULL)
			{
				/* this is an error-error */
				state = ESM_POSTMASTER;
				break;
			}
			if (returntosender(e->e_message, e->e_errorqueue,
					   sendbody ? RTSF_SEND_BODY
						    : RTSF_NO_BODY,
					   e) == 0)
			{
				state = ESM_DONE;
				break;
			}

			/* didn't work -- return to postmaster */
			state = ESM_POSTMASTER;
			break;

		  case ESM_POSTMASTER:
			/*
			**  Similar to previous case, but to system postmaster.
			*/

			q = NULL;
			expand(DoubleBounceAddr, buf, sizeof(buf), e);

			/*
			**  Just drop it on the floor if DoubleBounceAddr
			**  expands to an empty string.
			*/

			if (*buf == '\0')
			{
				state = ESM_DONE;
				break;
			}
			if (sendtolist(buf, NULLADDR, &q, 0, e) <= 0)
			{
				syserr("553 5.3.0 cannot parse %s!", buf);
				ExitStat = EX_SOFTWARE;
				state = ESM_DEADLETTERDROP;
				break;
			}
			flags = RTSF_PM_BOUNCE;
			if (sendbody)
				flags |= RTSF_SEND_BODY;
			if (returntosender(e->e_message, q, flags, e) == 0)
			{
				state = ESM_DONE;
				break;
			}

			/* didn't work -- last resort */
			state = ESM_DEADLETTERDROP;
			break;

		  case ESM_DEADLETTER:
			/*
			**  Save the message in dead.letter.
			**	If we weren't mailing back, and the user is
			**	local, we should save the message in
			**	~/dead.letter so that the poor person doesn't
			**	have to type it over again -- and we all know
			**	what poor typists UNIX users are.
			*/

			p = NULL;
			if (bitnset(M_HASPWENT, e->e_from.q_mailer->m_flags))
			{
				if (e->e_from.q_home != NULL)
					p = e->e_from.q_home;
				else if (sm_mbdb_lookup(e->e_from.q_user, &user)
					 == EX_OK &&
					 *user.mbdb_homedir != '\0')
					p = user.mbdb_homedir;
			}
			if (p == NULL || e->e_dfp == NULL)
			{
				/* no local directory or no data file */
				state = ESM_MAIL;
				break;
			}

			/* we have a home directory; write dead.letter */
			macdefine(&e->e_macro, A_TEMP, 'z', p);

			/* get the sender for the UnixFromLine */
			p = macvalue('g', e);
			macdefine(&e->e_macro, A_PERM, 'g', e->e_sender);

			expand("\201z/dead.letter", dlbuf, sizeof(dlbuf), e);
			sff = SFF_CREAT|SFF_REGONLY|SFF_RUNASREALUID;
			if (RealUid == 0)
				sff |= SFF_ROOTOK;
			e->e_to = dlbuf;
			if (writable(dlbuf, NULL, sff) &&
			    mailfile(dlbuf, FileMailer, NULL, sff, e) == EX_OK)
			{
				int oldverb = Verbose;

				if (OpMode != MD_DAEMON && OpMode != MD_SMTP)
					Verbose = 1;
				if (Verbose > 0)
					message("Saved message in %s", dlbuf);
				Verbose = oldverb;
				macdefine(&e->e_macro, A_PERM, 'g', p);
				state = ESM_DONE;
				break;
			}
			macdefine(&e->e_macro, A_PERM, 'g', p);
			state = ESM_MAIL;
			break;

		  case ESM_DEADLETTERDROP:
			/*
			**  Log the mail in DeadLetterDrop file.
			*/

			if (e->e_class < 0)
			{
				state = ESM_DONE;
				break;
			}

			if ((SafeFileEnv != NULL && SafeFileEnv[0] != '\0') ||
			    DeadLetterDrop == NULL ||
			    DeadLetterDrop[0] == '\0')
			{
				state = ESM_PANIC;
				break;
			}

			sff = SFF_CREAT|SFF_REGONLY|SFF_ROOTOK|SFF_OPENASROOT|SFF_MUSTOWN;
			if (!writable(DeadLetterDrop, NULL, sff) ||
			    (fp = safefopen(DeadLetterDrop, O_WRONLY|O_APPEND,
					    FileMode, sff)) == NULL)
			{
				state = ESM_PANIC;
				break;
			}

			memset(&mcibuf, '\0', sizeof(mcibuf));
			mcibuf.mci_out = fp;
			mcibuf.mci_mailer = FileMailer;
			if (bitnset(M_7BITS, FileMailer->m_flags))
				mcibuf.mci_flags |= MCIF_7BIT;

			/* get the sender for the UnixFromLine */
			p = macvalue('g', e);
			macdefine(&e->e_macro, A_PERM, 'g', e->e_sender);

			if (!putfromline(&mcibuf, e) ||
			    !(*e->e_puthdr)(&mcibuf, e->e_header, e,
					M87F_OUTER) ||
			    !(*e->e_putbody)(&mcibuf, e, NULL) ||
			    !putline("\n", &mcibuf) ||
			    sm_io_flush(fp, SM_TIME_DEFAULT) == SM_IO_EOF ||
			    sm_io_error(fp) ||
			    sm_io_close(fp, SM_TIME_DEFAULT) < 0)
				state = ESM_PANIC;
			else
			{
				int oldverb = Verbose;

				if (OpMode != MD_DAEMON && OpMode != MD_SMTP)
					Verbose = 1;
				if (Verbose > 0)
					message("Saved message in %s",
						DeadLetterDrop);
				Verbose = oldverb;
				if (LogLevel > 3)
					sm_syslog(LOG_NOTICE, e->e_id,
						  "Saved message in %s",
						  DeadLetterDrop);
				state = ESM_DONE;
			}
			macdefine(&e->e_macro, A_PERM, 'g', p);
			break;

		  default:
			syserr("554 5.3.5 savemail: unknown state %d", state);
			/* FALLTHROUGH */

		  case ESM_PANIC:
			/* leave the locked queue & transcript files around */
			loseqfile(e, "savemail panic");
			panic = true;
			errno = 0;
			syserr("554 savemail: cannot save rejected email anywhere");
			state = ESM_DONE;
			break;
		}
	}
	return panic;
}
/*
**  RETURNTOSENDER -- return a message to the sender with an error.
**
**	Parameters:
**		msg -- the explanatory message.
**		returnq -- the queue of people to send the message to.
**		flags -- flags tweaking the operation:
**			RTSF_SENDBODY -- include body of message (otherwise
**				just send the header).
**			RTSF_PMBOUNCE -- this is a postmaster bounce.
**		e -- the current envelope.
**
**	Returns:
**		zero -- if everything went ok.
**		else -- some error.
**
**	Side Effects:
**		Returns the current message to the sender via mail.
*/

#define MAXRETURNS	6	/* max depth of returning messages */
#define ERRORFUDGE	1024	/* nominal size of error message text */

int
returntosender(msg, returnq, flags, e)
	char *msg;
	ADDRESS *returnq;
	int flags;
	register ENVELOPE *e;
{
	int ret;
	register ENVELOPE *ee;
	ENVELOPE *oldcur = CurEnv;
	ENVELOPE errenvelope;
	static int returndepth = 0;
	register ADDRESS *q;
	char *p;
	char buf[MAXNAME + 1];

	if (returnq == NULL)
		return -1;

	if (msg == NULL)
		msg = "Unable to deliver mail";

	if (tTd(6, 1))
	{
		sm_dprintf("\n*** Return To Sender: msg=\"%s\", depth=%d, e=%p, returnq=",
			msg, returndepth, e);
		printaddr(sm_debug_file(), returnq, true);
		if (tTd(6, 20))
		{
			sm_dprintf("Sendq=");
			printaddr(sm_debug_file(), e->e_sendqueue, true);
		}
	}

	if (++returndepth >= MAXRETURNS)
	{
		if (returndepth != MAXRETURNS)
			syserr("554 5.3.0 returntosender: infinite recursion on %s",
			       returnq->q_paddr);
		/* don't "unrecurse" and fake a clean exit */
		/* returndepth--; */
		return 0;
	}

	macdefine(&e->e_macro, A_PERM, 'g', e->e_sender);
	macdefine(&e->e_macro, A_PERM, 'u', NULL);

	/* initialize error envelope */
	ee = newenvelope(&errenvelope, e, sm_rpool_new_x(NULL));
	macdefine(&ee->e_macro, A_PERM, 'a', "\201b");
	macdefine(&ee->e_macro, A_PERM, 'r', "");
	macdefine(&ee->e_macro, A_PERM, 's', "localhost");
	macdefine(&ee->e_macro, A_PERM, '_', "localhost");
	clrsessenvelope(ee);

	ee->e_puthdr = putheader;
	ee->e_putbody = errbody;
	ee->e_flags |= EF_RESPONSE|EF_METOO;
	if (!bitset(EF_OLDSTYLE, e->e_flags))
		ee->e_flags &= ~EF_OLDSTYLE;
	if (bitset(EF_DONT_MIME, e->e_flags))
	{
		ee->e_flags |= EF_DONT_MIME;

		/*
		**  If we can't convert to MIME and we don't pass
		**  8-bit, we can't send the body.
		*/

		if (bitset(EF_HAS8BIT, e->e_flags) &&
		    !bitset(MM_PASS8BIT, MimeMode))
			flags &= ~RTSF_SEND_BODY;
	}

	ee->e_sendqueue = returnq;
	ee->e_msgsize = 0;
	if (bitset(RTSF_SEND_BODY, flags) &&
	    !bitset(PRIV_NOBODYRETN, PrivacyFlags))
		ee->e_msgsize = ERRORFUDGE + e->e_msgsize;
	else
		ee->e_flags |= EF_NO_BODY_RETN;

#if _FFR_BOUNCE_QUEUE
	if (BounceQueue != NOQGRP)
		ee->e_qgrp = ee->e_dfqgrp = BounceQueue;
#endif /* _FFR_BOUNCE_QUEUE */
	if (!setnewqueue(ee))
	{
		syserr("554 5.3.0 returntosender: cannot select queue for %s",
			       returnq->q_paddr);
		ExitStat = EX_UNAVAILABLE;
		returndepth--;
		return -1;
	}
	initsys(ee);

#if NAMED_BIND
	_res.retry = TimeOuts.res_retry[RES_TO_FIRST];
	_res.retrans = TimeOuts.res_retrans[RES_TO_FIRST];
#endif /* NAMED_BIND */
	for (q = returnq; q != NULL; q = q->q_next)
	{
		if (QS_IS_BADADDR(q->q_state))
			continue;

		q->q_flags &= ~(QHASNOTIFY|Q_PINGFLAGS);
		q->q_flags |= QPINGONFAILURE;

		if (!QS_IS_DEAD(q->q_state))
			ee->e_nrcpts++;

		if (q->q_alias == NULL)
			addheader("To", q->q_paddr, 0, ee, true);
	}

	if (LogLevel > 5)
	{
		if (bitset(EF_RESPONSE, e->e_flags))
			p = "return to sender";
		else if (bitset(EF_WARNING, e->e_flags))
			p = "sender notify";
		else if (bitset(RTSF_PM_BOUNCE, flags))
			p = "postmaster notify";
		else
			p = "DSN";
		sm_syslog(LOG_INFO, e->e_id, "%s: %s: %s",
			  ee->e_id, p, shortenstring(msg, MAXSHORTSTR));
	}

	if (SendMIMEErrors)
	{
		addheader("MIME-Version", "1.0", 0, ee, true);
		(void) sm_snprintf(buf, sizeof(buf), "%s.%ld/%.100s",
				ee->e_id, (long)curtime(), MyHostName);
		ee->e_msgboundary = sm_rpool_strdup_x(ee->e_rpool, buf);
		(void) sm_snprintf(buf, sizeof(buf),
#if DSN
				"multipart/report; report-type=delivery-status;\n\tboundary=\"%s\"",
#else /* DSN */
				"multipart/mixed; boundary=\"%s\"",
#endif /* DSN */
				ee->e_msgboundary);
		addheader("Content-Type", buf, 0, ee, true);

		p = hvalue("Content-Transfer-Encoding", e->e_header);
		if (p != NULL && sm_strcasecmp(p, "binary") != 0)
			p = NULL;
		if (p == NULL && bitset(EF_HAS8BIT, e->e_flags))
			p = "8bit";
		if (p != NULL)
			addheader("Content-Transfer-Encoding", p, 0, ee, true);
	}
	if (strncmp(msg, "Warning:", 8) == 0)
	{
		addheader("Subject", msg, 0, ee, true);
		p = "warning-timeout";
	}
	else if (strncmp(msg, "Postmaster warning:", 19) == 0)
	{
		addheader("Subject", msg, 0, ee, true);
		p = "postmaster-warning";
	}
	else if (strcmp(msg, "Return receipt") == 0)
	{
		addheader("Subject", msg, 0, ee, true);
		p = "return-receipt";
	}
	else if (bitset(RTSF_PM_BOUNCE, flags))
	{
		(void) sm_snprintf(buf, sizeof(buf),
			 "Postmaster notify: see transcript for details");
		addheader("Subject", buf, 0, ee, true);
		p = "postmaster-notification";
	}
	else
	{
		(void) sm_snprintf(buf, sizeof(buf),
			 "Returned mail: see transcript for details");
		addheader("Subject", buf, 0, ee, true);
		p = "failure";
	}
	(void) sm_snprintf(buf, sizeof(buf), "auto-generated (%s)", p);
	addheader("Auto-Submitted", buf, 0, ee, true);

	/* fake up an address header for the from person */
	expand("\201n", buf, sizeof(buf), e);
	if (parseaddr(buf, &ee->e_from,
		      RF_COPYALL|RF_SENDERADDR, '\0', NULL, e, false) == NULL)
	{
		syserr("553 5.3.5 Can't parse myself!");
		ExitStat = EX_SOFTWARE;
		returndepth--;
		return -1;
	}
	ee->e_from.q_flags &= ~(QHASNOTIFY|Q_PINGFLAGS);
	ee->e_from.q_flags |= QPINGONFAILURE;
	ee->e_sender = ee->e_from.q_paddr;

	/* push state into submessage */
	CurEnv = ee;
	macdefine(&ee->e_macro, A_PERM, 'f', "\201n");
	macdefine(&ee->e_macro, A_PERM, 'x', "Mail Delivery Subsystem");
	eatheader(ee, true, true);

	/* mark statistics */
	markstats(ee, NULLADDR, STATS_NORMAL);

#if _FFR_BOUNCE_QUEUE
	if (BounceQueue == NOQGRP)
	{
#endif
		/* actually deliver the error message */
		sendall(ee, SM_DELIVER);
#if _FFR_BOUNCE_QUEUE
	}
#endif
	(void) dropenvelope(ee, true, false);

	/* check for delivery errors */
	ret = -1;
	if (ee->e_parent == NULL ||
	    !bitset(EF_RESPONSE, ee->e_parent->e_flags))
	{
		ret = 0;
	}
	else
	{
		for (q = ee->e_sendqueue; q != NULL; q = q->q_next)
		{
			if (QS_IS_ATTEMPTED(q->q_state))
			{
				ret = 0;
				break;
			}
		}
	}

	/* restore state */
	sm_rpool_free(ee->e_rpool);
	CurEnv = oldcur;
	returndepth--;

	return ret;
}

/*
**  ERRBODY -- output the body of an error message.
**
**	Typically this is a copy of the transcript plus a copy of the
**	original offending message.
**
**	Parameters:
**		mci -- the mailer connection information.
**		e -- the envelope we are working in.
**		separator -- any possible MIME separator (unused).
**
**	Returns:
**		true iff body was written successfully
**
**	Side Effects:
**		Outputs the body of an error message.
*/

/* ARGSUSED2 */
static bool
errbody(mci, e, separator)
	register MCI *mci;
	register ENVELOPE *e;
	char *separator;
{
	bool printheader;
	bool sendbody;
	bool pm_notify;
	int save_errno;
	register SM_FILE_T *xfile;
	char *p;
	register ADDRESS *q = NULL;
	char actual[MAXLINE];
	char buf[MAXLINE];

	if (bitset(MCIF_INHEADER, mci->mci_flags))
	{
		if (!putline("", mci))
			goto writeerr;
		mci->mci_flags &= ~MCIF_INHEADER;
	}
	if (e->e_parent == NULL)
	{
		syserr("errbody: null parent");
		if (!putline("   ----- Original message lost -----\n", mci))
			goto writeerr;
		return true;
	}

	/*
	**  Output MIME header.
	*/

	if (e->e_msgboundary != NULL)
	{
		(void) sm_strlcpyn(buf, sizeof(buf), 2, "--", e->e_msgboundary);
		if (!putline("This is a MIME-encapsulated message", mci) ||
		    !putline("", mci) ||
		    !putline(buf, mci) ||
		    !putline("", mci))
			goto writeerr;
	}

	/*
	**  Output introductory information.
	*/

	pm_notify = false;
	p = hvalue("subject", e->e_header);
	if (p != NULL && strncmp(p, "Postmaster ", 11) == 0)
		pm_notify = true;
	else
	{
		for (q = e->e_parent->e_sendqueue; q != NULL; q = q->q_next)
		{
			if (QS_IS_BADADDR(q->q_state))
				break;
		}
	}
	if (!pm_notify && q == NULL &&
	    !bitset(EF_FATALERRS|EF_SENDRECEIPT, e->e_parent->e_flags))
	{
		if (!putline("    **********************************************",
			mci) ||
		    !putline("    **      THIS IS A WARNING MESSAGE ONLY      **",
			mci) ||
		    !putline("    **  YOU DO NOT NEED TO RESEND YOUR MESSAGE  **",
			mci) ||
		    !putline("    **********************************************",
			mci) ||
		    !putline("", mci))
			goto writeerr;
	}
	(void) sm_snprintf(buf, sizeof(buf),
		"The original message was received at %s",
		arpadate(ctime(&e->e_parent->e_ctime)));
	if (!putline(buf, mci))
		goto writeerr;
	expand("from \201_", buf, sizeof(buf), e->e_parent);
	if (!putline(buf, mci))
		goto writeerr;

	/* include id in postmaster copies */
	if (pm_notify && e->e_parent->e_id != NULL)
	{
		(void) sm_strlcpyn(buf, sizeof(buf), 2, "with id ",
			e->e_parent->e_id);
		if (!putline(buf, mci))
			goto writeerr;
	}
	if (!putline("", mci))
		goto writeerr;

	/*
	**  Output error message header (if specified and available).
	*/

	if (ErrMsgFile != NULL &&
	    !bitset(EF_SENDRECEIPT, e->e_parent->e_flags))
	{
		if (*ErrMsgFile == '/')
		{
			long sff = SFF_ROOTOK|SFF_REGONLY;

			if (DontLockReadFiles)
				sff |= SFF_NOLOCK;
			if (!bitnset(DBS_ERRORHEADERINUNSAFEDIRPATH,
				     DontBlameSendmail))
				sff |= SFF_SAFEDIRPATH;
			xfile = safefopen(ErrMsgFile, O_RDONLY, 0444, sff);
			if (xfile != NULL)
			{
				while (sm_io_fgets(xfile, SM_TIME_DEFAULT, buf,
						   sizeof(buf)) >= 0)
				{
					int lbs;
					bool putok;
					char *lbp;

					lbs = sizeof(buf);
					lbp = translate_dollars(buf, buf, &lbs);
					expand(lbp, lbp, lbs, e);
					putok = putline(lbp, mci);
					if (lbp != buf)
						sm_free(lbp);
					if (!putok)
						goto writeerr;
				}
				(void) sm_io_close(xfile, SM_TIME_DEFAULT);
				if (!putline("\n", mci))
					goto writeerr;
			}
		}
		else
		{
			expand(ErrMsgFile, buf, sizeof(buf), e);
			if (!putline(buf, mci) || !putline("", mci))
				goto writeerr;
		}
	}

	/*
	**  Output message introduction
	*/

	/* permanent fatal errors */
	printheader = true;
	for (q = e->e_parent->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (!QS_IS_BADADDR(q->q_state) ||
		    !bitset(QPINGONFAILURE, q->q_flags))
			continue;

		if (printheader)
		{
			if (!putline("   ----- The following addresses had permanent fatal errors -----",
					mci))
				goto writeerr;
			printheader = false;
		}

		(void) sm_strlcpy(buf, shortenstring(q->q_paddr, MAXSHORTSTR),
				  sizeof(buf));
		if (!putline(buf, mci))
			goto writeerr;
		if (q->q_rstatus != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
				"    (reason: %s)",
				shortenstring(exitstat(q->q_rstatus),
					      MAXSHORTSTR));
			if (!putline(buf, mci))
				goto writeerr;
		}
		if (q->q_alias != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
				"    (expanded from: %s)",
				shortenstring(q->q_alias->q_paddr,
					      MAXSHORTSTR));
			if (!putline(buf, mci))
				goto writeerr;
		}
	}
	if (!printheader && !putline("", mci))
		goto writeerr;

	/* transient non-fatal errors */
	printheader = true;
	for (q = e->e_parent->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_BADADDR(q->q_state) ||
		    !bitset(QPRIMARY, q->q_flags) ||
		    !bitset(QBYNDELAY, q->q_flags) ||
		    !bitset(QDELAYED, q->q_flags))
			continue;

		if (printheader)
		{
			if (!putline("   ----- The following addresses had transient non-fatal errors -----",
					mci))
				goto writeerr;
			printheader = false;
		}

		(void) sm_strlcpy(buf, shortenstring(q->q_paddr, MAXSHORTSTR),
				  sizeof(buf));
		if (!putline(buf, mci))
			goto writeerr;
		if (q->q_alias != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
				"    (expanded from: %s)",
				shortenstring(q->q_alias->q_paddr,
					      MAXSHORTSTR));
			if (!putline(buf, mci))
				goto writeerr;
		}
	}
	if (!printheader && !putline("", mci))
		goto writeerr;

	/* successful delivery notifications */
	printheader = true;
	for (q = e->e_parent->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_BADADDR(q->q_state) ||
		    !bitset(QPRIMARY, q->q_flags) ||
		    bitset(QBYNDELAY, q->q_flags) ||
		    bitset(QDELAYED, q->q_flags))
			continue;
		else if (bitset(QBYNRELAY, q->q_flags))
			p = "Deliver-By notify: relayed";
		else if (bitset(QBYTRACE, q->q_flags))
			p = "Deliver-By trace: relayed";
		else if (!bitset(QPINGONSUCCESS, q->q_flags))
			continue;
		else if (bitset(QRELAYED, q->q_flags))
			p = "relayed to non-DSN-aware mailer";
		else if (bitset(QDELIVERED, q->q_flags))
		{
			if (bitset(QEXPANDED, q->q_flags))
				p = "successfully delivered to mailing list";
			else
				p = "successfully delivered to mailbox";
		}
		else if (bitset(QEXPANDED, q->q_flags))
			p = "expanded by alias";
		else
			continue;

		if (printheader)
		{
			if (!putline("   ----- The following addresses had successful delivery notifications -----",
					mci))
				goto writeerr;
			printheader = false;
		}

		(void) sm_snprintf(buf, sizeof(buf), "%s  (%s)",
			 shortenstring(q->q_paddr, MAXSHORTSTR), p);
		if (!putline(buf, mci))
			goto writeerr;
		if (q->q_alias != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
				"    (expanded from: %s)",
				shortenstring(q->q_alias->q_paddr,
					      MAXSHORTSTR));
			if (!putline(buf, mci))
				goto writeerr;
		}
	}
	if (!printheader && !putline("", mci))
		goto writeerr;

	/*
	**  Output transcript of errors
	*/

	(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
	if (e->e_parent->e_xfp == NULL)
	{
		if (!putline("   ----- Transcript of session is unavailable -----\n",
				mci))
			goto writeerr;
	}
	else
	{
		int blen;

		printheader = true;
		(void) bfrewind(e->e_parent->e_xfp);
		if (e->e_xfp != NULL)
			(void) sm_io_flush(e->e_xfp, SM_TIME_DEFAULT);
		while ((blen = sm_io_fgets(e->e_parent->e_xfp, SM_TIME_DEFAULT,
					buf, sizeof(buf))) >= 0)
		{
			if (printheader && !putline("   ----- Transcript of session follows -----\n",
						mci))
				goto writeerr;
			printheader = false;
			if (!putxline(buf, blen, mci, PXLF_MAPFROM))
				goto writeerr;
		}
	}
	errno = 0;

#if DSN
	/*
	**  Output machine-readable version.
	*/

	if (e->e_msgboundary != NULL)
	{
		(void) sm_strlcpyn(buf, sizeof(buf), 2, "--", e->e_msgboundary);
		if (!putline("", mci) ||
		    !putline(buf, mci) ||
		    !putline("Content-Type: message/delivery-status", mci) ||
		    !putline("", mci))
			goto writeerr;

		/*
		**  Output per-message information.
		*/

		/* original envelope id from MAIL FROM: line */
		if (e->e_parent->e_envid != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
					"Original-Envelope-Id: %.800s",
					xuntextify(e->e_parent->e_envid));
			if (!putline(buf, mci))
				goto writeerr;
		}

		/* Reporting-MTA: is us (required) */
		(void) sm_snprintf(buf, sizeof(buf),
				   "Reporting-MTA: dns; %.800s", MyHostName);
		if (!putline(buf, mci))
			goto writeerr;

		/* DSN-Gateway: not relevant since we are not translating */

		/* Received-From-MTA: shows where we got this message from */
		if (RealHostName != NULL)
		{
			/* XXX use $s for type? */
			if (e->e_parent->e_from.q_mailer == NULL ||
			    (p = e->e_parent->e_from.q_mailer->m_mtatype) == NULL)
				p = "dns";
			(void) sm_snprintf(buf, sizeof(buf),
					"Received-From-MTA: %s; %.800s",
					p, RealHostName);
			if (!putline(buf, mci))
				goto writeerr;
		}

		/* Arrival-Date: -- when it arrived here */
		(void) sm_strlcpyn(buf, sizeof(buf), 2, "Arrival-Date: ",
				arpadate(ctime(&e->e_parent->e_ctime)));
		if (!putline(buf, mci))
			goto writeerr;

		/* Deliver-By-Date: -- when it should have been delivered */
		if (IS_DLVR_BY(e->e_parent))
		{
			time_t dbyd;

			dbyd = e->e_parent->e_ctime + e->e_parent->e_deliver_by;
			(void) sm_strlcpyn(buf, sizeof(buf), 2,
					"Deliver-By-Date: ",
					arpadate(ctime(&dbyd)));
			if (!putline(buf, mci))
				goto writeerr;
		}

		/*
		**  Output per-address information.
		*/

		for (q = e->e_parent->e_sendqueue; q != NULL; q = q->q_next)
		{
			char *action;

			if (QS_IS_BADADDR(q->q_state))
			{
				/* RFC 1891, 6.2.6 (b) */
				if (bitset(QHASNOTIFY, q->q_flags) &&
				    !bitset(QPINGONFAILURE, q->q_flags))
					continue;
				action = "failed";
			}
			else if (!bitset(QPRIMARY, q->q_flags))
				continue;
			else if (bitset(QDELIVERED, q->q_flags))
			{
				if (bitset(QEXPANDED, q->q_flags))
					action = "delivered (to mailing list)";
				else
					action = "delivered (to mailbox)";
			}
			else if (bitset(QRELAYED, q->q_flags))
				action = "relayed (to non-DSN-aware mailer)";
			else if (bitset(QEXPANDED, q->q_flags))
				action = "expanded (to multi-recipient alias)";
			else if (bitset(QDELAYED, q->q_flags))
				action = "delayed";
			else if (bitset(QBYTRACE, q->q_flags))
				action = "relayed (Deliver-By trace mode)";
			else if (bitset(QBYNDELAY, q->q_flags))
				action = "delayed (Deliver-By notify mode)";
			else if (bitset(QBYNRELAY, q->q_flags))
				action = "relayed (Deliver-By notify mode)";
			else
				continue;

			if (!putline("", mci))
				goto writeerr;

			/* Original-Recipient: -- passed from on high */
			if (q->q_orcpt != NULL)
			{
				p = strchr(q->q_orcpt, ';');

				/*
				**  p == NULL shouldn't happen due to
				**  check in srvrsmtp.c
				**  we could log an error in this case.
				*/

				if (p != NULL)
				{
					*p = '\0';
					(void) sm_snprintf(buf, sizeof(buf),
						"Original-Recipient: %.100s;%.700s",
						q->q_orcpt, xuntextify(p + 1));
					*p = ';';
					if (!putline(buf, mci))
						goto writeerr;
				}
			}

			/* Figure out actual recipient */
			actual[0] = '\0';
			if (q->q_user[0] != '\0')
			{
				if (q->q_mailer != NULL &&
				    q->q_mailer->m_addrtype != NULL)
					p = q->q_mailer->m_addrtype;
				else
					p = "rfc822";

				if (sm_strcasecmp(p, "rfc822") == 0 &&
				    strchr(q->q_user, '@') == NULL)
				{
					(void) sm_snprintf(actual,
							   sizeof(actual),
							   "%s; %.700s@%.100s",
							   p, q->q_user,
							   MyHostName);
				}
				else
				{
					(void) sm_snprintf(actual,
							   sizeof(actual),
							   "%s; %.800s",
							   p, q->q_user);
				}
			}

			/* Final-Recipient: -- the name from the RCPT command */
			if (q->q_finalrcpt == NULL)
			{
				/* should never happen */
				sm_syslog(LOG_ERR, e->e_id,
					  "returntosender: q_finalrcpt is NULL");

				/* try to fall back to the actual recipient */
				if (actual[0] != '\0')
					q->q_finalrcpt = sm_rpool_strdup_x(e->e_rpool,
									   actual);
			}

			if (q->q_finalrcpt != NULL)
			{
				(void) sm_snprintf(buf, sizeof(buf),
						   "Final-Recipient: %s",
						   q->q_finalrcpt);
				if (!putline(buf, mci))
					goto writeerr;
			}

			/* X-Actual-Recipient: -- the real problem address */
			if (actual[0] != '\0' &&
			    q->q_finalrcpt != NULL &&
			    !bitset(PRIV_NOACTUALRECIPIENT, PrivacyFlags) &&
			    strcmp(actual, q->q_finalrcpt) != 0)
			{
				(void) sm_snprintf(buf, sizeof(buf),
						   "X-Actual-Recipient: %s",
						   actual);
				if (!putline(buf, mci))
					goto writeerr;
			}

			/* Action: -- what happened? */
			(void) sm_strlcpyn(buf, sizeof(buf), 2, "Action: ",
				action);
			if (!putline(buf, mci))
				goto writeerr;

			/* Status: -- what _really_ happened? */
			if (q->q_status != NULL)
				p = q->q_status;
			else if (QS_IS_BADADDR(q->q_state))
				p = "5.0.0";
			else if (QS_IS_QUEUEUP(q->q_state))
				p = "4.0.0";
			else
				p = "2.0.0";
			(void) sm_strlcpyn(buf, sizeof(buf), 2, "Status: ", p);
			if (!putline(buf, mci))
				goto writeerr;

			/* Remote-MTA: -- who was I talking to? */
			if (q->q_statmta != NULL)
			{
				if (q->q_mailer == NULL ||
				    (p = q->q_mailer->m_mtatype) == NULL)
					p = "dns";
				(void) sm_snprintf(buf, sizeof(buf),
						"Remote-MTA: %s; %.800s",
						p, q->q_statmta);
				p = &buf[strlen(buf) - 1];
				if (*p == '.')
					*p = '\0';
				if (!putline(buf, mci))
					goto writeerr;
			}

			/* Diagnostic-Code: -- actual result from other end */
			if (q->q_rstatus != NULL)
			{
				if (q->q_mailer == NULL ||
				    (p = q->q_mailer->m_diagtype) == NULL)
					p = "smtp";
				(void) sm_snprintf(buf, sizeof(buf),
						"Diagnostic-Code: %s; %.800s",
						p, q->q_rstatus);
				if (!putline(buf, mci))
					goto writeerr;
			}

			/* Last-Attempt-Date: -- fine granularity */
			if (q->q_statdate == (time_t) 0L)
				q->q_statdate = curtime();
			(void) sm_strlcpyn(buf, sizeof(buf), 2,
					"Last-Attempt-Date: ",
					arpadate(ctime(&q->q_statdate)));
			if (!putline(buf, mci))
				goto writeerr;

			/* Will-Retry-Until: -- for delayed messages only */
			if (QS_IS_QUEUEUP(q->q_state))
			{
				time_t xdate;

				xdate = e->e_parent->e_ctime +
					TimeOuts.to_q_return[e->e_parent->e_timeoutclass];
				(void) sm_strlcpyn(buf, sizeof(buf), 2,
					 "Will-Retry-Until: ",
					 arpadate(ctime(&xdate)));
				if (!putline(buf, mci))
					goto writeerr;
			}
		}
	}
#endif /* DSN */

	/*
	**  Output text of original message
	*/

	if (!putline("", mci))
		goto writeerr;
	if (bitset(EF_HAS_DF, e->e_parent->e_flags))
	{
		sendbody = !bitset(EF_NO_BODY_RETN, e->e_parent->e_flags) &&
			   !bitset(EF_NO_BODY_RETN, e->e_flags);

		if (e->e_msgboundary == NULL)
		{
			if (!putline(
				sendbody
				? "   ----- Original message follows -----\n"
				: "   ----- Message header follows -----\n",
				mci))
			{
				goto writeerr;
			}
		}
		else
		{
			(void) sm_strlcpyn(buf, sizeof(buf), 2, "--",
					e->e_msgboundary);

			if (!putline(buf, mci))
				goto writeerr;
			(void) sm_strlcpyn(buf, sizeof(buf), 2, "Content-Type: ",
					sendbody ? "message/rfc822"
						 : "text/rfc822-headers");
			if (!putline(buf, mci))
				goto writeerr;

			p = hvalue("Content-Transfer-Encoding",
				   e->e_parent->e_header);
			if (p != NULL && sm_strcasecmp(p, "binary") != 0)
				p = NULL;
			if (p == NULL &&
			    bitset(EF_HAS8BIT, e->e_parent->e_flags))
				p = "8bit";
			if (p != NULL)
			{
				(void) sm_snprintf(buf, sizeof(buf),
						"Content-Transfer-Encoding: %s",
						p);
				if (!putline(buf, mci))
					goto writeerr;
			}
		}
		if (!putline("", mci))
			goto writeerr;
		save_errno = errno;
		if (!putheader(mci, e->e_parent->e_header, e->e_parent,
				M87F_OUTER))
			goto writeerr;
		errno = save_errno;
		if (sendbody)
		{
			if (!putbody(mci, e->e_parent, e->e_msgboundary))
				goto writeerr;
		}
		else if (e->e_msgboundary == NULL)
		{
			if (!putline("", mci) ||
			    !putline("   ----- Message body suppressed -----",
					mci))
			{
				goto writeerr;
			}
		}
	}
	else if (e->e_msgboundary == NULL)
	{
		if (!putline("  ----- No message was collected -----\n", mci))
			goto writeerr;
	}

	if (e->e_msgboundary != NULL)
	{
		(void) sm_strlcpyn(buf, sizeof(buf), 3, "--", e->e_msgboundary,
				   "--");
		if (!putline("", mci) || !putline(buf, mci))
			goto writeerr;
	}
	if (!putline("", mci) ||
	    sm_io_flush(mci->mci_out, SM_TIME_DEFAULT) == SM_IO_EOF)
			goto writeerr;

	/*
	**  Cleanup and exit
	*/

	if (errno != 0)
	{
  writeerr:
		syserr("errbody: I/O error");
		return false;
	}
	return true;
}

/*
**  SMTPTODSN -- convert SMTP to DSN status code
**
**	Parameters:
**		smtpstat -- the smtp status code (e.g., 550).
**
**	Returns:
**		The DSN version of the status code.
**
**	Storage Management:
**		smtptodsn() returns a pointer to a character string literal,
**		which will remain valid forever, and thus does not need to
**		be copied.  Current code relies on this property.
*/

char *
smtptodsn(smtpstat)
	int smtpstat;
{
	if (smtpstat < 0)
		return "4.4.2";

	switch (smtpstat)
	{
	  case 450:	/* Req mail action not taken: mailbox unavailable */
		return "4.2.0";

	  case 451:	/* Req action aborted: local error in processing */
		return "4.3.0";

	  case 452:	/* Req action not taken: insufficient sys storage */
		return "4.3.1";

	  case 500:	/* Syntax error, command unrecognized */
		return "5.5.2";

	  case 501:	/* Syntax error in parameters or arguments */
		return "5.5.4";

	  case 502:	/* Command not implemented */
		return "5.5.1";

	  case 503:	/* Bad sequence of commands */
		return "5.5.1";

	  case 504:	/* Command parameter not implemented */
		return "5.5.4";

	  case 550:	/* Req mail action not taken: mailbox unavailable */
		return "5.2.0";

	  case 551:	/* User not local; please try <...> */
		return "5.1.6";

	  case 552:	/* Req mail action aborted: exceeded storage alloc */
		return "5.2.2";

	  case 553:	/* Req action not taken: mailbox name not allowed */
		return "5.1.0";

	  case 554:	/* Transaction failed */
		return "5.0.0";
	}

	if (REPLYTYPE(smtpstat) == 2)
		return "2.0.0";
	if (REPLYTYPE(smtpstat) == 4)
		return "4.0.0";
	return "5.0.0";
}
/*
**  XTEXTIFY -- take regular text and turn it into DSN-style xtext
**
**	Parameters:
**		t -- the text to convert.
**		taboo -- additional characters that must be encoded.
**
**	Returns:
**		The xtext-ified version of the same string.
*/

char *
xtextify(t, taboo)
	register char *t;
	char *taboo;
{
	register char *p;
	int l;
	int nbogus;
	static char *bp = NULL;
	static int bplen = 0;

	if (taboo == NULL)
		taboo = "";

	/* figure out how long this xtext will have to be */
	nbogus = l = 0;
	for (p = t; *p != '\0'; p++)
	{
		register int c = (*p & 0xff);

		/* ASCII dependence here -- this is the way the spec words it */
		if (c < '!' || c > '~' || c == '+' || c == '\\' || c == '(' ||
		    strchr(taboo, c) != NULL)
			nbogus++;
		l++;
	}
	if (nbogus < 0)
	{
		/* since nbogus is ssize_t and wrapped, 2 * size_t would wrap */
		syserr("!xtextify string too long");
	}
	if (nbogus == 0)
		return t;
	l += nbogus * 2 + 1;

	/* now allocate space if necessary for the new string */
	if (l > bplen)
	{
		if (bp != NULL)
			sm_free(bp); /* XXX */
		bp = sm_pmalloc_x(l);
		bplen = l;
	}

	/* ok, copy the text with byte expansion */
	for (p = bp; *t != '\0'; )
	{
		register int c = (*t++ & 0xff);

		/* ASCII dependence here -- this is the way the spec words it */
		if (c < '!' || c > '~' || c == '+' || c == '\\' || c == '(' ||
		    strchr(taboo, c) != NULL)
		{
			*p++ = '+';
			*p++ = "0123456789ABCDEF"[c >> 4];
			*p++ = "0123456789ABCDEF"[c & 0xf];
		}
		else
			*p++ = c;
	}
	*p = '\0';
	return bp;
}
/*
**  XUNTEXTIFY -- take xtext and turn it into plain text
**
**	Parameters:
**		t -- the xtextified text.
**
**	Returns:
**		The decoded text.  No attempt is made to deal with
**		null strings in the resulting text.
*/

char *
xuntextify(t)
	register char *t;
{
	register char *p;
	int l;
	static char *bp = NULL;
	static int bplen = 0;

	/* heuristic -- if no plus sign, just return the input */
	if (strchr(t, '+') == NULL)
		return t;

	/* xtext is always longer than decoded text */
	l = strlen(t);
	if (l > bplen)
	{
		if (bp != NULL)
			sm_free(bp); /* XXX */
		bp = xalloc(l);
		bplen = l;
	}

	/* ok, copy the text with byte compression */
	for (p = bp; *t != '\0'; t++)
	{
		register int c = *t & 0xff;

		if (c != '+')
		{
			*p++ = c;
			continue;
		}

		c = *++t & 0xff;
		if (!isascii(c) || !isxdigit(c))
		{
			/* error -- first digit is not hex */
			usrerr("bogus xtext: +%c", c);
			t--;
			continue;
		}
		if (isdigit(c))
			c -= '0';
		else if (isupper(c))
			c -= 'A' - 10;
		else
			c -= 'a' - 10;
		*p = c << 4;

		c = *++t & 0xff;
		if (!isascii(c) || !isxdigit(c))
		{
			/* error -- second digit is not hex */
			usrerr("bogus xtext: +%x%c", *p >> 4, c);
			t--;
			continue;
		}
		if (isdigit(c))
			c -= '0';
		else if (isupper(c))
			c -= 'A' - 10;
		else
			c -= 'a' - 10;
		*p++ |= c;
	}
	*p = '\0';
	return bp;
}
/*
**  XTEXTOK -- check if a string is legal xtext
**
**	Xtext is used in Delivery Status Notifications.  The spec was
**	taken from RFC 1891, ``SMTP Service Extension for Delivery
**	Status Notifications''.
**
**	Parameters:
**		s -- the string to check.
**
**	Returns:
**		true -- if 's' is legal xtext.
**		false -- if it has any illegal characters in it.
*/

bool
xtextok(s)
	char *s;
{
	int c;

	while ((c = *s++) != '\0')
	{
		if (c == '+')
		{
			c = *s++;
			if (!isascii(c) || !isxdigit(c))
				return false;
			c = *s++;
			if (!isascii(c) || !isxdigit(c))
				return false;
		}
		else if (c < '!' || c > '~' || c == '=')
			return false;
	}
	return true;
}

/*
**  ISATOM -- check if a string is an "atom"
**
**	Parameters:
**		s -- the string to check.
**
**	Returns:
**		true -- iff s is an atom
*/

bool
isatom(s)
	const char *s;
{
	int c;

	if (s == NULL || *s == '\0')
		return false;
	while ((c = *s++) != '\0')
	{
		if (strchr("()<>@,;:\\.[]\"", c) != NULL)
			return false;
		if (c < '!' || c > '~')
			return false;
	}
	return true;
}
/*
**  PRUNEROUTE -- prune an RFC-822 source route
**
**	Trims down a source route to the last internet-registered hop.
**	This is encouraged by RFC 1123 section 5.3.3.
**
**	Parameters:
**		addr -- the address
**
**	Returns:
**		true -- address was modified
**		false -- address could not be pruned
**
**	Side Effects:
**		modifies addr in-place
*/

static bool
pruneroute(addr)
	char *addr;
{
#if NAMED_BIND
	char *start, *at, *comma;
	char c;
	int braclev;
	int rcode;
	int i;
	char hostbuf[BUFSIZ];
	char *mxhosts[MAXMXHOSTS + 1];

	/* check to see if this is really a route-addr */
	if (*addr != '<' || addr[1] != '@' || addr[strlen(addr) - 1] != '>')
		return false;

	/*
	**  Can't simply find the first ':' is the address might be in the
	**  form:  "<@[IPv6:::1]:user@host>" and the first ':' in inside
	**  the IPv6 address.
	*/

	start = addr;
	braclev = 0;
	while (*start != '\0')
	{
		if (*start == ':' && braclev <= 0)
			break;
		else if (*start == '[')
			braclev++;
		else if (*start == ']' && braclev > 0)
			braclev--;
		start++;
	}
	if (braclev > 0 || *start != ':')
		return false;

	at = strrchr(addr, '@');
	if (at == NULL || at < start)
		return false;

	/* slice off the angle brackets */
	i = strlen(at + 1);
	if (i >= sizeof(hostbuf))
		return false;
	(void) sm_strlcpy(hostbuf, at + 1, sizeof(hostbuf));
	hostbuf[i - 1] = '\0';

	while (start != NULL)
	{
		if (getmxrr(hostbuf, mxhosts, NULL, false,
			    &rcode, true, NULL) > 0)
		{
			(void) sm_strlcpy(addr + 1, start + 1,
					  strlen(addr) - 1);
			return true;
		}
		c = *start;
		*start = '\0';
		comma = strrchr(addr, ',');
		if (comma != NULL && comma[1] == '@' &&
		    strlen(comma + 2) < sizeof(hostbuf))
			(void) sm_strlcpy(hostbuf, comma + 2, sizeof(hostbuf));
		else
			comma = NULL;
		*start = c;
		start = comma;
	}
#endif /* NAMED_BIND */
	return false;
}
