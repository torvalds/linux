/* vi: set sw=4 ts=4: */
/*
 * fsck --- A generic, parallelizing front-end for the fsck program.
 * It will automatically try to run fsck programs in parallel if the
 * devices are on separate spindles.  It is based on the same ideas as
 * the generic front end for fsck by David Engel and Fred van Kempen,
 * but it has been completely rewritten from scratch to support
 * parallel execution.
 *
 * Written by Theodore Ts'o, <tytso@mit.edu>
 *
 * Miquel van Smoorenburg (miquels@drinkel.ow.org) 20-Oct-1994:
 *   o Changed -t fstype to behave like with mount when -A (all file
 *     systems) or -M (like mount) is specified.
 *   o fsck looks if it can find the fsck.type program to decide
 *     if it should ignore the fs type. This way more fsck programs
 *     can be added without changing this front-end.
 *   o -R flag skip root file system.
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
 *      2001, 2002, 2003, 2004, 2005 by  Theodore Ts'o.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* All filesystem specific hooks have been removed.
 * If filesystem cannot be determined, we will execute
 * "fsck.auto". Currently this also happens if you specify
 * UUID=xxx or LABEL=xxx as an object to check.
 * Detection code for that is also probably has to be in fsck.auto.
 *
 * In other words, this is _really_ is just a driver program which
 * spawns actual fsck.something for each filesystem to check.
 * It doesn't guess filesystem types from on-disk format.
 */
//config:config FSCK
//config:	bool "fsck (6.7 kb)"
//config:	default y
//config:	help
//config:	fsck is used to check and optionally repair one or more filesystems.
//config:	In actuality, fsck is simply a front-end for the various file system
//config:	checkers (fsck.fstype) available under Linux.

//applet:IF_FSCK(APPLET(fsck, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FSCK) += fsck.o

//usage:#define fsck_trivial_usage
//usage:       "[-ANPRTV] [-t FSTYPE] [FS_OPTS] [BLOCKDEV]..."
//usage:#define fsck_full_usage "\n\n"
//usage:       "Check and repair filesystems\n"
//usage:     "\n	-A	Walk /etc/fstab and check all filesystems"
//usage:     "\n	-N	Don't execute, just show what would be done"
//usage:     "\n	-P	With -A, check filesystems in parallel"
//usage:     "\n	-R	With -A, skip the root filesystem"
//usage:     "\n	-T	Don't show title on startup"
//usage:     "\n	-V	Verbose"
//DO_PROGRESS_INDICATOR is off:
////usage:     "\n	-C FD	Write status information to specified file descriptor"
//usage:     "\n	-t TYPE	List of filesystem types to check"

#include "libbb.h"
#include "common_bufsiz.h"

/* "progress indicator" code is somewhat buggy and ext[23] specific.
 * We should be filesystem agnostic. IOW: there should be a well-defined
 * API for fsck.something, NOT ad-hoc hacks in generic fsck. */
#define DO_PROGRESS_INDICATOR 0

/* fsck 1.41.4 (27-Jan-2009) manpage says:
 * 0   - No errors
 * 1   - File system errors corrected
 * 2   - System should be rebooted
 * 4   - File system errors left uncorrected
 * 8   - Operational error
 * 16  - Usage or syntax error
 * 32  - Fsck canceled by user request
 * 128 - Shared library error
 */
#define EXIT_OK          0
#define EXIT_NONDESTRUCT 1
#define EXIT_DESTRUCT    2
#define EXIT_UNCORRECTED 4
#define EXIT_ERROR       8
#define EXIT_USAGE       16
#define FSCK_CANCELED    32     /* Aborted with a signal or ^C */

/*
 * Internal structure for mount table entries.
 */
struct fs_info {
	struct fs_info *next;
	char	*device;
	char	*mountpt;
	char	*type;
	char	*opts;
	int	passno;
	int	flags;
};

#define FLAG_DONE 1
#define FLAG_PROGRESS 2
/*
 * Structure to allow exit codes to be stored
 */
