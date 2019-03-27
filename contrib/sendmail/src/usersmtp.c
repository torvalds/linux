/*
 * Copyright (c) 1998-2006, 2008-2010, 2014 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: usersmtp.c,v 8.488 2013-11-22 20:51:57 ca Exp $")

#include <sysexits.h>


static void	esmtp_check __P((char *, bool, MAILER *, MCI *, ENVELOPE *));
static void	helo_options __P((char *, bool, MAILER *, MCI *, ENVELOPE *));
static int	smtprcptstat __P((ADDRESS *, MAILER *, MCI *, ENVELOPE *));

#if SASL
extern void	*sm_sasl_malloc __P((unsigned long));
extern void	sm_sasl_free __P((void *));
#endif /* SASL */

/*
**  USERSMTP -- run SMTP protocol from the user end.
**
**	This protocol is described in RFC821.
*/

#define SMTPCLOSING	421			/* "Service Shutting Down" */

#define ENHSCN(e, d)	((e) == NULL ? (d) : (e))

#define ENHSCN_RPOOL(e, d, rpool) \
	((e) == NULL ? (d) : sm_rpool_strdup_x(rpool, e))

static char	SmtpMsgBuffer[MAXLINE];		/* buffer for commands */
static char	SmtpReplyBuffer[MAXLINE];	/* buffer for replies */
static bool	SmtpNeedIntro;		/* need "while talking" in transcript */
/*
**  SMTPINIT -- initialize SMTP.
**
**	Opens the connection and sends the initial protocol.
**
**	Parameters:
**		m -- mailer to create connection to.
**		mci -- the mailer connection info.
**		e -- the envelope.
**		onlyhelo -- send only helo command?
**
**	Returns:
**		none.
**
**	Side Effects:
**		creates connection and sends initial protocol.
*/

void
smtpinit(m, mci, e, onlyhelo)
	MAILER *m;
	register MCI *mci;
	ENVELOPE *e;
	bool onlyhelo;
{
	register int r;
	int state;
	register char *p;
	register char *hn;
	char *enhsc;

	enhsc = NULL;
	if (tTd(18, 1))
	{
		sm_dprintf("smtpinit ");
		mci_dump(sm_debug_file(), mci, false);
	}

	/*
	**  Open the connection to the mailer.
	*/

	SmtpError[0] = '\0';
	SmtpMsgBuffer[0] = '\0';
	CurHostName = mci->mci_host;		/* XXX UGLY XXX */
	if (CurHostName == NULL)
		CurHostName = MyHostName;
	SmtpNeedIntro = true;
	state = mci->mci_state;
#if _FFR_ERRCODE
	e->e_rcode = 0;
	e->e_renhsc[0] = '\0';
	e->e_text = NULL;
#endif /* _FFR_ERRCODE */
	switch (state)
	{
	  case MCIS_MAIL:
	  case MCIS_RCPT:
	  case MCIS_DATA:
		/* need to clear old information */
		smtprset(m, mci, e);
		/* FALLTHROUGH */

	  case MCIS_OPEN:
		if (!onlyhelo)
			return;
		break;

	  case MCIS_ERROR:
	  case MCIS_QUITING:
	  case MCIS_SSD:
		/* shouldn't happen */
		smtpquit(m, mci, e);
		/* FALLTHROUGH */

	  case MCIS_CLOSED:
		syserr("451 4.4.0 smtpinit: state CLOSED (was %d)", state);
		return;

	  case MCIS_OPENING:
		break;
	}
	if (onlyhelo)
		goto helo;

	mci->mci_state = MCIS_OPENING;
	clrsessenvelope(e);

	/*
	**  Get the greeting message.
	**	This should appear spontaneously.  Give it five minutes to
	**	happen.
	*/

	SmtpPhase = mci->mci_phase = "client greeting";
	sm_setproctitle(true, e, "%s %s: %s",
			qid_printname(e), CurHostName, mci->mci_phase);
	r = reply(m, mci, e, TimeOuts.to_initial, esmtp_check, NULL, XS_GREET);
	if (r < 0)
		goto tempfail1;
	if (REPLYTYPE(r) == 4)
		goto tempfail2;
	if (REPLYTYPE(r) != 2)
		goto unavailable;

	/*
	**  Send the HELO command.
	**	My mother taught me to always introduce myself.
	*/

helo:
	if (bitnset(M_ESMTP, m->m_flags) || bitnset(M_LMTP, m->m_flags))
		mci->mci_flags |= MCIF_ESMTP;
	hn = mci->mci_heloname ? mci->mci_heloname : MyHostName;

tryhelo:
#if _FFR_IGNORE_EXT_ON_HELO
	mci->mci_flags &= ~MCIF_HELO;
#endif /* _FFR_IGNORE_EXT_ON_HELO */
	if (bitnset(M_LMTP, m->m_flags))
	{
		smtpmessage("LHLO %s", m, mci, hn);
		SmtpPhase = mci->mci_phase = "client LHLO";
	}
	else if (bitset(MCIF_ESMTP, mci->mci_flags) &&
		 !bitnset(M_FSMTP, m->m_flags))
	{
		smtpmessage("EHLO %s", m, mci, hn);
		SmtpPhase = mci->mci_phase = "client EHLO";
	}
	else
	{
		smtpmessage("HELO %s", m, mci, hn);
		SmtpPhase = mci->mci_phase = "client HELO";
#if _FFR_IGNORE_EXT_ON_HELO
		mci->mci_flags |= MCIF_HELO;
#endif /* _FFR_IGNORE_EXT_ON_HELO */
	}
	sm_setproctitle(true, e, "%s %s: %s", qid_printname(e),
			CurHostName, mci->mci_phase);
	r = reply(m, mci, e,
		  bitnset(M_LMTP, m->m_flags) ? TimeOuts.to_lhlo
					      : TimeOuts.to_helo,
		  helo_options, NULL, XS_EHLO);
	if (r < 0)
		goto tempfail1;
	else if (REPLYTYPE(r) == 5)
	{
		if (bitset(MCIF_ESMTP, mci->mci_flags) &&
		    !bitnset(M_LMTP, m->m_flags))
		{
			/* try old SMTP instead */
			mci->mci_flags &= ~MCIF_ESMTP;
			goto tryhelo;
		}
		goto unavailable;
	}
	else if (REPLYTYPE(r) != 2)
		goto tempfail2;

	/*
	**  Check to see if we actually ended up talking to ourself.
	**  This means we didn't know about an alias or MX, or we managed
	**  to connect to an echo server.
	*/

	p = strchr(&SmtpReplyBuffer[4], ' ');
	if (p != NULL)
		*p = '\0';
	if (!bitnset(M_NOLOOPCHECK, m->m_flags) &&
	    !bitnset(M_LMTP, m->m_flags) &&
	    sm_strcasecmp(&SmtpReplyBuffer[4], MyHostName) == 0)
	{
		syserr("553 5.3.5 %s config error: mail loops back to me (MX problem?)",
			CurHostName);
		mci_setstat(mci, EX_CONFIG, "5.3.5",
			    "553 5.3.5 system config error");
		mci->mci_errno = 0;
		smtpquit(m, mci, e);
		return;
	}

	/*
	**  If this is expected to be another sendmail, send some internal
	**  commands.
	**  If we're running as MSP, "propagate" -v flag if possible.
	*/

	if ((UseMSP && Verbose && bitset(MCIF_VERB, mci->mci_flags))
	    || bitnset(M_INTERNAL, m->m_flags))
	{
		/* tell it to be verbose */
		smtpmessage("VERB", m, mci);
		r = reply(m, mci, e, TimeOuts.to_miscshort, NULL, &enhsc,
			XS_DEFAULT);
		if (r < 0)
			goto tempfail1;
	}

	if (mci->mci_state != MCIS_CLOSED)
	{
		mci->mci_state = MCIS_OPEN;
		return;
	}

	/* got a 421 error code during startup */

  tempfail1:
	mci_setstat(mci, EX_TEMPFAIL, ENHSCN(enhsc, "4.4.2"), NULL);
	if (mci->mci_state != MCIS_CLOSED)
		smtpquit(m, mci, e);
	return;

  tempfail2:
	/* XXX should use code from other end iff ENHANCEDSTATUSCODES */
	mci_setstat(mci, EX_TEMPFAIL, ENHSCN(enhsc, "4.5.0"),
		    SmtpReplyBuffer);
	if (mci->mci_state != MCIS_CLOSED)
		smtpquit(m, mci, e);
	return;

  unavailable:
	mci_setstat(mci, EX_UNAVAILABLE, "5.5.0", SmtpReplyBuffer);
	smtpquit(m, mci, e);
	return;
}
/*
**  ESMTP_CHECK -- check to see if this implementation likes ESMTP protocol
**
**	Parameters:
**		line -- the response line.
**		firstline -- set if this is the first line of the reply.
**		m -- the mailer.
**		mci -- the mailer connection info.
**		e -- the envelope.
**
**	Returns:
**		none.
*/

static void
esmtp_check(line, firstline, m, mci, e)
	char *line;
	bool firstline;
	MAILER *m;
	register MCI *mci;
	ENVELOPE *e;
{
	if (strstr(line, "ESMTP") != NULL)
		mci->mci_flags |= MCIF_ESMTP;

	/*
	**  Dirty hack below. Quoting the author:
	**  This was a response to people who wanted SMTP transmission to be
	**  just-send-8 by default.  Essentially, you could put this tag into
	**  your greeting message to behave as though the F=8 flag was set on
	**  the mailer.
	*/

	if (strstr(line, "8BIT-OK") != NULL)
		mci->mci_flags |= MCIF_8BITOK;
}

#if SASL
/* specify prototype so compiler can check calls */
static char *str_union __P((char *, char *, SM_RPOOL_T *));

/*
**  STR_UNION -- create the union of two lists
**
**	Parameters:
**		s1, s2 -- lists of items (separated by single blanks).
**		rpool -- resource pool from which result is allocated.
**
**	Returns:
**		the union of both lists.
*/

static char *
str_union(s1, s2, rpool)
	char *s1, *s2;
	SM_RPOOL_T *rpool;
{
	char *hr, *h1, *h, *res;
	int l1, l2, rl;

	if (s1 == NULL || *s1 == '\0')
		return s2;
	if (s2 == NULL || *s2 == '\0')
		return s1;
	l1 = strlen(s1);
	l2 = strlen(s2);
	rl = l1 + l2;
	if (rl <= 0)
	{
		sm_syslog(LOG_WARNING, NOQID,
			  "str_union: stringlen1=%d, stringlen2=%d, sum=%d, status=overflow",
			  l1, l2, rl);
		res = NULL;
	}
	else
		res = (char *) sm_rpool_malloc(rpool, rl + 2);
	if (res == NULL)
	{
		if (l1 > l2)
			return s1;
		return s2;
	}
	(void) sm_strlcpy(res, s1, rl);
	hr = res + l1;
	h1 = s2;
	h = s2;

	/* walk through s2 */
	while (h != NULL && *h1 != '\0')
	{
		/* is there something after the current word? */
		if ((h = strchr(h1, ' ')) != NULL)
			*h = '\0';
		l1 = strlen(h1);

		/* does the current word appear in s1 ? */
		if (iteminlist(h1, s1, " ") == NULL)
		{
			/* add space as delimiter */
			*hr++ = ' ';

			/* copy the item */
			memcpy(hr, h1, l1);

			/* advance pointer in result list */
			hr += l1;
			*hr = '\0';
		}
		if (h != NULL)
		{
			/* there are more items */
			*h = ' ';
			h1 = h + 1;
		}
	}
	return res;
}
#endif /* SASL */

/*
**  HELO_OPTIONS -- process the options on a HELO line.
**
**	Parameters:
**		line -- the response line.
**		firstline -- set if this is the first line of the reply.
**		m -- the mailer.
**		mci -- the mailer connection info.
**		e -- the envelope (unused).
**
**	Returns:
**		none.
*/

static void
helo_options(line, firstline, m, mci, e)
	char *line;
	bool firstline;
	MAILER *m;
	register MCI *mci;
	ENVELOPE *e;
{
	register char *p;
#if _FFR_IGNORE_EXT_ON_HELO
	static bool logged = false;
#endif /* _FFR_IGNORE_EXT_ON_HELO */

	if (firstline)
	{
		mci_clr_extensions(mci);
#if _FFR_IGNORE_EXT_ON_HELO
		logged = false;
#endif /* _FFR_IGNORE_EXT_ON_HELO */
		return;
	}
#if _FFR_IGNORE_EXT_ON_HELO
	else if (bitset(MCIF_HELO, mci->mci_flags))
	{
		if (LogLevel > 8 && !logged)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "server=%s [%s] returned extensions despite HELO command",
				  macvalue(macid("{server_name}"), e),
				  macvalue(macid("{server_addr}"), e));
			logged = true;
		}
		return;
	}
