/*
 *  linux/drivers/acorn/scsi/acornscsi.c
 *
 *  Acorn SCSI 3 driver
 *  By R.M.King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Abandoned using the Select and Transfer command since there were
 * some nasty races between our software and the target devices that
 * were not easy to solve, and the device errata had a lot of entries
 * for this command, some of them quite nasty...
 *
 * Changelog:
 *  26-Sep-1997	RMK	Re-jigged to use the queue module.
 *			Re-coded state machine to be based on driver
 *			state not scsi state.  Should be easier to debug.
 *			Added acornscsi_release to clean up properly.
 *			Updated proc/scsi reporting.
 *  05-Oct-1997	RMK	Implemented writing to SCSI devices.
 *  06-Oct-1997	RMK	Corrected small (non-serious) bug with the connect/
 *			reconnect race condition causing a warning message.
 *  12-Oct-1997	RMK	Added catch for re-entering interrupt routine.
 *  15-Oct-1997	RMK	Improved handling of commands.
 *  27-Jun-1998	RMK	Changed asm/delay.h to linux/delay.h.
 *  13-Dec-1998	RMK	Better abort code and command handling.  Extra state
 *			transitions added to allow dodgy devices to work.
 */
#define DEBUG_NO_WRITE	1
#define DEBUG_QUEUES	2
#define DEBUG_DMA	4
#define DEBUG_ABORT	8
#define DEBUG_DISCON	16
#define DEBUG_CONNECT	32
#define DEBUG_PHASES	64
#define DEBUG_WRITE	128
#define DEBUG_LINK	256
#define DEBUG_MESSAGES	512
#define DEBUG_RESET	1024
#define DEBUG_ALL	(DEBUG_RESET|DEBUG_MESSAGES|DEBUG_LINK|DEBUG_WRITE|\
			 DEBUG_PHASES|DEBUG_CONNECT|DEBUG_DISCON|DEBUG_ABORT|\
			 DEBUG_DMA|DEBUG_QUEUES)

/* DRIVER CONFIGURATION
 *
 * SCSI-II Tagged queue support.
 *
 * I don't have any SCSI devices that support it, so it is totally untested
 * (except to make sure that it doesn't interfere with any non-tagging
 * devices).  It is not fully implemented either - what happens when a
 * tagging device reconnects???
 *
 * You can tell if you have a device that supports tagged queueing my
 * cating (eg) /proc/scsi/acornscsi/0 and see if the SCSI revision is reported
 * as '2 TAG'.
 *
 * Also note that CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE is normally set in the config
 * scripts, but disabled here.  Once debugged, remove the #undef, otherwise to debug,
 * comment out the undef.
 */
#undef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
/*
 * SCSI-II Linked command support.
 *
 * The higher level code doesn't support linked commands yet, and so the option
 * is undef'd here.
 */
#undef CONFIG_SCSI_ACORNSCSI_LINK
/*
 * SCSI-II Synchronous transfer support.
 *
 * Tried and tested...
 *
 * SDTR_SIZE	  - maximum number of un-acknowledged bytes (0 = off, 12 = max)
 * SDTR_PERIOD	  - period of REQ signal (min=125, max=1020)
 * DEFAULT_PERIOD - default REQ period.
 */
#define SDTR_SIZE	12
#define SDTR_PERIOD	125
#define DEFAULT_PERIOD	500

/*
 * Debugging information
 *
 * DEBUG	  - bit mask from list above
 * DEBUG_TARGET   - is defined to the target number if you want to debug
 *		    a specific target. [only recon/write/dma].
 */
#define DEBUG (DEBUG_RESET|DEBUG_WRITE|DEBUG_NO_WRITE)
/* only allow writing to SCSI device 0 */
#define NO_WRITE 0xFE
/*#define DEBUG_TARGET 2*/
/*
 * Select timeout time (in 10ms units)
 *
 * This is the timeout used between the start of selection and the WD33C93
 * chip deciding that the device isn't responding.
 */
#define TIMEOUT_TIME 10
/*
 * Define this if you want to have verbose explaination of SCSI
 * status/messages.
 */
#undef CONFIG_ACORNSCSI_CONSTANTS
/*
 * Define this if you want to use the on board DMAC [don't remove this option]
 * If not set, then use PIO mode (not currently supported).
 */
#define USE_DMAC

/*
 * ====================================================================================
 */

#ifdef DEBUG_TARGET
#define DBG(cmd,xxx...) \
  if (cmd->device->id == DEBUG_TARGET) { \
    xxx; \
  }
#else
#define DBG(cmd,xxx...) xxx
#endif

#ifndef STRINGIFY
#define STRINGIFY(x) #x
#endif
#define STRx(x) STRINGIFY(x)
#define NO_WRITE_STR STRx(NO_WRITE)

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/ecard.h>

#include "../scsi.h"
#include <scsi/scsi_host.h>
#include "acornscsi.h"
#include "msgqueue.h"
#include "scsi.h"

#include <scsi/scsicam.h>

#define VER_MAJOR 2
#define VER_MINOR 0
#define VER_PATCH 6

#ifndef ABORT_TAG
#define ABORT_TAG 0xd
#else
#error "Yippee!  ABORT TAG is now defined!  Remove this error!"
#endif

#ifdef CONFIG_SCSI_ACORNSCSI_LINK
#error SCSI2 LINKed commands not supported (yet)!
#endif

#ifdef USE_DMAC
/*
 * DMAC setup parameters
 */ 
#define INIT_DEVCON0	(DEVCON0_RQL|DEVCON0_EXW|DEVCON0_CMP)
#define INIT_DEVCON1	(DEVCON1_BHLD)
#define DMAC_READ	(MODECON_READ)
#define DMAC_WRITE	(MODECON_WRITE)
#define INIT_SBICDMA	(CTRL_DMABURST)

#define scsi_xferred	have_data_in

/*
 * Size of on-board DMA buffer
 */
#define DMAC_BUFFER_SIZE	65536
#endif

#define STATUS_BUFFER_TO_PRINT	24

unsigned int sdtr_period = SDTR_PERIOD;
unsigned int sdtr_size   = SDTR_SIZE;

static void acornscsi_done(AS_Host *host, Scsi_Cmnd **SCpntp, unsigned int result);
static int acornscsi_reconnect_finish(AS_Host *host);
static void acornscsi_dma_cleanup(AS_Host *host);
static void acornscsi_abortcmd(AS_Host *host, unsigned char tag);

/* ====================================================================================
 * Miscellaneous
 */

static inline void
sbic_arm_write(unsigned int io_port, int reg, int value)
{
    __raw_writeb(reg, io_port);
    __raw_writeb(value, io_port + 4);
}

#define sbic_arm_writenext(io,val) \
	__raw_writeb((val), (io) + 4)

static inline
int sbic_arm_read(unsigned int io_port, int reg)
{
    if(reg == SBIC_ASR)
	   return __raw_readl(io_port) & 255;
    __raw_writeb(reg, io_port);
    return __raw_readl(io_port + 4) & 255;
}

#define sbic_arm_readnext(io) \
	__raw_readb((io) + 4)

#ifdef USE_DMAC
#define dmac_read(io_port,reg) \
	inb((io_port) + (reg))

#define dmac_write(io_port,reg,value) \
	({ outb((value), (io_port) + (reg)); })

#define dmac_clearintr(io_port) \
	({ outb(0, (io_port)); })

static inline
unsigned int dmac_address(unsigned int io_port)
{
    return dmac_read(io_port, DMAC_TXADRHI) << 16 |
	   dmac_read(io_port, DMAC_TXADRMD) << 8 |
	   dmac_read(io_port, DMAC_TXADRLO);
}

static
void acornscsi_dumpdma(AS_Host *host, char *where)
{
	unsigned int mode, addr, len;

	mode = dmac_read(host->dma.io_port, DMAC_MODECON);
	addr = dmac_address(host->dma.io_port);
	len  = dmac_read(host->dma.io_port, DMAC_TXCNTHI) << 8 |
	       dmac_read(host->dma.io_port, DMAC_TXCNTLO);

	printk("scsi%d: %s: DMAC %02x @%06x+%04x msk %02x, ",
		host->host->host_no, where,
		mode, addr, (len + 1) & 0xffff,
		dmac_read(host->dma.io_port, DMAC_MASKREG));

	printk("DMA @%06x, ", host->dma.start_addr);
	printk("BH @%p +%04x, ", host->scsi.SCp.ptr,
		host->scsi.SCp.this_residual);
	printk("DT @+%04x ST @+%04x", host->dma.transferred,
		host->scsi.SCp.scsi_xferred);
	printk("\n");
}
#endif

static
unsigned long acornscsi_sbic_xfcount(AS_Host *host)
{
    unsigned long length;

    length = sbic_arm_read(host->scsi.io_port, SBIC_TRANSCNTH) << 16;
    length |= sbic_arm_readnext(host->scsi.io_port) << 8;
    length |= sbic_arm_readnext(host->scsi.io_port);

    return length;
}

static int
acornscsi_sbic_wait(AS_Host *host, int stat_mask, int stat, int timeout, char *msg)
{
	int asr;

	do {
		asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR);

		if ((asr & stat_mask) == stat)
			return 0;

		udelay(1);
	} while (--timeout);

	printk("scsi%d: timeout while %s\n", host->host->host_no, msg);

	return -1;
}

static
int acornscsi_sbic_issuecmd(AS_Host *host, int command)
{
    if (acornscsi_sbic_wait(host, ASR_CIP, 0, 1000, "issuing command"))
	return -1;

    sbic_arm_write(host->scsi.io_port, SBIC_CMND, command);

    return 0;
}

static void
acornscsi_csdelay(unsigned int cs)
{
    unsigned long target_jiffies, flags;

    target_jiffies = jiffies + 1 + cs * HZ / 100;

    local_save_flags(flags);
    local_irq_enable();

    while (time_before(jiffies, target_jiffies)) barrier();

    local_irq_restore(flags);
}

static
void acornscsi_resetcard(AS_Host *host)
{
    unsigned int i, timeout;

    /* assert reset line */
    host->card.page_reg = 0x80;
    outb(host->card.page_reg, host->card.io_page);

    /* wait 3 cs.  SCSI standard says 25ms. */
    acornscsi_csdelay(3);

    host->card.page_reg = 0;
    outb(host->card.page_reg, host->card.io_page);

    /*
     * Should get a reset from the card
     */
    timeout = 1000;
    do {
	if (inb(host->card.io_intr) & 8)
	    break;
	udelay(1);
    } while (--timeout);

    if (timeout == 0)
	printk("scsi%d: timeout while resetting card\n",
		host->host->host_no);

    sbic_arm_read(host->scsi.io_port, SBIC_ASR);
    sbic_arm_read(host->scsi.io_port, SBIC_SSR);

    /* setup sbic - WD33C93A */
    sbic_arm_write(host->scsi.io_port, SBIC_OWNID, OWNID_EAF | host->host->this_id);
    sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_RESET);

    /*
     * Command should cause a reset interrupt
     */
    timeout = 1000;
    do {
	if (inb(host->card.io_intr) & 8)
	    break;
	udelay(1);
    } while (--timeout);

    if (timeout == 0)
	printk("scsi%d: timeout while resetting card\n",
		host->host->host_no);

    sbic_arm_read(host->scsi.io_port, SBIC_ASR);
    if (sbic_arm_read(host->scsi.io_port, SBIC_SSR) != 0x01)
	printk(KERN_CRIT "scsi%d: WD33C93A didn't give enhanced reset interrupt\n",
		host->host->host_no);

    sbic_arm_write(host->scsi.io_port, SBIC_CTRL, INIT_SBICDMA | CTRL_IDI);
    sbic_arm_write(host->scsi.io_port, SBIC_TIMEOUT, TIMEOUT_TIME);
    sbic_arm_write(host->scsi.io_port, SBIC_SYNCHTRANSFER, SYNCHTRANSFER_2DBA);
    sbic_arm_write(host->scsi.io_port, SBIC_SOURCEID, SOURCEID_ER | SOURCEID_DSP);

    host->card.page_reg = 0x40;
    outb(host->card.page_reg, host->card.io_page);

    /* setup dmac - uPC71071 */
    dmac_write(host->dma.io_port, DMAC_INIT, 0);
#ifdef USE_DMAC
    dmac_write(host->dma.io_port, DMAC_INIT, INIT_8BIT);
    dmac_write(host->dma.io_port, DMAC_CHANNEL, CHANNEL_0);
    dmac_write(host->dma.io_port, DMAC_DEVCON0, INIT_DEVCON0);
    dmac_write(host->dma.io_port, DMAC_DEVCON1, INIT_DEVCON1);
#endif

    host->SCpnt = NULL;
    host->scsi.phase = PHASE_IDLE;
    host->scsi.disconnectable = 0;

    memset(host->busyluns, 0, sizeof(host->busyluns));

    for (i = 0; i < 8; i++) {
	host->device[i].sync_state = SYNC_NEGOCIATE;
	host->device[i].disconnect_ok = 1;
    }

    /* wait 25 cs.  SCSI standard says 250ms. */
    acornscsi_csdelay(25);
}

/*=============================================================================================
 * Utility routines (eg. debug)
 */
#ifdef CONFIG_ACORNSCSI_CONSTANTS
static char *acornscsi_interrupttype[] = {
  "rst",  "suc",  "p/a",  "3",
  "term", "5",	  "6",	  "7",
  "serv", "9",	  "a",	  "b",
  "c",	  "d",	  "e",	  "f"
};