struct fsck_instance {
	struct fsck_instance *next;
	int	pid;
	int	flags;
#if DO_PROGRESS_INDICATOR
	time_t	start_time;
#endif
	char	*prog;
	char	*device;
	char	*base_device; /* /dev/hda for /dev/hdaN etc */
};

static const char ignored_types[] ALIGN1 =
	"ignore\0"
	"iso9660\0"
	"nfs\0"
	"proc\0"
	"sw\0"
	"swap\0"
	"tmpfs\0"
	"devpts\0";

#if 0
static const char really_wanted[] ALIGN1 =
	"minix\0"
	"ext2\0"
	"ext3\0"
	"jfs\0"
	"reiserfs\0"
	"xiafs\0"
	"xfs\0";
#endif

#define BASE_MD "/dev/md"

struct globals {
	char **args;
	int num_args;
	int verbose;

#define FS_TYPE_FLAG_NORMAL 0
#define FS_TYPE_FLAG_OPT    1
#define FS_TYPE_FLAG_NEGOPT 2
	char **fs_type_list;
	uint8_t *fs_type_flag;
	smallint fs_type_negated;

	smallint noexecute;
	smallint serialize;
	smallint skip_root;
	/* smallint like_mount; */
	smallint parallel_root;
	smallint force_all_parallel;
	smallint kill_sent;

#if DO_PROGRESS_INDICATOR
	smallint progress;
	int progress_fd;
#endif

	int num_running;
	int max_running;
	char *fstype;
	struct fs_info *filesys_info;
	struct fs_info *filesys_last;
	struct fsck_instance *instance_list;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
} while (0)

/*
 * Return the "base device" given a particular device; this is used to
 * assure that we only fsck one partition on a particular drive at any
 * one time.  Otherwise, the disk heads will be seeking all over the
 * place.  If the base device cannot be determined, return NULL.
 *
 * The base_device() function returns an allocated string which must
 * be freed.
 */
#if ENABLE_FEATURE_DEVFS
/*
 * Required for the uber-silly devfs /dev/ide/host1/bus2/target3/lun3
 * pathames.
 */
static const char *const devfs_hier[] = {
	"host", "bus", "target", "lun", NULL
};
#endif

static char *base_device(const char *device)
{
	char *str, *cp;
#if ENABLE_FEATURE_DEVFS
	const char *const *hier;
	const char *disk;
	int len;
#endif
	str = xstrdup(device);

	/* Skip over "/dev/"; if it's not present, give up */
	cp = skip_dev_pfx(str);
	if (cp == str)
		goto errout;

	/*
	 * For md devices, we treat them all as if they were all
	 * on one disk, since we don't know how to parallelize them.
	 */
	if (cp[0] == 'm' && cp[1] == 'd') {
		cp[2] = 0;
		return str;
	}

	/* Handle DAC 960 devices */
	if (is_prefixed_with(cp, "rd/")) {
		cp += 3;
		if (cp[0] != 'c' || !isdigit(cp[1])
		 || cp[2] != 'd' || !isdigit(cp[3]))
			goto errout;
		cp[4] = 0;
		return str;
	}

	/* Now let's handle /dev/hd* and /dev/sd* devices.... */
	if ((cp[0] == 'h' || cp[0] == 's') && cp[1] == 'd') {
		cp += 2;
		/* If there's a single number after /dev/hd, skip it */
		if (isdigit(*cp))
			cp++;
		/* What follows must be an alpha char, or give up */
		if (!isalpha(*cp))
			goto errout;
		cp[1] = 0;
		return str;
	}

#if ENABLE_FEATURE_DEVFS
	/* Now let's handle devfs (ugh) names */
	len = 0;
	if (is_prefixed_with(cp, "ide/"))
		len = 4;
	if (is_prefixed_with(cp, "scsi/"))
		len = 5;
	if (len) {
		cp += len;
		/*
		 * Now we proceed down the expected devfs hierarchy.
		 * i.e., .../host1/bus2/target3/lun4/...
		 * If we don't find the expected token, followed by
		 * some number of digits at each level, abort.
		 */
		for (hier = devfs_hier; *hier; hier++) {
			cp = is_prefixed_with(cp, *hier);
			if (!cp)
				goto errout;
			while (*cp != '/' && *cp != '\0') {
				if (!isdigit(*cp))
					goto errout;
				cp++;
			}
//FIXME: what if *cp = '\0' now? cp++ moves past it!!!
			cp++;
		}
		cp[-1] = '\0';
		return str;
	}

	/* Now handle devfs /dev/disc or /dev/disk names */
	disk = NULL;
	if (is_prefixed_with(cp, "discs/"))
		disk = "disc";
	else if (is_prefixed_with(cp, "disks/"))
		disk = "disk";
	if (disk) {
		cp += 6;
		cp = is_prefixed_with(cp, disk);
		if (!cp)
			goto errout;
		while (*cp != '/' && *cp != '\0') {
			if (!isdigit(*cp))
				goto errout;
			cp++;
		}
		*cp = '\0';
		return str;
	}
#endif
 errout:
	free(str);
	return NULL;
}

