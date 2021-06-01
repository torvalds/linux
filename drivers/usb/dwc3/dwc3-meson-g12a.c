// SPDX-License-Identifier: GPL-2.0
/*
 * USB Glue for Amlogic G12A SoCs
 *
 * Copyright (c) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

/*
 * The USB is organized with a glue around the DWC3 Controller IP as :
 * - Control registers for each USB2 Ports
 * - Control registers for the USB PHY layer
 * - SuperSpeed PHY can be enabled only if port is used
 * - Dynamic OTG switching with ID change interrupt
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/usb/otg.h>
#include <linux/usb/role.h>
#include <linux/regulator/consumer.h>

/* USB2 Ports Control Registers, offsets are per-port */

#define U2P_REG_SIZE						0x20

#define U2P_R0							0x0
	#define U2P_R0_HOST_DEVICE				BIT(0)
	#define U2P_R0_POWER_OK					BIT(1)
	#define U2P_R0_HAST_MODE				BIT(2)
	#define U2P_R0_POWER_ON_RESET				BIT(3)
	#define U2P_R0_ID_PULLUP				BIT(4)
	#define U2P_R0_DRV_VBUS					BIT(5)

#define U2P_R1							0x4
	#define U2P_R1_PHY_READY				BIT(0)
	#define U2P_R1_ID_DIG					BIT(1)
	#define U2P_R1_OTG_SESSION_VALID			BIT(2)
	#define U2P_R1_VBUS_VALID				BIT(3)

/* USB Glue Control Registers */

#define G12A_GLUE_OFFSET					0x80

#define USB_R0							0x00
	#define USB_R0_P30_LANE0_TX2RX_LOOPBACK			BIT(17)
	#define USB_R0_P30_LANE0_EXT_PCLK_REQ			BIT(18)
	#define USB_R0_P30_PCS_RX_LOS_MASK_VAL_MASK		GENMASK(28, 19)
	#define USB_R0_U2D_SS_SCALEDOWN_MODE_MASK		GENMASK(30, 29)
	#define USB_R0_U2D_ACT					BIT(31)

#define USB_R1							0x04
	#define USB_R1_U3H_BIGENDIAN_GS				BIT(0)
	#define USB_R1_U3H_PME_ENABLE				BIT(1)
	#define USB_R1_U3H_HUB_PORT_OVERCURRENT_MASK		GENMASK(4, 2)
	#define USB_R1_U3H_HUB_PORT_PERM_ATTACH_MASK		GENMASK(9, 7)
	#define USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK		GENMASK(13, 12)
	#define USB_R1_U3H_HOST_U3_PORT_DISABLE			BIT(16)
	#define USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT	BIT(17)
	#define USB_R1_U3H_HOST_MSI_ENABLE			BIT(18)
	#define USB_R1_U3H_FLADJ_30MHZ_REG_MASK			GENMASK(24, 19)
	#define USB_R1_P30_PCS_TX_SWING_FULL_MASK		GENMASK(31, 25)

#define USB_R2							0x08
	#define USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK		GENMASK(25, 20)
	#define USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK		GENMASK(31, 26)

#define USB_R3							0x0c
	#define USB_R3_P30_SSC_ENABLE				BIT(0)
	#define USB_R3_P30_SSC_RANGE_MASK			GENMASK(3, 1)
	#define USB_R3_P30_SSC_REF_CLK_SEL_MASK			GENMASK(12, 4)
	#define USB_R3_P30_REF_SSP_EN				BIT(13)

#define USB_R4							0x10
	#define USB_R4_P21_PORT_RESET_0				BIT(0)
	#define USB_R4_P21_SLEEP_M0				BIT(1)
	#define USB_R4_MEM_PD_MASK				GENMASK(3, 2)
	#define USB_R4_P21_ONLY					BIT(4)

#define USB_R5							0x14
	#define USB_R5_ID_DIG_SYNC				BIT(0)
	#define USB_R5_ID_DIG_REG				BIT(1)
	#define USB_R5_ID_DIG_CFG_MASK				GENMASK(3, 2)
	#define USB_R5_ID_DIG_EN_0				BIT(4)
	#define USB_R5_ID_DIG_EN_1				BIT(5)
	#define USB_R5_ID_DIG_CURR				BIT(6)
	#define USB_R5_ID_DIG_IRQ				BIT(7)
	#define USB_R5_ID_DIG_TH_MASK				GENMASK(15, 8)
	#define USB_R5_ID_DIG_CNT_MASK				GENMASK(23, 16)

