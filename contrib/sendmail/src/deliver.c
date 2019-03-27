/*
 * Copyright (c) 1998-2010, 2012 Proofpoint, Inc. and its suppliers.
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
#include <sm/time.h>

SM_RCSID("@(#)$Id: deliver.c,v 8.1030 2013-11-22 20:51:55 ca Exp $")

#if HASSETUSERCONTEXT
# include <login_cap.h>
#endif /* HASSETUSERCONTEXT */

#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */

#if STARTTLS || SASL
# include "sfsasl.h"
#endif /* STARTTLS || SASL */

static int	deliver __P((ENVELOPE *, ADDRESS *));
static void	dup_queue_file __P((ENVELOPE *, ENVELOPE *, int));
static void	mailfiletimeout __P((int));
static void	endwaittimeout __P((int));
static int	parse_hostsignature __P((char *, char **, MAILER *));
static void	sendenvelope __P((ENVELOPE *, int));
static int	coloncmp __P((const char *, const char *));

#if STARTTLS
# include <openssl/err.h>
static int	starttls __P((MAILER *, MCI *, ENVELOPE *));
static int	endtlsclt __P((MCI *));
#endif /* STARTTLS */
# if STARTTLS || SASL
static bool	iscltflgset __P((ENVELOPE *, int));
# endif /* STARTTLS || SASL */

/*
**  SENDALL -- actually send all the messages.
**
**	Parameters:
**		e -- the envelope to send.
**		mode -- the delivery mode to use.  If SM_DEFAULT, use
**			the current e->e_sendmode.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Scans the send lists and sends everything it finds.
**		Delivers any appropriate error messages.
**		If we are running in a non-interactive mode, takes the
**			appropriate action.
*/

