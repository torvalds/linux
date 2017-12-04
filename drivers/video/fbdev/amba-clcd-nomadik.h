/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AMBA_CLCD_NOMADIK_H
#define _AMBA_CLCD_NOMADIK_H

#include <linux/amba/bus.h>

#ifdef CONFIG_ARCH_NOMADIK
int nomadik_clcd_init_board(struct amba_device *adev,
			     struct clcd_board *board);
int nomadik_clcd_init_panel(struct clcd_fb *fb, struct device_node *panel);
#else
static inline int nomadik_clcd_init_board(struct amba_device *adev,
					  struct clcd_board *board)
{
	return 0;
}
static inline int nomadik_clcd_init_panel(struct clcd_fb *fb,
					  struct device_node *panel)
{
	return 0;
}
#endif

#endif /* inclusion guard */
