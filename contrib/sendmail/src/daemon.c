/*
 * Copyright (c) 1998-2007, 2009, 2010 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: daemon.c,v 8.698 2013-11-22 20:51:55 ca Exp $")

#if defined(SOCK_STREAM) || defined(__GNU_LIBRARY__)
# define USE_SOCK_STREAM	1
#endif /* defined(SOCK_STREAM) || defined(__GNU_LIBRARY__) */

#if defined(USE_SOCK_STREAM)
# if NETINET || NETINET6
#  include <arpa/inet.h>
# endif /* NETINET || NETINET6 */
# if NAMED_BIND
#  ifndef NO_DATA
#   define NO_DATA	NO_ADDRESS
#  endif /* ! NO_DATA */
# endif /* NAMED_BIND */
#endif /* defined(USE_SOCK_STREAM) */

#if STARTTLS
# include <openssl/rand.h>
#endif /* STARTTLS */

#include <sm/time.h>

#if IP_SRCROUTE && NETINET
# include <netinet/in_systm.h>
# include <netinet/ip.h>
# if HAS_IN_H
#  include <netinet/in.h>
#  ifndef IPOPTION
#   define IPOPTION	ip_opts
#   define IP_LIST	ip_opts
#   define IP_DST	ip_dst
#  endif /* ! IPOPTION */
# else /* HAS_IN_H */
#  include <netinet/ip_var.h>
#  ifndef IPOPTION
#   define IPOPTION	ipoption
#   define IP_LIST	ipopt_list
#   define IP_DST	ipopt_dst
#  endif /* ! IPOPTION */
# endif /* HAS_IN_H */
#endif /* IP_SRCROUTE && NETINET */

#include <sm/fdset.h>

#define DAEMON_C 1
#include <daemon.h>

static void		connecttimeout __P((int));
static int		opendaemonsocket __P((DAEMON_T *, bool));
static unsigned short	setupdaemon __P((SOCKADDR *));
static void		getrequests_checkdiskspace __P((ENVELOPE *e));
static void		setsockaddroptions __P((char *, DAEMON_T *));
static void		printdaemonflags __P((DAEMON_T *));
static int		addr_family __P((char *));
static int		addrcmp __P((struct hostent *, char *, SOCKADDR *));
static void		authtimeout __P((int));

/*
**  DAEMON.C -- routines to use when running as a daemon.
**
**	This entire file is highly dependent on the 4.2 BSD
**	interprocess communication primitives.  No attempt has
**	been made to make this file portable to Version 7,
**	Version 6, MPX files, etc.  If you should try such a
**	thing yourself, I recommend chucking the entire file
**	and starting from scratch.  Basic semantics are:
**
**	getrequests(e)
**		Opens a port and initiates a connection.
**		Returns in a child.  Must set InChannel and
**		OutChannel appropriately.
**	clrdaemon()
**		Close any open files associated with getting
**		the connection; this is used when running the queue,
**		etc., to avoid having extra file descriptors during
**		the queue run and to avoid confusing the network
**		code (if it cares).
**	makeconnection(host, port, mci, e, enough)
**		Make a connection to the named host on the given
**		port. Returns zero on success, else an exit status
**		describing the error.
**	host_map_lookup(map, hbuf, avp, pstat)
**		Convert the entry in hbuf into a canonical form.
*/

static int	NDaemons = 0;			/* actual number of daemons */

static time_t	NextDiskSpaceCheck = 0;

/*
**  GETREQUESTS -- open mail IPC port and get requests.
**
**	Parameters:
**		e -- the current envelope.
**
**	Returns:
**		pointer to flags.
**
**	Side Effects:
**		Waits until some interesting activity occurs.  When
**		it does, a child is created to process it, and the
**		parent waits for completion.  Return from this
**		routine is always in the child.  The file pointers
**		"InChannel" and "OutChannel" should be set to point
**		to the communication channel.
**		May restart persistent queue runners if they have ended
**		for some reason.
*/

BITMAP256 *
getrequests(e)
	ENVELOPE *e;
{
	int t;
	int idx, curdaemon = -1;
	int i, olddaemon = 0;
#if XDEBUG
	bool j_has_dot;
#endif /* XDEBUG */
	char status[MAXLINE];
	SOCKADDR sa;
	SOCKADDR_LEN_T len = sizeof(sa);
#if _FFR_QUEUE_RUN_PARANOIA
	time_t lastrun;
#endif /* _FFR_QUEUE_RUN_PARANOIA */
# if NETUNIX
	extern int ControlSocket;
# endif /* NETUNIX */
	extern ENVELOPE BlankEnvelope;


	/* initialize data for function that generates queue ids */
	init_qid_alg();
	for (idx = 0; idx < NDaemons; idx++)
	{
		Daemons[idx].d_port = setupdaemon(&(Daemons[idx].d_addr));
		Daemons[idx].d_firsttime = true;
		Daemons[idx].d_refuse_connections_until = (time_t) 0;
	}

	/*
	**  Try to actually open the connection.
	*/

	if (tTd(15, 1))
	{
		for (idx = 0; idx < NDaemons; idx++)
		{
			sm_dprintf("getrequests: daemon %s: port %d\n",
				   Daemons[idx].d_name,
				   ntohs(Daemons[idx].d_port));
		}
	}

	/* get a socket for the SMTP connection */
	for (idx = 0; idx < NDaemons; idx++)
		Daemons[idx].d_socksize = opendaemonsocket(&Daemons[idx], true);

	if (opencontrolsocket() < 0)
		sm_syslog(LOG_WARNING, NOQID,
			  "daemon could not open control socket %s: %s",
			  ControlSocketName, sm_errstring(errno));

	/* If there are any queue runners released reapchild() co-ord's */
	(void) sm_signal(SIGCHLD, reapchild);

	/* write the pid to file, command line args to syslog */
	log_sendmail_pid(e);

#if XDEBUG
	{
		char jbuf[MAXHOSTNAMELEN];

		expand("\201j", jbuf, sizeof(jbuf), e);
		j_has_dot = strchr(jbuf, '.') != NULL;
	}
#endif /* XDEBUG */

	/* Add parent process as first item */
	proc_list_add(CurrentPid, "Sendmail daemon", PROC_DAEMON, 0, -1, NULL);

	if (tTd(15, 1))
	{
		for (idx = 0; idx < NDaemons; idx++)
			sm_dprintf("getrequests: daemon %s: socket %d\n",
				Daemons[idx].d_name,
				Daemons[idx].d_socket);
	}

	for (;;)
	{
		register pid_t pid;
		auto SOCKADDR_LEN_T lotherend;
		bool timedout = false;
		bool control = false;
		int save_errno;
		int pipefd[2];
		time_t now;
#if STARTTLS
		long seed;
#endif /* STARTTLS */

		/* see if we are rejecting connections */
		(void) sm_blocksignal(SIGALRM);
		CHECK_RESTART;

		for (idx = 0; idx < NDaemons; idx++)
		{
			/*
			**  XXX do this call outside the loop?
			**	no: refuse_connections may sleep().
			*/

			now = curtime();
			if (now < Daemons[idx].d_refuse_connections_until)
				continue;
			if (bitnset(D_DISABLE, Daemons[idx].d_flags))
				continue;
			if (refuseconnections(e, idx, curdaemon == idx))
			{
				if (Daemons[idx].d_socket >= 0)
				{
					/* close socket so peer fails quickly */
					(void) close(Daemons[idx].d_socket);
					Daemons[idx].d_socket = -1;
				}

				/* refuse connections for next 15 seconds */
				Daemons[idx].d_refuse_connections_until = now + 15;
			}
			else if (Daemons[idx].d_socket < 0 ||
				 Daemons[idx].d_firsttime)
			{
				if (!Daemons[idx].d_firsttime && LogLevel > 8)
					sm_syslog(LOG_INFO, NOQID,
						"accepting connections again for daemon %s",
						Daemons[idx].d_name);

				/* arrange to (re)open the socket if needed */
				(void) opendaemonsocket(&Daemons[idx], false);
				Daemons[idx].d_firsttime = false;
			}
		}

		/* May have been sleeping above, check again */
		CHECK_RESTART;

		getrequests_checkdiskspace(e);

#if XDEBUG
		/* check for disaster */
		{
			char jbuf[MAXHOSTNAMELEN];

			expand("\201j", jbuf, sizeof(jbuf), e);
			if (!wordinclass(jbuf, 'w'))
			{
				dumpstate("daemon lost $j");
				sm_syslog(LOG_ALERT, NOQID,
					  "daemon process doesn't have $j in $=w; see syslog");
				abort();
			}
			else if (j_has_dot && strchr(jbuf, '.') == NULL)
			{
				dumpstate("daemon $j lost dot");
				sm_syslog(LOG_ALERT, NOQID,
					  "daemon process $j lost dot; see syslog");
				abort();
			}
		}
#endif /* XDEBUG */

#if 0
		/*
		**  Andrew Sun <asun@ieps-sun.ml.com> claims that this will
		**  fix the SVr4 problem.  But it seems to have gone away,
		**  so is it worth doing this?
		*/

		if (DaemonSocket >= 0 &&
		    SetNonBlocking(DaemonSocket, false) < 0)
			log an error here;
#endif /* 0 */
		(void) sm_releasesignal(SIGALRM);

		for (;;)
		{
			bool setproc = false;
			int highest = -1;
			fd_set readfds;
			struct timeval timeout;

			CHECK_RESTART;
			FD_ZERO(&readfds);
			for (idx = 0; idx < NDaemons; idx++)
			{
				/* wait for a connection */
				if (Daemons[idx].d_socket >= 0)
				{
					if (!setproc &&
					    !bitnset(D_ETRNONLY,
						     Daemons[idx].d_flags))
					{
						sm_setproctitle(true, e,
								"accepting connections");
						setproc = true;
					}
					if (Daemons[idx].d_socket > highest)
						highest = Daemons[idx].d_socket;
					SM_FD_SET(Daemons[idx].d_socket,
						  &readfds);
				}
			}

#if NETUNIX
			if (ControlSocket >= 0)
			{
				if (ControlSocket > highest)
					highest = ControlSocket;
				SM_FD_SET(ControlSocket, &readfds);
			}
#endif /* NETUNIX */

			timeout.tv_sec = 5;
			timeout.tv_usec = 0;

			t = select(highest + 1, FDSET_CAST &readfds,
				   NULL, NULL, &timeout);

			/* Did someone signal while waiting? */
			CHECK_RESTART;

			curdaemon = -1;
			if (doqueuerun())
			{
				(void) runqueue(true, false, false, false);
#if _FFR_QUEUE_RUN_PARANOIA
				lastrun = now;
#endif /* _FFR_QUEUE_RUN_PARANOIA */
			}
#if _FFR_QUEUE_RUN_PARANOIA
			else if (CheckQueueRunners > 0 && QueueIntvl > 0 &&
				 lastrun + QueueIntvl + CheckQueueRunners < now)
			{

				/*
				**  set lastrun unconditionally to avoid
				**  calling checkqueuerunner() all the time.
				**  That's also why we currently ignore the
				**  result of the function call.
				*/

				(void) checkqueuerunner();
				lastrun = now;
			}
#endif /* _FFR_QUEUE_RUN_PARANOIA */

			if (t <= 0)
			{
				timedout = true;
				break;
			}

			control = false;
			errno = 0;

			/* look "round-robin" for an active socket */
			if ((idx = olddaemon + 1) >= NDaemons)
				idx = 0;
			for (i = 0; i < NDaemons; i++)
			{
				if (Daemons[idx].d_socket >= 0 &&
				    SM_FD_ISSET(Daemons[idx].d_socket,
						&readfds))
				{
					lotherend = Daemons[idx].d_socksize;
					memset(&RealHostAddr, '\0',
					       sizeof(RealHostAddr));
					t = accept(Daemons[idx].d_socket,
						   (struct sockaddr *)&RealHostAddr,
						   &lotherend);

					/*
					**  If remote side closes before
					**  accept() finishes, sockaddr
					**  might not be fully filled in.
					*/

					if (t >= 0 &&
					    (lotherend == 0 ||
# ifdef BSD4_4_SOCKADDR
					     RealHostAddr.sa.sa_len == 0 ||
# endif /* BSD4_4_SOCKADDR */
					     RealHostAddr.sa.sa_family != Daemons[idx].d_addr.sa.sa_family))
					{
						(void) close(t);
						t = -1;
						errno = EINVAL;
					}
					olddaemon = curdaemon = idx;
					break;
				}
				if (++idx >= NDaemons)
					idx = 0;
			}
#if NETUNIX
			if (curdaemon == -1 && ControlSocket >= 0 &&
			    SM_FD_ISSET(ControlSocket, &readfds))
			{
				struct sockaddr_un sa_un;

				lotherend = sizeof(sa_un);
				memset(&sa_un, '\0', sizeof(sa_un));
				t = accept(ControlSocket,
					   (struct sockaddr *)&sa_un,
					   &lotherend);

				/*
				**  If remote side closes before
				**  accept() finishes, sockaddr
				**  might not be fully filled in.
				*/

				if (t >= 0 &&
				    (lotherend == 0 ||
# ifdef BSD4_4_SOCKADDR
				     sa_un.sun_len == 0 ||
# endif /* BSD4_4_SOCKADDR */
				     sa_un.sun_family != AF_UNIX))
				{
					(void) close(t);
					t = -1;
					errno = EINVAL;
				}
				if (t >= 0)
					control = true;
			}
#else /* NETUNIX */
			if (curdaemon == -1)
			{
				/* No daemon to service */
				continue;
			}
#endif /* NETUNIX */
			if (t >= 0 || errno != EINTR)
				break;
		}
		if (timedout)
		{
			timedout = false;
			continue;
		}
		save_errno = errno;
		(void) sm_blocksignal(SIGALRM);
		if (t < 0)
		{
			errno = save_errno;

			/* let's ignore these temporary errors */
			if (save_errno == EINTR
#ifdef EAGAIN
			    || save_errno == EAGAIN
#endif /* EAGAIN */
#ifdef ECONNABORTED
			    || save_errno == ECONNABORTED
#endif /* ECONNABORTED */
#ifdef EWOULDBLOCK
			    || save_errno == EWOULDBLOCK
#endif /* EWOULDBLOCK */
			   )
				continue;

			syserr("getrequests: accept");

			if (curdaemon >= 0)
			{
				/* arrange to re-open socket next time around */
				(void) close(Daemons[curdaemon].d_socket);
				Daemons[curdaemon].d_socket = -1;
#if SO_REUSEADDR_IS_BROKEN
				/*
				**  Give time for bound socket to be released.
				**  This creates a denial-of-service if you can
				**  force accept() to fail on affected systems.
				*/

				Daemons[curdaemon].d_refuse_connections_until =
					curtime() + 15;
#endif /* SO_REUSEADDR_IS_BROKEN */
			}
			continue;
		}

		if (!control)
		{
			/* set some daemon related macros */
			switch (Daemons[curdaemon].d_addr.sa.sa_family)
			{
			  case AF_UNSPEC:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "unspec");
				break;
#if NETUNIX
			  case AF_UNIX:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "local");
				break;
#endif /* NETUNIX */
#if NETINET
			  case AF_INET:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "inet");
				break;
#endif /* NETINET */
#if NETINET6
			  case AF_INET6:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "inet6");
				break;
#endif /* NETINET6 */
#if NETISO
			  case AF_ISO:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "iso");
				break;
#endif /* NETISO */
#if NETNS
			  case AF_NS:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "ns");
				break;
#endif /* NETNS */
#if NETX25
			  case AF_CCITT:
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_family}"), "x.25");
				break;
