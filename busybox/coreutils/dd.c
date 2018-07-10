/* vi: set sw=4 ts=4: */
/*
 * Mini dd implementation for busybox
 *
 * Copyright (C) 2000,2001  Matt Kraai
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config DD
//config:	bool "dd (7.1 kb)"
//config:	default y
//config:	help
//config:	dd copies a file (from standard input to standard output,
//config:	by default) using specific input and output blocksizes,
//config:	while optionally performing conversions on it.
//config:
//config:config FEATURE_DD_SIGNAL_HANDLING
//config:	bool "Enable signal handling for status reporting"
//config:	default y
//config:	depends on DD
//config:	help
//config:	Sending a SIGUSR1 signal to a running 'dd' process makes it
//config:	print to standard error the number of records read and written
//config:	so far, then to resume copying.
//config:
//config:	$ dd if=/dev/zero of=/dev/null &
//config:	$ pid=$!; kill -USR1 $pid; sleep 1; kill $pid
//config:	10899206+0 records in
//config:	10899206+0 records out
//config:
//config:config FEATURE_DD_THIRD_STATUS_LINE
//config:	bool "Enable the third status line upon signal"
//config:	default y
//config:	depends on DD && FEATURE_DD_SIGNAL_HANDLING
//config:	help
//config:	Displays a coreutils-like third status line with transferred bytes,
//config:	elapsed time and speed.
//config:
//config:config FEATURE_DD_IBS_OBS
//config:	bool "Enable ibs, obs, iflag and conv options"
//config:	default y
//config:	depends on DD
//config:	help
//config:	Enable support for writing a certain number of bytes in and out,
//config:	at a time, and performing conversions on the data stream.
//config:
//config:config FEATURE_DD_STATUS
//config:	bool "Enable status display options"
//config:	default y
//config:	depends on DD
//config:	help
//config:	Enable support for status=noxfer/none option.

//applet:IF_DD(APPLET_NOEXEC(dd, dd, BB_DIR_BIN, BB_SUID_DROP, dd))

//kbuild:lib-$(CONFIG_DD) += dd.o

//usage:#define dd_trivial_usage
//usage:       "[if=FILE] [of=FILE] " IF_FEATURE_DD_IBS_OBS("[ibs=N] [obs=N] ") "[bs=N] [count=N] [skip=N]\n"
//usage:       "	[seek=N]" IF_FEATURE_DD_IBS_OBS(" [conv=notrunc|noerror|sync|fsync] [iflag=skip_bytes|fullblock]")
//usage:#define dd_full_usage "\n\n"
//usage:       "Copy a file with converting and formatting\n"
//usage:     "\n	if=FILE		Read from FILE instead of stdin"
//usage:     "\n	of=FILE		Write to FILE instead of stdout"
//usage:     "\n	bs=N		Read and write N bytes at a time"
//usage:	IF_FEATURE_DD_IBS_OBS(
//usage:     "\n	ibs=N		Read N bytes at a time"
//usage:	)
//usage:	IF_FEATURE_DD_IBS_OBS(
//usage:     "\n	obs=N		Write N bytes at a time"
//usage:	)
//usage:     "\n	count=N		Copy only N input blocks"
//usage:     "\n	skip=N		Skip N input blocks"
//usage:     "\n	seek=N		Skip N output blocks"
//usage:	IF_FEATURE_DD_IBS_OBS(
//usage:     "\n	conv=notrunc	Don't truncate output file"
//usage:     "\n	conv=noerror	Continue after read errors"
//usage:     "\n	conv=sync	Pad blocks with zeros"
//usage:     "\n	conv=fsync	Physically write data out before finishing"
//usage:     "\n	conv=swab	Swap every pair of bytes"
//usage:     "\n	iflag=skip_bytes	skip=N is in bytes"
//usage:     "\n	iflag=fullblock	Read full blocks"
//usage:	)
//usage:	IF_FEATURE_DD_STATUS(
//usage:     "\n	status=noxfer	Suppress rate output"
//usage:     "\n	status=none	Suppress all output"
//usage:	)
//usage:     "\n"
//usage:     "\nN may be suffixed by c (1), w (2), b (512), kB (1000), k (1024), MB, M, GB, G"
//usage:
//usage:#define dd_example_usage
//usage:       "$ dd if=/dev/zero of=/dev/ram1 bs=1M count=4\n"
//usage:       "4+0 records in\n"
//usage:       "4+0 records out\n"

#include "libbb.h"
#include "common_bufsiz.h"

/* This is a NOEXEC applet. Be very careful! */