void
sendall(e, mode)
	ENVELOPE *e;
	int mode;
{
	register ADDRESS *q;
	char *owner;
	int otherowners;
	int save_errno;
	register ENVELOPE *ee;
	ENVELOPE *splitenv = NULL;
	int oldverbose = Verbose;
	bool somedeliveries = false, expensive = false;
	pid_t pid;

	/*
	**  If this message is to be discarded, don't bother sending
	**  the message at all.
	*/

	if (bitset(EF_DISCARD, e->e_flags))
	{
		if (tTd(13, 1))
			sm_dprintf("sendall: discarding id %s\n", e->e_id);
		e->e_flags |= EF_CLRQUEUE;
		if (LogLevel > 9)
			logundelrcpts(e, "discarded", 9, true);
		else if (LogLevel > 4)
			sm_syslog(LOG_INFO, e->e_id, "discarded");
		markstats(e, NULL, STATS_REJECT);
		return;
	}

	/*
	**  If we have had global, fatal errors, don't bother sending
	**  the message at all if we are in SMTP mode.  Local errors
	**  (e.g., a single address failing) will still cause the other
	**  addresses to be sent.
	*/

	if (bitset(EF_FATALERRS, e->e_flags) &&
	    (OpMode == MD_SMTP || OpMode == MD_DAEMON))
	{
		e->e_flags |= EF_CLRQUEUE;
		return;
	}

	/* determine actual delivery mode */
	if (mode == SM_DEFAULT)
	{
		mode = e->e_sendmode;
		if (mode != SM_VERIFY && mode != SM_DEFER &&
		    shouldqueue(e->e_msgpriority, e->e_ctime))
			mode = SM_QUEUE;
	}

	if (tTd(13, 1))
	{
		sm_dprintf("\n===== SENDALL: mode %c, id %s, e_from ",
			mode, e->e_id);
		printaddr(sm_debug_file(), &e->e_from, false);
		sm_dprintf("\te_flags = ");
		printenvflags(e);
		sm_dprintf("sendqueue:\n");
		printaddr(sm_debug_file(), e->e_sendqueue, true);
	}

	/*
	**  Do any preprocessing necessary for the mode we are running.
	**	Check to make sure the hop count is reasonable.
	**	Delete sends to the sender in mailing lists.
	*/

	CurEnv = e;
	if (tTd(62, 1))
		checkfds(NULL);

	if (e->e_hopcount > MaxHopCount)
	{
		char *recip;

		if (e->e_sendqueue != NULL &&
		    e->e_sendqueue->q_paddr != NULL)
			recip = e->e_sendqueue->q_paddr;
		else
			recip = "(nobody)";

		errno = 0;
		queueup(e, WILL_BE_QUEUED(mode), false);
		e->e_flags |= EF_FATALERRS|EF_PM_NOTIFY|EF_CLRQUEUE;
		ExitStat = EX_UNAVAILABLE;
		syserr("554 5.4.6 Too many hops %d (%d max): from %s via %s, to %s",
		       e->e_hopcount, MaxHopCount, e->e_from.q_paddr,
		       RealHostName == NULL ? "localhost" : RealHostName,
		       recip);
		for (q = e->e_sendqueue; q != NULL; q = q->q_next)
		{
			if (QS_IS_DEAD(q->q_state))
				continue;
			q->q_state = QS_BADADDR;
			q->q_status = "5.4.6";
			q->q_rstatus = "554 5.4.6 Too many hops";
		}
		return;
	}

	/*
	**  Do sender deletion.
	**
	**	If the sender should be queued up, skip this.
	**	This can happen if the name server is hosed when you
	**	are trying to send mail.  The result is that the sender
	**	is instantiated in the queue as a recipient.
	*/

	if (!bitset(EF_METOO, e->e_flags) &&
	    !QS_IS_QUEUEUP(e->e_from.q_state))
	{
		if (tTd(13, 5))
		{
			sm_dprintf("sendall: QS_SENDER ");
			printaddr(sm_debug_file(), &e->e_from, false);
		}
		e->e_from.q_state = QS_SENDER;
		(void) recipient(&e->e_from, &e->e_sendqueue, 0, e);
	}

	/*
	**  Handle alias owners.
	**
	**	We scan up the q_alias chain looking for owners.
	**	We discard owners that are the same as the return path.
	*/

	for (q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		register struct address *a;

		for (a = q; a != NULL && a->q_owner == NULL; a = a->q_alias)
			continue;
		if (a != NULL)
			q->q_owner = a->q_owner;

		if (q->q_owner != NULL &&
		    !QS_IS_DEAD(q->q_state) &&
		    strcmp(q->q_owner, e->e_from.q_paddr) == 0)
			q->q_owner = NULL;
	}

	if (tTd(13, 25))
	{
		sm_dprintf("\nAfter first owner pass, sendq =\n");
		printaddr(sm_debug_file(), e->e_sendqueue, true);
	}

	owner = "";
	otherowners = 1;
	while (owner != NULL && otherowners > 0)
	{
		if (tTd(13, 28))
			sm_dprintf("owner = \"%s\", otherowners = %d\n",
				   owner, otherowners);
		owner = NULL;
		otherowners = bitset(EF_SENDRECEIPT, e->e_flags) ? 1 : 0;

		for (q = e->e_sendqueue; q != NULL; q = q->q_next)
		{
			if (tTd(13, 30))
			{
				sm_dprintf("Checking ");
				printaddr(sm_debug_file(), q, false);
			}
			if (QS_IS_DEAD(q->q_state))
			{
				if (tTd(13, 30))
					sm_dprintf("    ... QS_IS_DEAD\n");
				continue;
			}
			if (tTd(13, 29) && !tTd(13, 30))
			{
				sm_dprintf("Checking ");
				printaddr(sm_debug_file(), q, false);
			}

			if (q->q_owner != NULL)
			{
				if (owner == NULL)
				{
					if (tTd(13, 40))
						sm_dprintf("    ... First owner = \"%s\"\n",
							   q->q_owner);
					owner = q->q_owner;
				}
				else if (owner != q->q_owner)
				{
					if (strcmp(owner, q->q_owner) == 0)
					{
						if (tTd(13, 40))
							sm_dprintf("    ... Same owner = \"%s\"\n",
								   owner);

						/* make future comparisons cheap */
						q->q_owner = owner;
					}
					else
					{
						if (tTd(13, 40))
							sm_dprintf("    ... Another owner \"%s\"\n",
								   q->q_owner);
						otherowners++;
					}
					owner = q->q_owner;
				}
				else if (tTd(13, 40))
					sm_dprintf("    ... Same owner = \"%s\"\n",
						   owner);
			}
			else
			{
				if (tTd(13, 40))
					sm_dprintf("    ... Null owner\n");
				otherowners++;
			}

			if (QS_IS_BADADDR(q->q_state))
			{
				if (tTd(13, 30))
					sm_dprintf("    ... QS_IS_BADADDR\n");
				continue;
			}

			if (QS_IS_QUEUEUP(q->q_state))
			{
				MAILER *m = q->q_mailer;

				/*
				**  If we have temporary address failures
				**  (e.g., dns failure) and a fallback MX is
				**  set, send directly to the fallback MX host.
				*/

				if (FallbackMX != NULL &&
				    !wordinclass(FallbackMX, 'w') &&
				    mode != SM_VERIFY &&
				    !bitnset(M_NOMX, m->m_flags) &&
				    strcmp(m->m_mailer, "[IPC]") == 0 &&
				    m->m_argv[0] != NULL &&
				    strcmp(m->m_argv[0], "TCP") == 0)
				{
					int len;
					char *p;

					if (tTd(13, 30))
						sm_dprintf("    ... FallbackMX\n");

					len = strlen(FallbackMX) + 1;
					p = sm_rpool_malloc_x(e->e_rpool, len);
					(void) sm_strlcpy(p, FallbackMX, len);
					q->q_state = QS_OK;
					q->q_host = p;
				}
				else
				{
					if (tTd(13, 30))
						sm_dprintf("    ... QS_IS_QUEUEUP\n");
					continue;
				}
			}

			/*
			**  If this mailer is expensive, and if we don't
			**  want to make connections now, just mark these
			**  addresses and return.  This is useful if we
			**  want to batch connections to reduce load.  This
			**  will cause the messages to be queued up, and a
			**  daemon will come along to send the messages later.
			*/

			if (NoConnect && !Verbose &&
			    bitnset(M_EXPENSIVE, q->q_mailer->m_flags))
			{
				if (tTd(13, 30))
					sm_dprintf("    ... expensive\n");
				q->q_state = QS_QUEUEUP;
				expensive = true;
			}
			else if (bitnset(M_HOLD, q->q_mailer->m_flags) &&
				 QueueLimitId == NULL &&
				 QueueLimitSender == NULL &&
				 QueueLimitRecipient == NULL)
			{
				if (tTd(13, 30))
					sm_dprintf("    ... hold\n");
				q->q_state = QS_QUEUEUP;
				expensive = true;
			}
			else if (QueueMode != QM_QUARANTINE &&
				 e->e_quarmsg != NULL)
			{
				if (tTd(13, 30))
					sm_dprintf("    ... quarantine: %s\n",
						   e->e_quarmsg);
				q->q_state = QS_QUEUEUP;
				expensive = true;
			}
			else
			{
				if (tTd(13, 30))
					sm_dprintf("    ... deliverable\n");
				somedeliveries = true;
			}
		}

		if (owner != NULL && otherowners > 0)
		{
			/*
			**  Split this envelope into two.
			*/

			ee = (ENVELOPE *) sm_rpool_malloc_x(e->e_rpool,
							    sizeof(*ee));
			STRUCTCOPY(*e, *ee);
			ee->e_message = NULL;
			ee->e_id = NULL;
			assign_queueid(ee);

			if (tTd(13, 1))
				sm_dprintf("sendall: split %s into %s, owner = \"%s\", otherowners = %d\n",
					   e->e_id, ee->e_id, owner,
					   otherowners);

			ee->e_header = copyheader(e->e_header, ee->e_rpool);
			ee->e_sendqueue = copyqueue(e->e_sendqueue,
						    ee->e_rpool);
			ee->e_errorqueue = copyqueue(e->e_errorqueue,
						     ee->e_rpool);
			ee->e_flags = e->e_flags & ~(EF_INQUEUE|EF_CLRQUEUE|EF_FATALERRS|EF_SENDRECEIPT|EF_RET_PARAM);
			ee->e_flags |= EF_NORECEIPT;
			setsender(owner, ee, NULL, '\0', true);
			if (tTd(13, 5))
			{
				sm_dprintf("sendall(split): QS_SENDER ");
				printaddr(sm_debug_file(), &ee->e_from, false);
			}
			ee->e_from.q_state = QS_SENDER;
			ee->e_dfp = NULL;
			ee->e_lockfp = NULL;
			ee->e_xfp = NULL;
			ee->e_qgrp = e->e_qgrp;
			ee->e_qdir = e->e_qdir;
			ee->e_errormode = EM_MAIL;
			ee->e_sibling = splitenv;
			ee->e_statmsg = NULL;
			if (e->e_quarmsg != NULL)
				ee->e_quarmsg = sm_rpool_strdup_x(ee->e_rpool,
								  e->e_quarmsg);
			splitenv = ee;

			for (q = e->e_sendqueue; q != NULL; q = q->q_next)
			{
				if (q->q_owner == owner)
				{
					q->q_state = QS_CLONED;
					if (tTd(13, 6))
						sm_dprintf("\t... stripping %s from original envelope\n",
							   q->q_paddr);
				}
			}
			for (q = ee->e_sendqueue; q != NULL; q = q->q_next)
			{
				if (q->q_owner != owner)
				{
					q->q_state = QS_CLONED;
					if (tTd(13, 6))
						sm_dprintf("\t... dropping %s from cloned envelope\n",
							   q->q_paddr);
				}
				else
				{
					/* clear DSN parameters */
					q->q_flags &= ~(QHASNOTIFY|Q_PINGFLAGS);
					q->q_flags |= DefaultNotify & ~QPINGONSUCCESS;
					if (tTd(13, 6))
						sm_dprintf("\t... moving %s to cloned envelope\n",
							   q->q_paddr);
				}
			}

			if (mode != SM_VERIFY && bitset(EF_HAS_DF, e->e_flags))
				dup_queue_file(e, ee, DATAFL_LETTER);

			/*
			**  Give the split envelope access to the parent
			**  transcript file for errors obtained while
			**  processing the recipients (done before the
			**  envelope splitting).
			*/

			if (e->e_xfp != NULL)
				ee->e_xfp = sm_io_dup(e->e_xfp);

			/* failed to dup e->e_xfp, start a new transcript */
			if (ee->e_xfp == NULL)
				openxscript(ee);

			if (mode != SM_VERIFY && LogLevel > 4)
				sm_syslog(LOG_INFO, e->e_id,
					  "%s: clone: owner=%s",
					  ee->e_id, owner);
		}
	}

	if (owner != NULL)
	{
		setsender(owner, e, NULL, '\0', true);
		if (tTd(13, 5))
		{
			sm_dprintf("sendall(owner): QS_SENDER ");
			printaddr(sm_debug_file(), &e->e_from, false);
		}
		e->e_from.q_state = QS_SENDER;
		e->e_errormode = EM_MAIL;
		e->e_flags |= EF_NORECEIPT;
		e->e_flags &= ~EF_FATALERRS;
	}

	/* if nothing to be delivered, just queue up everything */
	if (!somedeliveries && !WILL_BE_QUEUED(mode) &&
	    mode != SM_VERIFY)
	{
		time_t now;

		if (tTd(13, 29))
			sm_dprintf("No deliveries: auto-queueing\n");
		mode = SM_QUEUE;
		now = curtime();

		/* treat this as a delivery in terms of counting tries */
		e->e_dtime = now;
		if (!expensive)
			e->e_ntries++;
		for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
		{
			ee->e_dtime = now;
			if (!expensive)
				ee->e_ntries++;
		}
	}

	if ((WILL_BE_QUEUED(mode) || mode == SM_FORK ||
	     (mode != SM_VERIFY &&
	      (SuperSafe == SAFE_REALLY ||
	       SuperSafe == SAFE_REALLY_POSTMILTER))) &&
	    (!bitset(EF_INQUEUE, e->e_flags) || splitenv != NULL))
	{
		bool msync;

		/*
		**  Be sure everything is instantiated in the queue.
		**  Split envelopes first in case the machine crashes.
		**  If the original were done first, we may lose
		**  recipients.
		*/

#if !HASFLOCK
		msync = false;
#else /* !HASFLOCK */
		msync = mode == SM_FORK;
#endif /* !HASFLOCK */

		for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
			queueup(ee, WILL_BE_QUEUED(mode), msync);
		queueup(e, WILL_BE_QUEUED(mode), msync);
	}

	if (tTd(62, 10))
		checkfds("after envelope splitting");

	/*
	**  If we belong in background, fork now.
	*/

	if (tTd(13, 20))
	{
		sm_dprintf("sendall: final mode = %c\n", mode);
		if (tTd(13, 21))
		{
			sm_dprintf("\n================ Final Send Queue(s) =====================\n");
			sm_dprintf("\n  *** Envelope %s, e_from=%s ***\n",
				   e->e_id, e->e_from.q_paddr);
			printaddr(sm_debug_file(), e->e_sendqueue, true);
			for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
			{
				sm_dprintf("\n  *** Envelope %s, e_from=%s ***\n",
					   ee->e_id, ee->e_from.q_paddr);
				printaddr(sm_debug_file(), ee->e_sendqueue, true);
			}
			sm_dprintf("==========================================================\n\n");
		}
	}
	switch (mode)
	{
	  case SM_VERIFY:
		Verbose = 2;
		break;

	  case SM_QUEUE:
	  case SM_DEFER:
#if HASFLOCK
  queueonly:
#endif /* HASFLOCK */
		if (e->e_nrcpts > 0)
			e->e_flags |= EF_INQUEUE;
		(void) dropenvelope(e, splitenv != NULL, true);
		for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
		{
			if (ee->e_nrcpts > 0)
				ee->e_flags |= EF_INQUEUE;
			(void) dropenvelope(ee, false, true);
		}
		return;

	  case SM_FORK:
		if (e->e_xfp != NULL)
			(void) sm_io_flush(e->e_xfp, SM_TIME_DEFAULT);

#if !HASFLOCK
		/*
		**  Since fcntl locking has the interesting semantic that
		**  the lock is owned by a process, not by an open file
		**  descriptor, we have to flush this to the queue, and
		**  then restart from scratch in the child.
		*/

		{
			/* save id for future use */
			char *qid = e->e_id;

			/* now drop the envelope in the parent */
			e->e_flags |= EF_INQUEUE;
			(void) dropenvelope(e, splitenv != NULL, false);

			/* arrange to reacquire lock after fork */
			e->e_id = qid;
		}

		for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
		{
			/* save id for future use */
			char *qid = ee->e_id;

			/* drop envelope in parent */
			ee->e_flags |= EF_INQUEUE;
			(void) dropenvelope(ee, false, false);

			/* and save qid for reacquisition */
			ee->e_id = qid;
		}

#endif /* !HASFLOCK */

		/*
		**  Since the delivery may happen in a child and the parent
		**  does not wait, the parent may close the maps thereby
		**  removing any shared memory used by the map.  Therefore,
		**  close the maps now so the child will dynamically open
		**  them if necessary.
		*/

		closemaps(false);

		pid = fork();
		if (pid < 0)
		{
			syserr("deliver: fork 1");
#if HASFLOCK
			goto queueonly;
#else /* HASFLOCK */
			e->e_id = NULL;
			for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
				ee->e_id = NULL;
			return;
#endif /* HASFLOCK */
		}
		else if (pid > 0)
		{
#if HASFLOCK
			/* be sure we leave the temp files to our child */
			/* close any random open files in the envelope */
			closexscript(e);
			if (e->e_dfp != NULL)
				(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
			e->e_dfp = NULL;
			e->e_flags &= ~EF_HAS_DF;

			/* can't call unlockqueue to avoid unlink of xfp */
			if (e->e_lockfp != NULL)
				(void) sm_io_close(e->e_lockfp, SM_TIME_DEFAULT);
			else
				syserr("%s: sendall: null lockfp", e->e_id);
			e->e_lockfp = NULL;
#endif /* HASFLOCK */

			/* make sure the parent doesn't own the envelope */
			e->e_id = NULL;

#if USE_DOUBLE_FORK
			/* catch intermediate zombie */
			(void) waitfor(pid);
#endif /* USE_DOUBLE_FORK */
			return;
		}

		/* Reset global flags */
		RestartRequest = NULL;
		RestartWorkGroup = false;
		ShutdownRequest = NULL;
		PendingSignal = 0;

		/*
		**  Initialize exception stack and default exception
		**  handler for child process.
		*/

		sm_exc_newthread(fatal_error);

		/*
		**  Since we have accepted responsbility for the message,
		**  change the SIGTERM handler.  intsig() (the old handler)
		**  would remove the envelope if this was a command line
		**  message submission.
		*/

		(void) sm_signal(SIGTERM, SIG_DFL);

#if USE_DOUBLE_FORK
		/* double fork to avoid zombies */
		pid = fork();
		if (pid > 0)
			exit(EX_OK);
		save_errno = errno;
#endif /* USE_DOUBLE_FORK */

		CurrentPid = getpid();

		/* be sure we are immune from the terminal */
		disconnect(2, e);
		clearstats();

		/* prevent parent from waiting if there was an error */
		if (pid < 0)
		{
			errno = save_errno;
			syserr("deliver: fork 2");
#if HASFLOCK
			e->e_flags |= EF_INQUEUE;
#else /* HASFLOCK */
			e->e_id = NULL;
#endif /* HASFLOCK */
			finis(true, true, ExitStat);
		}

		/* be sure to give error messages in child */
		QuickAbort = false;

		/*
		**  Close any cached connections.
		**
		**	We don't send the QUIT protocol because the parent
		**	still knows about the connection.
		**
		**	This should only happen when delivering an error
		**	message.
		*/

		mci_flush(false, NULL);

#if HASFLOCK
		break;
#else /* HASFLOCK */

		/*
		**  Now reacquire and run the various queue files.
		*/

		for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
		{
			ENVELOPE *sibling = ee->e_sibling;

			(void) dowork(ee->e_qgrp, ee->e_qdir, ee->e_id,
				      false, false, ee);
			ee->e_sibling = sibling;
		}
		(void) dowork(e->e_qgrp, e->e_qdir, e->e_id,
			      false, false, e);
		finis(true, true, ExitStat);
#endif /* HASFLOCK */
	}

	sendenvelope(e, mode);
	(void) dropenvelope(e, true, true);
	for (ee = splitenv; ee != NULL; ee = ee->e_sibling)
	{
		CurEnv = ee;
		if (mode != SM_VERIFY)
			openxscript(ee);
		sendenvelope(ee, mode);
		(void) dropenvelope(ee, true, true);
	}
	CurEnv = e;

	Verbose = oldverbose;
	if (mode == SM_FORK)
		finis(true, true, ExitStat);
}

static void
sendenvelope(e, mode)
	register ENVELOPE *e;
	int mode;
{
	register ADDRESS *q;
	bool didany;

	if (tTd(13, 10))
		sm_dprintf("sendenvelope(%s) e_flags=0x%lx\n",
			   e->e_id == NULL ? "[NOQUEUE]" : e->e_id,
			   e->e_flags);
	if (LogLevel > 80)
		sm_syslog(LOG_DEBUG, e->e_id,
			  "sendenvelope, flags=0x%lx",
			  e->e_flags);

	/*
	**  If we have had global, fatal errors, don't bother sending
	**  the message at all if we are in SMTP mode.  Local errors
	**  (e.g., a single address failing) will still cause the other
	**  addresses to be sent.
	*/

	if (bitset(EF_FATALERRS, e->e_flags) &&
	    (OpMode == MD_SMTP || OpMode == MD_DAEMON))
	{
		e->e_flags |= EF_CLRQUEUE;
		return;
	}

	/*
	**  Don't attempt deliveries if we want to bounce now
	**  or if deliver-by time is exceeded.
	*/

	if (!bitset(EF_RESPONSE, e->e_flags) &&
	    (TimeOuts.to_q_return[e->e_timeoutclass] == NOW ||
	     (IS_DLVR_RETURN(e) && e->e_deliver_by > 0 &&
	      curtime() > e->e_ctime + e->e_deliver_by)))
		return;

	/*
	**  Run through the list and send everything.
	**
	**	Set EF_GLOBALERRS so that error messages during delivery
	**	result in returned mail.
	*/

	e->e_nsent = 0;
	e->e_flags |= EF_GLOBALERRS;

	macdefine(&e->e_macro, A_PERM, macid("{envid}"), e->e_envid);
	macdefine(&e->e_macro, A_PERM, macid("{bodytype}"), e->e_bodytype);
	didany = false;

	if (!bitset(EF_SPLIT, e->e_flags))
	{
		ENVELOPE *oldsib;
		ENVELOPE *ee;

		/*
		**  Save old sibling and set it to NULL to avoid
		**  queueing up the same envelopes again.
		**  This requires that envelopes in that list have
		**  been take care of before (or at some other place).
		*/

		oldsib = e->e_sibling;
		e->e_sibling = NULL;
		if (!split_by_recipient(e) &&
		    bitset(EF_FATALERRS, e->e_flags))
		{
			if (OpMode == MD_SMTP || OpMode == MD_DAEMON)
				e->e_flags |= EF_CLRQUEUE;
			return;
		}
		for (ee = e->e_sibling; ee != NULL; ee = ee->e_sibling)
			queueup(ee, false, true);

		/* clean up */
		for (ee = e->e_sibling; ee != NULL; ee = ee->e_sibling)
		{
			/* now unlock the job */
			closexscript(ee);
			unlockqueue(ee);

			/* this envelope is marked unused */
			if (ee->e_dfp != NULL)
			{
				(void) sm_io_close(ee->e_dfp, SM_TIME_DEFAULT);
				ee->e_dfp = NULL;
			}
			ee->e_id = NULL;
			ee->e_flags &= ~EF_HAS_DF;
		}
		e->e_sibling = oldsib;
	}

	/* now run through the queue */
	for (q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
#if XDEBUG
		char wbuf[MAXNAME + 20];

		(void) sm_snprintf(wbuf, sizeof(wbuf), "sendall(%.*s)",
				   MAXNAME, q->q_paddr);
		checkfd012(wbuf);
#endif /* XDEBUG */
		if (mode == SM_VERIFY)
		{
			e->e_to = q->q_paddr;
			if (QS_IS_SENDABLE(q->q_state))
			{
				if (q->q_host != NULL && q->q_host[0] != '\0')
					message("deliverable: mailer %s, host %s, user %s",
						q->q_mailer->m_name,
						q->q_host,
						q->q_user);
				else
					message("deliverable: mailer %s, user %s",
						q->q_mailer->m_name,
						q->q_user);
			}
		}
		else if (QS_IS_OK(q->q_state))
		{
			/*
			**  Checkpoint the send list every few addresses
			*/

			if (CheckpointInterval > 0 &&
			    e->e_nsent >= CheckpointInterval)
			{
				queueup(e, false, false);
				e->e_nsent = 0;
			}
			(void) deliver(e, q);
			didany = true;
		}
	}
	if (didany)
	{
		e->e_dtime = curtime();
		e->e_ntries++;
	}

#if XDEBUG
	checkfd012("end of sendenvelope");
#endif /* XDEBUG */
}

#if REQUIRES_DIR_FSYNC
/*
**  SYNC_DIR -- fsync a directory based on a filename
**
**	Parameters:
**		filename -- path of file
**		panic -- panic?
**
**	Returns:
**		none
*/

void
sync_dir(filename, panic)
	char *filename;
	bool panic;
{
	int dirfd;
	char *dirp;
	char dir[MAXPATHLEN];

	if (!RequiresDirfsync)
		return;

	/* filesystems which require the directory be synced */
	dirp = strrchr(filename, '/');
	if (dirp != NULL)
	{
		if (sm_strlcpy(dir, filename, sizeof(dir)) >= sizeof(dir))
			return;
		dir[dirp - filename] = '\0';
		dirp = dir;
	}
	else
		dirp = ".";
	dirfd = open(dirp, O_RDONLY, 0700);
	if (tTd(40,32))
		sm_syslog(LOG_INFO, NOQID, "sync_dir: %s: fsync(%d)",
			  dirp, dirfd);
	if (dirfd >= 0)
	{
		if (fsync(dirfd) < 0)
		{
			if (panic)
				syserr("!sync_dir: cannot fsync directory %s",
				       dirp);
			else if (LogLevel > 1)
				sm_syslog(LOG_ERR, NOQID,
					  "sync_dir: cannot fsync directory %s: %s",
					  dirp, sm_errstring(errno));
		}
		(void) close(dirfd);
	}
}
#endif /* REQUIRES_DIR_FSYNC */
/*
**  DUP_QUEUE_FILE -- duplicate a queue file into a split queue
**
**	Parameters:
**		e -- the existing envelope
**		ee -- the new envelope
**		type -- the queue file type (e.g., DATAFL_LETTER)
**
**	Returns:
**		none
*/

static void
dup_queue_file(e, ee, type)
	ENVELOPE *e, *ee;
	int type;
{
	char f1buf[MAXPATHLEN], f2buf[MAXPATHLEN];

	ee->e_dfp = NULL;
	ee->e_xfp = NULL;

	/*
	**  Make sure both are in the same directory.
	*/

	(void) sm_strlcpy(f1buf, queuename(e, type), sizeof(f1buf));
	(void) sm_strlcpy(f2buf, queuename(ee, type), sizeof(f2buf));

	/* Force the df to disk if it's not there yet */
	if (type == DATAFL_LETTER && e->e_dfp != NULL &&
	    sm_io_setinfo(e->e_dfp, SM_BF_COMMIT, NULL) < 0 &&
	    errno != EINVAL)
	{
		syserr("!dup_queue_file: can't commit %s", f1buf);
		/* NOTREACHED */
	}

	if (link(f1buf, f2buf) < 0)
	{
		int save_errno = errno;

		syserr("sendall: link(%s, %s)", f1buf, f2buf);
		if (save_errno == EEXIST)
		{
			if (unlink(f2buf) < 0)
			{
				syserr("!sendall: unlink(%s): permanent",
				       f2buf);
				/* NOTREACHED */
			}
			if (link(f1buf, f2buf) < 0)
			{
				syserr("!sendall: link(%s, %s): permanent",
				       f1buf, f2buf);
				/* NOTREACHED */
			}
		}
	}
	SYNC_DIR(f2buf, true);
}
/*
**  DOFORK -- do a fork, retrying a couple of times on failure.
**
**	This MUST be a macro, since after a vfork we are running
**	two processes on the same stack!!!
**
**	Parameters:
**		none.
**
**	Returns:
**		From a macro???  You've got to be kidding!
**
**	Side Effects:
**		Modifies the ==> LOCAL <== variable 'pid', leaving:
**			pid of child in parent, zero in child.
**			-1 on unrecoverable error.
**
**	Notes:
**		I'm awfully sorry this looks so awful.  That's
**		vfork for you.....
*/

#define NFORKTRIES	5

#ifndef FORK
# define FORK	fork
#endif /* ! FORK */

#define DOFORK(fORKfN) \
{\
	register int i;\
\
	for (i = NFORKTRIES; --i >= 0; )\
	{\
		pid = fORKfN();\
		if (pid >= 0)\
			break;\
		if (i > 0)\
			(void) sleep((unsigned) NFORKTRIES - i);\
	}\
}
/*
**  DOFORK -- simple fork interface to DOFORK.
**
**	Parameters:
**		none.
**
**	Returns:
**		pid of child in parent.
**		zero in child.
**		-1 on error.
**
**	Side Effects:
**		returns twice, once in parent and once in child.
*/

pid_t
dofork()
{
	register pid_t pid = -1;

	DOFORK(fork);
	return pid;
}

/*
**  COLONCMP -- compare host-signatures up to first ':' or EOS
**
**	This takes two strings which happen to be host-signatures and
**	compares them. If the lowest preference portions of the MX-RR's
**	match (up to ':' or EOS, whichever is first), then we have
**	match. This is used for coattail-piggybacking messages during
**	message delivery.
**	If the signatures are the same up to the first ':' the remainder of
**	the signatures are then compared with a normal strcmp(). This saves
**	re-examining the first part of the signatures.
**
**	Parameters:
**		a - first host-signature
**		b - second host-signature
**
**	Returns:
**		HS_MATCH_NO -- no "match".
**		HS_MATCH_FIRST -- "match" for the first MX preference
**			(up to the first colon (':')).
**		HS_MATCH_FULL -- match for the entire MX record.
**
**	Side Effects:
**		none.
*/

#define HS_MATCH_NO	0
#define HS_MATCH_FIRST	1
#define HS_MATCH_FULL	2

static int
coloncmp(a, b)
	register const char *a;
	register const char *b;
{
	int ret = HS_MATCH_NO;
	int braclev = 0;

	while (*a == *b++)
	{
		/* Need to account for IPv6 bracketed addresses */
		if (*a == '[')
			braclev++;
		else if (*a == ']' && braclev > 0)
			braclev--;
		else if (*a == ':' && braclev <= 0)
		{
			ret = HS_MATCH_FIRST;
			a++;
			break;
		}
		else if (*a == '\0')
			return HS_MATCH_FULL; /* a full match */
		a++;
	}
	if (ret == HS_MATCH_NO &&
	    braclev <= 0 &&
	    ((*a == '\0' && *(b - 1) == ':') ||
	     (*a == ':' && *(b - 1) == '\0')))
		return HS_MATCH_FIRST;
	if (ret == HS_MATCH_FIRST && strcmp(a, b) == 0)
		return HS_MATCH_FULL;

	return ret;
}

/*
**  SHOULD_TRY_FBSH -- Should try FallbackSmartHost?
**
**	Parameters:
**		e -- envelope
**		tried_fallbacksmarthost -- has been tried already? (in/out)
**		hostbuf -- buffer for hostname (expand FallbackSmartHost) (out)
**		hbsz -- size of hostbuf
**		status -- current delivery status
**
**	Returns:
**		true iff FallbackSmartHost should be tried.
*/

static bool should_try_fbsh __P((ENVELOPE *, bool *, char *, size_t, int));

static bool
should_try_fbsh(e, tried_fallbacksmarthost, hostbuf, hbsz, status)
	ENVELOPE *e;
	bool *tried_fallbacksmarthost;
	char *hostbuf;
	size_t hbsz;
	int status;
{
	/*
	**  If the host was not found or a temporary failure occurred
	**  and a FallbackSmartHost is defined (and we have not yet
	**  tried it), then make one last try with it as the host.
	*/

	if ((status == EX_NOHOST || status == EX_TEMPFAIL) &&
	    FallbackSmartHost != NULL && !*tried_fallbacksmarthost)
	{
		*tried_fallbacksmarthost = true;
		expand(FallbackSmartHost, hostbuf, hbsz, e);
		if (!wordinclass(hostbuf, 'w'))
		{
			if (tTd(11, 1))
				sm_dprintf("one last try with FallbackSmartHost %s\n",
					   hostbuf);
			return true;
		}
	}
	return false;
}

/*
**  DELIVER -- Deliver a message to a list of addresses.
**
**	This routine delivers to everyone on the same host as the
**	user on the head of the list.  It is clever about mailers
**	that don't handle multiple users.  It is NOT guaranteed
**	that it will deliver to all these addresses however -- so
**	deliver should be called once for each address on the
**	list.
**	Deliver tries to be as opportunistic as possible about piggybacking
**	messages. Some definitions to make understanding easier follow below.
**	Piggybacking occurs when an existing connection to a mail host can
**	be used to send the same message to more than one recipient at the
**	same time. So "no piggybacking" means one message for one recipient
**	per connection. "Intentional piggybacking" happens when the
**	recipients' host address (not the mail host address) is used to
**	attempt piggybacking. Recipients with the same host address
**	have the same mail host. "Coincidental piggybacking" relies on
**	piggybacking based on all the mail host addresses in the MX-RR. This
**	is "coincidental" in the fact it could not be predicted until the
**	MX Resource Records for the hosts were obtained and examined. For
**	example (preference order and equivalence is important, not values):
**		domain1 IN MX 10 mxhost-A
**			IN MX 20 mxhost-B
**		domain2 IN MX  4 mxhost-A
**			IN MX  8 mxhost-B
**	Domain1 and domain2 can piggyback the same message to mxhost-A or
**	mxhost-B (if mxhost-A cannot be reached).
**	"Coattail piggybacking" relaxes the strictness of "coincidental
**	piggybacking" in the hope that most significant (lowest value)
**	MX preference host(s) can create more piggybacking. For example
**	(again, preference order and equivalence is important, not values):
**		domain3 IN MX 100 mxhost-C
**			IN MX 100 mxhost-D
**			IN MX 200 mxhost-E
**		domain4 IN MX  50 mxhost-C
**			IN MX  50 mxhost-D
**			IN MX  80 mxhost-F
**	A message for domain3 and domain4 can piggyback to mxhost-C if mxhost-C
**	is available. Same with mxhost-D because in both RR's the preference
**	value is the same as mxhost-C, respectively.
**	So deliver attempts coattail piggybacking when possible. If the
**	first MX preference level hosts cannot be used then the piggybacking
**	reverts to coincidental piggybacking. Using the above example you
**	cannot deliver to mxhost-F for domain3 regardless of preference value.
**	("Coattail" from "riding on the coattails of your predecessor" meaning
**	gaining benefit from a predecessor effort with no or little addition
**	effort. The predecessor here being the preceding MX RR).
**
**	Parameters:
**		e -- the envelope to deliver.
**		firstto -- head of the address list to deliver to.
**
**	Returns:
**		zero -- successfully delivered.
**		else -- some failure, see ExitStat for more info.
**
**	Side Effects:
**		The standard input is passed off to someone.
*/

static int
deliver(e, firstto)
	register ENVELOPE *e;
	ADDRESS *firstto;
{
	char *host;			/* host being sent to */
	char *user;			/* user being sent to */
	char **pvp;
	register char **mvp;
	register char *p;
	register MAILER *m;		/* mailer for this recipient */
	ADDRESS *volatile ctladdr;
#if HASSETUSERCONTEXT
	ADDRESS *volatile contextaddr = NULL;
#endif /* HASSETUSERCONTEXT */
	register MCI *volatile mci;
	register ADDRESS *SM_NONVOLATILE to = firstto;
	volatile bool clever = false;	/* running user smtp to this mailer */
	ADDRESS *volatile tochain = NULL; /* users chain in this mailer call */
	int rcode;			/* response code */
	SM_NONVOLATILE int lmtp_rcode = EX_OK;
	SM_NONVOLATILE int nummxhosts = 0; /* number of MX hosts available */
	SM_NONVOLATILE int hostnum = 0;	/* current MX host index */
	char *firstsig;			/* signature of firstto */
	volatile pid_t pid = -1;
	char *volatile curhost;
	SM_NONVOLATILE unsigned short port = 0;
	SM_NONVOLATILE time_t enough = 0;
#if NETUNIX
	char *SM_NONVOLATILE mux_path = NULL;	/* path to UNIX domain socket */
#endif /* NETUNIX */
	time_t xstart;
	bool suidwarn;
	bool anyok;			/* at least one address was OK */
	SM_NONVOLATILE bool goodmxfound = false; /* at least one MX was OK */
	bool ovr;
	bool quarantine;
	int strsize;
	int rcptcount;
	int ret;
	static int tobufsize = 0;
	static char *tobuf = NULL;
	char *rpath;	/* translated return path */
	int mpvect[2];
	int rpvect[2];
	char *mxhosts[MAXMXHOSTS + 1];
	char *pv[MAXPV + 1];
	char buf[MAXNAME + 1];
	char cbuf[MAXPATHLEN];

	errno = 0;
	SM_REQUIRE(firstto != NULL);	/* same as to */
	if (!QS_IS_OK(to->q_state))
		return 0;

	suidwarn = geteuid() == 0;

	SM_REQUIRE(e != NULL);
	m = to->q_mailer;
	host = to->q_host;
	CurEnv = e;			/* just in case */
	e->e_statmsg = NULL;
	SmtpError[0] = '\0';
	xstart = curtime();

	if (tTd(10, 1))
		sm_dprintf("\n--deliver, id=%s, mailer=%s, host=`%s', first user=`%s'\n",
			e->e_id, m->m_name, host, to->q_user);
	if (tTd(10, 100))
		printopenfds(false);

	/*
	**  Clear {client_*} macros if this is a bounce message to
	**  prevent rejection by check_compat ruleset.
	*/

	if (bitset(EF_RESPONSE, e->e_flags))
	{
		macdefine(&e->e_macro, A_PERM, macid("{client_name}"), "");
		macdefine(&e->e_macro, A_PERM, macid("{client_ptr}"), "");
		macdefine(&e->e_macro, A_PERM, macid("{client_addr}"), "");
		macdefine(&e->e_macro, A_PERM, macid("{client_port}"), "");
		macdefine(&e->e_macro, A_PERM, macid("{client_resolve}"), "");
	}

	SM_TRY
	{
	ADDRESS *skip_back = NULL;

	/*
	**  Do initial argv setup.
	**	Insert the mailer name.  Notice that $x expansion is
	**	NOT done on the mailer name.  Then, if the mailer has
	**	a picky -f flag, we insert it as appropriate.  This
	**	code does not check for 'pv' overflow; this places a
	**	manifest lower limit of 4 for MAXPV.
	**		The from address rewrite is expected to make
	**		the address relative to the other end.
	*/

	/* rewrite from address, using rewriting rules */
	rcode = EX_OK;
	SM_ASSERT(e->e_from.q_mailer != NULL);
	if (bitnset(M_UDBENVELOPE, e->e_from.q_mailer->m_flags))
		p = e->e_sender;
	else
		p = e->e_from.q_paddr;
	rpath = remotename(p, m, RF_SENDERADDR|RF_CANONICAL, &rcode, e);
	if (rcode != EX_OK && bitnset(M_xSMTP, m->m_flags))
		goto cleanup;
	if (strlen(rpath) > MAXNAME)
	{
		rpath = shortenstring(rpath, MAXSHORTSTR);

		/* avoid bogus errno */
		errno = 0;
		syserr("remotename: huge return path %s", rpath);
	}
	rpath = sm_rpool_strdup_x(e->e_rpool, rpath);
	macdefine(&e->e_macro, A_PERM, 'g', rpath);
	macdefine(&e->e_macro, A_PERM, 'h', host);
	Errors = 0;
	pvp = pv;
	*pvp++ = m->m_argv[0];

	/* ignore long term host status information if mailer flag W is set */
	if (bitnset(M_NOHOSTSTAT, m->m_flags))
		IgnoreHostStatus = true;

	/* insert -f or -r flag as appropriate */
	if (FromFlag &&
	    (bitnset(M_FOPT, m->m_flags) ||
	     bitnset(M_ROPT, m->m_flags)))
	{
		if (bitnset(M_FOPT, m->m_flags))
			*pvp++ = "-f";
		else
			*pvp++ = "-r";
		*pvp++ = rpath;
	}

	/*
	**  Append the other fixed parts of the argv.  These run
	**  up to the first entry containing "$u".  There can only
	**  be one of these, and there are only a few more slots
	**  in the pv after it.
	*/

	for (mvp = m->m_argv; (p = *++mvp) != NULL; )
	{
		/* can't use strchr here because of sign extension problems */
		while (*p != '\0')
		{
			if ((*p++ & 0377) == MACROEXPAND)
			{
				if (*p == 'u')
					break;
			}
		}

		if (*p != '\0')
			break;

		/* this entry is safe -- go ahead and process it */
		expand(*mvp, buf, sizeof(buf), e);
		*pvp++ = sm_rpool_strdup_x(e->e_rpool, buf);
		if (pvp >= &pv[MAXPV - 3])
		{
			syserr("554 5.3.5 Too many parameters to %s before $u",
			       pv[0]);
			rcode = -1;
			goto cleanup;
		}
	}

	/*
	**  If we have no substitution for the user name in the argument
	**  list, we know that we must supply the names otherwise -- and
	**  SMTP is the answer!!
	*/

	if (*mvp == NULL)
	{
		/* running LMTP or SMTP */
		clever = true;
		*pvp = NULL;
		setbitn(M_xSMTP, m->m_flags);
	}
	else if (bitnset(M_LMTP, m->m_flags))
	{
		/* not running LMTP */
		sm_syslog(LOG_ERR, NULL,
			  "Warning: mailer %s: LMTP flag (F=z) turned off",
			  m->m_name);
		clrbitn(M_LMTP, m->m_flags);
	}

	/*
	**  At this point *mvp points to the argument with $u.  We
	**  run through our address list and append all the addresses
	**  we can.  If we run out of space, do not fret!  We can
	**  always send another copy later.
	*/

	e->e_to = NULL;
	strsize = 2;
	rcptcount = 0;
	ctladdr = NULL;
	if (firstto->q_signature == NULL)
		firstto->q_signature = hostsignature(firstto->q_mailer,
						     firstto->q_host);
	firstsig = firstto->q_signature;

	for (; to != NULL; to = to->q_next)
	{
		/* avoid sending multiple recipients to dumb mailers */
		if (tochain != NULL && !bitnset(M_MUSER, m->m_flags))
			break;

		/* if already sent or not for this host, don't send */
		if (!QS_IS_OK(to->q_state)) /* already sent; look at next */
			continue;

		/*
		**  Must be same mailer to keep grouping rcpts.
		**  If mailers don't match: continue; sendqueue is not
		**  sorted by mailers, so don't break;
		*/

		if (to->q_mailer != firstto->q_mailer)
			continue;

		if (to->q_signature == NULL) /* for safety */
			to->q_signature = hostsignature(to->q_mailer,
							to->q_host);

		/*
		**  This is for coincidental and tailcoat piggybacking messages
		**  to the same mail host. While the signatures are identical
		**  (that's the MX-RR's are identical) we can do coincidental
		**  piggybacking. We try hard for coattail piggybacking
		**  with the same mail host when the next recipient has the
		**  same host at lowest preference. It may be that this
		**  won't work out, so 'skip_back' is maintained if a backup
		**  to coincidental piggybacking or full signature must happen.
		*/

		ret = firstto == to ? HS_MATCH_FULL :
				      coloncmp(to->q_signature, firstsig);
		if (ret == HS_MATCH_FULL)
			skip_back = to;
		else if (ret == HS_MATCH_NO)
			break;

		if (!clever)
		{
			/* avoid overflowing tobuf */
			strsize += strlen(to->q_paddr) + 1;
			if (strsize > TOBUFSIZE)
				break;
		}

		if (++rcptcount > to->q_mailer->m_maxrcpt)
			break;

		if (tTd(10, 1))
		{
			sm_dprintf("\nsend to ");
			printaddr(sm_debug_file(), to, false);
		}

		/* compute effective uid/gid when sending */
		if (bitnset(M_RUNASRCPT, to->q_mailer->m_flags))
# if HASSETUSERCONTEXT
			contextaddr = ctladdr = getctladdr(to);
# else /* HASSETUSERCONTEXT */
			ctladdr = getctladdr(to);
# endif /* HASSETUSERCONTEXT */

		if (tTd(10, 2))
		{
			sm_dprintf("ctladdr=");
			printaddr(sm_debug_file(), ctladdr, false);
		}

		user = to->q_user;
		e->e_to = to->q_paddr;

		/*
		**  Check to see that these people are allowed to
		**  talk to each other.
		**  Check also for overflow of e_msgsize.
		*/

		if (m->m_maxsize != 0 &&
		    (e->e_msgsize > m->m_maxsize || e->e_msgsize < 0))
		{
			e->e_flags |= EF_NO_BODY_RETN;
			if (bitnset(M_LOCALMAILER, to->q_mailer->m_flags))
				to->q_status = "5.2.3";
			else
				to->q_status = "5.3.4";

			/* set to->q_rstatus = NULL; or to the following? */
			usrerrenh(to->q_status,
				  "552 Message is too large; %ld bytes max",
				  m->m_maxsize);
			markfailure(e, to, NULL, EX_UNAVAILABLE, false);
			giveresponse(EX_UNAVAILABLE, to->q_status, m,
				     NULL, ctladdr, xstart, e, to);
			continue;
		}
		SM_SET_H_ERRNO(0);
		ovr = true;

		/* do config file checking of compatibility */
		quarantine = (e->e_quarmsg != NULL);
		rcode = rscheck("check_compat", e->e_from.q_paddr, to->q_paddr,
				e, RSF_RMCOMM|RSF_COUNT, 3, NULL,
				e->e_id, NULL, NULL);
		if (rcode == EX_OK)
		{
			/* do in-code checking if not discarding */
			if (!bitset(EF_DISCARD, e->e_flags))
			{
				rcode = checkcompat(to, e);
				ovr = false;
			}
		}
		if (rcode != EX_OK)
		{
			markfailure(e, to, NULL, rcode, ovr);
			giveresponse(rcode, to->q_status, m,
				     NULL, ctladdr, xstart, e, to);
			continue;
		}
		if (!quarantine && e->e_quarmsg != NULL)
		{
			/*
			**  check_compat or checkcompat() has tried
			**  to quarantine but that isn't supported.
			**  Revert the attempt.
			*/

			e->e_quarmsg = NULL;
			macdefine(&e->e_macro, A_PERM,
				  macid("{quarantine}"), "");
		}
		if (bitset(EF_DISCARD, e->e_flags))
		{
			if (tTd(10, 5))
			{
				sm_dprintf("deliver: discarding recipient ");
				printaddr(sm_debug_file(), to, false);
			}

			/* pretend the message was sent */
			/* XXX should we log something here? */
			to->q_state = QS_DISCARDED;

			/*
			**  Remove discard bit to prevent discard of
			**  future recipients.  This is safe because the
			**  true "global discard" has been handled before
			**  we get here.
			*/

			e->e_flags &= ~EF_DISCARD;
			continue;
		}

		/*
		**  Strip quote bits from names if the mailer is dumb
		**	about them.
		*/

		if (bitnset(M_STRIPQ, m->m_flags))
		{
			stripquotes(user);
			stripquotes(host);
		}

		/*
		**  Strip all leading backslashes if requested and the
		**  next character is alphanumerical (the latter can
		**  probably relaxed a bit, see RFC2821).
		*/

		if (bitnset(M_STRIPBACKSL, m->m_flags) && user[0] == '\\')
			stripbackslash(user);

		/* hack attack -- delivermail compatibility */
		if (m == ProgMailer && *user == '|')
			user++;

		/*
		**  If an error message has already been given, don't
		**	bother to send to this address.
		**
		**	>>>>>>>>>> This clause assumes that the local mailer
		**	>> NOTE >> cannot do any further aliasing; that
		**	>>>>>>>>>> function is subsumed by sendmail.
		*/

		if (!QS_IS_OK(to->q_state))
			continue;

		/*
		**  See if this user name is "special".
		**	If the user name has a slash in it, assume that this
		**	is a file -- send it off without further ado.  Note
		**	that this type of addresses is not processed along
		**	with the others, so we fudge on the To person.
		*/

		if (strcmp(m->m_mailer, "[FILE]") == 0)
		{
			macdefine(&e->e_macro, A_PERM, 'u', user);
			p = to->q_home;
			if (p == NULL && ctladdr != NULL)
				p = ctladdr->q_home;
			macdefine(&e->e_macro, A_PERM, 'z', p);
			expand(m->m_argv[1], buf, sizeof(buf), e);
			if (strlen(buf) > 0)
				rcode = mailfile(buf, m, ctladdr, SFF_CREAT, e);
			else
			{
				syserr("empty filename specification for mailer %s",
				       m->m_name);
				rcode = EX_CONFIG;
			}
			giveresponse(rcode, to->q_status, m, NULL,
				     ctladdr, xstart, e, to);
			markfailure(e, to, NULL, rcode, true);
			e->e_nsent++;
			if (rcode == EX_OK)
			{
				to->q_state = QS_SENT;
				if (bitnset(M_LOCALMAILER, m->m_flags) &&
				    bitset(QPINGONSUCCESS, to->q_flags))
				{
					to->q_flags |= QDELIVERED;
					to->q_status = "2.1.5";
					(void) sm_io_fprintf(e->e_xfp,
							     SM_TIME_DEFAULT,
							     "%s... Successfully delivered\n",
							     to->q_paddr);
				}
			}
			to->q_statdate = curtime();
			markstats(e, to, STATS_NORMAL);
			continue;
		}

		/*
		**  Address is verified -- add this user to mailer
		**  argv, and add it to the print list of recipients.
		*/

		/* link together the chain of recipients */
		to->q_tchain = tochain;
		tochain = to;
		e->e_to = "[CHAIN]";

		macdefine(&e->e_macro, A_PERM, 'u', user);  /* to user */
		p = to->q_home;
		if (p == NULL && ctladdr != NULL)
			p = ctladdr->q_home;
		macdefine(&e->e_macro, A_PERM, 'z', p);  /* user's home */

		/* set the ${dsn_notify} macro if applicable */
		if (bitset(QHASNOTIFY, to->q_flags))
		{
			char notify[MAXLINE];

			notify[0] = '\0';
			if (bitset(QPINGONSUCCESS, to->q_flags))
				(void) sm_strlcat(notify, "SUCCESS,",
						  sizeof(notify));
			if (bitset(QPINGONFAILURE, to->q_flags))
				(void) sm_strlcat(notify, "FAILURE,",
						  sizeof(notify));
			if (bitset(QPINGONDELAY, to->q_flags))
				(void) sm_strlcat(notify, "DELAY,",
						  sizeof(notify));

			/* Set to NEVER or drop trailing comma */
			if (notify[0] == '\0')
				(void) sm_strlcat(notify, "NEVER",
						  sizeof(notify));
			else
				notify[strlen(notify) - 1] = '\0';

			macdefine(&e->e_macro, A_TEMP,
				macid("{dsn_notify}"), notify);
		}
		else
			macdefine(&e->e_macro, A_PERM,
				macid("{dsn_notify}"), NULL);

		/*
		**  Expand out this user into argument list.
		*/

		if (!clever)
		{
			expand(*mvp, buf, sizeof(buf), e);
			*pvp++ = sm_rpool_strdup_x(e->e_rpool, buf);
			if (pvp >= &pv[MAXPV - 2])
			{
				/* allow some space for trailing parms */
				break;
			}
		}
	}

	/* see if any addresses still exist */
	if (tochain == NULL)
	{
		rcode = 0;
		goto cleanup;
	}

	/* print out messages as full list */
	strsize = 1;
	for (to = tochain; to != NULL; to = to->q_tchain)
		strsize += strlen(to->q_paddr) + 1;
	if (strsize < TOBUFSIZE)
		strsize = TOBUFSIZE;
	if (strsize > tobufsize)
	{
		SM_FREE_CLR(tobuf);
		tobuf = sm_pmalloc_x(strsize);
		tobufsize = strsize;
	}
	p = tobuf;
	*p = '\0';
	for (to = tochain; to != NULL; to = to->q_tchain)
	{
		(void) sm_strlcpyn(p, tobufsize - (p - tobuf), 2,
				   ",", to->q_paddr);
		p += strlen(p);
	}
	e->e_to = tobuf + 1;

	/*
	**  Fill out any parameters after the $u parameter.
	*/

	if (!clever)
	{
		while (*++mvp != NULL)
		{
			expand(*mvp, buf, sizeof(buf), e);
			*pvp++ = sm_rpool_strdup_x(e->e_rpool, buf);
			if (pvp >= &pv[MAXPV])
				syserr("554 5.3.0 deliver: pv overflow after $u for %s",
				       pv[0]);
		}
	}
	*pvp++ = NULL;

	/*
	**  Call the mailer.
	**	The argument vector gets built, pipes
	**	are created as necessary, and we fork & exec as
	**	appropriate.
	**	If we are running SMTP, we just need to clean up.
	*/

	/* XXX this seems a bit weird */
	if (ctladdr == NULL && m != ProgMailer && m != FileMailer &&
	    bitset(QGOODUID, e->e_from.q_flags))
		ctladdr = &e->e_from;

#if NAMED_BIND
	if (ConfigLevel < 2)
		_res.options &= ~(RES_DEFNAMES | RES_DNSRCH);	/* XXX */
#endif /* NAMED_BIND */

	if (tTd(11, 1))
	{
		sm_dprintf("openmailer:");
		printav(sm_debug_file(), pv);
	}
	errno = 0;
	SM_SET_H_ERRNO(0);
	CurHostName = NULL;

	/*
	**  Deal with the special case of mail handled through an IPC
	**  connection.
	**	In this case we don't actually fork.  We must be
	**	running SMTP for this to work.  We will return a
	**	zero pid to indicate that we are running IPC.
	**  We also handle a debug version that just talks to stdin/out.
	*/

	curhost = NULL;
	SmtpPhase = NULL;
	mci = NULL;

#if XDEBUG
	{
		char wbuf[MAXLINE];

		/* make absolutely certain 0, 1, and 2 are in use */
		(void) sm_snprintf(wbuf, sizeof(wbuf), "%s... openmailer(%s)",
				   shortenstring(e->e_to, MAXSHORTSTR),
				   m->m_name);
		checkfd012(wbuf);
	}
#endif /* XDEBUG */

	/* check for 8-bit available */
	if (bitset(EF_HAS8BIT, e->e_flags) &&
	    bitnset(M_7BITS, m->m_flags) &&
	    (bitset(EF_DONT_MIME, e->e_flags) ||
	     !(bitset(MM_MIME8BIT, MimeMode) ||
	       (bitset(EF_IS_MIME, e->e_flags) &&
		bitset(MM_CVTMIME, MimeMode)))))
	{
		e->e_status = "5.6.3";
		usrerrenh(e->e_status,
			  "554 Cannot send 8-bit data to 7-bit destination");
		rcode = EX_DATAERR;
		goto give_up;
	}

	if (tTd(62, 8))
		checkfds("before delivery");

	/* check for Local Person Communication -- not for mortals!!! */
	if (strcmp(m->m_mailer, "[LPC]") == 0)
	{
		if (clever)
		{
			/* flush any expired connections */
			(void) mci_scan(NULL);

			/* try to get a cached connection or just a slot */
			mci = mci_get(m->m_name, m);
			if (mci->mci_host == NULL)
				mci->mci_host = m->m_name;
			CurHostName = mci->mci_host;
			if (mci->mci_state != MCIS_CLOSED)
			{
				message("Using cached SMTP/LPC connection for %s...",
					m->m_name);
				mci->mci_deliveries++;
				goto do_transfer;
			}
		}
		else
		{
			mci = mci_new(e->e_rpool);
		}
		mci->mci_in = smioin;
		mci->mci_out = smioout;
		mci->mci_mailer = m;
		mci->mci_host = m->m_name;
		if (clever)
		{
			mci->mci_state = MCIS_OPENING;
			mci_cache(mci);
		}
		else
			mci->mci_state = MCIS_OPEN;
	}
	else if (strcmp(m->m_mailer, "[IPC]") == 0)
	{
		register int i;

		if (pv[0] == NULL || pv[1] == NULL || pv[1][0] == '\0')
		{
			syserr("null destination for %s mailer", m->m_mailer);
			rcode = EX_CONFIG;
			goto give_up;
		}

# if NETUNIX
		if (strcmp(pv[0], "FILE") == 0)
		{
			curhost = CurHostName = "localhost";
			mux_path = pv[1];
		}
		else
# endif /* NETUNIX */
		{
			CurHostName = pv[1];
			curhost = hostsignature(m, pv[1]);
		}

		if (curhost == NULL || curhost[0] == '\0')
		{
			syserr("null host signature for %s", pv[1]);
			rcode = EX_CONFIG;
			goto give_up;
		}

		if (!clever)
		{
			syserr("554 5.3.5 non-clever IPC");
			rcode = EX_CONFIG;
			goto give_up;
		}
		if (pv[2] != NULL
# if NETUNIX
		    && mux_path == NULL
# endif /* NETUNIX */
		    )
		{
			port = htons((unsigned short) atoi(pv[2]));
			if (port == 0)
			{
# ifdef NO_GETSERVBYNAME
				syserr("Invalid port number: %s", pv[2]);
# else /* NO_GETSERVBYNAME */
				struct servent *sp = getservbyname(pv[2], "tcp");

				if (sp == NULL)
					syserr("Service %s unknown", pv[2]);
				else
					port = sp->s_port;
# endif /* NO_GETSERVBYNAME */
			}
		}

		nummxhosts = parse_hostsignature(curhost, mxhosts, m);
		if (TimeOuts.to_aconnect > 0)
			enough = curtime() + TimeOuts.to_aconnect;
tryhost:
		while (hostnum < nummxhosts)
		{
			char sep = ':';
			char *endp;
			static char hostbuf[MAXNAME + 1];
			bool tried_fallbacksmarthost = false;

# if NETINET6
			if (*mxhosts[hostnum] == '[')
			{
				endp = strchr(mxhosts[hostnum] + 1, ']');
				if (endp != NULL)
					endp = strpbrk(endp + 1, ":,");
			}
			else
				endp = strpbrk(mxhosts[hostnum], ":,");
# else /* NETINET6 */
			endp = strpbrk(mxhosts[hostnum], ":,");
# endif /* NETINET6 */
			if (endp != NULL)
			{
				sep = *endp;
				*endp = '\0';
			}

			if (hostnum == 1 && skip_back != NULL)
			{
				/*
				**  Coattail piggybacking is no longer an
				**  option with the mail host next to be tried
				**  no longer the lowest MX preference
				**  (hostnum == 1 meaning we're on the second
				**  preference). We do not try to coattail
				**  piggyback more than the first MX preference.
				**  Revert 'tochain' to last location for
				**  coincidental piggybacking. This works this
				**  easily because the q_tchain kept getting
				**  added to the top of the linked list.
				*/

				tochain = skip_back;
			}

			if (*mxhosts[hostnum] == '\0')
			{
				syserr("deliver: null host name in signature");
				hostnum++;
				if (endp != NULL)
					*endp = sep;
				continue;
			}
			(void) sm_strlcpy(hostbuf, mxhosts[hostnum],
					  sizeof(hostbuf));
			hostnum++;
			if (endp != NULL)
				*endp = sep;

  one_last_try:
			/* see if we already know that this host is fried */
			CurHostName = hostbuf;
			mci = mci_get(hostbuf, m);
			if (mci->mci_state != MCIS_CLOSED)
			{
				char *type;

				if (tTd(11, 1))
				{
					sm_dprintf("openmailer: ");
					mci_dump(sm_debug_file(), mci, false);
				}
				CurHostName = mci->mci_host;
				if (bitnset(M_LMTP, m->m_flags))
					type = "L";
				else if (bitset(MCIF_ESMTP, mci->mci_flags))
					type = "ES";
				else
					type = "S";
				message("Using cached %sMTP connection to %s via %s...",
					type, hostbuf, m->m_name);
				mci->mci_deliveries++;
				break;
			}
			mci->mci_mailer = m;
			if (mci->mci_exitstat != EX_OK)
			{
				if (mci->mci_exitstat == EX_TEMPFAIL)
					goodmxfound = true;

				/* Try FallbackSmartHost? */
				if (should_try_fbsh(e, &tried_fallbacksmarthost,
						    hostbuf, sizeof(hostbuf),
						    mci->mci_exitstat))
					goto one_last_try;

				continue;
			}

			if (mci_lock_host(mci) != EX_OK)
			{
				mci_setstat(mci, EX_TEMPFAIL, "4.4.5", NULL);
				goodmxfound = true;
				continue;
			}

			/* try the connection */
			sm_setproctitle(true, e, "%s %s: %s",
					qid_printname(e),
					hostbuf, "user open");
# if NETUNIX
			if (mux_path != NULL)
			{
				message("Connecting to %s via %s...",
					mux_path, m->m_name);
				i = makeconnection_ds((char *) mux_path, mci);
			}
			else
# endif /* NETUNIX */
			{
				if (port == 0)
					message("Connecting to %s via %s...",
						hostbuf, m->m_name);
				else
					message("Connecting to %s port %d via %s...",
						hostbuf, ntohs(port),
						m->m_name);
				i = makeconnection(hostbuf, port, mci, e,
						   enough);
			}
			mci->mci_errno = errno;
			mci->mci_lastuse = curtime();
			mci->mci_deliveries = 0;
			mci->mci_exitstat = i;
			mci_clr_extensions(mci);
# if NAMED_BIND
			mci->mci_herrno = h_errno;
# endif /* NAMED_BIND */

			/*
			**  Have we tried long enough to get a connection?
			**	If yes, skip to the fallback MX hosts
			**	(if existent).
			*/

			if (enough > 0 && mci->mci_lastuse >= enough)
			{
				int h;
# if NAMED_BIND
				extern int NumFallbackMXHosts;
# else /* NAMED_BIND */
				const int NumFallbackMXHosts = 0;
# endif /* NAMED_BIND */

				if (hostnum < nummxhosts && LogLevel > 9)
					sm_syslog(LOG_INFO, e->e_id,
						  "Timeout.to_aconnect occurred before exhausting all addresses");

				/* turn off timeout if fallback available */
				if (NumFallbackMXHosts > 0)
					enough = 0;

				/* skip to a fallback MX host */
				h = nummxhosts - NumFallbackMXHosts;
				if (hostnum < h)
					hostnum = h;
			}
			if (i == EX_OK)
			{
				goodmxfound = true;
				markstats(e, firstto, STATS_CONNECT);
				mci->mci_state = MCIS_OPENING;
				mci_cache(mci);
				if (TrafficLogFile != NULL)
					(void) sm_io_fprintf(TrafficLogFile,
							     SM_TIME_DEFAULT,
							     "%05d === CONNECT %s\n",
							     (int) CurrentPid,
							     hostbuf);
				break;
			}
			else
			{
				/* Try FallbackSmartHost? */
				if (should_try_fbsh(e, &tried_fallbacksmarthost,
						    hostbuf, sizeof(hostbuf), i))
					goto one_last_try;

				if (tTd(11, 1))
					sm_dprintf("openmailer: makeconnection => stat=%d, errno=%d\n",
						   i, errno);
				if (i == EX_TEMPFAIL)
					goodmxfound = true;
				mci_unlock_host(mci);
			}

			/* enter status of this host */
			setstat(i);

			/* should print some message here for -v mode */
		}
		if (mci == NULL)
		{
			syserr("deliver: no host name");
			rcode = EX_SOFTWARE;
			goto give_up;
		}
		mci->mci_pid = 0;
	}
	else
	{
		/* flush any expired connections */
		(void) mci_scan(NULL);
		mci = NULL;

		if (bitnset(M_LMTP, m->m_flags))
		{
			/* try to get a cached connection */
			mci = mci_get(m->m_name, m);
			if (mci->mci_host == NULL)
				mci->mci_host = m->m_name;
			CurHostName = mci->mci_host;
			if (mci->mci_state != MCIS_CLOSED)
			{
				message("Using cached LMTP connection for %s...",
					m->m_name);
				mci->mci_deliveries++;
				goto do_transfer;
			}
		}

		/* announce the connection to verbose listeners */
		if (host == NULL || host[0] == '\0')
			message("Connecting to %s...", m->m_name);
		else
			message("Connecting to %s via %s...", host, m->m_name);
		if (TrafficLogFile != NULL)
		{
			char **av;

			(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
					     "%05d === EXEC", (int) CurrentPid);
			for (av = pv; *av != NULL; av++)
				(void) sm_io_fprintf(TrafficLogFile,
						     SM_TIME_DEFAULT, " %s",
						     *av);
			(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
					     "\n");
		}

#if XDEBUG
		checkfd012("before creating mail pipe");
#endif /* XDEBUG */

		/* create a pipe to shove the mail through */
		if (pipe(mpvect) < 0)
		{
			syserr("%s... openmailer(%s): pipe (to mailer)",
			       shortenstring(e->e_to, MAXSHORTSTR), m->m_name);
			if (tTd(11, 1))
				sm_dprintf("openmailer: NULL\n");
			rcode = EX_OSERR;
			goto give_up;
		}

#if XDEBUG
		/* make sure we didn't get one of the standard I/O files */
		if (mpvect[0] < 3 || mpvect[1] < 3)
		{
			syserr("%s... openmailer(%s): bogus mpvect %d %d",
			       shortenstring(e->e_to, MAXSHORTSTR), m->m_name,
			       mpvect[0], mpvect[1]);
			printopenfds(true);
			if (tTd(11, 1))
				sm_dprintf("openmailer: NULL\n");
			rcode = EX_OSERR;
			goto give_up;
		}

		/* make sure system call isn't dead meat */
		checkfdopen(mpvect[0], "mpvect[0]");
		checkfdopen(mpvect[1], "mpvect[1]");
		if (mpvect[0] == mpvect[1] ||
		    (e->e_lockfp != NULL &&
		     (mpvect[0] == sm_io_getinfo(e->e_lockfp, SM_IO_WHAT_FD,
						 NULL) ||
		      mpvect[1] == sm_io_getinfo(e->e_lockfp, SM_IO_WHAT_FD,
						 NULL))))
		{
			if (e->e_lockfp == NULL)
				syserr("%s... openmailer(%s): overlapping mpvect %d %d",
				       shortenstring(e->e_to, MAXSHORTSTR),
				       m->m_name, mpvect[0], mpvect[1]);
			else
				syserr("%s... openmailer(%s): overlapping mpvect %d %d, lockfp = %d",
				       shortenstring(e->e_to, MAXSHORTSTR),
				       m->m_name, mpvect[0], mpvect[1],
				       sm_io_getinfo(e->e_lockfp,
						     SM_IO_WHAT_FD, NULL));
		}
#endif /* XDEBUG */

		/* create a return pipe */
		if (pipe(rpvect) < 0)
		{
			syserr("%s... openmailer(%s): pipe (from mailer)",
			       shortenstring(e->e_to, MAXSHORTSTR),
			       m->m_name);
			(void) close(mpvect[0]);
			(void) close(mpvect[1]);
			if (tTd(11, 1))
				sm_dprintf("openmailer: NULL\n");
			rcode = EX_OSERR;
			goto give_up;
		}
#if XDEBUG
		checkfdopen(rpvect[0], "rpvect[0]");
		checkfdopen(rpvect[1], "rpvect[1]");
#endif /* XDEBUG */

		/*
		**  Actually fork the mailer process.
		**	DOFORK is clever about retrying.
		**
		**	Dispose of SIGCHLD signal catchers that may be laying
		**	around so that endmailer will get it.
		*/

		if (e->e_xfp != NULL)	/* for debugging */
			(void) sm_io_flush(e->e_xfp, SM_TIME_DEFAULT);
		(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
		(void) sm_signal(SIGCHLD, SIG_DFL);


		DOFORK(FORK);
		/* pid is set by DOFORK */

		if (pid < 0)
		{
			/* failure */
			syserr("%s... openmailer(%s): cannot fork",
			       shortenstring(e->e_to, MAXSHORTSTR), m->m_name);
			(void) close(mpvect[0]);
			(void) close(mpvect[1]);
			(void) close(rpvect[0]);
			(void) close(rpvect[1]);
			if (tTd(11, 1))
				sm_dprintf("openmailer: NULL\n");
			rcode = EX_OSERR;
			goto give_up;
		}
		else if (pid == 0)
		{
			int save_errno;
			int sff;
			int new_euid = NO_UID;
			int new_ruid = NO_UID;
			int new_gid = NO_GID;
			char *user = NULL;
			struct stat stb;
			extern int DtableSize;

			CurrentPid = getpid();

			/* clear the events to turn off SIGALRMs */
			sm_clear_events();

			/* Reset global flags */
			RestartRequest = NULL;
			RestartWorkGroup = false;
			ShutdownRequest = NULL;
			PendingSignal = 0;

			if (e->e_lockfp != NULL)
				(void) close(sm_io_getinfo(e->e_lockfp,
							   SM_IO_WHAT_FD,
							   NULL));

			/* child -- set up input & exec mailer */
			(void) sm_signal(SIGALRM, sm_signal_noop);
			(void) sm_signal(SIGCHLD, SIG_DFL);
			(void) sm_signal(SIGHUP, SIG_IGN);
			(void) sm_signal(SIGINT, SIG_IGN);
			(void) sm_signal(SIGTERM, SIG_DFL);
# ifdef SIGUSR1
			(void) sm_signal(SIGUSR1, sm_signal_noop);
# endif /* SIGUSR1 */

			if (m != FileMailer || stat(tochain->q_user, &stb) < 0)
				stb.st_mode = 0;

# if HASSETUSERCONTEXT
			/*
			**  Set user resources.
			*/

			if (contextaddr != NULL)
			{
				int sucflags;
				struct passwd *pwd;

				if (contextaddr->q_ruser != NULL)
					pwd = sm_getpwnam(contextaddr->q_ruser);
				else
					pwd = sm_getpwnam(contextaddr->q_user);
				sucflags = LOGIN_SETRESOURCES|LOGIN_SETPRIORITY;
#ifdef LOGIN_SETCPUMASK
				sucflags |= LOGIN_SETCPUMASK;
#endif /* LOGIN_SETCPUMASK */
#ifdef LOGIN_SETLOGINCLASS
				sucflags |= LOGIN_SETLOGINCLASS;
#endif /* LOGIN_SETLOGINCLASS */
#ifdef LOGIN_SETMAC
				sucflags |= LOGIN_SETMAC;
#endif /* LOGIN_SETMAC */
				if (pwd != NULL &&
				    setusercontext(NULL, pwd, pwd->pw_uid,
						   sucflags) == -1 &&
				    suidwarn)
				{
					syserr("openmailer: setusercontext() failed");
					exit(EX_TEMPFAIL);
				}
			}
# endif /* HASSETUSERCONTEXT */

#if HASNICE
			/* tweak niceness */
			if (m->m_nice != 0)
				(void) nice(m->m_nice);
#endif /* HASNICE */

			/* reset group id */
			if (bitnset(M_SPECIFIC_UID, m->m_flags))
			{
				if (m->m_gid == NO_GID)
					new_gid = RunAsGid;
				else
					new_gid = m->m_gid;
			}
			else if (bitset(S_ISGID, stb.st_mode))
				new_gid = stb.st_gid;
			else if (ctladdr != NULL && ctladdr->q_gid != 0)
			{
				if (!DontInitGroups)
				{
					user = ctladdr->q_ruser;
					if (user == NULL)
						user = ctladdr->q_user;

					if (initgroups(user,
						       ctladdr->q_gid) == -1
					    && suidwarn)
					{
						syserr("openmailer: initgroups(%s, %ld) failed",
							user, (long) ctladdr->q_gid);
						exit(EX_TEMPFAIL);
					}
				}
				else
				{
					GIDSET_T gidset[1];

					gidset[0] = ctladdr->q_gid;
					if (setgroups(1, gidset) == -1
					    && suidwarn)
					{
						syserr("openmailer: setgroups() failed");
						exit(EX_TEMPFAIL);
					}
				}
				new_gid = ctladdr->q_gid;
			}
			else
			{
				if (!DontInitGroups)
				{
					user = DefUser;
					if (initgroups(DefUser, DefGid) == -1 &&
					    suidwarn)
					{
						syserr("openmailer: initgroups(%s, %ld) failed",
						       DefUser, (long) DefGid);
						exit(EX_TEMPFAIL);
					}
				}
				else
				{
					GIDSET_T gidset[1];

					gidset[0] = DefGid;
					if (setgroups(1, gidset) == -1
					    && suidwarn)
					{
						syserr("openmailer: setgroups() failed");
						exit(EX_TEMPFAIL);
					}
				}
				if (m->m_gid == NO_GID)
					new_gid = DefGid;
				else
					new_gid = m->m_gid;
			}
			if (new_gid != NO_GID)
			{
				if (RunAsUid != 0 &&
				    bitnset(M_SPECIFIC_UID, m->m_flags) &&
				    new_gid != getgid() &&
				    new_gid != getegid())
				{
					/* Only root can change the gid */
					syserr("openmailer: insufficient privileges to change gid, RunAsUid=%ld, new_gid=%ld, gid=%ld, egid=%ld",
					       (long) RunAsUid, (long) new_gid,
					       (long) getgid(), (long) getegid());
					exit(EX_TEMPFAIL);
				}

				if (setgid(new_gid) < 0 && suidwarn)
				{
					syserr("openmailer: setgid(%ld) failed",
					       (long) new_gid);
					exit(EX_TEMPFAIL);
				}
			}

			/* change root to some "safe" directory */
			if (m->m_rootdir != NULL)
			{
				expand(m->m_rootdir, cbuf, sizeof(cbuf), e);
				if (tTd(11, 20))
					sm_dprintf("openmailer: chroot %s\n",
						   cbuf);
				if (chroot(cbuf) < 0)
				{
					syserr("openmailer: Cannot chroot(%s)",
					       cbuf);
					exit(EX_TEMPFAIL);
				}
				if (chdir("/") < 0)
				{
					syserr("openmailer: cannot chdir(/)");
					exit(EX_TEMPFAIL);
				}
			}

			/* reset user id */
			endpwent();
			sm_mbdb_terminate();
			if (bitnset(M_SPECIFIC_UID, m->m_flags))
			{
				if (m->m_uid == NO_UID)
					new_euid = RunAsUid;
				else
					new_euid = m->m_uid;

				/*
				**  Undo the effects of the uid change in main
				**  for signal handling.  The real uid may
				**  be used by mailer in adding a "From "
				**  line.
				*/

				if (RealUid != 0 && RealUid != getuid())
				{
# if MAILER_SETUID_METHOD == USE_SETEUID
#  if HASSETREUID
					if (setreuid(RealUid, geteuid()) < 0)
					{
						syserr("openmailer: setreuid(%d, %d) failed",
						       (int) RealUid, (int) geteuid());
						exit(EX_OSERR);
					}
#  endif /* HASSETREUID */
# endif /* MAILER_SETUID_METHOD == USE_SETEUID */
# if MAILER_SETUID_METHOD == USE_SETREUID
					new_ruid = RealUid;
# endif /* MAILER_SETUID_METHOD == USE_SETREUID */
				}
			}
			else if (bitset(S_ISUID, stb.st_mode))
				new_ruid = stb.st_uid;
			else if (ctladdr != NULL && ctladdr->q_uid != 0)
				new_ruid = ctladdr->q_uid;
			else if (m->m_uid != NO_UID)
				new_ruid = m->m_uid;
			else
				new_ruid = DefUid;

# if _FFR_USE_SETLOGIN
			/* run disconnected from terminal and set login name */
			if (setsid() >= 0 &&
			    ctladdr != NULL && ctladdr->q_uid != 0 &&
			    new_euid == ctladdr->q_uid)
			{
				struct passwd *pwd;

				pwd = sm_getpwuid(ctladdr->q_uid);
				if (pwd != NULL && suidwarn)
					(void) setlogin(pwd->pw_name);
				endpwent();
			}
# endif /* _FFR_USE_SETLOGIN */

			if (new_euid != NO_UID)
			{
				if (RunAsUid != 0 && new_euid != RunAsUid)
				{
					/* Only root can change the uid */
					syserr("openmailer: insufficient privileges to change uid, new_euid=%ld, RunAsUid=%ld",
					       (long) new_euid, (long) RunAsUid);
					exit(EX_TEMPFAIL);
				}

				vendor_set_uid(new_euid);
# if MAILER_SETUID_METHOD == USE_SETEUID
				if (seteuid(new_euid) < 0 && suidwarn)
				{
					syserr("openmailer: seteuid(%ld) failed",
					       (long) new_euid);
					exit(EX_TEMPFAIL);
				}
# endif /* MAILER_SETUID_METHOD == USE_SETEUID */
# if MAILER_SETUID_METHOD == USE_SETREUID
				if (setreuid(new_ruid, new_euid) < 0 && suidwarn)
				{
					syserr("openmailer: setreuid(%ld, %ld) failed",
					       (long) new_ruid, (long) new_euid);
					exit(EX_TEMPFAIL);
				}
# endif /* MAILER_SETUID_METHOD == USE_SETREUID */
# if MAILER_SETUID_METHOD == USE_SETUID
				if (new_euid != geteuid() && setuid(new_euid) < 0 && suidwarn)
				{
					syserr("openmailer: setuid(%ld) failed",
					       (long) new_euid);
					exit(EX_TEMPFAIL);
				}
# endif /* MAILER_SETUID_METHOD == USE_SETUID */
			}
			else if (new_ruid != NO_UID)
			{
				vendor_set_uid(new_ruid);
				if (setuid(new_ruid) < 0 && suidwarn)
				{
					syserr("openmailer: setuid(%ld) failed",
					       (long) new_ruid);
					exit(EX_TEMPFAIL);
				}
			}

			if (tTd(11, 2))
				sm_dprintf("openmailer: running as r/euid=%ld/%ld, r/egid=%ld/%ld\n",
					   (long) getuid(), (long) geteuid(),
					   (long) getgid(), (long) getegid());

			/* move into some "safe" directory */
			if (m->m_execdir != NULL)
			{
				char *q;

				for (p = m->m_execdir; p != NULL; p = q)
				{
					q = strchr(p, ':');
					if (q != NULL)
						*q = '\0';
					expand(p, cbuf, sizeof(cbuf), e);
					if (q != NULL)
						*q++ = ':';
					if (tTd(11, 20))
						sm_dprintf("openmailer: trydir %s\n",
							   cbuf);
					if (cbuf[0] != '\0' &&
					    chdir(cbuf) >= 0)
						break;
				}
			}

			/* Check safety of program to be run */
			sff = SFF_ROOTOK|SFF_EXECOK;
			if (!bitnset(DBS_RUNWRITABLEPROGRAM,
				     DontBlameSendmail))
				sff |= SFF_NOGWFILES|SFF_NOWWFILES;
			if (bitnset(DBS_RUNPROGRAMINUNSAFEDIRPATH,
				    DontBlameSendmail))
				sff |= SFF_NOPATHCHECK;
			else
				sff |= SFF_SAFEDIRPATH;
			ret = safefile(m->m_mailer, getuid(), getgid(),
				       user, sff, 0, NULL);
			if (ret != 0)
				sm_syslog(LOG_INFO, e->e_id,
					  "Warning: program %s unsafe: %s",
					  m->m_mailer, sm_errstring(ret));

			/* arrange to filter std & diag output of command */
			(void) close(rpvect[0]);
			if (dup2(rpvect[1], STDOUT_FILENO) < 0)
			{
				syserr("%s... openmailer(%s): cannot dup pipe %d for stdout",
				       shortenstring(e->e_to, MAXSHORTSTR),
				       m->m_name, rpvect[1]);
				_exit(EX_OSERR);
			}
			(void) close(rpvect[1]);

			if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
			{
				syserr("%s... openmailer(%s): cannot dup stdout for stderr",
				       shortenstring(e->e_to, MAXSHORTSTR),
				       m->m_name);
				_exit(EX_OSERR);
			}

			/* arrange to get standard input */
			(void) close(mpvect[1]);
			if (dup2(mpvect[0], STDIN_FILENO) < 0)
			{
				syserr("%s... openmailer(%s): cannot dup pipe %d for stdin",
				       shortenstring(e->e_to, MAXSHORTSTR),
				       m->m_name, mpvect[0]);
				_exit(EX_OSERR);
			}
			(void) close(mpvect[0]);

			/* arrange for all the files to be closed */
			sm_close_on_exec(STDERR_FILENO + 1, DtableSize);

# if !_FFR_USE_SETLOGIN
			/* run disconnected from terminal */
			(void) setsid();
# endif /* !_FFR_USE_SETLOGIN */

			/* try to execute the mailer */
			(void) execve(m->m_mailer, (ARGV_T) pv,
				      (ARGV_T) UserEnviron);
			save_errno = errno;
			syserr("Cannot exec %s", m->m_mailer);
			if (bitnset(M_LOCALMAILER, m->m_flags) ||
			    transienterror(save_errno))
				_exit(EX_OSERR);
			_exit(EX_UNAVAILABLE);
		}

		/*
		**  Set up return value.
		*/

		if (mci == NULL)
		{
			if (clever)
			{
				/*
				**  Allocate from general heap, not
				**  envelope rpool, because this mci
				**  is going to be cached.
				*/

				mci = mci_new(NULL);
			}
			else
			{
				/*
				**  Prevent a storage leak by allocating
				**  this from the envelope rpool.
				*/

				mci = mci_new(e->e_rpool);
			}
		}
		mci->mci_mailer = m;
		if (clever)
		{
			mci->mci_state = MCIS_OPENING;
			mci_cache(mci);
		}
		else
		{
			mci->mci_state = MCIS_OPEN;
		}
		mci->mci_pid = pid;
		(void) close(mpvect[0]);
		mci->mci_out = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
					  (void *) &(mpvect[1]), SM_IO_WRONLY_B,
					  NULL);
		if (mci->mci_out == NULL)
		{
			syserr("deliver: cannot create mailer output channel, fd=%d",
			       mpvect[1]);
			(void) close(mpvect[1]);
			(void) close(rpvect[0]);
			(void) close(rpvect[1]);
			rcode = EX_OSERR;
			goto give_up;
		}

		(void) close(rpvect[1]);
		mci->mci_in = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
					 (void *) &(rpvect[0]), SM_IO_RDONLY_B,
					 NULL);
		if (mci->mci_in == NULL)
		{
			syserr("deliver: cannot create mailer input channel, fd=%d",
			       mpvect[1]);
			(void) close(rpvect[0]);
			(void) sm_io_close(mci->mci_out, SM_TIME_DEFAULT);
			mci->mci_out = NULL;
			rcode = EX_OSERR;
			goto give_up;
		}
	}

	/*
	**  If we are in SMTP opening state, send initial protocol.
	*/

	if (bitnset(M_7BITS, m->m_flags) &&
	    (!clever || mci->mci_state == MCIS_OPENING))
		mci->mci_flags |= MCIF_7BIT;
	if (clever && mci->mci_state != MCIS_CLOSED)
	{
# if STARTTLS || SASL
		int dotpos;
		char *srvname;
		extern SOCKADDR CurHostAddr;
# endif /* STARTTLS || SASL */

# if SASL
#  define DONE_AUTH(f)		bitset(MCIF_AUTHACT, f)
# endif /* SASL */
# if STARTTLS
#  define DONE_STARTTLS(f)	bitset(MCIF_TLSACT, f)
# endif /* STARTTLS */
# define ONLY_HELO(f)		bitset(MCIF_ONLY_EHLO, f)
# define SET_HELO(f)		f |= MCIF_ONLY_EHLO
# define CLR_HELO(f)		f &= ~MCIF_ONLY_EHLO

# if STARTTLS || SASL
		/* don't use CurHostName, it is changed in many places */
		if (mci->mci_host != NULL)
		{
			srvname = mci->mci_host;
			dotpos = strlen(srvname) - 1;
			if (dotpos >= 0)
			{
				if (srvname[dotpos] == '.')
					srvname[dotpos] = '\0';
				else
					dotpos = -1;
			}
		}
		else if (mci->mci_mailer != NULL)
		{
			srvname = mci->mci_mailer->m_name;
			dotpos = -1;
		}
		else
		{
			srvname = "local";
			dotpos = -1;
		}

		/* don't set {server_name} to NULL or "": see getauth() */
		macdefine(&mci->mci_macro, A_TEMP, macid("{server_name}"),
			  srvname);

		/* CurHostAddr is set by makeconnection() and mci_get() */
		if (CurHostAddr.sa.sa_family != 0)
		{
			macdefine(&mci->mci_macro, A_TEMP,
				  macid("{server_addr}"),
				  anynet_ntoa(&CurHostAddr));
		}
		else if (mci->mci_mailer != NULL)
		{
			/* mailer name is unique, use it as address */
			macdefine(&mci->mci_macro, A_PERM,
				  macid("{server_addr}"),
				  mci->mci_mailer->m_name);
		}
		else
		{
			/* don't set it to NULL or "": see getauth() */
			macdefine(&mci->mci_macro, A_PERM,
				  macid("{server_addr}"), "0");
		}

		/* undo change of srvname (mci->mci_host) */
		if (dotpos >= 0)
			srvname[dotpos] = '.';

reconnect:	/* after switching to an encrypted connection */
# endif /* STARTTLS || SASL */

		/* set the current connection information */
		e->e_mci = mci;
# if SASL
		mci->mci_saslcap = NULL;
# endif /* SASL */
		smtpinit(m, mci, e, ONLY_HELO(mci->mci_flags));
		CLR_HELO(mci->mci_flags);

		if (IS_DLVR_RETURN(e))
		{
			/*
			**  Check whether other side can deliver e-mail
			**  fast enough
			*/

			if (!bitset(MCIF_DLVR_BY, mci->mci_flags))
			{
				e->e_status = "5.4.7";
				usrerrenh(e->e_status,
					  "554 Server does not support Deliver By");
				rcode = EX_UNAVAILABLE;
				goto give_up;
			}
			if (e->e_deliver_by > 0 &&
			    e->e_deliver_by - (curtime() - e->e_ctime) <
			    mci->mci_min_by)
			{
				e->e_status = "5.4.7";
				usrerrenh(e->e_status,
					  "554 Message can't be delivered in time; %ld < %ld",
					  e->e_deliver_by - (curtime() - e->e_ctime),
					  mci->mci_min_by);
				rcode = EX_UNAVAILABLE;
				goto give_up;
			}
		}

# if STARTTLS
		/* first TLS then AUTH to provide a security layer */
		if (mci->mci_state != MCIS_CLOSED &&
		    !DONE_STARTTLS(mci->mci_flags))
		{
			int olderrors;
			bool usetls;
			bool saveQuickAbort = QuickAbort;
			bool saveSuprErrs = SuprErrs;
			char *host = NULL;

			rcode = EX_OK;
			usetls = bitset(MCIF_TLS, mci->mci_flags);
			if (usetls)
				usetls = !iscltflgset(e, D_NOTLS);

			host = macvalue(macid("{server_name}"), e);
			if (usetls)
			{
				olderrors = Errors;
				QuickAbort = false;
				SuprErrs = true;
				if (rscheck("try_tls", host, NULL, e,
					    RSF_RMCOMM, 7, host, NOQID, NULL,
					    NULL) != EX_OK
				    || Errors > olderrors)
				{
					usetls = false;
				}
				SuprErrs = saveSuprErrs;
				QuickAbort = saveQuickAbort;
			}

			if (usetls)
			{
				if ((rcode = starttls(m, mci, e)) == EX_OK)
				{
					/* start again without STARTTLS */
					mci->mci_flags |= MCIF_TLSACT;
				}
				else
				{
					char *s;

					/*
					**  TLS negotiation failed, what to do?
					**  fall back to unencrypted connection
					**  or abort? How to decide?
					**  set a macro and call a ruleset.
					*/

					mci->mci_flags &= ~MCIF_TLS;
					switch (rcode)
					{
					  case EX_TEMPFAIL:
						s = "TEMP";
						break;
					  case EX_USAGE:
						s = "USAGE";
						break;
					  case EX_PROTOCOL:
						s = "PROTOCOL";
						break;
					  case EX_SOFTWARE:
						s = "SOFTWARE";
						break;
					  case EX_UNAVAILABLE:
						s = "NONE";
						break;

					  /* everything else is a failure */
					  default:
						s = "FAILURE";
						rcode = EX_TEMPFAIL;
					}
					macdefine(&e->e_macro, A_PERM,
						  macid("{verify}"), s);
				}
			}
			else
				macdefine(&e->e_macro, A_PERM,
					  macid("{verify}"), "NONE");
			olderrors = Errors;
			QuickAbort = false;
			SuprErrs = true;

			/*
			**  rcode == EX_SOFTWARE is special:
			**  the TLS negotiation failed
			**  we have to drop the connection no matter what
			**  However, we call tls_server to give it the chance
			**  to log the problem and return an appropriate
			**  error code.
			*/

			if (rscheck("tls_server",
				    macvalue(macid("{verify}"), e),
				    NULL, e, RSF_RMCOMM|RSF_COUNT, 5,
				    host, NOQID, NULL, NULL) != EX_OK ||
			    Errors > olderrors ||
			    rcode == EX_SOFTWARE)
			{
				char enhsc[ENHSCLEN];
				extern char MsgBuf[];

				if (ISSMTPCODE(MsgBuf) &&
				    extenhsc(MsgBuf + 4, ' ', enhsc) > 0)
				{
					p = sm_rpool_strdup_x(e->e_rpool,
							      MsgBuf);
				}
				else
				{
					p = "403 4.7.0 server not authenticated.";
					(void) sm_strlcpy(enhsc, "4.7.0",
							  sizeof(enhsc));
				}
				SuprErrs = saveSuprErrs;
				QuickAbort = saveQuickAbort;

				if (rcode == EX_SOFTWARE)
				{
					/* drop the connection */
					mci->mci_state = MCIS_QUITING;
					if (mci->mci_in != NULL)
					{
						(void) sm_io_close(mci->mci_in,
								   SM_TIME_DEFAULT);
						mci->mci_in = NULL;
					}
					mci->mci_flags &= ~MCIF_TLSACT;
					(void) endmailer(mci, e, pv);
				}
				else
				{
					/* abort transfer */
					smtpquit(m, mci, e);
				}

				/* avoid bogus error msg */
				mci->mci_errno = 0;

				/* temp or permanent failure? */
				rcode = (*p == '4') ? EX_TEMPFAIL
						    : EX_UNAVAILABLE;
				mci_setstat(mci, rcode, enhsc, p);

				/*
				**  hack to get the error message into
				**  the envelope (done in giveresponse())
				*/

				(void) sm_strlcpy(SmtpError, p,
						  sizeof(SmtpError));
			}
			else if (mci->mci_state == MCIS_CLOSED)
			{
				/* connection close caused by 421 */
				mci->mci_errno = 0;
				rcode = EX_TEMPFAIL;
				mci_setstat(mci, rcode, NULL, "421");
			}
			else
				rcode = 0;

			QuickAbort = saveQuickAbort;
			SuprErrs = saveSuprErrs;
			if (DONE_STARTTLS(mci->mci_flags) &&
			    mci->mci_state != MCIS_CLOSED)
			{
				SET_HELO(mci->mci_flags);
				mci_clr_extensions(mci);
				goto reconnect;
			}
		}
# endif /* STARTTLS */
# if SASL
		/* if other server supports authentication let's authenticate */
		if (mci->mci_state != MCIS_CLOSED &&
		    mci->mci_saslcap != NULL &&
		    !DONE_AUTH(mci->mci_flags) && !iscltflgset(e, D_NOAUTH))
		{
			/* Should we require some minimum authentication? */
			if ((ret = smtpauth(m, mci, e)) == EX_OK)
			{
				int result;
				sasl_ssf_t *ssf = NULL;

				/* Get security strength (features) */
				result = sasl_getprop(mci->mci_conn, SASL_SSF,
# if SASL >= 20000
						      (const void **) &ssf);
# else /* SASL >= 20000 */
						      (void **) &ssf);
# endif /* SASL >= 20000 */

				/* XXX authid? */
				if (LogLevel > 9)
					sm_syslog(LOG_INFO, NOQID,
						  "AUTH=client, relay=%.100s, mech=%.16s, bits=%d",
						  mci->mci_host,
						  macvalue(macid("{auth_type}"), e),
						  result == SASL_OK ? *ssf : 0);

				/*
				**  Only switch to encrypted connection
				**  if a security layer has been negotiated
				*/

				if (result == SASL_OK && *ssf > 0)
				{
					int tmo;

					/*
					**  Convert I/O layer to use SASL.
					**  If the call fails, the connection
					**  is aborted.
					*/

					tmo = DATA_PROGRESS_TIMEOUT * 1000;
					if (sfdcsasl(&mci->mci_in,
						     &mci->mci_out,
						     mci->mci_conn, tmo) == 0)
					{
						mci_clr_extensions(mci);
						mci->mci_flags |= MCIF_AUTHACT|
								  MCIF_ONLY_EHLO;
						goto reconnect;
					}
					syserr("AUTH TLS switch failed in client");
				}
				/* else? XXX */
				mci->mci_flags |= MCIF_AUTHACT;

			}
			else if (ret == EX_TEMPFAIL)
			{
				if (LogLevel > 8)
					sm_syslog(LOG_ERR, NOQID,
						  "AUTH=client, relay=%.100s, temporary failure, connection abort",
						  mci->mci_host);
				smtpquit(m, mci, e);

				/* avoid bogus error msg */
				mci->mci_errno = 0;
				rcode = EX_TEMPFAIL;
				mci_setstat(mci, rcode, "4.3.0", p);

				/*
				**  hack to get the error message into
				**  the envelope (done in giveresponse())
				*/

				(void) sm_strlcpy(SmtpError,
						  "Temporary AUTH failure",
						  sizeof(SmtpError));
			}
		}
# endif /* SASL */
	}


do_transfer:
	/* clear out per-message flags from connection structure */
	mci->mci_flags &= ~(MCIF_CVT7TO8|MCIF_CVT8TO7);

	if (bitset(EF_HAS8BIT, e->e_flags) &&
	    !bitset(EF_DONT_MIME, e->e_flags) &&
	    bitnset(M_7BITS, m->m_flags))
		mci->mci_flags |= MCIF_CVT8TO7;

#if MIME7TO8
	if (bitnset(M_MAKE8BIT, m->m_flags) &&
	    !bitset(MCIF_7BIT, mci->mci_flags) &&
	    (p = hvalue("Content-Transfer-Encoding", e->e_header)) != NULL &&
	     (sm_strcasecmp(p, "quoted-printable") == 0 ||
	      sm_strcasecmp(p, "base64") == 0) &&
	    (p = hvalue("Content-Type", e->e_header)) != NULL)
	{
		/* may want to convert 7 -> 8 */
		/* XXX should really parse it here -- and use a class XXX */
		if (sm_strncasecmp(p, "text/plain", 10) == 0 &&
		    (p[10] == '\0' || p[10] == ' ' || p[10] == ';'))
			mci->mci_flags |= MCIF_CVT7TO8;
	}
#endif /* MIME7TO8 */

	if (tTd(11, 1))
	{
		sm_dprintf("openmailer: ");
		mci_dump(sm_debug_file(), mci, false);
	}

#if _FFR_CLIENT_SIZE
	/*
	**  See if we know the maximum size and
	**  abort if the message is too big.
	**
	**  NOTE: _FFR_CLIENT_SIZE is untested.
	*/

	if (bitset(MCIF_SIZE, mci->mci_flags) &&
	    mci->mci_maxsize > 0 &&
	    e->e_msgsize > mci->mci_maxsize)
	{
		e->e_flags |= EF_NO_BODY_RETN;
		if (bitnset(M_LOCALMAILER, m->m_flags))
			e->e_status = "5.2.3";
		else
			e->e_status = "5.3.4";

		usrerrenh(e->e_status,
			  "552 Message is too large; %ld bytes max",
			  mci->mci_maxsize);
		rcode = EX_DATAERR;

		/* Need an e_message for error */
		(void) sm_snprintf(SmtpError, sizeof(SmtpError),
				   "Message is too large; %ld bytes max",
				   mci->mci_maxsize);
		goto give_up;
	}
#endif /* _FFR_CLIENT_SIZE */

	if (mci->mci_state != MCIS_OPEN)
	{
		/* couldn't open the mailer */
		rcode = mci->mci_exitstat;
		errno = mci->mci_errno;
		SM_SET_H_ERRNO(mci->mci_herrno);
		if (rcode == EX_OK)
		{
			/* shouldn't happen */
			syserr("554 5.3.5 deliver: mci=%lx rcode=%d errno=%d state=%d sig=%s",
			       (unsigned long) mci, rcode, errno,
			       mci->mci_state, firstsig);
			mci_dump_all(smioout, true);
			rcode = EX_SOFTWARE;
		}
		else if (nummxhosts > hostnum)
		{
			/* try next MX site */
			goto tryhost;
		}
	}
	else if (!clever)
	{
		bool ok;

		/*
		**  Format and send message.
		*/

		rcode = EX_OK;
		errno = 0;
		ok = putfromline(mci, e);
		if (ok)
			ok = (*e->e_puthdr)(mci, e->e_header, e, M87F_OUTER);
		if (ok)
			ok = (*e->e_putbody)(mci, e, NULL);
		if (ok && bitset(MCIF_INLONGLINE, mci->mci_flags))
			ok = putline("", mci);

		/*
		**  Ignore an I/O error that was caused by EPIPE.
		**  Some broken mailers don't read the entire body
		**  but just exit() thus causing an I/O error.
		*/

		if (!ok && (sm_io_error(mci->mci_out) && errno == EPIPE))
			ok = true;

		/* (always) get the exit status */
		rcode = endmailer(mci, e, pv);
		if (!ok)
			rcode = EX_TEMPFAIL;
		if (rcode == EX_TEMPFAIL && SmtpError[0] == '\0')
		{
			/*
			**  Need an e_message for mailq display.
			**  We set SmtpError as
			*/

			(void) sm_snprintf(SmtpError, sizeof(SmtpError),
					   "%s mailer (%s) exited with EX_TEMPFAIL",
					   m->m_name, m->m_mailer);
		}
	}
	else
	{
		/*
		**  Send the MAIL FROM: protocol
		*/

		/* XXX this isn't pipelined... */
		rcode = smtpmailfrom(m, mci, e);
		if (rcode == EX_OK)
		{
			register int i;
# if PIPELINING
			ADDRESS *volatile pchain;
# endif /* PIPELINING */

			/* send the recipient list */
			tobuf[0] = '\0';
			mci->mci_retryrcpt = false;
			mci->mci_tolist = tobuf;
# if PIPELINING
			pchain = NULL;
			mci->mci_nextaddr = NULL;
# endif /* PIPELINING */

			for (to = tochain; to != NULL; to = to->q_tchain)
			{
				if (!QS_IS_UNMARKED(to->q_state))
					continue;

				/* mark recipient state as "ok so far" */
				to->q_state = QS_OK;
				e->e_to = to->q_paddr;
# if STARTTLS
				i = rscheck("tls_rcpt", to->q_user, NULL, e,
					    RSF_RMCOMM|RSF_COUNT, 3,
					    mci->mci_host, e->e_id, NULL, NULL);
				if (i != EX_OK)
				{
					markfailure(e, to, mci, i, false);
					giveresponse(i, to->q_status,  m, mci,
						     ctladdr, xstart, e, to);
					if (i == EX_TEMPFAIL)
					{
						mci->mci_retryrcpt = true;
						to->q_state = QS_RETRY;
					}
					continue;
				}
# endif /* STARTTLS */

				i = smtprcpt(to, m, mci, e, ctladdr, xstart);
# if PIPELINING
				if (i == EX_OK &&
				    bitset(MCIF_PIPELINED, mci->mci_flags))
				{
					/*
					**  Add new element to list of
					**  recipients for pipelining.
					*/

					to->q_pchain = NULL;
					if (mci->mci_nextaddr == NULL)
						mci->mci_nextaddr = to;
					if (pchain == NULL)
						pchain = to;
					else
					{
						pchain->q_pchain = to;
						pchain = pchain->q_pchain;
					}
				}
# endif /* PIPELINING */
				if (i != EX_OK)
				{
					markfailure(e, to, mci, i, false);
					giveresponse(i, to->q_status, m, mci,
						     ctladdr, xstart, e, to);
					if (i == EX_TEMPFAIL)
						to->q_state = QS_RETRY;
				}
			}

			/* No recipients in list and no missing responses? */
			if (tobuf[0] == '\0'
# if PIPELINING
			    && bitset(MCIF_PIPELINED, mci->mci_flags)
			    && mci->mci_nextaddr == NULL
# endif /* PIPELINING */
			   )
			{
				rcode = EX_OK;
				e->e_to = NULL;
				if (bitset(MCIF_CACHED, mci->mci_flags))
					smtprset(m, mci, e);
			}
			else
			{
				e->e_to = tobuf + 1;
				rcode = smtpdata(m, mci, e, ctladdr, xstart);
			}
		}
		if (rcode == EX_TEMPFAIL && nummxhosts > hostnum)
		{
			/* try next MX site */
			goto tryhost;
		}
	}
#if NAMED_BIND
	if (ConfigLevel < 2)
		_res.options |= RES_DEFNAMES | RES_DNSRCH;	/* XXX */
#endif /* NAMED_BIND */

	if (tTd(62, 1))
		checkfds("after delivery");

	/*
	**  Do final status disposal.
	**	We check for something in tobuf for the SMTP case.
	**	If we got a temporary failure, arrange to queue the
	**		addressees.
	*/

  give_up:
	if (bitnset(M_LMTP, m->m_flags))
	{
		lmtp_rcode = rcode;
		tobuf[0] = '\0';
		anyok = false;
		strsize = 0;
	}
	else
		anyok = rcode == EX_OK;

	for (to = tochain; to != NULL; to = to->q_tchain)
	{
		/* see if address already marked */
		if (!QS_IS_OK(to->q_state))
			continue;

		/* if running LMTP, get the status for each address */
		if (bitnset(M_LMTP, m->m_flags))
		{
			if (lmtp_rcode == EX_OK)
				rcode = smtpgetstat(m, mci, e);
			if (rcode == EX_OK)
			{
				strsize += sm_strlcat2(tobuf + strsize, ",",
						to->q_paddr,
						tobufsize - strsize);
				SM_ASSERT(strsize < tobufsize);
				anyok = true;
			}
			else
			{
				e->e_to = to->q_paddr;
				markfailure(e, to, mci, rcode, true);
				giveresponse(rcode, to->q_status, m, mci,
					     ctladdr, xstart, e, to);
				e->e_to = tobuf + 1;
				continue;
			}
		}
		else
		{
			/* mark bad addresses */
			if (rcode != EX_OK)
			{
				if (goodmxfound && rcode == EX_NOHOST)
					rcode = EX_TEMPFAIL;
				markfailure(e, to, mci, rcode, true);
				continue;
			}
		}

		/* successful delivery */
		to->q_state = QS_SENT;
		to->q_statdate = curtime();
		e->e_nsent++;

		/*
		**  Checkpoint the send list every few addresses
		*/

		if (CheckpointInterval > 0 && e->e_nsent >= CheckpointInterval)
		{
			queueup(e, false, false);
			e->e_nsent = 0;
		}

		if (bitnset(M_LOCALMAILER, m->m_flags) &&
		    bitset(QPINGONSUCCESS, to->q_flags))
		{
			to->q_flags |= QDELIVERED;
			to->q_status = "2.1.5";
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					     "%s... Successfully delivered\n",
					     to->q_paddr);
		}
		else if (bitset(QPINGONSUCCESS, to->q_flags) &&
			 bitset(QPRIMARY, to->q_flags) &&
			 !bitset(MCIF_DSN, mci->mci_flags))
		{
			to->q_flags |= QRELAYED;
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					     "%s... relayed; expect no further notifications\n",
					     to->q_paddr);
		}
		else if (IS_DLVR_NOTIFY(e) &&
			 !bitset(MCIF_DLVR_BY, mci->mci_flags) &&
			 bitset(QPRIMARY, to->q_flags) &&
			 (!bitset(QHASNOTIFY, to->q_flags) ||
			  bitset(QPINGONSUCCESS, to->q_flags) ||
			  bitset(QPINGONFAILURE, to->q_flags) ||
			  bitset(QPINGONDELAY, to->q_flags)))
		{
			/* RFC 2852, 4.1.4.2: no NOTIFY, or not NEVER */
			to->q_flags |= QBYNRELAY;
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					     "%s... Deliver-by notify: relayed\n",
					     to->q_paddr);
		}
		else if (IS_DLVR_TRACE(e) &&
			 (!bitset(QHASNOTIFY, to->q_flags) ||
			  bitset(QPINGONSUCCESS, to->q_flags) ||
			  bitset(QPINGONFAILURE, to->q_flags) ||
			  bitset(QPINGONDELAY, to->q_flags)) &&
			 bitset(QPRIMARY, to->q_flags))
		{
			/* RFC 2852, 4.1.4: no NOTIFY, or not NEVER */
			to->q_flags |= QBYTRACE;
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					     "%s... Deliver-By trace: relayed\n",
					     to->q_paddr);
		}
	}

	if (bitnset(M_LMTP, m->m_flags))
	{
		/*
		**  Global information applies to the last recipient only;
		**  clear it out to avoid bogus errors.
		*/

		rcode = EX_OK;
		e->e_statmsg = NULL;

		/* reset the mci state for the next transaction */
		if (mci != NULL &&
		    (mci->mci_state == MCIS_MAIL ||
		     mci->mci_state == MCIS_RCPT ||
		     mci->mci_state == MCIS_DATA))
		{
			mci->mci_state = MCIS_OPEN;
			SmtpPhase = mci->mci_phase = "idle";
			sm_setproctitle(true, e, "%s: %s", CurHostName,
					mci->mci_phase);
		}
	}

	if (tobuf[0] != '\0')
	{
		giveresponse(rcode, NULL, m, mci, ctladdr, xstart, e, NULL);
#if 0
		/*
		**  This code is disabled for now because I am not
		**  sure that copying status from the first recipient
		**  to all non-status'ed recipients is a good idea.
		*/

		if (tochain->q_message != NULL &&
		    !bitnset(M_LMTP, m->m_flags) && rcode != EX_OK)
		{
			for (to = tochain->q_tchain; to != NULL;
			     to = to->q_tchain)
			{
				/* see if address already marked */
				if (QS_IS_QUEUEUP(to->q_state) &&
				    to->q_message == NULL)
					to->q_message = sm_rpool_strdup_x(e->e_rpool,
							tochain->q_message);
			}
		}
#endif /* 0 */
	}
	if (anyok)
		markstats(e, tochain, STATS_NORMAL);
	mci_store_persistent(mci);

	/* Some recipients were tempfailed, try them on the next host */
	if (mci != NULL && mci->mci_retryrcpt && nummxhosts > hostnum)
	{
		/* try next MX site */
		goto tryhost;
	}

	/* now close the connection */
	if (clever && mci != NULL && mci->mci_state != MCIS_CLOSED &&
	    !bitset(MCIF_CACHED, mci->mci_flags))
		smtpquit(m, mci, e);

