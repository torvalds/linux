/*
 * Copyright(c) 2015 EZchip Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 */

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include "nps_enet.h"

#define DRV_NAME			"nps_mgt_enet"

static void nps_enet_clean_rx_fifo(struct net_device *ndev, u32 frame_len)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	u32 i, len = DIV_ROUND_UP(frame_len, sizeof(u32));

	/* Empty Rx FIFO buffer by reading all words */
	for (i = 0; i < len; i++)
		nps_enet_reg_get(priv, NPS_ENET_REG_RX_BUF);
}

static void nps_enet_read_rx_fifo(struct net_device *ndev,
				  unsigned char *dst, u32 length)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	s32 i, last = length & (sizeof(u32) - 1);
	u32 *reg = (u32 *)dst, len = length / sizeof(u32);
	bool dst_is_aligned = IS_ALIGNED((unsigned long)dst, sizeof(u32));

	/* In case dst is not aligned we need an intermediate buffer */
	if (dst_is_aligned)
		for (i = 0; i < len; i++, reg++)
			*reg = nps_enet_reg_get(priv, NPS_ENET_REG_RX_BUF);
	else { /* !dst_is_aligned */
		for (i = 0; i < len; i++, reg++) {
			u32 buf =
				nps_enet_reg_get(priv, NPS_ENET_REG_RX_BUF);

			/* to accommodate word-unaligned address of "reg"
			 * we have to do memcpy_toio() instead of simple "=".
			 */
			memcpy_toio((void __iomem *)reg, &buf, sizeof(buf));
		}
	}

	/* copy last bytes (if any) */
	if (last) {
		u32 buf = nps_enet_reg_get(priv, NPS_ENET_REG_RX_BUF);

		memcpy_toio((void __iomem *)reg, &buf, last);
	}
}