#endif /* _FFR_IGNORE_EXT_ON_HELO */

	if (strlen(line) < 5)
		return;
	line += 4;
	p = strpbrk(line, " =");
	if (p != NULL)
		*p++ = '\0';
	if (sm_strcasecmp(line, "size") == 0)
	{
		mci->mci_flags |= MCIF_SIZE;
		if (p != NULL)
			mci->mci_maxsize = atol(p);
	}
	else if (sm_strcasecmp(line, "8bitmime") == 0)
	{
		mci->mci_flags |= MCIF_8BITMIME;
		mci->mci_flags &= ~MCIF_7BIT;
	}
	else if (sm_strcasecmp(line, "expn") == 0)
		mci->mci_flags |= MCIF_EXPN;
	else if (sm_strcasecmp(line, "dsn") == 0)
		mci->mci_flags |= MCIF_DSN;
	else if (sm_strcasecmp(line, "enhancedstatuscodes") == 0)
		mci->mci_flags |= MCIF_ENHSTAT;
	else if (sm_strcasecmp(line, "pipelining") == 0)
		mci->mci_flags |= MCIF_PIPELINED;
	else if (sm_strcasecmp(line, "verb") == 0)
		mci->mci_flags |= MCIF_VERB;
#if STARTTLS
	else if (sm_strcasecmp(line, "starttls") == 0)
		mci->mci_flags |= MCIF_TLS;
#endif /* STARTTLS */
	else if (sm_strcasecmp(line, "deliverby") == 0)
	{
		mci->mci_flags |= MCIF_DLVR_BY;
		if (p != NULL)
			mci->mci_min_by = atol(p);
	}
#if SASL
	else if (sm_strcasecmp(line, "auth") == 0)
	{
		if (p != NULL && *p != '\0' &&
		    !bitset(MCIF_AUTH2, mci->mci_flags))
		{
			if (mci->mci_saslcap != NULL)
			{
				/*
				**  Create the union with previous auth
				**  offerings because we recognize "auth "
				**  and "auth=" (old format).
				*/

				mci->mci_saslcap = str_union(mci->mci_saslcap,
							     p, mci->mci_rpool);
				mci->mci_flags |= MCIF_AUTH2;
			}
			else
			{
				int l;

				l = strlen(p) + 1;
				mci->mci_saslcap = (char *)
					sm_rpool_malloc(mci->mci_rpool, l);
				if (mci->mci_saslcap != NULL)
				{
					(void) sm_strlcpy(mci->mci_saslcap, p,
							  l);
					mci->mci_flags |= MCIF_AUTH;
				}
			}
		}
		if (tTd(95, 5))
			sm_syslog(LOG_DEBUG, NOQID, "AUTH flags=%lx, mechs=%s",
				mci->mci_flags, mci->mci_saslcap);
	}
#endif /* SASL */
}
#if SASL

static int getsimple	__P((void *, int, const char **, unsigned *));
static int getsecret	__P((sasl_conn_t *, void *, int, sasl_secret_t **));
static int saslgetrealm	__P((void *, int, const char **, const char **));
static int readauth	__P((char *, bool, SASL_AI_T *m, SM_RPOOL_T *));
static int getauth	__P((MCI *, ENVELOPE *, SASL_AI_T *));
static char *removemech	__P((char *, char *, SM_RPOOL_T *));
static int attemptauth	__P((MAILER *, MCI *, ENVELOPE *, SASL_AI_T *));

static sasl_callback_t callbacks[] =
{
	{	SASL_CB_GETREALM,	(sasl_callback_ft)&saslgetrealm,	NULL	},
#define CB_GETREALM_IDX	0
	{	SASL_CB_PASS,		(sasl_callback_ft)&getsecret,	NULL	},
#define CB_PASS_IDX	1
	{	SASL_CB_USER,		(sasl_callback_ft)&getsimple,	NULL	},
#define CB_USER_IDX	2
	{	SASL_CB_AUTHNAME,	(sasl_callback_ft)&getsimple,	NULL	},
#define CB_AUTHNAME_IDX	3
	{	SASL_CB_VERIFYFILE,	(sasl_callback_ft)&safesaslfile,	NULL	},
#define CB_SAFESASL_IDX	4
	{	SASL_CB_LIST_END,	NULL,		NULL	}
};

/*
**  INIT_SASL_CLIENT -- initialize client side of Cyrus-SASL
**
**	Parameters:
**		none.
**
**	Returns:
**		SASL_OK -- if successful.
**		SASL error code -- otherwise.
**
**	Side Effects:
**		checks/sets sasl_clt_init.
**
**	Note:
**	Callbacks are ignored if sasl_client_init() has
**	been called before (by a library such as libnss_ldap)
*/

static bool sasl_clt_init = false;

static int
init_sasl_client()
{
	int result;

	if (sasl_clt_init)
		return SASL_OK;
	result = sasl_client_init(callbacks);

	/* should we retry later again or just remember that it failed? */
	if (result == SASL_OK)
		sasl_clt_init = true;
	return result;
}
/*
**  STOP_SASL_CLIENT -- shutdown client side of Cyrus-SASL
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		checks/sets sasl_clt_init.
*/

void
stop_sasl_client()
{
	if (!sasl_clt_init)
		return;
	sasl_clt_init = false;
	sasl_done();
}
/*
**  GETSASLDATA -- process the challenges from the SASL protocol
**
**	This gets the relevant sasl response data out of the reply
**	from the server.
**
**	Parameters:
**		line -- the response line.
**		firstline -- set if this is the first line of the reply.
**		m -- the mailer.
**		mci -- the mailer connection info.
**		e -- the envelope (unused).
**
**	Returns:
**		none.
*/

static void getsasldata __P((char *, bool, MAILER *, MCI *, ENVELOPE *));

static void
getsasldata(line, firstline, m, mci, e)
	char *line;
	bool firstline;
	MAILER *m;
	register MCI *mci;
	ENVELOPE *e;
{
	int len;
	int result;
# if SASL < 20000
	char *out;
# endif /* SASL < 20000 */

	/* if not a continue we don't care about it */
	len = strlen(line);
	if ((len <= 4) ||
	    (line[0] != '3') ||
	     !isascii(line[1]) || !isdigit(line[1]) ||
	     !isascii(line[2]) || !isdigit(line[2]))
	{
		SM_FREE_CLR(mci->mci_sasl_string);
		return;
	}

	/* forget about "334 " */
	line += 4;
	len -= 4;
# if SASL >= 20000
	/* XXX put this into a macro/function? It's duplicated below */
	if (mci->mci_sasl_string != NULL)
	{
		if (mci->mci_sasl_string_len <= len)
		{
			sm_free(mci->mci_sasl_string); /* XXX */
			mci->mci_sasl_string = xalloc(len + 1);
		}
	}
	else
		mci->mci_sasl_string = xalloc(len + 1);

	result = sasl_decode64(line, len, mci->mci_sasl_string, len + 1,
			       (unsigned int *) &mci->mci_sasl_string_len);
	if (result != SASL_OK)
	{
		mci->mci_sasl_string_len = 0;
		*mci->mci_sasl_string = '\0';
	}
# else /* SASL >= 20000 */
	out = (char *) sm_rpool_malloc_x(mci->mci_rpool, len + 1);
	result = sasl_decode64(line, len, out, (unsigned int *) &len);
	if (result != SASL_OK)
	{
		len = 0;
		*out = '\0';
	}

	/*
	**  mci_sasl_string is "shared" with Cyrus-SASL library; hence
	**	it can't be in an rpool unless we use the same memory
	**	management mechanism (with same rpool!) for Cyrus SASL.
	*/

	if (mci->mci_sasl_string != NULL)
	{
		if (mci->mci_sasl_string_len <= len)
		{
			sm_free(mci->mci_sasl_string); /* XXX */
			mci->mci_sasl_string = xalloc(len + 1);
		}
	}
	else
		mci->mci_sasl_string = xalloc(len + 1);

	memcpy(mci->mci_sasl_string, out, len);
	mci->mci_sasl_string[len] = '\0';
	mci->mci_sasl_string_len = len;
# endif /* SASL >= 20000 */
	return;
}
/*
**  READAUTH -- read auth values from a file
**
**	Parameters:
**		filename -- name of file to read.
**		safe -- if set, this is a safe read.
**		sai -- where to store auth_info.
**		rpool -- resource pool for sai.
**
**	Returns:
**		EX_OK -- data succesfully read.
**		EX_UNAVAILABLE -- no valid filename.
**		EX_TEMPFAIL -- temporary failure.
*/

static char *sasl_info_name[] =
{
	"user id",
	"authentication id",
	"password",
	"realm",
	"mechlist"
};
static int
readauth(filename, safe, sai, rpool)
	char *filename;
	bool safe;
	SASL_AI_T *sai;
	SM_RPOOL_T *rpool;
{
	SM_FILE_T *f;
	long sff;
	pid_t pid;
	int lc;
	char *s;
	char buf[MAXLINE];

	if (filename == NULL || filename[0] == '\0')
		return EX_UNAVAILABLE;

#if !_FFR_ALLOW_SASLINFO
	/*
	**  make sure we don't use a program that is not
	**  accesible to the user who specified a different authinfo file.
	**  However, currently we don't pass this info (authinfo file
	**  specified by user) around, so we just turn off program access.
	*/

	if (filename[0] == '|')
	{
		auto int fd;
		int i;
		char *p;
		char *argv[MAXPV + 1];

		i = 0;
		for (p = strtok(&filename[1], " \t"); p != NULL;
		     p = strtok(NULL, " \t"))
		{
			if (i >= MAXPV)
				break;
			argv[i++] = p;
		}
		argv[i] = NULL;
		pid = prog_open(argv, &fd, CurEnv);
		if (pid < 0)
			f = NULL;
		else
			f = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
				       (void *) &fd, SM_IO_RDONLY, NULL);
	}
	else
#endif /* !_FFR_ALLOW_SASLINFO */
	{
		pid = -1;
		sff = SFF_REGONLY|SFF_SAFEDIRPATH|SFF_NOWLINK
		      |SFF_NOGWFILES|SFF_NOWWFILES|SFF_NOWRFILES;
		if (!bitnset(DBS_GROUPREADABLEAUTHINFOFILE, DontBlameSendmail))
			sff |= SFF_NOGRFILES;
		if (DontLockReadFiles)
			sff |= SFF_NOLOCK;

#if _FFR_ALLOW_SASLINFO
		/*
		**  XXX: make sure we don't read or open files that are not
		**  accesible to the user who specified a different authinfo
		**  file.
		*/

		sff |= SFF_MUSTOWN;
#else /* _FFR_ALLOW_SASLINFO */
		if (safe)
			sff |= SFF_OPENASROOT;
#endif /* _FFR_ALLOW_SASLINFO */

		f = safefopen(filename, O_RDONLY, 0, sff);
	}
	if (f == NULL)
	{
		if (LogLevel > 5)
			sm_syslog(LOG_ERR, NOQID,
				  "AUTH=client, error: can't open %s: %s",
				  filename, sm_errstring(errno));
		return EX_TEMPFAIL;
	}

	lc = 0;
	while (lc <= SASL_MECHLIST &&
		sm_io_fgets(f, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		if (buf[0] != '#')
		{
			(*sai)[lc] = sm_rpool_strdup_x(rpool, buf);
			if ((s = strchr((*sai)[lc], '\n')) != NULL)
				*s = '\0';
			lc++;
		}
	}

	(void) sm_io_close(f, SM_TIME_DEFAULT);
	if (pid > 0)
		(void) waitfor(pid);
	if (lc < SASL_PASSWORD)
	{
		if (LogLevel > 8)
			sm_syslog(LOG_ERR, NOQID,
				  "AUTH=client, error: can't read %s from %s",
				  sasl_info_name[lc + 1], filename);
		return EX_TEMPFAIL;
	}
	return EX_OK;
}

/*
**  GETAUTH -- get authinfo from ruleset call
**
**	{server_name}, {server_addr} must be set
**
**	Parameters:
**		mci -- the mailer connection structure.
**		e -- the envelope (including the sender to specify).
**		sai -- pointer to authinfo (result).
**
**	Returns:
**		EX_OK -- ruleset was succesfully called, data may not
**			be available, sai must be checked.
**		EX_UNAVAILABLE -- ruleset unavailable (or failed).
**		EX_TEMPFAIL -- temporary failure (from ruleset).
**
**	Side Effects:
**		Fills in sai if successful.
*/

