/*	$NetBSD: director.c,v 1.10 2012/06/03 23:19:11 joerg Exp $	*/

/*-
 * Copyright 2009 Brett Lymn <blymn@NetBSD.org>
 *
 * All rights reserved.
 *
 * This code has been donated to The NetBSD Foundation by the Author.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <err.h>
#include "returns.h"

void yyparse(void);
#define DEF_TERMPATH "."
#define DEF_TERM "atf"
#define DEF_SLAVE "./slave"

const char *def_check_path = "./"; /* default check path */
const char *def_include_path = "./"; /* default include path */

extern size_t nvars;	/* In testlang_conf.y */
saved_data_t  saved_output;	/* In testlang_conf.y */
int cmdpipe[2];		/* command pipe between director and slave */
int slvpipe[2];		/* reply pipe back from slave */
int master;		/* pty to the slave */
int verbose;		/* control verbosity of tests */
const char *check_path;	/* path to prepend to check files for output
			   validation */
const char *include_path;	/* path to prepend to include files */
char *cur_file;		/* name of file currently being read */

void init_parse_variables(int); /* in testlang_parse.y */

/*
 * Handle the slave exiting unexpectedly, try to recover the exit message
 * and print it out.
 */
static void
slave_died(int param)
{
	char last_words[256];
	size_t count;

	fprintf(stderr, "ERROR: Slave has exited\n");
	if (saved_output.count > 0) {
		fprintf(stderr, "output from slave: ");
		for (count = 0; count < saved_output.count; count ++) {
			if (isprint((unsigned char)saved_output.data[count]))
			    fprintf(stderr, "%c", saved_output.data[count]);
		}
		fprintf(stderr, "\n");
	}

	if ((count = read(master, &last_words, 255)) > 0) {
		last_words[count] = '\0';
		fprintf(stderr, "slave exited with message \"%s\"\n",
			last_words);
	}

	exit(2);
}


static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-v] [-I include-path] [-C check-path] "
	    "[-T terminfo-file] [-s pathtoslave] [-t term] "
	    "commandfile\n", getprogname());
	fprintf(stderr, " where:\n");
	fprintf(stderr, "    -v enables verbose test output\n");
	fprintf(stderr, "    -T is a directory containing the terminfo.cdb "
	    "file, or a file holding the terminfo description n");
	fprintf(stderr, "    -s is the path to the slave executable\n");
	fprintf(stderr, "    -t is value to set TERM to for the test\n");
	fprintf(stderr, "    -I is the directory to include files\n");
	fprintf(stderr, "    -C is the directory for config files\n");
	fprintf(stderr, "    commandfile is a file of test directives\n");
	exit(1);
}


int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	const char *termpath, *term, *slave;
	int ch;
	pid_t slave_pid;
	extern FILE *yyin;
	char *arg1, *arg2, *arg3, *arg4;
	struct termios term_attr;
	struct stat st;

	termpath = term = slave = NULL;
	verbose = 0;

	while ((ch = getopt(argc, argv, "vC:I:p:s:t:T:")) != -1) {
		switch(ch) {
		case 'I':
			include_path = optarg;
			break;
		case 'C':
			check_path = optarg;
			break;
		case 'T':
			termpath = optarg;
			break;
		case 'p':
			termpath = optarg;
			break;
		case 's':
			slave = optarg;
			break;
		case 't':
			term = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv += optind;
	if (argc < 1)
		usage();

	if (termpath == NULL)
		termpath = DEF_TERMPATH;

	if (slave == NULL)
		slave = DEF_SLAVE;

	if (term == NULL)
		term = DEF_TERM;

	if (check_path == NULL)
		check_path = getenv("CHECK_PATH");
	if ((check_path == NULL) || (check_path[0] == '\0')) {
		warn("$CHECK_PATH not set, defaulting to %s", def_check_path);
		check_path = def_check_path;
	}

	if (include_path == NULL)
		include_path = getenv("INCLUDE_PATH");
	if ((include_path == NULL) || (include_path[0] == '\0')) {
		warn("$INCLUDE_PATH not set, defaulting to %s",
			def_include_path);
		include_path = def_include_path;
	}

	signal(SIGCHLD, slave_died);

	if (setenv("TERM", term, 1) != 0)
		err(2, "Failed to set TERM variable");

	if (stat(termpath, &st) == -1)
		err(1, "Cannot stat %s", termpath);

	if (S_ISDIR(st.st_mode)) {
		char tinfo[MAXPATHLEN];
		int l = snprintf(tinfo, sizeof(tinfo), "%s/%s", termpath,
		    "terminfo.cdb");
		if (stat(tinfo, &st) == -1)
			err(1, "Cannot stat `%s'", tinfo);
		if (l >= 4)
			tinfo[l - 4] = '\0';
		if (setenv("TERMINFO", tinfo, 1) != 0)
			err(1, "Failed to set TERMINFO variable");
	} else {
		int fd;
		char *tinfo;
		if ((fd = open(termpath, O_RDONLY)) == -1)
			err(1, "Cannot open `%s'", termpath);
		if ((tinfo = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_FILE,
			fd, 0)) == MAP_FAILED)
			err(1, "Cannot map `%s'", termpath);
		if (setenv("TERMINFO", tinfo, 1) != 0)
			err(1, "Failed to set TERMINFO variable");
		close(fd);
		munmap(tinfo, (size_t)st.st_size);
	}

	if (pipe(cmdpipe) < 0)
		err(1, "Command pipe creation failed");

	if (pipe(slvpipe) < 0)
		err(1, "Slave pipe creation failed");

	/*
	 * Create default termios settings for later use
	 */
	memset(&term_attr, 0, sizeof(term_attr));
	term_attr.c_iflag = TTYDEF_IFLAG;
	term_attr.c_oflag = TTYDEF_OFLAG;
	term_attr.c_cflag = TTYDEF_CFLAG;
	term_attr.c_lflag = TTYDEF_LFLAG;
	cfsetspeed(&term_attr, TTYDEF_SPEED);
	term_attr.c_cc[VERASE] = '\b';
	term_attr.c_cc[VKILL] = '\025'; /* ^U */

	if ((slave_pid = forkpty(&master, NULL, &term_attr, NULL)) < 0)
		err(1, "Fork of pty for slave failed\n");

	if (slave_pid == 0) {
		/* slave side, just exec the slave process */
		if (asprintf(&arg1, "%d", cmdpipe[0]) < 0)
			err(1, "arg1 conversion failed");

		if (asprintf(&arg2, "%d", cmdpipe[1]) < 0)
			err(1, "arg2 conversion failed");

		if (asprintf(&arg3, "%d", slvpipe[0]) < 0)
			err(1, "arg3 conversion failed");

		if (asprintf(&arg4, "%d", slvpipe[1]) < 0)
			err(1, "arg4 conversion failed");

		if (execl(slave, slave, arg1, arg2, arg3, arg4, NULL) < 0)
			err(1, "Exec of slave %s failed", slave);

		/* NOT REACHED */
	}

	fcntl(master, F_SETFL, O_NONBLOCK);

	if ((yyin = fopen(argv[0], "r")) == NULL)
		err(1, "Cannot open command file %s", argv[0]);

	if ((cur_file = strdup(argv[0])) == NULL)
		err(2, "Failed to alloc memory for test file name");

	init_parse_variables(1);

	yyparse();
	fclose(yyin);

	exit(0);
}