enum {
	ifd = STDIN_FILENO,
	ofd = STDOUT_FILENO,
};

struct globals {
	off_t out_full, out_part, in_full, in_part;
#if ENABLE_FEATURE_DD_THIRD_STATUS_LINE
	unsigned long long total_bytes;
	unsigned long long begin_time_us;
#endif
	int flags;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	/* we have to zero it out because of NOEXEC */ \
	memset(&G, 0, sizeof(G)); \
} while (0)

enum {
	/* Must be in the same order as OP_conv_XXX! */
	/* (see "flags |= (1 << what)" below) */
	FLAG_NOTRUNC = (1 << 0) * ENABLE_FEATURE_DD_IBS_OBS,
	FLAG_SYNC    = (1 << 1) * ENABLE_FEATURE_DD_IBS_OBS,
	FLAG_NOERROR = (1 << 2) * ENABLE_FEATURE_DD_IBS_OBS,
	FLAG_FSYNC   = (1 << 3) * ENABLE_FEATURE_DD_IBS_OBS,
	FLAG_SWAB    = (1 << 4) * ENABLE_FEATURE_DD_IBS_OBS,
	/* end of conv flags */
	/* start of input flags */
	FLAG_IFLAG_SHIFT = 5,
	FLAG_SKIP_BYTES = (1 << 5) * ENABLE_FEATURE_DD_IBS_OBS,
	FLAG_FULLBLOCK = (1 << 6) * ENABLE_FEATURE_DD_IBS_OBS,
	/* end of input flags */
	FLAG_TWOBUFS = (1 << 7) * ENABLE_FEATURE_DD_IBS_OBS,
	FLAG_COUNT   = 1 << 8,
	FLAG_STATUS_NONE = 1 << 9,
	FLAG_STATUS_NOXFER = 1 << 10,
};

static void dd_output_status(int UNUSED_PARAM cur_signal)
{
#if ENABLE_FEATURE_DD_THIRD_STATUS_LINE
	double seconds;
	unsigned long long bytes_sec;
	unsigned long long now_us = monotonic_us(); /* before fprintf */
#endif

	/* Deliberately using %u, not %d */
	fprintf(stderr, "%"OFF_FMT"u+%"OFF_FMT"u records in\n"
			"%"OFF_FMT"u+%"OFF_FMT"u records out\n",
			G.in_full, G.in_part,
			G.out_full, G.out_part);

#if ENABLE_FEATURE_DD_THIRD_STATUS_LINE
# if ENABLE_FEATURE_DD_STATUS
	if (G.flags & FLAG_STATUS_NOXFER) /* status=noxfer active? */
		return;
	//TODO: should status=none make dd stop reacting to USR1 entirely?
	//So far we react to it (we print the stats),
	//status=none only suppresses final, non-USR1 generated status message.
# endif
	fprintf(stderr, "%llu bytes (%sB) copied, ",
			G.total_bytes,
			/* show fractional digit, use suffixes */
			make_human_readable_str(G.total_bytes, 1, 0)
	);
	/* Corner cases:
	 * ./busybox dd </dev/null >/dev/null
	 * ./busybox dd bs=1M count=2000 </dev/zero >/dev/null
	 * (echo DONE) | ./busybox dd >/dev/null
	 * (sleep 1; echo DONE) | ./busybox dd >/dev/null
	 */
	seconds = (now_us - G.begin_time_us) / 1000000.0;
	bytes_sec = G.total_bytes / seconds;
	fprintf(stderr, "%f seconds, %sB/s\n",
			seconds,
			/* show fractional digit, use suffixes */
			make_human_readable_str(bytes_sec, 1, 0)
	);
#endif
}

static ssize_t full_write_or_warn(const void *buf, size_t len,
	const char *const filename)
{
	ssize_t n = full_write(ofd, buf, len);
	if (n < 0)
		bb_perror_msg("writing '%s'", filename);
	return n;
}

