/*	$OpenBSD: main.c,v 1.100 2023/07/23 23:42:03 kn Exp $	*/

/*
 * startup, main loop, environments and error handling
 */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"

extern char **environ;

/*
 * global data
 */

static void	reclaim(void);
static void	remove_temps(struct temp *tp);
static int	is_restricted(char *name);
static void	init_username(void);

const char *kshname;
pid_t	kshpid;
pid_t	procpid;
uid_t	ksheuid;
int	exstat;
int	subst_exstat;
const char *safe_prompt;
int	disable_subst;

Area	aperm;

struct env	*genv;

char	shell_flags[FNFLAGS];

char	null[] = "";

int shl_stdout_ok;

unsigned int	ksh_tmout;
enum tmout_enum	ksh_tmout_state = TMOUT_EXECUTING;

int	really_exit;

int ifs0 = ' ';

volatile sig_atomic_t	trap;
volatile sig_atomic_t	intrsig;
volatile sig_atomic_t	fatal_trap;

Getopt	builtin_opt;
Getopt	user_opt;

struct coproc	coproc;
sigset_t	sm_default, sm_sigchld;

char	*builtin_argv0;
int	 builtin_flag;

char	*current_wd;
int	 current_wd_size;

int	x_cols = 80;

/*
 * shell initialization
 */

static const char initifs[] = "IFS= \t\n";

static const char initsubs[] = "${PS2=> } ${PS3=#? } ${PS4=+ }";

static const char *initcoms [] = {
#ifndef SMALL
	"typeset", "-r", "KSH_VERSION", NULL,
#endif /* SMALL */
	"typeset", "-x", "SHELL", "PATH", "HOME", "PWD", "OLDPWD", NULL,
	"typeset", "-ir", "PPID", NULL,
	"typeset", "-i", "OPTIND=1", NULL,
#ifndef SMALL
	"eval", "typeset -i RANDOM MAILCHECK=\"${MAILCHECK-600}\" SECONDS=\"${SECONDS-0}\" TMOUT=\"${TMOUT-0}\"", NULL,
#else
	"eval", "typeset -i RANDOM SECONDS=\"${SECONDS-0}\" TMOUT=\"${TMOUT-0}\"", NULL,
#endif /* SMALL */
	"alias",
	 /* Standard ksh aliases */
	  "hash=alias -t",	/* not "alias -t --": hash -r needs to work */
	  "stop=kill -STOP",
	  "autoload=typeset -fu",
	  "functions=typeset -f",
	  "history=fc -l",
	  "integer=typeset -i",
	  "nohup=nohup ",
	  "local=typeset",
	  "r=fc -s",
	 /* Aliases that are builtin commands in at&t */
	  "login=exec login",
	  NULL,
	/* this is what at&t ksh seems to track, with the addition of emacs */
	"alias", "-tU",
	  "cat", "cc", "chmod", "cp", "date", "ed", "emacs", "grep", "ls",
	  "mail", "make", "mv", "pr", "rm", "sed", "sh", "vi", "who",
	  NULL,
	NULL
};

char username[_PW_NAME_LEN + 1];

#ifndef SMALL
#define version_param  (initcoms[2])
#endif /* SMALL */

/* The shell uses its own variation on argv, to build variables like
 * $0 and $@.
 * Allocate a new array since modifying the original argv will modify
 * ps output.
 */
static char **
make_argv(int argc, char *argv[])
{
	int i;
	char **nargv;

	nargv = areallocarray(NULL, argc + 1, sizeof(char *), &aperm);
	nargv[0] = (char *) kshname;
	for (i = 1; i < argc; i++)
		nargv[i] = argv[i];
	nargv[i] = NULL;

	return nargv;
}

