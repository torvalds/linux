// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#include <drm/drm_blend.h>
#include <drm/drm_print.h>
#include "d71_dev.h"
#include "malidp_io.h"

static u64 get_lpu_event(struct d71_pipeline *d71_pipeline)
{
	u32 __iomem *reg = d71_pipeline->lpu_addr;
	u32 status, raw_status;
	u64 evts = 0ULL;

	raw_status = malidp_read32(reg, BLK_IRQ_RAW_STATUS);
	if (raw_status & LPU_IRQ_IBSY)
		evts |= KOMEDA_EVENT_IBSY;
	if (raw_status & LPU_IRQ_EOW)
		evts |= KOMEDA_EVENT_EOW;
	if (raw_status & LPU_IRQ_OVR)
		evts |= KOMEDA_EVENT_OVR;

	if (raw_status & (LPU_IRQ_ERR | LPU_IRQ_IBSY | LPU_IRQ_OVR)) {
		u32 restore = 0, tbu_status;
		/* Check error of LPU status */
		status = malidp_read32(reg, BLK_STATUS);
		if (status & LPU_STATUS_AXIE) {
			restore |= LPU_STATUS_AXIE;
			evts |= KOMEDA_ERR_AXIE;
		}
		if (status & LPU_STATUS_ACE0) {
			restore |= LPU_STATUS_ACE0;
			evts |= KOMEDA_ERR_ACE0;
		}
		if (status & LPU_STATUS_ACE1) {
			restore |= LPU_STATUS_ACE1;
			evts |= KOMEDA_ERR_ACE1;
		}
		if (status & LPU_STATUS_ACE2) {
			restore |= LPU_STATUS_ACE2;
			evts |= KOMEDA_ERR_ACE2;
		}
		if (status & LPU_STATUS_ACE3) {
			restore |= LPU_STATUS_ACE3;
			evts |= KOMEDA_ERR_ACE3;
		}
		if (status & LPU_STATUS_FEMPTY) {
			restore |= LPU_STATUS_FEMPTY;
			evts |= KOMEDA_EVENT_EMPTY;
		}
		if (status & LPU_STATUS_FFULL) {
			restore |= LPU_STATUS_FFULL;
			evts |= KOMEDA_EVENT_FULL;
		}

		if (restore != 0)
			malidp_write32_mask(reg, BLK_STATUS, restore, 0);

		restore = 0;
		/* Check errors of TBU status */
		tbu_status = malidp_read32(reg, LPU_TBU_STATUS);
		if (tbu_status & LPU_TBU_STATUS_TCF) {
			restore |= LPU_TBU_STATUS_TCF;
			evts |= KOMEDA_ERR_TCF;
		}
		if (tbu_status & LPU_TBU_STATUS_TTNG) {
			restore |= LPU_TBU_STATUS_TTNG;
			evts |= KOMEDA_ERR_TTNG;
		}
		if (tbu_status & LPU_TBU_STATUS_TITR) {
			restore |= LPU_TBU_STATUS_TITR;
			evts |= KOMEDA_ERR_TITR;
		}
		if (tbu_status & LPU_TBU_STATUS_TEMR) {
			restore |= LPU_TBU_STATUS_TEMR;
			evts |= KOMEDA_ERR_TEMR;
		}
		if (tbu_status & LPU_TBU_STATUS_TTF) {
			restore |= LPU_TBU_STATUS_TTF;
			evts |= KOMEDA_ERR_TTF;
		}
		if (restore != 0)
			malidp_write32_mask(reg, LPU_TBU_STATUS, restore, 0);
	}

	malidp_write32(reg, BLK_IRQ_CLEAR, raw_status);
	return evts;
}

