// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

/* Gigabit Ethernet driver for Mellanox BlueField SoC
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>

#include "mlxbf_gige.h"
#include "mlxbf_gige_regs.h"

/* Allocate SKB whose payload pointer aligns with the Bluefield
 * hardware DMA limitation, i.e. DMA operation can't cross
 * a 4KB boundary.  A maximum packet size of 2KB is assumed in the
 * alignment formula.  The alignment logic overallocates an SKB,
 * and then adjusts the headroom so that the SKB data pointer is
 * naturally aligned to a 2KB boundary.
 */
struct sk_buff *mlxbf_gige_alloc_skb(struct mlxbf_gige *priv,
				     unsigned int map_len,
				     dma_addr_t *buf_dma,
				     enum dma_data_direction dir)
{
	struct sk_buff *skb;
	u64 addr, offset;

	/* Overallocate the SKB so that any headroom adjustment (to
	 * provide 2KB natural alignment) does not exceed payload area
	 */
	skb = netdev_alloc_skb(priv->netdev, MLXBF_GIGE_DEFAULT_BUF_SZ * 2);
	if (!skb)
		return NULL;

	/* Adjust the headroom so that skb->data is naturally aligned to
	 * a 2KB boundary, which is the maximum packet size supported.
	 */
	addr = (long)skb->data;
	offset = (addr + MLXBF_GIGE_DEFAULT_BUF_SZ - 1) &
		~(MLXBF_GIGE_DEFAULT_BUF_SZ - 1);
	offset -= addr;
	if (offset)
		skb_reserve(skb, offset);

	/* Return streaming DMA mapping to caller */
	*buf_dma = dma_map_single(priv->dev, skb->data, map_len, dir);
	if (dma_mapping_error(priv->dev, *buf_dma)) {
		dev_kfree_skb(skb);
		*buf_dma = (dma_addr_t)0;
		return NULL;
	}

	return skb;
}

static void mlxbf_gige_initial_mac(struct mlxbf_gige *priv)
{
	u8 mac[ETH_ALEN];
	u64 local_mac;

	eth_zero_addr(mac);
	mlxbf_gige_get_mac_rx_filter(priv, MLXBF_GIGE_LOCAL_MAC_FILTER_IDX,
				     &local_mac);
	u64_to_ether_addr(local_mac, mac);

	if (is_valid_ether_addr(mac)) {
		eth_hw_addr_set(priv->netdev, mac);
	} else {
		/* Provide a random MAC if for some reason the device has
		 * not been configured with a valid MAC address already.
		 */
		eth_hw_addr_random(priv->netdev);
	}

	local_mac = ether_addr_to_u64(priv->netdev->dev_addr);
	mlxbf_gige_set_mac_rx_filter(priv, MLXBF_GIGE_LOCAL_MAC_FILTER_IDX,
				     local_mac);
}

static void mlxbf_gige_cache_stats(struct mlxbf_gige *priv)
{
	struct mlxbf_gige_stats *p;

	/* Cache stats that will be cleared by clean port operation */
	p = &priv->stats;
	p->rx_din_dropped_pkts += readq(priv->base +
					MLXBF_GIGE_RX_DIN_DROP_COUNTER);
	p->rx_filter_passed_pkts += readq(priv->base +
					  MLXBF_GIGE_RX_PASS_COUNTER_ALL);
	p->rx_filter_discard_pkts += readq(priv->base +
					   MLXBF_GIGE_RX_DISC_COUNTER_ALL);
}

static int mlxbf_gige_clean_port(struct mlxbf_gige *priv)
{
	u64 control;
	u64 temp;
	int err;

	/* Set the CLEAN_PORT_EN bit to trigger SW reset */
	control = readq(priv->base + MLXBF_GIGE_CONTROL);
	control |= MLXBF_GIGE_CONTROL_CLEAN_PORT_EN;
	writeq(control, priv->base + MLXBF_GIGE_CONTROL);

	/* Ensure completion of "clean port" write before polling status */
	mb();

	err = readq_poll_timeout_atomic(priv->base + MLXBF_GIGE_STATUS, temp,
					(temp & MLXBF_GIGE_STATUS_READY),
					100, 100000);

	/* Clear the CLEAN_PORT_EN bit at end of this loop */
	control = readq(priv->base + MLXBF_GIGE_CONTROL);
	control &= ~MLXBF_GIGE_CONTROL_CLEAN_PORT_EN;
	writeq(control, priv->base + MLXBF_GIGE_CONTROL);

	return err;
}

