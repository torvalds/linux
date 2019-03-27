/*
 * Copyright (c) 2001-2003,2009 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: mbdb.c,v 1.43 2014-01-08 17:03:15 ca Exp $")

#include <sys/param.h>

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>

#include <sm/limits.h>
#include <sm/conf.h>
#include <sm/assert.h>
#include <sm/bitops.h>
#include <sm/errstring.h>
#include <sm/heap.h>
#include <sm/mbdb.h>
#include <sm/string.h>
# ifdef EX_OK
#  undef EX_OK			/* for SVr4.2 SMP */
# endif /* EX_OK */
#include <sm/sysexits.h>

#if LDAPMAP
# if _LDAP_EXAMPLE_
#  include <sm/ldap.h>
# endif /* _LDAP_EXAMPLE_ */
#endif /* LDAPMAP */

typedef struct
{
	char	*mbdb_typename;
	int	(*mbdb_initialize) __P((char *));
	int	(*mbdb_lookup) __P((char *name, SM_MBDB_T *user));
	void	(*mbdb_terminate) __P((void));
} SM_MBDB_TYPE_T;

static int	mbdb_pw_initialize __P((char *));
static int	mbdb_pw_lookup __P((char *name, SM_MBDB_T *user));
static void	mbdb_pw_terminate __P((void));

#if LDAPMAP
# if _LDAP_EXAMPLE_
static struct sm_ldap_struct LDAPLMAP;
static int	mbdb_ldap_initialize __P((char *));
static int	mbdb_ldap_lookup __P((char *name, SM_MBDB_T *user));
static void	mbdb_ldap_terminate __P((void));
# endif /* _LDAP_EXAMPLE_ */
#endif /* LDAPMAP */

static SM_MBDB_TYPE_T SmMbdbTypes[] =
{
	{ "pw", mbdb_pw_initialize, mbdb_pw_lookup, mbdb_pw_terminate },
#if LDAPMAP
# if _LDAP_EXAMPLE_
	{ "ldap", mbdb_ldap_initialize, mbdb_ldap_lookup, mbdb_ldap_terminate },
# endif /* _LDAP_EXAMPLE_ */
#endif /* LDAPMAP */
	{ NULL, NULL, NULL, NULL }
};

static SM_MBDB_TYPE_T *SmMbdbType = &SmMbdbTypes[0];

/*
**  SM_MBDB_INITIALIZE -- specify which mailbox database to use
**
**	If this function is not called, then the "pw" implementation
**	is used by default; this implementation uses getpwnam().
**
**	Parameters:
**		mbdb -- Which mailbox database to use.
**			The argument has the form "name" or "name.arg".
**			"pw" means use getpwnam().
**
**	Results:
**		EX_OK on success, or an EX_* code on failure.
*/

int
sm_mbdb_initialize(mbdb)
	char *mbdb;
{
	size_t namelen;
	int err;
	char *name;
	char *arg;
	SM_MBDB_TYPE_T *t;

	SM_REQUIRE(mbdb != NULL);

	name = mbdb;
	arg = strchr(mbdb, '.');
	if (arg == NULL)
		namelen = strlen(name);
	else
	{
		namelen = arg - name;
		++arg;
	}

	for (t = SmMbdbTypes; t->mbdb_typename != NULL; ++t)
	{
		if (strlen(t->mbdb_typename) == namelen &&
		    strncmp(name, t->mbdb_typename, namelen) == 0)
		{
			err = EX_OK;
			if (t->mbdb_initialize != NULL)
				err = t->mbdb_initialize(arg);
			if (err == EX_OK)
				SmMbdbType = t;
			return err;
		}
	}
	return EX_UNAVAILABLE;
}

/*
**  SM_MBDB_TERMINATE -- terminate connection to the mailbox database
**
**	Because this function closes any cached file descriptors that
**	are being held open for the connection to the mailbox database,
**	it should be called for security reasons prior to dropping privileges
**	and execing another process.
**
**	Parameters:
**		none.
**
**	Results:
**		none.
*/

