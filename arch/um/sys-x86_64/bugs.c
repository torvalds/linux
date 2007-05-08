/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include "sysdep/ptrace.h"

void arch_init_thread(void)
{
}

void arch_check_bugs(void)
{
}

int arch_handle_signal(int sig, union uml_pt_regs *regs)
{
	return 0;
}
