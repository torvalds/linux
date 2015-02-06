/* $Id: aha1542.c,v 1.1 1992/07/24 06:27:38 root Exp root $
 *  linux/kernel/aha1542.c
 *
 *  Copyright (C) 1992  Tommy Thorn
 *  Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *
 *  Modified by Eric Youngdale
 *        Use request_irq and request_dma to help prevent unexpected conflicts
 *        Set up on-board DMA controller, such that we do not have to
 *        have the bios enabled to use the aha1542.
 *  Modified by David Gentzel
 *        Don't call request_dma if dma mask is 0 (for BusLogic BT-445S VL-Bus
 *        controller).
 *  Modified by Matti Aarnio
 *        Accept parameters from LILO cmd-line. -- 1-Oct-94
 *  Modified by Mike McLagan <mike.mclagan@linux.org>
 *        Recognise extended mode on AHA1542CP, different bit than 1542CF
 *        1-Jan-97
 *  Modified by Bjorn L. Thordarson and Einar Thor Einarsson
 *        Recognize that DMA0 is valid DMA channel -- 13-Jul-98
 *  Modified by Chris Faulhaber <jedgar@fxp.org>
 *        Added module command-line options
 *        19-Jul-99
 *  Modified by Adam Fritzler
 *        Added proper detection of the AHA-1640 (MCA, now deleted)
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/isa.h>
#include <linux/pnp.h>
#include <linux/blkdev.h>
#include <linux/slab.h>

#include <asm/dma.h>
#include <asm/io.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "aha1542.h"
#include <linux/stat.h>

#ifdef DEBUG
#define DEB(x) x
#else
#define DEB(x)
#endif
#define MAXBOARDS 4

static bool isapnp = 1;
module_param(isapnp, bool, 0);
MODULE_PARM_DESC(isapnp, "enable PnP support (default=1)");

static int io[MAXBOARDS] = { 0x330, 0x334, 0, 0 };
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io, "base IO address of controller (0x130,0x134,0x230,0x234,0x330,0x334, default=0x330,0x334)");

/* time AHA spends on the AT-bus during data transfer */
static int bus_on[MAXBOARDS] = { -1, -1, -1, -1 }; /* power-on default: 11us */
module_param_array(bus_on, int, NULL, 0);
MODULE_PARM_DESC(bus_on, "bus on time [us] (2-15, default=-1 [HW default: 11])");

/* time AHA spends off the bus (not to monopolize it) during data transfer  */
static int bus_off[MAXBOARDS] = { -1, -1, -1, -1 }; /* power-on default: 4us */
module_param_array(bus_off, int, NULL, 0);
MODULE_PARM_DESC(bus_off, "bus off time [us] (1-64, default=-1 [HW default: 4])");

/* default is jumper selected (J1 on 1542A), factory default = 5 MB/s */
static int dma_speed[MAXBOARDS] = { -1, -1, -1, -1 };
module_param_array(dma_speed, int, NULL, 0);
MODULE_PARM_DESC(dma_speed, "DMA speed [MB/s] (5,6,7,8,10, default=-1 [by jumper])");

#define BIOS_TRANSLATION_6432 1	/* Default case these days */
#define BIOS_TRANSLATION_25563 2	/* Big disk case */

struct aha1542_hostdata {
	/* This will effectively start both of them at the first mailbox */
	int bios_translation;	/* Mapping bios uses - for compatibility */
	int aha1542_last_mbi_used;
	int aha1542_last_mbo_used;
	struct scsi_cmnd *int_cmds[AHA1542_MAILBOXES];
	struct mailbox mb[2 * AHA1542_MAILBOXES];
	struct ccb ccb[AHA1542_MAILBOXES];
};

static DEFINE_SPINLOCK(aha1542_lock);

static inline void aha1542_intr_reset(u16 base)
{
	outb(IRST, CONTROL(base));
}

static inline bool wait_mask(u16 port, u8 mask, u8 allof, u8 noneof, int timeout)
{
	bool delayed = true;

	if (timeout == 0) {
		timeout = 3000000;
		delayed = false;
	}

	while (1) {
		u8 bits = inb(port) & mask;
		if ((bits & allof) == allof && ((bits & noneof) == 0))
			break;
		if (delayed)
			mdelay(1);
		if (--timeout == 0)
			return false;
	}

	return true;
}

/* This is a bit complicated, but we need to make sure that an interrupt
   routine does not send something out while we are in the middle of this.
   Fortunately, it is only at boot time that multi-byte messages
   are ever sent. */
static int aha1542_outb(unsigned int base, u8 val)
{
	unsigned long flags;

	while (1) {
		if (!wait_mask(STATUS(base), CDF, 0, CDF, 0)) {
			printk(KERN_ERR "aha1542_outb failed");
			return 1;
		}
		spin_lock_irqsave(&aha1542_lock, flags);
		if (inb(STATUS(base)) & CDF) {
			spin_unlock_irqrestore(&aha1542_lock, flags);
			continue;
		}
		outb(val, DATA(base));
		spin_unlock_irqrestore(&aha1542_lock, flags);
		return 0;
	}
}

