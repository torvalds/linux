// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Schneider Electric
 *
 * Clément Léger <clement.leger@bootlin.com>
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/mdio.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pcs-rzn1-miic.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <dt-bindings/net/pcs-rzn1-miic.h>
#include <dt-bindings/net/renesas,r9a09g077-pcs-miic.h>

#define MIIC_PRCMD			0x0
#define MIIC_ESID_CODE			0x4

#define MIIC_MODCTRL			0x8

#define MIIC_CONVCTRL(port)		(0x100 + (port) * 4)

#define MIIC_CONVCTRL_CONV_SPEED	GENMASK(1, 0)
#define CONV_MODE_10MBPS		0
#define CONV_MODE_100MBPS		1
#define CONV_MODE_1000MBPS		2

#define MIIC_CONVCTRL_CONV_MODE		GENMASK(3, 2)
#define CONV_MODE_MII			0
#define CONV_MODE_RMII			1
#define CONV_MODE_RGMII			2

#define MIIC_CONVCTRL_FULLD		BIT(8)
#define MIIC_CONVCTRL_RGMII_LINK	BIT(12)
#define MIIC_CONVCTRL_RGMII_DUPLEX	BIT(13)
#define MIIC_CONVCTRL_RGMII_SPEED	GENMASK(15, 14)

#define MIIC_CONVRST			0x114
#define MIIC_CONVRST_PHYIF_RST(port)	BIT(port)
#define MIIC_CONVRST_PHYIF_RST_MASK	GENMASK(4, 0)

#define MIIC_SWCTRL			0x304
#define MIIC_SWDUPC			0x308

#define MIIC_MODCTRL_CONF_CONV_MAX	6
#define MIIC_MODCTRL_CONF_NONE		-1

#define MIIC_MAX_NUM_RSTS		2

/**
 * struct modctrl_match - Matching table entry for  convctrl configuration
 *			  See section 8.2.1 of manual.
 * @mode_cfg: Configuration value for convctrl
 * @conv: Configuration of ethernet port muxes. First index is SWITCH_PORTIN,
 *	  then index 1 - 5 are CONV1 - CONV5 for RZ/N1 SoCs. In case
 *	  of RZ/T2H and RZ/N2H SoCs, the first index is SWITCH_PORTIN then
 *	  index 0 - 3 are CONV0 - CONV3.
 */
struct modctrl_match {
	u32 mode_cfg;
	u8 conv[MIIC_MODCTRL_CONF_CONV_MAX];
};

static struct modctrl_match modctrl_match_table[] = {
	{0x0, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_SWITCH_PORTC, MIIC_SERCOS_PORTB, MIIC_SERCOS_PORTA}},
	{0x1, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_SWITCH_PORTC, MIIC_ETHERCAT_PORTB, MIIC_ETHERCAT_PORTA}},
	{0x2, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_ETHERCAT_PORTC, MIIC_ETHERCAT_PORTB, MIIC_ETHERCAT_PORTA}},
	{0x3, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_SWITCH_PORTC, MIIC_SWITCH_PORTB, MIIC_SWITCH_PORTA}},

	{0x8, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_SWITCH_PORTC, MIIC_SERCOS_PORTB, MIIC_SERCOS_PORTA}},
	{0x9, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_SWITCH_PORTC, MIIC_ETHERCAT_PORTB, MIIC_ETHERCAT_PORTA}},
	{0xA, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_ETHERCAT_PORTC, MIIC_ETHERCAT_PORTB, MIIC_ETHERCAT_PORTA}},
	{0xB, {MIIC_RTOS_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
	       MIIC_SWITCH_PORTC, MIIC_SWITCH_PORTB, MIIC_SWITCH_PORTA}},

	{0x10, {MIIC_GMAC2_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
		MIIC_SWITCH_PORTC, MIIC_SERCOS_PORTB, MIIC_SERCOS_PORTA}},
	{0x11, {MIIC_GMAC2_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
		MIIC_SWITCH_PORTC, MIIC_ETHERCAT_PORTB, MIIC_ETHERCAT_PORTA}},
	{0x12, {MIIC_GMAC2_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
		MIIC_ETHERCAT_PORTC, MIIC_ETHERCAT_PORTB, MIIC_ETHERCAT_PORTA}},
	{0x13, {MIIC_GMAC2_PORT, MIIC_GMAC1_PORT, MIIC_SWITCH_PORTD,
		MIIC_SWITCH_PORTC, MIIC_SWITCH_PORTB, MIIC_SWITCH_PORTA}}
};

