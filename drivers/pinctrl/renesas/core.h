/* SPDX-License-Identifier: GPL-2.0
 *
 * SuperH Pin Function Controller support.
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 */
#ifndef __SH_PFC_CORE_H__
#define __SH_PFC_CORE_H__

#include <linux/types.h>

#include "sh_pfc.h"

struct sh_pfc_pin_range {
	u16 start;
	u16 end;
};

int sh_pfc_register_gpiochip(struct sh_pfc *pfc);

int sh_pfc_register_pinctrl(struct sh_pfc *pfc);

u32 sh_pfc_read_raw_reg(void __iomem *mapped_reg, unsigned int reg_width);
void sh_pfc_write_raw_reg(void __iomem *mapped_reg, unsigned int reg_width,
			  u32 data);
u32 sh_pfc_read(struct sh_pfc *pfc, u32 reg);
void sh_pfc_write(struct sh_pfc *pfc, u32 reg, u32 data);

int sh_pfc_get_pin_index(struct sh_pfc *pfc, unsigned int pin);
int sh_pfc_config_mux(struct sh_pfc *pfc, unsigned mark, int pinmux_type);

#endif /* __SH_PFC_CORE_H__ */
