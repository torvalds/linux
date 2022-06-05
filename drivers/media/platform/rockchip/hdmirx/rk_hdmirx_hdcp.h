/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#ifndef __RK_HDMIRX_HDCP_H__
#define __RK_HDMIRX_HDCP_H__

#include <linux/miscdevice.h>

#define HDCP_KEY_SIZE		308
#define HDCP_KEY_SEED_SIZE	2

#define KSV_LEN			5
#define HEADER			10
#define SHAMAX			20

#define PRIVATE_KEY_SIZE	280
#define KEY_SHA_SIZE		20
#define KEY_DATA_SIZE		314
#define VENDOR_DATA_SIZE	(KEY_DATA_SIZE + 16)

#define HDMIRX_HDCP1X_ID	13

#define HDCP_SIG_MAGIC		0x4B534541	/* "AESK" */
#define HDCP_FLG_AES		1

enum hdmirx_hdcp_enable {
	HDCP_1X_ENABLE = 0x1,
	HDCP_2X_ENABLE = 0x2,
};

struct hdcp_key_data_t {
	unsigned int signature;
	unsigned int length;
	unsigned int crc;
	unsigned int flags;
	unsigned char data[0];
};

struct rk_hdmirx_hdcp {
	u8 enable;
	u8 hdcp_support;
	int hdcp2_enable;
	int status;

	struct miscdevice mdev;
	bool keys_is_load;
	bool aes_encrypt;
	struct device *dev;
	struct rk_hdmirx_dev *hdmirx;

	void (*write)(struct rk_hdmirx_dev *hdmirx, int reg, u32 val);
	u32 (*read)(struct rk_hdmirx_dev *hdmirx, int reg);
	void (*hpd_config)(struct rk_hdmirx_dev *hdmirx, bool en);
	bool (*tx_5v_power)(struct rk_hdmirx_dev *hdmirx);
	int (*hdcp_start)(struct rk_hdmirx_hdcp *hdcp);
	int (*hdcp_stop)(struct rk_hdmirx_hdcp *hdcp);
	void (*hdcp2_connect_ctrl)(struct rk_hdmirx_hdcp *hdcp, bool en);
};

struct rk_hdmirx_hdcp *rk_hdmirx_hdcp_register(struct rk_hdmirx_hdcp *hdcp);
void rk_hdmirx_hdcp_unregister(struct rk_hdmirx_hdcp *hdcp);

#endif /* __RK_HDMIRX_HDCP_H__ */
