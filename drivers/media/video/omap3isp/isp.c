/*
 * isp.c
 *
 * TI OMAP3 ISP - Core
 *
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (C) 2007-2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * Contributors:
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	Sakari Ailus <sakari.ailus@iki.fi>
 *	David Cohen <dacohen@gmail.com>
 *	Stanimir Varbanov <svarbanov@mm-sol.com>
 *	Vimarsh Zutshi <vimarsh.zutshi@gmail.com>
 *	Tuukka Toivonen <tuukkat76@gmail.com>
 *	Sergio Aguirre <saaguirre@ti.com>
 *	Antti Koskipaa <akoskipa@gmail.com>
 *	Ivan T. Ivanov <iivanov@mm-sol.com>
 *	RaniSuneela <r-m@ti.com>
 *	Atanas Filipov <afilipov@mm-sol.com>
 *	Gjorgji Rosikopulos <grosikopulos@mm-sol.com>
 *	Hiroshi DOYU <hiroshi.doyu@nokia.com>
 *	Nayden Kanchev <nkanchev@mm-sol.com>
 *	Phil Carmody <ext-phil.2.carmody@nokia.com>
 *	Artem Bityutskiy <artem.bityutskiy@nokia.com>
 *	Dominic Curran <dcurran@ti.com>
 *	Ilkka Myllyperkio <ilkka.myllyperkio@sofica.fi>
 *	Pallavi Kulkarni <p-kulkarni@ti.com>
 *	Vaibhav Hiremath <hvaibhav@ti.com>
 *	Mohit Jalori <mjalori@ti.com>
 *	Sameer Venkatraman <sameerv@ti.com>
 *	Senthilvadivu Guruswamy <svadivu@ti.com>
 *	Thara Gopinath <thara@ti.com>
 *	Toni Leinonen <toni.leinonen@nokia.com>
 *	Troy Laramy <t-laramy@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <asm/cacheflush.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>

#include "isp.h"
#include "ispreg.h"
#include "ispccdc.h"
#include "isppreview.h"
#include "ispresizer.h"
#include "ispcsi2.h"
#include "ispccp2.h"
#include "isph3a.h"
#include "isphist.h"

static unsigned int autoidle;
module_param(autoidle, int, 0444);
MODULE_PARM_DESC(autoidle, "Enable OMAP3ISP AUTOIDLE support");

static void isp_save_ctx(struct isp_device *isp);

static void isp_restore_ctx(struct isp_device *isp);

static const struct isp_res_mapping isp_res_maps[] = {
	{
		.isp_rev = ISP_REVISION_2_0,
		.map = 1 << OMAP3_ISP_IOMEM_MAIN |
		       1 << OMAP3_ISP_IOMEM_CCP2 |
		       1 << OMAP3_ISP_IOMEM_CCDC |
		       1 << OMAP3_ISP_IOMEM_HIST |
		       1 << OMAP3_ISP_IOMEM_H3A |
		       1 << OMAP3_ISP_IOMEM_PREV |
		       1 << OMAP3_ISP_IOMEM_RESZ |
		       1 << OMAP3_ISP_IOMEM_SBL |
		       1 << OMAP3_ISP_IOMEM_CSI2A_REGS1 |
		       1 << OMAP3_ISP_IOMEM_CSIPHY2,
	},
	{
		.isp_rev = ISP_REVISION_15_0,
		.map = 1 << OMAP3_ISP_IOMEM_MAIN |
		       1 << OMAP3_ISP_IOMEM_CCP2 |
		       1 << OMAP3_ISP_IOMEM_CCDC |
		       1 << OMAP3_ISP_IOMEM_HIST |
		       1 << OMAP3_ISP_IOMEM_H3A |
		       1 << OMAP3_ISP_IOMEM_PREV |
		       1 << OMAP3_ISP_IOMEM_RESZ |
		       1 << OMAP3_ISP_IOMEM_SBL |
		       1 << OMAP3_ISP_IOMEM_CSI2A_REGS1 |
		       1 << OMAP3_ISP_IOMEM_CSIPHY2 |
		       1 << OMAP3_ISP_IOMEM_CSI2A_REGS2 |
		       1 << OMAP3_ISP_IOMEM_CSI2C_REGS1 |
		       1 << OMAP3_ISP_IOMEM_CSIPHY1 |
		       1 << OMAP3_ISP_IOMEM_CSI2C_REGS2,
	},
};

/* Structure for saving/restoring ISP module registers */
static struct isp_reg isp_reg_list[] = {
	{OMAP3_ISP_IOMEM_MAIN, ISP_SYSCONFIG, 0},
	{OMAP3_ISP_IOMEM_MAIN, ISP_CTRL, 0},
	{OMAP3_ISP_IOMEM_MAIN, ISP_TCTRL_CTRL, 0},
	{0, ISP_TOK_TERM, 0}
};

/*
 * omap3isp_flush - Post pending L3 bus writes by doing a register readback
 * @isp: OMAP3 ISP device
 *
 * In order to force posting of pending writes, we need to write and
 * readback the same register, in this case the revision register.
 *
 * See this link for reference:
 *   http://www.mail-archive.com/linux-omap@vger.kernel.org/msg08149.html
 */
void omap3isp_flush(struct isp_device *isp)
{
	isp_reg_writel(isp, 0, OMAP3_ISP_IOMEM_MAIN, ISP_REVISION);
	isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN, ISP_REVISION);
}

/*
 * isp_enable_interrupts - Enable ISP interrupts.
 * @isp: OMAP3 ISP device
 */
static void isp_enable_interrupts(struct isp_device *isp)
{
	static const u32 irq = IRQ0ENABLE_CSIA_IRQ
			     | IRQ0ENABLE_CSIB_IRQ
			     | IRQ0ENABLE_CCDC_LSC_PREF_ERR_IRQ
			     | IRQ0ENABLE_CCDC_LSC_DONE_IRQ
			     | IRQ0ENABLE_CCDC_VD0_IRQ
			     | IRQ0ENABLE_CCDC_VD1_IRQ
			     | IRQ0ENABLE_HS_VS_IRQ
			     | IRQ0ENABLE_HIST_DONE_IRQ
			     | IRQ0ENABLE_H3A_AWB_DONE_IRQ
			     | IRQ0ENABLE_H3A_AF_DONE_IRQ
			     | IRQ0ENABLE_PRV_DONE_IRQ
			     | IRQ0ENABLE_RSZ_DONE_IRQ;

	isp_reg_writel(isp, irq, OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS);
	isp_reg_writel(isp, irq, OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE);
}

/*
 * isp_disable_interrupts - Disable ISP interrupts.
 * @isp: OMAP3 ISP device
 */
static void isp_disable_interrupts(struct isp_device *isp)
{
	isp_reg_writel(isp, 0, OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE);
}

/**
 * isp_set_xclk - Configures the specified cam_xclk to the desired frequency.
 * @isp: OMAP3 ISP device
 * @xclk: Desired frequency of the clock in Hz. 0 = stable low, 1 is stable high
 * @xclksel: XCLK to configure (0 = A, 1 = B).
 *
 * Configures the specified MCLK divisor in the ISP timing control register
 * (TCTRL_CTRL) to generate the desired xclk clock value.
 *
 * Divisor = cam_mclk_hz / xclk
 *
 * Returns the final frequency that is actually being generated
 **/
static u32 isp_set_xclk(struct isp_device *isp, u32 xclk, u8 xclksel)
{
	u32 divisor;
	u32 currentxclk;
	unsigned long mclk_hz;

	if (!omap3isp_get(isp))
		return 0;

	mclk_hz = clk_get_rate(isp->clock[ISP_CLK_CAM_MCLK]);

	if (xclk >= mclk_hz) {
		divisor = ISPTCTRL_CTRL_DIV_BYPASS;
		currentxclk = mclk_hz;
	} else if (xclk >= 2) {
		divisor = mclk_hz / xclk;
		if (divisor >= ISPTCTRL_CTRL_DIV_BYPASS)
			divisor = ISPTCTRL_CTRL_DIV_BYPASS - 1;
		currentxclk = mclk_hz / divisor;
	} else {
		divisor = xclk;
		currentxclk = 0;
	}

	switch (xclksel) {
	case ISP_XCLK_A:
		isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_MAIN, ISP_TCTRL_CTRL,
				ISPTCTRL_CTRL_DIVA_MASK,
				divisor << ISPTCTRL_CTRL_DIVA_SHIFT);
		dev_dbg(isp->dev, "isp_set_xclk(): cam_xclka set to %d Hz\n",
			currentxclk);
		break;
	case ISP_XCLK_B:
		isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_MAIN, ISP_TCTRL_CTRL,
				ISPTCTRL_CTRL_DIVB_MASK,
				divisor << ISPTCTRL_CTRL_DIVB_SHIFT);
		dev_dbg(isp->dev, "isp_set_xclk(): cam_xclkb set to %d Hz\n",
			currentxclk);
		break;
	case ISP_XCLK_NONE:
	default:
		omap3isp_put(isp);
		dev_dbg(isp->dev, "ISP_ERR: isp_set_xclk(): Invalid requested "
			"xclk. Must be 0 (A) or 1 (B).\n");
		return -EINVAL;
	}

	/* Do we go from stable whatever to clock? */
	if (divisor >= 2 && isp->xclk_divisor[xclksel - 1] < 2)
		omap3isp_get(isp);
	/* Stopping the clock. */
	else if (divisor < 2 && isp->xclk_divisor[xclksel - 1] >= 2)
		omap3isp_put(isp);

	isp->xclk_divisor[xclksel - 1] = divisor;

	omap3isp_put(isp);

	return currentxclk;
}

