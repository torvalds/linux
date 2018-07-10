/* vi: set sw=4 ts=4: */
/*
 * Mini mount implementation for busybox
 *
 * Copyright (C) 1995, 1996 by Bruce Perens <bruce@pixar.com>.
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2005-2006 by Rob Landley <rob@landley.net>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
// Design notes: There is no spec for mount.  Remind me to write one.
//
// mount_main() calls singlemount() which calls mount_it_now().
//
// mount_main() can loop through /etc/fstab for mount -a
// singlemount() can loop through /etc/filesystems for fstype detection.
// mount_it_now() does the actual mount.
//

//config:config MOUNT
//config:	bool "mount (30 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	All files and filesystems in Unix are arranged into one big directory
//config:	tree. The 'mount' utility is used to graft a filesystem onto a
//config:	particular part of the tree. A filesystem can either live on a block
//config:	device, or it can be accessible over the network, as is the case with
//config:	NFS filesystems.
//config:
//config:config FEATURE_MOUNT_FAKE
//config:	bool "Support -f (fake mount)"
//config:	default y
//config:	depends on MOUNT
//config:	help
//config:	Enable support for faking a file system mount.
//config:
//config:config FEATURE_MOUNT_VERBOSE
//config:	bool "Support -v (verbose)"
//config:	default y
//config:	depends on MOUNT
//config:	help
//config:	Enable multi-level -v[vv...] verbose messages. Useful if you
//config:	debug mount problems and want to see what is exactly passed
//config:	to the kernel.
//config:
//config:config FEATURE_MOUNT_HELPERS
//config:	bool "Support mount helpers"
//config:	default n
//config:	depends on MOUNT
//config:	help
//config:	Enable mounting of virtual file systems via external helpers.
//config:	E.g. "mount obexfs#-b00.11.22.33.44.55 /mnt" will in effect call
//config:	"obexfs -b00.11.22.33.44.55 /mnt"
//config:	Also "mount -t sometype [-o opts] fs /mnt" will try
//config:	"sometype [-o opts] fs /mnt" if simple mount syscall fails.
//config:	The idea is to use such virtual filesystems in /etc/fstab.
//config:
//config:config FEATURE_MOUNT_LABEL
//config:	bool "Support specifying devices by label or UUID"
//config:	default y
//config:	depends on MOUNT
//config:	select VOLUMEID
//config:	help
//config:	This allows for specifying a device by label or uuid, rather than by
//config:	name. This feature utilizes the same functionality as blkid/findfs.
//config:
//config:config FEATURE_MOUNT_NFS
//config:	bool "Support mounting NFS file systems on Linux < 2.6.23"
//config:	default n
//config:	depends on MOUNT
//config:	select FEATURE_SYSLOG
//config:	help
//config:	Enable mounting of NFS file systems on Linux kernels prior
//config:	to version 2.6.23. Note that in this case mounting of NFS
//config:	over IPv6 will not be possible.
//config:
//config:	Note that this option links in RPC support from libc,
//config:	which is rather large (~10 kbytes on uclibc).
//config:
//config:config FEATURE_MOUNT_CIFS
//config:	bool "Support mounting CIFS/SMB file systems"
//config:	default y
//config:	depends on MOUNT
//config:	help
//config:	Enable support for samba mounts.
//config:
//config:config FEATURE_MOUNT_FLAGS
//config:	depends on MOUNT
//config:	bool "Support lots of -o flags"
//config:	default y
//config:	help
//config:	Without this, mount only supports ro/rw/remount. With this, it
//config:	supports nosuid, suid, dev, nodev, exec, noexec, sync, async, atime,
//config:	noatime, diratime, nodiratime, loud, bind, move, shared, slave,
//config:	private, unbindable, rshared, rslave, rprivate, and runbindable.
//config:
//config:config FEATURE_MOUNT_FSTAB
//config:	depends on MOUNT
//config:	bool "Support /etc/fstab and -a (mount all)"
//config:	default y
//config:	help
//config:	Support mount all and looking for files in /etc/fstab.
//config:
//config:config FEATURE_MOUNT_OTHERTAB
//config:	depends on FEATURE_MOUNT_FSTAB
//config:	bool "Support -T <alt_fstab>"
//config:	default y
//config:	help
//config:	Support mount -T (specifying an alternate fstab)

/* On full-blown systems, requires suid for user mounts.
 * But it's not unthinkable to have it available in non-suid flavor on some systems,
 * for viewing mount table.
 * Therefore we use BB_SUID_MAYBE instead of BB_SUID_REQUIRE: */
//applet:IF_MOUNT(APPLET(mount, BB_DIR_BIN, IF_DESKTOP(BB_SUID_MAYBE) IF_NOT_DESKTOP(BB_SUID_DROP)))

//kbuild:lib-$(CONFIG_MOUNT) += mount.o

//usage:#define mount_trivial_usage
//usage:       "[OPTIONS] [-o OPT] DEVICE NODE"
//usage:#define mount_full_usage "\n\n"
//usage:       "Mount a filesystem. Filesystem autodetection requires /proc.\n"
//usage:     "\n	-a		Mount all filesystems in fstab"
//usage:	IF_FEATURE_MOUNT_FAKE(
//usage:	IF_FEATURE_MTAB_SUPPORT(
//usage:     "\n	-f		Update /etc/mtab, but don't mount"
//usage:	)
//usage:	IF_NOT_FEATURE_MTAB_SUPPORT(
//usage:     "\n	-f		Dry run"
//usage:	)
//usage:	)
//usage:	IF_FEATURE_MOUNT_HELPERS(
//usage:     "\n	-i		Don't run mount helper"
//usage:	)
//usage:	IF_FEATURE_MTAB_SUPPORT(
//usage:     "\n	-n		Don't update /etc/mtab"
//usage:	)
//usage:	IF_FEATURE_MOUNT_VERBOSE(
//usage:     "\n	-v		Verbose"
//usage:	)
////usage:   "\n	-s		Sloppy (ignored)"
//usage:     "\n	-r		Read-only mount"
////usage:     "\n	-w		Read-write mount (default)"
//usage:     "\n	-t FSTYPE[,...]	Filesystem type(s)"
//usage:	IF_FEATURE_MOUNT_OTHERTAB(
//usage:     "\n	-T FILE		Read FILE instead of /etc/fstab"
//usage:	)
//usage:     "\n	-O OPT		Mount only filesystems with option OPT (-a only)"
//usage:     "\n-o OPT:"
//usage:	IF_FEATURE_MOUNT_LOOP(
//usage:     "\n	loop		Ignored (loop devices are autodetected)"
//usage:	)
//usage:	IF_FEATURE_MOUNT_FLAGS(
//usage:     "\n	[a]sync		Writes are [a]synchronous"
//usage:     "\n	[no]atime	Disable/enable updates to inode access times"
//usage:     "\n	[no]diratime	Disable/enable atime updates to directories"
//usage:     "\n	[no]relatime	Disable/enable atime updates relative to modification time"
//usage:     "\n	[no]dev		(Dis)allow use of special device files"
//usage:     "\n	[no]exec	(Dis)allow use of executable files"
//usage:     "\n	[no]suid	(Dis)allow set-user-id-root programs"
//usage:     "\n	[r]shared	Convert [recursively] to a shared subtree"
//usage:     "\n	[r]slave	Convert [recursively] to a slave subtree"
//usage:     "\n	[r]private	Convert [recursively] to a private subtree"
//usage:     "\n	[un]bindable	Make mount point [un]able to be bind mounted"
//usage:     "\n	[r]bind		Bind a file or directory [recursively] to another location"
//usage:     "\n	move		Relocate an existing mount point"
//usage:	)
//usage:     "\n	remount		Remount a mounted filesystem, changing flags"
//usage:     "\n	ro		Same as -r"
//usage:     "\n"
//usage:     "\nThere are filesystem-specific -o flags."
//usage:
//usage:#define mount_example_usage
//usage:       "$ mount\n"
//usage:       "/dev/hda3 on / type minix (rw)\n"
//usage:       "proc on /proc type proc (rw)\n"
//usage:       "devpts on /dev/pts type devpts (rw)\n"
//usage:       "$ mount /dev/fd0 /mnt -t msdos -o ro\n"
//usage:       "$ mount /tmp/diskimage /opt -t ext2 -o loop\n"
//usage:       "$ mount cd_image.iso mydir\n"
//usage:#define mount_notes_usage
//usage:       "Returns 0 for success, number of failed mounts for -a, or errno for one mount."

#include <mntent.h>
#include <syslog.h>
#include <sys/mount.h>
// Grab more as needed from util-linux's mount/mount_constants.h
#ifndef MS_DIRSYNC
# define MS_DIRSYNC     (1 << 7) // Directory modifications are synchronous
#endif
#ifndef MS_UNION
# define MS_UNION       (1 << 8)
#endif
#ifndef MS_BIND
# define MS_BIND        (1 << 12)
#endif
#ifndef MS_MOVE
# define MS_MOVE        (1 << 13)
#endif
#ifndef MS_RECURSIVE
# define MS_RECURSIVE   (1 << 14)
#endif
#ifndef MS_SILENT
# define MS_SILENT      (1 << 15)
#endif
// The shared subtree stuff, which went in around 2.6.15
#ifndef MS_UNBINDABLE
# define MS_UNBINDABLE  (1 << 17)
#endif
#ifndef MS_PRIVATE
# define MS_PRIVATE     (1 << 18)
#endif
#ifndef MS_SLAVE
# define MS_SLAVE       (1 << 19)
#endif
#ifndef MS_SHARED
# define MS_SHARED      (1 << 20)
#endif
#ifndef MS_RELATIME
# define MS_RELATIME    (1 << 21)
#endif
#ifndef MS_STRICTATIME
# define MS_STRICTATIME (1 << 24)
#endif

/* Any ~MS_FOO value has this bit set: */
#define BB_MS_INVERTED_VALUE (1u << 31)

#include "libbb.h"
#include "common_bufsiz.h"
#if ENABLE_FEATURE_MOUNT_LABEL
# include "volume_id.h"
#else
# define resolve_mount_spec(fsname) ((void)0)
#endif

// Needed for nfs support only
#include <sys/utsname.h>
#undef TRUE
#undef FALSE
#if ENABLE_FEATURE_MOUNT_NFS
/* This is just a warning of a common mistake.  Possibly this should be a
 * uclibc faq entry rather than in busybox... */
# if defined(__UCLIBC__) && ! defined(__UCLIBC_HAS_RPC__)
#  warning "You probably need to build uClibc with UCLIBC_HAS_RPC for NFS support"
   /* not #error, since user may be using e.g. libtirpc instead.
    * This might work:
    * CONFIG_EXTRA_CFLAGS="-I/usr/include/tirpc"
    * CONFIG_EXTRA_LDLIBS="tirpc"
    */
