/*
 * Copyright (c) 1995 - 2000, 2002, 2004, 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kafs_locl.h"

struct procdata {
    unsigned long param4;
    unsigned long param3;
    unsigned long param2;
    unsigned long param1;
    unsigned long syscall;
};
#define VIOC_SYSCALL_PROC _IOW('C', 1, void *)

struct devdata {
    unsigned long syscall;
    unsigned long param1;
    unsigned long param2;
    unsigned long param3;
    unsigned long param4;
    unsigned long param5;
    unsigned long param6;
    unsigned long retval;
};
#ifdef _IOWR
#define VIOC_SYSCALL_DEV _IOWR('C', 2, struct devdata)
#define VIOC_SYSCALL_DEV_OPENAFS _IOWR('C', 1, struct devdata)
#endif


int _kafs_debug; /* this should be done in a better way */

#define UNKNOWN_ENTRY_POINT	(-1)
#define NO_ENTRY_POINT		0
#define SINGLE_ENTRY_POINT	1
#define MULTIPLE_ENTRY_POINT	2
#define SINGLE_ENTRY_POINT2	3
#define SINGLE_ENTRY_POINT3	4
#define LINUX_PROC_POINT	5
#define AIX_ENTRY_POINTS	6
#define MACOS_DEV_POINT		7

static int afs_entry_point = UNKNOWN_ENTRY_POINT;
static int afs_syscalls[2];
static char *afs_ioctlpath;
static unsigned long afs_ioctlnum;

/* Magic to get AIX syscalls to work */
#ifdef _AIX

static int (*Pioctl)(char*, int, struct ViceIoctl*, int);
static int (*Setpag)(void);

#include "dlfcn.h"

/*
 *
 */

static int
try_aix(void)
{
#ifdef STATIC_AFS_SYSCALLS
    Pioctl = aix_pioctl;
    Setpag = aix_setpag;
#else
    void *ptr;
    char path[MaxPathLen], *p;
    /*
     * If we are root or running setuid don't trust AFSLIBPATH!
     */
    if (getuid() != 0 && !issuid() && (p = getenv("AFSLIBPATH")) != NULL)
	strlcpy(path, p, sizeof(path));
    else
	snprintf(path, sizeof(path), "%s/afslib.so", LIBDIR);

    ptr = dlopen(path, RTLD_NOW);
    if(ptr == NULL) {
	if(_kafs_debug) {
	    if(errno == ENOEXEC && (p = dlerror()) != NULL)
		fprintf(stderr, "dlopen(%s): %s\n", path, p);
	    else if (errno != ENOENT)
		fprintf(stderr, "dlopen(%s): %s\n", path, strerror(errno));
	}
	return 1;
    }
    Setpag = (int (*)(void))dlsym(ptr, "aix_setpag");
    Pioctl = (int (*)(char*, int,
		      struct ViceIoctl*, int))dlsym(ptr, "aix_pioctl");
#endif
    afs_entry_point = AIX_ENTRY_POINTS;
    return 0;
}
#endif /* _AIX */

/*
 * This probably only works under Solaris and could get confused if
 * there's a /etc/name_to_sysnum file.
 */

#if defined(AFS_SYSCALL) || defined(AFS_SYSCALL2) || defined(AFS_SYSCALL3)

#define _PATH_ETC_NAME_TO_SYSNUM "/etc/name_to_sysnum"

static int
map_syscall_name_to_number (const char *str, int *res)
{
    FILE *f;
    char buf[256];
    size_t str_len = strlen (str);

    f = fopen (_PATH_ETC_NAME_TO_SYSNUM, "r");
    if (f == NULL)
	return -1;
    while (fgets (buf, sizeof(buf), f) != NULL) {
	if (buf[0] == '#')
	    continue;

	if (strncmp (str, buf, str_len) == 0) {
	    char *begptr = buf + str_len;
	    char *endptr;
	    long val = strtol (begptr, &endptr, 0);

	    if (val != 0 && endptr != begptr) {
		fclose (f);
		*res = val;
		return 0;
	    }
	}
    }
    fclose (f);
    return -1;
}
#endif

