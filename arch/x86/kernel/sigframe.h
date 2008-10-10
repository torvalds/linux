#ifdef CONFIG_X86_32
struct sigframe {
	char __user *pretcode;
	int sig;
	struct sigcontext sc;
	struct _fpstate fpstate;
	unsigned long extramask[_NSIG_WORDS-1];
	char retcode[8];
};

struct rt_sigframe {
	char __user *pretcode;
	int sig;
	struct siginfo __user *pinfo;
	void __user *puc;
	struct siginfo info;
	struct ucontext uc;
	struct _fpstate fpstate;
	char retcode[8];
};
#else
struct rt_sigframe {
	char __user *pretcode;
	struct ucontext uc;
	struct siginfo info;
};

int ia32_setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
		sigset_t *set, struct pt_regs *regs);
int ia32_setup_frame(int sig, struct k_sigaction *ka,
		sigset_t *set, struct pt_regs *regs);
#endif
