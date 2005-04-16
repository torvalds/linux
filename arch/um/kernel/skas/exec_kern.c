/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/kernel.h"
#include "asm/current.h"
#include "asm/page.h"
#include "asm/signal.h"
#include "asm/ptrace.h"
#include "asm/uaccess.h"
#include "asm/mmu_context.h"
#include "tlb.h"
#include "skas.h"
#include "um_mmu.h"
#include "os.h"

void flush_thread_skas(void)
{
	force_flush_all();
	switch_mm_skas(current->mm->context.skas.mm_fd);
}

void start_thread_skas(struct pt_regs *regs, unsigned long eip, 
		       unsigned long esp)
{
	set_fs(USER_DS);
        PT_REGS_IP(regs) = eip;
	PT_REGS_SP(regs) = esp;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