static void free_instance(struct fsck_instance *p)
{
	free(p->prog);
	free(p->device);
	free(p->base_device);
	free(p);
}

static struct fs_info *create_fs_device(const char *device, const char *mntpnt,
					const char *type, const char *opts,
					int passno)
{
	struct fs_info *fs;

	fs = xzalloc(sizeof(*fs));
	fs->device = xstrdup(device);
	fs->mountpt = xstrdup(mntpnt);
	if (strchr(type, ','))
		type = (char *)"auto";
	fs->type = xstrdup(type);
	fs->opts = xstrdup(opts ? opts : "");
	fs->passno = passno < 0 ? 1 : passno;
	/*fs->flags = 0; */
	/*fs->next = NULL; */

	if (!G.filesys_info)
		G.filesys_info = fs;
	else
		G.filesys_last->next = fs;
	G.filesys_last = fs;

	return fs;
}

/* Load the filesystem database from /etc/fstab */
static void load_fs_info(const char *filename)
{
	FILE *fstab;
	struct mntent mte;
	char buf[1024];

	fstab = setmntent(filename, "r");
	if (!fstab) {
		bb_perror_msg("can't read '%s'", filename);
		return;
	}

	// Loop through entries
	while (getmntent_r(fstab, &mte, buf, sizeof(buf))) {
		//bb_error_msg("CREATE[%s][%s][%s][%s][%d]", mte.mnt_fsname, mte.mnt_dir,
		//	mte.mnt_type, mte.mnt_opts,
		//	mte.mnt_passno);
		create_fs_device(mte.mnt_fsname, mte.mnt_dir,
			mte.mnt_type, mte.mnt_opts,
			mte.mnt_passno);
	}
	endmntent(fstab);
}

/* Lookup filesys in /etc/fstab and return the corresponding entry. */
static struct fs_info *lookup(char *filesys)
{
	struct fs_info *fs;

	for (fs = G.filesys_info; fs; fs = fs->next) {
		if (strcmp(filesys, fs->device) == 0
		 || (fs->mountpt && strcmp(filesys, fs->mountpt) == 0)
		)
			break;
	}

	return fs;
}

#if DO_PROGRESS_INDICATOR
static int progress_active(void)
{
	struct fsck_instance *inst;

	for (inst = G.instance_list; inst; inst = inst->next) {
		if (inst->flags & FLAG_DONE)
			continue;
		if (inst->flags & FLAG_PROGRESS)
			return 1;
	}
	return 0;
}
#endif


/*
 * Send a signal to all outstanding fsck child processes
 */
