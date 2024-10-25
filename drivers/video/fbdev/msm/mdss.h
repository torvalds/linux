/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2012-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef MDSS_H
#define MDSS_H

#include <linux/msm_ion.h>
#include <linux/msm_mdp.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/irqreturn.h>
#include <linux/irqdomain.h>
#include <linux/mdss_io_util.h>
#include <linux/mdss_smmu_ext.h>

#include <linux/msm-bus.h>
#include <linux/file.h>
#include <linux/dma-direction.h>
#include <linux/dma-buf.h>

#include "mdss_panel.h"

#define MAX_DRV_SUP_MMB_BLKS	44
#define MAX_DRV_SUP_PIPES 10
#define MAX_CLIENT_NAME_LEN 20

#define MDSS_PINCTRL_STATE_DEFAULT "mdss_default"
#define MDSS_PINCTRL_STATE_SLEEP  "mdss_sleep"

enum mdss_mdp_clk_type {
	MDSS_CLK_AHB,
	MDSS_CLK_AXI,
	MDSS_CLK_MDP_CORE,
	MDSS_CLK_MDP_LUT,
	MDSS_CLK_MDP_VSYNC,
	MDSS_CLK_MNOC_AHB,
	MDSS_CLK_THROTTLE_AXI,
	MDSS_CLK_BIMC,
	MDSS_MAX_CLK
};

enum mdss_iommu_domain_type {
	MDSS_IOMMU_DOMAIN_UNSECURE,
	MDSS_IOMMU_DOMAIN_ROT_UNSECURE,
	MDSS_IOMMU_DOMAIN_SECURE,
	MDSS_IOMMU_DOMAIN_ROT_SECURE,
	MDSS_IOMMU_MAX_DOMAIN
};

enum mdss_bus_vote_type {
	VOTE_INDEX_DISABLE,
	VOTE_INDEX_LOW,
	VOTE_INDEX_MID,
	VOTE_INDEX_HIGH,
	VOTE_INDEX_MAX,
};

struct mdss_hw_settings {
	char __iomem *reg;
	u32 val;
};

struct mdss_max_bw_settings {
	u32 mdss_max_bw_mode;
	u32 mdss_max_bw_val;
};

struct mdss_debug_inf {
	void *debug_data;
	void (*debug_enable_clock)(int on);
};

struct mdss_perf_tune {
	unsigned long min_mdp_clk;
	u64 min_bus_vote;
};

#define MDSS_IRQ_SUSPEND	-1
#define MDSS_IRQ_RESUME		1
#define MDSS_IRQ_REQ		0

struct mdss_intr {
	/* requested intr */
	u32 req;
	/* currently enabled intr */
	u32 curr;
	int state;
	spinlock_t lock;
};

struct simplified_prefill_factors {
	u32 fmt_mt_nv12_factor;
	u32 fmt_mt_factor;
	u32 fmt_linear_factor;
	u32 scale_factor;
	u32 xtra_ff_factor;
};

struct mdss_prefill_data {
	u32 ot_bytes;
	u32 y_buf_bytes;
	u32 y_scaler_lines_bilinear;
	u32 y_scaler_lines_caf;
	u32 post_scaler_pixels;
	u32 pp_pixels;
	u32 fbc_lines;
	u32 ts_threshold;
	u32 ts_end;
	u32 ts_overhead;
	struct mult_factor ts_rate;
	struct simplified_prefill_factors prefill_factors;
};

struct mdss_mdp_dsc {
	u32 num;
	char __iomem *base;
};

enum mdss_hw_index {
	MDSS_HW_MDP,
	MDSS_HW_DSI0 = 1,
	MDSS_HW_DSI1,
	MDSS_HW_HDMI,
	MDSS_HW_EDP,
	MDSS_HW_MISC,
	MDSS_MAX_HW_BLK
};

enum mdss_bus_clients {
	MDSS_MDP_RT,
	MDSS_DSI_RT,
	MDSS_HW_RT,
	MDSS_MDP_NRT,
	MDSS_MAX_BUS_CLIENTS
};