static u32 nps_enet_rx_handler(struct net_device *ndev)
{
	u32 frame_len, err = 0;
	u32 work_done = 0;
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct sk_buff *skb;
	struct nps_enet_rx_ctl rx_ctrl;

	rx_ctrl.value = nps_enet_reg_get(priv, NPS_ENET_REG_RX_CTL);
	frame_len = rx_ctrl.nr;

	/* Check if we got RX */
	if (!rx_ctrl.cr)
		return work_done;

	/* If we got here there is a work for us */
	work_done++;

	/* Check Rx error */
	if (rx_ctrl.er) {
		ndev->stats.rx_errors++;
		err = 1;
	}

	/* Check Rx CRC error */
	if (rx_ctrl.crc) {
		ndev->stats.rx_crc_errors++;
		ndev->stats.rx_dropped++;
		err = 1;
	}

	/* Check Frame length Min 64b */
	if (unlikely(frame_len < ETH_ZLEN)) {
		ndev->stats.rx_length_errors++;
		ndev->stats.rx_dropped++;
		err = 1;
	}

	if (err)
		goto rx_irq_clean;

	/* Skb allocation */
	skb = netdev_alloc_skb_ip_align(ndev, frame_len);
	if (unlikely(!skb)) {
		ndev->stats.rx_errors++;
		ndev->stats.rx_dropped++;
		goto rx_irq_clean;
	}

	/* Copy frame from Rx fifo into the skb */
	nps_enet_read_rx_fifo(ndev, skb->data, frame_len);

	skb_put(skb, frame_len);
	skb->protocol = eth_type_trans(skb, ndev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += frame_len;
	netif_receive_skb(skb);

	goto rx_irq_frame_done;

rx_irq_clean:
	/* Clean Rx fifo */
	nps_enet_clean_rx_fifo(ndev, frame_len);

rx_irq_frame_done:
	/* Ack Rx ctrl register */
	nps_enet_reg_set(priv, NPS_ENET_REG_RX_CTL, 0);

	return work_done;
}

static void nps_enet_tx_handler(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_tx_ctl tx_ctrl;

	tx_ctrl.value = nps_enet_reg_get(priv, NPS_ENET_REG_TX_CTL);

	/* Check if we got TX */
	if (!priv->tx_packet_sent || tx_ctrl.ct)
		return;

	/* Check Tx transmit error */
	if (unlikely(tx_ctrl.et)) {
		ndev->stats.tx_errors++;
	} else {
		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += tx_ctrl.nt;
	}

	if (priv->tx_skb) {
		dev_kfree_skb(priv->tx_skb);
		priv->tx_skb = NULL;
	}

	priv->tx_packet_sent = false;

	if (netif_queue_stopped(ndev))
		netif_wake_queue(ndev);
}

/**
 * nps_enet_poll - NAPI poll handler.
 * @napi:       Pointer to napi_struct structure.
 * @budget:     How many frames to process on one call.
 *
 * returns:     Number of processed frames
 */
static int nps_enet_poll(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_buf_int_enable buf_int_enable;
	u32 work_done;

	buf_int_enable.rx_rdy = NPS_ENET_ENABLE;
	buf_int_enable.tx_done = NPS_ENET_ENABLE;
	nps_enet_tx_handler(ndev);
	work_done = nps_enet_rx_handler(ndev);
	if (work_done < budget) {
		napi_complete(napi);
		nps_enet_reg_set(priv, NPS_ENET_REG_BUF_INT_ENABLE,
				 buf_int_enable.value);
	}

	return work_done;
}

/**
 * nps_enet_irq_handler - Global interrupt handler for ENET.
 * @irq:                irq number.
 * @dev_instance:       device instance.
 *
 * returns: IRQ_HANDLED for all cases.
 *
 * EZchip ENET has 2 interrupt causes, and depending on bits raised in
 * CTRL registers we may tell what is a reason for interrupt to fire up.
 * We got one for RX and the other for TX (completion).
 */
static irqreturn_t nps_enet_irq_handler(s32 irq, void *dev_instance)
{
	struct net_device *ndev = dev_instance;
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_buf_int_cause buf_int_cause;

	buf_int_cause.value =
			nps_enet_reg_get(priv, NPS_ENET_REG_BUF_INT_CAUSE);

	if (buf_int_cause.tx_done || buf_int_cause.rx_rdy)
		if (likely(napi_schedule_prep(&priv->napi))) {
			nps_enet_reg_set(priv, NPS_ENET_REG_BUF_INT_ENABLE, 0);
			__napi_schedule(&priv->napi);
		}

	return IRQ_HANDLED;
}

static void nps_enet_set_hw_mac_address(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_ge_mac_cfg_1 ge_mac_cfg_1;
	struct nps_enet_ge_mac_cfg_2 *ge_mac_cfg_2 = &priv->ge_mac_cfg_2;

	/* set MAC address in HW */
	ge_mac_cfg_1.octet_0 = ndev->dev_addr[0];
	ge_mac_cfg_1.octet_1 = ndev->dev_addr[1];
	ge_mac_cfg_1.octet_2 = ndev->dev_addr[2];
	ge_mac_cfg_1.octet_3 = ndev->dev_addr[3];
	ge_mac_cfg_2->octet_4 = ndev->dev_addr[4];
	ge_mac_cfg_2->octet_5 = ndev->dev_addr[5];

	nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_1,
			 ge_mac_cfg_1.value);

	nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_2,
			 ge_mac_cfg_2->value);
}

/**
 * nps_enet_hw_reset - Reset the network device.
 * @ndev:       Pointer to the network device.
 *
 * This function reset the PCS and TX fifo.
 * The programming model is to set the relevant reset bits
 * wait for some time for this to propagate and then unset
 * the reset bits. This way we ensure that reset procedure
 * is done successfully by device.
 */
static void nps_enet_hw_reset(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_ge_rst ge_rst;
	struct nps_enet_phase_fifo_ctl phase_fifo_ctl;

	ge_rst.value = 0;
	phase_fifo_ctl.value = 0;
	/* Pcs reset sequence*/
	ge_rst.gmac_0 = NPS_ENET_ENABLE;
	nps_enet_reg_set(priv, NPS_ENET_REG_GE_RST, ge_rst.value);
	usleep_range(10, 20);
	ge_rst.value = 0;
	nps_enet_reg_set(priv, NPS_ENET_REG_GE_RST, ge_rst.value);

	/* Tx fifo reset sequence */
	phase_fifo_ctl.rst = NPS_ENET_ENABLE;
	phase_fifo_ctl.init = NPS_ENET_ENABLE;
	nps_enet_reg_set(priv, NPS_ENET_REG_PHASE_FIFO_CTL,
			 phase_fifo_ctl.value);
	usleep_range(10, 20);
	phase_fifo_ctl.value = 0;
	nps_enet_reg_set(priv, NPS_ENET_REG_PHASE_FIFO_CTL,
			 phase_fifo_ctl.value);
}