/*
 * isp_power_settings - Sysconfig settings, for Power Management.
 * @isp: OMAP3 ISP device
 * @idle: Consider idle state.
 *
 * Sets the power settings for the ISP, and SBL bus.
 */
static void isp_power_settings(struct isp_device *isp, int idle)
{
	isp_reg_writel(isp,
		       ((idle ? ISP_SYSCONFIG_MIDLEMODE_SMARTSTANDBY :
				ISP_SYSCONFIG_MIDLEMODE_FORCESTANDBY) <<
			ISP_SYSCONFIG_MIDLEMODE_SHIFT) |
			((isp->revision == ISP_REVISION_15_0) ?
			  ISP_SYSCONFIG_AUTOIDLE : 0),
		       OMAP3_ISP_IOMEM_MAIN, ISP_SYSCONFIG);

	if (isp->autoidle)
		isp_reg_writel(isp, ISPCTRL_SBL_AUTOIDLE, OMAP3_ISP_IOMEM_MAIN,
			       ISP_CTRL);
}

/*
 * Configure the bridge and lane shifter. Valid inputs are
 *
 * CCDC_INPUT_PARALLEL: Parallel interface
 * CCDC_INPUT_CSI2A: CSI2a receiver
 * CCDC_INPUT_CCP2B: CCP2b receiver
 * CCDC_INPUT_CSI2C: CSI2c receiver
 *
 * The bridge and lane shifter are configured according to the selected input
 * and the ISP platform data.
 */
void omap3isp_configure_bridge(struct isp_device *isp,
			       enum ccdc_input_entity input,
			       const struct isp_parallel_platform_data *pdata,
			       unsigned int shift)
{
	u32 ispctrl_val;

	ispctrl_val  = isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);
	ispctrl_val &= ~ISPCTRL_SHIFT_MASK;
	ispctrl_val &= ~ISPCTRL_PAR_CLK_POL_INV;
	ispctrl_val &= ~ISPCTRL_PAR_SER_CLK_SEL_MASK;
	ispctrl_val &= ~ISPCTRL_PAR_BRIDGE_MASK;

	switch (input) {
	case CCDC_INPUT_PARALLEL:
		ispctrl_val |= ISPCTRL_PAR_SER_CLK_SEL_PARALLEL;
		ispctrl_val |= pdata->clk_pol << ISPCTRL_PAR_CLK_POL_SHIFT;
		ispctrl_val |= pdata->bridge << ISPCTRL_PAR_BRIDGE_SHIFT;
		shift += pdata->data_lane_shift * 2;
		break;

	case CCDC_INPUT_CSI2A:
		ispctrl_val |= ISPCTRL_PAR_SER_CLK_SEL_CSIA;
		break;

	case CCDC_INPUT_CCP2B:
		ispctrl_val |= ISPCTRL_PAR_SER_CLK_SEL_CSIB;
		break;

	case CCDC_INPUT_CSI2C:
		ispctrl_val |= ISPCTRL_PAR_SER_CLK_SEL_CSIC;
		break;

	default:
		return;
	}

	ispctrl_val |= ((shift/2) << ISPCTRL_SHIFT_SHIFT) & ISPCTRL_SHIFT_MASK;

	ispctrl_val &= ~ISPCTRL_SYNC_DETECT_MASK;
	ispctrl_val |= ISPCTRL_SYNC_DETECT_VSRISE;

	isp_reg_writel(isp, ispctrl_val, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);
}

/**
 * isp_set_pixel_clock - Configures the ISP pixel clock
 * @isp: OMAP3 ISP device
 * @pixelclk: Average pixel clock in Hz
 *
 * Set the average pixel clock required by the sensor. The ISP will use the
 * lowest possible memory bandwidth settings compatible with the clock.
 **/
static void isp_set_pixel_clock(struct isp_device *isp, unsigned int pixelclk)
{
	isp->isp_ccdc.vpcfg.pixelclk = pixelclk;
}

void omap3isp_hist_dma_done(struct isp_device *isp)
{
	if (omap3isp_ccdc_busy(&isp->isp_ccdc) ||
	    omap3isp_stat_pcr_busy(&isp->isp_hist)) {
		/* Histogram cannot be enabled in this frame anymore */
		atomic_set(&isp->isp_hist.buf_err, 1);
		dev_dbg(isp->dev, "hist: Out of synchronization with "
				  "CCDC. Ignoring next buffer.\n");
	}
}

static inline void isp_isr_dbg(struct isp_device *isp, u32 irqstatus)
{
	static const char *name[] = {
		"CSIA_IRQ",
		"res1",
		"res2",
		"CSIB_LCM_IRQ",
		"CSIB_IRQ",
		"res5",
		"res6",
		"res7",
		"CCDC_VD0_IRQ",
		"CCDC_VD1_IRQ",
		"CCDC_VD2_IRQ",
		"CCDC_ERR_IRQ",
		"H3A_AF_DONE_IRQ",
		"H3A_AWB_DONE_IRQ",
		"res14",
		"res15",
		"HIST_DONE_IRQ",
		"CCDC_LSC_DONE",
		"CCDC_LSC_PREFETCH_COMPLETED",
		"CCDC_LSC_PREFETCH_ERROR",
		"PRV_DONE_IRQ",
		"CBUFF_IRQ",
		"res22",
		"res23",
		"RSZ_DONE_IRQ",
		"OVF_IRQ",
		"res26",
		"res27",
		"MMU_ERR_IRQ",
		"OCP_ERR_IRQ",
		"SEC_ERR_IRQ",
		"HS_VS_IRQ",
	};
	int i;

	dev_dbg(isp->dev, "ISP IRQ: ");

	for (i = 0; i < ARRAY_SIZE(name); i++) {
		if ((1 << i) & irqstatus)
			printk(KERN_CONT "%s ", name[i]);
	}
	printk(KERN_CONT "\n");
}

static void isp_isr_sbl(struct isp_device *isp)
{
	struct device *dev = isp->dev;
	struct isp_pipeline *pipe;
	u32 sbl_pcr;

	/*
	 * Handle shared buffer logic overflows for video buffers.
	 * ISPSBL_PCR_CCDCPRV_2_RSZ_OVF can be safely ignored.
	 */
	sbl_pcr = isp_reg_readl(isp, OMAP3_ISP_IOMEM_SBL, ISPSBL_PCR);
	isp_reg_writel(isp, sbl_pcr, OMAP3_ISP_IOMEM_SBL, ISPSBL_PCR);
	sbl_pcr &= ~ISPSBL_PCR_CCDCPRV_2_RSZ_OVF;

	if (sbl_pcr)
		dev_dbg(dev, "SBL overflow (PCR = 0x%08x)\n", sbl_pcr);

	if (sbl_pcr & ISPSBL_PCR_CSIB_WBL_OVF) {
		pipe = to_isp_pipeline(&isp->isp_ccp2.subdev.entity);
		if (pipe != NULL)
			pipe->error = true;
	}

	if (sbl_pcr & ISPSBL_PCR_CSIA_WBL_OVF) {
		pipe = to_isp_pipeline(&isp->isp_csi2a.subdev.entity);
		if (pipe != NULL)
			pipe->error = true;
	}

	if (sbl_pcr & ISPSBL_PCR_CCDC_WBL_OVF) {
		pipe = to_isp_pipeline(&isp->isp_ccdc.subdev.entity);
		if (pipe != NULL)
			pipe->error = true;
	}

	if (sbl_pcr & ISPSBL_PCR_PRV_WBL_OVF) {
		pipe = to_isp_pipeline(&isp->isp_prev.subdev.entity);
		if (pipe != NULL)
			pipe->error = true;
	}

	if (sbl_pcr & (ISPSBL_PCR_RSZ1_WBL_OVF
		       | ISPSBL_PCR_RSZ2_WBL_OVF
		       | ISPSBL_PCR_RSZ3_WBL_OVF
		       | ISPSBL_PCR_RSZ4_WBL_OVF)) {
		pipe = to_isp_pipeline(&isp->isp_res.subdev.entity);
		if (pipe != NULL)
			pipe->error = true;
	}

	if (sbl_pcr & ISPSBL_PCR_H3A_AF_WBL_OVF)
		omap3isp_stat_sbl_overflow(&isp->isp_af);

	if (sbl_pcr & ISPSBL_PCR_H3A_AEAWB_WBL_OVF)
		omap3isp_stat_sbl_overflow(&isp->isp_aewb);
}

/*
 * isp_isr - Interrupt Service Routine for Camera ISP module.
 * @irq: Not used currently.
 * @_isp: Pointer to the OMAP3 ISP device
 *
 * Handles the corresponding callback if plugged in.
 *
 * Returns IRQ_HANDLED when IRQ was correctly handled, or IRQ_NONE when the
 * IRQ wasn't handled.
 */
