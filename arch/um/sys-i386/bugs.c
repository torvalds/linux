/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include "kern_constants.h"
#include "os.h"
#include "task.h"
#include "user.h"
#include "sysdep/archsetjmp.h"

/* Set during early boot */
int host_has_cmov = 1;
static jmp_buf cmov_test_return;

static void cmov_sigill_test_handler(int sig)
{
	host_has_cmov = 0;
	longjmp(cmov_test_return, 1);
}

static void test_for_host_cmov(void)
{
	struct sigaction old, new;

	printk(UM_KERN_INFO "Checking for host processor cmov support...");
	new.sa_handler = cmov_sigill_test_handler;

	/* Make sure that SIGILL is enabled after the handler longjmps back */
	new.sa_flags = SA_NODEFER;
	sigemptyset(&new.sa_mask);
	sigaction(SIGILL, &new, &old);

	if (setjmp(cmov_test_return) == 0) {
		unsigned long foo = 0;
		__asm__ __volatile__("cmovz %0, %1" : "=r" (foo) : "0" (foo));
		printk(UM_KERN_CONT "Yes\n");
	} else
		printk(UM_KERN_CONT "No\n");

	sigaction(SIGILL, &old, &new);
}

void arch_init_thread(void)
{
}

void arch_check_bugs(void)
{
	test_for_host_cmov();
}

int arch_handle_signal(int sig, struct uml_pt_regs *regs)
{
	unsigned char tmp[2];

	/*
	 * This is testing for a cmov (0x0f 0x4x) instruction causing a
	 * SIGILL in init.
	 */
	if ((sig != SIGILL) || (TASK_PID(get_current()) != 1))
		return 0;

	if (copy_from_user_proc(tmp, (void *) UPT_IP(regs), 2))
		panic("SIGILL in init, could not read instructions!\n");
	if ((tmp[0] != 0x0f) || ((tmp[1] & 0xf0) != 0x40))
		return 0;

	if (host_has_cmov == 0)
		panic("SIGILL caused by cmov, which this processor doesn't "
		      "implement, boot a filesystem compiled for older "
		      "processors");
	else if (host_has_cmov == 1)
		panic("SIGILL caused by cmov, which this processor claims to "
		      "implement");
	else panic("Bad value for host_has_cmov (%d)", host_has_cmov);
	return 0;
}
