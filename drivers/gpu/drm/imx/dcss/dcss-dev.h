/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 NXP.
 */

#ifndef __DCSS_PRV_H__
#define __DCSS_PRV_H__

#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <linux/io.h>
#include <video/videomode.h>

#define SET			0x04
#define CLR			0x08
#define TGL			0x0C

#define dcss_writel(v, c)	writel((v), (c))
#define dcss_readl(c)		readl(c)
#define dcss_set(v, c)		writel((v), (c) + SET)
#define dcss_clr(v, c)		writel((v), (c) + CLR)
#define dcss_toggle(v, c)	writel((v), (c) + TGL)

static inline void dcss_update(u32 v, u32 m, void __iomem *c)
{
	writel((readl(c) & ~(m)) | (v), (c));
}

#define DCSS_DBG_REG(reg)	{.name = #reg, .ofs = reg}

enum {
	DCSS_IMX8MQ = 0,
};

struct dcss_type_data {
	const char *name;
	u32 blkctl_ofs;
	u32 ctxld_ofs;
	u32 rdsrc_ofs;
	u32 wrscl_ofs;
	u32 dtg_ofs;
	u32 scaler_ofs;
	u32 ss_ofs;
	u32 dpr_ofs;
	u32 dtrc_ofs;
	u32 dec400d_ofs;
	u32 hdr10_ofs;
};

struct dcss_debug_reg {
	char *name;
	u32 ofs;
};

enum dcss_ctxld_ctx_type {
	CTX_DB,
	CTX_SB_HP, /* high-priority */
	CTX_SB_LP, /* low-priority  */
};

struct dcss_dev {
	struct device *dev;
	const struct dcss_type_data *devtype;
	struct device_node *of_port;

	u32 start_addr;

	struct dcss_blkctl *blkctl;
	struct dcss_ctxld *ctxld;
	struct dcss_dpr *dpr;
	struct dcss_dtg *dtg;
	struct dcss_ss *ss;
	struct dcss_hdr10 *hdr10;
	struct dcss_scaler *scaler;
	struct dcss_dtrc *dtrc;
	struct dcss_dec400d *dec400d;
	struct dcss_wrscl *wrscl;
	struct dcss_rdsrc *rdsrc;

	struct clk *apb_clk;
	struct clk *axi_clk;
	struct clk *pix_clk;
	struct clk *rtrm_clk;
	struct clk *dtrc_clk;
	struct clk *pll_src_clk;
	struct clk *pll_phy_ref_clk;

	bool hdmi_output;

	void (*disable_callback)(void *data);
	struct completion disable_completion;
};

struct dcss_dev *dcss_drv_dev_to_dcss(struct device *dev);
struct drm_device *dcss_drv_dev_to_drm(struct device *dev);
struct dcss_dev *dcss_dev_create(struct device *dev, bool hdmi_output);
void dcss_dev_destroy(struct dcss_dev *dcss);
int dcss_dev_runtime_suspend(struct device *dev);
int dcss_dev_runtime_resume(struct device *dev);
int dcss_dev_suspend(struct device *dev);
int dcss_dev_resume(struct device *dev);
void dcss_enable_dtg_and_ss(struct dcss_dev *dcss);
void dcss_disable_dtg_and_ss(struct dcss_dev *dcss);

/* BLKCTL */
int dcss_blkctl_init(struct dcss_dev *dcss, unsigned long blkctl_base);
void dcss_blkctl_cfg(struct dcss_blkctl *blkctl);
void dcss_blkctl_exit(struct dcss_blkctl *blkctl);

/* CTXLD */
int dcss_ctxld_init(struct dcss_dev *dcss, unsigned long ctxld_base);
void dcss_ctxld_exit(struct dcss_ctxld *ctxld);
void dcss_ctxld_write(struct dcss_ctxld *ctxld, u32 ctx_id,
		      u32 val, u32 reg_idx);
int dcss_ctxld_resume(struct dcss_ctxld *dcss_ctxld);
int dcss_ctxld_suspend(struct dcss_ctxld *dcss_ctxld);
void dcss_ctxld_write_irqsafe(struct dcss_ctxld *ctlxd, u32 ctx_id, u32 val,
			      u32 reg_ofs);
