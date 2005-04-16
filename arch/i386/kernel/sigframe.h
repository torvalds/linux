struct sigframe
{
	char *pretcode;
	int sig;
	struct sigcontext sc;
	struct _fpstate fpstate;
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[8];
};

struct rt_sigframe
{
	char *pretcode;
	int sig;
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	struct _fpstate fpstate;
	char retcode[8];
};
