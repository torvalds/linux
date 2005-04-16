/*
 *    in2000.c -  Linux device driver for the
 *                Always IN2000 ISA SCSI card.
 *
 * Copyright (c) 1996 John Shifflett, GeoLog Consulting
 *    john@geolog.com
 *    jshiffle@netcom.com
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
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
 *
 * Drew Eckhardt's excellent 'Generic NCR5380' sources provided
 * much of the inspiration and some of the code for this driver.
 * The Linux IN2000 driver distributed in the Linux kernels through
 * version 1.2.13 was an extremely valuable reference on the arcane
 * (and still mysterious) workings of the IN2000's fifo. It also
 * is where I lifted in2000_biosparam(), the gist of the card
 * detection scheme, and other bits of code. Many thanks to the
 * talented and courageous people who wrote, contributed to, and
 * maintained that driver (including Brad McLean, Shaun Savage,
 * Bill Earnest, Larry Doolittle, Roger Sunshine, John Luckey,
 * Matt Postiff, Peter Lu, zerucha@shell.portal.com, and Eric
 * Youngdale). I should also mention the driver written by
 * Hamish Macdonald for the (GASP!) Amiga A2091 card, included
 * in the Linux-m68k distribution; it gave me a good initial
 * understanding of the proper way to run a WD33c93 chip, and I
 * ended up stealing lots of code from it.
 *
 * _This_ driver is (I feel) an improvement over the old one in
 * several respects:
 *    -  All problems relating to the data size of a SCSI request are
 *          gone (as far as I know). The old driver couldn't handle
 *          swapping to partitions because that involved 4k blocks, nor
 *          could it deal with the st.c tape driver unmodified, because
 *          that usually involved 4k - 32k blocks. The old driver never
 *          quite got away from a morbid dependence on 2k block sizes -
 *          which of course is the size of the card's fifo.
 *
 *    -  Target Disconnection/Reconnection is now supported. Any
 *          system with more than one device active on the SCSI bus
 *          will benefit from this. The driver defaults to what I'm
 *          calling 'adaptive disconnect' - meaning that each command
 *          is evaluated individually as to whether or not it should
 *          be run with the option to disconnect/reselect (if the
 *          device chooses), or as a "SCSI-bus-hog".
 *
 *    -  Synchronous data transfers are now supported. Because there
 *          are a few devices (and many improperly terminated systems)
 *          that choke when doing sync, the default is sync DISABLED
 *          for all devices. This faster protocol can (and should!)
 *          be enabled on selected devices via the command-line.
 *
 *    -  Runtime operating parameters can now be specified through
 *       either the LILO or the 'insmod' command line. For LILO do:
 *          "in2000=blah,blah,blah"
 *       and with insmod go like:
 *          "insmod /usr/src/linux/modules/in2000.o setup_strings=blah,blah"
 *       The defaults should be good for most people. See the comment
 *       for 'setup_strings' below for more details.
 *
 *    -  The old driver relied exclusively on what the Western Digital
 *          docs call "Combination Level 2 Commands", which are a great
 *          idea in that the CPU is relieved of a lot of interrupt
 *          overhead. However, by accepting a certain (user-settable)
 *          amount of additional interrupts, this driver achieves
 *          better control over the SCSI bus, and data transfers are
 *          almost as fast while being much easier to define, track,
 *          and debug.
 *
 *    -  You can force detection of a card whose BIOS has been disabled.
 *
 *    -  Multiple IN2000 cards might almost be supported. I've tried to
 *       keep it in mind, but have no way to test...
 *
 *
 * TODO:
 *       tagged queuing. multiple cards.
 *
 *
 * NOTE:
 *       When using this or any other SCSI driver as a module, you'll
 *       find that with the stock kernel, at most _two_ SCSI hard
 *       drives will be linked into the device list (ie, usable).
 *       If your IN2000 card has more than 2 disks on its bus, you
 *       might want to change the define of 'SD_EXTRA_DEVS' in the
 *       'hosts.h' file from 2 to whatever is appropriate. It took
 *       me a while to track down this surprisingly obscure and
 *       undocumented little "feature".
 *
 *
 * People with bug reports, wish-lists, complaints, comments,
 * or improvements are asked to pah-leeez email me (John Shifflett)
 * at john@geolog.com or jshiffle@netcom.com! I'm anxious to get
 * this thing into as good a shape as possible, and I'm positive
 * there are lots of lurking bugs and "Stupid Places".
 *
 * Updated for Linux 2.5 by Alan Cox <alan@redhat.com>
 *	- Using new_eh handler
 *	- Hopefully got all the locking right again
 *	See "FIXME" notes for items that could do with more work
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/stat.h>

#include <asm/io.h>
#include <asm/system.h>

#include "scsi.h"
#include <scsi/scsi_host.h>

#define IN2000_VERSION    "1.33-2.5"
#define IN2000_DATE       "2002/11/03"

#include "in2000.h"


/*
 * 'setup_strings' is a single string used to pass operating parameters and
 * settings from the kernel/module command-line to the driver. 'setup_args[]'
 * is an array of strings that define the compile-time default values for
 * these settings. If Linux boots with a LILO or insmod command-line, those
 * settings are combined with 'setup_args[]'. Note that LILO command-lines
 * are prefixed with "in2000=" while insmod uses a "setup_strings=" prefix.
 * The driver recognizes the following keywords (lower case required) and
 * arguments:
 *
 * -  ioport:addr    -Where addr is IO address of a (usually ROM-less) card.
 * -  noreset        -No optional args. Prevents SCSI bus reset at boot time.
 * -  nosync:x       -x is a bitmask where the 1st 7 bits correspond with
 *                    the 7 possible SCSI devices (bit 0 for device #0, etc).
 *                    Set a bit to PREVENT sync negotiation on that device.
 *                    The driver default is sync DISABLED on all devices.
 * -  period:ns      -ns is the minimum # of nanoseconds in a SCSI data transfer
 *                    period. Default is 500; acceptable values are 250 - 1000.
 * -  disconnect:x   -x = 0 to never allow disconnects, 2 to always allow them.
 *                    x = 1 does 'adaptive' disconnects, which is the default
 *                    and generally the best choice.
 * -  debug:x        -If 'DEBUGGING_ON' is defined, x is a bitmask that causes
 *                    various types of debug output to printed - see the DB_xxx
 *                    defines in in2000.h
 * -  proc:x         -If 'PROC_INTERFACE' is defined, x is a bitmask that
 *                    determines how the /proc interface works and what it
 *                    does - see the PR_xxx defines in in2000.h
 *
 * Syntax Notes:
 * -  Numeric arguments can be decimal or the '0x' form of hex notation. There
 *    _must_ be a colon between a keyword and its numeric argument, with no
 *    spaces.
 * -  Keywords are separated by commas, no spaces, in the standard kernel
 *    command-line manner.
 * -  A keyword in the 'nth' comma-separated command-line member will overwrite
 *    the 'nth' element of setup_args[]. A blank command-line member (in
 *    other words, a comma with no preceding keyword) will _not_ overwrite
 *    the corresponding setup_args[] element.
 *
 * A few LILO examples (for insmod, use 'setup_strings' instead of 'in2000'):
 * -  in2000=ioport:0x220,noreset
 * -  in2000=period:250,disconnect:2,nosync:0x03
 * -  in2000=debug:0x1e
 * -  in2000=proc:3
 */

/* Normally, no defaults are specified... */
static char *setup_args[] = { "", "", "", "", "", "", "", "", "" };

/* filled in by 'insmod' */
static char *setup_strings;

module_param(setup_strings, charp, 0);

static inline uchar read_3393(struct IN2000_hostdata *hostdata, uchar reg_num)
{
	write1_io(reg_num, IO_WD_ADDR);
	return read1_io(IO_WD_DATA);
}


#define READ_AUX_STAT() read1_io(IO_WD_ASR)


static inline void write_3393(struct IN2000_hostdata *hostdata, uchar reg_num, uchar value)
{
	write1_io(reg_num, IO_WD_ADDR);
	write1_io(value, IO_WD_DATA);
}


static inline void write_3393_cmd(struct IN2000_hostdata *hostdata, uchar cmd)
{
/*   while (READ_AUX_STAT() & ASR_CIP)
      printk("|");*/
	write1_io(WD_COMMAND, IO_WD_ADDR);
	write1_io(cmd, IO_WD_DATA);
}


static uchar read_1_byte(struct IN2000_hostdata *hostdata)
{
	uchar asr, x = 0;

	write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
	write_3393_cmd(hostdata, WD_CMD_TRANS_INFO | 0x80);
	do {
		asr = READ_AUX_STAT();
		if (asr & ASR_DBR)
			x = read_3393(hostdata, WD_DATA);
	} while (!(asr & ASR_INT));
	return x;
}


static void write_3393_count(struct IN2000_hostdata *hostdata, unsigned long value)
{
	write1_io(WD_TRANSFER_COUNT_MSB, IO_WD_ADDR);
	write1_io((value >> 16), IO_WD_DATA);
	write1_io((value >> 8), IO_WD_DATA);
	write1_io(value, IO_WD_DATA);
}


static unsigned long read_3393_count(struct IN2000_hostdata *hostdata)
{
	unsigned long value;

	write1_io(WD_TRANSFER_COUNT_MSB, IO_WD_ADDR);
	value = read1_io(IO_WD_DATA) << 16;
	value |= read1_io(IO_WD_DATA) << 8;
	value |= read1_io(IO_WD_DATA);
	return value;
}


/* The 33c93 needs to be told which direction a command transfers its
 * data; we use this function to figure it out. Returns true if there
 * will be a DATA_OUT phase with this command, false otherwise.
 * (Thanks to Joerg Dorchain for the research and suggestion.)
 */
static int is_dir_out(Scsi_Cmnd * cmd)
{
	switch (cmd->cmnd[0]) {
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case WRITE_LONG:
	case WRITE_SAME:
	case WRITE_BUFFER:
	case WRITE_VERIFY:
	case WRITE_VERIFY_12:
	case COMPARE:
	case COPY:
	case COPY_VERIFY:
	case SEARCH_EQUAL:
	case SEARCH_HIGH:
	case SEARCH_LOW:
	case SEARCH_EQUAL_12:
	case SEARCH_HIGH_12:
	case SEARCH_LOW_12:
	case FORMAT_UNIT:
	case REASSIGN_BLOCKS:
	case RESERVE:
	case MODE_SELECT:
	case MODE_SELECT_10:
	case LOG_SELECT:
	case SEND_DIAGNOSTIC:
	case CHANGE_DEFINITION:
	case UPDATE_BLOCK:
	case SET_WINDOW:
	case MEDIUM_SCAN:
	case SEND_VOLUME_TAG:
	case 0xea:
		return 1;
	default:
		return 0;
	}
}



