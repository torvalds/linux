/* vi: set sw=4 ts=4: */
/*
 * Copyright 2005 Rob Landley <rob@landley.net>
 *
 * Switch from rootfs to another filesystem as the root of the mount tree.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SWITCH_ROOT
//config:	bool "switch_root (5.2 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	The switch_root utility is used from initramfs to select a new
//config:	root device. Under initramfs, you have to use this instead of
//config:	pivot_root. (Stop reading here if you don't care why.)
//config:
//config:	Booting with initramfs extracts a gzipped cpio archive into rootfs
//config:	(which is a variant of ramfs/tmpfs). Because rootfs can't be moved
//config:	or unmounted*, pivot_root will not work from initramfs. Instead,
//config:	switch_root deletes everything out of rootfs (including itself),
//config:	does a mount --move that overmounts rootfs with the new root, and
//config:	then execs the specified init program.
//config:
//config:	* Because the Linux kernel uses rootfs internally as the starting
//config:	and ending point for searching through the kernel's doubly linked
//config:	list of active mount points. That's why.
//config:
// RUN_INIT config item is in klibc-utils

//applet:IF_SWITCH_ROOT(APPLET(switch_root, BB_DIR_SBIN, BB_SUID_DROP))
//                      APPLET_ODDNAME:name      main         location     suid_type     help
//applet:IF_RUN_INIT(   APPLET_ODDNAME(run-init, switch_root, BB_DIR_SBIN, BB_SUID_DROP, run_init))

//kbuild:lib-$(CONFIG_SWITCH_ROOT) += switch_root.o
//kbuild:lib-$(CONFIG_RUN_INIT)    += switch_root.o

#include <sys/vfs.h>
#include <sys/mount.h>
#if ENABLE_RUN_INIT
# include <sys/prctl.h>
# ifndef PR_CAPBSET_READ
# define PR_CAPBSET_READ 23
# endif
# ifndef PR_CAPBSET_DROP
# define PR_CAPBSET_DROP 24
# endif
# include <linux/capability.h>
// #include <sys/capability.h>
// This header is in libcap, but the functions are in libc.
// Comment in the header says this above capset/capget:
/* system calls - look to libc for function to system call mapping */
extern int capset(cap_user_header_t header, cap_user_data_t data);
extern int capget(cap_user_header_t header, const cap_user_data_t data);
// so for bbox, let's just repeat the declarations.
// This way, libcap needs not be installed in build environment.
#endif

#include "libbb.h"

// Make up for header deficiencies
#ifndef RAMFS_MAGIC
# define RAMFS_MAGIC ((unsigned)0x858458f6)
#endif
#ifndef TMPFS_MAGIC
# define TMPFS_MAGIC ((unsigned)0x01021994)
#endif
#ifndef MS_MOVE
# define MS_MOVE     8192
#endif

// Recursively delete contents of rootfs
static void delete_contents(const char *directory, dev_t rootdev)
{
	DIR *dir;
	struct dirent *d;
	struct stat st;

	// Don't descend into other filesystems
	if (lstat(directory, &st) || st.st_dev != rootdev)
		return;

	// Recursively delete the contents of directories
	if (S_ISDIR(st.st_mode)) {
		dir = opendir(directory);
		if (dir) {
			while ((d = readdir(dir))) {
				char *newdir = d->d_name;

				// Skip . and ..
				if (DOT_OR_DOTDOT(newdir))
					continue;

				// Recurse to delete contents
				newdir = concat_path_file(directory, newdir);
				delete_contents(newdir, rootdev);
				free(newdir);
			}
			closedir(dir);

			// Directory should now be empty, zap it
			rmdir(directory);
		}
	} else {
		// It wasn't a directory, zap it
		unlink(directory);
	}
}

#if ENABLE_RUN_INIT
DEFINE_STRUCT_CAPS;

static void drop_capset(int cap_idx)
{
	struct caps caps;

	getcaps(&caps);
	caps.data[CAP_TO_INDEX(cap_idx)].inheritable &= ~CAP_TO_MASK(cap_idx);
	if (capset(&caps.header, caps.data) != 0)
		bb_perror_msg_and_die("capset");
}

static void drop_bounding_set(int cap_idx)
{
	int ret;

	ret = prctl(PR_CAPBSET_READ, cap_idx, 0, 0, 0);
	if (ret < 0)
		bb_perror_msg_and_die("prctl: %s", "PR_CAPBSET_READ");

	if (ret == 1) {
		ret = prctl(PR_CAPBSET_DROP, cap_idx, 0, 0, 0);
		if (ret != 0)
			bb_perror_msg_and_die("prctl: %s", "PR_CAPBSET_DROP");
	}
}