# endif
# include <rpc/rpc.h>
# include <rpc/pmap_prot.h>
# include <rpc/pmap_clnt.h>
#endif


#if defined(__dietlibc__)
// 16.12.2006, Sampo Kellomaki (sampo@iki.fi)
// dietlibc-0.30 does not have implementation of getmntent_r()
static struct mntent *getmntent_r(FILE* stream, struct mntent* result,
		char* buffer UNUSED_PARAM, int bufsize UNUSED_PARAM)
{
	struct mntent* ment = getmntent(stream);
	return memcpy(result, ment, sizeof(*ment));
}
#endif


// Not real flags, but we want to be able to check for this.
enum {
	MOUNT_USERS  = (1 << 27) * ENABLE_DESKTOP,
	MOUNT_NOFAIL = (1 << 28) * ENABLE_DESKTOP,
	MOUNT_NOAUTO = (1 << 29),
	MOUNT_SWAP   = (1 << 30),
	MOUNT_FAKEFLAGS = MOUNT_USERS | MOUNT_NOFAIL | MOUNT_NOAUTO | MOUNT_SWAP
};


#define OPTION_STR "o:*t:rwanfvsiO:" IF_FEATURE_MOUNT_OTHERTAB("T:")
enum {
	OPT_o = (1 << 0),
	OPT_t = (1 << 1),
	OPT_r = (1 << 2),
	OPT_w = (1 << 3),
	OPT_a = (1 << 4),
	OPT_n = (1 << 5),
	OPT_f = (1 << 6),
	OPT_v = (1 << 7),
	OPT_s = (1 << 8),
	OPT_i = (1 << 9),
	OPT_O = (1 << 10),
	OPT_T = (1 << 11),
};

#if ENABLE_FEATURE_MTAB_SUPPORT
#define USE_MTAB (!(option_mask32 & OPT_n))
#else
#define USE_MTAB 0
#endif

#if ENABLE_FEATURE_MOUNT_FAKE
#define FAKE_IT (option_mask32 & OPT_f)
#else
#define FAKE_IT 0
#endif

#if ENABLE_FEATURE_MOUNT_HELPERS
#define HELPERS_ALLOWED (!(option_mask32 & OPT_i))
#else
#define HELPERS_ALLOWED 0
#endif


// TODO: more "user" flag compatibility.
// "user" option (from mount manpage):
// Only the user that mounted a filesystem can unmount it again.
// If any user should be able to unmount, then use users instead of user
// in the fstab line.  The owner option is similar to the user option,
// with the restriction that the user must be the owner of the special file.
// This may be useful e.g. for /dev/fd if a login script makes
// the console user owner of this device.

// Standard mount options (from -o options or --options),
// with corresponding flags
static const int32_t mount_options[] = {
	// MS_FLAGS set a bit.  ~MS_FLAGS disable that bit.  0 flags are NOPs.

	IF_FEATURE_MOUNT_LOOP(
		/* "loop" */ 0,
	)

	IF_FEATURE_MOUNT_FSTAB(
		/* "defaults" */ 0,
		/* "quiet" 0 - do not filter out, vfat wants to see it */
		/* "noauto" */ MOUNT_NOAUTO,
		/* "sw"     */ MOUNT_SWAP,
		/* "swap"   */ MOUNT_SWAP,
		IF_DESKTOP(/* "user"  */ MOUNT_USERS,)
		IF_DESKTOP(/* "users" */ MOUNT_USERS,)
		IF_DESKTOP(/* "nofail" */ MOUNT_NOFAIL,)
		/* "_netdev" */ 0,
		IF_DESKTOP(/* "comment=" */ 0,) /* systemd uses this in fstab */
	)

	IF_FEATURE_MOUNT_FLAGS(
		// vfs flags
		/* "nosuid"      */ MS_NOSUID,
		/* "suid"        */ ~MS_NOSUID,
		/* "dev"         */ ~MS_NODEV,
		/* "nodev"       */ MS_NODEV,
		/* "exec"        */ ~MS_NOEXEC,
		/* "noexec"      */ MS_NOEXEC,
		/* "sync"        */ MS_SYNCHRONOUS,
		/* "dirsync"     */ MS_DIRSYNC,
		/* "async"       */ ~MS_SYNCHRONOUS,
		/* "atime"       */ ~MS_NOATIME,
		/* "noatime"     */ MS_NOATIME,
		/* "diratime"    */ ~MS_NODIRATIME,
		/* "nodiratime"  */ MS_NODIRATIME,
		/* "mand"        */ MS_MANDLOCK,
		/* "nomand"      */ ~MS_MANDLOCK,
		/* "relatime"    */ MS_RELATIME,
		/* "norelatime"  */ ~MS_RELATIME,
		/* "strictatime" */ MS_STRICTATIME,
		/* "loud"        */ ~MS_SILENT,
		/* "rbind"       */ MS_BIND|MS_RECURSIVE,

		// action flags
		/* "union"       */ MS_UNION,
		/* "bind"        */ MS_BIND,
		/* "move"        */ MS_MOVE,
		/* "shared"      */ MS_SHARED,
		/* "slave"       */ MS_SLAVE,
		/* "private"     */ MS_PRIVATE,
		/* "unbindable"  */ MS_UNBINDABLE,
		/* "rshared"     */ MS_SHARED|MS_RECURSIVE,
		/* "rslave"      */ MS_SLAVE|MS_RECURSIVE,
		/* "rprivate"    */ MS_PRIVATE|MS_RECURSIVE,
		/* "runbindable" */ MS_UNBINDABLE|MS_RECURSIVE,
	)

	// Always understood.
	/* "ro"      */ MS_RDONLY,  // vfs flag
	/* "rw"      */ ~MS_RDONLY, // vfs flag
	/* "remount" */ MS_REMOUNT  // action flag
};

static const char mount_option_str[] ALIGN1 =
	IF_FEATURE_MOUNT_LOOP(
		"loop\0"
	)
	IF_FEATURE_MOUNT_FSTAB(
		"defaults\0"
		// "quiet\0" - do not filter out, vfat wants to see it
		"noauto\0"
		"sw\0"
		"swap\0"
		IF_DESKTOP("user\0")
		IF_DESKTOP("users\0")
		IF_DESKTOP("nofail\0")
		"_netdev\0"
		IF_DESKTOP("comment=\0") /* systemd uses this in fstab */
	)
	IF_FEATURE_MOUNT_FLAGS(
		// vfs flags
		"nosuid\0"
		"suid\0"
		"dev\0"
		"nodev\0"
		"exec\0"
		"noexec\0"
		"sync\0"
		"dirsync\0"
		"async\0"
		"atime\0"
		"noatime\0"
		"diratime\0"
		"nodiratime\0"
		"mand\0"
		"nomand\0"
		"relatime\0"
		"norelatime\0"
		"strictatime\0"
		"loud\0"
		"rbind\0"

		// action flags
		"union\0"
		"bind\0"
		"move\0"
		"make-shared\0"
		"make-slave\0"
		"make-private\0"
		"make-unbindable\0"
		"make-rshared\0"
		"make-rslave\0"
		"make-rprivate\0"
		"make-runbindable\0"
	)

	// Always understood.
	"ro\0"        // vfs flag
	"rw\0"        // vfs flag
	"remount\0"   // action flag
;


struct globals {
#if ENABLE_FEATURE_MOUNT_NFS
	smalluint nfs_mount_version;
#endif
#if ENABLE_FEATURE_MOUNT_VERBOSE
	unsigned verbose;
#endif
	llist_t *fslist;
	char getmntent_buf[1];
} FIX_ALIASING;
enum { GETMNTENT_BUFSIZE = COMMON_BUFSIZE - offsetof(struct globals, getmntent_buf) };
#define G (*(struct globals*)bb_common_bufsiz1)
#define nfs_mount_version (G.nfs_mount_version)
#if ENABLE_FEATURE_MOUNT_VERBOSE
#define verbose           (G.verbose          )
#else
#define verbose           0
#endif
#define fslist            (G.fslist           )
#define getmntent_buf     (G.getmntent_buf    )
#define INIT_G() do { setup_common_bufsiz(); } while (0)

#if ENABLE_FEATURE_MTAB_SUPPORT
/*
 * update_mtab_entry_on_move() is used to update entry in case of mount --move.
 * we are looking for existing entries mnt_dir which is equal to mnt_fsname of
 * input mntent and replace it by new one.
 */
static void FAST_FUNC update_mtab_entry_on_move(const struct mntent *mp)
{
	struct mntent *entries, *m;
	int i, count;
	FILE *mountTable;

	mountTable = setmntent(bb_path_mtab_file, "r");
	if (!mountTable) {
		bb_perror_msg(bb_path_mtab_file);
		return;
	}

	entries = NULL;
	count = 0;
	while ((m = getmntent(mountTable)) != NULL) {
		entries = xrealloc_vector(entries, 3, count);
		entries[count].mnt_fsname = xstrdup(m->mnt_fsname);
		entries[count].mnt_dir = xstrdup(m->mnt_dir);
		entries[count].mnt_type = xstrdup(m->mnt_type);
		entries[count].mnt_opts = xstrdup(m->mnt_opts);
		entries[count].mnt_freq = m->mnt_freq;
		entries[count].mnt_passno = m->mnt_passno;
		count++;
	}
	endmntent(mountTable);

	mountTable = setmntent(bb_path_mtab_file, "w");
	if (mountTable) {
		for (i = 0; i < count; i++) {
			if (strcmp(entries[i].mnt_dir, mp->mnt_fsname) != 0)
				addmntent(mountTable, &entries[i]);
			else
				addmntent(mountTable, mp);
		}
		endmntent(mountTable);
	} else if (errno != EROFS)
		bb_perror_msg(bb_path_mtab_file);

	if (ENABLE_FEATURE_CLEAN_UP) {
		for (i = 0; i < count; i++) {
			free(entries[i].mnt_fsname);
			free(entries[i].mnt_dir);
			free(entries[i].mnt_type);
			free(entries[i].mnt_opts);
		}
		free(entries);
	}
}
#endif

#if ENABLE_FEATURE_MOUNT_VERBOSE
static int verbose_mount(const char *source, const char *target,
		const char *filesystemtype,
		unsigned long mountflags, const void *data)
{
	int rc;

	errno = 0;
	rc = mount(source, target, filesystemtype, mountflags, data);
	if (verbose >= 2)
		bb_perror_msg("mount('%s','%s','%s',0x%08lx,'%s'):%d",
			source, target, filesystemtype,
			mountflags, (char*)data, rc);
	return rc;
}
#else
#define verbose_mount(...) mount(__VA_ARGS__)
#endif

