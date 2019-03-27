/*
 * $Id: dialog.c,v 1.268 2018/06/21 09:16:05 tom Exp $
 *
 *  cdialog - Display simple dialog boxes from shell scripts
 *
 *  Copyright 2000-2017,2018	Thomas E. Dickey
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#define PASSARGS             t,       av,        offset_add
#define CALLARGS const char *t, char *av[], int *offset_add
typedef int (callerFn) (CALLARGS);

typedef enum {
    o_unknown = 0
    ,o_allow_close
    ,o_and_widget
    ,o_ascii_lines
    ,o_aspect
    ,o_auto_placement
    ,o_backtitle
    ,o_beep
    ,o_beep_after
    ,o_begin
    ,o_cancel_label
    ,o_checklist
    ,o_clear
    ,o_colors
    ,o_column_separator
    ,o_cr_wrap
    ,o_create_rc
    ,o_date_format
    ,o_default_button
    ,o_default_item
    ,o_defaultno
    ,o_exit_label
    ,o_extra_button
    ,o_extra_label
    ,o_fixed_font
    ,o_form
    ,o_gauge
    ,o_help
    ,o_help_button
    ,o_help_file
    ,o_help_label
    ,o_help_line
    ,o_help_status
    ,o_help_tags
    ,o_icon
    ,o_ignore
    ,o_infobox
    ,o_input_fd
    ,o_inputbox
    ,o_inputmenu
    ,o_insecure
    ,o_item_help
    ,o_keep_colors
    ,o_keep_tite
    ,o_keep_window
    ,o_last_key
    ,o_max_input
    ,o_menu
    ,o_mixedform
    ,o_mixedgauge
    ,o_msgbox
    ,o_no_close
    ,o_no_collapse
    ,o_no_cr_wrap
    ,o_no_kill
    ,o_no_label
    ,o_no_lines
    ,o_no_mouse
    ,o_no_nl_expand
    ,o_no_shadow
    ,o_nocancel
    ,o_nook
    ,o_ok_label
    ,o_output_fd
    ,o_output_separator
    ,o_passwordbox
    ,o_passwordform
    ,o_pause
    ,o_prgbox
    ,o_print_maxsize
    ,o_print_size
    ,o_print_text_only
    ,o_print_text_size
    ,o_print_version
    ,o_programbox
    ,o_progressbox
    ,o_quoted
    ,o_radiolist
    ,o_screen_center
    ,o_scrollbar
    ,o_separate_output
    ,o_separate_widget
    ,o_separator
    ,o_shadow
    ,o_single_quoted
    ,o_size_err
    ,o_sleep
    ,o_smooth
    ,o_stderr
    ,o_stdout
    ,o_tab_correct
    ,o_tab_len
    ,o_tailbox
    ,o_tailboxbg
    ,o_textbox
    ,o_time_format
    ,o_timeout
    ,o_title
    ,o_trim
    ,o_under_mouse
    ,o_version
    ,o_visit_items
    ,o_wmclass
    ,o_yes_label
    ,o_yesno
#ifdef HAVE_WHIPTAIL
    ,o_fullbutton
    ,o_topleft
#endif
#ifdef HAVE_XDIALOG
    ,o_calendar
    ,o_dselect
    ,o_editbox
    ,o_fselect
    ,o_timebox
    ,o_week_start
#endif
#ifdef HAVE_XDIALOG2
    ,o_buildlist
    ,o_rangebox
    ,o_reorder
    ,o_treeview
#endif
#if defined(HAVE_XDIALOG2) || defined(HAVE_WHIPTAIL)
    ,o_no_items
    ,o_no_tags
#endif
#ifdef HAVE_DLG_TRACE
    ,o_trace
#endif
    ,o_iso_week
} eOptions;

/*
 * The bits in 'pass' are used to decide which options are applicable at
 * different stages in the program:
 *	1 flags before widgets
 *	2 widgets
 *	4 non-widget options
 */
typedef struct {
    const char *name;
    eOptions code;
    int pass;			/* 1,2,4 or combination */
    const char *help;		/* NULL to suppress, non-empty to display params */
} Options;

typedef struct {
    eOptions code;
    int argmin, argmax;
    callerFn *jumper;
} Mode;

static int known_opts = 0;
static const char **dialog_opts;
static char **dialog_argv;

static char **special_argv = 0;
static int special_argc = 0;

static bool ignore_unknown = FALSE;

static const char *program = "dialog";

#ifdef NO_LEAKS
typedef struct _all_blobs {
    struct _all_blobs *next;
    void *blob;
} AllBlobs;

static AllBlobs *all_blobs;
#endif

/*
 * The options[] table is organized this way to make it simple to maintain
 * a sorted list of options for the help-message.
 */
