/* Synopsys DesignWare Core Enterprise Ethernet (XLGMAC) Driver
 *
 * Copyright (c) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is dual-licensed; you may select either version 2 of
 * the GNU General Public License ("GPL") or BSD license ("BSD").
 *
 * This Synopsys DWC XLGMAC software driver and associated documentation
 * (hereinafter the "Software") is an unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing between
 * Synopsys and you. The Software IS NOT an item of Licensed Software or a
 * Licensed Product under any End User Software License Agreement or
 * Agreement for Licensed Products with Synopsys or any supplement thereto.
 * Synopsys is a registered trademark of Synopsys, Inc. Other names included
 * in the SOFTWARE may be the trademarks of their respective owners.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "dwc-xlgmac.h"
#include "dwc-xlgmac-reg.h"

static int debug = -1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "DWC ethernet debug level (0=none,...,16=all)");
static const u32 default_msg_level = (NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
				      NETIF_MSG_IFUP);

static unsigned char dev_addr[6] = {0, 0x55, 0x7b, 0xb5, 0x7d, 0xf7};

static void xlgmac_read_mac_addr(struct xlgmac_pdata *pdata)
{
	struct net_device *netdev = pdata->netdev;

	/* Currently it uses a static mac address for test */
	memcpy(pdata->mac_addr, dev_addr, netdev->addr_len);
}

static void xlgmac_default_config(struct xlgmac_pdata *pdata)
{
	pdata->tx_osp_mode = DMA_OSP_ENABLE;
	pdata->tx_sf_mode = MTL_TSF_ENABLE;
	pdata->rx_sf_mode = MTL_RSF_DISABLE;
	pdata->pblx8 = DMA_PBL_X8_ENABLE;
	pdata->tx_pbl = DMA_PBL_32;
	pdata->rx_pbl = DMA_PBL_32;
	pdata->tx_threshold = MTL_TX_THRESHOLD_128;
	pdata->rx_threshold = MTL_RX_THRESHOLD_128;
	pdata->tx_pause = 1;
	pdata->rx_pause = 1;
	pdata->phy_speed = SPEED_25000;
	pdata->sysclk_rate = XLGMAC_SYSCLOCK;

	strscpy(pdata->drv_name, XLGMAC_DRV_NAME, sizeof(pdata->drv_name));
	strscpy(pdata->drv_ver, XLGMAC_DRV_VERSION, sizeof(pdata->drv_ver));
}

static void xlgmac_init_all_ops(struct xlgmac_pdata *pdata)
{
	xlgmac_init_desc_ops(&pdata->desc_ops);
	xlgmac_init_hw_ops(&pdata->hw_ops);
}