static struct sx_period sx_table[] = {
	{1, 0x20},
	{252, 0x20},
	{376, 0x30},
	{500, 0x40},
	{624, 0x50},
	{752, 0x60},
	{876, 0x70},
	{1000, 0x00},
	{0, 0}
};

static int round_period(unsigned int period)
{
	int x;

	for (x = 1; sx_table[x].period_ns; x++) {
		if ((period <= sx_table[x - 0].period_ns) && (period > sx_table[x - 1].period_ns)) {
			return x;
		}
	}
	return 7;
}

static uchar calc_sync_xfer(unsigned int period, unsigned int offset)
{
	uchar result;

	period *= 4;		/* convert SDTR code to ns */
	result = sx_table[round_period(period)].reg_value;
	result |= (offset < OPTIMUM_SX_OFF) ? offset : OPTIMUM_SX_OFF;
	return result;
}



static void in2000_execute(struct Scsi_Host *instance);

static int in2000_queuecommand(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	struct Scsi_Host *instance;
	struct IN2000_hostdata *hostdata;
	Scsi_Cmnd *tmp;

	instance = cmd->device->host;
	hostdata = (struct IN2000_hostdata *) instance->hostdata;

	DB(DB_QUEUE_COMMAND, printk("Q-%d-%02x-%ld(", cmd->device->id, cmd->cmnd[0], cmd->pid))

/* Set up a few fields in the Scsi_Cmnd structure for our own use:
 *  - host_scribble is the pointer to the next cmd in the input queue
 *  - scsi_done points to the routine we call when a cmd is finished
 *  - result is what you'd expect
 */
	    cmd->host_scribble = NULL;
	cmd->scsi_done = done;
	cmd->result = 0;

/* We use the Scsi_Pointer structure that's included with each command
 * as a scratchpad (as it's intended to be used!). The handy thing about
 * the SCp.xxx fields is that they're always associated with a given
 * cmd, and are preserved across disconnect-reselect. This means we
 * can pretty much ignore SAVE_POINTERS and RESTORE_POINTERS messages
 * if we keep all the critical pointers and counters in SCp:
 *  - SCp.ptr is the pointer into the RAM buffer
 *  - SCp.this_residual is the size of that buffer
 *  - SCp.buffer points to the current scatter-gather buffer
 *  - SCp.buffers_residual tells us how many S.G. buffers there are
 *  - SCp.have_data_in helps keep track of >2048 byte transfers
 *  - SCp.sent_command is not used
 *  - SCp.phase records this command's SRCID_ER bit setting
 */

	if (cmd->use_sg) {
		cmd->SCp.buffer = (struct scatterlist *) cmd->buffer;
		cmd->SCp.buffers_residual = cmd->use_sg - 1;
		cmd->SCp.ptr = (char *) page_address(cmd->SCp.buffer->page) + cmd->SCp.buffer->offset;
		cmd->SCp.this_residual = cmd->SCp.buffer->length;
	} else {
		cmd->SCp.buffer = NULL;
		cmd->SCp.buffers_residual = 0;
		cmd->SCp.ptr = (char *) cmd->request_buffer;
		cmd->SCp.this_residual = cmd->request_bufflen;
	}
	cmd->SCp.have_data_in = 0;

/* We don't set SCp.phase here - that's done in in2000_execute() */

/* WD docs state that at the conclusion of a "LEVEL2" command, the
 * status byte can be retrieved from the LUN register. Apparently,
 * this is the case only for *uninterrupted* LEVEL2 commands! If
 * there are any unexpected phases entered, even if they are 100%
 * legal (different devices may choose to do things differently),
 * the LEVEL2 command sequence is exited. This often occurs prior
 * to receiving the status byte, in which case the driver does a
 * status phase interrupt and gets the status byte on its own.
 * While such a command can then be "resumed" (ie restarted to
 * finish up as a LEVEL2 command), the LUN register will NOT be
 * a valid status byte at the command's conclusion, and we must
 * use the byte obtained during the earlier interrupt. Here, we
 * preset SCp.Status to an illegal value (0xff) so that when
 * this command finally completes, we can tell where the actual
 * status byte is stored.
 */

	cmd->SCp.Status = ILLEGAL_STATUS_BYTE;

/* We need to disable interrupts before messing with the input
 * queue and calling in2000_execute().
 */

	/*
	 * Add the cmd to the end of 'input_Q'. Note that REQUEST_SENSE
	 * commands are added to the head of the queue so that the desired
	 * sense data is not lost before REQUEST_SENSE executes.
	 */

	if (!(hostdata->input_Q) || (cmd->cmnd[0] == REQUEST_SENSE)) {
		cmd->host_scribble = (uchar *) hostdata->input_Q;
		hostdata->input_Q = cmd;
	} else {		/* find the end of the queue */
		for (tmp = (Scsi_Cmnd *) hostdata->input_Q; tmp->host_scribble; tmp = (Scsi_Cmnd *) tmp->host_scribble);
		tmp->host_scribble = (uchar *) cmd;
	}

/* We know that there's at least one command in 'input_Q' now.
 * Go see if any of them are runnable!
 */

	in2000_execute(cmd->device->host);

	DB(DB_QUEUE_COMMAND, printk(")Q-%ld ", cmd->pid))
	    return 0;
}



/*
 * This routine attempts to start a scsi command. If the host_card is
 * already connected, we give up immediately. Otherwise, look through
 * the input_Q, using the first command we find that's intended
 * for a currently non-busy target/lun.
 * Note that this function is always called with interrupts already
 * disabled (either from in2000_queuecommand() or in2000_intr()).
 */
static void in2000_execute(struct Scsi_Host *instance)
{
	struct IN2000_hostdata *hostdata;
	Scsi_Cmnd *cmd, *prev;
	int i;
	unsigned short *sp;
	unsigned short f;
	unsigned short flushbuf[16];


	hostdata = (struct IN2000_hostdata *) instance->hostdata;

	DB(DB_EXECUTE, printk("EX("))

	    if (hostdata->selecting || hostdata->connected) {

		DB(DB_EXECUTE, printk(")EX-0 "))

		    return;
	}

	/*
	 * Search through the input_Q for a command destined
	 * for an idle target/lun.
	 */

	cmd = (Scsi_Cmnd *) hostdata->input_Q;
	prev = NULL;
	while (cmd) {
		if (!(hostdata->busy[cmd->device->id] & (1 << cmd->device->lun)))
			break;
		prev = cmd;
		cmd = (Scsi_Cmnd *) cmd->host_scribble;
	}

	/* quit if queue empty or all possible targets are busy */

	if (!cmd) {

		DB(DB_EXECUTE, printk(")EX-1 "))

		    return;
	}

	/*  remove command from queue */

	if (prev)
		prev->host_scribble = cmd->host_scribble;
	else
		hostdata->input_Q = (Scsi_Cmnd *) cmd->host_scribble;

#ifdef PROC_STATISTICS
	hostdata->cmd_cnt[cmd->device->id]++;
#endif

/*
 * Start the selection process
 */

	if (is_dir_out(cmd))
		write_3393(hostdata, WD_DESTINATION_ID, cmd->device->id);
	else
		write_3393(hostdata, WD_DESTINATION_ID, cmd->device->id | DSTID_DPD);

/* Now we need to figure out whether or not this command is a good
 * candidate for disconnect/reselect. We guess to the best of our
 * ability, based on a set of hierarchical rules. When several
 * devices are operating simultaneously, disconnects are usually
 * an advantage. In a single device system, or if only 1 device
 * is being accessed, transfers usually go faster if disconnects
 * are not allowed:
 *
 * + Commands should NEVER disconnect if hostdata->disconnect =
 *   DIS_NEVER (this holds for tape drives also), and ALWAYS
 *   disconnect if hostdata->disconnect = DIS_ALWAYS.
 * + Tape drive commands should always be allowed to disconnect.
 * + Disconnect should be allowed if disconnected_Q isn't empty.
 * + Commands should NOT disconnect if input_Q is empty.
 * + Disconnect should be allowed if there are commands in input_Q
 *   for a different target/lun. In this case, the other commands
 *   should be made disconnect-able, if not already.
 *
 * I know, I know - this code would flunk me out of any
 * "C Programming 101" class ever offered. But it's easy
 * to change around and experiment with for now.
 */

	cmd->SCp.phase = 0;	/* assume no disconnect */
	if (hostdata->disconnect == DIS_NEVER)
		goto no;
	if (hostdata->disconnect == DIS_ALWAYS)
		goto yes;
	if (cmd->device->type == 1)	/* tape drive? */
		goto yes;
	if (hostdata->disconnected_Q)	/* other commands disconnected? */
		goto yes;
	if (!(hostdata->input_Q))	/* input_Q empty? */
		goto no;
	for (prev = (Scsi_Cmnd *) hostdata->input_Q; prev; prev = (Scsi_Cmnd *) prev->host_scribble) {
		if ((prev->device->id != cmd->device->id) || (prev->device->lun != cmd->device->lun)) {
			for (prev = (Scsi_Cmnd *) hostdata->input_Q; prev; prev = (Scsi_Cmnd *) prev->host_scribble)
				prev->SCp.phase = 1;
			goto yes;
		}
	}
	goto no;

      yes:
	cmd->SCp.phase = 1;

#ifdef PROC_STATISTICS
	hostdata->disc_allowed_cnt[cmd->device->id]++;
#endif

      no:
	write_3393(hostdata, WD_SOURCE_ID, ((cmd->SCp.phase) ? SRCID_ER : 0));

	write_3393(hostdata, WD_TARGET_LUN, cmd->device->lun);
	write_3393(hostdata, WD_SYNCHRONOUS_TRANSFER, hostdata->sync_xfer[cmd->device->id]);
	hostdata->busy[cmd->device->id] |= (1 << cmd->device->lun);

	if ((hostdata->level2 <= L2_NONE) || (hostdata->sync_stat[cmd->device->id] == SS_UNSET)) {

		/*
		 * Do a 'Select-With-ATN' command. This will end with
		 * one of the following interrupts:
		 *    CSR_RESEL_AM:  failure - can try again later.
		 *    CSR_TIMEOUT:   failure - give up.
		 *    CSR_SELECT:    success - proceed.
		 */

		hostdata->selecting = cmd;

/* Every target has its own synchronous transfer setting, kept in
 * the sync_xfer array, and a corresponding status byte in sync_stat[].
 * Each target's sync_stat[] entry is initialized to SS_UNSET, and its
 * sync_xfer[] entry is initialized to the default/safe value. SS_UNSET
 * means that the parameters are undetermined as yet, and that we
 * need to send an SDTR message to this device after selection is
 * complete. We set SS_FIRST to tell the interrupt routine to do so,
 * unless we don't want to even _try_ synchronous transfers: In this
 * case we set SS_SET to make the defaults final.
 */
		if (hostdata->sync_stat[cmd->device->id] == SS_UNSET) {
			if (hostdata->sync_off & (1 << cmd->device->id))
				hostdata->sync_stat[cmd->device->id] = SS_SET;
			else
				hostdata->sync_stat[cmd->device->id] = SS_FIRST;
		}
		hostdata->state = S_SELECTING;
		write_3393_count(hostdata, 0);	/* this guarantees a DATA_PHASE interrupt */
		write_3393_cmd(hostdata, WD_CMD_SEL_ATN);
	}

	else {

		/*
		 * Do a 'Select-With-ATN-Xfer' command. This will end with
		 * one of the following interrupts:
		 *    CSR_RESEL_AM:  failure - can try again later.
		 *    CSR_TIMEOUT:   failure - give up.
		 *    anything else: success - proceed.
		 */

		hostdata->connected = cmd;
		write_3393(hostdata, WD_COMMAND_PHASE, 0);

		/* copy command_descriptor_block into WD chip
		 * (take advantage of auto-incrementing)
		 */

		write1_io(WD_CDB_1, IO_WD_ADDR);
		for (i = 0; i < cmd->cmd_len; i++)
			write1_io(cmd->cmnd[i], IO_WD_DATA);

		/* The wd33c93 only knows about Group 0, 1, and 5 commands when
		 * it's doing a 'select-and-transfer'. To be safe, we write the
		 * size of the CDB into the OWN_ID register for every case. This
		 * way there won't be problems with vendor-unique, audio, etc.
		 */

		write_3393(hostdata, WD_OWN_ID, cmd->cmd_len);

		/* When doing a non-disconnect command, we can save ourselves a DATA
		 * phase interrupt later by setting everything up now. With writes we
		 * need to pre-fill the fifo; if there's room for the 32 flush bytes,
		 * put them in there too - that'll avoid a fifo interrupt. Reads are
		 * somewhat simpler.
		 * KLUDGE NOTE: It seems that you can't completely fill the fifo here:
		 * This results in the IO_FIFO_COUNT register rolling over to zero,
		 * and apparently the gate array logic sees this as empty, not full,
		 * so the 3393 chip is never signalled to start reading from the
		 * fifo. Or maybe it's seen as a permanent fifo interrupt condition.
		 * Regardless, we fix this by temporarily pretending that the fifo
		 * is 16 bytes smaller. (I see now that the old driver has a comment
		 * about "don't fill completely" in an analogous place - must be the
		 * same deal.) This results in CDROM, swap partitions, and tape drives
		 * needing an extra interrupt per write command - I think we can live
		 * with that!
		 */

		if (!(cmd->SCp.phase)) {
			write_3393_count(hostdata, cmd->SCp.this_residual);
			write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_BUS);
			write1_io(0, IO_FIFO_WRITE);	/* clear fifo counter, write mode */

			if (is_dir_out(cmd)) {
				hostdata->fifo = FI_FIFO_WRITING;
				if ((i = cmd->SCp.this_residual) > (IN2000_FIFO_SIZE - 16))
					i = IN2000_FIFO_SIZE - 16;
				cmd->SCp.have_data_in = i;	/* this much data in fifo */
				i >>= 1;	/* Gulp. Assuming modulo 2. */
				sp = (unsigned short *) cmd->SCp.ptr;
				f = hostdata->io_base + IO_FIFO;

#ifdef FAST_WRITE_IO

				FAST_WRITE2_IO();
#else
				while (i--)
					write2_io(*sp++, IO_FIFO);

#endif

				/* Is there room for the flush bytes? */

				if (cmd->SCp.have_data_in <= ((IN2000_FIFO_SIZE - 16) - 32)) {
					sp = flushbuf;
					i = 16;

#ifdef FAST_WRITE_IO

					FAST_WRITE2_IO();
#else
					while (i--)
						write2_io(0, IO_FIFO);

#endif

				}
			}

			else {
				write1_io(0, IO_FIFO_READ);	/* put fifo in read mode */
				hostdata->fifo = FI_FIFO_READING;
				cmd->SCp.have_data_in = 0;	/* nothing transferred yet */
			}

		} else {
			write_3393_count(hostdata, 0);	/* this guarantees a DATA_PHASE interrupt */
		}
		hostdata->state = S_RUNNING_LEVEL2;
		write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
	}

	/*
	 * Since the SCSI bus can handle only 1 connection at a time,
	 * we get out of here now. If the selection fails, or when
	 * the command disconnects, we'll come back to this routine
	 * to search the input_Q again...
	 */

	DB(DB_EXECUTE, printk("%s%ld)EX-2 ", (cmd->SCp.phase) ? "d:" : "", cmd->pid))

}



