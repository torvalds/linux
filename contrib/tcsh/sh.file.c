/* $Header: /p/tcsh/cvsroot/tcsh/sh.file.c,v 3.40 2016/04/16 14:08:14 christos Exp $ */
/*
 * sh.file.c: File completion for csh. This file is not used in tcsh.
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"
#include "ed.h"

RCSID("$tcsh: sh.file.c,v 3.40 2016/04/16 14:08:14 christos Exp $")

#if defined(FILEC) && defined(TIOCSTI)

/*
 * Tenex style file name recognition, .. and more.
 * History:
 *	Author: Ken Greer, Sept. 1975, CMU.
 *	Finally got around to adding to the Cshell., Ken Greer, Dec. 1981.
 */

#define ON	1
#define OFF	0
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define ESC     CTL_ESC('\033')

typedef enum {
    LIST, RECOGNIZE
}       COMMAND;

static	void	 setup_tty		(int);
static	void	 back_to_col_1		(void);
static	void	 pushback		(const Char *);
static	int	 filetype		(const Char *, const Char *);
static	void	 print_by_column	(const Char *, Char *[], size_t);
static	Char 	*tilde			(const Char *);
static	void	 retype			(void);
static	void	 beep			(void);
static	void 	 print_recognized_stuff	(const Char *);
static	void	 extract_dir_and_name	(const Char *, Char **, const Char **);
static	Char	*getitem		(DIR *, int);
static	size_t	 tsearch		(Char *, COMMAND, size_t);
static	int	 compare		(const void *, const void *);
static	int	 recognize		(Char **, Char *, size_t, size_t);
static	int	 is_prefix		(const Char *, const Char *);
static	int	 is_suffix		(const Char *, const Char *);
static	int	 ignored		(const Char *);


/*
 * Put this here so the binary can be patched with adb to enable file
 * completion by default.  Filec controls completion, nobeep controls
 * ringing the terminal bell on incomplete expansions.
 */
int    filec = 0;

static void
setup_tty(int on)
{
#ifdef TERMIO
# ifdef POSIX
    struct termios tchars;
# else
    struct termio tchars;
# endif /* POSIX */

# ifdef POSIX
    (void) tcgetattr(SHIN, &tchars);
# else
    (void) ioctl(SHIN, TCGETA, (ioctl_t) &tchars);
# endif /* POSIX */
    if (on) {
	tchars.c_cc[VEOL] = ESC;
	if (tchars.c_lflag & ICANON)
# ifdef POSIX
	    on = TCSADRAIN;
# else
	    on = TCSETA;
# endif /* POSIX */
	else {
# ifdef POSIX
	    on = TCSAFLUSH;
# else
	    on = TCSETAF;
# endif /* POSIX */
	    tchars.c_lflag |= ICANON;
    
	}
    }
    else {
	tchars.c_cc[VEOL] = _POSIX_VDISABLE;
# ifdef POSIX
	on = TCSADRAIN;
# else
        on = TCSETA;
# endif /* POSIX */
    }
# ifdef POSIX
    (void) xtcsetattr(SHIN, on, &tchars);
# else
    (void) ioctl(SHIN, on, (ioctl_t) &tchars);
# endif /* POSIX */
#else
    struct sgttyb sgtty;
    static struct tchars tchars;/* INT, QUIT, XON, XOFF, EOF, BRK */

    if (on) {
	(void) ioctl(SHIN, TIOCGETC, (ioctl_t) & tchars);
	tchars.t_brkc = ESC;
	(void) ioctl(SHIN, TIOCSETC, (ioctl_t) & tchars);
	/*
	 * This must be done after every command: if the tty gets into raw or
	 * cbreak mode the user can't even type 'reset'.
	 */
	(void) ioctl(SHIN, TIOCGETP, (ioctl_t) & sgtty);
	if (sgtty.sg_flags & (RAW | CBREAK)) {
	    sgtty.sg_flags &= ~(RAW | CBREAK);
	    (void) ioctl(SHIN, TIOCSETP, (ioctl_t) & sgtty);
	}
    }
    else {
	tchars.t_brkc = -1;
	(void) ioctl(SHIN, TIOCSETC, (ioctl_t) & tchars);
    }
#endif /* TERMIO */
}

/*
 * Move back to beginning of current line
 */
