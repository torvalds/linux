/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
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

#ifdef HAVE_IO_H
#include <io.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
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
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "bsdtar.h"
#include "err.h"

struct progress_data {
	struct bsdtar *bsdtar;
	struct archive *archive;
	struct archive_entry *entry;
};

static void	read_archive(struct bsdtar *bsdtar, char mode, struct archive *);
static int unmatched_inclusions_warn(struct archive *matching, const char *);


void
tar_mode_t(struct bsdtar *bsdtar)
{
	read_archive(bsdtar, 't', NULL);
	if (unmatched_inclusions_warn(bsdtar->matching,
	    "Not found in archive") != 0)
		bsdtar->return_value = 1;
}

void
tar_mode_x(struct bsdtar *bsdtar)
{
	struct archive *writer;

	writer = archive_write_disk_new();
	if (writer == NULL)
		lafe_errc(1, ENOMEM, "Cannot allocate disk writer object");
	if ((bsdtar->flags & OPTFLAG_NUMERIC_OWNER) == 0)
		archive_write_disk_set_standard_lookup(writer);
	archive_write_disk_set_options(writer, bsdtar->extract_flags);

	read_archive(bsdtar, 'x', writer);

	if (unmatched_inclusions_warn(bsdtar->matching,
	    "Not found in archive") != 0)
		bsdtar->return_value = 1;
	archive_write_free(writer);
}

static void
progress_func(void *cookie)
{
	struct progress_data *progress_data = (struct progress_data *)cookie;
	struct bsdtar *bsdtar = progress_data->bsdtar;
	struct archive *a = progress_data->archive;
	struct archive_entry *entry = progress_data->entry;
	uint64_t comp, uncomp;
	int compression;

	if (!need_report())
		return;

	if (bsdtar->verbose)
		fprintf(stderr, "\n");
	if (a != NULL) {
		comp = archive_filter_bytes(a, -1);
		uncomp = archive_filter_bytes(a, 0);
		if (comp > uncomp)
			compression = 0;
		else
			compression = (int)((uncomp - comp) * 100 / uncomp);
		fprintf(stderr,
		    "In: %s bytes, compression %d%%;",
		    tar_i64toa(comp), compression);
		fprintf(stderr, "  Out: %d files, %s bytes\n",
		    archive_file_count(a), tar_i64toa(uncomp));
	}
	if (entry != NULL) {
		safe_fprintf(stderr, "Current: %s",
		    archive_entry_pathname(entry));
		fprintf(stderr, " (%s bytes)\n",
		    tar_i64toa(archive_entry_size(entry)));
	}
}

/*
 * Handle 'x' and 't' modes.
 */