static void transfer_pio(uchar * buf, int cnt, int data_in_dir, struct IN2000_hostdata *hostdata)
{
	uchar asr;

	DB(DB_TRANSFER, printk("(%p,%d,%s)", buf, cnt, data_in_dir ? "in" : "out"))

	    write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
	write_3393_count(hostdata, cnt);
	write_3393_cmd(hostdata, WD_CMD_TRANS_INFO);
	if (data_in_dir) {
		do {
			asr = READ_AUX_STAT();
			if (asr & ASR_DBR)
				*buf++ = read_3393(hostdata, WD_DATA);
		} while (!(asr & ASR_INT));
	} else {
		do {
			asr = READ_AUX_STAT();
			if (asr & ASR_DBR)
				write_3393(hostdata, WD_DATA, *buf++);
		} while (!(asr & ASR_INT));
	}

	/* Note: we are returning with the interrupt UN-cleared.
	 * Since (presumably) an entire I/O operation has
	 * completed, the bus phase is probably different, and
	 * the interrupt routine will discover this when it
	 * responds to the uncleared int.
	 */

}



static void transfer_bytes(Scsi_Cmnd * cmd, int data_in_dir)
{
	struct IN2000_hostdata *hostdata;
	unsigned short *sp;
	unsigned short f;
	int i;

	hostdata = (struct IN2000_hostdata *) cmd->device->host->hostdata;

/* Normally, you'd expect 'this_residual' to be non-zero here.
 * In a series of scatter-gather transfers, however, this
 * routine will usually be called with 'this_residual' equal
 * to 0 and 'buffers_residual' non-zero. This means that a
 * previous transfer completed, clearing 'this_residual', and
 * now we need to setup the next scatter-gather buffer as the
 * source or destination for THIS transfer.
 */
	if (!cmd->SCp.this_residual && cmd->SCp.buffers_residual) {
		++cmd->SCp.buffer;
		--cmd->SCp.buffers_residual;
		cmd->SCp.this_residual = cmd->SCp.buffer->length;
		cmd->SCp.ptr = page_address(cmd->SCp.buffer->page) + cmd->SCp.buffer->offset;
	}

/* Set up hardware registers */

	write_3393(hostdata, WD_SYNCHRONOUS_TRANSFER, hostdata->sync_xfer[cmd->device->id]);
	write_3393_count(hostdata, cmd->SCp.this_residual);
	write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_BUS);
	write1_io(0, IO_FIFO_WRITE);	/* zero counter, assume write */

/* Reading is easy. Just issue the command and return - we'll
 * get an interrupt later when we have actual data to worry about.
 */

	if (data_in_dir) {
		write1_io(0, IO_FIFO_READ);
		if ((hostdata->level2 >= L2_DATA) || (hostdata->level2 == L2_BASIC && cmd->SCp.phase == 0)) {
			write_3393(hostdata, WD_COMMAND_PHASE, 0x45);
			write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
			hostdata->state = S_RUNNING_LEVEL2;
		} else
			write_3393_cmd(hostdata, WD_CMD_TRANS_INFO);
		hostdata->fifo = FI_FIFO_READING;
		cmd->SCp.have_data_in = 0;
		return;
	}

/* Writing is more involved - we'll start the WD chip and write as
 * much data to the fifo as we can right now. Later interrupts will
 * write any bytes that don't make it at this stage.
 */

	if ((hostdata->level2 >= L2_DATA) || (hostdata->level2 == L2_BASIC && cmd->SCp.phase == 0)) {
		write_3393(hostdata, WD_COMMAND_PHASE, 0x45);
		write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
		hostdata->state = S_RUNNING_LEVEL2;
	} else
		write_3393_cmd(hostdata, WD_CMD_TRANS_INFO);
	hostdata->fifo = FI_FIFO_WRITING;
	sp = (unsigned short *) cmd->SCp.ptr;

	if ((i = cmd->SCp.this_residual) > IN2000_FIFO_SIZE)
		i = IN2000_FIFO_SIZE;
	cmd->SCp.have_data_in = i;
	i >>= 1;		/* Gulp. We assume this_residual is modulo 2 */
	f = hostdata->io_base + IO_FIFO;

#ifdef FAST_WRITE_IO

	FAST_WRITE2_IO();
#else
	while (i--)
		write2_io(*sp++, IO_FIFO);

#endif

}


/* We need to use spin_lock_irqsave() & spin_unlock_irqrestore() in this
 * function in order to work in an SMP environment. (I'd be surprised
 * if the driver is ever used by anyone on a real multi-CPU motherboard,
 * but it _does_ need to be able to compile and run in an SMP kernel.)
 */

