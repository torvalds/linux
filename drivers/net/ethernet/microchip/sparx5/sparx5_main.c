// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <net/switchdev.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/reset.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_port.h"
#include "sparx5_qos.h"

#define QLIM_WM(fraction) \
	((SPX5_BUFFER_MEMORY / SPX5_BUFFER_CELL_SZ - 100) * (fraction) / 100)
#define IO_RANGES 3

struct initial_port_config {
	u32 portno;
	struct device_node *node;
	struct sparx5_port_config conf;
	struct phy *serdes;
};

struct sparx5_ram_config {
	void __iomem *init_reg;
	u32 init_val;
};

struct sparx5_main_io_resource {
	enum sparx5_target id;
	phys_addr_t offset;
	int range;
};

static const struct sparx5_main_io_resource sparx5_main_iomap[] =  {
	{ TARGET_CPU,                         0, 0 }, /* 0x600000000 */
	{ TARGET_FDMA,                  0x80000, 0 }, /* 0x600080000 */
	{ TARGET_PCEP,                 0x400000, 0 }, /* 0x600400000 */
	{ TARGET_DEV2G5,             0x10004000, 1 }, /* 0x610004000 */
	{ TARGET_DEV5G,              0x10008000, 1 }, /* 0x610008000 */
	{ TARGET_PCS5G_BR,           0x1000c000, 1 }, /* 0x61000c000 */
	{ TARGET_DEV2G5 +  1,        0x10010000, 1 }, /* 0x610010000 */
	{ TARGET_DEV5G +  1,         0x10014000, 1 }, /* 0x610014000 */
	{ TARGET_PCS5G_BR +  1,      0x10018000, 1 }, /* 0x610018000 */
	{ TARGET_DEV2G5 +  2,        0x1001c000, 1 }, /* 0x61001c000 */
	{ TARGET_DEV5G +  2,         0x10020000, 1 }, /* 0x610020000 */
	{ TARGET_PCS5G_BR +  2,      0x10024000, 1 }, /* 0x610024000 */
	{ TARGET_DEV2G5 +  6,        0x10028000, 1 }, /* 0x610028000 */
	{ TARGET_DEV5G +  6,         0x1002c000, 1 }, /* 0x61002c000 */
	{ TARGET_PCS5G_BR +  6,      0x10030000, 1 }, /* 0x610030000 */
	{ TARGET_DEV2G5 +  7,        0x10034000, 1 }, /* 0x610034000 */
	{ TARGET_DEV5G +  7,         0x10038000, 1 }, /* 0x610038000 */
	{ TARGET_PCS5G_BR +  7,      0x1003c000, 1 }, /* 0x61003c000 */
	{ TARGET_DEV2G5 +  8,        0x10040000, 1 }, /* 0x610040000 */
	{ TARGET_DEV5G +  8,         0x10044000, 1 }, /* 0x610044000 */
	{ TARGET_PCS5G_BR +  8,      0x10048000, 1 }, /* 0x610048000 */
	{ TARGET_DEV2G5 +  9,        0x1004c000, 1 }, /* 0x61004c000 */
	{ TARGET_DEV5G +  9,         0x10050000, 1 }, /* 0x610050000 */
	{ TARGET_PCS5G_BR +  9,      0x10054000, 1 }, /* 0x610054000 */
	{ TARGET_DEV2G5 + 10,        0x10058000, 1 }, /* 0x610058000 */
	{ TARGET_DEV5G + 10,         0x1005c000, 1 }, /* 0x61005c000 */
	{ TARGET_PCS5G_BR + 10,      0x10060000, 1 }, /* 0x610060000 */
	{ TARGET_DEV2G5 + 11,        0x10064000, 1 }, /* 0x610064000 */
	{ TARGET_DEV5G + 11,         0x10068000, 1 }, /* 0x610068000 */
	{ TARGET_PCS5G_BR + 11,      0x1006c000, 1 }, /* 0x61006c000 */
	{ TARGET_DEV2G5 + 12,        0x10070000, 1 }, /* 0x610070000 */
	{ TARGET_DEV10G,             0x10074000, 1 }, /* 0x610074000 */
	{ TARGET_PCS10G_BR,          0x10078000, 1 }, /* 0x610078000 */
	{ TARGET_DEV2G5 + 14,        0x1007c000, 1 }, /* 0x61007c000 */
	{ TARGET_DEV10G +  2,        0x10080000, 1 }, /* 0x610080000 */
	{ TARGET_PCS10G_BR +  2,     0x10084000, 1 }, /* 0x610084000 */
	{ TARGET_DEV2G5 + 15,        0x10088000, 1 }, /* 0x610088000 */
	{ TARGET_DEV10G +  3,        0x1008c000, 1 }, /* 0x61008c000 */
	{ TARGET_PCS10G_BR +  3,     0x10090000, 1 }, /* 0x610090000 */
	{ TARGET_DEV2G5 + 16,        0x10094000, 1 }, /* 0x610094000 */
	{ TARGET_DEV2G5 + 17,        0x10098000, 1 }, /* 0x610098000 */
	{ TARGET_DEV2G5 + 18,        0x1009c000, 1 }, /* 0x61009c000 */
	{ TARGET_DEV2G5 + 19,        0x100a0000, 1 }, /* 0x6100a0000 */
	{ TARGET_DEV2G5 + 20,        0x100a4000, 1 }, /* 0x6100a4000 */
	{ TARGET_DEV2G5 + 21,        0x100a8000, 1 }, /* 0x6100a8000 */
	{ TARGET_DEV2G5 + 22,        0x100ac000, 1 }, /* 0x6100ac000 */
	{ TARGET_DEV2G5 + 23,        0x100b0000, 1 }, /* 0x6100b0000 */
	{ TARGET_DEV2G5 + 32,        0x100b4000, 1 }, /* 0x6100b4000 */
	{ TARGET_DEV2G5 + 33,        0x100b8000, 1 }, /* 0x6100b8000 */
	{ TARGET_DEV2G5 + 34,        0x100bc000, 1 }, /* 0x6100bc000 */
	{ TARGET_DEV2G5 + 35,        0x100c0000, 1 }, /* 0x6100c0000 */
	{ TARGET_DEV2G5 + 36,        0x100c4000, 1 }, /* 0x6100c4000 */
	{ TARGET_DEV2G5 + 37,        0x100c8000, 1 }, /* 0x6100c8000 */
	{ TARGET_DEV2G5 + 38,        0x100cc000, 1 }, /* 0x6100cc000 */
	{ TARGET_DEV2G5 + 39,        0x100d0000, 1 }, /* 0x6100d0000 */
	{ TARGET_DEV2G5 + 40,        0x100d4000, 1 }, /* 0x6100d4000 */
	{ TARGET_DEV2G5 + 41,        0x100d8000, 1 }, /* 0x6100d8000 */
	{ TARGET_DEV2G5 + 42,        0x100dc000, 1 }, /* 0x6100dc000 */
	{ TARGET_DEV2G5 + 43,        0x100e0000, 1 }, /* 0x6100e0000 */
	{ TARGET_DEV2G5 + 44,        0x100e4000, 1 }, /* 0x6100e4000 */
	{ TARGET_DEV2G5 + 45,        0x100e8000, 1 }, /* 0x6100e8000 */
	{ TARGET_DEV2G5 + 46,        0x100ec000, 1 }, /* 0x6100ec000 */
	{ TARGET_DEV2G5 + 47,        0x100f0000, 1 }, /* 0x6100f0000 */
	{ TARGET_DEV2G5 + 57,        0x100f4000, 1 }, /* 0x6100f4000 */
	{ TARGET_DEV25G +  1,        0x100f8000, 1 }, /* 0x6100f8000 */
	{ TARGET_PCS25G_BR +  1,     0x100fc000, 1 }, /* 0x6100fc000 */
	{ TARGET_DEV2G5 + 59,        0x10104000, 1 }, /* 0x610104000 */
	{ TARGET_DEV25G +  3,        0x10108000, 1 }, /* 0x610108000 */
	{ TARGET_PCS25G_BR +  3,     0x1010c000, 1 }, /* 0x61010c000 */
	{ TARGET_DEV2G5 + 60,        0x10114000, 1 }, /* 0x610114000 */
	{ TARGET_DEV25G +  4,        0x10118000, 1 }, /* 0x610118000 */
	{ TARGET_PCS25G_BR +  4,     0x1011c000, 1 }, /* 0x61011c000 */
	{ TARGET_DEV2G5 + 64,        0x10124000, 1 }, /* 0x610124000 */
	{ TARGET_DEV5G + 12,         0x10128000, 1 }, /* 0x610128000 */
	{ TARGET_PCS5G_BR + 12,      0x1012c000, 1 }, /* 0x61012c000 */
	{ TARGET_PORT_CONF,          0x10130000, 1 }, /* 0x610130000 */
	{ TARGET_DEV2G5 +  3,        0x10404000, 1 }, /* 0x610404000 */
	{ TARGET_DEV5G +  3,         0x10408000, 1 }, /* 0x610408000 */
	{ TARGET_PCS5G_BR +  3,      0x1040c000, 1 }, /* 0x61040c000 */
	{ TARGET_DEV2G5 +  4,        0x10410000, 1 }, /* 0x610410000 */
	{ TARGET_DEV5G +  4,         0x10414000, 1 }, /* 0x610414000 */
	{ TARGET_PCS5G_BR +  4,      0x10418000, 1 }, /* 0x610418000 */
	{ TARGET_DEV2G5 +  5,        0x1041c000, 1 }, /* 0x61041c000 */
	{ TARGET_DEV5G +  5,         0x10420000, 1 }, /* 0x610420000 */
	{ TARGET_PCS5G_BR +  5,      0x10424000, 1 }, /* 0x610424000 */
	{ TARGET_DEV2G5 + 13,        0x10428000, 1 }, /* 0x610428000 */
	{ TARGET_DEV10G +  1,        0x1042c000, 1 }, /* 0x61042c000 */
	{ TARGET_PCS10G_BR +  1,     0x10430000, 1 }, /* 0x610430000 */
	{ TARGET_DEV2G5 + 24,        0x10434000, 1 }, /* 0x610434000 */
	{ TARGET_DEV2G5 + 25,        0x10438000, 1 }, /* 0x610438000 */
	{ TARGET_DEV2G5 + 26,        0x1043c000, 1 }, /* 0x61043c000 */
	{ TARGET_DEV2G5 + 27,        0x10440000, 1 }, /* 0x610440000 */
	{ TARGET_DEV2G5 + 28,        0x10444000, 1 }, /* 0x610444000 */
	{ TARGET_DEV2G5 + 29,        0x10448000, 1 }, /* 0x610448000 */
	{ TARGET_DEV2G5 + 30,        0x1044c000, 1 }, /* 0x61044c000 */
	{ TARGET_DEV2G5 + 31,        0x10450000, 1 }, /* 0x610450000 */
	{ TARGET_DEV2G5 + 48,        0x10454000, 1 }, /* 0x610454000 */
	{ TARGET_DEV10G +  4,        0x10458000, 1 }, /* 0x610458000 */
	{ TARGET_PCS10G_BR +  4,     0x1045c000, 1 }, /* 0x61045c000 */
	{ TARGET_DEV2G5 + 49,        0x10460000, 1 }, /* 0x610460000 */
	{ TARGET_DEV10G +  5,        0x10464000, 1 }, /* 0x610464000 */
	{ TARGET_PCS10G_BR +  5,     0x10468000, 1 }, /* 0x610468000 */
	{ TARGET_DEV2G5 + 50,        0x1046c000, 1 }, /* 0x61046c000 */
	{ TARGET_DEV10G +  6,        0x10470000, 1 }, /* 0x610470000 */
	{ TARGET_PCS10G_BR +  6,     0x10474000, 1 }, /* 0x610474000 */
	{ TARGET_DEV2G5 + 51,        0x10478000, 1 }, /* 0x610478000 */
	{ TARGET_DEV10G +  7,        0x1047c000, 1 }, /* 0x61047c000 */
	{ TARGET_PCS10G_BR +  7,     0x10480000, 1 }, /* 0x610480000 */
	{ TARGET_DEV2G5 + 52,        0x10484000, 1 }, /* 0x610484000 */
	{ TARGET_DEV10G +  8,        0x10488000, 1 }, /* 0x610488000 */
	{ TARGET_PCS10G_BR +  8,     0x1048c000, 1 }, /* 0x61048c000 */
	{ TARGET_DEV2G5 + 53,        0x10490000, 1 }, /* 0x610490000 */
	{ TARGET_DEV10G +  9,        0x10494000, 1 }, /* 0x610494000 */
	{ TARGET_PCS10G_BR +  9,     0x10498000, 1 }, /* 0x610498000 */
	{ TARGET_DEV2G5 + 54,        0x1049c000, 1 }, /* 0x61049c000 */
	{ TARGET_DEV10G + 10,        0x104a0000, 1 }, /* 0x6104a0000 */
	{ TARGET_PCS10G_BR + 10,     0x104a4000, 1 }, /* 0x6104a4000 */
	{ TARGET_DEV2G5 + 55,        0x104a8000, 1 }, /* 0x6104a8000 */
	{ TARGET_DEV10G + 11,        0x104ac000, 1 }, /* 0x6104ac000 */
	{ TARGET_PCS10G_BR + 11,     0x104b0000, 1 }, /* 0x6104b0000 */
	{ TARGET_DEV2G5 + 56,        0x104b4000, 1 }, /* 0x6104b4000 */
	{ TARGET_DEV25G,             0x104b8000, 1 }, /* 0x6104b8000 */
	{ TARGET_PCS25G_BR,          0x104bc000, 1 }, /* 0x6104bc000 */
	{ TARGET_DEV2G5 + 58,        0x104c4000, 1 }, /* 0x6104c4000 */
	{ TARGET_DEV25G +  2,        0x104c8000, 1 }, /* 0x6104c8000 */
	{ TARGET_PCS25G_BR +  2,     0x104cc000, 1 }, /* 0x6104cc000 */
	{ TARGET_DEV2G5 + 61,        0x104d4000, 1 }, /* 0x6104d4000 */
	{ TARGET_DEV25G +  5,        0x104d8000, 1 }, /* 0x6104d8000 */
	{ TARGET_PCS25G_BR +  5,     0x104dc000, 1 }, /* 0x6104dc000 */
	{ TARGET_DEV2G5 + 62,        0x104e4000, 1 }, /* 0x6104e4000 */
	{ TARGET_DEV25G +  6,        0x104e8000, 1 }, /* 0x6104e8000 */
	{ TARGET_PCS25G_BR +  6,     0x104ec000, 1 }, /* 0x6104ec000 */
	{ TARGET_DEV2G5 + 63,        0x104f4000, 1 }, /* 0x6104f4000 */
	{ TARGET_DEV25G +  7,        0x104f8000, 1 }, /* 0x6104f8000 */
	{ TARGET_PCS25G_BR +  7,     0x104fc000, 1 }, /* 0x6104fc000 */
	{ TARGET_DSM,                0x10504000, 1 }, /* 0x610504000 */
	{ TARGET_ASM,                0x10600000, 1 }, /* 0x610600000 */
	{ TARGET_GCB,                0x11010000, 2 }, /* 0x611010000 */
	{ TARGET_QS,                 0x11030000, 2 }, /* 0x611030000 */
	{ TARGET_PTP,                0x11040000, 2 }, /* 0x611040000 */
	{ TARGET_ANA_ACL,            0x11050000, 2 }, /* 0x611050000 */
	{ TARGET_LRN,                0x11060000, 2 }, /* 0x611060000 */
	{ TARGET_VCAP_SUPER,         0x11080000, 2 }, /* 0x611080000 */
	{ TARGET_QSYS,               0x110a0000, 2 }, /* 0x6110a0000 */
	{ TARGET_QFWD,               0x110b0000, 2 }, /* 0x6110b0000 */
	{ TARGET_XQS,                0x110c0000, 2 }, /* 0x6110c0000 */
	{ TARGET_VCAP_ES2,           0x110d0000, 2 }, /* 0x6110d0000 */
	{ TARGET_VCAP_ES0,           0x110e0000, 2 }, /* 0x6110e0000 */
	{ TARGET_CLKGEN,             0x11100000, 2 }, /* 0x611100000 */
	{ TARGET_ANA_AC_POL,         0x11200000, 2 }, /* 0x611200000 */
	{ TARGET_QRES,               0x11280000, 2 }, /* 0x611280000 */
	{ TARGET_EACL,               0x112c0000, 2 }, /* 0x6112c0000 */
	{ TARGET_ANA_CL,             0x11400000, 2 }, /* 0x611400000 */
	{ TARGET_ANA_L3,             0x11480000, 2 }, /* 0x611480000 */
	{ TARGET_ANA_AC_SDLB,        0x11500000, 2 }, /* 0x611500000 */
	{ TARGET_HSCH,               0x11580000, 2 }, /* 0x611580000 */
	{ TARGET_REW,                0x11600000, 2 }, /* 0x611600000 */
	{ TARGET_ANA_L2,             0x11800000, 2 }, /* 0x611800000 */
	{ TARGET_ANA_AC,             0x11900000, 2 }, /* 0x611900000 */
	{ TARGET_VOP,                0x11a00000, 2 }, /* 0x611a00000 */
};