int
main(int argc, char *argv[])
{
	int i;
	int argi;
	Source *s;
	struct block *l;
	int restricted, errexit;
	char **wp;
	struct env env;
	pid_t ppid;

	kshname = argv[0];

	if (issetugid()) { /* could later drop privileges */
		if (pledge("stdio rpath wpath cpath fattr flock getpw proc "
		    "exec tty id", NULL) == -1) {
			perror("pledge");
			exit(1);
		}
	} else {
		if (pledge("stdio rpath wpath cpath fattr flock getpw proc "
		    "exec tty", NULL) == -1) {
			perror("pledge");
			exit(1);
		}
	}

	ainit(&aperm);		/* initialize permanent Area */

	/* set up base environment */
	memset(&env, 0, sizeof(env));
	env.type = E_NONE;
	ainit(&env.area);
	genv = &env;
	newblock();		/* set up global l->vars and l->funs */

	/* Do this first so output routines (eg, errorf, shellf) can work */
	initio();

	initvar();

	initctypes();

	inittraps();

	coproc_init();

	/* set up variable and command dictionaries */
	ktinit(&taliases, APERM, 0);
	ktinit(&aliases, APERM, 0);
	ktinit(&homedirs, APERM, 0);

	/* define shell keywords */
	initkeywords();

	/* define built-in commands */
	ktinit(&builtins, APERM, 64); /* must be 2^n (currently 40 builtins) */
	for (i = 0; shbuiltins[i].name != NULL; i++)
		builtin(shbuiltins[i].name, shbuiltins[i].func);
	for (i = 0; kshbuiltins[i].name != NULL; i++)
		builtin(kshbuiltins[i].name, kshbuiltins[i].func);

	init_histvec();

	def_path = _PATH_DEFPATH;
	{
		size_t len = confstr(_CS_PATH, NULL, 0);
		char *new;

		if (len > 0) {
			confstr(_CS_PATH, new = alloc(len + 1, APERM), len + 1);
			def_path = new;
		}
	}

	/* Set PATH to def_path (will set the path global variable).
	 * (import of environment below will probably change this setting).
	 */
	{
		struct tbl *vp = global("PATH");
		/* setstr can't fail here */
		setstr(vp, def_path, KSH_RETURN_ERROR);
	}


	/* Turn on nohup by default for now - will change to off
	 * by default once people are aware of its existence
	 * (at&t ksh does not have a nohup option - it always sends
	 * the hup).
	 */
	Flag(FNOHUP) = 1;

	/* Turn on brace expansion by default.  At&t ksh's that have
	 * alternation always have it on.  BUT, posix doesn't have
	 * brace expansion, so set this before setting up FPOSIX
	 * (change_flag() clears FBRACEEXPAND when FPOSIX is set).
	 */
	Flag(FBRACEEXPAND) = 1;

	/* set posix flag just before environment so that it will have
	 * exactly the same effect as the POSIXLY_CORRECT environment
	 * variable.  If this needs to be done sooner to ensure correct posix
	 * operation, an initial scan of the environment will also have
	 * done sooner.
	 */
#ifdef POSIXLY_CORRECT
	change_flag(FPOSIX, OF_SPECIAL, 1);
#endif /* POSIXLY_CORRECT */

	/* Check to see if we're /bin/sh. */
	if (!strcmp(kshname, "sh") || !strcmp(kshname, "-sh") ||
	    (strlen(kshname) >= 3 &&
	    !strcmp(&kshname[strlen(kshname) - 3], "/sh"))) {
		Flag(FSH) = 1;
#ifndef SMALL
		version_param = "SH_VERSION";
#endif /* SMALL */
	}

	/* Set edit mode to emacs by default, may be overridden
	 * by the environment or the user.  Also, we want tab completion
	 * on in vi by default. */
#if defined(EMACS)
	change_flag(FEMACS, OF_SPECIAL, 1);
#endif /* EMACS */
#if defined(VI)
	Flag(FVITABCOMPLETE) = 1;
#endif /* VI */

	/* import environment */
	if (environ != NULL)
		for (wp = environ; *wp != NULL; wp++)
			typeset(*wp, IMPORT|EXPORT, 0, 0, 0);

	kshpid = procpid = getpid();
	typeset(initifs, 0, 0, 0, 0);	/* for security */

	/* assign default shell variable values */
	substitute(initsubs, 0);

	/* Figure out the current working directory and set $PWD */
	{
		struct stat s_pwd, s_dot;
		struct tbl *pwd_v = global("PWD");
		char *pwd = str_val(pwd_v);
		char *pwdx = pwd;

		/* Try to use existing $PWD if it is valid */
		if (pwd[0] != '/' ||
		    stat(pwd, &s_pwd) == -1 || stat(".", &s_dot) == -1 ||
		    s_pwd.st_dev != s_dot.st_dev ||
		    s_pwd.st_ino != s_dot.st_ino)
			pwdx = NULL;
		set_current_wd(pwdx);
		if (current_wd[0])
			simplify_path(current_wd);
		/* Only set pwd if we know where we are or if it had a
		 * bogus value
		 */
		if (current_wd[0] || pwd != null)
			/* setstr can't fail here */
			setstr(pwd_v, current_wd, KSH_RETURN_ERROR);
	}
	ppid = getppid();
	setint(global("PPID"), (int64_t) ppid);
#ifndef SMALL
	/* setstr can't fail here */
	setstr(global(version_param), ksh_version, KSH_RETURN_ERROR);
#endif /* SMALL */

	/* execute initialization statements */
	for (wp = (char**) initcoms; *wp != NULL; wp++) {
		shcomexec(wp);
		for (; *wp != NULL; wp++)
			;
	}


	ksheuid = geteuid();
	init_username();
	safe_prompt = ksheuid ? "$ " : "# ";
	{
		struct tbl *vp = global("PS1");

		/* Set PS1 if it isn't set */
		if (!(vp->flag & ISSET)) {
			/* setstr can't fail here */
			setstr(vp, "\\h\\$ ", KSH_RETURN_ERROR);
		}
	}

	/* Set this before parsing arguments */
	Flag(FPRIVILEGED) = getuid() != ksheuid || getgid() != getegid();

	/* this to note if monitor is set on command line (see below) */
	Flag(FMONITOR) = 127;
	argi = parse_args(argv, OF_CMDLINE, NULL);
	if (argi < 0)
		exit(1);

	if (Flag(FCOMMAND)) {
		s = pushs(SSTRING, ATEMP);
		if (!(s->start = s->str = argv[argi++]))
			errorf("-c requires an argument");
		if (argv[argi])
			kshname = argv[argi++];
	} else if (argi < argc && !Flag(FSTDIN)) {
		s = pushs(SFILE, ATEMP);
		s->file = argv[argi++];
		s->u.shf = shf_open(s->file, O_RDONLY, 0, SHF_MAPHI|SHF_CLEXEC);
		if (s->u.shf == NULL) {
			exstat = 127; /* POSIX */
			errorf("%s: %s", s->file, strerror(errno));
		}
		kshname = s->file;
	} else {
		Flag(FSTDIN) = 1;
		s = pushs(SSTDIN, ATEMP);
		s->file = "<stdin>";
		s->u.shf = shf_fdopen(0, SHF_RD | can_seek(0), NULL);
		if (isatty(0) && isatty(2)) {
			Flag(FTALKING) = Flag(FTALKING_I) = 1;
			/* The following only if isatty(0) */
			s->flags |= SF_TTY;
			s->u.shf->flags |= SHF_INTERRUPT;
			s->file = NULL;
		}
	}

	/* This bizarreness is mandated by POSIX */
	{
		struct stat s_stdin;

		if (fstat(0, &s_stdin) >= 0 && S_ISCHR(s_stdin.st_mode) &&
		    Flag(FTALKING))
			reset_nonblock(0);
	}

	/* initialize job control */
	i = Flag(FMONITOR) != 127;
	Flag(FMONITOR) = 0;
	j_init(i);
	/* Do this after j_init(), as tty_fd is not initialized 'til then */
	if (Flag(FTALKING))
		x_init();

	l = genv->loc;
	l->argv = make_argv(argc - (argi - 1), &argv[argi - 1]);
	l->argc = argc - argi;
	getopts_reset(1);

	/* Disable during .profile/ENV reading */
	restricted = Flag(FRESTRICTED);
	Flag(FRESTRICTED) = 0;
	errexit = Flag(FERREXIT);
	Flag(FERREXIT) = 0;

	/* Do this before profile/$ENV so that if it causes problems in them,
	 * user will know why things broke.
	 */
	if (!current_wd[0] && Flag(FTALKING))
		warningf(false, "Cannot determine current working directory");

	if (Flag(FLOGIN)) {
		include(KSH_SYSTEM_PROFILE, 0, NULL, 1);
		if (!Flag(FPRIVILEGED))
			include(substitute("$HOME/.profile", 0), 0, NULL, 1);
	}

	if (Flag(FPRIVILEGED))
		include("/etc/suid_profile", 0, NULL, 1);
	else if (Flag(FTALKING)) {
		char *env_file;

		/* include $ENV */
		env_file = str_val(global("ENV"));

#ifdef DEFAULT_ENV
		/* If env isn't set, include default environment */
		if (env_file == null)
			env_file = DEFAULT_ENV;
#endif /* DEFAULT_ENV */
		env_file = substitute(env_file, DOTILDE);
		if (*env_file != '\0')
			include(env_file, 0, NULL, 1);
	}

	if (is_restricted(argv[0]) || is_restricted(str_val(global("SHELL"))))
		restricted = 1;
	if (restricted) {
		static const char *const restr_com[] = {
			"typeset", "-r", "PATH",
			"ENV", "SHELL",
			NULL
		};
		shcomexec((char **) restr_com);
		/* After typeset command... */
		Flag(FRESTRICTED) = 1;
	}
	if (errexit)
		Flag(FERREXIT) = 1;

	if (Flag(FTALKING)) {
		hist_init(s);
		alarm_init();
	} else
		Flag(FTRACKALL) = 1;	/* set after ENV */

	shell(s, true);	/* doesn't return */
	return 0;
}

