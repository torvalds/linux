/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "cpio_platform.h"
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <archive.h>
#include <archive_entry.h>

#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_GRP_H
#include <grp.h>
#endif
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include "cpio.h"
#include "err.h"
#include "line_reader.h"
#include "passphrase.h"

/* Fixed size of uname/gname caches. */
#define	name_cache_size 101

#ifndef O_BINARY
#define O_BINARY 0
#endif

struct name_cache {
	int	probes;
	int	hits;
	size_t	size;
	struct {
		id_t id;
		char *name;
	} cache[name_cache_size];
};

static int	extract_data(struct archive *, struct archive *);
const char *	cpio_i64toa(int64_t);
static const char *cpio_rename(const char *name);
static int	entry_to_archive(struct cpio *, struct archive_entry *);
static int	file_to_archive(struct cpio *, const char *);
static void	free_cache(struct name_cache *cache);
static void	list_item_verbose(struct cpio *, struct archive_entry *);
static void	long_help(void) __LA_DEAD;
static const char *lookup_gname(struct cpio *, gid_t gid);
static int	lookup_gname_helper(struct cpio *,
		    const char **name, id_t gid);
static const char *lookup_uname(struct cpio *, uid_t uid);
static int	lookup_uname_helper(struct cpio *,
		    const char **name, id_t uid);
static void	mode_in(struct cpio *) __LA_DEAD;
static void	mode_list(struct cpio *) __LA_DEAD;
static void	mode_out(struct cpio *);
static void	mode_pass(struct cpio *, const char *);
static const char *remove_leading_slash(const char *);
static int	restore_time(struct cpio *, struct archive_entry *,
		    const char *, int fd);
static void	usage(void) __LA_DEAD;
static void	version(void) __LA_DEAD;
static const char * passphrase_callback(struct archive *, void *);
static void	passphrase_free(char *);

