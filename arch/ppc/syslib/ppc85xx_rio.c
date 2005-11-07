/*
 * MPC85xx RapidIO support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>

#include <asm/io.h>

#define RIO_REGS_BASE		(CCSRBAR + 0xc0000)
#define RIO_ATMU_REGS_OFFSET	0x10c00
#define RIO_MSG_REGS_OFFSET	0x11000
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
#define DOORBELL_TID_OFFSET	0x03
#define DOORBELL_SID_OFFSET	0x05
#define DOORBELL_INFO_OFFSET	0x06

#define DOORBELL_MESSAGE_SIZE	0x08
#define DBELL_SID(x)		(*(u8 *)(x + DOORBELL_SID_OFFSET))
#define DBELL_TID(x)		(*(u8 *)(x + DOORBELL_TID_OFFSET))
#define DBELL_INF(x)		(*(u16 *)(x + DOORBELL_INFO_OFFSET))

#define is_power_of_2(x)	(((x) & ((x) - 1)) == 0)

struct rio_atmu_regs {
	u32 rowtar;
	u32 pad1;
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
	u32 pad7[250];
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

static u32 regs_win;
static struct rio_atmu_regs *atmu_regs;
static struct rio_atmu_regs *maint_atmu_regs;
static struct rio_atmu_regs *dbell_atmu_regs;
static u32 dbell_win;
static u32 maint_win;
static struct rio_msg_regs *msg_regs;

static struct rio_dbell_ring {
	void *virt;
	dma_addr_t phys;
} dbell_ring;

static struct rio_msg_tx_ring {
	void *virt;
	dma_addr_t phys;
	void *virt_buffer[RIO_MAX_TX_RING_SIZE];
	dma_addr_t phys_buffer[RIO_MAX_TX_RING_SIZE];
	int tx_slot;
	int size;
	void *dev_id;
} msg_tx_ring;

static struct rio_msg_rx_ring {
	void *virt;
	dma_addr_t phys;
	void *virt_buffer[RIO_MAX_RX_RING_SIZE];
	int rx_slot;
	int size;
	void *dev_id;
} msg_rx_ring;

/**
 * mpc85xx_rio_doorbell_send - Send a MPC85xx doorbell message
 * @index: ID of RapidIO interface
 * @destid: Destination ID of target device
 * @data: 16-bit info field of RapidIO doorbell message
 *
 * Sends a MPC85xx doorbell message. Returns %0 on success or
 * %-EINVAL on failure.
 */
static int mpc85xx_rio_doorbell_send(int index, u16 destid, u16 data)
{
	pr_debug("mpc85xx_doorbell_send: index %d destid %4.4x data %4.4x\n",
		 index, destid, data);
	out_be32((void *)&dbell_atmu_regs->rowtar, destid << 22);
	out_be16((void *)(dbell_win), data);

	return 0;
}

