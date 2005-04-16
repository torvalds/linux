/*
 * Initio A100 device driver for Linux.
 *
 * Copyright (c) 1994-1998 Initio Corporation
 * Copyright (c) 2003-2004 Christoph Hellwig
 * All rights reserved.
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
 * --------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU General Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Revision History:
 * 07/02/98 hl	- v.91n Initial drivers.
 * 09/14/98 hl - v1.01 Support new Kernel.
 * 09/22/98 hl - v1.01a Support reset.
 * 09/24/98 hl - v1.01b Fixed reset.
 * 10/05/98 hl - v1.02 split the source code and release.
 * 12/19/98 bv - v1.02a Use spinlocks for 2.1.95 and up
 * 01/31/99 bv - v1.02b Use mdelay instead of waitForPause
 * 08/08/99 bv - v1.02c Use waitForPause again.
 * 06/25/02 Doug Ledford <dledford@redhat.com> - v1.02d
 *          - Remove limit on number of controllers
 *          - Port to DMA mapping API
 *          - Clean up interrupt handler registration
 *          - Fix memory leaks
 *          - Fix allocation of scsi host structs and private data
 * 11/18/03 Christoph Hellwig <hch@lst.de>
 *	    - Port to new probing API
 *	    - Fix some more leaks in init failure cases
 * 9/28/04 Christoph Hellwig <hch@lst.de>
 *	    - merge the two source files
 *	    - remove internal queueing code
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "a100u2w.h"


#define JIFFIES_TO_MS(t) ((t) * 1000 / HZ)
#define MS_TO_JIFFIES(j) ((j * HZ) / 1000)

static ORC_SCB *orc_alloc_scb(ORC_HCS * hcsp);
static void inia100SCBPost(BYTE * pHcb, BYTE * pScb);

static NVRAM nvram, *nvramp = &nvram;
static UCHAR dftNvRam[64] =
{
/*----------header -------------*/
	0x01,			/* 0x00: Sub System Vendor ID 0 */
	0x11,			/* 0x01: Sub System Vendor ID 1 */
	0x60,			/* 0x02: Sub System ID 0        */
	0x10,			/* 0x03: Sub System ID 1        */
	0x00,			/* 0x04: SubClass               */
	0x01,			/* 0x05: Vendor ID 0            */
	0x11,			/* 0x06: Vendor ID 1            */
	0x60,			/* 0x07: Device ID 0            */
	0x10,			/* 0x08: Device ID 1            */
	0x00,			/* 0x09: Reserved               */
	0x00,			/* 0x0A: Reserved               */
	0x01,			/* 0x0B: Revision of Data Structure     */
				/* -- Host Adapter Structure --- */
	0x01,			/* 0x0C: Number Of SCSI Channel */
	0x01,			/* 0x0D: BIOS Configuration 1   */
	0x00,			/* 0x0E: BIOS Configuration 2   */
	0x00,			/* 0x0F: BIOS Configuration 3   */
				/* --- SCSI Channel 0 Configuration --- */
	0x07,			/* 0x10: H/A ID                 */
	0x83,			/* 0x11: Channel Configuration  */
	0x20,			/* 0x12: MAX TAG per target     */
	0x0A,			/* 0x13: SCSI Reset Recovering time     */
	0x00,			/* 0x14: Channel Configuration4 */
	0x00,			/* 0x15: Channel Configuration5 */
				/* SCSI Channel 0 Target Configuration  */
				/* 0x16-0x25                    */
	0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
	0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
				/* --- SCSI Channel 1 Configuration --- */
	0x07,			/* 0x26: H/A ID                 */
	0x83,			/* 0x27: Channel Configuration  */
	0x20,			/* 0x28: MAX TAG per target     */
	0x0A,			/* 0x29: SCSI Reset Recovering time     */
	0x00,			/* 0x2A: Channel Configuration4 */
	0x00,			/* 0x2B: Channel Configuration5 */
				/* SCSI Channel 1 Target Configuration  */
				/* 0x2C-0x3B                    */
	0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
	0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
	0x00,			/* 0x3C: Reserved               */
	0x00,			/* 0x3D: Reserved               */
	0x00,			/* 0x3E: Reserved               */
	0x00			/* 0x3F: Checksum               */
};


/***************************************************************************/
static void waitForPause(unsigned amount)
{
	ULONG the_time = jiffies + MS_TO_JIFFIES(amount);
	while (time_before_eq(jiffies, the_time))
		cpu_relax();
}

/***************************************************************************/
static UCHAR waitChipReady(ORC_HCS * hcsp)
{
	int i;

	for (i = 0; i < 10; i++) {	/* Wait 1 second for report timeout     */
		if (ORC_RD(hcsp->HCS_Base, ORC_HCTRL) & HOSTSTOP)	/* Wait HOSTSTOP set */
			return 1;
		waitForPause(100);	/* wait 100ms before try again  */
	}
	return 0;
}

/***************************************************************************/
static UCHAR waitFWReady(ORC_HCS * hcsp)
{
	int i;

	for (i = 0; i < 10; i++) {	/* Wait 1 second for report timeout     */
		if (ORC_RD(hcsp->HCS_Base, ORC_HSTUS) & RREADY)		/* Wait READY set */
			return 1;
		waitForPause(100);	/* wait 100ms before try again  */
	}
	return 0;
}

