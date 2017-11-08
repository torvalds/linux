/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _S390_MSGBUF_H
#define _S390_MSGBUF_H

/* 
 * The msqid64_ds structure for S/390 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct msqid64_ds {
	struct ipc64_perm msg_perm;
	__kernel_time_t msg_stime;	/* last msgsnd time */
#ifndef __s390x__
	unsigned long	__unused1;
#endif /* ! __s390x__ */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
#ifndef __s390x__
	unsigned long	__unused2;
#endif /* ! __s390x__ */
	__kernel_time_t msg_ctime;	/* last change time */
#ifndef __s390x__
	unsigned long	__unused3;
#endif /* ! __s390x__ */
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#endif /* _S390_MSGBUF_H */