/* *INDENT-OFF* */
static const Options options[] = {
    { "allow-close",	o_allow_close,		1, NULL },
    { "and-widget",	o_and_widget,		4, NULL },
    { "ascii-lines",	o_ascii_lines, 		1, "" },
    { "aspect",		o_aspect,		1, "<ratio>" },
    { "auto-placement", o_auto_placement,	1, NULL },
    { "backtitle",	o_backtitle,		1, "<backtitle>" },
    { "beep",		o_beep,			1, "" },
    { "beep-after",	o_beep_after,		1, "" },
    { "begin",		o_begin,		1, "<y> <x>" },
    { "cancel-label",	o_cancel_label,		1, "<str>" },
    { "checklist",	o_checklist,		2, "<text> <height> <width> <list height> <tag1> <item1> <status1>..." },
    { "clear",		o_clear,		1, "" },
    { "colors",		o_colors,		1, "" },
    { "column-separator",o_column_separator,	1, "<str>" },
    { "cr-wrap",	o_cr_wrap,		1, "" },
    { "create-rc",	o_create_rc,		1, NULL },
    { "date-format",	o_date_format,		1, "<str>" },
    { "default-button",	o_default_button,	1, "<str>" },
    { "default-item",	o_default_item,		1, "<str>" },
    { "defaultno",	o_defaultno,		1, "" },
    { "exit-label",	o_exit_label,		1, "<str>" },
    { "extra-button",	o_extra_button,		1, "" },
    { "extra-label",	o_extra_label,		1, "<str>" },
    { "fixed-font",	o_fixed_font,		1, NULL },
    { "form",		o_form,			2, "<text> <height> <width> <form height> <label1> <l_y1> <l_x1> <item1> <i_y1> <i_x1> <flen1> <ilen1>..." },
    { "gauge",		o_gauge,		2, "<text> <height> <width> [<percent>]" },
    { "guage",		o_gauge,		2, NULL },
    { "help",		o_help,			4, "" },
    { "help-button",	o_help_button,		1, "" },
    { "help-label",	o_help_label,		1, "<str>" },
    { "help-status",	o_help_status,		1, "" },
    { "help-tags",	o_help_tags,		1, "" },
    { "hfile",		o_help_file,		1, "<str>" },
    { "hline",		o_help_line,		1, "<str>" },
    { "icon",		o_icon,			1, NULL },
    { "ignore",		o_ignore,		1, "" },
    { "infobox",	o_infobox,		2, "<text> <height> <width>" },
    { "input-fd",	o_input_fd,		1, "<fd>" },
    { "inputbox",	o_inputbox,		2, "<text> <height> <width> [<init>]" },
    { "inputmenu",	o_inputmenu,		2, "<text> <height> <width> <menu height> <tag1> <item1>..." },
    { "insecure",	o_insecure,		1, "" },
    { "item-help",	o_item_help,		1, "" },
    { "keep-colors",	o_keep_colors,		1, NULL },
    { "keep-tite",	o_keep_tite,		1, "" },
    { "keep-window",	o_keep_window,		1, "" },
    { "last-key",	o_last_key,		1, "" },
    { "max-input",	o_max_input,		1, "<n>" },
    { "menu",		o_menu,			2, "<text> <height> <width> <menu height> <tag1> <item1>..." },
    { "mixedform",	o_mixedform,		2, "<text> <height> <width> <form height> <label1> <l_y1> <l_x1> <item1> <i_y1> <i_x1> <flen1> <ilen1> <itype>..." },
    { "mixedgauge",	o_mixedgauge,		2, "<text> <height> <width> <percent> <tag1> <item1>..." },
    { "msgbox",		o_msgbox,		2, "<text> <height> <width>" },
    { "no-cancel",	o_nocancel,		1, "" },
    { "no-close",	o_no_close,		1, NULL },
    { "no-collapse",	o_no_collapse,		1, "" },
    { "no-cr-wrap",	o_no_cr_wrap,		1, "" },
    { "no-kill",	o_no_kill,		1, "" },
    { "no-label",	o_no_label,		1, "<str>" },
    { "no-lines",	o_no_lines, 		1, "" },
    { "no-mouse",	o_no_mouse,		1, "" },
    { "no-nl-expand",	o_no_nl_expand,		1, "" },
    { "no-ok",		o_nook,			1, "" },
    { "no-shadow",	o_no_shadow,		1, "" },
    { "nocancel",	o_nocancel,		1, NULL }, /* see --no-cancel */
    { "nook",		o_nook,			1, "" }, /* See no-ok */
    { "ok-label",	o_ok_label,		1, "<str>" },
    { "output-fd",	o_output_fd,		1, "<fd>" },
    { "output-separator",o_output_separator,	1, "<str>" },
    { "passwordbox",	o_passwordbox,		2, "<text> <height> <width> [<init>]" },
    { "passwordform",	o_passwordform,		2, "<text> <height> <width> <form height> <label1> <l_y1> <l_x1> <item1> <i_y1> <i_x1> <flen1> <ilen1>..." },
    { "pause",		o_pause,		2, "<text> <height> <width> <seconds>" },
    { "prgbox",		o_prgbox,		2, "<text> <command> <height> <width>" },
    { "print-maxsize",	o_print_maxsize,	1, "" },
    { "print-size",	o_print_size,		1, "" },
    { "print-text-only",o_print_text_only,	5, "<text> <height> <width>" },
    { "print-text-size",o_print_text_size,	5, "<text> <height> <width>" },
    { "print-version",	o_print_version,	5, "" },
    { "programbox",	o_programbox,		2, "<text> <height> <width>" },
    { "progressbox",	o_progressbox,		2, "<text> <height> <width>" },
    { "quoted",		o_quoted,		1, "" },
    { "radiolist",	o_radiolist,		2, "<text> <height> <width> <list height> <tag1> <item1> <status1>..." },
    { "screen-center",	o_screen_center,	1, NULL },
    { "scrollbar",	o_scrollbar,		1, "" },
    { "separate-output",o_separate_output,	1, "" },
    { "separate-widget",o_separate_widget,	1, "<str>" },
    { "separator",	o_separator,		1, NULL },
    { "shadow",		o_shadow,		1, "" },
    { "single-quoted",	o_single_quoted,	1, "" },
    { "size-err",	o_size_err,		1, "" },
    { "sleep",		o_sleep,		1, "<secs>" },
    { "smooth",		o_smooth,		1, NULL },
    { "stderr",		o_stderr,		1, "" },
    { "stdout",		o_stdout,		1, "" },
    { "tab-correct",	o_tab_correct,		1, "" },
    { "tab-len",	o_tab_len,		1, "<n>" },
    { "tailbox",	o_tailbox,		2, "<file> <height> <width>" },
    { "tailboxbg",	o_tailboxbg,		2, "<file> <height> <width>" },
    { "textbox",	o_textbox,		2, "<file> <height> <width>" },
    { "time-format",	o_time_format,		1, "<str>" },
    { "timeout",	o_timeout,		1, "<secs>" },
    { "title",		o_title,		1, "<title>" },
    { "trim",		o_trim,			1, "" },
    { "under-mouse", 	o_under_mouse,		1, NULL },
    { "version",	o_version,		5, "" },
    { "visit-items", 	o_visit_items,		1, "" },
    { "wmclass",	o_wmclass,		1, NULL },
    { "yes-label",	o_yes_label,		1, "<str>" },
    { "yesno",		o_yesno,		2, "<text> <height> <width>" },
#ifdef HAVE_WHIPTAIL
    { "cancel-button",	o_cancel_label,		1, NULL },
    { "fb",		o_fullbutton,		1, NULL },
    { "fullbutton",	o_fullbutton,		1, NULL },
    { "no-button",	o_no_label,		1, NULL },
    { "ok-button",	o_ok_label,		1, NULL },
    { "scrolltext",	o_scrollbar,		1, NULL },
    { "topleft",	o_topleft,		1, NULL },
    { "yes-button",	o_yes_label,		1, NULL },
#endif
#ifdef HAVE_XDIALOG
    { "calendar",	o_calendar,		2, "<text> <height> <width> <day> <month> <year>" },
    { "dselect",	o_dselect,		2, "<directory> <height> <width>" },
    { "editbox",	o_editbox,		2, "<file> <height> <width>" },
    { "fselect",	o_fselect,		2, "<filepath> <height> <width>" },
    { "timebox",	o_timebox,		2, "<text> <height> <width> <hour> <minute> <second>" },
    { "week-start",	o_week_start,		1, "<str>" },
    { "iso-week",	o_iso_week,		1, NULL },
#endif
#ifdef HAVE_XDIALOG2
    { "buildlist",	o_buildlist,		2, "<text> <height> <width> <list-height> <tag1> <item1> <status1>..." },
    { "no-items", 	o_no_items,		1, "" },
    { "no-tags", 	o_no_tags,		1, "" },
    { "rangebox",	o_rangebox,		2, "<text> <height> <width> <min-value> <max-value> <default-value>" },
    { "reorder", 	o_reorder,		1, "" },
    { "treeview",	o_treeview,		2, "<text> <height> <width> <list-height> <tag1> <item1> <status1> <depth1>..." },
#endif
#if defined(HAVE_XDIALOG2) || defined(HAVE_WHIPTAIL)
    { "noitem", 	o_no_items,		1, NULL },
    { "notags", 	o_no_tags,		1, NULL },
#endif
#ifdef HAVE_DLG_TRACE
    { "trace",		o_trace,		1, "<file>" },
#endif
};
/* *INDENT-ON* */

#ifdef NO_LEAKS
static void
ignore_leak(void *value)
{
    AllBlobs *next = dlg_calloc(AllBlobs, (size_t) 1);
    if (next != 0) {
	next->blob = value;
	next->next = all_blobs;
	all_blobs = next;
    }
}

static void
handle_leaks(void)
{
    while (all_blobs != 0) {
	char *blob = all_blobs->blob;
	AllBlobs *next = all_blobs->next;
	free(blob);
	free(all_blobs);
	all_blobs = next;
    }
    free(dialog_opts);
    if (special_argv != 0) {
	free(special_argv[0]);
	free(special_argv);
	special_argv = 0;
	special_argc = 0;
    }
}
#else
#define handle_leaks()		/* nothing */
#define ignore_leak(n)		/* nothing */
#endif

#define OptionChars "\
0123456789\
-\
abcdefghijklmnopqrstuvwxyz\
"

/*
 * Check if the given string from main's argv is an option.
 */
static bool
isOption(const char *arg)
{
    bool result = FALSE;

    if (arg != 0) {
	if (dialog_opts != 0) {
	    int n;
	    for (n = 0; dialog_opts[n] != 0; ++n) {
		if (dialog_opts[n] == arg) {
		    result = TRUE;
		    break;
		}
	    }
	} else if (!strncmp(arg, "--", (size_t) 2) && isalpha(UCH(arg[2]))) {
	    if (strlen(arg) == (strspn) (arg, OptionChars)) {
		result = TRUE;
	    } else {
		handle_leaks();
		dlg_exiterr("Invalid option \"%s\"", arg);
	    }
	}
    }
    return result;
}

/*
 * Make an array showing which argv[] entries are options.  Use "--" as a
 * special token to escape the next argument, allowing it to begin with "--".
 * When we find a "--" argument, also remove it from argv[] and adjust argc.
 * That appears to be an undocumented feature of the popt library.
 *
 * Also, if we see a "--file", expand it into the parameter list by reading the
 * text from the given file and stripping quotes, treating whitespace outside
 * quotes as a parameter delimiter.
 *
 * Finally, if we see a "--args", dump the current list of arguments to the
 * standard error.  This is used for debugging complex --file combinations.
 */
