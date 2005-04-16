/* $Id: signal.h,v 1.3 1998/04/12 06:20:33 davem Exp $
 * signal.h: Signal emulation for Solaris
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
    
#define SOLARIS_SIGHUP		1
#define SOLARIS_SIGINT		2
#define SOLARIS_SIGQUIT		3
#define SOLARIS_SIGILL		4
#define SOLARIS_SIGTRAP		5
#define SOLARIS_SIGIOT		6
#define SOLARIS_SIGEMT		7
#define SOLARIS_SIGFPE		8
#define SOLARIS_SIGKILL		9
#define SOLARIS_SIGBUS		10
#define SOLARIS_SIGSEGV		11
#define SOLARIS_SIGSYS		12
#define SOLARIS_SIGPIPE		13
#define SOLARIS_SIGALRM		14
#define SOLARIS_SIGTERM		15
#define SOLARIS_SIGUSR1		16
#define SOLARIS_SIGUSR2		17
#define SOLARIS_SIGCLD		18
#define SOLARIS_SIGPWR		19
#define SOLARIS_SIGWINCH	20
#define SOLARIS_SIGURG		21
#define SOLARIS_SIGPOLL		22
#define SOLARIS_SIGSTOP		23
#define SOLARIS_SIGTSTP		24
#define SOLARIS_SIGCONT		25
#define SOLARIS_SIGTTIN		26
#define SOLARIS_SIGTTOU		27
#define SOLARIS_SIGVTALRM	28
#define SOLARIS_SIGPROF		29
#define SOLARIS_SIGXCPU		30
#define SOLARIS_SIGXFSZ		31
#define SOLARIS_SIGWAITING	32
#define SOLARIS_SIGLWP		33
#define SOLARIS_SIGFREEZE	34
#define SOLARIS_SIGTHAW		35
#define SOLARIS_SIGCANCEL	36
#define SOLARIS_SIGRTMIN	37
#define SOLARIS_SIGRTMAX	44
#define SOLARIS_NSIGNALS	44


#define SOLARIS_SA_ONSTACK	1
#define SOLARIS_SA_RESETHAND	2
#define SOLARIS_SA_RESTART	4
#define SOLARIS_SA_SIGINFO	8
#define SOLARIS_SA_NODEFER	16
#define SOLARIS_SA_NOCLDWAIT	0x10000
#define SOLARIS_SA_NOCLDSTOP	0x20000

struct sol_siginfo {
	int	si_signo;
	int	si_code;
	int	si_errno;
	union	{
		char	pad[128-3*sizeof(int)];
		struct { 
			s32	_pid;
			union {
				struct {
					s32	_uid;
					s32	_value;
				} _kill;
				struct {
					s32	_utime;
					int	_status;
					s32	_stime;
				} _cld;
			} _pdata;
		} _proc;
		struct { /* SIGSEGV, SIGBUS, SIGILL and SIGFPE */
			u32	_addr;
			int	_trapno;
		} _fault;
		struct { /* SIGPOLL, SIGXFSZ */
			int	_fd;
			s32	_band;
		} _file;
	} _data;
};

#define SOLARIS_WUNTRACED	0x04
#define SOLARIS_WNOHANG		0x40
#define SOLARIS_WEXITED         0x01
#define SOLARIS_WTRAPPED        0x02
#define SOLARIS_WSTOPPED        WUNTRACED
#define SOLARIS_WCONTINUED      0x08
#define SOLARIS_WNOWAIT         0x80

#define SOLARIS_TRAP_BRKPT      1
#define SOLARIS_TRAP_TRACE      2
#define SOLARIS_CLD_EXITED      1
#define SOLARIS_CLD_KILLED      2
#define SOLARIS_CLD_DUMPED      3
#define SOLARIS_CLD_TRAPPED     4
#define SOLARIS_CLD_STOPPED     5
#define SOLARIS_CLD_CONTINUED   6
#define SOLARIS_POLL_IN         1
#define SOLARIS_POLL_OUT        2
#define SOLARIS_POLL_MSG        3
#define SOLARIS_POLL_ERR        4
#define SOLARIS_POLL_PRI        5
#define SOLARIS_POLL_HUP        6
