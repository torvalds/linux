#ifdef CONFIG_X86_32
#define sigframe_ia32		sigframe
#define rt_sigframe_ia32	rt_sigframe
#define sigcontext_ia32		sigcontext
#define _fpstate_ia32		_fpstate
#define ucontext_ia32		ucontext

struct sigframe_ia32 {
	u32 pretcode;
	int sig;
	struct sigcontext_ia32 sc;
	/*
	 * fpstate is unused. fpstate is moved/allocated after
	 * retcode[] below. This movement allows to have the FP state and the
	 * future state extensions (xsave) stay together.
	 * And at the same time retaining the unused fpstate, prevents changing
	 * the offset of extramask[] in the sigframe and thus prevent any
	 * legacy application accessing/modifying it.
	 */
	struct _fpstate_ia32 fpstate_unused;
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[8];
	/* fp state follows here */
};

struct rt_sigframe_ia32 {
	u32 pretcode;
	int sig;
	u32 pinfo;
	u32 puc;
	struct siginfo info;
	struct ucontext_ia32 uc;
	char retcode[8];
	/* fp state follows here */
};
#else /* !CONFIG_X86_32 */
struct rt_sigframe {
	char __user *pretcode;
	struct ucontext uc;
	struct siginfo info;
	/* fp state follows here */
};
#endif /* CONFIG_X86_32 */