static void
init_username(void)
{
	char *p;
	struct tbl *vp = global("USER");

	if (vp->flag & ISSET)
		p = ksheuid == 0 ? "root" : str_val(vp);
	else
		p = getlogin();

	strlcpy(username, p != NULL ? p : "?", sizeof username);
}

int
include(const char *name, int argc, char **argv, int intr_ok)
{
	Source *volatile s = NULL;
	struct shf *shf;
	char **volatile old_argv;
	volatile int old_argc;
	int i;

	shf = shf_open(name, O_RDONLY, 0, SHF_MAPHI|SHF_CLEXEC);
	if (shf == NULL)
		return -1;

	if (argv) {
		old_argv = genv->loc->argv;
		old_argc = genv->loc->argc;
	} else {
		old_argv = NULL;
		old_argc = 0;
	}
	newenv(E_INCL);
	i = sigsetjmp(genv->jbuf, 0);
	if (i) {
		quitenv(s ? s->u.shf : NULL);
		if (old_argv) {
			genv->loc->argv = old_argv;
			genv->loc->argc = old_argc;
		}
		switch (i) {
		case LRETURN:
		case LERROR:
			return exstat & 0xff; /* see below */
		case LINTR:
			/* intr_ok is set if we are including .profile or $ENV.
			 * If user ^C's out, we don't want to kill the shell...
			 */
			if (intr_ok && (exstat - 128) != SIGTERM)
				return 1;
			/* FALLTHROUGH */
		case LEXIT:
		case LLEAVE:
		case LSHELL:
			unwind(i);
			/* NOTREACHED */
		default:
			internal_errorf("%s: %d", __func__, i);
			/* NOTREACHED */
		}
	}
	if (argv) {
		genv->loc->argv = argv;
		genv->loc->argc = argc;
	}
	s = pushs(SFILE, ATEMP);
	s->u.shf = shf;
	s->file = str_save(name, ATEMP);
	i = shell(s, false);
	quitenv(s->u.shf);
	if (old_argv) {
		genv->loc->argv = old_argv;
		genv->loc->argc = old_argc;
	}
	return i & 0xff;	/* & 0xff to ensure value not -1 */
}

