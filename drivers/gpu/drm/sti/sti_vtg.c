/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          Vincent Abriou <vincent.abriou@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>

#include "sti_vtg.h"

#define VTG_TYPE_MASTER         0
#define VTG_TYPE_SLAVE_BY_EXT0  1

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
#define HDMI_DELAY          (6)

/* delay introduced by the Arbitrary Waveform Generator in nb of pixels */
#define AWG_DELAY_HD        (-9)
#define AWG_DELAY_ED        (-8)
#define AWG_DELAY_SD        (-7)

LIST_HEAD(vtg_lookup);

/**
 * STI VTG structure
 *
 * @dev: pointer to device driver
 * @data: data associated to the device
 * @irq: VTG irq
 * @type: VTG type (main or aux)
 * @notifier_list: notifier callback
 * @crtc_id: the crtc id for vblank event
 * @slave: slave vtg
 * @link: List node to link the structure in lookup list
 */
struct sti_vtg {
	struct device *dev;
	struct device_node *np;
	void __iomem *regs;
	int irq;
	u32 irq_status;
	struct raw_notifier_head notifier_list;
	int crtc_id;
	struct sti_vtg *slave;
	struct list_head link;
};

static void vtg_register(struct sti_vtg *vtg)
{
	list_add_tail(&vtg->link, &vtg_lookup);
}

struct sti_vtg *of_vtg_find(struct device_node *np)
{
	struct sti_vtg *vtg;

	list_for_each_entry(vtg, &vtg_lookup, link) {
		if (vtg->np == np)
			return vtg;
	}
	return NULL;
}
EXPORT_SYMBOL(of_vtg_find);

static void vtg_reset(struct sti_vtg *vtg)
{
	/* reset slave and then master */
	if (vtg->slave)
		vtg_reset(vtg->slave);

	writel(1, vtg->regs + VTG_DRST_AUTOC);
}

static void vtg_set_mode(struct sti_vtg *vtg,
			 int type, const struct drm_display_mode *mode)
{
	u32 tmp;

	if (vtg->slave)
		vtg_set_mode(vtg->slave, VTG_TYPE_SLAVE_BY_EXT0, mode);

	writel(mode->htotal, vtg->regs + VTG_CLKLN);
	writel(mode->vtotal * 2, vtg->regs + VTG_HLFLN);

	tmp = (mode->vtotal - mode->vsync_start + 1) << 16;
	tmp |= mode->htotal - mode->hsync_start;
	writel(tmp, vtg->regs + VTG_VID_TFO);
	writel(tmp, vtg->regs + VTG_VID_BFO);

	tmp = (mode->vdisplay + mode->vtotal - mode->vsync_start + 1) << 16;
	tmp |= mode->hdisplay + mode->htotal - mode->hsync_start;
	writel(tmp, vtg->regs + VTG_VID_TFS);
	writel(tmp, vtg->regs + VTG_VID_BFS);

	/* prepare VTG set 1 for HDMI */
	tmp = (mode->hsync_end - mode->hsync_start + HDMI_DELAY) << 16;
	tmp |= HDMI_DELAY;
	writel(tmp, vtg->regs + VTG_H_HD_1);

	tmp = (mode->vsync_end - mode->vsync_start + 1) << 16;
	tmp |= 1;
	writel(tmp, vtg->regs + VTG_TOP_V_VD_1);
	writel(tmp, vtg->regs + VTG_BOT_V_VD_1);
	writel(0, vtg->regs + VTG_TOP_V_HD_1);
	writel(0, vtg->regs + VTG_BOT_V_HD_1);

	/* prepare VTG set 2 for for HD DCS */
	tmp = (mode->hsync_end - mode->hsync_start) << 16;
	writel(tmp, vtg->regs + VTG_H_HD_2);

	tmp = (mode->vsync_end - mode->vsync_start + 1) << 16;
	tmp |= 1;
	writel(tmp, vtg->regs + VTG_TOP_V_VD_2);
	writel(tmp, vtg->regs + VTG_BOT_V_VD_2);
	writel(0, vtg->regs + VTG_TOP_V_HD_2);
	writel(0, vtg->regs + VTG_BOT_V_HD_2);

	/* prepare VTG set 3 for HD Analog in HD mode */
	tmp = (mode->hsync_end - mode->hsync_start + AWG_DELAY_HD) << 16;
	tmp |= mode->htotal + AWG_DELAY_HD;
	writel(tmp, vtg->regs + VTG_H_HD_3);

	tmp = (mode->vsync_end - mode->vsync_start) << 16;
	tmp |= mode->vtotal;
	writel(tmp, vtg->regs + VTG_TOP_V_VD_3);
	writel(tmp, vtg->regs + VTG_BOT_V_VD_3);

	tmp = (mode->htotal + AWG_DELAY_HD) << 16;
	tmp |= mode->htotal + AWG_DELAY_HD;
	writel(tmp, vtg->regs + VTG_TOP_V_HD_3);
	writel(tmp, vtg->regs + VTG_BOT_V_HD_3);

	/* Prepare VTG set 4 for DVO */
	tmp = (mode->hsync_end - mode->hsync_start) << 16;
	writel(tmp, vtg->regs + VTG_H_HD_4);

	tmp = (mode->vsync_end - mode->vsync_start + 1) << 16;
	tmp |= 1;
	writel(tmp, vtg->regs + VTG_TOP_V_VD_4);
	writel(tmp, vtg->regs + VTG_BOT_V_VD_4);
	writel(0, vtg->regs + VTG_TOP_V_HD_4);
	writel(0, vtg->regs + VTG_BOT_V_HD_4);

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
	vtg_set_mode(vtg, VTG_TYPE_MASTER, mode);

	vtg_reset(vtg);

	/* enable irq for the vtg vblank synchro */
	if (vtg->slave)
		vtg_enable_irq(vtg->slave);
	else
		vtg_enable_irq(vtg);
}
EXPORT_SYMBOL(sti_vtg_set_config);

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
EXPORT_SYMBOL(sti_vtg_get_line_number);

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
EXPORT_SYMBOL(sti_vtg_get_pixel_number);

