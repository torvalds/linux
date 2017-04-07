/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author Huicong Xu <xhc@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DW_HDMI_HDCP_H
#define DW_HDMI_HDCP_H

#include <linux/miscdevice.h>

#define DW_HDCP_DRIVER_NAME "dw-hdmi-hdcp"
#define HDCP_PRIVATE_KEY_SIZE   280
#define HDCP_KEY_SHA_SIZE       20

struct hdcp_keys {
	u8 KSV[8];
	u8 devicekey[HDCP_PRIVATE_KEY_SIZE];
	u8 sha1[HDCP_KEY_SHA_SIZE];
};

struct dw_hdcp {
	bool enable;
	int retry_times;
	int remaining_times;
	char *seeds;
	int invalidkey;
	char *invalidkeys;
	int hdcp2_enable;
	int status;
	u32 reg_io_width;

	struct miscdevice mdev;
	struct hdcp_keys *keys;
	struct device *dev;
	struct dw_hdmi *hdmi;
	void __iomem *regs;

	void (*write)(struct dw_hdmi *hdmi, u8 val, int offset);
	u8 (*read)(struct dw_hdmi *hdmi, int offset);
	int (*hdcp_start)(struct dw_hdcp *hdcp);
	int (*hdcp_stop)(struct dw_hdcp *hdcp);
	void (*hdcp_isr)(struct dw_hdcp *hdcp, int hdcp_int);
};

#endif
