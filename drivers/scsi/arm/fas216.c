// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/acorn/scsi/fas216.c
 *
 *  Copyright (C) 1997-2003 Russell King
 *
 * Based on information in qlogicfas.c by Tom Zerucha, Michael Griffith, and
 * other sources, including:
 *   the AMD Am53CF94 data sheet
 *   the AMD Am53C94 data sheet
 *
 * This is a generic driver.  To use it, have a look at cumana_2.c.  You
 * should define your own structure that overlays FAS216_Info, eg:
 * struct my_host_data {
 *    FAS216_Info info;
 *    ... my host specific data ...
 * };
 *
 * Changelog:
 *  30-08-1997	RMK	Created
 *  14-09-1997	RMK	Started disconnect support
 *  08-02-1998	RMK	Corrected real DMA support
 *  15-02-1998	RMK	Started sync xfer support
 *  06-04-1998	RMK	Tightened conditions for printing incomplete
 *			transfers
 *  02-05-1998	RMK	Added extra checks in fas216_reset
 *  24-05-1998	RMK	Fixed synchronous transfers with period >= 200ns
 *  27-06-1998	RMK	Changed asm/delay.h to linux/delay.h
 *  26-08-1998	RMK	Improved message support wrt MESSAGE_REJECT
 *  02-04-2000	RMK	Converted to use the new error handling, and
 *			automatically request sense data upon check
 *			condition status from targets.
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ecard.h>

#include "../scsi.h"
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_host.h>
#include "fas216.h"
#include "scsi.h"

/* NOTE: SCSI2 Synchronous transfers *require* DMA according to
 *  the data sheet.  This restriction is crazy, especially when
 *  you only want to send 16 bytes!  What were the guys who
 *  designed this chip on at that time?  Did they read the SCSI2
 *  spec at all?  The following sections are taken from the SCSI2
 *  standard (s2r10) concerning this:
 *
 * > IMPLEMENTORS NOTES:
 * >   (1)  Re-negotiation at every selection is not recommended, since a
 * >   significant performance impact is likely.
 *
 * >  The implied synchronous agreement shall remain in effect until a BUS DEVICE
 * >  RESET message is received, until a hard reset condition occurs, or until one
 * >  of the two SCSI devices elects to modify the agreement.  The default data
 * >  transfer mode is asynchronous data transfer mode.  The default data transfer
 * >  mode is entered at power on, after a BUS DEVICE RESET message, or after a hard
 * >  reset condition.
 *
 *  In total, this means that once you have elected to use synchronous
 *  transfers, you must always use DMA.
 *
 *  I was thinking that this was a good chip until I found this restriction ;(
 */
#define SCSI2_SYNC
#undef  SCSI2_TAG

#undef DEBUG_CONNECT
#undef DEBUG_MESSAGES

#undef CHECK_STRUCTURE

#define LOG_CONNECT		(1 << 0)
#define LOG_BUSSERVICE		(1 << 1)
#define LOG_FUNCTIONDONE	(1 << 2)
#define LOG_MESSAGES		(1 << 3)
#define LOG_BUFFER		(1 << 4)
#define LOG_ERROR		(1 << 8)

static int level_mask = LOG_ERROR;

module_param(level_mask, int, 0644);

#ifndef MODULE
static int __init fas216_log_setup(char *str)
{
	char *s;

	level_mask = 0;

	while ((s = strsep(&str, ",")) != NULL) {
		switch (s[0]) {
		case 'a':
			if (strcmp(s, "all") == 0)
				level_mask |= -1;
			break;
		case 'b':
			if (strncmp(s, "bus", 3) == 0)
				level_mask |= LOG_BUSSERVICE;
			if (strncmp(s, "buf", 3) == 0)
				level_mask |= LOG_BUFFER;
			break;
		case 'c':
			level_mask |= LOG_CONNECT;
			break;
		case 'e':
			level_mask |= LOG_ERROR;
			break;
		case 'm':
			level_mask |= LOG_MESSAGES;
			break;
		case 'n':
			if (strcmp(s, "none") == 0)
				level_mask = 0;
			break;
		case 's':
			level_mask |= LOG_FUNCTIONDONE;
			break;
		}
	}
	return 1;
}

__setup("fas216_logging=", fas216_log_setup);
#endif

static inline unsigned char fas216_readb(FAS216_Info *info, unsigned int reg)
{
	unsigned int off = reg << info->scsi.io_shift;
	return readb(info->scsi.io_base + off);
}

static inline void fas216_writeb(FAS216_Info *info, unsigned int reg, unsigned int val)
{
	unsigned int off = reg << info->scsi.io_shift;
	writeb(val, info->scsi.io_base + off);
}

static void fas216_dumpstate(FAS216_Info *info)
{
	unsigned char is, stat, inst;

	is   = fas216_readb(info, REG_IS);
	stat = fas216_readb(info, REG_STAT);
	inst = fas216_readb(info, REG_INST);
	
	printk("FAS216: CTCL=%02X CTCM=%02X CMD=%02X STAT=%02X"
	       " INST=%02X IS=%02X CFIS=%02X",
		fas216_readb(info, REG_CTCL),
		fas216_readb(info, REG_CTCM),
		fas216_readb(info, REG_CMD),  stat, inst, is,
		fas216_readb(info, REG_CFIS));
	printk(" CNTL1=%02X CNTL2=%02X CNTL3=%02X CTCH=%02X\n",
		fas216_readb(info, REG_CNTL1),
		fas216_readb(info, REG_CNTL2),
		fas216_readb(info, REG_CNTL3),
		fas216_readb(info, REG_CTCH));
}

static void print_SCp(struct scsi_pointer *SCp, const char *prefix, const char *suffix)
{
	printk("%sptr %p this_residual 0x%x buffer %p buffers_residual 0x%x%s",
		prefix, SCp->ptr, SCp->this_residual, SCp->buffer,
		SCp->buffers_residual, suffix);
}

#ifdef CHECK_STRUCTURE
static void fas216_dumpinfo(FAS216_Info *info)
{
	static int used = 0;
	int i;

	if (used++)
		return;

	printk("FAS216_Info=\n");
	printk("  { magic_start=%lX host=%p SCpnt=%p origSCpnt=%p\n",
		info->magic_start, info->host, info->SCpnt,
		info->origSCpnt);
	printk("    scsi={ io_shift=%X irq=%X cfg={ %X %X %X %X }\n",
		info->scsi.io_shift, info->scsi.irq,
		info->scsi.cfg[0], info->scsi.cfg[1], info->scsi.cfg[2],
		info->scsi.cfg[3]);
	printk("           type=%p phase=%X\n",
		info->scsi.type, info->scsi.phase);
	print_SCp(&info->scsi.SCp, "           SCp={ ", " }\n");
	printk("      msgs async_stp=%X disconnectable=%d aborting=%d }\n",
		info->scsi.async_stp,
		info->scsi.disconnectable, info->scsi.aborting);
	printk("    stats={ queues=%X removes=%X fins=%X reads=%X writes=%X miscs=%X\n"
	       "            disconnects=%X aborts=%X bus_resets=%X host_resets=%X}\n",
		info->stats.queues, info->stats.removes, info->stats.fins,
		info->stats.reads, info->stats.writes, info->stats.miscs,
		info->stats.disconnects, info->stats.aborts, info->stats.bus_resets,
		info->stats.host_resets);
	printk("    ifcfg={ clockrate=%X select_timeout=%X asyncperiod=%X sync_max_depth=%X }\n",
		info->ifcfg.clockrate, info->ifcfg.select_timeout,
		info->ifcfg.asyncperiod, info->ifcfg.sync_max_depth);
	for (i = 0; i < 8; i++) {
		printk("    busyluns[%d]=%08lx dev[%d]={ disconnect_ok=%d stp=%X sof=%X sync_state=%X }\n",
			i, info->busyluns[i], i,
			info->device[i].disconnect_ok, info->device[i].stp,
			info->device[i].sof, info->device[i].sync_state);
	}
	printk("    dma={ transfer_type=%X setup=%p pseudo=%p stop=%p }\n",
		info->dma.transfer_type, info->dma.setup,
		info->dma.pseudo, info->dma.stop);
	printk("    internal_done=%X magic_end=%lX }\n",
		info->internal_done, info->magic_end);
}

static void __fas216_checkmagic(FAS216_Info *info, const char *func)
{
	int corruption = 0;
	if (info->magic_start != MAGIC) {
		printk(KERN_CRIT "FAS216 Error: magic at start corrupted\n");
		corruption++;
	}
	if (info->magic_end != MAGIC) {
		printk(KERN_CRIT "FAS216 Error: magic at end corrupted\n");
		corruption++;
	}
	if (corruption) {
		fas216_dumpinfo(info);
		panic("scsi memory space corrupted in %s", func);
	}
}
#define fas216_checkmagic(info) __fas216_checkmagic((info), __func__)
#else
#define fas216_checkmagic(info)
#endif

static const char *fas216_bus_phase(int stat)
{
	static const char *phases[] = {
		"DATA OUT", "DATA IN",
		"COMMAND", "STATUS",
		"MISC OUT", "MISC IN",
		"MESG OUT", "MESG IN"
	};

	return phases[stat & STAT_BUSMASK];
}

static const char *fas216_drv_phase(FAS216_Info *info)
{
	static const char *phases[] = {
		[PHASE_IDLE]		= "idle",
		[PHASE_SELECTION]	= "selection",
		[PHASE_COMMAND]		= "command",
		[PHASE_DATAOUT]		= "data out",
		[PHASE_DATAIN]		= "data in",
		[PHASE_MSGIN]		= "message in",
		[PHASE_MSGIN_DISCONNECT]= "disconnect",
		[PHASE_MSGOUT_EXPECT]	= "expect message out",
		[PHASE_MSGOUT]		= "message out",
		[PHASE_STATUS]		= "status",
		[PHASE_DONE]		= "done",
	};

	if (info->scsi.phase < ARRAY_SIZE(phases) &&
	    phases[info->scsi.phase])
		return phases[info->scsi.phase];
	return "???";
}

static char fas216_target(FAS216_Info *info)
{
	if (info->SCpnt)
		return '0' + info->SCpnt->device->id;
	else
		return 'H';
}

static void
fas216_do_log(FAS216_Info *info, char target, char *fmt, va_list ap)
{
	static char buf[1024];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	printk("scsi%d.%c: %s", info->host->host_no, target, buf);
}

static void fas216_log_command(FAS216_Info *info, int level,
			       struct scsi_cmnd *SCpnt, char *fmt, ...)
{
	va_list args;

	if (level != 0 && !(level & level_mask))
		return;

	va_start(args, fmt);
	fas216_do_log(info, '0' + SCpnt->device->id, fmt, args);
	va_end(args);

	scsi_print_command(SCpnt);
}

static void
fas216_log_target(FAS216_Info *info, int level, int target, char *fmt, ...)
{
	va_list args;

	if (level != 0 && !(level & level_mask))
		return;

	if (target < 0)
		target = 'H';
	else
		target += '0';

	va_start(args, fmt);
	fas216_do_log(info, target, fmt, args);
	va_end(args);

	printk("\n");
}

static void fas216_log(FAS216_Info *info, int level, char *fmt, ...)
{
	va_list args;

	if (level != 0 && !(level & level_mask))
		return;

	va_start(args, fmt);
	fas216_do_log(info, fas216_target(info), fmt, args);
	va_end(args);

	printk("\n");
}

#define PH_SIZE	32

static struct { int stat, ssr, isr, ph; } ph_list[PH_SIZE];
static int ph_ptr;

static void add_debug_list(int stat, int ssr, int isr, int ph)
{
	ph_list[ph_ptr].stat = stat;
	ph_list[ph_ptr].ssr = ssr;
	ph_list[ph_ptr].isr = isr;
	ph_list[ph_ptr].ph = ph;

	ph_ptr = (ph_ptr + 1) & (PH_SIZE-1);
}

static struct { int command; void *from; } cmd_list[8];
static int cmd_ptr;

