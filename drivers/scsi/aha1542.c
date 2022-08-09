// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for Adaptec AHA-1542 SCSI host adapters
 *
 *  Copyright (C) 1992  Tommy Thorn
 *  Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *  Copyright (C) 2015 Ondrej Zary
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/isa.h>
#include <linux/pnp.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <asm/dma.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include "aha1542.h"

#define MAXBOARDS 4

static bool isapnp = 1;
module_param(isapnp, bool, 0);
MODULE_PARM_DESC(isapnp, "enable PnP support (default=1)");

static int io[MAXBOARDS] = { 0x330, 0x334, 0, 0 };
module_param_hw_array(io, int, ioport, NULL, 0);
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
	struct mailbox *mb;
	dma_addr_t mb_handle;
	struct ccb *ccb;
	dma_addr_t ccb_handle;
};

#define AHA1542_MAX_SECTORS       16

struct aha1542_cmd {
	/* bounce buffer */
	void *data_buffer;
	dma_addr_t data_buffer_handle;
};

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

static int aha1542_outb(unsigned int base, u8 val)
{
	if (!wait_mask(STATUS(base), CDF, 0, CDF, 0))
		return 1;
	outb(val, DATA(base));

	return 0;
}

static int aha1542_out(unsigned int base, u8 *buf, int len)
{
	while (len--) {
		if (!wait_mask(STATUS(base), CDF, 0, CDF, 0))
			return 1;
		outb(*buf++, DATA(base));
	}
	if (!wait_mask(INTRFLAGS(base), INTRMASK, HACC, 0, 0))
		return 1;

	return 0;
}

/*
 * Only used at boot time, so we do not need to worry about latency as much
 * here
 */

static int aha1542_in(unsigned int base, u8 *buf, int len, int timeout)
{
	while (len--) {
		if (!wait_mask(STATUS(base), DF, DF, 0, timeout))
			return 1;
		*buf++ = inb(DATA(base));
	}
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
				 * reselection was not complete within the SCSI Time out period
				 */
		hosterr = DID_TIME_OUT;
		break;

	case 0x12:		/* Data overrun/underrun-The target attempted to transfer more data
				 * than was allocated by the Data Length field or the sum of the
				 * Scatter / Gather Data Length fields.
				 */

	case 0x13:		/* Unexpected bus free-The target dropped the SCSI BSY at an unexpected time. */

	case 0x15:		/* MBO command was not 00, 01 or 02-The first byte of the CB was
				 * invalid. This usually indicates a software failure.
				 */

	case 0x16:		/* Invalid CCB Operation Code-The first byte of the CCB was invalid.
				 * This usually indicates a software failure.
				 */

	case 0x17:		/* Linked CCB does not have the same LUN-A subsequent CCB of a set
				 * of linked CCB's does not specify the same logical unit number as
				 * the first.
				 */
	case 0x18:		/* Invalid Target Direction received from Host-The direction of a
				 * Target Mode CCB was invalid.
				 */

	case 0x19:		/* Duplicate CCB Received in Target Mode-More than once CCB was
				 * received to service data transfer between the same target LUN
				 * and initiator SCSI ID in the same direction.
				 */

	case 0x1a:		/* Invalid CCB or Segment List Parameter-A segment list with a zero
				 * length segment or invalid segment list boundaries was received.
				 * A CCB parameter was invalid.
				 */
#ifdef DEBUG
		printk("Aha1542: %x %x\n", hosterr, scsierr);
#endif
		hosterr = DID_ERROR;	/* Couldn't find any better */
		break;

	case 0x14:		/* Target bus phase sequence failure-An invalid bus phase or bus
				 * phase sequence was requested by the target. The host adapter
				 * will generate a SCSI Reset Condition, notifying the host with
				 * a SCRD interrupt
				 */
		hosterr = DID_RESET;
		break;
	default:
		printk(KERN_ERR "aha1542: makecode: unknown hoststatus %x\n", hosterr);
		break;
	}
	return scsierr | (hosterr << 16);
}

