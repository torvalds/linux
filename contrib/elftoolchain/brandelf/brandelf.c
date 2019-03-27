/*-
 * Copyright (c) 2008 Hyogeol Lee
 * Copyright (c) 2000, 2001 David O'Brien
 * Copyright (c) 1996 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <getopt.h>
#include <libelf.h>
#include <libelftc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "_elftc.h"

ELFTC_VCSID("$Id: brandelf.c 3440 2016-04-07 14:51:47Z emaste $");

static int elftype(const char *);
static const char *iselftype(int);
static void printelftypes(void);
static void printversion(void);
static void usage(void);

struct ELFtypes {
	const char *str;
	int value;
};
/* XXX - any more types? */
static struct ELFtypes elftypes[] = {
	{ "86Open",	ELFOSABI_86OPEN },
	{ "AIX",	ELFOSABI_AIX },
	{ "ARM",	ELFOSABI_ARM },
	{ "AROS",	ELFOSABI_AROS },
	{ "CloudABI",	ELFOSABI_CLOUDABI },
	{ "FreeBSD",	ELFOSABI_FREEBSD },
	{ "GNU",	ELFOSABI_GNU },
	{ "HP/UX",	ELFOSABI_HPUX},
	{ "Hurd",	ELFOSABI_HURD },
	{ "IRIX",	ELFOSABI_IRIX },
	{ "Linux",	ELFOSABI_GNU },
	{ "Modesto",	ELFOSABI_MODESTO },
	{ "NSK",	ELFOSABI_NSK },
	{ "NetBSD",	ELFOSABI_NETBSD},
	{ "None",	ELFOSABI_NONE},
	{ "OpenBSD",	ELFOSABI_OPENBSD },
	{ "OpenVMS",	ELFOSABI_OPENVMS },
	{ "Standalone",	ELFOSABI_STANDALONE },
	{ "SVR4",	ELFOSABI_NONE },
	{ "Solaris",	ELFOSABI_SOLARIS },
	{ "Tru64",	ELFOSABI_TRU64 }
};

static struct option brandelf_longopts[] = {
	{ "help",	no_argument,	NULL,   'h' },
	{ "version",	no_argument,	NULL, 	'V' },
	{ NULL,		0,		NULL,	0   }
};

