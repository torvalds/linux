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
 * Record the complete unique set of opened file names.
 * Primarily used to cache source position file names.
 */
#define MAX_N_FILE_NAMES	(100)

const char *file_names[MAX_N_FILE_NAMES];
static int n_file_names = 0;

/*
 * Like yylineno, this is the current open file pos.
 */

int srcpos_filenum = -1;



FILE *dtc_open_file(const char *fname)
{
	FILE *f;

	if (lookup_file_name(fname, 1) < 0)
		die("Too many files opened\n");

	if (streq(fname, "-"))
		f = stdin;
	else
		f = fopen(fname, "r");

	if (! f)
		die("Couldn't open \"%s\": %s\n", fname, strerror(errno));

	return f;
}



/*
 * Locate and optionally add filename fname in the file_names[] array.
 *
 * If the filename is currently not in the array and the boolean
 * add_it is non-zero, an attempt to add the filename will be made.
 *
 * Returns;
 *    Index [0..MAX_N_FILE_NAMES) where the filename is kept
 *    -1 if the name can not be recorded
 */

int lookup_file_name(const char *fname, int add_it)
{
	int i;

	for (i = 0; i < n_file_names; i++) {
		if (strcmp(file_names[i], fname) == 0)
			return i;
	}

	if (add_it) {
		if (n_file_names < MAX_N_FILE_NAMES) {
			file_names[n_file_names] = strdup(fname);
			return n_file_names++;
		}
	}

	return -1;
}


const char *srcpos_filename_for_num(int filenum)
{
	if (0 <= filenum && filenum < n_file_names) {
		return file_names[filenum];
	}

	return 0;
}


const char *srcpos_get_filename(void)
{
	return srcpos_filename_for_num(srcpos_filenum);
}