static int aha1542_test_port(struct Scsi_Host *sh)
{
	int i;

	/* Quick and dirty test for presence of the card. */
	if (inb(STATUS(sh->io_port)) == 0xff)
		return 0;

	/* Reset the adapter. I ought to make a hard reset, but it's not really necessary */

	/* In case some other card was probing here, reset interrupts */
	aha1542_intr_reset(sh->io_port);	/* reset interrupts, so they don't block */

	outb(SRST | IRST /*|SCRST */ , CONTROL(sh->io_port));

	mdelay(20);		/* Wait a little bit for things to settle down. */

	/* Expect INIT and IDLE, any of the others are bad */
	if (!wait_mask(STATUS(sh->io_port), STATMASK, INIT | IDLE, STST | DIAGF | INVDCMD | DF | CDF, 0))
		return 0;

	/* Shouldn't have generated any interrupts during reset */
	if (inb(INTRFLAGS(sh->io_port)) & INTRMASK)
		return 0;

	/*
	 * Perform a host adapter inquiry instead so we do not need to set
	 * up the mailboxes ahead of time
	 */

	aha1542_outb(sh->io_port, CMD_INQUIRY);

	for (i = 0; i < 4; i++) {
		if (!wait_mask(STATUS(sh->io_port), DF, DF, 0, 0))
			return 0;
		(void)inb(DATA(sh->io_port));
	}

	/* Reading port should reset DF */
	if (inb(STATUS(sh->io_port)) & DF)
		return 0;

	/* When HACC, command is completed, and we're though testing */
	if (!wait_mask(INTRFLAGS(sh->io_port), HACC, HACC, 0, 0))
		return 0;

	/* Clear interrupts */
	outb(IRST, CONTROL(sh->io_port));

	return 1;
}

static void aha1542_free_cmd(struct scsi_cmnd *cmd)
{
	struct aha1542_cmd *acmd = scsi_cmd_priv(cmd);

	if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		struct request *rq = scsi_cmd_to_rq(cmd);
		void *buf = acmd->data_buffer;
		struct req_iterator iter;
		struct bio_vec bv;

		rq_for_each_segment(bv, rq, iter) {
			memcpy_to_bvec(&bv, buf);
			buf += bv.bv_len;
		}
	}

	scsi_dma_unmap(cmd);
}

