/*
 * RapidIO mport driver for Tsi721 PCIExpress-to-SRIO bridge
 *
 * Copyright 2011 Integrated Device Technology, Inc.
 * Alexandre Bounine <alexandre.bounine@idt.com>
 * Chul Kim <chul.kim@idt.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/delay.h>

#include "tsi721.h"

#define DEBUG_PW	/* Inbound Port-Write debugging */

static void tsi721_omsg_handler(struct tsi721_device *priv, int ch);
static void tsi721_imsg_handler(struct tsi721_device *priv, int ch);

/**
 * tsi721_lcread - read from local SREP config space
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be read into
 *
 * Generates a local SREP space read. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int tsi721_lcread(struct rio_mport *mport, int index, u32 offset,
			 int len, u32 *data)
{
	struct tsi721_device *priv = mport->priv;

	if (len != sizeof(u32))
		return -EINVAL; /* only 32-bit access is supported */

	*data = ioread32(priv->regs + offset);

	return 0;
}

/**
 * tsi721_lcwrite - write into local SREP config space
 * @mport: RapidIO master port info
 * @index: ID of RapdiIO interface
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Value to be written
 *
 * Generates a local write into SREP configuration space. Returns %0 on
 * success or %-EINVAL on failure.
 */
static int tsi721_lcwrite(struct rio_mport *mport, int index, u32 offset,
			  int len, u32 data)
{
	struct tsi721_device *priv = mport->priv;

	if (len != sizeof(u32))
		return -EINVAL; /* only 32-bit access is supported */

	iowrite32(data, priv->regs + offset);

	return 0;
}

/**
 * tsi721_maint_dma - Helper function to generate RapidIO maintenance
 *                    transactions using designated Tsi721 DMA channel.
 * @priv: pointer to tsi721 private data
 * @sys_size: RapdiIO transport system size
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @data: Location to be read from or write into
 * @do_wr: Operation flag (1 == MAINT_WR)
 *
 * Generates a RapidIO maintenance transaction (Read or Write).
 * Returns %0 on success and %-EINVAL or %-EFAULT on failure.
 */
static int tsi721_maint_dma(struct tsi721_device *priv, u32 sys_size,
			u16 destid, u8 hopcount, u32 offset, int len,
			u32 *data, int do_wr)
{
	struct tsi721_dma_desc *bd_ptr;
	u32 rd_count, swr_ptr, ch_stat;
	int i, err = 0;
	u32 op = do_wr ? MAINT_WR : MAINT_RD;

	if (offset > (RIO_MAINT_SPACE_SZ - len) || (len != sizeof(u32)))
		return -EINVAL;

	bd_ptr = priv->bdma[TSI721_DMACH_MAINT].bd_base;

	rd_count = ioread32(
			priv->regs + TSI721_DMAC_DRDCNT(TSI721_DMACH_MAINT));

	/* Initialize DMA descriptor */
	bd_ptr[0].type_id = cpu_to_le32((DTYPE2 << 29) | (op << 19) | destid);
	bd_ptr[0].bcount = cpu_to_le32((sys_size << 26) | 0x04);
	bd_ptr[0].raddr_lo = cpu_to_le32((hopcount << 24) | offset);
	bd_ptr[0].raddr_hi = 0;
	if (do_wr)
		bd_ptr[0].data[0] = cpu_to_be32p(data);
	else
		bd_ptr[0].data[0] = 0xffffffff;

	mb();

	/* Start DMA operation */
	iowrite32(rd_count + 2,
		priv->regs + TSI721_DMAC_DWRCNT(TSI721_DMACH_MAINT));
	ioread32(priv->regs + TSI721_DMAC_DWRCNT(TSI721_DMACH_MAINT));
	i = 0;

	/* Wait until DMA transfer is finished */
	while ((ch_stat = ioread32(priv->regs +
		TSI721_DMAC_STS(TSI721_DMACH_MAINT))) & TSI721_DMAC_STS_RUN) {
		udelay(1);
		if (++i >= 5000000) {
			dev_dbg(&priv->pdev->dev,
				"%s : DMA[%d] read timeout ch_status=%x\n",
				__func__, TSI721_DMACH_MAINT, ch_stat);
			if (!do_wr)
				*data = 0xffffffff;
			err = -EIO;
			goto err_out;
		}
	}

	if (ch_stat & TSI721_DMAC_STS_ABORT) {
		/* If DMA operation aborted due to error,
		 * reinitialize DMA channel
		 */
		dev_dbg(&priv->pdev->dev, "%s : DMA ABORT ch_stat=%x\n",
			__func__, ch_stat);
		dev_dbg(&priv->pdev->dev, "OP=%d : destid=%x hc=%x off=%x\n",
			do_wr ? MAINT_WR : MAINT_RD, destid, hopcount, offset);
		iowrite32(TSI721_DMAC_INT_ALL,
			priv->regs + TSI721_DMAC_INT(TSI721_DMACH_MAINT));
		iowrite32(TSI721_DMAC_CTL_INIT,
			priv->regs + TSI721_DMAC_CTL(TSI721_DMACH_MAINT));
		udelay(10);
		iowrite32(0, priv->regs +
				TSI721_DMAC_DWRCNT(TSI721_DMACH_MAINT));
		udelay(1);
		if (!do_wr)
			*data = 0xffffffff;
		err = -EIO;
		goto err_out;
	}

	if (!do_wr)
		*data = be32_to_cpu(bd_ptr[0].data[0]);

	/*
	 * Update descriptor status FIFO RD pointer.
	 * NOTE: Skipping check and clear FIFO entries because we are waiting
	 * for transfer to be completed.
	 */
	swr_ptr = ioread32(priv->regs + TSI721_DMAC_DSWP(TSI721_DMACH_MAINT));
	iowrite32(swr_ptr, priv->regs + TSI721_DMAC_DSRP(TSI721_DMACH_MAINT));
err_out:

	return err;
}

/**
 * tsi721_cread_dma - Generate a RapidIO maintenance read transaction
 *                    using Tsi721 BDMA engine.
 * @mport: RapidIO master port control structure
 * @index: ID of RapdiIO interface
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @val: Location to be read into
 *
 * Generates a RapidIO maintenance read transaction.
 * Returns %0 on success and %-EINVAL or %-EFAULT on failure.
 */
static int tsi721_cread_dma(struct rio_mport *mport, int index, u16 destid,
			u8 hopcount, u32 offset, int len, u32 *data)
{
	struct tsi721_device *priv = mport->priv;

	return tsi721_maint_dma(priv, mport->sys_size, destid, hopcount,
				offset, len, data, 0);
}

/**
 * tsi721_cwrite_dma - Generate a RapidIO maintenance write transaction
 *                     using Tsi721 BDMA engine
 * @mport: RapidIO master port control structure
 * @index: ID of RapdiIO interface
 * @destid: Destination ID of transaction
 * @hopcount: Number of hops to target device
 * @offset: Offset into configuration space
 * @len: Length (in bytes) of the maintenance transaction
 * @val: Value to be written
 *
 * Generates a RapidIO maintenance write transaction.
 * Returns %0 on success and %-EINVAL or %-EFAULT on failure.
 */
static int tsi721_cwrite_dma(struct rio_mport *mport, int index, u16 destid,
			 u8 hopcount, u32 offset, int len, u32 data)
{
	struct tsi721_device *priv = mport->priv;
	u32 temp = data;

	return tsi721_maint_dma(priv, mport->sys_size, destid, hopcount,
				offset, len, &temp, 1);
}

/**
 * tsi721_pw_handler - Tsi721 inbound port-write interrupt handler
 * @mport: RapidIO master port structure
 *
 * Handles inbound port-write interrupts. Copies PW message from an internal
 * buffer into PW message FIFO and schedules deferred routine to process
 * queued messages.
 */
static int
tsi721_pw_handler(struct rio_mport *mport)
{
	struct tsi721_device *priv = mport->priv;
	u32 pw_stat;
	u32 pw_buf[TSI721_RIO_PW_MSG_SIZE/sizeof(u32)];


	pw_stat = ioread32(priv->regs + TSI721_RIO_PW_RX_STAT);

	if (pw_stat & TSI721_RIO_PW_RX_STAT_PW_VAL) {
		pw_buf[0] = ioread32(priv->regs + TSI721_RIO_PW_RX_CAPT(0));
		pw_buf[1] = ioread32(priv->regs + TSI721_RIO_PW_RX_CAPT(1));
		pw_buf[2] = ioread32(priv->regs + TSI721_RIO_PW_RX_CAPT(2));
		pw_buf[3] = ioread32(priv->regs + TSI721_RIO_PW_RX_CAPT(3));

		/* Queue PW message (if there is room in FIFO),
		 * otherwise discard it.
		 */
		spin_lock(&priv->pw_fifo_lock);
		if (kfifo_avail(&priv->pw_fifo) >= TSI721_RIO_PW_MSG_SIZE)
			kfifo_in(&priv->pw_fifo, pw_buf,
						TSI721_RIO_PW_MSG_SIZE);
		else
			priv->pw_discard_count++;
		spin_unlock(&priv->pw_fifo_lock);
	}

	/* Clear pending PW interrupts */
	iowrite32(TSI721_RIO_PW_RX_STAT_PW_DISC | TSI721_RIO_PW_RX_STAT_PW_VAL,
		  priv->regs + TSI721_RIO_PW_RX_STAT);

	schedule_work(&priv->pw_work);

	return 0;
}

static void tsi721_pw_dpc(struct work_struct *work)
{
	struct tsi721_device *priv = container_of(work, struct tsi721_device,
						    pw_work);
	u32 msg_buffer[RIO_PW_MSG_SIZE/sizeof(u32)]; /* Use full size PW message
							buffer for RIO layer */

	/*
	 * Process port-write messages
	 */
	while (kfifo_out_spinlocked(&priv->pw_fifo, (unsigned char *)msg_buffer,
			 TSI721_RIO_PW_MSG_SIZE, &priv->pw_fifo_lock)) {
		/* Process one message */
#ifdef DEBUG_PW
		{
		u32 i;
		pr_debug("%s : Port-Write Message:", __func__);
		for (i = 0; i < RIO_PW_MSG_SIZE/sizeof(u32); ) {
			pr_debug("0x%02x: %08x %08x %08x %08x", i*4,
				msg_buffer[i], msg_buffer[i + 1],
				msg_buffer[i + 2], msg_buffer[i + 3]);
			i += 4;
		}
		pr_debug("\n");
		}
#endif
		/* Pass the port-write message to RIO core for processing */
		rio_inb_pwrite_handler((union rio_pw_msg *)msg_buffer);
	}
}

/**
 * tsi721_pw_enable - enable/disable port-write interface init
 * @mport: Master port implementing the port write unit
 * @enable:    1=enable; 0=disable port-write message handling
 */
static int tsi721_pw_enable(struct rio_mport *mport, int enable)
{
	struct tsi721_device *priv = mport->priv;
	u32 rval;

	rval = ioread32(priv->regs + TSI721_RIO_EM_INT_ENABLE);

	if (enable)
		rval |= TSI721_RIO_EM_INT_ENABLE_PW_RX;
	else
		rval &= ~TSI721_RIO_EM_INT_ENABLE_PW_RX;

	/* Clear pending PW interrupts */
	iowrite32(TSI721_RIO_PW_RX_STAT_PW_DISC | TSI721_RIO_PW_RX_STAT_PW_VAL,
		  priv->regs + TSI721_RIO_PW_RX_STAT);
	/* Update enable bits */
	iowrite32(rval, priv->regs + TSI721_RIO_EM_INT_ENABLE);

	return 0;
}

