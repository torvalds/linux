/**************************************************************************
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 * $Id: slichw.h,v 1.3 2008/03/17 19:27:26 chris Exp $
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

#define PCI_VENDOR_ID_ALACRITECH    0x139A
#define SLIC_1GB_DEVICE_ID          0x0005
#define SLIC_2GB_DEVICE_ID          0x0007  /*Oasis Device ID */

#define SLIC_1GB_CICADA_SUBSYS_ID   0x0008

#define SLIC_NBR_MACS      4

#define SLIC_RCVBUF_SIZE        2048
#define SLIC_RCVBUF_HEADSIZE    34
#define SLIC_RCVBUF_TAILSIZE    0
#define SLIC_RCVBUF_DATASIZE    (SLIC_RCVBUF_SIZE - (SLIC_RCVBUF_HEADSIZE +\
					SLIC_RCVBUF_TAILSIZE))

#define VGBSTAT_XPERR           0x40000000
#define VGBSTAT_XERRSHFT        25
#define VGBSTAT_XCSERR          0x23
#define VGBSTAT_XUFLOW          0x22
#define VGBSTAT_XHLEN           0x20
#define VGBSTAT_NETERR          0x01000000
#define VGBSTAT_NERRSHFT        16
#define VGBSTAT_NERRMSK         0x1ff
#define VGBSTAT_NCSERR          0x103
#define VGBSTAT_NUFLOW          0x102
#define VGBSTAT_NHLEN           0x100
#define VGBSTAT_LNKERR          0x00000080
#define VGBSTAT_LERRMSK         0xff
#define VGBSTAT_LDEARLY         0x86
#define VGBSTAT_LBOFLO          0x85
#define VGBSTAT_LCODERR         0x84
#define VGBSTAT_LDBLNBL         0x83
#define VGBSTAT_LCRCERR         0x82
#define VGBSTAT_LOFLO           0x81
#define VGBSTAT_LUFLO           0x80
#define IRHDDR_FLEN_MSK         0x0000ffff
#define IRHDDR_SVALID           0x80000000
#define IRHDDR_ERR              0x10000000
#define VRHSTAT_802OE           0x80000000
#define VRHSTAT_TPOFLO          0x10000000
#define VRHSTATB_802UE          0x80000000
#define VRHSTATB_RCVE           0x40000000
#define VRHSTATB_BUFF           0x20000000
#define VRHSTATB_CARRE          0x08000000
#define VRHSTATB_LONGE          0x02000000
#define VRHSTATB_PREA           0x01000000
#define VRHSTATB_CRC            0x00800000
#define VRHSTATB_DRBL           0x00400000
#define VRHSTATB_CODE           0x00200000
#define VRHSTATB_TPCSUM         0x00100000
#define VRHSTATB_TPHLEN         0x00080000
#define VRHSTATB_IPCSUM         0x00040000
#define VRHSTATB_IPLERR         0x00020000
#define VRHSTATB_IPHERR         0x00010000
#define SLIC_MAX64_BCNT         23
#define SLIC_MAX32_BCNT         26
#define IHCMD_XMT_REQ           0x01
#define IHFLG_IFSHFT            2
#define SLIC_RSPBUF_SIZE        32

#define SLIC_RESET_MAGIC        0xDEAD
#define ICR_INT_OFF             0
#define ICR_INT_ON              1
#define ICR_INT_MASK            2

#define ISR_ERR                 0x80000000
#define ISR_RCV                 0x40000000
#define ISR_CMD                 0x20000000
#define ISR_IO                  0x60000000
#define ISR_UPC                 0x10000000
#define ISR_LEVENT              0x08000000
#define ISR_RMISS               0x02000000
#define ISR_UPCERR              0x01000000
#define ISR_XDROP               0x00800000
#define ISR_UPCBSY              0x00020000
#define ISR_EVMSK               0xffff0000
#define ISR_PINGMASK            0x00700000
#define ISR_PINGDSMASK          0x00710000
#define ISR_UPCMASK             0x11000000
#define SLIC_WCS_START          0x80000000
#define SLIC_WCS_COMPARE        0x40000000
#define SLIC_RCVWCS_BEGIN       0x40000000
#define SLIC_RCVWCS_FINISH      0x80000000
#define SLIC_PM_MAXPATTERNS     6
#define SLIC_PM_PATTERNSIZE     128
#define SLIC_PMCAPS_WAKEONLAN   0x00000001
#define MIICR_REG_PCR           0x00000000
#define MIICR_REG_4             0x00040000
#define MIICR_REG_9             0x00090000
#define MIICR_REG_16            0x00100000
#define PCR_RESET               0x8000
#define PCR_POWERDOWN           0x0800
#define PCR_SPEED_100           0x2000
#define PCR_SPEED_1000          0x0040
#define PCR_AUTONEG             0x1000
#define PCR_AUTONEG_RST         0x0200
#define PCR_DUPLEX_FULL         0x0100
#define PSR_LINKUP              0x0004

