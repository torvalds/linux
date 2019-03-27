/*
 * Copyright (c) 1998-2009, 2011, 2012, 2014 Proofpoint, Inc. and its suppliers.
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
#include <sm/sem.h>

SM_RCSID("@(#)$Id: queue.c,v 8.1000 2013-11-22 20:51:56 ca Exp $")

#include <dirent.h>

# define RELEASE_QUEUE	(void) 0
# define ST_INODE(st)	(st).st_ino

#  define sm_file_exists(errno) ((errno) == EEXIST)

# if HASFLOCK && defined(O_EXLOCK)
#   define SM_OPEN_EXLOCK 1
#   define TF_OPEN_FLAGS (O_CREAT|O_WRONLY|O_EXCL|O_EXLOCK)
# else /* HASFLOCK && defined(O_EXLOCK) */
#  define TF_OPEN_FLAGS (O_CREAT|O_WRONLY|O_EXCL)
# endif /* HASFLOCK && defined(O_EXLOCK) */

#ifndef SM_OPEN_EXLOCK
# define SM_OPEN_EXLOCK 0
#endif /* ! SM_OPEN_EXLOCK */

/*
**  Historical notes:
**	QF_VERSION == 4 was sendmail 8.10/8.11 without _FFR_QUEUEDELAY
**	QF_VERSION == 5 was sendmail 8.10/8.11 with    _FFR_QUEUEDELAY
**	QF_VERSION == 6 was sendmail 8.12      without _FFR_QUEUEDELAY
**	QF_VERSION == 7 was sendmail 8.12      with    _FFR_QUEUEDELAY
**	QF_VERSION == 8 is  sendmail 8.13
*/

#define QF_VERSION	8	/* version number of this queue format */

static char	queue_letter __P((ENVELOPE *, int));
static bool	quarantine_queue_item __P((int, int, ENVELOPE *, char *));

/* Naming convention: qgrp: index of queue group, qg: QUEUEGROUP */

/*
**  Work queue.
*/

struct work
{
	char		*w_name;	/* name of control file */
	char		*w_host;	/* name of recipient host */
	bool		w_lock;		/* is message locked? */
	bool		w_tooyoung;	/* is it too young to run? */
	long		w_pri;		/* priority of message, see below */
	time_t		w_ctime;	/* creation time */
	time_t		w_mtime;	/* modification time */
	int		w_qgrp;		/* queue group located in */
	int		w_qdir;		/* queue directory located in */
	struct work	*w_next;	/* next in queue */
};

typedef struct work	WORK;

static WORK	*WorkQ;		/* queue of things to be done */
static int	NumWorkGroups;	/* number of work groups */
static time_t	Current_LA_time = 0;

/* Get new load average every 30 seconds. */
#define GET_NEW_LA_TIME	30

#define SM_GET_LA(now)	\
	do							\
	{							\
		now = curtime();				\
		if (Current_LA_time < now - GET_NEW_LA_TIME)	\
		{						\
			sm_getla();				\
			Current_LA_time = now;			\
		}						\
	} while (0)

/*
**  DoQueueRun indicates that a queue run is needed.
**	Notice: DoQueueRun is modified in a signal handler!
*/

static bool	volatile DoQueueRun; /* non-interrupt time queue run needed */

/*
**  Work group definition structure.
**	Each work group contains one or more queue groups. This is done
**	to manage the number of queue group runners active at the same time
**	to be within the constraints of MaxQueueChildren (if it is set).
**	The number of queue groups that can be run on the next work run
**	is kept track of. The queue groups are run in a round robin.
*/

struct workgrp
{
	int		wg_numqgrp;	/* number of queue groups in work grp */
	int		wg_runners;	/* total runners */
	int		wg_curqgrp;	/* current queue group */
	QUEUEGRP	**wg_qgs;	/* array of queue groups */
	int		wg_maxact;	/* max # of active runners */
	time_t		wg_lowqintvl;	/* lowest queue interval */
	int		wg_restart;	/* needs restarting? */
	int		wg_restartcnt;	/* count of times restarted */
};

typedef struct workgrp WORKGRP;

static WORKGRP	volatile WorkGrp[MAXWORKGROUPS + 1];	/* work groups */

#if SM_HEAP_CHECK
static SM_DEBUG_T DebugLeakQ = SM_DEBUG_INITIALIZER("leak_q",
	"@(#)$Debug: leak_q - trace memory leaks during queue processing $");
#endif /* SM_HEAP_CHECK */

static void	grow_wlist __P((int, int));
static int	multiqueue_cache __P((char *, int, QUEUEGRP *, int, unsigned int *));
static int	gatherq __P((int, int, bool, bool *, bool *, int *));
static int	sortq __P((int));
static void	printctladdr __P((ADDRESS *, SM_FILE_T *));
static bool	readqf __P((ENVELOPE *, bool));
static void	restart_work_group __P((int));
static void	runner_work __P((ENVELOPE *, int, bool, int, int));
static void	schedule_queue_runs __P((bool, int, bool));
static char	*strrev __P((char *));
static ADDRESS	*setctluser __P((char *, int, ENVELOPE *));
#if _FFR_RHS
static int	sm_strshufflecmp __P((char *, char *));
static void	init_shuffle_alphabet __P(());
#endif /* _FFR_RHS */

/*
**  Note: workcmpf?() don't use a prototype because it will cause a conflict
**  with the qsort() call (which expects something like
**  int (*compar)(const void *, const void *), not (WORK *, WORK *))
*/

static int	workcmpf0();
static int	workcmpf1();
static int	workcmpf2();
static int	workcmpf3();
static int	workcmpf4();
static int	randi = 3;	/* index for workcmpf5() */
static int	workcmpf5();
static int	workcmpf6();
#if _FFR_RHS
static int	workcmpf7();
#endif /* _FFR_RHS */

#if RANDOMSHIFT
# define get_rand_mod(m)	((get_random() >> RANDOMSHIFT) % (m))
#else /* RANDOMSHIFT */
# define get_rand_mod(m)	(get_random() % (m))
#endif /* RANDOMSHIFT */

/*
**  File system definition.
**	Used to keep track of how much free space is available
**	on a file system in which one or more queue directories reside.
*/

typedef struct filesys_shared	FILESYS;

struct filesys_shared
{
	dev_t	fs_dev;		/* unique device id */
	long	fs_avail;	/* number of free blocks available */
	long	fs_blksize;	/* block size, in bytes */
};

/* probably kept in shared memory */
static FILESYS	FileSys[MAXFILESYS];	/* queue file systems */
static const char *FSPath[MAXFILESYS];	/* pathnames for file systems */

#if SM_CONF_SHM

/*
**  Shared memory data
**
**  Current layout:
**	size -- size of shared memory segment
**	pid -- pid of owner, should be a unique id to avoid misinterpretations
**		by other processes.
**	tag -- should be a unique id to avoid misinterpretations by others.
**		idea: hash over configuration data that will be stored here.
**	NumFileSys -- number of file systems.
**	FileSys -- (array of) structure for used file systems.
**	RSATmpCnt -- counter for number of uses of ephemeral RSA key.
**	QShm -- (array of) structure for information about queue directories.
*/

/*
**  Queue data in shared memory
*/

typedef struct queue_shared	QUEUE_SHM_T;

struct queue_shared
{
	int	qs_entries;	/* number of entries */
	/* XXX more to follow? */
};

static void	*Pshm;		/* pointer to shared memory */
static FILESYS	*PtrFileSys;	/* pointer to queue file system array */
int		ShmId = SM_SHM_NO_ID;	/* shared memory id */
static QUEUE_SHM_T	*QShm;		/* pointer to shared queue data */
static size_t shms;

# define SHM_OFF_PID(p)	(((char *) (p)) + sizeof(int))
# define SHM_OFF_TAG(p)	(((char *) (p)) + sizeof(pid_t) + sizeof(int))
# define SHM_OFF_HEAD	(sizeof(pid_t) + sizeof(int) * 2)

/* how to access FileSys */
# define FILE_SYS(i)	(PtrFileSys[i])

/* first entry is a tag, for now just the size */
# define OFF_FILE_SYS(p)	(((char *) (p)) + SHM_OFF_HEAD)

/* offset for PNumFileSys */
# define OFF_NUM_FILE_SYS(p)	(((char *) (p)) + SHM_OFF_HEAD + sizeof(FileSys))

/* offset for PRSATmpCnt */
# define OFF_RSA_TMP_CNT(p) (((char *) (p)) + SHM_OFF_HEAD + sizeof(FileSys) + sizeof(int))
int	*PRSATmpCnt;

/* offset for queue_shm */
# define OFF_QUEUE_SHM(p) (((char *) (p)) + SHM_OFF_HEAD + sizeof(FileSys) + sizeof(int) * 2)

# define QSHM_ENTRIES(i)	QShm[i].qs_entries

/* basic size of shared memory segment */
# define SM_T_SIZE	(SHM_OFF_HEAD + sizeof(FileSys) + sizeof(int) * 2)

static unsigned int	hash_q __P((char *, unsigned int));

/*
**  HASH_Q -- simple hash function
**
**	Parameters:
**		p -- string to hash.
**		h -- hash start value (from previous run).
**
**	Returns:
**		hash value.
*/

static unsigned int
hash_q(p, h)
	char *p;
	unsigned int h;
{
	int c, d;

	while (*p != '\0')
	{
		d = *p++;
		c = d;
		c ^= c<<6;
		h += (c<<11) ^ (c>>1);
		h ^= (d<<14) + (d<<7) + (d<<4) + d;
	}
	return h;
}


#else /* SM_CONF_SHM */
# define FILE_SYS(i)	FileSys[i]
#endif /* SM_CONF_SHM */

/* access to the various components of file system data */
#define FILE_SYS_NAME(i)	FSPath[i]
#define FILE_SYS_AVAIL(i)	FILE_SYS(i).fs_avail
#define FILE_SYS_BLKSIZE(i)	FILE_SYS(i).fs_blksize
#define FILE_SYS_DEV(i)	FILE_SYS(i).fs_dev


/*
**  Current qf file field assignments:
**
**	A	AUTH= parameter
**	B	body type
**	C	controlling user
**	D	data file name
**	d	data file directory name (added in 8.12)
**	E	error recipient
**	F	flag bits
**	G	free
**	H	header
**	I	data file's inode number
**	K	time of last delivery attempt
**	L	Solaris Content-Length: header (obsolete)
**	M	message
**	N	number of delivery attempts
**	P	message priority
**	q	quarantine reason
**	Q	original recipient (ORCPT=)
**	r	final recipient (Final-Recipient: DSN field)
**	R	recipient
**	S	sender
**	T	init time
**	V	queue file version
**	X	free (was: character set if _FFR_SAVE_CHARSET)
**	Y	free
**	Z	original envelope id from ESMTP
**	!	deliver by (added in 8.12)
**	$	define macro
**	.	terminate file
*/

/*
**  QUEUEUP -- queue a message up for future transmission.
**
**	Parameters:
**		e -- the envelope to queue up.
**		announce -- if true, tell when you are queueing up.
**		msync -- if true, then fsync() if SuperSafe interactive mode.
**
**	Returns:
**		none.
**
**	Side Effects:
**		The current request is saved in a control file.
**		The queue file is left locked.
*/

