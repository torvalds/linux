/*-
 * Copyright (c) 2007 S.Sam Arun Raj
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libelf.h>
#include <libelftc.h>
#include <gelf.h>

#include "_elftc.h"

ELFTC_VCSID("$Id: strings.c 3648 2018-11-22 23:26:43Z emaste $");

enum radix_style {
	RADIX_DECIMAL,
	RADIX_HEX,
	RADIX_OCTAL
};

enum encoding_style {
	ENCODING_7BIT,
	ENCODING_8BIT,
	ENCODING_16BIT_BIG,
	ENCODING_16BIT_LITTLE,
	ENCODING_32BIT_BIG,
	ENCODING_32BIT_LITTLE
};

#define PRINTABLE(c)						\
      ((c) >= 0 && (c) <= 255 &&				\
	  ((c) == '\t' || isprint((c)) ||			\
	      (encoding == ENCODING_8BIT && (c) > 127)))

static int encoding_size, entire_file, show_filename, show_loc;
static enum encoding_style encoding;
static enum radix_style radix;
static intmax_t min_len;

static struct option strings_longopts[] = {
	{ "all",		no_argument,		NULL,	'a'},
	{ "bytes",		required_argument,	NULL,	'n'},
	{ "encoding",		required_argument,	NULL,	'e'},
	{ "help",		no_argument,		NULL,	'h'},
	{ "print-file-name",	no_argument,		NULL,	'f'},
	{ "radix",		required_argument,	NULL,	't'},
	{ "version",		no_argument,		NULL,	'v'},
	{ NULL, 0, NULL, 0 }
};

int	getcharacter(FILE *, long *);
int	handle_file(const char *);
int	handle_elf(const char *, FILE *);
int	handle_binary(const char *, FILE *, size_t);
int	find_strings(const char *, FILE *, off_t, off_t);
void	show_version(void);
void	usage(void);

/*
 * strings(1) extracts text(contiguous printable characters)
 * from elf and binary files.
 */
int
main(int argc, char **argv)
{
	int ch, rc;

	rc = 0;
	min_len = 0;
	encoding_size = 1;
	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EXIT_FAILURE, "ELF library initialization failed: %s",
		    elf_errmsg(-1));

	while ((ch = getopt_long(argc, argv, "1234567890ae:fhn:ot:Vv",
	    strings_longopts, NULL)) != -1) {
		switch ((char)ch) {
		case 'a':
			entire_file = 1;
			break;
		case 'e':
			if (*optarg == 's') {
				encoding = ENCODING_7BIT;
			} else if (*optarg == 'S') {
				encoding = ENCODING_8BIT;
			} else if (*optarg == 'b') {
				encoding = ENCODING_16BIT_BIG;
				encoding_size = 2;
			} else if (*optarg == 'B') {
				encoding = ENCODING_32BIT_BIG;
				encoding_size = 4;
			} else if (*optarg == 'l') {
				encoding = ENCODING_16BIT_LITTLE;
				encoding_size = 2;
			} else if (*optarg == 'L') {
				encoding = ENCODING_32BIT_LITTLE;
				encoding_size = 4;
			} else
				usage();
			        /* NOTREACHED */
			break;
		case 'f':
			show_filename = 1;
			break;
		case 'n':
			min_len = strtoimax(optarg, (char**)NULL, 10);
			if (min_len <= 0)
				errx(EX_USAGE, "option -n should specify a "
				    "positive decimal integer.");
			break;
		case 'o':
			show_loc = 1;
			radix = RADIX_OCTAL;
			break;
		case 't':
			show_loc = 1;
			if (*optarg == 'd')
				radix = RADIX_DECIMAL;
			else if (*optarg == 'o')
				radix = RADIX_OCTAL;
			else if (*optarg == 'x')
				radix = RADIX_HEX;
			else
				usage();
			        /* NOTREACHED */
			break;
		case 'v':
		case 'V':
			show_version();
			/* NOTREACHED */
		case '0':
	        case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			min_len *= 10;
			min_len += ch - '0';
			break;
		case 'h':
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (min_len == 0)
		min_len = 4;
	if (*argv == NULL)
		rc = find_strings("{standard input}", stdin, 0, 0);
	else while (*argv != NULL) {
		if (handle_file(*argv) != 0)
			rc = 1;
		argv++;
	}
	return (rc);
}

int
handle_file(const char *name)
{
	FILE *pfile;
	int rt;

	if (name == NULL)
		return (1);
	pfile = fopen(name, "rb");
	if (pfile == NULL) {
		warnx("'%s': %s", name, strerror(errno));
		return (1);
	}

	rt = handle_elf(name, pfile);
	fclose(pfile);
	return (rt);
}

/*
 * Files not understood by handle_elf, will be passed off here and will
 * treated as a binary file. This would include text file, core dumps ...
 */
int
handle_binary(const char *name, FILE *pfile, size_t size)
{

	(void)fseeko(pfile, 0, SEEK_SET);
	return (find_strings(name, pfile, 0, size));
}

/*
 * Will analyse a file to see if it ELF, other files including ar(1),
 * core dumps are passed off and treated as flat binary files. Unlike
 * GNU size in FreeBSD this routine will not treat ELF object from
 * different archs as flat binary files(has to overridden using -a).
 */
int
handle_elf(const char *name, FILE *pfile)
{
	struct stat buf;
	GElf_Ehdr elfhdr;
	GElf_Shdr shdr;
	Elf *elf;
	Elf_Scn *scn;
	int rc, fd;

	rc = 0;
	fd = fileno(pfile);
	if (fstat(fd, &buf) < 0)
		return (1);

	/* If entire file is chosen, treat it as a binary file */
	if (entire_file)
		return (handle_binary(name, pfile, buf.st_size));

	(void)lseek(fd, 0, SEEK_SET);
	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf_kind(elf) != ELF_K_ELF) {
		(void)elf_end(elf);
		return (handle_binary(name, pfile, buf.st_size));
	}

	if (gelf_getehdr(elf, &elfhdr) == NULL) {
		(void)elf_end(elf);
		warnx("%s: ELF file could not be processed", name);
		return (1);
	}

	if (elfhdr.e_shnum == 0 && elfhdr.e_type == ET_CORE) {
		(void)elf_end(elf);
		return (handle_binary(name, pfile, buf.st_size));
	} else {
		scn = NULL;
		while ((scn = elf_nextscn(elf, scn)) != NULL) {
			if (gelf_getshdr(scn, &shdr) == NULL)
				continue;
			if (shdr.sh_type != SHT_NOBITS &&
			    (shdr.sh_flags & SHF_ALLOC) != 0) {
				rc = find_strings(name, pfile, shdr.sh_offset,
				    shdr.sh_size);
			}
		}
	}
	(void)elf_end(elf);
	return (rc);
}