static void
back_to_col_1(void)
{
#ifdef TERMIO
# ifdef POSIX
    struct termios tty, tty_normal;
# else
    struct termio tty, tty_normal;
# endif /* POSIX */
#else
    struct sgttyb tty, tty_normal;
#endif /* TERMIO */

    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);

#ifdef TERMIO
# ifdef POSIX
    (void) tcgetattr(SHOUT, &tty);
# else
    (void) ioctl(SHOUT, TCGETA, (ioctl_t) &tty_normal);
# endif /* POSIX */
    tty_normal = tty;
    tty.c_iflag &= ~INLCR;
    tty.c_oflag &= ~ONLCR;
# ifdef POSIX
    (void) xtcsetattr(SHOUT, TCSANOW, &tty);
# else
    (void) ioctl(SHOUT, TCSETAW, (ioctl_t) &tty);
# endif /* POSIX */
    (void) xwrite(SHOUT, "\r", 1);
# ifdef POSIX
    (void) xtcsetattr(SHOUT, TCSANOW, &tty_normal);
# else
    (void) ioctl(SHOUT, TCSETAW, (ioctl_t) &tty_normal);
# endif /* POSIX */
#else
    (void) ioctl(SHIN, TIOCGETP, (ioctl_t) & tty);
    tty_normal = tty;
    tty.sg_flags &= ~CRMOD;
    (void) ioctl(SHIN, TIOCSETN, (ioctl_t) & tty);
    (void) xwrite(SHOUT, "\r", 1);
    (void) ioctl(SHIN, TIOCSETN, (ioctl_t) & tty_normal);
#endif /* TERMIO */

    cleanup_until(&pintr_disabled);
}

/*
 * Push string contents back into tty queue
 */
static void
pushback(const Char *string)
{
    const Char *p;
#ifdef TERMIO
# ifdef POSIX
    struct termios tty, tty_normal;
# else
    struct termio tty, tty_normal;
# endif /* POSIX */
#else
    struct sgttyb tty, tty_normal;
#endif /* TERMIO */

    pintr_disabled++;
    cleanup_push(&pintr_disabled, disabled_cleanup);

#ifdef TERMIO
# ifdef POSIX
    (void) tcgetattr(SHOUT, &tty);
# else
    (void) ioctl(SHOUT, TCGETA, (ioctl_t) &tty);
# endif /* POSIX */
    tty_normal = tty;
    tty.c_lflag &= ~(ECHOKE | ECHO | ECHOE | ECHOK | ECHONL |
#ifndef __QNXNTO__
	ECHOPRT |
#endif
	ECHOCTL);
# ifdef POSIX
    (void) xtcsetattr(SHOUT, TCSANOW, &tty);
# else
    (void) ioctl(SHOUT, TCSETAW, (ioctl_t) &tty);
# endif /* POSIX */

    for (p = string; *p != '\0'; p++) {
	char buf[MB_LEN_MAX];
	size_t i, len;

	len = one_wctomb(buf, *p);
	for (i = 0; i < len; i++)
	    (void) ioctl(SHOUT, TIOCSTI, (ioctl_t) &buf[i]);
    }
# ifdef POSIX
    (void) xtcsetattr(SHOUT, TCSANOW, &tty_normal);
# else
    (void) ioctl(SHOUT, TCSETAW, (ioctl_t) &tty_normal);
# endif /* POSIX */
#else
    (void) ioctl(SHOUT, TIOCGETP, (ioctl_t) & tty);
    tty_normal = tty;
    tty.sg_flags &= ~ECHO;
    (void) ioctl(SHOUT, TIOCSETN, (ioctl_t) & tty);

    for (p = string; c = *p; p++)
	(void) ioctl(SHOUT, TIOCSTI, (ioctl_t) & c);
    (void) ioctl(SHOUT, TIOCSETN, (ioctl_t) & tty_normal);
#endif /* TERMIO */

    cleanup_until(&pintr_disabled);
}

static int
filetype(const Char *dir, const Char *file)
{
    Char    *path;
    char *spath;
    struct stat statb;

    path = Strspl(dir, file);
    spath = short2str(path);
    xfree(path);
    if (lstat(spath, &statb) == 0) {
	switch (statb.st_mode & S_IFMT) {
	case S_IFDIR:
	    return ('/');

	case S_IFLNK:
	    if (stat(spath, &statb) == 0 &&	/* follow it out */
		S_ISDIR(statb.st_mode))
		return ('>');
	    else
		return ('@');

	case S_IFSOCK:
	    return ('=');

	default:
	    if (statb.st_mode & 0111)
		return ('*');
	}
    }
    return (' ');
}

