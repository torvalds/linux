// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000-2010 Adaptec, Inc.
 *               2010-2015 PMC-Sierra, Inc. (aacraid@pmc-sierra.com)
 *		 2016-2017 Microsemi Corp. (aacraid@microsemi.com)
 *
 * Module Name:
 *  src.c
 *
 * Abstract: Hardware Device Interface for PMC SRC based controllers
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <scsi/scsi_host.h>

#include "aacraid.h"

static int aac_src_get_sync_status(struct aac_dev *dev);

static irqreturn_t aac_src_intr_message(int irq, void *dev_id)
{
	struct aac_msix_ctx *ctx;
	struct aac_dev *dev;
	unsigned long bellbits, bellbits_shifted;
	int vector_no;
	int isFastResponse, mode;
	u32 index, handle;

	ctx = (struct aac_msix_ctx *)dev_id;
	dev = ctx->dev;
	vector_no = ctx->vector_no;

	if (dev->msi_enabled) {
		mode = AAC_INT_MODE_MSI;
		if (vector_no == 0) {
			bellbits = src_readl(dev, MUnit.ODR_MSI);
			if (bellbits & 0x40000)
				mode |= AAC_INT_MODE_AIF;
			if (bellbits & 0x1000)
				mode |= AAC_INT_MODE_SYNC;
		}
	} else {
		mode = AAC_INT_MODE_INTX;
		bellbits = src_readl(dev, MUnit.ODR_R);
		if (bellbits & PmDoorBellResponseSent) {
			bellbits = PmDoorBellResponseSent;
			src_writel(dev, MUnit.ODR_C, bellbits);
			src_readl(dev, MUnit.ODR_C);
		} else {
			bellbits_shifted = (bellbits >> SRC_ODR_SHIFT);
			src_writel(dev, MUnit.ODR_C, bellbits);
			src_readl(dev, MUnit.ODR_C);

			if (bellbits_shifted & DoorBellAifPending)
				mode |= AAC_INT_MODE_AIF;
			else if (bellbits_shifted & OUTBOUNDDOORBELL_0)
				mode |= AAC_INT_MODE_SYNC;
		}
	}

	if (mode & AAC_INT_MODE_SYNC) {
		unsigned long sflags;
		struct list_head *entry;
		int send_it = 0;
		extern int aac_sync_mode;

		if (!aac_sync_mode && !dev->msi_enabled) {
			src_writel(dev, MUnit.ODR_C, bellbits);
			src_readl(dev, MUnit.ODR_C);
		}

		if (dev->sync_fib) {
			if (dev->sync_fib->callback)
				dev->sync_fib->callback(dev->sync_fib->callback_data,
					dev->sync_fib);
			spin_lock_irqsave(&dev->sync_fib->event_lock, sflags);
			if (dev->sync_fib->flags & FIB_CONTEXT_FLAG_WAIT) {
				dev->management_fib_count--;
				complete(&dev->sync_fib->event_wait);
			}
			spin_unlock_irqrestore(&dev->sync_fib->event_lock,
						sflags);
			spin_lock_irqsave(&dev->sync_lock, sflags);
			if (!list_empty(&dev->sync_fib_list)) {
				entry = dev->sync_fib_list.next;
				dev->sync_fib = list_entry(entry,
							   struct fib,
							   fiblink);
				list_del(entry);
				send_it = 1;
			} else {
				dev->sync_fib = NULL;
			}
			spin_unlock_irqrestore(&dev->sync_lock, sflags);
			if (send_it) {
				aac_adapter_sync_cmd(dev, SEND_SYNCHRONOUS_FIB,
					(u32)dev->sync_fib->hw_fib_pa,
					0, 0, 0, 0, 0,
					NULL, NULL, NULL, NULL, NULL);
			}
		}
		if (!dev->msi_enabled)
			mode = 0;

	}

	if (mode & AAC_INT_MODE_AIF) {
		/* handle AIF */
		if (dev->sa_firmware) {
			u32 events = src_readl(dev, MUnit.SCR0);

			aac_intr_normal(dev, events, 1, 0, NULL);
			writel(events, &dev->IndexRegs->Mailbox[0]);
			src_writel(dev, MUnit.IDR, 1 << 23);
		} else {
			if (dev->aif_thread && dev->fsa_dev)
				aac_intr_normal(dev, 0, 2, 0, NULL);
		}
		if (dev->msi_enabled)
			aac_src_access_devreg(dev, AAC_CLEAR_AIF_BIT);
		mode = 0;
	}

	if (mode) {
		index = dev->host_rrq_idx[vector_no];

		for (;;) {
			isFastResponse = 0;
			/* remove toggle bit (31) */
			handle = le32_to_cpu((dev->host_rrq[index])
				& 0x7fffffff);
			/* check fast response bits (30, 1) */
			if (handle & 0x40000000)
				isFastResponse = 1;
			handle &= 0x0000ffff;
			if (handle == 0)
				break;
			handle >>= 2;
			if (dev->msi_enabled && dev->max_msix > 1)
				atomic_dec(&dev->rrq_outstanding[vector_no]);
			aac_intr_normal(dev, handle, 0, isFastResponse, NULL);
			dev->host_rrq[index++] = 0;
			if (index == (vector_no + 1) * dev->vector_cap)
				index = vector_no * dev->vector_cap;
			dev->host_rrq_idx[vector_no] = index;
		}
		mode = 0;
	}

	return IRQ_HANDLED;
}

/**
 *	aac_src_disable_interrupt	-	Disable interrupts
 *	@dev: Adapter
 */

static void aac_src_disable_interrupt(struct aac_dev *dev)
{
	src_writel(dev, MUnit.OIMR, dev->OIMR = 0xffffffff);
}

/**
 *	aac_src_enable_interrupt_message	-	Enable interrupts
 *	@dev: Adapter
 */

static void aac_src_enable_interrupt_message(struct aac_dev *dev)
{
	aac_src_access_devreg(dev, AAC_ENABLE_INTERRUPT);
}

