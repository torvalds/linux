// SPDX-License-Identifier: GPL-2.0

/* Texas Instruments ICSSG SR1.0 Ethernet Driver
 *
 * Copyright (C) 2018-2022 Texas Instruments Incorporated - https://www.ti.com/
 * Copyright (c) Siemens AG, 2024
 *
 */

#include <linux/etherdevice.h>
#include <linux/genalloc.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/phy.h>
#include <linux/remoteproc/pruss.h>
#include <linux/pruss_driver.h>

#include "icssg_prueth.h"
#include "icssg_mii_rt.h"
#include "../k3-cppi-desc-pool.h"

#define PRUETH_MODULE_DESCRIPTION "PRUSS ICSSG SR1.0 Ethernet driver"

/* SR1: Set buffer sizes for the pools. There are 8 internal queues
 * implemented in firmware, but only 4 tx channels/threads in the Egress
 * direction to firmware. Need a high priority queue for management
 * messages since they shouldn't be blocked even during high traffic
 * situation. So use Q0-Q2 as data queues and Q3 as management queue
 * in the max case. However for ease of configuration, use the max
 * data queue + 1 for management message if we are not using max
 * case.
 *
 * Allocate 4 MTU buffers per data queue.  Firmware requires
 * pool sizes to be set for internal queues. Set the upper 5 queue
 * pool size to min size of 128 bytes since there are only 3 tx
 * data channels and management queue requires only minimum buffer.
 * i.e lower queues are used by driver and highest priority queue
 * from that is used for management message.
 */

static int emac_egress_buf_pool_size[] = {
	PRUETH_EMAC_BUF_POOL_SIZE_SR1, PRUETH_EMAC_BUF_POOL_SIZE_SR1,
	PRUETH_EMAC_BUF_POOL_SIZE_SR1, PRUETH_EMAC_BUF_POOL_MIN_SIZE_SR1,
	PRUETH_EMAC_BUF_POOL_MIN_SIZE_SR1, PRUETH_EMAC_BUF_POOL_MIN_SIZE_SR1,
	PRUETH_EMAC_BUF_POOL_MIN_SIZE_SR1, PRUETH_EMAC_BUF_POOL_MIN_SIZE_SR1
};

static void icssg_config_sr1(struct prueth *prueth, struct prueth_emac *emac,
			     int slice)
{
	struct icssg_sr1_config config;
	void __iomem *va;
	int i, index;

	memset(&config, 0, sizeof(config));
	config.addr_lo = cpu_to_le32(lower_32_bits(prueth->msmcram.pa));
	config.addr_hi = cpu_to_le32(upper_32_bits(prueth->msmcram.pa));
	config.rx_flow_id = cpu_to_le32(emac->rx_flow_id_base); /* flow id for host port */
	config.rx_mgr_flow_id = cpu_to_le32(emac->rx_mgm_flow_id_base); /* for mgm ch */
	config.rand_seed = cpu_to_le32(get_random_u32());

	for (i = PRUETH_EMAC_BUF_POOL_START_SR1; i < PRUETH_NUM_BUF_POOLS_SR1; i++) {
		index =  i - PRUETH_EMAC_BUF_POOL_START_SR1;
		config.tx_buf_sz[i] = cpu_to_le32(emac_egress_buf_pool_size[index]);
	}

	va = prueth->shram.va + slice * ICSSG_CONFIG_OFFSET_SLICE1;
	memcpy_toio(va, &config, sizeof(config));

	emac->speed = SPEED_1000;
	emac->duplex = DUPLEX_FULL;
}

static int emac_send_command_sr1(struct prueth_emac *emac, u32 cmd)
{
	struct cppi5_host_desc_t *first_desc;
	u32 pkt_len = sizeof(emac->cmd_data);
	__le32 *data = emac->cmd_data;
	dma_addr_t desc_dma, buf_dma;
	struct prueth_tx_chn *tx_chn;
	void **swdata;
	int ret = 0;
	u32 *epib;

	netdev_dbg(emac->ndev, "Sending cmd %x\n", cmd);

	/* only one command at a time allowed to firmware */
	mutex_lock(&emac->cmd_lock);
	data[0] = cpu_to_le32(cmd);

	/* highest priority channel for management messages */
	tx_chn = &emac->tx_chns[emac->tx_ch_num - 1];

	/* Map the linear buffer */
	buf_dma = dma_map_single(tx_chn->dma_dev, data, pkt_len, DMA_TO_DEVICE);
	if (dma_mapping_error(tx_chn->dma_dev, buf_dma)) {
		netdev_err(emac->ndev, "cmd %x: failed to map cmd buffer\n", cmd);
		ret = -EINVAL;
		goto err_unlock;
	}

	first_desc = k3_cppi_desc_pool_alloc(tx_chn->desc_pool);
	if (!first_desc) {
		netdev_err(emac->ndev, "cmd %x: failed to allocate descriptor\n", cmd);
		dma_unmap_single(tx_chn->dma_dev, buf_dma, pkt_len, DMA_TO_DEVICE);
		ret = -ENOMEM;
		goto err_unlock;
	}

	cppi5_hdesc_init(first_desc, CPPI5_INFO0_HDESC_EPIB_PRESENT,
			 PRUETH_NAV_PS_DATA_SIZE);
	cppi5_hdesc_set_pkttype(first_desc, PRUETH_PKT_TYPE_CMD);
	epib = first_desc->epib;
	epib[0] = 0;
	epib[1] = 0;

	cppi5_hdesc_attach_buf(first_desc, buf_dma, pkt_len, buf_dma, pkt_len);
	swdata = cppi5_hdesc_get_swdata(first_desc);
	*swdata = data;

	cppi5_hdesc_set_pktlen(first_desc, pkt_len);
	desc_dma = k3_cppi_desc_pool_virt2dma(tx_chn->desc_pool, first_desc);

	/* send command */
	reinit_completion(&emac->cmd_complete);
	ret = k3_udma_glue_push_tx_chn(tx_chn->tx_chn, first_desc, desc_dma);
	if (ret) {
		netdev_err(emac->ndev, "cmd %x: push failed: %d\n", cmd, ret);
		goto free_desc;
	}
	ret = wait_for_completion_timeout(&emac->cmd_complete, msecs_to_jiffies(100));
	if (!ret)
		netdev_err(emac->ndev, "cmd %x: completion timeout\n", cmd);

	mutex_unlock(&emac->cmd_lock);

	return ret;
free_desc:
	prueth_xmit_free(tx_chn, first_desc);
err_unlock:
	mutex_unlock(&emac->cmd_lock);

	return ret;
}