/**
 * tsi721_dsend - Send a RapidIO doorbell
 * @mport: RapidIO master port info
 * @index: ID of RapidIO interface
 * @destid: Destination ID of target device
 * @data: 16-bit info field of RapidIO doorbell
 *
 * Sends a RapidIO doorbell message. Always returns %0.
 */
static int tsi721_dsend(struct rio_mport *mport, int index,
			u16 destid, u16 data)
{
	struct tsi721_device *priv = mport->priv;
	u32 offset;

	offset = (((mport->sys_size) ? RIO_TT_CODE_16 : RIO_TT_CODE_8) << 18) |
		 (destid << 2);

	dev_dbg(&priv->pdev->dev,
		"Send Doorbell 0x%04x to destID 0x%x\n", data, destid);
	iowrite16be(data, priv->odb_base + offset);

	return 0;
}

/**
 * tsi721_dbell_handler - Tsi721 doorbell interrupt handler
 * @mport: RapidIO master port structure
 *
 * Handles inbound doorbell interrupts. Copies doorbell entry from an internal
 * buffer into DB message FIFO and schedules deferred  routine to process
 * queued DBs.
 */
static int
tsi721_dbell_handler(struct rio_mport *mport)
{
	struct tsi721_device *priv = mport->priv;
	u32 regval;

	/* Disable IDB interrupts */
	regval = ioread32(priv->regs + TSI721_SR_CHINTE(IDB_QUEUE));
	regval &= ~TSI721_SR_CHINT_IDBQRCV;
	iowrite32(regval,
		priv->regs + TSI721_SR_CHINTE(IDB_QUEUE));

	schedule_work(&priv->idb_work);

	return 0;
}

static void tsi721_db_dpc(struct work_struct *work)
{
	struct tsi721_device *priv = container_of(work, struct tsi721_device,
						    idb_work);
	struct rio_mport *mport;
	struct rio_dbell *dbell;
	int found = 0;
	u32 wr_ptr, rd_ptr;
	u64 *idb_entry;
	u32 regval;
	union {
		u64 msg;
		u8  bytes[8];
	} idb;

	/*
	 * Process queued inbound doorbells
	 */
	mport = priv->mport;

	wr_ptr = ioread32(priv->regs + TSI721_IDQ_WP(IDB_QUEUE)) % IDB_QSIZE;
	rd_ptr = ioread32(priv->regs + TSI721_IDQ_RP(IDB_QUEUE)) % IDB_QSIZE;

	while (wr_ptr != rd_ptr) {
		idb_entry = (u64 *)(priv->idb_base +
					(TSI721_IDB_ENTRY_SIZE * rd_ptr));
		rd_ptr++;
		rd_ptr %= IDB_QSIZE;
		idb.msg = *idb_entry;
		*idb_entry = 0;

		/* Process one doorbell */
		list_for_each_entry(dbell, &mport->dbells, node) {
			if ((dbell->res->start <= DBELL_INF(idb.bytes)) &&
			    (dbell->res->end >= DBELL_INF(idb.bytes))) {
				found = 1;
				break;
			}
		}

		if (found) {
			dbell->dinb(mport, dbell->dev_id, DBELL_SID(idb.bytes),
				    DBELL_TID(idb.bytes), DBELL_INF(idb.bytes));
		} else {
			dev_dbg(&priv->pdev->dev,
				"spurious inb doorbell, sid %2.2x tid %2.2x"
				" info %4.4x\n", DBELL_SID(idb.bytes),
				DBELL_TID(idb.bytes), DBELL_INF(idb.bytes));
		}
	}

	iowrite32(rd_ptr & (IDB_QSIZE - 1),
		priv->regs + TSI721_IDQ_RP(IDB_QUEUE));

	/* Re-enable IDB interrupts */
	regval = ioread32(priv->regs + TSI721_SR_CHINTE(IDB_QUEUE));
	regval |= TSI721_SR_CHINT_IDBQRCV;
	iowrite32(regval,
		priv->regs + TSI721_SR_CHINTE(IDB_QUEUE));
}

/**
 * tsi721_irqhandler - Tsi721 interrupt handler
 * @irq: Linux interrupt number
 * @ptr: Pointer to interrupt-specific data (mport structure)
 *
 * Handles Tsi721 interrupts signaled using MSI and INTA. Checks reported
 * interrupt events and calls an event-specific handler(s).
 */
static irqreturn_t tsi721_irqhandler(int irq, void *ptr)
{
	struct rio_mport *mport = (struct rio_mport *)ptr;
	struct tsi721_device *priv = mport->priv;
	u32 dev_int;
	u32 dev_ch_int;
	u32 intval;
	u32 ch_inte;

	dev_int = ioread32(priv->regs + TSI721_DEV_INT);
	if (!dev_int)
		return IRQ_NONE;

	dev_ch_int = ioread32(priv->regs + TSI721_DEV_CHAN_INT);

	if (dev_int & TSI721_DEV_INT_SR2PC_CH) {
		/* Service SR2PC Channel interrupts */
		if (dev_ch_int & TSI721_INT_SR2PC_CHAN(IDB_QUEUE)) {
			/* Service Inbound Doorbell interrupt */
			intval = ioread32(priv->regs +
						TSI721_SR_CHINT(IDB_QUEUE));
			if (intval & TSI721_SR_CHINT_IDBQRCV)
				tsi721_dbell_handler(mport);
			else
				dev_info(&priv->pdev->dev,
					"Unsupported SR_CH_INT %x\n", intval);

			/* Clear interrupts */
			iowrite32(intval,
				priv->regs + TSI721_SR_CHINT(IDB_QUEUE));
			ioread32(priv->regs + TSI721_SR_CHINT(IDB_QUEUE));
		}
	}

	if (dev_int & TSI721_DEV_INT_SMSG_CH) {
		int ch;

		/*
		 * Service channel interrupts from Messaging Engine
		 */

		if (dev_ch_int & TSI721_INT_IMSG_CHAN_M) { /* Inbound Msg */
			/* Disable signaled OB MSG Channel interrupts */
			ch_inte = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
			ch_inte &= ~(dev_ch_int & TSI721_INT_IMSG_CHAN_M);
			iowrite32(ch_inte, priv->regs + TSI721_DEV_CHAN_INTE);

			/*
			 * Process Inbound Message interrupt for each MBOX
			 */
			for (ch = 4; ch < RIO_MAX_MBOX + 4; ch++) {
				if (!(dev_ch_int & TSI721_INT_IMSG_CHAN(ch)))
					continue;
				tsi721_imsg_handler(priv, ch);
			}
		}

		if (dev_ch_int & TSI721_INT_OMSG_CHAN_M) { /* Outbound Msg */
			/* Disable signaled OB MSG Channel interrupts */
			ch_inte = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
			ch_inte &= ~(dev_ch_int & TSI721_INT_OMSG_CHAN_M);
			iowrite32(ch_inte, priv->regs + TSI721_DEV_CHAN_INTE);

			/*
			 * Process Outbound Message interrupts for each MBOX
			 */

			for (ch = 0; ch < RIO_MAX_MBOX; ch++) {
				if (!(dev_ch_int & TSI721_INT_OMSG_CHAN(ch)))
					continue;
				tsi721_omsg_handler(priv, ch);
			}
		}
	}

	if (dev_int & TSI721_DEV_INT_SRIO) {
		/* Service SRIO MAC interrupts */
		intval = ioread32(priv->regs + TSI721_RIO_EM_INT_STAT);
		if (intval & TSI721_RIO_EM_INT_STAT_PW_RX)
			tsi721_pw_handler(mport);
	}

	return IRQ_HANDLED;
}

static void tsi721_interrupts_init(struct tsi721_device *priv)
{
	u32 intr;

	/* Enable IDB interrupts */
	iowrite32(TSI721_SR_CHINT_ALL,
		priv->regs + TSI721_SR_CHINT(IDB_QUEUE));
	iowrite32(TSI721_SR_CHINT_IDBQRCV,
		priv->regs + TSI721_SR_CHINTE(IDB_QUEUE));
	iowrite32(TSI721_INT_SR2PC_CHAN(IDB_QUEUE),
		priv->regs + TSI721_DEV_CHAN_INTE);

	/* Enable SRIO MAC interrupts */
	iowrite32(TSI721_RIO_EM_DEV_INT_EN_INT,
		priv->regs + TSI721_RIO_EM_DEV_INT_EN);

	if (priv->flags & TSI721_USING_MSIX)
		intr = TSI721_DEV_INT_SRIO;
	else
		intr = TSI721_DEV_INT_SR2PC_CH | TSI721_DEV_INT_SRIO |
			TSI721_DEV_INT_SMSG_CH;

	iowrite32(intr, priv->regs + TSI721_DEV_INTE);
	ioread32(priv->regs + TSI721_DEV_INTE);
}

#ifdef CONFIG_PCI_MSI
/**
 * tsi721_omsg_msix - MSI-X interrupt handler for outbound messaging
 * @irq: Linux interrupt number
 * @ptr: Pointer to interrupt-specific data (mport structure)
 *
 * Handles outbound messaging interrupts signaled using MSI-X.
 */
static irqreturn_t tsi721_omsg_msix(int irq, void *ptr)
{
	struct tsi721_device *priv = ((struct rio_mport *)ptr)->priv;
	int mbox;

	mbox = (irq - priv->msix[TSI721_VECT_OMB0_DONE].vector) % RIO_MAX_MBOX;
	tsi721_omsg_handler(priv, mbox);
	return IRQ_HANDLED;
}

/**
 * tsi721_imsg_msix - MSI-X interrupt handler for inbound messaging
 * @irq: Linux interrupt number
 * @ptr: Pointer to interrupt-specific data (mport structure)
 *
 * Handles inbound messaging interrupts signaled using MSI-X.
 */
static irqreturn_t tsi721_imsg_msix(int irq, void *ptr)
{
	struct tsi721_device *priv = ((struct rio_mport *)ptr)->priv;
	int mbox;

	mbox = (irq - priv->msix[TSI721_VECT_IMB0_RCV].vector) % RIO_MAX_MBOX;
	tsi721_imsg_handler(priv, mbox + 4);
	return IRQ_HANDLED;
}

/**
 * tsi721_srio_msix - Tsi721 MSI-X SRIO MAC interrupt handler
 * @irq: Linux interrupt number
 * @ptr: Pointer to interrupt-specific data (mport structure)
 *
 * Handles Tsi721 interrupts from SRIO MAC.
 */
static irqreturn_t tsi721_srio_msix(int irq, void *ptr)
{
	struct tsi721_device *priv = ((struct rio_mport *)ptr)->priv;
	u32 srio_int;

	/* Service SRIO MAC interrupts */
	srio_int = ioread32(priv->regs + TSI721_RIO_EM_INT_STAT);
	if (srio_int & TSI721_RIO_EM_INT_STAT_PW_RX)
		tsi721_pw_handler((struct rio_mport *)ptr);

	return IRQ_HANDLED;
}

/**
 * tsi721_sr2pc_ch_msix - Tsi721 MSI-X SR2PC Channel interrupt handler
 * @irq: Linux interrupt number
 * @ptr: Pointer to interrupt-specific data (mport structure)
 *
 * Handles Tsi721 interrupts from SR2PC Channel.
 * NOTE: At this moment services only one SR2PC channel associated with inbound
 * doorbells.
 */
