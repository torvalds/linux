// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 */

/* Qualcomm Technologies, Inc. EMAC Gigabit Ethernet Driver */

#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_device.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include "emac.h"
#include "emac-mac.h"
#include "emac-phy.h"
#include "emac-sgmii.h"

#define EMAC_MSG_DEFAULT (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK |  \
		NETIF_MSG_TIMER | NETIF_MSG_IFDOWN | NETIF_MSG_IFUP)

#define EMAC_RRD_SIZE					     4
/* The RRD size if timestamping is enabled: */
#define EMAC_TS_RRD_SIZE				     6
#define EMAC_TPD_SIZE					     4
#define EMAC_RFD_SIZE					     2

#define REG_MAC_RX_STATUS_BIN		 EMAC_RXMAC_STATC_REG0
#define REG_MAC_RX_STATUS_END		EMAC_RXMAC_STATC_REG22
#define REG_MAC_TX_STATUS_BIN		 EMAC_TXMAC_STATC_REG0
#define REG_MAC_TX_STATUS_END		EMAC_TXMAC_STATC_REG24

#define RXQ0_NUM_RFD_PREF_DEF				     8
#define TXQ0_NUM_TPD_PREF_DEF				     5

#define EMAC_PREAMBLE_DEF				     7

#define DMAR_DLY_CNT_DEF				    15
#define DMAW_DLY_CNT_DEF				     4

#define IMR_NORMAL_MASK		(ISR_ERROR | ISR_OVER | ISR_TX_PKT)

#define ISR_TX_PKT      (\
	TX_PKT_INT      |\
	TX_PKT_INT1     |\
	TX_PKT_INT2     |\
	TX_PKT_INT3)

#define ISR_OVER        (\
	RFD0_UR_INT     |\
	RFD1_UR_INT     |\
	RFD2_UR_INT     |\
	RFD3_UR_INT     |\
	RFD4_UR_INT     |\
	RXF_OF_INT      |\
	TXF_UR_INT)

#define ISR_ERROR       (\
	DMAR_TO_INT     |\
	DMAW_TO_INT     |\
	TXQ_TO_INT)

/* in sync with enum emac_clk_id */
static const char * const emac_clk_name[] = {
	"axi_clk", "cfg_ahb_clk", "high_speed_clk", "mdio_clk", "tx_clk",
	"rx_clk", "sys_clk"
};

void emac_reg_update32(void __iomem *addr, u32 mask, u32 val)
{
	u32 data = readl(addr);

	writel(((data & ~mask) | val), addr);
}

/* reinitialize */
int emac_reinit_locked(struct emac_adapter *adpt)
{
	int ret;

	mutex_lock(&adpt->reset_lock);

	emac_mac_down(adpt);
	emac_sgmii_reset(adpt);
	ret = emac_mac_up(adpt);

	mutex_unlock(&adpt->reset_lock);

	return ret;
}

/* NAPI */
static int emac_napi_rtx(struct napi_struct *napi, int budget)
{
	struct emac_rx_queue *rx_q =
		container_of(napi, struct emac_rx_queue, napi);
	struct emac_adapter *adpt = netdev_priv(rx_q->netdev);
	struct emac_irq *irq = rx_q->irq;
	int work_done = 0;

	emac_mac_rx_process(adpt, rx_q, &work_done, budget);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);

		irq->mask |= rx_q->intr;
		writel(irq->mask, adpt->base + EMAC_INT_MASK);
	}

	return work_done;
}

/* Transmit the packet */
static netdev_tx_t emac_start_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	return emac_mac_tx_buf_send(adpt, &adpt->tx_q, skb);
}

