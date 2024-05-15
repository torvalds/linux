/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DDP_COMP_H
#define MTK_DDP_COMP_H

#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/soc/mediatek/mtk-mutex.h>

#include <drm/drm_modes.h>

struct device;
struct device_node;
struct drm_crtc;
struct drm_device;
struct mtk_plane_state;
struct drm_crtc_state;

enum mtk_ddp_comp_type {
	MTK_DISP_AAL,
	MTK_DISP_BLS,
	MTK_DISP_CCORR,
	MTK_DISP_COLOR,
	MTK_DISP_DITHER,
	MTK_DISP_DSC,
	MTK_DISP_GAMMA,
	MTK_DISP_MERGE,
	MTK_DISP_MUTEX,
	MTK_DISP_OD,
	MTK_DISP_OVL,
	MTK_DISP_OVL_2L,
	MTK_DISP_OVL_ADAPTOR,
	MTK_DISP_POSTMASK,
	MTK_DISP_PWM,
	MTK_DISP_RDMA,
	MTK_DISP_UFOE,
	MTK_DISP_WDMA,
	MTK_DPI,
	MTK_DP_INTF,
	MTK_DSI,
	MTK_DDP_COMP_TYPE_MAX,
};

struct mtk_ddp_comp;
struct cmdq_pkt;
struct mtk_ddp_comp_funcs {
	int (*power_on)(struct device *dev);
	void (*power_off)(struct device *dev);
	int (*clk_enable)(struct device *dev);
	void (*clk_disable)(struct device *dev);
	void (*config)(struct device *dev, unsigned int w,
		       unsigned int h, unsigned int vrefresh,
		       unsigned int bpc, struct cmdq_pkt *cmdq_pkt);
	void (*start)(struct device *dev);
	void (*stop)(struct device *dev);
	void (*register_vblank_cb)(struct device *dev,
				   void (*vblank_cb)(void *),
				   void *vblank_cb_data);
	void (*unregister_vblank_cb)(struct device *dev);
	void (*enable_vblank)(struct device *dev);
	void (*disable_vblank)(struct device *dev);
	unsigned int (*supported_rotations)(struct device *dev);
	unsigned int (*layer_nr)(struct device *dev);
	int (*layer_check)(struct device *dev,
			   unsigned int idx,
			   struct mtk_plane_state *state);
	void (*layer_config)(struct device *dev, unsigned int idx,
			     struct mtk_plane_state *state,
			     struct cmdq_pkt *cmdq_pkt);
	unsigned int (*gamma_get_lut_size)(struct device *dev);
	void (*gamma_set)(struct device *dev,
			  struct drm_crtc_state *state);
	void (*bgclr_in_on)(struct device *dev);
	void (*bgclr_in_off)(struct device *dev);
	void (*ctm_set)(struct device *dev,
			struct drm_crtc_state *state);
	struct device * (*dma_dev_get)(struct device *dev);
	const u32 *(*get_formats)(struct device *dev);
	size_t (*get_num_formats)(struct device *dev);
	void (*connect)(struct device *dev, struct device *mmsys_dev, unsigned int next);
	void (*disconnect)(struct device *dev, struct device *mmsys_dev, unsigned int next);
	void (*add)(struct device *dev, struct mtk_mutex *mutex);
	void (*remove)(struct device *dev, struct mtk_mutex *mutex);
	unsigned int (*encoder_index)(struct device *dev);
	enum drm_mode_status (*mode_valid)(struct device *dev, const struct drm_display_mode *mode);
};

struct mtk_ddp_comp {
	struct device *dev;
	int irq;
	unsigned int id;
	int encoder_index;
	const struct mtk_ddp_comp_funcs *funcs;
};

static inline int mtk_ddp_comp_power_on(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->power_on)
		return comp->funcs->power_on(comp->dev);
	else
		return pm_runtime_resume_and_get(comp->dev);
	return 0;
}

static inline void mtk_ddp_comp_power_off(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->power_off)
		comp->funcs->power_off(comp->dev);
	else
		pm_runtime_put(comp->dev);
}

static inline int mtk_ddp_comp_clk_enable(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->clk_enable)
		return comp->funcs->clk_enable(comp->dev);

	return 0;
}

static inline void mtk_ddp_comp_clk_disable(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->clk_disable)
		comp->funcs->clk_disable(comp->dev);
}

static inline
enum drm_mode_status mtk_ddp_comp_mode_valid(struct mtk_ddp_comp *comp,
					     const struct drm_display_mode *mode)
{
	if (comp && comp->funcs && comp->funcs->mode_valid)
		return comp->funcs->mode_valid(comp->dev, mode);
	return MODE_OK;
}

static inline void mtk_ddp_comp_config(struct mtk_ddp_comp *comp,
				       unsigned int w, unsigned int h,
				       unsigned int vrefresh, unsigned int bpc,
				       struct cmdq_pkt *cmdq_pkt)
{
	if (comp->funcs && comp->funcs->config)
		comp->funcs->config(comp->dev, w, h, vrefresh, bpc, cmdq_pkt);
}

static inline void mtk_ddp_comp_start(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->start)
		comp->funcs->start(comp->dev);
}

static inline void mtk_ddp_comp_stop(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->stop)
		comp->funcs->stop(comp->dev);
}

