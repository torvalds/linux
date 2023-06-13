// SPDX-License-Identifier: GPL-2.0

#include <linux/ptrace.h>
#include <asm/bugs.h>
#include <asm/traps.h>

static __ro_after_init bool ibt_fatal = true;

extern void ibt_selftest_ip(void); /* code label defined in asm below */

enum cp_error_code {
	CP_EC        = (1 << 15) - 1,

	CP_RET       = 1,
	CP_IRET      = 2,
	CP_ENDBR     = 3,
	CP_RSTRORSSP = 4,
	CP_SETSSBSY  = 5,

	CP_ENCL	     = 1 << 15,
};

DEFINE_IDTENTRY_ERRORCODE(exc_control_protection)
{
	if (!cpu_feature_enabled(X86_FEATURE_IBT)) {
		pr_err("Unexpected #CP\n");
		BUG();
	}

	if (WARN_ON_ONCE(user_mode(regs) || (error_code & CP_EC) != CP_ENDBR))
		return;

	if (unlikely(regs->ip == (unsigned long)&ibt_selftest_ip)) {
		regs->ax = 0;
		return;
	}

	pr_err("Missing ENDBR: %pS\n", (void *)instruction_pointer(regs));
	if (!ibt_fatal) {
		printk(KERN_DEFAULT CUT_HERE);
		__warn(__FILE__, __LINE__, (void *)regs->ip, TAINT_WARN, regs, NULL);
		return;
	}
	BUG();
}

/* Must be noinline to ensure uniqueness of ibt_selftest_ip. */
noinline bool ibt_selftest(void)
{
	unsigned long ret;

	asm ("	lea ibt_selftest_ip(%%rip), %%rax\n\t"
	     ANNOTATE_RETPOLINE_SAFE
	     "	jmp *%%rax\n\t"
	     "ibt_selftest_ip:\n\t"
	     UNWIND_HINT_FUNC
	     ANNOTATE_NOENDBR
	     "	nop\n\t"

	     : "=a" (ret) : : "memory");

	return !ret;
}

static int __init ibt_setup(char *str)
{
	if (!strcmp(str, "off"))
		setup_clear_cpu_cap(X86_FEATURE_IBT);

	if (!strcmp(str, "warn"))
		ibt_fatal = false;

	return 1;
}

__setup("ibt=", ibt_setup);
