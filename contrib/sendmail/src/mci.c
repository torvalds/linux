/*
 * Copyright (c) 1998-2005, 2010 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: mci.c,v 8.225 2013-11-22 20:51:56 ca Exp $")

#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */

#include <dirent.h>

static int	mci_generate_persistent_path __P((const char *, char *,
						  int, bool));
static bool	mci_load_persistent __P((MCI *));
static void	mci_uncache __P((MCI **, bool));
static int	mci_lock_host_statfile __P((MCI *));
static int	mci_read_persistent __P((SM_FILE_T *, MCI *));

/*
**  Mail Connection Information (MCI) Caching Module.
**
**	There are actually two separate things cached.  The first is
**	the set of all open connections -- these are stored in a
**	(small) list.  The second is stored in the symbol table; it
**	has the overall status for all hosts, whether or not there
**	is a connection open currently.
**
**	There should never be too many connections open (since this
**	could flood the socket table), nor should a connection be
**	allowed to sit idly for too long.
**
**	MaxMciCache is the maximum number of open connections that
**	will be supported.
**
**	MciCacheTimeout is the time (in seconds) that a connection
**	is permitted to survive without activity.
**
**	We actually try any cached connections by sending a RSET
**	before we use them; if the RSET fails we close down the
**	connection and reopen it (see smtpprobe()).
**
**	The persistent MCI code is donated by Mark Lovell and Paul
**	Vixie.  It is based on the long term host status code in KJS
**	written by Paul but has been adapted by Mark to fit into the
**	MCI structure.
*/

static MCI	**MciCache;		/* the open connection cache */

/*
**  MCI_CACHE -- enter a connection structure into the open connection cache
**
**	This may cause something else to be flushed.
**
**	Parameters:
**		mci -- the connection to cache.
**
**	Returns:
**		none.
*/

void
mci_cache(mci)
	register MCI *mci;
{
	register MCI **mcislot;

	/*
	**  Find the best slot.  This may cause expired connections
	**  to be closed.
	*/

	mcislot = mci_scan(mci);
	if (mcislot == NULL)
	{
		/* we don't support caching */
		return;
	}

	if (mci->mci_host == NULL)
		return;

	/* if this is already cached, we are done */
	if (bitset(MCIF_CACHED, mci->mci_flags))
		return;

	/* otherwise we may have to clear the slot */
	if (*mcislot != NULL)
		mci_uncache(mcislot, true);

	if (tTd(42, 5))
		sm_dprintf("mci_cache: caching %p (%s) in slot %d\n",
			   mci, mci->mci_host, (int) (mcislot - MciCache));
	if (tTd(91, 100))
		sm_syslog(LOG_DEBUG, CurEnv->e_id,
			  "mci_cache: caching %lx (%.100s) in slot %d",
			  (unsigned long) mci, mci->mci_host,
			  (int) (mcislot - MciCache));

	*mcislot = mci;
	mci->mci_flags |= MCIF_CACHED;
}
/*
**  MCI_SCAN -- scan the cache, flush junk, and return best slot
**
**	Parameters:
**		savemci -- never flush this one.  Can be null.
**
**	Returns:
**		The LRU (or empty) slot.
*/

MCI **
mci_scan(savemci)
	MCI *savemci;
{
	time_t now;
	register MCI **bestmci;
	register MCI *mci;
	register int i;

	if (MaxMciCache <= 0)
	{
		/* we don't support caching */
		return NULL;
	}

	if (MciCache == NULL)
	{
		/* first call */
		MciCache = (MCI **) sm_pmalloc_x(MaxMciCache * sizeof(*MciCache));
		memset((char *) MciCache, '\0', MaxMciCache * sizeof(*MciCache));
		return &MciCache[0];
	}

	now = curtime();
	bestmci = &MciCache[0];
	for (i = 0; i < MaxMciCache; i++)
	{
		mci = MciCache[i];
		if (mci == NULL || mci->mci_state == MCIS_CLOSED)
		{
			bestmci = &MciCache[i];
			continue;
		}
		if ((mci->mci_lastuse + MciCacheTimeout <= now ||
		     (mci->mci_mailer != NULL &&
		      mci->mci_mailer->m_maxdeliveries > 0 &&
		      mci->mci_deliveries + 1 >= mci->mci_mailer->m_maxdeliveries))&&
		    mci != savemci)
		{
			/* connection idle too long or too many deliveries */
			bestmci = &MciCache[i];

			/* close it */
			mci_uncache(bestmci, true);
			continue;
		}
		if (*bestmci == NULL)
			continue;
		if (mci->mci_lastuse < (*bestmci)->mci_lastuse)
			bestmci = &MciCache[i];
	}
	return bestmci;
}
/*
**  MCI_UNCACHE -- remove a connection from a slot.
**
**	May close a connection.
**
**	Parameters:
**		mcislot -- the slot to empty.
**		doquit -- if true, send QUIT protocol on this connection.
**			  if false, we are assumed to be in a forked child;
**				all we want to do is close the file(s).
**
**	Returns:
**		none.
*/

