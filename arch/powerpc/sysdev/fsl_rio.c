/*
 * Freescale MPC85xx/MPC86xx RapidIO support
 *
 * Copyright 2009 Sysgo AG
 * Thomas Moll <thomas.moll@sysgo.com>
 * - fixed maintenance access routines, check for aligned access
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write message handling
 * - Added Machine Check exception handling
 *
 * Copyright (C) 2007, 2008, 2010 Freescale Semiconductor, Inc.
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
#include <linux/slab.h>
#include <linux/kfifo.h>

#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>

#undef DEBUG_PW	/* Port-Write debugging */

/* RapidIO definition irq, which read from OF-tree */
#define IRQ_RIO_BELL(m)		(((struct rio_priv *)(m->priv))->bellirq)
#define IRQ_RIO_TX(m)		(((struct rio_priv *)(m->priv))->txirq)
#define IRQ_RIO_RX(m)		(((struct rio_priv *)(m->priv))->rxirq)
#define IRQ_RIO_PW(m)		(((struct rio_priv *)(m->priv))->pwirq)

#define IPWSR_CLEAR		0x98
#define OMSR_CLEAR		0x1cb3
#define IMSR_CLEAR		0x491
#define IDSR_CLEAR		0x91
#define ODSR_CLEAR		0x1c00
#define LTLEECSR_ENABLE_ALL	0xFFC000FC
#define ESCSR_CLEAR		0x07120204
#define IECSR_CLEAR		0x80000000

#define RIO_PORT1_EDCSR		0x0640
#define RIO_PORT2_EDCSR		0x0680
#define RIO_PORT1_IECSR		0x10130
#define RIO_PORT2_IECSR		0x101B0
#define RIO_IM0SR		0x13064
#define RIO_IM1SR		0x13164
#define RIO_OM0SR		0x13004
#define RIO_OM1SR		0x13104

#define RIO_ATMU_REGS_OFFSET	0x10c00
#define RIO_P_MSG_REGS_OFFSET	0x11000
#define RIO_S_MSG_REGS_OFFSET	0x13000
#define RIO_GCCSR		0x13c
#define RIO_ESCSR		0x158
#define RIO_PORT2_ESCSR		0x178
#define RIO_CCSR		0x15c
#define RIO_LTLEDCSR		0x0608
#define RIO_LTLEDCSR_IER	0x80000000
#define RIO_LTLEDCSR_PRT	0x01000000
#define RIO_LTLEECSR		0x060c
#define RIO_EPWISR		0x10010
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

#define RIO_IPWMR_SEN		0x00100000
#define RIO_IPWMR_QFIE		0x00000100
#define RIO_IPWMR_EIE		0x00000020
#define RIO_IPWMR_CQ		0x00000002
#define RIO_IPWMR_PWE		0x00000001

#define RIO_IPWSR_QF		0x00100000
#define RIO_IPWSR_TE		0x00000080
#define RIO_IPWSR_QFI		0x00000010
#define RIO_IPWSR_PWD		0x00000008
#define RIO_IPWSR_PWB		0x00000004