static int aha1542_out(unsigned int base, u8 *buf, int len)
{
	unsigned long flags;

	spin_lock_irqsave(&aha1542_lock, flags);
	while (len--) {
		if (!wait_mask(STATUS(base), CDF, 0, CDF, 0)) {
			spin_unlock_irqrestore(&aha1542_lock, flags);
			printk(KERN_ERR "aha1542_out failed(%d): ", len + 1);
			return 1;
		}
		outb(*buf++, DATA(base));
	}
	spin_unlock_irqrestore(&aha1542_lock, flags);
	if (!wait_mask(INTRFLAGS(base), INTRMASK, HACC, 0, 0))
		return 1;

	return 0;
}

/* Only used at boot time, so we do not need to worry about latency as much
   here */

static int aha1542_in(unsigned int base, u8 *buf, int len, int timeout)
{
	unsigned long flags;

	spin_lock_irqsave(&aha1542_lock, flags);
	while (len--) {
		if (!wait_mask(STATUS(base), DF, DF, 0, timeout)) {
			spin_unlock_irqrestore(&aha1542_lock, flags);
			if (timeout == 0)
				printk(KERN_ERR "aha1542_in failed(%d): ", len + 1);
			return 1;
		}
		*buf++ = inb(DATA(base));
	}
	spin_unlock_irqrestore(&aha1542_lock, flags);
	return 0;
}

static int makecode(unsigned hosterr, unsigned scsierr)
{
	switch (hosterr) {
	case 0x0:
	case 0xa:		/* Linked command complete without error and linked normally */
	case 0xb:		/* Linked command complete without error, interrupt generated */
		hosterr = 0;
		break;

	case 0x11:		/* Selection time out-The initiator selection or target
				   reselection was not complete within the SCSI Time out period */
		hosterr = DID_TIME_OUT;
		break;

	case 0x12:		/* Data overrun/underrun-The target attempted to transfer more data
				   than was allocated by the Data Length field or the sum of the
				   Scatter / Gather Data Length fields. */

	case 0x13:		/* Unexpected bus free-The target dropped the SCSI BSY at an unexpected time. */

	case 0x15:		/* MBO command was not 00, 01 or 02-The first byte of the CB was
				   invalid. This usually indicates a software failure. */

	case 0x16:		/* Invalid CCB Operation Code-The first byte of the CCB was invalid.
				   This usually indicates a software failure. */

	case 0x17:		/* Linked CCB does not have the same LUN-A subsequent CCB of a set
				   of linked CCB's does not specify the same logical unit number as
				   the first. */
	case 0x18:		/* Invalid Target Direction received from Host-The direction of a
				   Target Mode CCB was invalid. */

	case 0x19:		/* Duplicate CCB Received in Target Mode-More than once CCB was
				   received to service data transfer between the same target LUN
				   and initiator SCSI ID in the same direction. */

	case 0x1a:		/* Invalid CCB or Segment List Parameter-A segment list with a zero
				   length segment or invalid segment list boundaries was received.
				   A CCB parameter was invalid. */
		DEB(printk("Aha1542: %x %x\n", hosterr, scsierr));
		hosterr = DID_ERROR;	/* Couldn't find any better */
		break;

	case 0x14:		/* Target bus phase sequence failure-An invalid bus phase or bus
				   phase sequence was requested by the target. The host adapter
				   will generate a SCSI Reset Condition, notifying the host with
				   a SCRD interrupt */
		hosterr = DID_RESET;
		break;
	default:
		printk(KERN_ERR "aha1542: makecode: unknown hoststatus %x\n", hosterr);
		break;
	}
	return scsierr | (hosterr << 16);
}

static int aha1542_test_port(int bse, struct Scsi_Host *shpnt)
{
	u8 inquiry_result[4];
	int i;

	/* Quick and dirty test for presence of the card. */
	if (inb(STATUS(bse)) == 0xff)
		return 0;

	/* Reset the adapter. I ought to make a hard reset, but it's not really necessary */

	/* In case some other card was probing here, reset interrupts */
	aha1542_intr_reset(bse);	/* reset interrupts, so they don't block */

	outb(SRST | IRST /*|SCRST */ , CONTROL(bse));

	mdelay(20);		/* Wait a little bit for things to settle down. */

	/* Expect INIT and IDLE, any of the others are bad */
	if (!wait_mask(STATUS(bse), STATMASK, INIT | IDLE, STST | DIAGF | INVDCMD | DF | CDF, 0))
		return 0;

	/* Shouldn't have generated any interrupts during reset */
	if (inb(INTRFLAGS(bse)) & INTRMASK)
		return 0;

	/* Perform a host adapter inquiry instead so we do not need to set
	   up the mailboxes ahead of time */

	aha1542_outb(bse, CMD_INQUIRY);

	for (i = 0; i < 4; i++) {
		if (!wait_mask(STATUS(bse), DF, DF, 0, 0))
			return 0;
		inquiry_result[i] = inb(DATA(bse));
	}

	/* Reading port should reset DF */
	if (inb(STATUS(bse)) & DF)
		return 0;

	/* When HACC, command is completed, and we're though testing */
	if (!wait_mask(INTRFLAGS(bse), HACC, HACC, 0, 0))
		return 0;

	/* Clear interrupts */
	outb(IRST, CONTROL(bse));

	return 1;
}