static void
mci_uncache(mcislot, doquit)
	register MCI **mcislot;
	bool doquit;
{
	register MCI *mci;
	extern ENVELOPE BlankEnvelope;

	mci = *mcislot;
	if (mci == NULL)
		return;
	*mcislot = NULL;
	if (mci->mci_host == NULL)
		return;

	mci_unlock_host(mci);

	if (tTd(42, 5))
		sm_dprintf("mci_uncache: uncaching %p (%s) from slot %d (%d)\n",
			   mci, mci->mci_host, (int) (mcislot - MciCache),
			   doquit);
	if (tTd(91, 100))
		sm_syslog(LOG_DEBUG, CurEnv->e_id,
			  "mci_uncache: uncaching %lx (%.100s) from slot %d (%d)",
			  (unsigned long) mci, mci->mci_host,
			  (int) (mcislot - MciCache), doquit);

	mci->mci_deliveries = 0;
	if (doquit)
	{
		message("Closing connection to %s", mci->mci_host);

		mci->mci_flags &= ~MCIF_CACHED;

		/* only uses the envelope to flush the transcript file */
		if (mci->mci_state != MCIS_CLOSED)
			smtpquit(mci->mci_mailer, mci, &BlankEnvelope);
#if XLA
		xla_host_end(mci->mci_host);
#endif /* XLA */
	}
	else
	{
		if (mci->mci_in != NULL)
			(void) sm_io_close(mci->mci_in, SM_TIME_DEFAULT);
		if (mci->mci_out != NULL)
			(void) sm_io_close(mci->mci_out, SM_TIME_DEFAULT);
		mci->mci_in = mci->mci_out = NULL;
		mci->mci_state = MCIS_CLOSED;
		mci->mci_exitstat = EX_OK;
		mci->mci_errno = 0;
		mci->mci_flags = 0;

		mci->mci_retryrcpt = false;
		mci->mci_tolist = NULL;
#if PIPELINING
		mci->mci_okrcpts = 0;
#endif /* PIPELINING */
	}

	SM_FREE_CLR(mci->mci_status);
	SM_FREE_CLR(mci->mci_rstatus);
	SM_FREE_CLR(mci->mci_heloname);
	if (mci->mci_rpool != NULL)
	{
		sm_rpool_free(mci->mci_rpool);
		mci->mci_macro.mac_rpool = NULL;
		mci->mci_rpool = NULL;
	}
}
/*
**  MCI_FLUSH -- flush the entire cache
**
**	Parameters:
**		doquit -- if true, send QUIT protocol.
**			  if false, just close the connection.
**		allbut -- but leave this one open.
**
**	Returns:
**		none.
*/

void
mci_flush(doquit, allbut)
	bool doquit;
	MCI *allbut;
{
	register int i;

	if (MciCache == NULL)
		return;

	for (i = 0; i < MaxMciCache; i++)
	{
		if (allbut != MciCache[i])
			mci_uncache(&MciCache[i], doquit);
	}
}

/*
**  MCI_CLR_EXTENSIONS -- clear knowledge about SMTP extensions
**
**	Parameters:
**		mci -- the connection to clear.
**
**	Returns:
**		none.
*/

void
mci_clr_extensions(mci)
	MCI *mci;
{
	if (mci == NULL)
		return;

	mci->mci_flags &= ~MCIF_EXTENS;
	mci->mci_maxsize = 0;
	mci->mci_min_by = 0;
#if SASL
	mci->mci_saslcap = NULL;
#endif /* SASL */
}

/*
**  MCI_GET -- get information about a particular host
**
**	Parameters:
**		host -- host to look for.
**		m -- mailer.
**
**	Returns:
**		mci for this host (might be new).
*/

MCI *
mci_get(host, m)
	char *host;
	MAILER *m;
{
	register MCI *mci;
	register STAB *s;
	extern SOCKADDR CurHostAddr;

	/* clear CurHostAddr so we don't get a bogus address with this name */
	memset(&CurHostAddr, '\0', sizeof(CurHostAddr));

	/* clear out any expired connections */
	(void) mci_scan(NULL);

	if (m->m_mno < 0)
		syserr("!negative mno %d (%s)", m->m_mno, m->m_name);

	s = stab(host, ST_MCI + m->m_mno, ST_ENTER);
	mci = &s->s_mci;

	/* initialize per-message data */
	mci->mci_retryrcpt = false;
	mci->mci_tolist = NULL;
#if PIPELINING
	mci->mci_okrcpts = 0;
#endif /* PIPELINING */
	mci->mci_flags &= ~MCIF_NOTSTICKY;

	if (mci->mci_rpool == NULL)
		mci->mci_rpool = sm_rpool_new_x(NULL);

	if (mci->mci_macro.mac_rpool == NULL)
		mci->mci_macro.mac_rpool = mci->mci_rpool;

	/*
	**  We don't need to load the persistent data if we have data
	**  already loaded in the cache.
	*/

	if (mci->mci_host == NULL &&
	    (mci->mci_host = s->s_name) != NULL &&
	    !mci_load_persistent(mci))
	{
		if (tTd(42, 2))
			sm_dprintf("mci_get(%s %s): lock failed\n",
				host, m->m_name);
		mci->mci_exitstat = EX_TEMPFAIL;
		mci->mci_state = MCIS_CLOSED;
		mci->mci_statfile = NULL;
		return mci;
	}

	if (tTd(42, 2))
	{
		sm_dprintf("mci_get(%s %s): mci_state=%d, _flags=%lx, _exitstat=%d, _errno=%d\n",
			host, m->m_name, mci->mci_state, mci->mci_flags,
			mci->mci_exitstat, mci->mci_errno);
	}

	if (mci->mci_state == MCIS_OPEN)
	{
		/* poke the connection to see if it's still alive */
		(void) smtpprobe(mci);

		/* reset the stored state in the event of a timeout */
		if (mci->mci_state != MCIS_OPEN)
		{
			mci->mci_errno = 0;
			mci->mci_exitstat = EX_OK;
			mci->mci_state = MCIS_CLOSED;
		}
		else
		{
			/* get peer host address */
			/* (this should really be in the mci struct) */
			SOCKADDR_LEN_T socklen = sizeof(CurHostAddr);

			(void) getpeername(sm_io_getinfo(mci->mci_in,
							 SM_IO_WHAT_FD, NULL),
				(struct sockaddr *) &CurHostAddr, &socklen);
		}
	}
	if (mci->mci_state == MCIS_CLOSED)
	{
		time_t now = curtime();

		/* if this info is stale, ignore it */
		if (mci->mci_lastuse + MciInfoTimeout <= now)
		{
			mci->mci_lastuse = now;
			mci->mci_errno = 0;
			mci->mci_exitstat = EX_OK;
		}
	}

	return mci;
}

