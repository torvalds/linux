/* vi: set sw=4 ts=4: */
/*
 * micro lpd
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/*
 * A typical usage of BB lpd looks as follows:
 * # tcpsvd -E 0 515 lpd [SPOOLDIR] [HELPER-PROG [ARGS...]]
 *
 * This starts TCP listener on port 515 (default for LP protocol).
 * When a client connection is made (via lpr) lpd first changes its
 * working directory to SPOOLDIR (current dir is the default).
 *
 * SPOOLDIR is the spool directory which contains printing queues
 * and should have the following structure:
 *
 * SPOOLDIR/
 *      <queue1>
 *      ...
 *      <queueN>
 *
 * <queueX> can be of two types:
 *      A. a printer character device, an ordinary file or a link to such;
 *      B. a directory.
 *
 * In case A lpd just dumps the data it receives from client (lpr) to the
 * end of queue file/device. This is non-spooling mode.
 *
 * In case B lpd enters spooling mode. It reliably saves client data along
 * with control info in two unique files under the queue directory. These
 * files are named dfAXXXHHHH and cfAXXXHHHH, where XXX is the job number
 * and HHHH is the client hostname. Unless a printing helper application
 * is specified lpd is done at this point.
 *
 * NB: file names are produced by peer! They actually may be anything at all.
 * lpd only sanitizes them (by removing most non-alphanumerics).
 *
 * If HELPER-PROG (with optional arguments) is specified then lpd continues
 * to process client data:
 *      1. it reads and parses control file (cfA...). The parse process
 *      results in setting environment variables whose values were passed
 *      in control file; when parsing is complete, lpd deletes control file.
 *      2. it spawns specified helper application. It is then
 *      the helper application who is responsible for both actual printing
 *      and deleting of processed data file.
 *
 * A good lpr passes control files which when parsed provides the following
 * variables:
 * $H = host which issues the job
 * $P = user who prints
 * $C = class of printing (what is printed on banner page)
 * $J = the name of the job
 * $L = print banner page
 * $M = the user to whom a mail should be sent if a problem occurs
 *
 * We specifically filter out and NOT provide:
 * $l = name of datafile ("dfAxxx") - file whose content are to be printed
 *
 * lpd provides $DATAFILE instead - the ACTUAL name
 * of the datafile under which it was saved.
 * $l would be not reliable (you would be at mercy of remote peer).
 *
 * Thus, a typical helper can be something like this:
 * #!/bin/sh
 * cat ./"$DATAFILE" >/dev/lp0
 * mv -f ./"$DATAFILE" save/
 */
//config:config LPD
//config:	bool "lpd (5.3 kb)"
//config:	default y
//config:	help
//config:	lpd is a print spooling daemon.

//applet:IF_LPD(APPLET(lpd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_LPD) += lpd.o

//usage:#define lpd_trivial_usage
//usage:       "SPOOLDIR [HELPER [ARGS]]"
//usage:#define lpd_full_usage "\n\n"
//usage:       "SPOOLDIR must contain (symlinks to) device nodes or directories"
//usage:     "\nwith names matching print queue names. In the first case, jobs are"
//usage:     "\nsent directly to the device. Otherwise each job is stored in queue"
//usage:     "\ndirectory and HELPER program is called. Name of file to print"
//usage:     "\nis passed in $DATAFILE variable."
//usage:     "\nExample:"
//usage:     "\n	tcpsvd -E 0 515 softlimit -m 999999 lpd /var/spool ./print"

#include "libbb.h"

// strip argument of bad chars
static char *sane(char *str)
{
	char *s = str;
	char *p = s;
	while (*s) {
		if (isalnum(*s) || '-' == *s || '_' == *s) {
			*p++ = *s;
		}
		s++;
	}
	*p = '\0';
	return str;
}

static char *xmalloc_read_stdin(void)
{
	// SECURITY:
	size_t max = 4 * 1024; // more than enough for commands!
	return xmalloc_reads(STDIN_FILENO, &max);
}

