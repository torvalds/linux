/**************************************************************************
 * Initio 9100 device driver for Linux.
 *
 * Copyright (c) 1994-1998 Initio Corporation
 * Copyright (c) 1998 Bas Vermeulen <bvermeul@blackstar.xs4all.nl>
 * Copyright (c) 2004 Christoph Hellwig <hch@lst.de>
 * Copyright (c) 2007 Red Hat
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
 *
 *************************************************************************
 *
 * DESCRIPTION:
 *
 * This is the Linux low-level SCSI driver for Initio INI-9X00U/UW SCSI host
 * adapters
 *
 * 08/06/97 hc	- v1.01h
 *		- Support inic-940 and inic-935
 * 09/26/97 hc	- v1.01i
 *		- Make correction from J.W. Schultz suggestion
 * 10/13/97 hc	- Support reset function
 * 10/21/97 hc	- v1.01j
 *		- Support 32 LUN (SCSI 3)
 * 01/14/98 hc	- v1.01k
 *		- Fix memory allocation problem
 * 03/04/98 hc	- v1.01l
 *		- Fix tape rewind which will hang the system problem
 *		- Set can_queue to initio_num_scb
 * 06/25/98 hc	- v1.01m
 *		- Get it work for kernel version >= 2.1.75
 *		- Dynamic assign SCSI bus reset holding time in initio_init()
 * 07/02/98 hc	- v1.01n
 *		- Support 0002134A
 * 08/07/98 hc  - v1.01o
 *		- Change the initio_abort_srb routine to use scsi_done. <01>
 * 09/07/98 hl  - v1.02
 *              - Change the INI9100U define and proc_dir_entry to
 *                reflect the newer Kernel 2.1.118, but the v1.o1o
 *                should work with Kernel 2.1.118.
 * 09/20/98 wh  - v1.02a
 *              - Support Abort command.
 *              - Handle reset routine.
 * 09/21/98 hl  - v1.03
 *              - remove comments.
 * 12/09/98 bv	- v1.03a
 *		- Removed unused code
 * 12/13/98 bv	- v1.03b
 *		- Remove cli() locking for kernels >= 2.1.95. This uses
 *		  spinlocks to serialize access to the pSRB_head and
 *		  pSRB_tail members of the HCS structure.
 * 09/01/99 bv	- v1.03d
 *		- Fixed a deadlock problem in SMP.
 * 21/01/99 bv	- v1.03e
 *		- Add support for the Domex 3192U PCI SCSI
 *		  This is a slightly modified patch by
 *		  Brian Macy <bmacy@sunshinecomputing.com>
 * 22/02/99 bv	- v1.03f
 *		- Didn't detect the INIC-950 in 2.0.x correctly.
 *		  Now fixed.
 * 05/07/99 bv	- v1.03g
 *		- Changed the assumption that HZ = 100
 * 10/17/03 mc	- v1.04
 *		- added new DMA API support
 * 06/01/04 jmd	- v1.04a
 *		- Re-add reset_bus support
 **************************************************************************/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include "initio.h"

#define SENSE_SIZE		14

#define i91u_MAXQUEUE		2
#define i91u_REVID "Initio INI-9X00U/UW SCSI device driver; Revision: 1.04a"

#define I950_DEVICE_ID	0x9500	/* Initio's inic-950 product ID   */
#define I940_DEVICE_ID	0x9400	/* Initio's inic-940 product ID   */
#define I935_DEVICE_ID	0x9401	/* Initio's inic-935 product ID   */
#define I920_DEVICE_ID	0x0002	/* Initio's other product ID      */

#ifdef DEBUG_i91u
static unsigned int i91u_debug = DEBUG_DEFAULT;
#endif

static int initio_tag_enable = 1;

#ifdef DEBUG_i91u
static int setup_debug = 0;
#endif

static void i91uSCBPost(u8 * pHcb, u8 * pScb);

/* PCI Devices supported by this driver */
static struct pci_device_id i91u_pci_devices[] = {
	{ PCI_VENDOR_ID_INIT,  I950_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_INIT,  I940_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_INIT,  I935_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_INIT,  I920_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_DOMEX, I920_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ }
};
MODULE_DEVICE_TABLE(pci, i91u_pci_devices);

#define DEBUG_INTERRUPT 0
#define DEBUG_QUEUE     0
#define DEBUG_STATE     0
#define INT_DISC	0

/*--- forward references ---*/
static struct scsi_ctrl_blk *initio_find_busy_scb(struct initio_host * host, u16 tarlun);
static struct scsi_ctrl_blk *initio_find_done_scb(struct initio_host * host);

static int tulip_main(struct initio_host * host);

static int initio_next_state(struct initio_host * host);
static int initio_state_1(struct initio_host * host);
static int initio_state_2(struct initio_host * host);
static int initio_state_3(struct initio_host * host);
static int initio_state_4(struct initio_host * host);
static int initio_state_5(struct initio_host * host);
static int initio_state_6(struct initio_host * host);
static int initio_state_7(struct initio_host * host);
static int initio_xfer_data_in(struct initio_host * host);
static int initio_xfer_data_out(struct initio_host * host);
static int initio_xpad_in(struct initio_host * host);
static int initio_xpad_out(struct initio_host * host);
static int initio_status_msg(struct initio_host * host);

static int initio_msgin(struct initio_host * host);
static int initio_msgin_sync(struct initio_host * host);
static int initio_msgin_accept(struct initio_host * host);
static int initio_msgout_reject(struct initio_host * host);
static int initio_msgin_extend(struct initio_host * host);

static int initio_msgout_ide(struct initio_host * host);
static int initio_msgout_abort_targ(struct initio_host * host);
static int initio_msgout_abort_tag(struct initio_host * host);

static int initio_bus_device_reset(struct initio_host * host);
static void initio_select_atn(struct initio_host * host, struct scsi_ctrl_blk * scb);
static void initio_select_atn3(struct initio_host * host, struct scsi_ctrl_blk * scb);
static void initio_select_atn_stop(struct initio_host * host, struct scsi_ctrl_blk * scb);
static int int_initio_busfree(struct initio_host * host);
static int int_initio_scsi_rst(struct initio_host * host);
static int int_initio_bad_seq(struct initio_host * host);
static int int_initio_resel(struct initio_host * host);
static int initio_sync_done(struct initio_host * host);
static int wdtr_done(struct initio_host * host);
static int wait_tulip(struct initio_host * host);
static int initio_wait_done_disc(struct initio_host * host);
static int initio_wait_disc(struct initio_host * host);
static void tulip_scsi(struct initio_host * host);
static int initio_post_scsi_rst(struct initio_host * host);

static void initio_se2_ew_en(unsigned long base);
static void initio_se2_ew_ds(unsigned long base);
static int initio_se2_rd_all(unsigned long base);
static void initio_se2_update_all(unsigned long base);	/* setup default pattern */
static void initio_read_eeprom(unsigned long base);

/* ---- INTERNAL VARIABLES ---- */

static NVRAM i91unvram;
static NVRAM *i91unvramp;

static u8 i91udftNvRam[64] =
{
	/*----------- header -----------*/
	0x25, 0xc9,		/* Signature    */
	0x40,			/* Size         */
	0x01,			/* Revision     */
	/* -- Host Adapter Structure -- */
	0x95,			/* ModelByte0   */
	0x00,			/* ModelByte1   */
	0x00,			/* ModelInfo    */
	0x01,			/* NumOfCh      */
	NBC1_DEFAULT,		/* BIOSConfig1  */
	0,			/* BIOSConfig2  */
	0,			/* HAConfig1    */
	0,			/* HAConfig2    */
	/* SCSI channel 0 and target Structure  */
	7,			/* SCSIid       */
	NCC1_DEFAULT,		/* SCSIconfig1  */
	0,			/* SCSIconfig2  */
	0x10,			/* NumSCSItarget */

	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,

	/* SCSI channel 1 and target Structure  */
	7,			/* SCSIid       */
	NCC1_DEFAULT,		/* SCSIconfig1  */
	0,			/* SCSIconfig2  */
	0x10,			/* NumSCSItarget */

	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT, NTC_DEFAULT,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0};			/*      - CheckSum -            */


static u8 initio_rate_tbl[8] =	/* fast 20      */
{
				/* nanosecond divide by 4 */
	12,			/* 50ns,  20M   */
	18,			/* 75ns,  13.3M */
	25,			/* 100ns, 10M   */
	31,			/* 125ns, 8M    */
	37,			/* 150ns, 6.6M  */
	43,			/* 175ns, 5.7M  */
	50,			/* 200ns, 5M    */
	62			/* 250ns, 4M    */
};

static void initio_do_pause(unsigned amount)
{
	/* Pause for amount jiffies */
	unsigned long the_time = jiffies + amount;

	while (time_before_eq(jiffies, the_time))
		cpu_relax();
}

/*-- forward reference --*/

/******************************************************************
 Input: instruction for  Serial E2PROM

 EX: se2_rd(0 call se2_instr() to send address and read command

	 StartBit  OP_Code   Address                Data
	 --------- --------  ------------------     -------
	 1         1 , 0     A5,A4,A3,A2,A1,A0      D15-D0

		 +-----------------------------------------------------
		 |
 CS -----+
			+--+  +--+  +--+  +--+  +--+
			^  |  ^  |  ^  |  ^  |  ^  |
			|  |  |  |  |  |  |  |  |  |
 CLK -------+  +--+  +--+  +--+  +--+  +--
 (leading edge trigger)

		 +--1-----1--+
		 | SB    OP  |  OP    A5    A4
 DI  ----+           +--0------------------
 (address and cmd sent to nvram)

	 -------------------------------------------+
												|
 DO                                             +---
 (data sent from nvram)


******************************************************************/

/**
 *	initio_se2_instr	-	bitbang an instruction
 *	@base: Base of InitIO controller
 *	@instr: Instruction for serial E2PROM
 *
 *	Bitbang an instruction out to the serial E2Prom
 */

static void initio_se2_instr(unsigned long base, u8 instr)
{
	int i;
	u8 b;

	outb(SE2CS | SE2DO, base + TUL_NVRAM);		/* cs+start bit */
	udelay(30);
	outb(SE2CS | SE2CLK | SE2DO, base + TUL_NVRAM);	/* +CLK */
	udelay(30);

	for (i = 0; i < 8; i++) {
		if (instr & 0x80)
			b = SE2CS | SE2DO;		/* -CLK+dataBit */
		else
			b = SE2CS;			/* -CLK */
		outb(b, base + TUL_NVRAM);
		udelay(30);
		outb(b | SE2CLK, base + TUL_NVRAM);	/* +CLK */
		udelay(30);
		instr <<= 1;
	}
	outb(SE2CS, base + TUL_NVRAM);			/* -CLK */
	udelay(30);
}


/**
 *	initio_se2_ew_en	-	Enable erase/write
 *	@base: Base address of InitIO controller
 *
 *	Enable erase/write state of serial EEPROM
 */
void initio_se2_ew_en(unsigned long base)
{
	initio_se2_instr(base, 0x30);	/* EWEN */
	outb(0, base + TUL_NVRAM);	/* -CS  */
	udelay(30);
}


/**
 *	initio_se2_ew_ds	-	Disable erase/write
 *	@base: Base address of InitIO controller
 *
 *	Disable erase/write state of serial EEPROM
 */
void initio_se2_ew_ds(unsigned long base)
{
	initio_se2_instr(base, 0);	/* EWDS */
	outb(0, base + TUL_NVRAM);	/* -CS  */
	udelay(30);
}


/**
 *	initio_se2_rd		-	read E2PROM word
 *	@base: Base of InitIO controller
 *	@addr: Address of word in E2PROM
 *
 *	Read a word from the NV E2PROM device
 */
static u16 initio_se2_rd(unsigned long base, u8 addr)
{
	u8 instr, rb;
	u16 val = 0;
	int i;

	instr = (u8) (addr | 0x80);
	initio_se2_instr(base, instr);	/* READ INSTR */

	for (i = 15; i >= 0; i--) {
		outb(SE2CS | SE2CLK, base + TUL_NVRAM);	/* +CLK */
		udelay(30);
		outb(SE2CS, base + TUL_NVRAM);		/* -CLK */

		/* sample data after the following edge of clock  */
		rb = inb(base + TUL_NVRAM);
		rb &= SE2DI;
		val += (rb << i);
		udelay(30);	/* 6/20/95 */
	}

	outb(0, base + TUL_NVRAM);		/* no chip select */
	udelay(30);
	return val;
}