static int sparx5_create_targets(struct sparx5 *sparx5)
{
	struct resource *iores[IO_RANGES];
	void __iomem *iomem[IO_RANGES];
	void __iomem *begin[IO_RANGES];
	int range_id[IO_RANGES];
	int idx, jdx;

	for (idx = 0, jdx = 0; jdx < ARRAY_SIZE(sparx5_main_iomap); jdx++) {
		const struct sparx5_main_io_resource *iomap = &sparx5_main_iomap[jdx];

		if (idx == iomap->range) {
			range_id[idx] = jdx;
			idx++;
		}
	}
	for (idx = 0; idx < IO_RANGES; idx++) {
		iores[idx] = platform_get_resource(sparx5->pdev, IORESOURCE_MEM,
						   idx);
		if (!iores[idx]) {
			dev_err(sparx5->dev, "Invalid resource\n");
			return -EINVAL;
		}
		iomem[idx] = devm_ioremap(sparx5->dev,
					  iores[idx]->start,
					  resource_size(iores[idx]));
		if (!iomem[idx]) {
			dev_err(sparx5->dev, "Unable to get switch registers: %s\n",
				iores[idx]->name);
			return -ENOMEM;
		}
		begin[idx] = iomem[idx] - sparx5_main_iomap[range_id[idx]].offset;
	}
	for (jdx = 0; jdx < ARRAY_SIZE(sparx5_main_iomap); jdx++) {
		const struct sparx5_main_io_resource *iomap = &sparx5_main_iomap[jdx];

		sparx5->regs[iomap->id] = begin[iomap->range] + iomap->offset;
	}
	return 0;
}