static int
try_ioctlpath(const char *path, unsigned long ioctlnum, int entrypoint)
{
    int fd, ret, saved_errno;

    fd = open(path, O_RDWR);
    if (fd < 0)
	return 1;
    switch (entrypoint) {
    case LINUX_PROC_POINT: {
	struct procdata data = { 0, 0, 0, 0, AFSCALL_PIOCTL };
	data.param2 = (unsigned long)VIOCGETTOK;
	ret = ioctl(fd, ioctlnum, &data);
	break;
    }
    case MACOS_DEV_POINT: {
	struct devdata data = { AFSCALL_PIOCTL, 0, 0, 0, 0, 0, 0, 0 };
	data.param2 = (unsigned long)VIOCGETTOK;
	ret = ioctl(fd, ioctlnum, &data);
	break;
    }
    default:
	abort();
    }
    saved_errno = errno;
    close(fd);
    /*
     * Be quite liberal in what error are ok, the first is the one
     * that should trigger given that params is NULL.
     */
    if (ret &&
	(saved_errno != EFAULT &&
	 saved_errno != EDOM &&
	 saved_errno != ENOTCONN))
	return 1;
    afs_ioctlnum = ioctlnum;
    afs_ioctlpath = strdup(path);
    if (afs_ioctlpath == NULL)
	return 1;
    afs_entry_point = entrypoint;
    return 0;
}

static int
do_ioctl(void *data)
{
    int fd, ret, saved_errno;
    fd = open(afs_ioctlpath, O_RDWR);
    if (fd < 0) {
	errno = EINVAL;
	return -1;
    }
    ret = ioctl(fd, afs_ioctlnum, data);
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return ret;
}

int
k_pioctl(char *a_path,
	 int o_opcode,
	 struct ViceIoctl *a_paramsP,
	 int a_followSymlinks)
{
#ifndef NO_AFS
    switch(afs_entry_point){
#if defined(AFS_SYSCALL) || defined(AFS_SYSCALL2) || defined(AFS_SYSCALL3)
    case SINGLE_ENTRY_POINT:
    case SINGLE_ENTRY_POINT2:
    case SINGLE_ENTRY_POINT3:
	return syscall(afs_syscalls[0], AFSCALL_PIOCTL,
		       a_path, o_opcode, a_paramsP, a_followSymlinks);
#endif
#if defined(AFS_PIOCTL)
    case MULTIPLE_ENTRY_POINT:
	return syscall(afs_syscalls[0],
		       a_path, o_opcode, a_paramsP, a_followSymlinks);
#endif
    case LINUX_PROC_POINT: {
	struct procdata data = { 0, 0, 0, 0, AFSCALL_PIOCTL };
	data.param1 = (unsigned long)a_path;
	data.param2 = (unsigned long)o_opcode;
	data.param3 = (unsigned long)a_paramsP;
	data.param4 = (unsigned long)a_followSymlinks;
	return do_ioctl(&data);
    }
    case MACOS_DEV_POINT: {
	struct devdata data = { AFSCALL_PIOCTL, 0, 0, 0, 0, 0, 0, 0 };
	int ret;

	data.param1 = (unsigned long)a_path;
	data.param2 = (unsigned long)o_opcode;
	data.param3 = (unsigned long)a_paramsP;
	data.param4 = (unsigned long)a_followSymlinks;

	ret = do_ioctl(&data);
	if (ret)
	    return ret;

	return data.retval;
    }
#ifdef _AIX
    case AIX_ENTRY_POINTS:
	return Pioctl(a_path, o_opcode, a_paramsP, a_followSymlinks);
#endif
    }
    errno = ENOSYS;
#ifdef SIGSYS
    kill(getpid(), SIGSYS);	/* You lose! */
#endif
#endif /* NO_AFS */
    return -1;
}

int
k_afs_cell_of_file(const char *path, char *cell, int len)
{
    struct ViceIoctl parms;
    parms.in = NULL;
    parms.in_size = 0;
    parms.out = cell;
    parms.out_size = len;
    return k_pioctl(rk_UNCONST(path), VIOC_FILE_CELL_NAME, &parms, 1);
}

int
k_unlog(void)
{
    struct ViceIoctl parms;
    memset(&parms, 0, sizeof(parms));
    return k_pioctl(0, VIOCUNLOG, &parms, 0);
}