// Append mount options to string
static void append_mount_options(char **oldopts, const char *newopts)
{
	if (*oldopts && **oldopts) {
		// Do not insert options which are already there
		while (newopts[0]) {
			char *p;
			int len = strlen(newopts);
			p = strchr(newopts, ',');
			if (p) len = p - newopts;
			p = *oldopts;
			while (1) {
				if (!strncmp(p, newopts, len)
				 && (p[len] == ',' || p[len] == '\0'))
					goto skip;
				p = strchr(p,',');
				if (!p) break;
				p++;
			}
			p = xasprintf("%s,%.*s", *oldopts, len, newopts);
			free(*oldopts);
			*oldopts = p;
 skip:
			newopts += len;
			while (newopts[0] == ',') newopts++;
		}
	} else {
		if (ENABLE_FEATURE_CLEAN_UP) free(*oldopts);
		*oldopts = xstrdup(newopts);
	}
}

// Use the mount_options list to parse options into flags.
// Also update list of unrecognized options if unrecognized != NULL
static unsigned long parse_mount_options(char *options, char **unrecognized)
{
	unsigned long flags = MS_SILENT;

	// Loop through options
	for (;;) {
		unsigned i;
		char *comma = strchr(options, ',');
		const char *option_str = mount_option_str;

		if (comma) *comma = '\0';

// FIXME: use hasmntopt()
		// Find this option in mount_options
		for (i = 0; i < ARRAY_SIZE(mount_options); i++) {
			unsigned opt_len = strlen(option_str);

			if (strncasecmp(option_str, options, opt_len) == 0
			 && (options[opt_len] == '\0'
			    /* or is it "comment=" thingy in fstab? */
			    IF_FEATURE_MOUNT_FSTAB(IF_DESKTOP( || option_str[opt_len-1] == '=' ))
			    )
			) {
				unsigned long fl = mount_options[i];
				if (fl & BB_MS_INVERTED_VALUE)
					flags &= fl;
				else
					flags |= fl;
				goto found;
			}
			option_str += opt_len + 1;
		}
		// We did not recognize this option.
		// If "unrecognized" is not NULL, append option there.
		// Note that we should not append *empty* option -
		// in this case we want to pass NULL, not "", to "data"
		// parameter of mount(2) syscall.
		// This is crucial for filesystems that don't accept
		// any arbitrary mount options, like cgroup fs:
		// "mount -t cgroup none /mnt"
		if (options[0] && unrecognized) {
			// Add it to strflags, to pass on to kernel
			char *p = *unrecognized;
			unsigned len = p ? strlen(p) : 0;
			*unrecognized = p = xrealloc(p, len + strlen(options) + 2);

			// Comma separated if it's not the first one
			if (len) p[len++] = ',';
			strcpy(p + len, options);
		}
 found:
		if (!comma)
			break;
		// Advance to next option
		*comma = ',';
		options = ++comma;
	}

	return flags;
}

// Return a list of all block device backed filesystems
static llist_t *get_block_backed_filesystems(void)
{
	static const char filesystems[2][sizeof("/proc/filesystems")] = {
		"/etc/filesystems",
		"/proc/filesystems",
	};
	char *fs, *buf;
	llist_t *list = NULL;
	int i;
	FILE *f;

	for (i = 0; i < 2; i++) {
		f = fopen_for_read(filesystems[i]);
		if (!f) continue;

		while ((buf = xmalloc_fgetline(f)) != NULL) {
			if (is_prefixed_with(buf, "nodev") && isspace(buf[5]))
				goto next;
			fs = skip_whitespace(buf);
			if (*fs == '#' || *fs == '*' || !*fs)
				goto next;

			llist_add_to_end(&list, xstrdup(fs));
 next:
			free(buf);
		}
		if (ENABLE_FEATURE_CLEAN_UP) fclose(f);
	}

	return list;
}

#if ENABLE_FEATURE_CLEAN_UP
static void delete_block_backed_filesystems(void)
{
	llist_free(fslist, free);
}
#else
void delete_block_backed_filesystems(void);
#endif

// Perform actual mount of specific filesystem at specific location.
// NB: mp->xxx fields may be trashed on exit
static int mount_it_now(struct mntent *mp, unsigned long vfsflags, char *filteropts)
{
	int rc = 0;

	vfsflags &= ~(unsigned long)MOUNT_FAKEFLAGS;

	if (FAKE_IT) {
		if (verbose >= 2)
			bb_error_msg("would do mount('%s','%s','%s',0x%08lx,'%s')",
				mp->mnt_fsname, mp->mnt_dir, mp->mnt_type,
				vfsflags, filteropts);
		goto mtab;
	}

	// Mount, with fallback to read-only if necessary.
	for (;;) {
		errno = 0;
		rc = verbose_mount(mp->mnt_fsname, mp->mnt_dir, mp->mnt_type,
				vfsflags, filteropts);

		// If mount failed, try
		// helper program mount.<mnt_type>
		if (HELPERS_ALLOWED && rc && mp->mnt_type) {
			char *args[8];
			int errno_save = errno;
			args[0] = xasprintf("mount.%s", mp->mnt_type);
			rc = 1;
			if (FAKE_IT)
				args[rc++] = (char *)"-f";
			if (ENABLE_FEATURE_MTAB_SUPPORT && !USE_MTAB)
				args[rc++] = (char *)"-n";
			args[rc++] = mp->mnt_fsname;
			args[rc++] = mp->mnt_dir;
			if (filteropts) {
				args[rc++] = (char *)"-o";
				args[rc++] = filteropts;
			}
			args[rc] = NULL;
			rc = spawn_and_wait(args);
			free(args[0]);
			if (!rc)
				break;
			errno = errno_save;
		}

		if (!rc || (vfsflags & MS_RDONLY) || (errno != EACCES && errno != EROFS))
			break;
		if (!(vfsflags & MS_SILENT))
			bb_error_msg("%s is write-protected, mounting read-only",
						mp->mnt_fsname);
		vfsflags |= MS_RDONLY;
	}

	// Abort entirely if permission denied.

	if (rc && errno == EPERM)
		bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);

	// If the mount was successful, and we're maintaining an old-style
	// mtab file by hand, add the new entry to it now.
 mtab:
	if (USE_MTAB && !rc && !(vfsflags & MS_REMOUNT)) {
		char *fsname;
		FILE *mountTable = setmntent(bb_path_mtab_file, "a+");
		const char *option_str = mount_option_str;
		int i;

		if (!mountTable) {
			bb_perror_msg(bb_path_mtab_file);
			goto ret;
		}

		// Add vfs string flags
		for (i = 0; mount_options[i] != MS_REMOUNT; i++) {
			if (mount_options[i] > 0 && (mount_options[i] & vfsflags))
				append_mount_options(&(mp->mnt_opts), option_str);
			option_str += strlen(option_str) + 1;
		}

		// Remove trailing / (if any) from directory we mounted on
		i = strlen(mp->mnt_dir) - 1;
		while (i > 0 && mp->mnt_dir[i] == '/')
			mp->mnt_dir[i--] = '\0';

		// Convert to canonical pathnames as needed
		mp->mnt_dir = bb_simplify_path(mp->mnt_dir);
		fsname = NULL;
		if (!mp->mnt_type || !*mp->mnt_type) { // bind mount
			mp->mnt_fsname = fsname = bb_simplify_path(mp->mnt_fsname);
			mp->mnt_type = (char*)"bind";
		}
		mp->mnt_freq = mp->mnt_passno = 0;

		// Write and close
#if ENABLE_FEATURE_MTAB_SUPPORT
		if (vfsflags & MS_MOVE)
			update_mtab_entry_on_move(mp);
		else
#endif
			addmntent(mountTable, mp);
		endmntent(mountTable);

		if (ENABLE_FEATURE_CLEAN_UP) {
			free(mp->mnt_dir);
			free(fsname);
		}
	}
 ret:
	return rc;
}

#if ENABLE_FEATURE_MOUNT_NFS

/*
 * Linux NFS mount
 * Copyright (C) 1993 Rick Sladkey <jrs@world.std.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * Wed Feb  8 12:51:48 1995, biro@yggdrasil.com (Ross Biro): allow all port
 * numbers to be specified on the command line.
 *
 * Fri, 8 Mar 1996 18:01:39, Swen Thuemmler <swen@uni-paderborn.de>:
 * Omit the call to connect() for Linux version 1.3.11 or later.
 *
 * Wed Oct  1 23:55:28 1997: Dick Streefland <dick_streefland@tasking.com>
 * Implemented the "bg", "fg" and "retry" mount options for NFS.
 *
 * 1999-02-22 Arkadiusz Mickiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 *
 * Modified by Olaf Kirch and Trond Myklebust for new NFS code,
 * plus NFSv3 stuff.
 */

#define MOUNTPORT 635
#define MNTPATHLEN 1024
#define MNTNAMLEN 255
#define FHSIZE 32
#define FHSIZE3 64

typedef char fhandle[FHSIZE];

typedef struct {
	unsigned int fhandle3_len;
	char *fhandle3_val;
} fhandle3;

enum mountstat3 {
	MNT_OK = 0,
	MNT3ERR_PERM = 1,
	MNT3ERR_NOENT = 2,
	MNT3ERR_IO = 5,
	MNT3ERR_ACCES = 13,
	MNT3ERR_NOTDIR = 20,
	MNT3ERR_INVAL = 22,
	MNT3ERR_NAMETOOLONG = 63,
	MNT3ERR_NOTSUPP = 10004,
	MNT3ERR_SERVERFAULT = 10006,
};
typedef enum mountstat3 mountstat3;

struct fhstatus {
	unsigned int fhs_status;
	union {
		fhandle fhs_fhandle;
	} fhstatus_u;
};
typedef struct fhstatus fhstatus;

struct mountres3_ok {
	fhandle3 fhandle;
	struct {
		unsigned int auth_flavours_len;
		char *auth_flavours_val;
	} auth_flavours;
};
typedef struct mountres3_ok mountres3_ok;

struct mountres3 {
	mountstat3 fhs_status;
	union {
		mountres3_ok mountinfo;
	} mountres3_u;
};
typedef struct mountres3 mountres3;

typedef char *dirpath;

typedef char *name;

typedef struct mountbody *mountlist;

struct mountbody {
	name ml_hostname;
	dirpath ml_directory;
	mountlist ml_next;
};
typedef struct mountbody mountbody;

typedef struct groupnode *groups;

struct groupnode {
	name gr_name;
	groups gr_next;
};
typedef struct groupnode groupnode;