int
main(int argc, char *argv[])
{
	static char buff[16384];
	struct cpio _cpio; /* Allocated on stack. */
	struct cpio *cpio;
	const char *errmsg;
	char *tptr;
	int uid, gid;
	int opt, t;

	cpio = &_cpio;
	memset(cpio, 0, sizeof(*cpio));
	cpio->buff = buff;
	cpio->buff_size = sizeof(buff);

#if defined(HAVE_SIGACTION) && defined(SIGPIPE)
	{ /* Ignore SIGPIPE signals. */
		struct sigaction sa;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_IGN;
		sigaction(SIGPIPE, &sa, NULL);
	}
#endif

	/* Set lafe_progname before calling lafe_warnc. */
	lafe_setprogname(*argv, "bsdcpio");

#if HAVE_SETLOCALE
	if (setlocale(LC_ALL, "") == NULL)
		lafe_warnc(0, "Failed to set default locale");
#endif

	cpio->uid_override = -1;
	cpio->gid_override = -1;
	cpio->argv = argv;
	cpio->argc = argc;
	cpio->mode = '\0';
	cpio->verbose = 0;
	cpio->compress = '\0';
	cpio->extract_flags = ARCHIVE_EXTRACT_NO_AUTODIR;
	cpio->extract_flags |= ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER;
	cpio->extract_flags |= ARCHIVE_EXTRACT_SECURE_SYMLINKS;
	cpio->extract_flags |= ARCHIVE_EXTRACT_SECURE_NODOTDOT;
	cpio->extract_flags |= ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;
	cpio->extract_flags |= ARCHIVE_EXTRACT_PERM;
	cpio->extract_flags |= ARCHIVE_EXTRACT_FFLAGS;
	cpio->extract_flags |= ARCHIVE_EXTRACT_ACL;
#if !defined(_WIN32) && !defined(__CYGWIN__)
	if (geteuid() == 0)
		cpio->extract_flags |= ARCHIVE_EXTRACT_OWNER;
#endif
	cpio->bytes_per_block = 512;
	cpio->filename = NULL;

	cpio->matching = archive_match_new();
	if (cpio->matching == NULL)
		lafe_errc(1, 0, "Out of memory");

	while ((opt = cpio_getopt(cpio)) != -1) {
		switch (opt) {
		case '0': /* GNU convention: --null, -0 */
			cpio->option_null = 1;
			break;
		case 'A': /* NetBSD/OpenBSD */
			cpio->option_append = 1;
			break;
		case 'a': /* POSIX 1997 */
			cpio->option_atime_restore = 1;
			break;
		case 'B': /* POSIX 1997 */
			cpio->bytes_per_block = 5120;
			break;
		case OPTION_B64ENCODE:
			cpio->add_filter = opt;
			break;
		case 'C': /* NetBSD/OpenBSD */
			errno = 0;
			tptr = NULL;
			t = (int)strtol(cpio->argument, &tptr, 10);
			if (errno || t <= 0 || *(cpio->argument) == '\0' ||
			    tptr == NULL || *tptr != '\0') {
				lafe_errc(1, 0, "Invalid blocksize: %s",
				    cpio->argument);
			}
			cpio->bytes_per_block = t;
			break;
		case 'c': /* POSIX 1997 */
			cpio->format = "odc";
			break;
		case 'd': /* POSIX 1997 */
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_NO_AUTODIR;
			break;
		case 'E': /* NetBSD/OpenBSD */
			if (archive_match_include_pattern_from_file(
			    cpio->matching, cpio->argument,
			    cpio->option_null) != ARCHIVE_OK)
				lafe_errc(1, 0, "Error : %s",
				    archive_error_string(cpio->matching));
			break;
		case 'F': /* NetBSD/OpenBSD/GNU cpio */
			cpio->filename = cpio->argument;
			break;
		case 'f': /* POSIX 1997 */
			if (archive_match_exclude_pattern(cpio->matching,
			    cpio->argument) != ARCHIVE_OK)
				lafe_errc(1, 0, "Error : %s",
				    archive_error_string(cpio->matching));
			break;
		case OPTION_GRZIP:
			cpio->compress = opt;
			break;
		case 'H': /* GNU cpio (also --format) */
			cpio->format = cpio->argument;
			break;
		case 'h':
			long_help();
			break;
		case 'I': /* NetBSD/OpenBSD */
			cpio->filename = cpio->argument;
			break;
		case 'i': /* POSIX 1997 */
			if (cpio->mode != '\0')
				lafe_errc(1, 0,
				    "Cannot use both -i and -%c", cpio->mode);
			cpio->mode = opt;
			break;
		case 'J': /* GNU tar, others */
			cpio->compress = opt;
			break;
		case 'j': /* GNU tar, others */
			cpio->compress = opt;
			break;
		case OPTION_INSECURE:
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_SECURE_SYMLINKS;
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_SECURE_NODOTDOT;
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;
			break;
		case 'L': /* GNU cpio */
			cpio->option_follow_links = 1;
			break;
		case 'l': /* POSIX 1997 */
			cpio->option_link = 1;
			break;
		case OPTION_LRZIP:
		case OPTION_LZ4:
		case OPTION_LZMA: /* GNU tar, others */
		case OPTION_LZOP: /* GNU tar, others */
		case OPTION_ZSTD:
			cpio->compress = opt;
			break;
		case 'm': /* POSIX 1997 */
			cpio->extract_flags |= ARCHIVE_EXTRACT_TIME;
			break;
		case 'n': /* GNU cpio */
			cpio->option_numeric_uid_gid = 1;
			break;
		case OPTION_NO_PRESERVE_OWNER: /* GNU cpio */
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_OWNER;
			break;
		case 'O': /* GNU cpio */
			cpio->filename = cpio->argument;
			break;
		case 'o': /* POSIX 1997 */
			if (cpio->mode != '\0')
				lafe_errc(1, 0,
				    "Cannot use both -o and -%c", cpio->mode);
			cpio->mode = opt;
			break;
		case 'p': /* POSIX 1997 */
			if (cpio->mode != '\0')
				lafe_errc(1, 0,
				    "Cannot use both -p and -%c", cpio->mode);
			cpio->mode = opt;
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_SECURE_NODOTDOT;
			cpio->extract_flags &= ~ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS;
			break;
		case OPTION_PASSPHRASE:
			cpio->passphrase = cpio->argument;
			break;
		case OPTION_PRESERVE_OWNER:
			cpio->extract_flags |= ARCHIVE_EXTRACT_OWNER;
			break;
		case OPTION_QUIET: /* GNU cpio */
			cpio->quiet = 1;
			break;
		case 'R': /* GNU cpio, also --owner */
			/* TODO: owner_parse should return uname/gname
			 * also; use that to set [ug]name_override. */
			errmsg = owner_parse(cpio->argument, &uid, &gid);
			if (errmsg) {
				lafe_warnc(-1, "%s", errmsg);
				usage();
			}
			if (uid != -1) {
				cpio->uid_override = uid;
				cpio->uname_override = NULL;
			}
			if (gid != -1) {
				cpio->gid_override = gid;
				cpio->gname_override = NULL;
			}
			break;
		case 'r': /* POSIX 1997 */
			cpio->option_rename = 1;
			break;
		case 't': /* POSIX 1997 */
			cpio->option_list = 1;
			break;
		case 'u': /* POSIX 1997 */
			cpio->extract_flags
			    &= ~ARCHIVE_EXTRACT_NO_OVERWRITE_NEWER;
			break;
		case OPTION_UUENCODE:
			cpio->add_filter = opt;
			break;
		case 'v': /* POSIX 1997 */
			cpio->verbose++;
			break;
		case 'V': /* GNU cpio */
			cpio->dot++;
			break;
		case OPTION_VERSION: /* GNU convention */
			version();
			break;
#if 0
	        /*
		 * cpio_getopt() handles -W specially, so it's not
		 * available here.
		 */
		case 'W': /* Obscure, but useful GNU convention. */
			break;
#endif
		case 'y': /* tar convention */
			cpio->compress = opt;
			break;
		case 'Z': /* tar convention */
			cpio->compress = opt;
			break;
		case 'z': /* tar convention */
			cpio->compress = opt;
			break;
		default:
			usage();
		}
	}

	/*
	 * Sanity-check args, error out on nonsensical combinations.
	 */
	/* -t implies -i if no mode was specified. */
	if (cpio->option_list && cpio->mode == '\0')
		cpio->mode = 'i';
	/* -t requires -i */
	if (cpio->option_list && cpio->mode != 'i')
		lafe_errc(1, 0, "Option -t requires -i");
	/* -n requires -it */
	if (cpio->option_numeric_uid_gid && !cpio->option_list)
		lafe_errc(1, 0, "Option -n requires -it");
	/* Can only specify format when writing */
	if (cpio->format != NULL && cpio->mode != 'o')
		lafe_errc(1, 0, "Option --format requires -o");
	/* -l requires -p */
	if (cpio->option_link && cpio->mode != 'p')
		lafe_errc(1, 0, "Option -l requires -p");
	/* -v overrides -V */
	if (cpio->dot && cpio->verbose)
		cpio->dot = 0;
	/* TODO: Flag other nonsensical combinations. */

	switch (cpio->mode) {
	case 'o':
		/* TODO: Implement old binary format in libarchive,
		   use that here. */
		if (cpio->format == NULL)
			cpio->format = "odc"; /* Default format */

		mode_out(cpio);
		break;
	case 'i':
		while (*cpio->argv != NULL) {
			if (archive_match_include_pattern(cpio->matching,
			    *cpio->argv) != ARCHIVE_OK)
				lafe_errc(1, 0, "Error : %s",
				    archive_error_string(cpio->matching));
			--cpio->argc;
			++cpio->argv;
		}
		if (cpio->option_list)
			mode_list(cpio);
		else
			mode_in(cpio);
		break;
	case 'p':
		if (*cpio->argv == NULL || **cpio->argv == '\0')
			lafe_errc(1, 0,
			    "-p mode requires a target directory");
		mode_pass(cpio, *cpio->argv);
		break;
	default:
		lafe_errc(1, 0,
		    "Must specify at least one of -i, -o, or -p");
	}

	archive_match_free(cpio->matching);
	free_cache(cpio->gname_cache);
	free_cache(cpio->uname_cache);
	free(cpio->destdir);
	passphrase_free(cpio->ppbuff);
	return (cpio->return_value);
}

