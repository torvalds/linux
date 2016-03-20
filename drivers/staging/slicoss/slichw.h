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

struct slic_regs {
	u32	slic_reset;	/* Reset Register */
	u32	pad0;

	u32	slic_icr;	/* Interrupt Control Register */
	u32	pad2;
#define SLIC_ICR		0x0008

	u32	slic_isp;	/* Interrupt status pointer */
	u32	pad1;
#define SLIC_ISP		0x0010

	u32	slic_isr;	/* Interrupt status */
	u32	pad3;
#define SLIC_ISR		0x0018

	u32	slic_hbar;	/* Header buffer address reg */
	u32	pad4;
	/*
	 * 31-8 - phy addr of set of contiguous hdr buffers
	 *  7-0 - number of buffers passed
	 * Buffers are 256 bytes long on 256-byte boundaries.
	 */
#define SLIC_HBAR		0x0020
#define SLIC_HBAR_CNT_MSK	0x000000FF

	u32	slic_dbar;	/* Data buffer handle & address reg */
	u32	pad5;

	/* 4 sets of registers; Buffers are 2K bytes long 2 per 4K page. */
#define SLIC_DBAR		0x0028
#define SLIC_DBAR_SIZE		2048

	u32	slic_cbar;	/* Xmt Cmd buf addr regs.*/
	/*
	 * 1 per XMT interface
	 * 31-5 - phy addr of host command buffer
	 *  4-0 - length of cmd in multiples of 32 bytes
	 * Buffers are 32 bytes up to 512 bytes long
	 */
#define SLIC_CBAR		0x0030
#define SLIC_CBAR_LEN_MSK	0x0000001F
#define SLIC_CBAR_ALIGN		0x00000020

	u32	slic_wcs;	/* write control store*/
#define	SLIC_WCS		0x0034
#define SLIC_WCS_START		0x80000000	/*Start the SLIC (Jump to WCS)*/
#define SLIC_WCS_COMPARE	0x40000000	/* Compare with value in WCS*/

	u32	slic_rbar;	/* Response buffer address reg.*/
	u32	pad7;
	/*
	 * 31-8 - phy addr of set of contiguous response buffers
	 * 7-0 - number of buffers passed
	 * Buffers are 32 bytes long on 32-byte boundaries.
	 */
#define SLIC_RBAR		0x0038
#define SLIC_RBAR_CNT_MSK	0x000000FF
#define SLIC_RBAR_SIZE		32

	u32	slic_stats;	/* read statistics (UPR) */
	u32	pad8;
#define	SLIC_RSTAT		0x0040

	u32	slic_rlsr;	/* read link status */
	u32	pad9;
#define SLIC_LSTAT		0x0048

	u32	slic_wmcfg;	/* Write Mac Config */
	u32	pad10;
#define	SLIC_WMCFG		0x0050

	u32	slic_wphy;	/* Write phy register */
	u32	pad11;
#define SLIC_WPHY		0x0058

	u32	slic_rcbar;	/* Rcv Cmd buf addr reg */
	u32	pad12;
#define	SLIC_RCBAR		0x0060

	u32	slic_rconfig;	/* Read SLIC Config*/
	u32	pad13;
#define SLIC_RCONFIG	0x0068

	u32	slic_intagg;	/* Interrupt aggregation time */
	u32	pad14;
#define SLIC_INTAGG		0x0070

	u32	slic_wxcfg;	/* Write XMIT config reg*/
	u32	pad16;
#define	SLIC_WXCFG		0x0078

	u32	slic_wrcfg;	/* Write RCV config reg*/
	u32	pad17;
#define	SLIC_WRCFG		0x0080

	u32	slic_wraddral;	/* Write rcv addr a low*/
	u32	pad18;
#define	SLIC_WRADDRAL	0x0088

	u32	slic_wraddrah;	/* Write rcv addr a high*/
	u32	pad19;
#define	SLIC_WRADDRAH	0x0090

