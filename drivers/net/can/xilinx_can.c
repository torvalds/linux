// SPDX-License-Identifier: GPL-2.0-or-later
/* Xilinx CAN device driver
 *
 * Copyright (C) 2012 - 2014 Xilinx, Inc.
 * Copyright (C) 2009 PetaLogix. All rights reserved.
 * Copyright (C) 2017 - 2018 Sandvik Mining and Construction Oy
 *
 * Description:
 * This driver is developed for Axi CAN IP and for Zynq CANPS Controller.
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
#include <linux/pm_runtime.h>

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

	/* not on CAN FD cores */
	XCAN_TXFIFO_OFFSET	= 0x30, /* TX FIFO base */
	XCAN_RXFIFO_OFFSET	= 0x50, /* RX FIFO base */
	XCAN_AFR_OFFSET		= 0x60, /* Acceptance Filter */

	/* only on CAN FD cores */
	XCAN_TRR_OFFSET		= 0x0090, /* TX Buffer Ready Request */
	XCAN_AFR_EXT_OFFSET	= 0x00E0, /* Acceptance Filter */
	XCAN_FSR_OFFSET		= 0x00E8, /* RX FIFO Status */
	XCAN_TXMSG_BASE_OFFSET	= 0x0100, /* TX Message Space */
	XCAN_RXMSG_BASE_OFFSET	= 0x1100, /* RX Message Space */
	XCAN_RXMSG_2_BASE_OFFSET	= 0x2100, /* RX Message Space */
};

#define XCAN_FRAME_ID_OFFSET(frame_base)	((frame_base) + 0x00)
#define XCAN_FRAME_DLC_OFFSET(frame_base)	((frame_base) + 0x04)
#define XCAN_FRAME_DW1_OFFSET(frame_base)	((frame_base) + 0x08)
#define XCAN_FRAME_DW2_OFFSET(frame_base)	((frame_base) + 0x0C)

#define XCAN_CANFD_FRAME_SIZE		0x48
#define XCAN_TXMSG_FRAME_OFFSET(n)	(XCAN_TXMSG_BASE_OFFSET + \
					 XCAN_CANFD_FRAME_SIZE * (n))
#define XCAN_RXMSG_FRAME_OFFSET(n)	(XCAN_RXMSG_BASE_OFFSET + \
					 XCAN_CANFD_FRAME_SIZE * (n))
#define XCAN_RXMSG_2_FRAME_OFFSET(n)	(XCAN_RXMSG_2_BASE_OFFSET + \
					 XCAN_CANFD_FRAME_SIZE * (n))

/* the single TX mailbox used by this driver on CAN FD HW */
#define XCAN_TX_MAILBOX_IDX		0

/* CAN register bit masks - XCAN_<REG>_<BIT>_MASK */
#define XCAN_SRR_CEN_MASK		0x00000002 /* CAN enable */
#define XCAN_SRR_RESET_MASK		0x00000001 /* Soft Reset the CAN core */
#define XCAN_MSR_LBACK_MASK		0x00000002 /* Loop back mode select */
#define XCAN_MSR_SLEEP_MASK		0x00000001 /* Sleep mode select */
#define XCAN_BRPR_BRP_MASK		0x000000FF /* Baud rate prescaler */
#define XCAN_BTR_SJW_MASK		0x00000180 /* Synchronous jump width */
#define XCAN_BTR_TS2_MASK		0x00000070 /* Time segment 2 */
#define XCAN_BTR_TS1_MASK		0x0000000F /* Time segment 1 */
#define XCAN_BTR_SJW_MASK_CANFD		0x000F0000 /* Synchronous jump width */
#define XCAN_BTR_TS2_MASK_CANFD		0x00000F00 /* Time segment 2 */
#define XCAN_BTR_TS1_MASK_CANFD		0x0000003F /* Time segment 1 */
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
#define XCAN_IXR_RXMNF_MASK		0x00020000 /* RX match not finished */
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
#define XCAN_FSR_FL_MASK		0x00003F00 /* RX Fill Level */
#define XCAN_FSR_IRI_MASK		0x00000080 /* RX Increment Read Index */
#define XCAN_FSR_RI_MASK		0x0000001F /* RX Read Index */

