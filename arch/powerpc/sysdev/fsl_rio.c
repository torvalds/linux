/*
 * Freescale MPC85xx/MPC86xx RapidIO support
 *
 * Copyright (C) 2007, 2008 Freescale Semiconductor, Inc.
 * Zhang Wei <wei.zhang@freescale.com>
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/of_platform.h>
#include <linux/delay.h>

#include <asm/io.h>

/* RapidIO definition irq, which read from OF-tree */
#define IRQ_RIO_BELL(m)		(((struct rio_priv *)(m->priv))->bellirq)
#define IRQ_RIO_TX(m)		(((struct rio_priv *)(m->priv))->txirq)
#define IRQ_RIO_RX(m)		(((struct rio_priv *)(m->priv))->rxirq)

#define RIO_ATMU_REGS_OFFSET	0x10c00
#define RIO_P_MSG_REGS_OFFSET	0x11000
#define RIO_S_MSG_REGS_OFFSET	0x13000
#define RIO_ESCSR		0x158
#define RIO_CCSR		0x15c
#define RIO_ISR_AACR		0x10120
#define RIO_ISR_AACR_AA		0x1	/* Accept All ID */
#define RIO_MAINT_WIN_SIZE	0x400000
#define RIO_DBELL_WIN_SIZE	0x1000

#define RIO_MSG_OMR_MUI		0x00000002
#define RIO_MSG_OSR_TE		0x00000080
#define RIO_MSG_OSR_QOI		0x00000020
#define RIO_MSG_OSR_QFI		0x00000010
#define RIO_MSG_OSR_MUB		0x00000004
#define RIO_MSG_OSR_EOMI	0x00000002
#define RIO_MSG_OSR_QEI		0x00000001

#define RIO_MSG_IMR_MI		0x00000002
#define RIO_MSG_ISR_TE		0x00000080
#define RIO_MSG_ISR_QFI		0x00000010
#define RIO_MSG_ISR_DIQI	0x00000001

#define RIO_MSG_DESC_SIZE	32
#define RIO_MSG_BUFFER_SIZE	4096
#define RIO_MIN_TX_RING_SIZE	2
#define RIO_MAX_TX_RING_SIZE	2048
#define RIO_MIN_RX_RING_SIZE	2
#define RIO_MAX_RX_RING_SIZE	2048

#define DOORBELL_DMR_DI		0x00000002
#define DOORBELL_DSR_TE		0x00000080
#define DOORBELL_DSR_QFI	0x00000010
#define DOORBELL_DSR_DIQI	0x00000001
#define DOORBELL_TID_OFFSET	0x02
#define DOORBELL_SID_OFFSET	0x04
#define DOORBELL_INFO_OFFSET	0x06

#define DOORBELL_MESSAGE_SIZE	0x08
#define DBELL_SID(x)		(*(u16 *)(x + DOORBELL_SID_OFFSET))
#define DBELL_TID(x)		(*(u16 *)(x + DOORBELL_TID_OFFSET))
#define DBELL_INF(x)		(*(u16 *)(x + DOORBELL_INFO_OFFSET))

struct rio_atmu_regs {
	u32 rowtar;
	u32 rowtear;
	u32 rowbar;
	u32 pad2;
	u32 rowar;
	u32 pad3[3];
};

struct rio_msg_regs {
	u32 omr;
	u32 osr;
	u32 pad1;
	u32 odqdpar;
	u32 pad2;
	u32 osar;
	u32 odpr;
	u32 odatr;
	u32 odcr;
	u32 pad3;
	u32 odqepar;
	u32 pad4[13];
	u32 imr;
	u32 isr;
	u32 pad5;
	u32 ifqdpar;
	u32 pad6;
	u32 ifqepar;
	u32 pad7[226];
	u32 odmr;
	u32 odsr;
	u32 res0[4];
	u32 oddpr;
	u32 oddatr;
	u32 res1[3];
	u32 odretcr;
	u32 res2[12];
	u32 dmr;
	u32 dsr;
	u32 pad8;
	u32 dqdpar;
	u32 pad9;
	u32 dqepar;
	u32 pad10[26];
	u32 pwmr;
	u32 pwsr;
	u32 pad11;
	u32 pwqbar;
};

struct rio_tx_desc {
	u32 res1;
	u32 saddr;
	u32 dport;
	u32 dattr;
	u32 res2;
	u32 res3;
	u32 dwcnt;
	u32 res4;
};

struct rio_dbell_ring {
	void *virt;
	dma_addr_t phys;
};

struct rio_msg_tx_ring {
	void *virt;
	dma_addr_t phys;
	void *virt_buffer[RIO_MAX_TX_RING_SIZE];
	dma_addr_t phys_buffer[RIO_MAX_TX_RING_SIZE];
	int tx_slot;
	int size;
	void *dev_id;
};

struct rio_msg_rx_ring {
	void *virt;
	dma_addr_t phys;
	void *virt_buffer[RIO_MAX_RX_RING_SIZE];
	int rx_slot;
	int size;
	void *dev_id;
};

