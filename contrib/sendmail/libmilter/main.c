/*
 *  Copyright (c) 1999-2003, 2006, 2007 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: main.c,v 8.85 2013-11-22 20:51:36 ca Exp $")

#define _DEFINE	1
#include "libmilter.h"
#include <fcntl.h>
#include <sys/stat.h>


static smfiDesc_ptr smfi = NULL;

/*
**  SMFI_REGISTER -- register a filter description
**
**	Parameters:
**		smfilter -- description of filter to register
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_register(smfilter)
	smfiDesc_str smfilter;
{
	size_t len;

	if (smfi == NULL)
	{
		smfi = (smfiDesc_ptr) malloc(sizeof *smfi);
		if (smfi == NULL)
			return MI_FAILURE;
	}
	(void) memcpy(smfi, &smfilter, sizeof *smfi);
	if (smfilter.xxfi_name == NULL)
		smfilter.xxfi_name = "Unknown";

	len = strlen(smfilter.xxfi_name) + 1;
	smfi->xxfi_name = (char *) malloc(len);
	if (smfi->xxfi_name == NULL)
		return MI_FAILURE;
	(void) sm_strlcpy(smfi->xxfi_name, smfilter.xxfi_name, len);

	/* compare milter version with hard coded version */
	if ((SM_LM_VRS_MAJOR(smfi->xxfi_version) != SM_LM_VRS_MAJOR(SMFI_VERSION) ||
	     SM_LM_VRS_MINOR(smfi->xxfi_version) != SM_LM_VRS_MINOR(SMFI_VERSION)) &&
	    smfi->xxfi_version != 2 &&
	    smfi->xxfi_version != 3 &&
	    smfi->xxfi_version != 4)
	{
		/* hard failure for now! */
		smi_log(SMI_LOG_ERR,
			"%s: smfi_register: version mismatch application: %d != milter: %d",
			smfi->xxfi_name, smfi->xxfi_version,
			(int) SMFI_VERSION);

		/* XXX how about smfi? */
		free(smfi->xxfi_name);
		return MI_FAILURE;
	}

	return MI_SUCCESS;
}

/*
**  SMFI_STOP -- stop milter
**
**	Parameters:
**		none.
**
**	Returns:
**		success.
*/

int
smfi_stop()
{
	mi_stop_milters(MILTER_STOP);
	return MI_SUCCESS;
}

/*
**  Default values for some variables.
**	Most of these can be changed with the functions below.
*/

static int dbg = 0;
static char *conn = NULL;
static int timeout = MI_TIMEOUT;
static int backlog = MI_SOMAXCONN;

/*
**  SMFI_OPENSOCKET -- try the socket setup to make sure we'll be
**		       able to start up
**
**	Parameters:
**		rmsocket -- if true, instructs libmilter to attempt
**			to remove the socket before creating it;
**			only applies for "local:" or "unix:" sockets
**
**	Return:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_opensocket(rmsocket)
	bool rmsocket;
{
	if (smfi == NULL || conn == NULL)
		return MI_FAILURE;

	return mi_opensocket(conn, backlog, dbg, rmsocket, smfi);
}

/*
**  SMFI_SETDBG -- set debug level.
**
**	Parameters:
**		odbg -- new debug level.
**
**	Returns:
**		MI_SUCCESS
*/

int
smfi_setdbg(odbg)
	int odbg;
{
	dbg = odbg;
	return MI_SUCCESS;
}

/*
**  SMFI_SETTIMEOUT -- set timeout (for read/write).
**
**	Parameters:
**		otimeout -- new timeout.
**
**	Returns:
**		MI_SUCCESS
*/

int
smfi_settimeout(otimeout)
	int otimeout;
{
	timeout = otimeout;
	return MI_SUCCESS;
}

/*
**  SMFI_SETCONN -- set connection information (socket description)
**
**	Parameters:
**		oconn -- new connection information.
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_setconn(oconn)
	char *oconn;
{
	size_t l;

	if (oconn == NULL || *oconn == '\0')
		return MI_FAILURE;
	l = strlen(oconn) + 1;
	if ((conn = (char *) malloc(l)) == NULL)
		return MI_FAILURE;
	if (sm_strlcpy(conn, oconn, l) >= l)
		return MI_FAILURE;
	return MI_SUCCESS;
}

/*
**  SMFI_SETBACKLOG -- set backlog
**
**	Parameters:
**		obacklog -- new backlog.
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_setbacklog(obacklog)
	int obacklog;
{
	if (obacklog <= 0)
		return MI_FAILURE;
	backlog = obacklog;
	return MI_SUCCESS;
}


/*
**  SMFI_MAIN -- setup milter connnection and start listener.
**
**	Parameters:
**		none.
**
**	Returns:
**		MI_SUCCESS/MI_FAILURE
*/

int
smfi_main()
{
	int r;

	(void) signal(SIGPIPE, SIG_IGN);
	if (conn == NULL)
	{
		smi_log(SMI_LOG_FATAL, "%s: missing connection information",
			smfi->xxfi_name);
		return MI_FAILURE;
	}

	(void) atexit(mi_clean_signals);
	if (mi_control_startup(smfi->xxfi_name) != MI_SUCCESS)
	{
		smi_log(SMI_LOG_FATAL,
			"%s: Couldn't start signal thread",
			smfi->xxfi_name);
		return MI_FAILURE;
	}
	r = MI_MONITOR_INIT();

	/* Startup the listener */
	if (mi_listener(conn, dbg, smfi, timeout, backlog) != MI_SUCCESS)
		r = MI_FAILURE;

	return r;
}