static irqreturn_t aha1542_interrupt(int irq, void *dev_id)
{
	struct Scsi_Host *sh = dev_id;
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	int errstatus, mbi, mbo, mbistatus;
	int number_serviced;
	unsigned long flags;
	struct scsi_cmnd *tmp_cmd;
	int flag;
	struct mailbox *mb = aha1542->mb;
	struct ccb *ccb = aha1542->ccb;

#ifdef DEBUG
	{
		flag = inb(INTRFLAGS(sh->io_port));
		shost_printk(KERN_DEBUG, sh, "aha1542_intr_handle: ");
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
		printk("status %02x\n", inb(STATUS(sh->io_port)));
	};
#endif
	number_serviced = 0;

	spin_lock_irqsave(sh->host_lock, flags);
	while (1) {
		flag = inb(INTRFLAGS(sh->io_port));

		/*
		 * Check for unusual interrupts.  If any of these happen, we should
		 * probably do something special, but for now just printing a message
		 * is sufficient.  A SCSI reset detected is something that we really
		 * need to deal with in some way.
		 */
		if (flag & ~MBIF) {
			if (flag & MBOA)
				printk("MBOF ");
			if (flag & HACC)
				printk("HACC ");
			if (flag & SCRD)
				printk("SCRD ");
		}
		aha1542_intr_reset(sh->io_port);

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
			spin_unlock_irqrestore(sh->host_lock, flags);
			/* Hmm, no mail.  Must have read it the last time around */
			if (!number_serviced)
				shost_printk(KERN_WARNING, sh, "interrupt received, but no mail.\n");
			return IRQ_HANDLED;
		};

		mbo = (scsi2int(mb[mbi].ccbptr) - (unsigned long)aha1542->ccb_handle) / sizeof(struct ccb);
		mbistatus = mb[mbi].status;
		mb[mbi].status = 0;
		aha1542->aha1542_last_mbi_used = mbi;

#ifdef DEBUG
		if (ccb[mbo].tarstat | ccb[mbo].hastat)
			shost_printk(KERN_DEBUG, sh, "aha1542_command: returning %x (status %d)\n",
			       ccb[mbo].tarstat + ((int) ccb[mbo].hastat << 16), mb[mbi].status);
#endif

		if (mbistatus == 3)
			continue;	/* Aborted command not found */

#ifdef DEBUG
		shost_printk(KERN_DEBUG, sh, "...done %d %d\n", mbo, mbi);
#endif

		tmp_cmd = aha1542->int_cmds[mbo];

		if (!tmp_cmd) {
			spin_unlock_irqrestore(sh->host_lock, flags);
			shost_printk(KERN_WARNING, sh, "Unexpected interrupt\n");
			shost_printk(KERN_WARNING, sh, "tarstat=%x, hastat=%x idlun=%x ccb#=%d\n", ccb[mbo].tarstat,
			       ccb[mbo].hastat, ccb[mbo].idlun, mbo);
			return IRQ_HANDLED;
		}
		aha1542_free_cmd(tmp_cmd);
		/*
		 * Fetch the sense data, and tuck it away, in the required slot.  The
		 * Adaptec automatically fetches it, and there is no guarantee that
		 * we will still have it in the cdb when we come back
		 */
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
			shost_printk(KERN_DEBUG, sh, "(aha1542 error:%x %x %x) ", errstatus,
			       ccb[mbo].hastat, ccb[mbo].tarstat);
		if (ccb[mbo].tarstat == 2)
			print_hex_dump_bytes("sense: ", DUMP_PREFIX_NONE, &ccb[mbo].cdb[ccb[mbo].cdblen], 12);
		if (errstatus)
			printk("aha1542_intr_handle: returning %6x\n", errstatus);
#endif
		tmp_cmd->result = errstatus;
		aha1542->int_cmds[mbo] = NULL;	/* This effectively frees up the mailbox slot, as
						 * far as queuecommand is concerned
						 */
		scsi_done(tmp_cmd);
		number_serviced++;
	};
}

static int aha1542_queuecommand(struct Scsi_Host *sh, struct scsi_cmnd *cmd)
{
	struct aha1542_cmd *acmd = scsi_cmd_priv(cmd);
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	u8 direction;
	u8 target = cmd->device->id;
	u8 lun = cmd->device->lun;
	unsigned long flags;
	int bufflen = scsi_bufflen(cmd);
	int mbo;
	struct mailbox *mb = aha1542->mb;
	struct ccb *ccb = aha1542->ccb;

	if (*cmd->cmnd == REQUEST_SENSE) {
		/* Don't do the command - we have the sense data already */
		cmd->result = 0;
		scsi_done(cmd);
		return 0;
	}
#ifdef DEBUG
	{
		int i = -1;
		if (*cmd->cmnd == READ_10 || *cmd->cmnd == WRITE_10)
			i = xscsi2int(cmd->cmnd + 2);
		else if (*cmd->cmnd == READ_6 || *cmd->cmnd == WRITE_6)
			i = scsi2int(cmd->cmnd + 2);
		shost_printk(KERN_DEBUG, sh, "aha1542_queuecommand: dev %d cmd %02x pos %d len %d",
						target, *cmd->cmnd, i, bufflen);
		print_hex_dump_bytes("command: ", DUMP_PREFIX_NONE, cmd->cmnd, cmd->cmd_len);
	}
#endif

	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		struct request *rq = scsi_cmd_to_rq(cmd);
		void *buf = acmd->data_buffer;
		struct req_iterator iter;
		struct bio_vec bv;

		rq_for_each_segment(bv, rq, iter) {
			memcpy_from_bvec(buf, &bv);
			buf += bv.bv_len;
		}
	}

	/*
	 * Use the outgoing mailboxes in a round-robin fashion, because this
	 * is how the host adapter will scan for them
	 */

	spin_lock_irqsave(sh->host_lock, flags);
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
					 * screwing with this cdb.
					 */

	aha1542->aha1542_last_mbo_used = mbo;