struct rio_priv {
	struct device *dev;
	void __iomem *regs_win;
	struct rio_atmu_regs __iomem *atmu_regs;
	struct rio_atmu_regs __iomem *maint_atmu_regs;
	struct rio_atmu_regs __iomem *dbell_atmu_regs;
	void __iomem *dbell_win;
	void __iomem *maint_win;
	struct rio_msg_regs __iomem *msg_regs;
	struct rio_dbell_ring dbell_ring;
	struct rio_msg_tx_ring msg_tx_ring;
	struct rio_msg_rx_ring msg_rx_ring;
	int bellirq;
	int txirq;
	int rxirq;
};

/**
 * fsl_rio_doorbell_send - Send a MPC85xx doorbell message
 * @mport: RapidIO master port info
 * @index: ID of RapidIO interface
 * @destid: Destination ID of target device
 * @data: 16-bit info field of RapidIO doorbell message
 *
 * Sends a MPC85xx doorbell message. Returns %0 on success or
 * %-EINVAL on failure.
 */
static int fsl_rio_doorbell_send(struct rio_mport *mport,
				int index, u16 destid, u16 data)
{
	struct rio_priv *priv = mport->priv;
	pr_debug("fsl_doorbell_send: index %d destid %4.4x data %4.4x\n",
		 index, destid, data);
	switch (mport->phy_type) {
	case RIO_PHY_PARALLEL:
		out_be32(&priv->dbell_atmu_regs->rowtar, destid << 22);
		out_be16(priv->dbell_win, data);
		break;
	case RIO_PHY_SERIAL:
		/* In the serial version silicons, such as MPC8548, MPC8641,
		 * below operations is must be.
		 */
		out_be32(&priv->msg_regs->odmr, 0x00000000);
		out_be32(&priv->msg_regs->odretcr, 0x00000004);
		out_be32(&priv->msg_regs->oddpr, destid << 16);
		out_be32(&priv->msg_regs->oddatr, data);
		out_be32(&priv->msg_regs->odmr, 0x00000001);
		break;
	}

	return 0;
}

/**
 * fsl_local_config_read - Generate a MPC85xx local config space read
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be read into
 *
 * Generates a MPC85xx local configuration space read. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int fsl_local_config_read(struct rio_mport *mport,
				int index, u32 offset, int len, u32 *data)
{
	struct rio_priv *priv = mport->priv;
	pr_debug("fsl_local_config_read: index %d offset %8.8x\n", index,
		 offset);
	*data = in_be32(priv->regs_win + offset);

	return 0;
}

/**
 * fsl_local_config_write - Generate a MPC85xx local config space write
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be written
 *
 * Generates a MPC85xx local configuration space write. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int fsl_local_config_write(struct rio_mport *mport,
				int index, u32 offset, int len, u32 data)
{
	struct rio_priv *priv = mport->priv;
	pr_debug
	    ("fsl_local_config_write: index %d offset %8.8x data %8.8x\n",
	     index, offset, data);
	out_be32(priv->regs_win + offset, data);

	return 0;
}

/**
 * fsl_rio_config_read - Generate a MPC85xx read maintenance transaction
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @val: Location to be read into
 *
 * Generates a MPC85xx read maintenance transaction. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int
fsl_rio_config_read(struct rio_mport *mport, int index, u16 destid,
			u8 hopcount, u32 offset, int len, u32 *val)
{
	struct rio_priv *priv = mport->priv;
	u8 *data;

	pr_debug
	    ("fsl_rio_config_read: index %d destid %d hopcount %d offset %8.8x len %d\n",
	     index, destid, hopcount, offset, len);
	out_be32(&priv->maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | ((offset & ~0x3) >> 9));

	data = (u8 *) priv->maint_win + offset;
	switch (len) {
	case 1:
		*val = in_8((u8 *) data);
		break;
	case 2:
		*val = in_be16((u16 *) data);
		break;
	default:
		*val = in_be32((u32 *) data);
		break;
	}

	return 0;
}

/**
 * fsl_rio_config_write - Generate a MPC85xx write maintenance transaction
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @val: Value to be written
 *
 * Generates an MPC85xx write maintenance transaction. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int
fsl_rio_config_write(struct rio_mport *mport, int index, u16 destid,
			u8 hopcount, u32 offset, int len, u32 val)
{
	struct rio_priv *priv = mport->priv;
	u8 *data;
	pr_debug
	    ("fsl_rio_config_write: index %d destid %d hopcount %d offset %8.8x len %d val %8.8x\n",
	     index, destid, hopcount, offset, len, val);
	out_be32(&priv->maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | ((offset & ~0x3) >> 9));

	data = (u8 *) priv->maint_win + offset;
	switch (len) {
	case 1:
		out_8((u8 *) data, val);
		break;
	case 2:
		out_be16((u16 *) data, val);
		break;
	default:
		out_be32((u32 *) data, val);
		break;
	}

	return 0;
}

/**
 * rio_hw_add_outb_message - Add message to the MPC85xx outbound message queue
 * @mport: Master port with outbound message queue
 * @rdev: Target of outbound message
 * @mbox: Outbound mailbox
 * @buffer: Message to add to outbound queue
 * @len: Length of message
 *
 * Adds the @buffer message to the MPC85xx outbound message queue. Returns
 * %0 on success or %-EINVAL on failure.
 */