/**
 *	initio_se2_wr		-	read E2PROM word
 *	@base: Base of InitIO controller
 *	@addr: Address of word in E2PROM
 *	@val: Value to write
 *
 *	Write a word to the NV E2PROM device. Used when recovering from
 *	a problem with the NV.
 */
static void initio_se2_wr(unsigned long base, u8 addr, u16 val)
{
	u8 rb;
	u8 instr;
	int i;

	instr = (u8) (addr | 0x40);
	initio_se2_instr(base, instr);	/* WRITE INSTR */
	for (i = 15; i >= 0; i--) {
		if (val & 0x8000)
			outb(SE2CS | SE2DO, base + TUL_NVRAM);	/* -CLK+dataBit 1 */
		else
			outb(SE2CS, base + TUL_NVRAM);		/* -CLK+dataBit 0 */
		udelay(30);
		outb(SE2CS | SE2CLK, base + TUL_NVRAM);		/* +CLK */
		udelay(30);
		val <<= 1;
	}
	outb(SE2CS, base + TUL_NVRAM);				/* -CLK */
	udelay(30);
	outb(0, base + TUL_NVRAM);				/* -CS  */
	udelay(30);

	outb(SE2CS, base + TUL_NVRAM);				/* +CS  */
	udelay(30);

	for (;;) {
		outb(SE2CS | SE2CLK, base + TUL_NVRAM);		/* +CLK */
		udelay(30);
		outb(SE2CS, base + TUL_NVRAM);			/* -CLK */
		udelay(30);
		if ((rb = inb(base + TUL_NVRAM)) & SE2DI)
			break;	/* write complete */
	}
	outb(0, base + TUL_NVRAM);				/* -CS */
}

/**
 *	initio_se2_rd_all	-	read hostadapter NV configuration
 *	@base: Base address of InitIO controller
 *
 *	Reads the E2PROM data into main memory. Ensures that the checksum
 *	and header marker are valid. Returns 1 on success -1 on error.
 */

static int initio_se2_rd_all(unsigned long base)
{
	int i;
	u16 chksum = 0;
	u16 *np;

	i91unvramp = &i91unvram;
	np = (u16 *) i91unvramp;
	for (i = 0; i < 32; i++)
		*np++ = initio_se2_rd(base, i);

	/* Is signature "ini" ok ? */
	if (i91unvramp->NVM_Signature != INI_SIGNATURE)
		return -1;
	/* Is ckecksum ok ? */
	np = (u16 *) i91unvramp;
	for (i = 0; i < 31; i++)
		chksum += *np++;
	if (i91unvramp->NVM_CheckSum != chksum)
		return -1;
	return 1;
}

/**
 *	initio_se2_update_all		-	Update E2PROM
 *	@base: Base of InitIO controller
 *
 *	Update the E2PROM by wrting any changes into the E2PROM
 *	chip, rewriting the checksum.
 */
static void initio_se2_update_all(unsigned long base)
{				/* setup default pattern */
	int i;
	u16 chksum = 0;
	u16 *np, *np1;

	i91unvramp = &i91unvram;
	/* Calculate checksum first */
	np = (u16 *) i91udftNvRam;
	for (i = 0; i < 31; i++)
		chksum += *np++;
	*np = chksum;
	initio_se2_ew_en(base);	/* Enable write  */

	np = (u16 *) i91udftNvRam;
	np1 = (u16 *) i91unvramp;
	for (i = 0; i < 32; i++, np++, np1++) {
		if (*np != *np1)
			initio_se2_wr(base, i, *np);
	}
	initio_se2_ew_ds(base);	/* Disable write   */
}

/**
 *	initio_read_eeprom		-	Retrieve configuration
 *	@base: Base of InitIO Host Adapter
 *
 *	Retrieve the host adapter configuration data from E2Prom. If the
 *	data is invalid then the defaults are used and are also restored
 *	into the E2PROM. This forms the access point for the SCSI driver
 *	into the E2PROM layer, the other functions for the E2PROM are all
 *	internal use.
 *
 *	Must be called single threaded, uses a shared global area.
 */

static void initio_read_eeprom(unsigned long base)
{
	u8 gctrl;

	i91unvramp = &i91unvram;
	/* Enable EEProm programming */
	gctrl = inb(base + TUL_GCTRL);
	outb(gctrl | TUL_GCTRL_EEPROM_BIT, base + TUL_GCTRL);
	if (initio_se2_rd_all(base) != 1) {
		initio_se2_update_all(base);	/* setup default pattern */
		initio_se2_rd_all(base);	/* load again  */
	}
	/* Disable EEProm programming */
	gctrl = inb(base + TUL_GCTRL);
	outb(gctrl & ~TUL_GCTRL_EEPROM_BIT, base + TUL_GCTRL);
}

/**
 *	initio_stop_bm		-	stop bus master
 *	@host: InitIO we are stopping
 *
 *	Stop any pending DMA operation, aborting the DMA if necessary
 */

static void initio_stop_bm(struct initio_host * host)
{

	if (inb(host->addr + TUL_XStatus) & XPEND) {	/* if DMA xfer is pending, abort DMA xfer */
		outb(TAX_X_ABT | TAX_X_CLR_FIFO, host->addr + TUL_XCmd);
		/* wait Abort DMA xfer done */
		while ((inb(host->addr + TUL_Int) & XABT) == 0)
			cpu_relax();
	}
	outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
}

/**
 *	initio_reset_scsi		-	Reset SCSI host controller
 *	@host: InitIO host to reset
 *	@seconds: Recovery time
 *
 *	Perform a full reset of the SCSI subsystem.
 */

static int initio_reset_scsi(struct initio_host * host, int seconds)
{
	outb(TSC_RST_BUS, host->addr + TUL_SCtrl0);

	while (!((host->jsint = inb(host->addr + TUL_SInt)) & TSS_SCSIRST_INT))
		cpu_relax();

	/* reset tulip chip */
	outb(0, host->addr + TUL_SSignal);

	/* Stall for a while, wait for target's firmware ready,make it 2 sec ! */
	/* SONY 5200 tape drive won't work if only stall for 1 sec */
	/* FIXME: this is a very long busy wait right now */
	initio_do_pause(seconds * HZ);

	inb(host->addr + TUL_SInt);
	return SCSI_RESET_SUCCESS;
}

/**
 *	initio_init		-	set up an InitIO host adapter
 *	@host: InitIO host adapter
 *	@num_scbs: Number of SCBS
 *	@bios_addr: BIOS address
 *
 *	Set up the host adapter and devices according to the configuration
 *	retrieved from the E2PROM.
 *
 *	Locking: Calls E2PROM layer code which is not re-enterable so must
 *	run single threaded for now.
 */

static void initio_init(struct initio_host * host, u8 *bios_addr)
{
	int i;
	u8 *flags;
	u8 *heads;

	/* Get E2Prom configuration */
	initio_read_eeprom(host->addr);
	if (i91unvramp->NVM_SCSIInfo[0].NVM_NumOfTarg == 8)
		host->max_tar = 8;
	else
		host->max_tar = 16;

	host->config = i91unvramp->NVM_SCSIInfo[0].NVM_ChConfig1;

	host->scsi_id = i91unvramp->NVM_SCSIInfo[0].NVM_ChSCSIID;
	host->idmask = ~(1 << host->scsi_id);

#ifdef CHK_PARITY
	/* Enable parity error response */
	outb(inb(host->addr + TUL_PCMD) | 0x40, host->addr + TUL_PCMD);
#endif

	/* Mask all the interrupt       */
	outb(0x1F, host->addr + TUL_Mask);

	initio_stop_bm(host);
	/* --- Initialize the tulip --- */
	outb(TSC_RST_CHIP, host->addr + TUL_SCtrl0);

	/* program HBA's SCSI ID        */
	outb(host->scsi_id << 4, host->addr + TUL_SScsiId);

	/* Enable Initiator Mode ,phase latch,alternate sync period mode,
	   disable SCSI reset */
	if (host->config & HCC_EN_PAR)
		host->sconf1 = (TSC_INITDEFAULT | TSC_EN_SCSI_PAR);
	else
		host->sconf1 = (TSC_INITDEFAULT);
	outb(host->sconf1, host->addr + TUL_SConfig);

	/* Enable HW reselect */
	outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);

	outb(0, host->addr + TUL_SPeriod);

	/* selection time out = 250 ms */
	outb(153, host->addr + TUL_STimeOut);

	/* Enable SCSI terminator */
	outb((host->config & (HCC_ACT_TERM1 | HCC_ACT_TERM2)),
		host->addr + TUL_XCtrl);
	outb(((host->config & HCC_AUTO_TERM) >> 4) |
		(inb(host->addr + TUL_GCTRL1) & 0xFE),
		host->addr + TUL_GCTRL1);

	for (i = 0,
	     flags = & (i91unvramp->NVM_SCSIInfo[0].NVM_Targ0Config),
	     heads = bios_addr + 0x180;
	     i < host->max_tar;
	     i++, flags++) {
		host->targets[i].flags = *flags & ~(TCF_SYNC_DONE | TCF_WDTR_DONE);
		if (host->targets[i].flags & TCF_EN_255)
			host->targets[i].drv_flags = TCF_DRV_255_63;
		else
			host->targets[i].drv_flags = 0;
		host->targets[i].js_period = 0;
		host->targets[i].sconfig0 = host->sconf1;
		host->targets[i].heads = *heads++;
		if (host->targets[i].heads == 255)
			host->targets[i].drv_flags = TCF_DRV_255_63;
		else
			host->targets[i].drv_flags = 0;
		host->targets[i].sectors = *heads++;
		host->targets[i].flags &= ~TCF_BUSY;
		host->act_tags[i] = 0;
		host->max_tags[i] = 0xFF;
	}			/* for                          */
	printk("i91u: PCI Base=0x%04X, IRQ=%d, BIOS=0x%04X0, SCSI ID=%d\n",
	       host->addr, host->pci_dev->irq,
	       host->bios_addr, host->scsi_id);
	/* Reset SCSI Bus */
	if (host->config & HCC_SCSI_RESET) {
		printk(KERN_INFO "i91u: Reset SCSI Bus ... \n");
		initio_reset_scsi(host, 10);
	}
	outb(0x17, host->addr + TUL_SCFG1);
	outb(0xE9, host->addr + TUL_SIntEnable);
}

/**
 *	initio_alloc_scb		-	Allocate an SCB
 *	@host: InitIO host we are allocating for
 *
 *	Walk the SCB list for the controller and allocate a free SCB if
 *	one exists.
 */
static struct scsi_ctrl_blk *initio_alloc_scb(struct initio_host *host)
{
	struct scsi_ctrl_blk *scb;
	unsigned long flags;

	spin_lock_irqsave(&host->avail_lock, flags);
	if ((scb = host->first_avail) != NULL) {
#if DEBUG_QUEUE
		printk("find scb at %p\n", scb);
#endif
		if ((host->first_avail = scb->next) == NULL)
			host->last_avail = NULL;
		scb->next = NULL;
		scb->status = SCB_RENT;
	}
	spin_unlock_irqrestore(&host->avail_lock, flags);
	return scb;
}

/**
 *	initio_release_scb		-	Release an SCB
 *	@host: InitIO host that owns the SCB
 *	@cmnd: SCB command block being returned
 *
 *	Return an allocated SCB to the host free list
 */

static void initio_release_scb(struct initio_host * host, struct scsi_ctrl_blk * cmnd)
{
	unsigned long flags;

#if DEBUG_QUEUE
	printk("Release SCB %p; ", cmnd);
#endif
	spin_lock_irqsave(&(host->avail_lock), flags);
	cmnd->srb = NULL;
	cmnd->status = 0;
	cmnd->next = NULL;
	if (host->last_avail != NULL) {
		host->last_avail->next = cmnd;
		host->last_avail = cmnd;
	} else {
		host->first_avail = cmnd;
		host->last_avail = cmnd;
	}
	spin_unlock_irqrestore(&(host->avail_lock), flags);
}

