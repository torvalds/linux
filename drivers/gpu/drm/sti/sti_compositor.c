/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/component.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drmP.h>

#include "sti_compositor.h"
#include "sti_crtc.h"
#include "sti_cursor.h"
#include "sti_drv.h"
#include "sti_gdp.h"
#include "sti_plane.h"
#include "sti_vid.h"
#include "sti_vtg.h"

/*
 * stiH407 compositor properties
 */
struct sti_compositor_data stih407_compositor_data = {
	.nb_subdev = 8,
	.subdev_desc = {
			{STI_CURSOR_SUBDEV, (int)STI_CURSOR, 0x000},
			{STI_GPD_SUBDEV, (int)STI_GDP_0, 0x100},
			{STI_GPD_SUBDEV, (int)STI_GDP_1, 0x200},
			{STI_GPD_SUBDEV, (int)STI_GDP_2, 0x300},
			{STI_GPD_SUBDEV, (int)STI_GDP_3, 0x400},
			{STI_VID_SUBDEV, (int)STI_HQVDP_0, 0x700},
			{STI_MIXER_MAIN_SUBDEV, STI_MIXER_MAIN, 0xC00},
			{STI_MIXER_AUX_SUBDEV, STI_MIXER_AUX, 0xD00},
	},
};

/*
 * stiH416 compositor properties
 * Note:
 * on stih416 MIXER_AUX has a different base address from MIXER_MAIN
 * Moreover, GDPx is different for Main and Aux Mixer. So this subdev map does
 * not fit for stiH416 if we want to enable the MIXER_AUX.
 */
struct sti_compositor_data stih416_compositor_data = {
	.nb_subdev = 3,
	.subdev_desc = {
			{STI_GPD_SUBDEV, (int)STI_GDP_0, 0x100},
			{STI_GPD_SUBDEV, (int)STI_GDP_1, 0x200},
			{STI_MIXER_MAIN_SUBDEV, STI_MIXER_MAIN, 0xC00}
	},
};

static int sti_compositor_bind(struct device *dev,
			       struct device *master,
			       void *data)
{
	struct sti_compositor *compo = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	unsigned int i, mixer_id = 0, vid_id = 0, crtc_id = 0;
	struct sti_private *dev_priv = drm_dev->dev_private;
	struct drm_plane *cursor = NULL;
	struct drm_plane *primary = NULL;
	struct sti_compositor_subdev_descriptor *desc = compo->data.subdev_desc;
	unsigned int array_size = compo->data.nb_subdev;

	dev_priv->compo = compo;

	/* Register mixer subdev and video subdev first */
	for (i = 0; i < array_size; i++) {
		switch (desc[i].type) {
		case STI_VID_SUBDEV:
			compo->vid[vid_id++] =
			    sti_vid_create(compo->dev, desc[i].id,
					   compo->regs + desc[i].offset);
			break;
		case STI_MIXER_MAIN_SUBDEV:
		case STI_MIXER_AUX_SUBDEV:
			compo->mixer[mixer_id++] =
			    sti_mixer_create(compo->dev, desc[i].id,
					     compo->regs + desc[i].offset);
			break;
		case STI_GPD_SUBDEV:
		case STI_CURSOR_SUBDEV:
			/* Nothing to do, wait for the second round */
			break;
		default:
			DRM_ERROR("Unknow subdev compoment type\n");
			return 1;
		}
	}

	/* Register the other subdevs, create crtc and planes */
	for (i = 0; i < array_size; i++) {
		enum drm_plane_type plane_type = DRM_PLANE_TYPE_OVERLAY;

		if (crtc_id < mixer_id)
			plane_type = DRM_PLANE_TYPE_PRIMARY;

		switch (desc[i].type) {
		case STI_MIXER_MAIN_SUBDEV:
		case STI_MIXER_AUX_SUBDEV:
		case STI_VID_SUBDEV:
			/* Nothing to do, already done at the first round */
			break;
		case STI_CURSOR_SUBDEV:
			cursor = sti_cursor_create(drm_dev, compo->dev,
						   desc[i].id,
						   compo->regs + desc[i].offset,
						   1);
			if (!cursor) {
				DRM_ERROR("Can't create CURSOR plane\n");
				break;
			}
			break;
		case STI_GPD_SUBDEV:
			primary = sti_gdp_create(drm_dev, compo->dev,
						 desc[i].id,
						 compo->regs + desc[i].offset,
						 (1 << mixer_id) - 1,
						 plane_type);
			if (!primary) {
				DRM_ERROR("Can't create GDP plane\n");
				break;
			}
			break;
		default:
			DRM_ERROR("Unknown subdev compoment type\n");
			return 1;
		}

		/* The first planes are reserved for primary planes*/
		if (crtc_id < mixer_id && primary) {
			sti_crtc_init(drm_dev, compo->mixer[crtc_id],
				      primary, cursor);
			crtc_id++;
			cursor = NULL;
			primary = NULL;
		}
	}

	drm_vblank_init(drm_dev, crtc_id);
	/* Allow usage of vblank without having to call drm_irq_install */
	drm_dev->irq_enabled = 1;

	return 0;
}

