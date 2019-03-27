/*-
 * Copyright (c) 2001 Chris D. Faulhaber
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/acl.h>
#include <sys/queue.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "setfacl.h"

/* file operations */
#define	OP_MERGE_ACL		0x00	/* merge acl's (-mM) */
#define	OP_REMOVE_DEF		0x01	/* remove default acl's (-k) */
#define	OP_REMOVE_EXT		0x02	/* remove extended acl's (-b) */
#define	OP_REMOVE_ACL		0x03	/* remove acl's (-xX) */
#define	OP_REMOVE_BY_NUMBER	0x04	/* remove acl's (-xX) by acl entry number */
#define	OP_ADD_ACL		0x05	/* add acls entries at a given position */

/* TAILQ entry for acl operations */
struct sf_entry {
	uint	op;
	acl_t	acl;
	uint	entry_number;
	TAILQ_ENTRY(sf_entry) next;
};
static TAILQ_HEAD(, sf_entry) entrylist;

bool have_mask;
bool have_stdin;
bool n_flag;
static bool h_flag;
static bool H_flag;
static bool L_flag;
static bool R_flag;
static bool need_mask;
static acl_type_t acl_type = ACL_TYPE_ACCESS;

static int	handle_file(FTS *ftsp, FTSENT *file);
static acl_t	clear_inheritance_flags(acl_t acl);
static char	**stdin_files(void);
static void	usage(void);

static void
usage(void)
{

	fprintf(stderr, "usage: setfacl [-R [-H | -L | -P]] [-bdhkn] "
	    "[-a position entries] [-m entries] [-M file] "
	    "[-x entries] [-X file] [file ...]\n");
	exit(1);
}

static char **
stdin_files(void)
{
	char **files_list;
	char filename[PATH_MAX];
	size_t fl_count, i;

	if (have_stdin)
		err(1, "cannot have more than one stdin");

	i = 0;
	have_stdin = true;
	bzero(&filename, sizeof(filename));
	/* Start with an array size sufficient for basic cases. */
	fl_count = 1024;
	files_list = zmalloc(fl_count * sizeof(char *));
	while (fgets(filename, (int)sizeof(filename), stdin)) {
		/* remove the \n */
		filename[strlen(filename) - 1] = '\0';
		files_list[i] = strdup(filename);
		if (files_list[i] == NULL)
			err(1, "strdup() failed");
		/* Grow array if necessary. */
		if (++i == fl_count) {
			fl_count <<= 1;
			if (fl_count > SIZE_MAX / sizeof(char *))
				errx(1, "Too many input files");
			files_list = zrealloc(files_list,
					fl_count * sizeof(char *));
		}
	}

	/* fts_open() requires the last array element to be NULL. */
	files_list[i] = NULL;

	return (files_list);
}

/*
 * Remove any inheritance flags from NFSv4 ACLs when running in recursive
 * mode.  This is to avoid files being assigned identical ACLs to their
 * parent directory while also being set to inherit them.
 *
 * The acl argument is assumed to be valid.
 */
static acl_t
clear_inheritance_flags(acl_t acl)
{
	acl_t nacl;
	acl_entry_t acl_entry;
	acl_flagset_t acl_flagset;
	int acl_brand, entry_id;

	(void)acl_get_brand_np(acl, &acl_brand);
	if (acl_brand != ACL_BRAND_NFS4)
		return (acl);

	nacl = acl_dup(acl);
	if (nacl == NULL) {
		warn("acl_dup() failed");
		return (acl);
	}

	entry_id = ACL_FIRST_ENTRY;
	while (acl_get_entry(nacl, entry_id, &acl_entry) == 1) {
		entry_id = ACL_NEXT_ENTRY;
		if (acl_get_flagset_np(acl_entry, &acl_flagset) != 0) {
			warn("acl_get_flagset_np() failed");
			continue;
		}
		if (acl_get_flag_np(acl_flagset, ACL_ENTRY_INHERIT_ONLY) == 1) {
			if (acl_delete_entry(nacl, acl_entry) != 0)
				warn("acl_delete_entry() failed");
			continue;
		}
		if (acl_delete_flag_np(acl_flagset,
		    ACL_ENTRY_FILE_INHERIT |
		    ACL_ENTRY_DIRECTORY_INHERIT |
		    ACL_ENTRY_NO_PROPAGATE_INHERIT) != 0)
			warn("acl_delete_flag_np() failed");
	}

	return (nacl);
}