/**
 *	src_sync_cmd	-	send a command and wait
 *	@dev: Adapter
 *	@command: Command to execute
 *	@p1: first parameter
 *	@p2: second parameter
 *	@p3: third parameter
 *	@p4: forth parameter
 *	@p5: fifth parameter
 *	@p6: sixth parameter
 *	@status: adapter status
 *	@r1: first return value
 *	@r2: second return valu
 *	@r3: third return value
 *	@r4: forth return value
 *
 *	This routine will send a synchronous command to the adapter and wait
 *	for its	completion.
 */

static int src_sync_cmd(struct aac_dev *dev, u32 command,
	u32 p1, u32 p2, u32 p3, u32 p4, u32 p5, u32 p6,
	u32 *status, u32 * r1, u32 * r2, u32 * r3, u32 * r4)
{
	unsigned long start;
	unsigned long delay;
	int ok;

	/*
	 *	Write the command into Mailbox 0
	 */
	writel(command, &dev->IndexRegs->Mailbox[0]);
	/*
	 *	Write the parameters into Mailboxes 1 - 6
	 */
	writel(p1, &dev->IndexRegs->Mailbox[1]);
	writel(p2, &dev->IndexRegs->Mailbox[2]);
	writel(p3, &dev->IndexRegs->Mailbox[3]);
	writel(p4, &dev->IndexRegs->Mailbox[4]);

	/*
	 *	Clear the synch command doorbell to start on a clean slate.
	 */
	if (!dev->msi_enabled)
		src_writel(dev,
			   MUnit.ODR_C,
			   OUTBOUNDDOORBELL_0 << SRC_ODR_SHIFT);

	/*
	 *	Disable doorbell interrupts
	 */
	src_writel(dev, MUnit.OIMR, dev->OIMR = 0xffffffff);

	/*
	 *	Force the completion of the mask register write before issuing
	 *	the interrupt.
	 */
	src_readl(dev, MUnit.OIMR);

	/*
	 *	Signal that there is a new synch command
	 */
	src_writel(dev, MUnit.IDR, INBOUNDDOORBELL_0 << SRC_IDR_SHIFT);

	if ((!dev->sync_mode || command != SEND_SYNCHRONOUS_FIB) &&
		!dev->in_soft_reset) {
		ok = 0;
		start = jiffies;

		if (command == IOP_RESET_ALWAYS) {
			/* Wait up to 10 sec */
			delay = 10*HZ;
		} else {
			/* Wait up to 5 minutes */
			delay = 300*HZ;
		}
		while (time_before(jiffies, start+delay)) {
			udelay(5);	/* Delay 5 microseconds to let Mon960 get info. */
			/*
			 *	Mon960 will set doorbell0 bit when it has completed the command.
			 */
			if (aac_src_get_sync_status(dev) & OUTBOUNDDOORBELL_0) {
				/*
				 *	Clear the doorbell.
				 */
				if (dev->msi_enabled)
					aac_src_access_devreg(dev,
						AAC_CLEAR_SYNC_BIT);
				else
					src_writel(dev,
						MUnit.ODR_C,
						OUTBOUNDDOORBELL_0 << SRC_ODR_SHIFT);
				ok = 1;
				break;
			}
			/*
			 *	Yield the processor in case we are slow
			 */
			msleep(1);
		}
		if (unlikely(ok != 1)) {
			/*
			 *	Restore interrupt mask even though we timed out
			 */
			aac_adapter_enable_int(dev);
			return -ETIMEDOUT;
		}
		/*
		 *	Pull the synch status from Mailbox 0.
		 */
		if (status)
			*status = readl(&dev->IndexRegs->Mailbox[0]);
		if (r1)
			*r1 = readl(&dev->IndexRegs->Mailbox[1]);
		if (r2)
			*r2 = readl(&dev->IndexRegs->Mailbox[2]);
		if (r3)
			*r3 = readl(&dev->IndexRegs->Mailbox[3]);
		if (r4)
			*r4 = readl(&dev->IndexRegs->Mailbox[4]);
		if (command == GET_COMM_PREFERRED_SETTINGS)
			dev->max_msix =
				readl(&dev->IndexRegs->Mailbox[5]) & 0xFFFF;
		/*
		 *	Clear the synch command doorbell.
		 */
		if (!dev->msi_enabled)
			src_writel(dev,
				MUnit.ODR_C,
				OUTBOUNDDOORBELL_0 << SRC_ODR_SHIFT);
	}

	/*
	 *	Restore interrupt mask
	 */
	aac_adapter_enable_int(dev);
	return 0;
}

/**
 *	aac_src_interrupt_adapter	-	interrupt adapter
 *	@dev: Adapter
 *
 *	Send an interrupt to the i960 and breakpoint it.
 */

static void aac_src_interrupt_adapter(struct aac_dev *dev)
{
	src_sync_cmd(dev, BREAKPOINT_REQUEST,
		0, 0, 0, 0, 0, 0,
		NULL, NULL, NULL, NULL, NULL);
}

/**
 *	aac_src_notify_adapter		-	send an event to the adapter
 *	@dev: Adapter
 *	@event: Event to send
 *
 *	Notify the i960 that something it probably cares about has
 *	happened.
 */

static void aac_src_notify_adapter(struct aac_dev *dev, u32 event)
{
	switch (event) {

	case AdapNormCmdQue:
		src_writel(dev, MUnit.ODR_C,
			INBOUNDDOORBELL_1 << SRC_ODR_SHIFT);
		break;
	case HostNormRespNotFull:
		src_writel(dev, MUnit.ODR_C,
			INBOUNDDOORBELL_4 << SRC_ODR_SHIFT);
		break;
	case AdapNormRespQue:
		src_writel(dev, MUnit.ODR_C,
			INBOUNDDOORBELL_2 << SRC_ODR_SHIFT);
		break;
	case HostNormCmdNotFull:
		src_writel(dev, MUnit.ODR_C,
			INBOUNDDOORBELL_3 << SRC_ODR_SHIFT);
		break;
	case FastIo:
		src_writel(dev, MUnit.ODR_C,
			INBOUNDDOORBELL_6 << SRC_ODR_SHIFT);
		break;
	case AdapPrintfDone:
		src_writel(dev, MUnit.ODR_C,
			INBOUNDDOORBELL_5 << SRC_ODR_SHIFT);
		break;
	default:
		BUG();
		break;
	}
}

