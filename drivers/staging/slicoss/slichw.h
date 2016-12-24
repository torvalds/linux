/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slichw.h
 *
 * This header file contains definitions that are common to our hardware.
 */
#ifndef __SLICHW_H__
#define __SLICHW_H__

#define PCI_VENDOR_ID_ALACRITECH	0x139A
#define SLIC_1GB_DEVICE_ID		0x0005
#define SLIC_2GB_DEVICE_ID		0x0007	/* Oasis Device ID */

#define SLIC_1GB_CICADA_SUBSYS_ID	0x0008

#define SLIC_NBR_MACS		4

#define SLIC_RCVBUF_SIZE	2048
#define SLIC_RCVBUF_HEADSIZE	34
#define SLIC_RCVBUF_TAILSIZE	0
#define SLIC_RCVBUF_DATASIZE	(SLIC_RCVBUF_SIZE -		\
				 (SLIC_RCVBUF_HEADSIZE +	\
				  SLIC_RCVBUF_TAILSIZE))

#define VGBSTAT_XPERR		0x40000000
#define VGBSTAT_XERRSHFT	25
#define VGBSTAT_XCSERR		0x23
#define VGBSTAT_XUFLOW		0x22
#define VGBSTAT_XHLEN		0x20
#define VGBSTAT_NETERR		0x01000000
#define VGBSTAT_NERRSHFT	16
#define VGBSTAT_NERRMSK		0x1ff
#define VGBSTAT_NCSERR		0x103
#define VGBSTAT_NUFLOW		0x102
#define VGBSTAT_NHLEN		0x100
#define VGBSTAT_LNKERR		0x00000080
#define VGBSTAT_LERRMSK		0xff
#define VGBSTAT_LDEARLY		0x86
#define VGBSTAT_LBOFLO		0x85
#define VGBSTAT_LCODERR		0x84
#define VGBSTAT_LDBLNBL		0x83
#define VGBSTAT_LCRCERR		0x82
#define VGBSTAT_LOFLO		0x81
#define VGBSTAT_LUFLO		0x80
#define IRHDDR_FLEN_MSK		0x0000ffff
#define IRHDDR_SVALID		0x80000000
#define IRHDDR_ERR		0x10000000
#define VRHSTAT_802OE		0x80000000
#define VRHSTAT_TPOFLO		0x10000000
#define VRHSTATB_802UE		0x80000000
#define VRHSTATB_RCVE		0x40000000
#define VRHSTATB_BUFF		0x20000000
#define VRHSTATB_CARRE		0x08000000
#define VRHSTATB_LONGE		0x02000000
#define VRHSTATB_PREA		0x01000000
#define VRHSTATB_CRC		0x00800000
#define VRHSTATB_DRBL		0x00400000
#define VRHSTATB_CODE		0x00200000
#define VRHSTATB_TPCSUM		0x00100000
#define VRHSTATB_TPHLEN		0x00080000
#define VRHSTATB_IPCSUM		0x00040000
#define VRHSTATB_IPLERR		0x00020000
#define VRHSTATB_IPHERR		0x00010000
#define SLIC_MAX64_BCNT		23
#define SLIC_MAX32_BCNT		26
#define IHCMD_XMT_REQ		0x01
#define IHFLG_IFSHFT		2
#define SLIC_RSPBUF_SIZE	32

#define SLIC_RESET_MAGIC	0xDEAD
#define ICR_INT_OFF		0
#define ICR_INT_ON		1
#define ICR_INT_MASK		2