void
sm_mbdb_terminate()
{
	if (SmMbdbType->mbdb_terminate != NULL)
		SmMbdbType->mbdb_terminate();
}

/*
**  SM_MBDB_LOOKUP -- look up a local mail recipient, given name
**
**	Parameters:
**		name -- name of local mail recipient
**		user -- pointer to structure to fill in on success
**
**	Results:
**		On success, fill in *user and return EX_OK.
**		If the user does not exist, return EX_NOUSER.
**		If a temporary failure (eg, a network failure) occurred,
**		return EX_TEMPFAIL.  Otherwise return EX_OSERR.
*/

int
sm_mbdb_lookup(name, user)
	char *name;
	SM_MBDB_T *user;
{
	int ret = EX_NOUSER;

	if (SmMbdbType->mbdb_lookup != NULL)
		ret = SmMbdbType->mbdb_lookup(name, user);
	return ret;
}

/*
**  SM_MBDB_FROMPW -- copy from struct pw to SM_MBDB_T
**
**	Parameters:
**		user -- destination user information structure
**		pw -- source passwd structure
**
**	Results:
**		none.
*/

void
sm_mbdb_frompw(user, pw)
	SM_MBDB_T *user;
	struct passwd *pw;
{
	SM_REQUIRE(user != NULL);
	(void) sm_strlcpy(user->mbdb_name, pw->pw_name,
			  sizeof(user->mbdb_name));
	user->mbdb_uid = pw->pw_uid;
	user->mbdb_gid = pw->pw_gid;
	sm_pwfullname(pw->pw_gecos, pw->pw_name, user->mbdb_fullname,
		      sizeof(user->mbdb_fullname));
	(void) sm_strlcpy(user->mbdb_homedir, pw->pw_dir,
			  sizeof(user->mbdb_homedir));
	(void) sm_strlcpy(user->mbdb_shell, pw->pw_shell,
			  sizeof(user->mbdb_shell));
}

/*
**  SM_PWFULLNAME -- build full name of user from pw_gecos field.
**
**	This routine interprets the strange entry that would appear
**	in the GECOS field of the password file.
**
**	Parameters:
**		gecos -- name to build.
**		user -- the login name of this user (for &).
**		buf -- place to put the result.
**		buflen -- length of buf.
**
**	Returns:
**		none.
*/

#if _FFR_HANDLE_ISO8859_GECOS
static char Latin1ToASCII[128] =
{
	32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
	32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 33,
	99, 80, 36, 89, 124, 36, 34, 99, 97, 60, 45, 45, 114, 45, 111, 42,
	50, 51, 39, 117, 80, 46, 44, 49, 111, 62, 42, 42, 42, 63, 65, 65,
	65, 65, 65, 65, 65, 67, 69, 69, 69, 69, 73, 73, 73, 73, 68, 78, 79,
	79, 79, 79, 79, 88, 79, 85, 85, 85, 85, 89, 80, 66, 97, 97, 97, 97,
	97, 97, 97, 99, 101, 101, 101, 101, 105, 105, 105, 105, 100, 110,
	111, 111, 111, 111, 111, 47, 111, 117, 117, 117, 117, 121, 112, 121
};
#endif /* _FFR_HANDLE_ISO8859_GECOS */

void
sm_pwfullname(gecos, user, buf, buflen)
	register char *gecos;
	char *user;
	char *buf;
	size_t buflen;
{
	register char *p;
	register char *bp = buf;

	if (*gecos == '*')
		gecos++;

	/* copy gecos, interpolating & to be full name */
	for (p = gecos; *p != '\0' && *p != ',' && *p != ';' && *p != '%'; p++)
	{
		if (bp >= &buf[buflen - 1])
		{
			/* buffer overflow -- just use login name */
			(void) sm_strlcpy(buf, user, buflen);
			return;
		}
		if (*p == '&')
		{
			/* interpolate full name */
			(void) sm_strlcpy(bp, user, buflen - (bp - buf));
			*bp = toupper(*bp);
			bp += strlen(bp);
		}
		else
		{
#if _FFR_HANDLE_ISO8859_GECOS
			if ((unsigned char) *p >= 128)
				*bp++ = Latin1ToASCII[(unsigned char) *p - 128];
			else
#endif /* _FFR_HANDLE_ISO8859_GECOS */
				*bp++ = *p;
		}
	}
	*bp = '\0';
}

