/*
 * CAN bus driver for IFI CANFD controller
 *
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * Details about this controller can be found at
 * http://www.ifi-pld.de/IP/CANFD/canfd.html
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/can/dev.h>

#define IFI_CANFD_STCMD				0x0
#define IFI_CANFD_STCMD_HARDRESET		0xDEADCAFD
#define IFI_CANFD_STCMD_ENABLE			BIT(0)
#define IFI_CANFD_STCMD_ERROR_ACTIVE		BIT(2)
#define IFI_CANFD_STCMD_ERROR_PASSIVE		BIT(3)
#define IFI_CANFD_STCMD_BUSOFF			BIT(4)
#define IFI_CANFD_STCMD_ERROR_WARNING		BIT(5)
#define IFI_CANFD_STCMD_BUSMONITOR		BIT(16)
#define IFI_CANFD_STCMD_LOOPBACK		BIT(18)
#define IFI_CANFD_STCMD_DISABLE_CANFD		BIT(24)
#define IFI_CANFD_STCMD_ENABLE_ISO		BIT(25)
#define IFI_CANFD_STCMD_ENABLE_7_9_8_8_TIMING	BIT(26)
#define IFI_CANFD_STCMD_NORMAL_MODE		((u32)BIT(31))

#define IFI_CANFD_RXSTCMD			0x4
#define IFI_CANFD_RXSTCMD_REMOVE_MSG		BIT(0)
#define IFI_CANFD_RXSTCMD_RESET			BIT(7)
#define IFI_CANFD_RXSTCMD_EMPTY			BIT(8)
#define IFI_CANFD_RXSTCMD_OVERFLOW		BIT(13)

#define IFI_CANFD_TXSTCMD			0x8
#define IFI_CANFD_TXSTCMD_ADD_MSG		BIT(0)
#define IFI_CANFD_TXSTCMD_HIGH_PRIO		BIT(1)
#define IFI_CANFD_TXSTCMD_RESET			BIT(7)
#define IFI_CANFD_TXSTCMD_EMPTY			BIT(8)
#define IFI_CANFD_TXSTCMD_FULL			BIT(12)
#define IFI_CANFD_TXSTCMD_OVERFLOW		BIT(13)

#define IFI_CANFD_INTERRUPT			0xc
#define IFI_CANFD_INTERRUPT_ERROR_BUSOFF	BIT(0)
#define IFI_CANFD_INTERRUPT_ERROR_WARNING	BIT(1)
#define IFI_CANFD_INTERRUPT_ERROR_STATE_CHG	BIT(2)
#define IFI_CANFD_INTERRUPT_ERROR_REC_TEC_INC	BIT(3)
#define IFI_CANFD_INTERRUPT_ERROR_COUNTER	BIT(10)
#define IFI_CANFD_INTERRUPT_TXFIFO_EMPTY	BIT(16)
#define IFI_CANFD_INTERRUPT_TXFIFO_REMOVE	BIT(22)
#define IFI_CANFD_INTERRUPT_RXFIFO_NEMPTY	BIT(24)
#define IFI_CANFD_INTERRUPT_RXFIFO_NEMPTY_PER	BIT(25)
#define IFI_CANFD_INTERRUPT_SET_IRQ		((u32)BIT(31))

#define IFI_CANFD_IRQMASK			0x10
#define IFI_CANFD_IRQMASK_ERROR_BUSOFF		BIT(0)
#define IFI_CANFD_IRQMASK_ERROR_WARNING		BIT(1)
#define IFI_CANFD_IRQMASK_ERROR_STATE_CHG	BIT(2)
#define IFI_CANFD_IRQMASK_ERROR_REC_TEC_INC	BIT(3)
#define IFI_CANFD_IRQMASK_SET_ERR		BIT(7)
#define IFI_CANFD_IRQMASK_SET_TS		BIT(15)
#define IFI_CANFD_IRQMASK_TXFIFO_EMPTY		BIT(16)
#define IFI_CANFD_IRQMASK_SET_TX		BIT(23)
#define IFI_CANFD_IRQMASK_RXFIFO_NEMPTY		BIT(24)
#define IFI_CANFD_IRQMASK_SET_RX		((u32)BIT(31))

#define IFI_CANFD_TIME				0x14
#define IFI_CANFD_FTIME				0x18
#define IFI_CANFD_TIME_TIMEB_OFF		0
#define IFI_CANFD_TIME_TIMEA_OFF		8
#define IFI_CANFD_TIME_PRESCALE_OFF		16
#define IFI_CANFD_TIME_SJW_OFF_7_9_8_8		25
#define IFI_CANFD_TIME_SJW_OFF_4_12_6_6		28
#define IFI_CANFD_TIME_SET_SJW_4_12_6_6		BIT(6)
#define IFI_CANFD_TIME_SET_TIMEB_4_12_6_6	BIT(7)
#define IFI_CANFD_TIME_SET_PRESC_4_12_6_6	BIT(14)
#define IFI_CANFD_TIME_SET_TIMEA_4_12_6_6	BIT(15)

#define IFI_CANFD_TDELAY			0x1c
#define IFI_CANFD_TDELAY_DEFAULT		0xb
#define IFI_CANFD_TDELAY_MASK			0x3fff
#define IFI_CANFD_TDELAY_ABS			BIT(14)
#define IFI_CANFD_TDELAY_EN			BIT(15)

#define IFI_CANFD_ERROR				0x20
#define IFI_CANFD_ERROR_TX_OFFSET		0
#define IFI_CANFD_ERROR_TX_MASK			0xff
#define IFI_CANFD_ERROR_RX_OFFSET		16
#define IFI_CANFD_ERROR_RX_MASK			0xff

#define IFI_CANFD_ERRCNT			0x24

#define IFI_CANFD_SUSPEND			0x28

#define IFI_CANFD_REPEAT			0x2c

#define IFI_CANFD_TRAFFIC			0x30

#define IFI_CANFD_TSCONTROL			0x34

#define IFI_CANFD_TSC				0x38

#define IFI_CANFD_TST				0x3c

#define IFI_CANFD_RES1				0x40

#define IFI_CANFD_ERROR_CTR			0x44
#define IFI_CANFD_ERROR_CTR_UNLOCK_MAGIC	0x21302899
#define IFI_CANFD_ERROR_CTR_OVERLOAD_FIRST	BIT(0)
#define IFI_CANFD_ERROR_CTR_ACK_ERROR_FIRST	BIT(1)
#define IFI_CANFD_ERROR_CTR_BIT0_ERROR_FIRST	BIT(2)
#define IFI_CANFD_ERROR_CTR_BIT1_ERROR_FIRST	BIT(3)
#define IFI_CANFD_ERROR_CTR_STUFF_ERROR_FIRST	BIT(4)
#define IFI_CANFD_ERROR_CTR_CRC_ERROR_FIRST	BIT(5)
#define IFI_CANFD_ERROR_CTR_FORM_ERROR_FIRST	BIT(6)
#define IFI_CANFD_ERROR_CTR_OVERLOAD_ALL	BIT(8)
#define IFI_CANFD_ERROR_CTR_ACK_ERROR_ALL	BIT(9)
#define IFI_CANFD_ERROR_CTR_BIT0_ERROR_ALL	BIT(10)
#define IFI_CANFD_ERROR_CTR_BIT1_ERROR_ALL	BIT(11)
#define IFI_CANFD_ERROR_CTR_STUFF_ERROR_ALL	BIT(12)
#define IFI_CANFD_ERROR_CTR_CRC_ERROR_ALL	BIT(13)
#define IFI_CANFD_ERROR_CTR_FORM_ERROR_ALL	BIT(14)
#define IFI_CANFD_ERROR_CTR_BITPOSITION_OFFSET	16
#define IFI_CANFD_ERROR_CTR_BITPOSITION_MASK	0xff
#define IFI_CANFD_ERROR_CTR_ER_RESET		BIT(30)
#define IFI_CANFD_ERROR_CTR_ER_ENABLE		((u32)BIT(31))

#define IFI_CANFD_PAR				0x48

#define IFI_CANFD_CANCLOCK			0x4c

#define IFI_CANFD_SYSCLOCK			0x50

#define IFI_CANFD_VER				0x54
#define IFI_CANFD_VER_REV_MASK			0xff
#define IFI_CANFD_VER_REV_MIN_SUPPORTED		0x15

#define IFI_CANFD_IP_ID				0x58
#define IFI_CANFD_IP_ID_VALUE			0xD073CAFD

#define IFI_CANFD_TEST				0x5c

#define IFI_CANFD_RXFIFO_TS_63_32		0x60

#define IFI_CANFD_RXFIFO_TS_31_0		0x64

#define IFI_CANFD_RXFIFO_DLC			0x68
#define IFI_CANFD_RXFIFO_DLC_DLC_OFFSET		0
#define IFI_CANFD_RXFIFO_DLC_DLC_MASK		0xf
#define IFI_CANFD_RXFIFO_DLC_RTR		BIT(4)
#define IFI_CANFD_RXFIFO_DLC_EDL		BIT(5)
#define IFI_CANFD_RXFIFO_DLC_BRS		BIT(6)
#define IFI_CANFD_RXFIFO_DLC_ESI		BIT(7)
#define IFI_CANFD_RXFIFO_DLC_OBJ_OFFSET		8
#define IFI_CANFD_RXFIFO_DLC_OBJ_MASK		0x1ff
#define IFI_CANFD_RXFIFO_DLC_FNR_OFFSET		24
#define IFI_CANFD_RXFIFO_DLC_FNR_MASK		0xff

#define IFI_CANFD_RXFIFO_ID			0x6c
#define IFI_CANFD_RXFIFO_ID_ID_OFFSET		0
#define IFI_CANFD_RXFIFO_ID_ID_STD_MASK		CAN_SFF_MASK
#define IFI_CANFD_RXFIFO_ID_ID_STD_OFFSET	0
#define IFI_CANFD_RXFIFO_ID_ID_STD_WIDTH	10
#define IFI_CANFD_RXFIFO_ID_ID_XTD_MASK		CAN_EFF_MASK
#define IFI_CANFD_RXFIFO_ID_ID_XTD_OFFSET	11
#define IFI_CANFD_RXFIFO_ID_ID_XTD_WIDTH	18
#define IFI_CANFD_RXFIFO_ID_IDE			BIT(29)

#define IFI_CANFD_RXFIFO_DATA			0x70	/* 0x70..0xac */

