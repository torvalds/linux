/*
 * $Id: calendar.c,v 1.97 2018/06/19 22:57:01 tom Exp $
 *
 *  calendar.c -- implements the calendar box
 *
 *  Copyright 2001-2017,2018	Thomas E. Dickey
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
#include <dlg_keys.h>

#include <time.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#define intptr_t long
#endif

#define ONE_DAY  (60 * 60 * 24)

#define MON_WIDE 4		/* width of a month-name */
#define DAY_HIGH 6		/* maximum lines in day-grid */
#define DAY_WIDE (8 * MON_WIDE)	/* width of the day-grid */
#define HDR_HIGH 1		/* height of cells with month/year */
#define BTN_HIGH 1		/* height of button-row excluding margin */

/* two more lines: titles for day-of-week and month/year boxes */
#define MIN_HIGH (DAY_HIGH + 2 + HDR_HIGH + BTN_HIGH + (MAX_DAYS * MARGIN))
#define MIN_WIDE (DAY_WIDE + (4 * MARGIN))

typedef enum {
    sMONTH = -3
    ,sYEAR = -2
    ,sDAY = -1
} STATES;

struct _box;

typedef int (*BOX_DRAW) (struct _box *, struct tm *);

typedef struct _box {
    WINDOW *parent;
    WINDOW *window;
    int x;
    int y;
    int width;
    int height;
    BOX_DRAW box_draw;
    int week_start;
} BOX;

#define MAX_DAYS 7
#define MAX_MONTHS 12

static char *cached_days[MAX_DAYS];
static char *cached_months[MAX_MONTHS];

static const char *
nameOfDayOfWeek(int n)
{
    static bool shown[MAX_DAYS];
    static const char *posix_days[MAX_DAYS] =
    {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday"
    };

    while (n < 0) {
	n += MAX_DAYS;
    }
    n %= MAX_DAYS;
#ifdef ENABLE_NLS
    if (cached_days[n] == 0) {
	const nl_item items[MAX_DAYS] =
	{
	    ABDAY_1, ABDAY_2, ABDAY_3, ABDAY_4, ABDAY_5, ABDAY_6, ABDAY_7
	};
	cached_days[n] = dlg_strclone(nl_langinfo(items[n]));
	memset(shown, 0, sizeof(shown));
    }
#endif
    if (cached_days[n] == 0) {
	size_t len, limit = MON_WIDE - 1;
	char *value = dlg_strclone(posix_days[n]);

	/*
	 * POSIX does not actually say what the length of an abbreviated name
	 * is.  Typically it is 2, which will fit into our layout.  That also
	 * happens to work with CJK entries as seen in glibc, which are a
	 * double-width cell.  For now (2016/01/26), handle too-long names only
	 * for POSIX values.
	 */
	if ((len = strlen(value)) > limit)
	    value[limit] = '\0';
	cached_days[n] = value;
    }
    if (!shown[n]) {
	DLG_TRACE(("# DAY(%d) = '%s'\n", n, cached_days[n]));
	shown[n] = TRUE;
    }
    return cached_days[n];
}

static const char *
nameOfMonth(int n)
{
    static bool shown[MAX_MONTHS];
    static const char *posix_mons[MAX_MONTHS] =
    {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December"
    };

    while (n < 0) {
	n += MAX_MONTHS;
    }
    n %= MAX_MONTHS;
#ifdef ENABLE_NLS
    if (cached_months[n] == 0) {
	const nl_item items[MAX_MONTHS] =
	{
	    MON_1, MON_2, MON_3, MON_4, MON_5, MON_6,
	    MON_7, MON_8, MON_9, MON_10, MON_11, MON_12
	};
	cached_months[n] = dlg_strclone(nl_langinfo(items[n]));
	memset(shown, 0, sizeof(shown));
    }
#endif
    if (cached_months[n] == 0) {
	cached_months[n] = dlg_strclone(posix_mons[n]);
    }
    if (!shown[n]) {
	DLG_TRACE(("# MON(%d) = '%s'\n", n, cached_months[n]));
	shown[n] = TRUE;
    }
    return cached_months[n];
}

/*
 * Algorithm for Gregorian calendar.
 */