static void fas216_cmd(FAS216_Info *info, unsigned int command)
{
	cmd_list[cmd_ptr].command = command;
	cmd_list[cmd_ptr].from = __builtin_return_address(0);

	cmd_ptr = (cmd_ptr + 1) & 7;

	fas216_writeb(info, REG_CMD, command);
}

static void print_debug_list(void)
{
	int i;

	i = ph_ptr;

	printk(KERN_ERR "SCSI IRQ trail\n");
	do {
		printk(" %02x:%02x:%02x:%1x",
			ph_list[i].stat, ph_list[i].ssr,
			ph_list[i].isr, ph_list[i].ph);
		i = (i + 1) & (PH_SIZE - 1);
		if (((i ^ ph_ptr) & 7) == 0)
			printk("\n");
	} while (i != ph_ptr);
	if ((i ^ ph_ptr) & 7)
		printk("\n");

	i = cmd_ptr;
	printk(KERN_ERR "FAS216 commands: ");
	do {
		printk("%02x:%p ", cmd_list[i].command, cmd_list[i].from);
		i = (i + 1) & 7;
	} while (i != cmd_ptr);
	printk("\n");
}

static void fas216_done(FAS216_Info *info, unsigned int result);

/**
 * fas216_get_last_msg - retrive last message from the list
 * @info: interface to search
 * @pos: current fifo position
 *
 * Retrieve a last message from the list, using position in fifo.
 */
static inline unsigned short
fas216_get_last_msg(FAS216_Info *info, int pos)
{
	unsigned short packed_msg = NOP;
	struct message *msg;
	int msgnr = 0;

	while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
		if (pos >= msg->fifo)
			break;
	}

	if (msg) {
		if (msg->msg[0] == EXTENDED_MESSAGE)
			packed_msg = EXTENDED_MESSAGE | msg->msg[2] << 8;
		else
			packed_msg = msg->msg[0];
	}

	fas216_log(info, LOG_MESSAGES,
		"Message: %04x found at position %02x\n", packed_msg, pos);

	return packed_msg;
}

/**
 * fas216_syncperiod - calculate STP register value
 * @info: state structure for interface connected to device
 * @ns: period in ns (between subsequent bytes)
 *
 * Calculate value to be loaded into the STP register for a given period
 * in ns. Returns a value suitable for REG_STP.
 */
static int fas216_syncperiod(FAS216_Info *info, int ns)
{
	int value = (info->ifcfg.clockrate * ns) / 1000;

	fas216_checkmagic(info);

	if (value < 4)
		value = 4;
	else if (value > 35)
		value = 35;

	return value & 31;
}

/**
 * fas216_set_sync - setup FAS216 chip for specified transfer period.
 * @info: state structure for interface connected to device
 * @target: target
 *
 * Correctly setup FAS216 chip for specified transfer period.
 * Notes   : we need to switch the chip out of FASTSCSI mode if we have
 *           a transfer period >= 200ns - otherwise the chip will violate
 *           the SCSI timings.
 */
static void fas216_set_sync(FAS216_Info *info, int target)
{
	unsigned int cntl3;

	fas216_writeb(info, REG_SOF, info->device[target].sof);
	fas216_writeb(info, REG_STP, info->device[target].stp);

	cntl3 = info->scsi.cfg[2];
	if (info->device[target].period >= (200 / 4))
		cntl3 = cntl3 & ~CNTL3_FASTSCSI;

	fas216_writeb(info, REG_CNTL3, cntl3);
}

/* Synchronous transfer support
 *
 * Note: The SCSI II r10 spec says (5.6.12):
 *
 *  (2)  Due to historical problems with early host adapters that could
 *  not accept an SDTR message, some targets may not initiate synchronous
 *  negotiation after a power cycle as required by this standard.  Host
 *  adapters that support synchronous mode may avoid the ensuing failure
 *  modes when the target is independently power cycled by initiating a
 *  synchronous negotiation on each REQUEST SENSE and INQUIRY command.
 *  This approach increases the SCSI bus overhead and is not recommended
 *  for new implementations.  The correct method is to respond to an
 *  SDTR message with a MESSAGE REJECT message if the either the
 *  initiator or target devices does not support synchronous transfers
 *  or does not want to negotiate for synchronous transfers at the time.
 *  Using the correct method assures compatibility with wide data
 *  transfers and future enhancements.
 *
 * We will always initiate a synchronous transfer negotiation request on
 * every INQUIRY or REQUEST SENSE message, unless the target itself has
 * at some point performed a synchronous transfer negotiation request, or
 * we have synchronous transfers disabled for this device.
 */

/**
 * fas216_handlesync - Handle a synchronous transfer message
 * @info: state structure for interface
 * @msg: message from target
 *
 * Handle a synchronous transfer message from the target
 */
static void fas216_handlesync(FAS216_Info *info, char *msg)
{
	struct fas216_device *dev = &info->device[info->SCpnt->device->id];
	enum { sync, async, none, reject } res = none;

#ifdef SCSI2_SYNC
	switch (msg[0]) {
	case MESSAGE_REJECT:
		/* Synchronous transfer request failed.
		 * Note: SCSI II r10:
		 *
		 *  SCSI devices that are capable of synchronous
		 *  data transfers shall not respond to an SDTR
		 *  message with a MESSAGE REJECT message.
		 *
		 * Hence, if we get this condition, we disable
		 * negotiation for this device.
		 */
		if (dev->sync_state == neg_inprogress) {
			dev->sync_state = neg_invalid;
			res = async;
		}
		break;

	case EXTENDED_MESSAGE:
		switch (dev->sync_state) {
		/* We don't accept synchronous transfer requests.
		 * Respond with a MESSAGE_REJECT to prevent a
		 * synchronous transfer agreement from being reached.
		 */
		case neg_invalid:
			res = reject;
			break;

		/* We were not negotiating a synchronous transfer,
		 * but the device sent us a negotiation request.
		 * Honour the request by sending back a SDTR
		 * message containing our capability, limited by
		 * the targets capability.
		 */
		default:
			fas216_cmd(info, CMD_SETATN);
			if (msg[4] > info->ifcfg.sync_max_depth)
				msg[4] = info->ifcfg.sync_max_depth;
			if (msg[3] < 1000 / info->ifcfg.clockrate)
				msg[3] = 1000 / info->ifcfg.clockrate;

			msgqueue_flush(&info->scsi.msgs);
			msgqueue_addmsg(&info->scsi.msgs, 5,
					EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
					msg[3], msg[4]);
			info->scsi.phase = PHASE_MSGOUT_EXPECT;

			/* This is wrong.  The agreement is not in effect
			 * until this message is accepted by the device
			 */
			dev->sync_state = neg_targcomplete;
			res = sync;
			break;

		/* We initiated the synchronous transfer negotiation,
		 * and have successfully received a response from the
		 * target.  The synchronous transfer agreement has been
		 * reached.  Note: if the values returned are out of our
		 * bounds, we must reject the message.
		 */
		case neg_inprogress:
			res = reject;
			if (msg[4] <= info->ifcfg.sync_max_depth &&
			    msg[3] >= 1000 / info->ifcfg.clockrate) {
				dev->sync_state = neg_complete;
				res = sync;
			}
			break;
		}
	}
#else
	res = reject;
#endif

	switch (res) {
	case sync:
		dev->period = msg[3];
		dev->sof    = msg[4];
		dev->stp    = fas216_syncperiod(info, msg[3] * 4);
		fas216_set_sync(info, info->SCpnt->device->id);
		break;

	case reject:
		fas216_cmd(info, CMD_SETATN);
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		fallthrough;

	case async:
		dev->period = info->ifcfg.asyncperiod / 4;
		dev->sof    = 0;
		dev->stp    = info->scsi.async_stp;
		fas216_set_sync(info, info->SCpnt->device->id);
		break;

	case none:
		break;
	}
}

/**
 * fas216_updateptrs - update data pointers after transfer suspended/paused
 * @info: interface's local pointer to update
 * @bytes_transferred: number of bytes transferred
 *
 * Update data pointers after transfer suspended/paused
 */
static void fas216_updateptrs(FAS216_Info *info, int bytes_transferred)
{
	struct scsi_pointer *SCp = &info->scsi.SCp;

	fas216_checkmagic(info);

	BUG_ON(bytes_transferred < 0);

	SCp->phase -= bytes_transferred;

	while (bytes_transferred != 0) {
		if (SCp->this_residual > bytes_transferred)
			break;
		/*
		 * We have used up this buffer.  Move on to the
		 * next buffer.
		 */
		bytes_transferred -= SCp->this_residual;
		if (!next_SCp(SCp) && bytes_transferred) {
			printk(KERN_WARNING "scsi%d.%c: out of buffers\n",
				info->host->host_no, '0' + info->SCpnt->device->id);
			return;
		}
	}

	SCp->this_residual -= bytes_transferred;
	if (SCp->this_residual)
		SCp->ptr += bytes_transferred;
	else
		SCp->ptr = NULL;
}

/**
 * fas216_pio - transfer data off of/on to card using programmed IO
 * @info: interface to transfer data to/from
 * @direction: direction to transfer data (DMA_OUT/DMA_IN)
 *
 * Transfer data off of/on to card using programmed IO.
 * Notes: this is incredibly slow.
 */
static void fas216_pio(FAS216_Info *info, fasdmadir_t direction)
{
	struct scsi_pointer *SCp = &info->scsi.SCp;

	fas216_checkmagic(info);

	if (direction == DMA_OUT)
		fas216_writeb(info, REG_FF, get_next_SCp_byte(SCp));
	else
		put_next_SCp_byte(SCp, fas216_readb(info, REG_FF));

	if (SCp->this_residual == 0)
		next_SCp(SCp);
}

static void fas216_set_stc(FAS216_Info *info, unsigned int length)
{
	fas216_writeb(info, REG_STCL, length);
	fas216_writeb(info, REG_STCM, length >> 8);
	fas216_writeb(info, REG_STCH, length >> 16);
}

static unsigned int fas216_get_ctc(FAS216_Info *info)
{
	return fas216_readb(info, REG_CTCL) +
	       (fas216_readb(info, REG_CTCM) << 8) +
	       (fas216_readb(info, REG_CTCH) << 16);
}

/**
 * fas216_cleanuptransfer - clean up after a transfer has completed.
 * @info: interface to clean up
 *
 * Update the data pointers according to the number of bytes transferred
 * on the SCSI bus.
 */
static void fas216_cleanuptransfer(FAS216_Info *info)
{
	unsigned long total, residual, fifo;
	fasdmatype_t dmatype = info->dma.transfer_type;

	info->dma.transfer_type = fasdma_none;

	/*
	 * PIO transfers do not need to be cleaned up.
	 */
	if (dmatype == fasdma_pio || dmatype == fasdma_none)
		return;

	if (dmatype == fasdma_real_all)
		total = info->scsi.SCp.phase;
	else
		total = info->scsi.SCp.this_residual;

	residual = fas216_get_ctc(info);

	fifo = fas216_readb(info, REG_CFIS) & CFIS_CF;

	fas216_log(info, LOG_BUFFER, "cleaning up from previous "
		   "transfer: length 0x%06x, residual 0x%x, fifo %d",
		   total, residual, fifo);

	/*
	 * If we were performing Data-Out, the transfer counter
	 * counts down each time a byte is transferred by the
	 * host to the FIFO.  This means we must include the
	 * bytes left in the FIFO from the transfer counter.
	 */
	if (info->scsi.phase == PHASE_DATAOUT)
		residual += fifo;

	fas216_updateptrs(info, total - residual);
}

/**
 * fas216_transfer - Perform a DMA/PIO transfer off of/on to card
 * @info: interface from which device disconnected from
 *
 * Start a DMA/PIO transfer off of/on to card
 */