#endif /* NETX25 */
			}
			macdefine(&BlankEnvelope.e_macro, A_PERM,
				macid("{daemon_name}"),
				Daemons[curdaemon].d_name);
			if (Daemons[curdaemon].d_mflags != NULL)
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_flags}"),
					Daemons[curdaemon].d_mflags);
			else
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{daemon_flags}"), "");
		}

		/*
		**  If connection rate is exceeded here, connection shall be
		**  refused later by a new call after fork() by the
		**  validate_connection() function. Closing the connection
		**  at this point violates RFC 2821.
		**  Do NOT remove this call, its side effects are needed.
		*/

		connection_rate_check(&RealHostAddr, NULL);

		/*
		**  Create a subprocess to process the mail.
		*/

		if (tTd(15, 2))
			sm_dprintf("getrequests: forking (fd = %d)\n", t);

		/*
		**  Advance state of PRNG.
		**  This is necessary because otherwise all child processes
		**  will produce the same PRN sequence and hence the selection
		**  of a queue directory (and other things, e.g., MX selection)
		**  are not "really" random.
		*/
#if STARTTLS
		/* XXX get some better "random" data? */
		seed = get_random();
		RAND_seed((void *) &NextDiskSpaceCheck,
			  sizeof(NextDiskSpaceCheck));
		RAND_seed((void *) &now, sizeof(now));
		RAND_seed((void *) &seed, sizeof(seed));
#else /* STARTTLS */
		(void) get_random();
#endif /* STARTTLS */

#if NAMED_BIND
		/*
		**  Update MX records for FallbackMX.
		**  Let's hope this is fast otherwise we screw up the
		**  response time.
		*/

		if (FallbackMX != NULL)
			(void) getfallbackmxrr(FallbackMX);
#endif /* NAMED_BIND */

		if (tTd(93, 100))
		{
			/* don't fork, handle connection in this process */
			pid = 0;
			pipefd[0] = pipefd[1] = -1;
		}
		else
		{
			/*
			**  Create a pipe to keep the child from writing to
			**  the socket until after the parent has closed
			**  it.  Otherwise the parent may hang if the child
			**  has closed it first.
			*/

			if (pipe(pipefd) < 0)
				pipefd[0] = pipefd[1] = -1;

			(void) sm_blocksignal(SIGCHLD);
			pid = fork();
			if (pid < 0)
			{
				syserr("daemon: cannot fork");
				if (pipefd[0] != -1)
				{
					(void) close(pipefd[0]);
					(void) close(pipefd[1]);
				}
				(void) sm_releasesignal(SIGCHLD);
				(void) sleep(10);
				(void) close(t);
				continue;
			}
		}

		if (pid == 0)
		{
			char *p;
			SM_FILE_T *inchannel, *outchannel = NULL;

			/*
			**  CHILD -- return to caller.
			**	Collect verified idea of sending host.
			**	Verify calling user id if possible here.
			*/

			/* Reset global flags */
			RestartRequest = NULL;
			RestartWorkGroup = false;
			ShutdownRequest = NULL;
			PendingSignal = 0;
			CurrentPid = getpid();
			close_sendmail_pid();

			(void) sm_releasesignal(SIGALRM);
			(void) sm_releasesignal(SIGCHLD);
			(void) sm_signal(SIGCHLD, SIG_DFL);
			(void) sm_signal(SIGHUP, SIG_DFL);
			(void) sm_signal(SIGTERM, intsig);

			/* turn on profiling */
			/* SM_PROF(0); */

			/*
			**  Initialize exception stack and default exception
			**  handler for child process.
			*/

			sm_exc_newthread(fatal_error);

			if (!control)
			{
				macdefine(&BlankEnvelope.e_macro, A_TEMP,
					macid("{daemon_addr}"),
					anynet_ntoa(&Daemons[curdaemon].d_addr));
				(void) sm_snprintf(status, sizeof(status), "%d",
						ntohs(Daemons[curdaemon].d_port));
				macdefine(&BlankEnvelope.e_macro, A_TEMP,
					macid("{daemon_port}"), status);
			}

			for (idx = 0; idx < NDaemons; idx++)
			{
				if (Daemons[idx].d_socket >= 0)
					(void) close(Daemons[idx].d_socket);
				Daemons[idx].d_socket = -1;
			}
			clrcontrol();

			/* Avoid SMTP daemon actions if control command */
			if (control)
			{
				/* Add control socket process */
				proc_list_add(CurrentPid,
					      "console socket child",
					      PROC_CONTROL_CHILD, 0, -1, NULL);
			}
			else
			{
				proc_list_clear();

				/* clean up background delivery children */
				(void) sm_signal(SIGCHLD, reapchild);

				/* Add parent process as first child item */
				proc_list_add(CurrentPid, "daemon child",
					      PROC_DAEMON_CHILD, 0, -1, NULL);
				/* don't schedule queue runs if ETRN */
				QueueIntvl = 0;

				/*
				**  Hack: override global variables if
				**	the corresponding DaemonPortOption
				**	is set.
				*/
#if _FFR_SS_PER_DAEMON
				if (Daemons[curdaemon].d_supersafe !=
				    DPO_NOTSET)
					SuperSafe = Daemons[curdaemon].
								d_supersafe;
#endif /* _FFR_SS_PER_DAEMON */
				if (Daemons[curdaemon].d_dm != DM_NOTSET)
					set_delivery_mode(
						Daemons[curdaemon].d_dm, e);

				if (Daemons[curdaemon].d_refuseLA !=
				    DPO_NOTSET)
					RefuseLA = Daemons[curdaemon].
								d_refuseLA;
				if (Daemons[curdaemon].d_queueLA != DPO_NOTSET)
					QueueLA = Daemons[curdaemon].d_queueLA;
				if (Daemons[curdaemon].d_delayLA != DPO_NOTSET)
					DelayLA = Daemons[curdaemon].d_delayLA;
				if (Daemons[curdaemon].d_maxchildren !=
				    DPO_NOTSET)
					MaxChildren = Daemons[curdaemon].
								d_maxchildren;

				sm_setproctitle(true, e, "startup with %s",
						anynet_ntoa(&RealHostAddr));
			}

			if (pipefd[0] != -1)
			{
				auto char c;

				/*
				**  Wait for the parent to close the write end
				**  of the pipe, which we will see as an EOF.
				**  This guarantees that we won't write to the
				**  socket until after the parent has closed
				**  the pipe.
				*/

				/* close the write end of the pipe */
				(void) close(pipefd[1]);

				/* we shouldn't be interrupted, but ... */
				while (read(pipefd[0], &c, 1) < 0 &&
				       errno == EINTR)
					continue;
				(void) close(pipefd[0]);
			}

			/* control socket processing */
			if (control)
			{
				control_command(t, e);
				/* NOTREACHED */
				exit(EX_SOFTWARE);
			}

			/* determine host name */
			p = hostnamebyanyaddr(&RealHostAddr);
			if (strlen(p) > MAXNAME) /* XXX  - 1 ? */
				p[MAXNAME] = '\0';
			RealHostName = newstr(p);
			if (RealHostName[0] == '[')
			{
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{client_resolve}"),
					h_errno == TRY_AGAIN ? "TEMP" : "FAIL");
			}
			else
			{
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					  macid("{client_resolve}"), "OK");
			}
			sm_setproctitle(true, e, "startup with %s", p);
			markstats(e, NULL, STATS_CONNECT);

			if ((inchannel = sm_io_open(SmFtStdiofd,
						    SM_TIME_DEFAULT,
						    (void *) &t,
						    SM_IO_RDONLY_B,
						    NULL)) == NULL ||
			    (t = dup(t)) < 0 ||
			    (outchannel = sm_io_open(SmFtStdiofd,
						     SM_TIME_DEFAULT,
						     (void *) &t,
						     SM_IO_WRONLY_B,
						     NULL)) == NULL)
			{
				syserr("cannot open SMTP server channel, fd=%d",
					t);
				finis(false, true, EX_OK);
			}
			sm_io_automode(inchannel, outchannel);

			InChannel = inchannel;
			OutChannel = outchannel;
			DisConnected = false;

#if _FFR_XCNCT
			t = xconnect(inchannel);
			if (t <= 0)
			{
				clrbitn(D_XCNCT, Daemons[curdaemon].d_flags);
				clrbitn(D_XCNCT_M, Daemons[curdaemon].d_flags);
			}
			else
				setbitn(t, Daemons[curdaemon].d_flags);

#endif /* _FFR_XCNCT */

#if XLA
			if (!xla_host_ok(RealHostName))
			{
				message("421 4.4.5 Too many SMTP sessions for this host");
				finis(false, true, EX_OK);
			}
#endif /* XLA */
			/* find out name for interface of connection */
			if (getsockname(sm_io_getinfo(InChannel, SM_IO_WHAT_FD,
						      NULL), &sa.sa, &len) == 0)
			{
				p = hostnamebyanyaddr(&sa);
				if (tTd(15, 9))
					sm_dprintf("getreq: got name %s\n", p);
				macdefine(&BlankEnvelope.e_macro, A_TEMP,
					macid("{if_name}"), p);

				/*
				**  Do this only if it is not the loopback
				**  interface.
				*/

				if (!isloopback(sa))
				{
					char *addr;
					char family[5];

					addr = anynet_ntoa(&sa);
					(void) sm_snprintf(family,
						sizeof(family),
						"%d", sa.sa.sa_family);
					macdefine(&BlankEnvelope.e_macro,
						A_TEMP,
						macid("{if_addr}"), addr);
					macdefine(&BlankEnvelope.e_macro,
						A_TEMP,
						macid("{if_family}"), family);
					if (tTd(15, 7))
						sm_dprintf("getreq: got addr %s and family %s\n",
							addr, family);
				}
				else
				{
					macdefine(&BlankEnvelope.e_macro,
						A_PERM,
						macid("{if_addr}"), NULL);
					macdefine(&BlankEnvelope.e_macro,
						A_PERM,
						macid("{if_family}"), NULL);
				}
			}
			else
			{
				if (tTd(15, 7))
					sm_dprintf("getreq: getsockname failed\n");
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{if_name}"), NULL);
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{if_addr}"), NULL);
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					macid("{if_family}"), NULL);
			}
			break;
		}

		/* parent -- keep track of children */
		if (control)
		{
			(void) sm_snprintf(status, sizeof(status),
					   "control socket server child");
			proc_list_add(pid, status, PROC_CONTROL, 0, -1, NULL);
		}
		else
		{
			(void) sm_snprintf(status, sizeof(status),
					   "SMTP server child for %s",
					   anynet_ntoa(&RealHostAddr));
			proc_list_add(pid, status, PROC_DAEMON, 0, -1,
					&RealHostAddr);
		}
		(void) sm_releasesignal(SIGCHLD);

		/* close the read end of the synchronization pipe */
		if (pipefd[0] != -1)
		{
			(void) close(pipefd[0]);
			pipefd[0] = -1;
		}

		/* close the port so that others will hang (for a while) */
		(void) close(t);

		/* release the child by closing the read end of the sync pipe */
		if (pipefd[1] != -1)
		{
			(void) close(pipefd[1]);
			pipefd[1] = -1;
		}
	}
	if (tTd(15, 2))
		sm_dprintf("getreq: returning\n");

#if MILTER
	/* set the filters for this daemon */
	if (Daemons[curdaemon].d_inputfilterlist != NULL)
	{
		for (i = 0;
		     (i < MAXFILTERS &&
		      Daemons[curdaemon].d_inputfilters[i] != NULL);
		     i++)
		{
			InputFilters[i] = Daemons[curdaemon].d_inputfilters[i];
		}
		if (i < MAXFILTERS)
			InputFilters[i] = NULL;
	}
#endif /* MILTER */
	return &Daemons[curdaemon].d_flags;
}

/*
**  GETREQUESTS_CHECKDISKSPACE -- check available diskspace.
**
**	Parameters:
**		e -- envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Modifies Daemon flags (D_ETRNONLY) if not enough disk space.
*/

static void
getrequests_checkdiskspace(e)
	ENVELOPE *e;
{
	bool logged = false;
	int idx;
	time_t now;

	now = curtime();
	if (now < NextDiskSpaceCheck)
		return;

	/* Check if there is available disk space in all queue groups. */
	if (!enoughdiskspace(0, NULL))
	{
		for (idx = 0; idx < NDaemons; ++idx)
		{
			if (bitnset(D_ETRNONLY, Daemons[idx].d_flags))
				continue;

			/* log only if not logged before */
			if (!logged)
			{
				if (LogLevel > 8)
					sm_syslog(LOG_INFO, NOQID,
						  "rejecting new messages: min free: %ld",
						  MinBlocksFree);
				sm_setproctitle(true, e,
						"rejecting new messages: min free: %ld",
						MinBlocksFree);
				logged = true;
			}
			setbitn(D_ETRNONLY, Daemons[idx].d_flags);
		}
	}
	else
	{
		for (idx = 0; idx < NDaemons; ++idx)
		{
			if (!bitnset(D_ETRNONLY, Daemons[idx].d_flags))
				continue;

			/* log only if not logged before */
			if (!logged)
			{
				if (LogLevel > 8)
					sm_syslog(LOG_INFO, NOQID,
						  "accepting new messages (again)");
				logged = true;
			}

			/* title will be set later */
			clrbitn(D_ETRNONLY, Daemons[idx].d_flags);
		}
	}

	/* only check disk space once a minute */
	NextDiskSpaceCheck = now + 60;
}

/*
**  OPENDAEMONSOCKET -- open SMTP socket
**
**	Deals with setting all appropriate options.
**
**	Parameters:
**		d -- the structure for the daemon to open.
**		firsttime -- set if this is the initial open.
**
**	Returns:
**		Size in bytes of the daemon socket addr.
**
**	Side Effects:
**		Leaves DaemonSocket set to the open socket.
**		Exits if the socket cannot be created.
*/

#define MAXOPENTRIES	10	/* maximum number of tries to open connection */

