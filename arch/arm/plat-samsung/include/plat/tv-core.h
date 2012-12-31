/*
 * arch/arm/plat-samsung/include/plat/tv.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *	Tomasz Stanislawski <t.stanislaws@samsung.com>
 *
 * Samsung TV driver core functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SAMSUNG_PLAT_TV_H
#define __SAMSUNG_PLAT_TV_H __FILE__

/*
 * These functions are only for use with the core support code, such as
 * the CPU-specific initialization code.
 */

/* Re-define device name to differentiate the subsystem in various SoCs. */
static inline void s5p_hdmi_setname(char *name)
{
#ifdef CONFIG_S5P_DEV_TV
	s5p_device_hdmi.name = name;
#endif
}

static inline void s5p_mixer_setname(char *name)
{
#ifdef CONFIG_S5P_DEV_TV
	s5p_device_mixer.name = name;
#endif
}

#endif /* __SAMSUNG_PLAT_TV_H */
