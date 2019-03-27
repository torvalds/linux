/*
 * Copyright (c) 1999-2009, 2012, 2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: milter.c,v 8.281 2013-11-22 20:51:56 ca Exp $")

#if MILTER
# include <sm/sendmail.h>
# include <libmilter/mfapi.h>
# include <libmilter/mfdef.h>

# include <errno.h>
# include <sm/time.h>
# include <sys/uio.h>

# if NETINET || NETINET6
#  include <arpa/inet.h>
#  if MILTER_NO_NAGLE
#   include <netinet/tcp.h>
#  endif /* MILTER_NO_NAGLE */
# endif /* NETINET || NETINET6 */

# include <sm/fdset.h>

static void	milter_connect_timeout __P((int));
static void	milter_error __P((struct milter *, ENVELOPE *));
static int	milter_open __P((struct milter *, bool, ENVELOPE *));
static void	milter_parse_timeouts __P((char *, struct milter *));
static char	*milter_sysread __P((struct milter *, char *, ssize_t, time_t,
			ENVELOPE *, const char *));
static char	*milter_read __P((struct milter *, char *, ssize_t *, time_t,
			ENVELOPE *, const char *));
static char	*milter_write __P((struct milter *, int, char *, ssize_t,
			time_t, ENVELOPE *, const char *));
static char	*milter_send_command __P((struct milter *, int, void *,
			ssize_t, ENVELOPE *, char *, const char *));
static char	*milter_command __P((int, void *, ssize_t, int,
			ENVELOPE *, char *, const char *, bool));
static char	*milter_body __P((struct milter *, ENVELOPE *, char *));
static int	milter_reopen_df __P((ENVELOPE *));
static int	milter_reset_df __P((ENVELOPE *));
static void	milter_quit_filter __P((struct milter *, ENVELOPE *));
static void	milter_abort_filter __P((struct milter *, ENVELOPE *));
static void	milter_send_macros __P((struct milter *, char **, int,
			ENVELOPE *));
static int	milter_negotiate __P((struct milter *, ENVELOPE *,
			milters_T *));
static void	milter_per_connection_check __P((ENVELOPE *));
static char	*milter_headers __P((struct milter *, ENVELOPE *, char *));
static void	milter_addheader __P((struct milter *, char *, ssize_t,
			ENVELOPE *));
static void	milter_insheader __P((struct milter *, char *, ssize_t,
			ENVELOPE *));
static void	milter_changeheader __P((struct milter *, char *, ssize_t,
			ENVELOPE *));
static void	milter_chgfrom __P((char *, ssize_t, ENVELOPE *));
static void	milter_addrcpt __P((char *, ssize_t, ENVELOPE *));
static void	milter_addrcpt_par __P((char *, ssize_t, ENVELOPE *));
static void	milter_delrcpt __P((char *, ssize_t, ENVELOPE *));
static int	milter_replbody __P((char *, ssize_t, bool, ENVELOPE *));
static int	milter_set_macros __P((char *, char **, char *, int));


/* milter states */
# define SMFS_CLOSED		'C'	/* closed for all further actions */
# define SMFS_OPEN		'O'	/* connected to remote milter filter */
# define SMFS_INMSG		'M'	/* currently servicing a message */
# define SMFS_DONE		'D'	/* done with current message */
# define SMFS_CLOSABLE		'Q'	/* done with current connection */
# define SMFS_ERROR		'E'	/* error state */
# define SMFS_READY		'R'	/* ready for action */
# define SMFS_SKIP		'S'	/* skip body */

/*
**  MilterMacros contains the milter macros for each milter and each stage.
**  indices are (in order): stages, milter-index, macro
**  milter-index == 0: "global" macros (not for a specific milter).
*/

static char *MilterMacros[SMFIM_LAST + 1][MAXFILTERS + 1][MAXFILTERMACROS + 1];
static size_t MilterMaxDataSize = MILTER_MAX_DATA_SIZE;

# define MILTER_CHECK_DONE_MSG() \
	if (*state == SMFIR_REPLYCODE || \
	    *state == SMFIR_REJECT || \
	    *state == SMFIR_DISCARD || \
	    *state == SMFIR_TEMPFAIL) \
	{ \
		/* Abort the filters to let them know we are done with msg */ \
		milter_abort(e); \
	}

/* set state in case of an error */
# define MILTER_SET_STATE	\
	if (bitnset(SMF_TEMPFAIL, m->mf_flags)) \
		*state = SMFIR_TEMPFAIL; \
	else if (bitnset(SMF_TEMPDROP, m->mf_flags)) \
		*state = SMFIR_SHUTDOWN; \
	else if (bitnset(SMF_REJECT, m->mf_flags)) \
		*state = SMFIR_REJECT

/* flow through code maybe using continue; don't wrap in do {} while */
# define MILTER_CHECK_ERROR(initial, action) \
	if (!initial && tTd(71, 100)) \
	{ \
		if (e->e_quarmsg == NULL) \
		{ \
			e->e_quarmsg = sm_rpool_strdup_x(e->e_rpool, \
							 "filter failure"); \
			macdefine(&e->e_macro, A_PERM, macid("{quarantine}"), \
				  e->e_quarmsg); \
		} \
	} \
	else if (tTd(71, 101)) \
	{ \
		if (e->e_quarmsg == NULL) \
		{ \
			e->e_quarmsg = sm_rpool_strdup_x(e->e_rpool, \
							 "filter failure"); \
			macdefine(&e->e_macro, A_PERM, macid("{quarantine}"), \
				  e->e_quarmsg); \
		} \
	} \
	else MILTER_SET_STATE;	\
	else \
		action;

# define MILTER_CHECK_REPLYCODE(default) \
	if (response == NULL || \
	    strlen(response) + 1 != (size_t) rlen || \
	    rlen < 3 || \
	    (response[0] != '4' && response[0] != '5') || \
	    !isascii(response[1]) || !isdigit(response[1]) || \
	    !isascii(response[2]) || !isdigit(response[2])) \
	{ \
		if (response != NULL) \
			sm_free(response); /* XXX */ \
		response = newstr(default); \
	} \
	else \
	{ \
		char *ptr = response; \
 \
		/* Check for unprotected %'s in the string */ \
		while (*ptr != '\0') \
		{ \
			if (*ptr == '%' && *++ptr != '%') \
			{ \
				sm_free(response); /* XXX */ \
				response = newstr(default); \
				break; \
			} \
			ptr++; \
		} \
	}

# define MILTER_DF_ERROR(msg) \
{ \
	int save_errno = errno; \
 \
	if (tTd(64, 5)) \
	{ \
		sm_dprintf(msg, dfname, sm_errstring(save_errno)); \
		sm_dprintf("\n"); \
	} \
	if (MilterLogLevel > 0) \
		sm_syslog(LOG_ERR, e->e_id, msg, dfname, sm_errstring(save_errno)); \
	if (SuperSafe == SAFE_REALLY) \
	{ \
		if (e->e_dfp != NULL) \
		{ \
			(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT); \
			e->e_dfp = NULL; \
		} \
		e->e_flags &= ~EF_HAS_DF; \
	} \
	errno = save_errno; \
}

/*
**  MILTER_TIMEOUT -- make sure socket is ready in time
**
**	Parameters:
**		routine -- routine name for debug/logging
**		secs -- number of seconds in timeout
**		write -- waiting to read or write?
**		started -- whether this is part of a previous sequence
**
**	Assumes 'm' is a milter structure for the current socket.
*/

# define MILTER_TIMEOUT(routine, secs, write, started, function) \
{ \
	int ret; \
	int save_errno; \
	fd_set fds; \
	struct timeval tv; \
 \
	if (!SM_FD_OK_SELECT(m->mf_sock)) \
	{ \
		if (tTd(64, 5)) \
			sm_dprintf("milter_%s(%s): socket %d is larger than FD_SETSIZE %d\n", \
				   (routine), m->mf_name, m->mf_sock, \
				   SM_FD_SETSIZE); \
		if (MilterLogLevel > 0) \
			sm_syslog(LOG_ERR, e->e_id, \
				  "Milter (%s): socket(%s) %d is larger than FD_SETSIZE %d", \
				  m->mf_name, (routine), m->mf_sock, \
				  SM_FD_SETSIZE); \
		milter_error(m, e); \
		return NULL; \
	} \
 \
	do \
	{ \
		FD_ZERO(&fds); \
		SM_FD_SET(m->mf_sock, &fds); \
		tv.tv_sec = (secs); \
		tv.tv_usec = 0; \
		ret = select(m->mf_sock + 1, \
			     (write) ? NULL : &fds, \
			     (write) ? &fds : NULL, \
			     NULL, &tv); \
	} while (ret < 0 && errno == EINTR); \
 \
	switch (ret) \
	{ \
	  case 0: \
		if (tTd(64, 5)) \
			sm_dprintf("milter_%s(%s): timeout, where=%s\n", \
				(routine), m->mf_name, (function)); \
		if (MilterLogLevel > 0) \
			sm_syslog(LOG_ERR, e->e_id, \
				  "Milter (%s): timeout %s data %s, where=%s", \
				  m->mf_name, \
				  started ? "during" : "before", \
				  (routine), (function)); \
		milter_error(m, e); \
		return NULL; \
 \
	  case -1: \
		save_errno = errno; \
		if (tTd(64, 5)) \
			sm_dprintf("milter_%s(%s): select: %s\n", (routine), \
				   m->mf_name, sm_errstring(save_errno)); \
		if (MilterLogLevel > 0) \
		{ \
			sm_syslog(LOG_ERR, e->e_id, \
				  "Milter (%s): select(%s): %s", \
				  m->mf_name, (routine), \
				  sm_errstring(save_errno)); \
		} \
		milter_error(m, e); \
		return NULL; \
 \
	  default: \
		if (SM_FD_ISSET(m->mf_sock, &fds)) \
			break; \
		if (tTd(64, 5)) \
			sm_dprintf("milter_%s(%s): socket not ready\n", \
				(routine), m->mf_name); \
		if (MilterLogLevel > 0) \
		{ \
			sm_syslog(LOG_ERR, e->e_id, \
				  "Milter (%s): socket(%s) not ready", \
				  m->mf_name, (routine)); \
		} \
		milter_error(m, e); \
		return NULL; \
	} \
}

/*
**  Low level functions
*/

/*
**  MILTER_READ -- read from a remote milter filter
**
**	Parameters:
**		m -- milter to read from.
**		cmd -- return param for command read.
**		rlen -- return length of response string.
**		to -- timeout in seconds.
**		e -- current envelope.
**
**	Returns:
**		response string (may be NULL)
*/

static char *
milter_sysread(m, buf, sz, to, e, where)
	struct milter *m;
	char *buf;
	ssize_t sz;
	time_t to;
	ENVELOPE *e;
	const char *where;
{
	time_t readstart = 0;
	ssize_t len, curl;
	bool started = false;

	curl = 0;

	if (to > 0)
		readstart = curtime();

	for (;;)
	{
		if (to > 0)
		{
			time_t now;

			now = curtime();
			if (now - readstart >= to)
			{
				if (tTd(64, 5))
					sm_dprintf("milter_sys_read (%s): timeout %s data read in %s",
						  m->mf_name,
						  started ? "during" : "before",
						  where);
				if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "Milter (%s): timeout %s data read in %s",
						  m->mf_name,
						  started ? "during" : "before",
						  where);
				milter_error(m, e);
				return NULL;
			}
			to -= now - readstart;
			readstart = now;
			MILTER_TIMEOUT("read", to, false, started, where);
		}

		len = read(m->mf_sock, buf + curl, sz - curl);

		if (len < 0)
		{
			int save_errno = errno;

			if (tTd(64, 5))
				sm_dprintf("milter_sys_read(%s): read returned %ld: %s\n",
					m->mf_name, (long) len,
					sm_errstring(save_errno));
			if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): read returned %ld: %s",
					  m->mf_name, (long) len,
					  sm_errstring(save_errno));
			milter_error(m, e);
			return NULL;
		}

		started = true;
		curl += len;
		if (len == 0 || curl >= sz)
			break;

	}

	if (curl != sz)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_sys_read(%s): cmd read returned %ld, expecting %ld\n",
				m->mf_name, (long) curl, (long) sz);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "milter_sys_read(%s): cmd read returned %ld, expecting %ld",
				  m->mf_name, (long) curl, (long) sz);
		milter_error(m, e);
		return NULL;
	}
	return buf;
}

static char *
milter_read(m, cmd, rlen, to, e, where)
	struct milter *m;
	char *cmd;
	ssize_t *rlen;
	time_t to;
	ENVELOPE *e;
	const char *where;
{
	time_t readstart = 0;
	ssize_t expl;
	mi_int32 i;
# if MILTER_NO_NAGLE && defined(TCP_CORK)
	int cork = 0;
# endif /* MILTER_NO_NAGLE && defined(TCP_CORK) */
	char *buf;
	char data[MILTER_LEN_BYTES + 1];

	if (m->mf_sock < 0)
	{
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "milter_read(%s): socket closed, where=%s",
				  m->mf_name, where);
		milter_error(m, e);
		return NULL;
	}

	*rlen = 0;
	*cmd = '\0';

	if (to > 0)
		readstart = curtime();

# if MILTER_NO_NAGLE && defined(TCP_CORK)
	setsockopt(m->mf_sock, IPPROTO_TCP, TCP_CORK, (char *)&cork,
		   sizeof(cork));
# endif /* MILTER_NO_NAGLE && defined(TCP_CORK) */

	if (milter_sysread(m, data, sizeof(data), to, e, where) == NULL)
		return NULL;

# if MILTER_NO_NAGLE && defined(TCP_CORK)
	cork = 1;
	setsockopt(m->mf_sock, IPPROTO_TCP, TCP_CORK, (char *)&cork,
		   sizeof(cork));
# endif /* MILTER_NO_NAGLE && defined(TCP_CORK) */

	/* reset timeout */
	if (to > 0)
	{
		time_t now;

		now = curtime();
		if (now - readstart >= to)
		{
			if (tTd(64, 5))
				sm_dprintf("milter_read(%s): timeout before data read, where=%s\n",
					m->mf_name, where);
			if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter read(%s): timeout before data read, where=%s",
					  m->mf_name, where);
			milter_error(m, e);
			return NULL;
		}
		to -= now - readstart;
	}

	*cmd = data[MILTER_LEN_BYTES];
	data[MILTER_LEN_BYTES] = '\0';
	(void) memcpy(&i, data, MILTER_LEN_BYTES);
	expl = ntohl(i) - 1;

	if (tTd(64, 25))
		sm_dprintf("milter_read(%s): expecting %ld bytes\n",
			m->mf_name, (long) expl);

	if (expl < 0)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_read(%s): read size %ld out of range, where=%s\n",
				m->mf_name, (long) expl, where);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "milter_read(%s): read size %ld out of range, where=%s",
				  m->mf_name, (long) expl, where);
		milter_error(m, e);
		return NULL;
	}

	if (expl == 0)
		return NULL;

	buf = (char *) xalloc(expl);

	if (milter_sysread(m, buf, expl, to, e, where) == NULL)
	{
		sm_free(buf); /* XXX */
		return NULL;
	}

	if (tTd(64, 50))
		sm_dprintf("milter_read(%s): Returning %*s\n",
			m->mf_name, (int) expl, buf);
	*rlen = expl;
	return buf;
}

/*
**  MILTER_WRITE -- write to a remote milter filter
**
**	Parameters:
**		m -- milter to read from.
**		cmd -- command to send.
**		buf -- optional command data.
**		len -- length of buf.
**		to -- timeout in seconds.
**		e -- current envelope.
**
**	Returns:
**		buf if successful, NULL otherwise
**		Not actually used anywhere but function prototype
**			must match milter_read()
*/