/**
 *	aac_src_start_adapter		-	activate adapter
 *	@dev:	Adapter
 *
 *	Start up processing on an i960 based AAC adapter
 */

static void aac_src_start_adapter(struct aac_dev *dev)
{
	union aac_init *init;
	int i;

	 /* reset host_rrq_idx first */
	for (i = 0; i < dev->max_msix; i++) {
		dev->host_rrq_idx[i] = i * dev->vector_cap;
		atomic_set(&dev->rrq_outstanding[i], 0);
	}
	atomic_set(&dev->msix_counter, 0);
	dev->fibs_pushed_no = 0;

	init = dev->init;
	if (dev->comm_interface == AAC_COMM_MESSAGE_TYPE3) {
		init->r8.host_elapsed_seconds =
			cpu_to_le32(ktime_get_real_seconds());
		src_sync_cmd(dev, INIT_STRUCT_BASE_ADDRESS,
			lower_32_bits(dev->init_pa),
			upper_32_bits(dev->init_pa),
			sizeof(struct _r8) +
			(AAC_MAX_HRRQ - 1) * sizeof(struct _rrq),
			0, 0, 0, NULL, NULL, NULL, NULL, NULL);
	} else {
		init->r7.host_elapsed_seconds =
			cpu_to_le32(ktime_get_real_seconds());
		// We can only use a 32 bit address here
		src_sync_cmd(dev, INIT_STRUCT_BASE_ADDRESS,
			(u32)(ulong)dev->init_pa, 0, 0, 0, 0, 0,
			NULL, NULL, NULL, NULL, NULL);
	}

}

/**
 *	aac_src_check_health
 *	@dev: device to check if healthy
 *
 *	Will attempt to determine if the specified adapter is alive and
 *	capable of handling requests, returning 0 if alive.
 */
static int aac_src_check_health(struct aac_dev *dev)
{
	u32 status = src_readl(dev, MUnit.OMR);

	/*
	 *	Check to see if the board panic'd.
	 */
	if (unlikely(status & KERNEL_PANIC))
		goto err_blink;

	/*
	 *	Check to see if the board failed any self tests.
	 */
	if (unlikely(status & SELF_TEST_FAILED))
		goto err_out;

	/*
	 *	Check to see if the board failed any self tests.
	 */
	if (unlikely(status & MONITOR_PANIC))
		goto err_out;

	/*
	 *	Wait for the adapter to be up and running.
	 */
	if (unlikely(!(status & KERNEL_UP_AND_RUNNING)))
		return -3;
	/*
	 *	Everything is OK
	 */
	return 0;

err_out:
	return -1;

err_blink:
	return (status >> 16) & 0xFF;
}

static inline u32 aac_get_vector(struct aac_dev *dev)
{
	return atomic_inc_return(&dev->msix_counter)%dev->max_msix;
}

/**
 *	aac_src_deliver_message
 *	@fib: fib to issue
 *
 *	Will send a fib, returning 0 if successful.
 */
static int aac_src_deliver_message(struct fib *fib)
{
	struct aac_dev *dev = fib->dev;
	struct aac_queue *q = &dev->queues->queue[AdapNormCmdQueue];
	u32 fibsize;
	dma_addr_t address;
	struct aac_fib_xporthdr *pFibX;
	int native_hba;
#if !defined(writeq)
	unsigned long flags;
#endif

	u16 vector_no;

	atomic_inc(&q->numpending);

	native_hba = (fib->flags & FIB_CONTEXT_FLAG_NATIVE_HBA) ? 1 : 0;


	if (dev->msi_enabled && dev->max_msix > 1 &&
		(native_hba || fib->hw_fib_va->header.Command != AifRequest)) {

		if ((dev->comm_interface == AAC_COMM_MESSAGE_TYPE3)
			&& dev->sa_firmware)
			vector_no = aac_get_vector(dev);
		else
			vector_no = fib->vector_no;

		if (native_hba) {
			if (fib->flags & FIB_CONTEXT_FLAG_NATIVE_HBA_TMF) {
				struct aac_hba_tm_req *tm_req;

				tm_req = (struct aac_hba_tm_req *)
						fib->hw_fib_va;
				if (tm_req->iu_type ==
					HBA_IU_TYPE_SCSI_TM_REQ) {
					((struct aac_hba_tm_req *)
						fib->hw_fib_va)->reply_qid
							= vector_no;
					((struct aac_hba_tm_req *)
						fib->hw_fib_va)->request_id
							+= (vector_no << 16);
				} else {
					((struct aac_hba_reset_req *)
						fib->hw_fib_va)->reply_qid
							= vector_no;
					((struct aac_hba_reset_req *)
						fib->hw_fib_va)->request_id
							+= (vector_no << 16);
				}
			} else {
				((struct aac_hba_cmd_req *)
					fib->hw_fib_va)->reply_qid
						= vector_no;
				((struct aac_hba_cmd_req *)
					fib->hw_fib_va)->request_id
						+= (vector_no << 16);
			}
		} else {
			fib->hw_fib_va->header.Handle += (vector_no << 16);
		}
	} else {
		vector_no = 0;
	}

	atomic_inc(&dev->rrq_outstanding[vector_no]);

	if (native_hba) {
		address = fib->hw_fib_pa;
		fibsize = (fib->hbacmd_size + 127) / 128 - 1;
		if (fibsize > 31)
			fibsize = 31;
		address |= fibsize;
#if defined(writeq)
		src_writeq(dev, MUnit.IQN_L, (u64)address);
#else
		spin_lock_irqsave(&fib->dev->iq_lock, flags);
		src_writel(dev, MUnit.IQN_H,
			upper_32_bits(address) & 0xffffffff);
		src_writel(dev, MUnit.IQN_L, address & 0xffffffff);
		spin_unlock_irqrestore(&fib->dev->iq_lock, flags);
#endif
	} else {
		if (dev->comm_interface == AAC_COMM_MESSAGE_TYPE2 ||
			dev->comm_interface == AAC_COMM_MESSAGE_TYPE3) {
			/* Calculate the amount to the fibsize bits */
			fibsize = (le16_to_cpu(fib->hw_fib_va->header.Size)
				+ 127) / 128 - 1;
			/* New FIB header, 32-bit */
			address = fib->hw_fib_pa;
			fib->hw_fib_va->header.StructType = FIB_MAGIC2;
			fib->hw_fib_va->header.SenderFibAddress =
				cpu_to_le32((u32)address);
			fib->hw_fib_va->header.u.TimeStamp = 0;
			WARN_ON(upper_32_bits(address) != 0L);
		} else {
			/* Calculate the amount to the fibsize bits */
			fibsize = (sizeof(struct aac_fib_xporthdr) +
				le16_to_cpu(fib->hw_fib_va->header.Size)
				+ 127) / 128 - 1;
			/* Fill XPORT header */
			pFibX = (struct aac_fib_xporthdr *)
				((unsigned char *)fib->hw_fib_va -
				sizeof(struct aac_fib_xporthdr));
			pFibX->Handle = fib->hw_fib_va->header.Handle;
			pFibX->HostAddress =
				cpu_to_le64((u64)fib->hw_fib_pa);
			pFibX->Size = cpu_to_le32(
				le16_to_cpu(fib->hw_fib_va->header.Size));
			address = fib->hw_fib_pa -
				(u64)sizeof(struct aac_fib_xporthdr);
		}
		if (fibsize > 31)
			fibsize = 31;
		address |= fibsize;

#if defined(writeq)
		src_writeq(dev, MUnit.IQ_L, (u64)address);
#else
		spin_lock_irqsave(&fib->dev->iq_lock, flags);
		src_writel(dev, MUnit.IQ_H,
			upper_32_bits(address) & 0xffffffff);
		src_writel(dev, MUnit.IQ_L, address & 0xffffffff);
		spin_unlock_irqrestore(&fib->dev->iq_lock, flags);
#endif
	}
	return 0;
}