static signed char acornscsi_map[] = {
  0,  1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1,  2, -1, -1,  -1, -1,  3, -1,   4,	5,  6,	7,   8,  9, 10, 11,
 12, 13, 14, -1,  -1, -1, -1, -1,   4,	5,  6,	7,   8,  9, 10, 11,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 15, 16, 17, 18,  19, -1, -1, 20,   4,	5,  6,	7,   8,  9, 10, 11,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 21, 22, -1, -1,  -1, 23, -1, -1,   4,	5,  6,	7,   8,  9, 10, 11,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,
 -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1,  -1, -1, -1, -1
};      

static char *acornscsi_interruptcode[] = {
    /* 0 */
    "reset - normal mode",	/* 00 */
    "reset - advanced mode",	/* 01 */

    /* 2 */
    "sel",			/* 11 */
    "sel+xfer", 		/* 16 */
    "data-out", 		/* 18 */
    "data-in",			/* 19 */
    "cmd",			/* 1A */
    "stat",			/* 1B */
    "??-out",			/* 1C */
    "??-in",			/* 1D */
    "msg-out",			/* 1E */
    "msg-in",			/* 1F */

    /* 12 */
    "/ACK asserted",		/* 20 */
    "save-data-ptr",		/* 21 */
    "{re}sel",			/* 22 */

    /* 15 */
    "inv cmd",			/* 40 */
    "unexpected disconnect",	/* 41 */
    "sel timeout",		/* 42 */
    "P err",			/* 43 */
    "P err+ATN",		/* 44 */
    "bad status byte",		/* 47 */

    /* 21 */
    "resel, no id",		/* 80 */
    "resel",			/* 81 */
    "discon",			/* 85 */
};

static
void print_scsi_status(unsigned int ssr)
{
    if (acornscsi_map[ssr] != -1)
	printk("%s:%s",
		acornscsi_interrupttype[(ssr >> 4)],
		acornscsi_interruptcode[acornscsi_map[ssr]]);
    else
	printk("%X:%X", ssr >> 4, ssr & 0x0f);    
}    
#endif

static
void print_sbic_status(int asr, int ssr, int cmdphase)
{
#ifdef CONFIG_ACORNSCSI_CONSTANTS
    printk("sbic: %c%c%c%c%c%c ",
	    asr & ASR_INT ? 'I' : 'i',
	    asr & ASR_LCI ? 'L' : 'l',
	    asr & ASR_BSY ? 'B' : 'b',
	    asr & ASR_CIP ? 'C' : 'c',
	    asr & ASR_PE  ? 'P' : 'p',
	    asr & ASR_DBR ? 'D' : 'd');
    printk("scsi: ");
    print_scsi_status(ssr);
    printk(" ph %02X\n", cmdphase);
#else
    printk("sbic: %02X scsi: %X:%X ph: %02X\n",
	    asr, (ssr & 0xf0)>>4, ssr & 0x0f, cmdphase);
#endif
}

static void
acornscsi_dumplogline(AS_Host *host, int target, int line)
{
	unsigned long prev;
	signed int ptr;

	ptr = host->status_ptr[target] - STATUS_BUFFER_TO_PRINT;
	if (ptr < 0)
		ptr += STATUS_BUFFER_SIZE;

	printk("%c: %3s:", target == 8 ? 'H' : '0' + target,
		line == 0 ? "ph" : line == 1 ? "ssr" : "int");

	prev = host->status[target][ptr].when;

	for (; ptr != host->status_ptr[target]; ptr = (ptr + 1) & (STATUS_BUFFER_SIZE - 1)) {
		unsigned long time_diff;

		if (!host->status[target][ptr].when)
			continue;

		switch (line) {
		case 0:
			printk("%c%02X", host->status[target][ptr].irq ? '-' : ' ',
					 host->status[target][ptr].ph);
			break;

		case 1:
			printk(" %02X", host->status[target][ptr].ssr);
			break;

		case 2:
			time_diff = host->status[target][ptr].when - prev;
			prev = host->status[target][ptr].when;
			if (time_diff == 0)
				printk("==^");
			else if (time_diff >= 100)
				printk("   ");
			else
				printk(" %02ld", time_diff);
			break;
		}
	}

	printk("\n");
}

static
void acornscsi_dumplog(AS_Host *host, int target)
{
    do {
	acornscsi_dumplogline(host, target, 0);
	acornscsi_dumplogline(host, target, 1);
	acornscsi_dumplogline(host, target, 2);

	if (target == 8)
	    break;

	target = 8;
    } while (1);
}

static
char acornscsi_target(AS_Host *host)
{
	if (host->SCpnt)
		return '0' + host->SCpnt->device->id;
	return 'H';
}

/*
 * Prototype: cmdtype_t acornscsi_cmdtype(int command)
 * Purpose  : differentiate READ from WRITE from other commands
 * Params   : command - command to interpret
 * Returns  : CMD_READ	- command reads data,
 *	      CMD_WRITE - command writes data,
 *	      CMD_MISC	- everything else
 */
static inline
cmdtype_t acornscsi_cmdtype(int command)
{
    switch (command) {
    case WRITE_6:  case WRITE_10:  case WRITE_12:
	return CMD_WRITE;
    case READ_6:   case READ_10:   case READ_12:
	return CMD_READ;
    default:
	return CMD_MISC;
    }
}

/*
 * Prototype: int acornscsi_datadirection(int command)
 * Purpose  : differentiate between commands that have a DATA IN phase
 *	      and a DATA OUT phase
 * Params   : command - command to interpret
 * Returns  : DATADIR_OUT - data out phase expected
 *	      DATADIR_IN  - data in phase expected
 */
static
datadir_t acornscsi_datadirection(int command)
{
    switch (command) {
    case CHANGE_DEFINITION:	case COMPARE:		case COPY:
    case COPY_VERIFY:		case LOG_SELECT:	case MODE_SELECT:
    case MODE_SELECT_10:	case SEND_DIAGNOSTIC:	case WRITE_BUFFER:
    case FORMAT_UNIT:		case REASSIGN_BLOCKS:	case RESERVE:
    case SEARCH_EQUAL:		case SEARCH_HIGH:	case SEARCH_LOW:
    case WRITE_6:		case WRITE_10:		case WRITE_VERIFY:
    case UPDATE_BLOCK:		case WRITE_LONG:	case WRITE_SAME:
    case SEARCH_HIGH_12:	case SEARCH_EQUAL_12:	case SEARCH_LOW_12:
    case WRITE_12:		case WRITE_VERIFY_12:	case SET_WINDOW:
    case MEDIUM_SCAN:		case SEND_VOLUME_TAG:	case 0xea:
	return DATADIR_OUT;
    default:
	return DATADIR_IN;
    }
}

/*
 * Purpose  : provide values for synchronous transfers with 33C93.
 * Copyright: Copyright (c) 1996 John Shifflett, GeoLog Consulting
 *	Modified by Russell King for 8MHz WD33C93A
 */
static struct sync_xfer_tbl {
    unsigned int period_ns;
    unsigned char reg_value;
} sync_xfer_table[] = {
    {	1, 0x20 },    { 249, 0x20 },	{ 374, 0x30 },
    { 499, 0x40 },    { 624, 0x50 },	{ 749, 0x60 },
    { 874, 0x70 },    { 999, 0x00 },	{   0,	  0 }
};

/*
 * Prototype: int acornscsi_getperiod(unsigned char syncxfer)
 * Purpose  : period for the synchronous transfer setting
 * Params   : syncxfer SYNCXFER register value
 * Returns  : period in ns.
 */
static
int acornscsi_getperiod(unsigned char syncxfer)
{
    int i;

    syncxfer &= 0xf0;
    if (syncxfer == 0x10)
	syncxfer = 0;

    for (i = 1; sync_xfer_table[i].period_ns; i++)
	if (syncxfer == sync_xfer_table[i].reg_value)
	    return sync_xfer_table[i].period_ns;
    return 0;
}

/*
 * Prototype: int round_period(unsigned int period)
 * Purpose  : return index into above table for a required REQ period
 * Params   : period - time (ns) for REQ
 * Returns  : table index
 * Copyright: Copyright (c) 1996 John Shifflett, GeoLog Consulting
 */
static inline
int round_period(unsigned int period)
{
    int i;

    for (i = 1; sync_xfer_table[i].period_ns; i++) {
	if ((period <= sync_xfer_table[i].period_ns) &&
	    (period > sync_xfer_table[i - 1].period_ns))
	    return i;
    }
    return 7;
}

/*
 * Prototype: unsigned char calc_sync_xfer(unsigned int period, unsigned int offset)
 * Purpose  : calculate value for 33c93s SYNC register
 * Params   : period - time (ns) for REQ
 *	      offset - offset in bytes between REQ/ACK
 * Returns  : value for SYNC register
 * Copyright: Copyright (c) 1996 John Shifflett, GeoLog Consulting
 */
static
unsigned char calc_sync_xfer(unsigned int period, unsigned int offset)
{
    return sync_xfer_table[round_period(period)].reg_value |
		((offset < SDTR_SIZE) ? offset : SDTR_SIZE);
}

/* ====================================================================================
 * Command functions
 */
/*
 * Function: acornscsi_kick(AS_Host *host)
 * Purpose : kick next command to interface
 * Params  : host - host to send command to
 * Returns : INTR_IDLE if idle, otherwise INTR_PROCESSING
 * Notes   : interrupts are always disabled!
 */
static
intr_ret_t acornscsi_kick(AS_Host *host)
{
    int from_queue = 0;
    Scsi_Cmnd *SCpnt;

    /* first check to see if a command is waiting to be executed */
    SCpnt = host->origSCpnt;
    host->origSCpnt = NULL;

    /* retrieve next command */
    if (!SCpnt) {
	SCpnt = queue_remove_exclude(&host->queues.issue, host->busyluns);
	if (!SCpnt)
	    return INTR_IDLE;

	from_queue = 1;
    }

    if (host->scsi.disconnectable && host->SCpnt) {
	queue_add_cmd_tail(&host->queues.disconnected, host->SCpnt);
	host->scsi.disconnectable = 0;
#if (DEBUG & (DEBUG_QUEUES|DEBUG_DISCON))
	DBG(host->SCpnt, printk("scsi%d.%c: moved command to disconnected queue\n",
		host->host->host_no, acornscsi_target(host)));
#endif
	host->SCpnt = NULL;
    }

    /*
     * If we have an interrupt pending, then we may have been reselected.
     * In this case, we don't want to write to the registers
     */
    if (!(sbic_arm_read(host->scsi.io_port, SBIC_ASR) & (ASR_INT|ASR_BSY|ASR_CIP))) {
	sbic_arm_write(host->scsi.io_port, SBIC_DESTID, SCpnt->device->id);
	sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_SELWITHATN);
    }

    /*
     * claim host busy - all of these must happen atomically wrt
     * our interrupt routine.  Failure means command loss.
     */
    host->scsi.phase = PHASE_CONNECTING;
    host->SCpnt = SCpnt;
    host->scsi.SCp = SCpnt->SCp;
    host->dma.xfer_setup = 0;
    host->dma.xfer_required = 0;
    host->dma.xfer_done = 0;

#if (DEBUG & (DEBUG_ABORT|DEBUG_CONNECT))
    DBG(SCpnt,printk("scsi%d.%c: starting cmd %02X\n",
	    host->host->host_no, '0' + SCpnt->device->id,
	    SCpnt->cmnd[0]));
#endif

    if (from_queue) {
#ifdef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
	/*
	 * tagged queueing - allocate a new tag to this command
	 */
	if (SCpnt->device->simple_tags) {
	    SCpnt->device->current_tag += 1;
	    if (SCpnt->device->current_tag == 0)
		SCpnt->device->current_tag = 1;
	    SCpnt->tag = SCpnt->device->current_tag;
	} else
#endif
	    set_bit(SCpnt->device->id * 8 + SCpnt->device->lun, host->busyluns);

	host->stats.removes += 1;

	switch (acornscsi_cmdtype(SCpnt->cmnd[0])) {
	case CMD_WRITE:
	    host->stats.writes += 1;
	    break;
	case CMD_READ:
	    host->stats.reads += 1;
	    break;
	case CMD_MISC:
	    host->stats.miscs += 1;
	    break;
	}
    }

    return INTR_PROCESSING;
}    

/*
 * Function: void acornscsi_done(AS_Host *host, Scsi_Cmnd **SCpntp, unsigned int result)
 * Purpose : complete processing for command
 * Params  : host   - interface that completed
 *	     result - driver byte of result
 */
