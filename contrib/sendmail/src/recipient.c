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

SM_RCSID("@(#)$Id: recipient.c,v 8.351 2013-11-22 20:51:56 ca Exp $")

static void	includetimeout __P((int));
static ADDRESS	*self_reference __P((ADDRESS *));
static int	sortexpensive __P((ADDRESS *, ADDRESS *));
static int	sortbysignature __P((ADDRESS *, ADDRESS *));
static int	sorthost __P((ADDRESS *, ADDRESS *));

typedef int	sortfn_t __P((ADDRESS *, ADDRESS *));

/*
**  SORTHOST -- strcmp()-like func for host portion of an ADDRESS
**
**	Parameters:
**		xx -- first ADDRESS
**		yy -- second ADDRESS
**
**	Returns:
**		<0 when xx->q_host is less than yy->q_host
**		>0 when xx->q_host is greater than yy->q_host
**		0 when equal
*/

static int
sorthost(xx, yy)
	register ADDRESS *xx;
	register ADDRESS *yy;
{
#if _FFR_HOST_SORT_REVERSE
	/* XXX maybe compare hostnames from the end? */
	return sm_strrevcasecmp(xx->q_host, yy->q_host);
#else /* _FFR_HOST_SORT_REVERSE */
	return sm_strcasecmp(xx->q_host, yy->q_host);
#endif /* _FFR_HOST_SORT_REVERSE */
}

/*
**  SORTEXPENSIVE -- strcmp()-like func for expensive mailers
**
**  The mailer has been noted already as "expensive" for 'xx'. This
**  will give a result relative to 'yy'. Expensive mailers get rated
**  "greater than" non-expensive mailers because during the delivery phase
**  it will get queued -- no use it getting in the way of less expensive
**  recipients. We avoid an MX RR lookup when both 'xx' and 'yy' are
**  expensive since an MX RR lookup happens when extracted from the queue
**  later.
**
**	Parameters:
**		xx -- first ADDRESS
**		yy -- second ADDRESS
**
**	Returns:
**		<0 when xx->q_host is less than yy->q_host and both are
**			expensive
**		>0 when xx->q_host is greater than yy->q_host, or when
**			'yy' is non-expensive
**		0 when equal (by expense and q_host)
*/

static int
sortexpensive(xx, yy)
	ADDRESS *xx;
	ADDRESS *yy;
{
	if (!bitnset(M_EXPENSIVE, yy->q_mailer->m_flags))
		return 1; /* xx should go later */
#if _FFR_HOST_SORT_REVERSE
	/* XXX maybe compare hostnames from the end? */
	return sm_strrevcasecmp(xx->q_host, yy->q_host);
#else /* _FFR_HOST_SORT_REVERSE */
	return sm_strcasecmp(xx->q_host, yy->q_host);
#endif /* _FFR_HOST_SORT_REVERSE */
}

/*
**  SORTBYSIGNATURE -- a strcmp()-like func for q_mailer and q_host in ADDRESS
**
**	Parameters:
**		xx -- first ADDRESS
**		yy -- second ADDRESS
**
**	Returns:
**		0 when the "signature"'s are same
**		<0 when xx->q_signature is less than yy->q_signature
**		>0 when xx->q_signature is greater than yy->q_signature
**
**	Side Effect:
**		May set ADDRESS pointer for q_signature if not already set.
*/

static int
sortbysignature(xx, yy)
	ADDRESS *xx;
	ADDRESS *yy;
{
	register int ret;

	/* Let's avoid redoing the signature over and over again */
	if (xx->q_signature == NULL)
		xx->q_signature = hostsignature(xx->q_mailer, xx->q_host);
	if (yy->q_signature == NULL)
		yy->q_signature = hostsignature(yy->q_mailer, yy->q_host);
	ret = strcmp(xx->q_signature, yy->q_signature);

	/*
	**  If the two signatures are the same then we will return a sort
	**  value based on 'q_user'. But note that we have reversed xx and yy
	**  on purpose. This additional compare helps reduce the number of
	**  sameaddr() calls and loops in recipient() for the case when
	**  the rcpt list has been provided already in-order.
	*/

	if (ret == 0)
		return strcmp(yy->q_user, xx->q_user);
	else
		return ret;
}

/*
**  SENDTOLIST -- Designate a send list.
**
**	The parameter is a comma-separated list of people to send to.
**	This routine arranges to send to all of them.
**
**	Parameters:
**		list -- the send list.
**		ctladdr -- the address template for the person to
**			send to -- effective uid/gid are important.
**			This is typically the alias that caused this
**			expansion.
**		sendq -- a pointer to the head of a queue to put
**			these people into.
**		aliaslevel -- the current alias nesting depth -- to
**			diagnose loops.
**		e -- the envelope in which to add these recipients.
**
**	Returns:
**		The number of addresses actually on the list.
*/

/* q_flags bits inherited from ctladdr */
#define QINHERITEDBITS	(QPINGONSUCCESS|QPINGONFAILURE|QPINGONDELAY|QHASNOTIFY)

int
sendtolist(list, ctladdr, sendq, aliaslevel, e)
	char *list;
	ADDRESS *ctladdr;
	ADDRESS **sendq;
	int aliaslevel;
	register ENVELOPE *e;
{
	register char *p;
	register ADDRESS *SM_NONVOLATILE al; /* list of addresses to send to */
	SM_NONVOLATILE char delimiter;		/* the address delimiter */
	SM_NONVOLATILE int naddrs;
	SM_NONVOLATILE int i;
	char *endp;
	char *oldto = e->e_to;
	char *SM_NONVOLATILE bufp;
	char buf[MAXNAME + 1];

	if (list == NULL)
	{
		syserr("sendtolist: null list");
		return 0;
	}

	if (tTd(25, 1))
	{
		sm_dprintf("sendto: %s\n   ctladdr=", list);
		printaddr(sm_debug_file(), ctladdr, false);
	}

	/* heuristic to determine old versus new style addresses */
	if (ctladdr == NULL &&
	    (strchr(list, ',') != NULL || strchr(list, ';') != NULL ||
	     strchr(list, '<') != NULL || strchr(list, '(') != NULL))
		e->e_flags &= ~EF_OLDSTYLE;
	delimiter = ' ';
	if (!bitset(EF_OLDSTYLE, e->e_flags) || ctladdr != NULL)
		delimiter = ',';

	al = NULL;
	naddrs = 0;

	/* make sure we have enough space to copy the string */
	i = strlen(list) + 1;
	if (i <= sizeof(buf))
	{
		bufp = buf;
		i = sizeof(buf);
	}
	else
		bufp = sm_malloc_x(i);
	endp = bufp + i;

	SM_TRY
	{
		(void) sm_strlcpy(bufp, denlstring(list, false, true), i);

		macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), "e r");
		for (p = bufp; *p != '\0'; )
		{
			auto char *delimptr;
			register ADDRESS *a;

			SM_ASSERT(p < endp);

			/* parse the address */
			while ((isascii(*p) && isspace(*p)) || *p == ',')
				p++;
			SM_ASSERT(p < endp);
			a = parseaddr(p, NULLADDR, RF_COPYALL, delimiter,
				      &delimptr, e, true);
			p = delimptr;
			SM_ASSERT(p < endp);
			if (a == NULL)
				continue;
			a->q_next = al;
			a->q_alias = ctladdr;

			/* arrange to inherit attributes from parent */
			if (ctladdr != NULL)
			{
				ADDRESS *b;

				/* self reference test */
				if (sameaddr(ctladdr, a))
				{
					if (tTd(27, 5))
					{
						sm_dprintf("sendtolist: QSELFREF ");
						printaddr(sm_debug_file(), ctladdr, false);
					}
					ctladdr->q_flags |= QSELFREF;
				}

				/* check for address loops */
				b = self_reference(a);
				if (b != NULL)
				{
					b->q_flags |= QSELFREF;
					if (tTd(27, 5))
					{
						sm_dprintf("sendtolist: QSELFREF ");
						printaddr(sm_debug_file(), b, false);
					}
					if (a != b)
					{
						if (tTd(27, 5))
						{
							sm_dprintf("sendtolist: QS_DONTSEND ");
							printaddr(sm_debug_file(), a, false);
						}
						a->q_state = QS_DONTSEND;
						b->q_flags |= a->q_flags & QNOTREMOTE;
						continue;
					}
				}

				/* full name */
				if (a->q_fullname == NULL)
					a->q_fullname = ctladdr->q_fullname;

				/* various flag bits */
				a->q_flags &= ~QINHERITEDBITS;
				a->q_flags |= ctladdr->q_flags & QINHERITEDBITS;

				/* DSN recipient information */
				a->q_finalrcpt = ctladdr->q_finalrcpt;
				a->q_orcpt = ctladdr->q_orcpt;
			}

			al = a;
		}

		/* arrange to send to everyone on the local send list */
		while (al != NULL)
		{
			register ADDRESS *a = al;

			al = a->q_next;
			a = recipient(a, sendq, aliaslevel, e);
			naddrs++;
		}
	}
	SM_FINALLY
	{
		e->e_to = oldto;
		if (bufp != buf)
			sm_free(bufp);
		macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), NULL);
	}
	SM_END_TRY
	return naddrs;
}

