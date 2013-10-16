/*
 * Copyright 2004-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_COMMON_H__
#define __ASM_ARCH_MXC_COMMON_H__

#include <linux/reboot.h>

struct irq_data;
struct platform_device;
struct pt_regs;
struct clk;
enum mxc_cpu_pwr_mode;

void mx1_map_io(void);
void mx21_map_io(void);
void mx25_map_io(void);
void mx27_map_io(void);
void mx31_map_io(void);
void mx35_map_io(void);
void mx51_map_io(void);
void mx53_map_io(void);
void imx1_init_early(void);
void imx21_init_early(void);
void imx25_init_early(void);
void imx27_init_early(void);
void imx31_init_early(void);
void imx35_init_early(void);
void imx51_init_early(void);
void imx53_init_early(void);
void mxc_init_irq(void __iomem *);
void tzic_init_irq(void __iomem *);
void mx1_init_irq(void);
void mx21_init_irq(void);
void mx25_init_irq(void);
void mx27_init_irq(void);
void mx31_init_irq(void);
void mx35_init_irq(void);
void mx51_init_irq(void);
void mx53_init_irq(void);
void imx1_soc_init(void);
void imx21_soc_init(void);
void imx25_soc_init(void);
void imx27_soc_init(void);
void imx31_soc_init(void);
void imx35_soc_init(void);
void imx51_soc_init(void);
void imx51_init_late(void);
void imx53_init_late(void);
void epit_timer_init(void __iomem *base, int irq);
void mxc_timer_init(void __iomem *, int);
int mx1_clocks_init(unsigned long fref);
int mx21_clocks_init(unsigned long lref, unsigned long fref);
int mx25_clocks_init(void);
int mx27_clocks_init(unsigned long fref);
int mx31_clocks_init(unsigned long fref);
int mx35_clocks_init(void);
int mx51_clocks_init(unsigned long ckil, unsigned long osc,
			unsigned long ckih1, unsigned long ckih2);
int mx25_clocks_init_dt(void);
int mx27_clocks_init_dt(void);
int mx31_clocks_init_dt(void);
struct platform_device *mxc_register_gpio(char *name, int id,
	resource_size_t iobase, resource_size_t iosize, int irq, int irq_high);
void mxc_set_cpu_type(unsigned int type);
void mxc_restart(enum reboot_mode, const char *);
void mxc_arch_reset_init(void __iomem *);
void mxc_arch_reset_init_dt(void);
int mx53_revision(void);
void imx_set_aips(void __iomem *);
int mxc_device_init(void);
void imx_set_soc_revision(unsigned int rev);
unsigned int imx_get_soc_revision(void);
void imx_init_revision_from_anatop(void);
struct device *imx_soc_device_init(void);

enum mxc_cpu_pwr_mode {
	WAIT_CLOCKED,		/* wfi only */
	WAIT_UNCLOCKED,		/* WAIT */
	WAIT_UNCLOCKED_POWER_OFF,	/* WAIT + SRPG */
	STOP_POWER_ON,		/* just STOP */
	STOP_POWER_OFF,		/* STOP + SRPG */
};

enum mx3_cpu_pwr_mode {
	MX3_RUN,
	MX3_WAIT,
	MX3_DOZE,
	MX3_SLEEP,
};

void mx3_cpu_lp_set(enum mx3_cpu_pwr_mode mode);
void imx_print_silicon_rev(const char *cpu, int srev);

void avic_handle_irq(struct pt_regs *);
void tzic_handle_irq(struct pt_regs *);

#define imx1_handle_irq avic_handle_irq
#define imx21_handle_irq avic_handle_irq
#define imx25_handle_irq avic_handle_irq
#define imx27_handle_irq avic_handle_irq
#define imx31_handle_irq avic_handle_irq
#define imx35_handle_irq avic_handle_irq
#define imx51_handle_irq tzic_handle_irq
#define imx53_handle_irq tzic_handle_irq

void imx_enable_cpu(int cpu, bool enable);
void imx_set_cpu_jump(int cpu, void *jump_addr);
u32 imx_get_cpu_arg(int cpu);
void imx_set_cpu_arg(int cpu, u32 arg);
void v7_cpu_resume(void);
#ifdef CONFIG_SMP
void v7_secondary_startup(void);
void imx_scu_map_io(void);
void imx_smp_prepare(void);
void imx_scu_standby_enable(void);
#else
static inline void imx_scu_map_io(void) {}
static inline void imx_smp_prepare(void) {}
static inline void imx_scu_standby_enable(void) {}
#endif
void imx_src_init(void);
#ifdef CONFIG_HAVE_IMX_SRC
void imx_src_prepare_restart(void);
#else
static inline void imx_src_prepare_restart(void) {}
#endif
void imx_gpc_init(void);
void imx_gpc_pre_suspend(void);
void imx_gpc_post_resume(void);
void imx_gpc_mask_all(void);
void imx_gpc_restore_all(void);
void imx_gpc_irq_mask(struct irq_data *d);
void imx_gpc_irq_unmask(struct irq_data *d);
void imx_anatop_init(void);
void imx_anatop_pre_suspend(void);
void imx_anatop_post_resume(void);
int imx6q_set_lpm(enum mxc_cpu_pwr_mode mode);
void imx6q_set_chicken_bit(void);

void imx_cpu_die(unsigned int cpu);
int imx_cpu_kill(unsigned int cpu);

#ifdef CONFIG_PM
void imx6q_pm_init(void);
void imx6q_pm_set_ccm_base(void __iomem *base);
void imx5_pm_init(void);
#else
static inline void imx6q_pm_init(void) {}
static inline void imx6q_pm_set_ccm_base(void __iomem *base) {}
static inline void imx5_pm_init(void) {}
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

extern struct smp_operations imx_smp_ops;

#endif
