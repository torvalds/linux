/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POWERPC_DT_CPU_FTRS_H
#define __ASM_POWERPC_DT_CPU_FTRS_H

/*
 *  Copyright 2017, IBM Corporation
 *  cpufeatures is the new way to discover CPU features with /cpus/features
 *  devicetree. This supersedes PVR based discovery ("cputable"), and older
 *  device tree feature advertisement.
 */

#include <linux/types.h>
#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>
#include <uapi/asm/cputable.h>

#ifdef CONFIG_PPC_DT_CPU_FTRS
bool dt_cpu_ftrs_init(void *fdt);
void dt_cpu_ftrs_scan(void);
bool dt_cpu_ftrs_in_use(void);
#else
static inline bool dt_cpu_ftrs_init(void *fdt) { return false; }
static inline void dt_cpu_ftrs_scan(void) { }
static inline bool dt_cpu_ftrs_in_use(void) { return false; }
#endif

#endif /* __ASM_POWERPC_DT_CPU_FTRS_H */
