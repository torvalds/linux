/*
 * Blackfin On-Chip CAN Driver
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

#include <linux/can/dev.h>
#include <linux/can/error.h>

#include <asm/bfin_can.h>
#include <asm/portmux.h>

#define DRV_NAME "bfin_can"
#define BFIN_CAN_TIMEOUT 100
#define TX_ECHO_SKB_MAX  1

/*
 * bfin can private data
 */
struct bfin_can_priv {
	struct can_priv can;	/* must be the first member */
	struct net_device *dev;
	void __iomem *membase;
	int rx_irq;
	int tx_irq;
	int err_irq;
	unsigned short *pin_list;
};

/*
 * bfin can timing parameters
 */
static const struct can_bittiming_const bfin_can_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	/*
	 * Although the BRP field can be set to any value, it is recommended
	 * that the value be greater than or equal to 4, as restrictions
	 * apply to the bit timing configuration when BRP is less than 4.
	 */
	.brp_min = 4,
	.brp_max = 1024,
	.brp_inc = 1,
};

static int bfin_can_set_bittiming(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	struct can_bittiming *bt = &priv->can.bittiming;
	u16 clk, timing;

	clk = bt->brp - 1;
	timing = ((bt->sjw - 1) << 8) | (bt->prop_seg + bt->phase_seg1 - 1) |
		((bt->phase_seg2 - 1) << 4);

	/*
	 * If the SAM bit is set, the input signal is oversampled three times
	 * at the SCLK rate.
	 */
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		timing |= SAM;

	bfin_write(&reg->clock, clk);
	bfin_write(&reg->timing, timing);

	netdev_info(dev, "setting CLOCK=0x%04x TIMING=0x%04x\n", clk, timing);

	return 0;
}

static void bfin_can_set_reset_mode(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;
	int i;

	/* disable interrupts */
	bfin_write(&reg->mbim1, 0);
	bfin_write(&reg->mbim2, 0);
	bfin_write(&reg->gim, 0);

	/* reset can and enter configuration mode */
	bfin_write(&reg->control, SRS | CCR);
	SSYNC();
	bfin_write(&reg->control, CCR);
	SSYNC();
	while (!(bfin_read(&reg->control) & CCA)) {
		udelay(10);
		if (--timeout == 0) {
			netdev_err(dev, "fail to enter configuration mode\n");
			BUG();
		}
	}

	/*
	 * All mailbox configurations are marked as inactive
	 * by writing to CAN Mailbox Configuration Registers 1 and 2
	 * For all bits: 0 - Mailbox disabled, 1 - Mailbox enabled
	 */
	bfin_write(&reg->mc1, 0);
	bfin_write(&reg->mc2, 0);

	/* Set Mailbox Direction */
	bfin_write(&reg->md1, 0xFFFF);   /* mailbox 1-16 are RX */
	bfin_write(&reg->md2, 0);   /* mailbox 17-32 are TX */

	/* RECEIVE_STD_CHL */
	for (i = 0; i < 2; i++) {
		bfin_write(&reg->chl[RECEIVE_STD_CHL + i].id0, 0);
		bfin_write(&reg->chl[RECEIVE_STD_CHL + i].id1, AME);
		bfin_write(&reg->chl[RECEIVE_STD_CHL + i].dlc, 0);
		bfin_write(&reg->msk[RECEIVE_STD_CHL + i].amh, 0x1FFF);
		bfin_write(&reg->msk[RECEIVE_STD_CHL + i].aml, 0xFFFF);
	}

	/* RECEIVE_EXT_CHL */
	for (i = 0; i < 2; i++) {
		bfin_write(&reg->chl[RECEIVE_EXT_CHL + i].id0, 0);
		bfin_write(&reg->chl[RECEIVE_EXT_CHL + i].id1, AME | IDE);
		bfin_write(&reg->chl[RECEIVE_EXT_CHL + i].dlc, 0);
		bfin_write(&reg->msk[RECEIVE_EXT_CHL + i].amh, 0x1FFF);
		bfin_write(&reg->msk[RECEIVE_EXT_CHL + i].aml, 0xFFFF);
	}

	bfin_write(&reg->mc2, BIT(TRANSMIT_CHL - 16));
	bfin_write(&reg->mc1, BIT(RECEIVE_STD_CHL) + BIT(RECEIVE_EXT_CHL));
	SSYNC();

	priv->can.state = CAN_STATE_STOPPED;
}

