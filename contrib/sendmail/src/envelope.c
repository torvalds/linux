/*
 * Copyright (c) 1998-2003, 2006 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: envelope.c,v 8.313 2013-11-22 20:51:55 ca Exp $")

/*
**  CLRSESSENVELOPE -- clear session oriented data in an envelope
**
**	Parameters:
**		e -- the envelope to clear.
**
**	Returns:
**		none.
*/

void
clrsessenvelope(e)
	ENVELOPE *e;
{
#if SASL
	macdefine(&e->e_macro, A_PERM, macid("{auth_type}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{auth_authen}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{auth_author}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{auth_ssf}"), "");
#endif /* SASL */
#if STARTTLS
	macdefine(&e->e_macro, A_PERM, macid("{cert_issuer}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{cert_subject}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{cipher_bits}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{cipher}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{tls_version}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{verify}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{alg_bits}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{cn_issuer}"), "");
	macdefine(&e->e_macro, A_PERM, macid("{cn_subject}"), "");
#endif /* STARTTLS */
}

/*
**  NEWENVELOPE -- fill in a new envelope
**
**	Supports inheritance.
**
**	Parameters:
**		e -- the new envelope to fill in.
**		parent -- the envelope to be the parent of e.
**		rpool -- either NULL, or a pointer to a resource pool
**			from which envelope memory is allocated, and
**			to which envelope resources are attached.
**
**	Returns:
**		e.
**
**	Side Effects:
**		none.
*/

ENVELOPE *
newenvelope(e, parent, rpool)
	register ENVELOPE *e;
	register ENVELOPE *parent;
	SM_RPOOL_T *rpool;
{
	int sendmode;

	/*
	**  This code used to read:
	**	if (e == parent && e->e_parent != NULL)
	**		parent = e->e_parent;
	**  So if e == parent && e->e_parent == NULL then we would
	**  set e->e_parent = e, which creates a loop in the e_parent chain.
	**  This meant macvalue() could go into an infinite loop.
	*/

	if (parent != NULL)
		sendmode = parent->e_sendmode;
	else
		sendmode = DM_NOTSET;

	if (e == parent)
		parent = e->e_parent;
	clearenvelope(e, true, rpool);
	if (e == CurEnv)
		memmove((char *) &e->e_from,
			(char *) &NullAddress,
			sizeof(e->e_from));
	else
		memmove((char *) &e->e_from,
			(char *) &CurEnv->e_from,
			sizeof(e->e_from));
	e->e_parent = parent;
	assign_queueid(e);
	e->e_ctime = curtime();
#if _FFR_SESSID
	e->e_sessid = e->e_id;
#endif /* _FFR_SESSID */
	if (parent != NULL)
	{
		e->e_msgpriority = parent->e_msgsize;
#if _FFR_SESSID
		if (parent->e_sessid != NULL)
			e->e_sessid = sm_rpool_strdup_x(rpool,
							parent->e_sessid);
#endif /* _FFR_SESSID */

		if (parent->e_quarmsg == NULL)
		{
			e->e_quarmsg = NULL;
			macdefine(&e->e_macro, A_PERM,
				  macid("{quarantine}"), "");
		}
		else
		{
			e->e_quarmsg = sm_rpool_strdup_x(rpool,
							 parent->e_quarmsg);
			macdefine(&e->e_macro, A_PERM,
				  macid("{quarantine}"), e->e_quarmsg);
		}
	}
	e->e_puthdr = putheader;
	e->e_putbody = putbody;
	if (CurEnv->e_xfp != NULL)
		(void) sm_io_flush(CurEnv->e_xfp, SM_TIME_DEFAULT);
	if (sendmode != DM_NOTSET)
		set_delivery_mode(sendmode, e);

	return e;
}

/* values for msg_timeout, see also IS_* below for usage (bit layout) */
#define MSG_T_O		0x01	/* normal timeout */
#define MSG_T_O_NOW	0x02	/* NOW timeout */
#define MSG_NOT_BY	0x04	/* Deliver-By time exceeded, mode R */
#define MSG_WARN	0x10	/* normal queue warning */
#define MSG_WARN_BY	0x20	/* Deliver-By time exceeded, mode N */

#define IS_MSG_ERR(x)	(((x) & 0x0f) != 0)	/* return an error */

/* immediate return */
#define IS_IMM_RET(x)	(((x) & (MSG_T_O_NOW|MSG_NOT_BY)) != 0)
#define IS_MSG_WARN(x)	(((x) & 0xf0) != 0)	/* return a warning */

/*
**  DROPENVELOPE -- deallocate an envelope.
**
**	Parameters:
**		e -- the envelope to deallocate.
**		fulldrop -- if set, do return receipts.
**		split -- if true, split by recipient if message is queued up
**
**	Returns:
**		EX_* status (currently: 0: success, EX_IOERR on panic)
**
**	Side Effects:
**		housekeeping necessary to dispose of an envelope.
**		Unlocks this queue file.
*/