static irqreturn_t isp_isr(int irq, void *_isp)
{
	static const u32 ccdc_events = IRQ0STATUS_CCDC_LSC_PREF_ERR_IRQ |
				       IRQ0STATUS_CCDC_LSC_DONE_IRQ |
				       IRQ0STATUS_CCDC_VD0_IRQ |
				       IRQ0STATUS_CCDC_VD1_IRQ |
				       IRQ0STATUS_HS_VS_IRQ;
	struct isp_device *isp = _isp;
	u32 irqstatus;

	irqstatus = isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS);
	isp_reg_writel(isp, irqstatus, OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS);

	isp_isr_sbl(isp);

	if (irqstatus & IRQ0STATUS_CSIA_IRQ)
		omap3isp_csi2_isr(&isp->isp_csi2a);

	if (irqstatus & IRQ0STATUS_CSIB_IRQ)
		omap3isp_ccp2_isr(&isp->isp_ccp2);

	if (irqstatus & IRQ0STATUS_CCDC_VD0_IRQ) {
		if (isp->isp_ccdc.output & CCDC_OUTPUT_PREVIEW)
			omap3isp_preview_isr_frame_sync(&isp->isp_prev);
		if (isp->isp_ccdc.output & CCDC_OUTPUT_RESIZER)
			omap3isp_resizer_isr_frame_sync(&isp->isp_res);
		omap3isp_stat_isr_frame_sync(&isp->isp_aewb);
		omap3isp_stat_isr_frame_sync(&isp->isp_af);
		omap3isp_stat_isr_frame_sync(&isp->isp_hist);
	}

	if (irqstatus & ccdc_events)
		omap3isp_ccdc_isr(&isp->isp_ccdc, irqstatus & ccdc_events);

	if (irqstatus & IRQ0STATUS_PRV_DONE_IRQ) {
		if (isp->isp_prev.output & PREVIEW_OUTPUT_RESIZER)
			omap3isp_resizer_isr_frame_sync(&isp->isp_res);
		omap3isp_preview_isr(&isp->isp_prev);
	}

	if (irqstatus & IRQ0STATUS_RSZ_DONE_IRQ)
		omap3isp_resizer_isr(&isp->isp_res);

	if (irqstatus & IRQ0STATUS_H3A_AWB_DONE_IRQ)
		omap3isp_stat_isr(&isp->isp_aewb);

	if (irqstatus & IRQ0STATUS_H3A_AF_DONE_IRQ)
		omap3isp_stat_isr(&isp->isp_af);

	if (irqstatus & IRQ0STATUS_HIST_DONE_IRQ)
		omap3isp_stat_isr(&isp->isp_hist);

	omap3isp_flush(isp);

#if defined(DEBUG) && defined(ISP_ISR_DEBUG)
	isp_isr_dbg(isp, irqstatus);
#endif

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * Pipeline power management
 *
 * Entities must be powered up when part of a pipeline that contains at least
 * one open video device node.
 *
 * To achieve this use the entity use_count field to track the number of users.
 * For entities corresponding to video device nodes the use_count field stores
 * the users count of the node. For entities corresponding to subdevs the
 * use_count field stores the total number of users of all video device nodes
 * in the pipeline.
 *
 * The omap3isp_pipeline_pm_use() function must be called in the open() and
 * close() handlers of video device nodes. It increments or decrements the use
 * count of all subdev entities in the pipeline.
 *
 * To react to link management on powered pipelines, the link setup notification
 * callback updates the use count of all entities in the source and sink sides
 * of the link.
 */

/*
 * isp_pipeline_pm_use_count - Count the number of users of a pipeline
 * @entity: The entity
 *
 * Return the total number of users of all video device nodes in the pipeline.
 */
static int isp_pipeline_pm_use_count(struct media_entity *entity)
{
	struct media_entity_graph graph;
	int use = 0;

	media_entity_graph_walk_start(&graph, entity);

	while ((entity = media_entity_graph_walk_next(&graph))) {
		if (media_entity_type(entity) == MEDIA_ENT_T_DEVNODE)
			use += entity->use_count;
	}

	return use;
}

/*
 * isp_pipeline_pm_power_one - Apply power change to an entity
 * @entity: The entity
 * @change: Use count change
 *
 * Change the entity use count by @change. If the entity is a subdev update its
 * power state by calling the core::s_power operation when the use count goes
 * from 0 to != 0 or from != 0 to 0.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int isp_pipeline_pm_power_one(struct media_entity *entity, int change)
{
	struct v4l2_subdev *subdev;
	int ret;

	subdev = media_entity_type(entity) == MEDIA_ENT_T_V4L2_SUBDEV
	       ? media_entity_to_v4l2_subdev(entity) : NULL;

	if (entity->use_count == 0 && change > 0 && subdev != NULL) {
		ret = v4l2_subdev_call(subdev, core, s_power, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	if (entity->use_count == 0 && change < 0 && subdev != NULL)
		v4l2_subdev_call(subdev, core, s_power, 0);

	return 0;
}

/*
 * isp_pipeline_pm_power - Apply power change to all entities in a pipeline
 * @entity: The entity
 * @change: Use count change
 *
 * Walk the pipeline to update the use count and the power state of all non-node
 * entities.
 *
 * Return 0 on success or a negative error code on failure.
 */
static int isp_pipeline_pm_power(struct media_entity *entity, int change)
{
	struct media_entity_graph graph;
	struct media_entity *first = entity;
	int ret = 0;

	if (!change)
		return 0;

	media_entity_graph_walk_start(&graph, entity);

	while (!ret && (entity = media_entity_graph_walk_next(&graph)))
		if (media_entity_type(entity) != MEDIA_ENT_T_DEVNODE)
			ret = isp_pipeline_pm_power_one(entity, change);

	if (!ret)
		return 0;

	media_entity_graph_walk_start(&graph, first);

	while ((first = media_entity_graph_walk_next(&graph))
	       && first != entity)
		if (media_entity_type(first) != MEDIA_ENT_T_DEVNODE)
			isp_pipeline_pm_power_one(first, -change);

	return ret;
}

/*
 * omap3isp_pipeline_pm_use - Update the use count of an entity
 * @entity: The entity
 * @use: Use (1) or stop using (0) the entity
 *
 * Update the use count of all entities in the pipeline and power entities on or
 * off accordingly.
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. No failure can occur when the use parameter is
 * set to 0.
 */
int omap3isp_pipeline_pm_use(struct media_entity *entity, int use)
{
	int change = use ? 1 : -1;
	int ret;

	mutex_lock(&entity->parent->graph_mutex);

	/* Apply use count to node. */
	entity->use_count += change;
	WARN_ON(entity->use_count < 0);

	/* Apply power change to connected non-nodes. */
	ret = isp_pipeline_pm_power(entity, change);
	if (ret < 0)
		entity->use_count -= change;

	mutex_unlock(&entity->parent->graph_mutex);

	return ret;
}

/*
 * isp_pipeline_link_notify - Link management notification callback
 * @source: Pad at the start of the link
 * @sink: Pad at the end of the link
 * @flags: New link flags that will be applied
 *
 * React to link management on powered pipelines by updating the use count of
 * all entities in the source and sink sides of the link. Entities are powered
 * on or off accordingly.
 *
 * Return 0 on success or a negative error code on failure. Powering entities
 * off is assumed to never fail. This function will not fail for disconnection
 * events.
 */
static int isp_pipeline_link_notify(struct media_pad *source,
				    struct media_pad *sink, u32 flags)
{
	int source_use = isp_pipeline_pm_use_count(source->entity);
	int sink_use = isp_pipeline_pm_use_count(sink->entity);
	int ret;

	if (!(flags & MEDIA_LNK_FL_ENABLED)) {
		/* Powering off entities is assumed to never fail. */
		isp_pipeline_pm_power(source->entity, -sink_use);
		isp_pipeline_pm_power(sink->entity, -source_use);
		return 0;
	}

	ret = isp_pipeline_pm_power(source->entity, sink_use);
	if (ret < 0)
		return ret;

	ret = isp_pipeline_pm_power(sink->entity, source_use);
	if (ret < 0)
		isp_pipeline_pm_power(source->entity, -sink_use);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Pipeline stream management
 */

/*
 * isp_pipeline_enable - Enable streaming on a pipeline
 * @pipe: ISP pipeline
 * @mode: Stream mode (single shot or continuous)
 *
 * Walk the entities chain starting at the pipeline output video node and start
 * all modules in the chain in the given mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise.
 */
static int isp_pipeline_enable(struct isp_pipeline *pipe,
			       enum isp_pipeline_stream_state mode)
{
	struct isp_device *isp = pipe->output->isp;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	unsigned long flags;
	int ret;

	/* If the preview engine crashed it might not respond to read/write
	 * operations on the L4 bus. This would result in a bus fault and a
	 * kernel oops. Refuse to start streaming in that case. This check must
	 * be performed before the loop below to avoid starting entities if the
	 * pipeline won't start anyway (those entities would then likely fail to
	 * stop, making the problem worse).
	 */
	if ((pipe->entities & isp->crashed) &
	    (1U << isp->isp_prev.subdev.entity.id))
		return -EIO;

	spin_lock_irqsave(&pipe->lock, flags);
	pipe->state &= ~(ISP_PIPELINE_IDLE_INPUT | ISP_PIPELINE_IDLE_OUTPUT);
	spin_unlock_irqrestore(&pipe->lock, flags);