static irqreturn_t tsi721_sr2pc_ch_msix(int irq, void *ptr)
{
	struct tsi721_device *priv = ((struct rio_mport *)ptr)->priv;
	u32 sr_ch_int;

	/* Service Inbound DB interrupt from SR2PC channel */
	sr_ch_int = ioread32(priv->regs + TSI721_SR_CHINT(IDB_QUEUE));
	if (sr_ch_int & TSI721_SR_CHINT_IDBQRCV)
		tsi721_dbell_handler((struct rio_mport *)ptr);

	/* Clear interrupts */
	iowrite32(sr_ch_int, priv->regs + TSI721_SR_CHINT(IDB_QUEUE));
	/* Read back to ensure that interrupt was cleared */
	sr_ch_int = ioread32(priv->regs + TSI721_SR_CHINT(IDB_QUEUE));

	return IRQ_HANDLED;
}

/**
 * tsi721_request_msix - register interrupt service for MSI-X mode.
 * @mport: RapidIO master port structure
 *
 * Registers MSI-X interrupt service routines for interrupts that are active
 * immediately after mport initialization. Messaging interrupt service routines
 * should be registered during corresponding open requests.
 */
static int tsi721_request_msix(struct rio_mport *mport)
{
	struct tsi721_device *priv = mport->priv;
	int err = 0;

	err = request_irq(priv->msix[TSI721_VECT_IDB].vector,
			tsi721_sr2pc_ch_msix, 0,
			priv->msix[TSI721_VECT_IDB].irq_name, (void *)mport);
	if (err)
		goto out;

	err = request_irq(priv->msix[TSI721_VECT_PWRX].vector,
			tsi721_srio_msix, 0,
			priv->msix[TSI721_VECT_PWRX].irq_name, (void *)mport);
	if (err)
		free_irq(
			priv->msix[TSI721_VECT_IDB].vector,
			(void *)mport);
out:
	return err;
}

/**
 * tsi721_enable_msix - Attempts to enable MSI-X support for Tsi721.
 * @priv: pointer to tsi721 private data
 *
 * Configures MSI-X support for Tsi721. Supports only an exact number
 * of requested vectors.
 */
static int tsi721_enable_msix(struct tsi721_device *priv)
{
	struct msix_entry entries[TSI721_VECT_MAX];
	int err;
	int i;

	entries[TSI721_VECT_IDB].entry = TSI721_MSIX_SR2PC_IDBQ_RCV(IDB_QUEUE);
	entries[TSI721_VECT_PWRX].entry = TSI721_MSIX_SRIO_MAC_INT;

	/*
	 * Initialize MSI-X entries for Messaging Engine:
	 * this driver supports four RIO mailboxes (inbound and outbound)
	 * NOTE: Inbound message MBOX 0...4 use IB channels 4...7. Therefore
	 * offset +4 is added to IB MBOX number.
	 */
	for (i = 0; i < RIO_MAX_MBOX; i++) {
		entries[TSI721_VECT_IMB0_RCV + i].entry =
					TSI721_MSIX_IMSG_DQ_RCV(i + 4);
		entries[TSI721_VECT_IMB0_INT + i].entry =
					TSI721_MSIX_IMSG_INT(i + 4);
		entries[TSI721_VECT_OMB0_DONE + i].entry =
					TSI721_MSIX_OMSG_DONE(i);
		entries[TSI721_VECT_OMB0_INT + i].entry =
					TSI721_MSIX_OMSG_INT(i);
	}

	err = pci_enable_msix(priv->pdev, entries, ARRAY_SIZE(entries));
	if (err) {
		if (err > 0)
			dev_info(&priv->pdev->dev,
				 "Only %d MSI-X vectors available, "
				 "not using MSI-X\n", err);
		return err;
	}

	/*
	 * Copy MSI-X vector information into tsi721 private structure
	 */
	priv->msix[TSI721_VECT_IDB].vector = entries[TSI721_VECT_IDB].vector;
	snprintf(priv->msix[TSI721_VECT_IDB].irq_name, IRQ_DEVICE_NAME_MAX,
		 DRV_NAME "-idb@pci:%s", pci_name(priv->pdev));
	priv->msix[TSI721_VECT_PWRX].vector = entries[TSI721_VECT_PWRX].vector;
	snprintf(priv->msix[TSI721_VECT_PWRX].irq_name, IRQ_DEVICE_NAME_MAX,
		 DRV_NAME "-pwrx@pci:%s", pci_name(priv->pdev));

	for (i = 0; i < RIO_MAX_MBOX; i++) {
		priv->msix[TSI721_VECT_IMB0_RCV + i].vector =
				entries[TSI721_VECT_IMB0_RCV + i].vector;
		snprintf(priv->msix[TSI721_VECT_IMB0_RCV + i].irq_name,
			 IRQ_DEVICE_NAME_MAX, DRV_NAME "-imbr%d@pci:%s",
			 i, pci_name(priv->pdev));

		priv->msix[TSI721_VECT_IMB0_INT + i].vector =
				entries[TSI721_VECT_IMB0_INT + i].vector;
		snprintf(priv->msix[TSI721_VECT_IMB0_INT + i].irq_name,
			 IRQ_DEVICE_NAME_MAX, DRV_NAME "-imbi%d@pci:%s",
			 i, pci_name(priv->pdev));

		priv->msix[TSI721_VECT_OMB0_DONE + i].vector =
				entries[TSI721_VECT_OMB0_DONE + i].vector;
		snprintf(priv->msix[TSI721_VECT_OMB0_DONE + i].irq_name,
			 IRQ_DEVICE_NAME_MAX, DRV_NAME "-ombd%d@pci:%s",
			 i, pci_name(priv->pdev));

		priv->msix[TSI721_VECT_OMB0_INT + i].vector =
				entries[TSI721_VECT_OMB0_INT + i].vector;
		snprintf(priv->msix[TSI721_VECT_OMB0_INT + i].irq_name,
			 IRQ_DEVICE_NAME_MAX, DRV_NAME "-ombi%d@pci:%s",
			 i, pci_name(priv->pdev));
	}

	return 0;
}
#endif /* CONFIG_PCI_MSI */

static int tsi721_request_irq(struct rio_mport *mport)
{
	struct tsi721_device *priv = mport->priv;
	int err;

#ifdef CONFIG_PCI_MSI
	if (priv->flags & TSI721_USING_MSIX)
		err = tsi721_request_msix(mport);
	else
#endif
		err = request_irq(priv->pdev->irq, tsi721_irqhandler,
			  (priv->flags & TSI721_USING_MSI) ? 0 : IRQF_SHARED,
			  DRV_NAME, (void *)mport);

	if (err)
		dev_err(&priv->pdev->dev,
			"Unable to allocate interrupt, Error: %d\n", err);

	return err;
}

/**
 * tsi721_init_pc2sr_mapping - initializes outbound (PCIe->SRIO)
 * translation regions.
 * @priv: pointer to tsi721 private data
 *
 * Disables SREP translation regions.
 */
static void tsi721_init_pc2sr_mapping(struct tsi721_device *priv)
{
	int i;

	/* Disable all PC2SR translation windows */
	for (i = 0; i < TSI721_OBWIN_NUM; i++)
		iowrite32(0, priv->regs + TSI721_OBWINLB(i));
}

/**
 * tsi721_init_sr2pc_mapping - initializes inbound (SRIO->PCIe)
 * translation regions.
 * @priv: pointer to tsi721 private data
 *
 * Disables inbound windows.
 */
static void tsi721_init_sr2pc_mapping(struct tsi721_device *priv)
{
	int i;

	/* Disable all SR2PC inbound windows */
	for (i = 0; i < TSI721_IBWIN_NUM; i++)
		iowrite32(0, priv->regs + TSI721_IBWINLB(i));
}

/**
 * tsi721_port_write_init - Inbound port write interface init
 * @priv: pointer to tsi721 private data
 *
 * Initializes inbound port write handler.
 * Returns %0 on success or %-ENOMEM on failure.
 */
static int tsi721_port_write_init(struct tsi721_device *priv)
{
	priv->pw_discard_count = 0;
	INIT_WORK(&priv->pw_work, tsi721_pw_dpc);
	spin_lock_init(&priv->pw_fifo_lock);
	if (kfifo_alloc(&priv->pw_fifo,
			TSI721_RIO_PW_MSG_SIZE * 32, GFP_KERNEL)) {
		dev_err(&priv->pdev->dev, "PW FIFO allocation failed\n");
		return -ENOMEM;
	}

	/* Use reliable port-write capture mode */
	iowrite32(TSI721_RIO_PW_CTL_PWC_REL, priv->regs + TSI721_RIO_PW_CTL);
	return 0;
}

static int tsi721_doorbell_init(struct tsi721_device *priv)
{
	/* Outbound Doorbells do not require any setup.
	 * Tsi721 uses dedicated PCI BAR1 to generate doorbells.
	 * That BAR1 was mapped during the probe routine.
	 */

	/* Initialize Inbound Doorbell processing DPC and queue */
	priv->db_discard_count = 0;
	INIT_WORK(&priv->idb_work, tsi721_db_dpc);

	/* Allocate buffer for inbound doorbells queue */
	priv->idb_base = dma_zalloc_coherent(&priv->pdev->dev,
				IDB_QSIZE * TSI721_IDB_ENTRY_SIZE,
				&priv->idb_dma, GFP_KERNEL);
	if (!priv->idb_base)
		return -ENOMEM;

	dev_dbg(&priv->pdev->dev, "Allocated IDB buffer @ %p (phys = %llx)\n",
		priv->idb_base, (unsigned long long)priv->idb_dma);

	iowrite32(TSI721_IDQ_SIZE_VAL(IDB_QSIZE),
		priv->regs + TSI721_IDQ_SIZE(IDB_QUEUE));
	iowrite32(((u64)priv->idb_dma >> 32),
		priv->regs + TSI721_IDQ_BASEU(IDB_QUEUE));
	iowrite32(((u64)priv->idb_dma & TSI721_IDQ_BASEL_ADDR),
		priv->regs + TSI721_IDQ_BASEL(IDB_QUEUE));
	/* Enable accepting all inbound doorbells */
	iowrite32(0, priv->regs + TSI721_IDQ_MASK(IDB_QUEUE));

	iowrite32(TSI721_IDQ_INIT, priv->regs + TSI721_IDQ_CTL(IDB_QUEUE));

	iowrite32(0, priv->regs + TSI721_IDQ_RP(IDB_QUEUE));

	return 0;
}

static void tsi721_doorbell_free(struct tsi721_device *priv)
{
	if (priv->idb_base == NULL)
		return;

	/* Free buffer allocated for inbound doorbell queue */
	dma_free_coherent(&priv->pdev->dev, IDB_QSIZE * TSI721_IDB_ENTRY_SIZE,
			  priv->idb_base, priv->idb_dma);
	priv->idb_base = NULL;
}

