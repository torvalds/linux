/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_MSG_H
#define _LINUX_MSG_H

#include <linux/ipc.h>

/* ipcs ctl commands */
#define MSG_STAT 11
#define MSG_INFO 12
#define MSG_STAT_ANY 13

/* msgrcv options */
#define MSG_NOERROR     010000  /* no error if message is too big */
#define MSG_EXCEPT      020000  /* recv any msg except of specified type.*/
#define MSG_COPY        040000  /* copy (not remove) all queue messages */

/* Obsolete, used only for backwards compatibility and libc5 compiles */
struct msqid_ds {
	struct ipc_perm msg_perm;
	struct msg *msg_first;		/* first message on queue,unused  */
	struct msg *msg_last;		/* last message in queue,unused */
	__kernel_old_time_t msg_stime;	/* last msgsnd time */
	__kernel_old_time_t msg_rtime;	/* last msgrcv time */
	__kernel_old_time_t msg_ctime;	/* last change time */
	unsigned long  msg_lcbytes;	/* Reuse junk fields for 32 bit */
	unsigned long  msg_lqbytes;	/* ditto */
	unsigned short msg_cbytes;	/* current number of bytes on queue */
	unsigned short msg_qnum;	/* number of messages in queue */
	unsigned short msg_qbytes;	/* max number of bytes on queue */
	__kernel_ipc_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_ipc_pid_t msg_lrpid;	/* last receive pid */
};

/* Include the definition of msqid64_ds */
#include <asm/msgbuf.h>

/* message buffer for msgsnd and msgrcv calls */
struct msgbuf {
	__kernel_long_t mtype;          /* type of message */
	char mtext[1];                  /* message text */
};

/* buffer for msgctl calls IPC_INFO, MSG_INFO */
struct msginfo {
	int msgpool;
	int msgmap; 
	int msgmax; 
	int msgmnb; 
	int msgmni; 
	int msgssz; 
	int msgtql; 
	unsigned short  msgseg; 
};

/*
 * MSGMNI, MSGMAX and MSGMNB are default values which can be
 * modified by sysctl.
 *
 * MSGMNI is the upper limit for the number of messages queues per
 * namespace.
 * It has been chosen to be as large possible without facilitating
 * scenarios where userspace causes overflows when adjusting the limits via
 * operations of the form retrieve current limit; add X; update limit".
 *
 * MSGMNB is the default size of a new message queue. Non-root tasks can
 * decrease the size with msgctl(IPC_SET), root tasks
 * (actually: CAP_SYS_RESOURCE) can both increase and decrease the queue
 * size. The optimal value is application dependent.
 * 16384 is used because it was always used (since 0.99.10)
 *
 * MAXMAX is the maximum size of an individual message, it's a global
 * (per-namespace) limit that applies for all message queues.
 * It's set to 1/2 of MSGMNB, to ensure that at least two messages fit into
 * the queue. This is also an arbitrary choice (since 2.6.0).
 */

#define MSGMNI 32000   /* <= IPCMNI */     /* max # of msg queue identifiers */
#define MSGMAX  8192   /* <= INT_MAX */   /* max size of message (bytes) */
#define MSGMNB 16384   /* <= INT_MAX */   /* default max size of a message queue */

/* unused */
#define MSGPOOL (MSGMNI * MSGMNB / 1024) /* size in kbytes of message pool */
#define MSGTQL  MSGMNB            /* number of system message headers */
#define MSGMAP  MSGMNB            /* number of entries in message map */
#define MSGSSZ  16                /* message segment size */
#define __MSGSEG ((MSGPOOL * 1024) / MSGSSZ) /* max no. of segments */
#define MSGSEG (__MSGSEG <= 0xffff ? __MSGSEG : 0xffff)


#endif /* _LINUX_MSG_H */
