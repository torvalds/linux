// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM nVHE hypervisor stack tracing support.
 *
 * Copyright (C) 2022 Google LLC
 */
#include <asm/memory.h>
#include <asm/percpu.h>

DEFINE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack)
	__aligned(16);