/*
**  MCI_CLOSE -- (forcefully) close files used for a connection.
**	Note: this is a last resort, usually smtpquit() or endmailer()
**		should be used to close a connection.
**
**	Parameters:
**		mci -- the connection to close.
**		where -- where has this been called?
**
**	Returns:
**		none.
*/

void
mci_close(mci, where)
	MCI *mci;
	char *where;
{
	bool dumped;

	if (mci == NULL)
		return;
	dumped = false;
	if (mci->mci_out != NULL)
	{
		if (tTd(56, 1))
		{
			sm_dprintf("mci_close: mci_out!=NULL, where=%s\n",
				where);
			mci_dump(sm_debug_file(), mci, false);
			dumped = true;
		}
		(void) sm_io_close(mci->mci_out, SM_TIME_DEFAULT);
		mci->mci_out = NULL;
	}
	if (mci->mci_in != NULL)
	{
		if (tTd(56, 1))
		{
			sm_dprintf("mci_close: mci_in!=NULL, where=%s\n",
				where);
			if (!dumped)
				mci_dump(sm_debug_file(), mci, false);
		}
		(void) sm_io_close(mci->mci_in, SM_TIME_DEFAULT);
		mci->mci_in = NULL;
	}
	mci->mci_state = MCIS_CLOSED;
}

/*
**  MCI_NEW -- allocate new MCI structure
**
**	Parameters:
**		rpool -- if non-NULL: allocate from that rpool.
**
**	Returns:
**		mci (new).
*/

MCI *
mci_new(rpool)
	SM_RPOOL_T *rpool;
{
	register MCI *mci;

	if (rpool == NULL)
		mci = (MCI *) sm_malloc_x(sizeof(*mci));
	else
		mci = (MCI *) sm_rpool_malloc_x(rpool, sizeof(*mci));
	memset((char *) mci, '\0', sizeof(*mci));
	mci->mci_rpool = sm_rpool_new_x(NULL);
	mci->mci_macro.mac_rpool = mci->mci_rpool;
	return mci;
}
/*
**  MCI_MATCH -- check connection cache for a particular host
**
**	Parameters:
**		host -- host to look for.
**		m -- mailer.
**
**	Returns:
**		true iff open connection exists.
*/

bool
mci_match(host, m)
	char *host;
	MAILER *m;
{
	register MCI *mci;
	register STAB *s;

	if (m->m_mno < 0 || m->m_mno > MAXMAILERS)
		return false;
	s = stab(host, ST_MCI + m->m_mno, ST_FIND);
	if (s == NULL)
		return false;

	mci = &s->s_mci;
	return mci->mci_state == MCIS_OPEN;
}
/*
**  MCI_SETSTAT -- set status codes in MCI structure.
**
**	Parameters:
**		mci -- the MCI structure to set.
**		xstat -- the exit status code.
**		dstat -- the DSN status code.
**		rstat -- the SMTP status code.
**
**	Returns:
**		none.
*/

void
mci_setstat(mci, xstat, dstat, rstat)
	MCI *mci;
	int xstat;
	char *dstat;
	char *rstat;
{
	/* protocol errors should never be interpreted as sticky */
	if (xstat != EX_NOTSTICKY && xstat != EX_PROTOCOL)
		mci->mci_exitstat = xstat;

	SM_FREE_CLR(mci->mci_status);
	if (dstat != NULL)
		mci->mci_status = sm_strdup_x(dstat);

	SM_FREE_CLR(mci->mci_rstatus);
	if (rstat != NULL)
		mci->mci_rstatus = sm_strdup_x(rstat);
}
/*
**  MCI_DUMP -- dump the contents of an MCI structure.
**
**	Parameters:
**		fp -- output file pointer
**		mci -- the MCI structure to dump.
**
**	Returns:
**		none.
**
**	Side Effects:
**		none.
*/

struct mcifbits
{
	int	mcif_bit;	/* flag bit */
	char	*mcif_name;	/* flag name */
};
static struct mcifbits	MciFlags[] =
{
	{ MCIF_VALID,		"VALID"		},
	{ MCIF_CACHED,		"CACHED"	},
	{ MCIF_ESMTP,		"ESMTP"		},
	{ MCIF_EXPN,		"EXPN"		},
	{ MCIF_SIZE,		"SIZE"		},
	{ MCIF_8BITMIME,	"8BITMIME"	},
	{ MCIF_7BIT,		"7BIT"		},
	{ MCIF_INHEADER,	"INHEADER"	},
	{ MCIF_CVT8TO7,		"CVT8TO7"	},
	{ MCIF_DSN,		"DSN"		},
	{ MCIF_8BITOK,		"8BITOK"	},
	{ MCIF_CVT7TO8,		"CVT7TO8"	},
	{ MCIF_INMIME,		"INMIME"	},
	{ MCIF_AUTH,		"AUTH"		},
	{ MCIF_AUTH2,		"AUTH2"		},
	{ MCIF_AUTHACT,		"AUTHACT"	},
	{ MCIF_ENHSTAT,		"ENHSTAT"	},
	{ MCIF_PIPELINED,	"PIPELINED"	},
#if STARTTLS
	{ MCIF_TLS,		"TLS"		},
	{ MCIF_TLSACT,		"TLSACT"	},
#endif /* STARTTLS */
	{ MCIF_DLVR_BY,		"DLVR_BY"	},
	{ 0,			NULL		}
};