static irqreturn_t in2000_intr(int irqnum, void *dev_id, struct pt_regs *ptregs)
{
	struct Scsi_Host *instance = dev_id;
	struct IN2000_hostdata *hostdata;
	Scsi_Cmnd *patch, *cmd;
	uchar asr, sr, phs, id, lun, *ucp, msg;
	int i, j;
	unsigned long length;
	unsigned short *sp;
	unsigned short f;
	unsigned long flags;

	hostdata = (struct IN2000_hostdata *) instance->hostdata;

/* Get the spin_lock and disable further ints, for SMP */

	spin_lock_irqsave(instance->host_lock, flags);

#ifdef PROC_STATISTICS
	hostdata->int_cnt++;
#endif

/* The IN2000 card has 2 interrupt sources OR'ed onto its IRQ line - the
 * WD3393 chip and the 2k fifo (which is actually a dual-port RAM combined
 * with a big logic array, so it's a little different than what you might
 * expect). As far as I know, there's no reason that BOTH can't be active
 * at the same time, but there's a problem: while we can read the 3393
 * to tell if _it_ wants an interrupt, I don't know of a way to ask the
 * fifo the same question. The best we can do is check the 3393 and if
 * it _isn't_ the source of the interrupt, then we can be pretty sure
 * that the fifo is the culprit.
 *  UPDATE: I have it on good authority (Bill Earnest) that bit 0 of the
 *          IO_FIFO_COUNT register mirrors the fifo interrupt state. I
 *          assume that bit clear means interrupt active. As it turns
 *          out, the driver really doesn't need to check for this after
 *          all, so my remarks above about a 'problem' can safely be
 *          ignored. The way the logic is set up, there's no advantage
 *          (that I can see) to worrying about it.
 *
 * It seems that the fifo interrupt signal is negated when we extract
 * bytes during read or write bytes during write.
 *  - fifo will interrupt when data is moving from it to the 3393, and
 *    there are 31 (or less?) bytes left to go. This is sort of short-
 *    sighted: what if you don't WANT to do more? In any case, our
 *    response is to push more into the fifo - either actual data or
 *    dummy bytes if need be. Note that we apparently have to write at
 *    least 32 additional bytes to the fifo after an interrupt in order
 *    to get it to release the ones it was holding on to - writing fewer
 *    than 32 will result in another fifo int.
 *  UPDATE: Again, info from Bill Earnest makes this more understandable:
 *          32 bytes = two counts of the fifo counter register. He tells
 *          me that the fifo interrupt is a non-latching signal derived
 *          from a straightforward boolean interpretation of the 7
 *          highest bits of the fifo counter and the fifo-read/fifo-write
 *          state. Who'd a thought?
 */

	write1_io(0, IO_LED_ON);
	asr = READ_AUX_STAT();
	if (!(asr & ASR_INT)) {	/* no WD33c93 interrupt? */

/* Ok. This is definitely a FIFO-only interrupt.
 *
 * If FI_FIFO_READING is set, there are up to 2048 bytes waiting to be read,
 * maybe more to come from the SCSI bus. Read as many as we can out of the
 * fifo and into memory at the location of SCp.ptr[SCp.have_data_in], and
 * update have_data_in afterwards.
 *
 * If we have FI_FIFO_WRITING, the FIFO has almost run out of bytes to move
 * into the WD3393 chip (I think the interrupt happens when there are 31
 * bytes left, but it may be fewer...). The 3393 is still waiting, so we
 * shove some more into the fifo, which gets things moving again. If the
 * original SCSI command specified more than 2048 bytes, there may still
 * be some of that data left: fine - use it (from SCp.ptr[SCp.have_data_in]).
 * Don't forget to update have_data_in. If we've already written out the
 * entire buffer, feed 32 dummy bytes to the fifo - they're needed to
 * push out the remaining real data.
 *    (Big thanks to Bill Earnest for getting me out of the mud in here.)
 */

		cmd = (Scsi_Cmnd *) hostdata->connected;	/* assume we're connected */
		CHECK_NULL(cmd, "fifo_int")

		    if (hostdata->fifo == FI_FIFO_READING) {

			DB(DB_FIFO, printk("{R:%02x} ", read1_io(IO_FIFO_COUNT)))

			    sp = (unsigned short *) (cmd->SCp.ptr + cmd->SCp.have_data_in);
			i = read1_io(IO_FIFO_COUNT) & 0xfe;
			i <<= 2;	/* # of words waiting in the fifo */
			f = hostdata->io_base + IO_FIFO;

#ifdef FAST_READ_IO

			FAST_READ2_IO();
#else
			while (i--)
				*sp++ = read2_io(IO_FIFO);

#endif

			i = sp - (unsigned short *) (cmd->SCp.ptr + cmd->SCp.have_data_in);
			i <<= 1;
			cmd->SCp.have_data_in += i;
		}

		else if (hostdata->fifo == FI_FIFO_WRITING) {

			DB(DB_FIFO, printk("{W:%02x} ", read1_io(IO_FIFO_COUNT)))

/* If all bytes have been written to the fifo, flush out the stragglers.
 * Note that while writing 16 dummy words seems arbitrary, we don't
 * have another choice that I can see. What we really want is to read
 * the 3393 transfer count register (that would tell us how many bytes
 * needed flushing), but the TRANSFER_INFO command hasn't completed
 * yet (not enough bytes!) and that register won't be accessible. So,
 * we use 16 words - a number obtained through trial and error.
 *  UPDATE: Bill says this is exactly what Always does, so there.
 *          More thanks due him for help in this section.
 */
			    if (cmd->SCp.this_residual == cmd->SCp.have_data_in) {
				i = 16;
				while (i--)	/* write 32 dummy bytes */
					write2_io(0, IO_FIFO);
			}

/* If there are still bytes left in the SCSI buffer, write as many as we
 * can out to the fifo.
 */

			else {
				sp = (unsigned short *) (cmd->SCp.ptr + cmd->SCp.have_data_in);
				i = cmd->SCp.this_residual - cmd->SCp.have_data_in;	/* bytes yet to go */
				j = read1_io(IO_FIFO_COUNT) & 0xfe;
				j <<= 2;	/* how many words the fifo has room for */
				if ((j << 1) > i)
					j = (i >> 1);
				while (j--)
					write2_io(*sp++, IO_FIFO);

				i = sp - (unsigned short *) (cmd->SCp.ptr + cmd->SCp.have_data_in);
				i <<= 1;
				cmd->SCp.have_data_in += i;
			}
		}

		else {
			printk("*** Spurious FIFO interrupt ***");
		}

		write1_io(0, IO_LED_OFF);

/* release the SMP spin_lock and restore irq state */
		spin_unlock_irqrestore(instance->host_lock, flags);
		return IRQ_HANDLED;
	}

/* This interrupt was triggered by the WD33c93 chip. The fifo interrupt
 * may also be asserted, but we don't bother to check it: we get more
 * detailed info from FIFO_READING and FIFO_WRITING (see below).
 */

	cmd = (Scsi_Cmnd *) hostdata->connected;	/* assume we're connected */
	sr = read_3393(hostdata, WD_SCSI_STATUS);	/* clear the interrupt */
	phs = read_3393(hostdata, WD_COMMAND_PHASE);

	if (!cmd && (sr != CSR_RESEL_AM && sr != CSR_TIMEOUT && sr != CSR_SELECT)) {
		printk("\nNR:wd-intr-1\n");
		write1_io(0, IO_LED_OFF);

/* release the SMP spin_lock and restore irq state */
		spin_unlock_irqrestore(instance->host_lock, flags);
		return IRQ_HANDLED;
	}

	DB(DB_INTR, printk("{%02x:%02x-", asr, sr))

/* After starting a FIFO-based transfer, the next _WD3393_ interrupt is
 * guaranteed to be in response to the completion of the transfer.
 * If we were reading, there's probably data in the fifo that needs
 * to be copied into RAM - do that here. Also, we have to update
 * 'this_residual' and 'ptr' based on the contents of the
 * TRANSFER_COUNT register, in case the device decided to do an
 * intermediate disconnect (a device may do this if it has to
 * do a seek,  or just to be nice and let other devices have
 * some bus time during long transfers).
 * After doing whatever is necessary with the fifo, we go on and
 * service the WD3393 interrupt normally.
 */
	    if (hostdata->fifo == FI_FIFO_READING) {

/* buffer index = start-of-buffer + #-of-bytes-already-read */

		sp = (unsigned short *) (cmd->SCp.ptr + cmd->SCp.have_data_in);

/* bytes remaining in fifo = (total-wanted - #-not-got) - #-already-read */

		i = (cmd->SCp.this_residual - read_3393_count(hostdata)) - cmd->SCp.have_data_in;
		i >>= 1;	/* Gulp. We assume this will always be modulo 2 */
		f = hostdata->io_base + IO_FIFO;

#ifdef FAST_READ_IO

		FAST_READ2_IO();
#else
		while (i--)
			*sp++ = read2_io(IO_FIFO);

#endif

		hostdata->fifo = FI_FIFO_UNUSED;
		length = cmd->SCp.this_residual;
		cmd->SCp.this_residual = read_3393_count(hostdata);
		cmd->SCp.ptr += (length - cmd->SCp.this_residual);

		DB(DB_TRANSFER, printk("(%p,%d)", cmd->SCp.ptr, cmd->SCp.this_residual))

	}

	else if (hostdata->fifo == FI_FIFO_WRITING) {
		hostdata->fifo = FI_FIFO_UNUSED;
		length = cmd->SCp.this_residual;
		cmd->SCp.this_residual = read_3393_count(hostdata);
		cmd->SCp.ptr += (length - cmd->SCp.this_residual);

		DB(DB_TRANSFER, printk("(%p,%d)", cmd->SCp.ptr, cmd->SCp.this_residual))

	}

/* Respond to the specific WD3393 interrupt - there are quite a few! */

	switch (sr) {

	case CSR_TIMEOUT:
		DB(DB_INTR, printk("TIMEOUT"))

		    if (hostdata->state == S_RUNNING_LEVEL2)
			hostdata->connected = NULL;
		else {
			cmd = (Scsi_Cmnd *) hostdata->selecting;	/* get a valid cmd */
			CHECK_NULL(cmd, "csr_timeout")
			    hostdata->selecting = NULL;
		}

		cmd->result = DID_NO_CONNECT << 16;
		hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
		hostdata->state = S_UNCONNECTED;
		cmd->scsi_done(cmd);

/* We are not connected to a target - check to see if there
 * are commands waiting to be executed.
 */

		in2000_execute(instance);
		break;


/* Note: this interrupt should not occur in a LEVEL2 command */

	case CSR_SELECT:
		DB(DB_INTR, printk("SELECT"))
		    hostdata->connected = cmd = (Scsi_Cmnd *) hostdata->selecting;
		CHECK_NULL(cmd, "csr_select")
		    hostdata->selecting = NULL;

		/* construct an IDENTIFY message with correct disconnect bit */

		hostdata->outgoing_msg[0] = (0x80 | 0x00 | cmd->device->lun);
		if (cmd->SCp.phase)
			hostdata->outgoing_msg[0] |= 0x40;

		if (hostdata->sync_stat[cmd->device->id] == SS_FIRST) {
#ifdef SYNC_DEBUG
			printk(" sending SDTR ");
#endif

			hostdata->sync_stat[cmd->device->id] = SS_WAITING;

			/* tack on a 2nd message to ask about synchronous transfers */

			hostdata->outgoing_msg[1] = EXTENDED_MESSAGE;
			hostdata->outgoing_msg[2] = 3;
			hostdata->outgoing_msg[3] = EXTENDED_SDTR;
			hostdata->outgoing_msg[4] = OPTIMUM_SX_PER / 4;
			hostdata->outgoing_msg[5] = OPTIMUM_SX_OFF;
			hostdata->outgoing_len = 6;
		} else
			hostdata->outgoing_len = 1;

		hostdata->state = S_CONNECTED;
		break;


	case CSR_XFER_DONE | PHS_DATA_IN:
	case CSR_UNEXP | PHS_DATA_IN:
	case CSR_SRV_REQ | PHS_DATA_IN:
		DB(DB_INTR, printk("IN-%d.%d", cmd->SCp.this_residual, cmd->SCp.buffers_residual))
		    transfer_bytes(cmd, DATA_IN_DIR);
		if (hostdata->state != S_RUNNING_LEVEL2)
			hostdata->state = S_CONNECTED;
		break;


	case CSR_XFER_DONE | PHS_DATA_OUT:
	case CSR_UNEXP | PHS_DATA_OUT:
	case CSR_SRV_REQ | PHS_DATA_OUT:
		DB(DB_INTR, printk("OUT-%d.%d", cmd->SCp.this_residual, cmd->SCp.buffers_residual))
		    transfer_bytes(cmd, DATA_OUT_DIR);
		if (hostdata->state != S_RUNNING_LEVEL2)
			hostdata->state = S_CONNECTED;
		break;


/* Note: this interrupt should not occur in a LEVEL2 command */

	case CSR_XFER_DONE | PHS_COMMAND:
	case CSR_UNEXP | PHS_COMMAND:
	case CSR_SRV_REQ | PHS_COMMAND:
		DB(DB_INTR, printk("CMND-%02x,%ld", cmd->cmnd[0], cmd->pid))
		    transfer_pio(cmd->cmnd, cmd->cmd_len, DATA_OUT_DIR, hostdata);
		hostdata->state = S_CONNECTED;
		break;


	case CSR_XFER_DONE | PHS_STATUS:
	case CSR_UNEXP | PHS_STATUS:
	case CSR_SRV_REQ | PHS_STATUS:
		DB(DB_INTR, printk("STATUS="))

		    cmd->SCp.Status = read_1_byte(hostdata);
		DB(DB_INTR, printk("%02x", cmd->SCp.Status))
		    if (hostdata->level2 >= L2_BASIC) {
			sr = read_3393(hostdata, WD_SCSI_STATUS);	/* clear interrupt */
			hostdata->state = S_RUNNING_LEVEL2;
			write_3393(hostdata, WD_COMMAND_PHASE, 0x50);
			write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
		} else {
			hostdata->state = S_CONNECTED;
		}
		break;


	case CSR_XFER_DONE | PHS_MESS_IN:
	case CSR_UNEXP | PHS_MESS_IN:
	case CSR_SRV_REQ | PHS_MESS_IN:
		DB(DB_INTR, printk("MSG_IN="))

		    msg = read_1_byte(hostdata);
		sr = read_3393(hostdata, WD_SCSI_STATUS);	/* clear interrupt */

		hostdata->incoming_msg[hostdata->incoming_ptr] = msg;
		if (hostdata->incoming_msg[0] == EXTENDED_MESSAGE)
			msg = EXTENDED_MESSAGE;
		else
			hostdata->incoming_ptr = 0;

		cmd->SCp.Message = msg;
		switch (msg) {

		case COMMAND_COMPLETE:
			DB(DB_INTR, printk("CCMP-%ld", cmd->pid))
			    write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
			hostdata->state = S_PRE_CMP_DISC;
			break;

		case SAVE_POINTERS:
			DB(DB_INTR, printk("SDP"))
			    write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
			hostdata->state = S_CONNECTED;
			break;

		case RESTORE_POINTERS:
			DB(DB_INTR, printk("RDP"))
			    if (hostdata->level2 >= L2_BASIC) {
				write_3393(hostdata, WD_COMMAND_PHASE, 0x45);
				write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
				hostdata->state = S_RUNNING_LEVEL2;
			} else {
				write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
				hostdata->state = S_CONNECTED;
			}
			break;

		case DISCONNECT:
			DB(DB_INTR, printk("DIS"))
			    cmd->device->disconnect = 1;
			write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
			hostdata->state = S_PRE_TMP_DISC;
			break;

		case MESSAGE_REJECT:
			DB(DB_INTR, printk("REJ"))
#ifdef SYNC_DEBUG
			    printk("-REJ-");
#endif
			if (hostdata->sync_stat[cmd->device->id] == SS_WAITING)
				hostdata->sync_stat[cmd->device->id] = SS_SET;
			write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
			hostdata->state = S_CONNECTED;
			break;

		case EXTENDED_MESSAGE:
			DB(DB_INTR, printk("EXT"))

			    ucp = hostdata->incoming_msg;

#ifdef SYNC_DEBUG
			printk("%02x", ucp[hostdata->incoming_ptr]);
#endif
			/* Is this the last byte of the extended message? */

			if ((hostdata->incoming_ptr >= 2) && (hostdata->incoming_ptr == (ucp[1] + 1))) {

				switch (ucp[2]) {	/* what's the EXTENDED code? */
				case EXTENDED_SDTR:
					id = calc_sync_xfer(ucp[3], ucp[4]);
					if (hostdata->sync_stat[cmd->device->id] != SS_WAITING) {

/* A device has sent an unsolicited SDTR message; rather than go
 * through the effort of decoding it and then figuring out what
 * our reply should be, we're just gonna say that we have a
 * synchronous fifo depth of 0. This will result in asynchronous
 * transfers - not ideal but so much easier.
 * Actually, this is OK because it assures us that if we don't
 * specifically ask for sync transfers, we won't do any.
 */

						write_3393_cmd(hostdata, WD_CMD_ASSERT_ATN);	/* want MESS_OUT */
						hostdata->outgoing_msg[0] = EXTENDED_MESSAGE;
						hostdata->outgoing_msg[1] = 3;
						hostdata->outgoing_msg[2] = EXTENDED_SDTR;
						hostdata->outgoing_msg[3] = hostdata->default_sx_per / 4;
						hostdata->outgoing_msg[4] = 0;
						hostdata->outgoing_len = 5;
						hostdata->sync_xfer[cmd->device->id] = calc_sync_xfer(hostdata->default_sx_per / 4, 0);
					} else {
						hostdata->sync_xfer[cmd->device->id] = id;
					}
#ifdef SYNC_DEBUG
					printk("sync_xfer=%02x", hostdata->sync_xfer[cmd->device->id]);
#endif
					hostdata->sync_stat[cmd->device->id] = SS_SET;
					write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
					hostdata->state = S_CONNECTED;
					break;
				case EXTENDED_WDTR:
					write_3393_cmd(hostdata, WD_CMD_ASSERT_ATN);	/* want MESS_OUT */
					printk("sending WDTR ");
					hostdata->outgoing_msg[0] = EXTENDED_MESSAGE;
					hostdata->outgoing_msg[1] = 2;
					hostdata->outgoing_msg[2] = EXTENDED_WDTR;
					hostdata->outgoing_msg[3] = 0;	/* 8 bit transfer width */
					hostdata->outgoing_len = 4;
					write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
					hostdata->state = S_CONNECTED;
					break;
				default:
					write_3393_cmd(hostdata, WD_CMD_ASSERT_ATN);	/* want MESS_OUT */
					printk("Rejecting Unknown Extended Message(%02x). ", ucp[2]);
					hostdata->outgoing_msg[0] = MESSAGE_REJECT;
					hostdata->outgoing_len = 1;
					write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
					hostdata->state = S_CONNECTED;
					break;
				}
				hostdata->incoming_ptr = 0;
			}

			/* We need to read more MESS_IN bytes for the extended message */

			else {
				hostdata->incoming_ptr++;
				write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
				hostdata->state = S_CONNECTED;
			}
			break;

		default:
			printk("Rejecting Unknown Message(%02x) ", msg);
			write_3393_cmd(hostdata, WD_CMD_ASSERT_ATN);	/* want MESS_OUT */
			hostdata->outgoing_msg[0] = MESSAGE_REJECT;
			hostdata->outgoing_len = 1;
			write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
			hostdata->state = S_CONNECTED;
		}
		break;


/* Note: this interrupt will occur only after a LEVEL2 command */

	case CSR_SEL_XFER_DONE:

/* Make sure that reselection is enabled at this point - it may
 * have been turned off for the command that just completed.
 */

		write_3393(hostdata, WD_SOURCE_ID, SRCID_ER);
		if (phs == 0x60) {
			DB(DB_INTR, printk("SX-DONE-%ld", cmd->pid))
			    cmd->SCp.Message = COMMAND_COMPLETE;
			lun = read_3393(hostdata, WD_TARGET_LUN);
			DB(DB_INTR, printk(":%d.%d", cmd->SCp.Status, lun))
			    hostdata->connected = NULL;
			hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
			hostdata->state = S_UNCONNECTED;
			if (cmd->SCp.Status == ILLEGAL_STATUS_BYTE)
				cmd->SCp.Status = lun;
			if (cmd->cmnd[0] == REQUEST_SENSE && cmd->SCp.Status != GOOD)
				cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);
			else
				cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
			cmd->scsi_done(cmd);

/* We are no longer connected to a target - check to see if
 * there are commands waiting to be executed.
 */

			in2000_execute(instance);
		} else {
			printk("%02x:%02x:%02x-%ld: Unknown SEL_XFER_DONE phase!!---", asr, sr, phs, cmd->pid);
		}
		break;


/* Note: this interrupt will occur only after a LEVEL2 command */

	case CSR_SDP:
		DB(DB_INTR, printk("SDP"))
		    hostdata->state = S_RUNNING_LEVEL2;
		write_3393(hostdata, WD_COMMAND_PHASE, 0x41);
		write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
		break;


	case CSR_XFER_DONE | PHS_MESS_OUT:
	case CSR_UNEXP | PHS_MESS_OUT:
	case CSR_SRV_REQ | PHS_MESS_OUT:
		DB(DB_INTR, printk("MSG_OUT="))

/* To get here, we've probably requested MESSAGE_OUT and have
 * already put the correct bytes in outgoing_msg[] and filled
 * in outgoing_len. We simply send them out to the SCSI bus.
 * Sometimes we get MESSAGE_OUT phase when we're not expecting
 * it - like when our SDTR message is rejected by a target. Some
 * targets send the REJECT before receiving all of the extended
 * message, and then seem to go back to MESSAGE_OUT for a byte
 * or two. Not sure why, or if I'm doing something wrong to
 * cause this to happen. Regardless, it seems that sending
 * NOP messages in these situations results in no harm and
 * makes everyone happy.
 */
		    if (hostdata->outgoing_len == 0) {
			hostdata->outgoing_len = 1;
			hostdata->outgoing_msg[0] = NOP;
		}
		transfer_pio(hostdata->outgoing_msg, hostdata->outgoing_len, DATA_OUT_DIR, hostdata);
		DB(DB_INTR, printk("%02x", hostdata->outgoing_msg[0]))
		    hostdata->outgoing_len = 0;
		hostdata->state = S_CONNECTED;
		break;


	case CSR_UNEXP_DISC:

/* I think I've seen this after a request-sense that was in response
 * to an error condition, but not sure. We certainly need to do
 * something when we get this interrupt - the question is 'what?'.
 * Let's think positively, and assume some command has finished
 * in a legal manner (like a command that provokes a request-sense),
 * so we treat it as a normal command-complete-disconnect.
 */


/* Make sure that reselection is enabled at this point - it may
 * have been turned off for the command that just completed.
 */

		write_3393(hostdata, WD_SOURCE_ID, SRCID_ER);
		if (cmd == NULL) {
			printk(" - Already disconnected! ");
			hostdata->state = S_UNCONNECTED;

/* release the SMP spin_lock and restore irq state */
			spin_unlock_irqrestore(instance->host_lock, flags);
			return IRQ_HANDLED;
		}
		DB(DB_INTR, printk("UNEXP_DISC-%ld", cmd->pid))
		    hostdata->connected = NULL;
		hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
		hostdata->state = S_UNCONNECTED;
		if (cmd->cmnd[0] == REQUEST_SENSE && cmd->SCp.Status != GOOD)
			cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);
		else
			cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
		cmd->scsi_done(cmd);