/* A "high" level interrupt handler */
static void aha1542_intr_handle(struct Scsi_Host *shost)
{
	struct aha1542_hostdata *aha1542 = shost_priv(shost);
	void (*my_done)(struct scsi_cmnd *) = NULL;
	int errstatus, mbi, mbo, mbistatus;
	int number_serviced;
	unsigned long flags;
	struct scsi_cmnd *tmp_cmd;
	int flag;
	struct mailbox *mb = aha1542->mb;
	struct ccb *ccb = aha1542->ccb;

#ifdef DEBUG
	{
		flag = inb(INTRFLAGS(shost->io_port));
		printk(KERN_DEBUG "aha1542_intr_handle: ");
		if (!(flag & ANYINTR))
			printk("no interrupt?");
		if (flag & MBIF)
			printk("MBIF ");
		if (flag & MBOA)
			printk("MBOF ");
		if (flag & HACC)
			printk("HACC ");
		if (flag & SCRD)
			printk("SCRD ");
		printk("status %02x\n", inb(STATUS(shost->io_port)));
	};
#endif
	number_serviced = 0;

	while (1 == 1) {
		flag = inb(INTRFLAGS(shost->io_port));

		/* Check for unusual interrupts.  If any of these happen, we should
		   probably do something special, but for now just printing a message
		   is sufficient.  A SCSI reset detected is something that we really
		   need to deal with in some way. */
		if (flag & ~MBIF) {
			if (flag & MBOA)
				printk("MBOF ");
			if (flag & HACC)
				printk("HACC ");
			if (flag & SCRD)
				printk("SCRD ");
		}
		aha1542_intr_reset(shost->io_port);

		spin_lock_irqsave(&aha1542_lock, flags);
		mbi = aha1542->aha1542_last_mbi_used + 1;
		if (mbi >= 2 * AHA1542_MAILBOXES)
			mbi = AHA1542_MAILBOXES;

		do {
			if (mb[mbi].status != 0)
				break;
			mbi++;
			if (mbi >= 2 * AHA1542_MAILBOXES)
				mbi = AHA1542_MAILBOXES;
		} while (mbi != aha1542->aha1542_last_mbi_used);

		if (mb[mbi].status == 0) {
			spin_unlock_irqrestore(&aha1542_lock, flags);
			/* Hmm, no mail.  Must have read it the last time around */
			if (!number_serviced)
				printk(KERN_WARNING "aha1542.c: interrupt received, but no mail.\n");
			return;
		};

		mbo = (scsi2int(mb[mbi].ccbptr) - (isa_virt_to_bus(&ccb[0]))) / sizeof(struct ccb);
		mbistatus = mb[mbi].status;
		mb[mbi].status = 0;
		aha1542->aha1542_last_mbi_used = mbi;
		spin_unlock_irqrestore(&aha1542_lock, flags);

#ifdef DEBUG
		{
			if (ccb[mbo].tarstat | ccb[mbo].hastat)
				printk(KERN_DEBUG "aha1542_command: returning %x (status %d)\n",
				       ccb[mbo].tarstat + ((int) ccb[mbo].hastat << 16), mb[mbi].status);
		};
#endif

		if (mbistatus == 3)
			continue;	/* Aborted command not found */

#ifdef DEBUG
		printk(KERN_DEBUG "...done %d %d\n", mbo, mbi);
#endif

		tmp_cmd = aha1542->int_cmds[mbo];

		if (!tmp_cmd || !tmp_cmd->scsi_done) {
			printk(KERN_WARNING "aha1542_intr_handle: Unexpected interrupt\n");
			printk(KERN_WARNING "tarstat=%x, hastat=%x idlun=%x ccb#=%d \n", ccb[mbo].tarstat,
			       ccb[mbo].hastat, ccb[mbo].idlun, mbo);
			return;
		}
		my_done = tmp_cmd->scsi_done;
		kfree(tmp_cmd->host_scribble);
		tmp_cmd->host_scribble = NULL;
		/* Fetch the sense data, and tuck it away, in the required slot.  The
		   Adaptec automatically fetches it, and there is no guarantee that
		   we will still have it in the cdb when we come back */
		if (ccb[mbo].tarstat == 2)
			memcpy(tmp_cmd->sense_buffer, &ccb[mbo].cdb[ccb[mbo].cdblen],
			       SCSI_SENSE_BUFFERSIZE);


		/* is there mail :-) */

		/* more error checking left out here */
		if (mbistatus != 1)
			/* This is surely wrong, but I don't know what's right */
			errstatus = makecode(ccb[mbo].hastat, ccb[mbo].tarstat);
		else
			errstatus = 0;

#ifdef DEBUG
		if (errstatus)
			printk(KERN_DEBUG "(aha1542 error:%x %x %x) ", errstatus,
			       ccb[mbo].hastat, ccb[mbo].tarstat);
#endif

		if (ccb[mbo].tarstat == 2) {
#ifdef DEBUG
			int i;
#endif
			DEB(printk("aha1542_intr_handle: sense:"));
#ifdef DEBUG
			for (i = 0; i < 12; i++)
				printk("%02x ", ccb[mbo].cdb[ccb[mbo].cdblen + i]);
			printk("\n");
#endif
			/*
			   DEB(printk("aha1542_intr_handle: buf:"));
			   for (i = 0; i < bufflen; i++)
			   printk("%02x ", ((unchar *)buff)[i]);
			   printk("\n");
			 */
		}
		DEB(if (errstatus) printk("aha1542_intr_handle: returning %6x\n", errstatus));
		tmp_cmd->result = errstatus;
		aha1542->int_cmds[mbo] = NULL;	/* This effectively frees up the mailbox slot, as
						   far as queuecommand is concerned */
		my_done(tmp_cmd);
		number_serviced++;
	};
}

