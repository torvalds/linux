/* vi: set sw=4 ts=4: */
/*
 * busybox reimplementation of flashcp
 *
 * (C) 2009 Stefan Seyfried <seife@sphairon.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config FLASHCP
//config:	bool "flashcp (5.4 kb)"
//config:	default n  # doesn't build on Ubuntu 8.04
//config:	help
//config:	The flashcp binary, inspired by mtd-utils as of git head 5eceb74f7.
//config:	This utility is used to copy images into a MTD device.

//applet:IF_FLASHCP(APPLET(flashcp, BB_DIR_USR_SBIN, BB_SUID_DROP))
/* not NOEXEC: if flash operation stalls, use less memory in "hung" process */

//kbuild:lib-$(CONFIG_FLASHCP) += flashcp.o

//usage:#define flashcp_trivial_usage
//usage:       "-v FILE MTD_DEVICE"
//usage:#define flashcp_full_usage "\n\n"
//usage:       "Copy an image to MTD device\n"
//usage:     "\n	-v	Verbose"

#include "libbb.h"
#include <mtd/mtd-user.h>

/* If 1, simulates "flashing" by writing to existing regular file */
#define MTD_DEBUG 0

#define OPT_v (1 << 0)

#define BUFSIZE (4 * 1024)

static void progress(int mode, uoff_t count, uoff_t total)
{
	uoff_t percent;

	if (!option_mask32) //if (!(option_mask32 & OPT_v))
		return;
	percent = count * 100;
	if (total)
		percent = (unsigned) (percent / total);
	printf("\r%s: %"OFF_FMT"u/%"OFF_FMT"u (%u%%) ",
		(mode < 0) ? "Erasing block" : ((mode == 0) ? "Writing kb" : "Verifying kb"),
		count, total, (unsigned)percent);
	fflush_all();
}

static void progress_newline(void)
{
	if (!option_mask32) //if (!(option_mask32 & OPT_v))
		return;
	bb_putchar('\n');
}

int flashcp_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int flashcp_main(int argc UNUSED_PARAM, char **argv)
{
	int fd_f, fd_d; /* input file and mtd device file descriptors */
	int i;
	uoff_t erase_count;
	struct mtd_info_user mtd;
	struct erase_info_user e;
	struct stat statb;
//	const char *filename, *devicename;
	RESERVE_CONFIG_UBUFFER(buf, BUFSIZE);
	RESERVE_CONFIG_UBUFFER(buf2, BUFSIZE);

	/*opts =*/ getopt32(argv, "^" "v" "\0" "=2"/*exactly 2 non-option args: file,dev*/);
	argv += optind;
//	filename = *argv++;
//	devicename = *argv;
#define filename argv[0]
#define devicename argv[1]

	/* open input file and mtd device and do sanity checks */
	fd_f = xopen(filename, O_RDONLY);
	fstat(fd_f, &statb);
	fd_d = xopen(devicename, O_SYNC | O_RDWR);
#if !MTD_DEBUG
	if (ioctl(fd_d, MEMGETINFO, &mtd) < 0) {
		bb_error_msg_and_die("%s is not a MTD flash device", devicename);
	}
	if (statb.st_size > mtd.size) {
		bb_error_msg_and_die("%s bigger than %s", filename, devicename);
	}
#else
	mtd.erasesize = 64 * 1024;
#endif

	/* always erase a complete block */
	erase_count = (uoff_t)(statb.st_size + mtd.erasesize - 1) / mtd.erasesize;
	/* erase 1 block at a time to be able to give verbose output */
	e.length = mtd.erasesize;
#if 0
/* (1) bloat
 * (2) will it work for multi-gigabyte devices?
 * (3) worse wrt error detection granularity
 */
	/* optimization: if not verbose, erase in one go */
	if (!opts) { // if (!(opts & OPT_v))
		e.length = mtd.erasesize * erase_count;
		erase_count = 1;
	}
#endif
	e.start = 0;
	for (i = 1; i <= erase_count; i++) {
		progress(-1, i, erase_count);
#if !MTD_DEBUG
		if (ioctl(fd_d, MEMERASE, &e) < 0) {
			bb_perror_msg_and_die("erase error at 0x%llx on %s",
				(long long)e.start, devicename);
		}
#else
		usleep(100*1000);
#endif
		e.start += mtd.erasesize;
	}
	progress_newline();

	/* doing this outer loop gives significantly smaller code
	 * than doing two separate loops for writing and verifying */
	for (i = 0; i <= 1; i++) {
		uoff_t done;
		unsigned count;

		xlseek(fd_f, 0, SEEK_SET);
		xlseek(fd_d, 0, SEEK_SET);
		done = 0;
		count = BUFSIZE;
		while (1) {
			uoff_t rem;

			progress(i, done / 1024, (uoff_t)statb.st_size / 1024);
			rem = statb.st_size - done;
			if (rem == 0)
				break;
			if (rem < BUFSIZE)
				count = rem;
			xread(fd_f, buf, count);
			if (i == 0) {
				int ret;
				if (count < BUFSIZE)
					memset((char*)buf + count, 0, BUFSIZE - count);
				errno = 0;
				ret = full_write(fd_d, buf, BUFSIZE);
				if (ret != BUFSIZE) {
					bb_perror_msg_and_die("write error at 0x%"OFF_FMT"x on %s, "
						"write returned %d",
						done, devicename, ret);
				}
			} else { /* i == 1 */
				xread(fd_d, buf2, count);
				if (memcmp(buf, buf2, count) != 0) {
					bb_error_msg_and_die("verification mismatch at 0x%"OFF_FMT"x", done);
				}
			}

			done += count;
		}

		progress_newline();
	}
	/* we won't come here if there was an error */

	return EXIT_SUCCESS;
}