/***************************************************************************/
static UCHAR waitSCSIRSTdone(ORC_HCS * hcsp)
{
	int i;

	for (i = 0; i < 10; i++) {	/* Wait 1 second for report timeout     */
		if (!(ORC_RD(hcsp->HCS_Base, ORC_HCTRL) & SCSIRST))	/* Wait SCSIRST done */
			return 1;
		waitForPause(100);	/* wait 100ms before try again  */
	}
	return 0;
}

/***************************************************************************/
static UCHAR waitHDOoff(ORC_HCS * hcsp)
{
	int i;

	for (i = 0; i < 10; i++) {	/* Wait 1 second for report timeout     */
		if (!(ORC_RD(hcsp->HCS_Base, ORC_HCTRL) & HDO))		/* Wait HDO off */
			return 1;
		waitForPause(100);	/* wait 100ms before try again  */
	}
	return 0;
}

/***************************************************************************/
static UCHAR waitHDIset(ORC_HCS * hcsp, UCHAR * pData)
{
	int i;

	for (i = 0; i < 10; i++) {	/* Wait 1 second for report timeout     */
		if ((*pData = ORC_RD(hcsp->HCS_Base, ORC_HSTUS)) & HDI)
			return 1;	/* Wait HDI set */
		waitForPause(100);	/* wait 100ms before try again  */
	}
	return 0;
}

/***************************************************************************/
static unsigned short get_FW_version(ORC_HCS * hcsp)
{
	UCHAR bData;
	union {
		unsigned short sVersion;
		unsigned char cVersion[2];
	} Version;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, ORC_CMD_VERSION);
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	if (waitHDIset(hcsp, &bData) == 0)	/* Wait HDI set   */
		return 0;
	Version.cVersion[0] = ORC_RD(hcsp->HCS_Base, ORC_HDATA);
	ORC_WR(hcsp->HCS_Base + ORC_HSTUS, bData);	/* Clear HDI            */

	if (waitHDIset(hcsp, &bData) == 0)	/* Wait HDI set   */
		return 0;
	Version.cVersion[1] = ORC_RD(hcsp->HCS_Base, ORC_HDATA);
	ORC_WR(hcsp->HCS_Base + ORC_HSTUS, bData);	/* Clear HDI            */

	return (Version.sVersion);
}

/***************************************************************************/
static UCHAR set_NVRAM(ORC_HCS * hcsp, unsigned char address, unsigned char value)
{
	ORC_WR(hcsp->HCS_Base + ORC_HDATA, ORC_CMD_SET_NVM);	/* Write command */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, address);	/* Write address */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, value);	/* Write value  */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	return 1;
}

/***************************************************************************/
static UCHAR get_NVRAM(ORC_HCS * hcsp, unsigned char address, unsigned char *pDataIn)
{
	unsigned char bData;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, ORC_CMD_GET_NVM);	/* Write command */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, address);	/* Write address */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	if (waitHDIset(hcsp, &bData) == 0)	/* Wait HDI set   */
		return 0;
	*pDataIn = ORC_RD(hcsp->HCS_Base, ORC_HDATA);
	ORC_WR(hcsp->HCS_Base + ORC_HSTUS, bData);	/* Clear HDI    */

	return 1;
}

/***************************************************************************/
static void orc_exec_scb(ORC_HCS * hcsp, ORC_SCB * scbp)
{
	scbp->SCB_Status = ORCSCB_POST;
	ORC_WR(hcsp->HCS_Base + ORC_PQUEUE, scbp->SCB_ScbIdx);
	return;
}


/***********************************************************************
 Read SCSI H/A configuration parameters from serial EEPROM
************************************************************************/
static int se2_rd_all(ORC_HCS * hcsp)
{
	int i;
	UCHAR *np, chksum = 0;

	np = (UCHAR *) nvramp;
	for (i = 0; i < 64; i++, np++) {	/* <01> */
		if (get_NVRAM(hcsp, (unsigned char) i, np) == 0)
			return -1;
//      *np++ = get_NVRAM(hcsp, (unsigned char ) i);
	}

/*------ Is ckecksum ok ? ------*/
	np = (UCHAR *) nvramp;
	for (i = 0; i < 63; i++)
		chksum += *np++;

	if (nvramp->CheckSum != (UCHAR) chksum)
		return -1;
	return 1;
}

/************************************************************************
 Update SCSI H/A configuration parameters from serial EEPROM
*************************************************************************/
static void se2_update_all(ORC_HCS * hcsp)
{				/* setup default pattern  */
	int i;
	UCHAR *np, *np1, chksum = 0;

	/* Calculate checksum first   */
	np = (UCHAR *) dftNvRam;
	for (i = 0; i < 63; i++)
		chksum += *np++;
	*np = chksum;

	np = (UCHAR *) dftNvRam;
	np1 = (UCHAR *) nvramp;
	for (i = 0; i < 64; i++, np++, np1++) {
		if (*np != *np1) {
			set_NVRAM(hcsp, (unsigned char) i, *np);
		}
	}
	return;
}

/*************************************************************************
 Function name  : read_eeprom
**************************************************************************/
static void read_eeprom(ORC_HCS * hcsp)
{
	if (se2_rd_all(hcsp) != 1) {
		se2_update_all(hcsp);	/* setup default pattern        */
		se2_rd_all(hcsp);	/* load again                   */
	}
}


