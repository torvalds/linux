// SPDX-License-Identifier: GPL-2.0

/* Texas Instruments ICSSM Ethernet Driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#include <linux/etherdevice.h>
#include <linux/genalloc.h>
#include <linux/if_bridge.h>
#include <linux/if_hsr.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/remoteproc/pruss.h>
#include <linux/ptp_classify.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <net/pkt_cls.h>

#include "icssm_prueth.h"

/* called back by PHY layer if there is change in link state of hw port*/
static void icssm_emac_adjust_link(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	struct phy_device *phydev = emac->phydev;
	bool new_state = false;
	unsigned long flags;

	spin_lock_irqsave(&emac->lock, flags);

	if (phydev->link) {
		/* check the mode of operation */
		if (phydev->duplex != emac->duplex) {
			new_state = true;
			emac->duplex = phydev->duplex;
		}
		if (phydev->speed != emac->speed) {
			new_state = true;
			emac->speed = phydev->speed;
		}
		if (!emac->link) {
			new_state = true;
			emac->link = 1;
		}
	} else if (emac->link) {
		new_state = true;
		emac->link = 0;
	}

	if (new_state)
		phy_print_status(phydev);

	if (emac->link) {
	       /* reactivate the transmit queue if it is stopped */
		if (netif_running(ndev) && netif_queue_stopped(ndev))
			netif_wake_queue(ndev);
	} else {
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
	}

	spin_unlock_irqrestore(&emac->lock, flags);
}

static int icssm_emac_set_boot_pru(struct prueth_emac *emac,
				   struct net_device *ndev)
{
	const struct prueth_firmware *pru_firmwares;
	struct prueth *prueth = emac->prueth;
	const char *fw_name;
	int ret;

	pru_firmwares = &prueth->fw_data->fw_pru[emac->port_id - 1];
	fw_name = pru_firmwares->fw_name[prueth->eth_type];
	if (!fw_name) {
		netdev_err(ndev, "eth_type %d not supported\n",
			   prueth->eth_type);
		return -ENODEV;
	}

	ret = rproc_set_firmware(emac->pru, fw_name);
	if (ret) {
		netdev_err(ndev, "failed to set %s firmware: %d\n",
			   fw_name, ret);
		return ret;
	}

	ret = rproc_boot(emac->pru);
	if (ret) {
		netdev_err(ndev, "failed to boot %s firmware: %d\n",
			   fw_name, ret);
		return ret;
	}

	return ret;
}

/**
 * icssm_emac_ndo_open - EMAC device open
 * @ndev: network adapter device
 *
 * Called when system wants to start the interface.
 *
 * Return: 0 for a successful open, or appropriate error code
 */
static int icssm_emac_ndo_open(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	int ret;

	ret = icssm_emac_set_boot_pru(emac, ndev);
	if (ret)
		return ret;

	/* start PHY */
	phy_start(emac->phydev);

	return 0;
}

/**
 * icssm_emac_ndo_stop - EMAC device stop
 * @ndev: network adapter device
 *
 * Called when system wants to stop or down the interface.
 *
 * Return: Always 0 (Success)
 */
static int icssm_emac_ndo_stop(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);

	/* stop PHY */
	phy_stop(emac->phydev);

	rproc_shutdown(emac->pru);

	return 0;
}

static const struct net_device_ops emac_netdev_ops = {
	.ndo_open = icssm_emac_ndo_open,
	.ndo_stop = icssm_emac_ndo_stop,
};

/* get emac_port corresponding to eth_node name */
static int icssm_prueth_node_port(struct device_node *eth_node)
{
	u32 port_id;
	int ret;

	ret = of_property_read_u32(eth_node, "reg", &port_id);
	if (ret)
		return ret;

	if (port_id == 0)
		return PRUETH_PORT_MII0;
	else if (port_id == 1)
		return PRUETH_PORT_MII1;
	else
		return PRUETH_PORT_INVALID;
}

/* get MAC instance corresponding to eth_node name */
static int icssm_prueth_node_mac(struct device_node *eth_node)
{
	u32 port_id;
	int ret;

	ret = of_property_read_u32(eth_node, "reg", &port_id);
	if (ret)
		return ret;

	if (port_id == 0)
		return PRUETH_MAC0;
	else if (port_id == 1)
		return PRUETH_MAC1;
	else
		return PRUETH_MAC_INVALID;
}