static void fas216_transfer(FAS216_Info *info)
{
	fasdmadir_t direction;
	fasdmatype_t dmatype;

	fas216_log(info, LOG_BUFFER,
		   "starttransfer: buffer %p length 0x%06x reqlen 0x%06x",
		   info->scsi.SCp.ptr, info->scsi.SCp.this_residual,
		   info->scsi.SCp.phase);

	if (!info->scsi.SCp.ptr) {
		fas216_log(info, LOG_ERROR, "null buffer passed to "
			   "fas216_starttransfer");
		print_SCp(&info->scsi.SCp, "SCp: ", "\n");
		print_SCp(&info->SCpnt->SCp, "Cmnd SCp: ", "\n");
		return;
	}

	/*
	 * If we have a synchronous transfer agreement in effect, we must
	 * use DMA mode.  If we are using asynchronous transfers, we may
	 * use DMA mode or PIO mode.
	 */
	if (info->device[info->SCpnt->device->id].sof)
		dmatype = fasdma_real_all;
	else
		dmatype = fasdma_pio;

	if (info->scsi.phase == PHASE_DATAOUT)
		direction = DMA_OUT;
	else
		direction = DMA_IN;

	if (info->dma.setup)
		dmatype = info->dma.setup(info->host, &info->scsi.SCp,
					  direction, dmatype);
	info->dma.transfer_type = dmatype;

	if (dmatype == fasdma_real_all)
		fas216_set_stc(info, info->scsi.SCp.phase);
	else
		fas216_set_stc(info, info->scsi.SCp.this_residual);

	switch (dmatype) {
	case fasdma_pio:
		fas216_log(info, LOG_BUFFER, "PIO transfer");
		fas216_writeb(info, REG_SOF, 0);
		fas216_writeb(info, REG_STP, info->scsi.async_stp);
		fas216_cmd(info, CMD_TRANSFERINFO);
		fas216_pio(info, direction);
		break;

	case fasdma_pseudo:
		fas216_log(info, LOG_BUFFER, "pseudo transfer");
		fas216_cmd(info, CMD_TRANSFERINFO | CMD_WITHDMA);
		info->dma.pseudo(info->host, &info->scsi.SCp,
				 direction, info->SCpnt->transfersize);
		break;

	case fasdma_real_block:
		fas216_log(info, LOG_BUFFER, "block dma transfer");
		fas216_cmd(info, CMD_TRANSFERINFO | CMD_WITHDMA);
		break;

	case fasdma_real_all:
		fas216_log(info, LOG_BUFFER, "total dma transfer");
		fas216_cmd(info, CMD_TRANSFERINFO | CMD_WITHDMA);
		break;

	default:
		fas216_log(info, LOG_BUFFER | LOG_ERROR,
			   "invalid FAS216 DMA type");
		break;
	}
}

/**
 * fas216_stoptransfer - Stop a DMA transfer onto / off of the card
 * @info: interface from which device disconnected from
 *
 * Called when we switch away from DATA IN or DATA OUT phases.
 */
static void fas216_stoptransfer(FAS216_Info *info)
{
	fas216_checkmagic(info);

	if (info->dma.transfer_type == fasdma_real_all ||
	    info->dma.transfer_type == fasdma_real_block)
		info->dma.stop(info->host, &info->scsi.SCp);

	fas216_cleanuptransfer(info);

	if (info->scsi.phase == PHASE_DATAIN) {
		unsigned int fifo;

		/*
		 * If we were performing Data-In, then the FIFO counter
		 * contains the number of bytes not transferred via DMA
		 * from the on-board FIFO.  Read them manually.
		 */
		fifo = fas216_readb(info, REG_CFIS) & CFIS_CF;
		while (fifo && info->scsi.SCp.ptr) {
			*info->scsi.SCp.ptr = fas216_readb(info, REG_FF);
			fas216_updateptrs(info, 1);
			fifo--;
		}
	} else {
		/*
		 * After a Data-Out phase, there may be unsent
		 * bytes left in the FIFO.  Flush them out.
		 */
		fas216_cmd(info, CMD_FLUSHFIFO);
	}
}

static void fas216_aborttransfer(FAS216_Info *info)
{
	fas216_checkmagic(info);

	if (info->dma.transfer_type == fasdma_real_all ||
	    info->dma.transfer_type == fasdma_real_block)
		info->dma.stop(info->host, &info->scsi.SCp);

	info->dma.transfer_type = fasdma_none;
	fas216_cmd(info, CMD_FLUSHFIFO);
}

static void fas216_kick(FAS216_Info *info);

/**
 * fas216_disconnected_intr - handle device disconnection
 * @info: interface from which device disconnected from
 *
 * Handle device disconnection
 */
static void fas216_disconnect_intr(FAS216_Info *info)
{
	unsigned long flags;

	fas216_checkmagic(info);

	fas216_log(info, LOG_CONNECT, "disconnect phase=%02x",
		   info->scsi.phase);

	msgqueue_flush(&info->scsi.msgs);

	switch (info->scsi.phase) {
	case PHASE_SELECTION:			/* while selecting - no target		*/
	case PHASE_SELSTEPS:
		fas216_done(info, DID_NO_CONNECT);
		break;

	case PHASE_MSGIN_DISCONNECT:		/* message in - disconnecting		*/
		info->scsi.disconnectable = 1;
		info->scsi.phase = PHASE_IDLE;
		info->stats.disconnects += 1;
		spin_lock_irqsave(&info->host_lock, flags);
		if (info->scsi.phase == PHASE_IDLE)
			fas216_kick(info);
		spin_unlock_irqrestore(&info->host_lock, flags);
		break;

	case PHASE_DONE:			/* at end of command - complete		*/
		fas216_done(info, DID_OK);
		break;

	case PHASE_MSGOUT:			/* message out - possible ABORT message	*/
		if (fas216_get_last_msg(info, info->scsi.msgin_fifo) == ABORT) {
			info->scsi.aborting = 0;
			fas216_done(info, DID_ABORT);
			break;
		}
		fallthrough;

	default:				/* huh?					*/
		printk(KERN_ERR "scsi%d.%c: unexpected disconnect in phase %s\n",
			info->host->host_no, fas216_target(info), fas216_drv_phase(info));
		print_debug_list();
		fas216_stoptransfer(info);
		fas216_done(info, DID_ERROR);
		break;
	}
}

/**
 * fas216_reselected_intr - start reconnection of a device
 * @info: interface which was reselected
 *
 * Start reconnection of a device
 */
static void
fas216_reselected_intr(FAS216_Info *info)
{
	unsigned int cfis, i;
	unsigned char msg[4];
	unsigned char target, lun, tag;

	fas216_checkmagic(info);

	WARN_ON(info->scsi.phase == PHASE_SELECTION ||
		info->scsi.phase == PHASE_SELSTEPS);

	cfis = fas216_readb(info, REG_CFIS);

	fas216_log(info, LOG_CONNECT, "reconnect phase=%02x cfis=%02x",
		   info->scsi.phase, cfis);

	cfis &= CFIS_CF;

	if (cfis < 2 || cfis > 4) {
		printk(KERN_ERR "scsi%d.H: incorrect number of bytes after reselect\n",
			info->host->host_no);
		goto bad_message;
	}

	for (i = 0; i < cfis; i++)
		msg[i] = fas216_readb(info, REG_FF);

	if (!(msg[0] & (1 << info->host->this_id)) ||
	    !(msg[1] & 0x80))
		goto initiator_error;

	target = msg[0] & ~(1 << info->host->this_id);
	target = ffs(target) - 1;
	lun = msg[1] & 7;
	tag = 0;

	if (cfis >= 3) {
		if (msg[2] != SIMPLE_QUEUE_TAG)
			goto initiator_error;

		tag = msg[3];
	}

	/* set up for synchronous transfers */
	fas216_writeb(info, REG_SDID, target);
	fas216_set_sync(info, target);
	msgqueue_flush(&info->scsi.msgs);

	fas216_log(info, LOG_CONNECT, "Reconnected: target %1x lun %1x tag %02x",
		   target, lun, tag);

	if (info->scsi.disconnectable && info->SCpnt) {
		info->scsi.disconnectable = 0;
		if (info->SCpnt->device->id  == target &&
		    info->SCpnt->device->lun == lun &&
		    info->SCpnt->tag         == tag) {
			fas216_log(info, LOG_CONNECT, "reconnected previously executing command");
		} else {
			queue_add_cmd_tail(&info->queues.disconnected, info->SCpnt);
			fas216_log(info, LOG_CONNECT, "had to move command to disconnected queue");
			info->SCpnt = NULL;
		}
	}
	if (!info->SCpnt) {
		info->SCpnt = queue_remove_tgtluntag(&info->queues.disconnected,
					target, lun, tag);
		fas216_log(info, LOG_CONNECT, "had to get command");
	}

	if (info->SCpnt) {
		/*
		 * Restore data pointer from SAVED data pointer
		 */
		info->scsi.SCp = info->SCpnt->SCp;

		fas216_log(info, LOG_CONNECT, "data pointers: [%p, %X]",
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
		info->scsi.phase = PHASE_MSGIN;
	} else {
		/*
		 * Our command structure not found - abort the
		 * command on the target.  Since we have no
		 * record of this command, we can't send
		 * an INITIATOR DETECTED ERROR message.
		 */
		fas216_cmd(info, CMD_SETATN);

#if 0
		if (tag)
			msgqueue_addmsg(&info->scsi.msgs, 2, ABORT_TAG, tag);
		else
#endif
			msgqueue_addmsg(&info->scsi.msgs, 1, ABORT);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		info->scsi.aborting = 1;
	}

	fas216_cmd(info, CMD_MSGACCEPTED);
	return;

 initiator_error:
	printk(KERN_ERR "scsi%d.H: error during reselection: bytes",
		info->host->host_no);
	for (i = 0; i < cfis; i++)
		printk(" %02x", msg[i]);
	printk("\n");
 bad_message:
	fas216_cmd(info, CMD_SETATN);
	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
	info->scsi.phase = PHASE_MSGOUT_EXPECT;
	fas216_cmd(info, CMD_MSGACCEPTED);
}

static void fas216_parse_message(FAS216_Info *info, unsigned char *message, int msglen)
{
	int i;

	switch (message[0]) {
	case COMMAND_COMPLETE:
		if (msglen != 1)
			goto unrecognised;

		printk(KERN_ERR "scsi%d.%c: command complete with no "
			"status in MESSAGE_IN?\n",
			info->host->host_no, fas216_target(info));
		break;

	case SAVE_POINTERS:
		if (msglen != 1)
			goto unrecognised;

		/*
		 * Save current data pointer to SAVED data pointer
		 * SCSI II standard says that we must not acknowledge
		 * this until we have really saved pointers.
		 * NOTE: we DO NOT save the command nor status pointers
		 * as required by the SCSI II standard.  These always
		 * point to the start of their respective areas.
		 */
		info->SCpnt->SCp = info->scsi.SCp;
		info->SCpnt->SCp.sent_command = 0;
		fas216_log(info, LOG_CONNECT | LOG_MESSAGES | LOG_BUFFER,
			"save data pointers: [%p, %X]",
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
		break;

	case RESTORE_POINTERS:
		if (msglen != 1)
			goto unrecognised;

		/*
		 * Restore current data pointer from SAVED data pointer
		 */
		info->scsi.SCp = info->SCpnt->SCp;
		fas216_log(info, LOG_CONNECT | LOG_MESSAGES | LOG_BUFFER,
			"restore data pointers: [%p, 0x%x]",
			info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
		break;

	case DISCONNECT:
		if (msglen != 1)
			goto unrecognised;

		info->scsi.phase = PHASE_MSGIN_DISCONNECT;
		break;

	case MESSAGE_REJECT:
		if (msglen != 1)
			goto unrecognised;

		switch (fas216_get_last_msg(info, info->scsi.msgin_fifo)) {
		case EXTENDED_MESSAGE | EXTENDED_SDTR << 8:
			fas216_handlesync(info, message);
			break;

		default:
			fas216_log(info, 0, "reject, last message 0x%04x",
				fas216_get_last_msg(info, info->scsi.msgin_fifo));
		}
		break;

	case NOP:
		break;

	case EXTENDED_MESSAGE:
		if (msglen < 3)
			goto unrecognised;

		switch (message[2]) {
		case EXTENDED_SDTR:	/* Sync transfer negotiation request/reply */
			fas216_handlesync(info, message);
			break;

		default:
			goto unrecognised;
		}
		break;

	default:
		goto unrecognised;
	}
	return;

unrecognised:
	fas216_log(info, 0, "unrecognised message, rejecting");
	printk("scsi%d.%c: message was", info->host->host_no, fas216_target(info));
	for (i = 0; i < msglen; i++)
		printk("%s%02X", i & 31 ? " " : "\n  ", message[i]);
	printk("\n");

	/*
	 * Something strange seems to be happening here -
	 * I can't use SETATN since the chip gives me an
	 * invalid command interrupt when I do.  Weird.
	 */
fas216_cmd(info, CMD_NOP);
fas216_dumpstate(info);
	fas216_cmd(info, CMD_SETATN);
	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, MESSAGE_REJECT);
	info->scsi.phase = PHASE_MSGOUT_EXPECT;
fas216_dumpstate(info);
}

static int fas216_wait_cmd(FAS216_Info *info, int cmd)
{
	int tout;
	int stat;

	fas216_cmd(info, cmd);

	for (tout = 1000; tout; tout -= 1) {
		stat = fas216_readb(info, REG_STAT);
		if (stat & (STAT_INT|STAT_PARITYERROR))
			break;
		udelay(1);
	}

	return stat;
}

static int fas216_get_msg_byte(FAS216_Info *info)
{
	unsigned int stat = fas216_wait_cmd(info, CMD_MSGACCEPTED);

	if ((stat & STAT_INT) == 0)
		goto timedout;

	if ((stat & STAT_BUSMASK) != STAT_MESGIN)
		goto unexpected_phase_change;

	fas216_readb(info, REG_INST);

	stat = fas216_wait_cmd(info, CMD_TRANSFERINFO);

	if ((stat & STAT_INT) == 0)
		goto timedout;

	if (stat & STAT_PARITYERROR)
		goto parity_error;

	if ((stat & STAT_BUSMASK) != STAT_MESGIN)
		goto unexpected_phase_change;

	fas216_readb(info, REG_INST);

	return fas216_readb(info, REG_FF);

timedout:
	fas216_log(info, LOG_ERROR, "timed out waiting for message byte");
	return -1;

unexpected_phase_change:
	fas216_log(info, LOG_ERROR, "unexpected phase change: status = %02x", stat);
	return -2;

parity_error:
	fas216_log(info, LOG_ERROR, "parity error during message in phase");
	return -3;
}

/**
 * fas216_message - handle a function done interrupt from FAS216 chip
 * @info: interface which caused function done interrupt
 *
 * Handle a function done interrupt from FAS216 chip
 */
static void fas216_message(FAS216_Info *info)
{
	unsigned char *message = info->scsi.message;
	unsigned int msglen = 1;
	int msgbyte = 0;

	fas216_checkmagic(info);

	message[0] = fas216_readb(info, REG_FF);

	if (message[0] == EXTENDED_MESSAGE) {
		msgbyte = fas216_get_msg_byte(info);

		if (msgbyte >= 0) {
			message[1] = msgbyte;

			for (msglen = 2; msglen < message[1] + 2; msglen++) {
				msgbyte = fas216_get_msg_byte(info);

				if (msgbyte >= 0)
					message[msglen] = msgbyte;
				else
					break;
			}
		}
	}

	if (msgbyte == -3)
		goto parity_error;

#ifdef DEBUG_MESSAGES
	{
		int i;

		printk("scsi%d.%c: message in: ",
			info->host->host_no, fas216_target(info));
		for (i = 0; i < msglen; i++)
			printk("%02X ", message[i]);
		printk("\n");
	}
#endif

	fas216_parse_message(info, message, msglen);
	fas216_cmd(info, CMD_MSGACCEPTED);
	return;

parity_error:
	fas216_cmd(info, CMD_SETATN);
	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, MSG_PARITY_ERROR);
	info->scsi.phase = PHASE_MSGOUT_EXPECT;
	fas216_cmd(info, CMD_MSGACCEPTED);
	return;
}

/**
 * fas216_send_command - send command after all message bytes have been sent
 * @info: interface which caused bus service
 *
 * Send a command to a target after all message bytes have been sent
 */
static void fas216_send_command(FAS216_Info *info)
{
	int i;

	fas216_checkmagic(info);

	fas216_cmd(info, CMD_NOP|CMD_WITHDMA);
	fas216_cmd(info, CMD_FLUSHFIFO);

	/* load command */
	for (i = info->scsi.SCp.sent_command; i < info->SCpnt->cmd_len; i++)
		fas216_writeb(info, REG_FF, info->SCpnt->cmnd[i]);

	fas216_cmd(info, CMD_TRANSFERINFO);

	info->scsi.phase = PHASE_COMMAND;
}

/**
 * fas216_send_messageout - handle bus service to send a message
 * @info: interface which caused bus service
 *
 * Handle bus service to send a message.
 * Note: We do not allow the device to change the data direction!
 */
static void fas216_send_messageout(FAS216_Info *info, int start)
{
	unsigned int tot_msglen = msgqueue_msglength(&info->scsi.msgs);

	fas216_checkmagic(info);

	fas216_cmd(info, CMD_FLUSHFIFO);

	if (tot_msglen) {
		struct message *msg;
		int msgnr = 0;

		while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
			int i;

			for (i = start; i < msg->length; i++)
				fas216_writeb(info, REG_FF, msg->msg[i]);

			msg->fifo = tot_msglen - (fas216_readb(info, REG_CFIS) & CFIS_CF);
			start = 0;
		}
	} else
		fas216_writeb(info, REG_FF, NOP);

	fas216_cmd(info, CMD_TRANSFERINFO);

	info->scsi.phase = PHASE_MSGOUT;
}