/***************************************************************************/
static UCHAR load_FW(ORC_HCS * hcsp)
{
	U32 dData;
	USHORT wBIOSAddress;
	USHORT i;
	UCHAR *pData, bData;


	bData = ORC_RD(hcsp->HCS_Base, ORC_GCFG);
	ORC_WR(hcsp->HCS_Base + ORC_GCFG, bData | EEPRG);	/* Enable EEPROM programming */
	ORC_WR(hcsp->HCS_Base + ORC_EBIOSADR2, 0x00);
	ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, 0x00);
	if (ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA) != 0x55) {
		ORC_WR(hcsp->HCS_Base + ORC_GCFG, bData);	/* Disable EEPROM programming */
		return 0;
	}
	ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, 0x01);
	if (ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA) != 0xAA) {
		ORC_WR(hcsp->HCS_Base + ORC_GCFG, bData);	/* Disable EEPROM programming */
		return 0;
	}
	ORC_WR(hcsp->HCS_Base + ORC_RISCCTL, PRGMRST | DOWNLOAD);	/* Enable SRAM programming */
	pData = (UCHAR *) & dData;
	dData = 0;		/* Initial FW address to 0 */
	ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, 0x10);
	*pData = ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA);		/* Read from BIOS */
	ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, 0x11);
	*(pData + 1) = ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA);	/* Read from BIOS */
	ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, 0x12);
	*(pData + 2) = ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA);	/* Read from BIOS */
	ORC_WR(hcsp->HCS_Base + ORC_EBIOSADR2, *(pData + 2));
	ORC_WRLONG(hcsp->HCS_Base + ORC_FWBASEADR, dData);	/* Write FW address */

	wBIOSAddress = (USHORT) dData;	/* FW code locate at BIOS address + ? */
	for (i = 0, pData = (UCHAR *) & dData;	/* Download the code    */
	     i < 0x1000;	/* Firmware code size = 4K      */
	     i++, wBIOSAddress++) {
		ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, wBIOSAddress);
		*pData++ = ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA);	/* Read from BIOS */
		if ((i % 4) == 3) {
			ORC_WRLONG(hcsp->HCS_Base + ORC_RISCRAM, dData);	/* Write every 4 bytes */
			pData = (UCHAR *) & dData;
		}
	}

	ORC_WR(hcsp->HCS_Base + ORC_RISCCTL, PRGMRST | DOWNLOAD);	/* Reset program count 0 */
	wBIOSAddress -= 0x1000;	/* Reset the BIOS adddress      */
	for (i = 0, pData = (UCHAR *) & dData;	/* Check the code       */
	     i < 0x1000;	/* Firmware code size = 4K      */
	     i++, wBIOSAddress++) {
		ORC_WRSHORT(hcsp->HCS_Base + ORC_EBIOSADR0, wBIOSAddress);
		*pData++ = ORC_RD(hcsp->HCS_Base, ORC_EBIOSDATA);	/* Read from BIOS */
		if ((i % 4) == 3) {
			if (ORC_RDLONG(hcsp->HCS_Base, ORC_RISCRAM) != dData) {
				ORC_WR(hcsp->HCS_Base + ORC_RISCCTL, PRGMRST);	/* Reset program to 0 */
				ORC_WR(hcsp->HCS_Base + ORC_GCFG, bData);	/*Disable EEPROM programming */
				return 0;
			}
			pData = (UCHAR *) & dData;
		}
	}
	ORC_WR(hcsp->HCS_Base + ORC_RISCCTL, PRGMRST);	/* Reset program to 0   */
	ORC_WR(hcsp->HCS_Base + ORC_GCFG, bData);	/* Disable EEPROM programming */
	return 1;
}

/***************************************************************************/
static void setup_SCBs(ORC_HCS * hcsp)
{
	ORC_SCB *pVirScb;
	int i;
	ESCB *pVirEscb;
	dma_addr_t pPhysEscb;

	/* Setup SCB HCS_Base and SCB Size registers */
	ORC_WR(hcsp->HCS_Base + ORC_SCBSIZE, ORC_MAXQUEUE);	/* Total number of SCBs */
	/* SCB HCS_Base address 0      */
	ORC_WRLONG(hcsp->HCS_Base + ORC_SCBBASE0, hcsp->HCS_physScbArray);
	/* SCB HCS_Base address 1      */
	ORC_WRLONG(hcsp->HCS_Base + ORC_SCBBASE1, hcsp->HCS_physScbArray);

	/* setup scatter list address with one buffer */
	pVirScb = hcsp->HCS_virScbArray;
	pVirEscb = hcsp->HCS_virEscbArray;

	for (i = 0; i < ORC_MAXQUEUE; i++) {
		pPhysEscb = (hcsp->HCS_physEscbArray + (sizeof(ESCB) * i));
		pVirScb->SCB_SGPAddr = (U32) pPhysEscb;
		pVirScb->SCB_SensePAddr = (U32) pPhysEscb;
		pVirScb->SCB_EScb = pVirEscb;
		pVirScb->SCB_ScbIdx = i;
		pVirScb++;
		pVirEscb++;
	}

	return;
}

/***************************************************************************/
static void initAFlag(ORC_HCS * hcsp)
{
	UCHAR i, j;

	for (i = 0; i < MAX_CHANNELS; i++) {
		for (j = 0; j < 8; j++) {
			hcsp->BitAllocFlag[i][j] = 0xffffffff;
		}
	}
}