static void bfin_can_set_normal_mode(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;

	/*
	 * leave configuration mode
	 */
	bfin_write(&reg->control, bfin_read(&reg->control) & ~CCR);

	while (bfin_read(&reg->status) & CCA) {
		udelay(10);
		if (--timeout == 0) {
			netdev_err(dev, "fail to leave configuration mode\n");
			BUG();
		}
	}

	/*
	 * clear _All_  tx and rx interrupts
	 */
	bfin_write(&reg->mbtif1, 0xFFFF);
	bfin_write(&reg->mbtif2, 0xFFFF);
	bfin_write(&reg->mbrif1, 0xFFFF);
	bfin_write(&reg->mbrif2, 0xFFFF);

	/*
	 * clear global interrupt status register
	 */
	bfin_write(&reg->gis, 0x7FF); /* overwrites with '1' */

	/*
	 * Initialize Interrupts
	 * - set bits in the mailbox interrupt mask register
	 * - global interrupt mask
	 */
	bfin_write(&reg->mbim1, BIT(RECEIVE_STD_CHL) + BIT(RECEIVE_EXT_CHL));
	bfin_write(&reg->mbim2, BIT(TRANSMIT_CHL - 16));

	bfin_write(&reg->gim, EPIM | BOIM | RMLIM);
	SSYNC();
}

static void bfin_can_start(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);

	/* enter reset mode */
	if (priv->can.state != CAN_STATE_STOPPED)
		bfin_can_set_reset_mode(dev);

	/* leave reset mode */
	bfin_can_set_normal_mode(dev);
}

static int bfin_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		bfin_can_start(dev);
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int bfin_can_get_berr_counter(const struct net_device *dev,
				     struct can_berr_counter *bec)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;

	u16 cec = bfin_read(&reg->cec);

	bec->txerr = cec >> 8;
	bec->rxerr = cec;

	return 0;
}

static int bfin_can_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	struct can_frame *cf = (struct can_frame *)skb->data;
	u8 dlc = cf->can_dlc;
	canid_t id = cf->can_id;
	u8 *data = cf->data;
	u16 val;
	int i;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(dev);

	/* fill id */
	if (id & CAN_EFF_FLAG) {
		bfin_write(&reg->chl[TRANSMIT_CHL].id0, id);
		val = ((id & 0x1FFF0000) >> 16) | IDE;
	} else
		val = (id << 2);
	if (id & CAN_RTR_FLAG)
		val |= RTR;
	bfin_write(&reg->chl[TRANSMIT_CHL].id1, val | AME);

	/* fill payload */
	for (i = 0; i < 8; i += 2) {
		val = ((7 - i) < dlc ? (data[7 - i]) : 0) +
			((6 - i) < dlc ? (data[6 - i] << 8) : 0);
		bfin_write(&reg->chl[TRANSMIT_CHL].data[i], val);
	}

	/* fill data length code */
	bfin_write(&reg->chl[TRANSMIT_CHL].dlc, dlc);

	can_put_echo_skb(skb, dev, 0);

	/* set transmit request */
	bfin_write(&reg->trs2, BIT(TRANSMIT_CHL - 16));

	return 0;
}

static void bfin_can_rx(struct net_device *dev, u16 isrc)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct bfin_can_regs __iomem *reg = priv->membase;
	struct can_frame *cf;
	struct sk_buff *skb;
	int obj;
	int i;
	u16 val;

	skb = alloc_can_skb(dev, &cf);
	if (skb == NULL)
		return;

	/* get id */
	if (isrc & BIT(RECEIVE_EXT_CHL)) {
		/* extended frame format (EFF) */
		cf->can_id = ((bfin_read(&reg->chl[RECEIVE_EXT_CHL].id1)
			     & 0x1FFF) << 16)
			     + bfin_read(&reg->chl[RECEIVE_EXT_CHL].id0);
		cf->can_id |= CAN_EFF_FLAG;
		obj = RECEIVE_EXT_CHL;
	} else {
		/* standard frame format (SFF) */
		cf->can_id = (bfin_read(&reg->chl[RECEIVE_STD_CHL].id1)
			     & 0x1ffc) >> 2;
		obj = RECEIVE_STD_CHL;
	}
	if (bfin_read(&reg->chl[obj].id1) & RTR)
		cf->can_id |= CAN_RTR_FLAG;

	/* get data length code */
	cf->can_dlc = get_can_dlc(bfin_read(&reg->chl[obj].dlc) & 0xF);

	/* get payload */
	for (i = 0; i < 8; i += 2) {
		val = bfin_read(&reg->chl[obj].data[i]);
		cf->data[7 - i] = (7 - i) < cf->can_dlc ? val : 0;
		cf->data[6 - i] = (6 - i) < cf->can_dlc ? (val >> 8) : 0;
	}

	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
}

