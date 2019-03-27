/* misc - miscellaneous flex routines */

/*  Copyright (c) 1990 The Regents of the University of California. */
/*  All rights reserved. */

/*  This code is derived from software contributed to Berkeley by */
/*  Vern Paxson. */

/*  The United States Government has rights in this work pursuant */
/*  to contract no. DE-AC03-76SF00098 between the United States */
/*  Department of Energy and the University of California. */

/*  This file is part of flex. */

/*  Redistribution and use in source and binary forms, with or without */
/*  modification, are permitted provided that the following conditions */
/*  are met: */

/*  1. Redistributions of source code must retain the above copyright */
/*     notice, this list of conditions and the following disclaimer. */
/*  2. Redistributions in binary form must reproduce the above copyright */
/*     notice, this list of conditions and the following disclaimer in the */
/*     documentation and/or other materials provided with the distribution. */

/*  Neither the name of the University nor the names of its contributors */
/*  may be used to endorse or promote products derived from this software */
/*  without specific prior written permission. */

/*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR */
/*  IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR */
/*  PURPOSE. */

#include "flexdef.h"
#include "tables.h"

#define CMD_IF_TABLES_SER    "%if-tables-serialization"
#define CMD_TABLES_YYDMAP    "%tables-yydmap"
#define CMD_DEFINE_YYTABLES  "%define-yytables"
#define CMD_IF_CPP_ONLY      "%if-c++-only"
#define CMD_IF_C_ONLY        "%if-c-only"
#define CMD_IF_C_OR_CPP      "%if-c-or-c++"
#define CMD_NOT_FOR_HEADER   "%not-for-header"
#define CMD_OK_FOR_HEADER    "%ok-for-header"
#define CMD_PUSH             "%push"
#define CMD_POP              "%pop"
#define CMD_IF_REENTRANT     "%if-reentrant"
#define CMD_IF_NOT_REENTRANT "%if-not-reentrant"
#define CMD_IF_BISON_BRIDGE  "%if-bison-bridge"
#define CMD_IF_NOT_BISON_BRIDGE  "%if-not-bison-bridge"
#define CMD_ENDIF            "%endif"

/* we allow the skeleton to push and pop. */
struct sko_state {
    bool dc; /**< do_copy */
};
static struct sko_state *sko_stack=0;
static int sko_len=0,sko_sz=0;
static void sko_push(bool dc)
{
    if(!sko_stack){
        sko_sz = 1;
        sko_stack = (struct sko_state*)flex_alloc(sizeof(struct sko_state)*sko_sz);
        if (!sko_stack)
            flexfatal(_("allocation of sko_stack failed"));
        sko_len = 0;
    }
    if(sko_len >= sko_sz){
        sko_sz *= 2;
        sko_stack = (struct sko_state*)flex_realloc(sko_stack,sizeof(struct sko_state)*sko_sz);
    }
    
    /* initialize to zero and push */
    sko_stack[sko_len].dc = dc;
    sko_len++;
}
static void sko_peek(bool *dc)
{
    if(sko_len <= 0)
        flex_die("peek attempt when sko stack is empty");
    if(dc)
        *dc = sko_stack[sko_len-1].dc;
}
static void sko_pop(bool* dc)
{
    sko_peek(dc);
    sko_len--;
    if(sko_len < 0)
        flex_die("popped too many times in skeleton.");
}

/* Append "#define defname value\n" to the running buffer. */
void action_define (defname, value)
     const char *defname;
     int value;
{
	char    buf[MAXLINE];
	char   *cpy;

	if ((int) strlen (defname) > MAXLINE / 2) {
		format_pinpoint_message (_
					 ("name \"%s\" ridiculously long"),
					 defname);
		return;
	}

	snprintf (buf, sizeof(buf), "#define %s %d\n", defname, value);
	add_action (buf);

	/* track #defines so we can undef them when we're done. */
	cpy = copy_string (defname);
	buf_append (&defs_buf, &cpy, 1);
}