static inline void mtk_ddp_comp_register_vblank_cb(struct mtk_ddp_comp *comp,
						   void (*vblank_cb)(void *),
						   void *vblank_cb_data)
{
	if (comp->funcs && comp->funcs->register_vblank_cb)
		comp->funcs->register_vblank_cb(comp->dev, vblank_cb,
						vblank_cb_data);
}

static inline void mtk_ddp_comp_unregister_vblank_cb(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->unregister_vblank_cb)
		comp->funcs->unregister_vblank_cb(comp->dev);
}

static inline void mtk_ddp_comp_enable_vblank(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->enable_vblank)
		comp->funcs->enable_vblank(comp->dev);
}

static inline void mtk_ddp_comp_disable_vblank(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->disable_vblank)
		comp->funcs->disable_vblank(comp->dev);
}

static inline
unsigned int mtk_ddp_comp_supported_rotations(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->supported_rotations)
		return comp->funcs->supported_rotations(comp->dev);

	return 0;
}

static inline unsigned int mtk_ddp_comp_layer_nr(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->layer_nr)
		return comp->funcs->layer_nr(comp->dev);

	return 0;
}

static inline int mtk_ddp_comp_layer_check(struct mtk_ddp_comp *comp,
					   unsigned int idx,
					   struct mtk_plane_state *state)
{
	if (comp->funcs && comp->funcs->layer_check)
		return comp->funcs->layer_check(comp->dev, idx, state);
	return 0;
}

static inline void mtk_ddp_comp_layer_config(struct mtk_ddp_comp *comp,
					     unsigned int idx,
					     struct mtk_plane_state *state,
					     struct cmdq_pkt *cmdq_pkt)
{
	if (comp->funcs && comp->funcs->layer_config)
		comp->funcs->layer_config(comp->dev, idx, state, cmdq_pkt);
}

static inline unsigned int mtk_ddp_gamma_get_lut_size(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->gamma_get_lut_size)
		return comp->funcs->gamma_get_lut_size(comp->dev);

	return 0;
}

static inline void mtk_ddp_gamma_set(struct mtk_ddp_comp *comp,
				     struct drm_crtc_state *state)
{
	if (comp->funcs && comp->funcs->gamma_set)
		comp->funcs->gamma_set(comp->dev, state);
}

static inline void mtk_ddp_comp_bgclr_in_on(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->bgclr_in_on)
		comp->funcs->bgclr_in_on(comp->dev);
}

static inline void mtk_ddp_comp_bgclr_in_off(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->bgclr_in_off)
		comp->funcs->bgclr_in_off(comp->dev);
}

static inline void mtk_ddp_ctm_set(struct mtk_ddp_comp *comp,
				   struct drm_crtc_state *state)
{
	if (comp->funcs && comp->funcs->ctm_set)
		comp->funcs->ctm_set(comp->dev, state);
}

static inline struct device *mtk_ddp_comp_dma_dev_get(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->dma_dev_get)
		return comp->funcs->dma_dev_get(comp->dev);
	return comp->dev;
}

static inline
const u32 *mtk_ddp_comp_get_formats(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->get_formats)
		return comp->funcs->get_formats(comp->dev);

	return NULL;
}

static inline
size_t mtk_ddp_comp_get_num_formats(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->get_num_formats)
		return comp->funcs->get_num_formats(comp->dev);

	return 0;
}

static inline bool mtk_ddp_comp_add(struct mtk_ddp_comp *comp, struct mtk_mutex *mutex)
{
	if (comp->funcs && comp->funcs->add) {
		comp->funcs->add(comp->dev, mutex);
		return true;
	}
	return false;
}

static inline bool mtk_ddp_comp_remove(struct mtk_ddp_comp *comp, struct mtk_mutex *mutex)
{
	if (comp->funcs && comp->funcs->remove) {
		comp->funcs->remove(comp->dev, mutex);
		return true;
	}
	return false;
}

static inline bool mtk_ddp_comp_connect(struct mtk_ddp_comp *comp, struct device *mmsys_dev,
					unsigned int next)
{
	if (comp->funcs && comp->funcs->connect) {
		comp->funcs->connect(comp->dev, mmsys_dev, next);
		return true;
	}
	return false;
}

static inline bool mtk_ddp_comp_disconnect(struct mtk_ddp_comp *comp, struct device *mmsys_dev,
					   unsigned int next)
{
	if (comp->funcs && comp->funcs->disconnect) {
		comp->funcs->disconnect(comp->dev, mmsys_dev, next);
		return true;
	}
	return false;
}

static inline void mtk_ddp_comp_encoder_index_set(struct mtk_ddp_comp *comp)
{
	if (comp->funcs && comp->funcs->encoder_index)
		comp->encoder_index = (int)comp->funcs->encoder_index(comp->dev);
}

int mtk_ddp_comp_get_id(struct device_node *node,
			enum mtk_ddp_comp_type comp_type);
unsigned int mtk_find_possible_crtcs(struct drm_device *drm, struct device *dev);
int mtk_ddp_comp_init(struct device_node *comp_node, struct mtk_ddp_comp *comp,
		      unsigned int comp_id);
enum mtk_ddp_comp_type mtk_ddp_comp_get_type(unsigned int comp_id);
void mtk_ddp_write(struct cmdq_pkt *cmdq_pkt, unsigned int value,
		   struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
		   unsigned int offset);
void mtk_ddp_write_relaxed(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			   struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
			   unsigned int offset);
void mtk_ddp_write_mask(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
			unsigned int offset, unsigned int mask);
#endif /* MTK_DDP_COMP_H */
