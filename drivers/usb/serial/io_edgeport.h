/* SPDX-License-Identifier: GPL-2.0+ */
/************************************************************************
 *
 *	io_edgeport.h	Edgeport Linux Interface definitions
 *
 *	Copyright (C) 2000 Inside Out Networks, Inc.
 *
 ************************************************************************/

#if !defined(_IO_EDGEPORT_H_)
#define	_IO_EDGEPORT_H_

#define MAX_RS232_PORTS		8	/* Max # of RS-232 ports per device */

/* typedefs that the insideout headers need */
#ifndef LOW8
	#define LOW8(a)		((unsigned char)(a & 0xff))
#endif
#ifndef HIGH8
	#define HIGH8(a)	((unsigned char)((a & 0xff00) >> 8))
#endif

#include "io_usbvend.h"

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
	__u8	BootMinorVersion;		/*			  yy. */
	__le16	BootBuildNumber;		/*			  zzzz (LE format) */

	__u8	FirmwareMajorVersion;		/* Operational Firmware version:xx. */
	__u8	FirmwareMinorVersion;		/*				yy. */
	__le16	FirmwareBuildNumber;		/*				zzzz (LE format) */

	__u8	ManufactureDescDate[3];		/* MM/DD/YY when descriptor template was compiled */
	__u8	HardwareType;

	__u8	iDownloadFile;			/* What to download to EPiC device */
	__u8	EpicVer;			/* What version of EPiC spec this device supports */

	struct edge_compatibility_bits Epic;
};

#endif