/* CAN register bit shift - XCAN_<REG>_<BIT>_SHIFT */
#define XCAN_BTR_SJW_SHIFT		7  /* Synchronous jump width */
#define XCAN_BTR_TS2_SHIFT		4  /* Time segment 2 */
#define XCAN_BTR_SJW_SHIFT_CANFD	16 /* Synchronous jump width */
#define XCAN_BTR_TS2_SHIFT_CANFD	8  /* Time segment 2 */
#define XCAN_IDR_ID1_SHIFT		21 /* Standard Messg Identifier */
#define XCAN_IDR_ID2_SHIFT		1  /* Extended Message Identifier */
#define XCAN_DLCR_DLC_SHIFT		28 /* Data length code */
#define XCAN_ESR_REC_SHIFT		8  /* Rx Error Count */

/* CAN frame length constants */
#define XCAN_FRAME_MAX_DATA_LEN		8
#define XCAN_TIMEOUT			(1 * HZ)

/* TX-FIFO-empty interrupt available */
#define XCAN_FLAG_TXFEMP	0x0001
/* RX Match Not Finished interrupt available */
#define XCAN_FLAG_RXMNF		0x0002
/* Extended acceptance filters with control at 0xE0 */
#define XCAN_FLAG_EXT_FILTERS	0x0004
/* TX mailboxes instead of TX FIFO */
#define XCAN_FLAG_TX_MAILBOXES	0x0008
/* RX FIFO with each buffer in separate registers at 0x1100
 * instead of the regular FIFO at 0x50
 */
#define XCAN_FLAG_RX_FIFO_MULTI	0x0010
#define XCAN_FLAG_CANFD_2	0x0020

struct xcan_devtype_data {
	unsigned int flags;
	const struct can_bittiming_const *bittiming_const;
	const char *bus_clk_name;
	unsigned int btr_ts2_shift;
	unsigned int btr_sjw_shift;
};

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
 * @devtype:			Device type specific constants
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
	struct device *dev;
	void __iomem *reg_base;
	unsigned long irq_flags;
	struct clk *bus_clk;
	struct clk *can_clk;
	struct xcan_devtype_data devtype;
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

