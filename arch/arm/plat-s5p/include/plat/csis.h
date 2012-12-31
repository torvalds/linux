/* linux/arch/arm/plat-s5p/include/plat/csis.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *	 http://www.samsung.com/
 *
 * Platform header file for MIPI-CSI2 driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_PLAT_CSIS_H
#define __ASM_PLAT_CSIS_H __FILE__

#define to_csis_plat(d)		(to_platform_device(d)->dev.platform_data)

struct platform_device;
struct clk;

struct s3c_platform_csis {
	char	*srclk_name;
	char	*clk_name;
	unsigned long	clk_rate;

	void		(*cfg_gpio)(void);
	void		(*cfg_phy_global)(int on);
	int		(*clk_on)(struct platform_device *pdev, struct clk **clk);
	int		(*clk_off)(struct platform_device *pdev, struct clk **clk);
};
#ifdef CONFIG_ARCH_EXYNOS4
extern void s3c_csis0_set_platdata(struct s3c_platform_csis *csis);
extern void s3c_csis1_set_platdata(struct s3c_platform_csis *csis);
extern void s3c_csis0_cfg_gpio(void);
extern void s3c_csis1_cfg_gpio(void);
extern void s3c_csis0_cfg_phy_global(int on);
extern void s3c_csis1_cfg_phy_global(int on);
#else
extern void s3c_csis_set_platdata(struct s3c_platform_csis *csis);
extern void s3c_csis_cfg_gpio(void);
extern void s3c_csis_cfg_phy_global(int on);
#endif
extern int s3c_csis_clk_on(struct platform_device *pdev, struct clk **clk);
extern int s3c_csis_clk_off(struct platform_device *pdev, struct clk **clk);
#endif /* __ASM_PLAT_CSIS_H */
