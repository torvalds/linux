/*
 *  $Id: rc.c,v 1.53 2018/05/31 20:32:15 tom Exp $
 *
 *  rc.c -- routines for processing the configuration file
 *
 *  Copyright 2000-2012,2018	Thomas E. Dickey
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
 *
 *  An earlier version of this program lists as authors
 *	Savio Lam (lam836@cs.cuhk.hk)
 */

#include <dialog.h>

#include <dlg_keys.h>

#ifdef HAVE_COLOR
#include <dlg_colors.h>

/*
 * For matching color names with color values
 */
static const color_names_st color_names[] =
{
#ifdef HAVE_USE_DEFAULT_COLORS
    {"DEFAULT", -1},
#endif
    {"BLACK", COLOR_BLACK},
    {"RED", COLOR_RED},
    {"GREEN", COLOR_GREEN},
    {"YELLOW", COLOR_YELLOW},
    {"BLUE", COLOR_BLUE},
    {"MAGENTA", COLOR_MAGENTA},
    {"CYAN", COLOR_CYAN},
    {"WHITE", COLOR_WHITE},
};				/* color names */
#define COLOR_COUNT	(sizeof(color_names) / sizeof(color_names[0]))
#endif /* HAVE_COLOR */

#define GLOBALRC "/etc/dialogrc"
#define DIALOGRC ".dialogrc"

/* Types of values */
#define VAL_INT  0
#define VAL_STR  1
#define VAL_BOOL 2

/* Type of line in configuration file */
typedef enum {
    LINE_ERROR = -1,
    LINE_EQUALS,
    LINE_EMPTY
} PARSE_LINE;

/* number of configuration variables */
#define VAR_COUNT        (sizeof(vars) / sizeof(vars_st))

/* check if character is string quoting characters */
#define isquote(c)       ((c) == '"' || (c) == '\'')

/* get last character of string */
#define lastch(str)      str[strlen(str)-1]

/*
 * Configuration variables
 */
typedef struct {
    const char *name;		/* name of configuration variable as in DIALOGRC */
    void *var;			/* address of actual variable to change */
    int type;			/* type of value */
    const char *comment;	/* comment to put in "rc" file */
} vars_st;

/*
 * This table should contain only references to dialog_state, since dialog_vars
 * is reset specially in dialog.c before each widget.
 */
static const vars_st vars[] =
{
    {"aspect",
     &dialog_state.aspect_ratio,
     VAL_INT,
     "Set aspect-ration."},

    {"separate_widget",
     &dialog_state.separate_str,
     VAL_STR,
     "Set separator (for multiple widgets output)."},

    {"tab_len",
     &dialog_state.tab_len,
     VAL_INT,
     "Set tab-length (for textbox tab-conversion)."},

    {"visit_items",
     &dialog_state.visit_items,
     VAL_BOOL,
     "Make tab-traversal for checklist, etc., include the list."},

#ifdef HAVE_COLOR
    {"use_shadow",
     &dialog_state.use_shadow,
     VAL_BOOL,
     "Shadow dialog boxes? This also turns on color."},

    {"use_colors",
     &dialog_state.use_colors,
     VAL_BOOL,
     "Turn color support ON or OFF"},
#endif				/* HAVE_COLOR */
};				/* vars */

static int
skip_whitespace(char *str, int n)
{
    while (isblank(UCH(str[n])) && str[n] != '\0')
	n++;
    return n;
}

static int
skip_keyword(char *str, int n)
{
    while (isalnum(UCH(str[n])) && str[n] != '\0')
	n++;
    return n;
}

static int
find_vars(char *name)
{
    int result = -1;
    unsigned i;

    for (i = 0; i < VAR_COUNT; i++) {
	if (dlg_strcmp(vars[i].name, name) == 0) {
	    result = (int) i;
	    break;
	}
    }
    return result;
}

#ifdef HAVE_COLOR
static int
find_color(char *name)
{
    int result = -1;
    int i;
    int limit = dlg_color_count();

    for (i = 0; i < limit; i++) {
	if (dlg_strcmp(dlg_color_table[i].name, name) == 0) {
	    result = i;
	    break;
	}
    }
    return result;
}

/*
 * Convert an attribute to a string representation like this:
 *
 * "(foreground,background,highlight)"
 */
