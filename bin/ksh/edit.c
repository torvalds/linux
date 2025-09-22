/*	$OpenBSD: edit.c,v 1.71 2024/04/23 13:34:50 jsg Exp $	*/

/*
 * Command line editing - common code
 *
 */

#include "config.h"

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"
#include "edit.h"
#include "tty.h"

X_chars edchars;

static void x_sigwinch(int);
volatile sig_atomic_t got_sigwinch;
static void check_sigwinch(void);

static int	x_file_glob(int, const char *, int, char ***);
static int	x_command_glob(int, const char *, int, char ***);
static int	x_locate_word(const char *, int, int, int *, int *);


/* Called from main */
void
x_init(void)
{
	/* set to -2 to force initial binding */
	edchars.erase = edchars.kill = edchars.intr = edchars.quit =
	    edchars.eof = -2;
	/* default value for deficient systems */
	edchars.werase = 027;	/* ^W */

	if (setsig(&sigtraps[SIGWINCH], x_sigwinch, SS_RESTORE_ORIG|SS_SHTRAP))
		sigtraps[SIGWINCH].flags |= TF_SHELL_USES;
	got_sigwinch = 1; /* force initial check */
	check_sigwinch();

#ifdef EMACS
	x_init_emacs();
#endif /* EMACS */
}

static void
x_sigwinch(int sig)
{
	got_sigwinch = 1;
}

static void
check_sigwinch(void)
{
	if (got_sigwinch) {
		struct winsize ws;

		got_sigwinch = 0;
		if (procpid == kshpid && ioctl(tty_fd, TIOCGWINSZ, &ws) == 0) {
			struct tbl *vp;

			/* Do NOT export COLUMNS/LINES.  Many applications
			 * check COLUMNS/LINES before checking ws.ws_col/row,
			 * so if the app is started with C/L in the environ
			 * and the window is then resized, the app won't
			 * see the change cause the environ doesn't change.
			 */
			if (ws.ws_col) {
				x_cols = ws.ws_col < MIN_COLS ? MIN_COLS :
				    ws.ws_col;

				if ((vp = typeset("COLUMNS", 0, 0, 0, 0)))
					setint(vp, (int64_t) ws.ws_col);
			}
			if (ws.ws_row && (vp = typeset("LINES", 0, 0, 0, 0)))
				setint(vp, (int64_t) ws.ws_row);
		}
	}
}

/*
 * read an edited command line
 */
int
x_read(char *buf, size_t len)
{
	int	i;

	x_mode(true);
#ifdef EMACS
	if (Flag(FEMACS) || Flag(FGMACS))
		i = x_emacs(buf, len);
	else
#endif
#ifdef VI
	if (Flag(FVI))
		i = x_vi(buf, len);
	else
#endif
		i = -1;		/* internal error */
	x_mode(false);
	check_sigwinch();
	return i;
}

/* tty I/O */

int
x_getc(void)
{
	char c;
	int n;

	while ((n = blocking_read(STDIN_FILENO, &c, 1)) < 0 && errno == EINTR)
		if (trap) {
			x_mode(false);
			runtraps(0);
			x_mode(true);
		}
	if (n != 1)
		return -1;
	return (int) (unsigned char) c;
}

void
x_flush(void)
{
	shf_flush(shl_out);
}

int
x_putc(int c)
{
	return shf_putc(c, shl_out);
}

void
x_puts(const char *s)
{
	while (*s != 0)
		shf_putc(*s++, shl_out);
}