static const char * const conf_to_string[] = {
	[MIIC_GMAC1_PORT]	= "GMAC1_PORT",
	[MIIC_GMAC2_PORT]	= "GMAC2_PORT",
	[MIIC_RTOS_PORT]	= "RTOS_PORT",
	[MIIC_SERCOS_PORTA]	= "SERCOS_PORTA",
	[MIIC_SERCOS_PORTB]	= "SERCOS_PORTB",
	[MIIC_ETHERCAT_PORTA]	= "ETHERCAT_PORTA",
	[MIIC_ETHERCAT_PORTB]	= "ETHERCAT_PORTB",
	[MIIC_ETHERCAT_PORTC]	= "ETHERCAT_PORTC",
	[MIIC_SWITCH_PORTA]	= "SWITCH_PORTA",
	[MIIC_SWITCH_PORTB]	= "SWITCH_PORTB",
	[MIIC_SWITCH_PORTC]	= "SWITCH_PORTC",
	[MIIC_SWITCH_PORTD]	= "SWITCH_PORTD",
	[MIIC_HSR_PORTA]	= "HSR_PORTA",
	[MIIC_HSR_PORTB]	= "HSR_PORTB",
};

static const char * const index_to_string[] = {
	"SWITCH_PORTIN",
	"CONV1",
	"CONV2",
	"CONV3",
	"CONV4",
	"CONV5",
};

static struct modctrl_match rzt2h_modctrl_match_table[] = {
	{0x0, {ETHSS_GMAC0_PORT, ETHSS_ETHSW_PORT0, ETHSS_ETHSW_PORT1,
	       ETHSS_ETHSW_PORT2, ETHSS_GMAC1_PORT}},

	{0x1, {MIIC_MODCTRL_CONF_NONE, ETHSS_ESC_PORT0, ETHSS_ESC_PORT1,
	       ETHSS_GMAC2_PORT, ETHSS_GMAC1_PORT}},

	{0x2, {ETHSS_GMAC0_PORT, ETHSS_ESC_PORT0, ETHSS_ESC_PORT1,
		ETHSS_ETHSW_PORT2, ETHSS_GMAC1_PORT}},

	{0x3, {MIIC_MODCTRL_CONF_NONE, ETHSS_ESC_PORT0, ETHSS_ESC_PORT1,
	       ETHSS_ESC_PORT2, ETHSS_GMAC1_PORT}},

	{0x4, {ETHSS_GMAC0_PORT, ETHSS_ETHSW_PORT0, ETHSS_ESC_PORT1,
	       ETHSS_ESC_PORT2, ETHSS_GMAC1_PORT}},

	{0x5, {ETHSS_GMAC0_PORT, ETHSS_ETHSW_PORT0, ETHSS_ESC_PORT1,
	       ETHSS_ETHSW_PORT2, ETHSS_GMAC1_PORT}},

	{0x6, {ETHSS_GMAC0_PORT, ETHSS_ETHSW_PORT0, ETHSS_ETHSW_PORT1,
	       ETHSS_GMAC2_PORT, ETHSS_GMAC1_PORT}},

	{0x7, {MIIC_MODCTRL_CONF_NONE, ETHSS_GMAC0_PORT, ETHSS_GMAC1_PORT,
		ETHSS_GMAC2_PORT, MIIC_MODCTRL_CONF_NONE}}
};