/**
 * fas216_busservice_intr - handle bus service interrupt from FAS216 chip
 * @info: interface which caused bus service interrupt
 * @stat: Status register contents
 * @is: SCSI Status register contents
 *
 * Handle a bus service interrupt from FAS216 chip
 */
static void fas216_busservice_intr(FAS216_Info *info, unsigned int stat, unsigned int is)
{
	fas216_checkmagic(info);

	fas216_log(info, LOG_BUSSERVICE,
		   "bus service: stat=%02x is=%02x phase=%02x",
		   stat, is, info->scsi.phase);

	switch (info->scsi.phase) {
	case PHASE_SELECTION:
		if ((is & IS_BITS) != IS_MSGBYTESENT)
			goto bad_is;
		break;

	case PHASE_SELSTEPS:
		switch (is & IS_BITS) {
		case IS_SELARB:
		case IS_MSGBYTESENT:
			goto bad_is;

		case IS_NOTCOMMAND:
		case IS_EARLYPHASE:
			if ((stat & STAT_BUSMASK) == STAT_MESGIN)
				break;
			goto bad_is;

		case IS_COMPLETE:
			break;
		}

	default:
		break;
	}

	fas216_cmd(info, CMD_NOP);

#define STATE(st,ph) ((ph) << 3 | (st))
	/* This table describes the legal SCSI state transitions,
	 * as described by the SCSI II spec.
	 */
	switch (STATE(stat & STAT_BUSMASK, info->scsi.phase)) {
	case STATE(STAT_DATAIN, PHASE_SELSTEPS):/* Sel w/ steps -> Data In      */
	case STATE(STAT_DATAIN, PHASE_MSGOUT):  /* Message Out  -> Data In      */
	case STATE(STAT_DATAIN, PHASE_COMMAND): /* Command      -> Data In      */
	case STATE(STAT_DATAIN, PHASE_MSGIN):   /* Message In   -> Data In      */
		info->scsi.phase = PHASE_DATAIN;
		fas216_transfer(info);
		return;

	case STATE(STAT_DATAIN, PHASE_DATAIN):  /* Data In      -> Data In      */
	case STATE(STAT_DATAOUT, PHASE_DATAOUT):/* Data Out     -> Data Out     */
		fas216_cleanuptransfer(info);
		fas216_transfer(info);
		return;

	case STATE(STAT_DATAOUT, PHASE_SELSTEPS):/* Sel w/ steps-> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_MSGOUT): /* Message Out  -> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_COMMAND):/* Command      -> Data Out     */
	case STATE(STAT_DATAOUT, PHASE_MSGIN):  /* Message In   -> Data Out     */
		fas216_cmd(info, CMD_FLUSHFIFO);
		info->scsi.phase = PHASE_DATAOUT;
		fas216_transfer(info);
		return;

	case STATE(STAT_STATUS, PHASE_DATAOUT): /* Data Out     -> Status       */
	case STATE(STAT_STATUS, PHASE_DATAIN):  /* Data In      -> Status       */
		fas216_stoptransfer(info);
		fallthrough;

	case STATE(STAT_STATUS, PHASE_SELSTEPS):/* Sel w/ steps -> Status       */
	case STATE(STAT_STATUS, PHASE_MSGOUT):  /* Message Out  -> Status       */
	case STATE(STAT_STATUS, PHASE_COMMAND): /* Command      -> Status       */
	case STATE(STAT_STATUS, PHASE_MSGIN):   /* Message In   -> Status       */
		fas216_cmd(info, CMD_INITCMDCOMPLETE);
		info->scsi.phase = PHASE_STATUS;
		return;

	case STATE(STAT_MESGIN, PHASE_DATAOUT): /* Data Out     -> Message In   */
	case STATE(STAT_MESGIN, PHASE_DATAIN):  /* Data In      -> Message In   */
		fas216_stoptransfer(info);
		fallthrough;

	case STATE(STAT_MESGIN, PHASE_COMMAND):	/* Command	-> Message In	*/
	case STATE(STAT_MESGIN, PHASE_SELSTEPS):/* Sel w/ steps -> Message In   */
	case STATE(STAT_MESGIN, PHASE_MSGOUT):  /* Message Out  -> Message In   */
		info->scsi.msgin_fifo = fas216_readb(info, REG_CFIS) & CFIS_CF;
		fas216_cmd(info, CMD_FLUSHFIFO);
		fas216_cmd(info, CMD_TRANSFERINFO);
		info->scsi.phase = PHASE_MSGIN;
		return;

	case STATE(STAT_MESGIN, PHASE_MSGIN):
		info->scsi.msgin_fifo = fas216_readb(info, REG_CFIS) & CFIS_CF;
		fas216_cmd(info, CMD_TRANSFERINFO);
		return;

	case STATE(STAT_COMMAND, PHASE_MSGOUT): /* Message Out  -> Command      */
	case STATE(STAT_COMMAND, PHASE_MSGIN):  /* Message In   -> Command      */
		fas216_send_command(info);
		info->scsi.phase = PHASE_COMMAND;
		return;


	/*
	 * Selection    -> Message Out
	 */
	case STATE(STAT_MESGOUT, PHASE_SELECTION):
		fas216_send_messageout(info, 1);
		return;

	/*
	 * Message Out  -> Message Out
	 */
	case STATE(STAT_MESGOUT, PHASE_SELSTEPS):
	case STATE(STAT_MESGOUT, PHASE_MSGOUT):
		/*
		 * If we get another message out phase, this usually
		 * means some parity error occurred.  Resend complete
		 * set of messages.  If we have more than one byte to
		 * send, we need to assert ATN again.
		 */
		if (info->device[info->SCpnt->device->id].parity_check) {
			/*
			 * We were testing... good, the device
			 * supports parity checking.
			 */
			info->device[info->SCpnt->device->id].parity_check = 0;
			info->device[info->SCpnt->device->id].parity_enabled = 1;
			fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0]);
		}

		if (msgqueue_msglength(&info->scsi.msgs) > 1)
			fas216_cmd(info, CMD_SETATN);
		/*FALLTHROUGH*/

	/*
	 * Any          -> Message Out
	 */
	case STATE(STAT_MESGOUT, PHASE_MSGOUT_EXPECT):
		fas216_send_messageout(info, 0);
		return;

	/* Error recovery rules.
	 *   These either attempt to abort or retry the operation.
	 * TODO: we need more of these
	 */
	case STATE(STAT_COMMAND, PHASE_COMMAND):/* Command      -> Command      */
		/* error - we've sent out all the command bytes
		 * we have.
		 * NOTE: we need SAVE DATA POINTERS/RESTORE DATA POINTERS
		 * to include the command bytes sent for this to work
		 * correctly.
		 */
		printk(KERN_ERR "scsi%d.%c: "
			"target trying to receive more command bytes\n",
			info->host->host_no, fas216_target(info));
		fas216_cmd(info, CMD_SETATN);
		fas216_set_stc(info, 15);
		fas216_cmd(info, CMD_PADBYTES | CMD_WITHDMA);
		msgqueue_flush(&info->scsi.msgs);
		msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		return;
	}

	if (info->scsi.phase == PHASE_MSGIN_DISCONNECT) {
		printk(KERN_ERR "scsi%d.%c: disconnect message received, but bus service %s?\n",
			info->host->host_no, fas216_target(info),
			fas216_bus_phase(stat));
		msgqueue_flush(&info->scsi.msgs);
		fas216_cmd(info, CMD_SETATN);
		msgqueue_addmsg(&info->scsi.msgs, 1, INITIATOR_ERROR);
		info->scsi.phase = PHASE_MSGOUT_EXPECT;
		info->scsi.aborting = 1;
		fas216_cmd(info, CMD_TRANSFERINFO);
		return;
	}
	printk(KERN_ERR "scsi%d.%c: bus phase %s after %s?\n",
		info->host->host_no, fas216_target(info),
		fas216_bus_phase(stat),
		fas216_drv_phase(info));
	print_debug_list();
	return;

