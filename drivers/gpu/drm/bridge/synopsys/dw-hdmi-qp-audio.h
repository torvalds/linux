/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Sugar Zhang <sugar.zhang@rock-chips.com>
 */

#ifndef DW_HDMI_QP_AUDIO_H
#define DW_HDMI_QP_AUDIO_H

struct dw_hdmi_qp;

struct dw_hdmi_qp_audio_data {
	phys_addr_t phys;
	void __iomem *base;
	int irq;
	struct dw_hdmi_qp *hdmi;
	u8 *eld;
};

struct dw_hdmi_qp_i2s_audio_data {
	struct dw_hdmi_qp *hdmi;
	u8 *eld;

	void (*write)(struct dw_hdmi_qp *hdmi, u32 val, int offset);
	u32 (*read)(struct dw_hdmi_qp *hdmi, int offset);
	void (*mod)(struct dw_hdmi_qp *hdmi, u32 val, u32 mask, u32 reg);
};

#endif