static const struct can_bittiming_const xcan_bittiming_const_canfd = {
	.name = DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 64,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

static const struct can_bittiming_const xcan_bittiming_const_canfd2 = {
	.name = DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 256,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
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
 * xcan_rx_int_mask - Get the mask for the receive interrupt
 * @priv:	Driver private data structure
 *
 * Return: The receive interrupt mask used by the driver on this HW
 */
static u32 xcan_rx_int_mask(const struct xcan_priv *priv)
{
	/* RXNEMP is better suited for our use case as it cannot be cleared
	 * while the FIFO is non-empty, but CAN FD HW does not have it
	 */
	if (priv->devtype.flags & XCAN_FLAG_RX_FIFO_MULTI)
		return XCAN_IXR_RXOK_MASK;
	else
		return XCAN_IXR_RXNEMP_MASK;
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
	btr1 |= (bt->phase_seg2 - 1) << priv->devtype.btr_ts2_shift;

	/* Setting Synchronous jump width in BTR Register */
	btr1 |= (bt->sjw - 1) << priv->devtype.btr_sjw_shift;

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
	u32 ier;

	/* Check if it is in reset mode */
	err = set_reset_mode(ndev);
	if (err < 0)
		return err;

	err = xcan_set_bittiming(ndev);
	if (err < 0)
		return err;

	/* Enable interrupts */
	ier = XCAN_IXR_TXOK_MASK | XCAN_IXR_BSOFF_MASK |
		XCAN_IXR_WKUP_MASK | XCAN_IXR_SLP_MASK |
		XCAN_IXR_ERROR_MASK | XCAN_IXR_RXOFLW_MASK |
		XCAN_IXR_ARBLST_MASK | xcan_rx_int_mask(priv);

	if (priv->devtype.flags & XCAN_FLAG_RXMNF)
		ier |= XCAN_IXR_RXMNF_MASK;

	priv->write_reg(priv, XCAN_IER_OFFSET, ier);

	/* Check whether it is loopback mode or normal mode  */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK) {
		reg_msr = XCAN_MSR_LBACK_MASK;
		reg_sr_mask = XCAN_SR_LBACK_MASK;
	} else {
		reg_msr = 0x0;
		reg_sr_mask = XCAN_SR_NORMAL_MASK;
	}

	/* enable the first extended filter, if any, as cores with extended
	 * filtering default to non-receipt if all filters are disabled
	 */
	if (priv->devtype.flags & XCAN_FLAG_EXT_FILTERS)
		priv->write_reg(priv, XCAN_AFR_EXT_OFFSET, 0x00000001);

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
 * xcan_write_frame - Write a frame to HW
 * @skb:		sk_buff pointer that contains data to be Txed
 * @frame_offset:	Register offset to write the frame to
 */
static void xcan_write_frame(struct xcan_priv *priv, struct sk_buff *skb,
			     int frame_offset)
{
	u32 id, dlc, data[2] = {0, 0};
	struct can_frame *cf = (struct can_frame *)skb->data;

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

	priv->write_reg(priv, XCAN_FRAME_ID_OFFSET(frame_offset), id);
	/* If the CAN frame is RTR frame this write triggers transmission
	 * (not on CAN FD)
	 */
	priv->write_reg(priv, XCAN_FRAME_DLC_OFFSET(frame_offset), dlc);
	if (!(cf->can_id & CAN_RTR_FLAG)) {
		priv->write_reg(priv, XCAN_FRAME_DW1_OFFSET(frame_offset),
				data[0]);
		/* If the CAN frame is Standard/Extended frame this
		 * write triggers transmission (not on CAN FD)
		 */
		priv->write_reg(priv, XCAN_FRAME_DW2_OFFSET(frame_offset),
				data[1]);
	}
}

/**
 * xcan_start_xmit_fifo - Starts the transmission (FIFO mode)
 *
 * Return: 0 on success, -ENOSPC if FIFO is full.
 */
static int xcan_start_xmit_fifo(struct sk_buff *skb, struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	unsigned long flags;

	/* Check if the TX buffer is full */
	if (unlikely(priv->read_reg(priv, XCAN_SR_OFFSET) &
			XCAN_SR_TXFLL_MASK))
		return -ENOSPC;

	can_put_echo_skb(skb, ndev, priv->tx_head % priv->tx_max);

	spin_lock_irqsave(&priv->tx_lock, flags);

	priv->tx_head++;

	xcan_write_frame(priv, skb, XCAN_TXFIFO_OFFSET);

	/* Clear TX-FIFO-empty interrupt for xcan_tx_interrupt() */
	if (priv->tx_max > 1)
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_TXFEMP_MASK);

	/* Check if the TX buffer is full */
	if ((priv->tx_head - priv->tx_tail) == priv->tx_max)
		netif_stop_queue(ndev);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return 0;
}

/**
 * xcan_start_xmit_mailbox - Starts the transmission (mailbox mode)
 *
 * Return: 0 on success, -ENOSPC if there is no space
 */
static int xcan_start_xmit_mailbox(struct sk_buff *skb, struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	unsigned long flags;

	if (unlikely(priv->read_reg(priv, XCAN_TRR_OFFSET) &
		     BIT(XCAN_TX_MAILBOX_IDX)))
		return -ENOSPC;

	can_put_echo_skb(skb, ndev, 0);

	spin_lock_irqsave(&priv->tx_lock, flags);

	priv->tx_head++;

	xcan_write_frame(priv, skb,
			 XCAN_TXMSG_FRAME_OFFSET(XCAN_TX_MAILBOX_IDX));

	/* Mark buffer as ready for transmit */
	priv->write_reg(priv, XCAN_TRR_OFFSET, BIT(XCAN_TX_MAILBOX_IDX));

	netif_stop_queue(ndev);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return 0;
}

/**
 * xcan_start_xmit - Starts the transmission
 * @skb:	sk_buff pointer that contains data to be Txed
 * @ndev:	Pointer to net_device structure
 *
 * This function is invoked from upper layers to initiate transmission.
 *
 * Return: NETDEV_TX_OK on success and NETDEV_TX_BUSY when the tx queue is full
 */
static netdev_tx_t xcan_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (priv->devtype.flags & XCAN_FLAG_TX_MAILBOXES)
		ret = xcan_start_xmit_mailbox(skb, ndev);
	else
		ret = xcan_start_xmit_fifo(skb, ndev);

	if (ret < 0) {
		netdev_err(ndev, "BUG!, TX full when queue awake!\n");
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

/**
 * xcan_rx -  Is called from CAN isr to complete the received
 *		frame  processing
 * @ndev:	Pointer to net_device structure
 * @frame_base:	Register offset to the frame to be read
 *
 * This function is invoked from the CAN isr(poll) to process the Rx frames. It
 * does minimal processing and invokes "netif_receive_skb" to complete further
 * processing.
 * Return: 1 on success and 0 on failure.
 */
static int xcan_rx(struct net_device *ndev, int frame_base)
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
	id_xcan = priv->read_reg(priv, XCAN_FRAME_ID_OFFSET(frame_base));
	dlc = priv->read_reg(priv, XCAN_FRAME_DLC_OFFSET(frame_base)) >>
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
	data[0] = priv->read_reg(priv, XCAN_FRAME_DW1_OFFSET(frame_base));
	data[1] = priv->read_reg(priv, XCAN_FRAME_DW2_OFFSET(frame_base));

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
	enum can_state tx_state = txerr >= rxerr ? new_state : 0;
	enum can_state rx_state = txerr <= rxerr ? new_state : 0;

	/* non-ERROR states are handled elsewhere */
	if (WARN_ON(new_state > CAN_STATE_ERROR_PASSIVE))
		return;

	can_change_state(ndev, cf, tx_state, rx_state);

	if (cf) {
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
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

		if (new_state != priv->can.state)
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

	/* Check for RX Match Not Finished interrupt */
	if (isr & XCAN_IXR_RXMNF_MASK) {
		stats->rx_dropped++;
		stats->rx_errors++;
		netdev_err(ndev, "RX match not finished, frame discarded\n");
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= CAN_ERR_CRTL_UNSPEC;
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
 * xcan_rx_fifo_get_next_frame - Get register offset of next RX frame
 *
 * Return: Register offset of the next frame in RX FIFO.
 */
static int xcan_rx_fifo_get_next_frame(struct xcan_priv *priv)
{
	int offset;

	if (priv->devtype.flags & XCAN_FLAG_RX_FIFO_MULTI) {
		u32 fsr;

		/* clear RXOK before the is-empty check so that any newly
		 * received frame will reassert it without a race
		 */
		priv->write_reg(priv, XCAN_ICR_OFFSET, XCAN_IXR_RXOK_MASK);

		fsr = priv->read_reg(priv, XCAN_FSR_OFFSET);

		/* check if RX FIFO is empty */
		if (!(fsr & XCAN_FSR_FL_MASK))
			return -ENOENT;

		if (priv->devtype.flags & XCAN_FLAG_CANFD_2)
			offset = XCAN_RXMSG_2_FRAME_OFFSET(fsr & XCAN_FSR_RI_MASK);
		else
			offset = XCAN_RXMSG_FRAME_OFFSET(fsr & XCAN_FSR_RI_MASK);

	} else {
		/* check if RX FIFO is empty */
		if (!(priv->read_reg(priv, XCAN_ISR_OFFSET) &
		      XCAN_IXR_RXNEMP_MASK))
			return -ENOENT;

		/* frames are read from a static offset */
		offset = XCAN_RXFIFO_OFFSET;
	}

	return offset;
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
	u32 ier;
	int work_done = 0;
	int frame_offset;

	while ((frame_offset = xcan_rx_fifo_get_next_frame(priv)) >= 0 &&
	       (work_done < quota)) {
		work_done += xcan_rx(ndev, frame_offset);

		if (priv->devtype.flags & XCAN_FLAG_RX_FIFO_MULTI)
			/* increment read index */
			priv->write_reg(priv, XCAN_FSR_OFFSET,
					XCAN_FSR_IRI_MASK);
		else
			/* clear rx-not-empty (will actually clear only if
			 * empty)
			 */
			priv->write_reg(priv, XCAN_ICR_OFFSET,
					XCAN_IXR_RXNEMP_MASK);
	}

	if (work_done) {
		can_led_event(ndev, CAN_LED_EVENT_RX);
		xcan_update_error_state_after_rxtx(ndev);
	}

	if (work_done < quota) {
		napi_complete_done(napi, work_done);
		ier = priv->read_reg(priv, XCAN_IER_OFFSET);
		ier |= xcan_rx_int_mask(priv);
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
		stats->tx_bytes += can_get_echo_skb(ndev, priv->tx_tail %
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
	u32 rx_int_mask = xcan_rx_int_mask(priv);

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
			    XCAN_IXR_BSOFF_MASK | XCAN_IXR_ARBLST_MASK |
			    XCAN_IXR_RXMNF_MASK);
	if (isr_errors) {
		priv->write_reg(priv, XCAN_ICR_OFFSET, isr_errors);
		xcan_err_interrupt(ndev, isr);
	}

	/* Check for the type of receive interrupt and Processing it */
	if (isr & rx_int_mask) {
		ier = priv->read_reg(priv, XCAN_IER_OFFSET);
		ier &= ~rx_int_mask;
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

	/* Disable interrupts and leave the can in configuration mode */
	set_reset_mode(ndev);
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

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
				__func__, ret);
		return ret;
	}

	ret = request_irq(ndev->irq, xcan_interrupt, priv->irq_flags,
			ndev->name, ndev);
	if (ret < 0) {
		netdev_err(ndev, "irq allocation for CAN failed\n");
		goto err;
	}

	/* Set chip into reset mode */
	ret = set_reset_mode(ndev);
	if (ret < 0) {
		netdev_err(ndev, "mode resetting failed!\n");
		goto err_irq;
	}

	/* Common open */
	ret = open_candev(ndev);
	if (ret)
		goto err_irq;

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
err_irq:
	free_irq(ndev->irq, ndev);
err:
	pm_runtime_put(priv->dev);

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
	free_irq(ndev->irq, ndev);
	close_candev(ndev);

	can_led_event(ndev, CAN_LED_EVENT_STOP);
	pm_runtime_put(priv->dev);

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

	ret = pm_runtime_get_sync(priv->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
				__func__, ret);
		return ret;
	}

	bec->txerr = priv->read_reg(priv, XCAN_ECR_OFFSET) & XCAN_ECR_TEC_MASK;
	bec->rxerr = ((priv->read_reg(priv, XCAN_ECR_OFFSET) &
			XCAN_ECR_REC_MASK) >> XCAN_ESR_REC_SHIFT);

	pm_runtime_put(priv->dev);

	return 0;
}


static const struct net_device_ops xcan_netdev_ops = {
	.ndo_open	= xcan_open,
	.ndo_stop	= xcan_close,
	.ndo_start_xmit	= xcan_start_xmit,
	.ndo_change_mtu	= can_change_mtu,
};

/**
 * xcan_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	if (netif_running(ndev)) {
		netif_stop_queue(ndev);
		netif_device_detach(ndev);
		xcan_chip_stop(ndev);
	}

	return pm_runtime_force_suspend(dev);
}

/**
 * xcan_resume - Resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "pm_runtime_force_resume failed on resume\n");
		return ret;
	}

	if (netif_running(ndev)) {
		ret = xcan_chip_start(ndev);
		if (ret) {
			dev_err(dev, "xcan_chip_start failed on resume\n");
			return ret;
		}

		netif_device_attach(ndev);
		netif_start_queue(ndev);
	}

	return 0;
}

/**
 * xcan_runtime_suspend - Runtime suspend method for the driver
 * @dev:	Address of the device structure
 *
 * Put the driver into low power mode.
 * Return: 0 always
 */
static int __maybe_unused xcan_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct xcan_priv *priv = netdev_priv(ndev);

	clk_disable_unprepare(priv->bus_clk);
	clk_disable_unprepare(priv->can_clk);

	return 0;
}

/**
 * xcan_runtime_resume - Runtime resume from suspend
 * @dev:	Address of the device structure
 *
 * Resume operation after suspend.
 * Return: 0 on success and failure value on error
 */
static int __maybe_unused xcan_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct xcan_priv *priv = netdev_priv(ndev);
	int ret;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}
	ret = clk_prepare_enable(priv->can_clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		clk_disable_unprepare(priv->bus_clk);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops xcan_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xcan_suspend, xcan_resume)
	SET_RUNTIME_PM_OPS(xcan_runtime_suspend, xcan_runtime_resume, NULL)
};