#define IFI_CANFD_TXFIFO_SUSPEND_US		0xb0

#define IFI_CANFD_TXFIFO_REPEATCOUNT		0xb4

#define IFI_CANFD_TXFIFO_DLC			0xb8
#define IFI_CANFD_TXFIFO_DLC_DLC_OFFSET		0
#define IFI_CANFD_TXFIFO_DLC_DLC_MASK		0xf
#define IFI_CANFD_TXFIFO_DLC_RTR		BIT(4)
#define IFI_CANFD_TXFIFO_DLC_EDL		BIT(5)
#define IFI_CANFD_TXFIFO_DLC_BRS		BIT(6)
#define IFI_CANFD_TXFIFO_DLC_FNR_OFFSET		24
#define IFI_CANFD_TXFIFO_DLC_FNR_MASK		0xff

#define IFI_CANFD_TXFIFO_ID			0xbc
#define IFI_CANFD_TXFIFO_ID_ID_OFFSET		0
#define IFI_CANFD_TXFIFO_ID_ID_STD_MASK		CAN_SFF_MASK
#define IFI_CANFD_TXFIFO_ID_ID_STD_OFFSET	0
#define IFI_CANFD_TXFIFO_ID_ID_STD_WIDTH	10
#define IFI_CANFD_TXFIFO_ID_ID_XTD_MASK		CAN_EFF_MASK
#define IFI_CANFD_TXFIFO_ID_ID_XTD_OFFSET	11
#define IFI_CANFD_TXFIFO_ID_ID_XTD_WIDTH	18
#define IFI_CANFD_TXFIFO_ID_IDE			BIT(29)