int
dropenvelope(e, fulldrop, split)
	register ENVELOPE *e;
	bool fulldrop;
	bool split;
{
	bool panic = false;
	bool queueit = false;
	int msg_timeout = 0;
	bool failure_return = false;
	bool delay_return = false;
	bool success_return = false;
	bool pmnotify = bitset(EF_PM_NOTIFY, e->e_flags);
	bool done = false;
	register ADDRESS *q;
	char *id = e->e_id;
	time_t now;
	char buf[MAXLINE];

	if (tTd(50, 1))
	{
		sm_dprintf("dropenvelope %p: id=", e);
		xputs(sm_debug_file(), e->e_id);
		sm_dprintf(", flags=");
		printenvflags(e);
		if (tTd(50, 10))
		{
			sm_dprintf("sendq=");
			printaddr(sm_debug_file(), e->e_sendqueue, true);
		}
	}

	if (LogLevel > 84)
		sm_syslog(LOG_DEBUG, id,
			  "dropenvelope, e_flags=0x%lx, OpMode=%c, pid=%d",
			  e->e_flags, OpMode, (int) CurrentPid);

	/* we must have an id to remove disk files */
	if (id == NULL)
		return EX_OK;

	/* if verify-only mode, we can skip most of this */
	if (OpMode == MD_VERIFY)
		goto simpledrop;

	if (tTd(92, 2))
		sm_dprintf("dropenvelope: e_id=%s, EF_LOGSENDER=%d, LogLevel=%d\n",
			e->e_id, bitset(EF_LOGSENDER, e->e_flags), LogLevel);
	if (LogLevel > 4 && bitset(EF_LOGSENDER, e->e_flags))
		logsender(e, NULL);
	e->e_flags &= ~EF_LOGSENDER;

	/* post statistics */
	poststats(StatFile);

	/*
	**  Extract state information from dregs of send list.
	*/

	now = curtime();
	if (now >= e->e_ctime + TimeOuts.to_q_return[e->e_timeoutclass])
		msg_timeout = MSG_T_O;
	if (IS_DLVR_RETURN(e) && e->e_deliver_by > 0 &&
	    now >= e->e_ctime + e->e_deliver_by &&
	    !bitset(EF_RESPONSE, e->e_flags))
	{
		msg_timeout = MSG_NOT_BY;
		e->e_flags |= EF_FATALERRS|EF_CLRQUEUE;
	}
	else if (TimeOuts.to_q_return[e->e_timeoutclass] == NOW &&
		 !bitset(EF_RESPONSE, e->e_flags))
	{
		msg_timeout = MSG_T_O_NOW;
		e->e_flags |= EF_FATALERRS|EF_CLRQUEUE;
	}

#if _FFR_PROXY
	if (tTd(87, 2))
	{
		q = e->e_sendqueue;
		sm_dprintf("dropenvelope: mode=%c, e=%p, sibling=%p, nrcpts=%d, sendqueue=%p, next=%p, state=%d\n",
			e->e_sendmode, e, e->e_sibling, e->e_nrcpts, q,
			(q == NULL) ? (void *)0 : q->q_next,
			(q == NULL) ? -1 : q->q_state);
	}
#endif /* _FFR_PROXY */

	e->e_flags &= ~EF_QUEUERUN;
	for (q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_UNDELIVERED(q->q_state))
			queueit = true;

#if _FFR_PROXY
		if (queueit && e->e_sendmode == SM_PROXY)
			queueit = false;
#endif /* _FFR_PROXY */

		/* see if a notification is needed */
		if (bitset(QPINGONFAILURE, q->q_flags) &&
		    ((IS_MSG_ERR(msg_timeout) &&
		      QS_IS_UNDELIVERED(q->q_state)) ||
		     QS_IS_BADADDR(q->q_state) ||
		     IS_IMM_RET(msg_timeout)))
		{
			failure_return = true;
			if (!done && q->q_owner == NULL &&
			    !emptyaddr(&e->e_from))
			{
				(void) sendtolist(e->e_from.q_paddr, NULLADDR,
						  &e->e_errorqueue, 0, e);
				done = true;
			}
		}
		else if ((bitset(QPINGONSUCCESS, q->q_flags) &&
			  ((QS_IS_SENT(q->q_state) &&
			    bitnset(M_LOCALMAILER, q->q_mailer->m_flags)) ||
			   bitset(QRELAYED|QEXPANDED|QDELIVERED, q->q_flags))) ||
			  bitset(QBYTRACE, q->q_flags) ||
			  bitset(QBYNRELAY, q->q_flags))
		{
			success_return = true;
		}
	}

	if (e->e_class < 0)
		e->e_flags |= EF_NO_BODY_RETN;

	/*
	**  See if the message timed out.
	*/

	if (!queueit)
		/* EMPTY */
		/* nothing to do */ ;
	else if (IS_MSG_ERR(msg_timeout))
	{
		if (failure_return)
		{
			if (msg_timeout == MSG_NOT_BY)
			{
				(void) sm_snprintf(buf, sizeof(buf),
					"delivery time expired %lds",
					e->e_deliver_by);
			}
			else
			{
				(void) sm_snprintf(buf, sizeof(buf),
					"Cannot send message for %s",
					pintvl(TimeOuts.to_q_return[e->e_timeoutclass],
						false));
			}

			/* don't free, allocated from e_rpool */
			e->e_message = sm_rpool_strdup_x(e->e_rpool, buf);
			message(buf);
			e->e_flags |= EF_CLRQUEUE;
		}
		if (msg_timeout == MSG_NOT_BY)
		{
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
				"Delivery time (%lds) expired\n",
				e->e_deliver_by);
		}
		else
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
				"Message could not be delivered for %s\n",
				pintvl(TimeOuts.to_q_return[e->e_timeoutclass],
					false));
		(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
			"Message will be deleted from queue\n");
		for (q = e->e_sendqueue; q != NULL; q = q->q_next)
		{
			if (QS_IS_UNDELIVERED(q->q_state))
			{
				q->q_state = QS_BADADDR;
				if (msg_timeout == MSG_NOT_BY)
					q->q_status = "5.4.7";
				else
					q->q_status = "4.4.7";
			}
		}
	}
	else
	{
		if (TimeOuts.to_q_warning[e->e_timeoutclass] > 0 &&
		    now >= e->e_ctime +
				TimeOuts.to_q_warning[e->e_timeoutclass])
			msg_timeout = MSG_WARN;
		else if (IS_DLVR_NOTIFY(e) &&
			 e->e_deliver_by > 0 &&
			 now >= e->e_ctime + e->e_deliver_by)
			msg_timeout = MSG_WARN_BY;

		if (IS_MSG_WARN(msg_timeout))
		{
			if (!bitset(EF_WARNING|EF_RESPONSE, e->e_flags) &&
			    e->e_class >= 0 &&
			    e->e_from.q_paddr != NULL &&
			    strcmp(e->e_from.q_paddr, "<>") != 0 &&
			    sm_strncasecmp(e->e_from.q_paddr, "owner-", 6) != 0 &&
			    (strlen(e->e_from.q_paddr) <= 8 ||
			     sm_strcasecmp(&e->e_from.q_paddr[strlen(e->e_from.q_paddr) - 8],
					   "-request") != 0))
			{
				for (q = e->e_sendqueue; q != NULL;
				     q = q->q_next)
				{
					if (QS_IS_UNDELIVERED(q->q_state)
#if _FFR_NODELAYDSN_ON_HOLD
					    && !bitnset(M_HOLD,
							q->q_mailer->m_flags)
#endif /* _FFR_NODELAYDSN_ON_HOLD */
					   )
					{
						if (msg_timeout ==
						    MSG_WARN_BY &&
						    (bitset(QPINGONDELAY,
							    q->q_flags) ||
						    !bitset(QHASNOTIFY,
							    q->q_flags))
						   )
						{
							q->q_flags |= QBYNDELAY;
							delay_return = true;
						}
						if (bitset(QPINGONDELAY,
							   q->q_flags))
						{
							q->q_flags |= QDELAYED;
							delay_return = true;
						}
					}
				}
			}
			if (delay_return)
			{
				if (msg_timeout == MSG_WARN_BY)
				{
					(void) sm_snprintf(buf, sizeof(buf),
						"Warning: Delivery time (%lds) exceeded",
						e->e_deliver_by);
				}
				else
					(void) sm_snprintf(buf, sizeof(buf),
						"Warning: could not send message for past %s",
						pintvl(TimeOuts.to_q_warning[e->e_timeoutclass],
							false));

				/* don't free, allocated from e_rpool */
				e->e_message = sm_rpool_strdup_x(e->e_rpool,
								 buf);
				message(buf);
				e->e_flags |= EF_WARNING;
			}
			if (msg_timeout == MSG_WARN_BY)
			{
				(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					"Warning: Delivery time (%lds) exceeded\n",
					e->e_deliver_by);
			}
			else
				(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					"Warning: message still undelivered after %s\n",
					pintvl(TimeOuts.to_q_warning[e->e_timeoutclass],
					     false));
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
				      "Will keep trying until message is %s old\n",
				      pintvl(TimeOuts.to_q_return[e->e_timeoutclass],
					     false));
		}
	}

	if (tTd(50, 2))
		sm_dprintf("failure_return=%d delay_return=%d success_return=%d queueit=%d\n",
			failure_return, delay_return, success_return, queueit);

	/*
	**  If we had some fatal error, but no addresses are marked as
	**  bad, mark them _all_ as bad.
	*/

	if (bitset(EF_FATALERRS, e->e_flags) && !failure_return)
	{
		for (q = e->e_sendqueue; q != NULL; q = q->q_next)
		{
			if ((QS_IS_OK(q->q_state) ||
			     QS_IS_VERIFIED(q->q_state)) &&
			    bitset(QPINGONFAILURE, q->q_flags))
			{
				failure_return = true;
				q->q_state = QS_BADADDR;
			}
		}
	}

	/*
	**  Send back return receipts as requested.
	*/

	if (success_return && !failure_return && !delay_return && fulldrop &&
	    !bitset(PRIV_NORECEIPTS, PrivacyFlags) &&
	    strcmp(e->e_from.q_paddr, "<>") != 0)
	{
		auto ADDRESS *rlist = NULL;

		if (tTd(50, 8))
			sm_dprintf("dropenvelope(%s): sending return receipt\n",
				id);
		e->e_flags |= EF_SENDRECEIPT;
		(void) sendtolist(e->e_from.q_paddr, NULLADDR, &rlist, 0, e);
		(void) returntosender("Return receipt", rlist, RTSF_NO_BODY, e);
	}
	e->e_flags &= ~EF_SENDRECEIPT;

	/*
	**  Arrange to send error messages if there are fatal errors.
	*/

	if ((failure_return || delay_return) && e->e_errormode != EM_QUIET)
	{
		if (tTd(50, 8))
			sm_dprintf("dropenvelope(%s): saving mail\n", id);
		panic = savemail(e, !bitset(EF_NO_BODY_RETN, e->e_flags));
	}

	/*
	**  Arrange to send warning messages to postmaster as requested.
	*/

	if ((failure_return || pmnotify) &&
	    PostMasterCopy != NULL &&
	    !bitset(EF_RESPONSE, e->e_flags) &&
	    e->e_class >= 0)
	{
		auto ADDRESS *rlist = NULL;
		char pcopy[MAXNAME];

		if (failure_return)
		{
			expand(PostMasterCopy, pcopy, sizeof(pcopy), e);

			if (tTd(50, 8))
				sm_dprintf("dropenvelope(%s): sending postmaster copy to %s\n",
					id, pcopy);
			(void) sendtolist(pcopy, NULLADDR, &rlist, 0, e);
		}
		if (pmnotify)
			(void) sendtolist("postmaster", NULLADDR,
					  &rlist, 0, e);
		(void) returntosender(e->e_message, rlist,
				      RTSF_PM_BOUNCE|RTSF_NO_BODY, e);
	}

	/*
	**  Instantiate or deinstantiate the queue.
	*/