static const struct xcan_devtype_data xcan_zynq_data = {
	.bittiming_const = &xcan_bittiming_const,
	.btr_ts2_shift = XCAN_BTR_TS2_SHIFT,
	.btr_sjw_shift = XCAN_BTR_SJW_SHIFT,
	.bus_clk_name = "pclk",
};

static const struct xcan_devtype_data xcan_axi_data = {
	.bittiming_const = &xcan_bittiming_const,
	.btr_ts2_shift = XCAN_BTR_TS2_SHIFT,
	.btr_sjw_shift = XCAN_BTR_SJW_SHIFT,
	.bus_clk_name = "s_axi_aclk",
};

static const struct xcan_devtype_data xcan_canfd_data = {
	.flags = XCAN_FLAG_EXT_FILTERS |
		 XCAN_FLAG_RXMNF |
		 XCAN_FLAG_TX_MAILBOXES |
		 XCAN_FLAG_RX_FIFO_MULTI,
	.bittiming_const = &xcan_bittiming_const_canfd,
	.btr_ts2_shift = XCAN_BTR_TS2_SHIFT_CANFD,
	.btr_sjw_shift = XCAN_BTR_SJW_SHIFT_CANFD,
	.bus_clk_name = "s_axi_aclk",
};

static const struct xcan_devtype_data xcan_canfd2_data = {
	.flags = XCAN_FLAG_EXT_FILTERS |
		 XCAN_FLAG_RXMNF |
		 XCAN_FLAG_TX_MAILBOXES |
		 XCAN_FLAG_CANFD_2 |
		 XCAN_FLAG_RX_FIFO_MULTI,
	.bittiming_const = &xcan_bittiming_const_canfd2,
	.btr_ts2_shift = XCAN_BTR_TS2_SHIFT_CANFD,
	.btr_sjw_shift = XCAN_BTR_SJW_SHIFT_CANFD,
	.bus_clk_name = "s_axi_aclk",
};