/***************************************************************************/
static void initio_append_pend_scb(struct initio_host * host, struct scsi_ctrl_blk * scbp)
{

#if DEBUG_QUEUE
	printk("Append pend SCB %p; ", scbp);
#endif
	scbp->status = SCB_PEND;
	scbp->next = NULL;
	if (host->last_pending != NULL) {
		host->last_pending->next = scbp;
		host->last_pending = scbp;
	} else {
		host->first_pending = scbp;
		host->last_pending = scbp;
	}
}

/***************************************************************************/
static void initio_push_pend_scb(struct initio_host * host, struct scsi_ctrl_blk * scbp)
{

#if DEBUG_QUEUE
	printk("Push pend SCB %p; ", scbp);
#endif
	scbp->status = SCB_PEND;
	if ((scbp->next = host->first_pending) != NULL) {
		host->first_pending = scbp;
	} else {
		host->first_pending = scbp;
		host->last_pending = scbp;
	}
}

static struct scsi_ctrl_blk *initio_find_first_pend_scb(struct initio_host * host)
{
	struct scsi_ctrl_blk *first;


	first = host->first_pending;
	while (first != NULL) {
		if (first->opcode != ExecSCSI)
			return first;
		if (first->tagmsg == 0) {
			if ((host->act_tags[first->target] == 0) &&
			    !(host->targets[first->target].flags & TCF_BUSY))
				return first;
		} else {
			if ((host->act_tags[first->target] >=
			  host->max_tags[first->target]) |
			    (host->targets[first->target].flags & TCF_BUSY)) {
				first = first->next;
				continue;
			}
			return first;
		}
		first = first->next;
	}
	return first;
}

static void initio_unlink_pend_scb(struct initio_host * host, struct scsi_ctrl_blk * scb)
{
	struct scsi_ctrl_blk *tmp, *prev;

#if DEBUG_QUEUE
	printk("unlink pend SCB %p; ", scb);
#endif

	prev = tmp = host->first_pending;
	while (tmp != NULL) {
		if (scb == tmp) {	/* Unlink this SCB              */
			if (tmp == host->first_pending) {
				if ((host->first_pending = tmp->next) == NULL)
					host->last_pending = NULL;
			} else {
				prev->next = tmp->next;
				if (tmp == host->last_pending)
					host->last_pending = prev;
			}
			tmp->next = NULL;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}
}

static void initio_append_busy_scb(struct initio_host * host, struct scsi_ctrl_blk * scbp)
{

#if DEBUG_QUEUE
	printk("append busy SCB %p; ", scbp);
#endif
	if (scbp->tagmsg)
		host->act_tags[scbp->target]++;
	else
		host->targets[scbp->target].flags |= TCF_BUSY;
	scbp->status = SCB_BUSY;
	scbp->next = NULL;
	if (host->last_busy != NULL) {
		host->last_busy->next = scbp;
		host->last_busy = scbp;
	} else {
		host->first_busy = scbp;
		host->last_busy = scbp;
	}
}

/***************************************************************************/
static struct scsi_ctrl_blk *initio_pop_busy_scb(struct initio_host * host)
{
	struct scsi_ctrl_blk *tmp;


	if ((tmp = host->first_busy) != NULL) {
		if ((host->first_busy = tmp->next) == NULL)
			host->last_busy = NULL;
		tmp->next = NULL;
		if (tmp->tagmsg)
			host->act_tags[tmp->target]--;
		else
			host->targets[tmp->target].flags &= ~TCF_BUSY;
	}
#if DEBUG_QUEUE
	printk("Pop busy SCB %p; ", tmp);
#endif
	return tmp;
}

/***************************************************************************/
static void initio_unlink_busy_scb(struct initio_host * host, struct scsi_ctrl_blk * scb)
{
	struct scsi_ctrl_blk *tmp, *prev;

#if DEBUG_QUEUE
	printk("unlink busy SCB %p; ", scb);
#endif

	prev = tmp = host->first_busy;
	while (tmp != NULL) {
		if (scb == tmp) {	/* Unlink this SCB              */
			if (tmp == host->first_busy) {
				if ((host->first_busy = tmp->next) == NULL)
					host->last_busy = NULL;
			} else {
				prev->next = tmp->next;
				if (tmp == host->last_busy)
					host->last_busy = prev;
			}
			tmp->next = NULL;
			if (tmp->tagmsg)
				host->act_tags[tmp->target]--;
			else
				host->targets[tmp->target].flags &= ~TCF_BUSY;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}
	return;
}

struct scsi_ctrl_blk *initio_find_busy_scb(struct initio_host * host, u16 tarlun)
{
	struct scsi_ctrl_blk *tmp, *prev;
	u16 scbp_tarlun;


	prev = tmp = host->first_busy;
	while (tmp != NULL) {
		scbp_tarlun = (tmp->lun << 8) | (tmp->target);
		if (scbp_tarlun == tarlun) {	/* Unlink this SCB              */
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}
#if DEBUG_QUEUE
	printk("find busy SCB %p; ", tmp);
#endif
	return tmp;
}

static void initio_append_done_scb(struct initio_host * host, struct scsi_ctrl_blk * scbp)
{
#if DEBUG_QUEUE
	printk("append done SCB %p; ", scbp);
#endif

	scbp->status = SCB_DONE;
	scbp->next = NULL;
	if (host->last_done != NULL) {
		host->last_done->next = scbp;
		host->last_done = scbp;
	} else {
		host->first_done = scbp;
		host->last_done = scbp;
	}
}

struct scsi_ctrl_blk *initio_find_done_scb(struct initio_host * host)
{
	struct scsi_ctrl_blk *tmp;

	if ((tmp = host->first_done) != NULL) {
		if ((host->first_done = tmp->next) == NULL)
			host->last_done = NULL;
		tmp->next = NULL;
	}
#if DEBUG_QUEUE
	printk("find done SCB %p; ",tmp);
#endif
	return tmp;
}

static int initio_abort_srb(struct initio_host * host, struct scsi_cmnd *srbp)
{
	unsigned long flags;
	struct scsi_ctrl_blk *tmp, *prev;

	spin_lock_irqsave(&host->semaph_lock, flags);

	if ((host->semaph == 0) && (host->active == NULL)) {
		/* disable Jasmin SCSI Int        */
		outb(0x1F, host->addr + TUL_Mask);
		spin_unlock_irqrestore(&host->semaph_lock, flags);
		/* FIXME: synchronize_irq needed ? */
		tulip_main(host);
		spin_lock_irqsave(&host->semaph_lock, flags);
		host->semaph = 1;
		outb(0x0F, host->addr + TUL_Mask);
		spin_unlock_irqrestore(&host->semaph_lock, flags);
		return SCSI_ABORT_SNOOZE;
	}
	prev = tmp = host->first_pending;	/* Check Pend queue */
	while (tmp != NULL) {
		/* 07/27/98 */
		if (tmp->srb == srbp) {
			if (tmp == host->active) {
				spin_unlock_irqrestore(&host->semaph_lock, flags);
				return SCSI_ABORT_BUSY;
			} else if (tmp == host->first_pending) {
				if ((host->first_pending = tmp->next) == NULL)
					host->last_pending = NULL;
			} else {
				prev->next = tmp->next;
				if (tmp == host->last_pending)
					host->last_pending = prev;
			}
			tmp->hastat = HOST_ABORTED;
			tmp->flags |= SCF_DONE;
			if (tmp->flags & SCF_POST)
				(*tmp->post) ((u8 *) host, (u8 *) tmp);
			spin_unlock_irqrestore(&host->semaph_lock, flags);
			return SCSI_ABORT_SUCCESS;
		}
		prev = tmp;
		tmp = tmp->next;
	}

	prev = tmp = host->first_busy;	/* Check Busy queue */
	while (tmp != NULL) {
		if (tmp->srb == srbp) {
			if (tmp == host->active) {
				spin_unlock_irqrestore(&host->semaph_lock, flags);
				return SCSI_ABORT_BUSY;
			} else if (tmp->tagmsg == 0) {
				spin_unlock_irqrestore(&host->semaph_lock, flags);
				return SCSI_ABORT_BUSY;
			} else {
				host->act_tags[tmp->target]--;
				if (tmp == host->first_busy) {
					if ((host->first_busy = tmp->next) == NULL)
						host->last_busy = NULL;
				} else {
					prev->next = tmp->next;
					if (tmp == host->last_busy)
						host->last_busy = prev;
				}
				tmp->next = NULL;


				tmp->hastat = HOST_ABORTED;
				tmp->flags |= SCF_DONE;
				if (tmp->flags & SCF_POST)
					(*tmp->post) ((u8 *) host, (u8 *) tmp);
				spin_unlock_irqrestore(&host->semaph_lock, flags);
				return SCSI_ABORT_SUCCESS;
			}
		}
		prev = tmp;
		tmp = tmp->next;
	}
	spin_unlock_irqrestore(&host->semaph_lock, flags);
	return SCSI_ABORT_NOT_RUNNING;
}

/***************************************************************************/
static int initio_bad_seq(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb;

	printk("initio_bad_seg c=%d\n", host->index);

	if ((scb = host->active) != NULL) {
		initio_unlink_busy_scb(host, scb);
		scb->hastat = HOST_BAD_PHAS;
		scb->tastat = 0;
		initio_append_done_scb(host, scb);
	}
	initio_stop_bm(host);
	initio_reset_scsi(host, 8);	/* 7/29/98 */
	return initio_post_scsi_rst(host);
}


/************************************************************************/
static void initio_exec_scb(struct initio_host * host, struct scsi_ctrl_blk * scb)
{
	unsigned long flags;

	scb->mode = 0;

	scb->sgidx = 0;
	scb->sgmax = scb->sglen;

	spin_lock_irqsave(&host->semaph_lock, flags);

	initio_append_pend_scb(host, scb);	/* Append this SCB to Pending queue */

/* VVVVV 07/21/98 */
	if (host->semaph == 1) {
		/* Disable Jasmin SCSI Int */
		outb(0x1F, host->addr + TUL_Mask);
		host->semaph = 0;
		spin_unlock_irqrestore(&host->semaph_lock, flags);

		tulip_main(host);

		spin_lock_irqsave(&host->semaph_lock, flags);
		host->semaph = 1;
		outb(0x0F, host->addr + TUL_Mask);
	}
	spin_unlock_irqrestore(&host->semaph_lock, flags);
	return;
}

/***************************************************************************/
static int initio_isr(struct initio_host * host)
{
	if (inb(host->addr + TUL_Int) & TSS_INT_PENDING) {
		if (host->semaph == 1) {
			outb(0x1F, host->addr + TUL_Mask);
			/* Disable Tulip SCSI Int */
			host->semaph = 0;

			tulip_main(host);

			host->semaph = 1;
			outb(0x0F, host->addr + TUL_Mask);
			return 1;
		}
	}
	return 0;
}

static int tulip_main(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb;

	for (;;) {
		tulip_scsi(host);	/* Call tulip_scsi              */

		/* Walk the list of completed SCBs */
		while ((scb = initio_find_done_scb(host)) != NULL) {	/* find done entry */
			if (scb->tastat == INI_QUEUE_FULL) {
				host->max_tags[scb->target] =
				    host->act_tags[scb->target] - 1;
				scb->tastat = 0;
				initio_append_pend_scb(host, scb);
				continue;
			}
			if (!(scb->mode & SCM_RSENS)) {		/* not in auto req. sense mode */
				if (scb->tastat == 2) {

					/* clr sync. nego flag */

					if (scb->flags & SCF_SENSE) {
						u8 len;
						len = scb->senselen;
						if (len == 0)
							len = 1;
						scb->buflen = scb->senselen;
						scb->bufptr = scb->senseptr;
						scb->flags &= ~(SCF_SG | SCF_DIR);	/* for xfer_data_in */
						/* so, we won't report wrong direction in xfer_data_in,
						   and won't report HOST_DO_DU in state_6 */
						scb->mode = SCM_RSENS;
						scb->ident &= 0xBF;	/* Disable Disconnect */
						scb->tagmsg = 0;
						scb->tastat = 0;
						scb->cdblen = 6;
						scb->cdb[0] = SCSICMD_RequestSense;
						scb->cdb[1] = 0;
						scb->cdb[2] = 0;
						scb->cdb[3] = 0;
						scb->cdb[4] = len;
						scb->cdb[5] = 0;
						initio_push_pend_scb(host, scb);
						break;
					}
				}
			} else {	/* in request sense mode */

				if (scb->tastat == 2) {		/* check contition status again after sending
									   requset sense cmd 0x3 */
					scb->hastat = HOST_BAD_PHAS;
				}
				scb->tastat = 2;
			}
			scb->flags |= SCF_DONE;
			if (scb->flags & SCF_POST) {
				/* FIXME: only one post method and lose casts */
				(*scb->post) ((u8 *) host, (u8 *) scb);
			}
		}		/* while */
		/* find_active: */
		if (inb(host->addr + TUL_SStatus0) & TSS_INT_PENDING)
			continue;
		if (host->active)	/* return to OS and wait for xfer_done_ISR/Selected_ISR */
			return 1;	/* return to OS, enable interrupt */
		/* Check pending SCB            */
		if (initio_find_first_pend_scb(host) == NULL)
			return 1;	/* return to OS, enable interrupt */
	}			/* End of for loop */
	/* statement won't reach here */
}

static void tulip_scsi(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb;
	struct target_control *active_tc;

	/* make sure to service interrupt asap */
	if ((host->jsstatus0 = inb(host->addr + TUL_SStatus0)) & TSS_INT_PENDING) {
		host->phase = host->jsstatus0 & TSS_PH_MASK;
		host->jsstatus1 = inb(host->addr + TUL_SStatus1);
		host->jsint = inb(host->addr + TUL_SInt);
		if (host->jsint & TSS_SCSIRST_INT) {	/* SCSI bus reset detected      */
			int_initio_scsi_rst(host);
			return;
		}
		if (host->jsint & TSS_RESEL_INT) {	/* if selected/reselected interrupt */
			if (int_initio_resel(host) == 0)
				initio_next_state(host);
			return;
		}
		if (host->jsint & TSS_SEL_TIMEOUT) {
			int_initio_busfree(host);
			return;
		}
		if (host->jsint & TSS_DISC_INT) {	/* BUS disconnection            */
			int_initio_busfree(host);	/* unexpected bus free or sel timeout */
			return;
		}
		if (host->jsint & (TSS_FUNC_COMP | TSS_BUS_SERV)) {	/* func complete or Bus service */
			if ((scb = host->active) != NULL)
				initio_next_state(host);
			return;
		}
	}
	if (host->active != NULL)
		return;

	if ((scb = initio_find_first_pend_scb(host)) == NULL)
		return;

	/* program HBA's SCSI ID & target SCSI ID */
	outb((host->scsi_id << 4) | (scb->target & 0x0F),
		host->addr + TUL_SScsiId);
	if (scb->opcode == ExecSCSI) {
		active_tc = &host->targets[scb->target];

		if (scb->tagmsg)
			active_tc->drv_flags |= TCF_DRV_EN_TAG;
		else
			active_tc->drv_flags &= ~TCF_DRV_EN_TAG;

		outb(active_tc->js_period, host->addr + TUL_SPeriod);
		if ((active_tc->flags & (TCF_WDTR_DONE | TCF_NO_WDTR)) == 0) {	/* do wdtr negotiation          */
			initio_select_atn_stop(host, scb);
		} else {
			if ((active_tc->flags & (TCF_SYNC_DONE | TCF_NO_SYNC_NEGO)) == 0) {	/* do sync negotiation          */
				initio_select_atn_stop(host, scb);
			} else {
				if (scb->tagmsg)
					initio_select_atn3(host, scb);
				else
					initio_select_atn(host, scb);
			}
		}
		if (scb->flags & SCF_POLL) {
			while (wait_tulip(host) != -1) {
				if (initio_next_state(host) == -1)
					break;
			}
		}
	} else if (scb->opcode == BusDevRst) {
		initio_select_atn_stop(host, scb);
		scb->next_state = 8;
		if (scb->flags & SCF_POLL) {
			while (wait_tulip(host) != -1) {
				if (initio_next_state(host) == -1)
					break;
			}
		}
	} else if (scb->opcode == AbortCmd) {
		if (initio_abort_srb(host, scb->srb) != 0) {
			initio_unlink_pend_scb(host, scb);
			initio_release_scb(host, scb);
		} else {
			scb->opcode = BusDevRst;
			initio_select_atn_stop(host, scb);
			scb->next_state = 8;
		}
	} else {
		initio_unlink_pend_scb(host, scb);
		scb->hastat = 0x16;	/* bad command */
		initio_append_done_scb(host, scb);
	}
	return;
}

/**
 *	initio_next_state		-	Next SCSI state
 *	@host: InitIO host we are processing
 *
 *	Progress the active command block along the state machine
 *	until we hit a state which we must wait for activity to occur.
 *
 *	Returns zero or a negative code.
 */

static int initio_next_state(struct initio_host * host)
{
	int next;

	next = host->active->next_state;
	for (;;) {
		switch (next) {
		case 1:
			next = initio_state_1(host);
			break;
		case 2:
			next = initio_state_2(host);
			break;
		case 3:
			next = initio_state_3(host);
			break;
		case 4:
			next = initio_state_4(host);
			break;
		case 5:
			next = initio_state_5(host);
			break;
		case 6:
			next = initio_state_6(host);
			break;
		case 7:
			next = initio_state_7(host);
			break;
		case 8:
			return initio_bus_device_reset(host);
		default:
			return initio_bad_seq(host);
		}
		if (next <= 0)
			return next;
	}
}


/**
 *	initio_state_1		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 *	Perform SCSI state processing for Select/Attention/Stop
 */

static int initio_state_1(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	struct target_control *active_tc = host->active_tc;
#if DEBUG_STATE
	printk("-s1-");
#endif

	/* Move the SCB from pending to busy */
	initio_unlink_pend_scb(host, scb);
	initio_append_busy_scb(host, scb);

	outb(active_tc->sconfig0, host->addr + TUL_SConfig );
	/* ATN on */
	if (host->phase == MSG_OUT) {
		outb(TSC_EN_BUS_IN | TSC_HW_RESELECT, host->addr + TUL_SCtrl1);
		outb(scb->ident, host->addr + TUL_SFifo);

		if (scb->tagmsg) {
			outb(scb->tagmsg, host->addr + TUL_SFifo);
			outb(scb->tagid, host->addr + TUL_SFifo);
		}
		if ((active_tc->flags & (TCF_WDTR_DONE | TCF_NO_WDTR)) == 0) {
			active_tc->flags |= TCF_WDTR_DONE;
			outb(MSG_EXTEND, host->addr + TUL_SFifo);
			outb(2, host->addr + TUL_SFifo);	/* Extended msg length */
			outb(3, host->addr + TUL_SFifo);	/* Sync request */
			outb(1, host->addr + TUL_SFifo);	/* Start from 16 bits */
		} else if ((active_tc->flags & (TCF_SYNC_DONE | TCF_NO_SYNC_NEGO)) == 0) {
			active_tc->flags |= TCF_SYNC_DONE;
			outb(MSG_EXTEND, host->addr + TUL_SFifo);
			outb(3, host->addr + TUL_SFifo);	/* extended msg length */
			outb(1, host->addr + TUL_SFifo);	/* sync request */
			outb(initio_rate_tbl[active_tc->flags & TCF_SCSI_RATE], host->addr + TUL_SFifo);
			outb(MAX_OFFSET, host->addr + TUL_SFifo);	/* REQ/ACK offset */
		}
		outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
		if (wait_tulip(host) == -1)
			return -1;
	}
	outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
	outb((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)), host->addr + TUL_SSignal);
	/* Into before CDB xfer */
	return 3;
}


/**
 *	initio_state_2		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 * state after selection with attention
 * state after selection with attention3
 */

static int initio_state_2(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	struct target_control *active_tc = host->active_tc;
#if DEBUG_STATE
	printk("-s2-");
#endif

	initio_unlink_pend_scb(host, scb);
	initio_append_busy_scb(host, scb);

	outb(active_tc->sconfig0, host->addr + TUL_SConfig);

	if (host->jsstatus1 & TSS_CMD_PH_CMP)
		return 4;

	outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
	outb((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)), host->addr + TUL_SSignal);
	/* Into before CDB xfer */
	return 3;
}

/**
 *	initio_state_3		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 * state before CDB xfer is done
 */

static int initio_state_3(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	struct target_control *active_tc = host->active_tc;
	int i;

#if DEBUG_STATE
	printk("-s3-");
#endif
	for (;;) {
		switch (host->phase) {
		case CMD_OUT:	/* Command out phase            */
			for (i = 0; i < (int) scb->cdblen; i++)
				outb(scb->cdb[i], host->addr + TUL_SFifo);
			outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
			if (wait_tulip(host) == -1)
				return -1;
			if (host->phase == CMD_OUT)
				return initio_bad_seq(host);
			return 4;

		case MSG_IN:	/* Message in phase             */
			scb->next_state = 3;
			if (initio_msgin(host) == -1)
				return -1;
			break;

		case STATUS_IN:	/* Status phase                 */
			if (initio_status_msg(host) == -1)
				return -1;
			break;

		case MSG_OUT:	/* Message out phase            */
			if (active_tc->flags & (TCF_SYNC_DONE | TCF_NO_SYNC_NEGO)) {
				outb(MSG_NOP, host->addr + TUL_SFifo);		/* msg nop */
				outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
				if (wait_tulip(host) == -1)
					return -1;
			} else {
				active_tc->flags |= TCF_SYNC_DONE;

				outb(MSG_EXTEND, host->addr + TUL_SFifo);
				outb(3, host->addr + TUL_SFifo);	/* ext. msg len */
				outb(1, host->addr + TUL_SFifo);	/* sync request */
				outb(initio_rate_tbl[active_tc->flags & TCF_SCSI_RATE], host->addr + TUL_SFifo);
				outb(MAX_OFFSET, host->addr + TUL_SFifo);	/* REQ/ACK offset */
				outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
				if (wait_tulip(host) == -1)
					return -1;
				outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
				outb(inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7), host->addr + TUL_SSignal);

			}
			break;
		default:
			return initio_bad_seq(host);
		}
	}
}