static int
isleap(int y)
{
    return ((y % 4 == 0) &&
	    ((y % 100 != 0) ||
	     (y % 400 == 0))) ? 1 : 0;
}

static void
adjust_year_month(int *year, int *month)
{
    while (*month < 0) {
	*month += MAX_MONTHS;
	*year -= 1;
    }
    while (*month >= MAX_MONTHS) {
	*month -= MAX_MONTHS;
	*year += 1;
    }
}

static int
days_per_month(int year, int month)
{
    static const int nominal[] =
    {
	31, 28, 31, 30, 31, 30,
	31, 31, 30, 31, 30, 31
    };
    int result;

    adjust_year_month(&year, &month);
    result = nominal[month];
    if (month == 1)
	result += isleap(year);
    return result;
}

static int
days_in_month(struct tm *current, int offset /* -1, 0, 1 */ )
{
    int year = current->tm_year + 1900;
    int month = current->tm_mon + offset;

    adjust_year_month(&year, &month);
    return days_per_month(year, month);
}

static int
days_per_year(int year)
{
    return (isleap(year) ? 366 : 365);
}

static int
days_in_year(struct tm *current, int offset /* -1, 0, 1 */ )
{
    return days_per_year(current->tm_year + 1900 + offset);
}

/*
 * Adapted from C FAQ
 * "17.28: How can I find the day of the week given the date?"
 * implementation by Tomohiko Sakamoto.
 *
 * d = day (0 to whatever)
 * m = month (1 through 12)
 * y = year (1752 and later, for Gregorian calendar)
 */
static int
day_of_week(int y, int m, int d)
{
    static int t[] =
    {
	0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
    };
    y -= (m < 3);
    return (6 + (y + (y / 4) - (y / 100) + (y / 400) + t[m - 1] + d)) % MAX_DAYS;
}

static int
day_in_year(int year, int month, int day)
{
    int result = day;
    while (--month >= 1)
	result += days_per_month(year, month);
    return result;
}

static int
iso_week(int year, int month, int day)
{
    int week = 1;
    int dow;
    int new_year_dow;
    int diy;
    int new_years_eve_dow;
    static const int thursday = 3;

    /* add the number weeks *between* date and newyear */
    diy = day_in_year(year, month, day);
    week += (diy - 1) / MAX_DAYS;

    /* 0 = Monday */
    dow = day_of_week(year, month, day);
    new_year_dow = day_of_week(year, 1, 1);

    /*
     * If New Year falls on Friday, Saturday or Sunday, then New Years's week
     * is the last week of the preceding year.  In that case subtract one week.
     */
    if (new_year_dow > thursday)
	--week;

    /* Add one week if there is a Sunday to Monday transition. */
    if (dow - new_year_dow < 0)
	++week;

    /* Check if we are in the last week of the preceding year. */
    if (week < 1) {
	week = iso_week(--year, 12, 31);
    }

    /*
     * If we are in the same week as New Year's eve, check if New Year's eve is
     * in the first week of the next year.
     */
    new_years_eve_dow = (new_year_dow + 364 + isleap(year)) % MAX_DAYS;
    if (365 + isleap(year) - diy < MAX_DAYS
	&& new_years_eve_dow >= dow
	&& new_years_eve_dow < thursday) {
	++year;
	week = 1;
    }
    return week;
}

static int *
getisoweeks(int year, int month)
{
    static int result[10];
    int windx = 0;
    int day;
    int dpm = days_per_month(year, month);

    for (day = 1; day <= dpm; day += MAX_DAYS)
	result[windx++] = iso_week(year, month, day);
    /*
     * Ensure that there is a week number associated with the last day of the
     * month, e.g., in case the last day of the month falls before Thursday,
     * so that we have to show the week-number for the beginning of the
     * following month.
     */
    result[windx] = iso_week(year, month, dpm);
    return result;
}

static int
day_cell_number(struct tm *current)
{
    int cell;
    cell = current->tm_mday - ((6 + current->tm_mday - current->tm_wday) % MAX_DAYS);
    if ((current->tm_mday - 1) % MAX_DAYS != current->tm_wday)
	cell += 6;
    else
	cell--;
    return cell;
}

