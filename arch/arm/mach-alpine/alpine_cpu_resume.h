/*
 * Annapurna labs cpu-resume register structure.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