/* A quick wrapper for do_aha1542_intr_handle to grab the spin lock */
static irqreturn_t do_aha1542_intr_handle(int dummy, void *dev_id)
{
	unsigned long flags;
	struct Scsi_Host *shost = dev_id;

	spin_lock_irqsave(shost->host_lock, flags);
	aha1542_intr_handle(shost);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return IRQ_HANDLED;
}

static int aha1542_queuecommand_lck(struct scsi_cmnd *cmd, void (*done) (struct scsi_cmnd *))
{
	struct aha1542_hostdata *aha1542 = shost_priv(cmd->device->host);
	u8 direction;
	u8 target = cmd->device->id;
	u8 lun = cmd->device->lun;
	unsigned long flags;
	int bufflen = scsi_bufflen(cmd);
	int mbo;
	struct mailbox *mb = aha1542->mb;
	struct ccb *ccb = aha1542->ccb;

	DEB(int i);

	DEB(if (target > 1) {
	    cmd->result = DID_TIME_OUT << 16;
	    done(cmd); return 0;
	    }
	);

	if (*cmd->cmnd == REQUEST_SENSE) {
		/* Don't do the command - we have the sense data already */
		cmd->result = 0;
		done(cmd);
		return 0;
	}
#ifdef DEBUG
	if (*cmd->cmnd == READ_10 || *cmd->cmnd == WRITE_10)
		i = xscsi2int(cmd->cmnd + 2);
	else if (*cmd->cmnd == READ_6 || *cmd->cmnd == WRITE_6)
		i = scsi2int(cmd->cmnd + 2);
	else
		i = -1;
	if (done)
		printk(KERN_DEBUG "aha1542_queuecommand: dev %d cmd %02x pos %d len %d ", target, *cmd->cmnd, i, bufflen);
	else
		printk(KERN_DEBUG "aha1542_command: dev %d cmd %02x pos %d len %d ", target, *cmd->cmnd, i, bufflen);
	printk(KERN_DEBUG "aha1542_queuecommand: dumping scsi cmd:");
	for (i = 0; i < cmd->cmd_len; i++)
		printk("%02x ", cmd->cmnd[i]);
	printk("\n");
	if (*cmd->cmnd == WRITE_10 || *cmd->cmnd == WRITE_6)
		return 0;	/* we are still testing, so *don't* write */
#endif
	/* Use the outgoing mailboxes in a round-robin fashion, because this
	   is how the host adapter will scan for them */

	spin_lock_irqsave(&aha1542_lock, flags);
	mbo = aha1542->aha1542_last_mbo_used + 1;
	if (mbo >= AHA1542_MAILBOXES)
		mbo = 0;

	do {
		if (mb[mbo].status == 0 && aha1542->int_cmds[mbo] == NULL)
			break;
		mbo++;
		if (mbo >= AHA1542_MAILBOXES)
			mbo = 0;
	} while (mbo != aha1542->aha1542_last_mbo_used);

	if (mb[mbo].status || aha1542->int_cmds[mbo])
		panic("Unable to find empty mailbox for aha1542.\n");

	aha1542->int_cmds[mbo] = cmd;	/* This will effectively prevent someone else from
					   screwing with this cdb. */

	aha1542->aha1542_last_mbo_used = mbo;
	spin_unlock_irqrestore(&aha1542_lock, flags);

#ifdef DEBUG
	printk(KERN_DEBUG "Sending command (%d %x)...", mbo, done);
#endif

	any2scsi(mb[mbo].ccbptr, isa_virt_to_bus(&ccb[mbo]));	/* This gets trashed for some reason */

	memset(&ccb[mbo], 0, sizeof(struct ccb));

	ccb[mbo].cdblen = cmd->cmd_len;

	direction = 0;
	if (*cmd->cmnd == READ_10 || *cmd->cmnd == READ_6)
		direction = 8;
	else if (*cmd->cmnd == WRITE_10 || *cmd->cmnd == WRITE_6)
		direction = 16;

	memcpy(ccb[mbo].cdb, cmd->cmnd, ccb[mbo].cdblen);

	if (bufflen) {
		struct scatterlist *sg;
		struct chain *cptr;
#ifdef DEBUG
		unsigned char *ptr;
#endif
		int i, sg_count = scsi_sg_count(cmd);
		ccb[mbo].op = 2;	/* SCSI Initiator Command  w/scatter-gather */
		cmd->host_scribble = kmalloc(sizeof(*cptr)*sg_count,
		                                         GFP_KERNEL | GFP_DMA);
		cptr = (struct chain *) cmd->host_scribble;
		if (cptr == NULL) {
			/* free the claimed mailbox slot */
			aha1542->int_cmds[mbo] = NULL;
			return SCSI_MLQUEUE_HOST_BUSY;
		}
		scsi_for_each_sg(cmd, sg, sg_count, i) {
			any2scsi(cptr[i].dataptr, isa_page_to_bus(sg_page(sg))
								+ sg->offset);
			any2scsi(cptr[i].datalen, sg->length);
		};
		any2scsi(ccb[mbo].datalen, sg_count * sizeof(struct chain));
		any2scsi(ccb[mbo].dataptr, isa_virt_to_bus(cptr));
#ifdef DEBUG
		printk("cptr %x: ", cptr);
		ptr = (unsigned char *) cptr;
		for (i = 0; i < 18; i++)
			printk("%02x ", ptr[i]);
#endif
	} else {
		ccb[mbo].op = 0;	/* SCSI Initiator Command */
		cmd->host_scribble = NULL;
		any2scsi(ccb[mbo].datalen, 0);
		any2scsi(ccb[mbo].dataptr, 0);
	};
	ccb[mbo].idlun = (target & 7) << 5 | direction | (lun & 7);	/*SCSI Target Id */
	ccb[mbo].rsalen = 16;
	ccb[mbo].linkptr[0] = ccb[mbo].linkptr[1] = ccb[mbo].linkptr[2] = 0;
	ccb[mbo].commlinkid = 0;

#ifdef DEBUG
	{
		int i;
		printk(KERN_DEBUG "aha1542_command: sending.. ");
		for (i = 0; i < sizeof(ccb[mbo]) - 10; i++)
			printk("%02x ", ((u8 *) &ccb[mbo])[i]);
	};
#endif

	if (done) {
		DEB(printk("aha1542_queuecommand: now waiting for interrupt "));
		cmd->scsi_done = done;
		mb[mbo].status = 1;
		aha1542_outb(cmd->device->host->io_port, CMD_START_SCSI);
	} else
		printk("aha1542_queuecommand: done can't be NULL\n");

	return 0;
}