static int sparx5_create_port(struct sparx5 *sparx5,
			      struct initial_port_config *config)
{
	struct sparx5_port *spx5_port;
	struct net_device *ndev;
	struct phylink *phylink;
	int err;

	ndev = sparx5_create_netdev(sparx5, config->portno);
	if (IS_ERR(ndev)) {
		dev_err(sparx5->dev, "Could not create net device: %02u\n",
			config->portno);
		return PTR_ERR(ndev);
	}
	spx5_port = netdev_priv(ndev);
	spx5_port->of_node = config->node;
	spx5_port->serdes = config->serdes;
	spx5_port->pvid = NULL_VID;
	spx5_port->signd_internal = true;
	spx5_port->signd_active_high = true;
	spx5_port->signd_enable = true;
	spx5_port->max_vlan_tags = SPX5_PORT_MAX_TAGS_NONE;
	spx5_port->vlan_type = SPX5_VLAN_PORT_TYPE_UNAWARE;
	spx5_port->custom_etype = 0x8880; /* Vitesse */
	spx5_port->phylink_pcs.poll = true;
	spx5_port->phylink_pcs.ops = &sparx5_phylink_pcs_ops;
	spx5_port->is_mrouter = false;
	sparx5->ports[config->portno] = spx5_port;

	err = sparx5_port_init(sparx5, spx5_port, &config->conf);
	if (err) {
		dev_err(sparx5->dev, "port init failed\n");
		return err;
	}
	spx5_port->conf = config->conf;

