/*
 *  $Id: columns.c,v 1.10 2011/10/20 20:53:55 tom Exp $
 *
 *  columns.c -- implements column-alignment
 *
 *  Copyright 2008-2010,2011	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to
 *	Free Software Foundation, Inc.
 *	51 Franklin St., Fifth Floor
 *	Boston, MA 02110, USA.
 */

#include <dialog.h>

#define each(row, data) \
 		row = 0, data = target; \
 		row < num_rows; \
		++row, data = next_row(data, per_row)

static char *
column_separator(void)
{
    char *result = 0;

    if ((result = dialog_vars.column_separator) != 0) {
	if (*result == '\0')
	    result = 0;
    }
    return result;
}

static char **
next_row(char **target, int per_row)
{
    char *result = (char *) target;
    result += per_row;
    return (char **) (void *) result;
}

static char *
next_col(char *source, unsigned offset)
{
    char *mark = column_separator();
    char *result = source + offset;
    if (offset)
	result += strlen(mark);
    return strstr(result, mark);
}

/*
 * Parse the source string, storing the offsets and widths of each column in
 * the corresponding arrays.  Return the number of columns.
 */
static unsigned
split_row(char *source, unsigned *offsets, unsigned *widths)
{
    int mark = (int) strlen(column_separator());
    char *next = 0;
    unsigned result = 0;
    unsigned offset = 0;

    do {
	if (result) {
	    offset = (unsigned) (mark + next - source);
	    widths[result - 1] = offset - offsets[result - 1] - (unsigned) mark;
	}
	offsets[result] = offset;
	++result;
    } while ((next = next_col(source, offset)) != 0);

    offset = (unsigned) strlen(source);
    widths[result - 1] = offset - offsets[result - 1];

    return result;
}

/*
 * The caller passes a pointer to a struct or array containing pointers
 * to strings that we may want to copy and reformat according to the column
 * separator.
 */
void
dlg_align_columns(char **target, int per_row, int num_rows)
{
    int row;

    if (column_separator()) {
	char **value;
	unsigned numcols = 1;
	size_t maxcols = 0;
	unsigned *widths;
	unsigned *offsets;
	unsigned *maxwidth;
	unsigned realwidth;
	unsigned n;

	/* first allocate arrays for workspace */
	for (each(row, value)) {
	    size_t len = strlen(*value);
	    if (maxcols < len)
		maxcols = len;
	}
	++maxcols;
	widths = dlg_calloc(unsigned, maxcols);
	offsets = dlg_calloc(unsigned, maxcols);
	maxwidth = dlg_calloc(unsigned, maxcols);

	assert_ptr(widths, "dlg_align_columns");
	assert_ptr(offsets, "dlg_align_columns");
	assert_ptr(maxwidth, "dlg_align_columns");

	/* now, determine the number of columns and the column-widths */
	for (each(row, value)) {
	    unsigned cols = split_row(*value, offsets, widths);
	    if (numcols < cols)
		numcols = cols;
	    for (n = 0; n < cols; ++n) {
		if (maxwidth[n] < widths[n])
		    maxwidth[n] = widths[n];
	    }
	}
	realwidth = numcols - 1;
	for (n = 0; n < numcols; ++n) {
	    realwidth += maxwidth[n];
	}

	/* finally, construct reformatted strings */
	for (each(row, value)) {
	    unsigned cols = split_row(*value, offsets, widths);
	    unsigned offset = 0;
	    char *text = dlg_malloc(char, realwidth + 1);

	    assert_ptr(text, "dlg_align_columns");

	    memset(text, ' ', (size_t) realwidth);
	    for (n = 0; n < cols; ++n) {
		memcpy(text + offset, *value + offsets[n], (size_t) widths[n]);
		offset += maxwidth[n] + 1;
	    }
	    text[realwidth] = 0;
	    *value = text;
	}

	free(widths);
	free(offsets);
	free(maxwidth);
    }
}

/*
 * Free temporary storage used while making column-aligned data.
 */
void
dlg_free_columns(char **target, int per_row, int num_rows)
{
    int row;
    char **value;

    if (column_separator()) {
	for (each(row, value)) {
	    free(*value);
	}
    }
}
