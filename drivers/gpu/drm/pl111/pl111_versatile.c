#include <linux/device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "pl111_versatile.h"
#include "pl111_drm.h"

static struct regmap *versatile_syscon_map;

/*
 * We detect the different syscon types from the compatible strings.
 */
enum versatile_clcd {
	INTEGRATOR_CLCD_CM,
	VERSATILE_CLCD,
	REALVIEW_CLCD_EB,
	REALVIEW_CLCD_PB1176,
	REALVIEW_CLCD_PB11MP,
	REALVIEW_CLCD_PBA8,
	REALVIEW_CLCD_PBX,
};

static const struct of_device_id versatile_clcd_of_match[] = {
	{
		.compatible = "arm,core-module-integrator",
		.data = (void *)INTEGRATOR_CLCD_CM,
	},
	{
		.compatible = "arm,versatile-sysreg",
		.data = (void *)VERSATILE_CLCD,
	},
	{
		.compatible = "arm,realview-eb-syscon",
		.data = (void *)REALVIEW_CLCD_EB,
	},
	{
		.compatible = "arm,realview-pb1176-syscon",
		.data = (void *)REALVIEW_CLCD_PB1176,
	},
	{
		.compatible = "arm,realview-pb11mp-syscon",
		.data = (void *)REALVIEW_CLCD_PB11MP,
	},
	{
		.compatible = "arm,realview-pba8-syscon",
		.data = (void *)REALVIEW_CLCD_PBA8,
	},
	{
		.compatible = "arm,realview-pbx-syscon",
		.data = (void *)REALVIEW_CLCD_PBX,
	},
	{},
};

/*
 * Core module CLCD control on the Integrator/CP, bits
 * 8 thru 19 of the CM_CONTROL register controls a bunch
 * of CLCD settings.
 */
#define INTEGRATOR_HDR_CTRL_OFFSET	0x0C
#define INTEGRATOR_CLCD_LCDBIASEN	BIT(8)
#define INTEGRATOR_CLCD_LCDBIASUP	BIT(9)
#define INTEGRATOR_CLCD_LCDBIASDN	BIT(10)
/* Bits 11,12,13 controls the LCD type */
#define INTEGRATOR_CLCD_LCDMUX_MASK	(BIT(11)|BIT(12)|BIT(13))
#define INTEGRATOR_CLCD_LCDMUX_LCD24	BIT(11)
#define INTEGRATOR_CLCD_LCDMUX_VGA565	BIT(12)
#define INTEGRATOR_CLCD_LCDMUX_SHARP	(BIT(11)|BIT(12))
#define INTEGRATOR_CLCD_LCDMUX_VGA555	BIT(13)
#define INTEGRATOR_CLCD_LCDMUX_VGA24	(BIT(11)|BIT(12)|BIT(13))
#define INTEGRATOR_CLCD_LCD0_EN		BIT(14)
#define INTEGRATOR_CLCD_LCD1_EN		BIT(15)
/* R/L flip on Sharp */
#define INTEGRATOR_CLCD_LCD_STATIC1	BIT(16)
/* U/D flip on Sharp */
#define INTEGRATOR_CLCD_LCD_STATIC2	BIT(17)
/* No connection on Sharp */
#define INTEGRATOR_CLCD_LCD_STATIC	BIT(18)
/* 0 = 24bit VGA, 1 = 18bit VGA */
#define INTEGRATOR_CLCD_LCD_N24BITEN	BIT(19)

#define INTEGRATOR_CLCD_MASK		(INTEGRATOR_CLCD_LCDBIASEN | \
					 INTEGRATOR_CLCD_LCDBIASUP | \
					 INTEGRATOR_CLCD_LCDBIASDN | \
					 INTEGRATOR_CLCD_LCDMUX_MASK | \
					 INTEGRATOR_CLCD_LCD0_EN | \
					 INTEGRATOR_CLCD_LCD1_EN | \
					 INTEGRATOR_CLCD_LCD_STATIC1 | \
					 INTEGRATOR_CLCD_LCD_STATIC2 | \
					 INTEGRATOR_CLCD_LCD_STATIC | \
					 INTEGRATOR_CLCD_LCD_N24BITEN)

static void pl111_integrator_enable(struct drm_device *drm, u32 format)
{
	u32 val;

	dev_info(drm->dev, "enable Integrator CLCD connectors\n");

	/* FIXME: really needed? */
	val = INTEGRATOR_CLCD_LCD_STATIC1 | INTEGRATOR_CLCD_LCD_STATIC2 |
		INTEGRATOR_CLCD_LCD0_EN | INTEGRATOR_CLCD_LCD1_EN;

	switch (format) {
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
		break;
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB565:
		/* truecolor RGB565 */
		val |= INTEGRATOR_CLCD_LCDMUX_VGA565;
		break;
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_XRGB1555:
		/* Pseudocolor, RGB555, BGR555 */
		val |= INTEGRATOR_CLCD_LCDMUX_VGA555;
		break;
	default:
		dev_err(drm->dev, "unhandled format on Integrator 0x%08x\n",
			format);
		break;
	}

	regmap_update_bits(versatile_syscon_map,
			   INTEGRATOR_HDR_CTRL_OFFSET,
			   INTEGRATOR_CLCD_MASK,
			   val);
}