static int
getauth(mci, e, sai)
	MCI *mci;
	ENVELOPE *e;
	SASL_AI_T *sai;
{
	int i, r, l, got, ret;
	char **pvp;
	char pvpbuf[PSBUFSIZE];

	r = rscap("authinfo", macvalue(macid("{server_name}"), e),
		   macvalue(macid("{server_addr}"), e), e,
		   &pvp, pvpbuf, sizeof(pvpbuf));

	if (r != EX_OK)
		return EX_UNAVAILABLE;

	/* other than expected return value: ok (i.e., no auth) */
	if (pvp == NULL || pvp[0] == NULL || (pvp[0][0] & 0377) != CANONNET)
		return EX_OK;
	if (pvp[1] != NULL && sm_strncasecmp(pvp[1], "temp", 4) == 0)
		return EX_TEMPFAIL;

	/*
	**  parse the data, put it into sai
	**  format: "TDstring" (including the '"' !)
	**  where T is a tag: 'U', ...
	**  D is a delimiter: ':' or '='
	*/

	ret = EX_OK;	/* default return value */
	i = 0;
	got = 0;
	while (i < SASL_ENTRIES)
	{
		if (pvp[i + 1] == NULL)
			break;
		if (pvp[i + 1][0] != '"')
			break;
		switch (pvp[i + 1][1])
		{
		  case 'U':
		  case 'u':
			r = SASL_USER;
			break;
		  case 'I':
		  case 'i':
			r = SASL_AUTHID;
			break;
		  case 'P':
		  case 'p':
			r = SASL_PASSWORD;
			break;
		  case 'R':
		  case 'r':
			r = SASL_DEFREALM;
			break;
		  case 'M':
		  case 'm':
			r = SASL_MECHLIST;
			break;
		  default:
			goto fail;
		}
		l = strlen(pvp[i + 1]);

		/* check syntax */
		if (l <= 3 || pvp[i + 1][l - 1] != '"')
			goto fail;

		/* remove closing quote */
		pvp[i + 1][l - 1] = '\0';

		/* remove "TD and " */
		l -= 4;
		(*sai)[r] = (char *) sm_rpool_malloc(mci->mci_rpool, l + 1);
		if ((*sai)[r] == NULL)
			goto tempfail;
		if (pvp[i + 1][2] == ':')
		{
			/* ':text' (just copy) */
			(void) sm_strlcpy((*sai)[r], pvp[i + 1] + 3, l + 1);
			got |= 1 << r;
		}
		else if (pvp[i + 1][2] == '=')
		{
			unsigned int len;

			/* '=base64' (decode) */
# if SASL >= 20000
			ret = sasl_decode64(pvp[i + 1] + 3,
					  (unsigned int) l, (*sai)[r],
					  (unsigned int) l + 1, &len);
# else /* SASL >= 20000 */
			ret = sasl_decode64(pvp[i + 1] + 3,
					  (unsigned int) l, (*sai)[r], &len);
# endif /* SASL >= 20000 */
			if (ret != SASL_OK)
				goto fail;
			got |= 1 << r;
		}
		else
			goto fail;
		if (tTd(95, 5))
			sm_syslog(LOG_DEBUG, NOQID, "getauth %s=%s",
				  sasl_info_name[r], (*sai)[r]);
		++i;
	}

	/* did we get the expected data? */
	/* XXX: EXTERNAL mechanism only requires (and only uses) SASL_USER */
	if (!(bitset(SASL_USER_BIT|SASL_AUTHID_BIT, got) &&
	      bitset(SASL_PASSWORD_BIT, got)))
		goto fail;

	/* no authid? copy uid */
	if (!bitset(SASL_AUTHID_BIT, got))
	{
		l = strlen((*sai)[SASL_USER]) + 1;
		(*sai)[SASL_AUTHID] = (char *) sm_rpool_malloc(mci->mci_rpool,
							       l + 1);
		if ((*sai)[SASL_AUTHID] == NULL)
			goto tempfail;
		(void) sm_strlcpy((*sai)[SASL_AUTHID], (*sai)[SASL_USER], l);
	}

	/* no uid? copy authid */
	if (!bitset(SASL_USER_BIT, got))
	{
		l = strlen((*sai)[SASL_AUTHID]) + 1;
		(*sai)[SASL_USER] = (char *) sm_rpool_malloc(mci->mci_rpool,
							     l + 1);
		if ((*sai)[SASL_USER] == NULL)
			goto tempfail;
		(void) sm_strlcpy((*sai)[SASL_USER], (*sai)[SASL_AUTHID], l);
	}
	return EX_OK;

  tempfail:
	ret = EX_TEMPFAIL;
  fail:
	if (LogLevel > 8)
		sm_syslog(LOG_WARNING, NOQID,
			  "AUTH=client, relay=%.64s [%.16s], authinfo %sfailed",
			  macvalue(macid("{server_name}"), e),
			  macvalue(macid("{server_addr}"), e),
			  ret == EX_TEMPFAIL ? "temp" : "");
	for (i = 0; i <= SASL_MECHLIST; i++)
		(*sai)[i] = NULL;	/* just clear; rpool */
	return ret;
}

# if SASL >= 20000
/*
**  GETSIMPLE -- callback to get userid or authid
**
**	Parameters:
**		context -- sai
**		id -- what to do
**		result -- (pointer to) result
**		len -- (pointer to) length of result
**
**	Returns:
**		OK/failure values
*/

static int
getsimple(context, id, result, len)
	void *context;
	int id;
	const char **result;
	unsigned *len;
{
	SASL_AI_T *sai;

	if (result == NULL || context == NULL)
		return SASL_BADPARAM;
	sai = (SASL_AI_T *) context;

	switch (id)
	{
	  case SASL_CB_USER:
		*result = (*sai)[SASL_USER];
		if (tTd(95, 5))
			sm_syslog(LOG_DEBUG, NOQID, "AUTH username '%s'",
				  *result);
		if (len != NULL)
			*len = *result != NULL ? strlen(*result) : 0;
		break;

	  case SASL_CB_AUTHNAME:
		*result = (*sai)[SASL_AUTHID];
		if (tTd(95, 5))
			sm_syslog(LOG_DEBUG, NOQID, "AUTH authid '%s'",
				  *result);
		if (len != NULL)
			*len = *result != NULL ? strlen(*result) : 0;
		break;

	  case SASL_CB_LANGUAGE:
		*result = NULL;
		if (len != NULL)
			*len = 0;
		break;

	  default:
		return SASL_BADPARAM;
	}
	return SASL_OK;
}
/*
**  GETSECRET -- callback to get password
**
**	Parameters:
**		conn -- connection information
**		context -- sai
**		id -- what to do
**		psecret -- (pointer to) result
**
**	Returns:
**		OK/failure values
*/

static int
getsecret(conn, context, id, psecret)
	sasl_conn_t *conn;
	SM_UNUSED(void *context);
	int id;
	sasl_secret_t **psecret;
{
	int len;
	char *authpass;
	MCI *mci;

	if (conn == NULL || psecret == NULL || id != SASL_CB_PASS)
		return SASL_BADPARAM;

	mci = (MCI *) context;
	authpass = mci->mci_sai[SASL_PASSWORD];
	len = strlen(authpass);

	/*
	**  use an rpool because we are responsible for free()ing the secret,
	**  but we can't free() it until after the auth completes
	*/

	*psecret = (sasl_secret_t *) sm_rpool_malloc(mci->mci_rpool,
						     sizeof(sasl_secret_t) +
						     len + 1);
	if (*psecret == NULL)
		return SASL_FAIL;
	(void) sm_strlcpy((char *) (*psecret)->data, authpass, len + 1);
	(*psecret)->len = (unsigned long) len;
	return SASL_OK;
}
# else /* SASL >= 20000 */
/*
**  GETSIMPLE -- callback to get userid or authid
**
**	Parameters:
**		context -- sai
**		id -- what to do
**		result -- (pointer to) result
**		len -- (pointer to) length of result
**
**	Returns:
**		OK/failure values
*/

static int
getsimple(context, id, result, len)
	void *context;
	int id;
	const char **result;
	unsigned *len;
{
	char *h, *s;
# if SASL > 10509
	bool addrealm;
# endif /* SASL > 10509 */
	size_t l;
	SASL_AI_T *sai;
	char *authid = NULL;

	if (result == NULL || context == NULL)
		return SASL_BADPARAM;
	sai = (SASL_AI_T *) context;

	/*
	**  Unfortunately it is not clear whether this routine should
	**  return a copy of a string or just a pointer to a string.
	**  The Cyrus-SASL plugins treat these return values differently, e.g.,
	**  plugins/cram.c free()s authid, plugings/digestmd5.c does not.
	**  The best solution to this problem is to fix Cyrus-SASL, but it
	**  seems there is nobody who creates patches... Hello CMU!?
	**  The second best solution is to have flags that tell this routine
	**  whether to return an malloc()ed copy.
	**  The next best solution is to always return an malloc()ed copy,
	**  and suffer from some memory leak, which is ugly for persistent
	**  queue runners.
	**  For now we go with the last solution...
	**  We can't use rpools (which would avoid this particular problem)
	**  as explained in sasl.c.
	*/

	switch (id)
	{
	  case SASL_CB_USER:
		l = strlen((*sai)[SASL_USER]) + 1;
		s = sm_sasl_malloc(l);
		if (s == NULL)
		{
			if (len != NULL)
				*len = 0;
			*result = NULL;
			return SASL_NOMEM;
		}
		(void) sm_strlcpy(s, (*sai)[SASL_USER], l);
		*result = s;
		if (tTd(95, 5))
			sm_syslog(LOG_DEBUG, NOQID, "AUTH username '%s'",
				  *result);
		if (len != NULL)
			*len = *result != NULL ? strlen(*result) : 0;
		break;

	  case SASL_CB_AUTHNAME:
		h = (*sai)[SASL_AUTHID];
# if SASL > 10509
		/* XXX maybe other mechanisms too?! */
		addrealm = (*sai)[SASL_MECH] != NULL &&
			   sm_strcasecmp((*sai)[SASL_MECH], "CRAM-MD5") == 0;

		/*
		**  Add realm to authentication id unless authid contains
		**  '@' (i.e., a realm) or the default realm is empty.
		*/

		if (addrealm && h != NULL && strchr(h, '@') == NULL)
		{
			/* has this been done before? */
			if ((*sai)[SASL_ID_REALM] == NULL)
			{
				char *realm;

				realm = (*sai)[SASL_DEFREALM];

				/* do not add an empty realm */
				if (*realm == '\0')
				{
					authid = h;
					(*sai)[SASL_ID_REALM] = NULL;
				}
				else
				{
					l = strlen(h) + strlen(realm) + 2;

					/* should use rpool, but from where? */
					authid = sm_sasl_malloc(l);
					if (authid != NULL)
					{
						(void) sm_snprintf(authid, l,
								  "%s@%s",
								   h, realm);
						(*sai)[SASL_ID_REALM] = authid;
					}
					else
					{
						authid = h;
						(*sai)[SASL_ID_REALM] = NULL;
					}
				}
			}
			else
				authid = (*sai)[SASL_ID_REALM];
		}
		else
# endif /* SASL > 10509 */
			authid = h;
		l = strlen(authid) + 1;
		s = sm_sasl_malloc(l);
		if (s == NULL)
		{
			if (len != NULL)
				*len = 0;
			*result = NULL;
			return SASL_NOMEM;
		}
		(void) sm_strlcpy(s, authid, l);
		*result = s;
		if (tTd(95, 5))
			sm_syslog(LOG_DEBUG, NOQID, "AUTH authid '%s'",
				  *result);
		if (len != NULL)
			*len = authid ? strlen(authid) : 0;
		break;

	  case SASL_CB_LANGUAGE:
		*result = NULL;
		if (len != NULL)
			*len = 0;
		break;

	  default:
		return SASL_BADPARAM;
	}
	return SASL_OK;
}
/*
**  GETSECRET -- callback to get password
**
**	Parameters:
**		conn -- connection information
**		context -- sai
**		id -- what to do
**		psecret -- (pointer to) result
**
**	Returns:
**		OK/failure values
*/

static int
getsecret(conn, context, id, psecret)
	sasl_conn_t *conn;
	SM_UNUSED(void *context);
	int id;
	sasl_secret_t **psecret;
{
	int len;
	char *authpass;
	SASL_AI_T *sai;

	if (conn == NULL || psecret == NULL || id != SASL_CB_PASS)
		return SASL_BADPARAM;

	sai = (SASL_AI_T *) context;
	authpass = (*sai)[SASL_PASSWORD];
	len = strlen(authpass);
	*psecret = (sasl_secret_t *) sm_sasl_malloc(sizeof(sasl_secret_t) +
						    len + 1);
	if (*psecret == NULL)
		return SASL_FAIL;
	(void) sm_strlcpy((*psecret)->data, authpass, len + 1);
	(*psecret)->len = (unsigned long) len;
	return SASL_OK;
}
# endif /* SASL >= 20000 */

/*
**  SAFESASLFILE -- callback for sasl: is file safe?
**
**	Parameters:
**		context -- pointer to context between invocations (unused)
**		file -- name of file to check
**		type -- type of file to check
**
**	Returns:
**		SASL_OK -- file can be used
**		SASL_CONTINUE -- don't use file
**		SASL_FAIL -- failure (not used here)
**
*/

int
#if SASL > 10515
safesaslfile(context, file, type)
#else /* SASL > 10515 */
safesaslfile(context, file)
#endif /* SASL > 10515 */
	void *context;
# if SASL >= 20000
	const char *file;
# else /* SASL >= 20000 */
	char *file;
# endif /* SASL >= 20000 */
#if SASL > 10515
# if SASL >= 20000
	sasl_verify_type_t type;
# else /* SASL >= 20000 */
	int type;