cleanup: ;
	}
	SM_FINALLY
	{
		/*
		**  Restore state and return.
		*/
#if XDEBUG
		char wbuf[MAXLINE];

		/* make absolutely certain 0, 1, and 2 are in use */
		(void) sm_snprintf(wbuf, sizeof(wbuf),
				   "%s... end of deliver(%s)",
				   e->e_to == NULL ? "NO-TO-LIST"
						   : shortenstring(e->e_to,
								   MAXSHORTSTR),
				  m->m_name);
		checkfd012(wbuf);
#endif /* XDEBUG */

		errno = 0;

		/*
		**  It was originally necessary to set macro 'g' to NULL
		**  because it previously pointed to an auto buffer.
		**  We don't do this any more, so this may be unnecessary.
		*/

		macdefine(&e->e_macro, A_PERM, 'g', (char *) NULL);
		e->e_to = NULL;
	}
	SM_END_TRY
	return rcode;
}

/*
**  MARKFAILURE -- mark a failure on a specific address.
**
**	Parameters:
**		e -- the envelope we are sending.
**		q -- the address to mark.
**		mci -- mailer connection information.
**		rcode -- the code signifying the particular failure.
**		ovr -- override an existing code?
**
**	Returns:
**		none.
**
**	Side Effects:
**		marks the address (and possibly the envelope) with the
**			failure so that an error will be returned or
**			the message will be queued, as appropriate.
*/

