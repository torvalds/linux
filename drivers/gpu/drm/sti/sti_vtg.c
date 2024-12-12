// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Vincent Abriou <vincent.abriou@st.com>
 *          for STMicroelectronics.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <drm/drm_modes.h>
#include <drm/drm_print.h>

#include "sti_drv.h"
#include "sti_vtg.h"

#define VTG_MODE_MASTER         0

/* registers offset */
#define VTG_MODE            0x0000
#define VTG_CLKLN           0x0008
#define VTG_HLFLN           0x000C
#define VTG_DRST_AUTOC      0x0010
#define VTG_VID_TFO         0x0040
#define VTG_VID_TFS         0x0044
#define VTG_VID_BFO         0x0048
#define VTG_VID_BFS         0x004C

#define VTG_HOST_ITS        0x0078
#define VTG_HOST_ITS_BCLR   0x007C
#define VTG_HOST_ITM_BCLR   0x0088
#define VTG_HOST_ITM_BSET   0x008C

#define VTG_H_HD_1          0x00C0
#define VTG_TOP_V_VD_1      0x00C4
#define VTG_BOT_V_VD_1      0x00C8
#define VTG_TOP_V_HD_1      0x00CC
#define VTG_BOT_V_HD_1      0x00D0

#define VTG_H_HD_2          0x00E0
#define VTG_TOP_V_VD_2      0x00E4
#define VTG_BOT_V_VD_2      0x00E8
#define VTG_TOP_V_HD_2      0x00EC
#define VTG_BOT_V_HD_2      0x00F0

#define VTG_H_HD_3          0x0100
#define VTG_TOP_V_VD_3      0x0104
#define VTG_BOT_V_VD_3      0x0108
#define VTG_TOP_V_HD_3      0x010C
#define VTG_BOT_V_HD_3      0x0110

#define VTG_H_HD_4          0x0120
#define VTG_TOP_V_VD_4      0x0124
#define VTG_BOT_V_VD_4      0x0128
#define VTG_TOP_V_HD_4      0x012c
#define VTG_BOT_V_HD_4      0x0130

#define VTG_IRQ_BOTTOM      BIT(0)
#define VTG_IRQ_TOP         BIT(1)
#define VTG_IRQ_MASK        (VTG_IRQ_TOP | VTG_IRQ_BOTTOM)

/* Delay introduced by the HDMI in nb of pixel */
#define HDMI_DELAY          (5)

/* Delay introduced by the DVO in nb of pixel */
#define DVO_DELAY           (7)

/* delay introduced by the Arbitrary Waveform Generator in nb of pixels */
#define AWG_DELAY_HD        (-9)
#define AWG_DELAY_ED        (-8)
#define AWG_DELAY_SD        (-7)

/*
 * STI VTG register offset structure
 *
 *@h_hd:     stores the VTG_H_HD_x     register offset
 *@top_v_vd: stores the VTG_TOP_V_VD_x register offset
 *@bot_v_vd: stores the VTG_BOT_V_VD_x register offset
 *@top_v_hd: stores the VTG_TOP_V_HD_x register offset
 *@bot_v_hd: stores the VTG_BOT_V_HD_x register offset
 */
struct sti_vtg_regs_offs {
	u32 h_hd;
	u32 top_v_vd;
	u32 bot_v_vd;
	u32 top_v_hd;
	u32 bot_v_hd;
};

#define VTG_MAX_SYNC_OUTPUT 4
static const struct sti_vtg_regs_offs vtg_regs_offs[VTG_MAX_SYNC_OUTPUT] = {
	{ VTG_H_HD_1,
	  VTG_TOP_V_VD_1, VTG_BOT_V_VD_1, VTG_TOP_V_HD_1, VTG_BOT_V_HD_1 },
	{ VTG_H_HD_2,
	  VTG_TOP_V_VD_2, VTG_BOT_V_VD_2, VTG_TOP_V_HD_2, VTG_BOT_V_HD_2 },
	{ VTG_H_HD_3,
	  VTG_TOP_V_VD_3, VTG_BOT_V_VD_3, VTG_TOP_V_HD_3, VTG_BOT_V_HD_3 },
	{ VTG_H_HD_4,
	  VTG_TOP_V_VD_4, VTG_BOT_V_VD_4, VTG_TOP_V_HD_4, VTG_BOT_V_HD_4 }
};

/*
 * STI VTG synchronisation parameters structure
 *
 *@hsync: sample number falling and rising edge
 *@vsync_line_top: vertical top field line number falling and rising edge
 *@vsync_line_bot: vertical bottom field line number falling and rising edge
 *@vsync_off_top: vertical top field sample number rising and falling edge
 *@vsync_off_bot: vertical bottom field sample number rising and falling edge
 */
struct sti_vtg_sync_params {
	u32 hsync;
	u32 vsync_line_top;
	u32 vsync_line_bot;
	u32 vsync_off_top;
	u32 vsync_off_bot;
};

/*
 * STI VTG structure
 *
 * @regs: register mapping
 * @sync_params: synchronisation parameters used to generate timings
 * @irq: VTG irq
 * @irq_status: store the IRQ status value
 * @notifier_list: notifier callback
 * @crtc: the CRTC for vblank event
 */