static
void acornscsi_done(AS_Host *host, Scsi_Cmnd **SCpntp, unsigned int result)
{
    Scsi_Cmnd *SCpnt = *SCpntp;

    /* clean up */
    sbic_arm_write(host->scsi.io_port, SBIC_SOURCEID, SOURCEID_ER | SOURCEID_DSP);

    host->stats.fins += 1;

    if (SCpnt) {
	*SCpntp = NULL;

	acornscsi_dma_cleanup(host);

	SCpnt->result = result << 16 | host->scsi.SCp.Message << 8 | host->scsi.SCp.Status;

	/*
	 * In theory, this should not happen.  In practice, it seems to.
	 * Only trigger an error if the device attempts to report all happy
	 * but with untransferred buffers...  If we don't do something, then
	 * data loss will occur.  Should we check SCpnt->underflow here?
	 * It doesn't appear to be set to something meaningful by the higher
	 * levels all the time.
	 */
	if (result == DID_OK) {
		int xfer_warn = 0;

		if (SCpnt->underflow == 0) {
			if (host->scsi.SCp.ptr &&
			    acornscsi_cmdtype(SCpnt->cmnd[0]) != CMD_MISC)
				xfer_warn = 1;
		} else {
			if (host->scsi.SCp.scsi_xferred < SCpnt->underflow ||
			    host->scsi.SCp.scsi_xferred != host->dma.transferred)
				xfer_warn = 1;
		}

		/* ANSI standard says: (SCSI-2 Rev 10c Sect 5.6.6)
		 *  Targets which break data transfers into multiple
		 *  connections shall end each successful connection
		 *  (except possibly the last) with a SAVE DATA
		 *  POINTER - DISCONNECT message sequence.
		 *
		 * This makes it difficult to ensure that a transfer has
		 * completed.  If we reach the end of a transfer during
		 * the command, then we can only have finished the transfer.
		 * therefore, if we seem to have some data remaining, this
		 * is not a problem.
		 */
		if (host->dma.xfer_done)
			xfer_warn = 0;

		if (xfer_warn) {
		    switch (status_byte(SCpnt->result)) {
		    case CHECK_CONDITION:
		    case COMMAND_TERMINATED:
		    case BUSY:
		    case QUEUE_FULL:
		    case RESERVATION_CONFLICT:
			break;

		    default:
			printk(KERN_ERR "scsi%d.H: incomplete data transfer detected: result=%08X command=",
				host->host->host_no, SCpnt->result);
			print_command(SCpnt->cmnd);
			acornscsi_dumpdma(host, "done");
		 	acornscsi_dumplog(host, SCpnt->device->id);
			SCpnt->result &= 0xffff;
			SCpnt->result |= DID_ERROR << 16;
		    }
		}
	}

	if (!SCpnt->scsi_done)
	    panic("scsi%d.H: null scsi_done function in acornscsi_done", host->host->host_no);

	clear_bit(SCpnt->device->id * 8 + SCpnt->device->lun, host->busyluns);

	SCpnt->scsi_done(SCpnt);
    } else
	printk("scsi%d: null command in acornscsi_done", host->host->host_no);

    host->scsi.phase = PHASE_IDLE;
}

/* ====================================================================================
 * DMA routines
 */
/*
 * Purpose  : update SCSI Data Pointer
 * Notes    : this will only be one SG entry or less
 */
static
void acornscsi_data_updateptr(AS_Host *host, Scsi_Pointer *SCp, unsigned int length)
{
    SCp->ptr += length;
    SCp->this_residual -= length;

    if (SCp->this_residual == 0 && next_SCp(SCp) == 0)
	host->dma.xfer_done = 1;
}

/*
 * Prototype: void acornscsi_data_read(AS_Host *host, char *ptr,
 *				unsigned int start_addr, unsigned int length)
 * Purpose  : read data from DMA RAM
 * Params   : host - host to transfer from
 *	      ptr  - DRAM address
 *	      start_addr - host mem address
 *	      length - number of bytes to transfer
 * Notes    : this will only be one SG entry or less
 */
static
void acornscsi_data_read(AS_Host *host, char *ptr,
				 unsigned int start_addr, unsigned int length)
{
    extern void __acornscsi_in(int port, char *buf, int len);
    unsigned int page, offset, len = length;

    page = (start_addr >> 12);
    offset = start_addr & ((1 << 12) - 1);

    outb((page & 0x3f) | host->card.page_reg, host->card.io_page);

    while (len > 0) {
	unsigned int this_len;

	if (len + offset > (1 << 12))
	    this_len = (1 << 12) - offset;
	else
	    this_len = len;

	__acornscsi_in(host->card.io_ram + (offset << 1), ptr, this_len);

	offset += this_len;
	ptr += this_len;
	len -= this_len;

	if (offset == (1 << 12)) {
	    offset = 0;
	    page ++;
	    outb((page & 0x3f) | host->card.page_reg, host->card.io_page);
	}
    }
    outb(host->card.page_reg, host->card.io_page);
}

/*
 * Prototype: void acornscsi_data_write(AS_Host *host, char *ptr,
 *				unsigned int start_addr, unsigned int length)
 * Purpose  : write data to DMA RAM
 * Params   : host - host to transfer from
 *	      ptr  - DRAM address
 *	      start_addr - host mem address
 *	      length - number of bytes to transfer
 * Notes    : this will only be one SG entry or less
 */
static
void acornscsi_data_write(AS_Host *host, char *ptr,
				 unsigned int start_addr, unsigned int length)
{
    extern void __acornscsi_out(int port, char *buf, int len);
    unsigned int page, offset, len = length;

    page = (start_addr >> 12);
    offset = start_addr & ((1 << 12) - 1);

    outb((page & 0x3f) | host->card.page_reg, host->card.io_page);

    while (len > 0) {
	unsigned int this_len;

	if (len + offset > (1 << 12))
	    this_len = (1 << 12) - offset;
	else
	    this_len = len;

	__acornscsi_out(host->card.io_ram + (offset << 1), ptr, this_len);

	offset += this_len;
	ptr += this_len;
	len -= this_len;

	if (offset == (1 << 12)) {
	    offset = 0;
	    page ++;
	    outb((page & 0x3f) | host->card.page_reg, host->card.io_page);
	}
    }
    outb(host->card.page_reg, host->card.io_page);
}

/* =========================================================================================
 * On-board DMA routines
 */
#ifdef USE_DMAC
/*
 * Prototype: void acornscsi_dmastop(AS_Host *host)
 * Purpose  : stop all DMA
 * Params   : host - host on which to stop DMA
 * Notes    : This is called when leaving DATA IN/OUT phase,
 *	      or when interface is RESET
 */
static inline
void acornscsi_dma_stop(AS_Host *host)
{
    dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_ON);
    dmac_clearintr(host->dma.io_intr_clear);

#if (DEBUG & DEBUG_DMA)
    DBG(host->SCpnt, acornscsi_dumpdma(host, "stop"));
#endif
}

/*
 * Function: void acornscsi_dma_setup(AS_Host *host, dmadir_t direction)
 * Purpose : setup DMA controller for data transfer
 * Params  : host - host to setup
 *	     direction - data transfer direction
 * Notes   : This is called when entering DATA I/O phase, not
 *	     while we're in a DATA I/O phase
 */
static
void acornscsi_dma_setup(AS_Host *host, dmadir_t direction)
{
    unsigned int address, length, mode;

    host->dma.direction = direction;

    dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_ON);

    if (direction == DMA_OUT) {
#if (DEBUG & DEBUG_NO_WRITE)
	if (NO_WRITE & (1 << host->SCpnt->device->id)) {
	    printk(KERN_CRIT "scsi%d.%c: I can't handle DMA_OUT!\n",
		    host->host->host_no, acornscsi_target(host));
	    return;
	}
#endif
	mode = DMAC_WRITE;
    } else
	mode = DMAC_READ;

    /*
     * Allocate some buffer space, limited to half the buffer size
     */
    length = min_t(unsigned int, host->scsi.SCp.this_residual, DMAC_BUFFER_SIZE / 2);
    if (length) {
	host->dma.start_addr = address = host->dma.free_addr;
	host->dma.free_addr = (host->dma.free_addr + length) &
				(DMAC_BUFFER_SIZE - 1);

	/*
	 * Transfer data to DMA memory
	 */
	if (direction == DMA_OUT)
	    acornscsi_data_write(host, host->scsi.SCp.ptr, host->dma.start_addr,
				length);

	length -= 1;
	dmac_write(host->dma.io_port, DMAC_TXCNTLO, length);
	dmac_write(host->dma.io_port, DMAC_TXCNTHI, length >> 8);
	dmac_write(host->dma.io_port, DMAC_TXADRLO, address);
	dmac_write(host->dma.io_port, DMAC_TXADRMD, address >> 8);
	dmac_write(host->dma.io_port, DMAC_TXADRHI, 0);
	dmac_write(host->dma.io_port, DMAC_MODECON, mode);
	dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_OFF);

#if (DEBUG & DEBUG_DMA)
	DBG(host->SCpnt, acornscsi_dumpdma(host, "strt"));
#endif
	host->dma.xfer_setup = 1;
    }
}

/*
 * Function: void acornscsi_dma_cleanup(AS_Host *host)
 * Purpose : ensure that all DMA transfers are up-to-date & host->scsi.SCp is correct
 * Params  : host - host to finish
 * Notes   : This is called when a command is:
 *		terminating, RESTORE_POINTERS, SAVE_POINTERS, DISCONECT
 *	   : This must not return until all transfers are completed.
 */
static
void acornscsi_dma_cleanup(AS_Host *host)
{
    dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_ON);
    dmac_clearintr(host->dma.io_intr_clear);

    /*
     * Check for a pending transfer
     */
    if (host->dma.xfer_required) {
	host->dma.xfer_required = 0;
	if (host->dma.direction == DMA_IN)
	    acornscsi_data_read(host, host->dma.xfer_ptr,
				 host->dma.xfer_start, host->dma.xfer_length);
    }

    /*
     * Has a transfer been setup?
     */
    if (host->dma.xfer_setup) {
	unsigned int transferred;

	host->dma.xfer_setup = 0;

#if (DEBUG & DEBUG_DMA)
	DBG(host->SCpnt, acornscsi_dumpdma(host, "cupi"));
#endif

	/*
	 * Calculate number of bytes transferred from DMA.
	 */
	transferred = dmac_address(host->dma.io_port) - host->dma.start_addr;
	host->dma.transferred += transferred;

	if (host->dma.direction == DMA_IN)
	    acornscsi_data_read(host, host->scsi.SCp.ptr,
				 host->dma.start_addr, transferred);

	/*
	 * Update SCSI pointers
	 */
	acornscsi_data_updateptr(host, &host->scsi.SCp, transferred);
#if (DEBUG & DEBUG_DMA)
	DBG(host->SCpnt, acornscsi_dumpdma(host, "cupo"));
#endif
    }
}

/*
 * Function: void acornscsi_dmacintr(AS_Host *host)
 * Purpose : handle interrupts from DMAC device
 * Params  : host - host to process
 * Notes   : If reading, we schedule the read to main memory &
 *	     allow the transfer to continue.
 *	   : If writing, we fill the onboard DMA memory from main
 *	     memory.
 *	   : Called whenever DMAC finished it's current transfer.
 */
static
void acornscsi_dma_intr(AS_Host *host)
{
    unsigned int address, length, transferred;

#if (DEBUG & DEBUG_DMA)
    DBG(host->SCpnt, acornscsi_dumpdma(host, "inti"));
#endif

    dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_ON);
    dmac_clearintr(host->dma.io_intr_clear);

    /*
     * Calculate amount transferred via DMA
     */
    transferred = dmac_address(host->dma.io_port) - host->dma.start_addr;
    host->dma.transferred += transferred;

    /*
     * Schedule DMA transfer off board
     */
    if (host->dma.direction == DMA_IN) {
	host->dma.xfer_start = host->dma.start_addr;
	host->dma.xfer_length = transferred;
	host->dma.xfer_ptr = host->scsi.SCp.ptr;
	host->dma.xfer_required = 1;
    }

    acornscsi_data_updateptr(host, &host->scsi.SCp, transferred);

    /*
     * Allocate some buffer space, limited to half the on-board RAM size
     */
    length = min_t(unsigned int, host->scsi.SCp.this_residual, DMAC_BUFFER_SIZE / 2);
    if (length) {
	host->dma.start_addr = address = host->dma.free_addr;
	host->dma.free_addr = (host->dma.free_addr + length) &
				(DMAC_BUFFER_SIZE - 1);

	/*
	 * Transfer data to DMA memory
	 */
	if (host->dma.direction == DMA_OUT)
	    acornscsi_data_write(host, host->scsi.SCp.ptr, host->dma.start_addr,
				length);

	length -= 1;
	dmac_write(host->dma.io_port, DMAC_TXCNTLO, length);
	dmac_write(host->dma.io_port, DMAC_TXCNTHI, length >> 8);
	dmac_write(host->dma.io_port, DMAC_TXADRLO, address);
	dmac_write(host->dma.io_port, DMAC_TXADRMD, address >> 8);
	dmac_write(host->dma.io_port, DMAC_TXADRHI, 0);
	dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_OFF);

#if (DEBUG & DEBUG_DMA)
	DBG(host->SCpnt, acornscsi_dumpdma(host, "into"));
#endif
    } else {
	host->dma.xfer_setup = 0;
#if 0
	/*
	 * If the interface still wants more, then this is an error.
	 * We give it another byte, but we also attempt to raise an
	 * attention condition.  We continue giving one byte until
	 * the device recognises the attention.
	 */
	if (dmac_read(host->dma.io_port, DMAC_STATUS) & STATUS_RQ0) {
	    acornscsi_abortcmd(host, host->SCpnt->tag);

	    dmac_write(host->dma.io_port, DMAC_TXCNTLO, 0);
	    dmac_write(host->dma.io_port, DMAC_TXCNTHI, 0);
	    dmac_write(host->dma.io_port, DMAC_TXADRLO, 0);
	    dmac_write(host->dma.io_port, DMAC_TXADRMD, 0);
	    dmac_write(host->dma.io_port, DMAC_TXADRHI, 0);
	    dmac_write(host->dma.io_port, DMAC_MASKREG, MASK_OFF);
	}
#endif
    }
}

/*
 * Function: void acornscsi_dma_xfer(AS_Host *host)
 * Purpose : transfer data between AcornSCSI and memory
 * Params  : host - host to process
 */
static
void acornscsi_dma_xfer(AS_Host *host)
{
    host->dma.xfer_required = 0;

    if (host->dma.direction == DMA_IN)
	acornscsi_data_read(host, host->dma.xfer_ptr,
				host->dma.xfer_start, host->dma.xfer_length);
}

