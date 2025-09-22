/*	$OpenBSD: file.c,v 1.40 2020/10/06 01:40:43 deraadt Exp $	*/
/*	$NetBSD: file.c,v 1.11 1996/11/08 19:34:37 christos Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "csh.h"
#include "extern.h"

/*
 * Tenex style file name recognition, .. and more.
 * History:
 *	Author: Ken Greer, Sept. 1975, CMU.
 *	Finally got around to adding to the Cshell., Ken Greer, Dec. 1981.
 */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define	ESC		'\033'
#define	TABWIDTH	8

typedef enum {
	LIST,
	RECOGNIZE
} COMMAND;

struct cmdline {
	int	 fdin;
	int	 fdout;
	int	 istty;
	int	 flags;
#define	CL_ALTWERASE	0x1
#define	CL_PROMPT	0x2
	char	*buf;
	size_t	 len;
	size_t	 size;
	size_t	 cursor;
};

/* Command line auxiliary functions. */
static void	 cl_beep(struct cmdline *);
static void	 cl_flush(struct cmdline *);
static int	 cl_getc(struct cmdline *);
static Char	*cl_lastw(struct cmdline *);
static void	 cl_putc(struct cmdline *, int);
static void	 cl_visc(struct cmdline *, int);

/* Command line editing functions. */
static int	cl_abort(struct cmdline *, int);
static int	cl_erasec(struct cmdline *, int);
static int	cl_erasew(struct cmdline *, int);
static int	cl_insert(struct cmdline *, int);
static int	cl_kill(struct cmdline *, int);
static int	cl_list(struct cmdline *, int);
static int	cl_literal(struct cmdline *, int);
static int	cl_recognize(struct cmdline *, int);
static int	cl_reprint(struct cmdline *, int);
static int	cl_status(struct cmdline *, int);

static const struct termios	*setup_tty(int);

static void	 catn(Char *, Char *, int);
static void	 copyn(Char *, Char *, int);
static Char	 filetype(Char *, Char *);
static void	 print_by_column(Char *, Char *[], int);
static Char	*tilde(Char *, Char *);
static void	 extract_dir_and_name(Char *, Char *, Char *);
static Char	*getentry(DIR *, int);
static void	 free_items(Char **, int);
static int	 tsearch(Char *, COMMAND, int);
static int	 recognize(Char *, Char *, int, int);
static int	 is_prefix(Char *, Char *);
static int	 is_suffix(Char *, Char *);
static int	 ignored(Char *);

/*
 * Put this here so the binary can be patched with adb to enable file
 * completion by default.  Filec controls completion, nobeep controls
 * ringing the terminal bell on incomplete expansions.
 */
bool    filec = 0;

static void
cl_flush(struct cmdline *cl)
{
	size_t	i, len;
	int	c;

	if (cl->flags & CL_PROMPT) {
		cl->flags &= ~CL_PROMPT;
		printprompt();
	}

	if (cl->cursor < cl->len) {
		for (; cl->cursor < cl->len; cl->cursor++)
			cl_visc(cl, cl->buf[cl->cursor]);
	} else if (cl->cursor > cl->len) {
		len = cl->cursor - cl->len;
		for (i = len; i > 0; i--) {
			c = cl->buf[--cl->cursor];
			if (c == '\t')
				len += TABWIDTH - 1;
			else if (iscntrl(c))
				len++;	/* account for leading ^ */
		}
		for (i = 0; i < len; i++)
			cl_putc(cl, '\b');
		for (i = 0; i < len; i++)
			cl_putc(cl, ' ');
		for (i = 0; i < len; i++)
			cl_putc(cl, '\b');
		cl->cursor = cl->len;
	}
}

static int
cl_getc(struct cmdline *cl)
{
	ssize_t		n;
	unsigned char	c;

	for (;;) {
		n = read(cl->fdin, &c, 1);
		switch (n) {
		case -1:
			if (errno == EINTR)
				continue;
			/* FALLTHROUGH */
		case 0:
			return 0;
		default:
			return c & 0x7F;
		}
	}
}