void
markfailure(e, q, mci, rcode, ovr)
	register ENVELOPE *e;
	register ADDRESS *q;
	register MCI *mci;
	int rcode;
	bool ovr;
{
	int save_errno = errno;
	char *status = NULL;
	char *rstatus = NULL;

	switch (rcode)
	{
	  case EX_OK:
		break;

	  case EX_TEMPFAIL:
	  case EX_IOERR:
	  case EX_OSERR:
		q->q_state = QS_QUEUEUP;
		break;

	  default:
		q->q_state = QS_BADADDR;
		break;
	}

	/* find most specific error code possible */
	if (mci != NULL && mci->mci_status != NULL)
	{
		status = sm_rpool_strdup_x(e->e_rpool, mci->mci_status);
		if (mci->mci_rstatus != NULL)
			rstatus = sm_rpool_strdup_x(e->e_rpool,
						    mci->mci_rstatus);
		else
			rstatus = NULL;
	}
	else if (e->e_status != NULL)
	{
		status = e->e_status;
		rstatus = NULL;
	}
	else
	{
		switch (rcode)
		{
		  case EX_USAGE:
			status = "5.5.4";
			break;

		  case EX_DATAERR:
			status = "5.5.2";
			break;

		  case EX_NOUSER:
			status = "5.1.1";
			break;

		  case EX_NOHOST:
			status = "5.1.2";
			break;

		  case EX_NOINPUT:
		  case EX_CANTCREAT:
		  case EX_NOPERM:
			status = "5.3.0";
			break;

		  case EX_UNAVAILABLE:
		  case EX_SOFTWARE:
		  case EX_OSFILE:
		  case EX_PROTOCOL:
		  case EX_CONFIG:
			status = "5.5.0";
			break;

		  case EX_OSERR:
		  case EX_IOERR:
			status = "4.5.0";
			break;

		  case EX_TEMPFAIL:
			status = "4.2.0";
			break;
		}
	}

	/* new status? */
	if (status != NULL && *status != '\0' && (ovr || q->q_status == NULL ||
	    *q->q_status == '\0' || *q->q_status < *status))
	{
		q->q_status = status;
		q->q_rstatus = rstatus;
	}
	if (rcode != EX_OK && q->q_rstatus == NULL &&
	    q->q_mailer != NULL && q->q_mailer->m_diagtype != NULL &&
	    sm_strcasecmp(q->q_mailer->m_diagtype, "X-UNIX") == 0)
	{
		char buf[16];

		(void) sm_snprintf(buf, sizeof(buf), "%d", rcode);
		q->q_rstatus = sm_rpool_strdup_x(e->e_rpool, buf);
	}

	q->q_statdate = curtime();
	if (CurHostName != NULL && CurHostName[0] != '\0' &&
	    mci != NULL && !bitset(M_LOCALMAILER, mci->mci_flags))
		q->q_statmta = sm_rpool_strdup_x(e->e_rpool, CurHostName);

	/* restore errno */
	errno = save_errno;
}
/*
**  ENDMAILER -- Wait for mailer to terminate.
**
**	We should never get fatal errors (e.g., segmentation
**	violation), so we report those specially.  For other
**	errors, we choose a status message (into statmsg),
**	and if it represents an error, we print it.
**
**	Parameters:
**		mci -- the mailer connection info.
**		e -- the current envelope.
**		pv -- the parameter vector that invoked the mailer
**			(for error messages).
**
**	Returns:
**		exit code of mailer.
**
**	Side Effects:
**		none.
*/

