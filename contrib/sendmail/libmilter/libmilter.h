/*
 * Copyright (c) 1999-2003, 2006 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

/*
**  LIBMILTER.H -- include file for mail filter library functions
*/

#ifndef _LIBMILTER_H
# define _LIBMILTER_H	1

#include <sm/gen.h>

#ifdef _DEFINE
# define EXTERN
# define INIT(x)	= x
SM_IDSTR(MilterlId, "@(#)$Id: libmilter.h,v 8.78 2013-11-22 20:51:36 ca Exp $")
#else /* _DEFINE */
# define EXTERN extern
# define INIT(x)
#endif /* _DEFINE */


#include "sm/tailq.h"

#define NOT_SENDMAIL	1
#define _SOCK_ADDR	union bigsockaddr
#include "sendmail.h"

#ifdef SM_ASSERT
#undef SM_ASSERT
#endif
#ifndef SM_ASSERT
#include <assert.h>
#define SM_ASSERT(x) assert(x)
#endif

#include "libmilter/milter.h"

#define MAX_MACROS_ENTRIES	7	/* max size of macro pointer array */

typedef SM_TAILQ_HEAD(, smfi_str)	smfi_hd_T;
typedef struct smfi_str smfi_str_S;

/*
**  Context for one milter session.
**
**  Notes:
**	There is a 1-1 correlation between a sendmail SMTP server process,
**	an SMTP session, and an milter context. Due to the nature of SMTP
**	session handling in sendmail 8, this libmilter implementation deals
**	only with a single SMTP session per MTA - libmilter connection.
**
**	There is no "global" context for libmilter, global variables are
**	just that (they are not "collected" in a context).
**
**  Implementation hint:
**  macros are stored in mac_buf[] as sequence of:
**  macro_name \0 macro_value
**  (just as read from the MTA)
**  mac_ptr is a list of pointers into mac_buf to the beginning of each
**  entry, i.e., macro_name, macro_value, ...
*/

struct smfi_str
{
	sthread_t	ctx_id;		/* thread id */
	socket_t	ctx_sd;		/* socket descriptor */
	int		ctx_dbg;	/* debug level */
	time_t		ctx_timeout;	/* timeout */
	int		ctx_state;	/* state */
	smfiDesc_ptr	ctx_smfi;	/* filter description */

	int		ctx_prot_vers;	/* libmilter protocol version */
	unsigned long	ctx_aflags;	/* milter action flags */

	unsigned long	ctx_pflags;	/* milter protocol flags */

	/*
	**  milter protocol flags that are sent to the MTA;
	**  this is the same as ctx_pflags except for those flags that
	**  are not offered by the MTA but emulated in libmilter.
	*/

	unsigned long	ctx_pflags2mta;

	/*
	**  milter protocol version that is sent to the MTA;
	**  this is the same as ctx_prot_vers unless the
	**  MTA protocol version (ctx_mta_prot_vers) is smaller
	**  but still "acceptable".
	*/

	int		ctx_prot_vers2mta;

	char		**ctx_mac_ptr[MAX_MACROS_ENTRIES];
	char		*ctx_mac_buf[MAX_MACROS_ENTRIES];
	char		*ctx_mac_list[MAX_MACROS_ENTRIES];
	char		*ctx_reply;	/* reply code */
	void		*ctx_privdata;	/* private data */

	int		ctx_mta_prot_vers;	/* MTA protocol version */
	unsigned long	ctx_mta_pflags;	/* MTA protocol flags */
	unsigned long	ctx_mta_aflags;	/* MTA action flags */

#if _FFR_THREAD_MONITOR
	time_t		ctx_start;	/* start time of thread */
	SM_TAILQ_ENTRY(smfi_str)	ctx_mon_link;
#endif /* _FFR_THREAD_MONITOR */

#if _FFR_WORKERS_POOL
	long		ctx_sid;	/* session identifier */
	int		ctx_wstate;	/* state of the session (worker pool) */
	int		ctx_wait;	/* elapsed time waiting for sm cmd */
	SM_TAILQ_ENTRY(smfi_str)	ctx_link;
#endif /* _FFR_WORKERS_POOL */
};

# define ValidSocket(sd)	((sd) >= 0)
# define INVALID_SOCKET		(-1)
# define closesocket		close
# define MI_SOCK_READ(s, b, l)	read(s, b, l)
# define MI_SOCK_READ_FAIL(x)	((x) < 0)
# define MI_SOCK_WRITE(s, b, l)	write(s, b, l)