/**
 *	initio_state_4		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 *	SCSI state machine. State 4
 */

static int initio_state_4(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;

#if DEBUG_STATE
	printk("-s4-");
#endif
	if ((scb->flags & SCF_DIR) == SCF_NO_XF) {
		return 6;	/* Go to state 6 (After data) */
	}
	for (;;) {
		if (scb->buflen == 0)
			return 6;

		switch (host->phase) {

		case STATUS_IN:	/* Status phase                 */
			if ((scb->flags & SCF_DIR) != 0)	/* if direction bit set then report data underrun */
				scb->hastat = HOST_DO_DU;
			if ((initio_status_msg(host)) == -1)
				return -1;
			break;

		case MSG_IN:	/* Message in phase             */
			scb->next_state = 0x4;
			if (initio_msgin(host) == -1)
				return -1;
			break;

		case MSG_OUT:	/* Message out phase            */
			if (host->jsstatus0 & TSS_PAR_ERROR) {
				scb->buflen = 0;
				scb->hastat = HOST_DO_DU;
				if (initio_msgout_ide(host) == -1)
					return -1;
				return 6;
			} else {
				outb(MSG_NOP, host->addr + TUL_SFifo);		/* msg nop */
				outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
				if (wait_tulip(host) == -1)
					return -1;
			}
			break;

		case DATA_IN:	/* Data in phase                */
			return initio_xfer_data_in(host);

		case DATA_OUT:	/* Data out phase               */
			return initio_xfer_data_out(host);

		default:
			return initio_bad_seq(host);
		}
	}
}


/**
 *	initio_state_5		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 *	State after dma xfer done or phase change before xfer done
 */