static jmp_buf	EndWaitTimeout;

static void
endwaittimeout(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(EndWaitTimeout, 1);
}

int
endmailer(mci, e, pv)
	register MCI *mci;
	register ENVELOPE *e;
	char **pv;
{
	int st;
	int save_errno = errno;
	char buf[MAXLINE];
	SM_EVENT *ev = NULL;


	mci_unlock_host(mci);

	/* close output to mailer */
	if (mci->mci_out != NULL)
	{
		(void) sm_io_close(mci->mci_out, SM_TIME_DEFAULT);
		mci->mci_out = NULL;
	}

	/* copy any remaining input to transcript */
	if (mci->mci_in != NULL && mci->mci_state != MCIS_ERROR &&
	    e->e_xfp != NULL)
	{
		while (sfgets(buf, sizeof(buf), mci->mci_in,
			      TimeOuts.to_quit, "Draining Input") != NULL)
			(void) sm_io_fputs(e->e_xfp, SM_TIME_DEFAULT, buf);
	}

#if SASL
	/* close SASL connection */
	if (bitset(MCIF_AUTHACT, mci->mci_flags))
	{
		sasl_dispose(&mci->mci_conn);
		mci->mci_flags &= ~MCIF_AUTHACT;
	}
#endif /* SASL */

#if STARTTLS
	/* shutdown TLS */
	(void) endtlsclt(mci);
#endif /* STARTTLS */

	/* now close the input */
	if (mci->mci_in != NULL)
	{
		(void) sm_io_close(mci->mci_in, SM_TIME_DEFAULT);
		mci->mci_in = NULL;
	}
	mci->mci_state = MCIS_CLOSED;

	errno = save_errno;

	/* in the IPC case there is nothing to wait for */
	if (mci->mci_pid == 0)
		return EX_OK;

	/* put a timeout around the wait */
	if (mci->mci_mailer->m_wait > 0)
	{
		if (setjmp(EndWaitTimeout) == 0)
			ev = sm_setevent(mci->mci_mailer->m_wait,
					 endwaittimeout, 0);
		else
		{
			syserr("endmailer %s: wait timeout (%ld)",
			       mci->mci_mailer->m_name,
			       (long) mci->mci_mailer->m_wait);
			return EX_TEMPFAIL;
		}
	}

	/* wait for the mailer process, collect status */
	st = waitfor(mci->mci_pid);
	save_errno = errno;
	if (ev != NULL)
		sm_clrevent(ev);
	errno = save_errno;

	if (st == -1)
	{
		syserr("endmailer %s: wait", mci->mci_mailer->m_name);
		return EX_SOFTWARE;
	}

	if (WIFEXITED(st))
	{
		/* normal death -- return status */
		return (WEXITSTATUS(st));
	}

	/* it died a horrid death */
	syserr("451 4.3.0 mailer %s died with signal %d%s",
		mci->mci_mailer->m_name, WTERMSIG(st),
		WCOREDUMP(st) ? " (core dumped)" :
		(WIFSTOPPED(st) ? " (stopped)" : ""));

	/* log the arguments */
	if (pv != NULL && e->e_xfp != NULL)
	{
		register char **av;

		(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT, "Arguments:");
		for (av = pv; *av != NULL; av++)
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT, " %s",
					     *av);
		(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT, "\n");
	}

	ExitStat = EX_TEMPFAIL;
	return EX_TEMPFAIL;
}
/*
**  GIVERESPONSE -- Interpret an error response from a mailer
**
**	Parameters:
**		status -- the status code from the mailer (high byte
**			only; core dumps must have been taken care of
**			already).
**		dsn -- the DSN associated with the address, if any.
**		m -- the mailer info for this mailer.
**		mci -- the mailer connection info -- can be NULL if the
**			response is given before the connection is made.
**		ctladdr -- the controlling address for the recipient
**			address(es).
**		xstart -- the transaction start time, for computing
**			transaction delays.
**		e -- the current envelope.
**		to -- the current recipient (NULL if none).
**
**	Returns:
**		none.
**
**	Side Effects:
**		Errors may be incremented.
**		ExitStat may be set.
*/

