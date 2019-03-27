/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>
SM_RCSID("@(#)$Id: cf.c,v 8.20 2013-11-22 20:51:50 ca Exp $")
#include <sendmail/pathnames.h>

/*
**  GETCFNAME -- return the name of the .cf file to use.
**
**	Some systems (e.g., NeXT) determine this dynamically.
**
**	For others: returns submit.cf or sendmail.cf depending
**		on the modes.
**
**	Parameters:
**		opmode -- operation mode.
**		submitmode -- submit mode.
**		cftype -- may request a certain cf file.
**		conffile -- if set, return it.
**
**	Returns:
**		name of .cf file.
*/

char *
getcfname(opmode, submitmode, cftype, conffile)
	int opmode;
	int submitmode;
	int cftype;
	char *conffile;
{
#if NETINFO
	char *cflocation;
#endif /* NETINFO */

	if (conffile != NULL)
		return conffile;

	if (cftype == SM_GET_SUBMIT_CF ||
	    ((submitmode != SUBMIT_UNKNOWN ||
	      opmode == MD_DELIVER ||
	      opmode == MD_ARPAFTP ||
	      opmode == MD_SMTP) &&
	     cftype != SM_GET_SENDMAIL_CF))
	{
		struct stat sbuf;
		static char cf[MAXPATHLEN];

#if NETINFO
		cflocation = ni_propval("/locations", NULL, "sendmail",
					"submit.cf", '\0');
		if (cflocation != NULL)
			(void) sm_strlcpy(cf, cflocation, sizeof cf);
		else
#endif /* NETINFO */
			(void) sm_strlcpyn(cf, sizeof cf, 2, _DIR_SENDMAILCF,
					   "submit.cf");
		if (cftype == SM_GET_SUBMIT_CF || stat(cf, &sbuf) == 0)
			return cf;
	}
#if NETINFO
	cflocation = ni_propval("/locations", NULL, "sendmail",
				"sendmail.cf", '\0');
	if (cflocation != NULL)
		return cflocation;
#endif /* NETINFO */
	return _PATH_SENDMAILCF;
}
