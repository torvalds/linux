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
#include "map.h"

#if USERDB
SM_RCSID("@(#)$Id: udb.c,v 8.166 2013-11-22 20:51:57 ca Exp $ (with USERDB)")
#else /* USERDB */
SM_RCSID("@(#)$Id: udb.c,v 8.166 2013-11-22 20:51:57 ca Exp $ (without USERDB)")
#endif /* USERDB */

#if USERDB

#include <sm/sendmail.h>
# if NEWDB
#  include "sm/bdb.h"
# else /* NEWDB */
#  define DBT	struct _data_base_thang_
DBT
{
	void	*data;		/* pointer to data */
	size_t	size;		/* length of data */
};
# endif /* NEWDB */

/*
**  UDB.C -- interface between sendmail and Berkeley User Data Base.
**
**	This depends on the 4.4BSD db package.
*/


struct udbent
{
	char	*udb_spec;		/* string version of spec */
	int	udb_type;		/* type of entry */
	pid_t	udb_pid;		/* PID of process which opened db */
	char	*udb_default;		/* default host for outgoing mail */
	union
	{
# if NETINET || NETINET6
		/* type UE_REMOTE -- do remote call for lookup */
		struct
		{
			SOCKADDR	_udb_addr;	/* address */
			int		_udb_timeout;	/* timeout */
		} udb_remote;
#  define udb_addr	udb_u.udb_remote._udb_addr
#  define udb_timeout	udb_u.udb_remote._udb_timeout
# endif /* NETINET || NETINET6 */

		/* type UE_FORWARD -- forward message to remote */
		struct
		{
			char	*_udb_fwdhost;	/* name of forward host */
		} udb_forward;
# define udb_fwdhost	udb_u.udb_forward._udb_fwdhost

# if NEWDB
		/* type UE_FETCH -- lookup in local database */
		struct
		{
			char	*_udb_dbname;	/* pathname of database */
			DB	*_udb_dbp;	/* open database ptr */
		} udb_lookup;
#  define udb_dbname	udb_u.udb_lookup._udb_dbname
#  define udb_dbp	udb_u.udb_lookup._udb_dbp
# endif /* NEWDB */
	} udb_u;
};

# define UDB_EOLIST	0	/* end of list */
# define UDB_SKIP	1	/* skip this entry */
# define UDB_REMOTE	2	/* look up in remote database */
# define UDB_DBFETCH	3	/* look up in local database */
# define UDB_FORWARD	4	/* forward to remote host */
# define UDB_HESIOD	5	/* look up via hesiod */

# define MAXUDBENT	10	/* maximum number of UDB entries */


struct udb_option
{
	char	*udbo_name;
	char	*udbo_val;
};

# if HESIOD
static int	hes_udb_get __P((DBT *, DBT *));
# endif /* HESIOD */
static char	*udbmatch __P((char *, char *, SM_RPOOL_T *));
static int	_udbx_init __P((ENVELOPE *));
static int	_udb_parsespec __P((char *, struct udb_option [], int));

/*
**  UDBEXPAND -- look up user in database and expand
**
**	Parameters:
**		a -- address to expand.
**		sendq -- pointer to head of sendq to put the expansions in.
**		aliaslevel -- the current alias nesting depth.
**		e -- the current envelope.
**
**	Returns:
**		EX_TEMPFAIL -- if something "odd" happened -- probably due
**			to accessing a file on an NFS server that is down.
**		EX_OK -- otherwise.
**
**	Side Effects:
**		Modifies sendq.
*/

static struct udbent	UdbEnts[MAXUDBENT + 1];
static bool		UdbInitialized = false;