static char *
milter_write(m, cmd, buf, len, to, e, where)
	struct milter *m;
	int cmd;
	char *buf;
	ssize_t len;
	time_t to;
	ENVELOPE *e;
	const char *where;
{
	ssize_t sl, i;
	int num_vectors;
	mi_int32 nl;
	char command = (char) cmd;
	char data[MILTER_LEN_BYTES + 1];
	bool started = false;
	struct iovec vector[2];

	/*
	**  At most two buffers will be written, though
	**  only one may actually be used (see num_vectors).
	**  The first is the size/command and the second is the command data.
	*/

	if (len < 0 || len > MilterMaxDataSize)
	{
		if (tTd(64, 5))
		{
			sm_dprintf("milter_write(%s): length %ld out of range, cmd=%c\n",
				m->mf_name, (long) len, command);
			sm_dprintf("milter_write(%s): buf=%s\n",
				m->mf_name, str2prt(buf));
		}
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "milter_write(%s): length %ld out of range, cmd=%c",
				  m->mf_name, (long) len, command);
		milter_error(m, e);
		return NULL;
	}
	if (m->mf_sock < 0)
	{
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "milter_write(%s): socket closed",
				  m->mf_name);
		milter_error(m, e);
		return NULL;
	}

	if (tTd(64, 20))
		sm_dprintf("milter_write(%s): cmd %c, len %ld\n",
			   m->mf_name, command, (long) len);

	nl = htonl(len + 1);	/* add 1 for the command char */
	(void) memcpy(data, (char *) &nl, MILTER_LEN_BYTES);
	data[MILTER_LEN_BYTES] = command;
	sl = MILTER_LEN_BYTES + 1;

	/* set up the vector for the size / command */
	vector[0].iov_base = (void *) data;
	vector[0].iov_len  = sl;

	/*
	**  Determine if there is command data.  If so, there will be two
	**  vectors.  If not, there will be only one.  The vectors are set
	**  up here and 'num_vectors' and 'sl' are set appropriately.
	*/

	/* NOTE:  len<0 has already been checked for.  Pedantic */
	if (len <= 0 || buf == NULL)
	{
		/* There is no command data -- only a size / command data */
		num_vectors = 1;
	}
	else
	{
		/*
		**  There is both size / command and command data.
		**  Set up the vector for the command data.
		*/

		num_vectors = 2;
		sl += len;
		vector[1].iov_base = (void *) buf;
		vector[1].iov_len  = len;

		if (tTd(64, 50))
			sm_dprintf("milter_write(%s): Sending %*s\n",
				   m->mf_name, (int) len, buf);
	}

	if (to > 0)
		MILTER_TIMEOUT("write", to, true, started, where);

	/* write the vector(s) */
	i = writev(m->mf_sock, vector, num_vectors);
	if (i != sl)
	{
		int save_errno = errno;

		if (tTd(64, 5))
			sm_dprintf("milter_write(%s): write(%c) returned %ld, expected %ld: %s\n",
				   m->mf_name, command, (long) i, (long) sl,
				   sm_errstring(save_errno));
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): write(%c) returned %ld, expected %ld: %s",
				  m->mf_name, command, (long) i, (long) sl,
				  sm_errstring(save_errno));
		milter_error(m, e);
		return NULL;
	}
	return buf;
}

/*
**  Utility functions
*/

/*
**  MILTER_OPEN -- connect to remote milter filter
**
**	Parameters:
**		m -- milter to connect to.
**		parseonly -- parse but don't connect.
**		e -- current envelope.
**
**	Returns:
**		connected socket if successful && !parseonly,
**		0 upon parse success if parseonly,
**		-1 otherwise.
*/

static jmp_buf	MilterConnectTimeout;

static int
milter_open(m, parseonly, e)
	struct milter *m;
	bool parseonly;
	ENVELOPE *e;
{
	int sock = 0;
	SOCKADDR_LEN_T addrlen = 0;
	int addrno = 0;
	int save_errno;
	char *p;
	char *colon;
	char *at;
	struct hostent *hp = NULL;
	SOCKADDR addr;

	if (m->mf_conn == NULL || m->mf_conn[0] == '\0')
	{
		if (tTd(64, 5))
			sm_dprintf("X%s: empty or missing socket information\n",
				   m->mf_name);
		if (parseonly)
			syserr("X%s: empty or missing socket information",
			       m->mf_name);
		else if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): empty or missing socket information",
				  m->mf_name);
		milter_error(m, e);
		return -1;
	}

	/* protocol:filename or protocol:port@host */
	memset(&addr, '\0', sizeof(addr));
	p = m->mf_conn;
	colon = strchr(p, ':');
	if (colon != NULL)
	{
		*colon = '\0';

		if (*p == '\0')
		{
# if NETUNIX
			/* default to AF_UNIX */
			addr.sa.sa_family = AF_UNIX;
# else /* NETUNIX */
#  if NETINET
			/* default to AF_INET */
			addr.sa.sa_family = AF_INET;
#  else /* NETINET */
#   if NETINET6
			/* default to AF_INET6 */
			addr.sa.sa_family = AF_INET6;
#   else /* NETINET6 */
			/* no protocols available */
			if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): no valid socket protocols available",
					  m->mf_name);
			milter_error(m, e);
			return -1;
#   endif /* NETINET6 */
#  endif /* NETINET */
# endif /* NETUNIX */
		}
# if NETUNIX
		else if (sm_strcasecmp(p, "unix") == 0 ||
			 sm_strcasecmp(p, "local") == 0)
			addr.sa.sa_family = AF_UNIX;
# endif /* NETUNIX */
# if NETINET
		else if (sm_strcasecmp(p, "inet") == 0)
			addr.sa.sa_family = AF_INET;
# endif /* NETINET */
# if NETINET6
		else if (sm_strcasecmp(p, "inet6") == 0)
			addr.sa.sa_family = AF_INET6;
# endif /* NETINET6 */
		else
		{
# ifdef EPROTONOSUPPORT
			errno = EPROTONOSUPPORT;
# else /* EPROTONOSUPPORT */
			errno = EINVAL;
# endif /* EPROTONOSUPPORT */
			if (tTd(64, 5))
				sm_dprintf("X%s: unknown socket type %s\n",
					m->mf_name, p);
			if (parseonly)
				syserr("X%s: unknown socket type %s",
				       m->mf_name, p);
			else if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): unknown socket type %s",
					  m->mf_name, p);
			milter_error(m, e);
			return -1;
		}
		*colon++ = ':';
	}
	else
	{
		/* default to AF_UNIX */
		addr.sa.sa_family = AF_UNIX;
		colon = p;
	}

# if NETUNIX
	if (addr.sa.sa_family == AF_UNIX)
	{
		long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_EXECOK;

		at = colon;
		if (strlen(colon) >= sizeof(addr.sunix.sun_path))
		{
			if (tTd(64, 5))
				sm_dprintf("X%s: local socket name %s too long\n",
					m->mf_name, colon);
			errno = EINVAL;
			if (parseonly)
				syserr("X%s: local socket name %s too long",
				       m->mf_name, colon);
			else if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): local socket name %s too long",
					  m->mf_name, colon);
			milter_error(m, e);
			return -1;
		}
		errno = safefile(colon, RunAsUid, RunAsGid, RunAsUserName, sff,
				 S_IRUSR|S_IWUSR, NULL);

		/* if just parsing .cf file, socket doesn't need to exist */
		if (parseonly && errno == ENOENT)
		{
			if (OpMode == MD_DAEMON ||
			    OpMode == MD_FGDAEMON)
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "WARNING: X%s: local socket name %s missing\n",
						     m->mf_name, colon);
		}
		else if (errno != 0)
		{
			/* if not safe, don't create */
			save_errno = errno;
			if (tTd(64, 5))
				sm_dprintf("X%s: local socket name %s unsafe\n",
					m->mf_name, colon);
			errno = save_errno;
			if (parseonly)
			{
				if (OpMode == MD_DAEMON ||
				    OpMode == MD_FGDAEMON ||
				    OpMode == MD_SMTP)
					syserr("X%s: local socket name %s unsafe",
					       m->mf_name, colon);
			}
			else if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): local socket name %s unsafe",
					  m->mf_name, colon);
			milter_error(m, e);
			return -1;
		}

		(void) sm_strlcpy(addr.sunix.sun_path, colon,
			       sizeof(addr.sunix.sun_path));
		addrlen = sizeof(struct sockaddr_un);
	}
	else
# endif /* NETUNIX */
# if NETINET || NETINET6
	if (false
#  if NETINET
		 || addr.sa.sa_family == AF_INET
#  endif /* NETINET */
#  if NETINET6
		 || addr.sa.sa_family == AF_INET6
#  endif /* NETINET6 */
		 )
	{
		unsigned short port;

		/* Parse port@host */
		at = strchr(colon, '@');
		if (at == NULL)
		{
			if (tTd(64, 5))
				sm_dprintf("X%s: bad address %s (expected port@host)\n",
					m->mf_name, colon);
			if (parseonly)
				syserr("X%s: bad address %s (expected port@host)",
				       m->mf_name, colon);
			else if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): bad address %s (expected port@host)",
					  m->mf_name, colon);
			milter_error(m, e);
			return -1;
		}
		*at = '\0';
		if (isascii(*colon) && isdigit(*colon))
			port = htons((unsigned short) atoi(colon));
		else
		{
#  ifdef NO_GETSERVBYNAME
			if (tTd(64, 5))
				sm_dprintf("X%s: invalid port number %s\n",
					m->mf_name, colon);
			if (parseonly)
				syserr("X%s: invalid port number %s",
				       m->mf_name, colon);
			else if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): invalid port number %s",
					  m->mf_name, colon);
			milter_error(m, e);
			return -1;
#  else /* NO_GETSERVBYNAME */
			struct servent *sp;

			sp = getservbyname(colon, "tcp");
			if (sp == NULL)
			{
				save_errno = errno;
				if (tTd(64, 5))
					sm_dprintf("X%s: unknown port name %s\n",
						m->mf_name, colon);
				errno = save_errno;
				if (parseonly)
					syserr("X%s: unknown port name %s",
					       m->mf_name, colon);
				else if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "Milter (%s): unknown port name %s",
						  m->mf_name, colon);
				milter_error(m, e);
				return -1;
			}
			port = sp->s_port;
#  endif /* NO_GETSERVBYNAME */
		}
		*at++ = '@';
		if (*at == '[')
		{
			char *end;

			end = strchr(at, ']');
			if (end != NULL)
			{
				bool found = false;
#  if NETINET
				unsigned long hid = INADDR_NONE;
#  endif /* NETINET */
#  if NETINET6
				struct sockaddr_in6 hid6;
#  endif /* NETINET6 */

				*end = '\0';
#  if NETINET
				if (addr.sa.sa_family == AF_INET &&
				    (hid = inet_addr(&at[1])) != INADDR_NONE)
				{
					addr.sin.sin_addr.s_addr = hid;
					addr.sin.sin_port = port;
					found = true;
				}
#  endif /* NETINET */
#  if NETINET6
				(void) memset(&hid6, '\0', sizeof(hid6));
				if (addr.sa.sa_family == AF_INET6 &&
				    anynet_pton(AF_INET6, &at[1],
						&hid6.sin6_addr) == 1)
				{
					addr.sin6.sin6_addr = hid6.sin6_addr;
					addr.sin6.sin6_port = port;
					found = true;
				}
#  endif /* NETINET6 */
				*end = ']';
				if (!found)
				{
					if (tTd(64, 5))
						sm_dprintf("X%s: Invalid numeric domain spec \"%s\"\n",
							m->mf_name, at);
					if (parseonly)
						syserr("X%s: Invalid numeric domain spec \"%s\"",
						       m->mf_name, at);
					else if (MilterLogLevel > 0)
						sm_syslog(LOG_ERR, e->e_id,
							  "Milter (%s): Invalid numeric domain spec \"%s\"",
							  m->mf_name, at);
					milter_error(m, e);
					return -1;
				}
			}
			else
			{
				if (tTd(64, 5))
					sm_dprintf("X%s: Invalid numeric domain spec \"%s\"\n",
						m->mf_name, at);
				if (parseonly)
					syserr("X%s: Invalid numeric domain spec \"%s\"",
					       m->mf_name, at);
				else if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "Milter (%s): Invalid numeric domain spec \"%s\"",
						  m->mf_name, at);
				milter_error(m, e);
				return -1;
			}
		}
		else
		{
			hp = sm_gethostbyname(at, addr.sa.sa_family);
			if (hp == NULL)
			{
				save_errno = errno;
				if (tTd(64, 5))
					sm_dprintf("X%s: Unknown host name %s\n",
						   m->mf_name, at);
				errno = save_errno;
				if (parseonly)
					syserr("X%s: Unknown host name %s",
					       m->mf_name, at);
				else if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "Milter (%s): Unknown host name %s",
						  m->mf_name, at);
				milter_error(m, e);
				return -1;
			}
			addr.sa.sa_family = hp->h_addrtype;
			switch (hp->h_addrtype)
			{
#  if NETINET
			  case AF_INET:
				memmove(&addr.sin.sin_addr,
					hp->h_addr, INADDRSZ);
				addr.sin.sin_port = port;
				addrlen = sizeof(struct sockaddr_in);
				addrno = 1;
				break;
#  endif /* NETINET */

#  if NETINET6
			  case AF_INET6:
				memmove(&addr.sin6.sin6_addr,
					hp->h_addr, IN6ADDRSZ);
				addr.sin6.sin6_port = port;
				addrlen = sizeof(struct sockaddr_in6);
				addrno = 1;
				break;
#  endif /* NETINET6 */

			  default:
				if (tTd(64, 5))
					sm_dprintf("X%s: Unknown protocol for %s (%d)\n",
						   m->mf_name, at,
						   hp->h_addrtype);
				if (parseonly)
					syserr("X%s: Unknown protocol for %s (%d)",
					       m->mf_name, at, hp->h_addrtype);
				else if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "Milter (%s): Unknown protocol for %s (%d)",
						  m->mf_name, at,
						  hp->h_addrtype);
				milter_error(m, e);
#  if NETINET6
				freehostent(hp);
#  endif /* NETINET6 */
				return -1;
			}
		}
	}
	else
# endif /* NETINET || NETINET6 */
	{
		if (tTd(64, 5))
			sm_dprintf("X%s: unknown socket protocol\n",
				   m->mf_name);
		if (parseonly)
			syserr("X%s: unknown socket protocol", m->mf_name);
		else if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): unknown socket protocol",
				  m->mf_name);
		milter_error(m, e);
		return -1;
	}

	/* just parsing through? */
	if (parseonly)
	{
		m->mf_state = SMFS_READY;
# if NETINET6
		if (hp != NULL)
			freehostent(hp);
# endif /* NETINET6 */
		return 0;
	}

	/* sanity check */
	if (m->mf_state != SMFS_READY &&
	    m->mf_state != SMFS_CLOSED)
	{
		/* shouldn't happen */
		if (tTd(64, 1))
			sm_dprintf("Milter (%s): Trying to open filter in state %c\n",
				   m->mf_name, (char) m->mf_state);
		milter_error(m, e);
# if NETINET6
		if (hp != NULL)
			freehostent(hp);
# endif /* NETINET6 */
		return -1;
	}

	/* nope, actually connecting */
	for (;;)
	{
		sock = socket(addr.sa.sa_family, SOCK_STREAM, 0);
		if (sock < 0)
		{
			save_errno = errno;
			if (tTd(64, 5))
				sm_dprintf("Milter (%s): error creating socket: %s\n",
					   m->mf_name,
					   sm_errstring(save_errno));
			if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): error creating socket: %s",
					  m->mf_name, sm_errstring(save_errno));
			milter_error(m, e);
# if NETINET6
			if (hp != NULL)
				freehostent(hp);