int
k_setpag(void)
{
#ifndef NO_AFS
    switch(afs_entry_point){
#if defined(AFS_SYSCALL) || defined(AFS_SYSCALL2) || defined(AFS_SYSCALL3)
    case SINGLE_ENTRY_POINT:
    case SINGLE_ENTRY_POINT2:
    case SINGLE_ENTRY_POINT3:
	return syscall(afs_syscalls[0], AFSCALL_SETPAG);
#endif
#if defined(AFS_PIOCTL)
    case MULTIPLE_ENTRY_POINT:
	return syscall(afs_syscalls[1]);
#endif
    case LINUX_PROC_POINT: {
	struct procdata data = { 0, 0, 0, 0, AFSCALL_SETPAG };
	return do_ioctl(&data);
    }
    case MACOS_DEV_POINT: {
	struct devdata data = { AFSCALL_SETPAG, 0, 0, 0, 0, 0, 0, 0 };
	int ret = do_ioctl(&data);
	if (ret)
	    return ret;
	return data.retval;
     }
#ifdef _AIX
    case AIX_ENTRY_POINTS:
	return Setpag();
#endif
    }

    errno = ENOSYS;
#ifdef SIGSYS
    kill(getpid(), SIGSYS);	/* You lose! */
#endif
#endif /* NO_AFS */
    return -1;
}

static jmp_buf catch_SIGSYS;

#ifdef SIGSYS

static RETSIGTYPE
SIGSYS_handler(int sig)
{
    errno = 0;
    signal(SIGSYS, SIGSYS_handler); /* Need to reinstall handler on SYSV */
    longjmp(catch_SIGSYS, 1);
}

#endif

/*
 * Try to see if `syscall' is a pioctl.  Return 0 iff succesful.
 */