static void
unescape_argv(int *argcp, char ***argvp)
{
    int j, k;
    int limit_includes = 20 + *argcp;
    int count_includes = 0;
    bool doalloc = FALSE;
    char *filename;
    const char **my_argv = 0;
    int my_argc;

    DLG_TRACE(("# unescape_argv\n"));
    for (k = 0; k < 2; ++k) {

	my_argc = 0;
	if (special_argv != 0) {
	    for (j = 0; special_argv[j] != 0; ++j) {
		if (!strcmp(special_argv[j], "--")) {
		    break;
		} else if (isOption(special_argv[j])) {
		    if (k != 0)
			my_argv[my_argc] = special_argv[j];
		    my_argc++;
		}
	    }
	}

	if (k == 0) {
	    my_argc += (*argcp + 1);
	    my_argv = dlg_calloc(const char *, (size_t) my_argc);
	    assert_ptr(my_argv, "unescape_argv");
	}
    }

    for (j = 1; j < *argcp; j++) {
	bool escaped = FALSE;
	if (!strcmp((*argvp)[j], "--")) {
	    escaped = TRUE;
	    dlg_eat_argv(argcp, argvp, j, 1);
	} else if (!strcmp((*argvp)[j], "--args")) {
	    fprintf(stderr, "Showing arguments at arg%d\n", j);
	    for (k = 0; k < *argcp; ++k) {
		fprintf(stderr, " arg%d:%s\n", k, (*argvp)[k]);
	    }
	    dlg_eat_argv(argcp, argvp, j, 1);
	    --j;
	} else if (!strcmp((*argvp)[j], "--file")) {
	    if (++count_includes > limit_includes) {
		handle_leaks();
		dlg_exiterr("Too many --file options");
	    }

	    if ((filename = (*argvp)[j + 1]) != 0) {
		FILE *fp;
		char **list;
		char *blob;
		int added;
		size_t bytes_read;
		size_t length;
		int n;

		if (*filename == '&') {
		    fp = fdopen(atoi(filename + sizeof(char)), "r");
		} else {
		    fp = fopen(filename, "r");
		}

		if (fp) {
		    DLG_TRACE(("# opened --file %s ..\n", filename));
		    blob = NULL;
		    length = 0;
		    do {
			blob = dlg_realloc(char, length + BUFSIZ + 1, blob);
			assert_ptr(blob, "unescape_argv");
			bytes_read = fread(blob + length,
					   sizeof(char),
					     (size_t) BUFSIZ,
					   fp);
			length += bytes_read;
			if (ferror(fp)) {
			    handle_leaks();
			    dlg_exiterr("error on filehandle in unescape_argv");
			}
		    } while (bytes_read == BUFSIZ);
		    fclose(fp);

		    blob[length] = '\0';
		    ignore_leak(blob);

		    list = dlg_string_to_argv(blob);
		    added = dlg_count_argv(list);
		    if (added > 2) {
			/* *argcp arguments before the expansion of --file
			   - 2 for the removal of '--file <filepath>'
			   + added for the arguments contained in <filepath>
			   + 1 for the terminating NULL pointer */
			size_t need = (size_t) (*argcp + added - 1);
			if (doalloc) {
			    *argvp = dlg_realloc(char *, need, *argvp);
			    assert_ptr(*argvp, "unescape_argv");
			} else {
			    char **newp = dlg_malloc(char *, need);
			    ignore_leak(newp);
			    assert_ptr(newp, "unescape_argv");
			    for (n = 0; n < *argcp; ++n) {
				newp[n] = (*argvp)[n];
			    }
			    /* The new array is not NULL-terminated yet. */
			    *argvp = newp;
			    doalloc = TRUE;
			}
			my_argv = dlg_realloc(const char *, need, my_argv);
			assert_ptr(my_argv, "unescape_argv");

			/* Shift the arguments after '--file <filepath>'
			   right by (added - 2) positions */
			for (n = *argcp - 1; n >= j + 2; --n) {
			    (*argvp)[n + added - 2] = (*argvp)[n];
			}
		    } else if (added < 2) {
			/* 0 or 1 argument read from the included file
			   -> shift the arguments after '--file <filepath>'
			   left by (2 - added) positions */
			for (n = j + added; n + 2 - added < *argcp; ++n) {
			    (*argvp)[n] = (*argvp)[n + 2 - added];
			}
		    }
		    /* Copy the inserted arguments to *argvp */
		    for (n = 0; n < added; ++n) {
			(*argvp)[n + j] = list[n];
		    }
		    *argcp += added - 2;
		    (*argvp)[*argcp] = 0;	/* Write the NULL terminator */
		    free(list);	/* No-op if 'list' is NULL */
		    /* Force rescan starting from the first inserted argument */
		    --j;
		    DLG_TRACE(("# finished --file\n"));
		    continue;
		} else {
		    handle_leaks();
		    dlg_exiterr("Cannot open --file %s", filename);
		}
	    } else {
		handle_leaks();
		dlg_exiterr("No value given for --file");
	    }
	}
	if (!escaped
	    && (*argvp)[j] != 0
	    && !strncmp((*argvp)[j], "--", (size_t) 2)
	    && isalpha(UCH((*argvp)[j][2]))) {
	    my_argv[my_argc++] = (*argvp)[j];
	    DLG_TRACE(("#\toption argv[%d]=%s\n", j, (*argvp)[j]));
	}
    }

    my_argv[my_argc] = 0;

    known_opts = my_argc;
    dialog_opts = my_argv;

    DLG_TRACE(("#\t%d options vs %d arguments\n", known_opts, *argcp));
    dialog_argv = (*argvp);
}

static eOptions
lookupOption(const char *name, int pass)
{
    unsigned n;
    eOptions result = o_unknown;

    if (isOption(name)) {
	name += 2;
	for (n = 0; n < sizeof(options) / sizeof(options[0]); n++) {
	    if ((pass & options[n].pass) != 0
		&& !strcmp(name, options[n].name)) {
		result = options[n].code;
		break;
	    }
	}
    }
    return result;
}

static void
Usage(const char *msg)
{
    handle_leaks();
    dlg_exiterr("Error: %s.\nUse --help to list options.\n\n", msg);
}

/*
 * Count arguments, stopping at the end of the argument list, or on any of our
 * "--" tokens.
 */
static int
arg_rest(char *argv[])
{
    int i = 1;			/* argv[0] points to a "--" token */

    while (argv[i] != 0
	   && (!isOption(argv[i]) || lookupOption(argv[i], 7) == o_unknown))
	i++;
    return i;
}

/*
 * In MultiWidget this function is needed to count how many tags
 * a widget (menu, checklist, radiolist) has
 */
static int
howmany_tags(char *argv[], int group)
{
    int result = 0;
    int have;
    char temp[80];

    while (argv[0] != 0) {
	if (isOption(argv[0]))
	    break;
	if ((have = arg_rest(argv)) < group) {
	    const char *format = _("Expected %d arguments, found only %d");
	    sprintf(temp, format, group, have);
	    Usage(temp);
	} else if ((have % group) != 0) {
	    const char *format = _("Expected %d arguments, found extra %d");
	    sprintf(temp, format, group, (have % group));
	    Usage(temp);
	}

	argv += have;
	result += (have / group);
    }

    return result;
}

static int
numeric_arg(char **av, int n)
{
    int result = 0;

    if (n < dlg_count_argv(av)) {
	char msg[80];
	char *last = 0;
	result = (int) strtol(av[n], &last, 10);

	if (last == 0 || *last != 0) {
	    sprintf(msg, "Expected a number for token %d of %.20s", n, av[0]);
	    Usage(msg);
	}
    }
    return result;
}

static char *
optional_str(char **av, int n, char *dft)
{
    char *ret = dft;
    if (arg_rest(av) > n)
	ret = av[n];
    return ret;
}