/* We are no longer connected to a target - check to see if
 * there are commands waiting to be executed.
 */

		in2000_execute(instance);
		break;


	case CSR_DISC:

/* Make sure that reselection is enabled at this point - it may
 * have been turned off for the command that just completed.
 */

		write_3393(hostdata, WD_SOURCE_ID, SRCID_ER);
		DB(DB_INTR, printk("DISC-%ld", cmd->pid))
		    if (cmd == NULL) {
			printk(" - Already disconnected! ");
			hostdata->state = S_UNCONNECTED;
		}
		switch (hostdata->state) {
		case S_PRE_CMP_DISC:
			hostdata->connected = NULL;
			hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
			hostdata->state = S_UNCONNECTED;
			DB(DB_INTR, printk(":%d", cmd->SCp.Status))
			    if (cmd->cmnd[0] == REQUEST_SENSE && cmd->SCp.Status != GOOD)
				cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);
			else
				cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
			cmd->scsi_done(cmd);
			break;
		case S_PRE_TMP_DISC:
		case S_RUNNING_LEVEL2:
			cmd->host_scribble = (uchar *) hostdata->disconnected_Q;
			hostdata->disconnected_Q = cmd;
			hostdata->connected = NULL;
			hostdata->state = S_UNCONNECTED;

#ifdef PROC_STATISTICS
			hostdata->disc_done_cnt[cmd->device->id]++;