static int mlxbf_gige_open(struct net_device *netdev)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	u64 control;
	u64 int_en;
	int err;

	/* Perform general init of GigE block */
	control = readq(priv->base + MLXBF_GIGE_CONTROL);
	control |= MLXBF_GIGE_CONTROL_PORT_EN;
	writeq(control, priv->base + MLXBF_GIGE_CONTROL);

	mlxbf_gige_cache_stats(priv);
	err = mlxbf_gige_clean_port(priv);
	if (err)
		return err;

	/* Clear driver's valid_polarity to match hardware,
	 * since the above call to clean_port() resets the
	 * receive polarity used by hardware.
	 */
	priv->valid_polarity = 0;

	phy_start(phydev);

	err = mlxbf_gige_tx_init(priv);
	if (err)
		goto phy_deinit;
	err = mlxbf_gige_rx_init(priv);
	if (err)
		goto tx_deinit;

	netif_napi_add(netdev, &priv->napi, mlxbf_gige_poll);
	napi_enable(&priv->napi);
	netif_start_queue(netdev);

	err = mlxbf_gige_request_irqs(priv);
	if (err)
		goto napi_deinit;

	/* Set bits in INT_EN that we care about */
	int_en = MLXBF_GIGE_INT_EN_HW_ACCESS_ERROR |
		 MLXBF_GIGE_INT_EN_TX_CHECKSUM_INPUTS |
		 MLXBF_GIGE_INT_EN_TX_SMALL_FRAME_SIZE |
		 MLXBF_GIGE_INT_EN_TX_PI_CI_EXCEED_WQ_SIZE |
		 MLXBF_GIGE_INT_EN_SW_CONFIG_ERROR |
		 MLXBF_GIGE_INT_EN_SW_ACCESS_ERROR |
		 MLXBF_GIGE_INT_EN_RX_RECEIVE_PACKET;

	/* Ensure completion of all initialization before enabling interrupts */
	mb();

	writeq(int_en, priv->base + MLXBF_GIGE_INT_EN);

	return 0;

napi_deinit:
	netif_stop_queue(netdev);
	napi_disable(&priv->napi);
	netif_napi_del(&priv->napi);
	mlxbf_gige_rx_deinit(priv);

tx_deinit:
	mlxbf_gige_tx_deinit(priv);

phy_deinit:
	phy_stop(phydev);
	return err;
}

static int mlxbf_gige_stop(struct net_device *netdev)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);

	writeq(0, priv->base + MLXBF_GIGE_INT_EN);
	netif_stop_queue(netdev);
	napi_disable(&priv->napi);
	netif_napi_del(&priv->napi);
	mlxbf_gige_free_irqs(priv);

	phy_stop(netdev->phydev);

	mlxbf_gige_rx_deinit(priv);
	mlxbf_gige_tx_deinit(priv);
	mlxbf_gige_cache_stats(priv);
	mlxbf_gige_clean_port(priv);

	return 0;
}

static int mlxbf_gige_eth_ioctl(struct net_device *netdev,
				struct ifreq *ifr, int cmd)
{
	if (!(netif_running(netdev)))
		return -EINVAL;

	return phy_mii_ioctl(netdev->phydev, ifr, cmd);
}

static void mlxbf_gige_set_rx_mode(struct net_device *netdev)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);
	bool new_promisc_enabled;

	new_promisc_enabled = netdev->flags & IFF_PROMISC;

	/* Only write to the hardware registers if the new setting
	 * of promiscuous mode is different from the current one.
	 */
	if (new_promisc_enabled != priv->promisc_enabled) {
		priv->promisc_enabled = new_promisc_enabled;

		if (new_promisc_enabled)
			mlxbf_gige_enable_promisc(priv);
		else
			mlxbf_gige_disable_promisc(priv);
	}
}

static void mlxbf_gige_get_stats64(struct net_device *netdev,
				   struct rtnl_link_stats64 *stats)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);

	netdev_stats_to_stats64(stats, &netdev->stats);

	stats->rx_length_errors = priv->stats.rx_truncate_errors;
	stats->rx_fifo_errors = priv->stats.rx_din_dropped_pkts +
				readq(priv->base + MLXBF_GIGE_RX_DIN_DROP_COUNTER);
	stats->rx_crc_errors = priv->stats.rx_mac_errors;
	stats->rx_errors = stats->rx_length_errors +
			   stats->rx_fifo_errors +
			   stats->rx_crc_errors;

	stats->tx_fifo_errors = priv->stats.tx_fifo_full;
	stats->tx_errors = stats->tx_fifo_errors;
}