static int icssm_prueth_netdev_init(struct prueth *prueth,
				    struct device_node *eth_node)
{
	struct prueth_emac *emac;
	struct net_device *ndev;
	enum prueth_port port;
	enum prueth_mac mac;
	int ret;

	port = icssm_prueth_node_port(eth_node);
	if (port == PRUETH_PORT_INVALID)
		return -EINVAL;

	mac = icssm_prueth_node_mac(eth_node);
	if (mac == PRUETH_MAC_INVALID)
		return -EINVAL;

	ndev = devm_alloc_etherdev(prueth->dev, sizeof(*emac));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, prueth->dev);
	emac = netdev_priv(ndev);
	prueth->emac[mac] = emac;
	emac->prueth = prueth;
	emac->ndev = ndev;
	emac->port_id = port;

	/* by default eth_type is EMAC */
	switch (port) {
	case PRUETH_PORT_MII0:
		emac->pru = prueth->pru0;
		break;
	case PRUETH_PORT_MII1:
		emac->pru = prueth->pru1;
		break;
	default:
		return -EINVAL;
	}
	/* get mac address from DT and set private and netdev addr */
	ret = of_get_ethdev_address(eth_node, ndev);
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		eth_hw_addr_random(ndev);
		dev_warn(prueth->dev, "port %d: using random MAC addr: %pM\n",
			 port, ndev->dev_addr);
	}
	ether_addr_copy(emac->mac_addr, ndev->dev_addr);

	/* connect PHY */
	emac->phydev = of_phy_get_and_connect(ndev, eth_node,
					      icssm_emac_adjust_link);
	if (!emac->phydev) {
		dev_dbg(prueth->dev, "PHY connection failed\n");
		ret = -ENODEV;
		goto free;
	}

	/* remove unsupported modes */
	phy_remove_link_mode(emac->phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);

	phy_remove_link_mode(emac->phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	phy_remove_link_mode(emac->phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);

	phy_remove_link_mode(emac->phydev, ETHTOOL_LINK_MODE_Pause_BIT);
	phy_remove_link_mode(emac->phydev, ETHTOOL_LINK_MODE_Asym_Pause_BIT);

	ndev->dev.of_node = eth_node;
	ndev->netdev_ops = &emac_netdev_ops;

	return 0;
free:
	emac->ndev = NULL;
	prueth->emac[mac] = NULL;

	return ret;
}

static void icssm_prueth_netdev_exit(struct prueth *prueth,
				     struct device_node *eth_node)
{
	struct prueth_emac *emac;
	enum prueth_mac mac;

	mac = icssm_prueth_node_mac(eth_node);
	if (mac == PRUETH_MAC_INVALID)
		return;

	emac = prueth->emac[mac];
	if (!emac)
		return;

	phy_disconnect(emac->phydev);

	prueth->emac[mac] = NULL;
}

