#ifndef _ASM_POWERPC_IPCBUF_H
#define _ASM_POWERPC_IPCBUF_H

/*
 * The ipc64_perm structure for the powerpc is identical to
 * kern_ipc_perm as we have always had 32-bit UIDs and GIDs in the
 * kernel.  Note extra padding because this structure is passed back
 * and forth between kernel and user space.  Pad space is left for:
 *	- 1 32-bit value to fill up for 8-byte alignment
 *	- 2 miscellaneous 64-bit values
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>

struct ipc64_perm
{
	__kernel_key_t	key;
	__kernel_uid_t	uid;
	__kernel_gid_t	gid;
	__kernel_uid_t	cuid;
	__kernel_gid_t	cgid;
	__kernel_mode_t	mode;
	unsigned int	seq;
	unsigned int	__pad1;
	unsigned long long __unused1;
	unsigned long long __unused2;
};

#endif /* _ASM_POWERPC_IPCBUF_H */
