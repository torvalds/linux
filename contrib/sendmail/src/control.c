/*
 * Copyright (c) 1998-2004, 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: control.c,v 8.130 2013-11-22 20:51:55 ca Exp $")

#include <sm/fdset.h>

/* values for cmd_code */
#define CMDERROR	0	/* bad command */
#define CMDRESTART	1	/* restart daemon */
#define CMDSHUTDOWN	2	/* end daemon */
#define CMDHELP		3	/* help */
#define CMDSTATUS	4	/* daemon status */
#define CMDMEMDUMP	5	/* dump memory, to find memory leaks */
#define CMDMSTAT	6	/* daemon status, more info, tagged data */

struct cmd
{
	char	*cmd_name;	/* command name */
	int	cmd_code;	/* internal code, see below */
};

static struct cmd	CmdTab[] =
{
	{ "help",	CMDHELP		},
	{ "restart",	CMDRESTART	},
	{ "shutdown",	CMDSHUTDOWN	},
	{ "status",	CMDSTATUS	},
	{ "memdump",	CMDMEMDUMP	},
	{ "mstat",	CMDMSTAT	},
	{ NULL,		CMDERROR	}
};



static void	controltimeout __P((int));
int ControlSocket = -1;

/*
**  OPENCONTROLSOCKET -- create/open the daemon control named socket
**
**	Creates and opens a named socket for external control over
**	the sendmail daemon.
**
**	Parameters:
**		none.
**
**	Returns:
**		0 if successful, -1 otherwise
*/

int
opencontrolsocket()
{
# if NETUNIX
	int save_errno;
	int rval;
	long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_CREAT|SFF_MUSTOWN;
	struct sockaddr_un controladdr;

	if (ControlSocketName == NULL || *ControlSocketName == '\0')
		return 0;

	if (strlen(ControlSocketName) >= sizeof(controladdr.sun_path))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	rval = safefile(ControlSocketName, RunAsUid, RunAsGid, RunAsUserName,
			sff, S_IRUSR|S_IWUSR, NULL);

	/* if not safe, don't create */
	if (rval != 0)
	{
		errno = rval;
		return -1;
	}

	ControlSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ControlSocket < 0)
		return -1;
	if (SM_FD_SETSIZE > 0 && ControlSocket >= SM_FD_SETSIZE)
	{
		clrcontrol();
		errno = EINVAL;
		return -1;
	}

	(void) unlink(ControlSocketName);
	memset(&controladdr, '\0', sizeof(controladdr));
	controladdr.sun_family = AF_UNIX;
	(void) sm_strlcpy(controladdr.sun_path, ControlSocketName,
			  sizeof(controladdr.sun_path));

	if (bind(ControlSocket, (struct sockaddr *) &controladdr,
		 sizeof(controladdr)) < 0)
	{
		save_errno = errno;
		clrcontrol();
		errno = save_errno;
		return -1;
	}

	if (geteuid() == 0)
	{
		uid_t u = 0;

		if (RunAsUid != 0)
			u = RunAsUid;
		else if (TrustedUid != 0)
			u = TrustedUid;

		if (u != 0 &&
		    chown(ControlSocketName, u, -1) < 0)
		{
			save_errno = errno;
			sm_syslog(LOG_ALERT, NOQID,
				  "ownership change on %s to uid %d failed: %s",
				  ControlSocketName, (int) u,
				  sm_errstring(save_errno));
			message("050 ownership change on %s to uid %d failed: %s",
				ControlSocketName, (int) u,
				sm_errstring(save_errno));
			closecontrolsocket(true);
			errno = save_errno;
			return -1;
		}
	}

	if (chmod(ControlSocketName, S_IRUSR|S_IWUSR) < 0)
	{
		save_errno = errno;
		closecontrolsocket(true);
		errno = save_errno;
		return -1;
	}

	if (listen(ControlSocket, 8) < 0)
	{
		save_errno = errno;
		closecontrolsocket(true);
		errno = save_errno;
		return -1;
	}
# endif /* NETUNIX */
	return 0;
}
/*
**  CLOSECONTROLSOCKET -- close the daemon control named socket
**
**	Close a named socket.
**
**	Parameters:
**		fullclose -- if set, close the socket and remove it;
**			     otherwise, just remove it
**
**	Returns:
**		none.
*/

void
closecontrolsocket(fullclose)
	bool fullclose;
{
# if NETUNIX
	long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_CREAT|SFF_MUSTOWN;

	if (ControlSocket >= 0)
	{
		int rval;

		if (fullclose)
		{
			(void) close(ControlSocket);
			ControlSocket = -1;
		}

		rval = safefile(ControlSocketName, RunAsUid, RunAsGid,
				RunAsUserName, sff, S_IRUSR|S_IWUSR, NULL);

		/* if not safe, don't unlink */
		if (rval != 0)
			return;

		if (unlink(ControlSocketName) < 0)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "Could not remove control socket: %s",
				  sm_errstring(errno));
			return;
		}
	}
# endif /* NETUNIX */
	return;
}
/*
**  CLRCONTROL -- reset the control connection
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		releases any resources used by the control interface.
*/

void
clrcontrol()
{
# if NETUNIX
	if (ControlSocket >= 0)
		(void) close(ControlSocket);
	ControlSocket = -1;
# endif /* NETUNIX */
}
/*
**  CONTROL_COMMAND -- read and process command from named socket
**
**	Read and process the command from the opened socket.
**	Exits when done since it is running in a forked child.
**
**	Parameters:
**		sock -- the opened socket from getrequests()
**		e -- the current envelope
**
**	Returns:
**		none.
*/