static irqreturn_t emac_isr(int _irq, void *data)
{
	struct emac_irq *irq = data;
	struct emac_adapter *adpt =
		container_of(irq, struct emac_adapter, irq);
	struct emac_rx_queue *rx_q = &adpt->rx_q;
	u32 isr, status;

	/* disable the interrupt */
	writel(0, adpt->base + EMAC_INT_MASK);

	isr = readl_relaxed(adpt->base + EMAC_INT_STATUS);

	status = isr & irq->mask;
	if (status == 0)
		goto exit;

	if (status & ISR_ERROR) {
		net_err_ratelimited("%s: error interrupt 0x%lx\n",
				    adpt->netdev->name, status & ISR_ERROR);
		/* reset MAC */
		schedule_work(&adpt->work_thread);
	}

	/* Schedule the napi for receive queue with interrupt
	 * status bit set
	 */
	if (status & rx_q->intr) {
		if (napi_schedule_prep(&rx_q->napi)) {
			irq->mask &= ~rx_q->intr;
			__napi_schedule(&rx_q->napi);
		}
	}

	if (status & TX_PKT_INT)
		emac_mac_tx_process(adpt, &adpt->tx_q);

	if (status & ISR_OVER)
		net_warn_ratelimited("%s: TX/RX overflow interrupt\n",
				     adpt->netdev->name);

exit:
	/* enable the interrupt */
	writel(irq->mask, adpt->base + EMAC_INT_MASK);

	return IRQ_HANDLED;
}

/* Configure VLAN tag strip/insert feature */
static int emac_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	netdev_features_t changed = features ^ netdev->features;
	struct emac_adapter *adpt = netdev_priv(netdev);

	/* We only need to reprogram the hardware if the VLAN tag features
	 * have changed, and if it's already running.
	 */
	if (!(changed & (NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX)))
		return 0;

	if (!netif_running(netdev))
		return 0;

	/* emac_mac_mode_config() uses netdev->features to configure the EMAC,
	 * so make sure it's set first.
	 */
	netdev->features = features;

	return emac_reinit_locked(adpt);
}

/* Configure Multicast and Promiscuous modes */
static void emac_rx_mode_set(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct netdev_hw_addr *ha;

	emac_mac_mode_config(adpt);

	/* update multicast address filtering */
	emac_mac_multicast_addr_clear(adpt);
	netdev_for_each_mc_addr(ha, netdev)
		emac_mac_multicast_addr_set(adpt, ha->addr);
}

/* Change the Maximum Transfer Unit (MTU) */
static int emac_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	netif_dbg(adpt, hw, adpt->netdev,
		  "changing MTU from %d to %d\n", netdev->mtu,
		  new_mtu);
	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		return emac_reinit_locked(adpt);

	return 0;
}

/* Called when the network interface is made active */
static int emac_open(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct emac_irq	*irq = &adpt->irq;
	int ret;

	ret = request_irq(irq->irq, emac_isr, 0, "emac-core0", irq);
	if (ret) {
		netdev_err(adpt->netdev, "could not request emac-core0 irq\n");
		return ret;
	}

	/* allocate rx/tx dma buffer & descriptors */
	ret = emac_mac_rx_tx_rings_alloc_all(adpt);
	if (ret) {
		netdev_err(adpt->netdev, "error allocating rx/tx rings\n");
		free_irq(irq->irq, irq);
		return ret;
	}

	ret = emac_sgmii_open(adpt);
	if (ret) {
		emac_mac_rx_tx_rings_free_all(adpt);
		free_irq(irq->irq, irq);
		return ret;
	}

	ret = emac_mac_up(adpt);
	if (ret) {
		emac_mac_rx_tx_rings_free_all(adpt);
		free_irq(irq->irq, irq);
		emac_sgmii_close(adpt);
		return ret;
	}

	return 0;
}

/* Called when the network interface is disabled */
static int emac_close(struct net_device *netdev)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	mutex_lock(&adpt->reset_lock);

	emac_sgmii_close(adpt);
	emac_mac_down(adpt);
	emac_mac_rx_tx_rings_free_all(adpt);

	free_irq(adpt->irq.irq, &adpt->irq);

	mutex_unlock(&adpt->reset_lock);

	return 0;
}

