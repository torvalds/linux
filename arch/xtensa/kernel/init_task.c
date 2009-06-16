/*
 * arch/xtensa/kernel/init_task.c
 *
 * Xtensa Processor version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/module.h>
#include <linux/mqueue.h>

#include <asm/uaccess.h>

static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
union thread_union init_thread_union
	__attribute__((__section__(".data.init_task"))) =
{ INIT_THREAD_INFO(init_task) };

struct task_struct init_task = INIT_TASK(init_task);

EXPORT_SYMBOL(init_task);
