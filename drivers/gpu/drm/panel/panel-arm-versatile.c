// SPDX-License-Identifier: GPL-2.0
/*
 * Panel driver for the ARM Versatile family reference designs from
 * ARM Limited.
 *
 * Author:
 * Linus Walleij <linus.wallei@linaro.org>
 *
 * On the Versatile AB, these panels come mounted on daughterboards
 * named "IB1" or "IB2" (Interface Board 1 & 2 respectively.) They
 * are documented in ARM DUI 0225D Appendix C and D. These daughter
 * boards support TFT display panels.
 *
 * - The IB1 is a passive board where the display connector defines a
 *   few wires for encoding the display type for autodetection,
 *   suitable display settings can then be looked up from this setting.
 *   The magic bits can be read out from the system controller.
 *
 * - The IB2 is a more complex board intended for GSM phone development
 *   with some logic and a control register, which needs to be accessed
 *   and the board display needs to be turned on explicitly.
 *
 * On the Versatile PB, a special CLCD adaptor board is available
 * supporting the same displays as the Versatile AB, plus one more
 * Epson QCIF display.
 *
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

/*
 * This configuration register in the Versatile and RealView
 * family is uniformly present but appears more and more
 * unutilized starting with the RealView series.
 */
#define SYS_CLCD			0x50

/* The Versatile can detect the connected panel type */
#define SYS_CLCD_CLCDID_MASK		(BIT(8)|BIT(9)|BIT(10)|BIT(11)|BIT(12))
#define SYS_CLCD_ID_SANYO_3_8		(0x00 << 8)
#define SYS_CLCD_ID_SHARP_8_4		(0x01 << 8)
#define SYS_CLCD_ID_EPSON_2_2		(0x02 << 8)
#define SYS_CLCD_ID_SANYO_2_5		(0x07 << 8)
#define SYS_CLCD_ID_VGA			(0x1f << 8)

/* IB2 control register for the Versatile daughterboard */
#define IB2_CTRL			0x00
#define IB2_CTRL_LCD_SD			BIT(1) /* 1 = shut down LCD */
#define IB2_CTRL_LCD_BL_ON		BIT(0)
#define IB2_CTRL_LCD_MASK		(BIT(0)|BIT(1))

/**
 * struct versatile_panel_type - lookup struct for the supported panels
 */
struct versatile_panel_type {
	/**
	 * @name: the name of this panel
	 */
	const char *name;
	/**
	 * @magic: the magic value from the detection register
	 */
	u32 magic;
	/**
	 * @mode: the DRM display mode for this panel
	 */
	struct drm_display_mode mode;
	/**
	 * @bus_flags: the DRM bus flags for this panel e.g. inverted clock
	 */
	u32 bus_flags;
	/**
	 * @width_mm: the panel width in mm
	 */
	u32 width_mm;
	/**
	 * @height_mm: the panel height in mm
	 */
	u32 height_mm;
	/**
	 * @ib2: the panel may be connected on an IB2 daughterboard
	 */
	bool ib2;
};

/**
 * struct versatile_panel - state container for the Versatile panels
 */
struct versatile_panel {
	/**
	 * @dev: the container device
	 */
	struct device *dev;
	/**
	 * @panel: the DRM panel instance for this device
	 */
	struct drm_panel panel;
	/**
	 * @panel_type: the Versatile panel type as detected
	 */
	const struct versatile_panel_type *panel_type;
	/**
	 * @map: map to the parent syscon where the main register reside
	 */
	struct regmap *map;
	/**
	 * @ib2_map: map to the IB2 syscon, if applicable
	 */
	struct regmap *ib2_map;
};