static u64 get_cu_event(struct d71_pipeline *d71_pipeline)
{
	u32 __iomem *reg = d71_pipeline->cu_addr;
	u32 status, raw_status;
	u64 evts = 0ULL;

	raw_status = malidp_read32(reg, BLK_IRQ_RAW_STATUS);
	if (raw_status & CU_IRQ_OVR)
		evts |= KOMEDA_EVENT_OVR;

	if (raw_status & (CU_IRQ_ERR | CU_IRQ_OVR)) {
		status = malidp_read32(reg, BLK_STATUS) & 0x7FFFFFFF;
		if (status & CU_STATUS_CPE)
			evts |= KOMEDA_ERR_CPE;
		if (status & CU_STATUS_ZME)
			evts |= KOMEDA_ERR_ZME;
		if (status & CU_STATUS_CFGE)
			evts |= KOMEDA_ERR_CFGE;
		if (status)
			malidp_write32_mask(reg, BLK_STATUS, status, 0);
	}

	malidp_write32(reg, BLK_IRQ_CLEAR, raw_status);

	return evts;
}

static u64 get_dou_event(struct d71_pipeline *d71_pipeline)
{
	u32 __iomem *reg = d71_pipeline->dou_addr;
	u32 status, raw_status;
	u64 evts = 0ULL;

	raw_status = malidp_read32(reg, BLK_IRQ_RAW_STATUS);
	if (raw_status & DOU_IRQ_PL0)
		evts |= KOMEDA_EVENT_VSYNC;
	if (raw_status & DOU_IRQ_UND)
		evts |= KOMEDA_EVENT_URUN;

	if (raw_status & (DOU_IRQ_ERR | DOU_IRQ_UND)) {
		u32 restore  = 0;

		status = malidp_read32(reg, BLK_STATUS);
		if (status & DOU_STATUS_DRIFTTO) {
			restore |= DOU_STATUS_DRIFTTO;
			evts |= KOMEDA_ERR_DRIFTTO;
		}
		if (status & DOU_STATUS_FRAMETO) {
			restore |= DOU_STATUS_FRAMETO;
			evts |= KOMEDA_ERR_FRAMETO;
		}
		if (status & DOU_STATUS_TETO) {
			restore |= DOU_STATUS_TETO;
			evts |= KOMEDA_ERR_TETO;
		}
		if (status & DOU_STATUS_CSCE) {
			restore |= DOU_STATUS_CSCE;
			evts |= KOMEDA_ERR_CSCE;
		}

		if (restore != 0)
			malidp_write32_mask(reg, BLK_STATUS, restore, 0);
	}

	malidp_write32(reg, BLK_IRQ_CLEAR, raw_status);
	return evts;
}

static u64 get_pipeline_event(struct d71_pipeline *d71_pipeline, u32 gcu_status)
{
	u32 evts = 0ULL;

	if (gcu_status & (GLB_IRQ_STATUS_LPU0 | GLB_IRQ_STATUS_LPU1))
		evts |= get_lpu_event(d71_pipeline);

	if (gcu_status & (GLB_IRQ_STATUS_CU0 | GLB_IRQ_STATUS_CU1))
		evts |= get_cu_event(d71_pipeline);

	if (gcu_status & (GLB_IRQ_STATUS_DOU0 | GLB_IRQ_STATUS_DOU1))
		evts |= get_dou_event(d71_pipeline);

	return evts;
}

static irqreturn_t
d71_irq_handler(struct komeda_dev *mdev, struct komeda_events *evts)
{
	struct d71_dev *d71 = mdev->chip_data;
	u32 status, gcu_status, raw_status;

	gcu_status = malidp_read32(d71->gcu_addr, GLB_IRQ_STATUS);

	if (gcu_status & GLB_IRQ_STATUS_GCU) {
		raw_status = malidp_read32(d71->gcu_addr, BLK_IRQ_RAW_STATUS);
		if (raw_status & GCU_IRQ_CVAL0)
			evts->pipes[0] |= KOMEDA_EVENT_FLIP;
		if (raw_status & GCU_IRQ_CVAL1)
			evts->pipes[1] |= KOMEDA_EVENT_FLIP;
		if (raw_status & GCU_IRQ_ERR) {
			status = malidp_read32(d71->gcu_addr, BLK_STATUS);
			if (status & GCU_STATUS_MERR) {
				evts->global |= KOMEDA_ERR_MERR;
				malidp_write32_mask(d71->gcu_addr, BLK_STATUS,
						    GCU_STATUS_MERR, 0);
			}
		}

		malidp_write32(d71->gcu_addr, BLK_IRQ_CLEAR, raw_status);
	}

	if (gcu_status & GLB_IRQ_STATUS_PIPE0)
		evts->pipes[0] |= get_pipeline_event(d71->pipes[0], gcu_status);

	if (gcu_status & GLB_IRQ_STATUS_PIPE1)
		evts->pipes[1] |= get_pipeline_event(d71->pipes[1], gcu_status);

	return IRQ_RETVAL(gcu_status);
}