static int bfin_can_err(struct net_device *dev, u16 isrc, u16 status)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	enum can_state state = priv->can.state;

	skb = alloc_can_err_skb(dev, &cf);
	if (skb == NULL)
		return -ENOMEM;

	if (isrc & RMLIS) {
		/* data overrun interrupt */
		netdev_dbg(dev, "data overrun interrupt\n");
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		stats->rx_errors++;
	}

	if (isrc & BOIS) {
		netdev_dbg(dev, "bus-off mode interrupt\n");
		state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;
		can_bus_off(dev);
	}

	if (isrc & EPIS) {
		/* error passive interrupt */
		netdev_dbg(dev, "error passive interrupt\n");
		state = CAN_STATE_ERROR_PASSIVE;
	}

	if ((isrc & EWTIS) || (isrc & EWRIS)) {
		netdev_dbg(dev, "Error Warning Transmit/Receive Interrupt\n");
		state = CAN_STATE_ERROR_WARNING;
	}

	if (state != priv->can.state && (state == CAN_STATE_ERROR_WARNING ||
				state == CAN_STATE_ERROR_PASSIVE)) {
		u16 cec = bfin_read(&reg->cec);
		u8 rxerr = cec;
		u8 txerr = cec >> 8;

		cf->can_id |= CAN_ERR_CRTL;
		if (state == CAN_STATE_ERROR_WARNING) {
			priv->can.can_stats.error_warning++;
			cf->data[1] = (txerr > rxerr) ?
				CAN_ERR_CRTL_TX_WARNING :
				CAN_ERR_CRTL_RX_WARNING;
		} else {
			priv->can.can_stats.error_passive++;
			cf->data[1] = (txerr > rxerr) ?
				CAN_ERR_CRTL_TX_PASSIVE :
				CAN_ERR_CRTL_RX_PASSIVE;
		}
	}

	if (status) {
		priv->can.can_stats.bus_error++;

		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

		if (status & BEF)
			cf->data[2] |= CAN_ERR_PROT_BIT;
		else if (status & FER)
			cf->data[2] |= CAN_ERR_PROT_FORM;
		else if (status & SER)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
		else
			cf->data[2] |= CAN_ERR_PROT_UNSPEC;
	}

	priv->can.state = state;

	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;

	return 0;
}

irqreturn_t bfin_can_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	struct net_device_stats *stats = &dev->stats;
	u16 status, isrc;

	if ((irq == priv->tx_irq) && bfin_read(&reg->mbtif2)) {
		/* transmission complete interrupt */
		bfin_write(&reg->mbtif2, 0xFFFF);
		stats->tx_packets++;
		stats->tx_bytes += bfin_read(&reg->chl[TRANSMIT_CHL].dlc);
		can_get_echo_skb(dev, 0);
		netif_wake_queue(dev);
	} else if ((irq == priv->rx_irq) && bfin_read(&reg->mbrif1)) {
		/* receive interrupt */
		isrc = bfin_read(&reg->mbrif1);
		bfin_write(&reg->mbrif1, 0xFFFF);
		bfin_can_rx(dev, isrc);
	} else if ((irq == priv->err_irq) && bfin_read(&reg->gis)) {
		/* error interrupt */
		isrc = bfin_read(&reg->gis);
		status = bfin_read(&reg->esr);
		bfin_write(&reg->gis, 0x7FF);
		bfin_can_err(dev, isrc, status);
	} else {
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int bfin_can_open(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);
	int err;

	/* set chip into reset mode */
	bfin_can_set_reset_mode(dev);

	/* common open */
	err = open_candev(dev);
	if (err)
		goto exit_open;

	/* register interrupt handler */
	err = request_irq(priv->rx_irq, &bfin_can_interrupt, 0,
			"bfin-can-rx", dev);
	if (err)
		goto exit_rx_irq;
	err = request_irq(priv->tx_irq, &bfin_can_interrupt, 0,
			"bfin-can-tx", dev);
	if (err)
		goto exit_tx_irq;
	err = request_irq(priv->err_irq, &bfin_can_interrupt, 0,
			"bfin-can-err", dev);
	if (err)
		goto exit_err_irq;

	bfin_can_start(dev);

	netif_start_queue(dev);

	return 0;

exit_err_irq:
	free_irq(priv->tx_irq, dev);
exit_tx_irq:
	free_irq(priv->rx_irq, dev);
exit_rx_irq:
	close_candev(dev);
exit_open:
	return err;
}