static const char * const rzt2h_conf_to_string[] = {
	[ETHSS_GMAC0_PORT]	= "GMAC0_PORT",
	[ETHSS_GMAC1_PORT]	= "GMAC1_PORT",
	[ETHSS_GMAC2_PORT]	= "GMAC2_PORT",
	[ETHSS_ESC_PORT0]	= "ETHERCAT_PORT0",
	[ETHSS_ESC_PORT1]	= "ETHERCAT_PORT1",
	[ETHSS_ESC_PORT2]	= "ETHERCAT_PORT2",
	[ETHSS_ETHSW_PORT0]	= "SWITCH_PORT0",
	[ETHSS_ETHSW_PORT1]	= "SWITCH_PORT1",
	[ETHSS_ETHSW_PORT2]	= "SWITCH_PORT2",
};

static const char * const rzt2h_index_to_string[] = {
	"SWITCH_PORTIN",
	"CONV0",
	"CONV1",
	"CONV2",
	"CONV3",
};

static const char * const rzt2h_reset_ids[] = {
	"rst",
	"crst",
};

/**
 * struct miic - MII converter structure
 * @base: base address of the MII converter
 * @dev: Device associated to the MII converter
 * @lock: Lock used for read-modify-write access
 * @rsts: Reset controls for the MII converter
 * @of_data: Pointer to OF data
 */
struct miic {
	void __iomem *base;
	struct device *dev;
	spinlock_t lock;
	struct reset_control_bulk_data rsts[MIIC_MAX_NUM_RSTS];
	const struct miic_of_data *of_data;
};

/**
 * struct miic_of_data - OF data for MII converter
 * @match_table: Matching table for convctrl configuration
 * @match_table_count: Number of entries in the matching table
 * @conf_conv_count: Number of entries in the conf_conv array
 * @conf_to_string: String representations of the configuration values
 * @conf_to_string_count: Number of entries in the conf_to_string array
 * @index_to_string: String representations of the index values
 * @index_to_string_count: Number of entries in the index_to_string array
 * @miic_port_start: MIIC port start number
 * @miic_port_max: Maximum MIIC supported
 * @sw_mode_mask: Switch mode mask
 * @reset_ids: Reset names array
 * @reset_count: Number of entries in the reset_ids array
 * @init_unlock_lock_regs: Flag to indicate if registers need to be unlocked
 *  before access.
 * @miic_write: Function pointer to write a value to a MIIC register
 */
struct miic_of_data {
	struct modctrl_match *match_table;
	u8 match_table_count;
	u8 conf_conv_count;
	const char * const *conf_to_string;
	u8 conf_to_string_count;
	const char * const *index_to_string;
	u8 index_to_string_count;
	u8 miic_port_start;
	u8 miic_port_max;
	u8 sw_mode_mask;
	const char * const *reset_ids;
	u8 reset_count;
	bool init_unlock_lock_regs;
	void (*miic_write)(struct miic *miic, int offset, u32 value);
};

/**
 * struct miic_port - Per port MII converter struct
 * @miic: backiling to MII converter structure
 * @pcs: PCS structure associated to the port
 * @port: port number
 * @interface: interface mode of the port
 */
struct miic_port {
	struct miic *miic;
	struct phylink_pcs pcs;
	int port;
	phy_interface_t interface;
};

static struct miic_port *phylink_pcs_to_miic_port(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct miic_port, pcs);
}

static void miic_unlock_regs(struct miic *miic)
{
	/* Unprotect register writes */
	writel(0x00A5, miic->base + MIIC_PRCMD);
	writel(0x0001, miic->base + MIIC_PRCMD);
	writel(0xFFFE, miic->base + MIIC_PRCMD);
	writel(0x0001, miic->base + MIIC_PRCMD);
}

static void miic_lock_regs(struct miic *miic)
{
	/* Protect register writes */
	writel(0x0000, miic->base + MIIC_PRCMD);
}