simpledrop:
	if (tTd(50, 8))
		sm_dprintf("dropenvelope(%s): at simpledrop, queueit=%d\n",
			id, queueit);
	if (!queueit || bitset(EF_CLRQUEUE, e->e_flags))
	{
		if (tTd(50, 1))
		{
			sm_dprintf("\n===== Dropping queue files for %s... queueit=%d, e_flags=",
				e->e_id, queueit);
			printenvflags(e);
		}
		if (!panic)
		{
			if (e->e_dfp != NULL)
			{
				(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
				e->e_dfp = NULL;
			}
			(void) xunlink(queuename(e, DATAFL_LETTER));
		}
		if (panic && QueueMode == QM_LOST)
		{
			/*
			**  leave the Qf file behind as
			**  the delivery attempt failed.
			*/

			/* EMPTY */
		}
		else
		if (xunlink(queuename(e, ANYQFL_LETTER)) == 0)
		{
			/* add to available space in filesystem */
			updfs(e, -1, panic ? 0 : -1, "dropenvelope");
		}

		if (e->e_ntries > 0 && LogLevel > 9)
			sm_syslog(LOG_INFO, id, "done; delay=%s, ntries=%d",
				  pintvl(curtime() - e->e_ctime, true),
				  e->e_ntries);
	}
	else if (queueit || !bitset(EF_INQUEUE, e->e_flags))
	{
		if (!split)
			queueup(e, false, true);
		else
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
				syserr("!dropenvelope(%s): cannot commit data file %s, uid=%ld",
					e->e_id, queuename(e, DATAFL_LETTER),
					(long) geteuid());
			}
			for (ee = e->e_sibling; ee != NULL; ee = ee->e_sibling)
				queueup(ee, false, true);
			queueup(e, false, true);

			/* clean up */
			for (ee = e->e_sibling; ee != NULL; ee = ee->e_sibling)
			{
				/* now unlock the job */
				if (tTd(50, 8))
					sm_dprintf("dropenvelope(%s): unlocking job\n",
						   ee->e_id);
				closexscript(ee);
				unlockqueue(ee);

				/* this envelope is marked unused */
				if (ee->e_dfp != NULL)
				{
					(void) sm_io_close(ee->e_dfp,
							   SM_TIME_DEFAULT);
					ee->e_dfp = NULL;
				}
				ee->e_id = NULL;
				ee->e_flags &= ~EF_HAS_DF;
			}
			e->e_sibling = oldsib;
		}
	}

	/* now unlock the job */
	if (tTd(50, 8))
		sm_dprintf("dropenvelope(%s): unlocking job\n", id);
	closexscript(e);
	unlockqueue(e);

	/* make sure that this envelope is marked unused */
	if (e->e_dfp != NULL)
	{
		(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
		e->e_dfp = NULL;
	}
	e->e_id = NULL;
	e->e_flags &= ~EF_HAS_DF;
	if (panic)
		return EX_IOERR;
	return EX_OK;
}