/*
 * spawn a command into a shell optionally keeping track of line
 * number.
 */
int
command(const char *comm, int line)
{
	Source *s;

	s = pushs(SSTRING, ATEMP);
	s->start = s->str = comm;
	s->line = line;
	return shell(s, false);
}

/*
 * run the commands from the input source, returning status.
 */
int
shell(Source *volatile s, volatile int toplevel)
{
	struct op *t;
	volatile int wastty = s->flags & SF_TTY;
	volatile int attempts = 13;
	volatile int interactive = Flag(FTALKING) && toplevel;
	Source *volatile old_source = source;
	int i;

	newenv(E_PARSE);
	if (interactive)
		really_exit = 0;
	i = sigsetjmp(genv->jbuf, 0);
	if (i) {
		switch (i) {
		case LINTR: /* we get here if SIGINT not caught or ignored */
		case LERROR:
		case LSHELL:
			if (interactive) {
				c_fc_reset();
				if (i == LINTR)
					shellf("\n");
				/* Reset any eof that was read as part of a
				 * multiline command.
				 */
				if (Flag(FIGNOREEOF) && s->type == SEOF &&
				    wastty)
					s->type = SSTDIN;
				/* Used by exit command to get back to
				 * top level shell.  Kind of strange since
				 * interactive is set if we are reading from
				 * a tty, but to have stopped jobs, one only
				 * needs FMONITOR set (not FTALKING/SF_TTY)...
				 */
				/* toss any input we have so far */
				s->start = s->str = null;
				break;
			}
			/* FALLTHROUGH */
		case LEXIT:
		case LLEAVE:
		case LRETURN:
			source = old_source;
			quitenv(NULL);
			unwind(i);	/* keep on going */
			/* NOTREACHED */
		default:
			source = old_source;
			quitenv(NULL);
			internal_errorf("%s: %d", __func__, i);
			/* NOTREACHED */
		}
	}

	while (1) {
		if (trap)
			runtraps(0);

		if (s->next == NULL) {
			if (Flag(FVERBOSE))
				s->flags |= SF_ECHO;
			else
				s->flags &= ~SF_ECHO;
		}

		if (interactive) {
			got_sigwinch = 1;
			j_notify();
#ifndef SMALL
			mcheck();
#endif /* SMALL */
			set_prompt(PS1);
		}

		t = compile(s);
		if (t != NULL && t->type == TEOF) {
			if (wastty && Flag(FIGNOREEOF) && --attempts > 0) {
				shellf("Use `exit' to leave ksh\n");
				s->type = SSTDIN;
			} else if (wastty && !really_exit &&
			    j_stopped_running()) {
				really_exit = 1;
				s->type = SSTDIN;
			} else {
				/* this for POSIX, which says EXIT traps
				 * shall be taken in the environment
				 * immediately after the last command
				 * executed.
				 */
				if (toplevel)
					unwind(LEXIT);
				break;
			}
		}

		if (t && (!Flag(FNOEXEC) || (s->flags & SF_TTY)))
			exstat = execute(t, 0, NULL);

		if (t != NULL && t->type != TEOF && interactive && really_exit)
			really_exit = 0;

		reclaim();
	}
	quitenv(NULL);
	source = old_source;
	return exstat;
}