#define PAR_ADV100FD            0x0100
#define PAR_ADV100HD            0x0080
#define PAR_ADV10FD             0x0040
#define PAR_ADV10HD             0x0020
#define PAR_ASYMPAUSE           0x0C00
#define PAR_802_3               0x0001

#define PAR_ADV1000XFD          0x0020
#define PAR_ADV1000XHD          0x0040
#define PAR_ASYMPAUSE_FIBER     0x0180

#define PGC_ADV1000FD           0x0200
#define PGC_ADV1000HD           0x0100
#define SEEQ_LINKFAIL           0x4000
#define SEEQ_SPEED              0x0080
#define SEEQ_DUPLEX             0x0040
#define TDK_DUPLEX              0x0800
#define TDK_SPEED               0x0400
#define MRV_REG16_XOVERON       0x0068
#define MRV_REG16_XOVEROFF      0x0008
#define MRV_SPEED_1000          0x8000
#define MRV_SPEED_100           0x4000
#define MRV_SPEED_10            0x0000
#define MRV_FULLDUPLEX          0x2000
#define MRV_LINKUP              0x0400

#define GIG_LINKUP              0x0001
#define GIG_FULLDUPLEX          0x0002
#define GIG_SPEED_MASK          0x000C
#define GIG_SPEED_1000          0x0008
#define GIG_SPEED_100           0x0004
#define GIG_SPEED_10            0x0000

#define MCR_RESET               0x80000000
#define MCR_CRCEN               0x40000000
#define MCR_FULLD               0x10000000
#define MCR_PAD                 0x02000000
#define MCR_RETRYLATE           0x01000000
#define MCR_BOL_SHIFT           21
#define MCR_IPG1_SHIFT          14
#define MCR_IPG2_SHIFT          7
#define MCR_IPG3_SHIFT          0
#define GMCR_RESET              0x80000000
#define GMCR_GBIT               0x20000000
#define GMCR_FULLD              0x10000000
#define GMCR_GAPBB_SHIFT        14
#define GMCR_GAPR1_SHIFT        7
#define GMCR_GAPR2_SHIFT        0
#define GMCR_GAPBB_1000         0x60
#define GMCR_GAPR1_1000         0x2C
#define GMCR_GAPR2_1000         0x40
#define GMCR_GAPBB_100          0x70
#define GMCR_GAPR1_100          0x2C
#define GMCR_GAPR2_100          0x40
#define XCR_RESET               0x80000000
#define XCR_XMTEN               0x40000000
#define XCR_PAUSEEN             0x20000000
#define XCR_LOADRNG             0x10000000
#define RCR_RESET               0x80000000
#define RCR_RCVEN               0x40000000
#define RCR_RCVALL              0x20000000
#define RCR_RCVBAD              0x10000000
#define RCR_CTLEN               0x08000000
#define RCR_ADDRAEN             0x02000000
#define GXCR_RESET              0x80000000
#define GXCR_XMTEN              0x40000000
#define GXCR_PAUSEEN            0x20000000
#define GRCR_RESET              0x80000000
#define GRCR_RCVEN              0x40000000
#define GRCR_RCVALL             0x20000000
#define GRCR_RCVBAD             0x10000000
#define GRCR_CTLEN              0x08000000
#define GRCR_ADDRAEN            0x02000000
#define GRCR_HASHSIZE_SHIFT     17
#define GRCR_HASHSIZE           14

#define SLIC_EEPROM_ID        0xA5A5
#define SLIC_SRAM_SIZE2GB     (64 * 1024)
#define SLIC_SRAM_SIZE1GB     (32 * 1024)
#define SLIC_HOSTID_DEFAULT   0xFFFF      /* uninitialized hostid */
#define SLIC_NBR_MACS         4

#ifndef FALSE
#define FALSE  0
#else
#undef  FALSE
#define FALSE  0
#endif

#ifndef TRUE
#define TRUE   1
#else
#undef  TRUE
#define TRUE   1
#endif