int
main(int argc, char **argv)
{
	GElf_Ehdr ehdr;
	Elf *elf;
	Elf_Kind kind;
	int type = ELFOSABI_NONE;
	int retval = 0;
	int ch, change = 0, force = 0, listed = 0;

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EXIT_FAILURE, "elf_version error");

	while ((ch = getopt_long(argc, argv, "Vf:hlt:v", brandelf_longopts,
		NULL)) != -1)
		switch (ch) {
		case 'f':
			if (change)
				errx(EXIT_FAILURE, "ERROR: the -f option is "
				    "incompatible with the -t option.");
			force = 1;
			type = atoi(optarg);
			if (errno == ERANGE || type < 0 || type > 255) {
				warnx("ERROR: invalid argument to option "
				    "-f: %s", optarg);
				usage();
			}
			break;
		case 'h':
			usage();
			break;
		case 'l':
			printelftypes();
			listed = 1;
			break;
		case 'v':
			/* This flag is ignored. */
			break;
		case 't':
			if (force)
				errx(EXIT_FAILURE, "the -t option is "
				    "incompatible with the -f option.");
			if ((type = elftype(optarg)) == -1) {
				warnx("ERROR: invalid ELF type '%s'", optarg);
				usage();
			}

			change = 1;
			break;
		case 'V':
			printversion();
			break;
		default:
			usage();
	}
	argc -= optind;
	argv += optind;
	if (!argc) {
		if (listed)
			exit(0);
		else {
			warnx("no file(s) specified");
			usage();
		}
	}

	while (argc) {
		int fd;

		elf = NULL;

		if ((fd = open(argv[0], (change || force) ? O_RDWR :
		    O_RDONLY, 0)) < 0) {
			warn("error opening file %s", argv[0]);
			retval = 1;
			goto fail;
		}

		if ((elf = elf_begin(fd, (change || force) ? ELF_C_RDWR :
		    ELF_C_READ, NULL)) == NULL) {
			warnx("elf_begin failed: %s", elf_errmsg(-1));
			retval = 1;
			goto fail;
		}

		if ((kind = elf_kind(elf)) != ELF_K_ELF) {
			if (kind == ELF_K_AR)
				warnx("file '%s' is an archive.", argv[0]);
			else
				warnx("file '%s' is not an ELF file.",
				    argv[0]);
			retval = 1;
			goto fail;
		}

		if (gelf_getehdr(elf, &ehdr) == NULL) {
			warnx("gelf_getehdr: %s", elf_errmsg(-1));
			retval = 1;
			goto fail;
		}

		if (!change && !force) {
			fprintf(stdout,
			    "File '%s' is of brand '%s' (%u).\n",
			    argv[0], iselftype(ehdr.e_ident[EI_OSABI]),
			    ehdr.e_ident[EI_OSABI]);
			if (!iselftype(type)) {
				warnx("ELF ABI Brand '%u' is unknown",
				      type);
				printelftypes();
			}
		} else {

			/*
			 * Keep the existing layout of the ELF object.
			 */
			if (elf_flagelf(elf, ELF_C_SET, ELF_F_LAYOUT) == 0) {
				warnx("elf_flagelf failed: %s",
				    elf_errmsg(-1));
				retval = 1;
				goto fail;
			}

			/*
			 * Update the ABI type.
			 */
			ehdr.e_ident[EI_OSABI] = (unsigned char) type;
			if (gelf_update_ehdr(elf, &ehdr) == 0) {
				warnx("gelf_update_ehdr error: %s",
				    elf_errmsg(-1));
				retval = 1;
				goto fail;
			}

			/*
			 * Write back changes.
			 */
			if (elf_update(elf, ELF_C_WRITE) == -1) {
				warnx("elf_update error: %s", elf_errmsg(-1));
				retval = 1;
				goto fail;
			}
		}
fail:

		if (elf)
			elf_end(elf);

		if (fd >= 0 && close(fd) == -1) {
			warnx("%s: close error", argv[0]);
			retval = 1;
		}

		argc--;
		argv++;
	}

	return (retval);
}

#define	USAGE_MESSAGE	"\
Usage: %s [options] file...\n\
  Set or display the ABI field for an ELF object.\n\n\
  Supported options are:\n\
  -f NUM                    Set the ELF ABI to the number 'NUM'.\n\
  -h | --help               Print a usage message and exit.\n\
  -l                        List known ELF ABI names.\n\
  -t ABI                    Set the ELF ABI to the value named by \"ABI\".\n\
  -V | --version            Print a version identifier and exit.\n"

static void
usage(void)
{
	(void) fprintf(stderr, USAGE_MESSAGE, ELFTC_GETPROGNAME());
	exit(1);
}

static void
printversion(void)
{
	(void) printf("%s (%s)\n", ELFTC_GETPROGNAME(), elftc_version());
	exit(0);
}

static const char *
iselftype(int etype)
{
	size_t elfwalk;

	for (elfwalk = 0;
	     elfwalk < sizeof(elftypes)/sizeof(elftypes[0]);
	     elfwalk++)
		if (etype == elftypes[elfwalk].value)
			return (elftypes[elfwalk].str);
	return (0);
}

static int
elftype(const char *elfstrtype)
{
	size_t elfwalk;

	for (elfwalk = 0;
	     elfwalk < sizeof(elftypes)/sizeof(elftypes[0]);
	     elfwalk++)
		if (strcasecmp(elfstrtype, elftypes[elfwalk].str) == 0)
			return (elftypes[elfwalk].value);
	return (-1);
}

static void
printelftypes(void)
{
	size_t elfwalk;

	(void) printf("Known ELF types are: ");
	for (elfwalk = 0;
	     elfwalk < sizeof(elftypes)/sizeof(elftypes[0]);
	     elfwalk++)
		(void) printf("%s(%u) ", elftypes[elfwalk].str,
		    elftypes[elfwalk].value);
	(void) printf("\n");
}
