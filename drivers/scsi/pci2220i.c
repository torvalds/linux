/****************************************************************************
 * Perceptive Solutions, Inc. PCI-2220I device driver for Linux.
 *
 * pci2220i.c - Linux Host Driver for PCI-2220I EIDE RAID Adapters
 *
 * Copyright (c) 1997-1999 Perceptive Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * Technical updates and product information at:
 *  http://www.psidisk.com
 *
 * Please send questions, comments, bug reports to:
 *  tech@psidisk.com Technical Support
 *
 *
 *	Revisions 1.10		Mar-26-1999
 *		- Updated driver for RAID and hot reconstruct support.
 *
 *	Revisions 1.11		Mar-26-1999
 *		- Fixed spinlock and PCI configuration.
 *
 *	Revision 2.00		December-1-1999
 *		- Added code for the PCI-2240I controller
 *		- Added code for ATAPI devices.
 *		- Double buffer for scatter/gather support
 *
 *	Revision 2.10		March-27-2000
 *		- Added support for dynamic DMA
 *
 ****************************************************************************/

#error Convert me to understand page+offset based scatterlists

//#define DEBUG 1

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/blkdev.h>
#include <linux/timer.h>
#include <linux/spinlock.h>

#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "pci2220i.h"
#include "psi_dale.h"


#define	PCI2220I_VERSION		"2.10"
#define	READ_CMD				IDE_CMD_READ_MULTIPLE
#define	WRITE_CMD				IDE_CMD_WRITE_MULTIPLE
#define	MAX_BUS_MASTER_BLOCKS	SECTORSXFER		// This is the maximum we can bus master

#ifdef DEBUG
#define DEB(x) x
#define STOP_HERE()	{int st;for(st=0;st<100;st++){st=1;}}
#else
#define DEB(x)
#define STOP_HERE()
#endif

#define MAXADAPTER 4					// Increase this and the sizes of the arrays below, if you need more.


typedef struct
	{
	UCHAR			byte6;				// device select register image
	UCHAR			spigot;				// spigot number
	UCHAR			spigots[2];			// RAID spigots
	UCHAR			deviceID[2];		// device ID codes
	USHORT			sectors;			// number of sectors per track
	USHORT			heads;				// number of heads
	USHORT			cylinders;			// number of cylinders for this device
	USHORT			spareword;			// placeholder
	ULONG			blocks;				// number of blocks on device
	DISK_MIRROR		DiskMirror[2];		// RAID status and control
	ULONG			lastsectorlba[2];	// last addressable sector on the drive
	USHORT			raid;				// RAID active flag
	USHORT			mirrorRecon;
	UCHAR			reconOn;
	USHORT			reconCount;
	USHORT			reconIsStarting;	// indicate hot reconstruct is starting
	UCHAR			cmdDrqInt;			// flag for command interrupt
	UCHAR			packet;				// command packet size in bytes
	}	OUR_DEVICE, *POUR_DEVICE;	

typedef struct
	{
	USHORT		 bigD;					// identity is a PCI-2240I if true, otherwise a PCI-2220I
	USHORT		 atapi;					// this interface is for ATAPI devices only
	ULONG		 regDmaDesc;			// address of the DMA discriptor register for direction of transfer
	ULONG		 regDmaCmdStat;			// Byte #1 of DMA command status register
	ULONG		 regDmaAddrPci;			// 32 bit register for PCI address of DMA
	ULONG		 regDmaAddrLoc;			// 32 bit register for local bus address of DMA
	ULONG		 regDmaCount;			// 32 bit register for DMA transfer count
	ULONG		 regDmaMode;			// 32 bit register for DMA mode control
	ULONG		 regRemap;				// 32 bit local space remap
	ULONG		 regDesc;				// 32 bit local region descriptor
	ULONG		 regRange;				// 32 bit local range
	ULONG		 regIrqControl;			// 16 bit Interrupt enable/disable and status
	ULONG		 regScratchPad;			// scratch pad I/O base address
	ULONG		 regBase;				// Base I/O register for data space
	ULONG		 regData;				// data register I/O address
	ULONG		 regError;				// error register I/O address
	ULONG		 regSectCount;			// sector count register I/O address
	ULONG		 regLba0;				// least significant byte of LBA
	ULONG		 regLba8;				// next least significant byte of LBA
	ULONG		 regLba16;				// next most significan byte of LBA
	ULONG		 regLba24;				// head and most 4 significant bits of LBA
	ULONG		 regStatCmd;			// status on read and command on write register
	ULONG		 regStatSel;			// board status on read and spigot select on write register
	ULONG		 regFail;				// fail bits control register
	ULONG		 regAltStat;			// alternate status and drive control register
	ULONG		 basePort;				// PLX base I/O port
	USHORT		 timingMode;			// timing mode currently set for adapter
	USHORT		 timingPIO;				// TRUE if PIO timing is active
	struct pci_dev	*pcidev;
	ULONG		 timingAddress;			// address to use on adapter for current timing mode
	ULONG		 irqOwned;				// owned IRQ or zero if shared
	UCHAR		 numberOfDrives;		// saved number of drives on this controller
	UCHAR		 failRegister;			// current inverted data in fail register
	OUR_DEVICE	 device[BIGD_MAXDRIVES];
	DISK_MIRROR	*raidData[BIGD_MAXDRIVES];
	ULONG		 startSector;
	USHORT		 sectorCount;
	ULONG		 readCount;
	UCHAR		*currentSgBuffer;
	ULONG		 currentSgCount;
	USHORT		 nextSg;
	UCHAR		 cmd;
	Scsi_Cmnd	*SCpnt;
	POUR_DEVICE	 pdev;					// current device opearating on
	USHORT		 devInReconIndex;
	USHORT		 expectingIRQ;
	USHORT		 reconOn;				// Hot reconstruct is to be done.
	USHORT		 reconPhase;			// Hot reconstruct operation is in progress.
	ULONG		 reconSize;
	USHORT		 demoFail;				// flag for RAID failure demonstration
	USHORT		 survivor;
	USHORT		 failinprog;
	struct timer_list	reconTimer;	
	struct timer_list	timer;
	UCHAR		*kBuffer;
	dma_addr_t	 kBufferDma;
	UCHAR		 reqSense;
	UCHAR		 atapiCdb[16];
	UCHAR		 atapiSpecial;
	}	ADAPTER2220I, *PADAPTER2220I;

#define HOSTDATA(host) ((PADAPTER2220I)&host->hostdata)

#define	RECON_PHASE_READY		0x01
#define	RECON_PHASE_COPY		0x02
#define	RECON_PHASE_UPDATE		0x03
#define	RECON_PHASE_LAST		0x04
#define	RECON_PHASE_END			0x07	
#define	RECON_PHASE_MARKING		0x80
#define	RECON_PHASE_FAILOVER	0xFF

static struct	Scsi_Host 	   *PsiHost[MAXADAPTER] = {NULL,};  // One for each adapter
static			int				NumAdapters = 0;
static			int				Installed = 0;
static			SETUP			DaleSetup;
static			DISK_MIRROR		DiskMirror[BIGD_MAXDRIVES];
static			ULONG			ModeArray[] = {DALE_DATA_MODE2, DALE_DATA_MODE3, DALE_DATA_MODE4, DALE_DATA_MODE5};
static			ULONG			ModeArray2[] = {BIGD_DATA_MODE2, BIGD_DATA_MODE3, BIGD_DATA_MODE4, BIGD_DATA_MODE5};

static void ReconTimerExpiry (unsigned long data);

/*******************************************************************************************************
 *	Name:			Alarm
 *
 *	Description:	Sound the for the given device
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					device	 - Device number.
 *	
 *	Returns:		Nothing.
 *
 ******************************************************************************************************/
static void Alarm (PADAPTER2220I padapter, UCHAR device)
	{
	UCHAR	zc;

	if ( padapter->bigD )
		{
		zc = device | (FAIL_ANY | FAIL_AUDIBLE);
		if ( padapter->failRegister & FAIL_ANY ) 
			zc |= FAIL_MULTIPLE;
		
		padapter->failRegister = zc;
		outb_p (~zc, padapter->regFail);
		}
	else
		outb_p (0x3C | (1 << device), padapter->regFail);			// sound alarm and set fail light		
	}
/****************************************************************
 *	Name:	MuteAlarm	:LOCAL
 *
 *	Description:	Mute the audible alarm.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static void MuteAlarm (PADAPTER2220I padapter)
	{
	UCHAR	old;

	if ( padapter->bigD )
		{
		padapter->failRegister &= ~FAIL_AUDIBLE;
		outb_p (~padapter->failRegister, padapter->regFail);
		}
	else
		{
		old = (inb_p (padapter->regStatSel) >> 3) | (inb_p (padapter->regStatSel) & 0x83);
		outb_p (old | 0x40, padapter->regFail);
		}
	}
/****************************************************************
 *	Name:	WaitReady	:LOCAL
 *
 *	Description:	Wait for device ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WaitReady (PADAPTER2220I padapter)
	{
	ULONG	z;
	UCHAR	status;

	for ( z = 0;  z < (TIMEOUT_READY * 4);  z++ )
		{
		status = inb_p (padapter->regStatCmd);
		if ( (status & (IDE_STATUS_DRDY | IDE_STATUS_BUSY)) == IDE_STATUS_DRDY )
			return 0;
		udelay (250);
		}
	return status;
	}
/****************************************************************
 *	Name:	WaitReadyReset	:LOCAL
 *
 *	Description:	Wait for device ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WaitReadyReset (PADAPTER2220I padapter)
	{
	ULONG	z;
	UCHAR	status;

	for ( z = 0;  z < (125 * 16);  z++ )				// wait up to 1/4 second
		{
		status = inb_p (padapter->regStatCmd);
		if ( (status & (IDE_STATUS_DRDY | IDE_STATUS_BUSY)) == IDE_STATUS_DRDY )
			{
			DEB (printk ("\nPCI2220I:  Reset took %ld mSec to be ready", z / 8));
			return 0;
			}
		udelay (125);
		}
	DEB (printk ("\nPCI2220I:  Reset took more than 2 Seconds to come ready, Disk Failure"));
	return status;
	}
/****************************************************************
 *	Name:	WaitDrq	:LOCAL
 *
 *	Description:	Wait for device ready for data transfer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WaitDrq (PADAPTER2220I padapter)
	{
	ULONG	z;
	UCHAR	status;

	for ( z = 0;  z < (TIMEOUT_DRQ * 4);  z++ )
		{
		status = inb_p (padapter->regStatCmd);
		if ( status & IDE_STATUS_DRQ )
			return 0;
		udelay (250);
		}
	return status;
	}
/****************************************************************
 *	Name:	AtapiWaitReady	:LOCAL
 *
 *	Description:	Wait for device busy and DRQ to be cleared.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					msec	 - Number of milliseconds to wait.
 *
 *	Returns:		TRUE if drive does not clear busy in time.
 *
 ****************************************************************/
static int AtapiWaitReady (PADAPTER2220I padapter, int msec)
	{
	int z;

	for ( z = 0;  z < (msec * 16);  z++ )
		{
		if ( !(inb_p (padapter->regStatCmd) & (IDE_STATUS_BUSY | IDE_STATUS_DRQ)) )
			return FALSE;
		udelay (125);
		}
	return TRUE;
	}
/****************************************************************
 *	Name:	AtapiWaitDrq	:LOCAL
 *
 *	Description:	Wait for device ready for data transfer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					msec	 - Number of milliseconds to wait.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int AtapiWaitDrq (PADAPTER2220I padapter, int msec)
	{
	ULONG	z;

	for ( z = 0;  z < (msec * 16);  z++ )
		{
		if ( inb_p (padapter->regStatCmd) & IDE_STATUS_DRQ )
			return 0;
		udelay (128);
		}
	return TRUE;
	}
/****************************************************************
 *	Name:	HardReset	:LOCAL
 *
 *	Description:	Wait for device ready for data transfer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *					spigot	 - Spigot number.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int HardReset (PADAPTER2220I padapter, POUR_DEVICE pdev, UCHAR spigot)
	{
	DEB (printk ("\npci2220i:RESET  spigot = %X  devices = %d, %d", spigot, pdev->deviceID[0], pdev->deviceID[1]));
	mdelay (100);										// just wait 100 mSec to let drives flush	
	SelectSpigot (padapter, spigot | SEL_IRQ_OFF);
	
	outb_p (0x0E, padapter->regAltStat);					// reset the suvivor
	udelay (100);											// wait a little	
	outb_p (0x08, padapter->regAltStat);					// clear the reset
	udelay (100);

	outb_p (0xA0, padapter->regLba24);						// select the master drive
	if ( WaitReadyReset (padapter) )
		{
		DEB (printk ("\npci2220i: master not ready after reset"));
		return TRUE;
		}
	outb_p (0xB0, padapter->regLba24);						// try the slave drive
	if ( (inb_p (padapter->regStatCmd) & (IDE_STATUS_DRDY | IDE_STATUS_BUSY)) == IDE_STATUS_DRDY ) 
		{
		DEB (printk ("\nPCI2220I: initializing slave drive on spigot %X", spigot));
		outb_p (SECTORSXFER, padapter->regSectCount);
		WriteCommand (padapter, IDE_CMD_SET_MULTIPLE);	
		if ( WaitReady (padapter) )
			{
			DEB (printk ("\npci2220i: slave not ready after set multiple"));
			return TRUE;
			}
		}
	
	outb_p (0xA0, padapter->regLba24);				// select the drive
	outb_p (SECTORSXFER, padapter->regSectCount);
	WriteCommand (padapter, IDE_CMD_SET_MULTIPLE);	
	if ( WaitReady (padapter) )
		{
		DEB (printk ("\npci2220i: master not ready after set multiple"));
		return TRUE;
		}
	return FALSE;
	}
/****************************************************************
 *	Name:	AtapiReset	:LOCAL
 *
 *	Description:	Wait for device ready for data transfer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *
 *	Returns:		TRUE if drive does not come ready.
 *
 ****************************************************************/