static void icssg_config_set_speed_sr1(struct prueth_emac *emac)
{
	u32 cmd = ICSSG_PSTATE_SPEED_DUPLEX_CMD_SR1, val;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);

	val = icssg_rgmii_get_speed(prueth->miig_rt, slice);
	/* firmware expects speed settings in bit 2-1 */
	val <<= 1;
	cmd |= val;

	val = icssg_rgmii_get_fullduplex(prueth->miig_rt, slice);
	/* firmware expects full duplex settings in bit 3 */
	val <<= 3;
	cmd |= val;

	emac_send_command_sr1(emac, cmd);
}

/* called back by PHY layer if there is change in link state of hw port*/
static void emac_adjust_link_sr1(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;
	struct prueth *prueth = emac->prueth;
	bool new_state = false;
	unsigned long flags;

	if (phydev->link) {
		/* check the mode of operation - full/half duplex */
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

		/* f/w should support 100 & 1000 */
		emac->speed = SPEED_1000;

		/* half duplex may not be supported by f/w */
		emac->duplex = DUPLEX_FULL;
	}

	if (new_state) {
		phy_print_status(phydev);

		/* update RGMII and MII configuration based on PHY negotiated
		 * values
		 */
		if (emac->link) {
			/* Set the RGMII cfg for gig en and full duplex */
			icssg_update_rgmii_cfg(prueth->miig_rt, emac);

			/* update the Tx IPG based on 100M/1G speed */
			spin_lock_irqsave(&emac->lock, flags);
			icssg_config_ipg(emac);
			spin_unlock_irqrestore(&emac->lock, flags);
			icssg_config_set_speed_sr1(emac);
		}
	}

	if (emac->link) {
		/* reactivate the transmit queue */
		netif_tx_wake_all_queues(ndev);
	} else {
		netif_tx_stop_all_queues(ndev);
		prueth_cleanup_tx_ts(emac);
	}
}