int
udbexpand(a, sendq, aliaslevel, e)
	register ADDRESS *a;
	ADDRESS **sendq;
	int aliaslevel;
	register ENVELOPE *e;
{
	int i;
	DBT key;
	DBT info;
	bool breakout;
	register struct udbent *up;
	int keylen;
	int naddrs;
	char *user;
	char keybuf[MAXUDBKEY];

	memset(&key, '\0', sizeof(key));
	memset(&info, '\0', sizeof(info));

	if (tTd(28, 1))
		sm_dprintf("udbexpand(%s)\n", a->q_paddr);

	/* make certain we are supposed to send to this address */
	if (!QS_IS_SENDABLE(a->q_state))
		return EX_OK;
	e->e_to = a->q_paddr;

	/* on first call, locate the database */
	if (!UdbInitialized)
	{
		if (_udbx_init(e) == EX_TEMPFAIL)
			return EX_TEMPFAIL;
	}

	/* short circuit the process if no chance of a match */
	if (UdbSpec == NULL || UdbSpec[0] == '\0')
		return EX_OK;

	/* extract user to do userdb matching on */
	user = a->q_user;

	/* short circuit name begins with '\\' since it can't possibly match */
	/* (might want to treat this as unquoted instead) */
	if (user[0] == '\\')
		return EX_OK;

	/* if name begins with a colon, it indicates our metadata */
	if (user[0] == ':')
		return EX_OK;

	keylen = sm_strlcpyn(keybuf, sizeof(keybuf), 2, user, ":maildrop");

	/* if name is too long, assume it won't match */
	if (keylen >= sizeof(keybuf))
		return EX_OK;

	/* build actual database key */

	breakout = false;
	for (up = UdbEnts; !breakout; up++)
	{
		int usersize;
		int userleft;
		char userbuf[MEMCHUNKSIZE];
# if HESIOD && HES_GETMAILHOST
		char pobuf[MAXNAME];
# endif /* HESIOD && HES_GETMAILHOST */
# if defined(NEWDB) && DB_VERSION_MAJOR > 1
		DBC *dbc = NULL;
# endif /* defined(NEWDB) && DB_VERSION_MAJOR > 1 */

		user = userbuf;
		userbuf[0] = '\0';
		usersize = sizeof(userbuf);
		userleft = sizeof(userbuf) - 1;

		/*
		**  Select action based on entry type.
		**
		**	On dropping out of this switch, "class" should
		**	explain the type of the data, and "user" should
		**	contain the user information.
		*/

		switch (up->udb_type)
		{
# if NEWDB
		  case UDB_DBFETCH:
			key.data = keybuf;
			key.size = keylen;
			if (tTd(28, 80))
				sm_dprintf("udbexpand: trying %s (%d) via db\n",
					keybuf, keylen);
#  if DB_VERSION_MAJOR < 2
			i = (*up->udb_dbp->seq)(up->udb_dbp, &key, &info, R_CURSOR);
#  else /* DB_VERSION_MAJOR < 2 */
			i = 0;
			if (dbc == NULL &&
#   if DB_VERSION_MAJOR > 2 || DB_VERSION_MINOR >= 6
			    (errno = (*up->udb_dbp->cursor)(up->udb_dbp,
							    NULL, &dbc, 0)) != 0)
#   else /* DB_VERSION_MAJOR > 2 || DB_VERSION_MINOR >= 6 */
			    (errno = (*up->udb_dbp->cursor)(up->udb_dbp,
							    NULL, &dbc)) != 0)
#   endif /* DB_VERSION_MAJOR > 2 || DB_VERSION_MINOR >= 6 */
				i = -1;
			if (i != 0 || dbc == NULL ||
			    (errno = dbc->c_get(dbc, &key,
						&info, DB_SET)) != 0)
				i = 1;
#  endif /* DB_VERSION_MAJOR < 2 */
			if (i > 0 || info.size <= 0)
			{
				if (tTd(28, 2))
					sm_dprintf("udbexpand: no match on %s (%d)\n",
						keybuf, keylen);
#  if DB_VERSION_MAJOR > 1
				if (dbc != NULL)
				{
					(void) dbc->c_close(dbc);
					dbc = NULL;
				}
#  endif /* DB_VERSION_MAJOR > 1 */
				break;
			}
			if (tTd(28, 80))
				sm_dprintf("udbexpand: match %.*s: %.*s\n",
					(int) key.size, (char *) key.data,
					(int) info.size, (char *) info.data);

			a->q_flags &= ~QSELFREF;
			while (i == 0 && key.size == keylen &&
			       memcmp(key.data, keybuf, keylen) == 0)
			{
				char *p;

				if (bitset(EF_VRFYONLY, e->e_flags))
				{
					a->q_state = QS_VERIFIED;
#  if DB_VERSION_MAJOR > 1
					if (dbc != NULL)
					{
						(void) dbc->c_close(dbc);
						dbc = NULL;
					}
#  endif /* DB_VERSION_MAJOR > 1 */
					return EX_OK;
				}

				breakout = true;
				if (info.size >= userleft - 1)
				{
					char *nuser;
					int size = MEMCHUNKSIZE;

					if (info.size > MEMCHUNKSIZE)
						size = info.size;
					nuser = sm_malloc_x(usersize + size);

					memmove(nuser, user, usersize);
					if (user != userbuf)
						sm_free(user); /* XXX */
					user = nuser;
					usersize += size;
					userleft += size;
				}
				p = &user[strlen(user)];
				if (p != user)
				{
					*p++ = ',';
					userleft--;
				}
				memmove(p, info.data, info.size);
				p[info.size] = '\0';
				userleft -= info.size;

				/* get the next record */
#  if DB_VERSION_MAJOR < 2
				i = (*up->udb_dbp->seq)(up->udb_dbp, &key, &info, R_NEXT);
#  else /* DB_VERSION_MAJOR < 2 */
				i = 0;
				if ((errno = dbc->c_get(dbc, &key,
							&info, DB_NEXT)) != 0)
					i = 1;
#  endif /* DB_VERSION_MAJOR < 2 */
			}

#  if DB_VERSION_MAJOR > 1
			if (dbc != NULL)
			{
				(void) dbc->c_close(dbc);
				dbc = NULL;
			}
#  endif /* DB_VERSION_MAJOR > 1 */

			/* if nothing ever matched, try next database */
			if (!breakout)
				break;

			message("expanded to %s", user);
			if (LogLevel > 10)
				sm_syslog(LOG_INFO, e->e_id,
					  "expand %.100s => %s",
					  e->e_to,
					  shortenstring(user, MAXSHORTSTR));
			naddrs = sendtolist(user, a, sendq, aliaslevel + 1, e);
			if (naddrs > 0 && !bitset(QSELFREF, a->q_flags))
			{
				if (tTd(28, 5))
				{
					sm_dprintf("udbexpand: QS_EXPANDED ");
					printaddr(sm_debug_file(), a, false);
				}
				a->q_state = QS_EXPANDED;
			}
			if (i < 0)
			{
				syserr("udbexpand: db-get %.*s stat %d",
					(int) key.size, (char *) key.data, i);
				return EX_TEMPFAIL;
			}

			/*
			**  If this address has a -request address, reflect
			**  it into the envelope.
			*/

			memset(&key, '\0', sizeof(key));
			memset(&info, '\0', sizeof(info));
			(void) sm_strlcpyn(keybuf, sizeof(keybuf), 2, a->q_user,
					   ":mailsender");
			keylen = strlen(keybuf);
			key.data = keybuf;
			key.size = keylen;

#  if DB_VERSION_MAJOR < 2
			i = (*up->udb_dbp->get)(up->udb_dbp, &key, &info, 0);
#  else /* DB_VERSION_MAJOR < 2 */
			i = errno = (*up->udb_dbp->get)(up->udb_dbp, NULL,
							&key, &info, 0);
#  endif /* DB_VERSION_MAJOR < 2 */
			if (i != 0 || info.size <= 0)
				break;
			a->q_owner = sm_rpool_malloc_x(e->e_rpool,
						       info.size + 1);
			memmove(a->q_owner, info.data, info.size);
			a->q_owner[info.size] = '\0';

			/* announce delivery; NORECEIPT bit set later */
			if (e->e_xfp != NULL)
			{
				(void) sm_io_fprintf(e->e_xfp, SM_TIME_DEFAULT,
						     "Message delivered to mailing list %s\n",
						     a->q_paddr);
			}
			e->e_flags |= EF_SENDRECEIPT;
			a->q_flags |= QDELIVERED|QEXPANDED;
			break;
# endif /* NEWDB */

# if HESIOD
		  case UDB_HESIOD:
			key.data = keybuf;
			key.size = keylen;
			if (tTd(28, 80))
				sm_dprintf("udbexpand: trying %s (%d) via hesiod\n",
					keybuf, keylen);
			/* look up the key via hesiod */
			i = hes_udb_get(&key, &info);
			if (i < 0)
			{
				syserr("udbexpand: hesiod-get %.*s stat %d",
					(int) key.size, (char *) key.data, i);
				return EX_TEMPFAIL;
			}
			else if (i > 0 || info.size <= 0)
			{
#  if HES_GETMAILHOST
				struct hes_postoffice *hp;
#  endif /* HES_GETMAILHOST */

				if (tTd(28, 2))
					sm_dprintf("udbexpand: no match on %s (%d)\n",
						(char *) keybuf, (int) keylen);
#  if HES_GETMAILHOST
				if (tTd(28, 8))
					sm_dprintf("  ... trying hes_getmailhost(%s)\n",
						a->q_user);
				hp = hes_getmailhost(a->q_user);
				if (hp == NULL)
				{
					if (hes_error() == HES_ER_NET)
					{
						syserr("udbexpand: hesiod-getmail %s stat %d",
							a->q_user, hes_error());
						return EX_TEMPFAIL;
					}
					if (tTd(28, 2))
						sm_dprintf("hes_getmailhost(%s): %d\n",
							a->q_user, hes_error());
					break;
				}
				if (strlen(hp->po_name) + strlen(hp->po_host) >
				    sizeof(pobuf) - 2)
				{
					if (tTd(28, 2))
						sm_dprintf("hes_getmailhost(%s): expansion too long: %.30s@%.30s\n",
							a->q_user,
							hp->po_name,
							hp->po_host);
					break;
				}
				info.data = pobuf;
				(void) sm_snprintf(pobuf, sizeof(pobuf),
					"%s@%s", hp->po_name, hp->po_host);
				info.size = strlen(info.data);
#  else /* HES_GETMAILHOST */
				break;
#  endif /* HES_GETMAILHOST */
			}
			if (tTd(28, 80))
				sm_dprintf("udbexpand: match %.*s: %.*s\n",
					(int) key.size, (char *) key.data,
					(int) info.size, (char *) info.data);
			a->q_flags &= ~QSELFREF;

			if (bitset(EF_VRFYONLY, e->e_flags))
			{
				a->q_state = QS_VERIFIED;
				return EX_OK;
			}

			breakout = true;
			if (info.size >= usersize)
				user = sm_malloc_x(info.size + 1);
			memmove(user, info.data, info.size);
			user[info.size] = '\0';

			message("hesioded to %s", user);
			if (LogLevel > 10)
				sm_syslog(LOG_INFO, e->e_id,
					  "hesiod %.100s => %s",
					  e->e_to,
					  shortenstring(user, MAXSHORTSTR));
			naddrs = sendtolist(user, a, sendq, aliaslevel + 1, e);

			if (naddrs > 0 && !bitset(QSELFREF, a->q_flags))
			{
				if (tTd(28, 5))
				{
					sm_dprintf("udbexpand: QS_EXPANDED ");
					printaddr(sm_debug_file(), a, false);
				}
				a->q_state = QS_EXPANDED;
			}

			/*
			**  If this address has a -request address, reflect
			**  it into the envelope.
			*/

			(void) sm_strlcpyn(keybuf, sizeof(keybuf), 2, a->q_user,
					   ":mailsender");
			keylen = strlen(keybuf);
			key.data = keybuf;
			key.size = keylen;
			i = hes_udb_get(&key, &info);
			if (i != 0 || info.size <= 0)
				break;
			a->q_owner = sm_rpool_malloc_x(e->e_rpool,
						       info.size + 1);
			memmove(a->q_owner, info.data, info.size);
			a->q_owner[info.size] = '\0';
			break;
# endif /* HESIOD */

		  case UDB_REMOTE:
			/* not yet implemented */
			break;

		  case UDB_FORWARD:
			if (bitset(EF_VRFYONLY, e->e_flags))
			{
				a->q_state = QS_VERIFIED;
				return EX_OK;
			}
			i = strlen(up->udb_fwdhost) + strlen(a->q_user) + 1;
			if (i >= usersize)
			{
				usersize = i + 1;
				user = sm_malloc_x(usersize);
			}
			(void) sm_strlcpyn(user, usersize, 3,
					a->q_user, "@", up->udb_fwdhost);
			message("expanded to %s", user);
			a->q_flags &= ~QSELFREF;
			naddrs = sendtolist(user, a, sendq, aliaslevel + 1, e);
			if (naddrs > 0 && !bitset(QSELFREF, a->q_flags))
			{
				if (tTd(28, 5))
				{
					sm_dprintf("udbexpand: QS_EXPANDED ");
					printaddr(sm_debug_file(), a, false);
				}
				a->q_state = QS_EXPANDED;
			}
			breakout = true;
			break;

		  case UDB_EOLIST:
			breakout = true;
			break;

		  default:
			/* unknown entry type */
			break;
		}
		/* XXX if an exception occurs, there is a storage leak */
		if (user != userbuf)
			sm_free(user); /* XXX */
	}
	return EX_OK;
}
/*
**  UDBSENDER -- return canonical external name of sender, given local name
**
**	Parameters:
**		sender -- the name of the sender on the local machine.
**		rpool -- resource pool from which to allocate result
**
**	Returns:
**		The external name for this sender, if derivable from the
**			database.  Storage allocated from rpool.
**		NULL -- if nothing is changed from the database.
**
**	Side Effects:
**		none.
*/