static int
next_or_previous(int key, int two_d)
{
    int result = 0;

    switch (key) {
    case DLGK_GRID_UP:
	result = two_d ? -MAX_DAYS : -1;
	break;
    case DLGK_GRID_LEFT:
	result = -1;
	break;
    case DLGK_GRID_DOWN:
	result = two_d ? MAX_DAYS : 1;
	break;
    case DLGK_GRID_RIGHT:
	result = 1;
	break;
    default:
	beep();
	break;
    }
    return result;
}

/*
 * Draw the day-of-month selection box
 */
static int
draw_day(BOX * data, struct tm *current)
{
    int cell_wide = MON_WIDE;
    int y, x, this_x = 0;
    int save_y = 0, save_x = 0;
    int day = current->tm_mday;
    int mday;
    int week = 0;
    int windx = 0;
    int *weeks = 0;
    int last = days_in_month(current, 0);
    int prev = days_in_month(current, -1);

    werase(data->window);
    dlg_draw_box2(data->parent,
		  data->y - MARGIN, data->x - MARGIN,
		  data->height + (2 * MARGIN), data->width + (2 * MARGIN),
		  menubox_attr,
		  menubox_border_attr,
		  menubox_border2_attr);

    dlg_attrset(data->window, menubox_attr);	/* daynames headline */
    for (x = 0; x < MAX_DAYS; x++) {
	mvwprintw(data->window,
		  0, (x + 1) * cell_wide, "%*.*s ",
		  cell_wide - 1,
		  cell_wide - 1,
		  nameOfDayOfWeek(x + data->week_start));
    }

    mday = ((6 + current->tm_mday -
	     current->tm_wday +
	     data->week_start) % MAX_DAYS) - MAX_DAYS;
    if (mday <= -MAX_DAYS)
	mday += MAX_DAYS;

    if (dialog_vars.iso_week) {
	weeks = getisoweeks(current->tm_year + 1900, current->tm_mon + 1);
    } else {
	/* mday is now in the range -6 to 0. */
	week = (current->tm_yday + 6 + mday - current->tm_mday) / MAX_DAYS;
    }

    for (y = 1; mday < last; y++) {
	dlg_attrset(data->window, menubox_attr);	/* weeknumbers headline */
	mvwprintw(data->window,
		  y, 0,
		  "%*d ",
		  cell_wide - 1,
		  weeks ? weeks[windx++] : ++week);
	for (x = 0; x < MAX_DAYS; x++) {
	    this_x = 1 + (x + 1) * cell_wide;
	    ++mday;
	    if (wmove(data->window, y, this_x) == ERR)
		continue;
	    dlg_attrset(data->window, item_attr);	/* not selected days */
	    if (mday == day) {
		dlg_attrset(data->window, item_selected_attr);	/* selected day */
		save_y = y;
		save_x = this_x;
	    }
	    if (mday > 0) {
		if (mday <= last) {
		    wprintw(data->window, "%*d", cell_wide - 2, mday);
		} else if (mday == day) {
		    wprintw(data->window, "%*d", cell_wide - 2, mday - last);
		}
	    } else if (mday == day) {
		wprintw(data->window, "%*d", cell_wide - 2, mday + prev);
	    }
	}
	wmove(data->window, save_y, save_x);
    }
    /* just draw arrows - scrollbar is unsuitable here */
    dlg_draw_arrows(data->parent, TRUE, TRUE,
		    data->x + ARROWS_COL,
		    data->y - 1,
		    data->y + data->height);

    return 0;
}

/*
 * Draw the month-of-year selection box
 */
static int
draw_month(BOX * data, struct tm *current)
{
    int month;

    month = current->tm_mon + 1;

    dlg_attrset(data->parent, dialog_attr);	/* Headline "Month" */
    (void) mvwprintw(data->parent, data->y - 2, data->x - 1, _("Month"));
    dlg_draw_box2(data->parent,
		  data->y - 1, data->x - 1,
		  data->height + 2, data->width + 2,
		  menubox_attr,
		  menubox_border_attr,
		  menubox_border2_attr);
    dlg_attrset(data->window, item_attr);	/* color the month selection */
    mvwprintw(data->window, 0, 0, "%s", nameOfMonth(month - 1));
    wmove(data->window, 0, 0);
    return 0;
}