static void
read_archive(struct bsdtar *bsdtar, char mode, struct archive *writer)
{
	struct progress_data	progress_data;
	FILE			 *out;
	struct archive		 *a;
	struct archive_entry	 *entry;
	const char		 *reader_options;
	int			  r;

	while (*bsdtar->argv) {
		if (archive_match_include_pattern(bsdtar->matching,
		    *bsdtar->argv) != ARCHIVE_OK)
			lafe_errc(1, 0, "Error inclusion pattern: %s",
			    archive_error_string(bsdtar->matching));
		bsdtar->argv++;
	}

	if (bsdtar->names_from_file != NULL)
		if (archive_match_include_pattern_from_file(
		    bsdtar->matching, bsdtar->names_from_file,
		    (bsdtar->flags & OPTFLAG_NULL)) != ARCHIVE_OK)
			lafe_errc(1, 0, "Error inclusion pattern: %s",
			    archive_error_string(bsdtar->matching));

	a = archive_read_new();
	if (cset_read_support_filter_program(bsdtar->cset, a) == 0)
		archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	reader_options = getenv(ENV_READER_OPTIONS);
	if (reader_options != NULL) {
		size_t module_len = sizeof(IGNORE_WRONG_MODULE_NAME) - 1;
		size_t opt_len = strlen(reader_options) + 1;
		char *p;
		/* Set default read options. */
		if ((p = malloc(module_len + opt_len)) == NULL)
			lafe_errc(1, errno, "Out of memory");
		/* Prepend magic code to ignore options for
		 * a format or  modules which are not added to
		 *  the archive read object. */
		memcpy(p, IGNORE_WRONG_MODULE_NAME, module_len);
		memcpy(p + module_len, reader_options, opt_len);
		r = archive_read_set_options(a, p);
		free(p);
		if (r == ARCHIVE_FATAL)
			lafe_errc(1, 0, "%s", archive_error_string(a));
		else
			archive_clear_error(a);
	}
	if (ARCHIVE_OK != archive_read_set_options(a, bsdtar->option_options))
		lafe_errc(1, 0, "%s", archive_error_string(a));
	if (bsdtar->flags & OPTFLAG_IGNORE_ZEROS)
		if (archive_read_set_options(a,
		    "read_concatenated_archives") != ARCHIVE_OK)
			lafe_errc(1, 0, "%s", archive_error_string(a));
	if (bsdtar->passphrase != NULL)
		r = archive_read_add_passphrase(a, bsdtar->passphrase);
	else
		r = archive_read_set_passphrase_callback(a, bsdtar,
			&passphrase_callback);
	if (r != ARCHIVE_OK)
		lafe_errc(1, 0, "%s", archive_error_string(a));
	if (archive_read_open_filename(a, bsdtar->filename,
					bsdtar->bytes_per_block))
		lafe_errc(1, 0, "Error opening archive: %s",
		    archive_error_string(a));

	do_chdir(bsdtar);

	if (mode == 'x') {
		/* Set an extract callback so that we can handle SIGINFO. */
		progress_data.bsdtar = bsdtar;
		progress_data.archive = a;
		archive_read_extract_set_progress_callback(a, progress_func,
		    &progress_data);
	}

	if (mode == 'x' && (bsdtar->flags & OPTFLAG_CHROOT)) {
#if HAVE_CHROOT
		if (chroot(".") != 0)
			lafe_errc(1, errno, "Can't chroot to \".\"");
#else
		lafe_errc(1, 0,
		    "chroot isn't supported on this platform");
#endif
	}

#if defined(_WIN32) && !defined(__CYGWIN__)
	if (mode == 'x' && (bsdtar->flags & OPTFLAG_STDOUT)) {
		_setmode(1, _O_BINARY);
	}
#endif

	for (;;) {
		/* Support --fast-read option */
		const char *p;
		if ((bsdtar->flags & OPTFLAG_FAST_READ) &&
		    archive_match_path_unmatched_inclusions(bsdtar->matching) == 0)
			break;

		r = archive_read_next_header(a, &entry);
		progress_data.entry = entry;
		if (r == ARCHIVE_EOF)
			break;
		if (r < ARCHIVE_OK)
			lafe_warnc(0, "%s", archive_error_string(a));
		if (r <= ARCHIVE_WARN)
			bsdtar->return_value = 1;
		if (r == ARCHIVE_RETRY) {
			/* Retryable error: try again */
			lafe_warnc(0, "Retrying...");
			continue;
		}
		if (r == ARCHIVE_FATAL)
			break;
		p = archive_entry_pathname(entry);
		if (p == NULL || p[0] == '\0') {
			lafe_warnc(0, "Archive entry has empty or unreadable filename ... skipping.");
			bsdtar->return_value = 1;
			continue;
		}

		if (bsdtar->uid >= 0) {
			archive_entry_set_uid(entry, bsdtar->uid);
			archive_entry_set_uname(entry, NULL);
		}
		if (bsdtar->gid >= 0) {
			archive_entry_set_gid(entry, bsdtar->gid);
			archive_entry_set_gname(entry, NULL);
		}
		if (bsdtar->uname)
			archive_entry_set_uname(entry, bsdtar->uname);
		if (bsdtar->gname)
			archive_entry_set_gname(entry, bsdtar->gname);

		/*
		 * Note that pattern exclusions are checked before
		 * pathname rewrites are handled.  This gives more
		 * control over exclusions, since rewrites always lose
		 * information.  (For example, consider a rewrite
		 * s/foo[0-9]/foo/.  If we check exclusions after the
		 * rewrite, there would be no way to exclude foo1/bar
		 * while allowing foo2/bar.)
		 */
		if (archive_match_excluded(bsdtar->matching, entry))
			continue; /* Excluded by a pattern test. */

		if (mode == 't') {
			/* Perversely, gtar uses -O to mean "send to stderr"
			 * when used with -t. */
			out = (bsdtar->flags & OPTFLAG_STDOUT) ?
			    stderr : stdout;

			/*
			 * TODO: Provide some reasonable way to
			 * preview rewrites.  gtar always displays
			 * the unedited path in -t output, which means
			 * you cannot easily preview rewrites.
			 */
			if (bsdtar->verbose < 2)
				safe_fprintf(out, "%s",
				    archive_entry_pathname(entry));
			else
				list_item_verbose(bsdtar, out, entry);
			fflush(out);
			r = archive_read_data_skip(a);
			if (r == ARCHIVE_WARN) {
				fprintf(out, "\n");
				lafe_warnc(0, "%s",
				    archive_error_string(a));
			}
			if (r == ARCHIVE_RETRY) {
				fprintf(out, "\n");
				lafe_warnc(0, "%s",
				    archive_error_string(a));
			}
			if (r == ARCHIVE_FATAL) {
				fprintf(out, "\n");
				lafe_warnc(0, "%s",
				    archive_error_string(a));
				bsdtar->return_value = 1;
				break;
			}
			fprintf(out, "\n");
		} else {
			/* Note: some rewrite failures prevent extraction. */
			if (edit_pathname(bsdtar, entry))
				continue; /* Excluded by a rewrite failure. */

			if ((bsdtar->flags & OPTFLAG_INTERACTIVE) &&
			    !yes("extract '%s'", archive_entry_pathname(entry)))
				continue;

			if (bsdtar->verbose > 1) {
				/* GNU tar uses -tv format with -xvv */
				safe_fprintf(stderr, "x ");
				list_item_verbose(bsdtar, stderr, entry);
				fflush(stderr);
			} else if (bsdtar->verbose > 0) {
				/* Format follows SUSv2, including the
				 * deferred '\n'. */
				safe_fprintf(stderr, "x %s",
				    archive_entry_pathname(entry));
				fflush(stderr);
			}

			/* TODO siginfo_printinfo(bsdtar, 0); */

			if (bsdtar->flags & OPTFLAG_STDOUT)
				r = archive_read_data_into_fd(a, 1);
			else
				r = archive_read_extract2(a, entry, writer);
			if (r != ARCHIVE_OK) {
				if (!bsdtar->verbose)
					safe_fprintf(stderr, "%s",
					    archive_entry_pathname(entry));
				safe_fprintf(stderr, ": %s",
				    archive_error_string(a));
				if (!bsdtar->verbose)
					fprintf(stderr, "\n");
				bsdtar->return_value = 1;
			}
			if (bsdtar->verbose)
				fprintf(stderr, "\n");
			if (r == ARCHIVE_FATAL)
				break;
		}
	}


	r = archive_read_close(a);
	if (r != ARCHIVE_OK)
		lafe_warnc(0, "%s", archive_error_string(a));
	if (r <= ARCHIVE_WARN)
		bsdtar->return_value = 1;

	if (bsdtar->verbose > 2)
		fprintf(stdout, "Archive Format: %s,  Compression: %s\n",
		    archive_format_name(a), archive_filter_name(a, 0));

	archive_read_free(a);
}


static int
unmatched_inclusions_warn(struct archive *matching, const char *msg)
{
	const char *p;
	int r;

	if (matching == NULL)
		return (0);

	while ((r = archive_match_path_unmatched_inclusions_next(
	    matching, &p)) == ARCHIVE_OK)
		lafe_warnc(0, "%s: %s", p, msg);
	if (r == ARCHIVE_FATAL)
		lafe_errc(1, errno, "Out of memory");

	return (archive_match_path_unmatched_inclusions(matching));
}