char *
udbsender(sender, rpool)
	char *sender;
	SM_RPOOL_T *rpool;
{
	return udbmatch(sender, "mailname", rpool);
}
/*
**  UDBMATCH -- match user in field, return result of lookup.
**
**	Parameters:
**		user -- the name of the user.
**		field -- the field to lookup.
**		rpool -- resource pool from which to allocate result
**
**	Returns:
**		The external name for this sender, if derivable from the
**			database.  Storage allocated from rpool.
**		NULL -- if nothing is changed from the database.
**
**	Side Effects:
**		none.
*/

static char *
udbmatch(user, field, rpool)
	char *user;
	char *field;
	SM_RPOOL_T *rpool;
{
	register char *p;
	register struct udbent *up;
	int i;
	int keylen;
	DBT key, info;
	char keybuf[MAXUDBKEY];

	if (tTd(28, 1))
		sm_dprintf("udbmatch(%s, %s)\n", user, field);

	if (!UdbInitialized)
	{
		if (_udbx_init(CurEnv) == EX_TEMPFAIL)
			return NULL;
	}

	/* short circuit if no spec */
	if (UdbSpec == NULL || UdbSpec[0] == '\0')
		return NULL;

	/* short circuit name begins with '\\' since it can't possibly match */
	if (user[0] == '\\')
		return NULL;

	/* long names can never match and are a pain to deal with */
	i = strlen(field);
	if (i < sizeof("maildrop"))
		i = sizeof("maildrop");
	if ((strlen(user) + i) > sizeof(keybuf) - 4)
		return NULL;

	/* names beginning with colons indicate metadata */
	if (user[0] == ':')
		return NULL;

	/* build database key */
	(void) sm_strlcpyn(keybuf, sizeof(keybuf), 3, user, ":", field);
	keylen = strlen(keybuf);

	for (up = UdbEnts; up->udb_type != UDB_EOLIST; up++)
	{
		/*
		**  Select action based on entry type.
		*/

		switch (up->udb_type)
		{
# if NEWDB
		  case UDB_DBFETCH:
			memset(&key, '\0', sizeof(key));
			memset(&info, '\0', sizeof(info));
			key.data = keybuf;
			key.size = keylen;
#  if DB_VERSION_MAJOR < 2
			i = (*up->udb_dbp->get)(up->udb_dbp, &key, &info, 0);
#  else /* DB_VERSION_MAJOR < 2 */
			i = errno = (*up->udb_dbp->get)(up->udb_dbp, NULL,
							&key, &info, 0);
#  endif /* DB_VERSION_MAJOR < 2 */
			if (i != 0 || info.size <= 0)
			{
				if (tTd(28, 2))
					sm_dprintf("udbmatch: no match on %s (%d) via db\n",
						keybuf, keylen);
				continue;
			}

			p = sm_rpool_malloc_x(rpool, info.size + 1);
			memmove(p, info.data, info.size);
			p[info.size] = '\0';
			if (tTd(28, 1))
				sm_dprintf("udbmatch ==> %s\n", p);
			return p;
# endif /* NEWDB */

# if HESIOD
		  case UDB_HESIOD:
			key.data = keybuf;
			key.size = keylen;
			i = hes_udb_get(&key, &info);
			if (i != 0 || info.size <= 0)
			{
				if (tTd(28, 2))
					sm_dprintf("udbmatch: no match on %s (%d) via hesiod\n",
						keybuf, keylen);
				continue;
			}

			p = sm_rpool_malloc_x(rpool, info.size + 1);
			memmove(p, info.data, info.size);
			p[info.size] = '\0';
			if (tTd(28, 1))
				sm_dprintf("udbmatch ==> %s\n", p);
			return p;
# endif /* HESIOD */
		}
	}

	if (strcmp(field, "mailname") != 0)
		return NULL;

	/*
	**  Nothing yet.  Search again for a default case.  But only
	**  use it if we also have a forward (:maildrop) pointer already
	**  in the database.
	*/

	/* build database key */
	(void) sm_strlcpyn(keybuf, sizeof(keybuf), 2, user, ":maildrop");
	keylen = strlen(keybuf);

	for (up = UdbEnts; up->udb_type != UDB_EOLIST; up++)
	{
		switch (up->udb_type)
		{
# if NEWDB
		  case UDB_DBFETCH:
			/* get the default case for this database */
			if (up->udb_default == NULL)
			{
				memset(&key, '\0', sizeof(key));
				memset(&info, '\0', sizeof(info));
				key.data = ":default:mailname";
				key.size = strlen(key.data);
#  if DB_VERSION_MAJOR < 2
				i = (*up->udb_dbp->get)(up->udb_dbp,
							&key, &info, 0);
#  else /* DB_VERSION_MAJOR < 2 */
				i = errno = (*up->udb_dbp->get)(up->udb_dbp,
								NULL, &key,
								&info, 0);
#  endif /* DB_VERSION_MAJOR < 2 */
				if (i != 0 || info.size <= 0)
				{
					/* no default case */
					up->udb_default = "";
					continue;
				}

				/* save the default case */
				up->udb_default = sm_pmalloc_x(info.size + 1);
				memmove(up->udb_default, info.data, info.size);
				up->udb_default[info.size] = '\0';
			}
			else if (up->udb_default[0] == '\0')
				continue;

			/* we have a default case -- verify user:maildrop */
			memset(&key, '\0', sizeof(key));
			memset(&info, '\0', sizeof(info));
			key.data = keybuf;
			key.size = keylen;
#  if DB_VERSION_MAJOR < 2
			i = (*up->udb_dbp->get)(up->udb_dbp, &key, &info, 0);
#  else /* DB_VERSION_MAJOR < 2 */
			i = errno = (*up->udb_dbp->get)(up->udb_dbp, NULL,
							&key, &info, 0);
#  endif /* DB_VERSION_MAJOR < 2 */
			if (i != 0 || info.size <= 0)
			{
				/* nope -- no aliasing for this user */
				continue;
			}

			/* they exist -- build the actual address */
			i = strlen(user) + strlen(up->udb_default) + 2;
			p = sm_rpool_malloc_x(rpool, i);
			(void) sm_strlcpyn(p, i, 3, user, "@", up->udb_default);
			if (tTd(28, 1))
				sm_dprintf("udbmatch ==> %s\n", p);
			return p;
# endif /* NEWDB */

# if HESIOD
		  case UDB_HESIOD:
			/* get the default case for this database */
			if (up->udb_default == NULL)
			{
				key.data = ":default:mailname";
				key.size = strlen(key.data);
				i = hes_udb_get(&key, &info);

				if (i != 0 || info.size <= 0)
				{
					/* no default case */
					up->udb_default = "";
					continue;
				}

				/* save the default case */
				up->udb_default = sm_pmalloc_x(info.size + 1);
				memmove(up->udb_default, info.data, info.size);
				up->udb_default[info.size] = '\0';
			}
			else if (up->udb_default[0] == '\0')
				continue;

			/* we have a default case -- verify user:maildrop */
			key.data = keybuf;
			key.size = keylen;
			i = hes_udb_get(&key, &info);
			if (i != 0 || info.size <= 0)
			{
				/* nope -- no aliasing for this user */
				continue;
			}

			/* they exist -- build the actual address */
			i = strlen(user) + strlen(up->udb_default) + 2;
			p = sm_rpool_malloc_x(rpool, i);
			(void) sm_strlcpyn(p, i, 3, user, "@", up->udb_default);
			if (tTd(28, 1))
				sm_dprintf("udbmatch ==> %s\n", p);
			return p;
			break;
# endif /* HESIOD */
		}
	}

	/* still nothing....  too bad */
	return NULL;
}
/*
**  UDB_MAP_LOOKUP -- look up arbitrary entry in user database map
**
**	Parameters:
**		map -- the map being queried.
**		name -- the name to look up.
**		av -- arguments to the map lookup.
**		statp -- to get any error status.
**
**	Returns:
**		NULL if name not found in map.
**		The rewritten name otherwise.
*/

