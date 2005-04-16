/* $Id: systbl.S,v 1.11 2000/03/13 21:57:35 davem Exp $
 * systbl.S: System call entry point table for Solaris compatibility.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <asm/unistd.h>

/* Fall back to sys_call_table32 entry */
#define CHAIN(name)	__NR_##name

/* Pass pt_regs pointer as first argument */
#define REGS(name)	name+1

/* Hack till all be implemented */
#define solaris_getpmsg		solaris_unimplemented
#define solaris_hrtsys		solaris_unimplemented
#define solaris_msgsys		solaris_unimplemented
#define solaris_putpmsg		solaris_unimplemented
#define solaris_semsys		solaris_unimplemented

        .data
	.globl		solaris_sys_table
solaris_sys_table:
	.word solaris_unimplemented	/* nosys		0	*/
	.word CHAIN(exit)		/* exit		d	1	*/
	.word CHAIN(fork)		/* fork			2	*/
	.word CHAIN(read)		/* read		dpd	3	*/
	.word CHAIN(write)		/* write	dpd	4	*/
	.word solaris_open		/* open		soo	5	*/
	.word CHAIN(close)		/* close	d	6	*/
	.word solaris_wait		/* wait		xxx	7	*/
	.word CHAIN(creat)		/* creat	so	8	*/
	.word CHAIN(link)		/* link		ss	9	*/
	.word CHAIN(unlink)		/* unlink	s	10	*/
	.word solaris_unimplemented	/* exec		sxx	11	*/
	.word CHAIN(chdir)		/* chdir	s	12	*/
	.word CHAIN(time)		/* time			13	*/
	.word solaris_mknod		/* mknod	sox	14	*/
	.word CHAIN(chmod)		/* chmod	so	15	*/ 
	.word CHAIN(chown)		/* chown	sdd	16	*/
	.word solaris_brk		/* brk/break	x	17	*/
	.word solaris_stat		/* stat		sp	18	*/
	.word CHAIN(lseek)		/* seek/lseek	ddd	19	*/
	.word solaris_getpid		/* getpid		20	*/
	.word solaris_unimplemented	/* mount		21	*/
	.word CHAIN(umount)		/* umount	s	22	*/
	.word CHAIN(setuid)		/* setuid	d	23	*/
	.word solaris_getuid		/* getuid		24	*/
	.word CHAIN(stime)		/* stime	d	25	*/
#if 0
	.word solaris_ptrace		/* ptrace	xdxx	26	*/
#else
	.word CHAIN(ptrace)		/* ptrace	xdxx	26	*/