/* Respond to a TX hang */
static void emac_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct emac_adapter *adpt = netdev_priv(netdev);

	schedule_work(&adpt->work_thread);
}

/**
 * emac_update_hw_stats - read the EMAC stat registers
 *
 * Reads the stats registers and write the values to adpt->stats.
 *
 * adpt->stats.lock must be held while calling this function,
 * and while reading from adpt->stats.
 */
void emac_update_hw_stats(struct emac_adapter *adpt)
{
	struct emac_stats *stats = &adpt->stats;
	u64 *stats_itr = &adpt->stats.rx_ok;
	void __iomem *base = adpt->base;
	unsigned int addr;

	addr = REG_MAC_RX_STATUS_BIN;
	while (addr <= REG_MAC_RX_STATUS_END) {
		*stats_itr += readl_relaxed(base + addr);
		stats_itr++;
		addr += sizeof(u32);
	}

	/* additional rx status */
	stats->rx_crc_align += readl_relaxed(base + EMAC_RXMAC_STATC_REG23);
	stats->rx_jabbers += readl_relaxed(base + EMAC_RXMAC_STATC_REG24);

	/* update tx status */
	addr = REG_MAC_TX_STATUS_BIN;
	stats_itr = &stats->tx_ok;

	while (addr <= REG_MAC_TX_STATUS_END) {
		*stats_itr += readl_relaxed(base + addr);
		stats_itr++;
		addr += sizeof(u32);
	}

	/* additional tx status */
	stats->tx_col += readl_relaxed(base + EMAC_TXMAC_STATC_REG25);
}

/* Provide network statistics info for the interface */
static void emac_get_stats64(struct net_device *netdev,
			     struct rtnl_link_stats64 *net_stats)
{
	struct emac_adapter *adpt = netdev_priv(netdev);
	struct emac_stats *stats = &adpt->stats;

	spin_lock(&stats->lock);

	emac_update_hw_stats(adpt);

	/* return parsed statistics */
	net_stats->rx_packets = stats->rx_ok;
	net_stats->tx_packets = stats->tx_ok;
	net_stats->rx_bytes = stats->rx_byte_cnt;
	net_stats->tx_bytes = stats->tx_byte_cnt;
	net_stats->multicast = stats->rx_mcast;
	net_stats->collisions = stats->tx_1_col + stats->tx_2_col * 2 +
				stats->tx_late_col + stats->tx_abort_col;

	net_stats->rx_errors = stats->rx_frag + stats->rx_fcs_err +
			       stats->rx_len_err + stats->rx_sz_ov +
			       stats->rx_align_err;
	net_stats->rx_fifo_errors = stats->rx_rxf_ov;
	net_stats->rx_length_errors = stats->rx_len_err;
	net_stats->rx_crc_errors = stats->rx_fcs_err;
	net_stats->rx_frame_errors = stats->rx_align_err;
	net_stats->rx_over_errors = stats->rx_rxf_ov;
	net_stats->rx_missed_errors = stats->rx_rxf_ov;

	net_stats->tx_errors = stats->tx_late_col + stats->tx_abort_col +
			       stats->tx_underrun + stats->tx_trunc;
	net_stats->tx_fifo_errors = stats->tx_underrun;
	net_stats->tx_aborted_errors = stats->tx_abort_col;
	net_stats->tx_window_errors = stats->tx_late_col;

	spin_unlock(&stats->lock);
}

static const struct net_device_ops emac_netdev_ops = {
	.ndo_open		= emac_open,
	.ndo_stop		= emac_close,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_start_xmit		= emac_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= emac_change_mtu,
	.ndo_do_ioctl		= phy_do_ioctl_running,
	.ndo_tx_timeout		= emac_tx_timeout,
	.ndo_get_stats64	= emac_get_stats64,
	.ndo_set_features       = emac_set_features,
	.ndo_set_rx_mode        = emac_rx_mode_set,
};

