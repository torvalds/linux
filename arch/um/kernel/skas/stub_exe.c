#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/fcntl.h>
#include <asm/unistd.h>
#include <sysdep/stub.h>
#include <stub-data.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <generated/asm-offsets.h>

void _start(void);

noinline static void real_init(void)
{
	struct stub_init_data init_data;
	unsigned long res;
	struct {
		void  *ss_sp;
		int    ss_flags;
		size_t ss_size;
	} stack = {
		.ss_size = STUB_DATA_PAGES * UM_KERN_PAGE_SIZE,
	};
	struct {
		void *sa_handler_;
		unsigned long sa_flags;
		void *sa_restorer;
		unsigned long long sa_mask;
	} sa = {
		/* Need to set SA_RESTORER (but the handler never returns) */
		.sa_flags = SA_ONSTACK | SA_NODEFER | SA_SIGINFO | 0x04000000,
	};

	/* set a nice name */
	stub_syscall2(__NR_prctl, PR_SET_NAME, (unsigned long)"uml-userspace");

	/* Make sure this process dies if the kernel dies */
	stub_syscall2(__NR_prctl, PR_SET_PDEATHSIG, SIGKILL);

	/* Needed in SECCOMP mode (and safe to do anyway) */
	stub_syscall5(__NR_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

	/* read information from STDIN and close it */
	res = stub_syscall3(__NR_read, 0,
			    (unsigned long)&init_data, sizeof(init_data));
	if (res != sizeof(init_data))
		stub_syscall1(__NR_exit, 10);

	/* In SECCOMP mode, FD 0 is a socket and is later used for FD passing */
	if (!init_data.seccomp)
		stub_syscall1(__NR_close, 0);
	else
		stub_syscall3(__NR_fcntl, 0, F_SETFL, O_NONBLOCK);

	/* map stub code + data */
	res = stub_syscall6(STUB_MMAP_NR,
			    init_data.stub_start, UM_KERN_PAGE_SIZE,
			    PROT_READ | PROT_EXEC, MAP_FIXED | MAP_SHARED,
			    init_data.stub_code_fd, init_data.stub_code_offset);
	if (res != init_data.stub_start)
		stub_syscall1(__NR_exit, 11);

	res = stub_syscall6(STUB_MMAP_NR,
			    init_data.stub_start + UM_KERN_PAGE_SIZE,
			    STUB_DATA_PAGES * UM_KERN_PAGE_SIZE,
			    PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
			    init_data.stub_data_fd, init_data.stub_data_offset);
	if (res != init_data.stub_start + UM_KERN_PAGE_SIZE)
		stub_syscall1(__NR_exit, 12);

	/* In SECCOMP mode, we only need the signalling FD from now on */
	if (init_data.seccomp) {
		res = stub_syscall3(__NR_close_range, 1, ~0U, 0);
		if (res != 0)
			stub_syscall1(__NR_exit, 13);
	}

	/* setup signal stack inside stub data */
	stack.ss_sp = (void *)init_data.stub_start + UM_KERN_PAGE_SIZE;
	stub_syscall2(__NR_sigaltstack, (unsigned long)&stack, 0);

	/* register signal handlers */
	sa.sa_handler_ = (void *) init_data.signal_handler;
	sa.sa_restorer = (void *) init_data.signal_restorer;
	if (!init_data.seccomp) {
		/* In ptrace mode, the SIGSEGV handler never returns */
		sa.sa_mask = 0;

		res = stub_syscall4(__NR_rt_sigaction, SIGSEGV,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 14);
	} else {
		/* SECCOMP mode uses rt_sigreturn, need to mask all signals */
		sa.sa_mask = ~0ULL;

		res = stub_syscall4(__NR_rt_sigaction, SIGSEGV,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 15);

		res = stub_syscall4(__NR_rt_sigaction, SIGSYS,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 16);

		res = stub_syscall4(__NR_rt_sigaction, SIGALRM,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 17);

		res = stub_syscall4(__NR_rt_sigaction, SIGTRAP,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 18);

		res = stub_syscall4(__NR_rt_sigaction, SIGILL,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 19);

		res = stub_syscall4(__NR_rt_sigaction, SIGFPE,
				    (unsigned long)&sa, 0, sizeof(sa.sa_mask));
		if (res != 0)
			stub_syscall1(__NR_exit, 20);
	}

	/*
	 * If in seccomp mode, install the SECCOMP filter and trigger a syscall.
	 * Otherwise set PTRACE_TRACEME and do a SIGSTOP.
	 */
	if (init_data.seccomp) {
		struct sock_filter filter[] = {
#if __BITS_PER_LONG > 32
			/* [0] Load upper 32bit of instruction pointer from seccomp_data */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 (offsetof(struct seccomp_data, instruction_pointer) + 4)),

			/* [1] Jump forward 3 instructions if the upper address is not identical */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (init_data.stub_start) >> 32, 0, 3),
#endif
			/* [2] Load lower 32bit of instruction pointer from seccomp_data */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 (offsetof(struct seccomp_data, instruction_pointer))),

			/* [3] Mask out lower bits */
			BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xfffff000),

			/* [4] Jump to [6] if the lower bits are not on the expected page */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (init_data.stub_start) & 0xfffff000, 1, 0),

			/* [5] Trap call, allow */
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),

			/* [6,7] Check architecture */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 offsetof(struct seccomp_data, arch)),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
				 UM_SECCOMP_ARCH_NATIVE, 1, 0),

			/* [8] Kill (for architecture check) */
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

			/* [9] Load syscall number */
			BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
				 offsetof(struct seccomp_data, nr)),

			/* [10-16] Check against permitted syscalls */
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_futex,
				 7, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,__NR_recvmsg,
				 6, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,__NR_close,
				 5, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, STUB_MMAP_NR,
				 4, 0),
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_munmap,
				 3, 0),
#ifdef __i386__
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_set_thread_area,
				 2, 0),
#else
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_arch_prctl,
				 2, 0),
#endif
			BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_rt_sigreturn,
				 1, 0),

			/* [17] Not one of the permitted syscalls */
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

			/* [18] Permitted call for the stub */
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		};
		struct sock_fprog prog = {
			.len = sizeof(filter) / sizeof(filter[0]),
			.filter = filter,
		};

		if (stub_syscall3(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
				  SECCOMP_FILTER_FLAG_TSYNC,
				  (unsigned long)&prog) != 0)
			stub_syscall1(__NR_exit, 21);

		/* Fall through, the exit syscall will cause SIGSYS */
	} else {
		stub_syscall4(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0);

		stub_syscall2(__NR_kill, stub_syscall0(__NR_getpid), SIGSTOP);
	}

	stub_syscall1(__NR_exit, 30);

	__builtin_unreachable();
}

__attribute__((naked)) void _start(void)
{
	/*
	 * Since the stack after exec() starts at the top-most address,
	 * but that's exactly where we also want to map the stub data
	 * and code, this must:
	 *  - push the stack by 1 code and STUB_DATA_PAGES data pages
	 *  - call real_init()
	 * This way, real_init() can use the stack normally, while the
	 * original stack further down (higher address) will become
	 * inaccessible after the mmap() calls above.
	 */
	stub_start(real_init);
}