typedef struct _slic_rcvbuf_t {
    uchar     pad1[6];
    ushort    pad2;
    ulong32   pad3;
    ulong32   pad4;
    ulong32   buffer;
    ulong32   length;
    ulong32   status;
    ulong32   pad5;
    ushort    pad6;
    uchar     data[SLIC_RCVBUF_DATASIZE];
}  slic_rcvbuf_t, *p_slic_rcvbuf_t;

typedef struct _slic_hddr_wds {
  union {
    struct {
	ulong32    frame_status;
	ulong32    frame_status_b;
	ulong32    time_stamp;
	ulong32    checksum;
    } hdrs_14port;
    struct {
	ulong32    frame_status;
	ushort     ByteCnt;
	ushort     TpChksum;
	ushort     CtxHash;
	ushort     MacHash;
	ulong32    BufLnk;
    } hdrs_gbit;
  } u0;
} slic_hddr_wds_t, *p_slic_hddr_wds;

#define frame_status14        u0.hdrs_14port.frame_status
#define frame_status_b14      u0.hdrs_14port.frame_status_b
#define frame_statusGB        u0.hdrs_gbit.frame_status

typedef struct _slic_host64sg_t {
	ulong32        paddrl;
	ulong32        paddrh;
	ulong32        length;
} slic_host64sg_t, *p_slic_host64sg_t;

typedef struct _slic_host64_cmd_t {
    ulong32       hosthandle;
    ulong32       RSVD;
    uchar         command;
    uchar         flags;
    union {
	ushort          rsv1;
	ushort          rsv2;
    } u0;
    union {
	struct {
		ulong32            totlen;
		slic_host64sg_t    bufs[SLIC_MAX64_BCNT];
	} slic_buffers;
    } u;

} slic_host64_cmd_t, *p_slic_host64_cmd_t;

typedef struct _slic_rspbuf_t {
    ulong32   hosthandle;
    ulong32   pad0;
    ulong32   pad1;
    ulong32   status;
    ulong32   pad2[4];

}  slic_rspbuf_t, *p_slic_rspbuf_t;

typedef ulong32 SLIC_REG;