	/* Setup VLAN */
	sparx5_vlan_port_setup(sparx5, spx5_port->portno);

	/* Create a phylink for PHY management.  Also handles SFPs */
	spx5_port->phylink_config.dev = &spx5_port->ndev->dev;
	spx5_port->phylink_config.type = PHYLINK_NETDEV;
	spx5_port->phylink_config.mac_capabilities = MAC_ASYM_PAUSE |
		MAC_SYM_PAUSE | MAC_10 | MAC_100 | MAC_1000FD |
		MAC_2500FD | MAC_5000FD | MAC_10000FD | MAC_25000FD;

	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  spx5_port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_QSGMII,
		  spx5_port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  spx5_port->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX,
		  spx5_port->phylink_config.supported_interfaces);

	if (spx5_port->conf.bandwidth == SPEED_5000 ||
	    spx5_port->conf.bandwidth == SPEED_10000 ||
	    spx5_port->conf.bandwidth == SPEED_25000)
		__set_bit(PHY_INTERFACE_MODE_5GBASER,
			  spx5_port->phylink_config.supported_interfaces);

	if (spx5_port->conf.bandwidth == SPEED_10000 ||
	    spx5_port->conf.bandwidth == SPEED_25000)
		__set_bit(PHY_INTERFACE_MODE_10GBASER,
			  spx5_port->phylink_config.supported_interfaces);

	if (spx5_port->conf.bandwidth == SPEED_25000)
		__set_bit(PHY_INTERFACE_MODE_25GBASER,
			  spx5_port->phylink_config.supported_interfaces);

	phylink = phylink_create(&spx5_port->phylink_config,
				 of_fwnode_handle(config->node),
				 config->conf.phy_mode,
				 &sparx5_phylink_mac_ops);
	if (IS_ERR(phylink))
		return PTR_ERR(phylink);

	spx5_port->phylink = phylink;

	return 0;
}