#define IFI_CANFD_TXFIFO_DATA			0xc0	/* 0xb0..0xfc */

#define IFI_CANFD_FILTER_MASK(n)		(0x800 + ((n) * 8) + 0)
#define IFI_CANFD_FILTER_MASK_EXT		BIT(29)
#define IFI_CANFD_FILTER_MASK_EDL		BIT(30)
#define IFI_CANFD_FILTER_MASK_VALID		((u32)BIT(31))

#define IFI_CANFD_FILTER_IDENT(n)		(0x800 + ((n) * 8) + 4)
#define IFI_CANFD_FILTER_IDENT_IDE		BIT(29)
#define IFI_CANFD_FILTER_IDENT_CANFD		BIT(30)
#define IFI_CANFD_FILTER_IDENT_VALID		((u32)BIT(31))

/* IFI CANFD private data structure */
struct ifi_canfd_priv {
	struct can_priv		can;	/* must be the first member */
	struct napi_struct	napi;
	struct net_device	*ndev;
	void __iomem		*base;
};

static void ifi_canfd_irq_enable(struct net_device *ndev, bool enable)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	u32 enirq = 0;

	if (enable) {
		enirq = IFI_CANFD_IRQMASK_TXFIFO_EMPTY |
			IFI_CANFD_IRQMASK_RXFIFO_NEMPTY |
			IFI_CANFD_IRQMASK_ERROR_STATE_CHG |
			IFI_CANFD_IRQMASK_ERROR_WARNING |
			IFI_CANFD_IRQMASK_ERROR_BUSOFF;
		if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
			enirq |= IFI_CANFD_INTERRUPT_ERROR_COUNTER;
	}

	writel(IFI_CANFD_IRQMASK_SET_ERR |
	       IFI_CANFD_IRQMASK_SET_TS |
	       IFI_CANFD_IRQMASK_SET_TX |
	       IFI_CANFD_IRQMASK_SET_RX | enirq,
	       priv->base + IFI_CANFD_IRQMASK);
}

static void ifi_canfd_read_fifo(struct net_device *ndev)
{
	struct net_device_stats *stats = &ndev->stats;
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	struct canfd_frame *cf;
	struct sk_buff *skb;
	const u32 rx_irq_mask = IFI_CANFD_INTERRUPT_RXFIFO_NEMPTY |
				IFI_CANFD_INTERRUPT_RXFIFO_NEMPTY_PER;
	u32 rxdlc, rxid;
	u32 dlc, id;
	int i;

	rxdlc = readl(priv->base + IFI_CANFD_RXFIFO_DLC);
	if (rxdlc & IFI_CANFD_RXFIFO_DLC_EDL)
		skb = alloc_canfd_skb(ndev, &cf);
	else
		skb = alloc_can_skb(ndev, (struct can_frame **)&cf);

	if (!skb) {
		stats->rx_dropped++;
		return;
	}

	dlc = (rxdlc >> IFI_CANFD_RXFIFO_DLC_DLC_OFFSET) &
	      IFI_CANFD_RXFIFO_DLC_DLC_MASK;
	if (rxdlc & IFI_CANFD_RXFIFO_DLC_EDL)
		cf->len = can_fd_dlc2len(dlc);
	else
		cf->len = can_cc_dlc2len(dlc);

	rxid = readl(priv->base + IFI_CANFD_RXFIFO_ID);
	id = (rxid >> IFI_CANFD_RXFIFO_ID_ID_OFFSET);
	if (id & IFI_CANFD_RXFIFO_ID_IDE) {
		id &= IFI_CANFD_RXFIFO_ID_ID_XTD_MASK;
		/*
		 * In case the Extended ID frame is received, the standard
		 * and extended part of the ID are swapped in the register,
		 * so swap them back to obtain the correct ID.
		 */
		id = (id >> IFI_CANFD_RXFIFO_ID_ID_XTD_OFFSET) |
		     ((id & IFI_CANFD_RXFIFO_ID_ID_STD_MASK) <<
		       IFI_CANFD_RXFIFO_ID_ID_XTD_WIDTH);
		id |= CAN_EFF_FLAG;
	} else {
		id &= IFI_CANFD_RXFIFO_ID_ID_STD_MASK;
	}
	cf->can_id = id;

	if (rxdlc & IFI_CANFD_RXFIFO_DLC_ESI) {
		cf->flags |= CANFD_ESI;
		netdev_dbg(ndev, "ESI Error\n");
	}

	if (!(rxdlc & IFI_CANFD_RXFIFO_DLC_EDL) &&
	    (rxdlc & IFI_CANFD_RXFIFO_DLC_RTR)) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		if (rxdlc & IFI_CANFD_RXFIFO_DLC_BRS)
			cf->flags |= CANFD_BRS;

		for (i = 0; i < cf->len; i += 4) {
			*(u32 *)(cf->data + i) =
				readl(priv->base + IFI_CANFD_RXFIFO_DATA + i);
		}
	}

	/* Remove the packet from FIFO */
	writel(IFI_CANFD_RXSTCMD_REMOVE_MSG, priv->base + IFI_CANFD_RXSTCMD);
	writel(rx_irq_mask, priv->base + IFI_CANFD_INTERRUPT);

	stats->rx_packets++;
	stats->rx_bytes += cf->len;

	netif_receive_skb(skb);
}