# endif /* NETINET6 */
			return -1;
		}

		if (setjmp(MilterConnectTimeout) == 0)
		{
			SM_EVENT *ev = NULL;
			int i;

			if (m->mf_timeout[SMFTO_CONNECT] > 0)
				ev = sm_setevent(m->mf_timeout[SMFTO_CONNECT],
						 milter_connect_timeout, 0);

			i = connect(sock, (struct sockaddr *) &addr, addrlen);
			save_errno = errno;
			if (ev != NULL)
				sm_clrevent(ev);
			errno = save_errno;
			if (i >= 0)
				break;
		}

		/* couldn't connect.... try next address */
		save_errno = errno;
		p = CurHostName;
		CurHostName = at;
		if (tTd(64, 5))
			sm_dprintf("milter_open (%s): open %s failed: %s\n",
				   m->mf_name, at, sm_errstring(save_errno));
		if (MilterLogLevel > 13)
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter (%s): open %s failed: %s",
				  m->mf_name, at, sm_errstring(save_errno));
		CurHostName = p;
		(void) close(sock);

		/* try next address */
		if (hp != NULL && hp->h_addr_list[addrno] != NULL)
		{
			switch (addr.sa.sa_family)
			{
# if NETINET
			  case AF_INET:
				memmove(&addr.sin.sin_addr,
					hp->h_addr_list[addrno++],
					INADDRSZ);
				break;
# endif /* NETINET */

# if NETINET6
			  case AF_INET6:
				memmove(&addr.sin6.sin6_addr,
					hp->h_addr_list[addrno++],
					IN6ADDRSZ);
				break;
# endif /* NETINET6 */

			  default:
				if (tTd(64, 5))
					sm_dprintf("X%s: Unknown protocol for %s (%d)\n",
						   m->mf_name, at,
						   hp->h_addrtype);
				if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "Milter (%s): Unknown protocol for %s (%d)",
						  m->mf_name, at,
						  hp->h_addrtype);
				milter_error(m, e);
# if NETINET6
				freehostent(hp);
# endif /* NETINET6 */
				return -1;
			}
			continue;
		}
		p = CurHostName;
		CurHostName = at;
		if (tTd(64, 5))
			sm_dprintf("X%s: error connecting to filter: %s\n",
				   m->mf_name, sm_errstring(save_errno));
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): error connecting to filter: %s",
				  m->mf_name, sm_errstring(save_errno));
		CurHostName = p;
		milter_error(m, e);
# if NETINET6
		if (hp != NULL)
			freehostent(hp);
# endif /* NETINET6 */
		return -1;
	}
	m->mf_state = SMFS_OPEN;
# if NETINET6
	if (hp != NULL)
	{
		freehostent(hp);
		hp = NULL;
	}
# endif /* NETINET6 */
# if MILTER_NO_NAGLE && !defined(TCP_CORK)
	{
		int nodelay = 1;

		setsockopt(m->mf_sock, IPPROTO_TCP, TCP_NODELAY,
			   (char *)&nodelay, sizeof(nodelay));
	}
# endif /* MILTER_NO_NAGLE && !defined(TCP_CORK) */
	return sock;
}

static void
milter_connect_timeout(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(MilterConnectTimeout, 1);
}

/*
**  MILTER_SETUP -- setup structure for a mail filter
**
**	Parameters:
**		line -- the options line.
**
**	Returns:
**		none
*/

void
milter_setup(line)
	char *line;
{
	char fcode;
	char *p;
	struct milter *m;
	STAB *s;
	static int idx = 0;

	/* collect the filter name */
	for (p = line;
	     *p != '\0' && *p != ',' && !(isascii(*p) && isspace(*p));
	     p++)
		continue;
	if (*p != '\0')
		*p++ = '\0';
	if (line[0] == '\0')
	{
		syserr("name required for mail filter");
		return;
	}
	m = (struct milter *) xalloc(sizeof(*m));
	memset((char *) m, '\0', sizeof(*m));
	m->mf_name = newstr(line);
	m->mf_state = SMFS_READY;
	m->mf_sock = -1;
	m->mf_timeout[SMFTO_CONNECT] = (time_t) 300;
	m->mf_timeout[SMFTO_WRITE] = (time_t) 10;
	m->mf_timeout[SMFTO_READ] = (time_t) 10;
	m->mf_timeout[SMFTO_EOM] = (time_t) 300;
#if _FFR_MILTER_CHECK
	m->mf_mta_prot_version = SMFI_PROT_VERSION;
	m->mf_mta_prot_flags = SMFI_CURR_PROT;
	m->mf_mta_actions = SMFI_CURR_ACTS;
#endif /* _FFR_MILTER_CHECK */

	/* now scan through and assign info from the fields */
	while (*p != '\0')
	{
		char *delimptr;

		while (*p != '\0' &&
		       (*p == ',' || (isascii(*p) && isspace(*p))))
			p++;

		/* p now points to field code */
		fcode = *p;
		while (*p != '\0' && *p != '=' && *p != ',')
			p++;
		if (*p++ != '=')
		{
			syserr("X%s: `=' expected", m->mf_name);
			return;
		}
		while (isascii(*p) && isspace(*p))
			p++;

		/* p now points to the field body */
		p = munchstring(p, &delimptr, ',');

		/* install the field into the filter struct */
		switch (fcode)
		{
		  case 'S':		/* socket */
			if (p == NULL)
				m->mf_conn = NULL;
			else
				m->mf_conn = newstr(p);
			break;

		  case 'F':		/* Milter flags configured on MTA */
			for (; *p != '\0'; p++)
			{
				if (!(isascii(*p) && isspace(*p)))
					setbitn(bitidx(*p), m->mf_flags);
			}
			break;

		  case 'T':		/* timeouts */
			milter_parse_timeouts(p, m);
			break;

#if _FFR_MILTER_CHECK
		  case 'a':
			m->mf_mta_actions = strtoul(p, NULL, 0);
			break;
		  case 'f':
			m->mf_mta_prot_flags = strtoul(p, NULL, 0);
			break;
		  case 'v':
			m->mf_mta_prot_version = strtoul(p, NULL, 0);
			break;
#endif /* _FFR_MILTER_CHECK */

		  default:
			syserr("X%s: unknown filter equate %c=",
			       m->mf_name, fcode);
			break;
		}
		p = delimptr;
	}

	/* early check for errors */
	(void) milter_open(m, true, CurEnv);

	/* enter the filter into the symbol table */
	s = stab(m->mf_name, ST_MILTER, ST_ENTER);
	if (s->s_milter != NULL)
		syserr("X%s: duplicate filter definition", m->mf_name);
	else
	{
		s->s_milter = m;
		m->mf_idx = ++idx;
	}
}

/*
**  MILTER_CONFIG -- parse option list into an array and check config
**
**	Called when reading configuration file.
**
**	Parameters:
**		spec -- the filter list.
**		list -- the array to fill in.
**		max -- the maximum number of entries in list.
**
**	Returns:
**		none
*/

void
milter_config(spec, list, max)
	char *spec;
	struct milter **list;
	int max;
{
	int numitems = 0;
	char *p;

	/* leave one for the NULL signifying the end of the list */
	max--;

	for (p = spec; p != NULL; )
	{
		STAB *s;

		while (isascii(*p) && isspace(*p))
			p++;
		if (*p == '\0')
			break;
		spec = p;

		if (numitems >= max)
		{
			syserr("Too many filters defined, %d max", max);
			if (max > 0)
				list[0] = NULL;
			return;
		}
		p = strpbrk(p, ";,");
		if (p != NULL)
			*p++ = '\0';

		s = stab(spec, ST_MILTER, ST_FIND);
		if (s == NULL)
		{
			syserr("InputFilter %s not defined", spec);
			ExitStat = EX_CONFIG;
			return;
		}
		list[numitems++] = s->s_milter;
	}
	list[numitems] = NULL;

	/* if not set, set to LogLevel */
	if (MilterLogLevel == -1)
		MilterLogLevel = LogLevel;
}

/*
**  MILTER_PARSE_TIMEOUTS -- parse timeout list
**
**	Called when reading configuration file.
**
**	Parameters:
**		spec -- the timeout list.
**		m -- milter to set.
**
**	Returns:
**		none
*/

static void
milter_parse_timeouts(spec, m)
	char *spec;
	struct milter *m;
{
	char fcode;
	int tcode;
	char *p;

	p = spec;

	/* now scan through and assign info from the fields */
	while (*p != '\0')
	{
		char *delimptr;

		while (*p != '\0' &&
		       (*p == ';' || (isascii(*p) && isspace(*p))))
			p++;

		/* p now points to field code */
		fcode = *p;
		while (*p != '\0' && *p != ':')
			p++;
		if (*p++ != ':')
		{
			syserr("X%s, T=: `:' expected", m->mf_name);
			return;
		}
		while (isascii(*p) && isspace(*p))
			p++;

		/* p now points to the field body */
		p = munchstring(p, &delimptr, ';');
		tcode = -1;

		/* install the field into the filter struct */
		switch (fcode)
		{
		  case 'C':
			tcode = SMFTO_CONNECT;
			break;

		  case 'S':
			tcode = SMFTO_WRITE;
			break;

		  case 'R':
			tcode = SMFTO_READ;
			break;

		  case 'E':
			tcode = SMFTO_EOM;
			break;

		  default:
			if (tTd(64, 5))
				sm_dprintf("X%s: %c unknown\n",
					   m->mf_name, fcode);
			syserr("X%s: unknown filter timeout %c",
			       m->mf_name, fcode);
			break;
		}
		if (tcode >= 0)
		{
			m->mf_timeout[tcode] = convtime(p, 's');
			if (tTd(64, 5))
				sm_dprintf("X%s: %c=%ld\n",
					   m->mf_name, fcode,
					   (u_long) m->mf_timeout[tcode]);
		}
		p = delimptr;
	}
}

/*
**  MILTER_SET_MACROS -- set milter macros
**
**	Parameters:
**		name -- name of milter.
**		macros -- where to store macros.
**		val -- the value of the option.
**		nummac -- current number of macros
**
**	Returns:
**		new number of macros
*/

static int
milter_set_macros(name, macros, val, nummac)
	char *name;
	char **macros;
	char *val;
	int nummac;
{
	char *p;

	p = newstr(val);
	while (*p != '\0')
	{
		char *macro;

		/* Skip leading commas, spaces */
		while (*p != '\0' &&
		       (*p == ',' || (isascii(*p) && isspace(*p))))
			p++;

		if (*p == '\0')
			break;

		/* Find end of macro */
		macro = p;
		while (*p != '\0' && *p != ',' &&
		       isascii(*p) && !isspace(*p))
			p++;
		if (*p != '\0')
			*p++ = '\0';

		if (nummac >= MAXFILTERMACROS)
		{
			syserr("milter_set_option: too many macros in Milter.%s (max %d)",
			       name, MAXFILTERMACROS);
			macros[nummac] = NULL;
			return -1;
		}
		macros[nummac++] = macro;
	}
	macros[nummac] = NULL;
	return nummac;
}

/*
**  MILTER_SET_OPTION -- set an individual milter option
**
**	Parameters:
**		name -- the name of the option.
**		val -- the value of the option.
**		sticky -- if set, don't let other setoptions override
**			this value.
**
**	Returns:
**		none.
*/

/* set if Milter sub-option is stuck */
static BITMAP256	StickyMilterOpt;

static struct milteropt
{
	char		*mo_name;	/* long name of milter option */
	unsigned char	mo_code;	/* code for option */
} MilterOptTab[] =
{
	{ "macros.connect",		SMFIM_CONNECT			},
	{ "macros.helo",		SMFIM_HELO			},
	{ "macros.envfrom",		SMFIM_ENVFROM			},
	{ "macros.envrcpt",		SMFIM_ENVRCPT			},
	{ "macros.data",		SMFIM_DATA			},
	{ "macros.eom",			SMFIM_EOM			},
	{ "macros.eoh",			SMFIM_EOH			},

# define MO_LOGLEVEL			0x07
	{ "loglevel",			MO_LOGLEVEL			},
# if _FFR_MAXDATASIZE || _FFR_MDS_NEGOTIATE
#  define MO_MAXDATASIZE		0x08
	{ "maxdatasize",		MO_MAXDATASIZE			},
# endif /* _FFR_MAXDATASIZE || _FFR_MDS_NEGOTIATE */
	{ NULL,				(unsigned char)-1		},
};

void
milter_set_option(name, val, sticky)
	char *name;
	char *val;
	bool sticky;
{
	int nummac, r;
	struct milteropt *mo;
	char **macros = NULL;

	nummac = 0;
	if (tTd(37, 2) || tTd(64, 5))
		sm_dprintf("milter_set_option(%s = %s)", name, val);

	if (name == NULL)
	{
		syserr("milter_set_option: invalid Milter option, must specify suboption");
		return;
	}

	for (mo = MilterOptTab; mo->mo_name != NULL; mo++)
	{
		if (sm_strcasecmp(mo->mo_name, name) == 0)
			break;
	}

	if (mo->mo_name == NULL)
	{
		syserr("milter_set_option: invalid Milter option %s", name);
		return;
	}

	/*
	**  See if this option is preset for us.
	*/

	if (!sticky && bitnset(mo->mo_code, StickyMilterOpt))
	{
		if (tTd(37, 2) || tTd(64,5))
			sm_dprintf(" (ignored)\n");
		return;
	}

	if (tTd(37, 2) || tTd(64,5))
		sm_dprintf("\n");

	switch (mo->mo_code)
	{
	  case MO_LOGLEVEL:
		MilterLogLevel = atoi(val);
		break;

# if _FFR_MAXDATASIZE || _FFR_MDS_NEGOTIATE
	  case MO_MAXDATASIZE:
#  if _FFR_MDS_NEGOTIATE
		MilterMaxDataSize = (size_t)atol(val);
		if (MilterMaxDataSize != MILTER_MDS_64K &&
		    MilterMaxDataSize != MILTER_MDS_256K &&
		    MilterMaxDataSize != MILTER_MDS_1M)
		{
			sm_syslog(LOG_WARNING, NOQID,
				"WARNING: Milter.%s=%lu, allowed are only %d, %d, and %d",
				name, (unsigned long) MilterMaxDataSize,
				MILTER_MDS_64K, MILTER_MDS_256K,
				MILTER_MDS_1M);
			if (MilterMaxDataSize < MILTER_MDS_64K)
				MilterMaxDataSize = MILTER_MDS_64K;
			else if (MilterMaxDataSize < MILTER_MDS_256K)
				MilterMaxDataSize = MILTER_MDS_256K;
			else
				MilterMaxDataSize = MILTER_MDS_1M;
		}
#  endif /* _FFR_MDS_NEGOTIATE */
		break;
# endif /* _FFR_MAXDATASIZE || _FFR_MDS_NEGOTIATE */

	  case SMFIM_CONNECT:
	  case SMFIM_HELO:
	  case SMFIM_ENVFROM:
	  case SMFIM_ENVRCPT:
	  case SMFIM_EOH:
	  case SMFIM_EOM:
	  case SMFIM_DATA:
		macros = MilterMacros[mo->mo_code][0];

		r = milter_set_macros(name, macros, val, nummac);
		if (r >= 0)
			nummac = r;
		break;

	  default:
		syserr("milter_set_option: invalid Milter option %s", name);
		break;
	}
	if (sticky)
		setbitn(mo->mo_code, StickyMilterOpt);
}

/*
**  MILTER_REOPEN_DF -- open & truncate the data file (for replbody)
**
**	Parameters:
**		e -- current envelope.
**
**	Returns:
**		0 if succesful, -1 otherwise
*/