static const struct net_device_ops mlxbf_gige_netdev_ops = {
	.ndo_open		= mlxbf_gige_open,
	.ndo_stop		= mlxbf_gige_stop,
	.ndo_start_xmit		= mlxbf_gige_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= mlxbf_gige_eth_ioctl,
	.ndo_set_rx_mode        = mlxbf_gige_set_rx_mode,
	.ndo_get_stats64        = mlxbf_gige_get_stats64,
};

static void mlxbf_gige_bf2_adjust_link(struct net_device *netdev)
{
	struct phy_device *phydev = netdev->phydev;

	phy_print_status(phydev);
}

static void mlxbf_gige_bf3_adjust_link(struct net_device *netdev)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	u8 sgmii_mode;
	u16 ipg_size;
	u32 val;

	if (phydev->link && phydev->speed != priv->prev_speed) {
		switch (phydev->speed) {
		case 1000:
			ipg_size = MLXBF_GIGE_1G_IPG_SIZE;
			sgmii_mode = MLXBF_GIGE_1G_SGMII_MODE;
			break;
		case 100:
			ipg_size = MLXBF_GIGE_100M_IPG_SIZE;
			sgmii_mode = MLXBF_GIGE_100M_SGMII_MODE;
			break;
		case 10:
			ipg_size = MLXBF_GIGE_10M_IPG_SIZE;
			sgmii_mode = MLXBF_GIGE_10M_SGMII_MODE;
			break;
		default:
			return;
		}

		val = readl(priv->plu_base + MLXBF_GIGE_PLU_TX_REG0);
		val &= ~(MLXBF_GIGE_PLU_TX_IPG_SIZE_MASK | MLXBF_GIGE_PLU_TX_SGMII_MODE_MASK);
		val |= FIELD_PREP(MLXBF_GIGE_PLU_TX_IPG_SIZE_MASK, ipg_size);
		val |= FIELD_PREP(MLXBF_GIGE_PLU_TX_SGMII_MODE_MASK, sgmii_mode);
		writel(val, priv->plu_base + MLXBF_GIGE_PLU_TX_REG0);

		val = readl(priv->plu_base + MLXBF_GIGE_PLU_RX_REG0);
		val &= ~MLXBF_GIGE_PLU_RX_SGMII_MODE_MASK;
		val |= FIELD_PREP(MLXBF_GIGE_PLU_RX_SGMII_MODE_MASK, sgmii_mode);
		writel(val, priv->plu_base + MLXBF_GIGE_PLU_RX_REG0);

		priv->prev_speed = phydev->speed;
	}

	phy_print_status(phydev);
}

static void mlxbf_gige_bf2_set_phy_link_mode(struct phy_device *phydev)
{
	/* MAC only supports 1000T full duplex mode */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);

	/* Only symmetric pause with flow control enabled is supported so no
	 * need to negotiate pause.
	 */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->advertising);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->advertising);
}

static void mlxbf_gige_bf3_set_phy_link_mode(struct phy_device *phydev)
{
	/* MAC only supports full duplex mode */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);

	/* Only symmetric pause with flow control enabled is supported so no
	 * need to negotiate pause.
	 */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->advertising);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->advertising);
}

static struct mlxbf_gige_link_cfg mlxbf_gige_link_cfgs[] = {
	[MLXBF_GIGE_VERSION_BF2] = {
		.set_phy_link_mode = mlxbf_gige_bf2_set_phy_link_mode,
		.adjust_link = mlxbf_gige_bf2_adjust_link,
		.phy_mode = PHY_INTERFACE_MODE_GMII
	},
	[MLXBF_GIGE_VERSION_BF3] = {
		.set_phy_link_mode = mlxbf_gige_bf3_set_phy_link_mode,
		.adjust_link = mlxbf_gige_bf3_adjust_link,
		.phy_mode = PHY_INTERFACE_MODE_SGMII
	}
};

