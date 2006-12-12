#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include "kern_util.h"
#include "user.h"
#include "user_util.h"
#include "mem_user.h"
#include "init.h"
#include "os.h"
#include "tempfile.h"
#include "kern_constants.h"

#include <sys/param.h>

static char *default_tmpdir = "/tmp";
static char *tempdir = NULL;

static void __init find_tempdir(void)
{
	char *dirs[] = { "TMP", "TEMP", "TMPDIR", NULL };
	int i;
	char *dir = NULL;

	if(tempdir != NULL) return;	/* We've already been called */
	for(i = 0; dirs[i]; i++){
		dir = getenv(dirs[i]);
		if((dir != NULL) && (*dir != '\0'))
			break;
	}
	if((dir == NULL) || (*dir == '\0'))
		dir = default_tmpdir;

	tempdir = malloc(strlen(dir) + 2);
	if(tempdir == NULL){
		fprintf(stderr, "Failed to malloc tempdir, "
			"errno = %d\n", errno);
		return;
	}
	strcpy(tempdir, dir);
	strcat(tempdir, "/");
}

/* This will return 1, with the first character in buf being the
 * character following the next instance of c in the file.  This will
 * read the file as needed.  If there's an error, -errno is returned;
 * if the end of the file is reached, 0 is returned.
 */
static int next(int fd, char *buf, int size, char c)
{
	int n, len;
	char *ptr;

	while((ptr = strchr(buf, c)) == NULL){
		n = read(fd, buf, size - 1);
		if(n == 0)
			return 0;
		else if(n < 0)
			return -errno;

		buf[n] = '\0';
	}

	ptr++;
	len = strlen(ptr);
	memmove(buf, ptr, len + 1);

	/* Refill the buffer so that if there's a partial string that we care
	 * about, it will be completed, and we can recognize it.
	 */
	n = read(fd, &buf[len], size - len - 1);
	if(n < 0)
		return -errno;

	buf[len + n] = '\0';
	return 1;
}

static int checked_tmpdir = 0;

/* Look for a tmpfs mounted at /dev/shm.  I couldn't find a cleaner
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
	int fd, found;
	char buf[128] = { '\0' };

	if(checked_tmpdir)
		return;

	checked_tmpdir = 1;

	printf("Checking for tmpfs mount on /dev/shm...");

	fd = open("/proc/mounts", O_RDONLY);
	if(fd < 0){
		printf("failed to open /proc/mounts, errno = %d\n", errno);
		return;
	}

	while(1){
		found = next(fd, buf, ARRAY_SIZE(buf), ' ');
		if(found != 1)
			break;

		if(!strncmp(buf, "/dev/shm", strlen("/dev/shm")))
			goto found;

		found = next(fd, buf, ARRAY_SIZE(buf), '\n');
		if(found != 1)
			break;
	}

err:
	if(found == 0)
		printf("nothing mounted on /dev/shm\n");
	else if(found < 0)
		printf("read returned errno %d\n", -found);

out:
	close(fd);

	return;

found:
	found = next(fd, buf, ARRAY_SIZE(buf), ' ');
	if(found != 1)
		goto err;

	if(strncmp(buf, "tmpfs", strlen("tmpfs"))){
		printf("not tmpfs\n");
		goto out;
	}

	printf("OK\n");
	default_tmpdir = "/dev/shm";
	goto out;
}

/*
 * This proc still used in tt-mode
 * (file: kernel/tt/ptproxy/proxy.c, proc: start_debugger).
 * So it isn't 'static' yet.
 */
int make_tempfile(const char *template, char **out_tempname, int do_unlink)
{
	char *tempname;
	int fd;

	which_tmpdir();
	tempname = malloc(MAXPATHLEN);

	find_tempdir();
	if (template[0] != '/')
		strcpy(tempname, tempdir);
	else
		tempname[0] = '\0';
	strcat(tempname, template);
	fd = mkstemp(tempname);
	if(fd < 0){
		fprintf(stderr, "open - cannot create %s: %s\n", tempname,
			strerror(errno));
		goto out;
	}
	if(do_unlink && (unlink(tempname) < 0)){
		perror("unlink");
		goto out;
	}
	if(out_tempname){
		*out_tempname = tempname;
	} else {
		free(tempname);
	}
	return(fd);
out:
	free(tempname);
	return -1;
}

#define TEMPNAME_TEMPLATE "vm_file-XXXXXX"

/*
 * This proc is used in start_up.c
 * So it isn't 'static'.
 */
int create_tmp_file(unsigned long long len)
{
	int fd, err;
	char zero;

	fd = make_tempfile(TEMPNAME_TEMPLATE, NULL, 1);
	if(fd < 0) {
		exit(1);
	}

	err = fchmod(fd, 0777);
	if(err < 0){
		perror("os_mode_fd");
		exit(1);
	}

	/* Seek to len - 1 because writing a character there will
	 * increase the file size by one byte, to the desired length.
	 */
	if (lseek64(fd, len - 1, SEEK_SET) < 0) {
		perror("os_seek_file");
		exit(1);
	}

	zero = 0;

	err = os_write_file(fd, &zero, 1);
	if(err != 1){
		errno = -err;
		perror("os_write_file");
		exit(1);
	}

	return(fd);
}

int create_mem_file(unsigned long long len)
{
	int err, fd;

	fd = create_tmp_file(len);

	err = os_set_exec_close(fd, 1);
	if(err < 0){
		errno = -err;
		perror("exec_close");
	}
	return(fd);
}


void check_tmpexec(void)
{
	void *addr;
	int err, fd = create_tmp_file(UM_KERN_PAGE_SIZE);

	addr = mmap(NULL, UM_KERN_PAGE_SIZE,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0);
	printf("Checking PROT_EXEC mmap in %s...",tempdir);
	fflush(stdout);
	if(addr == MAP_FAILED){
		err = errno;
		perror("failed");
		if(err == EPERM)
			printf("%s must be not mounted noexec\n",tempdir);
		exit(1);
	}
	printf("OK\n");
	munmap(addr, UM_KERN_PAGE_SIZE);

	close(fd);
}