static int sparx5_init_ram(struct sparx5 *s5)
{
	const struct sparx5_ram_config spx5_ram_cfg[] = {
		{spx5_reg_get(s5, ANA_AC_STAT_RESET), ANA_AC_STAT_RESET_RESET},
		{spx5_reg_get(s5, ASM_STAT_CFG), ASM_STAT_CFG_STAT_CNT_CLR_SHOT},
		{spx5_reg_get(s5, QSYS_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, REW_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, VOP_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, ANA_AC_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, ASM_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, EACL_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, VCAP_SUPER_RAM_INIT), QSYS_RAM_INIT_RAM_INIT},
		{spx5_reg_get(s5, DSM_RAM_INIT), QSYS_RAM_INIT_RAM_INIT}
	};
	const struct sparx5_ram_config *cfg;
	u32 value, pending, jdx, idx;

	for (jdx = 0; jdx < 10; jdx++) {
		pending = ARRAY_SIZE(spx5_ram_cfg);
		for (idx = 0; idx < ARRAY_SIZE(spx5_ram_cfg); idx++) {
			cfg = &spx5_ram_cfg[idx];
			if (jdx == 0) {
				writel(cfg->init_val, cfg->init_reg);
			} else {
				value = readl(cfg->init_reg);
				if ((value & cfg->init_val) != cfg->init_val)
					pending--;
			}
		}
		if (!pending)
			break;
		usleep_range(USEC_PER_MSEC, 2 * USEC_PER_MSEC);
	}

	if (pending > 0) {
		/* Still initializing, should be complete in
		 * less than 1ms
		 */
		dev_err(s5->dev, "Memory initialization error\n");
		return -EINVAL;
	}
	return 0;
}

static int sparx5_init_switchcore(struct sparx5 *sparx5)
{
	u32 value;
	int err = 0;

	spx5_rmw(EACL_POL_EACL_CFG_EACL_FORCE_INIT_SET(1),
		 EACL_POL_EACL_CFG_EACL_FORCE_INIT,
		 sparx5,
		 EACL_POL_EACL_CFG);

	spx5_rmw(EACL_POL_EACL_CFG_EACL_FORCE_INIT_SET(0),
		 EACL_POL_EACL_CFG_EACL_FORCE_INIT,
		 sparx5,
		 EACL_POL_EACL_CFG);

	/* Initialize memories, if not done already */
	value = spx5_rd(sparx5, HSCH_RESET_CFG);
	if (!(value & HSCH_RESET_CFG_CORE_ENA)) {
		err = sparx5_init_ram(sparx5);
		if (err)
			return err;
	}

	/* Reset counters */
	spx5_wr(ANA_AC_STAT_RESET_RESET_SET(1), sparx5, ANA_AC_STAT_RESET);
	spx5_wr(ASM_STAT_CFG_STAT_CNT_CLR_SHOT_SET(1), sparx5, ASM_STAT_CFG);

	/* Enable switch-core and queue system */
	spx5_wr(HSCH_RESET_CFG_CORE_ENA_SET(1), sparx5, HSCH_RESET_CFG);

	return 0;
}

static int sparx5_init_coreclock(struct sparx5 *sparx5)
{
	enum sparx5_core_clockfreq freq = sparx5->coreclock;
	u32 clk_div, clk_period, pol_upd_int, idx;

	/* Verify if core clock frequency is supported on target.
	 * If 'VTSS_CORE_CLOCK_DEFAULT' then the highest supported
	 * freq. is used
	 */
	switch (sparx5->target_ct) {
	case SPX5_TARGET_CT_7546:
		if (sparx5->coreclock == SPX5_CORE_CLOCK_DEFAULT)
			freq = SPX5_CORE_CLOCK_250MHZ;
		else if (sparx5->coreclock != SPX5_CORE_CLOCK_250MHZ)
			freq = 0; /* Not supported */
		break;
	case SPX5_TARGET_CT_7549:
	case SPX5_TARGET_CT_7552:
	case SPX5_TARGET_CT_7556:
		if (sparx5->coreclock == SPX5_CORE_CLOCK_DEFAULT)
			freq = SPX5_CORE_CLOCK_500MHZ;
		else if (sparx5->coreclock != SPX5_CORE_CLOCK_500MHZ)
			freq = 0; /* Not supported */
		break;
	case SPX5_TARGET_CT_7558:
	case SPX5_TARGET_CT_7558TSN:
		if (sparx5->coreclock == SPX5_CORE_CLOCK_DEFAULT)
			freq = SPX5_CORE_CLOCK_625MHZ;
		else if (sparx5->coreclock != SPX5_CORE_CLOCK_625MHZ)
			freq = 0; /* Not supported */
		break;
	case SPX5_TARGET_CT_7546TSN:
		if (sparx5->coreclock == SPX5_CORE_CLOCK_DEFAULT)
			freq = SPX5_CORE_CLOCK_625MHZ;
		break;
	case SPX5_TARGET_CT_7549TSN:
	case SPX5_TARGET_CT_7552TSN:
	case SPX5_TARGET_CT_7556TSN:
		if (sparx5->coreclock == SPX5_CORE_CLOCK_DEFAULT)
			freq = SPX5_CORE_CLOCK_625MHZ;
		else if (sparx5->coreclock == SPX5_CORE_CLOCK_250MHZ)
			freq = 0; /* Not supported */
		break;
	default:
		dev_err(sparx5->dev, "Target (%#04x) not supported\n",
			sparx5->target_ct);
		return -ENODEV;
	}

	switch (freq) {
	case SPX5_CORE_CLOCK_250MHZ:
		clk_div = 10;
		pol_upd_int = 312;
		break;
	case SPX5_CORE_CLOCK_500MHZ:
		clk_div = 5;
		pol_upd_int = 624;
		break;
	case SPX5_CORE_CLOCK_625MHZ:
		clk_div = 4;
		pol_upd_int = 780;
		break;
	default:
		dev_err(sparx5->dev, "%d coreclock not supported on (%#04x)\n",
			sparx5->coreclock, sparx5->target_ct);
		return -EINVAL;
	}

	/* Update state with chosen frequency */
	sparx5->coreclock = freq;

	/* Configure the LCPLL */
	spx5_rmw(CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_CLK_DIV_SET(clk_div) |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_PRE_DIV_SET(0) |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_ROT_DIR_SET(0) |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_ROT_SEL_SET(0) |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_ROT_ENA_SET(0) |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_CLK_ENA_SET(1),
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_CLK_DIV |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_PRE_DIV |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_ROT_DIR |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_ROT_SEL |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_ROT_ENA |
		 CLKGEN_LCPLL1_CORE_CLK_CFG_CORE_CLK_ENA,
		 sparx5,
		 CLKGEN_LCPLL1_CORE_CLK_CFG);

	clk_period = sparx5_clk_period(freq);

	spx5_rmw(HSCH_SYS_CLK_PER_100PS_SET(clk_period / 100),
		 HSCH_SYS_CLK_PER_100PS,
		 sparx5,
		 HSCH_SYS_CLK_PER);

	spx5_rmw(ANA_AC_POL_BDLB_DLB_CTRL_CLK_PERIOD_01NS_SET(clk_period / 100),
		 ANA_AC_POL_BDLB_DLB_CTRL_CLK_PERIOD_01NS,
		 sparx5,
		 ANA_AC_POL_BDLB_DLB_CTRL);

	spx5_rmw(ANA_AC_POL_SLB_DLB_CTRL_CLK_PERIOD_01NS_SET(clk_period / 100),
		 ANA_AC_POL_SLB_DLB_CTRL_CLK_PERIOD_01NS,
		 sparx5,
		 ANA_AC_POL_SLB_DLB_CTRL);

	spx5_rmw(LRN_AUTOAGE_CFG_1_CLK_PERIOD_01NS_SET(clk_period / 100),
		 LRN_AUTOAGE_CFG_1_CLK_PERIOD_01NS,
		 sparx5,
		 LRN_AUTOAGE_CFG_1);

	for (idx = 0; idx < 3; idx++)
		spx5_rmw(GCB_SIO_CLOCK_SYS_CLK_PERIOD_SET(clk_period / 100),
			 GCB_SIO_CLOCK_SYS_CLK_PERIOD,
			 sparx5,
			 GCB_SIO_CLOCK(idx));

	spx5_rmw(HSCH_TAS_STATEMACHINE_CFG_REVISIT_DLY_SET
		 ((256 * 1000) / clk_period),
		 HSCH_TAS_STATEMACHINE_CFG_REVISIT_DLY,
		 sparx5,
		 HSCH_TAS_STATEMACHINE_CFG);

	spx5_rmw(ANA_AC_POL_POL_UPD_INT_CFG_POL_UPD_INT_SET(pol_upd_int),
		 ANA_AC_POL_POL_UPD_INT_CFG_POL_UPD_INT,
		 sparx5,
		 ANA_AC_POL_POL_UPD_INT_CFG);

	return 0;
}

