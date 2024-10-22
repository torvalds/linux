#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <asm/unistd.h>
#include <sysdep/stub.h>
#include <stub-data.h>

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
		/* no need to mask any signals */
		.sa_mask = 0,
	};

	/* set a nice name */
	stub_syscall2(__NR_prctl, PR_SET_NAME, (unsigned long)"uml-userspace");

	/* Make sure this process dies if the kernel dies */
	stub_syscall2(__NR_prctl, PR_SET_PDEATHSIG, SIGKILL);

	/* read information from STDIN and close it */
	res = stub_syscall3(__NR_read, 0,
			    (unsigned long)&init_data, sizeof(init_data));
	if (res != sizeof(init_data))
		stub_syscall1(__NR_exit, 10);

	stub_syscall1(__NR_close, 0);

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

	/* setup signal stack inside stub data */
	stack.ss_sp = (void *)init_data.stub_start + UM_KERN_PAGE_SIZE;
	stub_syscall2(__NR_sigaltstack, (unsigned long)&stack, 0);

	/* register SIGSEGV handler */
	sa.sa_handler_ = (void *) init_data.segv_handler;
	res = stub_syscall4(__NR_rt_sigaction, SIGSEGV, (unsigned long)&sa, 0,
			    sizeof(sa.sa_mask));
	if (res != 0)
		stub_syscall1(__NR_exit, 13);

	stub_syscall4(__NR_ptrace, PTRACE_TRACEME, 0, 0, 0);

	stub_syscall2(__NR_kill, stub_syscall0(__NR_getpid), SIGSTOP);

	stub_syscall1(__NR_exit, 14);

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