/*
 * Retrieves a character from input stream based on the encoding
 * type requested.
 */
int
getcharacter(FILE *pfile, long *rt)
{
	int i, c;
	char buf[4];

	for(i = 0; i < encoding_size; i++) {
		c = getc(pfile);
		if (c == EOF)
			return (-1);
		buf[i] = c;
	}

	switch (encoding) {
	case ENCODING_7BIT:
	case ENCODING_8BIT:
		*rt = buf[0];
		break;
	case ENCODING_16BIT_BIG:
		*rt = (buf[0] << 8) | buf[1];
		break;
	case ENCODING_16BIT_LITTLE:
		*rt = buf[0] | (buf[1] << 8);
		break;
	case ENCODING_32BIT_BIG:
		*rt = ((long) buf[0] << 24) | ((long) buf[1] << 16) |
		    ((long) buf[2] << 8) | buf[3];
		break;
	case ENCODING_32BIT_LITTLE:
		*rt = buf[0] | ((long) buf[1] << 8) | ((long) buf[2] << 16) |
		    ((long) buf[3] << 24);
		break;
	default:
		return (-1);
	}

	return (0);
}

/*
 * Input stream is read until the end of file is reached or until
 * the section size is reached in case of ELF files. Contiguous
 * characters of >= min_size(default 4) will be displayed.
 */
int
find_strings(const char *name, FILE *pfile, off_t offset, off_t size)
{
	off_t cur_off, start_off;
	char *obuf;
	long c;
	int i;

	if ((obuf = (char*)calloc(1, min_len + 1)) == NULL) {
		fprintf(stderr, "Unable to allocate memory: %s\n",
		    strerror(errno));
		return (1);
	}

	(void)fseeko(pfile, offset, SEEK_SET);
	cur_off = offset;
	start_off = 0;
	for (;;) {
		if ((offset + size) && (cur_off >= offset + size))
			break;
		start_off = cur_off;
		memset(obuf, 0, min_len + 1);
		for(i = 0; i < min_len; i++) {
			if (getcharacter(pfile, &c) < 0)
				goto _exit1;
			if (PRINTABLE(c)) {
				obuf[i] = c;
				obuf[i + 1] = 0;
				cur_off += encoding_size;
			} else {
				if (encoding == ENCODING_8BIT &&
				    (uint8_t)c > 127) {
					obuf[i] = c;
					obuf[i + 1] = 0;
					cur_off += encoding_size;
					continue;
				}
				cur_off += encoding_size;
				break;
			}
		}

		if (i >= min_len && ((cur_off <= offset + size) ||
		    !(offset + size))) {
			if (show_filename)
				printf("%s: ", name);
			if (show_loc) {
				switch (radix) {
				case RADIX_DECIMAL:
					printf("%7ju ", (uintmax_t)start_off);
					break;
				case RADIX_HEX:
					printf("%7jx ", (uintmax_t)start_off);
					break;
				case RADIX_OCTAL:
					printf("%7jo ", (uintmax_t)start_off);
					break;
				}
			}
			printf("%s", obuf);

			for (;;) {
				if ((offset + size) &&
				    (cur_off >= offset + size))
					break;
				if (getcharacter(pfile, &c) < 0)
					break;
				cur_off += encoding_size;
				if (encoding == ENCODING_8BIT &&
				    (uint8_t)c > 127) {
					putchar(c);
					continue;
				}
				if (!PRINTABLE(c))
					break;
				putchar(c);
			}
			putchar('\n');
		}
	}
_exit1:
	free(obuf);
	return (0);
}

#define	USAGE_MESSAGE	"\
Usage: %s [options] [file...]\n\
  Print contiguous sequences of printable characters.\n\n\
  Options:\n\
  -a     | --all               Scan the entire file for strings.\n\
  -e ENC | --encoding=ENC      Select the character encoding to use.\n\
  -f     | --print-file-name   Print the file name before each string.\n\
  -h     | --help              Print a help message and exit.\n\
  -n N   | --bytes=N | -N      Print sequences with 'N' or more characters.\n\
  -o                           Print offsets in octal.\n\
  -t R   | --radix=R           Print offsets using the radix named by 'R'.\n\
  -v     | --version           Print a version identifier and exit.\n"

void
usage(void)
{

	fprintf(stderr, USAGE_MESSAGE, ELFTC_GETPROGNAME());
	exit(EXIT_FAILURE);
}

void
show_version(void)
{

        printf("%s (%s)\n", ELFTC_GETPROGNAME(), elftc_version());
        exit(EXIT_SUCCESS);
}