	pipe->do_propagation = false;

	entity = &pipe->output->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, mode);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;

		if (subdev == &isp->isp_ccdc.subdev) {
			v4l2_subdev_call(&isp->isp_aewb.subdev, video,
					s_stream, mode);
			v4l2_subdev_call(&isp->isp_af.subdev, video,
					s_stream, mode);
			v4l2_subdev_call(&isp->isp_hist.subdev, video,
					s_stream, mode);
			pipe->do_propagation = true;
		}
	}

	return 0;
}

static int isp_pipeline_wait_resizer(struct isp_device *isp)
{
	return omap3isp_resizer_busy(&isp->isp_res);
}

static int isp_pipeline_wait_preview(struct isp_device *isp)
{
	return omap3isp_preview_busy(&isp->isp_prev);
}

static int isp_pipeline_wait_ccdc(struct isp_device *isp)
{
	return omap3isp_stat_busy(&isp->isp_af)
	    || omap3isp_stat_busy(&isp->isp_aewb)
	    || omap3isp_stat_busy(&isp->isp_hist)
	    || omap3isp_ccdc_busy(&isp->isp_ccdc);
}

#define ISP_STOP_TIMEOUT	msecs_to_jiffies(1000)

static int isp_pipeline_wait(struct isp_device *isp,
			     int(*busy)(struct isp_device *isp))
{
	unsigned long timeout = jiffies + ISP_STOP_TIMEOUT;

	while (!time_after(jiffies, timeout)) {
		if (!busy(isp))
			return 0;
	}

	return 1;
}

/*
 * isp_pipeline_disable - Disable streaming on a pipeline
 * @pipe: ISP pipeline
 *
 * Walk the entities chain starting at the pipeline output video node and stop
 * all modules in the chain. Wait synchronously for the modules to be stopped if
 * necessary.
 *
 * Return 0 if all modules have been properly stopped, or -ETIMEDOUT if a module
 * can't be stopped (in which case a software reset of the ISP is probably
 * necessary).
 */
static int isp_pipeline_disable(struct isp_pipeline *pipe)
{
	struct isp_device *isp = pipe->output->isp;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int failure = 0;
	int ret;

	/*
	 * We need to stop all the modules after CCDC first or they'll
	 * never stop since they may not get a full frame from CCDC.
	 */
	entity = &pipe->output->video.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		if (subdev == &isp->isp_ccdc.subdev) {
			v4l2_subdev_call(&isp->isp_aewb.subdev,
					 video, s_stream, 0);
			v4l2_subdev_call(&isp->isp_af.subdev,
					 video, s_stream, 0);
			v4l2_subdev_call(&isp->isp_hist.subdev,
					 video, s_stream, 0);
		}

		v4l2_subdev_call(subdev, video, s_stream, 0);

		if (subdev == &isp->isp_res.subdev)
			ret = isp_pipeline_wait(isp, isp_pipeline_wait_resizer);
		else if (subdev == &isp->isp_prev.subdev)
			ret = isp_pipeline_wait(isp, isp_pipeline_wait_preview);
		else if (subdev == &isp->isp_ccdc.subdev)
			ret = isp_pipeline_wait(isp, isp_pipeline_wait_ccdc);
		else
			ret = 0;

		if (ret) {
			dev_info(isp->dev, "Unable to stop %s\n", subdev->name);
			/* If the entity failed to stopped, assume it has
			 * crashed. Mark it as such, the ISP will be reset when
			 * applications will release it.
			 */
			isp->crashed |= 1U << subdev->entity.id;
			failure = -ETIMEDOUT;
		}
	}

	return failure;
}

/*
 * omap3isp_pipeline_set_stream - Enable/disable streaming on a pipeline
 * @pipe: ISP pipeline
 * @state: Stream state (stopped, single shot or continuous)
 *
 * Set the pipeline to the given stream state. Pipelines can be started in
 * single-shot or continuous mode.
 *
 * Return 0 if successful, or the return value of the failed video::s_stream
 * operation otherwise. The pipeline state is not updated when the operation
 * fails, except when stopping the pipeline.
 */
int omap3isp_pipeline_set_stream(struct isp_pipeline *pipe,
				 enum isp_pipeline_stream_state state)
{
	int ret;

	if (state == ISP_PIPELINE_STREAM_STOPPED)
		ret = isp_pipeline_disable(pipe);
	else
		ret = isp_pipeline_enable(pipe, state);

	if (ret == 0 || state == ISP_PIPELINE_STREAM_STOPPED)
		pipe->stream_state = state;

	return ret;
}

/*
 * isp_pipeline_resume - Resume streaming on a pipeline
 * @pipe: ISP pipeline
 *
 * Resume video output and input and re-enable pipeline.
 */
static void isp_pipeline_resume(struct isp_pipeline *pipe)
{
	int singleshot = pipe->stream_state == ISP_PIPELINE_STREAM_SINGLESHOT;

	omap3isp_video_resume(pipe->output, !singleshot);
	if (singleshot)
		omap3isp_video_resume(pipe->input, 0);
	isp_pipeline_enable(pipe, pipe->stream_state);
}

/*
 * isp_pipeline_suspend - Suspend streaming on a pipeline
 * @pipe: ISP pipeline
 *
 * Suspend pipeline.
 */
static void isp_pipeline_suspend(struct isp_pipeline *pipe)
{
	isp_pipeline_disable(pipe);
}

/*
 * isp_pipeline_is_last - Verify if entity has an enabled link to the output
 * 			  video node
 * @me: ISP module's media entity
 *
 * Returns 1 if the entity has an enabled link to the output video node or 0
 * otherwise. It's true only while pipeline can have no more than one output
 * node.
 */
static int isp_pipeline_is_last(struct media_entity *me)
{
	struct isp_pipeline *pipe;
	struct media_pad *pad;

	if (!me->pipe)
		return 0;
	pipe = to_isp_pipeline(me);
	if (pipe->stream_state == ISP_PIPELINE_STREAM_STOPPED)
		return 0;
	pad = media_entity_remote_source(&pipe->output->pad);
	return pad->entity == me;
}

/*
 * isp_suspend_module_pipeline - Suspend pipeline to which belongs the module
 * @me: ISP module's media entity
 *
 * Suspend the whole pipeline if module's entity has an enabled link to the
 * output video node. It works only while pipeline can have no more than one
 * output node.
 */
static void isp_suspend_module_pipeline(struct media_entity *me)
{
	if (isp_pipeline_is_last(me))
		isp_pipeline_suspend(to_isp_pipeline(me));
}

/*
 * isp_resume_module_pipeline - Resume pipeline to which belongs the module
 * @me: ISP module's media entity
 *
 * Resume the whole pipeline if module's entity has an enabled link to the
 * output video node. It works only while pipeline can have no more than one
 * output node.
 */
static void isp_resume_module_pipeline(struct media_entity *me)
{
	if (isp_pipeline_is_last(me))
		isp_pipeline_resume(to_isp_pipeline(me));
}

/*
 * isp_suspend_modules - Suspend ISP submodules.
 * @isp: OMAP3 ISP device
 *
 * Returns 0 if suspend left in idle state all the submodules properly,
 * or returns 1 if a general Reset is required to suspend the submodules.
 */
static int isp_suspend_modules(struct isp_device *isp)
{
	unsigned long timeout;

	omap3isp_stat_suspend(&isp->isp_aewb);
	omap3isp_stat_suspend(&isp->isp_af);
	omap3isp_stat_suspend(&isp->isp_hist);
	isp_suspend_module_pipeline(&isp->isp_res.subdev.entity);
	isp_suspend_module_pipeline(&isp->isp_prev.subdev.entity);
	isp_suspend_module_pipeline(&isp->isp_ccdc.subdev.entity);
	isp_suspend_module_pipeline(&isp->isp_csi2a.subdev.entity);
	isp_suspend_module_pipeline(&isp->isp_ccp2.subdev.entity);

	timeout = jiffies + ISP_STOP_TIMEOUT;
	while (omap3isp_stat_busy(&isp->isp_af)
	    || omap3isp_stat_busy(&isp->isp_aewb)
	    || omap3isp_stat_busy(&isp->isp_hist)
	    || omap3isp_preview_busy(&isp->isp_prev)
	    || omap3isp_resizer_busy(&isp->isp_res)
	    || omap3isp_ccdc_busy(&isp->isp_ccdc)) {
		if (time_after(jiffies, timeout)) {
			dev_info(isp->dev, "can't stop modules.\n");
			return 1;
		}
		msleep(1);
	}

	return 0;
}

/*
 * isp_resume_modules - Resume ISP submodules.
 * @isp: OMAP3 ISP device
 */
static void isp_resume_modules(struct isp_device *isp)
{
	omap3isp_stat_resume(&isp->isp_aewb);
	omap3isp_stat_resume(&isp->isp_af);
	omap3isp_stat_resume(&isp->isp_hist);
	isp_resume_module_pipeline(&isp->isp_res.subdev.entity);
	isp_resume_module_pipeline(&isp->isp_prev.subdev.entity);
	isp_resume_module_pipeline(&isp->isp_ccdc.subdev.entity);
	isp_resume_module_pipeline(&isp->isp_csi2a.subdev.entity);
	isp_resume_module_pipeline(&isp->isp_ccp2.subdev.entity);
}