typedef struct _slic_regs_t {
	ULONG	slic_reset;		/* Reset Register */
	ULONG	pad0;

	ULONG	slic_icr;		/* Interrupt Control Register */
	ULONG	pad2;
#define SLIC_ICR		0x0008

	ULONG	slic_isp;		/* Interrupt status pointer */
	ULONG	pad1;
#define SLIC_ISP		0x0010

    ULONG	slic_isr;		/* Interrupt status */
	ULONG	pad3;
#define SLIC_ISR		0x0018

    SLIC_REG	slic_hbar;		/* Header buffer address reg */
	ULONG		pad4;
	/* 31-8 - phy addr of set of contiguous hdr buffers
	    7-0 - number of buffers passed
	   Buffers are 256 bytes long on 256-byte boundaries. */
#define SLIC_HBAR		0x0020
#define SLIC_HBAR_CNT_MSK	0x000000FF

    SLIC_REG	slic_dbar;	/* Data buffer handle & address reg */
	ULONG		pad5;

	/* 4 sets of registers; Buffers are 2K bytes long 2 per 4K page. */
#define SLIC_DBAR		0x0028
#define SLIC_DBAR_SIZE		2048

    SLIC_REG	slic_cbar;		 	/* Xmt Cmd buf addr regs.*/
	/* 1 per XMT interface
	   31-5 - phy addr of host command buffer
	    4-0 - length of cmd in multiples of 32 bytes
	   Buffers are 32 bytes up to 512 bytes long */
#define SLIC_CBAR		0x0030
#define SLIC_CBAR_LEN_MSK	0x0000001F
#define SLIC_CBAR_ALIGN		0x00000020

	SLIC_REG	slic_wcs;		/* write control store*/
#define	SLIC_WCS		0x0034
#define SLIC_WCS_START		0x80000000	/*Start the SLIC (Jump to WCS)*/
#define SLIC_WCS_COMPARE	0x40000000	/* Compare with value in WCS*/

    SLIC_REG	slic_rbar;		/* Response buffer address reg.*/
	ULONG		pad7;
	 /*31-8 - phy addr of set of contiguous response buffers
	  7-0 - number of buffers passed
	 Buffers are 32 bytes long on 32-byte boundaries.*/
#define SLIC_RBAR		0x0038
#define SLIC_RBAR_CNT_MSK	0x000000FF
#define SLIC_RBAR_SIZE		32

	SLIC_REG	slic_stats;		/* read statistics (UPR) */
	ULONG		pad8;
#define	SLIC_RSTAT		0x0040

	SLIC_REG	slic_rlsr;			/* read link status */
	ULONG		pad9;
#define SLIC_LSTAT		0x0048

	SLIC_REG	slic_wmcfg;			/* Write Mac Config */
	ULONG		pad10;
#define	SLIC_WMCFG		0x0050

	SLIC_REG	slic_wphy;			/* Write phy register */
	ULONG		pad11;
#define SLIC_WPHY		0x0058

	SLIC_REG	slic_rcbar;			/*Rcv Cmd buf addr reg*/
	ULONG		pad12;
#define	SLIC_RCBAR		0x0060

	SLIC_REG	slic_rconfig;		/* Read SLIC Config*/
	ULONG		pad13;
#define SLIC_RCONFIG	0x0068

	SLIC_REG	slic_intagg;		/* Interrupt aggregation time*/
	ULONG		pad14;
#define SLIC_INTAGG		0x0070

	SLIC_REG	slic_wxcfg;		/* Write XMIT config reg*/
	ULONG		pad16;
#define	SLIC_WXCFG		0x0078

	SLIC_REG	slic_wrcfg;		/* Write RCV config reg*/
	ULONG		pad17;
#define	SLIC_WRCFG		0x0080

	SLIC_REG	slic_wraddral;		/* Write rcv addr a low*/
	ULONG		pad18;
#define	SLIC_WRADDRAL	0x0088

	SLIC_REG	slic_wraddrah;		/* Write rcv addr a high*/
	ULONG		pad19;
#define	SLIC_WRADDRAH	0x0090

	SLIC_REG	slic_wraddrbl;		/* Write rcv addr b low*/
	ULONG		pad20;
#define	SLIC_WRADDRBL	0x0098

	SLIC_REG	slic_wraddrbh;		/* Write rcv addr b high*/
	ULONG		pad21;
#define	SLIC_WRADDRBH	0x00a0

	SLIC_REG	slic_mcastlow;		/* Low bits of mcast mask*/
	ULONG		pad22;
#define	SLIC_MCASTLOW	0x00a8

	SLIC_REG	slic_mcasthigh;		/* High bits of mcast mask*/
	ULONG		pad23;
#define	SLIC_MCASTHIGH	0x00b0

	SLIC_REG	slic_ping;			/* Ping the card*/
	ULONG		pad24;
#define SLIC_PING		0x00b8

	SLIC_REG	slic_dump_cmd;		/* Dump command */
	ULONG		pad25;
#define SLIC_DUMP_CMD	0x00c0

	SLIC_REG	slic_dump_data;		/* Dump data pointer */
	ULONG		pad26;
#define SLIC_DUMP_DATA	0x00c8

	SLIC_REG	slic_pcistatus;	/* Read card's pci_status register */
	ULONG		pad27;
#define	SLIC_PCISTATUS	0x00d0

	SLIC_REG	slic_wrhostid;		/* Write hostid field */
	ULONG		pad28;
#define SLIC_WRHOSTID		 0x00d8
#define SLIC_RDHOSTID_1GB	 0x1554
#define SLIC_RDHOSTID_2GB	 0x1554

	SLIC_REG	slic_low_power;	/* Put card in a low power state */
	ULONG		pad29;
#define SLIC_LOW_POWER	0x00e0

	SLIC_REG	slic_quiesce;	/* force slic into quiescent state
					 before soft reset */
	ULONG		pad30;
#define SLIC_QUIESCE	0x00e8

	SLIC_REG	slic_reset_iface;	/* reset interface queues */
	ULONG		pad31;
#define SLIC_RESET_IFACE 0x00f0

    SLIC_REG	slic_addr_upper;	/* Bits 63-32 for host i/f addrs */
	ULONG		pad32;
#define SLIC_ADDR_UPPER	0x00f8 /*Register is only written when it has changed*/

    SLIC_REG	slic_hbar64;		/* 64 bit Header buffer address reg */
	ULONG		pad33;
#define SLIC_HBAR64		0x0100

    SLIC_REG	slic_dbar64;	/* 64 bit Data buffer handle & address reg */
	ULONG		pad34;
#define SLIC_DBAR64		0x0108

    SLIC_REG	slic_cbar64;	 	/* 64 bit Xmt Cmd buf addr regs. */
	ULONG		pad35;
#define SLIC_CBAR64		0x0110

    SLIC_REG	slic_rbar64;		/* 64 bit Response buffer address reg.*/
	ULONG		pad36;
#define SLIC_RBAR64		0x0118

	SLIC_REG	slic_rcbar64;		/* 64 bit Rcv Cmd buf addr reg*/
	ULONG		pad37;
#define	SLIC_RCBAR64	0x0120

	SLIC_REG	slic_stats64;		/*read statistics (64 bit UPR)*/
	ULONG		pad38;
#define	SLIC_RSTAT64	0x0128

	SLIC_REG	slic_rcv_wcs;	/*Download Gigabit RCV sequencer ucode*/
	ULONG		pad39;
#define SLIC_RCV_WCS	0x0130
#define SLIC_RCVWCS_BEGIN	0x40000000
#define SLIC_RCVWCS_FINISH	0x80000000

	SLIC_REG	slic_wrvlanid;		/* Write VlanId field */
	ULONG		pad40;
#define SLIC_WRVLANID	0x0138

	SLIC_REG	slic_read_xf_info;  /* Read Transformer info */
	ULONG		pad41;
#define SLIC_READ_XF_INFO 	0x0140

	SLIC_REG	slic_write_xf_info; /* Write Transformer info */
	ULONG		pad42;
#define SLIC_WRITE_XF_INFO 	0x0148

	SLIC_REG	RSVD1;              /* TOE Only */
	ULONG		pad43;

	SLIC_REG	RSVD2; 	            /* TOE Only */
	ULONG		pad44;

	SLIC_REG	RSVD3;              /* TOE Only */
	ULONG		pad45;

	SLIC_REG	RSVD4;              /* TOE Only */
	ULONG		pad46;

	SLIC_REG	slic_ticks_per_sec; /* Write card ticks per second */
	ULONG		pad47;
#define SLIC_TICKS_PER_SEC	0x0170

} __iomem slic_regs_t, *p_slic_regs_t, SLIC_REGS, *PSLIC_REGS;