static void
usage(void)
{
	const char	*p;

	p = lafe_getprogname();

	fprintf(stderr, "Brief Usage:\n");
	fprintf(stderr, "  List:    %s -it < archive\n", p);
	fprintf(stderr, "  Extract: %s -i < archive\n", p);
	fprintf(stderr, "  Create:  %s -o < filenames > archive\n", p);
	fprintf(stderr, "  Help:    %s --help\n", p);
	exit(1);
}

static const char *long_help_msg =
	"First option must be a mode specifier:\n"
	"  -i Input  -o Output  -p Pass\n"
	"Common Options:\n"
	"  -v Verbose filenames     -V  one dot per file\n"
	"Create: %p -o [options]  < [list of files] > [archive]\n"
	"  -J,-y,-z,--lzma  Compress archive with xz/bzip2/gzip/lzma\n"
	"  --format {odc|newc|ustar}  Select archive format\n"
	"List: %p -it < [archive]\n"
	"Extract: %p -i [options] < [archive]\n";


/*
 * Note that the word 'bsdcpio' will always appear in the first line
 * of output.
 *
 * In particular, /bin/sh scripts that need to test for the presence
 * of bsdcpio can use the following template:
 *
 * if (cpio --help 2>&1 | grep bsdcpio >/dev/null 2>&1 ) then \
 *          echo bsdcpio; else echo not bsdcpio; fi
 */
static void
long_help(void)
{
	const char	*prog;
	const char	*p;

	prog = lafe_getprogname();

	fflush(stderr);

	p = (strcmp(prog,"bsdcpio") != 0) ? "(bsdcpio)" : "";
	printf("%s%s: manipulate archive files\n", prog, p);

	for (p = long_help_msg; *p != '\0'; p++) {
		if (*p == '%') {
			if (p[1] == 'p') {
				fputs(prog, stdout);
				p++;
			} else
				putchar('%');
		} else
			putchar(*p);
	}
	version();
}

static void
version(void)
{
	fprintf(stdout,"bsdcpio %s - %s \n",
	    BSDCPIO_VERSION_STRING,
	    archive_version_details());
	exit(0);
}

static void
mode_out(struct cpio *cpio)
{
	struct archive_entry *entry, *spare;
	struct lafe_line_reader *lr;
	const char *p;
	int r;

	if (cpio->option_append)
		lafe_errc(1, 0, "Append mode not yet supported.");

	cpio->archive_read_disk = archive_read_disk_new();
	if (cpio->archive_read_disk == NULL)
		lafe_errc(1, 0, "Failed to allocate archive object");
	if (cpio->option_follow_links)
		archive_read_disk_set_symlink_logical(cpio->archive_read_disk);
	else
		archive_read_disk_set_symlink_physical(cpio->archive_read_disk);
	archive_read_disk_set_standard_lookup(cpio->archive_read_disk);

	cpio->archive = archive_write_new();
	if (cpio->archive == NULL)
		lafe_errc(1, 0, "Failed to allocate archive object");
	switch (cpio->compress) {
	case OPTION_GRZIP:
		r = archive_write_add_filter_grzip(cpio->archive);
		break;
	case 'J':
		r = archive_write_add_filter_xz(cpio->archive);
		break;
	case OPTION_LRZIP:
		r = archive_write_add_filter_lrzip(cpio->archive);
		break;
	case OPTION_LZ4:
		r = archive_write_add_filter_lz4(cpio->archive);
		break;
	case OPTION_LZMA:
		r = archive_write_add_filter_lzma(cpio->archive);
		break;
	case OPTION_LZOP:
		r = archive_write_add_filter_lzop(cpio->archive);
		break;
	case OPTION_ZSTD:
		r = archive_write_add_filter_zstd(cpio->archive);
		break;
	case 'j': case 'y':
		r = archive_write_add_filter_bzip2(cpio->archive);
		break;
	case 'z':
		r = archive_write_add_filter_gzip(cpio->archive);
		break;
	case 'Z':
		r = archive_write_add_filter_compress(cpio->archive);
		break;
	default:
		r = archive_write_add_filter_none(cpio->archive);
		break;
	}
	if (r < ARCHIVE_WARN)
		lafe_errc(1, 0, "Requested compression not available");
	switch (cpio->add_filter) {
	case 0:
		r = ARCHIVE_OK;
		break;
	case OPTION_B64ENCODE:
		r = archive_write_add_filter_b64encode(cpio->archive);
		break;
	case OPTION_UUENCODE:
		r = archive_write_add_filter_uuencode(cpio->archive);
		break;
	}
	if (r < ARCHIVE_WARN)
		lafe_errc(1, 0, "Requested filter not available");
	r = archive_write_set_format_by_name(cpio->archive, cpio->format);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(cpio->archive));
	archive_write_set_bytes_per_block(cpio->archive, cpio->bytes_per_block);
	cpio->linkresolver = archive_entry_linkresolver_new();
	archive_entry_linkresolver_set_strategy(cpio->linkresolver,
	    archive_format(cpio->archive));
	if (cpio->passphrase != NULL)
		r = archive_write_set_passphrase(cpio->archive,
			cpio->passphrase);
	else
		r = archive_write_set_passphrase_callback(cpio->archive, cpio,
			&passphrase_callback);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(cpio->archive));

	/*
	 * The main loop:  Copy each file into the output archive.
	 */
	r = archive_write_open_filename(cpio->archive, cpio->filename);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(cpio->archive));
	lr = lafe_line_reader("-", cpio->option_null);
	while ((p = lafe_line_reader_next(lr)) != NULL)
		file_to_archive(cpio, p);
	lafe_line_reader_free(lr);

	/*
	 * The hardlink detection may have queued up a couple of entries
	 * that can now be flushed.
	 */
	entry = NULL;
	archive_entry_linkify(cpio->linkresolver, &entry, &spare);
	while (entry != NULL) {
		entry_to_archive(cpio, entry);
		archive_entry_free(entry);
		entry = NULL;
		archive_entry_linkify(cpio->linkresolver, &entry, &spare);
	}

	r = archive_write_close(cpio->archive);
	if (cpio->dot)
		fprintf(stderr, "\n");
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(cpio->archive));

	if (!cpio->quiet) {
		int64_t blocks =
			(archive_filter_bytes(cpio->archive, 0) + 511)
			/ 512;
		fprintf(stderr, "%lu %s\n", (unsigned long)blocks,
		    blocks == 1 ? "block" : "blocks");
	}
	archive_write_free(cpio->archive);
	archive_entry_linkresolver_free(cpio->linkresolver);
}

