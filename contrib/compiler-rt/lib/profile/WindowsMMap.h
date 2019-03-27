/*===- WindowsMMap.h - Support library for PGO instrumentation ------------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#ifndef PROFILE_INSTRPROFILING_WINDOWS_MMAP_H
#define PROFILE_INSTRPROFILING_WINDOWS_MMAP_H

#if defined(_WIN32)

#include <basetsd.h>
#include <io.h>
#include <sys/types.h>

/*
 * mmap() flags
 */
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x0

#define MAP_FILE      0x00
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *) -1)

/*
 * msync() flags
 */
#define MS_ASYNC        0x0001  /* return immediately */
#define MS_INVALIDATE   0x0002  /* invalidate all cached data */
#define MS_SYNC         0x0010  /* msync synchronously */

/*
 * flock() operations
 */
#define   LOCK_SH   1    /* shared lock */
#define   LOCK_EX   2    /* exclusive lock */
#define   LOCK_NB   4    /* don't block when locking */
#define   LOCK_UN   8    /* unlock */

#ifdef __USE_FILE_OFFSET64
# define DWORD_HI(x) (x >> 32)
# define DWORD_LO(x) ((x) & 0xffffffff)
#else
# define DWORD_HI(x) (0)
# define DWORD_LO(x) (x)
#endif

void *mmap(void *start, size_t length, int prot, int flags, int fd,
           off_t offset);

void munmap(void *addr, size_t length);

int msync(void *addr, size_t length, int flags);

int flock(int fd, int operation);

#endif /* _WIN32 */

#endif /* PROFILE_INSTRPROFILING_WINDOWS_MMAP_H */