static int AtapiReset (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	SelectSpigot (padapter, pdev->spigot);
	AtapiDevice (padapter, pdev->byte6);
	AtapiCountLo (padapter, 0);
	AtapiCountHi (padapter, 0);
	WriteCommand (padapter, IDE_COMMAND_ATAPI_RESET);
	udelay (125);
	if ( AtapiWaitReady (padapter, 1000) )
		return TRUE;
	if ( inb_p (padapter->regStatCmd) || (inb_p (padapter->regLba8) != 0x14) || (inb_p (padapter->regLba16) != 0xEB) )
		return TRUE;
	return FALSE;
	}
/****************************************************************
 *	Name:	WalkScatGath	:LOCAL
 *
 *	Description:	Transfer data to/from scatter/gather buffers.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					datain   - TRUE if data read.
 *					length   - Number of bytes to transfer.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void WalkScatGath (PADAPTER2220I padapter, UCHAR datain, ULONG length)
	{
	ULONG	 count;
	UCHAR	*buffer = padapter->kBuffer;

	while ( length )
		{
		count = ( length > padapter->currentSgCount ) ? padapter->currentSgCount : length; 
		
		if ( datain )
			memcpy (padapter->currentSgBuffer, buffer, count);
		else
			memcpy (buffer, padapter->currentSgBuffer, count);

		padapter->currentSgCount -= count;
		if ( !padapter->currentSgCount )
			{
			if ( padapter->nextSg < padapter->SCpnt->use_sg )
				{
				padapter->currentSgBuffer = ((struct scatterlist *)padapter->SCpnt->request_buffer)[padapter->nextSg].address;
				padapter->currentSgCount = ((struct scatterlist *)padapter->SCpnt->request_buffer)[padapter->nextSg].length;
				padapter->nextSg++;
				}
			}
		else
			padapter->currentSgBuffer += count;

		length -= count;
		buffer += count;
		}
	}
/****************************************************************
 *	Name:	BusMaster	:LOCAL
 *
 *	Description:	Do a bus master I/O.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					datain	 - TRUE if data read.
 *					irq		 - TRUE if bus master interrupt expected.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void BusMaster (PADAPTER2220I padapter, UCHAR datain, UCHAR irq)
	{
	ULONG zl;
	
	zl = ( padapter->sectorCount > MAX_BUS_MASTER_BLOCKS ) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
	padapter->sectorCount -= zl;
	zl *= (ULONG)BYTES_PER_SECTOR;

	if ( datain )
		{
		padapter->readCount = zl;
		outb_p (8, padapter->regDmaDesc);							// read operation
		if ( padapter->bigD )
			{
			if ( irq && !padapter->sectorCount )
				outb_p (0x0C, padapter->regDmaMode);				// interrupt on
			else
				outb_p (0x08, padapter->regDmaMode);				// no interrupt
			}
		else 
			{
			if ( irq && !padapter->sectorCount )
				outb_p (0x05, padapter->regDmaMode);				// interrupt on
			else
				outb_p (0x01, padapter->regDmaMode);				// no interrupt
			}
		}
	else
		{
		outb_p (0x00, padapter->regDmaDesc);						// write operation
		if ( padapter->bigD )
			outb_p (0x08, padapter->regDmaMode);					// no interrupt						
		else
			outb_p (0x01, padapter->regDmaMode);					// no interrupt
		WalkScatGath (padapter, FALSE, zl);	
		}
	
	outl (padapter->timingAddress, padapter->regDmaAddrLoc);
	outl (padapter->kBufferDma, padapter->regDmaAddrPci);
	outl (zl, padapter->regDmaCount);
	outb_p (0x03, padapter->regDmaCmdStat);							// kick the DMA engine in gear
	}
/****************************************************************
 *	Name:	AtapiBusMaster	:LOCAL
 *
 *	Description:	Do a bus master I/O.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					datain	 - TRUE if data read.
 *					length	 - Number of bytes to transfer.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void AtapiBusMaster (PADAPTER2220I padapter, UCHAR datain, ULONG length)
	{
	outl (padapter->timingAddress, padapter->regDmaAddrLoc);
	outl (padapter->kBufferDma, padapter->regDmaAddrPci);
	outl (length, padapter->regDmaCount);
	if ( datain )
		{
		if ( padapter->readCount )
				WalkScatGath (padapter, TRUE, padapter->readCount);
		outb_p (0x08, padapter->regDmaDesc);						// read operation
		outb_p (0x08, padapter->regDmaMode);						// no interrupt
		padapter->readCount = length;
		}
	else
		{
		outb_p (0x00, padapter->regDmaDesc);						// write operation
		outb_p (0x08, padapter->regDmaMode);						// no interrupt						
		if ( !padapter->atapiSpecial )
			WalkScatGath (padapter, FALSE, length);	
		}
	outb_p (0x03, padapter->regDmaCmdStat);							// kick the DMA engine in gear
	}
/****************************************************************
 *	Name:	WriteData	:LOCAL
 *
 *	Description:	Write data to device.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE if drive does not assert DRQ in time.
 *
 ****************************************************************/
static int WriteData (PADAPTER2220I padapter)
	{
	ULONG	zl;
	
	if ( !WaitDrq (padapter) )
		{
		if ( padapter->timingPIO )
			{
			zl = (padapter->sectorCount > MAX_BUS_MASTER_BLOCKS) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
			WalkScatGath (padapter, FALSE, zl * BYTES_PER_SECTOR);
			outsw (padapter->regData, padapter->kBuffer, zl * (BYTES_PER_SECTOR / 2));
			padapter->sectorCount -= zl;
			}
		else
			BusMaster (padapter, 0, 0);
		return 0;
		}
	padapter->cmd = 0;												// null out the command byte
	return 1;
	}
/****************************************************************
 *	Name:	WriteDataBoth	:LOCAL
 *
 *	Description:	Write data to device.
 *
 *	Parameters:		padapter - Pointer to adapter structure.
 *					pdev	 - Pointer to device structure
 *
 *	Returns:		Index + 1 of drive not failed or zero for OK.
 *
 ****************************************************************/
static int WriteDataBoth (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	ULONG	zl;
	UCHAR	status0, status1;

	SelectSpigot (padapter, pdev->spigots[0]);
	status0 = WaitDrq (padapter);
	if ( !status0 )
		{
		SelectSpigot (padapter, pdev->spigots[1]);
		status1 = WaitDrq (padapter);
		if ( !status1 )
			{
			SelectSpigot (padapter, pdev->spigots[0] | pdev->spigots[1] | padapter->bigD);
			if ( padapter->timingPIO )
				{
				zl = (padapter->sectorCount > MAX_BUS_MASTER_BLOCKS) ? MAX_BUS_MASTER_BLOCKS : padapter->sectorCount;
				WalkScatGath (padapter, FALSE, zl * BYTES_PER_SECTOR);
				outsw (padapter->regData, padapter->kBuffer, zl * (BYTES_PER_SECTOR / 2));
				padapter->sectorCount -= zl;
				}
			else
				BusMaster (padapter, 0, 0);
			return 0;
			}
		}
	padapter->cmd = 0;												// null out the command byte
	if ( status0 )
		return 2;
	return 1;
	}
/****************************************************************
 *	Name:	IdeCmd	:LOCAL
 *
 *	Description:	Process an IDE command.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *
 *	Returns:		Zero if no error or status register contents on error.
 *
 ****************************************************************/
static UCHAR IdeCmd (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	UCHAR	status;

	SelectSpigot (padapter, pdev->spigot | padapter->bigD);							// select the spigot
	outb_p (pdev->byte6 | ((UCHAR *)(&padapter->startSector))[3], padapter->regLba24);			// select the drive
	status = WaitReady (padapter);
	if ( !status )
		{
		outb_p (padapter->sectorCount, padapter->regSectCount);
		outb_p (((UCHAR *)(&padapter->startSector))[0], padapter->regLba0);
		outb_p (((UCHAR *)(&padapter->startSector))[1], padapter->regLba8);
		outb_p (((UCHAR *)(&padapter->startSector))[2], padapter->regLba16);
		padapter->expectingIRQ = TRUE;
		WriteCommand (padapter, padapter->cmd);
		return 0;
		}

	padapter->cmd = 0;									// null out the command byte
	return status;
	}
/****************************************************************
 *	Name:	IdeCmdBoth	:LOCAL
 *
 *	Description:	Process an IDE command to both drivers.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device structure
 *
 *	Returns:		Index + 1 of drive not failed or zero for OK.
 *
 ****************************************************************/
static UCHAR IdeCmdBoth (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	UCHAR	status0;
	UCHAR	status1;

	SelectSpigot (padapter, pdev->spigots[0] | pdev->spigots[1]);								// select the spigots
	outb_p (padapter->pdev->byte6 | ((UCHAR *)(&padapter->startSector))[3], padapter->regLba24);// select the drive
	SelectSpigot (padapter, pdev->spigots[0]);
	status0 = WaitReady (padapter);
	if ( !status0 )
		{
		SelectSpigot (padapter, pdev->spigots[1]);
		status1 = WaitReady (padapter);
		if ( !status1 )
			{
			SelectSpigot (padapter, pdev->spigots[0] | pdev->spigots[1] | padapter->bigD);
			outb_p (padapter->sectorCount, padapter->regSectCount);
			outb_p (((UCHAR *)(&padapter->startSector))[0], padapter->regLba0);
			outb_p (((UCHAR *)(&padapter->startSector))[1], padapter->regLba8);
			outb_p (((UCHAR *)(&padapter->startSector))[2], padapter->regLba16);
			padapter->expectingIRQ = TRUE;
			WriteCommand (padapter, padapter->cmd);
			return 0;
			}
		}
	padapter->cmd = 0;									// null out the command byte
	if ( status0 )
		return 2;
	return 1;
	}
/****************************************************************
 *	Name:	OpDone	:LOCAL
 *
 *	Description:	Complete an operatoin done sequence.
 *
 *	Parameters:		padapter - Pointer to host data block.
 *					spigot	 - Spigot select code.
 *					device	 - Device byte code.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void OpDone (PADAPTER2220I padapter, ULONG result)
	{
	Scsi_Cmnd	   *SCpnt = padapter->SCpnt;
	
	if ( padapter->reconPhase )
		{
		padapter->reconPhase = 0;
		if ( padapter->SCpnt )
			{
			Pci2220i_QueueCommand (SCpnt, SCpnt->scsi_done);
			}
		else
			{
			if ( padapter->reconOn )
				{
				ReconTimerExpiry ((unsigned long)padapter);
				}
			}
		}
	else
		{
		padapter->cmd = 0;
		padapter->SCpnt = NULL;	
		padapter->pdev = NULL;
		SCpnt->result = result;
		SCpnt->scsi_done (SCpnt);
		if ( padapter->reconOn && !padapter->reconTimer.data )
			{
			padapter->reconTimer.expires = jiffies + (HZ / 4);	// start in 1/4 second
			padapter->reconTimer.data = (unsigned long)padapter;
			add_timer (&padapter->reconTimer);
			}
		}
	}
/****************************************************************
 *	Name:	InlineIdentify	:LOCAL
 *
 *	Description:	Do an intline inquiry on a drive.
 *
 *	Parameters:		padapter - Pointer to host data block.
 *					spigot	 - Spigot select code.
 *					device	 - Device byte code.
 *
 *	Returns:		Last addressable sector or zero if none.
 *
 ****************************************************************/
static ULONG InlineIdentify (PADAPTER2220I padapter, UCHAR spigot, UCHAR device)
	{
	PIDENTIFY_DATA	pid = (PIDENTIFY_DATA)padapter->kBuffer;

	SelectSpigot (padapter, spigot | SEL_IRQ_OFF);					// select the spigot
	outb_p ((device << 4) | 0xA0, padapter->regLba24);				// select the drive
	if ( WaitReady (padapter) )
		return 0;
	WriteCommand (padapter, IDE_COMMAND_IDENTIFY);	
	if ( WaitDrq (padapter) )
		return 0;
	insw (padapter->regData, padapter->kBuffer, sizeof (IDENTIFY_DATA) >> 1);
	return (pid->LBATotalSectors - 1);
	}