static const char *
remove_leading_slash(const char *p)
{
	const char *rp;

	/* Remove leading "//./" or "//?/" or "//?/UNC/"
	 * (absolute path prefixes used by Windows API) */
	if ((p[0] == '/' || p[0] == '\\') &&
	    (p[1] == '/' || p[1] == '\\') &&
	    (p[2] == '.' || p[2] == '?') &&
	    (p[3] == '/' || p[3] == '\\'))
	{
		if (p[2] == '?' &&
		    (p[4] == 'U' || p[4] == 'u') &&
		    (p[5] == 'N' || p[5] == 'n') &&
		    (p[6] == 'C' || p[6] == 'c') &&
		    (p[7] == '/' || p[7] == '\\'))
			p += 8;
		else
			p += 4;
	}
	do {
		rp = p;
		/* Remove leading drive letter from archives created
		 * on Windows. */
		if (((p[0] >= 'a' && p[0] <= 'z') ||
		     (p[0] >= 'A' && p[0] <= 'Z')) &&
			 p[1] == ':') {
			p += 2;
		}
		/* Remove leading "/../", "//", etc. */
		while (p[0] == '/' || p[0] == '\\') {
			if (p[1] == '.' && p[2] == '.' &&
				(p[3] == '/' || p[3] == '\\')) {
				p += 3; /* Remove "/..", leave "/"
					 * for next pass. */
			} else
				p += 1; /* Remove "/". */
		}
	} while (rp != p);
	return (p);
}

/*
 * This is used by both out mode (to copy objects from disk into
 * an archive) and pass mode (to copy objects from disk to
 * an archive_write_disk "archive").
 */
static int
file_to_archive(struct cpio *cpio, const char *srcpath)
{
	const char *destpath;
	struct archive_entry *entry, *spare;
	size_t len;
	int r;

	/*
	 * Create an archive_entry describing the source file.
	 *
	 */
	entry = archive_entry_new();
	if (entry == NULL)
		lafe_errc(1, 0, "Couldn't allocate entry");
	archive_entry_copy_sourcepath(entry, srcpath);
	r = archive_read_disk_entry_from_file(cpio->archive_read_disk,
	    entry, -1, NULL);
	if (r < ARCHIVE_FAILED)
		lafe_errc(1, 0, "%s",
		    archive_error_string(cpio->archive_read_disk));
	if (r < ARCHIVE_OK)
		lafe_warnc(0, "%s",
		    archive_error_string(cpio->archive_read_disk));
	if (r <= ARCHIVE_FAILED) {
		archive_entry_free(entry);
		cpio->return_value = 1;
		return (r);
	}

	if (cpio->uid_override >= 0) {
		archive_entry_set_uid(entry, cpio->uid_override);
		archive_entry_set_uname(entry, cpio->uname_override);
	}
	if (cpio->gid_override >= 0) {
		archive_entry_set_gid(entry, cpio->gid_override);
		archive_entry_set_gname(entry, cpio->gname_override);
	}

	/*
	 * Generate a destination path for this entry.
	 * "destination path" is the name to which it will be copied in
	 * pass mode or the name that will go into the archive in
	 * output mode.
	 */
	destpath = srcpath;
	if (cpio->destdir) {
		len = strlen(cpio->destdir) + strlen(srcpath) + 8;
		if (len >= cpio->pass_destpath_alloc) {
			while (len >= cpio->pass_destpath_alloc) {
				cpio->pass_destpath_alloc += 512;
				cpio->pass_destpath_alloc *= 2;
			}
			free(cpio->pass_destpath);
			cpio->pass_destpath = malloc(cpio->pass_destpath_alloc);
			if (cpio->pass_destpath == NULL)
				lafe_errc(1, ENOMEM,
				    "Can't allocate path buffer");
		}
		strcpy(cpio->pass_destpath, cpio->destdir);
		strcat(cpio->pass_destpath, remove_leading_slash(srcpath));
		destpath = cpio->pass_destpath;
	}
	if (cpio->option_rename)
		destpath = cpio_rename(destpath);
	if (destpath == NULL) {
		archive_entry_free(entry);
		return (0);
	}
	archive_entry_copy_pathname(entry, destpath);

	/*
	 * If we're trying to preserve hardlinks, match them here.
	 */
	spare = NULL;
	if (cpio->linkresolver != NULL
	    && archive_entry_filetype(entry) != AE_IFDIR) {
		archive_entry_linkify(cpio->linkresolver, &entry, &spare);
	}

	if (entry != NULL) {
		r = entry_to_archive(cpio, entry);
		archive_entry_free(entry);
		if (spare != NULL) {
			if (r == 0)
				r = entry_to_archive(cpio, spare);
			archive_entry_free(spare);
		}
	}
	return (r);
}

