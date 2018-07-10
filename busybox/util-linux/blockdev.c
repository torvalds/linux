/*
 * blockdev implementation for busybox
 *
 * Copyright (C) 2010 Sergey Naumov <sknaumov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config BLOCKDEV
//config:	bool "blockdev (2.4 kb)"
//config:	default y
//config:	help
//config:	Performs some ioctls with block devices.

//applet:IF_BLOCKDEV(APPLET_NOEXEC(blockdev, blockdev, BB_DIR_SBIN, BB_SUID_DROP, blockdev))

//kbuild:lib-$(CONFIG_BLOCKDEV) += blockdev.o

//usage:#define blockdev_trivial_usage
//usage:	"OPTION BLOCKDEV"
//usage:#define blockdev_full_usage "\n\n"
//usage:       "	--setro		Set ro"
//usage:     "\n	--setrw		Set rw"
//usage:     "\n	--getro		Get ro"
//usage:     "\n	--getss		Get sector size"
//usage:     "\n	--getbsz	Get block size"
//usage:     "\n	--setbsz BYTES	Set block size"
//usage:     "\n	--getsz		Get device size in 512-byte sectors"
/*//usage:     "\n	--getsize	Get device size in sectors (deprecated)"*/
//usage:     "\n	--getsize64	Get device size in bytes"
//usage:     "\n	--flushbufs	Flush buffers"
//usage:     "\n	--rereadpt	Reread partition table"


#include "libbb.h"
#include <linux/fs.h>

enum {
	ARG_NONE   = 0,
	ARG_INT    = 1,
	ARG_ULONG  = 2,
	/* Yes, BLKGETSIZE64 takes pointer to uint64_t, not ullong! */
	ARG_U64    = 3,
	ARG_MASK   = 3,

	FL_USRARG   = 4, /* argument is provided by user */
	FL_NORESULT = 8,
	FL_SCALE512 = 16,
};

struct bdc {
	uint32_t   ioc;                       /* ioctl code */
	const char name[sizeof("flushbufs")]; /* "--setfoo" wothout "--" */
	uint8_t    flags;
	int8_t     argval;                    /* default argument value */
};

static const struct bdc bdcommands[] = {
	{
		.ioc = BLKROSET,
		.name = "setro",
		.flags = ARG_INT + FL_NORESULT,
		.argval = 1,
	},{
		.ioc = BLKROSET,
		.name = "setrw",
		.flags = ARG_INT + FL_NORESULT,
		.argval = 0,
	},{
		.ioc = BLKROGET,
		.name = "getro",
		.flags = ARG_INT,
		.argval = -1,
	},{
		.ioc = BLKSSZGET,
		.name = "getss",
		.flags = ARG_INT,
		.argval = -1,
	},{
		.ioc = BLKBSZGET,
		.name = "getbsz",
		.flags = ARG_INT,
		.argval = -1,
	},{
		.ioc = BLKBSZSET,
		.name = "setbsz",
		.flags = ARG_INT + FL_NORESULT + FL_USRARG,
		.argval = 0,
	},{
		.ioc = BLKGETSIZE64,
		.name = "getsz",
		.flags = ARG_U64 + FL_SCALE512,
		.argval = -1,
	},{
		.ioc = BLKGETSIZE,
		.name = "getsize",
		.flags = ARG_ULONG,
		.argval = -1,
	},{
		.ioc = BLKGETSIZE64,
		.name = "getsize64",
		.flags = ARG_U64,
		.argval = -1,
	},{
		.ioc = BLKFLSBUF,
		.name = "flushbufs",
		.flags = ARG_NONE + FL_NORESULT,
		.argval = 0,
	},{
		.ioc = BLKRRPART,
		.name = "rereadpt",
		.flags = ARG_NONE + FL_NORESULT,
		.argval = 0,
	}
};

static const struct bdc *find_cmd(const char *s)
{
	const struct bdc *bdcmd = bdcommands;
	if (s[0] == '-' && s[1] == '-') {
		s += 2;
		do {
			if (strcmp(s, bdcmd->name) == 0)
				return bdcmd;
			bdcmd++;
		} while (bdcmd != bdcommands + ARRAY_SIZE(bdcommands));
	}
	bb_show_usage();
}

int blockdev_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int blockdev_main(int argc UNUSED_PARAM, char **argv)
{
	const struct bdc *bdcmd;
	int fd;
	uint64_t u64;
	union {
		int i;
		unsigned long lu;
		uint64_t u64;
	} ioctl_val_on_stack;

	argv++;
	if (!argv[0] || !argv[1]) /* must have at least 2 args */
		bb_show_usage();

	bdcmd = find_cmd(*argv);

	u64 = (int)bdcmd->argval;
	if (bdcmd->flags & FL_USRARG)
		u64 = xatoi_positive(*++argv);

	argv++;
	if (!argv[0] || argv[1])
		bb_show_usage();
	fd = xopen(argv[0], O_RDONLY);

	ioctl_val_on_stack.u64 = u64;
#if BB_BIG_ENDIAN
	/* Store data properly wrt data size.
	 * (1) It's no-op for little-endian.
	 * (2) it's no-op for 0 and -1. Only --setro uses arg != 0 and != -1,
	 * and it is ARG_INT. --setbsz USER_VAL is also ARG_INT.
	 * Thus, we don't need to handle ARG_ULONG.
	 */
	switch (bdcmd->flags & ARG_MASK) {
	case ARG_INT:
		ioctl_val_on_stack.i = (int)u64;
		break;
# if 0 /* unused */
	case ARG_ULONG:
		ioctl_val_on_stack.lu = (unsigned long)u64;
		break;
# endif
	}
#endif

	if (ioctl(fd, bdcmd->ioc, &ioctl_val_on_stack.u64) == -1)
		bb_simple_perror_msg_and_die(*argv);

	/* Fetch it into register(s) */
	u64 = ioctl_val_on_stack.u64;

	if (bdcmd->flags & FL_SCALE512)
		u64 >>= 9;

	/* Zero- or one-extend the value if needed, then print */
	switch (bdcmd->flags & (ARG_MASK+FL_NORESULT)) {
	case ARG_INT:
		/* Smaller code when we use long long
		 * (gcc tail-merges printf call)
		 */
		printf("%lld\n", (long long)(int)u64);
		break;
	case ARG_ULONG:
		u64 = (unsigned long)u64;
		/* FALLTHROUGH */
	case ARG_U64:
		printf("%llu\n", (unsigned long long)u64);
		break;
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);
	return EXIT_SUCCESS;
}