/* Watchdog task routine, called to reinitialize the EMAC */
static void emac_work_thread(struct work_struct *work)
{
	struct emac_adapter *adpt =
		container_of(work, struct emac_adapter, work_thread);

	emac_reinit_locked(adpt);
}

/* Initialize various data structures  */
static void emac_init_adapter(struct emac_adapter *adpt)
{
	u32 reg;

	adpt->rrd_size = EMAC_RRD_SIZE;
	adpt->tpd_size = EMAC_TPD_SIZE;
	adpt->rfd_size = EMAC_RFD_SIZE;

	/* descriptors */
	adpt->tx_desc_cnt = EMAC_DEF_TX_DESCS;
	adpt->rx_desc_cnt = EMAC_DEF_RX_DESCS;

	/* dma */
	adpt->dma_order = emac_dma_ord_out;
	adpt->dmar_block = emac_dma_req_4096;
	adpt->dmaw_block = emac_dma_req_128;
	adpt->dmar_dly_cnt = DMAR_DLY_CNT_DEF;
	adpt->dmaw_dly_cnt = DMAW_DLY_CNT_DEF;
	adpt->tpd_burst = TXQ0_NUM_TPD_PREF_DEF;
	adpt->rfd_burst = RXQ0_NUM_RFD_PREF_DEF;

	/* irq moderator */
	reg = ((EMAC_DEF_RX_IRQ_MOD >> 1) << IRQ_MODERATOR2_INIT_SHFT) |
	      ((EMAC_DEF_TX_IRQ_MOD >> 1) << IRQ_MODERATOR_INIT_SHFT);
	adpt->irq_mod = reg;

	/* others */
	adpt->preamble = EMAC_PREAMBLE_DEF;

	/* default to automatic flow control */
	adpt->automatic = true;

	/* Disable single-pause-frame mode by default */
	adpt->single_pause_mode = false;
}

/* Get the clock */
static int emac_clks_get(struct platform_device *pdev,
			 struct emac_adapter *adpt)
{
	unsigned int i;

	for (i = 0; i < EMAC_CLK_CNT; i++) {
		struct clk *clk = devm_clk_get(&pdev->dev, emac_clk_name[i]);

		if (IS_ERR(clk)) {
			dev_err(&pdev->dev,
				"could not claim clock %s (error=%li)\n",
				emac_clk_name[i], PTR_ERR(clk));

			return PTR_ERR(clk);
		}

		adpt->clk[i] = clk;
	}

	return 0;
}

/* Initialize clocks */
static int emac_clks_phase1_init(struct platform_device *pdev,
				 struct emac_adapter *adpt)
{
	int ret;

	/* On ACPI platforms, clocks are controlled by firmware and/or
	 * ACPI, not by drivers.
	 */
	if (has_acpi_companion(&pdev->dev))
		return 0;

	ret = emac_clks_get(pdev, adpt);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adpt->clk[EMAC_CLK_AXI]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adpt->clk[EMAC_CLK_CFG_AHB]);
	if (ret)
		goto disable_clk_axi;

	ret = clk_set_rate(adpt->clk[EMAC_CLK_HIGH_SPEED], 19200000);
	if (ret)
		goto disable_clk_cfg_ahb;

	ret = clk_prepare_enable(adpt->clk[EMAC_CLK_HIGH_SPEED]);
	if (ret)
		goto disable_clk_cfg_ahb;

	return 0;

disable_clk_cfg_ahb:
	clk_disable_unprepare(adpt->clk[EMAC_CLK_CFG_AHB]);
disable_clk_axi:
	clk_disable_unprepare(adpt->clk[EMAC_CLK_AXI]);

	return ret;
}

/* Enable clocks; needs emac_clks_phase1_init to be called before */
static int emac_clks_phase2_init(struct platform_device *pdev,
				 struct emac_adapter *adpt)
{
	int ret;

	if (has_acpi_companion(&pdev->dev))
		return 0;