static char *
attr_to_str(char *str, int fg, int bg, int hl)
{
    int i;

    strcpy(str, "(");
    /* foreground */
    for (i = 0; fg != color_names[i].value; i++) ;
    strcat(str, color_names[i].name);
    strcat(str, ",");

    /* background */
    for (i = 0; bg != color_names[i].value; i++) ;
    strcat(str, color_names[i].name);

    /* highlight */
    strcat(str, hl ? ",ON)" : ",OFF)");

    return str;
}

/*
 * Extract the foreground, background and highlight values from an attribute
 * represented as a string in one of two forms:
 *
 * "(foreground,background,highlight)"
 " "xxxx_color"
 */
static int
str_to_attr(char *str, int *fg, int *bg, int *hl)
{
    int i = 0, get_fg = 1;
    unsigned j;
    char tempstr[MAX_LEN + 1], *part;
    size_t have;

    if (str[0] != '(' || lastch(str) != ')') {
	if ((i = find_color(str)) >= 0) {
	    *fg = dlg_color_table[i].fg;
	    *bg = dlg_color_table[i].bg;
	    *hl = dlg_color_table[i].hilite;
	    return 0;
	}
	return -1;		/* invalid representation */
    }

    /* remove the parenthesis */
    have = strlen(str);
    if (have > MAX_LEN) {
	have = MAX_LEN - 1;
    } else {
	have -= 2;
    }
    memcpy(tempstr, str + 1, have);
    tempstr[have] = '\0';

    /* get foreground and background */

    while (1) {
	/* skip white space before fg/bg string */
	i = skip_whitespace(tempstr, i);
	if (tempstr[i] == '\0')
	    return -1;		/* invalid representation */
	part = tempstr + i;	/* set 'part' to start of fg/bg string */

	/* find end of fg/bg string */
	while (!isblank(UCH(tempstr[i])) && tempstr[i] != ','
	       && tempstr[i] != '\0')
	    i++;

	if (tempstr[i] == '\0')
	    return -1;		/* invalid representation */
	else if (isblank(UCH(tempstr[i]))) {	/* not yet ',' */
	    tempstr[i++] = '\0';

	    /* skip white space before ',' */
	    i = skip_whitespace(tempstr, i);
	    if (tempstr[i] != ',')
		return -1;	/* invalid representation */
	}
	tempstr[i++] = '\0';	/* skip the ',' */
	for (j = 0; j < COLOR_COUNT && dlg_strcmp(part, color_names[j].name);
	     j++) ;
	if (j == COLOR_COUNT)	/* invalid color name */
	    return -1;
	if (get_fg) {
	    *fg = color_names[j].value;
	    get_fg = 0;		/* next we have to get the background */
	} else {
	    *bg = color_names[j].value;
	    break;
	}
    }				/* got foreground and background */

    /* get highlight */

    /* skip white space before highlight string */
    i = skip_whitespace(tempstr, i);
    if (tempstr[i] == '\0')
	return -1;		/* invalid representation */
    part = tempstr + i;		/* set 'part' to start of highlight string */

    /* trim trailing white space from highlight string */
    i = (int) strlen(part) - 1;
    while (isblank(UCH(part[i])) && i > 0)
	i--;
    part[i + 1] = '\0';

    if (!dlg_strcmp(part, "ON"))
	*hl = TRUE;
    else if (!dlg_strcmp(part, "OFF"))
	*hl = FALSE;
    else
	return -1;		/* invalid highlight value */

    return 0;
}
#endif /* HAVE_COLOR */

/*
 * Check if the line begins with a special keyword; if so, return true while
 * pointing params to its parameters.
 */
static int
begins_with(char *line, const char *keyword, char **params)
{
    int i = skip_whitespace(line, 0);
    int j = skip_keyword(line, i);

    if ((j - i) == (int) strlen(keyword)) {
	char save = line[j];
	line[j] = 0;
	if (!dlg_strcmp(keyword, line + i)) {
	    *params = line + skip_whitespace(line, j + 1);
	    return 1;
	}
	line[j] = save;
    }

    return 0;
}

/*
 * Parse a line in the configuration file
 *
 * Each line is of the form:  "variable = value". On exit, 'var' will contain
 * the variable name, and 'value' will contain the value string.
 *
 * Return values:
 *
 * LINE_EMPTY   - line is blank or comment
 * LINE_EQUALS  - line contains "variable = value"
 * LINE_ERROR   - syntax error in line
 */
