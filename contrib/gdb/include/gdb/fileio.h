/* Hosted File I/O interface definitions, for GDB, the GNU Debugger.

   Copyright 2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef GDB_FILEIO_H_
#define GDB_FILEIO_H_

/* The following flags are defined to be independent of the host
   as well as the target side implementation of these constants.
   All constants are defined with a leading FILEIO_ in the name
   to allow the usage of these constants together with the
   corresponding implementation dependent constants in one module. */

/* open(2) flags */
#define FILEIO_O_RDONLY           0x0
#define FILEIO_O_WRONLY           0x1
#define FILEIO_O_RDWR             0x2
#define FILEIO_O_APPEND           0x8
#define FILEIO_O_CREAT          0x200
#define FILEIO_O_TRUNC          0x400
#define FILEIO_O_EXCL           0x800
#define FILEIO_O_SUPPORTED	(FILEIO_O_RDONLY | FILEIO_O_WRONLY| \
				 FILEIO_O_RDWR   | FILEIO_O_APPEND| \
				 FILEIO_O_CREAT  | FILEIO_O_TRUNC| \
				 FILEIO_O_EXCL)

/* mode_t bits */
#define FILEIO_S_IFREG        0100000
#define FILEIO_S_IFDIR         040000
#define FILEIO_S_IFCHR         020000
#define FILEIO_S_IRUSR           0400
#define FILEIO_S_IWUSR           0200
#define FILEIO_S_IXUSR           0100
#define FILEIO_S_IRWXU           0700
#define FILEIO_S_IRGRP            040
#define FILEIO_S_IWGRP            020
#define FILEIO_S_IXGRP            010
#define FILEIO_S_IRWXG            070
#define FILEIO_S_IROTH             04
#define FILEIO_S_IWOTH             02
#define FILEIO_S_IXOTH             01
#define FILEIO_S_IRWXO             07
#define FILEIO_S_SUPPORTED         (FILEIO_S_IFREG|FILEIO_S_IFDIR|  \
				    FILEIO_S_IRWXU|FILEIO_S_IRWXG|  \
                                    FILEIO_S_IRWXO)

/* lseek(2) flags */
#define FILEIO_SEEK_SET             0
#define FILEIO_SEEK_CUR             1
#define FILEIO_SEEK_END             2

/* errno values */
#define FILEIO_EPERM                1
#define FILEIO_ENOENT               2
#define FILEIO_EINTR                4
#define FILEIO_EIO                  5
#define FILEIO_EBADF                9
#define FILEIO_EACCES              13
#define FILEIO_EFAULT              14
#define FILEIO_EBUSY               16
#define FILEIO_EEXIST              17
#define FILEIO_ENODEV              19
#define FILEIO_ENOTDIR             20
#define FILEIO_EISDIR              21
#define FILEIO_EINVAL              22
#define FILEIO_ENFILE              23
#define FILEIO_EMFILE              24
#define FILEIO_EFBIG               27
#define FILEIO_ENOSPC              28
#define FILEIO_ESPIPE              29
#define FILEIO_EROFS               30
#define FILEIO_ENOSYS		   88
#define FILEIO_ENAMETOOLONG        91
#define FILEIO_EUNKNOWN          9999

/* limits */
#define FILEIO_INT_MIN    -2147483648L
#define FILEIO_INT_MAX     2147483647L
#define FILEIO_UINT_MAX    4294967295UL
#define FILEIO_LONG_MIN   -9223372036854775808LL
#define FILEIO_LONG_MAX    9223372036854775807LL
#define FILEIO_ULONG_MAX   18446744073709551615ULL

/* Integral types as used in protocol. */
#if 0
typedef __int32_t fio_int_t;
typedef __uint32_t fio_uint_t, fio_mode_t, fio_time_t;
typedef __int64_t fio_long_t;
typedef __uint64_t fio_ulong_t;
#endif

#define FIO_INT_LEN   4
#define FIO_UINT_LEN  4
#define FIO_MODE_LEN  4
#define FIO_TIME_LEN  4
#define FIO_LONG_LEN  8
#define FIO_ULONG_LEN 8

typedef char fio_int_t[FIO_INT_LEN];   
typedef char fio_uint_t[FIO_UINT_LEN];
typedef char fio_mode_t[FIO_MODE_LEN];
typedef char fio_time_t[FIO_TIME_LEN];
typedef char fio_long_t[FIO_LONG_LEN];
typedef char fio_ulong_t[FIO_ULONG_LEN];

/* Struct stat as used in protocol.  For complete independence
   of host/target systems, it's defined as an array with offsets
   to the members. */

struct fio_stat {
  fio_uint_t  fst_dev;
  fio_uint_t  fst_ino;
  fio_mode_t  fst_mode;
  fio_uint_t  fst_nlink;
  fio_uint_t  fst_uid;
  fio_uint_t  fst_gid;
  fio_uint_t  fst_rdev;
  fio_ulong_t fst_size;
  fio_ulong_t fst_blksize;
  fio_ulong_t fst_blocks;
  fio_time_t  fst_atime;
  fio_time_t  fst_mtime;
  fio_time_t  fst_ctime;
};

struct fio_timeval {
  fio_time_t  ftv_sec;
  fio_long_t  ftv_usec;
};

#endif /* GDB_FILEIO_H_ */
