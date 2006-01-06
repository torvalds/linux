/************************************************************************
 *
 *	io_edgeport.h	Edgeport Linux Interface definitions
 *
 *	Copyright (C) 2000 Inside Out Networks, Inc.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *
 ************************************************************************/

#if !defined(_IO_EDGEPORT_H_)
#define	_IO_EDGEPORT_H_


#define MAX_RS232_PORTS		8	/* Max # of RS-232 ports per device */

/* typedefs that the insideout headers need */
#ifndef TRUE
	#define TRUE		(1)
#endif
#ifndef FALSE
	#define FALSE		(0)
#endif
#ifndef LOW8
	#define LOW8(a)		((unsigned char)(a & 0xff))
#endif
#ifndef HIGH8
	#define HIGH8(a)	((unsigned char)((a & 0xff00) >> 8))
#endif

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include "io_usbvend.h"



/* The following table is used to map the USBx port number to 
 * the device serial number (or physical USB path), */
#define MAX_EDGEPORTS	64

struct comMapper {
	char	SerialNumber[MAX_SERIALNUMBER_LEN+1];	/* Serial number/usb path */
	int	numPorts;			       	/* Number of ports */
	int	Original[MAX_RS232_PORTS];	       	/* Port numbers set by IOCTL */
	int	Port[MAX_RS232_PORTS];		       	/* Actual used port numbers */
};


#define EDGEPORT_CONFIG_DEVICE "/proc/edgeport"

/* /proc/edgeport Interface
 * This interface uses read/write/lseek interface to talk to the edgeport driver
 * the following read functions are supported: */
#define PROC_GET_MAPPING_TO_PATH 	1
#define PROC_GET_COM_ENTRY		2
#define PROC_GET_EDGE_MANUF_DESCRIPTOR	3
#define PROC_GET_BOOT_DESCRIPTOR	4
#define PROC_GET_PRODUCT_INFO		5
#define PROC_GET_STRINGS		6
#define PROC_GET_CURRENT_COM_MAPPING	7

/* The parameters to the lseek() for the read is: */
#define PROC_READ_SETUP(Command, Argument)	((Command) + ((Argument)<<8))


/* the following write functions are supported: */
#define PROC_SET_COM_MAPPING 		1
#define PROC_SET_COM_ENTRY		2


/* The following sturcture is passed to the write */
struct procWrite {
	int	Command;
	union {
		struct comMapper	Entry;
		int			ComMappingBasedOnUSBPort;	/* Boolean value */
	} u;
};

/*
 *	Product information read from the Edgeport
 */
struct edgeport_product_info {
	__u16	ProductId;			/* Product Identifier */
	__u8	NumPorts;			/* Number of ports on edgeport */
	__u8	ProdInfoVer;			/* What version of structure is this? */

	__u32	IsServer        :1;		/* Set if Server */
	__u32	IsRS232         :1;		/* Set if RS-232 ports exist */
	__u32	IsRS422         :1;		/* Set if RS-422 ports exist */
	__u32	IsRS485         :1;		/* Set if RS-485 ports exist */
	__u32	IsReserved      :28;		/* Reserved for later expansion */

	__u8	RomSize;			/* Size of ROM/E2PROM in K */
	__u8	RamSize;			/* Size of external RAM in K */
	__u8	CpuRev;				/* CPU revision level (chg only if s/w visible) */
	__u8	BoardRev;			/* PCB revision level (chg only if s/w visible) */

	__u8	BootMajorVersion;		/* Boot Firmware version: xx. */
	__u8	BootMinorVersion;		/*	   		  yy. */
	__le16	BootBuildNumber;		/*		      	  zzzz (LE format) */

	__u8	FirmwareMajorVersion;		/* Operational Firmware version:xx. */
	__u8	FirmwareMinorVersion;		/*				yy. */
	__le16	FirmwareBuildNumber;		/*				zzzz (LE format) */

	__u8	ManufactureDescDate[3];		/* MM/DD/YY when descriptor template was compiled */
	__u8	Unused1[1];			/* Available */

	__u8	iDownloadFile;			/* What to download to EPiC device */
	__u8	Unused2[2];			/* Available */
};

/*
 *	Edgeport Stringblock String locations
 */
#define EDGESTRING_MANUFNAME		1	/* Manufacture Name */
#define EDGESTRING_PRODNAME		2	/* Product Name */
#define EDGESTRING_SERIALNUM		3	/* Serial Number */
#define EDGESTRING_ASSEMNUM		4	/* Assembly Number */
#define EDGESTRING_OEMASSEMNUM		5	/* OEM Assembly Number */
#define EDGESTRING_MANUFDATE		6	/* Manufacture Date */
#define EDGESTRING_ORIGSERIALNUM	7	/* Serial Number */

struct string_block {
	__u16	NumStrings;			/* Number of strings in block */
	__u16	Strings[1];			/* Start of string block */
};



#endif