/*
**  /etc/passwd implementation.
*/

/*
**  MBDB_PW_INITIALIZE -- initialize getpwnam() version
**
**	Parameters:
**		arg -- unused.
**
**	Results:
**		EX_OK.
*/

/* ARGSUSED0 */
static int
mbdb_pw_initialize(arg)
	char *arg;
{
	return EX_OK;
}

/*
**  MBDB_PW_LOOKUP -- look up a local mail recipient, given name
**
**	Parameters:
**		name -- name of local mail recipient
**		user -- pointer to structure to fill in on success
**
**	Results:
**		On success, fill in *user and return EX_OK.
**		Failure: EX_NOUSER.
*/

static int
mbdb_pw_lookup(name, user)
	char *name;
	SM_MBDB_T *user;
{
	struct passwd *pw;

#if HESIOD && !HESIOD_ALLOW_NUMERIC_LOGIN
	/* DEC Hesiod getpwnam accepts numeric strings -- short circuit it */
	{
		char *p;

		for (p = name; *p != '\0'; p++)
			if (!isascii(*p) || !isdigit(*p))
				break;
		if (*p == '\0')
			return EX_NOUSER;
	}
#endif /* HESIOD && !HESIOD_ALLOW_NUMERIC_LOGIN */

	errno = 0;
	pw = getpwnam(name);
	if (pw == NULL)
	{
#if _FFR_USE_GETPWNAM_ERRNO
		/*
		**  Only enable this code iff
		**  user unknown <-> getpwnam() == NULL && errno == 0
		**  (i.e., errno unchanged); see the POSIX spec.
		*/

		if (errno != 0)
			return EX_TEMPFAIL;
#endif /* _FFR_USE_GETPWNAM_ERRNO */
		return EX_NOUSER;
	}

	sm_mbdb_frompw(user, pw);
	return EX_OK;
}

/*
**  MBDB_PW_TERMINATE -- terminate connection to the mailbox database
**
**	Parameters:
**		none.
**
**	Results:
**		none.
*/

static void
mbdb_pw_terminate()
{
	endpwent();
}

#if LDAPMAP
# if _LDAP_EXAMPLE_
/*
**  LDAP example implementation based on RFC 2307, "An Approach for Using
**  LDAP as a Network Information Service":
**
**	( nisSchema.1.0 NAME 'uidNumber'
**	  DESC 'An integer uniquely identifying a user in an
**		administrative domain'
**	  EQUALITY integerMatch SYNTAX 'INTEGER' SINGLE-VALUE )
**
**	( nisSchema.1.1 NAME 'gidNumber'
**	  DESC 'An integer uniquely identifying a group in an
**		administrative domain'
**	  EQUALITY integerMatch SYNTAX 'INTEGER' SINGLE-VALUE )
**
**	( nisSchema.1.2 NAME 'gecos'
**	  DESC 'The GECOS field; the common name'
**	  EQUALITY caseIgnoreIA5Match
**	  SUBSTRINGS caseIgnoreIA5SubstringsMatch
**	  SYNTAX 'IA5String' SINGLE-VALUE )
**
**	( nisSchema.1.3 NAME 'homeDirectory'
**	  DESC 'The absolute path to the home directory'
**	  EQUALITY caseExactIA5Match
**	  SYNTAX 'IA5String' SINGLE-VALUE )
**
**	( nisSchema.1.4 NAME 'loginShell'
**	  DESC 'The path to the login shell'
**	  EQUALITY caseExactIA5Match
**	  SYNTAX 'IA5String' SINGLE-VALUE )
**
**	( nisSchema.2.0 NAME 'posixAccount' SUP top AUXILIARY
**	  DESC 'Abstraction of an account with POSIX attributes'
**	  MUST ( cn $ uid $ uidNumber $ gidNumber $ homeDirectory )
**	  MAY ( userPassword $ loginShell $ gecos $ description ) )
**
*/