/* ARGSUSED3 */
char *
udb_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *val;
	char *key;
	char *SM_NONVOLATILE result = NULL;
	char keybuf[MAXNAME + 1];

	if (tTd(28, 20) || tTd(38, 20))
		sm_dprintf("udb_map_lookup(%s, %s)\n", map->map_mname, name);

	if (bitset(MF_NOFOLDCASE, map->map_mflags))
	{
		key = name;
	}
	else
	{
		int keysize = strlen(name);

		if (keysize > sizeof(keybuf) - 1)
			keysize = sizeof(keybuf) - 1;
		memmove(keybuf, name, keysize);
		keybuf[keysize] = '\0';
		makelower(keybuf);
		key = keybuf;
	}
	val = udbmatch(key, map->map_file, NULL);
	if (val == NULL)
		return NULL;
	SM_TRY
		if (bitset(MF_MATCHONLY, map->map_mflags))
			result = map_rewrite(map, name, strlen(name), NULL);
		else
			result = map_rewrite(map, val, strlen(val), av);
	SM_FINALLY
		sm_free(val);
	SM_END_TRY
	return result;
}
/*
**  _UDBX_INIT -- parse the UDB specification, opening any valid entries.
**
**	Parameters:
**		e -- the current envelope.
**
**	Returns:
**		EX_TEMPFAIL -- if it appeared it couldn't get hold of a
**			database due to a host being down or some similar
**			(recoverable) situation.
**		EX_OK -- otherwise.
**
**	Side Effects:
**		Fills in the UdbEnts structure from UdbSpec.
*/