#define PHY_COUNT						3
#define USB2_OTG_PHY						1

static struct clk_bulk_data meson_gxl_clocks[] = {
	{ .id = "usb_ctrl" },
	{ .id = "ddr" },
};

static struct clk_bulk_data meson_g12a_clocks[] = {
	{ .id = NULL },
};

static struct clk_bulk_data meson_a1_clocks[] = {
	{ .id = "usb_ctrl" },
	{ .id = "usb_bus" },
	{ .id = "xtal_usb_ctrl" },
};

static const char * const meson_gxm_phy_names[] = {
	"usb2-phy0", "usb2-phy1", "usb2-phy2",
};

static const char * const meson_g12a_phy_names[] = {
	"usb2-phy0", "usb2-phy1", "usb3-phy0",
};

/*
 * Amlogic A1 has a single physical PHY, in slot 1, but still has the
 * two U2 PHY controls register blocks like G12A.
 * AXG has the similar scheme, thus needs the same tweak.
 * Handling the first PHY on slot 1 would need a large amount of code
 * changes, and the current management is generic enough to handle it
 * correctly when only the "usb2-phy1" phy is specified on-par with the
 * DT bindings.
 */
static const char * const meson_a1_phy_names[] = {
	"usb2-phy0", "usb2-phy1"
};

struct dwc3_meson_g12a;

struct dwc3_meson_g12a_drvdata {
	bool otg_switch_supported;
	bool otg_phy_host_port_disable;
	struct clk_bulk_data *clks;
	int num_clks;
	const char * const *phy_names;
	int num_phys;
	int (*setup_regmaps)(struct dwc3_meson_g12a *priv, void __iomem *base);
	int (*usb2_init_phy)(struct dwc3_meson_g12a *priv, int i,
			     enum phy_mode mode);
	int (*set_phy_mode)(struct dwc3_meson_g12a *priv, int i,
			    enum phy_mode mode);
	int (*usb_init)(struct dwc3_meson_g12a *priv);
	int (*usb_post_init)(struct dwc3_meson_g12a *priv);
};

static int dwc3_meson_gxl_setup_regmaps(struct dwc3_meson_g12a *priv,
					void __iomem *base);
static int dwc3_meson_g12a_setup_regmaps(struct dwc3_meson_g12a *priv,
					 void __iomem *base);

static int dwc3_meson_g12a_usb2_init_phy(struct dwc3_meson_g12a *priv, int i,
					 enum phy_mode mode);
static int dwc3_meson_gxl_usb2_init_phy(struct dwc3_meson_g12a *priv, int i,
					enum phy_mode mode);

static int dwc3_meson_g12a_set_phy_mode(struct dwc3_meson_g12a *priv,
					int i, enum phy_mode mode);
static int dwc3_meson_gxl_set_phy_mode(struct dwc3_meson_g12a *priv,
				       int i, enum phy_mode mode);

static int dwc3_meson_g12a_usb_init(struct dwc3_meson_g12a *priv);
static int dwc3_meson_gxl_usb_init(struct dwc3_meson_g12a *priv);

static int dwc3_meson_gxl_usb_post_init(struct dwc3_meson_g12a *priv);

/*
 * For GXL and GXM SoCs:
 * USB Phy muxing between the DWC2 Device controller and the DWC3 Host
 * controller is buggy when switching from Device to Host when USB port
 * is unpopulated, it causes the DWC3 to hard crash.
 * When populated (including OTG switching with ID pin), the switch works
 * like a charm like on the G12A platforms.
 * In order to still switch from Host to Device on an USB Type-A port,
 * an U2_PORT_DISABLE bit has been added to disconnect the DWC3 Host
 * controller from the port, but when used the DWC3 controller must be
 * reset to recover usage of the port.
 */

static struct dwc3_meson_g12a_drvdata gxl_drvdata = {
	.otg_switch_supported = true,
	.otg_phy_host_port_disable = true,
	.clks = meson_gxl_clocks,
	.num_clks = ARRAY_SIZE(meson_g12a_clocks),
	.phy_names = meson_a1_phy_names,
	.num_phys = ARRAY_SIZE(meson_a1_phy_names),
	.setup_regmaps = dwc3_meson_gxl_setup_regmaps,
	.usb2_init_phy = dwc3_meson_gxl_usb2_init_phy,
	.set_phy_mode = dwc3_meson_gxl_set_phy_mode,
	.usb_init = dwc3_meson_gxl_usb_init,
	.usb_post_init = dwc3_meson_gxl_usb_post_init,
};