/*
 * isp_reset - Reset ISP with a timeout wait for idle.
 * @isp: OMAP3 ISP device
 */
static int isp_reset(struct isp_device *isp)
{
	unsigned long timeout = 0;

	isp_reg_writel(isp,
		       isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN, ISP_SYSCONFIG)
		       | ISP_SYSCONFIG_SOFTRESET,
		       OMAP3_ISP_IOMEM_MAIN, ISP_SYSCONFIG);
	while (!(isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN,
			       ISP_SYSSTATUS) & 0x1)) {
		if (timeout++ > 10000) {
			dev_alert(isp->dev, "cannot reset ISP\n");
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	isp->crashed = 0;
	return 0;
}

/*
 * isp_save_context - Saves the values of the ISP module registers.
 * @isp: OMAP3 ISP device
 * @reg_list: Structure containing pairs of register address and value to
 *            modify on OMAP.
 */
static void
isp_save_context(struct isp_device *isp, struct isp_reg *reg_list)
{
	struct isp_reg *next = reg_list;

	for (; next->reg != ISP_TOK_TERM; next++)
		next->val = isp_reg_readl(isp, next->mmio_range, next->reg);
}

/*
 * isp_restore_context - Restores the values of the ISP module registers.
 * @isp: OMAP3 ISP device
 * @reg_list: Structure containing pairs of register address and value to
 *            modify on OMAP.
 */
static void
isp_restore_context(struct isp_device *isp, struct isp_reg *reg_list)
{
	struct isp_reg *next = reg_list;

	for (; next->reg != ISP_TOK_TERM; next++)
		isp_reg_writel(isp, next->val, next->mmio_range, next->reg);
}

/*
 * isp_save_ctx - Saves ISP, CCDC, HIST, H3A, PREV, RESZ & MMU context.
 * @isp: OMAP3 ISP device
 *
 * Routine for saving the context of each module in the ISP.
 * CCDC, HIST, H3A, PREV, RESZ and MMU.
 */
static void isp_save_ctx(struct isp_device *isp)
{
	isp_save_context(isp, isp_reg_list);
	omap_iommu_save_ctx(isp->dev);
}

/*
 * isp_restore_ctx - Restores ISP, CCDC, HIST, H3A, PREV, RESZ & MMU context.
 * @isp: OMAP3 ISP device
 *
 * Routine for restoring the context of each module in the ISP.
 * CCDC, HIST, H3A, PREV, RESZ and MMU.
 */
static void isp_restore_ctx(struct isp_device *isp)
{
	isp_restore_context(isp, isp_reg_list);
	omap_iommu_restore_ctx(isp->dev);
	omap3isp_ccdc_restore_context(isp);
	omap3isp_preview_restore_context(isp);
}

/* -----------------------------------------------------------------------------
 * SBL resources management
 */
#define OMAP3_ISP_SBL_READ	(OMAP3_ISP_SBL_CSI1_READ | \
				 OMAP3_ISP_SBL_CCDC_LSC_READ | \
				 OMAP3_ISP_SBL_PREVIEW_READ | \
				 OMAP3_ISP_SBL_RESIZER_READ)
#define OMAP3_ISP_SBL_WRITE	(OMAP3_ISP_SBL_CSI1_WRITE | \
				 OMAP3_ISP_SBL_CSI2A_WRITE | \
				 OMAP3_ISP_SBL_CSI2C_WRITE | \
				 OMAP3_ISP_SBL_CCDC_WRITE | \
				 OMAP3_ISP_SBL_PREVIEW_WRITE)

void omap3isp_sbl_enable(struct isp_device *isp, enum isp_sbl_resource res)
{
	u32 sbl = 0;

	isp->sbl_resources |= res;

	if (isp->sbl_resources & OMAP3_ISP_SBL_CSI1_READ)
		sbl |= ISPCTRL_SBL_SHARED_RPORTA;

	if (isp->sbl_resources & OMAP3_ISP_SBL_CCDC_LSC_READ)
		sbl |= ISPCTRL_SBL_SHARED_RPORTB;

	if (isp->sbl_resources & OMAP3_ISP_SBL_CSI2C_WRITE)
		sbl |= ISPCTRL_SBL_SHARED_WPORTC;

	if (isp->sbl_resources & OMAP3_ISP_SBL_RESIZER_WRITE)
		sbl |= ISPCTRL_SBL_WR0_RAM_EN;

	if (isp->sbl_resources & OMAP3_ISP_SBL_WRITE)
		sbl |= ISPCTRL_SBL_WR1_RAM_EN;

	if (isp->sbl_resources & OMAP3_ISP_SBL_READ)
		sbl |= ISPCTRL_SBL_RD_RAM_EN;

	isp_reg_set(isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL, sbl);
}

void omap3isp_sbl_disable(struct isp_device *isp, enum isp_sbl_resource res)
{
	u32 sbl = 0;

	isp->sbl_resources &= ~res;

	if (!(isp->sbl_resources & OMAP3_ISP_SBL_CSI1_READ))
		sbl |= ISPCTRL_SBL_SHARED_RPORTA;

	if (!(isp->sbl_resources & OMAP3_ISP_SBL_CCDC_LSC_READ))
		sbl |= ISPCTRL_SBL_SHARED_RPORTB;

	if (!(isp->sbl_resources & OMAP3_ISP_SBL_CSI2C_WRITE))
		sbl |= ISPCTRL_SBL_SHARED_WPORTC;

	if (!(isp->sbl_resources & OMAP3_ISP_SBL_RESIZER_WRITE))
		sbl |= ISPCTRL_SBL_WR0_RAM_EN;

	if (!(isp->sbl_resources & OMAP3_ISP_SBL_WRITE))
		sbl |= ISPCTRL_SBL_WR1_RAM_EN;

	if (!(isp->sbl_resources & OMAP3_ISP_SBL_READ))
		sbl |= ISPCTRL_SBL_RD_RAM_EN;

	isp_reg_clr(isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL, sbl);
}

/*
 * isp_module_sync_idle - Helper to sync module with its idle state
 * @me: ISP submodule's media entity
 * @wait: ISP submodule's wait queue for streamoff/interrupt synchronization
 * @stopping: flag which tells module wants to stop
 *
 * This function checks if ISP submodule needs to wait for next interrupt. If
 * yes, makes the caller to sleep while waiting for such event.
 */
