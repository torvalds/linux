/* vi: set sw=4 ts=4: */
/*
 * Mini lsof implementation for busybox
 *
 * Copyright (C) 2012 by Sven Oliver 'SvOlli' Moll <svolli@svolli.de>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config LSOF
//config:	bool "lsof (3.6 kb)"
//config:	default y
//config:	help
//config:	Show open files in the format of:
//config:	PID <TAB> /path/to/executable <TAB> /path/to/opened/file

//applet:IF_LSOF(APPLET(lsof, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_LSOF) += lsof.o

//usage:#define lsof_trivial_usage
//usage:       ""
//usage:#define lsof_full_usage "\n\n"
//usage:       "Show all open files"

#include "libbb.h"

/*
 * Examples of "standard" lsof output:
 *
 * COMMAND    PID USER   FD   TYPE             DEVICE     SIZE       NODE NAME
 * init         1 root  cwd    DIR                8,5     4096          2 /
 * init         1 root  rtd    DIR                8,5     4096          2 /
 * init         1 root  txt    REG                8,5   872400      63408 /app/busybox-1.19.2/busybox
 * rpc.portm 1064 root  mem    REG                8,5    43494      47451 /app/glibc-2.11/lib/libnss_files-2.11.so
 * rpc.portm 1064 root    3u  IPv4               2178                 UDP *:111
 * rpc.portm 1064 root    4u  IPv4               1244                 TCP *:111 (LISTEN)
 * runsvdir  1116 root    0r   CHR                1,3                1214 /dev/null
 * runsvdir  1116 root    1w   CHR                1,3                1214 /dev/null
 * runsvdir  1116 root    2w   CHR                1,3                1214 /dev/null
 * runsvdir  1116 root    3r   DIR                8,6     1560      58359 /.local/var/service
 * gpm       1128 root    4u  unix 0xffff88007c09ccc0                1302 /dev/gpmctl
 */

int lsof_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lsof_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	procps_status_t *proc = NULL;

	while ((proc = procps_scan(proc, PSSCAN_PID|PSSCAN_EXE)) != NULL) {
		char name[sizeof("/proc/%u/fd/0123456789") + sizeof(int)*3];
		unsigned baseofs;
		DIR *d_fd;
		char *fdlink;
		struct dirent *entry;

		if (getpid() == proc->pid)
			continue;

		baseofs = sprintf(name, "/proc/%u/fd/", proc->pid);
		d_fd = opendir(name);
		if (d_fd) {
			while ((entry = readdir(d_fd)) != NULL) {
				/* Skip entries '.' and '..' (and any hidden file) */
				if (entry->d_name[0] == '.')
					continue;

				safe_strncpy(name + baseofs, entry->d_name, 10);
				if ((fdlink = xmalloc_readlink(name)) != NULL) {
					printf("%d\t%s\t%s\n", proc->pid, proc->exe, fdlink);
					free(fdlink);
				}
			}
			closedir(d_fd);
		}
	}

	return EXIT_SUCCESS;
}