/**
 *	aac_src_ioremap
 *	@dev: device ioremap
 *	@size: mapping resize request
 *
 */
static int aac_src_ioremap(struct aac_dev *dev, u32 size)
{
	if (!size) {
		iounmap(dev->regs.src.bar1);
		dev->regs.src.bar1 = NULL;
		iounmap(dev->regs.src.bar0);
		dev->base = dev->regs.src.bar0 = NULL;
		return 0;
	}
	dev->regs.src.bar1 = ioremap(pci_resource_start(dev->pdev, 2),
		AAC_MIN_SRC_BAR1_SIZE);
	dev->base = NULL;
	if (dev->regs.src.bar1 == NULL)
		return -1;
	dev->base = dev->regs.src.bar0 = ioremap(dev->base_start, size);
	if (dev->base == NULL) {
		iounmap(dev->regs.src.bar1);
		dev->regs.src.bar1 = NULL;
		return -1;
	}
	dev->IndexRegs = &((struct src_registers __iomem *)
		dev->base)->u.tupelo.IndexRegs;
	return 0;
}

/**
 *  aac_srcv_ioremap
 *	@dev: device ioremap
 *	@size: mapping resize request
 *
 */
static int aac_srcv_ioremap(struct aac_dev *dev, u32 size)
{
	if (!size) {
		iounmap(dev->regs.src.bar0);
		dev->base = dev->regs.src.bar0 = NULL;
		return 0;
	}

	dev->regs.src.bar1 =
	ioremap(pci_resource_start(dev->pdev, 2), AAC_MIN_SRCV_BAR1_SIZE);
	dev->base = NULL;
	if (dev->regs.src.bar1 == NULL)
		return -1;
	dev->base = dev->regs.src.bar0 = ioremap(dev->base_start, size);
	if (dev->base == NULL) {
		iounmap(dev->regs.src.bar1);
		dev->regs.src.bar1 = NULL;
		return -1;
	}
	dev->IndexRegs = &((struct src_registers __iomem *)
		dev->base)->u.denali.IndexRegs;
	return 0;
}

void aac_set_intx_mode(struct aac_dev *dev)
{
	if (dev->msi_enabled) {
		aac_src_access_devreg(dev, AAC_ENABLE_INTX);
		dev->msi_enabled = 0;
		msleep(5000); /* Delay 5 seconds */
	}
}

static void aac_clear_omr(struct aac_dev *dev)
{
	u32 omr_value = 0;

	omr_value = src_readl(dev, MUnit.OMR);

	/*
	 * Check for PCI Errors or Kernel Panic
	 */
	if ((omr_value == INVALID_OMR) || (omr_value & KERNEL_PANIC))
		omr_value = 0;

	/*
	 * Preserve MSIX Value if any
	 */
	src_writel(dev, MUnit.OMR, omr_value & AAC_INT_MODE_MSIX);
	src_readl(dev, MUnit.OMR);
}

static void aac_dump_fw_fib_iop_reset(struct aac_dev *dev)
{
	__le32 supported_options3;

	if (!aac_fib_dump)
		return;

	supported_options3  = dev->supplement_adapter_info.supported_options3;
	if (!(supported_options3 & AAC_OPTION_SUPPORTED3_IOP_RESET_FIB_DUMP))
		return;

	aac_adapter_sync_cmd(dev, IOP_RESET_FW_FIB_DUMP,
			0, 0, 0,  0, 0, 0, NULL, NULL, NULL, NULL, NULL);
}

