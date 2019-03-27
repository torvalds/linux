/*-
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef _BSM_AUDIT_ERRNO_H_
#define	_BSM_AUDIT_ERRNO_H_

/*
 * For the purposes of portable encoding, we convert between local error
 * numbers and Solaris error numbers (as well as some extensions for error
 * numbers that don't exist in Solaris).  Although the first 35 or so
 * constants are the same across all OS's, we don't handle that in any
 * special way.
 *
 * When adding constants here, also add them to bsm_errno.c.
 */
#define	BSM_ERRNO_ESUCCESS		0
#define	BSM_ERRNO_EPERM			1
#define	BSM_ERRNO_ENOENT		2
#define	BSM_ERRNO_ESRCH			3
#define	BSM_ERRNO_EINTR			4
#define	BSM_ERRNO_EIO			5
#define	BSM_ERRNO_ENXIO			6
#define	BSM_ERRNO_E2BIG			7
#define	BSM_ERRNO_ENOEXEC		8
#define	BSM_ERRNO_EBADF			9
#define	BSM_ERRNO_ECHILD		10
#define	BSM_ERRNO_EAGAIN		11
#define	BSM_ERRNO_ENOMEM		12
#define	BSM_ERRNO_EACCES		13
#define	BSM_ERRNO_EFAULT		14
#define	BSM_ERRNO_ENOTBLK		15
#define	BSM_ERRNO_EBUSY			16
#define	BSM_ERRNO_EEXIST		17
#define	BSM_ERRNO_EXDEV			18
#define	BSM_ERRNO_ENODEV		19
#define	BSM_ERRNO_ENOTDIR		20
#define	BSM_ERRNO_EISDIR		21
#define	BSM_ERRNO_EINVAL		22
#define	BSM_ERRNO_ENFILE		23
#define	BSM_ERRNO_EMFILE		24
#define	BSM_ERRNO_ENOTTY		25
#define	BSM_ERRNO_ETXTBSY		26
#define	BSM_ERRNO_EFBIG			27
#define	BSM_ERRNO_ENOSPC		28
#define	BSM_ERRNO_ESPIPE		29
#define	BSM_ERRNO_EROFS			30
#define	BSM_ERRNO_EMLINK		31
#define	BSM_ERRNO_EPIPE			32
#define	BSM_ERRNO_EDOM			33
#define	BSM_ERRNO_ERANGE		34
#define	BSM_ERRNO_ENOMSG		35
#define	BSM_ERRNO_EIDRM			36
#define	BSM_ERRNO_ECHRNG		37	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EL2NSYNC		38	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EL3HLT		39	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EL3RST		40	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ELNRNG		41	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EUNATCH		42	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ENOCSI		43	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EL2HLT		44	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EDEADLK		45
#define	BSM_ERRNO_ENOLCK		46
#define	BSM_ERRNO_ECANCELED		47
#define	BSM_ERRNO_ENOTSUP		48
#define	BSM_ERRNO_EDQUOT		49
#define	BSM_ERRNO_EBADE			50	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EBADR			51	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EXFULL		52	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ENOANO		53	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EBADRQC		54	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EBADSLT		55	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EDEADLOCK		56	/* Solaris-specific. */
#define	BSM_ERRNO_EBFONT		57	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EOWNERDEAD		58	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ENOTRECOVERABLE	59	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ENOSTR		60	/* Solaris/Darwin/Linux-specific. */
#define	BSM_ERRNO_ENODATA		61	/* Solaris/Darwin/Linux-specific. */
#define	BSM_ERRNO_ETIME			62	/* Solaris/Darwin/Linux-specific. */
#define	BSM_ERRNO_ENOSR			63	/* Solaris/Darwin/Linux-specific. */
#define	BSM_ERRNO_ENONET		64	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ENOPKG		65	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EREMOTE		66
#define	BSM_ERRNO_ENOLINK		67
#define	BSM_ERRNO_EADV			68	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ESRMNT		69	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ECOMM			70	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EPROTO		71
#define	BSM_ERRNO_ELOCKUNMAPPED		72	/* Solaris-specific. */
#define	BSM_ERRNO_ENOTACTIVE		73	/* Solaris-specific. */
#define	BSM_ERRNO_EMULTIHOP		74
#define	BSM_ERRNO_EBADMSG		77
#define	BSM_ERRNO_ENAMETOOLONG		78
#define	BSM_ERRNO_EOVERFLOW		79
#define	BSM_ERRNO_ENOTUNIQ		80	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EBADFD		81	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EREMCHG		82	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ELIBACC		83	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ELIBBAD		84	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ELIBSCN		85	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ELIBMAX		86	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ELIBEXEC		87	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_EILSEQ		88
#define	BSM_ERRNO_ENOSYS		89
#define	BSM_ERRNO_ELOOP			90
#define	BSM_ERRNO_ERESTART		91
#define	BSM_ERRNO_ESTRPIPE		92	/* Solaris/Linux-specific. */
#define	BSM_ERRNO_ENOTEMPTY		93
#define	BSM_ERRNO_EUSERS		94
#define	BSM_ERRNO_ENOTSOCK		95
#define	BSM_ERRNO_EDESTADDRREQ		96
#define	BSM_ERRNO_EMSGSIZE		97
#define	BSM_ERRNO_EPROTOTYPE		98
#define	BSM_ERRNO_ENOPROTOOPT		99
#define	BSM_ERRNO_EPROTONOSUPPORT	120
#define	BSM_ERRNO_ESOCKTNOSUPPORT	121
#define	BSM_ERRNO_EOPNOTSUPP		122
#define	BSM_ERRNO_EPFNOSUPPORT		123
#define	BSM_ERRNO_EAFNOSUPPORT		124
#define	BSM_ERRNO_EADDRINUSE		125
#define	BSM_ERRNO_EADDRNOTAVAIL		126
#define	BSM_ERRNO_ENETDOWN		127
#define	BSM_ERRNO_ENETUNREACH		128
#define	BSM_ERRNO_ENETRESET		129
#define	BSM_ERRNO_ECONNABORTED		130
#define	BSM_ERRNO_ECONNRESET		131
#define	BSM_ERRNO_ENOBUFS		132
#define	BSM_ERRNO_EISCONN		133
#define	BSM_ERRNO_ENOTCONN		134
#define	BSM_ERRNO_ESHUTDOWN		143
#define	BSM_ERRNO_ETOOMANYREFS		144
#define	BSM_ERRNO_ETIMEDOUT		145
#define	BSM_ERRNO_ECONNREFUSED		146
#define	BSM_ERRNO_EHOSTDOWN		147
#define	BSM_ERRNO_EHOSTUNREACH		148
#define	BSM_ERRNO_EALREADY		149
#define	BSM_ERRNO_EINPROGRESS		150
#define	BSM_ERRNO_ESTALE		151