int omap3isp_module_sync_idle(struct media_entity *me, wait_queue_head_t *wait,
			      atomic_t *stopping)
{
	struct isp_pipeline *pipe = to_isp_pipeline(me);

	if (pipe->stream_state == ISP_PIPELINE_STREAM_STOPPED ||
	    (pipe->stream_state == ISP_PIPELINE_STREAM_SINGLESHOT &&
	     !isp_pipeline_ready(pipe)))
		return 0;

	/*
	 * atomic_set() doesn't include memory barrier on ARM platform for SMP
	 * scenario. We'll call it here to avoid race conditions.
	 */
	atomic_set(stopping, 1);
	smp_mb();

	/*
	 * If module is the last one, it's writing to memory. In this case,
	 * it's necessary to check if the module is already paused due to
	 * DMA queue underrun or if it has to wait for next interrupt to be
	 * idle.
	 * If it isn't the last one, the function won't sleep but *stopping
	 * will still be set to warn next submodule caller's interrupt the
	 * module wants to be idle.
	 */
	if (isp_pipeline_is_last(me)) {
		struct isp_video *video = pipe->output;
		unsigned long flags;
		spin_lock_irqsave(&video->queue->irqlock, flags);
		if (video->dmaqueue_flags & ISP_VIDEO_DMAQUEUE_UNDERRUN) {
			spin_unlock_irqrestore(&video->queue->irqlock, flags);
			atomic_set(stopping, 0);
			smp_mb();
			return 0;
		}
		spin_unlock_irqrestore(&video->queue->irqlock, flags);
		if (!wait_event_timeout(*wait, !atomic_read(stopping),
					msecs_to_jiffies(1000))) {
			atomic_set(stopping, 0);
			smp_mb();
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * omap3isp_module_sync_is_stopped - Helper to verify if module was stopping
 * @wait: ISP submodule's wait queue for streamoff/interrupt synchronization
 * @stopping: flag which tells module wants to stop
 *
 * This function checks if ISP submodule was stopping. In case of yes, it
 * notices the caller by setting stopping to 0 and waking up the wait queue.
 * Returns 1 if it was stopping or 0 otherwise.
 */
int omap3isp_module_sync_is_stopping(wait_queue_head_t *wait,
				     atomic_t *stopping)
{
	if (atomic_cmpxchg(stopping, 1, 0)) {
		wake_up(wait);
		return 1;
	}

	return 0;
}

/* --------------------------------------------------------------------------
 * Clock management
 */

#define ISPCTRL_CLKS_MASK	(ISPCTRL_H3A_CLK_EN | \
				 ISPCTRL_HIST_CLK_EN | \
				 ISPCTRL_RSZ_CLK_EN | \
				 (ISPCTRL_CCDC_CLK_EN | ISPCTRL_CCDC_RAM_EN) | \
				 (ISPCTRL_PREV_CLK_EN | ISPCTRL_PREV_RAM_EN))

static void __isp_subclk_update(struct isp_device *isp)
{
	u32 clk = 0;

	if (isp->subclk_resources & OMAP3_ISP_SUBCLK_H3A)
		clk |= ISPCTRL_H3A_CLK_EN;

	if (isp->subclk_resources & OMAP3_ISP_SUBCLK_HIST)
		clk |= ISPCTRL_HIST_CLK_EN;

	if (isp->subclk_resources & OMAP3_ISP_SUBCLK_RESIZER)
		clk |= ISPCTRL_RSZ_CLK_EN;

	/* NOTE: For CCDC & Preview submodules, we need to affect internal
	 *       RAM as well.
	 */
	if (isp->subclk_resources & OMAP3_ISP_SUBCLK_CCDC)
		clk |= ISPCTRL_CCDC_CLK_EN | ISPCTRL_CCDC_RAM_EN;

	if (isp->subclk_resources & OMAP3_ISP_SUBCLK_PREVIEW)
		clk |= ISPCTRL_PREV_CLK_EN | ISPCTRL_PREV_RAM_EN;

	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL,
			ISPCTRL_CLKS_MASK, clk);
}

void omap3isp_subclk_enable(struct isp_device *isp,
			    enum isp_subclk_resource res)
{
	isp->subclk_resources |= res;

	__isp_subclk_update(isp);
}

void omap3isp_subclk_disable(struct isp_device *isp,
			     enum isp_subclk_resource res)
{
	isp->subclk_resources &= ~res;

	__isp_subclk_update(isp);
}

/*
 * isp_enable_clocks - Enable ISP clocks
 * @isp: OMAP3 ISP device
 *
 * Return 0 if successful, or clk_enable return value if any of tthem fails.
 */
static int isp_enable_clocks(struct isp_device *isp)
{
	int r;
	unsigned long rate;
	int divisor;

	/*
	 * cam_mclk clock chain:
	 *   dpll4 -> dpll4_m5 -> dpll4_m5x2 -> cam_mclk
	 *
	 * In OMAP3630 dpll4_m5x2 != 2 x dpll4_m5 but both are
	 * set to the same value. Hence the rate set for dpll4_m5
	 * has to be twice of what is set on OMAP3430 to get
	 * the required value for cam_mclk
	 */
	if (cpu_is_omap3630())
		divisor = 1;
	else
		divisor = 2;

	r = clk_enable(isp->clock[ISP_CLK_CAM_ICK]);
	if (r) {
		dev_err(isp->dev, "clk_enable cam_ick failed\n");
		goto out_clk_enable_ick;
	}
	r = clk_set_rate(isp->clock[ISP_CLK_DPLL4_M5_CK],
			 CM_CAM_MCLK_HZ/divisor);
	if (r) {
		dev_err(isp->dev, "clk_set_rate for dpll4_m5_ck failed\n");
		goto out_clk_enable_mclk;
	}
	r = clk_enable(isp->clock[ISP_CLK_CAM_MCLK]);
	if (r) {
		dev_err(isp->dev, "clk_enable cam_mclk failed\n");
		goto out_clk_enable_mclk;
	}
	rate = clk_get_rate(isp->clock[ISP_CLK_CAM_MCLK]);
	if (rate != CM_CAM_MCLK_HZ)
		dev_warn(isp->dev, "unexpected cam_mclk rate:\n"
				   " expected : %d\n"
				   " actual   : %ld\n", CM_CAM_MCLK_HZ, rate);
	r = clk_enable(isp->clock[ISP_CLK_CSI2_FCK]);
	if (r) {
		dev_err(isp->dev, "clk_enable csi2_fck failed\n");
		goto out_clk_enable_csi2_fclk;
	}
	return 0;

out_clk_enable_csi2_fclk:
	clk_disable(isp->clock[ISP_CLK_CAM_MCLK]);
out_clk_enable_mclk:
	clk_disable(isp->clock[ISP_CLK_CAM_ICK]);
out_clk_enable_ick:
	return r;
}

/*
 * isp_disable_clocks - Disable ISP clocks
 * @isp: OMAP3 ISP device
 */
static void isp_disable_clocks(struct isp_device *isp)
{
	clk_disable(isp->clock[ISP_CLK_CAM_ICK]);
	clk_disable(isp->clock[ISP_CLK_CAM_MCLK]);
	clk_disable(isp->clock[ISP_CLK_CSI2_FCK]);
}

static const char *isp_clocks[] = {
	"cam_ick",
	"cam_mclk",
	"dpll4_m5_ck",
	"csi2_96m_fck",
	"l3_ick",
};

static void isp_put_clocks(struct isp_device *isp)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isp_clocks); ++i) {
		if (isp->clock[i]) {
			clk_put(isp->clock[i]);
			isp->clock[i] = NULL;
		}
	}
}

static int isp_get_clocks(struct isp_device *isp)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isp_clocks); ++i) {
		clk = clk_get(isp->dev, isp_clocks[i]);
		if (IS_ERR(clk)) {
			dev_err(isp->dev, "clk_get %s failed\n", isp_clocks[i]);
			isp_put_clocks(isp);
			return PTR_ERR(clk);
		}

		isp->clock[i] = clk;
	}

	return 0;
}

/*
 * omap3isp_get - Acquire the ISP resource.
 *
 * Initializes the clocks for the first acquire.
 *
 * Increment the reference count on the ISP. If the first reference is taken,
 * enable clocks and power-up all submodules.
 *
 * Return a pointer to the ISP device structure, or NULL if an error occurred.
 */
struct isp_device *omap3isp_get(struct isp_device *isp)
{
	struct isp_device *__isp = isp;

	if (isp == NULL)
		return NULL;

	mutex_lock(&isp->isp_mutex);
	if (isp->ref_count > 0)
		goto out;

	if (isp_enable_clocks(isp) < 0) {
		__isp = NULL;
		goto out;
	}

	/* We don't want to restore context before saving it! */
	if (isp->has_context)
		isp_restore_ctx(isp);
	else
		isp->has_context = 1;

	isp_enable_interrupts(isp);

out:
	if (__isp != NULL)
		isp->ref_count++;
	mutex_unlock(&isp->isp_mutex);

	return __isp;
}

/*
 * omap3isp_put - Release the ISP
 *
 * Decrement the reference count on the ISP. If the last reference is released,
 * power-down all submodules, disable clocks and free temporary buffers.
 */
void omap3isp_put(struct isp_device *isp)
{
	if (isp == NULL)
		return;

	mutex_lock(&isp->isp_mutex);
	BUG_ON(isp->ref_count == 0);
	if (--isp->ref_count == 0) {
		isp_disable_interrupts(isp);
		if (isp->domain)
			isp_save_ctx(isp);
		/* Reset the ISP if an entity has failed to stop. This is the
		 * only way to recover from such conditions.
		 */
		if (isp->crashed)
			isp_reset(isp);
		isp_disable_clocks(isp);
	}
	mutex_unlock(&isp->isp_mutex);
}

/* --------------------------------------------------------------------------
 * Platform device driver
 */

/*
 * omap3isp_print_status - Prints the values of the ISP Control Module registers
 * @isp: OMAP3 ISP device
 */