static void sti_compositor_unbind(struct device *dev, struct device *master,
	void *data)
{
	/* do nothing */
}

static const struct component_ops sti_compositor_ops = {
	.bind	= sti_compositor_bind,
	.unbind	= sti_compositor_unbind,
};

static const struct of_device_id compositor_of_match[] = {
	{
		.compatible = "st,stih416-compositor",
		.data = &stih416_compositor_data,
	}, {
		.compatible = "st,stih407-compositor",
		.data = &stih407_compositor_data,
	}, {
		/* end node */
	}
};
MODULE_DEVICE_TABLE(of, compositor_of_match);

static int sti_compositor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *vtg_np;
	struct sti_compositor *compo;
	struct resource *res;

	compo = devm_kzalloc(dev, sizeof(*compo), GFP_KERNEL);
	if (!compo) {
		DRM_ERROR("Failed to allocate compositor context\n");
		return -ENOMEM;
	}
	compo->dev = dev;
	compo->vtg_vblank_nb.notifier_call = sti_crtc_vblank_cb;

	/* populate data structure depending on compatibility */
	BUG_ON(!of_match_node(compositor_of_match, np)->data);

	memcpy(&compo->data, of_match_node(compositor_of_match, np)->data,
	       sizeof(struct sti_compositor_data));

	/* Get Memory ressources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		DRM_ERROR("Get memory resource failed\n");
		return -ENXIO;
	}
	compo->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (compo->regs == NULL) {
		DRM_ERROR("Register mapping failed\n");
		return -ENXIO;
	}

	/* Get clock resources */
	compo->clk_compo_main = devm_clk_get(dev, "compo_main");
	if (IS_ERR(compo->clk_compo_main)) {
		DRM_ERROR("Cannot get compo_main clock\n");
		return PTR_ERR(compo->clk_compo_main);
	}

	compo->clk_compo_aux = devm_clk_get(dev, "compo_aux");
	if (IS_ERR(compo->clk_compo_aux)) {
		DRM_ERROR("Cannot get compo_aux clock\n");
		return PTR_ERR(compo->clk_compo_aux);
	}

	compo->clk_pix_main = devm_clk_get(dev, "pix_main");
	if (IS_ERR(compo->clk_pix_main)) {
		DRM_ERROR("Cannot get pix_main clock\n");
		return PTR_ERR(compo->clk_pix_main);
	}

	compo->clk_pix_aux = devm_clk_get(dev, "pix_aux");
	if (IS_ERR(compo->clk_pix_aux)) {
		DRM_ERROR("Cannot get pix_aux clock\n");
		return PTR_ERR(compo->clk_pix_aux);
	}

	/* Get reset resources */
	compo->rst_main = devm_reset_control_get(dev, "compo-main");
	/* Take compo main out of reset */
	if (!IS_ERR(compo->rst_main))
		reset_control_deassert(compo->rst_main);

	compo->rst_aux = devm_reset_control_get(dev, "compo-aux");
	/* Take compo aux out of reset */
	if (!IS_ERR(compo->rst_aux))
		reset_control_deassert(compo->rst_aux);

	vtg_np = of_parse_phandle(pdev->dev.of_node, "st,vtg", 0);
	if (vtg_np)
		compo->vtg_main = of_vtg_find(vtg_np);

	vtg_np = of_parse_phandle(pdev->dev.of_node, "st,vtg", 1);
	if (vtg_np)
		compo->vtg_aux = of_vtg_find(vtg_np);

	platform_set_drvdata(pdev, compo);

	return component_add(&pdev->dev, &sti_compositor_ops);
}

static int sti_compositor_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sti_compositor_ops);
	return 0;
}

static struct platform_driver sti_compositor_driver = {
	.driver = {
		.name = "sti-compositor",
		.of_match_table = compositor_of_match,
	},
	.probe = sti_compositor_probe,
	.remove = sti_compositor_remove,
};

module_platform_driver(sti_compositor_driver);

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
