/* vi: set sw=4 ts=4: */
/*
 * tiny fuser implementation
 *
 * Copyright 2004 Tony J. White
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FUSER
//config:	bool "fuser (7 kb)"
//config:	default y
//config:	help
//config:	fuser lists all PIDs (Process IDs) that currently have a given
//config:	file open. fuser can also list all PIDs that have a given network
//config:	(TCP or UDP) port open.

//applet:IF_FUSER(APPLET(fuser, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FUSER) += fuser.o

//usage:#define fuser_trivial_usage
//usage:       "[OPTIONS] FILE or PORT/PROTO"
//usage:#define fuser_full_usage "\n\n"
//usage:       "Find processes which use FILEs or PORTs\n"
//usage:     "\n	-m	Find processes which use same fs as FILEs"
//usage:     "\n	-4,-6	Search only IPv4/IPv6 space"
//usage:     "\n	-s	Don't display PIDs"
//usage:     "\n	-k	Kill found processes"
//usage:     "\n	-SIGNAL	Signal to send (default: KILL)"

#include "libbb.h"
#include "common_bufsiz.h"

#define MAX_LINE 255

#define OPTION_STRING "mks64"
enum {
	OPT_MOUNT  = (1 << 0),
	OPT_KILL   = (1 << 1),
	OPT_SILENT = (1 << 2),
	OPT_IP6    = (1 << 3),
	OPT_IP4    = (1 << 4),
};

typedef struct inode_list {
	struct inode_list *next;
	ino_t inode;
	dev_t dev;
} inode_list;

struct globals {
	int recursion_depth;
	pid_t mypid;
	inode_list *inode_list_head;
	smallint kill_failed;
	int killsig;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	G.mypid = getpid(); \
	G.killsig = SIGKILL; \
} while (0)

static void add_inode(const struct stat *st)
{
	inode_list **curr = &G.inode_list_head;

	while (*curr) {
		if ((*curr)->dev == st->st_dev
		 && (*curr)->inode == st->st_ino
		) {
			return;
		}
		curr = &(*curr)->next;
	}

	*curr = xzalloc(sizeof(inode_list));
	(*curr)->dev = st->st_dev;
	(*curr)->inode = st->st_ino;
}

static smallint search_dev_inode(const struct stat *st)
{
	inode_list *ilist = G.inode_list_head;

	while (ilist) {
		if (ilist->dev == st->st_dev) {
			if (option_mask32 & OPT_MOUNT)
				return 1;
			if (ilist->inode == st->st_ino)
				return 1;
		}
		ilist = ilist->next;
	}
	return 0;
}

enum {
	PROC_NET = 0,
	PROC_DIR,
	PROC_DIR_LINKS,
	PROC_SUBDIR_LINKS,
};

static smallint scan_proc_net_or_maps(const char *path, unsigned port)
{
	FILE *f;
	char line[MAX_LINE + 1], addr[68];
	int major, minor, r;
	long long uint64_inode;
	unsigned tmp_port;
	smallint retval;
	struct stat statbuf;
	const char *fmt;
	void *fag, *sag;

	f = fopen_for_read(path);
	if (!f)
		return 0;

	if (G.recursion_depth == PROC_NET) {
		int fd;

		/* find socket dev */
		statbuf.st_dev = 0;
		fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd >= 0) {
			fstat(fd, &statbuf);
			close(fd);
		}

		fmt = "%*d: %64[0-9A-Fa-f]:%x %*x:%*x %*x "
			"%*x:%*x %*x:%*x %*x %*d %*d %llu";
		fag = addr;
		sag = &tmp_port;
	} else {
		fmt = "%*s %*s %*s %x:%x %llu";
		fag = &major;
		sag = &minor;
	}

	retval = 0;
	while (fgets(line, MAX_LINE, f)) {
		r = sscanf(line, fmt, fag, sag, &uint64_inode);
		if (r != 3)
			continue;

		statbuf.st_ino = uint64_inode;
		if (G.recursion_depth == PROC_NET) {
			r = strlen(addr);
			if (r == 8 && (option_mask32 & OPT_IP6))
				continue;
			if (r > 8 && (option_mask32 & OPT_IP4))
				continue;
			if (tmp_port == port)
				add_inode(&statbuf);
		} else {
			if (major != 0 && minor != 0 && statbuf.st_ino != 0) {
				statbuf.st_dev = makedev(major, minor);
				retval = search_dev_inode(&statbuf);
				if (retval)
					break;
			}
		}
	}
	fclose(f);

	return retval;
}