/***************************************************************************/
static int init_orchid(ORC_HCS * hcsp)
{
	UBYTE *readBytep;
	USHORT revision;
	UCHAR i;

	initAFlag(hcsp);
	ORC_WR(hcsp->HCS_Base + ORC_GIMSK, 0xFF);	/* Disable all interrupt        */
	if (ORC_RD(hcsp->HCS_Base, ORC_HSTUS) & RREADY) {	/* Orchid is ready              */
		revision = get_FW_version(hcsp);
		if (revision == 0xFFFF) {
			ORC_WR(hcsp->HCS_Base + ORC_HCTRL, DEVRST);	/* Reset Host Adapter   */
			if (waitChipReady(hcsp) == 0)
				return (-1);
			load_FW(hcsp);	/* Download FW                  */
			setup_SCBs(hcsp);	/* Setup SCB HCS_Base and SCB Size registers */
			ORC_WR(hcsp->HCS_Base + ORC_HCTRL, 0);	/* clear HOSTSTOP       */
			if (waitFWReady(hcsp) == 0)
				return (-1);
			/* Wait for firmware ready     */
		} else {
			setup_SCBs(hcsp);	/* Setup SCB HCS_Base and SCB Size registers */
		}
	} else {		/* Orchid is not Ready          */
		ORC_WR(hcsp->HCS_Base + ORC_HCTRL, DEVRST);	/* Reset Host Adapter   */
		if (waitChipReady(hcsp) == 0)
			return (-1);
		load_FW(hcsp);	/* Download FW                  */
		setup_SCBs(hcsp);	/* Setup SCB HCS_Base and SCB Size registers */
		ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);	/* Do Hardware Reset &  */

		/*     clear HOSTSTOP  */
		if (waitFWReady(hcsp) == 0)		/* Wait for firmware ready      */
			return (-1);
	}

/*------------- get serial EEProm settting -------*/

	read_eeprom(hcsp);

	if (nvramp->Revision != 1)
		return (-1);

	hcsp->HCS_SCSI_ID = nvramp->SCSI0Id;
	hcsp->HCS_BIOS = nvramp->BIOSConfig1;
	hcsp->HCS_MaxTar = MAX_TARGETS;
	readBytep = (UCHAR *) & (nvramp->Target00Config);
	for (i = 0; i < 16; readBytep++, i++) {
		hcsp->TargetFlag[i] = *readBytep;
		hcsp->MaximumTags[i] = ORC_MAXTAGS;
	}			/* for                          */

	if (nvramp->SCSI0Config & NCC_BUSRESET) {	/* Reset SCSI bus               */
		hcsp->HCS_Flags |= HCF_SCSI_RESET;
	}
	ORC_WR(hcsp->HCS_Base + ORC_GIMSK, 0xFB);	/* enable RP FIFO interrupt     */
	return (0);
}

/*****************************************************************************
 Function name  : orc_reset_scsi_bus
 Description    : Reset registers, reset a hanging bus and
                  kill active and disconnected commands for target w/o soft reset
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int orc_reset_scsi_bus(ORC_HCS * pHCB)
{				/* I need Host Control Block Information */
	ULONG flags;

	spin_lock_irqsave(&(pHCB->BitAllocFlagLock), flags);

	initAFlag(pHCB);
	/* reset scsi bus */
	ORC_WR(pHCB->HCS_Base + ORC_HCTRL, SCSIRST);
	if (waitSCSIRSTdone(pHCB) == 0) {
		spin_unlock_irqrestore(&(pHCB->BitAllocFlagLock), flags);
		return FAILED;
	} else {
		spin_unlock_irqrestore(&(pHCB->BitAllocFlagLock), flags);
		return SUCCESS;
	}
}

/*****************************************************************************
 Function name  : orc_device_reset
 Description    : Reset registers, reset a hanging bus and
                  kill active and disconnected commands for target w/o soft reset
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int orc_device_reset(ORC_HCS * pHCB, struct scsi_cmnd *SCpnt, unsigned int target)
{				/* I need Host Control Block Information */
	ORC_SCB *pScb;
	ESCB *pVirEscb;
	ORC_SCB *pVirScb;
	UCHAR i;
	ULONG flags;

	spin_lock_irqsave(&(pHCB->BitAllocFlagLock), flags);
	pScb = (ORC_SCB *) NULL;
	pVirEscb = (ESCB *) NULL;

	/* setup scatter list address with one buffer */
	pVirScb = pHCB->HCS_virScbArray;

	initAFlag(pHCB);
	/* device reset */
	for (i = 0; i < ORC_MAXQUEUE; i++) {
		pVirEscb = pVirScb->SCB_EScb;
		if ((pVirScb->SCB_Status) && (pVirEscb->SCB_Srb == SCpnt))
			break;
		pVirScb++;
	}

	if (i == ORC_MAXQUEUE) {
		printk("Unable to Reset - No SCB Found\n");
		spin_unlock_irqrestore(&(pHCB->BitAllocFlagLock), flags);
		return FAILED;
	}
	if ((pScb = orc_alloc_scb(pHCB)) == NULL) {
		spin_unlock_irqrestore(&(pHCB->BitAllocFlagLock), flags);
		return FAILED;
	}
	pScb->SCB_Opcode = ORC_BUSDEVRST;
	pScb->SCB_Target = target;
	pScb->SCB_HaStat = 0;
	pScb->SCB_TaStat = 0;
	pScb->SCB_Status = 0x0;
	pScb->SCB_Link = 0xFF;
	pScb->SCB_Reserved0 = 0;
	pScb->SCB_Reserved1 = 0;
	pScb->SCB_XferLen = 0;
	pScb->SCB_SGLen = 0;

	pVirEscb->SCB_Srb = NULL;
	pVirEscb->SCB_Srb = SCpnt;
	orc_exec_scb(pHCB, pScb);	/* Start execute SCB            */
	spin_unlock_irqrestore(&(pHCB->BitAllocFlagLock), flags);
	return SUCCESS;
}