void
giveresponse(status, dsn, m, mci, ctladdr, xstart, e, to)
	int status;
	char *dsn;
	register MAILER *m;
	register MCI *mci;
	ADDRESS *ctladdr;
	time_t xstart;
	ENVELOPE *e;
	ADDRESS *to;
{
	register const char *statmsg;
	int errnum = errno;
	int off = 4;
	bool usestat = false;
	char dsnbuf[ENHSCLEN];
	char buf[MAXLINE];
	char *exmsg;

	if (e == NULL)
	{
		syserr("giveresponse: null envelope");
		/* NOTREACHED */
		SM_ASSERT(0);
	}

	/*
	**  Compute status message from code.
	*/

	exmsg = sm_sysexmsg(status);
	if (status == 0)
	{
		statmsg = "250 2.0.0 Sent";
		if (e->e_statmsg != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf), "%s (%s)",
					   statmsg,
					   shortenstring(e->e_statmsg, 403));
			statmsg = buf;
		}
	}
	else if (exmsg == NULL)
	{
		(void) sm_snprintf(buf, sizeof(buf),
				   "554 5.3.0 unknown mailer error %d",
				   status);
		status = EX_UNAVAILABLE;
		statmsg = buf;
		usestat = true;
	}
	else if (status == EX_TEMPFAIL)
	{
		char *bp = buf;

		(void) sm_strlcpy(bp, exmsg + 1, SPACELEFT(buf, bp));
		bp += strlen(bp);
#if NAMED_BIND
		if (h_errno == TRY_AGAIN)
			statmsg = sm_errstring(h_errno + E_DNSBASE);
		else
#endif /* NAMED_BIND */
		{
			if (errnum != 0)
				statmsg = sm_errstring(errnum);
			else
				statmsg = SmtpError;
		}
		if (statmsg != NULL && statmsg[0] != '\0')
		{
			switch (errnum)
			{
#ifdef ENETDOWN
			  case ENETDOWN:	/* Network is down */
#endif /* ENETDOWN */
#ifdef ENETUNREACH
			  case ENETUNREACH:	/* Network is unreachable */
#endif /* ENETUNREACH */
#ifdef ENETRESET
			  case ENETRESET:	/* Network dropped connection on reset */
#endif /* ENETRESET */
#ifdef ECONNABORTED
			  case ECONNABORTED:	/* Software caused connection abort */
#endif /* ECONNABORTED */
#ifdef EHOSTDOWN
			  case EHOSTDOWN:	/* Host is down */
#endif /* EHOSTDOWN */
#ifdef EHOSTUNREACH
			  case EHOSTUNREACH:	/* No route to host */
#endif /* EHOSTUNREACH */
				if (mci != NULL && mci->mci_host != NULL)
				{
					(void) sm_strlcpyn(bp,
							   SPACELEFT(buf, bp),
							   2, ": ",
							   mci->mci_host);
					bp += strlen(bp);
				}
				break;
			}
			(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ": ",
					   statmsg);
			usestat = true;
		}
		statmsg = buf;
	}
#if NAMED_BIND
	else if (status == EX_NOHOST && h_errno != 0)
	{
		statmsg = sm_errstring(h_errno + E_DNSBASE);
		(void) sm_snprintf(buf, sizeof(buf), "%s (%s)", exmsg + 1,
				   statmsg);
		statmsg = buf;
		usestat = true;
	}
#endif /* NAMED_BIND */
	else
	{
		statmsg = exmsg;
		if (*statmsg++ == ':' && errnum != 0)
		{
			(void) sm_snprintf(buf, sizeof(buf), "%s: %s", statmsg,
					   sm_errstring(errnum));
			statmsg = buf;
			usestat = true;
		}
		else if (bitnset(M_LMTP, m->m_flags) && e->e_statmsg != NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf), "%s (%s)", statmsg,
					   shortenstring(e->e_statmsg, 403));
			statmsg = buf;
			usestat = true;
		}
	}

	/*
	**  Print the message as appropriate
	*/

	if (status == EX_OK || status == EX_TEMPFAIL)
	{
		extern char MsgBuf[];

		if ((off = isenhsc(statmsg + 4, ' ')) > 0)
		{
			if (dsn == NULL)
			{
				(void) sm_snprintf(dsnbuf, sizeof(dsnbuf),
						   "%.*s", off, statmsg + 4);
				dsn = dsnbuf;
			}
			off += 5;
		}
		else
		{
			off = 4;
		}
		message("%s", statmsg + off);
		if (status == EX_TEMPFAIL && e->e_xfp != NULL)
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT, "%s\n",
					     &MsgBuf[4]);
	}
	else
	{
		char mbuf[ENHSCLEN + 4];

		Errors++;
		if ((off = isenhsc(statmsg + 4, ' ')) > 0 &&
		    off < sizeof(mbuf) - 4)
		{
			if (dsn == NULL)
			{
				(void) sm_snprintf(dsnbuf, sizeof(dsnbuf),
						   "%.*s", off, statmsg + 4);
				dsn = dsnbuf;
			}
			off += 5;

			/* copy only part of statmsg to mbuf */
			(void) sm_strlcpy(mbuf, statmsg, off);
			(void) sm_strlcat(mbuf, " %s", sizeof(mbuf));
		}
		else
		{
			dsnbuf[0] = '\0';
			(void) sm_snprintf(mbuf, sizeof(mbuf), "%.3s %%s",
					   statmsg);
			off = 4;
		}
		usrerr(mbuf, &statmsg[off]);
	}

	/*
	**  Final cleanup.
	**	Log a record of the transaction.  Compute the new ExitStat
	**	-- if we already had an error, stick with that.
	*/

	if (OpMode != MD_VERIFY && !bitset(EF_VRFYONLY, e->e_flags) &&
	    LogLevel > ((status == EX_TEMPFAIL) ? 8 : (status == EX_OK) ? 7 : 6))
		logdelivery(m, mci, dsn, statmsg + off, ctladdr, xstart, e, to, status);

	if (tTd(11, 2))
		sm_dprintf("giveresponse: status=%d, dsn=%s, e->e_message=%s, errnum=%d\n",
			   status,
			   dsn == NULL ? "<NULL>" : dsn,
			   e->e_message == NULL ? "<NULL>" : e->e_message,
			   errnum);

	if (status != EX_TEMPFAIL)
		setstat(status);
	if (status != EX_OK && (status != EX_TEMPFAIL || e->e_message == NULL))
		e->e_message = sm_rpool_strdup_x(e->e_rpool, statmsg + off);
	if (status != EX_OK && to != NULL && to->q_message == NULL)
	{
		if (!usestat && e->e_message != NULL)
			to->q_message = sm_rpool_strdup_x(e->e_rpool,
							  e->e_message);
		else
			to->q_message = sm_rpool_strdup_x(e->e_rpool,
							  statmsg + off);
	}
	errno = 0;
	SM_SET_H_ERRNO(0);
}
/*
**  LOGDELIVERY -- log the delivery in the system log
**
**	Care is taken to avoid logging lines that are too long, because
**	some versions of syslog have an unfortunate proclivity for core
**	dumping.  This is a hack, to be sure, that is at best empirical.
**
**	Parameters:
**		m -- the mailer info.  Can be NULL for initial queue.
**		mci -- the mailer connection info -- can be NULL if the
**			log is occurring when no connection is active.
**		dsn -- the DSN attached to the status.
**		status -- the message to print for the status.
**		ctladdr -- the controlling address for the to list.
**		xstart -- the transaction start time, used for
**			computing transaction delay.
**		e -- the current envelope.
**		to -- the current recipient (NULL if none).
**		rcode -- status code
**
**	Returns:
**		none
**
**	Side Effects:
**		none
*/

void
logdelivery(m, mci, dsn, status, ctladdr, xstart, e, to, rcode)
	MAILER *m;
	register MCI *mci;
	char *dsn;
	const char *status;
	ADDRESS *ctladdr;
	time_t xstart;
	register ENVELOPE *e;
	ADDRESS *to;
	int rcode;
{
	register char *bp;
	register char *p;
	int l;
	time_t now = curtime();
	char buf[1024];

#if (SYSLOG_BUFSIZE) >= 256
	/* ctladdr: max 106 bytes */
	bp = buf;
	if (ctladdr != NULL)
	{
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", ctladdr=",
				   shortenstring(ctladdr->q_paddr, 83));
		bp += strlen(bp);
		if (bitset(QGOODUID, ctladdr->q_flags))
		{
			(void) sm_snprintf(bp, SPACELEFT(buf, bp), " (%d/%d)",
					   (int) ctladdr->q_uid,
					   (int) ctladdr->q_gid);
			bp += strlen(bp);
		}
	}

	/* delay & xdelay: max 41 bytes */
	(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", delay=",
			   pintvl(now - e->e_ctime, true));
	bp += strlen(bp);

	if (xstart != (time_t) 0)
	{
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", xdelay=",
				   pintvl(now - xstart, true));
		bp += strlen(bp);
	}

	/* mailer: assume about 19 bytes (max 10 byte mailer name) */
	if (m != NULL)
	{
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", mailer=",
				   m->m_name);
		bp += strlen(bp);
	}

# if _FFR_LOG_MORE2
#  if STARTTLS
	p = macvalue(macid("{verify}"), e);
	if (p == NULL || *p == '\0')
		p = "NONE";
	(void) sm_snprintf(bp, SPACELEFT(buf, bp), ", tls_verify=%.20s", p);
	bp += strlen(bp);
#  endif /* STARTTLS */
# endif /* _FFR_LOG_MORE2 */

	/* pri: changes with each delivery attempt */
	(void) sm_snprintf(bp, SPACELEFT(buf, bp), ", pri=%ld",
		PRT_NONNEGL(e->e_msgpriority));
	bp += strlen(bp);

	/* relay: max 66 bytes for IPv4 addresses */
	if (mci != NULL && mci->mci_host != NULL)
	{
		extern SOCKADDR CurHostAddr;

		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", relay=",
				   shortenstring(mci->mci_host, 40));
		bp += strlen(bp);

		if (CurHostAddr.sa.sa_family != 0)
		{
			(void) sm_snprintf(bp, SPACELEFT(buf, bp), " [%s]",
					   anynet_ntoa(&CurHostAddr));
		}
	}
	else if (strcmp(status, "quarantined") == 0)
	{
		if (e->e_quarmsg != NULL)
			(void) sm_snprintf(bp, SPACELEFT(buf, bp),
					   ", quarantine=%s",
					   shortenstring(e->e_quarmsg, 40));
	}
	else if (strcmp(status, "queued") != 0)
	{
		p = macvalue('h', e);
		if (p != NULL && p[0] != '\0')
		{
			(void) sm_snprintf(bp, SPACELEFT(buf, bp),
					   ", relay=%s", shortenstring(p, 40));
		}
	}
	bp += strlen(bp);

	/* dsn */
	if (dsn != NULL && *dsn != '\0')
	{
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", dsn=",
				   shortenstring(dsn, ENHSCLEN));
		bp += strlen(bp);
	}

#if _FFR_LOG_NTRIES
	/* ntries */
	if (e->e_ntries >= 0)
	{
		(void) sm_snprintf(bp, SPACELEFT(buf, bp),
				   ", ntries=%d", e->e_ntries + 1);
		bp += strlen(bp);
	}
#endif /* _FFR_LOG_NTRIES */

# define STATLEN		(((SYSLOG_BUFSIZE) - 100) / 4)
# if (STATLEN) < 63
#  undef STATLEN
#  define STATLEN	63
# endif /* (STATLEN) < 63 */
# if (STATLEN) > 203
#  undef STATLEN
#  define STATLEN	203
# endif /* (STATLEN) > 203 */

#if _FFR_LOGREPLY
	/*
	**  Notes:
	**  per-rcpt status: to->q_rstatus
	**  global status: e->e_text
	**
	**  We (re)use STATLEN here, is that a good choice?
	**
	**  stat=Deferred: ...
	**  has sometimes the same text?
	**
	**  Note: this doesn't show the stage at which the error happened.
	**  can/should we log that?
	**  XS_* in reply() basically encodes the state.
	*/

	/* only show errors */
	if (rcode != EX_OK && to != NULL && to->q_rstatus != NULL &&
	    *to->q_rstatus != '\0')
	{
		(void) sm_snprintf(bp, SPACELEFT(buf, bp),
			", reply=%s",
			shortenstring(to->q_rstatus, STATLEN));
		bp += strlen(bp);
	}
	else if (rcode != EX_OK && e->e_text != NULL)
	{
		(void) sm_snprintf(bp, SPACELEFT(buf, bp),
			", reply=%d %s%s%s",
			e->e_rcode,
			e->e_renhsc,
			(e->e_renhsc[0] != '\0') ? " " : "",
			shortenstring(e->e_text, STATLEN));
		bp += strlen(bp);
	}
#endif

	/* stat: max 210 bytes */
	if ((bp - buf) > (sizeof(buf) - ((STATLEN) + 20)))
	{
		/* desperation move -- truncate data */
		bp = buf + sizeof(buf) - ((STATLEN) + 17);
		(void) sm_strlcpy(bp, "...", SPACELEFT(buf, bp));
		bp += 3;
	}

	(void) sm_strlcpy(bp, ", stat=", SPACELEFT(buf, bp));
	bp += strlen(bp);

	(void) sm_strlcpy(bp, shortenstring(status, STATLEN),
			  SPACELEFT(buf, bp));

	/* id, to: max 13 + TOBUFSIZE bytes */
	l = SYSLOG_BUFSIZE - 100 - strlen(buf);
	if (l < 0)
		l = 0;
	p = e->e_to == NULL ? "NO-TO-LIST" : e->e_to;
	while (strlen(p) >= l)
	{
		register char *q;

		for (q = p + l; q > p; q--)
		{
			/* XXX a comma in an address will break this! */
			if (*q == ',')
				break;
		}
		if (p == q)
			break;
		sm_syslog(LOG_INFO, e->e_id, "to=%.*s [more]%s",
			  (int) (++q - p), p, buf);
		p = q;
	}
	sm_syslog(LOG_INFO, e->e_id, "to=%.*s%s", l, p, buf);

#else /* (SYSLOG_BUFSIZE) >= 256 */

	l = SYSLOG_BUFSIZE - 85;
	if (l < 0)
		l = 0;
	p = e->e_to == NULL ? "NO-TO-LIST" : e->e_to;
	while (strlen(p) >= l)
	{
		register char *q;

		for (q = p + l; q > p; q--)
		{
			if (*q == ',')
				break;
		}
		if (p == q)
			break;

		sm_syslog(LOG_INFO, e->e_id, "to=%.*s [more]",
			  (int) (++q - p), p);
		p = q;
	}
	sm_syslog(LOG_INFO, e->e_id, "to=%.*s", l, p);

	if (ctladdr != NULL)
	{
		bp = buf;
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, "ctladdr=",
				   shortenstring(ctladdr->q_paddr, 83));
		bp += strlen(bp);
		if (bitset(QGOODUID, ctladdr->q_flags))
		{
			(void) sm_snprintf(bp, SPACELEFT(buf, bp), " (%d/%d)",
					   ctladdr->q_uid, ctladdr->q_gid);
			bp += strlen(bp);
		}
		sm_syslog(LOG_INFO, e->e_id, "%s", buf);
	}
	bp = buf;
	(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, "delay=",
			   pintvl(now - e->e_ctime, true));
	bp += strlen(bp);
	if (xstart != (time_t) 0)
	{
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", xdelay=",
				   pintvl(now - xstart, true));
		bp += strlen(bp);
	}

	if (m != NULL)
	{
		(void) sm_strlcpyn(bp, SPACELEFT(buf, bp), 2, ", mailer=",
				   m->m_name);
		bp += strlen(bp);
	}
	sm_syslog(LOG_INFO, e->e_id, "%.1000s", buf);

	buf[0] = '\0';
	bp = buf;
	if (mci != NULL && mci->mci_host != NULL)
	{
		extern SOCKADDR CurHostAddr;

		(void) sm_snprintf(bp, SPACELEFT(buf, bp), "relay=%.100s",
				   mci->mci_host);
		bp += strlen(bp);

		if (CurHostAddr.sa.sa_family != 0)
			(void) sm_snprintf(bp, SPACELEFT(buf, bp),
					   " [%.100s]",
					   anynet_ntoa(&CurHostAddr));
	}
	else if (strcmp(status, "quarantined") == 0)
	{
		if (e->e_quarmsg != NULL)
			(void) sm_snprintf(bp, SPACELEFT(buf, bp),
					   ", quarantine=%.100s",
					   e->e_quarmsg);
	}
	else if (strcmp(status, "queued") != 0)
	{
		p = macvalue('h', e);
		if (p != NULL && p[0] != '\0')
			(void) sm_snprintf(buf, sizeof(buf), "relay=%.100s", p);
	}
	if (buf[0] != '\0')
		sm_syslog(LOG_INFO, e->e_id, "%.1000s", buf);

	sm_syslog(LOG_INFO, e->e_id, "stat=%s", shortenstring(status, 63));
#endif /* (SYSLOG_BUFSIZE) >= 256 */
}
/*
**  PUTFROMLINE -- output a UNIX-style from line (or whatever)
**
**	This can be made an arbitrary message separator by changing $l
**
**	One of the ugliest hacks seen by human eyes is contained herein:
**	UUCP wants those stupid "remote from <host>" lines.  Why oh why
**	does a well-meaning programmer such as myself have to deal with
**	this kind of antique garbage????
**
**	Parameters:
**		mci -- the connection information.
**		e -- the envelope.
**
**	Returns:
**		true iff line was written successfully
**
**	Side Effects:
**		outputs some text to fp.
*/

bool
putfromline(mci, e)
	register MCI *mci;
	ENVELOPE *e;
{
	char *template = UnixFromLine;
	char buf[MAXLINE];
	char xbuf[MAXLINE];

	if (bitnset(M_NHDR, mci->mci_mailer->m_flags))
		return true;

	mci->mci_flags |= MCIF_INHEADER;

	if (bitnset(M_UGLYUUCP, mci->mci_mailer->m_flags))
	{
		char *bang;

		expand("\201g", buf, sizeof(buf), e);
		bang = strchr(buf, '!');
		if (bang == NULL)
		{
			char *at;
			char hname[MAXNAME];

			/*
			**  If we can construct a UUCP path, do so
			*/

			at = strrchr(buf, '@');
			if (at == NULL)
			{
				expand("\201k", hname, sizeof(hname), e);
				at = hname;
			}
			else
				*at++ = '\0';
			(void) sm_snprintf(xbuf, sizeof(xbuf),
					   "From %.800s  \201d remote from %.100s\n",
					   buf, at);
		}
		else
		{
			*bang++ = '\0';
			(void) sm_snprintf(xbuf, sizeof(xbuf),
					   "From %.800s  \201d remote from %.100s\n",
					   bang, buf);
			template = xbuf;
		}
	}
	expand(template, buf, sizeof(buf), e);
	return putxline(buf, strlen(buf), mci, PXLF_HEADER);
}

/*
**  PUTBODY -- put the body of a message.
**
**	Parameters:
**		mci -- the connection information.
**		e -- the envelope to put out.
**		separator -- if non-NULL, a message separator that must
**			not be permitted in the resulting message.
**
**	Returns:
**		true iff message was written successfully
**
**	Side Effects:
**		The message is written onto fp.
*/

/* values for output state variable */
#define OSTATE_HEAD	0	/* at beginning of line */
#define OSTATE_CR	1	/* read a carriage return */
#define OSTATE_INLINE	2	/* putting rest of line */

bool
putbody(mci, e, separator)
	register MCI *mci;
	register ENVELOPE *e;
	char *separator;
{
	bool dead = false;
	bool ioerr = false;
	int save_errno;
	char buf[MAXLINE];
#if MIME8TO7
	char *boundaries[MAXMIMENESTING + 1];
#endif /* MIME8TO7 */

	/*
	**  Output the body of the message
	*/

	if (e->e_dfp == NULL && bitset(EF_HAS_DF, e->e_flags))
	{
		char *df = queuename(e, DATAFL_LETTER);

		e->e_dfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, df,
				      SM_IO_RDONLY_B, NULL);
		if (e->e_dfp == NULL)
		{
			char *msg = "!putbody: Cannot open %s for %s from %s";

			if (errno == ENOENT)
				msg++;
			syserr(msg, df, e->e_to, e->e_from.q_paddr);
		}

	}
	if (e->e_dfp == NULL)
	{
		if (bitset(MCIF_INHEADER, mci->mci_flags))
		{
			if (!putline("", mci))
				goto writeerr;
			mci->mci_flags &= ~MCIF_INHEADER;
		}
		if (!putline("<<< No Message Collected >>>", mci))
			goto writeerr;
		goto endofmessage;
	}

	if (e->e_dfino == (ino_t) 0)
	{
		struct stat stbuf;

		if (fstat(sm_io_getinfo(e->e_dfp, SM_IO_WHAT_FD, NULL), &stbuf)
		    < 0)
			e->e_dfino = -1;
		else
		{
			e->e_dfdev = stbuf.st_dev;
			e->e_dfino = stbuf.st_ino;
		}
	}

	/* paranoia: the data file should always be in a rewound state */
	(void) bfrewind(e->e_dfp);

	/* simulate an I/O timeout when used as source */
	if (tTd(84, 101))
		sleep(319);

#if MIME8TO7
	if (bitset(MCIF_CVT8TO7, mci->mci_flags))
	{
		/*
		**  Do 8 to 7 bit MIME conversion.
		*/

		/* make sure it looks like a MIME message */
		if (hvalue("MIME-Version", e->e_header) == NULL &&
		    !putline("MIME-Version: 1.0", mci))
			goto writeerr;

		if (hvalue("Content-Type", e->e_header) == NULL)
		{
			(void) sm_snprintf(buf, sizeof(buf),
					   "Content-Type: text/plain; charset=%s",
					   defcharset(e));
			if (!putline(buf, mci))
				goto writeerr;
		}

		/* now do the hard work */
		boundaries[0] = NULL;
		mci->mci_flags |= MCIF_INHEADER;
		if (mime8to7(mci, e->e_header, e, boundaries, M87F_OUTER, 0) ==
								SM_IO_EOF)
			goto writeerr;
	}
# if MIME7TO8
	else if (bitset(MCIF_CVT7TO8, mci->mci_flags))
	{
		if (!mime7to8(mci, e->e_header, e))
			goto writeerr;
	}