#ifdef notdef
/** Append "m4_define([[defname]],[[value]])m4_dnl\n" to the running buffer.
 *  @param defname The macro name.
 *  @param value The macro value, can be NULL, which is the same as the empty string.
 */
void action_m4_define (const char *defname, const char * value)
{
	char    buf[MAXLINE];

    flexfatal ("DO NOT USE THIS FUNCTION!");

	if ((int) strlen (defname) > MAXLINE / 2) {
		format_pinpoint_message (_
					 ("name \"%s\" ridiculously long"),
					 defname);
		return;
	}

	snprintf (buf, sizeof(buf), "m4_define([[%s]],[[%s]])m4_dnl\n", defname, value?value:"");
	add_action (buf);
}
#endif

/* Append "new_text" to the running buffer. */
void add_action (new_text)
     const char   *new_text;
{
	int     len = strlen (new_text);

	while (len + action_index >= action_size - 10 /* slop */ ) {
		int     new_size = action_size * 2;

		if (new_size <= 0)
			/* Increase just a little, to try to avoid overflow
			 * on 16-bit machines.
			 */
			action_size += action_size / 8;
		else
			action_size = new_size;

		action_array =
			reallocate_character_array (action_array,
						    action_size);
	}

	strcpy (&action_array[action_index], new_text);

	action_index += len;
}


/* allocate_array - allocate memory for an integer array of the given size */

void   *allocate_array (size, element_size)
     int size;
     size_t element_size;
{
	void *mem;
	size_t  num_bytes = element_size * size;

	mem = flex_alloc (num_bytes);
	if (!mem)
		flexfatal (_
			   ("memory allocation failed in allocate_array()"));

	return mem;
}


/* all_lower - true if a string is all lower-case */

int all_lower (str)
     char *str;
{
	while (*str) {
		if (!isascii ((Char) * str) || !islower ((Char) * str))
			return 0;
		++str;
	}

	return 1;
}


/* all_upper - true if a string is all upper-case */

int all_upper (str)
     char *str;
{
	while (*str) {
		if (!isascii ((Char) * str) || !isupper ((Char) * str))
			return 0;
		++str;
	}

	return 1;
}


/* intcmp - compares two integers for use by qsort. */

int intcmp (const void *a, const void *b)
{
  return *(const int *) a - *(const int *) b;
}


/* check_char - checks a character to make sure it's within the range
 *		we're expecting.  If not, generates fatal error message
 *		and exits.
 */

void check_char (c)
     int c;
{
	if (c >= CSIZE)
		lerrsf (_("bad character '%s' detected in check_char()"),
			readable_form (c));

	if (c >= csize)
		lerrsf (_
			("scanner requires -8 flag to use the character %s"),
			readable_form (c));
}



/* clower - replace upper-case letter to lower-case */

Char clower (c)
     int c;
{
	return (Char) ((isascii (c) && isupper (c)) ? tolower (c) : c);
}


/* copy_string - returns a dynamically allocated copy of a string */

char   *copy_string (str)
     const char *str;
{
	const char *c1;
	char *c2;
	char   *copy;
	unsigned int size;

	/* find length */
	for (c1 = str; *c1; ++c1) ;

	size = (c1 - str + 1) * sizeof (char);

	copy = (char *) flex_alloc (size);

	if (copy == NULL)
		flexfatal (_("dynamic memory failure in copy_string()"));

	for (c2 = copy; (*c2++ = *str++) != 0;) ;

	return copy;
}


/* copy_unsigned_string -
 *    returns a dynamically allocated copy of a (potentially) unsigned string
 */

Char   *copy_unsigned_string (str)
     Char *str;
{
	Char *c;
	Char   *copy;

	/* find length */
	for (c = str; *c; ++c) ;

	copy = allocate_Character_array (c - str + 1);

	for (c = copy; (*c++ = *str++) != 0;) ;

	return copy;
}


/* cclcmp - compares two characters for use by qsort with '\0' sorting last. */

int cclcmp (const void *a, const void *b)
{
  if (!*(const Char *) a)
	return 1;
  else
	if (!*(const Char *) b)
	  return - 1;
	else
	  return *(const Char *) a - *(const Char *) b;
}


