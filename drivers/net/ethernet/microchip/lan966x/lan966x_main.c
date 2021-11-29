// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/if_bridge.h>
#include <linux/iopoll.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/reset.h>

#include "lan966x_main.h"

#define READL_SLEEP_US			10
#define READL_TIMEOUT_US		100000000

#define IO_RANGES 2

static const struct of_device_id lan966x_match[] = {
	{ .compatible = "microchip,lan966x-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, lan966x_match);

struct lan966x_main_io_resource {
	enum lan966x_target id;
	phys_addr_t offset;
	int range;
};

static const struct lan966x_main_io_resource lan966x_main_iomap[] =  {
	{ TARGET_CPU,                   0xc0000, 0 }, /* 0xe00c0000 */
	{ TARGET_ORG,                         0, 1 }, /* 0xe2000000 */
	{ TARGET_GCB,                    0x4000, 1 }, /* 0xe2004000 */
	{ TARGET_QS,                     0x8000, 1 }, /* 0xe2008000 */
	{ TARGET_CHIP_TOP,              0x10000, 1 }, /* 0xe2010000 */
	{ TARGET_REW,                   0x14000, 1 }, /* 0xe2014000 */
	{ TARGET_SYS,                   0x28000, 1 }, /* 0xe2028000 */
	{ TARGET_DEV,                   0x34000, 1 }, /* 0xe2034000 */
	{ TARGET_DEV +  1,              0x38000, 1 }, /* 0xe2038000 */
	{ TARGET_DEV +  2,              0x3c000, 1 }, /* 0xe203c000 */
	{ TARGET_DEV +  3,              0x40000, 1 }, /* 0xe2040000 */
	{ TARGET_DEV +  4,              0x44000, 1 }, /* 0xe2044000 */
	{ TARGET_DEV +  5,              0x48000, 1 }, /* 0xe2048000 */
	{ TARGET_DEV +  6,              0x4c000, 1 }, /* 0xe204c000 */
	{ TARGET_DEV +  7,              0x50000, 1 }, /* 0xe2050000 */
	{ TARGET_QSYS,                 0x100000, 1 }, /* 0xe2100000 */
	{ TARGET_AFI,                  0x120000, 1 }, /* 0xe2120000 */
	{ TARGET_ANA,                  0x140000, 1 }, /* 0xe2140000 */
};

static int lan966x_create_targets(struct platform_device *pdev,
				  struct lan966x *lan966x)
{
	struct resource *iores[IO_RANGES];
	void __iomem *begin[IO_RANGES];
	int idx;

	/* Initially map the entire range and after that update each target to
	 * point inside the region at the correct offset. It is possible that
	 * other devices access the same region so don't add any checks about
	 * this.
	 */
	for (idx = 0; idx < IO_RANGES; idx++) {
		iores[idx] = platform_get_resource(pdev, IORESOURCE_MEM,
						   idx);
		if (!iores[idx]) {
			dev_err(&pdev->dev, "Invalid resource\n");
			return -EINVAL;
		}

		begin[idx] = devm_ioremap(&pdev->dev,
					  iores[idx]->start,
					  resource_size(iores[idx]));
		if (IS_ERR(begin[idx])) {
			dev_err(&pdev->dev, "Unable to get registers: %s\n",
				iores[idx]->name);
			return PTR_ERR(begin[idx]);
		}
	}

	for (idx = 0; idx < ARRAY_SIZE(lan966x_main_iomap); idx++) {
		const struct lan966x_main_io_resource *iomap =
			&lan966x_main_iomap[idx];

		lan966x->regs[iomap->id] = begin[iomap->range] + iomap->offset;
	}

	return 0;
}

static int lan966x_probe_port(struct lan966x *lan966x, u32 p,
			      phy_interface_t phy_mode)
{
	struct lan966x_port *port;

	if (p >= lan966x->num_phys_ports)
		return -EINVAL;

	port = devm_kzalloc(lan966x->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->lan966x = lan966x;
	port->chip_port = p;
	port->pvid = PORT_PVID;
	lan966x->ports[p] = port;

	return 0;
}

static void lan966x_init(struct lan966x *lan966x)
{
	u32 p, i;

	/* Flush queues */
	lan_wr(lan_rd(lan966x, QS_XTR_FLUSH) |
	       GENMASK(1, 0),
	       lan966x, QS_XTR_FLUSH);

	/* Allow to drain */
	mdelay(1);

	/* All Queues normal */
	lan_wr(lan_rd(lan966x, QS_XTR_FLUSH) &
	       ~(GENMASK(1, 0)),
	       lan966x, QS_XTR_FLUSH);

	/* Set MAC age time to default value, the entry is aged after
	 * 2 * AGE_PERIOD
	 */
	lan_wr(ANA_AUTOAGE_AGE_PERIOD_SET(BR_DEFAULT_AGEING_TIME / 2 / HZ),
	       lan966x, ANA_AUTOAGE);

	/* Disable learning for frames discarded by VLAN ingress filtering */
	lan_rmw(ANA_ADVLEARN_VLAN_CHK_SET(1),
		ANA_ADVLEARN_VLAN_CHK,
		lan966x, ANA_ADVLEARN);

	/* Setup frame ageing - "2 sec" - The unit is 6.5 us on lan966x */
	lan_wr(SYS_FRM_AGING_AGE_TX_ENA_SET(1) |
	       (20000000 / 65),
	       lan966x,  SYS_FRM_AGING);

	/* Map the 8 CPU extraction queues to CPU port */
	lan_wr(0, lan966x, QSYS_CPU_GROUP_MAP);

	/* Do byte-swap and expect status after last data word
	 * Extraction: Mode: manual extraction) | Byte_swap
	 */
	lan_wr(QS_XTR_GRP_CFG_MODE_SET(1) |
	       QS_XTR_GRP_CFG_BYTE_SWAP_SET(1),
	       lan966x, QS_XTR_GRP_CFG(0));

	/* Injection: Mode: manual injection | Byte_swap */
	lan_wr(QS_INJ_GRP_CFG_MODE_SET(1) |
	       QS_INJ_GRP_CFG_BYTE_SWAP_SET(1),
	       lan966x, QS_INJ_GRP_CFG(0));

	lan_rmw(QS_INJ_CTRL_GAP_SIZE_SET(0),
		QS_INJ_CTRL_GAP_SIZE,
		lan966x, QS_INJ_CTRL(0));

	/* Enable IFH insertion/parsing on CPU ports */
	lan_wr(SYS_PORT_MODE_INCL_INJ_HDR_SET(1) |
	       SYS_PORT_MODE_INCL_XTR_HDR_SET(1),
	       lan966x, SYS_PORT_MODE(CPU_PORT));

	/* Setup flooding PGIDs */
	lan_wr(ANA_FLOODING_IPMC_FLD_MC4_DATA_SET(PGID_MCIPV4) |
	       ANA_FLOODING_IPMC_FLD_MC4_CTRL_SET(PGID_MC) |
	       ANA_FLOODING_IPMC_FLD_MC6_DATA_SET(PGID_MC) |
	       ANA_FLOODING_IPMC_FLD_MC6_CTRL_SET(PGID_MC),
	       lan966x, ANA_FLOODING_IPMC);

	/* There are 8 priorities */
	for (i = 0; i < 8; ++i)
		lan_rmw(ANA_FLOODING_FLD_MULTICAST_SET(PGID_MC) |
			ANA_FLOODING_FLD_BROADCAST_SET(PGID_BC),
			ANA_FLOODING_FLD_MULTICAST |
			ANA_FLOODING_FLD_BROADCAST,
			lan966x, ANA_FLOODING(i));

	for (i = 0; i < PGID_ENTRIES; ++i)
		/* Set all the entries to obey VLAN_VLAN */
		lan_rmw(ANA_PGID_CFG_OBEY_VLAN_SET(1),
			ANA_PGID_CFG_OBEY_VLAN,
			lan966x, ANA_PGID_CFG(i));

	for (p = 0; p < lan966x->num_phys_ports; p++) {
		/* Disable bridging by default */
		lan_rmw(ANA_PGID_PGID_SET(0x0),
			ANA_PGID_PGID,
			lan966x, ANA_PGID(p + PGID_SRC));

		/* Do not forward BPDU frames to the front ports and copy them
		 * to CPU
		 */
		lan_wr(0xffff, lan966x, ANA_CPU_FWD_BPDU_CFG(p));
	}

	/* Set source buffer size for each priority and each port to 1500 bytes */
	for (i = 0; i <= QSYS_Q_RSRV; ++i) {
		lan_wr(1500 / 64, lan966x, QSYS_RES_CFG(i));
		lan_wr(1500 / 64, lan966x, QSYS_RES_CFG(512 + i));
	}

	/* Enable switching to/from cpu port */
	lan_wr(QSYS_SW_PORT_MODE_PORT_ENA_SET(1) |
	       QSYS_SW_PORT_MODE_SCH_NEXT_CFG_SET(1) |
	       QSYS_SW_PORT_MODE_INGRESS_DROP_MODE_SET(1),
	       lan966x,  QSYS_SW_PORT_MODE(CPU_PORT));

	/* Configure and enable the CPU port */
	lan_rmw(ANA_PGID_PGID_SET(0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(CPU_PORT));
	lan_rmw(ANA_PGID_PGID_SET(BIT(CPU_PORT)),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_CPU));

	/* Multicast to all other ports */
	lan_rmw(GENMASK(lan966x->num_phys_ports - 1, 0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_MC));

	/* This will be controlled by mrouter ports */
	lan_rmw(GENMASK(lan966x->num_phys_ports - 1, 0),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_MCIPV4));

	/* Broadcast to the CPU port and to other ports */
	lan_rmw(ANA_PGID_PGID_SET(BIT(CPU_PORT) | GENMASK(lan966x->num_phys_ports - 1, 0)),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(PGID_BC));

	lan_wr(REW_PORT_CFG_NO_REWRITE_SET(1),
	       lan966x, REW_PORT_CFG(CPU_PORT));

	lan_rmw(ANA_ANAINTR_INTR_ENA_SET(1),
		ANA_ANAINTR_INTR_ENA,
		lan966x, ANA_ANAINTR);
}

static int lan966x_ram_init(struct lan966x *lan966x)
{
	return lan_rd(lan966x, SYS_RAM_INIT);
}

static int lan966x_reset_switch(struct lan966x *lan966x)
{
	struct reset_control *switch_reset, *phy_reset;
	int val = 0;
	int ret;

	switch_reset = devm_reset_control_get_shared(lan966x->dev, "switch");
	if (IS_ERR(switch_reset))
		return dev_err_probe(lan966x->dev, PTR_ERR(switch_reset),
				     "Could not obtain switch reset");

	phy_reset = devm_reset_control_get_shared(lan966x->dev, "phy");
	if (IS_ERR(phy_reset))
		return dev_err_probe(lan966x->dev, PTR_ERR(phy_reset),
				     "Could not obtain phy reset\n");

	reset_control_reset(switch_reset);
	reset_control_reset(phy_reset);

	lan_wr(SYS_RESET_CFG_CORE_ENA_SET(0), lan966x, SYS_RESET_CFG);
	lan_wr(SYS_RAM_INIT_RAM_INIT_SET(1), lan966x, SYS_RAM_INIT);
	ret = readx_poll_timeout(lan966x_ram_init, lan966x,
				 val, (val & BIT(1)) == 0, READL_SLEEP_US,
				 READL_TIMEOUT_US);
	if (ret)
		return ret;

	lan_wr(SYS_RESET_CFG_CORE_ENA_SET(1), lan966x, SYS_RESET_CFG);

	return 0;
}

static int lan966x_probe(struct platform_device *pdev)
{
	struct fwnode_handle *ports, *portnp;
	struct lan966x *lan966x;
	int err, i;

	lan966x = devm_kzalloc(&pdev->dev, sizeof(*lan966x), GFP_KERNEL);
	if (!lan966x)
		return -ENOMEM;

	platform_set_drvdata(pdev, lan966x);
	lan966x->dev = &pdev->dev;

	ports = device_get_named_child_node(&pdev->dev, "ethernet-ports");
	if (!ports)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "no ethernet-ports child found\n");

	err = lan966x_create_targets(pdev, lan966x);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "Failed to create targets");

	err = lan966x_reset_switch(lan966x);
	if (err)
		return dev_err_probe(&pdev->dev, err, "Reset failed");

	i = 0;
	fwnode_for_each_available_child_node(ports, portnp)
		++i;

	lan966x->num_phys_ports = i;
	lan966x->ports = devm_kcalloc(&pdev->dev, lan966x->num_phys_ports,
				      sizeof(struct lan966x_port *),
				      GFP_KERNEL);
	if (!lan966x->ports)
		return -ENOMEM;

	/* There QS system has 32KB of memory */
	lan966x->shared_queue_sz = LAN966X_BUFFER_MEMORY;

	/* init switch */
	lan966x_init(lan966x);

	/* go over the child nodes */
	fwnode_for_each_available_child_node(ports, portnp) {
		phy_interface_t phy_mode;
		u32 p;

		if (fwnode_property_read_u32(portnp, "reg", &p))
			continue;

		phy_mode = fwnode_get_phy_mode(portnp);
		err = lan966x_probe_port(lan966x, p, phy_mode);
		if (err)
			goto cleanup_ports;
	}

	return 0;

cleanup_ports:
	fwnode_handle_put(portnp);

	return err;
}

static struct platform_driver lan966x_driver = {
	.probe = lan966x_probe,
	.driver = {
		.name = "lan966x-switch",
		.of_match_table = lan966x_match,
	},
};
module_platform_driver(lan966x_driver);

MODULE_DESCRIPTION("Microchip LAN966X switch driver");
MODULE_AUTHOR("Horatiu Vultur <horatiu.vultur@microchip.com>");
MODULE_LICENSE("Dual MIT/GPL");