int lpd_main(int argc, char *argv[]) MAIN_EXTERNALLY_VISIBLE;
int lpd_main(int argc UNUSED_PARAM, char *argv[])
{
	int spooling = spooling; // for compiler
	char *s, *queue;
	char *filenames[2];

	// goto spool directory
	if (*++argv)
		xchdir(*argv++);

	// error messages of xfuncs will be sent over network
	xdup2(STDOUT_FILENO, STDERR_FILENO);

	// nullify ctrl/data filenames
	memset(filenames, 0, sizeof(filenames));

	// read command
	s = queue = xmalloc_read_stdin();
	// we understand only "receive job" command
	if (2 != *queue) {
 unsupported_cmd:
		printf("Command %02x %s\n",
			(unsigned char)s[0], "is not supported");
		goto err_exit;
	}

	// parse command: "2 | QUEUE_NAME | '\n'"
	queue++;
	// protect against "/../" attacks
	// *strchrnul(queue, '\n') = '\0'; - redundant, sane() will do
	if (!*sane(queue))
		return EXIT_FAILURE;

	// queue is a directory -> chdir to it and enter spooling mode
	spooling = chdir(queue) + 1; // 0: cannot chdir, 1: done
	// we don't free(s), we might need "queue" var later

	while (1) {
		char *fname;
		int fd;
		// int is easier than ssize_t: can use xatoi_positive,
		// and can correctly display error returns (-1)
		int expected_len, real_len;

		// signal OK
		safe_write(STDOUT_FILENO, "", 1);

		// get subcommand
		// valid s must be of form: "SUBCMD | LEN | space | FNAME"
		// N.B. we bail out on any error
		s = xmalloc_read_stdin();
		if (!s) { // (probably) EOF
			char *p, *q, var[2];

			// non-spooling mode or no spool helper specified
			if (!spooling || !*argv)
				return EXIT_SUCCESS; // the only non-error exit
			// spooling mode but we didn't see both ctrlfile & datafile
			if (spooling != 7)
				goto err_exit; // reject job

			// spooling mode and spool helper specified -> exec spool helper
			// (we exit 127 if helper cannot be executed)
			var[1] = '\0';
			// read and delete ctrlfile
			q = xmalloc_xopen_read_close(filenames[0], NULL);
			unlink(filenames[0]);
			// provide datafile name
			// we can use leaky setenv since we are about to exec or exit
			xsetenv("DATAFILE", filenames[1]);
			// parse control file by "\n"
			while ((p = strchr(q, '\n')) != NULL && isalpha(*q)) {
				*p++ = '\0';
				// q is a line of <SYM><VALUE>,
				// we are setting environment string <SYM>=<VALUE>.
				// Ignoring "l<datafile>", exporting others:
				if (*q != 'l') {
					var[0] = *q++;
					xsetenv(var, q);
				}
				q = p; // next line
			}
			// helper should not talk over network.
			// this call reopens stdio fds to "/dev/null".
			bb_daemon_helper(DAEMON_DEVNULL_STDIO);
			BB_EXECVP_or_die(argv);
		}

		// validate input.
		// we understand only "control file" or "data file" cmds
		if (2 != s[0] && 3 != s[0])
			goto unsupported_cmd;
		if (spooling & (1 << (s[0]-1))) {
			puts("Duplicated subcommand");
			goto err_exit;
		}
		// get filename
		chomp(s);
		fname = strchr(s, ' ');
		if (!fname) {
// bad_fname:
			puts("No or bad filename");
			goto err_exit;
		}
		*fname++ = '\0';
//		// s[0]==2: ctrlfile, must start with 'c'
//		// s[0]==3: datafile, must start with 'd'
//		if (fname[0] != s[0] + ('c'-2))
//			goto bad_fname;
		// get length
		expected_len = bb_strtou(s + 1, NULL, 10);
		if (errno || expected_len < 0) {
			puts("Bad length");
			goto err_exit;
		}
		if (2 == s[0] && expected_len > 16 * 1024) {
			// SECURITY:
			// ctrlfile can't be big (we want to read it back later!)
			puts("File is too big");
			goto err_exit;
		}

		// open the file
		if (spooling) {
			// spooling mode: dump both files
			// job in flight has mode 0200 "only writable"
			sane(fname);
			fd = open3_or_warn(fname, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0200);
			if (fd < 0)
				goto err_exit;
			filenames[s[0] - 2] = xstrdup(fname);
		} else {
			// non-spooling mode:
			// 2: control file (ignoring), 3: data file
			fd = -1;
			if (3 == s[0])
				fd = xopen(queue, O_RDWR | O_APPEND);
		}

		// signal OK
		safe_write(STDOUT_FILENO, "", 1);

		// copy the file
		real_len = bb_copyfd_size(STDIN_FILENO, fd, expected_len);
		if (real_len != expected_len) {
			printf("Expected %d but got %d bytes\n",
				expected_len, real_len);
			goto err_exit;
		}
		// get EOF indicator, see whether it is NUL (ok)
		// (and don't trash s[0]!)
		if (safe_read(STDIN_FILENO, &s[1], 1) != 1 || s[1] != 0) {
			// don't send error msg to peer - it obviously
			// doesn't follow the protocol, so probably
			// it can't understand us either
			goto err_exit;
		}

		if (spooling) {
			// chmod completely downloaded file as "readable+writable"
			fchmod(fd, 0600);
			// accumulate dump state
			// N.B. after all files are dumped spooling should be 1+2+4==7
			spooling |= (1 << (s[0]-1)); // bit 1: ctrlfile; bit 2: datafile
		}

		free(s);
		close(fd); // NB: can do close(-1). Who cares?

		// NB: don't do "signal OK" write here, it will be done
		// at the top of the loop
	} // while (1)

 err_exit:
	// don't keep corrupted files
	if (spooling) {
#define i spooling
		for (i = 2; --i >= 0; )
			if (filenames[i])
				unlink(filenames[i]);
	}
	return EXIT_FAILURE;
}