static int initio_state_5(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	long cnt, xcnt;		/* cannot use unsigned !! code: if (xcnt < 0) */

#if DEBUG_STATE
	printk("-s5-");
#endif
	/*------ get remaining count -------*/
	cnt = inl(host->addr + TUL_SCnt0) & 0x0FFFFFF;

	if (inb(host->addr + TUL_XCmd) & 0x20) {
		/* ----------------------- DATA_IN ----------------------------- */
		/* check scsi parity error */
		if (host->jsstatus0 & TSS_PAR_ERROR)
			scb->hastat = HOST_DO_DU;
		if (inb(host->addr + TUL_XStatus) & XPEND) {	/* DMA xfer pending, Send STOP  */
			/* tell Hardware  scsi xfer has been terminated */
			outb(inb(host->addr + TUL_XCtrl) | 0x80, host->addr + TUL_XCtrl);
			/* wait until DMA xfer not pending */
			while (inb(host->addr + TUL_XStatus) & XPEND)
				cpu_relax();
		}
	} else {
		/*-------- DATA OUT -----------*/
		if ((inb(host->addr + TUL_SStatus1) & TSS_XFER_CMP) == 0) {
			if (host->active_tc->js_period & TSC_WIDE_SCSI)
				cnt += (inb(host->addr + TUL_SFifoCnt) & 0x1F) << 1;
			else
				cnt += (inb(host->addr + TUL_SFifoCnt) & 0x1F);
		}
		if (inb(host->addr + TUL_XStatus) & XPEND) {	/* if DMA xfer is pending, abort DMA xfer */
			outb(TAX_X_ABT, host->addr + TUL_XCmd);
			/* wait Abort DMA xfer done */
			while ((inb(host->addr + TUL_Int) & XABT) == 0)
				cpu_relax();
		}
		if ((cnt == 1) && (host->phase == DATA_OUT)) {
			outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
			if (wait_tulip(host) == -1)
				return -1;
			cnt = 0;
		} else {
			if ((inb(host->addr + TUL_SStatus1) & TSS_XFER_CMP) == 0)
				outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
		}
	}
	if (cnt == 0) {
		scb->buflen = 0;
		return 6;	/* After Data */
	}
	/* Update active data pointer */
	xcnt = (long) scb->buflen - cnt;	/* xcnt== bytes already xferred */
	scb->buflen = (u32) cnt;		/* cnt == bytes left to be xferred */
	if (scb->flags & SCF_SG) {
		struct sg_entry *sgp;
		unsigned long i;

		sgp = &scb->sglist[scb->sgidx];
		for (i = scb->sgidx; i < scb->sgmax; sgp++, i++) {
			xcnt -= (long) sgp->len;
			if (xcnt < 0) {		/* this sgp xfer half done */
				xcnt += (long) sgp->len;	/* xcnt == bytes xferred in this sgp */
				sgp->data += (u32) xcnt;	/* new ptr to be xfer */
				sgp->len -= (u32) xcnt;	/* new len to be xfer */
				scb->bufptr += ((u32) (i - scb->sgidx) << 3);
				/* new SG table ptr */
				scb->sglen = (u8) (scb->sgmax - i);
				/* new SG table len */
				scb->sgidx = (u16) i;
				/* for next disc and come in this loop */
				return 4;	/* Go to state 4                */
			}
			/* else (xcnt >= 0 , i.e. this sgp already xferred */
		}		/* for */
		return 6;	/* Go to state 6                */
	} else {
		scb->bufptr += (u32) xcnt;
	}
	return 4;		/* Go to state 4                */
}

/**
 *	initio_state_6		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 *	State after Data phase
 */

static int initio_state_6(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;

#if DEBUG_STATE
	printk("-s6-");
#endif
	for (;;) {
		switch (host->phase) {
		case STATUS_IN:	/* Status phase                 */
			if ((initio_status_msg(host)) == -1)
				return -1;
			break;

		case MSG_IN:	/* Message in phase             */
			scb->next_state = 6;
			if ((initio_msgin(host)) == -1)
				return -1;
			break;

		case MSG_OUT:	/* Message out phase            */
			outb(MSG_NOP, host->addr + TUL_SFifo);		/* msg nop */
			outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
			if (wait_tulip(host) == -1)
				return -1;
			break;

		case DATA_IN:	/* Data in phase                */
			return initio_xpad_in(host);

		case DATA_OUT:	/* Data out phase               */
			return initio_xpad_out(host);

		default:
			return initio_bad_seq(host);
		}
	}
}

/**
 *	initio_state_7		-	SCSI state machine
 *	@host: InitIO host we are controlling
 *
 */

int initio_state_7(struct initio_host * host)
{
	int cnt, i;

#if DEBUG_STATE
	printk("-s7-");
#endif
	/* flush SCSI FIFO */
	cnt = inb(host->addr + TUL_SFifoCnt) & 0x1F;
	if (cnt) {
		for (i = 0; i < cnt; i++)
			inb(host->addr + TUL_SFifo);
	}
	switch (host->phase) {
	case DATA_IN:		/* Data in phase                */
	case DATA_OUT:		/* Data out phase               */
		return initio_bad_seq(host);
	default:
		return 6;	/* Go to state 6                */
	}
}

/**
 *	initio_xfer_data_in	-	Commence data input
 *	@host: InitIO host in use
 *
 *	Commence a block of data transfer. The transfer itself will
 *	be managed by the controller and we will get a completion (or
 *	failure) interrupt.
 */
static int initio_xfer_data_in(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;

	if ((scb->flags & SCF_DIR) == SCF_DOUT)
		return 6;	/* wrong direction */

	outl(scb->buflen, host->addr + TUL_SCnt0);
	outb(TSC_XF_DMA_IN, host->addr + TUL_SCmd);	/* 7/25/95 */

	if (scb->flags & SCF_SG) {	/* S/G xfer */
		outl(((u32) scb->sglen) << 3, host->addr + TUL_XCntH);
		outl(scb->bufptr, host->addr + TUL_XAddH);
		outb(TAX_SG_IN, host->addr + TUL_XCmd);
	} else {
		outl(scb->buflen, host->addr + TUL_XCntH);
		outl(scb->bufptr, host->addr + TUL_XAddH);
		outb(TAX_X_IN, host->addr + TUL_XCmd);
	}
	scb->next_state = 0x5;
	return 0;		/* return to OS, wait xfer done , let jas_isr come in */
}

/**
 *	initio_xfer_data_out	-	Commence data output
 *	@host: InitIO host in use
 *
 *	Commence a block of data transfer. The transfer itself will
 *	be managed by the controller and we will get a completion (or
 *	failure) interrupt.
 */

static int initio_xfer_data_out(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;

	if ((scb->flags & SCF_DIR) == SCF_DIN)
		return 6;	/* wrong direction */

	outl(scb->buflen, host->addr + TUL_SCnt0);
	outb(TSC_XF_DMA_OUT, host->addr + TUL_SCmd);

	if (scb->flags & SCF_SG) {	/* S/G xfer */
		outl(((u32) scb->sglen) << 3, host->addr + TUL_XCntH);
		outl(scb->bufptr, host->addr + TUL_XAddH);
		outb(TAX_SG_OUT, host->addr + TUL_XCmd);
	} else {
		outl(scb->buflen, host->addr + TUL_XCntH);
		outl(scb->bufptr, host->addr + TUL_XAddH);
		outb(TAX_X_OUT, host->addr + TUL_XCmd);
	}

	scb->next_state = 0x5;
	return 0;		/* return to OS, wait xfer done , let jas_isr come in */
}

int initio_xpad_in(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	struct target_control *active_tc = host->active_tc;

	if ((scb->flags & SCF_DIR) != SCF_NO_DCHK)
		scb->hastat = HOST_DO_DU;	/* over run             */
	for (;;) {
		if (active_tc->js_period & TSC_WIDE_SCSI)
			outl(2, host->addr + TUL_SCnt0);
		else
			outl(1, host->addr + TUL_SCnt0);

		outb(TSC_XF_FIFO_IN, host->addr + TUL_SCmd);
		if (wait_tulip(host) == -1)
			return -1;
		if (host->phase != DATA_IN) {
			outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
			return 6;
		}
		inb(host->addr + TUL_SFifo);
	}
}

int initio_xpad_out(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	struct target_control *active_tc = host->active_tc;

	if ((scb->flags & SCF_DIR) != SCF_NO_DCHK)
		scb->hastat = HOST_DO_DU;	/* over run             */
	for (;;) {
		if (active_tc->js_period & TSC_WIDE_SCSI)
			outl(2, host->addr + TUL_SCnt0);
		else
			outl(1, host->addr + TUL_SCnt0);

		outb(0, host->addr + TUL_SFifo);
		outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
		if ((wait_tulip(host)) == -1)
			return -1;
		if (host->phase != DATA_OUT) {	/* Disable wide CPU to allow read 16 bits */
			outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);
			outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
			return 6;
		}
	}
}

int initio_status_msg(struct initio_host * host)
{				/* status & MSG_IN */
	struct scsi_ctrl_blk *scb = host->active;
	u8 msg;

	outb(TSC_CMD_COMP, host->addr + TUL_SCmd);
	if (wait_tulip(host) == -1)
		return -1;

	/* get status */
	scb->tastat = inb(host->addr + TUL_SFifo);

	if (host->phase == MSG_OUT) {
		if (host->jsstatus0 & TSS_PAR_ERROR)
			outb(MSG_PARITY, host->addr + TUL_SFifo);
		else
			outb(MSG_NOP, host->addr + TUL_SFifo);
		outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
		return wait_tulip(host);
	}
	if (host->phase == MSG_IN) {
		msg = inb(host->addr + TUL_SFifo);
		if (host->jsstatus0 & TSS_PAR_ERROR) {	/* Parity error                 */
			if ((initio_msgin_accept(host)) == -1)
				return -1;
			if (host->phase != MSG_OUT)
				return initio_bad_seq(host);
			outb(MSG_PARITY, host->addr + TUL_SFifo);
			outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
			return wait_tulip(host);
		}
		if (msg == 0) {	/* Command complete             */

			if ((scb->tastat & 0x18) == 0x10)	/* No link support              */
				return initio_bad_seq(host);
			outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
			outb(TSC_MSG_ACCEPT, host->addr + TUL_SCmd);
			return initio_wait_done_disc(host);

		}
		if (msg == MSG_LINK_COMP || msg == MSG_LINK_FLAG) {
			if ((scb->tastat & 0x18) == 0x10)
				return initio_msgin_accept(host);
		}
	}
	return initio_bad_seq(host);
}


/* scsi bus free */
int int_initio_busfree(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;

	if (scb != NULL) {
		if (scb->status & SCB_SELECT) {		/* selection timeout */
			initio_unlink_pend_scb(host, scb);
			scb->hastat = HOST_SEL_TOUT;
			initio_append_done_scb(host, scb);
		} else {	/* Unexpected bus free          */
			initio_unlink_busy_scb(host, scb);
			scb->hastat = HOST_BUS_FREE;
			initio_append_done_scb(host, scb);
		}
		host->active = NULL;
		host->active_tc = NULL;
	}
	outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);		/* Flush SCSI FIFO  */
	outb(TSC_INITDEFAULT, host->addr + TUL_SConfig);
	outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);	/* Enable HW reselect       */
	return -1;
}


/**
 *	int_initio_scsi_rst	-	SCSI reset occurred
 *	@host: Host seeing the reset
 *
 *	A SCSI bus reset has occurred. Clean up any pending transfer
 *	the hardware is doing by DMA and then abort all active and
 *	disconnected commands. The mid layer should sort the rest out
 *	for us
 */

static int int_initio_scsi_rst(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb;
	int i;

	/* if DMA xfer is pending, abort DMA xfer */
	if (inb(host->addr + TUL_XStatus) & 0x01) {
		outb(TAX_X_ABT | TAX_X_CLR_FIFO, host->addr + TUL_XCmd);
		/* wait Abort DMA xfer done */
		while ((inb(host->addr + TUL_Int) & 0x04) == 0)
			cpu_relax();
		outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
	}
	/* Abort all active & disconnected scb */
	while ((scb = initio_pop_busy_scb(host)) != NULL) {
		scb->hastat = HOST_BAD_PHAS;
		initio_append_done_scb(host, scb);
	}
	host->active = NULL;
	host->active_tc = NULL;

	/* clr sync nego. done flag */
	for (i = 0; i < host->max_tar; i++)
		host->targets[i].flags &= ~(TCF_SYNC_DONE | TCF_WDTR_DONE);
	return -1;
}

/**
 *	int_initio_scsi_resel	-	Reselection occurred
 *	@host: InitIO host adapter
 *
 *	A SCSI reselection event has been signalled and the interrupt
 *	is now being processed. Work out which command block needs attention
 *	and continue processing that command.
 */