static bool aac_is_ctrl_up_and_running(struct aac_dev *dev)
{
	bool ctrl_up = true;
	unsigned long status, start;
	bool is_up = false;

	start = jiffies;
	do {
		schedule();
		status = src_readl(dev, MUnit.OMR);

		if (status == 0xffffffff)
			status = 0;

		if (status & KERNEL_BOOTING) {
			start = jiffies;
			continue;
		}

		if (time_after(jiffies, start+HZ*SOFT_RESET_TIME)) {
			ctrl_up = false;
			break;
		}

		is_up = status & KERNEL_UP_AND_RUNNING;

	} while (!is_up);

	return ctrl_up;
}

static void aac_src_drop_io(struct aac_dev *dev)
{
	if (!dev->soft_reset_support)
		return;

	aac_adapter_sync_cmd(dev, DROP_IO,
			0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL);
}

static void aac_notify_fw_of_iop_reset(struct aac_dev *dev)
{
	aac_adapter_sync_cmd(dev, IOP_RESET_ALWAYS, 0, 0, 0, 0, 0, 0, NULL,
						NULL, NULL, NULL, NULL);
	aac_src_drop_io(dev);
}

static void aac_send_iop_reset(struct aac_dev *dev)
{
	aac_dump_fw_fib_iop_reset(dev);

	aac_notify_fw_of_iop_reset(dev);

	aac_set_intx_mode(dev);

	aac_clear_omr(dev);

	src_writel(dev, MUnit.IDR, IOP_SRC_RESET_MASK);

	msleep(5000);
}

static void aac_send_hardware_soft_reset(struct aac_dev *dev)
{
	u_int32_t val;

	aac_clear_omr(dev);
	val = readl(((char *)(dev->base) + IBW_SWR_OFFSET));
	val |= 0x01;
	writel(val, ((char *)(dev->base) + IBW_SWR_OFFSET));
	msleep_interruptible(20000);
}

static int aac_src_restart_adapter(struct aac_dev *dev, int bled, u8 reset_type)
{
	bool is_ctrl_up;
	int ret = 0;

	if (bled < 0)
		goto invalid_out;

	if (bled)
		dev_err(&dev->pdev->dev, "adapter kernel panic'd %x.\n", bled);

	/*
	 * When there is a BlinkLED, IOP_RESET has not effect
	 */
	if (bled >= 2 && dev->sa_firmware && reset_type & HW_IOP_RESET)
		reset_type &= ~HW_IOP_RESET;

	dev->a_ops.adapter_enable_int = aac_src_disable_interrupt;

	dev_err(&dev->pdev->dev, "Controller reset type is %d\n", reset_type);

	if (reset_type & HW_IOP_RESET) {
		dev_info(&dev->pdev->dev, "Issuing IOP reset\n");
		aac_send_iop_reset(dev);

		/*
		 * Creates a delay or wait till up and running comes thru
		 */
		is_ctrl_up = aac_is_ctrl_up_and_running(dev);
		if (!is_ctrl_up)
			dev_err(&dev->pdev->dev, "IOP reset failed\n");
		else {
			dev_info(&dev->pdev->dev, "IOP reset succeeded\n");
			goto set_startup;
		}
	}

	if (!dev->sa_firmware) {
		dev_err(&dev->pdev->dev, "ARC Reset attempt failed\n");
		ret = -ENODEV;
		goto out;
	}

	if (reset_type & HW_SOFT_RESET) {
		dev_info(&dev->pdev->dev, "Issuing SOFT reset\n");
		aac_send_hardware_soft_reset(dev);
		dev->msi_enabled = 0;

		is_ctrl_up = aac_is_ctrl_up_and_running(dev);
		if (!is_ctrl_up) {
			dev_err(&dev->pdev->dev, "SOFT reset failed\n");
			ret = -ENODEV;
			goto out;
		} else
			dev_info(&dev->pdev->dev, "SOFT reset succeeded\n");
	}

set_startup:
	if (startup_timeout < 300)
		startup_timeout = 300;

out:
	return ret;

invalid_out:
	if (src_readl(dev, MUnit.OMR) & KERNEL_PANIC)
		ret = -ENODEV;
goto out;
}

/**
 *	aac_src_select_comm	-	Select communications method
 *	@dev: Adapter
 *	@comm: communications method
 */
static int aac_src_select_comm(struct aac_dev *dev, int comm)
{
	switch (comm) {
	case AAC_COMM_MESSAGE:
		dev->a_ops.adapter_intr = aac_src_intr_message;
		dev->a_ops.adapter_deliver = aac_src_deliver_message;
		break;
	default:
		return 1;
	}
	return 0;
}

/**
 *  aac_src_init	-	initialize an Cardinal Frey Bar card
 *  @dev: device to configure
 *
 */