/*
 * Print sorted down columns
 */
static void
print_by_column(const Char *dir, Char *items[], size_t count)
{
    struct winsize win;
    size_t i;
    int rows, r, c, maxwidth = 0, columns;

    if (ioctl(SHOUT, TIOCGWINSZ, (ioctl_t) & win) < 0 || win.ws_col == 0)
	win.ws_col = 80;
    for (i = 0; i < count; i++)
	maxwidth = maxwidth > (r = Strlen(items[i])) ? maxwidth : r;
    maxwidth += 2;		/* for the file tag and space */
    columns = win.ws_col / maxwidth;
    if (columns == 0)
	columns = 1;
    rows = (count + (columns - 1)) / columns;
    for (r = 0; r < rows; r++) {
	for (c = 0; c < columns; c++) {
	    i = c * rows + r;
	    if (i < count) {
		int w;

		xprintf("%S", items[i]);
		xputchar(dir ? filetype(dir, items[i]) : ' ');
		if (c < columns - 1) {	/* last column? */
		    w = Strlen(items[i]) + 1;
		    for (; w < maxwidth; w++)
			xputchar(' ');
		}
	    }
	}
	xputchar('\r');
	xputchar('\n');
    }
}

/*
 * Expand file name with possible tilde usage
 *	~person/mumble
 * expands to
 *	home_directory_of_person/mumble
 */
static Char *
tilde(const Char *old)
{
    const Char *o, *home;
    struct passwd *pw;

    if (old[0] != '~')
	return (Strsave(old));
    old++;

    for (o = old; *o != '\0' && *o != '/'; o++)
	;
    if (o == old)
	home = varval(STRhome);
    else {
	Char *person;

	person = Strnsave(old, o - old);
	pw = xgetpwnam(short2str(person));
	xfree(person);
	if (pw == NULL)
	    return (NULL);
	home = str2short(pw->pw_dir);
    }
    return Strspl(home, o);
}

/*
 * Cause pending line to be printed
 */
static void
retype(void)
{
#ifdef TERMIO
# ifdef POSIX
    struct termios tty;

    (void) tcgetattr(SHOUT, &tty);
# else
    struct termio tty;

    (void) ioctl(SHOUT, TCGETA, (ioctl_t) &tty);
# endif /* POSIX */

#ifndef __QNXNTO__
    tty.c_lflag |= PENDIN;
#endif

# ifdef POSIX
    (void) xtcsetattr(SHOUT, TCSANOW, &tty);
# else
    (void) ioctl(SHOUT, TCSETAW, (ioctl_t) &tty);
# endif /* POSIX */
#else
    int     pending_input = LPENDIN;

    (void) ioctl(SHOUT, TIOCLBIS, (ioctl_t) & pending_input);
#endif /* TERMIO */
}

static void
beep(void)
{
    if (adrof(STRnobeep) == 0)
#ifdef IS_ASCII
	(void) xwrite(SHOUT, "\007", 1);
#else
    {
	unsigned char beep_ch = CTL_ESC('\007');
	(void) xwrite(SHOUT, &beep_ch, 1);
    }
#endif
}

/*
 * Erase that silly ^[ and
 * print the recognized part of the string
 */
static void
print_recognized_stuff(const Char *recognized_part)
{
    /* An optimized erasing of that silly ^[ */
    (void) putraw('\b');
    (void) putraw('\b');
    switch (Strlen(recognized_part)) {

    case 0:			/* erase two Characters: ^[ */
	(void) putraw(' ');
	(void) putraw(' ');
	(void) putraw('\b');
	(void) putraw('\b');
	break;

    case 1:			/* overstrike the ^, erase the [ */
	xprintf("%S", recognized_part);
	(void) putraw(' ');
	(void) putraw('\b');
	break;

    default:			/* overstrike both Characters ^[ */
	xprintf("%S", recognized_part);
	break;
    }
    flush();
}

/*
 * Parse full path in file into 2 parts: directory and file names
 * Should leave final slash (/) at end of dir.
 */
static void
extract_dir_and_name(const Char *path, Char **dir, const Char **name)
{
    const Char *p;

    p = Strrchr(path, '/');
    if (p == NULL)
	p = path;
    else
	p++;
    *name = p;
    *dir = Strnsave(path, p - path);
}

