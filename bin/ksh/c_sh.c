/*	$OpenBSD: c_sh.c,v 1.65 2023/09/14 18:32:03 cheloha Exp $	*/

/*
 * built-in Bourne commands
 */

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"

static void p_tv(struct shf *, int, struct timeval *, int, char *, char *);
static void p_ts(struct shf *, int, struct timespec *, int, char *, char *);

/* :, false and true */
int
c_label(char **wp)
{
	return wp[0][0] == 'f' ? 1 : 0;
}

int
c_shift(char **wp)
{
	struct block *l = genv->loc;
	int n;
	int64_t val;
	char *arg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	arg = wp[builtin_opt.optind];

	if (arg) {
		evaluate(arg, &val, KSH_UNWIND_ERROR, false);
		n = val;
	} else
		n = 1;
	if (n < 0) {
		bi_errorf("%s: bad number", arg);
		return (1);
	}
	if (l->argc < n) {
		bi_errorf("nothing to shift");
		return (1);
	}
	l->argv[n] = l->argv[0];
	l->argv += n;
	l->argc -= n;
	return 0;
}

int
c_umask(char **wp)
{
	int i;
	char *cp;
	int symbolic = 0;
	mode_t old_umask;
	int optc;

	while ((optc = ksh_getopt(wp, &builtin_opt, "S")) != -1)
		switch (optc) {
		case 'S':
			symbolic = 1;
			break;
		case '?':
			return 1;
		}
	cp = wp[builtin_opt.optind];
	if (cp == NULL) {
		old_umask = umask(0);
		umask(old_umask);
		if (symbolic) {
			char buf[18];
			int j;

			old_umask = ~old_umask;
			cp = buf;
			for (i = 0; i < 3; i++) {
				*cp++ = "ugo"[i];
				*cp++ = '=';
				for (j = 0; j < 3; j++)
					if (old_umask & (1 << (8 - (3*i + j))))
						*cp++ = "rwx"[j];
				*cp++ = ',';
			}
			cp[-1] = '\0';
			shprintf("%s\n", buf);
		} else
			shprintf("%#3.3o\n", old_umask);
	} else {
		mode_t new_umask;

		if (digit(*cp)) {
			for (new_umask = 0; *cp >= '0' && *cp <= '7'; cp++)
				new_umask = new_umask * 8 + (*cp - '0');
			if (*cp) {
				bi_errorf("bad number");
				return 1;
			}
		} else {
			/* symbolic format */
			int positions, new_val;
			char op;

			old_umask = umask(0);
			umask(old_umask); /* in case of error */
			old_umask = ~old_umask;
			new_umask = old_umask;
			positions = 0;
			while (*cp) {
				while (*cp && strchr("augo", *cp))
					switch (*cp++) {
					case 'a':
						positions |= 0111;
						break;
					case 'u':
						positions |= 0100;
						break;
					case 'g':
						positions |= 0010;
						break;
					case 'o':
						positions |= 0001;
						break;
					}
				if (!positions)
					positions = 0111; /* default is a */
				if (!strchr("=+-", op = *cp))
					break;
				cp++;
				new_val = 0;
				while (*cp && strchr("rwxugoXs", *cp))
					switch (*cp++) {
					case 'r': new_val |= 04; break;
					case 'w': new_val |= 02; break;
					case 'x': new_val |= 01; break;
					case 'u': new_val |= old_umask >> 6;
						  break;
					case 'g': new_val |= old_umask >> 3;
						  break;
					case 'o': new_val |= old_umask >> 0;
						  break;
					case 'X': if (old_umask & 0111)
							new_val |= 01;
						  break;
					case 's': /* ignored */
						  break;
					}
				new_val = (new_val & 07) * positions;
				switch (op) {
				case '-':
					new_umask &= ~new_val;
					break;
				case '=':
					new_umask = new_val |
					    (new_umask & ~(positions * 07));
					break;
				case '+':
					new_umask |= new_val;
				}
				if (*cp == ',') {
					positions = 0;
					cp++;
				} else if (!strchr("=+-", *cp))
					break;
			}
			if (*cp) {
				bi_errorf("bad mask");
				return 1;
			}
			new_umask = ~new_umask;
		}
		umask(new_umask);
	}
	return 0;
}