void
queueup(e, announce, msync)
	register ENVELOPE *e;
	bool announce;
	bool msync;
{
	register SM_FILE_T *tfp;
	register HDR *h;
	register ADDRESS *q;
	int tfd = -1;
	int i;
	bool newid;
	register char *p;
	MAILER nullmailer;
	MCI mcibuf;
	char qf[MAXPATHLEN];
	char tf[MAXPATHLEN];
	char df[MAXPATHLEN];
	char buf[MAXLINE];

	/*
	**  Create control file.
	*/

#define OPEN_TF	do							\
		{							\
			MODE_T oldumask = 0;				\
									\
			if (bitset(S_IWGRP, QueueFileMode))		\
				oldumask = umask(002);			\
			tfd = open(tf, TF_OPEN_FLAGS, QueueFileMode);	\
			if (bitset(S_IWGRP, QueueFileMode))		\
				(void) umask(oldumask);			\
		} while (0)


	newid = (e->e_id == NULL) || !bitset(EF_INQUEUE, e->e_flags);
	(void) sm_strlcpy(tf, queuename(e, NEWQFL_LETTER), sizeof(tf));
	tfp = e->e_lockfp;
	if (tfp == NULL && newid)
	{
		/*
		**  open qf file directly: this will give an error if the file
		**  already exists and hence prevent problems if a queue-id
		**  is reused (e.g., because the clock is set back).
		*/

		(void) sm_strlcpy(tf, queuename(e, ANYQFL_LETTER), sizeof(tf));
		OPEN_TF;
		if (tfd < 0 ||
#if !SM_OPEN_EXLOCK
		    !lockfile(tfd, tf, NULL, LOCK_EX|LOCK_NB) ||
#endif /* !SM_OPEN_EXLOCK */
		    (tfp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
					 (void *) &tfd, SM_IO_WRONLY,
					 NULL)) == NULL)
		{
			int save_errno = errno;

			printopenfds(true);
			errno = save_errno;
			syserr("!queueup: cannot create queue file %s, euid=%ld, fd=%d, fp=%p",
				tf, (long) geteuid(), tfd, tfp);
			/* NOTREACHED */
		}
		e->e_lockfp = tfp;
		upd_qs(e, 1, 0, "queueup");
	}

	/* if newid, write the queue file directly (instead of temp file) */
	if (!newid)
	{
		/* get a locked tf file */
		for (i = 0; i < 128; i++)
		{
			if (tfd < 0)
			{
				OPEN_TF;
				if (tfd < 0)
				{
					if (errno != EEXIST)
						break;
					if (LogLevel > 0 && (i % 32) == 0)
						sm_syslog(LOG_ALERT, e->e_id,
							  "queueup: cannot create %s, euid=%ld: %s",
							  tf, (long) geteuid(),
							  sm_errstring(errno));
				}
#if SM_OPEN_EXLOCK
				else
					break;
#endif /* SM_OPEN_EXLOCK */
			}
			if (tfd >= 0)
			{
#if SM_OPEN_EXLOCK
				/* file is locked by open() */
				break;
#else /* SM_OPEN_EXLOCK */
				if (lockfile(tfd, tf, NULL, LOCK_EX|LOCK_NB))
					break;
				else
#endif /* SM_OPEN_EXLOCK */
				if (LogLevel > 0 && (i % 32) == 0)
					sm_syslog(LOG_ALERT, e->e_id,
						  "queueup: cannot lock %s: %s",
						  tf, sm_errstring(errno));
				if ((i % 32) == 31)
				{
					(void) close(tfd);
					tfd = -1;
				}
			}

			if ((i % 32) == 31)
			{
				/* save the old temp file away */
				(void) rename(tf, queuename(e, TEMPQF_LETTER));
			}
			else
				(void) sleep(i % 32);
		}
		if (tfd < 0 || (tfp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
						 (void *) &tfd, SM_IO_WRONLY_B,
						 NULL)) == NULL)
		{
			int save_errno = errno;

			printopenfds(true);
			errno = save_errno;
			syserr("!queueup: cannot create queue temp file %s, uid=%ld",
				tf, (long) geteuid());
		}
	}

	if (tTd(40, 1))
		sm_dprintf("\n>>>>> queueing %s/%s%s >>>>>\n",
			   qid_printqueue(e->e_qgrp, e->e_qdir),
			   queuename(e, ANYQFL_LETTER),
			   newid ? " (new id)" : "");
	if (tTd(40, 3))
	{
		sm_dprintf("  e_flags=");
		printenvflags(e);
	}
	if (tTd(40, 32))
	{
		sm_dprintf("  sendq=");
		printaddr(sm_debug_file(), e->e_sendqueue, true);
	}
	if (tTd(40, 9))
	{
		sm_dprintf("  tfp=");
		dumpfd(sm_io_getinfo(tfp, SM_IO_WHAT_FD, NULL), true, false);
		sm_dprintf("  lockfp=");
		if (e->e_lockfp == NULL)
			sm_dprintf("NULL\n");
		else
			dumpfd(sm_io_getinfo(e->e_lockfp, SM_IO_WHAT_FD, NULL),
			       true, false);
	}

	/*
	**  If there is no data file yet, create one.
	*/

	(void) sm_strlcpy(df, queuename(e, DATAFL_LETTER), sizeof(df));
	if (bitset(EF_HAS_DF, e->e_flags))
	{
		if (e->e_dfp != NULL &&
		    SuperSafe != SAFE_REALLY &&
		    SuperSafe != SAFE_REALLY_POSTMILTER &&
		    sm_io_setinfo(e->e_dfp, SM_BF_COMMIT, NULL) < 0 &&
		    errno != EINVAL)
		{
			syserr("!queueup: cannot commit data file %s, uid=%ld",
			       queuename(e, DATAFL_LETTER), (long) geteuid());
		}
		if (e->e_dfp != NULL &&
		    SuperSafe == SAFE_INTERACTIVE && msync)
		{
			if (tTd(40,32))
				sm_syslog(LOG_INFO, e->e_id,
					  "queueup: fsync(e->e_dfp)");

			if (fsync(sm_io_getinfo(e->e_dfp, SM_IO_WHAT_FD,
						NULL)) < 0)
			{
				if (newid)
					syserr("!552 Error writing data file %s",
					       df);
				else
					syserr("!452 Error writing data file %s",
					       df);
			}
		}
	}
	else
	{
		int dfd;
		MODE_T oldumask = 0;
		register SM_FILE_T *dfp = NULL;
		struct stat stbuf;

		if (e->e_dfp != NULL &&
		    sm_io_getinfo(e->e_dfp, SM_IO_WHAT_ISTYPE, BF_FILE_TYPE))
			syserr("committing over bf file");

		if (bitset(S_IWGRP, QueueFileMode))
			oldumask = umask(002);
		dfd = open(df, O_WRONLY|O_CREAT|O_TRUNC|QF_O_EXTRA,
			   QueueFileMode);
		if (bitset(S_IWGRP, QueueFileMode))
			(void) umask(oldumask);
		if (dfd < 0 || (dfp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
						 (void *) &dfd, SM_IO_WRONLY_B,
						 NULL)) == NULL)
			syserr("!queueup: cannot create data temp file %s, uid=%ld",
				df, (long) geteuid());
		if (fstat(dfd, &stbuf) < 0)
			e->e_dfino = -1;
		else
		{
			e->e_dfdev = stbuf.st_dev;
			e->e_dfino = ST_INODE(stbuf);
		}
		e->e_flags |= EF_HAS_DF;
		memset(&mcibuf, '\0', sizeof(mcibuf));
		mcibuf.mci_out = dfp;
		mcibuf.mci_mailer = FileMailer;
		(*e->e_putbody)(&mcibuf, e, NULL);

		if (SuperSafe == SAFE_REALLY ||
		    SuperSafe == SAFE_REALLY_POSTMILTER ||
		    (SuperSafe == SAFE_INTERACTIVE && msync))
		{
			if (tTd(40,32))
				sm_syslog(LOG_INFO, e->e_id,
					  "queueup: fsync(dfp)");

			if (fsync(sm_io_getinfo(dfp, SM_IO_WHAT_FD, NULL)) < 0)
			{
				if (newid)
					syserr("!552 Error writing data file %s",
					       df);
				else
					syserr("!452 Error writing data file %s",
					       df);
			}
		}

		if (sm_io_close(dfp, SM_TIME_DEFAULT) < 0)
			syserr("!queueup: cannot save data temp file %s, uid=%ld",
				df, (long) geteuid());
		e->e_putbody = putbody;
	}

	/*
	**  Output future work requests.
	**	Priority and creation time should be first, since
	**	they are required by gatherq.
	*/

	/* output queue version number (must be first!) */
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "V%d\n", QF_VERSION);

	/* output creation time */
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "T%ld\n", (long) e->e_ctime);

	/* output last delivery time */
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "K%ld\n", (long) e->e_dtime);

	/* output number of delivery attempts */
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "N%d\n", e->e_ntries);

	/* output message priority */
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "P%ld\n", e->e_msgpriority);

	/*
	**  If data file is in a different directory than the queue file,
	**  output a "d" record naming the directory of the data file.
	*/

	if (e->e_dfqgrp != e->e_qgrp)
	{
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "d%s\n",
			Queue[e->e_dfqgrp]->qg_qpaths[e->e_dfqdir].qp_name);
	}

	/* output inode number of data file */
	if (e->e_dfino != -1)
	{
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "I%ld/%ld/%llu\n",
				     (long) major(e->e_dfdev),
				     (long) minor(e->e_dfdev),
				     (ULONGLONG_T) e->e_dfino);
	}

	/* output body type */
	if (e->e_bodytype != NULL)
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "B%s\n",
				     denlstring(e->e_bodytype, true, false));

	/* quarantine reason */
	if (e->e_quarmsg != NULL)
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "q%s\n",
				     denlstring(e->e_quarmsg, true, false));

	/* message from envelope, if it exists */
	if (e->e_message != NULL)
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "M%s\n",
				     denlstring(e->e_message, true, false));

	/* send various flag bits through */
	p = buf;
	if (bitset(EF_WARNING, e->e_flags))
		*p++ = 'w';
	if (bitset(EF_RESPONSE, e->e_flags))
		*p++ = 'r';
	if (bitset(EF_HAS8BIT, e->e_flags))
		*p++ = '8';
	if (bitset(EF_DELETE_BCC, e->e_flags))
		*p++ = 'b';
	if (bitset(EF_RET_PARAM, e->e_flags))
		*p++ = 'd';
	if (bitset(EF_NO_BODY_RETN, e->e_flags))
		*p++ = 'n';
	if (bitset(EF_SPLIT, e->e_flags))
		*p++ = 's';
	*p++ = '\0';
	if (buf[0] != '\0')
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "F%s\n", buf);

	/* save $={persistentMacros} macro values */
	queueup_macros(macid("{persistentMacros}"), tfp, e);

	/* output name of sender */
	if (bitnset(M_UDBENVELOPE, e->e_from.q_mailer->m_flags))
		p = e->e_sender;
	else
		p = e->e_from.q_paddr;
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "S%s\n",
			     denlstring(p, true, false));

	/* output ESMTP-supplied "original" information */
	if (e->e_envid != NULL)
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "Z%s\n",
				     denlstring(e->e_envid, true, false));

	/* output AUTH= parameter */
	if (e->e_auth_param != NULL)
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "A%s\n",
				     denlstring(e->e_auth_param, true, false));
	if (e->e_dlvr_flag != 0)
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "!%c %ld\n",
				     (char) e->e_dlvr_flag, e->e_deliver_by);

	/* output list of recipient addresses */
	printctladdr(NULL, NULL);
	for (q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (!QS_IS_UNDELIVERED(q->q_state))
			continue;

		/* message for this recipient, if it exists */
		if (q->q_message != NULL)
			(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "M%s\n",
					     denlstring(q->q_message, true,
							false));

		printctladdr(q, tfp);
		if (q->q_orcpt != NULL)
			(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "Q%s\n",
					     denlstring(q->q_orcpt, true,
							false));
		if (q->q_finalrcpt != NULL)
			(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "r%s\n",
					     denlstring(q->q_finalrcpt, true,
							false));
		(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'R');
		if (bitset(QPRIMARY, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'P');
		if (bitset(QHASNOTIFY, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'N');
		if (bitset(QPINGONSUCCESS, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'S');
		if (bitset(QPINGONFAILURE, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'F');
		if (bitset(QPINGONDELAY, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'D');
		if (bitset(QINTBCC, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'B');
		if (q->q_alias != NULL &&
		    bitset(QALIAS, q->q_alias->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, 'A');

		/* _FFR_RCPTFLAGS */
		if (bitset(QDYNMAILER, q->q_flags))
			(void) sm_io_putc(tfp, SM_TIME_DEFAULT, QDYNMAILFLG);
		(void) sm_io_putc(tfp, SM_TIME_DEFAULT, ':');
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "%s\n",
				     denlstring(q->q_paddr, true, false));
		if (announce)
		{
			char *tag = "queued";

			if (e->e_quarmsg != NULL)
				tag = "quarantined";

			e->e_to = q->q_paddr;
			message("%s", tag);
			if (LogLevel > 8)
				logdelivery(q->q_mailer, NULL, q->q_status,
					    tag, NULL, (time_t) 0, e, q, EX_OK);
			e->e_to = NULL;
		}
		if (tTd(40, 1))
		{
			sm_dprintf("queueing ");
			printaddr(sm_debug_file(), q, false);
		}
	}

	/*
	**  Output headers for this message.
	**	Expand macros completely here.  Queue run will deal with
	**	everything as absolute headers.
	**		All headers that must be relative to the recipient
	**		can be cracked later.
	**	We set up a "null mailer" -- i.e., a mailer that will have
	**	no effect on the addresses as they are output.
	*/

	memset((char *) &nullmailer, '\0', sizeof(nullmailer));
	nullmailer.m_re_rwset = nullmailer.m_rh_rwset =
			nullmailer.m_se_rwset = nullmailer.m_sh_rwset = -1;
	nullmailer.m_eol = "\n";
	memset(&mcibuf, '\0', sizeof(mcibuf));
	mcibuf.mci_mailer = &nullmailer;
	mcibuf.mci_out = tfp;

	macdefine(&e->e_macro, A_PERM, 'g', "\201f");
	for (h = e->e_header; h != NULL; h = h->h_link)
	{
		if (h->h_value == NULL)
			continue;

		/* don't output resent headers on non-resent messages */
		if (bitset(H_RESENT, h->h_flags) &&
		    !bitset(EF_RESENT, e->e_flags))
			continue;

		/* expand macros; if null, don't output header at all */
		if (bitset(H_DEFAULT, h->h_flags))
		{
			(void) expand(h->h_value, buf, sizeof(buf), e);
			if (buf[0] == '\0')
				continue;
			if (buf[0] == ' ' && buf[1] == '\0')
				continue;
		}

		/* output this header */
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "H?");

		/* output conditional macro if present */
		if (h->h_macro != '\0')
		{
			if (bitset(0200, h->h_macro))
				(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT,
						     "${%s}",
						      macname(bitidx(h->h_macro)));
			else
				(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT,
						     "$%c", h->h_macro);
		}
		else if (!bitzerop(h->h_mflags) &&
			 bitset(H_CHECK|H_ACHECK, h->h_flags))
		{
			int j;

			/* if conditional, output the set of conditions */
			for (j = '\0'; j <= '\177'; j++)
				if (bitnset(j, h->h_mflags))
					(void) sm_io_putc(tfp, SM_TIME_DEFAULT,
							  j);
		}
		(void) sm_io_putc(tfp, SM_TIME_DEFAULT, '?');

		/* output the header: expand macros, convert addresses */
		if (bitset(H_DEFAULT, h->h_flags) &&
		    !bitset(H_BINDLATE, h->h_flags))
		{
			(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "%s:%s\n",
					     h->h_field,
					     denlstring(buf, false, true));
		}
		else if (bitset(H_FROM|H_RCPT, h->h_flags) &&
			 !bitset(H_BINDLATE, h->h_flags))
		{
			bool oldstyle = bitset(EF_OLDSTYLE, e->e_flags);
			SM_FILE_T *savetrace = TrafficLogFile;

			TrafficLogFile = NULL;

			if (bitset(H_FROM, h->h_flags))
				oldstyle = false;
			commaize(h, h->h_value, oldstyle, &mcibuf, e,
				 PXLF_HEADER);

			TrafficLogFile = savetrace;
		}
		else
		{
			(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "%s:%s\n",
					     h->h_field,
					     denlstring(h->h_value, false,
							true));
		}
	}

	/*
	**  Clean up.
	**
	**	Write a terminator record -- this is to prevent
	**	scurrilous crackers from appending any data.
	*/

	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, ".\n");

	if (sm_io_flush(tfp, SM_TIME_DEFAULT) != 0 ||
	    ((SuperSafe == SAFE_REALLY ||
	      SuperSafe == SAFE_REALLY_POSTMILTER ||
	      (SuperSafe == SAFE_INTERACTIVE && msync)) &&
	     fsync(sm_io_getinfo(tfp, SM_IO_WHAT_FD, NULL)) < 0) ||
	    sm_io_error(tfp))
	{
		if (newid)
			syserr("!552 Error writing control file %s", tf);
		else
			syserr("!452 Error writing control file %s", tf);
	}

	if (!newid)
	{
		char new = queue_letter(e, ANYQFL_LETTER);

		/* rename (locked) tf to be (locked) [qh]f */
		(void) sm_strlcpy(qf, queuename(e, ANYQFL_LETTER),
				  sizeof(qf));
		if (rename(tf, qf) < 0)
			syserr("cannot rename(%s, %s), uid=%ld",
				tf, qf, (long) geteuid());
		else
		{
			/*
			**  Check if type has changed and only
			**  remove the old item if the rename above
			**  succeeded.
			*/

			if (e->e_qfletter != '\0' &&
			    e->e_qfletter != new)
			{
				if (tTd(40, 5))
				{
					sm_dprintf("type changed from %c to %c\n",
						   e->e_qfletter, new);
				}

				if (unlink(queuename(e, e->e_qfletter)) < 0)
				{
					/* XXX: something more drastic? */
					if (LogLevel > 0)
						sm_syslog(LOG_ERR, e->e_id,
							  "queueup: unlink(%s) failed: %s",
							  queuename(e, e->e_qfletter),
							  sm_errstring(errno));
				}
			}
		}
		e->e_qfletter = new;

		/*
		**  fsync() after renaming to make sure metadata is
		**  written to disk on filesystems in which renames are
		**  not guaranteed.
		*/

		if (SuperSafe != SAFE_NO)
		{
			/* for softupdates */
			if (tfd >= 0 && fsync(tfd) < 0)
			{
				syserr("!queueup: cannot fsync queue temp file %s",
				       tf);
			}
			SYNC_DIR(qf, true);
		}

		/* close and unlock old (locked) queue file */
		if (e->e_lockfp != NULL)
			(void) sm_io_close(e->e_lockfp, SM_TIME_DEFAULT);
		e->e_lockfp = tfp;

		/* save log info */
		if (LogLevel > 79)
			sm_syslog(LOG_DEBUG, e->e_id, "queueup %s", qf);
	}
	else
	{
		/* save log info */
		if (LogLevel > 79)
			sm_syslog(LOG_DEBUG, e->e_id, "queueup %s", tf);

		e->e_qfletter = queue_letter(e, ANYQFL_LETTER);
	}

	errno = 0;
	e->e_flags |= EF_INQUEUE;

	if (tTd(40, 1))
		sm_dprintf("<<<<< done queueing %s <<<<<\n\n", e->e_id);
	return;
}

/*
**  PRINTCTLADDR -- print control address to file.
**
**	Parameters:
**		a -- address.
**		tfp -- file pointer.
**
**	Returns:
**		none.
**
**	Side Effects:
**		The control address (if changed) is printed to the file.
**		The last control address and uid are saved.
*/

static void
printctladdr(a, tfp)
	register ADDRESS *a;
	SM_FILE_T *tfp;
{
	char *user;
	register ADDRESS *q;
	uid_t uid;
	gid_t gid;
	static ADDRESS *lastctladdr = NULL;
	static uid_t lastuid;

	/* initialization */
	if (a == NULL || a->q_alias == NULL || tfp == NULL)
	{
		if (lastctladdr != NULL && tfp != NULL)
			(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "C\n");
		lastctladdr = NULL;
		lastuid = 0;
		return;
	}

	/* find the active uid */
	q = getctladdr(a);
	if (q == NULL)
	{
		user = NULL;
		uid = 0;
		gid = 0;
	}
	else
	{
		user = q->q_ruser != NULL ? q->q_ruser : q->q_user;
		uid = q->q_uid;
		gid = q->q_gid;
	}
	a = a->q_alias;

	/* check to see if this is the same as last time */
	if (lastctladdr != NULL && uid == lastuid &&
	    strcmp(lastctladdr->q_paddr, a->q_paddr) == 0)
		return;
	lastuid = uid;
	lastctladdr = a;

	if (uid == 0 || user == NULL || user[0] == '\0')
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "C");
	else
		(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, "C%s:%ld:%ld",
				     denlstring(user, true, false), (long) uid,
				     (long) gid);
	(void) sm_io_fprintf(tfp, SM_TIME_DEFAULT, ":%s\n",
			     denlstring(a->q_paddr, true, false));
}

/*
**  RUNNERS_SIGTERM -- propagate a SIGTERM to queue runner process
**
**	This propagates the signal to the child processes that are queue
**	runners. This is for a queue runner "cleanup". After all of the
**	child queue runner processes are signaled (it should be SIGTERM
**	being the sig) then the old signal handler (Oldsh) is called
**	to handle any cleanup set for this process (provided it is not
**	SIG_DFL or SIG_IGN). The signal may not be handled immediately
**	if the BlockOldsh flag is set. If the current process doesn't
**	have a parent then handle the signal immediately, regardless of
**	BlockOldsh.
**
**	Parameters:
**		sig -- the signal number being sent
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets the NoMoreRunners boolean to true to stop more runners
**		from being started in runqueue().
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

static bool		volatile NoMoreRunners = false;
static sigfunc_t	Oldsh_term = SIG_DFL;
static sigfunc_t	Oldsh_hup = SIG_DFL;
static sigfunc_t	volatile Oldsh = SIG_DFL;
static bool		BlockOldsh = false;
static int		volatile Oldsig = 0;
static SIGFUNC_DECL	runners_sigterm __P((int));
static SIGFUNC_DECL	runners_sighup __P((int));

static SIGFUNC_DECL
runners_sigterm(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, runners_sigterm);
	errno = save_errno;
	CHECK_CRITICAL(sig);
	NoMoreRunners = true;
	Oldsh = Oldsh_term;
	Oldsig = sig;
	proc_list_signal(PROC_QUEUE, sig);

	if (!BlockOldsh || getppid() <= 1)
	{
		/* Check that a valid 'old signal handler' is callable */
		if (Oldsh_term != SIG_DFL && Oldsh_term != SIG_IGN &&
		    Oldsh_term != runners_sigterm)
			(*Oldsh_term)(sig);
	}
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  RUNNERS_SIGHUP -- propagate a SIGHUP to queue runner process
**
**	This propagates the signal to the child processes that are queue
**	runners. This is for a queue runner "cleanup". After all of the
**	child queue runner processes are signaled (it should be SIGHUP
**	being the sig) then the old signal handler (Oldsh) is called to
**	handle any cleanup set for this process (provided it is not SIG_DFL
**	or SIG_IGN). The signal may not be handled immediately if the
**	BlockOldsh flag is set. If the current process doesn't have
**	a parent then handle the signal immediately, regardless of
**	BlockOldsh.
**
**	Parameters:
**		sig -- the signal number being sent
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets the NoMoreRunners boolean to true to stop more runners
**		from being started in runqueue().
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

static SIGFUNC_DECL
runners_sighup(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, runners_sighup);
	errno = save_errno;
	CHECK_CRITICAL(sig);
	NoMoreRunners = true;
	Oldsh = Oldsh_hup;
	Oldsig = sig;
	proc_list_signal(PROC_QUEUE, sig);

	if (!BlockOldsh || getppid() <= 1)
	{
		/* Check that a valid 'old signal handler' is callable */
		if (Oldsh_hup != SIG_DFL && Oldsh_hup != SIG_IGN &&
		    Oldsh_hup != runners_sighup)
			(*Oldsh_hup)(sig);
	}
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  MARK_WORK_GROUP_RESTART -- mark a work group as needing a restart
**
**  Sets a workgroup for restarting.
**
**	Parameters:
**		wgrp -- the work group id to restart.
**		reason -- why (signal?), -1 to turn off restart
**
**	Returns:
**		none.
**
**	Side effects:
**		May set global RestartWorkGroup to true.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

void
mark_work_group_restart(wgrp, reason)
	int wgrp;
	int reason;
{
	if (wgrp < 0 || wgrp > NumWorkGroups)
		return;

	WorkGrp[wgrp].wg_restart = reason;
	if (reason >= 0)
		RestartWorkGroup = true;
}
/*
**  RESTART_MARKED_WORK_GROUPS -- restart work groups marked as needing restart
**
**  Restart any workgroup marked as needing a restart provided more
**  runners are allowed.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side effects:
**		Sets global RestartWorkGroup to false.
*/

void
restart_marked_work_groups()
{
	int i;
	int wasblocked;

	if (NoMoreRunners)
		return;

	/* Block SIGCHLD so reapchild() doesn't mess with us */
	wasblocked = sm_blocksignal(SIGCHLD);

	for (i = 0; i < NumWorkGroups; i++)
	{
		if (WorkGrp[i].wg_restart >= 0)
		{
			if (LogLevel > 8)
				sm_syslog(LOG_ERR, NOQID,
					  "restart queue runner=%d due to signal 0x%x",
					  i, WorkGrp[i].wg_restart);
			restart_work_group(i);
		}
	}
	RestartWorkGroup = false;

	if (wasblocked == 0)
		(void) sm_releasesignal(SIGCHLD);
}
/*
**  RESTART_WORK_GROUP -- restart a specific work group
**
**  Restart a specific workgroup provided more runners are allowed.
**  If the requested work group has been restarted too many times log
**  this and refuse to restart.
**
**	Parameters:
**		wgrp -- the work group id to restart
**
**	Returns:
**		none.
**
**	Side Effects:
**		starts another process doing the work of wgrp
*/

#define MAX_PERSIST_RESTART	10	/* max allowed number of restarts */

static void
restart_work_group(wgrp)
	int wgrp;
{
	if (NoMoreRunners ||
	    wgrp < 0 || wgrp > NumWorkGroups)
		return;

	WorkGrp[wgrp].wg_restart = -1;
	if (WorkGrp[wgrp].wg_restartcnt < MAX_PERSIST_RESTART)
	{
		/* avoid overflow; increment here */
		WorkGrp[wgrp].wg_restartcnt++;
		(void) run_work_group(wgrp, RWG_FORK|RWG_PERSISTENT|RWG_RUNALL);
	}
	else
	{
		sm_syslog(LOG_ERR, NOQID,
			  "ERROR: persistent queue runner=%d restarted too many times, queue runner lost",
			  wgrp);
	}
}
/*
**  SCHEDULE_QUEUE_RUNS -- schedule the next queue run for a work group.
**
**	Parameters:
**		runall -- schedule even if individual bit is not set.
**		wgrp -- the work group id to schedule.
**		didit -- the queue run was performed for this work group.
**
**	Returns:
**		nothing
*/

#define INCR_MOD(v, m)	if (++v >= m)	\
				v = 0;	\
			else

static void
schedule_queue_runs(runall, wgrp, didit)
	bool runall;
	int wgrp;
	bool didit;
{
	int qgrp, cgrp, endgrp;
#if _FFR_QUEUE_SCHED_DBG
	time_t lastsched;
	bool sched;
#endif /* _FFR_QUEUE_SCHED_DBG */
	time_t now;
	time_t minqintvl;

	/*
	**  This is a bit ugly since we have to duplicate the
	**  code that "walks" through a work queue group.
	*/

	now = curtime();
	minqintvl = 0;
	cgrp = endgrp = WorkGrp[wgrp].wg_curqgrp;
	do
	{
		time_t qintvl;

#if _FFR_QUEUE_SCHED_DBG
		lastsched = 0;
		sched = false;
#endif /* _FFR_QUEUE_SCHED_DBG */
		qgrp = WorkGrp[wgrp].wg_qgs[cgrp]->qg_index;
		if (Queue[qgrp]->qg_queueintvl > 0)
			qintvl = Queue[qgrp]->qg_queueintvl;
		else if (QueueIntvl > 0)
			qintvl = QueueIntvl;
		else
			qintvl = (time_t) 0;
#if _FFR_QUEUE_SCHED_DBG
		lastsched = Queue[qgrp]->qg_nextrun;
#endif /* _FFR_QUEUE_SCHED_DBG */
		if ((runall || Queue[qgrp]->qg_nextrun <= now) && qintvl > 0)
		{
#if _FFR_QUEUE_SCHED_DBG
			sched = true;
#endif /* _FFR_QUEUE_SCHED_DBG */
			if (minqintvl == 0 || qintvl < minqintvl)
				minqintvl = qintvl;

			/*
			**  Only set a new time if a queue run was performed
			**  for this queue group.  If the queue was not run,
			**  we could starve it by setting a new time on each
			**  call.
			*/

			if (didit)
				Queue[qgrp]->qg_nextrun += qintvl;
		}
#if _FFR_QUEUE_SCHED_DBG
		if (tTd(69, 10))
			sm_syslog(LOG_INFO, NOQID,
				"sqr: wgrp=%d, cgrp=%d, qgrp=%d, intvl=%ld, QI=%ld, runall=%d, lastrun=%ld, nextrun=%ld, sched=%d",
				wgrp, cgrp, qgrp, Queue[qgrp]->qg_queueintvl,
				QueueIntvl, runall, lastsched,
				Queue[qgrp]->qg_nextrun, sched);
#endif /* _FFR_QUEUE_SCHED_DBG */
		INCR_MOD(cgrp, WorkGrp[wgrp].wg_numqgrp);
	} while (endgrp != cgrp);
	if (minqintvl > 0)
		(void) sm_setevent(minqintvl, runqueueevent, 0);
}

#if _FFR_QUEUE_RUN_PARANOIA
/*
**  CHECKQUEUERUNNER -- check whether a queue group hasn't been run.
**
**	Use this if events may get lost and hence queue runners may not
**	be started and mail will pile up in a queue.
**
**	Parameters:
**		none.
**
**	Returns:
**		true if a queue run is necessary.
**
**	Side Effects:
**		may schedule a queue run.
*/

bool
checkqueuerunner()
{
	int qgrp;
	time_t now, minqintvl;

	now = curtime();
	minqintvl = 0;
	for (qgrp = 0; qgrp < NumQueue && Queue[qgrp] != NULL; qgrp++)
	{
		time_t qintvl;

		if (Queue[qgrp]->qg_queueintvl > 0)
			qintvl = Queue[qgrp]->qg_queueintvl;
		else if (QueueIntvl > 0)
			qintvl = QueueIntvl;
		else
			qintvl = (time_t) 0;
		if (Queue[qgrp]->qg_nextrun <= now - qintvl)
		{
			if (minqintvl == 0 || qintvl < minqintvl)
				minqintvl = qintvl;
			if (LogLevel > 1)
				sm_syslog(LOG_WARNING, NOQID,
					"checkqueuerunner: queue %d should have been run at %s, queue interval %ld",
					qgrp,
					arpadate(ctime(&Queue[qgrp]->qg_nextrun)),
					qintvl);
		}
	}
	if (minqintvl > 0)
	{
		(void) sm_setevent(minqintvl, runqueueevent, 0);
		return true;
	}
	return false;
}
#endif /* _FFR_QUEUE_RUN_PARANOIA */

/*
**  RUNQUEUE -- run the jobs in the queue.
**
**	Gets the stuff out of the queue in some presumably logical
**	order and processes them.
**
**	Parameters:
**		forkflag -- true if the queue scanning should be done in
**			a child process.  We double-fork so it is not our
**			child and we don't have to clean up after it.
**			false can be ignored if we have multiple queues.
**		verbose -- if true, print out status information.
**		persistent -- persistent queue runner?
**		runall -- run all groups or only a subset (DoQueueRun)?
**
**	Returns:
**		true if the queue run successfully began.
**
**	Side Effects:
**		runs things in the mail queue using run_work_group().
**		maybe schedules next queue run.
*/

static ENVELOPE	QueueEnvelope;		/* the queue run envelope */
static time_t	LastQueueTime = 0;	/* last time a queue ID assigned */
static pid_t	LastQueuePid = -1;	/* last PID which had a queue ID */

/* values for qp_supdirs */
#define QP_NOSUB	0x0000	/* No subdirectories */
#define QP_SUBDF	0x0001	/* "df" subdirectory */
#define QP_SUBQF	0x0002	/* "qf" subdirectory */
#define QP_SUBXF	0x0004	/* "xf" subdirectory */

bool
runqueue(forkflag, verbose, persistent, runall)
	bool forkflag;
	bool verbose;
	bool persistent;
	bool runall;
{
	int i;
	bool ret = true;
	static int curnum = 0;
	sigfunc_t cursh;
#if SM_HEAP_CHECK
	SM_NONVOLATILE int oldgroup = 0;

	if (sm_debug_active(&DebugLeakQ, 1))
	{
		oldgroup = sm_heap_group();
		sm_heap_newgroup();
		sm_dprintf("runqueue() heap group #%d\n", sm_heap_group());
	}
#endif /* SM_HEAP_CHECK */

	/* queue run has been started, don't do any more this time */
	DoQueueRun = false;

	/* more than one queue or more than one directory per queue */
	if (!forkflag && !verbose &&
	    (WorkGrp[0].wg_qgs[0]->qg_numqueues > 1 || NumWorkGroups > 1 ||
	     WorkGrp[0].wg_numqgrp > 1))
		forkflag = true;

	/*
	**  For controlling queue runners via signals sent to this process.
	**  Oldsh* will get called too by runners_sig* (if it is not SIG_IGN
	**  or SIG_DFL) to preserve cleanup behavior. Now that this process
	**  will have children (and perhaps grandchildren) this handler will
	**  be left in place. This is because this process, once it has
	**  finished spinning off queue runners, may go back to doing something
	**  else (like being a daemon). And we still want on a SIG{TERM,HUP} to
	**  clean up the child queue runners. Only install 'runners_sig*' once
	**  else we'll get stuck looping forever.
	*/

	cursh = sm_signal(SIGTERM, runners_sigterm);
	if (cursh != runners_sigterm)
		Oldsh_term = cursh;
	cursh = sm_signal(SIGHUP, runners_sighup);
	if (cursh != runners_sighup)
		Oldsh_hup = cursh;

	for (i = 0; i < NumWorkGroups && !NoMoreRunners; i++)
	{
		int rwgflags = RWG_NONE;
		int wasblocked;

		/*
		**  If MaxQueueChildren active then test whether the start
		**  of the next queue group's additional queue runners (maximum)
		**  will result in MaxQueueChildren being exceeded.
		**
		**  Note: do not use continue; even though another workgroup
		**	may have fewer queue runners, this would be "unfair",
		**	i.e., this work group might "starve" then.
		*/

#if _FFR_QUEUE_SCHED_DBG
		if (tTd(69, 10))
			sm_syslog(LOG_INFO, NOQID,
				"rq: curnum=%d, MaxQueueChildren=%d, CurRunners=%d, WorkGrp[curnum].wg_maxact=%d",
				curnum, MaxQueueChildren, CurRunners,
				WorkGrp[curnum].wg_maxact);
#endif /* _FFR_QUEUE_SCHED_DBG */
		if (MaxQueueChildren > 0 &&
		    CurRunners + WorkGrp[curnum].wg_maxact > MaxQueueChildren)
			break;

		/*
		**  Pick up where we left off (curnum), in case we
		**  used up all the children last time without finishing.
		**  This give a round-robin fairness to queue runs.
		**
		**  Increment CurRunners before calling run_work_group()
		**  to avoid a "race condition" with proc_list_drop() which
		**  decrements CurRunners if the queue runners terminate.
		**  Notice: CurRunners is an upper limit, in some cases
		**  (too few jobs in the queue) this value is larger than
		**  the actual number of queue runners. The discrepancy can
		**  increase if some queue runners "hang" for a long time.
		*/

		/* don't let proc_list_drop() change CurRunners */
		wasblocked = sm_blocksignal(SIGCHLD);
		CurRunners += WorkGrp[curnum].wg_maxact;
		if (wasblocked == 0)
			(void) sm_releasesignal(SIGCHLD);
		if (forkflag)
			rwgflags |= RWG_FORK;
		if (verbose)
			rwgflags |= RWG_VERBOSE;
		if (persistent)
			rwgflags |= RWG_PERSISTENT;
		if (runall)
			rwgflags |= RWG_RUNALL;
		ret = run_work_group(curnum, rwgflags);

		/*
		**  Failure means a message was printed for ETRN
		**  and subsequent queues are likely to fail as well.
		**  Decrement CurRunners in that case because
		**  none have been started.
		*/

		if (!ret)
		{
			/* don't let proc_list_drop() change CurRunners */
			wasblocked = sm_blocksignal(SIGCHLD);
			CurRunners -= WorkGrp[curnum].wg_maxact;
			CHK_CUR_RUNNERS("runqueue", curnum,
					WorkGrp[curnum].wg_maxact);
			if (wasblocked == 0)
				(void) sm_releasesignal(SIGCHLD);
			break;
		}

		if (!persistent)
			schedule_queue_runs(runall, curnum, true);
		INCR_MOD(curnum, NumWorkGroups);
	}

	/* schedule left over queue runs */
	if (i < NumWorkGroups && !NoMoreRunners && !persistent)
	{
		int h;

		for (h = curnum; i < NumWorkGroups; i++)
		{
			schedule_queue_runs(runall, h, false);
			INCR_MOD(h, NumWorkGroups);
		}
	}


#if SM_HEAP_CHECK
	if (sm_debug_active(&DebugLeakQ, 1))
		sm_heap_setgroup(oldgroup);
#endif /* SM_HEAP_CHECK */
	return ret;
}

#if _FFR_SKIP_DOMAINS
/*
**  SKIP_DOMAINS -- Skip 'skip' number of domains in the WorkQ.
**
**  Added by Stephen Frost <sfrost@snowman.net> to support
**  having each runner process every N'th domain instead of
**  every N'th message.
**
**	Parameters:
**		skip -- number of domains in WorkQ to skip.
**
**	Returns:
**		total number of messages skipped.
**
**	Side Effects:
**		may change WorkQ
*/

static int
skip_domains(skip)
	int skip;
{
	int n, seqjump;

	for (n = 0, seqjump = 0; n < skip && WorkQ != NULL; seqjump++)
	{
		if (WorkQ->w_next != NULL)
		{
			if (WorkQ->w_host != NULL &&
			    WorkQ->w_next->w_host != NULL)
			{
				if (sm_strcasecmp(WorkQ->w_host,
						WorkQ->w_next->w_host) != 0)
					n++;
			}
			else
			{
				if ((WorkQ->w_host != NULL &&
				     WorkQ->w_next->w_host == NULL) ||
				    (WorkQ->w_host == NULL &&
				     WorkQ->w_next->w_host != NULL))
					     n++;
			}
		}
		WorkQ = WorkQ->w_next;
	}
	return seqjump;
}
#endif /* _FFR_SKIP_DOMAINS */

/*
**  RUNNER_WORK -- have a queue runner do its work
**
**  Have a queue runner do its work a list of entries.
**  When work isn't directly being done then this process can take a signal
**  and terminate immediately (in a clean fashion of course).
**  When work is directly being done, it's not to be interrupted
**  immediately: the work should be allowed to finish at a clean point
**  before termination (in a clean fashion of course).
**
**	Parameters:
**		e -- envelope.
**		sequenceno -- 'th process to run WorkQ.
**		didfork -- did the calling process fork()?
**		skip -- process only each skip'th item.
**		njobs -- number of jobs in WorkQ.
**
**	Returns:
**		none.
**
**	Side Effects:
**		runs things in the mail queue.
*/

static void
runner_work(e, sequenceno, didfork, skip, njobs)
	register ENVELOPE *e;
	int sequenceno;
	bool didfork;
	int skip;
	int njobs;
{
	int n, seqjump;
	WORK *w;
	time_t now;

	SM_GET_LA(now);

	/*
	**  Here we temporarily block the second calling of the handlers.
	**  This allows us to handle the signal without terminating in the
	**  middle of direct work. If a signal does come, the test for
	**  NoMoreRunners will find it.
	*/

	BlockOldsh = true;
	seqjump = skip;

	/* process them once at a time */
	while (WorkQ != NULL)
	{
#if SM_HEAP_CHECK
		SM_NONVOLATILE int oldgroup = 0;

		if (sm_debug_active(&DebugLeakQ, 1))
		{
			oldgroup = sm_heap_group();
			sm_heap_newgroup();
			sm_dprintf("run_queue_group() heap group #%d\n",
				sm_heap_group());
		}
#endif /* SM_HEAP_CHECK */

		/* do no more work */
		if (NoMoreRunners)
		{
			/* Check that a valid signal handler is callable */
			if (Oldsh != SIG_DFL && Oldsh != SIG_IGN &&
			    Oldsh != runners_sighup &&
			    Oldsh != runners_sigterm)
				(*Oldsh)(Oldsig);
			break;
		}

		w = WorkQ; /* assign current work item */

		/*
		**  Set the head of the WorkQ to the next work item.
		**  It is set 'skip' ahead (the number of parallel queue
		**  runners working on WorkQ together) since each runner
		**  works on every 'skip'th (N-th) item.
#if _FFR_SKIP_DOMAINS
		**  In the case of the BYHOST Queue Sort Order, the 'item'
		**  is a domain, so we work on every 'skip'th (N-th) domain.
#endif * _FFR_SKIP_DOMAINS *
		*/

#if _FFR_SKIP_DOMAINS
		if (QueueSortOrder == QSO_BYHOST)
		{
			seqjump = 1;
			if (WorkQ->w_next != NULL)
			{
				if (WorkQ->w_host != NULL &&
				    WorkQ->w_next->w_host != NULL)
				{
					if (sm_strcasecmp(WorkQ->w_host,
							WorkQ->w_next->w_host)
								!= 0)
						seqjump = skip_domains(skip);
					else
						WorkQ = WorkQ->w_next;
				}
				else
				{
					if ((WorkQ->w_host != NULL &&
					     WorkQ->w_next->w_host == NULL) ||
					    (WorkQ->w_host == NULL &&
					     WorkQ->w_next->w_host != NULL))
						seqjump = skip_domains(skip);
					else
						WorkQ = WorkQ->w_next;
				}
			}
			else
				WorkQ = WorkQ->w_next;
		}
		else
#endif /* _FFR_SKIP_DOMAINS */
		{
			for (n = 0; n < skip && WorkQ != NULL; n++)
				WorkQ = WorkQ->w_next;
		}

		e->e_to = NULL;

		/*
		**  Ignore jobs that are too expensive for the moment.
		**
		**	Get new load average every GET_NEW_LA_TIME seconds.
		*/

		SM_GET_LA(now);
		if (shouldqueue(WkRecipFact, Current_LA_time))
		{
			char *msg = "Aborting queue run: load average too high";

			if (Verbose)
				message("%s", msg);
			if (LogLevel > 8)
				sm_syslog(LOG_INFO, NOQID, "runqueue: %s", msg);
			break;
		}
		if (shouldqueue(w->w_pri, w->w_ctime))
		{
			if (Verbose)
				message("%s", "");
			if (QueueSortOrder == QSO_BYPRIORITY)
			{
				if (Verbose)
					message("Skipping %s/%s (sequence %d of %d) and flushing rest of queue",
						qid_printqueue(w->w_qgrp,
							       w->w_qdir),
						w->w_name + 2, sequenceno,
						njobs);
				if (LogLevel > 8)
					sm_syslog(LOG_INFO, NOQID,
						  "runqueue: Flushing queue from %s/%s (pri %ld, LA %d, %d of %d)",
						  qid_printqueue(w->w_qgrp,
								 w->w_qdir),
						  w->w_name + 2, w->w_pri,
						  CurrentLA, sequenceno,
						  njobs);
				break;
			}
			else if (Verbose)
				message("Skipping %s/%s (sequence %d of %d)",
					qid_printqueue(w->w_qgrp, w->w_qdir),
					w->w_name + 2, sequenceno, njobs);
		}
		else
		{
			if (Verbose)
			{
				message("%s", "");
				message("Running %s/%s (sequence %d of %d)",
					qid_printqueue(w->w_qgrp, w->w_qdir),
					w->w_name + 2, sequenceno, njobs);
			}
			if (didfork && MaxQueueChildren > 0)
			{
				sm_blocksignal(SIGCHLD);
				(void) sm_signal(SIGCHLD, reapchild);
			}
			if (tTd(63, 100))
				sm_syslog(LOG_DEBUG, NOQID,
					  "runqueue %s dowork(%s)",
					  qid_printqueue(w->w_qgrp, w->w_qdir),
					  w->w_name + 2);

			(void) dowork(w->w_qgrp, w->w_qdir, w->w_name + 2,
				      ForkQueueRuns, false, e);
			errno = 0;
		}
		sm_free(w->w_name); /* XXX */
		if (w->w_host != NULL)
			sm_free(w->w_host); /* XXX */
		sm_free((char *) w); /* XXX */
		sequenceno += seqjump; /* next sequence number */
#if SM_HEAP_CHECK
		if (sm_debug_active(&DebugLeakQ, 1))
			sm_heap_setgroup(oldgroup);
#endif /* SM_HEAP_CHECK */
	}

	BlockOldsh = false;

	/* check the signals didn't happen during the revert */
	if (NoMoreRunners)
	{
		/* Check that a valid signal handler is callable */
		if (Oldsh != SIG_DFL && Oldsh != SIG_IGN &&
		    Oldsh != runners_sighup && Oldsh != runners_sigterm)
			(*Oldsh)(Oldsig);
	}

	Oldsh = SIG_DFL; /* after the NoMoreRunners check */
}
/*
**  RUN_WORK_GROUP -- run the jobs in a queue group from a work group.
**
**	Gets the stuff out of the queue in some presumably logical
**	order and processes them.
**
**	Parameters:
**		wgrp -- work group to process.
**		flags -- RWG_* flags
**
**	Returns:
**		true if the queue run successfully began.
**
**	Side Effects:
**		runs things in the mail queue.
*/

/* Minimum sleep time for persistent queue runners */
#define MIN_SLEEP_TIME	5

bool
run_work_group(wgrp, flags)
	int wgrp;
	int flags;
{
	register ENVELOPE *e;
	int njobs, qdir;
	int sequenceno = 1;
	int qgrp, endgrp, h, i;
	time_t now;
	bool full, more;
	SM_RPOOL_T *rpool;
	extern ENVELOPE BlankEnvelope;
	extern SIGFUNC_DECL reapchild __P((int));

	if (wgrp < 0)
		return false;

	/*
	**  If no work will ever be selected, don't even bother reading
	**  the queue.
	*/

	SM_GET_LA(now);

	if (!bitset(RWG_PERSISTENT, flags) &&
	    shouldqueue(WkRecipFact, Current_LA_time))
	{
		char *msg = "Skipping queue run -- load average too high";

		if (bitset(RWG_VERBOSE, flags))
			message("458 %s\n", msg);
		if (LogLevel > 8)
			sm_syslog(LOG_INFO, NOQID, "runqueue: %s", msg);
		return false;
	}

	/*
	**  See if we already have too many children.
	*/

	if (bitset(RWG_FORK, flags) &&
	    WorkGrp[wgrp].wg_lowqintvl > 0 &&
	    !bitset(RWG_PERSISTENT, flags) &&
	    MaxChildren > 0 && CurChildren >= MaxChildren)
	{
		char *msg = "Skipping queue run -- too many children";

		if (bitset(RWG_VERBOSE, flags))
			message("458 %s (%d)\n", msg, CurChildren);
		if (LogLevel > 8)
			sm_syslog(LOG_INFO, NOQID, "runqueue: %s (%d)",
				  msg, CurChildren);
		return false;
	}

	/*
	**  See if we want to go off and do other useful work.
	*/

	if (bitset(RWG_FORK, flags))
	{
		pid_t pid;

		(void) sm_blocksignal(SIGCHLD);
		(void) sm_signal(SIGCHLD, reapchild);

		pid = dofork();
		if (pid == -1)
		{
			const char *msg = "Skipping queue run -- fork() failed";
			const char *err = sm_errstring(errno);

			if (bitset(RWG_VERBOSE, flags))
				message("458 %s: %s\n", msg, err);
			if (LogLevel > 8)
				sm_syslog(LOG_INFO, NOQID, "runqueue: %s: %s",
					  msg, err);
			(void) sm_releasesignal(SIGCHLD);
			return false;
		}
		if (pid != 0)
		{
			/* parent -- pick up intermediate zombie */
			(void) sm_blocksignal(SIGALRM);

			/* wgrp only used when queue runners are persistent */
			proc_list_add(pid, "Queue runner", PROC_QUEUE,
				      WorkGrp[wgrp].wg_maxact,
				      bitset(RWG_PERSISTENT, flags) ? wgrp : -1,
				      NULL);
			(void) sm_releasesignal(SIGALRM);
			(void) sm_releasesignal(SIGCHLD);
			return true;
		}

		/* child -- clean up signals */

		/* Reset global flags */
		RestartRequest = NULL;
		RestartWorkGroup = false;
		ShutdownRequest = NULL;
		PendingSignal = 0;
		CurrentPid = getpid();
		close_sendmail_pid();

		/*
		**  Initialize exception stack and default exception
		**  handler for child process.
		*/

		sm_exc_newthread(fatal_error);
		clrcontrol();
		proc_list_clear();

		/* Add parent process as first child item */
		proc_list_add(CurrentPid, "Queue runner child process",
			      PROC_QUEUE_CHILD, 0, -1, NULL);
		(void) sm_releasesignal(SIGCHLD);
		(void) sm_signal(SIGCHLD, SIG_DFL);
		(void) sm_signal(SIGHUP, SIG_DFL);
		(void) sm_signal(SIGTERM, intsig);
	}

	/*
	**  Release any resources used by the daemon code.
	*/

	clrdaemon();

	/* force it to run expensive jobs */
	NoConnect = false;

	/* drop privileges */
	if (geteuid() == (uid_t) 0)
		(void) drop_privileges(false);

	/*
	**  Create ourselves an envelope
	*/

	CurEnv = &QueueEnvelope;
	rpool = sm_rpool_new_x(NULL);
	e = newenvelope(&QueueEnvelope, CurEnv, rpool);
	e->e_flags = BlankEnvelope.e_flags;
	e->e_parent = NULL;

	/* make sure we have disconnected from parent */
	if (bitset(RWG_FORK, flags))
	{
		disconnect(1, e);
		QuickAbort = false;
	}

	/*
	**  If we are running part of the queue, always ignore stored
	**  host status.
	*/

	if (QueueLimitId != NULL || QueueLimitSender != NULL ||
	    QueueLimitQuarantine != NULL ||
	    QueueLimitRecipient != NULL)
	{
		IgnoreHostStatus = true;
		MinQueueAge = 0;
		MaxQueueAge = 0;
	}

	/*
	**  Here is where we choose the queue group from the work group.
	**  The caller of the "domorework" label must setup a new envelope.
	*/

	endgrp = WorkGrp[wgrp].wg_curqgrp; /* to not spin endlessly */

  domorework:

	/*
	**  Run a queue group if:
	**  RWG_RUNALL bit is set or the bit for this group is set.
	*/

	now = curtime();
	for (;;)
	{
		/*
		**  Find the next queue group within the work group that
		**  has been marked as needing a run.
		*/

		qgrp = WorkGrp[wgrp].wg_qgs[WorkGrp[wgrp].wg_curqgrp]->qg_index;
		WorkGrp[wgrp].wg_curqgrp++; /* advance */
		WorkGrp[wgrp].wg_curqgrp %= WorkGrp[wgrp].wg_numqgrp; /* wrap */
		if (bitset(RWG_RUNALL, flags) ||
		    (Queue[qgrp]->qg_nextrun <= now &&
		     Queue[qgrp]->qg_nextrun != (time_t) -1))
			break;
		if (endgrp == WorkGrp[wgrp].wg_curqgrp)
		{
			e->e_id = NULL;
			if (bitset(RWG_FORK, flags))
				finis(true, true, ExitStat);
			return true; /* we're done */
		}
	}

	qdir = Queue[qgrp]->qg_curnum; /* round-robin init of queue position */
#if _FFR_QUEUE_SCHED_DBG
	if (tTd(69, 12))
		sm_syslog(LOG_INFO, NOQID,
			"rwg: wgrp=%d, qgrp=%d, qdir=%d, name=%s, curqgrp=%d, numgrps=%d",
			wgrp, qgrp, qdir, qid_printqueue(qgrp, qdir),
			WorkGrp[wgrp].wg_curqgrp, WorkGrp[wgrp].wg_numqgrp);
#endif /* _FFR_QUEUE_SCHED_DBG */

#if HASNICE
	/* tweak niceness of queue runs */
	if (Queue[qgrp]->qg_nice > 0)
		(void) nice(Queue[qgrp]->qg_nice);
#endif /* HASNICE */

	/* XXX running queue group... */
	sm_setproctitle(true, CurEnv, "running queue: %s",
			qid_printqueue(qgrp, qdir));

	if (LogLevel > 69 || tTd(63, 99))
		sm_syslog(LOG_DEBUG, NOQID,
			  "runqueue %s, pid=%d, forkflag=%d",
			  qid_printqueue(qgrp, qdir), (int) CurrentPid,
			  bitset(RWG_FORK, flags));

	/*
	**  Start making passes through the queue.
	**	First, read and sort the entire queue.
	**	Then, process the work in that order.
	**		But if you take too long, start over.
	*/

	for (i = 0; i < Queue[qgrp]->qg_numqueues; i++)
	{
		(void) gatherq(qgrp, qdir, false, &full, &more, &h);
#if SM_CONF_SHM
		if (ShmId != SM_SHM_NO_ID)
			QSHM_ENTRIES(Queue[qgrp]->qg_qpaths[qdir].qp_idx) = h;
#endif /* SM_CONF_SHM */
		/* If there are no more items in this queue advance */
		if (!more)
		{
			/* A round-robin advance */
			qdir++;
			qdir %= Queue[qgrp]->qg_numqueues;
		}

		/* Has the WorkList reached the limit? */
		if (full)
			break; /* don't try to gather more */
	}

	/* order the existing work requests */
	njobs = sortq(Queue[qgrp]->qg_maxlist);
	Queue[qgrp]->qg_curnum = qdir; /* update */


	if (!Verbose && bitnset(QD_FORK, Queue[qgrp]->qg_flags))
	{
		int loop, maxrunners;
		pid_t pid;

		/*
		**  For this WorkQ we want to fork off N children (maxrunners)
		**  at this point. Each child has a copy of WorkQ. Each child
		**  will process every N-th item. The parent will wait for all
		**  of the children to finish before moving on to the next
		**  queue group within the work group. This saves us forking
		**  a new runner-child for each work item.
		**  It's valid for qg_maxqrun == 0 since this may be an
		**  explicit "don't run this queue" setting.
		*/

		maxrunners = Queue[qgrp]->qg_maxqrun;

		/*
		**  If no runners are configured for this group but
		**  the queue is "forced" then lets use 1 runner.
		*/

		if (maxrunners == 0 && bitset(RWG_FORCE, flags))
			maxrunners = 1;

		/* No need to have more runners then there are jobs */
		if (maxrunners > njobs)
			maxrunners = njobs;
		for (loop = 0; loop < maxrunners; loop++)
		{
			/*
			**  Since the delivery may happen in a child and the
			**  parent does not wait, the parent may close the
			**  maps thereby removing any shared memory used by
			**  the map.  Therefore, close the maps now so the
			**  child will dynamically open them if necessary.
			*/

			closemaps(false);

			pid = fork();
			if (pid < 0)
			{
				syserr("run_work_group: cannot fork");
				return false;
			}
			else if (pid > 0)
			{
				/* parent -- clean out connection cache */
				mci_flush(false, NULL);
#if _FFR_SKIP_DOMAINS
				if (QueueSortOrder == QSO_BYHOST)
				{
					sequenceno += skip_domains(1);
				}
				else
#endif /* _FFR_SKIP_DOMAINS */
				{
					/* for the skip */
					WorkQ = WorkQ->w_next;
					sequenceno++;
				}
				proc_list_add(pid, "Queue child runner process",
					      PROC_QUEUE_CHILD, 0, -1, NULL);

				/* No additional work, no additional runners */
				if (WorkQ == NULL)
					break;
			}
			else
			{
				/* child -- Reset global flags */
				RestartRequest = NULL;
				RestartWorkGroup = false;
				ShutdownRequest = NULL;
				PendingSignal = 0;
				CurrentPid = getpid();
				close_sendmail_pid();

				/*
				**  Initialize exception stack and default
				**  exception handler for child process.
				**  When fork()'d the child now has a private
				**  copy of WorkQ at its current position.
				*/

				sm_exc_newthread(fatal_error);

				/*
				**  SMTP processes (whether -bd or -bs) set
				**  SIGCHLD to reapchild to collect
				**  children status.  However, at delivery
				**  time, that status must be collected
				**  by sm_wait() to be dealt with properly
				**  (check success of delivery based
				**  on status code, etc).  Therefore, if we
				**  are an SMTP process, reset SIGCHLD
				**  back to the default so reapchild
				**  doesn't collect status before
				**  sm_wait().
				*/

				if (OpMode == MD_SMTP ||
				    OpMode == MD_DAEMON ||
				    MaxQueueChildren > 0)
				{
					proc_list_clear();
					sm_releasesignal(SIGCHLD);
					(void) sm_signal(SIGCHLD, SIG_DFL);
				}

				/* child -- error messages to the transcript */
				QuickAbort = OnlyOneError = false;
				runner_work(e, sequenceno, true,
					    maxrunners, njobs);

				/* This child is done */
				finis(true, true, ExitStat);
				/* NOTREACHED */
			}
		}

		sm_releasesignal(SIGCHLD);

		/*
		**  Wait until all of the runners have completed before
		**  seeing if there is another queue group in the
		**  work group to process.
		**  XXX Future enhancement: don't wait() for all children
		**  here, just go ahead and make sure that overall the number
		**  of children is not exceeded.
		*/

		while (CurChildren > 0)
		{
			int status;
			pid_t ret;

			while ((ret = sm_wait(&status)) <= 0)
				continue;
			proc_list_drop(ret, status, NULL);
		}
	}
	else if (Queue[qgrp]->qg_maxqrun > 0 || bitset(RWG_FORCE, flags))
	{
		/*
		**  When current process will not fork children to do the work,
		**  it will do the work itself. The 'skip' will be 1 since
		**  there are no child runners to divide the work across.
		*/

		runner_work(e, sequenceno, false, 1, njobs);
	}

	/* free memory allocated by newenvelope() above */
	sm_rpool_free(rpool);
	QueueEnvelope.e_rpool = NULL;

	/* Are there still more queues in the work group to process? */
	if (endgrp != WorkGrp[wgrp].wg_curqgrp)
	{
		rpool = sm_rpool_new_x(NULL);
		e = newenvelope(&QueueEnvelope, CurEnv, rpool);
		e->e_flags = BlankEnvelope.e_flags;
		goto domorework;
	}

	/* No more queues in work group to process. Now check persistent. */
	if (bitset(RWG_PERSISTENT, flags))
	{
		sequenceno = 1;
		sm_setproctitle(true, NULL, "running queue: %s",
				qid_printqueue(qgrp, qdir));

		/*
		**  close bogus maps, i.e., maps which caused a tempfail,
		**	so we get fresh map connections on the next lookup.
		**  closemaps() is also called when children are started.
		*/

		closemaps(true);

		/* Close any cached connections. */
		mci_flush(true, NULL);

		/* Clean out expired related entries. */
		rmexpstab();

#if NAMED_BIND
		/* Update MX records for FallbackMX. */
		if (FallbackMX != NULL)
			(void) getfallbackmxrr(FallbackMX);
#endif /* NAMED_BIND */

#if USERDB
		/* close UserDatabase */
		_udbx_close();
#endif /* USERDB */

#if SM_HEAP_CHECK
		if (sm_debug_active(&SmHeapCheck, 2)
		    && access("memdump", F_OK) == 0
		   )
		{
			SM_FILE_T *out;

			remove("memdump");
			out = sm_io_open(SmFtStdio, SM_TIME_DEFAULT,
					 "memdump.out", SM_IO_APPEND, NULL);
			if (out != NULL)
			{
				(void) sm_io_fprintf(out, SM_TIME_DEFAULT, "----------------------\n");
				sm_heap_report(out,
					sm_debug_level(&SmHeapCheck) - 1);
				(void) sm_io_close(out, SM_TIME_DEFAULT);
			}
		}
#endif /* SM_HEAP_CHECK */

		/* let me rest for a second to catch my breath */
		if (njobs == 0 && WorkGrp[wgrp].wg_lowqintvl < MIN_SLEEP_TIME)
			sleep(MIN_SLEEP_TIME);
		else if (WorkGrp[wgrp].wg_lowqintvl <= 0)
			sleep(QueueIntvl > 0 ? QueueIntvl : MIN_SLEEP_TIME);
		else
			sleep(WorkGrp[wgrp].wg_lowqintvl);

		/*
		**  Get the LA outside the WorkQ loop if necessary.
		**  In a persistent queue runner the code is repeated over
		**  and over but gatherq() may ignore entries due to
		**  shouldqueue() (do we really have to do this twice?).
		**  Hence the queue runners would just idle around when once
		**  CurrentLA caused all entries in a queue to be ignored.
		*/

		if (njobs == 0)
			SM_GET_LA(now);
		rpool = sm_rpool_new_x(NULL);
		e = newenvelope(&QueueEnvelope, CurEnv, rpool);
		e->e_flags = BlankEnvelope.e_flags;
		goto domorework;
	}

	/* exit without the usual cleanup */
	e->e_id = NULL;
	if (bitset(RWG_FORK, flags))
		finis(true, true, ExitStat);
	/* NOTREACHED */
	return true;
}

/*
**  DOQUEUERUN -- do a queue run?
*/

bool
doqueuerun()
{
	return DoQueueRun;
}

/*
**  RUNQUEUEEVENT -- Sets a flag to indicate that a queue run should be done.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		The invocation of this function via an alarm may interrupt
**		a set of actions. Thus errno may be set in that context.
**		We need to restore errno at the end of this function to ensure
**		that any work done here that sets errno doesn't return a
**		misleading/false errno value. Errno may	be EINTR upon entry to
**		this function because of non-restartable/continuable system
**		API was active. Iff this is true we will override errno as
**		a timeout (as a more accurate error message).
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

void
runqueueevent(ignore)
	int ignore;
{
	int save_errno = errno;

	/*
	**  Set the general bit that we want a queue run,
	**  tested in doqueuerun()
	*/

	DoQueueRun = true;
#if _FFR_QUEUE_SCHED_DBG
	if (tTd(69, 10))
		sm_syslog(LOG_INFO, NOQID, "rqe: done");
#endif /* _FFR_QUEUE_SCHED_DBG */

	errno = save_errno;
	if (errno == EINTR)
		errno = ETIMEDOUT;
}
/*
**  GATHERQ -- gather messages from the message queue(s) the work queue.
**
**	Parameters:
**		qgrp -- the index of the queue group.
**		qdir -- the index of the queue directory.
**		doall -- if set, include everything in the queue (even
**			the jobs that cannot be run because the load
**			average is too high, or MaxQueueRun is reached).
**			Otherwise, exclude those jobs.
**		full -- (optional) to be set 'true' if WorkList is full
**		more -- (optional) to be set 'true' if there are still more
**			messages in this queue not added to WorkList
**		pnentries -- (optional) total nuber of entries in queue
**
**	Returns:
**		The number of request in the queue (not necessarily
**		the number of requests in WorkList however).
**
**	Side Effects:
**		prepares available work into WorkList
*/