#endif
	.word CHAIN(alarm)		/* alarm	d	27	*/
	.word solaris_fstat		/* fstat	dp	28	*/
	.word CHAIN(pause)		/* pause		29	*/
	.word CHAIN(utime)		/* utime	xx	30	*/
	.word solaris_unimplemented	/* stty			31	*/
	.word solaris_unimplemented	/* gtty			32	*/
	.word solaris_access		/* access	so	33	*/
	.word CHAIN(nice)		/* nice		d	34	*/
	.word solaris_statfs		/* statfs	spdd	35	*/
	.word CHAIN(sync)		/* sync			36	*/
	.word solaris_kill		/* kill		dd	37	*/
	.word solaris_fstatfs		/* fstatfs	dpdd	38	*/
	.word solaris_procids		/* pgrpsys	ddd	39	*/
	.word solaris_unimplemented	/* xenix		40	*/
	.word CHAIN(dup)		/* dup		d	41	*/
	.word CHAIN(pipe)		/* pipe			42	*/
	.word CHAIN(times)		/* times	p	43	*/
	.word 44 /*CHAIN(profil)*/	/* prof		xxxx	44	*/
	.word solaris_unimplemented	/* lock/plock		45	*/
	.word CHAIN(setgid)		/* setgid	d	46	*/
	.word solaris_getgid		/* getgid		47	*/
	.word solaris_sigfunc		/* sigfunc	xx	48	*/
	.word REGS(solaris_msgsys)	/* msgsys	dxddd	49	*/
	.word solaris_unimplemented	/* syssun/3b		50	*/
	.word CHAIN(acct)		/* acct/sysacct	x	51	*/
	.word solaris_shmsys		/* shmsys	ddxo	52	*/
	.word REGS(solaris_semsys)	/* semsys	dddx	53	*/
	.word solaris_ioctl		/* ioctl	dxx	54	*/
	.word solaris_unimplemented	/* uadmin	xxx	55	*/
	.word solaris_unimplemented	/* reserved:exch	56	*/
	.word solaris_utssys		/* utssys	x	57	*/
	.word CHAIN(fsync)		/* fsync	d	58	*/
	.word CHAIN(execve)		/* execv	spp	59	*/
	.word CHAIN(umask)		/* umask	o	60	*/
	.word CHAIN(chroot)		/* chroot	s	61	*/
	.word solaris_fcntl		/* fcntl	dxx	62	*/
	.word solaris_ulimit		/* ulimit	xx	63	*/
	.word solaris_unimplemented	/* ?			64	*/
	.word solaris_unimplemented	/* ?			65	*/
	.word solaris_unimplemented	/* ?			66	*/
	.word solaris_unimplemented	/* ?			67	*/
	.word solaris_unimplemented	/* ?			68	*/
	.word solaris_unimplemented	/* ?			69	*/
	.word solaris_unimplemented	/* advfs		70	*/
	.word solaris_unimplemented	/* unadvfs		71	*/
	.word solaris_unimplemented	/* rmount		72	*/
	.word solaris_unimplemented	/* rumount		73	*/
	.word solaris_unimplemented	/* rfstart		74	*/
	.word solaris_unimplemented	/* ?			75	*/
	.word solaris_unimplemented	/* rdebug		76	*/
	.word solaris_unimplemented	/* rfstop		77	*/
	.word solaris_unimplemented	/* rfsys		78	*/
	.word CHAIN(rmdir)		/* rmdir	s	79	*/
	.word CHAIN(mkdir)		/* mkdir	so	80	*/
	.word CHAIN(getdents)		/* getdents	dxd	81	*/
	.word solaris_unimplemented	/* libattach		82	*/
	.word solaris_unimplemented	/* libdetach		83	*/
	.word CHAIN(sysfs)		/* sysfs	dxx	84	*/
	.word solaris_getmsg		/* getmsg	dxxx	85	*/
	.word solaris_putmsg		/* putmsg	dxxd	86	*/
	.word CHAIN(poll)		/* poll		xdd	87	*/
	.word solaris_lstat		/* lstat	sp	88	*/
	.word CHAIN(symlink)		/* symlink	ss	89	*/
	.word CHAIN(readlink)		/* readlink	spd	90	*/
	.word CHAIN(setgroups)		/* setgroups	dp	91	*/
	.word CHAIN(getgroups)		/* getgroups	dp	92	*/
	.word CHAIN(fchmod)		/* fchmod	do	93	*/
	.word CHAIN(fchown)		/* fchown	ddd	94	*/
	.word solaris_sigprocmask	/* sigprocmask	dxx	95	*/
	.word solaris_sigsuspend	/* sigsuspend	x	96	*/
	.word solaris_sigaltstack	/* sigaltstack	xx	97	*/
	.word solaris_sigaction		/* sigaction	dxx	98	*/
	.word solaris_sigpending	/* sigpending	dd	99	*/
	.word REGS(solaris_context)	/* context		100	*/
	.word solaris_unimplemented	/* evsys		101	*/
	.word solaris_unimplemented	/* evtrapret		102	*/
	.word solaris_statvfs		/* statvfs	sp	103	*/
	.word solaris_fstatvfs		/* fstatvfs	dp	104	*/
	.word solaris_unimplemented	/* unknown		105	*/
	.word solaris_unimplemented	/* nfssys		106	*/
	.word solaris_waitid		/* waitid	ddxd	107	*/
	.word solaris_unimplemented	/* sigsendsys	ddd	108	*/
	.word REGS(solaris_hrtsys)	/* hrtsys	xxx	109	*/
	.word solaris_unimplemented	/* acancel	dxd	110	*/
	.word solaris_unimplemented	/* async		111	*/
	.word solaris_unimplemented	/* priocntlsys		112	*/
	.word solaris_pathconf		/* pathconf	sd	113	*/
	.word CHAIN(mincore)		/* mincore	d	114	*/
	.word solaris_mmap		/* mmap		xxxxdx	115	*/
	.word CHAIN(mprotect)		/* mprotect	xdx	116	*/
	.word CHAIN(munmap)		/* munmap	xd	117	*/
	.word solaris_fpathconf		/* fpathconf	dd	118	*/
	.word CHAIN(fork)		/* fork			119	*/
	.word solaris_unimplemented	/* fchdir	d	120	*/
	.word CHAIN(readv)		/* readv	dxd	121	*/
	.word CHAIN(writev)		/* writev	dxd	122	*/
	.word solaris_xstat		/* xstat	dsx	123	*/
	.word solaris_lxstat		/* lxstat	dsx	124	*/
	.word solaris_fxstat		/* fxstat	ddx	125	*/
	.word solaris_xmknod		/* xmknod	dsox	126	*/
	.word solaris_unimplemented	/* syslocal	d	127	*/
	.word solaris_setrlimit		/* setrlimit	dp	128	*/
	.word solaris_getrlimit		/* getrlimit	dp	129	*/
	.word CHAIN(chown)		/* lchown	sdd	130	*/
	.word solaris_unimplemented	/* memcntl		131	*/
	.word solaris_getpmsg		/* getpmsg	dxxxx	132	*/
	.word solaris_putpmsg		/* putpmsg	dxxdd	133	*/
	.word CHAIN(rename)		/* rename	ss	134	*/
	.word solaris_utsname		/* uname	x	135	*/
	.word solaris_unimplemented	/* setegid		136	*/
	.word solaris_sysconf		/* sysconfig	d	137	*/
	.word solaris_unimplemented	/* adjtime		138	*/
	.word solaris_sysinfo		/* systeminfo	dsd	139	*/
	.word solaris_unimplemented	/* ?			140	*/
	.word solaris_unimplemented	/* seteuid		141	*/
	.word solaris_unimplemented	/* ?			142	*/
	.word solaris_unimplemented	/* ?			143	*/
	.word solaris_unimplemented	/* secsys	dx	144	*/
	.word solaris_unimplemented	/* filepriv	sdxd	145	*/
	.word solaris_unimplemented	/* procpriv	dxd	146	*/
	.word solaris_unimplemented	/* devstat	sdx	147	*/
	.word solaris_unimplemented	/* aclipc	ddddx	148	*/
	.word solaris_unimplemented	/* fdevstat	ddx	149	*/
	.word solaris_unimplemented	/* flvlfile	ddx	150	*/
	.word solaris_unimplemented	/* lvlfile	sdx	151	*/
	.word solaris_unimplemented	/* ?			152	*/
	.word solaris_unimplemented	/* fchroot	d	153	*/
	.word solaris_unimplemented	/* lvlproc	dx	154	*/
	.word solaris_unimplemented	/* ?			155	*/
	.word solaris_gettimeofday	/* gettimeofday	x	156	*/
	.word CHAIN(getitimer)		/* getitimer	dx	157	*/
	.word CHAIN(setitimer)		/* setitimer	dxx	158	*/
	.word solaris_unimplemented	/* lwp-xxx		159	*/
	.word solaris_unimplemented	/* lwp-xxx		160	*/
	.word solaris_unimplemented	/* lwp-xxx		161	*/
	.word solaris_unimplemented	/* lwp-xxx		162	*/
	.word solaris_unimplemented	/* lwp-xxx		163	*/
	.word solaris_unimplemented	/* lwp-xxx		164	*/
	.word solaris_unimplemented	/* lwp-xxx		165	*/
	.word solaris_unimplemented	/* lwp-xxx		166	*/
	.word solaris_unimplemented	/* lwp-xxx		167	*/
	.word solaris_unimplemented	/* lwp-xxx		168	*/
	.word solaris_unimplemented	/* lwp-xxx		169	*/
	.word solaris_unimplemented	/* lwp-xxx		170	*/
	.word solaris_unimplemented	/* lwp-xxx		171	*/
	.word solaris_unimplemented	/* lwp-xxx		172	*/
	.word solaris_pread		/* pread	dpdd	173	*/
	.word solaris_pwrite		/* pwrite	dpdd	174	*/
	.word REGS(solaris_llseek)	/* llseek	dLd	175	*/
	.word solaris_unimplemented	/* lwpself		176	*/
	.word solaris_unimplemented	/* lwpinfo		177	*/
	.word solaris_unimplemented	/* lwpprivate		178	*/
	.word solaris_unimplemented	/* processorbind	179	*/
	.word solaris_unimplemented	/* processorexbind	180	*/
	.word solaris_unimplemented	/* 			181	*/
	.word solaris_unimplemented	/* sync_mailbox		182	*/
	.word solaris_unimplemented	/* prepblock		183	*/
	.word solaris_unimplemented	/* block		184	*/
	.word solaris_acl		/* acl		sddp	185	*/
	.word solaris_unimplemented	/* unblock		186	*/
	.word solaris_unimplemented	/* cancelblock		187	*/
	.word solaris_unimplemented	/* ?			188	*/
	.word solaris_unimplemented	/* xxxxx		189	*/
	.word solaris_unimplemented	/* xxxxxe		190	*/
	.word solaris_unimplemented	/*			191	*/
	.word solaris_unimplemented	/*			192	*/
	.word solaris_unimplemented	/*			193	*/
	.word solaris_unimplemented	/*			194	*/
	.word solaris_unimplemented	/* 			195	*/
	.word solaris_unimplemented	/* 			196	*/
	.word solaris_unimplemented	/* 			197	*/
	.word solaris_unimplemented	/* 			198	*/
	.word CHAIN(nanosleep)		/* nanosleep	dd	199	*/
	.word solaris_facl		/* facl		dddp	200	*/
	.word solaris_unimplemented	/* 			201	*/
	.word CHAIN(setreuid)		/* setreuid	dd	202	*/
	.word CHAIN(setregid)		/* setregid	dd	203	*/
	.word solaris_unimplemented	/* 			204	*/
	.word solaris_unimplemented	/* 			205	*/
	.word solaris_unimplemented	/* 			206	*/
	.word solaris_unimplemented	/* 			207	*/
	.word solaris_unimplemented	/* 			208	*/
	.word solaris_unimplemented	/* 			209	*/
	.word solaris_unimplemented	/* 			210	*/
	.word solaris_unimplemented	/* 			211	*/
	.word solaris_unimplemented	/* 			212	*/
	.word solaris_getdents64	/* getdents64	dpd	213	*/
	.word REGS(solaris_mmap64)	/* mmap64	xxxxdX	214	*/
	.word solaris_stat64		/* stat64	sP	215	*/
	.word solaris_lstat64		/* lstat64	sP	216	*/
	.word solaris_fstat64		/* fstat64	dP	217	*/
	.word solaris_statvfs64		/* statvfs64	sP	218	*/
	.word solaris_fstatvfs64	/* fstatvfs64	dP	219	*/
	.word solaris_setrlimit64	/* setrlimit64	dP	220	*/
	.word solaris_getrlimit64	/* getrlimit64	dP	221	*/
	.word CHAIN(pread64)		/* pread64	dpdD	222	*/
	.word CHAIN(pwrite64)		/* pwrite64	dpdD	223	*/
	.word CHAIN(creat)		/* creat64	so	224	*/
	.word solaris_open		/* open64	soo	225	*/
	.word solaris_unimplemented	/* 			226	*/
	.word solaris_unimplemented	/* 			227	*/
	.word solaris_unimplemented	/* 			228	*/
	.word solaris_unimplemented	/* 			229	*/
	.word solaris_socket		/* socket	ddd	230	*/
	.word solaris_socketpair	/* socketpair	dddp	231	*/
	.word solaris_bind		/* bind		dpd	232	*/
	.word solaris_listen		/* listen	dd	233	*/
	.word solaris_accept		/* accept	dpp	234	*/
	.word solaris_connect		/* connect	dpd	235	*/
	.word solaris_shutdown		/* shutdown	dd	236	*/
	.word solaris_recv		/* recv		dpdd	237	*/
	.word solaris_recvfrom		/* recvfrom	dpddpp	238	*/
	.word solaris_recvmsg		/* recvmsg	dpd	239	*/
	.word solaris_send		/* send		dpdd	240	*/
	.word solaris_sendmsg		/* sendmsg	dpd	241	*/
	.word solaris_sendto		/* sendto	dpddpd	242	*/
	.word solaris_getpeername	/* getpeername	dpp	243	*/
	.word solaris_getsockname	/* getsockname	dpp	244	*/
	.word solaris_getsockopt	/* getsockopt	dddpp	245	*/
	.word solaris_setsockopt	/* setsockopt	dddpp	246	*/
	.word solaris_unimplemented	/* 			247	*/
	.word solaris_ntp_gettime	/* ntp_gettime	p	248	*/
	.word solaris_ntp_adjtime	/* ntp_adjtime	p	249	*/
	.word solaris_unimplemented	/* 			250	*/
	.word solaris_unimplemented	/* 			251	*/
	.word solaris_unimplemented	/* 			252	*/
	.word solaris_unimplemented	/* 			253	*/
	.word solaris_unimplemented	/* 			254	*/
	.word solaris_unimplemented	/* 			255	*/
	.word solaris_unimplemented	/* 			256	*/
	.word solaris_unimplemented	/* 			257	*/
	.word solaris_unimplemented	/* 			258	*/
	.word solaris_unimplemented	/* 			259	*/
	.word solaris_unimplemented	/* 			260	*/
	.word solaris_unimplemented	/* 			261	*/
	.word solaris_unimplemented	/* 			262	*/
	.word solaris_unimplemented	/* 			263	*/
	.word solaris_unimplemented	/* 			264	*/
	.word solaris_unimplemented	/* 			265	*/
	.word solaris_unimplemented	/* 			266	*/
	.word solaris_unimplemented	/* 			267	*/
	.word solaris_unimplemented	/* 			268	*/
	.word solaris_unimplemented	/* 			269	*/
	.word solaris_unimplemented	/* 			270	*/
	.word solaris_unimplemented	/* 			271	*/
	.word solaris_unimplemented	/* 			272	*/
	.word solaris_unimplemented	/* 			273	*/
	.word solaris_unimplemented	/* 			274	*/
	.word solaris_unimplemented	/* 			275	*/
	.word solaris_unimplemented	/* 			276	*/
	.word solaris_unimplemented	/* 			277	*/
	.word solaris_unimplemented	/* 			278	*/
	.word solaris_unimplemented	/* 			279	*/
	.word solaris_unimplemented	/* 			280	*/
	.word solaris_unimplemented	/* 			281	*/
	.word solaris_unimplemented	/* 			282	*/
	.word solaris_unimplemented	/* 			283	*/

