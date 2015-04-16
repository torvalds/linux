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
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>

#include <linux/can/dev.h>
#include <linux/can/error.h>

#include <asm/portmux.h>

#define DRV_NAME "bfin_can"
#define BFIN_CAN_TIMEOUT 100
#define TX_ECHO_SKB_MAX  1

/* transmit and receive channels */
#define TRANSMIT_CHL 24
#define RECEIVE_STD_CHL 0
#define RECEIVE_EXT_CHL 4
#define RECEIVE_RTR_CHL 8
#define RECEIVE_EXT_RTR_CHL 12
#define MAX_CHL_NUMBER 32

/* All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this
 */
#define __BFP(m) u16 m; u16 __pad_##m

/* bfin can registers layout */
struct bfin_can_mask_regs {
	__BFP(aml);
	__BFP(amh);
};

struct bfin_can_channel_regs {
	/* data[0,2,4,6] -> data{0,1,2,3} while data[1,3,5,7] is padding */
	u16 data[8];
	__BFP(dlc);
	__BFP(tsv);
	__BFP(id0);
	__BFP(id1);
};

struct bfin_can_regs {
	/* global control and status registers */
	__BFP(mc1);		/* offset 0x00 */
	__BFP(md1);		/* offset 0x04 */
	__BFP(trs1);		/* offset 0x08 */
	__BFP(trr1);		/* offset 0x0c */
	__BFP(ta1);		/* offset 0x10 */
	__BFP(aa1);		/* offset 0x14 */
	__BFP(rmp1);		/* offset 0x18 */
	__BFP(rml1);		/* offset 0x1c */
	__BFP(mbtif1);		/* offset 0x20 */
	__BFP(mbrif1);		/* offset 0x24 */
	__BFP(mbim1);		/* offset 0x28 */
	__BFP(rfh1);		/* offset 0x2c */
	__BFP(opss1);		/* offset 0x30 */
	u32 __pad1[3];
	__BFP(mc2);		/* offset 0x40 */
	__BFP(md2);		/* offset 0x44 */
	__BFP(trs2);		/* offset 0x48 */
	__BFP(trr2);		/* offset 0x4c */
	__BFP(ta2);		/* offset 0x50 */
	__BFP(aa2);		/* offset 0x54 */
	__BFP(rmp2);		/* offset 0x58 */
	__BFP(rml2);		/* offset 0x5c */
	__BFP(mbtif2);		/* offset 0x60 */
	__BFP(mbrif2);		/* offset 0x64 */
	__BFP(mbim2);		/* offset 0x68 */
	__BFP(rfh2);		/* offset 0x6c */
	__BFP(opss2);		/* offset 0x70 */
	u32 __pad2[3];
	__BFP(clock);		/* offset 0x80 */
	__BFP(timing);		/* offset 0x84 */
	__BFP(debug);		/* offset 0x88 */
	__BFP(status);		/* offset 0x8c */
	__BFP(cec);		/* offset 0x90 */
	__BFP(gis);		/* offset 0x94 */
	__BFP(gim);		/* offset 0x98 */
	__BFP(gif);		/* offset 0x9c */
	__BFP(control);		/* offset 0xa0 */
	__BFP(intr);		/* offset 0xa4 */
	__BFP(version);		/* offset 0xa8 */
	__BFP(mbtd);		/* offset 0xac */
	__BFP(ewr);		/* offset 0xb0 */
	__BFP(esr);		/* offset 0xb4 */
	u32 __pad3[2];
	__BFP(ucreg);		/* offset 0xc0 */
	__BFP(uccnt);		/* offset 0xc4 */
	__BFP(ucrc);		/* offset 0xc8 */
	__BFP(uccnf);		/* offset 0xcc */
	u32 __pad4[1];
	__BFP(version2);	/* offset 0xd4 */
	u32 __pad5[10];

	/* channel(mailbox) mask and message registers */
	struct bfin_can_mask_regs msk[MAX_CHL_NUMBER];		/* offset 0x100 */
	struct bfin_can_channel_regs chl[MAX_CHL_NUMBER];	/* offset 0x200 */
};

#undef __BFP

