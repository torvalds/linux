/*
 * linux/fs/9p/error.h
 *
 * Huge Nasty Error Table
 *
 * Plan 9 uses error strings, Unix uses error numbers.  This table tries to
 * match UNIX strings and Plan 9 strings to unix error numbers.  It is used
 * to preload the dynamic error table which can also track user-specific error
 * strings.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/errno.h>
#include <asm/errno.h>

struct errormap {
	char *name;
	int val;

	struct hlist_node list;
};

#define ERRHASHSZ		32
static struct hlist_head hash_errmap[ERRHASHSZ];

/* FixMe - reduce to a reasonable size */
static struct errormap errmap[] = {
	{"Operation not permitted", EPERM},
	{"wstat prohibited", EPERM},
	{"No such file or directory", ENOENT},
	{"directory entry not found", ENOENT},
	{"file not found", ENOENT},
	{"Interrupted system call", EINTR},
	{"Input/output error", EIO},
	{"No such device or address", ENXIO},
	{"Argument list too long", E2BIG},
	{"Bad file descriptor", EBADF},
	{"Resource temporarily unavailable", EAGAIN},
	{"Cannot allocate memory", ENOMEM},
	{"Permission denied", EACCES},
	{"Bad address", EFAULT},
	{"Block device required", ENOTBLK},
	{"Device or resource busy", EBUSY},
	{"File exists", EEXIST},
	{"Invalid cross-device link", EXDEV},
	{"No such device", ENODEV},
	{"Not a directory", ENOTDIR},
	{"Is a directory", EISDIR},
	{"Invalid argument", EINVAL},
	{"Too many open files in system", ENFILE},
	{"Too many open files", EMFILE},
	{"Text file busy", ETXTBSY},
	{"File too large", EFBIG},
	{"No space left on device", ENOSPC},
	{"Illegal seek", ESPIPE},
	{"Read-only file system", EROFS},
	{"Too many links", EMLINK},
	{"Broken pipe", EPIPE},
	{"Numerical argument out of domain", EDOM},
	{"Numerical result out of range", ERANGE},
	{"Resource deadlock avoided", EDEADLK},
	{"File name too long", ENAMETOOLONG},
	{"No locks available", ENOLCK},
	{"Function not implemented", ENOSYS},
	{"Directory not empty", ENOTEMPTY},
	{"Too many levels of symbolic links", ELOOP},
	{"No message of desired type", ENOMSG},
	{"Identifier removed", EIDRM},
	{"No data available", ENODATA},
	{"Machine is not on the network", ENONET},
	{"Package not installed", ENOPKG},
	{"Object is remote", EREMOTE},
	{"Link has been severed", ENOLINK},
	{"Communication error on send", ECOMM},
	{"Protocol error", EPROTO},
	{"Bad message", EBADMSG},
	{"File descriptor in bad state", EBADFD},
	{"Streams pipe error", ESTRPIPE},
	{"Too many users", EUSERS},
	{"Socket operation on non-socket", ENOTSOCK},
	{"Message too long", EMSGSIZE},
	{"Protocol not available", ENOPROTOOPT},
	{"Protocol not supported", EPROTONOSUPPORT},
	{"Socket type not supported", ESOCKTNOSUPPORT},
	{"Operation not supported", EOPNOTSUPP},
	{"Protocol family not supported", EPFNOSUPPORT},
	{"Network is down", ENETDOWN},
	{"Network is unreachable", ENETUNREACH},
	{"Network dropped connection on reset", ENETRESET},
	{"Software caused connection abort", ECONNABORTED},
	{"Connection reset by peer", ECONNRESET},
	{"No buffer space available", ENOBUFS},
	{"Transport endpoint is already connected", EISCONN},
	{"Transport endpoint is not connected", ENOTCONN},
	{"Cannot send after transport endpoint shutdown", ESHUTDOWN},
	{"Connection timed out", ETIMEDOUT},
	{"Connection refused", ECONNREFUSED},
	{"Host is down", EHOSTDOWN},
	{"No route to host", EHOSTUNREACH},
	{"Operation already in progress", EALREADY},
	{"Operation now in progress", EINPROGRESS},
	{"Is a named type file", EISNAM},
	{"Remote I/O error", EREMOTEIO},
	{"Disk quota exceeded", EDQUOT},
/* errors from fossil, vacfs, and u9fs */
	{"fid unknown or out of range", EBADF},
	{"permission denied", EACCES},
	{"file does not exist", ENOENT},
	{"authentication failed", ECONNREFUSED},
	{"bad offset in directory read", ESPIPE},
	{"bad use of fid", EBADF},
	{"wstat can't convert between files and directories", EPERM},
	{"directory is not empty", ENOTEMPTY},
	{"file exists", EEXIST},
	{"file already exists", EEXIST},
	{"file or directory already exists", EEXIST},
	{"fid already in use", EBADF},
	{"file in use", ETXTBSY},
	{"i/o error", EIO},
	{"file already open for I/O", ETXTBSY},
	{"illegal mode", EINVAL},
	{"illegal name", ENAMETOOLONG},
	{"not a directory", ENOTDIR},
	{"not a member of proposed group", EPERM},
	{"not owner", EACCES},
	{"only owner can change group in wstat", EACCES},
	{"read only file system", EROFS},
	{"no access to special file", EPERM},
	{"i/o count too large", EIO},
	{"unknown group", EINVAL},
	{"unknown user", EINVAL},
	{"bogus wstat buffer", EPROTO},
	{"exclusive use file already open", EAGAIN},
	{"corrupted directory entry", EIO},
	{"corrupted file entry", EIO},
	{"corrupted block label", EIO},
	{"corrupted meta data", EIO},
	{"illegal offset", EINVAL},
	{"illegal path element", ENOENT},
	{"root of file system is corrupted", EIO},
	{"corrupted super block", EIO},
	{"protocol botch", EPROTO},
	{"file system is full", ENOSPC},
	{"file is in use", EAGAIN},
	{"directory entry is not allocated", ENOENT},
	{"file is read only", EROFS},
	{"file has been removed", EIDRM},
	{"only support truncation to zero length", EPERM},
	{"cannot remove root", EPERM},
	{"file too big", EFBIG},
	{"venti i/o error", EIO},
	/* these are not errors */
	{"u9fs rhostsauth: no authentication required", 0},
	{"u9fs authnone: no authentication required", 0},
	{NULL, -1}
};

extern int v9fs_error_init(void);
extern int v9fs_errstr2errno(char *errstr);