static int tsi721_bdma_ch_init(struct tsi721_device *priv, int chnum)
{
	struct tsi721_dma_desc *bd_ptr;
	u64		*sts_ptr;
	dma_addr_t	bd_phys, sts_phys;
	int		sts_size;
	int		bd_num = priv->bdma[chnum].bd_num;

	dev_dbg(&priv->pdev->dev, "Init Block DMA Engine, CH%d\n", chnum);

	/*
	 * Initialize DMA channel for maintenance requests
	 */

	/* Allocate space for DMA descriptors */
	bd_ptr = dma_zalloc_coherent(&priv->pdev->dev,
					bd_num * sizeof(struct tsi721_dma_desc),
					&bd_phys, GFP_KERNEL);
	if (!bd_ptr)
		return -ENOMEM;

	priv->bdma[chnum].bd_phys = bd_phys;
	priv->bdma[chnum].bd_base = bd_ptr;

	dev_dbg(&priv->pdev->dev, "DMA descriptors @ %p (phys = %llx)\n",
		bd_ptr, (unsigned long long)bd_phys);

	/* Allocate space for descriptor status FIFO */
	sts_size = (bd_num >= TSI721_DMA_MINSTSSZ) ?
					bd_num : TSI721_DMA_MINSTSSZ;
	sts_size = roundup_pow_of_two(sts_size);
	sts_ptr = dma_zalloc_coherent(&priv->pdev->dev,
				     sts_size * sizeof(struct tsi721_dma_sts),
				     &sts_phys, GFP_KERNEL);
	if (!sts_ptr) {
		/* Free space allocated for DMA descriptors */
		dma_free_coherent(&priv->pdev->dev,
				  bd_num * sizeof(struct tsi721_dma_desc),
				  bd_ptr, bd_phys);
		priv->bdma[chnum].bd_base = NULL;
		return -ENOMEM;
	}

	priv->bdma[chnum].sts_phys = sts_phys;
	priv->bdma[chnum].sts_base = sts_ptr;
	priv->bdma[chnum].sts_size = sts_size;

	dev_dbg(&priv->pdev->dev,
		"desc status FIFO @ %p (phys = %llx) size=0x%x\n",
		sts_ptr, (unsigned long long)sts_phys, sts_size);

	/* Initialize DMA descriptors ring */
	bd_ptr[bd_num - 1].type_id = cpu_to_le32(DTYPE3 << 29);
	bd_ptr[bd_num - 1].next_lo = cpu_to_le32((u64)bd_phys &
						 TSI721_DMAC_DPTRL_MASK);
	bd_ptr[bd_num - 1].next_hi = cpu_to_le32((u64)bd_phys >> 32);

	/* Setup DMA descriptor pointers */
	iowrite32(((u64)bd_phys >> 32),
		priv->regs + TSI721_DMAC_DPTRH(chnum));
	iowrite32(((u64)bd_phys & TSI721_DMAC_DPTRL_MASK),
		priv->regs + TSI721_DMAC_DPTRL(chnum));

	/* Setup descriptor status FIFO */
	iowrite32(((u64)sts_phys >> 32),
		priv->regs + TSI721_DMAC_DSBH(chnum));
	iowrite32(((u64)sts_phys & TSI721_DMAC_DSBL_MASK),
		priv->regs + TSI721_DMAC_DSBL(chnum));
	iowrite32(TSI721_DMAC_DSSZ_SIZE(sts_size),
		priv->regs + TSI721_DMAC_DSSZ(chnum));

	/* Clear interrupt bits */
	iowrite32(TSI721_DMAC_INT_ALL,
		priv->regs + TSI721_DMAC_INT(chnum));

	ioread32(priv->regs + TSI721_DMAC_INT(chnum));

	/* Toggle DMA channel initialization */
	iowrite32(TSI721_DMAC_CTL_INIT,	priv->regs + TSI721_DMAC_CTL(chnum));
	ioread32(priv->regs + TSI721_DMAC_CTL(chnum));
	udelay(10);

	return 0;
}

static int tsi721_bdma_ch_free(struct tsi721_device *priv, int chnum)
{
	u32 ch_stat;

	if (priv->bdma[chnum].bd_base == NULL)
		return 0;

	/* Check if DMA channel still running */
	ch_stat = ioread32(priv->regs +	TSI721_DMAC_STS(chnum));
	if (ch_stat & TSI721_DMAC_STS_RUN)
		return -EFAULT;

	/* Put DMA channel into init state */
	iowrite32(TSI721_DMAC_CTL_INIT,
		priv->regs + TSI721_DMAC_CTL(chnum));

	/* Free space allocated for DMA descriptors */
	dma_free_coherent(&priv->pdev->dev,
		priv->bdma[chnum].bd_num * sizeof(struct tsi721_dma_desc),
		priv->bdma[chnum].bd_base, priv->bdma[chnum].bd_phys);
	priv->bdma[chnum].bd_base = NULL;

	/* Free space allocated for status FIFO */
	dma_free_coherent(&priv->pdev->dev,
		priv->bdma[chnum].sts_size * sizeof(struct tsi721_dma_sts),
		priv->bdma[chnum].sts_base, priv->bdma[chnum].sts_phys);
	priv->bdma[chnum].sts_base = NULL;
	return 0;
}

static int tsi721_bdma_init(struct tsi721_device *priv)
{
	/* Initialize BDMA channel allocated for RapidIO maintenance read/write
	 * request generation
	 */
	priv->bdma[TSI721_DMACH_MAINT].bd_num = 2;
	if (tsi721_bdma_ch_init(priv, TSI721_DMACH_MAINT)) {
		dev_err(&priv->pdev->dev, "Unable to initialize maintenance DMA"
			" channel %d, aborting\n", TSI721_DMACH_MAINT);
		return -ENOMEM;
	}

	return 0;
}

static void tsi721_bdma_free(struct tsi721_device *priv)
{
	tsi721_bdma_ch_free(priv, TSI721_DMACH_MAINT);
}

/* Enable Inbound Messaging Interrupts */
static void
tsi721_imsg_interrupt_enable(struct tsi721_device *priv, int ch,
				  u32 inte_mask)
{
	u32 rval;

	if (!inte_mask)
		return;

	/* Clear pending Inbound Messaging interrupts */
	iowrite32(inte_mask, priv->regs + TSI721_IBDMAC_INT(ch));

	/* Enable Inbound Messaging interrupts */
	rval = ioread32(priv->regs + TSI721_IBDMAC_INTE(ch));
	iowrite32(rval | inte_mask, priv->regs + TSI721_IBDMAC_INTE(ch));

	if (priv->flags & TSI721_USING_MSIX)
		return; /* Finished if we are in MSI-X mode */

	/*
	 * For MSI and INTA interrupt signalling we need to enable next levels
	 */

	/* Enable Device Channel Interrupt */
	rval = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
	iowrite32(rval | TSI721_INT_IMSG_CHAN(ch),
		  priv->regs + TSI721_DEV_CHAN_INTE);
}

/* Disable Inbound Messaging Interrupts */
static void
tsi721_imsg_interrupt_disable(struct tsi721_device *priv, int ch,
				   u32 inte_mask)
{
	u32 rval;

	if (!inte_mask)
		return;

	/* Clear pending Inbound Messaging interrupts */
	iowrite32(inte_mask, priv->regs + TSI721_IBDMAC_INT(ch));

	/* Disable Inbound Messaging interrupts */
	rval = ioread32(priv->regs + TSI721_IBDMAC_INTE(ch));
	rval &= ~inte_mask;
	iowrite32(rval, priv->regs + TSI721_IBDMAC_INTE(ch));

	if (priv->flags & TSI721_USING_MSIX)
		return; /* Finished if we are in MSI-X mode */

	/*
	 * For MSI and INTA interrupt signalling we need to disable next levels
	 */

	/* Disable Device Channel Interrupt */
	rval = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
	rval &= ~TSI721_INT_IMSG_CHAN(ch);
	iowrite32(rval, priv->regs + TSI721_DEV_CHAN_INTE);
}

/* Enable Outbound Messaging interrupts */
static void
tsi721_omsg_interrupt_enable(struct tsi721_device *priv, int ch,
				  u32 inte_mask)
{
	u32 rval;

	if (!inte_mask)
		return;

	/* Clear pending Outbound Messaging interrupts */
	iowrite32(inte_mask, priv->regs + TSI721_OBDMAC_INT(ch));

	/* Enable Outbound Messaging channel interrupts */
	rval = ioread32(priv->regs + TSI721_OBDMAC_INTE(ch));
	iowrite32(rval | inte_mask, priv->regs + TSI721_OBDMAC_INTE(ch));

	if (priv->flags & TSI721_USING_MSIX)
		return; /* Finished if we are in MSI-X mode */

	/*
	 * For MSI and INTA interrupt signalling we need to enable next levels
	 */

	/* Enable Device Channel Interrupt */
	rval = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
	iowrite32(rval | TSI721_INT_OMSG_CHAN(ch),
		  priv->regs + TSI721_DEV_CHAN_INTE);
}

/* Disable Outbound Messaging interrupts */
static void
tsi721_omsg_interrupt_disable(struct tsi721_device *priv, int ch,
				   u32 inte_mask)
{
	u32 rval;

	if (!inte_mask)
		return;

	/* Clear pending Outbound Messaging interrupts */
	iowrite32(inte_mask, priv->regs + TSI721_OBDMAC_INT(ch));

	/* Disable Outbound Messaging interrupts */
	rval = ioread32(priv->regs + TSI721_OBDMAC_INTE(ch));
	rval &= ~inte_mask;
	iowrite32(rval, priv->regs + TSI721_OBDMAC_INTE(ch));

	if (priv->flags & TSI721_USING_MSIX)
		return; /* Finished if we are in MSI-X mode */

	/*
	 * For MSI and INTA interrupt signalling we need to disable next levels
	 */

	/* Disable Device Channel Interrupt */
	rval = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
	rval &= ~TSI721_INT_OMSG_CHAN(ch);
	iowrite32(rval, priv->regs + TSI721_DEV_CHAN_INTE);
}

/**
 * tsi721_add_outb_message - Add message to the Tsi721 outbound message queue
 * @mport: Master port with outbound message queue
 * @rdev: Target of outbound message
 * @mbox: Outbound mailbox
 * @buffer: Message to add to outbound queue
 * @len: Length of message
 */
static int
tsi721_add_outb_message(struct rio_mport *mport, struct rio_dev *rdev, int mbox,
			void *buffer, size_t len)
{
	struct tsi721_device *priv = mport->priv;
	struct tsi721_omsg_desc *desc;
	u32 tx_slot;

	if (!priv->omsg_init[mbox] ||
	    len > TSI721_MSG_MAX_SIZE || len < 8)
		return -EINVAL;

	tx_slot = priv->omsg_ring[mbox].tx_slot;

	/* Copy copy message into transfer buffer */
	memcpy(priv->omsg_ring[mbox].omq_base[tx_slot], buffer, len);

	if (len & 0x7)
		len += 8;

	/* Build descriptor associated with buffer */
	desc = priv->omsg_ring[mbox].omd_base;
	desc[tx_slot].type_id = cpu_to_le32((DTYPE4 << 29) | rdev->destid);
	if (tx_slot % 4 == 0)
		desc[tx_slot].type_id |= cpu_to_le32(TSI721_OMD_IOF);

	desc[tx_slot].msg_info =
		cpu_to_le32((mport->sys_size << 26) | (mbox << 22) |
			    (0xe << 12) | (len & 0xff8));
	desc[tx_slot].bufptr_lo =
		cpu_to_le32((u64)priv->omsg_ring[mbox].omq_phys[tx_slot] &
			    0xffffffff);
	desc[tx_slot].bufptr_hi =
		cpu_to_le32((u64)priv->omsg_ring[mbox].omq_phys[tx_slot] >> 32);

	priv->omsg_ring[mbox].wr_count++;

	/* Go to next descriptor */
	if (++priv->omsg_ring[mbox].tx_slot == priv->omsg_ring[mbox].size) {
		priv->omsg_ring[mbox].tx_slot = 0;
		/* Move through the ring link descriptor at the end */
		priv->omsg_ring[mbox].wr_count++;
	}

	mb();

	/* Set new write count value */
	iowrite32(priv->omsg_ring[mbox].wr_count,
		priv->regs + TSI721_OBDMAC_DWRCNT(mbox));
	ioread32(priv->regs + TSI721_OBDMAC_DWRCNT(mbox));

	return 0;
}