#ifdef DEBUG
	shost_printk(KERN_DEBUG, sh, "Sending command (%d)...", mbo);
#endif

	/* This gets trashed for some reason */
	any2scsi(mb[mbo].ccbptr, aha1542->ccb_handle + mbo * sizeof(*ccb));

	memset(&ccb[mbo], 0, sizeof(struct ccb));

	ccb[mbo].cdblen = cmd->cmd_len;

	direction = 0;
	if (*cmd->cmnd == READ_10 || *cmd->cmnd == READ_6)
		direction = 8;
	else if (*cmd->cmnd == WRITE_10 || *cmd->cmnd == WRITE_6)
		direction = 16;

	memcpy(ccb[mbo].cdb, cmd->cmnd, ccb[mbo].cdblen);
	ccb[mbo].op = 0;	/* SCSI Initiator Command */
	any2scsi(ccb[mbo].datalen, bufflen);
	if (bufflen)
		any2scsi(ccb[mbo].dataptr, acmd->data_buffer_handle);
	else
		any2scsi(ccb[mbo].dataptr, 0);
	ccb[mbo].idlun = (target & 7) << 5 | direction | (lun & 7);	/*SCSI Target Id */
	ccb[mbo].rsalen = 16;
	ccb[mbo].linkptr[0] = ccb[mbo].linkptr[1] = ccb[mbo].linkptr[2] = 0;
	ccb[mbo].commlinkid = 0;

#ifdef DEBUG
	print_hex_dump_bytes("sending: ", DUMP_PREFIX_NONE, &ccb[mbo], sizeof(ccb[mbo]) - 10);
	printk("aha1542_queuecommand: now waiting for interrupt ");
#endif
	mb[mbo].status = 1;
	aha1542_outb(cmd->device->host->io_port, CMD_START_SCSI);
	spin_unlock_irqrestore(sh->host_lock, flags);

	return 0;
}

/* Initialize mailboxes */
static void setup_mailboxes(struct Scsi_Host *sh)
{
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	u8 mb_cmd[5] = { CMD_MBINIT, AHA1542_MAILBOXES, 0, 0, 0};
	int i;

	for (i = 0; i < AHA1542_MAILBOXES; i++) {
		aha1542->mb[i].status = 0;
		any2scsi(aha1542->mb[i].ccbptr,
			 aha1542->ccb_handle + i * sizeof(struct ccb));
		aha1542->mb[AHA1542_MAILBOXES + i].status = 0;
	};
	aha1542_intr_reset(sh->io_port);	/* reset interrupts, so they don't block */
	any2scsi(mb_cmd + 2, aha1542->mb_handle);
	if (aha1542_out(sh->io_port, mb_cmd, 5))
		shost_printk(KERN_ERR, sh, "failed setting up mailboxes\n");
	aha1542_intr_reset(sh->io_port);
}