/*
**  CLEARENVELOPE -- clear an envelope without unlocking
**
**	This is normally used by a child process to get a clean
**	envelope without disturbing the parent.
**
**	Parameters:
**		e -- the envelope to clear.
**		fullclear - if set, the current envelope is total
**			garbage and should be ignored; otherwise,
**			release any resources it may indicate.
**		rpool -- either NULL, or a pointer to a resource pool
**			from which envelope memory is allocated, and
**			to which envelope resources are attached.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Closes files associated with the envelope.
**		Marks the envelope as unallocated.
*/

void
clearenvelope(e, fullclear, rpool)
	register ENVELOPE *e;
	bool fullclear;
	SM_RPOOL_T *rpool;
{
	register HDR *bh;
	register HDR **nhp;
	extern ENVELOPE BlankEnvelope;
	char **p;

	if (!fullclear)
	{
		/* clear out any file information */
		if (e->e_xfp != NULL)
			(void) sm_io_close(e->e_xfp, SM_TIME_DEFAULT);
		if (e->e_dfp != NULL)
			(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
		e->e_xfp = e->e_dfp = NULL;
	}

	/*
	**  Copy BlankEnvelope into *e.
	**  It is not safe to simply copy pointers to strings;
	**  the strings themselves must be copied (or set to NULL).
	**  The problem is that when we assign a new string value to
	**  a member of BlankEnvelope, we free the old string.
	**  We did not need to do this copying in sendmail 8.11 :-(
	**  and it is a potential performance hit.  Reference counted
	**  strings are one way out.
	*/

	*e = BlankEnvelope;
	e->e_message = NULL;
	e->e_qfletter = '\0';
	e->e_quarmsg = NULL;
	macdefine(&e->e_macro, A_PERM, macid("{quarantine}"), "");

	/*
	**  Copy the macro table.
	**  We might be able to avoid this by zeroing the macro table
	**  and always searching BlankEnvelope.e_macro after e->e_macro
	**  in macvalue().
	*/

	for (p = &e->e_macro.mac_table[0];
	     p <= &e->e_macro.mac_table[MAXMACROID];
	     ++p)
	{
		if (*p != NULL)
			*p = sm_rpool_strdup_x(rpool, *p);
	}

	/*
	**  XXX There are many strings in the envelope structure
	**  XXX that we are not attempting to copy here.
	**  XXX Investigate this further.
	*/

	e->e_rpool = rpool;
	e->e_macro.mac_rpool = rpool;
	if (Verbose)
		set_delivery_mode(SM_DELIVER, e);
	bh = BlankEnvelope.e_header;
	nhp = &e->e_header;
	while (bh != NULL)
	{
		*nhp = (HDR *) sm_rpool_malloc_x(rpool, sizeof(*bh));
		memmove((char *) *nhp, (char *) bh, sizeof(*bh));
		bh = bh->h_link;
		nhp = &(*nhp)->h_link;
	}
#if _FFR_MILTER_ENHSC
	e->e_enhsc[0] = '\0';
#endif /* _FFR_MILTER_ENHSC */
}
/*
**  INITSYS -- initialize instantiation of system
**
**	In Daemon mode, this is done in the child.
**
**	Parameters:
**		e -- the envelope to use.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Initializes the system macros, some global variables,
**		etc.  In particular, the current time in various
**		forms is set.
*/

void
initsys(e)
	register ENVELOPE *e;
{
	char buf[10];
#ifdef TTYNAME
	static char ybuf[60];			/* holds tty id */
	register char *p;
	extern char *ttyname();
#endif /* TTYNAME */

	/*
	**  Give this envelope a reality.
	**	I.e., an id, a transcript, and a creation time.
	**  We don't select the queue until all of the recipients are known.
	*/

	openxscript(e);
	e->e_ctime = curtime();
	e->e_qfletter = '\0';

	/*
	**  Set OutChannel to something useful if stdout isn't it.
	**	This arranges that any extra stuff the mailer produces
	**	gets sent back to the user on error (because it is
	**	tucked away in the transcript).
	*/

	if (OpMode == MD_DAEMON && bitset(EF_QUEUERUN, e->e_flags) &&
	    e->e_xfp != NULL)
		OutChannel = e->e_xfp;

	/*
	**  Set up some basic system macros.
	*/

	/* process id */
	(void) sm_snprintf(buf, sizeof(buf), "%d", (int) CurrentPid);
	macdefine(&e->e_macro, A_TEMP, 'p', buf);

	/* hop count */
	(void) sm_snprintf(buf, sizeof(buf), "%d", e->e_hopcount);
	macdefine(&e->e_macro, A_TEMP, 'c', buf);

	/* time as integer, unix time, arpa time */
	settime(e);

	/* Load average */
	sm_getla();

#ifdef TTYNAME
	/* tty name */
	if (macvalue('y', e) == NULL)
	{
		p = ttyname(2);
		if (p != NULL)
		{
			if (strrchr(p, '/') != NULL)
				p = strrchr(p, '/') + 1;
			(void) sm_strlcpy(ybuf, sizeof(ybuf), p);
			macdefine(&e->e_macro, A_PERM, 'y', ybuf);
		}
	}
#endif /* TTYNAME */
}
/*
**  SETTIME -- set the current time.
**
**	Parameters:
**		e -- the envelope in which the macros should be set.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets the various time macros -- $a, $b, $d, $t.
*/

void
settime(e)
	register ENVELOPE *e;
{
	register char *p;
	auto time_t now;
	char buf[30];
	register struct tm *tm;

	now = curtime();
	(void) sm_snprintf(buf, sizeof(buf), "%ld", (long) now);
	macdefine(&e->e_macro, A_TEMP, macid("{time}"), buf);
	tm = gmtime(&now);
	(void) sm_snprintf(buf, sizeof(buf), "%04d%02d%02d%02d%02d",
			   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			   tm->tm_hour, tm->tm_min);
	macdefine(&e->e_macro, A_TEMP, 't', buf);
	(void) sm_strlcpy(buf, ctime(&now), sizeof(buf));
	p = strchr(buf, '\n');
	if (p != NULL)
		*p = '\0';
	macdefine(&e->e_macro, A_TEMP, 'd', buf);
	macdefine(&e->e_macro, A_TEMP, 'b', arpadate(buf));
	if (macvalue('a', e) == NULL)
		macdefine(&e->e_macro, A_PERM, 'a', macvalue('b', e));
}
/*
**  OPENXSCRIPT -- Open transcript file
**
**	Creates a transcript file for possible eventual mailing or
**	sending back.
**
**	Parameters:
**		e -- the envelope to create the transcript in/for.
**
**	Returns:
**		none
**
**	Side Effects:
**		Creates the transcript file.
*/

#ifndef O_APPEND
# define O_APPEND	0
#endif /* ! O_APPEND */

void
openxscript(e)
	register ENVELOPE *e;
{
	register char *p;

	if (e->e_xfp != NULL)
		return;

#if 0
	if (e->e_lockfp == NULL && bitset(EF_INQUEUE, e->e_flags))
		syserr("openxscript: job not locked");
#endif /* 0 */

	p = queuename(e, XSCRPT_LETTER);
	e->e_xfp = bfopen(p, FileMode, XscriptFileBufferSize,
			  SFF_NOTEXCL|SFF_OPENASROOT);

	if (e->e_xfp == NULL)
	{
		syserr("Can't create transcript file %s", p);
		e->e_xfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT,
				      SM_PATH_DEVNULL, SM_IO_RDWR, NULL);
		if (e->e_xfp == NULL)
			syserr("!Can't open %s", SM_PATH_DEVNULL);
	}
	(void) sm_io_setvbuf(e->e_xfp, SM_TIME_DEFAULT, NULL, SM_IO_LBF, 0);
	if (tTd(46, 9))
	{
		sm_dprintf("openxscript(%s):\n  ", p);
		dumpfd(sm_io_getinfo(e->e_xfp, SM_IO_WHAT_FD, NULL), true,
		       false);
	}
}
/*
**  CLOSEXSCRIPT -- close the transcript file.
**
**	Parameters:
**		e -- the envelope containing the transcript to close.
**
**	Returns:
**		none.
**
**	Side Effects:
**		none.
*/