/**
 * tsi721_omsg_handler - Outbound Message Interrupt Handler
 * @priv: pointer to tsi721 private data
 * @ch:   number of OB MSG channel to service
 *
 * Services channel interrupts from outbound messaging engine.
 */
static void tsi721_omsg_handler(struct tsi721_device *priv, int ch)
{
	u32 omsg_int;

	spin_lock(&priv->omsg_ring[ch].lock);

	omsg_int = ioread32(priv->regs + TSI721_OBDMAC_INT(ch));

	if (omsg_int & TSI721_OBDMAC_INT_ST_FULL)
		dev_info(&priv->pdev->dev,
			"OB MBOX%d: Status FIFO is full\n", ch);

	if (omsg_int & (TSI721_OBDMAC_INT_DONE | TSI721_OBDMAC_INT_IOF_DONE)) {
		u32 srd_ptr;
		u64 *sts_ptr, last_ptr = 0, prev_ptr = 0;
		int i, j;
		u32 tx_slot;

		/*
		 * Find last successfully processed descriptor
		 */

		/* Check and clear descriptor status FIFO entries */
		srd_ptr = priv->omsg_ring[ch].sts_rdptr;
		sts_ptr = priv->omsg_ring[ch].sts_base;
		j = srd_ptr * 8;
		while (sts_ptr[j]) {
			for (i = 0; i < 8 && sts_ptr[j]; i++, j++) {
				prev_ptr = last_ptr;
				last_ptr = le64_to_cpu(sts_ptr[j]);
				sts_ptr[j] = 0;
			}

			++srd_ptr;
			srd_ptr %= priv->omsg_ring[ch].sts_size;
			j = srd_ptr * 8;
		}

		if (last_ptr == 0)
			goto no_sts_update;

		priv->omsg_ring[ch].sts_rdptr = srd_ptr;
		iowrite32(srd_ptr, priv->regs + TSI721_OBDMAC_DSRP(ch));

		if (!priv->mport->outb_msg[ch].mcback)
			goto no_sts_update;

		/* Inform upper layer about transfer completion */

		tx_slot = (last_ptr - (u64)priv->omsg_ring[ch].omd_phys)/
						sizeof(struct tsi721_omsg_desc);

		/*
		 * Check if this is a Link Descriptor (LD).
		 * If yes, ignore LD and use descriptor processed
		 * before LD.
		 */
		if (tx_slot == priv->omsg_ring[ch].size) {
			if (prev_ptr)
				tx_slot = (prev_ptr -
					(u64)priv->omsg_ring[ch].omd_phys)/
						sizeof(struct tsi721_omsg_desc);
			else
				goto no_sts_update;
		}

		/* Move slot index to the next message to be sent */
		++tx_slot;
		if (tx_slot == priv->omsg_ring[ch].size)
			tx_slot = 0;
		BUG_ON(tx_slot >= priv->omsg_ring[ch].size);
		priv->mport->outb_msg[ch].mcback(priv->mport,
				priv->omsg_ring[ch].dev_id, ch,
				tx_slot);
	}

no_sts_update:

	if (omsg_int & TSI721_OBDMAC_INT_ERROR) {
		/*
		* Outbound message operation aborted due to error,
		* reinitialize OB MSG channel
		*/

		dev_dbg(&priv->pdev->dev, "OB MSG ABORT ch_stat=%x\n",
			ioread32(priv->regs + TSI721_OBDMAC_STS(ch)));

		iowrite32(TSI721_OBDMAC_INT_ERROR,
				priv->regs + TSI721_OBDMAC_INT(ch));
		iowrite32(TSI721_OBDMAC_CTL_INIT,
				priv->regs + TSI721_OBDMAC_CTL(ch));
		ioread32(priv->regs + TSI721_OBDMAC_CTL(ch));

		/* Inform upper level to clear all pending tx slots */
		if (priv->mport->outb_msg[ch].mcback)
			priv->mport->outb_msg[ch].mcback(priv->mport,
					priv->omsg_ring[ch].dev_id, ch,
					priv->omsg_ring[ch].tx_slot);
		/* Synch tx_slot tracking */
		iowrite32(priv->omsg_ring[ch].tx_slot,
			priv->regs + TSI721_OBDMAC_DRDCNT(ch));
		ioread32(priv->regs + TSI721_OBDMAC_DRDCNT(ch));
		priv->omsg_ring[ch].wr_count = priv->omsg_ring[ch].tx_slot;
		priv->omsg_ring[ch].sts_rdptr = 0;
	}

	/* Clear channel interrupts */
	iowrite32(omsg_int, priv->regs + TSI721_OBDMAC_INT(ch));

	if (!(priv->flags & TSI721_USING_MSIX)) {
		u32 ch_inte;

		/* Re-enable channel interrupts */
		ch_inte = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
		ch_inte |= TSI721_INT_OMSG_CHAN(ch);
		iowrite32(ch_inte, priv->regs + TSI721_DEV_CHAN_INTE);
	}

	spin_unlock(&priv->omsg_ring[ch].lock);
}

/**
 * tsi721_open_outb_mbox - Initialize Tsi721 outbound mailbox
 * @mport: Master port implementing Outbound Messaging Engine
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the outbound mailbox ring
 */
static int tsi721_open_outb_mbox(struct rio_mport *mport, void *dev_id,
				 int mbox, int entries)
{
	struct tsi721_device *priv = mport->priv;
	struct tsi721_omsg_desc *bd_ptr;
	int i, rc = 0;

	if ((entries < TSI721_OMSGD_MIN_RING_SIZE) ||
	    (entries > (TSI721_OMSGD_RING_SIZE)) ||
	    (!is_power_of_2(entries)) || mbox >= RIO_MAX_MBOX) {
		rc = -EINVAL;
		goto out;
	}

	priv->omsg_ring[mbox].dev_id = dev_id;
	priv->omsg_ring[mbox].size = entries;
	priv->omsg_ring[mbox].sts_rdptr = 0;
	spin_lock_init(&priv->omsg_ring[mbox].lock);

	/* Outbound Msg Buffer allocation based on
	   the number of maximum descriptor entries */
	for (i = 0; i < entries; i++) {
		priv->omsg_ring[mbox].omq_base[i] =
			dma_alloc_coherent(
				&priv->pdev->dev, TSI721_MSG_BUFFER_SIZE,
				&priv->omsg_ring[mbox].omq_phys[i],
				GFP_KERNEL);
		if (priv->omsg_ring[mbox].omq_base[i] == NULL) {
			dev_dbg(&priv->pdev->dev,
				"Unable to allocate OB MSG data buffer for"
				" MBOX%d\n", mbox);
			rc = -ENOMEM;
			goto out_buf;
		}
	}

	/* Outbound message descriptor allocation */
	priv->omsg_ring[mbox].omd_base = dma_alloc_coherent(
				&priv->pdev->dev,
				(entries + 1) * sizeof(struct tsi721_omsg_desc),
				&priv->omsg_ring[mbox].omd_phys, GFP_KERNEL);
	if (priv->omsg_ring[mbox].omd_base == NULL) {
		dev_dbg(&priv->pdev->dev,
			"Unable to allocate OB MSG descriptor memory "
			"for MBOX%d\n", mbox);
		rc = -ENOMEM;
		goto out_buf;
	}

	priv->omsg_ring[mbox].tx_slot = 0;

	/* Outbound message descriptor status FIFO allocation */
	priv->omsg_ring[mbox].sts_size = roundup_pow_of_two(entries + 1);
	priv->omsg_ring[mbox].sts_base = dma_zalloc_coherent(&priv->pdev->dev,
			priv->omsg_ring[mbox].sts_size *
						sizeof(struct tsi721_dma_sts),
			&priv->omsg_ring[mbox].sts_phys, GFP_KERNEL);
	if (priv->omsg_ring[mbox].sts_base == NULL) {
		dev_dbg(&priv->pdev->dev,
			"Unable to allocate OB MSG descriptor status FIFO "
			"for MBOX%d\n", mbox);
		rc = -ENOMEM;
		goto out_desc;
	}

	/*
	 * Configure Outbound Messaging Engine
	 */

	/* Setup Outbound Message descriptor pointer */
	iowrite32(((u64)priv->omsg_ring[mbox].omd_phys >> 32),
			priv->regs + TSI721_OBDMAC_DPTRH(mbox));
	iowrite32(((u64)priv->omsg_ring[mbox].omd_phys &
					TSI721_OBDMAC_DPTRL_MASK),
			priv->regs + TSI721_OBDMAC_DPTRL(mbox));

	/* Setup Outbound Message descriptor status FIFO */
	iowrite32(((u64)priv->omsg_ring[mbox].sts_phys >> 32),
			priv->regs + TSI721_OBDMAC_DSBH(mbox));
	iowrite32(((u64)priv->omsg_ring[mbox].sts_phys &
					TSI721_OBDMAC_DSBL_MASK),
			priv->regs + TSI721_OBDMAC_DSBL(mbox));
	iowrite32(TSI721_DMAC_DSSZ_SIZE(priv->omsg_ring[mbox].sts_size),
		priv->regs + (u32)TSI721_OBDMAC_DSSZ(mbox));

	/* Enable interrupts */

#ifdef CONFIG_PCI_MSI
	if (priv->flags & TSI721_USING_MSIX) {
		/* Request interrupt service if we are in MSI-X mode */
		rc = request_irq(
			priv->msix[TSI721_VECT_OMB0_DONE + mbox].vector,
			tsi721_omsg_msix, 0,
			priv->msix[TSI721_VECT_OMB0_DONE + mbox].irq_name,
			(void *)mport);

		if (rc) {
			dev_dbg(&priv->pdev->dev,
				"Unable to allocate MSI-X interrupt for "
				"OBOX%d-DONE\n", mbox);
			goto out_stat;
		}

		rc = request_irq(priv->msix[TSI721_VECT_OMB0_INT + mbox].vector,
			tsi721_omsg_msix, 0,
			priv->msix[TSI721_VECT_OMB0_INT + mbox].irq_name,
			(void *)mport);

		if (rc)	{
			dev_dbg(&priv->pdev->dev,
				"Unable to allocate MSI-X interrupt for "
				"MBOX%d-INT\n", mbox);
			free_irq(
				priv->msix[TSI721_VECT_OMB0_DONE + mbox].vector,
				(void *)mport);
			goto out_stat;
		}
	}
#endif /* CONFIG_PCI_MSI */

	tsi721_omsg_interrupt_enable(priv, mbox, TSI721_OBDMAC_INT_ALL);

	/* Initialize Outbound Message descriptors ring */
	bd_ptr = priv->omsg_ring[mbox].omd_base;
	bd_ptr[entries].type_id = cpu_to_le32(DTYPE5 << 29);
	bd_ptr[entries].msg_info = 0;
	bd_ptr[entries].next_lo =
		cpu_to_le32((u64)priv->omsg_ring[mbox].omd_phys &
		TSI721_OBDMAC_DPTRL_MASK);
	bd_ptr[entries].next_hi =
		cpu_to_le32((u64)priv->omsg_ring[mbox].omd_phys >> 32);
	priv->omsg_ring[mbox].wr_count = 0;
	mb();

	/* Initialize Outbound Message engine */
	iowrite32(TSI721_OBDMAC_CTL_INIT, priv->regs + TSI721_OBDMAC_CTL(mbox));
	ioread32(priv->regs + TSI721_OBDMAC_DWRCNT(mbox));
	udelay(10);

	priv->omsg_init[mbox] = 1;

	return 0;

#ifdef CONFIG_PCI_MSI
out_stat:
	dma_free_coherent(&priv->pdev->dev,
		priv->omsg_ring[mbox].sts_size * sizeof(struct tsi721_dma_sts),
		priv->omsg_ring[mbox].sts_base,
		priv->omsg_ring[mbox].sts_phys);

	priv->omsg_ring[mbox].sts_base = NULL;
#endif /* CONFIG_PCI_MSI */

out_desc:
	dma_free_coherent(&priv->pdev->dev,
		(entries + 1) * sizeof(struct tsi721_omsg_desc),
		priv->omsg_ring[mbox].omd_base,
		priv->omsg_ring[mbox].omd_phys);

	priv->omsg_ring[mbox].omd_base = NULL;

out_buf:
	for (i = 0; i < priv->omsg_ring[mbox].size; i++) {
		if (priv->omsg_ring[mbox].omq_base[i]) {
			dma_free_coherent(&priv->pdev->dev,
				TSI721_MSG_BUFFER_SIZE,
				priv->omsg_ring[mbox].omq_base[i],
				priv->omsg_ring[mbox].omq_phys[i]);

			priv->omsg_ring[mbox].omq_base[i] = NULL;
		}
	}

out:
	return rc;
}

