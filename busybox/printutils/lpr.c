/* vi: set sw=4 ts=4: */
/*
 * bare bones version of lpr & lpq: BSD printing utilities
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Original idea and code:
 *      Walter Harms <WHarms@bfs.de>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * See RFC 1179 for protocol description.
 */
//config:config LPR
//config:	bool "lpr (10 kb)"
//config:	default y
//config:	help
//config:	lpr sends files (or standard input) to a print spooling daemon.
//config:
//config:config LPQ
//config:	bool "lpq (10 kb)"
//config:	default y
//config:	help
//config:	lpq is a print spool queue examination and manipulation program.

//              APPLET_ODDNAME:name main  location        suid_type     help
//applet:IF_LPQ(APPLET_ODDNAME(lpq, lpqr, BB_DIR_USR_BIN, BB_SUID_DROP, lpq))
//applet:IF_LPR(APPLET_ODDNAME(lpr, lpqr, BB_DIR_USR_BIN, BB_SUID_DROP, lpr))

//kbuild:lib-$(CONFIG_LPR) += lpr.o
//kbuild:lib-$(CONFIG_LPQ) += lpr.o

//usage:#define lpr_trivial_usage
//usage:       "-P queue[@host[:port]] -U USERNAME -J TITLE -Vmh [FILE]..."
/* -C CLASS exists too, not shown.
 * CLASS is supposed to be printed on banner page, if one is requested */
//usage:#define lpr_full_usage "\n\n"
//usage:       "	-P	lp service to connect to (else uses $PRINTER)"
//usage:     "\n	-m	Send mail on completion"
//usage:     "\n	-h	Print banner page too"
//usage:     "\n	-V	Verbose"
//usage:
//usage:#define lpq_trivial_usage
//usage:       "[-P queue[@host[:port]]] [-U USERNAME] [-d JOBID]... [-fs]"
//usage:#define lpq_full_usage "\n\n"
//usage:       "	-P	lp service to connect to (else uses $PRINTER)"
//usage:     "\n	-d	Delete jobs"
//usage:     "\n	-f	Force any waiting job to be printed"
//usage:     "\n	-s	Short display"

#include "libbb.h"

/*
 * LPD returns binary 0 on success.
 * Otherwise it returns error message.
 */
static void get_response_or_say_and_die(int fd, const char *errmsg)
{
	ssize_t sz;
	char buf[128];

	buf[0] = ' ';
	sz = safe_read(fd, buf, 1);
	if ('\0' != buf[0]) {
		// request has failed
		// try to make sure last char is '\n', but do not add
		// superfluous one
		sz = full_read(fd, buf + 1, 126);
		bb_error_msg("error while %s%s", errmsg,
				(sz > 0 ? ". Server said:" : ""));
		if (sz > 0) {
			// sz = (bytes in buf) - 1
			if (buf[sz] != '\n')
				buf[++sz] = '\n';
			safe_write(STDERR_FILENO, buf, sz + 1);
		}
		xfunc_die();
	}
}