static int aha1542_getconfig(struct Scsi_Host *sh)
{
	u8 inquiry_result[3];
	int i;
	i = inb(STATUS(sh->io_port));
	if (i & DF) {
		i = inb(DATA(sh->io_port));
	};
	aha1542_outb(sh->io_port, CMD_RETCONF);
	aha1542_in(sh->io_port, inquiry_result, 3, 0);
	if (!wait_mask(INTRFLAGS(sh->io_port), INTRMASK, HACC, 0, 0))
		shost_printk(KERN_ERR, sh, "error querying board settings\n");
	aha1542_intr_reset(sh->io_port);
	switch (inquiry_result[0]) {
	case 0x80:
		sh->dma_channel = 7;
		break;
	case 0x40:
		sh->dma_channel = 6;
		break;
	case 0x20:
		sh->dma_channel = 5;
		break;
	case 0x01:
		sh->dma_channel = 0;
		break;
	case 0:
		/*
		 * This means that the adapter, although Adaptec 1542 compatible, doesn't use a DMA channel.
		 * Currently only aware of the BusLogic BT-445S VL-Bus adapter which needs this.
		 */
		sh->dma_channel = 0xFF;
		break;
	default:
		shost_printk(KERN_ERR, sh, "Unable to determine DMA channel.\n");
		return -1;
	};
	switch (inquiry_result[1]) {
	case 0x40:
		sh->irq = 15;
		break;
	case 0x20:
		sh->irq = 14;
		break;
	case 0x8:
		sh->irq = 12;
		break;
	case 0x4:
		sh->irq = 11;
		break;
	case 0x2:
		sh->irq = 10;
		break;
	case 0x1:
		sh->irq = 9;
		break;
	default:
		shost_printk(KERN_ERR, sh, "Unable to determine IRQ level.\n");
		return -1;
	};
	sh->this_id = inquiry_result[2] & 7;
	return 0;
}

/*
 * This function should only be called for 1542C boards - we can detect
 * the special firmware settings and unlock the board
 */

static int aha1542_mbenable(struct Scsi_Host *sh)
{
	static u8 mbenable_cmd[3];
	static u8 mbenable_result[2];
	int retval;

	retval = BIOS_TRANSLATION_6432;

	aha1542_outb(sh->io_port, CMD_EXTBIOS);
	if (aha1542_in(sh->io_port, mbenable_result, 2, 100))
		return retval;
	if (!wait_mask(INTRFLAGS(sh->io_port), INTRMASK, HACC, 0, 100))
		goto fail;
	aha1542_intr_reset(sh->io_port);

	if ((mbenable_result[0] & 0x08) || mbenable_result[1]) {
		mbenable_cmd[0] = CMD_MBENABLE;
		mbenable_cmd[1] = 0;
		mbenable_cmd[2] = mbenable_result[1];

		if ((mbenable_result[0] & 0x08) && (mbenable_result[1] & 0x03))
			retval = BIOS_TRANSLATION_25563;

		if (aha1542_out(sh->io_port, mbenable_cmd, 3))
			goto fail;
	};
	while (0) {
fail:
		shost_printk(KERN_ERR, sh, "Mailbox init failed\n");
	}
	aha1542_intr_reset(sh->io_port);
	return retval;
}