int sti_vtg_register_client(struct sti_vtg *vtg,
		struct notifier_block *nb, int crtc_id)
{
	if (vtg->slave)
		return sti_vtg_register_client(vtg->slave, nb, crtc_id);

	vtg->crtc_id = crtc_id;
	return raw_notifier_chain_register(&vtg->notifier_list, nb);
}
EXPORT_SYMBOL(sti_vtg_register_client);

int sti_vtg_unregister_client(struct sti_vtg *vtg, struct notifier_block *nb)
{
	if (vtg->slave)
		return sti_vtg_unregister_client(vtg->slave, nb);

	return raw_notifier_chain_unregister(&vtg->notifier_list, nb);
}
EXPORT_SYMBOL(sti_vtg_unregister_client);

static irqreturn_t vtg_irq_thread(int irq, void *arg)
{
	struct sti_vtg *vtg = arg;
	u32 event;

	event = (vtg->irq_status & VTG_IRQ_TOP) ?
		VTG_TOP_FIELD_EVENT : VTG_BOTTOM_FIELD_EVENT;

	raw_notifier_call_chain(&vtg->notifier_list, event, &vtg->crtc_id);

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
	struct device_node *np;
	struct sti_vtg *vtg;
	struct resource *res;
	char irq_name[32];
	int ret;

	vtg = devm_kzalloc(dev, sizeof(*vtg), GFP_KERNEL);
	if (!vtg)
		return -ENOMEM;

	vtg->dev = dev;
	vtg->np = pdev->dev.of_node;

	/* Get Memory ressources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		DRM_ERROR("Get memory resource failed\n");
		return -ENOMEM;
	}
	vtg->regs = devm_ioremap_nocache(dev, res->start, resource_size(res));

	np = of_parse_phandle(pdev->dev.of_node, "st,slave", 0);
	if (np) {
		vtg->slave = of_vtg_find(np);

		if (!vtg->slave)
			return -EPROBE_DEFER;
	} else {
		vtg->irq = platform_get_irq(pdev, 0);
		if (IS_ERR_VALUE(vtg->irq)) {
			DRM_ERROR("Failed to get VTG interrupt\n");
			return vtg->irq;
		}

		snprintf(irq_name, sizeof(irq_name), "vsync-%s",
				dev_name(vtg->dev));

		RAW_INIT_NOTIFIER_HEAD(&vtg->notifier_list);

		ret = devm_request_threaded_irq(dev, vtg->irq, vtg_irq,
				vtg_irq_thread, IRQF_ONESHOT, irq_name, vtg);
		if (IS_ERR_VALUE(ret)) {
			DRM_ERROR("Failed to register VTG interrupt\n");
			return ret;
		}
	}

	vtg_register(vtg);
	platform_set_drvdata(pdev, vtg);

	DRM_INFO("%s %s\n", __func__, dev_name(vtg->dev));

	return 0;
}

static int vtg_remove(struct platform_device *pdev)
{
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
		.owner = THIS_MODULE,
		.of_match_table = vtg_of_match,
	},
	.probe	= vtg_probe,
	.remove = vtg_remove,
};

module_platform_driver(sti_vtg_driver);

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