/* Match table for OF platform binding */
static const struct of_device_id xcan_of_match[] = {
	{ .compatible = "xlnx,zynq-can-1.0", .data = &xcan_zynq_data },
	{ .compatible = "xlnx,axi-can-1.00.a", .data = &xcan_axi_data },
	{ .compatible = "xlnx,canfd-1.0", .data = &xcan_canfd_data },
	{ .compatible = "xlnx,canfd-2.0", .data = &xcan_canfd2_data },
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
	const struct xcan_devtype_data *devtype = &xcan_axi_data;
	void __iomem *addr;
	int ret;
	int rx_max, tx_max;
	int hw_tx_max, hw_rx_max;
	const char *hw_tx_max_property;

	/* Get the virtual base address for the device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto err;
	}

	of_id = of_match_device(xcan_of_match, &pdev->dev);
	if (of_id && of_id->data)
		devtype = of_id->data;

	hw_tx_max_property = devtype->flags & XCAN_FLAG_TX_MAILBOXES ?
			     "tx-mailbox-count" : "tx-fifo-depth";

	ret = of_property_read_u32(pdev->dev.of_node, hw_tx_max_property,
				   &hw_tx_max);
	if (ret < 0) {
		dev_err(&pdev->dev, "missing %s property\n",
			hw_tx_max_property);
		goto err;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "rx-fifo-depth",
				   &hw_rx_max);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"missing rx-fifo-depth property (mailbox mode is not supported)\n");
		goto err;
	}

	/* With TX FIFO:
	 *
	 * There is no way to directly figure out how many frames have been
	 * sent when the TXOK interrupt is processed. If TXFEMP
	 * is supported, we can have 2 frames in the FIFO and use TXFEMP
	 * to determine if 1 or 2 frames have been sent.
	 * Theoretically we should be able to use TXFWMEMP to determine up
	 * to 3 frames, but it seems that after putting a second frame in the
	 * FIFO, with watermark at 2 frames, it can happen that TXFWMEMP (less
	 * than 2 frames in FIFO) is set anyway with no TXOK (a frame was
	 * sent), which is not a sensible state - possibly TXFWMEMP is not
	 * completely synchronized with the rest of the bits?
	 *
	 * With TX mailboxes:
	 *
	 * HW sends frames in CAN ID priority order. To preserve FIFO ordering
	 * we submit frames one at a time.
	 */
	if (!(devtype->flags & XCAN_FLAG_TX_MAILBOXES) &&
	    (devtype->flags & XCAN_FLAG_TXFEMP))
		tx_max = min(hw_tx_max, 2);
	else
		tx_max = 1;

	rx_max = hw_rx_max;

	/* Create a CAN device instance */
	ndev = alloc_candev(sizeof(struct xcan_priv), tx_max);
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->dev = &pdev->dev;
	priv->can.bittiming_const = devtype->bittiming_const;
	priv->can.do_set_mode = xcan_do_set_mode;
	priv->can.do_get_berr_counter = xcan_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
					CAN_CTRLMODE_BERR_REPORTING;
	priv->reg_base = addr;
	priv->tx_max = tx_max;
	priv->devtype = *devtype;
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

	priv->bus_clk = devm_clk_get(&pdev->dev, devtype->bus_clk_name);
	if (IS_ERR(priv->bus_clk)) {
		dev_err(&pdev->dev, "bus clock not found\n");
		ret = PTR_ERR(priv->bus_clk);
		goto err_free;
	}

	priv->write_reg = xcan_write_reg_le;
	priv->read_reg = xcan_read_reg_le;

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		netdev_err(ndev, "%s: pm_runtime_get failed(%d)\n",
			__func__, ret);
		goto err_pmdisable;
	}

	if (priv->read_reg(priv, XCAN_SR_OFFSET) != XCAN_SR_CONFIG_MASK) {
		priv->write_reg = xcan_write_reg_be;
		priv->read_reg = xcan_read_reg_be;
	}

	priv->can.clock.freq = clk_get_rate(priv->can_clk);

	netif_napi_add(ndev, &priv->napi, xcan_rx_poll, rx_max);

	ret = register_candev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "fail to register failed (err=%d)\n", ret);
		goto err_disableclks;
	}

	devm_can_led_init(ndev);

	pm_runtime_put(&pdev->dev);

	netdev_dbg(ndev, "reg_base=0x%p irq=%d clock=%d, tx buffers: actual %d, using %d\n",
		   priv->reg_base, ndev->irq, priv->can.clock.freq,
		   hw_tx_max, priv->tx_max);

	return 0;

err_disableclks:
	pm_runtime_put(priv->dev);
err_pmdisable:
	pm_runtime_disable(&pdev->dev);
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

	unregister_candev(ndev);
	pm_runtime_disable(&pdev->dev);
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