/* return to closest error handler or shell(), exit if none found */
void
unwind(int i)
{
	/* ordering for EXIT vs ERR is a bit odd (this is what at&t ksh does) */
	if (i == LEXIT || (Flag(FERREXIT) && (i == LERROR || i == LINTR) &&
	    sigtraps[SIGEXIT_].trap)) {
		if (trap)
			runtraps(0);
		runtrap(&sigtraps[SIGEXIT_]);
		i = LLEAVE;
	} else if (Flag(FERREXIT) && (i == LERROR || i == LINTR)) {
		if (trap)
			runtraps(0);
		runtrap(&sigtraps[SIGERR_]);
		i = LLEAVE;
	}
	while (1) {
		switch (genv->type) {
		case E_PARSE:
		case E_FUNC:
		case E_INCL:
		case E_LOOP:
		case E_ERRH:
			siglongjmp(genv->jbuf, i);
			/* NOTREACHED */

		case E_NONE:
			if (i == LINTR)
				genv->flags |= EF_FAKE_SIGDIE;
			/* FALLTHROUGH */

		default:
			quitenv(NULL);
			/*
			 * quitenv() may have reclaimed the memory
			 * used by source which will end badly when
			 * we jump to a function that expects it to
			 * be valid
			 */
			source = NULL;
		}
	}
}