/*
 * OpenBSM constants for error numbers not defined in Solaris.  In the event
 * that these errors are added to Solaris, we will deprecate the OpenBSM
 * numbers in the same way we do for audit event constants.
 *
 * ELAST doesn't get a constant in the BSM space.
 */
#define	BSM_ERRNO_EPROCLIM		190	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EBADRPC		191	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_ERPCMISMATCH		192	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EPROGUNAVAIL		193	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EPROGMISMATCH		194	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EPROCUNAVAIL		195	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EFTYPE		196	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EAUTH			197	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_ENEEDAUTH		198	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_ENOATTR		199	/* FreeBSD/Darwin-specific. */
#define	BSM_ERRNO_EDOOFUS		200	/* FreeBSD-specific. */
#define	BSM_ERRNO_EJUSTRETURN		201	/* FreeBSD-specific. */
#define	BSM_ERRNO_ENOIOCTL		202	/* FreeBSD-specific. */
#define	BSM_ERRNO_EDIRIOCTL		203	/* FreeBSD-specific. */
#define	BSM_ERRNO_EPWROFF		204	/* Darwin-specific. */
#define	BSM_ERRNO_EDEVERR		205	/* Darwin-specific. */
#define	BSM_ERRNO_EBADEXEC		206	/* Darwin-specific. */
#define	BSM_ERRNO_EBADARCH		207	/* Darwin-specific. */
#define	BSM_ERRNO_ESHLIBVERS		208	/* Darwin-specific. */
#define	BSM_ERRNO_EBADMACHO		209	/* Darwin-specific. */
#define	BSM_ERRNO_EPOLICY		210	/* Darwin-specific. */
#define	BSM_ERRNO_EDOTDOT		211	/* Linux-specific. */
#define	BSM_ERRNO_EUCLEAN		212	/* Linux-specific. */
#define	BSM_ERRNO_ENOTNAM		213	/* Linux(Xenix?)-specific. */
#define	BSM_ERRNO_ENAVAIL		214	/* Linux(Xenix?)-specific. */
#define	BSM_ERRNO_EISNAM		215	/* Linux(Xenix?)-specific. */
#define	BSM_ERRNO_EREMOTEIO		216	/* Linux-specific. */
#define	BSM_ERRNO_ENOMEDIUM		217	/* Linux-specific. */
#define	BSM_ERRNO_EMEDIUMTYPE		218	/* Linux-specific. */
#define	BSM_ERRNO_ENOKEY		219	/* Linux-specific. */
#define	BSM_ERRNO_EKEYEXPIRED		220	/* Linux-specific. */
#define	BSM_ERRNO_EKEYREVOKED		221	/* Linux-specific. */
#define	BSM_ERRNO_EKEYREJECTED		222	/* Linux-specific. */
#define	BSM_ERRNO_ENOTCAPABLE		223	/* FreeBSD-specific. */
#define	BSM_ERRNO_ECAPMODE		224	/* FreeBSD-specific. */
#define	BSM_ERRNO_EINTEGRITY		225	/* FreeBSD-specific. */

/*
 * In the event that OpenBSM doesn't have a file representation of a local
 * error number, use this.
 */
#define	BSM_ERRNO_UNKNOWN		250	/* OpenBSM-specific. */

#endif /* !_BSM_AUDIT_ERRNO_H_ */