static Char *
cl_lastw(struct cmdline *cl)
{
	static Char		 word[BUFSIZ];
	const unsigned char	*delimiters = " '\"\t;&<>()|^%";
	Char			*cp;
	size_t			 i;

	for (i = cl->len; i > 0; i--)
		if (strchr(delimiters, cl->buf[i - 1]) != NULL)
			break;

	cp = word;
	for (; i < cl->len; i++)
		*cp++ = cl->buf[i];
	*cp = '\0';

	return word;
}

static void
cl_putc(struct cmdline *cl, int c)
{
	unsigned char	cc = c;

	write(cl->fdout, &cc, 1);
}

static void
cl_visc(struct cmdline *cl, int c)
{
#define	UNCNTRL(x)	((x) == 0x7F ? '?' : ((x) | 0x40))
	int	i;

	if (c == '\t') {
		for (i = 0; i < TABWIDTH; i++)
			cl_putc(cl, ' ');
	} else if (c != '\n' && iscntrl(c)) {
		cl_putc(cl, '^');
		cl_putc(cl, UNCNTRL(c));
	} else {
		cl_putc(cl, c);
	}
}

static int
cl_abort(struct cmdline *cl, int c)
{
	cl_visc(cl, c);

	/* Abort while/foreach loop prematurely. */
	if (whyles) {
		if (cl->istty)
			setup_tty(0);
		kill(getpid(), SIGINT);
	}

	cl_putc(cl, '\n');
	cl->len = cl->cursor = 0;
	cl->flags |= CL_PROMPT;

	return 0;
}

static int
cl_erasec(struct cmdline *cl, int c)
{
	if (cl->len > 0)
		cl->len--;

	return 0;
}

static int
cl_erasew(struct cmdline *cl, int c)
{
	const unsigned char	*ws = " \t";

	for (; cl->len > 0; cl->len--)
		if (strchr(ws, cl->buf[cl->len - 1]) == NULL &&
		    ((cl->flags & CL_ALTWERASE) == 0 ||
		     isalpha(cl->buf[cl->len - 1])))
			break;
	for (; cl->len > 0; cl->len--)
		if (strchr(ws, cl->buf[cl->len - 1]) != NULL ||
		    ((cl->flags & CL_ALTWERASE) &&
		     !isalpha(cl->buf[cl->len - 1])))
			break;

	return 0;
}

static void
cl_beep(struct cmdline *cl)
{
	if (adrof(STRnobeep) == 0)
		cl_putc(cl, '\007');
}

static int
cl_insert(struct cmdline *cl, int c)
{
	if (cl->len == cl->size)
		return 1;

	cl->buf[cl->len++] = c;

	if (c == '\n')
		return 1;

	return 0;
}

static int
cl_kill(struct cmdline *cl, int c)
{
	cl->len = 0;

	return 0;
}

static int
cl_list(struct cmdline *cl, int c)
{
	Char	*word;
	size_t	 len;

	if (adrof(STRignoreeof) || cl->len > 0)
		cl_visc(cl, c);

	if (cl->len == 0)
		return 1;

	cl_putc(cl, '\n');
	cl->cursor = 0;
	cl->flags |= CL_PROMPT;

	word = cl_lastw(cl);
	len = Strlen(word);
	tsearch(word, LIST, BUFSIZ - len - 1);	/* NUL */

	return 0;
}

static int
cl_literal(struct cmdline *cl, int c)
{
	int	literal;

	literal = cl_getc(cl);
	if (literal == '\n')
		literal = '\r';
	cl_insert(cl, literal);

	return 0;
}

static int
cl_recognize(struct cmdline *cl, int c)
{
	Char	*word;
	size_t	 len;
	int	 nitems;

	if (cl->len == 0) {
		cl_beep(cl);
		return 0;
	}

	word = cl_lastw(cl);
	len = Strlen(word);
	nitems = tsearch(word, RECOGNIZE, BUFSIZ - len - 1);	/* NUL */
	for (word += len; *word != '\0'; word++)
		cl_insert(cl, *word);
	if (nitems != 1)
		cl_beep(cl);

	return 0;
}