#define ISR_ERR			0x80000000
#define ISR_RCV			0x40000000
#define ISR_CMD			0x20000000
#define ISR_IO			0x60000000
#define ISR_UPC			0x10000000
#define ISR_LEVENT		0x08000000
#define ISR_RMISS		0x02000000
#define ISR_UPCERR		0x01000000
#define ISR_XDROP		0x00800000
#define ISR_UPCBSY		0x00020000
#define ISR_EVMSK		0xffff0000
#define ISR_PINGMASK		0x00700000
#define ISR_PINGDSMASK		0x00710000
#define ISR_UPCMASK		0x11000000
#define SLIC_WCS_START		0x80000000
#define SLIC_WCS_COMPARE	0x40000000
#define SLIC_RCVWCS_BEGIN	0x40000000
#define SLIC_RCVWCS_FINISH	0x80000000
#define SLIC_PM_MAXPATTERNS	6
#define SLIC_PM_PATTERNSIZE	128
#define SLIC_PMCAPS_WAKEONLAN	0x00000001
#define MIICR_REG_PCR		0x00000000
#define MIICR_REG_4		0x00040000
#define MIICR_REG_9		0x00090000
#define MIICR_REG_16		0x00100000
#define PCR_RESET		0x8000
#define PCR_POWERDOWN		0x0800
#define PCR_SPEED_100		0x2000
#define PCR_SPEED_1000		0x0040
#define PCR_AUTONEG		0x1000
#define PCR_AUTONEG_RST		0x0200
#define PCR_DUPLEX_FULL		0x0100
#define PSR_LINKUP		0x0004

#define PAR_ADV100FD		0x0100
#define PAR_ADV100HD		0x0080
#define PAR_ADV10FD		0x0040
#define PAR_ADV10HD		0x0020
#define PAR_ASYMPAUSE		0x0C00
#define PAR_802_3		0x0001

#define PAR_ADV1000XFD		0x0020
#define PAR_ADV1000XHD		0x0040
#define PAR_ASYMPAUSE_FIBER	0x0180

#define PGC_ADV1000FD		0x0200
#define PGC_ADV1000HD		0x0100
#define SEEQ_LINKFAIL		0x4000
#define SEEQ_SPEED		0x0080
#define SEEQ_DUPLEX		0x0040
#define TDK_DUPLEX		0x0800
#define TDK_SPEED		0x0400
#define MRV_REG16_XOVERON	0x0068
#define MRV_REG16_XOVEROFF	0x0008
#define MRV_SPEED_1000		0x8000
#define MRV_SPEED_100		0x4000
#define MRV_SPEED_10		0x0000
#define MRV_FULLDUPLEX		0x2000
#define MRV_LINKUP		0x0400

#define GIG_LINKUP		0x0001
#define GIG_FULLDUPLEX		0x0002
#define GIG_SPEED_MASK		0x000C
#define GIG_SPEED_1000		0x0008
#define GIG_SPEED_100		0x0004
#define GIG_SPEED_10		0x0000

#define MCR_RESET		0x80000000
#define MCR_CRCEN		0x40000000
#define MCR_FULLD		0x10000000
#define MCR_PAD			0x02000000
#define MCR_RETRYLATE		0x01000000
#define MCR_BOL_SHIFT		21
#define MCR_IPG1_SHIFT		14
#define MCR_IPG2_SHIFT		7
#define MCR_IPG3_SHIFT		0
#define GMCR_RESET		0x80000000
#define GMCR_GBIT		0x20000000
#define GMCR_FULLD		0x10000000
#define GMCR_GAPBB_SHIFT	14
#define GMCR_GAPR1_SHIFT	7
#define GMCR_GAPR2_SHIFT	0
#define GMCR_GAPBB_1000		0x60
#define GMCR_GAPR1_1000		0x2C
#define GMCR_GAPR2_1000		0x40
#define GMCR_GAPBB_100		0x70
#define GMCR_GAPR1_100		0x2C
#define GMCR_GAPR2_100		0x40
#define XCR_RESET		0x80000000
#define XCR_XMTEN		0x40000000
#define XCR_PAUSEEN		0x20000000
#define XCR_LOADRNG		0x10000000
#define RCR_RESET		0x80000000
#define RCR_RCVEN		0x40000000
#define RCR_RCVALL		0x20000000
#define RCR_RCVBAD		0x10000000
#define RCR_CTLEN		0x08000000
#define RCR_ADDRAEN		0x02000000
#define GXCR_RESET		0x80000000
#define GXCR_XMTEN		0x40000000
#define GXCR_PAUSEEN		0x20000000
#define GRCR_RESET		0x80000000
#define GRCR_RCVEN		0x40000000
#define GRCR_RCVALL		0x20000000
#define GRCR_RCVBAD		0x10000000
#define GRCR_CTLEN		0x08000000
#define GRCR_ADDRAEN		0x02000000
#define GRCR_HASHSIZE_SHIFT	17
#define GRCR_HASHSIZE		14