#define ENABLED_GCU_IRQS	(GCU_IRQ_CVAL0 | GCU_IRQ_CVAL1 | \
				 GCU_IRQ_MODE | GCU_IRQ_ERR)
#define ENABLED_LPU_IRQS	(LPU_IRQ_IBSY | LPU_IRQ_ERR | LPU_IRQ_EOW)
#define ENABLED_CU_IRQS		(CU_IRQ_OVR | CU_IRQ_ERR)
#define ENABLED_DOU_IRQS	(DOU_IRQ_UND | DOU_IRQ_ERR)

static int d71_enable_irq(struct komeda_dev *mdev)
{
	struct d71_dev *d71 = mdev->chip_data;
	struct d71_pipeline *pipe;
	u32 i;

	malidp_write32_mask(d71->gcu_addr, BLK_IRQ_MASK,
			    ENABLED_GCU_IRQS, ENABLED_GCU_IRQS);
	for (i = 0; i < d71->num_pipelines; i++) {
		pipe = d71->pipes[i];
		malidp_write32_mask(pipe->cu_addr,  BLK_IRQ_MASK,
				    ENABLED_CU_IRQS, ENABLED_CU_IRQS);
		malidp_write32_mask(pipe->lpu_addr, BLK_IRQ_MASK,
				    ENABLED_LPU_IRQS, ENABLED_LPU_IRQS);
		malidp_write32_mask(pipe->dou_addr, BLK_IRQ_MASK,
				    ENABLED_DOU_IRQS, ENABLED_DOU_IRQS);
	}
	return 0;
}

static int d71_disable_irq(struct komeda_dev *mdev)
{
	struct d71_dev *d71 = mdev->chip_data;
	struct d71_pipeline *pipe;
	u32 i;

	malidp_write32_mask(d71->gcu_addr, BLK_IRQ_MASK, ENABLED_GCU_IRQS, 0);
	for (i = 0; i < d71->num_pipelines; i++) {
		pipe = d71->pipes[i];
		malidp_write32_mask(pipe->cu_addr,  BLK_IRQ_MASK,
				    ENABLED_CU_IRQS, 0);
		malidp_write32_mask(pipe->lpu_addr, BLK_IRQ_MASK,
				    ENABLED_LPU_IRQS, 0);
		malidp_write32_mask(pipe->dou_addr, BLK_IRQ_MASK,
				    ENABLED_DOU_IRQS, 0);
	}
	return 0;
}

static void d71_on_off_vblank(struct komeda_dev *mdev, int master_pipe, bool on)
{
	struct d71_dev *d71 = mdev->chip_data;
	struct d71_pipeline *pipe = d71->pipes[master_pipe];

	malidp_write32_mask(pipe->dou_addr, BLK_IRQ_MASK,
			    DOU_IRQ_PL0, on ? DOU_IRQ_PL0 : 0);
}

static int to_d71_opmode(int core_mode)
{
	switch (core_mode) {
	case KOMEDA_MODE_DISP0:
		return DO0_ACTIVE_MODE;
	case KOMEDA_MODE_DISP1:
		return DO1_ACTIVE_MODE;
	case KOMEDA_MODE_DUAL_DISP:
		return DO01_ACTIVE_MODE;
	case KOMEDA_MODE_INACTIVE:
		return INACTIVE_MODE;
	default:
		WARN(1, "Unknown operation mode");
		return INACTIVE_MODE;
	}
}

static int d71_change_opmode(struct komeda_dev *mdev, int new_mode)
{
	struct d71_dev *d71 = mdev->chip_data;
	u32 opmode = to_d71_opmode(new_mode);
	int ret;

	malidp_write32_mask(d71->gcu_addr, BLK_CONTROL, 0x7, opmode);

	ret = dp_wait_cond(((malidp_read32(d71->gcu_addr, BLK_CONTROL) & 0x7) == opmode),
			   100, 1000, 10000);

	return ret;
}