/* dataend - finish up a block of data declarations */

void dataend ()
{
	/* short circuit any output */
	if (gentables) {

		if (datapos > 0)
			dataflush ();

		/* add terminator for initialization; { for vi */
		outn ("    } ;\n");
	}
	dataline = 0;
	datapos = 0;
}


/* dataflush - flush generated data statements */

void dataflush ()
{
	/* short circuit any output */
	if (!gentables)
		return;

	outc ('\n');

	if (++dataline >= NUMDATALINES) {
		/* Put out a blank line so that the table is grouped into
		 * large blocks that enable the user to find elements easily.
		 */
		outc ('\n');
		dataline = 0;
	}

	/* Reset the number of characters written on the current line. */
	datapos = 0;
}


/* flexerror - report an error message and terminate */

void flexerror (msg)
     const char *msg;
{
	fprintf (stderr, "%s: %s\n", program_name, msg);
	flexend (1);
}


/* flexfatal - report a fatal error message and terminate */

void flexfatal (msg)
     const char *msg;
{
	fprintf (stderr, _("%s: fatal internal error, %s\n"),
		 program_name, msg);
	FLEX_EXIT (1);
}


/* htoi - convert a hexadecimal digit string to an integer value */

int htoi (str)
     Char str[];
{
	unsigned int result;

	(void) sscanf ((char *) str, "%x", &result);

	return result;
}


/* lerrif - report an error message formatted with one integer argument */

void lerrif (msg, arg)
     const char *msg;
     int arg;
{
	char    errmsg[MAXLINE];

	snprintf (errmsg, sizeof(errmsg), msg, arg);
	flexerror (errmsg);
}


/* lerrsf - report an error message formatted with one string argument */

void lerrsf (msg, arg)
	const char *msg, arg[];
{
	char    errmsg[MAXLINE];

	snprintf (errmsg, sizeof(errmsg)-1, msg, arg);
	errmsg[sizeof(errmsg)-1] = 0; /* ensure NULL termination */
	flexerror (errmsg);
}


/* lerrsf_fatal - as lerrsf, but call flexfatal */

void lerrsf_fatal (msg, arg)
	const char *msg, arg[];
{
	char    errmsg[MAXLINE];

	snprintf (errmsg, sizeof(errmsg)-1, msg, arg);
	errmsg[sizeof(errmsg)-1] = 0; /* ensure NULL termination */
	flexfatal (errmsg);
}


/* line_directive_out - spit out a "#line" statement */

void line_directive_out (output_file, do_infile)
     FILE   *output_file;
     int do_infile;
{
	char    directive[MAXLINE], filename[MAXLINE];
	char   *s1, *s2, *s3;
	static const char *line_fmt = "#line %d \"%s\"\n";

	if (!gen_line_dirs)
		return;

	s1 = do_infile ? infilename : "M4_YY_OUTFILE_NAME";

	if (do_infile && !s1)
        s1 = "<stdin>";
    
	s2 = filename;
	s3 = &filename[sizeof (filename) - 2];

	while (s2 < s3 && *s1) {
		if (*s1 == '\\')
			/* Escape the '\' */
			*s2++ = '\\';

		*s2++ = *s1++;
	}

	*s2 = '\0';

	if (do_infile)
		snprintf (directive, sizeof(directive), line_fmt, linenum, filename);
	else {
		snprintf (directive, sizeof(directive), line_fmt, 0, filename);
	}

	/* If output_file is nil then we should put the directive in
	 * the accumulated actions.
	 */
	if (output_file) {
		fputs (directive, output_file);
	}
	else
		add_action (directive);
}


/* mark_defs1 - mark the current position in the action array as
 *               representing where the user's section 1 definitions end
 *		 and the prolog begins
 */
void mark_defs1 ()
{
	defs1_offset = 0;
	action_array[action_index++] = '\0';
	action_offset = prolog_offset = action_index;
	action_array[action_index] = '\0';
}


