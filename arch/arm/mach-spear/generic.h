/*
 * spear machine family generic header file
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Rajeev Kumar <rajeev-dlh.kumar@st.com>
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_GENERIC_H
#define __MACH_GENERIC_H

#include <linux/dmaengine.h>
#include <linux/amba/pl08x.h>
#include <linux/init.h>
#include <asm/mach/time.h>

extern void spear13xx_timer_init(void);
extern void spear3xx_timer_init(void);
extern struct pl022_ssp_controller pl022_plat_data;
extern struct pl08x_platform_data pl080_plat_data;
extern struct dw_dma_platform_data dmac_plat_data;
extern struct dw_dma_slave cf_dma_priv;
extern struct dw_dma_slave nand_read_dma_priv;
extern struct dw_dma_slave nand_write_dma_priv;
bool dw_dma_filter(struct dma_chan *chan, void *slave);

void __init spear_setup_of_timer(void);
void __init spear3xx_clk_init(void __iomem *misc_base,
			      void __iomem *soc_config_base);
void __init spear3xx_map_io(void);
void __init spear3xx_dt_init_irq(void);
void __init spear6xx_clk_init(void __iomem *misc_base);
void __init spear13xx_map_io(void);
void __init spear13xx_l2x0_init(void);

void spear_restart(char, const char *);

void spear13xx_secondary_startup(void);
void __cpuinit spear13xx_cpu_die(unsigned int cpu);

extern struct smp_operations spear13xx_smp_ops;

#ifdef CONFIG_MACH_SPEAR1310
void __init spear1310_clk_init(void __iomem *misc_base, void __iomem *ras_base);
#else
static inline void spear1310_clk_init(void __iomem *misc_base, void __iomem *ras_base) {}
#endif

#ifdef CONFIG_MACH_SPEAR1340
void __init spear1340_clk_init(void __iomem *misc_base);
#else
static inline void spear1340_clk_init(void __iomem *misc_base) {}
#endif

#endif /* __MACH_GENERIC_H */