static int emac_phy_connect(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	struct net_device *ndev = emac->ndev;
	/* connect PHY */
	ndev->phydev = of_phy_connect(emac->ndev, emac->phy_node,
				      &emac_adjust_link_sr1, 0,
				      emac->phy_if);
	if (!ndev->phydev) {
		dev_err(prueth->dev, "couldn't connect to phy %s\n",
			emac->phy_node->full_name);
		return -ENODEV;
	}

	if (!emac->half_duplex) {
		dev_dbg(prueth->dev, "half duplex mode is not supported\n");
		phy_remove_link_mode(ndev->phydev, ETHTOOL_LINK_MODE_10baseT_Half_BIT);
	}

	/* Remove 100Mbits half-duplex due to RGMII misreporting connection
	 * as full duplex */
	phy_remove_link_mode(ndev->phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);

	/* remove unsupported modes */
	phy_remove_link_mode(ndev->phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_remove_link_mode(ndev->phydev, ETHTOOL_LINK_MODE_Pause_BIT);
	phy_remove_link_mode(ndev->phydev, ETHTOOL_LINK_MODE_Asym_Pause_BIT);

	if (emac->phy_if == PHY_INTERFACE_MODE_MII)
		phy_set_max_speed(ndev->phydev, SPEED_100);

	return 0;
}

/* get one packet from requested flow_id
 *
 * Returns skb pointer if packet found else NULL
 * Caller must free the returned skb.
 */
static struct sk_buff *prueth_process_rx_mgm(struct prueth_emac *emac,
					     u32 flow_id)
{
	struct prueth_rx_chn *rx_chn = &emac->rx_mgm_chn;
	struct net_device *ndev = emac->ndev;
	struct cppi5_host_desc_t *desc_rx;
	struct sk_buff *skb, *new_skb;
	dma_addr_t desc_dma, buf_dma;
	u32 buf_dma_len, pkt_len;
	void **swdata;
	int ret;

	ret = k3_udma_glue_pop_rx_chn(rx_chn->rx_chn, flow_id, &desc_dma);
	if (ret) {
		if (ret != -ENODATA)
			netdev_err(ndev, "rx mgm pop: failed: %d\n", ret);
		return NULL;
	}

	if (cppi5_desc_is_tdcm(desc_dma)) /* Teardown */
		return NULL;

	desc_rx = k3_cppi_desc_pool_dma2virt(rx_chn->desc_pool, desc_dma);

	/* Fix FW bug about incorrect PSDATA size */
	if (cppi5_hdesc_get_psdata_size(desc_rx) != PRUETH_NAV_PS_DATA_SIZE) {
		cppi5_hdesc_update_psdata_size(desc_rx,
					       PRUETH_NAV_PS_DATA_SIZE);
	}

	swdata = cppi5_hdesc_get_swdata(desc_rx);
	skb = *swdata;
	cppi5_hdesc_get_obuf(desc_rx, &buf_dma, &buf_dma_len);
	pkt_len = cppi5_hdesc_get_pktlen(desc_rx);

	dma_unmap_single(rx_chn->dma_dev, buf_dma, buf_dma_len, DMA_FROM_DEVICE);
	k3_cppi_desc_pool_free(rx_chn->desc_pool, desc_rx);

	new_skb = netdev_alloc_skb_ip_align(ndev, PRUETH_MAX_PKT_SIZE);
	/* if allocation fails we drop the packet but push the
	 * descriptor back to the ring with old skb to prevent a stall
	 */
	if (!new_skb) {
		netdev_err(ndev,
			   "skb alloc failed, dropped mgm pkt from flow %d\n",
			   flow_id);
		new_skb = skb;
		skb = NULL;	/* return NULL */
	} else {
		/* return the filled skb */
		skb_put(skb, pkt_len);
	}

	/* queue another DMA */
	ret = prueth_dma_rx_push(emac, new_skb, &emac->rx_mgm_chn);
	if (WARN_ON(ret < 0))
		dev_kfree_skb_any(new_skb);

	return skb;
}

static void prueth_tx_ts_sr1(struct prueth_emac *emac,
			     struct emac_tx_ts_response_sr1 *tsr)
{
	struct skb_shared_hwtstamps ssh;
	u32 hi_ts, lo_ts, cookie;
	struct sk_buff *skb;
	u64 ns;

	hi_ts = le32_to_cpu(tsr->hi_ts);
	lo_ts = le32_to_cpu(tsr->lo_ts);

	ns = (u64)hi_ts << 32 | lo_ts;

	cookie = le32_to_cpu(tsr->cookie);
	if (cookie >= PRUETH_MAX_TX_TS_REQUESTS) {
		netdev_dbg(emac->ndev, "Invalid TX TS cookie 0x%x\n",
			   cookie);
		return;
	}

	skb = emac->tx_ts_skb[cookie];
	emac->tx_ts_skb[cookie] = NULL;	/* free slot */

	memset(&ssh, 0, sizeof(ssh));
	ssh.hwtstamp = ns_to_ktime(ns);

	skb_tstamp_tx(skb, &ssh);
	dev_consume_skb_any(skb);
}

static irqreturn_t prueth_rx_mgm_ts_thread_sr1(int irq, void *dev_id)
{
	struct prueth_emac *emac = dev_id;
	struct sk_buff *skb;

	skb = prueth_process_rx_mgm(emac, PRUETH_RX_MGM_FLOW_TIMESTAMP_SR1);
	if (!skb)
		return IRQ_NONE;

	prueth_tx_ts_sr1(emac, (void *)skb->data);
	dev_kfree_skb_any(skb);

	return IRQ_HANDLED;
}

static irqreturn_t prueth_rx_mgm_rsp_thread(int irq, void *dev_id)
{
	struct prueth_emac *emac = dev_id;
	struct sk_buff *skb;
	u32 rsp;

	skb = prueth_process_rx_mgm(emac, PRUETH_RX_MGM_FLOW_RESPONSE_SR1);
	if (!skb)
		return IRQ_NONE;

	/* Process command response */
	rsp = le32_to_cpu(*(__le32 *)skb->data) & 0xffff0000;
	if (rsp == ICSSG_SHUTDOWN_CMD_SR1) {
		netdev_dbg(emac->ndev, "f/w Shutdown cmd resp %x\n", rsp);
		complete(&emac->cmd_complete);
	} else if (rsp == ICSSG_PSTATE_SPEED_DUPLEX_CMD_SR1) {
		netdev_dbg(emac->ndev, "f/w Speed/Duplex cmd rsp %x\n", rsp);
		complete(&emac->cmd_complete);
	}

	dev_kfree_skb_any(skb);

	return IRQ_HANDLED;
}

static struct icssg_firmwares icssg_sr1_emac_firmwares[] = {
	{
		.pru = "ti-pruss/am65x-pru0-prueth-fw.elf",
		.rtu = "ti-pruss/am65x-rtu0-prueth-fw.elf",
	},
	{
		.pru = "ti-pruss/am65x-pru1-prueth-fw.elf",
		.rtu = "ti-pruss/am65x-rtu1-prueth-fw.elf",
	}
};

static int prueth_emac_start(struct prueth *prueth, struct prueth_emac *emac)
{
	struct icssg_firmwares *firmwares;
	struct device *dev = prueth->dev;
	int slice, ret;

	firmwares = icssg_sr1_emac_firmwares;

	slice = prueth_emac_slice(emac);
	if (slice < 0) {
		netdev_err(emac->ndev, "invalid port\n");
		return -EINVAL;
	}

	icssg_config_sr1(prueth, emac, slice);

	ret = rproc_set_firmware(prueth->pru[slice], firmwares[slice].pru);
	ret = rproc_boot(prueth->pru[slice]);
	if (ret) {
		dev_err(dev, "failed to boot PRU%d: %d\n", slice, ret);
		return -EINVAL;
	}

	ret = rproc_set_firmware(prueth->rtu[slice], firmwares[slice].rtu);
	ret = rproc_boot(prueth->rtu[slice]);
	if (ret) {
		dev_err(dev, "failed to boot RTU%d: %d\n", slice, ret);
		goto halt_pru;
	}

	return 0;

halt_pru:
	rproc_shutdown(prueth->pru[slice]);

	return ret;
}

static void prueth_emac_stop(struct prueth_emac *emac)
{
	struct prueth *prueth = emac->prueth;
	int slice;

	switch (emac->port_id) {
	case PRUETH_PORT_MII0:
		slice = ICSS_SLICE0;
		break;
	case PRUETH_PORT_MII1:
		slice = ICSS_SLICE1;
		break;
	default:
		netdev_err(emac->ndev, "invalid port\n");
		return;
	}

	if (!emac->is_sr1)
		rproc_shutdown(prueth->txpru[slice]);
	rproc_shutdown(prueth->rtu[slice]);
	rproc_shutdown(prueth->pru[slice]);
}

/**
 * emac_ndo_open - EMAC device open
 * @ndev: network adapter device
 *
 * Called when system wants to start the interface.
 *
 * Return: 0 for a successful open, or appropriate error code
 */
static int emac_ndo_open(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	int num_data_chn = emac->tx_ch_num - 1;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);
	struct device *dev = prueth->dev;
	int max_rx_flows, rx_flow;
	int ret, i;

	/* clear SMEM and MSMC settings for all slices */
	if (!prueth->emacs_initialized) {
		memset_io(prueth->msmcram.va, 0, prueth->msmcram.size);
		memset_io(prueth->shram.va, 0, ICSSG_CONFIG_OFFSET_SLICE1 * PRUETH_NUM_MACS);
	}

	/* set h/w MAC as user might have re-configured */
	ether_addr_copy(emac->mac_addr, ndev->dev_addr);

	icssg_class_set_mac_addr(prueth->miig_rt, slice, emac->mac_addr);

	icssg_class_default(prueth->miig_rt, slice, 0, true);

	/* Notify the stack of the actual queue counts. */
	ret = netif_set_real_num_tx_queues(ndev, num_data_chn);
	if (ret) {
		dev_err(dev, "cannot set real number of tx queues\n");
		return ret;
	}

	init_completion(&emac->cmd_complete);
	ret = prueth_init_tx_chns(emac);
	if (ret) {
		dev_err(dev, "failed to init tx channel: %d\n", ret);
		return ret;
	}

	max_rx_flows = PRUETH_MAX_RX_FLOWS_SR1;
	ret = prueth_init_rx_chns(emac, &emac->rx_chns, "rx",
				  max_rx_flows, PRUETH_MAX_RX_DESC);
	if (ret) {
		dev_err(dev, "failed to init rx channel: %d\n", ret);
		goto cleanup_tx;
	}

	ret = prueth_init_rx_chns(emac, &emac->rx_mgm_chn, "rxmgm",
				  PRUETH_MAX_RX_MGM_FLOWS_SR1,
				  PRUETH_MAX_RX_MGM_DESC_SR1);
	if (ret) {
		dev_err(dev, "failed to init rx mgmt channel: %d\n",
			ret);
		goto cleanup_rx;
	}

	ret = prueth_ndev_add_tx_napi(emac);
	if (ret)
		goto cleanup_rx_mgm;

	/* we use only the highest priority flow for now i.e. @irq[3] */
	rx_flow = PRUETH_RX_FLOW_DATA_SR1;
	ret = request_irq(emac->rx_chns.irq[rx_flow], prueth_rx_irq,
			  IRQF_TRIGGER_HIGH, dev_name(dev), emac);
	if (ret) {
		dev_err(dev, "unable to request RX IRQ\n");
		goto cleanup_napi;
	}

	ret = request_threaded_irq(emac->rx_mgm_chn.irq[PRUETH_RX_MGM_FLOW_RESPONSE_SR1],
				   NULL, prueth_rx_mgm_rsp_thread,
				   IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
				   dev_name(dev), emac);
	if (ret) {
		dev_err(dev, "unable to request RX Management RSP IRQ\n");
		goto free_rx_irq;
	}

	ret = request_threaded_irq(emac->rx_mgm_chn.irq[PRUETH_RX_MGM_FLOW_TIMESTAMP_SR1],
				   NULL, prueth_rx_mgm_ts_thread_sr1,
				   IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
				   dev_name(dev), emac);
	if (ret) {
		dev_err(dev, "unable to request RX Management TS IRQ\n");
		goto free_rx_mgm_rsp_irq;
	}

	/* reset and start PRU firmware */
	ret = prueth_emac_start(prueth, emac);
	if (ret)
		goto free_rx_mgmt_ts_irq;

	icssg_mii_update_mtu(prueth->mii_rt, slice, ndev->max_mtu);

	/* Prepare RX */
	ret = prueth_prepare_rx_chan(emac, &emac->rx_chns, PRUETH_MAX_PKT_SIZE);
	if (ret)
		goto stop;

	ret = prueth_prepare_rx_chan(emac, &emac->rx_mgm_chn, 64);
	if (ret)
		goto reset_rx_chn;

	ret = k3_udma_glue_enable_rx_chn(emac->rx_mgm_chn.rx_chn);
	if (ret)
		goto reset_rx_chn;

	ret = k3_udma_glue_enable_rx_chn(emac->rx_chns.rx_chn);
	if (ret)
		goto reset_rx_mgm_chn;

	for (i = 0; i < emac->tx_ch_num; i++) {
		ret = k3_udma_glue_enable_tx_chn(emac->tx_chns[i].tx_chn);
		if (ret)
			goto reset_tx_chan;
	}

	/* Enable NAPI in Tx and Rx direction */
	for (i = 0; i < emac->tx_ch_num; i++)
		napi_enable(&emac->tx_chns[i].napi_tx);
	napi_enable(&emac->napi_rx);

	/* start PHY */
	phy_start(ndev->phydev);

	prueth->emacs_initialized++;

	queue_work(system_long_wq, &emac->stats_work.work);

	return 0;

reset_tx_chan:
	/* Since interface is not yet up, there is wouldn't be
	 * any SKB for completion. So set false to free_skb
	 */
	prueth_reset_tx_chan(emac, i, false);
reset_rx_mgm_chn:
	prueth_reset_rx_chan(&emac->rx_mgm_chn,
			     PRUETH_MAX_RX_MGM_FLOWS_SR1, true);
reset_rx_chn:
	prueth_reset_rx_chan(&emac->rx_chns, max_rx_flows, false);
stop:
	prueth_emac_stop(emac);
free_rx_mgmt_ts_irq:
	free_irq(emac->rx_mgm_chn.irq[PRUETH_RX_MGM_FLOW_TIMESTAMP_SR1],
		 emac);
free_rx_mgm_rsp_irq:
	free_irq(emac->rx_mgm_chn.irq[PRUETH_RX_MGM_FLOW_RESPONSE_SR1],
		 emac);
free_rx_irq:
	free_irq(emac->rx_chns.irq[rx_flow], emac);
cleanup_napi:
	prueth_ndev_del_tx_napi(emac, emac->tx_ch_num);
cleanup_rx_mgm:
	prueth_cleanup_rx_chns(emac, &emac->rx_mgm_chn,
			       PRUETH_MAX_RX_MGM_FLOWS_SR1);
cleanup_rx:
	prueth_cleanup_rx_chns(emac, &emac->rx_chns, max_rx_flows);
cleanup_tx:
	prueth_cleanup_tx_chns(emac);

	return ret;
}