void
closexscript(e)
	register ENVELOPE *e;
{
	if (e->e_xfp == NULL)
		return;
#if 0
	if (e->e_lockfp == NULL)
		syserr("closexscript: job not locked");
#endif /* 0 */
	(void) sm_io_close(e->e_xfp, SM_TIME_DEFAULT);
	e->e_xfp = NULL;
}
/*
**  SETSENDER -- set the person who this message is from
**
**	Under certain circumstances allow the user to say who
**	s/he is (using -f or -r).  These are:
**	1.  The user's uid is zero (root).
**	2.  The user's login name is in an approved list (typically
**	    from a network server).
**	3.  The address the user is trying to claim has a
**	    "!" character in it (since #2 doesn't do it for
**	    us if we are dialing out for UUCP).
**	A better check to replace #3 would be if the
**	effective uid is "UUCP" -- this would require me
**	to rewrite getpwent to "grab" uucp as it went by,
**	make getname more nasty, do another passwd file
**	scan, or compile the UID of "UUCP" into the code,
**	all of which are reprehensible.
**
**	Assuming all of these fail, we figure out something
**	ourselves.
**
**	Parameters:
**		from -- the person we would like to believe this message
**			is from, as specified on the command line.
**		e -- the envelope in which we would like the sender set.
**		delimptr -- if non-NULL, set to the location of the
**			trailing delimiter.
**		delimchar -- the character that will delimit the sender
**			address.
**		internal -- set if this address is coming from an internal
**			source such as an owner alias.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets sendmail's notion of who the from person is.
*/