static int xlgmac_init(struct xlgmac_pdata *pdata)
{
	struct xlgmac_hw_ops *hw_ops = &pdata->hw_ops;
	struct net_device *netdev = pdata->netdev;
	unsigned int i;
	int ret;

	/* Set default configuration data */
	xlgmac_default_config(pdata);

	/* Set irq, base_addr, MAC address, */
	netdev->irq = pdata->dev_irq;
	netdev->base_addr = (unsigned long)pdata->mac_regs;
	xlgmac_read_mac_addr(pdata);
	eth_hw_addr_set(netdev, pdata->mac_addr);

	/* Set all the function pointers */
	xlgmac_init_all_ops(pdata);

	/* Issue software reset to device */
	hw_ops->exit(pdata);

	/* Populate the hardware features */
	xlgmac_get_all_hw_features(pdata);
	xlgmac_print_all_hw_features(pdata);

	/* TODO: Set the PHY mode to XLGMII */

	/* Set the DMA mask */
	ret = dma_set_mask_and_coherent(pdata->dev,
					DMA_BIT_MASK(pdata->hw_feat.dma_width));
	if (ret) {
		dev_err(pdata->dev, "dma_set_mask_and_coherent failed\n");
		return ret;
	}

	/* Channel and ring params initializtion
	 *  pdata->channel_count;
	 *  pdata->tx_ring_count;
	 *  pdata->rx_ring_count;
	 *  pdata->tx_desc_count;
	 *  pdata->rx_desc_count;
	 */
	BUILD_BUG_ON_NOT_POWER_OF_2(XLGMAC_TX_DESC_CNT);
	pdata->tx_desc_count = XLGMAC_TX_DESC_CNT;
	if (pdata->tx_desc_count & (pdata->tx_desc_count - 1)) {
		dev_err(pdata->dev, "tx descriptor count (%d) is not valid\n",
			pdata->tx_desc_count);
		ret = -EINVAL;
		return ret;
	}
	BUILD_BUG_ON_NOT_POWER_OF_2(XLGMAC_RX_DESC_CNT);
	pdata->rx_desc_count = XLGMAC_RX_DESC_CNT;
	if (pdata->rx_desc_count & (pdata->rx_desc_count - 1)) {
		dev_err(pdata->dev, "rx descriptor count (%d) is not valid\n",
			pdata->rx_desc_count);
		ret = -EINVAL;
		return ret;
	}

	pdata->tx_ring_count = min_t(unsigned int, num_online_cpus(),
				     pdata->hw_feat.tx_ch_cnt);
	pdata->tx_ring_count = min_t(unsigned int, pdata->tx_ring_count,
				     pdata->hw_feat.tx_q_cnt);
	pdata->tx_q_count = pdata->tx_ring_count;
	ret = netif_set_real_num_tx_queues(netdev, pdata->tx_q_count);
	if (ret) {
		dev_err(pdata->dev, "error setting real tx queue count\n");
		return ret;
	}

	pdata->rx_ring_count = min_t(unsigned int,
				     netif_get_num_default_rss_queues(),
				     pdata->hw_feat.rx_ch_cnt);
	pdata->rx_ring_count = min_t(unsigned int, pdata->rx_ring_count,
				     pdata->hw_feat.rx_q_cnt);
	pdata->rx_q_count = pdata->rx_ring_count;
	ret = netif_set_real_num_rx_queues(netdev, pdata->rx_q_count);
	if (ret) {
		dev_err(pdata->dev, "error setting real rx queue count\n");
		return ret;
	}

	pdata->channel_count =
		max_t(unsigned int, pdata->tx_ring_count, pdata->rx_ring_count);

	/* Initialize RSS hash key and lookup table */
	netdev_rss_key_fill(pdata->rss_key, sizeof(pdata->rss_key));

	for (i = 0; i < XLGMAC_RSS_MAX_TABLE_SIZE; i++)
		pdata->rss_table[i] = XLGMAC_SET_REG_BITS(
					pdata->rss_table[i],
					MAC_RSSDR_DMCH_POS,
					MAC_RSSDR_DMCH_LEN,
					i % pdata->rx_ring_count);

	pdata->rss_options = XLGMAC_SET_REG_BITS(
				pdata->rss_options,
				MAC_RSSCR_IP2TE_POS,
				MAC_RSSCR_IP2TE_LEN, 1);
	pdata->rss_options = XLGMAC_SET_REG_BITS(
				pdata->rss_options,
				MAC_RSSCR_TCP4TE_POS,
				MAC_RSSCR_TCP4TE_LEN, 1);
	pdata->rss_options = XLGMAC_SET_REG_BITS(
				pdata->rss_options,
				MAC_RSSCR_UDP4TE_POS,
				MAC_RSSCR_UDP4TE_LEN, 1);

	/* Set device operations */
	netdev->netdev_ops = xlgmac_get_netdev_ops();
	netdev->ethtool_ops = xlgmac_get_ethtool_ops();

	/* Set device features */
	if (pdata->hw_feat.tso) {
		netdev->hw_features = NETIF_F_TSO;
		netdev->hw_features |= NETIF_F_TSO6;
		netdev->hw_features |= NETIF_F_SG;
		netdev->hw_features |= NETIF_F_IP_CSUM;
		netdev->hw_features |= NETIF_F_IPV6_CSUM;
	} else if (pdata->hw_feat.tx_coe) {
		netdev->hw_features = NETIF_F_IP_CSUM;
		netdev->hw_features |= NETIF_F_IPV6_CSUM;
	}

	if (pdata->hw_feat.rx_coe) {
		netdev->hw_features |= NETIF_F_RXCSUM;
		netdev->hw_features |= NETIF_F_GRO;
	}

	if (pdata->hw_feat.rss)
		netdev->hw_features |= NETIF_F_RXHASH;

	netdev->vlan_features |= netdev->hw_features;

	netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX;
	if (pdata->hw_feat.sa_vlan_ins)
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX;
	if (pdata->hw_feat.vlhash)
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	netdev->features |= netdev->hw_features;
	pdata->netdev_features = netdev->features;

	netdev->priv_flags |= IFF_UNICAST_FLT;

	/* Use default watchdog timeout */
	netdev->watchdog_timeo = 0;

	/* Tx coalesce parameters initialization */
	pdata->tx_usecs = XLGMAC_INIT_DMA_TX_USECS;
	pdata->tx_frames = XLGMAC_INIT_DMA_TX_FRAMES;

	/* Rx coalesce parameters initialization */
	pdata->rx_riwt = hw_ops->usec_to_riwt(pdata, XLGMAC_INIT_DMA_RX_USECS);
	pdata->rx_usecs = XLGMAC_INIT_DMA_RX_USECS;
	pdata->rx_frames = XLGMAC_INIT_DMA_RX_FRAMES;

	return 0;
}

