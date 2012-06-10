/*
 * arch/arm/mach-spear13xx/include/mach/generic.h
 *
 * spear13xx machine family generic header file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_GENERIC_H
#define __MACH_GENERIC_H

#include <linux/dmaengine.h>
#include <asm/mach/time.h>

/* Add spear13xx structure declarations here */
extern struct sys_timer spear13xx_timer;
extern struct pl022_ssp_controller pl022_plat_data;
extern struct dw_dma_platform_data dmac_plat_data;
extern struct dw_dma_slave cf_dma_priv;
extern struct dw_dma_slave nand_read_dma_priv;
extern struct dw_dma_slave nand_write_dma_priv;

/* Add spear13xx family function declarations here */
void __init spear_setup_of_timer(void);
void __init spear13xx_map_io(void);
void __init spear13xx_dt_init_irq(void);
void __init spear13xx_l2x0_init(void);
bool dw_dma_filter(struct dma_chan *chan, void *slave);
void spear_restart(char, const char *);
void spear13xx_secondary_startup(void);

#ifdef CONFIG_MACH_SPEAR1310
void __init spear1310_clk_init(void);
#else
static inline void spear1310_clk_init(void) {}
#endif

#ifdef CONFIG_MACH_SPEAR1340
void __init spear1340_clk_init(void);
#else
static inline void spear1340_clk_init(void) {}
#endif

#endif /* __MACH_GENERIC_H */