#define NEED_P		0001	/* 'P': priority */
#define NEED_T		0002	/* 'T': time */
#define NEED_R		0004	/* 'R': recipient */
#define NEED_S		0010	/* 'S': sender */
#define NEED_H		0020	/* host */
#define HAS_QUARANTINE	0040	/* has an unexpected 'q' line */
#define NEED_QUARANTINE	0100	/* 'q': reason */

static WORK	*WorkList = NULL;	/* list of unsort work */
static int	WorkListSize = 0;	/* current max size of WorkList */
static int	WorkListCount = 0;	/* # of work items in WorkList */

static int
gatherq(qgrp, qdir, doall, full, more, pnentries)
	int qgrp;
	int qdir;
	bool doall;
	bool *full;
	bool *more;
	int *pnentries;
{
	register struct dirent *d;
	register WORK *w;
	register char *p;
	DIR *f;
	int i, num_ent, wn, nentries;
	QUEUE_CHAR *check;
	char qd[MAXPATHLEN];
	char qf[MAXPATHLEN];

	wn = WorkListCount - 1;
	num_ent = 0;
	nentries = 0;
	if (qdir == NOQDIR)
		(void) sm_strlcpy(qd, ".", sizeof(qd));
	else
		(void) sm_strlcpyn(qd, sizeof(qd), 2,
			Queue[qgrp]->qg_qpaths[qdir].qp_name,
			(bitset(QP_SUBQF,
				Queue[qgrp]->qg_qpaths[qdir].qp_subdirs)
					? "/qf" : ""));

	if (tTd(41, 1))
	{
		sm_dprintf("gatherq:\n");

		check = QueueLimitId;
		while (check != NULL)
		{
			sm_dprintf("\tQueueLimitId = %s%s\n",
				check->queue_negate ? "!" : "",
				check->queue_match);
			check = check->queue_next;
		}

		check = QueueLimitSender;
		while (check != NULL)
		{
			sm_dprintf("\tQueueLimitSender = %s%s\n",
				check->queue_negate ? "!" : "",
				check->queue_match);
			check = check->queue_next;
		}

		check = QueueLimitRecipient;
		while (check != NULL)
		{
			sm_dprintf("\tQueueLimitRecipient = %s%s\n",
				check->queue_negate ? "!" : "",
				check->queue_match);
			check = check->queue_next;
		}

		if (QueueMode == QM_QUARANTINE)
		{
			check = QueueLimitQuarantine;
			while (check != NULL)
			{
				sm_dprintf("\tQueueLimitQuarantine = %s%s\n",
					   check->queue_negate ? "!" : "",
					   check->queue_match);
				check = check->queue_next;
			}
		}
	}

	/* open the queue directory */
	f = opendir(qd);
	if (f == NULL)
	{
		syserr("gatherq: cannot open \"%s\"",
			qid_printqueue(qgrp, qdir));
		if (full != NULL)
			*full = WorkListCount >= MaxQueueRun && MaxQueueRun > 0;
		if (more != NULL)
			*more = false;
		return 0;
	}

	/*
	**  Read the work directory.
	*/

	while ((d = readdir(f)) != NULL)
	{
		SM_FILE_T *cf;
		int qfver = 0;
		char lbuf[MAXNAME + 1];
		struct stat sbuf;

		if (tTd(41, 50))
			sm_dprintf("gatherq: checking %s..", d->d_name);

		/* is this an interesting entry? */
		if (!(((QueueMode == QM_NORMAL &&
			d->d_name[0] == NORMQF_LETTER) ||
		       (QueueMode == QM_QUARANTINE &&
			d->d_name[0] == QUARQF_LETTER) ||
		       (QueueMode == QM_LOST &&
			d->d_name[0] == LOSEQF_LETTER)) &&
		      d->d_name[1] == 'f'))
		{
			if (tTd(41, 50))
				sm_dprintf("  skipping\n");
			continue;
		}
		if (tTd(41, 50))
			sm_dprintf("\n");

		if (strlen(d->d_name) >= MAXQFNAME)
		{
			if (Verbose)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "gatherq: %s too long, %d max characters\n",
						     d->d_name, MAXQFNAME);
			if (LogLevel > 0)
				sm_syslog(LOG_ALERT, NOQID,
					  "gatherq: %s too long, %d max characters",
					  d->d_name, MAXQFNAME);
			continue;
		}

		++nentries;
		check = QueueLimitId;
		while (check != NULL)
		{
			if (strcontainedin(false, check->queue_match,
					   d->d_name) != check->queue_negate)
				break;
			else
				check = check->queue_next;
		}
		if (QueueLimitId != NULL && check == NULL)
			continue;

		/* grow work list if necessary */
		if (++wn >= MaxQueueRun && MaxQueueRun > 0)
		{
			if (wn == MaxQueueRun && LogLevel > 0)
				sm_syslog(LOG_WARNING, NOQID,
					  "WorkList for %s maxed out at %d",
					  qid_printqueue(qgrp, qdir),
					  MaxQueueRun);
			if (doall)
				continue;	/* just count entries */
			break;
		}
		if (wn >= WorkListSize)
		{
			grow_wlist(qgrp, qdir);
			if (wn >= WorkListSize)
				continue;
		}
		SM_ASSERT(wn >= 0);
		w = &WorkList[wn];

		(void) sm_strlcpyn(qf, sizeof(qf), 3, qd, "/", d->d_name);
		if (stat(qf, &sbuf) < 0)
		{
			if (errno != ENOENT)
				sm_syslog(LOG_INFO, NOQID,
					  "gatherq: can't stat %s/%s",
					  qid_printqueue(qgrp, qdir),
					  d->d_name);
			wn--;
			continue;
		}
		if (!bitset(S_IFREG, sbuf.st_mode))
		{
			/* Yikes!  Skip it or we will hang on open! */
			if (!((d->d_name[0] == DATAFL_LETTER ||
			       d->d_name[0] == NORMQF_LETTER ||
			       d->d_name[0] == QUARQF_LETTER ||
			       d->d_name[0] == LOSEQF_LETTER ||
			       d->d_name[0] == XSCRPT_LETTER) &&
			      d->d_name[1] == 'f' && d->d_name[2] == '\0'))
				syserr("gatherq: %s/%s is not a regular file",
				       qid_printqueue(qgrp, qdir), d->d_name);
			wn--;
			continue;
		}

		/* avoid work if possible */
		if ((QueueSortOrder == QSO_BYFILENAME ||
		     QueueSortOrder == QSO_BYMODTIME ||
		     QueueSortOrder == QSO_NONE ||
		     QueueSortOrder == QSO_RANDOM) &&
		    QueueLimitQuarantine == NULL &&
		    QueueLimitSender == NULL &&
		    QueueLimitRecipient == NULL)
		{
			w->w_qgrp = qgrp;
			w->w_qdir = qdir;
			w->w_name = newstr(d->d_name);
			w->w_host = NULL;
			w->w_lock = w->w_tooyoung = false;
			w->w_pri = 0;
			w->w_ctime = 0;
			w->w_mtime = sbuf.st_mtime;
			++num_ent;
			continue;
		}

		/* open control file */
		cf = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, qf, SM_IO_RDONLY_B,
				NULL);
		if (cf == NULL && OpMode != MD_PRINT)
		{
			/* this may be some random person sending hir msgs */
			if (tTd(41, 2))
				sm_dprintf("gatherq: cannot open %s: %s\n",
					d->d_name, sm_errstring(errno));
			errno = 0;
			wn--;
			continue;
		}
		w->w_qgrp = qgrp;
		w->w_qdir = qdir;
		w->w_name = newstr(d->d_name);
		w->w_host = NULL;
		if (cf != NULL)
		{
			w->w_lock = !lockfile(sm_io_getinfo(cf, SM_IO_WHAT_FD,
							    NULL),
					      w->w_name, NULL,
					      LOCK_SH|LOCK_NB);
		}
		w->w_tooyoung = false;

		/* make sure jobs in creation don't clog queue */
		w->w_pri = 0x7fffffff;
		w->w_ctime = 0;
		w->w_mtime = sbuf.st_mtime;

		/* extract useful information */
		i = NEED_P|NEED_T;
		if (QueueSortOrder == QSO_BYHOST
#if _FFR_RHS
		    || QueueSortOrder == QSO_BYSHUFFLE
#endif /* _FFR_RHS */
		   )
		{
			/* need w_host set for host sort order */
			i |= NEED_H;
		}
		if (QueueLimitSender != NULL)
			i |= NEED_S;
		if (QueueLimitRecipient != NULL)
			i |= NEED_R;
		if (QueueLimitQuarantine != NULL)
			i |= NEED_QUARANTINE;
		while (cf != NULL && i != 0 &&
		       sm_io_fgets(cf, SM_TIME_DEFAULT, lbuf,
				   sizeof(lbuf)) >= 0)
		{
			int c;
			time_t age;

			p = strchr(lbuf, '\n');
			if (p != NULL)
				*p = '\0';
			else
			{
				/* flush rest of overly long line */
				while ((c = sm_io_getc(cf, SM_TIME_DEFAULT))
				       != SM_IO_EOF && c != '\n')
					continue;
			}

			switch (lbuf[0])
			{
			  case 'V':
				qfver = atoi(&lbuf[1]);
				break;

			  case 'P':
				w->w_pri = atol(&lbuf[1]);
				i &= ~NEED_P;
				break;

			  case 'T':
				w->w_ctime = atol(&lbuf[1]);
				i &= ~NEED_T;
				break;

			  case 'q':
				if (QueueMode != QM_QUARANTINE &&
				    QueueMode != QM_LOST)
				{
					if (tTd(41, 49))
						sm_dprintf("%s not marked as quarantined but has a 'q' line\n",
							   w->w_name);
					i |= HAS_QUARANTINE;
				}
				else if (QueueMode == QM_QUARANTINE)
				{
					if (QueueLimitQuarantine == NULL)
					{
						i &= ~NEED_QUARANTINE;
						break;
					}
					p = &lbuf[1];
					check = QueueLimitQuarantine;
					while (check != NULL)
					{
						if (strcontainedin(false,
								   check->queue_match,
								   p) !=
						    check->queue_negate)
							break;
						else
							check = check->queue_next;
					}
					if (check != NULL)
						i &= ~NEED_QUARANTINE;
				}
				break;

			  case 'R':
				if (w->w_host == NULL &&
				    (p = strrchr(&lbuf[1], '@')) != NULL)
				{
#if _FFR_RHS
					if (QueueSortOrder == QSO_BYSHUFFLE)
						w->w_host = newstr(&p[1]);
					else
#endif /* _FFR_RHS */
						w->w_host = strrev(&p[1]);
					makelower(w->w_host);
					i &= ~NEED_H;
				}
				if (QueueLimitRecipient == NULL)
				{
					i &= ~NEED_R;
					break;
				}
				if (qfver > 0)
				{
					p = strchr(&lbuf[1], ':');
					if (p == NULL)
						p = &lbuf[1];
					else
						++p; /* skip over ':' */
				}
				else
					p = &lbuf[1];
				check = QueueLimitRecipient;
				while (check != NULL)
				{
					if (strcontainedin(true,
							   check->queue_match,
							   p) !=
					    check->queue_negate)
						break;
					else
						check = check->queue_next;
				}
				if (check != NULL)
					i &= ~NEED_R;
				break;

			  case 'S':
				check = QueueLimitSender;
				while (check != NULL)
				{
					if (strcontainedin(true,
							   check->queue_match,
							   &lbuf[1]) !=
					    check->queue_negate)
						break;
					else
						check = check->queue_next;
				}
				if (check != NULL)
					i &= ~NEED_S;
				break;

			  case 'K':
				if (MaxQueueAge > 0)
				{
					time_t lasttry, delay;

					lasttry = (time_t) atol(&lbuf[1]);
					delay = MIN(lasttry - w->w_ctime,
						    MaxQueueAge);
					age = curtime() - lasttry;
					if (age < delay)
						w->w_tooyoung = true;
					break;
				}

				age = curtime() - (time_t) atol(&lbuf[1]);
				if (age >= 0 && MinQueueAge > 0 &&
				    age < MinQueueAge)
					w->w_tooyoung = true;
				break;

			  case 'N':
				if (atol(&lbuf[1]) == 0)
					w->w_tooyoung = false;
				break;
			}
		}
		if (cf != NULL)
			(void) sm_io_close(cf, SM_TIME_DEFAULT);

		if ((!doall && (shouldqueue(w->w_pri, w->w_ctime) ||
		    w->w_tooyoung)) ||
		    bitset(HAS_QUARANTINE, i) ||
		    bitset(NEED_QUARANTINE, i) ||
		    bitset(NEED_R|NEED_S, i))
		{
			/* don't even bother sorting this job in */
			if (tTd(41, 49))
				sm_dprintf("skipping %s (%x)\n", w->w_name, i);
			sm_free(w->w_name); /* XXX */
			if (w->w_host != NULL)
				sm_free(w->w_host); /* XXX */
			wn--;
		}
		else
			++num_ent;
	}
	(void) closedir(f);
	wn++;

	i = wn - WorkListCount;
	WorkListCount += SM_MIN(num_ent, WorkListSize);

	if (more != NULL)
		*more = WorkListCount < wn;

	if (full != NULL)
		*full = (wn >= MaxQueueRun && MaxQueueRun > 0) ||
			(WorkList == NULL && wn > 0);

	if (pnentries != NULL)
		*pnentries = nentries;
	return i;
}
/*
**  SORTQ -- sort the work list
**
**	First the old WorkQ is cleared away. Then the WorkList is sorted
**	for all items so that important (higher sorting value) items are not
**	truncated off. Then the most important items are moved from
**	WorkList to WorkQ. The lower count of 'max' or MaxListCount items
**	are moved.
**
**	Parameters:
**		max -- maximum number of items to be placed in WorkQ
**
**	Returns:
**		the number of items in WorkQ
**
**	Side Effects:
**		WorkQ gets released and filled with new work. WorkList
**		gets released. Work items get sorted in order.
*/

static int
sortq(max)
	int max;
{
	register int i;			/* local counter */
	register WORK *w;		/* tmp item pointer */
	int wc = WorkListCount;		/* trim size for WorkQ */

	if (WorkQ != NULL)
	{
		WORK *nw;

		/* Clear out old WorkQ. */
		for (w = WorkQ; w != NULL; w = nw)
		{
			nw = w->w_next;
			sm_free(w->w_name); /* XXX */
			if (w->w_host != NULL)
				sm_free(w->w_host); /* XXX */
			sm_free((char *) w); /* XXX */
		}
		WorkQ = NULL;
	}

	if (WorkList == NULL || wc <= 0)
		return 0;

	/*
	**  The sort now takes place using all of the items in WorkList.
	**  The list gets trimmed to the most important items after the sort.
	**  If the trim were to happen before the sort then one or more
	**  important items might get truncated off -- not what we want.
	*/

	if (QueueSortOrder == QSO_BYHOST)
	{
		/*
		**  Sort the work directory for the first time,
		**  based on host name, lock status, and priority.
		*/

		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf1);

		/*
		**  If one message to host is locked, "lock" all messages
		**  to that host.
		*/

		i = 0;
		while (i < wc)
		{
			if (!WorkList[i].w_lock)
			{
				i++;
				continue;
			}
			w = &WorkList[i];
			while (++i < wc)
			{
				if (WorkList[i].w_host == NULL &&
				    w->w_host == NULL)
					WorkList[i].w_lock = true;
				else if (WorkList[i].w_host != NULL &&
					 w->w_host != NULL &&
					 sm_strcasecmp(WorkList[i].w_host,
						       w->w_host) == 0)
					WorkList[i].w_lock = true;
				else
					break;
			}
		}

		/*
		**  Sort the work directory for the second time,
		**  based on lock status, host name, and priority.
		*/

		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf2);
	}
	else if (QueueSortOrder == QSO_BYTIME)
	{
		/*
		**  Simple sort based on submission time only.
		*/

		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf3);
	}
	else if (QueueSortOrder == QSO_BYFILENAME)
	{
		/*
		**  Sort based on queue filename.
		*/

		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf4);
	}
	else if (QueueSortOrder == QSO_RANDOM)
	{
		/*
		**  Sort randomly.  To avoid problems with an instable sort,
		**  use a random index into the queue file name to start
		**  comparison.
		*/

		randi = get_rand_mod(MAXQFNAME);
		if (randi < 2)
			randi = 3;
		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf5);
	}
	else if (QueueSortOrder == QSO_BYMODTIME)
	{
		/*
		**  Simple sort based on modification time of queue file.
		**  This puts the oldest items first.
		*/

		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf6);
	}
#if _FFR_RHS
	else if (QueueSortOrder == QSO_BYSHUFFLE)
	{
		/*
		**  Simple sort based on shuffled host name.
		*/

		init_shuffle_alphabet();
		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf7);
	}
#endif /* _FFR_RHS */
	else if (QueueSortOrder == QSO_BYPRIORITY)
	{
		/*
		**  Simple sort based on queue priority only.
		*/

		qsort((char *) WorkList, wc, sizeof(*WorkList), workcmpf0);
	}
	/* else don't sort at all */

	/* Check if the per queue group item limit will be exceeded */
	if (wc > max && max > 0)
		wc = max;

	/*
	**  Convert the work list into canonical form.
	**	Should be turning it into a list of envelopes here perhaps.
	**  Only take the most important items up to the per queue group
	**  maximum.
	*/

	for (i = wc; --i >= 0; )
	{
		w = (WORK *) xalloc(sizeof(*w));
		w->w_qgrp = WorkList[i].w_qgrp;
		w->w_qdir = WorkList[i].w_qdir;
		w->w_name = WorkList[i].w_name;
		w->w_host = WorkList[i].w_host;
		w->w_lock = WorkList[i].w_lock;
		w->w_tooyoung = WorkList[i].w_tooyoung;
		w->w_pri = WorkList[i].w_pri;
		w->w_ctime = WorkList[i].w_ctime;
		w->w_mtime = WorkList[i].w_mtime;
		w->w_next = WorkQ;
		WorkQ = w;
	}

	/* free the rest of the list */
	for (i = WorkListCount; --i >= wc; )
	{
		sm_free(WorkList[i].w_name);
		if (WorkList[i].w_host != NULL)
			sm_free(WorkList[i].w_host);
	}

	if (WorkList != NULL)
		sm_free(WorkList); /* XXX */
	WorkList = NULL;
	WorkListSize = 0;
	WorkListCount = 0;

	if (tTd(40, 1))
	{
		for (w = WorkQ; w != NULL; w = w->w_next)
		{
			if (w->w_host != NULL)
				sm_dprintf("%22s: pri=%ld %s\n",
					w->w_name, w->w_pri, w->w_host);
			else
				sm_dprintf("%32s: pri=%ld\n",
					w->w_name, w->w_pri);
		}
	}

	return wc; /* return number of WorkQ items */
}
/*
**  GROW_WLIST -- make the work list larger
**
**	Parameters:
**		qgrp -- the index for the queue group.
**		qdir -- the index for the queue directory.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Adds another QUEUESEGSIZE entries to WorkList if possible.
**		It can fail if there isn't enough memory, so WorkListSize
**		should be checked again upon return.
*/

static void
grow_wlist(qgrp, qdir)
	int qgrp;
	int qdir;
{
	if (tTd(41, 1))
		sm_dprintf("grow_wlist: WorkListSize=%d\n", WorkListSize);
	if (WorkList == NULL)
	{
		WorkList = (WORK *) xalloc((sizeof(*WorkList)) *
					   (QUEUESEGSIZE + 1));
		WorkListSize = QUEUESEGSIZE;
	}
	else
	{
		int newsize = WorkListSize + QUEUESEGSIZE;
		WORK *newlist = (WORK *) sm_realloc((char *) WorkList,
					  (unsigned) sizeof(WORK) * (newsize + 1));

		if (newlist != NULL)
		{
			WorkListSize = newsize;
			WorkList = newlist;
			if (LogLevel > 1)
			{
				sm_syslog(LOG_INFO, NOQID,
					  "grew WorkList for %s to %d",
					  qid_printqueue(qgrp, qdir),
					  WorkListSize);
			}
		}
		else if (LogLevel > 0)
		{
			sm_syslog(LOG_ALERT, NOQID,
				  "FAILED to grow WorkList for %s to %d",
				  qid_printqueue(qgrp, qdir), newsize);
		}
	}
	if (tTd(41, 1))
		sm_dprintf("grow_wlist: WorkListSize now %d\n", WorkListSize);
}
/*
**  WORKCMPF0 -- simple priority-only compare function.
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		-1 if a < b
**		 0 if a == b
**		+1 if a > b
**
*/