/*
 * Function: void acornscsi_dma_adjust(AS_Host *host)
 * Purpose : adjust DMA pointers & count for bytes transferred to
 *	     SBIC but not SCSI bus.
 * Params  : host - host to adjust DMA count for
 */
static
void acornscsi_dma_adjust(AS_Host *host)
{
    if (host->dma.xfer_setup) {
	signed long transferred;
#if (DEBUG & (DEBUG_DMA|DEBUG_WRITE))
	DBG(host->SCpnt, acornscsi_dumpdma(host, "adji"));
#endif
	/*
	 * Calculate correct DMA address - DMA is ahead of SCSI bus while
	 * writing.
	 *  host->scsi.SCp.scsi_xferred is the number of bytes
	 *  actually transferred to/from the SCSI bus.
	 *  host->dma.transferred is the number of bytes transferred
	 *  over DMA since host->dma.start_addr was last set.
	 *
	 * real_dma_addr = host->dma.start_addr + host->scsi.SCp.scsi_xferred
	 *		   - host->dma.transferred
	 */
	transferred = host->scsi.SCp.scsi_xferred - host->dma.transferred;
	if (transferred < 0)
	    printk("scsi%d.%c: Ack! DMA write correction %ld < 0!\n",
		    host->host->host_no, acornscsi_target(host), transferred);
	else if (transferred == 0)
	    host->dma.xfer_setup = 0;
	else {
	    transferred += host->dma.start_addr;
	    dmac_write(host->dma.io_port, DMAC_TXADRLO, transferred);
	    dmac_write(host->dma.io_port, DMAC_TXADRMD, transferred >> 8);
	    dmac_write(host->dma.io_port, DMAC_TXADRHI, transferred >> 16);
#if (DEBUG & (DEBUG_DMA|DEBUG_WRITE))
	    DBG(host->SCpnt, acornscsi_dumpdma(host, "adjo"));
#endif
	}
    }
}
#endif

/* =========================================================================================
 * Data I/O
 */
static int
acornscsi_write_pio(AS_Host *host, char *bytes, int *ptr, int len, unsigned int max_timeout)
{
	unsigned int asr, timeout = max_timeout;
	int my_ptr = *ptr;

	while (my_ptr < len) {
		asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR);

		if (asr & ASR_DBR) {
			timeout = max_timeout;

			sbic_arm_write(host->scsi.io_port, SBIC_DATA, bytes[my_ptr++]);
		} else if (asr & ASR_INT)
			break;
		else if (--timeout == 0)
			break;
		udelay(1);
	}

	*ptr = my_ptr;

	return (timeout == 0) ? -1 : 0;
}

/*
 * Function: void acornscsi_sendcommand(AS_Host *host)
 * Purpose : send a command to a target
 * Params  : host - host which is connected to target
 */
static void
acornscsi_sendcommand(AS_Host *host)
{
    Scsi_Cmnd *SCpnt = host->SCpnt;

    sbic_arm_write(host->scsi.io_port, SBIC_TRANSCNTH, 0);
    sbic_arm_writenext(host->scsi.io_port, 0);
    sbic_arm_writenext(host->scsi.io_port, SCpnt->cmd_len - host->scsi.SCp.sent_command);

    acornscsi_sbic_issuecmd(host, CMND_XFERINFO);

    if (acornscsi_write_pio(host, SCpnt->cmnd,
	(int *)&host->scsi.SCp.sent_command, SCpnt->cmd_len, 1000000))
	printk("scsi%d: timeout while sending command\n", host->host->host_no);

    host->scsi.phase = PHASE_COMMAND;
}

static
void acornscsi_sendmessage(AS_Host *host)
{
    unsigned int message_length = msgqueue_msglength(&host->scsi.msgs);
    unsigned int msgnr;
    struct message *msg;

#if (DEBUG & DEBUG_MESSAGES)
    printk("scsi%d.%c: sending message ",
	    host->host->host_no, acornscsi_target(host));
#endif

    switch (message_length) {
    case 0:
	acornscsi_sbic_issuecmd(host, CMND_XFERINFO | CMND_SBT);

	acornscsi_sbic_wait(host, ASR_DBR, ASR_DBR, 1000, "sending message 1");

	sbic_arm_write(host->scsi.io_port, SBIC_DATA, NOP);

	host->scsi.last_message = NOP;
#if (DEBUG & DEBUG_MESSAGES)
	printk("NOP");
#endif
	break;

    case 1:
	acornscsi_sbic_issuecmd(host, CMND_XFERINFO | CMND_SBT);
	msg = msgqueue_getmsg(&host->scsi.msgs, 0);

	acornscsi_sbic_wait(host, ASR_DBR, ASR_DBR, 1000, "sending message 2");

	sbic_arm_write(host->scsi.io_port, SBIC_DATA, msg->msg[0]);

	host->scsi.last_message = msg->msg[0];
#if (DEBUG & DEBUG_MESSAGES)
	print_msg(msg->msg);
#endif
	break;

    default:
	/*
	 * ANSI standard says: (SCSI-2 Rev 10c Sect 5.6.14)
	 * 'When a target sends this (MESSAGE_REJECT) message, it
	 *  shall change to MESSAGE IN phase and send this message
	 *  prior to requesting additional message bytes from the
	 *  initiator.  This provides an interlock so that the
	 *  initiator can determine which message byte is rejected.
	 */
	sbic_arm_write(host->scsi.io_port, SBIC_TRANSCNTH, 0);
	sbic_arm_writenext(host->scsi.io_port, 0);
	sbic_arm_writenext(host->scsi.io_port, message_length);
	acornscsi_sbic_issuecmd(host, CMND_XFERINFO);

	msgnr = 0;
	while ((msg = msgqueue_getmsg(&host->scsi.msgs, msgnr++)) != NULL) {
	    unsigned int i;
#if (DEBUG & DEBUG_MESSAGES)
	    print_msg(msg);
#endif
	    i = 0;
	    if (acornscsi_write_pio(host, msg->msg, &i, msg->length, 1000000))
		printk("scsi%d: timeout while sending message\n", host->host->host_no);

	    host->scsi.last_message = msg->msg[0];
	    if (msg->msg[0] == EXTENDED_MESSAGE)
		host->scsi.last_message |= msg->msg[2] << 8;

	    if (i != msg->length)
		break;
	}
	break;
    }
#if (DEBUG & DEBUG_MESSAGES)
    printk("\n");
#endif
}

/*
 * Function: void acornscsi_readstatusbyte(AS_Host *host)
 * Purpose : Read status byte from connected target
 * Params  : host - host connected to target
 */
static
void acornscsi_readstatusbyte(AS_Host *host)
{
    acornscsi_sbic_issuecmd(host, CMND_XFERINFO|CMND_SBT);
    acornscsi_sbic_wait(host, ASR_DBR, ASR_DBR, 1000, "reading status byte");
    host->scsi.SCp.Status = sbic_arm_read(host->scsi.io_port, SBIC_DATA);
}

/*
 * Function: unsigned char acornscsi_readmessagebyte(AS_Host *host)
 * Purpose : Read one message byte from connected target
 * Params  : host - host connected to target
 */
static
unsigned char acornscsi_readmessagebyte(AS_Host *host)
{
    unsigned char message;

    acornscsi_sbic_issuecmd(host, CMND_XFERINFO | CMND_SBT);

    acornscsi_sbic_wait(host, ASR_DBR, ASR_DBR, 1000, "for message byte");

    message = sbic_arm_read(host->scsi.io_port, SBIC_DATA);

    /* wait for MSGIN-XFER-PAUSED */
    acornscsi_sbic_wait(host, ASR_INT, ASR_INT, 1000, "for interrupt after message byte");

    sbic_arm_read(host->scsi.io_port, SBIC_SSR);

    return message;
}

/*
 * Function: void acornscsi_message(AS_Host *host)
 * Purpose : Read complete message from connected target & action message
 * Params  : host - host connected to target
 */
static
void acornscsi_message(AS_Host *host)
{
    unsigned char message[16];
    unsigned int msgidx = 0, msglen = 1;

    do {
	message[msgidx] = acornscsi_readmessagebyte(host);

	switch (msgidx) {
	case 0:
	    if (message[0] == EXTENDED_MESSAGE ||
		(message[0] >= 0x20 && message[0] <= 0x2f))
		msglen = 2;
	    break;

	case 1:
	    if (message[0] == EXTENDED_MESSAGE)
		msglen += message[msgidx];
	    break;
	}
	msgidx += 1;
	if (msgidx < msglen) {
	    acornscsi_sbic_issuecmd(host, CMND_NEGATEACK);

	    /* wait for next msg-in */
	    acornscsi_sbic_wait(host, ASR_INT, ASR_INT, 1000, "for interrupt after negate ack");
	    sbic_arm_read(host->scsi.io_port, SBIC_SSR);
	}
    } while (msgidx < msglen);

#if (DEBUG & DEBUG_MESSAGES)
    printk("scsi%d.%c: message in: ",
	    host->host->host_no, acornscsi_target(host));
    print_msg(message);
    printk("\n");
#endif

    if (host->scsi.phase == PHASE_RECONNECTED) {
	/*
	 * ANSI standard says: (Section SCSI-2 Rev. 10c Sect 5.6.17)
	 * 'Whenever a target reconnects to an initiator to continue
	 *  a tagged I/O process, the SIMPLE QUEUE TAG message shall
	 *  be sent immediately following the IDENTIFY message...'
	 */
	if (message[0] == SIMPLE_QUEUE_TAG)
	    host->scsi.reconnected.tag = message[1];
	if (acornscsi_reconnect_finish(host))
	    host->scsi.phase = PHASE_MSGIN;
    }

    switch (message[0]) {
    case ABORT:
    case ABORT_TAG:
    case COMMAND_COMPLETE:
	if (host->scsi.phase != PHASE_STATUSIN) {
	    printk(KERN_ERR "scsi%d.%c: command complete following non-status in phase?\n",
		    host->host->host_no, acornscsi_target(host));
	    acornscsi_dumplog(host, host->SCpnt->device->id);
	}
	host->scsi.phase = PHASE_DONE;
	host->scsi.SCp.Message = message[0];
	break;

    case SAVE_POINTERS:
	/*
	 * ANSI standard says: (Section SCSI-2 Rev. 10c Sect 5.6.20)
	 * 'The SAVE DATA POINTER message is sent from a target to
	 *  direct the initiator to copy the active data pointer to
	 *  the saved data pointer for the current I/O process.
	 */
	acornscsi_dma_cleanup(host);
	host->SCpnt->SCp = host->scsi.SCp;
	host->SCpnt->SCp.sent_command = 0;
	host->scsi.phase = PHASE_MSGIN;
	break;

    case RESTORE_POINTERS:
	/*
	 * ANSI standard says: (Section SCSI-2 Rev. 10c Sect 5.6.19)
	 * 'The RESTORE POINTERS message is sent from a target to
	 *  direct the initiator to copy the most recently saved
	 *  command, data, and status pointers for the I/O process
	 *  to the corresponding active pointers.  The command and
	 *  status pointers shall be restored to the beginning of
	 *  the present command and status areas.'
	 */
	acornscsi_dma_cleanup(host);
	host->scsi.SCp = host->SCpnt->SCp;
	host->scsi.phase = PHASE_MSGIN;
	break;

    case DISCONNECT:
	/*
	 * ANSI standard says: (Section SCSI-2 Rev. 10c Sect 6.4.2)
	 * 'On those occasions when an error or exception condition occurs
	 *  and the target elects to repeat the information transfer, the
	 *  target may repeat the transfer either issuing a RESTORE POINTERS
	 *  message or by disconnecting without issuing a SAVE POINTERS
	 *  message.  When reconnection is completed, the most recent
	 *  saved pointer values are restored.'
	 */
	acornscsi_dma_cleanup(host);
	host->scsi.phase = PHASE_DISCONNECT;
	break;

    case MESSAGE_REJECT:
#if 0 /* this isn't needed any more */
	/*
	 * If we were negociating sync transfer, we don't yet know if
	 * this REJECT is for the sync transfer or for the tagged queue/wide
	 * transfer.  Re-initiate sync transfer negociation now, and if
	 * we got a REJECT in response to SDTR, then it'll be set to DONE.
	 */
	if (host->device[host->SCpnt->device->id].sync_state == SYNC_SENT_REQUEST)
	    host->device[host->SCpnt->device->id].sync_state = SYNC_NEGOCIATE;
#endif

	/*
	 * If we have any messages waiting to go out, then assert ATN now
	 */
	if (msgqueue_msglength(&host->scsi.msgs))
	    acornscsi_sbic_issuecmd(host, CMND_ASSERTATN);

	switch (host->scsi.last_message) {
#ifdef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
	case HEAD_OF_QUEUE_TAG:
	case ORDERED_QUEUE_TAG:
	case SIMPLE_QUEUE_TAG:
	    /*
	     * ANSI standard says: (Section SCSI-2 Rev. 10c Sect 5.6.17)
	     *  If a target does not implement tagged queuing and a queue tag
	     *  message is received, it shall respond with a MESSAGE REJECT
	     *  message and accept the I/O process as if it were untagged.
	     */
	    printk(KERN_NOTICE "scsi%d.%c: disabling tagged queueing\n",
		    host->host->host_no, acornscsi_target(host));
	    host->SCpnt->device->simple_tags = 0;
	    set_bit(host->SCpnt->device->id * 8 + host->SCpnt->device->lun, host->busyluns);
	    break;
#endif
	case EXTENDED_MESSAGE | (EXTENDED_SDTR << 8):
	    /*
	     * Target can't handle synchronous transfers
	     */
	    printk(KERN_NOTICE "scsi%d.%c: Using asynchronous transfer\n",
		    host->host->host_no, acornscsi_target(host));
	    host->device[host->SCpnt->device->id].sync_xfer = SYNCHTRANSFER_2DBA;
	    host->device[host->SCpnt->device->id].sync_state = SYNC_ASYNCHRONOUS;
	    sbic_arm_write(host->scsi.io_port, SBIC_SYNCHTRANSFER, host->device[host->SCpnt->device->id].sync_xfer);
	    break;

	default:
	    break;
	}
	break;

    case QUEUE_FULL:
	/* TODO: target queue is full */
	break;

    case SIMPLE_QUEUE_TAG:
	/* tag queue reconnect... message[1] = queue tag.  Print something to indicate something happened! */
	printk("scsi%d.%c: reconnect queue tag %02X\n",
		host->host->host_no, acornscsi_target(host),
		message[1]);
	break;

    case EXTENDED_MESSAGE:
	switch (message[2]) {
#ifdef CONFIG_SCSI_ACORNSCSI_SYNC
	case EXTENDED_SDTR:
	    if (host->device[host->SCpnt->device->id].sync_state == SYNC_SENT_REQUEST) {
		/*
		 * We requested synchronous transfers.  This isn't quite right...
		 * We can only say if this succeeded if we proceed on to execute the
		 * command from this message.  If we get a MESSAGE PARITY ERROR,
		 * and the target retries fail, then we fallback to asynchronous mode
		 */
		host->device[host->SCpnt->device->id].sync_state = SYNC_COMPLETED;
		printk(KERN_NOTICE "scsi%d.%c: Using synchronous transfer, offset %d, %d ns\n",
			host->host->host_no, acornscsi_target(host),
			message[4], message[3] * 4);
		host->device[host->SCpnt->device->id].sync_xfer =
			calc_sync_xfer(message[3] * 4, message[4]);
	    } else {
		unsigned char period, length;
		/*
		 * Target requested synchronous transfers.  The agreement is only
		 * to be in operation AFTER the target leaves message out phase.
		 */
		acornscsi_sbic_issuecmd(host, CMND_ASSERTATN);
		period = max_t(unsigned int, message[3], sdtr_period / 4);
		length = min_t(unsigned int, message[4], sdtr_size);
		msgqueue_addmsg(&host->scsi.msgs, 5, EXTENDED_MESSAGE, 3,
				 EXTENDED_SDTR, period, length);
		host->device[host->SCpnt->device->id].sync_xfer =
			calc_sync_xfer(period * 4, length);
	    }
	    sbic_arm_write(host->scsi.io_port, SBIC_SYNCHTRANSFER, host->device[host->SCpnt->device->id].sync_xfer);
	    break;
#else
	    /* We do not accept synchronous transfers.  Respond with a
	     * MESSAGE_REJECT.
	     */
#endif

	case EXTENDED_WDTR:
	    /* The WD33C93A is only 8-bit.  We respond with a MESSAGE_REJECT
	     * to a wide data transfer request.
	     */
	default:
	    acornscsi_sbic_issuecmd(host, CMND_ASSERTATN);
	    msgqueue_flush(&host->scsi.msgs);
	    msgqueue_addmsg(&host->scsi.msgs, 1, MESSAGE_REJECT);
	    break;
	}
	break;

#ifdef CONFIG_SCSI_ACORNSCSI_LINK
    case LINKED_CMD_COMPLETE:
    case LINKED_FLG_CMD_COMPLETE:
	/*
	 * We don't support linked commands yet
	 */
	if (0) {
#if (DEBUG & DEBUG_LINK)
	    printk("scsi%d.%c: lun %d tag %d linked command complete\n",
		    host->host->host_no, acornscsi_target(host), host->SCpnt->tag);
#endif
	    /*
	     * A linked command should only terminate with one of these messages
	     * if there are more linked commands available.
	     */
	    if (!host->SCpnt->next_link) {
		printk(KERN_WARNING "scsi%d.%c: lun %d tag %d linked command complete, but no next_link\n",
			instance->host_no, acornscsi_target(host), host->SCpnt->tag);
		acornscsi_sbic_issuecmd(host, CMND_ASSERTATN);
		msgqueue_addmsg(&host->scsi.msgs, 1, ABORT);
	    } else {
		Scsi_Cmnd *SCpnt = host->SCpnt;

		acornscsi_dma_cleanup(host);

		host->SCpnt = host->SCpnt->next_link;
		host->SCpnt->tag = SCpnt->tag;
		SCpnt->result = DID_OK | host->scsi.SCp.Message << 8 | host->Scsi.SCp.Status;
		SCpnt->done(SCpnt);

		/* initialise host->SCpnt->SCp */
	    }
	    break;
	}
#endif

    default: /* reject message */
	printk(KERN_ERR "scsi%d.%c: unrecognised message %02X, rejecting\n",
		host->host->host_no, acornscsi_target(host),
		message[0]);
	acornscsi_sbic_issuecmd(host, CMND_ASSERTATN);
	msgqueue_flush(&host->scsi.msgs);
	msgqueue_addmsg(&host->scsi.msgs, 1, MESSAGE_REJECT);
	host->scsi.phase = PHASE_MSGIN;
	break;
    }
    acornscsi_sbic_issuecmd(host, CMND_NEGATEACK);
}