#if defined(AFS_SYSCALL) || defined(AFS_SYSCALL2) || defined(AFS_SYSCALL3)
static int
try_one (int syscall_num)
{
    struct ViceIoctl parms;
    memset(&parms, 0, sizeof(parms));

    if (setjmp(catch_SIGSYS) == 0) {
	syscall(syscall_num, AFSCALL_PIOCTL,
		0, VIOCSETTOK, &parms, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (errno == EINVAL) {
	    afs_entry_point = SINGLE_ENTRY_POINT;
	    afs_syscalls[0] = syscall_num;
	    return 0;
	}
    }
    return 1;
}
#endif

/*
 * Try to see if `syscall_pioctl' is a pioctl syscall.  Return 0 iff
 * succesful.
 *
 */

#ifdef AFS_PIOCTL
static int
try_two (int syscall_pioctl, int syscall_setpag)
{
    struct ViceIoctl parms;
    memset(&parms, 0, sizeof(parms));

    if (setjmp(catch_SIGSYS) == 0) {
	syscall(syscall_pioctl,
		0, VIOCSETTOK, &parms, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	if (errno == EINVAL) {
	    afs_entry_point = MULTIPLE_ENTRY_POINT;
	    afs_syscalls[0] = syscall_pioctl;
	    afs_syscalls[1] = syscall_setpag;
	    return 0;
	}
    }
    return 1;
}
#endif

int
k_hasafs(void)
{
#if !defined(NO_AFS) && defined(SIGSYS)
    RETSIGTYPE (*saved_func)(int);
#endif
    int saved_errno, ret;
    char *env = NULL;

    if (!issuid())
	env = getenv ("AFS_SYSCALL");

    /*
     * Already checked presence of AFS syscalls?
     */
    if (afs_entry_point != UNKNOWN_ENTRY_POINT)
	return afs_entry_point != NO_ENTRY_POINT;

    /*
     * Probe kernel for AFS specific syscalls,
     * they (currently) come in two flavors.
     * If the syscall is absent we recive a SIGSYS.
     */
    afs_entry_point = NO_ENTRY_POINT;

    saved_errno = errno;
#ifndef NO_AFS
#ifdef SIGSYS
    saved_func = signal(SIGSYS, SIGSYS_handler);
#endif
    if (env && strstr(env, "..") == NULL) {

	if (strncmp("/proc/", env, 6) == 0) {
	    if (try_ioctlpath(env, VIOC_SYSCALL_PROC, LINUX_PROC_POINT) == 0)
		goto done;
	}
	if (strncmp("/dev/", env, 5) == 0) {
#ifdef VIOC_SYSCALL_DEV
	    if (try_ioctlpath(env, VIOC_SYSCALL_DEV, MACOS_DEV_POINT) == 0)
		goto done;
#endif
#ifdef VIOC_SYSCALL_DEV_OPENAFS
	    if (try_ioctlpath(env,VIOC_SYSCALL_DEV_OPENAFS,MACOS_DEV_POINT) ==0)
		goto done;
#endif
	}
    }

    ret = try_ioctlpath("/proc/fs/openafs/afs_ioctl",
			VIOC_SYSCALL_PROC, LINUX_PROC_POINT);
    if (ret == 0)
	goto done;
    ret = try_ioctlpath("/proc/fs/nnpfs/afs_ioctl",
			VIOC_SYSCALL_PROC, LINUX_PROC_POINT);
    if (ret == 0)
	goto done;

#ifdef VIOC_SYSCALL_DEV_OPENAFS
    ret = try_ioctlpath("/dev/openafs_ioctl",
			VIOC_SYSCALL_DEV_OPENAFS, MACOS_DEV_POINT);
    if (ret == 0)
	goto done;
#endif
#ifdef VIOC_SYSCALL_DEV
    ret = try_ioctlpath("/dev/nnpfs_ioctl", VIOC_SYSCALL_DEV, MACOS_DEV_POINT);
    if (ret == 0)
	goto done;
#endif

#if defined(AFS_SYSCALL) || defined(AFS_SYSCALL2) || defined(AFS_SYSCALL3)
    {
	int tmp;

	if (env != NULL) {
	    if (sscanf (env, "%d", &tmp) == 1) {
		if (try_one (tmp) == 0)
		    goto done;
	    } else {
		char *end = NULL;
		char *p;
		char *s = strdup (env);

		if (s != NULL) {
		    for (p = strtok_r (s, ",", &end);
			 p != NULL;
			 p = strtok_r (NULL, ",", &end)) {
			if (map_syscall_name_to_number (p, &tmp) == 0)
			    if (try_one (tmp) == 0) {
				free (s);
				goto done;
			    }
		    }
		    free (s);
		}
	    }
	}
    }
#endif /* AFS_SYSCALL || AFS_SYSCALL2 || AFS_SYSCALL3 */

#ifdef AFS_SYSCALL
    if (try_one (AFS_SYSCALL) == 0)
	goto done;
#endif /* AFS_SYSCALL */

#ifdef AFS_PIOCTL
    {
	int tmp[2];

	if (env != NULL && sscanf (env, "%d%d", &tmp[0], &tmp[1]) == 2)
	    if (try_two (tmp[0], tmp[1]) == 2)
		goto done;
    }
#endif /* AFS_PIOCTL */

#ifdef AFS_PIOCTL
    if (try_two (AFS_PIOCTL, AFS_SETPAG) == 0)
	goto done;
#endif /* AFS_PIOCTL */

#ifdef AFS_SYSCALL2
    if (try_one (AFS_SYSCALL2) == 0)
	goto done;
#endif /* AFS_SYSCALL2 */

#ifdef AFS_SYSCALL3
    if (try_one (AFS_SYSCALL3) == 0)
	goto done;
#endif /* AFS_SYSCALL3 */

#ifdef _AIX
#if 0
    if (env != NULL) {
	char *pos = NULL;
	char *pioctl_name;
	char *setpag_name;

	pioctl_name = strtok_r (env, ", \t", &pos);
	if (pioctl_name != NULL) {
	    setpag_name = strtok_r (NULL, ", \t", &pos);
	    if (setpag_name != NULL)
		if (try_aix (pioctl_name, setpag_name) == 0)
		    goto done;
	}
    }
#endif

    if(try_aix() == 0)
	goto done;
#endif


done:
#ifdef SIGSYS
    signal(SIGSYS, saved_func);
#endif
#endif /* NO_AFS */
    errno = saved_errno;
    return afs_entry_point != NO_ENTRY_POINT;
}

int
k_hasafs_recheck(void)
{
    afs_entry_point = UNKNOWN_ENTRY_POINT;
    return k_hasafs();
}
