/*
 * SuperH Pin Function Controller support.
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __SH_PFC_CORE_H__
#define __SH_PFC_CORE_H__

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "sh_pfc.h"

struct sh_pfc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct sh_pfc_chip;
struct sh_pfc_pinctrl;

struct sh_pfc_pin_range {
	u16 start;
	u16 end;
};

struct sh_pfc {
	struct device *dev;
	const struct sh_pfc_soc_info *info;
	spinlock_t lock;

	unsigned int num_windows;
	struct sh_pfc_window *windows;
	unsigned int num_irqs;
	unsigned int *irqs;

	struct sh_pfc_pin_range *ranges;
	unsigned int nr_ranges;

	unsigned int nr_gpio_pins;

	struct sh_pfc_chip *gpio;
#ifdef CONFIG_SUPERH
	struct sh_pfc_chip *func;
#endif

};

int sh_pfc_register_gpiochip(struct sh_pfc *pfc);
int sh_pfc_unregister_gpiochip(struct sh_pfc *pfc);

int sh_pfc_register_pinctrl(struct sh_pfc *pfc);

u32 sh_pfc_read_raw_reg(void __iomem *mapped_reg, unsigned int reg_width);
void sh_pfc_write_raw_reg(void __iomem *mapped_reg, unsigned int reg_width,
			  u32 data);
u32 sh_pfc_read_reg(struct sh_pfc *pfc, u32 reg, unsigned int width);
void sh_pfc_write_reg(struct sh_pfc *pfc, u32 reg, unsigned int width,
		      u32 data);

int sh_pfc_get_pin_index(struct sh_pfc *pfc, unsigned int pin);
int sh_pfc_config_mux(struct sh_pfc *pfc, unsigned mark, int pinmux_type);

#endif /* __SH_PFC_CORE_H__ */