/****************************************************************
 *	Name:	AtapiIdentify	:LOCAL
 *
 *	Description:	Do an intline inquiry on a drive.
 *
 *	Parameters:		padapter - Pointer to host data block.
 *					pdev	 - Pointer to device table.
 *
 *	Returns:		TRUE on error.
 *
 ****************************************************************/
static ULONG AtapiIdentify (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	ATAPI_GENERAL_0		ag0;
	USHORT				zs;
	int					z;

	AtapiDevice (padapter, pdev->byte6);	
	WriteCommand (padapter, IDE_COMMAND_ATAPI_IDENTIFY);	
	if ( AtapiWaitDrq (padapter, 3000) )
		return TRUE;

	*(USHORT *)&ag0 = inw_p (padapter->regData);
	for ( z = 0;  z < 255;  z++ )
		zs = inw_p (padapter->regData);

	if ( ag0.ProtocolType == 2 )
		{
		if ( ag0.CmdDrqType == 1 )
			pdev->cmdDrqInt = TRUE;
		switch ( ag0.CmdPacketSize )
			{
			case 0:
				pdev->packet = 6;
				break;
			case 1:
				pdev->packet = 8;
				break;
			default:
				pdev->packet = 6;
				break;
			}
		return FALSE;
		}
	return TRUE;
	}
/****************************************************************
 *	Name:	Atapi2Scsi
 *
 *	Description:	Convert ATAPI data to SCSI data.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					SCpnt	 - Pointer to SCSI command structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
void Atapi2Scsi (PADAPTER2220I padapter, Scsi_Cmnd *SCpnt)
	{
	UCHAR	*buff = padapter->currentSgBuffer;
 
	switch ( SCpnt->cmnd[0] )
		{
		case SCSIOP_MODE_SENSE:
			buff[0] = padapter->kBuffer[1];
			buff[1] = padapter->kBuffer[2];
			buff[2] = padapter->kBuffer[3];
			buff[3] = padapter->kBuffer[7];
			memcpy (&buff[4], &padapter->kBuffer[8], padapter->atapiCdb[8] - 8);
			break;
		case SCSIOP_INQUIRY:
			padapter->kBuffer[2] = 2;
			memcpy (buff, padapter->kBuffer, padapter->currentSgCount);
			break;		
		default:
			if ( padapter->readCount )
				WalkScatGath (padapter, TRUE, padapter->readCount);
			break;
		}
	}
/****************************************************************
 *	Name:	Scsi2Atapi
 *
 *	Description:	Convert SCSI packet command to Atapi packet command.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					SCpnt	 - Pointer to SCSI command structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void Scsi2Atapi (PADAPTER2220I padapter, Scsi_Cmnd *SCpnt)
	{
	UCHAR	*cdb = SCpnt->cmnd;
	UCHAR	*buff = padapter->currentSgBuffer;

	switch (cdb[0]) 
		{
		case SCSIOP_READ6:
            padapter->atapiCdb[0] = SCSIOP_READ;
			padapter->atapiCdb[1] = cdb[1] & 0xE0;
            padapter->atapiCdb[3] = cdb[1] & 0x1F;
			padapter->atapiCdb[4] = cdb[2];
			padapter->atapiCdb[5] = cdb[3];
			padapter->atapiCdb[8] = cdb[4];
			padapter->atapiCdb[9] = cdb[5];
			break;
		case SCSIOP_WRITE6:
            padapter->atapiCdb[0] = SCSIOP_WRITE;
			padapter->atapiCdb[1] = cdb[1] & 0xE0;
            padapter->atapiCdb[3] = cdb[1] & 0x1F;
			padapter->atapiCdb[4] = cdb[2];
			padapter->atapiCdb[5] = cdb[3];
			padapter->atapiCdb[8] = cdb[4];
			padapter->atapiCdb[9] = cdb[5];
			break;
        case SCSIOP_MODE_SENSE: 
            padapter->atapiCdb[0] = SCSIOP_MODE_SENSE10;
			padapter->atapiCdb[2] = cdb[2];
			padapter->atapiCdb[8] = cdb[4] + 4;
            break;

        case SCSIOP_MODE_SELECT: 
			padapter->atapiSpecial = TRUE;
			padapter->atapiCdb[0] = SCSIOP_MODE_SELECT10;
			padapter->atapiCdb[1] = cdb[1] | 0x10;
			memcpy (padapter->kBuffer, buff, 4);
			padapter->kBuffer[4] = padapter->kBuffer[5] = 0;
			padapter->kBuffer[6] = padapter->kBuffer[7] = 0;
			memcpy (&padapter->kBuffer[8], &buff[4], cdb[4] - 4);
			padapter->atapiCdb[8] = cdb[4] + 4;
			break;
	    }
	}
/****************************************************************
 *	Name:	AtapiSendCdb
 *
 *	Description:	Send the CDB packet to the device.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *					cdb		 - Pointer to 16 byte SCSI cdb.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void AtapiSendCdb (PADAPTER2220I padapter, POUR_DEVICE pdev, CHAR *cdb)
	{
	DEB (printk ("\nPCI2242I: CDB: %X %X %X %X %X %X %X %X %X %X %X %X", cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]));
	outsw (padapter->regData, cdb, pdev->packet);
	}
/****************************************************************
 *	Name:	AtapiRequestSense
 *
 *	Description:	Send the CDB packet to the device.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *					SCpnt	 - Pointer to SCSI command structure.
 *					pass	 - If true then this is the second pass to send cdb.
 *
 *	Returns:		TRUE on error.
 *
 ****************************************************************/
static int AtapiRequestSense (PADAPTER2220I padapter, POUR_DEVICE pdev, Scsi_Cmnd *SCpnt, UCHAR pass)
	{
	UCHAR	cdb[16] = {SCSIOP_REQUEST_SENSE,0,0,0,16,0,0,0,0,0,0,0,0,0,0,0};		
	
	DEB (printk ("\nPCI2242I: AUTO REQUEST SENSE"));
	cdb[4] = (UCHAR)(sizeof (SCpnt->sense_buffer));
	if ( !pass )
		{
		padapter->reqSense = TRUE;
	 	AtapiCountLo (padapter, cdb[4]);						
		AtapiCountHi (padapter, 0);						
		outb_p (0, padapter->regError);						
		WriteCommand (padapter, IDE_COMMAND_ATAPI_PACKET);
		if ( pdev->cmdDrqInt )
			return FALSE;

		if ( AtapiWaitDrq (padapter, 500) )
			return TRUE;
		}
	AtapiSendCdb (padapter, pdev, cdb);	
	return FALSE;
	}
/****************************************************************
 *	Name:	InlineReadSignature	:LOCAL
 *
 *	Description:	Do an inline read RAID sigature.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to device.
 *					index	 - index of data to read.
 *
 *	Returns:		Zero if no error or status register contents on error.
 *
 ****************************************************************/