/* Query the board to find out if it is a 1542 or a 1740, or whatever. */
static int aha1542_query(struct Scsi_Host *sh)
{
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	u8 inquiry_result[4];
	int i;
	i = inb(STATUS(sh->io_port));
	if (i & DF) {
		i = inb(DATA(sh->io_port));
	};
	aha1542_outb(sh->io_port, CMD_INQUIRY);
	aha1542_in(sh->io_port, inquiry_result, 4, 0);
	if (!wait_mask(INTRFLAGS(sh->io_port), INTRMASK, HACC, 0, 0))
		shost_printk(KERN_ERR, sh, "error querying card type\n");
	aha1542_intr_reset(sh->io_port);

	aha1542->bios_translation = BIOS_TRANSLATION_6432;	/* Default case */

	/*
	 * For an AHA1740 series board, we ignore the board since there is a
	 * hardware bug which can lead to wrong blocks being returned if the board
	 * is operating in the 1542 emulation mode.  Since there is an extended mode
	 * driver, we simply ignore the board and let the 1740 driver pick it up.
	 */

	if (inquiry_result[0] == 0x43) {
		shost_printk(KERN_INFO, sh, "Emulation mode not supported for AHA-1740 hardware, use aha1740 driver instead.\n");
		return 1;
	};

	/*
	 * Always call this - boards that do not support extended bios translation
	 * will ignore the command, and we will set the proper default
	 */

	aha1542->bios_translation = aha1542_mbenable(sh);

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
static void aha1542_set_bus_times(struct Scsi_Host *sh, int bus_on, int bus_off, int dma_speed)
{
	if (bus_on > 0) {
		u8 oncmd[] = { CMD_BUSON_TIME, clamp(bus_on, 2, 15) };

		aha1542_intr_reset(sh->io_port);
		if (aha1542_out(sh->io_port, oncmd, 2))
			goto fail;
	}

	if (bus_off > 0) {
		u8 offcmd[] = { CMD_BUSOFF_TIME, clamp(bus_off, 1, 64) };

		aha1542_intr_reset(sh->io_port);
		if (aha1542_out(sh->io_port, offcmd, 2))
			goto fail;
	}

	if (dma_speed_hw(dma_speed) != 0xff) {
		u8 dmacmd[] = { CMD_DMASPEED, dma_speed_hw(dma_speed) };

		aha1542_intr_reset(sh->io_port);
		if (aha1542_out(sh->io_port, dmacmd, 2))
			goto fail;
	}
	aha1542_intr_reset(sh->io_port);
	return;
fail:
	shost_printk(KERN_ERR, sh, "setting bus on/off-time failed\n");
	aha1542_intr_reset(sh->io_port);
}

/* return non-zero on detection */
static struct Scsi_Host *aha1542_hw_init(struct scsi_host_template *tpnt, struct device *pdev, int indx)
{
	unsigned int base_io = io[indx];
	struct Scsi_Host *sh;
	struct aha1542_hostdata *aha1542;
	char dma_info[] = "no DMA";

	if (base_io == 0)
		return NULL;

	if (!request_region(base_io, AHA1542_REGION_SIZE, "aha1542"))
		return NULL;

	sh = scsi_host_alloc(tpnt, sizeof(struct aha1542_hostdata));
	if (!sh)
		goto release;
	aha1542 = shost_priv(sh);

	sh->unique_id = base_io;
	sh->io_port = base_io;
	sh->n_io_port = AHA1542_REGION_SIZE;
	aha1542->aha1542_last_mbi_used = 2 * AHA1542_MAILBOXES - 1;
	aha1542->aha1542_last_mbo_used = AHA1542_MAILBOXES - 1;

	if (!aha1542_test_port(sh))
		goto unregister;

	aha1542_set_bus_times(sh, bus_on[indx], bus_off[indx], dma_speed[indx]);
	if (aha1542_query(sh))
		goto unregister;
	if (aha1542_getconfig(sh) == -1)
		goto unregister;

	if (sh->dma_channel != 0xFF)
		snprintf(dma_info, sizeof(dma_info), "DMA %d", sh->dma_channel);
	shost_printk(KERN_INFO, sh, "Adaptec AHA-1542 (SCSI-ID %d) at IO 0x%x, IRQ %d, %s\n",
				sh->this_id, base_io, sh->irq, dma_info);
	if (aha1542->bios_translation == BIOS_TRANSLATION_25563)
		shost_printk(KERN_INFO, sh, "Using extended bios translation\n");

	if (dma_set_mask_and_coherent(pdev, DMA_BIT_MASK(24)) < 0)
		goto unregister;

	aha1542->mb = dma_alloc_coherent(pdev,
			AHA1542_MAILBOXES * 2 * sizeof(struct mailbox),
			&aha1542->mb_handle, GFP_KERNEL);
	if (!aha1542->mb)
		goto unregister;

	aha1542->ccb = dma_alloc_coherent(pdev,
			AHA1542_MAILBOXES * sizeof(struct ccb),
			&aha1542->ccb_handle, GFP_KERNEL);
	if (!aha1542->ccb)
		goto free_mb;

	setup_mailboxes(sh);

	if (request_irq(sh->irq, aha1542_interrupt, 0, "aha1542", sh)) {
		shost_printk(KERN_ERR, sh, "Unable to allocate IRQ.\n");
		goto free_ccb;
	}
	if (sh->dma_channel != 0xFF) {
		if (request_dma(sh->dma_channel, "aha1542")) {
			shost_printk(KERN_ERR, sh, "Unable to allocate DMA channel.\n");
			goto free_irq;
		}
		if (sh->dma_channel == 0 || sh->dma_channel >= 5) {
			set_dma_mode(sh->dma_channel, DMA_MODE_CASCADE);
			enable_dma(sh->dma_channel);
		}
	}

	if (scsi_add_host(sh, pdev))
		goto free_dma;

	scsi_scan_host(sh);

	return sh;

free_dma:
	if (sh->dma_channel != 0xff)
		free_dma(sh->dma_channel);
free_irq:
	free_irq(sh->irq, sh);
free_ccb:
	dma_free_coherent(pdev, AHA1542_MAILBOXES * sizeof(struct ccb),
			  aha1542->ccb, aha1542->ccb_handle);
free_mb:
	dma_free_coherent(pdev, AHA1542_MAILBOXES * 2 * sizeof(struct mailbox),
			  aha1542->mb, aha1542->mb_handle);
unregister:
	scsi_host_put(sh);
release:
	release_region(base_io, AHA1542_REGION_SIZE);

	return NULL;
}

static int aha1542_release(struct Scsi_Host *sh)
{
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	struct device *dev = sh->dma_dev;

	scsi_remove_host(sh);
	if (sh->dma_channel != 0xff)
		free_dma(sh->dma_channel);
	dma_free_coherent(dev, AHA1542_MAILBOXES * sizeof(struct ccb),
			  aha1542->ccb, aha1542->ccb_handle);
	dma_free_coherent(dev, AHA1542_MAILBOXES * 2 * sizeof(struct mailbox),
			  aha1542->mb, aha1542->mb_handle);
	if (sh->irq)
		free_irq(sh->irq, sh);
	if (sh->io_port && sh->n_io_port)
		release_region(sh->io_port, sh->n_io_port);
	scsi_host_put(sh);
	return 0;
}


/*
 * This is a device reset.  This is handled by sending a special command
 * to the device.
 */
static int aha1542_dev_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *sh = cmd->device->host;
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	unsigned long flags;
	struct mailbox *mb = aha1542->mb;
	u8 target = cmd->device->id;
	u8 lun = cmd->device->lun;
	int mbo;
	struct ccb *ccb = aha1542->ccb;

	spin_lock_irqsave(sh->host_lock, flags);
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
					 * prevent someone else from
					 * screwing with this cdb.
					 */

	aha1542->aha1542_last_mbo_used = mbo;

	/* This gets trashed for some reason */
	any2scsi(mb[mbo].ccbptr, aha1542->ccb_handle + mbo * sizeof(*ccb));

	memset(&ccb[mbo], 0, sizeof(struct ccb));

	ccb[mbo].op = 0x81;	/* BUS DEVICE RESET */

	ccb[mbo].idlun = (target & 7) << 5 | (lun & 7);		/*SCSI Target Id */

	ccb[mbo].linkptr[0] = ccb[mbo].linkptr[1] = ccb[mbo].linkptr[2] = 0;
	ccb[mbo].commlinkid = 0;

	/*
	 * Now tell the 1542 to flush all pending commands for this
	 * target
	 */
	aha1542_outb(sh->io_port, CMD_START_SCSI);
	spin_unlock_irqrestore(sh->host_lock, flags);

	scmd_printk(KERN_WARNING, cmd,
		"Trying device reset for target\n");

	return SUCCESS;
}