int aac_src_init(struct aac_dev *dev)
{
	unsigned long start;
	unsigned long status;
	int restart = 0;
	int instance = dev->id;
	const char *name = dev->name;

	dev->a_ops.adapter_ioremap = aac_src_ioremap;
	dev->a_ops.adapter_comm = aac_src_select_comm;

	dev->base_size = AAC_MIN_SRC_BAR0_SIZE;
	if (aac_adapter_ioremap(dev, dev->base_size)) {
		printk(KERN_WARNING "%s: unable to map adapter.\n", name);
		goto error_iounmap;
	}

	/* Failure to reset here is an option ... */
	dev->a_ops.adapter_sync_cmd = src_sync_cmd;
	dev->a_ops.adapter_enable_int = aac_src_disable_interrupt;

	if (dev->init_reset) {
		dev->init_reset = false;
		if (!aac_src_restart_adapter(dev, 0, IOP_HWSOFT_RESET))
			++restart;
	}

	/*
	 *	Check to see if the board panic'd while booting.
	 */
	status = src_readl(dev, MUnit.OMR);
	if (status & KERNEL_PANIC) {
		if (aac_src_restart_adapter(dev,
			aac_src_check_health(dev), IOP_HWSOFT_RESET))
			goto error_iounmap;
		++restart;
	}
	/*
	 *	Check to see if the board failed any self tests.
	 */
	status = src_readl(dev, MUnit.OMR);
	if (status & SELF_TEST_FAILED) {
		printk(KERN_ERR "%s%d: adapter self-test failed.\n",
			dev->name, instance);
		goto error_iounmap;
	}
	/*
	 *	Check to see if the monitor panic'd while booting.
	 */
	if (status & MONITOR_PANIC) {
		printk(KERN_ERR "%s%d: adapter monitor panic.\n",
			dev->name, instance);
		goto error_iounmap;
	}
	start = jiffies;
	/*
	 *	Wait for the adapter to be up and running. Wait up to 3 minutes
	 */
	while (!((status = src_readl(dev, MUnit.OMR)) &
		KERNEL_UP_AND_RUNNING)) {
		if ((restart &&
		  (status & (KERNEL_PANIC|SELF_TEST_FAILED|MONITOR_PANIC))) ||
		  time_after(jiffies, start+HZ*startup_timeout)) {
			printk(KERN_ERR "%s%d: adapter kernel failed to start, init status = %lx.\n",
					dev->name, instance, status);
			goto error_iounmap;
		}
		if (!restart &&
		  ((status & (KERNEL_PANIC|SELF_TEST_FAILED|MONITOR_PANIC)) ||
		  time_after(jiffies, start + HZ *
		  ((startup_timeout > 60)
		    ? (startup_timeout - 60)
		    : (startup_timeout / 2))))) {
			if (likely(!aac_src_restart_adapter(dev,
				aac_src_check_health(dev), IOP_HWSOFT_RESET)))
				start = jiffies;
			++restart;
		}
		msleep(1);
	}
	if (restart && aac_commit)
		aac_commit = 1;
	/*
	 *	Fill in the common function dispatch table.
	 */
	dev->a_ops.adapter_interrupt = aac_src_interrupt_adapter;
	dev->a_ops.adapter_disable_int = aac_src_disable_interrupt;
	dev->a_ops.adapter_enable_int = aac_src_disable_interrupt;
	dev->a_ops.adapter_notify = aac_src_notify_adapter;
	dev->a_ops.adapter_sync_cmd = src_sync_cmd;
	dev->a_ops.adapter_check_health = aac_src_check_health;
	dev->a_ops.adapter_restart = aac_src_restart_adapter;
	dev->a_ops.adapter_start = aac_src_start_adapter;

	/*
	 *	First clear out all interrupts.  Then enable the one's that we
	 *	can handle.
	 */
	aac_adapter_comm(dev, AAC_COMM_MESSAGE);
	aac_adapter_disable_int(dev);
	src_writel(dev, MUnit.ODR_C, 0xffffffff);
	aac_adapter_enable_int(dev);

	if (aac_init_adapter(dev) == NULL)
		goto error_iounmap;
	if (dev->comm_interface != AAC_COMM_MESSAGE_TYPE1)
		goto error_iounmap;

	dev->msi = !pci_enable_msi(dev->pdev);

	dev->aac_msix[0].vector_no = 0;
	dev->aac_msix[0].dev = dev;

	if (request_irq(dev->pdev->irq, dev->a_ops.adapter_intr,
			IRQF_SHARED, "aacraid", &(dev->aac_msix[0]))  < 0) {

		if (dev->msi)
			pci_disable_msi(dev->pdev);

		printk(KERN_ERR "%s%d: Interrupt unavailable.\n",
			name, instance);
		goto error_iounmap;
	}
	dev->dbg_base = pci_resource_start(dev->pdev, 2);
	dev->dbg_base_mapped = dev->regs.src.bar1;
	dev->dbg_size = AAC_MIN_SRC_BAR1_SIZE;
	dev->a_ops.adapter_enable_int = aac_src_enable_interrupt_message;

	aac_adapter_enable_int(dev);

	if (!dev->sync_mode) {
		/*
		 * Tell the adapter that all is configured, and it can
		 * start accepting requests
		 */
		aac_src_start_adapter(dev);
	}
	return 0;

error_iounmap:

	return -1;
}

static int aac_src_wait_sync(struct aac_dev *dev, int *status)
{
	unsigned long start = jiffies;
	unsigned long usecs = 0;
	int delay = 5 * HZ;
	int rc = 1;

	while (time_before(jiffies, start+delay)) {
		/*
		 * Delay 5 microseconds to let Mon960 get info.
		 */
		udelay(5);

		/*
		 * Mon960 will set doorbell0 bit when it has completed the
		 * command.
		 */
		if (aac_src_get_sync_status(dev) & OUTBOUNDDOORBELL_0) {
			/*
			 * Clear: the doorbell.
			 */
			if (dev->msi_enabled)
				aac_src_access_devreg(dev, AAC_CLEAR_SYNC_BIT);
			else
				src_writel(dev, MUnit.ODR_C,
					OUTBOUNDDOORBELL_0 << SRC_ODR_SHIFT);
			rc = 0;

			break;
		}

		/*
		 * Yield the processor in case we are slow
		 */
		usecs = 1 * USEC_PER_MSEC;
		usleep_range(usecs, usecs + 50);
	}
	/*
	 * Pull the synch status from Mailbox 0.
	 */
	if (status && !rc) {
		status[0] = readl(&dev->IndexRegs->Mailbox[0]);
		status[1] = readl(&dev->IndexRegs->Mailbox[1]);
		status[2] = readl(&dev->IndexRegs->Mailbox[2]);
		status[3] = readl(&dev->IndexRegs->Mailbox[3]);
		status[4] = readl(&dev->IndexRegs->Mailbox[4]);
	}

	return rc;
}

/**
 *  aac_src_soft_reset	-	perform soft reset to speed up
 *  access
 *
 *  Assumptions: That the controller is in a state where we can
 *  bring it back to life with an init struct. We can only use
 *  fast sync commands, as the timeout is 5 seconds.
 *
 *  @dev: device to configure
 *
 */