bool
x_mode(bool onoff)
{
	static bool	x_cur_mode;
	bool		prev;

	if (x_cur_mode == onoff)
		return x_cur_mode;
	prev = x_cur_mode;
	x_cur_mode = onoff;

	if (onoff) {
		struct termios	cb;
		X_chars		oldchars;

		oldchars = edchars;
		cb = tty_state;

		edchars.erase = cb.c_cc[VERASE];
		edchars.kill = cb.c_cc[VKILL];
		edchars.intr = cb.c_cc[VINTR];
		edchars.quit = cb.c_cc[VQUIT];
		edchars.eof = cb.c_cc[VEOF];
		edchars.werase = cb.c_cc[VWERASE];
		cb.c_iflag &= ~(INLCR|ICRNL);
		cb.c_lflag &= ~(ISIG|ICANON|ECHO);
		/* osf/1 processes lnext when ~icanon */
		cb.c_cc[VLNEXT] = _POSIX_VDISABLE;
		/* sunos 4.1.x & osf/1 processes discard(flush) when ~icanon */
		cb.c_cc[VDISCARD] = _POSIX_VDISABLE;
		cb.c_cc[VTIME] = 0;
		cb.c_cc[VMIN] = 1;

		tcsetattr(tty_fd, TCSADRAIN, &cb);

		/* Convert unset values to internal `unset' value */
		if (edchars.erase == _POSIX_VDISABLE)
			edchars.erase = -1;
		if (edchars.kill == _POSIX_VDISABLE)
			edchars.kill = -1;
		if (edchars.intr == _POSIX_VDISABLE)
			edchars.intr = -1;
		if (edchars.quit == _POSIX_VDISABLE)
			edchars.quit = -1;
		if (edchars.eof == _POSIX_VDISABLE)
			edchars.eof = -1;
		if (edchars.werase == _POSIX_VDISABLE)
			edchars.werase = -1;
		if (memcmp(&edchars, &oldchars, sizeof(edchars)) != 0) {
#ifdef EMACS
			x_emacs_keys(&edchars);
#endif
		}
	} else {
		tcsetattr(tty_fd, TCSADRAIN, &tty_state);
	}

	return prev;
}

void
set_editmode(const char *ed)
{
	static const enum sh_flag edit_flags[] = {
#ifdef EMACS
		FEMACS, FGMACS,
#endif
#ifdef VI
		FVI,
#endif
	};
	char *rcp;
	unsigned int ele;

	if ((rcp = strrchr(ed, '/')))
		ed = ++rcp;
	for (ele = 0; ele < NELEM(edit_flags); ele++)
		if (strstr(ed, sh_options[(int) edit_flags[ele]].name)) {
			change_flag(edit_flags[ele], OF_SPECIAL, 1);
			return;
		}
}

/* ------------------------------------------------------------------------- */
/*           Misc common code for vi/emacs				     */

/* Handle the commenting/uncommenting of a line.
 * Returns:
 *	1 if a carriage return is indicated (comment added)
 *	0 if no return (comment removed)
 *	-1 if there is an error (not enough room for comment chars)
 * If successful, *lenp contains the new length.  Note: cursor should be
 * moved to the start of the line after (un)commenting.
 */
int
x_do_comment(char *buf, int bsize, int *lenp)
{
	int i, j;
	int len = *lenp;

	if (len == 0)
		return 1; /* somewhat arbitrary - it's what at&t ksh does */

	/* Already commented? */
	if (buf[0] == '#') {
		int saw_nl = 0;

		for (j = 0, i = 1; i < len; i++) {
			if (!saw_nl || buf[i] != '#')
				buf[j++] = buf[i];
			saw_nl = buf[i] == '\n';
		}
		*lenp = j;
		return 0;
	} else {
		int n = 1;

		/* See if there's room for the #'s - 1 per \n */
		for (i = 0; i < len; i++)
			if (buf[i] == '\n')
				n++;
		if (len + n >= bsize)
			return -1;
		/* Now add them... */
		for (i = len, j = len + n; --i >= 0; ) {
			if (buf[i] == '\n')
				buf[--j] = '#';
			buf[--j] = buf[i];
		}
		buf[0] = '#';
		*lenp += n;
		return 1;
	}
}

/* ------------------------------------------------------------------------- */
/*           Common file/command completion code for vi/emacs	             */


static char	*add_glob(const char *str, int slen);
static void	glob_table(const char *pat, XPtrV *wp, struct table *tp);
static void	glob_path(int flags, const char *pat, XPtrV *wp,
				const char *path);

static char *
plain_fmt_entry(void *arg, int i, char *buf, int bsize)
{
	const char *str = ((char *const *)arg)[i];
	char *buf0 = buf;
	int ch;

	if (buf == NULL || bsize <= 0)
		internal_errorf("%s: buf %lx, bsize %d",
		    __func__, (long) buf, bsize);

	while ((ch = (unsigned char)*str++) != '\0') {
		if (iscntrl(ch)) {
			if (bsize < 3)
				break;
			*buf++ = '^';
			*buf++ = UNCTRL(ch);
			bsize -= 2;
			continue;
		}
		if (bsize < 2)
			break;
		*buf++ = ch;
		bsize--;
	}
	*buf = '\0';

	return buf0;
}