#  define MBDB_LDAP_LABEL		"MailboxDatabase"

#  ifndef MBDB_LDAP_FILTER
#   define MBDB_LDAP_FILTER		"(&(objectClass=posixAccount)(uid=%0))"
#  endif /* MBDB_LDAP_FILTER */

#  ifndef MBDB_DEFAULT_LDAP_BASEDN
#   define MBDB_DEFAULT_LDAP_BASEDN	NULL
#  endif /* MBDB_DEFAULT_LDAP_BASEDN */

#  ifndef MBDB_DEFAULT_LDAP_SERVER
#   define MBDB_DEFAULT_LDAP_SERVER	NULL
#  endif /* MBDB_DEFAULT_LDAP_SERVER */

/*
**  MBDB_LDAP_INITIALIZE -- initialize LDAP version
**
**	Parameters:
**		arg -- LDAP specification
**
**	Results:
**		EX_OK on success, or an EX_* code on failure.
*/

static int
mbdb_ldap_initialize(arg)
	char *arg;
{
	sm_ldap_clear(&LDAPLMAP);
	LDAPLMAP.ldap_base = MBDB_DEFAULT_LDAP_BASEDN;
	LDAPLMAP.ldap_host = MBDB_DEFAULT_LDAP_SERVER;
	LDAPLMAP.ldap_filter = MBDB_LDAP_FILTER;

	/* Only want one match */
	LDAPLMAP.ldap_sizelimit = 1;

	/* interpolate new ldap_base and ldap_host from arg if given */
	if (arg != NULL && *arg != '\0')
	{
		char *new;
		char *sep;
		size_t len;

		len = strlen(arg) + 1;
		new = sm_malloc(len);
		if (new == NULL)
			return EX_TEMPFAIL;
		(void) sm_strlcpy(new, arg, len);
		sep = strrchr(new, '@');
		if (sep != NULL)
		{
			*sep++ = '\0';
			LDAPLMAP.ldap_host = sep;
		}
		LDAPLMAP.ldap_base = new;
	}
	return EX_OK;
}


/*
**  MBDB_LDAP_LOOKUP -- look up a local mail recipient, given name
**
**	Parameters:
**		name -- name of local mail recipient
**		user -- pointer to structure to fill in on success
**
**	Results:
**		On success, fill in *user and return EX_OK.
**		Failure: EX_NOUSER.
*/

#define NEED_FULLNAME	0x01
#define NEED_HOMEDIR	0x02
#define NEED_SHELL	0x04
#define NEED_UID	0x08
#define NEED_GID	0x10