static int
workcmpf0(a, b)
	register WORK *a;
	register WORK *b;
{
	long pa = a->w_pri;
	long pb = b->w_pri;

	if (pa == pb)
		return 0;
	else if (pa > pb)
		return 1;
	else
		return -1;
}
/*
**  WORKCMPF1 -- first compare function for ordering work based on host name.
**
**	Sorts on host name, lock status, and priority in that order.
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		<0 if a < b
**		 0 if a == b
**		>0 if a > b
**
*/

static int
workcmpf1(a, b)
	register WORK *a;
	register WORK *b;
{
	int i;

	/* host name */
	if (a->w_host != NULL && b->w_host == NULL)
		return 1;
	else if (a->w_host == NULL && b->w_host != NULL)
		return -1;
	if (a->w_host != NULL && b->w_host != NULL &&
	    (i = sm_strcasecmp(a->w_host, b->w_host)) != 0)
		return i;

	/* lock status */
	if (a->w_lock != b->w_lock)
		return b->w_lock - a->w_lock;

	/* job priority */
	return workcmpf0(a, b);
}
/*
**  WORKCMPF2 -- second compare function for ordering work based on host name.
**
**	Sorts on lock status, host name, and priority in that order.
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		<0 if a < b
**		 0 if a == b
**		>0 if a > b
**
*/

static int
workcmpf2(a, b)
	register WORK *a;
	register WORK *b;
{
	int i;

	/* lock status */
	if (a->w_lock != b->w_lock)
		return a->w_lock - b->w_lock;

	/* host name */
	if (a->w_host != NULL && b->w_host == NULL)
		return 1;
	else if (a->w_host == NULL && b->w_host != NULL)
		return -1;
	if (a->w_host != NULL && b->w_host != NULL &&
	    (i = sm_strcasecmp(a->w_host, b->w_host)) != 0)
		return i;

	/* job priority */
	return workcmpf0(a, b);
}
/*
**  WORKCMPF3 -- simple submission-time-only compare function.
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		-1 if a < b
**		 0 if a == b
**		+1 if a > b
**
*/

static int
workcmpf3(a, b)
	register WORK *a;
	register WORK *b;
{
	if (a->w_ctime > b->w_ctime)
		return 1;
	else if (a->w_ctime < b->w_ctime)
		return -1;
	else
		return 0;
}
/*
**  WORKCMPF4 -- compare based on file name
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		-1 if a < b
**		 0 if a == b
**		+1 if a > b
**
*/

static int
workcmpf4(a, b)
	register WORK *a;
	register WORK *b;
{
	return strcmp(a->w_name, b->w_name);
}
/*
**  WORKCMPF5 -- compare based on assigned random number
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		randomly 1/-1
*/

/* ARGSUSED0 */
static int
workcmpf5(a, b)
	register WORK *a;
	register WORK *b;
{
	if (strlen(a->w_name) < randi || strlen(b->w_name) < randi)
		return -1;
	return a->w_name[randi] - b->w_name[randi];
}
/*
**  WORKCMPF6 -- simple modification-time-only compare function.
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		-1 if a < b
**		 0 if a == b
**		+1 if a > b
**
*/

static int
workcmpf6(a, b)
	register WORK *a;
	register WORK *b;
{
	if (a->w_mtime > b->w_mtime)
		return 1;
	else if (a->w_mtime < b->w_mtime)
		return -1;
	else
		return 0;
}
#if _FFR_RHS
/*
**  WORKCMPF7 -- compare function for ordering work based on shuffled host name.
**
**	Sorts on lock status, host name, and priority in that order.
**
**	Parameters:
**		a -- the first argument.
**		b -- the second argument.
**
**	Returns:
**		<0 if a < b
**		 0 if a == b
**		>0 if a > b
**
*/

static int
workcmpf7(a, b)
	register WORK *a;
	register WORK *b;
{
	int i;

	/* lock status */
	if (a->w_lock != b->w_lock)
		return a->w_lock - b->w_lock;

	/* host name */
	if (a->w_host != NULL && b->w_host == NULL)
		return 1;
	else if (a->w_host == NULL && b->w_host != NULL)
		return -1;
	if (a->w_host != NULL && b->w_host != NULL &&
	    (i = sm_strshufflecmp(a->w_host, b->w_host)) != 0)
		return i;

	/* job priority */
	return workcmpf0(a, b);
}
#endif /* _FFR_RHS */
/*
**  STRREV -- reverse string
**
**	Returns a pointer to a new string that is the reverse of
**	the string pointed to by fwd.  The space for the new
**	string is obtained using xalloc().
**
**	Parameters:
**		fwd -- the string to reverse.
**
**	Returns:
**		the reversed string.
*/

static char *
strrev(fwd)
	char *fwd;
{
	char *rev = NULL;
	int len, cnt;

	len = strlen(fwd);
	rev = xalloc(len + 1);
	for (cnt = 0; cnt < len; ++cnt)
		rev[cnt] = fwd[len - cnt - 1];
	rev[len] = '\0';
	return rev;
}

#if _FFR_RHS

# define NASCII	128
# define NCHAR	256

static unsigned char ShuffledAlphabet[NCHAR];

void
init_shuffle_alphabet()
{
	static bool init = false;
	int i;

	if (init)
		return;

	/* fill the ShuffledAlphabet */
	for (i = 0; i < NASCII; i++)
		ShuffledAlphabet[i] = i;

	/* mix it */
	for (i = 1; i < NASCII; i++)
	{
		register int j = get_random() % NASCII;
		register int tmp;

		tmp = ShuffledAlphabet[j];
		ShuffledAlphabet[j] = ShuffledAlphabet[i];
		ShuffledAlphabet[i] = tmp;
	}

	/* make it case insensitive */
	for (i = 'A'; i <= 'Z'; i++)
		ShuffledAlphabet[i] = ShuffledAlphabet[i + 'a' - 'A'];

	/* fill the upper part */
	for (i = 0; i < NASCII; i++)
		ShuffledAlphabet[i + NASCII] = ShuffledAlphabet[i];
	init = true;
}

static int
sm_strshufflecmp(a, b)
	char *a;
	char *b;
{
	const unsigned char *us1 = (const unsigned char *) a;
	const unsigned char *us2 = (const unsigned char *) b;

	while (ShuffledAlphabet[*us1] == ShuffledAlphabet[*us2++])
	{
		if (*us1++ == '\0')
			return 0;
	}
	return (ShuffledAlphabet[*us1] - ShuffledAlphabet[*--us2]);
}
#endif /* _FFR_RHS */

/*
**  DOWORK -- do a work request.
**
**	Parameters:
**		qgrp -- the index of the queue group for the job.
**		qdir -- the index of the queue directory for the job.
**		id -- the ID of the job to run.
**		forkflag -- if set, run this in background.
**		requeueflag -- if set, reinstantiate the queue quickly.
**			This is used when expanding aliases in the queue.
**			If forkflag is also set, it doesn't wait for the
**			child.
**		e - the envelope in which to run it.
**
**	Returns:
**		process id of process that is running the queue job.
**
**	Side Effects:
**		The work request is satisfied if possible.
*/

pid_t
dowork(qgrp, qdir, id, forkflag, requeueflag, e)
	int qgrp;
	int qdir;
	char *id;
	bool forkflag;
	bool requeueflag;
	register ENVELOPE *e;
{
	register pid_t pid;
	SM_RPOOL_T *rpool;

	if (tTd(40, 1))
		sm_dprintf("dowork(%s/%s)\n", qid_printqueue(qgrp, qdir), id);

	/*
	**  Fork for work.
	*/

	if (forkflag)
	{
		/*
		**  Since the delivery may happen in a child and the
		**  parent does not wait, the parent may close the
		**  maps thereby removing any shared memory used by
		**  the map.  Therefore, close the maps now so the
		**  child will dynamically open them if necessary.
		*/

		closemaps(false);

		pid = fork();
		if (pid < 0)
		{
			syserr("dowork: cannot fork");
			return 0;
		}
		else if (pid > 0)
		{
			/* parent -- clean out connection cache */
			mci_flush(false, NULL);
		}
		else
		{
			/*
			**  Initialize exception stack and default exception
			**  handler for child process.
			*/

			/* Reset global flags */
			RestartRequest = NULL;
			RestartWorkGroup = false;
			ShutdownRequest = NULL;
			PendingSignal = 0;
			CurrentPid = getpid();
			sm_exc_newthread(fatal_error);

			/*
			**  See note above about SMTP processes and SIGCHLD.
			*/

			if (OpMode == MD_SMTP ||
			    OpMode == MD_DAEMON ||
			    MaxQueueChildren > 0)
			{
				proc_list_clear();
				sm_releasesignal(SIGCHLD);
				(void) sm_signal(SIGCHLD, SIG_DFL);
			}

			/* child -- error messages to the transcript */
			QuickAbort = OnlyOneError = false;
		}
	}
	else
	{
		pid = 0;
	}

	if (pid == 0)
	{
		/*
		**  CHILD
		**	Lock the control file to avoid duplicate deliveries.
		**		Then run the file as though we had just read it.
		**	We save an idea of the temporary name so we
		**		can recover on interrupt.
		*/

		if (forkflag)
		{
			/* Reset global flags */
			RestartRequest = NULL;
			RestartWorkGroup = false;
			ShutdownRequest = NULL;
			PendingSignal = 0;
		}

		/* set basic modes, etc. */
		sm_clear_events();
		clearstats();
		rpool = sm_rpool_new_x(NULL);
		clearenvelope(e, false, rpool);
		e->e_flags |= EF_QUEUERUN|EF_GLOBALERRS;
		set_delivery_mode(SM_DELIVER, e);
		e->e_errormode = EM_MAIL;
		e->e_id = id;
		e->e_qgrp = qgrp;
		e->e_qdir = qdir;
		GrabTo = UseErrorsTo = false;
		ExitStat = EX_OK;
		if (forkflag)
		{
			disconnect(1, e);
			set_op_mode(MD_QUEUERUN);
		}
		sm_setproctitle(true, e, "%s from queue", qid_printname(e));
		if (LogLevel > 76)
			sm_syslog(LOG_DEBUG, e->e_id, "dowork, pid=%d",
				  (int) CurrentPid);

		/* don't use the headers from sendmail.cf... */
		e->e_header = NULL;

		/* read the queue control file -- return if locked */
		if (!readqf(e, false))
		{
			if (tTd(40, 4) && e->e_id != NULL)
				sm_dprintf("readqf(%s) failed\n",
					qid_printname(e));
			e->e_id = NULL;
			if (forkflag)
				finis(false, true, EX_OK);
			else
			{
				/* adding this frees 8 bytes */
				clearenvelope(e, false, rpool);

				/* adding this frees 12 bytes */
				sm_rpool_free(rpool);
				e->e_rpool = NULL;
				return 0;
			}
		}

		e->e_flags |= EF_INQUEUE;
		eatheader(e, requeueflag, true);

		if (requeueflag)
			queueup(e, false, false);

		/* do the delivery */
		sendall(e, SM_DELIVER);

		/* finish up and exit */
		if (forkflag)
			finis(true, true, ExitStat);
		else
		{
			(void) dropenvelope(e, true, false);
			sm_rpool_free(rpool);
			e->e_rpool = NULL;
			e->e_message = NULL;
		}
	}
	e->e_id = NULL;
	return pid;
}

/*
**  DOWORKLIST -- process a list of envelopes as work requests
**
**	Similar to dowork(), except that after forking, it processes an
**	envelope and its siblings, treating each envelope as a work request.
**
**	Parameters:
**		el -- envelope to be processed including its siblings.
**		forkflag -- if set, run this in background.
**		requeueflag -- if set, reinstantiate the queue quickly.
**			This is used when expanding aliases in the queue.
**			If forkflag is also set, it doesn't wait for the
**			child.
**
**	Returns:
**		process id of process that is running the queue job.
**
**	Side Effects:
**		The work request is satisfied if possible.
*/

pid_t
doworklist(el, forkflag, requeueflag)
	ENVELOPE *el;
	bool forkflag;
	bool requeueflag;
{
	register pid_t pid;
	ENVELOPE *ei;

	if (tTd(40, 1))
		sm_dprintf("doworklist()\n");

	/*
	**  Fork for work.
	*/

	if (forkflag)
	{
		/*
		**  Since the delivery may happen in a child and the
		**  parent does not wait, the parent may close the
		**  maps thereby removing any shared memory used by
		**  the map.  Therefore, close the maps now so the
		**  child will dynamically open them if necessary.
		*/

		closemaps(false);

		pid = fork();
		if (pid < 0)
		{
			syserr("doworklist: cannot fork");
			return 0;
		}
		else if (pid > 0)
		{
			/* parent -- clean out connection cache */
			mci_flush(false, NULL);
		}
		else
		{
			/*
			**  Initialize exception stack and default exception
			**  handler for child process.
			*/

			/* Reset global flags */
			RestartRequest = NULL;
			RestartWorkGroup = false;
			ShutdownRequest = NULL;
			PendingSignal = 0;
			CurrentPid = getpid();
			sm_exc_newthread(fatal_error);

			/*
			**  See note above about SMTP processes and SIGCHLD.
			*/

			if (OpMode == MD_SMTP ||
			    OpMode == MD_DAEMON ||
			    MaxQueueChildren > 0)
			{
				proc_list_clear();
				sm_releasesignal(SIGCHLD);
				(void) sm_signal(SIGCHLD, SIG_DFL);
			}

			/* child -- error messages to the transcript */
			QuickAbort = OnlyOneError = false;
		}
	}
	else
	{
		pid = 0;
	}

	if (pid != 0)
		return pid;

	/*
	**  IN CHILD
	**	Lock the control file to avoid duplicate deliveries.
	**		Then run the file as though we had just read it.
	**	We save an idea of the temporary name so we
	**		can recover on interrupt.
	*/

	if (forkflag)
	{
		/* Reset global flags */
		RestartRequest = NULL;
		RestartWorkGroup = false;
		ShutdownRequest = NULL;
		PendingSignal = 0;
	}

	/* set basic modes, etc. */
	sm_clear_events();
	clearstats();
	GrabTo = UseErrorsTo = false;
	ExitStat = EX_OK;
	if (forkflag)
	{
		disconnect(1, el);
		set_op_mode(MD_QUEUERUN);
	}
	if (LogLevel > 76)
		sm_syslog(LOG_DEBUG, el->e_id, "doworklist, pid=%d",
			  (int) CurrentPid);

	for (ei = el; ei != NULL; ei = ei->e_sibling)
	{
		ENVELOPE e;
		SM_RPOOL_T *rpool;

		if (WILL_BE_QUEUED(ei->e_sendmode))
			continue;
		else if (QueueMode != QM_QUARANTINE &&
			 ei->e_quarmsg != NULL)
			continue;

		rpool = sm_rpool_new_x(NULL);
		clearenvelope(&e, true, rpool);
		e.e_flags |= EF_QUEUERUN|EF_GLOBALERRS;
		set_delivery_mode(SM_DELIVER, &e);
		e.e_errormode = EM_MAIL;
		e.e_id = ei->e_id;
		e.e_qgrp = ei->e_qgrp;
		e.e_qdir = ei->e_qdir;
		openxscript(&e);
		sm_setproctitle(true, &e, "%s from queue", qid_printname(&e));

		/* don't use the headers from sendmail.cf... */
		e.e_header = NULL;
		CurEnv = &e;

		/* read the queue control file -- return if locked */
		if (readqf(&e, false))
		{
			e.e_flags |= EF_INQUEUE;
			eatheader(&e, requeueflag, true);

			if (requeueflag)
				queueup(&e, false, false);

			/* do the delivery */
			sendall(&e, SM_DELIVER);
			(void) dropenvelope(&e, true, false);
		}
		else
		{
			if (tTd(40, 4) && e.e_id != NULL)
				sm_dprintf("readqf(%s) failed\n",
					qid_printname(&e));
		}
		sm_rpool_free(rpool);
		ei->e_id = NULL;
	}

	/* restore CurEnv */
	CurEnv = el;

	/* finish up and exit */
	if (forkflag)
		finis(true, true, ExitStat);
	return 0;
}
/*
**  READQF -- read queue file and set up environment.
**
**	Parameters:
**		e -- the envelope of the job to run.
**		openonly -- only open the qf (returned as e_lockfp)
**
**	Returns:
**		true if it successfully read the queue file.
**		false otherwise.
**
**	Side Effects:
**		The queue file is returned locked.
*/

static bool
readqf(e, openonly)
	register ENVELOPE *e;
	bool openonly;
{
	register SM_FILE_T *qfp;
	ADDRESS *ctladdr;
	struct stat st, stf;
	char *bp;
	int qfver = 0;
	long hdrsize = 0;
	register char *p;
	char *frcpt = NULL;
	char *orcpt = NULL;
	bool nomore = false;
	bool bogus = false;
	MODE_T qsafe;
	char *err;
	char qf[MAXPATHLEN];
	char buf[MAXLINE];
	int bufsize;

	/*
	**  Read and process the file.
	*/

	SM_REQUIRE(e != NULL);
	bp = NULL;
	(void) sm_strlcpy(qf, queuename(e, ANYQFL_LETTER), sizeof(qf));
	qfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, qf, SM_IO_RDWR_B, NULL);
	if (qfp == NULL)
	{
		int save_errno = errno;

		if (tTd(40, 8))
			sm_dprintf("readqf(%s): sm_io_open failure (%s)\n",
				qf, sm_errstring(errno));
		errno = save_errno;
		if (errno != ENOENT
		    )
			syserr("readqf: no control file %s", qf);
		RELEASE_QUEUE;
		return false;
	}

	if (!lockfile(sm_io_getinfo(qfp, SM_IO_WHAT_FD, NULL), qf, NULL,
		      LOCK_EX|LOCK_NB))
	{
		/* being processed by another queuer */
		if (Verbose)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%s: locked\n", e->e_id);
		if (tTd(40, 8))
			sm_dprintf("%s: locked\n", e->e_id);
		if (LogLevel > 19)
			sm_syslog(LOG_DEBUG, e->e_id, "locked");
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		RELEASE_QUEUE;
		return false;
	}

	RELEASE_QUEUE;

	/*
	**  Prevent locking race condition.
	**
	**  Process A: readqf(): qfp = fopen(qffile)
	**  Process B: queueup(): rename(tf, qf)
	**  Process B: unlocks(tf)
	**  Process A: lockfile(qf);
	**
	**  Process A (us) has the old qf file (before the rename deleted
	**  the directory entry) and will be delivering based on old data.
	**  This can lead to multiple deliveries of the same recipients.
	**
	**  Catch this by checking if the underlying qf file has changed
	**  *after* acquiring our lock and if so, act as though the file
	**  was still locked (i.e., just return like the lockfile() case
	**  above.
	*/

	if (stat(qf, &stf) < 0 ||
	    fstat(sm_io_getinfo(qfp, SM_IO_WHAT_FD, NULL), &st) < 0)
	{
		/* must have been being processed by someone else */
		if (tTd(40, 8))
			sm_dprintf("readqf(%s): [f]stat failure (%s)\n",
				qf, sm_errstring(errno));
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}

	if (st.st_nlink != stf.st_nlink ||
	    st.st_dev != stf.st_dev ||
	    ST_INODE(st) != ST_INODE(stf) ||
#if HAS_ST_GEN && 0		/* AFS returns garbage in st_gen */
	    st.st_gen != stf.st_gen ||
#endif /* HAS_ST_GEN && 0 */
	    st.st_uid != stf.st_uid ||
	    st.st_gid != stf.st_gid ||
	    st.st_size != stf.st_size)
	{
		/* changed after opened */
		if (Verbose)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%s: changed\n", e->e_id);
		if (tTd(40, 8))
			sm_dprintf("%s: changed\n", e->e_id);
		if (LogLevel > 19)
			sm_syslog(LOG_DEBUG, e->e_id, "changed");
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}

	/*
	**  Check the queue file for plausibility to avoid attacks.
	*/

	qsafe = S_IWOTH|S_IWGRP;
	if (bitset(S_IWGRP, QueueFileMode))
		qsafe &= ~S_IWGRP;

	bogus = st.st_uid != geteuid() &&
		st.st_uid != TrustedUid &&
		geteuid() != RealUid;

	/*
	**  If this qf file results from a set-group-ID binary, then
	**  we check whether the directory is group-writable,
	**  the queue file mode contains the group-writable bit, and
	**  the groups are the same.
	**  Notice: this requires that the set-group-ID binary is used to
	**  run the queue!
	*/

	if (bogus && st.st_gid == getegid() && UseMSP)
	{
		char delim;
		struct stat dst;

		bp = SM_LAST_DIR_DELIM(qf);
		if (bp == NULL)
			delim = '\0';
		else
		{
			delim = *bp;
			*bp = '\0';
		}
		if (stat(delim == '\0' ? "." : qf, &dst) < 0)
			syserr("readqf: cannot stat directory %s",
				delim == '\0' ? "." : qf);
		else
		{
			bogus = !(bitset(S_IWGRP, QueueFileMode) &&
				  bitset(S_IWGRP, dst.st_mode) &&
				  dst.st_gid == st.st_gid);
		}
		if (delim != '\0')
			*bp = delim;
		bp = NULL;
	}
	if (!bogus)
		bogus = bitset(qsafe, st.st_mode);
	if (bogus)
	{
		if (LogLevel > 0)
		{
			sm_syslog(LOG_ALERT, e->e_id,
				  "bogus queue file, uid=%ld, gid=%ld, mode=%o",
				  (long) st.st_uid, (long) st.st_gid,
				  (unsigned int) st.st_mode);
		}
		if (tTd(40, 8))
			sm_dprintf("readqf(%s): bogus file\n", qf);
		e->e_flags |= EF_INQUEUE;
		if (!openonly)
			loseqfile(e, "bogus file uid/gid in mqueue");
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}

	if (st.st_size == 0)
	{
		/* must be a bogus file -- if also old, just remove it */
		if (!openonly && st.st_ctime + 10 * 60 < curtime())
		{
			(void) xunlink(queuename(e, DATAFL_LETTER));
			(void) xunlink(queuename(e, ANYQFL_LETTER));
		}
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}

	if (st.st_nlink == 0)
	{
		/*
		**  Race condition -- we got a file just as it was being
		**  unlinked.  Just assume it is zero length.
		*/

		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}

#if _FFR_TRUSTED_QF
	/*
	**  If we don't own the file mark it as unsafe.
	**  However, allow TrustedUser to own it as well
	**  in case TrustedUser manipulates the queue.
	*/

	if (st.st_uid != geteuid() && st.st_uid != TrustedUid)
		e->e_flags |= EF_UNSAFE;
#else /* _FFR_TRUSTED_QF */
	/* If we don't own the file mark it as unsafe */
	if (st.st_uid != geteuid())
		e->e_flags |= EF_UNSAFE;
#endif /* _FFR_TRUSTED_QF */

	/* good file -- save this lock */
	e->e_lockfp = qfp;

	/* Just wanted the open file */
	if (openonly)
		return true;

	/* do basic system initialization */
	initsys(e);
	macdefine(&e->e_macro, A_PERM, 'i', e->e_id);

	LineNumber = 0;
	e->e_flags |= EF_GLOBALERRS;
	set_op_mode(MD_QUEUERUN);
	ctladdr = NULL;
	e->e_qfletter = queue_letter(e, ANYQFL_LETTER);
	e->e_dfqgrp = e->e_qgrp;
	e->e_dfqdir = e->e_qdir;
#if _FFR_QUEUE_MACRO
	macdefine(&e->e_macro, A_TEMP, macid("{queue}"),
		  qid_printqueue(e->e_qgrp, e->e_qdir));
#endif /* _FFR_QUEUE_MACRO */
	e->e_dfino = -1;
	e->e_msgsize = -1;
	while (bufsize = sizeof(buf),
	       (bp = fgetfolded(buf, &bufsize, qfp)) != NULL)
	{
		unsigned long qflags;
		ADDRESS *q;
		int r;
		time_t now;
		auto char *ep;

		if (tTd(40, 4))
			sm_dprintf("+++++ %s\n", bp);
		if (nomore)
		{
			/* hack attack */
  hackattack:
			syserr("SECURITY ALERT: extra or bogus data in queue file: %s",
			       bp);
			err = "bogus queue line";
			goto fail;
		}
		switch (bp[0])
		{
		  case 'A':		/* AUTH= parameter */
			if (!xtextok(&bp[1]))
				goto hackattack;
			e->e_auth_param = sm_rpool_strdup_x(e->e_rpool, &bp[1]);
			break;

		  case 'B':		/* body type */
			r = check_bodytype(&bp[1]);
			if (!BODYTYPE_VALID(r))
				goto hackattack;
			e->e_bodytype = sm_rpool_strdup_x(e->e_rpool, &bp[1]);
			break;

		  case 'C':		/* specify controlling user */
			ctladdr = setctluser(&bp[1], qfver, e);
			break;

		  case 'D':		/* data file name */
			/* obsolete -- ignore */
			break;

		  case 'd':		/* data file directory name */
			{
				int qgrp, qdir;

#if _FFR_MSP_PARANOIA
				/* forbid queue groups in MSP? */
				if (UseMSP)
					goto hackattack;
#endif /* _FFR_MSP_PARANOIA */
				for (qgrp = 0;
				     qgrp < NumQueue && Queue[qgrp] != NULL;
				     ++qgrp)
				{
					for (qdir = 0;
					     qdir < Queue[qgrp]->qg_numqueues;
					     ++qdir)
					{
						if (strcmp(&bp[1],
							   Queue[qgrp]->qg_qpaths[qdir].qp_name)
						    == 0)
						{
							e->e_dfqgrp = qgrp;
							e->e_dfqdir = qdir;
							goto done;
						}
					}
				}
				err = "bogus queue file directory";
				goto fail;
			  done:
				break;
			}

		  case 'E':		/* specify error recipient */
			/* no longer used */
			break;

		  case 'F':		/* flag bits */
			if (strncmp(bp, "From ", 5) == 0)
			{
				/* we are being spoofed! */
				syserr("SECURITY ALERT: bogus qf line %s", bp);
				err = "bogus queue line";
				goto fail;
			}
			for (p = &bp[1]; *p != '\0'; p++)
			{
				switch (*p)
				{
				  case '8':	/* has 8 bit data */
					e->e_flags |= EF_HAS8BIT;
					break;

				  case 'b':	/* delete Bcc: header */
					e->e_flags |= EF_DELETE_BCC;
					break;

				  case 'd':	/* envelope has DSN RET= */
					e->e_flags |= EF_RET_PARAM;
					break;

				  case 'n':	/* don't return body */
					e->e_flags |= EF_NO_BODY_RETN;
					break;

				  case 'r':	/* response */
					e->e_flags |= EF_RESPONSE;
					break;

				  case 's':	/* split */
					e->e_flags |= EF_SPLIT;
					break;

				  case 'w':	/* warning sent */
					e->e_flags |= EF_WARNING;
					break;
				}
			}
			break;

		  case 'q':		/* quarantine reason */
			e->e_quarmsg = sm_rpool_strdup_x(e->e_rpool, &bp[1]);
			macdefine(&e->e_macro, A_PERM,
				  macid("{quarantine}"), e->e_quarmsg);
			break;

		  case 'H':		/* header */

			/*
			**  count size before chompheader() destroys the line.
			**  this isn't accurate due to macro expansion, but
			**  better than before. "-3" to skip H?? at least.
			*/

			hdrsize += strlen(bp) - 3;
			(void) chompheader(&bp[1], CHHDR_QUEUE, NULL, e);
			break;

		  case 'I':		/* data file's inode number */
			/* regenerated below */
			break;

		  case 'K':		/* time of last delivery attempt */
			e->e_dtime = atol(&buf[1]);
			break;

		  case 'L':		/* Solaris Content-Length: */
		  case 'M':		/* message */
			/* ignore this; we want a new message next time */
			break;

		  case 'N':		/* number of delivery attempts */
			e->e_ntries = atoi(&buf[1]);

			/* if this has been tried recently, let it be */
			now = curtime();
			if (e->e_ntries > 0 && e->e_dtime <= now &&
			    now < e->e_dtime + MinQueueAge)
			{
				char *howlong;

				howlong = pintvl(now - e->e_dtime, true);
				if (Verbose)
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "%s: too young (%s)\n",
							     e->e_id, howlong);
				if (tTd(40, 8))
					sm_dprintf("%s: too young (%s)\n",
						e->e_id, howlong);
				if (LogLevel > 19)
					sm_syslog(LOG_DEBUG, e->e_id,
						  "too young (%s)",
						  howlong);
				e->e_id = NULL;
				unlockqueue(e);
				if (bp != buf)
					sm_free(bp);
				return false;
			}
			macdefine(&e->e_macro, A_TEMP,
				macid("{ntries}"), &buf[1]);

#if NAMED_BIND
			/* adjust BIND parameters immediately */
			if (e->e_ntries == 0)
			{
				_res.retry = TimeOuts.res_retry[RES_TO_FIRST];
				_res.retrans = TimeOuts.res_retrans[RES_TO_FIRST];
			}
			else
			{
				_res.retry = TimeOuts.res_retry[RES_TO_NORMAL];
				_res.retrans = TimeOuts.res_retrans[RES_TO_NORMAL];
			}
#endif /* NAMED_BIND */
			break;

		  case 'P':		/* message priority */
			e->e_msgpriority = atol(&bp[1]) + WkTimeFact;
			break;

		  case 'Q':		/* original recipient */
			orcpt = sm_rpool_strdup_x(e->e_rpool, &bp[1]);
			break;

		  case 'r':		/* final recipient */
			frcpt = sm_rpool_strdup_x(e->e_rpool, &bp[1]);
			break;

		  case 'R':		/* specify recipient */
			p = bp;
			qflags = 0;
			if (qfver >= 1)
			{
				/* get flag bits */
				while (*++p != '\0' && *p != ':')
				{
					switch (*p)
					{
					  case 'N':
						qflags |= QHASNOTIFY;
						break;

					  case 'S':
						qflags |= QPINGONSUCCESS;
						break;

					  case 'F':
						qflags |= QPINGONFAILURE;
						break;

					  case 'D':
						qflags |= QPINGONDELAY;
						break;

					  case 'P':
						qflags |= QPRIMARY;
						break;

					  case 'A':
						if (ctladdr != NULL)
							ctladdr->q_flags |= QALIAS;
						break;

					  case 'B':
						qflags |= QINTBCC;
						break;

					  case QDYNMAILFLG:
						qflags |= QDYNMAILER;
						break;

					  default: /* ignore or complain? */
						break;
					}
				}
			}
			else
				qflags |= QPRIMARY;
			macdefine(&e->e_macro, A_PERM, macid("{addr_type}"),
				((qflags & QINTBCC) != 0) ? "e b" : "e r");
			if (*p != '\0')
				q = parseaddr(++p, NULLADDR, RF_COPYALL, '\0',
						NULL, e, true);
			else
				q = NULL;
			if (q != NULL)
			{
				/* make sure we keep the current qgrp */
				if (ISVALIDQGRP(e->e_qgrp))
					q->q_qgrp = e->e_qgrp;
				q->q_alias = ctladdr;
				if (qfver >= 1)
					q->q_flags &= ~Q_PINGFLAGS;
				q->q_flags |= qflags;
				q->q_finalrcpt = frcpt;
				q->q_orcpt = orcpt;
#if _FFR_RCPTFLAGS
				if (bitset(QDYNMAILER, qflags))
					newmodmailer(q, QDYNMAILFLG);
#endif
				(void) recipient(q, &e->e_sendqueue, 0, e);
			}
			frcpt = NULL;
			orcpt = NULL;
			macdefine(&e->e_macro, A_PERM, macid("{addr_type}"),
				NULL);
			break;

		  case 'S':		/* sender */
			setsender(sm_rpool_strdup_x(e->e_rpool, &bp[1]),
				  e, NULL, '\0', true);
			break;

		  case 'T':		/* init time */
			e->e_ctime = atol(&bp[1]);
			break;

		  case 'V':		/* queue file version number */
			qfver = atoi(&bp[1]);
			if (qfver <= QF_VERSION)
				break;
			syserr("Version number in queue file (%d) greater than max (%d)",
				qfver, QF_VERSION);
			err = "unsupported queue file version";
			goto fail;
			/* NOTREACHED */
			break;

		  case 'Z':		/* original envelope id from ESMTP */
			e->e_envid = sm_rpool_strdup_x(e->e_rpool, &bp[1]);
			macdefine(&e->e_macro, A_PERM,
				macid("{dsn_envid}"), e->e_envid);
			break;

		  case '!':		/* deliver by */

			/* format: flag (1 char) space long-integer */
			e->e_dlvr_flag = buf[1];
			e->e_deliver_by = strtol(&buf[3], NULL, 10);

		  case '$':		/* define macro */
			{
				char *p;

				/* XXX elimate p? */
				r = macid_parse(&bp[1], &ep);
				if (r == 0)
					break;
				p = sm_rpool_strdup_x(e->e_rpool, ep);
				macdefine(&e->e_macro, A_PERM, r, p);
			}
			break;

		  case '.':		/* terminate file */
			nomore = true;
			break;

		  default:
			syserr("readqf: %s: line %d: bad line \"%s\"",
				qf, LineNumber, shortenstring(bp, MAXSHORTSTR));
			err = "unrecognized line";
			goto fail;
		}

		if (bp != buf)
			SM_FREE(bp);
	}

	/*
	**  If we haven't read any lines, this queue file is empty.
	**  Arrange to remove it without referencing any null pointers.
	*/

	if (LineNumber == 0)
	{
		errno = 0;
		e->e_flags |= EF_CLRQUEUE|EF_FATALERRS|EF_RESPONSE;
		return true;
	}

	/* Check to make sure we have a complete queue file read */
	if (!nomore)
	{
		syserr("readqf: %s: incomplete queue file read", qf);
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}