int
rio_hw_add_outb_message(struct rio_mport *mport, struct rio_dev *rdev, int mbox,
			void *buffer, size_t len)
{
	struct rio_priv *priv = mport->priv;
	u32 omr;
	struct rio_tx_desc *desc = (struct rio_tx_desc *)priv->msg_tx_ring.virt
					+ priv->msg_tx_ring.tx_slot;
	int ret = 0;

	pr_debug
	    ("RIO: rio_hw_add_outb_message(): destid %4.4x mbox %d buffer %8.8x len %8.8x\n",
	     rdev->destid, mbox, (int)buffer, len);

	if ((len < 8) || (len > RIO_MAX_MSG_SIZE)) {
		ret = -EINVAL;
		goto out;
	}

	/* Copy and clear rest of buffer */
	memcpy(priv->msg_tx_ring.virt_buffer[priv->msg_tx_ring.tx_slot], buffer,
			len);
	if (len < (RIO_MAX_MSG_SIZE - 4))
		memset(priv->msg_tx_ring.virt_buffer[priv->msg_tx_ring.tx_slot]
				+ len, 0, RIO_MAX_MSG_SIZE - len);

	switch (mport->phy_type) {
	case RIO_PHY_PARALLEL:
		/* Set mbox field for message */
		desc->dport = mbox & 0x3;

		/* Enable EOMI interrupt, set priority, and set destid */
		desc->dattr = 0x28000000 | (rdev->destid << 2);
		break;
	case RIO_PHY_SERIAL:
		/* Set mbox field for message, and set destid */
		desc->dport = (rdev->destid << 16) | (mbox & 0x3);

		/* Enable EOMI interrupt and priority */
		desc->dattr = 0x28000000;
		break;
	}

	/* Set transfer size aligned to next power of 2 (in double words) */
	desc->dwcnt = is_power_of_2(len) ? len : 1 << get_bitmask_order(len);

	/* Set snooping and source buffer address */
	desc->saddr = 0x00000004
		| priv->msg_tx_ring.phys_buffer[priv->msg_tx_ring.tx_slot];

	/* Increment enqueue pointer */
	omr = in_be32(&priv->msg_regs->omr);
	out_be32(&priv->msg_regs->omr, omr | RIO_MSG_OMR_MUI);

	/* Go to next descriptor */
	if (++priv->msg_tx_ring.tx_slot == priv->msg_tx_ring.size)
		priv->msg_tx_ring.tx_slot = 0;

      out:
	return ret;
}

EXPORT_SYMBOL_GPL(rio_hw_add_outb_message);

/**
 * fsl_rio_tx_handler - MPC85xx outbound message interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles outbound message interrupts. Executes a register outbound
 * mailbox event handler and acks the interrupt occurrence.
 */
static irqreturn_t
fsl_rio_tx_handler(int irq, void *dev_instance)
{
	int osr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;
	struct rio_priv *priv = port->priv;

	osr = in_be32(&priv->msg_regs->osr);

	if (osr & RIO_MSG_OSR_TE) {
		pr_info("RIO: outbound message transmission error\n");
		out_be32(&priv->msg_regs->osr, RIO_MSG_OSR_TE);
		goto out;
	}

	if (osr & RIO_MSG_OSR_QOI) {
		pr_info("RIO: outbound message queue overflow\n");
		out_be32(&priv->msg_regs->osr, RIO_MSG_OSR_QOI);
		goto out;
	}

	if (osr & RIO_MSG_OSR_EOMI) {
		u32 dqp = in_be32(&priv->msg_regs->odqdpar);
		int slot = (dqp - priv->msg_tx_ring.phys) >> 5;
		port->outb_msg[0].mcback(port, priv->msg_tx_ring.dev_id, -1,
				slot);

		/* Ack the end-of-message interrupt */
		out_be32(&priv->msg_regs->osr, RIO_MSG_OSR_EOMI);
	}

      out:
	return IRQ_HANDLED;
}

/**
 * rio_open_outb_mbox - Initialize MPC85xx outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the outbound mailbox ring
 *
 * Initializes buffer ring, request the outbound message interrupt,
 * and enables the outbound message unit. Returns %0 on success and
 * %-EINVAL or %-ENOMEM on failure.
 */