# endif /* SASL >= 20000 */
#endif /* SASL > 10515 */
{
	long sff;
	int r;
#if SASL <= 10515
	size_t len;
#endif /* SASL <= 10515 */
	char *p;

	if (file == NULL || *file == '\0')
		return SASL_OK;
	sff = SFF_SAFEDIRPATH|SFF_NOWLINK|SFF_NOWWFILES|SFF_ROOTOK;
#if SASL <= 10515
	if ((p = strrchr(file, '/')) == NULL)
		p = file;
	else
		++p;

	/* everything beside libs and .conf files must not be readable */
	len = strlen(p);
	if ((len <= 3 || strncmp(p, "lib", 3) != 0) &&
	    (len <= 5 || strncmp(p + len - 5, ".conf", 5) != 0))
	{
		if (!bitnset(DBS_GROUPREADABLESASLDBFILE, DontBlameSendmail))
			sff |= SFF_NORFILES;
		if (!bitnset(DBS_GROUPWRITABLESASLDBFILE, DontBlameSendmail))
			sff |= SFF_NOGWFILES;
	}
#else /* SASL <= 10515 */
	/* files containing passwords should be not readable */
	if (type == SASL_VRFY_PASSWD)
	{
		if (bitnset(DBS_GROUPREADABLESASLDBFILE, DontBlameSendmail))
			sff |= SFF_NOWRFILES;
		else
			sff |= SFF_NORFILES;
		if (!bitnset(DBS_GROUPWRITABLESASLDBFILE, DontBlameSendmail))
			sff |= SFF_NOGWFILES;
	}
#endif /* SASL <= 10515 */

	p = (char *) file;
	if ((r = safefile(p, RunAsUid, RunAsGid, RunAsUserName, sff,
			  S_IRUSR, NULL)) == 0)
		return SASL_OK;
	if (LogLevel > (r != ENOENT ? 8 : 10))
		sm_syslog(LOG_WARNING, NOQID, "error: safesasl(%s) failed: %s",
			  p, sm_errstring(r));
	return SASL_CONTINUE;
}

/*
**  SASLGETREALM -- return the realm for SASL
**
**	return the realm for the client
**
**	Parameters:
**		context -- context shared between invocations
**		availrealms -- list of available realms
**			{realm, realm, ...}
**		result -- pointer to result
**
**	Returns:
**		failure/success
*/

static int
saslgetrealm(context, id, availrealms, result)
	void *context;
	int id;
	const char **availrealms;
	const char **result;
{
	char *r;
	SASL_AI_T *sai;

	sai = (SASL_AI_T *) context;
	if (sai == NULL)
		return SASL_FAIL;
	r = (*sai)[SASL_DEFREALM];

	if (LogLevel > 12)
		sm_syslog(LOG_INFO, NOQID,
			  "AUTH=client, realm=%s, available realms=%s",
			  r == NULL ? "<No Realm>" : r,
			  (availrealms == NULL || *availrealms == NULL)
				? "<No Realms>" : *availrealms);

	/* check whether context is in list */
	if (availrealms != NULL && *availrealms != NULL)
	{
		if (iteminlist(context, (char *)(*availrealms + 1), " ,}") ==
		    NULL)
		{
			if (LogLevel > 8)
				sm_syslog(LOG_ERR, NOQID,
					  "AUTH=client, realm=%s not in list=%s",
					  r, *availrealms);
			return SASL_FAIL;
		}
	}
	*result = r;
	return SASL_OK;
}
/*
**  ITEMINLIST -- does item appear in list?
**
**	Check whether item appears in list (which must be separated by a
**	character in delim) as a "word", i.e. it must appear at the begin
**	of the list or after a space, and it must end with a space or the
**	end of the list.
**
**	Parameters:
**		item -- item to search.
**		list -- list of items.
**		delim -- list of delimiters.
**
**	Returns:
**		pointer to occurrence (NULL if not found).
*/

char *
iteminlist(item, list, delim)
	char *item;
	char *list;
	char *delim;
{
	char *s;
	int len;

	if (list == NULL || *list == '\0')
		return NULL;
	if (item == NULL || *item == '\0')
		return NULL;
	s = list;
	len = strlen(item);
	while (s != NULL && *s != '\0')
	{
		if (sm_strncasecmp(s, item, len) == 0 &&
		    (s[len] == '\0' || strchr(delim, s[len]) != NULL))
			return s;
		s = strpbrk(s, delim);
		if (s != NULL)
			while (*++s == ' ')
				continue;
	}
	return NULL;
}
/*
**  REMOVEMECH -- remove item [rem] from list [list]
**
**	Parameters:
**		rem -- item to remove
**		list -- list of items
**		rpool -- resource pool from which result is allocated.
**
**	Returns:
**		pointer to new list (NULL in case of error).
*/

static char *
removemech(rem, list, rpool)
	char *rem;
	char *list;
	SM_RPOOL_T *rpool;
{
	char *ret;
	char *needle;
	int len;

	if (list == NULL)
		return NULL;
	if (rem == NULL || *rem == '\0')
	{
		/* take out what? */
		return NULL;
	}

	/* find the item in the list */
	if ((needle = iteminlist(rem, list, " ")) == NULL)
	{
		/* not in there: return original */
		return list;
	}

	/* length of string without rem */
	len = strlen(list) - strlen(rem);
	if (len <= 0)
	{
		ret = (char *) sm_rpool_malloc_x(rpool, 1);
		*ret = '\0';
		return ret;
	}
	ret = (char *) sm_rpool_malloc_x(rpool, len);
	memset(ret, '\0', len);

	/* copy from start to removed item */
	memcpy(ret, list, needle - list);

	/* length of rest of string past removed item */
	len = strlen(needle) - strlen(rem) - 1;
	if (len > 0)
	{
		/* not last item -- copy into string */
		memcpy(ret + (needle - list),
		       list + (needle - list) + strlen(rem) + 1,
		       len);
	}
	else
		ret[(needle - list) - 1] = '\0';
	return ret;
}
/*
**  ATTEMPTAUTH -- try to AUTHenticate using one mechanism
**
**	Parameters:
**		m -- the mailer.
**		mci -- the mailer connection structure.
**		e -- the envelope (including the sender to specify).
**		sai - sasl authinfo
**
**	Returns:
**		EX_OK -- authentication was successful.
**		EX_NOPERM -- authentication failed.
**		EX_IOERR -- authentication dialogue failed (I/O problem?).
**		EX_TEMPFAIL -- temporary failure.
**
*/

static int
attemptauth(m, mci, e, sai)
	MAILER *m;
	MCI *mci;
	ENVELOPE *e;
	SASL_AI_T *sai;
{
	int saslresult, smtpresult;
# if SASL >= 20000
	sasl_ssf_t ssf;
	const char *auth_id;
	const char *out;
# else /* SASL >= 20000 */
	sasl_external_properties_t ssf;
	char *out;
# endif /* SASL >= 20000 */
	unsigned int outlen;
	sasl_interact_t *client_interact = NULL;
	char *mechusing;
	sasl_security_properties_t ssp;

	/* MUST NOT be a multiple of 4: bug in some sasl_encode64() versions */
	char in64[MAXOUTLEN + 1];
#if NETINET || (NETINET6 && SASL >= 20000)
	extern SOCKADDR CurHostAddr;
#endif /* NETINET || (NETINET6 && SASL >= 20000) */

	/* no mechanism selected (yet) */
	(*sai)[SASL_MECH] = NULL;

	/* dispose old connection */
	if (mci->mci_conn != NULL)
		sasl_dispose(&(mci->mci_conn));

	/* make a new client sasl connection */
# if SASL >= 20000
	/*
	**  We provide the callbacks again because global callbacks in
	**  sasl_client_init() are ignored if SASL has been initialized
	**  before, for example, by a library such as libnss-ldap.
	*/

	saslresult = sasl_client_new(bitnset(M_LMTP, m->m_flags) ? "lmtp"
								 : "smtp",
				     CurHostName, NULL, NULL, callbacks, 0,
				     &mci->mci_conn);
# else /* SASL >= 20000 */
	saslresult = sasl_client_new(bitnset(M_LMTP, m->m_flags) ? "lmtp"
								 : "smtp",
				     CurHostName, NULL, 0, &mci->mci_conn);
# endif /* SASL >= 20000 */
	if (saslresult != SASL_OK)
		return EX_TEMPFAIL;

	/* set properties */
	(void) memset(&ssp, '\0', sizeof(ssp));

	/* XXX should these be options settable via .cf ? */
	ssp.max_ssf = MaxSLBits;
	ssp.maxbufsize = MAXOUTLEN;
#  if 0
	ssp.security_flags = SASL_SEC_NOPLAINTEXT;
#  endif /* 0 */
	saslresult = sasl_setprop(mci->mci_conn, SASL_SEC_PROPS, &ssp);
	if (saslresult != SASL_OK)
		return EX_TEMPFAIL;

# if SASL >= 20000
	/* external security strength factor, authentication id */
	ssf = 0;
	auth_id = NULL;
#  if STARTTLS
	out = macvalue(macid("{cert_subject}"), e);
	if (out != NULL && *out != '\0')
		auth_id = out;
	out = macvalue(macid("{cipher_bits}"), e);
	if (out != NULL && *out != '\0')
		ssf = atoi(out);
#  endif /* STARTTLS */
	saslresult = sasl_setprop(mci->mci_conn, SASL_SSF_EXTERNAL, &ssf);
	if (saslresult != SASL_OK)
		return EX_TEMPFAIL;
	saslresult = sasl_setprop(mci->mci_conn, SASL_AUTH_EXTERNAL, auth_id);
	if (saslresult != SASL_OK)
		return EX_TEMPFAIL;

#  if NETINET || NETINET6
	/* set local/remote ipv4 addresses */
	if (mci->mci_out != NULL && (
#   if NETINET6
		CurHostAddr.sa.sa_family == AF_INET6 ||
#   endif /* NETINET6 */
		CurHostAddr.sa.sa_family == AF_INET))
	{
		SOCKADDR_LEN_T addrsize;
		SOCKADDR saddr_l;
		char localip[60], remoteip[60];

		switch (CurHostAddr.sa.sa_family)
		{
		  case AF_INET:
			addrsize = sizeof(struct sockaddr_in);
			break;
#   if NETINET6
		  case AF_INET6:
			addrsize = sizeof(struct sockaddr_in6);
			break;
#   endif /* NETINET6 */
		  default:
			break;
		}
		if (iptostring(&CurHostAddr, addrsize,
			       remoteip, sizeof(remoteip)))
		{
			if (sasl_setprop(mci->mci_conn, SASL_IPREMOTEPORT,
					 remoteip) != SASL_OK)
				return EX_TEMPFAIL;
		}
		addrsize = sizeof(saddr_l);
		if (getsockname(sm_io_getinfo(mci->mci_out, SM_IO_WHAT_FD,
					      NULL),
				(struct sockaddr *) &saddr_l, &addrsize) == 0)
		{
			if (iptostring(&saddr_l, addrsize,
				       localip, sizeof(localip)))
			{
				if (sasl_setprop(mci->mci_conn,
						 SASL_IPLOCALPORT,
						 localip) != SASL_OK)
					return EX_TEMPFAIL;
			}
		}
	}
#  endif /* NETINET || NETINET6 */

	/* start client side of sasl */
	saslresult = sasl_client_start(mci->mci_conn, mci->mci_saslcap,
				       &client_interact,
				       &out, &outlen,
				       (const char **) &mechusing);
# else /* SASL >= 20000 */
	/* external security strength factor, authentication id */
	ssf.ssf = 0;
	ssf.auth_id = NULL;
#  if STARTTLS
	out = macvalue(macid("{cert_subject}"), e);
	if (out != NULL && *out != '\0')
		ssf.auth_id = out;
	out = macvalue(macid("{cipher_bits}"), e);
	if (out != NULL && *out != '\0')
		ssf.ssf = atoi(out);
#  endif /* STARTTLS */
	saslresult = sasl_setprop(mci->mci_conn, SASL_SSF_EXTERNAL, &ssf);
	if (saslresult != SASL_OK)
		return EX_TEMPFAIL;

#  if NETINET
	/* set local/remote ipv4 addresses */
	if (mci->mci_out != NULL && CurHostAddr.sa.sa_family == AF_INET)
	{
		SOCKADDR_LEN_T addrsize;
		struct sockaddr_in saddr_l;

		if (sasl_setprop(mci->mci_conn, SASL_IP_REMOTE,
				 (struct sockaddr_in *) &CurHostAddr)
		    != SASL_OK)
			return EX_TEMPFAIL;
		addrsize = sizeof(struct sockaddr_in);
		if (getsockname(sm_io_getinfo(mci->mci_out, SM_IO_WHAT_FD,
					      NULL),
				(struct sockaddr *) &saddr_l, &addrsize) == 0)
		{
			if (sasl_setprop(mci->mci_conn, SASL_IP_LOCAL,
					 &saddr_l) != SASL_OK)
				return EX_TEMPFAIL;
		}
	}
#  endif /* NETINET */

	/* start client side of sasl */
	saslresult = sasl_client_start(mci->mci_conn, mci->mci_saslcap,
				       NULL, &client_interact,
				       &out, &outlen,
				       (const char **) &mechusing);
# endif /* SASL >= 20000 */

	if (saslresult != SASL_OK && saslresult != SASL_CONTINUE)
	{
		if (saslresult == SASL_NOMECH && LogLevel > 8)
		{
			sm_syslog(LOG_NOTICE, e->e_id,
				  "AUTH=client, available mechanisms do not fulfill requirements");
		}
		return EX_TEMPFAIL;
	}

	/* just point current mechanism to the data in the sasl library */
	(*sai)[SASL_MECH] = mechusing;

	/* send the info across the wire */
	if (out == NULL
		/* login and digest-md5 up to 1.5.28 set out="" */
	    || (outlen == 0 &&
		(sm_strcasecmp(mechusing, "LOGIN") == 0 ||
		 sm_strcasecmp(mechusing, "DIGEST-MD5") == 0))
	   )
	{
		/* no initial response */
		smtpmessage("AUTH %s", m, mci, mechusing);
	}
	else if (outlen == 0)
	{
		/*
		**  zero-length initial response, per RFC 2554 4.:
		**  "Unlike a zero-length client answer to a 334 reply, a zero-
		**  length initial response is sent as a single equals sign"
		*/

		smtpmessage("AUTH %s =", m, mci, mechusing);
	}
	else
	{
		saslresult = sasl_encode64(out, outlen, in64, sizeof(in64),
					   NULL);
		if (saslresult != SASL_OK) /* internal error */
		{
			if (LogLevel > 8)
				sm_syslog(LOG_ERR, e->e_id,
					"encode64 for AUTH failed");
			return EX_TEMPFAIL;
		}
		smtpmessage("AUTH %s %s", m, mci, mechusing, in64);
	}
# if SASL < 20000
	sm_sasl_free(out); /* XXX only if no rpool is used */
# endif /* SASL < 20000 */

	/* get the reply */
	smtpresult = reply(m, mci, e, TimeOuts.to_auth, getsasldata, NULL,
			XS_AUTH);

	for (;;)
	{
		/* check return code from server */
		if (smtpresult == 235)
		{
			macdefine(&mci->mci_macro, A_TEMP, macid("{auth_type}"),
				  mechusing);
			return EX_OK;
		}
		if (smtpresult == -1)
			return EX_IOERR;
		if (REPLYTYPE(smtpresult) == 5)
			return EX_NOPERM;	/* ugly, but ... */
		if (REPLYTYPE(smtpresult) != 3)
		{
			/* should we fail deliberately, see RFC 2554 4. ? */
			/* smtpmessage("*", m, mci); */
			return EX_TEMPFAIL;
		}

		saslresult = sasl_client_step(mci->mci_conn,
					      mci->mci_sasl_string,
					      mci->mci_sasl_string_len,
					      &client_interact,
					      &out, &outlen);

		if (saslresult != SASL_OK && saslresult != SASL_CONTINUE)
		{
			if (tTd(95, 5))
				sm_dprintf("AUTH FAIL=%s (%d)\n",
					sasl_errstring(saslresult, NULL, NULL),
					saslresult);

			/* fail deliberately, see RFC 2554 4. */
			smtpmessage("*", m, mci);

			/*
			**  but we should only fail for this authentication
			**  mechanism; how to do that?
			*/

			smtpresult = reply(m, mci, e, TimeOuts.to_auth,
					   getsasldata, NULL, XS_AUTH);
			return EX_NOPERM;
		}

		if (outlen > 0)
		{
			saslresult = sasl_encode64(out, outlen, in64,
						   sizeof(in64), NULL);
			if (saslresult != SASL_OK)
			{
				/* give an error reply to the other side! */
				smtpmessage("*", m, mci);
				return EX_TEMPFAIL;
			}
		}
		else
			in64[0] = '\0';
# if SASL < 20000
		sm_sasl_free(out); /* XXX only if no rpool is used */
# endif /* SASL < 20000 */
		smtpmessage("%s", m, mci, in64);
		smtpresult = reply(m, mci, e, TimeOuts.to_auth,
				   getsasldata, NULL, XS_AUTH);
	}
	/* NOTREACHED */
}
/*
**  SMTPAUTH -- try to AUTHenticate
**
**	This will try mechanisms in the order the sasl library decided until:
**	- there are no more mechanisms
**	- a mechanism succeeds
**	- the sasl library fails initializing
**
**	Parameters:
**		m -- the mailer.
**		mci -- the mailer connection info.
**		e -- the envelope.
**
**	Returns:
**		EX_OK -- authentication was successful
**		EX_UNAVAILABLE -- authentication not possible, e.g.,
**			no data available.
**		EX_NOPERM -- authentication failed.
**		EX_TEMPFAIL -- temporary failure.
**
**	Notice: AuthInfo is used for all connections, hence we must
**		return EX_TEMPFAIL only if we really want to retry, i.e.,
**		iff getauth() tempfailed or getauth() was used and
**		authentication tempfailed.
*/

