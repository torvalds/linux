/* vi: set sw=4 ts=4: */
/*
 * mkswap.c - format swap device (Linux v1 only)
 *
 * Copyright 2006 Rob Landley <rob@landley.net>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MKSWAP
//config:	bool "mkswap (5.8 kb)"
//config:	default y
//config:	help
//config:	The mkswap utility is used to configure a file or disk partition as
//config:	Linux swap space. This allows Linux to use the entire file or
//config:	partition as if it were additional RAM, which can greatly increase
//config:	the capability of low-memory machines. This additional memory is
//config:	much slower than real RAM, but can be very helpful at preventing your
//config:	applications being killed by the Linux out of memory (OOM) killer.
//config:	Once you have created swap space using 'mkswap' you need to enable
//config:	the swap space using the 'swapon' utility.
//config:
//config:config FEATURE_MKSWAP_UUID
//config:	bool "UUID support"
//config:	default y
//config:	depends on MKSWAP
//config:	help
//config:	Generate swap spaces with universally unique identifiers.

//applet:IF_MKSWAP(APPLET(mkswap, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_MKSWAP) += mkswap.o

//usage:#define mkswap_trivial_usage
//usage:       "[-L LBL] BLOCKDEV [KBYTES]"
//usage:#define mkswap_full_usage "\n\n"
//usage:       "Prepare BLOCKDEV to be used as swap partition\n"
//usage:     "\n	-L LBL	Label"

#include "libbb.h"
#include "common_bufsiz.h"

#if ENABLE_SELINUX
static void mkswap_selinux_setcontext(int fd, const char *path)
{
	struct stat stbuf;

	if (!is_selinux_enabled())
		return;

	xfstat(fd, &stbuf, path);
	if (S_ISREG(stbuf.st_mode)) {
		security_context_t newcon;
		security_context_t oldcon = NULL;
		context_t context;

		if (fgetfilecon(fd, &oldcon) < 0) {
			if (errno != ENODATA)
				goto error;
			if (matchpathcon(path, stbuf.st_mode, &oldcon) < 0)
				goto error;
		}
		context = context_new(oldcon);
		if (!context || context_type_set(context, "swapfile_t"))
			goto error;
		newcon = context_str(context);
		if (!newcon)
			goto error;
		/* fsetfilecon_raw is hidden */
		if (strcmp(oldcon, newcon) != 0 && fsetfilecon(fd, newcon) < 0)
			goto error;
		if (ENABLE_FEATURE_CLEAN_UP) {
			context_free(context);
			freecon(oldcon);
		}
	}
	return;
 error:
	bb_perror_msg_and_die("SELinux relabeling failed");
}
#else
# define mkswap_selinux_setcontext(fd, path) ((void)0)
#endif

/* from Linux 2.6.23 */
/*
 * Magic header for a swap area. ... Note that the first
 * kilobyte is reserved for boot loader or disk label stuff.
 */
struct swap_header_v1 {
/*	char     bootbits[1024];    Space for disklabel etc. */
	uint32_t version;        /* second kbyte, word 0 */
	uint32_t last_page;      /* 1 */
	uint32_t nr_badpages;    /* 2 */
	char     sws_uuid[16];   /* 3,4,5,6 */
	char     sws_volume[16]; /* 7,8,9,10 */
	uint32_t padding[117];   /* 11..127 */
	uint32_t badpages[1];    /* 128 */
	/* total 129 32-bit words in 2nd kilobyte */
} FIX_ALIASING;

#define NWORDS 129
#define hdr ((struct swap_header_v1*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

struct BUG_sizes {
	char swap_header_v1_wrong[sizeof(*hdr)  != (NWORDS * 4) ? -1 : 1];
	char bufsiz1_is_too_small[COMMON_BUFSIZE < (NWORDS * 4) ? -1 : 1];
};

/* Stored without terminating NUL */
static const char SWAPSPACE2[sizeof("SWAPSPACE2")-1] ALIGN1 = "SWAPSPACE2";

int mkswap_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int mkswap_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;
	unsigned pagesize;
	off_t len;
	const char *label = "";

	INIT_G();

	/* TODO: -p PAGESZ, -U UUID */
	getopt32(argv, "^" "L:" "\0" "-1"/*at least one arg*/, &label);
	argv += optind;

	fd = xopen(argv[0], O_WRONLY);

	/* Figure out how big the device is */
	len = get_volume_size_in_bytes(fd, argv[1], 1024, /*extend:*/ 1);
	pagesize = getpagesize();
	len -= pagesize;

	/* Announce our intentions */
	printf("Setting up swapspace version 1, size = %"OFF_FMT"u bytes\n", len);
	mkswap_selinux_setcontext(fd, argv[0]);

	/* hdr is zero-filled so far. Clear the first kbyte, or else
	 * mkswap-ing former FAT partition does NOT erase its signature.
	 *
	 * util-linux-ng 2.17.2 claims to erase it only if it does not see
	 * a partition table and is not run on whole disk. -f forces it.
	 */
	xwrite(fd, hdr, 1024);

	/* Fill the header. */
	hdr->version = 1;
	hdr->last_page = (uoff_t)len / pagesize;

	if (ENABLE_FEATURE_MKSWAP_UUID) {
		char uuid_string[32];
		generate_uuid((void*)hdr->sws_uuid);
		bin2hex(uuid_string, hdr->sws_uuid, 16);
		/* f.e. UUID=dfd9c173-be52-4d27-99a5-c34c6c2ff55f */
		printf("UUID=%.8s"  "-%.4s-%.4s-%.4s-%.12s\n",
			uuid_string,
			uuid_string+8,
			uuid_string+8+4,
			uuid_string+8+4+4,
			uuid_string+8+4+4+4
		);
	}
	safe_strncpy(hdr->sws_volume, label, 16);

	/* Write the header.  Sync to disk because some kernel versions check
	 * signature on disk (not in cache) during swapon. */
	xwrite(fd, hdr, NWORDS * 4);
	xlseek(fd, pagesize - 10, SEEK_SET);
	xwrite(fd, SWAPSPACE2, 10);
	fsync(fd);

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);

	return 0;
}
