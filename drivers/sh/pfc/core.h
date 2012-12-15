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
#include <linux/sh_pfc.h>
#include <linux/types.h>

struct pfc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct sh_pfc_chip;

struct sh_pfc {
	struct sh_pfc_platform_data *pdata;
	spinlock_t lock;

	struct pfc_window *window;
	struct sh_pfc_chip *gpio;
};

int sh_pfc_register_gpiochip(struct sh_pfc *pfc);
int sh_pfc_unregister_gpiochip(struct sh_pfc *pfc);

int sh_pfc_register_pinctrl(struct sh_pfc *pfc);

int sh_pfc_read_bit(struct pinmux_data_reg *dr, unsigned long in_pos);
void sh_pfc_write_bit(struct pinmux_data_reg *dr, unsigned long in_pos,
		      unsigned long value);
int sh_pfc_get_data_reg(struct sh_pfc *pfc, unsigned gpio,
			struct pinmux_data_reg **drp, int *bitp);
int sh_pfc_gpio_to_enum(struct sh_pfc *pfc, unsigned gpio, int pos,
			pinmux_enum_t *enum_idp);
int sh_pfc_config_gpio(struct sh_pfc *pfc, unsigned gpio, int pinmux_type,
		       int cfg_mode);

#endif /* __SH_PFC_CORE_H__ */