#define ISP_PRINT_REGISTER(isp, name)\
	dev_dbg(isp->dev, "###ISP " #name "=0x%08x\n", \
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN, ISP_##name))
#define SBL_PRINT_REGISTER(isp, name)\
	dev_dbg(isp->dev, "###SBL " #name "=0x%08x\n", \
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_SBL, ISPSBL_##name))

void omap3isp_print_status(struct isp_device *isp)
{
	dev_dbg(isp->dev, "-------------ISP Register dump--------------\n");

	ISP_PRINT_REGISTER(isp, SYSCONFIG);
	ISP_PRINT_REGISTER(isp, SYSSTATUS);
	ISP_PRINT_REGISTER(isp, IRQ0ENABLE);
	ISP_PRINT_REGISTER(isp, IRQ0STATUS);
	ISP_PRINT_REGISTER(isp, TCTRL_GRESET_LENGTH);
	ISP_PRINT_REGISTER(isp, TCTRL_PSTRB_REPLAY);
	ISP_PRINT_REGISTER(isp, CTRL);
	ISP_PRINT_REGISTER(isp, TCTRL_CTRL);
	ISP_PRINT_REGISTER(isp, TCTRL_FRAME);
	ISP_PRINT_REGISTER(isp, TCTRL_PSTRB_DELAY);
	ISP_PRINT_REGISTER(isp, TCTRL_STRB_DELAY);
	ISP_PRINT_REGISTER(isp, TCTRL_SHUT_DELAY);
	ISP_PRINT_REGISTER(isp, TCTRL_PSTRB_LENGTH);
	ISP_PRINT_REGISTER(isp, TCTRL_STRB_LENGTH);
	ISP_PRINT_REGISTER(isp, TCTRL_SHUT_LENGTH);

	SBL_PRINT_REGISTER(isp, PCR);
	SBL_PRINT_REGISTER(isp, SDR_REQ_EXP);

	dev_dbg(isp->dev, "--------------------------------------------\n");
}

#ifdef CONFIG_PM

/*
 * Power management support.
 *
 * As the ISP can't properly handle an input video stream interruption on a non
 * frame boundary, the ISP pipelines need to be stopped before sensors get
 * suspended. However, as suspending the sensors can require a running clock,
 * which can be provided by the ISP, the ISP can't be completely suspended
 * before the sensor.
 *
 * To solve this problem power management support is split into prepare/complete
 * and suspend/resume operations. The pipelines are stopped in prepare() and the
 * ISP clocks get disabled in suspend(). Similarly, the clocks are reenabled in
 * resume(), and the the pipelines are restarted in complete().
 *
 * TODO: PM dependencies between the ISP and sensors are not modeled explicitly
 * yet.
 */
static int isp_pm_prepare(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	int reset;

	WARN_ON(mutex_is_locked(&isp->isp_mutex));

	if (isp->ref_count == 0)
		return 0;

	reset = isp_suspend_modules(isp);
	isp_disable_interrupts(isp);
	isp_save_ctx(isp);
	if (reset)
		isp_reset(isp);

	return 0;
}

static int isp_pm_suspend(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	WARN_ON(mutex_is_locked(&isp->isp_mutex));

	if (isp->ref_count)
		isp_disable_clocks(isp);

	return 0;
}

static int isp_pm_resume(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	if (isp->ref_count == 0)
		return 0;

	return isp_enable_clocks(isp);
}

static void isp_pm_complete(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	if (isp->ref_count == 0)
		return;

	isp_restore_ctx(isp);
	isp_enable_interrupts(isp);
	isp_resume_modules(isp);
}

#else

#define isp_pm_prepare	NULL
#define isp_pm_suspend	NULL
#define isp_pm_resume	NULL
#define isp_pm_complete	NULL

#endif /* CONFIG_PM */

static void isp_unregister_entities(struct isp_device *isp)
{
	omap3isp_csi2_unregister_entities(&isp->isp_csi2a);
	omap3isp_ccp2_unregister_entities(&isp->isp_ccp2);
	omap3isp_ccdc_unregister_entities(&isp->isp_ccdc);
	omap3isp_preview_unregister_entities(&isp->isp_prev);
	omap3isp_resizer_unregister_entities(&isp->isp_res);
	omap3isp_stat_unregister_entities(&isp->isp_aewb);
	omap3isp_stat_unregister_entities(&isp->isp_af);
	omap3isp_stat_unregister_entities(&isp->isp_hist);

	v4l2_device_unregister(&isp->v4l2_dev);
	media_device_unregister(&isp->media_dev);
}

/*
 * isp_register_subdev_group - Register a group of subdevices
 * @isp: OMAP3 ISP device
 * @board_info: I2C subdevs board information array
 *
 * Register all I2C subdevices in the board_info array. The array must be
 * terminated by a NULL entry, and the first entry must be the sensor.
 *
 * Return a pointer to the sensor media entity if it has been successfully
 * registered, or NULL otherwise.
 */
static struct v4l2_subdev *
isp_register_subdev_group(struct isp_device *isp,
		     struct isp_subdev_i2c_board_info *board_info)
{
	struct v4l2_subdev *sensor = NULL;
	unsigned int first;

	if (board_info->board_info == NULL)
		return NULL;

	for (first = 1; board_info->board_info; ++board_info, first = 0) {
		struct v4l2_subdev *subdev;
		struct i2c_adapter *adapter;

		adapter = i2c_get_adapter(board_info->i2c_adapter_id);
		if (adapter == NULL) {
			printk(KERN_ERR "%s: Unable to get I2C adapter %d for "
				"device %s\n", __func__,
				board_info->i2c_adapter_id,
				board_info->board_info->type);
			continue;
		}

		subdev = v4l2_i2c_new_subdev_board(&isp->v4l2_dev, adapter,
				board_info->board_info, NULL);
		if (subdev == NULL) {
			printk(KERN_ERR "%s: Unable to register subdev %s\n",
				__func__, board_info->board_info->type);
			continue;
		}

		if (first)
			sensor = subdev;
	}

	return sensor;
}

static int isp_register_entities(struct isp_device *isp)
{
	struct isp_platform_data *pdata = isp->pdata;
	struct isp_v4l2_subdevs_group *subdevs;
	int ret;

	isp->media_dev.dev = isp->dev;
	strlcpy(isp->media_dev.model, "TI OMAP3 ISP",
		sizeof(isp->media_dev.model));
	isp->media_dev.hw_revision = isp->revision;
	isp->media_dev.link_notify = isp_pipeline_link_notify;
	ret = media_device_register(&isp->media_dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: Media device registration failed (%d)\n",
			__func__, ret);
		return ret;
	}

	isp->v4l2_dev.mdev = &isp->media_dev;
	ret = v4l2_device_register(isp->dev, &isp->v4l2_dev);
	if (ret < 0) {
		printk(KERN_ERR "%s: V4L2 device registration failed (%d)\n",
			__func__, ret);
		goto done;
	}

	/* Register internal entities */
	ret = omap3isp_ccp2_register_entities(&isp->isp_ccp2, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_csi2_register_entities(&isp->isp_csi2a, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_ccdc_register_entities(&isp->isp_ccdc, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_preview_register_entities(&isp->isp_prev,
						 &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_resizer_register_entities(&isp->isp_res, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_stat_register_entities(&isp->isp_aewb, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_stat_register_entities(&isp->isp_af, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	ret = omap3isp_stat_register_entities(&isp->isp_hist, &isp->v4l2_dev);
	if (ret < 0)
		goto done;

	/* Register external entities */
	for (subdevs = pdata->subdevs; subdevs && subdevs->subdevs; ++subdevs) {
		struct v4l2_subdev *sensor;
		struct media_entity *input;
		unsigned int flags;
		unsigned int pad;

		sensor = isp_register_subdev_group(isp, subdevs->subdevs);
		if (sensor == NULL)
			continue;

		sensor->host_priv = subdevs;

		/* Connect the sensor to the correct interface module. Parallel
		 * sensors are connected directly to the CCDC, while serial
		 * sensors are connected to the CSI2a, CCP2b or CSI2c receiver
		 * through CSIPHY1 or CSIPHY2.
		 */
		switch (subdevs->interface) {
		case ISP_INTERFACE_PARALLEL:
			input = &isp->isp_ccdc.subdev.entity;
			pad = CCDC_PAD_SINK;
			flags = 0;
			break;

		case ISP_INTERFACE_CSI2A_PHY2:
			input = &isp->isp_csi2a.subdev.entity;
			pad = CSI2_PAD_SINK;
			flags = MEDIA_LNK_FL_IMMUTABLE
			      | MEDIA_LNK_FL_ENABLED;
			break;

		case ISP_INTERFACE_CCP2B_PHY1:
		case ISP_INTERFACE_CCP2B_PHY2:
			input = &isp->isp_ccp2.subdev.entity;
			pad = CCP2_PAD_SINK;
			flags = 0;
			break;

		case ISP_INTERFACE_CSI2C_PHY1:
			input = &isp->isp_csi2c.subdev.entity;
			pad = CSI2_PAD_SINK;
			flags = MEDIA_LNK_FL_IMMUTABLE
			      | MEDIA_LNK_FL_ENABLED;
			break;

		default:
			printk(KERN_ERR "%s: invalid interface type %u\n",
			       __func__, subdevs->interface);
			ret = -EINVAL;
			goto done;
		}

		ret = media_entity_create_link(&sensor->entity, 0, input, pad,
					       flags);
		if (ret < 0)
			goto done;
	}

	ret = v4l2_device_register_subdev_nodes(&isp->v4l2_dev);

done:
	if (ret < 0)
		isp_unregister_entities(isp);

	return ret;
}

static void isp_cleanup_modules(struct isp_device *isp)
{
	omap3isp_h3a_aewb_cleanup(isp);
	omap3isp_h3a_af_cleanup(isp);
	omap3isp_hist_cleanup(isp);
	omap3isp_resizer_cleanup(isp);
	omap3isp_preview_cleanup(isp);
	omap3isp_ccdc_cleanup(isp);
	omap3isp_ccp2_cleanup(isp);
	omap3isp_csi2_cleanup(isp);
}

static int isp_initialize_modules(struct isp_device *isp)
{
	int ret;

	ret = omap3isp_csiphy_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "CSI PHY initialization failed\n");
		goto error_csiphy;
	}

	ret = omap3isp_csi2_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "CSI2 initialization failed\n");
		goto error_csi2;
	}

	ret = omap3isp_ccp2_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "CCP2 initialization failed\n");
		goto error_ccp2;
	}

	ret = omap3isp_ccdc_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "CCDC initialization failed\n");
		goto error_ccdc;
	}

	ret = omap3isp_preview_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "Preview initialization failed\n");
		goto error_preview;
	}

	ret = omap3isp_resizer_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "Resizer initialization failed\n");
		goto error_resizer;
	}

	ret = omap3isp_hist_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "Histogram initialization failed\n");
		goto error_hist;
	}

	ret = omap3isp_h3a_aewb_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "H3A AEWB initialization failed\n");
		goto error_h3a_aewb;
	}

	ret = omap3isp_h3a_af_init(isp);
	if (ret < 0) {
		dev_err(isp->dev, "H3A AF initialization failed\n");
		goto error_h3a_af;
	}

	/* Connect the submodules. */
	ret = media_entity_create_link(
			&isp->isp_csi2a.subdev.entity, CSI2_PAD_SOURCE,
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_ccp2.subdev.entity, CCP2_PAD_SOURCE,
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SOURCE_VP,
			&isp->isp_prev.subdev.entity, PREV_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SOURCE_OF,
			&isp->isp_res.subdev.entity, RESZ_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_prev.subdev.entity, PREV_PAD_SOURCE,
			&isp->isp_res.subdev.entity, RESZ_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SOURCE_VP,
			&isp->isp_aewb.subdev.entity, 0,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SOURCE_VP,
			&isp->isp_af.subdev.entity, 0,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(
			&isp->isp_ccdc.subdev.entity, CCDC_PAD_SOURCE_VP,
			&isp->isp_hist.subdev.entity, 0,
			MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0)
		goto error_link;

	return 0;

error_link:
	omap3isp_h3a_af_cleanup(isp);
error_h3a_af:
	omap3isp_h3a_aewb_cleanup(isp);
error_h3a_aewb:
	omap3isp_hist_cleanup(isp);
error_hist:
	omap3isp_resizer_cleanup(isp);
error_resizer:
	omap3isp_preview_cleanup(isp);
error_preview:
	omap3isp_ccdc_cleanup(isp);
error_ccdc:
	omap3isp_ccp2_cleanup(isp);
error_ccp2:
	omap3isp_csi2_cleanup(isp);
error_csi2:
error_csiphy:
	return ret;
}