int
smtpauth(m, mci, e)
	MAILER *m;
	MCI *mci;
	ENVELOPE *e;
{
	int result;
	int i;
	bool usedgetauth;

	mci->mci_sasl_auth = false;
	for (i = 0; i < SASL_MECH ; i++)
		mci->mci_sai[i] = NULL;

	result = getauth(mci, e, &(mci->mci_sai));
	if (result == EX_TEMPFAIL)
		return result;
	usedgetauth = true;

	/* no data available: don't try to authenticate */
	if (result == EX_OK && mci->mci_sai[SASL_AUTHID] == NULL)
		return result;
	if (result != EX_OK)
	{
		if (SASLInfo == NULL)
			return EX_UNAVAILABLE;

		/* read authinfo from file */
		result = readauth(SASLInfo, true, &(mci->mci_sai),
				  mci->mci_rpool);
		if (result != EX_OK)
			return result;
		usedgetauth = false;
	}

	/* check whether sufficient data is available */
	if (mci->mci_sai[SASL_PASSWORD] == NULL ||
	    *(mci->mci_sai)[SASL_PASSWORD] == '\0')
		return EX_UNAVAILABLE;
	if ((mci->mci_sai[SASL_AUTHID] == NULL ||
	     *(mci->mci_sai)[SASL_AUTHID] == '\0') &&
	    (mci->mci_sai[SASL_USER] == NULL ||
	     *(mci->mci_sai)[SASL_USER] == '\0'))
		return EX_UNAVAILABLE;

	/* set the context for the callback function to sai */
# if SASL >= 20000
	callbacks[CB_PASS_IDX].context = (void *) mci;
# else /* SASL >= 20000 */
	callbacks[CB_PASS_IDX].context = (void *) &mci->mci_sai;
# endif /* SASL >= 20000 */
	callbacks[CB_USER_IDX].context = (void *) &mci->mci_sai;
	callbacks[CB_AUTHNAME_IDX].context = (void *) &mci->mci_sai;
	callbacks[CB_GETREALM_IDX].context = (void *) &mci->mci_sai;
#if 0
	callbacks[CB_SAFESASL_IDX].context = (void *) &mci->mci_sai;
#endif /* 0 */

	/* set default value for realm */
	if ((mci->mci_sai)[SASL_DEFREALM] == NULL)
		(mci->mci_sai)[SASL_DEFREALM] = sm_rpool_strdup_x(e->e_rpool,
							macvalue('j', CurEnv));

	/* set default value for list of mechanism to use */
	if ((mci->mci_sai)[SASL_MECHLIST] == NULL ||
	    *(mci->mci_sai)[SASL_MECHLIST] == '\0')
		(mci->mci_sai)[SASL_MECHLIST] = AuthMechanisms;

	/* create list of mechanisms to try */
	mci->mci_saslcap = intersect((mci->mci_sai)[SASL_MECHLIST],
				     mci->mci_saslcap, mci->mci_rpool);

	/* initialize sasl client library */
	result = init_sasl_client();
	if (result != SASL_OK)
		return usedgetauth ? EX_TEMPFAIL : EX_UNAVAILABLE;
	do
	{
		result = attemptauth(m, mci, e, &(mci->mci_sai));
		if (result == EX_OK)
			mci->mci_sasl_auth = true;
		else if (result == EX_TEMPFAIL || result == EX_NOPERM)
		{
			mci->mci_saslcap = removemech((mci->mci_sai)[SASL_MECH],
						      mci->mci_saslcap,
						      mci->mci_rpool);
			if (mci->mci_saslcap == NULL ||
			    *(mci->mci_saslcap) == '\0')
				return usedgetauth ? result
						   : EX_UNAVAILABLE;
		}
		else
			return result;
	} while (result != EX_OK);
	return result;
}
#endif /* SASL */

/*
**  SMTPMAILFROM -- send MAIL command
**
**	Parameters:
**		m -- the mailer.
**		mci -- the mailer connection structure.
**		e -- the envelope (including the sender to specify).
*/