struct mdss_pp_block_off {
	u32 sspp_igc_lut_off;
	u32 vig_pcc_off;
	u32 rgb_pcc_off;
	u32 dma_pcc_off;
	u32 lm_pgc_off;
	u32 dspp_gamut_off;
	u32 dspp_pcc_off;
	u32 dspp_pgc_off;
};

enum mdss_hw_quirk {
	MDSS_QUIRK_BWCPANIC,
	MDSS_QUIRK_ROTCDP,
	MDSS_QUIRK_DOWNSCALE_HANG,
	MDSS_QUIRK_DSC_RIGHT_ONLY_PU,
	MDSS_QUIRK_DSC_2SLICE_PU_THRPUT,
	MDSS_QUIRK_DMA_BI_DIR,
	MDSS_QUIRK_FMT_PACK_PATTERN,
	MDSS_QUIRK_NEED_SECURE_MAP,
	MDSS_QUIRK_SRC_SPLIT_ALWAYS,
	MDSS_QUIRK_MMSS_GDSC_COLLAPSE,
	MDSS_QUIRK_MDP_CLK_SET_RATE,
	MDSS_QUIRK_HDR_SUPPORT_ENABLED,
	MDSS_QUIRK_MAX,
};

enum mdss_hw_capabilities {
	MDSS_CAPS_YUV_CONFIG,
	MDSS_CAPS_SCM_RESTORE_NOT_REQUIRED,
	MDSS_CAPS_3D_MUX_UNDERRUN_RECOVERY_SUPPORTED,
	MDSS_CAPS_MIXER_1_FOR_WB,
	MDSS_CAPS_QSEED3,
	MDSS_CAPS_DEST_SCALER,
	MDSS_CAPS_10_BIT_SUPPORTED,
	MDSS_CAPS_CWB_SUPPORTED,
	MDSS_CAPS_MDP_VOTE_CLK_NOT_SUPPORTED,
	MDSS_CAPS_AVR_SUPPORTED,
	MDSS_CAPS_SEC_DETACH_SMMU,
	MDSS_CAPS_MAX,
};

enum mdss_qos_settings {
	MDSS_QOS_PER_PIPE_IB,
	MDSS_QOS_OVERHEAD_FACTOR,
	MDSS_QOS_CDP,
	MDSS_QOS_OTLIM,
	MDSS_QOS_PER_PIPE_LUT,
	MDSS_QOS_SIMPLIFIED_PREFILL,
	MDSS_QOS_VBLANK_PANIC_CTRL,
	MDSS_QOS_TS_PREFILL,
	MDSS_QOS_REMAPPER,
	MDSS_QOS_IB_NOCR,
	MDSS_QOS_WB2_WRITE_GATHER_EN,
	MDSS_QOS_WB_QOS,
	MDSS_QOS_MAX,
};

enum mdss_mdp_pipe_type {
	MDSS_MDP_PIPE_TYPE_INVALID = -1,
	MDSS_MDP_PIPE_TYPE_VIG = 0,
	MDSS_MDP_PIPE_TYPE_RGB,
	MDSS_MDP_PIPE_TYPE_DMA,
	MDSS_MDP_PIPE_TYPE_CURSOR,
	MDSS_MDP_PIPE_TYPE_MAX,
};

enum mdss_mdp_intf_index {
	MDSS_MDP_NO_INTF,
	MDSS_MDP_INTF0,
	MDSS_MDP_INTF1,
	MDSS_MDP_INTF2,
	MDSS_MDP_INTF3,
	MDSS_MDP_MAX_INTF
};

struct reg_bus_client {
	char name[MAX_CLIENT_NAME_LEN];
	short usecase_ndx;
	u32 id;
	struct list_head list;
};

struct mdss_smmu_client {
	struct mdss_smmu_intf base;
	struct dma_iommu_mapping *mmu_mapping;
	struct dss_module_power mp;
	struct reg_bus_client *reg_bus_clt;
	bool domain_attached;
	bool domain_reattach;
	bool handoff_pending;
	void __iomem *mmu_base;
	struct list_head _client;
};

