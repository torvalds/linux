/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#ifndef __RK_HDMIRX_CEC_H__
#define __RK_HDMIRX_CEC_H__

struct rk_hdmirx_dev;

struct hdmirx_cec_ops {
	void (*write)(struct rk_hdmirx_dev *hdmirx_dev, int reg, u32 val);
	u32 (*read)(struct rk_hdmirx_dev *hdmirx_dev, int reg);
	void (*enable)(struct rk_hdmirx_dev *hdmirx);
	void (*disable)(struct rk_hdmirx_dev *hdmirx);
};

struct hdmirx_cec_data {
	struct rk_hdmirx_dev *hdmirx;
	const struct hdmirx_cec_ops *ops;
	struct device *dev;
	int irq;
	u8 *edid;
};

struct hdmirx_cec {
	struct rk_hdmirx_dev *hdmirx;
	struct device *dev;
	const struct hdmirx_cec_ops *ops;
	u32 addresses;
	struct cec_adapter *adap;
	struct cec_msg rx_msg;
	unsigned int tx_status;
	bool tx_done;
	bool rx_done;
	struct cec_notifier *notify;
	int irq;
	struct edid *edid;
};

struct hdmirx_cec *rk_hdmirx_cec_register(struct hdmirx_cec_data *data);
void rk_hdmirx_cec_unregister(struct hdmirx_cec *cec);

#endif /* __DW_HDMI_RX_CEC_H__ */