int xlgmac_drv_probe(struct device *dev, struct xlgmac_resources *res)
{
	struct xlgmac_pdata *pdata;
	struct net_device *netdev;
	int ret;

	netdev = alloc_etherdev_mq(sizeof(struct xlgmac_pdata),
				   XLGMAC_MAX_DMA_CHANNELS);

	if (!netdev) {
		dev_err(dev, "alloc_etherdev failed\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(netdev, dev);
	dev_set_drvdata(dev, netdev);
	pdata = netdev_priv(netdev);
	pdata->dev = dev;
	pdata->netdev = netdev;

	pdata->dev_irq = res->irq;
	pdata->mac_regs = res->addr;

	mutex_init(&pdata->rss_mutex);
	pdata->msg_enable = netif_msg_init(debug, default_msg_level);

	ret = xlgmac_init(pdata);
	if (ret) {
		dev_err(dev, "xlgmac init failed\n");
		goto err_free_netdev;
	}

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(dev, "net device registration failed\n");
		goto err_free_netdev;
	}

	return 0;

err_free_netdev:
	free_netdev(netdev);

	return ret;
}

int xlgmac_drv_remove(struct device *dev)
{
	struct net_device *netdev = dev_get_drvdata(dev);

	unregister_netdev(netdev);
	free_netdev(netdev);

	return 0;
}

void xlgmac_dump_tx_desc(struct xlgmac_pdata *pdata,
			 struct xlgmac_ring *ring,
			 unsigned int idx,
			 unsigned int count,
			 unsigned int flag)
{
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_dma_desc *dma_desc;

	while (count--) {
		desc_data = XLGMAC_GET_DESC_DATA(ring, idx);
		dma_desc = desc_data->dma_desc;

		netdev_dbg(pdata->netdev, "TX: dma_desc=%p, dma_desc_addr=%pad\n",
			   desc_data->dma_desc, &desc_data->dma_desc_addr);
		netdev_dbg(pdata->netdev,
			   "TX_NORMAL_DESC[%d %s] = %08x:%08x:%08x:%08x\n", idx,
			   (flag == 1) ? "QUEUED FOR TX" : "TX BY DEVICE",
			   le32_to_cpu(dma_desc->desc0),
			   le32_to_cpu(dma_desc->desc1),
			   le32_to_cpu(dma_desc->desc2),
			   le32_to_cpu(dma_desc->desc3));

		idx++;
	}
}

void xlgmac_dump_rx_desc(struct xlgmac_pdata *pdata,
			 struct xlgmac_ring *ring,
			 unsigned int idx)
{
	struct xlgmac_desc_data *desc_data;
	struct xlgmac_dma_desc *dma_desc;

	desc_data = XLGMAC_GET_DESC_DATA(ring, idx);
	dma_desc = desc_data->dma_desc;

	netdev_dbg(pdata->netdev, "RX: dma_desc=%p, dma_desc_addr=%pad\n",
		   desc_data->dma_desc, &desc_data->dma_desc_addr);
	netdev_dbg(pdata->netdev,
		   "RX_NORMAL_DESC[%d RX BY DEVICE] = %08x:%08x:%08x:%08x\n",
		   idx,
		   le32_to_cpu(dma_desc->desc0),
		   le32_to_cpu(dma_desc->desc1),
		   le32_to_cpu(dma_desc->desc2),
		   le32_to_cpu(dma_desc->desc3));
}

void xlgmac_print_pkt(struct net_device *netdev,
		      struct sk_buff *skb, bool tx_rx)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	unsigned char buffer[128];
	unsigned int i;

	netdev_dbg(netdev, "\n************** SKB dump ****************\n");

	netdev_dbg(netdev, "%s packet of %d bytes\n",
		   (tx_rx ? "TX" : "RX"), skb->len);

	netdev_dbg(netdev, "Dst MAC addr: %pM\n", eth->h_dest);
	netdev_dbg(netdev, "Src MAC addr: %pM\n", eth->h_source);
	netdev_dbg(netdev, "Protocol: %#06hx\n", ntohs(eth->h_proto));

	for (i = 0; i < skb->len; i += 32) {
		unsigned int len = min(skb->len - i, 32U);

		hex_dump_to_buffer(&skb->data[i], len, 32, 1,
				   buffer, sizeof(buffer), false);
		netdev_dbg(netdev, "  %#06x: %s\n", i, buffer);
	}

	netdev_dbg(netdev, "\n************** SKB dump ****************\n");
}