void
mci_dump(fp, mci, logit)
	SM_FILE_T *fp;
	register MCI *mci;
	bool logit;
{
	register char *p;
	char *sep;
	char buf[4000];

	sep = logit ? " " : "\n\t";
	p = buf;
	(void) sm_snprintf(p, SPACELEFT(buf, p), "MCI@%p: ", mci);
	p += strlen(p);
	if (mci == NULL)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), "NULL");
		goto printit;
	}
	(void) sm_snprintf(p, SPACELEFT(buf, p), "flags=%lx", mci->mci_flags);
	p += strlen(p);

	/*
	**  The following check is just for paranoia.  It protects the
	**  assignment in the if() clause. If there's not some minimum
	**  amount of space we can stop right now. The check will not
	**  trigger as long as sizeof(buf)=4000.
	*/

	if (p >= buf + sizeof(buf) - 4)
		goto printit;
	if (mci->mci_flags != 0)
	{
		struct mcifbits *f;

		*p++ = '<';	/* protected above */
		for (f = MciFlags; f->mcif_bit != 0; f++)
		{
			if (!bitset(f->mcif_bit, mci->mci_flags))
				continue;
			(void) sm_strlcpyn(p, SPACELEFT(buf, p), 2,
					   f->mcif_name, ",");
			p += strlen(p);
		}
		p[-1] = '>';
	}

	/* Note: sm_snprintf() takes care of NULL arguments for %s */
	(void) sm_snprintf(p, SPACELEFT(buf, p),
		",%serrno=%d, herrno=%d, exitstat=%d, state=%d, pid=%d,%s",
		sep, mci->mci_errno, mci->mci_herrno,
		mci->mci_exitstat, mci->mci_state, (int) mci->mci_pid, sep);
	p += strlen(p);
	(void) sm_snprintf(p, SPACELEFT(buf, p),
		"maxsize=%ld, phase=%s, mailer=%s,%s",
		mci->mci_maxsize, mci->mci_phase,
		mci->mci_mailer == NULL ? "NULL" : mci->mci_mailer->m_name,
		sep);
	p += strlen(p);
	(void) sm_snprintf(p, SPACELEFT(buf, p),
		"status=%s, rstatus=%s,%s",
		mci->mci_status, mci->mci_rstatus, sep);
	p += strlen(p);
	(void) sm_snprintf(p, SPACELEFT(buf, p),
		"host=%s, lastuse=%s",
		mci->mci_host, ctime(&mci->mci_lastuse));
printit:
	if (logit)
		sm_syslog(LOG_DEBUG, CurEnv->e_id, "%.1000s", buf);
	else
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s\n", buf);
}
/*
**  MCI_DUMP_ALL -- print the entire MCI cache
**
**	Parameters:
**		fp -- output file pointer
**		logit -- if set, log the result instead of printing
**			to stdout.
**
**	Returns:
**		none.
*/

void
mci_dump_all(fp, logit)
	SM_FILE_T *fp;
	bool logit;
{
	register int i;

	if (MciCache == NULL)
		return;

	for (i = 0; i < MaxMciCache; i++)
		mci_dump(fp, MciCache[i], logit);
}
/*
**  MCI_LOCK_HOST -- Lock host while sending.
**
**	If we are contacting a host, we'll need to
**	update the status information in the host status
**	file, and if we want to do that, we ought to have
**	locked it. This has the (according to some)
**	desirable effect of serializing connectivity with
**	remote hosts -- i.e.: one connection to a given
**	host at a time.
**
**	Parameters:
**		mci -- containing the host we want to lock.
**
**	Returns:
**		EX_OK	    -- got the lock.
**		EX_TEMPFAIL -- didn't get the lock.
*/

int
mci_lock_host(mci)
	MCI *mci;
{
	if (mci == NULL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_lock_host: NULL mci\n");
		return EX_OK;
	}

	if (!SingleThreadDelivery)
		return EX_OK;

	return mci_lock_host_statfile(mci);
}

static int
mci_lock_host_statfile(mci)
	MCI *mci;
{
	int save_errno = errno;
	int retVal = EX_OK;
	char fname[MAXPATHLEN];

	if (HostStatDir == NULL || mci->mci_host == NULL)
		return EX_OK;

	if (tTd(56, 2))
		sm_dprintf("mci_lock_host: attempting to lock %s\n",
			   mci->mci_host);

	if (mci_generate_persistent_path(mci->mci_host, fname, sizeof(fname),
					 true) < 0)
	{
		/* of course this should never happen */
		if (tTd(56, 2))
			sm_dprintf("mci_lock_host: Failed to generate host path for %s\n",
				   mci->mci_host);

		retVal = EX_TEMPFAIL;
		goto cleanup;
	}

	mci->mci_statfile = safefopen(fname, O_RDWR, FileMode,
				      SFF_NOLOCK|SFF_NOLINK|SFF_OPENASROOT|SFF_REGONLY|SFF_SAFEDIRPATH|SFF_CREAT);

	if (mci->mci_statfile == NULL)
	{
		syserr("mci_lock_host: cannot create host lock file %s", fname);
		goto cleanup;
	}

	if (!lockfile(sm_io_getinfo(mci->mci_statfile, SM_IO_WHAT_FD, NULL),
		      fname, "", LOCK_EX|LOCK_NB))
	{
		if (tTd(56, 2))
			sm_dprintf("mci_lock_host: couldn't get lock on %s\n",
				fname);
		(void) sm_io_close(mci->mci_statfile, SM_TIME_DEFAULT);
		mci->mci_statfile = NULL;
		retVal = EX_TEMPFAIL;
		goto cleanup;
	}

	if (tTd(56, 12) && mci->mci_statfile != NULL)
		sm_dprintf("mci_lock_host: Sanity check -- lock is good\n");

cleanup:
	errno = save_errno;
	return retVal;
}
/*
**  MCI_UNLOCK_HOST -- unlock host
**
**	Clean up the lock on a host, close the file, let
**	someone else use it.
**
**	Parameters:
**		mci -- us.
**
**	Returns:
**		nothing.
*/