/**
 * emac_ndo_stop - EMAC device stop
 * @ndev: network adapter device
 *
 * Called when system wants to stop or down the interface.
 *
 * Return: Always 0 (Success)
 */
static int emac_ndo_stop(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	int rx_flow = PRUETH_RX_FLOW_DATA_SR1;
	struct prueth *prueth = emac->prueth;
	int max_rx_flows;
	int ret, i;

	/* inform the upper layers. */
	netif_tx_stop_all_queues(ndev);

	/* block packets from wire */
	if (ndev->phydev)
		phy_stop(ndev->phydev);

	icssg_class_disable(prueth->miig_rt, prueth_emac_slice(emac));

	emac_send_command_sr1(emac, ICSSG_SHUTDOWN_CMD_SR1);

	atomic_set(&emac->tdown_cnt, emac->tx_ch_num);
	/* ensure new tdown_cnt value is visible */
	smp_mb__after_atomic();
	/* tear down and disable UDMA channels */
	reinit_completion(&emac->tdown_complete);
	for (i = 0; i < emac->tx_ch_num; i++)
		k3_udma_glue_tdown_tx_chn(emac->tx_chns[i].tx_chn, false);

	ret = wait_for_completion_timeout(&emac->tdown_complete,
					  msecs_to_jiffies(1000));
	if (!ret)
		netdev_err(ndev, "tx teardown timeout\n");

	prueth_reset_tx_chan(emac, emac->tx_ch_num, true);
	for (i = 0; i < emac->tx_ch_num; i++)
		napi_disable(&emac->tx_chns[i].napi_tx);

	max_rx_flows = PRUETH_MAX_RX_FLOWS_SR1;
	k3_udma_glue_tdown_rx_chn(emac->rx_chns.rx_chn, true);

	prueth_reset_rx_chan(&emac->rx_chns, max_rx_flows, true);
	/* Teardown RX MGM channel */
	k3_udma_glue_tdown_rx_chn(emac->rx_mgm_chn.rx_chn, true);
	prueth_reset_rx_chan(&emac->rx_mgm_chn,
			     PRUETH_MAX_RX_MGM_FLOWS_SR1, true);

	napi_disable(&emac->napi_rx);

	/* Destroying the queued work in ndo_stop() */
	cancel_delayed_work_sync(&emac->stats_work);

	/* stop PRUs */
	prueth_emac_stop(emac);

	free_irq(emac->rx_mgm_chn.irq[PRUETH_RX_MGM_FLOW_TIMESTAMP_SR1], emac);
	free_irq(emac->rx_mgm_chn.irq[PRUETH_RX_MGM_FLOW_RESPONSE_SR1], emac);
	free_irq(emac->rx_chns.irq[rx_flow], emac);
	prueth_ndev_del_tx_napi(emac, emac->tx_ch_num);
	prueth_cleanup_tx_chns(emac);

	prueth_cleanup_rx_chns(emac, &emac->rx_mgm_chn, PRUETH_MAX_RX_MGM_FLOWS_SR1);
	prueth_cleanup_rx_chns(emac, &emac->rx_chns, max_rx_flows);

	prueth->emacs_initialized--;

	return 0;
}

