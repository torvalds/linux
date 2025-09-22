/*	$OpenBSD: trap.c,v 1.33 2018/12/08 21:03:51 jca Exp $	*/

/*
 * signal handling
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"

Trap sigtraps[NSIG + 1];

static struct sigaction Sigact_ign, Sigact_trap;

void
inittraps(void)
{
	int	i;

	/* Populate sigtraps based on sys_signame and sys_siglist. */
	for (i = 0; i <= NSIG; i++) {
		sigtraps[i].signal = i;
		if (i == SIGERR_) {
			sigtraps[i].name = "ERR";
			sigtraps[i].mess = "Error handler";
		} else {
			sigtraps[i].name = sys_signame[i];
			sigtraps[i].mess = sys_siglist[i];
		}
	}
	sigtraps[SIGEXIT_].name = "EXIT";	/* our name for signal 0 */

	sigemptyset(&Sigact_ign.sa_mask);
	Sigact_ign.sa_flags = 0; /* interruptible */
	Sigact_ign.sa_handler = SIG_IGN;
	Sigact_trap = Sigact_ign;
	Sigact_trap.sa_handler = trapsig;

	sigtraps[SIGINT].flags |= TF_DFL_INTR | TF_TTY_INTR;
	sigtraps[SIGQUIT].flags |= TF_DFL_INTR | TF_TTY_INTR;
	sigtraps[SIGTERM].flags |= TF_DFL_INTR;/* not fatal for interactive */
	sigtraps[SIGHUP].flags |= TF_FATAL;
	sigtraps[SIGCHLD].flags |= TF_SHELL_USES;

	/* these are always caught so we can clean up any temporary files. */
	setsig(&sigtraps[SIGINT], trapsig, SS_RESTORE_ORIG);
	setsig(&sigtraps[SIGQUIT], trapsig, SS_RESTORE_ORIG);
	setsig(&sigtraps[SIGTERM], trapsig, SS_RESTORE_ORIG);
	setsig(&sigtraps[SIGHUP], trapsig, SS_RESTORE_ORIG);
}

static void alarm_catcher(int sig);

void
alarm_init(void)
{
	sigtraps[SIGALRM].flags |= TF_SHELL_USES;
	setsig(&sigtraps[SIGALRM], alarm_catcher,
		SS_RESTORE_ORIG|SS_FORCE|SS_SHTRAP);
}

static void
alarm_catcher(int sig)
{
	int errno_ = errno;

	if (ksh_tmout_state == TMOUT_READING) {
		int left = alarm(0);

		if (left == 0) {
			ksh_tmout_state = TMOUT_LEAVING;
			intrsig = 1;
		} else
			alarm(left);
	}
	errno = errno_;
}

Trap *
gettrap(const char *name, int igncase)
{
	int i;
	Trap *p;

	if (digit(*name)) {
		int n;

		if (getn(name, &n) && 0 <= n && n < NSIG)
			return &sigtraps[n];
		return NULL;
	}

	if (igncase && strncasecmp(name, "SIG", 3) == 0)
		name += 3;
	if (!igncase && strncmp(name, "SIG", 3) == 0)
		name += 3;

	for (p = sigtraps, i = NSIG+1; --i >= 0; p++)
		if (p->name) {
			if (igncase && strcasecmp(p->name, name) == 0)
				return p;
			if (!igncase && strcmp(p->name, name) == 0)
				return p;
		}
	return NULL;
}

/*
 * trap signal handler
 */
void
trapsig(int i)
{
	Trap *p = &sigtraps[i];
	int errno_ = errno;

	trap = p->set = 1;
	if (p->flags & TF_DFL_INTR)
		intrsig = 1;
	if ((p->flags & TF_FATAL) && !p->trap) {
		fatal_trap = 1;
		intrsig = 1;
	}
	if (p->shtrap)
		(*p->shtrap)(i);
	errno = errno_;
}

/* called when we want to allow the user to ^C out of something - won't
 * work if user has trapped SIGINT.
 */
void
intrcheck(void)
{
	if (intrsig)
		runtraps(TF_DFL_INTR|TF_FATAL);
}

/* called after EINTR to check if a signal with normally causes process
 * termination has been received.
 */
int
fatal_trap_check(void)
{
	int i;
	Trap *p;

	/* todo: should check if signal is fatal, not the TF_DFL_INTR flag */
	for (p = sigtraps, i = NSIG+1; --i >= 0; p++)
		if (p->set && (p->flags & (TF_DFL_INTR|TF_FATAL)))
			/* return value is used as an exit code */
			return 128 + p->signal;
	return 0;
}

