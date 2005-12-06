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
#include "kern_util.h"
#include "user.h"
#include "user_util.h"
#include "mem_user.h"
#include "init.h"
#include "os.h"
#include "tempfile.h"
#include "kern_constants.h"

#include <sys/param.h>

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
		dir = "/tmp";

	tempdir = malloc(strlen(dir) + 2);
	if(tempdir == NULL){
		fprintf(stderr, "Failed to malloc tempdir, "
			"errno = %d\n", errno);
		return;
	}
	strcpy(tempdir, dir);
	strcat(tempdir, "/");
}

/*
 * This proc still used in tt-mode
 * (file: kernel/tt/ptproxy/proxy.c, proc: start_debugger).
 * So it isn't 'static' yet.
 */
int make_tempfile(const char *template, char **out_tempname, int do_unlink)
{
	char tempname[MAXPATHLEN];
	int fd;

	find_tempdir();
	if (*template != '/')
		strcpy(tempname, tempdir);
	else
		*tempname = 0;
	strcat(tempname, template);
	fd = mkstemp(tempname);
	if(fd < 0){
		fprintf(stderr, "open - cannot create %s: %s\n", tempname,
			strerror(errno));
		return -1;
	}
	if(do_unlink && (unlink(tempname) < 0)){
		perror("unlink");
		return -1;
	}
	if(out_tempname){
		*out_tempname = strdup(tempname);
		if(*out_tempname == NULL){
			perror("strdup");
			return -1;
		}
	}
	return(fd);
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

        if (lseek64(fd, len, SEEK_SET) < 0) {
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

static int create_anon_file(unsigned long long len)
{
	void *addr;
	int fd;

	fd = open("/dev/anon", O_RDWR);
	if(fd < 0) {
		perror("opening /dev/anon");
		exit(1);
	}

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if(addr == MAP_FAILED){
		perror("mapping physmem file");
		exit(1);
	}
	munmap(addr, len);

	return(fd);
}

extern int have_devanon;

int create_mem_file(unsigned long long len)
{
	int err, fd;

	if(have_devanon)
		fd = create_anon_file(len);
	else fd = create_tmp_file(len);

	err = os_set_exec_close(fd, 1);
	if(err < 0){
		errno = -err;
		perror("exec_close");
	}
	return(fd);
}