static void kill_all_if_got_signal(void)
{
	struct fsck_instance *inst;

	if (!bb_got_signal || G.kill_sent)
		return;

	for (inst = G.instance_list; inst; inst = inst->next) {
		if (inst->flags & FLAG_DONE)
			continue;
		kill(inst->pid, SIGTERM);
	}
	G.kill_sent = 1;
}

/*
 * Wait for one child process to exit; when it does, unlink it from
 * the list of executing child processes, free, and return its exit status.
 * If there is no exited child, return -1.
 */
static int wait_one(int flags)
{
	int status;
	int exitcode;
	struct fsck_instance *inst, *prev;
	pid_t pid;

	if (!G.instance_list)
		return -1;
	/* if (G.noexecute) { already returned -1; } */

	while (1) {
		pid = waitpid(-1, &status, flags);
		kill_all_if_got_signal();
		if (pid == 0) /* flags == WNOHANG and no children exited */
			return -1;
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			if (errno == ECHILD) { /* paranoia */
				bb_error_msg("wait: no more children");
				return -1;
			}
			bb_perror_msg("wait");
			continue;
		}
		prev = NULL;
		inst = G.instance_list;
		do {
			if (inst->pid == pid)
				goto child_died;
			prev = inst;
			inst = inst->next;
		} while (inst);
	}
 child_died:

	exitcode = WEXITSTATUS(status);
	if (WIFSIGNALED(status)) {
		unsigned sig;
		sig = WTERMSIG(status);
		exitcode = EXIT_UNCORRECTED;
		if (sig != SIGINT) {
			printf("Warning: %s %s terminated "
				"by signal %u\n",
				inst->prog, inst->device, sig);
			exitcode = EXIT_ERROR;
		}
	}

#if DO_PROGRESS_INDICATOR
	if (progress && (inst->flags & FLAG_PROGRESS) && !progress_active()) {
		struct fsck_instance *inst2;
		for (inst2 = G.instance_list; inst2; inst2 = inst2->next) {
			if (inst2->flags & FLAG_DONE)
				continue;
			if (strcmp(inst2->type, "ext2") != 0
			 && strcmp(inst2->type, "ext3") != 0
			) {
				continue;
			}
			/* ext[23], we will send USR1
			 * (request to start displaying progress bar)
			 *
			 * If we've just started the fsck, wait a tiny
			 * bit before sending the kill, to give it
			 * time to set up the signal handler
			 */
			if (inst2->start_time >= time(NULL) - 1)
				sleep(1);
			kill(inst2->pid, SIGUSR1);
			inst2->flags |= FLAG_PROGRESS;
			break;
		}
	}
#endif

	if (prev)
		prev->next = inst->next;
	else
		G.instance_list = inst->next;
	if (G.verbose > 1)
		printf("Finished with %s (exit status %u)\n",
			inst->device, exitcode);
	G.num_running--;
	free_instance(inst);

	return exitcode;
}

/*
 * Wait until all executing child processes have exited; return the
 * logical OR of all of their exit code values.
 */
#define FLAG_WAIT_ALL           0
#define FLAG_WAIT_ATLEAST_ONE   WNOHANG
static int wait_many(int flags)
{
	int exit_status;
	int global_status = 0;
	int wait_flags = 0;

	while ((exit_status = wait_one(wait_flags)) != -1) {
		global_status |= exit_status;
		wait_flags |= flags;
	}
	return global_status;
}

/*
 * Execute a particular fsck program, and link it into the list of
 * child processes we are waiting for.
 */