static int
entry_to_archive(struct cpio *cpio, struct archive_entry *entry)
{
	const char *destpath = archive_entry_pathname(entry);
	const char *srcpath = archive_entry_sourcepath(entry);
	int fd = -1;
	ssize_t bytes_read;
	int r;

	/* Print out the destination name to the user. */
	if (cpio->verbose)
		fprintf(stderr,"%s", destpath);
	if (cpio->dot)
		fprintf(stderr, ".");

	/*
	 * Option_link only makes sense in pass mode and for
	 * regular files.  Also note: if a link operation fails
	 * because of cross-device restrictions, we'll fall back
	 * to copy mode for that entry.
	 *
	 * TODO: Test other cpio implementations to see if they
	 * hard-link anything other than regular files here.
	 */
	if (cpio->option_link
	    && archive_entry_filetype(entry) == AE_IFREG)
	{
		struct archive_entry *t;
		/* Save the original entry in case we need it later. */
		t = archive_entry_clone(entry);
		if (t == NULL)
			lafe_errc(1, ENOMEM, "Can't create link");
		/* Note: link(2) doesn't create parent directories,
		 * so we use archive_write_header() instead as a
		 * convenience. */
		archive_entry_set_hardlink(t, srcpath);
		/* This is a straight link that carries no data. */
		archive_entry_set_size(t, 0);
		r = archive_write_header(cpio->archive, t);
		archive_entry_free(t);
		if (r != ARCHIVE_OK)
			lafe_warnc(archive_errno(cpio->archive),
			    "%s", archive_error_string(cpio->archive));
		if (r == ARCHIVE_FATAL)
			exit(1);
#ifdef EXDEV
		if (r != ARCHIVE_OK && archive_errno(cpio->archive) == EXDEV) {
			/* Cross-device link:  Just fall through and use
			 * the original entry to copy the file over. */
			lafe_warnc(0, "Copying file instead");
		} else
#endif
		return (0);
	}

	/*
	 * Make sure we can open the file (if necessary) before
	 * trying to write the header.
	 */
	if (archive_entry_filetype(entry) == AE_IFREG) {
		if (archive_entry_size(entry) > 0) {
			fd = open(srcpath, O_RDONLY | O_BINARY);
			if (fd < 0) {
				lafe_warnc(errno,
				    "%s: could not open file", srcpath);
				goto cleanup;
			}
		}
	} else {
		archive_entry_set_size(entry, 0);
	}

	r = archive_write_header(cpio->archive, entry);

	if (r != ARCHIVE_OK)
		lafe_warnc(archive_errno(cpio->archive),
		    "%s: %s",
		    srcpath,
		    archive_error_string(cpio->archive));

	if (r == ARCHIVE_FATAL)
		exit(1);

	if (r >= ARCHIVE_WARN && archive_entry_size(entry) > 0 && fd >= 0) {
		bytes_read = read(fd, cpio->buff, (unsigned)cpio->buff_size);
		while (bytes_read > 0) {
			ssize_t bytes_write;
			bytes_write = archive_write_data(cpio->archive,
			    cpio->buff, bytes_read);
			if (bytes_write < 0)
				lafe_errc(1, archive_errno(cpio->archive),
				    "%s", archive_error_string(cpio->archive));
			if (bytes_write < bytes_read) {
				lafe_warnc(0,
				    "Truncated write; file may have "
				    "grown while being archived.");
			}
			bytes_read = read(fd, cpio->buff,
			    (unsigned)cpio->buff_size);
		}
	}

	fd = restore_time(cpio, entry, srcpath, fd);

cleanup:
	if (cpio->verbose)
		fprintf(stderr,"\n");
	if (fd >= 0)
		close(fd);
	return (0);
}

static int
restore_time(struct cpio *cpio, struct archive_entry *entry,
    const char *name, int fd)
{
#ifndef HAVE_UTIMES
	static int warned = 0;

	(void)cpio; /* UNUSED */
	(void)entry; /* UNUSED */
	(void)name; /* UNUSED */

	if (!warned)
		lafe_warnc(0, "Can't restore access times on this platform");
	warned = 1;
	return (fd);
#else
#if defined(_WIN32) && !defined(__CYGWIN__)
	struct __timeval times[2];
#else
	struct timeval times[2];
#endif

	if (!cpio->option_atime_restore)
		return (fd);

        times[1].tv_sec = archive_entry_mtime(entry);
        times[1].tv_usec = archive_entry_mtime_nsec(entry) / 1000;

        times[0].tv_sec = archive_entry_atime(entry);
        times[0].tv_usec = archive_entry_atime_nsec(entry) / 1000;

#if defined(HAVE_FUTIMES) && !defined(__CYGWIN__)
        if (fd >= 0 && futimes(fd, times) == 0)
		return (fd);
#endif
	/*
	 * Some platform cannot restore access times if the file descriptor
	 * is still opened.
	 */
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}