static Char *
getitem(DIR *dir_fd, int looking_for_lognames)
{
    struct passwd *pw;
    struct dirent *dirp;

    if (looking_for_lognames) {
#ifndef HAVE_GETPWENT
	    return (NULL);
#else
	if ((pw = getpwent()) == NULL)
	    return (NULL);
	return (str2short(pw->pw_name));
#endif /* atp vmsposix */
    }
    if ((dirp = readdir(dir_fd)) != NULL)
	return (str2short(dirp->d_name));
    return (NULL);
}

/*
 * Perform a RECOGNIZE or LIST command on string "word".
 */
static size_t
tsearch(Char *word, COMMAND command, size_t max_word_length)
{
    DIR *dir_fd;
    int ignoring = TRUE, nignored = 0;
    int looking_for_lognames;
    Char *tilded_dir = NULL, *dir = NULL;
    Char *extended_name = NULL;
    const Char *name;
    Char *item;
    struct blk_buf items = BLK_BUF_INIT;
    size_t name_length;

    looking_for_lognames = (*word == '~') && (Strchr(word, '/') == NULL);
    if (looking_for_lognames) {
#ifdef HAVE_GETPWENT
	(void) setpwent();
#endif
	name = word + 1;	/* name sans ~ */
	dir_fd = NULL;
	cleanup_push(dir, xfree);
    }
    else {
	extract_dir_and_name(word, &dir, &name);
	cleanup_push(dir, xfree);
	tilded_dir = tilde(dir);
	if (tilded_dir == NULL)
	    goto end;
	cleanup_push(tilded_dir, xfree);
	dir_fd = opendir(*tilded_dir ? short2str(tilded_dir) : ".");
	if (dir_fd == NULL)
	    goto end;
    }

    name_length = Strlen(name);
    cleanup_push(&extended_name, xfree_indirect);
    cleanup_push(&items, bb_cleanup);
again:				/* search for matches */
    while ((item = getitem(dir_fd, looking_for_lognames)) != NULL) {
	if (!is_prefix(name, item))
	    continue;
	/* Don't match . files on null prefix match */
	if (name_length == 0 && item[0] == '.' &&
	    !looking_for_lognames)
	    continue;
	if (command == LIST)
	    bb_append(&items, Strsave(item));
	else {			/* RECOGNIZE command */
	    if (ignoring && ignored(item))
		nignored++;
	    else if (recognize(&extended_name, item, name_length, ++items.len))
		break;
	}
    }
    if (ignoring && items.len == 0 && nignored > 0) {
	ignoring = FALSE;
	nignored = 0;
	if (looking_for_lognames) {
#ifdef HAVE_GETPWENT
	    (void) setpwent();
#endif /* atp vmsposix */
	} else
	    rewinddir(dir_fd);
	goto again;
    }

    if (looking_for_lognames) {
#ifdef HAVE_GETPWENT
	(void) endpwent();
#endif
    } else
	xclosedir(dir_fd);
    if (items.len != 0) {
	if (command == RECOGNIZE) {
	    if (looking_for_lognames)
		copyn(word, STRtilde, 2);/*FIXBUF, sort of */
	    else
		/* put back dir part */
		copyn(word, dir, max_word_length);/*FIXBUF*/
	    /* add extended name */
	    catn(word, extended_name, max_word_length);/*FIXBUF*/
	}
	else {			/* LIST */
	    qsort(items.vec, items.len, sizeof(items.vec[0]), compare);
	    print_by_column(looking_for_lognames ? NULL : tilded_dir,
			    items.vec, items.len);
	}
    }
 end:
    cleanup_until(dir);
    return items.len;
}


static int
compare(const void *p, const void *q)
{
#if defined (WIDE_STRINGS) && !defined (UTF16_STRING)
    errno = 0;

    return (wcscoll(*(Char *const *) p, *(Char *const *) q));
#else
    char *p1, *q1;
    int res;

    p1 = strsave(short2str(*(Char *const *) p));
    q1 = strsave(short2str(*(Char *const *) q));
# if defined(NLS) && defined(HAVE_STRCOLL)
    res = strcoll(p1, q1);
# else
    res = strcmp(p1, q1);
# endif /* NLS && HAVE_STRCOLL */
    xfree (p1);
    xfree (q1);
    return res;
#endif /* not WIDE_STRINGS */
}

/*
 * Object: extend what user typed up to an ambiguity.
 * Algorithm:
 * On first match, copy full item (assume it'll be the only match)
 * On subsequent matches, shorten extended_name to the first
 * Character mismatch between extended_name and item.
 * If we shorten it back to the prefix length, stop searching.
 */