static int
opendaemonsocket(d, firsttime)
	DAEMON_T *d;
	bool firsttime;
{
	int on = 1;
	int fdflags;
	SOCKADDR_LEN_T socksize = 0;
	int ntries = 0;
	int save_errno;

	if (tTd(15, 2))
		sm_dprintf("opendaemonsocket(%s)\n", d->d_name);

	do
	{
		if (ntries > 0)
			(void) sleep(5);
		if (firsttime || d->d_socket < 0)
		{
#if NETUNIX
			if (d->d_addr.sa.sa_family == AF_UNIX)
			{
				int rval;
				long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_ROOTOK|SFF_EXECOK|SFF_CREAT;

				/* if not safe, don't use it */
				rval = safefile(d->d_addr.sunix.sun_path,
						RunAsUid, RunAsGid,
						RunAsUserName, sff,
						S_IRUSR|S_IWUSR, NULL);
				if (rval != 0)
				{
					save_errno = errno;
					syserr("opendaemonsocket: daemon %s: unsafe domain socket %s",
					       d->d_name,
					       d->d_addr.sunix.sun_path);
					goto fail;
				}

				/* Don't try to overtake an existing socket */
				(void) unlink(d->d_addr.sunix.sun_path);
			}
#endif /* NETUNIX */
			d->d_socket = socket(d->d_addr.sa.sa_family,
					     SOCK_STREAM, 0);
			if (d->d_socket < 0)
			{
				save_errno = errno;
				syserr("opendaemonsocket: daemon %s: can't create server SMTP socket",
				       d->d_name);
			  fail:
				if (bitnset(D_OPTIONAL, d->d_flags) &&
				    (!transienterror(save_errno) ||
				     ntries >= MAXOPENTRIES - 1))
				{
					syserr("opendaemonsocket: daemon %s: optional socket disabled",
					       d->d_name);
					setbitn(D_DISABLE, d->d_flags);
					d->d_socket = -1;
					return -1;
				}
			  severe:
				if (LogLevel > 0)
					sm_syslog(LOG_ALERT, NOQID,
						  "daemon %s: problem creating SMTP socket",
						  d->d_name);
				d->d_socket = -1;
				continue;
			}

			if (!SM_FD_OK_SELECT(d->d_socket))
			{
				save_errno = EINVAL;
				syserr("opendaemonsocket: daemon %s: server SMTP socket (%d) too large",
				       d->d_name, d->d_socket);
				goto fail;
			}

			/* turn on network debugging? */
			if (tTd(15, 101))
				(void) setsockopt(d->d_socket, SOL_SOCKET,
						  SO_DEBUG, (char *)&on,
						  sizeof(on));

			(void) setsockopt(d->d_socket, SOL_SOCKET,
					  SO_REUSEADDR, (char *)&on, sizeof(on));
			(void) setsockopt(d->d_socket, SOL_SOCKET,
					  SO_KEEPALIVE, (char *)&on, sizeof(on));

#ifdef SO_RCVBUF
			if (d->d_tcprcvbufsize > 0)
			{
				if (setsockopt(d->d_socket, SOL_SOCKET,
					       SO_RCVBUF,
					       (char *) &d->d_tcprcvbufsize,
					       sizeof(d->d_tcprcvbufsize)) < 0)
					syserr("opendaemonsocket: daemon %s: setsockopt(SO_RCVBUF)", d->d_name);
			}
#endif /* SO_RCVBUF */
#ifdef SO_SNDBUF
			if (d->d_tcpsndbufsize > 0)
			{
				if (setsockopt(d->d_socket, SOL_SOCKET,
					       SO_SNDBUF,
					       (char *) &d->d_tcpsndbufsize,
					       sizeof(d->d_tcpsndbufsize)) < 0)
					syserr("opendaemonsocket: daemon %s: setsockopt(SO_SNDBUF)", d->d_name);
			}
#endif /* SO_SNDBUF */

			if ((fdflags = fcntl(d->d_socket, F_GETFD, 0)) == -1 ||
			    fcntl(d->d_socket, F_SETFD,
				  fdflags | FD_CLOEXEC) == -1)
			{
				save_errno = errno;
				syserr("opendaemonsocket: daemon %s: failed to %s close-on-exec flag: %s",
				       d->d_name,
				       fdflags == -1 ? "get" : "set",
				       sm_errstring(save_errno));
				(void) close(d->d_socket);
				goto severe;
			}

			switch (d->d_addr.sa.sa_family)
			{
#ifdef NETUNIX
			  case AF_UNIX:
				socksize = sizeof(d->d_addr.sunix);
				break;
#endif /* NETUNIX */
#if NETINET
			  case AF_INET:
				socksize = sizeof(d->d_addr.sin);
				break;
#endif /* NETINET */

#if NETINET6
			  case AF_INET6:
				socksize = sizeof(d->d_addr.sin6);
				break;
#endif /* NETINET6 */

#if NETISO
			  case AF_ISO:
				socksize = sizeof(d->d_addr.siso);
				break;
#endif /* NETISO */

			  default:
				socksize = sizeof(d->d_addr);
				break;
			}

			if (bind(d->d_socket, &d->d_addr.sa, socksize) < 0)
			{
				/* probably another daemon already */
				save_errno = errno;
				syserr("opendaemonsocket: daemon %s: cannot bind",
				       d->d_name);
				(void) close(d->d_socket);
				goto fail;
			}
		}
		if (!firsttime &&
		    listen(d->d_socket, d->d_listenqueue) < 0)
		{
			save_errno = errno;
			syserr("opendaemonsocket: daemon %s: cannot listen",
			       d->d_name);
			(void) close(d->d_socket);
			goto severe;
		}
		return socksize;
	} while (ntries++ < MAXOPENTRIES && transienterror(save_errno));
	syserr("!opendaemonsocket: daemon %s: server SMTP socket wedged: exiting",
	       d->d_name);
	/* NOTREACHED */
	return -1;  /* avoid compiler warning on IRIX */
}
/*
**  SETUPDAEMON -- setup socket for daemon
**
**	Parameters:
**		daemonaddr -- socket for daemon
**
**	Returns:
**		port number on which daemon should run
**
*/

static unsigned short
setupdaemon(daemonaddr)
	SOCKADDR *daemonaddr;
{
	unsigned short port;

	/*
	**  Set up the address for the mailer.
	*/

	if (daemonaddr->sa.sa_family == AF_UNSPEC)
	{
		memset(daemonaddr, '\0', sizeof(*daemonaddr));
#if NETINET
		daemonaddr->sa.sa_family = AF_INET;
#endif /* NETINET */
	}

	switch (daemonaddr->sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		if (daemonaddr->sin.sin_addr.s_addr == 0)
			daemonaddr->sin.sin_addr.s_addr =
			    LocalDaemon ? htonl(INADDR_LOOPBACK) : INADDR_ANY;
		port = daemonaddr->sin.sin_port;
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&daemonaddr->sin6.sin6_addr))
			daemonaddr->sin6.sin6_addr =
			    (LocalDaemon && V6LoopbackAddrFound) ?
			    in6addr_loopback : in6addr_any;
		port = daemonaddr->sin6.sin6_port;
		break;
#endif /* NETINET6 */

	  default:
		/* unknown protocol */
		port = 0;
		break;
	}
	if (port == 0)
	{
#ifdef NO_GETSERVBYNAME
		port = htons(25);
#else /* NO_GETSERVBYNAME */
		{
			register struct servent *sp;

			sp = getservbyname("smtp", "tcp");
			if (sp == NULL)
			{
				syserr("554 5.3.5 service \"smtp\" unknown");
				port = htons(25);
			}
			else
				port = sp->s_port;
		}
#endif /* NO_GETSERVBYNAME */
	}

	switch (daemonaddr->sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		daemonaddr->sin.sin_port = port;
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		daemonaddr->sin6.sin6_port = port;
		break;
#endif /* NETINET6 */

	  default:
		/* unknown protocol */
		break;
	}
	return port;
}
/*
**  CLRDAEMON -- reset the daemon connection
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		releases any resources used by the passive daemon.
*/

void
clrdaemon()
{
	int i;

	for (i = 0; i < NDaemons; i++)
	{
		if (Daemons[i].d_socket >= 0)
			(void) close(Daemons[i].d_socket);
		Daemons[i].d_socket = -1;
	}
}

/*
**  GETMODIFIERS -- get modifier flags
**
**	Parameters:
**		v -- the modifiers (input text line).
**		modifiers -- pointer to flag field to represent modifiers.
**
**	Returns:
**		(xallocat()ed) string representation of modifiers.
**
**	Side Effects:
**		fills in modifiers.
*/

char *
getmodifiers(v, modifiers)
	char *v;
	BITMAP256 modifiers;
{
	int l;
	char *h, *f, *flags;

	/* maximum length of flags: upper case Option -> "OO " */
	l = 3 * strlen(v) + 3;

	/* is someone joking? */
	if (l < 0 || l > 256)
	{
		if (LogLevel > 2)
			sm_syslog(LOG_ERR, NOQID,
				  "getmodifiers too long, ignored");
		return NULL;
	}
	flags = xalloc(l);
	f = flags;
	clrbitmap(modifiers);
	for (h = v; *h != '\0'; h++)
	{
		if (isascii(*h) && !isspace(*h) && isprint(*h))
		{
			setbitn(*h, modifiers);
			if (flags != f)
				*flags++ = ' ';
			*flags++ = *h;
			if (isupper(*h))
				*flags++ = *h;
		}
	}
	*flags++ = '\0';
	return f;
}

/*
**  CHKDAEMONMODIFIERS -- check whether all daemons have set a flag.
**
**	Parameters:
**		flag -- the flag to test.
**
**	Returns:
**		true iff all daemons have set flag.
*/

bool
chkdaemonmodifiers(flag)
	int flag;
{
	int i;

	for (i = 0; i < NDaemons; i++)
		if (!bitnset((char) flag, Daemons[i].d_flags))
			return false;
	return true;
}

/*
**  SETSOCKADDROPTIONS -- set options for SOCKADDR (daemon or client)
**
**	Parameters:
**		p -- the options line.
**		d -- the daemon structure to fill in.
**
**	Returns:
**		none.
*/

static void
setsockaddroptions(p, d)
	char *p;
	DAEMON_T *d;
{
#if NETISO
	short portno;
#endif /* NETISO */
	char *port = NULL;
	char *addr = NULL;

#if NETINET
	if (d->d_addr.sa.sa_family == AF_UNSPEC)
		d->d_addr.sa.sa_family = AF_INET;
#endif /* NETINET */
#if _FFR_SS_PER_DAEMON
	d->d_supersafe = DPO_NOTSET;
#endif /* _FFR_SS_PER_DAEMON */
	d->d_dm = DM_NOTSET;
	d->d_refuseLA = DPO_NOTSET;
	d->d_queueLA = DPO_NOTSET;
	d->d_delayLA = DPO_NOTSET;
	d->d_maxchildren = DPO_NOTSET;

	while (p != NULL)
	{
		register char *f;
		register char *v;

		while (isascii(*p) && isspace(*p))
			p++;
		if (*p == '\0')
			break;
		f = p;
		p = strchr(p, ',');
		if (p != NULL)
			*p++ = '\0';
		v = strchr(f, '=');
		if (v == NULL)
			continue;
		while (isascii(*++v) && isspace(*v))
			continue;

		switch (*f)
		{
		  case 'A':		/* address */
#if !_FFR_DPO_CS
		  case 'a':
#endif /* !_FFR_DPO_CS */
			addr = v;
			break;

		  case 'c':
			d->d_maxchildren = atoi(v);
			break;

		  case 'D':		/* DeliveryMode */
			switch (*v)
			{
			  case SM_QUEUE:
			  case SM_DEFER:
			  case SM_DELIVER:
			  case SM_FORK:
#if _FFR_PROXY
			  case SM_PROXY_REQ:
#endif /* _FFR_PROXY */
				d->d_dm = *v;
				break;
			  default:
				syserr("554 5.3.5 Unknown delivery mode %c",
					*v);
				break;
			}
			break;

		  case 'd':		/* delayLA */
			d->d_delayLA = atoi(v);
			break;

		  case 'F':		/* address family */
#if !_FFR_DPO_CS
		  case 'f':
#endif /* !_FFR_DPO_CS */
			if (isascii(*v) && isdigit(*v))
				d->d_addr.sa.sa_family = atoi(v);
#ifdef NETUNIX
			else if (sm_strcasecmp(v, "unix") == 0 ||
				 sm_strcasecmp(v, "local") == 0)
				d->d_addr.sa.sa_family = AF_UNIX;
#endif /* NETUNIX */
#if NETINET
			else if (sm_strcasecmp(v, "inet") == 0)
				d->d_addr.sa.sa_family = AF_INET;
#endif /* NETINET */
#if NETINET6
			else if (sm_strcasecmp(v, "inet6") == 0)
				d->d_addr.sa.sa_family = AF_INET6;
#endif /* NETINET6 */
#if NETISO
			else if (sm_strcasecmp(v, "iso") == 0)
				d->d_addr.sa.sa_family = AF_ISO;
#endif /* NETISO */
#if NETNS
			else if (sm_strcasecmp(v, "ns") == 0)
				d->d_addr.sa.sa_family = AF_NS;
#endif /* NETNS */
#if NETX25
			else if (sm_strcasecmp(v, "x.25") == 0)
				d->d_addr.sa.sa_family = AF_CCITT;
#endif /* NETX25 */
			else
				syserr("554 5.3.5 Unknown address family %s in Family=option",
				       v);
			break;

#if MILTER
		  case 'I':
# if !_FFR_DPO_CS
		  case 'i':
# endif /* !_FFR_DPO_CS */
			d->d_inputfilterlist = v;
			break;
#endif /* MILTER */

		  case 'L':		/* listen queue size */
#if !_FFR_DPO_CS
		  case 'l':
#endif /* !_FFR_DPO_CS */
			d->d_listenqueue = atoi(v);
			break;

		  case 'M':		/* modifiers (flags) */
#if !_FFR_DPO_CS
		  case 'm':
#endif /* !_FFR_DPO_CS */
			d->d_mflags = getmodifiers(v, d->d_flags);
			break;

		  case 'N':		/* name */
#if !_FFR_DPO_CS
		  case 'n':
#endif /* !_FFR_DPO_CS */
			d->d_name = v;
			break;

		  case 'P':		/* port */
#if !_FFR_DPO_CS
		  case 'p':
#endif /* !_FFR_DPO_CS */
			port = v;
			break;

		  case 'q':
			d->d_queueLA = atoi(v);
			break;

		  case 'R':		/* receive buffer size */
			d->d_tcprcvbufsize = atoi(v);
			break;

		  case 'r':
			d->d_refuseLA = atoi(v);
			break;

		  case 'S':		/* send buffer size */
#if !_FFR_DPO_CS
		  case 's':
#endif /* !_FFR_DPO_CS */
			d->d_tcpsndbufsize = atoi(v);
			break;

#if _FFR_SS_PER_DAEMON
		  case 'T':		/* SuperSafe */
			if (tolower(*v) == 'i')
				d->d_supersafe = SAFE_INTERACTIVE;
			else if (tolower(*v) == 'p')
# if MILTER
				d->d_supersafe = SAFE_REALLY_POSTMILTER;
# else /* MILTER */
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					"Warning: SuperSafe=PostMilter requires Milter support (-DMILTER)\n");
# endif /* MILTER */
			else
				d->d_supersafe = atobool(v) ? SAFE_REALLY
							: SAFE_NO;
			break;
#endif /* _FFR_SS_PER_DAEMON */

		  default:
			syserr("554 5.3.5 PortOptions parameter \"%s\" unknown",
			       f);
		}
	}

	/* Check addr and port after finding family */
	if (addr != NULL)
	{
		switch (d->d_addr.sa.sa_family)
		{
#if NETUNIX
		  case AF_UNIX:
			if (strlen(addr) >= sizeof(d->d_addr.sunix.sun_path))
			{
				errno = ENAMETOOLONG;
				syserr("setsockaddroptions: domain socket name too long: %s > %ld",
				       addr,
				       (long) sizeof(d->d_addr.sunix.sun_path));
				break;
			}

			/* file safety check done in opendaemonsocket() */
			(void) memset(&d->d_addr.sunix.sun_path, '\0',
				      sizeof(d->d_addr.sunix.sun_path));
			(void) sm_strlcpy((char *)&d->d_addr.sunix.sun_path,
					  addr,
					  sizeof(d->d_addr.sunix.sun_path));
			break;
#endif /* NETUNIX */
#if NETINET
		  case AF_INET:
			if (!isascii(*addr) || !isdigit(*addr) ||
			    ((d->d_addr.sin.sin_addr.s_addr = inet_addr(addr))
			     == INADDR_NONE))
			{
				register struct hostent *hp;

				hp = sm_gethostbyname(addr, AF_INET);
				if (hp == NULL)
					syserr("554 5.3.0 host \"%s\" unknown",
					       addr);
				else
				{
					while (*(hp->h_addr_list) != NULL &&
					       hp->h_addrtype != AF_INET)
						hp->h_addr_list++;
					if (*(hp->h_addr_list) == NULL)
						syserr("554 5.3.0 host \"%s\" unknown",
						       addr);
					else
						memmove(&d->d_addr.sin.sin_addr,
							*(hp->h_addr_list),
							INADDRSZ);
# if NETINET6
					freehostent(hp);
					hp = NULL;
# endif /* NETINET6 */
				}
			}
			break;
#endif /* NETINET */

#if NETINET6
		  case AF_INET6:
			if (anynet_pton(AF_INET6, addr,
					&d->d_addr.sin6.sin6_addr) != 1)
			{
				register struct hostent *hp;

				hp = sm_gethostbyname(addr, AF_INET6);
				if (hp == NULL)
					syserr("554 5.3.0 host \"%s\" unknown",
					       addr);
				else
				{
					while (*(hp->h_addr_list) != NULL &&
					       hp->h_addrtype != AF_INET6)
						hp->h_addr_list++;
					if (*(hp->h_addr_list) == NULL)
						syserr("554 5.3.0 host \"%s\" unknown",
						       addr);
					else
						memmove(&d->d_addr.sin6.sin6_addr,
							*(hp->h_addr_list),
							IN6ADDRSZ);
					freehostent(hp);
					hp = NULL;
				}
			}
			break;
#endif /* NETINET6 */

		  default:
			syserr("554 5.3.5 address= option unsupported for family %d",
			       d->d_addr.sa.sa_family);
			break;
		}
	}

	if (port != NULL)
	{
		switch (d->d_addr.sa.sa_family)
		{
#if NETINET
		  case AF_INET:
			if (isascii(*port) && isdigit(*port))
				d->d_addr.sin.sin_port = htons((unsigned short)
						     atoi((const char *) port));
			else
			{
# ifdef NO_GETSERVBYNAME
				syserr("554 5.3.5 invalid port number: %s",
				       port);
# else /* NO_GETSERVBYNAME */
				register struct servent *sp;

				sp = getservbyname(port, "tcp");
				if (sp == NULL)
					syserr("554 5.3.5 service \"%s\" unknown",
					       port);
				else
					d->d_addr.sin.sin_port = sp->s_port;
# endif /* NO_GETSERVBYNAME */
			}
			break;
#endif /* NETINET */

#if NETINET6
		  case AF_INET6:
			if (isascii(*port) && isdigit(*port))
				d->d_addr.sin6.sin6_port = htons((unsigned short)
								  atoi(port));
			else
			{
# ifdef NO_GETSERVBYNAME
				syserr("554 5.3.5 invalid port number: %s",
				       port);
# else /* NO_GETSERVBYNAME */
				register struct servent *sp;

				sp = getservbyname(port, "tcp");
				if (sp == NULL)
					syserr("554 5.3.5 service \"%s\" unknown",
					       port);
				else
					d->d_addr.sin6.sin6_port = sp->s_port;
# endif /* NO_GETSERVBYNAME */
			}
			break;
#endif /* NETINET6 */

#if NETISO
		  case AF_ISO:
			/* assume two byte transport selector */
			if (isascii(*port) && isdigit(*port))
				portno = htons((unsigned short) atoi(port));
			else
			{
# ifdef NO_GETSERVBYNAME
				syserr("554 5.3.5 invalid port number: %s",
				       port);
# else /* NO_GETSERVBYNAME */
				register struct servent *sp;

				sp = getservbyname(port, "tcp");
				if (sp == NULL)
					syserr("554 5.3.5 service \"%s\" unknown",
					       port);
				else
					portno = sp->s_port;
# endif /* NO_GETSERVBYNAME */
			}
			memmove(TSEL(&d->d_addr.siso),
				(char *) &portno, 2);
			break;
#endif /* NETISO */

		  default:
			syserr("554 5.3.5 Port= option unsupported for family %d",
			       d->d_addr.sa.sa_family);
			break;
		}
	}
}
/*
**  SETDAEMONOPTIONS -- set options for running the MTA daemon
**
**	Parameters:
**		p -- the options line.
**
**	Returns:
**		true if successful, false otherwise.
**
**	Side Effects:
**		increments number of daemons.
*/