#if MILTER
/*
**  REMOVEFROMLIST -- Remove addresses from a send list.
**
**	The parameter is a comma-separated list of recipients to remove.
**	Note that it only deletes matching addresses.  If those addresses
**	have been expanded already in the sendq, it won't mark the
**	expanded recipients as QS_REMOVED.
**
**	Parameters:
**		list -- the list to remove.
**		sendq -- a pointer to the head of a queue to remove
**			these addresses from.
**		e -- the envelope in which to remove these recipients.
**
**	Returns:
**		The number of addresses removed from the list.
**
*/

int
removefromlist(list, sendq, e)
	char *list;
	ADDRESS **sendq;
	ENVELOPE *e;
{
	SM_NONVOLATILE char delimiter;		/* the address delimiter */
	SM_NONVOLATILE int naddrs;
	SM_NONVOLATILE int i;
	char *p;
	char *oldto = e->e_to;
	char *SM_NONVOLATILE bufp;
	char buf[MAXNAME + 1];

	if (list == NULL)
	{
		syserr("removefromlist: null list");
		return 0;
	}

	if (tTd(25, 1))
		sm_dprintf("removefromlist: %s\n", list);

	/* heuristic to determine old versus new style addresses */
	if (strchr(list, ',') != NULL || strchr(list, ';') != NULL ||
	    strchr(list, '<') != NULL || strchr(list, '(') != NULL)
		e->e_flags &= ~EF_OLDSTYLE;
	delimiter = ' ';
	if (!bitset(EF_OLDSTYLE, e->e_flags))
		delimiter = ',';

	naddrs = 0;

	/* make sure we have enough space to copy the string */
	i = strlen(list) + 1;
	if (i <= sizeof(buf))
	{
		bufp = buf;
		i = sizeof(buf);
	}
	else
		bufp = sm_malloc_x(i);

	SM_TRY
	{
		(void) sm_strlcpy(bufp, denlstring(list, false, true), i);

#if _FFR_ADDR_TYPE_MODES
		if (AddrTypeModes)
			macdefine(&e->e_macro, A_PERM, macid("{addr_type}"),
				  "e r d");
		else
#endif /* _FFR_ADDR_TYPE_MODES */
		macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), "e r");
		for (p = bufp; *p != '\0'; )
		{
			ADDRESS a;	/* parsed address to be removed */
			ADDRESS *q;
			ADDRESS **pq;
			char *delimptr;

			/* parse the address */
			while ((isascii(*p) && isspace(*p)) || *p == ',')
				p++;
			if (parseaddr(p, &a, RF_COPYALL|RF_RM_ADDR,
				      delimiter, &delimptr, e, true) == NULL)
			{
				p = delimptr;
				continue;
			}
			p = delimptr;
			for (pq = sendq; (q = *pq) != NULL; pq = &q->q_next)
			{
				if (!QS_IS_DEAD(q->q_state) &&
				    (sameaddr(q, &a) ||
				     strcmp(q->q_paddr, a.q_paddr) == 0))
				{
					if (tTd(25, 5))
					{
						sm_dprintf("removefromlist: QS_REMOVED ");
						printaddr(sm_debug_file(), &a, false);
					}
					q->q_state = QS_REMOVED;
					naddrs++;
					break;
				}
			}
		}
	}
	SM_FINALLY
	{
		e->e_to = oldto;
		if (bufp != buf)
			sm_free(bufp);
		macdefine(&e->e_macro, A_PERM, macid("{addr_type}"), NULL);
	}
	SM_END_TRY
	return naddrs;
}
#endif /* MILTER */

/*
**  RECIPIENT -- Designate a message recipient
**	Saves the named person for future mailing (after some checks).
**
**	Parameters:
**		new -- the (preparsed) address header for the recipient.
**		sendq -- a pointer to the head of a queue to put the
**			recipient in.  Duplicate suppression is done
**			in this queue.
**		aliaslevel -- the current alias nesting depth.
**		e -- the current envelope.
**
**	Returns:
**		The actual address in the queue.  This will be "a" if
**		the address is not a duplicate, else the original address.
**
*/

