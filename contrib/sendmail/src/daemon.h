/*
 * Copyright (c) 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: daemon.h,v 8.4 2013-11-22 20:51:55 ca Exp $
 */

#ifndef DAEMON_H
#define DAEMON_H 1

#if DAEMON_C
# define EXTERN 
#else
# define EXTERN extern
#endif

/* structure to describe a daemon or a client */
struct daemon
{
	int		d_socket;	/* fd for socket */
	SOCKADDR	d_addr;		/* socket for incoming */
	unsigned short	d_port;		/* port number */
	int		d_listenqueue;	/* size of listen queue */
	int		d_tcprcvbufsize;	/* size of TCP receive buffer */
	int		d_tcpsndbufsize;	/* size of TCP send buffer */
	time_t		d_refuse_connections_until;
	bool		d_firsttime;
	int		d_socksize;
	BITMAP256	d_flags;	/* flags; see sendmail.h */
	char		*d_mflags;	/* flags for use in macro */
	char		*d_name;	/* user-supplied name */

	int		d_dm;		/* DeliveryMode */
	int		d_refuseLA;
	int		d_queueLA;
	int		d_delayLA;
	int		d_maxchildren;

#if MILTER
	char		*d_inputfilterlist;
	struct milter	*d_inputfilters[MAXFILTERS];
#endif /* MILTER */
#if _FFR_SS_PER_DAEMON
	int		d_supersafe;
#endif /* _FFR_SS_PER_DAEMON */
};

typedef struct daemon DAEMON_T;

EXTERN DAEMON_T	Daemons[MAXDAEMONS];

#define DPO_NOTSET	(-1)	/* daemon option (int) not set */
/* see also sendmail.h: SuperSafe values */

extern bool refuseconnections __P((ENVELOPE *, int, bool));

#undef EXTERN
#endif /* ! DAEMON_H */
