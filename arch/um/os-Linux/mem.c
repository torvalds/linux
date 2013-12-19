/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <init.h>
#include <os.h>

/* Modified by which_tmpdir, which is called during early boot */
static char *default_tmpdir = "/tmp";

/*
 *  Modified when creating the physical memory file and when checking
 * the tmp filesystem for usability, both happening during early boot.
 */
static char *tempdir = NULL;

static void __init find_tempdir(void)
{
	const char *dirs[] = { "TMP", "TEMP", "TMPDIR", NULL };
	int i;
	char *dir = NULL;

	if (tempdir != NULL)
		/* We've already been called */
		return;
	for (i = 0; dirs[i]; i++) {
		dir = getenv(dirs[i]);
		if ((dir != NULL) && (*dir != '\0'))
			break;
	}
	if ((dir == NULL) || (*dir == '\0'))
		dir = default_tmpdir;

	tempdir = malloc(strlen(dir) + 2);
	if (tempdir == NULL) {
		fprintf(stderr, "Failed to malloc tempdir, "
			"errno = %d\n", errno);
		return;
	}
	strcpy(tempdir, dir);
	strcat(tempdir, "/");
}

/*
 * Remove bytes from the front of the buffer and refill it so that if there's a
 * partial string that we care about, it will be completed, and we can recognize
 * it.
 */
static int pop(int fd, char *buf, size_t size, size_t npop)
{
	ssize_t n;
	size_t len = strlen(&buf[npop]);

	memmove(buf, &buf[npop], len + 1);
	n = read(fd, &buf[len], size - len - 1);
	if (n < 0)
		return -errno;

	buf[len + n] = '\0';
	return 1;
}

/*
 * This will return 1, with the first character in buf being the
 * character following the next instance of c in the file.  This will
 * read the file as needed.  If there's an error, -errno is returned;
 * if the end of the file is reached, 0 is returned.
 */
static int next(int fd, char *buf, size_t size, char c)
{
	ssize_t n;
	char *ptr;

	while ((ptr = strchr(buf, c)) == NULL) {
		n = read(fd, buf, size - 1);
		if (n == 0)
			return 0;
		else if (n < 0)
			return -errno;

		buf[n] = '\0';
	}

	return pop(fd, buf, size, ptr - buf + 1);
}

/*
 * Decode an octal-escaped and space-terminated path of the form used by
 * /proc/mounts. May be used to decode a path in-place. "out" must be at least
 * as large as the input. The output is always null-terminated. "len" gets the
 * length of the output, excluding the trailing null. Returns 0 if a full path
 * was successfully decoded, otherwise an error.
 */
static int decode_path(const char *in, char *out, size_t *len)
{
	char *first = out;
	int c;
	int i;
	int ret = -EINVAL;
	while (1) {
		switch (*in) {
		case '\0':
			goto out;

		case ' ':
			ret = 0;
			goto out;

		case '\\':
			in++;
			c = 0;
			for (i = 0; i < 3; i++) {
				if (*in < '0' || *in > '7')
					goto out;
				c = (c << 3) | (*in++ - '0');
			}
			*(unsigned char *)out++ = (unsigned char) c;
			break;

		default:
			*out++ = *in++;
			break;
		}
	}

out:
	*out = '\0';
	*len = out - first;
	return ret;
}

/*
 * Computes the length of s when encoded with three-digit octal escape sequences
 * for the characters in chars.
 */
static size_t octal_encoded_length(const char *s, const char *chars)
{
	size_t len = strlen(s);
	while ((s = strpbrk(s, chars)) != NULL) {
		len += 3;
		s++;
	}

	return len;
}

enum {
	OUTCOME_NOTHING_MOUNTED,
	OUTCOME_TMPFS_MOUNT,
	OUTCOME_NON_TMPFS_MOUNT,
};

/* Read a line of /proc/mounts data looking for a tmpfs mount at "path". */
static int read_mount(int fd, char *buf, size_t bufsize, const char *path,
		      int *outcome)
{
	int found;
	int match;
	char *space;
	size_t len;

	enum {
		MATCH_NONE,
		MATCH_EXACT,
		MATCH_PARENT,
	};

	found = next(fd, buf, bufsize, ' ');
	if (found != 1)
		return found;

	/*
	 * If there's no following space in the buffer, then this path is
	 * truncated, so it can't be the one we're looking for.
	 */
	space = strchr(buf, ' ');
	if (space) {
		match = MATCH_NONE;
		if (!decode_path(buf, buf, &len)) {
			if (!strcmp(buf, path))
				match = MATCH_EXACT;
			else if (!strncmp(buf, path, len)
				 && (path[len] == '/' || !strcmp(buf, "/")))
				match = MATCH_PARENT;
		}

		found = pop(fd, buf, bufsize, space - buf + 1);
		if (found != 1)
			return found;

		switch (match) {
		case MATCH_EXACT:
			if (!strncmp(buf, "tmpfs", strlen("tmpfs")))
				*outcome = OUTCOME_TMPFS_MOUNT;
			else
				*outcome = OUTCOME_NON_TMPFS_MOUNT;
			break;

		case MATCH_PARENT:
			/* This mount obscures any previous ones. */
			*outcome = OUTCOME_NOTHING_MOUNTED;
			break;
		}
	}

	return next(fd, buf, bufsize, '\n');
}

