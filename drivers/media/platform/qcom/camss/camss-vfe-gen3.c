// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module gen3
 *
 * Copyright (c) 2024 Qualcomm Technologies, Inc.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "camss.h"
#include "camss-vfe.h"

#define IS_VFE_690(vfe) \
	    ((vfe->camss->res->version == CAMSS_8775P) \
	    || (vfe->camss->res->version == CAMSS_8300))

#define BUS_REG_BASE_690 \
	    (vfe_is_lite(vfe) ? 0x480 : 0x400)
#define BUS_REG_BASE_780 \
	    (vfe_is_lite(vfe) ? 0x200 : 0xC00)
#define BUS_REG_BASE \
	    (IS_VFE_690(vfe) ? BUS_REG_BASE_690 : BUS_REG_BASE_780)

#define VFE_TOP_CORE_CFG (0x24)
#define VFE_DISABLE_DSCALING_DS4  BIT(21)
#define VFE_DISABLE_DSCALING_DS16 BIT(22)

#define VFE_BUS_WM_TEST_BUS_CTRL_690 (BUS_REG_BASE + 0xFC)
#define VFE_BUS_WM_TEST_BUS_CTRL_780 (BUS_REG_BASE + 0xDC)
#define VFE_BUS_WM_TEST_BUS_CTRL \
	    (IS_VFE_690(vfe) ? VFE_BUS_WM_TEST_BUS_CTRL_690 \
	     : VFE_BUS_WM_TEST_BUS_CTRL_780)
/*
 * Bus client mapping:
 *
 * Full VFE:
 * VFE_690: 16 = RDI0, 17 = RDI1, 18 = RDI2
 * VFE_780: 23 = RDI0, 24 = RDI1, 25 = RDI2
 *
 * VFE LITE:
 * VFE_690 : 0 = RDI0, 1 = RDI1, 2 = RDI2, 3 = RDI3, 4 = RDI4, 5 = RDI5
 * VFE_780 : 0 = RDI0, 1 = RDI1, 2 = RDI2, 3 = RDI3, 4 = RDI4
 */
#define RDI_WM_690(n)	((vfe_is_lite(vfe) ? 0x0 : 0x10) + (n))
#define RDI_WM_780(n)	((vfe_is_lite(vfe) ? 0x0 : 0x17) + (n))
#define RDI_WM(n)	(IS_VFE_690(vfe) ? RDI_WM_690(n) : RDI_WM_780(n))

#define VFE_BUS_WM_CGC_OVERRIDE		(BUS_REG_BASE + 0x08)
#define		WM_CGC_OVERRIDE_ALL		(0x7FFFFFF)

#define VFE_BUS_WM_CFG(n)		(BUS_REG_BASE + 0x200 + (n) * 0x100)
#define		WM_CFG_EN			BIT(0)
#define		WM_VIR_FRM_EN			BIT(1)
#define		WM_CFG_MODE			BIT(16)
#define VFE_BUS_WM_IMAGE_ADDR(n)	(BUS_REG_BASE + 0x204 + (n) * 0x100)
#define VFE_BUS_WM_FRAME_INCR(n)	(BUS_REG_BASE + 0x208 + (n) * 0x100)
#define VFE_BUS_WM_IMAGE_CFG_0(n)	(BUS_REG_BASE + 0x20c + (n) * 0x100)
#define		WM_IMAGE_CFG_0_DEFAULT_WIDTH	(0xFFFF)
#define VFE_BUS_WM_IMAGE_CFG_2(n)	(BUS_REG_BASE + 0x214 + (n) * 0x100)
#define		WM_IMAGE_CFG_2_DEFAULT_STRIDE	(0xFFFF)
#define VFE_BUS_WM_PACKER_CFG(n)	(BUS_REG_BASE + 0x218 + (n) * 0x100)

#define VFE_BUS_WM_IRQ_SUBSAMPLE_PERIOD(n)	(BUS_REG_BASE + 0x230 + (n) * 0x100)
#define VFE_BUS_WM_IRQ_SUBSAMPLE_PATTERN(n)	(BUS_REG_BASE + 0x234 + (n) * 0x100)
#define VFE_BUS_WM_FRAMEDROP_PERIOD(n)		(BUS_REG_BASE + 0x238 + (n) * 0x100)
#define VFE_BUS_WM_FRAMEDROP_PATTERN(n)		(BUS_REG_BASE + 0x23c + (n) * 0x100)

#define VFE_BUS_WM_MMU_PREFETCH_CFG(n)		(BUS_REG_BASE + 0x260 + (n) * 0x100)
#define VFE_BUS_WM_MMU_PREFETCH_MAX_OFFSET(n)	(BUS_REG_BASE + 0x264 + (n) * 0x100)