static struct dwc3_meson_g12a_drvdata gxm_drvdata = {
	.otg_switch_supported = true,
	.otg_phy_host_port_disable = true,
	.clks = meson_gxl_clocks,
	.num_clks = ARRAY_SIZE(meson_g12a_clocks),
	.phy_names = meson_gxm_phy_names,
	.num_phys = ARRAY_SIZE(meson_gxm_phy_names),
	.setup_regmaps = dwc3_meson_gxl_setup_regmaps,
	.usb2_init_phy = dwc3_meson_gxl_usb2_init_phy,
	.set_phy_mode = dwc3_meson_gxl_set_phy_mode,
	.usb_init = dwc3_meson_gxl_usb_init,
	.usb_post_init = dwc3_meson_gxl_usb_post_init,
};

static struct dwc3_meson_g12a_drvdata axg_drvdata = {
	.otg_switch_supported = true,
	.clks = meson_gxl_clocks,
	.num_clks = ARRAY_SIZE(meson_gxl_clocks),
	.phy_names = meson_a1_phy_names,
	.num_phys = ARRAY_SIZE(meson_a1_phy_names),
	.setup_regmaps = dwc3_meson_gxl_setup_regmaps,
	.usb2_init_phy = dwc3_meson_gxl_usb2_init_phy,
	.set_phy_mode = dwc3_meson_gxl_set_phy_mode,
	.usb_init = dwc3_meson_g12a_usb_init,
	.usb_post_init = dwc3_meson_gxl_usb_post_init,
};

static struct dwc3_meson_g12a_drvdata g12a_drvdata = {
	.otg_switch_supported = true,
	.clks = meson_g12a_clocks,
	.num_clks = ARRAY_SIZE(meson_g12a_clocks),
	.phy_names = meson_g12a_phy_names,
	.num_phys = ARRAY_SIZE(meson_g12a_phy_names),
	.setup_regmaps = dwc3_meson_g12a_setup_regmaps,
	.usb2_init_phy = dwc3_meson_g12a_usb2_init_phy,
	.set_phy_mode = dwc3_meson_g12a_set_phy_mode,
	.usb_init = dwc3_meson_g12a_usb_init,
};

static struct dwc3_meson_g12a_drvdata a1_drvdata = {
	.otg_switch_supported = false,
	.clks = meson_a1_clocks,
	.num_clks = ARRAY_SIZE(meson_a1_clocks),
	.phy_names = meson_a1_phy_names,
	.num_phys = ARRAY_SIZE(meson_a1_phy_names),
	.setup_regmaps = dwc3_meson_g12a_setup_regmaps,
	.usb2_init_phy = dwc3_meson_g12a_usb2_init_phy,
	.set_phy_mode = dwc3_meson_g12a_set_phy_mode,
	.usb_init = dwc3_meson_g12a_usb_init,
};

struct dwc3_meson_g12a {
	struct device		*dev;
	struct regmap		*u2p_regmap[PHY_COUNT];
	struct regmap		*usb_glue_regmap;
	struct reset_control	*reset;
	struct phy		*phys[PHY_COUNT];
	enum usb_dr_mode	otg_mode;
	enum phy_mode		otg_phy_mode;
	unsigned int		usb2_ports;
	unsigned int		usb3_ports;
	struct regulator	*vbus;
	struct usb_role_switch_desc switch_desc;
	struct usb_role_switch	*role_switch;
	const struct dwc3_meson_g12a_drvdata *drvdata;
};

static int dwc3_meson_gxl_set_phy_mode(struct dwc3_meson_g12a *priv,
					 int i, enum phy_mode mode)
{
	return phy_set_mode(priv->phys[i], mode);
}

static int dwc3_meson_gxl_usb2_init_phy(struct dwc3_meson_g12a *priv, int i,
					enum phy_mode mode)
{
	/* On GXL PHY must be started in device mode for DWC2 init */
	return priv->drvdata->set_phy_mode(priv, i,
				(i == USB2_OTG_PHY) ? PHY_MODE_USB_DEVICE
						    : PHY_MODE_USB_HOST);
}

