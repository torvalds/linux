/* vi: set sw=4 ts=4: */
/*
 * Mini tail implementation for busybox
 *
 * Copyright (C) 2001 by Matt Kraai <kraai@alumni.carnegiemellon.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* Mar 16, 2003      Manuel Novoa III   (mjn3@codepoet.org)
 *
 * Pretty much rewritten to fix numerous bugs and reduce realloc() calls.
 * Bugs fixed (although I may have forgotten one or two... it was pretty bad)
 * 1) mixing printf/write without fflush()ing stdout
 * 2) no check that any open files are present
 * 3) optstring had -q taking an arg
 * 4) no error checking on write in some cases, and a warning even then
 * 5) q and s interaction bug
 * 6) no check for lseek error
 * 7) lseek attempted when count==0 even if arg was +0 (from top)
 */
//config:config TAIL
//config:	bool "tail (7.1 kb)"
//config:	default y
//config:	help
//config:	tail is used to print the last specified number of lines
//config:	from files.
//config:
//config:config FEATURE_FANCY_TAIL
//config:	bool "Enable -q, -s, -v, and -F options"
//config:	default y
//config:	depends on TAIL
//config:	help
//config:	These options are provided by GNU tail, but
//config:	are not specified in the SUSv3 standard:
//config:		-q      Never output headers giving file names
//config:		-s SEC  Wait SEC seconds between reads with -f
//config:		-v      Always output headers giving file names
//config:		-F      Same as -f, but keep retrying

//applet:IF_TAIL(APPLET(tail, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_TAIL) += tail.o

/* BB_AUDIT SUSv3 compliant (need fancy for -c) */
/* BB_AUDIT GNU compatible -c, -q, and -v options in 'fancy' configuration. */
/* http://www.opengroup.org/onlinepubs/007904975/utilities/tail.html */

//usage:#define tail_trivial_usage
//usage:       "[OPTIONS] [FILE]..."
//usage:#define tail_full_usage "\n\n"
//usage:       "Print last 10 lines of each FILE (or stdin) to stdout.\n"
//usage:       "With more than one FILE, precede each with a filename header.\n"
//usage:     "\n	-f		Print data as file grows"
//usage:     "\n	-c [+]N[kbm]	Print last N bytes"
//usage:     "\n	-n N[kbm]	Print last N lines"
//usage:     "\n	-n +N[kbm]	Start on Nth line and print the rest"
//usage:	IF_FEATURE_FANCY_TAIL(
//usage:     "\n	-q		Never print headers"
//usage:     "\n	-s SECONDS	Wait SECONDS between reads with -f"
//usage:     "\n	-v		Always print headers"
//usage:     "\n	-F		Same as -f, but keep retrying"
//usage:     "\n"
//usage:     "\nN may be suffixed by k (x1024), b (x512), or m (x1024^2)."
//usage:	)
//usage:
//usage:#define tail_example_usage
//usage:       "$ tail -n 1 /etc/resolv.conf\n"
//usage:       "nameserver 10.0.0.1\n"

#include "libbb.h"
#include "common_bufsiz.h"