/* which_tmpdir is called only during early boot */
static int checked_tmpdir = 0;

/*
 * Look for a tmpfs mounted at /dev/shm.  I couldn't find a cleaner
 * way to do this than to parse /proc/mounts.  statfs will return the
 * same filesystem magic number and fs id for both /dev and /dev/shm
 * when they are both tmpfs, so you can't tell if they are different
 * filesystems.  Also, there seems to be no other way of finding the
 * mount point of a filesystem from within it.
 *
 * If a /dev/shm tmpfs entry is found, then we switch to using it.
 * Otherwise, we stay with the default /tmp.
 */
static void which_tmpdir(void)
{
	int fd;
	int found;
	int outcome;
	char *path;
	char *buf;
	size_t bufsize;

	if (checked_tmpdir)
		return;

	checked_tmpdir = 1;

	printf("Checking for tmpfs mount on /dev/shm...");

	path = realpath("/dev/shm", NULL);
	if (!path) {
		printf("failed to check real path, errno = %d\n", errno);
		return;
	}
	printf("%s...", path);

	/*
	 * The buffer needs to be able to fit the full octal-escaped path, a
	 * space, and a trailing null in order to successfully decode it.
	 */
	bufsize = octal_encoded_length(path, " \t\n\\") + 2;

	if (bufsize < 128)
		bufsize = 128;

	buf = malloc(bufsize);
	if (!buf) {
		printf("malloc failed, errno = %d\n", errno);
		goto out;
	}
	buf[0] = '\0';

	fd = open("/proc/mounts", O_RDONLY);
	if (fd < 0) {
		printf("failed to open /proc/mounts, errno = %d\n", errno);
		goto out1;
	}

	outcome = OUTCOME_NOTHING_MOUNTED;
	while (1) {
		found = read_mount(fd, buf, bufsize, path, &outcome);
		if (found != 1)
			break;
	}

	if (found < 0) {
		printf("read returned errno %d\n", -found);
	} else {
		switch (outcome) {
		case OUTCOME_TMPFS_MOUNT:
			printf("OK\n");
			default_tmpdir = "/dev/shm";
			break;

		case OUTCOME_NON_TMPFS_MOUNT:
			printf("not tmpfs\n");
			break;

		default:
			printf("nothing mounted on /dev/shm\n");
			break;
		}
	}

	close(fd);
out1:
	free(buf);
out:
	free(path);
}

static int __init make_tempfile(const char *template, char **out_tempname,
				int do_unlink)
{
	char *tempname;
	int fd;

	which_tmpdir();
	tempname = malloc(MAXPATHLEN);
	if (tempname == NULL)
		return -1;

	find_tempdir();
	if ((tempdir == NULL) || (strlen(tempdir) >= MAXPATHLEN))
		goto out;

	if (template[0] != '/')
		strcpy(tempname, tempdir);
	else
		tempname[0] = '\0';
	strncat(tempname, template, MAXPATHLEN-1-strlen(tempname));
	fd = mkstemp(tempname);
	if (fd < 0) {
		fprintf(stderr, "open - cannot create %s: %s\n", tempname,
			strerror(errno));
		goto out;
	}
	if (do_unlink && (unlink(tempname) < 0)) {
		perror("unlink");
		goto close;
	}
	if (out_tempname) {
		*out_tempname = tempname;
	} else
		free(tempname);
	return fd;
close:
	close(fd);
out:
	free(tempname);
	return -1;
}

#define TEMPNAME_TEMPLATE "vm_file-XXXXXX"

static int __init create_tmp_file(unsigned long long len)
{
	int fd, err;
	char zero;

	fd = make_tempfile(TEMPNAME_TEMPLATE, NULL, 1);
	if (fd < 0)
		exit(1);

	err = fchmod(fd, 0777);
	if (err < 0) {
		perror("fchmod");
		exit(1);
	}

	/*
	 * Seek to len - 1 because writing a character there will
	 * increase the file size by one byte, to the desired length.
	 */
	if (lseek64(fd, len - 1, SEEK_SET) < 0) {
		perror("lseek64");
		exit(1);
	}

	zero = 0;

	err = write(fd, &zero, 1);
	if (err != 1) {
		perror("write");
		exit(1);
	}

	return fd;
}

int __init create_mem_file(unsigned long long len)
{
	int err, fd;

	fd = create_tmp_file(len);

	err = os_set_exec_close(fd);
	if (err < 0) {
		errno = -err;
		perror("exec_close");
	}
	return fd;
}


void __init check_tmpexec(void)
{
	void *addr;
	int err, fd = create_tmp_file(UM_KERN_PAGE_SIZE);

	addr = mmap(NULL, UM_KERN_PAGE_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0);
	printf("Checking PROT_EXEC mmap in %s...",tempdir);
	fflush(stdout);
	if (addr == MAP_FAILED) {
		err = errno;
		perror("failed");
		close(fd);
		if (err == EPERM)
			printf("%s must be not mounted noexec\n",tempdir);
		exit(1);
	}
	printf("OK\n");
	munmap(addr, UM_KERN_PAGE_SIZE);

	close(fd);
}
