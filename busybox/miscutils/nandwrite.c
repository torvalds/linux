/*
 * nandwrite and nanddump ported to busybox from mtd-utils
 *
 * Author: Baruch Siach <baruch@tkos.co.il>, Orex Computed Radiography
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * TODO: add support for large (>4GB) MTD devices
 */
//config:config NANDWRITE
//config:	bool "nandwrite (5.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Write to the specified MTD device, with bad blocks awareness
//config:
//config:config NANDDUMP
//config:	bool "nanddump (6.3 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Dump the content of raw NAND chip

//applet:IF_NANDWRITE(APPLET(nandwrite, BB_DIR_USR_SBIN, BB_SUID_DROP))
//applet:IF_NANDDUMP(APPLET_ODDNAME(nanddump, nandwrite, BB_DIR_USR_SBIN, BB_SUID_DROP, nanddump))

//kbuild:lib-$(CONFIG_NANDWRITE) += nandwrite.o
//kbuild:lib-$(CONFIG_NANDDUMP) += nandwrite.o

//usage:#define nandwrite_trivial_usage
//usage:	"[-np] [-s ADDR] MTD_DEVICE [FILE]"
//usage:#define nandwrite_full_usage "\n\n"
//usage:	"Write to MTD_DEVICE\n"
//usage:     "\n	-n	Write without ecc"
//usage:     "\n	-p	Pad to page size"
//usage:     "\n	-s ADDR	Start address"

//usage:#define nanddump_trivial_usage
//usage:	"[-no]" IF_LONG_OPTS(" [--bb padbad|skipbad]") " [-s ADDR] [-l LEN] [-f FILE] MTD_DEVICE"
//usage:#define nanddump_full_usage "\n\n"
//usage:	"Dump MTD_DEVICE\n"
//usage:     "\n	-n	Read without ecc"
//usage:     "\n	-o	Dump oob data"
//usage:     "\n	-s ADDR	Start address"
//usage:     "\n	-l LEN	Length"
//usage:     "\n	-f FILE	Dump to file ('-' for stdout)"
//usage:     IF_LONG_OPTS(
//usage:     "\n	--bb METHOD"
//usage:     "\n		skipbad: skip bad blocks"
//usage:     "\n		padbad: substitute bad blocks by 0xff (default)"
//usage:     )

#include "libbb.h"
#include <mtd/mtd-user.h>

/* Old headers call it MTD_MODE_RAW.
 * FIXME: In kernel headers, MTD_FILE_MODE_RAW is not a define,
 * it's an enum. How I can test for existence of an enum?
 */
#if !defined(MTD_FILE_MODE_RAW)
# define MTD_FILE_MODE_RAW 3
#endif


#define IS_NANDDUMP  (ENABLE_NANDDUMP && (!ENABLE_NANDWRITE || (applet_name[4] == 'd')))
#define IS_NANDWRITE (ENABLE_NANDWRITE && (!ENABLE_NANDDUMP || (applet_name[4] != 'd')))

#define OPT_p  (1 << 0) /* nandwrite only */
#define OPT_o  (1 << 0) /* nanddump only */
#define OPT_n  (1 << 1)
#define OPT_s  (1 << 2)
#define OPT_f  (1 << 3)
#define OPT_l  (1 << 4)
#define OPT_bb (1 << 5) /* must be the last one in the list */

#define BB_PADBAD (1 << 0)
#define BB_SKIPBAD (1 << 1)

/* helper for writing out 0xff for bad blocks pad */
static void dump_bad(struct mtd_info_user *meminfo, unsigned len, int oob)
{
	unsigned char buf[meminfo->writesize];
	unsigned count;

	/* round len to the next page only if len is not already on a page */
	len = ((len - 1) | (meminfo->writesize - 1)) + 1;

	memset(buf, 0xff, sizeof(buf));
	for (count = 0; count < len; count += meminfo->writesize) {
		xwrite(STDOUT_FILENO, buf, meminfo->writesize);
		if (oob)
			xwrite(STDOUT_FILENO, buf, meminfo->oobsize);
	}
}

static unsigned next_good_eraseblock(int fd, struct mtd_info_user *meminfo,
		unsigned block_offset)
{
	while (1) {
		loff_t offs;

		if (block_offset >= meminfo->size) {
			if (IS_NANDWRITE)
				bb_error_msg_and_die("not enough space in MTD device");
			return block_offset; /* let the caller exit */
		}
		offs = block_offset;
		if (xioctl(fd, MEMGETBADBLOCK, &offs) == 0)
			return block_offset;
		/* ioctl returned 1 => "bad block" */
		if (IS_NANDWRITE)
			printf("Skipping bad block at 0x%08x\n", block_offset);
		block_offset += meminfo->erasesize;
	}
}