typedef struct exportnode *exports;

struct exportnode {
	dirpath ex_dir;
	groups ex_groups;
	exports ex_next;
};
typedef struct exportnode exportnode;

struct ppathcnf {
	int pc_link_max;
	short pc_max_canon;
	short pc_max_input;
	short pc_name_max;
	short pc_path_max;
	short pc_pipe_buf;
	uint8_t pc_vdisable;
	char pc_xxx;
	short pc_mask[2];
};
typedef struct ppathcnf ppathcnf;

#define MOUNTPROG 100005
#define MOUNTVERS 1

#define MOUNTPROC_NULL 0
#define MOUNTPROC_MNT 1
#define MOUNTPROC_DUMP 2
#define MOUNTPROC_UMNT 3
#define MOUNTPROC_UMNTALL 4
#define MOUNTPROC_EXPORT 5
#define MOUNTPROC_EXPORTALL 6

#define MOUNTVERS_POSIX 2

#define MOUNTPROC_PATHCONF 7

#define MOUNT_V3 3

#define MOUNTPROC3_NULL 0
#define MOUNTPROC3_MNT 1
#define MOUNTPROC3_DUMP 2
#define MOUNTPROC3_UMNT 3
#define MOUNTPROC3_UMNTALL 4
#define MOUNTPROC3_EXPORT 5

enum {
#ifndef NFS_FHSIZE
	NFS_FHSIZE = 32,
#endif
#ifndef NFS_PORT
	NFS_PORT = 2049
#endif
};

/*
 * We want to be able to compile mount on old kernels in such a way
 * that the binary will work well on more recent kernels.
 * Thus, if necessary we teach nfsmount.c the structure of new fields
 * that will come later.
 *
 * Moreover, the new kernel includes conflict with glibc includes
 * so it is easiest to ignore the kernel altogether (at compile time).
 */

struct nfs2_fh {
	char            data[32];
};
struct nfs3_fh {
	unsigned short  size;
	unsigned char   data[64];
};

struct nfs_mount_data {
	int		version;	/* 1 */
	int		fd;		/* 1 */
	struct nfs2_fh	old_root;	/* 1 */
	int		flags;		/* 1 */
	int		rsize;		/* 1 */
	int		wsize;		/* 1 */
	int		timeo;		/* 1 */
	int		retrans;	/* 1 */
	int		acregmin;	/* 1 */
	int		acregmax;	/* 1 */
	int		acdirmin;	/* 1 */
	int		acdirmax;	/* 1 */
	struct sockaddr_in addr;	/* 1 */
	char		hostname[256];	/* 1 */
	int		namlen;		/* 2 */
	unsigned int	bsize;		/* 3 */
	struct nfs3_fh	root;		/* 4 */
};

/* bits in the flags field */
enum {
	NFS_MOUNT_SOFT = 0x0001,	/* 1 */
	NFS_MOUNT_INTR = 0x0002,	/* 1 */
	NFS_MOUNT_SECURE = 0x0004,	/* 1 */
	NFS_MOUNT_POSIX = 0x0008,	/* 1 */
	NFS_MOUNT_NOCTO = 0x0010,	/* 1 */
	NFS_MOUNT_NOAC = 0x0020,	/* 1 */
	NFS_MOUNT_TCP = 0x0040,		/* 2 */
	NFS_MOUNT_VER3 = 0x0080,	/* 3 */
	NFS_MOUNT_KERBEROS = 0x0100,	/* 3 */
	NFS_MOUNT_NONLM = 0x0200,	/* 3 */
	NFS_MOUNT_NOACL = 0x0800,	/* 4 */
	NFS_MOUNT_NORDIRPLUS = 0x4000
};


/*
 * We need to translate between nfs status return values and
 * the local errno values which may not be the same.
 *
 * Andreas Schwab <schwab@LS5.informatik.uni-dortmund.de>: change errno:
 * "after #include <errno.h> the symbol errno is reserved for any use,
 *  it cannot even be used as a struct tag or field name".
 */
#ifndef EDQUOT
# define EDQUOT ENOSPC
#endif
/* Convert each NFSERR_BLAH into EBLAH */
static const uint8_t nfs_err_stat[] ALIGN1 = {
	 1,  2,  5,  6, 13, 17,
	19, 20, 21, 22, 27, 28,
	30, 63, 66, 69, 70, 71
};
#if ( \
	EPERM | ENOENT      | EIO      | ENXIO | EACCES| EEXIST | \
	ENODEV| ENOTDIR     | EISDIR   | EINVAL| EFBIG | ENOSPC | \
	EROFS | ENAMETOOLONG| ENOTEMPTY| EDQUOT| ESTALE| EREMOTE) < 256
typedef uint8_t nfs_err_type;
#else
typedef uint16_t nfs_err_type;
#endif
static const nfs_err_type nfs_err_errnum[] ALIGN2 = {
	EPERM , ENOENT      , EIO      , ENXIO , EACCES, EEXIST,
	ENODEV, ENOTDIR     , EISDIR   , EINVAL, EFBIG , ENOSPC,
	EROFS , ENAMETOOLONG, ENOTEMPTY, EDQUOT, ESTALE, EREMOTE
};
static char *nfs_strerror(int status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nfs_err_stat); i++) {
		if (nfs_err_stat[i] == status)
			return strerror(nfs_err_errnum[i]);
	}
	return xasprintf("unknown nfs status return value: %d", status);
}

static bool_t xdr_fhandle(XDR *xdrs, fhandle objp)
{
	return xdr_opaque(xdrs, objp, FHSIZE);
}

static bool_t xdr_fhstatus(XDR *xdrs, fhstatus *objp)
{
	if (!xdr_u_int(xdrs, &objp->fhs_status))
		return FALSE;
	if (objp->fhs_status == 0)
		return xdr_fhandle(xdrs, objp->fhstatus_u.fhs_fhandle);
	return TRUE;
}

static bool_t xdr_dirpath(XDR *xdrs, dirpath *objp)
{
	return xdr_string(xdrs, objp, MNTPATHLEN);
}

static bool_t xdr_fhandle3(XDR *xdrs, fhandle3 *objp)
{
	return xdr_bytes(xdrs, (char **)&objp->fhandle3_val,
			(unsigned int *) &objp->fhandle3_len,
			FHSIZE3);
}

static bool_t xdr_mountres3_ok(XDR *xdrs, mountres3_ok *objp)
{
	if (!xdr_fhandle3(xdrs, &objp->fhandle))
		return FALSE;
	return xdr_array(xdrs, &(objp->auth_flavours.auth_flavours_val),
			&(objp->auth_flavours.auth_flavours_len),
			~0,
			sizeof(int),
			(xdrproc_t) xdr_int);
}

static bool_t xdr_mountstat3(XDR *xdrs, mountstat3 *objp)
{
	return xdr_enum(xdrs, (enum_t *) objp);
}

static bool_t xdr_mountres3(XDR *xdrs, mountres3 *objp)
{
	if (!xdr_mountstat3(xdrs, &objp->fhs_status))
		return FALSE;
	if (objp->fhs_status == MNT_OK)
		return xdr_mountres3_ok(xdrs, &objp->mountres3_u.mountinfo);
	return TRUE;
}

#define MAX_NFSPROT ((nfs_mount_version >= 4) ? 3 : 2)

/*
 * Unfortunately, the kernel prints annoying console messages
 * in case of an unexpected nfs mount version (instead of
 * just returning some error).  Therefore we'll have to try
 * and figure out what version the kernel expects.
 *
 * Variables:
 *	KERNEL_NFS_MOUNT_VERSION: kernel sources at compile time
 *	NFS_MOUNT_VERSION: these nfsmount sources at compile time
 *	nfs_mount_version: version this source and running kernel can handle
 */
static void
find_kernel_nfs_mount_version(void)
{
	int kernel_version;

	if (nfs_mount_version)
		return;

	nfs_mount_version = 4; /* default */

	kernel_version = get_linux_version_code();
	if (kernel_version) {
		if (kernel_version < KERNEL_VERSION(2,2,18))
			nfs_mount_version = 3;
		/* else v4 since 2.3.99pre4 */
	}
}

static void
get_mountport(struct pmap *pm_mnt,
	struct sockaddr_in *server_addr,
	long unsigned prog,
	long unsigned version,
	long unsigned proto,
	long unsigned port)
{
	struct pmaplist *pmap;

	server_addr->sin_port = PMAPPORT;
/* glibc 2.4 (still) has pmap_getmaps(struct sockaddr_in *).
 * I understand it like "IPv6 for this is not 100% ready" */
	pmap = pmap_getmaps(server_addr);

	if (version > MAX_NFSPROT)
		version = MAX_NFSPROT;
	if (!prog)
		prog = MOUNTPROG;
	pm_mnt->pm_prog = prog;
	pm_mnt->pm_vers = version;
	pm_mnt->pm_prot = proto;
	pm_mnt->pm_port = port;

	while (pmap) {
		if (pmap->pml_map.pm_prog != prog)
			goto next;
		if (!version && pm_mnt->pm_vers > pmap->pml_map.pm_vers)
			goto next;
		if (version > 2 && pmap->pml_map.pm_vers != version)
			goto next;
		if (version && version <= 2 && pmap->pml_map.pm_vers > 2)
			goto next;
		if (pmap->pml_map.pm_vers > MAX_NFSPROT
		 || (proto && pm_mnt->pm_prot && pmap->pml_map.pm_prot != proto)
		 || (port && pmap->pml_map.pm_port != port)
		) {
			goto next;
		}
		memcpy(pm_mnt, &pmap->pml_map, sizeof(*pm_mnt));
 next:
		pmap = pmap->pml_next;
	}
	if (!pm_mnt->pm_vers)
		pm_mnt->pm_vers = MOUNTVERS;
	if (!pm_mnt->pm_port)
		pm_mnt->pm_port = MOUNTPORT;
	if (!pm_mnt->pm_prot)
		pm_mnt->pm_prot = IPPROTO_TCP;
}

#if BB_MMU
static int daemonize(void)
{
	int pid = fork();
	if (pid < 0) /* error */
		return -errno;
	if (pid > 0) /* parent */
		return 0;
	/* child */
	close(0);
	xopen(bb_dev_null, O_RDWR);
	xdup2(0, 1);
	xdup2(0, 2);
	setsid();
	openlog(applet_name, LOG_PID, LOG_DAEMON);
	logmode = LOGMODE_SYSLOG;
	return 1;
}
#else
static inline int daemonize(void) { return -ENOSYS; }
#endif

/* TODO */
static inline int we_saw_this_host_before(const char *hostname UNUSED_PARAM)
{
	return 0;
}