static int icssm_prueth_probe(struct platform_device *pdev)
{
	struct device_node *eth0_node = NULL, *eth1_node = NULL;
	struct device_node *eth_node, *eth_ports_node;
	enum pruss_pru_id pruss_id0, pruss_id1;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct prueth *prueth;
	int i, ret;

	np = dev->of_node;
	if (!np)
		return -ENODEV; /* we don't support non DT */

	prueth = devm_kzalloc(dev, sizeof(*prueth), GFP_KERNEL);
	if (!prueth)
		return -ENOMEM;

	platform_set_drvdata(pdev, prueth);
	prueth->dev = dev;
	prueth->fw_data = device_get_match_data(dev);

	eth_ports_node = of_get_child_by_name(np, "ethernet-ports");
	if (!eth_ports_node)
		return -ENOENT;

	for_each_child_of_node(eth_ports_node, eth_node) {
		u32 reg;

		if (strcmp(eth_node->name, "ethernet-port"))
			continue;
		ret = of_property_read_u32(eth_node, "reg", &reg);
		if (ret < 0) {
			dev_err(dev, "%pOF error reading port_id %d\n",
				eth_node, ret);
			of_node_put(eth_node);
			return ret;
		}

		of_node_get(eth_node);

		if (reg == 0 && !eth0_node) {
			eth0_node = eth_node;
			if (!of_device_is_available(eth0_node)) {
				of_node_put(eth0_node);
				eth0_node = NULL;
			}
		} else if (reg == 1 && !eth1_node) {
			eth1_node = eth_node;
			if (!of_device_is_available(eth1_node)) {
				of_node_put(eth1_node);
				eth1_node = NULL;
			}
		} else {
			if (reg == 0 || reg == 1)
				dev_err(dev, "duplicate port reg value: %d\n",
					reg);
			else
				dev_err(dev, "invalid port reg value: %d\n",
					reg);

			of_node_put(eth_node);
		}
	}

	of_node_put(eth_ports_node);

	/* At least one node must be present and available else we fail */
	if (!eth0_node && !eth1_node) {
		dev_err(dev, "neither port0 nor port1 node available\n");
		return -ENODEV;
	}

	prueth->eth_node[PRUETH_MAC0] = eth0_node;
	prueth->eth_node[PRUETH_MAC1] = eth1_node;

	if (eth0_node) {
		prueth->pru0 = pru_rproc_get(np, 0, &pruss_id0);
		if (IS_ERR(prueth->pru0)) {
			ret = PTR_ERR(prueth->pru0);
			dev_err_probe(dev, ret, "unable to get PRU0");
			goto put_pru;
		}
	}

	if (eth1_node) {
		prueth->pru1 = pru_rproc_get(np, 1, &pruss_id1);
		if (IS_ERR(prueth->pru1)) {
			ret = PTR_ERR(prueth->pru1);
			dev_err_probe(dev, ret, "unable to get PRU1");
			goto put_pru;
		}
	}

	/* setup netdev interfaces */
	if (eth0_node) {
		ret = icssm_prueth_netdev_init(prueth, eth0_node);
		if (ret) {
			if (ret != -EPROBE_DEFER) {
				dev_err(dev, "netdev init %s failed: %d\n",
					eth0_node->name, ret);
			}
			goto put_pru;
		}
	}

	if (eth1_node) {
		ret = icssm_prueth_netdev_init(prueth, eth1_node);
		if (ret) {
			if (ret != -EPROBE_DEFER) {
				dev_err(dev, "netdev init %s failed: %d\n",
					eth1_node->name, ret);
			}
			goto netdev_exit;
		}
	}

	/* register the network devices */
	if (eth0_node) {
		ret = register_netdev(prueth->emac[PRUETH_MAC0]->ndev);
		if (ret) {
			dev_err(dev, "can't register netdev for port MII0");
			goto netdev_exit;
		}

		prueth->registered_netdevs[PRUETH_MAC0] =
			prueth->emac[PRUETH_MAC0]->ndev;
	}

	if (eth1_node) {
		ret = register_netdev(prueth->emac[PRUETH_MAC1]->ndev);
		if (ret) {
			dev_err(dev, "can't register netdev for port MII1");
			goto netdev_unregister;
		}

		prueth->registered_netdevs[PRUETH_MAC1] =
			prueth->emac[PRUETH_MAC1]->ndev;
	}

	if (eth1_node)
		of_node_put(eth1_node);
	if (eth0_node)
		of_node_put(eth0_node);
	return 0;

netdev_unregister:
	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		if (!prueth->registered_netdevs[i])
			continue;
		unregister_netdev(prueth->registered_netdevs[i]);
	}

netdev_exit:
	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		eth_node = prueth->eth_node[i];
		if (!eth_node)
			continue;

		icssm_prueth_netdev_exit(prueth, eth_node);
	}

put_pru:
	if (eth1_node) {
		if (prueth->pru1)
			pru_rproc_put(prueth->pru1);
		of_node_put(eth1_node);
	}

	if (eth0_node) {
		if (prueth->pru0)
			pru_rproc_put(prueth->pru0);
		of_node_put(eth0_node);
	}

	return ret;
}