static void miic_reg_writel_unlocked(struct miic *miic, int offset, u32 value)
{
	writel(value, miic->base + offset);
}

static void miic_reg_writel_locked(struct miic *miic, int offset, u32 value)
{
	miic_unlock_regs(miic);
	writel(value, miic->base + offset);
	miic_lock_regs(miic);
}

static void miic_reg_writel(struct miic *miic, int offset, u32 value)
{
	miic->of_data->miic_write(miic, offset, value);
}

static u32 miic_reg_readl(struct miic *miic, int offset)
{
	return readl(miic->base + offset);
}

static void miic_reg_rmw(struct miic *miic, int offset, u32 mask, u32 val)
{
	u32 reg;

	spin_lock(&miic->lock);

	reg = miic_reg_readl(miic, offset);
	reg &= ~mask;
	reg |= val;
	miic_reg_writel(miic, offset, reg);

	spin_unlock(&miic->lock);
}

static void miic_converter_enable(struct miic *miic, int port, int enable)
{
	u32 val = 0;

	if (enable)
		val = MIIC_CONVRST_PHYIF_RST(port);

	miic_reg_rmw(miic, MIIC_CONVRST, MIIC_CONVRST_PHYIF_RST(port), val);
}

static int miic_config(struct phylink_pcs *pcs, unsigned int neg_mode,
		       phy_interface_t interface,
		       const unsigned long *advertising, bool permit)
{
	struct miic_port *miic_port = phylink_pcs_to_miic_port(pcs);
	struct miic *miic = miic_port->miic;
	u32 speed, conv_mode, val, mask;
	int port = miic_port->port;

	switch (interface) {
	case PHY_INTERFACE_MODE_RMII:
		conv_mode = CONV_MODE_RMII;
		speed = CONV_MODE_100MBPS;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		conv_mode = CONV_MODE_RGMII;
		speed = CONV_MODE_1000MBPS;
		break;
	case PHY_INTERFACE_MODE_MII:
		conv_mode = CONV_MODE_MII;
		/* When in MII mode, speed should be set to 0 (which is actually
		 * CONV_MODE_10MBPS)
		 */
		speed = CONV_MODE_10MBPS;
		break;
	default:
		return -EOPNOTSUPP;
	}

	val = FIELD_PREP(MIIC_CONVCTRL_CONV_MODE, conv_mode);
	mask = MIIC_CONVCTRL_CONV_MODE;

	/* Update speed only if we are going to change the interface because
	 * the link might already be up and it would break it if the speed is
	 * changed.
	 */
	if (interface != miic_port->interface) {
		val |= FIELD_PREP(MIIC_CONVCTRL_CONV_SPEED, speed);
		mask |= MIIC_CONVCTRL_CONV_SPEED;
		miic_port->interface = interface;
	}

	miic_reg_rmw(miic, MIIC_CONVCTRL(port), mask, val);
	miic_converter_enable(miic, miic_port->port, 1);

	return 0;
}

static void miic_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			 phy_interface_t interface, int speed, int duplex)
{
	struct miic_port *miic_port = phylink_pcs_to_miic_port(pcs);
	struct miic *miic = miic_port->miic;
	u32 conv_speed = 0, val = 0;
	int port = miic_port->port;

	if (duplex == DUPLEX_FULL)
		val |= MIIC_CONVCTRL_FULLD;

	/* No speed in MII through-mode */
	if (interface != PHY_INTERFACE_MODE_MII) {
		switch (speed) {
		case SPEED_1000:
			conv_speed = CONV_MODE_1000MBPS;
			break;
		case SPEED_100:
			conv_speed = CONV_MODE_100MBPS;
			break;
		case SPEED_10:
			conv_speed = CONV_MODE_10MBPS;
			break;
		default:
			return;
		}
	}

	val |= FIELD_PREP(MIIC_CONVCTRL_CONV_SPEED, conv_speed);

	miic_reg_rmw(miic, MIIC_CONVCTRL(port),
		     (MIIC_CONVCTRL_CONV_SPEED | MIIC_CONVCTRL_FULLD), val);
}