static int ifi_canfd_do_rx_poll(struct net_device *ndev, int quota)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	u32 pkts = 0;
	u32 rxst;

	rxst = readl(priv->base + IFI_CANFD_RXSTCMD);
	if (rxst & IFI_CANFD_RXSTCMD_EMPTY) {
		netdev_dbg(ndev, "No messages in RX FIFO\n");
		return 0;
	}

	for (;;) {
		if (rxst & IFI_CANFD_RXSTCMD_EMPTY)
			break;
		if (quota <= 0)
			break;

		ifi_canfd_read_fifo(ndev);
		quota--;
		pkts++;
		rxst = readl(priv->base + IFI_CANFD_RXSTCMD);
	}

	if (pkts)
		can_led_event(ndev, CAN_LED_EVENT_RX);

	return pkts;
}

static int ifi_canfd_handle_lost_msg(struct net_device *ndev)
{
	struct net_device_stats *stats = &ndev->stats;
	struct sk_buff *skb;
	struct can_frame *frame;

	netdev_err(ndev, "RX FIFO overflow, message(s) lost.\n");

	stats->rx_errors++;
	stats->rx_over_errors++;

	skb = alloc_can_err_skb(ndev, &frame);
	if (unlikely(!skb))
		return 0;

	frame->can_id |= CAN_ERR_CRTL;
	frame->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	netif_receive_skb(skb);

	return 1;
}

static int ifi_canfd_handle_lec_err(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 errctr = readl(priv->base + IFI_CANFD_ERROR_CTR);
	const u32 errmask = IFI_CANFD_ERROR_CTR_OVERLOAD_FIRST |
			    IFI_CANFD_ERROR_CTR_ACK_ERROR_FIRST |
			    IFI_CANFD_ERROR_CTR_BIT0_ERROR_FIRST |
			    IFI_CANFD_ERROR_CTR_BIT1_ERROR_FIRST |
			    IFI_CANFD_ERROR_CTR_STUFF_ERROR_FIRST |
			    IFI_CANFD_ERROR_CTR_CRC_ERROR_FIRST |
			    IFI_CANFD_ERROR_CTR_FORM_ERROR_FIRST;

	if (!(errctr & errmask))	/* No error happened. */
		return 0;

	priv->can.can_stats.bus_error++;
	stats->rx_errors++;

	/* Propagate the error condition to the CAN stack. */
	skb = alloc_can_err_skb(ndev, &cf);
	if (unlikely(!skb))
		return 0;

	/* Read the error counter register and check for new errors. */
	cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	if (errctr & IFI_CANFD_ERROR_CTR_OVERLOAD_FIRST)
		cf->data[2] |= CAN_ERR_PROT_OVERLOAD;

	if (errctr & IFI_CANFD_ERROR_CTR_ACK_ERROR_FIRST)
		cf->data[3] = CAN_ERR_PROT_LOC_ACK;

	if (errctr & IFI_CANFD_ERROR_CTR_BIT0_ERROR_FIRST)
		cf->data[2] |= CAN_ERR_PROT_BIT0;

	if (errctr & IFI_CANFD_ERROR_CTR_BIT1_ERROR_FIRST)
		cf->data[2] |= CAN_ERR_PROT_BIT1;

	if (errctr & IFI_CANFD_ERROR_CTR_STUFF_ERROR_FIRST)
		cf->data[2] |= CAN_ERR_PROT_STUFF;

	if (errctr & IFI_CANFD_ERROR_CTR_CRC_ERROR_FIRST)
		cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;

	if (errctr & IFI_CANFD_ERROR_CTR_FORM_ERROR_FIRST)
		cf->data[2] |= CAN_ERR_PROT_FORM;

	/* Reset the error counter, ack the IRQ and re-enable the counter. */
	writel(IFI_CANFD_ERROR_CTR_ER_RESET, priv->base + IFI_CANFD_ERROR_CTR);
	writel(IFI_CANFD_INTERRUPT_ERROR_COUNTER,
	       priv->base + IFI_CANFD_INTERRUPT);
	writel(IFI_CANFD_ERROR_CTR_ER_ENABLE, priv->base + IFI_CANFD_ERROR_CTR);

	stats->rx_packets++;
	stats->rx_bytes += cf->len;
	netif_receive_skb(skb);

	return 1;
}