static void nps_enet_hw_enable_control(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_ge_mac_cfg_0 ge_mac_cfg_0;
	struct nps_enet_buf_int_enable buf_int_enable;
	struct nps_enet_ge_mac_cfg_2 *ge_mac_cfg_2 = &priv->ge_mac_cfg_2;
	struct nps_enet_ge_mac_cfg_3 *ge_mac_cfg_3 = &priv->ge_mac_cfg_3;
	s32 max_frame_length;

	ge_mac_cfg_0.value = 0;
	buf_int_enable.value = 0;
	/* Enable Rx and Tx statistics */
	ge_mac_cfg_2->stat_en = NPS_ENET_GE_MAC_CFG_2_STAT_EN;

	/* Discard packets with different MAC address */
	ge_mac_cfg_2->disc_da = NPS_ENET_ENABLE;

	/* Discard multicast packets */
	ge_mac_cfg_2->disc_mc = NPS_ENET_ENABLE;

	nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_2,
			 ge_mac_cfg_2->value);

	/* Discard Packets bigger than max frame length */
	max_frame_length = ETH_HLEN + ndev->mtu + ETH_FCS_LEN;
	if (max_frame_length <= NPS_ENET_MAX_FRAME_LENGTH) {
		ge_mac_cfg_3->max_len = max_frame_length;
		nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_3,
				 ge_mac_cfg_3->value);
	}

	/* Enable interrupts */
	buf_int_enable.rx_rdy = NPS_ENET_ENABLE;
	buf_int_enable.tx_done = NPS_ENET_ENABLE;
	nps_enet_reg_set(priv, NPS_ENET_REG_BUF_INT_ENABLE,
			 buf_int_enable.value);

	/* Write device MAC address to HW */
	nps_enet_set_hw_mac_address(ndev);

	/* Rx and Tx HW features */
	ge_mac_cfg_0.tx_pad_en = NPS_ENET_ENABLE;
	ge_mac_cfg_0.tx_crc_en = NPS_ENET_ENABLE;
	ge_mac_cfg_0.rx_crc_strip = NPS_ENET_ENABLE;

	/* IFG configuration */
	ge_mac_cfg_0.rx_ifg = NPS_ENET_GE_MAC_CFG_0_RX_IFG;
	ge_mac_cfg_0.tx_ifg = NPS_ENET_GE_MAC_CFG_0_TX_IFG;

	/* preamble configuration */
	ge_mac_cfg_0.rx_pr_check_en = NPS_ENET_ENABLE;
	ge_mac_cfg_0.tx_pr_len = NPS_ENET_GE_MAC_CFG_0_TX_PR_LEN;

	/* enable flow control frames */
	ge_mac_cfg_0.tx_fc_en = NPS_ENET_ENABLE;
	ge_mac_cfg_0.rx_fc_en = NPS_ENET_ENABLE;
	ge_mac_cfg_0.tx_fc_retr = NPS_ENET_GE_MAC_CFG_0_TX_FC_RETR;

	/* Enable Rx and Tx */
	ge_mac_cfg_0.rx_en = NPS_ENET_ENABLE;
	ge_mac_cfg_0.tx_en = NPS_ENET_ENABLE;

	nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_0,
			 ge_mac_cfg_0.value);
}

static void nps_enet_hw_disable_control(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);

	/* Disable interrupts */
	nps_enet_reg_set(priv, NPS_ENET_REG_BUF_INT_ENABLE, 0);

	/* Disable Rx and Tx */
	nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_0, 0);
}