struct mdss_mdp_qseed3_lut_tbl {
	bool valid;
	u32 *dir_lut;
	u32 *cir_lut;
	u32 *sep_lut;
};

struct mdss_scaler_block {
	u32 vig_scaler_off;
	u32 vig_scaler_lut_off;
	u32 has_dest_scaler;
	char __iomem *dest_base;
	u32 ndest_scalers;
	u32 *dest_scaler_off;
	u32 *dest_scaler_lut_off;
	struct mdss_mdp_qseed3_lut_tbl lut_tbl;

	/*
	 * Lock is mainly to serialize access to LUT.
	 * LUT values come asynchronously from userspace
	 * via ioctl.
	 */
	struct mutex scaler_lock;
};

struct mdss_data_type;

struct mdss_smmu_ops {
	int (*smmu_attach)(struct mdss_data_type *mdata);
	int (*smmu_detach)(struct mdss_data_type *mdata);
	int (*smmu_get_domain_id)(u32 type);
	struct dma_buf_attachment  * (*smmu_dma_buf_attach)(
			struct dma_buf *dma_buf, struct device *devce,
			int domain);
	int (*smmu_map_dma_buf)(struct dma_buf *dma_buf,
			struct sg_table *table, int domain,
			dma_addr_t *iova, unsigned long *size, int dir);
	void (*smmu_unmap_dma_buf)(struct sg_table *table, int domain,
			int dir, struct dma_buf *dma_buf);
	int (*smmu_dma_alloc_coherent)(struct device *dev, size_t size,
			dma_addr_t *phys, dma_addr_t *iova, void **cpu_addr,
			gfp_t gfp, int domain);
	void (*smmu_dma_free_coherent)(struct device *dev, size_t size,
			void *cpu_addr, dma_addr_t phys, dma_addr_t iova,
			int domain);
	int (*smmu_map)(int domain, phys_addr_t iova, phys_addr_t phys, int
			gfp_order, int prot);
	void (*smmu_unmap)(int domain, unsigned long iova, int gfp_order);
	char * (*smmu_dsi_alloc_buf)(struct device *dev, int size,
			dma_addr_t *dmap, gfp_t gfp);
	int (*smmu_dsi_map_buffer)(phys_addr_t phys, unsigned int domain,
			unsigned long size, dma_addr_t *dma_addr,
			void *cpu_addr, int dir);
	void (*smmu_dsi_unmap_buffer)(dma_addr_t dma_addr, int domain,
			unsigned long size, int dir);
	void (*smmu_deinit)(struct mdss_data_type *mdata);
};

struct mdss_data_type {
	u32 mdp_rev;
	struct clk *mdp_clk[MDSS_MAX_CLK];
	struct regulator *fs;
	struct regulator *core_gdsc;
	struct regulator *vdd_cx;
	u32 vdd_cx_min_uv;
	u32 vdd_cx_max_uv;
	bool batfet_required;
	struct regulator *batfet;
	bool en_svs_high;
	u32 max_mdp_clk_rate;
	struct mdss_util_intf *mdss_util;
	unsigned long mdp_clk_rate;

	struct platform_device *pdev;
	struct dss_io_data mdss_io;
	struct dss_io_data vbif_io;
	struct dss_io_data vbif_nrt_io;
	char __iomem *mdp_base;

	struct mdss_smmu_client mdss_smmu[MDSS_IOMMU_MAX_DOMAIN];
	struct mdss_smmu_ops smmu_ops;
	struct mutex reg_lock;

