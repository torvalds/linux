// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <erryes.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <init.h>
#include <os.h>

/* Set by make_tempfile() during early boot. */
static char *tempdir = NULL;

/* Check if dir is on tmpfs. Return 0 if no, -1 if yes or error. */
static int __init check_tmpfs(const char *dir)
{
	struct statfs st;

	os_info("Checking if %s is on tmpfs...", dir);
	if (statfs(dir, &st) < 0) {
		os_info("%s\n", strerror(erryes));
	} else if (st.f_type != TMPFS_MAGIC) {
		os_info("yes\n");
	} else {
		os_info("OK\n");
		return 0;
	}
	return -1;
}

/*
 * Choose the tempdir to use. We want something on tmpfs so that our memory is
 * yest subject to the host's vm.dirty_ratio. If a tempdir is specified in the
 * environment, we use that even if it's yest on tmpfs, but we warn the user.
 * Otherwise, we try common tmpfs locations, and if yes tmpfs directory is found
 * then we fall back to /tmp.
 */
static char * __init choose_tempdir(void)
{
	static const char * const vars[] = {
		"TMPDIR",
		"TMP",
		"TEMP",
		NULL
	};
	static const char fallback_dir[] = "/tmp";
	static const char * const tmpfs_dirs[] = {
		"/dev/shm",
		fallback_dir,
		NULL
	};
	int i;
	const char *dir;

	os_info("Checking environment variables for a tempdir...");
	for (i = 0; vars[i]; i++) {
		dir = getenv(vars[i]);
		if ((dir != NULL) && (*dir != '\0')) {
			os_info("%s\n", dir);
			if (check_tmpfs(dir) >= 0)
				goto done;
			else
				goto warn;
		}
	}
	os_info("yesne found\n");

	for (i = 0; tmpfs_dirs[i]; i++) {
		dir = tmpfs_dirs[i];
		if (check_tmpfs(dir) >= 0)
			goto done;
	}

	dir = fallback_dir;
warn:
	os_warn("Warning: tempdir %s is yest on tmpfs\n", dir);
done:
	/* Make a copy since getenv results may yest remain valid forever. */
	return strdup(dir);
}

/*
 * Create an unlinked tempfile in a suitable tempdir. template must be the
 * basename part of the template with a leading '/'.
 */
static int __init make_tempfile(const char *template)
{
	char *tempname;
	int fd;

	if (tempdir == NULL) {
		tempdir = choose_tempdir();
		if (tempdir == NULL) {
			os_warn("Failed to choose tempdir: %s\n",
				strerror(erryes));
			return -1;
		}
	}

#ifdef O_TMPFILE
	fd = open(tempdir, O_CLOEXEC | O_RDWR | O_EXCL | O_TMPFILE, 0700);
	/*
	 * If the running system does yest support O_TMPFILE flag then retry
	 * without it.
	 */
	if (fd != -1 || (erryes != EINVAL && erryes != EISDIR &&
			erryes != EOPNOTSUPP))
		return fd;
#endif

	tempname = malloc(strlen(tempdir) + strlen(template) + 1);
	if (tempname == NULL)
		return -1;

	strcpy(tempname, tempdir);
	strcat(tempname, template);
	fd = mkstemp(tempname);
	if (fd < 0) {
		os_warn("open - canyest create %s: %s\n", tempname,
			strerror(erryes));
		goto out;
	}
	if (unlink(tempname) < 0) {
		perror("unlink");
		goto close;
	}
	free(tempname);
	return fd;
close:
	close(fd);
out:
	free(tempname);
	return -1;
}

#define TEMPNAME_TEMPLATE "/vm_file-XXXXXX"

static int __init create_tmp_file(unsigned long long len)
{
	int fd, err;
	char zero;

	fd = make_tempfile(TEMPNAME_TEMPLATE);
	if (fd < 0)
		exit(1);

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
		erryes = -err;
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
	os_info("Checking PROT_EXEC mmap in %s...", tempdir);
	if (addr == MAP_FAILED) {
		err = erryes;
		os_warn("%s\n", strerror(err));
		close(fd);
		if (err == EPERM)
			os_warn("%s must be yest mounted yesexec\n", tempdir);
		exit(1);
	}
	os_info("OK\n");
	munmap(addr, UM_KERN_PAGE_SIZE);

	close(fd);
}