typedef enum _UPR_REQUEST {
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
} UPR_REQUEST;

typedef struct _inicpm_wakepattern {
    ulong32    patternlength;
    uchar      pattern[SLIC_PM_PATTERNSIZE];
    uchar      mask[SLIC_PM_PATTERNSIZE];
} inicpm_wakepattern_t, *p_inicpm_wakepattern_t;

typedef struct _inicpm_state {
    ulong32                 powercaps;
    ulong32                 powerstate;
    ulong32                 wake_linkstatus;
    ulong32                 wake_magicpacket;
    ulong32                 wake_framepattern;
    inicpm_wakepattern_t    wakepattern[SLIC_PM_MAXPATTERNS];
} inicpm_state_t, *p_inicpm_state_t;

typedef struct _slicpm_packet_pattern {
    ulong32     priority;
    ulong32     reserved;
    ulong32     masksize;
    ulong32     patternoffset;
    ulong32     patternsize;
    ulong32     patternflags;
} slicpm_packet_pattern_t, *p_slicpm_packet_pattern_t;

typedef enum _slicpm_power_state {
    slicpm_state_unspecified = 0,
    slicpm_state_d0,
    slicpm_state_d1,
    slicpm_state_d2,
    slicpm_state_d3,
    slicpm_state_maximum
} slicpm_state_t, *p_slicpm_state_t;

typedef struct _slicpm_wakeup_capabilities {
    slicpm_state_t  min_magic_packet_wakeup;
    slicpm_state_t  min_pattern_wakeup;
    slicpm_state_t  min_link_change_wakeup;
}  slicpm_wakeup_capabilities_t, *p_slicpm_wakeup_capabilities_t;


typedef struct _slic_pnp_capabilities {
    ulong32                         flags;
    slicpm_wakeup_capabilities_t  wakeup_capabilities;
}  slic_pnp_capabilities_t, *p_slic_pnp_capabilities_t;

typedef struct _xmt_stats_t {
    ulong32       xmit_tcp_bytes;
    ulong32       xmit_tcp_segs;
    ulong32       xmit_bytes;
    ulong32       xmit_collisions;
    ulong32       xmit_unicasts;
    ulong32       xmit_other_error;
    ulong32       xmit_excess_collisions;
}   xmt_stats100_t;