static int bfin_can_close(struct net_device *dev)
{
	struct bfin_can_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	bfin_can_set_reset_mode(dev);

	close_candev(dev);

	free_irq(priv->rx_irq, dev);
	free_irq(priv->tx_irq, dev);
	free_irq(priv->err_irq, dev);

	return 0;
}

struct net_device *alloc_bfin_candev(void)
{
	struct net_device *dev;
	struct bfin_can_priv *priv;

	dev = alloc_candev(sizeof(*priv), TX_ECHO_SKB_MAX);
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);

	priv->dev = dev;
	priv->can.bittiming_const = &bfin_can_bittiming_const;
	priv->can.do_set_bittiming = bfin_can_set_bittiming;
	priv->can.do_set_mode = bfin_can_set_mode;
	priv->can.do_get_berr_counter = bfin_can_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES;

	return dev;
}

static const struct net_device_ops bfin_can_netdev_ops = {
	.ndo_open               = bfin_can_open,
	.ndo_stop               = bfin_can_close,
	.ndo_start_xmit         = bfin_can_start_xmit,
};

static int bfin_can_probe(struct platform_device *pdev)
{
	int err;
	struct net_device *dev;
	struct bfin_can_priv *priv;
	struct resource *res_mem, *rx_irq, *tx_irq, *err_irq;
	unsigned short *pdata;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data provided!\n");
		err = -EINVAL;
		goto exit;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	tx_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	err_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (!res_mem || !rx_irq || !tx_irq || !err_irq) {
		err = -EINVAL;
		goto exit;
	}

	if (!request_mem_region(res_mem->start, resource_size(res_mem),
				dev_name(&pdev->dev))) {
		err = -EBUSY;
		goto exit;
	}

	/* request peripheral pins */
	err = peripheral_request_list(pdata, dev_name(&pdev->dev));
	if (err)
		goto exit_mem_release;

	dev = alloc_bfin_candev();
	if (!dev) {
		err = -ENOMEM;
		goto exit_peri_pin_free;
	}

	priv = netdev_priv(dev);
	priv->membase = (void __iomem *)res_mem->start;
	priv->rx_irq = rx_irq->start;
	priv->tx_irq = tx_irq->start;
	priv->err_irq = err_irq->start;
	priv->pin_list = pdata;
	priv->can.clock.freq = get_sclk();

	dev_set_drvdata(&pdev->dev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	dev->flags |= IFF_ECHO;	/* we support local echo */
	dev->netdev_ops = &bfin_can_netdev_ops;

	bfin_can_set_reset_mode(dev);

	err = register_candev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering failed (err=%d)\n", err);
		goto exit_candev_free;
	}

	dev_info(&pdev->dev,
		"%s device registered"
		"(&reg_base=%p, rx_irq=%d, tx_irq=%d, err_irq=%d, sclk=%d)\n",
		DRV_NAME, priv->membase, priv->rx_irq,
		priv->tx_irq, priv->err_irq, priv->can.clock.freq);
	return 0;

exit_candev_free:
	free_candev(dev);
exit_peri_pin_free:
	peripheral_free_list(pdata);
exit_mem_release:
	release_mem_region(res_mem->start, resource_size(res_mem));
exit:
	return err;
}

static int bfin_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct resource *res;

	bfin_can_set_reset_mode(dev);

	unregister_candev(dev);

	dev_set_drvdata(&pdev->dev, NULL);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	peripheral_free_list(priv->pin_list);

	free_candev(dev);
	return 0;
}

#ifdef CONFIG_PM
static int bfin_can_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;

	if (netif_running(dev)) {
		/* enter sleep mode */
		bfin_write(&reg->control, bfin_read(&reg->control) | SMR);
		SSYNC();
		while (!(bfin_read(&reg->intr) & SMACK)) {
			udelay(10);
			if (--timeout == 0) {
				netdev_err(dev, "fail to enter sleep mode\n");
				BUG();
			}
		}
	}

	return 0;
}

static int bfin_can_resume(struct platform_device *pdev)
{
	struct net_device *dev = dev_get_drvdata(&pdev->dev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;

	if (netif_running(dev)) {
		/* leave sleep mode */
		bfin_write(&reg->intr, 0);
		SSYNC();
	}

	return 0;
}
#else
#define bfin_can_suspend NULL
#define bfin_can_resume NULL
#endif	/* CONFIG_PM */

static struct platform_driver bfin_can_driver = {
	.probe = bfin_can_probe,
	.remove = bfin_can_remove,
	.suspend = bfin_can_suspend,
	.resume = bfin_can_resume,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(bfin_can_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blackfin on-chip CAN netdevice driver");
MODULE_ALIAS("platform:" DRV_NAME);