#ifdef HAVE_LUTIMES
        if (lutimes(name, times) != 0)
#else
        if ((AE_IFLNK != archive_entry_filetype(entry))
			&& utimes(name, times) != 0)
#endif
                lafe_warnc(errno, "Can't update time for %s", name);
#endif
	return (fd);
}


static void
mode_in(struct cpio *cpio)
{
	struct archive *a;
	struct archive_entry *entry;
	struct archive *ext;
	const char *destpath;
	int r;

	ext = archive_write_disk_new();
	if (ext == NULL)
		lafe_errc(1, 0, "Couldn't allocate restore object");
	r = archive_write_disk_set_options(ext, cpio->extract_flags);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(ext));
	a = archive_read_new();
	if (a == NULL)
		lafe_errc(1, 0, "Couldn't allocate archive object");
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	if (cpio->passphrase != NULL)
		r = archive_read_add_passphrase(a, cpio->passphrase);
	else
		r = archive_read_set_passphrase_callback(a, cpio,
			&passphrase_callback);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(a));

	if (archive_read_open_filename(a, cpio->filename,
					cpio->bytes_per_block))
		lafe_errc(1, archive_errno(a),
		    "%s", archive_error_string(a));
	for (;;) {
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			lafe_errc(1, archive_errno(a),
			    "%s", archive_error_string(a));
		}
		if (archive_match_path_excluded(cpio->matching, entry))
			continue;
		if (cpio->option_rename) {
			destpath = cpio_rename(archive_entry_pathname(entry));
			archive_entry_set_pathname(entry, destpath);
		} else
			destpath = archive_entry_pathname(entry);
		if (destpath == NULL)
			continue;
		if (cpio->verbose)
			fprintf(stderr, "%s\n", destpath);
		if (cpio->dot)
			fprintf(stderr, ".");
		if (cpio->uid_override >= 0)
			archive_entry_set_uid(entry, cpio->uid_override);
		if (cpio->gid_override >= 0)
			archive_entry_set_gid(entry, cpio->gid_override);
		r = archive_write_header(ext, entry);
		if (r != ARCHIVE_OK) {
			fprintf(stderr, "%s: %s\n",
			    archive_entry_pathname(entry),
			    archive_error_string(ext));
		} else if (!archive_entry_size_is_set(entry)
		    || archive_entry_size(entry) > 0) {
			r = extract_data(a, ext);
			if (r != ARCHIVE_OK)
				cpio->return_value = 1;
		}
	}
	r = archive_read_close(a);
	if (cpio->dot)
		fprintf(stderr, "\n");
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(a));
	r = archive_write_close(ext);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(ext));
	if (!cpio->quiet) {
		int64_t blocks = (archive_filter_bytes(a, 0) + 511)
			      / 512;
		fprintf(stderr, "%lu %s\n", (unsigned long)blocks,
		    blocks == 1 ? "block" : "blocks");
	}
	archive_read_free(a);
	archive_write_free(ext);
	exit(cpio->return_value);
}

/*
 * Exits if there's a fatal error.  Returns ARCHIVE_OK
 * if everything is kosher.
 */
static int
extract_data(struct archive *ar, struct archive *aw)
{
	int r;
	size_t size;
	const void *block;
	int64_t offset;

	for (;;) {
		r = archive_read_data_block(ar, &block, &size, &offset);
		if (r == ARCHIVE_EOF)
			return (ARCHIVE_OK);
		if (r != ARCHIVE_OK) {
			lafe_warnc(archive_errno(ar),
			    "%s", archive_error_string(ar));
			exit(1);
		}
		r = (int)archive_write_data_block(aw, block, size, offset);
		if (r != ARCHIVE_OK) {
			lafe_warnc(archive_errno(aw),
			    "%s", archive_error_string(aw));
			return (r);
		}
	}
}

static void
mode_list(struct cpio *cpio)
{
	struct archive *a;
	struct archive_entry *entry;
	int r;

	a = archive_read_new();
	if (a == NULL)
		lafe_errc(1, 0, "Couldn't allocate archive object");
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);
	if (cpio->passphrase != NULL)
		r = archive_read_add_passphrase(a, cpio->passphrase);
	else
		r = archive_read_set_passphrase_callback(a, cpio,
			&passphrase_callback);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(a));

	if (archive_read_open_filename(a, cpio->filename,
					cpio->bytes_per_block))
		lafe_errc(1, archive_errno(a),
		    "%s", archive_error_string(a));
	for (;;) {
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			lafe_errc(1, archive_errno(a),
			    "%s", archive_error_string(a));
		}
		if (archive_match_path_excluded(cpio->matching, entry))
			continue;
		if (cpio->verbose)
			list_item_verbose(cpio, entry);
		else
			fprintf(stdout, "%s\n", archive_entry_pathname(entry));
	}
	r = archive_read_close(a);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(a));
	if (!cpio->quiet) {
		int64_t blocks = (archive_filter_bytes(a, 0) + 511)
			      / 512;
		fprintf(stderr, "%lu %s\n", (unsigned long)blocks,
		    blocks == 1 ? "block" : "blocks");
	}
	archive_read_free(a);
	exit(0);
}

/*
 * Display information about the current file.
 *
 * The format here roughly duplicates the output of 'ls -l'.
 * This is based on SUSv2, where 'tar tv' is documented as
 * listing additional information in an "unspecified format,"
 * and 'pax -l' is documented as using the same format as 'ls -l'.
 */