#if _FFR_QF_PARANOIA
	/* Check to make sure key fields were read */
	if (e->e_from.q_mailer == NULL)
	{
		syserr("readqf: %s: sender not specified in queue file", qf);
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
		return false;
	}
	/* other checks? */
#endif /* _FFR_QF_PARANOIA */

	/* possibly set ${dsn_ret} macro */
	if (bitset(EF_RET_PARAM, e->e_flags))
	{
		if (bitset(EF_NO_BODY_RETN, e->e_flags))
			macdefine(&e->e_macro, A_PERM,
				macid("{dsn_ret}"), "hdrs");
		else
			macdefine(&e->e_macro, A_PERM,
				macid("{dsn_ret}"), "full");
	}

	/*
	**  Arrange to read the data file.
	*/

	p = queuename(e, DATAFL_LETTER);
	e->e_dfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, p, SM_IO_RDONLY_B,
			      NULL);
	if (e->e_dfp == NULL)
	{
		syserr("readqf: cannot open %s", p);
	}
	else
	{
		e->e_flags |= EF_HAS_DF;
		if (fstat(sm_io_getinfo(e->e_dfp, SM_IO_WHAT_FD, NULL), &st)
		    >= 0)
		{
			e->e_msgsize = st.st_size + hdrsize;
			e->e_dfdev = st.st_dev;
			e->e_dfino = ST_INODE(st);
			(void) sm_snprintf(buf, sizeof(buf), "%ld",
					   PRT_NONNEGL(e->e_msgsize));
			macdefine(&e->e_macro, A_TEMP, macid("{msg_size}"),
				  buf);
		}
	}

	return true;

  fail:
	/*
	**  There was some error reading the qf file (reason is in err var.)
	**  Cleanup:
	**	close file; clear e_lockfp since it is the same as qfp,
	**	hence it is invalid (as file) after qfp is closed;
	**	the qf file is on disk, so set the flag to avoid calling
	**	queueup() with bogus data.
	*/

	if (bp != buf)
		SM_FREE(bp);
	if (qfp != NULL)
		(void) sm_io_close(qfp, SM_TIME_DEFAULT);
	e->e_lockfp = NULL;
	e->e_flags |= EF_INQUEUE;
	loseqfile(e, err);
	return false;
}
/*
**  PRTSTR -- print a string, "unprintable" characters are shown as \oct
**
**	Parameters:
**		s -- string to print
**		ml -- maximum length of output
**
**	Returns:
**		number of entries
**
**	Side Effects:
**		Prints a string on stdout.
*/

static void prtstr __P((char *, int));

#if _FFR_BOUNCE_QUEUE
# define SKIP_BOUNCE_QUEUE	\
		if (i == BounceQueue)	\
			continue;
#else
# define SKIP_BOUNCE_QUEUE
#endif

static void
prtstr(s, ml)
	char *s;
	int ml;
{
	int c;

	if (s == NULL)
		return;
	while (ml-- > 0 && ((c = *s++) != '\0'))
	{
		if (c == '\\')
		{
			if (ml-- > 0)
			{
				(void) sm_io_putc(smioout, SM_TIME_DEFAULT, c);
				(void) sm_io_putc(smioout, SM_TIME_DEFAULT, c);
			}
		}
		else if (isascii(c) && isprint(c))
			(void) sm_io_putc(smioout, SM_TIME_DEFAULT, c);
		else
		{
			if ((ml -= 3) > 0)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "\\%03o", c & 0xFF);
		}
	}
}
/*
**  PRINTNQE -- print out number of entries in the mail queue
**
**	Parameters:
**		out -- output file pointer.
**		prefix -- string to output in front of each line.
**
**	Returns:
**		none.
*/

void
printnqe(out, prefix)
	SM_FILE_T *out;
	char *prefix;
{
#if SM_CONF_SHM
	int i, k = 0, nrequests = 0;
	bool unknown = false;

	if (ShmId == SM_SHM_NO_ID)
	{
		if (prefix == NULL)
			(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
					"Data unavailable: shared memory not updated\n");
		else
			(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
					"%sNOTCONFIGURED:-1\r\n", prefix);
		return;
	}
	for (i = 0; i < NumQueue && Queue[i] != NULL; i++)
	{
		int j;

		SKIP_BOUNCE_QUEUE
		k++;
		for (j = 0; j < Queue[i]->qg_numqueues; j++)
		{
			int n;

			if (StopRequest)
				stop_sendmail();

			n = QSHM_ENTRIES(Queue[i]->qg_qpaths[j].qp_idx);
			if (prefix != NULL)
				(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
					"%s%s:%d\r\n",
					prefix, qid_printqueue(i, j), n);
			else if (n < 0)
			{
				(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
					"%s: unknown number of entries\n",
					qid_printqueue(i, j));
				unknown = true;
			}
			else if (n == 0)
			{
				(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
					"%s is empty\n",
					qid_printqueue(i, j));
			}
			else if (n > 0)
			{
				(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
					"%s: entries=%d\n",
					qid_printqueue(i, j), n);
				nrequests += n;
				k++;
			}
		}
	}
	if (prefix == NULL && k > 1)
		(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
				     "\t\tTotal requests: %d%s\n",
				     nrequests, unknown ? " (about)" : "");
#else /* SM_CONF_SHM */
	if (prefix == NULL)
		(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
			     "Data unavailable without shared memory support\n");
	else
		(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
			     "%sNOTAVAILABLE:-1\r\n", prefix);
#endif /* SM_CONF_SHM */
}
/*
**  PRINTQUEUE -- print out a representation of the mail queue
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Prints a listing of the mail queue on the standard output.
*/

void
printqueue()
{
	int i, k = 0, nrequests = 0;

	for (i = 0; i < NumQueue && Queue[i] != NULL; i++)
	{
		int j;

		k++;
		for (j = 0; j < Queue[i]->qg_numqueues; j++)
		{
			if (StopRequest)
				stop_sendmail();
			nrequests += print_single_queue(i, j);
			k++;
		}
	}
	if (k > 1)
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "\t\tTotal requests: %d\n",
				     nrequests);
}
/*
**  PRINT_SINGLE_QUEUE -- print out a representation of a single mail queue
**
**	Parameters:
**		qgrp -- the index of the queue group.
**		qdir -- the queue directory.
**
**	Returns:
**		number of requests in mail queue.
**
**	Side Effects:
**		Prints a listing of the mail queue on the standard output.
*/

int
print_single_queue(qgrp, qdir)
	int qgrp;
	int qdir;
{
	register WORK *w;
	SM_FILE_T *f;
	int nrequests;
	char qd[MAXPATHLEN];
	char qddf[MAXPATHLEN];
	char buf[MAXLINE];

	if (qdir == NOQDIR)
	{
		(void) sm_strlcpy(qd, ".", sizeof(qd));
		(void) sm_strlcpy(qddf, ".", sizeof(qddf));
	}
	else
	{
		(void) sm_strlcpyn(qd, sizeof(qd), 2,
			Queue[qgrp]->qg_qpaths[qdir].qp_name,
			(bitset(QP_SUBQF,
				Queue[qgrp]->qg_qpaths[qdir].qp_subdirs)
					? "/qf" : ""));
		(void) sm_strlcpyn(qddf, sizeof(qddf), 2,
			Queue[qgrp]->qg_qpaths[qdir].qp_name,
			(bitset(QP_SUBDF,
				Queue[qgrp]->qg_qpaths[qdir].qp_subdirs)
					? "/df" : ""));
	}

	/*
	**  Check for permission to print the queue
	*/

	if (bitset(PRIV_RESTRICTMAILQ, PrivacyFlags) && RealUid != 0)
	{
		struct stat st;
#ifdef NGROUPS_MAX
		int n;
		extern GIDSET_T InitialGidSet[NGROUPS_MAX];
#endif /* NGROUPS_MAX */

		if (stat(qd, &st) < 0)
		{
			syserr("Cannot stat %s",
				qid_printqueue(qgrp, qdir));
			return 0;
		}
#ifdef NGROUPS_MAX
		n = NGROUPS_MAX;
		while (--n >= 0)
		{
			if (InitialGidSet[n] == st.st_gid)
				break;
		}
		if (n < 0 && RealGid != st.st_gid)
#else /* NGROUPS_MAX */
		if (RealGid != st.st_gid)
#endif /* NGROUPS_MAX */
		{
			usrerr("510 You are not permitted to see the queue");
			setstat(EX_NOPERM);
			return 0;
		}
	}

	/*
	**  Read and order the queue.
	*/

	nrequests = gatherq(qgrp, qdir, true, NULL, NULL, NULL);
	(void) sortq(Queue[qgrp]->qg_maxlist);

	/*
	**  Print the work list that we have read.
	*/

	/* first see if there is anything */
	if (nrequests <= 0)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%s is empty\n",
				     qid_printqueue(qgrp, qdir));
		return 0;
	}

	sm_getla();	/* get load average */

	(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "\t\t%s (%d request%s",
			     qid_printqueue(qgrp, qdir),
			     nrequests, nrequests == 1 ? "" : "s");
	if (MaxQueueRun > 0 && nrequests > MaxQueueRun)
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     ", only %d printed", MaxQueueRun);
	if (Verbose)
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
			")\n-----Q-ID----- --Size-- -Priority- ---Q-Time--- --------Sender/Recipient--------\n");
	else
		(void) sm_io_fprintf(smioout,  SM_TIME_DEFAULT,
			")\n-----Q-ID----- --Size-- -----Q-Time----- ------------Sender/Recipient-----------\n");
	for (w = WorkQ; w != NULL; w = w->w_next)
	{
		struct stat st;
		auto time_t submittime = 0;
		long dfsize;
		int flags = 0;
		int qfver;
		char quarmsg[MAXLINE];
		char statmsg[MAXLINE];
		char bodytype[MAXNAME + 1];
		char qf[MAXPATHLEN];

		if (StopRequest)
			stop_sendmail();

		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%13s",
				     w->w_name + 2);
		(void) sm_strlcpyn(qf, sizeof(qf), 3, qd, "/", w->w_name);
		f = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, qf, SM_IO_RDONLY_B,
			       NULL);
		if (f == NULL)
		{
			if (errno == EPERM)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     " (permission denied)\n");
			else if (errno == ENOENT)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     " (job completed)\n");
			else
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     " (%s)\n",
						     sm_errstring(errno));
			errno = 0;
			continue;
		}
		w->w_name[0] = DATAFL_LETTER;
		(void) sm_strlcpyn(qf, sizeof(qf), 3, qddf, "/", w->w_name);
		if (stat(qf, &st) >= 0)
			dfsize = st.st_size;
		else
		{
			ENVELOPE e;

			/*
			**  Maybe the df file can't be statted because
			**  it is in a different directory than the qf file.
			**  In order to find out, we must read the qf file.
			*/

			newenvelope(&e, &BlankEnvelope, sm_rpool_new_x(NULL));
			e.e_id = w->w_name + 2;
			e.e_qgrp = qgrp;
			e.e_qdir = qdir;
			dfsize = -1;
			if (readqf(&e, false))
			{
				char *df = queuename(&e, DATAFL_LETTER);
				if (stat(df, &st) >= 0)
					dfsize = st.st_size;
			}
			if (e.e_lockfp != NULL)
			{
				(void) sm_io_close(e.e_lockfp, SM_TIME_DEFAULT);
				e.e_lockfp = NULL;
			}
			clearenvelope(&e, false, e.e_rpool);
			sm_rpool_free(e.e_rpool);
		}
		if (w->w_lock)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "*");
		else if (QueueMode == QM_LOST)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "?");
		else if (w->w_tooyoung)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "-");
		else if (shouldqueue(w->w_pri, w->w_ctime))
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "X");
		else
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, " ");

		errno = 0;

		quarmsg[0] = '\0';
		statmsg[0] = bodytype[0] = '\0';
		qfver = 0;
		while (sm_io_fgets(f, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
		{
			register int i;
			register char *p;

			if (StopRequest)
				stop_sendmail();

			fixcrlf(buf, true);
			switch (buf[0])
			{
			  case 'V':	/* queue file version */
				qfver = atoi(&buf[1]);
				break;

			  case 'M':	/* error message */
				if ((i = strlen(&buf[1])) >= sizeof(statmsg))
					i = sizeof(statmsg) - 1;
				memmove(statmsg, &buf[1], i);
				statmsg[i] = '\0';
				break;

			  case 'q':	/* quarantine reason */
				if ((i = strlen(&buf[1])) >= sizeof(quarmsg))
					i = sizeof(quarmsg) - 1;
				memmove(quarmsg, &buf[1], i);
				quarmsg[i] = '\0';
				break;

			  case 'B':	/* body type */
				if ((i = strlen(&buf[1])) >= sizeof(bodytype))
					i = sizeof(bodytype) - 1;
				memmove(bodytype, &buf[1], i);
				bodytype[i] = '\0';
				break;

			  case 'S':	/* sender name */
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
						SM_TIME_DEFAULT,
						"%8ld %10ld%c%.12s ",
						dfsize,
						w->w_pri,
						bitset(EF_WARNING, flags)
							? '+' : ' ',
						ctime(&submittime) + 4);
					prtstr(&buf[1], 78);
				}
				else
				{
					(void) sm_io_fprintf(smioout,
						SM_TIME_DEFAULT,
						"%8ld %.16s ",
						dfsize,
						ctime(&submittime));
					prtstr(&buf[1], 39);
				}

				if (quarmsg[0] != '\0')
				{
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "\n     QUARANTINE: %.*s",
							     Verbose ? 100 : 60,
							     quarmsg);
					quarmsg[0] = '\0';
				}

				if (statmsg[0] != '\0' || bodytype[0] != '\0')
				{
					(void) sm_io_fprintf(smioout,
						SM_TIME_DEFAULT,
						"\n    %10.10s",
						bodytype);
					if (statmsg[0] != '\0')
						(void) sm_io_fprintf(smioout,
							SM_TIME_DEFAULT,
							"   (%.*s)",
							Verbose ? 100 : 60,
							statmsg);
					statmsg[0] = '\0';
				}
				break;

			  case 'C':	/* controlling user */
				if (Verbose)
					(void) sm_io_fprintf(smioout,
						SM_TIME_DEFAULT,
						"\n\t\t\t\t\t\t(---%.64s---)",
						&buf[1]);
				break;

			  case 'R':	/* recipient name */
				p = &buf[1];
				if (qfver >= 1)
				{
					p = strchr(p, ':');
					if (p == NULL)
						break;
					p++;
				}
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
							SM_TIME_DEFAULT,
							"\n\t\t\t\t\t\t");
					prtstr(p, 71);
				}
				else
				{
					(void) sm_io_fprintf(smioout,
							SM_TIME_DEFAULT,
							"\n\t\t\t\t\t ");
					prtstr(p, 38);
				}
				if (Verbose && statmsg[0] != '\0')
				{
					(void) sm_io_fprintf(smioout,
							SM_TIME_DEFAULT,
							"\n\t\t (%.100s)",
							statmsg);
					statmsg[0] = '\0';
				}
				break;

			  case 'T':	/* creation time */
				submittime = atol(&buf[1]);
				break;

			  case 'F':	/* flag bits */
				for (p = &buf[1]; *p != '\0'; p++)
				{
					switch (*p)
					{
					  case 'w':
						flags |= EF_WARNING;
						break;
					}
				}
			}
		}
		if (submittime == (time_t) 0)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     " (no control file)");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "\n");
		(void) sm_io_close(f, SM_TIME_DEFAULT);
	}
	return nrequests;
}

/*
**  QUEUE_LETTER -- get the proper queue letter for the current QueueMode.
**
**	Parameters:
**		e -- envelope to build it in/from.
**		type -- the file type, used as the first character
**			of the file name.
**
**	Returns:
**		the letter to use
*/

static char
queue_letter(e, type)
	ENVELOPE *e;
	int type;
{
	/* Change type according to QueueMode */
	if (type == ANYQFL_LETTER)
	{
		if (e->e_quarmsg != NULL)
			type = QUARQF_LETTER;
		else
		{
			switch (QueueMode)
			{
			  case QM_NORMAL:
				type = NORMQF_LETTER;
				break;

			  case QM_QUARANTINE:
				type = QUARQF_LETTER;
				break;

			  case QM_LOST:
				type = LOSEQF_LETTER;
				break;

			  default:
				/* should never happen */
				abort();
				/* NOTREACHED */
			}
		}
	}
	return type;
}

/*
**  QUEUENAME -- build a file name in the queue directory for this envelope.
**
**	Parameters:
**		e -- envelope to build it in/from.
**		type -- the file type, used as the first character
**			of the file name.
**
**	Returns:
**		a pointer to the queue name (in a static buffer).
**
**	Side Effects:
**		If no id code is already assigned, queuename() will
**		assign an id code with assign_queueid().  If no queue
**		directory is assigned, one will be set with setnewqueue().
*/

char *
queuename(e, type)
	register ENVELOPE *e;
	int type;
{
	int qd, qg;
	char *sub = "/";
	char pref[3];
	static char buf[MAXPATHLEN];

	/* Assign an ID if needed */
	if (e->e_id == NULL)
	{
		if (IntSig)
			return NULL;
		assign_queueid(e);
	}
	type = queue_letter(e, type);

	/* begin of filename */
	pref[0] = (char) type;
	pref[1] = 'f';
	pref[2] = '\0';

	/* Assign a queue group/directory if needed */
	if (type == XSCRPT_LETTER)
	{
		/*
		**  We don't want to call setnewqueue() if we are fetching
		**  the pathname of the transcript file, because setnewqueue
		**  chooses a queue, and sometimes we need to write to the
		**  transcript file before we have gathered enough information
		**  to choose a queue.
		*/

		if (e->e_xfqgrp == NOQGRP || e->e_xfqdir == NOQDIR)
		{
			if (e->e_qgrp != NOQGRP && e->e_qdir != NOQDIR)
			{
				e->e_xfqgrp = e->e_qgrp;
				e->e_xfqdir = e->e_qdir;
			}
			else
			{
				e->e_xfqgrp = 0;
				if (Queue[e->e_xfqgrp]->qg_numqueues <= 1)
					e->e_xfqdir = 0;
				else
				{
					e->e_xfqdir = get_rand_mod(
					      Queue[e->e_xfqgrp]->qg_numqueues);
				}
			}
		}
		qd = e->e_xfqdir;
		qg = e->e_xfqgrp;
	}
	else
	{
		if (e->e_qgrp == NOQGRP || e->e_qdir == NOQDIR)
		{
			if (IntSig)
				return NULL;
			(void) setnewqueue(e);
		}
		if (type ==  DATAFL_LETTER)
		{
			qd = e->e_dfqdir;
			qg = e->e_dfqgrp;
		}
		else
		{
			qd = e->e_qdir;
			qg = e->e_qgrp;
		}
	}

	/* xf files always have a valid qd and qg picked above */
	if ((qd == NOQDIR || qg == NOQGRP) && type != XSCRPT_LETTER)
		(void) sm_strlcpyn(buf, sizeof(buf), 2, pref, e->e_id);
	else
	{
		switch (type)
		{
		  case DATAFL_LETTER:
			if (bitset(QP_SUBDF, Queue[qg]->qg_qpaths[qd].qp_subdirs))
				sub = "/df/";
			break;

		  case QUARQF_LETTER:
		  case TEMPQF_LETTER:
		  case NEWQFL_LETTER:
		  case LOSEQF_LETTER:
		  case NORMQF_LETTER:
			if (bitset(QP_SUBQF, Queue[qg]->qg_qpaths[qd].qp_subdirs))
				sub = "/qf/";
			break;

		  case XSCRPT_LETTER:
			if (bitset(QP_SUBXF, Queue[qg]->qg_qpaths[qd].qp_subdirs))
				sub = "/xf/";
			break;

		  default:
			if (IntSig)
				return NULL;
			sm_abort("queuename: bad queue file type %d", type);
		}

		(void) sm_strlcpyn(buf, sizeof(buf), 4,
				Queue[qg]->qg_qpaths[qd].qp_name,
				sub, pref, e->e_id);
	}

	if (tTd(7, 2))
		sm_dprintf("queuename: %s\n", buf);
	return buf;
}

/*
**  INIT_QID_ALG -- Initialize the (static) parameters that are used to
**	generate a queue ID.
**
**	This function is called by the daemon to reset
**	LastQueueTime and LastQueuePid which are used by assign_queueid().
**	Otherwise the algorithm may cause problems because
**	LastQueueTime and LastQueuePid are set indirectly by main()
**	before the daemon process is started, hence LastQueuePid is not
**	the pid of the daemon and therefore a child of the daemon can
**	actually have the same pid as LastQueuePid which means the section
**	in  assign_queueid():
**	* see if we need to get a new base time/pid *
**	is NOT triggered which will cause the same queue id to be generated.
**
**	Parameters:
**		none
**
**	Returns:
**		none.
*/

void
init_qid_alg()
{
	LastQueueTime = 0;
	LastQueuePid = -1;
}

/*
**  ASSIGN_QUEUEID -- assign a queue ID for this envelope.
**
**	Assigns an id code if one does not already exist.
**	This code assumes that nothing will remain in the queue for
**	longer than 60 years.  It is critical that files with the given
**	name do not already exist in the queue.
**	[No longer initializes e_qdir to NOQDIR.]
**
**	Parameters:
**		e -- envelope to set it in.
**
**	Returns:
**		none.
*/

static const char QueueIdChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
# define QIC_LEN	60
# define QIC_LEN_R	62

/*
**  Note: the length is "officially" 60 because minutes and seconds are
**	usually only 0-59.  However (Linux):
**       tm_sec The number of seconds after the minute, normally in
**		the range 0 to 59, but can be up to 61 to allow for
**		leap seconds.
**	Hence the real length of the string is 62 to take this into account.
**	Alternatively % QIC_LEN can (should) be used for access everywhere.
*/

# define queuenextid() CurrentPid
#define QIC_LEN_SQR	(QIC_LEN * QIC_LEN)

void
assign_queueid(e)
	register ENVELOPE *e;
{
	pid_t pid = queuenextid();
	static unsigned int cX = 0;
	static unsigned int random_offset;
	struct tm *tm;
	char idbuf[MAXQFNAME - 2];
	unsigned int seq;

	if (e->e_id != NULL)
		return;

	/* see if we need to get a new base time/pid */
	if (cX >= QIC_LEN_SQR || LastQueueTime == 0 || LastQueuePid != pid)
	{
		time_t then = LastQueueTime;

		/* if the first time through, pick a random offset */
		if (LastQueueTime == 0)
			random_offset = ((unsigned int)get_random())
					% QIC_LEN_SQR;

		while ((LastQueueTime = curtime()) == then &&
		       LastQueuePid == pid)
		{
			(void) sleep(1);
		}
		LastQueuePid = queuenextid();
		cX = 0;
	}

	/*
	**  Generate a new sequence number between 0 and QIC_LEN_SQR-1.
	**  This lets us generate up to QIC_LEN_SQR unique queue ids
	**  per second, per process.  With envelope splitting,
	**  a single message can consume many queue ids.
	*/

	seq = (cX + random_offset) % QIC_LEN_SQR;
	++cX;
	if (tTd(7, 50))
		sm_dprintf("assign_queueid: random_offset=%u (%u)\n",
			random_offset, seq);

	tm = gmtime(&LastQueueTime);
	idbuf[0] = QueueIdChars[tm->tm_year % QIC_LEN];
	idbuf[1] = QueueIdChars[tm->tm_mon];
	idbuf[2] = QueueIdChars[tm->tm_mday];
	idbuf[3] = QueueIdChars[tm->tm_hour];
	idbuf[4] = QueueIdChars[tm->tm_min % QIC_LEN_R];
	idbuf[5] = QueueIdChars[tm->tm_sec % QIC_LEN_R];
	idbuf[6] = QueueIdChars[seq / QIC_LEN];
	idbuf[7] = QueueIdChars[seq % QIC_LEN];
	(void) sm_snprintf(&idbuf[8], sizeof(idbuf) - 8, "%06d",
			   (int) LastQueuePid);
	e->e_id = sm_rpool_strdup_x(e->e_rpool, idbuf);
	macdefine(&e->e_macro, A_PERM, 'i', e->e_id);
#if 0
	/* XXX: inherited from MainEnvelope */
	e->e_qgrp = NOQGRP;  /* too early to do anything else */
	e->e_qdir = NOQDIR;
	e->e_xfqgrp = NOQGRP;
#endif /* 0 */

	/* New ID means it's not on disk yet */
	e->e_qfletter = '\0';

	if (tTd(7, 1))
		sm_dprintf("assign_queueid: assigned id %s, e=%p\n",
			e->e_id, e);
	if (LogLevel > 93)
		sm_syslog(LOG_DEBUG, e->e_id, "assigned id");
}
/*
**  SYNC_QUEUE_TIME -- Assure exclusive PID in any given second
**
**	Make sure one PID can't be used by two processes in any one second.
**
**		If the system rotates PIDs fast enough, may get the
**		same pid in the same second for two distinct processes.
**		This will interfere with the queue file naming system.
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/

void
sync_queue_time()
{
#if FAST_PID_RECYCLE
	if (OpMode != MD_TEST &&
	    OpMode != MD_CHECKCONFIG &&
	    OpMode != MD_VERIFY &&
	    LastQueueTime > 0 &&
	    LastQueuePid == CurrentPid &&
	    curtime() == LastQueueTime)
		(void) sleep(1);
#endif /* FAST_PID_RECYCLE */
}
/*
**  UNLOCKQUEUE -- unlock the queue entry for a specified envelope
**
**	Parameters:
**		e -- the envelope to unlock.
**
**	Returns:
**		none
**
**	Side Effects:
**		unlocks the queue for `e'.
*/

void
unlockqueue(e)
	ENVELOPE *e;
{
	if (tTd(51, 4))
		sm_dprintf("unlockqueue(%s)\n",
			e->e_id == NULL ? "NOQUEUE" : e->e_id);


	/* if there is a lock file in the envelope, close it */
	if (e->e_lockfp != NULL)
		(void) sm_io_close(e->e_lockfp, SM_TIME_DEFAULT);
	e->e_lockfp = NULL;

	/* don't create a queue id if we don't already have one */
	if (e->e_id == NULL)
		return;

	/* remove the transcript */
	if (LogLevel > 87)
		sm_syslog(LOG_DEBUG, e->e_id, "unlock");
	if (!tTd(51, 104))
		(void) xunlink(queuename(e, XSCRPT_LETTER));
}
/*
**  SETCTLUSER -- create a controlling address
**
**	Create a fake "address" given only a local login name; this is
**	used as a "controlling user" for future recipient addresses.
**
**	Parameters:
**		user -- the user name of the controlling user.
**		qfver -- the version stamp of this queue file.
**		e -- envelope
**
**	Returns:
**		An address descriptor for the controlling user,
**		using storage allocated from e->e_rpool.
**
*/

static ADDRESS *
setctluser(user, qfver, e)
	char *user;
	int qfver;
	ENVELOPE *e;
{
	register ADDRESS *a;
	struct passwd *pw;
	char *p;

	/*
	**  See if this clears our concept of controlling user.
	*/

	if (user == NULL || *user == '\0')
		return NULL;

	/*
	**  Set up addr fields for controlling user.
	*/

	a = (ADDRESS *) sm_rpool_malloc_x(e->e_rpool, sizeof(*a));
	memset((char *) a, '\0', sizeof(*a));

	if (*user == ':')
	{
		p = &user[1];
		a->q_user = sm_rpool_strdup_x(e->e_rpool, p);
	}
	else
	{
		p = strtok(user, ":");
		a->q_user = sm_rpool_strdup_x(e->e_rpool, user);
		if (qfver >= 2)
		{
			if ((p = strtok(NULL, ":")) != NULL)
				a->q_uid = atoi(p);
			if ((p = strtok(NULL, ":")) != NULL)
				a->q_gid = atoi(p);
			if ((p = strtok(NULL, ":")) != NULL)
			{
				char *o;

				a->q_flags |= QGOODUID;

				/* if there is another ':': restore it */
				if ((o = strtok(NULL, ":")) != NULL && o > p)
					o[-1] = ':';
			}
		}
		else if ((pw = sm_getpwnam(user)) != NULL)
		{
			if (*pw->pw_dir == '\0')
				a->q_home = NULL;
			else if (strcmp(pw->pw_dir, "/") == 0)
				a->q_home = "";
			else
				a->q_home = sm_rpool_strdup_x(e->e_rpool, pw->pw_dir);
			a->q_uid = pw->pw_uid;
			a->q_gid = pw->pw_gid;
			a->q_flags |= QGOODUID;
		}
	}

	a->q_flags |= QPRIMARY;		/* flag as a "ctladdr" */
	a->q_mailer = LocalMailer;
	if (p == NULL)
		a->q_paddr = sm_rpool_strdup_x(e->e_rpool, a->q_user);
	else
		a->q_paddr = sm_rpool_strdup_x(e->e_rpool, p);
	return a;
}
/*
**  LOSEQFILE -- rename queue file with LOSEQF_LETTER & try to let someone know
**
**	Parameters:
**		e -- the envelope (e->e_id will be used).
**		why -- reported to whomever can hear.
**
**	Returns:
**		none.
*/

void
loseqfile(e, why)
	register ENVELOPE *e;
	char *why;
{
	bool loseit = true;
	char *p;
	char buf[MAXPATHLEN];

	if (e == NULL || e->e_id == NULL)
		return;
	p = queuename(e, ANYQFL_LETTER);
	if (sm_strlcpy(buf, p, sizeof(buf)) >= sizeof(buf))
		return;
	if (!bitset(EF_INQUEUE, e->e_flags))
		queueup(e, false, true);
	else if (QueueMode == QM_LOST)
		loseit = false;

	/* if already lost, no need to re-lose */
	if (loseit)
	{
		p = queuename(e, LOSEQF_LETTER);
		if (rename(buf, p) < 0)
			syserr("cannot rename(%s, %s), uid=%ld",
			       buf, p, (long) geteuid());
		else if (LogLevel > 0)
			sm_syslog(LOG_ALERT, e->e_id,
				  "Losing %s: %s", buf, why);
	}
	if (e->e_dfp != NULL)
	{
		(void) sm_io_close(e->e_dfp, SM_TIME_DEFAULT);
		e->e_dfp = NULL;
	}
	e->e_flags &= ~EF_HAS_DF;
}
/*
**  NAME2QID -- translate a queue group name to a queue group id
**
**	Parameters:
**		queuename -- name of queue group.
**
**	Returns:
**		queue group id if found.
**		NOQGRP otherwise.
*/