static void execute(const char *type, const char *device,
		const char *mntpt /*, int interactive */)
{
	int i;
	struct fsck_instance *inst;
	pid_t pid;

	G.args[0] = xasprintf("fsck.%s", type);

#if DO_PROGRESS_INDICATOR
	if (progress && !progress_active()) {
		if (strcmp(type, "ext2") == 0
		 || strcmp(type, "ext3") == 0
		) {
			G.args[XXX] = xasprintf("-C%d", progress_fd); /* 1 */
			inst->flags |= FLAG_PROGRESS;
		}
	}
#endif

	G.args[G.num_args - 2] = (char*)device;
	/* G.args[G.num_args - 1] = NULL; - already is */

	if (G.verbose || G.noexecute) {
		printf("[%s (%d) -- %s]", G.args[0], G.num_running,
					mntpt ? mntpt : device);
		for (i = 0; G.args[i]; i++)
			printf(" %s", G.args[i]);
		bb_putchar('\n');
	}

	/* Fork and execute the correct program. */
	pid = -1;
	if (!G.noexecute) {
		pid = spawn(G.args);
		if (pid < 0)
			bb_simple_perror_msg(G.args[0]);
	}

#if DO_PROGRESS_INDICATOR
	free(G.args[XXX]);
#endif

	/* No child, so don't record an instance */
	if (pid <= 0) {
		free(G.args[0]);
		return;
	}

	inst = xzalloc(sizeof(*inst));
	inst->pid = pid;
	inst->prog = G.args[0];
	inst->device = xstrdup(device);
	inst->base_device = base_device(device);
#if DO_PROGRESS_INDICATOR
	inst->start_time = time(NULL);
#endif

	/* Add to the list of running fsck's.
	 * (was adding to the end, but adding to the front is simpler...) */
	inst->next = G.instance_list;
	G.instance_list = inst;
}

/*
 * Run the fsck program on a particular device
 *
 * If the type is specified using -t, and it isn't prefixed with "no"
 * (as in "noext2") and only one filesystem type is specified, then
 * use that type regardless of what is specified in /etc/fstab.
 *
 * If the type isn't specified by the user, then use either the type
 * specified in /etc/fstab, or "auto".
 */
static void fsck_device(struct fs_info *fs /*, int interactive */)
{
	const char *type;

	if (strcmp(fs->type, "auto") != 0) {
		type = fs->type;
		if (G.verbose > 2)
			printf("using filesystem type '%s' %s\n",
					type, "from fstab");
	} else if (G.fstype
	 && (G.fstype[0] != 'n' || G.fstype[1] != 'o') /* != "no" */
	 && !is_prefixed_with(G.fstype, "opts=")
	 && !is_prefixed_with(G.fstype, "loop")
	 && !strchr(G.fstype, ',')
	) {
		type = G.fstype;
		if (G.verbose > 2)
			printf("using filesystem type '%s' %s\n",
					type, "from -t");
	} else {
		type = "auto";
		if (G.verbose > 2)
			printf("using filesystem type '%s' %s\n",
					type, "(default)");
	}

	G.num_running++;
	execute(type, fs->device, fs->mountpt /*, interactive */);
}

/*
 * Returns TRUE if a partition on the same disk is already being
 * checked.
 */
static int device_already_active(char *device)
{
	struct fsck_instance *inst;
	char *base;

	if (G.force_all_parallel)
		return 0;

#ifdef BASE_MD
	/* Don't check a soft raid disk with any other disk */
	if (G.instance_list
	 && (is_prefixed_with(G.instance_list->device, BASE_MD)
	     || is_prefixed_with(device, BASE_MD))
	) {
		return 1;
	}
#endif

	base = base_device(device);
	/*
	 * If we don't know the base device, assume that the device is
	 * already active if there are any fsck instances running.
	 */
	if (!base)
		return (G.instance_list != NULL);

	for (inst = G.instance_list; inst; inst = inst->next) {
		if (!inst->base_device || strcmp(base, inst->base_device) == 0) {
			free(base);
			return 1;
		}
	}

	free(base);
	return 0;
}

/*
 * This function returns true if a particular option appears in a
 * comma-delimited options list
 */
static int opt_in_list(char *opt, char *optlist)
{
	char *s;
	int len;

	if (!optlist)
		return 0;

	len = strlen(opt);
	s = optlist - 1;
	while (1) {
		s = strstr(s + 1, opt);
		if (!s)
			return 0;
		/* neither "opt.." nor "xxx,opt.."? */
		if (s != optlist && s[-1] != ',')
			continue;
		/* neither "..opt" nor "..opt,xxx"? */
		if (s[len] != '\0' && s[len] != ',')
			continue;
		return 1;
	}
}