static void d71_flush(struct komeda_dev *mdev,
		      int master_pipe, u32 active_pipes)
{
	struct d71_dev *d71 = mdev->chip_data;
	u32 reg_offset = (master_pipe == 0) ?
			 GCU_CONFIG_VALID0 : GCU_CONFIG_VALID1;

	malidp_write32(d71->gcu_addr, reg_offset, GCU_CONFIG_CVAL);
}

static int d71_reset(struct d71_dev *d71)
{
	u32 __iomem *gcu = d71->gcu_addr;
	int ret;

	malidp_write32(gcu, BLK_CONTROL, GCU_CONTROL_SRST);

	ret = dp_wait_cond(!(malidp_read32(gcu, BLK_CONTROL) & GCU_CONTROL_SRST),
			   100, 1000, 10000);

	return ret;
}

void d71_read_block_header(u32 __iomem *reg, struct block_header *blk)
{
	int i;

	blk->block_info = malidp_read32(reg, BLK_BLOCK_INFO);
	if (BLOCK_INFO_BLK_TYPE(blk->block_info) == D71_BLK_TYPE_RESERVED)
		return;

	blk->pipeline_info = malidp_read32(reg, BLK_PIPELINE_INFO);

	/* get valid input and output ids */
	for (i = 0; i < PIPELINE_INFO_N_VALID_INPUTS(blk->pipeline_info); i++)
		blk->input_ids[i] = malidp_read32(reg + i, BLK_VALID_INPUT_ID0);
	for (i = 0; i < PIPELINE_INFO_N_OUTPUTS(blk->pipeline_info); i++)
		blk->output_ids[i] = malidp_read32(reg + i, BLK_OUTPUT_ID0);
}

static void d71_cleanup(struct komeda_dev *mdev)
{
	struct d71_dev *d71 = mdev->chip_data;

	if (!d71)
		return;

	devm_kfree(mdev->dev, d71);
	mdev->chip_data = NULL;
}