static int
milter_reopen_df(e)
	ENVELOPE *e;
{
	char dfname[MAXPATHLEN];

	(void) sm_strlcpy(dfname, queuename(e, DATAFL_LETTER), sizeof(dfname));

	/*
	**  In SuperSafe == SAFE_REALLY mode, e->e_dfp is a read-only FP so
	**  close and reopen writable (later close and reopen
	**  read only again).
	**
	**  In SuperSafe != SAFE_REALLY mode, e->e_dfp still points at the
	**  buffered file I/O descriptor, still open for writing so there
	**  isn't any work to do here (except checking for consistency).
	*/

	if (SuperSafe == SAFE_REALLY)
	{
		/* close read-only data file */
		if (bitset(EF_HAS_DF, e->e_flags) && e->e_dfp != NULL)
		{
			(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
			e->e_flags &= ~EF_HAS_DF;
		}

		/* open writable */
		if ((e->e_dfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, dfname,
					   SM_IO_RDWR_B, NULL)) == NULL)
		{
			MILTER_DF_ERROR("milter_reopen_df: sm_io_open %s: %s");
			return -1;
		}
	}
	else if (e->e_dfp == NULL)
	{
		/* shouldn't happen */
		errno = ENOENT;
		MILTER_DF_ERROR("milter_reopen_df: NULL e_dfp (%s: %s)");
		return -1;
	}
	return 0;
}

/*
**  MILTER_RESET_DF -- re-open read-only the data file (for replbody)
**
**	Parameters:
**		e -- current envelope.
**
**	Returns:
**		0 if succesful, -1 otherwise
*/

static int
milter_reset_df(e)
	ENVELOPE *e;
{
	int afd;
	char dfname[MAXPATHLEN];

	(void) sm_strlcpy(dfname, queuename(e, DATAFL_LETTER), sizeof(dfname));

	if (sm_io_flush(e->e_dfp, SM_TIME_DEFAULT) != 0 ||
	    sm_io_error(e->e_dfp))
	{
		MILTER_DF_ERROR("milter_reset_df: error writing/flushing %s: %s");
		return -1;
	}
	else if (SuperSafe != SAFE_REALLY)
	{
		/* skip next few clauses */
		/* EMPTY */
	}
	else if ((afd = sm_io_getinfo(e->e_dfp, SM_IO_WHAT_FD, NULL)) >= 0
		 && fsync(afd) < 0)
	{
		MILTER_DF_ERROR("milter_reset_df: error sync'ing %s: %s");
		return -1;
	}
	else if (sm_io_close(e->e_dfp, SM_TIME_DEFAULT) < 0)
	{
		MILTER_DF_ERROR("milter_reset_df: error closing %s: %s");
		return -1;
	}
	else if ((e->e_dfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, dfname,
					SM_IO_RDONLY_B, NULL)) == NULL)
	{
		MILTER_DF_ERROR("milter_reset_df: error reopening %s: %s");
		return -1;
	}
	else
		e->e_flags |= EF_HAS_DF;
	return 0;
}

/*
**  MILTER_QUIT_FILTER -- close down a single filter
**
**	Parameters:
**		m -- milter structure of filter to close down.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_quit_filter(m, e)
	struct milter *m;
	ENVELOPE *e;
{
	if (tTd(64, 10))
		sm_dprintf("milter_quit_filter(%s)\n", m->mf_name);
	if (MilterLogLevel > 18)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): quit filter",
			  m->mf_name);

	/* Never replace error state */
	if (m->mf_state == SMFS_ERROR)
		return;

	if (m->mf_sock < 0 ||
	    m->mf_state == SMFS_CLOSED ||
	    m->mf_state == SMFS_READY)
	{
		m->mf_sock = -1;
		m->mf_state = SMFS_CLOSED;
		return;
	}

	(void) milter_write(m, SMFIC_QUIT, (char *) NULL, 0,
			    m->mf_timeout[SMFTO_WRITE], e, "quit_filter");
	if (m->mf_sock >= 0)
	{
		(void) close(m->mf_sock);
		m->mf_sock = -1;
	}
	if (m->mf_state != SMFS_ERROR)
		m->mf_state = SMFS_CLOSED;
}

/*
**  MILTER_ABORT_FILTER -- tell filter to abort current message
**
**	Parameters:
**		m -- milter structure of filter to abort.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_abort_filter(m, e)
	struct milter *m;
	ENVELOPE *e;
{
	if (tTd(64, 10))
		sm_dprintf("milter_abort_filter(%s)\n", m->mf_name);
	if (MilterLogLevel > 10)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): abort filter",
			  m->mf_name);

	if (m->mf_sock < 0 ||
	    m->mf_state != SMFS_INMSG)
		return;

	(void) milter_write(m, SMFIC_ABORT, (char *) NULL, 0,
			    m->mf_timeout[SMFTO_WRITE], e, "abort_filter");
	if (m->mf_state != SMFS_ERROR)
		m->mf_state = SMFS_DONE;
}

/*
**  MILTER_SEND_MACROS -- provide macros to the filters
**
**	Parameters:
**		m -- milter to send macros to.
**		macros -- macros to send for filter smfi_getsymval().
**		cmd -- which command the macros are associated with.
**		e -- current envelope (for macro access).
**
**	Returns:
**		none
*/

static void
milter_send_macros(m, macros, cmd, e)
	struct milter *m;
	char **macros;
	int cmd;
	ENVELOPE *e;
{
	int i;
	int mid;
	char command = (char) cmd;
	char *v;
	char *buf, *bp;
	char exp[MAXLINE];
	ssize_t s;

	/* sanity check */
	if (macros == NULL || macros[0] == NULL)
		return;

	/* put together data */
	s = 1;			/* for the command character */
	for (i = 0; macros[i] != NULL; i++)
	{
		mid = macid(macros[i]);
		if (mid == 0)
			continue;
		v = macvalue(mid, e);
		if (v == NULL)
			continue;
		expand(v, exp, sizeof(exp), e);
		s += strlen(macros[i]) + 1 + strlen(exp) + 1;
	}

	if (s < 0)
		return;

	buf = (char *) xalloc(s);
	bp = buf;
	*bp++ = command;
	for (i = 0; macros[i] != NULL; i++)
	{
		mid = macid(macros[i]);
		if (mid == 0)
			continue;
		v = macvalue(mid, e);
		if (v == NULL)
			continue;
		expand(v, exp, sizeof(exp), e);

		if (tTd(64, 10))
			sm_dprintf("milter_send_macros(%s, %c): %s=%s\n",
				m->mf_name, command, macros[i], exp);

		(void) sm_strlcpy(bp, macros[i], s - (bp - buf));
		bp += strlen(bp) + 1;
		(void) sm_strlcpy(bp, exp, s - (bp - buf));
		bp += strlen(bp) + 1;
	}
	(void) milter_write(m, SMFIC_MACRO, buf, s,
			    m->mf_timeout[SMFTO_WRITE], e, "send_macros");
	sm_free(buf);
}

/*
**  MILTER_SEND_COMMAND -- send a command and return the response for a filter
**
**	Parameters:
**		m -- current milter filter
**		cmd -- command to send.
**		data -- optional command data.
**		sz -- length of buf.
**		e -- current envelope (for e->e_id).
**		state -- return state word.
**
**	Returns:
**		response string (may be NULL)
*/

static char *
milter_send_command(m, cmd, data, sz, e, state, where)
	struct milter *m;
	int cmd;
	void *data;
	ssize_t sz;
	ENVELOPE *e;
	char *state;
	const char *where;
{
	char rcmd;
	ssize_t rlen;
	unsigned long skipflag;
	unsigned long norespflag = 0;
	char command = (char) cmd;
	char *action;
	char *defresponse;
	char *response;

	if (tTd(64, 10))
		sm_dprintf("milter_send_command(%s): cmd %c len %ld\n",
			m->mf_name, (char) command, (long) sz);

	/* find skip flag and default failure */
	switch (command)
	{
	  case SMFIC_CONNECT:
		skipflag = SMFIP_NOCONNECT;
		norespflag = SMFIP_NR_CONN;
		action = "connect";
		defresponse = "554 Command rejected";
		break;

	  case SMFIC_HELO:
		skipflag = SMFIP_NOHELO;
		norespflag = SMFIP_NR_HELO;
		action = "helo";
		defresponse = "550 Command rejected";
		break;

	  case SMFIC_MAIL:
		skipflag = SMFIP_NOMAIL;
		norespflag = SMFIP_NR_MAIL;
		action = "mail";
		defresponse = "550 5.7.1 Command rejected";
		break;

	  case SMFIC_RCPT:
		skipflag = SMFIP_NORCPT;
		norespflag = SMFIP_NR_RCPT;
		action = "rcpt";
		defresponse = "550 5.7.1 Command rejected";
		break;

	  case SMFIC_HEADER:
		skipflag = SMFIP_NOHDRS;
		norespflag = SMFIP_NR_HDR;
		action = "header";
		defresponse = "550 5.7.1 Command rejected";
		break;

	  case SMFIC_BODY:
		skipflag = SMFIP_NOBODY;
		norespflag = SMFIP_NR_BODY;
		action = "body";
		defresponse = "554 5.7.1 Command rejected";
		break;

	  case SMFIC_EOH:
		skipflag = SMFIP_NOEOH;
		norespflag = SMFIP_NR_EOH;
		action = "eoh";
		defresponse = "550 5.7.1 Command rejected";
		break;

	  case SMFIC_UNKNOWN:
		skipflag = SMFIP_NOUNKNOWN;
		norespflag = SMFIP_NR_UNKN;
		action = "unknown";
		defresponse = "550 5.7.1 Command rejected";
		break;

	  case SMFIC_DATA:
		skipflag = SMFIP_NODATA;
		norespflag = SMFIP_NR_DATA;
		action = "data";
		defresponse = "550 5.7.1 Command rejected";
		break;

	  case SMFIC_BODYEOB:
	  case SMFIC_OPTNEG:
	  case SMFIC_MACRO:
	  case SMFIC_ABORT:
	  case SMFIC_QUIT:
		/* NOTE: not handled by milter_send_command() */
		/* FALLTHROUGH */

	  default:
		skipflag = 0;
		action = "default";
		defresponse = "550 5.7.1 Command rejected";
		break;
	}

	if (tTd(64, 10))
		sm_dprintf("milter_send_command(%s): skip=%lx, pflags=%x\n",
			m->mf_name, skipflag, m->mf_pflags);

	/* check if filter wants this command */
	if (skipflag != 0 && bitset(skipflag, m->mf_pflags))
		return NULL;

	/* send the command to the filter */
	(void) milter_write(m, command, data, sz,
			    m->mf_timeout[SMFTO_WRITE], e, where);
	if (m->mf_state == SMFS_ERROR)
	{
		MILTER_CHECK_ERROR(false, return NULL);
		return NULL;
	}

	/* check if filter sends response to this command */
	if (norespflag != 0 && bitset(norespflag, m->mf_pflags))
		return NULL;

	/* get the response from the filter */
	response = milter_read(m, &rcmd, &rlen,
			       m->mf_timeout[SMFTO_READ], e, where);
	if (m->mf_state == SMFS_ERROR)
	{
		MILTER_CHECK_ERROR(false, return NULL);
		return NULL;
	}

	if (tTd(64, 10))
		sm_dprintf("milter_send_command(%s): returned %c\n",
			   m->mf_name, (char) rcmd);

	switch (rcmd)
	{
	  case SMFIR_REPLYCODE:
		MILTER_CHECK_REPLYCODE(defresponse);
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, reject=%s",
				  m->mf_name, action, response);
		*state = rcmd;
		break;

	  case SMFIR_REJECT:
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, reject",
				  m->mf_name, action);
		*state = rcmd;
		break;

	  case SMFIR_DISCARD:
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, discard",
				  m->mf_name, action);
		*state = rcmd;
		break;

	  case SMFIR_TEMPFAIL:
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, tempfail",
				  m->mf_name, action);
		*state = rcmd;
		break;

	  case SMFIR_ACCEPT:
		/* this filter is done with message/connection */
		if (command == SMFIC_HELO ||
		    command == SMFIC_CONNECT)
			m->mf_state = SMFS_CLOSABLE;
		else
			m->mf_state = SMFS_DONE;
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, accepted",
				  m->mf_name, action);
		break;

	  case SMFIR_CONTINUE:
		/* if MAIL command is ok, filter is in message state */
		if (command == SMFIC_MAIL)
			m->mf_state = SMFS_INMSG;
		if (MilterLogLevel > 12)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, continue",
				  m->mf_name, action);
		break;

	  case SMFIR_SKIP:
		if (MilterLogLevel > 12)
			sm_syslog(LOG_INFO, e->e_id,
				  "milter=%s, action=%s, skip",
				  m->mf_name, action);
		m->mf_state = SMFS_SKIP;
		break;

	  default:
		/* Invalid response to command */
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "milter_send_command(%s): action=%s returned bogus response %c",
				  m->mf_name, action, rcmd);
		milter_error(m, e); /* NO ERROR CHECK? */
		break;
	}

	if (*state != SMFIR_REPLYCODE && response != NULL)
	{
		sm_free(response); /* XXX */
		response = NULL;
	}
	return response;
}

/*
**  MILTER_COMMAND -- send a command and return the response for each filter
**
**	Parameters:
**		cmd -- command to send.
**		data -- optional command data.
**		sz -- length of buf.
**		stage -- index of macros to send for filter smfi_getsymval().
**		e -- current envelope (for macro access).
**		state -- return state word.
**		where -- description of calling function (logging).
**		cmd_error -- did the SMTP command cause an error?
**
**	Returns:
**		response string (may be NULL)
*/

static char *
milter_command(cmd, data, sz, stage, e, state, where, cmd_error)
	int cmd;
	void *data;
	ssize_t sz;
	int stage;
	ENVELOPE *e;
	char *state;
	const char *where;
	bool cmd_error;
{
	int i;
	char command = (char) cmd;
	char *response = NULL;
	time_t tn = 0;

	if (tTd(64, 10))
		sm_dprintf("milter_command: cmd %c len %ld\n",
			command, (long) sz);

	*state = SMFIR_CONTINUE;
	for (i = 0; InputFilters[i] != NULL; i++)
	{
		struct milter *m = InputFilters[i];

		/* previous problem? */
		if (m->mf_state == SMFS_ERROR)
		{
			MILTER_CHECK_ERROR(false, continue);
			break;
		}

		/* sanity check */
		if (m->mf_sock < 0 ||
		    (m->mf_state != SMFS_OPEN && m->mf_state != SMFS_INMSG))
			continue;

		if (stage >= SMFIM_FIRST && stage <= SMFIM_LAST)
		{
			int idx;
			char **macros;

			if ((m->mf_lflags & MI_LFLAGS_SYM(stage)) != 0)
				idx = m->mf_idx;
			else
				idx = 0;
			SM_ASSERT(idx >= 0 && idx <= MAXFILTERS);
			macros = MilterMacros[stage][idx];

			/* send macros (regardless of whether we send cmd) */
			if (macros != NULL && macros[0] != NULL)
			{
				milter_send_macros(m, macros, command, e);
				if (m->mf_state == SMFS_ERROR)
				{
					MILTER_CHECK_ERROR(false, continue);
					break;
				}
			}
		}

		if (MilterLogLevel > 21)
			tn = curtime();

		/*
		**  send the command if
		**	there is no error
		**	or it's RCPT and the client asked for it:
		**	!cmd_error ||
		**	where == "rcpt" && m->mf_pflags & SMFIP_RCPT_REJ != 0
		**  negate that condition and use continue
		*/

		if (cmd_error &&
		    (strcmp(where, "rcpt") != 0 ||
		     (m->mf_pflags & SMFIP_RCPT_REJ) == 0))
			continue;

		response = milter_send_command(m, command, data, sz, e, state,
						where);

		if (MilterLogLevel > 21)
		{
			/* log the time it took for the command per filter */
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter (%s): time command (%c), %d",
				  m->mf_name, command, (int) (tn - curtime()));
		}

		if (*state != SMFIR_CONTINUE)
			break;
	}
	return response;
}

static int milter_getsymlist __P((struct milter *, char *, int, int));