/* RPC strerror analogs are terminally idiotic:
 * *mandatory* prefix and \n at end.
 * This hopefully helps. Usage:
 * error_msg_rpc(clnt_*error*(" ")) */
static void error_msg_rpc(const char *msg)
{
	int len;
	while (msg[0] == ' ' || msg[0] == ':') msg++;
	len = strlen(msg);
	while (len && msg[len-1] == '\n') len--;
	bb_error_msg("%.*s", len, msg);
}

/* NB: mp->xxx fields may be trashed on exit */
static NOINLINE int nfsmount(struct mntent *mp, unsigned long vfsflags, char *filteropts)
{
	CLIENT *mclient;
	char *hostname;
	char *pathname;
	char *mounthost;
	/* prior to 2.6.23, kernel took NFS options in a form of this struct
	 * only. 2.6.23+ looks at data->version, and if it's not 1..6,
	 * then data pointer is interpreted as a string. */
	struct nfs_mount_data data;
	char *opt;
	struct hostent *hp;
	struct sockaddr_in server_addr;
	struct sockaddr_in mount_server_addr;
	int msock, fsock;
	union {
		struct fhstatus nfsv2;
		struct mountres3 nfsv3;
	} status;
	int daemonized;
	char *s;
	int port;
	int mountport;
	int proto;
#if BB_MMU
	smallint bg = 0;
#else
	enum { bg = 0 };
#endif
	int retry;
	int mountprog;
	int mountvers;
	int nfsprog;
	int nfsvers;
	int retval;
	/* these all are one-bit really. gcc 4.3.1 likes this combination: */
	smallint tcp;
	smallint soft;
	int intr;
	int posix;
	int nocto;
	int noac;
	int nordirplus;
	int nolock;
	int noacl;

	find_kernel_nfs_mount_version();

	daemonized = 0;
	mounthost = NULL;
	retval = ETIMEDOUT;
	msock = fsock = -1;
	mclient = NULL;

	/* NB: hostname, mounthost, filteropts must be free()d prior to return */

	filteropts = xstrdup(filteropts); /* going to trash it later... */

	hostname = xstrdup(mp->mnt_fsname);
	/* mount_main() guarantees that ':' is there */
	s = strchr(hostname, ':');
	pathname = s + 1;
	*s = '\0';
	/* Ignore all but first hostname in replicated mounts
	 * until they can be fully supported. (mack@sgi.com) */
	s = strchr(hostname, ',');
	if (s) {
		*s = '\0';
		bb_error_msg("warning: multiple hostnames not supported");
	}

	server_addr.sin_family = AF_INET;
	if (!inet_aton(hostname, &server_addr.sin_addr)) {
		hp = gethostbyname(hostname);
		if (hp == NULL) {
			bb_herror_msg("%s", hostname);
			goto fail;
		}
		if (hp->h_length != (int)sizeof(struct in_addr)) {
			bb_error_msg_and_die("only IPv4 is supported");
		}
		memcpy(&server_addr.sin_addr, hp->h_addr_list[0], sizeof(struct in_addr));
	}

	memcpy(&mount_server_addr, &server_addr, sizeof(mount_server_addr));

	/* add IP address to mtab options for use when unmounting */

	if (!mp->mnt_opts) { /* TODO: actually mp->mnt_opts is never NULL */
		mp->mnt_opts = xasprintf("addr=%s", inet_ntoa(server_addr.sin_addr));
	} else {
		char *tmp = xasprintf("%s%saddr=%s", mp->mnt_opts,
					mp->mnt_opts[0] ? "," : "",
					inet_ntoa(server_addr.sin_addr));
		free(mp->mnt_opts);
		mp->mnt_opts = tmp;
	}

	/* Set default options.
	 * rsize/wsize (and bsize, for ver >= 3) are left 0 in order to
	 * let the kernel decide.
	 * timeo is filled in after we know whether it'll be TCP or UDP. */
	memset(&data, 0, sizeof(data));
	data.retrans  = 3;
	data.acregmin = 3;
	data.acregmax = 60;
	data.acdirmin = 30;
	data.acdirmax = 60;
	data.namlen   = NAME_MAX;

	soft = 0;
	intr = 0;
	posix = 0;
	nocto = 0;
	nolock = 0;
	noac = 0;
	nordirplus = 0;
	noacl = 0;
	retry = 10000;		/* 10000 minutes ~ 1 week */
	tcp = 1;			/* nfs-utils uses tcp per default */

	mountprog = MOUNTPROG;
	mountvers = 0;
	port = 0;
	mountport = 0;
	nfsprog = 100003;
	nfsvers = 0;

	/* parse options */
	if (filteropts)	for (opt = strtok(filteropts, ","); opt; opt = strtok(NULL, ",")) {
		char *opteq = strchr(opt, '=');
		if (opteq) {
			int val, idx;
			static const char options[] ALIGN1 =
				/* 0 */ "rsize\0"
				/* 1 */ "wsize\0"
				/* 2 */ "timeo\0"
				/* 3 */ "retrans\0"
				/* 4 */ "acregmin\0"
				/* 5 */ "acregmax\0"
				/* 6 */ "acdirmin\0"
				/* 7 */ "acdirmax\0"
				/* 8 */ "actimeo\0"
				/* 9 */ "retry\0"
				/* 10 */ "port\0"
				/* 11 */ "mountport\0"
				/* 12 */ "mounthost\0"
				/* 13 */ "mountprog\0"
				/* 14 */ "mountvers\0"
				/* 15 */ "nfsprog\0"
				/* 16 */ "nfsvers\0"
				/* 17 */ "vers\0"
				/* 18 */ "proto\0"
				/* 19 */ "namlen\0"
				/* 20 */ "addr\0";

			*opteq++ = '\0';
			idx = index_in_strings(options, opt);
			switch (idx) {
			case 12: // "mounthost"
				mounthost = xstrndup(opteq,
						strcspn(opteq, " \t\n\r,"));
				continue;
			case 18: // "proto"
				if (is_prefixed_with(opteq, "tcp"))
					tcp = 1;
				else if (is_prefixed_with(opteq, "udp"))
					tcp = 0;
				else
					bb_error_msg("warning: unrecognized proto= option");
				continue;
			case 20: // "addr" - ignore
				continue;
			case -1: // unknown
				if (vfsflags & MS_REMOUNT)
					continue;
			}

			val = xatoi_positive(opteq);
			switch (idx) {
			case 0: // "rsize"
				data.rsize = val;
				continue;
			case 1: // "wsize"
				data.wsize = val;
				continue;
			case 2: // "timeo"
				data.timeo = val;
				continue;
			case 3: // "retrans"
				data.retrans = val;
				continue;
			case 4: // "acregmin"
				data.acregmin = val;
				continue;
			case 5: // "acregmax"
				data.acregmax = val;
				continue;
			case 6: // "acdirmin"
				data.acdirmin = val;
				continue;
			case 7: // "acdirmax"
				data.acdirmax = val;
				continue;
			case 8: // "actimeo"
				data.acregmin = val;
				data.acregmax = val;
				data.acdirmin = val;
				data.acdirmax = val;
				continue;
			case 9: // "retry"
				retry = val;
				continue;
			case 10: // "port"
				port = val;
				continue;
			case 11: // "mountport"
				mountport = val;
				continue;
			case 13: // "mountprog"
				mountprog = val;
				continue;
			case 14: // "mountvers"
				mountvers = val;
				continue;
			case 15: // "nfsprog"
				nfsprog = val;
				continue;
			case 16: // "nfsvers"
			case 17: // "vers"
				nfsvers = val;
				continue;
			case 19: // "namlen"
				//if (nfs_mount_version >= 2)
					data.namlen = val;
				//else
				//	bb_error_msg("warning: option namlen is not supported\n");
				continue;
			default:
				bb_error_msg("unknown nfs mount parameter: %s=%d", opt, val);
				goto fail;
			}
		}
		else { /* not of the form opt=val */
			static const char options[] ALIGN1 =
				"bg\0"
				"fg\0"
				"soft\0"
				"hard\0"
				"intr\0"
				"posix\0"
				"cto\0"
				"ac\0"
				"tcp\0"
				"udp\0"
				"lock\0"
				"rdirplus\0"
				"acl\0";
			int val = 1;
			if (is_prefixed_with(opt, "no")) {
				val = 0;
				opt += 2;
			}
			switch (index_in_strings(options, opt)) {
			case 0: // "bg"
#if BB_MMU
				bg = val;
#endif
				break;
			case 1: // "fg"
#if BB_MMU
				bg = !val;
#endif
				break;
			case 2: // "soft"
				soft = val;
				break;
			case 3: // "hard"
				soft = !val;
				break;
			case 4: // "intr"
				intr = val;
				break;
			case 5: // "posix"
				posix = val;
				break;
			case 6: // "cto"
				nocto = !val;
				break;
			case 7: // "ac"
				noac = !val;
				break;
			case 8: // "tcp"
				tcp = val;
				break;
			case 9: // "udp"
				tcp = !val;
				break;
			case 10: // "lock"
				if (nfs_mount_version >= 3)
					nolock = !val;
				else
					bb_error_msg("warning: option nolock is not supported");
				break;
			case 11: //rdirplus
				nordirplus = !val;
				break;
			case 12: // acl
				noacl = !val;
				break;
			default:
				bb_error_msg("unknown nfs mount option: %s%s", val ? "" : "no", opt);
				goto fail;
			}
		}
	}
	proto = (tcp) ? IPPROTO_TCP : IPPROTO_UDP;

	data.flags = (soft ? NFS_MOUNT_SOFT : 0)
		| (intr ? NFS_MOUNT_INTR : 0)
		| (posix ? NFS_MOUNT_POSIX : 0)
		| (nocto ? NFS_MOUNT_NOCTO : 0)
		| (noac ? NFS_MOUNT_NOAC : 0)
		| (nordirplus ? NFS_MOUNT_NORDIRPLUS : 0)
		| (noacl ? NFS_MOUNT_NOACL : 0);
	if (nfs_mount_version >= 2)
		data.flags |= (tcp ? NFS_MOUNT_TCP : 0);
	if (nfs_mount_version >= 3)
		data.flags |= (nolock ? NFS_MOUNT_NONLM : 0);
	if (nfsvers > MAX_NFSPROT || mountvers > MAX_NFSPROT) {
		bb_error_msg("NFSv%d not supported", nfsvers);
		goto fail;
	}
	if (nfsvers && !mountvers)
		mountvers = (nfsvers < 3) ? 1 : nfsvers;
	if (nfsvers && nfsvers < mountvers) {
		mountvers = nfsvers;
	}

	/* Adjust options if none specified */
	if (!data.timeo)
		data.timeo = tcp ? 70 : 7;

	data.version = nfs_mount_version;

	if (vfsflags & MS_REMOUNT)
		goto do_mount;

	/*
	 * If the previous mount operation on the same host was
	 * backgrounded, and the "bg" for this mount is also set,
	 * give up immediately, to avoid the initial timeout.
	 */
	if (bg && we_saw_this_host_before(hostname)) {
		daemonized = daemonize();
		if (daemonized <= 0) { /* parent or error */
			retval = -daemonized;
			goto ret;
		}
	}

	/* Create mount daemon client */
	/* See if the nfs host = mount host. */
	if (mounthost) {
		if (mounthost[0] >= '0' && mounthost[0] <= '9') {
			mount_server_addr.sin_family = AF_INET;
			mount_server_addr.sin_addr.s_addr = inet_addr(hostname);
		} else {
			hp = gethostbyname(mounthost);
			if (hp == NULL) {
				bb_herror_msg("%s", mounthost);
				goto fail;
			}
			if (hp->h_length != (int)sizeof(struct in_addr)) {
				bb_error_msg_and_die("only IPv4 is supported");
			}
			mount_server_addr.sin_family = AF_INET;
			memcpy(&mount_server_addr.sin_addr, hp->h_addr_list[0], sizeof(struct in_addr));
		}
	}

	/*
	 * The following loop implements the mount retries. When the mount
	 * times out, and the "bg" option is set, we background ourself
	 * and continue trying.
	 *
	 * The case where the mount point is not present and the "bg"
	 * option is set, is treated as a timeout. This is done to
	 * support nested mounts.
	 *
	 * The "retry" count specified by the user is the number of
	 * minutes to retry before giving up.
	 */
	{
		struct timeval total_timeout;
		struct timeval retry_timeout;
		struct pmap pm_mnt;
		time_t t;
		time_t prevt;
		time_t timeout;

		retry_timeout.tv_sec = 3;
		retry_timeout.tv_usec = 0;
		total_timeout.tv_sec = 20;
		total_timeout.tv_usec = 0;
/* FIXME: use monotonic()? */
		timeout = time(NULL) + 60 * retry;
		prevt = 0;
		t = 30;
 retry:
		/* Be careful not to use too many CPU cycles */
		if (t - prevt < 30)
			sleep(30);

		get_mountport(&pm_mnt, &mount_server_addr,
				mountprog,
				mountvers,
				proto,
				mountport);
		nfsvers = (pm_mnt.pm_vers < 2) ? 2 : pm_mnt.pm_vers;

		/* contact the mount daemon via TCP */
		mount_server_addr.sin_port = htons(pm_mnt.pm_port);
		msock = RPC_ANYSOCK;

		switch (pm_mnt.pm_prot) {
		case IPPROTO_UDP:
			mclient = clntudp_create(&mount_server_addr,
						pm_mnt.pm_prog,
						pm_mnt.pm_vers,
						retry_timeout,
						&msock);
			if (mclient)
				break;
			mount_server_addr.sin_port = htons(pm_mnt.pm_port);
			msock = RPC_ANYSOCK;
		case IPPROTO_TCP:
			mclient = clnttcp_create(&mount_server_addr,
						pm_mnt.pm_prog,
						pm_mnt.pm_vers,
						&msock, 0, 0);
			break;
		default:
			mclient = NULL;
		}
		if (!mclient) {
			if (!daemonized && prevt == 0)
				error_msg_rpc(clnt_spcreateerror(" "));
		} else {
			enum clnt_stat clnt_stat;

			/* Try to mount hostname:pathname */
			mclient->cl_auth = authunix_create_default();

			/* Make pointers in xdr_mountres3 NULL so
			 * that xdr_array allocates memory for us
			 */
			memset(&status, 0, sizeof(status));

			if (pm_mnt.pm_vers == 3)
				clnt_stat = clnt_call(mclient, MOUNTPROC3_MNT,
						(xdrproc_t) xdr_dirpath,
						(caddr_t) &pathname,
						(xdrproc_t) xdr_mountres3,
						(caddr_t) &status,
						total_timeout);
			else
				clnt_stat = clnt_call(mclient, MOUNTPROC_MNT,
						(xdrproc_t) xdr_dirpath,
						(caddr_t) &pathname,
						(xdrproc_t) xdr_fhstatus,
						(caddr_t) &status,
						total_timeout);

			if (clnt_stat == RPC_SUCCESS)
				goto prepare_kernel_data; /* we're done */
			if (errno != ECONNREFUSED) {
				error_msg_rpc(clnt_sperror(mclient, " "));
				goto fail;	/* don't retry */
			}
			/* Connection refused */
			if (!daemonized && prevt == 0) /* print just once */
				error_msg_rpc(clnt_sperror(mclient, " "));
			auth_destroy(mclient->cl_auth);
			clnt_destroy(mclient);
			mclient = NULL;
			close(msock);
			msock = -1;
		}

		/* Timeout. We are going to retry... maybe */
		if (!bg)
			goto fail;
		if (!daemonized) {
			daemonized = daemonize();
			if (daemonized <= 0) { /* parent or error */
				retval = -daemonized;
				goto ret;
			}
		}
		prevt = t;
		t = time(NULL);
		if (t >= timeout)
			/* TODO error message */
			goto fail;

		goto retry;
	}

 prepare_kernel_data:

	if (nfsvers == 2) {
		if (status.nfsv2.fhs_status != 0) {
			bb_error_msg("%s:%s failed, reason given by server: %s",
				hostname, pathname,
				nfs_strerror(status.nfsv2.fhs_status));
			goto fail;
		}
		memcpy(data.root.data,
				(char *) status.nfsv2.fhstatus_u.fhs_fhandle,
				NFS_FHSIZE);
		data.root.size = NFS_FHSIZE;
		memcpy(data.old_root.data,
				(char *) status.nfsv2.fhstatus_u.fhs_fhandle,
				NFS_FHSIZE);
	} else {
		fhandle3 *my_fhandle;
		if (status.nfsv3.fhs_status != 0) {
			bb_error_msg("%s:%s failed, reason given by server: %s",
				hostname, pathname,
				nfs_strerror(status.nfsv3.fhs_status));
			goto fail;
		}
		my_fhandle = &status.nfsv3.mountres3_u.mountinfo.fhandle;
		memset(data.old_root.data, 0, NFS_FHSIZE);
		memset(&data.root, 0, sizeof(data.root));
		data.root.size = my_fhandle->fhandle3_len;
		memcpy(data.root.data,
				(char *) my_fhandle->fhandle3_val,
				my_fhandle->fhandle3_len);

		data.flags |= NFS_MOUNT_VER3;
	}

	/* Create nfs socket for kernel */
	if (tcp) {
		if (nfs_mount_version < 3) {
			bb_error_msg("NFS over TCP is not supported");
			goto fail;
		}
		fsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	} else
		fsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fsock < 0) {
		bb_perror_msg("nfs socket");
		goto fail;
	}
	if (bindresvport(fsock, 0) < 0) {
		bb_perror_msg("nfs bindresvport");
		goto fail;
	}
	if (port == 0) {
		server_addr.sin_port = PMAPPORT;
		port = pmap_getport(&server_addr, nfsprog, nfsvers,
					tcp ? IPPROTO_TCP : IPPROTO_UDP);
		if (port == 0)
			port = NFS_PORT;
	}
	server_addr.sin_port = htons(port);

	/* Prepare data structure for kernel */
	data.fd = fsock;
	memcpy((char *) &data.addr, (char *) &server_addr, sizeof(data.addr));
	strncpy(data.hostname, hostname, sizeof(data.hostname));

	/* Clean up */
	auth_destroy(mclient->cl_auth);
	clnt_destroy(mclient);
	close(msock);
	msock = -1;

	if (bg) {
		/* We must wait until mount directory is available */
		struct stat statbuf;
		int delay = 1;
		while (stat(mp->mnt_dir, &statbuf) == -1) {
			if (!daemonized) {
				daemonized = daemonize();
				if (daemonized <= 0) { /* parent or error */
/* FIXME: parent doesn't close fsock - ??! */
					retval = -daemonized;
					goto ret;
				}
			}
			sleep(delay);	/* 1, 2, 4, 8, 16, 30, ... */
			delay *= 2;
			if (delay > 30)
				delay = 30;
		}
	}

	/* Perform actual mount */
 do_mount:
	retval = mount_it_now(mp, vfsflags, (char*)&data);
	goto ret;

	/* Abort */
 fail:
	if (msock >= 0) {
		if (mclient) {
			auth_destroy(mclient->cl_auth);
			clnt_destroy(mclient);
		}
		close(msock);
	}
	if (fsock >= 0)
		close(fsock);

 ret:
	free(hostname);
	free(mounthost);
	free(filteropts);
	return retval;
}