#define DEF_LISTENQUEUE	10

struct dflags
{
	char	*d_name;
	int	d_flag;
};

static struct dflags	DaemonFlags[] =
{
	{ "AUTHREQ",		D_AUTHREQ	},
	{ "BINDIF",		D_BINDIF	},
	{ "CANONREQ",		D_CANONREQ	},
	{ "IFNHELO",		D_IFNHELO	},
	{ "FQMAIL",		D_FQMAIL	},
	{ "FQRCPT",		D_FQRCPT	},
	{ "SMTPS",		D_SMTPS		},
	{ "UNQUALOK",		D_UNQUALOK	},
	{ "NOAUTH",		D_NOAUTH	},
	{ "NOCANON",		D_NOCANON	},
	{ "NOETRN",		D_NOETRN	},
	{ "NOTLS",		D_NOTLS		},
	{ "ETRNONLY",		D_ETRNONLY	},
	{ "OPTIONAL",		D_OPTIONAL	},
	{ "DISABLE",		D_DISABLE	},
	{ "ISSET",		D_ISSET		},
	{ NULL,			0		}
};

static void
printdaemonflags(d)
	DAEMON_T *d;
{
	register struct dflags *df;
	bool first = true;

	for (df = DaemonFlags; df->d_name != NULL; df++)
	{
		if (!bitnset(df->d_flag, d->d_flags))
			continue;
		if (first)
			sm_dprintf("<%s", df->d_name);
		else
			sm_dprintf(",%s", df->d_name);
		first = false;
	}
	if (!first)
		sm_dprintf(">");
}

bool
setdaemonoptions(p)
	register char *p;
{
	if (NDaemons >= MAXDAEMONS)
		return false;
	Daemons[NDaemons].d_socket = -1;
	Daemons[NDaemons].d_listenqueue = DEF_LISTENQUEUE;
	clrbitmap(Daemons[NDaemons].d_flags);
	setsockaddroptions(p, &Daemons[NDaemons]);

#if MILTER
	if (Daemons[NDaemons].d_inputfilterlist != NULL)
		Daemons[NDaemons].d_inputfilterlist = newstr(Daemons[NDaemons].d_inputfilterlist);
#endif /* MILTER */

	if (Daemons[NDaemons].d_name != NULL)
		Daemons[NDaemons].d_name = newstr(Daemons[NDaemons].d_name);
	else
	{
		char num[30];

		(void) sm_snprintf(num, sizeof(num), "Daemon%d", NDaemons);
		Daemons[NDaemons].d_name = newstr(num);
	}

	if (tTd(37, 1))
	{
		sm_dprintf("Daemon %s flags: ", Daemons[NDaemons].d_name);
		printdaemonflags(&Daemons[NDaemons]);
		sm_dprintf("\n");
	}
	++NDaemons;
	return true;
}
/*
**  INITDAEMON -- initialize daemon if not yet done.
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		initializes structure for one daemon.
*/

void
initdaemon()
{
	if (NDaemons == 0)
	{
		Daemons[NDaemons].d_socket = -1;
		Daemons[NDaemons].d_listenqueue = DEF_LISTENQUEUE;
		Daemons[NDaemons].d_name = "Daemon0";
		NDaemons = 1;
	}
}
/*
**  SETCLIENTOPTIONS -- set options for running the client
**
**	Parameters:
**		p -- the options line.
**
**	Returns:
**		none.
*/

static DAEMON_T	ClientSettings[AF_MAX + 1];

void
setclientoptions(p)
	register char *p;
{
	int family;
	DAEMON_T d;

	memset(&d, '\0', sizeof(d));
	setsockaddroptions(p, &d);

	/* grab what we need */
	family = d.d_addr.sa.sa_family;
	STRUCTCOPY(d, ClientSettings[family]);
	setbitn(D_ISSET, ClientSettings[family].d_flags); /* mark as set */
	if (d.d_name != NULL)
		ClientSettings[family].d_name = newstr(d.d_name);
	else
	{
		char num[30];

		(void) sm_snprintf(num, sizeof(num), "Client%d", family);
		ClientSettings[family].d_name = newstr(num);
	}
}
/*
**  ADDR_FAMILY -- determine address family from address
**
**	Parameters:
**		addr -- the string representation of the address
**
**	Returns:
**		AF_INET, AF_INET6 or AF_UNSPEC
**
**	Side Effects:
**		none.
*/

static int
addr_family(addr)
	char *addr;
{
#if NETINET6
	SOCKADDR clt_addr;
#endif /* NETINET6 */

#if NETINET
	if (inet_addr(addr) != INADDR_NONE)
	{
		if (tTd(16, 9))
			sm_dprintf("addr_family(%s): INET\n", addr);
		return AF_INET;
	}
#endif /* NETINET */
#if NETINET6
	if (anynet_pton(AF_INET6, addr, &clt_addr.sin6.sin6_addr) == 1)
	{
		if (tTd(16, 9))
			sm_dprintf("addr_family(%s): INET6\n", addr);
		return AF_INET6;
	}
#endif /* NETINET6 */
#if NETUNIX
	if (*addr == '/')
	{
		if (tTd(16, 9))
			sm_dprintf("addr_family(%s): LOCAL\n", addr);
		return AF_UNIX;
	}
#endif /* NETUNIX */
	if (tTd(16, 9))
		sm_dprintf("addr_family(%s): UNSPEC\n", addr);
	return AF_UNSPEC;
}

/*
**  CHKCLIENTMODIFIERS -- check whether all clients have set a flag.
**
**	Parameters:
**		flag -- the flag to test.
**
**	Returns:
**		true iff all configured clients have set the flag.
*/

bool
chkclientmodifiers(flag)
	int flag;
{
	int i;
	bool flagisset;

	flagisset = false;
	for (i = 0; i < AF_MAX; i++)
	{
		if (bitnset(D_ISSET, ClientSettings[i].d_flags))
		{
			if (!bitnset((char) flag, ClientSettings[i].d_flags))
				return false;
			flagisset = true;
		}
	}
	return flagisset;
}

#if MILTER
/*
**  SETUP_DAEMON_MILTERS -- Parse per-socket filters
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/

void
setup_daemon_milters()
{
	int idx;

	if (OpMode == MD_SMTP)
	{
		/* no need to configure the daemons */
		return;
	}

	for (idx = 0; idx < NDaemons; idx++)
	{
		if (Daemons[idx].d_inputfilterlist != NULL)
		{
			milter_config(Daemons[idx].d_inputfilterlist,
				      Daemons[idx].d_inputfilters,
				      MAXFILTERS);
		}
	}
}
#endif /* MILTER */
/*
**  MAKECONNECTION -- make a connection to an SMTP socket on a machine.
**
**	Parameters:
**		host -- the name of the host.
**		port -- the port number to connect to.
**		mci -- a pointer to the mail connection information
**			structure to be filled in.
**		e -- the current envelope.
**		enough -- time at which to stop further connection attempts.
**			(0 means no limit)
**
**	Returns:
**		An exit code telling whether the connection could be
**			made and if not why not.
**
**	Side Effects:
**		none.
*/

static jmp_buf	CtxConnectTimeout;

SOCKADDR	CurHostAddr;		/* address of current host */

