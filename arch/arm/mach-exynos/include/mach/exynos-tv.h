/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Platform Header file for Samsung TV driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __EXYNOS_TV_H
#define __EXYNOS_TV_H __FILE__

struct platform_device;

struct s5p_platform_hpd {
	void	(*int_src_hdmi_hpd)(struct platform_device *pdev);
	void	(*int_src_ext_hpd)(struct platform_device *pdev);
	int	(*read_gpio)(struct platform_device *pdev);
};

extern void s5p_hdmi_hpd_set_platdata(struct s5p_platform_hpd *pd);

extern void s5p_tv_setup(void);

#endif /* __EXYNOS_TV_H */