typedef struct _rcv_stats_t {
    ulong32       rcv_tcp_bytes;
    ulong32       rcv_tcp_segs;
    ulong32       rcv_bytes;
    ulong32       rcv_unicasts;
    ulong32       rcv_other_error;
    ulong32       rcv_drops;
}   rcv_stats100_t;

typedef struct _xmt_statsgb_t {
    ulong64       xmit_tcp_bytes;
    ulong64       xmit_tcp_segs;
    ulong64       xmit_bytes;
    ulong64       xmit_collisions;
    ulong64       xmit_unicasts;
    ulong64       xmit_other_error;
    ulong64       xmit_excess_collisions;
}   xmt_statsGB_t;

typedef struct _rcv_statsgb_t {
    ulong64       rcv_tcp_bytes;
    ulong64       rcv_tcp_segs;
    ulong64       rcv_bytes;
    ulong64       rcv_unicasts;
    u64       rcv_other_error;
    ulong64       rcv_drops;
}   rcv_statsGB_t;

typedef struct _slic_stats {
    union {
	struct {
		xmt_stats100_t      xmt100;
		rcv_stats100_t      rcv100;
	} stats_100;
	struct {
		xmt_statsGB_t     xmtGB;
		rcv_statsGB_t     rcvGB;
	} stats_GB;
    } u;
} slic_stats_t, *p_slic_stats_t;

#define xmit_tcp_segs100           u.stats_100.xmt100.xmit_tcp_segs
#define xmit_tcp_bytes100          u.stats_100.xmt100.xmit_tcp_bytes
#define xmit_bytes100              u.stats_100.xmt100.xmit_bytes
#define xmit_collisions100         u.stats_100.xmt100.xmit_collisions
#define xmit_unicasts100           u.stats_100.xmt100.xmit_unicasts
#define xmit_other_error100        u.stats_100.xmt100.xmit_other_error
#define xmit_excess_collisions100  u.stats_100.xmt100.xmit_excess_collisions
#define rcv_tcp_segs100            u.stats_100.rcv100.rcv_tcp_segs
#define rcv_tcp_bytes100           u.stats_100.rcv100.rcv_tcp_bytes
#define rcv_bytes100               u.stats_100.rcv100.rcv_bytes
#define rcv_unicasts100            u.stats_100.rcv100.rcv_unicasts
#define rcv_other_error100         u.stats_100.rcv100.rcv_other_error
#define rcv_drops100               u.stats_100.rcv100.rcv_drops
#define xmit_tcp_segs_gb           u.stats_GB.xmtGB.xmit_tcp_segs
#define xmit_tcp_bytes_gb          u.stats_GB.xmtGB.xmit_tcp_bytes
#define xmit_bytes_gb              u.stats_GB.xmtGB.xmit_bytes
#define xmit_collisions_gb         u.stats_GB.xmtGB.xmit_collisions
#define xmit_unicasts_gb           u.stats_GB.xmtGB.xmit_unicasts
#define xmit_other_error_gb        u.stats_GB.xmtGB.xmit_other_error
#define xmit_excess_collisions_gb  u.stats_GB.xmtGB.xmit_excess_collisions

#define rcv_tcp_segs_gb            u.stats_GB.rcvGB.rcv_tcp_segs
#define rcv_tcp_bytes_gb           u.stats_GB.rcvGB.rcv_tcp_bytes
#define rcv_bytes_gb               u.stats_GB.rcvGB.rcv_bytes
#define rcv_unicasts_gb            u.stats_GB.rcvGB.rcv_unicasts
#define rcv_other_error_gb         u.stats_GB.rcvGB.rcv_other_error
#define rcv_drops_gb               u.stats_GB.rcvGB.rcv_drops

typedef struct _slic_config_mac_t {
    uchar        macaddrA[6];

}   slic_config_mac_t, *pslic_config_mac_t;

#define ATK_FRU_FORMAT        0x00
#define VENDOR1_FRU_FORMAT    0x01
#define VENDOR2_FRU_FORMAT    0x02
#define VENDOR3_FRU_FORMAT    0x03
#define VENDOR4_FRU_FORMAT    0x04
#define NO_FRU_FORMAT         0xFF

typedef struct _atk_fru_t {
    uchar        assembly[6];
    uchar        revision[2];
    uchar        serial[14];
    uchar        pad[3];
} atk_fru_t, *patk_fru_t;

typedef struct _vendor1_fru_t {
    uchar        commodity;
    uchar        assembly[4];
    uchar        revision[2];
    uchar        supplier[2];
    uchar        date[2];
    uchar        sequence[3];
    uchar        pad[13];
} vendor1_fru_t, *pvendor1_fru_t;