#else // !ENABLE_FEATURE_MOUNT_NFS

/* Linux 2.6.23+ supports nfs mounts with options passed as a string.
 * For older kernels, you must build busybox with ENABLE_FEATURE_MOUNT_NFS.
 * (However, note that then you lose any chances that NFS over IPv6 would work).
 */
static int nfsmount(struct mntent *mp, unsigned long vfsflags, char *filteropts)
{
	len_and_sockaddr *lsa;
	char *opts;
	char *end;
	char *dotted;
	int ret;

# if ENABLE_FEATURE_IPV6
	end = strchr(mp->mnt_fsname, ']');
	if (end && end[1] == ':')
		end++;
	else
# endif
		/* mount_main() guarantees that ':' is there */
		end = strchr(mp->mnt_fsname, ':');

	*end = '\0';
	lsa = xhost2sockaddr(mp->mnt_fsname, /*port:*/ 0);
	*end = ':';
	dotted = xmalloc_sockaddr2dotted_noport(&lsa->u.sa);
	if (ENABLE_FEATURE_CLEAN_UP) free(lsa);
	opts = xasprintf("%s%saddr=%s",
		filteropts ? filteropts : "",
		filteropts ? "," : "",
		dotted
	);
	if (ENABLE_FEATURE_CLEAN_UP) free(dotted);
	ret = mount_it_now(mp, vfsflags, opts);
	if (ENABLE_FEATURE_CLEAN_UP) free(opts);

	return ret;
}

#endif // !ENABLE_FEATURE_MOUNT_NFS

