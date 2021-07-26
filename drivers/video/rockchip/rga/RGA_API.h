/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_API_H__
#define __RGA_API_H__

#include <linux/miscdevice.h>
#include <linux/wakelock.h>

#include "rga_reg_info.h"
#include "rga.h"

#define ENABLE      1
#define DISABLE     0

struct rga_drvdata {
	struct miscdevice miscdev;
	struct device *dev;
	void *rga_base;
	int irq;

	struct delayed_work power_off_work;
	void (*rga_irq_callback)(int rga_retval);   //callback function used by aync call
	struct wake_lock wake_lock;

	struct clk *pd_rga;
	struct clk *aclk_rga;
	struct clk *hclk_rga;

	//#if defined(CONFIG_ION_ROCKCHIP)
	struct ion_client *ion_client;
	//#endif
	char *version;
};

int32_t RGA_gen_two_pro(struct rga_req *msg, struct rga_req *msg1);





#endif