int
smtpmailfrom(m, mci, e)
	MAILER *m;
	MCI *mci;
	ENVELOPE *e;
{
	int r;
	char *bufp;
	char *bodytype;
	char *enhsc;
	char buf[MAXNAME + 1];
	char optbuf[MAXLINE];

	if (tTd(18, 2))
		sm_dprintf("smtpmailfrom: CurHost=%s\n", CurHostName);
	enhsc = NULL;

	/*
	**  Check if connection is gone, if so
	**  it's a tempfail and we use mci_errno
	**  for the reason.
	*/

	if (mci->mci_state == MCIS_CLOSED)
	{
		errno = mci->mci_errno;
		return EX_TEMPFAIL;
	}

	/* set up appropriate options to include */
	if (bitset(MCIF_SIZE, mci->mci_flags) && e->e_msgsize > 0)
	{
		(void) sm_snprintf(optbuf, sizeof(optbuf), " SIZE=%ld",
			e->e_msgsize);
		bufp = &optbuf[strlen(optbuf)];
	}
	else
	{
		optbuf[0] = '\0';
		bufp = optbuf;
	}

	bodytype = e->e_bodytype;
	if (bitset(MCIF_8BITMIME, mci->mci_flags))
	{
		if (bodytype == NULL &&
		    bitset(MM_MIME8BIT, MimeMode) &&
		    bitset(EF_HAS8BIT, e->e_flags) &&
		    !bitset(EF_DONT_MIME, e->e_flags) &&
		    !bitnset(M_8BITS, m->m_flags))
			bodytype = "8BITMIME";
		if (bodytype != NULL &&
		    SPACELEFT(optbuf, bufp) > strlen(bodytype) + 7)
		{
			(void) sm_snprintf(bufp, SPACELEFT(optbuf, bufp),
				 " BODY=%s", bodytype);
			bufp += strlen(bufp);
		}
	}
	else if (bitnset(M_8BITS, m->m_flags) ||
		 !bitset(EF_HAS8BIT, e->e_flags) ||
		 bitset(MCIF_8BITOK, mci->mci_flags))
	{
		/* EMPTY */
		/* just pass it through */
	}
#if MIME8TO7
	else if (bitset(MM_CVTMIME, MimeMode) &&
		 !bitset(EF_DONT_MIME, e->e_flags) &&
		 (!bitset(MM_PASS8BIT, MimeMode) ||
		  bitset(EF_IS_MIME, e->e_flags)))
	{
		/* must convert from 8bit MIME format to 7bit encoded */
		mci->mci_flags |= MCIF_CVT8TO7;
	}
#endif /* MIME8TO7 */
	else if (!bitset(MM_PASS8BIT, MimeMode))
	{
		/* cannot just send a 8-bit version */
		extern char MsgBuf[];

		usrerrenh("5.6.3", "%s does not support 8BITMIME", CurHostName);
		mci_setstat(mci, EX_NOTSTICKY, "5.6.3", MsgBuf);
		return EX_DATAERR;
	}

	if (bitset(MCIF_DSN, mci->mci_flags))
	{
		if (e->e_envid != NULL &&
		    SPACELEFT(optbuf, bufp) > strlen(e->e_envid) + 7)
		{
			(void) sm_snprintf(bufp, SPACELEFT(optbuf, bufp),
				 " ENVID=%s", e->e_envid);
			bufp += strlen(bufp);
		}

		/* RET= parameter */
		if (bitset(EF_RET_PARAM, e->e_flags) &&
		    SPACELEFT(optbuf, bufp) > 9)
		{
			(void) sm_snprintf(bufp, SPACELEFT(optbuf, bufp),
				 " RET=%s",
				 bitset(EF_NO_BODY_RETN, e->e_flags) ?
					"HDRS" : "FULL");
			bufp += strlen(bufp);
		}
	}

	if (bitset(MCIF_AUTH, mci->mci_flags) && e->e_auth_param != NULL &&
	    SPACELEFT(optbuf, bufp) > strlen(e->e_auth_param) + 7
#if SASL
	     && (!bitset(SASL_AUTH_AUTH, SASLOpts) || mci->mci_sasl_auth)
#endif /* SASL */
	    )
	{
		(void) sm_snprintf(bufp, SPACELEFT(optbuf, bufp),
			 " AUTH=%s", e->e_auth_param);
		bufp += strlen(bufp);
	}

	/*
	**  17 is the max length required, we could use log() to compute
	**  the exact length (and check IS_DLVR_TRACE())
	*/

	if (bitset(MCIF_DLVR_BY, mci->mci_flags) &&
	    IS_DLVR_BY(e) && SPACELEFT(optbuf, bufp) > 17)
	{
		long dby;

		/*
		**  Avoid problems with delays (for R) since the check
		**  in deliver() whether min-deliver-time is sufficient.
		**  Alternatively we could pass the computed time to this
		**  function.
		*/

		dby = e->e_deliver_by - (curtime() - e->e_ctime);
		if (dby <= 0 && IS_DLVR_RETURN(e))
			dby = mci->mci_min_by <= 0 ? 1 : mci->mci_min_by;
		(void) sm_snprintf(bufp, SPACELEFT(optbuf, bufp),
			" BY=%ld;%c%s",
			dby,
			IS_DLVR_RETURN(e) ? 'R' : 'N',
			IS_DLVR_TRACE(e) ? "T" : "");
		bufp += strlen(bufp);
	}

	/*
	**  Send the MAIL command.
	**	Designates the sender.
	*/

	mci->mci_state = MCIS_MAIL;

	if (bitset(EF_RESPONSE, e->e_flags) &&
	    !bitnset(M_NO_NULL_FROM, m->m_flags))
		buf[0] = '\0';
	else
		expand("\201g", buf, sizeof(buf), e);
	if (buf[0] == '<')
	{
		/* strip off <angle brackets> (put back on below) */
		bufp = &buf[strlen(buf) - 1];
		if (*bufp == '>')
			*bufp = '\0';
		bufp = &buf[1];
	}
	else
		bufp = buf;
	if (bitnset(M_LOCALMAILER, e->e_from.q_mailer->m_flags) ||
	    !bitnset(M_FROMPATH, m->m_flags))
	{
		smtpmessage("MAIL From:<%s>%s", m, mci, bufp, optbuf);
	}
	else
	{
		smtpmessage("MAIL From:<@%s%c%s>%s", m, mci, MyHostName,
			    *bufp == '@' ? ',' : ':', bufp, optbuf);
	}
	SmtpPhase = mci->mci_phase = "client MAIL";
	sm_setproctitle(true, e, "%s %s: %s", qid_printname(e),
			CurHostName, mci->mci_phase);
	r = reply(m, mci, e, TimeOuts.to_mail, NULL, &enhsc, XS_MAIL);
	if (r < 0)
	{
		/* communications failure */
		mci_setstat(mci, EX_TEMPFAIL, "4.4.2", NULL);
		return EX_TEMPFAIL;
	}
	else if (r == SMTPCLOSING)
	{
		/* service shutting down: handled by reply() */
		return EX_TEMPFAIL;
	}
	else if (REPLYTYPE(r) == 4)
	{
		mci_setstat(mci, EX_NOTSTICKY, ENHSCN(enhsc, smtptodsn(r)),
			    SmtpReplyBuffer);
		return EX_TEMPFAIL;
	}
	else if (REPLYTYPE(r) == 2)
	{
		return EX_OK;
	}
	else if (r == 501)
	{
		/* syntax error in arguments */
		mci_setstat(mci, EX_NOTSTICKY, ENHSCN(enhsc, "5.5.2"),
			    SmtpReplyBuffer);
		return EX_DATAERR;
	}
	else if (r == 553)
	{
		/* mailbox name not allowed */
		mci_setstat(mci, EX_NOTSTICKY, ENHSCN(enhsc, "5.1.3"),
			    SmtpReplyBuffer);
		return EX_DATAERR;
	}
	else if (r == 552)
	{
		/* exceeded storage allocation */
		mci_setstat(mci, EX_NOTSTICKY, ENHSCN(enhsc, "5.3.4"),
			    SmtpReplyBuffer);
		if (bitset(MCIF_SIZE, mci->mci_flags))
			e->e_flags |= EF_NO_BODY_RETN;
		return EX_UNAVAILABLE;
	}
	else if (REPLYTYPE(r) == 5)
	{
		/* unknown error */
		mci_setstat(mci, EX_NOTSTICKY, ENHSCN(enhsc, "5.0.0"),
			    SmtpReplyBuffer);
		return EX_UNAVAILABLE;
	}

	if (LogLevel > 1)
	{
		sm_syslog(LOG_CRIT, e->e_id,
			  "%.100s: SMTP MAIL protocol error: %s",
			  CurHostName,
			  shortenstring(SmtpReplyBuffer, 403));
	}

	/* protocol error -- close up */
	mci_setstat(mci, EX_PROTOCOL, ENHSCN(enhsc, "5.5.1"),
		    SmtpReplyBuffer);
	smtpquit(m, mci, e);
	return EX_PROTOCOL;
}
/*
**  SMTPRCPT -- designate recipient.
**
**	Parameters:
**		to -- address of recipient.
**		m -- the mailer we are sending to.
**		mci -- the connection info for this transaction.
**		e -- the envelope for this transaction.
**
**	Returns:
**		exit status corresponding to recipient status.
**
**	Side Effects:
**		Sends the mail via SMTP.
*/

int
smtprcpt(to, m, mci, e, ctladdr, xstart)
	ADDRESS *to;
	register MAILER *m;
	MCI *mci;
	ENVELOPE *e;
	ADDRESS *ctladdr;
	time_t xstart;
{
	char *bufp;
	char optbuf[MAXLINE];

#if PIPELINING
	/*
	**  If there is status waiting from the other end, read it.
	**  This should normally happen because of SMTP pipelining.
	*/

	while (mci->mci_nextaddr != NULL &&
	       sm_io_getinfo(mci->mci_in, SM_IO_IS_READABLE, NULL) > 0)
	{
		int r;

		r = smtprcptstat(mci->mci_nextaddr, m, mci, e);
		if (r != EX_OK)
		{
			markfailure(e, mci->mci_nextaddr, mci, r, false);
			giveresponse(r, mci->mci_nextaddr->q_status,  m, mci,
				     ctladdr, xstart, e, to);
		}
		mci->mci_nextaddr = mci->mci_nextaddr->q_pchain;
	}
#endif /* PIPELINING */

	/*
	**  Check if connection is gone, if so
	**  it's a tempfail and we use mci_errno
	**  for the reason.
	*/

	if (mci->mci_state == MCIS_CLOSED)
	{
		errno = mci->mci_errno;
		return EX_TEMPFAIL;
	}

	optbuf[0] = '\0';
	bufp = optbuf;

	/*
	**  Warning: in the following it is assumed that the free space
	**  in bufp is sizeof(optbuf)
	*/

	if (bitset(MCIF_DSN, mci->mci_flags))
	{
		if (IS_DLVR_NOTIFY(e) &&
		    !bitset(MCIF_DLVR_BY, mci->mci_flags))
		{
			/* RFC 2852: 4.1.4.2 */
			if (!bitset(QHASNOTIFY, to->q_flags))
				to->q_flags |= QPINGONFAILURE|QPINGONDELAY|QHASNOTIFY;
			else if (bitset(QPINGONSUCCESS, to->q_flags) ||
				 bitset(QPINGONFAILURE, to->q_flags) ||
				 bitset(QPINGONDELAY, to->q_flags))
				to->q_flags |= QPINGONDELAY;
		}

		/* NOTIFY= parameter */
		if (bitset(QHASNOTIFY, to->q_flags) &&
		    bitset(QPRIMARY, to->q_flags) &&
		    !bitnset(M_LOCALMAILER, m->m_flags))
		{
			bool firstone = true;

			(void) sm_strlcat(bufp, " NOTIFY=", sizeof(optbuf));
			if (bitset(QPINGONSUCCESS, to->q_flags))
			{
				(void) sm_strlcat(bufp, "SUCCESS", sizeof(optbuf));
				firstone = false;
			}
			if (bitset(QPINGONFAILURE, to->q_flags))
			{
				if (!firstone)
					(void) sm_strlcat(bufp, ",",
						       sizeof(optbuf));
				(void) sm_strlcat(bufp, "FAILURE", sizeof(optbuf));
				firstone = false;
			}
			if (bitset(QPINGONDELAY, to->q_flags))
			{
				if (!firstone)
					(void) sm_strlcat(bufp, ",",
						       sizeof(optbuf));
				(void) sm_strlcat(bufp, "DELAY", sizeof(optbuf));
				firstone = false;
			}
			if (firstone)
				(void) sm_strlcat(bufp, "NEVER", sizeof(optbuf));
			bufp += strlen(bufp);
		}

		/* ORCPT= parameter */
		if (to->q_orcpt != NULL &&
		    SPACELEFT(optbuf, bufp) > strlen(to->q_orcpt) + 7)
		{
			(void) sm_snprintf(bufp, SPACELEFT(optbuf, bufp),
				 " ORCPT=%s", to->q_orcpt);
			bufp += strlen(bufp);
		}
	}

	smtpmessage("RCPT To:<%s>%s", m, mci, to->q_user, optbuf);
	mci->mci_state = MCIS_RCPT;

	SmtpPhase = mci->mci_phase = "client RCPT";
	sm_setproctitle(true, e, "%s %s: %s", qid_printname(e),
			CurHostName, mci->mci_phase);

#if PIPELINING
	/*
	**  If running SMTP pipelining, we will pick up status later
	*/

	if (bitset(MCIF_PIPELINED, mci->mci_flags))
		return EX_OK;
#endif /* PIPELINING */

	return smtprcptstat(to, m, mci, e);
}
/*
**  SMTPRCPTSTAT -- get recipient status
**
**	This is only called during SMTP pipelining
**
**	Parameters:
**		to -- address of recipient.
**		m -- mailer being sent to.
**		mci -- the mailer connection information.
**		e -- the envelope for this message.
**
**	Returns:
**		EX_* -- protocol status
*/

static int
smtprcptstat(to, m, mci, e)
	ADDRESS *to;
	MAILER *m;
	register MCI *mci;
	register ENVELOPE *e;
{
	int r;
	int save_errno;
	char *enhsc;

	/*
	**  Check if connection is gone, if so
	**  it's a tempfail and we use mci_errno
	**  for the reason.
	*/

	if (mci->mci_state == MCIS_CLOSED)
	{
		errno = mci->mci_errno;
		return EX_TEMPFAIL;
	}

	enhsc = NULL;
	r = reply(m, mci, e, TimeOuts.to_rcpt, NULL, &enhsc, XS_RCPT);
	save_errno = errno;
	to->q_rstatus = sm_rpool_strdup_x(e->e_rpool, SmtpReplyBuffer);
	to->q_status = ENHSCN_RPOOL(enhsc, smtptodsn(r), e->e_rpool);
	if (!bitnset(M_LMTP, m->m_flags))
		to->q_statmta = mci->mci_host;
	if (r < 0 || REPLYTYPE(r) == 4)
	{
		mci->mci_retryrcpt = true;
		errno = save_errno;
		return EX_TEMPFAIL;
	}
	else if (REPLYTYPE(r) == 2)
	{
		char *t;

		if ((t = mci->mci_tolist) != NULL)
		{
			char *p;

			*t++ = ',';
			for (p = to->q_paddr; *p != '\0'; *t++ = *p++)
				continue;
			*t = '\0';
			mci->mci_tolist = t;
		}
#if PIPELINING
		mci->mci_okrcpts++;
#endif /* PIPELINING */
		return EX_OK;
	}
	else if (r == 550)
	{
		to->q_status = ENHSCN_RPOOL(enhsc, "5.1.1", e->e_rpool);
		return EX_NOUSER;
	}
	else if (r == 551)
	{
		to->q_status = ENHSCN_RPOOL(enhsc, "5.1.6", e->e_rpool);
		return EX_NOUSER;
	}
	else if (r == 553)
	{
		to->q_status = ENHSCN_RPOOL(enhsc, "5.1.3", e->e_rpool);
		return EX_NOUSER;
	}
	else if (REPLYTYPE(r) == 5)
	{
		return EX_UNAVAILABLE;
	}

	if (LogLevel > 1)
	{
		sm_syslog(LOG_CRIT, e->e_id,
			  "%.100s: SMTP RCPT protocol error: %s",
			  CurHostName,
			  shortenstring(SmtpReplyBuffer, 403));
	}

	mci_setstat(mci, EX_PROTOCOL, ENHSCN(enhsc, "5.5.1"),
		    SmtpReplyBuffer);
	return EX_PROTOCOL;
}
/*
**  SMTPDATA -- send the data and clean up the transaction.
**
**	Parameters:
**		m -- mailer being sent to.
**		mci -- the mailer connection information.
**		e -- the envelope for this message.
**
**	Returns:
**		exit status corresponding to DATA command.
*/