static DEF_SCSI_QCMD(aha1542_queuecommand)

/* Initialize mailboxes */
static void setup_mailboxes(int bse, struct Scsi_Host *shpnt)
{
	struct aha1542_hostdata *aha1542 = shost_priv(shpnt);
	int i;
	struct mailbox *mb = aha1542->mb;
	struct ccb *ccb = aha1542->ccb;

	u8 mb_cmd[5] = { CMD_MBINIT, AHA1542_MAILBOXES, 0, 0, 0};

	for (i = 0; i < AHA1542_MAILBOXES; i++) {
		mb[i].status = mb[AHA1542_MAILBOXES + i].status = 0;
		any2scsi(mb[i].ccbptr, isa_virt_to_bus(&ccb[i]));
	};
	aha1542_intr_reset(bse);	/* reset interrupts, so they don't block */
	any2scsi((mb_cmd + 2), isa_virt_to_bus(mb));
	if (aha1542_out(bse, mb_cmd, 5))
		printk(KERN_ERR "aha1542_detect: failed setting up mailboxes\n");
	aha1542_intr_reset(bse);
}

static int aha1542_getconfig(int base_io, unsigned int *irq_level, unsigned char *dma_chan, unsigned int *scsi_id)
{
	u8 inquiry_result[3];
	int i;
	i = inb(STATUS(base_io));
	if (i & DF) {
		i = inb(DATA(base_io));
	};
	aha1542_outb(base_io, CMD_RETCONF);
	aha1542_in(base_io, inquiry_result, 3, 0);
	if (!wait_mask(INTRFLAGS(base_io), INTRMASK, HACC, 0, 0))
		printk(KERN_ERR "aha1542_detect: query board settings\n");
	aha1542_intr_reset(base_io);
	switch (inquiry_result[0]) {
	case 0x80:
		*dma_chan = 7;
		break;
	case 0x40:
		*dma_chan = 6;
		break;
	case 0x20:
		*dma_chan = 5;
		break;
	case 0x01:
		*dma_chan = 0;
		break;
	case 0:
		/* This means that the adapter, although Adaptec 1542 compatible, doesn't use a DMA channel.
		   Currently only aware of the BusLogic BT-445S VL-Bus adapter which needs this. */
		*dma_chan = 0xFF;
		break;
	default:
		printk(KERN_ERR "Unable to determine Adaptec DMA priority.  Disabling board\n");
		return -1;
	};
	switch (inquiry_result[1]) {
	case 0x40:
		*irq_level = 15;
		break;
	case 0x20:
		*irq_level = 14;
		break;
	case 0x8:
		*irq_level = 12;
		break;
	case 0x4:
		*irq_level = 11;
		break;
	case 0x2:
		*irq_level = 10;
		break;
	case 0x1:
		*irq_level = 9;
		break;
	default:
		printk(KERN_ERR "Unable to determine Adaptec IRQ level.  Disabling board\n");
		return -1;
	};
	*scsi_id = inquiry_result[2] & 7;
	return 0;
}

/* This function should only be called for 1542C boards - we can detect
   the special firmware settings and unlock the board */

static int aha1542_mbenable(int base)
{
	static u8 mbenable_cmd[3];
	static u8 mbenable_result[2];
	int retval;

	retval = BIOS_TRANSLATION_6432;

	aha1542_outb(base, CMD_EXTBIOS);
	if (aha1542_in(base, mbenable_result, 2, 100))
		return retval;
	if (!wait_mask(INTRFLAGS(base), INTRMASK, HACC, 0, 100))
		goto fail;
	aha1542_intr_reset(base);

	if ((mbenable_result[0] & 0x08) || mbenable_result[1]) {
		mbenable_cmd[0] = CMD_MBENABLE;
		mbenable_cmd[1] = 0;
		mbenable_cmd[2] = mbenable_result[1];

		if ((mbenable_result[0] & 0x08) && (mbenable_result[1] & 0x03))
			retval = BIOS_TRANSLATION_25563;

		if (aha1542_out(base, mbenable_cmd, 3))
			goto fail;
	};
	while (0) {
fail:
		printk(KERN_ERR "aha1542_mbenable: Mailbox init failed\n");
	}
	aha1542_intr_reset(base);
	return retval;
}

