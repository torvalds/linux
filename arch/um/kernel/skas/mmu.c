/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/list.h"
#include "linux/spinlock.h"
#include "linux/slab.h"
#include "asm/current.h"
#include "asm/segment.h"
#include "asm/mmu.h"
#include "os.h"
#include "skas.h"

int init_new_context_skas(struct task_struct *task, struct mm_struct *mm)
{
	int from;

	if((current->mm != NULL) && (current->mm != &init_mm))
		from = current->mm->context.skas.mm_fd;
	else from = -1;

	mm->context.skas.mm_fd = new_mm(from);
	if(mm->context.skas.mm_fd < 0){
		printk("init_new_context_skas - new_mm failed, errno = %d\n",
		       mm->context.skas.mm_fd);
		return(mm->context.skas.mm_fd);
	}

	return(0);
}

void destroy_context_skas(struct mm_struct *mm)
{
	os_close_file(mm->context.skas.mm_fd);
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
