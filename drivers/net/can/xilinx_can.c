/* Xilinx CAN device driver
 *
 * Copyright (C) 2012 - 2014 Xilinx, Inc.
 * Copyright (C) 2009 PetaLogix. All rights reserved.
 * Copyright (C) 2017 Sandvik Mining and Construction Oy
 *
 * Description:
 * This driver is developed for Axi CAN IP and for Zynq CANPS Controller.
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/led.h>

#define DRIVER_NAME	"xilinx_can"

/* CAN registers set */
enum xcan_reg {
	XCAN_SRR_OFFSET		= 0x00, /* Software reset */
	XCAN_MSR_OFFSET		= 0x04, /* Mode select */
	XCAN_BRPR_OFFSET	= 0x08, /* Baud rate prescaler */
	XCAN_BTR_OFFSET		= 0x0C, /* Bit timing */
	XCAN_ECR_OFFSET		= 0x10, /* Error counter */
	XCAN_ESR_OFFSET		= 0x14, /* Error status */
	XCAN_SR_OFFSET		= 0x18, /* Status */
	XCAN_ISR_OFFSET		= 0x1C, /* Interrupt status */
	XCAN_IER_OFFSET		= 0x20, /* Interrupt enable */
	XCAN_ICR_OFFSET		= 0x24, /* Interrupt clear */
	XCAN_TXFIFO_ID_OFFSET	= 0x30,/* TX FIFO ID */
	XCAN_TXFIFO_DLC_OFFSET	= 0x34, /* TX FIFO DLC */
	XCAN_TXFIFO_DW1_OFFSET	= 0x38, /* TX FIFO Data Word 1 */
	XCAN_TXFIFO_DW2_OFFSET	= 0x3C, /* TX FIFO Data Word 2 */
	XCAN_RXFIFO_ID_OFFSET	= 0x50, /* RX FIFO ID */
	XCAN_RXFIFO_DLC_OFFSET	= 0x54, /* RX FIFO DLC */
	XCAN_RXFIFO_DW1_OFFSET	= 0x58, /* RX FIFO Data Word 1 */
	XCAN_RXFIFO_DW2_OFFSET	= 0x5C, /* RX FIFO Data Word 2 */
};

/* CAN register bit masks - XCAN_<REG>_<BIT>_MASK */
#define XCAN_SRR_CEN_MASK		0x00000002 /* CAN enable */
#define XCAN_SRR_RESET_MASK		0x00000001 /* Soft Reset the CAN core */
#define XCAN_MSR_LBACK_MASK		0x00000002 /* Loop back mode select */
#define XCAN_MSR_SLEEP_MASK		0x00000001 /* Sleep mode select */
#define XCAN_BRPR_BRP_MASK		0x000000FF /* Baud rate prescaler */
#define XCAN_BTR_SJW_MASK		0x00000180 /* Synchronous jump width */
#define XCAN_BTR_TS2_MASK		0x00000070 /* Time segment 2 */
#define XCAN_BTR_TS1_MASK		0x0000000F /* Time segment 1 */
#define XCAN_ECR_REC_MASK		0x0000FF00 /* Receive error counter */
#define XCAN_ECR_TEC_MASK		0x000000FF /* Transmit error counter */
#define XCAN_ESR_ACKER_MASK		0x00000010 /* ACK error */
#define XCAN_ESR_BERR_MASK		0x00000008 /* Bit error */
#define XCAN_ESR_STER_MASK		0x00000004 /* Stuff error */
#define XCAN_ESR_FMER_MASK		0x00000002 /* Form error */
#define XCAN_ESR_CRCER_MASK		0x00000001 /* CRC error */
#define XCAN_SR_TXFLL_MASK		0x00000400 /* TX FIFO is full */
#define XCAN_SR_ESTAT_MASK		0x00000180 /* Error status */
#define XCAN_SR_ERRWRN_MASK		0x00000040 /* Error warning */
#define XCAN_SR_NORMAL_MASK		0x00000008 /* Normal mode */
#define XCAN_SR_LBACK_MASK		0x00000002 /* Loop back mode */
#define XCAN_SR_CONFIG_MASK		0x00000001 /* Configuration mode */
#define XCAN_IXR_TXFEMP_MASK		0x00004000 /* TX FIFO Empty */
#define XCAN_IXR_WKUP_MASK		0x00000800 /* Wake up interrupt */
#define XCAN_IXR_SLP_MASK		0x00000400 /* Sleep interrupt */
#define XCAN_IXR_BSOFF_MASK		0x00000200 /* Bus off interrupt */
#define XCAN_IXR_ERROR_MASK		0x00000100 /* Error interrupt */
#define XCAN_IXR_RXNEMP_MASK		0x00000080 /* RX FIFO NotEmpty intr */
#define XCAN_IXR_RXOFLW_MASK		0x00000040 /* RX FIFO Overflow intr */
#define XCAN_IXR_RXOK_MASK		0x00000010 /* Message received intr */
#define XCAN_IXR_TXFLL_MASK		0x00000004 /* Tx FIFO Full intr */
#define XCAN_IXR_TXOK_MASK		0x00000002 /* TX successful intr */
#define XCAN_IXR_ARBLST_MASK		0x00000001 /* Arbitration lost intr */
#define XCAN_IDR_ID1_MASK		0xFFE00000 /* Standard msg identifier */
#define XCAN_IDR_SRR_MASK		0x00100000 /* Substitute remote TXreq */
#define XCAN_IDR_IDE_MASK		0x00080000 /* Identifier extension */
#define XCAN_IDR_ID2_MASK		0x0007FFFE /* Extended message ident */
#define XCAN_IDR_RTR_MASK		0x00000001 /* Remote TX request */
#define XCAN_DLCR_DLC_MASK		0xF0000000 /* Data length code */