int
makeconnection(host, port, mci, e, enough)
	char *host;
	volatile unsigned int port;
	register MCI *mci;
	ENVELOPE *e;
	time_t enough;
{
	register volatile int addrno = 0;
	volatile int s;
	register struct hostent *volatile hp = (struct hostent *) NULL;
	SOCKADDR addr;
	SOCKADDR clt_addr;
	int save_errno = 0;
	volatile SOCKADDR_LEN_T addrlen;
	volatile bool firstconnect = true;
	SM_EVENT *volatile ev = NULL;
#if NETINET6
	volatile bool v6found = false;
#endif /* NETINET6 */
	volatile int family = InetMode;
	SOCKADDR_LEN_T len;
	volatile SOCKADDR_LEN_T socksize = 0;
	volatile bool clt_bind;
	BITMAP256 d_flags;
	char *p;
	extern ENVELOPE BlankEnvelope;

	/* retranslate {daemon_flags} into bitmap */
	clrbitmap(d_flags);
	if ((p = macvalue(macid("{daemon_flags}"), e)) != NULL)
	{
		for (; *p != '\0'; p++)
		{
			if (!(isascii(*p) && isspace(*p)))
				setbitn(bitidx(*p), d_flags);
		}
	}

#if NETINET6
 v4retry:
#endif /* NETINET6 */
	clt_bind = false;

	/* Set up the address for outgoing connection. */
	if (bitnset(D_BINDIF, d_flags) &&
	    (p = macvalue(macid("{if_addr}"), e)) != NULL &&
	    *p != '\0')
	{
#if NETINET6
		char p6[INET6_ADDRSTRLEN];
#endif /* NETINET6 */

		memset(&clt_addr, '\0', sizeof(clt_addr));

		/* infer the address family from the address itself */
		clt_addr.sa.sa_family = addr_family(p);
		switch (clt_addr.sa.sa_family)
		{
#if NETINET
		  case AF_INET:
			clt_addr.sin.sin_addr.s_addr = inet_addr(p);
			if (clt_addr.sin.sin_addr.s_addr != INADDR_NONE &&
			    clt_addr.sin.sin_addr.s_addr !=
				htonl(INADDR_LOOPBACK))
			{
				clt_bind = true;
				socksize = sizeof(struct sockaddr_in);
			}
			break;
#endif /* NETINET */

#if NETINET6
		  case AF_INET6:
			if (inet_addr(p) != INADDR_NONE)
				(void) sm_snprintf(p6, sizeof(p6),
						   "IPv6:::ffff:%s", p);
			else
				(void) sm_strlcpy(p6, p, sizeof(p6));
			if (anynet_pton(AF_INET6, p6,
					&clt_addr.sin6.sin6_addr) == 1 &&
			    !IN6_IS_ADDR_LOOPBACK(&clt_addr.sin6.sin6_addr))
			{
				clt_bind = true;
				socksize = sizeof(struct sockaddr_in6);
			}
			break;
#endif /* NETINET6 */

#if 0
		  default:
			syserr("554 5.3.5 Address= option unsupported for family %d",
			       clt_addr.sa.sa_family);
			break;
#endif /* 0 */
		}
		if (clt_bind)
			family = clt_addr.sa.sa_family;
	}

	/* D_BINDIF not set or not available, fallback to ClientPortOptions */
	if (!clt_bind)
	{
		STRUCTCOPY(ClientSettings[family].d_addr, clt_addr);
		switch (clt_addr.sa.sa_family)
		{
#if NETINET
		  case AF_INET:
			if (clt_addr.sin.sin_addr.s_addr == 0)
				clt_addr.sin.sin_addr.s_addr = LocalDaemon ?
					htonl(INADDR_LOOPBACK) : INADDR_ANY;
			else
				clt_bind = true;
			if (clt_addr.sin.sin_port != 0)
				clt_bind = true;
			socksize = sizeof(struct sockaddr_in);
			break;
#endif /* NETINET */
#if NETINET6
		  case AF_INET6:
			if (IN6_IS_ADDR_UNSPECIFIED(&clt_addr.sin6.sin6_addr))
				clt_addr.sin6.sin6_addr =
					(LocalDaemon && V6LoopbackAddrFound) ?
					in6addr_loopback : in6addr_any;
			else
				clt_bind = true;
			socksize = sizeof(struct sockaddr_in6);
			if (clt_addr.sin6.sin6_port != 0)
				clt_bind = true;
			break;
#endif /* NETINET6 */
#if NETISO
		  case AF_ISO:
			socksize = sizeof(clt_addr.siso);
			clt_bind = true;
			break;
#endif /* NETISO */
		  default:
			break;
		}
	}

	/*
	**  Set up the address for the mailer.
	**	Accept "[a.b.c.d]" syntax for host name.
	*/

	SM_SET_H_ERRNO(0);
	errno = 0;
	memset(&CurHostAddr, '\0', sizeof(CurHostAddr));
	memset(&addr, '\0', sizeof(addr));
	SmtpPhase = mci->mci_phase = "initial connection";
	CurHostName = host;

	if (host[0] == '[')
	{
		p = strchr(host, ']');
		if (p != NULL)
		{
#if NETINET
			unsigned long hid = INADDR_NONE;
#endif /* NETINET */
#if NETINET6
			struct sockaddr_in6 hid6;
#endif /* NETINET6 */

			*p = '\0';
#if NETINET6
			memset(&hid6, '\0', sizeof(hid6));
#endif /* NETINET6 */
#if NETINET
			if (family == AF_INET &&
			    (hid = inet_addr(&host[1])) != INADDR_NONE)
			{
				addr.sin.sin_family = AF_INET;
				addr.sin.sin_addr.s_addr = hid;
			}
			else
#endif /* NETINET */
#if NETINET6
			if (family == AF_INET6 &&
			    anynet_pton(AF_INET6, &host[1],
					&hid6.sin6_addr) == 1)
			{
				addr.sin6.sin6_family = AF_INET6;
				addr.sin6.sin6_addr = hid6.sin6_addr;
			}
			else
#endif /* NETINET6 */
			{
				/* try it as a host name (avoid MX lookup) */
				hp = sm_gethostbyname(&host[1], family);
				if (hp == NULL && p[-1] == '.')
				{
#if NAMED_BIND
					int oldopts = _res.options;

					_res.options &= ~(RES_DEFNAMES|RES_DNSRCH);
#endif /* NAMED_BIND */
					p[-1] = '\0';
					hp = sm_gethostbyname(&host[1],
							      family);
					p[-1] = '.';
#if NAMED_BIND
					_res.options = oldopts;
#endif /* NAMED_BIND */
				}
				*p = ']';
				goto gothostent;
			}
			*p = ']';
		}
		if (p == NULL)
		{
			extern char MsgBuf[];

			usrerrenh("5.1.2",
				  "553 Invalid numeric domain spec \"%s\"",
				  host);
			mci_setstat(mci, EX_NOHOST, "5.1.2", MsgBuf);
			errno = EINVAL;
			return EX_NOHOST;
		}
	}
	else
	{
		/* contortion to get around SGI cc complaints */
		{
			p = &host[strlen(host) - 1];
			hp = sm_gethostbyname(host, family);
			if (hp == NULL && *p == '.')
			{
#if NAMED_BIND
				int oldopts = _res.options;

				_res.options &= ~(RES_DEFNAMES|RES_DNSRCH);
#endif /* NAMED_BIND */
				*p = '\0';
				hp = sm_gethostbyname(host, family);
				*p = '.';
#if NAMED_BIND
				_res.options = oldopts;
#endif /* NAMED_BIND */
			}
		}
gothostent:
		if (hp == NULL || hp->h_addr == NULL)
		{
#if NAMED_BIND
			/* check for name server timeouts */
# if NETINET6
			if (WorkAroundBrokenAAAA && family == AF_INET6 &&
			    (h_errno == TRY_AGAIN || errno == ETIMEDOUT))
			{
				/*
				**  An attempt with family AF_INET may
				**  succeed. By skipping the next section
				**  of code, we will try AF_INET before
				**  failing.
				*/

				if (tTd(16, 10))
					sm_dprintf("makeconnection: WorkAroundBrokenAAAA: Trying AF_INET lookup (AF_INET6 failed)\n");
			}
			else
# endif /* NETINET6 */
			{
				if (errno == ETIMEDOUT ||
# if _FFR_GETHBN_ExFILE
#  ifdef EMFILE
				   errno == EMFILE ||
#  endif /* EMFILE */
#  ifdef ENFILE
				   errno == ENFILE ||
#  endif /* ENFILE */
# endif /* _FFR_GETHBN_ExFILE */
				    h_errno == TRY_AGAIN ||
				    (errno == ECONNREFUSED && UseNameServer))
				{
					save_errno = errno;
					mci_setstat(mci, EX_TEMPFAIL,
						    "4.4.3", NULL);
					errno = save_errno;
					return EX_TEMPFAIL;
				}
			}
#endif /* NAMED_BIND */
#if NETINET6
			/*
			**  Try v6 first, then fall back to v4.
			**  If we found a v6 address, but no v4
			**  addresses, then TEMPFAIL.
			*/

			if (family == AF_INET6)
			{
				family = AF_INET;
				goto v4retry;
			}
			if (v6found)
				goto v6tempfail;
#endif /* NETINET6 */
			save_errno = errno;
			mci_setstat(mci, EX_NOHOST, "5.1.2", NULL);
			errno = save_errno;
			return EX_NOHOST;
		}
		addr.sa.sa_family = hp->h_addrtype;
		switch (hp->h_addrtype)
		{
#if NETINET
		  case AF_INET:
			memmove(&addr.sin.sin_addr,
				hp->h_addr,
				INADDRSZ);
			break;
#endif /* NETINET */

#if NETINET6
		  case AF_INET6:
			memmove(&addr.sin6.sin6_addr,
				hp->h_addr,
				IN6ADDRSZ);
			break;
#endif /* NETINET6 */

		  default:
			if (hp->h_length > sizeof(addr.sa.sa_data))
			{
				syserr("makeconnection: long sa_data: family %d len %d",
					hp->h_addrtype, hp->h_length);
				mci_setstat(mci, EX_NOHOST, "5.1.2", NULL);
				errno = EINVAL;
				return EX_NOHOST;
			}
			memmove(addr.sa.sa_data, hp->h_addr, hp->h_length);
			break;
		}
		addrno = 1;
	}

	/*
	**  Determine the port number.
	*/

	if (port == 0)
	{
#ifdef NO_GETSERVBYNAME
		port = htons(25);
#else /* NO_GETSERVBYNAME */
		register struct servent *sp = getservbyname("smtp", "tcp");

		if (sp == NULL)
		{
			if (LogLevel > 2)
				sm_syslog(LOG_ERR, NOQID,
					  "makeconnection: service \"smtp\" unknown");
			port = htons(25);
		}
		else
			port = sp->s_port;
#endif /* NO_GETSERVBYNAME */
	}

#if NETINET6
	if (addr.sa.sa_family == AF_INET6 &&
	    IN6_IS_ADDR_V4MAPPED(&addr.sin6.sin6_addr) &&
	    ClientSettings[AF_INET].d_addr.sa.sa_family != 0)
	{
		/*
		**  Ignore mapped IPv4 address since
		**  there is a ClientPortOptions setting
		**  for IPv4.
		*/

		goto nextaddr;
	}
#endif /* NETINET6 */

	switch (addr.sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		addr.sin.sin_port = port;
		addrlen = sizeof(struct sockaddr_in);
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		addr.sin6.sin6_port = port;
		addrlen = sizeof(struct sockaddr_in6);
		break;
#endif /* NETINET6 */

#if NETISO
	  case AF_ISO:
		/* assume two byte transport selector */
		memmove(TSEL((struct sockaddr_iso *) &addr), (char *) &port, 2);
		addrlen = sizeof(struct sockaddr_iso);
		break;
#endif /* NETISO */

	  default:
		syserr("Can't connect to address family %d", addr.sa.sa_family);
		mci_setstat(mci, EX_NOHOST, "5.1.2", NULL);
		errno = EINVAL;
#if NETINET6
		if (hp != NULL)
			freehostent(hp);
#endif /* NETINET6 */
		return EX_NOHOST;
	}

	/*
	**  Try to actually open the connection.
	*/

#if XLA
	/* if too many connections, don't bother trying */
	if (!xla_noqueue_ok(host))
	{
# if NETINET6
		if (hp != NULL)
			freehostent(hp);
# endif /* NETINET6 */
		return EX_TEMPFAIL;
	}
#endif /* XLA */

	for (;;)
	{
		if (tTd(16, 1))
			sm_dprintf("makeconnection (%s [%s].%d (%d))\n",
				   host, anynet_ntoa(&addr), ntohs(port),
				   (int) addr.sa.sa_family);

		/* save for logging */
		CurHostAddr = addr;

#if HASRRESVPORT
		if (bitnset(M_SECURE_PORT, mci->mci_mailer->m_flags))
		{
			int rport = IPPORT_RESERVED - 1;

			s = rresvport(&rport);
		}
		else
#endif /* HASRRESVPORT */
		{
			s = socket(addr.sa.sa_family, SOCK_STREAM, 0);
		}
		if (s < 0)
		{
			save_errno = errno;
			syserr("makeconnection: cannot create socket");
#if XLA
			xla_host_end(host);
#endif /* XLA */
			mci_setstat(mci, EX_TEMPFAIL, "4.4.5", NULL);
#if NETINET6
			if (hp != NULL)
				freehostent(hp);
#endif /* NETINET6 */
			errno = save_errno;
			return EX_TEMPFAIL;
		}

#ifdef SO_SNDBUF
		if (ClientSettings[family].d_tcpsndbufsize > 0)
		{
			if (setsockopt(s, SOL_SOCKET, SO_SNDBUF,
				       (char *) &ClientSettings[family].d_tcpsndbufsize,
				       sizeof(ClientSettings[family].d_tcpsndbufsize)) < 0)
				syserr("makeconnection: setsockopt(SO_SNDBUF)");
		}
#endif /* SO_SNDBUF */
#ifdef SO_RCVBUF
		if (ClientSettings[family].d_tcprcvbufsize > 0)
		{
			if (setsockopt(s, SOL_SOCKET, SO_RCVBUF,
				       (char *) &ClientSettings[family].d_tcprcvbufsize,
				       sizeof(ClientSettings[family].d_tcprcvbufsize)) < 0)
				syserr("makeconnection: setsockopt(SO_RCVBUF)");
		}
#endif /* SO_RCVBUF */

		if (tTd(16, 1))
			sm_dprintf("makeconnection: fd=%d\n", s);

		/* turn on network debugging? */
		if (tTd(16, 101))
		{
			int on = 1;

			(void) setsockopt(s, SOL_SOCKET, SO_DEBUG,
					  (char *)&on, sizeof(on));
		}
		if (e->e_xfp != NULL)	/* for debugging */
			(void) sm_io_flush(e->e_xfp, SM_TIME_DEFAULT);
		errno = 0;		/* for debugging */

		if (clt_bind)
		{
			int on = 1;

			switch (clt_addr.sa.sa_family)
			{
#if NETINET
			  case AF_INET:
				if (clt_addr.sin.sin_port != 0)
					(void) setsockopt(s, SOL_SOCKET,
							  SO_REUSEADDR,
							  (char *) &on,
							  sizeof(on));
				break;
#endif /* NETINET */

#if NETINET6
			  case AF_INET6:
				if (clt_addr.sin6.sin6_port != 0)
					(void) setsockopt(s, SOL_SOCKET,
							  SO_REUSEADDR,
							  (char *) &on,
							  sizeof(on));
				break;
#endif /* NETINET6 */
			}

			if (bind(s, &clt_addr.sa, socksize) < 0)
			{
				save_errno = errno;
				(void) close(s);
				errno = save_errno;
				syserr("makeconnection: cannot bind socket [%s]",
				       anynet_ntoa(&clt_addr));
#if NETINET6
				if (hp != NULL)
					freehostent(hp);
#endif /* NETINET6 */
				errno = save_errno;
				return EX_TEMPFAIL;
			}
		}

		/*
		**  Linux seems to hang in connect for 90 minutes (!!!).
		**  Time out the connect to avoid this problem.
		*/

		if (setjmp(CtxConnectTimeout) == 0)
		{
			int i;

			if (e->e_ntries <= 0 && TimeOuts.to_iconnect != 0)
				ev = sm_setevent(TimeOuts.to_iconnect,
						 connecttimeout, 0);
			else if (TimeOuts.to_connect != 0)
				ev = sm_setevent(TimeOuts.to_connect,
						 connecttimeout, 0);
			else
				ev = NULL;

			switch (ConnectOnlyTo.sa.sa_family)
			{
#if NETINET
			  case AF_INET:
				addr.sin.sin_addr.s_addr = ConnectOnlyTo.sin.sin_addr.s_addr;
				addr.sa.sa_family = ConnectOnlyTo.sa.sa_family;
				break;
#endif /* NETINET */

#if NETINET6
			  case AF_INET6:
				memmove(&addr.sin6.sin6_addr,
					&ConnectOnlyTo.sin6.sin6_addr,
					IN6ADDRSZ);
				break;
#endif /* NETINET6 */
			}
			if (tTd(16, 1))
				sm_dprintf("Connecting to [%s]...\n", anynet_ntoa(&addr));
			i = connect(s, (struct sockaddr *) &addr, addrlen);
			save_errno = errno;
			if (ev != NULL)
				sm_clrevent(ev);
			if (i >= 0)
				break;
		}
		else
			save_errno = errno;

		/* couldn't connect.... figure out why */
		(void) close(s);

		/* if running demand-dialed connection, try again */
		if (DialDelay > 0 && firstconnect &&
		    bitnset(M_DIALDELAY, mci->mci_mailer->m_flags))
		{
			if (tTd(16, 1))
				sm_dprintf("Connect failed (%s); trying again...\n",
					   sm_errstring(save_errno));
			firstconnect = false;
			(void) sleep(DialDelay);
			continue;
		}

		if (LogLevel > 13)
			sm_syslog(LOG_INFO, e->e_id,
				  "makeconnection (%s [%s]) failed: %s",
				  host, anynet_ntoa(&addr),
				  sm_errstring(save_errno));

#if NETINET6
nextaddr:
#endif /* NETINET6 */
		if (hp != NULL && hp->h_addr_list[addrno] != NULL &&
		    (enough == 0 || curtime() < enough))
		{
			if (tTd(16, 1))
				sm_dprintf("Connect failed (%s); trying new address....\n",
					   sm_errstring(save_errno));
			switch (addr.sa.sa_family)
			{
#if NETINET
			  case AF_INET:
				memmove(&addr.sin.sin_addr,
					hp->h_addr_list[addrno++],
					INADDRSZ);
				break;
#endif /* NETINET */

#if NETINET6
			  case AF_INET6:
				memmove(&addr.sin6.sin6_addr,
					hp->h_addr_list[addrno++],
					IN6ADDRSZ);
				break;
#endif /* NETINET6 */

			  default:
				memmove(addr.sa.sa_data,
					hp->h_addr_list[addrno++],
					hp->h_length);
				break;
			}
			continue;
		}
		errno = save_errno;

#if NETINET6
		if (family == AF_INET6)
		{
			if (tTd(16, 1))
				sm_dprintf("Connect failed (%s); retrying with AF_INET....\n",
					   sm_errstring(save_errno));
			v6found = true;
			family = AF_INET;
			if (hp != NULL)
			{
				freehostent(hp);
				hp = NULL;
			}
			goto v4retry;
		}
	v6tempfail:
#endif /* NETINET6 */
		/* couldn't open connection */
#if NETINET6
		/* Don't clobber an already saved errno from v4retry */
		if (errno > 0)
#endif /* NETINET6 */
			save_errno = errno;
		if (tTd(16, 1))
			sm_dprintf("Connect failed (%s)\n",
				   sm_errstring(save_errno));
#if XLA
		xla_host_end(host);
#endif /* XLA */
		mci_setstat(mci, EX_TEMPFAIL, "4.4.1", NULL);
#if NETINET6
		if (hp != NULL)
			freehostent(hp);
#endif /* NETINET6 */
		errno = save_errno;
		return EX_TEMPFAIL;
	}

#if NETINET6
	if (hp != NULL)
	{
		freehostent(hp);
		hp = NULL;
	}
#endif /* NETINET6 */

	/* connection ok, put it into canonical form */
	mci->mci_out = NULL;
	if ((mci->mci_out = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
				       (void *) &s,
				       SM_IO_WRONLY_B, NULL)) == NULL ||
	    (s = dup(s)) < 0 ||
	    (mci->mci_in = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
				      (void *) &s,
				      SM_IO_RDONLY_B, NULL)) == NULL)
	{
		save_errno = errno;
		syserr("cannot open SMTP client channel, fd=%d", s);
		mci_setstat(mci, EX_TEMPFAIL, "4.4.5", NULL);
		if (mci->mci_out != NULL)
			(void) sm_io_close(mci->mci_out, SM_TIME_DEFAULT);
		(void) close(s);
		errno = save_errno;
		return EX_TEMPFAIL;
	}
	sm_io_automode(mci->mci_out, mci->mci_in);

	/* set {client_flags} */
	if (ClientSettings[addr.sa.sa_family].d_mflags != NULL)
	{
		macdefine(&mci->mci_macro, A_PERM,
			  macid("{client_flags}"),
			  ClientSettings[addr.sa.sa_family].d_mflags);
	}
	else
		macdefine(&mci->mci_macro, A_PERM,
			  macid("{client_flags}"), "");

	/* "add" {client_flags} to bitmap */
	if (bitnset(D_IFNHELO, ClientSettings[addr.sa.sa_family].d_flags))
	{
		/* look for just this one flag */
		setbitn(D_IFNHELO, d_flags);
	}

	/* find out name for Interface through which we connect */
	len = sizeof(addr);
	if (getsockname(s, &addr.sa, &len) == 0)
	{
		char *name;
		char family[5];

		macdefine(&BlankEnvelope.e_macro, A_TEMP,
			macid("{if_addr_out}"), anynet_ntoa(&addr));
		(void) sm_snprintf(family, sizeof(family), "%d",
			addr.sa.sa_family);
		macdefine(&BlankEnvelope.e_macro, A_TEMP,
			macid("{if_family_out}"), family);

		name = hostnamebyanyaddr(&addr);
		macdefine(&BlankEnvelope.e_macro, A_TEMP,
			macid("{if_name_out}"), name);
		if (LogLevel > 11)
		{
			/* log connection information */
			sm_syslog(LOG_INFO, e->e_id,
				  "SMTP outgoing connect on %.40s", name);
		}
		if (bitnset(D_IFNHELO, d_flags))
		{
			if (name[0] != '[' && strchr(name, '.') != NULL)
				mci->mci_heloname = newstr(name);
		}
	}
	else
	{
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			macid("{if_name_out}"), NULL);
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			macid("{if_addr_out}"), NULL);
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			macid("{if_family_out}"), NULL);
	}

	/* Use the configured HeloName as appropriate */
	if (HeloName != NULL && HeloName[0] != '\0')
	{
		SM_FREE_CLR(mci->mci_heloname);
		mci->mci_heloname = newstr(HeloName);
	}

	mci_setstat(mci, EX_OK, NULL, NULL);
	return EX_OK;
}