struct globals {
	bool from_top;
	bool exitcode;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void tail_xprint_header(const char *fmt, const char *filename)
{
	if (fdprintf(STDOUT_FILENO, fmt, filename) < 0)
		bb_perror_nomsg_and_die();
}

static ssize_t tail_read(int fd, char *buf, size_t count)
{
	ssize_t r;

	r = full_read(fd, buf, count);
	if (r < 0) {
		bb_perror_msg(bb_msg_read_error);
		G.exitcode = EXIT_FAILURE;
	}

	return r;
}

#define header_fmt_str "\n==> %s <==\n"

static unsigned eat_num(const char *p)
{
	if (*p == '-')
		p++;
	else if (*p == '+') {
		p++;
		G.from_top = 1;
	}
	return xatou_sfx(p, bkm_suffixes);
}

int tail_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tail_main(int argc, char **argv)
{
	unsigned count = 10;
	unsigned sleep_period = 1;
	const char *str_c, *str_n;

	char *tailbuf;
	size_t tailbufsize;
	unsigned header_threshhold = 1;
	unsigned nfiles;
	int i, opt;

	int *fds;
	const char *fmt;
	int prev_fd;

	INIT_G();

#if ENABLE_INCLUDE_SUSv2 || ENABLE_FEATURE_FANCY_TAIL
	/* Allow legacy syntax of an initial numeric option without -n. */
	if (argv[1] && (argv[1][0] == '+' || argv[1][0] == '-')
	 && isdigit(argv[1][1])
	) {
		count = eat_num(argv[1]);
		argv++;
		argc--;
	}
#endif

	/* -s NUM, -F imlies -f */
	opt = getopt32(argv, IF_FEATURE_FANCY_TAIL("^")
			"fc:n:"IF_FEATURE_FANCY_TAIL("qs:+vF")
			IF_FEATURE_FANCY_TAIL("\0" "Ff"),
			&str_c, &str_n IF_FEATURE_FANCY_TAIL(,&sleep_period)
	);
#define FOLLOW (opt & 0x1)
#define COUNT_BYTES (opt & 0x2)
	//if (opt & 0x1) // -f
	if (opt & 0x2) count = eat_num(str_c); // -c
	if (opt & 0x4) count = eat_num(str_n); // -n
#if ENABLE_FEATURE_FANCY_TAIL
	/* q: make it impossible for nfiles to be > header_threshhold */
	if (opt & 0x8) header_threshhold = UINT_MAX; // -q
	//if (opt & 0x10) // -s
	if (opt & 0x20) header_threshhold = 0; // -v
# define FOLLOW_RETRY (opt & 0x40)
#else
# define FOLLOW_RETRY 0
#endif
	argc -= optind;
	argv += optind;

	/* open all the files */
	fds = xmalloc(sizeof(fds[0]) * (argc + 1));
	if (!argv[0]) {
		struct stat statbuf;

		if (fstat(STDIN_FILENO, &statbuf) == 0
		 && S_ISFIFO(statbuf.st_mode)
		) {
			opt &= ~1; /* clear FOLLOW */
		}
		argv[0] = (char *) bb_msg_standard_input;
	}
	nfiles = i = 0;
	do {
		int fd = open_or_warn_stdin(argv[i]);
		if (fd < 0 && !FOLLOW_RETRY) {
			G.exitcode = EXIT_FAILURE;
			continue;
		}
		fds[nfiles] = fd;
		argv[nfiles++] = argv[i];
	} while (++i < argc);

	if (!nfiles)
		bb_error_msg_and_die("no files");

	/* prepare the buffer */
	tailbufsize = BUFSIZ;
	if (!G.from_top && COUNT_BYTES) {
		if (tailbufsize < count + BUFSIZ) {
			tailbufsize = count + BUFSIZ;
		}
	}
	/* tail -c1024m REGULAR_FILE doesn't really need 1G mem block.
	 * (In fact, it doesn't need ANY memory). So delay allocation.
	 */
	tailbuf = NULL;

	/* tail the files */

	fmt = header_fmt_str + 1; /* skip leading newline in the header on the first output */
	i = 0;
	do {
		char *buf;
		int taillen;
		int newlines_seen;
		unsigned seen;
		int nread;
		int fd = fds[i];

		if (ENABLE_FEATURE_FANCY_TAIL && fd < 0)
			continue; /* may happen with -F */

		if (nfiles > header_threshhold) {
			tail_xprint_header(fmt, argv[i]);
			fmt = header_fmt_str;
		}

		if (!G.from_top) {
			off_t current = lseek(fd, 0, SEEK_END);
			if (current > 0) {
				unsigned off;
				if (COUNT_BYTES) {
				/* Optimizing count-bytes case if the file is seekable.
				 * Beware of backing up too far.
				 * Also we exclude files with size 0 (because of /proc/xxx) */
					if (count == 0)
						continue; /* showing zero bytes is easy :) */
					current -= count;
					if (current < 0)
						current = 0;
					xlseek(fd, current, SEEK_SET);
					bb_copyfd_size(fd, STDOUT_FILENO, count);
					continue;
				}
#if 1 /* This is technically incorrect for *LONG* strings, but very useful */
				/* Optimizing count-lines case if the file is seekable.
				 * We assume the lines are <64k.
				 * (Users complain that tail takes too long
				 * on multi-gigabyte files) */
				off = (count | 0xf); /* for small counts, be more paranoid */
				if (off > (INT_MAX / (64*1024)))
					off = (INT_MAX / (64*1024));
				current -= off * (64*1024);
				if (current < 0)
					current = 0;
				xlseek(fd, current, SEEK_SET);
#endif
			}
		}

		if (!tailbuf)
			tailbuf = xmalloc(tailbufsize);

		buf = tailbuf;
		taillen = 0;
		/* "We saw 1st line/byte".
		 * Used only by +N code ("start from Nth", 1-based): */
		seen = 1;
		newlines_seen = 0;
		while ((nread = tail_read(fd, buf, tailbufsize - taillen)) > 0) {
			if (G.from_top) {
				int nwrite = nread;
				if (seen < count) {
					/* We need to skip a few more bytes/lines */
					if (COUNT_BYTES) {
						nwrite -= (count - seen);
						seen += nread;
					} else {
						char *s = buf;
						do {
							--nwrite;
							if (*s++ == '\n' && ++seen == count) {
								break;
							}
						} while (nwrite);
					}
				}
				if (nwrite > 0)
					xwrite(STDOUT_FILENO, buf + nread - nwrite, nwrite);
			} else if (count) {
				if (COUNT_BYTES) {
					taillen += nread;
					if (taillen > (int)count) {
						memmove(tailbuf, tailbuf + taillen - count, count);
						taillen = count;
					}
				} else {
					int k = nread;
					int newlines_in_buf = 0;

					do { /* count '\n' in last read */
						k--;
						if (buf[k] == '\n') {
							newlines_in_buf++;
						}
					} while (k);

					if (newlines_seen + newlines_in_buf < (int)count) {
						newlines_seen += newlines_in_buf;
						taillen += nread;
					} else {
						int extra = (buf[nread-1] != '\n');
						char *s;

						k = newlines_seen + newlines_in_buf + extra - count;
						s = tailbuf;
						while (k) {
							if (*s == '\n') {
								k--;
							}
							s++;
						}
						taillen += nread - (s - tailbuf);
						memmove(tailbuf, s, taillen);
						newlines_seen = count - extra;
					}
					if (tailbufsize < (size_t)taillen + BUFSIZ) {
						tailbufsize = taillen + BUFSIZ;
						tailbuf = xrealloc(tailbuf, tailbufsize);
					}
				}
				buf = tailbuf + taillen;
			}
		} /* while (tail_read() > 0) */
		if (!G.from_top) {
			xwrite(STDOUT_FILENO, tailbuf, taillen);
		}
	} while (++i < nfiles);
	prev_fd = fds[i-1];

	tailbuf = xrealloc(tailbuf, BUFSIZ);

	fmt = NULL;

	if (FOLLOW) while (1) {
		sleep(sleep_period);

		i = 0;
		do {
			int nread;
			const char *filename = argv[i];
			int fd = fds[i];

			if (FOLLOW_RETRY) {
				struct stat sbuf, fsbuf;

				if (fd < 0
				 || fstat(fd, &fsbuf) < 0
				 || stat(filename, &sbuf) < 0
				 || fsbuf.st_dev != sbuf.st_dev
				 || fsbuf.st_ino != sbuf.st_ino
				) {
					int new_fd;

					if (fd >= 0)
						close(fd);
					new_fd = open(filename, O_RDONLY);
					if (new_fd >= 0) {
						bb_error_msg("%s has %s; following end of new file",
							filename, (fd < 0) ? "appeared" : "been replaced"
						);
					} else if (fd >= 0) {
						bb_perror_msg("%s has become inaccessible", filename);
					}
					fds[i] = fd = new_fd;
				}
			}
			if (ENABLE_FEATURE_FANCY_TAIL && fd < 0)
				continue;
			if (nfiles > header_threshhold) {
				fmt = header_fmt_str;
			}
			for (;;) {
				/* tail -f keeps following files even if they are truncated */
				struct stat sbuf;
				/* /proc files report zero st_size, don't lseek them */
				if (fstat(fd, &sbuf) == 0 && sbuf.st_size > 0) {
					off_t current = lseek(fd, 0, SEEK_CUR);
					if (sbuf.st_size < current)
						xlseek(fd, 0, SEEK_SET);
				}

				nread = tail_read(fd, tailbuf, BUFSIZ);
				if (nread <= 0)
					break;
				if (fmt && (fd != prev_fd)) {
					tail_xprint_header(fmt, filename);
					fmt = NULL;
					prev_fd = fd;
				}
				xwrite(STDOUT_FILENO, tailbuf, nread);
			}
		} while (++i < nfiles);
	} /* while (1) */

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(fds);
		free(tailbuf);
	}
	return G.exitcode;
}