#endif

			break;
		default:
			printk("*** Unexpected DISCONNECT interrupt! ***");
			hostdata->state = S_UNCONNECTED;
		}

/* We are no longer connected to a target - check to see if
 * there are commands waiting to be executed.
 */

		in2000_execute(instance);
		break;


	case CSR_RESEL_AM:
		DB(DB_INTR, printk("RESEL"))

		    /* First we have to make sure this reselection didn't */
		    /* happen during Arbitration/Selection of some other device. */
		    /* If yes, put losing command back on top of input_Q. */
		    if (hostdata->level2 <= L2_NONE) {

			if (hostdata->selecting) {
				cmd = (Scsi_Cmnd *) hostdata->selecting;
				hostdata->selecting = NULL;
				hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
				cmd->host_scribble = (uchar *) hostdata->input_Q;
				hostdata->input_Q = cmd;
			}
		}

		else {

			if (cmd) {
				if (phs == 0x00) {
					hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
					cmd->host_scribble = (uchar *) hostdata->input_Q;
					hostdata->input_Q = cmd;
				} else {
					printk("---%02x:%02x:%02x-TROUBLE: Intrusive ReSelect!---", asr, sr, phs);
					while (1)
						printk("\r");
				}
			}

		}

		/* OK - find out which device reselected us. */

		id = read_3393(hostdata, WD_SOURCE_ID);
		id &= SRCID_MASK;

		/* and extract the lun from the ID message. (Note that we don't
		 * bother to check for a valid message here - I guess this is
		 * not the right way to go, but....)
		 */

		lun = read_3393(hostdata, WD_DATA);
		if (hostdata->level2 < L2_RESELECT)
			write_3393_cmd(hostdata, WD_CMD_NEGATE_ACK);
		lun &= 7;

		/* Now we look for the command that's reconnecting. */

		cmd = (Scsi_Cmnd *) hostdata->disconnected_Q;
		patch = NULL;
		while (cmd) {
			if (id == cmd->device->id && lun == cmd->device->lun)
				break;
			patch = cmd;
			cmd = (Scsi_Cmnd *) cmd->host_scribble;
		}

		/* Hmm. Couldn't find a valid command.... What to do? */

		if (!cmd) {
			printk("---TROUBLE: target %d.%d not in disconnect queue---", id, lun);
			break;
		}

		/* Ok, found the command - now start it up again. */

		if (patch)
			patch->host_scribble = cmd->host_scribble;
		else
			hostdata->disconnected_Q = (Scsi_Cmnd *) cmd->host_scribble;
		hostdata->connected = cmd;

		/* We don't need to worry about 'initialize_SCp()' or 'hostdata->busy[]'
		 * because these things are preserved over a disconnect.
		 * But we DO need to fix the DPD bit so it's correct for this command.
		 */

		if (is_dir_out(cmd))
			write_3393(hostdata, WD_DESTINATION_ID, cmd->device->id);
		else
			write_3393(hostdata, WD_DESTINATION_ID, cmd->device->id | DSTID_DPD);
		if (hostdata->level2 >= L2_RESELECT) {
			write_3393_count(hostdata, 0);	/* we want a DATA_PHASE interrupt */
			write_3393(hostdata, WD_COMMAND_PHASE, 0x45);
			write_3393_cmd(hostdata, WD_CMD_SEL_ATN_XFER);
			hostdata->state = S_RUNNING_LEVEL2;
		} else
			hostdata->state = S_CONNECTED;

		DB(DB_INTR, printk("-%ld", cmd->pid))
		    break;

	default:
		printk("--UNKNOWN INTERRUPT:%02x:%02x:%02x--", asr, sr, phs);
	}

	write1_io(0, IO_LED_OFF);

	DB(DB_INTR, printk("} "))

/* release the SMP spin_lock and restore irq state */
	    spin_unlock_irqrestore(instance->host_lock, flags);
	return IRQ_HANDLED;
}



#define RESET_CARD         0
#define RESET_CARD_AND_BUS 1
#define B_FLAG 0x80

/*
 *	Caller must hold instance lock!
 */

static int reset_hardware(struct Scsi_Host *instance, int type)
{
	struct IN2000_hostdata *hostdata;
	int qt, x;

	hostdata = (struct IN2000_hostdata *) instance->hostdata;

	write1_io(0, IO_LED_ON);
	if (type == RESET_CARD_AND_BUS) {
		write1_io(0, IO_CARD_RESET);
		x = read1_io(IO_HARDWARE);
	}
	x = read_3393(hostdata, WD_SCSI_STATUS);	/* clear any WD intrpt */
	write_3393(hostdata, WD_OWN_ID, instance->this_id | OWNID_EAF | OWNID_RAF | OWNID_FS_8);
	write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
	write_3393(hostdata, WD_SYNCHRONOUS_TRANSFER, calc_sync_xfer(hostdata->default_sx_per / 4, DEFAULT_SX_OFF));

	write1_io(0, IO_FIFO_WRITE);	/* clear fifo counter */
	write1_io(0, IO_FIFO_READ);	/* start fifo out in read mode */
	write_3393(hostdata, WD_COMMAND, WD_CMD_RESET);
	/* FIXME: timeout ?? */
	while (!(READ_AUX_STAT() & ASR_INT))
		cpu_relax();	/* wait for RESET to complete */

	x = read_3393(hostdata, WD_SCSI_STATUS);	/* clear interrupt */

	write_3393(hostdata, WD_QUEUE_TAG, 0xa5);	/* any random number */
	qt = read_3393(hostdata, WD_QUEUE_TAG);
	if (qt == 0xa5) {
		x |= B_FLAG;
		write_3393(hostdata, WD_QUEUE_TAG, 0);
	}
	write_3393(hostdata, WD_TIMEOUT_PERIOD, TIMEOUT_PERIOD_VALUE);
	write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
	write1_io(0, IO_LED_OFF);
	return x;
}



static int in2000_bus_reset(Scsi_Cmnd * cmd)
{
	struct Scsi_Host *instance;
	struct IN2000_hostdata *hostdata;
	int x;

	instance = cmd->device->host;
	hostdata = (struct IN2000_hostdata *) instance->hostdata;

	printk(KERN_WARNING "scsi%d: Reset. ", instance->host_no);

	/* do scsi-reset here */

	reset_hardware(instance, RESET_CARD_AND_BUS);
	for (x = 0; x < 8; x++) {
		hostdata->busy[x] = 0;
		hostdata->sync_xfer[x] = calc_sync_xfer(DEFAULT_SX_PER / 4, DEFAULT_SX_OFF);
		hostdata->sync_stat[x] = SS_UNSET;	/* using default sync values */
	}
	hostdata->input_Q = NULL;
	hostdata->selecting = NULL;
	hostdata->connected = NULL;
	hostdata->disconnected_Q = NULL;
	hostdata->state = S_UNCONNECTED;
	hostdata->fifo = FI_FIFO_UNUSED;
	hostdata->incoming_ptr = 0;
	hostdata->outgoing_len = 0;

	cmd->result = DID_RESET << 16;
	return SUCCESS;
}

static int in2000_host_reset(Scsi_Cmnd * cmd)
{
	return FAILED;
}

static int in2000_device_reset(Scsi_Cmnd * cmd)
{
	return FAILED;
}


