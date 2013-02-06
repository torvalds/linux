/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#ifndef __PLAT_SAMSUNG_HDMI_H
#define __PLAT_SAMSUNG_HDMI_H

/**
 * Platform device data for Samsung hdmi
 *
 * @is_v13: use hdmi version 1.3
 * @cfg_hpd: configure the hpd pin, if enable, set to hpd special function 3,
 *	     else set to external interrupt.
 * @get_hpd: get level value of hpd pin
 */
struct s5p_hdmi_platdata {
	bool is_v13;
	void (*cfg_hpd)(bool enable);
	int (*get_hpd)(void);
};

extern void s5p_hdmi_set_platdata(struct s5p_hdmi_platdata *pd);
#ifdef CONFIG_EXYNOS4_SETUP_HDMI
extern void s5p_hdmi_cfg_hpd(bool enable);
extern int s5p_hdmi_get_hpd(void);
#else
static inline void s5p_hdmi_cfg_hpd(bool enable) { }
static inline int s5p_hdmi_get_hpd(void) { return 0; }
#endif

#endif /* __PLAT_SAMSUNG_HDMI_H */
