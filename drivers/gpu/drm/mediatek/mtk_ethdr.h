/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_ETHDR_H__
#define __MTK_ETHDR_H__

void mtk_ethdr_start(struct device *dev);
void mtk_ethdr_stop(struct device *dev);
int mtk_ethdr_clk_enable(struct device *dev);
void mtk_ethdr_clk_disable(struct device *dev);
void mtk_ethdr_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
void mtk_ethdr_layer_config(struct device *dev, unsigned int idx,
			    struct mtk_plane_state *state,
			    struct cmdq_pkt *cmdq_pkt);
void mtk_ethdr_register_vblank_cb(struct device *dev,
				  void (*vblank_cb)(void *),
				  void *vblank_cb_data);
void mtk_ethdr_unregister_vblank_cb(struct device *dev);
void mtk_ethdr_enable_vblank(struct device *dev);
void mtk_ethdr_disable_vblank(struct device *dev);
#endif