static int
milter_getsymlist(m, buf, rlen, offset)
	struct milter *m;
	char *buf;
	int rlen;
	int offset;
{
	int i, r, nummac;
	mi_int32 v;

	SM_ASSERT(m != NULL);
	SM_ASSERT(buf != NULL);

	while (offset + MILTER_LEN_BYTES < rlen)
	{
		size_t len;
		char **macros;

		nummac = 0;
		(void) memcpy((char *) &v, buf + offset, MILTER_LEN_BYTES);
		i = ntohl(v);
		if (i < SMFIM_FIRST || i > SMFIM_LAST)
			return -1;
		offset += MILTER_LEN_BYTES;
		macros = NULL;

#define SM_M_MACRO_NAME(i) (((i) < SM_ARRAY_SIZE(MilterOptTab) && (i) >= 0) \
				?  MilterOptTab[i].mo_name : "?")
		switch (i)
		{
		  case SMFIM_CONNECT:
		  case SMFIM_HELO:
		  case SMFIM_ENVFROM:
		  case SMFIM_ENVRCPT:
		  case SMFIM_EOH:
		  case SMFIM_EOM:
		  case SMFIM_DATA:
			SM_ASSERT(m->mf_idx > 0 && m->mf_idx < MAXFILTERS);
			macros = MilterMacros[i][m->mf_idx];
			m->mf_lflags |= MI_LFLAGS_SYM(i);
			len = strlen(buf + offset);
			if (len >= 0)
			{
				r = milter_set_macros(m->mf_name, macros,
						buf + offset, nummac);
				if (r >= 0)
					nummac = r;
				if (tTd(64, 5))
					sm_dprintf("milter_getsymlist(%s, %s, \"%s\")=%d\n",
						m->mf_name,
						SM_M_MACRO_NAME(i),
						buf + offset, r);
			}
			break;

		  default:
			return -1;
		}
		offset += len + 1;
	}

	return 0;
}

/*
**  MILTER_NEGOTIATE -- get version and flags from filter
**
**	Parameters:
**		m -- milter filter structure.
**		e -- current envelope.
**		milters -- milters structure.
**
**	Returns:
**		0 on success, -1 otherwise
*/

static int
milter_negotiate(m, e, milters)
	struct milter *m;
	ENVELOPE *e;
	milters_T *milters;
{
	char rcmd;
	mi_int32 fvers, fflags, pflags;
	mi_int32 mta_prot_vers, mta_prot_flags, mta_actions;
	ssize_t rlen;
	char *response;
	char data[MILTER_OPTLEN];

	/* sanity check */
	if (m->mf_sock < 0 || m->mf_state != SMFS_OPEN)
	{
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate, impossible state",
				  m->mf_name);
		milter_error(m, e);
		return -1;
	}

#if _FFR_MILTER_CHECK
	mta_prot_vers = m->mf_mta_prot_version;
	mta_prot_flags = m->mf_mta_prot_flags;
	mta_actions = m->mf_mta_actions;
#else /* _FFR_MILTER_CHECK */
	mta_prot_vers = SMFI_PROT_VERSION;
	mta_prot_flags = SMFI_CURR_PROT;
	mta_actions = SMFI_CURR_ACTS;
#endif /* _FFR_MILTER_CHECK */
#if _FFR_MDS_NEGOTIATE
	if (MilterMaxDataSize == MILTER_MDS_256K)
		mta_prot_flags |= SMFIP_MDS_256K;
	else if (MilterMaxDataSize == MILTER_MDS_1M)
		mta_prot_flags |= SMFIP_MDS_1M;
#endif /* _FFR_MDS_NEGOTIATE */

	fvers = htonl(mta_prot_vers);
	pflags = htonl(mta_prot_flags);
	fflags = htonl(mta_actions);
	(void) memcpy(data, (char *) &fvers, MILTER_LEN_BYTES);
	(void) memcpy(data + MILTER_LEN_BYTES,
		      (char *) &fflags, MILTER_LEN_BYTES);
	(void) memcpy(data + (MILTER_LEN_BYTES * 2),
		      (char *) &pflags, MILTER_LEN_BYTES);
	(void) milter_write(m, SMFIC_OPTNEG, data, sizeof(data),
			    m->mf_timeout[SMFTO_WRITE], e, "negotiate");

	if (m->mf_state == SMFS_ERROR)
		return -1;

	if (tTd(64, 5))
		sm_dprintf("milter_negotiate(%s): send: version %lu, fflags 0x%lx, pflags 0x%lx\n",
			m->mf_name, (unsigned long) ntohl(fvers),
			(unsigned long) ntohl(fflags),
			(unsigned long) ntohl(pflags));

	response = milter_read(m, &rcmd, &rlen, m->mf_timeout[SMFTO_READ], e,
				"negotiate");
	if (m->mf_state == SMFS_ERROR)
		return -1;

	if (rcmd != SMFIC_OPTNEG)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_negotiate(%s): returned %c instead of %c\n",
				m->mf_name, rcmd, SMFIC_OPTNEG);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate: returned %c instead of %c",
				  m->mf_name, rcmd, SMFIC_OPTNEG);
		if (response != NULL)
			sm_free(response); /* XXX */
		milter_error(m, e);
		return -1;
	}

	/* Make sure we have enough bytes for the version */
	if (response == NULL || rlen < MILTER_LEN_BYTES)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_negotiate(%s): did not return valid info\n",
				m->mf_name);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate: did not return valid info",
				  m->mf_name);
		if (response != NULL)
			sm_free(response); /* XXX */
		milter_error(m, e);
		return -1;
	}

	/* extract information */
	(void) memcpy((char *) &fvers, response, MILTER_LEN_BYTES);

	/* Now make sure we have enough for the feature bitmap */
	if (rlen < MILTER_OPTLEN)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_negotiate(%s): did not return enough info\n",
				m->mf_name);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate: did not return enough info",
				  m->mf_name);
		if (response != NULL)
			sm_free(response); /* XXX */
		milter_error(m, e);
		return -1;
	}

	(void) memcpy((char *) &fflags, response + MILTER_LEN_BYTES,
		      MILTER_LEN_BYTES);
	(void) memcpy((char *) &pflags, response + (MILTER_LEN_BYTES * 2),
		      MILTER_LEN_BYTES);

	m->mf_fvers = ntohl(fvers);
	m->mf_fflags = ntohl(fflags);
	m->mf_pflags = ntohl(pflags);

	/* check for version compatibility */
	if (m->mf_fvers == 1 ||
	    m->mf_fvers > SMFI_VERSION)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_negotiate(%s): version %d != MTA milter version %d\n",
				m->mf_name, m->mf_fvers, SMFI_VERSION);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate: version %d != MTA milter version %d",
				  m->mf_name, m->mf_fvers, SMFI_VERSION);
		milter_error(m, e);
		goto error;
	}

	/* check for filter feature mismatch */
	if ((m->mf_fflags & mta_actions) != m->mf_fflags)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_negotiate(%s): filter abilities 0x%x != MTA milter abilities 0x%lx\n",
				m->mf_name, m->mf_fflags,
				(unsigned long) mta_actions);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate: filter abilities 0x%x != MTA milter abilities 0x%lx",
				  m->mf_name, m->mf_fflags,
				  (unsigned long) mta_actions);
		milter_error(m, e);
		goto error;
	}

#if _FFR_MDS_NEGOTIATE
	/* use a table instead of sequence? */
	if (bitset(SMFIP_MDS_1M, m->mf_pflags))
	{
		if (MilterMaxDataSize != MILTER_MDS_1M)
		{
			/* this should not happen... */
			sm_syslog(LOG_WARNING, NOQID,
				  "WARNING: Milter.maxdatasize: configured=%lu, set by libmilter=%d",
				  (unsigned long) MilterMaxDataSize,
				  MILTER_MDS_1M);
			MilterMaxDataSize = MILTER_MDS_1M;
		}
	}
	else if (bitset(SMFIP_MDS_256K, m->mf_pflags))
	{
		if (MilterMaxDataSize != MILTER_MDS_256K)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "WARNING: Milter.maxdatasize: configured=%lu, set by libmilter=%d",
				  (unsigned long) MilterMaxDataSize,
				  MILTER_MDS_256K);
			MilterMaxDataSize = MILTER_MDS_256K;
		}
	}
	else if (MilterMaxDataSize != MILTER_MDS_64K)
	{
		sm_syslog(LOG_WARNING, NOQID,
			  "WARNING: Milter.maxdatasize: configured=%lu, set by libmilter=%d",
			  (unsigned long) MilterMaxDataSize,
			  MILTER_MDS_64K);
		MilterMaxDataSize = MILTER_MDS_64K;
	}
	m->mf_pflags &= ~SMFI_INTERNAL;
#endif /* _FFR_MDS_NEGOTIATE */

	/* check for protocol feature mismatch */
	if ((m->mf_pflags & mta_prot_flags) != m->mf_pflags)
	{
		if (tTd(64, 5))
			sm_dprintf("milter_negotiate(%s): protocol abilities 0x%x != MTA milter abilities 0x%lx\n",
				m->mf_name, m->mf_pflags,
				(unsigned long) mta_prot_flags);
		if (MilterLogLevel > 0)
			sm_syslog(LOG_ERR, e->e_id,
				  "Milter (%s): negotiate: protocol abilities 0x%x != MTA milter abilities 0x%lx",
				  m->mf_name, m->mf_pflags,
				  (unsigned long) mta_prot_flags);
		milter_error(m, e);
		goto error;
	}

	if (m->mf_fvers <= 2)
		m->mf_pflags |= SMFIP_NOUNKNOWN;
	if (m->mf_fvers <= 3)
		m->mf_pflags |= SMFIP_NODATA;

	if (rlen > MILTER_OPTLEN)
	{
		milter_getsymlist(m, response, rlen, MILTER_OPTLEN);
	}

	if (bitset(SMFIF_DELRCPT, m->mf_fflags))
		milters->mis_flags |= MIS_FL_DEL_RCPT;
	if (!bitset(SMFIP_NORCPT, m->mf_pflags) &&
	    !bitset(SMFIP_NR_RCPT, m->mf_pflags))
		milters->mis_flags |= MIS_FL_REJ_RCPT;

	if (tTd(64, 5))
		sm_dprintf("milter_negotiate(%s): received: version %u, fflags 0x%x, pflags 0x%x\n",
			m->mf_name, m->mf_fvers, m->mf_fflags, m->mf_pflags);
	return 0;

  error:
	if (response != NULL)
		sm_free(response); /* XXX */
	return -1;
}

/*
**  MILTER_PER_CONNECTION_CHECK -- checks on per-connection commands
**
**	Reduce code duplication by putting these checks in one place
**
**	Parameters:
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_per_connection_check(e)
	ENVELOPE *e;
{
	int i;

	/* see if we are done with any of the filters */
	for (i = 0; InputFilters[i] != NULL; i++)
	{
		struct milter *m = InputFilters[i];

		if (m->mf_state == SMFS_CLOSABLE)
			milter_quit_filter(m, e);
	}
}

/*
**  MILTER_ERROR -- Put a milter filter into error state
**
**	Parameters:
**		m -- the broken filter.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_error(m, e)
	struct milter *m;
	ENVELOPE *e;
{
	/*
	**  We could send a quit here but we may have gotten here due to
	**  an I/O error so we don't want to try to make things worse.
	*/

	if (m->mf_sock >= 0)
	{
		(void) close(m->mf_sock);
		m->mf_sock = -1;
	}
	m->mf_state = SMFS_ERROR;

	if (MilterLogLevel > 0)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): to error state",
			  m->mf_name);
}

/*
**  MILTER_HEADERS -- send headers to a single milter filter
**
**	Parameters:
**		m -- current filter.
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
*/

static char *
milter_headers(m, e, state)
	struct milter *m;
	ENVELOPE *e;
	char *state;
{
	char *response = NULL;
	HDR *h;

	if (MilterLogLevel > 17)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): headers, send",
			  m->mf_name);

	for (h = e->e_header; h != NULL; h = h->h_link)
	{
		int len_n, len_v, len_t, len_f;
		char *buf, *hv;

		/* don't send over deleted headers */
		if (h->h_value == NULL)
		{
			/* strip H_USER so not counted in milter_changeheader() */
			h->h_flags &= ~H_USER;
			continue;
		}

		/* skip auto-generated */
		if (!bitset(H_USER, h->h_flags))
			continue;

		if (tTd(64, 10))
			sm_dprintf("milter_headers: %s:%s\n",
				h->h_field, h->h_value);
		if (MilterLogLevel > 21)
			sm_syslog(LOG_INFO, e->e_id, "Milter (%s): header, %s",
				  m->mf_name, h->h_field);

		if (bitset(SMFIP_HDR_LEADSPC, m->mf_pflags)
		    || *(h->h_value) != ' ')
			hv = h->h_value;
		else
			hv = h->h_value + 1;
		len_f = strlen(h->h_field) + 1;
		len_t = len_f + strlen(hv) + 1;
		if (len_t < 0)
			continue;
		buf = (char *) xalloc(len_t);

		/*
		**  Note: currently the call to dequote_internal_chars()
		**  is not required as h_field is supposed to be 7-bit US-ASCII.
		*/

		len_n = dequote_internal_chars(h->h_field, buf, len_f);
		SM_ASSERT(len_n < len_f);
		len_v = dequote_internal_chars(hv, buf + len_n + 1,
						len_t - len_n - 1);
		SM_ASSERT(len_t >= len_n + 1 + len_v + 1);
		len_t = len_n + 1 + len_v + 1;

		/* send it over */
		response = milter_send_command(m, SMFIC_HEADER, buf,
					       len_t, e, state, "header");
		sm_free(buf);
		if (m->mf_state == SMFS_ERROR ||
		    m->mf_state == SMFS_DONE ||
		    *state != SMFIR_CONTINUE)
			break;
	}
	if (MilterLogLevel > 17)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): headers, sent",
			  m->mf_name);
	return response;
}

/*
**  MILTER_BODY -- send the body to a filter
**
**	Parameters:
**		m -- current filter.
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
*/

static char *
milter_body(m, e, state)
	struct milter *m;
	ENVELOPE *e;
	char *state;
{
	char bufchar = '\0';
	char prevchar = '\0';
	int c;
	char *response = NULL;
	char *bp;
	char buf[MILTER_CHUNK_SIZE];

	if (tTd(64, 10))
		sm_dprintf("milter_body\n");

	if (bfrewind(e->e_dfp) < 0)
	{
		ExitStat = EX_IOERR;
		*state = SMFIR_TEMPFAIL;
		syserr("milter_body: %s/%cf%s: rewind error",
		       qid_printqueue(e->e_qgrp, e->e_qdir),
		       DATAFL_LETTER, e->e_id);
		return NULL;
	}

	if (MilterLogLevel > 17)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): body, send",
			  m->mf_name);
	bp = buf;
	while ((c = sm_io_getc(e->e_dfp, SM_TIME_DEFAULT)) != SM_IO_EOF)
	{
		/*  Change LF to CRLF */
		if (c == '\n')
		{
#if !_FFR_MILTER_CONVERT_ALL_LF_TO_CRLF
			/* Not a CRLF already? */
			if (prevchar != '\r')
#endif /* !_FFR_MILTER_CONVERT_ALL_LF_TO_CRLF */
			{
				/* Room for CR now? */
				if (bp + 2 > &buf[sizeof(buf)])
				{
					/* No room, buffer LF */
					bufchar = c;

					/* and send CR now */
					c = '\r';
				}
				else
				{
					/* Room to do it now */
					*bp++ = '\r';
					prevchar = '\r';
				}
			}
		}
		*bp++ = (char) c;
		prevchar = c;
		if (bp >= &buf[sizeof(buf)])
		{
			/* send chunk */
			response = milter_send_command(m, SMFIC_BODY, buf,
						       bp - buf, e, state,
							"body chunk");
			bp = buf;
			if (bufchar != '\0')
			{
				*bp++ = bufchar;
				bufchar = '\0';
				prevchar = bufchar;
			}
		}
		if (m->mf_state == SMFS_ERROR ||
		    m->mf_state == SMFS_DONE ||
		    m->mf_state == SMFS_SKIP ||
		    *state != SMFIR_CONTINUE)
			break;
	}

	/* check for read errors */
	if (sm_io_error(e->e_dfp))
	{
		ExitStat = EX_IOERR;
		if (*state == SMFIR_CONTINUE ||
		    *state == SMFIR_ACCEPT ||
		    m->mf_state == SMFS_SKIP)
		{
			*state = SMFIR_TEMPFAIL;
			if (response != NULL)
			{
				sm_free(response); /* XXX */
				response = NULL;
			}
		}
		syserr("milter_body: %s/%cf%s: read error",
		       qid_printqueue(e->e_qgrp, e->e_qdir),
		       DATAFL_LETTER, e->e_id);
		return response;
	}

	/* send last body chunk */
	if (bp > buf &&
	    m->mf_state != SMFS_ERROR &&
	    m->mf_state != SMFS_DONE &&
	    m->mf_state != SMFS_SKIP &&
	    *state == SMFIR_CONTINUE)
	{
		/* send chunk */
		response = milter_send_command(m, SMFIC_BODY, buf, bp - buf,
					       e, state, "last body chunk");
		bp = buf;
	}
	if (MilterLogLevel > 17)
		sm_syslog(LOG_INFO, e->e_id, "Milter (%s): body, sent",
			  m->mf_name);
	if (m->mf_state == SMFS_SKIP)
	{
		*state = SMFIR_CONTINUE;
		m->mf_state = SMFS_READY;
	}

	return response;
}