static void icssm_prueth_remove(struct platform_device *pdev)
{
	struct prueth *prueth = platform_get_drvdata(pdev);
	struct device_node *eth_node;
	int i;

	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		if (!prueth->registered_netdevs[i])
			continue;
		unregister_netdev(prueth->registered_netdevs[i]);
	}

	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		eth_node = prueth->eth_node[i];
		if (!eth_node)
			continue;

		icssm_prueth_netdev_exit(prueth, eth_node);
		of_node_put(eth_node);
	}

	pruss_put(prueth->pruss);

	if (prueth->eth_node[PRUETH_MAC0])
		pru_rproc_put(prueth->pru0);
	if (prueth->eth_node[PRUETH_MAC1])
		pru_rproc_put(prueth->pru1);
}

#ifdef CONFIG_PM_SLEEP
static int icssm_prueth_suspend(struct device *dev)
{
	struct prueth *prueth = dev_get_drvdata(dev);
	struct net_device *ndev;
	int i, ret;

	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		ndev = prueth->registered_netdevs[i];

		if (!ndev)
			continue;

		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			ret = icssm_emac_ndo_stop(ndev);
			if (ret < 0) {
				netdev_err(ndev, "failed to stop: %d", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int icssm_prueth_resume(struct device *dev)
{
	struct prueth *prueth = dev_get_drvdata(dev);
	struct net_device *ndev;
	int i, ret;

	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		ndev = prueth->registered_netdevs[i];

		if (!ndev)
			continue;

		if (netif_running(ndev)) {
			ret = icssm_emac_ndo_open(ndev);
			if (ret < 0) {
				netdev_err(ndev, "failed to start: %d", ret);
				return ret;
			}
			netif_device_attach(ndev);
		}
	}

	return 0;
}

#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops prueth_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(icssm_prueth_suspend, icssm_prueth_resume)
};

/* AM335x SoC-specific firmware data */
static struct prueth_private_data am335x_prueth_pdata = {
	.fw_pru[PRUSS_PRU0] = {
		.fw_name[PRUSS_ETHTYPE_EMAC] =
			"ti-pruss/am335x-pru0-prueth-fw.elf",
	},
	.fw_pru[PRUSS_PRU1] = {
		.fw_name[PRUSS_ETHTYPE_EMAC] =
			"ti-pruss/am335x-pru1-prueth-fw.elf",
	},
};

/* AM437x SoC-specific firmware data */
static struct prueth_private_data am437x_prueth_pdata = {
	.fw_pru[PRUSS_PRU0] = {
		.fw_name[PRUSS_ETHTYPE_EMAC] =
			"ti-pruss/am437x-pru0-prueth-fw.elf",
	},
	.fw_pru[PRUSS_PRU1] = {
		.fw_name[PRUSS_ETHTYPE_EMAC] =
			"ti-pruss/am437x-pru1-prueth-fw.elf",
	},
};

/* AM57xx SoC-specific firmware data */
static struct prueth_private_data am57xx_prueth_pdata = {
	.fw_pru[PRUSS_PRU0] = {
		.fw_name[PRUSS_ETHTYPE_EMAC] =
			"ti-pruss/am57xx-pru0-prueth-fw.elf",
	},
	.fw_pru[PRUSS_PRU1] = {
		.fw_name[PRUSS_ETHTYPE_EMAC] =
			"ti-pruss/am57xx-pru1-prueth-fw.elf",
	},
};

static const struct of_device_id prueth_dt_match[] = {
	{ .compatible = "ti,am57-prueth", .data = &am57xx_prueth_pdata, },
	{ .compatible = "ti,am4376-prueth", .data = &am437x_prueth_pdata, },
	{ .compatible = "ti,am3359-prueth", .data = &am335x_prueth_pdata, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, prueth_dt_match);

static struct platform_driver prueth_driver = {
	.probe = icssm_prueth_probe,
	.remove = icssm_prueth_remove,
	.driver = {
		.name = "prueth",
		.of_match_table = prueth_dt_match,
		.pm = &prueth_dev_pm_ops,
	},
};
module_platform_driver(prueth_driver);

MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("PRUSS ICSSM Ethernet Driver");
MODULE_LICENSE("GPL");