static void emac_ndo_set_rx_mode_sr1(struct net_device *ndev)
{
	struct prueth_emac *emac = netdev_priv(ndev);
	bool allmulti = ndev->flags & IFF_ALLMULTI;
	bool promisc = ndev->flags & IFF_PROMISC;
	struct prueth *prueth = emac->prueth;
	int slice = prueth_emac_slice(emac);

	if (promisc) {
		icssg_class_promiscuous_sr1(prueth->miig_rt, slice);
		return;
	}

	if (allmulti) {
		icssg_class_default(prueth->miig_rt, slice, 1, true);
		return;
	}

	icssg_class_default(prueth->miig_rt, slice, 0, true);
	if (!netdev_mc_empty(ndev)) {
		/* program multicast address list into Classifier */
		icssg_class_add_mcast_sr1(prueth->miig_rt, slice, ndev);
	}
}

static const struct net_device_ops emac_netdev_ops = {
	.ndo_open = emac_ndo_open,
	.ndo_stop = emac_ndo_stop,
	.ndo_start_xmit = icssg_ndo_start_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_tx_timeout = icssg_ndo_tx_timeout,
	.ndo_set_rx_mode = emac_ndo_set_rx_mode_sr1,
	.ndo_eth_ioctl = icssg_ndo_ioctl,
	.ndo_get_stats64 = icssg_ndo_get_stats64,
	.ndo_get_phys_port_name = icssg_ndo_get_phys_port_name,
};