#if defined(HAVE_DLG_GAUGE) || defined(HAVE_XDIALOG)
static int
optional_num(char **av, int n, int dft)
{
    int ret = dft;
    if (arg_rest(av) > n)
	ret = numeric_arg(av, n);
    return ret;
}
#endif

/*
 * On AIX 4.x, we have to flush the output right away since there is a bug in
 * the curses package which discards stdout even when we've used newterm to
 * redirect output to /dev/tty.
 */
static int
show_result(int ret)
{
    bool either = FALSE;

    switch (ret) {
    case DLG_EXIT_OK:
    case DLG_EXIT_EXTRA:
    case DLG_EXIT_HELP:
    case DLG_EXIT_ITEM_HELP:
	if ((dialog_state.output_count > 1) && !dialog_vars.separate_output) {
	    fputs((dialog_state.separate_str
		   ? dialog_state.separate_str
		   : DEFAULT_SEPARATE_STR),
		  dialog_state.output);
	    either = TRUE;
	}
	if (dialog_vars.input_result != 0
	    && dialog_vars.input_result[0] != '\0') {
	    fputs(dialog_vars.input_result, dialog_state.output);
	    DLG_TRACE(("# input_result:\n%s\n", dialog_vars.input_result));
	    either = TRUE;
	}
	if (either) {
	    fflush(dialog_state.output);
	}
	break;
    }
    return ret;
}

/*
 * These are the widget callers.
 */

static int
call_yesno(CALLARGS)
{
    *offset_add = 4;
    return dialog_yesno(t,
			av[1],
			numeric_arg(av, 2),
			numeric_arg(av, 3));
}

static int
call_msgbox(CALLARGS)
{
    *offset_add = 4;
    return dialog_msgbox(t,
			 av[1],
			 numeric_arg(av, 2),
			 numeric_arg(av, 3), 1);
}

static int
call_infobox(CALLARGS)
{
    *offset_add = 4;
    return dialog_msgbox(t,
			 av[1],
			 numeric_arg(av, 2),
			 numeric_arg(av, 3), 0);
}

static int
call_textbox(CALLARGS)
{
    *offset_add = 4;
    return dialog_textbox(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3));
}

static int
call_menu(CALLARGS)
{
    int tags = howmany_tags(av + 5, MENUBOX_TAGS);
    *offset_add = 5 + tags * MENUBOX_TAGS;

    return dialog_menu(t,
		       av[1],
		       numeric_arg(av, 2),
		       numeric_arg(av, 3),
		       numeric_arg(av, 4),
		       tags, av + 5);
}

static int
call_inputmenu(CALLARGS)
{
    int tags = howmany_tags(av + 5, MENUBOX_TAGS);
    bool free_extra_label = FALSE;
    int result;

    dialog_vars.input_menu = TRUE;

    if (dialog_vars.max_input == 0)
	dialog_vars.max_input = MAX_LEN / 2;

    if (dialog_vars.extra_label == 0) {
	free_extra_label = TRUE;
	dialog_vars.extra_label = dlg_strclone(_("Rename"));
    }

    dialog_vars.extra_button = TRUE;

    *offset_add = 5 + tags * MENUBOX_TAGS;
    result = dialog_menu(t,
			 av[1],
			 numeric_arg(av, 2),
			 numeric_arg(av, 3),
			 numeric_arg(av, 4),
			 tags, av + 5);
    if (free_extra_label) {
	free(dialog_vars.extra_label);
	dialog_vars.extra_label = 0;
    }
    return result;
}

static int
call_checklist(CALLARGS)
{
    int tags = howmany_tags(av + 5, CHECKBOX_TAGS);
    int code;

    *offset_add = 5 + tags * CHECKBOX_TAGS;
    code = dialog_checklist(t,
			    av[1],
			    numeric_arg(av, 2),
			    numeric_arg(av, 3),
			    numeric_arg(av, 4),
			    tags, av + 5, FLAG_CHECK);
    return code;
}

static int
call_radiolist(CALLARGS)
{
    int tags = howmany_tags(av + 5, CHECKBOX_TAGS);
    *offset_add = 5 + tags * CHECKBOX_TAGS;
    return dialog_checklist(t,
			    av[1],
			    numeric_arg(av, 2),
			    numeric_arg(av, 3),
			    numeric_arg(av, 4),
			    tags, av + 5, FLAG_RADIO);
}

static int
call_inputbox(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_inputbox(t,
			   av[1],
			   numeric_arg(av, 2),
			   numeric_arg(av, 3),
			   optional_str(av, 4, 0), 0);
}

static int
call_passwordbox(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_inputbox(t,
			   av[1],
			   numeric_arg(av, 2),
			   numeric_arg(av, 3),
			   optional_str(av, 4, 0), 1);
}

#ifdef HAVE_XDIALOG
static int
call_calendar(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_calendar(t,
			   av[1],
			   numeric_arg(av, 2),
			   numeric_arg(av, 3),
			   optional_num(av, 4, -1),
			   optional_num(av, 5, -1),
			   optional_num(av, 6, -1));
}

static int
call_dselect(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_dselect(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3));
}

static int
call_editbox(CALLARGS)
{
    *offset_add = 4;
    return dialog_editbox(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3));
}

static int
call_fselect(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_fselect(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3));
}

static int
call_timebox(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_timebox(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3),
			  optional_num(av, 4, -1),
			  optional_num(av, 5, -1),
			  optional_num(av, 6, -1));
}
#endif /* HAVE_XDIALOG */

/* dialog 1.2 widgets */
#ifdef HAVE_XDIALOG2

#define DisableNoTags() \
	bool save_no_tags = dialog_vars.no_tags; \
	bool save_no_items = dialog_vars.no_items; \
	dialog_vars.no_tags = TRUE; \
	dialog_vars.no_items = FALSE

#define RestoreNoTags() \
	dialog_vars.no_tags = save_no_tags; \
	dialog_vars.no_items = save_no_items

static int
call_buildlist(CALLARGS)
{
    int tags = howmany_tags(av + 5, CHECKBOX_TAGS);
    int result;

    DisableNoTags();

    *offset_add = 5 + tags * CHECKBOX_TAGS;
    result = dialog_buildlist(t,
			      av[1],
			      numeric_arg(av, 2),
			      numeric_arg(av, 3),
			      numeric_arg(av, 4),
			      tags, av + 5,
			      dialog_vars.reorder);
    RestoreNoTags();
    return result;
}

static int
call_rangebox(CALLARGS)
{
    int min_value;

    *offset_add = arg_rest(av);
    min_value = numeric_arg(av, 4);
    return dialog_rangebox(t,
			   av[1],
			   numeric_arg(av, 2),
			   numeric_arg(av, 3),
			   min_value,
			   numeric_arg(av, 5),
			   (*offset_add > 6) ? numeric_arg(av, 6) : min_value);
}

static int
call_treeview(CALLARGS)
{
    int tags = howmany_tags(av + 5, TREEVIEW_TAGS);
    int result;

    DisableNoTags();

    *offset_add = arg_rest(av);
    result = dialog_treeview(t,
			     av[1],
			     numeric_arg(av, 2),
			     numeric_arg(av, 3),
			     numeric_arg(av, 4),
			     tags, av + 5, FLAG_RADIO);
    RestoreNoTags();
    return result;
}
#endif /* HAVE_XDIALOG */

#ifdef HAVE_DLG_FORMBOX
static int
call_form(CALLARGS)
{
    int group = FORMBOX_TAGS;
    int tags = howmany_tags(av + 5, group);
    *offset_add = 5 + tags * group;

    return dialog_form(t,
		       av[1],
		       numeric_arg(av, 2),
		       numeric_arg(av, 3),
		       numeric_arg(av, 4),
		       tags, av + 5);
}

static int
call_password_form(CALLARGS)
{
    unsigned save = dialog_vars.formitem_type;
    int result;

    dialog_vars.formitem_type = 1;
    result = call_form(PASSARGS);
    dialog_vars.formitem_type = save;

    return result;
}
#endif /* HAVE_DLG_FORMBOX */

