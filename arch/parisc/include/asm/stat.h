#ifndef _PARISC_STAT_H
#define _PARISC_STAT_H

#include <linux/types.h>

struct stat {
	unsigned int	st_dev;		/* dev_t is 32 bits on parisc */
	ino_t		st_ino;		/* 32 bits */
	mode_t		st_mode;	/* 16 bits */
	unsigned short	st_nlink;	/* 16 bits */
	unsigned short	st_reserved1;	/* old st_uid */
	unsigned short	st_reserved2;	/* old st_gid */
	unsigned int	st_rdev;
	off_t		st_size;
	time_t		st_atime;
	unsigned int	st_atime_nsec;
	time_t		st_mtime;
	unsigned int	st_mtime_nsec;
	time_t		st_ctime;
	unsigned int	st_ctime_nsec;
	int		st_blksize;
	int		st_blocks;
	unsigned int	__unused1;	/* ACL stuff */
	unsigned int	__unused2;	/* network */
	ino_t		__unused3;	/* network */
	unsigned int	__unused4;	/* cnodes */
	unsigned short	__unused5;	/* netsite */
	short		st_fstype;
	unsigned int	st_realdev;
	unsigned short	st_basemode;
	unsigned short	st_spareshort;
	uid_t		st_uid;
	gid_t		st_gid;
	unsigned int	st_spare4[3];
};

#define STAT_HAVE_NSEC

typedef __kernel_off64_t	off64_t;

struct hpux_stat64 {
	unsigned int	st_dev;		/* dev_t is 32 bits on parisc */
	ino_t           st_ino;         /* 32 bits */
	mode_t		st_mode;	/* 16 bits */
	unsigned short	st_nlink;	/* 16 bits */
	unsigned short	st_reserved1;	/* old st_uid */
	unsigned short	st_reserved2;	/* old st_gid */
	unsigned int	st_rdev;
	off64_t		st_size;
	time_t		st_atime;
	unsigned int	st_spare1;
	time_t		st_mtime;
	unsigned int	st_spare2;
	time_t		st_ctime;
	unsigned int	st_spare3;
	int		st_blksize;
	__u64		st_blocks;
	unsigned int	__unused1;	/* ACL stuff */
	unsigned int	__unused2;	/* network */
	ino_t           __unused3;      /* network */
	unsigned int	__unused4;	/* cnodes */
	unsigned short	__unused5;	/* netsite */
	short		st_fstype;
	unsigned int	st_realdev;
	unsigned short	st_basemode;
	unsigned short	st_spareshort;
	uid_t		st_uid;
	gid_t		st_gid;
	unsigned int	st_spare4[3];
};

/* This is the struct that 32-bit userspace applications are expecting.
 * How 64-bit apps are going to be compiled, I have no idea.  But at least
 * this way, we don't have a wrapper in the kernel.
 */
struct stat64 {
	unsigned long long	st_dev;
	unsigned int		__pad1;

	unsigned int		__st_ino;	/* Not actually filled in */
	unsigned int		st_mode;
	unsigned int		st_nlink;
	unsigned int		st_uid;
	unsigned int		st_gid;
	unsigned long long	st_rdev;
	unsigned int		__pad2;
	signed long long	st_size;
	signed int		st_blksize;

	signed long long	st_blocks;
	signed int		st_atime;
	unsigned int		st_atime_nsec;
	signed int		st_mtime;
	unsigned int		st_mtime_nsec;
	signed int		st_ctime;
	unsigned int		st_ctime_nsec;
	unsigned long long	st_ino;
};

#endif
