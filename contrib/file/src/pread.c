#include "file.h"
#ifndef lint
FILE_RCSID("@(#)$File: pread.c,v 1.3 2014/09/15 19:11:25 christos Exp $")
#endif  /* lint */
#include <fcntl.h>
#include <unistd.h>

ssize_t
pread(int fd, void *buf, size_t len, off_t off) {
	off_t old;
	ssize_t rv;

	if ((old = lseek(fd, off, SEEK_SET)) == -1)
		return -1;

	if ((rv = read(fd, buf, len)) == -1)
		return -1;

	if (lseek(fd, old, SEEK_SET) == -1)
		return -1;

	return rv;
}