/*
 * Function: int acornscsi_buildmessages(AS_Host *host)
 * Purpose : build the connection messages for a host
 * Params  : host - host to add messages to
 */
static
void acornscsi_buildmessages(AS_Host *host)
{
#if 0
    /* does the device need resetting? */
    if (cmd_reset) {
	msgqueue_addmsg(&host->scsi.msgs, 1, BUS_DEVICE_RESET);
	return;
    }
#endif

    msgqueue_addmsg(&host->scsi.msgs, 1,
		     IDENTIFY(host->device[host->SCpnt->device->id].disconnect_ok,
			     host->SCpnt->device->lun));

#if 0
    /* does the device need the current command aborted */
    if (cmd_aborted) {
	acornscsi_abortcmd(host->SCpnt->tag);
	return;
    }
#endif

#ifdef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
    if (host->SCpnt->tag) {
	unsigned int tag_type;

	if (host->SCpnt->cmnd[0] == REQUEST_SENSE ||
	    host->SCpnt->cmnd[0] == TEST_UNIT_READY ||
	    host->SCpnt->cmnd[0] == INQUIRY)
	    tag_type = HEAD_OF_QUEUE_TAG;
	else
	    tag_type = SIMPLE_QUEUE_TAG;
	msgqueue_addmsg(&host->scsi.msgs, 2, tag_type, host->SCpnt->tag);
    }
#endif

#ifdef CONFIG_SCSI_ACORNSCSI_SYNC
    if (host->device[host->SCpnt->device->id].sync_state == SYNC_NEGOCIATE) {
	host->device[host->SCpnt->device->id].sync_state = SYNC_SENT_REQUEST;
	msgqueue_addmsg(&host->scsi.msgs, 5,
			 EXTENDED_MESSAGE, 3, EXTENDED_SDTR,
			 sdtr_period / 4, sdtr_size);
    }
#endif
}

/*
 * Function: int acornscsi_starttransfer(AS_Host *host)
 * Purpose : transfer data to/from connected target
 * Params  : host - host to which target is connected
 * Returns : 0 if failure
 */
static
int acornscsi_starttransfer(AS_Host *host)
{
    int residual;

    if (!host->scsi.SCp.ptr /*&& host->scsi.SCp.this_residual*/) {
	printk(KERN_ERR "scsi%d.%c: null buffer passed to acornscsi_starttransfer\n",
		host->host->host_no, acornscsi_target(host));
	return 0;
    }

    residual = host->SCpnt->request_bufflen - host->scsi.SCp.scsi_xferred;

    sbic_arm_write(host->scsi.io_port, SBIC_SYNCHTRANSFER, host->device[host->SCpnt->device->id].sync_xfer);
    sbic_arm_writenext(host->scsi.io_port, residual >> 16);
    sbic_arm_writenext(host->scsi.io_port, residual >> 8);
    sbic_arm_writenext(host->scsi.io_port, residual);
    acornscsi_sbic_issuecmd(host, CMND_XFERINFO);
    return 1;
}

/* =========================================================================================
 * Connection & Disconnection
 */
/*
 * Function : acornscsi_reconnect(AS_Host *host)
 * Purpose  : reconnect a previously disconnected command
 * Params   : host - host specific data
 * Remarks  : SCSI spec says:
 *		'The set of active pointers is restored from the set
 *		 of saved pointers upon reconnection of the I/O process'
 */
static
int acornscsi_reconnect(AS_Host *host)
{
    unsigned int target, lun, ok = 0;

    target = sbic_arm_read(host->scsi.io_port, SBIC_SOURCEID);

    if (!(target & 8))
	printk(KERN_ERR "scsi%d: invalid source id after reselection "
		"- device fault?\n",
		host->host->host_no);

    target &= 7;

    if (host->SCpnt && !host->scsi.disconnectable) {
	printk(KERN_ERR "scsi%d.%d: reconnected while command in "
		"progress to target %d?\n",
		host->host->host_no, target, host->SCpnt->device->id);
	host->SCpnt = NULL;
    }

    lun = sbic_arm_read(host->scsi.io_port, SBIC_DATA) & 7;

    host->scsi.reconnected.target = target;
    host->scsi.reconnected.lun = lun;
    host->scsi.reconnected.tag = 0;

    if (host->scsi.disconnectable && host->SCpnt &&
	host->SCpnt->device->id == target && host->SCpnt->device->lun == lun)
	ok = 1;

    if (!ok && queue_probetgtlun(&host->queues.disconnected, target, lun))
	ok = 1;

    ADD_STATUS(target, 0x81, host->scsi.phase, 0);

    if (ok) {
	host->scsi.phase = PHASE_RECONNECTED;
    } else {
	/* this doesn't seem to work */
	printk(KERN_ERR "scsi%d.%c: reselected with no command "
		"to reconnect with\n",
		host->host->host_no, '0' + target);
	acornscsi_dumplog(host, target);
	acornscsi_abortcmd(host, 0);
	if (host->SCpnt) {
	    queue_add_cmd_tail(&host->queues.disconnected, host->SCpnt);
	    host->SCpnt = NULL;
	}
    }
    acornscsi_sbic_issuecmd(host, CMND_NEGATEACK);
    return !ok;
}

/*
 * Function: int acornscsi_reconect_finish(AS_Host *host)
 * Purpose : finish reconnecting a command
 * Params  : host - host to complete
 * Returns : 0 if failed
 */
static
int acornscsi_reconnect_finish(AS_Host *host)
{
    if (host->scsi.disconnectable && host->SCpnt) {
	host->scsi.disconnectable = 0;
	if (host->SCpnt->device->id  == host->scsi.reconnected.target &&
	    host->SCpnt->device->lun == host->scsi.reconnected.lun &&
	    host->SCpnt->tag         == host->scsi.reconnected.tag) {
#if (DEBUG & (DEBUG_QUEUES|DEBUG_DISCON))
	    DBG(host->SCpnt, printk("scsi%d.%c: reconnected",
		    host->host->host_no, acornscsi_target(host)));
#endif
	} else {
	    queue_add_cmd_tail(&host->queues.disconnected, host->SCpnt);
#if (DEBUG & (DEBUG_QUEUES|DEBUG_DISCON))
	    DBG(host->SCpnt, printk("scsi%d.%c: had to move command "
		    "to disconnected queue\n",
		    host->host->host_no, acornscsi_target(host)));
#endif
	    host->SCpnt = NULL;
	}
    }
    if (!host->SCpnt) {
	host->SCpnt = queue_remove_tgtluntag(&host->queues.disconnected,
				host->scsi.reconnected.target,
				host->scsi.reconnected.lun,
				host->scsi.reconnected.tag);
#if (DEBUG & (DEBUG_QUEUES|DEBUG_DISCON))
	DBG(host->SCpnt, printk("scsi%d.%c: had to get command",
		host->host->host_no, acornscsi_target(host)));
#endif
    }

    if (!host->SCpnt)
	acornscsi_abortcmd(host, host->scsi.reconnected.tag);
    else {
	/*
	 * Restore data pointer from SAVED pointers.
	 */
	host->scsi.SCp = host->SCpnt->SCp;
#if (DEBUG & (DEBUG_QUEUES|DEBUG_DISCON))
	printk(", data pointers: [%p, %X]",
		host->scsi.SCp.ptr, host->scsi.SCp.this_residual);
#endif
    }
#if (DEBUG & (DEBUG_QUEUES|DEBUG_DISCON))
    printk("\n");
#endif

    host->dma.transferred = host->scsi.SCp.scsi_xferred;

    return host->SCpnt != NULL;
}

/*
 * Function: void acornscsi_disconnect_unexpected(AS_Host *host)
 * Purpose : handle an unexpected disconnect
 * Params  : host - host on which disconnect occurred
 */
static
void acornscsi_disconnect_unexpected(AS_Host *host)
{
    printk(KERN_ERR "scsi%d.%c: unexpected disconnect\n",
	    host->host->host_no, acornscsi_target(host));
#if (DEBUG & DEBUG_ABORT)
    acornscsi_dumplog(host, 8);
#endif

    acornscsi_done(host, &host->SCpnt, DID_ERROR);
}