static int aac_src_soft_reset(struct aac_dev *dev)
{
	u32 status_omr = src_readl(dev, MUnit.OMR);
	u32 status[5];
	int rc = 1;
	int state = 0;
	char *state_str[7] = {
		"GET_ADAPTER_PROPERTIES Failed",
		"GET_ADAPTER_PROPERTIES timeout",
		"SOFT_RESET not supported",
		"DROP_IO Failed",
		"DROP_IO timeout",
		"Check Health failed"
	};

	if (status_omr == INVALID_OMR)
		return 1;       // pcie hosed

	if (!(status_omr & KERNEL_UP_AND_RUNNING))
		return 1;       // not up and running

	/*
	 * We go into soft reset mode to allow us to handle response
	 */
	dev->in_soft_reset = 1;
	dev->msi_enabled = status_omr & AAC_INT_MODE_MSIX;

	/* Get adapter properties */
	rc = aac_adapter_sync_cmd(dev, GET_ADAPTER_PROPERTIES, 0, 0, 0,
		0, 0, 0, status+0, status+1, status+2, status+3, status+4);
	if (rc)
		goto out;

	state++;
	if (aac_src_wait_sync(dev, status)) {
		rc = 1;
		goto out;
	}

	state++;
	if (!(status[1] & le32_to_cpu(AAC_OPT_EXTENDED) &&
		(status[4] & le32_to_cpu(AAC_EXTOPT_SOFT_RESET)))) {
		rc = 2;
		goto out;
	}

	if ((status[1] & le32_to_cpu(AAC_OPT_EXTENDED)) &&
		(status[4] & le32_to_cpu(AAC_EXTOPT_SA_FIRMWARE)))
		dev->sa_firmware = 1;

	state++;
	rc = aac_adapter_sync_cmd(dev, DROP_IO, 0, 0, 0, 0, 0, 0,
		 status+0, status+1, status+2, status+3, status+4);

	if (rc)
		goto out;

	state++;
	if (aac_src_wait_sync(dev, status)) {
		rc = 3;
		goto out;
	}

	if (status[1])
		dev_err(&dev->pdev->dev, "%s: %d outstanding I/O pending\n",
			__func__, status[1]);

	state++;
	rc = aac_src_check_health(dev);

out:
	dev->in_soft_reset = 0;
	dev->msi_enabled = 0;
	if (rc)
		dev_err(&dev->pdev->dev, "%s: %s status = %d", __func__,
			state_str[state], rc);

	return rc;
}
/**
 *  aac_srcv_init	-	initialize an SRCv card
 *  @dev: device to configure
 *
 */

int aac_srcv_init(struct aac_dev *dev)
{
	unsigned long start;
	unsigned long status;
	int restart = 0;
	int instance = dev->id;
	const char *name = dev->name;

	dev->a_ops.adapter_ioremap = aac_srcv_ioremap;
	dev->a_ops.adapter_comm = aac_src_select_comm;

	dev->base_size = AAC_MIN_SRCV_BAR0_SIZE;
	if (aac_adapter_ioremap(dev, dev->base_size)) {
		printk(KERN_WARNING "%s: unable to map adapter.\n", name);
		goto error_iounmap;
	}

	/* Failure to reset here is an option ... */
	dev->a_ops.adapter_sync_cmd = src_sync_cmd;
	dev->a_ops.adapter_enable_int = aac_src_disable_interrupt;

	if (dev->init_reset) {
		dev->init_reset = false;
		if (aac_src_soft_reset(dev)) {
			aac_src_restart_adapter(dev, 0, IOP_HWSOFT_RESET);
			++restart;
		}
	}

	/*
	 *	Check to see if flash update is running.
	 *	Wait for the adapter to be up and running. Wait up to 5 minutes
	 */
	status = src_readl(dev, MUnit.OMR);
	if (status & FLASH_UPD_PENDING) {
		start = jiffies;
		do {
			status = src_readl(dev, MUnit.OMR);
			if (time_after(jiffies, start+HZ*FWUPD_TIMEOUT)) {
				printk(KERN_ERR "%s%d: adapter flash update failed.\n",
					dev->name, instance);
				goto error_iounmap;
			}
		} while (!(status & FLASH_UPD_SUCCESS) &&
			 !(status & FLASH_UPD_FAILED));
		/* Delay 10 seconds.
		 * Because right now FW is doing a soft reset,
		 * do not read scratch pad register at this time
		 */
		ssleep(10);
	}
	/*
	 *	Check to see if the board panic'd while booting.
	 */
	status = src_readl(dev, MUnit.OMR);
	if (status & KERNEL_PANIC) {
		if (aac_src_restart_adapter(dev,
			aac_src_check_health(dev), IOP_HWSOFT_RESET))
			goto error_iounmap;
		++restart;
	}
	/*
	 *	Check to see if the board failed any self tests.
	 */
	status = src_readl(dev, MUnit.OMR);
	if (status & SELF_TEST_FAILED) {
		printk(KERN_ERR "%s%d: adapter self-test failed.\n", dev->name, instance);
		goto error_iounmap;
	}
	/*
	 *	Check to see if the monitor panic'd while booting.
	 */
	if (status & MONITOR_PANIC) {
		printk(KERN_ERR "%s%d: adapter monitor panic.\n", dev->name, instance);
		goto error_iounmap;
	}

	start = jiffies;
	/*
	 *	Wait for the adapter to be up and running. Wait up to 3 minutes
	 */
	do {
		status = src_readl(dev, MUnit.OMR);
		if (status == INVALID_OMR)
			status = 0;

		if ((restart &&
		  (status & (KERNEL_PANIC|SELF_TEST_FAILED|MONITOR_PANIC))) ||
		  time_after(jiffies, start+HZ*startup_timeout)) {
			printk(KERN_ERR "%s%d: adapter kernel failed to start, init status = %lx.\n",
					dev->name, instance, status);
			goto error_iounmap;
		}
		if (!restart &&
		  ((status & (KERNEL_PANIC|SELF_TEST_FAILED|MONITOR_PANIC)) ||
		  time_after(jiffies, start + HZ *
		  ((startup_timeout > 60)
		    ? (startup_timeout - 60)
		    : (startup_timeout / 2))))) {
			if (likely(!aac_src_restart_adapter(dev,
				aac_src_check_health(dev), IOP_HWSOFT_RESET)))
				start = jiffies;
			++restart;
		}
		msleep(1);
	} while (!(status & KERNEL_UP_AND_RUNNING));

	if (restart && aac_commit)
		aac_commit = 1;
	/*
	 *	Fill in the common function dispatch table.
	 */
	dev->a_ops.adapter_interrupt = aac_src_interrupt_adapter;
	dev->a_ops.adapter_disable_int = aac_src_disable_interrupt;
	dev->a_ops.adapter_enable_int = aac_src_disable_interrupt;
	dev->a_ops.adapter_notify = aac_src_notify_adapter;
	dev->a_ops.adapter_sync_cmd = src_sync_cmd;
	dev->a_ops.adapter_check_health = aac_src_check_health;
	dev->a_ops.adapter_restart = aac_src_restart_adapter;
	dev->a_ops.adapter_start = aac_src_start_adapter;

	/*
	 *	First clear out all interrupts.  Then enable the one's that we
	 *	can handle.
	 */
	aac_adapter_comm(dev, AAC_COMM_MESSAGE);
	aac_adapter_disable_int(dev);
	src_writel(dev, MUnit.ODR_C, 0xffffffff);
	aac_adapter_enable_int(dev);

	if (aac_init_adapter(dev) == NULL)
		goto error_iounmap;
	if ((dev->comm_interface != AAC_COMM_MESSAGE_TYPE2) &&
		(dev->comm_interface != AAC_COMM_MESSAGE_TYPE3))
		goto error_iounmap;
	if (dev->msi_enabled)
		aac_src_access_devreg(dev, AAC_ENABLE_MSIX);

	if (aac_acquire_irq(dev))
		goto error_iounmap;

	dev->dbg_base = pci_resource_start(dev->pdev, 2);
	dev->dbg_base_mapped = dev->regs.src.bar1;
	dev->dbg_size = AAC_MIN_SRCV_BAR1_SIZE;
	dev->a_ops.adapter_enable_int = aac_src_enable_interrupt_message;

	aac_adapter_enable_int(dev);

	if (!dev->sync_mode) {
		/*
		 * Tell the adapter that all is configured, and it can
		 * start accepting requests
		 */
		aac_src_start_adapter(dev);
	}
	return 0;

error_iounmap:

	return -1;
}