int
name2qid(queuename)
	char *queuename;
{
	register STAB *s;

	s = stab(queuename, ST_QUEUE, ST_FIND);
	if (s == NULL)
		return NOQGRP;
	return s->s_quegrp->qg_index;
}
/*
**  QID_PRINTNAME -- create externally printable version of queue id
**
**	Parameters:
**		e -- the envelope.
**
**	Returns:
**		a printable version
*/

char *
qid_printname(e)
	ENVELOPE *e;
{
	char *id;
	static char idbuf[MAXQFNAME + 34];

	if (e == NULL)
		return "";

	if (e->e_id == NULL)
		id = "";
	else
		id = e->e_id;

	if (e->e_qdir == NOQDIR)
		return id;

	(void) sm_snprintf(idbuf, sizeof(idbuf), "%.32s/%s",
			   Queue[e->e_qgrp]->qg_qpaths[e->e_qdir].qp_name,
			   id);
	return idbuf;
}
/*
**  QID_PRINTQUEUE -- create full version of queue directory for data files
**
**	Parameters:
**		qgrp -- index in queue group.
**		qdir -- the short version of the queue directory
**
**	Returns:
**		the full pathname to the queue (might point to a static var)
*/

char *
qid_printqueue(qgrp, qdir)
	int qgrp;
	int qdir;
{
	char *subdir;
	static char dir[MAXPATHLEN];

	if (qdir == NOQDIR)
		return Queue[qgrp]->qg_qdir;

	if (strcmp(Queue[qgrp]->qg_qpaths[qdir].qp_name, ".") == 0)
		subdir = NULL;
	else
		subdir = Queue[qgrp]->qg_qpaths[qdir].qp_name;

	(void) sm_strlcpyn(dir, sizeof(dir), 4,
			Queue[qgrp]->qg_qdir,
			subdir == NULL ? "" : "/",
			subdir == NULL ? "" : subdir,
			(bitset(QP_SUBDF,
				Queue[qgrp]->qg_qpaths[qdir].qp_subdirs)
					? "/df" : ""));
	return dir;
}

/*
**  PICKQDIR -- Pick a queue directory from a queue group
**
**	Parameters:
**		qg -- queue group
**		fsize -- file size in bytes
**		e -- envelope, or NULL
**
**	Result:
**		NOQDIR if no queue directory in qg has enough free space to
**		hold a file of size 'fsize', otherwise the index of
**		a randomly selected queue directory which resides on a
**		file system with enough disk space.
**		XXX This could be extended to select a queuedir with
**			a few (the fewest?) number of entries. That data
**			is available if shared memory is used.
**
**	Side Effects:
**		If the request fails and e != NULL then sm_syslog is called.
*/

int
pickqdir(qg, fsize, e)
	QUEUEGRP *qg;
	long fsize;
	ENVELOPE *e;
{
	int qdir;
	int i;
	long avail = 0;

	/* Pick a random directory, as a starting point. */
	if (qg->qg_numqueues <= 1)
		qdir = 0;
	else
		qdir = get_rand_mod(qg->qg_numqueues);

#if _FFR_TESTS
	if (tTd(4, 101))
		return NOQDIR;
#endif /* _FFR_TESTS */
	if (MinBlocksFree <= 0 && fsize <= 0)
		return qdir;

	/*
	**  Now iterate over the queue directories,
	**  looking for a directory with enough space for this message.
	*/

	i = qdir;
	do
	{
		QPATHS *qp = &qg->qg_qpaths[i];
		long needed = 0;
		long fsavail = 0;

		if (fsize > 0)
			needed += fsize / FILE_SYS_BLKSIZE(qp->qp_fsysidx)
				  + ((fsize % FILE_SYS_BLKSIZE(qp->qp_fsysidx)
				      > 0) ? 1 : 0);
		if (MinBlocksFree > 0)
			needed += MinBlocksFree;
		fsavail = FILE_SYS_AVAIL(qp->qp_fsysidx);
#if SM_CONF_SHM
		if (fsavail <= 0)
		{
			long blksize;

			/*
			**  might be not correctly updated,
			**  let's try to get the info directly.
			*/

			fsavail = freediskspace(FILE_SYS_NAME(qp->qp_fsysidx),
						&blksize);
			if (fsavail < 0)
				fsavail = 0;
		}
#endif /* SM_CONF_SHM */
		if (needed <= fsavail)
			return i;
		if (avail < fsavail)
			avail = fsavail;

		if (qg->qg_numqueues > 0)
			i = (i + 1) % qg->qg_numqueues;
	} while (i != qdir);

	if (e != NULL && LogLevel > 0)
		sm_syslog(LOG_ALERT, e->e_id,
			"low on space (%s needs %ld bytes + %ld blocks in %s), max avail: %ld",
			CurHostName == NULL ? "SMTP-DAEMON" : CurHostName,
			fsize, MinBlocksFree,
			qg->qg_qdir, avail);
	return NOQDIR;
}
/*
**  SETNEWQUEUE -- Sets a new queue group and directory
**
**	Assign a queue group and directory to an envelope and store the
**	directory in e->e_qdir.
**
**	Parameters:
**		e -- envelope to assign a queue for.
**
**	Returns:
**		true if successful
**		false otherwise
**
**	Side Effects:
**		On success, e->e_qgrp and e->e_qdir are non-negative.
**		On failure (not enough disk space),
**		e->qgrp = NOQGRP, e->e_qdir = NOQDIR
**		and usrerr() is invoked (which could raise an exception).
*/

bool
setnewqueue(e)
	ENVELOPE *e;
{
	if (tTd(41, 20))
		sm_dprintf("setnewqueue: called\n");

	/* not set somewhere else */
	if (e->e_qgrp == NOQGRP)
	{
		ADDRESS *q;

		/*
		**  Use the queue group of the "first" recipient, as set by
		**  the "queuegroup" rule set.  If that is not defined, then
		**  use the queue group of the mailer of the first recipient.
		**  If that is not defined either, then use the default
		**  queue group.
		**  Notice: "first" depends on the sorting of sendqueue
		**  in recipient().
		**  To avoid problems with "bad" recipients look
		**  for a valid address first.
		*/

		q = e->e_sendqueue;
		while (q != NULL &&
		       (QS_IS_BADADDR(q->q_state) || QS_IS_DEAD(q->q_state)))
		{
			q = q->q_next;
		}
		if (q == NULL)
			e->e_qgrp = 0;
		else if (q->q_qgrp >= 0)
			e->e_qgrp = q->q_qgrp;
		else if (q->q_mailer != NULL &&
			 ISVALIDQGRP(q->q_mailer->m_qgrp))
			e->e_qgrp = q->q_mailer->m_qgrp;
		else
			e->e_qgrp = 0;
		e->e_dfqgrp = e->e_qgrp;
	}

	if (ISVALIDQDIR(e->e_qdir) && ISVALIDQDIR(e->e_dfqdir))
	{
		if (tTd(41, 20))
			sm_dprintf("setnewqueue: e_qdir already assigned (%s)\n",
				qid_printqueue(e->e_qgrp, e->e_qdir));
		return true;
	}

	filesys_update();
	e->e_qdir = pickqdir(Queue[e->e_qgrp], e->e_msgsize, e);
	if (e->e_qdir == NOQDIR)
	{
		e->e_qgrp = NOQGRP;
		if (!bitset(EF_FATALERRS, e->e_flags))
			usrerr("452 4.4.5 Insufficient disk space; try again later");
		e->e_flags |= EF_FATALERRS;
		return false;
	}

	if (tTd(41, 3))
		sm_dprintf("setnewqueue: Assigned queue directory %s\n",
			qid_printqueue(e->e_qgrp, e->e_qdir));

	if (e->e_xfqgrp == NOQGRP || e->e_xfqdir == NOQDIR)
	{
		e->e_xfqgrp = e->e_qgrp;
		e->e_xfqdir = e->e_qdir;
	}
	e->e_dfqdir = e->e_qdir;
	return true;
}
/*
**  CHKQDIR -- check a queue directory
**
**	Parameters:
**		name -- name of queue directory
**		sff -- flags for safefile()
**
**	Returns:
**		is it a queue directory?
*/

static bool chkqdir __P((char *, long));

static bool
chkqdir(name, sff)
	char *name;
	long sff;
{
	struct stat statb;
	int i;

	/* skip over . and .. directories */
	if (name[0] == '.' &&
	    (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
		return false;
#if HASLSTAT
	if (lstat(name, &statb) < 0)
#else /* HASLSTAT */
	if (stat(name, &statb) < 0)
#endif /* HASLSTAT */
	{
		if (tTd(41, 2))
			sm_dprintf("chkqdir: stat(\"%s\"): %s\n",
				   name, sm_errstring(errno));
		return false;
	}
#if HASLSTAT
	if (S_ISLNK(statb.st_mode))
	{
		/*
		**  For a symlink we need to make sure the
		**  target is a directory
		*/

		if (stat(name, &statb) < 0)
		{
			if (tTd(41, 2))
				sm_dprintf("chkqdir: stat(\"%s\"): %s\n",
					   name, sm_errstring(errno));
			return false;
		}
	}
#endif /* HASLSTAT */

	if (!S_ISDIR(statb.st_mode))
	{
		if (tTd(41, 2))
			sm_dprintf("chkqdir: \"%s\": Not a directory\n",
				name);
		return false;
	}

	/* Print a warning if unsafe (but still use it) */
	/* XXX do this only if we want the warning? */
	i = safedirpath(name, RunAsUid, RunAsGid, NULL, sff, 0, 0);
	if (i != 0)
	{
		if (tTd(41, 2))
			sm_dprintf("chkqdir: \"%s\": Not safe: %s\n",
				   name, sm_errstring(i));
#if _FFR_CHK_QUEUE
		if (LogLevel > 8)
			sm_syslog(LOG_WARNING, NOQID,
				  "queue directory \"%s\": Not safe: %s",
				  name, sm_errstring(i));
#endif /* _FFR_CHK_QUEUE */
	}
	return true;
}
/*
**  MULTIQUEUE_CACHE -- cache a list of paths to queues.
**
**	Each potential queue is checked as the cache is built.
**	Thereafter, each is blindly trusted.
**	Note that we can be called again after a timeout to rebuild
**	(although code for that is not ready yet).
**
**	Parameters:
**		basedir -- base of all queue directories.
**		blen -- strlen(basedir).
**		qg -- queue group.
**		qn -- number of queue directories already cached.
**		phash -- pointer to hash value over queue dirs.
#if SM_CONF_SHM
**			only used if shared memory is active.
#endif * SM_CONF_SHM *
**
**	Returns:
**		new number of queue directories.
*/

#define INITIAL_SLOTS	20
#define ADD_SLOTS	10

static int
multiqueue_cache(basedir, blen, qg, qn, phash)
	char *basedir;
	int blen;
	QUEUEGRP *qg;
	int qn;
	unsigned int *phash;
{
	char *cp;
	int i, len;
	int slotsleft = 0;
	long sff = SFF_ANYFILE;
	char qpath[MAXPATHLEN];
	char subdir[MAXPATHLEN];
	char prefix[MAXPATHLEN];	/* dir relative to basedir */

	if (tTd(41, 20))
		sm_dprintf("multiqueue_cache: called\n");

	/* Initialize to current directory */
	prefix[0] = '.';
	prefix[1] = '\0';
	if (qg->qg_numqueues != 0 && qg->qg_qpaths != NULL)
	{
		for (i = 0; i < qg->qg_numqueues; i++)
		{
			if (qg->qg_qpaths[i].qp_name != NULL)
				(void) sm_free(qg->qg_qpaths[i].qp_name); /* XXX */
		}
		(void) sm_free((char *) qg->qg_qpaths); /* XXX */
		qg->qg_qpaths = NULL;
		qg->qg_numqueues = 0;
	}

	/* If running as root, allow safedirpath() checks to use privs */
	if (RunAsUid == 0)
		sff |= SFF_ROOTOK;
#if _FFR_CHK_QUEUE
	sff |= SFF_SAFEDIRPATH|SFF_NOWWFILES;
	if (!UseMSP)
		sff |= SFF_NOGWFILES;
#endif /* _FFR_CHK_QUEUE */

	if (!SM_IS_DIR_START(qg->qg_qdir))
	{
		/*
		**  XXX we could add basedir, but then we have to realloc()
		**  the string... Maybe another time.
		*/

		syserr("QueuePath %s not absolute", qg->qg_qdir);
		ExitStat = EX_CONFIG;
		return qn;
	}

	/* qpath: directory of current workgroup */
	len = sm_strlcpy(qpath, qg->qg_qdir, sizeof(qpath));
	if (len >= sizeof(qpath))
	{
		syserr("QueuePath %.256s too long (%d max)",
		       qg->qg_qdir, (int) sizeof(qpath));
		ExitStat = EX_CONFIG;
		return qn;
	}

	/* begin of qpath must be same as basedir */
	if (strncmp(basedir, qpath, blen) != 0 &&
	    (strncmp(basedir, qpath, blen - 1) != 0 || len != blen - 1))
	{
		syserr("QueuePath %s not subpath of QueueDirectory %s",
			qpath, basedir);
		ExitStat = EX_CONFIG;
		return qn;
	}

	/* Do we have a nested subdirectory? */
	if (blen < len && SM_FIRST_DIR_DELIM(qg->qg_qdir + blen) != NULL)
	{

		/* Copy subdirectory into prefix for later use */
		if (sm_strlcpy(prefix, qg->qg_qdir + blen, sizeof(prefix)) >=
		    sizeof(prefix))
		{
			syserr("QueuePath %.256s too long (%d max)",
				qg->qg_qdir, (int) sizeof(qpath));
			ExitStat = EX_CONFIG;
			return qn;
		}
		cp = SM_LAST_DIR_DELIM(prefix);
		SM_ASSERT(cp != NULL);
		*cp = '\0';	/* cut off trailing / */
	}

	/* This is guaranteed by the basedir check above */
	SM_ASSERT(len >= blen - 1);
	cp = &qpath[len - 1];
	if (*cp == '*')
	{
		register DIR *dp;
		register struct dirent *d;
		int off;
		char *delim;
		char relpath[MAXPATHLEN];

		*cp = '\0';	/* Overwrite wildcard */
		if ((cp = SM_LAST_DIR_DELIM(qpath)) == NULL)
		{
			syserr("QueueDirectory: can not wildcard relative path");
			if (tTd(41, 2))
				sm_dprintf("multiqueue_cache: \"%s*\": Can not wildcard relative path.\n",
					qpath);
			ExitStat = EX_CONFIG;
			return qn;
		}
		if (cp == qpath)
		{
			/*
			**  Special case of top level wildcard, like /foo*
			**	Change to //foo*
			*/

			(void) sm_strlcpy(qpath + 1, qpath, sizeof(qpath) - 1);
			++cp;
		}
		delim = cp;
		*(cp++) = '\0';		/* Replace / with \0 */
		len = strlen(cp);	/* Last component of queue directory */

		/*
		**  Path relative to basedir, with trailing /
		**  It will be modified below to specify the subdirectories
		**  so they can be opened without chdir().
		*/

		off = sm_strlcpyn(relpath, sizeof(relpath), 2, prefix, "/");
		SM_ASSERT(off < sizeof(relpath));

		if (tTd(41, 2))
			sm_dprintf("multiqueue_cache: prefix=\"%s%s\"\n",
				   relpath, cp);

		/* It is always basedir: we don't need to store it per group */
		/* XXX: optimize this! -> one more global? */
		qg->qg_qdir = newstr(basedir);
		qg->qg_qdir[blen - 1] = '\0';	/* cut off trailing / */

		/*
		**  XXX Should probably wrap this whole loop in a timeout
		**  in case some wag decides to NFS mount the queues.
		*/

		/* Test path to get warning messages. */
		if (qn == 0)
		{
			/*  XXX qg_runasuid and qg_runasgid for specials? */
			i = safedirpath(basedir, RunAsUid, RunAsGid, NULL,
					sff, 0, 0);
			if (i != 0 && tTd(41, 2))
				sm_dprintf("multiqueue_cache: \"%s\": Not safe: %s\n",
					   basedir, sm_errstring(i));
		}

		if ((dp = opendir(prefix)) == NULL)
		{
			syserr("can not opendir(%s/%s)", qg->qg_qdir, prefix);
			if (tTd(41, 2))
				sm_dprintf("multiqueue_cache: opendir(\"%s/%s\"): %s\n",
					   qg->qg_qdir, prefix,
					   sm_errstring(errno));
			ExitStat = EX_CONFIG;
			return qn;
		}
		while ((d = readdir(dp)) != NULL)
		{
			/* Skip . and .. directories */
			if (strcmp(d->d_name, ".") == 0 ||
			    strcmp(d->d_name, "..") == 0)
				continue;

			i = strlen(d->d_name);
			if (i < len || strncmp(d->d_name, cp, len) != 0)
			{
				if (tTd(41, 5))
					sm_dprintf("multiqueue_cache: \"%s\", skipped\n",
						d->d_name);
				continue;
			}

			/* Create relative pathname: prefix + local directory */
			i = sizeof(relpath) - off;
			if (sm_strlcpy(relpath + off, d->d_name, i) >= i)
				continue;	/* way too long */

			if (!chkqdir(relpath, sff))
				continue;

			if (qg->qg_qpaths == NULL)
			{
				slotsleft = INITIAL_SLOTS;
				qg->qg_qpaths = (QPATHS *)xalloc((sizeof(*qg->qg_qpaths)) *
								slotsleft);
				qg->qg_numqueues = 0;
			}
			else if (slotsleft < 1)
			{
				qg->qg_qpaths = (QPATHS *)sm_realloc((char *)qg->qg_qpaths,
							  (sizeof(*qg->qg_qpaths)) *
							  (qg->qg_numqueues +
							   ADD_SLOTS));
				if (qg->qg_qpaths == NULL)
				{
					(void) closedir(dp);
					return qn;
				}
				slotsleft += ADD_SLOTS;
			}

			/* check subdirs */
			qg->qg_qpaths[qg->qg_numqueues].qp_subdirs = QP_NOSUB;

#define CHKRSUBDIR(name, flag)	\
	(void) sm_strlcpyn(subdir, sizeof(subdir), 3, relpath, "/", name); \
	if (chkqdir(subdir, sff))	\
		qg->qg_qpaths[qg->qg_numqueues].qp_subdirs |= flag;	\
	else


			CHKRSUBDIR("qf", QP_SUBQF);
			CHKRSUBDIR("df", QP_SUBDF);
			CHKRSUBDIR("xf", QP_SUBXF);

			/* assert(strlen(d->d_name) < MAXPATHLEN - 14) */
			/* maybe even - 17 (subdirs) */

			if (prefix[0] != '.')
				qg->qg_qpaths[qg->qg_numqueues].qp_name =
					newstr(relpath);
			else
				qg->qg_qpaths[qg->qg_numqueues].qp_name =
					newstr(d->d_name);

			if (tTd(41, 2))
				sm_dprintf("multiqueue_cache: %d: \"%s\" cached (%x).\n",
					qg->qg_numqueues, relpath,
					qg->qg_qpaths[qg->qg_numqueues].qp_subdirs);
#if SM_CONF_SHM
			qg->qg_qpaths[qg->qg_numqueues].qp_idx = qn;
			*phash = hash_q(relpath, *phash);
#endif /* SM_CONF_SHM */
			qg->qg_numqueues++;
			++qn;
			slotsleft--;
		}
		(void) closedir(dp);

		/* undo damage */
		*delim = '/';
	}
	if (qg->qg_numqueues == 0)
	{
		qg->qg_qpaths = (QPATHS *) xalloc(sizeof(*qg->qg_qpaths));

		/* test path to get warning messages */
		i = safedirpath(qpath, RunAsUid, RunAsGid, NULL, sff, 0, 0);
		if (i == ENOENT)
		{
			syserr("can not opendir(%s)", qpath);
			if (tTd(41, 2))
				sm_dprintf("multiqueue_cache: opendir(\"%s\"): %s\n",
					   qpath, sm_errstring(i));
			ExitStat = EX_CONFIG;
			return qn;
		}

		qg->qg_qpaths[0].qp_subdirs = QP_NOSUB;
		qg->qg_numqueues = 1;

		/* check subdirs */
#define CHKSUBDIR(name, flag)	\
	(void) sm_strlcpyn(subdir, sizeof(subdir), 3, qg->qg_qdir, "/", name); \
	if (chkqdir(subdir, sff))	\
		qg->qg_qpaths[0].qp_subdirs |= flag;	\
	else

		CHKSUBDIR("qf", QP_SUBQF);
		CHKSUBDIR("df", QP_SUBDF);
		CHKSUBDIR("xf", QP_SUBXF);

		if (qg->qg_qdir[blen - 1] != '\0' &&
		    qg->qg_qdir[blen] != '\0')
		{
			/*
			**  Copy the last component into qpaths and
			**  cut off qdir
			*/

			qg->qg_qpaths[0].qp_name = newstr(qg->qg_qdir + blen);
			qg->qg_qdir[blen - 1] = '\0';
		}
		else
			qg->qg_qpaths[0].qp_name = newstr(".");

#if SM_CONF_SHM
		qg->qg_qpaths[0].qp_idx = qn;
		*phash = hash_q(qg->qg_qpaths[0].qp_name, *phash);
#endif /* SM_CONF_SHM */
		++qn;
	}
	return qn;
}

/*
**  FILESYS_FIND -- find entry in FileSys table, or add new one
**
**	Given the pathname of a directory, determine the file system
**	in which that directory resides, and return a pointer to the
**	entry in the FileSys table that describes the file system.
**	A new entry is added if necessary (and requested).
**	If the directory does not exist, -1 is returned.
**
**	Parameters:
**		name -- name of directory (must be persistent!)
**		path -- pathname of directory (name plus maybe "/df")
**		add -- add to structure if not found.
**
**	Returns:
**		>=0: found: index in file system table
**		<0: some error, i.e.,
**		FSF_TOO_MANY: too many filesystems (-> syserr())
**		FSF_STAT_FAIL: can't stat() filesystem (-> syserr())
**		FSF_NOT_FOUND: not in list
*/

static short filesys_find __P((const char *, const char *, bool));

#define FSF_NOT_FOUND	(-1)
#define FSF_STAT_FAIL	(-2)
#define FSF_TOO_MANY	(-3)

static short
filesys_find(name, path, add)
	const char *name;
	const char *path;
	bool add;
{
	struct stat st;
	short i;

	if (stat(path, &st) < 0)
	{
		syserr("cannot stat queue directory %s", path);
		return FSF_STAT_FAIL;
	}
	for (i = 0; i < NumFileSys; ++i)
	{
		if (FILE_SYS_DEV(i) == st.st_dev)
		{
			/*
			**  Make sure the file system (FS) name is set:
			**  even though the source code indicates that
			**  FILE_SYS_DEV() is only set below, it could be
			**  set via shared memory, hence we need to perform
			**  this check/assignment here.
			*/

			if (NULL == FILE_SYS_NAME(i))
				FILE_SYS_NAME(i) = name;
			return i;
		}
	}
	if (i >= MAXFILESYS)
	{
		syserr("too many queue file systems (%d max)", MAXFILESYS);
		return FSF_TOO_MANY;
	}
	if (!add)
		return FSF_NOT_FOUND;

	++NumFileSys;
	FILE_SYS_NAME(i) = name;
	FILE_SYS_DEV(i) = st.st_dev;
	FILE_SYS_AVAIL(i) = 0;
	FILE_SYS_BLKSIZE(i) = 1024; /* avoid divide by zero */
	return i;
}

/*
**  FILESYS_SETUP -- set up mapping from queue directories to file systems
**
**	This data structure is used to efficiently check the amount of
**	free space available in a set of queue directories.
**
**	Parameters:
**		add -- initialize structure if necessary.
**
**	Returns:
**		0: success
**		<0: some error, i.e.,
**		FSF_NOT_FOUND: not in list
**		FSF_STAT_FAIL: can't stat() filesystem (-> syserr())
**		FSF_TOO_MANY: too many filesystems (-> syserr())
*/

static int filesys_setup __P((bool));

static int
filesys_setup(add)
	bool add;
{
	int i, j;
	short fs;
	int ret;

	ret = 0;
	for (i = 0; i < NumQueue && Queue[i] != NULL; i++)
	{
		for (j = 0; j < Queue[i]->qg_numqueues; ++j)
		{
			QPATHS *qp = &Queue[i]->qg_qpaths[j];
			char qddf[MAXPATHLEN];

			(void) sm_strlcpyn(qddf, sizeof(qddf), 2, qp->qp_name,
					(bitset(QP_SUBDF, qp->qp_subdirs)
						? "/df" : ""));
			fs = filesys_find(qp->qp_name, qddf, add);
			if (fs >= 0)
				qp->qp_fsysidx = fs;
			else
				qp->qp_fsysidx = 0;
			if (fs < ret)
				ret = fs;
		}
	}
	return ret;
}

/*
**  FILESYS_UPDATE -- update amount of free space on all file systems
**
**	The FileSys table is used to cache the amount of free space
**	available on all queue directory file systems.
**	This function updates the cached information if it has expired.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Updates FileSys table.
*/

void
filesys_update()
{
	int i;
	long avail, blksize;
	time_t now;
	static time_t nextupdate = 0;

#if SM_CONF_SHM
	/*
	**  Only the daemon updates the shared memory, i.e.,
	**  if shared memory is available but the pid is not the
	**  one of the daemon, then don't do anything.
	*/

	if (ShmId != SM_SHM_NO_ID && DaemonPid != CurrentPid)
		return;
#endif /* SM_CONF_SHM */
	now = curtime();
	if (now < nextupdate)
		return;
	nextupdate = now + FILESYS_UPDATE_INTERVAL;
	for (i = 0; i < NumFileSys; ++i)
	{
		FILESYS *fs = &FILE_SYS(i);

		avail = freediskspace(FILE_SYS_NAME(i), &blksize);
		if (avail < 0 || blksize <= 0)
		{
			if (LogLevel > 5)
				sm_syslog(LOG_ERR, NOQID,
					"filesys_update failed: %s, fs=%s, avail=%ld, blocksize=%ld",
					sm_errstring(errno),
					FILE_SYS_NAME(i), avail, blksize);
			fs->fs_avail = 0;
			fs->fs_blksize = 1024; /* avoid divide by zero */
			nextupdate = now + 2; /* let's do this soon again */
		}
		else
		{
			fs->fs_avail = avail;
			fs->fs_blksize = blksize;
		}
	}
}

#if _FFR_ANY_FREE_FS
/*
**  FILESYS_FREE -- check whether there is at least one fs with enough space.
**
**	Parameters:
**		fsize -- file size in bytes
**
**	Returns:
**		true iff there is one fs with more than fsize bytes free.
*/

bool
filesys_free(fsize)
	long fsize;
{
	int i;

	if (fsize <= 0)
		return true;
	for (i = 0; i < NumFileSys; ++i)
	{
		long needed = 0;

		if (FILE_SYS_AVAIL(i) < 0 || FILE_SYS_BLKSIZE(i) <= 0)
			continue;
		needed += fsize / FILE_SYS_BLKSIZE(i)
			  + ((fsize % FILE_SYS_BLKSIZE(i)
			      > 0) ? 1 : 0)
			  + MinBlocksFree;
		if (needed <= FILE_SYS_AVAIL(i))
			return true;
	}
	return false;
}
#endif /* _FFR_ANY_FREE_FS */

/*
**  DISK_STATUS -- show amount of free space in queue directories
**
**	Parameters:
**		out -- output file pointer.
**		prefix -- string to output in front of each line.
**
**	Returns:
**		none.
*/

void
disk_status(out, prefix)
	SM_FILE_T *out;
	char *prefix;
{
	int i;
	long avail, blksize;
	long free;

	for (i = 0; i < NumFileSys; ++i)
	{
		avail = freediskspace(FILE_SYS_NAME(i), &blksize);
		if (avail >= 0 && blksize > 0)
		{
			free = (long)((double) avail *
				((double) blksize / 1024));
		}
		else
			free = -1;
		(void) sm_io_fprintf(out, SM_TIME_DEFAULT,
				"%s%d/%s/%ld\r\n",
				prefix, i,
				FILE_SYS_NAME(i),
					free);
	}
}

#if SM_CONF_SHM

/*
**  INIT_SEM -- initialize semaphore system
**
**	Parameters:
**		owner -- is this the owner of semaphores?
**
**	Returns:
**		none.
*/

#if _FFR_USE_SEM_LOCKING
#if SM_CONF_SEM
static int SemId = -1;		/* Semaphore Id */
int SemKey = SM_SEM_KEY;
#endif /* SM_CONF_SEM */
#endif /* _FFR_USE_SEM_LOCKING */

static void init_sem __P((bool));

static void
init_sem(owner)
	bool owner;
{
#if _FFR_USE_SEM_LOCKING
#if SM_CONF_SEM
	SemId = sm_sem_start(SemKey, 1, 0, owner);
	if (SemId < 0)
	{
		sm_syslog(LOG_ERR, NOQID,
			"func=init_sem, sem_key=%ld, sm_sem_start=%d, error=%s",
			(long) SemKey, SemId, sm_errstring(-SemId));
		return;
	}
	if (owner && RunAsUid != 0)
	{
		int r;

		r = sm_semsetowner(SemId, RunAsUid, RunAsGid, 0660);
		if (r != 0)
			sm_syslog(LOG_ERR, NOQID,
				"key=%ld, sm_semsetowner=%d, RunAsUid=%ld, RunAsGid=%ld",
				(long) SemKey, r, (long) RunAsUid, (long) RunAsGid);
	}
#endif /* SM_CONF_SEM */
#endif /* _FFR_USE_SEM_LOCKING */
	return;
}

/*
**  STOP_SEM -- stop semaphore system
**
**	Parameters:
**		owner -- is this the owner of semaphores?
**
**	Returns:
**		none.
*/

static void stop_sem __P((bool));

static void
stop_sem(owner)
	bool owner;
{
#if _FFR_USE_SEM_LOCKING
#if SM_CONF_SEM
	if (owner && SemId >= 0)
		sm_sem_stop(SemId);
#endif /* SM_CONF_SEM */
#endif /* _FFR_USE_SEM_LOCKING */
	return;
}

/*
**  UPD_QS -- update information about queue when adding/deleting an entry
**
**	Parameters:
**		e -- envelope.
**		count -- add/remove entry (+1/0/-1: add/no change/remove)
**		space -- update the space available as well.
**			(>0/0/<0: add/no change/remove)
**		where -- caller (for logging)
**
**	Returns:
**		none.
**
**	Side Effects:
**		Modifies available space in filesystem.
**		Changes number of entries in queue directory.
*/

void
upd_qs(e, count, space, where)
	ENVELOPE *e;
	int count;
	int space;
	char *where;
{
	short fidx;
	int idx;
# if _FFR_USE_SEM_LOCKING
	int r;
# endif /* _FFR_USE_SEM_LOCKING */
	long s;

	if (ShmId == SM_SHM_NO_ID || e == NULL)
		return;
	if (e->e_qgrp == NOQGRP || e->e_qdir == NOQDIR)
		return;
	idx = Queue[e->e_qgrp]->qg_qpaths[e->e_qdir].qp_idx;
	if (tTd(73,2))
		sm_dprintf("func=upd_qs, count=%d, space=%d, where=%s, idx=%d, entries=%d\n",
			count, space, where, idx, QSHM_ENTRIES(idx));

	/* XXX in theory this needs to be protected with a mutex */
	if (QSHM_ENTRIES(idx) >= 0 && count != 0)
	{
# if _FFR_USE_SEM_LOCKING
		if (SemId >= 0)
			r = sm_sem_acq(SemId, 0, 1);
# endif /* _FFR_USE_SEM_LOCKING */
		QSHM_ENTRIES(idx) += count;
# if _FFR_USE_SEM_LOCKING
		if (SemId >= 0 && r >= 0)
			r = sm_sem_rel(SemId, 0, 1);
# endif /* _FFR_USE_SEM_LOCKING */
	}

	fidx = Queue[e->e_qgrp]->qg_qpaths[e->e_qdir].qp_fsysidx;
	if (fidx < 0)
		return;

	/* update available space also?  (might be loseqfile) */
	if (space == 0)
		return;

	/* convert size to blocks; this causes rounding errors */
	s = e->e_msgsize / FILE_SYS_BLKSIZE(fidx);
	if (s == 0)
		return;

	/* XXX in theory this needs to be protected with a mutex */
	if (space > 0)
		FILE_SYS_AVAIL(fidx) += s;
	else
		FILE_SYS_AVAIL(fidx) -= s;

}

static bool write_key_file __P((char *, long));
static long read_key_file __P((char *, long));

/*
**  WRITE_KEY_FILE -- record some key into a file.
**
**	Parameters:
**		keypath -- file name.
**		key -- key to write.
**
**	Returns:
**		true iff file could be written.
**
**	Side Effects:
**		writes file.
*/

static bool
write_key_file(keypath, key)
	char *keypath;
	long key;
{
	bool ok;
	long sff;
	SM_FILE_T *keyf;

	ok = false;
	if (keypath == NULL || *keypath == '\0')
		return ok;
	sff = SFF_NOLINK|SFF_ROOTOK|SFF_REGONLY|SFF_CREAT;
	if (TrustedUid != 0 && RealUid == TrustedUid)
		sff |= SFF_OPENASROOT;
	keyf = safefopen(keypath, O_WRONLY|O_TRUNC, FileMode, sff);
	if (keyf == NULL)
	{
		sm_syslog(LOG_ERR, NOQID, "unable to write %s: %s",
			  keypath, sm_errstring(errno));
	}
	else
	{
		if (geteuid() == 0 && RunAsUid != 0)
		{
#  if HASFCHOWN
			int fd;

			fd = keyf->f_file;
			if (fd >= 0 && fchown(fd, RunAsUid, -1) < 0)
			{
				int err = errno;

				sm_syslog(LOG_ALERT, NOQID,
					  "ownership change on %s to %ld failed: %s",
					  keypath, (long) RunAsUid, sm_errstring(err));
			}
#  endif /* HASFCHOWN */
		}
		ok = sm_io_fprintf(keyf, SM_TIME_DEFAULT, "%ld\n", key) !=
		     SM_IO_EOF;
		ok = (sm_io_close(keyf, SM_TIME_DEFAULT) != SM_IO_EOF) && ok;
	}
	return ok;
}

/*
**  READ_KEY_FILE -- read a key from a file.
**
**	Parameters:
**		keypath -- file name.
**		key -- default key.
**
**	Returns:
**		key.
*/

static long
read_key_file(keypath, key)
	char *keypath;
	long key;
{
	int r;
	long sff, n;
	SM_FILE_T *keyf;

	if (keypath == NULL || *keypath == '\0')
		return key;
	sff = SFF_NOLINK|SFF_ROOTOK|SFF_REGONLY;
	if (RealUid == 0 || (TrustedUid != 0 && RealUid == TrustedUid))
		sff |= SFF_OPENASROOT;
	keyf = safefopen(keypath, O_RDONLY, FileMode, sff);
	if (keyf == NULL)
	{
		sm_syslog(LOG_ERR, NOQID, "unable to read %s: %s",
			  keypath, sm_errstring(errno));
	}
	else
	{
		r = sm_io_fscanf(keyf, SM_TIME_DEFAULT, "%ld", &n);
		if (r == 1)
			key = n;
		(void) sm_io_close(keyf, SM_TIME_DEFAULT);
	}
	return key;
}

/*
**  INIT_SHM -- initialize shared memory structure
**
**	Initialize or attach to shared memory segment.
**	Currently it is not a fatal error if this doesn't work.
**	However, it causes us to have a "fallback" storage location
**	for everything that is supposed to be in the shared memory,
**	which makes the code slightly ugly.
**
**	Parameters:
**		qn -- number of queue directories.
**		owner -- owner of shared memory.
**		hash -- identifies data that is stored in shared memory.
**
**	Returns:
**		none.
*/

static void init_shm __P((int, bool, unsigned int));

static void
init_shm(qn, owner, hash)
	int qn;
	bool owner;
	unsigned int hash;
{
	int i;
	int count;
	int save_errno;
	bool keyselect;

	PtrFileSys = &FileSys[0];
	PNumFileSys = &Numfilesys;
/* if this "key" is specified: select one yourself */
#define SEL_SHM_KEY	((key_t) -1)
#define FIRST_SHM_KEY	25

	/* This allows us to disable shared memory at runtime. */
	if (ShmKey == 0)
		return;

	count = 0;
	shms = SM_T_SIZE + qn * sizeof(QUEUE_SHM_T);
	keyselect = ShmKey == SEL_SHM_KEY;
	if (keyselect)
	{
		if (owner)
			ShmKey = FIRST_SHM_KEY;
		else
		{
			errno = 0;
			ShmKey = read_key_file(ShmKeyFile, ShmKey);
			keyselect = false;
			if (ShmKey == SEL_SHM_KEY)
			{
				save_errno = (errno != 0) ? errno : EINVAL;
				goto error;
			}
		}
	}
	for (;;)
	{
		/* allow read/write access for group? */
		Pshm = sm_shmstart(ShmKey, shms,
				SHM_R|SHM_W|(SHM_R>>3)|(SHM_W>>3),
				&ShmId, owner);
		save_errno = errno;
		if (Pshm != NULL || !sm_file_exists(save_errno))
			break;
		if (++count >= 3)
		{
			if (keyselect)
			{
				++ShmKey;

				/* back where we started? */
				if (ShmKey == SEL_SHM_KEY)
					break;
				continue;
			}
			break;
		}

		/* only sleep if we are at the first key */
		if (!keyselect || ShmKey == SEL_SHM_KEY)
			sleep(count);
	}
	if (Pshm != NULL)
	{
		int *p;

		if (keyselect)
			(void) write_key_file(ShmKeyFile, (long) ShmKey);
		if (owner && RunAsUid != 0)
		{
			i = sm_shmsetowner(ShmId, RunAsUid, RunAsGid, 0660);
			if (i != 0)
				sm_syslog(LOG_ERR, NOQID,
					"key=%ld, sm_shmsetowner=%d, RunAsUid=%ld, RunAsGid=%ld",
					(long) ShmKey, i, (long) RunAsUid, (long) RunAsGid);
		}
		p = (int *) Pshm;
		if (owner)
		{
			*p = (int) shms;
			*((pid_t *) SHM_OFF_PID(Pshm)) = CurrentPid;
			p = (int *) SHM_OFF_TAG(Pshm);
			*p = hash;
		}
		else
		{
			if (*p != (int) shms)
			{
				save_errno = EINVAL;
				cleanup_shm(false);
				goto error;
			}
			p = (int *) SHM_OFF_TAG(Pshm);
			if (*p != (int) hash)
			{
				save_errno = EINVAL;
				cleanup_shm(false);
				goto error;
			}

			/*
			**  XXX how to check the pid?
			**  Read it from the pid-file? That does
			**  not need to exist.
			**  We could disable shm if we can't confirm
			**  that it is the right one.
			*/
		}

		PtrFileSys = (FILESYS *) OFF_FILE_SYS(Pshm);
		PNumFileSys = (int *) OFF_NUM_FILE_SYS(Pshm);
		QShm = (QUEUE_SHM_T *) OFF_QUEUE_SHM(Pshm);
		PRSATmpCnt = (int *) OFF_RSA_TMP_CNT(Pshm);
		*PRSATmpCnt = 0;
		if (owner)
		{
			/* initialize values in shared memory */
			NumFileSys = 0;
			for (i = 0; i < qn; i++)
				QShm[i].qs_entries = -1;
		}
		init_sem(owner);
		return;
	}
  error:
	if (LogLevel > (owner ? 8 : 11))
	{
		sm_syslog(owner ? LOG_ERR : LOG_NOTICE, NOQID,
			  "can't %s shared memory, key=%ld: %s",
			  owner ? "initialize" : "attach to",
			  (long) ShmKey, sm_errstring(save_errno));
	}
}
#endif /* SM_CONF_SHM */


/*
**  SETUP_QUEUES -- set up all queue groups
**
**	Parameters:
**		owner -- owner of shared memory?
**
**	Returns:
**		none.
**
#if SM_CONF_SHM
**	Side Effects:
**		attaches shared memory.
#endif * SM_CONF_SHM *
*/

void
setup_queues(owner)
	bool owner;
{
	int i, qn, len;
	unsigned int hashval;
	time_t now;
	char basedir[MAXPATHLEN];
	struct stat st;

	/*
	**  Determine basedir for all queue directories.
	**  All queue directories must be (first level) subdirectories
	**  of the basedir.  The basedir is the QueueDir
	**  without wildcards, but with trailing /
	*/

	hashval = 0;
	errno = 0;
	len = sm_strlcpy(basedir, QueueDir, sizeof(basedir));

	/* Provide space for trailing '/' */
	if (len >= sizeof(basedir) - 1)
	{
		syserr("QueueDirectory: path too long: %d,  max %d",
			len, (int) sizeof(basedir) - 1);
		ExitStat = EX_CONFIG;
		return;
	}
	SM_ASSERT(len > 0);
	if (basedir[len - 1] == '*')
	{
		char *cp;

		cp = SM_LAST_DIR_DELIM(basedir);
		if (cp == NULL)
		{
			syserr("QueueDirectory: can not wildcard relative path \"%s\"",
				QueueDir);
			if (tTd(41, 2))
				sm_dprintf("setup_queues: \"%s\": Can not wildcard relative path.\n",
					QueueDir);
			ExitStat = EX_CONFIG;
			return;
		}

		/* cut off wildcard pattern */
		*++cp = '\0';
		len = cp - basedir;
	}
	else if (!SM_IS_DIR_DELIM(basedir[len - 1]))
	{
		/* append trailing slash since it is a directory */
		basedir[len] = '/';
		basedir[++len] = '\0';
	}

	/* len counts up to the last directory delimiter */
	SM_ASSERT(basedir[len - 1] == '/');

	if (chdir(basedir) < 0)
	{
		int save_errno = errno;

		syserr("can not chdir(%s)", basedir);
		if (save_errno == EACCES)
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				"Program mode requires special privileges, e.g., root or TrustedUser.\n");
		if (tTd(41, 2))
			sm_dprintf("setup_queues: \"%s\": %s\n",
				   basedir, sm_errstring(errno));
		ExitStat = EX_CONFIG;
		return;
	}
#if SM_CONF_SHM
	hashval = hash_q(basedir, hashval);
#endif /* SM_CONF_SHM */

	/* initialize for queue runs */
	DoQueueRun = false;
	now = curtime();
	for (i = 0; i < NumQueue && Queue[i] != NULL; i++)
		Queue[i]->qg_nextrun = now;


	if (UseMSP && OpMode != MD_TEST)
	{
		long sff = SFF_CREAT;

		if (stat(".", &st) < 0)
		{
			syserr("can not stat(%s)", basedir);
			if (tTd(41, 2))
				sm_dprintf("setup_queues: \"%s\": %s\n",
					   basedir, sm_errstring(errno));
			ExitStat = EX_CONFIG;
			return;
		}
		if (RunAsUid == 0)
			sff |= SFF_ROOTOK;

		/*
		**  Check queue directory permissions.
		**	Can we write to a group writable queue directory?
		*/

		if (bitset(S_IWGRP, QueueFileMode) &&
		    bitset(S_IWGRP, st.st_mode) &&
		    safefile(" ", RunAsUid, RunAsGid, RunAsUserName, sff,
			     QueueFileMode, NULL) != 0)
		{
			syserr("can not write to queue directory %s (RunAsGid=%ld, required=%ld)",
				basedir, (long) RunAsGid, (long) st.st_gid);
		}
		if (bitset(S_IWOTH|S_IXOTH, st.st_mode))
		{
#if _FFR_MSP_PARANOIA
			syserr("dangerous permissions=%o on queue directory %s",
				(unsigned int) st.st_mode, basedir);
#else /* _FFR_MSP_PARANOIA */
			if (LogLevel > 0)
				sm_syslog(LOG_ERR, NOQID,
					  "dangerous permissions=%o on queue directory %s",
					  (unsigned int) st.st_mode, basedir);
#endif /* _FFR_MSP_PARANOIA */
		}
#if _FFR_MSP_PARANOIA
		if (NumQueue > 1)
			syserr("can not use multiple queues for MSP");
#endif /* _FFR_MSP_PARANOIA */
	}

	/* initial number of queue directories */
	qn = 0;
	for (i = 0; i < NumQueue && Queue[i] != NULL; i++)
		qn = multiqueue_cache(basedir, len, Queue[i], qn, &hashval);

#if SM_CONF_SHM
	init_shm(qn, owner, hashval);
	i = filesys_setup(owner || ShmId == SM_SHM_NO_ID);
	if (i == FSF_NOT_FOUND)
	{
		/*
		**  We didn't get the right filesystem data
		**  This may happen if we don't have the right shared memory.
		**  So let's do this without shared memory.
		*/

		SM_ASSERT(!owner);
		cleanup_shm(false);	/* release shared memory */
		i = filesys_setup(false);
		if (i < 0)
			syserr("filesys_setup failed twice, result=%d", i);
		else if (LogLevel > 8)
			sm_syslog(LOG_WARNING, NOQID,
				  "shared memory does not contain expected data, ignored");
	}
#else /* SM_CONF_SHM */
	i = filesys_setup(true);
#endif /* SM_CONF_SHM */
	if (i < 0)
		ExitStat = EX_CONFIG;
}

#if SM_CONF_SHM
/*
**  CLEANUP_SHM -- do some cleanup work for shared memory etc
**
**	Parameters:
**		owner -- owner of shared memory?
**
**	Returns:
**		none.
**
**	Side Effects:
**		detaches shared memory.
*/

void
cleanup_shm(owner)
	bool owner;
{
	if (ShmId != SM_SHM_NO_ID)
	{
		if (sm_shmstop(Pshm, ShmId, owner) < 0 && LogLevel > 8)
			sm_syslog(LOG_INFO, NOQID, "sm_shmstop failed=%s",
				  sm_errstring(errno));
		Pshm = NULL;
		ShmId = SM_SHM_NO_ID;
	}
	stop_sem(owner);
}
#endif /* SM_CONF_SHM */

/*
**  CLEANUP_QUEUES -- do some cleanup work for queues
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
*/

void
cleanup_queues()
{
	sync_queue_time();
}
/*
**  SET_DEF_QUEUEVAL -- set default values for a queue group.
**
**	Parameters:
**		qg -- queue group
**		all -- set all values (true for default group)?
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets default values for the queue group.
*/

void
set_def_queueval(qg, all)
	QUEUEGRP *qg;
	bool all;
{
	if (bitnset(QD_DEFINED, qg->qg_flags))
		return;
	if (all)
		qg->qg_qdir = QueueDir;
#if _FFR_QUEUE_GROUP_SORTORDER
	qg->qg_sortorder = QueueSortOrder;
#endif /* _FFR_QUEUE_GROUP_SORTORDER */
	qg->qg_maxqrun = all ? MaxRunnersPerQueue : -1;
	qg->qg_nice = NiceQueueRun;
}
/*
**  MAKEQUEUE -- define a new queue.
**
**	Parameters:
**		line -- description of queue.  This is in labeled fields.
**			The fields are:
**			   F -- the flags associated with the queue
**			   I -- the interval between running the queue
**			   J -- the maximum # of jobs in work list
**			   [M -- the maximum # of jobs in a queue run]
**			   N -- the niceness at which to run
**			   P -- the path to the queue
**			   S -- the queue sorting order
**			   R -- number of parallel queue runners
**			   r -- max recipients per envelope
**			The first word is the canonical name of the queue.
**		qdef -- this is a 'Q' definition from .cf
**
**	Returns:
**		none.
**
**	Side Effects:
**		enters the queue into the queue table.
*/

void
makequeue(line, qdef)
	char *line;
	bool qdef;
{
	register char *p;
	register QUEUEGRP *qg;
	register STAB *s;
	int i;
	char fcode;

	/* allocate a queue and set up defaults */
	qg = (QUEUEGRP *) xalloc(sizeof(*qg));
	memset((char *) qg, '\0', sizeof(*qg));

	if (line[0] == '\0')
	{
		syserr("name required for queue");
		return;
	}

	/* collect the queue name */
	for (p = line;
	     *p != '\0' && *p != ',' && !(isascii(*p) && isspace(*p));
	     p++)
		continue;
	if (*p != '\0')
		*p++ = '\0';
	qg->qg_name = newstr(line);

	/* set default values, can be overridden below */
	set_def_queueval(qg, false);

	/* now scan through and assign info from the fields */
	while (*p != '\0')
	{
		auto char *delimptr;

		while (*p != '\0' &&
		       (*p == ',' || (isascii(*p) && isspace(*p))))
			p++;

		/* p now points to field code */
		fcode = *p;
		while (*p != '\0' && *p != '=' && *p != ',')
			p++;
		if (*p++ != '=')
		{
			syserr("queue %s: `=' expected", qg->qg_name);
			return;
		}
		while (isascii(*p) && isspace(*p))
			p++;

		/* p now points to the field body */
		p = munchstring(p, &delimptr, ',');

		/* install the field into the queue struct */
		switch (fcode)
		{
		  case 'P':		/* pathname */
			if (*p == '\0')
				syserr("queue %s: empty path name",
					qg->qg_name);
			else
				qg->qg_qdir = newstr(p);
			break;

		  case 'F':		/* flags */
			for (; *p != '\0'; p++)
				if (!(isascii(*p) && isspace(*p)))
					setbitn(*p, qg->qg_flags);
			break;

			/*
			**  Do we need two intervals here:
			**  One for persistent queue runners,
			**  one for "normal" queue runs?
			*/

		  case 'I':	/* interval between running the queue */
			qg->qg_queueintvl = convtime(p, 'm');
			break;

		  case 'N':		/* run niceness */
			qg->qg_nice = atoi(p);
			break;

		  case 'R':		/* maximum # of runners for the group */
			i = atoi(p);

			/* can't have more runners than allowed total */
			if (MaxQueueChildren > 0 && i > MaxQueueChildren)
			{
				qg->qg_maxqrun = MaxQueueChildren;
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Q=%s: R=%d exceeds MaxQueueChildren=%d, set to MaxQueueChildren\n",
						     qg->qg_name, i,
						     MaxQueueChildren);
			}
			else
				qg->qg_maxqrun = i;
			break;

		  case 'J':		/* maximum # of jobs in work list */
			qg->qg_maxlist = atoi(p);
			break;

		  case 'r':		/* max recipients per envelope */
			qg->qg_maxrcpt = atoi(p);
			break;

#if _FFR_QUEUE_GROUP_SORTORDER
		  case 'S':		/* queue sorting order */
			switch (*p)
			{
			  case 'h':	/* Host first */
			  case 'H':
				qg->qg_sortorder = QSO_BYHOST;
				break;

			  case 'p':	/* Priority order */
			  case 'P':
				qg->qg_sortorder = QSO_BYPRIORITY;
				break;

			  case 't':	/* Submission time */
			  case 'T':
				qg->qg_sortorder = QSO_BYTIME;
				break;

			  case 'f':	/* File name */
			  case 'F':
				qg->qg_sortorder = QSO_BYFILENAME;
				break;

			  case 'm':	/* Modification time */
			  case 'M':
				qg->qg_sortorder = QSO_BYMODTIME;
				break;

			  case 'r':	/* Random */
			  case 'R':
				qg->qg_sortorder = QSO_RANDOM;
				break;

# if _FFR_RHS
			  case 's':	/* Shuffled host name */
			  case 'S':
				qg->qg_sortorder = QSO_BYSHUFFLE;
				break;
# endif /* _FFR_RHS */

			  case 'n':	/* none */
			  case 'N':
				qg->qg_sortorder = QSO_NONE;
				break;

			  default:
				syserr("Invalid queue sort order \"%s\"", p);
			}
			break;
#endif /* _FFR_QUEUE_GROUP_SORTORDER */

		  default:
			syserr("Q%s: unknown queue equate %c=",
			       qg->qg_name, fcode);
			break;
		}

		p = delimptr;
	}

#if !HASNICE
	if (qg->qg_nice != NiceQueueRun)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Q%s: Warning: N= set on system that doesn't support nice()\n",
				     qg->qg_name);
	}
