/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Save registers before calling assembly functions. This avoids
 * disturbance of register allocation in some inline assembly constructs.
 * Copyright 2001,2002 by Andi Kleen, SuSE Labs.
 */
#include <linux/export.h>
#include <linux/linkage.h>
#include "calling.h"
#include <asm/asm.h>

THUNK preempt_schedule_thunk, preempt_schedule
THUNK preempt_schedule_notrace_thunk, preempt_schedule_notrace
EXPORT_SYMBOL(preempt_schedule_thunk)
EXPORT_SYMBOL(preempt_schedule_notrace_thunk)