static void
list_item_verbose(struct cpio *cpio, struct archive_entry *entry)
{
	char			 size[32];
	char			 date[32];
	char			 uids[16], gids[16];
	const char 		*uname, *gname;
	FILE			*out = stdout;
	const char		*fmt;
	time_t			 mtime;
	static time_t		 now;

	if (!now)
		time(&now);

	if (cpio->option_numeric_uid_gid) {
		/* Format numeric uid/gid for display. */
		strcpy(uids, cpio_i64toa(archive_entry_uid(entry)));
		uname = uids;
		strcpy(gids, cpio_i64toa(archive_entry_gid(entry)));
		gname = gids;
	} else {
		/* Use uname if it's present, else lookup name from uid. */
		uname = archive_entry_uname(entry);
		if (uname == NULL)
			uname = lookup_uname(cpio, (uid_t)archive_entry_uid(entry));
		/* Use gname if it's present, else lookup name from gid. */
		gname = archive_entry_gname(entry);
		if (gname == NULL)
			gname = lookup_gname(cpio, (uid_t)archive_entry_gid(entry));
	}

	/* Print device number or file size. */
	if (archive_entry_filetype(entry) == AE_IFCHR
	    || archive_entry_filetype(entry) == AE_IFBLK) {
		snprintf(size, sizeof(size), "%lu,%lu",
		    (unsigned long)archive_entry_rdevmajor(entry),
		    (unsigned long)archive_entry_rdevminor(entry));
	} else {
		strcpy(size, cpio_i64toa(archive_entry_size(entry)));
	}

	/* Format the time using 'ls -l' conventions. */
	mtime = archive_entry_mtime(entry);
#if defined(_WIN32) && !defined(__CYGWIN__)
	/* Windows' strftime function does not support %e format. */
	if (mtime - now > 365*86400/2
		|| mtime - now < -365*86400/2)
		fmt = cpio->day_first ? "%d %b  %Y" : "%b %d  %Y";
	else
		fmt = cpio->day_first ? "%d %b %H:%M" : "%b %d %H:%M";
#else
	if (mtime - now > 365*86400/2
		|| mtime - now < -365*86400/2)
		fmt = cpio->day_first ? "%e %b  %Y" : "%b %e  %Y";
	else
		fmt = cpio->day_first ? "%e %b %H:%M" : "%b %e %H:%M";
#endif
	strftime(date, sizeof(date), fmt, localtime(&mtime));

	fprintf(out, "%s%3d %-8s %-8s %8s %12s %s",
	    archive_entry_strmode(entry),
	    archive_entry_nlink(entry),
	    uname, gname, size, date,
	    archive_entry_pathname(entry));

	/* Extra information for links. */
	if (archive_entry_hardlink(entry)) /* Hard link */
		fprintf(out, " link to %s", archive_entry_hardlink(entry));
	else if (archive_entry_symlink(entry)) /* Symbolic link */
		fprintf(out, " -> %s", archive_entry_symlink(entry));
	fprintf(out, "\n");
}

static void
mode_pass(struct cpio *cpio, const char *destdir)
{
	struct lafe_line_reader *lr;
	const char *p;
	int r;
	size_t destdir_len;

	/* Ensure target dir has a trailing '/' to simplify path surgery. */
	destdir_len = strlen(destdir);
	cpio->destdir = malloc(destdir_len + 8);
	memcpy(cpio->destdir, destdir, destdir_len);
	if (destdir_len == 0 || destdir[destdir_len - 1] != '/')
		cpio->destdir[destdir_len++] = '/';
	cpio->destdir[destdir_len++] = '\0';

	cpio->archive = archive_write_disk_new();
	if (cpio->archive == NULL)
		lafe_errc(1, 0, "Failed to allocate archive object");
	r = archive_write_disk_set_options(cpio->archive, cpio->extract_flags);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(cpio->archive));
	cpio->linkresolver = archive_entry_linkresolver_new();
	archive_write_disk_set_standard_lookup(cpio->archive);

	cpio->archive_read_disk = archive_read_disk_new();
	if (cpio->archive_read_disk == NULL)
		lafe_errc(1, 0, "Failed to allocate archive object");
	if (cpio->option_follow_links)
		archive_read_disk_set_symlink_logical(cpio->archive_read_disk);
	else
		archive_read_disk_set_symlink_physical(cpio->archive_read_disk);
	archive_read_disk_set_standard_lookup(cpio->archive_read_disk);

	lr = lafe_line_reader("-", cpio->option_null);
	while ((p = lafe_line_reader_next(lr)) != NULL)
		file_to_archive(cpio, p);
	lafe_line_reader_free(lr);

	archive_entry_linkresolver_free(cpio->linkresolver);
	r = archive_write_close(cpio->archive);
	if (cpio->dot)
		fprintf(stderr, "\n");
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(cpio->archive));

	if (!cpio->quiet) {
		int64_t blocks =
			(archive_filter_bytes(cpio->archive, 0) + 511)
			/ 512;
		fprintf(stderr, "%lu %s\n", (unsigned long)blocks,
		    blocks == 1 ? "block" : "blocks");
	}

	archive_write_free(cpio->archive);
	free(cpio->pass_destpath);
}

/*
 * Prompt for a new name for this entry.  Returns a pointer to the
 * new name or NULL if the entry should not be copied.  This
 * implements the semantics defined in POSIX.1-1996, which specifies
 * that an input of '.' means the name should be unchanged.  GNU cpio
 * treats '.' as a literal new name.
 */
