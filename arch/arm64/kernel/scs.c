// SPDX-License-Identifier: GPL-2.0
/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2019 Google LLC
 */

#include <linux/percpu.h>
#include <asm/scs.h>

/* Allocate a static per-CPU shadow stack */
#define DEFINE_SCS(name)						\
	DEFINE_PER_CPU(unsigned long [SCS_SIZE/sizeof(long)], name)	\

DEFINE_SCS(irq_shadow_call_stack);