static int aha1542_reset(struct scsi_cmnd *cmd, u8 reset_cmd)
{
	struct Scsi_Host *sh = cmd->device->host;
	struct aha1542_hostdata *aha1542 = shost_priv(sh);
	unsigned long flags;
	int i;

	spin_lock_irqsave(sh->host_lock, flags);
	/*
	 * This does a scsi reset for all devices on the bus.
	 * In principle, we could also reset the 1542 - should
	 * we do this?  Try this first, and we can add that later
	 * if it turns out to be useful.
	 */
	outb(reset_cmd, CONTROL(cmd->device->host->io_port));

	if (!wait_mask(STATUS(cmd->device->host->io_port),
	     STATMASK, IDLE, STST | DIAGF | INVDCMD | DF | CDF, 0)) {
		spin_unlock_irqrestore(sh->host_lock, flags);
		return FAILED;
	}

	/*
	 * We need to do this too before the 1542 can interact with
	 * us again after host reset.
	 */
	if (reset_cmd & HRST)
		setup_mailboxes(cmd->device->host);

	/*
	 * Now try to pick up the pieces.  For all pending commands,
	 * free any internal data structures, and basically clear things
	 * out.  We do not try and restart any commands or anything -
	 * the strategy handler takes care of that crap.
	 */
	shost_printk(KERN_WARNING, cmd->device->host, "Sent BUS RESET to scsi host %d\n", cmd->device->host->host_no);

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
			aha1542_free_cmd(tmp_cmd);
			aha1542->int_cmds[i] = NULL;
			aha1542->mb[i].status = 0;
		}
	}

	spin_unlock_irqrestore(sh->host_lock, flags);
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