int int_initio_resel(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb;
	struct target_control *active_tc;
	u8 tag, msg = 0;
	u8 tar, lun;

	if ((scb = host->active) != NULL) {
		/* FIXME: Why check and not just clear ? */
		if (scb->status & SCB_SELECT)		/* if waiting for selection complete */
			scb->status &= ~SCB_SELECT;
		host->active = NULL;
	}
	/* --------- get target id---------------------- */
	tar = inb(host->addr + TUL_SBusId);
	/* ------ get LUN from Identify message----------- */
	lun = inb(host->addr + TUL_SIdent) & 0x0F;
	/* 07/22/98 from 0x1F -> 0x0F */
	active_tc = &host->targets[tar];
	host->active_tc = active_tc;
	outb(active_tc->sconfig0, host->addr + TUL_SConfig);
	outb(active_tc->js_period, host->addr + TUL_SPeriod);

	/* ------------- tag queueing ? ------------------- */
	if (active_tc->drv_flags & TCF_DRV_EN_TAG) {
		if ((initio_msgin_accept(host)) == -1)
			return -1;
		if (host->phase != MSG_IN)
			goto no_tag;
		outl(1, host->addr + TUL_SCnt0);
		outb(TSC_XF_FIFO_IN, host->addr + TUL_SCmd);
		if (wait_tulip(host) == -1)
			return -1;
		msg = inb(host->addr + TUL_SFifo);	/* Read Tag Message    */

		if (msg < MSG_STAG || msg > MSG_OTAG)		/* Is simple Tag      */
			goto no_tag;

		if (initio_msgin_accept(host) == -1)
			return -1;

		if (host->phase != MSG_IN)
			goto no_tag;

		outl(1, host->addr + TUL_SCnt0);
		outb(TSC_XF_FIFO_IN, host->addr + TUL_SCmd);
		if (wait_tulip(host) == -1)
			return -1;
		tag = inb(host->addr + TUL_SFifo);	/* Read Tag ID       */
		scb = host->scb + tag;
		if (scb->target != tar || scb->lun != lun) {
			return initio_msgout_abort_tag(host);
		}
		if (scb->status != SCB_BUSY) {	/* 03/24/95             */
			return initio_msgout_abort_tag(host);
		}
		host->active = scb;
		if ((initio_msgin_accept(host)) == -1)
			return -1;
	} else {		/* No tag               */
	      no_tag:
		if ((scb = initio_find_busy_scb(host, tar | (lun << 8))) == NULL) {
			return initio_msgout_abort_targ(host);
		}
		host->active = scb;
		if (!(active_tc->drv_flags & TCF_DRV_EN_TAG)) {
			if ((initio_msgin_accept(host)) == -1)
				return -1;
		}
	}
	return 0;
}

/**
 *	int_initio_bad_seq		-	out of phase
 *	@host: InitIO host flagging event
 *
 *	We have ended up out of phase somehow. Reset the host controller
 *	and throw all our toys out of the pram. Let the midlayer clean up
 */

static int int_initio_bad_seq(struct initio_host * host)
{				/* target wrong phase           */
	struct scsi_ctrl_blk *scb;
	int i;

	initio_reset_scsi(host, 10);

	while ((scb = initio_pop_busy_scb(host)) != NULL) {
		scb->hastat = HOST_BAD_PHAS;
		initio_append_done_scb(host, scb);
	}
	for (i = 0; i < host->max_tar; i++)
		host->targets[i].flags &= ~(TCF_SYNC_DONE | TCF_WDTR_DONE);
	return -1;
}


/**
 *	initio_msgout_abort_targ		-	abort a tag
 *	@host: InitIO host
 *
 *	Abort when the target/lun does not match or when our SCB is not
 *	busy. Used by untagged commands.
 */

static int initio_msgout_abort_targ(struct initio_host * host)
{

	outb(((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)) | TSC_SET_ATN), host->addr + TUL_SSignal);
	if (initio_msgin_accept(host) == -1)
		return -1;
	if (host->phase != MSG_OUT)
		return initio_bad_seq(host);

	outb(MSG_ABORT, host->addr + TUL_SFifo);
	outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);

	return initio_wait_disc(host);
}

/**
 *	initio_msgout_abort_tag		-	abort a tag
 *	@host: InitIO host
 *
 *	Abort when the target/lun does not match or when our SCB is not
 *	busy. Used for tagged commands.
 */

static int initio_msgout_abort_tag(struct initio_host * host)
{

	outb(((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)) | TSC_SET_ATN), host->addr + TUL_SSignal);
	if (initio_msgin_accept(host) == -1)
		return -1;
	if (host->phase != MSG_OUT)
		return initio_bad_seq(host);

	outb(MSG_ABORT_TAG, host->addr + TUL_SFifo);
	outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);

	return initio_wait_disc(host);

}

/**
 *	initio_msgin		-	Message in
 *	@host: InitIO Host
 *
 *	Process incoming message
 */
static int initio_msgin(struct initio_host * host)
{
	struct target_control *active_tc;

	for (;;) {
		outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);

		outl(1, host->addr + TUL_SCnt0);
		outb(TSC_XF_FIFO_IN, host->addr + TUL_SCmd);
		if (wait_tulip(host) == -1)
			return -1;

		switch (inb(host->addr + TUL_SFifo)) {
		case MSG_DISC:	/* Disconnect msg */
			outb(TSC_MSG_ACCEPT, host->addr + TUL_SCmd);
			return initio_wait_disc(host);
		case MSG_SDP:
		case MSG_RESTORE:
		case MSG_NOP:
			initio_msgin_accept(host);
			break;
		case MSG_REJ:	/* Clear ATN first              */
			outb((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)),
				host->addr + TUL_SSignal);
			active_tc = host->active_tc;
			if ((active_tc->flags & (TCF_SYNC_DONE | TCF_NO_SYNC_NEGO)) == 0)	/* do sync nego */
				outb(((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)) | TSC_SET_ATN),
					host->addr + TUL_SSignal);
			initio_msgin_accept(host);
			break;
		case MSG_EXTEND:	/* extended msg */
			initio_msgin_extend(host);
			break;
		case MSG_IGNOREWIDE:
			initio_msgin_accept(host);
			break;
		case MSG_COMP:
			outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);
			outb(TSC_MSG_ACCEPT, host->addr + TUL_SCmd);
			return initio_wait_done_disc(host);
		default:
			initio_msgout_reject(host);
			break;
		}
		if (host->phase != MSG_IN)
			return host->phase;
	}
	/* statement won't reach here */
}

static int initio_msgout_reject(struct initio_host * host)
{
	outb(((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)) | TSC_SET_ATN), host->addr + TUL_SSignal);

	if (initio_msgin_accept(host) == -1)
		return -1;

	if (host->phase == MSG_OUT) {
		outb(MSG_REJ, host->addr + TUL_SFifo);		/* Msg reject           */
		outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
		return wait_tulip(host);
	}
	return host->phase;
}

static int initio_msgout_ide(struct initio_host * host)
{
	outb(MSG_IDE, host->addr + TUL_SFifo);		/* Initiator Detected Error */
	outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
	return wait_tulip(host);
}

static int initio_msgin_extend(struct initio_host * host)
{
	u8 len, idx;

	if (initio_msgin_accept(host) != MSG_IN)
		return host->phase;

	/* Get extended msg length      */
	outl(1, host->addr + TUL_SCnt0);
	outb(TSC_XF_FIFO_IN, host->addr + TUL_SCmd);
	if (wait_tulip(host) == -1)
		return -1;

	len = inb(host->addr + TUL_SFifo);
	host->msg[0] = len;
	for (idx = 1; len != 0; len--) {

		if ((initio_msgin_accept(host)) != MSG_IN)
			return host->phase;
		outl(1, host->addr + TUL_SCnt0);
		outb(TSC_XF_FIFO_IN, host->addr + TUL_SCmd);
		if (wait_tulip(host) == -1)
			return -1;
		host->msg[idx++] = inb(host->addr + TUL_SFifo);
	}
	if (host->msg[1] == 1) {		/* if it's synchronous data transfer request */
		u8 r;
		if (host->msg[0] != 3)	/* if length is not right */
			return initio_msgout_reject(host);
		if (host->active_tc->flags & TCF_NO_SYNC_NEGO) {	/* Set OFFSET=0 to do async, nego back */
			host->msg[3] = 0;
		} else {
			if (initio_msgin_sync(host) == 0 &&
			    (host->active_tc->flags & TCF_SYNC_DONE)) {
				initio_sync_done(host);
				return initio_msgin_accept(host);
			}
		}

		r = inb(host->addr + TUL_SSignal);
		outb((r & (TSC_SET_ACK | 7)) | TSC_SET_ATN,
			host->addr + TUL_SSignal);
		if (initio_msgin_accept(host) != MSG_OUT)
			return host->phase;
		/* sync msg out */
		outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);

		initio_sync_done(host);

		outb(MSG_EXTEND, host->addr + TUL_SFifo);
		outb(3, host->addr + TUL_SFifo);
		outb(1, host->addr + TUL_SFifo);
		outb(host->msg[2], host->addr + TUL_SFifo);
		outb(host->msg[3], host->addr + TUL_SFifo);
		outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
		return wait_tulip(host);
	}
	if (host->msg[0] != 2 || host->msg[1] != 3)
		return initio_msgout_reject(host);
	/* if it's WIDE DATA XFER REQ   */
	if (host->active_tc->flags & TCF_NO_WDTR) {
		host->msg[2] = 0;
	} else {
		if (host->msg[2] > 2)	/* > 32 bits            */
			return initio_msgout_reject(host);
		if (host->msg[2] == 2) {		/* == 32                */
			host->msg[2] = 1;
		} else {
			if ((host->active_tc->flags & TCF_NO_WDTR) == 0) {
				wdtr_done(host);
				if ((host->active_tc->flags & (TCF_SYNC_DONE | TCF_NO_SYNC_NEGO)) == 0)
					outb(((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)) | TSC_SET_ATN), host->addr + TUL_SSignal);
				return initio_msgin_accept(host);
			}
		}
	}
	outb(((inb(host->addr + TUL_SSignal) & (TSC_SET_ACK | 7)) | TSC_SET_ATN), host->addr + TUL_SSignal);

	if (initio_msgin_accept(host) != MSG_OUT)
		return host->phase;
	/* WDTR msg out                 */
	outb(MSG_EXTEND, host->addr + TUL_SFifo);
	outb(2, host->addr + TUL_SFifo);
	outb(3, host->addr + TUL_SFifo);
	outb(host->msg[2], host->addr + TUL_SFifo);
	outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
	return wait_tulip(host);
}

static int initio_msgin_sync(struct initio_host * host)
{
	char default_period;

	default_period = initio_rate_tbl[host->active_tc->flags & TCF_SCSI_RATE];
	if (host->msg[3] > MAX_OFFSET) {
		host->msg[3] = MAX_OFFSET;
		if (host->msg[2] < default_period) {
			host->msg[2] = default_period;
			return 1;
		}
		if (host->msg[2] >= 59)	/* Change to async              */
			host->msg[3] = 0;
		return 1;
	}
	/* offset requests asynchronous transfers ? */
	if (host->msg[3] == 0) {
		return 0;
	}
	if (host->msg[2] < default_period) {
		host->msg[2] = default_period;
		return 1;
	}
	if (host->msg[2] >= 59) {
		host->msg[3] = 0;
		return 1;
	}
	return 0;
}

static int wdtr_done(struct initio_host * host)
{
	host->active_tc->flags &= ~TCF_SYNC_DONE;
	host->active_tc->flags |= TCF_WDTR_DONE;

	host->active_tc->js_period = 0;
	if (host->msg[2])	/* if 16 bit */
		host->active_tc->js_period |= TSC_WIDE_SCSI;
	host->active_tc->sconfig0 &= ~TSC_ALT_PERIOD;
	outb(host->active_tc->sconfig0, host->addr + TUL_SConfig);
	outb(host->active_tc->js_period, host->addr + TUL_SPeriod);

	return 1;
}

static int initio_sync_done(struct initio_host * host)
{
	int i;

	host->active_tc->flags |= TCF_SYNC_DONE;

	if (host->msg[3]) {
		host->active_tc->js_period |= host->msg[3];
		for (i = 0; i < 8; i++) {
			if (initio_rate_tbl[i] >= host->msg[2])	/* pick the big one */
				break;
		}
		host->active_tc->js_period |= (i << 4);
		host->active_tc->sconfig0 |= TSC_ALT_PERIOD;
	}
	outb(host->active_tc->sconfig0, host->addr + TUL_SConfig);
	outb(host->active_tc->js_period, host->addr + TUL_SPeriod);

	return -1;
}