/*
 * This configuration register in the Versatile and RealView
 * family is uniformly present but appears more and more
 * unutilized starting with the RealView series.
 */
#define SYS_CLCD			0x50
#define SYS_CLCD_MODE_MASK		(BIT(0)|BIT(1))
#define SYS_CLCD_MODE_888		0
#define SYS_CLCD_MODE_5551		BIT(0)
#define SYS_CLCD_MODE_565_R_LSB		BIT(1)
#define SYS_CLCD_MODE_565_B_LSB		(BIT(0)|BIT(1))
#define SYS_CLCD_CONNECTOR_MASK		(BIT(2)|BIT(3)|BIT(4)|BIT(5))
#define SYS_CLCD_NLCDIOON		BIT(2)
#define SYS_CLCD_VDDPOSSWITCH		BIT(3)
#define SYS_CLCD_PWR3V5SWITCH		BIT(4)
#define SYS_CLCD_VDDNEGSWITCH		BIT(5)

static void pl111_versatile_disable(struct drm_device *drm)
{
	dev_info(drm->dev, "disable Versatile CLCD connectors\n");
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   0);
}

static void pl111_versatile_enable(struct drm_device *drm, u32 format)
{
	u32 val = 0;

	dev_info(drm->dev, "enable Versatile CLCD connectors\n");

	switch (format) {
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		val |= SYS_CLCD_MODE_888;
		break;
	case DRM_FORMAT_BGR565:
		val |= SYS_CLCD_MODE_565_R_LSB;
		break;
	case DRM_FORMAT_RGB565:
		val |= SYS_CLCD_MODE_565_B_LSB;
		break;
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		val |= SYS_CLCD_MODE_5551;
		break;
	default:
		dev_err(drm->dev, "unhandled format on Versatile 0x%08x\n",
			format);
		break;
	}

	/* Set up the MUX */
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_MODE_MASK,
			   val);

	/* Then enable the display */
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH);
}

static void pl111_realview_clcd_disable(struct drm_device *drm)
{
	dev_info(drm->dev, "disable RealView CLCD connectors\n");
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   0);
}

static void pl111_realview_clcd_enable(struct drm_device *drm, u32 format)
{
	dev_info(drm->dev, "enable RealView CLCD connectors\n");
	regmap_update_bits(versatile_syscon_map,
			   SYS_CLCD,
			   SYS_CLCD_CONNECTOR_MASK,
			   SYS_CLCD_NLCDIOON | SYS_CLCD_PWR3V5SWITCH);
}

int pl111_versatile_init(struct device *dev, struct pl111_drm_dev_private *priv)
{
	const struct of_device_id *clcd_id;
	enum versatile_clcd versatile_clcd_type;
	struct device_node *np;
	struct regmap *map;

	np = of_find_matching_node_and_match(NULL, versatile_clcd_of_match,
					     &clcd_id);
	if (!np) {
		/* Non-ARM reference designs, just bail out */
		return 0;
	}
	versatile_clcd_type = (enum versatile_clcd)clcd_id->data;

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		dev_err(dev, "no Versatile syscon regmap\n");
		return PTR_ERR(map);
	}

	switch (versatile_clcd_type) {
	case INTEGRATOR_CLCD_CM:
		versatile_syscon_map = map;
		priv->variant_display_enable = pl111_integrator_enable;
		dev_info(dev, "set up callbacks for Integrator PL110\n");
		break;
	case VERSATILE_CLCD:
		versatile_syscon_map = map;
		priv->variant_display_enable = pl111_versatile_enable;
		priv->variant_display_disable = pl111_versatile_disable;
		dev_info(dev, "set up callbacks for Versatile PL110+\n");
		break;
	case REALVIEW_CLCD_EB:
	case REALVIEW_CLCD_PB1176:
	case REALVIEW_CLCD_PB11MP:
	case REALVIEW_CLCD_PBA8:
	case REALVIEW_CLCD_PBX:
		versatile_syscon_map = map;
		priv->variant_display_enable = pl111_realview_clcd_enable;
		priv->variant_display_disable = pl111_realview_clcd_disable;
		dev_info(dev, "set up callbacks for RealView PL111\n");
		break;
	default:
		dev_info(dev, "unknown Versatile system controller\n");
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pl111_versatile_init);