/*
 * isp_remove - Remove ISP platform device
 * @pdev: Pointer to ISP platform device
 *
 * Always returns 0.
 */
static int isp_remove(struct platform_device *pdev)
{
	struct isp_device *isp = platform_get_drvdata(pdev);
	int i;

	isp_unregister_entities(isp);
	isp_cleanup_modules(isp);

	omap3isp_get(isp);
	iommu_detach_device(isp->domain, &pdev->dev);
	iommu_domain_free(isp->domain);
	isp->domain = NULL;
	omap3isp_put(isp);

	free_irq(isp->irq_num, isp);
	isp_put_clocks(isp);

	for (i = 0; i < OMAP3_ISP_IOMEM_LAST; i++) {
		if (isp->mmio_base[i]) {
			iounmap(isp->mmio_base[i]);
			isp->mmio_base[i] = NULL;
		}

		if (isp->mmio_base_phys[i]) {
			release_mem_region(isp->mmio_base_phys[i],
					   isp->mmio_size[i]);
			isp->mmio_base_phys[i] = 0;
		}
	}

	regulator_put(isp->isp_csiphy1.vdd);
	regulator_put(isp->isp_csiphy2.vdd);
	kfree(isp);

	return 0;
}

static int isp_map_mem_resource(struct platform_device *pdev,
				struct isp_device *isp,
				enum isp_mem_resources res)
{
	struct resource *mem;

	/* request the mem region for the camera registers */

	mem = platform_get_resource(pdev, IORESOURCE_MEM, res);
	if (!mem) {
		dev_err(isp->dev, "no mem resource?\n");
		return -ENODEV;
	}

	if (!request_mem_region(mem->start, resource_size(mem), pdev->name)) {
		dev_err(isp->dev,
			"cannot reserve camera register I/O region\n");
		return -ENODEV;
	}
	isp->mmio_base_phys[res] = mem->start;
	isp->mmio_size[res] = resource_size(mem);

	/* map the region */
	isp->mmio_base[res] = ioremap_nocache(isp->mmio_base_phys[res],
					      isp->mmio_size[res]);
	if (!isp->mmio_base[res]) {
		dev_err(isp->dev, "cannot map camera register I/O region\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * isp_probe - Probe ISP platform device
 * @pdev: Pointer to ISP platform device
 *
 * Returns 0 if successful,
 *   -ENOMEM if no memory available,
 *   -ENODEV if no platform device resources found
 *     or no space for remapping registers,
 *   -EINVAL if couldn't install ISR,
 *   or clk_get return error value.
 */
static int isp_probe(struct platform_device *pdev)
{
	struct isp_platform_data *pdata = pdev->dev.platform_data;
	struct isp_device *isp;
	int ret;
	int i, m;

	if (pdata == NULL)
		return -EINVAL;

	isp = kzalloc(sizeof(*isp), GFP_KERNEL);
	if (!isp) {
		dev_err(&pdev->dev, "could not allocate memory\n");
		return -ENOMEM;
	}

	isp->autoidle = autoidle;
	isp->platform_cb.set_xclk = isp_set_xclk;
	isp->platform_cb.set_pixel_clock = isp_set_pixel_clock;

	mutex_init(&isp->isp_mutex);
	spin_lock_init(&isp->stat_lock);

	isp->dev = &pdev->dev;
	isp->pdata = pdata;
	isp->ref_count = 0;

	isp->raw_dmamask = DMA_BIT_MASK(32);
	isp->dev->dma_mask = &isp->raw_dmamask;
	isp->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, isp);

	/* Regulators */
	isp->isp_csiphy1.vdd = regulator_get(&pdev->dev, "VDD_CSIPHY1");
	isp->isp_csiphy2.vdd = regulator_get(&pdev->dev, "VDD_CSIPHY2");

	/* Clocks */
	ret = isp_map_mem_resource(pdev, isp, OMAP3_ISP_IOMEM_MAIN);
	if (ret < 0)
		goto error;

	ret = isp_get_clocks(isp);
	if (ret < 0)
		goto error;

	if (omap3isp_get(isp) == NULL)
		goto error;

	ret = isp_reset(isp);
	if (ret < 0)
		goto error_isp;

	/* Memory resources */
	isp->revision = isp_reg_readl(isp, OMAP3_ISP_IOMEM_MAIN, ISP_REVISION);
	dev_info(isp->dev, "Revision %d.%d found\n",
		 (isp->revision & 0xf0) >> 4, isp->revision & 0x0f);

	for (m = 0; m < ARRAY_SIZE(isp_res_maps); m++)
		if (isp->revision == isp_res_maps[m].isp_rev)
			break;

	if (m == ARRAY_SIZE(isp_res_maps)) {
		dev_err(isp->dev, "No resource map found for ISP rev %d.%d\n",
			(isp->revision & 0xf0) >> 4, isp->revision & 0xf);
		ret = -ENODEV;
		goto error_isp;
	}

	for (i = 1; i < OMAP3_ISP_IOMEM_LAST; i++) {
		if (isp_res_maps[m].map & 1 << i) {
			ret = isp_map_mem_resource(pdev, isp, i);
			if (ret)
				goto error_isp;
		}
	}

	isp->domain = iommu_domain_alloc(pdev->dev.bus);
	if (!isp->domain) {
		dev_err(isp->dev, "can't alloc iommu domain\n");
		ret = -ENOMEM;
		goto error_isp;
	}

	ret = iommu_attach_device(isp->domain, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "can't attach iommu device: %d\n", ret);
		goto free_domain;
	}

	/* Interrupt */
	isp->irq_num = platform_get_irq(pdev, 0);
	if (isp->irq_num <= 0) {
		dev_err(isp->dev, "No IRQ resource\n");
		ret = -ENODEV;
		goto detach_dev;
	}

	if (request_irq(isp->irq_num, isp_isr, IRQF_SHARED, "OMAP3 ISP", isp)) {
		dev_err(isp->dev, "Unable to request IRQ\n");
		ret = -EINVAL;
		goto detach_dev;
	}

	/* Entities */
	ret = isp_initialize_modules(isp);
	if (ret < 0)
		goto error_irq;

	ret = isp_register_entities(isp);
	if (ret < 0)
		goto error_modules;

	isp_power_settings(isp, 1);
	omap3isp_put(isp);

	return 0;

error_modules:
	isp_cleanup_modules(isp);
error_irq:
	free_irq(isp->irq_num, isp);
detach_dev:
	iommu_detach_device(isp->domain, &pdev->dev);
free_domain:
	iommu_domain_free(isp->domain);
error_isp:
	omap3isp_put(isp);
error:
	isp_put_clocks(isp);

	for (i = 0; i < OMAP3_ISP_IOMEM_LAST; i++) {
		if (isp->mmio_base[i]) {
			iounmap(isp->mmio_base[i]);
			isp->mmio_base[i] = NULL;
		}

		if (isp->mmio_base_phys[i]) {
			release_mem_region(isp->mmio_base_phys[i],
					   isp->mmio_size[i]);
			isp->mmio_base_phys[i] = 0;
		}
	}
	regulator_put(isp->isp_csiphy2.vdd);
	regulator_put(isp->isp_csiphy1.vdd);
	platform_set_drvdata(pdev, NULL);

	mutex_destroy(&isp->isp_mutex);
	kfree(isp);

	return ret;
}

static const struct dev_pm_ops omap3isp_pm_ops = {
	.prepare = isp_pm_prepare,
	.suspend = isp_pm_suspend,
	.resume = isp_pm_resume,
	.complete = isp_pm_complete,
};

static struct platform_device_id omap3isp_id_table[] = {
	{ "omap3isp", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, omap3isp_id_table);

static struct platform_driver omap3isp_driver = {
	.probe = isp_probe,
	.remove = isp_remove,
	.id_table = omap3isp_id_table,
	.driver = {
		.owner = THIS_MODULE,
		.name = "omap3isp",
		.pm	= &omap3isp_pm_ops,
	},
};

module_platform_driver(omap3isp_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("TI OMAP3 ISP driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(ISP_VIDEO_DRIVER_VERSION);