#define XCAN_INTR_ALL		(XCAN_IXR_TXOK_MASK | XCAN_IXR_BSOFF_MASK |\
				 XCAN_IXR_WKUP_MASK | XCAN_IXR_SLP_MASK | \
				 XCAN_IXR_RXNEMP_MASK | XCAN_IXR_ERROR_MASK | \
				 XCAN_IXR_RXOFLW_MASK | XCAN_IXR_ARBLST_MASK)

/* CAN register bit shift - XCAN_<REG>_<BIT>_SHIFT */
#define XCAN_BTR_SJW_SHIFT		7  /* Synchronous jump width */
#define XCAN_BTR_TS2_SHIFT		4  /* Time segment 2 */
#define XCAN_IDR_ID1_SHIFT		21 /* Standard Messg Identifier */
#define XCAN_IDR_ID2_SHIFT		1  /* Extended Message Identifier */
#define XCAN_DLCR_DLC_SHIFT		28 /* Data length code */
#define XCAN_ESR_REC_SHIFT		8  /* Rx Error Count */

/* CAN frame length constants */
#define XCAN_FRAME_MAX_DATA_LEN		8
#define XCAN_TIMEOUT			(1 * HZ)

/**
 * struct xcan_priv - This definition define CAN driver instance
 * @can:			CAN private data structure.
 * @tx_lock:			Lock for synchronizing TX interrupt handling
 * @tx_head:			Tx CAN packets ready to send on the queue
 * @tx_tail:			Tx CAN packets successfully sended on the queue
 * @tx_max:			Maximum number packets the driver can send
 * @napi:			NAPI structure
 * @read_reg:			For reading data from CAN registers
 * @write_reg:			For writing data to CAN registers
 * @dev:			Network device data structure
 * @reg_base:			Ioremapped address to registers
 * @irq_flags:			For request_irq()
 * @bus_clk:			Pointer to struct clk
 * @can_clk:			Pointer to struct clk
 */
struct xcan_priv {
	struct can_priv can;
	spinlock_t tx_lock;
	unsigned int tx_head;
	unsigned int tx_tail;
	unsigned int tx_max;
	struct napi_struct napi;
	u32 (*read_reg)(const struct xcan_priv *priv, enum xcan_reg reg);
	void (*write_reg)(const struct xcan_priv *priv, enum xcan_reg reg,
			u32 val);
	struct net_device *dev;
	void __iomem *reg_base;
	unsigned long irq_flags;
	struct clk *bus_clk;
	struct clk *can_clk;
};

/* CAN Bittiming constants as per Xilinx CAN specs */
static const struct can_bittiming_const xcan_bittiming_const = {
	.name = DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

#define XCAN_CAP_WATERMARK	0x0001
struct xcan_devtype_data {
	unsigned int caps;
};

/**
 * xcan_write_reg_le - Write a value to the device register little endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 * @val:	Value to write at the Register offset
 *
 * Write data to the paricular CAN register
 */
static void xcan_write_reg_le(const struct xcan_priv *priv, enum xcan_reg reg,
			u32 val)
{
	iowrite32(val, priv->reg_base + reg);
}

/**
 * xcan_read_reg_le - Read a value from the device register little endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 *
 * Read data from the particular CAN register
 * Return: value read from the CAN register
 */
static u32 xcan_read_reg_le(const struct xcan_priv *priv, enum xcan_reg reg)
{
	return ioread32(priv->reg_base + reg);
}

/**
 * xcan_write_reg_be - Write a value to the device register big endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 * @val:	Value to write at the Register offset
 *
 * Write data to the paricular CAN register
 */
static void xcan_write_reg_be(const struct xcan_priv *priv, enum xcan_reg reg,
			u32 val)
{
	iowrite32be(val, priv->reg_base + reg);
}

/**
 * xcan_read_reg_be - Read a value from the device register big endian
 * @priv:	Driver private data structure
 * @reg:	Register offset
 *
 * Read data from the particular CAN register
 * Return: value read from the CAN register
 */
static u32 xcan_read_reg_be(const struct xcan_priv *priv, enum xcan_reg reg)
{
	return ioread32be(priv->reg_base + reg);
}

/**
 * set_reset_mode - Resets the CAN device mode
 * @ndev:	Pointer to net_device structure
 *
 * This is the driver reset mode routine.The driver
 * enters into configuration mode.
 *
 * Return: 0 on success and failure value on error
 */
static int set_reset_mode(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	unsigned long timeout;

	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);

	timeout = jiffies + XCAN_TIMEOUT;
	while (!(priv->read_reg(priv, XCAN_SR_OFFSET) & XCAN_SR_CONFIG_MASK)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(ndev, "timed out for config mode\n");
			return -ETIMEDOUT;
		}
		usleep_range(500, 10000);
	}

	/* reset clears FIFOs */
	priv->tx_head = 0;
	priv->tx_tail = 0;

	return 0;
}

/**
 * xcan_set_bittiming - CAN set bit timing routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the driver set bittiming  routine.
 * Return: 0 on success and failure value on error
 */