	u32	slic_wraddrbl;	/* Write rcv addr b low*/
	u32	pad20;
#define	SLIC_WRADDRBL	0x0098

	u32	slic_wraddrbh;	/* Write rcv addr b high*/
	u32		pad21;
#define	SLIC_WRADDRBH	0x00a0

	u32	slic_mcastlow;	/* Low bits of mcast mask*/
	u32		pad22;
#define	SLIC_MCASTLOW	0x00a8

	u32	slic_mcasthigh;	/* High bits of mcast mask*/
	u32		pad23;
#define	SLIC_MCASTHIGH	0x00b0

	u32	slic_ping;	/* Ping the card*/
	u32	pad24;
#define SLIC_PING		0x00b8

	u32	slic_dump_cmd;	/* Dump command */
	u32	pad25;
#define SLIC_DUMP_CMD	0x00c0

	u32	slic_dump_data;	/* Dump data pointer */
	u32	pad26;
#define SLIC_DUMP_DATA	0x00c8

	u32	slic_pcistatus;	/* Read card's pci_status register */
	u32	pad27;
#define	SLIC_PCISTATUS	0x00d0

	u32	slic_wrhostid;	/* Write hostid field */
	u32		pad28;
#define SLIC_WRHOSTID		 0x00d8
#define SLIC_RDHOSTID_1GB	 0x1554
#define SLIC_RDHOSTID_2GB	 0x1554

	u32	slic_low_power;	/* Put card in a low power state */
	u32	pad29;
#define SLIC_LOW_POWER	0x00e0

	u32	slic_quiesce;	/* force slic into quiescent state
				 * before soft reset
				 */
	u32	pad30;
#define SLIC_QUIESCE	0x00e8

	u32	slic_reset_iface;/* reset interface queues */
	u32	pad31;
#define SLIC_RESET_IFACE 0x00f0

	u32	slic_addr_upper;/* Bits 63-32 for host i/f addrs */
	u32	pad32;
#define SLIC_ADDR_UPPER	0x00f8 /*Register is only written when it has changed*/

	u32	slic_hbar64;	/* 64 bit Header buffer address reg */
	u32	pad33;
#define SLIC_HBAR64		0x0100

	u32	slic_dbar64;	/* 64 bit Data buffer handle & address reg */
	u32	pad34;
#define SLIC_DBAR64		0x0108

	u32	slic_cbar64;	/* 64 bit Xmt Cmd buf addr regs. */
	u32	pad35;
#define SLIC_CBAR64		0x0110

	u32	slic_rbar64;	/* 64 bit Response buffer address reg.*/
	u32	pad36;
#define SLIC_RBAR64		0x0118

	u32	slic_rcbar64;	/* 64 bit Rcv Cmd buf addr reg*/
	u32	pad37;
#define	SLIC_RCBAR64	0x0120

	u32	slic_stats64;	/* read statistics (64 bit UPR) */
	u32	pad38;
#define	SLIC_RSTAT64	0x0128

	u32	slic_rcv_wcs;	/*Download Gigabit RCV sequencer ucode*/
	u32	pad39;
#define SLIC_RCV_WCS	0x0130
#define SLIC_RCVWCS_BEGIN	0x40000000
#define SLIC_RCVWCS_FINISH	0x80000000

	u32	slic_wrvlanid;	/* Write VlanId field */
	u32	pad40;
#define SLIC_WRVLANID	0x0138

	u32	slic_read_xf_info;	/* Read Transformer info */
	u32	pad41;
#define SLIC_READ_XF_INFO	0x0140

	u32	slic_write_xf_info;	/* Write Transformer info */
	u32	pad42;
#define SLIC_WRITE_XF_INFO	0x0148

	u32	RSVD1;		/* TOE Only */
	u32	pad43;

	u32	RSVD2;		/* TOE Only */
	u32	pad44;

	u32	RSVD3;		/* TOE Only */
	u32	pad45;