# define MAXUDBOPTS	27

static int
_udbx_init(e)
	ENVELOPE *e;
{
	int ents = 0;
	register char *p;
	register struct udbent *up;

	if (UdbInitialized)
		return EX_OK;

# ifdef UDB_DEFAULT_SPEC
	if (UdbSpec == NULL)
		UdbSpec = UDB_DEFAULT_SPEC;
# endif /* UDB_DEFAULT_SPEC */

	p = UdbSpec;
	up = UdbEnts;
	while (p != NULL)
	{
		char *spec;
		int l;
		struct udb_option opts[MAXUDBOPTS + 1];

		while (*p == ' ' || *p == '\t' || *p == ',')
			p++;
		if (*p == '\0')
			break;
		spec = p;
		p = strchr(p, ',');
		if (p != NULL)
			*p++ = '\0';

		if (ents >= MAXUDBENT)
		{
			syserr("Maximum number of UDB entries exceeded");
			break;
		}

		/* extract options */
		(void) _udb_parsespec(spec, opts, MAXUDBOPTS);

		/*
		**  Decode database specification.
		**
		**	In the sendmail tradition, the leading character
		**	defines the semantics of the rest of the entry.
		**
		**	@hostname --	forward email to the indicated host.
		**			This should be the last in the list,
		**			since it always matches the input.
		**	/dbname	 --	search the named database on the local
		**			host using the Berkeley db package.
		**	Hesiod --	search the named database with BIND
		**			using the MIT Hesiod package.
		*/

		switch (*spec)
		{
		  case '@':	/* forward to remote host */
			up->udb_type = UDB_FORWARD;
			up->udb_pid = CurrentPid;
			up->udb_fwdhost = spec + 1;
			ents++;
			up++;
			break;

# if HESIOD
		  case 'h':	/* use hesiod */
		  case 'H':
			if (sm_strcasecmp(spec, "hesiod") != 0)
				goto badspec;
			up->udb_type = UDB_HESIOD;
			up->udb_pid = CurrentPid;
			ents++;
			up++;
			break;
# endif /* HESIOD */

# if NEWDB
		  case '/':	/* look up remote name */
			l = strlen(spec);
			if (l > 3 && strcmp(&spec[l - 3], ".db") == 0)
			{
				up->udb_dbname = spec;
			}
			else
			{
				up->udb_dbname = sm_pmalloc_x(l + 4);
				(void) sm_strlcpyn(up->udb_dbname, l + 4, 2,
						   spec, ".db");
			}
			errno = 0;
#  if DB_VERSION_MAJOR < 2
			up->udb_dbp = dbopen(up->udb_dbname, O_RDONLY,
					     0644, DB_BTREE, NULL);
#  else /* DB_VERSION_MAJOR < 2 */
			{
				int flags = DB_RDONLY;
#  if DB_VERSION_MAJOR > 2
				int ret;
#  endif /* DB_VERSION_MAJOR > 2 */

				SM_DB_FLAG_ADD(flags);
				up->udb_dbp = NULL;
#  if DB_VERSION_MAJOR > 2
				ret = db_create(&up->udb_dbp, NULL, 0);
				if (ret != 0)
				{
					(void) up->udb_dbp->close(up->udb_dbp,
								  0);
					up->udb_dbp = NULL;
				}
				else
				{
					ret = up->udb_dbp->open(up->udb_dbp,
								DBTXN
								up->udb_dbname,
								NULL,
								DB_BTREE,
								flags,
								0644);
					if (ret != 0)
					{
#ifdef DB_OLD_VERSION
						if (ret == DB_OLD_VERSION)
							ret = EINVAL;
#endif /* DB_OLD_VERSION */
						(void) up->udb_dbp->close(up->udb_dbp, 0);
						up->udb_dbp = NULL;
					}
				}
				errno = ret;
#  else /* DB_VERSION_MAJOR > 2 */
				errno = db_open(up->udb_dbname, DB_BTREE,
						flags, 0644, NULL,
						NULL, &up->udb_dbp);
#  endif /* DB_VERSION_MAJOR > 2 */
			}
#  endif /* DB_VERSION_MAJOR < 2 */
			if (up->udb_dbp == NULL)
			{
				if (tTd(28, 1))
				{
					int save_errno = errno;

#  if DB_VERSION_MAJOR < 2
					sm_dprintf("dbopen(%s): %s\n",
#  else /* DB_VERSION_MAJOR < 2 */
					sm_dprintf("db_open(%s): %s\n",
#  endif /* DB_VERSION_MAJOR < 2 */
						up->udb_dbname,
						sm_errstring(errno));
					errno = save_errno;
				}
				if (errno != ENOENT && errno != EACCES)
				{
					if (LogLevel > 2)
						sm_syslog(LOG_ERR, e->e_id,
#  if DB_VERSION_MAJOR < 2
							  "dbopen(%s): %s",
#  else /* DB_VERSION_MAJOR < 2 */
							  "db_open(%s): %s",
#  endif /* DB_VERSION_MAJOR < 2 */
							  up->udb_dbname,
							  sm_errstring(errno));
					up->udb_type = UDB_EOLIST;
					if (up->udb_dbname != spec)
						sm_free(up->udb_dbname); /* XXX */
					goto tempfail;
				}
				if (up->udb_dbname != spec)
					sm_free(up->udb_dbname); /* XXX */
				break;
			}
			if (tTd(28, 1))
			{
#  if DB_VERSION_MAJOR < 2
				sm_dprintf("_udbx_init: dbopen(%s)\n",
#  else /* DB_VERSION_MAJOR < 2 */
				sm_dprintf("_udbx_init: db_open(%s)\n",
#  endif /* DB_VERSION_MAJOR < 2 */
					up->udb_dbname);
			}
			up->udb_type = UDB_DBFETCH;
			up->udb_pid = CurrentPid;
			ents++;
			up++;
			break;
# endif /* NEWDB */

		  default:
# if HESIOD
badspec:
# endif /* HESIOD */
			syserr("Unknown UDB spec %s", spec);
			break;
		}
	}
	up->udb_type = UDB_EOLIST;

	if (tTd(28, 4))
	{
		for (up = UdbEnts; up->udb_type != UDB_EOLIST; up++)
		{
			switch (up->udb_type)
			{
			  case UDB_REMOTE:
				sm_dprintf("REMOTE: addr %s, timeo %d\n",
					   anynet_ntoa((SOCKADDR *) &up->udb_addr),
					   up->udb_timeout);
				break;

			  case UDB_DBFETCH:
# if NEWDB
				sm_dprintf("FETCH: file %s\n",
					up->udb_dbname);
# else /* NEWDB */
				sm_dprintf("FETCH\n");
# endif /* NEWDB */
				break;

			  case UDB_FORWARD:
				sm_dprintf("FORWARD: host %s\n",
					up->udb_fwdhost);
				break;

			  case UDB_HESIOD:
				sm_dprintf("HESIOD\n");
				break;

			  default:
				sm_dprintf("UNKNOWN\n");
				break;
			}
		}
	}

	UdbInitialized = true;
	errno = 0;
	return EX_OK;

	/*
	**  On temporary failure, back out anything we've already done
	*/

  tempfail:
# if NEWDB
	for (up = UdbEnts; up->udb_type != UDB_EOLIST; up++)
	{
		if (up->udb_type == UDB_DBFETCH)
		{
#  if DB_VERSION_MAJOR < 2
			(*up->udb_dbp->close)(up->udb_dbp);
#  else /* DB_VERSION_MAJOR < 2 */
			errno = (*up->udb_dbp->close)(up->udb_dbp, 0);
#  endif /* DB_VERSION_MAJOR < 2 */
			if (tTd(28, 1))
				sm_dprintf("_udbx_init: db->close(%s)\n",
					up->udb_dbname);
		}
	}
# endif /* NEWDB */
	return EX_TEMPFAIL;
}

static int
_udb_parsespec(udbspec, opt, maxopts)
	char *udbspec;
	struct udb_option opt[];
	int maxopts;
{
	register char *spec;
	register char *spec_end;
	register int optnum;

	spec_end = strchr(udbspec, ':');
	for (optnum = 0; optnum < maxopts && (spec = spec_end) != NULL; optnum++)
	{
		register char *p;

		while (isascii(*spec) && isspace(*spec))
			spec++;
		spec_end = strchr(spec, ':');
		if (spec_end != NULL)
			*spec_end++ = '\0';

		opt[optnum].udbo_name = spec;
		opt[optnum].udbo_val = NULL;
		p = strchr(spec, '=');
		if (p != NULL)
			opt[optnum].udbo_val = ++p;
	}
	return optnum;
}
/*
**  _UDBX_CLOSE -- close all file based UDB entries.
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/
void
_udbx_close()
{
	struct udbent *up;

	if (!UdbInitialized)
		return;

	for (up = UdbEnts; up->udb_type != UDB_EOLIST; up++)
	{
		if (up->udb_pid != CurrentPid)
			continue;

# if NEWDB
		if (up->udb_type == UDB_DBFETCH)
		{
#  if DB_VERSION_MAJOR < 2
			(*up->udb_dbp->close)(up->udb_dbp);
#  else /* DB_VERSION_MAJOR < 2 */
			errno = (*up->udb_dbp->close)(up->udb_dbp, 0);
#  endif /* DB_VERSION_MAJOR < 2 */
		}
		if (tTd(28, 1))
			sm_dprintf("_udbx_close: db->close(%s)\n",
				up->udb_dbname);
# endif /* NEWDB */
	}
}