static int xcan_set_bittiming(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct can_bittiming *bt = &priv->can.bittiming;
	u32 btr0, btr1;
	u32 is_config_mode;

	/* Check whether Xilinx CAN is in configuration mode.
	 * It cannot set bit timing if Xilinx CAN is not in configuration mode.
	 */
	is_config_mode = priv->read_reg(priv, XCAN_SR_OFFSET) &
				XCAN_SR_CONFIG_MASK;
	if (!is_config_mode) {
		netdev_alert(ndev,
		     "BUG! Cannot set bittiming - CAN is not in config mode\n");
		return -EPERM;
	}

	/* Setting Baud Rate prescalar value in BRPR Register */
	btr0 = (bt->brp - 1);

	/* Setting Time Segment 1 in BTR Register */
	btr1 = (bt->prop_seg + bt->phase_seg1 - 1);

	/* Setting Time Segment 2 in BTR Register */
	btr1 |= (bt->phase_seg2 - 1) << XCAN_BTR_TS2_SHIFT;

	/* Setting Synchronous jump width in BTR Register */
	btr1 |= (bt->sjw - 1) << XCAN_BTR_SJW_SHIFT;

	priv->write_reg(priv, XCAN_BRPR_OFFSET, btr0);
	priv->write_reg(priv, XCAN_BTR_OFFSET, btr1);

	netdev_dbg(ndev, "BRPR=0x%08x, BTR=0x%08x\n",
			priv->read_reg(priv, XCAN_BRPR_OFFSET),
			priv->read_reg(priv, XCAN_BTR_OFFSET));

	return 0;
}

/**
 * xcan_chip_start - This the drivers start routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the drivers start routine.
 * Based on the State of the CAN device it puts
 * the CAN device into a proper mode.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_chip_start(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 reg_msr, reg_sr_mask;
	int err;
	unsigned long timeout;

	/* Check if it is in reset mode */
	err = set_reset_mode(ndev);
	if (err < 0)
		return err;

	err = xcan_set_bittiming(ndev);
	if (err < 0)
		return err;

	/* Enable interrupts */
	priv->write_reg(priv, XCAN_IER_OFFSET, XCAN_INTR_ALL);

	/* Check whether it is loopback mode or normal mode  */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		reg_msr = XCAN_MSR_LBACK_MASK;
		reg_sr_mask = XCAN_SR_LBACK_MASK;
	} else {
		reg_msr = 0x0;
		reg_sr_mask = XCAN_SR_NORMAL_MASK;
	}

	priv->write_reg(priv, XCAN_MSR_OFFSET, reg_msr);
	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_CEN_MASK);

	timeout = jiffies + XCAN_TIMEOUT;
	while (!(priv->read_reg(priv, XCAN_SR_OFFSET) & reg_sr_mask)) {
		if (time_after(jiffies, timeout)) {
			netdev_warn(ndev,
				"timed out for correct mode\n");
			return -ETIMEDOUT;
		}
	}
	netdev_dbg(ndev, "status:#x%08x\n",
			priv->read_reg(priv, XCAN_SR_OFFSET));

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	return 0;
}

/**
 * xcan_do_set_mode - This sets the mode of the driver
 * @ndev:	Pointer to net_device structure
 * @mode:	Tells the mode of the driver
 *
 * This check the drivers state and calls the
 * the corresponding modes to set.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret;

	switch (mode) {
	case CAN_MODE_START:
		ret = xcan_chip_start(ndev);
		if (ret < 0) {
			netdev_err(ndev, "xcan_chip_start failed!\n");
			return ret;
		}
		netif_wake_queue(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/**
 * xcan_start_xmit - Starts the transmission
 * @skb:	sk_buff pointer that contains data to be Txed
 * @ndev:	Pointer to net_device structure
 *
 * This function is invoked from upper layers to initiate transmission. This
 * function uses the next available free txbuff and populates their fields to
 * start the transmission.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	u32 id, dlc, data[2] = {0, 0};
	unsigned long flags;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	/* Check if the TX buffer is full */
	if (unlikely(priv->read_reg(priv, XCAN_SR_OFFSET) &
			XCAN_SR_TXFLL_MASK)) {
		netif_stop_queue(ndev);
		netdev_err(ndev, "BUG!, TX FIFO full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	/* Watch carefully on the bit sequence */
	if (cf->can_id & CAN_EFF_FLAG) {
		/* Extended CAN ID format */
		id = ((cf->can_id & CAN_EFF_MASK) << XCAN_IDR_ID2_SHIFT) &
			XCAN_IDR_ID2_MASK;
		id |= (((cf->can_id & CAN_EFF_MASK) >>
			(CAN_EFF_ID_BITS-CAN_SFF_ID_BITS)) <<
			XCAN_IDR_ID1_SHIFT) & XCAN_IDR_ID1_MASK;

		/* The substibute remote TX request bit should be "1"
		 * for extended frames as in the Xilinx CAN datasheet
		 */
		id |= XCAN_IDR_IDE_MASK | XCAN_IDR_SRR_MASK;

		if (cf->can_id & CAN_RTR_FLAG)
			/* Extended frames remote TX request */
			id |= XCAN_IDR_RTR_MASK;
	} else {
		/* Standard CAN ID format */
		id = ((cf->can_id & CAN_SFF_MASK) << XCAN_IDR_ID1_SHIFT) &
			XCAN_IDR_ID1_MASK;

		if (cf->can_id & CAN_RTR_FLAG)
			/* Standard frames remote TX request */
			id |= XCAN_IDR_SRR_MASK;
	}

	dlc = cf->can_dlc << XCAN_DLCR_DLC_SHIFT;

	if (cf->can_dlc > 0)
		data[0] = be32_to_cpup((__be32 *)(cf->data + 0));
	if (cf->can_dlc > 4)
		data[1] = be32_to_cpup((__be32 *)(cf->data + 4));

	can_put_echo_skb(skb, ndev, priv->tx_head % priv->tx_max);

	spin_lock_irqsave(&priv->tx_lock, flags);

	priv->tx_head++;

	/* Write the Frame to Xilinx CAN TX FIFO */
	priv->write_reg(priv, XCAN_TXFIFO_ID_OFFSET, id);
	/* If the CAN frame is RTR frame this write triggers tranmission */
	priv->write_reg(priv, XCAN_TXFIFO_DLC_OFFSET, dlc);
	if (!(cf->can_id & CAN_RTR_FLAG)) {
		priv->write_reg(priv, XCAN_TXFIFO_DW1_OFFSET, data[0]);
		/* If the CAN frame is Standard/Extended frame this
		 * write triggers tranmission
		 */
		priv->write_reg(priv, XCAN_TXFIFO_DW2_OFFSET, data[1]);
		stats->tx_bytes += cf->can_dlc;
	}

	/* Clear TX-FIFO-empty interrupt for xcan_tx_interrupt() */
	if (priv->tx_max > 1)
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_TXFEMP_MASK);

	/* Check if the TX buffer is full */
	if ((priv->tx_head - priv->tx_tail) == priv->tx_max)
		netif_stop_queue(ndev);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return NETDEV_TX_OK;
}