static smallint scan_recursive(const char *path)
{
	DIR *d;
	struct dirent *d_ent;
	smallint stop_scan;
	smallint retval;

	d = opendir(path);
	if (d == NULL)
		return 0;

	G.recursion_depth++;
	retval = 0;
	stop_scan = 0;
	while (!stop_scan && (d_ent = readdir(d)) != NULL) {
		struct stat statbuf;
		pid_t pid;
		char *subpath;

		subpath = concat_subpath_file(path, d_ent->d_name);
		if (subpath == NULL)
			continue; /* . or .. */

		switch (G.recursion_depth) {
		case PROC_DIR:
			pid = (pid_t)bb_strtou(d_ent->d_name, NULL, 10);
			if (errno != 0
			 || pid == G.mypid
			/* "this PID doesn't use specified FILEs or PORT/PROTO": */
			 || scan_recursive(subpath) == 0
			) {
				break;
			}
			if (option_mask32 & OPT_KILL) {
				if (kill(pid, G.killsig) != 0) {
					bb_perror_msg("kill pid %s", d_ent->d_name);
					G.kill_failed = 1;
				}
			}
			if (!(option_mask32 & OPT_SILENT))
				printf("%s ", d_ent->d_name);
			retval = 1;
			break;

		case PROC_DIR_LINKS:
			switch (
				index_in_substrings(
					"cwd"  "\0" "exe"  "\0"
					"root" "\0" "fd"   "\0"
					"lib"  "\0" "mmap" "\0"
					"maps" "\0",
					d_ent->d_name
				)
			) {
			enum {
				CWD_LINK,
				EXE_LINK,
				ROOT_LINK,
				FD_DIR_LINKS,
				LIB_DIR_LINKS,
				MMAP_DIR_LINKS,
				MAPS,
			};
			case CWD_LINK:
			case EXE_LINK:
			case ROOT_LINK:
				goto scan_link;
			case FD_DIR_LINKS:
			case LIB_DIR_LINKS:
			case MMAP_DIR_LINKS:
				stop_scan = scan_recursive(subpath);
				if (stop_scan)
					retval = stop_scan;
				break;
			case MAPS:
				stop_scan = scan_proc_net_or_maps(subpath, 0);
				if (stop_scan)
					retval = stop_scan;
			default:
				break;
			}
			break;
		case PROC_SUBDIR_LINKS:
  scan_link:
			if (stat(subpath, &statbuf) < 0)
				break;
			stop_scan = search_dev_inode(&statbuf);
			if (stop_scan)
				retval = stop_scan;
		default:
			break;
		}
		free(subpath);
	}
	closedir(d);
	G.recursion_depth--;
	return retval;
}

int fuser_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fuser_main(int argc UNUSED_PARAM, char **argv)
{
	char **pp;

	INIT_G();

	/* Handle -SIGNAL. Oh my... */
	pp = argv;
	while (*++pp) {
		int sig;
		char *arg = *pp;

		if (arg[0] != '-')
			continue;
		if (arg[1] == '-' && arg[2] == '\0') /* "--" */
			break;
		if ((arg[1] == '4' || arg[1] == '6') && arg[2] == '\0')
			continue; /* it's "-4" or "-6" */
		sig = get_signum(&arg[1]);
		if (sig < 0)
			continue;
		/* "-SIGNAL" option found. Remove it and bail out */
		G.killsig = sig;
		do {
			pp[0] = arg = pp[1];
			pp++;
		} while (arg);
		break;
	}

	getopt32(argv, "^" OPTION_STRING "\0" "-1"/*at least one arg*/);
	argv += optind;

	pp = argv;
	while (*pp) {
		/* parse net arg */
		unsigned port;
		char path[sizeof("/proc/net/TCP6")];

		strcpy(path, "/proc/net/");
		if (sscanf(*pp, "%u/%4s", &port, path + sizeof("/proc/net/")-1) == 2
		 && access(path, R_OK) == 0
		) {
			/* PORT/PROTO */
			scan_proc_net_or_maps(path, port);
		} else {
			/* FILE */
			struct stat statbuf;
			xstat(*pp, &statbuf);
			add_inode(&statbuf);
		}
		pp++;
	}

	if (scan_recursive("/proc")) {
		if (!(option_mask32 & OPT_SILENT))
			bb_putchar('\n');
		return G.kill_failed;
	}

	return EXIT_FAILURE;
}