	u32	RSVD4;		/* TOE Only */
	u32	pad46;

	u32	slic_ticks_per_sec; /* Write card ticks per second */
	u32	pad47;
#define SLIC_TICKS_PER_SEC	0x0170
};

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

struct xmt_stats {
	u32 xmit_tcp_bytes;
	u32 xmit_tcp_segs;
	u32 xmit_bytes;
	u32 xmit_collisions;
	u32 xmit_unicasts;
	u32 xmit_other_error;
	u32 xmit_excess_collisions;
};

struct rcv_stats {
	u32 rcv_tcp_bytes;
	u32 rcv_tcp_segs;
	u32 rcv_bytes;
	u32 rcv_unicasts;
	u32 rcv_other_error;
	u32 rcv_drops;
};

struct xmt_statsgb {
	u64 xmit_tcp_bytes;
	u64 xmit_tcp_segs;
	u64 xmit_bytes;
	u64 xmit_collisions;
	u64 xmit_unicasts;
	u64 xmit_other_error;
	u64 xmit_excess_collisions;
};

struct rcv_statsgb {
	u64 rcv_tcp_bytes;
	u64 rcv_tcp_segs;
	u64 rcv_bytes;
	u64 rcv_unicasts;
	u64 rcv_other_error;
	u64 rcv_drops;
};

struct slic_stats {
	union {
		struct {
			struct xmt_stats xmt100;
			struct rcv_stats rcv100;
		} stats_100;
		struct {
			struct xmt_statsgb xmtGB;
			struct rcv_statsgb rcvGB;
		} stats_GB;
	} u;
};

#define xmit_tcp_segs100		u.stats_100.xmt100.xmit_tcp_segs
#define xmit_tcp_bytes100		u.stats_100.xmt100.xmit_tcp_bytes
#define xmit_bytes100			u.stats_100.xmt100.xmit_bytes
#define xmit_collisions100		u.stats_100.xmt100.xmit_collisions
#define xmit_unicasts100		u.stats_100.xmt100.xmit_unicasts
#define xmit_other_error100		u.stats_100.xmt100.xmit_other_error
#define xmit_excess_collisions100	u.stats_100.xmt100.xmit_excess_collisions
#define rcv_tcp_segs100			u.stats_100.rcv100.rcv_tcp_segs
#define rcv_tcp_bytes100		u.stats_100.rcv100.rcv_tcp_bytes
#define rcv_bytes100			u.stats_100.rcv100.rcv_bytes
#define rcv_unicasts100			u.stats_100.rcv100.rcv_unicasts
#define rcv_other_error100		u.stats_100.rcv100.rcv_other_error
#define rcv_drops100			u.stats_100.rcv100.rcv_drops
#define xmit_tcp_segs_gb		u.stats_GB.xmtGB.xmit_tcp_segs
#define xmit_tcp_bytes_gb		u.stats_GB.xmtGB.xmit_tcp_bytes
#define xmit_bytes_gb			u.stats_GB.xmtGB.xmit_bytes
#define xmit_collisions_gb		u.stats_GB.xmtGB.xmit_collisions
#define xmit_unicasts_gb		u.stats_GB.xmtGB.xmit_unicasts
#define xmit_other_error_gb		u.stats_GB.xmtGB.xmit_other_error
#define xmit_excess_collisions_gb	u.stats_GB.xmtGB.xmit_excess_collisions

#define rcv_tcp_segs_gb			u.stats_GB.rcvGB.rcv_tcp_segs
#define rcv_tcp_bytes_gb		u.stats_GB.rcvGB.rcv_tcp_bytes
#define rcv_bytes_gb			u.stats_GB.rcvGB.rcv_bytes
#define rcv_unicasts_gb			u.stats_GB.rcvGB.rcv_unicasts
#define rcv_other_error_gb		u.stats_GB.rcvGB.rcv_other_error
#define rcv_drops_gb			u.stats_GB.rcvGB.rcv_drops

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