# endif /* MIME7TO8 */
	else if (MaxMimeHeaderLength > 0 || MaxMimeFieldLength > 0)
	{
		bool oldsuprerrs = SuprErrs;

		/* Use mime8to7 to check multipart for MIME header overflows */
		boundaries[0] = NULL;
		mci->mci_flags |= MCIF_INHEADER;

		/*
		**  If EF_DONT_MIME is set, we have a broken MIME message
		**  and don't want to generate a new bounce message whose
		**  body propagates the broken MIME.  We can't just not call
		**  mime8to7() as is done above since we need the security
		**  checks.  The best we can do is suppress the errors.
		*/

		if (bitset(EF_DONT_MIME, e->e_flags))
			SuprErrs = true;

		if (mime8to7(mci, e->e_header, e, boundaries,
				M87F_OUTER|M87F_NO8TO7, 0) == SM_IO_EOF)
			goto writeerr;

		/* restore SuprErrs */
		SuprErrs = oldsuprerrs;
	}
	else
#endif /* MIME8TO7 */
	{
		int ostate;
		register char *bp;
		register char *pbp;
		register int c;
		register char *xp;
		int padc;
		char *buflim;
		int pos = 0;
		char peekbuf[12];

		if (bitset(MCIF_INHEADER, mci->mci_flags))
		{
			if (!putline("", mci))
				goto writeerr;
			mci->mci_flags &= ~MCIF_INHEADER;
		}

		/* determine end of buffer; allow for short mailer lines */
		buflim = &buf[sizeof(buf) - 1];
		if (mci->mci_mailer->m_linelimit > 0 &&
		    mci->mci_mailer->m_linelimit < sizeof(buf) - 1)
			buflim = &buf[mci->mci_mailer->m_linelimit - 1];

		/* copy temp file to output with mapping */
		ostate = OSTATE_HEAD;
		bp = buf;
		pbp = peekbuf;
		while (!sm_io_error(mci->mci_out) && !dead)
		{
			if (pbp > peekbuf)
				c = *--pbp;
			else if ((c = sm_io_getc(e->e_dfp, SM_TIME_DEFAULT))
				 == SM_IO_EOF)
				break;
			if (bitset(MCIF_7BIT, mci->mci_flags))
				c &= 0x7f;
			switch (ostate)
			{
			  case OSTATE_HEAD:
				if (c == '\0' &&
				    bitnset(M_NONULLS,
					    mci->mci_mailer->m_flags))
					break;
				if (c != '\r' && c != '\n' && bp < buflim)
				{
					*bp++ = c;
					break;
				}

				/* check beginning of line for special cases */
				*bp = '\0';
				pos = 0;
				padc = SM_IO_EOF;
				if (buf[0] == 'F' &&
				    bitnset(M_ESCFROM, mci->mci_mailer->m_flags)
				    && strncmp(buf, "From ", 5) == 0)
				{
					padc = '>';
				}
				if (buf[0] == '-' && buf[1] == '-' &&
				    separator != NULL)
				{
					/* possible separator */
					int sl = strlen(separator);

					if (strncmp(&buf[2], separator, sl)
					    == 0)
						padc = ' ';
				}
				if (buf[0] == '.' &&
				    bitnset(M_XDOT, mci->mci_mailer->m_flags))
				{
					padc = '.';
				}

				/* now copy out saved line */
				if (TrafficLogFile != NULL)
				{
					(void) sm_io_fprintf(TrafficLogFile,
							     SM_TIME_DEFAULT,
							     "%05d >>> ",
							     (int) CurrentPid);
					if (padc != SM_IO_EOF)
						(void) sm_io_putc(TrafficLogFile,
								  SM_TIME_DEFAULT,
								  padc);
					for (xp = buf; xp < bp; xp++)
						(void) sm_io_putc(TrafficLogFile,
								  SM_TIME_DEFAULT,
								  (unsigned char) *xp);
					if (c == '\n')
						(void) sm_io_fputs(TrafficLogFile,
								   SM_TIME_DEFAULT,
								   mci->mci_mailer->m_eol);
				}
				if (padc != SM_IO_EOF)
				{
					if (sm_io_putc(mci->mci_out,
						       SM_TIME_DEFAULT, padc)
					    == SM_IO_EOF)
					{
						dead = true;
						continue;
					}
					pos++;
				}
				for (xp = buf; xp < bp; xp++)
				{
					if (sm_io_putc(mci->mci_out,
						       SM_TIME_DEFAULT,
						       (unsigned char) *xp)
					    == SM_IO_EOF)
					{
						dead = true;
						break;
					}
				}
				if (dead)
					continue;
				if (c == '\n')
				{
					if (sm_io_fputs(mci->mci_out,
							SM_TIME_DEFAULT,
							mci->mci_mailer->m_eol)
							== SM_IO_EOF)
						break;
					pos = 0;
				}
				else
				{
					pos += bp - buf;
					if (c != '\r')
					{
						SM_ASSERT(pbp < peekbuf +
								sizeof(peekbuf));
						*pbp++ = c;
					}
				}

				bp = buf;

				/* determine next state */
				if (c == '\n')
					ostate = OSTATE_HEAD;
				else if (c == '\r')
					ostate = OSTATE_CR;
				else
					ostate = OSTATE_INLINE;
				continue;

			  case OSTATE_CR:
				if (c == '\n')
				{
					/* got CRLF */
					if (sm_io_fputs(mci->mci_out,
							SM_TIME_DEFAULT,
							mci->mci_mailer->m_eol)
							== SM_IO_EOF)
						continue;

					if (TrafficLogFile != NULL)
					{
						(void) sm_io_fputs(TrafficLogFile,
								   SM_TIME_DEFAULT,
								   mci->mci_mailer->m_eol);
					}
					pos = 0;
					ostate = OSTATE_HEAD;
					continue;
				}

				/* had a naked carriage return */
				SM_ASSERT(pbp < peekbuf + sizeof(peekbuf));
				*pbp++ = c;
				c = '\r';
				ostate = OSTATE_INLINE;
				goto putch;

			  case OSTATE_INLINE:
				if (c == '\r')
				{
					ostate = OSTATE_CR;
					continue;
				}
				if (c == '\0' &&
				    bitnset(M_NONULLS,
					    mci->mci_mailer->m_flags))
					break;
putch:
				if (mci->mci_mailer->m_linelimit > 0 &&
				    pos >= mci->mci_mailer->m_linelimit - 1 &&
				    c != '\n')
				{
					int d;

					/* check next character for EOL */
					if (pbp > peekbuf)
						d = *(pbp - 1);
					else if ((d = sm_io_getc(e->e_dfp,
								 SM_TIME_DEFAULT))
						 != SM_IO_EOF)
					{
						SM_ASSERT(pbp < peekbuf +
								sizeof(peekbuf));
						*pbp++ = d;
					}

					if (d == '\n' || d == SM_IO_EOF)
					{
						if (TrafficLogFile != NULL)
							(void) sm_io_putc(TrafficLogFile,
									  SM_TIME_DEFAULT,
									  (unsigned char) c);
						if (sm_io_putc(mci->mci_out,
							       SM_TIME_DEFAULT,
							       (unsigned char) c)
							       == SM_IO_EOF)
						{
							dead = true;
							continue;
						}
						pos++;
						continue;
					}

					if (sm_io_putc(mci->mci_out,
						       SM_TIME_DEFAULT, '!')
					    == SM_IO_EOF ||
					    sm_io_fputs(mci->mci_out,
							SM_TIME_DEFAULT,
							mci->mci_mailer->m_eol)
					    == SM_IO_EOF)
					{
						dead = true;
						continue;
					}

					if (TrafficLogFile != NULL)
					{
						(void) sm_io_fprintf(TrafficLogFile,
								     SM_TIME_DEFAULT,
								     "!%s",
								     mci->mci_mailer->m_eol);
					}
					ostate = OSTATE_HEAD;
					SM_ASSERT(pbp < peekbuf +
							sizeof(peekbuf));
					*pbp++ = c;
					continue;
				}
				if (c == '\n')
				{
					if (TrafficLogFile != NULL)
						(void) sm_io_fputs(TrafficLogFile,
								   SM_TIME_DEFAULT,
								   mci->mci_mailer->m_eol);
					if (sm_io_fputs(mci->mci_out,
							SM_TIME_DEFAULT,
							mci->mci_mailer->m_eol)
							== SM_IO_EOF)
						continue;
					pos = 0;
					ostate = OSTATE_HEAD;
				}
				else
				{
					if (TrafficLogFile != NULL)
						(void) sm_io_putc(TrafficLogFile,
								  SM_TIME_DEFAULT,
								  (unsigned char) c);
					if (sm_io_putc(mci->mci_out,
						       SM_TIME_DEFAULT,
						       (unsigned char) c)
					    == SM_IO_EOF)
					{
						dead = true;
						continue;
					}
					pos++;
					ostate = OSTATE_INLINE;
				}
				break;
			}
		}

		/* make sure we are at the beginning of a line */
		if (bp > buf)
		{
			if (TrafficLogFile != NULL)
			{
				for (xp = buf; xp < bp; xp++)
					(void) sm_io_putc(TrafficLogFile,
							  SM_TIME_DEFAULT,
							  (unsigned char) *xp);
			}
			for (xp = buf; xp < bp; xp++)
			{
				if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,
					       (unsigned char) *xp)
				    == SM_IO_EOF)
				{
					dead = true;
					break;
				}
			}
			pos += bp - buf;
		}
		if (!dead && pos > 0)
		{
			if (TrafficLogFile != NULL)
				(void) sm_io_fputs(TrafficLogFile,
						   SM_TIME_DEFAULT,
						   mci->mci_mailer->m_eol);
			if (sm_io_fputs(mci->mci_out, SM_TIME_DEFAULT,
					   mci->mci_mailer->m_eol) == SM_IO_EOF)
				goto writeerr;
		}
	}

	if (sm_io_error(e->e_dfp))
	{
		syserr("putbody: %s/%cf%s: read error",
		       qid_printqueue(e->e_dfqgrp, e->e_dfqdir),
		       DATAFL_LETTER, e->e_id);
		ExitStat = EX_IOERR;
		ioerr = true;
	}

endofmessage:
	/*
	**  Since mailfile() uses e_dfp in a child process,
	**  the file offset in the stdio library for the
	**  parent process will not agree with the in-kernel
	**  file offset since the file descriptor is shared
	**  between the processes.  Therefore, it is vital
	**  that the file always be rewound.  This forces the
	**  kernel offset (lseek) and stdio library (ftell)
	**  offset to match.
	*/

	save_errno = errno;
	if (e->e_dfp != NULL)
		(void) bfrewind(e->e_dfp);

	/* some mailers want extra blank line at end of message */
	if (!dead && bitnset(M_BLANKEND, mci->mci_mailer->m_flags) &&
	    buf[0] != '\0' && buf[0] != '\n')
	{
		if (!putline("", mci))
			goto writeerr;
	}

	if (!dead &&
	    (sm_io_flush(mci->mci_out, SM_TIME_DEFAULT) == SM_IO_EOF ||
	     (sm_io_error(mci->mci_out) && errno != EPIPE)))
	{
		save_errno = errno;
		syserr("putbody: write error");
		ExitStat = EX_IOERR;
		ioerr = true;
	}

	errno = save_errno;
	return !dead && !ioerr;

  writeerr:
	return false;
}

/*
**  MAILFILE -- Send a message to a file.
**
**	If the file has the set-user-ID/set-group-ID bits set, but NO
**	execute bits, sendmail will try to become the owner of that file
**	rather than the real user.  Obviously, this only works if
**	sendmail runs as root.
**
**	This could be done as a subordinate mailer, except that it
**	is used implicitly to save messages in ~/dead.letter.  We
**	view this as being sufficiently important as to include it
**	here.  For example, if the system is dying, we shouldn't have
**	to create another process plus some pipes to save the message.
**
**	Parameters:
**		filename -- the name of the file to send to.
**		mailer -- mailer definition for recipient -- if NULL,
**			use FileMailer.
**		ctladdr -- the controlling address header -- includes
**			the userid/groupid to be when sending.
**		sfflags -- flags for opening.
**		e -- the current envelope.
**
**	Returns:
**		The exit code associated with the operation.
**
**	Side Effects:
**		none.
*/

# define RETURN(st)			exit(st);

static jmp_buf	CtxMailfileTimeout;

int
mailfile(filename, mailer, ctladdr, sfflags, e)
	char *volatile filename;
	MAILER *volatile mailer;
	ADDRESS *ctladdr;
	volatile long sfflags;
	register ENVELOPE *e;
{
	register SM_FILE_T *f;
	register pid_t pid = -1;
	volatile int mode;
	int len;
	off_t curoff;
	bool suidwarn = geteuid() == 0;
	char *p;
	char *volatile realfile;
	SM_EVENT *ev;
	char buf[MAXPATHLEN];
	char targetfile[MAXPATHLEN];

	if (tTd(11, 1))
	{
		sm_dprintf("mailfile %s\n  ctladdr=", filename);
		printaddr(sm_debug_file(), ctladdr, false);
	}

	if (mailer == NULL)
		mailer = FileMailer;

	if (e->e_xfp != NULL)
		(void) sm_io_flush(e->e_xfp, SM_TIME_DEFAULT);

	/*
	**  Special case /dev/null.  This allows us to restrict file
	**  delivery to regular files only.
	*/

	if (sm_path_isdevnull(filename))
		return EX_OK;

	/* check for 8-bit available */
	if (bitset(EF_HAS8BIT, e->e_flags) &&
	    bitnset(M_7BITS, mailer->m_flags) &&
	    (bitset(EF_DONT_MIME, e->e_flags) ||
	     !(bitset(MM_MIME8BIT, MimeMode) ||
	       (bitset(EF_IS_MIME, e->e_flags) &&
		bitset(MM_CVTMIME, MimeMode)))))
	{
		e->e_status = "5.6.3";
		usrerrenh(e->e_status,
			  "554 Cannot send 8-bit data to 7-bit destination");
		errno = 0;
		return EX_DATAERR;
	}

	/* Find the actual file */
	if (SafeFileEnv != NULL && SafeFileEnv[0] != '\0')
	{
		len = strlen(SafeFileEnv);

		if (strncmp(SafeFileEnv, filename, len) == 0)
			filename += len;

		if (len + strlen(filename) + 1 >= sizeof(targetfile))
		{
			syserr("mailfile: filename too long (%s/%s)",
			       SafeFileEnv, filename);
			return EX_CANTCREAT;
		}
		(void) sm_strlcpy(targetfile, SafeFileEnv, sizeof(targetfile));
		realfile = targetfile + len;
		if (*filename == '/')
			filename++;
		if (*filename != '\0')
		{
			/* paranoia: trailing / should be removed in readcf */
			if (targetfile[len - 1] != '/')
				(void) sm_strlcat(targetfile,
						  "/", sizeof(targetfile));
			(void) sm_strlcat(targetfile, filename,
					  sizeof(targetfile));
		}
	}
	else if (mailer->m_rootdir != NULL)
	{
		expand(mailer->m_rootdir, targetfile, sizeof(targetfile), e);
		len = strlen(targetfile);

		if (strncmp(targetfile, filename, len) == 0)
			filename += len;

		if (len + strlen(filename) + 1 >= sizeof(targetfile))
		{
			syserr("mailfile: filename too long (%s/%s)",
			       targetfile, filename);
			return EX_CANTCREAT;
		}
		realfile = targetfile + len;
		if (targetfile[len - 1] != '/')
			(void) sm_strlcat(targetfile, "/", sizeof(targetfile));
		if (*filename == '/')
			(void) sm_strlcat(targetfile, filename + 1,
					  sizeof(targetfile));
		else
			(void) sm_strlcat(targetfile, filename,
					  sizeof(targetfile));
	}
	else
	{
		if (sm_strlcpy(targetfile, filename, sizeof(targetfile)) >=
		    sizeof(targetfile))
		{
			syserr("mailfile: filename too long (%s)", filename);
			return EX_CANTCREAT;
		}
		realfile = targetfile;
	}

	/*
	**  Fork so we can change permissions here.
	**	Note that we MUST use fork, not vfork, because of
	**	the complications of calling subroutines, etc.
	*/


	/*
	**  Dispose of SIGCHLD signal catchers that may be laying
	**  around so that the waitfor() below will get it.
	*/

	(void) sm_signal(SIGCHLD, SIG_DFL);

	DOFORK(fork);

	if (pid < 0)
		return EX_OSERR;
	else if (pid == 0)
	{
		/* child -- actually write to file */
		struct stat stb;
		MCI mcibuf;
		int err;
		volatile int oflags = O_WRONLY|O_APPEND;

		/* Reset global flags */
		RestartRequest = NULL;
		RestartWorkGroup = false;
		ShutdownRequest = NULL;
		PendingSignal = 0;
		CurrentPid = getpid();

		if (e->e_lockfp != NULL)
		{
			int fd;

			fd = sm_io_getinfo(e->e_lockfp, SM_IO_WHAT_FD, NULL);
			/* SM_ASSERT(fd >= 0); */
			if (fd >= 0)
				(void) close(fd);
		}

		(void) sm_signal(SIGINT, SIG_DFL);
		(void) sm_signal(SIGHUP, SIG_DFL);
		(void) sm_signal(SIGTERM, SIG_DFL);
		(void) umask(OldUmask);
		e->e_to = filename;
		ExitStat = EX_OK;

		if (setjmp(CtxMailfileTimeout) != 0)
		{
			RETURN(EX_TEMPFAIL);
		}

		if (TimeOuts.to_fileopen > 0)
			ev = sm_setevent(TimeOuts.to_fileopen, mailfiletimeout,
					 0);
		else
			ev = NULL;

		/* check file mode to see if set-user-ID */
		if (stat(targetfile, &stb) < 0)
			mode = FileMode;
		else
			mode = stb.st_mode;

		/* limit the errors to those actually caused in the child */
		errno = 0;
		ExitStat = EX_OK;

		/* Allow alias expansions to use the S_IS{U,G}ID bits */
		if ((ctladdr != NULL && !bitset(QALIAS, ctladdr->q_flags)) ||
		    bitset(SFF_RUNASREALUID, sfflags))
		{
			/* ignore set-user-ID and set-group-ID bits */
			mode &= ~(S_ISGID|S_ISUID);
			if (tTd(11, 20))
				sm_dprintf("mailfile: ignoring set-user-ID/set-group-ID bits\n");
		}

		/* we have to open the data file BEFORE setuid() */
		if (e->e_dfp == NULL && bitset(EF_HAS_DF, e->e_flags))
		{
			char *df = queuename(e, DATAFL_LETTER);

			e->e_dfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, df,
					      SM_IO_RDONLY_B, NULL);
			if (e->e_dfp == NULL)
			{
				syserr("mailfile: Cannot open %s for %s from %s",
					df, e->e_to, e->e_from.q_paddr);
			}
		}

		/* select a new user to run as */
		if (!bitset(SFF_RUNASREALUID, sfflags))
		{
			if (bitnset(M_SPECIFIC_UID, mailer->m_flags))
			{
				RealUserName = NULL;
				if (mailer->m_uid == NO_UID)
					RealUid = RunAsUid;
				else
					RealUid = mailer->m_uid;
				if (RunAsUid != 0 && RealUid != RunAsUid)
				{
					/* Only root can change the uid */
					syserr("mailfile: insufficient privileges to change uid, RunAsUid=%ld, RealUid=%ld",
						(long) RunAsUid, (long) RealUid);
					RETURN(EX_TEMPFAIL);
				}
			}
			else if (bitset(S_ISUID, mode))
			{
				RealUserName = NULL;
				RealUid = stb.st_uid;
			}
			else if (ctladdr != NULL && ctladdr->q_uid != 0)
			{
				if (ctladdr->q_ruser != NULL)
					RealUserName = ctladdr->q_ruser;
				else
					RealUserName = ctladdr->q_user;
				RealUid = ctladdr->q_uid;
			}
			else if (mailer != NULL && mailer->m_uid != NO_UID)
			{
				RealUserName = DefUser;
				RealUid = mailer->m_uid;
			}
			else
			{
				RealUserName = DefUser;
				RealUid = DefUid;
			}

			/* select a new group to run as */
			if (bitnset(M_SPECIFIC_UID, mailer->m_flags))
			{
				if (mailer->m_gid == NO_GID)
					RealGid = RunAsGid;
				else
					RealGid = mailer->m_gid;
				if (RunAsUid != 0 &&
				    (RealGid != getgid() ||
				     RealGid != getegid()))
				{
					/* Only root can change the gid */
					syserr("mailfile: insufficient privileges to change gid, RealGid=%ld, RunAsUid=%ld, gid=%ld, egid=%ld",
					       (long) RealGid, (long) RunAsUid,
					       (long) getgid(), (long) getegid());
					RETURN(EX_TEMPFAIL);
				}
			}
			else if (bitset(S_ISGID, mode))
				RealGid = stb.st_gid;
			else if (ctladdr != NULL &&
				 ctladdr->q_uid == DefUid &&
				 ctladdr->q_gid == 0)
			{
				/*
				**  Special case:  This means it is an
				**  alias and we should act as DefaultUser.
				**  See alias()'s comments.
				*/

				RealGid = DefGid;
				RealUserName = DefUser;
			}
			else if (ctladdr != NULL && ctladdr->q_uid != 0)
				RealGid = ctladdr->q_gid;
			else if (mailer != NULL && mailer->m_gid != NO_GID)
				RealGid = mailer->m_gid;
			else
				RealGid = DefGid;
		}

		/* last ditch */
		if (!bitset(SFF_ROOTOK, sfflags))
		{
			if (RealUid == 0)
				RealUid = DefUid;
			if (RealGid == 0)
				RealGid = DefGid;
		}

		/* set group id list (needs /etc/group access) */
		if (RealUserName != NULL && !DontInitGroups)
		{
			if (initgroups(RealUserName, RealGid) == -1 && suidwarn)
			{
				syserr("mailfile: initgroups(%s, %ld) failed",
					RealUserName, (long) RealGid);
				RETURN(EX_TEMPFAIL);
			}
		}
		else
		{
			GIDSET_T gidset[1];

			gidset[0] = RealGid;
			if (setgroups(1, gidset) == -1 && suidwarn)
			{
				syserr("mailfile: setgroups() failed");
				RETURN(EX_TEMPFAIL);
			}
		}

		/*
		**  If you have a safe environment, go into it.
		*/

		if (realfile != targetfile)
		{
			char save;

			save = *realfile;
			*realfile = '\0';
			if (tTd(11, 20))
				sm_dprintf("mailfile: chroot %s\n", targetfile);
			if (chroot(targetfile) < 0)
			{
				syserr("mailfile: Cannot chroot(%s)",
				       targetfile);
				RETURN(EX_CANTCREAT);
			}
			*realfile = save;
		}

		if (tTd(11, 40))
			sm_dprintf("mailfile: deliver to %s\n", realfile);

		if (chdir("/") < 0)
		{
			syserr("mailfile: cannot chdir(/)");
			RETURN(EX_CANTCREAT);
		}

		/* now reset the group and user ids */
		endpwent();
		sm_mbdb_terminate();
		if (setgid(RealGid) < 0 && suidwarn)
		{
			syserr("mailfile: setgid(%ld) failed", (long) RealGid);
			RETURN(EX_TEMPFAIL);
		}
		vendor_set_uid(RealUid);
		if (setuid(RealUid) < 0 && suidwarn)
		{
			syserr("mailfile: setuid(%ld) failed", (long) RealUid);
			RETURN(EX_TEMPFAIL);
		}

		if (tTd(11, 2))
			sm_dprintf("mailfile: running as r/euid=%ld/%ld, r/egid=%ld/%ld\n",
				(long) getuid(), (long) geteuid(),
				(long) getgid(), (long) getegid());


		/* move into some "safe" directory */
		if (mailer->m_execdir != NULL)
		{
			char *q;

			for (p = mailer->m_execdir; p != NULL; p = q)
			{
				q = strchr(p, ':');
				if (q != NULL)
					*q = '\0';
				expand(p, buf, sizeof(buf), e);
				if (q != NULL)
					*q++ = ':';
				if (tTd(11, 20))
					sm_dprintf("mailfile: trydir %s\n",
						   buf);
				if (buf[0] != '\0' && chdir(buf) >= 0)
					break;
			}
		}

		/*
		**  Recheck the file after we have assumed the ID of the
		**  delivery user to make sure we can deliver to it as
		**  that user.  This is necessary if sendmail is running
		**  as root and the file is on an NFS mount which treats
		**  root as nobody.
		*/

#if HASLSTAT
		if (bitnset(DBS_FILEDELIVERYTOSYMLINK, DontBlameSendmail))
			err = stat(realfile, &stb);
		else
			err = lstat(realfile, &stb);
#else /* HASLSTAT */
		err = stat(realfile, &stb);
#endif /* HASLSTAT */

		if (err < 0)
		{
			stb.st_mode = ST_MODE_NOFILE;
			mode = FileMode;
			oflags |= O_CREAT|O_EXCL;
		}
		else if (bitset(S_IXUSR|S_IXGRP|S_IXOTH, mode) ||
			 (!bitnset(DBS_FILEDELIVERYTOHARDLINK,
				   DontBlameSendmail) &&
			  stb.st_nlink != 1) ||
			 (realfile != targetfile && !S_ISREG(mode)))
			exit(EX_CANTCREAT);
		else
			mode = stb.st_mode;

		if (!bitnset(DBS_FILEDELIVERYTOSYMLINK, DontBlameSendmail))
			sfflags |= SFF_NOSLINK;
		if (!bitnset(DBS_FILEDELIVERYTOHARDLINK, DontBlameSendmail))
			sfflags |= SFF_NOHLINK;
		sfflags &= ~SFF_OPENASROOT;
		f = safefopen(realfile, oflags, mode, sfflags);
		if (f == NULL)
		{
			if (transienterror(errno))
			{
				usrerr("454 4.3.0 cannot open %s: %s",
				       shortenstring(realfile, MAXSHORTSTR),
				       sm_errstring(errno));
				RETURN(EX_TEMPFAIL);
			}
			else
			{
				usrerr("554 5.3.0 cannot open %s: %s",
				       shortenstring(realfile, MAXSHORTSTR),
				       sm_errstring(errno));
				RETURN(EX_CANTCREAT);
			}
		}
		if (filechanged(realfile, sm_io_getinfo(f, SM_IO_WHAT_FD, NULL),
		    &stb))
		{
			syserr("554 5.3.0 file changed after open");
			RETURN(EX_CANTCREAT);
		}
		if (fstat(sm_io_getinfo(f, SM_IO_WHAT_FD, NULL), &stb) < 0)
		{
			syserr("554 5.3.0 cannot fstat %s",
				sm_errstring(errno));
			RETURN(EX_CANTCREAT);
		}

		curoff = stb.st_size;

		if (ev != NULL)
			sm_clrevent(ev);

		memset(&mcibuf, '\0', sizeof(mcibuf));
		mcibuf.mci_mailer = mailer;
		mcibuf.mci_out = f;
		if (bitnset(M_7BITS, mailer->m_flags))
			mcibuf.mci_flags |= MCIF_7BIT;

		/* clear out per-message flags from connection structure */
		mcibuf.mci_flags &= ~(MCIF_CVT7TO8|MCIF_CVT8TO7);

		if (bitset(EF_HAS8BIT, e->e_flags) &&
		    !bitset(EF_DONT_MIME, e->e_flags) &&
		    bitnset(M_7BITS, mailer->m_flags))
			mcibuf.mci_flags |= MCIF_CVT8TO7;

#if MIME7TO8
		if (bitnset(M_MAKE8BIT, mailer->m_flags) &&
		    !bitset(MCIF_7BIT, mcibuf.mci_flags) &&
		    (p = hvalue("Content-Transfer-Encoding", e->e_header)) != NULL &&
		    (sm_strcasecmp(p, "quoted-printable") == 0 ||
		     sm_strcasecmp(p, "base64") == 0) &&
		    (p = hvalue("Content-Type", e->e_header)) != NULL)
		{
			/* may want to convert 7 -> 8 */
			/* XXX should really parse it here -- and use a class XXX */
			if (sm_strncasecmp(p, "text/plain", 10) == 0 &&
			    (p[10] == '\0' || p[10] == ' ' || p[10] == ';'))
				mcibuf.mci_flags |= MCIF_CVT7TO8;
		}
#endif /* MIME7TO8 */

		if (!putfromline(&mcibuf, e) ||
		    !(*e->e_puthdr)(&mcibuf, e->e_header, e, M87F_OUTER) ||
		    !(*e->e_putbody)(&mcibuf, e, NULL) ||
		    !putline("\n", &mcibuf) ||
		    (sm_io_flush(f, SM_TIME_DEFAULT) != 0 ||
		    (SuperSafe != SAFE_NO &&
		     fsync(sm_io_getinfo(f, SM_IO_WHAT_FD, NULL)) < 0) ||
		    sm_io_error(f)))
		{
			setstat(EX_IOERR);
#if !NOFTRUNCATE
			(void) ftruncate(sm_io_getinfo(f, SM_IO_WHAT_FD, NULL),
					 curoff);
#endif /* !NOFTRUNCATE */
		}

		/* reset ISUID & ISGID bits for paranoid systems */
#if HASFCHMOD
		(void) fchmod(sm_io_getinfo(f, SM_IO_WHAT_FD, NULL),
			      (MODE_T) mode);
#else /* HASFCHMOD */
		(void) chmod(filename, (MODE_T) mode);
#endif /* HASFCHMOD */
		if (sm_io_close(f, SM_TIME_DEFAULT) < 0)
			setstat(EX_IOERR);
		(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
		(void) setuid(RealUid);
		exit(ExitStat);
		/* NOTREACHED */
	}
	else
	{
		/* parent -- wait for exit status */
		int st;

		st = waitfor(pid);
		if (st == -1)
		{
			syserr("mailfile: %s: wait", mailer->m_name);
			return EX_SOFTWARE;
		}
		if (WIFEXITED(st))
		{
			errno = 0;
			return (WEXITSTATUS(st));
		}
		else
		{
			syserr("mailfile: %s: child died on signal %d",
			       mailer->m_name, st);
			return EX_UNAVAILABLE;
		}
		/* NOTREACHED */
	}
	return EX_UNAVAILABLE;	/* avoid compiler warning on IRIX */
}