static int prueth_netdev_init(struct prueth *prueth,
			      struct device_node *eth_node)
{
	struct prueth_emac *emac;
	struct net_device *ndev;
	enum prueth_port port;
	enum prueth_mac mac;
	/* Only enable one TX channel due to timeouts when
	 * using multiple channels */
	int num_tx_chn = 1;
	int ret;

	port = prueth_node_port(eth_node);
	if (port == PRUETH_PORT_INVALID)
		return -EINVAL;

	mac = prueth_node_mac(eth_node);
	if (mac == PRUETH_MAC_INVALID)
		return -EINVAL;

	ndev = alloc_etherdev_mq(sizeof(*emac), num_tx_chn);
	if (!ndev)
		return -ENOMEM;

	emac = netdev_priv(ndev);
	emac->is_sr1 = 1;
	emac->prueth = prueth;
	emac->ndev = ndev;
	emac->port_id = port;
	emac->cmd_wq = create_singlethread_workqueue("icssg_cmd_wq");
	if (!emac->cmd_wq) {
		ret = -ENOMEM;
		goto free_ndev;
	}

	INIT_DELAYED_WORK(&emac->stats_work, icssg_stats_work_handler);

	ret = pruss_request_mem_region(prueth->pruss,
				       port == PRUETH_PORT_MII0 ?
				       PRUSS_MEM_DRAM0 : PRUSS_MEM_DRAM1,
				       &emac->dram);
	if (ret) {
		dev_err(prueth->dev, "unable to get DRAM: %d\n", ret);
		ret = -ENOMEM;
		goto free_wq;
	}

	/* SR1.0 uses a dedicated high priority channel
	 * to send commands to the firmware
	 */
	emac->tx_ch_num = 2;

	SET_NETDEV_DEV(ndev, prueth->dev);
	spin_lock_init(&emac->lock);
	mutex_init(&emac->cmd_lock);

	emac->phy_node = of_parse_phandle(eth_node, "phy-handle", 0);
	if (!emac->phy_node && !of_phy_is_fixed_link(eth_node)) {
		dev_err(prueth->dev, "couldn't find phy-handle\n");
		ret = -ENODEV;
		goto free;
	} else if (of_phy_is_fixed_link(eth_node)) {
		ret = of_phy_register_fixed_link(eth_node);
		if (ret) {
			ret = dev_err_probe(prueth->dev, ret,
					    "failed to register fixed-link phy\n");
			goto free;
		}

		emac->phy_node = eth_node;
	}

	ret = of_get_phy_mode(eth_node, &emac->phy_if);
	if (ret) {
		dev_err(prueth->dev, "could not get phy-mode property\n");
		goto free;
	}

	if (emac->phy_if != PHY_INTERFACE_MODE_MII &&
	    !phy_interface_mode_is_rgmii(emac->phy_if)) {
		dev_err(prueth->dev, "PHY mode unsupported %s\n", phy_modes(emac->phy_if));
		ret = -EINVAL;
		goto free;
	}

	/* AM65 SR2.0 has TX Internal delay always enabled by hardware
	 * and it is not possible to disable TX Internal delay. The below
	 * switch case block describes how we handle different phy modes
	 * based on hardware restriction.
	 */
	switch (emac->phy_if) {
	case PHY_INTERFACE_MODE_RGMII_ID:
		emac->phy_if = PHY_INTERFACE_MODE_RGMII_RXID;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		emac->phy_if = PHY_INTERFACE_MODE_RGMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		dev_err(prueth->dev, "RGMII mode without TX delay is not supported");
		ret = -EINVAL;
		goto free;
	default:
		break;
	}

	/* get mac address from DT and set private and netdev addr */
	ret = of_get_ethdev_address(eth_node, ndev);
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		eth_hw_addr_random(ndev);
		dev_warn(prueth->dev, "port %d: using random MAC addr: %pM\n",
			 port, ndev->dev_addr);
	}
	ether_addr_copy(emac->mac_addr, ndev->dev_addr);

	ndev->dev.of_node = eth_node;
	ndev->min_mtu = PRUETH_MIN_PKT_SIZE;
	ndev->max_mtu = PRUETH_MAX_MTU;
	ndev->netdev_ops = &emac_netdev_ops;
	ndev->ethtool_ops = &icssg_ethtool_ops;
	ndev->hw_features = NETIF_F_SG;
	ndev->features = ndev->hw_features;

	netif_napi_add(ndev, &emac->napi_rx, icssg_napi_rx_poll);
	prueth->emac[mac] = emac;

	return 0;

free:
	pruss_release_mem_region(prueth->pruss, &emac->dram);
free_wq:
	destroy_workqueue(emac->cmd_wq);
free_ndev:
	emac->ndev = NULL;
	prueth->emac[mac] = NULL;
	free_netdev(ndev);

	return ret;
}

