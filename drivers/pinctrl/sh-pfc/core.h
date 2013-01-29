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
#include <linux/types.h>

#include "sh_pfc.h"

struct sh_pfc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct sh_pfc_chip;
struct sh_pfc_pinctrl;

struct sh_pfc {
	struct device *dev;
	struct sh_pfc_soc_info *info;
	spinlock_t lock;

	unsigned int num_windows;
	struct sh_pfc_window *window;

	struct sh_pfc_chip *gpio;
	struct sh_pfc_pinctrl *pinctrl;
};

int sh_pfc_register_gpiochip(struct sh_pfc *pfc);
int sh_pfc_unregister_gpiochip(struct sh_pfc *pfc);

int sh_pfc_register_pinctrl(struct sh_pfc *pfc);
int sh_pfc_unregister_pinctrl(struct sh_pfc *pfc);

int sh_pfc_read_bit(struct pinmux_data_reg *dr, unsigned long in_pos);
void sh_pfc_write_bit(struct pinmux_data_reg *dr, unsigned long in_pos,
		      unsigned long value);
int sh_pfc_get_data_reg(struct sh_pfc *pfc, unsigned gpio,
			struct pinmux_data_reg **drp, int *bitp);
int sh_pfc_gpio_to_enum(struct sh_pfc *pfc, unsigned gpio, int pos,
			pinmux_enum_t *enum_idp);
int sh_pfc_config_gpio(struct sh_pfc *pfc, unsigned gpio, int pinmux_type,
		       int cfg_mode);

extern struct sh_pfc_soc_info r8a7740_pinmux_info;
extern struct sh_pfc_soc_info r8a7779_pinmux_info;
extern struct sh_pfc_soc_info sh7203_pinmux_info;
extern struct sh_pfc_soc_info sh7264_pinmux_info;
extern struct sh_pfc_soc_info sh7269_pinmux_info;
extern struct sh_pfc_soc_info sh7372_pinmux_info;
extern struct sh_pfc_soc_info sh73a0_pinmux_info;
extern struct sh_pfc_soc_info sh7720_pinmux_info;
extern struct sh_pfc_soc_info sh7722_pinmux_info;
extern struct sh_pfc_soc_info sh7723_pinmux_info;
extern struct sh_pfc_soc_info sh7724_pinmux_info;
extern struct sh_pfc_soc_info sh7734_pinmux_info;
extern struct sh_pfc_soc_info sh7757_pinmux_info;
extern struct sh_pfc_soc_info sh7785_pinmux_info;
extern struct sh_pfc_soc_info sh7786_pinmux_info;
extern struct sh_pfc_soc_info shx3_pinmux_info;

#endif /* __SH_PFC_CORE_H__ */