/* Compute the length of string taking into account escape characters. */
static size_t
strlen_esc(const char *str)
{
	size_t len = 0;
	int ch;

	while ((ch = (unsigned char)*str++) != '\0') {
		if (iscntrl(ch))
			len++;
		len++;
	}
	return len;
}

static int
pr_list(char *const *ap)
{
	char *const *pp;
	int nwidth;
	int i, n;

	for (n = 0, nwidth = 0, pp = ap; *pp; n++, pp++) {
		i = strlen_esc(*pp);
		nwidth = (i > nwidth) ? i : nwidth;
	}
	print_columns(shl_out, n, plain_fmt_entry, (void *) ap, nwidth + 1, 0);

	return n;
}

void
x_print_expansions(int nwords, char *const *words, int is_command)
{
	int prefix_len;

	/* Check if all matches are in the same directory (in this
	 * case, we want to omit the directory name)
	 */
	if (!is_command &&
	    (prefix_len = x_longest_prefix(nwords, words)) > 0) {
		int i;

		/* Special case for 1 match (prefix is whole word) */
		if (nwords == 1)
			prefix_len = x_basename(words[0], NULL);
		/* Any (non-trailing) slashes in non-common word suffixes? */
		for (i = 0; i < nwords; i++)
			if (x_basename(words[i] + prefix_len, NULL) >
			    prefix_len)
				break;
		/* All in same directory? */
		if (i == nwords) {
			XPtrV l;

			while (prefix_len > 0 && words[0][prefix_len - 1] != '/')
				prefix_len--;
			XPinit(l, nwords + 1);
			for (i = 0; i < nwords; i++)
				XPput(l, words[i] + prefix_len);
			XPput(l, NULL);

			/* Enumerate expansions */
			x_putc('\r');
			x_putc('\n');
			pr_list((char **) XPptrv(l));

			XPfree(l); /* not x_free_words() */
			return;
		}
	}

	/* Enumerate expansions */
	x_putc('\r');
	x_putc('\n');
	pr_list(words);
}

/*
 *  Do file globbing:
 *	- appends * to (copy of) str if no globbing chars found
 *	- does expansion, checks for no match, etc.
 *	- sets *wordsp to array of matching strings
 *	- returns number of matching strings
 */
static int
x_file_glob(int flags, const char *str, int slen, char ***wordsp)
{
	char *toglob;
	char **words;
	int nwords;
	XPtrV w;
	struct source *s, *sold;

	if (slen < 0)
		return 0;

	toglob = add_glob(str, slen);

	/*
	 * Convert "foo*" (toglob) to an array of strings (words)
	 */
	sold = source;
	s = pushs(SWSTR, ATEMP);
	s->start = s->str = toglob;
	source = s;
	if (yylex(ONEWORD|UNESCAPE) != LWORD) {
		source = sold;
		internal_warningf("%s: substitute error", __func__);
		return 0;
	}
	source = sold;
	XPinit(w, 32);
	expand(yylval.cp, &w, DOGLOB|DOTILDE|DOMARKDIRS);
	XPput(w, NULL);
	words = (char **) XPclose(w);

	for (nwords = 0; words[nwords]; nwords++)
		;
	if (nwords == 1) {
		struct stat statb;

		/* Check if file exists, also, check for empty
		 * result - happens if we tried to glob something
		 * which evaluated to an empty string (e.g.,
		 * "$FOO" when there is no FOO, etc).
		 */
		if ((lstat(words[0], &statb) == -1) ||
		    words[0][0] == '\0') {
			x_free_words(nwords, words);
			words = NULL;
			nwords = 0;
		}
	}
	afree(toglob, ATEMP);

	if (nwords) {
		*wordsp = words;
	} else if (words) {
		x_free_words(nwords, words);
		*wordsp = NULL;
	}

	return nwords;
}

/* Data structure used in x_command_glob() */
struct path_order_info {
	char *word;
	int base;
	int path_order;
};

static int path_order_cmp(const void *aa, const void *bb);

/* Compare routine used in x_command_glob() */
static int
path_order_cmp(const void *aa, const void *bb)
{
	const struct path_order_info *a = (const struct path_order_info *) aa;
	const struct path_order_info *b = (const struct path_order_info *) bb;
	int t;

	t = strcmp(a->word + a->base, b->word + b->base);
	return t ? t : a->path_order - b->path_order;
}