/**
 * xcan_rx -  Is called from CAN isr to complete the received
 *		frame  processing
 * @ndev:	Pointer to net_device structure
 *
 * This function is invoked from the CAN isr(poll) to process the Rx frames. It
 * does minimal processing and invokes "netif_receive_skb" to complete further
 * processing.
 * Return: 1 on success and 0 on failure.
 */
static int xcan_rx(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 id_xcan, dlc, data[2] = {0, 0};

	skb = alloc_can_skb(ndev, &cf);
	if (unlikely(!skb)) {
		stats->rx_dropped++;
		return 0;
	}

	/* Read a frame from Xilinx zynq CANPS */
	id_xcan = priv->read_reg(priv, XCAN_RXFIFO_ID_OFFSET);
	dlc = priv->read_reg(priv, XCAN_RXFIFO_DLC_OFFSET) >>
				XCAN_DLCR_DLC_SHIFT;

	/* Change Xilinx CAN data length format to socketCAN data format */
	cf->can_dlc = get_can_dlc(dlc);

	/* Change Xilinx CAN ID format to socketCAN ID format */
	if (id_xcan & XCAN_IDR_IDE_MASK) {
		/* The received frame is an Extended format frame */
		cf->can_id = (id_xcan & XCAN_IDR_ID1_MASK) >> 3;
		cf->can_id |= (id_xcan & XCAN_IDR_ID2_MASK) >>
				XCAN_IDR_ID2_SHIFT;
		cf->can_id |= CAN_EFF_FLAG;
		if (id_xcan & XCAN_IDR_RTR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	} else {
		/* The received frame is a standard format frame */
		cf->can_id = (id_xcan & XCAN_IDR_ID1_MASK) >>
				XCAN_IDR_ID1_SHIFT;
		if (id_xcan & XCAN_IDR_SRR_MASK)
			cf->can_id |= CAN_RTR_FLAG;
	}

	/* DW1/DW2 must always be read to remove message from RXFIFO */
	data[0] = priv->read_reg(priv, XCAN_RXFIFO_DW1_OFFSET);
	data[1] = priv->read_reg(priv, XCAN_RXFIFO_DW2_OFFSET);

	if (!(cf->can_id & CAN_RTR_FLAG)) {
		/* Change Xilinx CAN data format to socketCAN data format */
		if (cf->can_dlc > 0)
			*(__be32 *)(cf->data) = cpu_to_be32(data[0]);
		if (cf->can_dlc > 4)
			*(__be32 *)(cf->data + 4) = cpu_to_be32(data[1]);
	}

	stats->rx_bytes += cf->can_dlc;
	stats->rx_packets++;
	netif_receive_skb(skb);

	return 1;
}

/**
 * xcan_current_error_state - Get current error state from HW
 * @ndev:	Pointer to net_device structure
 *
 * Checks the current CAN error state from the HW. Note that this
 * only checks for ERROR_PASSIVE and ERROR_WARNING.
 *
 * Return:
 * ERROR_PASSIVE or ERROR_WARNING if either is active, ERROR_ACTIVE
 * otherwise.
 */
static enum can_state xcan_current_error_state(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 status = priv->read_reg(priv, XCAN_SR_OFFSET);

	if ((status & XCAN_SR_ESTAT_MASK) == XCAN_SR_ESTAT_MASK)
		return CAN_STATE_ERROR_PASSIVE;
	else if (status & XCAN_SR_ERRWRN_MASK)
		return CAN_STATE_ERROR_WARNING;
	else
		return CAN_STATE_ERROR_ACTIVE;
}

/**
 * xcan_set_error_state - Set new CAN error state
 * @ndev:	Pointer to net_device structure
 * @new_state:	The new CAN state to be set
 * @cf:		Error frame to be populated or NULL
 *
 * Set new CAN error state for the device, updating statistics and
 * populating the error frame if given.
 */
static void xcan_set_error_state(struct net_device *ndev,
				 enum can_state new_state,
				 struct can_frame *cf)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 ecr = priv->read_reg(priv, XCAN_ECR_OFFSET);
	u32 txerr = ecr & XCAN_ECR_TEC_MASK;
	u32 rxerr = (ecr & XCAN_ECR_REC_MASK) >> XCAN_ESR_REC_SHIFT;

	priv->can.state = new_state;

	if (cf) {
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}

	switch (new_state) {
	case CAN_STATE_ERROR_PASSIVE:
		priv->can.can_stats.error_passive++;
		if (cf)
			cf->data[1] = (rxerr > 127) ?
					CAN_ERR_CRTL_RX_PASSIVE :
					CAN_ERR_CRTL_TX_PASSIVE;
		break;
	case CAN_STATE_ERROR_WARNING:
		priv->can.can_stats.error_warning++;
		if (cf)
			cf->data[1] |= (txerr > rxerr) ?
					CAN_ERR_CRTL_TX_WARNING :
					CAN_ERR_CRTL_RX_WARNING;
		break;
	case CAN_STATE_ERROR_ACTIVE:
		if (cf)
			cf->data[1] |= CAN_ERR_CRTL_ACTIVE;
		break;
	default:
		/* non-ERROR states are handled elsewhere */
		WARN_ON(1);
		break;
	}
}

