/* vi: set sw=4 ts=4: */
/*
 * Mini swapon/swapoff implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SWAPON
//config:	bool "swapon (4.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Once you have created some swap space using 'mkswap', you also need
//config:	to enable your swap space with the 'swapon' utility. The 'swapoff'
//config:	utility is used, typically at system shutdown, to disable any swap
//config:	space. If you are not using any swap space, you can leave this
//config:	option disabled.
//config:
//config:config FEATURE_SWAPON_DISCARD
//config:	bool "Support discard option -d"
//config:	default y
//config:	depends on SWAPON
//config:	help
//config:	Enable support for discarding swap area blocks at swapon and/or as
//config:	the kernel frees them. This option enables both the -d option on
//config:	'swapon' and the 'discard' option for swap entries in /etc/fstab.
//config:
//config:config FEATURE_SWAPON_PRI
//config:	bool "Support priority option -p"
//config:	default y
//config:	depends on SWAPON
//config:	help
//config:	Enable support for setting swap device priority in swapon.
//config:
//config:config SWAPOFF
//config:	bool "swapoff (4.3 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:
//config:config FEATURE_SWAPONOFF_LABEL
//config:	bool "Support specifying devices by label or UUID"
//config:	default y
//config:	depends on SWAPON || SWAPOFF
//config:	select VOLUMEID
//config:	help
//config:	This allows for specifying a device by label or uuid, rather than by
//config:	name. This feature utilizes the same functionality as blkid/findfs.

//                  APPLET_ODDNAME:name     main         location     suid_type     help
//applet:IF_SWAPON( APPLET_ODDNAME(swapon,  swap_on_off, BB_DIR_SBIN, BB_SUID_DROP, swapon))
//applet:IF_SWAPOFF(APPLET_ODDNAME(swapoff, swap_on_off, BB_DIR_SBIN, BB_SUID_DROP, swapoff))

//kbuild:lib-$(CONFIG_SWAPON) += swaponoff.o
//kbuild:lib-$(CONFIG_SWAPOFF) += swaponoff.o

//usage:#define swapon_trivial_usage
//usage:       "[-a] [-e]" IF_FEATURE_SWAPON_DISCARD(" [-d[POL]]") IF_FEATURE_SWAPON_PRI(" [-p PRI]") " [DEVICE]"
//usage:#define swapon_full_usage "\n\n"
//usage:       "Start swapping on DEVICE\n"
//usage:     "\n	-a	Start swapping on all swap devices"
//usage:	IF_FEATURE_SWAPON_DISCARD(
//usage:     "\n	-d[POL]	Discard blocks at swapon (POL=once),"
//usage:     "\n		as freed (POL=pages), or both (POL omitted)"
//usage:	)
//usage:     "\n	-e	Silently skip devices that do not exist"
//usage:	IF_FEATURE_SWAPON_PRI(
//usage:     "\n	-p PRI	Set swap device priority"
//usage:	)
//usage:
//usage:#define swapoff_trivial_usage
//usage:       "[-a] [DEVICE]"
//usage:#define swapoff_full_usage "\n\n"
//usage:       "Stop swapping on DEVICE\n"
//usage:     "\n	-a	Stop swapping on all swap devices"

#include "libbb.h"
#include "common_bufsiz.h"
#include <mntent.h>
#ifndef __BIONIC__
# include <sys/swap.h>
#endif

#if ENABLE_FEATURE_SWAPONOFF_LABEL
# include "volume_id.h"
#else
# define resolve_mount_spec(fsname) ((void)0)
#endif

#ifndef MNTTYPE_SWAP
# define MNTTYPE_SWAP "swap"
#endif

#if ENABLE_FEATURE_SWAPON_DISCARD
#ifndef SWAP_FLAG_DISCARD
#define SWAP_FLAG_DISCARD 0x10000
#endif
#ifndef SWAP_FLAG_DISCARD_ONCE
#define SWAP_FLAG_DISCARD_ONCE 0x20000
#endif
#ifndef SWAP_FLAG_DISCARD_PAGES
#define SWAP_FLAG_DISCARD_PAGES 0x40000
#endif
#define SWAP_FLAG_DISCARD_MASK \
	(SWAP_FLAG_DISCARD | SWAP_FLAG_DISCARD_ONCE | SWAP_FLAG_DISCARD_PAGES)
#endif


#if ENABLE_FEATURE_SWAPON_DISCARD || ENABLE_FEATURE_SWAPON_PRI
struct globals {
	int flags;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define g_flags (G.flags)
#define save_g_flags()    int save_g_flags = g_flags
#define restore_g_flags() g_flags = save_g_flags
#else
#define g_flags 0
#define save_g_flags()    ((void)0)
#define restore_g_flags() ((void)0)
#endif
#define INIT_G() do { setup_common_bufsiz(); } while (0)

#if ENABLE_SWAPOFF
# if ENABLE_SWAPON
#  define do_swapoff (applet_name[5] == 'f')
# else
#  define do_swapoff 1
# endif
#else
#  define do_swapoff 0
#endif

/* Command line options */
enum {
	OPTBIT_a,                              /* -a all      */
	OPTBIT_e,                              /* -e ifexists */
	IF_FEATURE_SWAPON_DISCARD( OPTBIT_d ,) /* -d discard  */
	IF_FEATURE_SWAPON_PRI    ( OPTBIT_p ,) /* -p priority */
	OPT_a = 1 << OPTBIT_a,
	OPT_e = 1 << OPTBIT_e,
	OPT_d = IF_FEATURE_SWAPON_DISCARD((1 << OPTBIT_d)) + 0,
	OPT_p = IF_FEATURE_SWAPON_PRI    ((1 << OPTBIT_p)) + 0,
};

