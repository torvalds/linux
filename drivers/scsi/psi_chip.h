/*+M*************************************************************************
 * Perceptive Solutions, Inc. PSI-240I device driver proc support for Linux.
 *
 * Copyright (c) 1997 Perceptive Solutions, Inc.
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
 *	File Name:	psi_chip.h
 *
 *	Description:	This file contains the interface defines and
 *					error codes.
 *
 *-M*************************************************************************/
#ifndef PSI_CHIP
#define PSI_CHIP

/************************************************/
/*		Misc konstants							*/
/************************************************/
#define	CHIP_MAXDRIVES			8

/************************************************/
/*		Chip I/O addresses						*/
/************************************************/
#define	CHIP_ADRS_0				0x0130
#define	CHIP_ADRS_1				0x0150
#define	CHIP_ADRS_2				0x0190
#define	CHIP_ADRS_3				0x0210
#define	CHIP_ADRS_4				0x0230
#define	CHIP_ADRS_5				0x0250

/************************************************/
/*		EEPROM locations		*/
/************************************************/
#define	CHIP_EEPROM_BIOS		0x0000		// BIOS base address
#define	CHIP_EEPROM_DATA		0x2000	   	// SETUP data base address
#define	CHIP_EEPROM_FACTORY		0x2400	   	// FACTORY data base address
#define	CHIP_EEPROM_SETUP		0x3000	   	// SETUP PROGRAM base address

#define	CHIP_EEPROM_SIZE		32768U	   	// size of the entire EEPROM
#define	CHIP_EEPROM_BIOS_SIZE	8192	   	// size of the BIOS in bytes
#define	CHIP_EEPROM_DATA_SIZE	4096	   	// size of factory, setup, log data block in bytes
#define	CHIP_EEPROM_SETUP_SIZE	20480U	   	// size of the setup program in bytes

/************************************************/
/*		Chip Interrupts							*/
/************************************************/
#define	CHIP_IRQ_10				0x72
#define	CHIP_IRQ_11				0x73
#define	CHIP_IRQ_12				0x74

/************************************************/
/*		Chip Setup addresses		*/
/************************************************/
#define	CHIP_SETUP_BASE			0x0000C000L

/************************************************/
/*		Chip Register address offsets	*/
/************************************************/
#define	REG_DATA				0x00
#define	REG_ERROR				0x01
#define	REG_SECTOR_COUNT		0x02
#define	REG_LBA_0				0x03
#define	REG_LBA_8				0x04
#define	REG_LBA_16				0x05
#define	REG_LBA_24				0x06
#define	REG_STAT_CMD			0x07
#define	REG_SEL_FAIL			0x08
#define	REG_IRQ_STATUS			0x09
#define	REG_ADDRESS				0x0A
#define	REG_FAIL				0x0C
#define	REG_ALT_STAT		   	0x0E
#define	REG_DRIVE_ADRS			0x0F

/************************************************/
/*		Chip RAM locations		*/
/************************************************/
#define	CHIP_DEVICE				0x8000
#define	CHIP_DEVICE_0			0x8000
#define CHIP_DEVICE_1			0x8008
#define	CHIP_DEVICE_2			0x8010
#define	CHIP_DEVICE_3			0x8018
#define	CHIP_DEVICE_4			0x8020
#define	CHIP_DEVICE_5			0x8028
#define	CHIP_DEVICE_6			0x8030
#define	CHIP_DEVICE_7			0x8038
typedef struct
	{
	UCHAR	channel;		// channel of this device (0-8).
	UCHAR	spt;			// Sectors Per Track.
	ULONG	spc;			// Sectors Per Cylinder.
	}	CHIP_DEVICE_N;

#define	CHIP_CONFIG				0x8100		// address of boards configuration.
typedef struct
	{
	UCHAR		irq;			// interrupt request channel number
	UCHAR		numDrives;		// Number of accessible drives
	UCHAR		fastFormat;	 	// Boolean for fast format enable
	}	CHIP_CONFIG_N;

#define	CHIP_MAP				0x8108 		// eight byte device type map.


#define	CHIP_RAID				0x8120 		// array of RAID signature structures and LBA
#define	CHIP_RAID_1				0x8120
#define CHIP_RAID_2				0x8130
#define	CHIP_RAID_3				0x8140
#define	CHIP_RAID_4				0x8150

/************************************************/
/*		Chip Register Masks		*/
/************************************************/
#define	CHIP_ID					0x7B
#define	SEL_RAM					0x8000
#define	MASK_FAIL				0x80

/************************************************/
/*		Chip cable select bits		*/
/************************************************/
#define	SECTORSXFER				8

/************************************************/
/*		Chip cable select bits		*/
/************************************************/
#define	SEL_NONE				0x00
#define	SEL_1					0x01
#define	SEL_2					0x02
#define	SEL_3					0x04
#define	SEL_4					0x08

/************************************************/
/*		Programmable Interrupt Controller*/
/************************************************/
#define	PIC1					0x20		// first 8259 base port address
#define	PIC2					0xA0		// second 8259 base port address
#define	INT_OCW1				1			// Operation Control Word 1: IRQ mask
#define	EOI						0x20		// non-specific end-of-interrupt

/************************************************/
/*		Device/Geometry controls				*/
/************************************************/
#define GEOMETRY_NONE		 	0x0			// No device
#define GEOMETRY_AUTO			0x1			// Geometry set automatically
#define GEOMETRY_USER		 	0x2			// User supplied geometry

#define	DEVICE_NONE				0x0			// No device present
#define	DEVICE_INACTIVE			0x1			// device present but not registered active
#define	DEVICE_ATAPI			0x2			// ATAPI device (CD_ROM, Tape, Etc...)
#define	DEVICE_DASD_NONLBA		0x3			// Non LBA incompatible device
#define	DEVICE_DASD_LBA			0x4			// LBA compatible device

/************************************************/
/*		Setup Structure Definitions	*/
/************************************************/
typedef struct							// device setup parameters
	{
	UCHAR			geometryControl;	// geometry control flags
	UCHAR		   	device;				// device code
	USHORT			sectors;			// number of sectors per track
	USHORT			heads;				// number of heads
	USHORT			cylinders;			// number of cylinders for this device
	ULONG			blocks;				// number of blocks on device
	USHORT			spare1;
	USHORT			spare2;
	} SETUP_DEVICE, *PSETUP_DEVICE;

typedef struct		// master setup structure
	{
	USHORT 			startupDelay;
	USHORT 			promptBIOS;
	USHORT 			fastFormat;
	USHORT			spare2;
	USHORT			spare3;
	USHORT			spare4;
	USHORT			spare5;
	USHORT			spare6;
	SETUP_DEVICE	setupDevice[8];
	}	SETUP, *PSETUP;

#endif