/* Query the board to find out if it is a 1542 or a 1740, or whatever. */
static int aha1542_query(int base_io, int *transl)
{
	u8 inquiry_result[4];
	int i;
	i = inb(STATUS(base_io));
	if (i & DF) {
		i = inb(DATA(base_io));
	};
	aha1542_outb(base_io, CMD_INQUIRY);
	aha1542_in(base_io, inquiry_result, 4, 0);
	if (!wait_mask(INTRFLAGS(base_io), INTRMASK, HACC, 0, 0))
		printk(KERN_ERR "aha1542_detect: query card type\n");
	aha1542_intr_reset(base_io);

	*transl = BIOS_TRANSLATION_6432;	/* Default case */

	/* For an AHA1740 series board, we ignore the board since there is a
	   hardware bug which can lead to wrong blocks being returned if the board
	   is operating in the 1542 emulation mode.  Since there is an extended mode
	   driver, we simply ignore the board and let the 1740 driver pick it up.
	 */

	if (inquiry_result[0] == 0x43) {
		printk(KERN_INFO "aha1542.c: Emulation mode not supported for AHA 174N hardware.\n");
		return 1;
	};

	/* Always call this - boards that do not support extended bios translation
	   will ignore the command, and we will set the proper default */

	*transl = aha1542_mbenable(base_io);

	return 0;
}

static u8 dma_speed_hw(int dma_speed)
{
	switch (dma_speed) {
	case 5:
		return 0x00;
	case 6:
		return 0x04;
	case 7:
		return 0x01;
	case 8:
		return 0x02;
	case 10:
		return 0x03;
	}

	return 0xff;	/* invalid */
}

/* Set the Bus on/off-times as not to ruin floppy performance */
static void aha1542_set_bus_times(int indx)
{
	unsigned int base_io = io[indx];

	if (bus_on[indx] > 0) {
		u8 oncmd[] = { CMD_BUSON_TIME, clamp(bus_on[indx], 2, 15) };

		aha1542_intr_reset(base_io);
		if (aha1542_out(base_io, oncmd, 2))
			goto fail;
	}

	if (bus_off[indx] > 0) {
		u8 offcmd[] = { CMD_BUSOFF_TIME, clamp(bus_off[indx], 1, 64) };

		aha1542_intr_reset(base_io);
		if (aha1542_out(base_io, offcmd, 2))
			goto fail;
	}

	if (dma_speed_hw(dma_speed[indx]) != 0xff) {
		u8 dmacmd[] = { CMD_DMASPEED, dma_speed_hw(dma_speed[indx]) };

		aha1542_intr_reset(base_io);
		if (aha1542_out(base_io, dmacmd, 2))
			goto fail;
	}
	aha1542_intr_reset(base_io);
	return;
fail:
	printk(KERN_ERR "setting bus on/off-time failed\n");
	aha1542_intr_reset(base_io);
}

/* return non-zero on detection */
static struct Scsi_Host *aha1542_hw_init(struct scsi_host_template *tpnt, struct device *pdev, int indx)
{
	unsigned int base_io = io[indx];
	struct Scsi_Host *shpnt;
	struct aha1542_hostdata *aha1542;

	if (base_io == 0)
		return NULL;

	if (!request_region(base_io, AHA1542_REGION_SIZE, "aha1542"))
		return NULL;

	shpnt = scsi_host_alloc(tpnt, sizeof(struct aha1542_hostdata));
	if (!shpnt)
		goto release;
	aha1542 = shost_priv(shpnt);

	if (!aha1542_test_port(base_io, shpnt))
		goto unregister;

	aha1542_set_bus_times(indx);
	if (aha1542_query(base_io, &aha1542->bios_translation))
		goto unregister;
	if (aha1542_getconfig(base_io, &shpnt->irq, &shpnt->dma_channel, &shpnt->this_id) == -1)
		goto unregister;

	printk(KERN_INFO "Adaptec AHA-1542 (SCSI-ID %d) at IO 0x%x, IRQ %d", shpnt->this_id, base_io, shpnt->irq);
	if (shpnt->dma_channel != 0xFF)
		printk(", DMA %d", shpnt->dma_channel);
	printk("\n");
	if (aha1542->bios_translation == BIOS_TRANSLATION_25563)
		printk(KERN_INFO "aha1542.c: Using extended bios translation\n");

	setup_mailboxes(base_io, shpnt);

	if (request_irq(shpnt->irq, do_aha1542_intr_handle, 0,
					"aha1542", shpnt)) {
		printk(KERN_ERR "Unable to allocate IRQ for adaptec controller.\n");
		goto unregister;
	}
	if (shpnt->dma_channel != 0xFF) {
		if (request_dma(shpnt->dma_channel, "aha1542")) {
			printk(KERN_ERR "Unable to allocate DMA channel for Adaptec.\n");
			goto free_irq;
		}
		if (shpnt->dma_channel == 0 || shpnt->dma_channel >= 5) {
			set_dma_mode(shpnt->dma_channel, DMA_MODE_CASCADE);
			enable_dma(shpnt->dma_channel);
		}
	}