int rio_open_outb_mbox(struct rio_mport *mport, void *dev_id, int mbox, int entries)
{
	int i, j, rc = 0;
	struct rio_priv *priv = mport->priv;

	if ((entries < RIO_MIN_TX_RING_SIZE) ||
	    (entries > RIO_MAX_TX_RING_SIZE) || (!is_power_of_2(entries))) {
		rc = -EINVAL;
		goto out;
	}

	/* Initialize shadow copy ring */
	priv->msg_tx_ring.dev_id = dev_id;
	priv->msg_tx_ring.size = entries;

	for (i = 0; i < priv->msg_tx_ring.size; i++) {
		priv->msg_tx_ring.virt_buffer[i] =
			dma_alloc_coherent(priv->dev, RIO_MSG_BUFFER_SIZE,
				&priv->msg_tx_ring.phys_buffer[i], GFP_KERNEL);
		if (!priv->msg_tx_ring.virt_buffer[i]) {
			rc = -ENOMEM;
			for (j = 0; j < priv->msg_tx_ring.size; j++)
				if (priv->msg_tx_ring.virt_buffer[j])
					dma_free_coherent(priv->dev,
							RIO_MSG_BUFFER_SIZE,
							priv->msg_tx_ring.
							virt_buffer[j],
							priv->msg_tx_ring.
							phys_buffer[j]);
			goto out;
		}
	}

	/* Initialize outbound message descriptor ring */
	priv->msg_tx_ring.virt = dma_alloc_coherent(priv->dev,
				priv->msg_tx_ring.size * RIO_MSG_DESC_SIZE,
				&priv->msg_tx_ring.phys, GFP_KERNEL);
	if (!priv->msg_tx_ring.virt) {
		rc = -ENOMEM;
		goto out_dma;
	}
	memset(priv->msg_tx_ring.virt, 0,
			priv->msg_tx_ring.size * RIO_MSG_DESC_SIZE);
	priv->msg_tx_ring.tx_slot = 0;

	/* Point dequeue/enqueue pointers at first entry in ring */
	out_be32(&priv->msg_regs->odqdpar, priv->msg_tx_ring.phys);
	out_be32(&priv->msg_regs->odqepar, priv->msg_tx_ring.phys);

	/* Configure for snooping */
	out_be32(&priv->msg_regs->osar, 0x00000004);

	/* Clear interrupt status */
	out_be32(&priv->msg_regs->osr, 0x000000b3);

	/* Hook up outbound message handler */
	rc = request_irq(IRQ_RIO_TX(mport), fsl_rio_tx_handler, 0,
			 "msg_tx", (void *)mport);
	if (rc < 0)
		goto out_irq;

	/*
	 * Configure outbound message unit
	 *      Snooping
	 *      Interrupts (all enabled, except QEIE)
	 *      Chaining mode
	 *      Disable
	 */
	out_be32(&priv->msg_regs->omr, 0x00100220);

	/* Set number of entries */
	out_be32(&priv->msg_regs->omr,
		 in_be32(&priv->msg_regs->omr) |
		 ((get_bitmask_order(entries) - 2) << 12));

	/* Now enable the unit */
	out_be32(&priv->msg_regs->omr, in_be32(&priv->msg_regs->omr) | 0x1);

      out:
	return rc;

      out_irq:
	dma_free_coherent(priv->dev,
			  priv->msg_tx_ring.size * RIO_MSG_DESC_SIZE,
			  priv->msg_tx_ring.virt, priv->msg_tx_ring.phys);

      out_dma:
	for (i = 0; i < priv->msg_tx_ring.size; i++)
		dma_free_coherent(priv->dev, RIO_MSG_BUFFER_SIZE,
				  priv->msg_tx_ring.virt_buffer[i],
				  priv->msg_tx_ring.phys_buffer[i]);

	return rc;
}

/**
 * rio_close_outb_mbox - Shut down MPC85xx outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @mbox: Mailbox to close
 *
 * Disables the outbound message unit, free all buffers, and
 * frees the outbound message interrupt.
 */
void rio_close_outb_mbox(struct rio_mport *mport, int mbox)
{
	struct rio_priv *priv = mport->priv;
	/* Disable inbound message unit */
	out_be32(&priv->msg_regs->omr, 0);

	/* Free ring */
	dma_free_coherent(priv->dev,
			  priv->msg_tx_ring.size * RIO_MSG_DESC_SIZE,
			  priv->msg_tx_ring.virt, priv->msg_tx_ring.phys);

	/* Free interrupt */
	free_irq(IRQ_RIO_TX(mport), (void *)mport);
}

/**
 * fsl_rio_rx_handler - MPC85xx inbound message interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles inbound message interrupts. Executes a registered inbound
 * mailbox event handler and acks the interrupt occurrence.
 */
static irqreturn_t
fsl_rio_rx_handler(int irq, void *dev_instance)
{
	int isr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;
	struct rio_priv *priv = port->priv;

	isr = in_be32(&priv->msg_regs->isr);

	if (isr & RIO_MSG_ISR_TE) {
		pr_info("RIO: inbound message reception error\n");
		out_be32((void *)&priv->msg_regs->isr, RIO_MSG_ISR_TE);
		goto out;
	}

	/* XXX Need to check/dispatch until queue empty */
	if (isr & RIO_MSG_ISR_DIQI) {
		/*
		 * We implement *only* mailbox 0, but can receive messages
		 * for any mailbox/letter to that mailbox destination. So,
		 * make the callback with an unknown/invalid mailbox number
		 * argument.
		 */
		port->inb_msg[0].mcback(port, priv->msg_rx_ring.dev_id, -1, -1);

		/* Ack the queueing interrupt */
		out_be32(&priv->msg_regs->isr, RIO_MSG_ISR_DIQI);
	}

      out:
	return IRQ_HANDLED;
}

/**
 * rio_open_inb_mbox - Initialize MPC85xx inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the inbound mailbox ring
 *
 * Initializes buffer ring, request the inbound message interrupt,
 * and enables the inbound message unit. Returns %0 on success
 * and %-EINVAL or %-ENOMEM on failure.
 */
