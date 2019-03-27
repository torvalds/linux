/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: strexit.c,v 1.6 2013-11-22 20:51:43 ca Exp $")
#include <sm/string.h>
#include <sm/sysexits.h>

/*
**  SM_STREXIT -- convert EX_* value from <sm/sysexits.h> to a character string
**
**	This function is analogous to strerror(), except that it
**	operates on EX_* values from <sm/sysexits.h>.
**
**	Parameters:
**		ex -- EX_* value
**
**	Results:
**		pointer to a static message string
*/

char *
sm_strexit(ex)
	int ex;
{
	char *msg;
	static char buf[64];

	msg = sm_sysexitmsg(ex);
	if (msg == NULL)
	{
		(void) sm_snprintf(buf, sizeof buf, "Unknown exit status %d",
				   ex);
		msg = buf;
	}
	return msg;
}

/*
**  SM_SYSEXITMSG -- convert an EX_* value to a character string, or NULL
**
**	Parameters:
**		ex -- EX_* value
**
**	Results:
**		If ex is a known exit value, then a pointer to a static
**		message string is returned.  Otherwise NULL is returned.
*/

char *
sm_sysexitmsg(ex)
	int ex;
{
	char *msg;

	msg = sm_sysexmsg(ex);
	if (msg != NULL)
		return &msg[11];
	else
		return msg;
}

/*
**  SM_SYSEXMSG -- convert an EX_* value to a character string, or NULL
**
**	Parameters:
**		ex -- EX_* value
**
**	Results:
**		If ex is a known exit value, then a pointer to a static
**		string is returned.  Otherwise NULL is returned.
**		The string contains the following fixed width fields:
**		 [0]	':' if there is an errno value associated with this
**			exit value, otherwise ' '.
**		 [1,3]	3 digit SMTP error code
**		 [4]	' '
**		 [5,9]	3 digit SMTP extended error code
**		 [10]	' '
**		 [11,]	message string
*/

char *
sm_sysexmsg(ex)
	int ex;
{
	switch (ex)
	{
	  case EX_USAGE:
		return " 500 5.0.0 Command line usage error";
	  case EX_DATAERR:
		return " 501 5.6.0 Data format error";
	  case EX_NOINPUT:
		return ":550 5.3.0 Cannot open input";
	  case EX_NOUSER:
		return " 550 5.1.1 User unknown";
	  case EX_NOHOST:
		return " 550 5.1.2 Host unknown";
	  case EX_UNAVAILABLE:
		return " 554 5.0.0 Service unavailable";
	  case EX_SOFTWARE:
		return ":554 5.3.0 Internal error";
	  case EX_OSERR:
		return ":451 4.0.0 Operating system error";
	  case EX_OSFILE:
		return ":554 5.3.5 System file missing";
	  case EX_CANTCREAT:
		return ":550 5.0.0 Can't create output";
	  case EX_IOERR:
		return ":451 4.0.0 I/O error";
	  case EX_TEMPFAIL:
		return " 450 4.0.0 Deferred";
	  case EX_PROTOCOL:
		return " 554 5.5.0 Remote protocol error";
	  case EX_NOPERM:
		return ":550 5.0.0 Insufficient permission";
	  case EX_CONFIG:
		return " 554 5.3.5 Local configuration error";
	  default:
		return NULL;
	}
}