static PARSE_LINE
parse_line(char *line, char **var, char **value)
{
    int i = 0;

    /* ignore white space at beginning of line */
    i = skip_whitespace(line, i);

    if (line[i] == '\0')	/* line is blank */
	return LINE_EMPTY;
    else if (line[i] == '#')	/* line is comment */
	return LINE_EMPTY;
    else if (line[i] == '=')	/* variable names cannot start with a '=' */
	return LINE_ERROR;

    /* set 'var' to variable name */
    *var = line + i++;		/* skip to next character */

    /* find end of variable name */
    while (!isblank(UCH(line[i])) && line[i] != '=' && line[i] != '\0')
	i++;

    if (line[i] == '\0')	/* syntax error */
	return LINE_ERROR;
    else if (line[i] == '=')
	line[i++] = '\0';
    else {
	line[i++] = '\0';

	/* skip white space before '=' */
	i = skip_whitespace(line, i);

	if (line[i] != '=')	/* syntax error */
	    return LINE_ERROR;
	else
	    i++;		/* skip the '=' */
    }

    /* skip white space after '=' */
    i = skip_whitespace(line, i);

    if (line[i] == '\0')
	return LINE_ERROR;
    else
	*value = line + i;	/* set 'value' to value string */

    /* trim trailing white space from 'value' */
    i = (int) strlen(*value) - 1;
    while (isblank(UCH((*value)[i])) && i > 0)
	i--;
    (*value)[i + 1] = '\0';

    return LINE_EQUALS;		/* no syntax error in line */
}

/*
 * Create the configuration file
 */
void
dlg_create_rc(const char *filename)
{
    unsigned i;
    FILE *rc_file;

    if ((rc_file = fopen(filename, "wt")) == NULL)
	dlg_exiterr("Error opening file for writing in dlg_create_rc().");

    fprintf(rc_file, "#\n\
# Run-time configuration file for dialog\n\
#\n\
# Automatically generated by \"dialog --create-rc <file>\"\n\
#\n\
#\n\
# Types of values:\n\
#\n\
# Number     -  <number>\n\
# String     -  \"string\"\n\
# Boolean    -  <ON|OFF>\n"
#ifdef HAVE_COLOR
	    "\
# Attribute  -  (foreground,background,highlight?)\n"
#endif
	);

    /* Print an entry for each configuration variable */
    for (i = 0; i < VAR_COUNT; i++) {
	fprintf(rc_file, "\n# %s\n", vars[i].comment);
	switch (vars[i].type) {
	case VAL_INT:
	    fprintf(rc_file, "%s = %d\n", vars[i].name,
		    *((int *) vars[i].var));
	    break;
	case VAL_STR:
	    fprintf(rc_file, "%s = \"%s\"\n", vars[i].name,
		    (char *) vars[i].var);
	    break;
	case VAL_BOOL:
	    fprintf(rc_file, "%s = %s\n", vars[i].name,
		    *((bool *) vars[i].var) ? "ON" : "OFF");
	    break;
	}
    }
#ifdef HAVE_COLOR
    for (i = 0; i < (unsigned) dlg_color_count(); ++i) {
	char buffer[MAX_LEN + 1];
	unsigned j;
	bool repeat = FALSE;

	fprintf(rc_file, "\n# %s\n", dlg_color_table[i].comment);
	for (j = 0; j != i; ++j) {
	    if (dlg_color_table[i].fg == dlg_color_table[j].fg
		&& dlg_color_table[i].bg == dlg_color_table[j].bg
		&& dlg_color_table[i].hilite == dlg_color_table[j].hilite) {
		fprintf(rc_file, "%s = %s\n",
			dlg_color_table[i].name,
			dlg_color_table[j].name);
		repeat = TRUE;
		break;
	    }
	}

	if (!repeat) {
	    fprintf(rc_file, "%s = %s\n", dlg_color_table[i].name,
		    attr_to_str(buffer,
				dlg_color_table[i].fg,
				dlg_color_table[i].bg,
				dlg_color_table[i].hilite));
	}
    }
#endif /* HAVE_COLOR */
    dlg_dump_keys(rc_file);

    (void) fclose(rc_file);
}

/*
 * Parse the configuration file and set up variables
 */