int rio_open_inb_mbox(struct rio_mport *mport, void *dev_id, int mbox, int entries)
{
	int i, rc = 0;
	struct rio_priv *priv = mport->priv;

	if ((entries < RIO_MIN_RX_RING_SIZE) ||
	    (entries > RIO_MAX_RX_RING_SIZE) || (!is_power_of_2(entries))) {
		rc = -EINVAL;
		goto out;
	}

	/* Initialize client buffer ring */
	priv->msg_rx_ring.dev_id = dev_id;
	priv->msg_rx_ring.size = entries;
	priv->msg_rx_ring.rx_slot = 0;
	for (i = 0; i < priv->msg_rx_ring.size; i++)
		priv->msg_rx_ring.virt_buffer[i] = NULL;

	/* Initialize inbound message ring */
	priv->msg_rx_ring.virt = dma_alloc_coherent(priv->dev,
				priv->msg_rx_ring.size * RIO_MAX_MSG_SIZE,
				&priv->msg_rx_ring.phys, GFP_KERNEL);
	if (!priv->msg_rx_ring.virt) {
		rc = -ENOMEM;
		goto out;
	}

	/* Point dequeue/enqueue pointers at first entry in ring */
	out_be32(&priv->msg_regs->ifqdpar, (u32) priv->msg_rx_ring.phys);
	out_be32(&priv->msg_regs->ifqepar, (u32) priv->msg_rx_ring.phys);

	/* Clear interrupt status */
	out_be32(&priv->msg_regs->isr, 0x00000091);

	/* Hook up inbound message handler */
	rc = request_irq(IRQ_RIO_RX(mport), fsl_rio_rx_handler, 0,
			 "msg_rx", (void *)mport);
	if (rc < 0) {
		dma_free_coherent(priv->dev, RIO_MSG_BUFFER_SIZE,
				  priv->msg_tx_ring.virt_buffer[i],
				  priv->msg_tx_ring.phys_buffer[i]);
		goto out;
	}

	/*
	 * Configure inbound message unit:
	 *      Snooping
	 *      4KB max message size
	 *      Unmask all interrupt sources
	 *      Disable
	 */
	out_be32(&priv->msg_regs->imr, 0x001b0060);

	/* Set number of queue entries */
	setbits32(&priv->msg_regs->imr, (get_bitmask_order(entries) - 2) << 12);

	/* Now enable the unit */
	setbits32(&priv->msg_regs->imr, 0x1);

      out:
	return rc;
}

/**
 * rio_close_inb_mbox - Shut down MPC85xx inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @mbox: Mailbox to close
 *
 * Disables the inbound message unit, free all buffers, and
 * frees the inbound message interrupt.
 */
void rio_close_inb_mbox(struct rio_mport *mport, int mbox)
{
	struct rio_priv *priv = mport->priv;
	/* Disable inbound message unit */
	out_be32(&priv->msg_regs->imr, 0);

	/* Free ring */
	dma_free_coherent(priv->dev, priv->msg_rx_ring.size * RIO_MAX_MSG_SIZE,
			  priv->msg_rx_ring.virt, priv->msg_rx_ring.phys);

	/* Free interrupt */
	free_irq(IRQ_RIO_RX(mport), (void *)mport);
}

/**
 * rio_hw_add_inb_buffer - Add buffer to the MPC85xx inbound message queue
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 * @buf: Buffer to add to inbound queue
 *
 * Adds the @buf buffer to the MPC85xx inbound message queue. Returns
 * %0 on success or %-EINVAL on failure.
 */
int rio_hw_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf)
{
	int rc = 0;
	struct rio_priv *priv = mport->priv;

	pr_debug("RIO: rio_hw_add_inb_buffer(), msg_rx_ring.rx_slot %d\n",
		 priv->msg_rx_ring.rx_slot);

	if (priv->msg_rx_ring.virt_buffer[priv->msg_rx_ring.rx_slot]) {
		printk(KERN_ERR
		       "RIO: error adding inbound buffer %d, buffer exists\n",
		       priv->msg_rx_ring.rx_slot);
		rc = -EINVAL;
		goto out;
	}

	priv->msg_rx_ring.virt_buffer[priv->msg_rx_ring.rx_slot] = buf;
	if (++priv->msg_rx_ring.rx_slot == priv->msg_rx_ring.size)
		priv->msg_rx_ring.rx_slot = 0;

      out:
	return rc;
}

EXPORT_SYMBOL_GPL(rio_hw_add_inb_buffer);

/**
 * rio_hw_get_inb_message - Fetch inbound message from the MPC85xx message unit
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 *
 * Gets the next available inbound message from the inbound message queue.
 * A pointer to the message is returned on success or NULL on failure.
 */