// Mount one directory.  Handles CIFS, NFS, loopback, autobind, and filesystem
// type detection.  Returns 0 for success, nonzero for failure.
// NB: mp->xxx fields may be trashed on exit
static int singlemount(struct mntent *mp, int ignore_busy)
{
	int loopfd = -1;
	int rc = -1;
	unsigned long vfsflags;
	char *loopFile = NULL, *filteropts = NULL;
	llist_t *fl = NULL;
	struct stat st;

	errno = 0;

	vfsflags = parse_mount_options(mp->mnt_opts, &filteropts);

	// Treat fstype "auto" as unspecified
	if (mp->mnt_type && strcmp(mp->mnt_type, "auto") == 0)
		mp->mnt_type = NULL;

	// Might this be a virtual filesystem?
	if (ENABLE_FEATURE_MOUNT_HELPERS && strchr(mp->mnt_fsname, '#')) {
		char *args[35];
		char *s;
		int n;
		// fsname: "cmd#arg1#arg2..."
		// WARNING: allows execution of arbitrary commands!
		// Try "mount 'sh#-c#sh' bogus_dir".
		// It is safe ONLY because non-root
		// cannot use two-argument mount command
		// and using one-argument "mount 'sh#-c#sh'" doesn't work:
		// "mount: can't find sh#-c#sh in /etc/fstab"
		// (if /etc/fstab has it, it's ok: root sets up /etc/fstab).

		s = mp->mnt_fsname;
		n = 0;
		args[n++] = s;
		while (*s && n < 35 - 2) {
			if (*s++ == '#' && *s != '#') {
				s[-1] = '\0';
				args[n++] = s;
			}
		}
		args[n++] = mp->mnt_dir;
		args[n] = NULL;
		rc = spawn_and_wait(args);
		goto report_error;
	}

	// Might this be an CIFS filesystem?
	if (ENABLE_FEATURE_MOUNT_CIFS
	 && (!mp->mnt_type || strcmp(mp->mnt_type, "cifs") == 0)
	 && (mp->mnt_fsname[0] == '/' || mp->mnt_fsname[0] == '\\')
	 && mp->mnt_fsname[0] == mp->mnt_fsname[1]
	) {
		int len;
		char c;
		char *hostname, *share;
		len_and_sockaddr *lsa;

		// Parse mp->mnt_fsname of the form "//hostname/share[/dir1/dir2]"

		hostname = mp->mnt_fsname + 2;
		len = strcspn(hostname, "/\\");
		share = hostname + len + 1;
		if (len == 0          // 3rd char is a [back]slash (IOW: empty hostname)
		 || share[-1] == '\0' // no [back]slash after hostname
		 || share[0] == '\0'  // empty share name
		) {
			goto report_error;
		}
		c = share[-1];
		share[-1] = '\0';
		len = strcspn(share, "/\\");

		// "unc=\\hostname\share" option is mandatory
		// after CIFS option parsing was rewritten in Linux 3.4.
		// Must use backslashes.
		// If /dir1/dir2 is present, also add "prefixpath=dir1/dir2"
		{
			char *unc = xasprintf(
				share[len] != '\0'  /* "/dir1/dir2" exists? */
					? "unc=\\\\%s\\%.*s,prefixpath=%s"
					: "unc=\\\\%s\\%.*s",
				hostname,
				len, share,
				share + len + 1  /* "dir1/dir2" */
			);
			parse_mount_options(unc, &filteropts);
			if (ENABLE_FEATURE_CLEAN_UP) free(unc);
		}

		lsa = host2sockaddr(hostname, 0);
		share[-1] = c;
		if (!lsa)
			goto report_error;

		// If there is no "ip=..." option yet
		if (!is_prefixed_with(filteropts, ",ip="+1)
		 && !strstr(filteropts, ",ip=")
		) {
			char *dotted, *ip;
			// Insert "ip=..." option into options
			dotted = xmalloc_sockaddr2dotted_noport(&lsa->u.sa);
			if (ENABLE_FEATURE_CLEAN_UP) free(lsa);
			ip = xasprintf("ip=%s", dotted);
			if (ENABLE_FEATURE_CLEAN_UP) free(dotted);
// Note: IPv6 scoped addresses ("host%iface", see RFC 4007) should be
// handled by libc in getnameinfo() (inside xmalloc_sockaddr2dotted_noport()).
// Currently, glibc does not support that (has no NI_NUMERICSCOPE),
// musl apparently does. This results in "ip=numericIPv6%iface_name"
// (instead of _numeric_ iface_id) with glibc.
// This probably should be fixed in glibc, not here.
// The workaround is to manually specify correct "ip=ADDR%n" option.
			parse_mount_options(ip, &filteropts);
			if (ENABLE_FEATURE_CLEAN_UP) free(ip);
		}

		mp->mnt_type = (char*)"cifs";
		rc = mount_it_now(mp, vfsflags, filteropts);

		goto report_error;
	}

	// Might this be an NFS filesystem?
	if ((!mp->mnt_type || is_prefixed_with(mp->mnt_type, "nfs"))
	 && strchr(mp->mnt_fsname, ':') != NULL
	) {
		if (!mp->mnt_type)
			mp->mnt_type = (char*)"nfs";
		rc = nfsmount(mp, vfsflags, filteropts);
		goto report_error;
	}

	// Look at the file.  (Not found isn't a failure for remount, or for
	// a synthetic filesystem like proc or sysfs.)
	// (We use stat, not lstat, in order to allow
	// mount symlink_to_file_or_blkdev dir)
	if (!stat(mp->mnt_fsname, &st)
	 && !(vfsflags & (MS_REMOUNT | MS_BIND | MS_MOVE))
	) {
		// Do we need to allocate a loopback device for it?
		if (ENABLE_FEATURE_MOUNT_LOOP && S_ISREG(st.st_mode)) {
			loopFile = bb_simplify_path(mp->mnt_fsname);
			mp->mnt_fsname = NULL; // will receive malloced loop dev name

			// mount always creates AUTOCLEARed loopdevs, so that umounting
			// drops them without any code in the userspace.
			// This happens since circa linux-2.6.25:
			// commit 96c5865559cee0f9cbc5173f3c949f6ce3525581
			// Date:    Wed Feb 6 01:36:27 2008 -0800
			// Subject: Allow auto-destruction of loop devices
			loopfd = set_loop(&mp->mnt_fsname,
					loopFile,
					0,
					((vfsflags & MS_RDONLY) ? BB_LO_FLAGS_READ_ONLY : 0)
						| BB_LO_FLAGS_AUTOCLEAR
			);
			if (loopfd < 0) {
				if (errno == EPERM || errno == EACCES)
					bb_error_msg(bb_msg_perm_denied_are_you_root);
				else
					bb_perror_msg("can't setup loop device");
				return errno;
			}

		// Autodetect bind mounts
		} else if (S_ISDIR(st.st_mode) && !mp->mnt_type)
			vfsflags |= MS_BIND;
	}

	// If we know the fstype (or don't need to), jump straight
	// to the actual mount.
	if (mp->mnt_type || (vfsflags & (MS_REMOUNT | MS_BIND | MS_MOVE))) {
		char *next;
		for (;;) {
			next = mp->mnt_type ? strchr(mp->mnt_type, ',') : NULL;
			if (next)
				*next = '\0';
			rc = mount_it_now(mp, vfsflags, filteropts);
			if (rc == 0 || !next)
				break;
			mp->mnt_type = next + 1;
		}
	} else {
		// Loop through filesystem types until mount succeeds
		// or we run out

		// Initialize list of block backed filesystems.
		// This has to be done here so that during "mount -a",
		// mounts after /proc shows up can autodetect.
		if (!fslist) {
			fslist = get_block_backed_filesystems();
			if (ENABLE_FEATURE_CLEAN_UP && fslist)
				atexit(delete_block_backed_filesystems);
		}

		for (fl = fslist; fl; fl = fl->link) {
			mp->mnt_type = fl->data;
			rc = mount_it_now(mp, vfsflags, filteropts);
			if (rc == 0)
				break;
		}
	}

	// If mount failed, clean up loop file (if any).
	// (Newer kernels which support LO_FLAGS_AUTOCLEAR should not need this,
	// merely "close(loopfd)" should do it?)
	if (ENABLE_FEATURE_MOUNT_LOOP && rc && loopFile) {
		del_loop(mp->mnt_fsname);
		if (ENABLE_FEATURE_CLEAN_UP) {
			free(loopFile);
			/* No, "rc != 0" needs it: free(mp->mnt_fsname); */
		}
	}

 report_error:
	if (ENABLE_FEATURE_CLEAN_UP)
		free(filteropts);

	if (loopfd >= 0)
		close(loopfd);

	if (errno == EBUSY && ignore_busy)
		return 0;
	if (errno == ENOENT && (vfsflags & MOUNT_NOFAIL))
		return 0;
	if (rc != 0)
		bb_perror_msg("mounting %s on %s failed", mp->mnt_fsname, mp->mnt_dir);
	return rc;
}

// -O support
//    -O interprets a list of filter options which select whether a mount
// point will be mounted: only mounts with options matching *all* filtering
// options will be selected.
//    By default each -O filter option must be present in the list of mount
// options, but if it is prefixed by "no" then it must be absent.
// For example,
//  -O a,nob,c  matches  -o a,c  but fails to match  -o a,b,c
//              (and also fails to match  -o a  because  -o c  is absent).
//
// It is different from -t in that each option is matched exactly; a leading
// "no" at the beginning of one option does not negate the rest.
static int match_opt(const char *fs_opt_in, const char *O_opt)
{
	if (!O_opt)
		return 1;

	while (*O_opt) {
		const char *fs_opt = fs_opt_in;
		int O_len;
		int match;

		// If option begins with "no" then treat as an inverted match:
		// matching is a failure
		match = 0;
		if (O_opt[0] == 'n' && O_opt[1] == 'o') {
			match = 1;
			O_opt += 2;
		}
		// Isolate the current O option
		O_len = strchrnul(O_opt, ',') - O_opt;
		// Check for a match against existing options
		while (1) {
			if (strncmp(fs_opt, O_opt, O_len) == 0
			 && (fs_opt[O_len] == '\0' || fs_opt[O_len] == ',')
			) {
				if (match)
					return 0;  // "no" prefix, but option found
				match = 1;  // current O option found, go check next one
				break;
			}
			fs_opt = strchr(fs_opt, ',');
			if (!fs_opt)
				break;
			fs_opt++;
		}
		if (match == 0)
			return 0;     // match wanted but not found
		if (O_opt[O_len] == '\0') // end?
			break;
		// Step to the next O option
		O_opt += O_len + 1;
	}
	// If we get here then everything matched
	return 1;
}