static int sparx5_qlim_set(struct sparx5 *sparx5)
{
	u32 res, dp, prio;

	for (res = 0; res < 2; res++) {
		for (prio = 0; prio < 8; prio++)
			spx5_wr(0xFFF, sparx5,
				QRES_RES_CFG(prio + 630 + res * 1024));

		for (dp = 0; dp < 4; dp++)
			spx5_wr(0xFFF, sparx5,
				QRES_RES_CFG(dp + 638 + res * 1024));
	}

	/* Set 80,90,95,100% of memory size for top watermarks */
	spx5_wr(QLIM_WM(80), sparx5, XQS_QLIMIT_SHR_QLIM_CFG(0));
	spx5_wr(QLIM_WM(90), sparx5, XQS_QLIMIT_SHR_CTOP_CFG(0));
	spx5_wr(QLIM_WM(95), sparx5, XQS_QLIMIT_SHR_ATOP_CFG(0));
	spx5_wr(QLIM_WM(100), sparx5, XQS_QLIMIT_SHR_TOP_CFG(0));

	return 0;
}

/* Some boards needs to map the SGPIO for signal detect explicitly to the
 * port module
 */
static void sparx5_board_init(struct sparx5 *sparx5)
{
	int idx;

	if (!sparx5->sd_sgpio_remapping)
		return;

	/* Enable SGPIO Signal Detect remapping */
	spx5_rmw(GCB_HW_SGPIO_SD_CFG_SD_MAP_SEL,
		 GCB_HW_SGPIO_SD_CFG_SD_MAP_SEL,
		 sparx5,
		 GCB_HW_SGPIO_SD_CFG);

	/* Refer to LOS SGPIO */
	for (idx = 0; idx < SPX5_PORTS; idx++)
		if (sparx5->ports[idx])
			if (sparx5->ports[idx]->conf.sd_sgpio != ~0)
				spx5_wr(sparx5->ports[idx]->conf.sd_sgpio,
					sparx5,
					GCB_HW_SGPIO_TO_SD_MAP_CFG(idx));
}