void dcss_ctxld_kick(struct dcss_ctxld *ctxld);
bool dcss_ctxld_is_flushed(struct dcss_ctxld *ctxld);
int dcss_ctxld_enable(struct dcss_ctxld *ctxld);
void dcss_ctxld_register_completion(struct dcss_ctxld *ctxld,
				    struct completion *dis_completion);
void dcss_ctxld_assert_locked(struct dcss_ctxld *ctxld);

/* DPR */
int dcss_dpr_init(struct dcss_dev *dcss, unsigned long dpr_base);
void dcss_dpr_exit(struct dcss_dpr *dpr);
void dcss_dpr_write_sysctrl(struct dcss_dpr *dpr);
void dcss_dpr_set_res(struct dcss_dpr *dpr, int ch_num, u32 xres, u32 yres);
void dcss_dpr_addr_set(struct dcss_dpr *dpr, int ch_num, u32 luma_base_addr,
		       u32 chroma_base_addr, u16 pitch);
void dcss_dpr_enable(struct dcss_dpr *dpr, int ch_num, bool en);
void dcss_dpr_format_set(struct dcss_dpr *dpr, int ch_num,
			 const struct drm_format_info *format, u64 modifier);
void dcss_dpr_set_rotation(struct dcss_dpr *dpr, int ch_num, u32 rotation);

/* DTG */
int dcss_dtg_init(struct dcss_dev *dcss, unsigned long dtg_base);
void dcss_dtg_exit(struct dcss_dtg *dtg);
bool dcss_dtg_vblank_irq_valid(struct dcss_dtg *dtg);
void dcss_dtg_vblank_irq_enable(struct dcss_dtg *dtg, bool en);
void dcss_dtg_vblank_irq_clear(struct dcss_dtg *dtg);
void dcss_dtg_sync_set(struct dcss_dtg *dtg, struct videomode *vm);
void dcss_dtg_css_set(struct dcss_dtg *dtg);
void dcss_dtg_enable(struct dcss_dtg *dtg);
void dcss_dtg_shutoff(struct dcss_dtg *dtg);
bool dcss_dtg_is_enabled(struct dcss_dtg *dtg);
void dcss_dtg_ctxld_kick_irq_enable(struct dcss_dtg *dtg, bool en);
bool dcss_dtg_global_alpha_changed(struct dcss_dtg *dtg, int ch_num, int alpha);
void dcss_dtg_plane_alpha_set(struct dcss_dtg *dtg, int ch_num,
			      const struct drm_format_info *format, int alpha);
void dcss_dtg_plane_pos_set(struct dcss_dtg *dtg, int ch_num,
			    int px, int py, int pw, int ph);
void dcss_dtg_ch_enable(struct dcss_dtg *dtg, int ch_num, bool en);

/* SUBSAM */
int dcss_ss_init(struct dcss_dev *dcss, unsigned long subsam_base);
void dcss_ss_exit(struct dcss_ss *ss);
void dcss_ss_enable(struct dcss_ss *ss);
void dcss_ss_shutoff(struct dcss_ss *ss);
void dcss_ss_subsam_set(struct dcss_ss *ss);
void dcss_ss_sync_set(struct dcss_ss *ss, struct videomode *vm,
		      bool phsync, bool pvsync);

/* SCALER */
int dcss_scaler_init(struct dcss_dev *dcss, unsigned long scaler_base);
void dcss_scaler_exit(struct dcss_scaler *scl);
void dcss_scaler_set_filter(struct dcss_scaler *scl, int ch_num,
			    enum drm_scaling_filter scaling_filter);
void dcss_scaler_setup(struct dcss_scaler *scl, int ch_num,
		       const struct drm_format_info *format,
		       int src_xres, int src_yres, int dst_xres, int dst_yres,
		       u32 vrefresh_hz);
void dcss_scaler_ch_enable(struct dcss_scaler *scl, int ch_num, bool en);
int dcss_scaler_get_min_max_ratios(struct dcss_scaler *scl, int ch_num,
				   int *min, int *max);
void dcss_scaler_write_sclctrl(struct dcss_scaler *scl);

#endif /* __DCSS_PRV_H__ */
