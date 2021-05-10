/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/prom.h
 *
 *  Copyright (C) 2009 Canonical Ltd. <jeremy.kerr@canonical.com>
 */
#ifndef __ASMARM_PROM_H
#define __ASMARM_PROM_H

#ifdef CONFIG_OF

extern const struct machine_desc *setup_machine_fdt(void *dt_virt);
extern void __init arm_dt_init_cpu_maps(void);

#else /* CONFIG_OF */

static inline const struct machine_desc *setup_machine_fdt(void *dt_virt)
{
	return NULL;
}

static inline void arm_dt_init_cpu_maps(void) { }

#endif /* CONFIG_OF */
#endif /* ASMARM_PROM_H */
