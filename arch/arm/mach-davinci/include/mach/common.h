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

struct sys_timer;

extern struct sys_timer davinci_timer;

extern void davinci_irq_init(void);
extern void __iomem *davinci_intc_base;
extern int davinci_intc_type;

struct davinci_timer_instance {
	void __iomem	*base;
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

/* SoC specific init support */
struct davinci_soc_info {
	struct map_desc			*io_desc;
	unsigned long			io_desc_num;
	u32				cpu_id;
	u32				jtag_id;
	void __iomem			*jtag_id_base;
	struct davinci_id		*ids;
	unsigned long			ids_num;
	struct clk_lookup		*cpu_clks;
	void __iomem			**psc_bases;
	unsigned long			psc_bases_num;
	void __iomem			*pinmux_base;
	const struct mux_config		*pinmux_pins;
	unsigned long			pinmux_pins_num;
	void __iomem			*intc_base;
	int				intc_type;
	u8				*intc_irq_prios;
	unsigned long			intc_irq_num;
	struct davinci_timer_info	*timer_info;
	void __iomem			*gpio_base;
	unsigned			gpio_num;
	unsigned			gpio_irq;
	unsigned			gpio_unbanked;
	struct platform_device		*serial_dev;
	struct emac_platform_data	*emac_pdata;
	dma_addr_t			sram_dma;
	unsigned			sram_len;
};

extern struct davinci_soc_info davinci_soc_info;

extern void davinci_common_init(struct davinci_soc_info *soc_info);

/* standard place to map on-chip SRAMs; they *may* support DMA */
#define SRAM_VIRT	0xfffe0000
#define SRAM_SIZE	SZ_128K

#endif /* __ARCH_ARM_MACH_DAVINCI_COMMON_H */
