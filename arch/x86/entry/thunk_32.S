/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trampoline to trace irqs off. (otherwise CALLER_ADDR1 might crash)
 * Copyright 2008 by Steven Rostedt, Red Hat, Inc
 *  (inspired by Andi Kleen's thunk_64.S)
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/asm.h>

#include "calling.h"

THUNK preempt_schedule_thunk, preempt_schedule
THUNK preempt_schedule_notrace_thunk, preempt_schedule_notrace
EXPORT_SYMBOL(preempt_schedule_thunk)
EXPORT_SYMBOL(preempt_schedule_notrace_thunk)