/**
 * tsi721_close_outb_mbox - Close Tsi721 outbound mailbox
 * @mport: Master port implementing the outbound message unit
 * @mbox: Mailbox to close
 */
static void tsi721_close_outb_mbox(struct rio_mport *mport, int mbox)
{
	struct tsi721_device *priv = mport->priv;
	u32 i;

	if (!priv->omsg_init[mbox])
		return;
	priv->omsg_init[mbox] = 0;

	/* Disable Interrupts */

	tsi721_omsg_interrupt_disable(priv, mbox, TSI721_OBDMAC_INT_ALL);

#ifdef CONFIG_PCI_MSI
	if (priv->flags & TSI721_USING_MSIX) {
		free_irq(priv->msix[TSI721_VECT_OMB0_DONE + mbox].vector,
			 (void *)mport);
		free_irq(priv->msix[TSI721_VECT_OMB0_INT + mbox].vector,
			 (void *)mport);
	}
#endif /* CONFIG_PCI_MSI */

	/* Free OMSG Descriptor Status FIFO */
	dma_free_coherent(&priv->pdev->dev,
		priv->omsg_ring[mbox].sts_size * sizeof(struct tsi721_dma_sts),
		priv->omsg_ring[mbox].sts_base,
		priv->omsg_ring[mbox].sts_phys);

	priv->omsg_ring[mbox].sts_base = NULL;

	/* Free OMSG descriptors */
	dma_free_coherent(&priv->pdev->dev,
		(priv->omsg_ring[mbox].size + 1) *
			sizeof(struct tsi721_omsg_desc),
		priv->omsg_ring[mbox].omd_base,
		priv->omsg_ring[mbox].omd_phys);

	priv->omsg_ring[mbox].omd_base = NULL;

	/* Free message buffers */
	for (i = 0; i < priv->omsg_ring[mbox].size; i++) {
		if (priv->omsg_ring[mbox].omq_base[i]) {
			dma_free_coherent(&priv->pdev->dev,
				TSI721_MSG_BUFFER_SIZE,
				priv->omsg_ring[mbox].omq_base[i],
				priv->omsg_ring[mbox].omq_phys[i]);

			priv->omsg_ring[mbox].omq_base[i] = NULL;
		}
	}
}

/**
 * tsi721_imsg_handler - Inbound Message Interrupt Handler
 * @priv: pointer to tsi721 private data
 * @ch: inbound message channel number to service
 *
 * Services channel interrupts from inbound messaging engine.
 */
static void tsi721_imsg_handler(struct tsi721_device *priv, int ch)
{
	u32 mbox = ch - 4;
	u32 imsg_int;

	spin_lock(&priv->imsg_ring[mbox].lock);

	imsg_int = ioread32(priv->regs + TSI721_IBDMAC_INT(ch));

	if (imsg_int & TSI721_IBDMAC_INT_SRTO)
		dev_info(&priv->pdev->dev, "IB MBOX%d SRIO timeout\n",
			mbox);

	if (imsg_int & TSI721_IBDMAC_INT_PC_ERROR)
		dev_info(&priv->pdev->dev, "IB MBOX%d PCIe error\n",
			mbox);

	if (imsg_int & TSI721_IBDMAC_INT_FQ_LOW)
		dev_info(&priv->pdev->dev,
			"IB MBOX%d IB free queue low\n", mbox);

	/* Clear IB channel interrupts */
	iowrite32(imsg_int, priv->regs + TSI721_IBDMAC_INT(ch));

	/* If an IB Msg is received notify the upper layer */
	if (imsg_int & TSI721_IBDMAC_INT_DQ_RCV &&
		priv->mport->inb_msg[mbox].mcback)
		priv->mport->inb_msg[mbox].mcback(priv->mport,
				priv->imsg_ring[mbox].dev_id, mbox, -1);

	if (!(priv->flags & TSI721_USING_MSIX)) {
		u32 ch_inte;

		/* Re-enable channel interrupts */
		ch_inte = ioread32(priv->regs + TSI721_DEV_CHAN_INTE);
		ch_inte |= TSI721_INT_IMSG_CHAN(ch);
		iowrite32(ch_inte, priv->regs + TSI721_DEV_CHAN_INTE);
	}

	spin_unlock(&priv->imsg_ring[mbox].lock);
}

/**
 * tsi721_open_inb_mbox - Initialize Tsi721 inbound mailbox
 * @mport: Master port implementing the Inbound Messaging Engine
 * @dev_id: Device specific pointer to pass on event
 * @mbox: Mailbox to open
 * @entries: Number of entries in the inbound mailbox ring
 */
static int tsi721_open_inb_mbox(struct rio_mport *mport, void *dev_id,
				int mbox, int entries)
{
	struct tsi721_device *priv = mport->priv;
	int ch = mbox + 4;
	int i;
	u64 *free_ptr;
	int rc = 0;

	if ((entries < TSI721_IMSGD_MIN_RING_SIZE) ||
	    (entries > TSI721_IMSGD_RING_SIZE) ||
	    (!is_power_of_2(entries)) || mbox >= RIO_MAX_MBOX) {
		rc = -EINVAL;
		goto out;
	}

	/* Initialize IB Messaging Ring */
	priv->imsg_ring[mbox].dev_id = dev_id;
	priv->imsg_ring[mbox].size = entries;
	priv->imsg_ring[mbox].rx_slot = 0;
	priv->imsg_ring[mbox].desc_rdptr = 0;
	priv->imsg_ring[mbox].fq_wrptr = 0;
	for (i = 0; i < priv->imsg_ring[mbox].size; i++)
		priv->imsg_ring[mbox].imq_base[i] = NULL;
	spin_lock_init(&priv->imsg_ring[mbox].lock);

	/* Allocate buffers for incoming messages */
	priv->imsg_ring[mbox].buf_base =
		dma_alloc_coherent(&priv->pdev->dev,
				   entries * TSI721_MSG_BUFFER_SIZE,
				   &priv->imsg_ring[mbox].buf_phys,
				   GFP_KERNEL);

	if (priv->imsg_ring[mbox].buf_base == NULL) {
		dev_err(&priv->pdev->dev,
			"Failed to allocate buffers for IB MBOX%d\n", mbox);
		rc = -ENOMEM;
		goto out;
	}

	/* Allocate memory for circular free list */
	priv->imsg_ring[mbox].imfq_base =
		dma_alloc_coherent(&priv->pdev->dev,
				   entries * 8,
				   &priv->imsg_ring[mbox].imfq_phys,
				   GFP_KERNEL);

	if (priv->imsg_ring[mbox].imfq_base == NULL) {
		dev_err(&priv->pdev->dev,
			"Failed to allocate free queue for IB MBOX%d\n", mbox);
		rc = -ENOMEM;
		goto out_buf;
	}

	/* Allocate memory for Inbound message descriptors */
	priv->imsg_ring[mbox].imd_base =
		dma_alloc_coherent(&priv->pdev->dev,
				   entries * sizeof(struct tsi721_imsg_desc),
				   &priv->imsg_ring[mbox].imd_phys, GFP_KERNEL);

	if (priv->imsg_ring[mbox].imd_base == NULL) {
		dev_err(&priv->pdev->dev,
			"Failed to allocate descriptor memory for IB MBOX%d\n",
			mbox);
		rc = -ENOMEM;
		goto out_dma;
	}

	/* Fill free buffer pointer list */
	free_ptr = priv->imsg_ring[mbox].imfq_base;
	for (i = 0; i < entries; i++)
		free_ptr[i] = cpu_to_le64(
				(u64)(priv->imsg_ring[mbox].buf_phys) +
				i * 0x1000);

	mb();

	/*
	 * For mapping of inbound SRIO Messages into appropriate queues we need
	 * to set Inbound Device ID register in the messaging engine. We do it
	 * once when first inbound mailbox is requested.
	 */
	if (!(priv->flags & TSI721_IMSGID_SET)) {
		iowrite32((u32)priv->mport->host_deviceid,
			priv->regs + TSI721_IB_DEVID);
		priv->flags |= TSI721_IMSGID_SET;
	}

	/*
	 * Configure Inbound Messaging channel (ch = mbox + 4)
	 */

	/* Setup Inbound Message free queue */
	iowrite32(((u64)priv->imsg_ring[mbox].imfq_phys >> 32),
		priv->regs + TSI721_IBDMAC_FQBH(ch));
	iowrite32(((u64)priv->imsg_ring[mbox].imfq_phys &
			TSI721_IBDMAC_FQBL_MASK),
		priv->regs+TSI721_IBDMAC_FQBL(ch));
	iowrite32(TSI721_DMAC_DSSZ_SIZE(entries),
		priv->regs + TSI721_IBDMAC_FQSZ(ch));

	/* Setup Inbound Message descriptor queue */
	iowrite32(((u64)priv->imsg_ring[mbox].imd_phys >> 32),
		priv->regs + TSI721_IBDMAC_DQBH(ch));
	iowrite32(((u32)priv->imsg_ring[mbox].imd_phys &
		   (u32)TSI721_IBDMAC_DQBL_MASK),
		priv->regs+TSI721_IBDMAC_DQBL(ch));
	iowrite32(TSI721_DMAC_DSSZ_SIZE(entries),
		priv->regs + TSI721_IBDMAC_DQSZ(ch));

	/* Enable interrupts */

#ifdef CONFIG_PCI_MSI
	if (priv->flags & TSI721_USING_MSIX) {
		/* Request interrupt service if we are in MSI-X mode */
		rc = request_irq(priv->msix[TSI721_VECT_IMB0_RCV + mbox].vector,
			tsi721_imsg_msix, 0,
			priv->msix[TSI721_VECT_IMB0_RCV + mbox].irq_name,
			(void *)mport);

		if (rc) {
			dev_dbg(&priv->pdev->dev,
				"Unable to allocate MSI-X interrupt for "
				"IBOX%d-DONE\n", mbox);
			goto out_desc;
		}

		rc = request_irq(priv->msix[TSI721_VECT_IMB0_INT + mbox].vector,
			tsi721_imsg_msix, 0,
			priv->msix[TSI721_VECT_IMB0_INT + mbox].irq_name,
			(void *)mport);

		if (rc)	{
			dev_dbg(&priv->pdev->dev,
				"Unable to allocate MSI-X interrupt for "
				"IBOX%d-INT\n", mbox);
			free_irq(
				priv->msix[TSI721_VECT_IMB0_RCV + mbox].vector,
				(void *)mport);
			goto out_desc;
		}
	}
#endif /* CONFIG_PCI_MSI */

	tsi721_imsg_interrupt_enable(priv, ch, TSI721_IBDMAC_INT_ALL);

	/* Initialize Inbound Message Engine */
	iowrite32(TSI721_IBDMAC_CTL_INIT, priv->regs + TSI721_IBDMAC_CTL(ch));
	ioread32(priv->regs + TSI721_IBDMAC_CTL(ch));
	udelay(10);
	priv->imsg_ring[mbox].fq_wrptr = entries - 1;
	iowrite32(entries - 1, priv->regs + TSI721_IBDMAC_FQWP(ch));

	priv->imsg_init[mbox] = 1;
	return 0;

#ifdef CONFIG_PCI_MSI
out_desc:
	dma_free_coherent(&priv->pdev->dev,
		priv->imsg_ring[mbox].size * sizeof(struct tsi721_imsg_desc),
		priv->imsg_ring[mbox].imd_base,
		priv->imsg_ring[mbox].imd_phys);

	priv->imsg_ring[mbox].imd_base = NULL;
#endif /* CONFIG_PCI_MSI */

out_dma:
	dma_free_coherent(&priv->pdev->dev,
		priv->imsg_ring[mbox].size * 8,
		priv->imsg_ring[mbox].imfq_base,
		priv->imsg_ring[mbox].imfq_phys);

	priv->imsg_ring[mbox].imfq_base = NULL;

out_buf:
	dma_free_coherent(&priv->pdev->dev,
		priv->imsg_ring[mbox].size * TSI721_MSG_BUFFER_SIZE,
		priv->imsg_ring[mbox].buf_base,
		priv->imsg_ring[mbox].buf_phys);

	priv->imsg_ring[mbox].buf_base = NULL;

out:
	return rc;
}