static int in2000_abort(Scsi_Cmnd * cmd)
{
	struct Scsi_Host *instance;
	struct IN2000_hostdata *hostdata;
	Scsi_Cmnd *tmp, *prev;
	uchar sr, asr;
	unsigned long timeout;

	instance = cmd->device->host;
	hostdata = (struct IN2000_hostdata *) instance->hostdata;

	printk(KERN_DEBUG "scsi%d: Abort-", instance->host_no);
	printk("(asr=%02x,count=%ld,resid=%d,buf_resid=%d,have_data=%d,FC=%02x)- ", READ_AUX_STAT(), read_3393_count(hostdata), cmd->SCp.this_residual, cmd->SCp.buffers_residual, cmd->SCp.have_data_in, read1_io(IO_FIFO_COUNT));

/*
 * Case 1 : If the command hasn't been issued yet, we simply remove it
 *     from the inout_Q.
 */

	tmp = (Scsi_Cmnd *) hostdata->input_Q;
	prev = NULL;
	while (tmp) {
		if (tmp == cmd) {
			if (prev)
				prev->host_scribble = cmd->host_scribble;
			cmd->host_scribble = NULL;
			cmd->result = DID_ABORT << 16;
			printk(KERN_WARNING "scsi%d: Abort - removing command %ld from input_Q. ", instance->host_no, cmd->pid);
			cmd->scsi_done(cmd);
			return SUCCESS;
		}
		prev = tmp;
		tmp = (Scsi_Cmnd *) tmp->host_scribble;
	}

/*
 * Case 2 : If the command is connected, we're going to fail the abort
 *     and let the high level SCSI driver retry at a later time or
 *     issue a reset.
 *
 *     Timeouts, and therefore aborted commands, will be highly unlikely
 *     and handling them cleanly in this situation would make the common
 *     case of noresets less efficient, and would pollute our code.  So,
 *     we fail.
 */

	if (hostdata->connected == cmd) {

		printk(KERN_WARNING "scsi%d: Aborting connected command %ld - ", instance->host_no, cmd->pid);

		printk("sending wd33c93 ABORT command - ");
		write_3393(hostdata, WD_CONTROL, CTRL_IDI | CTRL_EDI | CTRL_POLLED);
		write_3393_cmd(hostdata, WD_CMD_ABORT);

/* Now we have to attempt to flush out the FIFO... */

		printk("flushing fifo - ");
		timeout = 1000000;
		do {
			asr = READ_AUX_STAT();
			if (asr & ASR_DBR)
				read_3393(hostdata, WD_DATA);
		} while (!(asr & ASR_INT) && timeout-- > 0);
		sr = read_3393(hostdata, WD_SCSI_STATUS);
		printk("asr=%02x, sr=%02x, %ld bytes un-transferred (timeout=%ld) - ", asr, sr, read_3393_count(hostdata), timeout);

		/*
		 * Abort command processed.
		 * Still connected.
		 * We must disconnect.
		 */

		printk("sending wd33c93 DISCONNECT command - ");
		write_3393_cmd(hostdata, WD_CMD_DISCONNECT);

		timeout = 1000000;
		asr = READ_AUX_STAT();
		while ((asr & ASR_CIP) && timeout-- > 0)
			asr = READ_AUX_STAT();
		sr = read_3393(hostdata, WD_SCSI_STATUS);
		printk("asr=%02x, sr=%02x.", asr, sr);

		hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
		hostdata->connected = NULL;
		hostdata->state = S_UNCONNECTED;
		cmd->result = DID_ABORT << 16;
		cmd->scsi_done(cmd);

		in2000_execute(instance);

		return SUCCESS;
	}

/*
 * Case 3: If the command is currently disconnected from the bus,
 * we're not going to expend much effort here: Let's just return
 * an ABORT_SNOOZE and hope for the best...
 */

	for (tmp = (Scsi_Cmnd *) hostdata->disconnected_Q; tmp; tmp = (Scsi_Cmnd *) tmp->host_scribble)
		if (cmd == tmp) {
			printk(KERN_DEBUG "scsi%d: unable to abort disconnected command.\n", instance->host_no);
			return FAILED;
		}

/*
 * Case 4 : If we reached this point, the command was not found in any of
 *     the queues.
 *
 * We probably reached this point because of an unlikely race condition
 * between the command completing successfully and the abortion code,
 * so we won't panic, but we will notify the user in case something really
 * broke.
 */

	in2000_execute(instance);

	printk("scsi%d: warning : SCSI command probably completed successfully" "         before abortion. ", instance->host_no);
	return SUCCESS;
}



#define MAX_IN2000_HOSTS 3
#define MAX_SETUP_ARGS (sizeof(setup_args) / sizeof(char *))
#define SETUP_BUFFER_SIZE 200
static char setup_buffer[SETUP_BUFFER_SIZE];
static char setup_used[MAX_SETUP_ARGS];
static int done_setup = 0;

static void __init in2000_setup(char *str, int *ints)
{
	int i;
	char *p1, *p2;

	strlcpy(setup_buffer, str, SETUP_BUFFER_SIZE);
	p1 = setup_buffer;
	i = 0;
	while (*p1 && (i < MAX_SETUP_ARGS)) {
		p2 = strchr(p1, ',');
		if (p2) {
			*p2 = '\0';
			if (p1 != p2)
				setup_args[i] = p1;
			p1 = p2 + 1;
			i++;
		} else {
			setup_args[i] = p1;
			break;
		}
	}
	for (i = 0; i < MAX_SETUP_ARGS; i++)
		setup_used[i] = 0;
	done_setup = 1;
}


/* check_setup_args() returns index if key found, 0 if not
 */

static int __init check_setup_args(char *key, int *val, char *buf)
{
	int x;
	char *cp;

	for (x = 0; x < MAX_SETUP_ARGS; x++) {
		if (setup_used[x])
			continue;
		if (!strncmp(setup_args[x], key, strlen(key)))
			break;
	}
	if (x == MAX_SETUP_ARGS)
		return 0;
	setup_used[x] = 1;
	cp = setup_args[x] + strlen(key);
	*val = -1;
	if (*cp != ':')
		return ++x;
	cp++;
	if ((*cp >= '0') && (*cp <= '9')) {
		*val = simple_strtoul(cp, NULL, 0);
	}
	return ++x;
}



/* The "correct" (ie portable) way to access memory-mapped hardware
 * such as the IN2000 EPROM and dip switch is through the use of
 * special macros declared in 'asm/io.h'. We use readb() and readl()
 * when reading from the card's BIOS area in in2000_detect().
 */
static u32 bios_tab[] in2000__INITDATA = {
	0xc8000,
	0xd0000,
	0xd8000,
	0
};

static unsigned short base_tab[] in2000__INITDATA = {
	0x220,
	0x200,
	0x110,
	0x100,
};

static int int_tab[] in2000__INITDATA = {
	15,
	14,
	11,
	10
};


static int __init in2000_detect(Scsi_Host_Template * tpnt)
{
	struct Scsi_Host *instance;
	struct IN2000_hostdata *hostdata;
	int detect_count;
	int bios;
	int x;
	unsigned short base;
	uchar switches;
	uchar hrev;
	unsigned long flags;
	int val;
	char buf[32];

/* Thanks to help from Bill Earnest, probing for IN2000 cards is a
 * pretty straightforward and fool-proof operation. There are 3
 * possible locations for the IN2000 EPROM in memory space - if we
 * find a BIOS signature, we can read the dip switch settings from
 * the byte at BIOS+32 (shadowed in by logic on the card). From 2
 * of the switch bits we get the card's address in IO space. There's
 * an image of the dip switch there, also, so we have a way to back-
 * check that this really is an IN2000 card. Very nifty. Use the
 * 'ioport:xx' command-line parameter if your BIOS EPROM is absent
 * or disabled.
 */

	if (!done_setup && setup_strings)
		in2000_setup(setup_strings, NULL);

	detect_count = 0;
	for (bios = 0; bios_tab[bios]; bios++) {
		if (check_setup_args("ioport", &val, buf)) {
			base = val;
			switches = ~inb(base + IO_SWITCHES) & 0xff;
			printk("Forcing IN2000 detection at IOport 0x%x ", base);
			bios = 2;
		}
/*
 * There have been a couple of BIOS versions with different layouts
 * for the obvious ID strings. We look for the 2 most common ones and
 * hope that they cover all the cases...
 */
		else if (isa_readl(bios_tab[bios] + 0x10) == 0x41564f4e || isa_readl(bios_tab[bios] + 0x30) == 0x61776c41) {
			printk("Found IN2000 BIOS at 0x%x ", (unsigned int) bios_tab[bios]);

/* Read the switch image that's mapped into EPROM space */

			switches = ~((isa_readb(bios_tab[bios] + 0x20) & 0xff));

/* Find out where the IO space is */

			x = switches & (SW_ADDR0 | SW_ADDR1);
			base = base_tab[x];

/* Check for the IN2000 signature in IO space. */

			x = ~inb(base + IO_SWITCHES) & 0xff;
			if (x != switches) {
				printk("Bad IO signature: %02x vs %02x.\n", x, switches);
				continue;
			}
		} else
			continue;

/* OK. We have a base address for the IO ports - run a few safety checks */

		if (!(switches & SW_BIT7)) {	/* I _think_ all cards do this */
			printk("There is no IN-2000 SCSI card at IOport 0x%03x!\n", base);
			continue;
		}

/* Let's assume any hardware version will work, although the driver
 * has only been tested on 0x21, 0x22, 0x25, 0x26, and 0x27. We'll
 * print out the rev number for reference later, but accept them all.
 */

		hrev = inb(base + IO_HARDWARE);

		/* Bit 2 tells us if interrupts are disabled */
		if (switches & SW_DISINT) {
			printk("The IN-2000 SCSI card at IOport 0x%03x ", base);
			printk("is not configured for interrupt operation!\n");
			printk("This driver requires an interrupt: cancelling detection.\n");
			continue;
		}

/* Ok. We accept that there's an IN2000 at ioaddr 'base'. Now
 * initialize it.
 */

		tpnt->proc_name = "in2000";
		instance = scsi_register(tpnt, sizeof(struct IN2000_hostdata));
		if (instance == NULL)
			continue;
		detect_count++;
		hostdata = (struct IN2000_hostdata *) instance->hostdata;
		instance->io_port = hostdata->io_base = base;
		hostdata->dip_switch = switches;
		hostdata->hrev = hrev;

		write1_io(0, IO_FIFO_WRITE);	/* clear fifo counter */
		write1_io(0, IO_FIFO_READ);	/* start fifo out in read mode */
		write1_io(0, IO_INTR_MASK);	/* allow all ints */
		x = int_tab[(switches & (SW_INT0 | SW_INT1)) >> SW_INT_SHIFT];
		if (request_irq(x, in2000_intr, SA_INTERRUPT, "in2000", instance)) {
			printk("in2000_detect: Unable to allocate IRQ.\n");
			detect_count--;
			continue;
		}
		instance->irq = x;
		instance->n_io_port = 13;
		request_region(base, 13, "in2000");	/* lock in this IO space for our use */

		for (x = 0; x < 8; x++) {
			hostdata->busy[x] = 0;
			hostdata->sync_xfer[x] = calc_sync_xfer(DEFAULT_SX_PER / 4, DEFAULT_SX_OFF);
			hostdata->sync_stat[x] = SS_UNSET;	/* using default sync values */
#ifdef PROC_STATISTICS
			hostdata->cmd_cnt[x] = 0;
			hostdata->disc_allowed_cnt[x] = 0;
			hostdata->disc_done_cnt[x] = 0;
#endif
		}
		hostdata->input_Q = NULL;
		hostdata->selecting = NULL;
		hostdata->connected = NULL;
		hostdata->disconnected_Q = NULL;
		hostdata->state = S_UNCONNECTED;
		hostdata->fifo = FI_FIFO_UNUSED;
		hostdata->level2 = L2_BASIC;
		hostdata->disconnect = DIS_ADAPTIVE;
		hostdata->args = DEBUG_DEFAULTS;
		hostdata->incoming_ptr = 0;
		hostdata->outgoing_len = 0;
		hostdata->default_sx_per = DEFAULT_SX_PER;

/* Older BIOS's had a 'sync on/off' switch - use its setting */

		if (isa_readl(bios_tab[bios] + 0x10) == 0x41564f4e && (switches & SW_SYNC_DOS5))
			hostdata->sync_off = 0x00;	/* sync defaults to on */
		else
			hostdata->sync_off = 0xff;	/* sync defaults to off */

#ifdef PROC_INTERFACE
		hostdata->proc = PR_VERSION | PR_INFO | PR_STATISTICS | PR_CONNECTED | PR_INPUTQ | PR_DISCQ | PR_STOP;
#ifdef PROC_STATISTICS
		hostdata->int_cnt = 0;
#endif
#endif

		if (check_setup_args("nosync", &val, buf))
			hostdata->sync_off = val;

		if (check_setup_args("period", &val, buf))
			hostdata->default_sx_per = sx_table[round_period((unsigned int) val)].period_ns;

		if (check_setup_args("disconnect", &val, buf)) {
			if ((val >= DIS_NEVER) && (val <= DIS_ALWAYS))
				hostdata->disconnect = val;
			else
				hostdata->disconnect = DIS_ADAPTIVE;
		}

		if (check_setup_args("noreset", &val, buf))
			hostdata->args ^= A_NO_SCSI_RESET;

		if (check_setup_args("level2", &val, buf))
			hostdata->level2 = val;

		if (check_setup_args("debug", &val, buf))
			hostdata->args = (val & DB_MASK);

#ifdef PROC_INTERFACE
		if (check_setup_args("proc", &val, buf))
			hostdata->proc = val;
#endif


		/* FIXME: not strictly needed I think but the called code expects
		   to be locked */
		spin_lock_irqsave(instance->host_lock, flags);
		x = reset_hardware(instance, (hostdata->args & A_NO_SCSI_RESET) ? RESET_CARD : RESET_CARD_AND_BUS);
		spin_unlock_irqrestore(instance->host_lock, flags);

		hostdata->microcode = read_3393(hostdata, WD_CDB_1);
		if (x & 0x01) {
			if (x & B_FLAG)
				hostdata->chip = C_WD33C93B;
			else
				hostdata->chip = C_WD33C93A;
		} else
			hostdata->chip = C_WD33C93;

		printk("dip_switch=%02x irq=%d ioport=%02x floppy=%s sync/DOS5=%s ", (switches & 0x7f), instance->irq, hostdata->io_base, (switches & SW_FLOPPY) ? "Yes" : "No", (switches & SW_SYNC_DOS5) ? "Yes" : "No");
		printk("hardware_ver=%02x chip=%s microcode=%02x\n", hrev, (hostdata->chip == C_WD33C93) ? "WD33c93" : (hostdata->chip == C_WD33C93A) ? "WD33c93A" : (hostdata->chip == C_WD33C93B) ? "WD33c93B" : "unknown", hostdata->microcode);
#ifdef DEBUGGING_ON
		printk("setup_args = ");
		for (x = 0; x < MAX_SETUP_ARGS; x++)
			printk("%s,", setup_args[x]);
		printk("\n");
#endif
		if (hostdata->sync_off == 0xff)
			printk("Sync-transfer DISABLED on all devices: ENABLE from command-line\n");
		printk("IN2000 driver version %s - %s\n", IN2000_VERSION, IN2000_DATE);
	}

	return detect_count;
}