int
smtpdata(m, mci, e, ctladdr, xstart)
	MAILER *m;
	register MCI *mci;
	register ENVELOPE *e;
	ADDRESS *ctladdr;
	time_t xstart;
{
	register int r;
	int rstat;
	int xstat;
	int timeout;
	char *enhsc;

	/*
	**  Check if connection is gone, if so
	**  it's a tempfail and we use mci_errno
	**  for the reason.
	*/

	if (mci->mci_state == MCIS_CLOSED)
	{
		errno = mci->mci_errno;
		return EX_TEMPFAIL;
	}

	enhsc = NULL;

	/*
	**  Send the data.
	**	First send the command and check that it is ok.
	**	Then send the data (if there are valid recipients).
	**	Follow it up with a dot to terminate.
	**	Finally get the results of the transaction.
	*/

	/* send the command and check ok to proceed */
	smtpmessage("DATA", m, mci);

#if PIPELINING
	if (mci->mci_nextaddr != NULL)
	{
		char *oldto = e->e_to;

		/* pick up any pending RCPT responses for SMTP pipelining */
		while (mci->mci_nextaddr != NULL)
		{
			int r;

			e->e_to = mci->mci_nextaddr->q_paddr;
			r = smtprcptstat(mci->mci_nextaddr, m, mci, e);
			if (r != EX_OK)
			{
				markfailure(e, mci->mci_nextaddr, mci, r,
					    false);
				giveresponse(r, mci->mci_nextaddr->q_status, m,
					     mci, ctladdr, xstart, e,
					     mci->mci_nextaddr);
				if (r == EX_TEMPFAIL)
					mci->mci_nextaddr->q_state = QS_RETRY;
			}
			mci->mci_nextaddr = mci->mci_nextaddr->q_pchain;
		}
		e->e_to = oldto;

		/*
		**  Connection might be closed in response to a RCPT command,
		**  i.e., the server responded with 421. In that case (at
		**  least) one RCPT has a temporary failure, hence we don't
		**  need to check mci_okrcpts (as it is done below) to figure
		**  out which error to return.
		*/

		if (mci->mci_state == MCIS_CLOSED)
		{
			errno = mci->mci_errno;
			return EX_TEMPFAIL;
		}
	}
#endif /* PIPELINING */

	/* now proceed with DATA phase */
	SmtpPhase = mci->mci_phase = "client DATA 354";
	mci->mci_state = MCIS_DATA;
	sm_setproctitle(true, e, "%s %s: %s",
			qid_printname(e), CurHostName, mci->mci_phase);
	r = reply(m, mci, e, TimeOuts.to_datainit, NULL, &enhsc, XS_DATA);
	if (r < 0 || REPLYTYPE(r) == 4)
	{
		if (r >= 0)
			smtpquit(m, mci, e);
		errno = mci->mci_errno;
		return EX_TEMPFAIL;
	}
	else if (REPLYTYPE(r) == 5)
	{
		smtprset(m, mci, e);
#if PIPELINING
		if (mci->mci_okrcpts <= 0)
			return mci->mci_retryrcpt ? EX_TEMPFAIL
						  : EX_UNAVAILABLE;
#endif /* PIPELINING */
		return EX_UNAVAILABLE;
	}
	else if (REPLYTYPE(r) != 3)
	{
		if (LogLevel > 1)
		{
			sm_syslog(LOG_CRIT, e->e_id,
				  "%.100s: SMTP DATA-1 protocol error: %s",
				  CurHostName,
				  shortenstring(SmtpReplyBuffer, 403));
		}
		smtprset(m, mci, e);
		mci_setstat(mci, EX_PROTOCOL, ENHSCN(enhsc, "5.5.1"),
			    SmtpReplyBuffer);
#if PIPELINING
		if (mci->mci_okrcpts <= 0)
			return mci->mci_retryrcpt ? EX_TEMPFAIL
						  : EX_PROTOCOL;
#endif /* PIPELINING */
		return EX_PROTOCOL;
	}

#if PIPELINING
	if (mci->mci_okrcpts > 0)
	{
#endif /* PIPELINING */

	/*
	**  Set timeout around data writes.  Make it at least large
	**  enough for DNS timeouts on all recipients plus some fudge
	**  factor.  The main thing is that it should not be infinite.
	*/

	if (tTd(18, 101))
	{
		/* simulate a DATA timeout */
		timeout = 10;
	}
	else
		timeout = DATA_PROGRESS_TIMEOUT * 1000;
	sm_io_setinfo(mci->mci_out, SM_IO_WHAT_TIMEOUT, &timeout);


	/*
	**  Output the actual message.
	*/

	if (!(*e->e_puthdr)(mci, e->e_header, e, M87F_OUTER))
		goto writeerr;

	if (tTd(18, 101))
	{
		/* simulate a DATA timeout */
		(void) sleep(2);
	}

	if (!(*e->e_putbody)(mci, e, NULL))
		goto writeerr;

	/*
	**  Cleanup after sending message.
	*/


#if PIPELINING
	}
#endif /* PIPELINING */

#if _FFR_CATCH_BROKEN_MTAS
	if (sm_io_getinfo(mci->mci_in, SM_IO_IS_READABLE, NULL) > 0)
	{
		/* terminate the message */
		(void) sm_io_fprintf(mci->mci_out, SM_TIME_DEFAULT, ".%s",
				     m->m_eol);
		if (TrafficLogFile != NULL)
			(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
					     "%05d >>> .\n", (int) CurrentPid);
		if (Verbose)
			nmessage(">>> .");

		sm_syslog(LOG_CRIT, e->e_id,
			  "%.100s: SMTP DATA-1 protocol error: remote server returned response before final dot",
			  CurHostName);
		mci->mci_errno = EIO;
		mci->mci_state = MCIS_ERROR;
		mci_setstat(mci, EX_PROTOCOL, "5.5.0", NULL);
		smtpquit(m, mci, e);
		return EX_PROTOCOL;
	}
#endif /* _FFR_CATCH_BROKEN_MTAS */

	if (sm_io_error(mci->mci_out))
	{
		/* error during processing -- don't send the dot */
		mci->mci_errno = EIO;
		mci->mci_state = MCIS_ERROR;
		mci_setstat(mci, EX_IOERR, "4.4.2", NULL);
		smtpquit(m, mci, e);
		return EX_IOERR;
	}

	/* terminate the message */
	if (sm_io_fprintf(mci->mci_out, SM_TIME_DEFAULT, "%s.%s",
			bitset(MCIF_INLONGLINE, mci->mci_flags) ? m->m_eol : "",
			m->m_eol) == SM_IO_EOF)
		goto writeerr;
	if (TrafficLogFile != NULL)
		(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
				     "%05d >>> .\n", (int) CurrentPid);
	if (Verbose)
		nmessage(">>> .");

	/* check for the results of the transaction */
	SmtpPhase = mci->mci_phase = "client DATA status";
	sm_setproctitle(true, e, "%s %s: %s", qid_printname(e),
			CurHostName, mci->mci_phase);
	if (bitnset(M_LMTP, m->m_flags))
		return EX_OK;
	r = reply(m, mci, e, TimeOuts.to_datafinal, NULL, &enhsc, XS_EOM);
	if (r < 0)
		return EX_TEMPFAIL;
	if (mci->mci_state == MCIS_DATA)
		mci->mci_state = MCIS_OPEN;
	xstat = EX_NOTSTICKY;
	if (r == 452)
		rstat = EX_TEMPFAIL;
	else if (REPLYTYPE(r) == 4)
		rstat = xstat = EX_TEMPFAIL;
	else if (REPLYTYPE(r) == 2)
		rstat = xstat = EX_OK;
	else if (REPLYCLASS(r) != 5)
		rstat = xstat = EX_PROTOCOL;
	else if (REPLYTYPE(r) == 5)
		rstat = EX_UNAVAILABLE;
	else
		rstat = EX_PROTOCOL;
	mci_setstat(mci, xstat, ENHSCN(enhsc, smtptodsn(r)),
		    SmtpReplyBuffer);
	if (bitset(MCIF_ENHSTAT, mci->mci_flags) &&
	    (r = isenhsc(SmtpReplyBuffer + 4, ' ')) > 0)
		r += 5;
	else
		r = 4;
	e->e_statmsg = sm_rpool_strdup_x(e->e_rpool, &SmtpReplyBuffer[r]);
	SmtpPhase = mci->mci_phase = "idle";
	sm_setproctitle(true, e, "%s: %s", CurHostName, mci->mci_phase);
	if (rstat != EX_PROTOCOL)
		return rstat;
	if (LogLevel > 1)
	{
		sm_syslog(LOG_CRIT, e->e_id,
			  "%.100s: SMTP DATA-2 protocol error: %s",
			  CurHostName,
			  shortenstring(SmtpReplyBuffer, 403));
	}
	return rstat;

  writeerr:
	mci->mci_errno = errno;
	mci->mci_state = MCIS_ERROR;
	mci_setstat(mci, bitset(MCIF_NOTSTICKY, mci->mci_flags)
			 ? EX_NOTSTICKY: EX_TEMPFAIL,
		    "4.4.2", NULL);
	mci->mci_flags &= ~MCIF_NOTSTICKY;

	/*
	**  If putbody() couldn't finish due to a timeout,
	**  rewind it here in the timeout handler.  See
	**  comments at the end of putbody() for reasoning.
	*/

	if (e->e_dfp != NULL)
		(void) bfrewind(e->e_dfp);

	errno = mci->mci_errno;
	syserr("+451 4.4.1 timeout writing message to %s", CurHostName);
	smtpquit(m, mci, e);
	return EX_TEMPFAIL;
}

/*
**  SMTPGETSTAT -- get status code from DATA in LMTP
**
**	Parameters:
**		m -- the mailer to which we are sending the message.
**		mci -- the mailer connection structure.
**		e -- the current envelope.
**
**	Returns:
**		The exit status corresponding to the reply code.
*/

int
smtpgetstat(m, mci, e)
	MAILER *m;
	MCI *mci;
	ENVELOPE *e;
{
	int r;
	int off;
	int status, xstat;
	char *enhsc;

	enhsc = NULL;

	/* check for the results of the transaction */
	r = reply(m, mci, e, TimeOuts.to_datafinal, NULL, &enhsc, XS_DATA2);
	if (r < 0)
		return EX_TEMPFAIL;
	xstat = EX_NOTSTICKY;
	if (REPLYTYPE(r) == 4)
		status = EX_TEMPFAIL;
	else if (REPLYTYPE(r) == 2)
		status = xstat = EX_OK;
	else if (REPLYCLASS(r) != 5)
		status = xstat = EX_PROTOCOL;
	else if (REPLYTYPE(r) == 5)
		status = EX_UNAVAILABLE;
	else
		status = EX_PROTOCOL;
	if (bitset(MCIF_ENHSTAT, mci->mci_flags) &&
	    (off = isenhsc(SmtpReplyBuffer + 4, ' ')) > 0)
		off += 5;
	else
		off = 4;
	e->e_statmsg = sm_rpool_strdup_x(e->e_rpool, &SmtpReplyBuffer[off]);
	mci_setstat(mci, xstat, ENHSCN(enhsc, smtptodsn(r)), SmtpReplyBuffer);
	if (LogLevel > 1 && status == EX_PROTOCOL)
	{
		sm_syslog(LOG_CRIT, e->e_id,
			  "%.100s: SMTP DATA-3 protocol error: %s",
			  CurHostName,
			  shortenstring(SmtpReplyBuffer, 403));
	}
	return status;
}
/*
**  SMTPQUIT -- close the SMTP connection.
**
**	Parameters:
**		m -- a pointer to the mailer.
**		mci -- the mailer connection information.
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sends the final protocol and closes the connection.
*/

void
smtpquit(m, mci, e)
	register MAILER *m;
	register MCI *mci;
	ENVELOPE *e;
{
	bool oldSuprErrs = SuprErrs;
	int rcode;
	char *oldcurhost;

	if (mci->mci_state == MCIS_CLOSED)
	{
		mci_close(mci, "smtpquit:1");
		return;
	}

	oldcurhost = CurHostName;
	CurHostName = mci->mci_host;		/* XXX UGLY XXX */
	if (CurHostName == NULL)
		CurHostName = MyHostName;

#if PIPELINING
	mci->mci_okrcpts = 0;
#endif /* PIPELINING */

	/*
	**	Suppress errors here -- we may be processing a different
	**	job when we do the quit connection, and we don't want the
	**	new job to be penalized for something that isn't it's
	**	problem.
	*/

	SuprErrs = true;

	/* send the quit message if we haven't gotten I/O error */
	if (mci->mci_state != MCIS_ERROR &&
	    mci->mci_state != MCIS_QUITING)
	{
		SmtpPhase = "client QUIT";
		mci->mci_state = MCIS_QUITING;
		smtpmessage("QUIT", m, mci);
		(void) reply(m, mci, e, TimeOuts.to_quit, NULL, NULL, XS_QUIT);
		SuprErrs = oldSuprErrs;
		if (mci->mci_state == MCIS_CLOSED)
			goto end;
	}

	/* now actually close the connection and pick up the zombie */
	rcode = endmailer(mci, e, NULL);
	if (rcode != EX_OK)
	{
		char *mailer = NULL;

		if (mci->mci_mailer != NULL &&
		    mci->mci_mailer->m_name != NULL)
			mailer = mci->mci_mailer->m_name;

		/* look for naughty mailers */
		sm_syslog(LOG_ERR, e->e_id,
			  "smtpquit: mailer%s%s exited with exit value %d",
			  mailer == NULL ? "" : " ",
			  mailer == NULL ? "" : mailer,
			  rcode);
	}

	SuprErrs = oldSuprErrs;

  end:
	CurHostName = oldcurhost;
	return;
}
/*
**  SMTPRSET -- send a RSET (reset) command
**
**	Parameters:
**		m -- a pointer to the mailer.
**		mci -- the mailer connection information.
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		closes the connection if there is no reply to RSET.
*/

