/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author:
 *      Algea Cao <algea.cao@rock-chips.com>
 */
#ifndef DW_HDMI_QP_HDCP_H
#define DW_HDMI_QP_HDCP_H

#include <linux/miscdevice.h>

#define DW_HDCP_QP_DRIVER_NAME "dw-hdmi-qp-hdcp"
#define PRIVATE_KEY_SIZE	280
#define KEY_SHA_SIZE		20

#define KSV_LEN			5
#define BSTATUS_LEN		2
#define M0_LEN			8
#define SHAMAX			20

struct dw_hdmi_qp_hdcp_keys {
	u8 KSV[8];
	u8 devicekey[PRIVATE_KEY_SIZE];
	u8 sha1[KEY_SHA_SIZE];
};

struct dw_qp_hdcp {
	int retry_times;
	int remaining_times;
	char *seeds;
	int invalidkey;
	char *invalidkeys;
	int hdcp2_enable;
	int status;
	u32 reg_io_width;

	struct dw_hdmi_qp_hdcp_keys *keys;
	struct device *dev;
	struct dw_hdmi_qp *hdmi;
	void __iomem *regs;

	struct mutex mutex;

	struct work_struct work;
	struct workqueue_struct *workqueue;

	void (*write)(struct dw_hdmi_qp *hdmi, u32 val, int offset);
	u32 (*read)(struct dw_hdmi_qp *hdmi, int offset);
	void (*get_mem)(struct dw_hdmi_qp *hdmi, u8 *data, u32 len);
	int (*hdcp_start)(struct dw_qp_hdcp *hdcp);
	int (*hdcp_stop)(struct dw_qp_hdcp *hdcp);
	void (*hdcp_isr)(struct dw_qp_hdcp *hdcp, u32 avp_int, u32 hdcp_status);
};

#endif