typedef struct _vendor2_fru_t {
    uchar        part[8];
    uchar        supplier[5];
    uchar        date[3];
    uchar        sequence[4];
    uchar        pad[7];
} vendor2_fru_t, *pvendor2_fru_t;

typedef struct _vendor3_fru_t {
    uchar        assembly[6];
    uchar        revision[2];
    uchar        serial[14];
    uchar        pad[3];
} vendor3_fru_t, *pvendor3_fru_t;

typedef struct _vendor4_fru_t {
    uchar        number[8];
    uchar        part[8];
    uchar        version[8];
    uchar        pad[3];
} vendor4_fru_t, *pvendor4_fru_t;

typedef union _oemfru_t {
    vendor1_fru_t   vendor1_fru;
    vendor2_fru_t   vendor2_fru;
    vendor3_fru_t   vendor3_fru;
    vendor4_fru_t   vendor4_fru;
}  oemfru_t, *poemfru_t;

/*
   SLIC EEPROM structure for Mojave
*/
typedef struct _slic_eeprom {
	ushort		Id;		/* 00 EEPROM/FLASH Magic code 'A5A5'*/
	ushort		EecodeSize;	/* 01 Size of EEPROM Codes (bytes * 4)*/
	ushort		FlashSize;	/* 02 Flash size */
	ushort		EepromSize;	/* 03 EEPROM Size */
	ushort		VendorId;	/* 04 Vendor ID */
	ushort		DeviceId;	/* 05 Device ID */
	uchar		RevisionId;	/* 06 Revision ID */
	uchar		ClassCode[3];	/* 07 Class Code */
	uchar		DbgIntPin;	/* 08 Debug Interrupt pin */
	uchar		NetIntPin0;	/*    Network Interrupt Pin */
	uchar		MinGrant;	/* 09 Minimum grant */
	uchar		MaxLat;		/*    Maximum Latency */
	ushort		PciStatus;	/* 10 PCI Status */
	ushort		SubSysVId;	/* 11 Subsystem Vendor Id */
	ushort		SubSysId;	/* 12 Subsystem ID */
	ushort		DbgDevId;	/* 13 Debug Device Id */
	ushort		DramRomFn;	/* 14 Dram/Rom function */
	ushort		DSize2Pci;	/* 15 DRAM size to PCI (bytes * 64K) */
	ushort	RSize2Pci;	/* 16 ROM extension size to PCI (bytes * 4k) */
	uchar	NetIntPin1; /* 17 Network Interface Pin 1 (simba/leone only) */
	uchar	NetIntPin2; /*    Network Interface Pin 2 (simba/leone only) */
	union {
		uchar	NetIntPin3;/* 18 Network Interface Pin 3 (simba only) */
		uchar	FreeTime;/*    FreeTime setting (leone/mojave only) */
	} u1;
	uchar		TBIctl;	/*    10-bit interface control (Mojave only) */
	ushort		DramSize;	/* 19 DRAM size (bytes * 64k) */
	union {
		struct {
			/* Mac Interface Specific portions */
			slic_config_mac_t	MacInfo[SLIC_NBR_MACS];
		} mac;				/* MAC access for all boards */
		struct {
			/* use above struct for MAC access */
			slic_config_mac_t	pad[SLIC_NBR_MACS - 1];
			ushort		DeviceId2;	/* Device ID for 2nd
								PCI function */
			uchar		IntPin2;	/* Interrupt pin for
							   2nd PCI function */
			uchar		ClassCode2[3];	/* Class Code for 2nd
								PCI function */
		} mojave;	/* 2nd function access for gigabit board */
	} u2;
	ushort		CfgByte6;	/* Config Byte 6 */
	ushort		PMECapab;	/* Power Mgment capabilities */
	ushort		NwClkCtrls;	/* NetworkClockControls */
	uchar		FruFormat;	/* Alacritech FRU format type */
	atk_fru_t   AtkFru;		/* Alacritech FRU information */
	uchar		OemFruFormat;	/* optional OEM FRU format type */
    oemfru_t    OemFru;         /* optional OEM FRU information */
	uchar		Pad[4];	/* Pad to 128 bytes - includes 2 cksum bytes
				 *(if OEM FRU info exists) and two unusable
				 * bytes at the end */
} slic_eeprom_t, *pslic_eeprom_t;