#endif /* !HASNICE */

	/* do some rationality checking */
	if (NumQueue >= MAXQUEUEGROUPS)
	{
		syserr("too many queue groups defined (%d max)",
			MAXQUEUEGROUPS);
		return;
	}

	if (qg->qg_qdir == NULL)
	{
		if (QueueDir == NULL || *QueueDir == '\0')
		{
			syserr("QueueDir must be defined before queue groups");
			return;
		}
		qg->qg_qdir = newstr(QueueDir);
	}

	if (qg->qg_maxqrun > 1 && !bitnset(QD_FORK, qg->qg_flags))
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Q=%s: R=%d: multiple queue runners specified\n\tbut flag '%c' is not set\n",
				     qg->qg_name, qg->qg_maxqrun, QD_FORK);
	}

	/* enter the queue into the symbol table */
	if (tTd(37, 8))
		sm_syslog(LOG_INFO, NOQID,
			  "Adding %s to stab, path: %s", qg->qg_name,
			  qg->qg_qdir);
	s = stab(qg->qg_name, ST_QUEUE, ST_ENTER);
	if (s->s_quegrp != NULL)
	{
		i = s->s_quegrp->qg_index;

		/* XXX what about the pointers inside this struct? */
		sm_free(s->s_quegrp); /* XXX */
	}
	else
		i = NumQueue++;
	Queue[i] = s->s_quegrp = qg;
	qg->qg_index = i;

	/* set default value for max queue runners */
	if (qg->qg_maxqrun < 0)
	{
		if (MaxRunnersPerQueue > 0)
			qg->qg_maxqrun = MaxRunnersPerQueue;
		else
			qg->qg_maxqrun = 1;
	}
	if (qdef)
		setbitn(QD_DEFINED, qg->qg_flags);
}
#if 0
/*
**  HASHFQN -- calculate a hash value for a fully qualified host name
**
**	Arguments:
**		fqn -- an all lower-case host.domain string
**		buckets -- the number of buckets (queue directories)
**
**	Returns:
**		a bucket number (signed integer)
**		-1 on error
**
**	Contributed by Exactis.com, Inc.
*/

int
hashfqn(fqn, buckets)
	register char *fqn;
	int buckets;
{
	register char *p;
	register int h = 0, hash, cnt;

	if (fqn == NULL)
		return -1;

	/*
	**  A variation on the gdb hash
	**  This is the best as of Feb 19, 1996 --bcx
	*/

	p = fqn;
	h = 0x238F13AF * strlen(p);
	for (cnt = 0; *p != 0; ++p, cnt++)
	{
		h = (h + (*p << (cnt * 5 % 24))) & 0x7FFFFFFF;
	}
	h = (1103515243 * h + 12345) & 0x7FFFFFFF;
	if (buckets < 2)
		hash = 0;
	else
		hash = (h % buckets);

	return hash;
}
#endif /* 0 */

/*
**  A structure for sorting Queue according to maxqrun without
**	screwing up Queue itself.
*/

struct sortqgrp
{
	int sg_idx;		/* original index */
	int sg_maxqrun;		/* max queue runners */
};
typedef struct sortqgrp	SORTQGRP_T;
static int cmpidx __P((const void *, const void *));

static int
cmpidx(a, b)
	const void *a;
	const void *b;
{
	/* The sort is highest to lowest, so the comparison is reversed */
	if (((SORTQGRP_T *)a)->sg_maxqrun < ((SORTQGRP_T *)b)->sg_maxqrun)
		return 1;
	else if (((SORTQGRP_T *)a)->sg_maxqrun > ((SORTQGRP_T *)b)->sg_maxqrun)
		return -1;
	else
		return 0;
}

/*
**  MAKEWORKGROUPS -- balance queue groups into work groups per MaxQueueChildren
**
**  Take the now defined queue groups and assign them to work groups.
**  This is done to balance out the number of concurrently active
**  queue runners such that MaxQueueChildren is not exceeded. This may
**  result in more than one queue group per work group. In such a case
**  the number of running queue groups in that work group will have no
**  more than the work group maximum number of runners (a "fair" portion
**  of MaxQueueRunners). All queue groups within a work group will get a
**  chance at running.
**
**	Parameters:
**		none.
**
**	Returns:
**		nothing.
**
**	Side Effects:
**		Sets up WorkGrp structure.
*/

void
makeworkgroups()
{
	int i, j, total_runners, dir, h;
	SORTQGRP_T si[MAXQUEUEGROUPS + 1];

	total_runners = 0;
	if (NumQueue == 1 && strcmp(Queue[0]->qg_name, "mqueue") == 0)
	{
		/*
		**  There is only the "mqueue" queue group (a default)
		**  containing all of the queues. We want to provide to
		**  this queue group the maximum allowable queue runners.
		**  To match older behavior (8.10/8.11) we'll try for
		**  1 runner per queue capping it at MaxQueueChildren.
		**  So if there are N queues, then there will be N runners
		**  for the "mqueue" queue group (where N is kept less than
		**  MaxQueueChildren).
		*/

		NumWorkGroups = 1;
		WorkGrp[0].wg_numqgrp = 1;
		WorkGrp[0].wg_qgs = (QUEUEGRP **) xalloc(sizeof(QUEUEGRP *));
		WorkGrp[0].wg_qgs[0] = Queue[0];
		if (MaxQueueChildren > 0 &&
		    Queue[0]->qg_numqueues > MaxQueueChildren)
			WorkGrp[0].wg_runners = MaxQueueChildren;
		else
			WorkGrp[0].wg_runners = Queue[0]->qg_numqueues;

		Queue[0]->qg_wgrp = 0;

		/* can't have more runners than allowed total */
		if (MaxQueueChildren > 0 &&
		    Queue[0]->qg_maxqrun > MaxQueueChildren)
			Queue[0]->qg_maxqrun = MaxQueueChildren;
		WorkGrp[0].wg_maxact = Queue[0]->qg_maxqrun;
		WorkGrp[0].wg_lowqintvl = Queue[0]->qg_queueintvl;
		return;
	}

	for (i = 0; i < NumQueue; i++)
	{
		si[i].sg_maxqrun = Queue[i]->qg_maxqrun;
		si[i].sg_idx = i;
	}
	qsort(si, NumQueue, sizeof(si[0]), cmpidx);

	NumWorkGroups = 0;
	for (i = 0; i < NumQueue; i++)
	{
		SKIP_BOUNCE_QUEUE
		total_runners += si[i].sg_maxqrun;
		if (MaxQueueChildren <= 0 || total_runners <= MaxQueueChildren)
			NumWorkGroups++;
		else
			break;
	}

	if (NumWorkGroups < 1)
		NumWorkGroups = 1; /* gotta have one at least */
	else if (NumWorkGroups > MAXWORKGROUPS)
		NumWorkGroups = MAXWORKGROUPS; /* the limit */

	/*
	**  We now know the number of work groups to pack the queue groups
	**  into. The queue groups in 'Queue' are sorted from highest
	**  to lowest for the number of runners per queue group.
	**  We put the queue groups with the largest number of runners
	**  into work groups first. Then the smaller ones are fitted in
	**  where it looks best.
	*/

	j = 0;
	dir = 1;
	for (i = 0; i < NumQueue; i++)
	{
		SKIP_BOUNCE_QUEUE

		/* a to-and-fro packing scheme, continue from last position */
		if (j >= NumWorkGroups)
		{
			dir = -1;
			j = NumWorkGroups - 1;
		}
		else if (j < 0)
		{
			j = 0;
			dir = 1;
		}

		if (WorkGrp[j].wg_qgs == NULL)
			WorkGrp[j].wg_qgs = (QUEUEGRP **)sm_malloc(sizeof(QUEUEGRP *) *
							(WorkGrp[j].wg_numqgrp + 1));
		else
			WorkGrp[j].wg_qgs = (QUEUEGRP **)sm_realloc(WorkGrp[j].wg_qgs,
							sizeof(QUEUEGRP *) *
							(WorkGrp[j].wg_numqgrp + 1));
		if (WorkGrp[j].wg_qgs == NULL)
		{
			syserr("!cannot allocate memory for work queues, need %d bytes",
			       (int) (sizeof(QUEUEGRP *) *
				      (WorkGrp[j].wg_numqgrp + 1)));
		}

		h = si[i].sg_idx;
		WorkGrp[j].wg_qgs[WorkGrp[j].wg_numqgrp] = Queue[h];
		WorkGrp[j].wg_numqgrp++;
		WorkGrp[j].wg_runners += Queue[h]->qg_maxqrun;
		Queue[h]->qg_wgrp = j;

		if (WorkGrp[j].wg_maxact == 0)
		{
			/* can't have more runners than allowed total */
			if (MaxQueueChildren > 0 &&
			    Queue[h]->qg_maxqrun > MaxQueueChildren)
				Queue[h]->qg_maxqrun = MaxQueueChildren;
			WorkGrp[j].wg_maxact = Queue[h]->qg_maxqrun;
		}

		/*
		**  XXX: must wg_lowqintvl be the GCD?
		**  qg1: 2m, qg2: 3m, minimum: 2m, when do queue runs for
		**  qg2 occur?
		*/

		/* keep track of the lowest interval for a persistent runner */
		if (Queue[h]->qg_queueintvl > 0 &&
		    WorkGrp[j].wg_lowqintvl < Queue[h]->qg_queueintvl)
			WorkGrp[j].wg_lowqintvl = Queue[h]->qg_queueintvl;
		j += dir;
	}
	if (tTd(41, 9))
	{
		for (i = 0; i < NumWorkGroups; i++)
		{
			sm_dprintf("Workgroup[%d]=", i);
			for (j = 0; j < WorkGrp[i].wg_numqgrp; j++)
			{
				sm_dprintf("%s, ",
					WorkGrp[i].wg_qgs[j]->qg_name);
			}
			sm_dprintf("\n");
		}
	}
}

/*
**  DUP_DF -- duplicate envelope data file
**
**	Copy the data file from the 'old' envelope to the 'new' envelope
**	in the most efficient way possible.
**
**	Create a hard link from the 'old' data file to the 'new' data file.
**	If the old and new queue directories are on different file systems,
**	then the new data file link is created in the old queue directory,
**	and the new queue file will contain a 'd' record pointing to the
**	directory containing the new data file.
**
**	Parameters:
**		old -- old envelope.
**		new -- new envelope.
**
**	Results:
**		Returns true on success, false on failure.
**
**	Side Effects:
**		On success, the new data file is created.
**		On fatal failure, EF_FATALERRS is set in old->e_flags.
*/

static bool	dup_df __P((ENVELOPE *, ENVELOPE *));