/*
 * Draw the year selection box
 */
static int
draw_year(BOX * data, struct tm *current)
{
    int year = current->tm_year + 1900;

    dlg_attrset(data->parent, dialog_attr);	/* Headline "Year" */
    (void) mvwprintw(data->parent, data->y - 2, data->x - 1, _("Year"));
    dlg_draw_box2(data->parent,
		  data->y - 1, data->x - 1,
		  data->height + 2, data->width + 2,
		  menubox_attr,
		  menubox_border_attr,
		  menubox_border2_attr);
    dlg_attrset(data->window, item_attr);	/* color the year selection */
    mvwprintw(data->window, 0, 0, "%4d", year);
    wmove(data->window, 0, 0);
    return 0;
}

static int
init_object(BOX * data,
	    WINDOW *parent,
	    int x, int y,
	    int width, int height,
	    BOX_DRAW box_draw,
	    int key_offset,
	    int code)
{
    data->parent = parent;
    data->x = x;
    data->y = y;
    data->width = width;
    data->height = height;
    data->box_draw = box_draw;
    data->week_start = key_offset;

    data->window = derwin(data->parent,
			  data->height, data->width,
			  data->y, data->x);
    if (data->window == 0)
	return -1;
    (void) keypad(data->window, TRUE);

    dlg_mouse_setbase(getbegx(parent), getbegy(parent));
    if (code == 'D') {
	dlg_mouse_mkbigregion(y + 1, x + MON_WIDE, height - 1, width - MON_WIDE,
			      KEY_MAX + key_offset, 1, MON_WIDE, 3);
    } else {
	dlg_mouse_mkregion(y, x, height, width, code);
    }

    return 0;
}

#if defined(ENABLE_NLS) && defined(HAVE_NL_LANGINFO_1STDAY)
#elif defined(HAVE_DLG_GAUGE)
static int
read_locale_setting(const char *name, int which)
{
    FILE *fp;
    char command[80];
    int result = -1;

    sprintf(command, "locale %s", name);
    if ((fp = dlg_popen(command, "r")) != 0) {
	int count = 0;
	char buf[80];

	while (fgets(buf, (int) sizeof(buf) - 1, fp) != 0) {
	    if (++count > which) {
		char *next = 0;
		long check = strtol(buf, &next, 0);
		if (next != 0 &&
		    next != buf &&
		    *next == '\n') {
		    result = (int) check;
		}
		break;
	    }
	}
	pclose(fp);
    }
    return result;
}
#endif

static int
WeekStart(void)
{
    int result = 0;
    char *option = dialog_vars.week_start;
    if (option != 0) {
	if (option[0]) {
	    char *next = 0;
	    long check = strtol(option, &next, 0);
	    if (next == 0 ||
		next == option ||
		*next != '\0') {
		if (!strcmp(option, "locale")) {
#if defined(ENABLE_NLS) && defined(HAVE_NL_LANGINFO_1STDAY)
		    /*
		     * glibc-specific.
		     */
		    int first_day = nl_langinfo(_NL_TIME_FIRST_WEEKDAY)[0];
		    char *basis_ptr = nl_langinfo(_NL_TIME_WEEK_1STDAY);
		    int basis_day = (int) (intptr_t) basis_ptr;
#elif defined(HAVE_DLG_GAUGE)
		    /*
		     * probably Linux-specific, but harmless otherwise.  When
		     * available, the locale program will return a single
		     * integer on one line.
		     */
		    int first_day = read_locale_setting("first_weekday", 0);
		    int basis_day = read_locale_setting("week-1stday", 0);
#endif
#if (defined(ENABLE_NLS) && defined(HAVE_NL_LANGINFO_1STDAY)) || defined(HAVE_DLG_GAUGE)
		    int week_1stday = -1;
		    if (basis_day == 19971130)
			week_1stday = 0;	/* Sun */
		    else if (basis_day == 19971201)
			week_1stday = 1;	/* Mon */
		    if (week_1stday >= 0) {
			result = first_day - week_1stday - 1;
		    }
#else
		    result = 0;	/* Sun */
#endif
		} else {
		    int day;
		    size_t eql = strlen(option);
		    for (day = 0; day < MAX_DAYS; ++day) {
			if (!strncmp(nameOfDayOfWeek(day), option, eql)) {
			    result = day;
			    break;
			}
		    }
		}
	    } else if (check < 0) {
		result = -1;
	    } else {
		result = (int) (check % MAX_DAYS);
	    }
	}
    }
    return result;
}