/***************************************************************************/
static ORC_SCB *__orc_alloc_scb(ORC_HCS * hcsp)
{
	ORC_SCB *pTmpScb;
	UCHAR Ch;
	ULONG idx;
	UCHAR index;
	UCHAR i;

	Ch = hcsp->HCS_Index;
	for (i = 0; i < 8; i++) {
		for (index = 0; index < 32; index++) {
			if ((hcsp->BitAllocFlag[Ch][i] >> index) & 0x01) {
				hcsp->BitAllocFlag[Ch][i] &= ~(1 << index);
				break;
			}
		}
		idx = index + 32 * i;
		pTmpScb = (ORC_SCB *) ((ULONG) hcsp->HCS_virScbArray + (idx * sizeof(ORC_SCB)));
		return (pTmpScb);
	}
	return (NULL);
}

static ORC_SCB *orc_alloc_scb(ORC_HCS * hcsp)
{
	ORC_SCB *pTmpScb;
	ULONG flags;

	spin_lock_irqsave(&(hcsp->BitAllocFlagLock), flags);
	pTmpScb = __orc_alloc_scb(hcsp);
	spin_unlock_irqrestore(&(hcsp->BitAllocFlagLock), flags);
	return (pTmpScb);
}


/***************************************************************************/
static void orc_release_scb(ORC_HCS * hcsp, ORC_SCB * scbp)
{
	ULONG flags;
	UCHAR Index;
	UCHAR i;
	UCHAR Ch;

	spin_lock_irqsave(&(hcsp->BitAllocFlagLock), flags);
	Ch = hcsp->HCS_Index;
	Index = scbp->SCB_ScbIdx;
	i = Index / 32;
	Index %= 32;
	hcsp->BitAllocFlag[Ch][i] |= (1 << Index);
	spin_unlock_irqrestore(&(hcsp->BitAllocFlagLock), flags);
}

/*****************************************************************************
 Function name  : abort_SCB
 Description    : Abort a queued command.
	                 (commands that are on the bus can't be aborted easily)
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int abort_SCB(ORC_HCS * hcsp, ORC_SCB * pScb)
{
	unsigned char bData, bStatus;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, ORC_CMD_ABORT_SCB);	/* Write command */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	ORC_WR(hcsp->HCS_Base + ORC_HDATA, pScb->SCB_ScbIdx);	/* Write address */
	ORC_WR(hcsp->HCS_Base + ORC_HCTRL, HDO);
	if (waitHDOoff(hcsp) == 0)	/* Wait HDO off   */
		return 0;

	if (waitHDIset(hcsp, &bData) == 0)	/* Wait HDI set   */
		return 0;
	bStatus = ORC_RD(hcsp->HCS_Base, ORC_HDATA);
	ORC_WR(hcsp->HCS_Base + ORC_HSTUS, bData);	/* Clear HDI    */

	if (bStatus == 1)	/* 0 - Successfully               */
		return 0;	/* 1 - Fail                     */
	return 1;
}

/*****************************************************************************
 Function name  : inia100_abort
 Description    : Abort a queued command.
	                 (commands that are on the bus can't be aborted easily)
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int orc_abort_srb(ORC_HCS * hcsp, struct scsi_cmnd *SCpnt)
{
	ESCB *pVirEscb;
	ORC_SCB *pVirScb;
	UCHAR i;
	ULONG flags;

	spin_lock_irqsave(&(hcsp->BitAllocFlagLock), flags);

	pVirScb = hcsp->HCS_virScbArray;

	for (i = 0; i < ORC_MAXQUEUE; i++, pVirScb++) {
		pVirEscb = pVirScb->SCB_EScb;
		if ((pVirScb->SCB_Status) && (pVirEscb->SCB_Srb == SCpnt)) {
			if (pVirScb->SCB_TagMsg == 0) {
				spin_unlock_irqrestore(&(hcsp->BitAllocFlagLock), flags);
				return FAILED;
			} else {
				if (abort_SCB(hcsp, pVirScb)) {
					pVirEscb->SCB_Srb = NULL;
					spin_unlock_irqrestore(&(hcsp->BitAllocFlagLock), flags);
					return SUCCESS;
				} else {
					spin_unlock_irqrestore(&(hcsp->BitAllocFlagLock), flags);
					return FAILED;
				}
			}
		}
	}
	spin_unlock_irqrestore(&(hcsp->BitAllocFlagLock), flags);
	return FAILED;
}

/***********************************************************************
 Routine Description:
	  This is the interrupt service routine for the Orchid SCSI adapter.
	  It reads the interrupt register to determine if the adapter is indeed
	  the source of the interrupt and clears the interrupt at the device.
 Arguments:
	  HwDeviceExtension - HBA miniport driver's adapter data storage
 Return Value:
***********************************************************************/
static void orc_interrupt(
			  ORC_HCS * hcsp
)
{
	BYTE bScbIdx;
	ORC_SCB *pScb;

	if (ORC_RD(hcsp->HCS_Base, ORC_RQUEUECNT) == 0) {
		return;		// 0;

	}
	do {
		bScbIdx = ORC_RD(hcsp->HCS_Base, ORC_RQUEUE);

		pScb = (ORC_SCB *) ((ULONG) hcsp->HCS_virScbArray + (ULONG) (sizeof(ORC_SCB) * bScbIdx));
		pScb->SCB_Status = 0x0;

		inia100SCBPost((BYTE *) hcsp, (BYTE *) pScb);
	} while (ORC_RD(hcsp->HCS_Base, ORC_RQUEUECNT));
	return;			//1;

}				/* End of I1060Interrupt() */