struct sti_vtg {
	void __iomem *regs;
	struct sti_vtg_sync_params sync_params[VTG_MAX_SYNC_OUTPUT];
	int irq;
	u32 irq_status;
	struct raw_notifier_head notifier_list;
	struct drm_crtc *crtc;
};

struct sti_vtg *of_vtg_find(struct device_node *np)
{
	struct platform_device *pdev;

	pdev = of_find_device_by_node(np);
	if (!pdev)
		return NULL;

	return (struct sti_vtg *)platform_get_drvdata(pdev);
}

static void vtg_reset(struct sti_vtg *vtg)
{
	writel(1, vtg->regs + VTG_DRST_AUTOC);
}

static void vtg_set_output_window(void __iomem *regs,
				  const struct drm_display_mode *mode)
{
	u32 video_top_field_start;
	u32 video_top_field_stop;
	u32 video_bottom_field_start;
	u32 video_bottom_field_stop;
	u32 xstart = sti_vtg_get_pixel_number(*mode, 0);
	u32 ystart = sti_vtg_get_line_number(*mode, 0);
	u32 xstop = sti_vtg_get_pixel_number(*mode, mode->hdisplay - 1);
	u32 ystop = sti_vtg_get_line_number(*mode, mode->vdisplay - 1);

	/* Set output window to fit the display mode selected */
	video_top_field_start = (ystart << 16) | xstart;
	video_top_field_stop = (ystop << 16) | xstop;

	/* Only progressive supported for now */
	video_bottom_field_start = video_top_field_start;
	video_bottom_field_stop = video_top_field_stop;

	writel(video_top_field_start, regs + VTG_VID_TFO);
	writel(video_top_field_stop, regs + VTG_VID_TFS);
	writel(video_bottom_field_start, regs + VTG_VID_BFO);
	writel(video_bottom_field_stop, regs + VTG_VID_BFS);
}

static void vtg_set_hsync_vsync_pos(struct sti_vtg_sync_params *sync,
				    int delay,
				    const struct drm_display_mode *mode)
{
	long clocksperline, start, stop;
	u32 risesync_top, fallsync_top;
	u32 risesync_offs_top, fallsync_offs_top;

	clocksperline = mode->htotal;

	/* Get the hsync position */
	start = 0;
	stop = mode->hsync_end - mode->hsync_start;

	start += delay;
	stop  += delay;

	if (start < 0)
		start += clocksperline;
	else if (start >= clocksperline)
		start -= clocksperline;

	if (stop < 0)
		stop += clocksperline;
	else if (stop >= clocksperline)
		stop -= clocksperline;

	sync->hsync = (stop << 16) | start;

	/* Get the vsync position */
	if (delay >= 0) {
		risesync_top = 1;
		fallsync_top = risesync_top;
		fallsync_top += mode->vsync_end - mode->vsync_start;

		fallsync_offs_top = (u32)delay;
		risesync_offs_top = (u32)delay;
	} else {
		risesync_top = mode->vtotal;
		fallsync_top = mode->vsync_end - mode->vsync_start;

		fallsync_offs_top = clocksperline + delay;
		risesync_offs_top = clocksperline + delay;
	}

	sync->vsync_line_top = (fallsync_top << 16) | risesync_top;
	sync->vsync_off_top = (fallsync_offs_top << 16) | risesync_offs_top;

	/* Only progressive supported for now */
	sync->vsync_line_bot = sync->vsync_line_top;
	sync->vsync_off_bot = sync->vsync_off_top;
}

static void vtg_set_mode(struct sti_vtg *vtg,
			 int type,
			 struct sti_vtg_sync_params *sync,
			 const struct drm_display_mode *mode)
{
	unsigned int i;

	/* Set the number of clock cycles per line */
	writel(mode->htotal, vtg->regs + VTG_CLKLN);

	/* Set Half Line Per Field (only progressive supported for now) */
	writel(mode->vtotal * 2, vtg->regs + VTG_HLFLN);

	/* Program output window */
	vtg_set_output_window(vtg->regs, mode);

	/* Set hsync and vsync position for HDMI */
	vtg_set_hsync_vsync_pos(&sync[VTG_SYNC_ID_HDMI - 1], HDMI_DELAY, mode);

	/* Set hsync and vsync position for HD DCS */
	vtg_set_hsync_vsync_pos(&sync[VTG_SYNC_ID_HDDCS - 1], 0, mode);

	/* Set hsync and vsync position for HDF */
	vtg_set_hsync_vsync_pos(&sync[VTG_SYNC_ID_HDF - 1], AWG_DELAY_HD, mode);

	/* Set hsync and vsync position for DVO */
	vtg_set_hsync_vsync_pos(&sync[VTG_SYNC_ID_DVO - 1], DVO_DELAY, mode);

	/* Progam the syncs outputs */
	for (i = 0; i < VTG_MAX_SYNC_OUTPUT ; i++) {
		writel(sync[i].hsync,
		       vtg->regs + vtg_regs_offs[i].h_hd);
		writel(sync[i].vsync_line_top,
		       vtg->regs + vtg_regs_offs[i].top_v_vd);
		writel(sync[i].vsync_line_bot,
		       vtg->regs + vtg_regs_offs[i].bot_v_vd);
		writel(sync[i].vsync_off_top,
		       vtg->regs + vtg_regs_offs[i].top_v_hd);
		writel(sync[i].vsync_off_bot,
		       vtg->regs + vtg_regs_offs[i].bot_v_hd);
	}

	/* mode */
	writel(type, vtg->regs + VTG_MODE);
}

