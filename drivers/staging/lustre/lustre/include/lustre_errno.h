/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * GPL HEADER END
 */
/*
 * Copyright (C) 2011 FUJITSU LIMITED.  All rights reserved.
 *
 * Copyright (c) 2013, Intel Corporation.
 */

#ifndef LUSTRE_ERRNO_H
#define LUSTRE_ERRNO_H

/*
 * Only "network" errnos, which are defined below, are allowed on wire (or on
 * disk).  Generic routines exist to help translate between these and a subset
 * of the "host" errnos.  Some host errnos (e.g., EDEADLOCK) are intentionally
 * left out.  See also the comment on lustre_errno_hton_mapping[].
 *
 * To maintain compatibility with existing x86 clients and servers, each of
 * these network errnos has the same numerical value as its corresponding host
 * errno on x86.
 */
#define LUSTRE_EPERM		1	/* Operation not permitted */
#define LUSTRE_ENOENT		2	/* No such file or directory */
#define LUSTRE_ESRCH		3	/* No such process */
#define LUSTRE_EINTR		4	/* Interrupted system call */
#define LUSTRE_EIO		5	/* I/O error */
#define LUSTRE_ENXIO		6	/* No such device or address */
#define LUSTRE_E2BIG		7	/* Argument list too long */
#define LUSTRE_ENOEXEC		8	/* Exec format error */
#define LUSTRE_EBADF		9	/* Bad file number */
#define LUSTRE_ECHILD		10	/* No child processes */
#define LUSTRE_EAGAIN		11	/* Try again */
#define LUSTRE_ENOMEM		12	/* Out of memory */
#define LUSTRE_EACCES		13	/* Permission denied */
#define LUSTRE_EFAULT		14	/* Bad address */
#define LUSTRE_ENOTBLK		15	/* Block device required */
#define LUSTRE_EBUSY		16	/* Device or resource busy */
#define LUSTRE_EEXIST		17	/* File exists */
#define LUSTRE_EXDEV		18	/* Cross-device link */
#define LUSTRE_ENODEV		19	/* No such device */
#define LUSTRE_ENOTDIR		20	/* Not a directory */
#define LUSTRE_EISDIR		21	/* Is a directory */
#define LUSTRE_EINVAL		22	/* Invalid argument */
#define LUSTRE_ENFILE		23	/* File table overflow */
#define LUSTRE_EMFILE		24	/* Too many open files */
#define LUSTRE_ENOTTY		25	/* Not a typewriter */
#define LUSTRE_ETXTBSY		26	/* Text file busy */
#define LUSTRE_EFBIG		27	/* File too large */
#define LUSTRE_ENOSPC		28	/* No space left on device */
#define LUSTRE_ESPIPE		29	/* Illegal seek */
#define LUSTRE_EROFS		30	/* Read-only file system */
#define LUSTRE_EMLINK		31	/* Too many links */
#define LUSTRE_EPIPE		32	/* Broken pipe */
#define LUSTRE_EDOM		33	/* Math argument out of func domain */
#define LUSTRE_ERANGE		34	/* Math result not representable */
#define LUSTRE_EDEADLK		35	/* Resource deadlock would occur */
#define LUSTRE_ENAMETOOLONG	36	/* File name too long */
#define LUSTRE_ENOLCK		37	/* No record locks available */
#define LUSTRE_ENOSYS		38	/* Function not implemented */
#define LUSTRE_ENOTEMPTY	39	/* Directory not empty */
#define LUSTRE_ELOOP		40	/* Too many symbolic links found */
#define LUSTRE_ENOMSG		42	/* No message of desired type */
#define LUSTRE_EIDRM		43	/* Identifier removed */
#define LUSTRE_ECHRNG		44	/* Channel number out of range */
#define LUSTRE_EL2NSYNC		45	/* Level 2 not synchronized */
#define LUSTRE_EL3HLT		46	/* Level 3 halted */
#define LUSTRE_EL3RST		47	/* Level 3 reset */
#define LUSTRE_ELNRNG		48	/* Link number out of range */
#define LUSTRE_EUNATCH		49	/* Protocol driver not attached */
#define LUSTRE_ENOCSI		50	/* No CSI structure available */
#define LUSTRE_EL2HLT		51	/* Level 2 halted */
#define LUSTRE_EBADE		52	/* Invalid exchange */
#define LUSTRE_EBADR		53	/* Invalid request descriptor */
#define LUSTRE_EXFULL		54	/* Exchange full */
#define LUSTRE_ENOANO		55	/* No anode */
#define LUSTRE_EBADRQC		56	/* Invalid request code */
#define LUSTRE_EBADSLT		57	/* Invalid slot */
#define LUSTRE_EBFONT		59	/* Bad font file format */
#define LUSTRE_ENOSTR		60	/* Device not a stream */
#define LUSTRE_ENODATA		61	/* No data available */
#define LUSTRE_ETIME		62	/* Timer expired */
#define LUSTRE_ENOSR		63	/* Out of streams resources */
#define LUSTRE_ENONET		64	/* Machine is not on the network */
#define LUSTRE_ENOPKG		65	/* Package not installed */
#define LUSTRE_EREMOTE		66	/* Object is remote */
#define LUSTRE_ENOLINK		67	/* Link has been severed */
#define LUSTRE_EADV		68	/* Advertise error */
#define LUSTRE_ESRMNT		69	/* Srmount error */
#define LUSTRE_ECOMM		70	/* Communication error on send */
#define LUSTRE_EPROTO		71	/* Protocol error */
#define LUSTRE_EMULTIHOP	72	/* Multihop attempted */
#define LUSTRE_EDOTDOT		73	/* RFS specific error */
#define LUSTRE_EBADMSG		74	/* Not a data message */
#define LUSTRE_EOVERFLOW	75	/* Value too large for data type */
#define LUSTRE_ENOTUNIQ		76	/* Name not unique on network */
#define LUSTRE_EBADFD		77	/* File descriptor in bad state */
#define LUSTRE_EREMCHG		78	/* Remote address changed */
#define LUSTRE_ELIBACC		79	/* Can't access needed shared library */
#define LUSTRE_ELIBBAD		80	/* Access corrupted shared library */
#define LUSTRE_ELIBSCN		81	/* .lib section in a.out corrupted */
#define LUSTRE_ELIBMAX		82	/* Trying to link too many libraries */
#define LUSTRE_ELIBEXEC		83	/* Cannot exec a shared lib directly */
#define LUSTRE_EILSEQ		84	/* Illegal byte sequence */
#define LUSTRE_ERESTART		85	/* Restart interrupted system call */
#define LUSTRE_ESTRPIPE		86	/* Streams pipe error */
#define LUSTRE_EUSERS		87	/* Too many users */
#define LUSTRE_ENOTSOCK		88	/* Socket operation on non-socket */
#define LUSTRE_EDESTADDRREQ	89	/* Destination address required */
#define LUSTRE_EMSGSIZE		90	/* Message too long */
#define LUSTRE_EPROTOTYPE	91	/* Protocol wrong type for socket */
#define LUSTRE_ENOPROTOOPT	92	/* Protocol not available */
#define LUSTRE_EPROTONOSUPPORT	93	/* Protocol not supported */
#define LUSTRE_ESOCKTNOSUPPORT	94	/* Socket type not supported */
#define LUSTRE_EOPNOTSUPP	95	/* Operation not supported */
#define LUSTRE_EPFNOSUPPORT	96	/* Protocol family not supported */
#define LUSTRE_EAFNOSUPPORT	97	/* Address family not supported */
#define LUSTRE_EADDRINUSE	98	/* Address already in use */
#define LUSTRE_EADDRNOTAVAIL	99	/* Cannot assign requested address */
#define LUSTRE_ENETDOWN		100	/* Network is down */
#define LUSTRE_ENETUNREACH	101	/* Network is unreachable */
#define LUSTRE_ENETRESET	102	/* Network connection drop for reset */
#define LUSTRE_ECONNABORTED	103	/* Software caused connection abort */
#define LUSTRE_ECONNRESET	104	/* Connection reset by peer */
#define LUSTRE_ENOBUFS		105	/* No buffer space available */
#define LUSTRE_EISCONN		106	/* Transport endpoint is connected */
#define LUSTRE_ENOTCONN		107	/* Transport endpoint not connected */
#define LUSTRE_ESHUTDOWN	108	/* Cannot send after shutdown */
#define LUSTRE_ETOOMANYREFS	109	/* Too many references: cannot splice */
#define LUSTRE_ETIMEDOUT	110	/* Connection timed out */
#define LUSTRE_ECONNREFUSED	111	/* Connection refused */
#define LUSTRE_EHOSTDOWN	112	/* Host is down */
#define LUSTRE_EHOSTUNREACH	113	/* No route to host */
#define LUSTRE_EALREADY		114	/* Operation already in progress */
#define LUSTRE_EINPROGRESS	115	/* Operation now in progress */
#define LUSTRE_ESTALE		116	/* Stale file handle */
#define LUSTRE_EUCLEAN		117	/* Structure needs cleaning */
#define LUSTRE_ENOTNAM		118	/* Not a XENIX named type file */
#define LUSTRE_ENAVAIL		119	/* No XENIX semaphores available */
#define LUSTRE_EISNAM		120	/* Is a named type file */
#define LUSTRE_EREMOTEIO	121	/* Remote I/O error */
#define LUSTRE_EDQUOT		122	/* Quota exceeded */
#define LUSTRE_ENOMEDIUM	123	/* No medium found */
#define LUSTRE_EMEDIUMTYPE	124	/* Wrong medium type */
#define LUSTRE_ECANCELED	125	/* Operation Canceled */
#define LUSTRE_ENOKEY		126	/* Required key not available */
#define LUSTRE_EKEYEXPIRED	127	/* Key has expired */
#define LUSTRE_EKEYREVOKED	128	/* Key has been revoked */
#define LUSTRE_EKEYREJECTED	129	/* Key was rejected by service */
#define LUSTRE_EOWNERDEAD	130	/* Owner died */
#define LUSTRE_ENOTRECOVERABLE	131	/* State not recoverable */
#define LUSTRE_ERESTARTSYS	512
#define LUSTRE_ERESTARTNOINTR	513
#define LUSTRE_ERESTARTNOHAND	514	/* restart if no handler.. */
#define LUSTRE_ENOIOCTLCMD	515	/* No ioctl command */
#define LUSTRE_ERESTART_RESTARTBLOCK 516 /* restart via sys_restart_syscall */
#define LUSTRE_EBADHANDLE	521	/* Illegal NFS file handle */
#define LUSTRE_ENOTSYNC		522	/* Update synchronization mismatch */
#define LUSTRE_EBADCOOKIE	523	/* Cookie is stale */
#define LUSTRE_ENOTSUPP		524	/* Operation is not supported */
#define LUSTRE_ETOOSMALL	525	/* Buffer or request is too small */
#define LUSTRE_ESERVERFAULT	526	/* An untranslatable error occurred */
#define LUSTRE_EBADTYPE		527	/* Type not supported by server */
#define LUSTRE_EJUKEBOX		528	/* Request won't finish until timeout */
#define LUSTRE_EIOCBQUEUED	529	/* iocb queued await completion event */
#define LUSTRE_EIOCBRETRY	530	/* iocb queued, will trigger a retry */

/*
 * Translations are optimized away on x86.  Host errnos that shouldn't be put
 * on wire could leak through as a result.  Do not count on this side effect.
 */
#ifdef CONFIG_LUSTRE_TRANSLATE_ERRNOS
unsigned int lustre_errno_hton(unsigned int h);
unsigned int lustre_errno_ntoh(unsigned int n);
#else
#define lustre_errno_hton(h) (h)
#define lustre_errno_ntoh(n) (n)
#endif

#endif /* LUSTRE_ERRNO_H */