/* mark_prolog - mark the current position in the action array as
 *               representing the end of the action prolog
 */
void mark_prolog ()
{
	action_array[action_index++] = '\0';
	action_offset = action_index;
	action_array[action_index] = '\0';
}


/* mk2data - generate a data statement for a two-dimensional array
 *
 * Generates a data statement initializing the current 2-D array to "value".
 */
void mk2data (value)
     int value;
{
	/* short circuit any output */
	if (!gentables)
		return;

	if (datapos >= NUMDATAITEMS) {
		outc (',');
		dataflush ();
	}

	if (datapos == 0)
		/* Indent. */
		out ("    ");

	else
		outc (',');

	++datapos;

	out_dec ("%5d", value);
}


/* mkdata - generate a data statement
 *
 * Generates a data statement initializing the current array element to
 * "value".
 */
void mkdata (value)
     int value;
{
	/* short circuit any output */
	if (!gentables)
		return;

	if (datapos >= NUMDATAITEMS) {
		outc (',');
		dataflush ();
	}

	if (datapos == 0)
		/* Indent. */
		out ("    ");
	else
		outc (',');

	++datapos;

	out_dec ("%5d", value);
}


/* myctoi - return the integer represented by a string of digits */

int myctoi (array)
     const char *array;
{
	int     val = 0;

	(void) sscanf (array, "%d", &val);

	return val;
}


/* myesc - return character corresponding to escape sequence */

Char myesc (array)
     Char array[];
{
	Char    c, esc_char;

	switch (array[1]) {
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'n':
		return '\n';
	case 'r':
		return '\r';
	case 't':
		return '\t';

#if defined (__STDC__)
	case 'a':
		return '\a';
	case 'v':
		return '\v';
#else
	case 'a':
		return '\007';
	case 'v':
		return '\013';
#endif

	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
		{		/* \<octal> */
			int     sptr = 1;

			while (isascii (array[sptr]) &&
			       isdigit (array[sptr]))
				/* Don't increment inside loop control
				 * because if isdigit() is a macro it might
				 * expand into multiple increments ...
				 */
				++sptr;

			c = array[sptr];
			array[sptr] = '\0';

			esc_char = otoi (array + 1);

			array[sptr] = c;

			return esc_char;
		}

	case 'x':
		{		/* \x<hex> */
			int     sptr = 2;

			while (isascii (array[sptr]) &&
			       isxdigit (array[sptr]))
				/* Don't increment inside loop control
				 * because if isdigit() is a macro it might
				 * expand into multiple increments ...
				 */
				++sptr;

			c = array[sptr];
			array[sptr] = '\0';

			esc_char = htoi (array + 2);

			array[sptr] = c;

			return esc_char;
		}

	default:
		return array[1];
	}
}


/* otoi - convert an octal digit string to an integer value */

int otoi (str)
     Char str[];
{
	unsigned int result;

	(void) sscanf ((char *) str, "%o", &result);
	return result;
}


/* out - various flavors of outputing a (possibly formatted) string for the
 *	 generated scanner, keeping track of the line count.
 */

void out (str)
     const char *str;
{
	fputs (str, stdout);
}

void out_dec (fmt, n)
     const char *fmt;
     int n;
{
	fprintf (stdout, fmt, n);
}

void out_dec2 (fmt, n1, n2)
     const char *fmt;
     int n1, n2;
{
	fprintf (stdout, fmt, n1, n2);
}

void out_hex (fmt, x)
     const char *fmt;
     unsigned int x;
{
	fprintf (stdout, fmt, x);
}

void out_str (fmt, str)
     const char *fmt, str[];
{
	fprintf (stdout,fmt, str);
}

void out_str3 (fmt, s1, s2, s3)
     const char *fmt, s1[], s2[], s3[];
{
	fprintf (stdout,fmt, s1, s2, s3);
}

void out_str_dec (fmt, str, n)
     const char *fmt, str[];
     int n;
{
	fprintf (stdout,fmt, str, n);
}

void outc (c)
     int c;
{
	fputc (c, stdout);
}