	/* bitmap to track pipes that have BWC enabled */
	DECLARE_BITMAP(bwc_enable_map, MAX_DRV_SUP_PIPES);
	/* bitmap to track hw workarounds */
	DECLARE_BITMAP(mdss_quirk_map, MDSS_QUIRK_MAX);
	/* bitmap to track total mmbs in use */
	DECLARE_BITMAP(mmb_alloc_map, MAX_DRV_SUP_MMB_BLKS);
	/* bitmap to track qos applicable settings */
	DECLARE_BITMAP(mdss_qos_map, MDSS_QOS_MAX);
	/* bitmap to track hw capabilities/features */
	DECLARE_BITMAP(mdss_caps_map, MDSS_CAPS_MAX);

	u32 has_bwc;
	/* values used when HW has a common panic/robust LUT */
	u32 default_panic_lut0;
	u32 default_panic_lut1;
	u32 default_robust_lut;

	/* values used when HW has panic/robust LUTs per pipe */
	u32 default_panic_lut_per_pipe_linear;
	u32 default_panic_lut_per_pipe_tile;
	u32 default_robust_lut_per_pipe_linear;
	u32 default_robust_lut_per_pipe_tile;

	u32 has_decimation;
	bool has_fixed_qos_arbiter_enabled;
	bool has_panic_ctrl;
	u32 wfd_mode;
	u32 has_no_lut_read;
	atomic_t sd_client_count;
	atomic_t sc_client_count;
	u8 has_wb_ad;
	u8 has_non_scalar_rgb;
	bool has_src_split;
	bool idle_pc_enabled;
	bool has_pingpong_split;
	bool has_pixel_ram;
	bool needs_hist_vote;
	bool has_ubwc;
	bool has_wb_ubwc;
	bool has_separate_rotator;

	u32 default_ot_rd_limit;
	u32 default_ot_wr_limit;

	struct irq_domain *irq_domain;
	u32 *mdp_irq_raw;
	u32 *mdp_irq_export;
	u32 *mdp_irq_mask;
	u32 mdp_hist_irq_mask;
	u32 mdp_intf_irq_mask;

	int suspend_fs_ena;
	u8 clk_ena;
	u8 fs_ena;
	u8 vsync_ena;

	struct notifier_block gdsc_cb;

	u32 res_init;

	u32 highest_bank_bit;
	u32 smp_mb_cnt;
	u32 smp_mb_size;
	u32 smp_mb_per_pipe;
	u32 pixel_ram_size;

	u32 rot_block_size;

	/* HW RT  bus (AXI) */
	u32 hw_rt_bus_hdl;
	u32 hw_rt_bus_ref_cnt;

	/* data bus (AXI) */
	u32 bus_hdl;
	u32 bus_ref_cnt;
	struct mutex bus_lock;

	/* register bus (AHB) */
	u32 reg_bus_hdl;
	u32 reg_bus_usecase_ndx;
	struct list_head reg_bus_clist;
	struct mutex reg_bus_lock;
	struct reg_bus_client *reg_bus_clt;
	struct reg_bus_client *pp_reg_bus_clt;

	u32 axi_port_cnt;
	u32 nrt_axi_port_cnt;
	u32 bus_channels;
	u32 curr_bw_uc_idx;
	u32 ao_bw_uc_idx; /* active only idx */
	struct msm_bus_scale_pdata *bus_scale_table;
	struct msm_bus_scale_pdata *reg_bus_scale_table;
	struct msm_bus_scale_pdata *hw_rt_bus_scale_table;
	u32 max_bw_low;
	u32 max_bw_high;
	u32 max_bw_per_pipe;
	u32 *vbif_rt_qos;
	u32 *vbif_nrt_qos;
	u32 npriority_lvl;

	struct mult_factor ab_factor;
	struct mult_factor ib_factor;
	struct mult_factor ib_factor_overlap;
	struct mult_factor clk_factor;
	struct mult_factor per_pipe_ib_factor;
	bool apply_post_scale_bytes;
	bool hflip_buffer_reused;

	u32 disable_prefill;
	u32 *clock_levels;
	u32 nclk_lvl;

	u32 enable_gate;
	u32 enable_bw_release;
	u32 enable_rotator_bw_release;
	u32 enable_cdp;
	u32 serialize_wait4pp;
	u32 wait4autorefresh;
	u32 lines_before_active;