void aac_src_access_devreg(struct aac_dev *dev, int mode)
{
	u_int32_t val;

	switch (mode) {
	case AAC_ENABLE_INTERRUPT:
		src_writel(dev,
			   MUnit.OIMR,
			   dev->OIMR = (dev->msi_enabled ?
					AAC_INT_ENABLE_TYPE1_MSIX :
					AAC_INT_ENABLE_TYPE1_INTX));
		break;

	case AAC_DISABLE_INTERRUPT:
		src_writel(dev,
			   MUnit.OIMR,
			   dev->OIMR = AAC_INT_DISABLE_ALL);
		break;

	case AAC_ENABLE_MSIX:
		/* set bit 6 */
		val = src_readl(dev, MUnit.IDR);
		val |= 0x40;
		src_writel(dev,  MUnit.IDR, val);
		src_readl(dev, MUnit.IDR);
		/* unmask int. */
		val = PMC_ALL_INTERRUPT_BITS;
		src_writel(dev, MUnit.IOAR, val);
		val = src_readl(dev, MUnit.OIMR);
		src_writel(dev,
			   MUnit.OIMR,
			   val & (~(PMC_GLOBAL_INT_BIT2 | PMC_GLOBAL_INT_BIT0)));
		break;

	case AAC_DISABLE_MSIX:
		/* reset bit 6 */
		val = src_readl(dev, MUnit.IDR);
		val &= ~0x40;
		src_writel(dev, MUnit.IDR, val);
		src_readl(dev, MUnit.IDR);
		break;

	case AAC_CLEAR_AIF_BIT:
		/* set bit 5 */
		val = src_readl(dev, MUnit.IDR);
		val |= 0x20;
		src_writel(dev, MUnit.IDR, val);
		src_readl(dev, MUnit.IDR);
		break;

	case AAC_CLEAR_SYNC_BIT:
		/* set bit 4 */
		val = src_readl(dev, MUnit.IDR);
		val |= 0x10;
		src_writel(dev, MUnit.IDR, val);
		src_readl(dev, MUnit.IDR);
		break;

	case AAC_ENABLE_INTX:
		/* set bit 7 */
		val = src_readl(dev, MUnit.IDR);
		val |= 0x80;
		src_writel(dev, MUnit.IDR, val);
		src_readl(dev, MUnit.IDR);
		/* unmask int. */
		val = PMC_ALL_INTERRUPT_BITS;
		src_writel(dev, MUnit.IOAR, val);
		src_readl(dev, MUnit.IOAR);
		val = src_readl(dev, MUnit.OIMR);
		src_writel(dev, MUnit.OIMR,
				val & (~(PMC_GLOBAL_INT_BIT2)));
		break;

	default:
		break;
	}
}

static int aac_src_get_sync_status(struct aac_dev *dev)
{
	int msix_val = 0;
	int legacy_val = 0;

	msix_val = src_readl(dev, MUnit.ODR_MSI) & SRC_MSI_READ_MASK ? 1 : 0;

	if (!dev->msi_enabled) {
		/*
		 * if Legacy int status indicates cmd is not complete
		 * sample MSIx register to see if it indiactes cmd complete,
		 * if yes set the controller in MSIx mode and consider cmd
		 * completed
		 */
		legacy_val = src_readl(dev, MUnit.ODR_R) >> SRC_ODR_SHIFT;
		if (!(legacy_val & 1) && msix_val)
			dev->msi_enabled = 1;
		return legacy_val;
	}

	return msix_val;
}