static int mlxbf_gige_probe(struct platform_device *pdev)
{
	struct phy_device *phydev;
	struct net_device *netdev;
	struct mlxbf_gige *priv;
	void __iomem *llu_base;
	void __iomem *plu_base;
	void __iomem *base;
	int addr, phy_irq;
	int err;

	base = devm_platform_ioremap_resource(pdev, MLXBF_GIGE_RES_MAC);
	if (IS_ERR(base))
		return PTR_ERR(base);

	llu_base = devm_platform_ioremap_resource(pdev, MLXBF_GIGE_RES_LLU);
	if (IS_ERR(llu_base))
		return PTR_ERR(llu_base);

	plu_base = devm_platform_ioremap_resource(pdev, MLXBF_GIGE_RES_PLU);
	if (IS_ERR(plu_base))
		return PTR_ERR(plu_base);

	netdev = devm_alloc_etherdev(&pdev->dev, sizeof(*priv));
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	netdev->netdev_ops = &mlxbf_gige_netdev_ops;
	netdev->ethtool_ops = &mlxbf_gige_ethtool_ops;
	priv = netdev_priv(netdev);
	priv->netdev = netdev;

	platform_set_drvdata(pdev, priv);
	priv->dev = &pdev->dev;
	priv->pdev = pdev;

	spin_lock_init(&priv->lock);

	priv->hw_version = readq(base + MLXBF_GIGE_VERSION);

	/* Attach MDIO device */
	err = mlxbf_gige_mdio_probe(pdev, priv);
	if (err)
		return err;

	priv->base = base;
	priv->llu_base = llu_base;
	priv->plu_base = plu_base;

	priv->rx_q_entries = MLXBF_GIGE_DEFAULT_RXQ_SZ;
	priv->tx_q_entries = MLXBF_GIGE_DEFAULT_TXQ_SZ;

	/* Write initial MAC address to hardware */
	mlxbf_gige_initial_mac(priv);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: 0x%x\n", err);
		goto out;
	}

	priv->error_irq = platform_get_irq(pdev, MLXBF_GIGE_ERROR_INTR_IDX);
	priv->rx_irq = platform_get_irq(pdev, MLXBF_GIGE_RECEIVE_PKT_INTR_IDX);
	priv->llu_plu_irq = platform_get_irq(pdev, MLXBF_GIGE_LLU_PLU_INTR_IDX);

	phy_irq = acpi_dev_gpio_irq_get_by(ACPI_COMPANION(&pdev->dev), "phy-gpios", 0);
	if (phy_irq < 0) {
		dev_err(&pdev->dev, "Error getting PHY irq. Use polling instead");
		phy_irq = PHY_POLL;
	}

	phydev = phy_find_first(priv->mdiobus);
	if (!phydev) {
		err = -ENODEV;
		goto out;
	}

	addr = phydev->mdio.addr;
	priv->mdiobus->irq[addr] = phy_irq;
	phydev->irq = phy_irq;

	err = phy_connect_direct(netdev, phydev,
				 mlxbf_gige_link_cfgs[priv->hw_version].adjust_link,
				 mlxbf_gige_link_cfgs[priv->hw_version].phy_mode);
	if (err) {
		dev_err(&pdev->dev, "Could not attach to PHY\n");
		goto out;
	}

	mlxbf_gige_link_cfgs[priv->hw_version].set_phy_link_mode(phydev);

	/* Display information about attached PHY device */
	phy_attached_info(phydev);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		phy_disconnect(phydev);
		goto out;
	}

	return 0;

out:
	mlxbf_gige_mdio_remove(priv);
	return err;
}

static void mlxbf_gige_remove(struct platform_device *pdev)
{
	struct mlxbf_gige *priv = platform_get_drvdata(pdev);

	unregister_netdev(priv->netdev);
	phy_disconnect(priv->netdev->phydev);
	mlxbf_gige_mdio_remove(priv);
	platform_set_drvdata(pdev, NULL);
}

static void mlxbf_gige_shutdown(struct platform_device *pdev)
{
	struct mlxbf_gige *priv = platform_get_drvdata(pdev);

	rtnl_lock();
	netif_device_detach(priv->netdev);

	if (netif_running(priv->netdev))
		dev_close(priv->netdev);

	rtnl_unlock();
}

static const struct acpi_device_id __maybe_unused mlxbf_gige_acpi_match[] = {
	{ "MLNXBF17", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, mlxbf_gige_acpi_match);

static struct platform_driver mlxbf_gige_driver = {
	.probe = mlxbf_gige_probe,
	.remove_new = mlxbf_gige_remove,
	.shutdown = mlxbf_gige_shutdown,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = ACPI_PTR(mlxbf_gige_acpi_match),
	},
};

module_platform_driver(mlxbf_gige_driver);

MODULE_DESCRIPTION("Mellanox BlueField SoC Gigabit Ethernet Driver");
MODULE_AUTHOR("David Thompson <davthompson@nvidia.com>");
MODULE_AUTHOR("Asmaa Mnebhi <asmaa@nvidia.com>");
MODULE_LICENSE("Dual BSD/GPL");
