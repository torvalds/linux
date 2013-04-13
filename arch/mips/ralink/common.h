/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#ifndef _RALINK_COMMON_H__
#define _RALINK_COMMON_H__

#define RAMIPS_SYS_TYPE_LEN	32

struct ralink_pinmux_grp {
	const char *name;
	u32 mask;
	int gpio_first;
	int gpio_last;
};

struct ralink_pinmux {
	struct ralink_pinmux_grp *mode;
	struct ralink_pinmux_grp *uart;
	int uart_shift;
	u32 uart_mask;
	void (*wdt_reset)(void);
	struct ralink_pinmux_grp *pci;
	int pci_shift;
	u32 pci_mask;
};
extern struct ralink_pinmux rt_gpio_pinmux;

struct ralink_soc_info {
	unsigned char sys_type[RAMIPS_SYS_TYPE_LEN];
	unsigned char *compatible;

	unsigned long mem_base;
	unsigned long mem_size;
	unsigned long mem_size_min;
	unsigned long mem_size_max;
};
extern struct ralink_soc_info soc_info;

extern void ralink_of_remap(void);

extern void ralink_clk_init(void);
extern void ralink_clk_add(const char *dev, unsigned long rate);

extern void prom_soc_init(struct ralink_soc_info *soc_info);

__iomem void *plat_of_remap_node(const char *node);

#endif /* _RALINK_COMMON_H__ */