static int d71_enum_resources(struct komeda_dev *mdev)
{
	struct d71_dev *d71;
	struct komeda_pipeline *pipe;
	struct block_header blk;
	u32 __iomem *blk_base;
	u32 i, value, offset;
	int err;

	d71 = devm_kzalloc(mdev->dev, sizeof(*d71), GFP_KERNEL);
	if (!d71)
		return -ENOMEM;

	mdev->chip_data = d71;
	d71->mdev = mdev;
	d71->gcu_addr = mdev->reg_base;
	d71->periph_addr = mdev->reg_base + (D71_BLOCK_OFFSET_PERIPH >> 2);

	err = d71_reset(d71);
	if (err) {
		DRM_ERROR("Fail to reset d71 device.\n");
		goto err_cleanup;
	}

	/* probe GCU */
	value = malidp_read32(d71->gcu_addr, GLB_CORE_INFO);
	d71->num_blocks = value & 0xFF;
	d71->num_pipelines = (value >> 8) & 0x7;

	if (d71->num_pipelines > D71_MAX_PIPELINE) {
		DRM_ERROR("d71 supports %d pipelines, but got: %d.\n",
			  D71_MAX_PIPELINE, d71->num_pipelines);
		err = -EINVAL;
		goto err_cleanup;
	}

	/* Only the legacy HW has the periph block, the newer merges the periph
	 * into GCU
	 */
	value = malidp_read32(d71->periph_addr, BLK_BLOCK_INFO);
	if (BLOCK_INFO_BLK_TYPE(value) != D71_BLK_TYPE_PERIPH)
		d71->periph_addr = NULL;

	if (d71->periph_addr) {
		/* probe PERIPHERAL in legacy HW */
		value = malidp_read32(d71->periph_addr, PERIPH_CONFIGURATION_ID);

		d71->max_line_size	= value & PERIPH_MAX_LINE_SIZE ? 4096 : 2048;
		d71->max_vsize		= 4096;
		d71->num_rich_layers	= value & PERIPH_NUM_RICH_LAYERS ? 2 : 1;
		d71->supports_dual_link	= !!(value & PERIPH_SPLIT_EN);
		d71->integrates_tbu	= !!(value & PERIPH_TBU_EN);
	} else {
		value = malidp_read32(d71->gcu_addr, GCU_CONFIGURATION_ID0);
		d71->max_line_size	= GCU_MAX_LINE_SIZE(value);
		d71->max_vsize		= GCU_MAX_NUM_LINES(value);

		value = malidp_read32(d71->gcu_addr, GCU_CONFIGURATION_ID1);
		d71->num_rich_layers	= GCU_NUM_RICH_LAYERS(value);
		d71->supports_dual_link	= GCU_DISPLAY_SPLIT_EN(value);
		d71->integrates_tbu	= GCU_DISPLAY_TBU_EN(value);
	}

	for (i = 0; i < d71->num_pipelines; i++) {
		pipe = komeda_pipeline_add(mdev, sizeof(struct d71_pipeline),
					   &d71_pipeline_funcs);
		if (IS_ERR(pipe)) {
			err = PTR_ERR(pipe);
			goto err_cleanup;
		}

		/* D71 HW doesn't update shadow registers when display output
		 * is turning off, so when we disable all pipeline components
		 * together with display output disable by one flush or one
		 * operation, the disable operation updated registers will not
		 * be flush to or valid in HW, which may leads problem.
		 * To workaround this problem, introduce a two phase disable.
		 * Phase1: Disabling components with display is on to make sure
		 *	   the disable can be flushed to HW.
		 * Phase2: Only turn-off display output.
		 */
		value = KOMEDA_PIPELINE_IMPROCS |
			BIT(KOMEDA_COMPONENT_TIMING_CTRLR);

		pipe->standalone_disabled_comps = value;

		d71->pipes[i] = to_d71_pipeline(pipe);
	}

	/* loop the register blks and probe.
	 * NOTE: d71->num_blocks includes reserved blocks.
	 * d71->num_blocks = GCU + valid blocks + reserved blocks
	 */
	i = 1; /* exclude GCU */
	offset = D71_BLOCK_SIZE; /* skip GCU */
	while (i < d71->num_blocks) {
		blk_base = mdev->reg_base + (offset >> 2);

		d71_read_block_header(blk_base, &blk);
		if (BLOCK_INFO_BLK_TYPE(blk.block_info) != D71_BLK_TYPE_RESERVED) {
			err = d71_probe_block(d71, &blk, blk_base);
			if (err)
				goto err_cleanup;
		}

		i++;
		offset += D71_BLOCK_SIZE;
	}

	DRM_DEBUG("total %d (out of %d) blocks are found.\n",
		  i, d71->num_blocks);

	return 0;

err_cleanup:
	d71_cleanup(mdev);
	return err;
}

#define __HW_ID(__group, __format) \
	((((__group) & 0x7) << 3) | ((__format) & 0x7))

#define RICH		KOMEDA_FMT_RICH_LAYER
#define SIMPLE		KOMEDA_FMT_SIMPLE_LAYER
#define RICH_SIMPLE	(KOMEDA_FMT_RICH_LAYER | KOMEDA_FMT_SIMPLE_LAYER)
#define RICH_WB		(KOMEDA_FMT_RICH_LAYER | KOMEDA_FMT_WB_LAYER)
#define RICH_SIMPLE_WB	(RICH_SIMPLE | KOMEDA_FMT_WB_LAYER)

#define Rot_0		DRM_MODE_ROTATE_0
#define Flip_H_V	(DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y | Rot_0)
#define Rot_ALL_H_V	(DRM_MODE_ROTATE_MASK | Flip_H_V)

#define LYT_NM		BIT(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)
#define LYT_WB		BIT(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)
#define LYT_NM_WB	(LYT_NM | LYT_WB)

#define AFB_TH		AFBC(_TILED | _SPARSE)
#define AFB_TH_SC_YTR	AFBC(_TILED | _SC | _SPARSE | _YTR)
#define AFB_TH_SC_YTR_BS AFBC(_TILED | _SC | _SPARSE | _YTR | _SPLIT)