void xlgmac_get_all_hw_features(struct xlgmac_pdata *pdata)
{
	struct xlgmac_hw_features *hw_feat = &pdata->hw_feat;
	unsigned int mac_hfr0, mac_hfr1, mac_hfr2;

	mac_hfr0 = readl(pdata->mac_regs + MAC_HWF0R);
	mac_hfr1 = readl(pdata->mac_regs + MAC_HWF1R);
	mac_hfr2 = readl(pdata->mac_regs + MAC_HWF2R);

	memset(hw_feat, 0, sizeof(*hw_feat));

	hw_feat->version = readl(pdata->mac_regs + MAC_VR);

	/* Hardware feature register 0 */
	hw_feat->phyifsel    = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_PHYIFSEL_POS,
						MAC_HWF0R_PHYIFSEL_LEN);
	hw_feat->vlhash      = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_VLHASH_POS,
						MAC_HWF0R_VLHASH_LEN);
	hw_feat->sma         = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_SMASEL_POS,
						MAC_HWF0R_SMASEL_LEN);
	hw_feat->rwk         = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_RWKSEL_POS,
						MAC_HWF0R_RWKSEL_LEN);
	hw_feat->mgk         = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_MGKSEL_POS,
						MAC_HWF0R_MGKSEL_LEN);
	hw_feat->mmc         = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_MMCSEL_POS,
						MAC_HWF0R_MMCSEL_LEN);
	hw_feat->aoe         = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_ARPOFFSEL_POS,
						MAC_HWF0R_ARPOFFSEL_LEN);
	hw_feat->ts          = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_TSSEL_POS,
						MAC_HWF0R_TSSEL_LEN);
	hw_feat->eee         = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_EEESEL_POS,
						MAC_HWF0R_EEESEL_LEN);
	hw_feat->tx_coe      = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_TXCOESEL_POS,
						MAC_HWF0R_TXCOESEL_LEN);
	hw_feat->rx_coe      = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_RXCOESEL_POS,
						MAC_HWF0R_RXCOESEL_LEN);
	hw_feat->addn_mac    = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_ADDMACADRSEL_POS,
						MAC_HWF0R_ADDMACADRSEL_LEN);
	hw_feat->ts_src      = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_TSSTSSEL_POS,
						MAC_HWF0R_TSSTSSEL_LEN);
	hw_feat->sa_vlan_ins = XLGMAC_GET_REG_BITS(mac_hfr0,
						MAC_HWF0R_SAVLANINS_POS,
						MAC_HWF0R_SAVLANINS_LEN);

	/* Hardware feature register 1 */
	hw_feat->rx_fifo_size  = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_RXFIFOSIZE_POS,
						MAC_HWF1R_RXFIFOSIZE_LEN);
	hw_feat->tx_fifo_size  = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_TXFIFOSIZE_POS,
						MAC_HWF1R_TXFIFOSIZE_LEN);
	hw_feat->adv_ts_hi     = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_ADVTHWORD_POS,
						MAC_HWF1R_ADVTHWORD_LEN);
	hw_feat->dma_width     = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_ADDR64_POS,
						MAC_HWF1R_ADDR64_LEN);
	hw_feat->dcb           = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_DCBEN_POS,
						MAC_HWF1R_DCBEN_LEN);
	hw_feat->sph           = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_SPHEN_POS,
						MAC_HWF1R_SPHEN_LEN);
	hw_feat->tso           = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_TSOEN_POS,
						MAC_HWF1R_TSOEN_LEN);
	hw_feat->dma_debug     = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_DBGMEMA_POS,
						MAC_HWF1R_DBGMEMA_LEN);
	hw_feat->rss           = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_RSSEN_POS,
						MAC_HWF1R_RSSEN_LEN);
	hw_feat->tc_cnt	       = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_NUMTC_POS,
						MAC_HWF1R_NUMTC_LEN);
	hw_feat->hash_table_size = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_HASHTBLSZ_POS,
						MAC_HWF1R_HASHTBLSZ_LEN);
	hw_feat->l3l4_filter_num = XLGMAC_GET_REG_BITS(mac_hfr1,
						MAC_HWF1R_L3L4FNUM_POS,
						MAC_HWF1R_L3L4FNUM_LEN);

	/* Hardware feature register 2 */
	hw_feat->rx_q_cnt     = XLGMAC_GET_REG_BITS(mac_hfr2,
						MAC_HWF2R_RXQCNT_POS,
						MAC_HWF2R_RXQCNT_LEN);
	hw_feat->tx_q_cnt     = XLGMAC_GET_REG_BITS(mac_hfr2,
						MAC_HWF2R_TXQCNT_POS,
						MAC_HWF2R_TXQCNT_LEN);
	hw_feat->rx_ch_cnt    = XLGMAC_GET_REG_BITS(mac_hfr2,
						MAC_HWF2R_RXCHCNT_POS,
						MAC_HWF2R_RXCHCNT_LEN);
	hw_feat->tx_ch_cnt    = XLGMAC_GET_REG_BITS(mac_hfr2,
						MAC_HWF2R_TXCHCNT_POS,
						MAC_HWF2R_TXCHCNT_LEN);
	hw_feat->pps_out_num  = XLGMAC_GET_REG_BITS(mac_hfr2,
						MAC_HWF2R_PPSOUTNUM_POS,
						MAC_HWF2R_PPSOUTNUM_LEN);
	hw_feat->aux_snap_num = XLGMAC_GET_REG_BITS(mac_hfr2,
						MAC_HWF2R_AUXSNAPNUM_POS,
						MAC_HWF2R_AUXSNAPNUM_LEN);

	/* Translate the Hash Table size into actual number */
	switch (hw_feat->hash_table_size) {
	case 0:
		break;
	case 1:
		hw_feat->hash_table_size = 64;
		break;
	case 2:
		hw_feat->hash_table_size = 128;
		break;
	case 3:
		hw_feat->hash_table_size = 256;
		break;
	}

	/* Translate the address width setting into actual number */
	switch (hw_feat->dma_width) {
	case 0:
		hw_feat->dma_width = 32;
		break;
	case 1:
		hw_feat->dma_width = 40;
		break;
	case 2:
		hw_feat->dma_width = 48;
		break;
	default:
		hw_feat->dma_width = 32;
	}

	/* The Queue, Channel and TC counts are zero based so increment them
	 * to get the actual number
	 */
	hw_feat->rx_q_cnt++;
	hw_feat->tx_q_cnt++;
	hw_feat->rx_ch_cnt++;
	hw_feat->tx_ch_cnt++;
	hw_feat->tc_cnt++;
}