static void
mailfiletimeout(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(CtxMailfileTimeout, 1);
}
/*
**  HOSTSIGNATURE -- return the "signature" for a host.
**
**	The signature describes how we are going to send this -- it
**	can be just the hostname (for non-Internet hosts) or can be
**	an ordered list of MX hosts.
**
**	Parameters:
**		m -- the mailer describing this host.
**		host -- the host name.
**
**	Returns:
**		The signature for this host.
**
**	Side Effects:
**		Can tweak the symbol table.
*/

#define MAXHOSTSIGNATURE	8192	/* max len of hostsignature */

char *
hostsignature(m, host)
	register MAILER *m;
	char *host;
{
	register char *p;
	register STAB *s;
	time_t now;
#if NAMED_BIND
	char sep = ':';
	char prevsep = ':';
	int i;
	int len;
	int nmx;
	int hl;
	char *hp;
	char *endp;
	int oldoptions = _res.options;
	char *mxhosts[MAXMXHOSTS + 1];
	unsigned short mxprefs[MAXMXHOSTS + 1];
#endif /* NAMED_BIND */

	if (tTd(17, 3))
		sm_dprintf("hostsignature(%s)\n", host);

	/*
	**  If local delivery (and not remote), just return a constant.
	*/

	if (bitnset(M_LOCALMAILER, m->m_flags) &&
	    strcmp(m->m_mailer, "[IPC]") != 0 &&
	    !(m->m_argv[0] != NULL && strcmp(m->m_argv[0], "TCP") == 0))
		return "localhost";

	/* an empty host does not have MX records */
	if (*host == '\0')
		return "_empty_";

	/*
	**  Check to see if this uses IPC -- if not, it can't have MX records.
	*/

	if (strcmp(m->m_mailer, "[IPC]") != 0 ||
	    CurEnv->e_sendmode == SM_DEFER)
	{
		/* just an ordinary mailer or deferred mode */
		return host;
	}
#if NETUNIX
	else if (m->m_argv[0] != NULL &&
		 strcmp(m->m_argv[0], "FILE") == 0)
	{
		/* rendezvous in the file system, no MX records */
		return host;
	}
#endif /* NETUNIX */

	/*
	**  Look it up in the symbol table.
	*/

	now = curtime();
	s = stab(host, ST_HOSTSIG, ST_ENTER);
	if (s->s_hostsig.hs_sig != NULL)
	{
		if (s->s_hostsig.hs_exp >= now)
		{
			if (tTd(17, 3))
				sm_dprintf("hostsignature(): stab(%s) found %s\n", host,
					   s->s_hostsig.hs_sig);
			return s->s_hostsig.hs_sig;
		}

		/* signature is expired: clear it */
		sm_free(s->s_hostsig.hs_sig);
		s->s_hostsig.hs_sig = NULL;
	}

	/* set default TTL */
	s->s_hostsig.hs_exp = now + SM_DEFAULT_TTL;

	/*
	**  Not already there or expired -- create a signature.
	*/

#if NAMED_BIND
	if (ConfigLevel < 2)
		_res.options &= ~(RES_DEFNAMES | RES_DNSRCH);	/* XXX */

	for (hp = host; hp != NULL; hp = endp)
	{
#if NETINET6
		if (*hp == '[')
		{
			endp = strchr(hp + 1, ']');
			if (endp != NULL)
				endp = strpbrk(endp + 1, ":,");
		}
		else
			endp = strpbrk(hp, ":,");
#else /* NETINET6 */
		endp = strpbrk(hp, ":,");
#endif /* NETINET6 */
		if (endp != NULL)
		{
			sep = *endp;
			*endp = '\0';
		}

		if (bitnset(M_NOMX, m->m_flags))
		{
			/* skip MX lookups */
			nmx = 1;
			mxhosts[0] = hp;
		}
		else
		{
			auto int rcode;
			int ttl;

			nmx = getmxrr(hp, mxhosts, mxprefs, true, &rcode, true,
				      &ttl);
			if (nmx <= 0)
			{
				int save_errno;
				register MCI *mci;

				/* update the connection info for this host */
				save_errno = errno;
				mci = mci_get(hp, m);
				mci->mci_errno = save_errno;
				mci->mci_herrno = h_errno;
				mci->mci_lastuse = now;
				if (rcode == EX_NOHOST)
					mci_setstat(mci, rcode, "5.1.2",
						    "550 Host unknown");
				else
					mci_setstat(mci, rcode, NULL, NULL);

				/* use the original host name as signature */
				nmx = 1;
				mxhosts[0] = hp;
			}
			if (tTd(17, 3))
				sm_dprintf("hostsignature(): getmxrr() returned %d, mxhosts[0]=%s\n",
					   nmx, mxhosts[0]);

			/*
			**  Set new TTL: we use only one!
			**	We could try to use the minimum instead.
			*/

			s->s_hostsig.hs_exp = now + SM_MIN(ttl, SM_DEFAULT_TTL);
		}

		len = 0;
		for (i = 0; i < nmx; i++)
			len += strlen(mxhosts[i]) + 1;
		if (s->s_hostsig.hs_sig != NULL)
			len += strlen(s->s_hostsig.hs_sig) + 1;
		if (len < 0 || len >= MAXHOSTSIGNATURE)
		{
			sm_syslog(LOG_WARNING, NOQID, "hostsignature for host '%s' exceeds maxlen (%d): %d",
				  host, MAXHOSTSIGNATURE, len);
			len = MAXHOSTSIGNATURE;
		}
		p = sm_pmalloc_x(len);
		if (s->s_hostsig.hs_sig != NULL)
		{
			(void) sm_strlcpy(p, s->s_hostsig.hs_sig, len);
			sm_free(s->s_hostsig.hs_sig); /* XXX */
			s->s_hostsig.hs_sig = p;
			hl = strlen(p);
			p += hl;
			*p++ = prevsep;
			len -= hl + 1;
		}
		else
			s->s_hostsig.hs_sig = p;
		for (i = 0; i < nmx; i++)
		{
			hl = strlen(mxhosts[i]);
			if (len - 1 < hl || len <= 1)
			{
				/* force to drop out of outer loop */
				len = -1;
				break;
			}
			if (i != 0)
			{
				if (mxprefs[i] == mxprefs[i - 1])
					*p++ = ',';
				else
					*p++ = ':';
				len--;
			}
			(void) sm_strlcpy(p, mxhosts[i], len);
			p += hl;
			len -= hl;
		}

		/*
		**  break out of loop if len exceeded MAXHOSTSIGNATURE
		**  because we won't have more space for further hosts
		**  anyway (separated by : in the .cf file).
		*/

		if (len < 0)
			break;
		if (endp != NULL)
			*endp++ = sep;
		prevsep = sep;
	}
	makelower(s->s_hostsig.hs_sig);
	if (ConfigLevel < 2)
		_res.options = oldoptions;
#else /* NAMED_BIND */
	/* not using BIND -- the signature is just the host name */
	/*
	**  'host' points to storage that will be freed after we are
	**  done processing the current envelope, so we copy it.
	*/
	s->s_hostsig.hs_sig = sm_pstrdup_x(host);
#endif /* NAMED_BIND */
	if (tTd(17, 1))
		sm_dprintf("hostsignature(%s) = %s\n", host, s->s_hostsig.hs_sig);
	return s->s_hostsig.hs_sig;
}
/*
**  PARSE_HOSTSIGNATURE -- parse the "signature" and return MX host array.
**
**	The signature describes how we are going to send this -- it
**	can be just the hostname (for non-Internet hosts) or can be
**	an ordered list of MX hosts which must be randomized for equal
**	MX preference values.
**
**	Parameters:
**		sig -- the host signature.
**		mxhosts -- array to populate.
**		mailer -- mailer.
**
**	Returns:
**		The number of hosts inserted into mxhosts array.
**
**	Side Effects:
**		Randomizes equal MX preference hosts in mxhosts.
*/

static int
parse_hostsignature(sig, mxhosts, mailer)
	char *sig;
	char **mxhosts;
	MAILER *mailer;
{
	unsigned short curpref = 0;
	int nmx = 0, i, j;	/* NOTE: i, j, and nmx must have same type */
	char *hp, *endp;
	unsigned short prefer[MAXMXHOSTS];
	long rndm[MAXMXHOSTS];

	for (hp = sig; hp != NULL; hp = endp)
	{
		char sep = ':';

#if NETINET6
		if (*hp == '[')
		{
			endp = strchr(hp + 1, ']');
			if (endp != NULL)
				endp = strpbrk(endp + 1, ":,");
		}
		else
			endp = strpbrk(hp, ":,");
#else /* NETINET6 */
		endp = strpbrk(hp, ":,");
#endif /* NETINET6 */
		if (endp != NULL)
		{
			sep = *endp;
			*endp = '\0';
		}

		mxhosts[nmx] = hp;
		prefer[nmx] = curpref;
		if (mci_match(hp, mailer))
			rndm[nmx] = 0;
		else
			rndm[nmx] = get_random();

		if (endp != NULL)
		{
			/*
			**  Since we don't have the original MX prefs,
			**  make our own.  If the separator is a ':', that
			**  means the preference for the next host will be
			**  higher than this one, so simply increment curpref.
			*/

			if (sep == ':')
				curpref++;

			*endp++ = sep;
		}
		if (++nmx >= MAXMXHOSTS)
			break;
	}

	/* sort the records using the random factor for equal preferences */
	for (i = 0; i < nmx; i++)
	{
		for (j = i + 1; j < nmx; j++)
		{
			/*
			**  List is already sorted by MX preference, only
			**  need to look for equal preference MX records
			*/

			if (prefer[i] < prefer[j])
				break;

			if (prefer[i] > prefer[j] ||
			    (prefer[i] == prefer[j] && rndm[i] > rndm[j]))
			{
				register unsigned short tempp;
				register long tempr;
				register char *temp1;

				tempp = prefer[i];
				prefer[i] = prefer[j];
				prefer[j] = tempp;
				temp1 = mxhosts[i];
				mxhosts[i] = mxhosts[j];
				mxhosts[j] = temp1;
				tempr = rndm[i];
				rndm[i] = rndm[j];
				rndm[j] = tempr;
			}
		}
	}
	return nmx;
}

# if STARTTLS
static SSL_CTX	*clt_ctx = NULL;
static bool	tls_ok_clt = true;

/*
**  SETCLTTLS -- client side TLS: allow/disallow.
**
**	Parameters:
**		tls_ok -- should tls be done?
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets tls_ok_clt (static variable in this module)
*/

void
setclttls(tls_ok)
	bool tls_ok;
{
	tls_ok_clt = tls_ok;
	return;
}
/*
**  INITCLTTLS -- initialize client side TLS
**
**	Parameters:
**		tls_ok -- should tls initialization be done?
**
**	Returns:
**		succeeded?
**
**	Side Effects:
**		sets tls_ok_clt (static variable in this module)
*/

bool
initclttls(tls_ok)
	bool tls_ok;
{
	if (!tls_ok_clt)
		return false;
	tls_ok_clt = tls_ok;
	if (!tls_ok_clt)
		return false;
	if (clt_ctx != NULL)
		return true;	/* already done */
	tls_ok_clt = inittls(&clt_ctx, TLS_I_CLT, Clt_SSL_Options, false,
			     CltCertFile, CltKeyFile,
			     CACertPath, CACertFile, DHParams);
	return tls_ok_clt;
}

/*
**  STARTTLS -- try to start secure connection (client side)
**
**	Parameters:
**		m -- the mailer.
**		mci -- the mailer connection info.
**		e -- the envelope.
**
**	Returns:
**		success?
**		(maybe this should be some other code than EX_
**		that denotes which stage failed.)
*/

static int
starttls(m, mci, e)
	MAILER *m;
	MCI *mci;
	ENVELOPE *e;
{
	int smtpresult;
	int result = 0;
	int rfd, wfd;
	SSL *clt_ssl = NULL;
	time_t tlsstart;

	if (clt_ctx == NULL && !initclttls(true))
		return EX_TEMPFAIL;

# if USE_OPENSSL_ENGINE
	if (!SSLEngineInitialized && !SSL_set_engine(NULL))
	{
		sm_syslog(LOG_ERR, NOQID,
			  "STARTTLS=client, SSL_set_engine=failed");
		return EX_TEMPFAIL;
	}
	SSLEngineInitialized = true;
# endif /* USE_OPENSSL_ENGINE */

	smtpmessage("STARTTLS", m, mci);

	/* get the reply */
	smtpresult = reply(m, mci, e, TimeOuts.to_starttls, NULL, NULL,
			XS_STARTTLS);

	/* check return code from server */
	if (REPLYTYPE(smtpresult) == 4)
		return EX_TEMPFAIL;
	if (smtpresult == 501)
		return EX_USAGE;
	if (smtpresult == -1)
		return smtpresult;

	/* not an expected reply but we have to deal with it */
	if (REPLYTYPE(smtpresult) == 5)
		return EX_UNAVAILABLE;
	if (smtpresult != 220)
		return EX_PROTOCOL;

	if (LogLevel > 13)
		sm_syslog(LOG_INFO, NOQID, "STARTTLS=client, start=ok");

	/* start connection */
	if ((clt_ssl = SSL_new(clt_ctx)) == NULL)
	{
		if (LogLevel > 5)
		{
			sm_syslog(LOG_ERR, NOQID,
				  "STARTTLS=client, error: SSL_new failed");
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, "client");
		}
		return EX_SOFTWARE;
	}
	/* SSL_clear(clt_ssl); ? */

	if (get_tls_se_options(e, clt_ssl, false) != 0)
	{
		sm_syslog(LOG_ERR, NOQID,
			  "STARTTLS=client, get_tls_se_options=fail");
		return EX_SOFTWARE;
	}

	rfd = sm_io_getinfo(mci->mci_in, SM_IO_WHAT_FD, NULL);
	wfd = sm_io_getinfo(mci->mci_out, SM_IO_WHAT_FD, NULL);

	if (rfd < 0 || wfd < 0 ||
	    (result = SSL_set_rfd(clt_ssl, rfd)) != 1 ||
	    (result = SSL_set_wfd(clt_ssl, wfd)) != 1)
	{
		if (LogLevel > 5)
		{
			sm_syslog(LOG_ERR, NOQID,
				  "STARTTLS=client, error: SSL_set_xfd failed=%d",
				  result);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, "client");
		}
		return EX_SOFTWARE;
	}
	SSL_set_connect_state(clt_ssl);
	tlsstart = curtime();

ssl_retry:
	if ((result = SSL_connect(clt_ssl)) <= 0)
	{
		int i, ssl_err;
		int save_errno = errno;

		ssl_err = SSL_get_error(clt_ssl, result);
		i = tls_retry(clt_ssl, rfd, wfd, tlsstart,
			TimeOuts.to_starttls, ssl_err, "client");
		if (i > 0)
			goto ssl_retry;

		if (LogLevel > 5)
		{
			unsigned long l;
			const char *sr;

			l = ERR_peek_error();
			sr = ERR_reason_error_string(l);
			sm_syslog(LOG_WARNING, NOQID,
				  "STARTTLS=client, error: connect failed=%d, reason=%s, SSL_error=%d, errno=%d, retry=%d",
				  result, sr == NULL ? "unknown" : sr, ssl_err,
				  save_errno, i);
			if (LogLevel > 9)
				tlslogerr(LOG_WARNING, "client");
		}

		SSL_free(clt_ssl);
		clt_ssl = NULL;
		return EX_SOFTWARE;
	}
	mci->mci_ssl = clt_ssl;
	result = tls_get_info(mci->mci_ssl, false, mci->mci_host,
			      &mci->mci_macro, true);

	/* switch to use TLS... */
	if (sfdctls(&mci->mci_in, &mci->mci_out, mci->mci_ssl) == 0)
		return EX_OK;

	/* failure */
	SSL_free(clt_ssl);
	clt_ssl = NULL;
	return EX_SOFTWARE;
}
/*
**  ENDTLSCLT -- shutdown secure connection (client side)
**
**	Parameters:
**		mci -- the mailer connection info.
**
**	Returns:
**		success?
*/

static int
endtlsclt(mci)
	MCI *mci;
{
	int r;

	if (!bitset(MCIF_TLSACT, mci->mci_flags))
		return EX_OK;
	r = endtls(mci->mci_ssl, "client");
	mci->mci_flags &= ~MCIF_TLSACT;
	return r;
}
# endif /* STARTTLS */
# if STARTTLS || SASL
/*
**  ISCLTFLGSET -- check whether client flag is set.
**
**	Parameters:
**		e -- envelope.
**		flag -- flag to check in {client_flags}
**
**	Returns:
**		true iff flag is set.
*/

static bool
iscltflgset(e, flag)
	ENVELOPE *e;
	int flag;
{
	char *p;

	p = macvalue(macid("{client_flags}"), e);
	if (p == NULL)
		return false;
	for (; *p != '\0'; p++)
	{
		/* look for just this one flag */
		if (*p == (char) flag)
			return true;
	}
	return false;
}
# endif /* STARTTLS || SASL */