#define SLIC_EEPROM_ID		0xA5A5
#define SLIC_SRAM_SIZE2GB	(64 * 1024)
#define SLIC_SRAM_SIZE1GB	(32 * 1024)
#define SLIC_HOSTID_DEFAULT	0xFFFF		/* uninitialized hostid */
#define SLIC_NBR_MACS		4

struct slic_rcvbuf {
	u8 pad1[6];
	u16 pad2;
	u32 pad3;
	u32 pad4;
	u32 buffer;
	u32 length;
	u32 status;
	u32 pad5;
	u16 pad6;
	u8 data[SLIC_RCVBUF_DATASIZE];
};

struct slic_hddr_wds {
	union {
		struct {
			u32 frame_status;
			u32 frame_status_b;
			u32 time_stamp;
			u32 checksum;
		} hdrs_14port;
		struct {
			u32 frame_status;
			u16 ByteCnt;
			u16 TpChksum;
			u16 CtxHash;
			u16 MacHash;
			u32 BufLnk;
		} hdrs_gbit;
	} u0;
};

#define frame_status14		u0.hdrs_14port.frame_status
#define frame_status_b14	u0.hdrs_14port.frame_status_b
#define frame_statusGB		u0.hdrs_gbit.frame_status

struct slic_host64sg {
	u32 paddrl;
	u32 paddrh;
	u32 length;
};

struct slic_host64_cmd {
	u32 hosthandle;
	u32 RSVD;
	u8 command;
	u8 flags;
	union {
		u16 rsv1;
		u16 rsv2;
	} u0;
	union {
		struct {
			u32 totlen;
			struct slic_host64sg bufs[SLIC_MAX64_BCNT];
		} slic_buffers;
	} u;
};

struct slic_rspbuf {
	u32 hosthandle;
	u32 pad0;
	u32 pad1;
	u32 status;
	u32 pad2[4];
};

/* Reset Register */
#define SLIC_REG_RESET		0x0000
/* Interrupt Control Register */
#define SLIC_REG_ICR		0x0008
/* Interrupt status pointer */
#define SLIC_REG_ISP		0x0010
/* Interrupt status */
#define SLIC_REG_ISR		0x0018
/*
 * Header buffer address reg
 * 31-8 - phy addr of set of contiguous hdr buffers
 *  7-0 - number of buffers passed
 * Buffers are 256 bytes long on 256-byte boundaries.
 */
#define SLIC_REG_HBAR		0x0020
/*
 * Data buffer handle & address reg
 * 4 sets of registers; Buffers are 2K bytes long 2 per 4K page.
 */
#define SLIC_REG_DBAR		0x0028
/*
 * Xmt Cmd buf addr regs.
 * 1 per XMT interface
 * 31-5 - phy addr of host command buffer
 *  4-0 - length of cmd in multiples of 32 bytes
 * Buffers are 32 bytes up to 512 bytes long
 */
#define SLIC_REG_CBAR		0x0030
/* Write control store */
#define	SLIC_REG_WCS		0x0034
/*
 * Response buffer address reg.
 * 31-8 - phy addr of set of contiguous response buffers
 * 7-0 - number of buffers passed
 * Buffers are 32 bytes long on 32-byte boundaries.
 */
#define	SLIC_REG_RBAR		0x0038
/* Read statistics (UPR) */
#define	SLIC_REG_RSTAT		0x0040
/* Read link status */
#define	SLIC_REG_LSTAT		0x0048
/* Write Mac Config */
#define	SLIC_REG_WMCFG		0x0050
/* Write phy register */
#define SLIC_REG_WPHY		0x0058
/* Rcv Cmd buf addr reg */
#define	SLIC_REG_RCBAR		0x0060
/* Read SLIC Config*/
#define SLIC_REG_RCONFIG	0x0068
/* Interrupt aggregation time */
#define SLIC_REG_INTAGG		0x0070
/* Write XMIT config reg */
#define	SLIC_REG_WXCFG		0x0078
/* Write RCV config reg */
#define	SLIC_REG_WRCFG		0x0080
/* Write rcv addr a low */
#define	SLIC_REG_WRADDRAL	0x0088
/* Write rcv addr a high */
#define	SLIC_REG_WRADDRAH	0x0090
/* Write rcv addr b low */
#define	SLIC_REG_WRADDRBL	0x0098
/* Write rcv addr b high */
#define	SLIC_REG_WRADDRBH	0x00a0
/* Low bits of mcast mask */
#define	SLIC_REG_MCASTLOW	0x00a8
/* High bits of mcast mask */
#define	SLIC_REG_MCASTHIGH	0x00b0
/* Ping the card */
#define SLIC_REG_PING		0x00b8
/* Dump command */
#define SLIC_REG_DUMP_CMD	0x00c0
/* Dump data pointer */
#define SLIC_REG_DUMP_DATA	0x00c8
/* Read card's pci_status register */
#define	SLIC_REG_PCISTATUS	0x00d0
/* Write hostid field */
#define SLIC_REG_WRHOSTID	0x00d8
/* Put card in a low power state */
#define SLIC_REG_LOW_POWER	0x00e0
/* Force slic into quiescent state  before soft reset */
#define SLIC_REG_QUIESCE	0x00e8
/* Reset interface queues */
#define SLIC_REG_RESET_IFACE	0x00f0
/*
 * Register is only written when it has changed.
 * Bits 63-32 for host i/f addrs.
 */