/*
**  Actions
*/

/*
**  ADDLEADINGSPACE -- Add a leading space to a string
**
**	Parameters:
**		str -- string
**		rp -- resource pool for allocations
**
**	Returns:
**		pointer to new string
*/

static char *addleadingspace __P((char *, SM_RPOOL_T *));

static char *
addleadingspace(str, rp)
	char *str;
	SM_RPOOL_T *rp;
{
	size_t l;
	char *new;

	SM_ASSERT(str != NULL);
	l = strlen(str);
	SM_ASSERT(l + 2 > l);
	new = sm_rpool_malloc_x(rp, l + 2);
	new[0] = ' ';
	new[1] = '\0';
	sm_strlcpy(new + 1, str, l + 1);
	return new;
}

/*
**  MILTER_ADDHEADER -- Add the supplied header to the message
**
**	Parameters:
**		m -- current filter.
**		response -- encoded form of header/value.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_addheader(m, response, rlen, e)
	struct milter *m;
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	int mh_v_len;
	char *val, *mh_value;
	HDR *h;

	if (tTd(64, 10))
		sm_dprintf("milter_addheader: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (rlen < 2 || strlen(response) + 1 >= (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len %d != rlen %d)\n",
				   (int) strlen(response), (int) (rlen - 1));
		return;
	}

	/* Find separating NUL */
	val = response + strlen(response) + 1;

	/* another sanity check */
	if (strlen(response) + strlen(val) + 2 != (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (part len)\n");
		return;
	}

	if (*response == '\0')
	{
		if (tTd(64, 10))
			sm_dprintf("empty field name\n");
		return;
	}

	for (h = e->e_header; h != NULL; h = h->h_link)
	{
		if (sm_strcasecmp(h->h_field, response) == 0 &&
		    !bitset(H_USER, h->h_flags) &&
		    !bitset(H_TRACE, h->h_flags))
			break;
	}

	mh_v_len = 0;
	mh_value = quote_internal_chars(val, NULL, &mh_v_len);

	/* add to e_msgsize */
	e->e_msgsize += strlen(response) + 2 + strlen(val);

	if (h != NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("Replace default header %s value with %s\n",
				   h->h_field, mh_value);
		if (MilterLogLevel > 8)
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter change: default header %s value with %s",
				  h->h_field, mh_value);
		if (bitset(SMFIP_HDR_LEADSPC, m->mf_pflags))
			h->h_value = mh_value;
		else
		{
			h->h_value = addleadingspace(mh_value, e->e_rpool);
			SM_FREE(mh_value);
		}
		h->h_flags |= H_USER;
	}
	else
	{
		if (tTd(64, 10))
			sm_dprintf("Add %s: %s\n", response, mh_value);
		if (MilterLogLevel > 8)
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter add: header: %s: %s",
				  response, mh_value);
		addheader(newstr(response), mh_value, H_USER, e,
			!bitset(SMFIP_HDR_LEADSPC, m->mf_pflags));
		SM_FREE(mh_value);
	}
}

/*
**  MILTER_INSHEADER -- Insert the supplied header
**
**	Parameters:
**		m -- current filter.
**		response -- encoded form of header/value.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
**
**	Notes:
**		Unlike milter_addheader(), this does not attempt to determine
**		if the header already exists in the envelope, even a
**		deleted version.  It just blindly inserts.
*/

static void
milter_insheader(m, response, rlen, e)
	struct milter *m;
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	mi_int32 idx, i;
	int mh_v_len;
	char *field, *val, *mh_value;

	if (tTd(64, 10))
		sm_dprintf("milter_insheader: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (rlen < 2 || strlen(response) + 1 >= (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len)\n");
		return;
	}

	/* decode */
	(void) memcpy((char *) &i, response, MILTER_LEN_BYTES);
	idx = ntohl(i);
	field = response + MILTER_LEN_BYTES;
	val = field + strlen(field) + 1;

	/* another sanity check */
	if (MILTER_LEN_BYTES + strlen(field) + 1 +
	    strlen(val) + 1 != (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (part len)\n");
		return;
	}

	if (*field == '\0')
	{
		if (tTd(64, 10))
			sm_dprintf("empty field name\n");
		return;
	}

	/* add to e_msgsize */
	e->e_msgsize += strlen(response) + 2 + strlen(val);

	if (tTd(64, 10))
		sm_dprintf("Insert (%d) %s: %s\n", idx, field, val);
	if (MilterLogLevel > 8)
		sm_syslog(LOG_INFO, e->e_id,
			  "Milter insert (%d): header: %s: %s",
			  idx, field, val);
	mh_v_len = 0;
	mh_value = quote_internal_chars(val, NULL, &mh_v_len);
	insheader(idx, newstr(field), mh_value, H_USER, e,
		!bitset(SMFIP_HDR_LEADSPC, m->mf_pflags));
	SM_FREE(mh_value);
}

/*
**  MILTER_CHANGEHEADER -- Change the supplied header in the message
**
**	Parameters:
**		m -- current filter.
**		response -- encoded form of header/index/value.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_changeheader(m, response, rlen, e)
	struct milter *m;
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	mi_int32 i, index;
	int mh_v_len;
	char *field, *val, *mh_value;
	HDR *h, *sysheader;

	if (tTd(64, 10))
		sm_dprintf("milter_changeheader: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (rlen < 2 || strlen(response) + 1 >= (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len)\n");
		return;
	}

	/* Find separating NUL */
	(void) memcpy((char *) &i, response, MILTER_LEN_BYTES);
	index = ntohl(i);
	field = response + MILTER_LEN_BYTES;
	val = field + strlen(field) + 1;

	/* another sanity check */
	if (MILTER_LEN_BYTES + strlen(field) + 1 +
	    strlen(val) + 1 != (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (part len)\n");
		return;
	}

	if (*field == '\0')
	{
		if (tTd(64, 10))
			sm_dprintf("empty field name\n");
		return;
	}

	mh_v_len = 0;
	mh_value = quote_internal_chars(val, NULL, &mh_v_len);

	sysheader = NULL;
	for (h = e->e_header; h != NULL; h = h->h_link)
	{
		if (sm_strcasecmp(h->h_field, field) == 0)
		{
			if (bitset(H_USER, h->h_flags) && --index <= 0)
			{
				sysheader = NULL;
				break;
			}
			else if (!bitset(H_USER, h->h_flags) &&
				 !bitset(H_TRACE, h->h_flags))
			{
				/*
				**  DRUMS msg-fmt draft says can only have
				**  multiple occurences of trace fields,
				**  so make sure we replace any non-trace,
				**  non-user field.
				*/

				sysheader = h;
			}
		}
	}

	/* if not found as user-provided header at index, use sysheader */
	if (h == NULL)
		h = sysheader;

	if (h == NULL)
	{
		if (*val == '\0')
		{
			if (tTd(64, 10))
				sm_dprintf("Delete (noop) %s\n", field);
			if (MilterLogLevel > 8)
				sm_syslog(LOG_INFO, e->e_id,
					"Milter delete (noop): header: %s"
					, field);
		}
		else
		{
			/* treat modify value with no existing header as add */
			if (tTd(64, 10))
				sm_dprintf("Add %s: %s\n", field, mh_value);
			if (MilterLogLevel > 8)
				sm_syslog(LOG_INFO, e->e_id,
					"Milter change (add): header: %s: %s"
					, field, mh_value);
			addheader(newstr(field), mh_value, H_USER, e,
				!bitset(SMFIP_HDR_LEADSPC, m->mf_pflags));
		}
		return;
	}

	if (tTd(64, 10))
	{
		if (*val == '\0')
		{
			sm_dprintf("Delete%s %s:%s\n",
				   h == sysheader ? " (default header)" : "",
				   field,
				   h->h_value == NULL ? "<NULL>" : h->h_value);
		}
		else
		{
			sm_dprintf("Change%s %s: from %s to %s\n",
				   h == sysheader ? " (default header)" : "",
				   field,
				   h->h_value == NULL ? "<NULL>" : h->h_value,
				   mh_value);
		}
	}

	if (MilterLogLevel > 8)
	{
		if (*val == '\0')
		{
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter delete: header%s %s:%s",
				  h == sysheader ? " (default header)" : "",
				  field,
				  h->h_value == NULL ? "<NULL>" : h->h_value);
		}
		else
		{
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter change: header%s %s: from %s to %s",
				  h == sysheader ? " (default header)" : "",
				  field,
				  h->h_value == NULL ? "<NULL>" : h->h_value,
				  mh_value);
		}
	}

	if (h != sysheader && h->h_value != NULL)
	{
		size_t l;

		l = strlen(h->h_value);
		if (l > e->e_msgsize)
			e->e_msgsize = 0;
		else
			e->e_msgsize -= l;
		/* rpool, don't free: sm_free(h->h_value); XXX */
	}

	if (*val == '\0')
	{
		/* Remove "Field: " from message size */
		if (h != sysheader)
		{
			size_t l;

			l = strlen(h->h_field) + 2;
			if (l > e->e_msgsize)
				e->e_msgsize = 0;
			else
				e->e_msgsize -= l;
		}
		h->h_value = NULL;
		SM_FREE(mh_value);
	}
	else
	{
		if (bitset(SMFIP_HDR_LEADSPC, m->mf_pflags))
			h->h_value = mh_value;
		else
		{
			h->h_value = addleadingspace(mh_value, e->e_rpool);
			SM_FREE(mh_value);
		}
		h->h_flags |= H_USER;
		e->e_msgsize += strlen(h->h_value);
	}
}

/*
**  MILTER_SPLIT_RESPONSE -- Split response into fields.
**
**	Parameters:
**		response -- encoded repsonse.
**		rlen -- length of response.
**		pargc -- number of arguments (ouput)
**
**	Returns:
**		array of pointers to the individual strings
*/

static char **milter_split_response __P((char *, ssize_t, int *));

static char **
milter_split_response(response, rlen, pargc)
	char *response;
	ssize_t rlen;
	int *pargc;
{
	char **s;
	size_t i;
	int elem, nelem;

	SM_ASSERT(response != NULL);
	SM_ASSERT(pargc != NULL);
	*pargc = 0;
	if (rlen < 2 || strlen(response) >= (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len %d != rlen %d)\n",
				   (int) strlen(response), (int) (rlen - 1));
		return NULL;
	}

	nelem = 0;
	for (i = 0; i < rlen; i++)
	{
		if (response[i] == '\0')
			++nelem;
	}
	if (nelem == 0)
		return NULL;

	/* last entry is only for the name */
	s = (char **)malloc((nelem + 1) * (sizeof(*s)));
	if (s == NULL)
		return NULL;
	s[0] = response;
	for (i = 0, elem = 0; i < rlen && elem < nelem; i++)
	{
		if (response[i] == '\0')
		{
			++elem;
			if (i + 1 >= rlen)
				s[elem] = NULL;
			else
				s[elem] = &(response[i + 1]);
		}
	}
	*pargc = nelem;

	if (tTd(64, 10))
	{
		for (elem = 0; elem < nelem; elem++)
			sm_dprintf("argv[%d]=\"%s\"\n", elem, s[elem]);
	}

	/* overwrite last entry (already done above, just paranoia) */
	s[elem] = NULL;
	return s;
}

/*
**  MILTER_CHGFROM -- Change the envelope sender address
**
**	Parameters:
**		response -- encoded form of recipient address.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_chgfrom(response, rlen, e)
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	int olderrors, argc;
	char **argv;

	if (tTd(64, 10))
		sm_dprintf("milter_chgfrom: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (*response == '\0' ||
	    strlen(response) + 1 > (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len %d != rlen %d)\n",
				   (int) strlen(response), (int) (rlen - 1));
		return;
	}

	if (tTd(64, 10))
		sm_dprintf("%s\n", response);
	if (MilterLogLevel > 8)
		sm_syslog(LOG_INFO, e->e_id, "Milter chgfrom: %s", response);
	argv = milter_split_response(response, rlen, &argc);
	if (argc < 1 || argc > 2)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol argc=%d\n", argc);
		return;
	}

	olderrors = Errors;
	setsender(argv[0], e, NULL, '\0', false);
	if (argc == 2)
	{
		reset_mail_esmtp_args(e);

		/*
		**  need "features" here: how to get those? via e?
		**  "fake" it for now: allow everything.
		*/

		parse_esmtp_args(e, NULL, argv[0], argv[1], "MAIL", NULL,
				mail_esmtp_args);
	}
	Errors = olderrors;
	return;
}

/*
**  MILTER_ADDRCPT_PAR -- Add the supplied recipient to the message
**
**	Parameters:
**		response -- encoded form of recipient address.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_addrcpt_par(response, rlen, e)
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	int olderrors, argc;
	char *delimptr;
	char **argv;
	ADDRESS *a;

	if (tTd(64, 10))
		sm_dprintf("milter_addrcpt_par: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (tTd(64, 10))
		sm_dprintf("%s\n", response);
	if (MilterLogLevel > 8)
		sm_syslog(LOG_INFO, e->e_id, "Milter add: rcpt: %s", response);

	argv = milter_split_response(response, rlen, &argc);
	if (argc < 1 || argc > 2)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol argc=%d\n", argc);
		return;
	}
	olderrors = Errors;

	/* how to set ESMTP arguments? */
	a = parseaddr(argv[0], NULLADDR, RF_COPYALL, ' ', &delimptr, e, true);

	if (a != NULL && olderrors == Errors)
	{
		parse_esmtp_args(e, a, argv[0], argv[1], "RCPT", NULL,
				rcpt_esmtp_args);
		if (olderrors == Errors)
			a = recipient(a, &e->e_sendqueue, 0, e);
		else
			sm_dprintf("olderrors=%d, Errors=%d\n",
				olderrors, Errors);
	}
	else
	{
		sm_dprintf("a=%p, olderrors=%d, Errors=%d\n",
			a, olderrors, Errors);
	}

	Errors = olderrors;
	return;
}