void
smtprset(m, mci, e)
	register MAILER *m;
	register MCI *mci;
	ENVELOPE *e;
{
	int r;

	CurHostName = mci->mci_host;		/* XXX UGLY XXX */
	if (CurHostName == NULL)
		CurHostName = MyHostName;

#if PIPELINING
	mci->mci_okrcpts = 0;
#endif /* PIPELINING */

	/*
	**  Check if connection is gone, if so
	**  it's a tempfail and we use mci_errno
	**  for the reason.
	*/

	if (mci->mci_state == MCIS_CLOSED)
	{
		errno = mci->mci_errno;
		return;
	}

	SmtpPhase = "client RSET";
	smtpmessage("RSET", m, mci);
	r = reply(m, mci, e, TimeOuts.to_rset, NULL, NULL, XS_DEFAULT);
	if (r < 0)
		return;

	/*
	**  Any response is deemed to be acceptable.
	**  The standard does not state the proper action
	**  to take when a value other than 250 is received.
	**
	**  However, if 421 is returned for the RSET, leave
	**  mci_state alone (MCIS_SSD can be set in reply()
	**  and MCIS_CLOSED can be set in smtpquit() if
	**  reply() gets a 421 and calls smtpquit()).
	*/

	if (mci->mci_state != MCIS_SSD && mci->mci_state != MCIS_CLOSED)
		mci->mci_state = MCIS_OPEN;
	else if (mci->mci_exitstat == EX_OK)
		mci_setstat(mci, EX_TEMPFAIL, "4.5.0", NULL);
}
/*
**  SMTPPROBE -- check the connection state
**
**	Parameters:
**		mci -- the mailer connection information.
**
**	Returns:
**		none.
**
**	Side Effects:
**		closes the connection if there is no reply to RSET.
*/

int
smtpprobe(mci)
	register MCI *mci;
{
	int r;
	MAILER *m = mci->mci_mailer;
	ENVELOPE *e;
	extern ENVELOPE BlankEnvelope;

	CurHostName = mci->mci_host;		/* XXX UGLY XXX */
	if (CurHostName == NULL)
		CurHostName = MyHostName;

	e = &BlankEnvelope;
	SmtpPhase = "client probe";
	smtpmessage("RSET", m, mci);
	r = reply(m, mci, e, TimeOuts.to_miscshort, NULL, NULL, XS_DEFAULT);
	if (REPLYTYPE(r) != 2)
		smtpquit(m, mci, e);
	return r;
}
/*
**  REPLY -- read arpanet reply
**
**	Parameters:
**		m -- the mailer we are reading the reply from.
**		mci -- the mailer connection info structure.
**		e -- the current envelope.
**		timeout -- the timeout for reads.
**		pfunc -- processing function called on each line of response.
**			If null, no special processing is done.
**		enhstat -- optional, returns enhanced error code string (if set)
**		rtype -- type of SmtpMsgBuffer: does it contains secret data?
**
**	Returns:
**		reply code it reads.
**
**	Side Effects:
**		flushes the mail file.
*/

int
reply(m, mci, e, timeout, pfunc, enhstat, rtype)
	MAILER *m;
	MCI *mci;
	ENVELOPE *e;
	time_t timeout;
	void (*pfunc) __P((char *, bool, MAILER *, MCI *, ENVELOPE *));
	char **enhstat;
	int rtype;
{
	register char *bufp;
	register int r;
	bool firstline = true;
	char junkbuf[MAXLINE];
	static char enhstatcode[ENHSCLEN];
	int save_errno;

	/*
	**  Flush the output before reading response.
	**
	**	For SMTP pipelining, it would be better if we didn't do
	**	this if there was already data waiting to be read.  But
	**	to do it properly means pushing it to the I/O library,
	**	since it really needs to be done below the buffer layer.
	*/

	if (mci->mci_out != NULL)
		(void) sm_io_flush(mci->mci_out, SM_TIME_DEFAULT);

	if (tTd(18, 1))
		sm_dprintf("reply\n");

	/*
	**  Read the input line, being careful not to hang.
	*/

	bufp = SmtpReplyBuffer;
	(void) set_tls_rd_tmo(timeout);
	for (;;)
	{
		register char *p;

		/* actually do the read */
		if (e->e_xfp != NULL)	/* for debugging */
			(void) sm_io_flush(e->e_xfp, SM_TIME_DEFAULT);

		/* if we are in the process of closing just give the code */
		if (mci->mci_state == MCIS_CLOSED)
			return SMTPCLOSING;

		/* don't try to read from a non-existent fd */
		if (mci->mci_in == NULL)
		{
			if (mci->mci_errno == 0)
				mci->mci_errno = EBADF;

			/* errors on QUIT should be ignored */
			if (strncmp(SmtpMsgBuffer, "QUIT", 4) == 0)
			{
				errno = mci->mci_errno;
				mci_close(mci, "reply:1");
				return -1;
			}
			mci->mci_state = MCIS_ERROR;
			smtpquit(m, mci, e);
			errno = mci->mci_errno;
			return -1;
		}

		if (mci->mci_out != NULL)
			(void) sm_io_flush(mci->mci_out, SM_TIME_DEFAULT);

		/* get the line from the other side */
		p = sfgets(bufp, MAXLINE, mci->mci_in, timeout, SmtpPhase);
		save_errno = errno;
		mci->mci_lastuse = curtime();

		if (p == NULL)
		{
			bool oldholderrs;
			extern char MsgBuf[];

			/* errors on QUIT should be ignored */
			if (strncmp(SmtpMsgBuffer, "QUIT", 4) == 0)
			{
				mci_close(mci, "reply:2");
				return -1;
			}

			/* if the remote end closed early, fake an error */
			errno = save_errno;
			if (errno == 0)
			{
				(void) sm_snprintf(SmtpReplyBuffer,
						   sizeof(SmtpReplyBuffer),
						   "421 4.4.1 Connection reset by %s",
						   CURHOSTNAME);
#ifdef ECONNRESET
				errno = ECONNRESET;
#else /* ECONNRESET */
				errno = EPIPE;
#endif /* ECONNRESET */
			}

			mci->mci_errno = errno;
			oldholderrs = HoldErrs;
			HoldErrs = true;
			usrerr("451 4.4.1 reply: read error from %s",
			       CURHOSTNAME);
			mci_setstat(mci, EX_TEMPFAIL, "4.4.2", MsgBuf);

			/* if debugging, pause so we can see state */
			if (tTd(18, 100))
				(void) pause();
			mci->mci_state = MCIS_ERROR;
			smtpquit(m, mci, e);
#if XDEBUG
			{
				char wbuf[MAXLINE];

				p = wbuf;
				if (e->e_to != NULL)
				{
					(void) sm_snprintf(p,
							   SPACELEFT(wbuf, p),
							   "%s... ",
							   shortenstring(e->e_to, MAXSHORTSTR));
					p += strlen(p);
				}
				(void) sm_snprintf(p, SPACELEFT(wbuf, p),
						   "reply(%.100s) during %s",
						   CURHOSTNAME, SmtpPhase);
				checkfd012(wbuf);
			}
#endif /* XDEBUG */
			HoldErrs = oldholderrs;
			errno = save_errno;
			return -1;
		}
		fixcrlf(bufp, true);

		/* EHLO failure is not a real error */
		if (e->e_xfp != NULL && (bufp[0] == '4' ||
		    (bufp[0] == '5' && strncmp(SmtpMsgBuffer, "EHLO", 4) != 0)))
		{
			/* serious error -- log the previous command */
			if (SmtpNeedIntro)
			{
				/* inform user who we are chatting with */
				(void) sm_io_fprintf(CurEnv->e_xfp,
						     SM_TIME_DEFAULT,
						     "... while talking to %s:\n",
						     CURHOSTNAME);
				SmtpNeedIntro = false;
			}
			if (SmtpMsgBuffer[0] != '\0')
			{
				(void) sm_io_fprintf(e->e_xfp,
					SM_TIME_DEFAULT,
					">>> %s\n",
					(rtype == XS_STARTTLS)
					? "STARTTLS dialogue"
					: ((rtype == XS_AUTH)
					   ? "AUTH dialogue"
					   : SmtpMsgBuffer));
				SmtpMsgBuffer[0] = '\0';
			}

			/* now log the message as from the other side */
			(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
					     "<<< %s\n", bufp);
		}

		/* display the input for verbose mode */
		if (Verbose)
			nmessage("050 %s", bufp);

		/* ignore improperly formatted input */
		if (!ISSMTPREPLY(bufp))
			continue;

		if (bitset(MCIF_ENHSTAT, mci->mci_flags) &&
		    enhstat != NULL &&
		    extenhsc(bufp + 4, ' ', enhstatcode) > 0)
			*enhstat = enhstatcode;

		/* process the line */
		if (pfunc != NULL)
			(*pfunc)(bufp, firstline, m, mci, e);

		/* decode the reply code */
		r = atoi(bufp);

		/* extra semantics: 0xx codes are "informational" */
		if (r < 100)
		{
			firstline = false;
			continue;
		}
#if _FFR_ERRCODE
# if _FFR_PROXY
		if ((e->e_rcode == 0 || REPLYTYPE(e->e_rcode) < 5)
		    && REPLYTYPE(r) > 3 && firstline)
# endif
# if _FFR_LOGREPLY
		if (REPLYTYPE(r) > 3 && firstline)
# endif
		{
			int o = -1;
# if PIPELINING
			/*
			**  ignore error iff: DATA, 5xy error, but we had
			**  "retryable" recipients. XREF: smtpdata()
			*/

			if (!(rtype == XS_DATA && REPLYTYPE(r) == 5 &&
			      mci->mci_okrcpts <= 0 && mci->mci_retryrcpt))
# endif /* PIPELINING */
			{
				o = extenhsc(bufp + 4, ' ', enhstatcode);
				if (o > 0)
				{
					sm_strlcpy(e->e_renhsc, enhstatcode,
						sizeof(e->e_renhsc));

					/* skip SMTP reply code, delimiters */
					o += 5;
				}
				else
					o = 4;
				e->e_rcode = r;
				e->e_text = sm_rpool_strdup_x(e->e_rpool,
							      bufp + o);
			}
			if (tTd(87, 2))
			{
				sm_dprintf("user: offset=%d, bufp=%s, rcode=%d, enhstat=%s, text=%s\n",
					o, bufp, r, e->e_renhsc, e->e_text);
			}
		}
#endif /* _FFR_ERRCODE */

		firstline = false;

		/* if no continuation lines, return this line */
		if (bufp[3] != '-')
			break;

		/* first line of real reply -- ignore rest */
		bufp = junkbuf;
	}

	/*
	**  Now look at SmtpReplyBuffer -- only care about the first
	**  line of the response from here on out.
	*/

	/* save temporary failure messages for posterity */
	if (SmtpReplyBuffer[0] == '4')
		(void) sm_strlcpy(SmtpError, SmtpReplyBuffer, sizeof(SmtpError));

	/* reply code 421 is "Service Shutting Down" */
	if (r == SMTPCLOSING && mci->mci_state != MCIS_SSD &&
	    mci->mci_state != MCIS_QUITING)
	{
		/* send the quit protocol */
		mci->mci_state = MCIS_SSD;
		smtpquit(m, mci, e);
	}

	return r;
}
/*
**  SMTPMESSAGE -- send message to server
**
**	Parameters:
**		f -- format
**		m -- the mailer to control formatting.
**		a, b, c -- parameters
**
**	Returns:
**		none.
**
**	Side Effects:
**		writes message to mci->mci_out.
*/

/*VARARGS1*/
void
#ifdef __STDC__
smtpmessage(char *f, MAILER *m, MCI *mci, ...)
#else /* __STDC__ */
smtpmessage(f, m, mci, va_alist)
	char *f;
	MAILER *m;
	MCI *mci;
	va_dcl
#endif /* __STDC__ */
{
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, mci);
	(void) sm_vsnprintf(SmtpMsgBuffer, sizeof(SmtpMsgBuffer), f, ap);
	SM_VA_END(ap);

	if (tTd(18, 1) || Verbose)
		nmessage(">>> %s", SmtpMsgBuffer);
	if (TrafficLogFile != NULL)
		(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
				     "%05d >>> %s\n", (int) CurrentPid,
				     SmtpMsgBuffer);
	if (mci->mci_out != NULL)
	{
		(void) sm_io_fprintf(mci->mci_out, SM_TIME_DEFAULT, "%s%s",
				     SmtpMsgBuffer, m == NULL ? "\r\n"
							      : m->m_eol);
	}
	else if (tTd(18, 1))
	{
		sm_dprintf("smtpmessage: NULL mci_out\n");
	}
}