static struct komeda_format_caps d71_format_caps_table[] = {
	/*   HW_ID    |        fourcc         |   layer_types |   rots    | afbc_layouts | afbc_features */
	/* ABGR_2101010*/
	{__HW_ID(0, 0),	DRM_FORMAT_ARGB2101010,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(0, 1),	DRM_FORMAT_ABGR2101010,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(0, 1),	DRM_FORMAT_ABGR2101010,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR_BS}, /* afbc */
	{__HW_ID(0, 2),	DRM_FORMAT_RGBA1010102,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(0, 3),	DRM_FORMAT_BGRA1010102,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	/* ABGR_8888*/
	{__HW_ID(1, 0),	DRM_FORMAT_ARGB8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(1, 1),	DRM_FORMAT_ABGR8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(1, 1),	DRM_FORMAT_ABGR8888,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR_BS}, /* afbc */
	{__HW_ID(1, 2),	DRM_FORMAT_RGBA8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(1, 3),	DRM_FORMAT_BGRA8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	/* XBGB_8888 */
	{__HW_ID(2, 0),	DRM_FORMAT_XRGB8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(2, 1),	DRM_FORMAT_XBGR8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(2, 2),	DRM_FORMAT_RGBX8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	{__HW_ID(2, 3),	DRM_FORMAT_BGRX8888,	RICH_SIMPLE_WB,	Flip_H_V,		0, 0},
	/* BGR_888 */ /* none-afbc RGB888 doesn't support rotation and flip */
	{__HW_ID(3, 0),	DRM_FORMAT_RGB888,	RICH_SIMPLE_WB,	Rot_0,			0, 0},
	{__HW_ID(3, 1),	DRM_FORMAT_BGR888,	RICH_SIMPLE_WB,	Rot_0,			0, 0},
	{__HW_ID(3, 1),	DRM_FORMAT_BGR888,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR_BS}, /* afbc */
	/* BGR 16bpp */
	{__HW_ID(4, 0),	DRM_FORMAT_RGBA5551,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 1),	DRM_FORMAT_ABGR1555,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 1),	DRM_FORMAT_ABGR1555,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR}, /* afbc */
	{__HW_ID(4, 2),	DRM_FORMAT_RGB565,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 3),	DRM_FORMAT_BGR565,	RICH_SIMPLE,	Flip_H_V,		0, 0},
	{__HW_ID(4, 3),	DRM_FORMAT_BGR565,	RICH_SIMPLE,	Rot_ALL_H_V,	LYT_NM_WB, AFB_TH_SC_YTR}, /* afbc */
	{__HW_ID(4, 4), DRM_FORMAT_R8,		SIMPLE,		Rot_0,			0, 0},
	/* YUV 444/422/420 8bit  */
	{__HW_ID(5, 1),	DRM_FORMAT_YUYV,	RICH,		Rot_ALL_H_V,	LYT_NM, AFB_TH}, /* afbc */
	{__HW_ID(5, 2),	DRM_FORMAT_YUYV,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(5, 3),	DRM_FORMAT_UYVY,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(5, 6),	DRM_FORMAT_NV12,	RICH_WB,	Flip_H_V,		0, 0},
	{__HW_ID(5, 6),	DRM_FORMAT_YUV420_8BIT,	RICH,		Rot_ALL_H_V,	LYT_NM, AFB_TH}, /* afbc */
	{__HW_ID(5, 7),	DRM_FORMAT_YUV420,	RICH,		Flip_H_V,		0, 0},
	/* YUV 10bit*/
	{__HW_ID(6, 6),	DRM_FORMAT_X0L2,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(6, 7),	DRM_FORMAT_P010,	RICH,		Flip_H_V,		0, 0},
	{__HW_ID(6, 7),	DRM_FORMAT_YUV420_10BIT, RICH,		Rot_ALL_H_V,	LYT_NM, AFB_TH},
};

static bool d71_format_mod_supported(const struct komeda_format_caps *caps,
				     u32 layer_type, u64 modifier, u32 rot)
{
	uint64_t layout = modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK;

	if ((layout == AFBC_FORMAT_MOD_BLOCK_SIZE_32x8) &&
	    drm_rotation_90_or_270(rot)) {
		DRM_DEBUG_ATOMIC("D71 doesn't support ROT90 for WB-AFBC.\n");
		return false;
	}

	return true;
}