/**
 * tsi721_close_inb_mbox - Shut down Tsi721 inbound mailbox
 * @mport: Master port implementing the Inbound Messaging Engine
 * @mbox: Mailbox to close
 */
static void tsi721_close_inb_mbox(struct rio_mport *mport, int mbox)
{
	struct tsi721_device *priv = mport->priv;
	u32 rx_slot;
	int ch = mbox + 4;

	if (!priv->imsg_init[mbox]) /* mbox isn't initialized yet */
		return;
	priv->imsg_init[mbox] = 0;

	/* Disable Inbound Messaging Engine */

	/* Disable Interrupts */
	tsi721_imsg_interrupt_disable(priv, ch, TSI721_OBDMAC_INT_MASK);

#ifdef CONFIG_PCI_MSI
	if (priv->flags & TSI721_USING_MSIX) {
		free_irq(priv->msix[TSI721_VECT_IMB0_RCV + mbox].vector,
				(void *)mport);
		free_irq(priv->msix[TSI721_VECT_IMB0_INT + mbox].vector,
				(void *)mport);
	}
#endif /* CONFIG_PCI_MSI */

	/* Clear Inbound Buffer Queue */
	for (rx_slot = 0; rx_slot < priv->imsg_ring[mbox].size; rx_slot++)
		priv->imsg_ring[mbox].imq_base[rx_slot] = NULL;

	/* Free memory allocated for message buffers */
	dma_free_coherent(&priv->pdev->dev,
		priv->imsg_ring[mbox].size * TSI721_MSG_BUFFER_SIZE,
		priv->imsg_ring[mbox].buf_base,
		priv->imsg_ring[mbox].buf_phys);

	priv->imsg_ring[mbox].buf_base = NULL;

	/* Free memory allocated for free pointr list */
	dma_free_coherent(&priv->pdev->dev,
		priv->imsg_ring[mbox].size * 8,
		priv->imsg_ring[mbox].imfq_base,
		priv->imsg_ring[mbox].imfq_phys);

	priv->imsg_ring[mbox].imfq_base = NULL;

	/* Free memory allocated for RX descriptors */
	dma_free_coherent(&priv->pdev->dev,
		priv->imsg_ring[mbox].size * sizeof(struct tsi721_imsg_desc),
		priv->imsg_ring[mbox].imd_base,
		priv->imsg_ring[mbox].imd_phys);

	priv->imsg_ring[mbox].imd_base = NULL;
}

/**
 * tsi721_add_inb_buffer - Add buffer to the Tsi721 inbound message queue
 * @mport: Master port implementing the Inbound Messaging Engine
 * @mbox: Inbound mailbox number
 * @buf: Buffer to add to inbound queue
 */
static int tsi721_add_inb_buffer(struct rio_mport *mport, int mbox, void *buf)
{
	struct tsi721_device *priv = mport->priv;
	u32 rx_slot;
	int rc = 0;

	rx_slot = priv->imsg_ring[mbox].rx_slot;
	if (priv->imsg_ring[mbox].imq_base[rx_slot]) {
		dev_err(&priv->pdev->dev,
			"Error adding inbound buffer %d, buffer exists\n",
			rx_slot);
		rc = -EINVAL;
		goto out;
	}

	priv->imsg_ring[mbox].imq_base[rx_slot] = buf;

	if (++priv->imsg_ring[mbox].rx_slot == priv->imsg_ring[mbox].size)
		priv->imsg_ring[mbox].rx_slot = 0;

out:
	return rc;
}

/**
 * tsi721_get_inb_message - Fetch inbound message from the Tsi721 MSG Queue
 * @mport: Master port implementing the Inbound Messaging Engine
 * @mbox: Inbound mailbox number
 *
 * Returns pointer to the message on success or NULL on failure.
 */
static void *tsi721_get_inb_message(struct rio_mport *mport, int mbox)
{
	struct tsi721_device *priv = mport->priv;
	struct tsi721_imsg_desc *desc;
	u32 rx_slot;
	void *rx_virt = NULL;
	u64 rx_phys;
	void *buf = NULL;
	u64 *free_ptr;
	int ch = mbox + 4;
	int msg_size;

	if (!priv->imsg_init[mbox])
		return NULL;

	desc = priv->imsg_ring[mbox].imd_base;
	desc += priv->imsg_ring[mbox].desc_rdptr;

	if (!(le32_to_cpu(desc->msg_info) & TSI721_IMD_HO))
		goto out;

	rx_slot = priv->imsg_ring[mbox].rx_slot;
	while (priv->imsg_ring[mbox].imq_base[rx_slot] == NULL) {
		if (++rx_slot == priv->imsg_ring[mbox].size)
			rx_slot = 0;
	}

	rx_phys = ((u64)le32_to_cpu(desc->bufptr_hi) << 32) |
			le32_to_cpu(desc->bufptr_lo);

	rx_virt = priv->imsg_ring[mbox].buf_base +
		  (rx_phys - (u64)priv->imsg_ring[mbox].buf_phys);

	buf = priv->imsg_ring[mbox].imq_base[rx_slot];
	msg_size = le32_to_cpu(desc->msg_info) & TSI721_IMD_BCOUNT;
	if (msg_size == 0)
		msg_size = RIO_MAX_MSG_SIZE;

	memcpy(buf, rx_virt, msg_size);
	priv->imsg_ring[mbox].imq_base[rx_slot] = NULL;

	desc->msg_info &= cpu_to_le32(~TSI721_IMD_HO);
	if (++priv->imsg_ring[mbox].desc_rdptr == priv->imsg_ring[mbox].size)
		priv->imsg_ring[mbox].desc_rdptr = 0;

	iowrite32(priv->imsg_ring[mbox].desc_rdptr,
		priv->regs + TSI721_IBDMAC_DQRP(ch));

	/* Return free buffer into the pointer list */
	free_ptr = priv->imsg_ring[mbox].imfq_base;
	free_ptr[priv->imsg_ring[mbox].fq_wrptr] = cpu_to_le64(rx_phys);

	if (++priv->imsg_ring[mbox].fq_wrptr == priv->imsg_ring[mbox].size)
		priv->imsg_ring[mbox].fq_wrptr = 0;

	iowrite32(priv->imsg_ring[mbox].fq_wrptr,
		priv->regs + TSI721_IBDMAC_FQWP(ch));
out:
	return buf;
}

/**
 * tsi721_messages_init - Initialization of Messaging Engine
 * @priv: pointer to tsi721 private data
 *
 * Configures Tsi721 messaging engine.
 */
static int tsi721_messages_init(struct tsi721_device *priv)
{
	int	ch;

	iowrite32(0, priv->regs + TSI721_SMSG_ECC_LOG);
	iowrite32(0, priv->regs + TSI721_RETRY_GEN_CNT);
	iowrite32(0, priv->regs + TSI721_RETRY_RX_CNT);

	/* Set SRIO Message Request/Response Timeout */
	iowrite32(TSI721_RQRPTO_VAL, priv->regs + TSI721_RQRPTO);

	/* Initialize Inbound Messaging Engine Registers */
	for (ch = 0; ch < TSI721_IMSG_CHNUM; ch++) {
		/* Clear interrupt bits */
		iowrite32(TSI721_IBDMAC_INT_MASK,
			priv->regs + TSI721_IBDMAC_INT(ch));
		/* Clear Status */
		iowrite32(0, priv->regs + TSI721_IBDMAC_STS(ch));

		iowrite32(TSI721_SMSG_ECC_COR_LOG_MASK,
				priv->regs + TSI721_SMSG_ECC_COR_LOG(ch));
		iowrite32(TSI721_SMSG_ECC_NCOR_MASK,
				priv->regs + TSI721_SMSG_ECC_NCOR(ch));
	}

	return 0;
}

/**
 * tsi721_disable_ints - disables all device interrupts
 * @priv: pointer to tsi721 private data
 */
static void tsi721_disable_ints(struct tsi721_device *priv)
{
	int ch;

	/* Disable all device level interrupts */
	iowrite32(0, priv->regs + TSI721_DEV_INTE);

	/* Disable all Device Channel interrupts */
	iowrite32(0, priv->regs + TSI721_DEV_CHAN_INTE);

	/* Disable all Inbound Msg Channel interrupts */
	for (ch = 0; ch < TSI721_IMSG_CHNUM; ch++)
		iowrite32(0, priv->regs + TSI721_IBDMAC_INTE(ch));

	/* Disable all Outbound Msg Channel interrupts */
	for (ch = 0; ch < TSI721_OMSG_CHNUM; ch++)
		iowrite32(0, priv->regs + TSI721_OBDMAC_INTE(ch));

	/* Disable all general messaging interrupts */
	iowrite32(0, priv->regs + TSI721_SMSG_INTE);

	/* Disable all BDMA Channel interrupts */
	for (ch = 0; ch < TSI721_DMA_MAXCH; ch++)
		iowrite32(0, priv->regs + TSI721_DMAC_INTE(ch));

	/* Disable all general BDMA interrupts */
	iowrite32(0, priv->regs + TSI721_BDMA_INTE);

	/* Disable all SRIO Channel interrupts */
	for (ch = 0; ch < TSI721_SRIO_MAXCH; ch++)
		iowrite32(0, priv->regs + TSI721_SR_CHINTE(ch));

	/* Disable all general SR2PC interrupts */
	iowrite32(0, priv->regs + TSI721_SR2PC_GEN_INTE);

	/* Disable all PC2SR interrupts */
	iowrite32(0, priv->regs + TSI721_PC2SR_INTE);

	/* Disable all I2C interrupts */
	iowrite32(0, priv->regs + TSI721_I2C_INT_ENABLE);

	/* Disable SRIO MAC interrupts */
	iowrite32(0, priv->regs + TSI721_RIO_EM_INT_ENABLE);
	iowrite32(0, priv->regs + TSI721_RIO_EM_DEV_INT_EN);
}

