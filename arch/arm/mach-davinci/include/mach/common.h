/*
 * Header for code common to all DaVinci machines.
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ARCH_ARM_MACH_DAVINCI_COMMON_H
#define __ARCH_ARM_MACH_DAVINCI_COMMON_H

#include <linux/compiler.h>
#include <linux/types.h>

struct sys_timer;

extern struct sys_timer davinci_timer;

extern void davinci_irq_init(void);
extern void __iomem *davinci_intc_base;
extern int davinci_intc_type;

struct davinci_timer_instance {
	u32		base;
	u32		bottom_irq;
	u32		top_irq;
	unsigned long	cmp_off;
	unsigned int	cmp_irq;
};

struct davinci_timer_info {
	struct davinci_timer_instance	*timers;
	unsigned int			clockevent_id;
	unsigned int			clocksource_id;
};

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
	struct clk_lookup		*cpu_clks;
	u32				*psc_bases;
	unsigned long			psc_bases_num;
	u32				pinmux_base;
	const struct mux_config		*pinmux_pins;
	unsigned long			pinmux_pins_num;
	u32				intc_base;
	int				intc_type;
	u8				*intc_irq_prios;
	unsigned long			intc_irq_num;
	u32				*intc_host_map;
	struct davinci_timer_info	*timer_info;
	int				gpio_type;
	u32				gpio_base;
	unsigned			gpio_num;
	unsigned			gpio_irq;
	unsigned			gpio_unbanked;
	struct davinci_gpio_controller	*gpio_ctlrs;
	int				gpio_ctlrs_num;
	struct platform_device		*serial_dev;
	struct emac_platform_data	*emac_pdata;
	dma_addr_t			sram_dma;
	unsigned			sram_len;
	struct platform_device		*reset_device;
	void				(*reset)(struct platform_device *);
};

extern struct davinci_soc_info davinci_soc_info;

extern void davinci_common_init(struct davinci_soc_info *soc_info);
extern void davinci_init_ide(void);

/* standard place to map on-chip SRAMs; they *may* support DMA */
#define SRAM_VIRT	0xfffe0000
#define SRAM_SIZE	SZ_128K

#endif /* __ARCH_ARM_MACH_DAVINCI_COMMON_H */
