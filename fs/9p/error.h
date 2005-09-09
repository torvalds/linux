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

struct errormap {
	char *name;
	int val;

	struct hlist_node list;
};

#define ERRHASHSZ		32
static struct hlist_head hash_errmap[ERRHASHSZ];

/* FixMe - reduce to a reasonable size */
static struct errormap errmap[] = {
	{"Operation not permitted", 1},
	{"wstat prohibited", 1},
	{"No such file or directory", 2},
	{"file not found", 2},
	{"Interrupted system call", 4},
	{"Input/output error", 5},
	{"No such device or address", 6},
	{"Argument list too long", 7},
	{"Bad file descriptor", 9},
	{"Resource temporarily unavailable", 11},
	{"Cannot allocate memory", 12},
	{"Permission denied", 13},
	{"Bad address", 14},
	{"Block device required", 15},
	{"Device or resource busy", 16},
	{"File exists", 17},
	{"Invalid cross-device link", 18},
	{"No such device", 19},
	{"Not a directory", 20},
	{"Is a directory", 21},
	{"Invalid argument", 22},
	{"Too many open files in system", 23},
	{"Too many open files", 24},
	{"Text file busy", 26},
	{"File too large", 27},
	{"No space left on device", 28},
	{"Illegal seek", 29},
	{"Read-only file system", 30},
	{"Too many links", 31},
	{"Broken pipe", 32},
	{"Numerical argument out of domain", 33},
	{"Numerical result out of range", 34},
	{"Resource deadlock avoided", 35},
	{"File name too long", 36},
	{"No locks available", 37},
	{"Function not implemented", 38},
	{"Directory not empty", 39},
	{"Too many levels of symbolic links", 40},
	{"Unknown error 41", 41},
	{"No message of desired type", 42},
	{"Identifier removed", 43},
	{"File locking deadlock error", 58},
	{"No data available", 61},
	{"Machine is not on the network", 64},
	{"Package not installed", 65},
	{"Object is remote", 66},
	{"Link has been severed", 67},
	{"Communication error on send", 70},
	{"Protocol error", 71},
	{"Bad message", 74},
	{"File descriptor in bad state", 77},
	{"Streams pipe error", 86},
	{"Too many users", 87},
	{"Socket operation on non-socket", 88},
	{"Message too long", 90},
	{"Protocol not available", 92},
	{"Protocol not supported", 93},
	{"Socket type not supported", 94},
	{"Operation not supported", 95},
	{"Protocol family not supported", 96},
	{"Network is down", 100},
	{"Network is unreachable", 101},
	{"Network dropped connection on reset", 102},
	{"Software caused connection abort", 103},
	{"Connection reset by peer", 104},
	{"No buffer space available", 105},
	{"Transport endpoint is already connected", 106},
	{"Transport endpoint is not connected", 107},
	{"Cannot send after transport endpoint shutdown", 108},
	{"Connection timed out", 110},
	{"Connection refused", 111},
	{"Host is down", 112},
	{"No route to host", 113},
	{"Operation already in progress", 114},
	{"Operation now in progress", 115},
	{"Is a named type file", 120},
	{"Remote I/O error", 121},
	{"Disk quota exceeded", 122},
	{"Operation canceled", 125},
	{"Unknown error 126", 126},
	{"Unknown error 127", 127},
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
	{"not a member of proposed group", EINVAL},
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
