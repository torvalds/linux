/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_SAMSUNG_USB_PHY_H
#define __PLAT_SAMSUNG_USB_PHY_H __FILE__

enum s5p_usb_phy_type {
	S5P_USB_PHY_DEVICE,
	S5P_USB_PHY_HOST,
	S5P_USB_PHY_DRD,
};

extern int s5p_usb_phy_init(struct platform_device *pdev, int type);
extern int s5p_usb_phy_exit(struct platform_device *pdev, int type);
extern int s5p_usb_phy_tune(struct platform_device *pdev, int type);
extern int s5p_usb_phy_suspend(struct platform_device *pdev, int type);
extern int s5p_usb_phy_resume(struct platform_device *pdev, int type);
extern int exynos5_usb_phy_crport_ctrl(struct platform_device *pdev,
					u32 addr, u32 data);
extern int exynos_check_usb_op(void);
#endif /* __PLAT_SAMSUNG_USB_PHY_H */