void
setsender(from, e, delimptr, delimchar, internal)
	char *from;
	register ENVELOPE *e;
	char **delimptr;
	int delimchar;
	bool internal;
{
	register char **pvp;
	char *realname = NULL;
	char *bp;
	char buf[MAXNAME + 2];
	char pvpbuf[PSBUFSIZE];
	extern char *FullName;

	if (tTd(45, 1))
		sm_dprintf("setsender(%s)\n", from == NULL ? "" : from);

	/* may be set from earlier calls */
	macdefine(&e->e_macro, A_PERM, 'x', "");

	/*
	**  Figure out the real user executing us.
	**	Username can return errno != 0 on non-errors.
	*/

	if (bitset(EF_QUEUERUN, e->e_flags) || OpMode == MD_SMTP ||
	    OpMode == MD_ARPAFTP || OpMode == MD_DAEMON)
		realname = from;
	if (realname == NULL || realname[0] == '\0')
		realname = username();

	if (ConfigLevel < 2)
		SuprErrs = true;

	macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), "e s");

	/* preset state for then clause in case from == NULL */
	e->e_from.q_state = QS_BADADDR;
	e->e_from.q_flags = 0;
	if (from == NULL ||
	    parseaddr(from, &e->e_from, RF_COPYALL|RF_SENDERADDR,
		      delimchar, delimptr, e, false) == NULL ||
	    QS_IS_BADADDR(e->e_from.q_state) ||
	    e->e_from.q_mailer == ProgMailer ||
	    e->e_from.q_mailer == FileMailer ||
	    e->e_from.q_mailer == InclMailer)
	{
		/* log garbage addresses for traceback */
		if (from != NULL && LogLevel > 2)
		{
			char *p;
			char ebuf[MAXNAME * 2 + 2];

			p = macvalue('_', e);
			if (p == NULL)
			{
				char *host = RealHostName;

				if (host == NULL)
					host = MyHostName;
				(void) sm_snprintf(ebuf, sizeof(ebuf),
						   "%.*s@%.*s", MAXNAME,
						   realname, MAXNAME, host);
				p = ebuf;
			}
			sm_syslog(LOG_NOTICE, e->e_id,
				  "setsender: %s: invalid or unparsable, received from %s",
				  shortenstring(from, 83), p);
		}
		if (from != NULL)
		{
			if (!QS_IS_BADADDR(e->e_from.q_state))
			{
				/* it was a bogus mailer in the from addr */
				e->e_status = "5.1.7";
				usrerrenh(e->e_status,
					  "553 Invalid sender address");
			}
			SuprErrs = true;
		}
		if (from == realname ||
		    parseaddr(from = realname,
			      &e->e_from, RF_COPYALL|RF_SENDERADDR, ' ',
			      NULL, e, false) == NULL)
		{
			char nbuf[100];

			SuprErrs = true;
			expand("\201n", nbuf, sizeof(nbuf), e);
			from = sm_rpool_strdup_x(e->e_rpool, nbuf);
			if (parseaddr(from, &e->e_from, RF_COPYALL, ' ',
				      NULL, e, false) == NULL &&
			    parseaddr(from = "postmaster", &e->e_from,
				      RF_COPYALL, ' ', NULL, e, false) == NULL)
				syserr("553 5.3.0 setsender: can't even parse postmaster!");
		}
	}
	else
		FromFlag = true;
	e->e_from.q_state = QS_SENDER;
	if (tTd(45, 5))
	{
		sm_dprintf("setsender: QS_SENDER ");
		printaddr(sm_debug_file(), &e->e_from, false);
	}
	SuprErrs = false;