static int initio_post_scsi_rst(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb;
	struct target_control *active_tc;
	int i;

	host->active = NULL;
	host->active_tc = NULL;
	host->flags = 0;

	while ((scb = initio_pop_busy_scb(host)) != NULL) {
		scb->hastat = HOST_BAD_PHAS;
		initio_append_done_scb(host, scb);
	}
	/* clear sync done flag         */
	active_tc = &host->targets[0];
	for (i = 0; i < host->max_tar; active_tc++, i++) {
		active_tc->flags &= ~(TCF_SYNC_DONE | TCF_WDTR_DONE);
		/* Initialize the sync. xfer register values to an asyn xfer */
		active_tc->js_period = 0;
		active_tc->sconfig0 = host->sconf1;
		host->act_tags[0] = 0;	/* 07/22/98 */
		host->targets[i].flags &= ~TCF_BUSY;	/* 07/22/98 */
	}			/* for */

	return -1;
}

static void initio_select_atn_stop(struct initio_host * host, struct scsi_ctrl_blk * scb)
{
	scb->status |= SCB_SELECT;
	scb->next_state = 0x1;
	host->active = scb;
	host->active_tc = &host->targets[scb->target];
	outb(TSC_SELATNSTOP, host->addr + TUL_SCmd);
}


static void initio_select_atn(struct initio_host * host, struct scsi_ctrl_blk * scb)
{
	int i;

	scb->status |= SCB_SELECT;
	scb->next_state = 0x2;

	outb(scb->ident, host->addr + TUL_SFifo);
	for (i = 0; i < (int) scb->cdblen; i++)
		outb(scb->cdb[i], host->addr + TUL_SFifo);
	host->active_tc = &host->targets[scb->target];
	host->active = scb;
	outb(TSC_SEL_ATN, host->addr + TUL_SCmd);
}

static void initio_select_atn3(struct initio_host * host, struct scsi_ctrl_blk * scb)
{
	int i;

	scb->status |= SCB_SELECT;
	scb->next_state = 0x2;

	outb(scb->ident, host->addr + TUL_SFifo);
	outb(scb->tagmsg, host->addr + TUL_SFifo);
	outb(scb->tagid, host->addr + TUL_SFifo);
	for (i = 0; i < scb->cdblen; i++)
		outb(scb->cdb[i], host->addr + TUL_SFifo);
	host->active_tc = &host->targets[scb->target];
	host->active = scb;
	outb(TSC_SEL_ATN3, host->addr + TUL_SCmd);
}

/**
 *	initio_bus_device_reset	-	 SCSI Bus Device Reset
 *	@host: InitIO host to reset
 *
 *	Perform a device reset and abort all pending SCBs for the
 *	victim device
 */
int initio_bus_device_reset(struct initio_host * host)
{
	struct scsi_ctrl_blk *scb = host->active;
	struct target_control *active_tc = host->active_tc;
	struct scsi_ctrl_blk *tmp, *prev;
	u8 tar;

	if (host->phase != MSG_OUT)
		return int_initio_bad_seq(host);	/* Unexpected phase */

	initio_unlink_pend_scb(host, scb);
	initio_release_scb(host, scb);


	tar = scb->target;	/* target                       */
	active_tc->flags &= ~(TCF_SYNC_DONE | TCF_WDTR_DONE | TCF_BUSY);
	/* clr sync. nego & WDTR flags  07/22/98 */

	/* abort all SCB with same target */
	prev = tmp = host->first_busy;	/* Check Busy queue */
	while (tmp != NULL) {
		if (tmp->target == tar) {
			/* unlink it */
			if (tmp == host->first_busy) {
				if ((host->first_busy = tmp->next) == NULL)
					host->last_busy = NULL;
			} else {
				prev->next = tmp->next;
				if (tmp == host->last_busy)
					host->last_busy = prev;
			}
			tmp->hastat = HOST_ABORTED;
			initio_append_done_scb(host, tmp);
		}
		/* Previous haven't change      */
		else {
			prev = tmp;
		}
		tmp = tmp->next;
	}
	outb(MSG_DEVRST, host->addr + TUL_SFifo);
	outb(TSC_XF_FIFO_OUT, host->addr + TUL_SCmd);
	return initio_wait_disc(host);

}

static int initio_msgin_accept(struct initio_host * host)
{
	outb(TSC_MSG_ACCEPT, host->addr + TUL_SCmd);
	return wait_tulip(host);
}

static int wait_tulip(struct initio_host * host)
{

	while (!((host->jsstatus0 = inb(host->addr + TUL_SStatus0))
		 & TSS_INT_PENDING))
			cpu_relax();

	host->jsint = inb(host->addr + TUL_SInt);
	host->phase = host->jsstatus0 & TSS_PH_MASK;
	host->jsstatus1 = inb(host->addr + TUL_SStatus1);

	if (host->jsint & TSS_RESEL_INT)	/* if SCSI bus reset detected */
		return int_initio_resel(host);
	if (host->jsint & TSS_SEL_TIMEOUT)	/* if selected/reselected timeout interrupt */
		return int_initio_busfree(host);
	if (host->jsint & TSS_SCSIRST_INT)	/* if SCSI bus reset detected   */
		return int_initio_scsi_rst(host);

	if (host->jsint & TSS_DISC_INT) {	/* BUS disconnection            */
		if (host->flags & HCF_EXPECT_DONE_DISC) {
			outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0); /* Flush SCSI FIFO */
			initio_unlink_busy_scb(host, host->active);
			host->active->hastat = 0;
			initio_append_done_scb(host, host->active);
			host->active = NULL;
			host->active_tc = NULL;
			host->flags &= ~HCF_EXPECT_DONE_DISC;
			outb(TSC_INITDEFAULT, host->addr + TUL_SConfig);
			outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);	/* Enable HW reselect */
			return -1;
		}
		if (host->flags & HCF_EXPECT_DISC) {
			outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0); /* Flush SCSI FIFO */
			host->active = NULL;
			host->active_tc = NULL;
			host->flags &= ~HCF_EXPECT_DISC;
			outb(TSC_INITDEFAULT, host->addr + TUL_SConfig);
			outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);	/* Enable HW reselect */
			return -1;
		}
		return int_initio_busfree(host);
	}
	/* The old code really does the below. Can probably be removed */
	if (host->jsint & (TSS_FUNC_COMP | TSS_BUS_SERV))
		return host->phase;
	return host->phase;
}

static int initio_wait_disc(struct initio_host * host)
{
	while (!((host->jsstatus0 = inb(host->addr + TUL_SStatus0)) & TSS_INT_PENDING))
		cpu_relax();

	host->jsint = inb(host->addr + TUL_SInt);

	if (host->jsint & TSS_SCSIRST_INT)	/* if SCSI bus reset detected */
		return int_initio_scsi_rst(host);
	if (host->jsint & TSS_DISC_INT) {	/* BUS disconnection */
		outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0); /* Flush SCSI FIFO */
		outb(TSC_INITDEFAULT, host->addr + TUL_SConfig);
		outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);	/* Enable HW reselect */
		host->active = NULL;
		return -1;
	}
	return initio_bad_seq(host);
}

static int initio_wait_done_disc(struct initio_host * host)
{
	while (!((host->jsstatus0 = inb(host->addr + TUL_SStatus0))
		 & TSS_INT_PENDING))
		 cpu_relax();

	host->jsint = inb(host->addr + TUL_SInt);

	if (host->jsint & TSS_SCSIRST_INT)	/* if SCSI bus reset detected */
		return int_initio_scsi_rst(host);
	if (host->jsint & TSS_DISC_INT) {	/* BUS disconnection */
		outb(TSC_FLUSH_FIFO, host->addr + TUL_SCtrl0);		/* Flush SCSI FIFO */
		outb(TSC_INITDEFAULT, host->addr + TUL_SConfig);
		outb(TSC_HW_RESELECT, host->addr + TUL_SCtrl1);		/* Enable HW reselect */
		initio_unlink_busy_scb(host, host->active);

		initio_append_done_scb(host, host->active);
		host->active = NULL;
		return -1;
	}
	return initio_bad_seq(host);
}

/**
 *	i91u_intr		-	IRQ handler
 *	@irqno: IRQ number
 *	@dev_id: IRQ identifier
 *
 *	Take the relevant locks and then invoke the actual isr processing
 *	code under the lock.
 */

static irqreturn_t i91u_intr(int irqno, void *dev_id)
{
	struct Scsi_Host *dev = dev_id;
	unsigned long flags;
	int r;
	
	spin_lock_irqsave(dev->host_lock, flags);
	r = initio_isr((struct initio_host *)dev->hostdata);
	spin_unlock_irqrestore(dev->host_lock, flags);
	if (r)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}


/**
 *	initio_build_scb		-	Build the mappings and SCB
 *	@host: InitIO host taking the command
 *	@cblk: Firmware command block
 *	@cmnd: SCSI midlayer command block
 *
 *	Translate the abstract SCSI command into a firmware command block
 *	suitable for feeding to the InitIO host controller. This also requires
 *	we build the scatter gather lists and ensure they are mapped properly.
 */

static void initio_build_scb(struct initio_host * host, struct scsi_ctrl_blk * cblk, struct scsi_cmnd * cmnd)
{				/* Create corresponding SCB     */
	struct scatterlist *sglist;
	struct sg_entry *sg;		/* Pointer to SG list           */
	int i, nseg;
	long total_len;
	dma_addr_t dma_addr;

	/* Fill in the command headers */
	cblk->post = i91uSCBPost;	/* i91u's callback routine      */
	cblk->srb = cmnd;
	cblk->opcode = ExecSCSI;
	cblk->flags = SCF_POST;	/* After SCSI done, call post routine */
	cblk->target = cmnd->device->id;
	cblk->lun = cmnd->device->lun;
	cblk->ident = cmnd->device->lun | DISC_ALLOW;

	cblk->flags |= SCF_SENSE;	/* Turn on auto request sense   */

	/* Map the sense buffer into bus memory */
	dma_addr = dma_map_single(&host->pci_dev->dev, cmnd->sense_buffer,
				  SENSE_SIZE, DMA_FROM_DEVICE);
	cblk->senseptr = (u32)dma_addr;
	cblk->senselen = SENSE_SIZE;
	cmnd->SCp.ptr = (char *)(unsigned long)dma_addr;
	cblk->cdblen = cmnd->cmd_len;

	/* Clear the returned status */
	cblk->hastat = 0;
	cblk->tastat = 0;
	/* Command the command */
	memcpy(cblk->cdb, cmnd->cmnd, cmnd->cmd_len);

	/* Set up tags */
	if (cmnd->device->tagged_supported) {	/* Tag Support                  */
		cblk->tagmsg = SIMPLE_QUEUE_TAG;	/* Do simple tag only   */
	} else {
		cblk->tagmsg = 0;	/* No tag support               */
	}

	/* todo handle map_sg error */
	nseg = scsi_dma_map(cmnd);
	BUG_ON(nseg < 0);
	if (nseg) {
		dma_addr = dma_map_single(&host->pci_dev->dev, &cblk->sglist[0],
					  sizeof(struct sg_entry) * TOTAL_SG_ENTRY,
					  DMA_BIDIRECTIONAL);
		cblk->bufptr = (u32)dma_addr;
		cmnd->SCp.dma_handle = dma_addr;

		cblk->sglen = nseg;

		cblk->flags |= SCF_SG;	/* Turn on SG list flag       */
		total_len = 0;
		sg = &cblk->sglist[0];
		scsi_for_each_sg(cmnd, sglist, cblk->sglen, i) {
			sg->data = cpu_to_le32((u32)sg_dma_address(sglist));
			sg->len = cpu_to_le32((u32)sg_dma_len(sglist));
			total_len += sg_dma_len(sglist);
			++sg;
		}

		cblk->buflen = (scsi_bufflen(cmnd) > total_len) ?
			total_len : scsi_bufflen(cmnd);
	} else {	/* No data transfer required */
		cblk->buflen = 0;
		cblk->sglen = 0;
	}
}

/**
 *	i91u_queuecommand	-	Queue a new command if possible
 *	@cmd: SCSI command block from the mid layer
 *	@done: Completion handler
 *
 *	Attempts to queue a new command with the host adapter. Will return
 *	zero if successful or indicate a host busy condition if not (which
 *	will cause the mid layer to call us again later with the command)
 */