#define OPT_ALL      (option_mask32 & OPT_a)
#define OPT_DISCARD  (option_mask32 & OPT_d)
#define OPT_IFEXISTS (option_mask32 & OPT_e)
#define OPT_PRIO     (option_mask32 & OPT_p)

static int swap_enable_disable(char *device)
{
	int err = 0;
	int quiet = 0;

	resolve_mount_spec(&device);

	if (do_swapoff) {
		err = swapoff(device);
		/* Don't complain on OPT_ALL if not a swap device or if it doesn't exist */
		quiet = (OPT_ALL && (errno == EINVAL || errno == ENOENT));
	} else {
		/* swapon */
		struct stat st;
		err = stat(device, &st);
		if (!err) {
			if (ENABLE_DESKTOP && S_ISREG(st.st_mode)) {
				if (st.st_blocks * (off_t)512 < st.st_size) {
					bb_error_msg("%s: file has holes", device);
					return 1;
				}
			}
			err = swapon(device, g_flags);
			/* Don't complain on swapon -a if device is already in use */
			quiet = (OPT_ALL && errno == EBUSY);
		}
		/* Don't complain if file does not exist with -e option */
		if (err && OPT_IFEXISTS && errno == ENOENT)
			err = 0;
	}

	if (err && !quiet) {
		bb_simple_perror_msg(device);
		return 1;
	}
	return 0;
}

#if ENABLE_FEATURE_SWAPON_DISCARD
static void set_discard_flag(char *s)
{
	/* Unset the flag first to allow fstab options to override */
	/* options set on the command line */
	g_flags = (g_flags & ~SWAP_FLAG_DISCARD_MASK) | SWAP_FLAG_DISCARD;

	if (!s) /* No optional policy value on the commandline */
		return;
	/* Skip prepended '=' */
	if (*s == '=')
		s++;
	/* For fstab parsing: remove other appended options */
	*strchrnul(s, ',') = '\0';

	if (strcmp(s, "once") == 0)
		g_flags |= SWAP_FLAG_DISCARD_ONCE;
	if  (strcmp(s, "pages") == 0)
		g_flags |= SWAP_FLAG_DISCARD_PAGES;
}
#else
#define set_discard_flag(s) ((void)0)
#endif