/*****************************************************************************
 Function name  : inia100BuildSCB
 Description    : 
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static void inia100BuildSCB(ORC_HCS * pHCB, ORC_SCB * pSCB, struct scsi_cmnd * SCpnt)
{				/* Create corresponding SCB     */
	struct scatterlist *pSrbSG;
	ORC_SG *pSG;		/* Pointer to SG list           */
	int i, count_sg;
	ESCB *pEScb;

	pEScb = pSCB->SCB_EScb;
	pEScb->SCB_Srb = SCpnt;
	pSG = NULL;

	pSCB->SCB_Opcode = ORC_EXECSCSI;
	pSCB->SCB_Flags = SCF_NO_DCHK;	/* Clear done bit               */
	pSCB->SCB_Target = SCpnt->device->id;
	pSCB->SCB_Lun = SCpnt->device->lun;
	pSCB->SCB_Reserved0 = 0;
	pSCB->SCB_Reserved1 = 0;
	pSCB->SCB_SGLen = 0;

	if ((pSCB->SCB_XferLen = (U32) SCpnt->request_bufflen)) {
		pSG = (ORC_SG *) & pEScb->ESCB_SGList[0];
		if (SCpnt->use_sg) {
			pSrbSG = (struct scatterlist *) SCpnt->request_buffer;
			count_sg = pci_map_sg(pHCB->pdev, pSrbSG, SCpnt->use_sg,
					SCpnt->sc_data_direction);
			pSCB->SCB_SGLen = (U32) (count_sg * 8);
			for (i = 0; i < count_sg; i++, pSG++, pSrbSG++) {
				pSG->SG_Ptr = (U32) sg_dma_address(pSrbSG);
				pSG->SG_Len = (U32) sg_dma_len(pSrbSG);
			}
		} else if (SCpnt->request_bufflen != 0) {/* Non SG */
			pSCB->SCB_SGLen = 0x8;
			SCpnt->SCp.dma_handle = pci_map_single(pHCB->pdev,
					SCpnt->request_buffer,
					SCpnt->request_bufflen,
					SCpnt->sc_data_direction);
			pSG->SG_Ptr = (U32) SCpnt->SCp.dma_handle;
			pSG->SG_Len = (U32) SCpnt->request_bufflen;
		} else {
			pSCB->SCB_SGLen = 0;
			pSG->SG_Ptr = 0;
			pSG->SG_Len = 0;
		}
	}
	pSCB->SCB_SGPAddr = (U32) pSCB->SCB_SensePAddr;
	pSCB->SCB_HaStat = 0;
	pSCB->SCB_TaStat = 0;
	pSCB->SCB_Link = 0xFF;
	pSCB->SCB_SenseLen = SENSE_SIZE;
	pSCB->SCB_CDBLen = SCpnt->cmd_len;
	if (pSCB->SCB_CDBLen >= IMAX_CDB) {
		printk("max cdb length= %x\b", SCpnt->cmd_len);
		pSCB->SCB_CDBLen = IMAX_CDB;
	}
	pSCB->SCB_Ident = SCpnt->device->lun | DISC_ALLOW;
	if (SCpnt->device->tagged_supported) {	/* Tag Support                  */
		pSCB->SCB_TagMsg = SIMPLE_QUEUE_TAG;	/* Do simple tag only   */
	} else {
		pSCB->SCB_TagMsg = 0;	/* No tag support               */
	}
	memcpy(&pSCB->SCB_CDB[0], &SCpnt->cmnd, pSCB->SCB_CDBLen);
	return;
}

/*****************************************************************************
 Function name  : inia100_queue
 Description    : Queue a command and setup interrupts for a free bus.
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_queue(struct scsi_cmnd * SCpnt, void (*done) (struct scsi_cmnd *))
{
	register ORC_SCB *pSCB;
	ORC_HCS *pHCB;		/* Point to Host adapter control block */

	pHCB = (ORC_HCS *) SCpnt->device->host->hostdata;
	SCpnt->scsi_done = done;
	/* Get free SCSI control block  */
	if ((pSCB = orc_alloc_scb(pHCB)) == NULL)
		return SCSI_MLQUEUE_HOST_BUSY;

	inia100BuildSCB(pHCB, pSCB, SCpnt);
	orc_exec_scb(pHCB, pSCB);	/* Start execute SCB            */

	return (0);
}

/*****************************************************************************
 Function name  : inia100_abort
 Description    : Abort a queued command.
	                 (commands that are on the bus can't be aborted easily)
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_abort(struct scsi_cmnd * SCpnt)
{
	ORC_HCS *hcsp;

	hcsp = (ORC_HCS *) SCpnt->device->host->hostdata;
	return orc_abort_srb(hcsp, SCpnt);
}

/*****************************************************************************
 Function name  : inia100_reset
 Description    : Reset registers, reset a hanging bus and
                  kill active and disconnected commands for target w/o soft reset
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_bus_reset(struct scsi_cmnd * SCpnt)
{				/* I need Host Control Block Information */
	ORC_HCS *pHCB;
	pHCB = (ORC_HCS *) SCpnt->device->host->hostdata;
	return orc_reset_scsi_bus(pHCB);
}