static const char *
cpio_rename(const char *name)
{
	static char buff[1024];
	FILE *t;
	char *p, *ret;
#if defined(_WIN32) && !defined(__CYGWIN__)
	FILE *to;

	t = fopen("CONIN$", "r");
	if (t == NULL)
		return (name);
	to = fopen("CONOUT$", "w");
	if (to == NULL) {
		fclose(t);
		return (name);
	}
	fprintf(to, "%s (Enter/./(new name))? ", name);
	fclose(to);
#else
	t = fopen("/dev/tty", "r+");
	if (t == NULL)
		return (name);
	fprintf(t, "%s (Enter/./(new name))? ", name);
	fflush(t);
#endif

	p = fgets(buff, sizeof(buff), t);
	fclose(t);
	if (p == NULL)
		/* End-of-file is a blank line. */
		return (NULL);

	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == '\n' || *p == '\0')
		/* Empty line. */
		return (NULL);
	if (*p == '.' && p[1] == '\n')
		/* Single period preserves original name. */
		return (name);
	ret = p;
	/* Trim the final newline. */
	while (*p != '\0' && *p != '\n')
		++p;
	/* Overwrite the final \n with a null character. */
	*p = '\0';
	return (ret);
}

static void
free_cache(struct name_cache *cache)
{
	size_t i;

	if (cache != NULL) {
		for (i = 0; i < cache->size; i++)
			free(cache->cache[i].name);
		free(cache);
	}
}

/*
 * Lookup uname/gname from uid/gid, return NULL if no match.
 */
static const char *
lookup_name(struct cpio *cpio, struct name_cache **name_cache_variable,
    int (*lookup_fn)(struct cpio *, const char **, id_t), id_t id)
{
	char asnum[16];
	struct name_cache	*cache;
	const char *name;
	int slot;


	if (*name_cache_variable == NULL) {
		*name_cache_variable = calloc(1, sizeof(struct name_cache));
		if (*name_cache_variable == NULL)
			lafe_errc(1, ENOMEM, "No more memory");
		(*name_cache_variable)->size = name_cache_size;
	}

	cache = *name_cache_variable;
	cache->probes++;

	slot = id % cache->size;
	if (cache->cache[slot].name != NULL) {
		if (cache->cache[slot].id == id) {
			cache->hits++;
			return (cache->cache[slot].name);
		}
		free(cache->cache[slot].name);
		cache->cache[slot].name = NULL;
	}

	if (lookup_fn(cpio, &name, id)) {
		/* If lookup failed, format it as a number. */
		snprintf(asnum, sizeof(asnum), "%u", (unsigned)id);
		name = asnum;
	}

	cache->cache[slot].name = strdup(name);
	if (cache->cache[slot].name != NULL) {
		cache->cache[slot].id = id;
		return (cache->cache[slot].name);
	}

	/*
	 * Conveniently, NULL marks an empty slot, so
	 * if the strdup() fails, we've just failed to
	 * cache it.  No recovery necessary.
	 */
	return (NULL);
}

static const char *
lookup_uname(struct cpio *cpio, uid_t uid)
{
	return (lookup_name(cpio, &cpio->uname_cache,
		    &lookup_uname_helper, (id_t)uid));
}

static int
lookup_uname_helper(struct cpio *cpio, const char **name, id_t id)
{
	struct passwd	*pwent;

	(void)cpio; /* UNUSED */

	errno = 0;
	pwent = getpwuid((uid_t)id);
	if (pwent == NULL) {
		if (errno && errno != ENOENT)
			lafe_warnc(errno, "getpwuid(%s) failed",
			    cpio_i64toa((int64_t)id));
		return 1;
	}

	*name = pwent->pw_name;
	return 0;
}

static const char *
lookup_gname(struct cpio *cpio, gid_t gid)
{
	return (lookup_name(cpio, &cpio->gname_cache,
		    &lookup_gname_helper, (id_t)gid));
}

static int
lookup_gname_helper(struct cpio *cpio, const char **name, id_t id)
{
	struct group	*grent;

	(void)cpio; /* UNUSED */

	errno = 0;
	grent = getgrgid((gid_t)id);
	if (grent == NULL) {
		if (errno && errno != ENOENT)
			lafe_warnc(errno, "getgrgid(%s) failed",
			    cpio_i64toa((int64_t)id));
		return 1;
	}

	*name = grent->gr_name;
	return 0;
}

/*
 * It would be nice to just use printf() for formatting large numbers,
 * but the compatibility problems are a big headache.  Hence the
 * following simple utility function.
 */
const char *
cpio_i64toa(int64_t n0)
{
	/* 2^64 =~ 1.8 * 10^19, so 20 decimal digits suffice.
	 * We also need 1 byte for '-' and 1 for '\0'.
	 */
	static char buff[22];
	int64_t n = n0 < 0 ? -n0 : n0;
	char *p = buff + sizeof(buff);

	*--p = '\0';
	do {
		*--p = '0' + (int)(n % 10);
		n /= 10;
	} while (n > 0);
	if (n0 < 0)
		*--p = '-';
	return p;
}

#define PPBUFF_SIZE 1024
static const char *
passphrase_callback(struct archive *a, void *_client_data)
{
	struct cpio *cpio = (struct cpio *)_client_data;
	(void)a; /* UNUSED */

	if (cpio->ppbuff == NULL) {
		cpio->ppbuff = malloc(PPBUFF_SIZE);
		if (cpio->ppbuff == NULL)
			lafe_errc(1, errno, "Out of memory");
	}
	return lafe_readpassphrase("Enter passphrase:",
		cpio->ppbuff, PPBUFF_SIZE);
}

static void
passphrase_free(char *ppbuff)
{
	if (ppbuff != NULL) {
		memset(ppbuff, 0, PPBUFF_SIZE);
		free(ppbuff);
	}
}