static void vtg_enable_irq(struct sti_vtg *vtg)
{
	/* clear interrupt status and mask */
	writel(0xFFFF, vtg->regs + VTG_HOST_ITS_BCLR);
	writel(0xFFFF, vtg->regs + VTG_HOST_ITM_BCLR);
	writel(VTG_IRQ_MASK, vtg->regs + VTG_HOST_ITM_BSET);
}

void sti_vtg_set_config(struct sti_vtg *vtg,
		const struct drm_display_mode *mode)
{
	/* write configuration */
	vtg_set_mode(vtg, VTG_MODE_MASTER, vtg->sync_params, mode);

	vtg_reset(vtg);

	vtg_enable_irq(vtg);
}

/**
 * sti_vtg_get_line_number
 *
 * @mode: display mode to be used
 * @y:    line
 *
 * Return the line number according to the display mode taking
 * into account the Sync and Back Porch information.
 * Video frame line numbers start at 1, y starts at 0.
 * In interlaced modes the start line is the field line number of the odd
 * field, but y is still defined as a progressive frame.
 */
u32 sti_vtg_get_line_number(struct drm_display_mode mode, int y)
{
	u32 start_line = mode.vtotal - mode.vsync_start + 1;

	if (mode.flags & DRM_MODE_FLAG_INTERLACE)
		start_line *= 2;

	return start_line + y;
}

/**
 * sti_vtg_get_pixel_number
 *
 * @mode: display mode to be used
 * @x:    row
 *
 * Return the pixel number according to the display mode taking
 * into account the Sync and Back Porch information.
 * Pixels are counted from 0.
 */
u32 sti_vtg_get_pixel_number(struct drm_display_mode mode, int x)
{
	return mode.htotal - mode.hsync_start + x;
}

int sti_vtg_register_client(struct sti_vtg *vtg, struct notifier_block *nb,
			    struct drm_crtc *crtc)
{
	vtg->crtc = crtc;
	return raw_notifier_chain_register(&vtg->notifier_list, nb);
}

int sti_vtg_unregister_client(struct sti_vtg *vtg, struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&vtg->notifier_list, nb);
}

static irqreturn_t vtg_irq_thread(int irq, void *arg)
{
	struct sti_vtg *vtg = arg;
	u32 event;

	event = (vtg->irq_status & VTG_IRQ_TOP) ?
		VTG_TOP_FIELD_EVENT : VTG_BOTTOM_FIELD_EVENT;

	raw_notifier_call_chain(&vtg->notifier_list, event, vtg->crtc);

	return IRQ_HANDLED;
}

static irqreturn_t vtg_irq(int irq, void *arg)
{
	struct sti_vtg *vtg = arg;

	vtg->irq_status = readl(vtg->regs + VTG_HOST_ITS);

	writel(vtg->irq_status, vtg->regs + VTG_HOST_ITS_BCLR);

	/* force sync bus write */
	readl(vtg->regs + VTG_HOST_ITS);

	return IRQ_WAKE_THREAD;
}

static int vtg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sti_vtg *vtg;
	struct resource *res;
	int ret;

	vtg = devm_kzalloc(dev, sizeof(*vtg), GFP_KERNEL);
	if (!vtg)
		return -ENOMEM;

	/* Get Memory ressources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		DRM_ERROR("Get memory resource failed\n");
		return -ENOMEM;
	}
	vtg->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vtg->regs) {
		DRM_ERROR("failed to remap I/O memory\n");
		return -ENOMEM;
	}

	vtg->irq = platform_get_irq(pdev, 0);
	if (vtg->irq < 0) {
		DRM_ERROR("Failed to get VTG interrupt\n");
		return vtg->irq;
	}

	RAW_INIT_NOTIFIER_HEAD(&vtg->notifier_list);

	ret = devm_request_threaded_irq(dev, vtg->irq, vtg_irq,
					vtg_irq_thread, IRQF_ONESHOT,
					dev_name(dev), vtg);
	if (ret < 0) {
		DRM_ERROR("Failed to register VTG interrupt\n");
		return ret;
	}

	platform_set_drvdata(pdev, vtg);

	DRM_INFO("%s %s\n", __func__, dev_name(dev));

	return 0;
}

static const struct of_device_id vtg_of_match[] = {
	{ .compatible = "st,vtg", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vtg_of_match);

struct platform_driver sti_vtg_driver = {
	.driver = {
		.name = "sti-vtg",
		.of_match_table = vtg_of_match,
	},
	.probe	= vtg_probe,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