static void drop_usermodehelper(const char *filename, int cap_idx)
{
	unsigned lo, hi;
	char buf[sizeof(int)*3 * 2 + 8];
	int fd;
	int ret;

	ret = open_read_close(filename, buf, sizeof(buf) - 1);
	if (ret < 0)
		return; /* assuming files do not exist */

	buf[ret] = '\0';
	ret = sscanf(buf, "%u %u", &lo, &hi);
	if (ret != 2)
		bb_perror_msg_and_die("can't parse file '%s'", filename);

	if (cap_idx < 32)
		lo &= ~(1 << cap_idx);
	else
		hi &= ~(1 << (cap_idx - 32));

	fd = xopen(filename, O_WRONLY);
	fdprintf(fd, "%u %u", lo, hi);
	close(fd);
}

static void drop_capabilities(char *string)
{
	char *cap;

	cap = strtok(string, ",");
	while (cap) {
		unsigned cap_idx;

		cap_idx = cap_name_to_number(cap);
		drop_usermodehelper("/proc/sys/kernel/usermodehelper/bset", cap_idx);
		drop_usermodehelper("/proc/sys/kernel/usermodehelper/inheritable", cap_idx);
		drop_bounding_set(cap_idx);
		drop_capset(cap_idx);
		bb_error_msg("dropped capability: %s", cap);
		cap = strtok(NULL, ",");
	}
}
#endif

int switch_root_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int switch_root_main(int argc UNUSED_PARAM, char **argv)
{
	char *newroot, *console = NULL;
	struct stat st;
	struct statfs stfs;
	unsigned dry_run = 0;
	dev_t rootdev;

	// Parse args. '+': stop at first non-option
	if (ENABLE_SWITCH_ROOT && (!ENABLE_RUN_INIT || applet_name[0] == 's')) {
//usage:#define switch_root_trivial_usage
//usage:       "[-c CONSOLE_DEV] NEW_ROOT NEW_INIT [ARGS]"
//usage:#define switch_root_full_usage "\n\n"
//usage:       "Free initramfs and switch to another root fs:\n"
//usage:       "chroot to NEW_ROOT, delete all in /, move NEW_ROOT to /,\n"
//usage:       "execute NEW_INIT. PID must be 1. NEW_ROOT must be a mountpoint.\n"
//usage:     "\n	-c DEV	Reopen stdio to DEV after switch"
		getopt32(argv, "^+"
			"c:"
			"\0" "-2" /* minimum 2 args */,
			&console
		);
	} else {
#if ENABLE_RUN_INIT
//usage:#define run_init_trivial_usage
//usage:       "[-d CAP,CAP...] [-n] [-c CONSOLE_DEV] NEW_ROOT NEW_INIT [ARGS]"
//usage:#define run_init_full_usage "\n\n"
//usage:       "Free initramfs and switch to another root fs:\n"
//usage:       "chroot to NEW_ROOT, delete all in /, move NEW_ROOT to /,\n"
//usage:       "execute NEW_INIT. PID must be 1. NEW_ROOT must be a mountpoint.\n"
//usage:     "\n	-c DEV	Reopen stdio to DEV after switch"
//usage:     "\n	-d CAPS	Drop capabilities"
//usage:     "\n	-n	Dry run"
		char *cap_list = NULL;
		dry_run = getopt32(argv, "^+"
			"c:d:n"
			"\0" "-2" /* minimum 2 args */,
			&console,
			&cap_list
		);
		dry_run >>= 2; // -n
		if (cap_list)
			drop_capabilities(cap_list);
#endif
	}
	argv += optind;
	newroot = *argv++;

	// Change to new root directory and verify it's a different fs
	xchdir(newroot);
	xstat("/", &st);
	rootdev = st.st_dev;
	xstat(".", &st);
	if (st.st_dev == rootdev) {
		// Show usage, it says new root must be a mountpoint
		bb_show_usage();
	}
	if (!dry_run && getpid() != 1) {
		// Show usage, it says we must be PID 1
		bb_show_usage();
	}

	// Additional sanity checks: we're about to rm -rf /, so be REALLY SURE
	// we mean it. I could make this a CONFIG option, but I would get email
	// from all the people who WILL destroy their filesystems.
	if (stat("/init", &st) != 0 || !S_ISREG(st.st_mode)) {
		bb_error_msg_and_die("'%s' is not a regular file", "/init");
	}
	statfs("/", &stfs); // this never fails
	if ((unsigned)stfs.f_type != RAMFS_MAGIC
	 && (unsigned)stfs.f_type != TMPFS_MAGIC
	) {
		bb_error_msg_and_die("root filesystem is not ramfs/tmpfs");
	}

	if (!dry_run) {
		// Zap everything out of rootdev
		delete_contents("/", rootdev);

		// Overmount / with newdir and chroot into it
		if (mount(".", "/", NULL, MS_MOVE, NULL)) {
			// For example, fails when newroot is not a mountpoint
			bb_perror_msg_and_die("error moving root");
		}
	}
	xchroot(".");
	// The chdir is needed to recalculate "." and ".." links
	/*xchdir("/"); - done in xchroot */

	// If a new console specified, redirect stdin/stdout/stderr to it
	if (console) {
		int fd = open_or_warn(console, O_RDWR);
		if (fd >= 0) {
			xmove_fd(fd, 0);
			xdup2(0, 1);
			xdup2(0, 2);
		}
	}

	if (dry_run) {
		// Does NEW_INIT look like it can be executed?
		//xstat(argv[0], &st);
		//if (!S_ISREG(st.st_mode))
		//	bb_perror_msg_and_die("'%s' is not a regular file", argv[0]);
		if (access(argv[0], X_OK) == 0)
			return 0;
	} else {
		// Exec NEW_INIT
		execv(argv[0], argv);
	}
	bb_perror_msg_and_die("can't execute '%s'", argv[0]);
}