# define thread_create(ptid,wr,arg) pthread_create(ptid, NULL, wr, arg)
# define sthread_get_id()	pthread_self()

typedef pthread_mutex_t smutex_t;
# define smutex_init(mp)	(pthread_mutex_init(mp, NULL) == 0)
# define smutex_destroy(mp)	(pthread_mutex_destroy(mp) == 0)
# define smutex_lock(mp)	(pthread_mutex_lock(mp) == 0)
# define smutex_unlock(mp)	(pthread_mutex_unlock(mp) == 0)
# define smutex_trylock(mp)	(pthread_mutex_trylock(mp) == 0)

#if _FFR_WORKERS_POOL
/* SM_CONF_POLL shall be defined with _FFR_WORKERS_POOL */
# if !SM_CONF_POLL
#  define SM_CONF_POLL 1
# endif /* SM_CONF_POLL */
#endif /* _FFR_WORKERS_POOL */

typedef pthread_cond_t scond_t;
#define scond_init(cp)			pthread_cond_init(cp, NULL)
#define scond_destroy(cp)		pthread_cond_destroy(cp)
#define scond_wait(cp, mp)		pthread_cond_wait(cp, mp)
#define scond_signal(cp)		pthread_cond_signal(cp)
#define scond_broadcast(cp)		pthread_cond_broadcast(cp)
#define scond_timedwait(cp, mp, to)					\
	do								\
	{								\
		struct timespec timeout;				\
		struct timeval now;					\
		gettimeofday(&now, NULL);				\
		timeout.tv_sec = now.tv_sec + to;			\
		timeout.tv_nsec = now.tv_usec / 1000;			\
		r = pthread_cond_timedwait(cp,mp,&timeout);		\
		if (r != 0 && r != ETIMEDOUT)				\
			smi_log(SMI_LOG_ERR,				\
				"pthread_cond_timedwait error %d", r);	\
	} while (0)


#if SM_CONF_POLL

# include <poll.h>
# define MI_POLLSELECT  "poll"

# define MI_POLL_RD_FLAGS (POLLIN | POLLPRI)
# define MI_POLL_WR_FLAGS (POLLOUT)
# define MI_MS(timeout)	(((timeout)->tv_sec * 1000) + (timeout)->tv_usec)

# define FD_RD_VAR(rds, excs) struct pollfd rds
# define FD_WR_VAR(wrs) struct pollfd wrs

# define FD_RD_INIT(sd, rds, excs)			\
		(rds).fd = (sd);			\
		(rds).events = MI_POLL_RD_FLAGS;	\
		(rds).revents = 0

# define FD_WR_INIT(sd, wrs)				\
		(wrs).fd = (sd);			\
		(wrs).events = MI_POLL_WR_FLAGS;	\
		(wrs).revents = 0