static int
handle_file(FTS *ftsp, FTSENT *file)
{
	acl_t acl, nacl;
	acl_entry_t unused_entry;
	int local_error, ret;
	struct sf_entry *entry;
	bool follow_symlink;

	local_error = 0;
	switch (file->fts_info) {
	case FTS_D:
		/* Do not recurse if -R not specified. */
		if (!R_flag)
			fts_set(ftsp, file, FTS_SKIP);
		break;
	case FTS_DP:
		/* Skip the second visit to a directory. */
		return (0);
	case FTS_DNR:
	case FTS_ERR:
		warnx("%s: %s", file->fts_path, strerror(file->fts_errno));
		return (0);
	default:
		break;
	}

	if (acl_type == ACL_TYPE_DEFAULT && file->fts_info != FTS_D) {
		warnx("%s: default ACL may only be set on a directory",
		    file->fts_path);
		return (1);
	}

	follow_symlink = (!R_flag && !h_flag) || (R_flag && L_flag) ||
	    (R_flag && H_flag && file->fts_level == FTS_ROOTLEVEL);

	if (follow_symlink)
		ret = pathconf(file->fts_accpath, _PC_ACL_NFS4);
	else
		ret = lpathconf(file->fts_accpath, _PC_ACL_NFS4);
	if (ret > 0) {
		if (acl_type == ACL_TYPE_DEFAULT) {
			warnx("%s: there are no default entries in NFSv4 ACLs",
			    file->fts_path);
			return (1);
		}
		acl_type = ACL_TYPE_NFS4;
	} else if (ret == 0) {
		if (acl_type == ACL_TYPE_NFS4)
			acl_type = ACL_TYPE_ACCESS;
	} else if (ret < 0 && errno != EINVAL && errno != ENOENT) {
		warn("%s: pathconf(_PC_ACL_NFS4) failed",
		    file->fts_path);
	}

	if (follow_symlink)
		acl = acl_get_file(file->fts_accpath, acl_type);
	else
		acl = acl_get_link_np(file->fts_accpath, acl_type);
	if (acl == NULL) {
		if (follow_symlink)
			warn("%s: acl_get_file() failed", file->fts_path);
		else
			warn("%s: acl_get_link_np() failed", file->fts_path);
		return (1);
	}

	/* Cycle through each option. */
	TAILQ_FOREACH(entry, &entrylist, next) {
		nacl = entry->acl;
		switch (entry->op) {
		case OP_ADD_ACL:
			if (R_flag && file->fts_info != FTS_D &&
			    acl_type == ACL_TYPE_NFS4)
				nacl = clear_inheritance_flags(nacl);
			local_error += add_acl(nacl, entry->entry_number, &acl,
			    file->fts_path);
			break;
		case OP_MERGE_ACL:
			if (R_flag && file->fts_info != FTS_D &&
			    acl_type == ACL_TYPE_NFS4)
				nacl = clear_inheritance_flags(nacl);
			local_error += merge_acl(nacl, &acl, file->fts_path);
			need_mask = true;
			break;
		case OP_REMOVE_EXT:
			/*
			 * Don't try to call remove_ext() for empty
			 * default ACL.
			 */
			if (acl_type == ACL_TYPE_DEFAULT &&
			    acl_get_entry(acl, ACL_FIRST_ENTRY,
			    &unused_entry) == 0) {
				local_error += remove_default(&acl,
				    file->fts_path);
				break;
			}
			remove_ext(&acl, file->fts_path);
			need_mask = false;
			break;
		case OP_REMOVE_DEF:
			if (acl_type == ACL_TYPE_NFS4) {
				warnx("%s: there are no default entries in "
				    "NFSv4 ACLs; cannot remove",
				    file->fts_path);
				local_error++;
				break;
			}
			if (acl_delete_def_file(file->fts_accpath) == -1) {
				warn("%s: acl_delete_def_file() failed",
				    file->fts_path);
				local_error++;
			}
			if (acl_type == ACL_TYPE_DEFAULT)
				local_error += remove_default(&acl,
				    file->fts_path);
			need_mask = false;
			break;
		case OP_REMOVE_ACL:
			local_error += remove_acl(nacl, &acl, file->fts_path);
			need_mask = true;
			break;
		case OP_REMOVE_BY_NUMBER:
			local_error += remove_by_number(entry->entry_number,
			    &acl, file->fts_path);
			need_mask = true;
			break;
		}

		if (nacl != entry->acl) {
			acl_free(nacl);
			nacl = NULL;
		}
		if (local_error)
			break;
	}

	ret = 0;

	/*
	 * Don't try to set an empty default ACL; it will always fail.
	 * Use acl_delete_def_file(3) instead.
	 */
	if (acl_type == ACL_TYPE_DEFAULT &&
	    acl_get_entry(acl, ACL_FIRST_ENTRY, &unused_entry) == 0) {
		if (acl_delete_def_file(file->fts_accpath) == -1) {
			warn("%s: acl_delete_def_file() failed",
			    file->fts_path);
			ret = 1;
		}
		goto out;
	}

	/* Don't bother setting the ACL if something is broken. */
	if (local_error) {
		ret = 1;
	} else if (acl_type != ACL_TYPE_NFS4 && need_mask &&
	    set_acl_mask(&acl, file->fts_path) == -1) {
		warnx("%s: failed to set ACL mask", file->fts_path);
		ret = 1;
	} else if (follow_symlink) {
		if (acl_set_file(file->fts_accpath, acl_type, acl) == -1) {
			warn("%s: acl_set_file() failed", file->fts_path);
			ret = 1;
		}
	} else {
		if (acl_set_link_np(file->fts_accpath, acl_type, acl) == -1) {
			warn("%s: acl_set_link_np() failed", file->fts_path);
			ret = 1;
		}
	}

out:
	acl_free(acl);
	return (ret);
}