static int
cl_reprint(struct cmdline *cl, int c)
{
	cl_visc(cl, c);
	cl_putc(cl, '\n');
	cl->cursor = 0;

	return 0;
}

static int
cl_status(struct cmdline *cl, int c)
{
	cl->cursor = 0;
	if (cl->istty)
		ioctl(cl->fdin, TIOCSTAT);

	return 0;
}

const struct termios *
setup_tty(int on)
{
	static struct termios	newtio, oldtio;

	if (on) {
		tcgetattr(SHIN, &oldtio);

		newtio = oldtio;
		newtio.c_lflag &= ~(ECHO | ICANON | ISIG);
		newtio.c_cc[VEOL] = ESC;
		newtio.c_cc[VLNEXT] = _POSIX_VDISABLE;
		newtio.c_cc[VMIN] = 1;
		newtio.c_cc[VTIME] = 0;
	} else {
		newtio = oldtio;
	}

	tcsetattr(SHIN, TCSADRAIN, &newtio);

	/*
	 * Since VLNEXT is disabled, restore its previous value in order to make
	 * the key detectable.
	 */
	newtio.c_cc[VLNEXT] = oldtio.c_cc[VLNEXT];

	return &newtio;
}

/*
 * Concatenate src onto tail of des.
 * Des is a string whose maximum length is count.
 * Always null terminate.
 */
static void
catn(Char *des, Char *src, int count)
{
    while (--count >= 0 && *des)
	des++;
    while (--count >= 0)
	if ((*des++ = *src++) == 0)
	    return;
    *des = '\0';
}

/*
 * Places Char's like strlcpy, but no special return value.
 */
static void
copyn(Char *des, Char *src, int count)
{
    while (--count >= 0)
	if ((*des++ = *src++) == 0)
	    return;
    *des = '\0';
}

static  Char
filetype(Char *dir, Char *file)
{
    Char    path[PATH_MAX];
    struct stat statb;

    Strlcpy(path, dir, sizeof path/sizeof(Char));
    catn(path, file, sizeof(path) / sizeof(Char));
    if (lstat(short2str(path), &statb) == 0) {
	switch (statb.st_mode & S_IFMT) {
	case S_IFDIR:
	    return ('/');

	case S_IFLNK:
	    if (stat(short2str(path), &statb) == 0 &&	/* follow it out */
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
print_by_column(Char *dir, Char *items[], int count)
{
    struct winsize win;
    int i, rows, r, c, maxwidth = 0, columns;

    if (ioctl(SHOUT, TIOCGWINSZ, (ioctl_t) & win) == -1 || win.ws_col == 0)
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

		(void) fprintf(cshout, "%s", vis_str(items[i]));
		(void) fputc(dir ? filetype(dir, items[i]) : ' ', cshout);
		if (c < columns - 1) {	/* last column? */
		    w = Strlen(items[i]) + 1;
		    for (; w < maxwidth; w++)
			(void) fputc(' ', cshout);
		}
	    }
	}
	(void) fputc('\r', cshout);
	(void) fputc('\n', cshout);
    }
}

/*
 * Expand file name with possible tilde usage
 *	~person/mumble
 * expands to
 *	home_directory_of_person/mumble
 */
static Char *
tilde(Char *new, Char *old)
{
    Char *o, *p;
    struct passwd *pw;
    static Char person[40];

    if (old[0] != '~') {
	Strlcpy(new, old, PATH_MAX);
	return new;
    }

    for (p = person, o = &old[1]; *o && *o != '/'; *p++ = *o++)
	continue;
    *p = '\0';
    if (person[0] == '\0')
	(void) Strlcpy(new, value(STRhome), PATH_MAX);
    else {
	pw = getpwnam(short2str(person));
	if (pw == NULL)
	    return (NULL);
	(void) Strlcpy(new, str2short(pw->pw_dir), PATH_MAX);
    }
    (void) Strlcat(new, o, PATH_MAX);
    return (new);
}

/*
 * Parse full path in file into 2 parts: directory and file names
 * Should leave final slash (/) at end of dir.
 */
static void
extract_dir_and_name(Char *path, Char *dir, Char *name)
{
    Char *p;

    p = Strrchr(path, '/');
    if (p == NULL) {
	copyn(name, path, MAXNAMLEN);
	dir[0] = '\0';
    }
    else {
	copyn(name, ++p, MAXNAMLEN);
	copyn(dir, path, p - path);
    }
}

static Char *
getentry(DIR *dir_fd, int looking_for_lognames)
{
    struct passwd *pw;
    struct dirent *dirp;

    if (looking_for_lognames) {
	if ((pw = getpwent()) == NULL)
	    return (NULL);
	return (str2short(pw->pw_name));
    }
    if ((dirp = readdir(dir_fd)) != NULL)
	return (str2short(dirp->d_name));
    return (NULL);
}

static void
free_items(Char **items, int numitems)
{
    int i;

    for (i = 0; i < numitems; i++)
	free(items[i]);
    free(items);
}

#define FREE_ITEMS(items) { \
	sigset_t sigset, osigset;\
\
	sigemptyset(&sigset);\
	sigaddset(&sigset, SIGINT);\
	sigprocmask(SIG_BLOCK, &sigset, &osigset);\
	free_items(items, numitems);\
	sigprocmask(SIG_SETMASK, &osigset, NULL);\
}

