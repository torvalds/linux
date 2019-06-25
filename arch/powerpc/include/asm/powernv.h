/*
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERNV_H
#define _ASM_POWERNV_H

#ifdef CONFIG_PPC_POWERNV
extern void powernv_set_nmmu_ptcr(unsigned long ptcr);

void pnv_program_cpu_hotplug_lpcr(unsigned int cpu, u64 lpcr_val);

void pnv_tm_init(void);
#else
static inline void powernv_set_nmmu_ptcr(unsigned long ptcr) { }

static inline void pnv_tm_init(void) { }
#endif

#endif /* _ASM_POWERNV_H */
