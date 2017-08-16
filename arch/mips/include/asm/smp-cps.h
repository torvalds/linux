/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_SMP_CPS_H__
#define __MIPS_ASM_SMP_CPS_H__

#ifndef __ASSEMBLY__

struct vpe_boot_config {
	unsigned long pc;
	unsigned long sp;
	unsigned long gp;
};

struct core_boot_config {
	atomic_t vpe_mask;
	struct vpe_boot_config *vpe_config;
};

extern struct core_boot_config *mips_cps_core_bootcfg;

extern void mips_cps_core_entry(void);
extern void mips_cps_core_init(void);

extern void mips_cps_boot_vpes(struct core_boot_config *cfg, unsigned vpe);

extern void mips_cps_pm_save(void);
extern void mips_cps_pm_restore(void);

#ifdef CONFIG_MIPS_CPS

extern bool mips_cps_smp_in_use(void);

#else /* !CONFIG_MIPS_CPS */

static inline bool mips_cps_smp_in_use(void) { return false; }

#endif /* !CONFIG_MIPS_CPS */

#else /* __ASSEMBLY__ */

.extern mips_cps_bootcfg;

#endif /* __ASSEMBLY__ */
#endif /* __MIPS_ASM_SMP_CPS_H__ */