static int
x_command_glob(int flags, const char *str, int slen, char ***wordsp)
{
	char *toglob;
	char *pat;
	char *fpath;
	int nwords;
	XPtrV w;
	struct block *l;

	if (slen < 0)
		return 0;

	toglob = add_glob(str, slen);

	/* Convert "foo*" (toglob) to a pattern for future use */
	pat = evalstr(toglob, DOPAT|DOTILDE);
	afree(toglob, ATEMP);

	XPinit(w, 32);

	glob_table(pat, &w, &keywords);
	glob_table(pat, &w, &aliases);
	glob_table(pat, &w, &builtins);
	for (l = genv->loc; l; l = l->next)
		glob_table(pat, &w, &l->funs);

	glob_path(flags, pat, &w, search_path);
	if ((fpath = str_val(global("FPATH"))) != null)
		glob_path(flags, pat, &w, fpath);

	nwords = XPsize(w);

	if (!nwords) {
		*wordsp = NULL;
		XPfree(w);
		return 0;
	}

	/* Sort entries */
	if (flags & XCF_FULLPATH) {
		/* Sort by basename, then path order */
		struct path_order_info *info;
		struct path_order_info *last_info = NULL;
		char **words = (char **) XPptrv(w);
		int path_order = 0;
		int i;

		info = areallocarray(NULL, nwords,
		    sizeof(struct path_order_info), ATEMP);

		for (i = 0; i < nwords; i++) {
			info[i].word = words[i];
			info[i].base = x_basename(words[i], NULL);
			if (!last_info || info[i].base != last_info->base ||
			    strncmp(words[i], last_info->word, info[i].base) != 0) {
				last_info = &info[i];
				path_order++;
			}
			info[i].path_order = path_order;
		}
		qsort(info, nwords, sizeof(struct path_order_info),
			path_order_cmp);
		for (i = 0; i < nwords; i++)
			words[i] = info[i].word;
		afree(info, ATEMP);
	} else {
		/* Sort and remove duplicate entries */
		char **words = (char **) XPptrv(w);
		int i, j;

		qsortp(XPptrv(w), (size_t) nwords, xstrcmp);

		for (i = j = 0; i < nwords - 1; i++) {
			if (strcmp(words[i], words[i + 1]))
				words[j++] = words[i];
			else
				afree(words[i], ATEMP);
		}
		words[j++] = words[i];
		nwords = j;
		w.cur = (void **) &words[j];
	}

	XPput(w, NULL);
	*wordsp = (char **) XPclose(w);

	return nwords;
}

#define IS_WORDC(c)	!( ctype(c, C_LEX1) || (c) == '\'' || (c) == '"' || \
			    (c) == '`' || (c) == '=' || (c) == ':' )

static int
x_locate_word(const char *buf, int buflen, int pos, int *startp,
    int *is_commandp)
{
	int p;
	int start, end;

	/* Bad call?  Probably should report error */
	if (pos < 0 || pos > buflen) {
		*startp = pos;
		*is_commandp = 0;
		return 0;
	}
	/* The case where pos == buflen happens to take care of itself... */

	start = pos;
	/* Keep going backwards to start of word (has effect of allowing
	 * one blank after the end of a word)
	 */
	for (; (start > 0 && IS_WORDC(buf[start - 1])) ||
	    (start > 1 && buf[start-2] == '\\'); start--)
		;
	/* Go forwards to end of word */
	for (end = start; end < buflen && IS_WORDC(buf[end]); end++) {
		if (buf[end] == '\\' && (end+1) < buflen)
			end++;
	}

	if (is_commandp) {
		int iscmd;

		/* Figure out if this is a command */
		for (p = start - 1; p >= 0 && isspace((unsigned char)buf[p]);
		    p--)
			;
		iscmd = p < 0 || strchr(";|&()`", buf[p]);
		if (iscmd) {
			/* If command has a /, path, etc. is not searched;
			 * only current directory is searched, which is just
			 * like file globbing.
			 */
			for (p = start; p < end; p++)
				if (buf[p] == '/')
					break;
			iscmd = p == end;
		}
		*is_commandp = iscmd;
	}

	*startp = start;

	return end - start;
}