/* EPWISR Error match value */
#define RIO_EPWISR_PINT1	0x80000000
#define RIO_EPWISR_PINT2	0x40000000
#define RIO_EPWISR_MU		0x00000002
#define RIO_EPWISR_PW		0x00000001

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
	u32 omr;	/* 0xD_3000 - Outbound message 0 mode register */
	u32 osr;	/* 0xD_3004 - Outbound message 0 status register */
	u32 pad1;
	u32 odqdpar;	/* 0xD_300C - Outbound message 0 descriptor queue
			   dequeue pointer address register */
	u32 pad2;
	u32 osar;	/* 0xD_3014 - Outbound message 0 source address
			   register */
	u32 odpr;	/* 0xD_3018 - Outbound message 0 destination port
			   register */
	u32 odatr;	/* 0xD_301C - Outbound message 0 destination attributes
			   Register*/
	u32 odcr;	/* 0xD_3020 - Outbound message 0 double-word count
			   register */
	u32 pad3;
	u32 odqepar;	/* 0xD_3028 - Outbound message 0 descriptor queue
			   enqueue pointer address register */
	u32 pad4[13];
	u32 imr;	/* 0xD_3060 - Inbound message 0 mode register */
	u32 isr;	/* 0xD_3064 - Inbound message 0 status register */
	u32 pad5;
	u32 ifqdpar;	/* 0xD_306C - Inbound message 0 frame queue dequeue
			   pointer address register*/
	u32 pad6;
	u32 ifqepar;	/* 0xD_3074 - Inbound message 0 frame queue enqueue
			   pointer address register */
	u32 pad7[226];
	u32 odmr;	/* 0xD_3400 - Outbound doorbell mode register */
	u32 odsr;	/* 0xD_3404 - Outbound doorbell status register */
	u32 res0[4];
	u32 oddpr;	/* 0xD_3418 - Outbound doorbell destination port
			   register */
	u32 oddatr;	/* 0xD_341c - Outbound doorbell destination attributes
			   register */
	u32 res1[3];
	u32 odretcr;	/* 0xD_342C - Outbound doorbell retry error threshold
			   configuration register */
	u32 res2[12];
	u32 dmr;	/* 0xD_3460 - Inbound doorbell mode register */
	u32 dsr;	/* 0xD_3464 - Inbound doorbell status register */
	u32 pad8;
	u32 dqdpar;	/* 0xD_346C - Inbound doorbell queue dequeue Pointer
			   address register */
	u32 pad9;
	u32 dqepar;	/* 0xD_3474 - Inbound doorbell Queue enqueue pointer
			   address register */
	u32 pad10[26];
	u32 pwmr;	/* 0xD_34E0 - Inbound port-write mode register */
	u32 pwsr;	/* 0xD_34E4 - Inbound port-write status register */
	u32 epwqbar;	/* 0xD_34E8 - Extended Port-Write Queue Base Address
			   register */
	u32 pwqbar;	/* 0xD_34EC - Inbound port-write queue base address
			   register */
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

struct rio_port_write_msg {
	void *virt;
	dma_addr_t phys;
	u32 msg_count;
	u32 err_count;
	u32 discard_count;
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
	struct rio_port_write_msg port_write_msg;
	int bellirq;
	int txirq;
	int rxirq;
	int pwirq;
	struct work_struct pw_work;
	struct kfifo pw_fifo;
	spinlock_t pw_fifo_lock;
};

#define __fsl_read_rio_config(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op" %1,0(%2)\n"		\
		"	eieio\n"			\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li %1,-1\n"			\
		"	li %0,%3\n"			\
		"	b 2b\n"				\
		".section __ex_table,\"a\"\n"		\
		"	.align 2\n"			\
		"	.long 1b,3b\n"			\
		".text"					\
		: "=r" (err), "=r" (x)			\
		: "b" (addr), "i" (-EFAULT), "0" (err))

static void __iomem *rio_regs_win;

#ifdef CONFIG_E500
int fsl_rio_mcheck_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *entry;
	unsigned long reason;

	if (!rio_regs_win)
		return 0;

	reason = in_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR));
	if (reason & (RIO_LTLEDCSR_IER | RIO_LTLEDCSR_PRT)) {
		/* Check if we are prepared to handle this fault */
		entry = search_exception_tables(regs->nip);
		if (entry) {
			pr_debug("RIO: %s - MC Exception handled\n",
				 __func__);
			out_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR),
				 0);
			regs->msr |= MSR_RI;
			regs->nip = entry->fixup;
			return 1;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fsl_rio_mcheck_exception);
