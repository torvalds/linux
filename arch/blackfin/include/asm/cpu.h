/*
 * Copyright 2007-2009 Analog Devices Inc.
 *                         Philippe Gerum <rpm@xenomai.org>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BLACKFIN_CPU_H
#define __ASM_BLACKFIN_CPU_H

#include <linux/percpu.h>

struct task_struct;

struct blackfin_cpudata {
	struct cpu cpu;
	struct task_struct *idle;
	unsigned int imemctl;
	unsigned int dmemctl;
	unsigned long dcache_invld_count;
	unsigned long icache_invld_count;
};

DECLARE_PER_CPU(struct blackfin_cpudata, cpu_data);

#endif
