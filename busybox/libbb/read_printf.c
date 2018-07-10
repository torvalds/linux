/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"


/* Suppose that you are a shell. You start child processes.
 * They work and eventually exit. You want to get user input.
 * You read stdin. But what happens if last child switched
 * its stdin into O_NONBLOCK mode?
 *
 * *** SURPRISE! It will affect the parent too! ***
 * *** BIG SURPRISE! It stays even after child exits! ***
 *
 * This is a design bug in UNIX API.
 *      fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
 * will set nonblocking mode not only on _your_ stdin, but
 * also on stdin of your parent, etc.
 *
 * In general,
 *      fd2 = dup(fd1);
 *      fcntl(fd2, F_SETFL, fcntl(fd2, F_GETFL) | O_NONBLOCK);
 * sets both fd1 and fd2 to O_NONBLOCK. This includes cases
 * where duping is done implicitly by fork() etc.
 *
 * We need
 *      fcntl(fd2, F_SETFD, fcntl(fd2, F_GETFD) | O_NONBLOCK);
 * (note SETFD, not SETFL!) but such thing doesn't exist.
 *
 * Alternatively, we need nonblocking_read(fd, ...) which doesn't
 * require O_NONBLOCK dance at all. Actually, it exists:
 *      n = recv(fd, buf, len, MSG_DONTWAIT);
 *      "MSG_DONTWAIT:
 *      Enables non-blocking operation; if the operation
 *      would block, EAGAIN is returned."
 * but recv() works only for sockets!
 *
 * So far I don't see any good solution, I can only propose
 * that affected readers should be careful and use this routine,
 * which detects EAGAIN and uses poll() to wait on the fd.
 * Thankfully, poll() doesn't care about O_NONBLOCK flag.
 */
ssize_t FAST_FUNC nonblock_immune_read(int fd, void *buf, size_t count)
{
	struct pollfd pfd[1];
	ssize_t n;

	while (1) {
		n = safe_read(fd, buf, count);
		if (n >= 0 || errno != EAGAIN)
			return n;
		/* fd is in O_NONBLOCK mode. Wait using poll and repeat */
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		/* note: safe_poll pulls in printf */
		safe_poll(pfd, 1, -1);
	}
}

// Reads one line a-la fgets (but doesn't save terminating '\n').
// Reads byte-by-byte. Useful when it is important to not read ahead.
// Bytes are appended to pfx (which must be malloced, or NULL).
char* FAST_FUNC xmalloc_reads(int fd, size_t *maxsz_p)
{
	char *p;
	char *buf = NULL;
	size_t sz = 0;
	size_t maxsz = maxsz_p ? *maxsz_p : (INT_MAX - 4095);

	goto jump_in;

	while (sz < maxsz) {
		if ((size_t)(p - buf) == sz) {
 jump_in:
			buf = xrealloc(buf, sz + 128);
			p = buf + sz;
			sz += 128;
		}
		if (nonblock_immune_read(fd, p, 1) != 1) {
			/* EOF/error */
			if (p == buf) { /* we read nothing */
				free(buf);
				return NULL;
			}
			break;
		}
		if (*p == '\n')
			break;
		p++;
	}
	*p = '\0';
	if (maxsz_p)
		*maxsz_p  = p - buf;
	p++;
	return xrealloc(buf, p - buf);
}

// Read (potentially big) files in one go. File size is estimated
// by stat. Extra '\0' byte is appended.
void* FAST_FUNC xmalloc_read(int fd, size_t *maxsz_p)
{
	char *buf;
	size_t size, rd_size, total;
	size_t to_read;
	struct stat st;

	to_read = maxsz_p ? *maxsz_p : (INT_MAX - 4095); /* max to read */

	/* Estimate file size */
	st.st_size = 0; /* in case fstat fails, assume 0 */
	fstat(fd, &st);
	/* /proc/N/stat files report st_size 0 */
	/* In order to make such files readable, we add small const */
	size = (st.st_size | 0x3ff) + 1;

	total = 0;
	buf = NULL;
	while (1) {
		if (to_read < size)
			size = to_read;
		buf = xrealloc(buf, total + size + 1);
		rd_size = full_read(fd, buf + total, size);
		if ((ssize_t)rd_size == (ssize_t)(-1)) { /* error */
			free(buf);
			return NULL;
		}
		total += rd_size;
		if (rd_size < size) /* EOF */
			break;
		if (to_read <= rd_size)
			break;
		to_read -= rd_size;
		/* grow by 1/8, but in [1k..64k] bounds */
		size = ((total / 8) | 0x3ff) + 1;
		if (size > 64*1024)
			size = 64*1024;
	}
	buf = xrealloc(buf, total + 1);
	buf[total] = '\0';

	if (maxsz_p)
		*maxsz_p = total;
	return buf;
}

#ifdef USING_LSEEK_TO_GET_SIZE
/* Alternatively, file size can be obtained by lseek to the end.
 * The code is slightly bigger. Retained in case fstat approach
 * will not work for some weird cases (/proc, block devices, etc).
 * (NB: lseek also can fail to work for some weird files) */

// Read (potentially big) files in one go. File size is estimated by
// lseek to end.
void* FAST_FUNC xmalloc_open_read_close(const char *filename, size_t *maxsz_p)
{
	char *buf;
	size_t size;
	int fd;
	off_t len;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	/* /proc/N/stat files report len 0 here */
	/* In order to make such files readable, we add small const */
	size = 0x3ff; /* read only 1k on unseekable files */
	len = lseek(fd, 0, SEEK_END) | 0x3ff; /* + up to 1k */
	if (len != (off_t)-1) {
		xlseek(fd, 0, SEEK_SET);
		size = maxsz_p ? *maxsz_p : (INT_MAX - 4095);
		if (len < size)
			size = len;
	}

	buf = xmalloc(size + 1);
	size = read_close(fd, buf, size);
	if ((ssize_t)size < 0) {
		free(buf);
		return NULL;
	}
	buf = xrealloc(buf, size + 1);
	buf[size] = '\0';

	if (maxsz_p)
		*maxsz_p = size;
	return buf;
}
#endif

// Read (potentially big) files in one go. File size is estimated
// by stat.
void* FAST_FUNC xmalloc_open_read_close(const char *filename, size_t *maxsz_p)
{
	char *buf;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	buf = xmalloc_read(fd, maxsz_p);
	close(fd);
	return buf;
}

/* Die with an error message if we can't read the entire buffer. */
void FAST_FUNC xread(int fd, void *buf, size_t count)
{
	if (count) {
		ssize_t size = full_read(fd, buf, count);
		if ((size_t)size != count)
			bb_error_msg_and_die("short read");
	}
}

/* Die with an error message if we can't read one character. */
unsigned char FAST_FUNC xread_char(int fd)
{
	char tmp;
	xread(fd, &tmp, 1);
	return tmp;
}

void* FAST_FUNC xmalloc_xopen_read_close(const char *filename, size_t *maxsz_p)
{
	void *buf = xmalloc_open_read_close(filename, maxsz_p);
	if (!buf)
		bb_perror_msg_and_die("can't read '%s'", filename);
	return buf;
}
