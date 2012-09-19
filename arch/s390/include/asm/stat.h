/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/stat.h"
 */

#ifndef _S390_STAT_H
#define _S390_STAT_H

#ifndef __s390x__
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

struct stat {
        unsigned short st_dev;
        unsigned short __pad1;
        unsigned long  st_ino;
        unsigned short st_mode;
        unsigned short st_nlink;
        unsigned short st_uid;
        unsigned short st_gid;
        unsigned short st_rdev;
        unsigned short __pad2;
        unsigned long  st_size;
        unsigned long  st_blksize;
        unsigned long  st_blocks;
        unsigned long  st_atime;
        unsigned long  st_atime_nsec;
        unsigned long  st_mtime;
        unsigned long  st_mtime_nsec;
        unsigned long  st_ctime;
        unsigned long  st_ctime_nsec;
        unsigned long  __unused4;
        unsigned long  __unused5;
};

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
struct stat64 {
        unsigned long long	st_dev;
        unsigned int    __pad1;
#define STAT64_HAS_BROKEN_ST_INO        1
        unsigned long   __st_ino;
        unsigned int    st_mode;
        unsigned int    st_nlink;
        unsigned long   st_uid;
        unsigned long   st_gid;
        unsigned long long	st_rdev;
        unsigned int    __pad3;
        long long	st_size;
        unsigned long   st_blksize;
        unsigned char   __pad4[4];
        unsigned long   __pad5;     /* future possible st_blocks high bits */
        unsigned long   st_blocks;  /* Number 512-byte blocks allocated. */
        unsigned long   st_atime;
        unsigned long   st_atime_nsec;
        unsigned long   st_mtime;
        unsigned long   st_mtime_nsec;
        unsigned long   st_ctime;
        unsigned long   st_ctime_nsec;  /* will be high 32 bits of ctime someday */
        unsigned long long	st_ino;
};

#else /* __s390x__ */

struct stat {
        unsigned long  st_dev;
        unsigned long  st_ino;
        unsigned long  st_nlink;
        unsigned int   st_mode;
        unsigned int   st_uid;
        unsigned int   st_gid;
        unsigned int   __pad1;
        unsigned long  st_rdev;
        unsigned long  st_size;
        unsigned long  st_atime;
	unsigned long  st_atime_nsec;
        unsigned long  st_mtime;
	unsigned long  st_mtime_nsec;
        unsigned long  st_ctime;
	unsigned long  st_ctime_nsec;
        unsigned long  st_blksize;
        long           st_blocks;
        unsigned long  __unused[3];
};

#endif /* __s390x__ */

#define STAT_HAVE_NSEC 1

#endif