static int aha1542_init_cmd_priv(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct aha1542_cmd *acmd = scsi_cmd_priv(cmd);

	acmd->data_buffer = dma_alloc_coherent(shost->dma_dev,
			SECTOR_SIZE * AHA1542_MAX_SECTORS,
			&acmd->data_buffer_handle, GFP_KERNEL);
	if (!acmd->data_buffer)
		return -ENOMEM;
	return 0;
}

static int aha1542_exit_cmd_priv(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct aha1542_cmd *acmd = scsi_cmd_priv(cmd);

	dma_free_coherent(shost->dma_dev, SECTOR_SIZE * AHA1542_MAX_SECTORS,
			acmd->data_buffer, acmd->data_buffer_handle);
	return 0;
}

static struct scsi_host_template driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= "aha1542",
	.name			= "Adaptec 1542",
	.cmd_size		= sizeof(struct aha1542_cmd),
	.queuecommand		= aha1542_queuecommand,
	.eh_device_reset_handler= aha1542_dev_reset,
	.eh_bus_reset_handler	= aha1542_bus_reset,
	.eh_host_reset_handler	= aha1542_host_reset,
	.bios_param		= aha1542_biosparam,
	.init_cmd_priv		= aha1542_init_cmd_priv,
	.exit_cmd_priv		= aha1542_exit_cmd_priv,
	.can_queue		= AHA1542_MAILBOXES,
	.this_id		= 7,
	.max_sectors		= AHA1542_MAX_SECTORS,
	.sg_tablesize		= SG_ALL,
};

static int aha1542_isa_match(struct device *pdev, unsigned int ndev)
{
	struct Scsi_Host *sh = aha1542_hw_init(&driver_template, pdev, ndev);

	if (!sh)
		return 0;

	dev_set_drvdata(pdev, sh);
	return 1;
}

static void aha1542_isa_remove(struct device *pdev,
				    unsigned int ndev)
{
	aha1542_release(dev_get_drvdata(pdev));
	dev_set_drvdata(pdev, NULL);
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
static const struct pnp_device_id aha1542_pnp_ids[] = {
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

		/*
		 * The card can be queried for its DMA, we have
		 * the DMA set up that is enough
		 */

		dev_info(&pdev->dev, "ISAPnP found an AHA1535 at I/O 0x%03X", io[indx]);
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