/* SLIC EEPROM structure for Oasis */
typedef struct _oslic_eeprom_t {
	ushort		Id;		/* 00 EEPROM/FLASH Magic code 'A5A5' */
	ushort		EecodeSize;	/* 01 Size of EEPROM Codes (bytes * 4)*/
	ushort		FlashConfig0;	/* 02 Flash Config for SPI device 0 */
	ushort		FlashConfig1;	/* 03 Flash Config for SPI device 1 */
	ushort		VendorId;	/* 04 Vendor ID */
	ushort		DeviceId;	/* 05 Device ID (function 0) */
	uchar		RevisionId;	/* 06 Revision ID */
	uchar		ClassCode[3];	/* 07 Class Code for PCI function 0 */
	uchar		IntPin1;	/* 08 Interrupt pin for PCI function 1*/
	uchar		ClassCode2[3];	/* 09 Class Code for PCI function 1 */
	uchar		IntPin2;	/* 10 Interrupt pin for PCI function 2*/
	uchar		IntPin0;	/*    Interrupt pin for PCI function 0*/
	uchar		MinGrant;	/* 11 Minimum grant */
	uchar		MaxLat;		/*    Maximum Latency */
	ushort		SubSysVId;	/* 12 Subsystem Vendor Id */
	ushort		SubSysId;	/* 13 Subsystem ID */
	ushort		FlashSize;	/* 14 Flash size (bytes / 4K) */
	ushort		DSize2Pci;	/* 15 DRAM size to PCI (bytes / 64K) */
	ushort		RSize2Pci;	/* 16 Flash (ROM extension) size to
						PCI (bytes / 4K) */
	ushort		DeviceId1;	/* 17 Device Id (function 1) */
	ushort		DeviceId2;	/* 18 Device Id (function 2) */
	ushort		CfgByte6;	/* 19 Device Status Config Bytes 6-7 */
	ushort		PMECapab;	/* 20 Power Mgment capabilities */
	uchar		MSICapab;	/* 21 MSI capabilities */
	uchar		ClockDivider;	/*    Clock divider */
	ushort		PciStatusLow;	/* 22 PCI Status bits 15:0 */
	ushort		PciStatusHigh;	/* 23 PCI Status bits 31:16 */
	ushort		DramConfigLow;	/* 24 DRAM Configuration bits 15:0 */
	ushort		DramConfigHigh;	/* 25 DRAM Configuration bits 31:16 */
	ushort		DramSize;	/* 26 DRAM size (bytes / 64K) */
	ushort		GpioTbiCtl;/* 27 GPIO/TBI controls for functions 1/0 */
	ushort		EepromSize;		/* 28 EEPROM Size */
	slic_config_mac_t	MacInfo[2];	/* 29 MAC addresses (2 ports) */
	uchar		FruFormat;	/* 35 Alacritech FRU format type */
	atk_fru_t	AtkFru;		/* Alacritech FRU information */
	uchar		OemFruFormat;	/* optional OEM FRU format type */
	oemfru_t    OemFru;         /* optional OEM FRU information */
	uchar		Pad[4];	/* Pad to 128 bytes - includes 2 checksum bytes
				 * (if OEM FRU info exists) and two unusable
				 * bytes at the end
				 */
} oslic_eeprom_t, *poslic_eeprom_t;

#define	MAX_EECODE_SIZE	sizeof(slic_eeprom_t)
#define MIN_EECODE_SIZE	0x62	/* code size without optional OEM FRU stuff */

/* SLIC CONFIG structure

 This structure lives in the CARD structure and is valid for all
 board types.  It is filled in from the appropriate EEPROM structure
 by SlicGetConfigData().
*/
typedef struct _slic_config_t {
	boolean		EepromValid;	/* Valid EEPROM flag (checksum good?) */
	ushort		DramSize;	/* DRAM size (bytes / 64K) */
	slic_config_mac_t	MacInfo[SLIC_NBR_MACS];	/* MAC addresses */
	uchar		FruFormat;	/* Alacritech FRU format type */
	atk_fru_t	AtkFru;		/* Alacritech FRU information */
	uchar		OemFruFormat;	/* optional OEM FRU format type */
    union {
      vendor1_fru_t   vendor1_fru;
      vendor2_fru_t   vendor2_fru;
      vendor3_fru_t   vendor3_fru;
      vendor4_fru_t   vendor4_fru;
    } OemFru;
} slic_config_t, *pslic_config_t;

#pragma pack()

#endif