#if USERDB
	if (bitnset(M_CHECKUDB, e->e_from.q_mailer->m_flags))
	{
		register char *p;

		p = udbsender(e->e_from.q_user, e->e_rpool);
		if (p != NULL)
			from = p;
	}
#endif /* USERDB */

	if (bitnset(M_HASPWENT, e->e_from.q_mailer->m_flags))
	{
		SM_MBDB_T user;

		if (!internal)
		{
			/* if the user already given fullname don't redefine */
			if (FullName == NULL)
				FullName = macvalue('x', e);
			if (FullName != NULL)
			{
				if (FullName[0] == '\0')
					FullName = NULL;
				else
					FullName = newstr(FullName);
			}
		}

		if (e->e_from.q_user[0] != '\0' &&
		    sm_mbdb_lookup(e->e_from.q_user, &user) == EX_OK)
		{
			/*
			**  Process passwd file entry.
			*/

			/* extract home directory */
			if (*user.mbdb_homedir == '\0')
				e->e_from.q_home = NULL;
			else if (strcmp(user.mbdb_homedir, "/") == 0)
				e->e_from.q_home = "";
			else
				e->e_from.q_home = sm_rpool_strdup_x(e->e_rpool,
							user.mbdb_homedir);
			macdefine(&e->e_macro, A_PERM, 'z', e->e_from.q_home);

			/* extract user and group id */
			if (user.mbdb_uid != SM_NO_UID)
			{
				e->e_from.q_uid = user.mbdb_uid;
				e->e_from.q_gid = user.mbdb_gid;
				e->e_from.q_flags |= QGOODUID;
			}

			/* extract full name from passwd file */
			if (FullName == NULL && !internal &&
			    user.mbdb_fullname[0] != '\0' &&
			    strcmp(user.mbdb_name, e->e_from.q_user) == 0)
			{
				FullName = newstr(user.mbdb_fullname);
			}
		}
		else
		{
			e->e_from.q_home = NULL;
		}
		if (FullName != NULL && !internal)
			macdefine(&e->e_macro, A_TEMP, 'x', FullName);
	}
	else if (!internal && OpMode != MD_DAEMON && OpMode != MD_SMTP)
	{
		if (e->e_from.q_home == NULL)
		{
			e->e_from.q_home = getenv("HOME");
			if (e->e_from.q_home != NULL)
			{
				if (*e->e_from.q_home == '\0')
					e->e_from.q_home = NULL;
				else if (strcmp(e->e_from.q_home, "/") == 0)
					e->e_from.q_home++;
			}
		}
		e->e_from.q_uid = RealUid;
		e->e_from.q_gid = RealGid;
		e->e_from.q_flags |= QGOODUID;
	}

	/*
	**  Rewrite the from person to dispose of possible implicit
	**	links in the net.
	*/

	pvp = prescan(from, delimchar, pvpbuf, sizeof(pvpbuf), NULL,
			IntTokenTab, false);
	if (pvp == NULL)
	{
		/* don't need to give error -- prescan did that already */
		if (LogLevel > 2)
			sm_syslog(LOG_NOTICE, e->e_id,
				  "cannot prescan from (%s)",
				  shortenstring(from, MAXSHORTSTR));
		finis(true, true, ExitStat);
	}
	(void) REWRITE(pvp, 3, e);
	(void) REWRITE(pvp, 1, e);
	(void) REWRITE(pvp, 4, e);
	macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), NULL);
	bp = buf + 1;
	cataddr(pvp, NULL, bp, sizeof(buf) - 2, '\0', false);
	if (*bp == '@' && !bitnset(M_NOBRACKET, e->e_from.q_mailer->m_flags))
	{
		/* heuristic: route-addr: add angle brackets */
		(void) sm_strlcat(bp, ">", sizeof(buf) - 1);
		*--bp = '<';
	}
	e->e_sender = sm_rpool_strdup_x(e->e_rpool, bp);
	macdefine(&e->e_macro, A_PERM, 'f', e->e_sender);

	/* save the domain spec if this mailer wants it */
	if (e->e_from.q_mailer != NULL &&
	    bitnset(M_CANONICAL, e->e_from.q_mailer->m_flags))
	{
		char **lastat;

		/* get rid of any pesky angle brackets */
		macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), "e s");
		(void) REWRITE(pvp, 3, e);
		(void) REWRITE(pvp, 1, e);
		(void) REWRITE(pvp, 4, e);
		macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), NULL);

		/* strip off to the last "@" sign */
		for (lastat = NULL; *pvp != NULL; pvp++)
		{
			if (strcmp(*pvp, "@") == 0)
				lastat = pvp;
		}
		if (lastat != NULL)
		{
			e->e_fromdomain = copyplist(lastat, true, e->e_rpool);
			if (tTd(45, 3))
			{
				sm_dprintf("Saving from domain: ");
				printav(sm_debug_file(), e->e_fromdomain);
			}
		}
	}
}
/*
**  PRINTENVFLAGS -- print envelope flags for debugging
**
**	Parameters:
**		e -- the envelope with the flags to be printed.
**
**	Returns:
**		none.
*/

