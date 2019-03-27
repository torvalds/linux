/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Given a file containing sections with stabs data, convert the stabs data to
 * CTF data, and replace the stabs sections with a CTF section.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>
#include <errno.h>
#include <assert.h>

#include "ctftools.h"
#include "memory.h"

const  char *progname;
int debug_level = DEBUG_LEVEL;

static char *infile = NULL;
static const char *outfile = NULL;
static int dynsym;

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: %s [-gis] -l label | -L labelenv [-o outfile] object_file\n"
	    "\n"
	    "  Note: if -L labelenv is specified and labelenv is not set in\n"
	    "  the environment, a default value is used.\n",
	    progname);
}

static void
terminate_cleanup(void)
{
#if !defined(__FreeBSD__)
	if (!outfile) {
		fprintf(stderr, "Removing %s\n", infile);
		unlink(infile);
	}
#endif
}

static void
handle_sig(int sig)
{
	terminate("Caught signal %d - exiting\n", sig);
}

static int
file_read(tdata_t *td, char *filename, int ignore_non_c)
{
	typedef int (*reader_f)(tdata_t *, Elf *, char *);
	static reader_f readers[] = {
		stabs_read,
		dw_read,
		NULL
	};

	source_types_t source_types;
	Elf *elf;
	int i, rc, fd;

	if ((fd = open(filename, O_RDONLY)) < 0)
		terminate("failed to open %s", filename);

	(void) elf_version(EV_CURRENT);

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		close(fd);
		terminate("failed to read %s: %s\n", filename,
		    elf_errmsg(-1));
	}

	source_types = built_source_types(elf, filename);

	if ((source_types == SOURCE_NONE || (source_types & SOURCE_UNKNOWN)) &&
	    ignore_non_c) {
		debug(1, "Ignoring file %s from unknown sources\n", filename);
		exit(0);
	}

	for (i = 0; readers[i] != NULL; i++) {
		if ((rc = readers[i](td, elf, filename)) == 0)
			break;

		assert(rc < 0 && errno == ENOENT);
	}

	if (readers[i] == NULL) {
		/*
		 * None of the readers found compatible type data.
		 */

		if (findelfsecidx(elf, filename, ".debug") >= 0) {
			terminate("%s: DWARF version 1 is not supported\n",
			    filename);
		}

		if (!(source_types & SOURCE_C) && ignore_non_c) {
			debug(1, "Ignoring file %s not built from C sources\n",
			    filename);
			exit(0);
		}

		rc = 0;
	} else {
		rc = 1;
	}

	(void) elf_end(elf);
	(void) close(fd);

	return (rc);
}

int
main(int argc, char **argv)
{
	tdata_t *filetd, *mstrtd;
	const char *label = NULL;
	int verbose = 0;
	int ignore_non_c = 0;
	int keep_stabs = 0;
	int c;

#ifdef illumos
	sighold(SIGINT);
	sighold(SIGQUIT);
	sighold(SIGTERM);
#endif

	progname = basename(argv[0]);

	if (getenv("CTFCONVERT_DEBUG_LEVEL"))
		debug_level = atoi(getenv("CTFCONVERT_DEBUG_LEVEL"));
	if (getenv("CTFCONVERT_DEBUG_PARSE"))
		debug_parse = atoi(getenv("CTFCONVERT_DEBUG_PARSE"));

	while ((c = getopt(argc, argv, ":l:L:o:givs")) != EOF) {
		switch (c) {
		case 'l':
			label = optarg;
			break;
		case 'L':
			if ((label = getenv(optarg)) == NULL)
				label = CTF_DEFAULT_LABEL;
			break;
		case 'o':
			outfile = optarg;
			break;
		case 's':
			dynsym = CTF_USE_DYNSYM;
			break;
		case 'i':
			ignore_non_c = 1;
			break;
		case 'g':
			keep_stabs = CTF_KEEP_STABS;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			exit(2);
		}
	}

	if (getenv("STRIPSTABS_KEEP_STABS") != NULL)
		keep_stabs = CTF_KEEP_STABS;

	if (argc - optind != 1 || label == NULL) {
		usage();
		exit(2);
	}

	infile = argv[optind];
	if (access(infile, R_OK) != 0)
		terminate("Can't access %s", infile);

	/*
	 * Upon receipt of a signal, we want to clean up and exit.  Our
	 * primary goal during cleanup is to restore the system to a state
	 * such that a subsequent make will eventually cause this command to
	 * be re-run.  If we remove the input file (which we do if we get a
	 * signal and the user didn't specify a separate output file), make
	 * will need to rebuild the input file, and will then need to re-run
	 * ctfconvert, which is what we want.
	 */
	set_terminate_cleanup(terminate_cleanup);

#ifdef illumos
	sigset(SIGINT, handle_sig);
	sigset(SIGQUIT, handle_sig);
	sigset(SIGTERM, handle_sig);
#else
	signal(SIGINT, handle_sig);
	signal(SIGQUIT, handle_sig);
	signal(SIGTERM, handle_sig);
#endif

	filetd = tdata_new();

	if (!file_read(filetd, infile, ignore_non_c))
		terminate("%s doesn't have type data to convert\n", infile);

	if (verbose)
		iidesc_stats(filetd->td_iihash);

	mstrtd = tdata_new();
	merge_into_master(filetd, mstrtd, NULL, 1);

	tdata_label_add(mstrtd, label, CTF_LABEL_LASTIDX);

	/*
	 * If the user supplied an output file that is different from the
	 * input file, write directly to the output file.  Otherwise, write
	 * to a temporary file, and replace the input file when we're done.
	 */
	if (outfile && strcmp(infile, outfile) != 0) {
		write_ctf(mstrtd, infile, outfile, dynsym | keep_stabs);
	} else {
		char *tmpname = mktmpname(infile, ".ctf");
		write_ctf(mstrtd, infile, tmpname, dynsym | keep_stabs);
		if (rename(tmpname, infile) != 0)
			terminate("Couldn't rename temp file %s", tmpname);
		free(tmpname);
	}

	return (0);
}
