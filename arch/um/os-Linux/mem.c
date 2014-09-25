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
#include <sys/vfs.h>
#include <linux/magic.h>
#include <init.h>
#include <os.h>

/* Set by make_tempfile() during early boot. */
static char *tempdir = NULL;

/* Check if dir is on tmpfs. Return 0 if yes, -1 if no or error. */
static int __init check_tmpfs(const char *dir)
{
	struct statfs st;

	printf("Checking if %s is on tmpfs...", dir);
	if (statfs(dir, &st) < 0) {
		printf("%s\n", strerror(errno));
	} else if (st.f_type != TMPFS_MAGIC) {
		printf("no\n");
	} else {
		printf("OK\n");
		return 0;
	}
	return -1;
}

/*
 * Choose the tempdir to use. We want something on tmpfs so that our memory is
 * not subject to the host's vm.dirty_ratio. If a tempdir is specified in the
 * environment, we use that even if it's not on tmpfs, but we warn the user.
 * Otherwise, we try common tmpfs locations, and if no tmpfs directory is found
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

	printf("Checking environment variables for a tempdir...");
	for (i = 0; vars[i]; i++) {
		dir = getenv(vars[i]);
		if ((dir != NULL) && (*dir != '\0')) {
			printf("%s\n", dir);
			if (check_tmpfs(dir) >= 0)
				goto done;
			else
				goto warn;
		}
	}
	printf("none found\n");

	for (i = 0; tmpfs_dirs[i]; i++) {
		dir = tmpfs_dirs[i];
		if (check_tmpfs(dir) >= 0)
			goto done;
	}

	dir = fallback_dir;
warn:
	printf("Warning: tempdir %s is not on tmpfs\n", dir);
done:
	/* Make a copy since getenv results may not remain valid forever. */
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
			fprintf(stderr, "Failed to choose tempdir: %s\n",
				strerror(errno));
			return -1;
		}
	}

	tempname = malloc(strlen(tempdir) + strlen(template) + 1);
	if (tempname == NULL)
		return -1;

	strcpy(tempname, tempdir);
	strcat(tempname, template);
	fd = mkstemp(tempname);
	if (fd < 0) {
		fprintf(stderr, "open - cannot create %s: %s\n", tempname,
			strerror(errno));
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
	printf("Checking PROT_EXEC mmap in %s...", tempdir);
	if (addr == MAP_FAILED) {
		err = errno;
		printf("%s\n", strerror(err));
		close(fd);
		if (err == EPERM)
			printf("%s must be not mounted noexec\n", tempdir);
		exit(1);
	}
	printf("OK\n");
	munmap(addr, UM_KERN_PAGE_SIZE);

	close(fd);
}