ADDRESS *
recipient(new, sendq, aliaslevel, e)
	register ADDRESS *new;
	register ADDRESS **sendq;
	int aliaslevel;
	register ENVELOPE *e;
{
	register ADDRESS *q;
	ADDRESS **pq;
	ADDRESS **prev;
	register struct mailer *m;
	register char *p;
	int i, buflen;
	bool quoted;		/* set if the addr has a quote bit */
	bool insert;
	int findusercount;
	bool initialdontsend;
	char *buf;
	char buf0[MAXNAME + 1];		/* unquoted image of the user name */
	sortfn_t *sortfn;

	p = NULL;
	quoted = false;
	insert = false;
	findusercount = 0;
	initialdontsend = QS_IS_DEAD(new->q_state);
	e->e_to = new->q_paddr;
	m = new->q_mailer;
	errno = 0;
	if (aliaslevel == 0)
		new->q_flags |= QPRIMARY;
	if (tTd(26, 1))
	{
		sm_dprintf("\nrecipient (%d): ", aliaslevel);
		printaddr(sm_debug_file(), new, false);
	}

	/* if this is primary, use it as original recipient */
	if (new->q_alias == NULL)
	{
		if (e->e_origrcpt == NULL)
			e->e_origrcpt = new->q_paddr;
		else if (e->e_origrcpt != new->q_paddr)
			e->e_origrcpt = "";
	}

	/* find parent recipient for finalrcpt and orcpt */
	for (q = new; q->q_alias != NULL; q = q->q_alias)
		continue;

	/* find final recipient DSN address */
	if (new->q_finalrcpt == NULL &&
	    e->e_from.q_mailer != NULL)
	{
		char frbuf[MAXLINE];

		p = e->e_from.q_mailer->m_addrtype;
		if (p == NULL)
			p = "rfc822";
		if (sm_strcasecmp(p, "rfc822") != 0)
		{
			(void) sm_snprintf(frbuf, sizeof(frbuf), "%s; %.800s",
					   q->q_mailer->m_addrtype,
					   q->q_user);
		}
		else if (strchr(q->q_user, '@') != NULL)
		{
			(void) sm_snprintf(frbuf, sizeof(frbuf), "%s; %.800s",
					   p, q->q_user);
		}
		else if (strchr(q->q_paddr, '@') != NULL)
		{
			char *qp;
			bool b;

			qp = q->q_paddr;

			/* strip brackets from address */
			b = false;
			if (*qp == '<')
			{
				b = qp[strlen(qp) - 1] == '>';
				if (b)
					qp[strlen(qp) - 1] = '\0';
				qp++;
			}
			(void) sm_snprintf(frbuf, sizeof(frbuf), "%s; %.800s",
					   p, qp);

			/* undo damage */
			if (b)
				qp[strlen(qp)] = '>';
		}
		else
		{
			(void) sm_snprintf(frbuf, sizeof(frbuf),
					   "%s; %.700s@%.100s",
					   p, q->q_user, MyHostName);
		}
		new->q_finalrcpt = sm_rpool_strdup_x(e->e_rpool, frbuf);
	}

#if _FFR_GEN_ORCPT
	/* set ORCPT DSN arg if not already set */
	if (new->q_orcpt == NULL)
	{
		/* check for an existing ORCPT */
		if (q->q_orcpt != NULL)
			new->q_orcpt = q->q_orcpt;
		else
		{
			/* make our own */
			bool b = false;
			char *qp;
			char obuf[MAXLINE];

			if (e->e_from.q_mailer != NULL)
				p = e->e_from.q_mailer->m_addrtype;
			if (p == NULL)
				p = "rfc822";
			(void) sm_strlcpyn(obuf, sizeof(obuf), 2, p, ";");

			qp = q->q_paddr;

			/* FFR: Needs to strip comments from stdin addrs */

			/* strip brackets from address */
			if (*qp == '<')
			{
				b = qp[strlen(qp) - 1] == '>';
				if (b)
					qp[strlen(qp) - 1] = '\0';
				qp++;
			}

			p = xtextify(denlstring(qp, true, false), "=");

			if (sm_strlcat(obuf, p, sizeof(obuf)) >= sizeof(obuf))
			{
				/* if too big, don't use it */
				obuf[0] = '\0';
			}

			/* undo damage */
			if (b)
				qp[strlen(qp)] = '>';

			if (obuf[0] != '\0')
				new->q_orcpt =
					sm_rpool_strdup_x(e->e_rpool, obuf);
		}
	}
#endif /* _FFR_GEN_ORCPT */

	/* break aliasing loops */
	if (aliaslevel > MaxAliasRecursion)
	{
		new->q_state = QS_BADADDR;
		new->q_status = "5.4.6";
		if (new->q_alias != NULL)
		{
			new->q_alias->q_state = QS_BADADDR;
			new->q_alias->q_status = "5.4.6";
		}
		if ((SuprErrs || !LogUsrErrs) && LogLevel > 0)
		{
			sm_syslog(LOG_ERR, e->e_id,
				"aliasing/forwarding loop broken: %s (%d aliases deep; %d max)",
				FileName != NULL ? FileName : "", aliaslevel,
				MaxAliasRecursion);
		}
		usrerrenh(new->q_status,
			  "554 aliasing/forwarding loop broken (%d aliases deep; %d max)",
			  aliaslevel, MaxAliasRecursion);
		return new;
	}

	/*
	**  Finish setting up address structure.
	*/

	/* get unquoted user for file, program or user.name check */
	i = strlen(new->q_user);
	if (i >= sizeof(buf0))
	{
		buflen = i + 1;
		buf = xalloc(buflen);
	}
	else
	{
		buf = buf0;
		buflen = sizeof(buf0);
	}
	(void) sm_strlcpy(buf, new->q_user, buflen);
	for (p = buf; *p != '\0' && !quoted; p++)
	{
		if (*p == '\\')
			quoted = true;
	}
	stripquotes(buf);

	/* check for direct mailing to restricted mailers */
	if (m == ProgMailer)
	{
		if (new->q_alias == NULL || UseMSP ||
		    bitset(EF_UNSAFE, e->e_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			usrerrenh(new->q_status,
				  "550 Cannot mail directly to programs");
		}
		else if (bitset(QBOGUSSHELL, new->q_alias->q_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			if (new->q_alias->q_ruser == NULL)
				usrerrenh(new->q_status,
					  "550 UID %ld is an unknown user: cannot mail to programs",
					  (long) new->q_alias->q_uid);
			else
				usrerrenh(new->q_status,
					  "550 User %s@%s doesn't have a valid shell for mailing to programs",
					  new->q_alias->q_ruser, MyHostName);
		}
		else if (bitset(QUNSAFEADDR, new->q_alias->q_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			new->q_rstatus = "550 Unsafe for mailing to programs";
			usrerrenh(new->q_status,
				  "550 Address %s is unsafe for mailing to programs",
				  new->q_alias->q_paddr);
		}
	}

	/*
	**  Look up this person in the recipient list.
	**	If they are there already, return, otherwise continue.
	**	If the list is empty, just add it.  Notice the cute
	**	hack to make from addresses suppress things correctly:
	**	the QS_DUPLICATE state will be set in the send list.
	**	[Please note: the emphasis is on "hack."]
	*/

	prev = NULL;

	/*
	**  If this message is going to the queue or FastSplit is set
	**  and it is the first try and the envelope hasn't split, then we
	**  avoid doing an MX RR lookup now because one will be done when the
	**  message is extracted from the queue later. It can go to the queue
	**  because all messages are going to the queue or this mailer of
	**  the current recipient is marked expensive.
	*/

	if (UseMSP || WILL_BE_QUEUED(e->e_sendmode) ||
	    (!bitset(EF_SPLIT, e->e_flags) && e->e_ntries == 0 &&
	     FastSplit > 0))
		sortfn = sorthost;
	else if (NoConnect && bitnset(M_EXPENSIVE, new->q_mailer->m_flags))
		sortfn = sortexpensive;
	else
		sortfn = sortbysignature;

	for (pq = sendq; (q = *pq) != NULL; pq = &q->q_next)
	{
		/*
		**  If address is "less than" it should be inserted now.
		**  If address is "greater than" current comparison it'll
		**  insert later in the list; so loop again (if possible).
		**  If address is "equal" (different equal than sameaddr()
		**  call) then check if sameaddr() will be true.
		**  Because this list is now sorted, it'll mean fewer
		**  comparisons and fewer loops which is important for more
		**  recipients.
		*/

		i = (*sortfn)(new, q);
		if (i == 0) /* equal */
		{
			/*
			**  Sortbysignature() has said that the two have
			**  equal MX RR's and the same user. Calling sameaddr()
			**  now checks if the two hosts are as identical as the
			**  MX RR's are (which might not be the case)
			**  before saying these are the identical addresses.
			*/

			if (sameaddr(q, new) &&
			    (bitset(QRCPTOK, q->q_flags) ||
			     !bitset(QPRIMARY, q->q_flags)))
			{
				if (tTd(26, 1))
				{
					sm_dprintf("%s in sendq: ",
						   new->q_paddr);
					printaddr(sm_debug_file(), q, false);
				}
				if (!bitset(QPRIMARY, q->q_flags))
				{
					if (!QS_IS_DEAD(new->q_state))
						message("duplicate suppressed");
					else
						q->q_state = QS_DUPLICATE;
					q->q_flags |= new->q_flags;
				}
				else if (bitset(QSELFREF, q->q_flags)
					 || q->q_state == QS_REMOVED)
				{
					/*
					**  If an earlier milter removed the
					**  address, a later one can still add
					**  it back.
					*/

					q->q_state = new->q_state;
					q->q_flags |= new->q_flags;
				}
				new = q;
				goto done;
			}
		}
		else if (i < 0) /* less than */
		{
			insert = true;
			break;
		}
		prev = pq;
	}

	/* pq should point to an address, never NULL */
	SM_ASSERT(pq != NULL);

	/* add address on list */
	if (insert)
	{
		/*
		**  insert before 'pq'. Only possible when at least 1
		**  ADDRESS is in the list already.
		*/

		new->q_next = *pq;
		if (prev == NULL)
			*sendq = new; /* To be the first ADDRESS */
		else
			(*prev)->q_next = new;
	}
	else
	{
		/*
		**  Place in list at current 'pq' position. Possible
		**  when there are 0 or more ADDRESS's in the list.
		*/

		new->q_next = NULL;
		*pq = new;
	}

	/* added a new address: clear split flag */
	e->e_flags &= ~EF_SPLIT;

	/*
	**  Alias the name and handle special mailer types.
	*/

  trylocaluser:
	if (tTd(29, 7))
	{
		sm_dprintf("at trylocaluser: ");
		printaddr(sm_debug_file(), new, false);
	}

	if (!QS_IS_OK(new->q_state))
	{
		if (QS_IS_UNDELIVERED(new->q_state))
			e->e_nrcpts++;
		goto testselfdestruct;
	}

	if (m == InclMailer)
	{
		new->q_state = QS_INCLUDED;
		if (new->q_alias == NULL || UseMSP ||
		    bitset(EF_UNSAFE, e->e_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			usrerrenh(new->q_status,
				  "550 Cannot mail directly to :include:s");
		}
		else
		{
			int ret;

			message("including file %s", new->q_user);
			ret = include(new->q_user, false, new,
				      sendq, aliaslevel, e);
			if (transienterror(ret))
			{
				if (LogLevel > 2)
					sm_syslog(LOG_ERR, e->e_id,
						  "include %s: transient error: %s",
						  shortenstring(new->q_user,
								MAXSHORTSTR),
								sm_errstring(ret));
				new->q_state = QS_QUEUEUP;
				usrerr("451 4.2.4 Cannot open %s: %s",
					shortenstring(new->q_user,
						      MAXSHORTSTR),
					sm_errstring(ret));
			}
			else if (ret != 0)
			{
				new->q_state = QS_BADADDR;
				new->q_status = "5.2.4";
				usrerrenh(new->q_status,
					  "550 Cannot open %s: %s",
					  shortenstring(new->q_user,
							MAXSHORTSTR),
					  sm_errstring(ret));
			}
		}
	}
	else if (m == FileMailer)
	{
		/* check if allowed */
		if (new->q_alias == NULL || UseMSP ||
		    bitset(EF_UNSAFE, e->e_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			usrerrenh(new->q_status,
				  "550 Cannot mail directly to files");
		}
		else if (bitset(QBOGUSSHELL, new->q_alias->q_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			if (new->q_alias->q_ruser == NULL)
				usrerrenh(new->q_status,
					  "550 UID %ld is an unknown user: cannot mail to files",
					  (long) new->q_alias->q_uid);
			else
				usrerrenh(new->q_status,
					  "550 User %s@%s doesn't have a valid shell for mailing to files",
					  new->q_alias->q_ruser, MyHostName);
		}
		else if (bitset(QUNSAFEADDR, new->q_alias->q_flags))
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.7.1";
			new->q_rstatus = "550 Unsafe for mailing to files";
			usrerrenh(new->q_status,
				  "550 Address %s is unsafe for mailing to files",
				  new->q_alias->q_paddr);
		}
	}

	/* try aliasing */
	if (!quoted && QS_IS_OK(new->q_state) &&
	    bitnset(M_ALIASABLE, m->m_flags))
		alias(new, sendq, aliaslevel, e);

#if USERDB
	/* if not aliased, look it up in the user database */
	if (!bitset(QNOTREMOTE, new->q_flags) &&
	    QS_IS_SENDABLE(new->q_state) &&
	    bitnset(M_CHECKUDB, m->m_flags))
	{
		if (udbexpand(new, sendq, aliaslevel, e) == EX_TEMPFAIL)
		{
			new->q_state = QS_QUEUEUP;
			if (e->e_message == NULL)
				e->e_message = sm_rpool_strdup_x(e->e_rpool,
						"Deferred: user database error");
			if (new->q_message == NULL)
				new->q_message = "Deferred: user database error";
			if (LogLevel > 8)
				sm_syslog(LOG_INFO, e->e_id,
					  "deferred: udbexpand: %s",
					  sm_errstring(errno));
			message("queued (user database error): %s",
				sm_errstring(errno));
			e->e_nrcpts++;
			goto testselfdestruct;
		}
	}
#endif /* USERDB */

	/*
	**  If we have a level two config file, then pass the name through
	**  Ruleset 5 before sending it off.  Ruleset 5 has the right
	**  to rewrite it to another mailer.  This gives us a hook
	**  after local aliasing has been done.
	*/

	if (tTd(29, 5))
	{
		sm_dprintf("recipient: testing local?  cl=%d, rr5=%p\n\t",
			   ConfigLevel, RewriteRules[5]);
		printaddr(sm_debug_file(), new, false);
	}
	if (ConfigLevel >= 2 && RewriteRules[5] != NULL &&
	    bitnset(M_TRYRULESET5, m->m_flags) &&
	    !bitset(QNOTREMOTE, new->q_flags) &&
	    QS_IS_OK(new->q_state))
	{
		maplocaluser(new, sendq, aliaslevel + 1, e);
	}

	/*
	**  If it didn't get rewritten to another mailer, go ahead
	**  and deliver it.
	*/

	if (QS_IS_OK(new->q_state) &&
	    bitnset(M_HASPWENT, m->m_flags))
	{
		auto bool fuzzy;
		SM_MBDB_T user;
		int status;

		/* warning -- finduser may trash buf */
		status = finduser(buf, &fuzzy, &user);
		switch (status)
		{
		  case EX_TEMPFAIL:
			new->q_state = QS_QUEUEUP;
			new->q_status = "4.5.2";
			giveresponse(EX_TEMPFAIL, new->q_status, m, NULL,
				     new->q_alias, (time_t) 0, e, new);
			break;
		  default:
			new->q_state = QS_BADADDR;
			new->q_status = "5.1.1";
			new->q_rstatus = "550 5.1.1 User unknown";
			giveresponse(EX_NOUSER, new->q_status, m, NULL,
				     new->q_alias, (time_t) 0, e, new);
			break;
		  case EX_OK:
			if (fuzzy)
			{
				/* name was a fuzzy match */
				new->q_user = sm_rpool_strdup_x(e->e_rpool,
								user.mbdb_name);
				if (findusercount++ > 3)
				{
					new->q_state = QS_BADADDR;
					new->q_status = "5.4.6";
					usrerrenh(new->q_status,
						  "554 aliasing/forwarding loop for %s broken",
						  user.mbdb_name);
					goto done;
				}

				/* see if it aliases */
				(void) sm_strlcpy(buf, user.mbdb_name, buflen);
				goto trylocaluser;
			}
			if (*user.mbdb_homedir == '\0')
				new->q_home = NULL;
			else if (strcmp(user.mbdb_homedir, "/") == 0)
				new->q_home = "";
			else
				new->q_home = sm_rpool_strdup_x(e->e_rpool,
							user.mbdb_homedir);
			if (user.mbdb_uid != SM_NO_UID)
			{
				new->q_uid = user.mbdb_uid;
				new->q_gid = user.mbdb_gid;
				new->q_flags |= QGOODUID;
			}
			new->q_ruser = sm_rpool_strdup_x(e->e_rpool,
							 user.mbdb_name);
			if (user.mbdb_fullname[0] != '\0')
				new->q_fullname = sm_rpool_strdup_x(e->e_rpool,
							user.mbdb_fullname);
			if (!usershellok(user.mbdb_name, user.mbdb_shell))
			{
				new->q_flags |= QBOGUSSHELL;
			}
			if (bitset(EF_VRFYONLY, e->e_flags))
			{
				/* don't do any more now */
				new->q_state = QS_VERIFIED;
			}
			else if (!quoted)
				forward(new, sendq, aliaslevel, e);
		}
	}
	if (!QS_IS_DEAD(new->q_state))
		e->e_nrcpts++;

  testselfdestruct:
	new->q_flags |= QTHISPASS;
	if (tTd(26, 8))
	{
		sm_dprintf("testselfdestruct: ");
		printaddr(sm_debug_file(), new, false);
		if (tTd(26, 10))
		{
			sm_dprintf("SENDQ:\n");
			printaddr(sm_debug_file(), *sendq, true);
			sm_dprintf("----\n");
		}
	}
	if (new->q_alias == NULL && new != &e->e_from &&
	    QS_IS_DEAD(new->q_state))
	{
		for (q = *sendq; q != NULL; q = q->q_next)
		{
			if (!QS_IS_DEAD(q->q_state))
				break;
		}
		if (q == NULL)
		{
			new->q_state = QS_BADADDR;
			new->q_status = "5.4.6";
			usrerrenh(new->q_status,
				  "554 aliasing/forwarding loop broken");
		}
	}

  done:
	new->q_flags |= QTHISPASS;
	if (buf != buf0)
		sm_free(buf); /* XXX leak if above code raises exception */

	/*
	**  If we are at the top level, check to see if this has
	**  expanded to exactly one address.  If so, it can inherit
	**  the primaryness of the address.
	**
	**  While we're at it, clear the QTHISPASS bits.
	*/

	if (aliaslevel == 0)
	{
		int nrcpts = 0;
		ADDRESS *only = NULL;

		for (q = *sendq; q != NULL; q = q->q_next)
		{
			if (bitset(QTHISPASS, q->q_flags) &&
			    QS_IS_SENDABLE(q->q_state))
			{
				nrcpts++;
				only = q;
			}
			q->q_flags &= ~QTHISPASS;
		}
		if (nrcpts == 1)
		{
			/* check to see if this actually got a new owner */
			q = only;
			while ((q = q->q_alias) != NULL)
			{
				if (q->q_owner != NULL)
					break;
			}
			if (q == NULL)
				only->q_flags |= QPRIMARY;
		}
		else if (!initialdontsend && nrcpts > 0)
		{
			/* arrange for return receipt */
			e->e_flags |= EF_SENDRECEIPT;
			new->q_flags |= QEXPANDED;
			if (e->e_xfp != NULL &&
			    bitset(QPINGONSUCCESS, new->q_flags))
				(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
						     "%s... expanded to multiple addresses\n",
						     new->q_paddr);
		}
	}
	new->q_flags |= QRCPTOK;
	(void) sm_snprintf(buf0, sizeof(buf0), "%d", e->e_nrcpts);
	macdefine(&e->e_macro, A_TEMP, macid("{nrcpts}"), buf0);
	return new;
}

/*
**  FINDUSER -- find the password entry for a user.
**
**	This looks a lot like getpwnam, except that it may want to
**	do some fancier pattern matching in /etc/passwd.
**
**	This routine contains most of the time of many sendmail runs.
**	It deserves to be optimized.
**
**	Parameters:
**		name -- the name to match against.
**		fuzzyp -- an outarg that is set to true if this entry
**			was found using the fuzzy matching algorithm;
**			set to false otherwise.
**		user -- structure to fill in if user is found
**
**	Returns:
**		On success, fill in *user, set *fuzzyp and return EX_OK.
**		If the user was not found, return EX_NOUSER.
**		On error, return EX_TEMPFAIL or EX_OSERR.
**
**	Side Effects:
**		may modify name.
*/

int
finduser(name, fuzzyp, user)
	char *name;
	bool *fuzzyp;
	SM_MBDB_T *user;
{
#if MATCHGECOS
	register struct passwd *pw;
#endif /* MATCHGECOS */
	register char *p;
	bool tryagain;
	int status;

	if (tTd(29, 4))
		sm_dprintf("finduser(%s): ", name);

	*fuzzyp = false;

#if HESIOD && !HESIOD_ALLOW_NUMERIC_LOGIN
	/* DEC Hesiod getpwnam accepts numeric strings -- short circuit it */
	for (p = name; *p != '\0'; p++)
		if (!isascii(*p) || !isdigit(*p))
			break;
	if (*p == '\0')
	{
		if (tTd(29, 4))
			sm_dprintf("failed (numeric input)\n");
		return EX_NOUSER;
	}
#endif /* HESIOD && !HESIOD_ALLOW_NUMERIC_LOGIN */

	/* look up this login name using fast path */
	status = sm_mbdb_lookup(name, user);
	if (status != EX_NOUSER)
	{
		if (tTd(29, 4))
			sm_dprintf("%s (non-fuzzy)\n", sm_strexit(status));
		return status;
	}

	/* try mapping it to lower case */
	tryagain = false;
	for (p = name; *p != '\0'; p++)
	{
		if (isascii(*p) && isupper(*p))
		{
			*p = tolower(*p);
			tryagain = true;
		}
	}
	if (tryagain && (status = sm_mbdb_lookup(name, user)) != EX_NOUSER)
	{
		if (tTd(29, 4))
			sm_dprintf("%s (lower case)\n", sm_strexit(status));
		*fuzzyp = true;
		return status;
	}

#if MATCHGECOS
	/* see if fuzzy matching allowed */
	if (!MatchGecos)
	{
		if (tTd(29, 4))
			sm_dprintf("not found (fuzzy disabled)\n");
		return EX_NOUSER;
	}

	/* search for a matching full name instead */
	for (p = name; *p != '\0'; p++)
	{
		if (*p == (SpaceSub & 0177) || *p == '_')
			*p = ' ';
	}
	(void) setpwent();
	while ((pw = getpwent()) != NULL)
	{
		char buf[MAXNAME + 1];

# if 0
		if (sm_strcasecmp(pw->pw_name, name) == 0)
		{
			if (tTd(29, 4))
				sm_dprintf("found (case wrapped)\n");
			break;
		}
# endif /* 0 */

		sm_pwfullname(pw->pw_gecos, pw->pw_name, buf, sizeof(buf));
		if (strchr(buf, ' ') != NULL && sm_strcasecmp(buf, name) == 0)
		{
			if (tTd(29, 4))
				sm_dprintf("fuzzy matches %s\n", pw->pw_name);
			message("sending to login name %s", pw->pw_name);
			break;
		}
	}
	if (pw != NULL)
		*fuzzyp = true;
	else if (tTd(29, 4))
		sm_dprintf("no fuzzy match found\n");
# if DEC_OSF_BROKEN_GETPWENT	/* DEC OSF/1 3.2 or earlier */
	endpwent();
# endif /* DEC_OSF_BROKEN_GETPWENT */
	if (pw == NULL)
		return EX_NOUSER;
	sm_mbdb_frompw(user, pw);
	return EX_OK;
#else /* MATCHGECOS */
	if (tTd(29, 4))
		sm_dprintf("not found (fuzzy disabled)\n");
	return EX_NOUSER;
#endif /* MATCHGECOS */
}

/*
**  WRITABLE -- predicate returning if the file is writable.
**
**	This routine must duplicate the algorithm in sys/fio.c.
**	Unfortunately, we cannot use the access call since we
**	won't necessarily be the real uid when we try to
**	actually open the file.
**
**	Notice that ANY file with ANY execute bit is automatically
**	not writable.  This is also enforced by mailfile.
**
**	Parameters:
**		filename -- the file name to check.
**		ctladdr -- the controlling address for this file.
**		flags -- SFF_* flags to control the function.
**
**	Returns:
**		true -- if we will be able to write this file.
**		false -- if we cannot write this file.
**
**	Side Effects:
**		none.
*/

bool
writable(filename, ctladdr, flags)
	char *filename;
	ADDRESS *ctladdr;
	long flags;
{
	uid_t euid = 0;
	gid_t egid = 0;
	char *user = NULL;

	if (tTd(44, 5))
		sm_dprintf("writable(%s, 0x%lx)\n", filename, flags);

	/*
	**  File does exist -- check that it is writable.
	*/

	if (geteuid() != 0)
	{
		euid = geteuid();
		egid = getegid();
		user = NULL;
	}
	else if (ctladdr != NULL)
	{
		euid = ctladdr->q_uid;
		egid = ctladdr->q_gid;
		user = ctladdr->q_user;
	}
	else if (bitset(SFF_RUNASREALUID, flags))
	{
		euid = RealUid;
		egid = RealGid;
		user = RealUserName;
	}
	else if (FileMailer != NULL && !bitset(SFF_ROOTOK, flags))
	{
		if (FileMailer->m_uid == NO_UID)
		{
			euid = DefUid;
			user = DefUser;
		}
		else
		{
			euid = FileMailer->m_uid;
			user = NULL;
		}
		if (FileMailer->m_gid == NO_GID)
			egid = DefGid;
		else
			egid = FileMailer->m_gid;
	}
	else
	{
		euid = egid = 0;
		user = NULL;
	}
	if (!bitset(SFF_ROOTOK, flags))
	{
		if (euid == 0)
		{
			euid = DefUid;
			user = DefUser;
		}
		if (egid == 0)
			egid = DefGid;
	}
	if (geteuid() == 0 &&
	    (ctladdr == NULL || !bitset(QGOODUID, ctladdr->q_flags)))
		flags |= SFF_SETUIDOK;

	if (!bitnset(DBS_FILEDELIVERYTOSYMLINK, DontBlameSendmail))
		flags |= SFF_NOSLINK;
	if (!bitnset(DBS_FILEDELIVERYTOHARDLINK, DontBlameSendmail))
		flags |= SFF_NOHLINK;

	errno = safefile(filename, euid, egid, user, flags, S_IWRITE, NULL);
	return errno == 0;
}

/*
**  INCLUDE -- handle :include: specification.
**
**	Parameters:
**		fname -- filename to include.
**		forwarding -- if true, we are reading a .forward file.
**			if false, it's a :include: file.
**		ctladdr -- address template to use to fill in these
**			addresses -- effective user/group id are
**			the important things.
**		sendq -- a pointer to the head of the send queue
**			to put these addresses in.
**		aliaslevel -- the alias nesting depth.
**		e -- the current envelope.
**
**	Returns:
**		open error status
**
**	Side Effects:
**		reads the :include: file and sends to everyone
**		listed in that file.
**
**	Security Note:
**		If you have restricted chown (that is, you can't
**		give a file away), it is reasonable to allow programs
**		and files called from this :include: file to be to be
**		run as the owner of the :include: file.  This is bogus
**		if there is any chance of someone giving away a file.
**		We assume that pre-POSIX systems can give away files.
**
**		There is an additional restriction that if you
**		forward to a :include: file, it will not take on
**		the ownership of the :include: file.  This may not
**		be necessary, but shouldn't hurt.
*/

static jmp_buf	CtxIncludeTimeout;

int
include(fname, forwarding, ctladdr, sendq, aliaslevel, e)
	char *fname;
	bool forwarding;
	ADDRESS *ctladdr;
	ADDRESS **sendq;
	int aliaslevel;
	ENVELOPE *e;
{
	SM_FILE_T *volatile fp = NULL;
	char *oldto = e->e_to;
	char *oldfilename = FileName;
	int oldlinenumber = LineNumber;
	register SM_EVENT *ev = NULL;
	int nincludes;
	int mode;
	volatile bool maxreached = false;
	register ADDRESS *ca;
	volatile uid_t saveduid;
	volatile gid_t savedgid;
	volatile uid_t uid;
	volatile gid_t gid;
	char *volatile user;
	int rval = 0;
	volatile long sfflags = SFF_REGONLY;
	register char *p;
	bool safechown = false;
	volatile bool safedir = false;
	struct stat st;
	char buf[MAXLINE];

	if (tTd(27, 2))
		sm_dprintf("include(%s)\n", fname);
	if (tTd(27, 4))
		sm_dprintf("   ruid=%ld euid=%ld\n",
			(long) getuid(), (long) geteuid());
	if (tTd(27, 14))
	{
		sm_dprintf("ctladdr ");
		printaddr(sm_debug_file(), ctladdr, false);
	}

	if (tTd(27, 9))
		sm_dprintf("include: old uid = %ld/%ld\n",
			   (long) getuid(), (long) geteuid());

	if (forwarding)
	{
		sfflags |= SFF_MUSTOWN|SFF_ROOTOK;
		if (!bitnset(DBS_GROUPWRITABLEFORWARDFILE, DontBlameSendmail))
			sfflags |= SFF_NOGWFILES;
		if (!bitnset(DBS_WORLDWRITABLEFORWARDFILE, DontBlameSendmail))
			sfflags |= SFF_NOWWFILES;
	}
	else
	{
		if (!bitnset(DBS_GROUPWRITABLEINCLUDEFILE, DontBlameSendmail))
			sfflags |= SFF_NOGWFILES;
		if (!bitnset(DBS_WORLDWRITABLEINCLUDEFILE, DontBlameSendmail))
			sfflags |= SFF_NOWWFILES;
	}

	/*
	**  If RunAsUser set, won't be able to run programs as user
	**  so mark them as unsafe unless the administrator knows better.
	*/

	if ((geteuid() != 0 || RunAsUid != 0) &&
	    !bitnset(DBS_NONROOTSAFEADDR, DontBlameSendmail))
	{
		if (tTd(27, 4))
			sm_dprintf("include: not safe (euid=%ld, RunAsUid=%ld)\n",
				   (long) geteuid(), (long) RunAsUid);
		ctladdr->q_flags |= QUNSAFEADDR;
	}

	ca = getctladdr(ctladdr);
	if (ca == NULL ||
	    (ca->q_uid == DefUid && ca->q_gid == 0))
	{
		uid = DefUid;
		gid = DefGid;
		user = DefUser;
	}
	else
	{
		uid = ca->q_uid;
		gid = ca->q_gid;
		user = ca->q_user;
	}
#if MAILER_SETUID_METHOD != USE_SETUID
	saveduid = geteuid();
	savedgid = getegid();
	if (saveduid == 0)
	{
		if (!DontInitGroups)
		{
			if (initgroups(user, gid) == -1)
			{
				rval = EAGAIN;
				syserr("include: initgroups(%s, %ld) failed",
					user, (long) gid);
				goto resetuid;
			}
		}
		else
		{
			GIDSET_T gidset[1];

			gidset[0] = gid;
			if (setgroups(1, gidset) == -1)
			{
				rval = EAGAIN;
				syserr("include: setgroups() failed");
				goto resetuid;
			}
		}

		if (gid != 0 && setgid(gid) < -1)
		{
			rval = EAGAIN;
			syserr("setgid(%ld) failure", (long) gid);
			goto resetuid;
		}
		if (uid != 0)
		{
# if MAILER_SETUID_METHOD == USE_SETEUID
			if (seteuid(uid) < 0)
			{
				rval = EAGAIN;
				syserr("seteuid(%ld) failure (real=%ld, eff=%ld)",
					(long) uid, (long) getuid(), (long) geteuid());
				goto resetuid;
			}
# endif /* MAILER_SETUID_METHOD == USE_SETEUID */
# if MAILER_SETUID_METHOD == USE_SETREUID
			if (setreuid(0, uid) < 0)
			{
				rval = EAGAIN;
				syserr("setreuid(0, %ld) failure (real=%ld, eff=%ld)",
					(long) uid, (long) getuid(), (long) geteuid());
				goto resetuid;
			}
# endif /* MAILER_SETUID_METHOD == USE_SETREUID */
		}
	}
#endif /* MAILER_SETUID_METHOD != USE_SETUID */

	if (tTd(27, 9))
		sm_dprintf("include: new uid = %ld/%ld\n",
			   (long) getuid(), (long) geteuid());

	/*
	**  If home directory is remote mounted but server is down,
	**  this can hang or give errors; use a timeout to avoid this
	*/

	if (setjmp(CtxIncludeTimeout) != 0)
	{
		ctladdr->q_state = QS_QUEUEUP;
		errno = 0;

		/* return pseudo-error code */
		rval = E_SM_OPENTIMEOUT;
		goto resetuid;
	}
	if (TimeOuts.to_fileopen > 0)
		ev = sm_setevent(TimeOuts.to_fileopen, includetimeout, 0);
	else
		ev = NULL;


	/* check for writable parent directory */
	p = strrchr(fname, '/');
	if (p != NULL)
	{
		int ret;

		*p = '\0';
		ret = safedirpath(fname, uid, gid, user,
				  sfflags|SFF_SAFEDIRPATH, 0, 0);
		if (ret == 0)
		{
			/* in safe directory: relax chown & link rules */
			safedir = true;
			sfflags |= SFF_NOPATHCHECK;
		}
		else
		{
			if (bitnset((forwarding ?
				     DBS_FORWARDFILEINUNSAFEDIRPATH :
				     DBS_INCLUDEFILEINUNSAFEDIRPATH),
				    DontBlameSendmail))
				sfflags |= SFF_NOPATHCHECK;
			else if (bitnset((forwarding ?
					  DBS_FORWARDFILEINGROUPWRITABLEDIRPATH :
					  DBS_INCLUDEFILEINGROUPWRITABLEDIRPATH),
					 DontBlameSendmail) &&
				 ret == E_SM_GWDIR)
			{
				setbitn(DBS_GROUPWRITABLEDIRPATHSAFE,
					DontBlameSendmail);
				ret = safedirpath(fname, uid, gid, user,
						  sfflags|SFF_SAFEDIRPATH,
						  0, 0);
				clrbitn(DBS_GROUPWRITABLEDIRPATHSAFE,
					DontBlameSendmail);
				if (ret == 0)
					sfflags |= SFF_NOPATHCHECK;
				else
					sfflags |= SFF_SAFEDIRPATH;
			}
			else
				sfflags |= SFF_SAFEDIRPATH;
			if (ret > E_PSEUDOBASE &&
			    !bitnset((forwarding ?
				      DBS_FORWARDFILEINUNSAFEDIRPATHSAFE :
				      DBS_INCLUDEFILEINUNSAFEDIRPATHSAFE),
				     DontBlameSendmail))
			{
				if (LogLevel > 11)
					sm_syslog(LOG_INFO, e->e_id,
						  "%s: unsafe directory path, marked unsafe",
						  shortenstring(fname, MAXSHORTSTR));
				ctladdr->q_flags |= QUNSAFEADDR;
			}
		}
		*p = '/';
	}

	/* allow links only in unwritable directories */
	if (!safedir &&
	    !bitnset((forwarding ?
		      DBS_LINKEDFORWARDFILEINWRITABLEDIR :
		      DBS_LINKEDINCLUDEFILEINWRITABLEDIR),
		     DontBlameSendmail))
		sfflags |= SFF_NOLINK;

	rval = safefile(fname, uid, gid, user, sfflags, S_IREAD, &st);
	if (rval != 0)
	{
		/* don't use this :include: file */
		if (tTd(27, 4))
			sm_dprintf("include: not safe (uid=%ld): %s\n",
				   (long) uid, sm_errstring(rval));
	}
	else if ((fp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, fname,
				  SM_IO_RDONLY, NULL)) == NULL)
	{
		rval = errno;
		if (tTd(27, 4))
			sm_dprintf("include: open: %s\n", sm_errstring(rval));
	}
	else if (filechanged(fname, sm_io_getinfo(fp,SM_IO_WHAT_FD, NULL), &st))
	{
		rval = E_SM_FILECHANGE;
		if (tTd(27, 4))
			sm_dprintf("include: file changed after open\n");
	}
	if (ev != NULL)
		sm_clrevent(ev);

resetuid:

#if HASSETREUID || USESETEUID
	if (saveduid == 0)
	{
		if (uid != 0)
		{
# if USESETEUID
			if (seteuid(0) < 0)
				syserr("!seteuid(0) failure (real=%ld, eff=%ld)",
				       (long) getuid(), (long) geteuid());
# else /* USESETEUID */
			if (setreuid(-1, 0) < 0)
				syserr("!setreuid(-1, 0) failure (real=%ld, eff=%ld)",
				       (long) getuid(), (long) geteuid());
			if (setreuid(RealUid, 0) < 0)
				syserr("!setreuid(%ld, 0) failure (real=%ld, eff=%ld)",
				       (long) RealUid, (long) getuid(),
				       (long) geteuid());
# endif /* USESETEUID */
		}
		if (setgid(savedgid) < 0)
			syserr("!setgid(%ld) failure (real=%ld eff=%ld)",
			       (long) savedgid, (long) getgid(),
			       (long) getegid());
	}
#endif /* HASSETREUID || USESETEUID */

	if (tTd(27, 9))
		sm_dprintf("include: reset uid = %ld/%ld\n",
			   (long) getuid(), (long) geteuid());

	if (rval == E_SM_OPENTIMEOUT)
		usrerr("451 4.4.1 open timeout on %s", fname);

	if (fp == NULL)
		return rval;

	if (fstat(sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL), &st) < 0)
	{
		rval = errno;
		syserr("Cannot fstat %s!", fname);
		(void) sm_io_close(fp, SM_TIME_DEFAULT);
		return rval;
	}

	/* if path was writable, check to avoid file giveaway tricks */
	safechown = chownsafe(sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL), safedir);
	if (tTd(27, 6))
		sm_dprintf("include: parent of %s is %s, chown is %ssafe\n",
			   fname, safedir ? "safe" : "dangerous",
			   safechown ? "" : "un");

	/* if no controlling user or coming from an alias delivery */
	if (safechown &&
	    (ca == NULL ||
	     (ca->q_uid == DefUid && ca->q_gid == 0)))
	{
		ctladdr->q_uid = st.st_uid;
		ctladdr->q_gid = st.st_gid;
		ctladdr->q_flags |= QGOODUID;
	}
	if (ca != NULL && ca->q_uid == st.st_uid)
	{
		/* optimization -- avoid getpwuid if we already have info */
		ctladdr->q_flags |= ca->q_flags & QBOGUSSHELL;
		ctladdr->q_ruser = ca->q_ruser;
	}
	else if (!forwarding)
	{
		register struct passwd *pw;

		pw = sm_getpwuid(st.st_uid);
		if (pw == NULL)
		{
			ctladdr->q_uid = st.st_uid;
			ctladdr->q_flags |= QBOGUSSHELL;
		}
		else
		{
			char *sh;

			ctladdr->q_ruser = sm_rpool_strdup_x(e->e_rpool,
							     pw->pw_name);
			if (safechown)
				sh = pw->pw_shell;
			else
				sh = "/SENDMAIL/ANY/SHELL/";
			if (!usershellok(pw->pw_name, sh))
			{
				if (LogLevel > 11)
					sm_syslog(LOG_INFO, e->e_id,
						  "%s: user %s has bad shell %s, marked %s",
						  shortenstring(fname,
								MAXSHORTSTR),
						  pw->pw_name, sh,
						  safechown ? "bogus" : "unsafe");
				if (safechown)
					ctladdr->q_flags |= QBOGUSSHELL;
				else
					ctladdr->q_flags |= QUNSAFEADDR;
			}
		}
	}

	if (bitset(EF_VRFYONLY, e->e_flags))
	{
		/* don't do any more now */
		ctladdr->q_state = QS_VERIFIED;
		e->e_nrcpts++;
		(void) sm_io_close(fp, SM_TIME_DEFAULT);
		return rval;
	}

	/*
	**  Check to see if some bad guy can write this file
	**
	**	Group write checking could be more clever, e.g.,
	**	guessing as to which groups are actually safe ("sys"
	**	may be; "user" probably is not).
	*/

	mode = S_IWOTH;
	if (!bitnset((forwarding ?
		      DBS_GROUPWRITABLEFORWARDFILESAFE :
		      DBS_GROUPWRITABLEINCLUDEFILESAFE),
		     DontBlameSendmail))
		mode |= S_IWGRP;

	if (bitset(mode, st.st_mode))
	{
		if (tTd(27, 6))
			sm_dprintf("include: %s is %s writable, marked unsafe\n",
				   shortenstring(fname, MAXSHORTSTR),
				   bitset(S_IWOTH, st.st_mode) ? "world"
							       : "group");
		if (LogLevel > 11)
			sm_syslog(LOG_INFO, e->e_id,
				  "%s: %s writable %s file, marked unsafe",
				  shortenstring(fname, MAXSHORTSTR),
				  bitset(S_IWOTH, st.st_mode) ? "world" : "group",
				  forwarding ? "forward" : ":include:");
		ctladdr->q_flags |= QUNSAFEADDR;
	}

	/* read the file -- each line is a comma-separated list. */
	FileName = fname;
	LineNumber = 0;
	ctladdr->q_flags &= ~QSELFREF;
	nincludes = 0;
	while (sm_io_fgets(fp, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0 &&
	       !maxreached)
	{
		fixcrlf(buf, true);
		LineNumber++;
		if (buf[0] == '#' || buf[0] == '\0')
			continue;

		/* <sp>#@# introduces a comment anywhere */
		/* for Japanese character sets */
		for (p = buf; (p = strchr(++p, '#')) != NULL; )
		{
			if (p[1] == '@' && p[2] == '#' &&
			    isascii(p[-1]) && isspace(p[-1]) &&
			    (p[3] == '\0' || (isascii(p[3]) && isspace(p[3]))))
			{
				--p;
				while (p > buf && isascii(p[-1]) &&
				       isspace(p[-1]))
					--p;
				p[0] = '\0';
				break;
			}
		}
		if (buf[0] == '\0')
			continue;

		e->e_to = NULL;
		message("%s to %s",
			forwarding ? "forwarding" : "sending", buf);
		if (forwarding && LogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "forward %.200s => %s",
				  oldto, shortenstring(buf, MAXSHORTSTR));

		nincludes += sendtolist(buf, ctladdr, sendq, aliaslevel + 1, e);

		if (forwarding &&
		    MaxForwardEntries > 0 &&
		    nincludes >= MaxForwardEntries)
		{
			/* just stop reading and processing further entries */
#if 0
			/* additional: (?) */
			ctladdr->q_state = QS_DONTSEND;
#endif /* 0 */

			syserr("Attempt to forward to more than %d addresses (in %s)!",
				MaxForwardEntries, fname);
			maxreached = true;
		}
	}

	if (sm_io_error(fp) && tTd(27, 3))
		sm_dprintf("include: read error: %s\n", sm_errstring(errno));
	if (nincludes > 0 && !bitset(QSELFREF, ctladdr->q_flags))
	{
		if (aliaslevel <= MaxAliasRecursion ||
		    ctladdr->q_state != QS_BADADDR)
		{
			ctladdr->q_state = QS_DONTSEND;
			if (tTd(27, 5))
			{
				sm_dprintf("include: QS_DONTSEND ");
				printaddr(sm_debug_file(), ctladdr, false);
			}
		}
	}

	(void) sm_io_close(fp, SM_TIME_DEFAULT);
	FileName = oldfilename;
	LineNumber = oldlinenumber;
	e->e_to = oldto;
	return rval;
}

static void
includetimeout(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(CtxIncludeTimeout, 1);
}

/*
**  SENDTOARGV -- send to an argument vector.
**
**	Parameters:
**		argv -- argument vector to send to.
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		puts all addresses on the argument vector onto the
**			send queue.
*/

void
sendtoargv(argv, e)
	register char **argv;
	register ENVELOPE *e;
{
	register char *p;

	while ((p = *argv++) != NULL)
		(void) sendtolist(p, NULLADDR, &e->e_sendqueue, 0, e);
}

/*
**  GETCTLADDR -- get controlling address from an address header.
**
**	If none, get one corresponding to the effective userid.
**
**	Parameters:
**		a -- the address to find the controller of.
**
**	Returns:
**		the controlling address.
*/

ADDRESS *
getctladdr(a)
	register ADDRESS *a;
{
	while (a != NULL && !bitset(QGOODUID, a->q_flags))
		a = a->q_alias;
	return a;
}

/*
**  SELF_REFERENCE -- check to see if an address references itself
**
**	The check is done through a chain of aliases.  If it is part of
**	a loop, break the loop at the "best" address, that is, the one
**	that exists as a real user.
**
**	This is to handle the case of:
**		awc:		Andrew.Chang
**		Andrew.Chang:	awc@mail.server
**	which is a problem only on mail.server.
**
**	Parameters:
**		a -- the address to check.
**
**	Returns:
**		The address that should be retained.
*/

static ADDRESS *
self_reference(a)
	ADDRESS *a;
{
	ADDRESS *b;		/* top entry in self ref loop */
	ADDRESS *c;		/* entry that point to a real mail box */

	if (tTd(27, 1))
		sm_dprintf("self_reference(%s)\n", a->q_paddr);

	for (b = a->q_alias; b != NULL; b = b->q_alias)
	{
		if (sameaddr(a, b))
			break;
	}

	if (b == NULL)
	{
		if (tTd(27, 1))
			sm_dprintf("\t... no self ref\n");
		return NULL;
	}

	/*
	**  Pick the first address that resolved to a real mail box
	**  i.e has a mbdb entry.  The returned value will be marked
	**  QSELFREF in recipient(), which in turn will disable alias()
	**  from marking it as QS_IS_DEAD(), which mean it will be used
	**  as a deliverable address.
	**
	**  The 2 key thing to note here are:
	**	1) we are in a recursive call sequence:
	**		alias->sendtolist->recipient->alias
	**	2) normally, when we return back to alias(), the address
	**	   will be marked QS_EXPANDED, since alias() assumes the
	**	   expanded form will be used instead of the current address.
	**	   This behaviour is turned off if the address is marked
	**	   QSELFREF.  We set QSELFREF when we return to recipient().
	*/

	c = a;
	while (c != NULL)
	{
		if (tTd(27, 10))
			sm_dprintf("  %s", c->q_user);
		if (bitnset(M_HASPWENT, c->q_mailer->m_flags))
		{
			SM_MBDB_T user;

			if (tTd(27, 2))
				sm_dprintf("\t... getpwnam(%s)... ", c->q_user);
			if (sm_mbdb_lookup(c->q_user, &user) == EX_OK)
			{
				if (tTd(27, 2))
					sm_dprintf("found\n");

				/* ought to cache results here */
				if (sameaddr(b, c))
					return b;
				else
					return c;
			}
			if (tTd(27, 2))
				sm_dprintf("failed\n");
		}
		else
		{
			/* if local delivery, compare usernames */
			if (bitnset(M_LOCALMAILER, c->q_mailer->m_flags) &&
			    b->q_mailer == c->q_mailer)
			{
				if (tTd(27, 2))
					sm_dprintf("\t... local match (%s)\n",
						c->q_user);
				if (sameaddr(b, c))
					return b;
				else
					return c;
			}
		}
		if (tTd(27, 10))
			sm_dprintf("\n");
		c = c->q_alias;
	}

	if (tTd(27, 1))
		sm_dprintf("\t... cannot break loop for \"%s\"\n", a->q_paddr);

	return NULL;
}