bad_is:
	fas216_log(info, 0, "bus service at step %d?", is & IS_BITS);
	fas216_dumpstate(info);
	print_debug_list();

	fas216_done(info, DID_ERROR);
}

/**
 * fas216_funcdone_intr - handle a function done interrupt from FAS216 chip
 * @info: interface which caused function done interrupt
 * @stat: Status register contents
 * @is: SCSI Status register contents
 *
 * Handle a function done interrupt from FAS216 chip
 */
static void fas216_funcdone_intr(FAS216_Info *info, unsigned int stat, unsigned int is)
{
	unsigned int fifo_len = fas216_readb(info, REG_CFIS) & CFIS_CF;

	fas216_checkmagic(info);

	fas216_log(info, LOG_FUNCTIONDONE,
		   "function done: stat=%02x is=%02x phase=%02x",
		   stat, is, info->scsi.phase);

	switch (info->scsi.phase) {
	case PHASE_STATUS:			/* status phase - read status and msg	*/
		if (fifo_len != 2) {
			fas216_log(info, 0, "odd number of bytes in FIFO: %d", fifo_len);
		}
		/*
		 * Read status then message byte.
		 */
		info->scsi.SCp.Status = fas216_readb(info, REG_FF);
		info->scsi.SCp.Message = fas216_readb(info, REG_FF);
		info->scsi.phase = PHASE_DONE;
		fas216_cmd(info, CMD_MSGACCEPTED);
		break;

	case PHASE_IDLE:
	case PHASE_SELECTION:
	case PHASE_SELSTEPS:
		break;

	case PHASE_MSGIN:			/* message in phase			*/
		if ((stat & STAT_BUSMASK) == STAT_MESGIN) {
			info->scsi.msgin_fifo = fifo_len;
			fas216_message(info);
			break;
		}
		fallthrough;

	default:
		fas216_log(info, 0, "internal phase %s for function done?"
			"  What do I do with this?",
			fas216_target(info), fas216_drv_phase(info));
	}
}

static void fas216_bus_reset(FAS216_Info *info)
{
	neg_t sync_state;
	int i;

	msgqueue_flush(&info->scsi.msgs);

	sync_state = neg_invalid;

#ifdef SCSI2_SYNC
	if (info->ifcfg.capabilities & (FASCAP_DMA|FASCAP_PSEUDODMA))
		sync_state = neg_wait;
#endif

	info->scsi.phase = PHASE_IDLE;
	info->SCpnt = NULL; /* bug! */
	memset(&info->scsi.SCp, 0, sizeof(info->scsi.SCp));

	for (i = 0; i < 8; i++) {
		info->device[i].disconnect_ok	= info->ifcfg.disconnect_ok;
		info->device[i].sync_state	= sync_state;
		info->device[i].period		= info->ifcfg.asyncperiod / 4;
		info->device[i].stp		= info->scsi.async_stp;
		info->device[i].sof		= 0;
		info->device[i].wide_xfer	= 0;
	}

	info->rst_bus_status = 1;
	wake_up(&info->eh_wait);
}

/**
 * fas216_intr - handle interrupts to progress a command
 * @info: interface to service
 *
 * Handle interrupts from the interface to progress a command
 */
irqreturn_t fas216_intr(FAS216_Info *info)
{
	unsigned char inst, is, stat;
	int handled = IRQ_NONE;

	fas216_checkmagic(info);

	stat = fas216_readb(info, REG_STAT);
	is = fas216_readb(info, REG_IS);
	inst = fas216_readb(info, REG_INST);

	add_debug_list(stat, is, inst, info->scsi.phase);

	if (stat & STAT_INT) {
		if (inst & INST_BUSRESET) {
			fas216_log(info, 0, "bus reset detected");
			fas216_bus_reset(info);
			scsi_report_bus_reset(info->host, 0);
		} else if (inst & INST_ILLEGALCMD) {
			fas216_log(info, LOG_ERROR, "illegal command given\n");
			fas216_dumpstate(info);
			print_debug_list();
		} else if (inst & INST_DISCONNECT)
			fas216_disconnect_intr(info);
		else if (inst & INST_RESELECTED)	/* reselected			*/
			fas216_reselected_intr(info);
		else if (inst & INST_BUSSERVICE)	/* bus service request		*/
			fas216_busservice_intr(info, stat, is);
		else if (inst & INST_FUNCDONE)		/* function done		*/
			fas216_funcdone_intr(info, stat, is);
		else
		    	fas216_log(info, 0, "unknown interrupt received:"
				" phase %s inst %02X is %02X stat %02X",
				fas216_drv_phase(info), inst, is, stat);
		handled = IRQ_HANDLED;
	}
	return handled;
}

static void __fas216_start_command(FAS216_Info *info, struct scsi_cmnd *SCpnt)
{
	int tot_msglen;

	/* following what the ESP driver says */
	fas216_set_stc(info, 0);
	fas216_cmd(info, CMD_NOP | CMD_WITHDMA);

	/* flush FIFO */
	fas216_cmd(info, CMD_FLUSHFIFO);

	/* load bus-id and timeout */
	fas216_writeb(info, REG_SDID, BUSID(SCpnt->device->id));
	fas216_writeb(info, REG_STIM, info->ifcfg.select_timeout);

	/* synchronous transfers */
	fas216_set_sync(info, SCpnt->device->id);

	tot_msglen = msgqueue_msglength(&info->scsi.msgs);

#ifdef DEBUG_MESSAGES
	{
		struct message *msg;
		int msgnr = 0, i;

		printk("scsi%d.%c: message out: ",
			info->host->host_no, '0' + SCpnt->device->id);
		while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
			printk("{ ");
			for (i = 0; i < msg->length; i++)
				printk("%02x ", msg->msg[i]);
			printk("} ");
		}
		printk("\n");
	}
#endif

	if (tot_msglen == 1 || tot_msglen == 3) {
		/*
		 * We have an easy message length to send...
		 */
		struct message *msg;
		int msgnr = 0, i;

		info->scsi.phase = PHASE_SELSTEPS;

		/* load message bytes */
		while ((msg = msgqueue_getmsg(&info->scsi.msgs, msgnr++)) != NULL) {
			for (i = 0; i < msg->length; i++)
				fas216_writeb(info, REG_FF, msg->msg[i]);
			msg->fifo = tot_msglen - (fas216_readb(info, REG_CFIS) & CFIS_CF);
		}

		/* load command */
		for (i = 0; i < SCpnt->cmd_len; i++)
			fas216_writeb(info, REG_FF, SCpnt->cmnd[i]);

		if (tot_msglen == 1)
			fas216_cmd(info, CMD_SELECTATN);
		else
			fas216_cmd(info, CMD_SELECTATN3);
	} else {
		/*
		 * We have an unusual number of message bytes to send.
		 *  Load first byte into fifo, and issue SELECT with ATN and
		 *  stop steps.
		 */
		struct message *msg = msgqueue_getmsg(&info->scsi.msgs, 0);

		fas216_writeb(info, REG_FF, msg->msg[0]);
		msg->fifo = 1;

		fas216_cmd(info, CMD_SELECTATNSTOP);
	}
}

/*
 * Decide whether we need to perform a parity test on this device.
 * Can also be used to force parity error conditions during initial
 * information transfer phase (message out) for test purposes.
 */
static int parity_test(FAS216_Info *info, int target)
{
#if 0
	if (target == 3) {
		info->device[target].parity_check = 0;
		return 1;
	}
#endif
	return info->device[target].parity_check;
}

static void fas216_start_command(FAS216_Info *info, struct scsi_cmnd *SCpnt)
{
	int disconnect_ok;

	/*
	 * claim host busy
	 */
	info->scsi.phase = PHASE_SELECTION;
	info->scsi.SCp = SCpnt->SCp;
	info->SCpnt = SCpnt;
	info->dma.transfer_type = fasdma_none;

	if (parity_test(info, SCpnt->device->id))
		fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0] | CNTL1_PTE);
	else
		fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0]);

	/*
	 * Don't allow request sense commands to disconnect.
	 */
	disconnect_ok = SCpnt->cmnd[0] != REQUEST_SENSE &&
			info->device[SCpnt->device->id].disconnect_ok;

	/*
	 * build outgoing message bytes
	 */
	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, IDENTIFY(disconnect_ok, SCpnt->device->lun));

	/*
	 * add tag message if required
	 */
	if (SCpnt->tag)
		msgqueue_addmsg(&info->scsi.msgs, 2, SIMPLE_QUEUE_TAG, SCpnt->tag);

	do {
#ifdef SCSI2_SYNC
		if ((info->device[SCpnt->device->id].sync_state == neg_wait ||
		     info->device[SCpnt->device->id].sync_state == neg_complete) &&
		    (SCpnt->cmnd[0] == REQUEST_SENSE ||
		     SCpnt->cmnd[0] == INQUIRY)) {
			info->device[SCpnt->device->id].sync_state = neg_inprogress;
			msgqueue_addmsg(&info->scsi.msgs, 5,
					EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
					1000 / info->ifcfg.clockrate,
					info->ifcfg.sync_max_depth);
			break;
		}
#endif
	} while (0);

	__fas216_start_command(info, SCpnt);
}

static void fas216_allocate_tag(FAS216_Info *info, struct scsi_cmnd *SCpnt)
{
#ifdef SCSI2_TAG
	/*
	 * tagged queuing - allocate a new tag to this command
	 */
	if (SCpnt->device->simple_tags && SCpnt->cmnd[0] != REQUEST_SENSE &&
	    SCpnt->cmnd[0] != INQUIRY) {
	    SCpnt->device->current_tag += 1;
		if (SCpnt->device->current_tag == 0)
		    SCpnt->device->current_tag = 1;
			SCpnt->tag = SCpnt->device->current_tag;
	} else
#endif
		set_bit(SCpnt->device->id * 8 +
			(u8)(SCpnt->device->lun & 0x7), info->busyluns);

	info->stats.removes += 1;
	switch (SCpnt->cmnd[0]) {
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
		info->stats.writes += 1;
		break;
	case READ_6:
	case READ_10:
	case READ_12:
		info->stats.reads += 1;
		break;
	default:
		info->stats.miscs += 1;
		break;
	}
}

static void fas216_do_bus_device_reset(FAS216_Info *info,
				       struct scsi_cmnd *SCpnt)
{
	struct message *msg;