static jmp_buf	CtxControlTimeout;

/* ARGSUSED0 */
static void
controltimeout(timeout)
	int timeout;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(CtxControlTimeout, 1);
}

void
control_command(sock, e)
	int sock;
	ENVELOPE *e;
{
	volatile int exitstat = EX_OK;
	SM_FILE_T *s = NULL;
	SM_EVENT *ev = NULL;
	SM_FILE_T *traffic;
	SM_FILE_T *oldout;
	char *cmd;
	char *p;
	struct cmd *c;
	char cmdbuf[MAXLINE];
	char inp[MAXLINE];

	sm_setproctitle(false, e, "control cmd read");

	if (TimeOuts.to_control > 0)
	{
		/* handle possible input timeout */
		if (setjmp(CtxControlTimeout) != 0)
		{
			if (LogLevel > 2)
				sm_syslog(LOG_NOTICE, e->e_id,
					  "timeout waiting for input during control command");
			exit(EX_IOERR);
		}
		ev = sm_setevent(TimeOuts.to_control, controltimeout,
				 TimeOuts.to_control);
	}

	s = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT, (void *) &sock,
		       SM_IO_RDWR, NULL);
	if (s == NULL)
	{
		int save_errno = errno;

		(void) close(sock);
		errno = save_errno;
		exit(EX_IOERR);
	}
	(void) sm_io_setvbuf(s, SM_TIME_DEFAULT, NULL,
			     SM_IO_NBF, SM_IO_BUFSIZ);

	if (sm_io_fgets(s, SM_TIME_DEFAULT, inp, sizeof(inp)) < 0)
	{
		(void) sm_io_close(s, SM_TIME_DEFAULT);
		exit(EX_IOERR);
	}
	(void) sm_io_flush(s, SM_TIME_DEFAULT);

	/* clean up end of line */
	fixcrlf(inp, true);

	sm_setproctitle(false, e, "control: %s", inp);

	/* break off command */
	for (p = inp; isascii(*p) && isspace(*p); p++)
		continue;
	cmd = cmdbuf;
	while (*p != '\0' &&
	       !(isascii(*p) && isspace(*p)) &&
	       cmd < &cmdbuf[sizeof(cmdbuf) - 2])
		*cmd++ = *p++;
	*cmd = '\0';

	/* throw away leading whitespace */
	while (isascii(*p) && isspace(*p))
		p++;

	/* decode command */
	for (c = CmdTab; c->cmd_name != NULL; c++)
	{
		if (sm_strcasecmp(c->cmd_name, cmdbuf) == 0)
			break;
	}

	switch (c->cmd_code)
	{
	  case CMDHELP:		/* get help */
		traffic = TrafficLogFile;
		TrafficLogFile = NULL;
		oldout = OutChannel;
		OutChannel = s;
		help("control", e);
		TrafficLogFile = traffic;
		OutChannel = oldout;
		break;

	  case CMDRESTART:	/* restart the daemon */
		(void) sm_io_fprintf(s, SM_TIME_DEFAULT, "OK\r\n");
		exitstat = EX_RESTART;
		break;

	  case CMDSHUTDOWN:	/* kill the daemon */
		(void) sm_io_fprintf(s, SM_TIME_DEFAULT, "OK\r\n");
		exitstat = EX_SHUTDOWN;
		break;

	  case CMDSTATUS:	/* daemon status */
		proc_list_probe();
		{
			int qgrp;
			long bsize;
			long free;

			/* XXX need to deal with different partitions */
			qgrp = e->e_qgrp;
			if (!ISVALIDQGRP(qgrp))
				qgrp = 0;
			free = freediskspace(Queue[qgrp]->qg_qdir, &bsize);

			/*
			**  Prevent overflow and don't lose
			**  precision (if bsize == 512)
			*/

			if (free > 0)
				free = (long)((double) free *
					      ((double) bsize / 1024));

			(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
					     "%d/%d/%ld/%d\r\n",
					     CurChildren, MaxChildren,
					     free, getla());
		}
		proc_list_display(s, "");
		break;

	  case CMDMSTAT:	/* daemon status, extended, tagged format */
		proc_list_probe();
		(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
				     "C:%d\r\nM:%d\r\nL:%d\r\n",
				     CurChildren, MaxChildren,
				     getla());
		printnqe(s, "Q:");
		disk_status(s, "D:");
		proc_list_display(s, "P:");
		break;

	  case CMDMEMDUMP:	/* daemon memory dump, to find memory leaks */
# if SM_HEAP_CHECK
		/* dump the heap, if we are checking for memory leaks */
		if (sm_debug_active(&SmHeapCheck, 2))
		{
			sm_heap_report(s, sm_debug_level(&SmHeapCheck) - 1);
		}
		else
		{
			(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
					     "Memory dump unavailable.\r\n");
			(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
					     "To fix, run sendmail with -dsm_check_heap.4\r\n");
		}
# else /* SM_HEAP_CHECK */
		(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
				     "Memory dump unavailable.\r\n");
		(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
				     "To fix, rebuild with -DSM_HEAP_CHECK\r\n");
# endif /* SM_HEAP_CHECK */
		break;

	  case CMDERROR:	/* unknown command */
		(void) sm_io_fprintf(s, SM_TIME_DEFAULT,
				     "Bad command (%s)\r\n", cmdbuf);
		break;
	}
	(void) sm_io_close(s, SM_TIME_DEFAULT);
	if (ev != NULL)
		sm_clrevent(ev);
	exit(exitstat);
}