#ifdef HAVE_DLG_MIXEDFORM
static int
call_mixed_form(CALLARGS)
{
    int group = MIXEDFORM_TAGS;
    int tags = howmany_tags(av + 5, group);
    *offset_add = 5 + tags * group;

    return dialog_mixedform(t,
			    av[1],
			    numeric_arg(av, 2),
			    numeric_arg(av, 3),
			    numeric_arg(av, 4),
			    tags, av + 5);
}
#endif /* HAVE_DLG_MIXEDFORM */

#ifdef HAVE_DLG_GAUGE
static int
call_gauge(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_gauge(t,
			av[1],
			numeric_arg(av, 2),
			numeric_arg(av, 3),
			optional_num(av, 4, 0));
}

static int
call_pause(CALLARGS)
{
    *offset_add = arg_rest(av);
    return dialog_pause(t,
			av[1],
			numeric_arg(av, 2),
			numeric_arg(av, 3),
			numeric_arg(av, 4));
}
#endif

#ifdef HAVE_MIXEDGAUGE
static int
call_mixed_gauge(CALLARGS)
{
#define MIXEDGAUGE_BASE 5
    int tags = howmany_tags(av + MIXEDGAUGE_BASE, MIXEDGAUGE_TAGS);
    *offset_add = MIXEDGAUGE_BASE + tags * MIXEDGAUGE_TAGS;
    return dialog_mixedgauge(t,
			     av[1],
			     numeric_arg(av, 2),
			     numeric_arg(av, 3),
			     numeric_arg(av, 4),
			     tags, av + MIXEDGAUGE_BASE);
}
#endif

#ifdef HAVE_DLG_GAUGE
static int
call_prgbox(CALLARGS)
{
    *offset_add = arg_rest(av);
    /* the original version does not accept a prompt string, but for
     * consistency we allow it.
     */
    return ((*offset_add == 5)
	    ? dialog_prgbox(t,
			    av[1],
			    av[2],
			    numeric_arg(av, 3),
			    numeric_arg(av, 4), TRUE)
	    : dialog_prgbox(t,
			    "",
			    av[1],
			    numeric_arg(av, 2),
			    numeric_arg(av, 3), TRUE));
}
#endif

#ifdef HAVE_DLG_GAUGE
static int
call_programbox(CALLARGS)
{
    int result;

    *offset_add = arg_rest(av);
    /* this function is a compromise between --prgbox and --progressbox.
     */
    result = ((*offset_add == 4)
	      ? dlg_progressbox(t,
				av[1],
				numeric_arg(av, 2),
				numeric_arg(av, 3),
				TRUE,
				dialog_state.pipe_input)
	      : dlg_progressbox(t,
				"",
				numeric_arg(av, 1),
				numeric_arg(av, 2),
				TRUE,
				dialog_state.pipe_input));
    dialog_state.pipe_input = 0;
    return result;
}
#endif

#ifdef HAVE_DLG_GAUGE
static int
call_progressbox(CALLARGS)
{
    *offset_add = arg_rest(av);
    /* the original version does not accept a prompt string, but for
     * consistency we allow it.
     */
    return ((*offset_add == 4)
	    ? dialog_progressbox(t,
				 av[1],
				 numeric_arg(av, 2),
				 numeric_arg(av, 3))
	    : dialog_progressbox(t,
				 "",
				 numeric_arg(av, 1),
				 numeric_arg(av, 2)));
}
#endif

#ifdef HAVE_DLG_TAILBOX
static int
call_tailbox(CALLARGS)
{
    *offset_add = 4;
    return dialog_tailbox(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3),
			  FALSE);
}

static int
call_tailboxbg(CALLARGS)
{
    *offset_add = 4;
    return dialog_tailbox(t,
			  av[1],
			  numeric_arg(av, 2),
			  numeric_arg(av, 3),
			  TRUE);
}
#endif
/* *INDENT-OFF* */
static const Mode modes[] =
{
    {o_yesno,           4, 4, call_yesno},
    {o_msgbox,          4, 4, call_msgbox},
    {o_infobox,         4, 4, call_infobox},
    {o_textbox,         4, 4, call_textbox},
    {o_menu,            6, 0, call_menu},
    {o_inputmenu,       6, 0, call_inputmenu},
    {o_checklist,       7, 0, call_checklist},
    {o_radiolist,       7, 0, call_radiolist},
    {o_inputbox,        4, 5, call_inputbox},
    {o_passwordbox,     4, 5, call_passwordbox},
#ifdef HAVE_DLG_GAUGE
    {o_gauge,           4, 5, call_gauge},
    {o_pause,           5, 5, call_pause},
    {o_prgbox,          4, 5, call_prgbox},
    {o_programbox,      3, 4, call_programbox},
    {o_progressbox,     3, 4, call_progressbox},
#endif
#ifdef HAVE_DLG_FORMBOX
    {o_passwordform,   13, 0, call_password_form},
    {o_form,           13, 0, call_form},
#endif
#ifdef HAVE_MIXEDGAUGE
    {o_mixedgauge,      MIXEDGAUGE_BASE, 0, call_mixed_gauge},
#endif
#ifdef HAVE_DLG_MIXEDFORM
    {o_mixedform,      13, 0, call_mixed_form},
#endif
#ifdef HAVE_DLG_TAILBOX
    {o_tailbox,         4, 4, call_tailbox},
    {o_tailboxbg,       4, 4, call_tailboxbg},
#endif
#ifdef HAVE_XDIALOG
    {o_calendar,        4, 7, call_calendar},
    {o_dselect,         4, 5, call_dselect},
    {o_editbox,         4, 4, call_editbox},
    {o_fselect,         4, 5, call_fselect},
    {o_timebox,         4, 7, call_timebox},
#endif
#ifdef HAVE_XDIALOG2
    {o_buildlist,       4, 0, call_buildlist},
    {o_rangebox,        5, 7, call_rangebox},
    {o_treeview,        4, 0, call_treeview},
#endif
};
/* *INDENT-ON* */

static char *
optionString(char **argv, int *num)
{
    int next = *num + 1;
    char *result = argv[next];
    if (result == 0) {
	char temp[80];
	sprintf(temp, "Expected a string-parameter for %.20s", argv[*num]);
	Usage(temp);
    }
    *num = next;
    return result;
}

static int
optionValue(char **argv, int *num)
{
    int next = *num + 1;
    char *src = argv[next];
    char *tmp = 0;
    int result = 0;

    if (src != 0) {
	result = (int) strtol(src, &tmp, 0);
	if (tmp == 0 || *tmp != 0)
	    src = 0;
    }

    if (src == 0) {
	char temp[80];
	sprintf(temp, "Expected a numeric-parameter for %.20s", argv[*num]);
	Usage(temp);
    }
    *num = next;
    return result;
}

/* Return exit-code for a named button */
static int
button_code(const char *name)
{
    /* *INDENT-OFF* */
    static struct {
	const char *name;
	int code;
    } table[] = {
	{ "ok",	    DLG_EXIT_OK },
	{ "yes",    DLG_EXIT_OK },
	{ "cancel", DLG_EXIT_CANCEL },
	{ "no",	    DLG_EXIT_CANCEL },
	{ "help",   DLG_EXIT_HELP },
	{ "extra",  DLG_EXIT_EXTRA },
    };
    /* *INDENT-ON* */

    int code = DLG_EXIT_ERROR;
    size_t i;

    for (i = 0; i < (sizeof(table) / sizeof(table[0])); i++) {
	if (!dlg_strcmp(name, table[i].name)) {
	    code = table[i].code;
	    break;
	}
    }

    if (code == DLG_EXIT_ERROR) {
	char temp[80];
	sprintf(temp, "Button name \"%.20s\" unknown", name);
	Usage(temp);
    }

    return code;
}

/*
 * If this is the last option, we do not want any error messages - just our
 * output.  Calling end_dialog() cancels the refresh() at the end of the
 * program as well.
 */
static void
IgnoreNonScreen(char **argv, int offset)
{
    if (argv[offset + 1] == 0) {
	ignore_unknown = TRUE;
	end_dialog();
    }
}

