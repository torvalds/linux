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

#include <sys/types.h>

#include <config/config.h>

#include <bsm/audit_errno.h>
#include <bsm/libbsm.h>

#include <errno.h>
#include <string.h>

/*
 * Different operating systems use different numeric constants for different
 * error numbers, and sometimes error numbers don't exist in more than one
 * operating system.  These routines convert between BSM and local error
 * number spaces, subject to the above realities.  BSM error numbers are
 * stored in a single 8-bit character, so don't have a byte order.
 *
 * Don't include string definitions when this code is compiled into a kernel.
 */
struct bsm_errno {
	int		 be_bsm_errno;
	int		 be_local_errno;
#if !defined(KERNEL) && !defined(_KERNEL)
	const char	*be_strerror;
#endif
};

#define	ERRNO_NO_LOCAL_MAPPING	-600

#if !defined(KERNEL) && !defined(_KERNEL)
#define	ES(x)	x
#else
#define	ES(x)
#endif

/*
 * Mapping table -- please maintain in numeric sorted order with respect to
 * the BSM constant.  Today we do a linear lookup, but could switch to a
 * binary search if it makes sense.  We only ifdef errors that aren't
 * generally available, but it does make the table a lot more ugly.
 *
 * XXXRW: It would be nice to have a similar ordered table mapping to BSM
 * constant from local constant, but the order of local constants varies by
 * OS.  Really we need to build that table at compile-time but don't do that
 * yet.
 *
 * XXXRW: We currently embed English-language error strings here, but should
 * support catalogues; these are only used if the OS doesn't have an error
 * string using strerror(3).
 */