	/*
	 * claim host busy
	 */
	info->scsi.phase = PHASE_SELECTION;
	info->scsi.SCp = SCpnt->SCp;
	info->SCpnt = SCpnt;
	info->dma.transfer_type = fasdma_none;

	fas216_log(info, LOG_ERROR, "sending bus device reset");

	msgqueue_flush(&info->scsi.msgs);
	msgqueue_addmsg(&info->scsi.msgs, 1, BUS_DEVICE_RESET);

	/* following what the ESP driver says */
	fas216_set_stc(info, 0);
	fas216_cmd(info, CMD_NOP | CMD_WITHDMA);

	/* flush FIFO */
	fas216_cmd(info, CMD_FLUSHFIFO);

	/* load bus-id and timeout */
	fas216_writeb(info, REG_SDID, BUSID(SCpnt->device->id));
	fas216_writeb(info, REG_STIM, info->ifcfg.select_timeout);

	/* synchronous transfers */
	fas216_set_sync(info, SCpnt->device->id);

	msg = msgqueue_getmsg(&info->scsi.msgs, 0);

	fas216_writeb(info, REG_FF, BUS_DEVICE_RESET);
	msg->fifo = 1;

	fas216_cmd(info, CMD_SELECTATNSTOP);
}

/**
 * fas216_kick - kick a command to the interface
 * @info: our host interface to kick
 *
 * Kick a command to the interface, interface should be idle.
 * Notes: Interrupts are always disabled!
 */
static void fas216_kick(FAS216_Info *info)
{
	struct scsi_cmnd *SCpnt = NULL;
#define TYPE_OTHER	0
#define TYPE_RESET	1
#define TYPE_QUEUE	2
	int where_from = TYPE_OTHER;

	fas216_checkmagic(info);

	/*
	 * Obtain the next command to process.
	 */
	do {
		if (info->rstSCpnt) {
			SCpnt = info->rstSCpnt;
			/* don't remove it */
			where_from = TYPE_RESET;
			break;
		}

		if (info->reqSCpnt) {
			SCpnt = info->reqSCpnt;
			info->reqSCpnt = NULL;
			break;
		}

		if (info->origSCpnt) {
			SCpnt = info->origSCpnt;
			info->origSCpnt = NULL;
			break;
		}

		/* retrieve next command */
		if (!SCpnt) {
			SCpnt = queue_remove_exclude(&info->queues.issue,
						     info->busyluns);
			where_from = TYPE_QUEUE;
			break;
		}
	} while (0);

	if (!SCpnt) {
		/*
		 * no command pending, so enable reselection.
		 */
		fas216_cmd(info, CMD_ENABLESEL);
		return;
	}

	/*
	 * We're going to start a command, so disable reselection
	 */
	fas216_cmd(info, CMD_DISABLESEL);

	if (info->scsi.disconnectable && info->SCpnt) {
		fas216_log(info, LOG_CONNECT,
			"moved command for %d to disconnected queue",
			info->SCpnt->device->id);
		queue_add_cmd_tail(&info->queues.disconnected, info->SCpnt);
		info->scsi.disconnectable = 0;
		info->SCpnt = NULL;
	}

	fas216_log_command(info, LOG_CONNECT | LOG_MESSAGES, SCpnt,
			   "starting");

	switch (where_from) {
	case TYPE_QUEUE:
		fas216_allocate_tag(info, SCpnt);
		fallthrough;
	case TYPE_OTHER:
		fas216_start_command(info, SCpnt);
		break;
	case TYPE_RESET:
		fas216_do_bus_device_reset(info, SCpnt);
		break;
	}

	fas216_log(info, LOG_CONNECT, "select: data pointers [%p, %X]",
		info->scsi.SCp.ptr, info->scsi.SCp.this_residual);

	/*
	 * should now get either DISCONNECT or
	 * (FUNCTION DONE with BUS SERVICE) interrupt
	 */
}

/*
 * Clean up from issuing a BUS DEVICE RESET message to a device.
 */
static void fas216_devicereset_done(FAS216_Info *info, struct scsi_cmnd *SCpnt,
				    unsigned int result)
{
	fas216_log(info, LOG_ERROR, "fas216 device reset complete");

	info->rstSCpnt = NULL;
	info->rst_dev_status = 1;
	wake_up(&info->eh_wait);
}

/**
 * fas216_rq_sns_done - Finish processing automatic request sense command
 * @info: interface that completed
 * @SCpnt: command that completed
 * @result: driver byte of result
 *
 * Finish processing automatic request sense command
 */
static void fas216_rq_sns_done(FAS216_Info *info, struct scsi_cmnd *SCpnt,
			       unsigned int result)
{
	fas216_log_target(info, LOG_CONNECT, SCpnt->device->id,
		   "request sense complete, result=0x%04x%02x%02x",
		   result, SCpnt->SCp.Message, SCpnt->SCp.Status);

	if (result != DID_OK || SCpnt->SCp.Status != GOOD)
		/*
		 * Something went wrong.  Make sure that we don't
		 * have valid data in the sense buffer that could
		 * confuse the higher levels.
		 */
		memset(SCpnt->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
//printk("scsi%d.%c: sense buffer: ", info->host->host_no, '0' + SCpnt->device->id);
//{ int i; for (i = 0; i < 32; i++) printk("%02x ", SCpnt->sense_buffer[i]); printk("\n"); }
	/*
	 * Note that we don't set SCpnt->result, since that should
	 * reflect the status of the command that we were asked by
	 * the upper layers to process.  This would have been set
	 * correctly by fas216_std_done.
	 */
	scsi_eh_restore_cmnd(SCpnt, &info->ses);
	SCpnt->scsi_done(SCpnt);
}

/**
 * fas216_std_done - finish processing of standard command
 * @info: interface that completed
 * @SCpnt: command that completed
 * @result: driver byte of result
 *
 * Finish processing of standard command
 */
static void
fas216_std_done(FAS216_Info *info, struct scsi_cmnd *SCpnt, unsigned int result)
{
	info->stats.fins += 1;

	SCpnt->result = result << 16 | info->scsi.SCp.Message << 8 |
			info->scsi.SCp.Status;

	fas216_log_command(info, LOG_CONNECT, SCpnt,
		"command complete, result=0x%08x", SCpnt->result);

	/*
	 * If the driver detected an error, we're all done.
	 */
	if (host_byte(SCpnt->result) != DID_OK ||
	    msg_byte(SCpnt->result) != COMMAND_COMPLETE)
		goto done;

	/*
	 * If the command returned CHECK_CONDITION or COMMAND_TERMINATED
	 * status, request the sense information.
	 */
	if (status_byte(SCpnt->result) == CHECK_CONDITION ||
	    status_byte(SCpnt->result) == COMMAND_TERMINATED)
		goto request_sense;

	/*
	 * If the command did not complete with GOOD status,
	 * we are all done here.
	 */
	if (status_byte(SCpnt->result) != GOOD)
		goto done;

	/*
	 * We have successfully completed a command.  Make sure that
	 * we do not have any buffers left to transfer.  The world
	 * is not perfect, and we seem to occasionally hit this.
	 * It can be indicative of a buggy driver, target or the upper
	 * levels of the SCSI code.
	 */
	if (info->scsi.SCp.ptr) {
		switch (SCpnt->cmnd[0]) {
		case INQUIRY:
		case START_STOP:
		case MODE_SENSE:
			break;

		default:
			scmd_printk(KERN_ERR, SCpnt,
				    "incomplete data transfer detected: res=%08X ptr=%p len=%X\n",
				    SCpnt->result, info->scsi.SCp.ptr,
				    info->scsi.SCp.this_residual);
			scsi_print_command(SCpnt);
			set_host_byte(SCpnt, DID_ERROR);
			goto request_sense;
		}
	}

done:
	if (SCpnt->scsi_done) {
		SCpnt->scsi_done(SCpnt);
		return;
	}

	panic("scsi%d.H: null scsi_done function in fas216_done",
		info->host->host_no);


request_sense:
	if (SCpnt->cmnd[0] == REQUEST_SENSE)
		goto done;

	scsi_eh_prep_cmnd(SCpnt, &info->ses, NULL, 0, ~0);
	fas216_log_target(info, LOG_CONNECT, SCpnt->device->id,
			  "requesting sense");
	init_SCp(SCpnt);
	SCpnt->SCp.Message = 0;
	SCpnt->SCp.Status = 0;
	SCpnt->tag = 0;
	SCpnt->host_scribble = (void *)fas216_rq_sns_done;

	/*
	 * Place this command into the high priority "request
	 * sense" slot.  This will be the very next command
	 * executed, unless a target connects to us.
	 */
	if (info->reqSCpnt)
		printk(KERN_WARNING "scsi%d.%c: losing request command\n",
			info->host->host_no, '0' + SCpnt->device->id);
	info->reqSCpnt = SCpnt;
}

/**
 * fas216_done - complete processing for current command
 * @info: interface that completed
 * @result: driver byte of result
 *
 * Complete processing for current command
 */
static void fas216_done(FAS216_Info *info, unsigned int result)
{
	void (*fn)(FAS216_Info *, struct scsi_cmnd *, unsigned int);
	struct scsi_cmnd *SCpnt;
	unsigned long flags;

	fas216_checkmagic(info);

	if (!info->SCpnt)
		goto no_command;

	SCpnt = info->SCpnt;
	info->SCpnt = NULL;
    	info->scsi.phase = PHASE_IDLE;

	if (info->scsi.aborting) {
		fas216_log(info, 0, "uncaught abort - returning DID_ABORT");
		result = DID_ABORT;
		info->scsi.aborting = 0;
	}

	/*
	 * Sanity check the completion - if we have zero bytes left
	 * to transfer, we should not have a valid pointer.
	 */
	if (info->scsi.SCp.ptr && info->scsi.SCp.this_residual == 0) {
		scmd_printk(KERN_INFO, SCpnt,
			    "zero bytes left to transfer, but buffer pointer still valid: ptr=%p len=%08x\n",
			    info->scsi.SCp.ptr, info->scsi.SCp.this_residual);
		info->scsi.SCp.ptr = NULL;
		scsi_print_command(SCpnt);
	}

	/*
	 * Clear down this command as completed.  If we need to request
	 * the sense information, fas216_kick will re-assert the busy
	 * status.
	 */
	info->device[SCpnt->device->id].parity_check = 0;
	clear_bit(SCpnt->device->id * 8 +
		  (u8)(SCpnt->device->lun & 0x7), info->busyluns);

	fn = (void (*)(FAS216_Info *, struct scsi_cmnd *, unsigned int))SCpnt->host_scribble;
	fn(info, SCpnt, result);

	if (info->scsi.irq) {
		spin_lock_irqsave(&info->host_lock, flags);
		if (info->scsi.phase == PHASE_IDLE)
			fas216_kick(info);
		spin_unlock_irqrestore(&info->host_lock, flags);
	}
	return;

no_command:
	panic("scsi%d.H: null command in fas216_done",
		info->host->host_no);
}

/**
 * fas216_queue_command - queue a command for adapter to process.
 * @SCpnt: Command to queue
 * @done: done function to call once command is complete
 *
 * Queue a command for adapter to process.
 * Returns: 0 on success, else error.
 * Notes: io_request_lock is held, interrupts are disabled.
 */
static int fas216_queue_command_lck(struct scsi_cmnd *SCpnt,
			 void (*done)(struct scsi_cmnd *))
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;
	int result;

	fas216_checkmagic(info);

	fas216_log_command(info, LOG_CONNECT, SCpnt,
			   "received command (%p)", SCpnt);

	SCpnt->scsi_done = done;
	SCpnt->host_scribble = (void *)fas216_std_done;
	SCpnt->result = 0;

	init_SCp(SCpnt);

	info->stats.queues += 1;
	SCpnt->tag = 0;

	spin_lock(&info->host_lock);

	/*
	 * Add command into execute queue and let it complete under
	 * whatever scheme we're using.
	 */
	result = !queue_add_cmd_ordered(&info->queues.issue, SCpnt);

	/*
	 * If we successfully added the command,
	 * kick the interface to get it moving.
	 */
	if (result == 0 && info->scsi.phase == PHASE_IDLE)
		fas216_kick(info);
	spin_unlock(&info->host_lock);

	fas216_log_target(info, LOG_CONNECT, -1, "queue %s",
		result ? "failure" : "success");

	return result;
}

