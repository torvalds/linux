#ifndef __PARISC_IPCBUF_H__
#define __PARISC_IPCBUF_H__

/*
 * The ipc64_perm structure for PA-RISC is almost identical to
 * kern_ipc_perm as we have always had 32-bit UIDs and GIDs in the kernel.
 * 'seq' has been changed from long to int so that it's the same size
 * on 64-bit kernels as on 32-bit ones.
 */

struct ipc64_perm
{
	key_t           key;
	uid_t           uid;
	gid_t           gid;
	uid_t           cuid;
	gid_t           cgid;
	unsigned short int	__pad1;
	mode_t          mode;
	unsigned short int	__pad2;
	unsigned short int	seq;
	unsigned int	__pad3;
	unsigned long long int __unused1;
	unsigned long long int __unused2;
};

#endif /* __PARISC_IPCBUF_H__ */