	shpnt->unique_id = base_io;
	shpnt->io_port = base_io;
	shpnt->n_io_port = AHA1542_REGION_SIZE;
	aha1542->aha1542_last_mbi_used = 2 * AHA1542_MAILBOXES - 1;
	aha1542->aha1542_last_mbo_used = AHA1542_MAILBOXES - 1;

	if (scsi_add_host(shpnt, pdev))
		goto free_dma;

	scsi_scan_host(shpnt);

	return shpnt;
free_dma:
	if (shpnt->dma_channel != 0xff)
		free_dma(shpnt->dma_channel);
free_irq:
	free_irq(shpnt->irq, shpnt);
unregister:
	scsi_host_put(shpnt);
release:
	release_region(base_io, AHA1542_REGION_SIZE);

	return NULL;
}

static int aha1542_release(struct Scsi_Host *shost)
{
	scsi_remove_host(shost);
	if (shost->dma_channel != 0xff)
		free_dma(shost->dma_channel);
	if (shost->irq)
		free_irq(shost->irq, shost);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	scsi_host_put(shost);
	return 0;
}


/*
 * This is a device reset.  This is handled by sending a special command
 * to the device.
 */
static int aha1542_dev_reset(struct scsi_cmnd *cmd)
{
	struct aha1542_hostdata *aha1542 = shost_priv(cmd->device->host);
	unsigned long flags;
	struct mailbox *mb = aha1542->mb;
	u8 target = cmd->device->id;
	u8 lun = cmd->device->lun;
	int mbo;
	struct ccb *ccb = aha1542->ccb;

	spin_lock_irqsave(&aha1542_lock, flags);
	mbo = aha1542->aha1542_last_mbo_used + 1;
	if (mbo >= AHA1542_MAILBOXES)
		mbo = 0;

	do {
		if (mb[mbo].status == 0 && aha1542->int_cmds[mbo] == NULL)
			break;
		mbo++;
		if (mbo >= AHA1542_MAILBOXES)
			mbo = 0;
	} while (mbo != aha1542->aha1542_last_mbo_used);

	if (mb[mbo].status || aha1542->int_cmds[mbo])
		panic("Unable to find empty mailbox for aha1542.\n");

	aha1542->int_cmds[mbo] = cmd;	/* This will effectively
					   prevent someone else from
					   screwing with this cdb. */

	aha1542->aha1542_last_mbo_used = mbo;
	spin_unlock_irqrestore(&aha1542_lock, flags);

	any2scsi(mb[mbo].ccbptr, isa_virt_to_bus(&ccb[mbo]));	/* This gets trashed for some reason */

	memset(&ccb[mbo], 0, sizeof(struct ccb));

	ccb[mbo].op = 0x81;	/* BUS DEVICE RESET */

	ccb[mbo].idlun = (target & 7) << 5 | (lun & 7);		/*SCSI Target Id */

	ccb[mbo].linkptr[0] = ccb[mbo].linkptr[1] = ccb[mbo].linkptr[2] = 0;
	ccb[mbo].commlinkid = 0;

	/* 
	 * Now tell the 1542 to flush all pending commands for this 
	 * target 
	 */
	aha1542_outb(cmd->device->host->io_port, CMD_START_SCSI);

	scmd_printk(KERN_WARNING, cmd,
		"Trying device reset for target\n");

	return SUCCESS;
}

static int aha1542_reset(struct scsi_cmnd *cmd, u8 reset_cmd)
{
	struct aha1542_hostdata *aha1542 = shost_priv(cmd->device->host);
	int i;

	/* 
	 * This does a scsi reset for all devices on the bus.
	 * In principle, we could also reset the 1542 - should
	 * we do this?  Try this first, and we can add that later
	 * if it turns out to be useful.
	 */
	outb(reset_cmd, CONTROL(cmd->device->host->io_port));

	/*
	 * Wait for the thing to settle down a bit.  Unfortunately
	 * this is going to basically lock up the machine while we
	 * wait for this to complete.  To be 100% correct, we need to
	 * check for timeout, and if we are doing something like this
	 * we are pretty desperate anyways.
	 */
	ssleep(4);
	spin_lock_irq(cmd->device->host->host_lock);

	if (!wait_mask(STATUS(cmd->device->host->io_port),
	     STATMASK, INIT | IDLE, STST | DIAGF | INVDCMD | DF | CDF, 0)) {
		spin_unlock_irq(cmd->device->host->host_lock);
		return FAILED;
	}
	/*
	 * We need to do this too before the 1542 can interact with
	 * us again after host reset.
	 */
	if (reset_cmd & HRST)
		setup_mailboxes(cmd->device->host->io_port, cmd->device->host);
	/*
	 * Now try to pick up the pieces.  For all pending commands,
	 * free any internal data structures, and basically clear things
	 * out.  We do not try and restart any commands or anything - 
	 * the strategy handler takes care of that crap.
	 */
	printk(KERN_WARNING "Sent BUS RESET to scsi host %d\n", cmd->device->host->host_no);

	for (i = 0; i < AHA1542_MAILBOXES; i++) {
		if (aha1542->int_cmds[i] != NULL) {
			struct scsi_cmnd *tmp_cmd;
			tmp_cmd = aha1542->int_cmds[i];

			if (tmp_cmd->device->soft_reset) {
				/*
				 * If this device implements the soft reset option,
				 * then it is still holding onto the command, and
				 * may yet complete it.  In this case, we don't
				 * flush the data.
				 */
				continue;
			}
			kfree(tmp_cmd->host_scribble);
			tmp_cmd->host_scribble = NULL;
			aha1542->int_cmds[i] = NULL;
			aha1542->mb[i].status = 0;
		}
	}

	spin_unlock_irq(cmd->device->host->host_lock);
	return SUCCESS;
}