static void
PrintTextOnly(char **argv, int *offset, eOptions code)
{
    /* TODO - handle two optional numeric params */
    char *text;
    int height = 0;
    int width = 0;
    int height2 = 0;
    int width2 = 0;
    int next = arg_rest(argv + *offset);

    if (LINES <= 0 && COLS <= 0)
	dlg_ttysize(fileno(dialog_state.input), &LINES, &COLS);

    text = strdup(optionString(argv, offset));
    IgnoreNonScreen(argv, *offset);

    if (next >= 1) {
	next = MIN(next, 3);
	height = numeric_arg(argv, *offset + 1);
	if (next >= 2)
	    width = numeric_arg(argv, *offset + 2);
	*offset += next - 1;
    }

    dlg_trim_string(text);
    dlg_auto_size(NULL, text, &height2, &width2, height, width);

    switch (code) {
    case o_print_text_only:
	dialog_state.text_only = TRUE;
	dlg_print_autowrap(stdscr, text, height2, width2);
	dialog_state.text_only = FALSE;
	break;
    case o_print_text_size:
	fprintf(dialog_state.output, "%d %d\n",
		dialog_state.text_height,
		dialog_state.text_width);
	break;
    default:
	break;
    }
}

/*
 * Print parts of a message
 */
static void
PrintList(const char *const *list)
{
    const char *leaf = strrchr(program, '/');
    unsigned n = 0;

    if (leaf != 0)
	leaf++;
    else
	leaf = program;

    while (*list != 0) {
	fprintf(dialog_state.output, *list, n ? leaf : dialog_version());
	(void) fputc('\n', dialog_state.output);
	n = 1;
	list++;
    }
}

static const Mode *
lookupMode(eOptions code)
{
    const Mode *modePtr = 0;
    unsigned n;

    for (n = 0; n < sizeof(modes) / sizeof(modes[0]); n++) {
	if (modes[n].code == code) {
	    modePtr = &modes[n];
	    break;
	}
    }
    return modePtr;
}

static int
compare_opts(const void *a, const void *b)
{
    Options *const *p = (Options * const *) a;
    Options *const *q = (Options * const *) b;
    return strcmp((*p)->name, (*q)->name);
}

/*
 * Print program's version.
 */
static void
PrintVersion(FILE *fp)
{
    fprintf(fp, "Version: %s\n", dialog_version());
}

/*
 * Print program help-message
 */
static void
Help(void)
{
    static const char *const tbl_1[] =
    {
	"cdialog (ComeOn Dialog!) version %s",
	"Copyright 2000-2017,2018 Thomas E. Dickey",
	"This is free software; see the source for copying conditions.  There is NO",
	"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.",
	"",
	"* Display dialog boxes from shell scripts *",
	"",
	"Usage: %s <options> { --and-widget <options> }",
	"where options are \"common\" options, followed by \"box\" options",
	"",
#ifdef HAVE_RC_FILE
	"Special options:",
	"  [--create-rc \"file\"]",
#endif
	0
    }, *const tbl_3[] =
    {
	"",
	"Auto-size with height and width = 0. Maximize with height and width = -1.",
	"Global-auto-size if also menu_height/list_height = 0.",
	0
    };
    size_t limit = sizeof(options) / sizeof(options[0]);
    size_t j, k;
    const Options **opts;

    end_dialog();
    dialog_state.output = stdout;

    opts = dlg_calloc(const Options *, limit);
    assert_ptr(opts, "Help");
    for (j = 0; j < limit; ++j) {
	opts[j] = &(options[j]);
    }
    qsort(opts, limit, sizeof(Options *), compare_opts);

    PrintList(tbl_1);
    fprintf(dialog_state.output, "Common options:\n ");
    for (j = k = 0; j < limit; j++) {
	if ((opts[j]->pass & 1)
	    && opts[j]->help != 0) {
	    size_t len = 6 + strlen(opts[j]->name) + strlen(opts[j]->help);
	    k += len;
	    if (k > 75) {
		fprintf(dialog_state.output, "\n ");
		k = len;
	    }
	    fprintf(dialog_state.output, " [--%s%s%s]", opts[j]->name,
		    *(opts[j]->help) ? " " : "", opts[j]->help);
	}
    }
    fprintf(dialog_state.output, "\nBox options:\n");
    for (j = 0; j < limit; j++) {
	if ((opts[j]->pass & 2) != 0
	    && opts[j]->help != 0
	    && lookupMode(opts[j]->code)) {
	    fprintf(dialog_state.output, "  --%-12s %s\n", opts[j]->name,
		    opts[j]->help);
	}
    }
    PrintList(tbl_3);

    free(opts);
    handle_leaks();
    dlg_exit(DLG_EXIT_OK);
}

#ifdef HAVE_DLG_TRACE
/*
 * Only the first call to dlg_trace will open a trace file.  But each time
 * --trace is parsed, we show the whole parameter list as it is at that moment,
 * counting discarded parameters.  The only way to capture the whole parameter
 * list is if --trace is the first option.
 */
static void
process_trace_option(char **argv, int *offset)
{
    int j;

    if (dialog_state.trace_output == 0) {
	dlg_trace(optionString(argv, offset));
    } else {
	DLG_TRACE(("# ignore extra --trace option\n"));
	*offset += 1;
    }

    DLG_TRACE(("# Parameters:\n"));
    for (j = 0; argv[j] != 0; ++j) {
	DLG_TRACE(("#\targv[%d] = %s\n", j, argv[j]));
    }
}
#endif

/*
 * "Common" options apply to all widgets more/less.  Most of the common options
 * set values in dialog_vars, a few set dialog_state and a couple write to the
 * output stream.
 */