static void
connecttimeout(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(CtxConnectTimeout, 1);
}
/*
**  MAKECONNECTION_DS -- make a connection to a domain socket.
**
**	Parameters:
**		mux_path -- the path of the socket to connect to.
**		mci -- a pointer to the mail connection information
**			structure to be filled in.
**
**	Returns:
**		An exit code telling whether the connection could be
**			made and if not why not.
**
**	Side Effects:
**		none.
*/

#if NETUNIX
int
makeconnection_ds(mux_path, mci)
	char *mux_path;
	register MCI *mci;
{
	int sock;
	int rval, save_errno;
	long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_ROOTOK|SFF_EXECOK;
	struct sockaddr_un unix_addr;

	/* if not safe, don't connect */
	rval = safefile(mux_path, RunAsUid, RunAsGid, RunAsUserName,
			sff, S_IRUSR|S_IWUSR, NULL);

	if (rval != 0)
	{
		syserr("makeconnection_ds: unsafe domain socket %s",
			mux_path);
		mci_setstat(mci, EX_TEMPFAIL, "4.3.5", NULL);
		errno = rval;
		return EX_TEMPFAIL;
	}

	/* prepare address structure */
	memset(&unix_addr, '\0', sizeof(unix_addr));
	unix_addr.sun_family = AF_UNIX;

	if (strlen(mux_path) >= sizeof(unix_addr.sun_path))
	{
		syserr("makeconnection_ds: domain socket name %s too long",
			mux_path);

		/* XXX why TEMPFAIL but 5.x.y ? */
		mci_setstat(mci, EX_TEMPFAIL, "5.3.5", NULL);
		errno = ENAMETOOLONG;
		return EX_UNAVAILABLE;
	}
	(void) sm_strlcpy(unix_addr.sun_path, mux_path,
			  sizeof(unix_addr.sun_path));

	/* initialize domain socket */
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
	{
		save_errno = errno;
		syserr("makeconnection_ds: could not create domain socket %s",
			mux_path);
		mci_setstat(mci, EX_TEMPFAIL, "4.4.5", NULL);
		errno = save_errno;
		return EX_TEMPFAIL;
	}

	/* connect to server */
	if (connect(sock, (struct sockaddr *) &unix_addr,
		    sizeof(unix_addr)) == -1)
	{
		save_errno = errno;
		syserr("Could not connect to socket %s", mux_path);
		mci_setstat(mci, EX_TEMPFAIL, "4.4.1", NULL);
		(void) close(sock);
		errno = save_errno;
		return EX_TEMPFAIL;
	}

	/* connection ok, put it into canonical form */
	mci->mci_out = NULL;
	if ((mci->mci_out = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
				       (void *) &sock, SM_IO_WRONLY_B, NULL))
					== NULL
	    || (sock = dup(sock)) < 0 ||
	    (mci->mci_in = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
				      (void *) &sock, SM_IO_RDONLY_B, NULL))
					== NULL)
	{
		save_errno = errno;
		syserr("cannot open SMTP client channel, fd=%d", sock);
		mci_setstat(mci, EX_TEMPFAIL, "4.4.5", NULL);
		if (mci->mci_out != NULL)
			(void) sm_io_close(mci->mci_out, SM_TIME_DEFAULT);
		(void) close(sock);
		errno = save_errno;
		return EX_TEMPFAIL;
	}
	sm_io_automode(mci->mci_out, mci->mci_in);

	mci_setstat(mci, EX_OK, NULL, NULL);
	errno = 0;
	return EX_OK;
}
#endif /* NETUNIX */
/*
**  SHUTDOWN_DAEMON -- Performs a clean shutdown of the daemon
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		closes control socket, exits.
*/

void
shutdown_daemon()
{
	int i;
	char *reason;

	sm_allsignals(true);

	reason = ShutdownRequest;
	ShutdownRequest = NULL;
	PendingSignal = 0;

	if (LogLevel > 9)
		sm_syslog(LOG_INFO, CurEnv->e_id, "stopping daemon, reason=%s",
			  reason == NULL ? "implicit call" : reason);

	FileName = NULL;
	closecontrolsocket(true);
#if XLA
	xla_all_end();
#endif /* XLA */

	for (i = 0; i < NDaemons; i++)
	{
		if (Daemons[i].d_socket >= 0)
		{
			(void) close(Daemons[i].d_socket);
			Daemons[i].d_socket = -1;

#if NETUNIX
			/* Remove named sockets */
			if (Daemons[i].d_addr.sa.sa_family == AF_UNIX)
			{
				int rval;
				long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_MUSTOWN|SFF_EXECOK|SFF_CREAT;

				/* if not safe, don't use it */
				rval = safefile(Daemons[i].d_addr.sunix.sun_path,
						RunAsUid, RunAsGid,
						RunAsUserName, sff,
						S_IRUSR|S_IWUSR, NULL);
				if (rval == 0 &&
				    unlink(Daemons[i].d_addr.sunix.sun_path) < 0)
				{
					sm_syslog(LOG_WARNING, NOQID,
						  "Could not remove daemon %s socket: %s: %s",
						  Daemons[i].d_name,
						  Daemons[i].d_addr.sunix.sun_path,
						  sm_errstring(errno));
				}
			}
#endif /* NETUNIX */
		}
	}

	finis(false, true, EX_OK);
}
/*
**  RESTART_DAEMON -- Performs a clean restart of the daemon
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		restarts the daemon or exits if restart fails.
*/

/* Make a non-DFL/IGN signal a noop */
#define SM_NOOP_SIGNAL(sig, old)				\
do								\
{								\
	(old) = sm_signal((sig), sm_signal_noop);		\
	if ((old) == SIG_IGN || (old) == SIG_DFL)		\
		(void) sm_signal((sig), (old));			\
} while (0)

void
restart_daemon()
{
	bool drop;
	int save_errno;
	char *reason;
	sigfunc_t ignore, oalrm, ousr1;
	extern int DtableSize;

	/* clear the events to turn off SIGALRMs */
	sm_clear_events();
	sm_allsignals(true);

	reason = RestartRequest;
	RestartRequest = NULL;
	PendingSignal = 0;

	if (SaveArgv[0][0] != '/')
	{
		if (LogLevel > 3)
			sm_syslog(LOG_INFO, NOQID,
				  "could not restart: need full path");
		finis(false, true, EX_OSFILE);
		/* NOTREACHED */
	}
	if (LogLevel > 3)
		sm_syslog(LOG_INFO, NOQID, "restarting %s due to %s",
			  SaveArgv[0],
			  reason == NULL ? "implicit call" : reason);

	closecontrolsocket(true);
#if SM_CONF_SHM
	cleanup_shm(DaemonPid == getpid());
#endif /* SM_CONF_SHM */

	/* close locked pid file */
	close_sendmail_pid();

	/*
	**  Want to drop to the user who started the process in all cases
	**  *but* when running as "smmsp" for the clientmqueue queue run
	**  daemon.  In that case, UseMSP will be true, RunAsUid should not
	**  be root, and RealUid should be either 0 or RunAsUid.
	*/

	drop = !(UseMSP && RunAsUid != 0 &&
		 (RealUid == 0 || RealUid == RunAsUid));

	if (drop_privileges(drop) != EX_OK)
	{
		if (LogLevel > 0)
			sm_syslog(LOG_ALERT, NOQID,
				  "could not drop privileges: %s",
				  sm_errstring(errno));
		finis(false, true, EX_OSERR);
		/* NOTREACHED */
	}

	sm_close_on_exec(STDERR_FILENO + 1, DtableSize);

	/*
	**  Need to allow signals before execve() to make them "harmless".
	**  However, the default action can be "terminate", so it isn't
	**  really harmless.  Setting signals to IGN will cause them to be
	**  ignored in the new process to, so that isn't a good alternative.
	*/

	SM_NOOP_SIGNAL(SIGALRM, oalrm);
	SM_NOOP_SIGNAL(SIGCHLD, ignore);
	SM_NOOP_SIGNAL(SIGHUP, ignore);
	SM_NOOP_SIGNAL(SIGINT, ignore);
	SM_NOOP_SIGNAL(SIGPIPE, ignore);
	SM_NOOP_SIGNAL(SIGTERM, ignore);
#ifdef SIGUSR1
	SM_NOOP_SIGNAL(SIGUSR1, ousr1);
#endif /* SIGUSR1 */

	/* Turn back on signals */
	sm_allsignals(false);

	(void) execve(SaveArgv[0], (ARGV_T) SaveArgv, (ARGV_T) ExternalEnviron);
	save_errno = errno;

	/* block signals again and restore needed signals */
	sm_allsignals(true);

	/* For finis() events */
	(void) sm_signal(SIGALRM, oalrm);

#ifdef SIGUSR1
	/* For debugging finis() */
	(void) sm_signal(SIGUSR1, ousr1);
#endif /* SIGUSR1 */

	errno = save_errno;
	if (LogLevel > 0)
		sm_syslog(LOG_ALERT, NOQID, "could not exec %s: %s",
			  SaveArgv[0], sm_errstring(errno));
	finis(false, true, EX_OSFILE);
	/* NOTREACHED */
}
/*
**  MYHOSTNAME -- return the name of this host.
**
**	Parameters:
**		hostbuf -- a place to return the name of this host.
**		size -- the size of hostbuf.
**
**	Returns:
**		A list of aliases for this host.
**
**	Side Effects:
**		Adds numeric codes to $=w.
*/

struct hostent *
myhostname(hostbuf, size)
	char hostbuf[];
	int size;
{
	register struct hostent *hp;

	if (gethostname(hostbuf, size) < 0 || hostbuf[0] == '\0')
		(void) sm_strlcpy(hostbuf, "localhost", size);
	hp = sm_gethostbyname(hostbuf, InetMode);
#if NETINET && NETINET6
	if (hp == NULL && InetMode == AF_INET6)
	{
		/*
		**  It's possible that this IPv6 enabled machine doesn't
		**  actually have any IPv6 interfaces and, therefore, no
		**  IPv6 addresses.  Fall back to AF_INET.
		*/

		hp = sm_gethostbyname(hostbuf, AF_INET);
	}
#endif /* NETINET && NETINET6 */
	if (hp == NULL)
		return NULL;
	if (strchr(hp->h_name, '.') != NULL || strchr(hostbuf, '.') == NULL)
		(void) cleanstrcpy(hostbuf, hp->h_name, size);

#if NETINFO
	if (strchr(hostbuf, '.') == NULL)
	{
		char *domainname;

		domainname = ni_propval("/locations", NULL, "resolver",
					"domain", '\0');
		if (domainname != NULL &&
		    strlen(domainname) + strlen(hostbuf) + 1 < size)
			(void) sm_strlcat2(hostbuf, ".", domainname, size);
	}
#endif /* NETINFO */

	/*
	**  If there is still no dot in the name, try looking for a
	**  dotted alias.
	*/

	if (strchr(hostbuf, '.') == NULL)
	{
		char **ha;

		for (ha = hp->h_aliases; ha != NULL && *ha != NULL; ha++)
		{
			if (strchr(*ha, '.') != NULL)
			{
				(void) cleanstrcpy(hostbuf, *ha, size - 1);
				hostbuf[size - 1] = '\0';
				break;
			}
		}
	}

	/*
	**  If _still_ no dot, wait for a while and try again -- it is
	**  possible that some service is starting up.  This can result
	**  in excessive delays if the system is badly configured, but
	**  there really isn't a way around that, particularly given that
	**  the config file hasn't been read at this point.
	**  All in all, a bit of a mess.
	*/

	if (strchr(hostbuf, '.') == NULL &&
	    !getcanonname(hostbuf, size, true, NULL))
	{
		sm_syslog(LocalDaemon ? LOG_WARNING : LOG_CRIT, NOQID,
			  "My unqualified host name (%s) unknown; sleeping for retry",
			  hostbuf);
		message("My unqualified host name (%s) unknown; sleeping for retry",
			hostbuf);
		(void) sleep(60);
		if (!getcanonname(hostbuf, size, true, NULL))
		{
			sm_syslog(LocalDaemon ? LOG_WARNING : LOG_ALERT, NOQID,
				  "unable to qualify my own domain name (%s) -- using short name",
				  hostbuf);
			message("WARNING: unable to qualify my own domain name (%s) -- using short name",
				hostbuf);
		}
	}
	return hp;
}
/*
**  ADDRCMP -- compare two host addresses
**
**	Parameters:
**		hp -- hostent structure for the first address
**		ha -- actual first address
**		sa -- second address
**
**	Returns:
**		0 -- if ha and sa match
**		else -- they don't match
*/