static int
mbdb_ldap_lookup(name, user)
	char *name;
	SM_MBDB_T *user;
{
	int msgid;
	int need;
	int ret;
	int save_errno;
	LDAPMessage *entry;
	BerElement *ber;
	char *attr = NULL;

	if (strlen(name) >= sizeof(user->mbdb_name))
	{
		errno = EINVAL;
		return EX_NOUSER;
	}

	if (LDAPLMAP.ldap_filter == NULL)
	{
		/* map not initialized, but don't have arg here */
		errno = EFAULT;
		return EX_TEMPFAIL;
	}

	if (LDAPLMAP.ldap_pid != getpid())
	{
		/* re-open map in this child process */
		LDAPLMAP.ldap_ld = NULL;
	}

	if (LDAPLMAP.ldap_ld == NULL)
	{
		/* map not open, try to open now */
		if (!sm_ldap_start(MBDB_LDAP_LABEL, &LDAPLMAP))
			return EX_TEMPFAIL;
	}

	sm_ldap_setopts(LDAPLMAP.ldap_ld, &LDAPLMAP);
	msgid = sm_ldap_search(&LDAPLMAP, name);
	if (msgid == -1)
	{
		save_errno = sm_ldap_geterrno(LDAPLMAP.ldap_ld) + E_LDAPBASE;
#  ifdef LDAP_SERVER_DOWN
		if (errno == LDAP_SERVER_DOWN)
		{
			/* server disappeared, try reopen on next search */
			sm_ldap_close(&LDAPLMAP);
		}
#  endif /* LDAP_SERVER_DOWN */
		errno = save_errno;
		return EX_TEMPFAIL;
	}

	/* Get results */
	ret = ldap_result(LDAPLMAP.ldap_ld, msgid, 1,
			  (LDAPLMAP.ldap_timeout.tv_sec == 0 ? NULL :
			   &(LDAPLMAP.ldap_timeout)),
			  &(LDAPLMAP.ldap_res));

	if (ret != LDAP_RES_SEARCH_RESULT &&
	    ret != LDAP_RES_SEARCH_ENTRY)
	{
		if (ret == 0)
			errno = ETIMEDOUT;
		else
			errno = sm_ldap_geterrno(LDAPLMAP.ldap_ld);
		ret = EX_TEMPFAIL;
		goto abort;
	}

	entry = ldap_first_entry(LDAPLMAP.ldap_ld, LDAPLMAP.ldap_res);
	if (entry == NULL)
	{
		int rc;

		/*  
		**  We may have gotten an LDAP_RES_SEARCH_RESULT response
		**  with an error inside it, so we have to extract that
		**  with ldap_parse_result().  This can happen when talking
		**  to an LDAP proxy whose backend has gone down.
		*/

		save_errno = ldap_parse_result(LDAPLMAP.ldap_ld,
					       LDAPLMAP.ldap_res, &rc, NULL,
					       NULL, NULL, NULL, 0);
		if (save_errno == LDAP_SUCCESS)
			save_errno = rc;
		if (save_errno == LDAP_SUCCESS)
		{
			errno = ENOENT;
			ret = EX_NOUSER;
		}
		else
		{
			errno = save_errno;
			ret = EX_TEMPFAIL;
		}
		goto abort;
	}

# if !defined(LDAP_VERSION_MAX) && !defined(LDAP_OPT_SIZELIMIT)
	/*
	**  Reset value to prevent lingering
	**  LDAP_DECODING_ERROR due to
	**  OpenLDAP 1.X's hack (see below)
	*/

	LDAPLMAP.ldap_ld->ld_errno = LDAP_SUCCESS;
# endif /* !defined(LDAP_VERSION_MAX) !defined(LDAP_OPT_SIZELIMIT) */

	ret = EX_OK;
	need = NEED_FULLNAME|NEED_HOMEDIR|NEED_SHELL|NEED_UID|NEED_GID;
	for (attr = ldap_first_attribute(LDAPLMAP.ldap_ld, entry, &ber);
	     attr != NULL;
	     attr = ldap_next_attribute(LDAPLMAP.ldap_ld, entry, ber))
	{
		char **vals;

		vals = ldap_get_values(LDAPLMAP.ldap_ld, entry, attr);
		if (vals == NULL)
		{
			errno = sm_ldap_geterrno(LDAPLMAP.ldap_ld);
			if (errno == LDAP_SUCCESS)
			{
				ldap_memfree(attr);
				continue;
			}

			/* Must be an error */
			errno += E_LDAPBASE;
			ret = EX_TEMPFAIL;
			goto abort;
		}

# if !defined(LDAP_VERSION_MAX) && !defined(LDAP_OPT_SIZELIMIT)
		/*
		**  Reset value to prevent lingering
		**  LDAP_DECODING_ERROR due to
		**  OpenLDAP 1.X's hack (see below)
		*/

		LDAPLMAP.ldap_ld->ld_errno = LDAP_SUCCESS;
# endif /* !defined(LDAP_VERSION_MAX) !defined(LDAP_OPT_SIZELIMIT) */

		if (vals[0] == NULL || vals[0][0] == '\0')
			goto skip;

		if (strcasecmp(attr, "gecos") == 0)
		{
			if (!bitset(NEED_FULLNAME, need) ||
			    strlen(vals[0]) >= sizeof(user->mbdb_fullname))
				goto skip;

			sm_pwfullname(vals[0], name, user->mbdb_fullname,
				      sizeof(user->mbdb_fullname));
			need &= ~NEED_FULLNAME;
		}
		else if (strcasecmp(attr, "homeDirectory") == 0)
		{
			if (!bitset(NEED_HOMEDIR, need) ||
			    strlen(vals[0]) >= sizeof(user->mbdb_homedir))
				goto skip;

			(void) sm_strlcpy(user->mbdb_homedir, vals[0],
					  sizeof(user->mbdb_homedir));
			need &= ~NEED_HOMEDIR;
		}
		else if (strcasecmp(attr, "loginShell") == 0)
		{
			if (!bitset(NEED_SHELL, need) ||
			    strlen(vals[0]) >= sizeof(user->mbdb_shell))
				goto skip;

			(void) sm_strlcpy(user->mbdb_shell, vals[0],
					  sizeof(user->mbdb_shell));
			need &= ~NEED_SHELL;
		}
		else if (strcasecmp(attr, "uidNumber") == 0)
		{
			char *p;

			if (!bitset(NEED_UID, need))
				goto skip;

			for (p = vals[0]; *p != '\0'; p++)
			{
				/* allow negative numbers */
				if (p == vals[0] && *p == '-')
				{
					/* but not simply '-' */
					if (*(p + 1) == '\0')
						goto skip;
				}
				else if (!isascii(*p) || !isdigit(*p))
					goto skip;
			}
			user->mbdb_uid = atoi(vals[0]);
			need &= ~NEED_UID;
		}
		else if (strcasecmp(attr, "gidNumber") == 0)
		{
			char *p;

			if (!bitset(NEED_GID, need))
				goto skip;

			for (p = vals[0]; *p != '\0'; p++)
			{
				/* allow negative numbers */
				if (p == vals[0] && *p == '-')
				{
					/* but not simply '-' */
					if (*(p + 1) == '\0')
						goto skip;
				}
				else if (!isascii(*p) || !isdigit(*p))
					goto skip;
			}
			user->mbdb_gid = atoi(vals[0]);
			need &= ~NEED_GID;
		}

skip:
		ldap_value_free(vals);
		ldap_memfree(attr);
	}

	errno = sm_ldap_geterrno(LDAPLMAP.ldap_ld);

	/*
	**  We check errno != LDAP_DECODING_ERROR since
	**  OpenLDAP 1.X has a very ugly *undocumented*
	**  hack of returning this error code from
	**  ldap_next_attribute() if the library freed the
	**  ber attribute.  See:
	**  http://www.openldap.org/lists/openldap-devel/9901/msg00064.html
	*/

	if (errno != LDAP_SUCCESS &&
	    errno != LDAP_DECODING_ERROR)
	{
		/* Must be an error */
		errno += E_LDAPBASE;
		ret = EX_TEMPFAIL;
		goto abort;
	}

 abort:
	save_errno = errno;
	if (attr != NULL)
	{
		ldap_memfree(attr);
		attr = NULL;
	}
	if (LDAPLMAP.ldap_res != NULL)
	{
		ldap_msgfree(LDAPLMAP.ldap_res);
		LDAPLMAP.ldap_res = NULL;
	}
	if (ret == EX_OK)
	{
		if (need == 0)
		{
			(void) sm_strlcpy(user->mbdb_name, name,
					  sizeof(user->mbdb_name));
			save_errno = 0;
		}
		else
		{
			ret = EX_NOUSER;
			save_errno = EINVAL;
		}
	}
	errno = save_errno;
	return ret;
}

/*
**  MBDB_LDAP_TERMINATE -- terminate connection to the mailbox database
**
**	Parameters:
**		none.
**
**	Results:
**		none.
*/

static void
mbdb_ldap_terminate()
{
	sm_ldap_close(&LDAPLMAP);
}
# endif /* _LDAP_EXAMPLE_ */
#endif /* LDAPMAP */