/* See if the filesystem matches the criteria given by the -t option */
static int fs_match(struct fs_info *fs)
{
	int n, ret, checked_type;
	char *cp;

	if (!G.fs_type_list)
		return 1;

	ret = 0;
	checked_type = 0;
	n = 0;
	while (1) {
		cp = G.fs_type_list[n];
		if (!cp)
			break;
		switch (G.fs_type_flag[n]) {
		case FS_TYPE_FLAG_NORMAL:
			checked_type++;
			if (strcmp(cp, fs->type) == 0)
				ret = 1;
			break;
		case FS_TYPE_FLAG_NEGOPT:
			if (opt_in_list(cp, fs->opts))
				return 0;
			break;
		case FS_TYPE_FLAG_OPT:
			if (!opt_in_list(cp, fs->opts))
				return 0;
			break;
		}
		n++;
	}
	if (checked_type == 0)
		return 1;

	return (G.fs_type_negated ? !ret : ret);
}

/* Check if we should ignore this filesystem. */
static int ignore(struct fs_info *fs)
{
	/*
	 * If the pass number is 0, ignore it.
	 */
	if (fs->passno == 0)
		return 1;

	/*
	 * If a specific fstype is specified, and it doesn't match,
	 * ignore it.
	 */
	if (!fs_match(fs))
		return 1;

	/* Are we ignoring this type? */
	if (index_in_strings(ignored_types, fs->type) >= 0)
		return 1;

	/* We can and want to check this file system type. */
	return 0;
}

/* Check all file systems, using the /etc/fstab table. */
static int check_all(void)
{
	struct fs_info *fs;
	int status = EXIT_OK;
	smallint not_done_yet;
	smallint pass_done;
	int passno;

	if (G.verbose)
		puts("Checking all filesystems");

	/*
	 * Do an initial scan over the filesystem; mark filesystems
	 * which should be ignored as done, and resolve any "auto"
	 * filesystem types (done as a side-effect of calling ignore()).
	 */
	for (fs = G.filesys_info; fs; fs = fs->next)
		if (ignore(fs))
			fs->flags |= FLAG_DONE;

	/*
	 * Find and check the root filesystem.
	 */
	if (!G.parallel_root) {
		for (fs = G.filesys_info; fs; fs = fs->next) {
			if (LONE_CHAR(fs->mountpt, '/')) {
				if (!G.skip_root && !ignore(fs)) {
					fsck_device(fs /*, 1*/);
					status |= wait_many(FLAG_WAIT_ALL);
					if (status > EXIT_NONDESTRUCT)
						return status;
				}
				fs->flags |= FLAG_DONE;
				break;
			}
		}
	}
	/*
	 * This is for the bone-headed user who has root
	 * filesystem listed twice.
	 * "Skip root" will skip _all_ root entries.
	 */
	if (G.skip_root)
		for (fs = G.filesys_info; fs; fs = fs->next)
			if (LONE_CHAR(fs->mountpt, '/'))
				fs->flags |= FLAG_DONE;

	not_done_yet = 1;
	passno = 1;
	while (not_done_yet) {
		not_done_yet = 0;
		pass_done = 1;

		for (fs = G.filesys_info; fs; fs = fs->next) {
			if (bb_got_signal)
				break;
			if (fs->flags & FLAG_DONE)
				continue;
			/*
			 * If the filesystem's pass number is higher
			 * than the current pass number, then we didn't
			 * do it yet.
			 */
			if (fs->passno > passno) {
				not_done_yet = 1;
				continue;
			}
			/*
			 * If a filesystem on a particular device has
			 * already been spawned, then we need to defer
			 * this to another pass.
			 */
			if (device_already_active(fs->device)) {
				pass_done = 0;
				continue;
			}
			/*
			 * Spawn off the fsck process
			 */
			fsck_device(fs /*, G.serialize*/);
			fs->flags |= FLAG_DONE;

			/*
			 * Only do one filesystem at a time, or if we
			 * have a limit on the number of fsck's extant
			 * at one time, apply that limit.
			 */
			if (G.serialize
			 || (G.num_running >= G.max_running)
			) {
				pass_done = 0;
				break;
			}
		}
		if (bb_got_signal)
			break;
		if (G.verbose > 1)
			printf("--waiting-- (pass %d)\n", passno);
		status |= wait_many(pass_done ? FLAG_WAIT_ALL :
				FLAG_WAIT_ATLEAST_ONE);
		if (pass_done) {
			if (G.verbose > 1)
				puts("----------------------------------");
			passno++;
		} else
			not_done_yet = 1;
	}
	kill_all_if_got_signal();
	status |= wait_many(FLAG_WAIT_ATLEAST_ONE);
	return status;
}