static void vfe_wm_start(struct vfe_device *vfe, u8 wm, struct vfe_line *line)
{
	struct v4l2_pix_format_mplane *pix =
		&line->video_out.active_fmt.fmt.pix_mp;

	wm = RDI_WM(wm);

	/* no clock gating at bus input */
	writel(WM_CGC_OVERRIDE_ALL, vfe->base + VFE_BUS_WM_CGC_OVERRIDE);

	writel(0x0, vfe->base + VFE_BUS_WM_TEST_BUS_CTRL);

	if (IS_VFE_690(vfe))
		writel(ALIGN(pix->plane_fmt[0].bytesperline, 16) * pix->height,
		       vfe->base + VFE_BUS_WM_FRAME_INCR(wm));
	else
		writel(ALIGN(pix->plane_fmt[0].bytesperline, 16) * pix->height >> 8,
		       vfe->base + VFE_BUS_WM_FRAME_INCR(wm));

	writel((WM_IMAGE_CFG_0_DEFAULT_WIDTH & 0xFFFF),
	       vfe->base + VFE_BUS_WM_IMAGE_CFG_0(wm));
	writel(WM_IMAGE_CFG_2_DEFAULT_STRIDE,
	       vfe->base + VFE_BUS_WM_IMAGE_CFG_2(wm));
	writel(0, vfe->base + VFE_BUS_WM_PACKER_CFG(wm));

	/* TOP CORE CFG */
	if (IS_VFE_690(vfe))
		writel(VFE_DISABLE_DSCALING_DS4 | VFE_DISABLE_DSCALING_DS16,
			vfe->base + VFE_TOP_CORE_CFG);

	/* no dropped frames, one irq per frame */
	writel(0, vfe->base + VFE_BUS_WM_FRAMEDROP_PERIOD(wm));
	writel(1, vfe->base + VFE_BUS_WM_FRAMEDROP_PATTERN(wm));
	writel(0, vfe->base + VFE_BUS_WM_IRQ_SUBSAMPLE_PERIOD(wm));
	writel(1, vfe->base + VFE_BUS_WM_IRQ_SUBSAMPLE_PATTERN(wm));

	writel(1, vfe->base + VFE_BUS_WM_MMU_PREFETCH_CFG(wm));
	writel(0xFFFFFFFF, vfe->base + VFE_BUS_WM_MMU_PREFETCH_MAX_OFFSET(wm));

	writel(WM_CFG_EN | WM_CFG_MODE, vfe->base + VFE_BUS_WM_CFG(wm));
}

static void vfe_wm_stop(struct vfe_device *vfe, u8 wm)
{
	wm = RDI_WM(wm);
	writel(0, vfe->base + VFE_BUS_WM_CFG(wm));
}

static void vfe_wm_update(struct vfe_device *vfe, u8 wm, u32 addr,
			  struct vfe_line *line)
{
	wm = RDI_WM(wm);

	if (IS_VFE_690(vfe))
		writel(addr, vfe->base + VFE_BUS_WM_IMAGE_ADDR(wm));
	else
		writel((addr >> 8), vfe->base + VFE_BUS_WM_IMAGE_ADDR(wm));

	dev_dbg(vfe->camss->dev, "wm:%d, image buf addr:0x%x\n",
		wm, addr);
}

static void vfe_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_reg_update(vfe->camss, vfe->id, port_id, false);
}

static inline void vfe_reg_update_clear(struct vfe_device *vfe,
					enum vfe_line_id line_id)
{
	int port_id = line_id;

	camss_reg_update(vfe->camss, vfe->id, port_id, true);
}

static const struct camss_video_ops vfe_video_ops_gen3 = {
	.queue_buffer = vfe_queue_buffer_v2,
	.flush_buffers = vfe_flush_buffers,
};

static void vfe_subdev_init(struct device *dev, struct vfe_device *vfe)
{
	vfe->video_ops = vfe_video_ops_gen3;
}

static void vfe_global_reset(struct vfe_device *vfe)
{
	vfe_isr_reset_ack(vfe);
}

static irqreturn_t vfe_isr(int irq, void *dev)
{
	/* nop */
	return IRQ_HANDLED;
}

static int vfe_halt(struct vfe_device *vfe)
{
	/* rely on vfe_disable_output() to stop the VFE */
	return 0;
}

const struct vfe_hw_ops vfe_ops_gen3 = {
	.global_reset = vfe_global_reset,
	.hw_version = vfe_hw_version,
	.isr = vfe_isr,
	.pm_domain_off = vfe_pm_domain_off,
	.pm_domain_on = vfe_pm_domain_on,
	.reg_update = vfe_reg_update,
	.reg_update_clear = vfe_reg_update_clear,
	.subdev_init = vfe_subdev_init,
	.vfe_disable = vfe_disable,
	.vfe_enable = vfe_enable_v2,
	.vfe_halt = vfe_halt,
	.vfe_wm_start = vfe_wm_start,
	.vfe_wm_stop = vfe_wm_stop,
	.vfe_buf_done = vfe_buf_done,
	.vfe_wm_update = vfe_wm_update,
};