static int dwc3_meson_g12a_set_phy_mode(struct dwc3_meson_g12a *priv,
					 int i, enum phy_mode mode)
{
	if (mode == PHY_MODE_USB_HOST)
		regmap_update_bits(priv->u2p_regmap[i], U2P_R0,
				U2P_R0_HOST_DEVICE,
				U2P_R0_HOST_DEVICE);
	else
		regmap_update_bits(priv->u2p_regmap[i], U2P_R0,
				U2P_R0_HOST_DEVICE, 0);

	return 0;
}

static int dwc3_meson_g12a_usb2_init_phy(struct dwc3_meson_g12a *priv, int i,
					 enum phy_mode mode)
{
	int ret;

	regmap_update_bits(priv->u2p_regmap[i], U2P_R0,
			U2P_R0_POWER_ON_RESET,
			U2P_R0_POWER_ON_RESET);

	if (priv->drvdata->otg_switch_supported && i == USB2_OTG_PHY) {
		regmap_update_bits(priv->u2p_regmap[i], U2P_R0,
				   U2P_R0_ID_PULLUP | U2P_R0_DRV_VBUS,
				   U2P_R0_ID_PULLUP | U2P_R0_DRV_VBUS);

		ret = priv->drvdata->set_phy_mode(priv, i, mode);
	} else
		ret = priv->drvdata->set_phy_mode(priv, i,
						  PHY_MODE_USB_HOST);

	if (ret)
		return ret;

	regmap_update_bits(priv->u2p_regmap[i], U2P_R0,
			U2P_R0_POWER_ON_RESET, 0);

	return 0;
}

static int dwc3_meson_g12a_usb2_init(struct dwc3_meson_g12a *priv,
				     enum phy_mode mode)
{
	int i, ret;

	for (i = 0; i < priv->drvdata->num_phys; ++i) {
		if (!priv->phys[i])
			continue;

		if (!strstr(priv->drvdata->phy_names[i], "usb2"))
			continue;

		ret = priv->drvdata->usb2_init_phy(priv, i, mode);
		if (ret)
			return ret;
	}

	return 0;
}

static void dwc3_meson_g12a_usb3_init(struct dwc3_meson_g12a *priv)
{
	regmap_update_bits(priv->usb_glue_regmap, USB_R3,
			USB_R3_P30_SSC_RANGE_MASK |
			USB_R3_P30_REF_SSP_EN,
			USB_R3_P30_SSC_ENABLE |
			FIELD_PREP(USB_R3_P30_SSC_RANGE_MASK, 2) |
			USB_R3_P30_REF_SSP_EN);
	udelay(2);

	regmap_update_bits(priv->usb_glue_regmap, USB_R2,
			USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK,
			FIELD_PREP(USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK, 0x15));

	regmap_update_bits(priv->usb_glue_regmap, USB_R2,
			USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK,
			FIELD_PREP(USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK, 0x20));

	udelay(2);

	regmap_update_bits(priv->usb_glue_regmap, USB_R1,
			USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT,
			USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT);

	regmap_update_bits(priv->usb_glue_regmap, USB_R1,
			USB_R1_P30_PCS_TX_SWING_FULL_MASK,
			FIELD_PREP(USB_R1_P30_PCS_TX_SWING_FULL_MASK, 127));
}

static void dwc3_meson_g12a_usb_otg_apply_mode(struct dwc3_meson_g12a *priv,
					       enum phy_mode mode)
{
	if (mode == PHY_MODE_USB_DEVICE) {
		if (priv->otg_mode != USB_DR_MODE_OTG &&
		    priv->drvdata->otg_phy_host_port_disable)
			/* Isolate the OTG PHY port from the Host Controller */
			regmap_update_bits(priv->usb_glue_regmap, USB_R1,
				USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK,
				FIELD_PREP(USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK,
					   BIT(USB2_OTG_PHY)));

		regmap_update_bits(priv->usb_glue_regmap, USB_R0,
				USB_R0_U2D_ACT, USB_R0_U2D_ACT);
		regmap_update_bits(priv->usb_glue_regmap, USB_R0,
				USB_R0_U2D_SS_SCALEDOWN_MODE_MASK, 0);
		regmap_update_bits(priv->usb_glue_regmap, USB_R4,
				USB_R4_P21_SLEEP_M0, USB_R4_P21_SLEEP_M0);
	} else {
		if (priv->otg_mode != USB_DR_MODE_OTG &&
		    priv->drvdata->otg_phy_host_port_disable) {
			regmap_update_bits(priv->usb_glue_regmap, USB_R1,
				USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK, 0);
			msleep(500);
		}
		regmap_update_bits(priv->usb_glue_regmap, USB_R0,
				USB_R0_U2D_ACT, 0);
		regmap_update_bits(priv->usb_glue_regmap, USB_R4,
				USB_R4_P21_SLEEP_M0, 0);
	}
}

