/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#ifndef __MIPS_ASM_SMP_CPS_H__
#define __MIPS_ASM_SMP_CPS_H__

#define CPS_ENTRY_PATCH_INSNS	6

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

extern void mips_cps_core_boot(int cca, void __iomem *gcr_base);
extern void mips_cps_core_init(void);

extern void mips_cps_boot_vpes(struct core_boot_config *cfg, unsigned vpe);

extern void mips_cps_pm_save(void);
extern void mips_cps_pm_restore(void);

extern void excep_tlbfill(void);
extern void excep_xtlbfill(void);
extern void excep_cache(void);
extern void excep_genex(void);
extern void excep_intex(void);
extern void excep_ejtag(void);

#ifdef CONFIG_MIPS_CPS

extern bool mips_cps_smp_in_use(void);

#else /* !CONFIG_MIPS_CPS */

static inline bool mips_cps_smp_in_use(void) { return false; }

#endif /* !CONFIG_MIPS_CPS */

#else /* __ASSEMBLY__ */

.extern mips_cps_bootcfg;

#endif /* __ASSEMBLY__ */
#endif /* __MIPS_ASM_SMP_CPS_H__ */