static bool
dup_df(old, new)
	ENVELOPE *old;
	ENVELOPE *new;
{
	int ofs, nfs, r;
	char opath[MAXPATHLEN];
	char npath[MAXPATHLEN];

	if (!bitset(EF_HAS_DF, old->e_flags))
	{
		/*
		**  this can happen if: SuperSafe != True
		**  and a bounce mail is sent that is split.
		*/

		queueup(old, false, true);
	}
	SM_REQUIRE(ISVALIDQGRP(old->e_qgrp) && ISVALIDQDIR(old->e_qdir));
	SM_REQUIRE(ISVALIDQGRP(new->e_qgrp) && ISVALIDQDIR(new->e_qdir));

	(void) sm_strlcpy(opath, queuename(old, DATAFL_LETTER), sizeof(opath));
	(void) sm_strlcpy(npath, queuename(new, DATAFL_LETTER), sizeof(npath));

	if (old->e_dfp != NULL)
	{
		r = sm_io_setinfo(old->e_dfp, SM_BF_COMMIT, NULL);
		if (r < 0 && errno != EINVAL)
		{
			syserr("@can't commit %s", opath);
			old->e_flags |= EF_FATALERRS;
			return false;
		}
	}

	/*
	**  Attempt to create a hard link, if we think both old and new
	**  are on the same file system, otherwise copy the file.
	**
	**  Don't waste time attempting a hard link unless old and new
	**  are on the same file system.
	*/

	SM_REQUIRE(ISVALIDQGRP(old->e_dfqgrp) && ISVALIDQDIR(old->e_dfqdir));
	SM_REQUIRE(ISVALIDQGRP(new->e_dfqgrp) && ISVALIDQDIR(new->e_dfqdir));

	ofs = Queue[old->e_dfqgrp]->qg_qpaths[old->e_dfqdir].qp_fsysidx;
	nfs = Queue[new->e_dfqgrp]->qg_qpaths[new->e_dfqdir].qp_fsysidx;
	if (FILE_SYS_DEV(ofs) == FILE_SYS_DEV(nfs))
	{
		if (link(opath, npath) == 0)
		{
			new->e_flags |= EF_HAS_DF;
			SYNC_DIR(npath, true);
			return true;
		}
		goto error;
	}

	/*
	**  Can't link across queue directories, so try to create a hard
	**  link in the same queue directory as the old df file.
	**  The qf file will refer to the new df file using a 'd' record.
	*/

	new->e_dfqgrp = old->e_dfqgrp;
	new->e_dfqdir = old->e_dfqdir;
	(void) sm_strlcpy(npath, queuename(new, DATAFL_LETTER), sizeof(npath));
	if (link(opath, npath) == 0)
	{
		new->e_flags |= EF_HAS_DF;
		SYNC_DIR(npath, true);
		return true;
	}

  error:
	if (LogLevel > 0)
		sm_syslog(LOG_ERR, old->e_id,
			  "dup_df: can't link %s to %s, error=%s, envelope splitting failed",
			  opath, npath, sm_errstring(errno));
	return false;
}

/*
**  SPLIT_ENV -- Allocate a new envelope based on a given envelope.
**
**	Parameters:
**		e -- envelope.
**		sendqueue -- sendqueue for new envelope.
**		qgrp -- index of queue group.
**		qdir -- queue directory.
**
**	Results:
**		new envelope.
**
*/

static ENVELOPE	*split_env __P((ENVELOPE *, ADDRESS *, int, int));

static ENVELOPE *
split_env(e, sendqueue, qgrp, qdir)
	ENVELOPE *e;
	ADDRESS *sendqueue;
	int qgrp;
	int qdir;
{
	ENVELOPE *ee;

	ee = (ENVELOPE *) sm_rpool_malloc_x(e->e_rpool, sizeof(*ee));
	STRUCTCOPY(*e, *ee);
	ee->e_message = NULL;	/* XXX use original message? */
	ee->e_id = NULL;
	assign_queueid(ee);
	ee->e_sendqueue = sendqueue;
	ee->e_flags &= ~(EF_INQUEUE|EF_CLRQUEUE|EF_FATALERRS
			 |EF_SENDRECEIPT|EF_RET_PARAM|EF_HAS_DF);
	ee->e_flags |= EF_NORECEIPT;	/* XXX really? */
	ee->e_from.q_state = QS_SENDER;
	ee->e_dfp = NULL;
	ee->e_lockfp = NULL;
	if (e->e_xfp != NULL)
		ee->e_xfp = sm_io_dup(e->e_xfp);

	/* failed to dup e->e_xfp, start a new transcript */
	if (ee->e_xfp == NULL)
		openxscript(ee);

	ee->e_qgrp = ee->e_dfqgrp = qgrp;
	ee->e_qdir = ee->e_dfqdir = qdir;
	ee->e_errormode = EM_MAIL;
	ee->e_statmsg = NULL;
	if (e->e_quarmsg != NULL)
		ee->e_quarmsg = sm_rpool_strdup_x(ee->e_rpool,
						  e->e_quarmsg);

	/*
	**  XXX Not sure if this copying is necessary.
	**  sendall() does this copying, but I (dm) don't know if that is
	**  because of the storage management discipline we were using
	**  before rpools were introduced, or if it is because these lists
	**  can be modified later.
	*/

	ee->e_header = copyheader(e->e_header, ee->e_rpool);
	ee->e_errorqueue = copyqueue(e->e_errorqueue, ee->e_rpool);

	return ee;
}

/* return values from split functions, check also below! */
#define SM_SPLIT_FAIL	(0)
#define SM_SPLIT_NONE	(1)
#define SM_SPLIT_NEW(n)	(1 + (n))

/*
**  SPLIT_ACROSS_QUEUE_GROUPS
**
**	This function splits an envelope across multiple queue groups
**	based on the queue group of each recipient.
**
**	Parameters:
**		e -- envelope.
**
**	Results:
**		SM_SPLIT_FAIL on failure
**		SM_SPLIT_NONE if no splitting occurred,
**		or 1 + the number of additional envelopes created.
**
**	Side Effects:
**		On success, e->e_sibling points to a list of zero or more
**		additional envelopes, and the associated data files exist
**		on disk.  But the queue files are not created.
**
**		On failure, e->e_sibling is not changed.
**		The order of recipients in e->e_sendqueue is permuted.
**		Abandoned data files for additional envelopes that failed
**		to be created may exist on disk.
*/

static int	q_qgrp_compare __P((const void *, const void *));
static int	e_filesys_compare __P((const void *, const void *));

static int
q_qgrp_compare(p1, p2)
	const void *p1;
	const void *p2;
{
	ADDRESS **pq1 = (ADDRESS **) p1;
	ADDRESS **pq2 = (ADDRESS **) p2;

	return (*pq1)->q_qgrp - (*pq2)->q_qgrp;
}

static int
e_filesys_compare(p1, p2)
	const void *p1;
	const void *p2;
{
	ENVELOPE **pe1 = (ENVELOPE **) p1;
	ENVELOPE **pe2 = (ENVELOPE **) p2;
	int fs1, fs2;

	fs1 = Queue[(*pe1)->e_qgrp]->qg_qpaths[(*pe1)->e_qdir].qp_fsysidx;
	fs2 = Queue[(*pe2)->e_qgrp]->qg_qpaths[(*pe2)->e_qdir].qp_fsysidx;
	if (FILE_SYS_DEV(fs1) < FILE_SYS_DEV(fs2))
		return -1;
	if (FILE_SYS_DEV(fs1) > FILE_SYS_DEV(fs2))
		return 1;
	return 0;
}

static int split_across_queue_groups __P((ENVELOPE *));
static int
split_across_queue_groups(e)
	ENVELOPE *e;
{
	int naddrs, nsplits, i;
	bool changed;
	char **pvp;
	ADDRESS *q, **addrs;
	ENVELOPE *ee, *es;
	ENVELOPE *splits[MAXQUEUEGROUPS];
	char pvpbuf[PSBUFSIZE];

	SM_REQUIRE(ISVALIDQGRP(e->e_qgrp));

	/* Count addresses and assign queue groups. */
	naddrs = 0;
	changed = false;
	for (q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_DEAD(q->q_state))
			continue;
		++naddrs;

		/* bad addresses and those already sent stay put */
		if (QS_IS_BADADDR(q->q_state) ||
		    QS_IS_SENT(q->q_state))
			q->q_qgrp = e->e_qgrp;
		else if (!ISVALIDQGRP(q->q_qgrp))
		{
			/* call ruleset which should return a queue group */
			i = rscap(RS_QUEUEGROUP, q->q_user, NULL, e, &pvp,
				  pvpbuf, sizeof(pvpbuf));
			if (i == EX_OK &&
			    pvp != NULL && pvp[0] != NULL &&
			    (pvp[0][0] & 0377) == CANONNET &&
			    pvp[1] != NULL && pvp[1][0] != '\0')
			{
				i = name2qid(pvp[1]);
				if (ISVALIDQGRP(i))
				{
					q->q_qgrp = i;
					changed = true;
					if (tTd(20, 4))
						sm_syslog(LOG_INFO, NOQID,
							"queue group name %s -> %d",
							pvp[1], i);
					continue;
				}
				else if (LogLevel > 10)
					sm_syslog(LOG_INFO, NOQID,
						"can't find queue group name %s, selection ignored",
						pvp[1]);
			}
			if (q->q_mailer != NULL &&
			    ISVALIDQGRP(q->q_mailer->m_qgrp))
			{
				changed = true;
				q->q_qgrp = q->q_mailer->m_qgrp;
			}
			else if (ISVALIDQGRP(e->e_qgrp))
				q->q_qgrp = e->e_qgrp;
			else
				q->q_qgrp = 0;
		}
	}

	/* only one address? nothing to split. */
	if (naddrs <= 1 && !changed)
		return SM_SPLIT_NONE;

	/* sort the addresses by queue group */
	addrs = sm_rpool_malloc_x(e->e_rpool, naddrs * sizeof(ADDRESS *));
	for (i = 0, q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_DEAD(q->q_state))
			continue;
		addrs[i++] = q;
	}
	qsort(addrs, naddrs, sizeof(ADDRESS *), q_qgrp_compare);

	/* split into multiple envelopes, by queue group */
	nsplits = 0;
	es = NULL;
	e->e_sendqueue = NULL;
	for (i = 0; i < naddrs; ++i)
	{
		if (i == naddrs - 1 || addrs[i]->q_qgrp != addrs[i + 1]->q_qgrp)
			addrs[i]->q_next = NULL;
		else
			addrs[i]->q_next = addrs[i + 1];

		/* same queue group as original envelope? */
		if (addrs[i]->q_qgrp == e->e_qgrp)
		{
			if (e->e_sendqueue == NULL)
				e->e_sendqueue = addrs[i];
			continue;
		}

		/* different queue group than original envelope */
		if (es == NULL || addrs[i]->q_qgrp != es->e_qgrp)
		{
			ee = split_env(e, addrs[i], addrs[i]->q_qgrp, NOQDIR);
			es = ee;
			splits[nsplits++] = ee;
		}
	}

	/* no splits? return right now. */
	if (nsplits <= 0)
		return SM_SPLIT_NONE;

	/* assign a queue directory to each additional envelope */
	for (i = 0; i < nsplits; ++i)
	{
		es = splits[i];
#if 0
		es->e_qdir = pickqdir(Queue[es->e_qgrp], es->e_msgsize, es);
#endif /* 0 */
		if (!setnewqueue(es))
			goto failure;
	}

	/* sort the additional envelopes by queue file system */
	qsort(splits, nsplits, sizeof(ENVELOPE *), e_filesys_compare);

	/* create data files for each additional envelope */
	if (!dup_df(e, splits[0]))
	{
		i = 0;
		goto failure;
	}
	for (i = 1; i < nsplits; ++i)
	{
		/* copy or link to the previous data file */
		if (!dup_df(splits[i - 1], splits[i]))
			goto failure;
	}

	/* success: prepend the new envelopes to the e->e_sibling list */
	for (i = 0; i < nsplits; ++i)
	{
		es = splits[i];
		es->e_sibling = e->e_sibling;
		e->e_sibling = es;
	}
	return SM_SPLIT_NEW(nsplits);

	/* failure: clean up */
  failure:
	if (i > 0)
	{
		int j;

		for (j = 0; j < i; j++)
			(void) unlink(queuename(splits[j], DATAFL_LETTER));
	}
	e->e_sendqueue = addrs[0];
	for (i = 0; i < naddrs - 1; ++i)
		addrs[i]->q_next = addrs[i + 1];
	addrs[naddrs - 1]->q_next = NULL;
	return SM_SPLIT_FAIL;
}

/*
**  SPLIT_WITHIN_QUEUE
**
**	Split an envelope with multiple recipients into several
**	envelopes within the same queue directory, if the number of
**	recipients exceeds the limit for the queue group.
**
**	Parameters:
**		e -- envelope.
**
**	Results:
**		SM_SPLIT_FAIL on failure
**		SM_SPLIT_NONE if no splitting occurred,
**		or 1 + the number of additional envelopes created.
*/

#define SPLIT_LOG_LEVEL	8

static int	split_within_queue __P((ENVELOPE *));

static int
split_within_queue(e)
	ENVELOPE *e;
{
	int maxrcpt, nrcpt, ndead, nsplit, i;
	int j, l;
	char *lsplits;
	ADDRESS *q, **addrs;
	ENVELOPE *ee, *firstsibling;

	if (!ISVALIDQGRP(e->e_qgrp) || bitset(EF_SPLIT, e->e_flags))
		return SM_SPLIT_NONE;

	/* don't bother if there is no recipient limit */
	maxrcpt = Queue[e->e_qgrp]->qg_maxrcpt;
	if (maxrcpt <= 0)
		return SM_SPLIT_NONE;

	/* count recipients */
	nrcpt = 0;
	for (q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_DEAD(q->q_state))
			continue;
		++nrcpt;
	}
	if (nrcpt <= maxrcpt)
		return SM_SPLIT_NONE;

	/*
	**  Preserve the recipient list
	**  so that we can restore it in case of error.
	**  (But we discard dead addresses.)
	*/

	addrs = sm_rpool_malloc_x(e->e_rpool, nrcpt * sizeof(ADDRESS *));
	for (i = 0, q = e->e_sendqueue; q != NULL; q = q->q_next)
	{
		if (QS_IS_DEAD(q->q_state))
			continue;
		addrs[i++] = q;
	}

	/*
	**  Partition the recipient list so that bad and sent addresses
	**  come first. These will go with the original envelope, and
	**  do not count towards the maxrcpt limit.
	**  addrs[] does not contain QS_IS_DEAD() addresses.
	*/

	ndead = 0;
	for (i = 0; i < nrcpt; ++i)
	{
		if (QS_IS_BADADDR(addrs[i]->q_state) ||
		    QS_IS_SENT(addrs[i]->q_state) ||
		    QS_IS_DEAD(addrs[i]->q_state)) /* for paranoia's sake */
		{
			if (i > ndead)
			{
				ADDRESS *tmp = addrs[i];

				addrs[i] = addrs[ndead];
				addrs[ndead] = tmp;
			}
			++ndead;
		}
	}

	/* Check if no splitting required. */
	if (nrcpt - ndead <= maxrcpt)
		return SM_SPLIT_NONE;

	/* fix links */
	for (i = 0; i < nrcpt - 1; ++i)
		addrs[i]->q_next = addrs[i + 1];
	addrs[nrcpt - 1]->q_next = NULL;
	e->e_sendqueue = addrs[0];

	/* prepare buffer for logging */
	if (LogLevel > SPLIT_LOG_LEVEL)
	{
		l = MAXLINE;
		lsplits = sm_malloc(l);
		if (lsplits != NULL)
			*lsplits = '\0';
		j = 0;
	}
	else
	{
		/* get rid of stupid compiler warnings */
		lsplits = NULL;
		j = l = 0;
	}

	/* split the envelope */
	firstsibling = e->e_sibling;
	i = maxrcpt + ndead;
	nsplit = 0;
	for (;;)
	{
		addrs[i - 1]->q_next = NULL;
		ee = split_env(e, addrs[i], e->e_qgrp, e->e_qdir);
		if (!dup_df(e, ee))
		{

			ee = firstsibling;
			while (ee != NULL)
			{
				(void) unlink(queuename(ee, DATAFL_LETTER));
				ee = ee->e_sibling;
			}

			/* Error.  Restore e's sibling & recipient lists. */
			e->e_sibling = firstsibling;
			for (i = 0; i < nrcpt - 1; ++i)
				addrs[i]->q_next = addrs[i + 1];
			if (lsplits != NULL)
				sm_free(lsplits);
			return SM_SPLIT_FAIL;
		}

		/* prepend the new envelope to e->e_sibling */
		ee->e_sibling = e->e_sibling;
		e->e_sibling = ee;
		++nsplit;
		if (LogLevel > SPLIT_LOG_LEVEL && lsplits != NULL)
		{
			if (j >= l - strlen(ee->e_id) - 3)
			{
				char *p;

				l += MAXLINE;
				p = sm_realloc(lsplits, l);
				if (p == NULL)
				{
					/* let's try to get this done */
					sm_free(lsplits);
					lsplits = NULL;
				}
				else
					lsplits = p;
			}
			if (lsplits != NULL)
			{
				if (j == 0)
					j += sm_strlcat(lsplits + j,
							ee->e_id,
							l - j);
				else
					j += sm_strlcat2(lsplits + j,
							 "; ",
							 ee->e_id,
							 l - j);
				SM_ASSERT(j < l);
			}
		}
		if (nrcpt - i <= maxrcpt)
			break;
		i += maxrcpt;
	}
	if (LogLevel > SPLIT_LOG_LEVEL && lsplits != NULL)
	{
		if (nsplit > 0)
		{
			sm_syslog(LOG_NOTICE, e->e_id,
				  "split: maxrcpts=%d, rcpts=%d, count=%d, id%s=%s",
				  maxrcpt, nrcpt - ndead, nsplit,
				  nsplit > 1 ? "s" : "", lsplits);
		}
		sm_free(lsplits);
	}
	return SM_SPLIT_NEW(nsplit);
}
/*
**  SPLIT_BY_RECIPIENT
**
**	Split an envelope with multiple recipients into multiple
**	envelopes as required by the sendmail configuration.
**
**	Parameters:
**		e -- envelope.
**
**	Results:
**		Returns true on success, false on failure.
**
**	Side Effects:
**		see split_across_queue_groups(), split_within_queue(e)
*/

bool
split_by_recipient(e)
	ENVELOPE *e;
{
	int split, n, i, j, l;
	char *lsplits;
	ENVELOPE *ee, *next, *firstsibling;

	if (OpMode == SM_VERIFY || !ISVALIDQGRP(e->e_qgrp) ||
	    bitset(EF_SPLIT, e->e_flags))
		return true;
	n = split_across_queue_groups(e);
	if (n == SM_SPLIT_FAIL)
		return false;
	firstsibling = ee = e->e_sibling;
	if (n > 1 && LogLevel > SPLIT_LOG_LEVEL)
	{
		l = MAXLINE;
		lsplits = sm_malloc(l);
		if (lsplits != NULL)
			*lsplits = '\0';
		j = 0;
	}
	else
	{
		/* get rid of stupid compiler warnings */
		lsplits = NULL;
		j = l = 0;
	}
	for (i = 1; i < n; ++i)
	{
		next = ee->e_sibling;
		if (split_within_queue(ee) == SM_SPLIT_FAIL)
		{
			e->e_sibling = firstsibling;
			return false;
		}
		ee->e_flags |= EF_SPLIT;
		if (LogLevel > SPLIT_LOG_LEVEL && lsplits != NULL)
		{
			if (j >= l - strlen(ee->e_id) - 3)
			{
				char *p;

				l += MAXLINE;
				p = sm_realloc(lsplits, l);
				if (p == NULL)
				{
					/* let's try to get this done */
					sm_free(lsplits);
					lsplits = NULL;
				}
				else
					lsplits = p;
			}
			if (lsplits != NULL)
			{
				if (j == 0)
					j += sm_strlcat(lsplits + j,
							ee->e_id, l - j);
				else
					j += sm_strlcat2(lsplits + j, "; ",
							 ee->e_id, l - j);
				SM_ASSERT(j < l);
			}
		}
		ee = next;
	}
	if (LogLevel > SPLIT_LOG_LEVEL && lsplits != NULL && n > 1)
	{
		sm_syslog(LOG_NOTICE, e->e_id, "split: count=%d, id%s=%s",
			  n - 1, n > 2 ? "s" : "", lsplits);
		sm_free(lsplits);
	}
	split = split_within_queue(e) != SM_SPLIT_FAIL;
	if (split)
		e->e_flags |= EF_SPLIT;
	return split;
}

/*
**  QUARANTINE_QUEUE_ITEM -- {un,}quarantine a single envelope
**
**	Add/remove quarantine reason and requeue appropriately.
**
**	Parameters:
**		qgrp -- queue group for the item
**		qdir -- queue directory in the given queue group
**		e -- envelope information for the item
**		reason -- quarantine reason, NULL means unquarantine.
**
**	Results:
**		true if item changed, false otherwise
**
**	Side Effects:
**		Changes quarantine tag in queue file and renames it.
*/

static bool
quarantine_queue_item(qgrp, qdir, e, reason)
	int qgrp;
	int qdir;
	ENVELOPE *e;
	char *reason;
{
	bool dirty = false;
	bool failing = false;
	bool foundq = false;
	bool finished = false;
	int fd;
	int flags;
	int oldtype;
	int newtype;
	int save_errno;
	MODE_T oldumask = 0;
	SM_FILE_T *oldqfp, *tempqfp;
	char *bp;
	int bufsize;
	char oldqf[MAXPATHLEN];
	char tempqf[MAXPATHLEN];
	char newqf[MAXPATHLEN];
	char buf[MAXLINE];

	oldtype = queue_letter(e, ANYQFL_LETTER);
	(void) sm_strlcpy(oldqf, queuename(e, ANYQFL_LETTER), sizeof(oldqf));
	(void) sm_strlcpy(tempqf, queuename(e, NEWQFL_LETTER), sizeof(tempqf));

	/*
	**  Instead of duplicating all the open
	**  and lock code here, tell readqf() to
	**  do that work and return the open
	**  file pointer in e_lockfp.  Note that
	**  we must release the locks properly when
	**  we are done.
	*/

	if (!readqf(e, true))
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s\n", qid_printname(e));
		return false;
	}
	oldqfp = e->e_lockfp;

	/* open the new queue file */
	flags = O_CREAT|O_WRONLY|O_EXCL;
	if (bitset(S_IWGRP, QueueFileMode))
		oldumask = umask(002);
	fd = open(tempqf, flags, QueueFileMode);
	if (bitset(S_IWGRP, QueueFileMode))
		(void) umask(oldumask);
	RELEASE_QUEUE;

	if (fd < 0)
	{
		save_errno = errno;
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s: Could not open %s: %s\n",
				     qid_printname(e), tempqf,
				     sm_errstring(save_errno));
		(void) sm_io_close(oldqfp, SM_TIME_DEFAULT);
		return false;
	}
	if (!lockfile(fd, tempqf, NULL, LOCK_EX|LOCK_NB))
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s: Could not lock %s\n",
				     qid_printname(e), tempqf);
		(void) close(fd);
		(void) sm_io_close(oldqfp, SM_TIME_DEFAULT);
		return false;
	}

	tempqfp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT, (void *) &fd,
			     SM_IO_WRONLY_B, NULL);
	if (tempqfp == NULL)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s: Could not lock %s\n",
				     qid_printname(e), tempqf);
		(void) close(fd);
		(void) sm_io_close(oldqfp, SM_TIME_DEFAULT);
		return false;
	}

	/* Copy the data over, changing the quarantine reason */
	while (bufsize = sizeof(buf),
	       (bp = fgetfolded(buf, &bufsize, oldqfp)) != NULL)
	{
		if (tTd(40, 4))
			sm_dprintf("+++++ %s\n", bp);
		switch (bp[0])
		{
		  case 'q':		/* quarantine reason */
			foundq = true;
			if (reason == NULL)
			{
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "%s: Removed quarantine of \"%s\"\n",
							     e->e_id, &bp[1]);
				}
				sm_syslog(LOG_INFO, e->e_id, "unquarantine");
				dirty = true;
			}
			else if (strcmp(reason, &bp[1]) == 0)
			{
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "%s: Already quarantined with \"%s\"\n",
							     e->e_id, reason);
				}
				(void) sm_io_fprintf(tempqfp, SM_TIME_DEFAULT,
						     "q%s\n", reason);
			}
			else
			{
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "%s: Quarantine changed from \"%s\" to \"%s\"\n",
							     e->e_id, &bp[1],
							     reason);
				}
				(void) sm_io_fprintf(tempqfp, SM_TIME_DEFAULT,
						     "q%s\n", reason);
				sm_syslog(LOG_INFO, e->e_id, "quarantine=%s",
					  reason);
				dirty = true;
			}
			break;

		  case 'S':
			/*
			**  If we are quarantining an unquarantined item,
			**  need to put in a new 'q' line before it's
			**  too late.
			*/

			if (!foundq && reason != NULL)
			{
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "%s: Quarantined with \"%s\"\n",
							     e->e_id, reason);
				}
				(void) sm_io_fprintf(tempqfp, SM_TIME_DEFAULT,
						     "q%s\n", reason);
				sm_syslog(LOG_INFO, e->e_id, "quarantine=%s",
					  reason);
				foundq = true;
				dirty = true;
			}

			/* Copy the line to the new file */
			(void) sm_io_fprintf(tempqfp, SM_TIME_DEFAULT,
					     "%s\n", bp);
			break;

		  case '.':
			finished = true;
			/* FALLTHROUGH */

		  default:
			/* Copy the line to the new file */
			(void) sm_io_fprintf(tempqfp, SM_TIME_DEFAULT,
					     "%s\n", bp);
			break;
		}
		if (bp != buf)
			sm_free(bp);
	}

	/* Make sure we read the whole old file */
	errno = sm_io_error(tempqfp);
	if (errno != 0 && errno != SM_IO_EOF)
	{
		save_errno = errno;
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s: Error reading %s: %s\n",
				     qid_printname(e), oldqf,
				     sm_errstring(save_errno));
		failing = true;
	}

	if (!failing && !finished)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s: Incomplete file: %s\n",
				     qid_printname(e), oldqf);
		failing = true;
	}

	/* Check if we actually changed anything or we can just bail now */
	if (!dirty)
	{
		/* pretend we failed, even though we technically didn't */
		failing = true;
	}

	/* Make sure we wrote things out safely */
	if (!failing &&
	    (sm_io_flush(tempqfp, SM_TIME_DEFAULT) != 0 ||
	     ((SuperSafe == SAFE_REALLY ||
	       SuperSafe == SAFE_REALLY_POSTMILTER ||
	       SuperSafe == SAFE_INTERACTIVE) &&
	      fsync(sm_io_getinfo(tempqfp, SM_IO_WHAT_FD, NULL)) < 0) ||
	     ((errno = sm_io_error(tempqfp)) != 0)))
	{
		save_errno = errno;
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Skipping %s: Error writing %s: %s\n",
				     qid_printname(e), tempqf,
				     sm_errstring(save_errno));
		failing = true;
	}


	/* Figure out the new filename */
	newtype = (reason == NULL ? NORMQF_LETTER : QUARQF_LETTER);
	if (oldtype == newtype)
	{
		/* going to rename tempqf to oldqf */
		(void) sm_strlcpy(newqf, oldqf, sizeof(newqf));
	}
	else
	{
		/* going to rename tempqf to new name based on newtype */
		(void) sm_strlcpy(newqf, queuename(e, newtype), sizeof(newqf));
	}

	save_errno = 0;

	/* rename tempqf to newqf */
	if (!failing &&
	    rename(tempqf, newqf) < 0)
		save_errno = (errno == 0) ? EINVAL : errno;

	/* Check rename() success */
	if (!failing && save_errno != 0)
	{
		sm_syslog(LOG_DEBUG, e->e_id,
			  "quarantine_queue_item: rename(%s, %s): %s",
			  tempqf, newqf, sm_errstring(save_errno));

		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Error renaming %s to %s: %s\n",
				     tempqf, newqf,
				     sm_errstring(save_errno));
		if (oldtype == newtype)
		{
			/*
			**  Bail here since we don't know the state of
			**  the filesystem and may need to keep tempqf
			**  for the user to rescue us.
			*/

			RELEASE_QUEUE;
			errno = save_errno;
			syserr("!452 Error renaming control file %s", tempqf);
			/* NOTREACHED */
		}
		else
		{
			/* remove new file (if rename() half completed) */
			if (xunlink(newqf) < 0)
			{
				save_errno = errno;
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Error removing %s: %s\n",
						     newqf,
						     sm_errstring(save_errno));
			}

			/* tempqf removed below */
			failing = true;
		}

	}

	/* If changing file types, need to remove old type */
	if (!failing && oldtype != newtype)
	{
		if (xunlink(oldqf) < 0)
		{
			save_errno = errno;
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Error removing %s: %s\n",
					     oldqf, sm_errstring(save_errno));
		}
	}

	/* see if anything above failed */
	if (failing)
	{
		/* Something failed: remove new file, old file still there */
		(void) xunlink(tempqf);
	}

	/*
	**  fsync() after file operations to make sure metadata is
	**  written to disk on filesystems in which renames are
	**  not guaranteed.  It's ok if they fail, mail won't be lost.
	*/

	if (SuperSafe != SAFE_NO)
	{
		/* for soft-updates */
		(void) fsync(sm_io_getinfo(tempqfp,
					   SM_IO_WHAT_FD, NULL));

		if (!failing)
		{
			/* for soft-updates */
			(void) fsync(sm_io_getinfo(oldqfp,
						   SM_IO_WHAT_FD, NULL));
		}

		/* for other odd filesystems */
		SYNC_DIR(tempqf, false);
	}

	/* Close up shop */
	RELEASE_QUEUE;
	if (tempqfp != NULL)
		(void) sm_io_close(tempqfp, SM_TIME_DEFAULT);
	if (oldqfp != NULL)
		(void) sm_io_close(oldqfp, SM_TIME_DEFAULT);

	/* All went well */
	return !failing;
}

/*
**  QUARANTINE_QUEUE -- {un,}quarantine matching items in the queue
**
**	Read all matching queue items, add/remove quarantine
**	reason, and requeue appropriately.
**
**	Parameters:
**		reason -- quarantine reason, "." means unquarantine.
**		qgrplimit -- limit to single queue group unless NOQGRP
**
**	Results:
**		none.
**
**	Side Effects:
**		Lots of changes to the queue.
*/

void
quarantine_queue(reason, qgrplimit)
	char *reason;
	int qgrplimit;
{
	int changed = 0;
	int qgrp;

	/* Convert internal representation of unquarantine */
	if (reason != NULL && reason[0] == '.' && reason[1] == '\0')
		reason = NULL;

	if (reason != NULL)
	{
		/* clean it */
		reason = newstr(denlstring(reason, true, true));
	}

	for (qgrp = 0; qgrp < NumQueue && Queue[qgrp] != NULL; qgrp++)
	{
		int qdir;

		if (qgrplimit != NOQGRP && qgrplimit != qgrp)
			continue;

		for (qdir = 0; qdir < Queue[qgrp]->qg_numqueues; qdir++)
		{
			int i;
			int nrequests;

			if (StopRequest)
				stop_sendmail();

			nrequests = gatherq(qgrp, qdir, true, NULL, NULL, NULL);

			/* first see if there is anything */
			if (nrequests <= 0)
			{
				if (Verbose)
				{
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT, "%s: no matches\n",
							     qid_printqueue(qgrp, qdir));
				}
				continue;
			}

			if (Verbose)
			{
				(void) sm_io_fprintf(smioout,
						     SM_TIME_DEFAULT, "Processing %s:\n",
						     qid_printqueue(qgrp, qdir));
			}

			for (i = 0; i < WorkListCount; i++)
			{
				ENVELOPE e;

				if (StopRequest)
					stop_sendmail();

				/* setup envelope */
				clearenvelope(&e, true, sm_rpool_new_x(NULL));
				e.e_id = WorkList[i].w_name + 2;
				e.e_qgrp = qgrp;
				e.e_qdir = qdir;

				if (tTd(70, 101))
				{
					sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						      "Would do %s\n", e.e_id);
					changed++;
				}
				else if (quarantine_queue_item(qgrp, qdir,
							       &e, reason))
					changed++;

				/* clean up */
				sm_rpool_free(e.e_rpool);
				e.e_rpool = NULL;
			}
			if (WorkList != NULL)
				sm_free(WorkList); /* XXX */
			WorkList = NULL;
			WorkListSize = 0;
			WorkListCount = 0;
		}
	}
	if (Verbose)
	{
		if (changed == 0)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "No changes\n");
		else
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%d change%s\n",
					     changed,
					     changed == 1 ? "" : "s");
	}
}