/*
 * Deal with the fsck -t argument.
 * Huh, for mount "-t novfat,nfs" means "neither vfat nor nfs"!
 * Why here we require "-t novfat,nonfs" ??
 */
static void compile_fs_type(char *fs_type)
{
	char *s;
	int num = 2;
	smallint negate;

	s = fs_type;
	while ((s = strchr(s, ','))) {
		num++;
		s++;
	}

	G.fs_type_list = xzalloc(num * sizeof(G.fs_type_list[0]));
	G.fs_type_flag = xzalloc(num * sizeof(G.fs_type_flag[0]));
	G.fs_type_negated = -1; /* not yet known is it negated or not */

	num = 0;
	s = fs_type;
	while (1) {
		char *comma;

		negate = 0;
		if (s[0] == 'n' && s[1] == 'o') { /* "no.." */
			s += 2;
			negate = 1;
		} else if (s[0] == '!') {
			s++;
			negate = 1;
		}

		if (strcmp(s, "loop") == 0)
			/* loop is really short-hand for opts=loop */
			goto loop_special_case;
		if (is_prefixed_with(s, "opts=")) {
			s += 5;
 loop_special_case:
			G.fs_type_flag[num] = negate ? FS_TYPE_FLAG_NEGOPT : FS_TYPE_FLAG_OPT;
		} else {
			if (G.fs_type_negated == -1)
				G.fs_type_negated = negate;
			if (G.fs_type_negated != negate)
				bb_error_msg_and_die(
"either all or none of the filesystem types passed to -t must be prefixed "
"with 'no' or '!'");
		}
		comma = strchrnul(s, ',');
		G.fs_type_list[num++] = xstrndup(s, comma-s);
		if (*comma == '\0')
			break;
		s = comma + 1;
	}
}

static char **new_args(void)
{
	G.args = xrealloc_vector(G.args, 2, G.num_args);
	return &G.args[G.num_args++];
}