static int miic_pre_init(struct phylink_pcs *pcs)
{
	struct miic_port *miic_port = phylink_pcs_to_miic_port(pcs);
	struct miic *miic = miic_port->miic;
	u32 val, mask;

	/* Start RX clock if required */
	if (pcs->rxc_always_on) {
		/* In MII through mode, the clock signals will be driven by the
		 * external PHY, which might not be initialized yet. Set RMII
		 * as default mode to ensure that a reference clock signal is
		 * generated.
		 */
		miic_port->interface = PHY_INTERFACE_MODE_RMII;

		val = FIELD_PREP(MIIC_CONVCTRL_CONV_MODE, CONV_MODE_RMII) |
		      FIELD_PREP(MIIC_CONVCTRL_CONV_SPEED, CONV_MODE_100MBPS);
		mask = MIIC_CONVCTRL_CONV_MODE | MIIC_CONVCTRL_CONV_SPEED;

		miic_reg_rmw(miic, MIIC_CONVCTRL(miic_port->port), mask, val);

		miic_converter_enable(miic, miic_port->port, 1);
	}

	return 0;
}

static const struct phylink_pcs_ops miic_phylink_ops = {
	.pcs_config = miic_config,
	.pcs_link_up = miic_link_up,
	.pcs_pre_init = miic_pre_init,
};

struct phylink_pcs *miic_create(struct device *dev, struct device_node *np)
{
	const struct miic_of_data *of_data;
	struct platform_device *pdev;
	struct miic_port *miic_port;
	struct device_node *pcs_np;
	struct miic *miic;
	u32 port;

	if (!of_device_is_available(np))
		return ERR_PTR(-ENODEV);

	if (of_property_read_u32(np, "reg", &port))
		return ERR_PTR(-EINVAL);

	/* The PCS pdev is attached to the parent node */
	pcs_np = of_get_parent(np);
	if (!pcs_np)
		return ERR_PTR(-ENODEV);