static int
x_try_array(const char *buf, int buflen, const char *want, int wantlen,
    int *nwords, char ***words)
{
	const char *cmd, *cp;
	int cmdlen, n, i, slen;
	char *name, *s;
	struct tbl *v, *vp;

	*nwords = 0;
	*words = NULL;

	/* Walk back to find start of command. */
	if (want == buf)
		return 0;
	for (cmd = want; cmd > buf; cmd--) {
		if (strchr(";|&()`", cmd[-1]) != NULL)
			break;
	}
	while (cmd < want && isspace((u_char)*cmd))
		cmd++;
	cmdlen = 0;
	while (cmd + cmdlen < want && !isspace((u_char)cmd[cmdlen]))
		cmdlen++;
	for (i = 0; i < cmdlen; i++) {
		if (!isalnum((u_char)cmd[i]) && cmd[i] != '_')
			return 0;
	}

	/* Take a stab at argument count from here. */
	n = 1;
	for (cp = cmd + cmdlen + 1; cp < want; cp++) {
		if (!isspace((u_char)cp[-1]) && isspace((u_char)*cp))
			n++;
	}

	/* Try to find the array. */
	if (asprintf(&name, "complete_%.*s_%d", cmdlen, cmd, n) == -1)
		internal_errorf("unable to allocate memory");
	v = global(name);
	free(name);
	if (~v->flag & (ISSET|ARRAY)) {
		if (asprintf(&name, "complete_%.*s", cmdlen, cmd) == -1)
			internal_errorf("unable to allocate memory");
		v = global(name);
		free(name);
		if (~v->flag & (ISSET|ARRAY))
			return 0;
	}

	/* Walk the array and build words list. */
	for (vp = v; vp; vp = vp->u.array) {
		if (~vp->flag & ISSET)
			continue;

		s = str_val(vp);
		slen = strlen(s);

		if (slen < wantlen)
			continue;
		if (slen > wantlen)
			slen = wantlen;
		if (slen != 0 && strncmp(s, want, slen) != 0)
			continue;

		*words = areallocarray(*words, (*nwords) + 2, sizeof **words,
		    ATEMP);
		(*words)[(*nwords)++] = str_save(s, ATEMP);
	}
	if (*nwords != 0)
		(*words)[*nwords] = NULL;

	return *nwords != 0;
}

int
x_cf_glob(int flags, const char *buf, int buflen, int pos, int *startp,
    int *endp, char ***wordsp, int *is_commandp)
{
	int len;
	int nwords;
	char **words = NULL;
	int is_command;

	len = x_locate_word(buf, buflen, pos, startp, &is_command);
	if (!(flags & XCF_COMMAND))
		is_command = 0;
	/* Don't do command globing on zero length strings - it takes too
	 * long and isn't very useful.  File globs are more likely to be
	 * useful, so allow these.
	 */
	if (len == 0 && is_command)
		return 0;

	if (is_command)
		nwords = x_command_glob(flags, buf + *startp, len, &words);
	else if (!x_try_array(buf, buflen, buf + *startp, len, &nwords, &words))
		nwords = x_file_glob(flags, buf + *startp, len, &words);
	if (nwords == 0) {
		*wordsp = NULL;
		return 0;
	}

	if (is_commandp)
		*is_commandp = is_command;
	*wordsp = words;
	*endp = *startp + len;

	return nwords;
}

/* Given a string, copy it and possibly add a '*' to the end.  The
 * new string is returned.
 */
static char *
add_glob(const char *str, int slen)
{
	char *toglob;
	char *s;
	bool saw_slash = false;

	if (slen < 0)
		return NULL;

	toglob = str_nsave(str, slen + 1, ATEMP); /* + 1 for "*" */
	toglob[slen] = '\0';

	/*
	 * If the pathname contains a wildcard (an unquoted '*',
	 * '?', or '[') or parameter expansion ('$'), or a ~username
	 * with no trailing slash, then it is globbed based on that
	 * value (i.e., without the appended '*').
	 */
	for (s = toglob; *s; s++) {
		if (*s == '\\' && s[1])
			s++;
		else if (*s == '*' || *s == '[' || *s == '?' || *s == '$' ||
		    (s[1] == '(' /*)*/ && strchr("+@!", *s)))
			break;
		else if (*s == '/')
			saw_slash = true;
	}
	if (!*s && (*toglob != '~' || saw_slash)) {
		toglob[slen] = '*';
		toglob[slen + 1] = '\0';
	}

	return toglob;
}

/*
 * Find longest common prefix
 */
int
x_longest_prefix(int nwords, char *const *words)
{
	int i, j;
	int prefix_len;
	char *p;

	if (nwords <= 0)
		return 0;

	prefix_len = strlen(words[0]);
	for (i = 1; i < nwords; i++)
		for (j = 0, p = words[i]; j < prefix_len; j++)
			if (p[j] != words[0][j]) {
				prefix_len = j;
				break;
			}
	return prefix_len;
}

