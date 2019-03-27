/*
 * Copyright (c) 2000, 2002, 2004 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include "roken.h"
#include "rtbl.h"

struct column_entry {
    char *data;
};

struct column_data {
    char *header;
    char *prefix;
    int width;
    unsigned flags;
    size_t num_rows;
    struct column_entry *rows;
    unsigned int column_id;
    char *suffix;
};

struct rtbl_data {
    char *column_prefix;
    size_t num_columns;
    struct column_data **columns;
    unsigned int flags;
    char *column_separator;
};

ROKEN_LIB_FUNCTION rtbl_t ROKEN_LIB_CALL
rtbl_create (void)
{
    return calloc (1, sizeof (struct rtbl_data));
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rtbl_set_flags (rtbl_t table, unsigned int flags)
{
    table->flags = flags;
}

ROKEN_LIB_FUNCTION unsigned int ROKEN_LIB_CALL
rtbl_get_flags (rtbl_t table)
{
    return table->flags;
}

static struct column_data *
rtbl_get_column_by_id (rtbl_t table, unsigned int id)
{
    size_t i;
    for(i = 0; i < table->num_columns; i++)
	if(table->columns[i]->column_id == id)
	    return table->columns[i];
    return NULL;
}

static struct column_data *
rtbl_get_column (rtbl_t table, const char *column)
{
    size_t i;
    for(i = 0; i < table->num_columns; i++)
	if(strcmp(table->columns[i]->header, column) == 0)
	    return table->columns[i];
    return NULL;
}

ROKEN_LIB_FUNCTION void ROKEN_LIB_CALL
rtbl_destroy (rtbl_t table)
{
    size_t i, j;

    for (i = 0; i < table->num_columns; i++) {
	struct column_data *c = table->columns[i];

	for (j = 0; j < c->num_rows; j++)
	    free (c->rows[j].data);
	free (c->rows);
	free (c->header);
	free (c->prefix);
	free (c->suffix);
	free (c);
    }
    free (table->column_prefix);
    free (table->column_separator);
    free (table->columns);
    free (table);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_add_column_by_id (rtbl_t table, unsigned int id,
		       const char *header, unsigned int flags)
{
    struct column_data *col, **tmp;

    tmp = realloc (table->columns, (table->num_columns + 1) * sizeof (*tmp));
    if (tmp == NULL)
	return ENOMEM;
    table->columns = tmp;
    col = malloc (sizeof (*col));
    if (col == NULL)
	return ENOMEM;
    col->header = strdup (header);
    if (col->header == NULL) {
	free (col);
	return ENOMEM;
    }
    col->prefix = NULL;
    col->width = 0;
    col->flags = flags;
    col->num_rows = 0;
    col->rows = NULL;
    col->column_id = id;
    col->suffix = NULL;
    table->columns[table->num_columns++] = col;
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_add_column (rtbl_t table, const char *header, unsigned int flags)
{
    return rtbl_add_column_by_id(table, 0, header, flags);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_new_row(rtbl_t table)
{
    size_t max_rows = 0;
    size_t c;
    for (c = 0; c < table->num_columns; c++)
	if(table->columns[c]->num_rows > max_rows)
	    max_rows = table->columns[c]->num_rows;
    for (c = 0; c < table->num_columns; c++) {
	struct column_entry *tmp;

	if(table->columns[c]->num_rows == max_rows)
	    continue;
	tmp = realloc(table->columns[c]->rows,
		      max_rows * sizeof(table->columns[c]->rows));
	if(tmp == NULL)
	    return ENOMEM;
	table->columns[c]->rows = tmp;
	while(table->columns[c]->num_rows < max_rows) {
	    if((tmp[table->columns[c]->num_rows++].data = strdup("")) == NULL)
		return ENOMEM;
	}
    }
    return 0;
}

static void
column_compute_width (rtbl_t table, struct column_data *column)
{
    size_t i;

    if(table->flags & RTBL_HEADER_STYLE_NONE)
	column->width = 0;
    else
	column->width = strlen (column->header);
    for (i = 0; i < column->num_rows; i++)
	column->width = max (column->width, (int) strlen (column->rows[i].data));
}

/* DEPRECATED */
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_set_prefix (rtbl_t table, const char *prefix)
{
    if (table->column_prefix)
	free (table->column_prefix);
    table->column_prefix = strdup (prefix);
    if (table->column_prefix == NULL)
	return ENOMEM;
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_set_separator (rtbl_t table, const char *separator)
{
    if (table->column_separator)
	free (table->column_separator);
    table->column_separator = strdup (separator);
    if (table->column_separator == NULL)
	return ENOMEM;
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_set_column_prefix (rtbl_t table, const char *column,
			const char *prefix)
{
    struct column_data *c = rtbl_get_column (table, column);

    if (c == NULL)
	return -1;
    if (c->prefix)
	free (c->prefix);
    c->prefix = strdup (prefix);
    if (c->prefix == NULL)
	return ENOMEM;
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_set_column_affix_by_id(rtbl_t table, unsigned int id,
			    const char *prefix, const char *suffix)
{
    struct column_data *c = rtbl_get_column_by_id (table, id);

    if (c == NULL)
	return -1;
    if (c->prefix)
	free (c->prefix);
    if(prefix == NULL)
	c->prefix = NULL;
    else {
	c->prefix = strdup (prefix);
	if (c->prefix == NULL)
	    return ENOMEM;
    }

    if (c->suffix)
	free (c->suffix);
    if(suffix == NULL)
	c->suffix = NULL;
    else {
	c->suffix = strdup (suffix);
	if (c->suffix == NULL)
	    return ENOMEM;
    }
    return 0;
}


static const char *
get_column_prefix (rtbl_t table, struct column_data *c)
{
    if (c == NULL)
	return "";
    if (c->prefix)
	return c->prefix;
    if (table->column_prefix)
	return table->column_prefix;
    return "";
}

static const char *
get_column_suffix (rtbl_t table, struct column_data *c)
{
    if (c && c->suffix)
	return c->suffix;
    return "";
}

static int
add_column_entry (struct column_data *c, const char *data)
{
    struct column_entry row, *tmp;

    row.data = strdup (data);
    if (row.data == NULL)
	return ENOMEM;
    tmp = realloc (c->rows, (c->num_rows + 1) * sizeof (*tmp));
    if (tmp == NULL) {
	free (row.data);
	return ENOMEM;
    }
    c->rows = tmp;
    c->rows[c->num_rows++] = row;
    return 0;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_add_column_entry_by_id (rtbl_t table, unsigned int id, const char *data)
{
    struct column_data *c = rtbl_get_column_by_id (table, id);

    if (c == NULL)
	return -1;

    return add_column_entry(c, data);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_add_column_entryv_by_id (rtbl_t table, unsigned int id,
			      const char *fmt, ...)
{
    va_list ap;
    char *str;
    int ret;

    va_start(ap, fmt);
    ret = vasprintf(&str, fmt, ap);
    va_end(ap);
    if (ret == -1)
	return -1;
    ret = rtbl_add_column_entry_by_id(table, id, str);
    free(str);
    return ret;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_add_column_entry (rtbl_t table, const char *column, const char *data)
{
    struct column_data *c = rtbl_get_column (table, column);

    if (c == NULL)
	return -1;

    return add_column_entry(c, data);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_add_column_entryv (rtbl_t table, const char *column, const char *fmt, ...)
{
    va_list ap;
    char *str;
    int ret;

    va_start(ap, fmt);
    ret = vasprintf(&str, fmt, ap);
    va_end(ap);
    if (ret == -1)
	return -1;
    ret = rtbl_add_column_entry(table, column, str);
    free(str);
    return ret;
}


ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rtbl_format (rtbl_t table, FILE * f)
{
    size_t i, j;

    for (i = 0; i < table->num_columns; i++)
	column_compute_width (table, table->columns[i]);
    if((table->flags & RTBL_HEADER_STYLE_NONE) == 0) {
	for (i = 0; i < table->num_columns; i++) {
	    struct column_data *c = table->columns[i];

	    if(table->column_separator != NULL && i > 0)
		fprintf (f, "%s", table->column_separator);
	    fprintf (f, "%s", get_column_prefix (table, c));
	    if(i == table->num_columns - 1 && c->suffix == NULL)
		/* last column, so no need to pad with spaces */
		fprintf (f, "%-*s", 0, c->header);
	    else
		fprintf (f, "%-*s", (int)c->width, c->header);
	    fprintf (f, "%s", get_column_suffix (table, c));
	}
	fprintf (f, "\n");
    }

    for (j = 0;; j++) {
	int flag = 0;

	/* are there any more rows left? */
	for (i = 0; flag == 0 && i < table->num_columns; ++i) {
	    struct column_data *c = table->columns[i];

	    if (c->num_rows > j) {
		++flag;
		break;
	    }
	}
	if (flag == 0)
	    break;

	for (i = 0; i < table->num_columns; i++) {
	    int w;
	    struct column_data *c = table->columns[i];

	    if(table->column_separator != NULL && i > 0)
		fprintf (f, "%s", table->column_separator);

	    w = c->width;

	    if ((c->flags & RTBL_ALIGN_RIGHT) == 0) {
		if(i == table->num_columns - 1 && c->suffix == NULL)
		    /* last column, so no need to pad with spaces */
		    w = 0;
		else
		    w = -w;
	    }
	    fprintf (f, "%s", get_column_prefix (table, c));
	    if (c->num_rows <= j)
		fprintf (f, "%*s", w, "");
	    else
		fprintf (f, "%*s", w, c->rows[j].data);
	    fprintf (f, "%s", get_column_suffix (table, c));
	}
	fprintf (f, "\n");
    }
    return 0;
}

#ifdef TEST
int
main (int argc, char **argv)
{
    rtbl_t table;

    table = rtbl_create ();
    rtbl_add_column_by_id (table, 0, "Issued", 0);
    rtbl_add_column_by_id (table, 1, "Expires", 0);
    rtbl_add_column_by_id (table, 2, "Foo", RTBL_ALIGN_RIGHT);
    rtbl_add_column_by_id (table, 3, "Principal", 0);

    rtbl_add_column_entry_by_id (table, 0, "Jul  7 21:19:29");
    rtbl_add_column_entry_by_id (table, 1, "Jul  8 07:19:29");
    rtbl_add_column_entry_by_id (table, 2, "73");
    rtbl_add_column_entry_by_id (table, 2, "0");
    rtbl_add_column_entry_by_id (table, 2, "-2000");
    rtbl_add_column_entry_by_id (table, 3, "krbtgt/NADA.KTH.SE@NADA.KTH.SE");

    rtbl_add_column_entry_by_id (table, 0, "Jul  7 21:19:29");
    rtbl_add_column_entry_by_id (table, 1, "Jul  8 07:19:29");
    rtbl_add_column_entry_by_id (table, 3, "afs/pdc.kth.se@NADA.KTH.SE");

    rtbl_add_column_entry_by_id (table, 0, "Jul  7 21:19:29");
    rtbl_add_column_entry_by_id (table, 1, "Jul  8 07:19:29");
    rtbl_add_column_entry_by_id (table, 3, "afs@NADA.KTH.SE");

    rtbl_set_separator (table, "  ");

    rtbl_format (table, stdout);

    rtbl_destroy (table);

    printf("\n");

    table = rtbl_create ();
    rtbl_add_column_by_id (table, 0, "Column A", 0);
    rtbl_set_column_affix_by_id (table, 0, "<", ">");
    rtbl_add_column_by_id (table, 1, "Column B", 0);
    rtbl_set_column_affix_by_id (table, 1, "[", "]");
    rtbl_add_column_by_id (table, 2, "Column C", 0);
    rtbl_set_column_affix_by_id (table, 2, "(", ")");

    rtbl_add_column_entry_by_id (table, 0, "1");
    rtbl_new_row(table);
    rtbl_add_column_entry_by_id (table, 1, "2");
    rtbl_new_row(table);
    rtbl_add_column_entry_by_id (table, 2, "3");
    rtbl_new_row(table);

    rtbl_set_separator (table, "  ");
    rtbl_format (table, stdout);

    rtbl_destroy (table);

    return 0;
}

#endif
