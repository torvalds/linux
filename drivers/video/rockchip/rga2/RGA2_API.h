/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_API_H__
#define __RGA_API_H__

#include <linux/miscdevice.h>
#include <linux/wakelock.h>

#include "rga2_reg_info.h"
#include "rga2.h"

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
};

#define ENABLE      1
#define DISABLE     0



#endif
