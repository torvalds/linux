#ifndef _ASM_STAT_H
#define _ASM_STAT_H

struct __old_kernel_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};

/* This matches struct stat in uClibc/glibc.  */
struct stat {
	unsigned char __pad1[6];
	unsigned short st_dev;

	unsigned long __pad2;
	unsigned long st_ino;

	unsigned short __pad3;
	unsigned short st_mode;
	unsigned short __pad4;
	unsigned short st_nlink;

	unsigned short __pad5;
	unsigned short st_uid;
	unsigned short __pad6;
	unsigned short st_gid;

	unsigned char __pad7[6];
	unsigned short st_rdev;

	unsigned long __pad8;
	unsigned long st_size;

	unsigned long __pad9;		/* align 64-bit st_blocks to 2-word */
	unsigned long st_blksize;

	unsigned long __pad10;	/* future possible st_blocks high bits */
	unsigned long st_blocks;	/* Number 512-byte blocks allocated. */

	unsigned long __unused1;
	unsigned long st_atime;

	unsigned long __unused2;
	unsigned long st_mtime;

	unsigned long __unused3;
	unsigned long st_ctime;

	unsigned long long __unused4;
};

/* This matches struct stat64 in uClibc/glibc.  The layout is exactly
   the same as that of struct stat above, with 64-bit types taking up
   space that was formerly used by padding.  stat syscalls are still
   different from stat64, though, in that the former tests for
   overflow.  */
struct stat64 {
	unsigned char __pad1[6];
	unsigned short st_dev;

	unsigned long long st_ino;

	unsigned int st_mode;
	unsigned int st_nlink;

	unsigned long st_uid;
	unsigned long st_gid;

	unsigned char __pad2[6];
	unsigned short st_rdev;

	long long st_size;

	unsigned long __pad3;		/* align 64-bit st_blocks to 2-word */
	unsigned long st_blksize;

	unsigned long __pad4;		/* future possible st_blocks high bits */
	unsigned long st_blocks;	/* Number 512-byte blocks allocated. */

	unsigned long st_atime_nsec;
	unsigned long st_atime;

	unsigned int st_mtime_nsec;
	unsigned long st_mtime;

	unsigned long st_ctime_nsec;
	unsigned long st_ctime;

	unsigned long long __unused4;
};

#endif /* _ASM_STAT_H */