#define SLIC_REG_ADDR_UPPER	0x00f8
/* 64 bit Header buffer address reg */
#define SLIC_REG_HBAR64		0x0100
/* 64 bit Data buffer handle & address reg */
#define SLIC_REG_DBAR64		0x0108
/* 64 bit Xmt Cmd buf addr regs. */
#define SLIC_REG_CBAR64		0x0110
/* 64 bit Response buffer address reg.*/
#define SLIC_REG_RBAR64		0x0118
/* 64 bit Rcv Cmd buf addr reg*/
#define	SLIC_REG_RCBAR64	0x0120
/* Read statistics (64 bit UPR) */
#define	SLIC_REG_RSTAT64	0x0128
/* Download Gigabit RCV sequencer ucode */
#define SLIC_REG_RCV_WCS	0x0130
/* Write VlanId field */
#define SLIC_REG_WRVLANID	0x0138
/* Read Transformer info */
#define SLIC_REG_READ_XF_INFO	0x0140
/* Write Transformer info */
#define SLIC_REG_WRITE_XF_INFO	0x0148
/* Write card ticks per second */
#define SLIC_REG_TICKS_PER_SEC	0x0170

#define SLIC_REG_HOSTID		0x1554

enum UPR_REQUEST {
	SLIC_UPR_STATS,
	SLIC_UPR_RLSR,
	SLIC_UPR_WCFG,
	SLIC_UPR_RCONFIG,
	SLIC_UPR_RPHY,
	SLIC_UPR_ENLB,
	SLIC_UPR_ENCT,
	SLIC_UPR_PDWN,
	SLIC_UPR_PING,
	SLIC_UPR_DUMP,
};

struct inicpm_wakepattern {
	u32 patternlength;
	u8 pattern[SLIC_PM_PATTERNSIZE];
	u8 mask[SLIC_PM_PATTERNSIZE];
};

struct inicpm_state {
	u32 powercaps;
	u32 powerstate;
	u32 wake_linkstatus;
	u32 wake_magicpacket;
	u32 wake_framepattern;
	struct inicpm_wakepattern wakepattern[SLIC_PM_MAXPATTERNS];
};

struct slicpm_packet_pattern {
	u32 priority;
	u32 reserved;
	u32 masksize;
	u32 patternoffset;
	u32 patternsize;
	u32 patternflags;
};

enum slicpm_power_state {
	slicpm_state_unspecified = 0,
	slicpm_state_d0,
	slicpm_state_d1,
	slicpm_state_d2,
	slicpm_state_d3,
	slicpm_state_maximum
};

struct slicpm_wakeup_capabilities {
	enum slicpm_power_state min_magic_packet_wakeup;
	enum slicpm_power_state min_pattern_wakeup;
	enum slicpm_power_state min_link_change_wakeup;
};

struct slic_pnp_capabilities {
	u32 flags;
	struct slicpm_wakeup_capabilities wakeup_capabilities;
};

struct slic_config_mac {
	u8 macaddrA[6];
};

#define ATK_FRU_FORMAT		0x00
#define VENDOR1_FRU_FORMAT	0x01
#define VENDOR2_FRU_FORMAT	0x02
#define VENDOR3_FRU_FORMAT	0x03
#define VENDOR4_FRU_FORMAT	0x04
#define NO_FRU_FORMAT		0xFF