/*
From: Rob Landley <rob@landley.net>
Date: Tue, Jun 16, 2009 at 7:47 PM
Subject: Re: switch_root...

...
...
...

If you're _not_ running out of init_ramfs (if for example you're using initrd
instead), you probably shouldn't use switch_root because it's the wrong tool.

Basically what the sucker does is something like the following shell script:

 find / -xdev | xargs rm -rf
 cd "$1"
 shift
 mount --move . /
 exec chroot . "$@"

There are a couple reasons that won't work as a shell script:

1) If you delete the commands out of your $PATH, your shell scripts can't run
more commands, but you can't start using dynamically linked _new_ commands
until after you do the chroot because the path to the dynamic linker is wrong.
So there's a step that needs to be sort of atomic but can't be as a shell
script.  (You can work around this with static linking or very carefully laid
out paths and sequencing, but it's brittle, ugly, and non-obvious.)

2) The "find | rm" bit will actually delete everything because the mount points
still show up (even if their contents don't), and rm -rf will then happily zap
that.  So the first line is an oversimplification of what you need to do _not_
to descend into other filesystems and delete their contents.

The reason we do this is to free up memory, by the way.  Since initramfs is a
ramfs, deleting its contents frees up the memory it uses.  (We leave it with
one remaining dentry for the new mount point, but that's ok.)

Note that you cannot ever umount rootfs, for approximately the same reason you
can't kill PID 1.  The kernel tracks mount points as a doubly linked list, and
the pointer to the start/end of that list always points to an entry that's
known to be there (rootfs), so it never has to worry about moving that pointer
and it never has to worry about the list being empty.  (Back around 2.6.13
there _was_ a bug that let you umount rootfs, and the system locked hard the
instant you did so endlessly looping to find the end of the mount list and
never stopping.  They fixed it.)

Oh, and the reason we mount --move _and_ do the chroot is due to the way "/"
works.  Each process has two special symlinks, ".", and "/".  Each of them
points to the dentry of a directory, and give you a location paths can start
from.  (Historically ".." was also special, because you could enter a
directory via a symlink so backing out to the directory you came from doesn't
necessarily mean the one physically above where "." points to.  These days I
think it's just handed off to the filesystem.)

Anyway, path resolution starts with "." or "/" (although the "./" at the start
of the path may be implicit), meaning it's relative to one of those two
directories.  Your current directory, and your current root directory.  The
chdir() syscall changes where "." points to, and the chroot() syscall changes
where "/" points to.  (Again, both are per-process which is why chroot only
affects your current process and its child processes.)

Note that chroot() does _not_ change where "." points to, and back before they
put crazy security checks into the kernel your current directory could be
somewhere you could no longer access after the chroot.  (The command line
chroot does a cd as well, the chroot _syscall_ is what I'm talking about.)

The reason mounting something new over / has no obvious effect is the same
reason mounting something over your current directory has no obvious effect:
the . and / links aren't recalculated after a mount, so they still point to
the same dentry they did before, even if that dentry is no longer accessible
by other means.  Note that "cd ." is a NOP, and "chroot /" is a nop; both look
up the cached dentry and set it right back.  They don't re-parse any paths,
because they're what all paths your process uses would be relative to.

That's why the careful sequencing above: we cd into the new mount point before
we do the mount --move.  Moving the mount point would otherwise make it
totally inaccessible to us because cd-ing to the old path wouldn't give it to
us anymore, and cd "/" just gives us the cached dentry from when the process
was created (in this case the old initramfs one).  But the "." symlink gives
us the dentry of the filesystem we just moved, so we can then "chroot ." to
copy that dentry to "/" and get the new filesystem.  If we _didn't_ save that
dentry in "." we couldn't get it back after the mount --move.

(Yes, this is all screwy and I had to email questions to Linus Torvalds to get
it straight myself.  I keep meaning to write up a "how mount actually works"
document someday...)
*/