struct eflags
{
	char		*ef_name;
	unsigned long	ef_bit;
};

static struct eflags	EnvelopeFlags[] =
{
	{ "OLDSTYLE",		EF_OLDSTYLE	},
	{ "INQUEUE",		EF_INQUEUE	},
	{ "NO_BODY_RETN",	EF_NO_BODY_RETN	},
	{ "CLRQUEUE",		EF_CLRQUEUE	},
	{ "SENDRECEIPT",	EF_SENDRECEIPT	},
	{ "FATALERRS",		EF_FATALERRS	},
	{ "DELETE_BCC",		EF_DELETE_BCC	},
	{ "RESPONSE",		EF_RESPONSE	},
	{ "RESENT",		EF_RESENT	},
	{ "VRFYONLY",		EF_VRFYONLY	},
	{ "WARNING",		EF_WARNING	},
	{ "QUEUERUN",		EF_QUEUERUN	},
	{ "GLOBALERRS",		EF_GLOBALERRS	},
	{ "PM_NOTIFY",		EF_PM_NOTIFY	},
	{ "METOO",		EF_METOO	},
	{ "LOGSENDER",		EF_LOGSENDER	},
	{ "NORECEIPT",		EF_NORECEIPT	},
	{ "HAS8BIT",		EF_HAS8BIT	},
	{ "NL_NOT_EOL",		EF_NL_NOT_EOL	},
	{ "CRLF_NOT_EOL",	EF_CRLF_NOT_EOL	},
	{ "RET_PARAM",		EF_RET_PARAM	},
	{ "HAS_DF",		EF_HAS_DF	},
	{ "IS_MIME",		EF_IS_MIME	},
	{ "DONT_MIME",		EF_DONT_MIME	},
	{ "DISCARD",		EF_DISCARD	},
	{ "TOOBIG",		EF_TOOBIG	},
	{ "SPLIT",		EF_SPLIT	},
	{ "UNSAFE",		EF_UNSAFE	},
	{ NULL,			0		}
};

void
printenvflags(e)
	register ENVELOPE *e;
{
	register struct eflags *ef;
	bool first = true;

	sm_dprintf("%lx", e->e_flags);
	for (ef = EnvelopeFlags; ef->ef_name != NULL; ef++)
	{
		if (!bitset(ef->ef_bit, e->e_flags))
			continue;
		if (first)
			sm_dprintf("<%s", ef->ef_name);
		else
			sm_dprintf(",%s", ef->ef_name);
		first = false;
	}
	if (!first)
		sm_dprintf(">\n");
}
