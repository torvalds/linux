/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for code common to all DaVinci machines.
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc.
 */

#ifndef __ARCH_ARM_MACH_DAVINCI_COMMON_H
#define __ARCH_ARM_MACH_DAVINCI_COMMON_H

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/reboot.h>

#include <asm/irq.h>

#define DAVINCI_INTC_START             NR_IRQS
#define DAVINCI_INTC_IRQ(_irqnum)      (DAVINCI_INTC_START + (_irqnum))

struct davinci_gpio_controller;

/*
 * SoC info passed into common davinci modules.
 *
 * Base addresses in this structure should be physical and not virtual.
 * Modules that take such base addresses, should internally ioremap() them to
 * use.
 */
struct davinci_soc_info {
	struct map_desc			*io_desc;
	unsigned long			io_desc_num;
	u32				cpu_id;
	u32				jtag_id;
	u32				jtag_id_reg;
	struct davinci_id		*ids;
	unsigned long			ids_num;
	u32				pinmux_base;
	const struct mux_config		*pinmux_pins;
	unsigned long			pinmux_pins_num;
	int				gpio_type;
	u32				gpio_base;
	unsigned			gpio_num;
	unsigned			gpio_irq;
	unsigned			gpio_unbanked;
	dma_addr_t			sram_dma;
	unsigned			sram_len;
};

extern struct davinci_soc_info davinci_soc_info;

extern void davinci_common_init(const struct davinci_soc_info *soc_info);
extern void davinci_init_ide(void);
void davinci_init_late(void);

#ifdef CONFIG_CPU_FREQ
int davinci_cpufreq_init(void);
#else
static inline int davinci_cpufreq_init(void) { return 0; }
#endif

#ifdef CONFIG_SUSPEND
int davinci_pm_init(void);
#else
static inline int davinci_pm_init(void) { return 0; }
#endif

void __init pdata_quirks_init(void);

#define SRAM_SIZE	SZ_128K

#endif /* __ARCH_ARM_MACH_DAVINCI_COMMON_H */