	ret = clk_set_rate(adpt->clk[EMAC_CLK_TX], 125000000);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adpt->clk[EMAC_CLK_TX]);
	if (ret)
		return ret;

	ret = clk_set_rate(adpt->clk[EMAC_CLK_HIGH_SPEED], 125000000);
	if (ret)
		return ret;

	ret = clk_set_rate(adpt->clk[EMAC_CLK_MDIO], 25000000);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adpt->clk[EMAC_CLK_MDIO]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adpt->clk[EMAC_CLK_RX]);
	if (ret)
		return ret;

	return clk_prepare_enable(adpt->clk[EMAC_CLK_SYS]);
}

static void emac_clks_teardown(struct emac_adapter *adpt)
{

	unsigned int i;

	for (i = 0; i < EMAC_CLK_CNT; i++)
		clk_disable_unprepare(adpt->clk[i]);
}

/* Get the resources */
static int emac_probe_resources(struct platform_device *pdev,
				struct emac_adapter *adpt)
{
	struct net_device *netdev = adpt->netdev;
	char maddr[ETH_ALEN];
	int ret = 0;

	/* get mac address */
	if (device_get_mac_address(&pdev->dev, maddr, ETH_ALEN))
		ether_addr_copy(netdev->dev_addr, maddr);
	else
		eth_hw_addr_random(netdev);

	/* Core 0 interrupt */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	adpt->irq.irq = ret;

	/* base register address */
	adpt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adpt->base))
		return PTR_ERR(adpt->base);

	/* CSR register address */
	adpt->csr = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(adpt->csr))
		return PTR_ERR(adpt->csr);

	netdev->base_addr = (unsigned long)adpt->base;

	return 0;
}

static const struct of_device_id emac_dt_match[] = {
	{
		.compatible = "qcom,fsm9900-emac",
	},
	{}
};
MODULE_DEVICE_TABLE(of, emac_dt_match);

#if IS_ENABLED(CONFIG_ACPI)
static const struct acpi_device_id emac_acpi_match[] = {
	{
		.id = "QCOM8070",
	},
	{}
};
MODULE_DEVICE_TABLE(acpi, emac_acpi_match);
#endif

