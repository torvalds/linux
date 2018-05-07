/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_MSGBUF_H
#define _ASM_MSGBUF_H


/*
 * The msqid64_ds structure for the MIPS architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 2 miscellaneous unsigned long values
 */

#if defined(__mips64)
struct msqid64_ds {
	struct ipc64_perm msg_perm;
	__kernel_time_t msg_stime;	/* last msgsnd time */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	__kernel_time_t msg_ctime;	/* last change time */
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};
#elif defined (__MIPSEB__)
struct msqid64_ds {
	struct ipc64_perm msg_perm;
	unsigned long  msg_stime_high;
	unsigned long  msg_stime;	/* last msgsnd time */
	unsigned long  msg_rtime_high;
	unsigned long  msg_rtime;	/* last msgrcv time */
	unsigned long  msg_ctime_high;
	unsigned long  msg_ctime;	/* last change time */
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};
#elif defined (__MIPSEL__)
struct msqid64_ds {
	struct ipc64_perm msg_perm;
	unsigned long  msg_stime;	/* last msgsnd time */
	unsigned long  msg_stime_high;
	unsigned long  msg_rtime;	/* last msgrcv time */
	unsigned long  msg_rtime_high;
	unsigned long  msg_ctime;	/* last change time */
	unsigned long  msg_ctime_high;
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};
#else
#warning no endianess set
#endif

#endif /* _ASM_MSGBUF_H */