/*
**  MILTER_ADDRCPT -- Add the supplied recipient to the message
**
**	Parameters:
**		response -- encoded form of recipient address.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_addrcpt(response, rlen, e)
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	int olderrors;

	if (tTd(64, 10))
		sm_dprintf("milter_addrcpt: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (*response == '\0' ||
	    strlen(response) + 1 != (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len %d != rlen %d)\n",
				   (int) strlen(response), (int) (rlen - 1));
		return;
	}

	if (tTd(64, 10))
		sm_dprintf("%s\n", response);
	if (MilterLogLevel > 8)
		sm_syslog(LOG_INFO, e->e_id, "Milter add: rcpt: %s", response);
	olderrors = Errors;
	(void) sendtolist(response, NULLADDR, &e->e_sendqueue, 0, e);
	Errors = olderrors;
	return;
}

/*
**  MILTER_DELRCPT -- Delete the supplied recipient from the message
**
**	Parameters:
**		response -- encoded form of recipient address.
**		rlen -- length of response.
**		e -- current envelope.
**
**	Returns:
**		none
*/

static void
milter_delrcpt(response, rlen, e)
	char *response;
	ssize_t rlen;
	ENVELOPE *e;
{
	if (tTd(64, 10))
		sm_dprintf("milter_delrcpt: ");

	/* sanity checks */
	if (response == NULL)
	{
		if (tTd(64, 10))
			sm_dprintf("NULL response\n");
		return;
	}

	if (*response == '\0' ||
	    strlen(response) + 1 != (size_t) rlen)
	{
		if (tTd(64, 10))
			sm_dprintf("didn't follow protocol (total len %d != rlen %d)\n",
				   (int) strlen(response), (int) (rlen - 1));
		return;
	}

	if (tTd(64, 10))
		sm_dprintf("%s\n", response);
	if (MilterLogLevel > 8)
		sm_syslog(LOG_INFO, e->e_id, "Milter delete: rcpt %s",
			  response);
	(void) removefromlist(response, &e->e_sendqueue, e);
	return;
}

/*
**  MILTER_REPLBODY -- Replace the current data file with new body
**
**	Parameters:
**		response -- encoded form of new body.
**		rlen -- length of response.
**		newfilter -- if first time called by a new filter
**		e -- current envelope.
**
**	Returns:
**		0 upon success, -1 upon failure
*/

static int
milter_replbody(response, rlen, newfilter, e)
	char *response;
	ssize_t rlen;
	bool newfilter;
	ENVELOPE *e;
{
	static char prevchar;
	int i;

	if (tTd(64, 10))
		sm_dprintf("milter_replbody\n");

	/* If a new filter, reset previous character and truncate data file */
	if (newfilter)
	{
		off_t prevsize;
		char dfname[MAXPATHLEN];

		(void) sm_strlcpy(dfname, queuename(e, DATAFL_LETTER),
				  sizeof(dfname));

		/* Reset prevchar */
		prevchar = '\0';

		/* Get the current data file information */
		prevsize = sm_io_getinfo(e->e_dfp, SM_IO_WHAT_SIZE, NULL);
		if (prevsize < 0)
			prevsize = 0;

		/* truncate current data file */
		if (sm_io_getinfo(e->e_dfp, SM_IO_WHAT_ISTYPE, BF_FILE_TYPE))
		{
			if (sm_io_setinfo(e->e_dfp, SM_BF_TRUNCATE, NULL) < 0)
			{
				MILTER_DF_ERROR("milter_replbody: sm_io truncate %s: %s");
				return -1;
			}
		}
		else
		{
			int err;

			err = sm_io_error(e->e_dfp);
			(void) sm_io_flush(e->e_dfp, SM_TIME_DEFAULT);

			/*
			**  Clear error if tried to fflush()
			**  a read-only file pointer and
			**  there wasn't a previous error.
			*/

			if (err == 0)
				sm_io_clearerr(e->e_dfp);

			/* errno is set implicitly by fseek() before return */
			err = sm_io_seek(e->e_dfp, SM_TIME_DEFAULT,
					 0, SEEK_SET);
			if (err < 0)
			{
				MILTER_DF_ERROR("milter_replbody: sm_io_seek %s: %s");
				return -1;
			}
# if NOFTRUNCATE
			/* XXX: Not much we can do except rewind it */
			errno = EINVAL;
			MILTER_DF_ERROR("milter_replbody: ftruncate not available on this platform (%s:%s)");
			return -1;
# else /* NOFTRUNCATE */
			err = ftruncate(sm_io_getinfo(e->e_dfp,
						      SM_IO_WHAT_FD, NULL),
					0);
			if (err < 0)
			{
				MILTER_DF_ERROR("milter_replbody: sm_io ftruncate %s: %s");
				return -1;
			}
# endif /* NOFTRUNCATE */
		}

		if (prevsize > e->e_msgsize)
			e->e_msgsize = 0;
		else
			e->e_msgsize -= prevsize;
	}

	if (newfilter && MilterLogLevel > 8)
		sm_syslog(LOG_INFO, e->e_id, "Milter message: body replaced");

	if (response == NULL)
	{
		/* Flush the buffered '\r' */
		if (prevchar == '\r')
		{
			(void) sm_io_putc(e->e_dfp, SM_TIME_DEFAULT, prevchar);
			e->e_msgsize++;
		}
		return 0;
	}

	for (i = 0; i < rlen; i++)
	{
		/* Buffered char from last chunk */
		if (i == 0 && prevchar == '\r')
		{
			/* Not CRLF, output prevchar */
			if (response[i] != '\n')
			{
				(void) sm_io_putc(e->e_dfp, SM_TIME_DEFAULT,
						  prevchar);
				e->e_msgsize++;
			}
			prevchar = '\0';
		}

		/* Turn CRLF into LF */
		if (response[i] == '\r')
		{
			/* check if at end of chunk */
			if (i + 1 < rlen)
			{
				/* If LF, strip CR */
				if (response[i + 1] == '\n')
					i++;
			}
			else
			{
				/* check next chunk */
				prevchar = '\r';
				continue;
			}
		}
		(void) sm_io_putc(e->e_dfp, SM_TIME_DEFAULT, response[i]);
		e->e_msgsize++;
	}
	return 0;
}

/*
**  MTA callouts
*/

/*
**  MILTER_INIT -- open and negotiate with all of the filters
**
**	Parameters:
**		e -- current envelope.
**		state -- return state from response.
**		milters -- milters structure.
**
**	Returns:
**		true iff at least one filter is active
*/

/* ARGSUSED */
bool
milter_init(e, state, milters)
	ENVELOPE *e;
	char *state;
	milters_T *milters;
{
	int i;

	if (tTd(64, 10))
		sm_dprintf("milter_init\n");

	memset(milters, '\0', sizeof(*milters));
	*state = SMFIR_CONTINUE;
	if (InputFilters[0] == NULL)
	{
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter: no active filter");
		return false;
	}

	for (i = 0; InputFilters[i] != NULL; i++)
	{
		struct milter *m = InputFilters[i];

		m->mf_sock = milter_open(m, false, e);
		if (m->mf_state == SMFS_ERROR)
		{
			MILTER_CHECK_ERROR(true, continue);
			break;
		}

		if (m->mf_sock < 0 ||
		    milter_negotiate(m, e, milters) < 0 ||
		    m->mf_state == SMFS_ERROR)
		{
			if (tTd(64, 5))
				sm_dprintf("milter_init(%s): failed to %s\n",
					   m->mf_name,
					   m->mf_sock < 0 ? "open" :
							    "negotiate");
			if (MilterLogLevel > 0)
				sm_syslog(LOG_ERR, e->e_id,
					  "Milter (%s): init failed to %s",
					  m->mf_name,
					  m->mf_sock < 0 ? "open" :
							   "negotiate");

			/* if negotiation failure, close socket */
			milter_error(m, e);
			MILTER_CHECK_ERROR(true, continue);
			continue;
		}
		if (MilterLogLevel > 9)
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter (%s): init success to %s",
				  m->mf_name,
				  m->mf_sock < 0 ? "open" : "negotiate");
	}

	/*
	**  If something temp/perm failed with one of the filters,
	**  we won't be using any of them, so clear any existing
	**  connections.
	*/

	if (*state != SMFIR_CONTINUE)
		milter_quit(e);

	return true;
}

/*
**  MILTER_CONNECT -- send connection info to milter filters
**
**	Parameters:
**		hostname -- hostname of remote machine.
**		addr -- address of remote machine.
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
*/

char *
milter_connect(hostname, addr, e, state)
	char *hostname;
	SOCKADDR addr;
	ENVELOPE *e;
	char *state;
{
	char family;
	unsigned short port;
	char *buf, *bp;
	char *response;
	char *sockinfo = NULL;
	ssize_t s;
# if NETINET6
	char buf6[INET6_ADDRSTRLEN];
# endif /* NETINET6 */

	if (tTd(64, 10))
		sm_dprintf("milter_connect(%s)\n", hostname);
	if (MilterLogLevel > 9)
		sm_syslog(LOG_INFO, e->e_id, "Milter: connect to filters");

	/* gather data */
	switch (addr.sa.sa_family)
	{
# if NETUNIX
	  case AF_UNIX:
		family = SMFIA_UNIX;
		port = htons(0);
		sockinfo = addr.sunix.sun_path;
		break;
# endif /* NETUNIX */

# if NETINET
	  case AF_INET:
		family = SMFIA_INET;
		port = addr.sin.sin_port;
		sockinfo = (char *) inet_ntoa(addr.sin.sin_addr);
		break;
# endif /* NETINET */

# if NETINET6
	  case AF_INET6:
		if (IN6_IS_ADDR_V4MAPPED(&addr.sin6.sin6_addr))
			family = SMFIA_INET;
		else
			family = SMFIA_INET6;
		port = addr.sin6.sin6_port;
		sockinfo = anynet_ntop(&addr.sin6.sin6_addr, buf6,
				       sizeof(buf6));
		if (sockinfo == NULL)
			sockinfo = "";
		break;
# endif /* NETINET6 */

	  default:
		family = SMFIA_UNKNOWN;
		break;
	}

	s = strlen(hostname) + 1 + sizeof(family);
	if (family != SMFIA_UNKNOWN)
		s += sizeof(port) + strlen(sockinfo) + 1;

	buf = (char *) xalloc(s);
	bp = buf;

	/* put together data */
	(void) memcpy(bp, hostname, strlen(hostname));
	bp += strlen(hostname);
	*bp++ = '\0';
	(void) memcpy(bp, &family, sizeof(family));
	bp += sizeof(family);
	if (family != SMFIA_UNKNOWN)
	{
		(void) memcpy(bp, &port, sizeof(port));
		bp += sizeof(port);

		/* include trailing '\0' */
		(void) memcpy(bp, sockinfo, strlen(sockinfo) + 1);
	}

	response = milter_command(SMFIC_CONNECT, buf, s, SMFIM_CONNECT,
				e, state, "connect", false);
	sm_free(buf); /* XXX */

	/*
	**  If this message connection is done for,
	**  close the filters.
	*/

	if (*state != SMFIR_CONTINUE)
	{
		if (MilterLogLevel > 9)
			sm_syslog(LOG_INFO, e->e_id, "Milter: connect, ending");
		milter_quit(e);
	}
	else
		milter_per_connection_check(e);

#if !_FFR_MILTER_CONNECT_REPLYCODE
	/*
	**  SMFIR_REPLYCODE can't work with connect due to
	**  the requirements of SMTP.  Therefore, ignore the
	**  reply code text but keep the state it would reflect.
	*/

	if (*state == SMFIR_REPLYCODE)
	{
		if (response != NULL &&
		    *response == '4')
		{
			if (strncmp(response, "421 ", 4) == 0)
				*state = SMFIR_SHUTDOWN;
			else
				*state = SMFIR_TEMPFAIL;
		}
		else
			*state = SMFIR_REJECT;
		if (response != NULL)
		{
			sm_free(response); /* XXX */
			response = NULL;
		}
	}
#endif /* !_FFR_MILTER_CONNECT_REPLYCODE */
	return response;
}

/*
**  MILTER_HELO -- send SMTP HELO/EHLO command info to milter filters
**
**	Parameters:
**		helo -- argument to SMTP HELO/EHLO command.
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
*/

char *
milter_helo(helo, e, state)
	char *helo;
	ENVELOPE *e;
	char *state;
{
	int i;
	char *response;

	if (tTd(64, 10))
		sm_dprintf("milter_helo(%s)\n", helo);

	/* HELO/EHLO can come at any point */
	for (i = 0; InputFilters[i] != NULL; i++)
	{
		struct milter *m = InputFilters[i];

		switch (m->mf_state)
		{
		  case SMFS_INMSG:
			/* abort in message filters */
			milter_abort_filter(m, e);
			/* FALLTHROUGH */

		  case SMFS_DONE:
			/* reset done filters */
			m->mf_state = SMFS_OPEN;
			break;
		}
	}

	response = milter_command(SMFIC_HELO, helo, strlen(helo) + 1,
				  SMFIM_HELO, e, state, "helo", false);
	milter_per_connection_check(e);
	return response;
}

/*
**  MILTER_ENVFROM -- send SMTP MAIL command info to milter filters
**
**	Parameters:
**		args -- SMTP MAIL command args (args[0] == sender).
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
*/

char *
milter_envfrom(args, e, state)
	char **args;
	ENVELOPE *e;
	char *state;
{
	int i;
	char *buf, *bp;
	char *response;
	ssize_t s;

	if (tTd(64, 10))
	{
		sm_dprintf("milter_envfrom:");
		for (i = 0; args[i] != NULL; i++)
			sm_dprintf(" %s", args[i]);
		sm_dprintf("\n");
	}

	/* sanity check */
	if (args[0] == NULL)
	{
		*state = SMFIR_REJECT;
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id,
				  "Milter: reject, no sender");
		return NULL;
	}

	/* new message, so ... */
	for (i = 0; InputFilters[i] != NULL; i++)
	{
		struct milter *m = InputFilters[i];

		switch (m->mf_state)
		{
		  case SMFS_INMSG:
			/* abort in message filters */
			milter_abort_filter(m, e);
			/* FALLTHROUGH */

		  case SMFS_DONE:
			/* reset done filters */
			m->mf_state = SMFS_OPEN;
			break;
		}
	}

	/* put together data */
	s = 0;
	for (i = 0; args[i] != NULL; i++)
		s += strlen(args[i]) + 1;

	if (s < 0)
	{
		*state = SMFIR_TEMPFAIL;
		return NULL;
	}

	buf = (char *) xalloc(s);
	bp = buf;
	for (i = 0; args[i] != NULL; i++)
	{
		(void) sm_strlcpy(bp, args[i], s - (bp - buf));
		bp += strlen(bp) + 1;
	}

	if (MilterLogLevel > 14)
		sm_syslog(LOG_INFO, e->e_id, "Milter: sender: %s", buf);

	/* send it over */
	response = milter_command(SMFIC_MAIL, buf, s, SMFIM_ENVFROM,
				e, state, "mail", false);
	sm_free(buf); /* XXX */

	/*
	**  If filter rejects/discards a per message command,
	**  abort the other filters since we are done with the
	**  current message.
	*/

	MILTER_CHECK_DONE_MSG();
	if (MilterLogLevel > 10 && *state == SMFIR_REJECT)
		sm_syslog(LOG_INFO, e->e_id, "Milter: reject, sender");
	return response;
}

/*
**  MILTER_ENVRCPT -- send SMTP RCPT command info to milter filters
**
**	Parameters:
**		args -- SMTP MAIL command args (args[0] == recipient).
**		e -- current envelope.
**		state -- return state from response.
**		rcpt_error -- does RCPT have an error?
**
**	Returns:
**		response string (may be NULL)
*/