static int prueth_probe(struct platform_device *pdev)
{
	struct device_node *eth_node, *eth_ports_node;
	struct device_node  *eth0_node = NULL;
	struct device_node  *eth1_node = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct prueth *prueth;
	struct pruss *pruss;
	u32 msmc_ram_size;
	int i, ret;

	np = dev->of_node;

	prueth = devm_kzalloc(dev, sizeof(*prueth), GFP_KERNEL);
	if (!prueth)
		return -ENOMEM;

	dev_set_drvdata(dev, prueth);
	prueth->pdev = pdev;
	prueth->pdata = *(const struct prueth_pdata *)device_get_match_data(dev);

	prueth->dev = dev;
	eth_ports_node = of_get_child_by_name(np, "ethernet-ports");
	if (!eth_ports_node)
		return -ENOENT;

	for_each_child_of_node(eth_ports_node, eth_node) {
		u32 reg;

		if (strcmp(eth_node->name, "port"))
			continue;
		ret = of_property_read_u32(eth_node, "reg", &reg);
		if (ret < 0) {
			dev_err(dev, "%pOF error reading port_id %d\n",
				eth_node, ret);
		}

		of_node_get(eth_node);

		if (reg == 0) {
			eth0_node = eth_node;
			if (!of_device_is_available(eth0_node)) {
				of_node_put(eth0_node);
				eth0_node = NULL;
			}
		} else if (reg == 1) {
			eth1_node = eth_node;
			if (!of_device_is_available(eth1_node)) {
				of_node_put(eth1_node);
				eth1_node = NULL;
			}
		} else {
			dev_err(dev, "port reg should be 0 or 1\n");
		}
	}

	of_node_put(eth_ports_node);

	/* At least one node must be present and available else we fail */
	if (!eth0_node && !eth1_node) {
		dev_err(dev, "neither port0 nor port1 node available\n");
		return -ENODEV;
	}

	if (eth0_node == eth1_node) {
		dev_err(dev, "port0 and port1 can't have same reg\n");
		of_node_put(eth0_node);
		return -ENODEV;
	}

	prueth->eth_node[PRUETH_MAC0] = eth0_node;
	prueth->eth_node[PRUETH_MAC1] = eth1_node;

	prueth->miig_rt = syscon_regmap_lookup_by_phandle(np, "ti,mii-g-rt");
	if (IS_ERR(prueth->miig_rt)) {
		dev_err(dev, "couldn't get ti,mii-g-rt syscon regmap\n");
		return -ENODEV;
	}

	prueth->mii_rt = syscon_regmap_lookup_by_phandle(np, "ti,mii-rt");
	if (IS_ERR(prueth->mii_rt)) {
		dev_err(dev, "couldn't get ti,mii-rt syscon regmap\n");
		return -ENODEV;
	}

	if (eth0_node) {
		ret = prueth_get_cores(prueth, ICSS_SLICE0, true);
		if (ret)
			goto put_cores;
	}

	if (eth1_node) {
		ret = prueth_get_cores(prueth, ICSS_SLICE1, true);
		if (ret)
			goto put_cores;
	}

	pruss = pruss_get(eth0_node ?
			  prueth->pru[ICSS_SLICE0] : prueth->pru[ICSS_SLICE1]);
	if (IS_ERR(pruss)) {
		ret = PTR_ERR(pruss);
		dev_err(dev, "unable to get pruss handle\n");
		goto put_cores;
	}

	prueth->pruss = pruss;

	ret = pruss_request_mem_region(pruss, PRUSS_MEM_SHRD_RAM2,
				       &prueth->shram);
	if (ret) {
		dev_err(dev, "unable to get PRUSS SHRD RAM2: %d\n", ret);
		goto put_pruss;
	}

	prueth->sram_pool = of_gen_pool_get(np, "sram", 0);
	if (!prueth->sram_pool) {
		dev_err(dev, "unable to get SRAM pool\n");
		ret = -ENODEV;

		goto put_mem;
	}

	msmc_ram_size = MSMC_RAM_SIZE_SR1;

	prueth->msmcram.va = (void __iomem *)gen_pool_alloc(prueth->sram_pool,
							    msmc_ram_size);

	if (!prueth->msmcram.va) {
		ret = -ENOMEM;
		dev_err(dev, "unable to allocate MSMC resource\n");
		goto put_mem;
	}
	prueth->msmcram.pa = gen_pool_virt_to_phys(prueth->sram_pool,
						   (unsigned long)prueth->msmcram.va);
	prueth->msmcram.size = msmc_ram_size;
	memset_io(prueth->msmcram.va, 0, msmc_ram_size);
	dev_dbg(dev, "sram: pa %llx va %p size %zx\n", prueth->msmcram.pa,
		prueth->msmcram.va, prueth->msmcram.size);

	prueth->iep0 = icss_iep_get_idx(np, 0);
	if (IS_ERR(prueth->iep0)) {
		ret = dev_err_probe(dev, PTR_ERR(prueth->iep0),
				    "iep0 get failed\n");
		goto free_pool;
	}

	prueth->iep1 = icss_iep_get_idx(np, 1);
	if (IS_ERR(prueth->iep1)) {
		ret = dev_err_probe(dev, PTR_ERR(prueth->iep1),
				    "iep1 get failed\n");
		goto put_iep0;
	}

	ret = icss_iep_init(prueth->iep0, NULL, NULL, 0);
	if (ret) {
		dev_err_probe(dev, ret, "failed to init iep0\n");
		goto put_iep;
	}

	ret = icss_iep_init(prueth->iep1, NULL, NULL, 0);
	if (ret) {
		dev_err_probe(dev, ret, "failed to init iep1\n");
		goto exit_iep0;
	}

	if (eth0_node) {
		ret = prueth_netdev_init(prueth, eth0_node);
		if (ret) {
			dev_err_probe(dev, ret, "netdev init %s failed\n",
				      eth0_node->name);
			goto exit_iep;
		}

		prueth->emac[PRUETH_MAC0]->half_duplex =
			of_property_read_bool(eth0_node, "ti,half-duplex-capable");

		prueth->emac[PRUETH_MAC0]->iep = prueth->iep0;
	}

	if (eth1_node) {
		ret = prueth_netdev_init(prueth, eth1_node);
		if (ret) {
			dev_err_probe(dev, ret, "netdev init %s failed\n",
				      eth1_node->name);
			goto netdev_exit;
		}

		prueth->emac[PRUETH_MAC1]->half_duplex =
			of_property_read_bool(eth1_node, "ti,half-duplex-capable");

		prueth->emac[PRUETH_MAC1]->iep = prueth->iep1;
	}

	/* register the network devices */
	if (eth0_node) {
		ret = register_netdev(prueth->emac[PRUETH_MAC0]->ndev);
		if (ret) {
			dev_err(dev, "can't register netdev for port MII0\n");
			goto netdev_exit;
		}

		prueth->registered_netdevs[PRUETH_MAC0] = prueth->emac[PRUETH_MAC0]->ndev;
		emac_phy_connect(prueth->emac[PRUETH_MAC0]);
		phy_attached_info(prueth->emac[PRUETH_MAC0]->ndev->phydev);
	}

	if (eth1_node) {
		ret = register_netdev(prueth->emac[PRUETH_MAC1]->ndev);
		if (ret) {
			dev_err(dev, "can't register netdev for port MII1\n");
			goto netdev_unregister;
		}

		prueth->registered_netdevs[PRUETH_MAC1] = prueth->emac[PRUETH_MAC1]->ndev;
		emac_phy_connect(prueth->emac[PRUETH_MAC1]);
		phy_attached_info(prueth->emac[PRUETH_MAC1]->ndev->phydev);
	}

	dev_info(dev, "TI PRU SR1.0 ethernet driver initialized: %s EMAC mode\n",
		 (!eth0_node || !eth1_node) ? "single" : "dual");

	if (eth1_node)
		of_node_put(eth1_node);
	if (eth0_node)
		of_node_put(eth0_node);

	return 0;

netdev_unregister:
	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		if (!prueth->registered_netdevs[i])
			continue;

		if (prueth->emac[i]->ndev->phydev) {
			phy_disconnect(prueth->emac[i]->ndev->phydev);
			prueth->emac[i]->ndev->phydev = NULL;
		}
		unregister_netdev(prueth->registered_netdevs[i]);
	}