static int i91u_queuecommand_lck(struct scsi_cmnd *cmd,
		void (*done)(struct scsi_cmnd *))
{
	struct initio_host *host = (struct initio_host *) cmd->device->host->hostdata;
	struct scsi_ctrl_blk *cmnd;

	cmd->scsi_done = done;

	cmnd = initio_alloc_scb(host);
	if (!cmnd)
		return SCSI_MLQUEUE_HOST_BUSY;

	initio_build_scb(host, cmnd, cmd);
	initio_exec_scb(host, cmnd);
	return 0;
}

static DEF_SCSI_QCMD(i91u_queuecommand)

/**
 *	i91u_bus_reset		-	reset the SCSI bus
 *	@cmnd: Command block we want to trigger the reset for
 *
 *	Initiate a SCSI bus reset sequence
 */

static int i91u_bus_reset(struct scsi_cmnd * cmnd)
{
	struct initio_host *host;

	host = (struct initio_host *) cmnd->device->host->hostdata;

	spin_lock_irq(cmnd->device->host->host_lock);
	initio_reset_scsi(host, 0);
	spin_unlock_irq(cmnd->device->host->host_lock);

	return SUCCESS;
}

/**
 *	i91u_biospararm			-	return the "logical geometry
 *	@sdev: SCSI device
 *	@dev; Matching block device
 *	@capacity: Sector size of drive
 *	@info_array: Return space for BIOS geometry
 *
 *	Map the device geometry in a manner compatible with the host
 *	controller BIOS behaviour.
 *
 *	FIXME: limited to 2^32 sector devices.
 */

static int i91u_biosparam(struct scsi_device *sdev, struct block_device *dev,
		sector_t capacity, int *info_array)
{
	struct initio_host *host;		/* Point to Host adapter control block */
	struct target_control *tc;

	host = (struct initio_host *) sdev->host->hostdata;
	tc = &host->targets[sdev->id];

	if (tc->heads) {
		info_array[0] = tc->heads;
		info_array[1] = tc->sectors;
		info_array[2] = (unsigned long)capacity / tc->heads / tc->sectors;
	} else {
		if (tc->drv_flags & TCF_DRV_255_63) {
			info_array[0] = 255;
			info_array[1] = 63;
			info_array[2] = (unsigned long)capacity / 255 / 63;
		} else {
			info_array[0] = 64;
			info_array[1] = 32;
			info_array[2] = (unsigned long)capacity >> 11;
		}
	}

#if defined(DEBUG_BIOSPARAM)
	if (i91u_debug & debug_biosparam) {
		printk("bios geometry: head=%d, sec=%d, cyl=%d\n",
		       info_array[0], info_array[1], info_array[2]);
		printk("WARNING: check, if the bios geometry is correct.\n");
	}
#endif

	return 0;
}

/**
 *	i91u_unmap_scb		-	Unmap a command
 *	@pci_dev: PCI device the command is for
 *	@cmnd: The command itself
 *
 *	Unmap any PCI mapping/IOMMU resources allocated when the command
 *	was mapped originally as part of initio_build_scb
 */

static void i91u_unmap_scb(struct pci_dev *pci_dev, struct scsi_cmnd *cmnd)
{
	/* auto sense buffer */
	if (cmnd->SCp.ptr) {
		dma_unmap_single(&pci_dev->dev,
				 (dma_addr_t)((unsigned long)cmnd->SCp.ptr),
				 SENSE_SIZE, DMA_FROM_DEVICE);
		cmnd->SCp.ptr = NULL;
	}

	/* request buffer */
	if (scsi_sg_count(cmnd)) {
		dma_unmap_single(&pci_dev->dev, cmnd->SCp.dma_handle,
				 sizeof(struct sg_entry) * TOTAL_SG_ENTRY,
				 DMA_BIDIRECTIONAL);

		scsi_dma_unmap(cmnd);
	}
}

/**
 *	i91uSCBPost		-	SCSI callback
 *	@host: Pointer to host adapter control block.
 *	@cmnd: Pointer to SCSI control block.
 *
 *	This is callback routine be called when tulip finish one
 *	SCSI command.
 */

static void i91uSCBPost(u8 * host_mem, u8 * cblk_mem)
{
	struct scsi_cmnd *cmnd;	/* Pointer to SCSI request block */
	struct initio_host *host;
	struct scsi_ctrl_blk *cblk;

	host = (struct initio_host *) host_mem;
	cblk = (struct scsi_ctrl_blk *) cblk_mem;
	if ((cmnd = cblk->srb) == NULL) {
		printk(KERN_ERR "i91uSCBPost: SRB pointer is empty\n");
		WARN_ON(1);
		initio_release_scb(host, cblk);	/* Release SCB for current channel */
		return;
	}

	/*
	 *	Remap the firmware error status into a mid layer one
	 */
	switch (cblk->hastat) {
	case 0x0:
	case 0xa:		/* Linked command complete without error and linked normally */
	case 0xb:		/* Linked command complete without error interrupt generated */
		cblk->hastat = 0;
		break;

	case 0x11:		/* Selection time out-The initiator selection or target
				   reselection was not complete within the SCSI Time out period */
		cblk->hastat = DID_TIME_OUT;
		break;

	case 0x14:		/* Target bus phase sequence failure-An invalid bus phase or bus
				   phase sequence was requested by the target. The host adapter
				   will generate a SCSI Reset Condition, notifying the host with
				   a SCRD interrupt */
		cblk->hastat = DID_RESET;
		break;

	case 0x1a:		/* SCB Aborted. 07/21/98 */
		cblk->hastat = DID_ABORT;
		break;

	case 0x12:		/* Data overrun/underrun-The target attempted to transfer more data
				   than was allocated by the Data Length field or the sum of the
				   Scatter / Gather Data Length fields. */
	case 0x13:		/* Unexpected bus free-The target dropped the SCSI BSY at an unexpected time. */
	case 0x16:		/* Invalid SCB Operation Code. */

	default:
		printk("ini9100u: %x %x\n", cblk->hastat, cblk->tastat);
		cblk->hastat = DID_ERROR;	/* Couldn't find any better */
		break;
	}

	cmnd->result = cblk->tastat | (cblk->hastat << 16);
	i91u_unmap_scb(host->pci_dev, cmnd);
	cmnd->scsi_done(cmnd);	/* Notify system DONE           */
	initio_release_scb(host, cblk);	/* Release SCB for current channel */
}

static struct scsi_host_template initio_template = {
	.proc_name		= "INI9100U",
	.name			= "Initio INI-9X00U/UW SCSI device driver",
	.queuecommand		= i91u_queuecommand,
	.eh_bus_reset_handler	= i91u_bus_reset,
	.bios_param		= i91u_biosparam,
	.can_queue		= MAX_TARGETS * i91u_MAXQUEUE,
	.this_id		= 1,
	.sg_tablesize		= SG_ALL,
	.use_clustering		= ENABLE_CLUSTERING,
};

static int initio_probe_one(struct pci_dev *pdev,
	const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	struct initio_host *host;
	u32 reg;
	u16 bios_seg;
	struct scsi_ctrl_blk *scb, *tmp, *prev = NULL /* silence gcc */;
	int num_scb, i, error;

	error = pci_enable_device(pdev);
	if (error)
		return error;

	pci_read_config_dword(pdev, 0x44, (u32 *) & reg);
	bios_seg = (u16) (reg & 0xFF);
	if (((reg & 0xFF00) >> 8) == 0xFF)
		reg = 0;
	bios_seg = (bios_seg << 8) + ((u16) ((reg & 0xFF00) >> 8));

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		printk(KERN_WARNING  "i91u: Could not set 32 bit DMA mask\n");
		error = -ENODEV;
		goto out_disable_device;
	}
	shost = scsi_host_alloc(&initio_template, sizeof(struct initio_host));
	if (!shost) {
		printk(KERN_WARNING "initio: Could not allocate host structure.\n");
		error = -ENOMEM;
		goto out_disable_device;
	}
	host = (struct initio_host *)shost->hostdata;
	memset(host, 0, sizeof(struct initio_host));
	host->addr = pci_resource_start(pdev, 0);
	host->bios_addr = bios_seg;

	if (!request_region(host->addr, 256, "i91u")) {
		printk(KERN_WARNING "initio: I/O port range 0x%x is busy.\n", host->addr);
		error = -ENODEV;
		goto out_host_put;
	}

	if (initio_tag_enable)	/* 1.01i */
		num_scb = MAX_TARGETS * i91u_MAXQUEUE;
	else
		num_scb = MAX_TARGETS + 3;	/* 1-tape, 1-CD_ROM, 1- extra */

	for (; num_scb >= MAX_TARGETS + 3; num_scb--) {
		i = num_scb * sizeof(struct scsi_ctrl_blk);
		if ((scb = kzalloc(i, GFP_DMA)) != NULL)
			break;
	}

	if (!scb) {
		printk(KERN_WARNING "initio: Cannot allocate SCB array.\n");
		error = -ENOMEM;
		goto out_release_region;
	}

	host->pci_dev = pdev;

	host->semaph = 1;
	spin_lock_init(&host->semaph_lock);
	host->num_scbs = num_scb;
	host->scb = scb;
	host->next_pending = scb;
	host->next_avail = scb;
	for (i = 0, tmp = scb; i < num_scb; i++, tmp++) {
		tmp->tagid = i;
		if (i != 0)
			prev->next = tmp;
		prev = tmp;
	}
	prev->next = NULL;
	host->scb_end = tmp;
	host->first_avail = scb;
	host->last_avail = prev;
	spin_lock_init(&host->avail_lock);

	initio_init(host, phys_to_virt(((u32)bios_seg << 4)));

	host->jsstatus0 = 0;

	shost->io_port = host->addr;
	shost->n_io_port = 0xff;
	shost->can_queue = num_scb;		/* 03/05/98                      */
	shost->unique_id = host->addr;
	shost->max_id = host->max_tar;
	shost->max_lun = 32;	/* 10/21/97                     */
	shost->irq = pdev->irq;
	shost->this_id = host->scsi_id;	/* Assign HCS index           */
	shost->base = host->addr;
	shost->sg_tablesize = TOTAL_SG_ENTRY;

	error = request_irq(pdev->irq, i91u_intr, IRQF_SHARED, "i91u", shost);
	if (error < 0) {
		printk(KERN_WARNING "initio: Unable to request IRQ %d\n", pdev->irq);
		goto out_free_scbs;
	}

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_free_irq;
	scsi_scan_host(shost);
	return 0;
out_free_irq:
	free_irq(pdev->irq, shost);
out_free_scbs:
	kfree(host->scb);
out_release_region:
	release_region(host->addr, 256);
out_host_put:
	scsi_host_put(shost);
out_disable_device:
	pci_disable_device(pdev);
	return error;
}

/**
 *	initio_remove_one	-	control shutdown
 *	@pdev:	PCI device being released
 *
 *	Release the resources assigned to this adapter after it has
 *	finished being used.
 */

static void initio_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	struct initio_host *s = (struct initio_host *)host->hostdata;
	scsi_remove_host(host);
	free_irq(pdev->irq, host);
	release_region(s->addr, 256);
	scsi_host_put(host);
	pci_disable_device(pdev);
}

MODULE_LICENSE("GPL");

static struct pci_device_id initio_pci_tbl[] = {
	{PCI_VENDOR_ID_INIT, 0x9500, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_INIT, 0x9400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_INIT, 0x9401, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_INIT, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_DOMEX, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};
MODULE_DEVICE_TABLE(pci, initio_pci_tbl);

static struct pci_driver initio_pci_driver = {
	.name		= "initio",
	.id_table	= initio_pci_tbl,
	.probe		= initio_probe_one,
	.remove		= initio_remove_one,
};

static int __init initio_init_driver(void)
{
	return pci_register_driver(&initio_pci_driver);
}

static void __exit initio_exit_driver(void)
{
	pci_unregister_driver(&initio_pci_driver);
}

MODULE_DESCRIPTION("Initio INI-9X00U/UW SCSI device driver");
MODULE_AUTHOR("Initio Corporation");
MODULE_LICENSE("GPL");

module_init(initio_init_driver);
module_exit(initio_exit_driver);