int
c_dot(char **wp)
{
	char *file, *cp;
	char **argv;
	int argc;
	int i;
	int err;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;

	if ((cp = wp[builtin_opt.optind]) == NULL)
		return 0;
	file = search(cp, search_path, R_OK, &err);
	if (file == NULL) {
		bi_errorf("%s: %s", cp, err ? strerror(err) : "not found");
		return 1;
	}

	/* Set positional parameters? */
	if (wp[builtin_opt.optind + 1]) {
		argv = wp + builtin_opt.optind;
		argv[0] = genv->loc->argv[0]; /* preserve $0 */
		for (argc = 0; argv[argc + 1]; argc++)
			;
	} else {
		argc = 0;
		argv = NULL;
	}
	i = include(file, argc, argv, 0);
	if (i < 0) { /* should not happen */
		bi_errorf("%s: %s", cp, strerror(errno));
		return 1;
	}
	return i;
}

int
c_wait(char **wp)
{
	int rv = 0;
	int sig;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	wp += builtin_opt.optind;
	if (*wp == NULL) {
		while (waitfor(NULL, &sig) >= 0)
			;
		rv = sig;
	} else {
		for (; *wp; wp++)
			rv = waitfor(*wp, &sig);
		if (rv < 0)
			rv = sig ? sig : 127; /* magic exit code: bad job-id */
	}
	return rv;
}