/*****************************************************************************
 Function name  : inia100_device_reset
 Description    : Reset the device
 Input          : pHCB  -       Pointer to host adapter structure
 Output         : None.
 Return         : pSRB  -       Pointer to SCSI request block.
*****************************************************************************/
static int inia100_device_reset(struct scsi_cmnd * SCpnt)
{				/* I need Host Control Block Information */
	ORC_HCS *pHCB;
	pHCB = (ORC_HCS *) SCpnt->device->host->hostdata;
	return orc_device_reset(pHCB, SCpnt, SCpnt->device->id);

}

/*****************************************************************************
 Function name  : inia100SCBPost
 Description    : This is callback routine be called when orc finish one
			SCSI command.
 Input          : pHCB  -       Pointer to host adapter control block.
		  pSCB  -       Pointer to SCSI control block.
 Output         : None.
 Return         : None.
*****************************************************************************/
static void inia100SCBPost(BYTE * pHcb, BYTE * pScb)
{
	struct scsi_cmnd *pSRB;	/* Pointer to SCSI request block */
	ORC_HCS *pHCB;
	ORC_SCB *pSCB;
	ESCB *pEScb;

	pHCB = (ORC_HCS *) pHcb;
	pSCB = (ORC_SCB *) pScb;
	pEScb = pSCB->SCB_EScb;
	if ((pSRB = (struct scsi_cmnd *) pEScb->SCB_Srb) == 0) {
		printk("inia100SCBPost: SRB pointer is empty\n");
		orc_release_scb(pHCB, pSCB);	/* Release SCB for current channel */
		return;
	}
	pEScb->SCB_Srb = NULL;

	switch (pSCB->SCB_HaStat) {
	case 0x0:
	case 0xa:		/* Linked command complete without error and linked normally */
	case 0xb:		/* Linked command complete without error interrupt generated */
		pSCB->SCB_HaStat = 0;
		break;

	case 0x11:		/* Selection time out-The initiator selection or target
				   reselection was not complete within the SCSI Time out period */
		pSCB->SCB_HaStat = DID_TIME_OUT;
		break;

	case 0x14:		/* Target bus phase sequence failure-An invalid bus phase or bus
				   phase sequence was requested by the target. The host adapter
				   will generate a SCSI Reset Condition, notifying the host with
				   a SCRD interrupt */
		pSCB->SCB_HaStat = DID_RESET;
		break;

	case 0x1a:		/* SCB Aborted. 07/21/98 */
		pSCB->SCB_HaStat = DID_ABORT;
		break;

	case 0x12:		/* Data overrun/underrun-The target attempted to transfer more data
				   than was allocated by the Data Length field or the sum of the
				   Scatter / Gather Data Length fields. */
	case 0x13:		/* Unexpected bus free-The target dropped the SCSI BSY at an unexpected time. */
	case 0x16:		/* Invalid CCB Operation Code-The first byte of the CCB was invalid. */

	default:
		printk("inia100: %x %x\n", pSCB->SCB_HaStat, pSCB->SCB_TaStat);
		pSCB->SCB_HaStat = DID_ERROR;	/* Couldn't find any better */
		break;
	}

	if (pSCB->SCB_TaStat == 2) {	/* Check condition              */
		memcpy((unsigned char *) &pSRB->sense_buffer[0],
		   (unsigned char *) &pEScb->ESCB_SGList[0], SENSE_SIZE);
	}
	pSRB->result = pSCB->SCB_TaStat | (pSCB->SCB_HaStat << 16);

	if (pSRB->use_sg) {
		pci_unmap_sg(pHCB->pdev,
			     (struct scatterlist *)pSRB->request_buffer,
			     pSRB->use_sg, pSRB->sc_data_direction);
	} else if (pSRB->request_bufflen != 0) {
		pci_unmap_single(pHCB->pdev, pSRB->SCp.dma_handle,
				 pSRB->request_bufflen,
				 pSRB->sc_data_direction);
	}

	pSRB->scsi_done(pSRB);	/* Notify system DONE           */

	orc_release_scb(pHCB, pSCB);	/* Release SCB for current channel */
}

/*
 * Interrupt handler (main routine of the driver)
 */
static irqreturn_t inia100_intr(int irqno, void *devid, struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *)devid;
	ORC_HCS *pHcb = (ORC_HCS *)host->hostdata;
	unsigned long flags;

	spin_lock_irqsave(host->host_lock, flags);
	orc_interrupt(pHcb);
	spin_unlock_irqrestore(host->host_lock, flags);

	return IRQ_HANDLED;
}

static struct scsi_host_template inia100_template = {
	.proc_name		= "inia100",
	.name			= inia100_REVID,
	.queuecommand		= inia100_queue,
	.eh_abort_handler	= inia100_abort,
	.eh_bus_reset_handler	= inia100_bus_reset,
	.eh_device_reset_handler = inia100_device_reset,
	.can_queue		= 1,
	.this_id		= 1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun 		= 1,
	.use_clustering		= ENABLE_CLUSTERING,
};