	if (!of_device_is_available(pcs_np)) {
		of_node_put(pcs_np);
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(pcs_np);
	of_node_put(pcs_np);
	if (!pdev || !platform_get_drvdata(pdev)) {
		if (pdev)
			put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	miic = platform_get_drvdata(pdev);
	of_data = miic->of_data;
	if (port > of_data->miic_port_max || port < of_data->miic_port_start) {
		put_device(&pdev->dev);
		return ERR_PTR(-EINVAL);
	}

	miic_port = kzalloc(sizeof(*miic_port), GFP_KERNEL);
	if (!miic_port) {
		put_device(&pdev->dev);
		return ERR_PTR(-ENOMEM);
	}

	device_link_add(dev, miic->dev, DL_FLAG_AUTOREMOVE_CONSUMER);
	put_device(&pdev->dev);

	miic_port->miic = miic;
	miic_port->port = port - of_data->miic_port_start;
	miic_port->pcs.ops = &miic_phylink_ops;

	phy_interface_set_rgmii(miic_port->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RMII, miic_port->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_MII, miic_port->pcs.supported_interfaces);

	return &miic_port->pcs;
}
EXPORT_SYMBOL(miic_create);

void miic_destroy(struct phylink_pcs *pcs)
{
	struct miic_port *miic_port = phylink_pcs_to_miic_port(pcs);

	miic_converter_enable(miic_port->miic, miic_port->port, 0);
	kfree(miic_port);
}
EXPORT_SYMBOL(miic_destroy);

static int miic_init_hw(struct miic *miic, u32 cfg_mode)
{
	u8 sw_mode_mask = miic->of_data->sw_mode_mask;
	int port;

	/* Unlock write access to accessory registers (cf datasheet). If this
	 * is going to be used in conjunction with the Cortex-M3, this sequence
	 * will have to be moved in register write
	 */
	if (miic->of_data->init_unlock_lock_regs)
		miic_unlock_regs(miic);

	/* TODO: Replace with FIELD_PREP() when compile-time constant
	 * restriction is lifted. Currently __ffs() returns 0 for sw_mode_mask.
	 */
	miic_reg_writel(miic, MIIC_MODCTRL,
			((cfg_mode << __ffs(sw_mode_mask)) & sw_mode_mask));

	for (port = 0; port < miic->of_data->miic_port_max; port++) {
		miic_converter_enable(miic, port, 0);
		/* Disable speed/duplex control from these registers, datasheet
		 * says switch registers should be used to setup switch port
		 * speed and duplex.
		 */
		miic_reg_writel(miic, MIIC_SWCTRL, 0x0);
		miic_reg_writel(miic, MIIC_SWDUPC, 0x0);
	}

	return 0;
}

static bool miic_modctrl_match(s8 *table_val, s8 *dt_val, u8 count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (dt_val[i] == MIIC_MODCTRL_CONF_NONE)
			continue;

		if (dt_val[i] != table_val[i])
			return false;
	}

	return true;
}

static void miic_dump_conf(struct miic *miic, s8 *conf)
{
	const struct miic_of_data *of_data = miic->of_data;
	const char *conf_name;
	int i;

	for (i = 0; i < of_data->conf_conv_count; i++) {
		if (conf[i] != MIIC_MODCTRL_CONF_NONE)
			conf_name = of_data->conf_to_string[conf[i]];
		else
			conf_name = "NONE";

		dev_err(miic->dev, "%s: %s\n",
			of_data->index_to_string[i], conf_name);
	}
}

static int miic_match_dt_conf(struct miic *miic, s8 *dt_val, u32 *mode_cfg)
{
	const struct miic_of_data *of_data = miic->of_data;
	struct modctrl_match *table_entry;
	int i;

	for (i = 0; i < of_data->match_table_count; i++) {
		table_entry = &of_data->match_table[i];

		if (miic_modctrl_match(table_entry->conv, dt_val,
				       miic->of_data->conf_conv_count)) {
			*mode_cfg = table_entry->mode_cfg;
			return 0;
		}
	}

	dev_err(miic->dev, "Failed to apply requested configuration\n");
	miic_dump_conf(miic, dt_val);

	return -EINVAL;
}

static int miic_parse_dt(struct miic *miic, u32 *mode_cfg)
{
	struct device_node *np = miic->dev->of_node;
	struct device_node *conv;
	int port, ret;
	s8 *dt_val;
	u32 conf;

	dt_val = kmalloc_array(miic->of_data->conf_conv_count,
			       sizeof(*dt_val), GFP_KERNEL);
	if (!dt_val)
		return -ENOMEM;

	memset(dt_val, MIIC_MODCTRL_CONF_NONE, sizeof(*dt_val));

	if (of_property_read_u32(np, "renesas,miic-switch-portin", &conf) == 0)
		dt_val[0] = conf;

	for_each_available_child_of_node(np, conv) {
		if (of_property_read_u32(conv, "reg", &port))
			continue;

		/* Adjust for 0 based index */
		port += !miic->of_data->miic_port_start;
		if (of_property_read_u32(conv, "renesas,miic-input", &conf) == 0)
			dt_val[port] = conf;
	}

	ret = miic_match_dt_conf(miic, dt_val, mode_cfg);
	kfree(dt_val);

	return ret;
}

static void miic_reset_control_bulk_assert(void *data)
{
	struct miic *miic = data;
	int ret;

	ret = reset_control_bulk_assert(miic->of_data->reset_count, miic->rsts);
	if (ret)
		dev_err(miic->dev, "failed to assert reset lines\n");
}

static int miic_reset_control_init(struct miic *miic)
{
	const struct miic_of_data *of_data = miic->of_data;
	struct device *dev = miic->dev;
	int ret;
	u8 i;

	if (!of_data->reset_count)
		return 0;

	for (i = 0; i < of_data->reset_count; i++)
		miic->rsts[i].id = of_data->reset_ids[i];

	ret = devm_reset_control_bulk_get_exclusive(dev, of_data->reset_count,
						    miic->rsts);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get bulk reset lines\n");

	ret = reset_control_bulk_deassert(of_data->reset_count, miic->rsts);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to deassert reset lines\n");

	ret = devm_add_action_or_reset(dev, miic_reset_control_bulk_assert,
				       miic);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add reset action\n");

	return 0;
}

static int miic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct miic *miic;
	u32 mode_cfg;
	int ret;