int
dlg_parse_rc(void)
{
    int i;
    int l = 1;
    PARSE_LINE parse;
    char str[MAX_LEN + 1];
    char *var;
    char *value;
    char *tempptr;
    int result = 0;
    FILE *rc_file = 0;
    char *params;

    /*
     *  At startup, dialog determines the settings to use as follows:
     *
     *  a) if the environment variable $DIALOGRC is set, its value determines
     *     the name of the configuration file.
     *
     *  b) if the file in (a) can't be found, use the file $HOME/.dialogrc
     *     as the configuration file.
     *
     *  c) if the file in (b) can't be found, try using the GLOBALRC file.
     *     Usually this will be /etc/dialogrc.
     *
     *  d) if the file in (c) cannot be found, use the compiled-in defaults.
     */

    /* try step (a) */
    if ((tempptr = getenv("DIALOGRC")) != NULL)
	rc_file = fopen(tempptr, "rt");

    if (rc_file == NULL) {	/* step (a) failed? */
	/* try step (b) */
	if ((tempptr = getenv("HOME")) != NULL
	    && strlen(tempptr) < MAX_LEN - (sizeof(DIALOGRC) + 3)) {
	    if (tempptr[0] == '\0' || lastch(tempptr) == '/')
		sprintf(str, "%s%s", tempptr, DIALOGRC);
	    else
		sprintf(str, "%s/%s", tempptr, DIALOGRC);
	    rc_file = fopen(tempptr = str, "rt");
	}
    }

    if (rc_file == NULL) {	/* step (b) failed? */
	/* try step (c) */
	strcpy(str, GLOBALRC);
	if ((rc_file = fopen(tempptr = str, "rt")) == NULL)
	    return 0;		/* step (c) failed, use default values */
    }

    DLG_TRACE(("# opened rc file \"%s\"\n", tempptr));
    /* Scan each line and set variables */
    while ((result == 0) && (fgets(str, MAX_LEN, rc_file) != NULL)) {
	DLG_TRACE(("#\t%s", str));
	if (*str == '\0' || lastch(str) != '\n') {
	    /* ignore rest of file if line too long */
	    fprintf(stderr, "\nParse error: line %d of configuration"
		    " file too long.\n", l);
	    result = -1;	/* parse aborted */
	    break;
	}

	lastch(str) = '\0';
	if (begins_with(str, "bindkey", &params)) {
	    if (!dlg_parse_bindkey(params)) {
		fprintf(stderr, "\nParse error: line %d of configuration\n", l);
		result = -1;
	    }
	    continue;
	}
	parse = parse_line(str, &var, &value);	/* parse current line */

	switch (parse) {
	case LINE_EMPTY:	/* ignore blank lines and comments */
	    break;
	case LINE_EQUALS:
	    /* search table for matching config variable name */
	    if ((i = find_vars(var)) >= 0) {
		switch (vars[i].type) {
		case VAL_INT:
		    *((int *) vars[i].var) = atoi(value);
		    break;
		case VAL_STR:
		    if (!isquote(value[0]) || !isquote(lastch(value))
			|| strlen(value) < 2) {
			fprintf(stderr, "\nParse error: string value "
				"expected at line %d of configuration "
				"file.\n", l);
			result = -1;	/* parse aborted */
		    } else {
			/* remove the (") quotes */
			value++;
			lastch(value) = '\0';
			strcpy((char *) vars[i].var, value);
		    }
		    break;
		case VAL_BOOL:
		    if (!dlg_strcmp(value, "ON"))
			*((bool *) vars[i].var) = TRUE;
		    else if (!dlg_strcmp(value, "OFF"))
			*((bool *) vars[i].var) = FALSE;
		    else {
			fprintf(stderr, "\nParse error: boolean value "
				"expected at line %d of configuration "
				"file (found %s).\n", l, value);
			result = -1;	/* parse aborted */
		    }
		    break;
		}
#ifdef HAVE_COLOR
	    } else if ((i = find_color(var)) >= 0) {
		int fg = 0;
		int bg = 0;
		int hl = 0;
		if (str_to_attr(value, &fg, &bg, &hl) == -1) {
		    fprintf(stderr, "\nParse error: attribute "
			    "value expected at line %d of configuration "
			    "file.\n", l);
		    result = -1;	/* parse aborted */
		} else {
		    dlg_color_table[i].fg = fg;
		    dlg_color_table[i].bg = bg;
		    dlg_color_table[i].hilite = hl;
		}
	    } else {
#endif /* HAVE_COLOR */
		fprintf(stderr, "\nParse error: unknown variable "
			"at line %d of configuration file:\n\t%s\n", l, var);
		result = -1;	/* parse aborted */
	    }
	    break;
	case LINE_ERROR:
	    fprintf(stderr, "\nParse error: syntax error at line %d of "
		    "configuration file.\n", l);
	    result = -1;	/* parse aborted */
	    break;
	}
	l++;			/* next line */
    }

    (void) fclose(rc_file);
    return result;
}