/**
 * xcan_update_error_state_after_rxtx - Update CAN error state after RX/TX
 * @ndev:	Pointer to net_device structure
 *
 * If the device is in a ERROR-WARNING or ERROR-PASSIVE state, check if
 * the performed RX/TX has caused it to drop to a lesser state and set
 * the interface state accordingly.
 */
static void xcan_update_error_state_after_rxtx(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	enum can_state old_state = priv->can.state;
	enum can_state new_state;

	/* changing error state due to successful frame RX/TX can only
	 * occur from these states
	 */
	if (old_state != CAN_STATE_ERROR_WARNING &&
	    old_state != CAN_STATE_ERROR_PASSIVE)
		return;

	new_state = xcan_current_error_state(ndev);

	if (new_state != old_state) {
		struct sk_buff *skb;
		struct can_frame *cf;

		skb = alloc_can_err_skb(ndev, &cf);

		xcan_set_error_state(ndev, new_state, skb ? cf : NULL);

		if (skb) {
			struct net_device_stats *stats = &ndev->stats;

			stats->rx_packets++;
			stats->rx_bytes += cf->can_dlc;
			netif_rx(skb);
		}
	}
}

/**
 * xcan_err_interrupt - error frame Isr
 * @ndev:	net_device pointer
 * @isr:	interrupt status register value
 *
 * This is the CAN error interrupt and it will
 * check the the type of error and forward the error
 * frame to upper layers.
 */
static void xcan_err_interrupt(struct net_device *ndev, u32 isr)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u32 err_status;

	skb = alloc_can_err_skb(ndev, &cf);

	err_status = priv->read_reg(priv, XCAN_ESR_OFFSET);
	priv->write_reg(priv, XCAN_ESR_OFFSET, err_status);

	if (isr & XCAN_IXR_BSOFF_MASK) {
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		/* Leave device in Config Mode in bus-off state */
		priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);
		can_bus_off(ndev);
		if (skb)
			cf->can_id |= CAN_ERR_BUSOFF;
	} else {
		enum can_state new_state = xcan_current_error_state(ndev);

		xcan_set_error_state(ndev, new_state, skb ? cf : NULL);
	}

	/* Check for Arbitration lost interrupt */
	if (isr & XCAN_IXR_ARBLST_MASK) {
		priv->can.can_stats.arbitration_lost++;
		if (skb) {
			cf->can_id |= CAN_ERR_LOSTARB;
			cf->data[0] = CAN_ERR_LOSTARB_UNSPEC;
		}
	}

	/* Check for RX FIFO Overflow interrupt */
	if (isr & XCAN_IXR_RXOFLW_MASK) {
		stats->rx_over_errors++;
		stats->rx_errors++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
		}
	}

	/* Check for error interrupt */
	if (isr & XCAN_IXR_ERROR_MASK) {
		if (skb)
			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

		/* Check for Ack error interrupt */
		if (err_status & XCAN_ESR_ACKER_MASK) {
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_ACK;
				cf->data[3] = CAN_ERR_PROT_LOC_ACK;
			}
		}

		/* Check for Bit error interrupt */
		if (err_status & XCAN_ESR_BERR_MASK) {
			stats->tx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_BIT;
			}
		}

		/* Check for Stuff error interrupt */
		if (err_status & XCAN_ESR_STER_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_STUFF;
			}
		}

		/* Check for Form error interrupt */
		if (err_status & XCAN_ESR_FMER_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[2] = CAN_ERR_PROT_FORM;
			}
		}

		/* Check for CRC error interrupt */
		if (err_status & XCAN_ESR_CRCER_MASK) {
			stats->rx_errors++;
			if (skb) {
				cf->can_id |= CAN_ERR_PROT;
				cf->data[3] = CAN_ERR_PROT_LOC_CRC_SEQ;
			}
		}
			priv->can.can_stats.bus_error++;
	}

	if (skb) {
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}

	netdev_dbg(ndev, "%s: error status register:0x%x\n",
			__func__, priv->read_reg(priv, XCAN_ESR_OFFSET));
}

/**
 * xcan_state_interrupt - It will check the state of the CAN device
 * @ndev:	net_device pointer
 * @isr:	interrupt status register value
 *
 * This will checks the state of the CAN device
 * and puts the device into appropriate state.
 */
static void xcan_state_interrupt(struct net_device *ndev, u32 isr)
{
	struct xcan_priv *priv = netdev_priv(ndev);

	/* Check for Sleep interrupt if set put CAN device in sleep state */
	if (isr & XCAN_IXR_SLP_MASK)
		priv->can.state = CAN_STATE_SLEEPING;

	/* Check for Wake up interrupt if set put CAN device in Active state */
	if (isr & XCAN_IXR_WKUP_MASK)
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
}