void *rio_hw_get_inb_message(struct rio_mport *mport, int mbox)
{
	struct rio_priv *priv = mport->priv;
	u32 phys_buf, virt_buf;
	void *buf = NULL;
	int buf_idx;

	phys_buf = in_be32(&priv->msg_regs->ifqdpar);

	/* If no more messages, then bail out */
	if (phys_buf == in_be32(&priv->msg_regs->ifqepar))
		goto out2;

	virt_buf = (u32) priv->msg_rx_ring.virt + (phys_buf
						- priv->msg_rx_ring.phys);
	buf_idx = (phys_buf - priv->msg_rx_ring.phys) / RIO_MAX_MSG_SIZE;
	buf = priv->msg_rx_ring.virt_buffer[buf_idx];

	if (!buf) {
		printk(KERN_ERR
		       "RIO: inbound message copy failed, no buffers\n");
		goto out1;
	}

	/* Copy max message size, caller is expected to allocate that big */
	memcpy(buf, (void *)virt_buf, RIO_MAX_MSG_SIZE);

	/* Clear the available buffer */
	priv->msg_rx_ring.virt_buffer[buf_idx] = NULL;

      out1:
	setbits32(&priv->msg_regs->imr, RIO_MSG_IMR_MI);

      out2:
	return buf;
}

EXPORT_SYMBOL_GPL(rio_hw_get_inb_message);

/**
 * fsl_rio_dbell_handler - MPC85xx doorbell interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles doorbell interrupts. Parses a list of registered
 * doorbell event handlers and executes a matching event handler.
 */
static irqreturn_t
fsl_rio_dbell_handler(int irq, void *dev_instance)
{
	int dsr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;
	struct rio_priv *priv = port->priv;

	dsr = in_be32(&priv->msg_regs->dsr);

	if (dsr & DOORBELL_DSR_TE) {
		pr_info("RIO: doorbell reception error\n");
		out_be32(&priv->msg_regs->dsr, DOORBELL_DSR_TE);
		goto out;
	}

	if (dsr & DOORBELL_DSR_QFI) {
		pr_info("RIO: doorbell queue full\n");
		out_be32(&priv->msg_regs->dsr, DOORBELL_DSR_QFI);
		goto out;
	}

	/* XXX Need to check/dispatch until queue empty */
	if (dsr & DOORBELL_DSR_DIQI) {
		u32 dmsg =
		    (u32) priv->dbell_ring.virt +
		    (in_be32(&priv->msg_regs->dqdpar) & 0xfff);
		struct rio_dbell *dbell;
		int found = 0;

		pr_debug
		    ("RIO: processing doorbell, sid %2.2x tid %2.2x info %4.4x\n",
		     DBELL_SID(dmsg), DBELL_TID(dmsg), DBELL_INF(dmsg));

		list_for_each_entry(dbell, &port->dbells, node) {
			if ((dbell->res->start <= DBELL_INF(dmsg)) &&
			    (dbell->res->end >= DBELL_INF(dmsg))) {
				found = 1;
				break;
			}
		}
		if (found) {
			dbell->dinb(port, dbell->dev_id, DBELL_SID(dmsg), DBELL_TID(dmsg),
				    DBELL_INF(dmsg));
		} else {
			pr_debug
			    ("RIO: spurious doorbell, sid %2.2x tid %2.2x info %4.4x\n",
			     DBELL_SID(dmsg), DBELL_TID(dmsg), DBELL_INF(dmsg));
		}
		setbits32(&priv->msg_regs->dmr, DOORBELL_DMR_DI);
		out_be32(&priv->msg_regs->dsr, DOORBELL_DSR_DIQI);
	}

      out:
	return IRQ_HANDLED;
}

/**
 * fsl_rio_doorbell_init - MPC85xx doorbell interface init
 * @mport: Master port implementing the inbound doorbell unit
 *
 * Initializes doorbell unit hardware and inbound DMA buffer
 * ring. Called from fsl_rio_setup(). Returns %0 on success
 * or %-ENOMEM on failure.
 */
static int fsl_rio_doorbell_init(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	int rc = 0;

	/* Map outbound doorbell window immediately after maintenance window */
	priv->dbell_win = ioremap(mport->iores.start + RIO_MAINT_WIN_SIZE,
			    RIO_DBELL_WIN_SIZE);
	if (!priv->dbell_win) {
		printk(KERN_ERR
		       "RIO: unable to map outbound doorbell window\n");
		rc = -ENOMEM;
		goto out;
	}

	/* Initialize inbound doorbells */
	priv->dbell_ring.virt = dma_alloc_coherent(priv->dev, 512 *
		    DOORBELL_MESSAGE_SIZE, &priv->dbell_ring.phys, GFP_KERNEL);
	if (!priv->dbell_ring.virt) {
		printk(KERN_ERR "RIO: unable allocate inbound doorbell ring\n");
		rc = -ENOMEM;
		iounmap(priv->dbell_win);
		goto out;
	}

	/* Point dequeue/enqueue pointers at first entry in ring */
	out_be32(&priv->msg_regs->dqdpar, (u32) priv->dbell_ring.phys);
	out_be32(&priv->msg_regs->dqepar, (u32) priv->dbell_ring.phys);

	/* Clear interrupt status */
	out_be32(&priv->msg_regs->dsr, 0x00000091);

	/* Hook up doorbell handler */
	rc = request_irq(IRQ_RIO_BELL(mport), fsl_rio_dbell_handler, 0,
			 "dbell_rx", (void *)mport);
	if (rc < 0) {
		iounmap(priv->dbell_win);
		dma_free_coherent(priv->dev, 512 * DOORBELL_MESSAGE_SIZE,
				  priv->dbell_ring.virt, priv->dbell_ring.phys);
		printk(KERN_ERR
		       "MPC85xx RIO: unable to request inbound doorbell irq");
		goto out;
	}

	/* Configure doorbells for snooping, 512 entries, and enable */
	out_be32(&priv->msg_regs->dmr, 0x00108161);

      out:
	return rc;
}