static bool write_and_stats(const void *buf, size_t len, size_t obs,
	const char *filename)
{
	ssize_t n = full_write_or_warn(buf, len, filename);
	if (n < 0)
		return 1;
#if ENABLE_FEATURE_DD_THIRD_STATUS_LINE
	G.total_bytes += n;
#endif
	if ((size_t)n == obs) {
		G.out_full++;
		return 0;
	}
	if ((size_t)n == len) {
		G.out_part++;
		return 0;
	}
	return 1;
}

#if ENABLE_LFS
# define XATOU_SFX xatoull_sfx
#else
# define XATOU_SFX xatoul_sfx
#endif

#if ENABLE_FEATURE_DD_IBS_OBS
static int parse_comma_flags(char *val, const char *words, const char *error_in)
{
	int flags = 0;
	while (1) {
		int n;
		char *arg;
		/* find ',', replace them with NUL so we can use val for
		 * index_in_strings() without copying.
		 * We rely on val being non-null, else strchr would fault.
		 */
		arg = strchr(val, ',');
		if (arg)
			*arg = '\0';
		n = index_in_strings(words, val);
		if (n < 0)
			bb_error_msg_and_die(bb_msg_invalid_arg_to, val, error_in);
		flags |= (1 << n);
		if (!arg) /* no ',' left, so this was the last specifier */
			break;
		*arg = ','; /* to preserve ps listing */
		val = arg + 1; /* skip this keyword and ',' */
	}
	return flags;
}
#endif

