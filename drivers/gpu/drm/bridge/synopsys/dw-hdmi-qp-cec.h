/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author:
 *      Algea Cao <algea.cao@rock-chips.com>
 */
#ifndef DW_HDMI_QP_CEC_H
#define DW_HDMI_QP_CEC_H

struct dw_hdmi_qp;

struct dw_hdmi_qp_cec_ops {
	void (*enable)(struct dw_hdmi_qp *hdmi);
	void (*disable)(struct dw_hdmi_qp *hdmi);
	void (*write)(struct dw_hdmi_qp *hdmi, u32 val, int offset);
	u32 (*read)(struct dw_hdmi_qp *hdmi, int offset);
};

struct dw_hdmi_qp_cec_data {
	struct dw_hdmi_qp *hdmi;
	const struct dw_hdmi_qp_cec_ops *ops;
	int irq;
};

#endif