static int
addrcmp(hp, ha, sa)
	struct hostent *hp;
	char *ha;
	SOCKADDR *sa;
{
#if NETINET6
	unsigned char *a;
#endif /* NETINET6 */

	switch (sa->sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		if (hp->h_addrtype == AF_INET)
			return memcmp(ha, (char *) &sa->sin.sin_addr, INADDRSZ);
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		a = (unsigned char *) &sa->sin6.sin6_addr;

		/* Straight binary comparison */
		if (hp->h_addrtype == AF_INET6)
			return memcmp(ha, a, IN6ADDRSZ);

		/* If IPv4-mapped IPv6 address, compare the IPv4 section */
		if (hp->h_addrtype == AF_INET &&
		    IN6_IS_ADDR_V4MAPPED(&sa->sin6.sin6_addr))
			return memcmp(a + IN6ADDRSZ - INADDRSZ, ha, INADDRSZ);
		break;
#endif /* NETINET6 */
	}
	return -1;
}
/*
**  GETAUTHINFO -- get the real host name associated with a file descriptor
**
**	Uses RFC1413 protocol to try to get info from the other end.
**
**	Parameters:
**		fd -- the descriptor
**		may_be_forged -- an outage that is set to true if the
**			forward lookup of RealHostName does not match
**			RealHostAddr; set to false if they do match.
**
**	Returns:
**		The user@host information associated with this descriptor.
*/

static jmp_buf	CtxAuthTimeout;

static void
authtimeout(ignore)
	int ignore;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(CtxAuthTimeout, 1);
}

char *
getauthinfo(fd, may_be_forged)
	int fd;
	bool *may_be_forged;
{
	unsigned short SM_NONVOLATILE port = 0;
	SOCKADDR_LEN_T falen;
	register char *volatile p = NULL;
	SOCKADDR la;
	SOCKADDR_LEN_T lalen;
#ifndef NO_GETSERVBYNAME
	register struct servent *sp;
# if NETINET
	static unsigned short port4 = 0;
# endif /* NETINET */
# if NETINET6
	static unsigned short port6 = 0;
# endif /* NETINET6 */
#endif /* ! NO_GETSERVBYNAME */
	volatile int s;
	int i = 0;
	size_t len;
	SM_EVENT *ev;
	int nleft;
	struct hostent *hp;
	char *ostype = NULL;
	char **ha;
	char ibuf[MAXNAME + 1];
	static char hbuf[MAXNAME + MAXAUTHINFO + 11];

	*may_be_forged = true;
	falen = sizeof(RealHostAddr);
	if (isatty(fd) || (i = getpeername(fd, &RealHostAddr.sa, &falen)) < 0 ||
	    falen <= 0 || RealHostAddr.sa.sa_family == 0)
	{
		if (i < 0)
		{
			/*
			**  ENOTSOCK is OK: bail on anything else, but reset
			**  errno in this case, so a mis-report doesn't
			**  happen later.
			*/

			if (errno != ENOTSOCK)
				return NULL;
			errno = 0;
		}

		*may_be_forged = false;
		(void) sm_strlcpyn(hbuf, sizeof(hbuf), 2, RealUserName,
				   "@localhost");
		if (tTd(9, 1))
			sm_dprintf("getauthinfo: %s\n", hbuf);
		return hbuf;
	}

	if (RealHostName == NULL)
	{
		/* translate that to a host name */
		RealHostName = newstr(hostnamebyanyaddr(&RealHostAddr));
		if (strlen(RealHostName) > MAXNAME)
			RealHostName[MAXNAME] = '\0'; /* XXX - 1 ? */
	}

	/* cross check RealHostName with forward DNS lookup */
	if (anynet_ntoa(&RealHostAddr)[0] == '[' ||
	    RealHostName[0] == '[')
		*may_be_forged = false;
	else
	{
		int family;

		family = RealHostAddr.sa.sa_family;
#if NETINET6 && NEEDSGETIPNODE
		/*
		**  If RealHostAddr is an IPv6 connection with an
		**  IPv4-mapped address, we need RealHostName's IPv4
		**  address(es) for addrcmp() to compare against
		**  RealHostAddr.
		**
		**  Actually, we only need to do this for systems
		**  which NEEDSGETIPNODE since the real getipnodebyname()
		**  already does V4MAPPED address via the AI_V4MAPPEDCFG
		**  flag.  A better fix to this problem is to add this
		**  functionality to our stub getipnodebyname().
		*/

		if (family == AF_INET6 &&
		    IN6_IS_ADDR_V4MAPPED(&RealHostAddr.sin6.sin6_addr))
			family = AF_INET;
#endif /* NETINET6 && NEEDSGETIPNODE */

		/* try to match the reverse against the forward lookup */
		hp = sm_gethostbyname(RealHostName, family);
		if (hp != NULL)
		{
			for (ha = hp->h_addr_list; *ha != NULL; ha++)
			{
				if (addrcmp(hp, *ha, &RealHostAddr) == 0)
				{
					*may_be_forged = false;
					break;
				}
			}
#if NETINET6
			freehostent(hp);
			hp = NULL;
#endif /* NETINET6 */
		}
	}

	if (TimeOuts.to_ident == 0)
		goto noident;

	lalen = sizeof(la);
	switch (RealHostAddr.sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		if (getsockname(fd, &la.sa, &lalen) < 0 ||
		    lalen <= 0 ||
		    la.sa.sa_family != AF_INET)
		{
			/* no ident info */
			goto noident;
		}
		port = RealHostAddr.sin.sin_port;

		/* create ident query */
		(void) sm_snprintf(ibuf, sizeof(ibuf), "%d,%d\r\n",
				ntohs(RealHostAddr.sin.sin_port),
				ntohs(la.sin.sin_port));

		/* create local address */
		la.sin.sin_port = 0;

		/* create foreign address */
# ifdef NO_GETSERVBYNAME
		RealHostAddr.sin.sin_port = htons(113);
# else /* NO_GETSERVBYNAME */

		/*
		**  getservbyname() consumes about 5% of the time
		**  when receiving a small message (almost all of the time
		**  spent in this routine).
		**  Hence we store the port in a static variable
		**  to save this time.
		**  The portnumber shouldn't change very often...
		**  This code makes the assumption that the port number
		**  is not 0.
		*/

		if (port4 == 0)
		{
			sp = getservbyname("auth", "tcp");
			if (sp != NULL)
				port4 = sp->s_port;
			else
				port4 = htons(113);
		}
		RealHostAddr.sin.sin_port = port4;
		break;
# endif /* NO_GETSERVBYNAME */
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		if (getsockname(fd, &la.sa, &lalen) < 0 ||
		    lalen <= 0 ||
		    la.sa.sa_family != AF_INET6)
		{
			/* no ident info */
			goto noident;
		}
		port = RealHostAddr.sin6.sin6_port;

		/* create ident query */
		(void) sm_snprintf(ibuf, sizeof(ibuf), "%d,%d\r\n",
				ntohs(RealHostAddr.sin6.sin6_port),
				ntohs(la.sin6.sin6_port));

		/* create local address */
		la.sin6.sin6_port = 0;

		/* create foreign address */
# ifdef NO_GETSERVBYNAME
		RealHostAddr.sin6.sin6_port = htons(113);
# else /* NO_GETSERVBYNAME */
		if (port6 == 0)
		{
			sp = getservbyname("auth", "tcp");
			if (sp != NULL)
				port6 = sp->s_port;
			else
				port6 = htons(113);
		}
		RealHostAddr.sin6.sin6_port = port6;
		break;
# endif /* NO_GETSERVBYNAME */
#endif /* NETINET6 */
	  default:
		/* no ident info */
		goto noident;
	}

	s = -1;
	if (setjmp(CtxAuthTimeout) != 0)
	{
		if (s >= 0)
			(void) close(s);
		goto noident;
	}

	/* put a timeout around the whole thing */
	ev = sm_setevent(TimeOuts.to_ident, authtimeout, 0);

	/* connect to foreign IDENT server using same address as SMTP socket */
	s = socket(la.sa.sa_family, SOCK_STREAM, 0);
	if (s < 0)
	{
		sm_clrevent(ev);
		goto noident;
	}
	if (bind(s, &la.sa, lalen) < 0 ||
	    connect(s, &RealHostAddr.sa, lalen) < 0)
		goto closeident;

	if (tTd(9, 10))
		sm_dprintf("getauthinfo: sent %s", ibuf);

	/* send query */
	if (write(s, ibuf, strlen(ibuf)) < 0)
		goto closeident;

	/* get result */
	p = &ibuf[0];
	nleft = sizeof(ibuf) - 1;
	while ((i = read(s, p, nleft)) > 0)
	{
		char *s;

		p += i;
		nleft -= i;
		*p = '\0';
		if ((s = strchr(ibuf, '\n')) != NULL)
		{
			if (p > s + 1)
			{
				p = s + 1;
				*p = '\0';
			}
			break;
		}
		if (nleft <= 0)
			break;
	}
	(void) close(s);
	sm_clrevent(ev);
	if (i < 0 || p == &ibuf[0])
		goto noident;

	if (p >= &ibuf[2] && *--p == '\n' && *--p == '\r')
		p--;
	*++p = '\0';

	if (tTd(9, 3))
		sm_dprintf("getauthinfo:  got %s\n", ibuf);

	/* parse result */
	p = strchr(ibuf, ':');
	if (p == NULL)
	{
		/* malformed response */
		goto noident;
	}
	while (isascii(*++p) && isspace(*p))
		continue;
	if (sm_strncasecmp(p, "userid", 6) != 0)
	{
		/* presumably an error string */
		goto noident;
	}
	p += 6;
	while (isascii(*p) && isspace(*p))
		p++;
	if (*p++ != ':')
	{
		/* either useridxx or malformed response */
		goto noident;
	}

	/* p now points to the OSTYPE field */
	while (isascii(*p) && isspace(*p))
		p++;
	ostype = p;
	p = strchr(p, ':');
	if (p == NULL)
	{
		/* malformed response */
		goto noident;
	}
	else
	{
		char *charset;

		*p = '\0';
		charset = strchr(ostype, ',');
		if (charset != NULL)
			*charset = '\0';
	}

	/* 1413 says don't do this -- but it's broken otherwise */
	while (isascii(*++p) && isspace(*p))
		continue;

	/* p now points to the authenticated name -- copy carefully */
	if (sm_strncasecmp(ostype, "other", 5) == 0 &&
	    (ostype[5] == ' ' || ostype[5] == '\0'))
	{
		(void) sm_strlcpy(hbuf, "IDENT:", sizeof(hbuf));
		cleanstrcpy(&hbuf[6], p, MAXAUTHINFO);
	}
	else
		cleanstrcpy(hbuf, p, MAXAUTHINFO);
	len = strlen(hbuf);
	(void) sm_strlcpyn(&hbuf[len], sizeof(hbuf) - len, 2, "@",
			   RealHostName == NULL ? "localhost" : RealHostName);
	goto postident;

closeident:
	(void) close(s);
	sm_clrevent(ev);

noident:
	/* put back the original incoming port */
	switch (RealHostAddr.sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		if (port > 0)
			RealHostAddr.sin.sin_port = port;
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		if (port > 0)
			RealHostAddr.sin6.sin6_port = port;
		break;
#endif /* NETINET6 */
	}

	if (RealHostName == NULL)
	{
		if (tTd(9, 1))
			sm_dprintf("getauthinfo: NULL\n");
		return NULL;
	}
	(void) sm_strlcpy(hbuf, RealHostName, sizeof(hbuf));

postident:
#if IP_SRCROUTE
# ifndef GET_IPOPT_DST
#  define GET_IPOPT_DST(dst)	(dst)
# endif /* ! GET_IPOPT_DST */
	/*
	**  Extract IP source routing information.
	**
	**	Format of output for a connection from site a through b
	**	through c to d:
	**		loose:      @site-c@site-b:site-a
	**		strict:	   !@site-c@site-b:site-a
	**
	**	o - pointer within ipopt_list structure.
	**	q - pointer within ls/ss rr route data
	**	p - pointer to hbuf
	*/

	if (RealHostAddr.sa.sa_family == AF_INET)
	{
		SOCKOPT_LEN_T ipoptlen;
		int j;
		unsigned char *q;
		unsigned char *o;
		int l;
		struct IPOPTION ipopt;

		ipoptlen = sizeof(ipopt);
		if (getsockopt(fd, IPPROTO_IP, IP_OPTIONS,
			       (char *) &ipopt, &ipoptlen) < 0)
			goto noipsr;
		if (ipoptlen == 0)
			goto noipsr;
		o = (unsigned char *) ipopt.IP_LIST;
		while (o != NULL && o < (unsigned char *) &ipopt + ipoptlen)
		{
			switch (*o)
			{
			  case IPOPT_EOL:
				o = NULL;
				break;

			  case IPOPT_NOP:
				o++;
				break;

			  case IPOPT_SSRR:
			  case IPOPT_LSRR:
				/*
				**  Source routing.
				**	o[0] is the option type (loose/strict).
				**	o[1] is the length of this option,
				**		including option type and
				**		length.
				**	o[2] is the pointer into the route
				**		data.
				**	o[3] begins the route data.
				*/

				p = &hbuf[strlen(hbuf)];
				l = sizeof(hbuf) - (hbuf - p) - 6;
				(void) sm_snprintf(p, SPACELEFT(hbuf, p),
					" [%s@%.*s",
					*o == IPOPT_SSRR ? "!" : "",
					l > 240 ? 120 : l / 2,
					inet_ntoa(GET_IPOPT_DST(ipopt.IP_DST)));
				i = strlen(p);
				p += i;
				l -= strlen(p);

				j = o[1] / sizeof(struct in_addr) - 1;

				/* q skips length and router pointer to data */
				q = &o[3];
				for ( ; j >= 0; j--)
				{
					struct in_addr addr;

					memcpy(&addr, q, sizeof(addr));
					(void) sm_snprintf(p,
						SPACELEFT(hbuf, p),
						"%c%.*s",
						j != 0 ? '@' : ':',
						l > 240 ? 120 :
							j == 0 ? l : l / 2,
						inet_ntoa(addr));
					i = strlen(p);
					p += i;
					l -= i + 1;
					q += sizeof(struct in_addr);
				}
				o += o[1];
				break;

			  default:
				/* Skip over option */
				o += o[1];
				break;
			}
		}
		(void) sm_snprintf(p, SPACELEFT(hbuf, p), "]");
		goto postipsr;
	}

noipsr:
#endif /* IP_SRCROUTE */
	if (RealHostName != NULL && RealHostName[0] != '[')
	{
		p = &hbuf[strlen(hbuf)];
		(void) sm_snprintf(p, SPACELEFT(hbuf, p), " [%.100s]",
				   anynet_ntoa(&RealHostAddr));
	}
	if (*may_be_forged)
	{
		p = &hbuf[strlen(hbuf)];
		(void) sm_strlcpy(p, " (may be forged)", SPACELEFT(hbuf, p));
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			  macid("{client_resolve}"), "FORGED");
	}

#if IP_SRCROUTE
postipsr:
#endif /* IP_SRCROUTE */

	/* put back the original incoming port */
	switch (RealHostAddr.sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		if (port > 0)
			RealHostAddr.sin.sin_port = port;
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		if (port > 0)
			RealHostAddr.sin6.sin6_port = port;
		break;
#endif /* NETINET6 */
	}

	if (tTd(9, 1))
		sm_dprintf("getauthinfo: %s\n", hbuf);
	return hbuf;
}
/*
**  HOST_MAP_LOOKUP -- turn a hostname into canonical form
**
**	Parameters:
**		map -- a pointer to this map.
**		name -- the (presumably unqualified) hostname.
**		av -- unused -- for compatibility with other mapping
**			functions.
**		statp -- an exit status (out parameter) -- set to
**			EX_TEMPFAIL if the name server is unavailable.
**
**	Returns:
**		The mapping, if found.
**		NULL if no mapping found.
**
**	Side Effects:
**		Looks up the host specified in hbuf.  If it is not
**		the canonical name for that host, return the canonical
**		name (unless MF_MATCHONLY is set, which will cause the
**		status only to be returned).
*/