#define SRS 0x0001		/* Software Reset */
#define SER 0x0008		/* Stuff Error */
#define BOIM 0x0008		/* Enable Bus Off Interrupt */
#define CCR 0x0080		/* CAN Configuration Mode Request */
#define CCA 0x0080		/* Configuration Mode Acknowledge */
#define SAM 0x0080		/* Sampling */
#define AME 0x8000		/* Acceptance Mask Enable */
#define RMLIM 0x0080		/* Enable RX Message Lost Interrupt */
#define RMLIS 0x0080		/* RX Message Lost IRQ Status */
#define RTR 0x4000		/* Remote Frame Transmission Request */
#define BOIS 0x0008		/* Bus Off IRQ Status */
#define IDE 0x2000		/* Identifier Extension */
#define EPIS 0x0004		/* Error-Passive Mode IRQ Status */
#define EPIM 0x0004		/* Enable Error-Passive Mode Interrupt */
#define EWTIS 0x0001		/* TX Error Count IRQ Status */
#define EWRIS 0x0002		/* RX Error Count IRQ Status */
#define BEF 0x0040		/* Bit Error Flag */
#define FER 0x0080		/* Form Error Flag */
#define SMR 0x0020		/* Sleep Mode Request */
#define SMACK 0x0008		/* Sleep Mode Acknowledge */

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

	writew(clk, &reg->clock);
	writew(timing, &reg->timing);

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
	writew(0, &reg->mbim1);
	writew(0, &reg->mbim2);
	writew(0, &reg->gim);

	/* reset can and enter configuration mode */
	writew(SRS | CCR, &reg->control);
	writew(CCR, &reg->control);
	while (!(readw(&reg->control) & CCA)) {
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
	writew(0, &reg->mc1);
	writew(0, &reg->mc2);

	/* Set Mailbox Direction */
	writew(0xFFFF, &reg->md1);   /* mailbox 1-16 are RX */
	writew(0, &reg->md2);   /* mailbox 17-32 are TX */

	/* RECEIVE_STD_CHL */
	for (i = 0; i < 2; i++) {
		writew(0, &reg->chl[RECEIVE_STD_CHL + i].id0);
		writew(AME, &reg->chl[RECEIVE_STD_CHL + i].id1);
		writew(0, &reg->chl[RECEIVE_STD_CHL + i].dlc);
		writew(0x1FFF, &reg->msk[RECEIVE_STD_CHL + i].amh);
		writew(0xFFFF, &reg->msk[RECEIVE_STD_CHL + i].aml);
	}

	/* RECEIVE_EXT_CHL */
	for (i = 0; i < 2; i++) {
		writew(0, &reg->chl[RECEIVE_EXT_CHL + i].id0);
		writew(AME | IDE, &reg->chl[RECEIVE_EXT_CHL + i].id1);
		writew(0, &reg->chl[RECEIVE_EXT_CHL + i].dlc);
		writew(0x1FFF, &reg->msk[RECEIVE_EXT_CHL + i].amh);
		writew(0xFFFF, &reg->msk[RECEIVE_EXT_CHL + i].aml);
	}

	writew(BIT(TRANSMIT_CHL - 16), &reg->mc2);
	writew(BIT(RECEIVE_STD_CHL) + BIT(RECEIVE_EXT_CHL), &reg->mc1);

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
	writew(readw(&reg->control) & ~CCR, &reg->control);

	while (readw(&reg->status) & CCA) {
		udelay(10);
		if (--timeout == 0) {
			netdev_err(dev, "fail to leave configuration mode\n");
			BUG();
		}
	}

	/*
	 * clear _All_  tx and rx interrupts
	 */
	writew(0xFFFF, &reg->mbtif1);
	writew(0xFFFF, &reg->mbtif2);
	writew(0xFFFF, &reg->mbrif1);
	writew(0xFFFF, &reg->mbrif2);

	/*
	 * clear global interrupt status register
	 */
	writew(0x7FF, &reg->gis); /* overwrites with '1' */

	/*
	 * Initialize Interrupts
	 * - set bits in the mailbox interrupt mask register
	 * - global interrupt mask
	 */
	writew(BIT(RECEIVE_STD_CHL) + BIT(RECEIVE_EXT_CHL), &reg->mbim1);
	writew(BIT(TRANSMIT_CHL - 16), &reg->mbim2);

	writew(EPIM | BOIM | RMLIM, &reg->gim);
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

	u16 cec = readw(&reg->cec);

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
		writew(id, &reg->chl[TRANSMIT_CHL].id0);
		val = ((id & 0x1FFF0000) >> 16) | IDE;
	} else
		val = (id << 2);
	if (id & CAN_RTR_FLAG)
		val |= RTR;
	writew(val | AME, &reg->chl[TRANSMIT_CHL].id1);

	/* fill payload */
	for (i = 0; i < 8; i += 2) {
		val = ((7 - i) < dlc ? (data[7 - i]) : 0) +
			((6 - i) < dlc ? (data[6 - i] << 8) : 0);
		writew(val, &reg->chl[TRANSMIT_CHL].data[i]);
	}

	/* fill data length code */
	writew(dlc, &reg->chl[TRANSMIT_CHL].dlc);

	can_put_echo_skb(skb, dev, 0);

	/* set transmit request */
	writew(BIT(TRANSMIT_CHL - 16), &reg->trs2);

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
		cf->can_id = ((readw(&reg->chl[RECEIVE_EXT_CHL].id1)
			     & 0x1FFF) << 16)
			     + readw(&reg->chl[RECEIVE_EXT_CHL].id0);
		cf->can_id |= CAN_EFF_FLAG;
		obj = RECEIVE_EXT_CHL;
	} else {
		/* standard frame format (SFF) */
		cf->can_id = (readw(&reg->chl[RECEIVE_STD_CHL].id1)
			     & 0x1ffc) >> 2;
		obj = RECEIVE_STD_CHL;
	}
	if (readw(&reg->chl[obj].id1) & RTR)
		cf->can_id |= CAN_RTR_FLAG;

	/* get data length code */
	cf->can_dlc = get_can_dlc(readw(&reg->chl[obj].dlc) & 0xF);

	/* get payload */
	for (i = 0; i < 8; i += 2) {
		val = readw(&reg->chl[obj].data[i]);
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
		priv->can.can_stats.bus_off++;
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
		u16 cec = readw(&reg->cec);
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

static irqreturn_t bfin_can_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	struct net_device_stats *stats = &dev->stats;
	u16 status, isrc;

	if ((irq == priv->tx_irq) && readw(&reg->mbtif2)) {
		/* transmission complete interrupt */
		writew(0xFFFF, &reg->mbtif2);
		stats->tx_packets++;
		stats->tx_bytes += readw(&reg->chl[TRANSMIT_CHL].dlc);
		can_get_echo_skb(dev, 0);
		netif_wake_queue(dev);
	} else if ((irq == priv->rx_irq) && readw(&reg->mbrif1)) {
		/* receive interrupt */
		isrc = readw(&reg->mbrif1);
		writew(0xFFFF, &reg->mbrif1);
		bfin_can_rx(dev, isrc);
	} else if ((irq == priv->err_irq) && readw(&reg->gis)) {
		/* error interrupt */
		isrc = readw(&reg->gis);
		status = readw(&reg->esr);
		writew(0x7FF, &reg->gis);
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

static struct net_device *alloc_bfin_candev(void)
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
	.ndo_change_mtu         = can_change_mtu,
};

static int bfin_can_probe(struct platform_device *pdev)
{
	int err;
	struct net_device *dev;
	struct bfin_can_priv *priv;
	struct resource *res_mem, *rx_irq, *tx_irq, *err_irq;
	unsigned short *pdata;

	pdata = dev_get_platdata(&pdev->dev);
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

	/* request peripheral pins */
	err = peripheral_request_list(pdata, dev_name(&pdev->dev));
	if (err)
		goto exit;

	dev = alloc_bfin_candev();
	if (!dev) {
		err = -ENOMEM;
		goto exit_peri_pin_free;
	}

	priv = netdev_priv(dev);

	priv->membase = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(priv->membase)) {
		err = PTR_ERR(priv->membase);
		goto exit_peri_pin_free;
	}

	priv->rx_irq = rx_irq->start;
	priv->tx_irq = tx_irq->start;
	priv->err_irq = err_irq->start;
	priv->pin_list = pdata;
	priv->can.clock.freq = get_sclk();

	platform_set_drvdata(pdev, dev);
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
exit:
	return err;
}

static int bfin_can_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct bfin_can_priv *priv = netdev_priv(dev);

	bfin_can_set_reset_mode(dev);

	unregister_candev(dev);

	peripheral_free_list(priv->pin_list);

	free_candev(dev);
	return 0;
}

#ifdef CONFIG_PM
static int bfin_can_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;
	int timeout = BFIN_CAN_TIMEOUT;

	if (netif_running(dev)) {
		/* enter sleep mode */
		writew(readw(&reg->control) | SMR, &reg->control);
		while (!(readw(&reg->intr) & SMACK)) {
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
	struct net_device *dev = platform_get_drvdata(pdev);
	struct bfin_can_priv *priv = netdev_priv(dev);
	struct bfin_can_regs __iomem *reg = priv->membase;

	if (netif_running(dev)) {
		/* leave sleep mode */
		writew(0, &reg->intr);
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
	},
};

module_platform_driver(bfin_can_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blackfin on-chip CAN netdevice driver");
MODULE_ALIAS("platform:" DRV_NAME);