	struct mdss_hw_settings *hw_settings;

	int rects_per_sspp[MDSS_MDP_PIPE_TYPE_MAX];
	struct mdss_mdp_pipe *vig_pipes;
	struct mdss_mdp_pipe *rgb_pipes;
	struct mdss_mdp_pipe *dma_pipes;
	struct mdss_mdp_pipe *cursor_pipes;
	u32 nvig_pipes;
	u32 nrgb_pipes;
	u32 ndma_pipes;
	u32 max_target_zorder;
	u8  ncursor_pipes;
	u32 max_cursor_size;

	u32 nppb_ctl;
	u32 *ppb_ctl;
	u32 nppb_cfg;
	u32 *ppb_cfg;
	char __iomem *slave_pingpong_base;

	struct mdss_mdp_mixer *mixer_intf;
	struct mdss_mdp_mixer *mixer_wb;
	u32 nmixers_intf;
	u32 nmixers_wb;
	u32 max_mixer_width;
	u32 max_pipe_width;

	struct mdss_mdp_writeback *wb;
	u32 nwb;
	u32 *wb_offsets;
	u32 nwb_offsets;
	struct mutex wb_lock;

	struct mdss_mdp_ctl *ctl_off;
	u32 nctl;
	u32 ndspp;

	struct mdss_mdp_dp_intf *dp_off;
	u32 ndp;
	void *video_intf;
	u32 nintf;

	struct mdss_mdp_ad *ad_off;
	struct mdss_ad_info *ad_cfgs;
	u32 nad_cfgs;
	u32 nmax_concurrent_ad_hw;
	struct workqueue_struct *ad_calc_wq;
	u32 ad_debugen;
	bool mem_retain;

	struct mdss_intr hist_intr;

	int iommu_attached;

	u32 dbg_bus_flags;
	struct debug_bus *dbg_bus;
	u32 dbg_bus_size;
	struct vbif_debug_bus *vbif_dbg_bus;
	u32 vbif_dbg_bus_size;
	struct vbif_debug_bus *nrt_vbif_dbg_bus;
	u32 nrt_vbif_dbg_bus_size;
	struct mdss_debug_inf debug_inf;
	bool mixer_switched;
	struct mdss_panel_cfg pan_cfg;
	struct mdss_prefill_data prefill_data;
	u32 min_prefill_lines; /* this changes within different chipsets */
	u32 props;

	bool twm_en;
	int handoff_pending;
	bool idle_pc;
	struct mdss_perf_tune perf_tune;
	bool traffic_shaper_en;
	int iommu_ref_cnt;
	u32 latency_buff_per;
	atomic_t active_intf_cnt;
	bool has_rot_dwnscale;
	bool regulator_notif_register;

	u64 ab[MDSS_MAX_BUS_CLIENTS];
	u64 ib[MDSS_MAX_BUS_CLIENTS];
	struct mdss_pp_block_off pp_block_off;

	struct mdss_mdp_cdm *cdm_off;
	u32 ncdm;
	struct mutex cdm_lock;

	struct mdss_mdp_dsc *dsc_off;
	u32 ndsc;

	struct mdss_max_bw_settings *max_bw_settings;
	u32 bw_mode_bitmap;
	u32 max_bw_settings_cnt;
	bool bw_limit_pending;

	struct mdss_max_bw_settings *max_per_pipe_bw_settings;
	u32 mdss_per_pipe_bw_cnt;
	u32 min_bw_per_pipe;

	u32 bcolor0;
	u32 bcolor1;
	u32 bcolor2;
	struct mdss_scaler_block *scaler_off;

	u32 max_dest_scaler_input_width;
	u32 max_dest_scaler_output_width;
	struct mdss_mdp_destination_scaler *ds;
	u32 sec_disp_en;
	u32 sec_cam_en;
	u32 sec_session_cnt;
	wait_queue_head_t secure_waitq;
	struct mult_factor bus_throughput_factor;
};