int dd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int dd_main(int argc UNUSED_PARAM, char **argv)
{
	static const char keywords[] ALIGN1 =
		"bs\0""count\0""seek\0""skip\0""if\0""of\0"IF_FEATURE_DD_STATUS("status\0")
#if ENABLE_FEATURE_DD_IBS_OBS
		"ibs\0""obs\0""conv\0""iflag\0"
#endif
		;
#if ENABLE_FEATURE_DD_IBS_OBS
	static const char conv_words[] ALIGN1 =
		"notrunc\0""sync\0""noerror\0""fsync\0""swab\0";
	static const char iflag_words[] ALIGN1 =
		"skip_bytes\0""fullblock\0";
#endif
#if ENABLE_FEATURE_DD_STATUS
	static const char status_words[] ALIGN1 =
		"none\0""noxfer\0";
#endif
	enum {
		OP_bs = 0,
		OP_count,
		OP_seek,
		OP_skip,
		OP_if,
		OP_of,
		IF_FEATURE_DD_STATUS(OP_status,)
#if ENABLE_FEATURE_DD_IBS_OBS
		OP_ibs,
		OP_obs,
		OP_conv,
		OP_iflag,
		/* Must be in the same order as FLAG_XXX! */
		OP_conv_notrunc = 0,
		OP_conv_sync,
		OP_conv_noerror,
		OP_conv_fsync,
		OP_conv_swab,
	/* Unimplemented conv=XXX: */
	//nocreat       do not create the output file
	//excl          fail if the output file already exists
	//fdatasync     physically write output file data before finishing
	//lcase         change upper case to lower case
	//ucase         change lower case to upper case
	//block         pad newline-terminated records with spaces to cbs-size
	//unblock       replace trailing spaces in cbs-size records with newline
	//ascii         from EBCDIC to ASCII
	//ebcdic        from ASCII to EBCDIC
	//ibm           from ASCII to alternate EBCDIC
	/* Partially implemented: */
	//swab          swap every pair of input bytes: will abort on non-even reads
		OP_iflag_skip_bytes,
		OP_iflag_fullblock,
#endif
	};
	smallint exitcode = EXIT_FAILURE;
	int i;
	size_t ibs = 512;
	char *ibuf;
#if ENABLE_FEATURE_DD_IBS_OBS
	size_t obs = 512;
	char *obuf;
#else
# define obs  ibs
# define obuf ibuf
#endif
	/* These are all zeroed at once! */
	struct {
		size_t oc;
		ssize_t prev_read_size; /* for detecting swab failure */
		off_t count;
		off_t seek, skip;
		const char *infile, *outfile;
	} Z;
#define oc      (Z.oc     )
#define prev_read_size (Z.prev_read_size)
#define count   (Z.count  )
#define seek    (Z.seek   )
#define skip    (Z.skip   )
#define infile  (Z.infile )
#define outfile (Z.outfile)

	memset(&Z, 0, sizeof(Z));
	INIT_G();
	//fflush_all(); - is this needed because of NOEXEC?

	for (i = 1; argv[i]; i++) {
		int what;
		char *val;
		char *arg = argv[i];

#if ENABLE_DESKTOP
		/* "dd --". NB: coreutils 6.9 will complain if they see
		 * more than one of them. We wouldn't. */
		if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0')
			continue;
#endif
		val = strchr(arg, '=');
		if (val == NULL)
			bb_show_usage();
		*val = '\0';
		what = index_in_strings(keywords, arg);
		if (what < 0)
			bb_show_usage();
		/* *val = '='; - to preserve ps listing? */
		val++;
#if ENABLE_FEATURE_DD_IBS_OBS
		if (what == OP_ibs) {
			/* Must fit into positive ssize_t */
			ibs = xatoul_range_sfx(val, 1, ((size_t)-1L)/2, cwbkMG_suffixes);
			/*continue;*/
		}
		if (what == OP_obs) {
			obs = xatoul_range_sfx(val, 1, ((size_t)-1L)/2, cwbkMG_suffixes);
			/*continue;*/
		}
		if (what == OP_conv) {
			G.flags |= parse_comma_flags(val, conv_words, "conv");
			/*continue;*/
		}
		if (what == OP_iflag) {
			G.flags |= parse_comma_flags(val, iflag_words, "iflag") << FLAG_IFLAG_SHIFT;
			/*continue;*/
		}
#endif
		if (what == OP_bs) {
			ibs = xatoul_range_sfx(val, 1, ((size_t)-1L)/2, cwbkMG_suffixes);
			obs = ibs;
			/*continue;*/
		}
		/* These can be large: */
		if (what == OP_count) {
			G.flags |= FLAG_COUNT;
			count = XATOU_SFX(val, cwbkMG_suffixes);
			/*continue;*/
		}
		if (what == OP_seek) {
			seek = XATOU_SFX(val, cwbkMG_suffixes);
			/*continue;*/
		}
		if (what == OP_skip) {
			skip = XATOU_SFX(val, cwbkMG_suffixes);
			/*continue;*/
		}
		if (what == OP_if) {
			infile = val;
			/*continue;*/
		}
		if (what == OP_of) {
			outfile = val;
			/*continue;*/
		}
#if ENABLE_FEATURE_DD_STATUS
		if (what == OP_status) {
			int n;
			n = index_in_strings(status_words, val);
			if (n < 0)
				bb_error_msg_and_die(bb_msg_invalid_arg_to, val, "status");
			G.flags |= FLAG_STATUS_NONE << n;
			/*continue;*/
		}
#endif
	} /* end of "for (argv[i])" */

//XXX:FIXME for huge ibs or obs, malloc'ing them isn't the brightest idea ever
	ibuf = xmalloc(ibs);
	obuf = ibuf;
#if ENABLE_FEATURE_DD_IBS_OBS
	if (ibs != obs) {
		G.flags |= FLAG_TWOBUFS;
		obuf = xmalloc(obs);
	}
#endif

#if ENABLE_FEATURE_DD_SIGNAL_HANDLING
	signal_SA_RESTART_empty_mask(SIGUSR1, dd_output_status);
#endif
#if ENABLE_FEATURE_DD_THIRD_STATUS_LINE
	G.begin_time_us = monotonic_us();
#endif

	if (infile) {
		xmove_fd(xopen(infile, O_RDONLY), ifd);
	} else {
		infile = bb_msg_standard_input;
	}
	if (outfile) {
		int oflag = O_WRONLY | O_CREAT;

		if (!seek && !(G.flags & FLAG_NOTRUNC))
			oflag |= O_TRUNC;

		xmove_fd(xopen(outfile, oflag), ofd);

		if (seek && !(G.flags & FLAG_NOTRUNC)) {
			if (ftruncate(ofd, seek * obs) < 0) {
				struct stat st;

				if (fstat(ofd, &st) < 0
				 || S_ISREG(st.st_mode)
				 || S_ISDIR(st.st_mode)
				) {
					goto die_outfile;
				}
			}
		}
	} else {
		outfile = bb_msg_standard_output;
	}
	if (skip) {
		size_t blocksz = (G.flags & FLAG_SKIP_BYTES) ? 1 : ibs;
		if (lseek(ifd, skip * blocksz, SEEK_CUR) < 0) {
			do {
				ssize_t n;
#if ENABLE_FEATURE_DD_IBS_OBS
				if (G.flags & FLAG_FULLBLOCK)
					n = full_read(ifd, ibuf, blocksz);
				else
#endif
					n = safe_read(ifd, ibuf, blocksz);
				if (n < 0)
					goto die_infile;
				if (n == 0)
					break;
			} while (--skip != 0);
		}
	}
	if (seek) {
		if (lseek(ofd, seek * obs, SEEK_CUR) < 0)
			goto die_outfile;
	}

	while (!(G.flags & FLAG_COUNT) || (G.in_full + G.in_part != count)) {
		ssize_t n;
#if ENABLE_FEATURE_DD_IBS_OBS
		if (G.flags & FLAG_FULLBLOCK)
			n = full_read(ifd, ibuf, ibs);
		else
#endif
			n = safe_read(ifd, ibuf, ibs);
		if (n == 0)
			break;
		if (n < 0) {
			/* "Bad block" */
			if (!(G.flags & FLAG_NOERROR))
				goto die_infile;
			bb_simple_perror_msg(infile);
			/* GNU dd with conv=noerror skips over bad blocks */
			xlseek(ifd, ibs, SEEK_CUR);
			/* conv=noerror,sync writes NULs,
			 * conv=noerror just ignores input bad blocks */
			n = 0;
		}
		if (G.flags & FLAG_SWAB) {
			uint16_t *p16;
			ssize_t n2;

			/* Our code allows only last read to be odd-sized */
			if (prev_read_size & 1)
				bb_error_msg_and_die("can't swab %lu byte buffer",
						(unsigned long)prev_read_size);
			prev_read_size = n;

			/* If n is odd, last byte is not swapped:
			 *  echo -n "qwe" | dd conv=swab
			 * prints "wqe".
			 */
			p16 = (void*) ibuf;
			n2 = (n >> 1);
			while (--n2 >= 0) {
				*p16 = bswap_16(*p16);
				p16++;
			}
		}
		if ((size_t)n == ibs)
			G.in_full++;
		else {
			G.in_part++;
			if (G.flags & FLAG_SYNC) {
				memset(ibuf + n, 0, ibs - n);
				n = ibs;
			}
		}
		if (G.flags & FLAG_TWOBUFS) {
			char *tmp = ibuf;
			while (n) {
				size_t d = obs - oc;

				if (d > (size_t)n)
					d = n;
				memcpy(obuf + oc, tmp, d);
				n -= d;
				tmp += d;
				oc += d;
				if (oc == obs) {
					if (write_and_stats(obuf, obs, obs, outfile))
						goto out_status;
					oc = 0;
				}
			}
		} else {
			if (write_and_stats(ibuf, n, obs, outfile))
				goto out_status;
		}
	}

	if (G.flags & FLAG_FSYNC) {
		if (fsync(ofd) < 0)
			goto die_outfile;
	}

	if (ENABLE_FEATURE_DD_IBS_OBS && oc) {
		if (write_and_stats(obuf, oc, obs, outfile))
			goto out_status;
	}
	if (close(ifd) < 0) {
 die_infile:
		bb_simple_perror_msg_and_die(infile);
	}

	if (close(ofd) < 0) {
 die_outfile:
		bb_simple_perror_msg_and_die(outfile);
	}

	exitcode = EXIT_SUCCESS;
 out_status:
	if (!ENABLE_FEATURE_DD_STATUS || !(G.flags & FLAG_STATUS_NONE))
		dd_output_status(0);

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(obuf);
		if (G.flags & FLAG_TWOBUFS)
			free(ibuf);
	}

	return exitcode;
}
