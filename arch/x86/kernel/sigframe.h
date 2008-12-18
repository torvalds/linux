#ifdef CONFIG_X86_32
struct sigframe {
	char __user *pretcode;
	int sig;
	struct sigcontext sc;
	/*
	 * fpstate is unused. fpstate is moved/allocated after
	 * retcode[] below. This movement allows to have the FP state and the
	 * future state extensions (xsave) stay together.
	 * And at the same time retaining the unused fpstate, prevents changing
	 * the offset of extramask[] in the sigframe and thus prevent any
	 * legacy application accessing/modifying it.
	 */
	struct _fpstate fpstate_unused;
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[8];
	/* fp state follows here */
};

struct rt_sigframe {
	char __user *pretcode;
	int sig;
	struct siginfo __user *pinfo;
	void __user *puc;
	struct siginfo info;
	struct ucontext uc;
	char retcode[8];
	/* fp state follows here */
};
#else
struct rt_sigframe {
	char __user *pretcode;
	struct ucontext uc;
	struct siginfo info;
	/* fp state follows here */
};
#endif