static int ifi_canfd_get_berr_counter(const struct net_device *ndev,
				      struct can_berr_counter *bec)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	u32 err;

	err = readl(priv->base + IFI_CANFD_ERROR);
	bec->rxerr = (err >> IFI_CANFD_ERROR_RX_OFFSET) &
		     IFI_CANFD_ERROR_RX_MASK;
	bec->txerr = (err >> IFI_CANFD_ERROR_TX_OFFSET) &
		     IFI_CANFD_ERROR_TX_MASK;

	return 0;
}

static int ifi_canfd_handle_state_change(struct net_device *ndev,
					 enum can_state new_state)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	struct can_berr_counter bec;

	switch (new_state) {
	case CAN_STATE_ERROR_ACTIVE:
		/* error active state */
		priv->can.can_stats.error_warning++;
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		break;
	case CAN_STATE_ERROR_WARNING:
		/* error warning state */
		priv->can.can_stats.error_warning++;
		priv->can.state = CAN_STATE_ERROR_WARNING;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		/* error passive state */
		priv->can.can_stats.error_passive++;
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		break;
	case CAN_STATE_BUS_OFF:
		/* bus-off state */
		priv->can.state = CAN_STATE_BUS_OFF;
		ifi_canfd_irq_enable(ndev, 0);
		priv->can.can_stats.bus_off++;
		can_bus_off(ndev);
		break;
	default:
		break;
	}

	/* propagate the error condition to the CAN stack */
	skb = alloc_can_err_skb(ndev, &cf);
	if (unlikely(!skb))
		return 0;

	ifi_canfd_get_berr_counter(ndev, &bec);

	switch (new_state) {
	case CAN_STATE_ERROR_WARNING:
		/* error warning state */
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = (bec.txerr > bec.rxerr) ?
			CAN_ERR_CRTL_TX_WARNING :
			CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
		break;
	case CAN_STATE_ERROR_PASSIVE:
		/* error passive state */
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		if (bec.txerr > 127)
			cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
		break;
	case CAN_STATE_BUS_OFF:
		/* bus-off state */
		cf->can_id |= CAN_ERR_BUSOFF;
		break;
	default:
		break;
	}

	stats->rx_packets++;
	stats->rx_bytes += cf->len;
	netif_receive_skb(skb);

	return 1;
}

static int ifi_canfd_handle_state_errors(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	u32 stcmd = readl(priv->base + IFI_CANFD_STCMD);
	int work_done = 0;

	if ((stcmd & IFI_CANFD_STCMD_ERROR_ACTIVE) &&
	    (priv->can.state != CAN_STATE_ERROR_ACTIVE)) {
		netdev_dbg(ndev, "Error, entered active state\n");
		work_done += ifi_canfd_handle_state_change(ndev,
						CAN_STATE_ERROR_ACTIVE);
	}

	if ((stcmd & IFI_CANFD_STCMD_ERROR_WARNING) &&
	    (priv->can.state != CAN_STATE_ERROR_WARNING)) {
		netdev_dbg(ndev, "Error, entered warning state\n");
		work_done += ifi_canfd_handle_state_change(ndev,
						CAN_STATE_ERROR_WARNING);
	}

	if ((stcmd & IFI_CANFD_STCMD_ERROR_PASSIVE) &&
	    (priv->can.state != CAN_STATE_ERROR_PASSIVE)) {
		netdev_dbg(ndev, "Error, entered passive state\n");
		work_done += ifi_canfd_handle_state_change(ndev,
						CAN_STATE_ERROR_PASSIVE);
	}

	if ((stcmd & IFI_CANFD_STCMD_BUSOFF) &&
	    (priv->can.state != CAN_STATE_BUS_OFF)) {
		netdev_dbg(ndev, "Error, entered bus-off state\n");
		work_done += ifi_canfd_handle_state_change(ndev,
						CAN_STATE_BUS_OFF);
	}

	return work_done;
}

static int ifi_canfd_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	u32 rxstcmd = readl(priv->base + IFI_CANFD_RXSTCMD);
	int work_done = 0;

	/* Handle bus state changes */
	work_done += ifi_canfd_handle_state_errors(ndev);

	/* Handle lost messages on RX */
	if (rxstcmd & IFI_CANFD_RXSTCMD_OVERFLOW)
		work_done += ifi_canfd_handle_lost_msg(ndev);

	/* Handle lec errors on the bus */
	if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		work_done += ifi_canfd_handle_lec_err(ndev);

	/* Handle normal messages on RX */
	if (!(rxstcmd & IFI_CANFD_RXSTCMD_EMPTY))
		work_done += ifi_canfd_do_rx_poll(ndev, quota - work_done);

	if (work_done < quota) {
		napi_complete_done(napi, work_done);
		ifi_canfd_irq_enable(ndev, 1);
	}

	return work_done;
}