void
mci_unlock_host(mci)
	MCI *mci;
{
	int save_errno = errno;

	if (mci == NULL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_unlock_host: NULL mci\n");
		return;
	}

	if (HostStatDir == NULL || mci->mci_host == NULL)
		return;

	if (!SingleThreadDelivery && mci_lock_host_statfile(mci) == EX_TEMPFAIL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_unlock_host: stat file already locked\n");
	}
	else
	{
		if (tTd(56, 2))
			sm_dprintf("mci_unlock_host: store prior to unlock\n");
		mci_store_persistent(mci);
	}

	if (mci->mci_statfile != NULL)
	{
		(void) sm_io_close(mci->mci_statfile, SM_TIME_DEFAULT);
		mci->mci_statfile = NULL;
	}

	errno = save_errno;
}
/*
**  MCI_LOAD_PERSISTENT -- load persistent host info
**
**	Load information about host that is kept
**	in common for all running sendmails.
**
**	Parameters:
**		mci -- the host/connection to load persistent info for.
**
**	Returns:
**		true -- lock was successful
**		false -- lock failed
*/

static bool
mci_load_persistent(mci)
	MCI *mci;
{
	int save_errno = errno;
	bool locked = true;
	SM_FILE_T *fp;
	char fname[MAXPATHLEN];

	if (mci == NULL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_load_persistent: NULL mci\n");
		return true;
	}

	if (IgnoreHostStatus || HostStatDir == NULL || mci->mci_host == NULL)
		return true;

	/* Already have the persistent information in memory */
	if (SingleThreadDelivery && mci->mci_statfile != NULL)
		return true;

	if (tTd(56, 1))
		sm_dprintf("mci_load_persistent: Attempting to load persistent information for %s\n",
			   mci->mci_host);

	if (mci_generate_persistent_path(mci->mci_host, fname, sizeof(fname),
					 false) < 0)
	{
		/* Not much we can do if the file isn't there... */
		if (tTd(56, 1))
			sm_dprintf("mci_load_persistent: Couldn't generate host path\n");
		goto cleanup;
	}

	fp = safefopen(fname, O_RDONLY, FileMode,
		       SFF_NOLOCK|SFF_NOLINK|SFF_OPENASROOT|SFF_REGONLY|SFF_SAFEDIRPATH);
	if (fp == NULL)
	{
		/* I can't think of any reason this should ever happen */
		if (tTd(56, 1))
			sm_dprintf("mci_load_persistent: open(%s): %s\n",
				fname, sm_errstring(errno));
		goto cleanup;
	}

	FileName = fname;
	locked = lockfile(sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL), fname, "",
			  LOCK_SH|LOCK_NB);
	if (locked)
	{
		(void) mci_read_persistent(fp, mci);
		(void) lockfile(sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL), fname,
				"", LOCK_UN);
	}
	FileName = NULL;
	(void) sm_io_close(fp, SM_TIME_DEFAULT);

cleanup:
	errno = save_errno;
	return locked;
}
/*
**  MCI_READ_PERSISTENT -- read persistent host status file
**
**	Parameters:
**		fp -- the file pointer to read.
**		mci -- the pointer to fill in.
**
**	Returns:
**		-1 -- if the file was corrupt.
**		0 -- otherwise.
**
**	Warning:
**		This code makes the assumption that this data
**		will be read in an atomic fashion, and that the data
**		was written in an atomic fashion.  Any other functioning
**		may lead to some form of insanity.  This should be
**		perfectly safe due to underlying stdio buffering.
*/