#endif

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
	u32 rval, err = 0;

	pr_debug
	    ("fsl_rio_config_read: index %d destid %d hopcount %d offset %8.8x len %d\n",
	     index, destid, hopcount, offset, len);

	/* 16MB maintenance window possible */
	/* allow only aligned access to maintenance registers */
	if (offset > (0x1000000 - len) || !IS_ALIGNED(offset, len))
		return -EINVAL;

	out_be32(&priv->maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | (offset >> 12));
	out_be32(&priv->maint_atmu_regs->rowtear,  (destid >> 10));

	data = (u8 *) priv->maint_win + (offset & (RIO_MAINT_WIN_SIZE - 1));
	switch (len) {
	case 1:
		__fsl_read_rio_config(rval, data, err, "lbz");
		break;
	case 2:
		__fsl_read_rio_config(rval, data, err, "lhz");
		break;
	case 4:
		__fsl_read_rio_config(rval, data, err, "lwz");
		break;
	default:
		return -EINVAL;
	}

	if (err) {
		pr_debug("RIO: cfg_read error %d for %x:%x:%x\n",
			 err, destid, hopcount, offset);
	}

	*val = rval;

	return err;
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

	/* 16MB maintenance windows possible */
	/* allow only aligned access to maintenance registers */
	if (offset > (0x1000000 - len) || !IS_ALIGNED(offset, len))
		return -EINVAL;

	out_be32(&priv->maint_atmu_regs->rowtar,
		 (destid << 22) | (hopcount << 12) | (offset >> 12));
	out_be32(&priv->maint_atmu_regs->rowtear,  (destid >> 10));

	data = (u8 *) priv->maint_win + (offset & (RIO_MAINT_WIN_SIZE - 1));
	switch (len) {
	case 1:
		out_8((u8 *) data, val);
		break;
	case 2:
		out_be16((u16 *) data, val);
		break;
	case 4:
		out_be32((u32 *) data, val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * fsl_add_outb_message - Add message to the MPC85xx outbound message queue
 * @mport: Master port with outbound message queue
 * @rdev: Target of outbound message
 * @mbox: Outbound mailbox
 * @buffer: Message to add to outbound queue
 * @len: Length of message
 *
 * Adds the @buffer message to the MPC85xx outbound message queue. Returns
 * %0 on success or %-EINVAL on failure.
 */
static int
fsl_add_outb_message(struct rio_mport *mport, struct rio_dev *rdev, int mbox,
			void *buffer, size_t len)
{
	struct rio_priv *priv = mport->priv;
	u32 omr;
	struct rio_tx_desc *desc = (struct rio_tx_desc *)priv->msg_tx_ring.virt
					+ priv->msg_tx_ring.tx_slot;
	int ret = 0;

	pr_debug("RIO: fsl_add_outb_message(): destid %4.4x mbox %d buffer " \
		 "%8.8x len %8.8x\n", rdev->destid, mbox, (int)buffer, len);

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
 * fsl_open_outb_mbox - Initialize MPC85xx outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the outbound mailbox ring
 *
 * Initializes buffer ring, request the outbound message interrupt,
 * and enables the outbound message unit. Returns %0 on success and
 * %-EINVAL or %-ENOMEM on failure.
 */
static int
fsl_open_outb_mbox(struct rio_mport *mport, void *dev_id, int mbox, int entries)
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
 * fsl_close_outb_mbox - Shut down MPC85xx outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @mbox: Mailbox to close
 *
 * Disables the outbound message unit, free all buffers, and
 * frees the outbound message interrupt.
 */
static void fsl_close_outb_mbox(struct rio_mport *mport, int mbox)
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
 * fsl_open_inb_mbox - Initialize MPC85xx inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the inbound mailbox ring
 *
 * Initializes buffer ring, request the inbound message interrupt,
 * and enables the inbound message unit. Returns %0 on success
 * and %-EINVAL or %-ENOMEM on failure.
 */
static int
fsl_open_inb_mbox(struct rio_mport *mport, void *dev_id, int mbox, int entries)
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
 * fsl_close_inb_mbox - Shut down MPC85xx inbound mailbox
 * @mport: Master port implementing the inbound message unit
 * @mbox: Mailbox to close
 *
 * Disables the inbound message unit, free all buffers, and
 * frees the inbound message interrupt.
 */
static void fsl_close_inb_mbox(struct rio_mport *mport, int mbox)
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
 * fsl_add_inb_buffer - Add buffer to the MPC85xx inbound message queue
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 * @buf: Buffer to add to inbound queue
 *
 * Adds the @buf buffer to the MPC85xx inbound message queue. Returns
 * %0 on success or %-EINVAL on failure.
 */
static int fsl_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf)
{
	int rc = 0;
	struct rio_priv *priv = mport->priv;

	pr_debug("RIO: fsl_add_inb_buffer(), msg_rx_ring.rx_slot %d\n",
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

/**
 * fsl_get_inb_message - Fetch inbound message from the MPC85xx message unit
 * @mport: Master port implementing the inbound message unit
 * @mbox: Inbound mailbox number
 *
 * Gets the next available inbound message from the inbound message queue.
 * A pointer to the message is returned on success or NULL on failure.
 */
static void *fsl_get_inb_message(struct rio_mport *mport, int mbox)
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

static void port_error_handler(struct rio_mport *port, int offset)
{
	/*XXX: Error recovery is not implemented, we just clear errors */
	out_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR), 0);

	if (offset == 0) {
		out_be32((u32 *)(rio_regs_win + RIO_PORT1_EDCSR), 0);
		out_be32((u32 *)(rio_regs_win + RIO_PORT1_IECSR), IECSR_CLEAR);
		out_be32((u32 *)(rio_regs_win + RIO_ESCSR), ESCSR_CLEAR);
	} else {
		out_be32((u32 *)(rio_regs_win + RIO_PORT2_EDCSR), 0);
		out_be32((u32 *)(rio_regs_win + RIO_PORT2_IECSR), IECSR_CLEAR);
		out_be32((u32 *)(rio_regs_win + RIO_PORT2_ESCSR), ESCSR_CLEAR);
	}
}

static void msg_unit_error_handler(struct rio_mport *port)
{
	struct rio_priv *priv = port->priv;

	/*XXX: Error recovery is not implemented, we just clear errors */
	out_be32((u32 *)(rio_regs_win + RIO_LTLEDCSR), 0);

	out_be32((u32 *)(rio_regs_win + RIO_IM0SR), IMSR_CLEAR);
	out_be32((u32 *)(rio_regs_win + RIO_IM1SR), IMSR_CLEAR);
	out_be32((u32 *)(rio_regs_win + RIO_OM0SR), OMSR_CLEAR);
	out_be32((u32 *)(rio_regs_win + RIO_OM1SR), OMSR_CLEAR);

	out_be32(&priv->msg_regs->odsr, ODSR_CLEAR);
	out_be32(&priv->msg_regs->dsr, IDSR_CLEAR);

	out_be32(&priv->msg_regs->pwsr, IPWSR_CLEAR);
}

/**
 * fsl_rio_port_write_handler - MPC85xx port write interrupt handler
 * @irq: Linux interrupt number
 * @dev_instance: Pointer to interrupt-specific data
 *
 * Handles port write interrupts. Parses a list of registered
 * port write event handlers and executes a matching event handler.
 */
static irqreturn_t
fsl_rio_port_write_handler(int irq, void *dev_instance)
{
	u32 ipwmr, ipwsr;
	struct rio_mport *port = (struct rio_mport *)dev_instance;
	struct rio_priv *priv = port->priv;
	u32 epwisr, tmp;

	epwisr = in_be32(priv->regs_win + RIO_EPWISR);
	if (!(epwisr & RIO_EPWISR_PW))
		goto pw_done;

	ipwmr = in_be32(&priv->msg_regs->pwmr);
	ipwsr = in_be32(&priv->msg_regs->pwsr);

#ifdef DEBUG_PW
	pr_debug("PW Int->IPWMR: 0x%08x IPWSR: 0x%08x (", ipwmr, ipwsr);
	if (ipwsr & RIO_IPWSR_QF)
		pr_debug(" QF");
	if (ipwsr & RIO_IPWSR_TE)
		pr_debug(" TE");
	if (ipwsr & RIO_IPWSR_QFI)
		pr_debug(" QFI");
	if (ipwsr & RIO_IPWSR_PWD)
		pr_debug(" PWD");
	if (ipwsr & RIO_IPWSR_PWB)
		pr_debug(" PWB");
	pr_debug(" )\n");
#endif
	/* Schedule deferred processing if PW was received */
	if (ipwsr & RIO_IPWSR_QFI) {
		/* Save PW message (if there is room in FIFO),
		 * otherwise discard it.
		 */
		if (kfifo_avail(&priv->pw_fifo) >= RIO_PW_MSG_SIZE) {
			priv->port_write_msg.msg_count++;
			kfifo_in(&priv->pw_fifo, priv->port_write_msg.virt,
				 RIO_PW_MSG_SIZE);
		} else {
			priv->port_write_msg.discard_count++;
			pr_debug("RIO: ISR Discarded Port-Write Msg(s) (%d)\n",
				 priv->port_write_msg.discard_count);
		}
		/* Clear interrupt and issue Clear Queue command. This allows
		 * another port-write to be received.
		 */
		out_be32(&priv->msg_regs->pwsr,	RIO_IPWSR_QFI);
		out_be32(&priv->msg_regs->pwmr, ipwmr | RIO_IPWMR_CQ);

		schedule_work(&priv->pw_work);
	}

	if ((ipwmr & RIO_IPWMR_EIE) && (ipwsr & RIO_IPWSR_TE)) {
		priv->port_write_msg.err_count++;
		pr_debug("RIO: Port-Write Transaction Err (%d)\n",
			 priv->port_write_msg.err_count);
		/* Clear Transaction Error: port-write controller should be
		 * disabled when clearing this error
		 */
		out_be32(&priv->msg_regs->pwmr, ipwmr & ~RIO_IPWMR_PWE);
		out_be32(&priv->msg_regs->pwsr,	RIO_IPWSR_TE);
		out_be32(&priv->msg_regs->pwmr, ipwmr);
	}

	if (ipwsr & RIO_IPWSR_PWD) {
		priv->port_write_msg.discard_count++;
		pr_debug("RIO: Port Discarded Port-Write Msg(s) (%d)\n",
			 priv->port_write_msg.discard_count);
		out_be32(&priv->msg_regs->pwsr, RIO_IPWSR_PWD);
	}

pw_done:
	if (epwisr & RIO_EPWISR_PINT1) {
		tmp = in_be32(priv->regs_win + RIO_LTLEDCSR);
		pr_debug("RIO_LTLEDCSR = 0x%x\n", tmp);
		port_error_handler(port, 0);
	}

	if (epwisr & RIO_EPWISR_PINT2) {
		tmp = in_be32(priv->regs_win + RIO_LTLEDCSR);
		pr_debug("RIO_LTLEDCSR = 0x%x\n", tmp);
		port_error_handler(port, 1);
	}

	if (epwisr & RIO_EPWISR_MU) {
		tmp = in_be32(priv->regs_win + RIO_LTLEDCSR);
		pr_debug("RIO_LTLEDCSR = 0x%x\n", tmp);
		msg_unit_error_handler(port);
	}

	return IRQ_HANDLED;
}

static void fsl_pw_dpc(struct work_struct *work)
{
	struct rio_priv *priv = container_of(work, struct rio_priv, pw_work);
	unsigned long flags;
	u32 msg_buffer[RIO_PW_MSG_SIZE/sizeof(u32)];

	/*
	 * Process port-write messages
	 */
	spin_lock_irqsave(&priv->pw_fifo_lock, flags);
	while (kfifo_out(&priv->pw_fifo, (unsigned char *)msg_buffer,
			 RIO_PW_MSG_SIZE)) {
		/* Process one message */
		spin_unlock_irqrestore(&priv->pw_fifo_lock, flags);
#ifdef DEBUG_PW
		{
		u32 i;
		pr_debug("%s : Port-Write Message:", __func__);
		for (i = 0; i < RIO_PW_MSG_SIZE/sizeof(u32); i++) {
			if ((i%4) == 0)
				pr_debug("\n0x%02x: 0x%08x", i*4,
					 msg_buffer[i]);
			else
				pr_debug(" 0x%08x", msg_buffer[i]);
		}
		pr_debug("\n");
		}
#endif
		/* Pass the port-write message to RIO core for processing */
		rio_inb_pwrite_handler((union rio_pw_msg *)msg_buffer);
		spin_lock_irqsave(&priv->pw_fifo_lock, flags);
	}
	spin_unlock_irqrestore(&priv->pw_fifo_lock, flags);
}

/**
 * fsl_rio_pw_enable - enable/disable port-write interface init
 * @mport: Master port implementing the port write unit
 * @enable:    1=enable; 0=disable port-write message handling
 */
static int fsl_rio_pw_enable(struct rio_mport *mport, int enable)
{
	struct rio_priv *priv = mport->priv;
	u32 rval;

	rval = in_be32(&priv->msg_regs->pwmr);

	if (enable)
		rval |= RIO_IPWMR_PWE;
	else
		rval &= ~RIO_IPWMR_PWE;

	out_be32(&priv->msg_regs->pwmr, rval);

	return 0;
}

/**
 * fsl_rio_port_write_init - MPC85xx port write interface init
 * @mport: Master port implementing the port write unit
 *
 * Initializes port write unit hardware and DMA buffer
 * ring. Called from fsl_rio_setup(). Returns %0 on success
 * or %-ENOMEM on failure.
 */
static int fsl_rio_port_write_init(struct rio_mport *mport)
{
	struct rio_priv *priv = mport->priv;
	int rc = 0;

	/* Following configurations require a disabled port write controller */
	out_be32(&priv->msg_regs->pwmr,
		 in_be32(&priv->msg_regs->pwmr) & ~RIO_IPWMR_PWE);

	/* Initialize port write */
	priv->port_write_msg.virt = dma_alloc_coherent(priv->dev,
					RIO_PW_MSG_SIZE,
					&priv->port_write_msg.phys, GFP_KERNEL);
	if (!priv->port_write_msg.virt) {
		pr_err("RIO: unable allocate port write queue\n");
		return -ENOMEM;
	}

	priv->port_write_msg.err_count = 0;
	priv->port_write_msg.discard_count = 0;

	/* Point dequeue/enqueue pointers at first entry */
	out_be32(&priv->msg_regs->epwqbar, 0);
	out_be32(&priv->msg_regs->pwqbar, (u32) priv->port_write_msg.phys);

	pr_debug("EIPWQBAR: 0x%08x IPWQBAR: 0x%08x\n",
		 in_be32(&priv->msg_regs->epwqbar),
		 in_be32(&priv->msg_regs->pwqbar));

	/* Clear interrupt status IPWSR */
	out_be32(&priv->msg_regs->pwsr,
		 (RIO_IPWSR_TE | RIO_IPWSR_QFI | RIO_IPWSR_PWD));

	/* Configure port write contoller for snooping enable all reporting,
	   clear queue full */
	out_be32(&priv->msg_regs->pwmr,
		 RIO_IPWMR_SEN | RIO_IPWMR_QFIE | RIO_IPWMR_EIE | RIO_IPWMR_CQ);


	/* Hook up port-write handler */
	rc = request_irq(IRQ_RIO_PW(mport), fsl_rio_port_write_handler,
			IRQF_SHARED, "port-write", (void *)mport);
	if (rc < 0) {
		pr_err("MPC85xx RIO: unable to request inbound doorbell irq");
		goto err_out;
	}
	/* Enable Error Interrupt */
	out_be32((u32 *)(rio_regs_win + RIO_LTLEECSR), LTLEECSR_ENABLE_ALL);

	INIT_WORK(&priv->pw_work, fsl_pw_dpc);
	spin_lock_init(&priv->pw_fifo_lock);
	if (kfifo_alloc(&priv->pw_fifo, RIO_PW_MSG_SIZE * 32, GFP_KERNEL)) {
		pr_err("FIFO allocation failed\n");
		rc = -ENOMEM;
		goto err_out_irq;
	}

	pr_debug("IPWMR: 0x%08x IPWSR: 0x%08x\n",
		 in_be32(&priv->msg_regs->pwmr),
		 in_be32(&priv->msg_regs->pwsr));

	return rc;

err_out_irq:
	free_irq(IRQ_RIO_PW(mport), (void *)mport);
err_out:
	dma_free_coherent(priv->dev, RIO_PW_MSG_SIZE,
			  priv->port_write_msg.virt,
			  priv->port_write_msg.phys);
	return rc;
}

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
			break;
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
 * @dev: platform_device pointer
 *
 * Initializes MPC85xx RapidIO hardware interface, configures
 * master port with system-specific info, and registers the
 * master port with the RapidIO subsystem.
 */
int fsl_rio_setup(struct platform_device *dev)
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

	if (!dev->dev.of_node) {
		dev_err(&dev->dev, "Device OF-Node is NULL");
		return -EFAULT;
	}

	rc = of_address_to_resource(dev->dev.of_node, 0, &regs);
	if (rc) {
		dev_err(&dev->dev, "Can't get %s property 'reg'\n",
				dev->dev.of_node->full_name);
		return -EFAULT;
	}
	dev_info(&dev->dev, "Of-device full name %s\n", dev->dev.of_node->full_name);
	dev_info(&dev->dev, "Regs: %pR\n", &regs);

	dt_range = of_get_property(dev->dev.of_node, "ranges", &rlen);
	if (!dt_range) {
		dev_err(&dev->dev, "Can't get %s property 'ranges'\n",
				dev->dev.of_node->full_name);
		return -EFAULT;
	}

	/* Get node address wide */
	cell = of_get_property(dev->dev.of_node, "#address-cells", NULL);
	if (cell)
		aw = *cell;
	else
		aw = of_n_addr_cells(dev->dev.of_node);
	/* Get node size wide */
	cell = of_get_property(dev->dev.of_node, "#size-cells", NULL);
	if (cell)
		sw = *cell;
	else
		sw = of_n_size_cells(dev->dev.of_node);
	/* Get parent address wide wide */
	paw = of_n_addr_cells(dev->dev.of_node);

	law_start = of_read_number(dt_range + aw, paw);
	law_size = of_read_number(dt_range + aw + paw, sw);

	dev_info(&dev->dev, "LAW start 0x%016llx, size 0x%016llx.\n",
			law_start, law_size);

	ops = kzalloc(sizeof(struct rio_ops), GFP_KERNEL);
	if (!ops) {
		rc = -ENOMEM;
		goto err_ops;
	}
	ops->lcread = fsl_local_config_read;
	ops->lcwrite = fsl_local_config_write;
	ops->cread = fsl_rio_config_read;
	ops->cwrite = fsl_rio_config_write;
	ops->dsend = fsl_rio_doorbell_send;
	ops->pwenable = fsl_rio_pw_enable;
	ops->open_outb_mbox = fsl_open_outb_mbox;
	ops->open_inb_mbox = fsl_open_inb_mbox;
	ops->close_outb_mbox = fsl_close_outb_mbox;
	ops->close_inb_mbox = fsl_close_inb_mbox;
	ops->add_outb_message = fsl_add_outb_message;
	ops->add_inb_buffer = fsl_add_inb_buffer;
	ops->get_inb_message = fsl_get_inb_message;

	port = kzalloc(sizeof(struct rio_mport), GFP_KERNEL);
	if (!port) {
		rc = -ENOMEM;
		goto err_port;
	}
	port->index = 0;

	priv = kzalloc(sizeof(struct rio_priv), GFP_KERNEL);
	if (!priv) {
		printk(KERN_ERR "Can't alloc memory for 'priv'\n");
		rc = -ENOMEM;
		goto err_priv;
	}

	INIT_LIST_HEAD(&port->dbells);
	port->iores.start = law_start;
	port->iores.end = law_start + law_size - 1;
	port->iores.flags = IORESOURCE_MEM;
	port->iores.name = "rio_io_win";

	if (request_resource(&iomem_resource, &port->iores) < 0) {
		dev_err(&dev->dev, "RIO: Error requesting master port region"
			" 0x%016llx-0x%016llx\n",
			(u64)port->iores.start, (u64)port->iores.end);
			rc = -ENOMEM;
			goto err_res;
	}

	priv->pwirq   = irq_of_parse_and_map(dev->dev.of_node, 0);
	priv->bellirq = irq_of_parse_and_map(dev->dev.of_node, 2);
	priv->txirq = irq_of_parse_and_map(dev->dev.of_node, 3);
	priv->rxirq = irq_of_parse_and_map(dev->dev.of_node, 4);
	dev_info(&dev->dev, "pwirq: %d, bellirq: %d, txirq: %d, rxirq %d\n",
		 priv->pwirq, priv->bellirq, priv->txirq, priv->rxirq);

	rio_init_dbell_res(&port->riores[RIO_DOORBELL_RESOURCE], 0, 0xffff);
	rio_init_mbox_res(&port->riores[RIO_INB_MBOX_RESOURCE], 0, 0);
	rio_init_mbox_res(&port->riores[RIO_OUTB_MBOX_RESOURCE], 0, 0);
	strcpy(port->name, "RIO0 mport");

	priv->dev = &dev->dev;

	port->ops = ops;
	port->priv = priv;
	port->phys_efptr = 0x100;

	priv->regs_win = ioremap(regs.start, regs.end - regs.start + 1);
	rio_regs_win = priv->regs_win;

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

	if (rio_register_mport(port))
		goto err;

	if (port->host_deviceid >= 0)
		out_be32(priv->regs_win + RIO_GCCSR, RIO_PORT_GEN_HOST |
			RIO_PORT_GEN_MASTER | RIO_PORT_GEN_DISCOVERED);
	else
		out_be32(priv->regs_win + RIO_GCCSR, 0x00000000);

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
	out_be32(&priv->maint_atmu_regs->rowar,
		 0x80077000 | (ilog2(RIO_MAINT_WIN_SIZE) - 1));

	priv->maint_win = ioremap(law_start, RIO_MAINT_WIN_SIZE);

	/* Configure outbound doorbell window */
	out_be32(&priv->dbell_atmu_regs->rowbar,
			(law_start + RIO_MAINT_WIN_SIZE) >> 12);
	out_be32(&priv->dbell_atmu_regs->rowar, 0x8004200b);	/* 4k */
	fsl_rio_doorbell_init(port);
	fsl_rio_port_write_init(port);

	return 0;
err:
	iounmap(priv->regs_win);
err_res:
	kfree(priv);
err_priv:
	kfree(port);
err_port:
	kfree(ops);
err_ops:
	return rc;
}

/* The probe function for RapidIO peer-to-peer network.
 */
static int __devinit fsl_of_rio_rpn_probe(struct platform_device *dev)
{
	printk(KERN_INFO "Setting up RapidIO peer-to-peer network %s\n",
			dev->dev.of_node->full_name);

	return fsl_rio_setup(dev);
};

static const struct of_device_id fsl_of_rio_rpn_ids[] = {
	{
		.compatible = "fsl,rapidio-delta",
	},
	{},
};

static struct platform_driver fsl_of_rio_rpn_driver = {
	.driver = {
		.name = "fsl-of-rio",
		.owner = THIS_MODULE,
		.of_match_table = fsl_of_rio_rpn_ids,
	},
	.probe = fsl_of_rio_rpn_probe,
};

static __init int fsl_of_rio_rpn_init(void)
{
	return platform_driver_register(&fsl_of_rio_rpn_driver);
}

subsys_initcall(fsl_of_rio_rpn_init);