extern struct mdss_data_type *mdss_res;

struct irq_info {
	u32 irq;
	u32 irq_mask;
	u32 irq_wake_mask;
	u32 irq_ena;
	u32 irq_wake_ena;
	u32 irq_buzy;
};

struct mdss_hw {
	u32 hw_ndx;
	void *ptr;
	struct irq_info *irq_info;
	irqreturn_t (*irq_handler)(int irq, void *ptr);
};

struct irq_info *mdss_intr_line(void);
void mdss_bus_bandwidth_ctrl(int enable);
int mdss_iommu_ctrl(int enable);
int mdss_bus_scale_set_quota(int client, u64 ab_quota, u64 ib_quota);
int mdss_update_reg_bus_vote(struct reg_bus_client *bus_client,
				u32 usecase_ndx);
struct reg_bus_client *mdss_reg_bus_vote_client_create(char *client_name);
void mdss_reg_bus_vote_client_destroy(struct reg_bus_client *bus_client);

struct mdss_util_intf {
	bool mdp_probe_done;
	int (*register_irq)(struct mdss_hw *hw);
	void (*enable_irq)(struct mdss_hw *hw);
	void (*disable_irq)(struct mdss_hw *hw);
	void (*enable_wake_irq)(struct mdss_hw *hw);
	void (*disable_wake_irq)(struct mdss_hw *hw);
	void (*disable_irq_nosync)(struct mdss_hw *hw);
	int (*irq_dispatch)(u32 hw_ndx, int irq, void *ptr);
	int (*get_iommu_domain)(u32 type);
	int (*iommu_attached)(void);
	int (*iommu_ctrl)(int enable);
	void (*iommu_lock)(void);
	void (*iommu_unlock)(void);
	void (*vbif_reg_lock)(void);
	void (*vbif_reg_unlock)(void);
	int (*secure_session_ctrl)(int enable);
	void (*bus_bandwidth_ctrl)(int enable);
	int (*bus_scale_set_quota)(int client, u64 ab_quota, u64 ib_quota);
	int (*panel_intf_status)(u32 disp_num, u32 intf_type);
	struct mdss_panel_cfg* (*panel_intf_type)(int intf_val);
	int (*dyn_clk_gating_ctrl)(int enable);
	bool (*mdp_handoff_pending)(void);
};

struct mdss_util_intf *mdss_get_util_intf(void);
bool mdss_get_irq_enable_state(struct mdss_hw *hw);

static inline int mdss_get_sd_client_cnt(void)
{
	if (!mdss_res)
		return 0;
	else
		return atomic_read(&mdss_res->sd_client_count);
}

static inline int mdss_get_sc_client_cnt(void)
{
	if (!mdss_res)
		return 0;
	else
		return atomic_read(&mdss_res->sc_client_count);
}

static inline void mdss_set_quirk(struct mdss_data_type *mdata,
	enum mdss_hw_quirk bit)
{
	set_bit(bit, mdata->mdss_quirk_map);
}

static inline bool mdss_has_quirk(struct mdss_data_type *mdata,
	enum mdss_hw_quirk bit)
{
	return test_bit(bit, mdata->mdss_quirk_map);
}

#define MDSS_VBIF_WRITE(mdata, offset, value, nrt_vbif) \
		(nrt_vbif ? dss_reg_w(&mdata->vbif_nrt_io, offset, value, 0) :\
		dss_reg_w(&mdata->vbif_io, offset, value, 0))
#define MDSS_VBIF_READ(mdata, offset, nrt_vbif) \
		(nrt_vbif ? dss_reg_r(&mdata->vbif_nrt_io, offset, 0) :\
		dss_reg_r(&mdata->vbif_io, offset, 0))
#define MDSS_REG_WRITE(mdata, offset, value) \
		dss_reg_w(&mdata->mdss_io, offset, value, 0)
#define MDSS_REG_READ(mdata, offset) \
		dss_reg_r(&mdata->mdss_io, offset, 0)

#endif /* MDSS_H */