static int
mci_read_persistent(fp, mci)
	SM_FILE_T *fp;
	register MCI *mci;
{
	int ver;
	register char *p;
	int saveLineNumber = LineNumber;
	char buf[MAXLINE];

	if (fp == NULL)
	{
		syserr("mci_read_persistent: NULL fp");
		/* NOTREACHED */
		return -1;
	}
	if (mci == NULL)
	{
		syserr("mci_read_persistent: NULL mci");
		/* NOTREACHED */
		return -1;
	}
	if (tTd(56, 93))
	{
		sm_dprintf("mci_read_persistent: fp=%lx, mci=",
			   (unsigned long) fp);
	}

	SM_FREE_CLR(mci->mci_status);
	SM_FREE_CLR(mci->mci_rstatus);

	sm_io_rewind(fp, SM_TIME_DEFAULT);
	ver = -1;
	LineNumber = 0;
	while (sm_io_fgets(fp, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		LineNumber++;
		p = strchr(buf, '\n');
		if (p != NULL)
			*p = '\0';
		switch (buf[0])
		{
		  case 'V':		/* version stamp */
			ver = atoi(&buf[1]);
			if (ver < 0 || ver > 0)
				syserr("Unknown host status version %d: %d max",
					ver, 0);
			break;

		  case 'E':		/* UNIX error number */
			mci->mci_errno = atoi(&buf[1]);
			break;

		  case 'H':		/* DNS error number */
			mci->mci_herrno = atoi(&buf[1]);
			break;

		  case 'S':		/* UNIX exit status */
			mci->mci_exitstat = atoi(&buf[1]);
			break;

		  case 'D':		/* DSN status */
			mci->mci_status = newstr(&buf[1]);
			break;

		  case 'R':		/* SMTP status */
			mci->mci_rstatus = newstr(&buf[1]);
			break;

		  case 'U':		/* last usage time */
			mci->mci_lastuse = atol(&buf[1]);
			break;

		  case '.':		/* end of file */
			if (tTd(56, 93))
				mci_dump(sm_debug_file(), mci, false);
			return 0;

		  default:
			sm_syslog(LOG_CRIT, NOQID,
				  "%s: line %d: Unknown host status line \"%s\"",
				  FileName == NULL ? mci->mci_host : FileName,
				  LineNumber, buf);
			LineNumber = saveLineNumber;
			return -1;
		}
	}
	LineNumber = saveLineNumber;
	if (tTd(56, 93))
		sm_dprintf("incomplete (missing dot for EOF)\n");
	if (ver < 0)
		return -1;
	return 0;
}
/*
**  MCI_STORE_PERSISTENT -- Store persistent MCI information
**
**	Store information about host that is kept
**	in common for all running sendmails.
**
**	Parameters:
**		mci -- the host/connection to store persistent info for.
**
**	Returns:
**		none.
*/

void
mci_store_persistent(mci)
	MCI *mci;
{
	int save_errno = errno;

	if (mci == NULL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_store_persistent: NULL mci\n");
		return;
	}

	if (HostStatDir == NULL || mci->mci_host == NULL)
		return;

	if (tTd(56, 1))
		sm_dprintf("mci_store_persistent: Storing information for %s\n",
			   mci->mci_host);

	if (mci->mci_statfile == NULL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_store_persistent: no statfile\n");
		return;
	}

	sm_io_rewind(mci->mci_statfile, SM_TIME_DEFAULT);
#if !NOFTRUNCATE
	(void) ftruncate(sm_io_getinfo(mci->mci_statfile, SM_IO_WHAT_FD, NULL),
			 (off_t) 0);
#endif /* !NOFTRUNCATE */

	(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT, "V0\n");
	(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT, "E%d\n",
			     mci->mci_errno);
	(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT, "H%d\n",
			     mci->mci_herrno);
	(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT, "S%d\n",
			     mci->mci_exitstat);
	if (mci->mci_status != NULL)
		(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT,
				     "D%.80s\n",
				     denlstring(mci->mci_status, true, false));
	if (mci->mci_rstatus != NULL)
		(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT,
				     "R%.80s\n",
				     denlstring(mci->mci_rstatus, true, false));
	(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT, "U%ld\n",
			     (long)(mci->mci_lastuse));
	(void) sm_io_fprintf(mci->mci_statfile, SM_TIME_DEFAULT, ".\n");

	(void) sm_io_flush(mci->mci_statfile, SM_TIME_DEFAULT);

	errno = save_errno;
	return;
}
/*
**  MCI_TRAVERSE_PERSISTENT -- walk persistent status tree
**
**	Recursively find all the mci host files in `pathname'.  Default to
**		main host status directory if no path is provided.
**	Call (*action)(pathname, host) for each file found.
**
**	Note: all information is collected in a list before it is processed.
**	This may not be the best way to do it, but it seems safest, since
**	the file system would be touched while we are attempting to traverse
**	the directory tree otherwise (during purges).
**
**	Parameters:
**		action -- function to call on each node.  If returns < 0,
**			return immediately.
**		pathname -- root of tree.  If null, use main host status
**			directory.
**
**	Returns:
**		< 0 -- if any action routine returns a negative value, that
**			value is returned.
**		0 -- if we successfully went to completion.
**		> 0 -- return status from action()
*/

int
mci_traverse_persistent(action, pathname)
	int (*action)__P((char *, char *));
	char *pathname;
{
	struct stat statbuf;
	DIR *d;
	int ret;

	if (pathname == NULL)
		pathname = HostStatDir;
	if (pathname == NULL)
		return -1;

	if (tTd(56, 1))
		sm_dprintf("mci_traverse: pathname is %s\n", pathname);

	ret = stat(pathname, &statbuf);
	if (ret < 0)
	{
		if (tTd(56, 2))
			sm_dprintf("mci_traverse: Failed to stat %s: %s\n",
				pathname, sm_errstring(errno));
		return ret;
	}
	if (S_ISDIR(statbuf.st_mode))
	{
		bool leftone, removedone;
		size_t len;
		char *newptr;
		struct dirent *e;
		char newpath[MAXPATHLEN];
#if MAXPATHLEN <= MAXNAMLEN - 3
 ERROR "MAXPATHLEN <= MAXNAMLEN - 3"
#endif /* MAXPATHLEN  <= MAXNAMLEN - 3 */

		if ((d = opendir(pathname)) == NULL)
		{
			if (tTd(56, 2))
				sm_dprintf("mci_traverse: opendir %s: %s\n",
					pathname, sm_errstring(errno));
			return -1;
		}

		/*
		**  Reserve space for trailing '/', at least one
		**  character, and '\0'
		*/

		len = sizeof(newpath) - 3;
		if (sm_strlcpy(newpath, pathname, len) >= len)
		{
			int save_errno = errno;

			if (tTd(56, 2))
				sm_dprintf("mci_traverse: path \"%s\" too long",
					pathname);
			(void) closedir(d);
			errno = save_errno;
			return -1;
		}
		newptr = newpath + strlen(newpath);
		*newptr++ = '/';
		len = sizeof(newpath) - (newptr - newpath);

		/*
		**  repeat until no file has been removed
		**  this may become ugly when several files "expire"
		**  during these loops, but it's better than doing
		**  a rewinddir() inside the inner loop
		*/

		do
		{
			leftone = removedone = false;
			while ((e = readdir(d)) != NULL)
			{
				if (e->d_name[0] == '.')
					continue;

				if (sm_strlcpy(newptr, e->d_name, len) >= len)
				{
					/* Skip truncated copies */
					if (tTd(56, 4))
					{
						*newptr = '\0';
						sm_dprintf("mci_traverse: path \"%s%s\" too long",
							   newpath, e->d_name);
					}
					continue;
				}

				if (StopRequest)
					stop_sendmail();
				ret = mci_traverse_persistent(action, newpath);
				if (ret < 0)
					break;
				if (ret == 1)
					leftone = true;
				if (!removedone && ret == 0 &&
				    action == mci_purge_persistent)
					removedone = true;
			}
			if (ret < 0)
				break;

			/*
			**  The following appears to be
			**  necessary during purges, since
			**  we modify the directory structure
			*/

			if (removedone)
				rewinddir(d);
			if (tTd(56, 40))
				sm_dprintf("mci_traverse: path %s: ret %d removed %d left %d\n",
					pathname, ret, removedone, leftone);
		} while (removedone);

		/* purge (or whatever) the directory proper */
		if (!leftone)
		{
			*--newptr = '\0';
			ret = (*action)(newpath, NULL);
		}
		(void) closedir(d);
	}
	else if (S_ISREG(statbuf.st_mode))
	{
		char *end = pathname + strlen(pathname) - 1;
		char *start;
		char *scan;
		char host[MAXHOSTNAMELEN];
		char *hostptr = host;

		/*
		**  Reconstruct the host name from the path to the
		**  persistent information.
		*/

		do
		{
			if (hostptr != host)
				*(hostptr++) = '.';
			start = end;
			while (start > pathname && *(start - 1) != '/')
				start--;

			if (*end == '.')
				end--;

			for (scan = start; scan <= end; scan++)
				*(hostptr++) = *scan;

			end = start - 2;
		} while (end > pathname && *end == '.');

		*hostptr = '\0';

		/*
		**  Do something with the file containing the persistent
		**  information.
		*/

		ret = (*action)(pathname, host);
	}

	return ret;
}
/*
**  MCI_PRINT_PERSISTENT -- print persistent info
**
**	Dump the persistent information in the file 'pathname'
**
**	Parameters:
**		pathname -- the pathname to the status file.
**		hostname -- the corresponding host name.
**
**	Returns:
**		0
*/

int
mci_print_persistent(pathname, hostname)
	char *pathname;
	char *hostname;
{
	static bool initflag = false;
	SM_FILE_T *fp;
	int width = Verbose ? 78 : 25;
	bool locked;
	MCI mcib;

	/* skip directories */
	if (hostname == NULL)
		return 0;

	if (StopRequest)
		stop_sendmail();

	if (!initflag)
	{
		initflag = true;
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     " -------------- Hostname --------------- How long ago ---------Results---------\n");
	}

	fp = safefopen(pathname, O_RDONLY, FileMode,
		       SFF_NOLOCK|SFF_NOLINK|SFF_OPENASROOT|SFF_REGONLY|SFF_SAFEDIRPATH);

	if (fp == NULL)
	{
		if (tTd(56, 1))
			sm_dprintf("mci_print_persistent: cannot open %s: %s\n",
				pathname, sm_errstring(errno));
		return 0;
	}

	FileName = pathname;
	memset(&mcib, '\0', sizeof(mcib));
	if (mci_read_persistent(fp, &mcib) < 0)
	{
		syserr("%s: could not read status file", pathname);
		(void) sm_io_close(fp, SM_TIME_DEFAULT);
		FileName = NULL;
		return 0;
	}

	locked = !lockfile(sm_io_getinfo(fp, SM_IO_WHAT_FD, NULL), pathname,
			   "", LOCK_SH|LOCK_NB);
	(void) sm_io_close(fp, SM_TIME_DEFAULT);
	FileName = NULL;

	(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%c%-39s %12s ",
			     locked ? '*' : ' ', hostname,
			     pintvl(curtime() - mcib.mci_lastuse, true));
	if (mcib.mci_rstatus != NULL)
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%.*s\n", width,
				     mcib.mci_rstatus);
	else if (mcib.mci_exitstat == EX_TEMPFAIL && mcib.mci_errno != 0)
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Deferred: %.*s\n", width - 10,
				     sm_errstring(mcib.mci_errno));
	else if (mcib.mci_exitstat != 0)
	{
		char *exmsg = sm_sysexmsg(mcib.mci_exitstat);

		if (exmsg == NULL)
		{
			char buf[80];

			(void) sm_snprintf(buf, sizeof(buf),
				"Unknown mailer error %d",
				mcib.mci_exitstat);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%.*s\n",
					     width, buf);
		}
		else
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%.*s\n",
					     width, &exmsg[5]);
	}
	else if (mcib.mci_errno == 0)
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "OK\n");
	else
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "OK: %.*s\n",
				     width - 4, sm_errstring(mcib.mci_errno));

	return 0;
}
/*
**  MCI_PURGE_PERSISTENT -- Remove a persistence status file.
**
**	Parameters:
**		pathname -- path to the status file.
**		hostname -- name of host corresponding to that file.
**			NULL if this is a directory (domain).
**
**	Returns:
**		0 -- ok
**		1 -- file not deleted (too young, incorrect format)
**		< 0 -- some error occurred
*/