static int
CleanupResult(int code, WINDOW *dialog, char *prompt, DIALOG_VARS * save_vars)
{
    int n;

    if (dialog != 0)
	dlg_del_window(dialog);
    dlg_mouse_free_regions();
    if (prompt != 0)
	free(prompt);
    dlg_restore_vars(save_vars);

    for (n = 0; n < MAX_DAYS; ++n) {
	free(cached_days[n]);
	cached_days[n] = 0;
    }
    for (n = 0; n < MAX_MONTHS; ++n) {
	free(cached_months[n]);
	cached_months[n] = 0;
    }

    return code;
}

static void
trace_date(struct tm *current, struct tm *old)
{
    bool changed = (old == 0 ||
		    current->tm_mday != old->tm_mday ||
		    current->tm_mon != old->tm_mon ||
		    current->tm_year != old->tm_year);
    if (changed) {
	DLG_TRACE(("# current %04d/%02d/%02d\n",
		   current->tm_year + 1900,
		   current->tm_mon + 1,
		   current->tm_mday));
    } else {
	DLG_TRACE(("# current (unchanged)\n"));
    }
}

#define DrawObject(data) (data)->box_draw(data, &current)

/*
 * Display a dialog box for entering a date
 */
int
dialog_calendar(const char *title,
		const char *subtitle,
		int height,
		int width,
		int day,
		int month,
		int year)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	TOGGLEKEY_BINDINGS,
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ),
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	'j' ),
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	DLGK_MOUSE(KEY_NPAGE) ),
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	KEY_DOWN ),
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	KEY_NPAGE ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,	'-' ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  'h' ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  CHR_BACKSPACE ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  CHR_PREVIOUS ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  KEY_LEFT ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT,	'+' ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, 'l' ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, CHR_NEXT ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, KEY_NEXT ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, KEY_RIGHT ),
	DLG_KEYS_DATA( DLGK_GRID_UP,	'k' ),
	DLG_KEYS_DATA( DLGK_GRID_UP,	KEY_PPAGE ),
	DLG_KEYS_DATA( DLGK_GRID_UP,	KEY_PREVIOUS ),
	DLG_KEYS_DATA( DLGK_GRID_UP,	KEY_UP ),
	DLG_KEYS_DATA( DLGK_GRID_UP,  	DLGK_MOUSE(KEY_PPAGE) ),
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
#endif
    BOX dy_box, mn_box, yr_box;
    int fkey;
    int key = 0;
    int key2;
    int step;
    int button;
    int result = DLG_EXIT_UNKNOWN;
    int week_start;
    WINDOW *dialog;
    time_t now_time = time((time_t *) 0);
    struct tm current;
    int state = dlg_default_button();
    const char **buttons = dlg_ok_labels();
    char *prompt;
    int mincols = MIN_WIDE;
    char buffer[MAX_LEN];
    DIALOG_VARS save_vars;

    DLG_TRACE(("# calendar args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", subtitle);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("day", day);
    DLG_TRACE2N("month", month);
    DLG_TRACE2N("year", year);

    dlg_save_vars(&save_vars);
    dialog_vars.separate_output = TRUE;

    dlg_does_output();

    /*
     * Unless overrridden, the current time/date is our starting point.
     */
    now_time = time((time_t *) 0);
    current = *localtime(&now_time);

#if HAVE_MKTIME
    current.tm_isdst = -1;
    if (year >= 1900) {
	current.tm_year = year - 1900;
    }
    if (month >= 1) {
	current.tm_mon = month - 1;
    }
    if (day > 0 && day <= days_per_month(current.tm_year + 1900,
					 current.tm_mon + 1)) {
	current.tm_mday = day;
    }
    now_time = mktime(&current);
#else
    if (day < 0)
	day = current.tm_mday;
    if (month < 0)
	month = current.tm_mon + 1;
    if (year < 0)
	year = current.tm_year + 1900;

    /* compute a struct tm that matches the day/month/year parameters */
    if (((year -= 1900) > 0) && (year < 200)) {
	/* ugly, but I'd like to run this on older machines w/o mktime -TD */
	for (;;) {
	    if (year > current.tm_year) {
		now_time += ONE_DAY * days_in_year(&current, 0);
	    } else if (year < current.tm_year) {
		now_time -= ONE_DAY * days_in_year(&current, -1);
	    } else if (month > current.tm_mon + 1) {
		now_time += ONE_DAY * days_in_month(&current, 0);
	    } else if (month < current.tm_mon + 1) {
		now_time -= ONE_DAY * days_in_month(&current, -1);
	    } else if (day > current.tm_mday) {
		now_time += ONE_DAY;
	    } else if (day < current.tm_mday) {
		now_time -= ONE_DAY;
	    } else {
		break;
	    }
	    current = *localtime(&now_time);
	}
    }
#endif

    dlg_button_layout(buttons, &mincols);

#ifdef KEY_RESIZE
  retry:
#endif

    prompt = dlg_strclone(subtitle);
    dlg_auto_size(title, prompt, &height, &width, 0, mincols);

    height += MIN_HIGH - 1;
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    dialog = dlg_new_window(height, width,
			    dlg_box_y_ordinate(height),
			    dlg_box_x_ordinate(width));
    dlg_register_window(dialog, "calendar", binding);
    dlg_register_buttons(dialog, "calendar", buttons);

    /* mainbox */
    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);	/* text mainbox */
    dlg_print_autowrap(dialog, prompt, height, width);

    /* compute positions of day, month and year boxes */
    memset(&dy_box, 0, sizeof(dy_box));
    memset(&mn_box, 0, sizeof(mn_box));
    memset(&yr_box, 0, sizeof(yr_box));

    if ((week_start = WeekStart()) < 0 ||
	init_object(&dy_box,
		    dialog,
		    (width - DAY_WIDE) / 2,
		    1 + (height - (DAY_HIGH + BTN_HIGH + (5 * MARGIN))),
		    DAY_WIDE,
		    DAY_HIGH + 1,
		    draw_day,
		    week_start,
		    'D') < 0 ||
	((dy_box.week_start = WeekStart()) < 0) ||
	DrawObject(&dy_box) < 0) {
	return CleanupResult(DLG_EXIT_ERROR, dialog, prompt, &save_vars);
    }

    if (init_object(&mn_box,
		    dialog,
		    dy_box.x,
		    dy_box.y - (HDR_HIGH + 2 * MARGIN),
		    (DAY_WIDE / 2) - MARGIN,
		    HDR_HIGH,
		    draw_month,
		    0,
		    'M') < 0
	|| DrawObject(&mn_box) < 0) {
	return CleanupResult(DLG_EXIT_ERROR, dialog, prompt, &save_vars);
    }

    if (init_object(&yr_box,
		    dialog,
		    dy_box.x + mn_box.width + 2,
		    mn_box.y,
		    mn_box.width,
		    mn_box.height,
		    draw_year,
		    0,
		    'Y') < 0
	|| DrawObject(&yr_box) < 0) {
	return CleanupResult(DLG_EXIT_ERROR, dialog, prompt, &save_vars);
    }

    dlg_trace_win(dialog);
    while (result == DLG_EXIT_UNKNOWN) {
	BOX *obj = (state == sDAY ? &dy_box
		    : (state == sMONTH ? &mn_box :
		       (state == sYEAR ? &yr_box : 0)));

	button = (state < 0) ? 0 : state;
	dlg_draw_buttons(dialog, height - 2, 0, buttons, button, FALSE, width);
	if (obj != 0)
	    dlg_set_focus(dialog, obj->window);

	key = dlg_mouse_wgetch(dialog, &fkey);
	if (dlg_result_key(key, fkey, &result))
	    break;

#define Mouse2Key(key) (key - M_EVENT)
	if (fkey && (key >= DLGK_MOUSE(KEY_MIN) && key <= DLGK_MOUSE(KEY_MAX))) {
	    key = dlg_lookup_key(dialog, Mouse2Key(key), &fkey);
	}

	if ((key2 = dlg_char_to_button(key, buttons)) >= 0) {
	    result = key2;
	} else if (fkey) {
	    /* handle function-keys */
	    switch (key) {
	    case DLGK_MOUSE('D'):
		state = sDAY;
		break;
	    case DLGK_MOUSE('M'):
		state = sMONTH;
		break;
	    case DLGK_MOUSE('Y'):
		state = sYEAR;
		break;
	    case DLGK_TOGGLE:
	    case DLGK_ENTER:
		result = dlg_enter_buttoncode(button);
		break;
	    case DLGK_FIELD_PREV:
		state = dlg_prev_ok_buttonindex(state, sMONTH);
		break;
	    case DLGK_FIELD_NEXT:
		state = dlg_next_ok_buttonindex(state, sMONTH);
		break;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		free(prompt);
		dlg_clear();
		dlg_del_window(dialog);
		dlg_mouse_free_regions();
		/* repaint */
		goto retry;
#endif
	    default:
		step = 0;
		key2 = -1;
		if (is_DLGK_MOUSE(key)) {
		    if ((key2 = dlg_ok_buttoncode(Mouse2Key(key))) >= 0) {
			result = key2;
			break;
		    } else if (key >= DLGK_MOUSE(KEY_MAX)) {
			state = sDAY;
			obj = &dy_box;
			key2 = 1;
			step = (key
				- DLGK_MOUSE(KEY_MAX)
				- day_cell_number(&current));
			DLG_TRACE(("# mouseclick decoded %d\n", step));
		    }
		}
		if (obj != 0) {
		    if (key2 < 0) {
			step = next_or_previous(key, (obj == &dy_box));
		    }
		    if (step != 0) {
			struct tm old = current;

			/* see comment regarding mktime -TD */
			if (obj == &dy_box) {
			    now_time += ONE_DAY * step;
			} else if (obj == &mn_box) {
			    if (step > 0)
				now_time += ONE_DAY *
				    days_in_month(&current, 0);
			    else
				now_time -= ONE_DAY *
				    days_in_month(&current, -1);
			} else if (obj == &yr_box) {
			    if (step > 0)
				now_time += (ONE_DAY
					     * days_in_year(&current, 0));
			    else
				now_time -= (ONE_DAY
					     * days_in_year(&current, -1));
			}

			current = *localtime(&now_time);

			trace_date(&current, &old);
			if (obj != &dy_box
			    && (current.tm_mday != old.tm_mday
				|| current.tm_mon != old.tm_mon
				|| current.tm_year != old.tm_year))
			    DrawObject(&dy_box);
			if (obj != &mn_box && current.tm_mon != old.tm_mon)
			    DrawObject(&mn_box);
			if (obj != &yr_box && current.tm_year != old.tm_year)
			    DrawObject(&yr_box);
			(void) DrawObject(obj);
		    }
		} else if (state >= 0) {
		    if (next_or_previous(key, FALSE) < 0)
			state = dlg_prev_ok_buttonindex(state, sMONTH);
		    else if (next_or_previous(key, FALSE) > 0)
			state = dlg_next_ok_buttonindex(state, sMONTH);
		}
		break;
	    }
	}
    }

#define DefaultFormat(dst, src) \
	sprintf(dst, "%02d/%02d/%0d", \
		src.tm_mday, src.tm_mon + 1, src.tm_year + 1900)
#ifdef HAVE_STRFTIME
    if (dialog_vars.date_format != 0) {
	size_t used = strftime(buffer,
			       sizeof(buffer) - 1,
			       dialog_vars.date_format,
			       &current);
	if (used == 0 || *buffer == '\0')
	    DefaultFormat(buffer, current);
    } else
#endif
	DefaultFormat(buffer, current);

    dlg_add_result(buffer);
    dlg_add_separator();
    dlg_add_last_key(-1);

    return CleanupResult(result, dialog, prompt, &save_vars);
}