/**
 * mpc85xx_local_config_read - Generate a MPC85xx local config space read
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be read into
 *
 * Generates a MPC85xx local configuration space read. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int mpc85xx_local_config_read(int index, u32 offset, int len, u32 * data)
{
	pr_debug("mpc85xx_local_config_read: index %d offset %8.8x\n", index,
		 offset);
	*data = in_be32((void *)(regs_win + offset));

	return 0;
}

/**
 * mpc85xx_local_config_write - Generate a MPC85xx local config space write
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be written
 *
 * Generates a MPC85xx local configuration space write. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int mpc85xx_local_config_write(int index, u32 offset, int len, u32 data)
{
	pr_debug
	    ("mpc85xx_local_config_write: index %d offset %8.8x data %8.8x\n",
	     index, offset, data);
	out_be32((void *)(regs_win + offset), data);

	return 0;
}

/**
 * mpc85xx_rio_config_read - Generate a MPC85xx read maintenance transaction
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
mpc85xx_rio_config_read(int index, u16 destid, u8 hopcount, u32 offset, int len,
			u32 * val)
{
	u8 *data;

	pr_debug
	    ("mpc85xx_rio_config_read: index %d destid %d hopcount %d offset %8.8x len %d\n",
	     index, destid, hopcount, offset, len);
	out_be32((void *)&maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | ((offset & ~0x3) >> 9));

	data = (u8 *) maint_win + offset;
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
 * mpc85xx_rio_config_write - Generate a MPC85xx write maintenance transaction
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
mpc85xx_rio_config_write(int index, u16 destid, u8 hopcount, u32 offset,
			 int len, u32 val)
{
	u8 *data;
	pr_debug
	    ("mpc85xx_rio_config_write: index %d destid %d hopcount %d offset %8.8x len %d val %8.8x\n",
	     index, destid, hopcount, offset, len, val);
	out_be32((void *)&maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | ((offset & ~0x3) >> 9));

	data = (u8 *) maint_win + offset;
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
	u32 omr;
	struct rio_tx_desc *desc =
	    (struct rio_tx_desc *)msg_tx_ring.virt + msg_tx_ring.tx_slot;
	int ret = 0;

	pr_debug
	    ("RIO: rio_hw_add_outb_message(): destid %4.4x mbox %d buffer %8.8x len %8.8x\n",
	     rdev->destid, mbox, (int)buffer, len);

	if ((len < 8) || (len > RIO_MAX_MSG_SIZE)) {
		ret = -EINVAL;
		goto out;
	}

	/* Copy and clear rest of buffer */
	memcpy(msg_tx_ring.virt_buffer[msg_tx_ring.tx_slot], buffer, len);
	if (len < (RIO_MAX_MSG_SIZE - 4))
		memset((void *)((u32) msg_tx_ring.
				virt_buffer[msg_tx_ring.tx_slot] + len), 0,
		       RIO_MAX_MSG_SIZE - len);

	/* Set mbox field for message */
	desc->dport = mbox & 0x3;

	/* Enable EOMI interrupt, set priority, and set destid */
	desc->dattr = 0x28000000 | (rdev->destid << 2);

	/* Set transfer size aligned to next power of 2 (in double words) */
	desc->dwcnt = is_power_of_2(len) ? len : 1 << get_bitmask_order(len);

	/* Set snooping and source buffer address */
	desc->saddr = 0x00000004 | msg_tx_ring.phys_buffer[msg_tx_ring.tx_slot];

	/* Increment enqueue pointer */
	omr = in_be32((void *)&msg_regs->omr);
	out_be32((void *)&msg_regs->omr, omr | RIO_MSG_OMR_MUI);

	/* Go to next descriptor */
	if (++msg_tx_ring.tx_slot == msg_tx_ring.size)
		msg_tx_ring.tx_slot = 0;

      out:
	return ret;
}

EXPORT_SYMBOL_GPL(rio_hw_add_outb_message);

/**
 * mpc85xx_rio_tx_handler - MPC85xx outbound message interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 * @regs: Register context
 *
 * Handles outbound message interrupts. Executes a register outbound
 * mailbox event handler and acks the interrupt occurence.
 */