static int dwc3_meson_g12a_usb_init_glue(struct dwc3_meson_g12a *priv,
					 enum phy_mode mode)
{
	int ret;

	ret = dwc3_meson_g12a_usb2_init(priv, mode);
	if (ret)
		return ret;

	regmap_update_bits(priv->usb_glue_regmap, USB_R1,
			USB_R1_U3H_FLADJ_30MHZ_REG_MASK,
			FIELD_PREP(USB_R1_U3H_FLADJ_30MHZ_REG_MASK, 0x20));

	regmap_update_bits(priv->usb_glue_regmap, USB_R5,
			USB_R5_ID_DIG_EN_0,
			USB_R5_ID_DIG_EN_0);
	regmap_update_bits(priv->usb_glue_regmap, USB_R5,
			USB_R5_ID_DIG_EN_1,
			USB_R5_ID_DIG_EN_1);
	regmap_update_bits(priv->usb_glue_regmap, USB_R5,
			USB_R5_ID_DIG_TH_MASK,
			FIELD_PREP(USB_R5_ID_DIG_TH_MASK, 0xff));

	/* If we have an actual SuperSpeed port, initialize it */
	if (priv->usb3_ports)
		dwc3_meson_g12a_usb3_init(priv);

	dwc3_meson_g12a_usb_otg_apply_mode(priv, mode);

	return 0;
}

static const struct regmap_config phy_meson_g12a_usb_glue_regmap_conf = {
	.name = "usb-glue",
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = USB_R5,
};

static int dwc3_meson_g12a_get_phys(struct dwc3_meson_g12a *priv)
{
	const char *phy_name;
	int i;

	for (i = 0 ; i < priv->drvdata->num_phys ; ++i) {
		phy_name = priv->drvdata->phy_names[i];
		priv->phys[i] = devm_phy_optional_get(priv->dev, phy_name);
		if (!priv->phys[i])
			continue;

		if (IS_ERR(priv->phys[i]))
			return PTR_ERR(priv->phys[i]);

		if (strstr(phy_name, "usb3"))
			priv->usb3_ports++;
		else
			priv->usb2_ports++;
	}

	dev_info(priv->dev, "USB2 ports: %d\n", priv->usb2_ports);
	dev_info(priv->dev, "USB3 ports: %d\n", priv->usb3_ports);

	return 0;
}

static enum phy_mode dwc3_meson_g12a_get_id(struct dwc3_meson_g12a *priv)
{
	u32 reg;

	regmap_read(priv->usb_glue_regmap, USB_R5, &reg);

	if (reg & (USB_R5_ID_DIG_SYNC | USB_R5_ID_DIG_REG))
		return PHY_MODE_USB_DEVICE;

	return PHY_MODE_USB_HOST;
}

static int dwc3_meson_g12a_otg_mode_set(struct dwc3_meson_g12a *priv,
					enum phy_mode mode)
{
	int ret;

	if (!priv->drvdata->otg_switch_supported || !priv->phys[USB2_OTG_PHY])
		return -EINVAL;

	if (mode == PHY_MODE_USB_HOST)
		dev_info(priv->dev, "switching to Host Mode\n");
	else
		dev_info(priv->dev, "switching to Device Mode\n");

	if (priv->vbus) {
		if (mode == PHY_MODE_USB_DEVICE)
			ret = regulator_disable(priv->vbus);
		else
			ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;
	}

	priv->otg_phy_mode = mode;

	ret = priv->drvdata->set_phy_mode(priv, USB2_OTG_PHY, mode);
	if (ret)
		return ret;

	dwc3_meson_g12a_usb_otg_apply_mode(priv, mode);

	return 0;
}

static int dwc3_meson_g12a_role_set(struct usb_role_switch *sw,
				    enum usb_role role)
{
	struct dwc3_meson_g12a *priv = usb_role_switch_get_drvdata(sw);
	enum phy_mode mode;