/**
 * xcan_rx_poll - Poll routine for rx packets (NAPI)
 * @napi:	napi structure pointer
 * @quota:	Max number of rx packets to be processed.
 *
 * This is the poll routine for rx part.
 * It will process the packets maximux quota value.
 *
 * Return: number of packets received
 */
static int xcan_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 isr, ier;
	int work_done = 0;

	isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	while ((isr & XCAN_IXR_RXNEMP_MASK) && (work_done < quota)) {
		work_done += xcan_rx(ndev);
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_RXNEMP_MASK);
		isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	}

	if (work_done) {
		can_led_event(ndev, CAN_LED_EVENT_RX);
		xcan_update_error_state_after_rxtx(ndev);
	}

	if (work_done < quota) {
		napi_complete(napi);
		ier = priv->read_reg(priv, XCAN_IER_OFFSET);
		ier |= XCAN_IXR_RXNEMP_MASK;
		priv->write_reg(priv, XCAN_IER_OFFSET, ier);
	}
	return work_done;
}

/**
 * xcan_tx_interrupt - Tx Done Isr
 * @ndev:	net_device pointer
 * @isr:	Interrupt status register value
 */
static void xcan_tx_interrupt(struct net_device *ndev, u32 isr)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	unsigned int frames_in_fifo;
	int frames_sent = 1; /* TXOK => at least 1 frame was sent */
	unsigned long flags;
	int retries = 0;

	/* Synchronize with xmit as we need to know the exact number
	 * of frames in the FIFO to stay in sync due to the TXFEMP
	 * handling.
	 * This also prevents a race between netif_wake_queue() and
	 * netif_stop_queue().
	 */
	spin_lock_irqsave(&priv->tx_lock, flags);

	frames_in_fifo = priv->tx_head - priv->tx_tail;

	if (WARN_ON_ONCE(frames_in_fifo == 0)) {
		/* clear TXOK anyway to avoid getting back here */
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_TXOK_MASK);
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return;
	}

	/* Check if 2 frames were sent (TXOK only means that at least 1
	 * frame was sent).
	 */
	if (frames_in_fifo > 1) {
		WARN_ON(frames_in_fifo > priv->tx_max);

		/* Synchronize TXOK and isr so that after the loop:
		 * (1) isr variable is up-to-date at least up to TXOK clear
		 *     time. This avoids us clearing a TXOK of a second frame
		 *     but not noticing that the FIFO is now empty and thus
		 *     marking only a single frame as sent.
		 * (2) No TXOK is left. Having one could mean leaving a
		 *     stray TXOK as we might process the associated frame
		 *     via TXFEMP handling as we read TXFEMP *after* TXOK
		 *     clear to satisfy (1).
		 */
		while ((isr & XCAN_IXR_TXOK_MASK) && !WARN_ON(++retries == 100)) {
			priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_TXOK_MASK);
			isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
		}

		if (isr & XCAN_IXR_TXFEMP_MASK) {
			/* nothing in FIFO anymore */
			frames_sent = frames_in_fifo;
		}
	} else {
		/* single frame in fifo, just clear TXOK */
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_TXOK_MASK);
	}

	while (frames_sent--) {
		can_get_echo_skb(ndev, priv->tx_tail %
					priv->tx_max);
		priv->tx_tail++;
		stats->tx_packets++;
	}

	netif_wake_queue(ndev);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	can_led_event(ndev, CAN_LED_EVENT_TX);
	xcan_update_error_state_after_rxtx(ndev);
}

/**
 * xcan_interrupt - CAN Isr
 * @irq:	irq number
 * @dev_id:	device id poniter
 *
 * This is the xilinx CAN Isr. It checks for the type of interrupt
 * and invokes the corresponding ISR.
 *
 * Return:
 * IRQ_NONE - If CAN device is in sleep mode, IRQ_HANDLED otherwise
 */
static irqreturn_t xcan_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 isr, ier;
	u32 isr_errors;

	/* Get the interrupt status from Xilinx CAN */
	isr = priv->read_reg(priv, XCAN_ISR_OFFSET);
	if (!isr)
		return IRQ_NONE;

	/* Check for the type of interrupt and Processing it */
	if (isr & (XCAN_IXR_SLP_MASK | XCAN_IXR_WKUP_MASK)) {
		priv->write_reg(priv, XCAN_ICR_OFFSET, (XCAN_IXR_SLP_MASK |
				XCAN_IXR_WKUP_MASK));
		xcan_state_interrupt(ndev, isr);
	}

	/* Check for Tx interrupt and Processing it */
	if (isr & XCAN_IXR_TXOK_MASK)
		xcan_tx_interrupt(ndev, isr);

	/* Check for the type of error interrupt and Processing it */
	isr_errors = isr & (XCAN_IXR_ERROR_MASK | XCAN_IXR_RXOFLW_MASK |
			    XCAN_IXR_BSOFF_MASK | XCAN_IXR_ARBLST_MASK);
	if (isr_errors) {
		priv->write_reg(priv, XCAN_ICR_OFFSET, isr_errors);
		xcan_err_interrupt(ndev, isr);
	}

	/* Check for the type of receive interrupt and Processing it */
	if (isr & XCAN_IXR_RXNEMP_MASK) {
		ier = priv->read_reg(priv, XCAN_IER_OFFSET);
		ier &= ~XCAN_IXR_RXNEMP_MASK;
		priv->write_reg(priv, XCAN_IER_OFFSET, ier);
		napi_schedule(&priv->napi);
	}
	return IRQ_HANDLED;
}

/**
 * xcan_chip_stop - Driver stop routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the drivers stop routine. It will disable the
 * interrupts and put the device into configuration mode.
 */