/*
 * Function: void acornscsi_abortcmd(AS_host *host, unsigned char tag)
 * Purpose : abort a currently executing command
 * Params  : host - host with connected command to abort
 *	     tag  - tag to abort
 */
static
void acornscsi_abortcmd(AS_Host *host, unsigned char tag)
{
    host->scsi.phase = PHASE_ABORTED;
    sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_ASSERTATN);

    msgqueue_flush(&host->scsi.msgs);
#ifdef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
    if (tag)
	msgqueue_addmsg(&host->scsi.msgs, 2, ABORT_TAG, tag);
    else
#endif
	msgqueue_addmsg(&host->scsi.msgs, 1, ABORT);
}

/* ==========================================================================================
 * Interrupt routines.
 */
/*
 * Function: int acornscsi_sbicintr(AS_Host *host)
 * Purpose : handle interrupts from SCSI device
 * Params  : host - host to process
 * Returns : INTR_PROCESS if expecting another SBIC interrupt
 *	     INTR_IDLE if no interrupt
 *	     INTR_NEXT_COMMAND if we have finished processing the command
 */
static
intr_ret_t acornscsi_sbicintr(AS_Host *host, int in_irq)
{
    unsigned int asr, ssr;

    asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR);
    if (!(asr & ASR_INT))
	return INTR_IDLE;

    ssr = sbic_arm_read(host->scsi.io_port, SBIC_SSR);

#if (DEBUG & DEBUG_PHASES)
    print_sbic_status(asr, ssr, host->scsi.phase);