void
x_free_words(int nwords, char **words)
{
	int i;

	for (i = 0; i < nwords; i++)
		afree(words[i], ATEMP);
	afree(words, ATEMP);
}

/* Return the offset of the basename of string s (which ends at se - need not
 * be null terminated).  Trailing slashes are ignored.  If s is just a slash,
 * then the offset is 0 (actually, length - 1).
 *	s		Return
 *	/etc		1
 *	/etc/		1
 *	/etc//		1
 *	/etc/fo		5
 *	foo		0
 *	///		2
 *			0
 */
int
x_basename(const char *s, const char *se)
{
	const char *p;

	if (se == NULL)
		se = s + strlen(s);
	if (s == se)
		return 0;

	/* Skip trailing slashes */
	for (p = se - 1; p > s && *p == '/'; p--)
		;
	for (; p > s && *p != '/'; p--)
		;
	if (*p == '/' && p + 1 < se)
		p++;

	return p - s;
}

/*
 *  Apply pattern matching to a table: all table entries that match a pattern
 * are added to wp.
 */
static void
glob_table(const char *pat, XPtrV *wp, struct table *tp)
{
	struct tstate ts;
	struct tbl *te;

	for (ktwalk(&ts, tp); (te = ktnext(&ts)); ) {
		if (gmatch(te->name, pat, false))
			XPput(*wp, str_save(te->name, ATEMP));
	}
}

static void
glob_path(int flags, const char *pat, XPtrV *wp, const char *path)
{
	const char *sp, *p;
	char *xp;
	int staterr;
	int pathlen;
	int patlen;
	int oldsize, newsize, i, j;
	char **words;
	XString xs;

	patlen = strlen(pat) + 1;
	sp = path;
	Xinit(xs, xp, patlen + 128, ATEMP);
	while (sp) {
		xp = Xstring(xs, xp);
		if (!(p = strchr(sp, ':')))
			p = sp + strlen(sp);
		pathlen = p - sp;
		if (pathlen) {
			/* Copy sp into xp, stuffing any MAGIC characters
			 * on the way
			 */
			const char *s = sp;

			XcheckN(xs, xp, pathlen * 2);
			while (s < p) {
				if (ISMAGIC(*s))
					*xp++ = MAGIC;
				*xp++ = *s++;
			}
			*xp++ = '/';
			pathlen++;
		}
		sp = p;
		XcheckN(xs, xp, patlen);
		memcpy(xp, pat, patlen);

		oldsize = XPsize(*wp);
		glob_str(Xstring(xs, xp), wp, 1); /* mark dirs */
		newsize = XPsize(*wp);

		/* Check that each match is executable... */
		words = (char **) XPptrv(*wp);
		for (i = j = oldsize; i < newsize; i++) {
			staterr = 0;
			if ((search_access(words[i], X_OK, &staterr) >= 0) ||
			    (staterr == EISDIR)) {
				words[j] = words[i];
				if (!(flags & XCF_FULLPATH))
					memmove(words[j], words[j] + pathlen,
					    strlen(words[j] + pathlen) + 1);
				j++;
			} else
				afree(words[i], ATEMP);
		}
		wp->cur = (void **) &words[j];

		if (!*sp++)
			break;
	}
	Xfree(xs, xp);
}

/*
 * if argument string contains any special characters, they will
 * be escaped and the result will be put into edit buffer by
 * keybinding-specific function
 */
int
x_escape(const char *s, size_t len, int (*putbuf_func) (const char *, size_t))
{
	size_t add, wlen;
	const char *ifs = str_val(local("IFS", 0));
	int rval = 0;

	for (add = 0, wlen = len; wlen - add > 0; add++) {
		if (strchr("!\"#$&'()*:;<=>?[\\]`{|}", s[add]) ||
		    strchr(ifs, s[add])) {
			if (putbuf_func(s, add) != 0) {
				rval = -1;
				break;
			}

			putbuf_func("\\", 1);
			putbuf_func(&s[add], 1);

			add++;
			wlen -= add;
			s += add;
			add = -1; /* after the increment it will go to 0 */
		}
	}
	if (wlen > 0 && rval == 0)
		rval = putbuf_func(s, wlen);

	return (rval);
}