static int sparx5_start(struct sparx5 *sparx5)
{
	u8 broadcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	char queue_name[32];
	u32 idx;
	int err;

	/* Setup own UPSIDs */
	for (idx = 0; idx < 3; idx++) {
		spx5_wr(idx, sparx5, ANA_AC_OWN_UPSID(idx));
		spx5_wr(idx, sparx5, ANA_CL_OWN_UPSID(idx));
		spx5_wr(idx, sparx5, ANA_L2_OWN_UPSID(idx));
		spx5_wr(idx, sparx5, REW_OWN_UPSID(idx));
	}

	/* Enable CPU ports */
	for (idx = SPX5_PORTS; idx < SPX5_PORTS_ALL; idx++)
		spx5_rmw(QFWD_SWITCH_PORT_MODE_PORT_ENA_SET(1),
			 QFWD_SWITCH_PORT_MODE_PORT_ENA,
			 sparx5,
			 QFWD_SWITCH_PORT_MODE(idx));

	/* Init masks */
	sparx5_update_fwd(sparx5);

	/* CPU copy CPU pgids */
	spx5_wr(ANA_AC_PGID_MISC_CFG_PGID_CPU_COPY_ENA_SET(1),
		sparx5, ANA_AC_PGID_MISC_CFG(PGID_CPU));
	spx5_wr(ANA_AC_PGID_MISC_CFG_PGID_CPU_COPY_ENA_SET(1),
		sparx5, ANA_AC_PGID_MISC_CFG(PGID_BCAST));

	/* Recalc injected frame FCS */
	for (idx = SPX5_PORT_CPU_0; idx <= SPX5_PORT_CPU_1; idx++)
		spx5_rmw(ANA_CL_FILTER_CTRL_FORCE_FCS_UPDATE_ENA_SET(1),
			 ANA_CL_FILTER_CTRL_FORCE_FCS_UPDATE_ENA,
			 sparx5, ANA_CL_FILTER_CTRL(idx));

	/* Init MAC table, ageing */
	sparx5_mact_init(sparx5);

	/* Init PGID table arbitrator */
	sparx5_pgid_init(sparx5);

	/* Setup VLANs */
	sparx5_vlan_init(sparx5);

	/* Add host mode BC address (points only to CPU) */
	sparx5_mact_learn(sparx5, PGID_CPU, broadcast, NULL_VID);

	/* Enable queue limitation watermarks */
	sparx5_qlim_set(sparx5);

	err = sparx5_config_auto_calendar(sparx5);
	if (err)
		return err;

	err = sparx5_config_dsm_calendar(sparx5);
	if (err)
		return err;

	/* Init stats */
	err = sparx_stats_init(sparx5);
	if (err)
		return err;

	/* Init mact_sw struct */
	mutex_init(&sparx5->mact_lock);
	INIT_LIST_HEAD(&sparx5->mact_entries);
	snprintf(queue_name, sizeof(queue_name), "%s-mact",
		 dev_name(sparx5->dev));
	sparx5->mact_queue = create_singlethread_workqueue(queue_name);
	if (!sparx5->mact_queue)
		return -ENOMEM;

	INIT_DELAYED_WORK(&sparx5->mact_work, sparx5_mact_pull_work);
	queue_delayed_work(sparx5->mact_queue, &sparx5->mact_work,
			   SPX5_MACT_PULL_DELAY);

	mutex_init(&sparx5->mdb_lock);
	INIT_LIST_HEAD(&sparx5->mdb_entries);

	err = sparx5_register_netdevs(sparx5);
	if (err)
		return err;

	sparx5_board_init(sparx5);
	err = sparx5_register_notifier_blocks(sparx5);
	if (err)
		return err;

	err = sparx5_vcap_init(sparx5);
	if (err) {
		sparx5_unregister_notifier_blocks(sparx5);
		return err;
	}

	/* Start Frame DMA with fallback to register based INJ/XTR */
	err = -ENXIO;
	if (sparx5->fdma_irq >= 0) {
		if (GCB_CHIP_ID_REV_ID_GET(sparx5->chip_id) > 0)
			err = devm_request_threaded_irq(sparx5->dev,
							sparx5->fdma_irq,
							NULL,
							sparx5_fdma_handler,
							IRQF_ONESHOT,
							"sparx5-fdma", sparx5);
		if (!err)
			err = sparx5_fdma_start(sparx5);
		if (err)
			sparx5->fdma_irq = -ENXIO;
	} else {
		sparx5->fdma_irq = -ENXIO;
	}
	if (err && sparx5->xtr_irq >= 0) {
		err = devm_request_irq(sparx5->dev, sparx5->xtr_irq,
				       sparx5_xtr_handler, IRQF_SHARED,
				       "sparx5-xtr", sparx5);
		if (!err)
			err = sparx5_manual_injection_mode(sparx5);
		if (err)
			sparx5->xtr_irq = -ENXIO;
	} else {
		sparx5->xtr_irq = -ENXIO;
	}

	if (sparx5->ptp_irq >= 0) {
		err = devm_request_threaded_irq(sparx5->dev, sparx5->ptp_irq,
						NULL, sparx5_ptp_irq_handler,
						IRQF_ONESHOT, "sparx5-ptp",
						sparx5);
		if (err)
			sparx5->ptp_irq = -ENXIO;

		sparx5->ptp = 1;
	}

	return err;
}

static void sparx5_cleanup_ports(struct sparx5 *sparx5)
{
	sparx5_unregister_netdevs(sparx5);
	sparx5_destroy_netdevs(sparx5);
}