static void nps_enet_send_frame(struct net_device *ndev,
				struct sk_buff *skb)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_tx_ctl tx_ctrl;
	short length = skb->len;
	u32 i, len = DIV_ROUND_UP(length, sizeof(u32));
	u32 *src = (u32 *)virt_to_phys(skb->data);
	bool src_is_aligned = IS_ALIGNED((unsigned long)src, sizeof(u32));

	tx_ctrl.value = 0;
	/* In case src is not aligned we need an intermediate buffer */
	if (src_is_aligned)
		for (i = 0; i < len; i++, src++)
			nps_enet_reg_set(priv, NPS_ENET_REG_TX_BUF, *src);
	else { /* !src_is_aligned */
		for (i = 0; i < len; i++, src++) {
			u32 buf;

			/* to accommodate word-unaligned address of "src"
			 * we have to do memcpy_fromio() instead of simple "="
			 */
			memcpy_fromio(&buf, (void __iomem *)src, sizeof(buf));
			nps_enet_reg_set(priv, NPS_ENET_REG_TX_BUF, buf);
		}
	}
	/* Write the length of the Frame */
	tx_ctrl.nt = length;

	/* Indicate SW is done */
	priv->tx_packet_sent = true;
	tx_ctrl.ct = NPS_ENET_ENABLE;

	/* Send Frame */
	nps_enet_reg_set(priv, NPS_ENET_REG_TX_CTL, tx_ctrl.value);
}

/**
 * nps_enet_set_mac_address - Set the MAC address for this device.
 * @ndev:       Pointer to net_device structure.
 * @p:          6 byte Address to be written as MAC address.
 *
 * This function copies the HW address from the sockaddr structure to the
 * net_device structure and updates the address in HW.
 *
 * returns:     -EBUSY if the net device is busy or 0 if the address is set
 *              successfully.
 */
static s32 nps_enet_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;
	s32 res;

	if (netif_running(ndev))
		return -EBUSY;

	res = eth_mac_addr(ndev, p);
	if (!res) {
		ether_addr_copy(ndev->dev_addr, addr->sa_data);
		nps_enet_set_hw_mac_address(ndev);
	}

	return res;
}

/**
 * nps_enet_set_rx_mode - Change the receive filtering mode.
 * @ndev:       Pointer to the network device.
 *
 * This function enables/disables promiscuous mode
 */
static void nps_enet_set_rx_mode(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	struct nps_enet_ge_mac_cfg_2 ge_mac_cfg_2;

	ge_mac_cfg_2.value = priv->ge_mac_cfg_2.value;

	if (ndev->flags & IFF_PROMISC) {
		ge_mac_cfg_2.disc_da = NPS_ENET_DISABLE;
		ge_mac_cfg_2.disc_mc = NPS_ENET_DISABLE;
	} else {
		ge_mac_cfg_2.disc_da = NPS_ENET_ENABLE;
		ge_mac_cfg_2.disc_mc = NPS_ENET_ENABLE;
	}

	nps_enet_reg_set(priv, NPS_ENET_REG_GE_MAC_CFG_2, ge_mac_cfg_2.value);
}

/**
 * nps_enet_open - Open the network device.
 * @ndev:       Pointer to the network device.
 *
 * returns: 0, on success or non-zero error value on failure.
 *
 * This function sets the MAC address, requests and enables an IRQ
 * for the ENET device and starts the Tx queue.
 */
static s32 nps_enet_open(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);
	s32 err;

	/* Reset private variables */
	priv->tx_packet_sent = false;
	priv->ge_mac_cfg_2.value = 0;
	priv->ge_mac_cfg_3.value = 0;

	/* ge_mac_cfg_3 default values */
	priv->ge_mac_cfg_3.rx_ifg_th = NPS_ENET_GE_MAC_CFG_3_RX_IFG_TH;
	priv->ge_mac_cfg_3.max_len = NPS_ENET_GE_MAC_CFG_3_MAX_LEN;

	/* Disable HW device */
	nps_enet_hw_disable_control(ndev);

	/* irq Rx allocation */
	err = request_irq(priv->irq, nps_enet_irq_handler,
			  0, "enet-rx-tx", ndev);
	if (err)
		return err;

	napi_enable(&priv->napi);

	/* Enable HW device */
	nps_enet_hw_reset(ndev);
	nps_enet_hw_enable_control(ndev);

	netif_start_queue(ndev);

	return 0;
}

/**
 * nps_enet_stop - Close the network device.
 * @ndev:       Pointer to the network device.
 *
 * This function stops the Tx queue, disables interrupts for the ENET device.
 */
