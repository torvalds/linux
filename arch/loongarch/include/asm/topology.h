/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_TOPOLOGY_H
#define __ASM_TOPOLOGY_H

#include <linux/smp.h>

#define cpu_logical_map(cpu)  0

#include <asm-generic/topology.h>

static inline void arch_fix_phys_package_id(int num, u32 slot) { }
#endif /* __ASM_TOPOLOGY_H */