DEF_SCSI_QCMD(fas216_queue_command)

/**
 * fas216_internal_done - trigger restart of a waiting thread in fas216_noqueue_command
 * @SCpnt: Command to wake
 *
 * Trigger restart of a waiting thread in fas216_command
 */
static void fas216_internal_done(struct scsi_cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;

	fas216_checkmagic(info);

	info->internal_done = 1;
}

/**
 * fas216_noqueue_command - process a command for the adapter.
 * @SCpnt: Command to queue
 *
 * Queue a command for adapter to process.
 * Returns: scsi result code.
 * Notes: io_request_lock is held, interrupts are disabled.
 */
static int fas216_noqueue_command_lck(struct scsi_cmnd *SCpnt,
			   void (*done)(struct scsi_cmnd *))
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;

	fas216_checkmagic(info);

	/*
	 * We should only be using this if we don't have an interrupt.
	 * Provide some "incentive" to use the queueing code.
	 */
	BUG_ON(info->scsi.irq);

	info->internal_done = 0;
	fas216_queue_command_lck(SCpnt, fas216_internal_done);

	/*
	 * This wastes time, since we can't return until the command is
	 * complete. We can't sleep either since we may get re-entered!
	 * However, we must re-enable interrupts, or else we'll be
	 * waiting forever.
	 */
	spin_unlock_irq(info->host->host_lock);

	while (!info->internal_done) {
		/*
		 * If we don't have an IRQ, then we must poll the card for
		 * it's interrupt, and use that to call this driver's
		 * interrupt routine.  That way, we keep the command
		 * progressing.  Maybe we can add some intelligence here
		 * and go to sleep if we know that the device is going
		 * to be some time (eg, disconnected).
		 */
		if (fas216_readb(info, REG_STAT) & STAT_INT) {
			spin_lock_irq(info->host->host_lock);
			fas216_intr(info);
			spin_unlock_irq(info->host->host_lock);
		}
	}

	spin_lock_irq(info->host->host_lock);

	done(SCpnt);

	return 0;
}

DEF_SCSI_QCMD(fas216_noqueue_command)

/*
 * Error handler timeout function.  Indicate that we timed out,
 * and wake up any error handler process so it can continue.
 */
static void fas216_eh_timer(struct timer_list *t)
{
	FAS216_Info *info = from_timer(info, t, eh_timer);

	fas216_log(info, LOG_ERROR, "error handling timed out\n");

	del_timer(&info->eh_timer);

	if (info->rst_bus_status == 0)
		info->rst_bus_status = -1;
	if (info->rst_dev_status == 0)
		info->rst_dev_status = -1;

	wake_up(&info->eh_wait);
}

enum res_find {
	res_failed,		/* not found			*/
	res_success,		/* command on issue queue	*/
	res_hw_abort		/* command on disconnected dev	*/
};

/**
 * fas216_do_abort - decide how to abort a command
 * @SCpnt: command to abort
 *
 * Decide how to abort a command.
 * Returns: abort status
 */
static enum res_find fas216_find_command(FAS216_Info *info,
					 struct scsi_cmnd *SCpnt)
{
	enum res_find res = res_failed;

	if (queue_remove_cmd(&info->queues.issue, SCpnt)) {
		/*
		 * The command was on the issue queue, and has not been
		 * issued yet.  We can remove the command from the queue,
		 * and acknowledge the abort.  Neither the device nor the
		 * interface know about the command.
		 */
		printk("on issue queue ");

		res = res_success;
	} else if (queue_remove_cmd(&info->queues.disconnected, SCpnt)) {
		/*
		 * The command was on the disconnected queue.  We must
		 * reconnect with the device if possible, and send it
		 * an abort message.
		 */
		printk("on disconnected queue ");

		res = res_hw_abort;
	} else if (info->SCpnt == SCpnt) {
		printk("executing ");

		switch (info->scsi.phase) {
		/*
		 * If the interface is idle, and the command is 'disconnectable',
		 * then it is the same as on the disconnected queue.
		 */
		case PHASE_IDLE:
			if (info->scsi.disconnectable) {
				info->scsi.disconnectable = 0;
				info->SCpnt = NULL;
				res = res_hw_abort;
			}
			break;

		default:
			break;
		}
	} else if (info->origSCpnt == SCpnt) {
		/*
		 * The command will be executed next, but a command
		 * is currently using the interface.  This is similar to
		 * being on the issue queue, except the busylun bit has
		 * been set.
		 */
		info->origSCpnt = NULL;
		clear_bit(SCpnt->device->id * 8 +
			  (u8)(SCpnt->device->lun & 0x7), info->busyluns);
		printk("waiting for execution ");
		res = res_success;
	} else
		printk("unknown ");

	return res;
}

/**
 * fas216_eh_abort - abort this command
 * @SCpnt: command to abort
 *
 * Abort this command.
 * Returns: FAILED if unable to abort
 * Notes: io_request_lock is taken, and irqs are disabled
 */
int fas216_eh_abort(struct scsi_cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;
	int result = FAILED;

	fas216_checkmagic(info);

	info->stats.aborts += 1;

	scmd_printk(KERN_WARNING, SCpnt, "abort command\n");

	print_debug_list();
	fas216_dumpstate(info);

	switch (fas216_find_command(info, SCpnt)) {
	/*
	 * We found the command, and cleared it out.  Either
	 * the command is still known to be executing on the
	 * target, or the busylun bit is not set.
	 */
	case res_success:
		scmd_printk(KERN_WARNING, SCpnt, "abort %p success\n", SCpnt);
		result = SUCCESS;
		break;

	/*
	 * We need to reconnect to the target and send it an
	 * ABORT or ABORT_TAG message.  We can only do this
	 * if the bus is free.
	 */
	case res_hw_abort:

	/*
	 * We are unable to abort the command for some reason.
	 */
	default:
	case res_failed:
		scmd_printk(KERN_WARNING, SCpnt, "abort %p failed\n", SCpnt);
		break;
	}

	return result;
}

/**
 * fas216_eh_device_reset - Reset the device associated with this command
 * @SCpnt: command specifing device to reset
 *
 * Reset the device associated with this command.
 * Returns: FAILED if unable to reset.
 * Notes: We won't be re-entered, so we'll only have one device
 * reset on the go at one time.
 */
int fas216_eh_device_reset(struct scsi_cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;
	unsigned long flags;
	int i, res = FAILED, target = SCpnt->device->id;

	fas216_log(info, LOG_ERROR, "device reset for target %d", target);

	spin_lock_irqsave(&info->host_lock, flags);

	do {
		/*
		 * If we are currently connected to a device, and
		 * it is the device we want to reset, there is
		 * nothing we can do here.  Chances are it is stuck,
		 * and we need a bus reset.
		 */
		if (info->SCpnt && !info->scsi.disconnectable &&
		    info->SCpnt->device->id == SCpnt->device->id)
			break;

		/*
		 * We're going to be resetting this device.  Remove
		 * all pending commands from the driver.  By doing
		 * so, we guarantee that we won't touch the command
		 * structures except to process the reset request.
		 */
		queue_remove_all_target(&info->queues.issue, target);
		queue_remove_all_target(&info->queues.disconnected, target);
		if (info->origSCpnt && info->origSCpnt->device->id == target)
			info->origSCpnt = NULL;
		if (info->reqSCpnt && info->reqSCpnt->device->id == target)
			info->reqSCpnt = NULL;
		for (i = 0; i < 8; i++)
			clear_bit(target * 8 + i, info->busyluns);

		/*
		 * Hijack this SCSI command structure to send
		 * a bus device reset message to this device.
		 */
		SCpnt->host_scribble = (void *)fas216_devicereset_done;

		info->rst_dev_status = 0;
		info->rstSCpnt = SCpnt;

		if (info->scsi.phase == PHASE_IDLE)
			fas216_kick(info);

		mod_timer(&info->eh_timer, jiffies + 30 * HZ);
		spin_unlock_irqrestore(&info->host_lock, flags);

		/*
		 * Wait up to 30 seconds for the reset to complete.
		 */
		wait_event(info->eh_wait, info->rst_dev_status);

		del_timer_sync(&info->eh_timer);
		spin_lock_irqsave(&info->host_lock, flags);
		info->rstSCpnt = NULL;

		if (info->rst_dev_status == 1)
			res = SUCCESS;
	} while (0);

	SCpnt->host_scribble = NULL;
	spin_unlock_irqrestore(&info->host_lock, flags);

	fas216_log(info, LOG_ERROR, "device reset complete: %s\n",
		   res == SUCCESS ? "success" : "failed");

	return res;
}

/**
 * fas216_eh_bus_reset - Reset the bus associated with the command
 * @SCpnt: command specifing bus to reset
 *
 * Reset the bus associated with the command.
 * Returns: FAILED if unable to reset.
 * Notes: Further commands are blocked.
 */
int fas216_eh_bus_reset(struct scsi_cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;
	unsigned long flags;
	struct scsi_device *SDpnt;

	fas216_checkmagic(info);
	fas216_log(info, LOG_ERROR, "resetting bus");

	info->stats.bus_resets += 1;

	spin_lock_irqsave(&info->host_lock, flags);

	/*
	 * Stop all activity on this interface.
	 */
	fas216_aborttransfer(info);
	fas216_writeb(info, REG_CNTL3, info->scsi.cfg[2]);

	/*
	 * Clear any pending interrupts.
	 */
	while (fas216_readb(info, REG_STAT) & STAT_INT)
		fas216_readb(info, REG_INST);

	info->rst_bus_status = 0;

	/*
	 * For each attached hard-reset device, clear out
	 * all command structures.  Leave the running
	 * command in place.
	 */
	shost_for_each_device(SDpnt, info->host) {
		int i;

		if (SDpnt->soft_reset)
			continue;

		queue_remove_all_target(&info->queues.issue, SDpnt->id);
		queue_remove_all_target(&info->queues.disconnected, SDpnt->id);
		if (info->origSCpnt && info->origSCpnt->device->id == SDpnt->id)
			info->origSCpnt = NULL;
		if (info->reqSCpnt && info->reqSCpnt->device->id == SDpnt->id)
			info->reqSCpnt = NULL;
		info->SCpnt = NULL;

		for (i = 0; i < 8; i++)
			clear_bit(SDpnt->id * 8 + i, info->busyluns);
	}

	info->scsi.phase = PHASE_IDLE;

	/*
	 * Reset the SCSI bus.  Device cleanup happens in
	 * the interrupt handler.
	 */
	fas216_cmd(info, CMD_RESETSCSI);

	mod_timer(&info->eh_timer, jiffies + HZ);
	spin_unlock_irqrestore(&info->host_lock, flags);

	/*
	 * Wait one second for the interrupt.
	 */
	wait_event(info->eh_wait, info->rst_bus_status);
	del_timer_sync(&info->eh_timer);

	fas216_log(info, LOG_ERROR, "bus reset complete: %s\n",
		   info->rst_bus_status == 1 ? "success" : "failed");

	return info->rst_bus_status == 1 ? SUCCESS : FAILED;
}

/**
 * fas216_init_chip - Initialise FAS216 state after reset
 * @info: state structure for interface
 *
 * Initialise FAS216 state after reset
 */
static void fas216_init_chip(FAS216_Info *info)
{
	unsigned int clock = ((info->ifcfg.clockrate - 1) / 5 + 1) & 7;
	fas216_writeb(info, REG_CLKF, clock);
	fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0]);
	fas216_writeb(info, REG_CNTL2, info->scsi.cfg[1]);
	fas216_writeb(info, REG_CNTL3, info->scsi.cfg[2]);
	fas216_writeb(info, REG_STIM, info->ifcfg.select_timeout);
	fas216_writeb(info, REG_SOF, 0);
	fas216_writeb(info, REG_STP, info->scsi.async_stp);
	fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0]);
}