char *
host_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	register struct hostent *hp;
#if NETINET
	struct in_addr in_addr;
#endif /* NETINET */
#if NETINET6
	struct in6_addr in6_addr;
#endif /* NETINET6 */
	char *cp, *ans = NULL;
	register STAB *s;
	time_t now;
#if NAMED_BIND
	time_t SM_NONVOLATILE retrans = 0;
	int SM_NONVOLATILE retry = 0;
#endif /* NAMED_BIND */
	char hbuf[MAXNAME + 1];

	/*
	**  See if we have already looked up this name.  If so, just
	**  return it (unless expired).
	*/

	now = curtime();
	s = stab(name, ST_NAMECANON, ST_ENTER);
	if (bitset(NCF_VALID, s->s_namecanon.nc_flags) &&
	    s->s_namecanon.nc_exp >= now)
	{
		if (tTd(9, 1))
			sm_dprintf("host_map_lookup(%s) => CACHE %s\n",
				    name,
				    s->s_namecanon.nc_cname == NULL
					? "NULL"
					: s->s_namecanon.nc_cname);
		errno = s->s_namecanon.nc_errno;
		SM_SET_H_ERRNO(s->s_namecanon.nc_herrno);
		*statp = s->s_namecanon.nc_stat;
		if (*statp == EX_TEMPFAIL)
		{
			CurEnv->e_status = "4.4.3";
			message("851 %s: Name server timeout",
				shortenstring(name, 33));
		}
		if (*statp != EX_OK)
			return NULL;
		if (s->s_namecanon.nc_cname == NULL)
		{
			syserr("host_map_lookup(%s): bogus NULL cache entry, errno=%d, h_errno=%d",
			       name,
			       s->s_namecanon.nc_errno,
			       s->s_namecanon.nc_herrno);
			return NULL;
		}
		if (bitset(MF_MATCHONLY, map->map_mflags))
			cp = map_rewrite(map, name, strlen(name), NULL);
		else
			cp = map_rewrite(map,
					 s->s_namecanon.nc_cname,
					 strlen(s->s_namecanon.nc_cname),
					 av);
		return cp;
	}

	/*
	**  If we are running without a regular network connection (usually
	**  dial-on-demand) and we are just queueing, we want to avoid DNS
	**  lookups because those could try to connect to a server.
	*/

	if (CurEnv->e_sendmode == SM_DEFER &&
	    bitset(MF_DEFER, map->map_mflags))
	{
		if (tTd(9, 1))
			sm_dprintf("host_map_lookup(%s) => DEFERRED\n", name);
		*statp = EX_TEMPFAIL;
		return NULL;
	}

	/*
	**  If first character is a bracket, then it is an address
	**  lookup.  Address is copied into a temporary buffer to
	**  strip the brackets and to preserve name if address is
	**  unknown.
	*/

	if (tTd(9, 1))
		sm_dprintf("host_map_lookup(%s) => ", name);
#if NAMED_BIND
	if (map->map_timeout > 0)
	{
		retrans = _res.retrans;
		_res.retrans = map->map_timeout;
	}
	if (map->map_retry > 0)
	{
		retry = _res.retry;
		_res.retry = map->map_retry;
	}
#endif /* NAMED_BIND */

	/* set default TTL */
	s->s_namecanon.nc_exp = now + SM_DEFAULT_TTL;
	if (*name != '[')
	{
		int ttl;

		(void) sm_strlcpy(hbuf, name, sizeof(hbuf));
		if (getcanonname(hbuf, sizeof(hbuf) - 1, !HasWildcardMX, &ttl))
		{
			ans = hbuf;
			if (ttl > 0)
				s->s_namecanon.nc_exp = now + SM_MIN(ttl,
								SM_DEFAULT_TTL);
		}
	}
	else
	{
		if ((cp = strchr(name, ']')) == NULL)
		{
			if (tTd(9, 1))
				sm_dprintf("FAILED\n");
			return NULL;
		}
		*cp = '\0';

		hp = NULL;
#if NETINET
		if ((in_addr.s_addr = inet_addr(&name[1])) != INADDR_NONE)
			hp = sm_gethostbyaddr((char *)&in_addr,
					      INADDRSZ, AF_INET);
#endif /* NETINET */
#if NETINET6
		if (hp == NULL &&
		    anynet_pton(AF_INET6, &name[1], &in6_addr) == 1)
			hp = sm_gethostbyaddr((char *)&in6_addr,
					      IN6ADDRSZ, AF_INET6);
#endif /* NETINET6 */
		*cp = ']';

		if (hp != NULL)
		{
			/* found a match -- copy out */
			ans = denlstring((char *) hp->h_name, true, true);
#if NETINET6
			if (ans == hp->h_name)
			{
				static char n[MAXNAME + 1];

				/* hp->h_name is about to disappear */
				(void) sm_strlcpy(n, ans, sizeof(n));
				ans = n;
			}
			freehostent(hp);
			hp = NULL;
#endif /* NETINET6 */
		}
	}
#if NAMED_BIND
	if (map->map_timeout > 0)
		_res.retrans = retrans;
	if (map->map_retry > 0)
		_res.retry = retry;
#endif /* NAMED_BIND */

	s->s_namecanon.nc_flags |= NCF_VALID;	/* will be soon */

	/* Found an answer */
	if (ans != NULL)
	{
		s->s_namecanon.nc_stat = *statp = EX_OK;
		if (s->s_namecanon.nc_cname != NULL)
			sm_free(s->s_namecanon.nc_cname);
		s->s_namecanon.nc_cname = sm_strdup_x(ans);
		if (bitset(MF_MATCHONLY, map->map_mflags))
			cp = map_rewrite(map, name, strlen(name), NULL);
		else
			cp = map_rewrite(map, ans, strlen(ans), av);
		if (tTd(9, 1))
			sm_dprintf("FOUND %s\n", ans);
		return cp;
	}


	/* No match found */
	s->s_namecanon.nc_errno = errno;
#if NAMED_BIND
	s->s_namecanon.nc_herrno = h_errno;
	if (tTd(9, 1))
		sm_dprintf("FAIL (%d)\n", h_errno);
	switch (h_errno)
	{
	  case TRY_AGAIN:
		if (UseNameServer)
		{
			CurEnv->e_status = "4.4.3";
			message("851 %s: Name server timeout",
				shortenstring(name, 33));
		}
		*statp = EX_TEMPFAIL;
		break;

	  case HOST_NOT_FOUND:
	  case NO_DATA:
		*statp = EX_NOHOST;
		break;

	  case NO_RECOVERY:
		*statp = EX_SOFTWARE;
		break;

	  default:
		*statp = EX_UNAVAILABLE;
		break;
	}
#else /* NAMED_BIND */
	if (tTd(9, 1))
		sm_dprintf("FAIL\n");
	*statp = EX_NOHOST;
#endif /* NAMED_BIND */
	s->s_namecanon.nc_stat = *statp;
	return NULL;
}
/*
**  HOST_MAP_INIT -- initialize host class structures
**
**	Parameters:
**		map -- a pointer to this map.
**		args -- argument string.
**
**	Returns:
**		true.
*/

bool
host_map_init(map, args)
	MAP *map;
	char *args;
{
	register char *p = args;

	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'a':
			map->map_app = ++p;
			break;

		  case 'T':
			map->map_tapp = ++p;
			break;

		  case 'm':
			map->map_mflags |= MF_MATCHONLY;
			break;

		  case 't':
			map->map_mflags |= MF_NODEFER;
			break;

		  case 'S':	/* only for consistency */
			map->map_spacesub = *++p;
			break;

		  case 'D':
			map->map_mflags |= MF_DEFER;
			break;

		  case 'd':
			{
				char *h;

				while (isascii(*++p) && isspace(*p))
					continue;
				h = strchr(p, ' ');
				if (h != NULL)
					*h = '\0';
				map->map_timeout = convtime(p, 's');
				if (h != NULL)
					*h = ' ';
			}
			break;

		  case 'r':
			while (isascii(*++p) && isspace(*p))
				continue;
			map->map_retry = atoi(p);
			break;
		}
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		if (*p != '\0')
			*p++ = '\0';
	}
	if (map->map_app != NULL)
		map->map_app = newstr(map->map_app);
	if (map->map_tapp != NULL)
		map->map_tapp = newstr(map->map_tapp);
	return true;
}

#if NETINET6
/*
**  ANYNET_NTOP -- convert an IPv6 network address to printable form.
**
**	Parameters:
**		s6a -- a pointer to an in6_addr structure.
**		dst -- buffer to store result in
**		dst_len -- size of dst buffer
**
**	Returns:
**		A printable version of that structure.
*/

char *
anynet_ntop(s6a, dst, dst_len)
	struct in6_addr *s6a;
	char *dst;
	size_t dst_len;
{
	register char *ap;

	if (IN6_IS_ADDR_V4MAPPED(s6a))
		ap = (char *) inet_ntop(AF_INET,
					&s6a->s6_addr[IN6ADDRSZ - INADDRSZ],
					dst, dst_len);
	else
	{
		char *d;
		size_t sz;

		/* Save pointer to beginning of string */
		d = dst;

		/* Add IPv6: protocol tag */
		sz = sm_strlcpy(dst, "IPv6:", dst_len);
		if (sz >= dst_len)
			return NULL;
		dst += sz;
		dst_len -= sz;
		if (UseCompressedIPv6Addresses)
			ap = (char *) inet_ntop(AF_INET6, s6a, dst, dst_len);
		else
			ap = sm_inet6_ntop(s6a, dst, dst_len);
		/* Restore pointer to beginning of string */
		if (ap != NULL)
			ap = d;
	}
	return ap;
}

/*
**  ANYNET_PTON -- convert printed form to network address.
**
**	Wrapper for inet_pton() which handles IPv6: labels.
**
**	Parameters:
**		family -- address family
**		src -- string
**		dst -- destination address structure
**
**	Returns:
**		1 if the address was valid
**		0 if the address wasn't parseable
**		-1 if error
*/

int
anynet_pton(family, src, dst)
	int family;
	const char *src;
	void *dst;
{
	if (family == AF_INET6 && sm_strncasecmp(src, "IPv6:", 5) == 0)
		src += 5;
	return inet_pton(family, src, dst);
}
#endif /* NETINET6 */
/*
**  ANYNET_NTOA -- convert a network address to printable form.
**
**	Parameters:
**		sap -- a pointer to a sockaddr structure.
**
**	Returns:
**		A printable version of that sockaddr.
*/

#ifdef USE_SOCK_STREAM

# if NETLINK
#  include <net/if_dl.h>
# endif /* NETLINK */

char *
anynet_ntoa(sap)
	register SOCKADDR *sap;
{
	register char *bp;
	register char *ap;
	int l;
	static char buf[100];

	/* check for null/zero family */
	if (sap == NULL)
		return "NULLADDR";
	if (sap->sa.sa_family == 0)
		return "0";

	switch (sap->sa.sa_family)
	{
# if NETUNIX
	  case AF_UNIX:
		if (sap->sunix.sun_path[0] != '\0')
			(void) sm_snprintf(buf, sizeof(buf), "[UNIX: %.64s]",
					   sap->sunix.sun_path);
		else
			(void) sm_strlcpy(buf, "[UNIX: localhost]", sizeof(buf));
		return buf;
# endif /* NETUNIX */

# if NETINET
	  case AF_INET:
		return (char *) inet_ntoa(sap->sin.sin_addr);
# endif /* NETINET */

# if NETINET6
	  case AF_INET6:
		ap = anynet_ntop(&sap->sin6.sin6_addr, buf, sizeof(buf));
		if (ap != NULL)
			return ap;
		break;
# endif /* NETINET6 */

# if NETLINK
	  case AF_LINK:
		(void) sm_snprintf(buf, sizeof(buf), "[LINK: %s]",
				   link_ntoa((struct sockaddr_dl *) &sap->sa));
		return buf;
# endif /* NETLINK */
	  default:
		/* this case is needed when nothing is #defined */
		/* in order to keep the switch syntactically correct */
		break;
	}

	/* unknown family -- just dump bytes */
	(void) sm_snprintf(buf, sizeof(buf), "Family %d: ", sap->sa.sa_family);
	bp = &buf[strlen(buf)];
	ap = sap->sa.sa_data;
	for (l = sizeof(sap->sa.sa_data); --l >= 0; )
	{
		(void) sm_snprintf(bp, SPACELEFT(buf, bp), "%02x:",
				   *ap++ & 0377);
		bp += 3;
	}
	*--bp = '\0';
	return buf;
}
/*
**  HOSTNAMEBYANYADDR -- return name of host based on address
**
**	Parameters:
**		sap -- SOCKADDR pointer
**
**	Returns:
**		text representation of host name.
**
**	Side Effects:
**		none.
*/

char *
hostnamebyanyaddr(sap)
	register SOCKADDR *sap;
{
	register struct hostent *hp;
# if NAMED_BIND
	int saveretry;
# endif /* NAMED_BIND */
# if NETINET6
	struct in6_addr in6_addr;
# endif /* NETINET6 */

# if NAMED_BIND
	/* shorten name server timeout to avoid higher level timeouts */
	saveretry = _res.retry;
	if (_res.retry * _res.retrans > 20)
		_res.retry = 20 / _res.retrans;
	if (_res.retry == 0)
		_res.retry = 1;
# endif /* NAMED_BIND */

	switch (sap->sa.sa_family)
	{
# if NETINET
	  case AF_INET:
		hp = sm_gethostbyaddr((char *) &sap->sin.sin_addr,
				      INADDRSZ, AF_INET);
		break;
# endif /* NETINET */

# if NETINET6
	  case AF_INET6:
		hp = sm_gethostbyaddr((char *) &sap->sin6.sin6_addr,
				      IN6ADDRSZ, AF_INET6);
		break;
# endif /* NETINET6 */

# if NETISO
	  case AF_ISO:
		hp = sm_gethostbyaddr((char *) &sap->siso.siso_addr,
				      sizeof(sap->siso.siso_addr), AF_ISO);
		break;
# endif /* NETISO */

# if NETUNIX
	  case AF_UNIX:
		hp = NULL;
		break;
# endif /* NETUNIX */

	  default:
		hp = sm_gethostbyaddr(sap->sa.sa_data, sizeof(sap->sa.sa_data),
				      sap->sa.sa_family);
		break;
	}

# if NAMED_BIND
	_res.retry = saveretry;
# endif /* NAMED_BIND */

# if NETINET || NETINET6
	if (hp != NULL && hp->h_name[0] != '['
#  if NETINET6
	    && inet_pton(AF_INET6, hp->h_name, &in6_addr) != 1
#  endif /* NETINET6 */
#  if NETINET
	    && inet_addr(hp->h_name) == INADDR_NONE
#  endif /* NETINET */
	    )
	{
		char *name;

		name = denlstring((char *) hp->h_name, true, true);
#  if NETINET6
		if (name == hp->h_name)
		{
			static char n[MAXNAME + 1];

			/* Copy the string, hp->h_name is about to disappear */
			(void) sm_strlcpy(n, name, sizeof(n));
			name = n;
		}
		freehostent(hp);
#  endif /* NETINET6 */
		return name;
	}
# endif /* NETINET || NETINET6 */

# if NETINET6
	if (hp != NULL)
	{
		freehostent(hp);
		hp = NULL;
	}
# endif /* NETINET6 */

# if NETUNIX
	if (sap->sa.sa_family == AF_UNIX && sap->sunix.sun_path[0] == '\0')
		return "localhost";
# endif /* NETUNIX */
	{
		static char buf[203];

		(void) sm_snprintf(buf, sizeof(buf), "[%.200s]",
				   anynet_ntoa(sap));
		return buf;
	}
}
#endif /* USE_SOCK_STREAM */