static irqreturn_t
mpc85xx_rio_tx_handler(int irq, void *dev_instance, struct pt_regs *regs)
{
	int osr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;

	osr = in_be32((void *)&msg_regs->osr);

	if (osr & RIO_MSG_OSR_TE) {
		pr_info("RIO: outbound message transmission error\n");
		out_be32((void *)&msg_regs->osr, RIO_MSG_OSR_TE);
		goto out;
	}

	if (osr & RIO_MSG_OSR_QOI) {
		pr_info("RIO: outbound message queue overflow\n");
		out_be32((void *)&msg_regs->osr, RIO_MSG_OSR_QOI);
		goto out;
	}

	if (osr & RIO_MSG_OSR_EOMI) {
		u32 dqp = in_be32((void *)&msg_regs->odqdpar);
		int slot = (dqp - msg_tx_ring.phys) >> 5;
		port->outb_msg[0].mcback(port, msg_tx_ring.dev_id, -1, slot);

		/* Ack the end-of-message interrupt */
		out_be32((void *)&msg_regs->osr, RIO_MSG_OSR_EOMI);
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

	if ((entries < RIO_MIN_TX_RING_SIZE) ||
	    (entries > RIO_MAX_TX_RING_SIZE) || (!is_power_of_2(entries))) {
		rc = -EINVAL;
		goto out;
	}

	/* Initialize shadow copy ring */
	msg_tx_ring.dev_id = dev_id;
	msg_tx_ring.size = entries;

	for (i = 0; i < msg_tx_ring.size; i++) {
		if (!
		    (msg_tx_ring.virt_buffer[i] =
		     dma_alloc_coherent(NULL, RIO_MSG_BUFFER_SIZE,
					&msg_tx_ring.phys_buffer[i],
					GFP_KERNEL))) {
			rc = -ENOMEM;
			for (j = 0; j < msg_tx_ring.size; j++)
				if (msg_tx_ring.virt_buffer[j])
					dma_free_coherent(NULL,
							  RIO_MSG_BUFFER_SIZE,
							  msg_tx_ring.
							  virt_buffer[j],
							  msg_tx_ring.
							  phys_buffer[j]);
			goto out;
		}
	}

	/* Initialize outbound message descriptor ring */
	if (!(msg_tx_ring.virt = dma_alloc_coherent(NULL,
						    msg_tx_ring.size *
						    RIO_MSG_DESC_SIZE,
						    &msg_tx_ring.phys,
						    GFP_KERNEL))) {
		rc = -ENOMEM;
		goto out_dma;
	}
	memset(msg_tx_ring.virt, 0, msg_tx_ring.size * RIO_MSG_DESC_SIZE);
	msg_tx_ring.tx_slot = 0;

	/* Point dequeue/enqueue pointers at first entry in ring */
	out_be32((void *)&msg_regs->odqdpar, msg_tx_ring.phys);
	out_be32((void *)&msg_regs->odqepar, msg_tx_ring.phys);

	/* Configure for snooping */
	out_be32((void *)&msg_regs->osar, 0x00000004);

	/* Clear interrupt status */
	out_be32((void *)&msg_regs->osr, 0x000000b3);

	/* Hook up outbound message handler */
	if ((rc =
	     request_irq(MPC85xx_IRQ_RIO_TX, mpc85xx_rio_tx_handler, 0,
			 "msg_tx", (void *)mport)) < 0)
		goto out_irq;

	/*
	 * Configure outbound message unit
	 *      Snooping
	 *      Interrupts (all enabled, except QEIE)
	 *      Chaining mode
	 *      Disable
	 */
	out_be32((void *)&msg_regs->omr, 0x00100220);

	/* Set number of entries */
	out_be32((void *)&msg_regs->omr,
		 in_be32((void *)&msg_regs->omr) |
		 ((get_bitmask_order(entries) - 2) << 12));

	/* Now enable the unit */
	out_be32((void *)&msg_regs->omr, in_be32((void *)&msg_regs->omr) | 0x1);

      out:
	return rc;

      out_irq:
	dma_free_coherent(NULL, msg_tx_ring.size * RIO_MSG_DESC_SIZE,
			  msg_tx_ring.virt, msg_tx_ring.phys);

      out_dma:
	for (i = 0; i < msg_tx_ring.size; i++)
		dma_free_coherent(NULL, RIO_MSG_BUFFER_SIZE,
				  msg_tx_ring.virt_buffer[i],
				  msg_tx_ring.phys_buffer[i]);

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
	/* Disable inbound message unit */
	out_be32((void *)&msg_regs->omr, 0);

	/* Free ring */
	dma_free_coherent(NULL, msg_tx_ring.size * RIO_MSG_DESC_SIZE,
			  msg_tx_ring.virt, msg_tx_ring.phys);

	/* Free interrupt */
	free_irq(MPC85xx_IRQ_RIO_TX, (void *)mport);
}

/**
 * mpc85xx_rio_rx_handler - MPC85xx inbound message interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 * @regs: Register context
 *
 * Handles inbound message interrupts. Executes a registered inbound
 * mailbox event handler and acks the interrupt occurence.
 */
static irqreturn_t
mpc85xx_rio_rx_handler(int irq, void *dev_instance, struct pt_regs *regs)
{
	int isr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;

	isr = in_be32((void *)&msg_regs->isr);

	if (isr & RIO_MSG_ISR_TE) {
		pr_info("RIO: inbound message reception error\n");
		out_be32((void *)&msg_regs->isr, RIO_MSG_ISR_TE);
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
		port->inb_msg[0].mcback(port, msg_rx_ring.dev_id, -1, -1);

		/* Ack the queueing interrupt */
		out_be32((void *)&msg_regs->isr, RIO_MSG_ISR_DIQI);
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

	if ((entries < RIO_MIN_RX_RING_SIZE) ||
	    (entries > RIO_MAX_RX_RING_SIZE) || (!is_power_of_2(entries))) {
		rc = -EINVAL;
		goto out;
	}

	/* Initialize client buffer ring */
	msg_rx_ring.dev_id = dev_id;
	msg_rx_ring.size = entries;
	msg_rx_ring.rx_slot = 0;
	for (i = 0; i < msg_rx_ring.size; i++)
		msg_rx_ring.virt_buffer[i] = NULL;

	/* Initialize inbound message ring */
	if (!(msg_rx_ring.virt = dma_alloc_coherent(NULL,
						    msg_rx_ring.size *
						    RIO_MAX_MSG_SIZE,
						    &msg_rx_ring.phys,
						    GFP_KERNEL))) {
		rc = -ENOMEM;
		goto out;
	}

	/* Point dequeue/enqueue pointers at first entry in ring */
	out_be32((void *)&msg_regs->ifqdpar, (u32) msg_rx_ring.phys);
	out_be32((void *)&msg_regs->ifqepar, (u32) msg_rx_ring.phys);

	/* Clear interrupt status */
	out_be32((void *)&msg_regs->isr, 0x00000091);

	/* Hook up inbound message handler */
	if ((rc =
	     request_irq(MPC85xx_IRQ_RIO_RX, mpc85xx_rio_rx_handler, 0,
			 "msg_rx", (void *)mport)) < 0) {
		dma_free_coherent(NULL, RIO_MSG_BUFFER_SIZE,
				  msg_tx_ring.virt_buffer[i],
				  msg_tx_ring.phys_buffer[i]);
		goto out;
	}

	/*
	 * Configure inbound message unit:
	 *      Snooping
	 *      4KB max message size
	 *      Unmask all interrupt sources
	 *      Disable
	 */
	out_be32((void *)&msg_regs->imr, 0x001b0060);

	/* Set number of queue entries */
	out_be32((void *)&msg_regs->imr,
		 in_be32((void *)&msg_regs->imr) |
		 ((get_bitmask_order(entries) - 2) << 12));

	/* Now enable the unit */
	out_be32((void *)&msg_regs->imr, in_be32((void *)&msg_regs->imr) | 0x1);

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
	/* Disable inbound message unit */
	out_be32((void *)&msg_regs->imr, 0);

	/* Free ring */
	dma_free_coherent(NULL, msg_rx_ring.size * RIO_MAX_MSG_SIZE,
			  msg_rx_ring.virt, msg_rx_ring.phys);

	/* Free interrupt */
	free_irq(MPC85xx_IRQ_RIO_RX, (void *)mport);
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

	pr_debug("RIO: rio_hw_add_inb_buffer(), msg_rx_ring.rx_slot %d\n",
		 msg_rx_ring.rx_slot);

	if (msg_rx_ring.virt_buffer[msg_rx_ring.rx_slot]) {
		printk(KERN_ERR
		       "RIO: error adding inbound buffer %d, buffer exists\n",
		       msg_rx_ring.rx_slot);
		rc = -EINVAL;
		goto out;
	}

	msg_rx_ring.virt_buffer[msg_rx_ring.rx_slot] = buf;
	if (++msg_rx_ring.rx_slot == msg_rx_ring.size)
		msg_rx_ring.rx_slot = 0;

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
	u32 imr;
	u32 phys_buf, virt_buf;
	void *buf = NULL;
	int buf_idx;

	phys_buf = in_be32((void *)&msg_regs->ifqdpar);

	/* If no more messages, then bail out */
	if (phys_buf == in_be32((void *)&msg_regs->ifqepar))
		goto out2;

	virt_buf = (u32) msg_rx_ring.virt + (phys_buf - msg_rx_ring.phys);
	buf_idx = (phys_buf - msg_rx_ring.phys) / RIO_MAX_MSG_SIZE;
	buf = msg_rx_ring.virt_buffer[buf_idx];

	if (!buf) {
		printk(KERN_ERR
		       "RIO: inbound message copy failed, no buffers\n");
		goto out1;
	}

	/* Copy max message size, caller is expected to allocate that big */
	memcpy(buf, (void *)virt_buf, RIO_MAX_MSG_SIZE);

	/* Clear the available buffer */
	msg_rx_ring.virt_buffer[buf_idx] = NULL;

      out1:
	imr = in_be32((void *)&msg_regs->imr);
	out_be32((void *)&msg_regs->imr, imr | RIO_MSG_IMR_MI);

      out2:
	return buf;
}

EXPORT_SYMBOL_GPL(rio_hw_get_inb_message);

/**
 * mpc85xx_rio_dbell_handler - MPC85xx doorbell interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 * @regs: Register context
 *
 * Handles doorbell interrupts. Parses a list of registered
 * doorbell event handlers and executes a matching event handler.
 */
static irqreturn_t
mpc85xx_rio_dbell_handler(int irq, void *dev_instance, struct pt_regs *regs)
{
	int dsr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;

	dsr = in_be32((void *)&msg_regs->dsr);

	if (dsr & DOORBELL_DSR_TE) {
		pr_info("RIO: doorbell reception error\n");
		out_be32((void *)&msg_regs->dsr, DOORBELL_DSR_TE);
		goto out;
	}

	if (dsr & DOORBELL_DSR_QFI) {
		pr_info("RIO: doorbell queue full\n");
		out_be32((void *)&msg_regs->dsr, DOORBELL_DSR_QFI);
		goto out;
	}

	/* XXX Need to check/dispatch until queue empty */
	if (dsr & DOORBELL_DSR_DIQI) {
		u32 dmsg =
		    (u32) dbell_ring.virt +
		    (in_be32((void *)&msg_regs->dqdpar) & 0xfff);
		u32 dmr;
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
		dmr = in_be32((void *)&msg_regs->dmr);
		out_be32((void *)&msg_regs->dmr, dmr | DOORBELL_DMR_DI);
		out_be32((void *)&msg_regs->dsr, DOORBELL_DSR_DIQI);
	}

      out:
	return IRQ_HANDLED;
}

/**
 * mpc85xx_rio_doorbell_init - MPC85xx doorbell interface init
 * @mport: Master port implementing the inbound doorbell unit
 *
 * Initializes doorbell unit hardware and inbound DMA buffer
 * ring. Called from mpc85xx_rio_setup(). Returns %0 on success
 * or %-ENOMEM on failure.
 */
static int mpc85xx_rio_doorbell_init(struct rio_mport *mport)
{
	int rc = 0;

	/* Map outbound doorbell window immediately after maintenance window */
	if (!(dbell_win =
	      (u32) ioremap(mport->iores.start + RIO_MAINT_WIN_SIZE,
			    RIO_DBELL_WIN_SIZE))) {
		printk(KERN_ERR
		       "RIO: unable to map outbound doorbell window\n");
		rc = -ENOMEM;
		goto out;
	}

	/* Initialize inbound doorbells */
	if (!(dbell_ring.virt = dma_alloc_coherent(NULL,
						   512 * DOORBELL_MESSAGE_SIZE,
						   &dbell_ring.phys,
						   GFP_KERNEL))) {
		printk(KERN_ERR "RIO: unable allocate inbound doorbell ring\n");
		rc = -ENOMEM;
		iounmap((void *)dbell_win);
		goto out;
	}

	/* Point dequeue/enqueue pointers at first entry in ring */
	out_be32((void *)&msg_regs->dqdpar, (u32) dbell_ring.phys);
	out_be32((void *)&msg_regs->dqepar, (u32) dbell_ring.phys);

	/* Clear interrupt status */
	out_be32((void *)&msg_regs->dsr, 0x00000091);

	/* Hook up doorbell handler */
	if ((rc =
	     request_irq(MPC85xx_IRQ_RIO_BELL, mpc85xx_rio_dbell_handler, 0,
			 "dbell_rx", (void *)mport) < 0)) {
		iounmap((void *)dbell_win);
		dma_free_coherent(NULL, 512 * DOORBELL_MESSAGE_SIZE,
				  dbell_ring.virt, dbell_ring.phys);
		printk(KERN_ERR
		       "MPC85xx RIO: unable to request inbound doorbell irq");
		goto out;
	}

	/* Configure doorbells for snooping, 512 entries, and enable */
	out_be32((void *)&msg_regs->dmr, 0x00108161);

      out:
	return rc;
}

static char *cmdline = NULL;

static int mpc85xx_rio_get_hdid(int index)
{
	/* XXX Need to parse multiple entries in some format */
	if (!cmdline)
		return -1;

	return simple_strtol(cmdline, NULL, 0);
}

static int mpc85xx_rio_get_cmdline(char *s)
{
	if (!s)
		return 0;

	cmdline = s;
	return 1;
}

__setup("riohdid=", mpc85xx_rio_get_cmdline);

/**
 * mpc85xx_rio_setup - Setup MPC85xx RapidIO interface
 * @law_start: Starting physical address of RapidIO LAW
 * @law_size: Size of RapidIO LAW
 *
 * Initializes MPC85xx RapidIO hardware interface, configures
 * master port with system-specific info, and registers the
 * master port with the RapidIO subsystem.
 */
void mpc85xx_rio_setup(int law_start, int law_size)
{
	struct rio_ops *ops;
	struct rio_mport *port;

	ops = kmalloc(sizeof(struct rio_ops), GFP_KERNEL);
	ops->lcread = mpc85xx_local_config_read;
	ops->lcwrite = mpc85xx_local_config_write;
	ops->cread = mpc85xx_rio_config_read;
	ops->cwrite = mpc85xx_rio_config_write;
	ops->dsend = mpc85xx_rio_doorbell_send;

	port = kmalloc(sizeof(struct rio_mport), GFP_KERNEL);
	port->id = 0;
	port->index = 0;
	INIT_LIST_HEAD(&port->dbells);
	port->iores.start = law_start;
	port->iores.end = law_start + law_size;
	port->iores.flags = IORESOURCE_MEM;

	rio_init_dbell_res(&port->riores[RIO_DOORBELL_RESOURCE], 0, 0xffff);
	rio_init_mbox_res(&port->riores[RIO_INB_MBOX_RESOURCE], 0, 0);
	rio_init_mbox_res(&port->riores[RIO_OUTB_MBOX_RESOURCE], 0, 0);
	strcpy(port->name, "RIO0 mport");

	port->ops = ops;
	port->host_deviceid = mpc85xx_rio_get_hdid(port->id);

	rio_register_mport(port);

	regs_win = (u32) ioremap(RIO_REGS_BASE, 0x20000);
	atmu_regs = (struct rio_atmu_regs *)(regs_win + RIO_ATMU_REGS_OFFSET);
	maint_atmu_regs = atmu_regs + 1;
	dbell_atmu_regs = atmu_regs + 2;
	msg_regs = (struct rio_msg_regs *)(regs_win + RIO_MSG_REGS_OFFSET);

	/* Configure maintenance transaction window */
	out_be32((void *)&maint_atmu_regs->rowbar, 0x000c0000);
	out_be32((void *)&maint_atmu_regs->rowar, 0x80077015);

	maint_win = (u32) ioremap(law_start, RIO_MAINT_WIN_SIZE);

	/* Configure outbound doorbell window */
	out_be32((void *)&dbell_atmu_regs->rowbar, 0x000c0400);
	out_be32((void *)&dbell_atmu_regs->rowar, 0x8004200b);
	mpc85xx_rio_doorbell_init(port);
}
