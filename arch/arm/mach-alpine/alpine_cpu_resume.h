/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Annapurna labs cpu-resume register structure.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
 */

#ifndef ALPINE_CPU_RESUME_H_
#define ALPINE_CPU_RESUME_H_

/* Per-cpu regs */
struct al_cpu_resume_regs_per_cpu {
	uint32_t	flags;
	uint32_t	resume_addr;
};

/* general regs */
struct al_cpu_resume_regs {
	/* Watermark for validating the CPU resume struct */
	uint32_t watermark;
	uint32_t flags;
	struct al_cpu_resume_regs_per_cpu per_cpu[];
};

/* The expected magic number for validating the resume addresses */
#define AL_CPU_RESUME_MAGIC_NUM		0xf0e1d200
#define AL_CPU_RESUME_MAGIC_NUM_MASK	0xffffff00

#endif /* ALPINE_CPU_RESUME_H_ */
