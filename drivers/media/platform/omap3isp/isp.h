/*
 * isp.h
 *
 * TI OMAP3 ISP - Core
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef OMAP3_ISP_CORE_H
#define OMAP3_ISP_CORE_H

#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include "omap3isp.h"
#include "ispstat.h"
#include "ispccdc.h"
#include "ispreg.h"
#include "ispresizer.h"
#include "isppreview.h"
#include "ispcsiphy.h"
#include "ispcsi2.h"
#include "ispccp2.h"

#define ISP_TOK_TERM		0xFFFFFFFF	/*
						 * terminating token for ISP
						 * modules reg list
						 */
#define to_isp_device(ptr_module)				\
	container_of(ptr_module, struct isp_device, isp_##ptr_module)
#define to_device(ptr_module)						\
	(to_isp_device(ptr_module)->dev)

enum isp_mem_resources {
	OMAP3_ISP_IOMEM_MAIN,
	OMAP3_ISP_IOMEM_CCP2,
	OMAP3_ISP_IOMEM_CCDC,
	OMAP3_ISP_IOMEM_HIST,
	OMAP3_ISP_IOMEM_H3A,
	OMAP3_ISP_IOMEM_PREV,
	OMAP3_ISP_IOMEM_RESZ,
	OMAP3_ISP_IOMEM_SBL,
	OMAP3_ISP_IOMEM_CSI2A_REGS1,
	OMAP3_ISP_IOMEM_CSIPHY2,
	OMAP3_ISP_IOMEM_CSI2A_REGS2,
	OMAP3_ISP_IOMEM_CSI2C_REGS1,
	OMAP3_ISP_IOMEM_CSIPHY1,
	OMAP3_ISP_IOMEM_CSI2C_REGS2,
	OMAP3_ISP_IOMEM_LAST
};

enum isp_sbl_resource {
	OMAP3_ISP_SBL_CSI1_READ		= 0x1,
	OMAP3_ISP_SBL_CSI1_WRITE	= 0x2,
	OMAP3_ISP_SBL_CSI2A_WRITE	= 0x4,
	OMAP3_ISP_SBL_CSI2C_WRITE	= 0x8,
	OMAP3_ISP_SBL_CCDC_LSC_READ	= 0x10,
	OMAP3_ISP_SBL_CCDC_WRITE	= 0x20,
	OMAP3_ISP_SBL_PREVIEW_READ	= 0x40,
	OMAP3_ISP_SBL_PREVIEW_WRITE	= 0x80,
	OMAP3_ISP_SBL_RESIZER_READ	= 0x100,
	OMAP3_ISP_SBL_RESIZER_WRITE	= 0x200,
};

enum isp_subclk_resource {
	OMAP3_ISP_SUBCLK_CCDC		= (1 << 0),
	OMAP3_ISP_SUBCLK_AEWB		= (1 << 1),
	OMAP3_ISP_SUBCLK_AF		= (1 << 2),
	OMAP3_ISP_SUBCLK_HIST		= (1 << 3),
	OMAP3_ISP_SUBCLK_PREVIEW	= (1 << 4),
	OMAP3_ISP_SUBCLK_RESIZER	= (1 << 5),
};

/* ISP: OMAP 34xx ES 1.0 */
#define ISP_REVISION_1_0		0x10
/* ISP2: OMAP 34xx ES 2.0, 2.1 and 3.0 */
#define ISP_REVISION_2_0		0x20
/* ISP2P: OMAP 36xx */
#define ISP_REVISION_15_0		0xF0

#define ISP_PHY_TYPE_3430		0
#define ISP_PHY_TYPE_3630		1

struct regmap;

/*
 * struct isp_res_mapping - Map ISP io resources to ISP revision.
 * @isp_rev: ISP_REVISION_x_x
 * @offset: register offsets of various ISP sub-blocks
 * @phy_type: ISP_PHY_TYPE_{3430,3630}
 */
struct isp_res_mapping {
	u32 isp_rev;
	u32 offset[OMAP3_ISP_IOMEM_LAST];
	u32 phy_type;
};

/*
 * struct isp_reg - Structure for ISP register values.
 * @reg: 32-bit Register address.
 * @val: 32-bit Register value.
 */
struct isp_reg {
	enum isp_mem_resources mmio_range;
	u32 reg;
	u32 val;
};

enum isp_xclk_id {
	ISP_XCLK_A,
	ISP_XCLK_B,
};

struct isp_xclk {
	struct isp_device *isp;
	struct clk_hw hw;
	struct clk *clk;
	enum isp_xclk_id id;

	spinlock_t lock;	/* Protects enabled and divider */
	bool enabled;
	unsigned int divider;
};

/*
 * struct isp_device - ISP device structure.
 * @dev: Device pointer specific to the OMAP3 ISP.
 * @revision: Stores current ISP module revision.
 * @irq_num: Currently used IRQ number.
 * @mmio_base: Array with kernel base addresses for ioremapped ISP register
 *             regions.
 * @mmio_hist_base_phys: Physical L4 bus address for ISP hist block register
 *			 region.
 * @syscon: Regmap for the syscon register space
 * @syscon_offset: Offset of the CSIPHY control register in syscon
 * @phy_type: ISP_PHY_TYPE_{3430,3630}
 * @mapping: IOMMU mapping
 * @stat_lock: Spinlock for handling statistics
 * @isp_mutex: Mutex for serializing requests to ISP.
 * @stop_failure: Indicates that an entity failed to stop.
 * @crashed: Crashed ent_enum
 * @has_context: Context has been saved at least once and can be restored.
 * @ref_count: Reference count for handling multiple ISP requests.
 * @cam_ick: Pointer to camera interface clock structure.
 * @cam_mclk: Pointer to camera functional clock structure.
 * @csi2_fck: Pointer to camera CSI2 complexIO clock structure.
 * @l3_ick: Pointer to OMAP3 L3 bus interface clock.
 * @xclks: External clocks provided by the ISP
 * @irq: Currently attached ISP ISR callbacks information structure.
 * @isp_af: Pointer to current settings for ISP AutoFocus SCM.
 * @isp_hist: Pointer to current settings for ISP Histogram SCM.
 * @isp_h3a: Pointer to current settings for ISP Auto Exposure and
 *           White Balance SCM.
 * @isp_res: Pointer to current settings for ISP Resizer.
 * @isp_prev: Pointer to current settings for ISP Preview.
 * @isp_ccdc: Pointer to current settings for ISP CCDC.
 * @platform_cb: ISP driver callback function pointers for platform code
 *
 * This structure is used to store the OMAP ISP Information.
 */
struct isp_device {
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
	struct media_device media_dev;
	struct device *dev;
	u32 revision;

	/* platform HW resources */
	unsigned int irq_num;

	void __iomem *mmio_base[OMAP3_ISP_IOMEM_LAST];
	unsigned long mmio_hist_base_phys;
	struct regmap *syscon;
	u32 syscon_offset;
	u32 phy_type;

	struct dma_iommu_mapping *mapping;

	/* ISP Obj */
	spinlock_t stat_lock;	/* common lock for statistic drivers */
	struct mutex isp_mutex;	/* For handling ref_count field */
	bool stop_failure;
	struct media_entity_enum crashed;
	int has_context;
	int ref_count;
	unsigned int autoidle;
#define ISP_CLK_CAM_ICK		0
#define ISP_CLK_CAM_MCLK	1
#define ISP_CLK_CSI2_FCK	2
#define ISP_CLK_L3_ICK		3
	struct clk *clock[4];
	struct isp_xclk xclks[2];

	/* ISP modules */
	struct ispstat isp_af;
	struct ispstat isp_aewb;
	struct ispstat isp_hist;
	struct isp_res_device isp_res;
	struct isp_prev_device isp_prev;
	struct isp_ccdc_device isp_ccdc;
	struct isp_csi2_device isp_csi2a;
	struct isp_csi2_device isp_csi2c;
	struct isp_ccp2_device isp_ccp2;
	struct isp_csiphy isp_csiphy1;
	struct isp_csiphy isp_csiphy2;

	unsigned int sbl_resources;
	unsigned int subclk_resources;

#define ISP_MAX_SUBDEVS		8
	struct v4l2_subdev *subdevs[ISP_MAX_SUBDEVS];
};

struct isp_async_subdev {
	struct v4l2_subdev *sd;
	struct isp_bus_cfg bus;
	struct v4l2_async_subdev asd;
};

#define v4l2_dev_to_isp_device(dev) \
	container_of(dev, struct isp_device, v4l2_dev)

void omap3isp_hist_dma_done(struct isp_device *isp);

void omap3isp_flush(struct isp_device *isp);

int omap3isp_module_sync_idle(struct media_entity *me, wait_queue_head_t *wait,
			      atomic_t *stopping);

int omap3isp_module_sync_is_stopping(wait_queue_head_t *wait,
				     atomic_t *stopping);

int omap3isp_pipeline_set_stream(struct isp_pipeline *pipe,
				 enum isp_pipeline_stream_state state);
void omap3isp_pipeline_cancel_stream(struct isp_pipeline *pipe);
void omap3isp_configure_bridge(struct isp_device *isp,
			       enum ccdc_input_entity input,
			       const struct isp_parallel_cfg *buscfg,
			       unsigned int shift, unsigned int bridge);

struct isp_device *omap3isp_get(struct isp_device *isp);
void omap3isp_put(struct isp_device *isp);

void omap3isp_print_status(struct isp_device *isp);

void omap3isp_sbl_enable(struct isp_device *isp, enum isp_sbl_resource res);
void omap3isp_sbl_disable(struct isp_device *isp, enum isp_sbl_resource res);

void omap3isp_subclk_enable(struct isp_device *isp,
			    enum isp_subclk_resource res);
void omap3isp_subclk_disable(struct isp_device *isp,
			     enum isp_subclk_resource res);

int omap3isp_register_entities(struct platform_device *pdev,
			       struct v4l2_device *v4l2_dev);
void omap3isp_unregister_entities(struct platform_device *pdev);

/*
 * isp_reg_readl - Read value of an OMAP3 ISP register
 * @isp: Device pointer specific to the OMAP3 ISP.
 * @isp_mmio_range: Range to which the register offset refers to.
 * @reg_offset: Register offset to read from.
 *
 * Returns an unsigned 32 bit value with the required register contents.
 */
static inline
u32 isp_reg_readl(struct isp_device *isp, enum isp_mem_resources isp_mmio_range,
		  u32 reg_offset)
{
	return __raw_readl(isp->mmio_base[isp_mmio_range] + reg_offset);
}

/*
 * isp_reg_writel - Write value to an OMAP3 ISP register
 * @isp: Device pointer specific to the OMAP3 ISP.
 * @reg_value: 32 bit value to write to the register.
 * @isp_mmio_range: Range to which the register offset refers to.
 * @reg_offset: Register offset to write into.
 */
static inline
void isp_reg_writel(struct isp_device *isp, u32 reg_value,
		    enum isp_mem_resources isp_mmio_range, u32 reg_offset)
{
	__raw_writel(reg_value, isp->mmio_base[isp_mmio_range] + reg_offset);
}

/*
 * isp_reg_clr - Clear individual bits in an OMAP3 ISP register
 * @isp: Device pointer specific to the OMAP3 ISP.
 * @mmio_range: Range to which the register offset refers to.
 * @reg: Register offset to work on.
 * @clr_bits: 32 bit value which would be cleared in the register.
 */
static inline
void isp_reg_clr(struct isp_device *isp, enum isp_mem_resources mmio_range,
		 u32 reg, u32 clr_bits)
{
	u32 v = isp_reg_readl(isp, mmio_range, reg);

	isp_reg_writel(isp, v & ~clr_bits, mmio_range, reg);
}

/*
 * isp_reg_set - Set individual bits in an OMAP3 ISP register
 * @isp: Device pointer specific to the OMAP3 ISP.
 * @mmio_range: Range to which the register offset refers to.
 * @reg: Register offset to work on.
 * @set_bits: 32 bit value which would be set in the register.
 */
static inline
void isp_reg_set(struct isp_device *isp, enum isp_mem_resources mmio_range,
		 u32 reg, u32 set_bits)
{
	u32 v = isp_reg_readl(isp, mmio_range, reg);

	isp_reg_writel(isp, v | set_bits, mmio_range, reg);
}

/*
 * isp_reg_clr_set - Clear and set invidial bits in an OMAP3 ISP register
 * @isp: Device pointer specific to the OMAP3 ISP.
 * @mmio_range: Range to which the register offset refers to.
 * @reg: Register offset to work on.
 * @clr_bits: 32 bit value which would be cleared in the register.
 * @set_bits: 32 bit value which would be set in the register.
 *
 * The clear operation is done first, and then the set operation.
 */
static inline
void isp_reg_clr_set(struct isp_device *isp, enum isp_mem_resources mmio_range,
		     u32 reg, u32 clr_bits, u32 set_bits)
{
	u32 v = isp_reg_readl(isp, mmio_range, reg);

	isp_reg_writel(isp, (v & ~clr_bits) | set_bits, mmio_range, reg);
}

static inline enum v4l2_buf_type
isp_pad_buffer_type(const struct v4l2_subdev *subdev, int pad)
{
	if (pad >= subdev->entity.num_pads)
		return 0;

	if (subdev->entity.pads[pad].flags & MEDIA_PAD_FL_SINK)
		return V4L2_BUF_TYPE_VIDEO_OUTPUT;
	else
		return V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

#endif	/* OMAP3_ISP_CORE_H */