	miic = devm_kzalloc(dev, sizeof(*miic), GFP_KERNEL);
	if (!miic)
		return -ENOMEM;

	miic->of_data = of_device_get_match_data(dev);
	miic->dev = dev;

	ret = miic_parse_dt(miic, &mode_cfg);
	if (ret < 0)
		return ret;

	spin_lock_init(&miic->lock);
	miic->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(miic->base))
		return PTR_ERR(miic->base);

	ret = miic_reset_control_init(miic);
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret < 0)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = miic_init_hw(miic, mode_cfg);
	if (ret)
		goto disable_runtime_pm;

	/* miic_create() relies on that fact that data are attached to the
	 * platform device to determine if the driver is ready so this needs to
	 * be the last thing to be done after everything is initialized
	 * properly.
	 */
	platform_set_drvdata(pdev, miic);

	return 0;

disable_runtime_pm:
	pm_runtime_put(dev);

	return ret;
}

static void miic_remove(struct platform_device *pdev)
{
	pm_runtime_put(&pdev->dev);
}

static struct miic_of_data rzn1_miic_of_data = {
	.match_table = modctrl_match_table,
	.match_table_count = ARRAY_SIZE(modctrl_match_table),
	.conf_conv_count = MIIC_MODCTRL_CONF_CONV_MAX,
	.conf_to_string = conf_to_string,
	.conf_to_string_count = ARRAY_SIZE(conf_to_string),
	.index_to_string = index_to_string,
	.index_to_string_count = ARRAY_SIZE(index_to_string),
	.miic_port_start = 1,
	.miic_port_max = 5,
	.sw_mode_mask = GENMASK(4, 0),
	.init_unlock_lock_regs = true,
	.miic_write = miic_reg_writel_unlocked,
};

static struct miic_of_data rzt2h_miic_of_data = {
	.match_table = rzt2h_modctrl_match_table,
	.match_table_count = ARRAY_SIZE(rzt2h_modctrl_match_table),
	.conf_conv_count = 5,
	.conf_to_string = rzt2h_conf_to_string,
	.conf_to_string_count = ARRAY_SIZE(rzt2h_conf_to_string),
	.index_to_string = rzt2h_index_to_string,
	.index_to_string_count = ARRAY_SIZE(rzt2h_index_to_string),
	.miic_port_start = 0,
	.miic_port_max = 4,
	.sw_mode_mask = GENMASK(2, 0),
	.reset_ids = rzt2h_reset_ids,
	.reset_count = ARRAY_SIZE(rzt2h_reset_ids),
	.miic_write = miic_reg_writel_locked,
};

static const struct of_device_id miic_of_mtable[] = {
	{ .compatible = "renesas,r9a09g077-miic", .data = &rzt2h_miic_of_data },
	{ .compatible = "renesas,rzn1-miic", .data = &rzn1_miic_of_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, miic_of_mtable);

static struct platform_driver miic_driver = {
	.driver = {
		.name	 = "rzn1_miic",
		.suppress_bind_attrs = true,
		.of_match_table = miic_of_mtable,
	},
	.probe = miic_probe,
	.remove = miic_remove,
};
module_platform_driver(miic_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas MII converter PCS driver");
MODULE_AUTHOR("Clément Léger <clement.leger@bootlin.com>");