// Parse options, if necessary parse fstab/mtab, and call singlemount for
// each directory to be mounted.
int mount_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mount_main(int argc UNUSED_PARAM, char **argv)
{
	char *cmdopts = xzalloc(1);
	char *fstype = NULL;
	char *O_optmatch = NULL;
	char *storage_path;
	llist_t *lst_o = NULL;
	const char *fstabname = "/etc/fstab";
	FILE *fstab;
	int i, j;
	int rc = EXIT_SUCCESS;
	unsigned long cmdopt_flags;
	unsigned opt;
	struct mntent mtpair[2], *mtcur = mtpair;
	IF_NOT_DESKTOP(const int nonroot = 0;)

	IF_DESKTOP(int nonroot = ) sanitize_env_if_suid();

	INIT_G();

	// Parse long options, like --bind and --move.  Note that -o option
	// and --option are synonymous.  Yes, this means --remount,rw works.
	for (i = j = 1; argv[i]; i++) {
		if (argv[i][0] == '-' && argv[i][1] == '-')
			append_mount_options(&cmdopts, argv[i] + 2);
		else
			argv[j++] = argv[i];
	}
	argv[j] = NULL;

	// Parse remaining options
	// Max 2 params; -o is a list, -v is a counter
	opt = getopt32(argv, "^"
		OPTION_STR
		"\0" "?2"IF_FEATURE_MOUNT_VERBOSE("vv"),
		&lst_o, &fstype, &O_optmatch
		IF_FEATURE_MOUNT_OTHERTAB(, &fstabname)
		IF_FEATURE_MOUNT_VERBOSE(, &verbose)
	);

	while (lst_o) append_mount_options(&cmdopts, llist_pop(&lst_o)); // -o
	if (opt & OPT_r) append_mount_options(&cmdopts, "ro"); // -r
	if (opt & OPT_w) append_mount_options(&cmdopts, "rw"); // -w
	argv += optind;

	// If we have no arguments, show currently mounted filesystems
	if (!argv[0]) {
		if (!(opt & OPT_a)) {
			FILE *mountTable = setmntent(bb_path_mtab_file, "r");

			if (!mountTable)
				bb_error_msg_and_die("no %s", bb_path_mtab_file);

			while (getmntent_r(mountTable, &mtpair[0], getmntent_buf,
								GETMNTENT_BUFSIZE))
			{
				// Don't show rootfs. FIXME: why??
				// util-linux 2.12a happily shows rootfs...
				//if (strcmp(mtpair->mnt_fsname, "rootfs") == 0) continue;

				if (!fstype || strcmp(mtpair->mnt_type, fstype) == 0)
					printf("%s on %s type %s (%s)\n", mtpair->mnt_fsname,
							mtpair->mnt_dir, mtpair->mnt_type,
							mtpair->mnt_opts);
			}
			if (ENABLE_FEATURE_CLEAN_UP)
				endmntent(mountTable);
			return EXIT_SUCCESS;
		}
		storage_path = NULL;
	} else {
		// When we have two arguments, the second is the directory and we can
		// skip looking at fstab entirely.  We can always abspath() the directory
		// argument when we get it.
		if (argv[1]) {
			if (nonroot)
				bb_error_msg_and_die(bb_msg_you_must_be_root);
			mtpair->mnt_fsname = argv[0];
			mtpair->mnt_dir = argv[1];
			mtpair->mnt_type = fstype;
			mtpair->mnt_opts = cmdopts;
			resolve_mount_spec(&mtpair->mnt_fsname);
			rc = singlemount(mtpair, /*ignore_busy:*/ 0);
			return rc;
		}
		storage_path = bb_simplify_path(argv[0]); // malloced
	}

	// Past this point, we are handling either "mount -a [opts]"
	// or "mount [opts] single_param"

	cmdopt_flags = parse_mount_options(cmdopts, NULL);
	if (nonroot && (cmdopt_flags & ~MS_SILENT)) // Non-root users cannot specify flags
		bb_error_msg_and_die(bb_msg_you_must_be_root);

	// If we have a shared subtree flag, don't worry about fstab or mtab.
	if (ENABLE_FEATURE_MOUNT_FLAGS
	 && (cmdopt_flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE))
	) {
		// verbose_mount(source, target, type, flags, data)
		rc = verbose_mount("", argv[0], "", cmdopt_flags, "");
		if (rc)
			bb_simple_perror_msg_and_die(argv[0]);
		return rc;
	}

	// A malicious user could overmount /usr without this.
	if (ENABLE_FEATURE_MOUNT_OTHERTAB && nonroot)
		fstabname = "/etc/fstab";
	// Open either fstab or mtab
	if (cmdopt_flags & MS_REMOUNT) {
		// WARNING. I am not sure this matches util-linux's
		// behavior. It's possible util-linux does not
		// take -o opts from mtab (takes only mount source).
		fstabname = bb_path_mtab_file;
	}
	fstab = setmntent(fstabname, "r");
	if (!fstab)
		bb_perror_msg_and_die("can't read '%s'", fstabname);

	// Loop through entries until we find what we're looking for
	memset(mtpair, 0, sizeof(mtpair));
	for (;;) {
		struct mntent *mtother = (mtcur==mtpair ? mtpair+1 : mtpair);

		// Get next fstab entry
		if (!getmntent_r(fstab, mtcur, getmntent_buf
					+ (mtcur==mtpair ? GETMNTENT_BUFSIZE/2 : 0),
				GETMNTENT_BUFSIZE/2)
		) { // End of fstab/mtab is reached
			mtcur = mtother; // the thing we found last time
			break;
		}

		// If we're trying to mount something specific and this isn't it,
		// skip it.  Note we must match the exact text in fstab (ala
		// "proc") or a full path from root
		if (argv[0]) {

			// Is this what we're looking for?
			if (strcmp(argv[0], mtcur->mnt_fsname) != 0
			 && strcmp(storage_path, mtcur->mnt_fsname) != 0
			 && strcmp(argv[0], mtcur->mnt_dir) != 0
			 && strcmp(storage_path, mtcur->mnt_dir) != 0
			) {
				continue; // no
			}

			// Remember this entry.  Something later may have
			// overmounted it, and we want the _last_ match.
			mtcur = mtother;

		// If we're mounting all
		} else {
			struct mntent *mp;
			// No, mount -a won't mount anything,
			// even user mounts, for mere humans
			if (nonroot)
				bb_error_msg_and_die(bb_msg_you_must_be_root);

			// Does type match? (NULL matches always)
			if (!fstype_matches(mtcur->mnt_type, fstype))
				continue;

			// Skip noauto and swap anyway
			if ((parse_mount_options(mtcur->mnt_opts, NULL) & (MOUNT_NOAUTO | MOUNT_SWAP))
			// swap is bogus "fstype", parse_mount_options can't check fstypes
			 || strcasecmp(mtcur->mnt_type, "swap") == 0
			) {
				continue;
			}

			// Does (at least one) option match?
			// (NULL matches always)
			if (!match_opt(mtcur->mnt_opts, O_optmatch))
				continue;

			resolve_mount_spec(&mtcur->mnt_fsname);

			// NFS mounts want this to be xrealloc-able
			mtcur->mnt_opts = xstrdup(mtcur->mnt_opts);

			// If nothing is mounted on this directory...
			// (otherwise repeated "mount -a" mounts everything again)
			mp = find_mount_point(mtcur->mnt_dir, /*subdir_too:*/ 0);
			// We do not check fsname match of found mount point -
			// "/" may have fsname of "/dev/root" while fstab
			// says "/dev/something_else".
			if (mp) {
				if (verbose) {
					bb_error_msg("according to %s, "
						"%s is already mounted on %s",
						bb_path_mtab_file,
						mp->mnt_fsname, mp->mnt_dir);
				}
			} else {
				// ...mount this thing
				if (singlemount(mtcur, /*ignore_busy:*/ 1)) {
					// Count number of failed mounts
					rc++;
				}
			}
			free(mtcur->mnt_opts);
		}
	}

	// End of fstab/mtab is reached.
	// Were we looking for something specific?
	if (argv[0]) { // yes
		unsigned long l;

		// If we didn't find anything, complain
		if (!mtcur->mnt_fsname)
			bb_error_msg_and_die("can't find %s in %s",
				argv[0], fstabname);

		// What happens when we try to "mount swap_partition"?
		// (fstab containts "swap_partition swap swap defaults 0 0")
		// util-linux-ng 2.13.1 does this:
		// stat("/sbin/mount.swap", 0x7fff62a3a350) = -1 ENOENT (No such file or directory)
		// mount("swap_partition", "swap", "swap", MS_MGC_VAL, NULL) = -1 ENOENT (No such file or directory)
		// lstat("swap", 0x7fff62a3a640)           = -1 ENOENT (No such file or directory)
		// write(2, "mount: mount point swap does not exist\n", 39) = 39
		// exit_group(32)                          = ?
#if 0
		// In case we want to simply skip swap partitions:
		l = parse_mount_options(mtcur->mnt_opts, NULL);
		if ((l & MOUNT_SWAP)
		// swap is bogus "fstype", parse_mount_options can't check fstypes
		 || strcasecmp(mtcur->mnt_type, "swap") == 0
		) {
			goto ret;
		}
#endif
		if (nonroot) {
			// fstab must have "users" or "user"
			l = parse_mount_options(mtcur->mnt_opts, NULL);
			if (!(l & MOUNT_USERS))
				bb_error_msg_and_die(bb_msg_you_must_be_root);
		}

		//util-linux-2.12 does not do this check.
		//// If nothing is mounted on this directory...
		//// (otherwise repeated "mount FOO" mounts FOO again)
		//mp = find_mount_point(mtcur->mnt_dir, /*subdir_too:*/ 0);
		//if (mp) {
		//	bb_error_msg("according to %s, "
		//		"%s is already mounted on %s",
		//		bb_path_mtab_file,
		//		mp->mnt_fsname, mp->mnt_dir);
		//} else {
			// ...mount the last thing we found
			mtcur->mnt_opts = xstrdup(mtcur->mnt_opts);
			append_mount_options(&(mtcur->mnt_opts), cmdopts);
			resolve_mount_spec(&mtpair->mnt_fsname);
			rc = singlemount(mtcur, /*ignore_busy:*/ 0);
			if (ENABLE_FEATURE_CLEAN_UP)
				free(mtcur->mnt_opts);
		//}
	}

 //ret:
	if (ENABLE_FEATURE_CLEAN_UP)
		endmntent(fstab);
	if (ENABLE_FEATURE_CLEAN_UP) {
		free(storage_path);
		free(cmdopts);
	}

//TODO: exitcode should be ORed mask of (from "man mount"):
// 0 success
// 1 incorrect invocation or permissions
// 2 system error (out of memory, cannot fork, no more loop devices)
// 4 internal mount bug or missing nfs support in mount
// 8 user interrupt
//16 problems writing or locking /etc/mtab
//32 mount failure
//64 some mount succeeded
	return rc;
}
