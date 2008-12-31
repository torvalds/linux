/*
 * Copyright 2007 Jon Loeliger, Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

#include "dtc.h"
#include "srcpos.h"

/*
 * Like yylineno, this is the current open file pos.
 */

struct dtc_file *srcpos_file;

static int dtc_open_one(struct dtc_file *file,
                        const char *search,
                        const char *fname)
{
	char *fullname;

	if (search) {
		fullname = xmalloc(strlen(search) + strlen(fname) + 2);

		strcpy(fullname, search);
		strcat(fullname, "/");
		strcat(fullname, fname);
	} else {
		fullname = strdup(fname);
	}

	file->file = fopen(fullname, "r");
	if (!file->file) {
		free(fullname);
		return 0;
	}

	file->name = fullname;
	return 1;
}


struct dtc_file *dtc_open_file(const char *fname,
                               const struct search_path *search)
{
	static const struct search_path default_search = { NULL, NULL, NULL };

	struct dtc_file *file;
	const char *slash;

	file = xmalloc(sizeof(struct dtc_file));

	slash = strrchr(fname, '/');
	if (slash) {
		char *dir = xmalloc(slash - fname + 1);

		memcpy(dir, fname, slash - fname);
		dir[slash - fname] = 0;
		file->dir = dir;
	} else {
		file->dir = NULL;
	}

	if (streq(fname, "-")) {
		file->name = "stdin";
		file->file = stdin;
		return file;
	}

	if (fname[0] == '/') {
		file->file = fopen(fname, "r");
		if (!file->file)
			goto fail;

		file->name = strdup(fname);
		return file;
	}

	if (!search)
		search = &default_search;

	while (search) {
		if (dtc_open_one(file, search->dir, fname))
			return file;

		if (errno != ENOENT)
			goto fail;

		search = search->next;
	}

fail:
	die("Couldn't open \"%s\": %s\n", fname, strerror(errno));
}

void dtc_close_file(struct dtc_file *file)
{
	if (fclose(file->file))
		die("Error closing \"%s\": %s\n", file->name, strerror(errno));

	free(file->dir);
	free(file);
}