static const struct versatile_panel_type versatile_panels[] = {
	/*
	 * Sanyo TM38QV67A02A - 3.8 inch QVGA (320x240) Color TFT
	 * found on the Versatile AB IB1 connector or the Versatile
	 * PB adaptor board connector.
	 */
	{
		.name = "Sanyo TM38QV67A02A",
		.magic = SYS_CLCD_ID_SANYO_3_8,
		.width_mm = 79,
		.height_mm = 54,
		.mode = {
			.clock = 10000,
			.hdisplay = 320,
			.hsync_start = 320 + 6,
			.hsync_end = 320 + 6 + 6,
			.htotal = 320 + 6 + 6 + 6,
			.vdisplay = 240,
			.vsync_start = 240 + 5,
			.vsync_end = 240 + 5 + 6,
			.vtotal = 240 + 5 + 6 + 5,
			.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
		},
	},
	/*
	 * Sharp LQ084V1DG21 640x480 VGA Color TFT module
	 * found on the Versatile AB IB1 connector or the Versatile
	 * PB adaptor board connector.
	 */
	{
		.name = "Sharp LQ084V1DG21",
		.magic = SYS_CLCD_ID_SHARP_8_4,
		.width_mm = 171,
		.height_mm = 130,
		.mode = {
			.clock = 25000,
			.hdisplay = 640,
			.hsync_start = 640 + 24,
			.hsync_end = 640 + 24 + 96,
			.htotal = 640 + 24 + 96 + 24,
			.vdisplay = 480,
			.vsync_start = 480 + 11,
			.vsync_end = 480 + 11 + 2,
			.vtotal = 480 + 11 + 2 + 32,
			.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
		},
	},
	/*
	 * Epson L2F50113T00 - 2.2 inch QCIF 176x220 Color TFT
	 * found on the Versatile PB adaptor board connector.
	 */
	{
		.name = "Epson L2F50113T00",
		.magic = SYS_CLCD_ID_EPSON_2_2,
		.width_mm = 34,
		.height_mm = 45,
		.mode = {
			.clock = 62500,
			.hdisplay = 176,
			.hsync_start = 176 + 2,
			.hsync_end = 176 + 2 + 3,
			.htotal = 176 + 2 + 3 + 3,
			.vdisplay = 220,
			.vsync_start = 220 + 0,
			.vsync_end = 220 + 0 + 2,
			.vtotal = 220 + 0 + 2 + 1,
			.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
	},
	/*
	 * Sanyo ALR252RGT 240x320 portrait display found on the
	 * Versatile AB IB2 daughterboard for GSM prototyping.
	 */
	{
		.name = "Sanyo ALR252RGT",
		.magic = SYS_CLCD_ID_SANYO_2_5,
		.width_mm = 37,
		.height_mm = 50,
		.mode = {
			.clock = 5400,
			.hdisplay = 240,
			.hsync_start = 240 + 10,
			.hsync_end = 240 + 10 + 10,
			.htotal = 240 + 10 + 10 + 20,
			.vdisplay = 320,
			.vsync_start = 320 + 2,
			.vsync_end = 320 + 2 + 2,
			.vtotal = 320 + 2 + 2 + 2,
			.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
		},
		.bus_flags = DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
		.ib2 = true,
	},
};

static inline struct versatile_panel *
to_versatile_panel(struct drm_panel *panel)
{
	return container_of(panel, struct versatile_panel, panel);
}

static int versatile_panel_disable(struct drm_panel *panel)
{
	struct versatile_panel *vpanel = to_versatile_panel(panel);

	/* If we're on an IB2 daughterboard, turn off display */
	if (vpanel->ib2_map) {
		dev_dbg(vpanel->dev, "disable IB2 display\n");
		regmap_update_bits(vpanel->ib2_map,
				   IB2_CTRL,
				   IB2_CTRL_LCD_MASK,
				   IB2_CTRL_LCD_SD);
	}

	return 0;
}

static int versatile_panel_enable(struct drm_panel *panel)
{
	struct versatile_panel *vpanel = to_versatile_panel(panel);

	/* If we're on an IB2 daughterboard, turn on display */
	if (vpanel->ib2_map) {
		dev_dbg(vpanel->dev, "enable IB2 display\n");
		regmap_update_bits(vpanel->ib2_map,
				   IB2_CTRL,
				   IB2_CTRL_LCD_MASK,
				   IB2_CTRL_LCD_BL_ON);
	}

	return 0;
}

static int versatile_panel_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	struct versatile_panel *vpanel = to_versatile_panel(panel);
	struct drm_display_mode *mode;

	connector->display_info.width_mm = vpanel->panel_type->width_mm;
	connector->display_info.height_mm = vpanel->panel_type->height_mm;
	connector->display_info.bus_flags = vpanel->panel_type->bus_flags;

	mode = drm_mode_duplicate(connector->dev, &vpanel->panel_type->mode);
	if (!mode)
		return -ENOMEM;
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	mode->width_mm = vpanel->panel_type->width_mm;
	mode->height_mm = vpanel->panel_type->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs versatile_panel_drm_funcs = {
	.disable = versatile_panel_disable,
	.enable = versatile_panel_enable,
	.get_modes = versatile_panel_get_modes,
};

static int versatile_panel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct versatile_panel *vpanel;
	struct device *parent;
	struct regmap *map;
	int ret;
	u32 val;
	int i;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent for versatile panel\n");
		return -ENODEV;
	}
	map = syscon_node_to_regmap(parent->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "no regmap for versatile panel parent\n");
		return PTR_ERR(map);
	}

	vpanel = devm_kzalloc(dev, sizeof(*vpanel), GFP_KERNEL);
	if (!vpanel)
		return -ENOMEM;

	ret = regmap_read(map, SYS_CLCD, &val);
	if (ret) {
		dev_err(dev, "cannot access syscon regs\n");
		return ret;
	}

	val &= SYS_CLCD_CLCDID_MASK;

	for (i = 0; i < ARRAY_SIZE(versatile_panels); i++) {
		const struct versatile_panel_type *pt;

		pt = &versatile_panels[i];
		if (pt->magic == val) {
			vpanel->panel_type = pt;
			break;
		}
	}

	/* No panel detected or VGA, let's leave this show */
	if (i == ARRAY_SIZE(versatile_panels)) {
		dev_info(dev, "no panel detected\n");
		return -ENODEV;
	}

	dev_info(dev, "detected: %s\n", vpanel->panel_type->name);
	vpanel->dev = dev;
	vpanel->map = map;

	/* Check if the panel is mounted on an IB2 daughterboard */
	if (vpanel->panel_type->ib2) {
		vpanel->ib2_map = syscon_regmap_lookup_by_compatible(
			"arm,versatile-ib2-syscon");
		if (IS_ERR(vpanel->ib2_map))
			vpanel->ib2_map = NULL;
		else
			dev_info(dev, "panel mounted on IB2 daughterboard\n");
	}

	drm_panel_init(&vpanel->panel, dev, &versatile_panel_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	drm_panel_add(&vpanel->panel);

	return 0;
}

static const struct of_device_id versatile_panel_match[] = {
	{ .compatible = "arm,versatile-tft-panel", },
	{},
};
MODULE_DEVICE_TABLE(of, versatile_panel_match);

static struct platform_driver versatile_panel_driver = {
	.probe		= versatile_panel_probe,
	.driver		= {
		.name	= "versatile-tft-panel",
		.of_match_table = versatile_panel_match,
	},
};
module_platform_driver(versatile_panel_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("ARM Versatile panel driver");
MODULE_LICENSE("GPL v2");