static int emac_probe(struct platform_device *pdev)
{
	struct net_device *netdev;
	struct emac_adapter *adpt;
	struct emac_sgmii *phy;
	u16 devid, revid;
	u32 reg;
	int ret;

	/* The TPD buffer address is limited to:
	 * 1. PTP:	45bits. (Driver doesn't support yet.)
	 * 2. NON-PTP:	46bits.
	 */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(46));
	if (ret) {
		dev_err(&pdev->dev, "could not set DMA mask\n");
		return ret;
	}

	netdev = alloc_etherdev(sizeof(struct emac_adapter));
	if (!netdev)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);
	emac_set_ethtool_ops(netdev);

	adpt = netdev_priv(netdev);
	adpt->netdev = netdev;
	adpt->msg_enable = EMAC_MSG_DEFAULT;

	phy = &adpt->phy;
	atomic_set(&phy->decode_error_count, 0);

	mutex_init(&adpt->reset_lock);
	spin_lock_init(&adpt->stats.lock);

	adpt->irq.mask = RX_PKT_INT0 | IMR_NORMAL_MASK;

	ret = emac_probe_resources(pdev, adpt);
	if (ret)
		goto err_undo_netdev;

	/* initialize clocks */
	ret = emac_clks_phase1_init(pdev, adpt);
	if (ret) {
		dev_err(&pdev->dev, "could not initialize clocks\n");
		goto err_undo_netdev;
	}

	netdev->watchdog_timeo = EMAC_WATCHDOG_TIME;
	netdev->irq = adpt->irq.irq;

	netdev->netdev_ops = &emac_netdev_ops;

	emac_init_adapter(adpt);

	/* init external phy */
	ret = emac_phy_config(pdev, adpt);
	if (ret)
		goto err_undo_clocks;

	/* init internal sgmii phy */
	ret = emac_sgmii_config(pdev, adpt);
	if (ret)
		goto err_undo_mdiobus;

	/* enable clocks */
	ret = emac_clks_phase2_init(pdev, adpt);
	if (ret) {
		dev_err(&pdev->dev, "could not initialize clocks\n");
		goto err_undo_mdiobus;
	}

	/* set hw features */
	netdev->features = NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_RXCSUM |
			NETIF_F_TSO | NETIF_F_TSO6 | NETIF_F_HW_VLAN_CTAG_RX |
			NETIF_F_HW_VLAN_CTAG_TX;
	netdev->hw_features = netdev->features;

	netdev->vlan_features |= NETIF_F_SG | NETIF_F_HW_CSUM |
				 NETIF_F_TSO | NETIF_F_TSO6;

	/* MTU range: 46 - 9194 */
	netdev->min_mtu = EMAC_MIN_ETH_FRAME_SIZE -
			  (ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);
	netdev->max_mtu = EMAC_MAX_ETH_FRAME_SIZE -
			  (ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN);

	INIT_WORK(&adpt->work_thread, emac_work_thread);

	/* Initialize queues */
	emac_mac_rx_tx_ring_init_all(pdev, adpt);

	netif_napi_add(netdev, &adpt->rx_q.napi, emac_napi_rtx,
		       NAPI_POLL_WEIGHT);

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(&pdev->dev, "could not register net device\n");
		goto err_undo_napi;
	}

	reg =  readl_relaxed(adpt->base + EMAC_DMA_MAS_CTRL);
	devid = (reg & DEV_ID_NUM_BMSK)  >> DEV_ID_NUM_SHFT;
	revid = (reg & DEV_REV_NUM_BMSK) >> DEV_REV_NUM_SHFT;
	reg = readl_relaxed(adpt->base + EMAC_CORE_HW_VERSION);

	netif_info(adpt, probe, netdev,
		   "hardware id %d.%d, hardware version %d.%d.%d\n",
		   devid, revid,
		   (reg & MAJOR_BMSK) >> MAJOR_SHFT,
		   (reg & MINOR_BMSK) >> MINOR_SHFT,
		   (reg & STEP_BMSK)  >> STEP_SHFT);

	return 0;

err_undo_napi:
	netif_napi_del(&adpt->rx_q.napi);
err_undo_mdiobus:
	put_device(&adpt->phydev->mdio.dev);
	mdiobus_unregister(adpt->mii_bus);
err_undo_clocks:
	emac_clks_teardown(adpt);
err_undo_netdev:
	free_netdev(netdev);

	return ret;
}

static int emac_remove(struct platform_device *pdev)
{
	struct net_device *netdev = dev_get_drvdata(&pdev->dev);
	struct emac_adapter *adpt = netdev_priv(netdev);

	unregister_netdev(netdev);
	netif_napi_del(&adpt->rx_q.napi);

	emac_clks_teardown(adpt);

	put_device(&adpt->phydev->mdio.dev);
	mdiobus_unregister(adpt->mii_bus);
	free_netdev(netdev);

	if (adpt->phy.digital)
		iounmap(adpt->phy.digital);
	iounmap(adpt->phy.base);

	return 0;
}

static void emac_shutdown(struct platform_device *pdev)
{
	struct net_device *netdev = dev_get_drvdata(&pdev->dev);
	struct emac_adapter *adpt = netdev_priv(netdev);

	if (netdev->flags & IFF_UP) {
		/* Closing the SGMII turns off its interrupts */
		emac_sgmii_close(adpt);

		/* Resetting the MAC turns off all DMA and its interrupts */
		emac_mac_reset(adpt);
	}
}

static struct platform_driver emac_platform_driver = {
	.probe	= emac_probe,
	.remove	= emac_remove,
	.driver = {
		.name		= "qcom-emac",
		.of_match_table = emac_dt_match,
		.acpi_match_table = ACPI_PTR(emac_acpi_match),
	},
	.shutdown = emac_shutdown,
};

module_platform_driver(emac_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-emac");