static char *cmdline = NULL;

static int fsl_rio_get_hdid(int index)
{
	/* XXX Need to parse multiple entries in some format */
	if (!cmdline)
		return -1;

	return simple_strtol(cmdline, NULL, 0);
}

static int fsl_rio_get_cmdline(char *s)
{
	if (!s)
		return 0;

	cmdline = s;
	return 1;
}

__setup("riohdid=", fsl_rio_get_cmdline);

static inline void fsl_rio_info(struct device *dev, u32 ccsr)
{
	const char *str;
	if (ccsr & 1) {
		/* Serial phy */
		switch (ccsr >> 30) {
		case 0:
			str = "1";
			break;
		case 1:
			str = "4";
			break;
		default:
			str = "Unknown";
			break;;
		}
		dev_info(dev, "Hardware port width: %s\n", str);

		switch ((ccsr >> 27) & 7) {
		case 0:
			str = "Single-lane 0";
			break;
		case 1:
			str = "Single-lane 2";
			break;
		case 2:
			str = "Four-lane";
			break;
		default:
			str = "Unknown";
			break;
		}
		dev_info(dev, "Training connection status: %s\n", str);
	} else {
		/* Parallel phy */
		if (!(ccsr & 0x80000000))
			dev_info(dev, "Output port operating in 8-bit mode\n");
		if (!(ccsr & 0x08000000))
			dev_info(dev, "Input port operating in 8-bit mode\n");
	}
}

/**
 * fsl_rio_setup - Setup Freescale PowerPC RapidIO interface
 * @dev: of_device pointer
 *
 * Initializes MPC85xx RapidIO hardware interface, configures
 * master port with system-specific info, and registers the
 * master port with the RapidIO subsystem.
 */