static void d71_init_fmt_tbl(struct komeda_dev *mdev)
{
	struct komeda_format_caps_table *table = &mdev->fmt_tbl;

	table->format_caps = d71_format_caps_table;
	table->format_mod_supported = d71_format_mod_supported;
	table->n_formats = ARRAY_SIZE(d71_format_caps_table);
}

static int d71_connect_iommu(struct komeda_dev *mdev)
{
	struct d71_dev *d71 = mdev->chip_data;
	u32 __iomem *reg = d71->gcu_addr;
	u32 check_bits = (d71->num_pipelines == 2) ?
			 GCU_STATUS_TCS0 | GCU_STATUS_TCS1 : GCU_STATUS_TCS0;
	int i, ret;

	if (!d71->integrates_tbu)
		return -1;

	malidp_write32_mask(reg, BLK_CONTROL, 0x7, TBU_CONNECT_MODE);

	ret = dp_wait_cond(has_bits(check_bits, malidp_read32(reg, BLK_STATUS)),
			100, 1000, 1000);
	if (ret < 0) {
		DRM_ERROR("timed out connecting to TCU!\n");
		malidp_write32_mask(reg, BLK_CONTROL, 0x7, INACTIVE_MODE);
		return ret;
	}

	for (i = 0; i < d71->num_pipelines; i++)
		malidp_write32_mask(d71->pipes[i]->lpu_addr, LPU_TBU_CONTROL,
				    LPU_TBU_CTRL_TLBPEN, LPU_TBU_CTRL_TLBPEN);
	return 0;
}

static int d71_disconnect_iommu(struct komeda_dev *mdev)
{
	struct d71_dev *d71 = mdev->chip_data;
	u32 __iomem *reg = d71->gcu_addr;
	u32 check_bits = (d71->num_pipelines == 2) ?
			 GCU_STATUS_TCS0 | GCU_STATUS_TCS1 : GCU_STATUS_TCS0;
	int ret;

	malidp_write32_mask(reg, BLK_CONTROL, 0x7, TBU_DISCONNECT_MODE);

	ret = dp_wait_cond(((malidp_read32(reg, BLK_STATUS) & check_bits) == 0),
			100, 1000, 1000);
	if (ret < 0) {
		DRM_ERROR("timed out disconnecting from TCU!\n");
		malidp_write32_mask(reg, BLK_CONTROL, 0x7, INACTIVE_MODE);
	}

	return ret;
}

static const struct komeda_dev_funcs d71_chip_funcs = {
	.init_format_table	= d71_init_fmt_tbl,
	.enum_resources		= d71_enum_resources,
	.cleanup		= d71_cleanup,
	.irq_handler		= d71_irq_handler,
	.enable_irq		= d71_enable_irq,
	.disable_irq		= d71_disable_irq,
	.on_off_vblank		= d71_on_off_vblank,
	.change_opmode		= d71_change_opmode,
	.flush			= d71_flush,
	.connect_iommu		= d71_connect_iommu,
	.disconnect_iommu	= d71_disconnect_iommu,
	.dump_register		= d71_dump,
};

const struct komeda_dev_funcs *
d71_identify(u32 __iomem *reg_base, struct komeda_chip_info *chip)
{
	const struct komeda_dev_funcs *funcs;
	u32 product_id;

	chip->core_id = malidp_read32(reg_base, GLB_CORE_ID);

	product_id = MALIDP_CORE_ID_PRODUCT_ID(chip->core_id);

	switch (product_id) {
	case MALIDP_D71_PRODUCT_ID:
	case MALIDP_D32_PRODUCT_ID:
		funcs = &d71_chip_funcs;
		break;
	default:
		DRM_ERROR("Unsupported product: 0x%x\n", product_id);
		return NULL;
	}

	chip->arch_id	= malidp_read32(reg_base, GLB_ARCH_ID);
	chip->core_info	= malidp_read32(reg_base, GLB_CORE_INFO);
	chip->bus_width	= D71_BUS_WIDTH_16_BYTES;

	return funcs;
}
