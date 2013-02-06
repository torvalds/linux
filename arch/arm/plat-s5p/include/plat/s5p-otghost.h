/* linux/arch/arm/plat-s5p/include/plat/s5p-otghost.h
 *
 * Copyright 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Platform header file for Samsung OTG Host driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef _PLAT_S5P_OTGHOST_H
#define _PLAT_S5P_OTGHOST_H __FILE__

#include <linux/wakelock.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

/*#define CONFIG_USB_S3C_OTG_HOST_HANDLING_CLOCK*/
#define CONFIG_USB_S3C_OTG_HOST_DTGDRVVBUS

union port_flags_t {
	/** raw register data */
	u32 d32;
	/** register bits */
	struct {
		unsigned port_connect_status_change:1;
		unsigned port_connect_status:1;
		unsigned port_reset_change:1;
		unsigned port_enable_change:1;
		unsigned port_suspend_change:1;
		unsigned port_over_current_change:1;
		unsigned reserved:26;
	} b;
};

struct sec_otghost_data {
	bool clk_usage;

	void (*set_pwr_cb)(int on);
	void (*host_notify_cb)(int a);
	int (*start)(u32 reg);
	int (*stop)(void);

	int (*phy_init)(int mode);
	int (*phy_exit)(int mode);

	struct platform_device *pdev;
	struct clk *clk;

	int sec_whlist_table_num;
	void __iomem *regs;
};

struct sec_otghost {
	spinlock_t lock;

	bool ch_halt;
	union port_flags_t port_flag;
	struct wake_lock wake_lock;

	struct work_struct work;
	struct workqueue_struct *wq;

	struct sec_otghost_data *otg_data;
};

#endif /*_PLAT_S5P_OTGHOST_H */