static UCHAR InlineReadSignature (PADAPTER2220I padapter, POUR_DEVICE pdev, int index)
	{
	UCHAR	status;
	ULONG	zl = pdev->lastsectorlba[index];

	SelectSpigot (padapter, pdev->spigots[index] | SEL_IRQ_OFF);	// select the spigot without interrupts
	outb_p (pdev->byte6 | ((UCHAR *)&zl)[3], padapter->regLba24);		
	status = WaitReady (padapter);
	if ( !status )
		{
		outb_p (((UCHAR *)&zl)[2], padapter->regLba16);
		outb_p (((UCHAR *)&zl)[1], padapter->regLba8); 
		outb_p (((UCHAR *)&zl)[0], padapter->regLba0);
		outb_p (1, padapter->regSectCount);
		WriteCommand (padapter, IDE_COMMAND_READ);
		status = WaitDrq (padapter);
		if ( !status )
			{
			insw (padapter->regData, padapter->kBuffer, BYTES_PER_SECTOR / 2);
			((ULONG *)(&pdev->DiskMirror[index]))[0] = ((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[0];
			((ULONG *)(&pdev->DiskMirror[index]))[1] = ((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[1];
			// some drives assert DRQ before IRQ so let's make sure we clear the IRQ
			WaitReady (padapter);
			return 0;			
			}
		}
	return status;
	}
/****************************************************************
 *	Name:	DecodeError	:LOCAL
 *
 *	Description:	Decode and process device errors.
 *
 *	Parameters:		padapter - Pointer to adapter data.
 *					status - Status register code.
 *
 *	Returns:		The driver status code.
 *
 ****************************************************************/
static ULONG DecodeError (PADAPTER2220I	padapter, UCHAR status)
	{
	UCHAR			error;

	padapter->expectingIRQ = 0;
	if ( status & IDE_STATUS_WRITE_FAULT )
		{
		return DID_PARITY << 16;
		}
	if ( status & IDE_STATUS_BUSY )
		return DID_BUS_BUSY << 16;

	error = inb_p (padapter->regError);
	DEB(printk ("\npci2220i error register: %x", error));
	switch ( error )
		{
		case IDE_ERROR_AMNF:
		case IDE_ERROR_TKONF:
		case IDE_ERROR_ABRT:
		case IDE_ERROR_IDFN:
		case IDE_ERROR_UNC:
		case IDE_ERROR_BBK:
		default:
			return DID_ERROR << 16;
		}
	return DID_ERROR << 16;
	}
/****************************************************************
 *	Name:	StartTimer	:LOCAL
 *
 *	Description:	Start the timer.
 *
 *	Parameters:		ipadapter - Pointer adapter data structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void StartTimer (PADAPTER2220I padapter)
	{
	padapter->timer.expires = jiffies + TIMEOUT_DATA;
	add_timer (&padapter->timer);
	}
/****************************************************************
 *	Name:	WriteSignature	:LOCAL
 *
 *	Description:	Start the timer.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to our device.
 *					spigot	 - Selected spigot.
 *					index	 - index of mirror signature on device.
 *
 *	Returns:		TRUE on any error.
 *
 ****************************************************************/
static int WriteSignature (PADAPTER2220I padapter, POUR_DEVICE pdev, UCHAR spigot, int index)
	{
	ULONG	zl;

	SelectSpigot (padapter, spigot);
	zl = pdev->lastsectorlba[index];
	outb_p (pdev->byte6 | ((UCHAR *)&zl)[3], padapter->regLba24);		
	outb_p (((UCHAR *)&zl)[2], padapter->regLba16);
	outb_p (((UCHAR *)&zl)[1], padapter->regLba8);
	outb_p (((UCHAR *)&zl)[0], padapter->regLba0);
	outb_p (1, padapter->regSectCount);

	WriteCommand (padapter, IDE_COMMAND_WRITE);	
	if ( WaitDrq (padapter) )
		return TRUE;
	StartTimer (padapter);	
	padapter->expectingIRQ = TRUE;
	
	((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[0] = ((ULONG *)(&pdev->DiskMirror[index]))[0];
	((ULONG *)(&padapter->kBuffer[DISK_MIRROR_POSITION]))[1] = ((ULONG *)(&pdev->DiskMirror[index]))[1];
	outsw (padapter->regData, padapter->kBuffer, BYTES_PER_SECTOR / 2);
	return FALSE;
	}
/*******************************************************************************************************
 *	Name:			InitFailover
 *
 *	Description:	This is the beginning of the failover routine
 *
 *	Parameters:		SCpnt	 - Pointer to SCSI command structure.
 *					padapter - Pointer adapter data structure.
 *					pdev	 - Pointer to our device.
 *	
 *	Returns:		TRUE on error.
 *
 ******************************************************************************************************/
static int InitFailover (PADAPTER2220I padapter, POUR_DEVICE pdev)
	{
	UCHAR	spigot;
	
	DEB (printk ("\npci2220i:  Initialize failover process - survivor = %d", pdev->deviceID[padapter->survivor]));
	pdev->raid = FALSE;									//initializes system for non raid mode
	pdev->reconOn = FALSE;
	spigot = pdev->spigots[padapter->survivor];	

	if ( pdev->DiskMirror[padapter->survivor].status & UCBF_REBUILD )
		{
		DEB (printk ("\n         failed, is survivor"));
		return (TRUE); 
		}

	if ( HardReset (padapter, pdev, spigot) )
		{
		DEB (printk ("\n         failed, reset"));
		return TRUE;
		}

	Alarm (padapter, pdev->deviceID[padapter->survivor ^ 1]);
	pdev->DiskMirror[padapter->survivor].status = UCBF_MIRRORED | UCBF_SURVIVOR;	//clear present status
	
	if ( WriteSignature (padapter, pdev, spigot, padapter->survivor) )
		{
		DEB (printk ("\n         failed, write signature"));
		return TRUE;
		}
	padapter->failinprog = TRUE;
	return FALSE;
	}
/****************************************************************
 *	Name:	TimerExpiry	:LOCAL
 *
 *	Description:	Timer expiry routine.
 *
 *	Parameters:		data - Pointer adapter data structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void TimerExpiry (unsigned long data)
	{
	PADAPTER2220I	padapter = (PADAPTER2220I)data;
	struct Scsi_Host *host = padapter->SCpnt->device->host;
	POUR_DEVICE		pdev = padapter->pdev;
	UCHAR			status = IDE_STATUS_BUSY;
	UCHAR			temp, temp1;
    unsigned long		flags;

    /*
     * Disable interrupts, if they aren't already disabled and acquire
     * the I/O spinlock.
     */
    spin_lock_irqsave (host->host_lock, flags);
	DEB (printk ("\nPCI2220I: Timeout expired "));

	if ( padapter->failinprog )
		{
		DEB (printk ("in failover process"));
		OpDone (padapter, DecodeError (padapter, inb_p (padapter->regStatCmd)));
		goto timerExpiryDone;
		}
	
	while ( padapter->reconPhase )
		{
		DEB (printk ("in recon phase %X", padapter->reconPhase));
		switch ( padapter->reconPhase )
			{
			case RECON_PHASE_MARKING:
			case RECON_PHASE_LAST:
				padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 1 : 0;
				DEB (printk ("\npci2220i: FAILURE 1"));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;
			
			case RECON_PHASE_READY:
				OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;

			case RECON_PHASE_COPY:
				padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
				DEB (printk ("\npci2220i: FAILURE 2"));
				DEB (printk ("\n       spig/stat = %X", inb_p (padapter->regStatSel));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;

			case RECON_PHASE_UPDATE:
				padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
				DEB (printk ("\npci2220i: FAILURE 3")));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;

			case RECON_PHASE_END:
				padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
				DEB (printk ("\npci2220i: FAILURE 4"));
				if ( InitFailover (padapter, pdev) )
					OpDone (padapter, DID_ERROR << 16);
				goto timerExpiryDone;
			
			default:
				goto timerExpiryDone;
			}
		}
	
	while ( padapter->cmd )
		{
		outb_p (0x08, padapter->regDmaCmdStat);					// cancel interrupt from DMA engine
		if ( pdev->raid )
			{
			if ( padapter->cmd == WRITE_CMD )
				{
				DEB (printk ("in RAID write operation"));
				temp = ( pdev->spigot & (SEL_1 | SEL_2) ) ? SEL_1 : SEL_3;
				if ( inb_p (padapter->regStatSel) & temp )
					{
					DEB (printk ("\npci2220i: Determined A OK"));
					SelectSpigot (padapter, temp | SEL_IRQ_OFF); // Masking the interrupt during spigot select
					temp = inb_p (padapter->regStatCmd);
					}
				else
					temp = IDE_STATUS_BUSY;

				temp1 = ( pdev->spigot & (SEL_1 | SEL_2) ) ? SEL_2 : SEL_4;
				if ( inb (padapter->regStatSel) & temp1 )
					{
					DEB (printk ("\npci2220i: Determined B OK"));
					SelectSpigot (padapter, temp1 | SEL_IRQ_OFF); // Masking the interrupt during spigot select
					temp1 = inb_p (padapter->regStatCmd);
					}
				else
					temp1 = IDE_STATUS_BUSY;
			
				if ( (temp & IDE_STATUS_BUSY) || (temp1 & IDE_STATUS_BUSY) )
					{
					DEB (printk ("\npci2220i: Status A: %X   B: %X", temp & 0xFF, temp1 & 0xFF));
	 				if ( (temp & IDE_STATUS_BUSY) && (temp1 & IDE_STATUS_BUSY) ) 
						{
						status = temp;
						break;
						}		
					else	
						{
						if ( temp & IDE_STATUS_BUSY )
							padapter->survivor = 1;
						else
							padapter->survivor = 0;
						if ( InitFailover (padapter, pdev) )
							{
							status = inb_p (padapter->regStatCmd);
							break;
							}
						goto timerExpiryDone;
						}
					}
				}
			else
				{
				DEB (printk ("in RAID read operation"));
				padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
				DEB (printk ("\npci2220i: FAILURE 6"));
				if ( InitFailover (padapter, pdev) )
					{
					status = inb_p (padapter->regStatCmd);
					break;
					}
				goto timerExpiryDone;
				}
			}
		else
			{
			DEB (printk ("in I/O operation"));
			status = inb_p (padapter->regStatCmd);
			}
		break;
		}
	
	OpDone (padapter, DecodeError (padapter, status));

timerExpiryDone:;
    /*
     * Release the I/O spinlock and restore the original flags
     * which will enable interrupts if and only if they were
     * enabled on entry.
     */
    spin_unlock_irqrestore (host->host_lock, flags);
	}
/****************************************************************
 *	Name:			SetReconstruct	:LOCAL
 *
 *	Description:	Set the reconstruct up.
 *
 *	Parameters:		pdev	- Pointer to device structure.
 *					index	- Mirror index number.
 *
 *	Returns:		Number of sectors on new disk required.
 *
 ****************************************************************/
static LONG SetReconstruct (POUR_DEVICE pdev, int index)
	{
	pdev->DiskMirror[index].status = UCBF_MIRRORED;							// setup the flags
	pdev->DiskMirror[index ^ 1].status = UCBF_MIRRORED | UCBF_REBUILD;
	pdev->DiskMirror[index ^ 1].reconstructPoint = 0;						// start the reconstruct
	pdev->reconCount = 1990;												// mark target drive early
	return pdev->DiskMirror[index].reconstructPoint;
	}
/****************************************************************
 *	Name:	ReconTimerExpiry	:LOCAL
 *
 *	Description:	Reconstruct timer expiry routine.
 *
 *	Parameters:		data - Pointer adapter data structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void ReconTimerExpiry (unsigned long data)
	{
	PADAPTER2220I	padapter = (PADAPTER2220I)data;
	struct Scsi_Host *host = padapter->SCpnt->device->host;
	POUR_DEVICE		pdev;
	ULONG			testsize = 0;
	PIDENTIFY_DATA	pid;
	USHORT			minmode;
	ULONG			zl;
	UCHAR			zc;
	USHORT			z;
    unsigned long	flags;

    /*
     * Disable interrupts, if they aren't already disabled and acquire
     * the I/O spinlock.
     */
    spin_lock_irqsave(host->host_lock, flags);

	if ( padapter->SCpnt )
		goto reconTimerExpiry;

	padapter->reconTimer.data = 0;
	for ( z = padapter->devInReconIndex + 1;  z < BIGD_MAXDRIVES;  z++ )
		{
		if ( padapter->device[z].reconOn )
			break;
		}
	if ( z < BIGD_MAXDRIVES )
		pdev = &padapter->device[z];
	else
		{
		for ( z = 0;  z < BIGD_MAXDRIVES;  z++ )
			{
			if ( padapter->device[z].reconOn )
				break;
			}
		if ( z < BIGD_MAXDRIVES )
			pdev = &padapter->device[z];
		else
			{
			padapter->reconOn = FALSE;
			goto reconTimerExpiry;
			}
		}

	padapter->devInReconIndex = z;
	pid = (PIDENTIFY_DATA)padapter->kBuffer;
	padapter->pdev = pdev;
	if ( pdev->reconIsStarting )
		{
		pdev->reconIsStarting = FALSE;
		pdev->reconOn = FALSE;

		while ( (pdev->DiskMirror[0].signature == SIGNATURE) && (pdev->DiskMirror[1].signature == SIGNATURE) &&
			 (pdev->DiskMirror[0].pairIdentifier == (pdev->DiskMirror[1].pairIdentifier ^ 1)) )
			{
			if ( (pdev->DiskMirror[0].status & UCBF_MATCHED) && (pdev->DiskMirror[1].status & UCBF_MATCHED) )
				break;

			if ( pdev->DiskMirror[0].status & UCBF_SURVIVOR )				// is first drive survivor?
				testsize = SetReconstruct (pdev, 0);
			else
				if ( pdev->DiskMirror[1].status & UCBF_SURVIVOR )			// is second drive survivor?
					testsize = SetReconstruct (pdev, 1);

			if ( (pdev->DiskMirror[0].status & UCBF_REBUILD) || (pdev->DiskMirror[1].status & UCBF_REBUILD) )
				{
				if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
					pdev->mirrorRecon = 0;
				else
					pdev->mirrorRecon = 1;
				pdev->reconOn = TRUE;
				}
			break;
			}

		if ( !pdev->reconOn )
			goto reconTimerExpiry;

		if ( padapter->bigD )
			{
			padapter->failRegister = 0;
			outb_p (~padapter->failRegister, padapter->regFail);
			}
		else
			{
			zc = ((inb_p (padapter->regStatSel) >> 3) | inb_p (padapter->regStatSel)) & 0x83;		// mute the alarm
			outb_p (0xFF, padapter->regFail);
			}

		while ( 1 )
			{
			DEB (printk ("\npci2220i: hard reset issue"));
			if ( HardReset (padapter, pdev, pdev->spigots[pdev->mirrorRecon]) )
				{
				DEB (printk ("\npci2220i: sub 1"));
				break;
				}

			pdev->lastsectorlba[pdev->mirrorRecon] = InlineIdentify (padapter, pdev->spigots[pdev->mirrorRecon], pdev->deviceID[pdev->mirrorRecon] & 1);

			if ( pdev->lastsectorlba[pdev->mirrorRecon] < testsize )
				{
				DEB (printk ("\npci2220i: sub 2 %ld %ld", pdev->lastsectorlba[pdev->mirrorRecon], testsize));
				break;
				}

	        // test LBA and multiper sector transfer compatibility
			if (!pid->SupportLBA || (pid->NumSectorsPerInt < SECTORSXFER) || !pid->Valid_64_70 )
				{
				DEB (printk ("\npci2220i: sub 3"));
				break;
				}

	        // test PIO/bus matering mode compatibility
			if ( (pid->MinPIOCycleWithoutFlow > 240) && !pid->SupportIORDYDisable && !padapter->timingPIO )
				{
				DEB (printk ("\npci2220i: sub 4"));
				break;
				}

			if ( pid->MinPIOCycleWithoutFlow <= 120 )	// setup timing mode of drive
				minmode = 5;
			else
				{
				if ( pid->MinPIOCylceWithFlow <= 150 )
					minmode = 4;
				else
					{
					if ( pid->MinPIOCylceWithFlow <= 180 )
						minmode = 3;
					else
						{
						if ( pid->MinPIOCylceWithFlow <= 240 )
							minmode = 2;
						else
							{
							DEB (printk ("\npci2220i: sub 5"));
							break;
							}
						}
					}
				}

			if ( padapter->timingMode > minmode )									// set minimum timing mode
				padapter->timingMode = minmode;
			if ( padapter->timingMode >= 2 )
				padapter->timingAddress	= ModeArray[padapter->timingMode - 2];
			else
				padapter->timingPIO = TRUE;

			padapter->reconOn = TRUE;
			break;
			}

		if ( !pdev->reconOn )
			{		
			padapter->survivor = pdev->mirrorRecon ^ 1;
			padapter->reconPhase = RECON_PHASE_FAILOVER;
			DEB (printk ("\npci2220i: FAILURE 7"));
			InitFailover (padapter, pdev);
			goto reconTimerExpiry;
			}

		pdev->raid = TRUE;
	
		if ( WriteSignature (padapter, pdev, pdev->spigot, pdev->mirrorRecon ^ 1) )
			goto reconTimerExpiry;
		padapter->reconPhase = RECON_PHASE_MARKING;
		goto reconTimerExpiry;
		}

	//**********************************
	// reconstruct copy starts here	
	//**********************************
	if ( pdev->reconCount++ > 2000 )
		{
		pdev->reconCount = 0;
		if ( WriteSignature (padapter, pdev, pdev->spigots[pdev->mirrorRecon], pdev->mirrorRecon) )
			{
			padapter->survivor = pdev->mirrorRecon ^ 1;
			padapter->reconPhase = RECON_PHASE_FAILOVER;
			DEB (printk ("\npci2220i: FAILURE 8"));
			InitFailover (padapter, pdev);
			goto reconTimerExpiry;
			}
		padapter->reconPhase = RECON_PHASE_UPDATE;
		goto reconTimerExpiry;
		}

	zl = pdev->DiskMirror[pdev->mirrorRecon].reconstructPoint;	
	padapter->reconSize = pdev->DiskMirror[pdev->mirrorRecon ^ 1].reconstructPoint - zl;
	if ( padapter->reconSize > MAX_BUS_MASTER_BLOCKS )
		padapter->reconSize = MAX_BUS_MASTER_BLOCKS;

	if ( padapter->reconSize )
		{
		SelectSpigot (padapter, pdev->spigots[0] | pdev->spigots[1]);	// select the spigots
		outb_p (pdev->byte6 | ((UCHAR *)(&zl))[3], padapter->regLba24);	// select the drive
		SelectSpigot (padapter, pdev->spigot);
		if ( WaitReady (padapter) )
			goto reconTimerExpiry;

		SelectSpigot (padapter, pdev->spigots[pdev->mirrorRecon]);
		if ( WaitReady (padapter) )
			{
			padapter->survivor = pdev->mirrorRecon ^ 1;
			padapter->reconPhase = RECON_PHASE_FAILOVER;
			DEB (printk ("\npci2220i: FAILURE 9"));
			InitFailover (padapter, pdev);
			goto reconTimerExpiry;
			}
	
		SelectSpigot (padapter, pdev->spigots[0] | pdev->spigots[1]);
		outb_p (padapter->reconSize & 0xFF, padapter->regSectCount);
		outb_p (((UCHAR *)(&zl))[0], padapter->regLba0);
		outb_p (((UCHAR *)(&zl))[1], padapter->regLba8);
		outb_p (((UCHAR *)(&zl))[2], padapter->regLba16);
		padapter->expectingIRQ = TRUE;
		padapter->reconPhase = RECON_PHASE_READY;
		SelectSpigot (padapter, pdev->spigots[pdev->mirrorRecon]);
		WriteCommand (padapter, WRITE_CMD);
		StartTimer (padapter);
		SelectSpigot (padapter, pdev->spigot);
		WriteCommand (padapter, READ_CMD);
		goto reconTimerExpiry;
		}

	pdev->DiskMirror[pdev->mirrorRecon].status = UCBF_MIRRORED | UCBF_MATCHED;
	pdev->DiskMirror[pdev->mirrorRecon ^ 1].status = UCBF_MIRRORED | UCBF_MATCHED;
	if ( WriteSignature (padapter, pdev, pdev->spigot, pdev->mirrorRecon ^ 1) )
		goto reconTimerExpiry;
	padapter->reconPhase = RECON_PHASE_LAST;

reconTimerExpiry:;
    /*
     * Release the I/O spinlock and restore the original flags
     * which will enable interrupts if and only if they were
     * enabled on entry.
     */
    spin_unlock_irqrestore(host->host_lock, flags);
	}
/****************************************************************
 *	Name:	Irq_Handler	:LOCAL
 *
 *	Description:	Interrupt handler.
 *
 *	Parameters:		irq		- Hardware IRQ number.
 *					dev_id	-
 *					regs	-
 *
 *	Returns:		TRUE if drive is not ready in time.
 *
 ****************************************************************/
static irqreturn_t Irq_Handler (int irq, void *dev_id, struct pt_regs *regs)
	{
	struct Scsi_Host   *shost = NULL;	// Pointer to host data block
	PADAPTER2220I		padapter;		// Pointer to adapter control structure
	POUR_DEVICE			pdev;
	Scsi_Cmnd		   *SCpnt;
	UCHAR				status;
	UCHAR				status1;
	ATAPI_STATUS		statusa;
	ATAPI_REASON		reasona;
	ATAPI_ERROR			errora;
	int					z;
	ULONG				zl;
    unsigned long		flags;
    int handled = 0;

//	DEB (printk ("\npci2220i received interrupt\n"));

	for ( z = 0; z < NumAdapters;  z++ )								// scan for interrupt to process
		{
		if ( PsiHost[z]->irq == (UCHAR)(irq & 0xFF) )
			{
			if ( inw_p (HOSTDATA(PsiHost[z])->regIrqControl) & 0x8000 )
				{
				shost = PsiHost[z];
				break;
				}
			}
		}

	if ( !shost )
		{
		DEB (printk ("\npci2220i: not my interrupt"));
		goto out;
		}

	handled = 1;
	spin_lock_irqsave(shost->host_lock, flags);
	padapter = HOSTDATA(shost);
	pdev = padapter->pdev;
	SCpnt = padapter->SCpnt;
	outb_p (0x08, padapter->regDmaCmdStat);									// cancel interrupt from DMA engine

	if ( padapter->atapi && SCpnt )
		{
		*(char *)&statusa = inb_p (padapter->regStatCmd);						// read the device status
		*(char *)&reasona = inb_p (padapter->regSectCount);						// read the device interrupt reason
	
		if ( !statusa.bsy )
			{
			if ( statusa.drq )													// test for transfer phase
				{
				if ( !reasona.cod )												// test for data phase
					{
					z = (ULONG)inb_p (padapter->regLba8) | (ULONG)(inb_p (padapter->regLba16) << 8);
					if ( padapter->reqSense )
						insw (padapter->regData, SCpnt->sense_buffer, z / 2);
					else
						AtapiBusMaster (padapter, reasona.io, z);
					goto irq_return;
					}
				if ( reasona.cod && !reasona.io )								// test for command packet phase
					{
					if ( padapter->reqSense )
						AtapiRequestSense (padapter, pdev, SCpnt, TRUE);
					else
						AtapiSendCdb (padapter, pdev, padapter->atapiCdb);
					goto irq_return;
					}
				}
			else
				{
				if ( reasona.io && statusa.drdy )								// test for status phase
					{
					Atapi2Scsi (padapter, SCpnt);
					if ( statusa.check )
						{
						*(UCHAR *)&errora = inb_p (padapter->regError);			// read the device error
						if ( errora.senseKey )
							{
							if ( padapter->reqSense || AtapiRequestSense (padapter, pdev, SCpnt, FALSE) )
								OpDone (padapter, DID_ERROR << 16);
							}
						else
							{
							if ( errora.ili || errora.abort )
								OpDone (padapter, DID_ERROR << 16);
							else
								OpDone (padapter, DID_OK << 16);	
							}
						}
					else
						if ( padapter->reqSense )
							{
							DEB (printk ("PCI2242I: Sense codes - %X %X %X ", ((UCHAR *)SCpnt->sense_buffer)[0], ((UCHAR *)SCpnt->sense_buffer)[12], ((UCHAR *)SCpnt->sense_buffer)[13]));
							OpDone (padapter, (DRIVER_SENSE << 24) | (DID_OK << 16) | 2);
							}
						else
							OpDone (padapter, DID_OK << 16);	
					}
				}
			}		
		goto irq_return;
		}
	
	if ( !padapter->expectingIRQ || !(SCpnt || padapter->reconPhase) )
		{
		DEB(printk ("\npci2220i Unsolicited interrupt\n"));
		STOP_HERE ();
		goto irq_return;
		}
	padapter->expectingIRQ = 0;

	if ( padapter->failinprog )
		{
		DEB (printk ("\npci2220i interrupt failover complete"));
		padapter->failinprog = FALSE;
		status = inb_p (padapter->regStatCmd);								// read the device status
		if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
			{
			DEB (printk ("\npci2220i: interrupt failover error from drive %X", status));
			padapter->cmd = 0;
			}
		else
			{
			DEB (printk ("\npci2220i: restarting failed opertation."));
			pdev->spigot = (padapter->survivor) ? pdev->spigots[1] : pdev->spigots[0];
			del_timer (&padapter->timer);
			if ( padapter->reconPhase )
				OpDone (padapter, DID_OK << 16);
			else
				Pci2220i_QueueCommand (SCpnt, SCpnt->scsi_done);
			goto irq_return;		
			}
		}

	if ( padapter->reconPhase )
		{
		switch ( padapter->reconPhase )
			{
			case RECON_PHASE_MARKING:
			case RECON_PHASE_LAST:
				status = inb_p (padapter->regStatCmd);						// read the device status
				del_timer (&padapter->timer);
				if ( padapter->reconPhase == RECON_PHASE_LAST )
					{
					if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
						{
						padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 1 : 0;
						DEB (printk ("\npci2220i: FAILURE 10"));
						if ( InitFailover (padapter, pdev) )
							OpDone (padapter, DecodeError (padapter, status));
						goto irq_return;
						}
					if ( WriteSignature (padapter, pdev, pdev->spigots[pdev->mirrorRecon], pdev->mirrorRecon) )
						{
						padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
						DEB (printk ("\npci2220i: FAILURE 11"));
						if ( InitFailover (padapter, pdev) )
							OpDone (padapter, DecodeError (padapter, status));
						goto irq_return;
						}
					padapter->reconPhase = RECON_PHASE_END;	
					goto irq_return;
					}
				OpDone (padapter, DID_OK << 16);
				goto irq_return;

			case RECON_PHASE_READY:
				status = inb_p (padapter->regStatCmd);						// read the device status
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					del_timer (&padapter->timer);
					OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				SelectSpigot (padapter, pdev->spigots[pdev->mirrorRecon]);
				if ( WaitDrq (padapter) )
					{
					del_timer (&padapter->timer);
					padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
					DEB (printk ("\npci2220i: FAILURE 12"));
					if ( InitFailover (padapter, pdev) )
						OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				SelectSpigot (padapter, pdev->spigot | SEL_COPY | padapter->bigD);
				padapter->reconPhase = RECON_PHASE_COPY;
				padapter->expectingIRQ = TRUE;
				if ( padapter->timingPIO )
					{
					insw (padapter->regData, padapter->kBuffer, padapter->reconSize * (BYTES_PER_SECTOR / 2));
					}
				else
					{
					if ( (padapter->timingMode > 3) )
						{
						if ( padapter->bigD )
							outl (BIGD_DATA_MODE3, padapter->regDmaAddrLoc);
						else
							outl (DALE_DATA_MODE3, padapter->regDmaAddrLoc);
						}
					else
						outl (padapter->timingAddress, padapter->regDmaAddrLoc);
					outl (padapter->kBufferDma, padapter->regDmaAddrPci);
					outl (padapter->reconSize * BYTES_PER_SECTOR, padapter->regDmaCount);
					outb_p (8, padapter->regDmaDesc);						// read operation
					if ( padapter->bigD )
						outb_p (8, padapter->regDmaMode);					// no interrupt
					else
						outb_p (1, padapter->regDmaMode);					// no interrupt
					outb_p (0x03, padapter->regDmaCmdStat);					// kick the DMA engine in gear
					}
				goto irq_return;

			case RECON_PHASE_COPY:
				pdev->DiskMirror[pdev->mirrorRecon].reconstructPoint += padapter->reconSize;

			case RECON_PHASE_UPDATE:
				SelectSpigot (padapter, pdev->spigots[pdev->mirrorRecon] | SEL_IRQ_OFF);
				status = inb_p (padapter->regStatCmd);						// read the device status
				del_timer (&padapter->timer);
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
					DEB (printk ("\npci2220i: FAILURE 13"));
					DEB (printk ("\n  status register = %X   error = %X", status, inb_p (padapter->regError)));
					if ( InitFailover (padapter, pdev) )
						OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				OpDone (padapter, DID_OK << 16);
				goto irq_return;

			case RECON_PHASE_END:
				status = inb_p (padapter->regStatCmd);						// read the device status
				del_timer (&padapter->timer);
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 0 : 1;
					DEB (printk ("\npci2220i: FAILURE 14"));
					if ( InitFailover (padapter, pdev) )
						OpDone (padapter, DecodeError (padapter, status));
					goto irq_return;
					}
				pdev->reconOn = 0;
				if ( padapter->bigD )
					{
					for ( z = 0;  z < padapter->numberOfDrives;  z++ )
						{
						if ( padapter->device[z].DiskMirror[0].status & UCBF_SURVIVOR )
							{
							Alarm (padapter, padapter->device[z].deviceID[0] ^ 2);
							MuteAlarm (padapter);
							}
						if ( padapter->device[z].DiskMirror[1].status & UCBF_SURVIVOR )
							{
							Alarm (padapter, padapter->device[z].deviceID[1] ^ 2);
							MuteAlarm (padapter);
							}
						}
					}
				OpDone (padapter, DID_OK << 16);
				goto irq_return;

			default:
				goto irq_return;
			}
		}
		
	switch ( padapter->cmd )												// decide how to handle the interrupt
		{
		case READ_CMD:
			if ( padapter->sectorCount )
				{
				status = inb_p (padapter->regStatCmd);						// read the device status
				if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{
					if ( pdev->raid )
						{
						padapter->survivor = ( pdev->spigot == pdev->spigots[0] ) ? 1 : 0;
						del_timer (&padapter->timer);
						DEB (printk ("\npci2220i: FAILURE 15"));
						if ( !InitFailover (padapter, pdev) )
							goto irq_return;
						}
					break;	
					}
				if ( padapter->timingPIO )
					{
					insw (padapter->regData, padapter->kBuffer, padapter->readCount / 2);
					padapter->sectorCount -= padapter->readCount / BYTES_PER_SECTOR;
					WalkScatGath (padapter, TRUE, padapter->readCount);
					if ( !padapter->sectorCount )
						{
						status = 0;
						break;
						}
					}
				else
					{
					if ( padapter->readCount )
						WalkScatGath (padapter, TRUE, padapter->readCount);
					BusMaster (padapter, 1, 1);
					}
				padapter->expectingIRQ = TRUE;
				goto irq_return;
				}
			if ( padapter->readCount && !padapter->timingPIO )
				WalkScatGath (padapter, TRUE, padapter->readCount);
			status = 0;
			break;

		case WRITE_CMD:
			if ( pdev->raid )
				{
				SelectSpigot (padapter, pdev->spigots[0] | SEL_IRQ_OFF);				
				status = inb_p (padapter->regStatCmd);								// read the device status
				SelectSpigot (padapter, pdev->spigots[1] | SEL_IRQ_OFF);				
				status1 = inb_p (padapter->regStatCmd);								// read the device status
				}
			else
				SelectSpigot (padapter, pdev->spigot | SEL_IRQ_OFF);				
				status = inb_p (padapter->regStatCmd);								// read the device status
				status1 = 0;
		
			if ( status & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
				{	
				if ( pdev->raid && !(status1 & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT)) )
					{
					padapter->survivor = 1;
					del_timer (&padapter->timer);
					SelectSpigot (padapter, pdev->spigot | SEL_IRQ_OFF);
					DEB (printk ("\npci2220i: FAILURE 16  status = %X  error = %X", status, inb_p (padapter->regError)));
					if ( !InitFailover (padapter, pdev) )
						goto irq_return;
					}
				break;
				}
			if ( pdev->raid )
				{
				if ( status1 & (IDE_STATUS_ERROR | IDE_STATUS_WRITE_FAULT) )
					{	
					padapter->survivor = 0;
					del_timer (&padapter->timer);
					DEB (printk ("\npci2220i: FAILURE 17  status = %X  error = %X", status1, inb_p (padapter->regError)));
					if ( !InitFailover (padapter, pdev) )
						goto irq_return;
					status = status1;
					break;
					}
				if ( padapter->sectorCount )
					{
					status = WriteDataBoth (padapter, pdev);
					if ( status )
						{
						padapter->survivor = status >> 1;
						del_timer (&padapter->timer);
						DEB (printk ("\npci2220i: FAILURE 18"));
						if ( !InitFailover (padapter, pdev) )
							goto irq_return;
						SelectSpigot (padapter, pdev->spigots[status] | SEL_IRQ_OFF);				
						status = inb_p (padapter->regStatCmd);								// read the device status
						break;
						}
					padapter->expectingIRQ = TRUE;
					goto irq_return;
					}
				status = 0;
				break;
				}
			if ( padapter->sectorCount )	
				{	
				SelectSpigot (padapter, pdev->spigot | padapter->bigD);
				status = WriteData (padapter);
				if ( status )
					break;
				padapter->expectingIRQ = TRUE;
				goto irq_return;
				}
			status = 0;
			break;

		case IDE_COMMAND_IDENTIFY:
			{
			PINQUIRYDATA	pinquiryData  = SCpnt->request_buffer;
			PIDENTIFY_DATA	pid = (PIDENTIFY_DATA)padapter->kBuffer;

			status = inb_p (padapter->regStatCmd);
			if ( status & IDE_STATUS_DRQ )
				{
				insw (padapter->regData, pid, sizeof (IDENTIFY_DATA) >> 1);

				memset (pinquiryData, 0, SCpnt->request_bufflen);		// Zero INQUIRY data structure.
				pinquiryData->DeviceType = 0;
				pinquiryData->Versions = 2;
				pinquiryData->AdditionalLength = 35 - 4;

				// Fill in vendor identification fields.
				for ( z = 0;  z < 20;  z += 2 )
					{
					pinquiryData->VendorId[z]	  = ((UCHAR *)pid->ModelNumber)[z + 1];
					pinquiryData->VendorId[z + 1] = ((UCHAR *)pid->ModelNumber)[z];
					}

				// Initialize unused portion of product id.
				for ( z = 0;  z < 4;  z++ )
					pinquiryData->ProductId[12 + z] = ' ';

				// Move firmware revision from IDENTIFY data to
				// product revision in INQUIRY data.
				for ( z = 0;  z < 4;  z += 2 )
					{
					pinquiryData->ProductRevisionLevel[z]	 = ((UCHAR *)pid->FirmwareRevision)[z + 1];
					pinquiryData->ProductRevisionLevel[z + 1] = ((UCHAR *)pid->FirmwareRevision)[z];
					}
				if ( pdev == padapter->device )
					*((USHORT *)(&pinquiryData->VendorSpecific)) = DEVICE_DALE_1;
				
				status = 0;
				}
			break;
			}

		default:
			status = 0;
			break;
		}

	del_timer (&padapter->timer);
	if ( status )
		{
		DEB (printk ("\npci2220i Interrupt handler return error"));
		zl = DecodeError (padapter, status);
		}
	else
		zl = DID_OK << 16;

	OpDone (padapter, zl);
irq_return:
    spin_unlock_irqrestore(shost->host_lock, flags);
out:
	return IRQ_RETVAL(handled);
}

/****************************************************************
 *	Name:	Pci2220i_QueueCommand
 *
 *	Description:	Process a queued command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					done  - Pointer to done function to call.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2220i_QueueCommand (Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
	{
	UCHAR		   *cdb = (UCHAR *)SCpnt->cmnd;					// Pointer to SCSI CDB
	PADAPTER2220I	padapter = HOSTDATA(SCpnt->device->host);			// Pointer to adapter control structure
	POUR_DEVICE		pdev	 = &padapter->device[SCpnt->device->id];// Pointer to device information
	UCHAR			rc;											// command return code
	int				z; 
	PDEVICE_RAID1	pdr;

	SCpnt->scsi_done = done;
	padapter->SCpnt = SCpnt;  									// Save this command data
	padapter->readCount = 0;

	if ( SCpnt->use_sg )
		{
		padapter->currentSgBuffer = ((struct scatterlist *)SCpnt->request_buffer)[0].address;
		padapter->currentSgCount = ((struct scatterlist *)SCpnt->request_buffer)[0].length;
		}
	else
		{
		padapter->currentSgBuffer = SCpnt->request_buffer;
		padapter->currentSgCount = SCpnt->request_bufflen;
		}
	padapter->nextSg = 1;

	if ( !done )
		{
		printk("pci2220i_queuecommand: %02X: done can't be NULL\n", *cdb);
		return 0;
		}
	
	if ( padapter->atapi )
		{
		UCHAR			zlo, zhi;

		DEB (printk ("\nPCI2242I: ID %d, LUN %d opcode %X ", SCpnt->device->id, SCpnt->device->lun, *cdb));
		padapter->pdev = pdev;
		if ( !pdev->byte6 || SCpnt->device->lun )
			{
			OpDone (padapter, DID_BAD_TARGET << 16);
			return 0;
			}
	
		padapter->atapiSpecial = FALSE;
		padapter->reqSense = FALSE;
		memset (padapter->atapiCdb, 0, 16);
		SelectSpigot (padapter, pdev->spigot);									// select the spigot
		AtapiDevice (padapter, pdev->byte6);									// select the drive
		if ( AtapiWaitReady (padapter, 100) )
			{
			OpDone (padapter, DID_NO_CONNECT << 16);
			return 0;
			}

		switch ( cdb[0] ) 
			{
			case SCSIOP_MODE_SENSE:
			case SCSIOP_MODE_SELECT:
				Scsi2Atapi (padapter, SCpnt);
				z = SCpnt->request_bufflen + 4;
				break;
			case SCSIOP_READ6:
			case SCSIOP_WRITE6:
				Scsi2Atapi (padapter, SCpnt);
				z = SCpnt->request_bufflen;
				break;
			default:
				memcpy (padapter->atapiCdb, cdb, SCpnt->cmd_len);
				z = SCpnt->request_bufflen;
				break;
			}
		if ( z > ATAPI_TRANSFER )
			z = ATAPI_TRANSFER;
	    zlo = (UCHAR)(z & 0xFF);
	    zhi = (UCHAR)(z >> 8);

	 	AtapiCountLo (padapter, zlo);						
	   	AtapiCountHi (padapter, zhi);						
	   	outb_p (0, padapter->regError);						
		WriteCommand (padapter, IDE_COMMAND_ATAPI_PACKET);
		if ( pdev->cmdDrqInt )
			return 0;

		if ( AtapiWaitDrq (padapter, 500) )
			{
			OpDone (padapter, DID_ERROR << 16);
			return 0;
			}
		AtapiSendCdb (padapter, pdev, padapter->atapiCdb);	
		return 0;
		}
	
	if ( padapter->reconPhase )
		return 0;
	if ( padapter->reconTimer.data )
		{
		del_timer (&padapter->reconTimer);
		padapter->reconTimer.data = 0;
		}
		
	if ( (SCpnt->device->id >= padapter->numberOfDrives) || SCpnt->device->lun )
		{
		OpDone (padapter, DID_BAD_TARGET << 16);
		return 0;
		}
	
	switch ( *cdb )
		{
		case SCSIOP_INQUIRY:   					// inquiry CDB
			{
			if ( cdb[2] == SC_MY_RAID )
				{
				switch ( cdb[3] ) 
					{
					case MY_SCSI_REBUILD:
						for ( z = 0;  z < padapter->numberOfDrives;  z++ )
							{
							pdev = &padapter->device[z];
							if ( ((pdev->DiskMirror[0].status & UCBF_SURVIVOR) && (pdev->DiskMirror[1].status & UCBF_MIRRORED)) ||
								 ((pdev->DiskMirror[1].status & UCBF_SURVIVOR) && (pdev->DiskMirror[0].status & UCBF_MIRRORED)) )
								{
								padapter->reconOn = pdev->reconOn = pdev->reconIsStarting = TRUE;
								}
							}
						OpDone (padapter, DID_OK << 16);
						break;
					case MY_SCSI_ALARMMUTE:
						MuteAlarm (padapter);
						OpDone (padapter, DID_OK << 16);
						break;
					case MY_SCSI_DEMOFAIL:
						padapter->demoFail = TRUE;				
						OpDone (padapter, DID_OK << 16);
						break;
					default:
						z = cdb[5];				// get index
						pdr = (PDEVICE_RAID1)SCpnt->request_buffer;
						if ( padapter->raidData[z] )
							{
							memcpy (&pdr->DiskRaid1, padapter->raidData[z], sizeof (DISK_MIRROR));
							if ( padapter->raidData[z]->reconstructPoint > padapter->raidData[z ^ 2]->reconstructPoint )
								pdr->TotalSectors = padapter->raidData[z]->reconstructPoint;
							else
								pdr->TotalSectors = padapter->raidData[z ^ 2]->reconstructPoint;
							}
						else
							memset (pdr, 0, sizeof (DEVICE_RAID1));
						OpDone (padapter, DID_OK << 16);
						break;
					}	
				return 0;
				}
			padapter->cmd = IDE_COMMAND_IDENTIFY;
			break;
			}

		case SCSIOP_TEST_UNIT_READY:			// test unit ready CDB
			OpDone (padapter, DID_OK << 16);
			return 0;
		case SCSIOP_READ_CAPACITY:			  	// read capctiy CDB
			{
			PREAD_CAPACITY_DATA	pdata = (PREAD_CAPACITY_DATA)SCpnt->request_buffer;

			pdata->blksiz = 0x20000;
			XANY2SCSI ((UCHAR *)&pdata->blks, pdev->blocks);
			OpDone (padapter, DID_OK << 16);
			return 0;
			}
		case SCSIOP_VERIFY:						// verify CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (UCHAR)((USHORT)cdb[8] | ((USHORT)cdb[7] << 8));
			padapter->cmd = IDE_COMMAND_VERIFY;
			break;
		case SCSIOP_READ:						// read10 CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (USHORT)cdb[8] | ((USHORT)cdb[7] << 8);
			padapter->cmd = READ_CMD;
			break;
		case SCSIOP_READ6:						// read6  CDB
			padapter->startSector = SCSI2LONG (&cdb[1]);
			padapter->sectorCount = cdb[4];
			padapter->cmd = READ_CMD;
			break;
		case SCSIOP_WRITE:						// write10 CDB
			padapter->startSector = XSCSI2LONG (&cdb[2]);
			padapter->sectorCount = (USHORT)cdb[8] | ((USHORT)cdb[7] << 8);
			padapter->cmd = WRITE_CMD;
			break;
		case SCSIOP_WRITE6:						// write6  CDB
			padapter->startSector = SCSI2LONG (&cdb[1]);
			padapter->sectorCount = cdb[4];
			padapter->cmd = WRITE_CMD;
			break;
		default:
			DEB (printk ("pci2220i_queuecommand: Unsupported command %02X\n", *cdb));
			OpDone (padapter, DID_ERROR << 16);
			return 0;
		}

	if ( padapter->reconPhase )
		return 0;
	
	padapter->pdev = pdev;

	while ( padapter->demoFail )
		{
		pdev = padapter->pdev = &padapter->device[0];
		padapter->demoFail = FALSE;
		if ( !pdev->raid || 
			 (pdev->DiskMirror[0].status & UCBF_SURVIVOR) || 
			 (pdev->DiskMirror[1].status & UCBF_SURVIVOR) )
			{
			break;
			}
		if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
			padapter->survivor = 1;
		else
			padapter->survivor = 0;
				DEB (printk ("\npci2220i: FAILURE 19"));
		if ( InitFailover (padapter, pdev) )
			break;
		return 0;
		}

	StartTimer (padapter);
	if ( pdev->raid && (padapter->cmd == WRITE_CMD) )
		{
		rc = IdeCmdBoth (padapter, pdev);
		if ( !rc )
			rc = WriteDataBoth (padapter, pdev);
		if ( rc )
			{
			del_timer (&padapter->timer);
			padapter->expectingIRQ = 0;
			padapter->survivor = rc >> 1;
				DEB (printk ("\npci2220i: FAILURE 20"));
			if ( InitFailover (padapter, pdev) )
				{
				OpDone (padapter, DID_ERROR << 16);
				return 0;
				}
			}
		}
	else
		{
		rc = IdeCmd (padapter, pdev);
		if ( (padapter->cmd == WRITE_CMD) && !rc )
			rc = WriteData (padapter);
		if ( rc )
			{
			del_timer (&padapter->timer);
			padapter->expectingIRQ = 0;
			if ( pdev->raid )
				{
				padapter->survivor = (pdev->spigot ^ 3) >> 1;
				DEB (printk ("\npci2220i: FAILURE 21"));
				if ( !InitFailover (padapter, pdev) )
					return 0;
				}
			OpDone (padapter, DID_ERROR << 16);
			return 0;
			}
		}
	return 0;
	}
/****************************************************************
 *	Name:			ReadFlash
 *
 *	Description:	Read information from controller Flash memory.
 *
 *	Parameters:		padapter - Pointer to host interface data structure.
 *					pdata	 - Pointer to data structures.
 *					base	 - base address in Flash.
 *					length	 - lenght of data space in bytes.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static VOID ReadFlash (PADAPTER2220I padapter, VOID *pdata, ULONG base, ULONG length)
	{
	ULONG	 oldremap;
	UCHAR	 olddesc;
	ULONG	 z;
	UCHAR	*pd = (UCHAR *)pdata;

	oldremap = inl (padapter->regRemap);							// save values to restore later
	olddesc  = inb_p (padapter->regDesc);

	outl (base | 1, padapter->regRemap);							// remap to Flash space as specified
	outb_p (0x40, padapter->regDesc);								// describe remap region as 8 bit
	for ( z = 0;  z < length;  z++)									// get "length" data count
		*pd++ = inb_p (padapter->regBase + z);						// read in the data

	outl (oldremap, padapter->regRemap);							// restore remap register values
	outb_p (olddesc, padapter->regDesc);
	}
/****************************************************************
 *	Name:			GetRegs
 *
 *	Description:	Initialize the regester information.
 *
 *	Parameters:		pshost		  - Pointer to SCSI host data structure.
 *					bigd		  - PCI-2240I identifier
 *					pcidev		  - Pointer to device data structure.
 *
 *	Returns:		TRUE if failure to install.
 *
 ****************************************************************/
static USHORT GetRegs (struct Scsi_Host *pshost, BOOL bigd, struct pci_dev *pcidev)
	{
	PADAPTER2220I	padapter;
	int				setirq;
	int				z;
	USHORT			zr, zl;
	UCHAR		   *consistent;
	dma_addr_t		consistentDma;

	padapter = HOSTDATA(pshost);
	memset (padapter, 0, sizeof (ADAPTER2220I));
	memset (&DaleSetup, 0, sizeof (DaleSetup));
	memset (DiskMirror, 0, sizeof (DiskMirror));

	zr = pci_resource_start (pcidev, 1);
	zl = pci_resource_start (pcidev, 2);

	padapter->basePort = zr;
	padapter->regRemap		= zr + RTR_LOCAL_REMAP;					// 32 bit local space remap
	padapter->regDesc		= zr + RTR_REGIONS;	  					// 32 bit local region descriptor
	padapter->regRange		= zr + RTR_LOCAL_RANGE;					// 32 bit local range
	padapter->regIrqControl	= zr + RTR_INT_CONTROL_STATUS;			// 16 bit interrupt control and status
	padapter->regScratchPad	= zr + RTR_MAILBOX;	  					// 16 byte scratchpad I/O base address

	padapter->regBase		= zl;
	padapter->regData		= zl + REG_DATA;						// data register I/O address
	padapter->regError		= zl + REG_ERROR;						// error register I/O address
	padapter->regSectCount	= zl + REG_SECTOR_COUNT;				// sector count register I/O address
	padapter->regLba0		= zl + REG_LBA_0;						// least significant byte of LBA
	padapter->regLba8		= zl + REG_LBA_8;						// next least significant byte of LBA
	padapter->regLba16		= zl + REG_LBA_16;						// next most significan byte of LBA
	padapter->regLba24		= zl + REG_LBA_24;						// head and most 4 significant bits of LBA
	padapter->regStatCmd	= zl + REG_STAT_CMD;					// status on read and command on write register
	padapter->regStatSel	= zl + REG_STAT_SEL;					// board status on read and spigot select on write register
	padapter->regFail		= zl + REG_FAIL;
	padapter->regAltStat	= zl + REG_ALT_STAT;
	padapter->pcidev		= pcidev;

	if ( bigd )
		{
		padapter->regDmaDesc	= zr + RTR_DMA0_DESC_PTR;			// address of the DMA discriptor register for direction of transfer
		padapter->regDmaCmdStat	= zr + RTR_DMA_COMMAND_STATUS;		// Byte #0 of DMA command status register
		padapter->regDmaAddrPci	= zr + RTR_DMA0_PCI_ADDR;			// 32 bit register for PCI address of DMA
		padapter->regDmaAddrLoc	= zr + RTR_DMA0_LOCAL_ADDR;			// 32 bit register for local bus address of DMA
		padapter->regDmaCount	= zr + RTR_DMA0_COUNT;				// 32 bit register for DMA transfer count
		padapter->regDmaMode	= zr + RTR_DMA0_MODE + 1;			// 32 bit register for DMA mode control
		padapter->bigD			= SEL_NEW_SPEED_1;					// set spigot speed control bit
		}
	else
		{
		padapter->regDmaDesc	= zl + RTL_DMA1_DESC_PTR;			// address of the DMA discriptor register for direction of transfer
		padapter->regDmaCmdStat	= zl + RTL_DMA_COMMAND_STATUS + 1;	// Byte #1 of DMA command status register
		padapter->regDmaAddrPci	= zl + RTL_DMA1_PCI_ADDR;			// 32 bit register for PCI address of DMA
		padapter->regDmaAddrLoc	= zl + RTL_DMA1_LOCAL_ADDR;			// 32 bit register for local bus address of DMA
		padapter->regDmaCount	= zl + RTL_DMA1_COUNT;				// 32 bit register for DMA transfer count
		padapter->regDmaMode	= zl + RTL_DMA1_MODE + 1;			// 32 bit register for DMA mode control
		}

	padapter->numberOfDrives = inb_p (padapter->regScratchPad + BIGD_NUM_DRIVES);
	if ( !bigd && !padapter->numberOfDrives )						// if no devices on this board
		return TRUE;

	pshost->irq = pcidev->irq;
	setirq = 1;
	for ( z = 0;  z < Installed;  z++ )								// scan for shared interrupts
		{
		if ( PsiHost[z]->irq == pshost->irq )						// if shared then, don't posses
			setirq = 0;
		}
	if ( setirq )													// if not shared, posses
		{
		if ( request_irq (pshost->irq, Irq_Handler, SA_SHIRQ, "pci2220i", padapter) < 0 )
			{
			if ( request_irq (pshost->irq, Irq_Handler, SA_INTERRUPT | SA_SHIRQ, "pci2220i", padapter) < 0 )
				{
				printk ("Unable to allocate IRQ for PCI-2220I controller.\n");
				return TRUE;
				}
			}
		padapter->irqOwned = pshost->irq;							// set IRQ as owned
		}

	if ( padapter->numberOfDrives )
		consistent = pci_alloc_consistent (pcidev, SECTORSXFER * BYTES_PER_SECTOR, &consistentDma);
	else
		consistent = pci_alloc_consistent (pcidev, ATAPI_TRANSFER, &consistentDma);
	if ( !consistent )
		{
		printk ("Unable to allocate DMA buffer for PCI-2220I controller.\n");
		free_irq (pshost->irq, padapter);
		return TRUE;
		}
	padapter->kBuffer = consistent;
	padapter->kBufferDma = consistentDma;

	PsiHost[Installed]	= pshost;									// save SCSI_HOST pointer
	pshost->io_port		= padapter->basePort;
	pshost->n_io_port	= 0xFF;
	pshost->unique_id	= padapter->regBase;

	outb_p (0x01, padapter->regRange);								// fix our range register because other drivers want to tromp on it

	padapter->timingMode = inb_p (padapter->regScratchPad + DALE_TIMING_MODE);
	if ( padapter->timingMode >= 2 )
		{
		if ( bigd )
			padapter->timingAddress	= ModeArray2[padapter->timingMode - 2];
		else
			padapter->timingAddress	= ModeArray[padapter->timingMode - 2];
		}
	else
		padapter->timingPIO = TRUE;

	ReadFlash (padapter, &DaleSetup, DALE_FLASH_SETUP, sizeof (SETUP));
	ReadFlash (padapter, &DiskMirror, DALE_FLASH_RAID, sizeof (DiskMirror));

	return FALSE;
	}
/****************************************************************
 *	Name:			SetupFinish
 *
 *	Description:	Complete the driver initialization process for a card
 *
 *	Parameters:		padapter  - Pointer to SCSI host data structure.
 *					str		  - Pointer to board type string.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
VOID SetupFinish (PADAPTER2220I padapter, char *str, int irq)
	{
	init_timer (&padapter->timer);
	padapter->timer.function = TimerExpiry;
	padapter->timer.data = (unsigned long)padapter;
	init_timer (&padapter->reconTimer);
	padapter->reconTimer.function = ReconTimerExpiry;
	padapter->reconTimer.data = (unsigned long)padapter;
	printk("\nPCI-%sI EIDE CONTROLLER: at I/O = %lX/%lX  IRQ = %d\n", str, padapter->basePort, padapter->regBase, irq);
	printk("Version %s, Compiled %s %s\n\n", PCI2220I_VERSION, __DATE__, __TIME__);
	}	
/****************************************************************
 *	Name:	Pci2220i_Detect
 *
 *	Description:	Detect and initialize our boards.
 *
 *	Parameters:		tpnt - Pointer to SCSI host template structure.
 *
 *	Returns:		Number of adapters installed.
 *
 ****************************************************************/
int Pci2220i_Detect (Scsi_Host_Template *tpnt)
	{
	struct Scsi_Host   *pshost;
	PADAPTER2220I	    padapter;
	POUR_DEVICE			pdev;
	int					unit;
	int					z;
	USHORT				raidon;
	UCHAR				spigot1, spigot2;
	UCHAR				device;
	struct pci_dev	   *pcidev = NULL;

	while ( (pcidev = pci_find_device (VENDOR_PSI, DEVICE_DALE_1, pcidev)) != NULL )
		{
		if (pci_enable_device(pcidev))
			continue;
		pshost = scsi_register (tpnt, sizeof(ADAPTER2220I));
		if(pshost==NULL)
			continue;
			
		padapter = HOSTDATA(pshost);

		if ( GetRegs (pshost, FALSE, pcidev) )
			goto unregister;

		scsi_set_device(pshost, &pcidev->dev);
		pshost->max_id = padapter->numberOfDrives;
		for ( z = 0;  z < padapter->numberOfDrives;  z++ )
			{
			unit = inb_p (padapter->regScratchPad + DALE_CHANNEL_DEVICE_0 + z) & 0x0F;
			pdev = &padapter->device[z];
			pdev->byte6		= (UCHAR)(((unit & 1) << 4) | 0xE0);
			pdev->spigot	= (UCHAR)(1 << (unit >> 1));
			pdev->sectors	= DaleSetup.setupDevice[unit].sectors;
			pdev->heads		= DaleSetup.setupDevice[unit].heads;
			pdev->cylinders = DaleSetup.setupDevice[unit].cylinders;
			pdev->blocks	= DaleSetup.setupDevice[unit].blocks;

			if ( !z )
				{
				DiskMirror[0].status = inb_p (padapter->regScratchPad + DALE_RAID_0_STATUS);		
				DiskMirror[1].status = inb_p (padapter->regScratchPad + DALE_RAID_1_STATUS);		
				if ( (DiskMirror[0].signature == SIGNATURE) && (DiskMirror[1].signature == SIGNATURE) &&
				     (DiskMirror[0].pairIdentifier == (DiskMirror[1].pairIdentifier ^ 1)) )
					{			 
					raidon = TRUE;
					if ( unit > (unit ^ 2) )
						unit = unit ^ 2;
					}	
				else
					raidon = FALSE;

				memcpy (pdev->DiskMirror, DiskMirror, sizeof (DiskMirror));
				padapter->raidData[0] = &pdev->DiskMirror[0];
				padapter->raidData[2] = &pdev->DiskMirror[1];
				
				spigot1 = spigot2 = FALSE;
				pdev->spigots[0] = 1;
				pdev->spigots[1] = 2;
				pdev->lastsectorlba[0] = InlineIdentify (padapter, 1, 0);
				pdev->lastsectorlba[1] = InlineIdentify (padapter, 2, 0);
						
				if ( !(pdev->DiskMirror[1].status & UCBF_SURVIVOR) && pdev->lastsectorlba[0] )
					spigot1 = TRUE;
				if ( !(pdev->DiskMirror[0].status & UCBF_SURVIVOR) && pdev->lastsectorlba[1] )
					spigot2 = TRUE;
				if ( pdev->DiskMirror[0].status & DiskMirror[1].status & UCBF_SURVIVOR )
					spigot1 = TRUE;

				if ( spigot1 && (pdev->DiskMirror[0].status & UCBF_REBUILD) )
					InlineReadSignature (padapter, pdev, 0);
				if ( spigot2 && (pdev->DiskMirror[1].status & UCBF_REBUILD) )
					InlineReadSignature (padapter, pdev, 1);

				if ( spigot1 && spigot2 && raidon )
					{
					pdev->raid = 1;
					if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
						pdev->spigot = 2;
					else
						pdev->spigot = 1;
					if ( (pdev->DiskMirror[0].status & UCBF_REBUILD) || (pdev->DiskMirror[1].status & UCBF_REBUILD) )
						padapter->reconOn = pdev->reconOn = pdev->reconIsStarting = TRUE;
					}
				else
					{
					if ( spigot1 )
						{
						if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
							goto unregister;
						pdev->DiskMirror[0].status = UCBF_MIRRORED | UCBF_SURVIVOR;
						pdev->spigot = 1;
						}
					else
						{
						if ( pdev->DiskMirror[1].status & UCBF_REBUILD )
							goto unregister;
						pdev->DiskMirror[1].status = UCBF_MIRRORED | UCBF_SURVIVOR;
						pdev->spigot = 2;
						}
					if ( DaleSetup.rebootRebuild && raidon )
						padapter->reconOn = pdev->reconOn = pdev->reconIsStarting = TRUE;
					}
			
				if ( raidon )
					break;
				}
			}

		SetupFinish (padapter, "2220", pshost->irq);
	
		if ( ++Installed < MAXADAPTER )
			continue;
		break;
unregister:;
		scsi_unregister (pshost);
		}

	while ( (pcidev = pci_find_device (VENDOR_PSI, DEVICE_BIGD_1, pcidev)) != NULL )
		{
		pshost = scsi_register (tpnt, sizeof(ADAPTER2220I));
		padapter = HOSTDATA(pshost);

		if ( GetRegs (pshost, TRUE, pcidev) )
			goto unregister1;

		for ( z = 0;  z < BIGD_MAXDRIVES;  z++ )
			DiskMirror[z].status = inb_p (padapter->regScratchPad + BIGD_RAID_0_STATUS + z);		

		scsi_set_pci_device(pshost, pcidev);
		pshost->max_id = padapter->numberOfDrives;
		padapter->failRegister = inb_p (padapter->regScratchPad + BIGD_ALARM_IMAGE);
		for ( z = 0;  z < padapter->numberOfDrives;  z++ )
			{
			unit = inb_p (padapter->regScratchPad + BIGD_DEVICE_0 + z);
			pdev = &padapter->device[z];
			pdev->byte6		= (UCHAR)(((unit & 1) << 4) | 0xE0);
			pdev->spigot	= (UCHAR)(1 << (unit >> 1));
			pdev->sectors	= DaleSetup.setupDevice[unit].sectors;
			pdev->heads		= DaleSetup.setupDevice[unit].heads;
			pdev->cylinders = DaleSetup.setupDevice[unit].cylinders;
			pdev->blocks	= DaleSetup.setupDevice[unit].blocks;
			
			if ( (DiskMirror[unit].signature == SIGNATURE) && (DiskMirror[unit ^ 2].signature == SIGNATURE) &&
			     (DiskMirror[unit].pairIdentifier == (DiskMirror[unit ^ 2].pairIdentifier ^ 1)) )
				{			 
				raidon = TRUE;
				if ( unit > (unit ^ 2) )
					unit = unit ^ 2;
				}	
			else
				raidon = FALSE;
				
			spigot1 = spigot2 = FALSE;
			memcpy (&pdev->DiskMirror[0], &DiskMirror[unit], sizeof (DISK_MIRROR));
			memcpy (&pdev->DiskMirror[1], &DiskMirror[unit ^ 2], sizeof (DISK_MIRROR));
			padapter->raidData[unit]	 = &pdev->DiskMirror[0];
			padapter->raidData[unit ^ 2] = &pdev->DiskMirror[1];
			pdev->spigots[0] = 1 << (unit >> 1);
			pdev->spigots[1] = 1 << ((unit ^ 2) >> 1);
			pdev->deviceID[0] = unit;
			pdev->deviceID[1] = unit ^ 2;
			pdev->lastsectorlba[0] = InlineIdentify (padapter, pdev->spigots[0], unit & 1);
			pdev->lastsectorlba[1] = InlineIdentify (padapter, pdev->spigots[1], unit & 1);

			if ( !(pdev->DiskMirror[1].status & UCBF_SURVIVOR) && pdev->lastsectorlba[0] )
				spigot1 = TRUE;
			if ( !(pdev->DiskMirror[0].status & UCBF_SURVIVOR) && pdev->lastsectorlba[1] )
				spigot2 = TRUE;
			if ( pdev->DiskMirror[0].status & pdev->DiskMirror[1].status & UCBF_SURVIVOR )
				spigot1 = TRUE;

			if ( spigot1 && (pdev->DiskMirror[0].status & UCBF_REBUILD) )
				InlineReadSignature (padapter, pdev, 0);
			if ( spigot2 && (pdev->DiskMirror[1].status & UCBF_REBUILD) )
				InlineReadSignature (padapter, pdev, 1);

			if ( spigot1 && spigot2 && raidon )
				{
				pdev->raid = 1;
				if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
					pdev->spigot = pdev->spigots[1];
				else
					pdev->spigot = pdev->spigots[0];
				if ( (pdev->DiskMirror[0].status & UCBF_REBUILD) || (pdev->DiskMirror[1].status & UCBF_REBUILD) )
					padapter->reconOn = pdev->reconOn = pdev->reconIsStarting = TRUE;
				}
			else
				{
				if ( spigot1 )
					{
					if ( pdev->DiskMirror[0].status & UCBF_REBUILD )
						goto unregister1;
					pdev->DiskMirror[0].status = UCBF_MIRRORED | UCBF_SURVIVOR;
					pdev->spigot = pdev->spigots[0];
					}
				else
					{
					if ( pdev->DiskMirror[1].status & UCBF_REBUILD )
						goto unregister;
					pdev->DiskMirror[1].status = UCBF_MIRRORED | UCBF_SURVIVOR;
					pdev->spigot = pdev->spigots[1];
					}
				if ( DaleSetup.rebootRebuild && raidon )
					padapter->reconOn = pdev->reconOn = pdev->reconIsStarting = TRUE;
				}
			}
		
		if ( !padapter->numberOfDrives )									// If no ATA devices then scan ATAPI
			{
			unit = 0;
			for ( spigot1 = 0;  spigot1 < 4;  spigot1++ )
				{
				for ( device = 0;  device < 2;  device++ )
					{
					DEB (printk ("\nPCI2242I: scanning for ID %d ", (spigot1 * 2) + device));
					pdev = &(padapter->device[(spigot1 * 2) + device]);
					pdev->byte6 = 0x0A | (device << 4);
					pdev->spigot = 1 << spigot1;
					if ( !AtapiReset (padapter, pdev) )
						{
						DEB (printk (" Device found "));
						if ( !AtapiIdentify (padapter, pdev) )
							{
							DEB (printk (" Device verified"));
							unit++;
							continue;
							}
						}
					pdev->spigot = pdev->byte6 = 0;
					}
				}

			if ( unit )
				{
				padapter->atapi = TRUE;
				padapter->timingAddress = DALE_DATA_MODE3;
				outw_p (0x0900, padapter->regIrqControl);					// Turn our interrupts on
				outw_p (0x0C41, padapter->regDmaMode - 1);					// setup for 16 bits, ready enabled, done IRQ enabled, no incriment
				outb_p (0xFF, padapter->regFail);							// all fail lights and alarm off
				pshost->max_id = 8;
				}
			}
		SetupFinish (padapter, "2240", pshost->irq);
		
		if ( ++Installed < MAXADAPTER )
			continue;
		break;
unregister1:;
		scsi_unregister (pshost);
		}

	NumAdapters = Installed;
	return Installed;
	}
/****************************************************************
 *	Name:	Pci2220i_Abort
 *
 *	Description:	Process the Abort command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Allways snooze.
 *
 ****************************************************************/
int Pci2220i_Abort (Scsi_Cmnd *SCpnt)
	{
	PADAPTER2220I	padapter = HOSTDATA(SCpnt->device->host);			// Pointer to adapter control structure
	POUR_DEVICE		pdev	 = &padapter->device[SCpnt->device->id];// Pointer to device information

	if ( !padapter->SCpnt )
		return SCSI_ABORT_NOT_RUNNING;
	
	if ( padapter->atapi )
		{
		if ( AtapiReset (padapter, pdev) )
			return SCSI_ABORT_ERROR;
		OpDone (padapter, DID_ABORT << 16);
		return SCSI_ABORT_SUCCESS;
		}
	return SCSI_ABORT_SNOOZE;
	}
/****************************************************************
 *	Name:	Pci2220i_Reset
 *
 *	Description:	Process the Reset command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					flags - Flags about the reset command
 *
 *	Returns:		No active command at this time, so this means
 *					that each time we got some kind of response the
 *					last time through.  Tell the mid-level code to
 *					request sense information in order to decide what
 *					to do next.
 *
 ****************************************************************/
int Pci2220i_Reset (Scsi_Cmnd *SCpnt, unsigned int reset_flags)
	{
	PADAPTER2220I	padapter = HOSTDATA(SCpnt->device->host);			// Pointer to adapter control structure
	POUR_DEVICE		pdev	 = &padapter->device[SCpnt->device->id];// Pointer to device information

	if ( padapter->atapi )
		{
		if ( AtapiReset (padapter, pdev) )
			return SCSI_RESET_ERROR;
		return SCSI_RESET_SUCCESS;
		}
	return SCSI_RESET_PUNT;
	}
/****************************************************************
 *	Name:	Pci2220i_Release
 *
 *	Description:	Release resources allocated for a single each adapter.
 *
 *	Parameters:		pshost - Pointer to SCSI command structure.
 *
 *	Returns:		zero.
 *
 ****************************************************************/
int Pci2220i_Release (struct Scsi_Host *pshost)
	{
    PADAPTER2220I	padapter = HOSTDATA (pshost);
	USHORT			z;

	if ( padapter->reconOn )
		{
		padapter->reconOn = FALSE;						// shut down the hot reconstruct
		if ( padapter->reconPhase )
			mdelay (300);
		if ( padapter->reconTimer.data )				// is the timer running?
			{
			del_timer (&padapter->reconTimer);
			padapter->reconTimer.data = 0;
			}
		}

	// save RAID status on the board
	if ( padapter->bigD )
		{
		outb_p (padapter->failRegister, padapter->regScratchPad + BIGD_ALARM_IMAGE);
		for ( z = 0;  z < BIGD_MAXDRIVES;  z++ )
			{
			if ( padapter->raidData )
				outb_p (padapter->raidData[z]->status, padapter->regScratchPad + BIGD_RAID_0_STATUS + z);	
			else
				outb_p (0, padapter->regScratchPad + BIGD_RAID_0_STATUS);	
			}
		}
	else
		{
		outb_p (padapter->device[0].DiskMirror[0].status, padapter->regScratchPad + DALE_RAID_0_STATUS);		
		outb_p (padapter->device[0].DiskMirror[1].status, padapter->regScratchPad + DALE_RAID_1_STATUS);		
		}

	if ( padapter->irqOwned )
		free_irq (pshost->irq, padapter);
    release_region (pshost->io_port, pshost->n_io_port);
	if ( padapter->numberOfDrives )
		pci_free_consistent (padapter->pcidev, SECTORSXFER * BYTES_PER_SECTOR, padapter->kBuffer, padapter->kBufferDma);
	else	
		pci_free_consistent (padapter->pcidev, ATAPI_TRANSFER, padapter->kBuffer, padapter->kBufferDma);
    scsi_unregister(pshost);
    return 0;
	}

/****************************************************************
 *	Name:	Pci2220i_BiosParam
 *
 *	Description:	Process the biosparam request from the SCSI manager to
 *					return C/H/S data.
 *
 *	Parameters:		disk - Pointer to SCSI disk structure.
 *					dev	 - Major/minor number from kernel.
 *					geom - Pointer to integer array to place geometry data.
 *
 *	Returns:		zero.
 *
 ****************************************************************/
int Pci2220i_BiosParam (struct scsi_device *sdev, struct block_device *dev,
		sector_t capacity, int geom[])
	{
	POUR_DEVICE	pdev;

	if ( !(HOSTDATA(sdev->host))->atapi )
		{
		pdev = &(HOSTDATA(sdev->host)->device[sdev->id]);

		geom[0] = pdev->heads;
		geom[1] = pdev->sectors;
		geom[2] = pdev->cylinders;
		}
	return 0;
	}

MODULE_LICENSE("Dual BSD/GPL");

static Scsi_Host_Template driver_template = {
	.proc_name		= "pci2220i",
	.name			= "PCI-2220I/PCI-2240I",
	.detect			= Pci2220i_Detect,
	.release		= Pci2220i_Release,
	.queuecommand		= Pci2220i_QueueCommand,
	.abort			= Pci2220i_Abort,
	.reset			= Pci2220i_Reset,
	.bios_param		= Pci2220i_BiosParam,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= DISABLE_CLUSTERING,
};
#include "scsi_module.c"