int lpqr_main(int argc, char *argv[]) MAIN_EXTERNALLY_VISIBLE;
int lpqr_main(int argc UNUSED_PARAM, char *argv[])
{
	enum {
		OPT_P           = 1 << 0, // -P queue[@host[:port]]. If no -P is given use $PRINTER, then "lp@localhost:515"
		OPT_U           = 1 << 1, // -U username

		LPR_V           = 1 << 2, // -V: be verbose
		LPR_h           = 1 << 3, // -h: want banner printed
		LPR_C           = 1 << 4, // -C class: job "class" (? supposedly printed on banner)
		LPR_J           = 1 << 5, // -J title: the job title for the banner page
		LPR_m           = 1 << 6, // -m: send mail back to user

		LPQ_SHORT_FMT   = 1 << 2, // -s: short listing format
		LPQ_DELETE      = 1 << 3, // -d: delete job(s)
		LPQ_FORCE       = 1 << 4, // -f: force waiting job(s) to be printed
	};
	char tempfile[sizeof("/tmp/lprXXXXXX")];
	const char *job_title;
	const char *printer_class = "";   // printer class, max 32 char
	const char *queue;                // name of printer queue
	const char *server = "localhost"; // server[:port] of printer queue
	char *hostname;
	// N.B. IMHO getenv("USER") can be way easily spoofed!
	const char *user = xuid2uname(getuid());
	unsigned job;
	unsigned opts;
	int fd;

	queue = getenv("PRINTER");
	if (!queue)
		queue = "lp";

	// parse options
	// TODO: set opt_complementary: s,d,f are mutually exclusive
	opts = getopt32(argv,
		(/*lp*/'r' == applet_name[2]) ? "P:U:VhC:J:m" : "P:U:sdf"
		, &queue, &user
		, &printer_class, &job_title
	);
	argv += optind;

	{
		// queue name is to the left of '@'
		char *s = strchr(queue, '@');
		if (s) {
			// server name is to the right of '@'
			*s = '\0';
			server = s + 1;
		}
	}

	// do connect
	fd = create_and_connect_stream_or_die(server, 515);

	//
	// LPQ ------------------------
	//
	if (/*lp*/'q' == applet_name[2]) {
		char cmd;
		// force printing of every job still in queue
		if (opts & LPQ_FORCE) {
			cmd = 1;
			goto command;
		// delete job(s)
		} else if (opts & LPQ_DELETE) {
			fdprintf(fd, "\x5" "%s %s", queue, user);
			while (*argv) {
				fdprintf(fd, " %s", *argv++);
			}
			bb_putchar('\n');
		// dump current jobs status
		// N.B. periodical polling should be achieved
		// via "watch -n delay lpq"
		// They say it's the UNIX-way :)
		} else {
			cmd = (opts & LPQ_SHORT_FMT) ? 3 : 4;
 command:
			fdprintf(fd, "%c" "%s\n", cmd, queue);
			bb_copyfd_eof(fd, STDOUT_FILENO);
		}

		return EXIT_SUCCESS;
	}

	//
	// LPR ------------------------
	//
	if (opts & LPR_V)
		bb_error_msg("connected to server");

	job = getpid() % 1000;
	hostname = safe_gethostname();

	// no files given on command line? -> use stdin
	if (!*argv)
		*--argv = (char *)"-";

	fdprintf(fd, "\x2" "%s\n", queue);
	get_response_or_say_and_die(fd, "setting queue");

	// process files
	do {
		unsigned cflen;
		int dfd;
		struct stat st;
		char *c;
		char *remote_filename;
		char *controlfile;

		// if data file is stdin, we need to dump it first
		if (LONE_DASH(*argv)) {
			strcpy(tempfile, "/tmp/lprXXXXXX");
			dfd = xmkstemp(tempfile);
			bb_copyfd_eof(STDIN_FILENO, dfd);
			xlseek(dfd, 0, SEEK_SET);
			*argv = (char*)bb_msg_standard_input;
		} else {
			dfd = xopen(*argv, O_RDONLY);
		}

		st.st_size = 0; /* paranoia: fstat may theoretically fail */
		fstat(dfd, &st);

		/* Apparently, some servers are buggy and won't accept 0-sized jobs.
		 * Standard lpr works around it by refusing to send such jobs:
		 */
		if (st.st_size == 0) {
			bb_error_msg("nothing to print");
			continue;
		}

		/* "The name ... should start with ASCII "cfA",
		 * followed by a three digit job number, followed
		 * by the host name which has constructed the file."
		 * We supply 'c' or 'd' as needed for control/data file. */
		remote_filename = xasprintf("fA%03u%s", job, hostname);

		// create control file
		// TODO: all lines but 2 last are constants! How we can use this fact?
		controlfile = xasprintf(
			"H" "%.32s\n" "P" "%.32s\n" /* H HOST, P USER */
			"C" "%.32s\n" /* C CLASS - printed on banner page (if L cmd is also given) */
			"J" "%.99s\n" /* J JOBNAME */
			/* "class name for banner page and job name
			 * for banner page commands must precede L command" */
			"L" "%.32s\n" /* L USER - print banner page, with given user's name */
			"M" "%.32s\n" /* M WHOM_TO_MAIL */
			"l" "d%.31s\n" /* l DATA_FILE_NAME ("dfAxxx") */
			, hostname, user
			, printer_class /* can be "" */
			, ((opts & LPR_J) ? job_title : *argv)
			, (opts & LPR_h) ? user : ""
			, (opts & LPR_m) ? user : ""
			, remote_filename
		);
		// delete possible "\nX\n" (that is, one-char) patterns
		c = controlfile;
		while ((c = strchr(c, '\n')) != NULL) {
			if (c[1] && c[2] == '\n') {
				overlapping_strcpy(c, c+2);
			} else {
				c++;
			}
		}

		// send control file
		if (opts & LPR_V)
			bb_error_msg("sending control file");
		/* "Acknowledgement processing must occur as usual
		 * after the command is sent." */
		cflen = (unsigned)strlen(controlfile);
		fdprintf(fd, "\x2" "%u c%s\n", cflen, remote_filename);
		get_response_or_say_and_die(fd, "sending control file");
		/* "Once all of the contents have
		 * been delivered, an octet of zero bits is sent as
		 * an indication that the file being sent is complete.
		 * A second level of acknowledgement processing
		 * must occur at this point." */
		full_write(fd, controlfile, cflen + 1); /* writes NUL byte too */
		get_response_or_say_and_die(fd, "sending control file");

		// send data file, with name "dfaXXX"
		if (opts & LPR_V)
			bb_error_msg("sending data file");
		fdprintf(fd, "\x3" "%"OFF_FMT"u d%s\n", st.st_size, remote_filename);
		get_response_or_say_and_die(fd, "sending data file");
		if (bb_copyfd_size(dfd, fd, st.st_size) != st.st_size) {
			// We're screwed. We sent less bytes than we advertised.
			bb_error_msg_and_die("local file changed size?!");
		}
		write(fd, "", 1); // send ACK
		get_response_or_say_and_die(fd, "sending data file");

		// delete temporary file if we dumped stdin
		if (*argv == (char*)bb_msg_standard_input)
			unlink(tempfile);

		// cleanup
		close(fd);
		free(remote_filename);
		free(controlfile);

		// say job accepted
		if (opts & LPR_V)
			bb_error_msg("job accepted");

		// next, please!
		job = (job + 1) % 1000;
	} while (*++argv);

	return EXIT_SUCCESS;
}