void outn (str)
     const char *str;
{
	fputs (str,stdout);
    fputc('\n',stdout);
}

/** Print "m4_define( [[def]], [[val]])m4_dnl\n".
 * @param def The m4 symbol to define.
 * @param val The definition; may be NULL.
 * @return buf
 */
void out_m4_define (const char* def, const char* val)
{
    const char * fmt = "m4_define( [[%s]], [[%s]])m4_dnl\n";
    fprintf(stdout, fmt, def, val?val:"");
}


/* readable_form - return the human-readable form of a character
 *
 * The returned string is in static storage.
 */

char   *readable_form (c)
     int c;
{
	static char rform[10];

	if ((c >= 0 && c < 32) || c >= 127) {
		switch (c) {
		case '\b':
			return "\\b";
		case '\f':
			return "\\f";
		case '\n':
			return "\\n";
		case '\r':
			return "\\r";
		case '\t':
			return "\\t";

#if defined (__STDC__)
		case '\a':
			return "\\a";
		case '\v':
			return "\\v";
#endif

		default:
			snprintf (rform, sizeof(rform), "\\%.3o", (unsigned int) c);
			return rform;
		}
	}

	else if (c == ' ')
		return "' '";

	else {
		rform[0] = c;
		rform[1] = '\0';

		return rform;
	}
}


/* reallocate_array - increase the size of a dynamic array */

void   *reallocate_array (array, size, element_size)
     void   *array;
     int size;
     size_t element_size;
{
	void *new_array;
	size_t  num_bytes = element_size * size;

	new_array = flex_realloc (array, num_bytes);
	if (!new_array)
		flexfatal (_("attempt to increase array size failed"));

	return new_array;
}


/* skelout - write out one section of the skeleton file
 *
 * Description
 *    Copies skelfile or skel array to stdout until a line beginning with
 *    "%%" or EOF is found.
 */
void skelout ()
{
	char    buf_storage[MAXLINE];
	char   *buf = buf_storage;
	bool   do_copy = true;

    /* "reset" the state by clearing the buffer and pushing a '1' */
    if(sko_len > 0)
        sko_peek(&do_copy);
    sko_len = 0;
    sko_push(do_copy=true);


	/* Loop pulling lines either from the skelfile, if we're using
	 * one, or from the skel[] array.
	 */
	while (skelfile ?
	       (fgets (buf, MAXLINE, skelfile) != NULL) :
	       ((buf = (char *) skel[skel_ind++]) != 0)) {

		if (skelfile)
			chomp (buf);

		/* copy from skel array */
		if (buf[0] == '%') {	/* control line */
			/* print the control line as a comment. */
			if (ddebug && buf[1] != '#') {
				if (buf[strlen (buf) - 1] == '\\')
					out_str ("/* %s */\\\n", buf);
				else
					out_str ("/* %s */\n", buf);
			}

			/* We've been accused of using cryptic markers in the skel.
			 * So we'll use emacs-style-hyphenated-commands.
             * We might consider a hash if this if-else-if-else
             * chain gets too large.
			 */
#define cmd_match(s) (strncmp(buf,(s),strlen(s))==0)

			if (buf[1] == '%') {
				/* %% is a break point for skelout() */
				return;
			}
            else if (cmd_match (CMD_PUSH)){
                sko_push(do_copy);
                if(ddebug){
                    out_str("/*(state = (%s) */",do_copy?"true":"false");
                }
                out_str("%s\n", buf[strlen (buf) - 1] =='\\' ? "\\" : "");
            }
            else if (cmd_match (CMD_POP)){
                sko_pop(&do_copy);
                if(ddebug){
                    out_str("/*(state = (%s) */",do_copy?"true":"false");
                }
                out_str("%s\n", buf[strlen (buf) - 1] =='\\' ? "\\" : "");
            }
            else if (cmd_match (CMD_IF_REENTRANT)){
                sko_push(do_copy);
                do_copy = reentrant && do_copy;
            }
            else if (cmd_match (CMD_IF_NOT_REENTRANT)){
                sko_push(do_copy);
                do_copy = !reentrant && do_copy;
            }
            else if (cmd_match(CMD_IF_BISON_BRIDGE)){
                sko_push(do_copy);
                do_copy = bison_bridge_lval && do_copy;
            }
            else if (cmd_match(CMD_IF_NOT_BISON_BRIDGE)){
                sko_push(do_copy);
                do_copy = !bison_bridge_lval && do_copy;
            }
            else if (cmd_match (CMD_ENDIF)){
                sko_pop(&do_copy);
            }
			else if (cmd_match (CMD_IF_TABLES_SER)) {
                do_copy = do_copy && tablesext;
			}
			else if (cmd_match (CMD_TABLES_YYDMAP)) {
				if (tablesext && yydmap_buf.elts)
					outn ((char *) (yydmap_buf.elts));
			}
            else if (cmd_match (CMD_DEFINE_YYTABLES)) {
                out_str("#define YYTABLES_NAME \"%s\"\n",
                        tablesname?tablesname:"yytables");
            }
			else if (cmd_match (CMD_IF_CPP_ONLY)) {
				/* only for C++ */
                sko_push(do_copy);
				do_copy = C_plus_plus;
			}
			else if (cmd_match (CMD_IF_C_ONLY)) {
				/* %- only for C */
                sko_push(do_copy);
				do_copy = !C_plus_plus;
			}
			else if (cmd_match (CMD_IF_C_OR_CPP)) {
				/* %* for C and C++ */
                sko_push(do_copy);
				do_copy = true;
			}
			else if (cmd_match (CMD_NOT_FOR_HEADER)) {
				/* %c begin linkage-only (non-header) code. */
				OUT_BEGIN_CODE ();
			}
			else if (cmd_match (CMD_OK_FOR_HEADER)) {
				/* %e end linkage-only code. */
				OUT_END_CODE ();
			}
			else if (buf[1] == '#') {
				/* %# a comment in the skel. ignore. */
			}
			else {
				flexfatal (_("bad line in skeleton file"));
			}
		}

		else if (do_copy) 
            outn (buf);
	}			/* end while */
}