static int
process_common_options(int argc, char **argv, int offset, bool output)
{
    bool done = FALSE;
    eOptions code;

    DLG_TRACE(("# process_common_options, offset %d\n", offset));

    while (offset < argc && !done) {	/* Common options */
	DLG_TRACE(("#\targv[%d] = %s\n", offset, argv[offset]));
	switch (code = lookupOption(argv[offset], 1)) {
	case o_title:
	    dialog_vars.title = optionString(argv, &offset);
	    break;
	case o_backtitle:
	    dialog_vars.backtitle = optionString(argv, &offset);
	    break;
	case o_separate_widget:
	    dialog_state.separate_str = optionString(argv, &offset);
	    break;
	case o_separate_output:
	    dialog_vars.separate_output = TRUE;
	    break;
	case o_colors:
	    dialog_vars.colors = TRUE;
	    break;
	case o_cr_wrap:
	    dialog_vars.cr_wrap = TRUE;
	    break;
	case o_no_nl_expand:
	    dialog_vars.no_nl_expand = TRUE;
	    break;
	case o_no_collapse:
	    dialog_vars.nocollapse = TRUE;
	    break;
	case o_no_kill:
	    dialog_vars.cant_kill = TRUE;
	    break;
	case o_nocancel:
	    dialog_vars.nocancel = TRUE;
	    break;
	case o_nook:
	    dialog_vars.nook = TRUE;
	    break;
	case o_quoted:
	    dialog_vars.quoted = TRUE;
	    break;
	case o_single_quoted:
	    dialog_vars.single_quoted = TRUE;
	    break;
	case o_size_err:
	    dialog_vars.size_err = TRUE;
	    break;
	case o_beep:
	    dialog_vars.beep_signal = TRUE;
	    break;
	case o_beep_after:
	    dialog_vars.beep_after_signal = TRUE;
	    break;
	case o_scrollbar:
	    dialog_state.use_scrollbar = TRUE;
	    break;
	case o_shadow:
	    dialog_state.use_shadow = TRUE;
	    break;
	case o_defaultno:
	    dialog_vars.defaultno = TRUE;
	    dialog_vars.default_button = DLG_EXIT_CANCEL;
	    break;
	case o_default_button:
	    dialog_vars.default_button = button_code(optionString(argv, &offset));
	    dialog_vars.defaultno = dialog_vars.default_button == DLG_EXIT_CANCEL;
	    break;
	case o_default_item:
	    dialog_vars.default_item = optionString(argv, &offset);
	    break;
	case o_insecure:
	    dialog_vars.insecure = TRUE;
	    break;
	case o_item_help:
	    dialog_vars.item_help = TRUE;
	    break;
	case o_help_line:
	    dialog_vars.help_line = optionString(argv, &offset);
	    break;
	case o_help_file:
	    dialog_vars.help_file = optionString(argv, &offset);
	    break;
	case o_help_button:
	    dialog_vars.help_button = TRUE;
	    break;
	case o_help_status:
	    dialog_vars.help_status = TRUE;
	    break;
	case o_help_tags:
	    dialog_vars.help_tags = TRUE;
	    break;
	case o_extra_button:
	    dialog_vars.extra_button = TRUE;
	    break;
	case o_ignore:
	    ignore_unknown = TRUE;
	    break;
	case o_keep_window:
	    dialog_vars.keep_window = TRUE;
	    break;
	case o_last_key:
	    dialog_vars.last_key = TRUE;
	    break;
	case o_no_shadow:
	    dialog_state.use_shadow = FALSE;
	    break;
	case o_print_size:
	    dialog_vars.print_siz = TRUE;
	    break;
	case o_print_text_only:
	case o_print_text_size:
	    PrintTextOnly(argv, &offset, code);
	    break;
	case o_print_maxsize:
	    if (output) {
		IgnoreNonScreen(argv, offset);
		fflush(dialog_state.output);
		fprintf(dialog_state.output, "MaxSize: %d, %d\n", SLINES, SCOLS);
	    }
	    break;
	case o_print_version:
	    if (output) {
		PrintVersion(dialog_state.output);
	    }
	    break;
	case o_separator:
	case o_output_separator:
	    dialog_vars.output_separator = optionString(argv, &offset);
	    break;
	case o_column_separator:
	    dialog_vars.column_separator = optionString(argv, &offset);
	    break;
	case o_tab_correct:
	    dialog_vars.tab_correct = TRUE;
	    break;
	case o_sleep:
	    dialog_vars.sleep_secs = optionValue(argv, &offset);
	    break;
	case o_timeout:
	    dialog_vars.timeout_secs = optionValue(argv, &offset);
	    break;
	case o_max_input:
	    dialog_vars.max_input = optionValue(argv, &offset);
	    break;
	case o_tab_len:
	    dialog_state.tab_len = optionValue(argv, &offset);
	    break;
	case o_trim:
	    dialog_vars.trim_whitespace = TRUE;
	    break;
	case o_visit_items:
	    dialog_state.visit_items = TRUE;
	    dialog_state.visit_cols = 1;
	    break;
	case o_aspect:
	    dialog_state.aspect_ratio = optionValue(argv, &offset);
	    break;
	case o_begin:
	    dialog_vars.begin_set = TRUE;
	    dialog_vars.begin_y = optionValue(argv, &offset);
	    dialog_vars.begin_x = optionValue(argv, &offset);
	    break;
	case o_clear:
	    dialog_vars.dlg_clear_screen = TRUE;
	    break;
	case o_yes_label:
	    dialog_vars.yes_label = optionString(argv, &offset);
	    break;
	case o_no_label:
	    dialog_vars.no_label = optionString(argv, &offset);
	    break;
	case o_ok_label:
	    dialog_vars.ok_label = optionString(argv, &offset);
	    break;
	case o_cancel_label:
	    dialog_vars.cancel_label = optionString(argv, &offset);
	    break;
	case o_extra_label:
	    dialog_vars.extra_label = optionString(argv, &offset);
	    break;
	case o_exit_label:
	    dialog_vars.exit_label = optionString(argv, &offset);
	    break;
	case o_help_label:
	    dialog_vars.help_label = optionString(argv, &offset);
	    break;
	case o_date_format:
	    dialog_vars.date_format = optionString(argv, &offset);
	    break;
	case o_time_format:
	    dialog_vars.time_format = optionString(argv, &offset);
	    break;
	case o_keep_tite:
	    dialog_vars.keep_tite = TRUE;
	    break;
	case o_ascii_lines:
	    dialog_vars.ascii_lines = TRUE;
	    dialog_vars.no_lines = FALSE;
	    break;
	case o_no_lines:
	    dialog_vars.no_lines = TRUE;
	    dialog_vars.ascii_lines = FALSE;
	    break;
	case o_no_mouse:
	    dialog_state.no_mouse = TRUE;
	    mouse_close();
	    break;
#ifdef HAVE_WHIPTAIL
	case o_topleft:
	    dialog_vars.begin_set = TRUE;
	    dialog_vars.begin_y = 0;
	    dialog_vars.begin_x = 0;
	    break;
	case o_fullbutton:
	    /* ignore */
	    break;
#endif
	    /* options of Xdialog which we ignore */
	case o_icon:
	case o_wmclass:
	    (void) optionString(argv, &offset);
	    /* FALLTHRU */
	case o_allow_close:
	case o_auto_placement:
	case o_fixed_font:
	case o_keep_colors:
	case o_no_close:
	case o_no_cr_wrap:
	case o_screen_center:
	case o_smooth:
	case o_under_mouse:
	    break;
	case o_unknown:
	    if (ignore_unknown)
		break;
	    /* FALLTHRU */
	default:		/* no more common options */
	    done = TRUE;
	    break;
#ifdef HAVE_DLG_TRACE
	case o_trace:
	    process_trace_option(argv, &offset);
	    break;
#endif
#if defined(HAVE_XDIALOG2) || defined(HAVE_WHIPTAIL)
	case o_no_items:
	    dialog_vars.no_items = TRUE;
	    break;
	case o_no_tags:
	    dialog_vars.no_tags = TRUE;
	    break;
#endif
#ifdef HAVE_XDIALOG2
	case o_reorder:
	    dialog_vars.reorder = TRUE;
	    break;
#endif
#ifdef HAVE_XDIALOG
	case o_week_start:
	    dialog_vars.week_start = optionString(argv, &offset);
	    break;
#endif
	case o_iso_week:
	    dialog_vars.iso_week = TRUE;
	    if (dialog_vars.week_start == 0) {	/* Monday is implied */
		static char default_1st[] = "1";
		dialog_vars.week_start = default_1st;
	    }
	    break;
	}
	if (!done)
	    offset++;
    }

    if (dialog_state.aspect_ratio == 0)
	dialog_state.aspect_ratio = DEFAULT_ASPECT_RATIO;

    return offset;
}

/*
 * Initialize options at the start of a series of common options culminating
 * in a widget.
 */
static void
init_result(char *buffer)
{
    static bool first = TRUE;

    DLG_TRACE(("# init_result\n"));

    /* clear everything we do not save for the next widget */
    memset(&dialog_vars, 0, sizeof(dialog_vars));

    dialog_vars.input_result = buffer;
    dialog_vars.input_result[0] = '\0';

    dialog_vars.default_button = -1;

    /*
     * The first time this is called, check for common options given by an
     * environment variable.
     */
    if (first) {
	char *env = getenv("DIALOGOPTS");
	if (env != 0)
	    env = dlg_strclone(env);
	if (env != 0) {
	    special_argv = dlg_string_to_argv(env);
	    special_argc = dlg_count_argv(special_argv);
	}
	first = FALSE;
    }

    if (special_argv != 0) {
	process_common_options(special_argc, special_argv, 0, FALSE);
    }
}