static const struct bsm_errno bsm_errnos[] = {
	{ BSM_ERRNO_ESUCCESS, 0, ES("Success") },
	{ BSM_ERRNO_EPERM, EPERM, ES("Operation not permitted") },
	{ BSM_ERRNO_ENOENT, ENOENT, ES("No such file or directory") },
	{ BSM_ERRNO_ESRCH, ESRCH, ES("No such process") },
	{ BSM_ERRNO_EINTR, EINTR, ES("Interrupted system call") },
	{ BSM_ERRNO_EIO, EIO, ES("Input/output error") },
	{ BSM_ERRNO_ENXIO, ENXIO, ES("Device not configured") },
	{ BSM_ERRNO_E2BIG, E2BIG, ES("Argument list too long") },
	{ BSM_ERRNO_ENOEXEC, ENOEXEC, ES("Exec format error") },
	{ BSM_ERRNO_EBADF, EBADF, ES("Bad file descriptor") },
	{ BSM_ERRNO_ECHILD, ECHILD, ES("No child processes") },
	{ BSM_ERRNO_EAGAIN, EAGAIN, ES("Resource temporarily unavailable") },
	{ BSM_ERRNO_ENOMEM, ENOMEM, ES("Cannot allocate memory") },
	{ BSM_ERRNO_EACCES, EACCES, ES("Permission denied") },
	{ BSM_ERRNO_EFAULT, EFAULT, ES("Bad address") },
	{ BSM_ERRNO_ENOTBLK, ENOTBLK, ES("Block device required") },
	{ BSM_ERRNO_EBUSY, EBUSY, ES("Device busy") },
	{ BSM_ERRNO_EEXIST, EEXIST, ES("File exists") },
	{ BSM_ERRNO_EXDEV, EXDEV, ES("Cross-device link") },
	{ BSM_ERRNO_ENODEV, ENODEV, ES("Operation not supported by device") },
	{ BSM_ERRNO_ENOTDIR, ENOTDIR, ES("Not a directory") },
	{ BSM_ERRNO_EISDIR, EISDIR, ES("Is a directory") },
	{ BSM_ERRNO_EINVAL, EINVAL, ES("Invalid argument") },
	{ BSM_ERRNO_ENFILE, ENFILE, ES("Too many open files in system") },
	{ BSM_ERRNO_EMFILE, EMFILE, ES("Too many open files") },
	{ BSM_ERRNO_ENOTTY, ENOTTY, ES("Inappropriate ioctl for device") },
	{ BSM_ERRNO_ETXTBSY, ETXTBSY, ES("Text file busy") },
	{ BSM_ERRNO_EFBIG, EFBIG, ES("File too large") },
	{ BSM_ERRNO_ENOSPC, ENOSPC, ES("No space left on device") },
	{ BSM_ERRNO_ESPIPE, ESPIPE, ES("Illegal seek") },
	{ BSM_ERRNO_EROFS, EROFS, ES("Read-only file system") },
	{ BSM_ERRNO_EMLINK, EMLINK, ES("Too many links") },
	{ BSM_ERRNO_EPIPE, EPIPE, ES("Broken pipe") },
	{ BSM_ERRNO_EDOM, EDOM, ES("Numerical argument out of domain") },
	{ BSM_ERRNO_ERANGE, ERANGE, ES("Result too large") },
	{ BSM_ERRNO_ENOMSG, ENOMSG, ES("No message of desired type") },
	{ BSM_ERRNO_EIDRM, EIDRM, ES("Identifier removed") },
	{ BSM_ERRNO_ECHRNG,
#ifdef ECHRNG
	ECHRNG,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Channel number out of range") },
	{ BSM_ERRNO_EL2NSYNC,
#ifdef EL2NSYNC
	EL2NSYNC,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Level 2 not synchronized") },
	{ BSM_ERRNO_EL3HLT,
#ifdef EL3HLT
	EL3HLT,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Level 3 halted") },
	{ BSM_ERRNO_EL3RST,
#ifdef EL3RST
	EL3RST,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Level 3 reset") },
	{ BSM_ERRNO_ELNRNG,
#ifdef ELNRNG
	ELNRNG,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Link number out of range") },
	{ BSM_ERRNO_EUNATCH,
#ifdef EUNATCH
	EUNATCH,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Protocol driver not attached") },
	{ BSM_ERRNO_ENOCSI,
#ifdef ENOCSI
	ENOCSI,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("No CSI structure available") },
	{ BSM_ERRNO_EL2HLT,
#ifdef EL2HLT
	EL2HLT,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Level 2 halted") },
	{ BSM_ERRNO_EDEADLK, EDEADLK, ES("Resource deadlock avoided") },
	{ BSM_ERRNO_ENOLCK, ENOLCK, ES("No locks available") },
	{ BSM_ERRNO_ECANCELED, ECANCELED, ES("Operation canceled") },
	{ BSM_ERRNO_ENOTSUP, ENOTSUP, ES("Operation not supported") },
	{ BSM_ERRNO_EDQUOT, EDQUOT, ES("Disc quota exceeded") },
	{ BSM_ERRNO_EBADE,
#ifdef EBADE
	EBADE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Invalid exchange") },
	{ BSM_ERRNO_EBADR,
#ifdef EBADR
	EBADR,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Invalid request descriptor") },
	{ BSM_ERRNO_EXFULL,
#ifdef EXFULL
	EXFULL,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Exchange full") },
	{ BSM_ERRNO_ENOANO,
#ifdef ENOANO
	ENOANO,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("No anode") },
	{ BSM_ERRNO_EBADRQC,
#ifdef EBADRQC
	EBADRQC,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Invalid request descriptor") },
	{ BSM_ERRNO_EBADSLT,
#ifdef EBADSLT
	EBADSLT,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Invalid slot") },
	{ BSM_ERRNO_EDEADLOCK,
#ifdef EDEADLOCK
	EDEADLOCK,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Resource deadlock avoided") },
	{ BSM_ERRNO_EBFONT,
#ifdef EBFONT
	EBFONT,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Bad font file format") },
	{ BSM_ERRNO_EOWNERDEAD,
#ifdef EOWNERDEAD
	EOWNERDEAD,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Process died with the lock") },
	{ BSM_ERRNO_EINTEGRITY,
#ifdef EINTEGRITY
	EINTEGRITY,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Integrity check failed") },
	{ BSM_ERRNO_ENOTRECOVERABLE,
#ifdef ENOTRECOVERABLE
	ENOTRECOVERABLE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Lock is not recoverable") },
	{ BSM_ERRNO_ENOSTR,
#ifdef ENOSTR
	ENOSTR,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Device not a stream") },
	{ BSM_ERRNO_ENONET,
#ifdef ENONET
	ENONET,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Machine is not on the network") },
	{ BSM_ERRNO_ENOPKG,
#ifdef ENOPKG
	ENOPKG,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Package not installed") },
	{ BSM_ERRNO_EREMOTE, EREMOTE,
	    ES("Too many levels of remote in path") },
	{ BSM_ERRNO_ENOLINK,
#ifdef ENOLINK
	ENOLINK,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Link has been severed") },
	{ BSM_ERRNO_EADV,
#ifdef EADV
	EADV,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Advertise error") },
	{ BSM_ERRNO_ESRMNT,
#ifdef ESRMNT
	ESRMNT,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("srmount error") },
	{ BSM_ERRNO_ECOMM,
#ifdef ECOMM
	ECOMM,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Communication error on send") },
	{ BSM_ERRNO_EPROTO,
#ifdef EPROTO
	EPROTO,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Protocol error") },
	{ BSM_ERRNO_ELOCKUNMAPPED,
#ifdef ELOCKUNMAPPED
	ELOCKUNMAPPED,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Locked lock was unmapped") },
	{ BSM_ERRNO_ENOTACTIVE,
#ifdef ENOTACTIVE
	ENOTACTIVE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Facility is not active") },
	{ BSM_ERRNO_EMULTIHOP,
#ifdef EMULTIHOP
	EMULTIHOP,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Multihop attempted") },
	{ BSM_ERRNO_EBADMSG,
#ifdef EBADMSG
	EBADMSG,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Bad message") },
	{ BSM_ERRNO_ENAMETOOLONG, ENAMETOOLONG, ES("File name too long") },
	{ BSM_ERRNO_EOVERFLOW, EOVERFLOW,
	    ES("Value too large to be stored in data type") },
	{ BSM_ERRNO_ENOTUNIQ,
#ifdef ENOTUNIQ
	ENOTUNIQ,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Given log name not unique") },
	{ BSM_ERRNO_EBADFD,
#ifdef EBADFD
	EBADFD,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Given f.d. invalid for this operation") },
	{ BSM_ERRNO_EREMCHG,
#ifdef EREMCHG
	EREMCHG,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Remote address changed") },
	{ BSM_ERRNO_ELIBACC,
#ifdef ELIBACC
	ELIBACC,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Can't access a needed shared lib") },
	{ BSM_ERRNO_ELIBBAD,
#ifdef ELIBBAD
	ELIBBAD,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Accessing a corrupted shared lib") },
	{ BSM_ERRNO_ELIBSCN,
#ifdef ELIBSCN
	ELIBSCN,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES(".lib section in a.out corrupted") },
	{ BSM_ERRNO_ELIBMAX,
#ifdef ELIBMAX
	ELIBMAX,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Attempting to link in too many libs") },
	{ BSM_ERRNO_ELIBEXEC,
#ifdef ELIBEXEC
	ELIBEXEC,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Attempting to exec a shared library") },
	{ BSM_ERRNO_EILSEQ, EILSEQ, ES("Illegal byte sequence") },
	{ BSM_ERRNO_ENOSYS, ENOSYS, ES("Function not implemented") },
	{ BSM_ERRNO_ELOOP, ELOOP, ES("Too many levels of symbolic links") },
	{ BSM_ERRNO_ERESTART,
#ifdef ERESTART
	ERESTART,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Restart syscall") },
	{ BSM_ERRNO_ESTRPIPE,
#ifdef ESTRPIPE
	ESTRPIPE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("If pipe/FIFO, don't sleep in stream head") },
	{ BSM_ERRNO_ENOTEMPTY, ENOTEMPTY, ES("Directory not empty") },
	{ BSM_ERRNO_EUSERS, EUSERS, ES("Too many users") },
	{ BSM_ERRNO_ENOTSOCK, ENOTSOCK,
	    ES("Socket operation on non-socket") },
	{ BSM_ERRNO_EDESTADDRREQ, EDESTADDRREQ,
	    ES("Destination address required") },
	{ BSM_ERRNO_EMSGSIZE, EMSGSIZE, ES("Message too long") },
	{ BSM_ERRNO_EPROTOTYPE, EPROTOTYPE,
	    ES("Protocol wrong type for socket") },
	{ BSM_ERRNO_ENOPROTOOPT, ENOPROTOOPT, ES("Protocol not available") },
	{ BSM_ERRNO_EPROTONOSUPPORT, EPROTONOSUPPORT,
	    ES("Protocol not supported") },
	{ BSM_ERRNO_ESOCKTNOSUPPORT, ESOCKTNOSUPPORT,
	    ES("Socket type not supported") },
	{ BSM_ERRNO_EOPNOTSUPP, EOPNOTSUPP, ES("Operation not supported") },
	{ BSM_ERRNO_EPFNOSUPPORT, EPFNOSUPPORT,
	    ES("Protocol family not supported") },
	{ BSM_ERRNO_EAFNOSUPPORT, EAFNOSUPPORT,
	    ES("Address family not supported by protocol family") },
	{ BSM_ERRNO_EADDRINUSE, EADDRINUSE, ES("Address already in use") },
	{ BSM_ERRNO_EADDRNOTAVAIL, EADDRNOTAVAIL,
	    ES("Can't assign requested address") },
	{ BSM_ERRNO_ENETDOWN, ENETDOWN, ES("Network is down") },
	{ BSM_ERRNO_ENETRESET, ENETRESET,
	    ES("Network dropped connection on reset") },
	{ BSM_ERRNO_ECONNABORTED, ECONNABORTED,
	    ES("Software caused connection abort") },
	{ BSM_ERRNO_ECONNRESET, ECONNRESET, ES("Connection reset by peer") },
	{ BSM_ERRNO_ENOBUFS, ENOBUFS, ES("No buffer space available") },
	{ BSM_ERRNO_EISCONN, EISCONN, ES("Socket is already connected") },
	{ BSM_ERRNO_ENOTCONN, ENOTCONN, ES("Socket is not connected") },
	{ BSM_ERRNO_ESHUTDOWN, ESHUTDOWN,
	    ES("Can't send after socket shutdown") },
	{ BSM_ERRNO_ETOOMANYREFS, ETOOMANYREFS,
	    ES("Too many references: can't splice") },
	{ BSM_ERRNO_ETIMEDOUT, ETIMEDOUT, ES("Operation timed out") },
	{ BSM_ERRNO_ECONNREFUSED, ECONNREFUSED, ES("Connection refused") },
	{ BSM_ERRNO_EHOSTDOWN, EHOSTDOWN, ES("Host is down") },
	{ BSM_ERRNO_EHOSTUNREACH, EHOSTUNREACH, ES("No route to host") },
	{ BSM_ERRNO_EALREADY, EALREADY, ES("Operation already in progress") },
	{ BSM_ERRNO_EINPROGRESS, EINPROGRESS,
	    ES("Operation now in progress") },
	{ BSM_ERRNO_ESTALE, ESTALE, ES("Stale NFS file handle") },
	{ BSM_ERRNO_EPROCLIM,
#ifdef EPROCLIM
	EPROCLIM,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Too many processes") },
	{ BSM_ERRNO_EBADRPC,
#ifdef EBADRPC
	EBADRPC,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("RPC struct is bad") },
	{ BSM_ERRNO_ERPCMISMATCH,
#ifdef ERPCMISMATCH
	ERPCMISMATCH,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("RPC version wrong") },
	{ BSM_ERRNO_EPROGUNAVAIL,
#ifdef EPROGUNAVAIL
	EPROGUNAVAIL,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("RPC prog. not avail") },
	{ BSM_ERRNO_EPROGMISMATCH,
#ifdef EPROGMISMATCH
	EPROGMISMATCH,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("RPC version wrong") },
	{ BSM_ERRNO_EPROCUNAVAIL,
#ifdef EPROCUNAVAIL
	EPROCUNAVAIL,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Bad procedure for program") },
	{ BSM_ERRNO_EFTYPE,
#ifdef EFTYPE
	EFTYPE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Inappropriate file type or format") },
	{ BSM_ERRNO_EAUTH,
#ifdef EAUTH
	EAUTH,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Authenticateion error") },
	{ BSM_ERRNO_ENEEDAUTH,
#ifdef ENEEDAUTH
	ENEEDAUTH,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Need authenticator") },
	{ BSM_ERRNO_ENOATTR,
#ifdef ENOATTR
	ENOATTR,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Attribute not found") },
	{ BSM_ERRNO_EDOOFUS,
#ifdef EDOOFUS
	EDOOFUS,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Programming error") },
	{ BSM_ERRNO_EJUSTRETURN,
#ifdef EJUSTRETURN
	EJUSTRETURN,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Just return") },
	{ BSM_ERRNO_ENOIOCTL,
#ifdef ENOIOCTL
	ENOIOCTL,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("ioctl not handled by this layer") },
	{ BSM_ERRNO_EDIRIOCTL,
#ifdef EDIRIOCTL
	EDIRIOCTL,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("do direct ioctl in GEOM") },
	{ BSM_ERRNO_EPWROFF,
#ifdef EPWROFF
	EPWROFF,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Device power is off") },
	{ BSM_ERRNO_EDEVERR,
#ifdef EDEVERR
	EDEVERR,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Device error") },
	{ BSM_ERRNO_EBADEXEC,
#ifdef EBADEXEC
	EBADEXEC,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Bad executable") },
	{ BSM_ERRNO_EBADARCH,
#ifdef EBADARCH
	EBADARCH,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Bad CPU type in executable") },
	{ BSM_ERRNO_ESHLIBVERS,
#ifdef ESHLIBVERS
	ESHLIBVERS,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Shared library version mismatch") },
	{ BSM_ERRNO_EBADMACHO,
#ifdef EBADMACHO
	EBADMACHO,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Malformed Macho file") },
	{ BSM_ERRNO_EPOLICY,
#ifdef EPOLICY
	EPOLICY,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Operation failed by policy") },
	{ BSM_ERRNO_EDOTDOT,
#ifdef EDOTDOT
	EDOTDOT,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("RFS specific error") },
	{ BSM_ERRNO_EUCLEAN,
#ifdef EUCLEAN
	EUCLEAN,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Structure needs cleaning") },
	{ BSM_ERRNO_ENOTNAM,
#ifdef ENOTNAM
	ENOTNAM,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Not a XENIX named type file") },
	{ BSM_ERRNO_ENAVAIL,
#ifdef ENAVAIL
	ENAVAIL,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("No XENIX semaphores available") },
	{ BSM_ERRNO_EISNAM,
#ifdef EISNAM
	EISNAM,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Is a named type file") },
	{ BSM_ERRNO_EREMOTEIO,
#ifdef EREMOTEIO
	EREMOTEIO,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Remote I/O error") },
	{ BSM_ERRNO_ENOMEDIUM,
#ifdef ENOMEDIUM
	ENOMEDIUM,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("No medium found") },
	{ BSM_ERRNO_EMEDIUMTYPE,
#ifdef EMEDIUMTYPE
	EMEDIUMTYPE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Wrong medium type") },
	{ BSM_ERRNO_ENOKEY,
#ifdef ENOKEY
	ENOKEY,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Required key not available") },
	{ BSM_ERRNO_EKEYEXPIRED,
#ifdef EKEYEXPIRED
	EKEYEXPIRED,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Key has expired") },
	{ BSM_ERRNO_EKEYREVOKED,
#ifdef EKEYREVOKED
	EKEYREVOKED,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Key has been revoked") },
	{ BSM_ERRNO_EKEYREJECTED,
#ifdef EKEYREJECTED
	EKEYREJECTED,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Key was rejected by service") },
	{ BSM_ERRNO_ENOTCAPABLE,
#ifdef ENOTCAPABLE
	ENOTCAPABLE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Capabilities insufficient") },
	{ BSM_ERRNO_ECAPMODE,
#ifdef ECAPMODE
	ECAPMODE,
#else
	ERRNO_NO_LOCAL_MAPPING,
#endif
	ES("Not permitted in capability mode") },
};
static const int bsm_errnos_count = sizeof(bsm_errnos) / sizeof(bsm_errnos[0]);

static const struct bsm_errno *
bsm_lookup_errno_local(int local_errno)
{
	int i;

	for (i = 0; i < bsm_errnos_count; i++) {
		if (bsm_errnos[i].be_local_errno == local_errno)
			return (&bsm_errnos[i]);
	}
	return (NULL);
}

/*
 * Conversion to the BSM errno space isn't allowed to fail; we simply map to
 * BSM_ERRNO_UNKNOWN and let the remote endpoint deal with it.
 */
u_char
au_errno_to_bsm(int local_errno)
{
	const struct bsm_errno *bsme;

	bsme = bsm_lookup_errno_local(local_errno);
	if (bsme == NULL)
		return (BSM_ERRNO_UNKNOWN);
	return (bsme->be_bsm_errno);
}

static const struct bsm_errno *
bsm_lookup_errno_bsm(u_char bsm_errno)
{
	int i;

	for (i = 0; i < bsm_errnos_count; i++) {
		if (bsm_errnos[i].be_bsm_errno == bsm_errno)
			return (&bsm_errnos[i]);
	}
	return (NULL);
}

/*
 * Converstion from a BSM error to a local error number may fail if either
 * OpenBSM doesn't recognize the error on the wire, or because there is no
 * appropriate local mapping.
 */
int
au_bsm_to_errno(u_char bsm_errno, int *errorp)
{
	const struct bsm_errno *bsme;

	bsme = bsm_lookup_errno_bsm(bsm_errno);
	if (bsme == NULL || bsme->be_local_errno == ERRNO_NO_LOCAL_MAPPING)
		return (-1);
	*errorp = bsme->be_local_errno;
	return (0);
}

#if !defined(KERNEL) && !defined(_KERNEL)
const char *
au_strerror(u_char bsm_errno)
{
	const struct bsm_errno *bsme;

	bsme = bsm_lookup_errno_bsm(bsm_errno);
	if (bsme == NULL)
		return ("Unrecognized BSM error");
	if (bsme->be_local_errno != ERRNO_NO_LOCAL_MAPPING)
		return (strerror(bsme->be_local_errno));
	return (bsme->be_strerror);
}
#endif