static irqreturn_t ifi_canfd_isr(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	const u32 rx_irq_mask = IFI_CANFD_INTERRUPT_RXFIFO_NEMPTY |
				IFI_CANFD_INTERRUPT_RXFIFO_NEMPTY_PER |
				IFI_CANFD_INTERRUPT_ERROR_COUNTER |
				IFI_CANFD_INTERRUPT_ERROR_STATE_CHG |
				IFI_CANFD_INTERRUPT_ERROR_WARNING |
				IFI_CANFD_INTERRUPT_ERROR_BUSOFF;
	const u32 tx_irq_mask = IFI_CANFD_INTERRUPT_TXFIFO_EMPTY |
				IFI_CANFD_INTERRUPT_TXFIFO_REMOVE;
	const u32 clr_irq_mask = ~((u32)IFI_CANFD_INTERRUPT_SET_IRQ);
	u32 isr;

	isr = readl(priv->base + IFI_CANFD_INTERRUPT);

	/* No interrupt */
	if (isr == 0)
		return IRQ_NONE;

	/* Clear all pending interrupts but ErrWarn */
	writel(clr_irq_mask, priv->base + IFI_CANFD_INTERRUPT);

	/* RX IRQ or bus warning, start NAPI */
	if (isr & rx_irq_mask) {
		ifi_canfd_irq_enable(ndev, 0);
		napi_schedule(&priv->napi);
	}

	/* TX IRQ */
	if (isr & IFI_CANFD_INTERRUPT_TXFIFO_REMOVE) {
		stats->tx_bytes += can_get_echo_skb(ndev, 0, NULL);
		stats->tx_packets++;
		can_led_event(ndev, CAN_LED_EVENT_TX);
	}

	if (isr & tx_irq_mask)
		netif_wake_queue(ndev);

	return IRQ_HANDLED;
}

static const struct can_bittiming_const ifi_canfd_bittiming_const = {
	.name		= KBUILD_MODNAME,
	.tseg1_min	= 1,	/* Time segment 1 = prop_seg + phase_seg1 */
	.tseg1_max	= 256,
	.tseg2_min	= 2,	/* Time segment 2 = phase_seg2 */
	.tseg2_max	= 256,
	.sjw_max	= 128,
	.brp_min	= 2,
	.brp_max	= 512,
	.brp_inc	= 1,
};

static void ifi_canfd_set_bittiming(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	const struct can_bittiming *dbt = &priv->can.data_bittiming;
	u16 brp, sjw, tseg1, tseg2, tdc;

	/* Configure bit timing */
	brp = bt->brp - 2;
	sjw = bt->sjw - 1;
	tseg1 = bt->prop_seg + bt->phase_seg1 - 1;
	tseg2 = bt->phase_seg2 - 2;
	writel((tseg2 << IFI_CANFD_TIME_TIMEB_OFF) |
	       (tseg1 << IFI_CANFD_TIME_TIMEA_OFF) |
	       (brp << IFI_CANFD_TIME_PRESCALE_OFF) |
	       (sjw << IFI_CANFD_TIME_SJW_OFF_7_9_8_8),
	       priv->base + IFI_CANFD_TIME);

	/* Configure data bit timing */
	brp = dbt->brp - 2;
	sjw = dbt->sjw - 1;
	tseg1 = dbt->prop_seg + dbt->phase_seg1 - 1;
	tseg2 = dbt->phase_seg2 - 2;
	writel((tseg2 << IFI_CANFD_TIME_TIMEB_OFF) |
	       (tseg1 << IFI_CANFD_TIME_TIMEA_OFF) |
	       (brp << IFI_CANFD_TIME_PRESCALE_OFF) |
	       (sjw << IFI_CANFD_TIME_SJW_OFF_7_9_8_8),
	       priv->base + IFI_CANFD_FTIME);

	/* Configure transmitter delay */
	tdc = dbt->brp * (dbt->prop_seg + dbt->phase_seg1);
	tdc &= IFI_CANFD_TDELAY_MASK;
	writel(IFI_CANFD_TDELAY_EN | tdc, priv->base + IFI_CANFD_TDELAY);
}

static void ifi_canfd_set_filter(struct net_device *ndev, const u32 id,
				 const u32 mask, const u32 ident)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);

	writel(mask, priv->base + IFI_CANFD_FILTER_MASK(id));
	writel(ident, priv->base + IFI_CANFD_FILTER_IDENT(id));
}

static void ifi_canfd_set_filters(struct net_device *ndev)
{
	/* Receive all CAN frames (standard ID) */
	ifi_canfd_set_filter(ndev, 0,
			     IFI_CANFD_FILTER_MASK_VALID |
			     IFI_CANFD_FILTER_MASK_EXT,
			     IFI_CANFD_FILTER_IDENT_VALID);

	/* Receive all CAN frames (extended ID) */
	ifi_canfd_set_filter(ndev, 1,
			     IFI_CANFD_FILTER_MASK_VALID |
			     IFI_CANFD_FILTER_MASK_EXT,
			     IFI_CANFD_FILTER_IDENT_VALID |
			     IFI_CANFD_FILTER_IDENT_IDE);

	/* Receive all CANFD frames */
	ifi_canfd_set_filter(ndev, 2,
			     IFI_CANFD_FILTER_MASK_VALID |
			     IFI_CANFD_FILTER_MASK_EDL |
			     IFI_CANFD_FILTER_MASK_EXT,
			     IFI_CANFD_FILTER_IDENT_VALID |
			     IFI_CANFD_FILTER_IDENT_CANFD |
			     IFI_CANFD_FILTER_IDENT_IDE);
}