int
main(int argc, char *argv[])
{
    char temp[256];
    bool esc_pressed = FALSE;
    bool keep_tite = FALSE;
    bool first_time = TRUE;
    int offset = 1;
    int offset_add;
    int retval = DLG_EXIT_OK;
    int j, have;
    eOptions code;
    const Mode *modePtr;
    char my_buffer[MAX_LEN + 1];

    memset(&dialog_state, 0, sizeof(dialog_state));
    memset(&dialog_vars, 0, sizeof(dialog_vars));

#if defined(ENABLE_NLS)
    /* initialize locale support */
    setlocale(LC_ALL, "");
    bindtextdomain(NLS_TEXTDOMAIN, LOCALEDIR);
    textdomain(NLS_TEXTDOMAIN);
#elif defined(HAVE_SETLOCALE)
    (void) setlocale(LC_ALL, "");
#endif

    init_result(my_buffer);	/* honor $DIALOGOPTS */
    unescape_argv(&argc, &argv);
    program = argv[0];
    dialog_state.output = stderr;
    dialog_state.input = stdin;

    /*
     * Look for the last --stdout, --stderr or --output-fd option, and use
     * that.  We can only write to one of them.  If --stdout is used, that
     * can interfere with initializing the curses library, so we want to
     * know explicitly if it is used.
     *
     * Also, look for any --version or --help message, processing those
     * immediately.
     */
    while (offset < argc) {
	int base = offset;
	switch (lookupOption(argv[offset], 7)) {
	case o_stdout:
	    dialog_state.output = stdout;
	    break;
	case o_stderr:
	    dialog_state.output = stderr;
	    break;
	case o_input_fd:
	    if ((j = optionValue(argv, &offset)) < 0
		|| (dialog_state.input = fdopen(j, "r")) == 0) {
		handle_leaks();
		dlg_exiterr("Cannot open input-fd\n");
	    }
	    break;
	case o_output_fd:
	    if ((j = optionValue(argv, &offset)) < 0
		|| (dialog_state.output = fdopen(j, "w")) == 0) {
		handle_leaks();
		dlg_exiterr("Cannot open output-fd\n");
	    }
	    break;
	case o_keep_tite:
	    keep_tite = TRUE;
	    break;
	case o_version:
	    dialog_state.output = stdout;
	    PrintVersion(dialog_state.output);
	    dlg_exit(DLG_EXIT_OK);
	    break;
	case o_help:
	    Help();
	    break;
#ifdef HAVE_DLG_TRACE
	case o_trace:
	    /*
	     * Process/remove the --trace option if it is the first option.
	     * Otherwise, process it in more/less expected order as a
	     * "common" option.
	     */
	    if (base == 1) {
		process_trace_option(argv, &offset);
		break;
	    } else {
		++offset;
		continue;
	    }
#endif
	default:
	    ++offset;
	    continue;
	}
	DLG_TRACE(("# discarding %d parameters starting with argv[%d] (%s)\n",
		   1 + offset - base, base,
		   argv[base]));
	for (j = base; j < argc; ++j) {
	    dialog_argv[j] = dialog_argv[j + 1 + (offset - base)];
	}
	argc -= (1 + offset - base);
	offset = base;
    }
    offset = 1;
    init_result(my_buffer);
    dialog_vars.keep_tite = keep_tite;	/* init_result() cleared global */

    /*
     * Dialog's output may be redirected (see above).  Handle the special
     * case of options that only report information without interaction.
     */
    if (argc == 2) {
	switch (code = lookupOption(argv[1], 7)) {
	case o_print_maxsize:
	    (void) initscr();
	    endwin();
	    fflush(dialog_state.output);
	    fprintf(dialog_state.output, "MaxSize: %d, %d\n", SLINES, SCOLS);
	    break;
	case o_print_version:
	    PrintVersion(dialog_state.output);
	    break;
	case o_clear:
	    initscr();
	    refresh();
	    endwin();
	    break;
	case o_ignore:
	    break;
	default:
	    Help();
	    break;
	}
	dlg_exit(DLG_EXIT_OK);
    } else if (argc < 2) {
	Help();
    }
#ifdef HAVE_RC_FILE
    else if (lookupOption(argv[1], 7) == o_create_rc) {
	if (argc != 3) {
	    sprintf(temp, "Expected a filename for %.50s", argv[1]);
	    Usage(temp);
	}
	if (dlg_parse_rc() == -1) {	/* Read the configuration file */
	    handle_leaks();
	    dlg_exiterr("dialog: dlg_parse_rc");
	}
	dlg_create_rc(argv[2]);
	dlg_exit(DLG_EXIT_OK);
    }
#endif
    else {
	/*
	 * Handle combinations of common options including --print-text-only
	 * which can be done before involving curses, in case we can exit
	 * without initializing curses (and writing to the terminal).
	 */
	offset = process_common_options(argc, argv, offset, TRUE);
	if (offset >= argc)
	    dlg_exit(DLG_EXIT_OK);
    }

    init_dialog(dialog_state.input, dialog_state.output);

    while (offset < argc && !esc_pressed) {
	if (first_time) {
	    first_time = FALSE;
	} else {
	    init_result(my_buffer);
	    offset = process_common_options(argc, argv, offset, TRUE);
	}

	if (argv[offset] == NULL) {
	    if (ignore_unknown)
		break;
	    Usage("Expected a box option");
	}

	if (dialog_vars.separate_output) {
	    switch (lookupOption(argv[offset], 2)) {
#ifdef HAVE_XDIALOG2
	    case o_buildlist:
	    case o_treeview:
#endif
	    case o_checklist:
		break;
	    default:
		sprintf(temp,
			"Unexpected widget with --separate-output %.20s",
			argv[offset]);
		Usage(temp);
	    }
	}

	dlg_put_backtitle();

	/* use a table to look for the requested mode, to avoid code duplication */

	modePtr = 0;
	if ((code = lookupOption(argv[offset], 2)) != o_unknown)
	    modePtr = lookupMode(code);
	if (modePtr == 0) {
	    sprintf(temp, "%s option %.20s",
		    lookupOption(argv[offset], 7) != o_unknown
		    ? "Unexpected"
		    : "Unknown",
		    argv[offset]);
	    Usage(temp);
	}

	have = arg_rest(&argv[offset]);
	if (have < modePtr->argmin) {
	    sprintf(temp, "Expected at least %d tokens for %.20s, have %d",
		    modePtr->argmin - 1, argv[offset],
		    have - 1);
	    Usage(temp);
	}
	if (modePtr->argmax && have > modePtr->argmax) {
	    sprintf(temp,
		    "Expected no more than %d tokens for %.20s, have %d",
		    modePtr->argmax - 1, argv[offset],
		    have - 1);
	    Usage(temp);
	}

	/*
	 * Trim whitespace from non-title option values, e.g., the ones that
	 * will be used as captions or prompts.   Do that only for the widget
	 * we are about to process, since the "--trim" option is reset before
	 * accumulating options for each widget.
	 */
	for (j = offset + 1; j <= offset + have; j++) {
	    switch (lookupOption(argv[j - 1], 7)) {
	    case o_unknown:
	    case o_title:
	    case o_backtitle:
	    case o_help_line:
	    case o_help_file:
		break;
	    default:
		if (argv[j] != 0) {
		    char *argv_j = strdup(argv[j]);
		    if (argv_j != 0) {
			dlg_trim_string(argv_j);
			argv[j] = argv_j;
		    } else {
			argv[j] = strdup("?");
		    }
		    ignore_leak(argv[j]);
		}
		break;
	    }
	}

	DLG_TRACE(("# execute %s\n", argv[offset]));
	retval = show_result((*(modePtr->jumper)) (dialog_vars.title,
						   argv + offset,
						   &offset_add));
	DLG_TRACE(("# widget returns %d\n", retval));
	offset += offset_add;

	if (dialog_vars.input_result != my_buffer) {
	    free(dialog_vars.input_result);
	    dialog_vars.input_result = 0;
	}

	if (retval == DLG_EXIT_ESC) {
	    esc_pressed = TRUE;
	} else {

	    if (dialog_vars.beep_after_signal)
		(void) beep();

	    if (dialog_vars.sleep_secs)
		(void) napms(dialog_vars.sleep_secs * 1000);

	    if (offset < argc) {
		switch (lookupOption(argv[offset], 7)) {
		case o_and_widget:
		    offset++;
		    break;
		case o_unknown:
		    sprintf(temp, "Expected --and-widget, not %.20s",
			    argv[offset]);
		    Usage(temp);
		    break;
		default:
		    /* if we got a cancel, etc., stop chaining */
		    if (retval != DLG_EXIT_OK)
			esc_pressed = TRUE;
		    else
			dialog_vars.dlg_clear_screen = TRUE;
		    break;
		}
	    }
	    if (dialog_vars.dlg_clear_screen)
		dlg_clear();
	}
    }

    dlg_killall_bg(&retval);
    if (dialog_state.screen_initialized) {
	(void) refresh();
	end_dialog();
    }
    handle_leaks();
    dlg_exit(retval);
}