static int in2000_release(struct Scsi_Host *shost)
{
	if (shost->irq)
		free_irq(shost->irq, shost);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	return 0;
}

/* NOTE: I lifted this function straight out of the old driver,
 *       and have not tested it. Presumably it does what it's
 *       supposed to do...
 */

static int in2000_biosparam(struct scsi_device *sdev, struct block_device *bdev, sector_t capacity, int *iinfo)
{
	int size;

	size = capacity;
	iinfo[0] = 64;
	iinfo[1] = 32;
	iinfo[2] = size >> 11;

/* This should approximate the large drive handling that the DOS ASPI manager
   uses.  Drives very near the boundaries may not be handled correctly (i.e.
   near 2.0 Gb and 4.0 Gb) */

	if (iinfo[2] > 1024) {
		iinfo[0] = 64;
		iinfo[1] = 63;
		iinfo[2] = (unsigned long) capacity / (iinfo[0] * iinfo[1]);
	}
	if (iinfo[2] > 1024) {
		iinfo[0] = 128;
		iinfo[1] = 63;
		iinfo[2] = (unsigned long) capacity / (iinfo[0] * iinfo[1]);
	}
	if (iinfo[2] > 1024) {
		iinfo[0] = 255;
		iinfo[1] = 63;
		iinfo[2] = (unsigned long) capacity / (iinfo[0] * iinfo[1]);
	}
	return 0;
}


static int in2000_proc_info(struct Scsi_Host *instance, char *buf, char **start, off_t off, int len, int in)
{

#ifdef PROC_INTERFACE

	char *bp;
	char tbuf[128];
	unsigned long flags;
	struct IN2000_hostdata *hd;
	Scsi_Cmnd *cmd;
	int x, i;
	static int stop = 0;

	hd = (struct IN2000_hostdata *) instance->hostdata;

/* If 'in' is TRUE we need to _read_ the proc file. We accept the following
 * keywords (same format as command-line, but only ONE per read):
 *    debug
 *    disconnect
 *    period
 *    resync
 *    proc
 */

	if (in) {
		buf[len] = '\0';
		bp = buf;
		if (!strncmp(bp, "debug:", 6)) {
			bp += 6;
			hd->args = simple_strtoul(bp, NULL, 0) & DB_MASK;
		} else if (!strncmp(bp, "disconnect:", 11)) {
			bp += 11;
			x = simple_strtoul(bp, NULL, 0);
			if (x < DIS_NEVER || x > DIS_ALWAYS)
				x = DIS_ADAPTIVE;
			hd->disconnect = x;
		} else if (!strncmp(bp, "period:", 7)) {
			bp += 7;
			x = simple_strtoul(bp, NULL, 0);
			hd->default_sx_per = sx_table[round_period((unsigned int) x)].period_ns;
		} else if (!strncmp(bp, "resync:", 7)) {
			bp += 7;
			x = simple_strtoul(bp, NULL, 0);
			for (i = 0; i < 7; i++)
				if (x & (1 << i))
					hd->sync_stat[i] = SS_UNSET;
		} else if (!strncmp(bp, "proc:", 5)) {
			bp += 5;
			hd->proc = simple_strtoul(bp, NULL, 0);
		} else if (!strncmp(bp, "level2:", 7)) {
			bp += 7;
			hd->level2 = simple_strtoul(bp, NULL, 0);
		}
		return len;
	}

	spin_lock_irqsave(instance->host_lock, flags);
	bp = buf;
	*bp = '\0';
	if (hd->proc & PR_VERSION) {
		sprintf(tbuf, "\nVersion %s - %s. Compiled %s %s", IN2000_VERSION, IN2000_DATE, __DATE__, __TIME__);
		strcat(bp, tbuf);
	}
	if (hd->proc & PR_INFO) {
		sprintf(tbuf, "\ndip_switch=%02x: irq=%d io=%02x floppy=%s sync/DOS5=%s", (hd->dip_switch & 0x7f), instance->irq, hd->io_base, (hd->dip_switch & 0x40) ? "Yes" : "No", (hd->dip_switch & 0x20) ? "Yes" : "No");
		strcat(bp, tbuf);
		strcat(bp, "\nsync_xfer[] =       ");
		for (x = 0; x < 7; x++) {
			sprintf(tbuf, "\t%02x", hd->sync_xfer[x]);
			strcat(bp, tbuf);
		}
		strcat(bp, "\nsync_stat[] =       ");
		for (x = 0; x < 7; x++) {
			sprintf(tbuf, "\t%02x", hd->sync_stat[x]);
			strcat(bp, tbuf);
		}
	}
#ifdef PROC_STATISTICS
	if (hd->proc & PR_STATISTICS) {
		strcat(bp, "\ncommands issued:    ");
		for (x = 0; x < 7; x++) {
			sprintf(tbuf, "\t%ld", hd->cmd_cnt[x]);
			strcat(bp, tbuf);
		}
		strcat(bp, "\ndisconnects allowed:");
		for (x = 0; x < 7; x++) {
			sprintf(tbuf, "\t%ld", hd->disc_allowed_cnt[x]);
			strcat(bp, tbuf);
		}
		strcat(bp, "\ndisconnects done:   ");
		for (x = 0; x < 7; x++) {
			sprintf(tbuf, "\t%ld", hd->disc_done_cnt[x]);
			strcat(bp, tbuf);
		}
		sprintf(tbuf, "\ninterrupts:      \t%ld", hd->int_cnt);
		strcat(bp, tbuf);
	}
#endif
	if (hd->proc & PR_CONNECTED) {
		strcat(bp, "\nconnected:     ");
		if (hd->connected) {
			cmd = (Scsi_Cmnd *) hd->connected;
			sprintf(tbuf, " %ld-%d:%d(%02x)", cmd->pid, cmd->device->id, cmd->device->lun, cmd->cmnd[0]);
			strcat(bp, tbuf);
		}
	}
	if (hd->proc & PR_INPUTQ) {
		strcat(bp, "\ninput_Q:       ");
		cmd = (Scsi_Cmnd *) hd->input_Q;
		while (cmd) {
			sprintf(tbuf, " %ld-%d:%d(%02x)", cmd->pid, cmd->device->id, cmd->device->lun, cmd->cmnd[0]);
			strcat(bp, tbuf);
			cmd = (Scsi_Cmnd *) cmd->host_scribble;
		}
	}
	if (hd->proc & PR_DISCQ) {
		strcat(bp, "\ndisconnected_Q:");
		cmd = (Scsi_Cmnd *) hd->disconnected_Q;
		while (cmd) {
			sprintf(tbuf, " %ld-%d:%d(%02x)", cmd->pid, cmd->device->id, cmd->device->lun, cmd->cmnd[0]);
			strcat(bp, tbuf);
			cmd = (Scsi_Cmnd *) cmd->host_scribble;
		}
	}
	if (hd->proc & PR_TEST) {
		;		/* insert your own custom function here */
	}
	strcat(bp, "\n");
	spin_unlock_irqrestore(instance->host_lock, flags);
	*start = buf;
	if (stop) {
		stop = 0;
		return 0;	/* return 0 to signal end-of-file */
	}
	if (off > 0x40000)	/* ALWAYS stop after 256k bytes have been read */
		stop = 1;
	if (hd->proc & PR_STOP)	/* stop every other time */
		stop = 1;
	return strlen(bp);

#else				/* PROC_INTERFACE */

	return 0;

#endif				/* PROC_INTERFACE */

}

MODULE_LICENSE("GPL");


static Scsi_Host_Template driver_template = {
	.proc_name       		= "in2000",
	.proc_info       		= in2000_proc_info,
	.name            		= "Always IN2000",
	.detect          		= in2000_detect, 
	.release			= in2000_release,
	.queuecommand    		= in2000_queuecommand,
	.eh_abort_handler		= in2000_abort,
	.eh_bus_reset_handler		= in2000_bus_reset,
	.eh_device_reset_handler	= in2000_device_reset,
	.eh_host_reset_handler	= in2000_host_reset, 
	.bios_param      		= in2000_biosparam, 
	.can_queue       		= IN2000_CAN_Q,
	.this_id         		= IN2000_HOST_ID,
	.sg_tablesize    		= IN2000_SG,
	.cmd_per_lun     		= IN2000_CPL,
	.use_clustering  		= DISABLE_CLUSTERING,
};
#include "scsi_module.c"