/*
 * Perform a RECOGNIZE or LIST command on string "word".
 */
static int
tsearch(Char *word, COMMAND command, int max_word_length)
{
    DIR *dir_fd;
    int numitems = 0, ignoring = TRUE, nignored = 0;
    int name_length, looking_for_lognames;
    Char    tilded_dir[PATH_MAX], dir[PATH_MAX];
    Char    name[MAXNAMLEN + 1], extended_name[MAXNAMLEN + 1];
    Char   *entry;
    Char   **items = NULL;
    size_t  maxitems = 0;

    looking_for_lognames = (*word == '~') && (Strchr(word, '/') == NULL);
    if (looking_for_lognames) {
	(void) setpwent();
	copyn(name, &word[1], MAXNAMLEN);	/* name sans ~ */
	dir_fd = NULL;
    }
    else {
	extract_dir_and_name(word, dir, name);
	if (tilde(tilded_dir, dir) == 0)
	    return (0);
	dir_fd = opendir(*tilded_dir ? short2str(tilded_dir) : ".");
	if (dir_fd == NULL)
	    return (0);
    }

again:				/* search for matches */
    name_length = Strlen(name);
    for (numitems = 0; (entry = getentry(dir_fd, looking_for_lognames)) != NULL;) {
	if (!is_prefix(name, entry))
	    continue;
	/* Don't match . files on null prefix match */
	if (name_length == 0 && entry[0] == '.' &&
	    !looking_for_lognames)
	    continue;
	if (command == LIST) {
	    if (numitems >= maxitems) {
		maxitems += 1024;
		items = xreallocarray(items, maxitems, sizeof(*items));
	    }
	    items[numitems] = xreallocarray(NULL, (Strlen(entry) + 1), sizeof(Char));
	    copyn(items[numitems], entry, MAXNAMLEN);
	    numitems++;
	}
	else {			/* RECOGNIZE command */
	    if (ignoring && ignored(entry))
		nignored++;
	    else if (recognize(extended_name,
			       entry, name_length, ++numitems))
		break;
	}
    }
    if (ignoring && numitems == 0 && nignored > 0) {
	ignoring = FALSE;
	nignored = 0;
	if (looking_for_lognames)
	    (void) setpwent();
	else
	    rewinddir(dir_fd);
	goto again;
    }

    if (looking_for_lognames)
	(void) endpwent();
    else
	(void) closedir(dir_fd);
    if (numitems == 0)
	return (0);
    if (command == RECOGNIZE) {
	if (looking_for_lognames)
	    copyn(word, STRtilde, 1);
	else
	    /* put back dir part */
	    copyn(word, dir, max_word_length);
	/* add extended name */
	catn(word, extended_name, max_word_length);
	return (numitems);
    }
    else {			/* LIST */
	qsort(items, numitems, sizeof(*items), sortscmp);
	print_by_column(looking_for_lognames ? NULL : tilded_dir,
			items, numitems);
	if (items != NULL)
	    FREE_ITEMS(items);
    }
    return (0);
}