int
mci_purge_persistent(pathname, hostname)
	char *pathname;
	char *hostname;
{
	struct stat statbuf;
	char *end = pathname + strlen(pathname) - 1;
	int ret;

	if (tTd(56, 1))
		sm_dprintf("mci_purge_persistent: purging %s\n", pathname);

	ret = stat(pathname, &statbuf);
	if (ret < 0)
	{
		if (tTd(56, 2))
			sm_dprintf("mci_purge_persistent: Failed to stat %s: %s\n",
				pathname, sm_errstring(errno));
		return ret;
	}
	if (curtime() - statbuf.st_mtime <= MciInfoTimeout)
		return 1;
	if (hostname != NULL)
	{
		/* remove the file */
		ret = unlink(pathname);
		if (ret < 0)
		{
			if (LogLevel > 8)
				sm_syslog(LOG_ERR, NOQID,
					  "mci_purge_persistent: failed to unlink %s: %s",
					  pathname, sm_errstring(errno));
			if (tTd(56, 2))
				sm_dprintf("mci_purge_persistent: failed to unlink %s: %s\n",
					pathname, sm_errstring(errno));
			return ret;
		}
	}
	else
	{
		/* remove the directory */
		if (*end != '.')
			return 1;

		if (tTd(56, 1))
			sm_dprintf("mci_purge_persistent: dpurge %s\n", pathname);

		ret = rmdir(pathname);
		if (ret < 0)
		{
			if (tTd(56, 2))
				sm_dprintf("mci_purge_persistent: rmdir %s: %s\n",
					pathname, sm_errstring(errno));
			return ret;
		}
	}

	return 0;
}
/*
**  MCI_GENERATE_PERSISTENT_PATH -- generate path from hostname
**
**	Given `host', convert from a.b.c to $HostStatDir/c./b./a,
**	putting the result into `path'.  if `createflag' is set, intervening
**	directories will be created as needed.
**
**	Parameters:
**		host -- host name to convert from.
**		path -- place to store result.
**		pathlen -- length of path buffer.
**		createflag -- if set, create intervening directories as
**			needed.
**
**	Returns:
**		0 -- success
**		-1 -- failure
*/