static int __devinit inia100_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	struct Scsi_Host *shost;
	ORC_HCS *pHCB;
	unsigned long port, bios;
	int error = -ENODEV;
	u32 sz;
	unsigned long dBiosAdr;
	char *pbBiosAdr;

	if (pci_enable_device(pdev))
		goto out;
	if (pci_set_dma_mask(pdev, 0xffffffffULL)) {
		printk(KERN_WARNING "Unable to set 32bit DMA "
				    "on inia100 adapter, ignoring.\n");
		goto out_disable_device;
	}

	pci_set_master(pdev);

	port = pci_resource_start(pdev, 0);
	if (!request_region(port, 256, "inia100")) {
		printk(KERN_WARNING "inia100: io port 0x%lx, is busy.\n", port);
		goto out_disable_device;
	}

	/* <02> read from base address + 0x50 offset to get the bios balue. */
	bios = ORC_RDWORD(port, 0x50);


	shost = scsi_host_alloc(&inia100_template, sizeof(ORC_HCS));
	if (!shost)
		goto out_release_region;

	pHCB = (ORC_HCS *)shost->hostdata;
	pHCB->pdev = pdev;
	pHCB->HCS_Base = port;
	pHCB->HCS_BIOS = bios;
	spin_lock_init(&pHCB->BitAllocFlagLock);

	/* Get total memory needed for SCB */
	sz = ORC_MAXQUEUE * sizeof(ORC_SCB);
	pHCB->HCS_virScbArray = pci_alloc_consistent(pdev, sz,
			&pHCB->HCS_physScbArray);
	if (!pHCB->HCS_virScbArray) {
		printk("inia100: SCB memory allocation error\n");
		goto out_host_put;
	}
	memset(pHCB->HCS_virScbArray, 0, sz);

	/* Get total memory needed for ESCB */
	sz = ORC_MAXQUEUE * sizeof(ESCB);
	pHCB->HCS_virEscbArray = pci_alloc_consistent(pdev, sz,
			&pHCB->HCS_physEscbArray);
	if (!pHCB->HCS_virEscbArray) {
		printk("inia100: ESCB memory allocation error\n");
		goto out_free_scb_array;
	}
	memset(pHCB->HCS_virEscbArray, 0, sz);

	dBiosAdr = pHCB->HCS_BIOS;
	dBiosAdr = (dBiosAdr << 4);
	pbBiosAdr = phys_to_virt(dBiosAdr);
	if (init_orchid(pHCB)) {	/* Initialize orchid chip */
		printk("inia100: initial orchid fail!!\n");
		goto out_free_escb_array;
	}

	shost->io_port = pHCB->HCS_Base;
	shost->n_io_port = 0xff;
	shost->can_queue = ORC_MAXQUEUE;
	shost->unique_id = shost->io_port;
	shost->max_id = pHCB->HCS_MaxTar;
	shost->max_lun = 16;
	shost->irq = pHCB->HCS_Intr = pdev->irq;
	shost->this_id = pHCB->HCS_SCSI_ID;	/* Assign HCS index */
	shost->sg_tablesize = TOTAL_SG_ENTRY;

	/* Initial orc chip           */
	error = request_irq(pdev->irq, inia100_intr, SA_SHIRQ,
			"inia100", shost);
	if (error < 0) {
		printk(KERN_WARNING "inia100: unable to get irq %d\n",
				pdev->irq);
		goto out_free_escb_array;
	}

	pci_set_drvdata(pdev, shost);

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_free_irq;

	scsi_scan_host(shost);
	return 0;

 out_free_irq:
        free_irq(shost->irq, shost);
 out_free_escb_array:
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ESCB),
			pHCB->HCS_virEscbArray, pHCB->HCS_physEscbArray);
 out_free_scb_array:
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ORC_SCB),
			pHCB->HCS_virScbArray, pHCB->HCS_physScbArray);
 out_host_put:
	scsi_host_put(shost);
 out_release_region:
        release_region(port, 256);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

static void __devexit inia100_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	ORC_HCS *pHCB = (ORC_HCS *)shost->hostdata;

	scsi_remove_host(shost);

        free_irq(shost->irq, shost);
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ESCB),
			pHCB->HCS_virEscbArray, pHCB->HCS_physEscbArray);
	pci_free_consistent(pdev, ORC_MAXQUEUE * sizeof(ORC_SCB),
			pHCB->HCS_virScbArray, pHCB->HCS_physScbArray);
        release_region(shost->io_port, 256);

	scsi_host_put(shost);
} 

static struct pci_device_id inia100_pci_tbl[] = {
	{PCI_VENDOR_ID_INIT, 0x1060, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};
MODULE_DEVICE_TABLE(pci, inia100_pci_tbl);

static struct pci_driver inia100_pci_driver = {
	.name		= "inia100",
	.id_table	= inia100_pci_tbl,
	.probe		= inia100_probe_one,
	.remove		= __devexit_p(inia100_remove_one),
};

static int __init inia100_init(void)
{
	return pci_module_init(&inia100_pci_driver);
}

static void __exit inia100_exit(void)
{
	pci_unregister_driver(&inia100_pci_driver);
}

MODULE_DESCRIPTION("Initio A100U2W SCSI driver");
MODULE_AUTHOR("Initio Corporation");
MODULE_LICENSE("Dual BSD/GPL");

module_init(inia100_init);
module_exit(inia100_exit);