void xlgmac_print_all_hw_features(struct xlgmac_pdata *pdata)
{
	char __maybe_unused *str = NULL;

	XLGMAC_PR("\n");
	XLGMAC_PR("=====================================================\n");
	XLGMAC_PR("\n");
	XLGMAC_PR("HW support following features\n");
	XLGMAC_PR("\n");
	/* HW Feature Register0 */
	XLGMAC_PR("VLAN Hash Filter Selected                   : %s\n",
		  pdata->hw_feat.vlhash ? "YES" : "NO");
	XLGMAC_PR("SMA (MDIO) Interface                        : %s\n",
		  pdata->hw_feat.sma ? "YES" : "NO");
	XLGMAC_PR("PMT Remote Wake-up Packet Enable            : %s\n",
		  pdata->hw_feat.rwk ? "YES" : "NO");
	XLGMAC_PR("PMT Magic Packet Enable                     : %s\n",
		  pdata->hw_feat.mgk ? "YES" : "NO");
	XLGMAC_PR("RMON/MMC Module Enable                      : %s\n",
		  pdata->hw_feat.mmc ? "YES" : "NO");
	XLGMAC_PR("ARP Offload Enabled                         : %s\n",
		  pdata->hw_feat.aoe ? "YES" : "NO");
	XLGMAC_PR("IEEE 1588-2008 Timestamp Enabled            : %s\n",
		  pdata->hw_feat.ts ? "YES" : "NO");
	XLGMAC_PR("Energy Efficient Ethernet Enabled           : %s\n",
		  pdata->hw_feat.eee ? "YES" : "NO");
	XLGMAC_PR("Transmit Checksum Offload Enabled           : %s\n",
		  pdata->hw_feat.tx_coe ? "YES" : "NO");
	XLGMAC_PR("Receive Checksum Offload Enabled            : %s\n",
		  pdata->hw_feat.rx_coe ? "YES" : "NO");
	XLGMAC_PR("Additional MAC Addresses 1-31 Selected      : %s\n",
		  pdata->hw_feat.addn_mac ? "YES" : "NO");

	switch (pdata->hw_feat.ts_src) {
	case 0:
		str = "RESERVED";
		break;
	case 1:
		str = "INTERNAL";
		break;
	case 2:
		str = "EXTERNAL";
		break;
	case 3:
		str = "BOTH";
		break;
	}
	XLGMAC_PR("Timestamp System Time Source                : %s\n", str);

	XLGMAC_PR("Source Address or VLAN Insertion Enable     : %s\n",
		  pdata->hw_feat.sa_vlan_ins ? "YES" : "NO");

	/* HW Feature Register1 */
	switch (pdata->hw_feat.rx_fifo_size) {
	case 0:
		str = "128 bytes";
		break;
	case 1:
		str = "256 bytes";
		break;
	case 2:
		str = "512 bytes";
		break;
	case 3:
		str = "1 KBytes";
		break;
	case 4:
		str = "2 KBytes";
		break;
	case 5:
		str = "4 KBytes";
		break;
	case 6:
		str = "8 KBytes";
		break;
	case 7:
		str = "16 KBytes";
		break;
	case 8:
		str = "32 kBytes";
		break;
	case 9:
		str = "64 KBytes";
		break;
	case 10:
		str = "128 KBytes";
		break;
	case 11:
		str = "256 KBytes";
		break;
	default:
		str = "RESERVED";
	}
	XLGMAC_PR("MTL Receive FIFO Size                       : %s\n", str);

	switch (pdata->hw_feat.tx_fifo_size) {
	case 0:
		str = "128 bytes";
		break;
	case 1:
		str = "256 bytes";
		break;
	case 2:
		str = "512 bytes";
		break;
	case 3:
		str = "1 KBytes";
		break;
	case 4:
		str = "2 KBytes";
		break;
	case 5:
		str = "4 KBytes";
		break;
	case 6:
		str = "8 KBytes";
		break;
	case 7:
		str = "16 KBytes";
		break;
	case 8:
		str = "32 kBytes";
		break;
	case 9:
		str = "64 KBytes";
		break;
	case 10:
		str = "128 KBytes";
		break;
	case 11:
		str = "256 KBytes";
		break;
	default:
		str = "RESERVED";
	}
	XLGMAC_PR("MTL Transmit FIFO Size                      : %s\n", str);

	XLGMAC_PR("IEEE 1588 High Word Register Enable         : %s\n",
		  pdata->hw_feat.adv_ts_hi ? "YES" : "NO");
	XLGMAC_PR("Address width                               : %u\n",
		  pdata->hw_feat.dma_width);
	XLGMAC_PR("DCB Feature Enable                          : %s\n",
		  pdata->hw_feat.dcb ? "YES" : "NO");
	XLGMAC_PR("Split Header Feature Enable                 : %s\n",
		  pdata->hw_feat.sph ? "YES" : "NO");
	XLGMAC_PR("TCP Segmentation Offload Enable             : %s\n",
		  pdata->hw_feat.tso ? "YES" : "NO");
	XLGMAC_PR("DMA Debug Registers Enabled                 : %s\n",
		  pdata->hw_feat.dma_debug ? "YES" : "NO");
	XLGMAC_PR("RSS Feature Enabled                         : %s\n",
		  pdata->hw_feat.rss ? "YES" : "NO");
	XLGMAC_PR("Number of Traffic classes                   : %u\n",
		  (pdata->hw_feat.tc_cnt));
	XLGMAC_PR("Hash Table Size                             : %u\n",
		  pdata->hw_feat.hash_table_size);
	XLGMAC_PR("Total number of L3 or L4 Filters            : %u\n",
		  pdata->hw_feat.l3l4_filter_num);

	/* HW Feature Register2 */
	XLGMAC_PR("Number of MTL Receive Queues                : %u\n",
		  pdata->hw_feat.rx_q_cnt);
	XLGMAC_PR("Number of MTL Transmit Queues               : %u\n",
		  pdata->hw_feat.tx_q_cnt);
	XLGMAC_PR("Number of DMA Receive Channels              : %u\n",
		  pdata->hw_feat.rx_ch_cnt);
	XLGMAC_PR("Number of DMA Transmit Channels             : %u\n",
		  pdata->hw_feat.tx_ch_cnt);

	switch (pdata->hw_feat.pps_out_num) {
	case 0:
		str = "No PPS output";
		break;
	case 1:
		str = "1 PPS output";
		break;
	case 2:
		str = "2 PPS output";
		break;
	case 3:
		str = "3 PPS output";
		break;
	case 4:
		str = "4 PPS output";
		break;
	default:
		str = "RESERVED";
	}
	XLGMAC_PR("Number of PPS Outputs                       : %s\n", str);

	switch (pdata->hw_feat.aux_snap_num) {
	case 0:
		str = "No auxiliary input";
		break;
	case 1:
		str = "1 auxiliary input";
		break;
	case 2:
		str = "2 auxiliary input";
		break;
	case 3:
		str = "3 auxiliary input";
		break;
	case 4:
		str = "4 auxiliary input";
		break;
	default:
		str = "RESERVED";
	}
	XLGMAC_PR("Number of Auxiliary Snapshot Inputs         : %s", str);

	XLGMAC_PR("\n");
	XLGMAC_PR("=====================================================\n");
	XLGMAC_PR("\n");
}

MODULE_DESCRIPTION(XLGMAC_DRV_DESC);
MODULE_VERSION(XLGMAC_DRV_VERSION);
MODULE_AUTHOR("Jie Deng <jiedeng@synopsys.com>");
MODULE_LICENSE("Dual BSD/GPL");