static s32 nps_enet_stop(struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);

	napi_disable(&priv->napi);
	netif_stop_queue(ndev);
	nps_enet_hw_disable_control(ndev);
	free_irq(priv->irq, ndev);

	return 0;
}

/**
 * nps_enet_start_xmit - Starts the data transmission.
 * @skb:        sk_buff pointer that contains data to be Transmitted.
 * @ndev:       Pointer to net_device structure.
 *
 * returns: NETDEV_TX_OK, on success
 *              NETDEV_TX_BUSY, if any of the descriptors are not free.
 *
 * This function is invoked from upper layers to initiate transmission.
 */
static netdev_tx_t nps_enet_start_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct nps_enet_priv *priv = netdev_priv(ndev);

	/* This driver handles one frame at a time  */
	netif_stop_queue(ndev);

	nps_enet_send_frame(ndev, skb);

	priv->tx_skb = skb;

	return NETDEV_TX_OK;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void nps_enet_poll_controller(struct net_device *ndev)
{
	disable_irq(ndev->irq);
	nps_enet_irq_handler(ndev->irq, ndev);
	enable_irq(ndev->irq);
}
#endif

static const struct net_device_ops nps_netdev_ops = {
	.ndo_open		= nps_enet_open,
	.ndo_stop		= nps_enet_stop,
	.ndo_start_xmit		= nps_enet_start_xmit,
	.ndo_set_mac_address	= nps_enet_set_mac_address,
	.ndo_set_rx_mode        = nps_enet_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= nps_enet_poll_controller,
#endif
};

static s32 nps_enet_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct nps_enet_priv *priv;
	s32 err = 0;
	const char *mac_addr;
	struct resource *res_regs;

	if (!dev->of_node)
		return -ENODEV;

	ndev = alloc_etherdev(sizeof(struct nps_enet_priv));
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, dev);
	priv = netdev_priv(ndev);

	/* The EZ NET specific entries in the device structure. */
	ndev->netdev_ops = &nps_netdev_ops;
	ndev->watchdog_timeo = (400 * HZ / 1000);
	/* FIXME :: no multicast support yet */
	ndev->flags &= ~IFF_MULTICAST;

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs_base = devm_ioremap_resource(dev, res_regs);
	if (IS_ERR(priv->regs_base)) {
		err = PTR_ERR(priv->regs_base);
		goto out_netdev;
	}
	dev_dbg(dev, "Registers base address is 0x%p\n", priv->regs_base);

	/* set kernel MAC address to dev */
	mac_addr = of_get_mac_address(dev->of_node);
	if (mac_addr)
		ether_addr_copy(ndev->dev_addr, mac_addr);
	else
		eth_hw_addr_random(ndev);

	/* Get IRQ number */
	priv->irq = platform_get_irq(pdev, 0);
	if (!priv->irq) {
		dev_err(dev, "failed to retrieve <irq Rx-Tx> value from device tree\n");
		err = -ENODEV;
		goto out_netdev;
	}

	netif_napi_add(ndev, &priv->napi, nps_enet_poll,
		       NPS_ENET_NAPI_POLL_WEIGHT);

	/* Register the driver. Should be the last thing in probe */
	err = register_netdev(ndev);
	if (err) {
		dev_err(dev, "Failed to register ndev for %s, err = 0x%08x\n",
			ndev->name, (s32)err);
		goto out_netif_api;
	}

	dev_info(dev, "(rx/tx=%d)\n", priv->irq);
	return 0;

out_netif_api:
	netif_napi_del(&priv->napi);
out_netdev:
	if (err)
		free_netdev(ndev);

	return err;
}

static s32 nps_enet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct nps_enet_priv *priv = netdev_priv(ndev);

	unregister_netdev(ndev);
	free_netdev(ndev);
	netif_napi_del(&priv->napi);

	return 0;
}

static const struct of_device_id nps_enet_dt_ids[] = {
	{ .compatible = "ezchip,nps-mgt-enet" },
	{ /* Sentinel */ }
};

static struct platform_driver nps_enet_driver = {
	.probe = nps_enet_probe,
	.remove = nps_enet_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table  = nps_enet_dt_ids,
	},
};

module_platform_driver(nps_enet_driver);

MODULE_AUTHOR("EZchip Semiconductor");
MODULE_LICENSE("GPL v2");