static void xcan_chip_stop(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	u32 ier;

	/* Disable interrupts and leave the can in configuration mode */
	ier = priv->read_reg(priv, XCAN_IER_OFFSET);
	ier &= ~XCAN_INTR_ALL;
	priv->write_reg(priv, XCAN_IER_OFFSET, ier);
	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_RESET_MASK);
	priv->can.state = CAN_STATE_STOPPED;
}

/**
 * xcan_open - Driver open routine
 * @ndev:	Pointer to net_device structure
 *
 * This is the driver open routine.
 * Return: 0 on success and failure value on error
 */
static int xcan_open(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	ret = request_irq(ndev->irq, xcan_interrupt, priv->irq_flags,
			ndev->name, ndev);
	if (ret < 0) {
		netdev_err(ndev, "irq allocation for CAN failed\n");
		goto err;
	}

	ret = clk_prepare_enable(priv->can_clk);
	if (ret) {
		netdev_err(ndev, "unable to enable device clock\n");
		goto err_irq;
	}

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret) {
		netdev_err(ndev, "unable to enable bus clock\n");
		goto err_can_clk;
	}

	/* Set chip into reset mode */
	ret = set_reset_mode(ndev);
	if (ret < 0) {
		netdev_err(ndev, "mode resetting failed!\n");
		goto err_bus_clk;
	}

	/* Common open */
	ret = open_candev(ndev);
	if (ret)
		goto err_bus_clk;

	ret = xcan_chip_start(ndev);
	if (ret < 0) {
		netdev_err(ndev, "xcan_chip_start failed!\n");
		goto err_candev;
	}

	can_led_event(ndev, CAN_LED_EVENT_OPEN);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;

err_candev:
	close_candev(ndev);
err_bus_clk:
	clk_disable_unprepare(priv->bus_clk);
err_can_clk:
	clk_disable_unprepare(priv->can_clk);
err_irq:
	free_irq(ndev->irq, ndev);
err:
	return ret;
}

/**
 * xcan_close - Driver close routine
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0 always
 */
static int xcan_close(struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	xcan_chip_stop(ndev);
	clk_disable_unprepare(priv->bus_clk);
	clk_disable_unprepare(priv->can_clk);
	free_irq(ndev->irq, ndev);
	close_candev(ndev);

	can_led_event(ndev, CAN_LED_EVENT_STOP);

	return 0;
}

/**
 * xcan_get_berr_counter - error counter routine
 * @ndev:	Pointer to net_device structure
 * @bec:	Pointer to can_berr_counter structure
 *
 * This is the driver error counter routine.
 * Return: 0 on success and failure value on error
 */
static int xcan_get_berr_counter(const struct net_device *ndev,
					struct can_berr_counter *bec)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	ret = clk_prepare_enable(priv->can_clk);
	if (ret)
		goto err;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret)
		goto err_clk;

	bec->txerr = priv->read_reg(priv, XCAN_ECR_OFFSET) & XCAN_ECR_TEC_MASK;
	bec->rxerr = ((priv->read_reg(priv, XCAN_ECR_OFFSET) &
			XCAN_ECR_REC_MASK) >> XCAN_ESR_REC_SHIFT);

	clk_disable_unprepare(priv->bus_clk);
	clk_disable_unprepare(priv->can_clk);

	return 0;

err_clk:
	clk_disable_unprepare(priv->can_clk);
err:
	return ret;
}


static const struct net_device_ops xcan_netdev_ops = {
	.ndo_open	= xcan_open,
	.ndo_stop	= xcan_close,
	.ndo_start_xmit	= xcan_start_xmit,
	.ndo_change_mtu	= can_change_mtu,
};

/**
 * xcan_suspend - Suspend method for the driver
 * @dev:	Address of the platform_device structure
 *
 * Put the driver into low power mode.
 * Return: 0 always
 */
static int __maybe_unused xcan_suspend(struct device *dev)
{
	struct platform_device *pdev = dev_get_drvdata(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct xcan_priv *priv = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
	}

	priv->write_reg(priv, XCAN_MSR_OFFSET, XCAN_MSR_SLEEP_MASK);
	priv->can.state = CAN_STATE_SLEEPING;

	clk_disable(priv->bus_clk);
	clk_disable(priv->can_clk);

	return 0;
}