static void ifi_canfd_start(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	u32 stcmd;

	/* Reset the IP */
	writel(IFI_CANFD_STCMD_HARDRESET, priv->base + IFI_CANFD_STCMD);
	writel(IFI_CANFD_STCMD_ENABLE_7_9_8_8_TIMING,
	       priv->base + IFI_CANFD_STCMD);

	ifi_canfd_set_bittiming(ndev);
	ifi_canfd_set_filters(ndev);

	/* Reset FIFOs */
	writel(IFI_CANFD_RXSTCMD_RESET, priv->base + IFI_CANFD_RXSTCMD);
	writel(0, priv->base + IFI_CANFD_RXSTCMD);
	writel(IFI_CANFD_TXSTCMD_RESET, priv->base + IFI_CANFD_TXSTCMD);
	writel(0, priv->base + IFI_CANFD_TXSTCMD);

	/* Repeat transmission until successful */
	writel(0, priv->base + IFI_CANFD_REPEAT);
	writel(0, priv->base + IFI_CANFD_SUSPEND);

	/* Clear all pending interrupts */
	writel((u32)(~IFI_CANFD_INTERRUPT_SET_IRQ),
	       priv->base + IFI_CANFD_INTERRUPT);

	stcmd = IFI_CANFD_STCMD_ENABLE | IFI_CANFD_STCMD_NORMAL_MODE |
		IFI_CANFD_STCMD_ENABLE_7_9_8_8_TIMING;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		stcmd |= IFI_CANFD_STCMD_BUSMONITOR;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		stcmd |= IFI_CANFD_STCMD_LOOPBACK;

	if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) &&
	    !(priv->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO))
		stcmd |= IFI_CANFD_STCMD_ENABLE_ISO;

	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD))
		stcmd |= IFI_CANFD_STCMD_DISABLE_CANFD;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	ifi_canfd_irq_enable(ndev, 1);

	/* Unlock, reset and enable the error counter. */
	writel(IFI_CANFD_ERROR_CTR_UNLOCK_MAGIC,
	       priv->base + IFI_CANFD_ERROR_CTR);
	writel(IFI_CANFD_ERROR_CTR_ER_RESET, priv->base + IFI_CANFD_ERROR_CTR);
	writel(IFI_CANFD_ERROR_CTR_ER_ENABLE, priv->base + IFI_CANFD_ERROR_CTR);

	/* Enable controller */
	writel(stcmd, priv->base + IFI_CANFD_STCMD);
}

static void ifi_canfd_stop(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);

	/* Reset and disable the error counter. */
	writel(IFI_CANFD_ERROR_CTR_ER_RESET, priv->base + IFI_CANFD_ERROR_CTR);
	writel(0, priv->base + IFI_CANFD_ERROR_CTR);

	/* Reset the IP */
	writel(IFI_CANFD_STCMD_HARDRESET, priv->base + IFI_CANFD_STCMD);

	/* Mask all interrupts */
	writel(~0, priv->base + IFI_CANFD_IRQMASK);

	/* Clear all pending interrupts */
	writel((u32)(~IFI_CANFD_INTERRUPT_SET_IRQ),
	       priv->base + IFI_CANFD_INTERRUPT);

	/* Set the state as STOPPED */
	priv->can.state = CAN_STATE_STOPPED;
}