/* Returns the signal number of any pending traps: ie, a signal which has
 * occurred for which a trap has been set or for which the TF_DFL_INTR flag
 * is set.
 */
int
trap_pending(void)
{
	int i;
	Trap *p;

	for (p = sigtraps, i = NSIG+1; --i >= 0; p++)
		if (p->set && ((p->trap && p->trap[0]) ||
		    ((p->flags & (TF_DFL_INTR|TF_FATAL)) && !p->trap)))
			return p->signal;
	return 0;
}

/*
 * run any pending traps.  If intr is set, only run traps that
 * can interrupt commands.
 */
void
runtraps(int flag)
{
	int i;
	Trap *p;

	if (ksh_tmout_state == TMOUT_LEAVING) {
		ksh_tmout_state = TMOUT_EXECUTING;
		warningf(false, "timed out waiting for input");
		unwind(LEXIT);
	} else
		/* XXX: this means the alarm will have no effect if a trap
		 * is caught after the alarm() was started...not good.
		 */
		ksh_tmout_state = TMOUT_EXECUTING;
	if (!flag)
		trap = 0;
	if (flag & TF_DFL_INTR)
		intrsig = 0;
	if (flag & TF_FATAL)
		fatal_trap = 0;
	for (p = sigtraps, i = NSIG+1; --i >= 0; p++)
		if (p->set && (!flag ||
		    ((p->flags & flag) && p->trap == NULL)))
			runtrap(p);
}

void
runtrap(Trap *p)
{
	int	i = p->signal;
	char	*trapstr = p->trap;
	int	oexstat;
	int	old_changed = 0;

	p->set = 0;
	if (trapstr == NULL) { /* SIG_DFL */
		if (p->flags & TF_FATAL) {
			/* eg, SIGHUP */
			exstat = 128 + i;
			unwind(LLEAVE);
		}
		if (p->flags & TF_DFL_INTR) {
			/* eg, SIGINT, SIGQUIT, SIGTERM, etc. */
			exstat = 128 + i;
			unwind(LINTR);
		}
		return;
	}
	if (trapstr[0] == '\0') /* SIG_IGN */
		return;
	if (i == SIGEXIT_ || i == SIGERR_) {	/* avoid recursion on these */
		old_changed = p->flags & TF_CHANGED;
		p->flags &= ~TF_CHANGED;
		p->trap = NULL;
	}
	oexstat = exstat;
	/* Note: trapstr is fully parsed before anything is executed, thus
	 * no problem with afree(p->trap) in settrap() while still in use.
	 */
	command(trapstr, current_lineno);
	exstat = oexstat;
	if (i == SIGEXIT_ || i == SIGERR_) {
		if (p->flags & TF_CHANGED)
			/* don't clear TF_CHANGED */
			afree(trapstr, APERM);
		else
			p->trap = trapstr;
		p->flags |= old_changed;
	}
}

/* clear pending traps and reset user's trap handlers; used after fork(2) */
void
cleartraps(void)
{
	int i;
	Trap *p;

	trap = 0;
	intrsig = 0;
	fatal_trap = 0;
	for (i = NSIG+1, p = sigtraps; --i >= 0; p++) {
		p->set = 0;
		if ((p->flags & TF_USER_SET) && (p->trap && p->trap[0]))
			settrap(p, NULL);
	}
}

/* restore signals just before an exec(2) */
void
restoresigs(void)
{
	int i;
	Trap *p;

	for (i = NSIG+1, p = sigtraps; --i >= 0; p++)
		if (p->flags & (TF_EXEC_IGN|TF_EXEC_DFL))
			setsig(p, (p->flags & TF_EXEC_IGN) ? SIG_IGN : SIG_DFL,
			    SS_RESTORE_CURR|SS_FORCE);
}

