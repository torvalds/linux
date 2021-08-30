/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_API_H__
#define __RGA_API_H__

#include <linux/miscdevice.h>
#include <linux/wakelock.h>

#include "rga2_reg_info.h"
#include "rga2_debugger.h"
#include "rga2.h"

/* Driver information */
#define DRIVER_DESC			"RGA2 Device Driver"
#define DRIVER_NAME			"rga2"
#define DRIVER_VERSION		"2.1.0"

/* Logging */
#define RGA_DEBUG 1
#if RGA_DEBUG
#define DBG(format, args...) printk(KERN_DEBUG "%s: " format, DRIVER_NAME, ## args)
#define ERR(format, args...) printk(KERN_ERR "%s: " format, DRIVER_NAME, ## args)
#define WARNING(format, args...) printk(KERN_WARN "%s: " format, DRIVER_NAME, ## args)
#define INFO(format, args...) printk(KERN_INFO "%s: " format, DRIVER_NAME, ## args)
#else
#define DBG(format, args...)
#define ERR(format, args...)
#define WARNING(format, args...)
#define INFO(format, args...)
#endif

struct rga2_drvdata_t {
	struct miscdevice miscdev;
	struct device *dev;
	void *rga_base;
	int irq;

	struct delayed_work power_off_work;
	struct wake_lock wake_lock;
	void (*rga_irq_callback)(int rga_retval);

	struct clk *aclk_rga2;
	struct clk *hclk_rga2;
	struct clk *pd_rga2;
	struct clk *clk_rga2;

	struct ion_client *ion_client;
	char version[16];

#ifdef CONFIG_ROCKCHIP_RGA2_DEBUGGER
	struct rga_debugger *debugger;
#endif
};

#define ENABLE      1
#define DISABLE     0



#endif