static int aha1542_bus_reset(struct scsi_cmnd *cmd)
{
	return aha1542_reset(cmd, SCRST);
}

static int aha1542_host_reset(struct scsi_cmnd *cmd)
{
	return aha1542_reset(cmd, HRST | SCRST);
}

static int aha1542_biosparam(struct scsi_device *sdev,
		struct block_device *bdev, sector_t capacity, int geom[])
{
	struct aha1542_hostdata *aha1542 = shost_priv(sdev->host);

	if (capacity >= 0x200000 &&
			aha1542->bios_translation == BIOS_TRANSLATION_25563) {
		/* Please verify that this is the same as what DOS returns */
		geom[0] = 255;	/* heads */
		geom[1] = 63;	/* sectors */
	} else {
		geom[0] = 64;	/* heads */
		geom[1] = 32;	/* sectors */
	}
	geom[2] = sector_div(capacity, geom[0] * geom[1]);	/* cylinders */

	return 0;
}
MODULE_LICENSE("GPL");

static struct scsi_host_template driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= "aha1542",
	.name			= "Adaptec 1542",
	.queuecommand		= aha1542_queuecommand,
	.eh_device_reset_handler= aha1542_dev_reset,
	.eh_bus_reset_handler	= aha1542_bus_reset,
	.eh_host_reset_handler	= aha1542_host_reset,
	.bios_param		= aha1542_biosparam,
	.can_queue		= AHA1542_MAILBOXES, 
	.this_id		= 7,
	.sg_tablesize		= 16,
	.cmd_per_lun		= 1,
	.unchecked_isa_dma	= 1, 
	.use_clustering		= ENABLE_CLUSTERING,
};

static int aha1542_isa_match(struct device *pdev, unsigned int ndev)
{
	struct Scsi_Host *sh = aha1542_hw_init(&driver_template, pdev, ndev);

	if (!sh)
		return 0;

	dev_set_drvdata(pdev, sh);
	return 1;
}

static int aha1542_isa_remove(struct device *pdev,
				    unsigned int ndev)
{
	aha1542_release(dev_get_drvdata(pdev));
	dev_set_drvdata(pdev, NULL);
	return 0;
}

static struct isa_driver aha1542_isa_driver = {
	.match		= aha1542_isa_match,
	.remove		= aha1542_isa_remove,
	.driver		= {
		.name	= "aha1542"
	},
};
static int isa_registered;

#ifdef CONFIG_PNP
static struct pnp_device_id aha1542_pnp_ids[] = {
	{ .id = "ADP1542" },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, aha1542_pnp_ids);

static int aha1542_pnp_probe(struct pnp_dev *pdev, const struct pnp_device_id *id)
{
	int indx;
	struct Scsi_Host *sh;

	for (indx = 0; indx < ARRAY_SIZE(io); indx++) {
		if (io[indx])
			continue;

		if (pnp_activate_dev(pdev) < 0)
			continue;

		io[indx] = pnp_port_start(pdev, 0);

		/* The card can be queried for its DMA, we have
		   the DMA set up that is enough */

		printk(KERN_INFO "ISAPnP found an AHA1535 at I/O 0x%03X\n", io[indx]);
	}

	sh = aha1542_hw_init(&driver_template, &pdev->dev, indx);
	if (!sh)
		return -ENODEV;

	pnp_set_drvdata(pdev, sh);
	return 0;
}

static void aha1542_pnp_remove(struct pnp_dev *pdev)
{
	aha1542_release(pnp_get_drvdata(pdev));
	pnp_set_drvdata(pdev, NULL);
}

static struct pnp_driver aha1542_pnp_driver = {
	.name		= "aha1542",
	.id_table	= aha1542_pnp_ids,
	.probe		= aha1542_pnp_probe,
	.remove		= aha1542_pnp_remove,
};
static int pnp_registered;
#endif /* CONFIG_PNP */

static int __init aha1542_init(void)
{
	int ret = 0;

#ifdef CONFIG_PNP
	if (isapnp) {
		ret = pnp_register_driver(&aha1542_pnp_driver);
		if (!ret)
			pnp_registered = 1;
	}
#endif
	ret = isa_register_driver(&aha1542_isa_driver, MAXBOARDS);
	if (!ret)
		isa_registered = 1;

#ifdef CONFIG_PNP
	if (pnp_registered)
		ret = 0;
#endif
	if (isa_registered)
		ret = 0;

	return ret;
}

static void __exit aha1542_exit(void)
{
#ifdef CONFIG_PNP
	if (pnp_registered)
		pnp_unregister_driver(&aha1542_pnp_driver);
#endif
	if (isa_registered)
		isa_unregister_driver(&aha1542_isa_driver);
}

module_init(aha1542_init);
module_exit(aha1542_exit);