/**
 * fas216_eh_host_reset - Reset the host associated with this command
 * @SCpnt: command specifing host to reset
 *
 * Reset the host associated with this command.
 * Returns: FAILED if unable to reset.
 * Notes: io_request_lock is taken, and irqs are disabled
 */
int fas216_eh_host_reset(struct scsi_cmnd *SCpnt)
{
	FAS216_Info *info = (FAS216_Info *)SCpnt->device->host->hostdata;

	spin_lock_irq(info->host->host_lock);

	fas216_checkmagic(info);

	fas216_log(info, LOG_ERROR, "resetting host");

	/*
	 * Reset the SCSI chip.
	 */
	fas216_cmd(info, CMD_RESETCHIP);

	/*
	 * Ugly ugly ugly!
	 * We need to release the host_lock and enable
	 * IRQs if we sleep, but we must relock and disable
	 * IRQs after the sleep.
	 */
	spin_unlock_irq(info->host->host_lock);
	msleep(50 * 1000/100);
	spin_lock_irq(info->host->host_lock);

	/*
	 * Release the SCSI reset.
	 */
	fas216_cmd(info, CMD_NOP);

	fas216_init_chip(info);

	spin_unlock_irq(info->host->host_lock);
	return SUCCESS;
}

#define TYPE_UNKNOWN	0
#define TYPE_NCR53C90	1
#define TYPE_NCR53C90A	2
#define TYPE_NCR53C9x	3
#define TYPE_Am53CF94	4
#define TYPE_EmFAS216	5
#define TYPE_QLFAS216	6

static char *chip_types[] = {
	"unknown",
	"NS NCR53C90",
	"NS NCR53C90A",
	"NS NCR53C9x",
	"AMD Am53CF94",
	"Emulex FAS216",
	"QLogic FAS216"
};

static int fas216_detect_type(FAS216_Info *info)
{
	int family, rev;

	/*
	 * Reset the chip.
	 */
	fas216_writeb(info, REG_CMD, CMD_RESETCHIP);
	udelay(50);
	fas216_writeb(info, REG_CMD, CMD_NOP);

	/*
	 * Check to see if control reg 2 is present.
	 */
	fas216_writeb(info, REG_CNTL3, 0);
	fas216_writeb(info, REG_CNTL2, CNTL2_S2FE);

	/*
	 * If we are unable to read back control reg 2
	 * correctly, it is not present, and we have a
	 * NCR53C90.
	 */
	if ((fas216_readb(info, REG_CNTL2) & (~0xe0)) != CNTL2_S2FE)
		return TYPE_NCR53C90;

	/*
	 * Now, check control register 3
	 */
	fas216_writeb(info, REG_CNTL2, 0);
	fas216_writeb(info, REG_CNTL3, 0);
	fas216_writeb(info, REG_CNTL3, 5);

	/*
	 * If we are unable to read the register back
	 * correctly, we have a NCR53C90A
	 */
	if (fas216_readb(info, REG_CNTL3) != 5)
		return TYPE_NCR53C90A;

	/*
	 * Now read the ID from the chip.
	 */
	fas216_writeb(info, REG_CNTL3, 0);

	fas216_writeb(info, REG_CNTL3, CNTL3_ADIDCHK);
	fas216_writeb(info, REG_CNTL3, 0);

	fas216_writeb(info, REG_CMD, CMD_RESETCHIP);
	udelay(50);
	fas216_writeb(info, REG_CMD, CMD_WITHDMA | CMD_NOP);

	fas216_writeb(info, REG_CNTL2, CNTL2_ENF);
	fas216_writeb(info, REG_CMD, CMD_RESETCHIP);
	udelay(50);
	fas216_writeb(info, REG_CMD, CMD_NOP);

	rev     = fas216_readb(info, REG_ID);
	family  = rev >> 3;
	rev    &= 7;

	switch (family) {
	case 0x01:
		if (rev == 4)
			return TYPE_Am53CF94;
		break;

	case 0x02:
		switch (rev) {
		case 2:
			return TYPE_EmFAS216;
		case 3:
			return TYPE_QLFAS216;
		}
		break;

	default:
		break;
	}
	printk("family %x rev %x\n", family, rev);
	return TYPE_NCR53C9x;
}

/**
 * fas216_reset_state - Initialise driver internal state
 * @info: state to initialise
 *
 * Initialise driver internal state
 */
static void fas216_reset_state(FAS216_Info *info)
{
	int i;

	fas216_checkmagic(info);

	fas216_bus_reset(info);

	/*
	 * Clear out all stale info in our state structure
	 */
	memset(info->busyluns, 0, sizeof(info->busyluns));
	info->scsi.disconnectable = 0;
	info->scsi.aborting = 0;

	for (i = 0; i < 8; i++) {
		info->device[i].parity_enabled	= 0;
		info->device[i].parity_check	= 1;
	}

	/*
	 * Drain all commands on disconnected queue
	 */
	while (queue_remove(&info->queues.disconnected) != NULL);

	/*
	 * Remove executing commands.
	 */
	info->SCpnt     = NULL;
	info->reqSCpnt  = NULL;
	info->rstSCpnt  = NULL;
	info->origSCpnt = NULL;
}

/**
 * fas216_init - initialise FAS/NCR/AMD SCSI structures.
 * @host: a driver-specific filled-out structure
 *
 * Initialise FAS/NCR/AMD SCSI structures.
 * Returns: 0 on success
 */
int fas216_init(struct Scsi_Host *host)
{
	FAS216_Info *info = (FAS216_Info *)host->hostdata;

	info->magic_start    = MAGIC;
	info->magic_end      = MAGIC;
	info->host           = host;
	info->scsi.cfg[0]    = host->this_id | CNTL1_PERE;
	info->scsi.cfg[1]    = CNTL2_ENF | CNTL2_S2FE;
	info->scsi.cfg[2]    = info->ifcfg.cntl3 |
			       CNTL3_ADIDCHK | CNTL3_QTAG | CNTL3_G2CB | CNTL3_LBTM;
	info->scsi.async_stp = fas216_syncperiod(info, info->ifcfg.asyncperiod);

	info->rst_dev_status = -1;
	info->rst_bus_status = -1;
	init_waitqueue_head(&info->eh_wait);
	timer_setup(&info->eh_timer, fas216_eh_timer, 0);
	
	spin_lock_init(&info->host_lock);

	memset(&info->stats, 0, sizeof(info->stats));

	msgqueue_initialise(&info->scsi.msgs);

	if (!queue_initialise(&info->queues.issue))
		return -ENOMEM;

	if (!queue_initialise(&info->queues.disconnected)) {
		queue_free(&info->queues.issue);
		return -ENOMEM;
	}

	return 0;
}

/**
 * fas216_add - initialise FAS/NCR/AMD SCSI ic.
 * @host: a driver-specific filled-out structure
 * @dev: parent device
 *
 * Initialise FAS/NCR/AMD SCSI ic.
 * Returns: 0 on success
 */
int fas216_add(struct Scsi_Host *host, struct device *dev)
{
	FAS216_Info *info = (FAS216_Info *)host->hostdata;
	int type, ret;

	if (info->ifcfg.clockrate <= 10 || info->ifcfg.clockrate > 40) {
		printk(KERN_CRIT "fas216: invalid clock rate %u MHz\n",
			info->ifcfg.clockrate);
		return -EINVAL;
	}

	fas216_reset_state(info);
	type = fas216_detect_type(info);
	info->scsi.type = chip_types[type];

	udelay(300);

	/*
	 * Initialise the chip correctly.
	 */
	fas216_init_chip(info);

	/*
	 * Reset the SCSI bus.  We don't want to see
	 * the resulting reset interrupt, so mask it
	 * out.
	 */
	fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0] | CNTL1_DISR);
	fas216_writeb(info, REG_CMD, CMD_RESETSCSI);

	/*
	 * scsi standard says wait 250ms
	 */
	spin_unlock_irq(info->host->host_lock);
	msleep(100*1000/100);
	spin_lock_irq(info->host->host_lock);

	fas216_writeb(info, REG_CNTL1, info->scsi.cfg[0]);
	fas216_readb(info, REG_INST);

	fas216_checkmagic(info);

	ret = scsi_add_host(host, dev);
	if (ret)
		fas216_writeb(info, REG_CMD, CMD_RESETCHIP);
	else
		scsi_scan_host(host);

	return ret;
}

void fas216_remove(struct Scsi_Host *host)
{
	FAS216_Info *info = (FAS216_Info *)host->hostdata;

	fas216_checkmagic(info);
	scsi_remove_host(host);

	fas216_writeb(info, REG_CMD, CMD_RESETCHIP);
	scsi_host_put(host);
}

/**
 * fas216_release - release all resources for FAS/NCR/AMD SCSI ic.
 * @host: a driver-specific filled-out structure
 *
 * release all resources and put everything to bed for FAS/NCR/AMD SCSI ic.
 */
void fas216_release(struct Scsi_Host *host)
{
	FAS216_Info *info = (FAS216_Info *)host->hostdata;

	queue_free(&info->queues.disconnected);
	queue_free(&info->queues.issue);
}

void fas216_print_host(FAS216_Info *info, struct seq_file *m)
{
	seq_printf(m,
			"\n"
			"Chip    : %s\n"
			" Address: 0x%p\n"
			" IRQ    : %d\n"
			" DMA    : %d\n",
			info->scsi.type, info->scsi.io_base,
			info->scsi.irq, info->scsi.dma);
}

void fas216_print_stats(FAS216_Info *info, struct seq_file *m)
{
	seq_printf(m, "\n"
			"Command Statistics:\n"
			" Queued     : %u\n"
			" Issued     : %u\n"
			" Completed  : %u\n"
			" Reads      : %u\n"
			" Writes     : %u\n"
			" Others     : %u\n"
			" Disconnects: %u\n"
			" Aborts     : %u\n"
			" Bus resets : %u\n"
			" Host resets: %u\n",
			info->stats.queues,	 info->stats.removes,
			info->stats.fins,	 info->stats.reads,
			info->stats.writes,	 info->stats.miscs,
			info->stats.disconnects, info->stats.aborts,
			info->stats.bus_resets,	 info->stats.host_resets);
}

void fas216_print_devices(FAS216_Info *info, struct seq_file *m)
{
	struct fas216_device *dev;
	struct scsi_device *scd;

	seq_puts(m, "Device/Lun TaggedQ       Parity   Sync\n");

	shost_for_each_device(scd, info->host) {
		dev = &info->device[scd->id];
		seq_printf(m, "     %d/%llu   ", scd->id, scd->lun);
		if (scd->tagged_supported)
			seq_printf(m, "%3sabled(%3d) ",
				     scd->simple_tags ? "en" : "dis",
				     scd->current_tag);
		else
			seq_puts(m, "unsupported   ");

		seq_printf(m, "%3sabled ", dev->parity_enabled ? "en" : "dis");

		if (dev->sof)
			seq_printf(m, "offset %d, %d ns\n",
				     dev->sof, dev->period * 4);
		else
			seq_puts(m, "async\n");
	}
}

EXPORT_SYMBOL(fas216_init);
EXPORT_SYMBOL(fas216_add);
EXPORT_SYMBOL(fas216_queue_command);
EXPORT_SYMBOL(fas216_noqueue_command);
EXPORT_SYMBOL(fas216_intr);
EXPORT_SYMBOL(fas216_remove);
EXPORT_SYMBOL(fas216_release);
EXPORT_SYMBOL(fas216_eh_abort);
EXPORT_SYMBOL(fas216_eh_device_reset);
EXPORT_SYMBOL(fas216_eh_bus_reset);
EXPORT_SYMBOL(fas216_eh_host_reset);
EXPORT_SYMBOL(fas216_print_host);
EXPORT_SYMBOL(fas216_print_stats);
EXPORT_SYMBOL(fas216_print_devices);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Generic FAS216/NCR53C9x driver core");
MODULE_LICENSE("GPL");
