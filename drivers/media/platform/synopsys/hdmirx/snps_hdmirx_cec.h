/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#ifndef DW_HDMI_RX_CEC_H
#define DW_HDMI_RX_CEC_H

struct snps_hdmirx_dev;

struct hdmirx_cec_ops {
	void (*write)(struct snps_hdmirx_dev *hdmirx_dev, int reg, u32 val);
	u32 (*read)(struct snps_hdmirx_dev *hdmirx_dev, int reg);
	void (*enable)(struct snps_hdmirx_dev *hdmirx);
	void (*disable)(struct snps_hdmirx_dev *hdmirx);
};

struct hdmirx_cec_data {
	struct snps_hdmirx_dev *hdmirx;
	const struct hdmirx_cec_ops *ops;
	struct device *dev;
	int irq;
};

struct hdmirx_cec {
	struct snps_hdmirx_dev *hdmirx;
	struct device *dev;
	const struct hdmirx_cec_ops *ops;
	u32 addresses;
	struct cec_adapter *adap;
	struct cec_msg rx_msg;
	unsigned int tx_status;
	bool tx_done;
	bool rx_done;
	int irq;
};

struct hdmirx_cec *snps_hdmirx_cec_register(struct hdmirx_cec_data *data);
void snps_hdmirx_cec_unregister(struct hdmirx_cec *cec);

#endif /* DW_HDMI_RX_CEC_H */
