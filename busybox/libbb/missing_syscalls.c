/*
 * Copyright 2012, Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//kbuild:lib-y += missing_syscalls.o

#include "libbb.h"

#if defined(ANDROID) || defined(__ANDROID__)
/*# include <linux/timex.h> - for struct timex, but may collide with <time.h> */
# include <sys/syscall.h>
pid_t getsid(pid_t pid)
{
	return syscall(__NR_getsid, pid);
}

int stime(const time_t *t)
{
	struct timeval tv;
	tv.tv_sec = *t;
	tv.tv_usec = 0;
	return settimeofday(&tv, NULL);
}

int sethostname(const char *name, size_t len)
{
	return syscall(__NR_sethostname, name, len);
}

struct timex;
int adjtimex(struct timex *buf)
{
	return syscall(__NR_adjtimex, buf);
}

int pivot_root(const char *new_root, const char *put_old)
{
	return syscall(__NR_pivot_root, new_root, put_old);
}

# if __ANDROID_API__ < 21
int tcdrain(int fd)
{
	return ioctl(fd, TCSBRK, 1);
}
# endif
#endif