int fsck_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fsck_main(int argc UNUSED_PARAM, char **argv)
{
	int i, status;
	/*int interactive;*/
	struct fs_info *fs;
	const char *fstab;
	char *tmp;
	char **devices;
	int num_devices;
	smallint opts_for_fsck;
	smallint doall;
	smallint notitle;

	INIT_G();

	/* we want wait() to be interruptible */
	signal_no_SA_RESTART_empty_mask(SIGINT, record_signo);
	signal_no_SA_RESTART_empty_mask(SIGTERM, record_signo);

	setbuf(stdout, NULL);

	opts_for_fsck = doall = notitle = 0;
	devices = NULL;
	num_devices = 0;
	new_args(); /* G.args[0] = NULL, will be replaced by fsck.<type> */
	/* G.instance_list = NULL; - in bss, so already zeroed */

	while (*++argv) {
		int j;
		int optpos;
		char *options;
		char *arg = *argv;

		/* "/dev/blk" or "/path" or "UUID=xxx" or "LABEL=xxx" */
		if ((arg[0] == '/' && !opts_for_fsck) || strchr(arg, '=')) {
// FIXME: must check that arg is a blkdev, or resolve
// "/path", "UUID=xxx" or "LABEL=xxx" into block device name
// ("UUID=xxx"/"LABEL=xxx" can probably shifted to fsck.auto duties)
			devices = xrealloc_vector(devices, 2, num_devices);
			devices[num_devices++] = arg;
			continue;
		}

		if (arg[0] != '-' || opts_for_fsck) {
			*new_args() = arg;
			continue;
		}

		if (LONE_CHAR(arg + 1, '-')) { /* "--" ? */
			opts_for_fsck = 1;
			continue;
		}

		optpos = 0;
		options = NULL;
		for (j = 1; arg[j]; j++) {
			switch (arg[j]) {
			case 'A':
				doall = 1;
				break;
#if DO_PROGRESS_INDICATOR
			case 'C':
				progress = 1;
				if (arg[++j]) { /* -Cn */
					progress_fd = xatoi_positive(&arg[j]);
					goto next_arg;
				}
				/* -C n */
				if (!*++argv)
					bb_show_usage();
				progress_fd = xatoi_positive(*argv);
				goto next_arg;
#endif
			case 'V':
				G.verbose++;
				break;
			case 'N':
				G.noexecute = 1;
				break;
			case 'R':
				G.skip_root = 1;
				break;
			case 'T':
				notitle = 1;
				break;
/*			case 'M':
				like_mount = 1;
				break; */
			case 'P':
				G.parallel_root = 1;
				break;
			case 's':
				G.serialize = 1;
				break;
			case 't':
				if (G.fstype)
					bb_show_usage();
				if (arg[++j])
					tmp = &arg[j];
				else if (*++argv)
					tmp = *argv;
				else
					bb_show_usage();
				G.fstype = xstrdup(tmp);
				compile_fs_type(G.fstype);
				goto next_arg;
			case '?':
				bb_show_usage();
				break;
			default:
				optpos++;
				/* one extra for '\0' */
				options = xrealloc(options, optpos + 2);
				options[optpos] = arg[j];
				break;
			}
		}
 next_arg:
		if (optpos) {
			options[0] = '-';
			options[optpos + 1] = '\0';
			*new_args() = options;
		}
	}
	if (getenv("FSCK_FORCE_ALL_PARALLEL"))
		G.force_all_parallel = 1;
	tmp = getenv("FSCK_MAX_INST");
	G.max_running = INT_MAX;
	if (tmp)
		G.max_running = xatoi(tmp);
	new_args(); /* G.args[G.num_args - 2] will be replaced by <device> */
	new_args(); /* G.args[G.num_args - 1] is the last, NULL element */

	if (!notitle)
		puts("fsck (busybox "BB_VER")");

	/* Even plain "fsck /dev/hda1" needs fstab to get fs type,
	 * so we are scanning it anyway */
	fstab = getenv("FSTAB_FILE");
	if (!fstab)
		fstab = "/etc/fstab";
	load_fs_info(fstab);

	/*interactive = (num_devices == 1) | G.serialize;*/

	if (num_devices == 0)
		/*interactive =*/ G.serialize = doall = 1;
	if (doall)
		return check_all();

	status = 0;
	for (i = 0; i < num_devices; i++) {
		if (bb_got_signal) {
			kill_all_if_got_signal();
			break;
		}

		fs = lookup(devices[i]);
		if (!fs)
			fs = create_fs_device(devices[i], "", "auto", NULL, -1);
		fsck_device(fs /*, interactive */);

		if (G.serialize
		 || (G.num_running >= G.max_running)
		) {
			int exit_status = wait_one(0);
			if (exit_status >= 0)
				status |= exit_status;
			if (G.verbose > 1)
				puts("----------------------------------");
		}
	}
	status |= wait_many(FLAG_WAIT_ALL);
	return status;
}