	if (role == USB_ROLE_NONE)
		return 0;

	mode = (role == USB_ROLE_HOST) ? PHY_MODE_USB_HOST
				       : PHY_MODE_USB_DEVICE;

	if (mode == priv->otg_phy_mode)
		return 0;

	if (priv->drvdata->otg_phy_host_port_disable)
		dev_warn_once(priv->dev, "Broken manual OTG switch\n");

	return dwc3_meson_g12a_otg_mode_set(priv, mode);
}

static enum usb_role dwc3_meson_g12a_role_get(struct usb_role_switch *sw)
{
	struct dwc3_meson_g12a *priv = usb_role_switch_get_drvdata(sw);

	return priv->otg_phy_mode == PHY_MODE_USB_HOST ?
		USB_ROLE_HOST : USB_ROLE_DEVICE;
}

static irqreturn_t dwc3_meson_g12a_irq_thread(int irq, void *data)
{
	struct dwc3_meson_g12a *priv = data;
	enum phy_mode otg_id;

	otg_id = dwc3_meson_g12a_get_id(priv);
	if (otg_id != priv->otg_phy_mode) {
		if (dwc3_meson_g12a_otg_mode_set(priv, otg_id))
			dev_warn(priv->dev, "Failed to switch OTG mode\n");
	}

	regmap_update_bits(priv->usb_glue_regmap, USB_R5,
			   USB_R5_ID_DIG_IRQ, 0);

	return IRQ_HANDLED;
}

static struct device *dwc3_meson_g12_find_child(struct device *dev,
						const char *compatible)
{
	struct platform_device *pdev;
	struct device_node *np;

	np = of_get_compatible_child(dev->of_node, compatible);
	if (!np)
		return NULL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return NULL;

	return &pdev->dev;
}

static int dwc3_meson_g12a_otg_init(struct platform_device *pdev,
				    struct dwc3_meson_g12a *priv)
{
	enum phy_mode otg_id;
	int ret, irq;
	struct device *dev = &pdev->dev;

	if (!priv->drvdata->otg_switch_supported)
		return 0;

	if (priv->otg_mode == USB_DR_MODE_OTG) {
		/* Ack irq before registering */
		regmap_update_bits(priv->usb_glue_regmap, USB_R5,
				   USB_R5_ID_DIG_IRQ, 0);

		irq = platform_get_irq(pdev, 0);
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						dwc3_meson_g12a_irq_thread,
						IRQF_ONESHOT, pdev->name, priv);
		if (ret)
			return ret;
	}

	/* Setup OTG mode corresponding to the ID pin */
	if (priv->otg_mode == USB_DR_MODE_OTG) {
		otg_id = dwc3_meson_g12a_get_id(priv);
		if (otg_id != priv->otg_phy_mode) {
			if (dwc3_meson_g12a_otg_mode_set(priv, otg_id))
				dev_warn(dev, "Failed to switch OTG mode\n");
		}
	}

	/* Setup role switcher */
	priv->switch_desc.usb2_port = dwc3_meson_g12_find_child(dev,
								"snps,dwc3");
	priv->switch_desc.udc = dwc3_meson_g12_find_child(dev, "snps,dwc2");
	priv->switch_desc.allow_userspace_control = true;
	priv->switch_desc.set = dwc3_meson_g12a_role_set;
	priv->switch_desc.get = dwc3_meson_g12a_role_get;
	priv->switch_desc.driver_data = priv;

	priv->role_switch = usb_role_switch_register(dev, &priv->switch_desc);
	if (IS_ERR(priv->role_switch))
		dev_warn(dev, "Unable to register Role Switch\n");

	return 0;
}

static int dwc3_meson_gxl_setup_regmaps(struct dwc3_meson_g12a *priv,
					void __iomem *base)
{
	/* GXL controls the PHY mode in the PHY registers unlike G12A */
	priv->usb_glue_regmap = devm_regmap_init_mmio(priv->dev, base,
					&phy_meson_g12a_usb_glue_regmap_conf);
	return PTR_ERR_OR_ZERO(priv->usb_glue_regmap);
}