#endif

    ADD_STATUS(8, ssr, host->scsi.phase, in_irq);

    if (host->SCpnt && !host->scsi.disconnectable)
	ADD_STATUS(host->SCpnt->device->id, ssr, host->scsi.phase, in_irq);

    switch (ssr) {
    case 0x00:				/* reset state - not advanced			*/
	printk(KERN_ERR "scsi%d: reset in standard mode but wanted advanced mode.\n",
		host->host->host_no);
	/* setup sbic - WD33C93A */
	sbic_arm_write(host->scsi.io_port, SBIC_OWNID, OWNID_EAF | host->host->this_id);
	sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_RESET);
	return INTR_IDLE;

    case 0x01:				/* reset state - advanced			*/
	sbic_arm_write(host->scsi.io_port, SBIC_CTRL, INIT_SBICDMA | CTRL_IDI);
	sbic_arm_write(host->scsi.io_port, SBIC_TIMEOUT, TIMEOUT_TIME);
	sbic_arm_write(host->scsi.io_port, SBIC_SYNCHTRANSFER, SYNCHTRANSFER_2DBA);
	sbic_arm_write(host->scsi.io_port, SBIC_SOURCEID, SOURCEID_ER | SOURCEID_DSP);
	msgqueue_flush(&host->scsi.msgs);
	return INTR_IDLE;

    case 0x41:				/* unexpected disconnect aborted command	*/
	acornscsi_disconnect_unexpected(host);
	return INTR_NEXT_COMMAND;
    }

    switch (host->scsi.phase) {
    case PHASE_CONNECTING:		/* STATE: command removed from issue queue	*/
	switch (ssr) {
	case 0x11:			/* -> PHASE_CONNECTED				*/
	    /* BUS FREE -> SELECTION */
	    host->scsi.phase = PHASE_CONNECTED;
	    msgqueue_flush(&host->scsi.msgs);
	    host->dma.transferred = host->scsi.SCp.scsi_xferred;
	    /* 33C93 gives next interrupt indicating bus phase */
	    asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR);
	    if (!(asr & ASR_INT))
		break;
	    ssr = sbic_arm_read(host->scsi.io_port, SBIC_SSR);
	    ADD_STATUS(8, ssr, host->scsi.phase, 1);
	    ADD_STATUS(host->SCpnt->device->id, ssr, host->scsi.phase, 1);
	    goto connected;
	    
	case 0x42:			/* select timed out				*/
					/* -> PHASE_IDLE				*/
	    acornscsi_done(host, &host->SCpnt, DID_NO_CONNECT);
	    return INTR_NEXT_COMMAND;

	case 0x81:			/* -> PHASE_RECONNECTED or PHASE_ABORTED	*/
	    /* BUS FREE -> RESELECTION */
	    host->origSCpnt = host->SCpnt;
	    host->SCpnt = NULL;
	    msgqueue_flush(&host->scsi.msgs);
	    acornscsi_reconnect(host);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_CONNECTING, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	    acornscsi_abortcmd(host, host->SCpnt->tag);
	}
	return INTR_PROCESSING;

    connected:
    case PHASE_CONNECTED:		/* STATE: device selected ok			*/
	switch (ssr) {
#ifdef NONSTANDARD
	case 0x8a:			/* -> PHASE_COMMAND, PHASE_COMMANDPAUSED	*/
	    /* SELECTION -> COMMAND */
	    acornscsi_sendcommand(host);
	    break;

	case 0x8b:			/* -> PHASE_STATUS				*/
	    /* SELECTION -> STATUS */
	    acornscsi_readstatusbyte(host);
	    host->scsi.phase = PHASE_STATUSIN;
	    break;
#endif

	case 0x8e:			/* -> PHASE_MSGOUT				*/
	    /* SELECTION ->MESSAGE OUT */
	    host->scsi.phase = PHASE_MSGOUT;
	    acornscsi_buildmessages(host);
	    acornscsi_sendmessage(host);
	    break;

	/* these should not happen */
	case 0x85:			/* target disconnected				*/
	    acornscsi_done(host, &host->SCpnt, DID_ERROR);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_CONNECTED, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	    acornscsi_abortcmd(host, host->SCpnt->tag);
	}
	return INTR_PROCESSING;

    case PHASE_MSGOUT:			/* STATE: connected & sent IDENTIFY message	*/
	/*
	 * SCSI standard says that MESSAGE OUT phases can be followed by a
	 * DATA phase, STATUS phase, MESSAGE IN phase or COMMAND phase
	 */
	switch (ssr) {
	case 0x8a:			/* -> PHASE_COMMAND, PHASE_COMMANDPAUSED	*/
	case 0x1a:			/* -> PHASE_COMMAND, PHASE_COMMANDPAUSED	*/
	    /* MESSAGE OUT -> COMMAND */
	    acornscsi_sendcommand(host);
	    break;

	case 0x8b:			/* -> PHASE_STATUS				*/
	case 0x1b:			/* -> PHASE_STATUS				*/
	    /* MESSAGE OUT -> STATUS */
	    acornscsi_readstatusbyte(host);
	    host->scsi.phase = PHASE_STATUSIN;
	    break;

	case 0x8e:			/* -> PHASE_MSGOUT				*/
	    /* MESSAGE_OUT(MESSAGE_IN) ->MESSAGE OUT */
	    acornscsi_sendmessage(host);
	    break;

	case 0x4f:			/* -> PHASE_MSGIN, PHASE_DISCONNECT		*/
	case 0x1f:			/* -> PHASE_MSGIN, PHASE_DISCONNECT		*/
	    /* MESSAGE OUT -> MESSAGE IN */
	    acornscsi_message(host);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_MSGOUT, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_COMMAND: 		/* STATE: connected & command sent		*/
	switch (ssr) {
	case 0x18:			/* -> PHASE_DATAOUT				*/
	    /* COMMAND -> DATA OUT */
	    if (host->scsi.SCp.sent_command != host->SCpnt->cmd_len)
		acornscsi_abortcmd(host, host->SCpnt->tag);
	    acornscsi_dma_setup(host, DMA_OUT);
	    if (!acornscsi_starttransfer(host))
		acornscsi_abortcmd(host, host->SCpnt->tag);
	    host->scsi.phase = PHASE_DATAOUT;
	    return INTR_IDLE;

	case 0x19:			/* -> PHASE_DATAIN				*/
	    /* COMMAND -> DATA IN */
	    if (host->scsi.SCp.sent_command != host->SCpnt->cmd_len)
		acornscsi_abortcmd(host, host->SCpnt->tag);
	    acornscsi_dma_setup(host, DMA_IN);
	    if (!acornscsi_starttransfer(host))
		acornscsi_abortcmd(host, host->SCpnt->tag);
	    host->scsi.phase = PHASE_DATAIN;
	    return INTR_IDLE;

	case 0x1b:			/* -> PHASE_STATUS				*/
	    /* COMMAND -> STATUS */
	    acornscsi_readstatusbyte(host);
	    host->scsi.phase = PHASE_STATUSIN;
	    break;

	case 0x1e:			/* -> PHASE_MSGOUT				*/
	    /* COMMAND -> MESSAGE OUT */
	    acornscsi_sendmessage(host);
	    break;

	case 0x1f:			/* -> PHASE_MSGIN, PHASE_DISCONNECT		*/
	    /* COMMAND -> MESSAGE IN */
	    acornscsi_message(host);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_COMMAND, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_DISCONNECT:		/* STATE: connected, received DISCONNECT msg	*/
	if (ssr == 0x85) {		/* -> PHASE_IDLE				*/
	    host->scsi.disconnectable = 1;
	    host->scsi.reconnected.tag = 0;
	    host->scsi.phase = PHASE_IDLE;
	    host->stats.disconnects += 1;
	} else {
	    printk(KERN_ERR "scsi%d.%c: PHASE_DISCONNECT, SSR %02X instead of disconnect?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_NEXT_COMMAND;

    case PHASE_IDLE:			/* STATE: disconnected				*/
	if (ssr == 0x81)		/* -> PHASE_RECONNECTED or PHASE_ABORTED	*/
	    acornscsi_reconnect(host);
	else {
	    printk(KERN_ERR "scsi%d.%c: PHASE_IDLE, SSR %02X while idle?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_RECONNECTED:		/* STATE: device reconnected to initiator	*/
	/*
	 * Command reconnected - if MESGIN, get message - it may be
	 * the tag.  If not, get command out of disconnected queue
	 */
	/*
	 * If we reconnected and we're not in MESSAGE IN phase after IDENTIFY,
	 * reconnect I_T_L command
	 */
	if (ssr != 0x8f && !acornscsi_reconnect_finish(host))
	    return INTR_IDLE;
	ADD_STATUS(host->SCpnt->device->id, ssr, host->scsi.phase, in_irq);
	switch (ssr) {
	case 0x88:			/* data out phase				*/
					/* -> PHASE_DATAOUT				*/
	    /* MESSAGE IN -> DATA OUT */
	    acornscsi_dma_setup(host, DMA_OUT);
	    if (!acornscsi_starttransfer(host))
		acornscsi_abortcmd(host, host->SCpnt->tag);
	    host->scsi.phase = PHASE_DATAOUT;
	    return INTR_IDLE;

	case 0x89:			/* data in phase				*/
					/* -> PHASE_DATAIN				*/
	    /* MESSAGE IN -> DATA IN */
	    acornscsi_dma_setup(host, DMA_IN);
	    if (!acornscsi_starttransfer(host))
		acornscsi_abortcmd(host, host->SCpnt->tag);
	    host->scsi.phase = PHASE_DATAIN;
	    return INTR_IDLE;

	case 0x8a:			/* command out					*/
	    /* MESSAGE IN -> COMMAND */
	    acornscsi_sendcommand(host);/* -> PHASE_COMMAND, PHASE_COMMANDPAUSED	*/
	    break;

	case 0x8b:			/* status in					*/
					/* -> PHASE_STATUSIN				*/
	    /* MESSAGE IN -> STATUS */
	    acornscsi_readstatusbyte(host);
	    host->scsi.phase = PHASE_STATUSIN;
	    break;

	case 0x8e:			/* message out					*/
					/* -> PHASE_MSGOUT				*/
	    /* MESSAGE IN -> MESSAGE OUT */
	    acornscsi_sendmessage(host);
	    break;

	case 0x8f:			/* message in					*/
	    acornscsi_message(host);	/* -> PHASE_MSGIN, PHASE_DISCONNECT		*/
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_RECONNECTED, SSR %02X after reconnect?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_DATAIN:			/* STATE: transferred data in			*/
	/*
	 * This is simple - if we disconnect then the DMA address & count is
	 * correct.
	 */
	switch (ssr) {
	case 0x19:			/* -> PHASE_DATAIN				*/
	case 0x89:			/* -> PHASE_DATAIN				*/
	    acornscsi_abortcmd(host, host->SCpnt->tag);
	    return INTR_IDLE;

	case 0x1b:			/* -> PHASE_STATUSIN				*/
	case 0x4b:			/* -> PHASE_STATUSIN				*/
	case 0x8b:			/* -> PHASE_STATUSIN				*/
	    /* DATA IN -> STATUS */
	    host->scsi.SCp.scsi_xferred = host->SCpnt->request_bufflen -
					  acornscsi_sbic_xfcount(host);
	    acornscsi_dma_stop(host);
	    acornscsi_readstatusbyte(host);
	    host->scsi.phase = PHASE_STATUSIN;
	    break;

	case 0x1e:			/* -> PHASE_MSGOUT				*/
	case 0x4e:			/* -> PHASE_MSGOUT				*/
	case 0x8e:			/* -> PHASE_MSGOUT				*/
	    /* DATA IN -> MESSAGE OUT */
	    host->scsi.SCp.scsi_xferred = host->SCpnt->request_bufflen -
					  acornscsi_sbic_xfcount(host);
	    acornscsi_dma_stop(host);
	    acornscsi_sendmessage(host);
	    break;

	case 0x1f:			/* message in					*/
	case 0x4f:			/* message in					*/
	case 0x8f:			/* message in					*/
	    /* DATA IN -> MESSAGE IN */
	    host->scsi.SCp.scsi_xferred = host->SCpnt->request_bufflen -
					  acornscsi_sbic_xfcount(host);
	    acornscsi_dma_stop(host);
	    acornscsi_message(host);	/* -> PHASE_MSGIN, PHASE_DISCONNECT		*/
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_DATAIN, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_DATAOUT: 		/* STATE: transferred data out			*/
	/*
	 * This is more complicated - if we disconnect, the DMA could be 12
	 * bytes ahead of us.  We need to correct this.
	 */
	switch (ssr) {
	case 0x18:			/* -> PHASE_DATAOUT				*/
	case 0x88:			/* -> PHASE_DATAOUT				*/
	    acornscsi_abortcmd(host, host->SCpnt->tag);
	    return INTR_IDLE;

	case 0x1b:			/* -> PHASE_STATUSIN				*/
	case 0x4b:			/* -> PHASE_STATUSIN				*/
	case 0x8b:			/* -> PHASE_STATUSIN				*/
	    /* DATA OUT -> STATUS */
	    host->scsi.SCp.scsi_xferred = host->SCpnt->request_bufflen -
					  acornscsi_sbic_xfcount(host);
	    acornscsi_dma_stop(host);
	    acornscsi_dma_adjust(host);
	    acornscsi_readstatusbyte(host);
	    host->scsi.phase = PHASE_STATUSIN;
	    break;

	case 0x1e:			/* -> PHASE_MSGOUT				*/
	case 0x4e:			/* -> PHASE_MSGOUT				*/
	case 0x8e:			/* -> PHASE_MSGOUT				*/
	    /* DATA OUT -> MESSAGE OUT */
	    host->scsi.SCp.scsi_xferred = host->SCpnt->request_bufflen -
					  acornscsi_sbic_xfcount(host);
	    acornscsi_dma_stop(host);
	    acornscsi_dma_adjust(host);
	    acornscsi_sendmessage(host);
	    break;

	case 0x1f:			/* message in					*/
	case 0x4f:			/* message in					*/
	case 0x8f:			/* message in					*/
	    /* DATA OUT -> MESSAGE IN */
	    host->scsi.SCp.scsi_xferred = host->SCpnt->request_bufflen -
					  acornscsi_sbic_xfcount(host);
	    acornscsi_dma_stop(host);
	    acornscsi_dma_adjust(host);
	    acornscsi_message(host);	/* -> PHASE_MSGIN, PHASE_DISCONNECT		*/
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_DATAOUT, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_STATUSIN:		/* STATE: status in complete			*/
	switch (ssr) {
	case 0x1f:			/* -> PHASE_MSGIN, PHASE_DONE, PHASE_DISCONNECT */
	case 0x8f:			/* -> PHASE_MSGIN, PHASE_DONE, PHASE_DISCONNECT */
	    /* STATUS -> MESSAGE IN */
	    acornscsi_message(host);
	    break;

	case 0x1e:			/* -> PHASE_MSGOUT				*/
	case 0x8e:			/* -> PHASE_MSGOUT				*/
	    /* STATUS -> MESSAGE OUT */
	    acornscsi_sendmessage(host);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_STATUSIN, SSR %02X instead of MESSAGE_IN?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_MSGIN:			/* STATE: message in				*/
	switch (ssr) {
	case 0x1e:			/* -> PHASE_MSGOUT				*/
	case 0x4e:			/* -> PHASE_MSGOUT				*/
	case 0x8e:			/* -> PHASE_MSGOUT				*/
	    /* MESSAGE IN -> MESSAGE OUT */
	    acornscsi_sendmessage(host);
	    break;

	case 0x1f:			/* -> PHASE_MSGIN, PHASE_DONE, PHASE_DISCONNECT */
	case 0x2f:
	case 0x4f:
	case 0x8f:
	    acornscsi_message(host);
	    break;

	case 0x85:
	    printk("scsi%d.%c: strange message in disconnection\n",
		host->host->host_no, acornscsi_target(host));
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	    acornscsi_done(host, &host->SCpnt, DID_ERROR);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_MSGIN, SSR %02X after message in?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_DONE:			/* STATE: received status & message		*/
	switch (ssr) {
	case 0x85:			/* -> PHASE_IDLE				*/
	    acornscsi_done(host, &host->SCpnt, DID_OK);
	    return INTR_NEXT_COMMAND;

	case 0x1e:
	case 0x8e:
	    acornscsi_sendmessage(host);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_DONE, SSR %02X instead of disconnect?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    case PHASE_ABORTED:
	switch (ssr) {
	case 0x85:
	    if (host->SCpnt)
		acornscsi_done(host, &host->SCpnt, DID_ABORT);
	    else {
		clear_bit(host->scsi.reconnected.target * 8 + host->scsi.reconnected.lun,
			  host->busyluns);
		host->scsi.phase = PHASE_IDLE;
	    }
	    return INTR_NEXT_COMMAND;

	case 0x1e:
	case 0x2e:
	case 0x4e:
	case 0x8e:
	    acornscsi_sendmessage(host);
	    break;

	default:
	    printk(KERN_ERR "scsi%d.%c: PHASE_ABORTED, SSR %02X?\n",
		    host->host->host_no, acornscsi_target(host), ssr);
	    acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
	}
	return INTR_PROCESSING;

    default:
	printk(KERN_ERR "scsi%d.%c: unknown driver phase %d\n",
		host->host->host_no, acornscsi_target(host), ssr);
	acornscsi_dumplog(host, host->SCpnt ? host->SCpnt->device->id : 8);
    }
    return INTR_PROCESSING;
}

/*
 * Prototype: void acornscsi_intr(int irq, void *dev_id, struct pt_regs *regs)
 * Purpose  : handle interrupts from Acorn SCSI card
 * Params   : irq    - interrupt number
 *	      dev_id - device specific data (AS_Host structure)
 *	      regs   - processor registers when interrupt occurred
 */
static irqreturn_t
acornscsi_intr(int irq, void *dev_id, struct pt_regs *regs)
{
    AS_Host *host = (AS_Host *)dev_id;
    intr_ret_t ret;
    int iostatus;
    int in_irq = 0;

    do {
	ret = INTR_IDLE;

	iostatus = inb(host->card.io_intr);

	if (iostatus & 2) {
	    acornscsi_dma_intr(host);
	    iostatus = inb(host->card.io_intr);
	}

	if (iostatus & 8)
	    ret = acornscsi_sbicintr(host, in_irq);

	/*
	 * If we have a transfer pending, start it.
	 * Only start it if the interface has already started transferring
	 * it's data
	 */
	if (host->dma.xfer_required)
	    acornscsi_dma_xfer(host);

	if (ret == INTR_NEXT_COMMAND)
	    ret = acornscsi_kick(host);

	in_irq = 1;
    } while (ret != INTR_IDLE);

    return IRQ_HANDLED;
}

/*=============================================================================================
 * Interfaces between interrupt handler and rest of scsi code
 */

/*
 * Function : acornscsi_queuecmd(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
 * Purpose  : queues a SCSI command
 * Params   : cmd  - SCSI command
 *	      done - function called on completion, with pointer to command descriptor
 * Returns  : 0, or < 0 on error.
 */
int acornscsi_queuecmd(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
    AS_Host *host = (AS_Host *)SCpnt->device->host->hostdata;

    if (!done) {
	/* there should be some way of rejecting errors like this without panicing... */
	panic("scsi%d: queuecommand called with NULL done function [cmd=%p]",
		host->host->host_no, SCpnt);
	return -EINVAL;
    }

#if (DEBUG & DEBUG_NO_WRITE)
    if (acornscsi_cmdtype(SCpnt->cmnd[0]) == CMD_WRITE && (NO_WRITE & (1 << SCpnt->device->id))) {
	printk(KERN_CRIT "scsi%d.%c: WRITE attempted with NO_WRITE flag set\n",
	    host->host->host_no, '0' + SCpnt->device->id);
	SCpnt->result = DID_NO_CONNECT << 16;
	done(SCpnt);
	return 0;
    }
#endif

    SCpnt->scsi_done = done;
    SCpnt->host_scribble = NULL;
    SCpnt->result = 0;
    SCpnt->tag = 0;
    SCpnt->SCp.phase = (int)acornscsi_datadirection(SCpnt->cmnd[0]);
    SCpnt->SCp.sent_command = 0;
    SCpnt->SCp.scsi_xferred = 0;

    init_SCp(SCpnt);

    host->stats.queues += 1;

    {
	unsigned long flags;

	if (!queue_add_cmd_ordered(&host->queues.issue, SCpnt)) {
	    SCpnt->result = DID_ERROR << 16;
	    done(SCpnt);
	    return 0;
	}
	local_irq_save(flags);
	if (host->scsi.phase == PHASE_IDLE)
	    acornscsi_kick(host);
	local_irq_restore(flags);
    }
    return 0;
}

/*
 * Prototype: void acornscsi_reportstatus(Scsi_Cmnd **SCpntp1, Scsi_Cmnd **SCpntp2, int result)
 * Purpose  : pass a result to *SCpntp1, and check if *SCpntp1 = *SCpntp2
 * Params   : SCpntp1 - pointer to command to return
 *	      SCpntp2 - pointer to command to check
 *	      result  - result to pass back to mid-level done function
 * Returns  : *SCpntp2 = NULL if *SCpntp1 is the same command structure as *SCpntp2.
 */
static inline
void acornscsi_reportstatus(Scsi_Cmnd **SCpntp1, Scsi_Cmnd **SCpntp2, int result)
{
    Scsi_Cmnd *SCpnt = *SCpntp1;

    if (SCpnt) {
	*SCpntp1 = NULL;

	SCpnt->result = result;
	SCpnt->scsi_done(SCpnt);
    }

    if (SCpnt == *SCpntp2)
	*SCpntp2 = NULL;
}

enum res_abort { res_not_running, res_success, res_success_clear, res_snooze };

/*
 * Prototype: enum res acornscsi_do_abort(Scsi_Cmnd *SCpnt)
 * Purpose  : abort a command on this host
 * Params   : SCpnt - command to abort
 * Returns  : our abort status
 */
static enum res_abort
acornscsi_do_abort(AS_Host *host, Scsi_Cmnd *SCpnt)
{
	enum res_abort res = res_not_running;

	if (queue_remove_cmd(&host->queues.issue, SCpnt)) {
		/*
		 * The command was on the issue queue, and has not been
		 * issued yet.  We can remove the command from the queue,
		 * and acknowledge the abort.  Neither the devices nor the
		 * interface know about the command.
		 */
//#if (DEBUG & DEBUG_ABORT)
		printk("on issue queue ");
//#endif
		res = res_success;
	} else if (queue_remove_cmd(&host->queues.disconnected, SCpnt)) {
		/*
		 * The command was on the disconnected queue.  Simply
		 * acknowledge the abort condition, and when the target
		 * reconnects, we will give it an ABORT message.  The
		 * target should then disconnect, and we will clear
		 * the busylun bit.
		 */
//#if (DEBUG & DEBUG_ABORT)
		printk("on disconnected queue ");
//#endif
		res = res_success;
	} else if (host->SCpnt == SCpnt) {
		unsigned long flags;

//#if (DEBUG & DEBUG_ABORT)
		printk("executing ");
//#endif

		local_irq_save(flags);
		switch (host->scsi.phase) {
		/*
		 * If the interface is idle, and the command is 'disconnectable',
		 * then it is the same as on the disconnected queue.  We simply
		 * remove all traces of the command.  When the target reconnects,
		 * we will give it an ABORT message since the command could not
		 * be found.  When the target finally disconnects, we will clear
		 * the busylun bit.
		 */
		case PHASE_IDLE:
			if (host->scsi.disconnectable) {
				host->scsi.disconnectable = 0;
				host->SCpnt = NULL;
				res = res_success;
			}
			break;

		/*
		 * If the command has connected and done nothing further,
		 * simply force a disconnect.  We also need to clear the
		 * busylun bit.
		 */
		case PHASE_CONNECTED:
			sbic_arm_write(host->scsi.io_port, SBIC_CMND, CMND_DISCONNECT);
			host->SCpnt = NULL;
			res = res_success_clear;
			break;

		default:
			acornscsi_abortcmd(host, host->SCpnt->tag);
			res = res_snooze;
		}
		local_irq_restore(flags);
	} else if (host->origSCpnt == SCpnt) {
		/*
		 * The command will be executed next, but a command
		 * is currently using the interface.  This is similar to
		 * being on the issue queue, except the busylun bit has
		 * been set.
		 */
		host->origSCpnt = NULL;
//#if (DEBUG & DEBUG_ABORT)
		printk("waiting for execution ");
//#endif
		res = res_success_clear;
	} else
		printk("unknown ");

	return res;
}

/*
 * Prototype: int acornscsi_abort(Scsi_Cmnd *SCpnt)
 * Purpose  : abort a command on this host
 * Params   : SCpnt - command to abort
 * Returns  : one of SCSI_ABORT_ macros
 */
int acornscsi_abort(Scsi_Cmnd *SCpnt)
{
	AS_Host *host = (AS_Host *) SCpnt->device->host->hostdata;
	int result;

	host->stats.aborts += 1;

#if (DEBUG & DEBUG_ABORT)
	{
		int asr, ssr;
		asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR);
		ssr = sbic_arm_read(host->scsi.io_port, SBIC_SSR);

		printk(KERN_WARNING "acornscsi_abort: ");
		print_sbic_status(asr, ssr, host->scsi.phase);
		acornscsi_dumplog(host, SCpnt->device->id);
	}
#endif

	printk("scsi%d: ", host->host->host_no);

	switch (acornscsi_do_abort(host, SCpnt)) {
	/*
	 * We managed to find the command and cleared it out.
	 * We do not expect the command to be executing on the
	 * target, but we have set the busylun bit.
	 */
	case res_success_clear:
//#if (DEBUG & DEBUG_ABORT)
		printk("clear ");
//#endif
		clear_bit(SCpnt->device->id * 8 + SCpnt->device->lun, host->busyluns);

	/*
	 * We found the command, and cleared it out.  Either
	 * the command is still known to be executing on the
	 * target, or the busylun bit is not set.
	 */
	case res_success:
//#if (DEBUG & DEBUG_ABORT)
		printk("success\n");
//#endif
		SCpnt->result = DID_ABORT << 16;
		SCpnt->scsi_done(SCpnt);
		result = SCSI_ABORT_SUCCESS;
		break;

	/*
	 * We did find the command, but unfortunately we couldn't
	 * unhook it from ourselves.  Wait some more, and if it
	 * still doesn't complete, reset the interface.
	 */
	case res_snooze:
//#if (DEBUG & DEBUG_ABORT)
		printk("snooze\n");
//#endif
		result = SCSI_ABORT_SNOOZE;
		break;

	/*
	 * The command could not be found (either because it completed,
	 * or it got dropped.
	 */
	default:
	case res_not_running:
		acornscsi_dumplog(host, SCpnt->device->id);
#if (DEBUG & DEBUG_ABORT)
		result = SCSI_ABORT_SNOOZE;
#else
		result = SCSI_ABORT_NOT_RUNNING;
#endif
//#if (DEBUG & DEBUG_ABORT)
		printk("not running\n");
//#endif
		break;
	}

	return result;
}

/*
 * Prototype: int acornscsi_reset(Scsi_Cmnd *SCpnt, unsigned int reset_flags)
 * Purpose  : reset a command on this host/reset this host
 * Params   : SCpnt  - command causing reset
 *	      result - what type of reset to perform
 * Returns  : one of SCSI_RESET_ macros
 */
int acornscsi_reset(Scsi_Cmnd *SCpnt, unsigned int reset_flags)
{
    AS_Host *host = (AS_Host *)SCpnt->device->host->hostdata;
    Scsi_Cmnd *SCptr;
    
    host->stats.resets += 1;

#if (DEBUG & DEBUG_RESET)
    {
	int asr, ssr;

	asr = sbic_arm_read(host->scsi.io_port, SBIC_ASR);
	ssr = sbic_arm_read(host->scsi.io_port, SBIC_SSR);

	printk(KERN_WARNING "acornscsi_reset: ");
	print_sbic_status(asr, ssr, host->scsi.phase);
	acornscsi_dumplog(host, SCpnt->device->id);
    }
#endif

    acornscsi_dma_stop(host);

    SCptr = host->SCpnt;

    /*
     * do hard reset.  This resets all devices on this host, and so we
     * must set the reset status on all commands.
     */
    acornscsi_resetcard(host);

    /*
     * report reset on commands current connected/disconnected
     */
    acornscsi_reportstatus(&host->SCpnt, &SCptr, DID_RESET);

    while ((SCptr = queue_remove(&host->queues.disconnected)) != NULL)
	acornscsi_reportstatus(&SCptr, &SCpnt, DID_RESET);

    if (SCpnt) {
	SCpnt->result = DID_RESET << 16;
	SCpnt->scsi_done(SCpnt);
    }

    return SCSI_RESET_BUS_RESET | SCSI_RESET_HOST_RESET | SCSI_RESET_SUCCESS;
}

/*==============================================================================================
 * initialisation & miscellaneous support
 */

/*
 * Function: char *acornscsi_info(struct Scsi_Host *host)
 * Purpose : return a string describing this interface
 * Params  : host - host to give information on
 * Returns : a constant string
 */
const
char *acornscsi_info(struct Scsi_Host *host)
{
    static char string[100], *p;

    p = string;
    
    p += sprintf(string, "%s at port %08lX irq %d v%d.%d.%d"
#ifdef CONFIG_SCSI_ACORNSCSI_SYNC
    " SYNC"
#endif
#ifdef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
    " TAG"
#endif
#ifdef CONFIG_SCSI_ACORNSCSI_LINK
    " LINK"
#endif
#if (DEBUG & DEBUG_NO_WRITE)
    " NOWRITE ("NO_WRITE_STR")"
#endif
		, host->hostt->name, host->io_port, host->irq,
		VER_MAJOR, VER_MINOR, VER_PATCH);
    return string;
}

int acornscsi_proc_info(struct Scsi_Host *instance, char *buffer, char **start, off_t offset,
			int length, int inout)
{
    int pos, begin = 0, devidx;
    Scsi_Device *scd;
    AS_Host *host;
    char *p = buffer;

    if (inout == 1)
	return -EINVAL;

    host  = (AS_Host *)instance->hostdata;
    
    p += sprintf(p, "AcornSCSI driver v%d.%d.%d"
#ifdef CONFIG_SCSI_ACORNSCSI_SYNC
    " SYNC"
#endif
#ifdef CONFIG_SCSI_ACORNSCSI_TAGGED_QUEUE
    " TAG"
#endif
#ifdef CONFIG_SCSI_ACORNSCSI_LINK
    " LINK"
#endif
#if (DEBUG & DEBUG_NO_WRITE)
    " NOWRITE ("NO_WRITE_STR")"
#endif
		"\n\n", VER_MAJOR, VER_MINOR, VER_PATCH);

    p += sprintf(p,	"SBIC: WD33C93A  Address: %08X  IRQ : %d\n",
			host->scsi.io_port, host->scsi.irq);
#ifdef USE_DMAC
    p += sprintf(p,	"DMAC: uPC71071  Address: %08X  IRQ : %d\n\n",
			host->dma.io_port, host->scsi.irq);
#endif

    p += sprintf(p,	"Statistics:\n"
			"Queued commands: %-10u    Issued commands: %-10u\n"
			"Done commands  : %-10u    Reads          : %-10u\n"
			"Writes         : %-10u    Others         : %-10u\n"
			"Disconnects    : %-10u    Aborts         : %-10u\n"
			"Resets         : %-10u\n\nLast phases:",
			host->stats.queues,		host->stats.removes,
			host->stats.fins,		host->stats.reads,
			host->stats.writes,		host->stats.miscs,
			host->stats.disconnects,	host->stats.aborts,
			host->stats.resets);

    for (devidx = 0; devidx < 9; devidx ++) {
	unsigned int statptr, prev;

	p += sprintf(p, "\n%c:", devidx == 8 ? 'H' : ('0' + devidx));
	statptr = host->status_ptr[devidx] - 10;

	if ((signed int)statptr < 0)
	    statptr += STATUS_BUFFER_SIZE;

	prev = host->status[devidx][statptr].when;

	for (; statptr != host->status_ptr[devidx]; statptr = (statptr + 1) & (STATUS_BUFFER_SIZE - 1)) {
	    if (host->status[devidx][statptr].when) {
		p += sprintf(p, "%c%02X:%02X+%2ld",
			host->status[devidx][statptr].irq ? '-' : ' ',
			host->status[devidx][statptr].ph,
			host->status[devidx][statptr].ssr,
			(host->status[devidx][statptr].when - prev) < 100 ?
				(host->status[devidx][statptr].when - prev) : 99);
		prev = host->status[devidx][statptr].when;
	    }
	}
    }

    p += sprintf(p, "\nAttached devices:\n");

    shost_for_each_device(scd, instance) {
	p += sprintf(p, "Device/Lun TaggedQ      Sync\n");
	p += sprintf(p, "     %d/%d   ", scd->id, scd->lun);
	if (scd->tagged_supported)
		p += sprintf(p, "%3sabled(%3d) ",
			     scd->simple_tags ? "en" : "dis",
			     scd->current_tag);
	else
		p += sprintf(p, "unsupported  ");

	if (host->device[scd->id].sync_xfer & 15)
		p += sprintf(p, "offset %d, %d ns\n",
			     host->device[scd->id].sync_xfer & 15,
			     acornscsi_getperiod(host->device[scd->id].sync_xfer));
	else
		p += sprintf(p, "async\n");

	pos = p - buffer;
	if (pos + begin < offset) {
	    begin += pos;
	    p = buffer;
	}
	pos = p - buffer;
	if (pos + begin > offset + length) {
	    scsi_device_put(scd);
	    break;
	}
    }

    pos = p - buffer;

    *start = buffer + (offset - begin);
    pos -= offset - begin;

    if (pos > length)
	pos = length;

    return pos;
}

static Scsi_Host_Template acornscsi_template = {
	.module			= THIS_MODULE,
	.proc_info		= acornscsi_proc_info,
	.name			= "AcornSCSI",
	.info			= acornscsi_info,
	.queuecommand		= acornscsi_queuecmd,
#warning fixme
	.abort			= acornscsi_abort,
	.reset			= acornscsi_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.unchecked_isa_dma	= 0,
	.use_clustering		= DISABLE_CLUSTERING,
	.proc_name		= "acornscsi",
};

static int __devinit
acornscsi_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct Scsi_Host *host;
	AS_Host *ashost;
	int ret = -ENOMEM;

	host = scsi_host_alloc(&acornscsi_template, sizeof(AS_Host));
	if (!host)
		goto out;

	ashost = (AS_Host *)host->hostdata;

	host->io_port = ecard_address(ec, ECARD_MEMC, 0);
	host->irq = ec->irq;

	ashost->host		= host;
	ashost->scsi.io_port	= ioaddr(host->io_port + 0x800);
	ashost->scsi.irq	= host->irq;
	ashost->card.io_intr	= POD_SPACE(host->io_port) + 0x800;
	ashost->card.io_page	= POD_SPACE(host->io_port) + 0xc00;
	ashost->card.io_ram	= ioaddr(host->io_port);
	ashost->dma.io_port	= host->io_port + 0xc00;
	ashost->dma.io_intr_clear = POD_SPACE(host->io_port) + 0x800;

	ec->irqaddr	= (char *)ioaddr(ashost->card.io_intr);
	ec->irqmask	= 0x0a;

	ret = -EBUSY;
	if (!request_region(host->io_port + 0x800, 2, "acornscsi(sbic)"))
		goto err_1;
	if (!request_region(ashost->card.io_intr, 1, "acornscsi(intr)"))
		goto err_2;
	if (!request_region(ashost->card.io_page, 1, "acornscsi(page)"))
		goto err_3;
#ifdef USE_DMAC
	if (!request_region(ashost->dma.io_port, 256, "acornscsi(dmac)"))
		goto err_4;
#endif
	if (!request_region(host->io_port, 2048, "acornscsi(ram)"))
		goto err_5;

	ret = request_irq(host->irq, acornscsi_intr, SA_INTERRUPT, "acornscsi", ashost);
	if (ret) {
		printk(KERN_CRIT "scsi%d: IRQ%d not free: %d\n",
			host->host_no, ashost->scsi.irq, ret);
		goto err_6;
	}

	memset(&ashost->stats, 0, sizeof (ashost->stats));
	queue_initialise(&ashost->queues.issue);
	queue_initialise(&ashost->queues.disconnected);
	msgqueue_initialise(&ashost->scsi.msgs);

	acornscsi_resetcard(ashost);

	ret = scsi_add_host(host, &ec->dev);
	if (ret)
		goto err_7;

	scsi_scan_host(host);
	goto out;

 err_7:
	free_irq(host->irq, ashost);
 err_6:
	release_region(host->io_port, 2048);
 err_5:
#ifdef USE_DMAC
	release_region(ashost->dma.io_port, 256);
#endif
 err_4:
	release_region(ashost->card.io_page, 1);
 err_3:
	release_region(ashost->card.io_intr, 1);    
 err_2:
	release_region(host->io_port + 0x800, 2);
 err_1:
	scsi_host_put(host);
 out:
	return ret;
}

static void __devexit acornscsi_remove(struct expansion_card *ec)
{
	struct Scsi_Host *host = ecard_get_drvdata(ec);
	AS_Host *ashost = (AS_Host *)host->hostdata;

	ecard_set_drvdata(ec, NULL);
	scsi_remove_host(host);

	/*
	 * Put card into RESET state
	 */
	outb(0x80, ashost->card.io_page);

	free_irq(host->irq, ashost);

	release_region(host->io_port + 0x800, 2);
	release_region(ashost->card.io_intr, 1);
	release_region(ashost->card.io_page, 1);
	release_region(ashost->dma.io_port, 256);
	release_region(host->io_port, 2048);

	msgqueue_free(&ashost->scsi.msgs);
	queue_free(&ashost->queues.disconnected);
	queue_free(&ashost->queues.issue);
	scsi_host_put(host);
}

static const struct ecard_id acornscsi_cids[] = {
	{ MANU_ACORN, PROD_ACORN_SCSI },
	{ 0xffff, 0xffff },
};

static struct ecard_driver acornscsi_driver = {
	.probe		= acornscsi_probe,
	.remove		= __devexit_p(acornscsi_remove),
	.id_table	= acornscsi_cids,
	.drv = {
		.name		= "acornscsi",
	},
};

static int __init acornscsi_init(void)
{
	return ecard_register_driver(&acornscsi_driver);
}

static void __exit acornscsi_exit(void)
{
	ecard_remove_driver(&acornscsi_driver);
}

module_init(acornscsi_init);
module_exit(acornscsi_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("AcornSCSI driver");
MODULE_LICENSE("GPL");