static int
mci_generate_persistent_path(host, path, pathlen, createflag)
	const char *host;
	char *path;
	int pathlen;
	bool createflag;
{
	char *elem, *p, *x, ch;
	int ret = 0;
	int len;
	char t_host[MAXHOSTNAMELEN];
#if NETINET6
	struct in6_addr in6_addr;
#endif /* NETINET6 */

	/*
	**  Rationality check the arguments.
	*/

	if (host == NULL)
	{
		syserr("mci_generate_persistent_path: null host");
		return -1;
	}
	if (path == NULL)
	{
		syserr("mci_generate_persistent_path: null path");
		return -1;
	}

	if (tTd(56, 80))
		sm_dprintf("mci_generate_persistent_path(%s): ", host);

	if (*host == '\0' || *host == '.')
		return -1;

	/* make certain this is not a bracketed host number */
	if (strlen(host) > sizeof(t_host) - 1)
		return -1;
	if (host[0] == '[')
		(void) sm_strlcpy(t_host, host + 1, sizeof(t_host));
	else
		(void) sm_strlcpy(t_host, host, sizeof(t_host));

	/*
	**  Delete any trailing dots from the hostname.
	**  Leave 'elem' pointing at the \0.
	*/

	elem = t_host + strlen(t_host);
	while (elem > t_host &&
	       (elem[-1] == '.' || (host[0] == '[' && elem[-1] == ']')))
		*--elem = '\0';

	/* check for bogus bracketed address */
	if (host[0] == '[')
	{
		bool good = false;
# if NETINET6
		if (anynet_pton(AF_INET6, t_host, &in6_addr) == 1)
			good = true;
# endif /* NETINET6 */
# if NETINET
		if (inet_addr(t_host) != INADDR_NONE)
			good = true;
# endif /* NETINET */
		if (!good)
			return -1;
	}

	/* check for what will be the final length of the path */
	len = strlen(HostStatDir) + 2;
	for (p = (char *) t_host; *p != '\0'; p++)
	{
		if (*p == '.')
			len++;
		len++;
		if (p[0] == '.' && p[1] == '.')
			return -1;
	}
	if (len > pathlen || len < 1)
		return -1;
	(void) sm_strlcpy(path, HostStatDir, pathlen);
	p = path + strlen(path);
	while (elem > t_host)
	{
		if (!path_is_dir(path, createflag))
		{
			ret = -1;
			break;
		}
		elem--;
		while (elem >= t_host && *elem != '.')
			elem--;
		*p++ = '/';
		x = elem + 1;
		while ((ch = *x++) != '\0' && ch != '.')
		{
			if (isascii(ch) && isupper(ch))
				ch = tolower(ch);
			if (ch == '/')
				ch = ':';	/* / -> : */
			*p++ = ch;
		}
		if (elem >= t_host)
			*p++ = '.';
		*p = '\0';
	}
	if (tTd(56, 80))
	{
		if (ret < 0)
			sm_dprintf("FAILURE %d\n", ret);
		else
			sm_dprintf("SUCCESS %s\n", path);
	}
	return ret;
}
