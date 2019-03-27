/*
 * Copyright (c) 2000-2003, 2007 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: config.c,v 1.32 2013-11-22 20:51:42 ca Exp $")

#include <stdlib.h>
#include <sm/heap.h>
#include <sm/string.h>
#include <sm/conf.h>

/*
**  PUTENV -- emulation of putenv() in terms of setenv()
**
**	Not needed on Posix-compliant systems.
**	This doesn't have full Posix semantics, but it's good enough
**		for sendmail.
**
**	Parameter:
**		env -- the environment to put.
**
**	Returns:
**		0 on success, < 0 on failure.
*/

#if NEEDPUTENV

# if NEEDPUTENV == 2		/* no setenv(3) call available */

int
putenv(str)
	char *str;
{
	char **current;
	int matchlen, envlen = 0;
	char *tmp;
	char **newenv;
	static bool first = true;
	extern char **environ;

	/*
	**  find out how much of str to match when searching
	**  for a string to replace.
	*/

	if ((tmp = strchr(str, '=')) == NULL || tmp == str)
		matchlen = strlen(str);
	else
		matchlen = (int) (tmp - str);
	++matchlen;

	/*
	**  Search for an existing string in the environment and find the
	**  length of environ.  If found, replace and exit.
	*/

	for (current = environ; *current != NULL; current++)
	{
		++envlen;

		if (strncmp(str, *current, matchlen) == 0)
		{
			/* found it, now insert the new version */
			*current = (char *) str;
			return 0;
		}
	}

	/*
	**  There wasn't already a slot so add space for a new slot.
	**  If this is our first time through, use malloc(), else realloc().
	*/

	if (first)
	{
		newenv = (char **) sm_malloc(sizeof(char *) * (envlen + 2));
		if (newenv == NULL)
			return -1;

		first = false;
		(void) memcpy(newenv, environ, sizeof(char *) * envlen);
	}
	else
	{
		newenv = (char **) sm_realloc((char *) environ,
					      sizeof(char *) * (envlen + 2));
		if (newenv == NULL)
			return -1;
	}

	/* actually add in the new entry */
	environ = newenv;
	environ[envlen] = (char *) str;
	environ[envlen + 1] = NULL;

	return 0;
}

# else /* NEEDPUTENV == 2 */

int
putenv(env)
	char *env;
{
	char *p;
	int l;
	char nbuf[100];

	p = strchr(env, '=');
	if (p == NULL)
		return 0;
	l = p - env;
	if (l > sizeof nbuf - 1)
		l = sizeof nbuf - 1;
	memmove(nbuf, env, l);
	nbuf[l] = '\0';
	return setenv(nbuf, ++p, 1);
}

# endif /* NEEDPUTENV == 2 */
#endif /* NEEDPUTENV */
/*
**  UNSETENV -- remove a variable from the environment
**
**	Not needed on newer systems.
**
**	Parameters:
**		name -- the string name of the environment variable to be
**			deleted from the current environment.
**
**	Returns:
**		none.
**
**	Globals:
**		environ -- a pointer to the current environment.
**
**	Side Effects:
**		Modifies environ.
*/

#if !HASUNSETENV

void
unsetenv(name)
	char *name;
{
	extern char **environ;
	register char **pp;
	int len = strlen(name);

	for (pp = environ; *pp != NULL; pp++)
	{
		if (strncmp(name, *pp, len) == 0 &&
		    ((*pp)[len] == '=' || (*pp)[len] == '\0'))
			break;
	}

	for (; *pp != NULL; pp++)
		*pp = pp[1];
}

#endif /* !HASUNSETENV */

char *SmCompileOptions[] =
{
#if SM_CONF_BROKEN_STRTOD
	"SM_CONF_BROKEN_STRTOD",
#endif /* SM_CONF_BROKEN_STRTOD */
#if SM_CONF_GETOPT
	"SM_CONF_GETOPT",
#endif /* SM_CONF_GETOPT */
#if SM_CONF_LDAP_INITIALIZE
	"SM_CONF_LDAP_INITIALIZE",
#endif /* SM_CONF_LDAP_INITIALIZE */
#if SM_CONF_LDAP_MEMFREE
	"SM_CONF_LDAP_MEMFREE",
#endif /* SM_CONF_LDAP_MEMFREE */
#if SM_CONF_LONGLONG
	"SM_CONF_LONGLONG",
#endif /* SM_CONF_LONGLONG */
#if SM_CONF_MEMCHR
	"SM_CONF_MEMCHR",
#endif /* SM_CONF_MEMCHR */
#if SM_CONF_MSG
	"SM_CONF_MSG",
#endif /* SM_CONF_MSG */
#if SM_CONF_QUAD_T
	"SM_CONF_QUAD_T",
#endif /* SM_CONF_QUAD_T */
#if SM_CONF_SEM
	"SM_CONF_SEM",
#endif /* SM_CONF_SEM */
#if SM_CONF_SETITIMER
	"SM_CONF_SETITIMER",
#endif /* SM_CONF_SETITIMER */
#if SM_CONF_SIGSETJMP
	"SM_CONF_SIGSETJMP",
#endif /* SM_CONF_SIGSETJMP */
#if SM_CONF_SHM
	"SM_CONF_SHM",
#endif /* SM_CONF_SHM */
#if SM_CONF_SHM_DELAY
	"SM_CONF_SHM_DELAY",
#endif /* SM_CONF_SHM_DELAY */
#if SM_CONF_SSIZE_T
	"SM_CONF_SSIZE_T",
#endif /* SM_CONF_SSIZE_T */
#if SM_CONF_STDBOOL_H
	"SM_CONF_STDBOOL_H",
#endif /* SM_CONF_STDBOOL_H */
#if SM_CONF_STDDEF_H
	"SM_CONF_STDDEF_H",
#endif /* SM_CONF_STDDEF_H */

#if 0
/* XXX this is always enabled (for now) */
#if SM_CONF_STRL
	"SM_CONF_STRL",
#endif /* SM_CONF_STRL */
#endif /* 0 */

#if SM_CONF_SYS_CDEFS_H
	"SM_CONF_SYS_CDEFS_H",
#endif /* SM_CONF_SYS_CDEFS_H */
#if SM_CONF_SYSEXITS_H
	"SM_CONF_SYSEXITS_H",
#endif /* SM_CONF_SYSEXITS_H */
#if SM_CONF_UID_GID
	"SM_CONF_UID_GID",
#endif /* SM_CONF_UID_GID */
#if DO_NOT_USE_STRCPY
	"DO_NOT_USE_STRCPY",
#endif /* DO_NOT_USE_STRCPY */
#if SM_HEAP_CHECK
	"SM_HEAP_CHECK",
#endif /* SM_HEAP_CHECK */
#if defined(SM_OS_NAME) && defined(__STDC__)
	"SM_OS=sm_os_" SM_OS_NAME,
#endif /* defined(SM_OS_NAME) && defined(__STDC__) */
#if SM_VA_STD
	"SM_VA_STD",
#endif /* SM_VA_STD */
#if USEKSTAT
	"USEKSTAT",
#endif /* USEKSTAT */
#if USEPROCMEMINFO
	"USEPROCMEMINFO",
#endif /* USEPROCMEMINFO */
#if USESWAPCTL
	"USESWAPCTL",
#endif /* USESWAPCTL */
	NULL
};