int nandwrite_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nandwrite_main(int argc UNUSED_PARAM, char **argv)
{
	/* Buffer for OOB data */
	unsigned char *oobbuf;
	unsigned opts;
	unsigned bb_method = BB_SKIPBAD;
	int fd;
	ssize_t cnt;
	unsigned mtdoffset, meminfo_writesize, blockstart, limit;
	unsigned end_addr = ~0;
	struct mtd_info_user meminfo;
	struct mtd_oob_buf oob;
	unsigned char *filebuf;
	const char *opt_s = "0", *opt_f = "-", *opt_l, *opt_bb;

	if (IS_NANDDUMP) {
		opts = getopt32long(argv, "^" "ons:f:l:" "\0" "=1",
				"bb\0" Required_argument "\xff", /* no short equivalent */
				&opt_s, &opt_f, &opt_l, &opt_bb
		);
	} else { /* nandwrite */
		opts = getopt32(argv, "^" "pns:" "\0" "-1:?2", &opt_s);
	}
	argv += optind;

	if (IS_NANDWRITE && argv[1])
		opt_f = argv[1];
	if (!LONE_DASH(opt_f)) {
		int tmp_fd = xopen(opt_f,
			IS_NANDDUMP ? O_WRONLY | O_TRUNC | O_CREAT : O_RDONLY
		);
		xmove_fd(tmp_fd, IS_NANDDUMP ? STDOUT_FILENO : STDIN_FILENO);
	}

	fd = xopen(argv[0], IS_NANDWRITE ? O_RDWR : O_RDONLY);
	xioctl(fd, MEMGETINFO, &meminfo);

	if (opts & OPT_n)
		xioctl(fd, MTDFILEMODE, (void *)MTD_FILE_MODE_RAW);

	mtdoffset = xstrtou(opt_s, 0);
	if (IS_NANDDUMP && (opts & OPT_l)) {
		unsigned length = xstrtou(opt_l, 0);
		if (length < meminfo.size - mtdoffset)
			end_addr = mtdoffset + length;
	}
	if (IS_NANDDUMP && (opts & OPT_bb)) {
		if (strcmp("skipbad", opt_bb) == 0)
			bb_method = BB_SKIPBAD;
		else if (strcmp("padbad", opt_bb) == 0)
			bb_method = BB_PADBAD;
		else
			bb_show_usage();
	}

	/* Pull it into a CPU register (hopefully) - smaller code that way */
	meminfo_writesize = meminfo.writesize;

	if (mtdoffset & (meminfo_writesize - 1))
		bb_error_msg_and_die("start address is not page aligned");

	filebuf = xmalloc(meminfo_writesize);
	oobbuf = xmalloc(meminfo.oobsize);

	oob.start  = 0;
	oob.length = meminfo.oobsize;
	oob.ptr    = oobbuf;

	blockstart = mtdoffset & ~(meminfo.erasesize - 1);
	if (blockstart != mtdoffset) {
		unsigned tmp;
		/* mtdoffset is in the middle of an erase block, verify that
		 * this block is OK. Advance mtdoffset only if this block is
		 * bad.
		 */
		tmp = next_good_eraseblock(fd, &meminfo, blockstart);
		if (tmp != blockstart) {
			/* bad block(s), advance mtdoffset */
			if (IS_NANDDUMP) {
				if (bb_method == BB_PADBAD) {
					int bad_len = MIN(tmp, end_addr) - mtdoffset;
					dump_bad(&meminfo, bad_len, opts & OPT_o);
				}
				/* with option skipbad, increase the total length */
				if (bb_method == BB_SKIPBAD) {
					end_addr += (tmp - blockstart);
				}
			}
			mtdoffset = tmp;
		}
	}

	cnt = -1;
	limit = MIN(meminfo.size, end_addr);
	while (mtdoffset < limit) {
		int input_fd = IS_NANDWRITE ? STDIN_FILENO : fd;
		int output_fd = IS_NANDWRITE ? fd : STDOUT_FILENO;

		blockstart = mtdoffset & ~(meminfo.erasesize - 1);
		if (blockstart == mtdoffset) {
			/* starting a new eraseblock */
			mtdoffset = next_good_eraseblock(fd, &meminfo, blockstart);
			if (IS_NANDWRITE)
				printf("Writing at 0x%08x\n", mtdoffset);
			else if (mtdoffset > blockstart) {
				if (bb_method == BB_PADBAD) {
					/* dump FF padded bad block */
					int bad_len = MIN(mtdoffset, limit) - blockstart;
					dump_bad(&meminfo, bad_len, opts & OPT_o);
				} else if (bb_method == BB_SKIPBAD) {
					/* for skipbad, increase the length */
					if ((end_addr + mtdoffset - blockstart) > end_addr)
						end_addr += (mtdoffset - blockstart);
					else
						end_addr = ~0;
					limit = MIN(meminfo.size, end_addr);
				}
			}
			if (mtdoffset >= limit)
				break;
		}
		xlseek(fd, mtdoffset, SEEK_SET);

		/* get some more data from input */
		cnt = full_read(input_fd, filebuf, meminfo_writesize);
		if (cnt == 0) {
			/* even with -p, we do not pad past the end of input
			 * (-p only zero-pads last incomplete page)
			 */
			break;
		}
		if (cnt < meminfo_writesize) {
			if (IS_NANDDUMP)
				bb_error_msg_and_die("short read");
			if (!(opts & OPT_p))
				bb_error_msg_and_die("input size is not rounded up to page size, "
						"use -p to zero pad");
			/* zero pad to end of write block */
			memset(filebuf + cnt, 0, meminfo_writesize - cnt);
		}
		xwrite(output_fd, filebuf, meminfo_writesize);

		if (IS_NANDDUMP && (opts & OPT_o)) {
			/* Dump OOB data */
			oob.start = mtdoffset;
			xioctl(fd, MEMREADOOB, &oob);
			xwrite(output_fd, oobbuf, meminfo.oobsize);
		}

		mtdoffset += meminfo_writesize;
		if (cnt < meminfo_writesize)
			break;
	}

	if (IS_NANDWRITE && cnt != 0) {
		/* We filled entire MTD, but did we reach EOF on input? */
		if (full_read(STDIN_FILENO, filebuf, meminfo_writesize) != 0) {
			/* no */
			bb_error_msg_and_die("not enough space in MTD device");
		}
	}

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(filebuf);
		close(fd);
	}

	return EXIT_SUCCESS;
}