static int
recognize(Char **extended_name, Char *item, size_t name_length,
	  size_t numitems)
{
    if (numitems == 1)		/* 1st match */
	*extended_name = Strsave(item);
    else {			/* 2nd & subsequent matches */
	Char *x, *ent;
	size_t len = 0;

	x = *extended_name;
	for (ent = item; *x && *x == *ent++; x++, len++);
	*x = '\0';		/* Shorten at 1st Char diff */
	if (len == name_length)	/* Ambiguous to prefix? */
	    return (-1);	/* So stop now and save time */
    }
    return (0);
}

/*
 * Return true if check matches initial Chars in template.
 * This differs from PWB imatch in that if check is null
 * it matches anything.
 */
static int
is_prefix(const Char *check, const Char *template)
{
    do
	if (*check == 0)
	    return (TRUE);
    while (*check++ == *template++);
    return (FALSE);
}

/*
 *  Return true if the Chars in template appear at the
 *  end of check, I.e., are it's suffix.
 */
static int
is_suffix(const Char *check, const Char *template)
{
    const Char *c, *t;

    for (c = check; *c++;);
    for (t = template; *t++;);
    for (;;) {
	if (t == template)
	    return 1;
	if (c == check || *--t != *--c)
	    return 0;
    }
}

static void
setup_tty_cleanup(void *dummy)
{
    USE(dummy);
    setup_tty(OFF);
}

size_t
tenex(Char *inputline, size_t inputline_size)
{
    size_t numitems;
    ssize_t num_read;
    char    tinputline[BUFSIZE + 1];/*FIXBUF*/

    setup_tty(ON);
    cleanup_push(&num_read, setup_tty_cleanup); /* num_read is only a marker */

    while ((num_read = xread(SHIN, tinputline, BUFSIZE)) > 0) {/*FIXBUF*/
	static const Char delims[] = {' ', '\'', '"', '\t', ';', '&', '<',
	'>', '(', ')', '|', '^', '%', '\0'};
	Char *str_end, *word_start, last_Char, should_retype;
	size_t space_left;
	COMMAND command;

	tinputline[num_read] = 0;
	Strcpy(inputline, str2short(tinputline));/*FIXBUF*/
	num_read = Strlen(inputline);
	last_Char = CTL_ESC(ASC(inputline[num_read - 1]) & ASCII);

	if (last_Char == '\n' || (size_t)num_read == inputline_size)
	    break;
	command = (last_Char == ESC) ? RECOGNIZE : LIST;
	if (command == LIST)
	    xputchar('\n');
	str_end = &inputline[num_read];
	if (last_Char == ESC)
	    --str_end;		/* wipeout trailing cmd Char */
	*str_end = '\0';
	/*
	 * Find LAST occurence of a delimiter in the inputline. The word start
	 * is one Character past it.
	 */
	for (word_start = str_end; word_start > inputline; --word_start)
	    if (Strchr(delims, word_start[-1]))
		break;
	space_left = inputline_size - (word_start - inputline) - 1;
	numitems = tsearch(word_start, command, space_left);

	if (command == RECOGNIZE) {
	    /* print from str_end on */
	    print_recognized_stuff(str_end);
	    if (numitems != 1)	/* Beep = No match/ambiguous */
		beep();
	}

	/*
	 * Tabs in the input line cause trouble after a pushback. tty driver
	 * won't backspace over them because column positions are now
	 * incorrect. This is solved by retyping over current line.
	 */
	should_retype = FALSE;
	if (Strchr(inputline, '\t')) {	/* tab Char in input line? */
	    back_to_col_1();
	    should_retype = TRUE;
	}
	if (command == LIST)	/* Always retype after a LIST */
	    should_retype = TRUE;
	if (should_retype)
	    printprompt(0, NULL);
	pushback(inputline);
	if (should_retype)
	    retype();
    }
    cleanup_until(&num_read);
    return (num_read);
}

static int
ignored(const Char *item)
{
    struct varent *vp;
    Char **cp;

    if ((vp = adrof(STRfignore)) == NULL || (cp = vp->vec) == NULL)
	return (FALSE);
    for (; *cp != NULL; cp++)
	if (is_suffix(item, *cp))
	    return (TRUE);
    return (FALSE);
}
#endif	/* FILEC && TIOCSTI */