static int dwc3_meson_g12a_setup_regmaps(struct dwc3_meson_g12a *priv,
					 void __iomem *base)
{
	int i;

	priv->usb_glue_regmap = devm_regmap_init_mmio(priv->dev,
					base + G12A_GLUE_OFFSET,
					&phy_meson_g12a_usb_glue_regmap_conf);
	if (IS_ERR(priv->usb_glue_regmap))
		return PTR_ERR(priv->usb_glue_regmap);

	/* Create a regmap for each USB2 PHY control register set */
	for (i = 0; i < priv->drvdata->num_phys; i++) {
		struct regmap_config u2p_regmap_config = {
			.reg_bits = 8,
			.val_bits = 32,
			.reg_stride = 4,
			.max_register = U2P_R1,
		};

		if (!strstr(priv->drvdata->phy_names[i], "usb2"))
			continue;

		u2p_regmap_config.name = devm_kasprintf(priv->dev, GFP_KERNEL,
							"u2p-%d", i);
		if (!u2p_regmap_config.name)
			return -ENOMEM;

		priv->u2p_regmap[i] = devm_regmap_init_mmio(priv->dev,
						base + (i * U2P_REG_SIZE),
						&u2p_regmap_config);
		if (IS_ERR(priv->u2p_regmap[i]))
			return PTR_ERR(priv->u2p_regmap[i]);
	}

	return 0;
}

static int dwc3_meson_g12a_usb_init(struct dwc3_meson_g12a *priv)
{
	return dwc3_meson_g12a_usb_init_glue(priv, priv->otg_phy_mode);
}

static int dwc3_meson_gxl_usb_init(struct dwc3_meson_g12a *priv)
{
	return dwc3_meson_g12a_usb_init_glue(priv, PHY_MODE_USB_DEVICE);
}

static int dwc3_meson_gxl_usb_post_init(struct dwc3_meson_g12a *priv)
{
	int ret;

	ret = priv->drvdata->set_phy_mode(priv, USB2_OTG_PHY,
					  priv->otg_phy_mode);
	if (ret)
		return ret;

	dwc3_meson_g12a_usb_otg_apply_mode(priv, priv->otg_phy_mode);

	return 0;
}

static int dwc3_meson_g12a_probe(struct platform_device *pdev)
{
	struct dwc3_meson_g12a	*priv;
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;
	void __iomem *base;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->drvdata = of_device_get_match_data(&pdev->dev);
	priv->dev = dev;

	priv->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(priv->vbus)) {
		if (PTR_ERR(priv->vbus) == -EPROBE_DEFER)
			return PTR_ERR(priv->vbus);
		priv->vbus = NULL;
	}

	ret = devm_clk_bulk_get(dev,
				priv->drvdata->num_clks,
				priv->drvdata->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(priv->drvdata->num_clks,
				      priv->drvdata->clks);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);

	priv->reset = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(priv->reset)) {
		ret = PTR_ERR(priv->reset);
		dev_err(dev, "failed to get device reset, err=%d\n", ret);
		goto err_disable_clks;
	}

	ret = reset_control_reset(priv->reset);
	if (ret)
		goto err_disable_clks;

	ret = dwc3_meson_g12a_get_phys(priv);
	if (ret)
		goto err_disable_clks;

	ret = priv->drvdata->setup_regmaps(priv, base);
	if (ret)
		goto err_disable_clks;

	if (priv->vbus) {
		ret = regulator_enable(priv->vbus);
		if (ret)
			goto err_disable_clks;
	}

	/* Get dr_mode */
	priv->otg_mode = usb_get_dr_mode(dev);

	if (priv->otg_mode == USB_DR_MODE_PERIPHERAL)
		priv->otg_phy_mode = PHY_MODE_USB_DEVICE;
	else
		priv->otg_phy_mode = PHY_MODE_USB_HOST;

	ret = priv->drvdata->usb_init(priv);
	if (ret)
		goto err_disable_clks;

	/* Init PHYs */
	for (i = 0 ; i < PHY_COUNT ; ++i) {
		ret = phy_init(priv->phys[i]);
		if (ret)
			goto err_disable_clks;
	}

	/* Set PHY Power */
	for (i = 0 ; i < PHY_COUNT ; ++i) {
		ret = phy_power_on(priv->phys[i]);
		if (ret)
			goto err_phys_exit;
	}

	if (priv->drvdata->usb_post_init) {
		ret = priv->drvdata->usb_post_init(priv);
		if (ret)
			goto err_phys_power;
	}

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret)
		goto err_phys_power;

	ret = dwc3_meson_g12a_otg_init(pdev, priv);
	if (ret)
		goto err_phys_power;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	return 0;