# define FD_IS_RD_EXC(sd, rds, excs)	\
		(((rds).revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)

# define FD_IS_WR_RDY(sd, wrs)		\
		(((wrs).revents & MI_POLL_WR_FLAGS) != 0)

# define FD_IS_RD_RDY(sd, rds, excs)			\
		(((rds).revents & MI_POLL_RD_FLAGS) != 0)

# define FD_WR_READY(sd, excs, timeout)	\
		poll(&(wrs), 1, MI_MS(timeout))

# define FD_RD_READY(sd, rds, excs, timeout)	\
		poll(&(rds), 1, MI_MS(timeout))

#else /* SM_CONF_POLL */

# include <sm/fdset.h>
# define MI_POLLSELECT  "select"

# define FD_RD_VAR(rds, excs) fd_set rds, excs
# define FD_WR_VAR(wrs) fd_set wrs

# define FD_RD_INIT(sd, rds, excs)			\
		FD_ZERO(&(rds));			\
		FD_SET((unsigned int) (sd), &(rds));	\
		FD_ZERO(&(excs));			\
		FD_SET((unsigned int) (sd), &(excs))

# define FD_WR_INIT(sd, wrs)			\
		FD_ZERO(&(wrs));			\
		FD_SET((unsigned int) (sd), &(wrs))

# define FD_IS_RD_EXC(sd, rds, excs) FD_ISSET(sd, &(excs))
# define FD_IS_WR_RDY(sd, wrs) FD_ISSET((sd), &(wrs))
# define FD_IS_RD_RDY(sd, rds, excs) FD_ISSET((sd), &(rds))

# define FD_WR_READY(sd, wrs, timeout)	\
		select((sd) + 1, NULL, &(wrs), NULL, (timeout))
# define FD_RD_READY(sd, rds, excs, timeout)	\
		select((sd) + 1, &(rds), NULL, &(excs), (timeout))

#endif /* SM_CONF_POLL */

#include <sys/time.h>

/* some defaults */
#define MI_TIMEOUT	7210		/* default timeout for read/write */
#define MI_CHK_TIME	5		/* checking whether to terminate */

#ifndef MI_SOMAXCONN
# if SOMAXCONN > 20
#  define MI_SOMAXCONN	SOMAXCONN
# else /* SOMAXCONN */
#  define MI_SOMAXCONN	20
# endif /* SOMAXCONN */
#endif /* ! MI_SOMAXCONN */

/* maximum number of repeated failures in mi_listener() */
#define MAX_FAILS_M	16	/* malloc() */
#define MAX_FAILS_T	16	/* thread creation */
#define MAX_FAILS_A	16	/* accept() */
#define MAX_FAILS_S	16	/* select() */

/* internal "commands", i.e., error codes */
#define SMFIC_TIMEOUT	((char) 1)	/* timeout */
#define SMFIC_SELECT	((char) 2)	/* select error */
#define SMFIC_MALLOC	((char) 3)	/* malloc error */
#define SMFIC_RECVERR	((char) 4)	/* recv() error */
#define SMFIC_EOF	((char) 5)	/* eof */
#define SMFIC_UNKNERR	((char) 6)	/* unknown error */
#define SMFIC_TOOBIG	((char) 7)	/* body chunk too big */
#define SMFIC_VALIDCMD	' '		/* first valid command */

/* hack */
#define smi_log		syslog
#define sm_dprintf	(void) printf
#define milter_ret	int
#define SMI_LOG_ERR	LOG_ERR
#define SMI_LOG_FATAL	LOG_ERR
#define SMI_LOG_WARN	LOG_WARNING
#define SMI_LOG_INFO	LOG_INFO
#define SMI_LOG_DEBUG	LOG_DEBUG

/* stop? */
#define MILTER_CONT	0
#define MILTER_STOP	1
#define MILTER_ABRT	2

/* functions */
extern int	mi_handle_session __P((SMFICTX_PTR));
extern int	mi_engine __P((SMFICTX_PTR));
extern int	mi_listener __P((char *, int, smfiDesc_ptr, time_t, int));
extern void	mi_clr_macros __P((SMFICTX_PTR, int));
extern void	mi_clr_ctx __P((SMFICTX_PTR));
extern int	mi_stop __P((void));
extern int	mi_control_startup __P((char *));
extern void	mi_stop_milters __P((int));
extern void	mi_clean_signals __P((void));
extern struct hostent *mi_gethostbyname __P((char *, int));
extern int	mi_inet_pton __P((int, const char *, void *));
extern void	mi_closener __P((void));
extern int	mi_opensocket __P((char *, int, int, bool, smfiDesc_ptr));

/* communication functions */
extern char	*mi_rd_cmd __P((socket_t, struct timeval *, char *, size_t *, char *));
extern int	mi_wr_cmd __P((socket_t, struct timeval *, int, char *, size_t));
extern bool	mi_sendok __P((SMFICTX_PTR, int));


#if _FFR_THREAD_MONITOR
extern bool Monitor;

#define MI_MONITOR_INIT()	mi_monitor_init()
#define MI_MONITOR_BEGIN(ctx, cmd)			\
	do						\
	{						\
		if (Monitor)				\
			mi_monitor_work_begin(ctx, cmd);\
	} while (0)

#define MI_MONITOR_END(ctx, cmd)			\
	do						\
	{						\
		if (Monitor)				\
			mi_monitor_work_end(ctx, cmd);	\
	} while (0)

int mi_monitor_init __P((void));
int mi_monitor_work_begin __P((SMFICTX_PTR, int));
int mi_monitor_work_end __P((SMFICTX_PTR, int));

#else /* _FFR_THREAD_MONITOR */
#define MI_MONITOR_INIT()	MI_SUCCESS
#define MI_MONITOR_BEGIN(ctx, cmd)
#define MI_MONITOR_END(ctx, cmd)
#endif /* _FFR_THREAD_MONITOR */

#if _FFR_WORKERS_POOL
extern int mi_pool_manager_init __P((void));
extern int mi_pool_controller_init __P((void));
extern int mi_start_session __P((SMFICTX_PTR));
#endif /* _FFR_WORKERS_POOL */

#endif /* ! _LIBMILTER_H */
