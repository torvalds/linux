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

/* parameters describe VBUS sourcing for host mode */
extern void setup_usb(unsigned mA, unsigned potpgt_msec);

/* parameters describe VBUS sourcing for host mode */
extern void setup_usb(unsigned mA, unsigned potpgt_msec);

/* SoC specific init support */
struct davinci_soc_info {
	struct map_desc			*io_desc;
	unsigned long			io_desc_num;
	u32				cpu_id;
	u32				jtag_id;
	void __iomem			*jtag_id_base;
	struct davinci_id		*ids;
	unsigned long			ids_num;
	struct davinci_clk		*cpu_clks;
};

extern struct davinci_soc_info davinci_soc_info;

extern void davinci_common_init(struct davinci_soc_info *soc_info);

#endif /* __ARCH_ARM_MACH_DAVINCI_COMMON_H */