int
main(int argc, char *argv[])
{
	int carried_error, ch, entry_number, fts_options;
	FTS *ftsp;
	FTSENT *file;
	char **files_list;
	struct sf_entry *entry;
	char *end;

	acl_type = ACL_TYPE_ACCESS;
	carried_error = fts_options = 0;
	have_mask = have_stdin = n_flag = false;

	TAILQ_INIT(&entrylist);

	while ((ch = getopt(argc, argv, "HLM:PRX:a:bdhkm:nx:")) != -1)
		switch(ch) {
		case 'H':
			H_flag = true;
			L_flag = false;
			break;
		case 'L':
			L_flag = true;
			H_flag = false;
			break;
		case 'M':
			entry = zmalloc(sizeof(struct sf_entry));
			entry->acl = get_acl_from_file(optarg);
			if (entry->acl == NULL)
				err(1, "%s: get_acl_from_file() failed",
				    optarg);
			entry->op = OP_MERGE_ACL;
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		case 'P':
			H_flag = L_flag = false;
			break;
		case 'R':
			R_flag = true;
			break;
		case 'X':
			entry = zmalloc(sizeof(struct sf_entry));
			entry->acl = get_acl_from_file(optarg);
			entry->op = OP_REMOVE_ACL;
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		case 'a':
			entry = zmalloc(sizeof(struct sf_entry));

			entry_number = strtol(optarg, &end, 10);
			if (end - optarg != (int)strlen(optarg))
				errx(1, "%s: invalid entry number", optarg);
			if (entry_number < 0)
				errx(1,
				    "%s: entry number cannot be less than zero",
				    optarg);
			entry->entry_number = entry_number;

			if (argv[optind] == NULL)
				errx(1, "missing ACL");
			entry->acl = acl_from_text(argv[optind]);
			if (entry->acl == NULL)
				err(1, "%s", argv[optind]);
			optind++;
			entry->op = OP_ADD_ACL;
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		case 'b':
			entry = zmalloc(sizeof(struct sf_entry));
			entry->op = OP_REMOVE_EXT;
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		case 'd':
			acl_type = ACL_TYPE_DEFAULT;
			break;
		case 'h':
			h_flag = 1;
			break;
		case 'k':
			entry = zmalloc(sizeof(struct sf_entry));
			entry->op = OP_REMOVE_DEF;
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		case 'm':
			entry = zmalloc(sizeof(struct sf_entry));
			entry->acl = acl_from_text(optarg);
			if (entry->acl == NULL)
				err(1, "%s", optarg);
			entry->op = OP_MERGE_ACL;
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		case 'n':
			n_flag = true;
			break;
		case 'x':
			entry = zmalloc(sizeof(struct sf_entry));
			entry_number = strtol(optarg, &end, 10);
			if (end - optarg == (int)strlen(optarg)) {
				if (entry_number < 0)
					errx(1,
					    "%s: entry number cannot be less than zero",
					    optarg);
				entry->entry_number = entry_number;
				entry->op = OP_REMOVE_BY_NUMBER;
			} else {
				entry->acl = acl_from_text(optarg);
				if (entry->acl == NULL)
					err(1, "%s", optarg);
				entry->op = OP_REMOVE_ACL;
			}
			TAILQ_INSERT_TAIL(&entrylist, entry, next);
			break;
		default:
			usage();
			break;
		}
	argc -= optind;
	argv += optind;

	if (!n_flag && TAILQ_EMPTY(&entrylist))
		usage();

	/* Take list of files from stdin. */
	if (argc == 0 || strcmp(argv[0], "-") == 0) {
		files_list = stdin_files();
	} else
		files_list = argv;

	if (R_flag) {
		if (h_flag)
			errx(1, "the -R and -h options may not be "
			    "specified together.");
		if (L_flag) {
			fts_options = FTS_LOGICAL;
		} else {
			fts_options = FTS_PHYSICAL;

			if (H_flag) {
				fts_options |= FTS_COMFOLLOW;
			}
		}
	} else if (h_flag) {
		fts_options = FTS_PHYSICAL;
	} else {
		fts_options = FTS_LOGICAL;
	}

	/* Open all files. */
	if ((ftsp = fts_open(files_list, fts_options | FTS_NOSTAT, 0)) == NULL)
		err(1, "fts_open");
	while ((file = fts_read(ftsp)) != NULL)
		carried_error += handle_file(ftsp, file);

	return (carried_error);
}
