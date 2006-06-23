/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *  sa.c
 *
 * Abstract: Drawbridge specific support functions
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <asm/semaphore.h>

#include <scsi/scsi_host.h>

#include "aacraid.h"

static irqreturn_t aac_sa_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct aac_dev *dev = dev_id;
	unsigned short intstat, mask;

	intstat = sa_readw(dev, DoorbellReg_p);
	/*
	 *	Read mask and invert because drawbridge is reversed.
	 *	This allows us to only service interrupts that have been enabled.
	 */
	mask = ~(sa_readw(dev, SaDbCSR.PRISETIRQMASK));

	/* Check to see if this is our interrupt.  If it isn't just return */

	if (intstat & mask) {
		if (intstat & PrintfReady) {
			aac_printf(dev, sa_readl(dev, Mailbox5));
			sa_writew(dev, DoorbellClrReg_p, PrintfReady); /* clear PrintfReady */
			sa_writew(dev, DoorbellReg_s, PrintfDone);
		} else if (intstat & DOORBELL_1) {	// dev -> Host Normal Command Ready
			sa_writew(dev, DoorbellClrReg_p, DOORBELL_1);
			aac_command_normal(&dev->queues->queue[HostNormCmdQueue]);
		} else if (intstat & DOORBELL_2) {	// dev -> Host Normal Response Ready
			sa_writew(dev, DoorbellClrReg_p, DOORBELL_2);
			aac_response_normal(&dev->queues->queue[HostNormRespQueue]);
		} else if (intstat & DOORBELL_3) {	// dev -> Host Normal Command Not Full
			sa_writew(dev, DoorbellClrReg_p, DOORBELL_3);
		} else if (intstat & DOORBELL_4) {	// dev -> Host Normal Response Not Full
			sa_writew(dev, DoorbellClrReg_p, DOORBELL_4);
		}
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/**
 *	aac_sa_disable_interrupt	-	disable interrupt
 *	@dev: Which adapter to enable.
 */

static void aac_sa_disable_interrupt (struct aac_dev *dev)
{
	sa_writew(dev, SaDbCSR.PRISETIRQMASK, 0xffff);
}

/**
 *	aac_sa_notify_adapter		-	handle adapter notification
 *	@dev:	Adapter that notification is for
 *	@event:	Event to notidy
 *
 *	Notify the adapter of an event
 */
 
static void aac_sa_notify_adapter(struct aac_dev *dev, u32 event)
{
	switch (event) {

	case AdapNormCmdQue:
		sa_writew(dev, DoorbellReg_s,DOORBELL_1);
		break;
	case HostNormRespNotFull:
		sa_writew(dev, DoorbellReg_s,DOORBELL_4);
		break;
	case AdapNormRespQue:
		sa_writew(dev, DoorbellReg_s,DOORBELL_2);
		break;
	case HostNormCmdNotFull:
		sa_writew(dev, DoorbellReg_s,DOORBELL_3);
		break;
	case HostShutdown:
		/*
		sa_sync_cmd(dev, HOST_CRASHING, 0, 0, 0, 0, 0, 0,
		NULL, NULL, NULL, NULL, NULL);
		*/
		break;
	case FastIo:
		sa_writew(dev, DoorbellReg_s,DOORBELL_6);
		break;
	case AdapPrintfDone:
		sa_writew(dev, DoorbellReg_s,DOORBELL_5);
		break;
	default:
		BUG();
		break;
	}
}


/**
 *	sa_sync_cmd	-	send a command and wait
 *	@dev: Adapter
 *	@command: Command to execute
 *	@p1: first parameter
 *	@ret: adapter status
 *
 *	This routine will send a synchronous command to the adapter and wait 
 *	for its	completion.
 */

static int sa_sync_cmd(struct aac_dev *dev, u32 command, 
		u32 p1, u32 p2, u32 p3, u32 p4, u32 p5, u32 p6,
		u32 *ret, u32 *r1, u32 *r2, u32 *r3, u32 *r4)
{
	unsigned long start;
 	int ok;
	/*
	 *	Write the Command into Mailbox 0
	 */
	sa_writel(dev, Mailbox0, command);
	/*
	 *	Write the parameters into Mailboxes 1 - 4
	 */
	sa_writel(dev, Mailbox1, p1);
	sa_writel(dev, Mailbox2, p2);
	sa_writel(dev, Mailbox3, p3);
	sa_writel(dev, Mailbox4, p4);

	/*
	 *	Clear the synch command doorbell to start on a clean slate.
	 */
	sa_writew(dev, DoorbellClrReg_p, DOORBELL_0);
	/*
	 *	Signal that there is a new synch command
	 */
	sa_writew(dev, DoorbellReg_s, DOORBELL_0);

	ok = 0;
	start = jiffies;

	while(time_before(jiffies, start+30*HZ))
	{
		/*
		 *	Delay 5uS so that the monitor gets access
		 */
		udelay(5);
		/*
		 *	Mon110 will set doorbell0 bit when it has 
		 *	completed the command.
		 */
		if(sa_readw(dev, DoorbellReg_p) & DOORBELL_0)  {
			ok = 1;
			break;
		}
		msleep(1);
	}

	if (ok != 1)
		return -ETIMEDOUT;
	/*
	 *	Clear the synch command doorbell.
	 */
	sa_writew(dev, DoorbellClrReg_p, DOORBELL_0);
	/*
	 *	Pull the synch status from Mailbox 0.
	 */
	if (ret)
		*ret = sa_readl(dev, Mailbox0);
	if (r1)
		*r1 = sa_readl(dev, Mailbox1);
	if (r2)
		*r2 = sa_readl(dev, Mailbox2);
	if (r3)
		*r3 = sa_readl(dev, Mailbox3);
	if (r4)
		*r4 = sa_readl(dev, Mailbox4);
	return 0;
}

/**
 *	aac_sa_interrupt_adapter	-	interrupt an adapter
 *	@dev: Which adapter to enable.
 *
 *	Breakpoint an adapter.
 */
 
static void aac_sa_interrupt_adapter (struct aac_dev *dev)
{
	sa_sync_cmd(dev, BREAKPOINT_REQUEST, 0, 0, 0, 0, 0, 0,
			NULL, NULL, NULL, NULL, NULL);
}

/**
 *	aac_sa_start_adapter		-	activate adapter
 *	@dev:	Adapter
 *
 *	Start up processing on an ARM based AAC adapter
 */

static void aac_sa_start_adapter(struct aac_dev *dev)
{
	struct aac_init *init;
	/*
	 * Fill in the remaining pieces of the init.
	 */
	init = dev->init;
	init->HostElapsedSeconds = cpu_to_le32(get_seconds());
	/* We can only use a 32 bit address here */
	sa_sync_cmd(dev, INIT_STRUCT_BASE_ADDRESS, 
			(u32)(ulong)dev->init_pa, 0, 0, 0, 0, 0,
			NULL, NULL, NULL, NULL, NULL);
}

/**
 *	aac_sa_check_health
 *	@dev: device to check if healthy
 *
 *	Will attempt to determine if the specified adapter is alive and
 *	capable of handling requests, returning 0 if alive.
 */
static int aac_sa_check_health(struct aac_dev *dev)
{
	long status = sa_readl(dev, Mailbox7);

	/*
	 *	Check to see if the board failed any self tests.
	 */
	if (status & SELF_TEST_FAILED)
		return -1;
	/*
	 *	Check to see if the board panic'd while booting.
	 */
	if (status & KERNEL_PANIC)
		return -2;
	/*
	 *	Wait for the adapter to be up and running. Wait up to 3 minutes
	 */
	if (!(status & KERNEL_UP_AND_RUNNING))
		return -3;
	/*
	 *	Everything is OK
	 */
	return 0;
}

/**
 *	aac_sa_init	-	initialize an ARM based AAC card
 *	@dev: device to configure
 *
 *	Allocate and set up resources for the ARM based AAC variants. The 
 *	device_interface in the commregion will be allocated and linked 
 *	to the comm region.
 */

int aac_sa_init(struct aac_dev *dev)
{
	unsigned long start;
	unsigned long status;
	int instance;
	const char *name;

	instance = dev->id;
	name     = dev->name;

	/*
	 *	Check to see if the board failed any self tests.
	 */
	if (sa_readl(dev, Mailbox7) & SELF_TEST_FAILED) {
		printk(KERN_WARNING "%s%d: adapter self-test failed.\n", name, instance);
		goto error_iounmap;
	}
	/*
	 *	Check to see if the board panic'd while booting.
	 */
	if (sa_readl(dev, Mailbox7) & KERNEL_PANIC) {
		printk(KERN_WARNING "%s%d: adapter kernel panic'd.\n", name, instance);
		goto error_iounmap;
	}
	start = jiffies;
	/*
	 *	Wait for the adapter to be up and running. Wait up to 3 minutes.
	 */
	while (!(sa_readl(dev, Mailbox7) & KERNEL_UP_AND_RUNNING)) {
		if (time_after(jiffies, start+startup_timeout*HZ)) {
			status = sa_readl(dev, Mailbox7);
			printk(KERN_WARNING "%s%d: adapter kernel failed to start, init status = %lx.\n", 
					name, instance, status);
			goto error_iounmap;
		}
		msleep(1);
	}

	if (request_irq(dev->scsi_host_ptr->irq, aac_sa_intr, SA_SHIRQ|SA_INTERRUPT, "aacraid", (void *)dev ) < 0) {
		printk(KERN_WARNING "%s%d: Interrupt unavailable.\n", name, instance);
		goto error_iounmap;
	}

	/*
	 *	Fill in the function dispatch table.
	 */

	dev->a_ops.adapter_interrupt = aac_sa_interrupt_adapter;
	dev->a_ops.adapter_disable_int = aac_sa_disable_interrupt;
	dev->a_ops.adapter_notify = aac_sa_notify_adapter;
	dev->a_ops.adapter_sync_cmd = sa_sync_cmd;
	dev->a_ops.adapter_check_health = aac_sa_check_health;

	/*
	 *	First clear out all interrupts.  Then enable the one's that 
	 *	we can handle.
	 */
	sa_writew(dev, SaDbCSR.PRISETIRQMASK, 0xffff);
	sa_writew(dev, SaDbCSR.PRICLEARIRQMASK, (PrintfReady | DOORBELL_1 | 
				DOORBELL_2 | DOORBELL_3 | DOORBELL_4));

	if(aac_init_adapter(dev) == NULL)
		goto error_irq;

	/*
	 *	Tell the adapter that all is configure, and it can start 
	 *	accepting requests
	 */
	aac_sa_start_adapter(dev);
	return 0;

error_irq:
	sa_writew(dev, SaDbCSR.PRISETIRQMASK, 0xffff);
	free_irq(dev->scsi_host_ptr->irq, (void *)dev);

error_iounmap:

	return -1;
}

