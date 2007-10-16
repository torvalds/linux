/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,intel.linux}.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/init_task.h"
#include "linux/fs.h"
#include "linux/module.h"
#include "linux/mqueue.h"
#include "asm/uaccess.h"

static struct fs_struct init_fs = INIT_FS;
struct mm_struct init_mm = INIT_MM(init_mm);
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
EXPORT_SYMBOL(init_mm);

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */

struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_task);

/*
 * Initial thread structure.
 *
 * We need to make sure that this is aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */

union thread_union init_thread_union
	__attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

union thread_union cpu0_irqstack
	__attribute__((__section__(".data.init_irqstack"))) =
		{ INIT_THREAD_INFO(init_task) };