/* transition_struct_out - output a yy_trans_info structure
 *
 * outputs the yy_trans_info structure with the two elements, element_v and
 * element_n.  Formats the output with spaces and carriage returns.
 */

void transition_struct_out (element_v, element_n)
     int element_v, element_n;
{

	/* short circuit any output */
	if (!gentables)
		return;

	out_dec2 (" {%4d,%4d },", element_v, element_n);

	datapos += TRANS_STRUCT_PRINT_LENGTH;

	if (datapos >= 79 - TRANS_STRUCT_PRINT_LENGTH) {
		outc ('\n');

		if (++dataline % 10 == 0)
			outc ('\n');

		datapos = 0;
	}
}


/* The following is only needed when building flex's parser using certain
 * broken versions of bison.
 */
void   *yy_flex_xmalloc (size)
     int size;
{
	void   *result = flex_alloc ((size_t) size);

	if (!result)
		flexfatal (_
			   ("memory allocation failed in yy_flex_xmalloc()"));

	return result;
}


/* zero_out - set a region of memory to 0
 *
 * Sets region_ptr[0] through region_ptr[size_in_bytes - 1] to zero.
 */

void zero_out (region_ptr, size_in_bytes)
     char   *region_ptr;
     size_t size_in_bytes;
{
	char *rp, *rp_end;

	rp = region_ptr;
	rp_end = region_ptr + size_in_bytes;

	while (rp < rp_end)
		*rp++ = 0;
}

/* Remove all '\n' and '\r' characters, if any, from the end of str.
 * str can be any null-terminated string, or NULL.
 * returns str. */
char   *chomp (str)
     char   *str;
{
	char   *p = str;

	if (!str || !*str)	/* s is null or empty string */
		return str;

	/* find end of string minus one */
	while (*p)
		++p;
	--p;

	/* eat newlines */
	while (p >= str && (*p == '\r' || *p == '\n'))
		*p-- = 0;
	return str;
}