char *
milter_envrcpt(args, e, state, rcpt_error)
	char **args;
	ENVELOPE *e;
	char *state;
	bool rcpt_error;
{
	int i;
	char *buf, *bp;
	char *response;
	ssize_t s;

	if (tTd(64, 10))
	{
		sm_dprintf("milter_envrcpt:");
		for (i = 0; args[i] != NULL; i++)
			sm_dprintf(" %s", args[i]);
		sm_dprintf("\n");
	}

	/* sanity check */
	if (args[0] == NULL)
	{
		*state = SMFIR_REJECT;
		if (MilterLogLevel > 10)
			sm_syslog(LOG_INFO, e->e_id, "Milter: reject, no rcpt");
		return NULL;
	}

	/* put together data */
	s = 0;
	for (i = 0; args[i] != NULL; i++)
		s += strlen(args[i]) + 1;

	if (s < 0)
	{
		*state = SMFIR_TEMPFAIL;
		return NULL;
	}

	buf = (char *) xalloc(s);
	bp = buf;
	for (i = 0; args[i] != NULL; i++)
	{
		(void) sm_strlcpy(bp, args[i], s - (bp - buf));
		bp += strlen(bp) + 1;
	}

	if (MilterLogLevel > 14)
		sm_syslog(LOG_INFO, e->e_id, "Milter: rcpts: %s", buf);

	/* send it over */
	response = milter_command(SMFIC_RCPT, buf, s, SMFIM_ENVRCPT,
				e, state, "rcpt", rcpt_error);
	sm_free(buf); /* XXX */
	return response;
}

/*
**  MILTER_DATA_CMD -- send SMTP DATA command info to milter filters
**
**	Parameters:
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
*/

char *
milter_data_cmd(e, state)
	ENVELOPE *e;
	char *state;
{
	if (tTd(64, 10))
		sm_dprintf("milter_data_cmd\n");

	/* send it over */
	return milter_command(SMFIC_DATA, NULL, 0, SMFIM_DATA,
				e, state, "data", false);
}

/*
**  MILTER_DATA -- send message headers/body and gather final message results
**
**	Parameters:
**		e -- current envelope.
**		state -- return state from response.
**
**	Returns:
**		response string (may be NULL)
**
**	Side effects:
**		- Uses e->e_dfp for access to the body
**		- Can call the various milter action routines to
**		  modify the envelope or message.
*/

/* flow through code using continue; don't wrap in do {} while */
# define MILTER_CHECK_RESULTS() \
	if (m->mf_state == SMFS_ERROR && *state == SMFIR_CONTINUE) \
	{ \
			MILTER_SET_STATE;	\
	} \
	if (*state == SMFIR_ACCEPT || \
	    m->mf_state == SMFS_DONE || \
	    m->mf_state == SMFS_ERROR) \
	{ \
		if (m->mf_state != SMFS_ERROR) \
			m->mf_state = SMFS_DONE; \
		continue;	/* to next filter */ \
	} \
	if (*state != SMFIR_CONTINUE) \
	{ \
		m->mf_state = SMFS_DONE; \
		goto finishup; \
	}

char *
milter_data(e, state)
	ENVELOPE *e;
	char *state;
{
	bool replbody = false;		/* milter_replbody() called? */
	bool replfailed = false;	/* milter_replbody() failed? */
	bool rewind = false;		/* rewind data file? */
	bool dfopen = false;		/* data file open for writing? */
	bool newfilter;			/* reset on each new filter */
	char rcmd;
	int i;
	int save_errno;
	char *response = NULL;
	time_t eomsent;
	ssize_t rlen;

	if (tTd(64, 10))
		sm_dprintf("milter_data\n");

	*state = SMFIR_CONTINUE;

	/*
	**  XXX: Should actually send body chunks to each filter
	**  a chunk at a time instead of sending the whole body to
	**  each filter in turn.  However, only if the filters don't
	**  change the body.
	*/

	for (i = 0; InputFilters[i] != NULL; i++)
	{
		int idx;
		char **macros;
		struct milter *m = InputFilters[i];

		if (*state != SMFIR_CONTINUE &&
		    *state != SMFIR_ACCEPT)
		{
			/*
			**  A previous filter has dealt with the message,
			**  safe to stop processing the filters.
			*/

			break;
		}

		/* Now reset state for later evaluation */
		*state = SMFIR_CONTINUE;
		newfilter = true;

		/* previous problem? */
		if (m->mf_state == SMFS_ERROR)
		{
			MILTER_CHECK_ERROR(false, continue);
			break;
		}

		/* sanity checks */
		if (m->mf_sock < 0 ||
		    (m->mf_state != SMFS_OPEN && m->mf_state != SMFS_INMSG))
			continue;

		m->mf_state = SMFS_INMSG;

		/* check if filter wants the headers */
		if (!bitset(SMFIP_NOHDRS, m->mf_pflags))
		{
			response = milter_headers(m, e, state);
			MILTER_CHECK_RESULTS();
		}

		/* check if filter wants EOH */
		if (!bitset(SMFIP_NOEOH, m->mf_pflags))
		{
			if (tTd(64, 10))
				sm_dprintf("milter_data: eoh\n");

			if ((m->mf_lflags & MI_LFLAGS_SYM(SMFIM_EOH)) != 0)
				idx = m->mf_idx;
			else
				idx = 0;
			SM_ASSERT(idx >= 0 && idx <= MAXFILTERS);
			macros = MilterMacros[SMFIM_EOH][idx];

			if (macros != NULL)
			{
				milter_send_macros(m, macros, SMFIC_EOH, e);
				MILTER_CHECK_RESULTS();
			}

			/* send it over */
			response = milter_send_command(m, SMFIC_EOH, NULL, 0,
						       e, state, "eoh");
			MILTER_CHECK_RESULTS();
		}

		/* check if filter wants the body */
		if (!bitset(SMFIP_NOBODY, m->mf_pflags) &&
		    e->e_dfp != NULL)
		{
			rewind = true;
			response = milter_body(m, e, state);
			MILTER_CHECK_RESULTS();
		}

		if ((m->mf_lflags & MI_LFLAGS_SYM(SMFIM_EOH)) != 0)
			idx = m->mf_idx;
		else
			idx = 0;
		SM_ASSERT(idx >= 0 && idx <= MAXFILTERS);
		macros = MilterMacros[SMFIM_EOM][idx];
		if (macros != NULL)
		{
			milter_send_macros(m, macros, SMFIC_BODYEOB, e);
			MILTER_CHECK_RESULTS();
		}

		/* send the final body chunk */
		(void) milter_write(m, SMFIC_BODYEOB, NULL, 0,
				    m->mf_timeout[SMFTO_WRITE], e, "eom");

		/* Get time EOM sent for timeout */
		eomsent = curtime();

		/* deal with the possibility of multiple responses */
		while (*state == SMFIR_CONTINUE)
		{
			/* Check total timeout from EOM to final ACK/NAK */
			if (m->mf_timeout[SMFTO_EOM] > 0 &&
			    curtime() - eomsent >= m->mf_timeout[SMFTO_EOM])
			{
				if (tTd(64, 5))
					sm_dprintf("milter_data(%s): EOM ACK/NAK timeout\n",
						m->mf_name);
				if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "milter_data(%s): EOM ACK/NAK timeout",
						  m->mf_name);
				milter_error(m, e);
				MILTER_CHECK_ERROR(false, break);
				break;
			}

			response = milter_read(m, &rcmd, &rlen,
					       m->mf_timeout[SMFTO_READ], e,
						"eom");
			if (m->mf_state == SMFS_ERROR)
				break;

			if (tTd(64, 10))
				sm_dprintf("milter_data(%s): state %c\n",
					   m->mf_name, (char) rcmd);

			switch (rcmd)
			{
			  case SMFIR_REPLYCODE:
				MILTER_CHECK_REPLYCODE("554 5.7.1 Command rejected");
				if (MilterLogLevel > 12)
					sm_syslog(LOG_INFO, e->e_id, "milter=%s, reject=%s",
						  m->mf_name, response);
				*state = rcmd;
				m->mf_state = SMFS_DONE;
				break;

			  case SMFIR_REJECT: /* log msg at end of function */
				if (MilterLogLevel > 12)
					sm_syslog(LOG_INFO, e->e_id, "milter=%s, reject",
						  m->mf_name);
				*state = rcmd;
				m->mf_state = SMFS_DONE;
				break;

			  case SMFIR_DISCARD:
				if (MilterLogLevel > 12)
					sm_syslog(LOG_INFO, e->e_id, "milter=%s, discard",
						  m->mf_name);
				*state = rcmd;
				m->mf_state = SMFS_DONE;
				break;

			  case SMFIR_TEMPFAIL:
				if (MilterLogLevel > 12)
					sm_syslog(LOG_INFO, e->e_id, "milter=%s, tempfail",
						  m->mf_name);
				*state = rcmd;
				m->mf_state = SMFS_DONE;
				break;

			  case SMFIR_CONTINUE:
			  case SMFIR_ACCEPT:
				/* this filter is done with message */
				if (replfailed)
					*state = SMFIR_TEMPFAIL;
				else
					*state = SMFIR_ACCEPT;
				m->mf_state = SMFS_DONE;
				break;

			  case SMFIR_PROGRESS:
				break;

			  case SMFIR_QUARANTINE:
				if (!bitset(SMFIF_QUARANTINE, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s): lied about quarantining, honoring request anyway",
							  m->mf_name);
				}
				if (response == NULL)
					response = newstr("");
				if (MilterLogLevel > 3)
					sm_syslog(LOG_INFO, e->e_id,
						  "milter=%s, quarantine=%s",
						  m->mf_name, response);
				e->e_quarmsg = sm_rpool_strdup_x(e->e_rpool,
								 response);
				macdefine(&e->e_macro, A_PERM,
					  macid("{quarantine}"), e->e_quarmsg);
				break;

			  case SMFIR_ADDHEADER:
				if (!bitset(SMFIF_ADDHDRS, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s): lied about adding headers, honoring request anyway",
							  m->mf_name);
				}
				milter_addheader(m, response, rlen, e);
				break;

			  case SMFIR_INSHEADER:
				if (!bitset(SMFIF_ADDHDRS, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s): lied about adding headers, honoring request anyway",
							  m->mf_name);
				}
				milter_insheader(m, response, rlen, e);
				break;

			  case SMFIR_CHGHEADER:
				if (!bitset(SMFIF_CHGHDRS, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s): lied about changing headers, honoring request anyway",
							  m->mf_name);
				}
				milter_changeheader(m, response, rlen, e);
				break;

			  case SMFIR_CHGFROM:
				if (!bitset(SMFIF_CHGFROM, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s) lied about changing sender, honoring request anyway",
							  m->mf_name);
				}
				milter_chgfrom(response, rlen, e);
				break;

			  case SMFIR_ADDRCPT:
				if (!bitset(SMFIF_ADDRCPT, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s) lied about adding recipients, honoring request anyway",
							  m->mf_name);
				}
				milter_addrcpt(response, rlen, e);
				break;

			  case SMFIR_ADDRCPT_PAR:
				if (!bitset(SMFIF_ADDRCPT_PAR, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s) lied about adding recipients with parameters, honoring request anyway",
							  m->mf_name);
				}
				milter_addrcpt_par(response, rlen, e);
				break;

			  case SMFIR_DELRCPT:
				if (!bitset(SMFIF_DELRCPT, m->mf_fflags))
				{
					if (MilterLogLevel > 9)
						sm_syslog(LOG_WARNING, e->e_id,
							  "milter_data(%s): lied about removing recipients, honoring request anyway",
							  m->mf_name);
				}
				milter_delrcpt(response, rlen, e);
				break;

			  case SMFIR_REPLBODY:
				if (!bitset(SMFIF_MODBODY, m->mf_fflags))
				{
					if (MilterLogLevel > 0)
						sm_syslog(LOG_ERR, e->e_id,
							  "milter_data(%s): lied about replacing body, rejecting request and tempfailing message",
							  m->mf_name);
					replfailed = true;
					break;
				}

				/* already failed in attempt */
				if (replfailed)
					break;

				if (!dfopen)
				{
					if (milter_reopen_df(e) < 0)
					{
						replfailed = true;
						break;
					}
					dfopen = true;
					rewind = true;
				}

				if (milter_replbody(response, rlen,
						    newfilter, e) < 0)
					replfailed = true;
				newfilter = false;
				replbody = true;
				break;

			  default:
				/* Invalid response to command */
				if (MilterLogLevel > 0)
					sm_syslog(LOG_ERR, e->e_id,
						  "milter_data(%s): returned bogus response %c",
						  m->mf_name, rcmd);
				milter_error(m, e);
				break;
			}
			if (rcmd != SMFIR_REPLYCODE && response != NULL)
			{
				sm_free(response); /* XXX */
				response = NULL;
			}

			if (m->mf_state == SMFS_ERROR)
				break;
		}

		if (replbody && !replfailed)
		{
			/* flush possible buffered character */
			milter_replbody(NULL, 0, !replbody, e);
			replbody = false;
		}

		if (m->mf_state == SMFS_ERROR)
		{
			MILTER_CHECK_ERROR(false, continue);
			goto finishup;
		}
	}

finishup:
	/* leave things in the expected state if we touched it */
	if (replfailed)
	{
		if (*state == SMFIR_CONTINUE ||
		    *state == SMFIR_ACCEPT)
		{
			*state = SMFIR_TEMPFAIL;
			SM_FREE_CLR(response);
		}

		if (dfopen)
		{
			(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
			e->e_dfp = NULL;
			e->e_flags &= ~EF_HAS_DF;
			dfopen = false;
		}
		rewind = false;
	}

	if ((dfopen && milter_reset_df(e) < 0) ||
	    (rewind && bfrewind(e->e_dfp) < 0))
	{
		save_errno = errno;
		ExitStat = EX_IOERR;

		/*
		**  If filter told us to keep message but we had
		**  an error, we can't really keep it, tempfail it.
		*/

		if (*state == SMFIR_CONTINUE ||
		    *state == SMFIR_ACCEPT)
		{
			*state = SMFIR_TEMPFAIL;
			SM_FREE_CLR(response);
		}

		errno = save_errno;
		syserr("milter_data: %s/%cf%s: read error",
		       qid_printqueue(e->e_qgrp, e->e_qdir),
		       DATAFL_LETTER, e->e_id);
	}

	MILTER_CHECK_DONE_MSG();
	if (MilterLogLevel > 10 && *state == SMFIR_REJECT)
		sm_syslog(LOG_INFO, e->e_id, "Milter: reject, data");
	return response;
}

/*
**  MILTER_UNKNOWN -- send any unrecognized or unimplemented command
**			string to milter filters
**
**	Parameters:
**		smtpcmd -- the string itself.
**		e -- current envelope.
**		state -- return state from response.
**
**
**	Returns:
**		response string (may be NULL)
*/

char *
milter_unknown(smtpcmd, e, state)
	char *smtpcmd;
	ENVELOPE *e;
	char *state;
{
	if (tTd(64, 10))
		sm_dprintf("milter_unknown(%s)\n", smtpcmd);

	return milter_command(SMFIC_UNKNOWN, smtpcmd, strlen(smtpcmd) + 1,
				SMFIM_NOMACROS, e, state, "unknown", false);
}

/*
**  MILTER_QUIT -- informs the filter(s) we are done and closes connection(s)
**
**	Parameters:
**		e -- current envelope.
**
**	Returns:
**		none
*/

void
milter_quit(e)
	ENVELOPE *e;
{
	int i;

	if (tTd(64, 10))
		sm_dprintf("milter_quit(%s)\n", e->e_id);

	for (i = 0; InputFilters[i] != NULL; i++)
		milter_quit_filter(InputFilters[i], e);
}

/*
**  MILTER_ABORT -- informs the filter(s) that we are aborting current message
**
**	Parameters:
**		e -- current envelope.
**
**	Returns:
**		none
*/

void
milter_abort(e)
	ENVELOPE *e;
{
	int i;

	if (tTd(64, 10))
		sm_dprintf("milter_abort\n");

	for (i = 0; InputFilters[i] != NULL; i++)
	{
		struct milter *m = InputFilters[i];

		/* sanity checks */
		if (m->mf_sock < 0 || m->mf_state != SMFS_INMSG)
			continue;

		milter_abort_filter(m, e);
	}
}
#endif /* MILTER */