netdev_exit:
	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		eth_node = prueth->eth_node[i];
		if (!eth_node)
			continue;

		prueth_netdev_exit(prueth, eth_node);
	}

exit_iep:
	icss_iep_exit(prueth->iep1);
exit_iep0:
	icss_iep_exit(prueth->iep0);

put_iep:
	icss_iep_put(prueth->iep1);

put_iep0:
	icss_iep_put(prueth->iep0);
	prueth->iep0 = NULL;
	prueth->iep1 = NULL;

free_pool:
	gen_pool_free(prueth->sram_pool,
		      (unsigned long)prueth->msmcram.va, msmc_ram_size);

put_mem:
	pruss_release_mem_region(prueth->pruss, &prueth->shram);

put_pruss:
	pruss_put(prueth->pruss);

put_cores:
	if (eth1_node) {
		prueth_put_cores(prueth, ICSS_SLICE1);
		of_node_put(eth1_node);
	}

	if (eth0_node) {
		prueth_put_cores(prueth, ICSS_SLICE0);
		of_node_put(eth0_node);
	}

	return ret;
}

static void prueth_remove(struct platform_device *pdev)
{
	struct prueth *prueth = platform_get_drvdata(pdev);
	struct device_node *eth_node;
	int i;

	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		if (!prueth->registered_netdevs[i])
			continue;
		phy_stop(prueth->emac[i]->ndev->phydev);
		phy_disconnect(prueth->emac[i]->ndev->phydev);
		prueth->emac[i]->ndev->phydev = NULL;
		unregister_netdev(prueth->registered_netdevs[i]);
	}

	for (i = 0; i < PRUETH_NUM_MACS; i++) {
		eth_node = prueth->eth_node[i];
		if (!eth_node)
			continue;

		prueth_netdev_exit(prueth, eth_node);
	}

	icss_iep_exit(prueth->iep1);
	icss_iep_exit(prueth->iep0);

	icss_iep_put(prueth->iep1);
	icss_iep_put(prueth->iep0);

	gen_pool_free(prueth->sram_pool,
		      (unsigned long)prueth->msmcram.va,
		      MSMC_RAM_SIZE_SR1);

	pruss_release_mem_region(prueth->pruss, &prueth->shram);

	pruss_put(prueth->pruss);

	if (prueth->eth_node[PRUETH_MAC1])
		prueth_put_cores(prueth, ICSS_SLICE1);

	if (prueth->eth_node[PRUETH_MAC0])
		prueth_put_cores(prueth, ICSS_SLICE0);
}

static const struct prueth_pdata am654_sr1_icssg_pdata = {
	.fdqring_mode = K3_RINGACC_RING_MODE_MESSAGE,
};

static const struct of_device_id prueth_dt_match[] = {
	{ .compatible = "ti,am654-sr1-icssg-prueth", .data = &am654_sr1_icssg_pdata },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, prueth_dt_match);

static struct platform_driver prueth_driver = {
	.probe = prueth_probe,
	.remove = prueth_remove,
	.driver = {
		.name = "icssg-prueth-sr1",
		.of_match_table = prueth_dt_match,
		.pm = &prueth_dev_pm_ops,
	},
};
module_platform_driver(prueth_driver);

MODULE_AUTHOR("Roger Quadros <rogerq@ti.com>");
MODULE_AUTHOR("Md Danish Anwar <danishanwar@ti.com>");
MODULE_AUTHOR("Diogo Ivo <diogo.ivo@siemens.com>");
MODULE_DESCRIPTION(PRUETH_MODULE_DESCRIPTION);
MODULE_LICENSE("GPL");