err_phys_power:
	for (i = 0 ; i < PHY_COUNT ; ++i)
		phy_power_off(priv->phys[i]);

err_phys_exit:
	for (i = 0 ; i < PHY_COUNT ; ++i)
		phy_exit(priv->phys[i]);

err_disable_clks:
	clk_bulk_disable_unprepare(priv->drvdata->num_clks,
				   priv->drvdata->clks);

	return ret;
}

static int dwc3_meson_g12a_remove(struct platform_device *pdev)
{
	struct dwc3_meson_g12a *priv = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int i;

	if (priv->drvdata->otg_switch_supported)
		usb_role_switch_unregister(priv->role_switch);

	of_platform_depopulate(dev);

	for (i = 0 ; i < PHY_COUNT ; ++i) {
		phy_power_off(priv->phys[i]);
		phy_exit(priv->phys[i]);
	}

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	clk_bulk_disable_unprepare(priv->drvdata->num_clks,
				   priv->drvdata->clks);

	return 0;
}

static int __maybe_unused dwc3_meson_g12a_runtime_suspend(struct device *dev)
{
	struct dwc3_meson_g12a	*priv = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(priv->drvdata->num_clks,
				   priv->drvdata->clks);

	return 0;
}

static int __maybe_unused dwc3_meson_g12a_runtime_resume(struct device *dev)
{
	struct dwc3_meson_g12a	*priv = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(priv->drvdata->num_clks,
				       priv->drvdata->clks);
}

static int __maybe_unused dwc3_meson_g12a_suspend(struct device *dev)
{
	struct dwc3_meson_g12a *priv = dev_get_drvdata(dev);
	int i, ret;

	if (priv->vbus && priv->otg_phy_mode == PHY_MODE_USB_HOST) {
		ret = regulator_disable(priv->vbus);
		if (ret)
			return ret;
	}

	for (i = 0 ; i < PHY_COUNT ; ++i) {
		phy_power_off(priv->phys[i]);
		phy_exit(priv->phys[i]);
	}

	reset_control_assert(priv->reset);

	return 0;
}

static int __maybe_unused dwc3_meson_g12a_resume(struct device *dev)
{
	struct dwc3_meson_g12a *priv = dev_get_drvdata(dev);
	int i, ret;

	reset_control_deassert(priv->reset);

	ret = priv->drvdata->usb_init(priv);
	if (ret)
		return ret;

	/* Init PHYs */
	for (i = 0 ; i < PHY_COUNT ; ++i) {
		ret = phy_init(priv->phys[i]);
		if (ret)
			return ret;
	}

	/* Set PHY Power */
	for (i = 0 ; i < PHY_COUNT ; ++i) {
		ret = phy_power_on(priv->phys[i]);
		if (ret)
			return ret;
	}

	if (priv->vbus && priv->otg_phy_mode == PHY_MODE_USB_HOST) {
		ret = regulator_enable(priv->vbus);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct dev_pm_ops dwc3_meson_g12a_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_meson_g12a_suspend, dwc3_meson_g12a_resume)
	SET_RUNTIME_PM_OPS(dwc3_meson_g12a_runtime_suspend,
			   dwc3_meson_g12a_runtime_resume, NULL)
};

static const struct of_device_id dwc3_meson_g12a_match[] = {
	{
		.compatible = "amlogic,meson-gxl-usb-ctrl",
		.data = &gxl_drvdata,
	},
	{
		.compatible = "amlogic,meson-gxm-usb-ctrl",
		.data = &gxm_drvdata,
	},
	{
		.compatible = "amlogic,meson-axg-usb-ctrl",
		.data = &axg_drvdata,
	},
	{
		.compatible = "amlogic,meson-g12a-usb-ctrl",
		.data = &g12a_drvdata,
	},
	{
		.compatible = "amlogic,meson-a1-usb-ctrl",
		.data = &a1_drvdata,
	},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwc3_meson_g12a_match);

static struct platform_driver dwc3_meson_g12a_driver = {
	.probe		= dwc3_meson_g12a_probe,
	.remove		= dwc3_meson_g12a_remove,
	.driver		= {
		.name	= "dwc3-meson-g12a",
		.of_match_table = dwc3_meson_g12a_match,
		.pm	= &dwc3_meson_g12a_dev_pm_ops,
	},
};

module_platform_driver(dwc3_meson_g12a_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic Meson G12A USB Glue Layer");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