int
c_read(char **wp)
{
	int c = 0;
	int expand = 1, savehist = 0;
	int expanding;
	int ecode = 0;
	char *cp;
	int fd = 0;
	struct shf *shf;
	int optc;
	const char *emsg;
	XString cs, xs;
	struct tbl *vp;
	char *xp = NULL;

	while ((optc = ksh_getopt(wp, &builtin_opt, "prsu,")) != -1)
		switch (optc) {
		case 'p':
			if ((fd = coproc_getfd(R_OK, &emsg)) < 0) {
				bi_errorf("-p: %s", emsg);
				return 1;
			}
			break;
		case 'r':
			expand = 0;
			break;
		case 's':
			savehist = 1;
			break;
		case 'u':
			if (!*(cp = builtin_opt.optarg))
				fd = 0;
			else if ((fd = check_fd(cp, R_OK, &emsg)) < 0) {
				bi_errorf("-u: %s: %s", cp, emsg);
				return 1;
			}
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;

	if (*wp == NULL)
		*--wp = "REPLY";

	/* Since we can't necessarily seek backwards on non-regular files,
	 * don't buffer them so we can't read too much.
	 */
	shf = shf_reopen(fd, SHF_RD | SHF_INTERRUPT | can_seek(fd), shl_spare);

	if ((cp = strchr(*wp, '?')) != NULL) {
		*cp = 0;
		if (isatty(fd)) {
			/* at&t ksh says it prints prompt on fd if it's open
			 * for writing and is a tty, but it doesn't do it
			 * (it also doesn't check the interactive flag,
			 * as is indicated in the Kornshell book).
			 */
			shellf("%s", cp+1);
		}
	}

	/* If we are reading from the co-process for the first time,
	 * make sure the other side of the pipe is closed first.  This allows
	 * the detection of eof.
	 *
	 * This is not compatible with at&t ksh... the fd is kept so another
	 * coproc can be started with same output, however, this means eof
	 * can't be detected...  This is why it is closed here.
	 * If this call is removed, remove the eof check below, too.
	 * coproc_readw_close(fd);
	 */

	if (savehist)
		Xinit(xs, xp, 128, ATEMP);
	expanding = 0;
	Xinit(cs, cp, 128, ATEMP);
	for (; *wp != NULL; wp++) {
		for (cp = Xstring(cs, cp); ; ) {
			if (c == '\n' || c == EOF)
				break;
			while (1) {
				c = shf_getc(shf);
				if (c == '\0')
					continue;
				if (c == EOF && shf_error(shf) &&
				    shf->errno_ == EINTR) {
					/* Was the offending signal one that
					 * would normally kill a process?
					 * If so, pretend the read was killed.
					 */
					ecode = fatal_trap_check();

					/* non fatal (eg, CHLD), carry on */
					if (!ecode) {
						shf_clearerr(shf);
						continue;
					}
				}
				break;
			}
			if (savehist) {
				Xcheck(xs, xp);
				Xput(xs, xp, c);
			}
			Xcheck(cs, cp);
			if (expanding) {
				expanding = 0;
				if (c == '\n') {
					c = 0;
					if (Flag(FTALKING_I) && isatty(fd)) {
						/* set prompt in case this is
						 * called from .profile or $ENV
						 */
						set_prompt(PS2);
						pprompt(prompt, 0);
					}
				} else if (c != EOF)
					Xput(cs, cp, c);
				continue;
			}
			if (expand && c == '\\') {
				expanding = 1;
				continue;
			}
			if (c == '\n' || c == EOF)
				break;
			if (ctype(c, C_IFS)) {
				if (Xlength(cs, cp) == 0 && ctype(c, C_IFSWS))
					continue;
				if (wp[1])
					break;
			}
			Xput(cs, cp, c);
		}
		/* strip trailing IFS white space from last variable */
		if (!wp[1])
			while (Xlength(cs, cp) && ctype(cp[-1], C_IFS) &&
			    ctype(cp[-1], C_IFSWS))
				cp--;
		Xput(cs, cp, '\0');
		vp = global(*wp);
		/* Must be done before setting export. */
		if (vp->flag & RDONLY) {
			shf_flush(shf);
			bi_errorf("%s is read only", *wp);
			return 1;
		}
		if (Flag(FEXPORT))
			typeset(*wp, EXPORT, 0, 0, 0);
		if (!setstr(vp, Xstring(cs, cp), KSH_RETURN_ERROR)) {
		    shf_flush(shf);
		    return 1;
		}
	}

	shf_flush(shf);
	if (savehist) {
		Xput(xs, xp, '\0');
		source->line++;
		histsave(source->line, Xstring(xs, xp), 1);
		Xfree(xs, xp);
	}
	/* if this is the co-process fd, close the file descriptor
	 * (can get eof if and only if all processes are have died, ie,
	 * coproc.njobs is 0 and the pipe is closed).
	 */
	if (c == EOF && !ecode)
		coproc_read_close(fd);

	return ecode ? ecode : c == EOF;
}

int
c_eval(char **wp)
{
	struct source *s;
	struct source *saves = source;
	int savef;
	int rv;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	s = pushs(SWORDS, ATEMP);
	s->u.strv = wp + builtin_opt.optind;
	if (!Flag(FPOSIX)) {
		/*
		 * Handle case where the command is empty due to failed
		 * command substitution, eg, eval "$(false)".
		 * In this case, shell() will not set/change exstat (because
		 * compiled tree is empty), so will use this value.
		 * subst_exstat is cleared in execute(), so should be 0 if
		 * there were no substitutions.
		 *
		 * A strict reading of POSIX says we don't do this (though
		 * it is traditionally done). [from 1003.2-1992]
		 *    3.9.1: Simple Commands
		 *	... If there is a command name, execution shall
		 *	continue as described in 3.9.1.1.  If there
		 *	is no command name, but the command contained a command
		 *	substitution, the command shall complete with the exit
		 *	status of the last command substitution
		 *    3.9.1.1: Command Search and Execution
		 *	...(1)...(a) If the command name matches the name of
		 *	a special built-in utility, that special built-in
		 *	utility shall be invoked.
		 * 3.14.5: Eval
		 *	... If there are no arguments, or only null arguments,
		 *	eval shall return an exit status of zero.
		 */
		exstat = subst_exstat;
	}

	savef = Flag(FERREXIT);
	Flag(FERREXIT) = 0;
	rv = shell(s, false);
	Flag(FERREXIT) = savef;
	source = saves;
	afree(s, ATEMP);
	return (rv);
}

int
c_trap(char **wp)
{
	int i;
	char *s;
	Trap *p;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	wp += builtin_opt.optind;

	if (*wp == NULL) {
		for (p = sigtraps, i = NSIG+1; --i >= 0; p++) {
			if (p->trap != NULL) {
				shprintf("trap -- ");
				print_value_quoted(p->trap);
				shprintf(" %s\n", p->name);
			}
		}
		return 0;
	}

	/*
	 * Use case sensitive lookup for first arg so the
	 * command 'exit' isn't confused with the pseudo-signal
	 * 'EXIT'.
	 */
	s = (gettrap(*wp, false) == NULL) ? *wp++ : NULL; /* get command */
	if (s != NULL && s[0] == '-' && s[1] == '\0')
		s = NULL;

	/* set/clear traps */
	while (*wp != NULL) {
		p = gettrap(*wp++, true);
		if (p == NULL) {
			bi_errorf("bad signal %s", wp[-1]);
			return 1;
		}
		settrap(p, s);
	}
	return 0;
}

int
c_exitreturn(char **wp)
{
	int how = LEXIT;
	int n;
	char *arg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	arg = wp[builtin_opt.optind];

	if (arg) {
		if (!getn(arg, &n)) {
			exstat = 1;
			warningf(true, "%s: bad number", arg);
		} else
			exstat = n;
	}
	if (wp[0][0] == 'r') { /* return */
		struct env *ep;

		/* need to tell if this is exit or return so trap exit will
		 * work right (POSIX)
		 */
		for (ep = genv; ep; ep = ep->oenv)
			if (STOP_RETURN(ep->type)) {
				how = LRETURN;
				break;
			}
	}

	if (how == LEXIT && !really_exit && j_stopped_running()) {
		really_exit = 1;
		how = LSHELL;
	}

	quitenv(NULL);	/* get rid of any i/o redirections */
	unwind(how);
	/* NOTREACHED */
	return 0;
}

int
c_brkcont(char **wp)
{
	int n, quit;
	struct env *ep, *last_ep = NULL;
	char *arg;

	if (ksh_getopt(wp, &builtin_opt, null) == '?')
		return 1;
	arg = wp[builtin_opt.optind];

	if (!arg)
		n = 1;
	else if (!bi_getn(arg, &n))
		return 1;
	quit = n;
	if (quit <= 0) {
		/* at&t ksh does this for non-interactive shells only - weird */
		bi_errorf("%s: bad value", arg);
		return 1;
	}

	/* Stop at E_NONE, E_PARSE, E_FUNC, or E_INCL */
	for (ep = genv; ep && !STOP_BRKCONT(ep->type); ep = ep->oenv)
		if (ep->type == E_LOOP) {
			if (--quit == 0)
				break;
			ep->flags |= EF_BRKCONT_PASS;
			last_ep = ep;
		}

	if (quit) {
		/* at&t ksh doesn't print a message - just does what it
		 * can.  We print a message 'cause it helps in debugging
		 * scripts, but don't generate an error (ie, keep going).
		 */
		if (n == quit) {
			warningf(true, "%s: cannot %s", wp[0], wp[0]);
			return 0;
		}
		/* POSIX says if n is too big, the last enclosing loop
		 * shall be used.  Doesn't say to print an error but we
		 * do anyway 'cause the user messed up.
		 */
		if (last_ep)
			last_ep->flags &= ~EF_BRKCONT_PASS;
		warningf(true, "%s: can only %s %d level(s)",
		    wp[0], wp[0], n - quit);
	}

	unwind(*wp[0] == 'b' ? LBREAK : LCONTIN);
	/* NOTREACHED */
}

int
c_set(char **wp)
{
	int argi, setargs;
	struct block *l = genv->loc;
	char **owp = wp;

	if (wp[1] == NULL) {
		static const char *const args [] = { "set", "-", NULL };
		return c_typeset((char **) args);
	}

	argi = parse_args(wp, OF_SET, &setargs);
	if (argi < 0)
		return 1;
	/* set $# and $* */
	if (setargs) {
		owp = wp += argi - 1;
		wp[0] = l->argv[0]; /* save $0 */
		while (*++wp != NULL)
			*wp = str_save(*wp, &l->area);
		l->argc = wp - owp - 1;
		l->argv = areallocarray(NULL, l->argc+2, sizeof(char *), &l->area);
		for (wp = l->argv; (*wp++ = *owp++) != NULL; )
			;
	}
	/* POSIX says set exit status is 0, but old scripts that use
	 * getopt(1), use the construct: set -- `getopt ab:c "$@"`
	 * which assumes the exit value set will be that of the ``
	 * (subst_exstat is cleared in execute() so that it will be 0
	 * if there are no command substitutions).
	 */
	return Flag(FPOSIX) ? 0 : subst_exstat;
}

int
c_unset(char **wp)
{
	char *id;
	int optc, unset_var = 1;

	while ((optc = ksh_getopt(wp, &builtin_opt, "fv")) != -1)
		switch (optc) {
		case 'f':
			unset_var = 0;
			break;
		case 'v':
			unset_var = 1;
			break;
		case '?':
			return 1;
		}
	wp += builtin_opt.optind;
	for (; (id = *wp) != NULL; wp++)
		if (unset_var) {	/* unset variable */
			struct tbl *vp = global(id);

			if ((vp->flag&RDONLY)) {
				bi_errorf("%s is read only", vp->name);
				return 1;
			}
			unset(vp, strchr(id, '[') ? 1 : 0);
		} else {		/* unset function */
			define(id, NULL);
		}
	return 0;
}

static void
p_tv(struct shf *shf, int posix, struct timeval *tv, int width, char *prefix,
    char *suffix)
{
	struct timespec ts;

	TIMEVAL_TO_TIMESPEC(tv, &ts);
	p_ts(shf, posix, &ts, width, prefix, suffix);
}

static void
p_ts(struct shf *shf, int posix, struct timespec *ts, int width, char *prefix,
    char *suffix)
{
	if (posix)
		shf_fprintf(shf, "%s%*lld.%02ld%s", prefix ? prefix : "",
		    width, (long long)ts->tv_sec, ts->tv_nsec / 10000000,
		    suffix);
	else
		shf_fprintf(shf, "%s%*lldm%02lld.%02lds%s", prefix ? prefix : "",
		    width, (long long)ts->tv_sec / 60,
		    (long long)ts->tv_sec % 60,
		    ts->tv_nsec / 10000000, suffix);
}


int
c_times(char **wp)
{
	struct rusage usage;

	(void) getrusage(RUSAGE_SELF, &usage);
	p_tv(shl_stdout, 0, &usage.ru_utime, 0, NULL, " ");
	p_tv(shl_stdout, 0, &usage.ru_stime, 0, NULL, "\n");

	(void) getrusage(RUSAGE_CHILDREN, &usage);
	p_tv(shl_stdout, 0, &usage.ru_utime, 0, NULL, " ");
	p_tv(shl_stdout, 0, &usage.ru_stime, 0, NULL, "\n");

	return 0;
}

/*
 * time pipeline (really a statement, not a built-in command)
 */
int
timex(struct op *t, int f, volatile int *xerrok)
{
#define TF_NOARGS	BIT(0)
#define TF_NOREAL	BIT(1)		/* don't report real time */
#define TF_POSIX	BIT(2)		/* report in posix format */
	int rv = 0;
	struct rusage ru0, ru1, cru0, cru1;
	struct timeval usrtime, systime;
	struct timespec ts0, ts1, ts2;
	int tf = 0;
	extern struct timeval j_usrtime, j_systime; /* computed by j_wait */

	clock_gettime(CLOCK_MONOTONIC, &ts0);
	getrusage(RUSAGE_SELF, &ru0);
	getrusage(RUSAGE_CHILDREN, &cru0);
	if (t->left) {
		/*
		 * Two ways of getting cpu usage of a command: just use t0
		 * and t1 (which will get cpu usage from other jobs that
		 * finish while we are executing t->left), or get the
		 * cpu usage of t->left. at&t ksh does the former, while
		 * pdksh tries to do the later (the j_usrtime hack doesn't
		 * really work as it only counts the last job).
		 */
		timerclear(&j_usrtime);
		timerclear(&j_systime);
		rv = execute(t->left, f | XTIME, xerrok);
		if (t->left->type == TCOM)
			tf |= t->left->str[0];
		clock_gettime(CLOCK_MONOTONIC, &ts1);
		getrusage(RUSAGE_SELF, &ru1);
		getrusage(RUSAGE_CHILDREN, &cru1);
	} else
		tf = TF_NOARGS;

	if (tf & TF_NOARGS) { /* ksh93 - report shell times (shell+kids) */
		tf |= TF_NOREAL;
		timeradd(&ru0.ru_utime, &cru0.ru_utime, &usrtime);
		timeradd(&ru0.ru_stime, &cru0.ru_stime, &systime);
	} else {
		timersub(&ru1.ru_utime, &ru0.ru_utime, &usrtime);
		timeradd(&usrtime, &j_usrtime, &usrtime);
		timersub(&ru1.ru_stime, &ru0.ru_stime, &systime);
		timeradd(&systime, &j_systime, &systime);
	}

	if (!(tf & TF_NOREAL)) {
		timespecsub(&ts1, &ts0, &ts2);
		if (tf & TF_POSIX)
			p_ts(shl_out, 1, &ts2, 5, "real ", "\n");
		else
			p_ts(shl_out, 0, &ts2, 5, NULL, " real ");
	}
	if (tf & TF_POSIX)
		p_tv(shl_out, 1, &usrtime, 5, "user ", "\n");
	else
		p_tv(shl_out, 0, &usrtime, 5, NULL, " user ");
	if (tf & TF_POSIX)
		p_tv(shl_out, 1, &systime, 5, "sys  ", "\n");
	else
		p_tv(shl_out, 0, &systime, 5, NULL, " system\n");
	shf_flush(shl_out);

	return rv;
}

void
timex_hook(struct op *t, char **volatile *app)
{
	char **wp = *app;
	int optc;
	int i, j;
	Getopt opt;

	ksh_getopt_reset(&opt, 0);
	opt.optind = 0;	/* start at the start */
	while ((optc = ksh_getopt(wp, &opt, ":p")) != -1)
		switch (optc) {
		case 'p':
			t->str[0] |= TF_POSIX;
			break;
		case '?':
			errorf("time: -%s unknown option", opt.optarg);
		case ':':
			errorf("time: -%s requires an argument",
			    opt.optarg);
		}
	/* Copy command words down over options. */
	if (opt.optind != 0) {
		for (i = 0; i < opt.optind; i++)
			afree(wp[i], ATEMP);
		for (i = 0, j = opt.optind; (wp[i] = wp[j]); i++, j++)
			;
	}
	if (!wp[0])
		t->str[0] |= TF_NOARGS;
	*app = wp;
}

/* exec with no args - args case is taken care of in comexec() */
int
c_exec(char **wp)
{
	int i;

	/* make sure redirects stay in place */
	if (genv->savefd != NULL) {
		for (i = 0; i < NUFILE; i++) {
			if (genv->savefd[i] > 0)
				close(genv->savefd[i]);
			/*
			 * For ksh keep anything > 2 private,
			 * for sh, let them be (POSIX says what
			 * happens is unspecified and the bourne shell
			 * keeps them open).
			 */
			if (!Flag(FSH) && i > 2 && genv->savefd[i])
				fcntl(i, F_SETFD, FD_CLOEXEC);
		}
		genv->savefd = NULL;
	}
	return 0;
}

static int
c_suspend(char **wp)
{
	if (wp[1] != NULL) {
		bi_errorf("too many arguments");
		return 1;
	}
	if (Flag(FLOGIN)) {
		/* Can't suspend an orphaned process group. */
		pid_t parent = getppid();
		if (getpgid(parent) == getpgid(0) ||
		    getsid(parent) != getsid(0)) {
			bi_errorf("can't suspend a login shell");
			return 1;
		}
	}
	j_suspend();
	return 0;
}

/* dummy function, special case in comexec() */
int
c_builtin(char **wp)
{
	return 0;
}

extern	int c_test(char **wp);			/* in c_test.c */
extern	int c_ulimit(char **wp);		/* in c_ulimit.c */

/* A leading = means assignments before command are kept;
 * a leading * means a POSIX special builtin;
 * a leading + means a POSIX regular builtin
 * (* and + should not be combined).
 */
const struct builtin shbuiltins [] = {
	{"*=.", c_dot},
	{"*=:", c_label},
	{"[", c_test},
	{"*=break", c_brkcont},
	{"=builtin", c_builtin},
	{"*=continue", c_brkcont},
	{"*=eval", c_eval},
	{"*=exec", c_exec},
	{"*=exit", c_exitreturn},
	{"+false", c_label},
	{"*=return", c_exitreturn},
	{"*=set", c_set},
	{"*=shift", c_shift},
	{"*=times", c_times},
	{"*=trap", c_trap},
	{"+=wait", c_wait},
	{"+read", c_read},
	{"test", c_test},
	{"+true", c_label},
	{"ulimit", c_ulimit},
	{"+umask", c_umask},
	{"*=unset", c_unset},
	{"suspend", c_suspend},
	{NULL, NULL}
};