/*
 * Object: extend what user typed up to an ambiguity.
 * Algorithm:
 * On first match, copy full entry (assume it'll be the only match)
 * On subsequent matches, shorten extended_name to the first
 * Character mismatch between extended_name and entry.
 * If we shorten it back to the prefix length, stop searching.
 */
static int
recognize(Char *extended_name, Char *entry, int name_length, int numitems)
{
    if (numitems == 1)		/* 1st match */
	copyn(extended_name, entry, MAXNAMLEN);
    else {			/* 2nd & subsequent matches */
	Char *x, *ent;
	int len = 0;

	x = extended_name;
	for (ent = entry; *x && *x == *ent++; x++, len++)
	    continue;
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
is_prefix(Char *check, Char *template)
{
    do
	if (*check == 0)
	    return (TRUE);
    while (*check++ == *template++);
    return (FALSE);
}

/*
 *  Return true if the Chars in template appear at the
 *  end of check, I.e., are its suffix.
 */
static int
is_suffix(Char *check, Char *template)
{
    Char *c, *t;

    for (c = check; *c++;)
	continue;
    for (t = template; *t++;)
	continue;
    for (;;) {
	if (t == template)
	    return 1;
	if (c == check || *--t != *--c)
	    return 0;
    }
}

int
tenex(Char *inputline, int inputline_size)
{
	static struct {
		int	(*fn)(struct cmdline *, int);
		int	idx;
	}			 keys[] = {
		{ cl_abort,	VINTR },
		{ cl_erasec,	VERASE },
		{ cl_erasew,	VWERASE },
		{ cl_kill,	VKILL },
		{ cl_list,	VEOF },
		{ cl_literal,	VLNEXT },
		{ cl_recognize,	VEOL },
		{ cl_reprint,	VREPRINT },
		{ cl_status,	VSTATUS },
		{ cl_insert,	-1 }
	};
	unsigned char		 buf[BUFSIZ];
	const struct termios	*tio;
	struct cmdline		 cl;
	size_t			 i;
	int			 c, ret;

	memset(&cl, 0, sizeof(cl));
	cl.fdin = SHIN;
	cl.fdout = SHOUT;
	cl.istty = isatty(SHIN);

	if (cl.istty)
		tio = setup_tty(1);

	cl.buf = buf;
	cl.size = sizeof(buf);
	if (inputline_size < cl.size)
		cl.size = inputline_size;
	if (cl.istty && tio->c_lflag & ALTWERASE)
		cl.flags |= CL_ALTWERASE;
	if (needprompt) {
		needprompt = 0;
		cl.flags |= CL_PROMPT;
		cl_flush(&cl);
	}

	for (;;) {
		if ((c = cl_getc(&cl)) == 0)
			break;

		for (i = 0; keys[i].idx >= 0; i++)
			if (cl.istty && CCEQ(tio->c_cc[keys[i].idx], c))
				break;
		ret = keys[i].fn(&cl, c);
		cl_flush(&cl);
		if (ret)
			break;
	}

	if (cl.istty)
		setup_tty(0);

	for (i = 0; i < cl.len; i++)
		inputline[i] = cl.buf[i];
	/*
	 * NUL-terminating the buffer implies that it contains a complete
	 * command ready to be executed. Therefore, don't terminate if the
	 * buffer is full since more characters must be read in order to form a
	 * complete command.
	 */
	if (i < cl.size)
		inputline[i] = '\0';

	return cl.len;
}

static int
ignored(Char *entry)
{
    struct varent *vp;
    Char **cp;

    if ((vp = adrof(STRfignore)) == NULL || (cp = vp->vec) == NULL)
	return (FALSE);
    for (; *cp != NULL; cp++)
	if (is_suffix(entry, *cp))
	    return (TRUE);
    return (FALSE);
}