struct atk_fru {
	u8 assembly[6];
	u8 revision[2];
	u8 serial[14];
	u8 pad[3];
};

struct vendor1_fru {
	u8 commodity;
	u8 assembly[4];
	u8 revision[2];
	u8 supplier[2];
	u8 date[2];
	u8 sequence[3];
	u8 pad[13];
};

struct vendor2_fru {
	u8 part[8];
	u8 supplier[5];
	u8 date[3];
	u8 sequence[4];
	u8 pad[7];
};

struct vendor3_fru {
	u8 assembly[6];
	u8 revision[2];
	u8 serial[14];
	u8 pad[3];
};

struct vendor4_fru {
	u8 number[8];
	u8 part[8];
	u8 version[8];
	u8 pad[3];
};

union oemfru {
	struct vendor1_fru vendor1_fru;
	struct vendor2_fru vendor2_fru;
	struct vendor3_fru vendor3_fru;
	struct vendor4_fru vendor4_fru;
};

/*
 * SLIC EEPROM structure for Mojave
 */
struct slic_eeprom {
	u16 Id;			/* 00 EEPROM/FLASH Magic code 'A5A5'*/
	u16 EecodeSize;		/* 01 Size of EEPROM Codes (bytes * 4)*/
	u16 FlashSize;		/* 02 Flash size */
	u16 EepromSize;		/* 03 EEPROM Size */
	u16 VendorId;		/* 04 Vendor ID */
	u16 DeviceId;		/* 05 Device ID */
	u8 RevisionId;		/* 06 Revision ID */
	u8 ClassCode[3];	/* 07 Class Code */
	u8 DbgIntPin;		/* 08 Debug Interrupt pin */
	u8 NetIntPin0;		/*    Network Interrupt Pin */
	u8 MinGrant;		/* 09 Minimum grant */
	u8 MaxLat;		/*    Maximum Latency */
	u16 PciStatus;		/* 10 PCI Status */
	u16 SubSysVId;		/* 11 Subsystem Vendor Id */
	u16 SubSysId;		/* 12 Subsystem ID */
	u16 DbgDevId;		/* 13 Debug Device Id */
	u16 DramRomFn;		/* 14 Dram/Rom function */
	u16 DSize2Pci;		/* 15 DRAM size to PCI (bytes * 64K) */
	u16 RSize2Pci;		/* 16 ROM extension size to PCI (bytes * 4k) */
	u8 NetIntPin1;		/* 17 Network Interface Pin 1
				 *  (simba/leone only)
				 */
	u8 NetIntPin2;		/* Network Interface Pin 2 (simba/leone only)*/
	union {
		u8 NetIntPin3;	/* 18 Network Interface Pin 3 (simba only) */
		u8 FreeTime;	/* FreeTime setting (leone/mojave only) */
	} u1;
	u8 TBIctl;		/* 10-bit interface control (Mojave only) */
	u16 DramSize;		/* 19 DRAM size (bytes * 64k) */
	union {
		struct {
			/* Mac Interface Specific portions */
			struct slic_config_mac	MacInfo[SLIC_NBR_MACS];
		} mac;				/* MAC access for all boards */
		struct {
			/* use above struct for MAC access */
			struct slic_config_mac	pad[SLIC_NBR_MACS - 1];
			u16 DeviceId2;	/* Device ID for 2nd PCI function */
			u8 IntPin2;	/* Interrupt pin for 2nd PCI function */
			u8 ClassCode2[3]; /* Class Code for 2nd PCI function */
		} mojave;	/* 2nd function access for gigabit board */
	} u2;
	u16 CfgByte6;		/* Config Byte 6 */
	u16 PMECapab;		/* Power Mgment capabilities */
	u16 NwClkCtrls;		/* NetworkClockControls */
	u8 FruFormat;		/* Alacritech FRU format type */
	struct atk_fru  AtkFru;	/* Alacritech FRU information */
	u8 OemFruFormat;	/* optional OEM FRU format type */
	union oemfru OemFru;	/* optional OEM FRU information */
	u8	Pad[4];		/* Pad to 128 bytes - includes 2 cksum bytes
				 * (if OEM FRU info exists) and two unusable
				 * bytes at the end
				 */
};

