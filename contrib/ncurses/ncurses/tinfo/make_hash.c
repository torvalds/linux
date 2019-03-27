/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	make_hash.c --- build-time program for constructing comp_captab.c
 *
 */

#include <build.priv.h>

#include <tic.h>
#include <hashsize.h>

#include <ctype.h>

MODULE_ID("$Id: make_hash.c,v 1.13 2013/09/28 20:55:47 tom Exp $")

/*
 *	_nc_make_hash_table()
 *
 *	Takes the entries in table[] and hashes them into hash_table[]
 *	by name.  There are CAPTABSIZE entries in table[] and HASHTABSIZE
 *	slots in hash_table[].
 *
 */

#undef MODULE_ID
#define MODULE_ID(id)		/*nothing */
#include <tinfo/doalloc.c>

static void
failed(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

static char *
strmalloc(char *s)
{
    size_t need = strlen(s) + 1;
    char *result = malloc(need);
    if (result == 0)
	failed("strmalloc");
    _nc_STRCPY(result, s, need);
    return result;
}

/*
 *	int hash_function(string)
 *
 *	Computes the hashing function on the given string.
 *
 *	The current hash function is the sum of each consectutive pair
 *	of characters, taken as two-byte integers, mod HASHTABSIZE.
 *
 */

static int
hash_function(const char *string)
{
    long sum = 0;

    while (*string) {
	sum += (long) (*string + (*(string + 1) << 8));
	string++;
    }

    return (int) (sum % HASHTABSIZE);
}

static void
_nc_make_hash_table(struct name_table_entry *table,
		    HashValue * hash_table)
{
    short i;
    int hashvalue;
    int collisions = 0;

    for (i = 0; i < HASHTABSIZE; i++) {
	hash_table[i] = -1;
    }
    for (i = 0; i < CAPTABSIZE; i++) {
	hashvalue = hash_function(table[i].nte_name);

	if (hash_table[hashvalue] >= 0)
	    collisions++;

	if (hash_table[hashvalue] != 0)
	    table[i].nte_link = hash_table[hashvalue];
	hash_table[hashvalue] = i;
    }

    printf("/* %d collisions out of %d entries */\n", collisions, CAPTABSIZE);
}

/*
 * This filter reads from standard input a list of tab-delimited columns,
 * (e.g., from Caps.filtered) computes the hash-value of a specified column and
 * writes the hashed tables to standard output.
 *
 * By compiling the hash table at build time, we're able to make the entire
 * set of terminfo and termcap tables readonly (and also provide some runtime
 * performance enhancement).
 */

#define MAX_COLUMNS BUFSIZ	/* this _has_ to be worst-case */

static int
count_columns(char **list)
{
    int result = 0;
    if (list != 0) {
	while (*list++) {
	    ++result;
	}
    }
    return result;
}

static char **
parse_columns(char *buffer)
{
    static char **list;

    int col = 0;

    if (list == 0 && (list = typeCalloc(char *, (MAX_COLUMNS + 1))) == 0)
	  return (0);

    if (*buffer != '#') {
	while (*buffer != '\0') {
	    char *s;
	    for (s = buffer; (*s != '\0') && !isspace(UChar(*s)); s++)
		/*EMPTY */ ;
	    if (s != buffer) {
		char mark = *s;
		*s = '\0';
		if ((s - buffer) > 1
		    && (*buffer == '"')
		    && (s[-1] == '"')) {	/* strip the quotes */
		    assert(s > buffer + 1);
		    s[-1] = '\0';
		    buffer++;
		}
		list[col] = buffer;
		col++;
		if (mark == '\0')
		    break;
		while (*++s && isspace(UChar(*s)))
		    /*EMPTY */ ;
		buffer = s;
	    } else
		break;
	}
    }
    return col ? list : 0;
}

int
main(int argc, char **argv)
{
    struct name_table_entry *name_table = typeCalloc(struct
						     name_table_entry, CAPTABSIZE);
    HashValue *hash_table = typeCalloc(HashValue, HASHTABSIZE);
    const char *root_name = "";
    int column = 0;
    int bigstring = 0;
    int n;
    char buffer[BUFSIZ];

    static const char *typenames[] =
    {"BOOLEAN", "NUMBER", "STRING"};

    short BoolCount = 0;
    short NumCount = 0;
    short StrCount = 0;

    /* The first argument is the column-number (starting with 0).
     * The second is the root name of the tables to generate.
     */
    if (argc <= 3
	|| (column = atoi(argv[1])) <= 0
	|| (column >= MAX_COLUMNS)
	|| *(root_name = argv[2]) == 0
	|| (bigstring = atoi(argv[3])) < 0
	|| name_table == 0
	|| hash_table == 0) {
	fprintf(stderr, "usage: make_hash column root_name bigstring\n");
	exit(EXIT_FAILURE);
    }

    /*
     * Read the table into our arrays.
     */
    for (n = 0; (n < CAPTABSIZE) && fgets(buffer, BUFSIZ, stdin);) {
	char **list, *nlp = strchr(buffer, '\n');
	if (nlp)
	    *nlp = '\0';
	list = parse_columns(buffer);
	if (list == 0)		/* blank or comment */
	    continue;
	if (column > count_columns(list)) {
	    fprintf(stderr, "expected %d columns, have %d:\n%s\n",
		    column,
		    count_columns(list),
		    buffer);
	    exit(EXIT_FAILURE);
	}
	name_table[n].nte_link = -1;	/* end-of-hash */
	name_table[n].nte_name = strmalloc(list[column]);
	if (!strcmp(list[2], "bool")) {
	    name_table[n].nte_type = BOOLEAN;
	    name_table[n].nte_index = BoolCount++;
	} else if (!strcmp(list[2], "num")) {
	    name_table[n].nte_type = NUMBER;
	    name_table[n].nte_index = NumCount++;
	} else if (!strcmp(list[2], "str")) {
	    name_table[n].nte_type = STRING;
	    name_table[n].nte_index = StrCount++;
	} else {
	    fprintf(stderr, "Unknown type: %s\n", list[2]);
	    exit(EXIT_FAILURE);
	}
	n++;
    }
    _nc_make_hash_table(name_table, hash_table);

    /*
     * Write the compiled tables to standard output
     */
    if (bigstring) {
	int len = 0;
	int nxt;

	printf("static const char %s_names_text[] = \\\n", root_name);
	for (n = 0; n < CAPTABSIZE; n++) {
	    nxt = (int) strlen(name_table[n].nte_name) + 5;
	    if (nxt + len > 72) {
		printf("\\\n");
		len = 0;
	    }
	    printf("\"%s\\0\" ", name_table[n].nte_name);
	    len += nxt;
	}
	printf(";\n\n");

	len = 0;
	printf("static name_table_data const %s_names_data[] =\n",
	       root_name);
	printf("{\n");
	for (n = 0; n < CAPTABSIZE; n++) {
	    printf("\t{ %15d,\t%10s,\t%3d, %3d }%c\n",
		   len,
		   typenames[name_table[n].nte_type],
		   name_table[n].nte_index,
		   name_table[n].nte_link,
		   n < CAPTABSIZE - 1 ? ',' : ' ');
	    len += (int) strlen(name_table[n].nte_name) + 1;
	}
	printf("};\n\n");
	printf("static struct name_table_entry *_nc_%s_table = 0;\n\n", root_name);
    } else {

	printf("static struct name_table_entry const _nc_%s_table[] =\n",
	       root_name);
	printf("{\n");
	for (n = 0; n < CAPTABSIZE; n++) {
	    _nc_SPRINTF(buffer, _nc_SLIMIT(sizeof(buffer)) "\"%s\"",
			name_table[n].nte_name);
	    printf("\t{ %15s,\t%10s,\t%3d, %3d }%c\n",
		   buffer,
		   typenames[name_table[n].nte_type],
		   name_table[n].nte_index,
		   name_table[n].nte_link,
		   n < CAPTABSIZE - 1 ? ',' : ' ');
	}
	printf("};\n\n");
    }

    printf("static const HashValue _nc_%s_hash_table[%d] =\n",
	   root_name,
	   HASHTABSIZE + 1);
    printf("{\n");
    for (n = 0; n < HASHTABSIZE; n++) {
	printf("\t%3d,\n", hash_table[n]);
    }
    printf("\t0\t/* base-of-table */\n");
    printf("};\n\n");

    printf("#if (BOOLCOUNT!=%d)||(NUMCOUNT!=%d)||(STRCOUNT!=%d)\n",
	   BoolCount, NumCount, StrCount);
    printf("#error\t--> term.h and comp_captab.c disagree about the <--\n");
    printf("#error\t--> numbers of booleans, numbers and/or strings <--\n");
    printf("#endif\n\n");

    free(hash_table);
    return EXIT_SUCCESS;
}