# if HESIOD

static int
hes_udb_get(key, info)
	DBT *key;
	DBT *info;
{
	char *name, *type;
	char **hp;
	char kbuf[MAXUDBKEY + 1];

	if (sm_strlcpy(kbuf, key->data, sizeof(kbuf)) >= sizeof(kbuf))
		return 0;
	name = kbuf;
	type = strrchr(name, ':');
	if (type == NULL)
		return 1;
	*type++ = '\0';
	if (strchr(name, '@') != NULL)
		return 1;

	if (tTd(28, 1))
		sm_dprintf("hes_udb_get(%s, %s)\n", name, type);

	/* make the hesiod query */
#  ifdef HESIOD_INIT
	if (HesiodContext == NULL && hesiod_init(&HesiodContext) != 0)
		return -1;
	hp = hesiod_resolve(HesiodContext, name, type);
#  else /* HESIOD_INIT */
	hp = hes_resolve(name, type);
#  endif /* HESIOD_INIT */
	*--type = ':';
#  ifdef HESIOD_INIT
	if (hp == NULL)
		return 1;
	if (*hp == NULL)
	{
		hesiod_free_list(HesiodContext, hp);
		if (errno == ECONNREFUSED || errno == EMSGSIZE)
			return -1;
		return 1;
	}
#  else /* HESIOD_INIT */
	if (hp == NULL || hp[0] == NULL)
	{
		/* network problem or timeout */
		if (hes_error() == HES_ER_NET)
			return -1;

		return 1;
	}
#  endif /* HESIOD_INIT */
	else
	{
		/*
		**  If there are multiple matches, just return the
		**  first one.
		**
		**  XXX These should really be returned; for example,
		**  XXX it is legal for :maildrop to be multi-valued.
		*/

		info->data = hp[0];
		info->size = (size_t) strlen(info->data);
	}

	if (tTd(28, 80))
		sm_dprintf("hes_udb_get => %s\n", *hp);

	return 0;
}
# endif /* HESIOD */

#else /* USERDB */

int
udbexpand(a, sendq, aliaslevel, e)
	ADDRESS *a;
	ADDRESS **sendq;
	int aliaslevel;
	ENVELOPE *e;
{
	return EX_OK;
}

#endif /* USERDB */