void
settrap(Trap *p, char *s)
{
	sig_t f;

	afree(p->trap, APERM);
	p->trap = str_save(s, APERM); /* handles s == 0 */
	p->flags |= TF_CHANGED;
	f = !s ? SIG_DFL : s[0] ? trapsig : SIG_IGN;

	p->flags |= TF_USER_SET;
	if ((p->flags & (TF_DFL_INTR|TF_FATAL)) && f == SIG_DFL)
		f = trapsig;
	else if (p->flags & TF_SHELL_USES) {
		if (!(p->flags & TF_ORIG_IGN) || Flag(FTALKING)) {
			/* do what user wants at exec time */
			p->flags &= ~(TF_EXEC_IGN|TF_EXEC_DFL);
			if (f == SIG_IGN)
				p->flags |= TF_EXEC_IGN;
			else
				p->flags |= TF_EXEC_DFL;
		}

		/* assumes handler already set to what shell wants it
		 * (normally trapsig, but could be j_sigchld() or SIG_IGN)
		 */
		return;
	}

	/* todo: should we let user know signal is ignored? how? */
	setsig(p, f, SS_RESTORE_CURR|SS_USER);
}

/* Called by c_print() when writing to a co-process to ensure SIGPIPE won't
 * kill shell (unless user catches it and exits)
 */
int
block_pipe(void)
{
	int restore_dfl = 0;
	Trap *p = &sigtraps[SIGPIPE];

	if (!(p->flags & (TF_ORIG_IGN|TF_ORIG_DFL))) {
		setsig(p, SIG_IGN, SS_RESTORE_CURR);
		if (p->flags & TF_ORIG_DFL)
			restore_dfl = 1;
	} else if (p->cursig == SIG_DFL) {
		setsig(p, SIG_IGN, SS_RESTORE_CURR);
		restore_dfl = 1; /* restore to SIG_DFL */
	}
	return restore_dfl;
}

/* Called by c_print() to undo whatever block_pipe() did */
void
restore_pipe(int restore_dfl)
{
	if (restore_dfl)
		setsig(&sigtraps[SIGPIPE], SIG_DFL, SS_RESTORE_CURR);
}

/* Set action for a signal.  Action may not be set if original
 * action was SIG_IGN, depending on the value of flags and
 * FTALKING.
 */
int
setsig(Trap *p, sig_t f, int flags)
{
	struct sigaction sigact;

	if (p->signal == SIGEXIT_ || p->signal == SIGERR_)
		return 1;

	/* First time setting this signal?  If so, get and note the current
	 * setting.
	 */
	if (!(p->flags & (TF_ORIG_IGN|TF_ORIG_DFL))) {
		sigaction(p->signal, &Sigact_ign, &sigact);
		p->flags |= sigact.sa_handler == SIG_IGN ?
		    TF_ORIG_IGN : TF_ORIG_DFL;
		p->cursig = SIG_IGN;
	}

	/* Generally, an ignored signal stays ignored, except if
	 *	- the user of an interactive shell wants to change it
	 *	- the shell wants for force a change
	 */
	if ((p->flags & TF_ORIG_IGN) && !(flags & SS_FORCE) &&
	    (!(flags & SS_USER) || !Flag(FTALKING)))
		return 0;

	setexecsig(p, flags & SS_RESTORE_MASK);

	/* This is here 'cause there should be a way of clearing shtraps, but
	 * don't know if this is a sane way of doing it.  At the moment,
	 * all users of shtrap are lifetime users (SIGCHLD, SIGALRM, SIGWINCH).
	 */
	if (!(flags & SS_USER))
		p->shtrap = NULL;
	if (flags & SS_SHTRAP) {
		p->shtrap = f;
		f = trapsig;
	}

	if (p->cursig != f) {
		p->cursig = f;
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = 0 /* interruptible */;
		sigact.sa_handler = f;
		sigaction(p->signal, &sigact, NULL);
	}

	return 1;
}

/* control what signal is set to before an exec() */
void
setexecsig(Trap *p, int restore)
{
	/* XXX debugging */
	if (!(p->flags & (TF_ORIG_IGN|TF_ORIG_DFL)))
		internal_errorf("%s: unset signal %d(%s)",
		    __func__, p->signal, p->name);

	/* restore original value for exec'd kids */
	p->flags &= ~(TF_EXEC_IGN|TF_EXEC_DFL);
	switch (restore & SS_RESTORE_MASK) {
	case SS_RESTORE_CURR: /* leave things as they currently are */
		break;
	case SS_RESTORE_ORIG:
		p->flags |= p->flags & TF_ORIG_IGN ? TF_EXEC_IGN : TF_EXEC_DFL;
		break;
	case SS_RESTORE_DFL:
		p->flags |= TF_EXEC_DFL;
		break;
	case SS_RESTORE_IGN:
		p->flags |= TF_EXEC_IGN;
		break;
	}
}