/**
 * tsi721_setup_mport - Setup Tsi721 as RapidIO subsystem master port
 * @priv: pointer to tsi721 private data
 *
 * Configures Tsi721 as RapidIO master port.
 */
static int __devinit tsi721_setup_mport(struct tsi721_device *priv)
{
	struct pci_dev *pdev = priv->pdev;
	int err = 0;
	struct rio_ops *ops;

	struct rio_mport *mport;

	ops = kzalloc(sizeof(struct rio_ops), GFP_KERNEL);
	if (!ops) {
		dev_dbg(&pdev->dev, "Unable to allocate memory for rio_ops\n");
		return -ENOMEM;
	}

	ops->lcread = tsi721_lcread;
	ops->lcwrite = tsi721_lcwrite;
	ops->cread = tsi721_cread_dma;
	ops->cwrite = tsi721_cwrite_dma;
	ops->dsend = tsi721_dsend;
	ops->open_inb_mbox = tsi721_open_inb_mbox;
	ops->close_inb_mbox = tsi721_close_inb_mbox;
	ops->open_outb_mbox = tsi721_open_outb_mbox;
	ops->close_outb_mbox = tsi721_close_outb_mbox;
	ops->add_outb_message = tsi721_add_outb_message;
	ops->add_inb_buffer = tsi721_add_inb_buffer;
	ops->get_inb_message = tsi721_get_inb_message;

	mport = kzalloc(sizeof(struct rio_mport), GFP_KERNEL);
	if (!mport) {
		kfree(ops);
		dev_dbg(&pdev->dev, "Unable to allocate memory for mport\n");
		return -ENOMEM;
	}

	mport->ops = ops;
	mport->index = 0;
	mport->sys_size = 0; /* small system */
	mport->phy_type = RIO_PHY_SERIAL;
	mport->priv = (void *)priv;
	mport->phys_efptr = 0x100;

	INIT_LIST_HEAD(&mport->dbells);

	rio_init_dbell_res(&mport->riores[RIO_DOORBELL_RESOURCE], 0, 0xffff);
	rio_init_mbox_res(&mport->riores[RIO_INB_MBOX_RESOURCE], 0, 3);
	rio_init_mbox_res(&mport->riores[RIO_OUTB_MBOX_RESOURCE], 0, 3);
	strcpy(mport->name, "Tsi721 mport");

	/* Hook up interrupt handler */

#ifdef CONFIG_PCI_MSI
	if (!tsi721_enable_msix(priv))
		priv->flags |= TSI721_USING_MSIX;
	else if (!pci_enable_msi(pdev))
		priv->flags |= TSI721_USING_MSI;
	else
		dev_info(&pdev->dev,
			 "MSI/MSI-X is not available. Using legacy INTx.\n");
#endif /* CONFIG_PCI_MSI */

	err = tsi721_request_irq(mport);

	if (!err) {
		tsi721_interrupts_init(priv);
		ops->pwenable = tsi721_pw_enable;
	} else
		dev_err(&pdev->dev, "Unable to get assigned PCI IRQ "
			"vector %02X err=0x%x\n", pdev->irq, err);

	/* Enable SRIO link */
	iowrite32(ioread32(priv->regs + TSI721_DEVCTL) |
		  TSI721_DEVCTL_SRBOOT_CMPL,
		  priv->regs + TSI721_DEVCTL);

	rio_register_mport(mport);
	priv->mport = mport;

	if (mport->host_deviceid >= 0)
		iowrite32(RIO_PORT_GEN_HOST | RIO_PORT_GEN_MASTER |
			  RIO_PORT_GEN_DISCOVERED,
			  priv->regs + (0x100 + RIO_PORT_GEN_CTL_CSR));
	else
		iowrite32(0, priv->regs + (0x100 + RIO_PORT_GEN_CTL_CSR));

	return 0;
}

static int __devinit tsi721_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	struct tsi721_device *priv;
	int i, cap;
	int err;
	u32 regval;

	priv = kzalloc(sizeof(struct tsi721_device), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "Failed to allocate memory for device\n");
		err = -ENOMEM;
		goto err_exit;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		goto err_clean;
	}

	priv->pdev = pdev;

#ifdef DEBUG
	for (i = 0; i <= PCI_STD_RESOURCE_END; i++) {
		dev_dbg(&pdev->dev, "res[%d] @ 0x%llx (0x%lx, 0x%lx)\n",
			i, (unsigned long long)pci_resource_start(pdev, i),
			(unsigned long)pci_resource_len(pdev, i),
			pci_resource_flags(pdev, i));
	}
#endif
	/*
	 * Verify BAR configuration
	 */

	/* BAR_0 (registers) must be 512KB+ in 32-bit address space */
	if (!(pci_resource_flags(pdev, BAR_0) & IORESOURCE_MEM) ||
	    pci_resource_flags(pdev, BAR_0) & IORESOURCE_MEM_64 ||
	    pci_resource_len(pdev, BAR_0) < TSI721_REG_SPACE_SIZE) {
		dev_err(&pdev->dev,
			"Missing or misconfigured CSR BAR0, aborting.\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

	/* BAR_1 (outbound doorbells) must be 16MB+ in 32-bit address space */
	if (!(pci_resource_flags(pdev, BAR_1) & IORESOURCE_MEM) ||
	    pci_resource_flags(pdev, BAR_1) & IORESOURCE_MEM_64 ||
	    pci_resource_len(pdev, BAR_1) < TSI721_DB_WIN_SIZE) {
		dev_err(&pdev->dev,
			"Missing or misconfigured Doorbell BAR1, aborting.\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

	/*
	 * BAR_2 and BAR_4 (outbound translation) must be in 64-bit PCIe address
	 * space.
	 * NOTE: BAR_2 and BAR_4 are not used by this version of driver.
	 * It may be a good idea to keep them disabled using HW configuration
	 * to save PCI memory space.
	 */
	if ((pci_resource_flags(pdev, BAR_2) & IORESOURCE_MEM) &&
	    (pci_resource_flags(pdev, BAR_2) & IORESOURCE_MEM_64)) {
		dev_info(&pdev->dev, "Outbound BAR2 is not used but enabled.\n");
	}

	if ((pci_resource_flags(pdev, BAR_4) & IORESOURCE_MEM) &&
	    (pci_resource_flags(pdev, BAR_4) & IORESOURCE_MEM_64)) {
		dev_info(&pdev->dev, "Outbound BAR4 is not used but enabled.\n");
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, "
			"aborting.\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	priv->regs = pci_ioremap_bar(pdev, BAR_0);
	if (!priv->regs) {
		dev_err(&pdev->dev,
			"Unable to map device registers space, aborting\n");
		err = -ENOMEM;
		goto err_free_res;
	}

	priv->odb_base = pci_ioremap_bar(pdev, BAR_1);
	if (!priv->odb_base) {
		dev_err(&pdev->dev,
			"Unable to map outbound doorbells space, aborting\n");
		err = -ENOMEM;
		goto err_unmap_bars;
	}

	/* Configure DMA attributes. */
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
			dev_info(&pdev->dev, "Unable to set DMA mask\n");
			goto err_unmap_bars;
		}

		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))
			dev_info(&pdev->dev, "Unable to set consistent DMA mask\n");
	} else {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err)
			dev_info(&pdev->dev, "Unable to set consistent DMA mask\n");
	}

	cap = pci_pcie_cap(pdev);
	BUG_ON(cap == 0);

	/* Clear "no snoop" and "relaxed ordering" bits, use default MRRS. */
	pci_read_config_dword(pdev, cap + PCI_EXP_DEVCTL, &regval);
	regval &= ~(PCI_EXP_DEVCTL_READRQ | PCI_EXP_DEVCTL_RELAX_EN |
		    PCI_EXP_DEVCTL_NOSNOOP_EN);
	regval |= 0x2 << MAX_READ_REQUEST_SZ_SHIFT;
	pci_write_config_dword(pdev, cap + PCI_EXP_DEVCTL, regval);

	/* Adjust PCIe completion timeout. */
	pci_read_config_dword(pdev, cap + PCI_EXP_DEVCTL2, &regval);
	regval &= ~(0x0f);
	pci_write_config_dword(pdev, cap + PCI_EXP_DEVCTL2, regval | 0x2);

	/*
	 * FIXUP: correct offsets of MSI-X tables in the MSI-X Capability Block
	 */
	pci_write_config_dword(pdev, TSI721_PCIECFG_EPCTL, 0x01);
	pci_write_config_dword(pdev, TSI721_PCIECFG_MSIXTBL,
						TSI721_MSIXTBL_OFFSET);
	pci_write_config_dword(pdev, TSI721_PCIECFG_MSIXPBA,
						TSI721_MSIXPBA_OFFSET);
	pci_write_config_dword(pdev, TSI721_PCIECFG_EPCTL, 0);
	/* End of FIXUP */

	tsi721_disable_ints(priv);

	tsi721_init_pc2sr_mapping(priv);
	tsi721_init_sr2pc_mapping(priv);

	if (tsi721_bdma_init(priv)) {
		dev_err(&pdev->dev, "BDMA initialization failed, aborting\n");
		err = -ENOMEM;
		goto err_unmap_bars;
	}

	err = tsi721_doorbell_init(priv);
	if (err)
		goto err_free_bdma;

	tsi721_port_write_init(priv);

	err = tsi721_messages_init(priv);
	if (err)
		goto err_free_consistent;

	err = tsi721_setup_mport(priv);
	if (err)
		goto err_free_consistent;

	return 0;

err_free_consistent:
	tsi721_doorbell_free(priv);
err_free_bdma:
	tsi721_bdma_free(priv);
err_unmap_bars:
	if (priv->regs)
		iounmap(priv->regs);
	if (priv->odb_base)
		iounmap(priv->odb_base);
err_free_res:
	pci_release_regions(pdev);
	pci_clear_master(pdev);
err_disable_pdev:
	pci_disable_device(pdev);
err_clean:
	kfree(priv);
err_exit:
	return err;
}

static DEFINE_PCI_DEVICE_TABLE(tsi721_pci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IDT, PCI_DEVICE_ID_TSI721) },
	{ 0, }	/* terminate list */
};

MODULE_DEVICE_TABLE(pci, tsi721_pci_tbl);

static struct pci_driver tsi721_driver = {
	.name		= "tsi721",
	.id_table	= tsi721_pci_tbl,
	.probe		= tsi721_probe,
};

static int __init tsi721_init(void)
{
	return pci_register_driver(&tsi721_driver);
}

static void __exit tsi721_exit(void)
{
	pci_unregister_driver(&tsi721_driver);
}

device_initcall(tsi721_init);
