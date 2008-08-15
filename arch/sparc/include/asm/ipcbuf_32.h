#ifndef _SPARC_IPCBUF_H
#define _SPARC_IPCBUF_H

/*
 * The ipc64_perm structure for sparc architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 32-bit mode
 * - 32-bit seq
 * - 2 miscellaneous 64-bit values (so that this structure matches
 *				    sparc64 ipc64_perm)
 */

struct ipc64_perm
{
	__kernel_key_t		key;
	__kernel_uid32_t	uid;
	__kernel_gid32_t	gid;
	__kernel_uid32_t	cuid;
	__kernel_gid32_t	cgid;
	unsigned short		__pad1;
	__kernel_mode_t		mode;
	unsigned short		__pad2;
	unsigned short		seq;
	unsigned long long	__unused1;
	unsigned long long	__unused2;
};

#endif /* _SPARC_IPCBUF_H */
