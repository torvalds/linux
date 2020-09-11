/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2004-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */


#ifndef __ASM_ARCH_MXC_COMMON_H__
#define __ASM_ARCH_MXC_COMMON_H__

#include <linux/reboot.h>

struct irq_data;
struct platform_device;
struct pt_regs;
struct clk;
struct device_node;
enum mxc_cpu_pwr_mode;
struct of_device_id;

void mx31_map_io(void);
void mx35_map_io(void);
void imx21_init_early(void);
void imx31_init_early(void);
void imx35_init_early(void);
void mxc_init_irq(void __iomem *);
void mx31_init_irq(void);
void mx35_init_irq(void);
void mxc_set_cpu_type(unsigned int type);
void mxc_restart(enum reboot_mode, const char *);
void mxc_arch_reset_init(void __iomem *);
void imx1_reset_init(void __iomem *);
void imx_set_aips(void __iomem *);
void imx_aips_allow_unprivileged_access(const char *compat);
int mxc_device_init(void);
void imx_set_soc_revision(unsigned int rev);
void imx_init_revision_from_anatop(void);
void imx6_enable_rbc(bool enable);
void imx_gpc_check_dt(void);
void imx_gpc_set_arm_power_in_lpm(bool power_off);
void imx_gpc_set_l2_mem_power_in_lpm(bool power_off);
void imx_gpc_set_arm_power_up_timing(u32 sw2iso, u32 sw);
void imx_gpc_set_arm_power_down_timing(u32 sw2iso, u32 sw);
void imx25_pm_init(void);
void imx27_pm_init(void);
void imx5_pmu_init(void);

enum mxc_cpu_pwr_mode {
	WAIT_CLOCKED,		/* wfi only */
	WAIT_UNCLOCKED,		/* WAIT */
	WAIT_UNCLOCKED_POWER_OFF,	/* WAIT + SRPG */
	STOP_POWER_ON,		/* just STOP */
	STOP_POWER_OFF,		/* STOP + SRPG */
};

enum ulp_cpu_pwr_mode {
	ULP_PM_HSRUN,    /* High speed run mode */
	ULP_PM_RUN,      /* Run mode */
	ULP_PM_WAIT,     /* Wait mode */
	ULP_PM_STOP,     /* Stop mode */
	ULP_PM_VLPS,     /* Very low power stop mode */
	ULP_PM_VLLS,     /* very low leakage stop mode */
};

void imx_enable_cpu(int cpu, bool enable);
void imx_set_cpu_jump(int cpu, void *jump_addr);
u32 imx_get_cpu_arg(int cpu);
void imx_set_cpu_arg(int cpu, u32 arg);
#ifdef CONFIG_SMP
void v7_secondary_startup(void);
void imx_scu_map_io(void);
void imx_smp_prepare(void);
#else
static inline void imx_scu_map_io(void) {}
static inline void imx_smp_prepare(void) {}
#endif
void imx_src_init(void);
void imx_gpc_pre_suspend(bool arm_power_off);
void imx_gpc_post_resume(void);
void imx_gpc_mask_all(void);
void imx_gpc_restore_all(void);
void imx_gpc_hwirq_mask(unsigned int hwirq);
void imx_gpc_hwirq_unmask(unsigned int hwirq);
void imx_anatop_init(void);
void imx_anatop_pre_suspend(void);
void imx_anatop_post_resume(void);
int imx6_set_lpm(enum mxc_cpu_pwr_mode mode);
void imx6_set_int_mem_clk_lpm(bool enable);
void imx6sl_set_wait_clk(bool enter);
int imx_mmdc_get_ddr_type(void);
int imx7ulp_set_lpm(enum ulp_cpu_pwr_mode mode);

void imx_cpu_die(unsigned int cpu);
int imx_cpu_kill(unsigned int cpu);

#ifdef CONFIG_SUSPEND
void imx53_suspend(void __iomem *ocram_vbase);
extern const u32 imx53_suspend_sz;
void imx6_suspend(void __iomem *ocram_vbase);
#else
static inline void imx53_suspend(void __iomem *ocram_vbase) {}
static const u32 imx53_suspend_sz;
static inline void imx6_suspend(void __iomem *ocram_vbase) {}
#endif

void v7_cpu_resume(void);

void imx6_pm_ccm_init(const char *ccm_compat);
void imx6q_pm_init(void);
void imx6dl_pm_init(void);
void imx6sl_pm_init(void);
void imx6sx_pm_init(void);
void imx6ul_pm_init(void);
void imx7ulp_pm_init(void);

#ifdef CONFIG_PM
void imx51_pm_init(void);
void imx53_pm_init(void);
#else
static inline void imx51_pm_init(void) {}
static inline void imx53_pm_init(void) {}
#endif

#ifdef CONFIG_NEON
int mx51_neon_fixup(void);
#else
static inline int mx51_neon_fixup(void) { return 0; }
#endif

#ifdef CONFIG_CACHE_L2X0
void imx_init_l2cache(void);
#else
static inline void imx_init_l2cache(void) {}
#endif

extern const struct smp_operations imx_smp_ops;
extern const struct smp_operations ls1021a_smp_ops;

#endif