#if ENABLE_FEATURE_SWAPON_PRI
static void set_priority_flag(char *s)
{
	unsigned prio;

	/* For fstab parsing: remove other appended options */
	*strchrnul(s, ',') = '\0';
	/* Max allowed 32767 (== SWAP_FLAG_PRIO_MASK) */
	prio = bb_strtou(s, NULL, 10);
	if (!errno) {
		/* Unset the flag first to allow fstab options to override */
		/* options set on the command line */
		g_flags = (g_flags & ~SWAP_FLAG_PRIO_MASK) | SWAP_FLAG_PREFER |
					MIN(prio, SWAP_FLAG_PRIO_MASK);
	}
}
#else
#define set_priority_flag(s) ((void)0)
#endif

static int do_em_all_in_fstab(void)
{
	struct mntent *m;
	int err = 0;
	FILE *f = xfopen_for_read("/etc/fstab");

	while ((m = getmntent(f)) != NULL) {
		if (strcmp(m->mnt_type, MNTTYPE_SWAP) == 0) {
			/* swapon -a should ignore entries with noauto,
			 * but swapoff -a should process them
			 */
			if (do_swapoff || hasmntopt(m, MNTOPT_NOAUTO) == NULL) {
				/* each swap space might have different flags */
				/* save global flags for the next round */
				save_g_flags();
				if (ENABLE_FEATURE_SWAPON_DISCARD) {
					char *p = hasmntopt(m, "discard");
					if (p) {
						/* move to '=' or to end of string */
						p += 7;
						set_discard_flag(p);
					}
				}
				if (ENABLE_FEATURE_SWAPON_PRI) {
					char *p = hasmntopt(m, "pri");
					if (p) {
						set_priority_flag(p + 4);
					}
				}
				err |= swap_enable_disable(m->mnt_fsname);
				restore_g_flags();
			}
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		endmntent(f);

	return err;
}

static int do_all_in_proc_swaps(void)
{
	char *line;
	int err = 0;
	FILE *f = fopen_for_read("/proc/swaps");
	/* Don't complain if missing */
	if (f) {
		while ((line = xmalloc_fgetline(f)) != NULL) {
			if (line[0] == '/') {
				*strchrnul(line, ' ') = '\0';
				err |= swap_enable_disable(line);
			}
			free(line);
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			fclose(f);
	}

	return err;
}

#define OPTSTR_SWAPON "ae" \
	IF_FEATURE_SWAPON_DISCARD("d::") \
	IF_FEATURE_SWAPON_PRI("p:")

int swap_on_off_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int swap_on_off_main(int argc UNUSED_PARAM, char **argv)
{
	IF_FEATURE_SWAPON_PRI(char *prio;)
	IF_FEATURE_SWAPON_DISCARD(char *discard = NULL;)
	int ret = 0;

	INIT_G();

	getopt32(argv, do_swapoff ? "ae" : OPTSTR_SWAPON
			IF_FEATURE_SWAPON_DISCARD(, &discard)
			IF_FEATURE_SWAPON_PRI(, &prio)
	);

	argv += optind;

	if (OPT_DISCARD) {
		set_discard_flag(discard);
	}
	if (OPT_PRIO) {
		set_priority_flag(prio);
	}

	if (OPT_ALL) {
		/* swapoff -a does also /proc/swaps */
		if (do_swapoff)
			ret = do_all_in_proc_swaps();
		ret |= do_em_all_in_fstab();
	} else if (!*argv) {
		/* if not -a we need at least one arg */
		bb_show_usage();
	}
	/* Unset -a now to allow for more messages in swap_enable_disable */
	option_mask32 = option_mask32 & ~OPT_a;
	/* Now process devices on the commandline if any */
	while (*argv) {
		ret |= swap_enable_disable(*argv++);
	}
	return ret;
}