void
newenv(int type)
{
	struct env *ep;

	ep = alloc(sizeof(*ep), ATEMP);
	ep->type = type;
	ep->flags = 0;
	ainit(&ep->area);
	ep->loc = genv->loc;
	ep->savefd = NULL;
	ep->oenv = genv;
	ep->temps = NULL;
	genv = ep;
}

void
quitenv(struct shf *shf)
{
	struct env *ep = genv;
	int fd;

	if (ep->oenv && ep->oenv->loc != ep->loc)
		popblock();
	if (ep->savefd != NULL) {
		for (fd = 0; fd < NUFILE; fd++)
			/* if ep->savefd[fd] < 0, means fd was closed */
			if (ep->savefd[fd])
				restfd(fd, ep->savefd[fd]);
		if (ep->savefd[2]) /* Clear any write errors */
			shf_reopen(2, SHF_WR, shl_out);
	}

	/* Bottom of the stack.
	 * Either main shell is exiting or cleanup_parents_env() was called.
	 */
	if (ep->oenv == NULL) {
		if (ep->type == E_NONE) {	/* Main shell exiting? */
			if (Flag(FTALKING))
				hist_finish();
			j_exit();
			if (ep->flags & EF_FAKE_SIGDIE) {
				int sig = exstat - 128;

				/* ham up our death a bit (at&t ksh
				 * only seems to do this for SIGTERM)
				 * Don't do it for SIGQUIT, since we'd
				 * dump a core..
				 */
				if ((sig == SIGINT || sig == SIGTERM) &&
				    getpgrp() == kshpid) {
					setsig(&sigtraps[sig], SIG_DFL,
					    SS_RESTORE_CURR|SS_FORCE);
					kill(0, sig);
				}
			}
		}
		if (shf)
			shf_close(shf);
		reclaim();
		exit(exstat);
	}
	if (shf)
		shf_close(shf);
	reclaim();

	genv = genv->oenv;
	afree(ep, ATEMP);
}

/* Called after a fork to cleanup stuff left over from parents environment */
void
cleanup_parents_env(void)
{
	struct env *ep;
	int fd;

	/* Don't clean up temporary files - parent will probably need them.
	 * Also, can't easily reclaim memory since variables, etc. could be
	 * anywhere.
	 */

	/* close all file descriptors hiding in savefd */
	for (ep = genv; ep; ep = ep->oenv) {
		if (ep->savefd) {
			for (fd = 0; fd < NUFILE; fd++)
				if (ep->savefd[fd] > 0)
					close(ep->savefd[fd]);
			afree(ep->savefd, &ep->area);
			ep->savefd = NULL;
		}
	}
	genv->oenv = NULL;
}

/* Called just before an execve cleanup stuff temporary files */
void
cleanup_proc_env(void)
{
	struct env *ep;

	for (ep = genv; ep; ep = ep->oenv)
		remove_temps(ep->temps);
}

/* remove temp files and free ATEMP Area */
static void
reclaim(void)
{
	remove_temps(genv->temps);
	genv->temps = NULL;
	afreeall(&genv->area);
}

static void
remove_temps(struct temp *tp)
{

	for (; tp != NULL; tp = tp->next)
		if (tp->pid == procpid) {
			unlink(tp->name);
		}
}

/* Returns true if name refers to a restricted shell */
static int
is_restricted(char *name)
{
	char *p;

	if ((p = strrchr(name, '/')))
		name = p + 1;
	/* accepts rsh, rksh, rpdksh, pdrksh */
	if (strcmp(name, "rsh") && \
		strcmp(name, "rksh") && \
		strcmp(name, "rpdksh") && \
		strcmp(name, "pdrksh"))
		return(0);
	else
		return(1);

}