/* SLIC EEPROM structure for Oasis */
struct oslic_eeprom {
	u16 Id;			/* 00 EEPROM/FLASH Magic code 'A5A5' */
	u16 EecodeSize;		/* 01 Size of EEPROM Codes (bytes * 4)*/
	u16 FlashConfig0;	/* 02 Flash Config for SPI device 0 */
	u16 FlashConfig1;	/* 03 Flash Config for SPI device 1 */
	u16 VendorId;		/* 04 Vendor ID */
	u16 DeviceId;		/* 05 Device ID (function 0) */
	u8 RevisionId;		/* 06 Revision ID */
	u8 ClassCode[3];	/* 07 Class Code for PCI function 0 */
	u8 IntPin1;		/* 08 Interrupt pin for PCI function 1*/
	u8 ClassCode2[3];	/* 09 Class Code for PCI function 1 */
	u8 IntPin2;		/* 10 Interrupt pin for PCI function 2*/
	u8 IntPin0;		/*    Interrupt pin for PCI function 0*/
	u8 MinGrant;		/* 11 Minimum grant */
	u8 MaxLat;		/*    Maximum Latency */
	u16 SubSysVId;		/* 12 Subsystem Vendor Id */
	u16 SubSysId;		/* 13 Subsystem ID */
	u16 FlashSize;		/* 14 Flash size (bytes / 4K) */
	u16 DSize2Pci;		/* 15 DRAM size to PCI (bytes / 64K) */
	u16 RSize2Pci;		/* 16 Flash (ROM extension) size to PCI
				 *	(bytes / 4K)
				 */
	u16 DeviceId1;		/* 17 Device Id (function 1) */
	u16 DeviceId2;		/* 18 Device Id (function 2) */
	u16 CfgByte6;		/* 19 Device Status Config Bytes 6-7 */
	u16 PMECapab;		/* 20 Power Mgment capabilities */
	u8 MSICapab;		/* 21 MSI capabilities */
	u8 ClockDivider;	/*    Clock divider */
	u16 PciStatusLow;	/* 22 PCI Status bits 15:0 */
	u16 PciStatusHigh;	/* 23 PCI Status bits 31:16 */
	u16 DramConfigLow;	/* 24 DRAM Configuration bits 15:0 */
	u16 DramConfigHigh;	/* 25 DRAM Configuration bits 31:16 */
	u16 DramSize;		/* 26 DRAM size (bytes / 64K) */
	u16 GpioTbiCtl;		/* 27 GPIO/TBI controls for functions 1/0 */
	u16 EepromSize;		/* 28 EEPROM Size */
	struct slic_config_mac MacInfo[2];	/* 29 MAC addresses (2 ports) */
	u8 FruFormat;		/* 35 Alacritech FRU format type */
	struct atk_fru	AtkFru;	/* Alacritech FRU information */
	u8 OemFruFormat;	/* optional OEM FRU format type */
	union oemfru OemFru;	/* optional OEM FRU information */
	u8 Pad[4];		/* Pad to 128 bytes - includes 2 checksum bytes
				 * (if OEM FRU info exists) and two unusable
				 * bytes at the end
				 */
};

#define	MAX_EECODE_SIZE	sizeof(struct slic_eeprom)
#define MIN_EECODE_SIZE	0x62	/* code size without optional OEM FRU stuff */

/*
 * SLIC CONFIG structure
 *
 * This structure lives in the CARD structure and is valid for all board types.
 * It is filled in from the appropriate EEPROM structure by
 * SlicGetConfigData()
 */
struct slic_config {
	bool EepromValid;	/* Valid EEPROM flag (checksum good?) */
	u16 DramSize;		/* DRAM size (bytes / 64K) */
	struct slic_config_mac MacInfo[SLIC_NBR_MACS]; /* MAC addresses */
	u8 FruFormat;		/* Alacritech FRU format type */
	struct atk_fru	AtkFru;	/* Alacritech FRU information */
	u8 OemFruFormat;	/* optional OEM FRU format type */
	union {
		struct vendor1_fru vendor1_fru;
		struct vendor2_fru vendor2_fru;
		struct vendor3_fru vendor3_fru;
		struct vendor4_fru vendor4_fru;
	} OemFru;
};

#pragma pack()

#endif