static int ifi_canfd_set_mode(struct net_device *ndev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		ifi_canfd_start(ndev);
		netif_wake_queue(ndev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ifi_canfd_open(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	int ret;

	ret = open_candev(ndev);
	if (ret) {
		netdev_err(ndev, "Failed to open CAN device\n");
		return ret;
	}

	/* Register interrupt handler */
	ret = request_irq(ndev->irq, ifi_canfd_isr, IRQF_SHARED,
			  ndev->name, ndev);
	if (ret < 0) {
		netdev_err(ndev, "Failed to request interrupt\n");
		goto err_irq;
	}

	ifi_canfd_start(ndev);

	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;
err_irq:
	close_candev(ndev);
	return ret;
}

static int ifi_canfd_close(struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);

	ifi_canfd_stop(ndev);

	free_irq(ndev->irq, ndev);

	close_candev(ndev);

	can_led_event(ndev, CAN_LED_EVENT_STOP);

	return 0;
}

static netdev_tx_t ifi_canfd_start_xmit(struct sk_buff *skb,
					struct net_device *ndev)
{
	struct ifi_canfd_priv *priv = netdev_priv(ndev);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	u32 txst, txid, txdlc;
	int i;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	/* Check if the TX buffer is full */
	txst = readl(priv->base + IFI_CANFD_TXSTCMD);
	if (txst & IFI_CANFD_TXSTCMD_FULL) {
		netif_stop_queue(ndev);
		netdev_err(ndev, "BUG! TX FIFO full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	netif_stop_queue(ndev);

	if (cf->can_id & CAN_EFF_FLAG) {
		txid = cf->can_id & CAN_EFF_MASK;
		/*
		 * In case the Extended ID frame is transmitted, the
		 * standard and extended part of the ID are swapped
		 * in the register, so swap them back to send the
		 * correct ID.
		 */
		txid = (txid >> IFI_CANFD_TXFIFO_ID_ID_XTD_WIDTH) |
		       ((txid & IFI_CANFD_TXFIFO_ID_ID_XTD_MASK) <<
		         IFI_CANFD_TXFIFO_ID_ID_XTD_OFFSET);
		txid |= IFI_CANFD_TXFIFO_ID_IDE;
	} else {
		txid = cf->can_id & CAN_SFF_MASK;
	}

	txdlc = can_fd_len2dlc(cf->len);
	if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) && can_is_canfd_skb(skb)) {
		txdlc |= IFI_CANFD_TXFIFO_DLC_EDL;
		if (cf->flags & CANFD_BRS)
			txdlc |= IFI_CANFD_TXFIFO_DLC_BRS;
	}

	if (cf->can_id & CAN_RTR_FLAG)
		txdlc |= IFI_CANFD_TXFIFO_DLC_RTR;

	/* message ram configuration */
	writel(txid, priv->base + IFI_CANFD_TXFIFO_ID);
	writel(txdlc, priv->base + IFI_CANFD_TXFIFO_DLC);

	for (i = 0; i < cf->len; i += 4) {
		writel(*(u32 *)(cf->data + i),
		       priv->base + IFI_CANFD_TXFIFO_DATA + i);
	}

	writel(0, priv->base + IFI_CANFD_TXFIFO_REPEATCOUNT);
	writel(0, priv->base + IFI_CANFD_TXFIFO_SUSPEND_US);

	can_put_echo_skb(skb, ndev, 0, 0);

	/* Start the transmission */
	writel(IFI_CANFD_TXSTCMD_ADD_MSG, priv->base + IFI_CANFD_TXSTCMD);

	return NETDEV_TX_OK;
}

static const struct net_device_ops ifi_canfd_netdev_ops = {
	.ndo_open	= ifi_canfd_open,
	.ndo_stop	= ifi_canfd_close,
	.ndo_start_xmit	= ifi_canfd_start_xmit,
	.ndo_change_mtu	= can_change_mtu,
};

static int ifi_canfd_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct ifi_canfd_priv *priv;
	void __iomem *addr;
	int irq, ret;
	u32 id, rev;

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	id = readl(addr + IFI_CANFD_IP_ID);
	if (id != IFI_CANFD_IP_ID_VALUE) {
		dev_err(dev, "This block is not IFI CANFD, id=%08x\n", id);
		return -EINVAL;
	}

	rev = readl(addr + IFI_CANFD_VER) & IFI_CANFD_VER_REV_MASK;
	if (rev < IFI_CANFD_VER_REV_MIN_SUPPORTED) {
		dev_err(dev, "This block is too old (rev %i), minimum supported is rev %i\n",
			rev, IFI_CANFD_VER_REV_MIN_SUPPORTED);
		return -EINVAL;
	}

	ndev = alloc_candev(sizeof(*priv), 1);
	if (!ndev)
		return -ENOMEM;

	ndev->irq = irq;
	ndev->flags |= IFF_ECHO;	/* we support local echo */
	ndev->netdev_ops = &ifi_canfd_netdev_ops;

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->base = addr;

	netif_napi_add(ndev, &priv->napi, ifi_canfd_poll, 64);

	priv->can.state = CAN_STATE_STOPPED;

	priv->can.clock.freq = readl(addr + IFI_CANFD_CANCLOCK);

	priv->can.bittiming_const	= &ifi_canfd_bittiming_const;
	priv->can.data_bittiming_const	= &ifi_canfd_bittiming_const;
	priv->can.do_set_mode		= ifi_canfd_set_mode;
	priv->can.do_get_berr_counter	= ifi_canfd_get_berr_counter;

	/* IFI CANFD can do both Bosch FD and ISO FD */
	priv->can.ctrlmode = CAN_CTRLMODE_FD;

	/* IFI CANFD can do both Bosch FD and ISO FD */
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_FD |
				       CAN_CTRLMODE_FD_NON_ISO |
				       CAN_CTRLMODE_BERR_REPORTING;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, dev);

	ret = register_candev(ndev);
	if (ret) {
		dev_err(dev, "Failed to register (ret=%d)\n", ret);
		goto err_reg;
	}

	devm_can_led_init(ndev);

	dev_info(dev, "Driver registered: regs=%p, irq=%d, clock=%d\n",
		 priv->base, ndev->irq, priv->can.clock.freq);

	return 0;

err_reg:
	free_candev(ndev);
	return ret;
}

static int ifi_canfd_plat_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	unregister_candev(ndev);
	platform_set_drvdata(pdev, NULL);
	free_candev(ndev);

	return 0;
}

static const struct of_device_id ifi_canfd_of_table[] = {
	{ .compatible = "ifi,canfd-1.0", .data = NULL },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ifi_canfd_of_table);

static struct platform_driver ifi_canfd_plat_driver = {
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= ifi_canfd_of_table,
	},
	.probe	= ifi_canfd_plat_probe,
	.remove	= ifi_canfd_plat_remove,
};

module_platform_driver(ifi_canfd_plat_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN bus driver for IFI CANFD controller");