int fsl_rio_setup(struct of_device *dev)
{
	struct rio_ops *ops;
	struct rio_mport *port;
	struct rio_priv *priv;
	int rc = 0;
	const u32 *dt_range, *cell;
	struct resource regs;
	int rlen;
	u32 ccsr;
	u64 law_start, law_size;
	int paw, aw, sw;

	if (!dev->node) {
		dev_err(&dev->dev, "Device OF-Node is NULL");
		return -EFAULT;
	}

	rc = of_address_to_resource(dev->node, 0, &regs);
	if (rc) {
		dev_err(&dev->dev, "Can't get %s property 'reg'\n",
				dev->node->full_name);
		return -EFAULT;
	}
	dev_info(&dev->dev, "Of-device full name %s\n", dev->node->full_name);
	dev_info(&dev->dev, "Regs: %pR\n", &regs);

	dt_range = of_get_property(dev->node, "ranges", &rlen);
	if (!dt_range) {
		dev_err(&dev->dev, "Can't get %s property 'ranges'\n",
				dev->node->full_name);
		return -EFAULT;
	}

	/* Get node address wide */
	cell = of_get_property(dev->node, "#address-cells", NULL);
	if (cell)
		aw = *cell;
	else
		aw = of_n_addr_cells(dev->node);
	/* Get node size wide */
	cell = of_get_property(dev->node, "#size-cells", NULL);
	if (cell)
		sw = *cell;
	else
		sw = of_n_size_cells(dev->node);
	/* Get parent address wide wide */
	paw = of_n_addr_cells(dev->node);

	law_start = of_read_number(dt_range + aw, paw);
	law_size = of_read_number(dt_range + aw + paw, sw);

	dev_info(&dev->dev, "LAW start 0x%016llx, size 0x%016llx.\n",
			law_start, law_size);

	ops = kmalloc(sizeof(struct rio_ops), GFP_KERNEL);
	ops->lcread = fsl_local_config_read;
	ops->lcwrite = fsl_local_config_write;
	ops->cread = fsl_rio_config_read;
	ops->cwrite = fsl_rio_config_write;
	ops->dsend = fsl_rio_doorbell_send;

	port = kzalloc(sizeof(struct rio_mport), GFP_KERNEL);
	port->id = 0;
	port->index = 0;

	priv = kzalloc(sizeof(struct rio_priv), GFP_KERNEL);
	if (!priv) {
		printk(KERN_ERR "Can't alloc memory for 'priv'\n");
		rc = -ENOMEM;
		goto err;
	}

	INIT_LIST_HEAD(&port->dbells);
	port->iores.start = law_start;
	port->iores.end = law_start + law_size - 1;
	port->iores.flags = IORESOURCE_MEM;
	port->iores.name = "rio_io_win";

	priv->bellirq = irq_of_parse_and_map(dev->node, 2);
	priv->txirq = irq_of_parse_and_map(dev->node, 3);
	priv->rxirq = irq_of_parse_and_map(dev->node, 4);
	dev_info(&dev->dev, "bellirq: %d, txirq: %d, rxirq %d\n", priv->bellirq,
				priv->txirq, priv->rxirq);

	rio_init_dbell_res(&port->riores[RIO_DOORBELL_RESOURCE], 0, 0xffff);
	rio_init_mbox_res(&port->riores[RIO_INB_MBOX_RESOURCE], 0, 0);
	rio_init_mbox_res(&port->riores[RIO_OUTB_MBOX_RESOURCE], 0, 0);
	strcpy(port->name, "RIO0 mport");

	priv->dev = &dev->dev;

	port->ops = ops;
	port->host_deviceid = fsl_rio_get_hdid(port->id);

	port->priv = priv;
	rio_register_mport(port);

	priv->regs_win = ioremap(regs.start, regs.end - regs.start + 1);

	/* Probe the master port phy type */
	ccsr = in_be32(priv->regs_win + RIO_CCSR);
	port->phy_type = (ccsr & 1) ? RIO_PHY_SERIAL : RIO_PHY_PARALLEL;
	dev_info(&dev->dev, "RapidIO PHY type: %s\n",
			(port->phy_type == RIO_PHY_PARALLEL) ? "parallel" :
			((port->phy_type == RIO_PHY_SERIAL) ? "serial" :
			 "unknown"));
	/* Checking the port training status */
	if (in_be32((priv->regs_win + RIO_ESCSR)) & 1) {
		dev_err(&dev->dev, "Port is not ready. "
				   "Try to restart connection...\n");
		switch (port->phy_type) {
		case RIO_PHY_SERIAL:
			/* Disable ports */
			out_be32(priv->regs_win + RIO_CCSR, 0);
			/* Set 1x lane */
			setbits32(priv->regs_win + RIO_CCSR, 0x02000000);
			/* Enable ports */
			setbits32(priv->regs_win + RIO_CCSR, 0x00600000);
			break;
		case RIO_PHY_PARALLEL:
			/* Disable ports */
			out_be32(priv->regs_win + RIO_CCSR, 0x22000000);
			/* Enable ports */
			out_be32(priv->regs_win + RIO_CCSR, 0x44000000);
			break;
		}
		msleep(100);
		if (in_be32((priv->regs_win + RIO_ESCSR)) & 1) {
			dev_err(&dev->dev, "Port restart failed.\n");
			rc = -ENOLINK;
			goto err;
		}
		dev_info(&dev->dev, "Port restart success!\n");
	}
	fsl_rio_info(&dev->dev, ccsr);

	port->sys_size = (in_be32((priv->regs_win + RIO_PEF_CAR))
					& RIO_PEF_CTLS) >> 4;
	dev_info(&dev->dev, "RapidIO Common Transport System size: %d\n",
			port->sys_size ? 65536 : 256);

	priv->atmu_regs = (struct rio_atmu_regs *)(priv->regs_win
					+ RIO_ATMU_REGS_OFFSET);
	priv->maint_atmu_regs = priv->atmu_regs + 1;
	priv->dbell_atmu_regs = priv->atmu_regs + 2;
	priv->msg_regs = (struct rio_msg_regs *)(priv->regs_win +
				((port->phy_type == RIO_PHY_SERIAL) ?
				RIO_S_MSG_REGS_OFFSET : RIO_P_MSG_REGS_OFFSET));

	/* Set to receive any dist ID for serial RapidIO controller. */
	if (port->phy_type == RIO_PHY_SERIAL)
		out_be32((priv->regs_win + RIO_ISR_AACR), RIO_ISR_AACR_AA);

	/* Configure maintenance transaction window */
	out_be32(&priv->maint_atmu_regs->rowbar, law_start >> 12);
	out_be32(&priv->maint_atmu_regs->rowar, 0x80077015);	/* 4M */

	priv->maint_win = ioremap(law_start, RIO_MAINT_WIN_SIZE);

	/* Configure outbound doorbell window */
	out_be32(&priv->dbell_atmu_regs->rowbar,
			(law_start + RIO_MAINT_WIN_SIZE) >> 12);
	out_be32(&priv->dbell_atmu_regs->rowar, 0x8004200b);	/* 4k */
	fsl_rio_doorbell_init(port);

	return 0;
err:
	if (priv)
		iounmap(priv->regs_win);
	kfree(ops);
	kfree(priv);
	kfree(port);
	return rc;
}

/* The probe function for RapidIO peer-to-peer network.
 */
static int __devinit fsl_of_rio_rpn_probe(struct of_device *dev,
				     const struct of_device_id *match)
{
	int rc;
	printk(KERN_INFO "Setting up RapidIO peer-to-peer network %s\n",
			dev->node->full_name);

	rc = fsl_rio_setup(dev);
	if (rc)
		goto out;

	/* Enumerate all registered ports */
	rc = rio_init_mports();
out:
	return rc;
};

static const struct of_device_id fsl_of_rio_rpn_ids[] = {
	{
		.compatible = "fsl,rapidio-delta",
	},
	{},
};

static struct of_platform_driver fsl_of_rio_rpn_driver = {
	.name = "fsl-of-rio",
	.match_table = fsl_of_rio_rpn_ids,
	.probe = fsl_of_rio_rpn_probe,
};

static __init int fsl_of_rio_rpn_init(void)
{
	return of_register_platform_driver(&fsl_of_rio_rpn_driver);
}

subsys_initcall(fsl_of_rio_rpn_init);