static int mchp_sparx5_probe(struct platform_device *pdev)
{
	struct initial_port_config *configs, *config;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ports, *portnp;
	struct reset_control *reset;
	struct sparx5 *sparx5;
	int idx = 0, err = 0;

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	sparx5 = devm_kzalloc(&pdev->dev, sizeof(*sparx5), GFP_KERNEL);
	if (!sparx5)
		return -ENOMEM;

	platform_set_drvdata(pdev, sparx5);
	sparx5->pdev = pdev;
	sparx5->dev = &pdev->dev;

	/* Do switch core reset if available */
	reset = devm_reset_control_get_optional_shared(&pdev->dev, "switch");
	if (IS_ERR(reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(reset),
				     "Failed to get switch reset controller.\n");
	reset_control_reset(reset);

	/* Default values, some from DT */
	sparx5->coreclock = SPX5_CORE_CLOCK_DEFAULT;

	sparx5->debugfs_root = debugfs_create_dir("sparx5", NULL);

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(sparx5->dev, "no ethernet-ports child node found\n");
		return -ENODEV;
	}
	sparx5->port_count = of_get_child_count(ports);

	configs = kcalloc(sparx5->port_count,
			  sizeof(struct initial_port_config), GFP_KERNEL);
	if (!configs) {
		err = -ENOMEM;
		goto cleanup_pnode;
	}

	for_each_available_child_of_node(ports, portnp) {
		struct sparx5_port_config *conf;
		struct phy *serdes;
		u32 portno;

		err = of_property_read_u32(portnp, "reg", &portno);
		if (err) {
			dev_err(sparx5->dev, "port reg property error\n");
			continue;
		}
		config = &configs[idx];
		conf = &config->conf;
		conf->speed = SPEED_UNKNOWN;
		conf->bandwidth = SPEED_UNKNOWN;
		err = of_get_phy_mode(portnp, &conf->phy_mode);
		if (err) {
			dev_err(sparx5->dev, "port %u: missing phy-mode\n",
				portno);
			continue;
		}
		err = of_property_read_u32(portnp, "microchip,bandwidth",
					   &conf->bandwidth);
		if (err) {
			dev_err(sparx5->dev, "port %u: missing bandwidth\n",
				portno);
			continue;
		}
		err = of_property_read_u32(portnp, "microchip,sd-sgpio", &conf->sd_sgpio);
		if (err)
			conf->sd_sgpio = ~0;
		else
			sparx5->sd_sgpio_remapping = true;
		serdes = devm_of_phy_get(sparx5->dev, portnp, NULL);
		if (IS_ERR(serdes)) {
			err = dev_err_probe(sparx5->dev, PTR_ERR(serdes),
					    "port %u: missing serdes\n",
					    portno);
			of_node_put(portnp);
			goto cleanup_config;
		}
		config->portno = portno;
		config->node = portnp;
		config->serdes = serdes;

		conf->media = PHY_MEDIA_DAC;
		conf->serdes_reset = true;
		conf->portmode = conf->phy_mode;
		conf->power_down = true;
		idx++;
	}

	err = sparx5_create_targets(sparx5);
	if (err)
		goto cleanup_config;

	if (of_get_mac_address(np, sparx5->base_mac)) {
		dev_info(sparx5->dev, "MAC addr was not set, use random MAC\n");
		eth_random_addr(sparx5->base_mac);
		sparx5->base_mac[5] = 0;
	}

	sparx5->fdma_irq = platform_get_irq_byname(sparx5->pdev, "fdma");
	sparx5->xtr_irq = platform_get_irq_byname(sparx5->pdev, "xtr");
	sparx5->ptp_irq = platform_get_irq_byname(sparx5->pdev, "ptp");

	/* Read chip ID to check CPU interface */
	sparx5->chip_id = spx5_rd(sparx5, GCB_CHIP_ID);

	sparx5->target_ct = (enum spx5_target_chiptype)
		GCB_CHIP_ID_PART_ID_GET(sparx5->chip_id);

	/* Initialize Switchcore and internal RAMs */
	err = sparx5_init_switchcore(sparx5);
	if (err) {
		dev_err(sparx5->dev, "Switchcore initialization error\n");
		goto cleanup_config;
	}

	/* Initialize the LC-PLL (core clock) and set affected registers */
	err = sparx5_init_coreclock(sparx5);
	if (err) {
		dev_err(sparx5->dev, "LC-PLL initialization error\n");
		goto cleanup_config;
	}

	for (idx = 0; idx < sparx5->port_count; ++idx) {
		config = &configs[idx];
		if (!config->node)
			continue;

		err = sparx5_create_port(sparx5, config);
		if (err) {
			dev_err(sparx5->dev, "port create error\n");
			goto cleanup_ports;
		}
	}

	err = sparx5_start(sparx5);
	if (err) {
		dev_err(sparx5->dev, "Start failed\n");
		goto cleanup_ports;
	}

	err = sparx5_qos_init(sparx5);
	if (err) {
		dev_err(sparx5->dev, "Failed to initialize QoS\n");
		goto cleanup_ports;
	}

	err = sparx5_ptp_init(sparx5);
	if (err) {
		dev_err(sparx5->dev, "PTP failed\n");
		goto cleanup_ports;
	}
	goto cleanup_config;

cleanup_ports:
	sparx5_cleanup_ports(sparx5);
	if (sparx5->mact_queue)
		destroy_workqueue(sparx5->mact_queue);
cleanup_config:
	kfree(configs);
cleanup_pnode:
	of_node_put(ports);
	return err;
}

static int mchp_sparx5_remove(struct platform_device *pdev)
{
	struct sparx5 *sparx5 = platform_get_drvdata(pdev);

	debugfs_remove_recursive(sparx5->debugfs_root);
	if (sparx5->xtr_irq) {
		disable_irq(sparx5->xtr_irq);
		sparx5->xtr_irq = -ENXIO;
	}
	if (sparx5->fdma_irq) {
		disable_irq(sparx5->fdma_irq);
		sparx5->fdma_irq = -ENXIO;
	}
	sparx5_ptp_deinit(sparx5);
	sparx5_fdma_stop(sparx5);
	sparx5_cleanup_ports(sparx5);
	sparx5_vcap_destroy(sparx5);
	/* Unregister netdevs */
	sparx5_unregister_notifier_blocks(sparx5);
	destroy_workqueue(sparx5->mact_queue);

	return 0;
}

static const struct of_device_id mchp_sparx5_match[] = {
	{ .compatible = "microchip,sparx5-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mchp_sparx5_match);

static struct platform_driver mchp_sparx5_driver = {
	.probe = mchp_sparx5_probe,
	.remove = mchp_sparx5_remove,
	.driver = {
		.name = "sparx5-switch",
		.of_match_table = mchp_sparx5_match,
	},
};

module_platform_driver(mchp_sparx5_driver);

MODULE_DESCRIPTION("Microchip Sparx5 switch driver");
MODULE_AUTHOR("Steen Hegelund <steen.hegelund@microchip.com>");
MODULE_LICENSE("Dual MIT/GPL");