/**
 * xcan_resume - Resume from suspend
 * @dev:	Address of the platformdevice structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_resume(struct device *dev)
{
	struct platform_device *pdev = dev_get_drvdata(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	ret = clk_enable(priv->bus_clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}
	ret = clk_enable(priv->can_clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		clk_disable_unprepare(priv->bus_clk);
		return ret;
	}

	priv->write_reg(priv, XCAN_MSR_OFFSET, 0);
	priv->write_reg(priv, XCAN_SRR_OFFSET, XCAN_SRR_CEN_MASK);
	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	if (netif_running(ndev)) {
		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(xcan_dev_pm_ops, xcan_suspend, xcan_resume);

static const struct xcan_devtype_data xcan_zynq_data = {
	.caps = XCAN_CAP_WATERMARK,
};

/* Match table for OF platform binding */
static const struct of_device_id xcan_of_match[] = {
	{ .compatible = "xlnx,zynq-can-1.0", .data = &xcan_zynq_data },
	{ .compatible = "xlnx,axi-can-1.00.a", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, xcan_of_match);

/**
 * xcan_probe - Platform registration call
 * @pdev:	Handle to the platform device structure
 *
 * This function does all the memory allocation and registration for the CAN
 * device.
 *
 * Return: 0 on success and failure value on error
 */
static int xcan_probe(struct platform_device *pdev)
{
	struct resource *res; /* IO mem resources */
	struct net_device *ndev;
	struct xcan_priv *priv;
	const struct of_device_id *of_id;
	int caps = 0;
	void __iomem *addr;
	int ret, rx_max, tx_max, tx_fifo_depth;

	/* Get the virtual base address for the device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto err;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "tx-fifo-depth",
				   &tx_fifo_depth);
	if (ret < 0)
		goto err;

	ret = of_property_read_u32(pdev->dev.of_node, "rx-fifo-depth", &rx_max);
	if (ret < 0)
		goto err;

	of_id = of_match_device(xcan_of_match, &pdev->dev);
	if (of_id) {
		const struct xcan_devtype_data *devtype_data = of_id->data;

		if (devtype_data)
			caps = devtype_data->caps;
	}

	/* There is no way to directly figure out how many frames have been
	 * sent when the TXOK interrupt is processed. If watermark programming
	 * is supported, we can have 2 frames in the FIFO and use TXFEMP
	 * to determine if 1 or 2 frames have been sent.
	 * Theoretically we should be able to use TXFWMEMP to determine up
	 * to 3 frames, but it seems that after putting a second frame in the
	 * FIFO, with watermark at 2 frames, it can happen that TXFWMEMP (less
	 * than 2 frames in FIFO) is set anyway with no TXOK (a frame was
	 * sent), which is not a sensible state - possibly TXFWMEMP is not
	 * completely synchronized with the rest of the bits?
	 */
	if (caps & XCAN_CAP_WATERMARK)
		tx_max = min(tx_fifo_depth, 2);
	else
		tx_max = 1;

	/* Create a CAN device instance */
	ndev = alloc_candev(sizeof(struct xcan_priv), tx_max);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->dev = ndev;
	priv->can.bittiming_const = &xcan_bittiming_const;
	priv->can.do_set_mode = xcan_do_set_mode;
	priv->can.do_get_berr_counter = xcan_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
					CAN_CTRLMODE_BERR_REPORTING;
	priv->reg_base = addr;
	priv->tx_max = tx_max;
	spin_lock_init(&priv->tx_lock);

	/* Get IRQ for the device */
	ndev->irq = platform_get_irq(pdev, 0);
	ndev->flags |= IFF_ECHO;	/* We support local echo */

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &xcan_netdev_ops;

	/* Getting the CAN can_clk info */
	priv->can_clk = devm_clk_get(&pdev->dev, "can_clk");
	if (IS_ERR(priv->can_clk)) {
		dev_err(&pdev->dev, "Device clock not found.\n");
		ret = PTR_ERR(priv->can_clk);
		goto err_free;
	}
	/* Check for type of CAN device */
	if (of_device_is_compatible(pdev->dev.of_node,
				    "xlnx,zynq-can-1.0")) {
		priv->bus_clk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(priv->bus_clk)) {
			dev_err(&pdev->dev, "bus clock not found\n");
			ret = PTR_ERR(priv->bus_clk);
			goto err_free;
		}
	} else {
		priv->bus_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
		if (IS_ERR(priv->bus_clk)) {
			dev_err(&pdev->dev, "bus clock not found\n");
			ret = PTR_ERR(priv->bus_clk);
			goto err_free;
		}
	}

	ret = clk_prepare_enable(priv->can_clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable device clock\n");
		goto err_free;
	}

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable bus clock\n");
		goto err_unprepare_disable_dev;
	}

	priv->write_reg = xcan_write_reg_le;
	priv->read_reg = xcan_read_reg_le;

	if (priv->read_reg(priv, XCAN_SR_OFFSET) != XCAN_SR_CONFIG_MASK) {
		priv->write_reg = xcan_write_reg_be;
		priv->read_reg = xcan_read_reg_be;
	}

	priv->can.clock.freq = clk_get_rate(priv->can_clk);

	netif_napi_add(ndev, &priv->napi, xcan_rx_poll, rx_max);

	ret = register_candev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "fail to register failed (err=%d)\n", ret);
		goto err_unprepare_disable_busclk;
	}

	devm_can_led_init(ndev);
	clk_disable_unprepare(priv->bus_clk);
	clk_disable_unprepare(priv->can_clk);
	netdev_dbg(ndev, "reg_base=0x%p irq=%d clock=%d, tx fifo depth: actual %d, using %d\n",
			priv->reg_base, ndev->irq, priv->can.clock.freq,
			tx_fifo_depth, priv->tx_max);

	return 0;

err_unprepare_disable_busclk:
	clk_disable_unprepare(priv->bus_clk);
err_unprepare_disable_dev:
	clk_disable_unprepare(priv->can_clk);
err_free:
	free_candev(ndev);
err:
	return ret;
}

/**
 * xcan_remove - Unregister the device after releasing the resources
 * @pdev:	Handle to the platform device structure
 *
 * This function frees all the resources allocated to the device.
 * Return: 0 always
 */
static int xcan_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct xcan_priv *priv = netdev_priv(ndev);

	if (set_reset_mode(ndev) < 0)
		netdev_err(ndev, "mode resetting failed!\n");

	unregister_candev(ndev);
	netif_napi_del(&priv->napi);
	free_candev(ndev);

	return 0;
}

static struct platform_driver xcan_driver = {
	.probe = xcan_probe,
	.remove	= xcan_remove,
	.driver	= {
		.name = DRIVER_NAME,
		.pm = &xcan_dev_pm_ops,
		.of_match_table	= xcan_of_match,
	},
};

module_platform_driver(xcan_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Xilinx CAN interface");
