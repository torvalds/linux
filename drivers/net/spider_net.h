/*
 * Network device driver for Cell Processor-Based Blade and Celleb platform
 *
 * (C) Copyright IBM Corp. 2005
 * (C) Copyright 2006 TOSHIBA CORPORATION
 *
 * Authors : Utz Bacher <utz.bacher@de.ibm.com>
 *           Jens Osterkamp <Jens.Osterkamp@de.ibm.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _SPIDER_NET_H
#define _SPIDER_NET_H

#define VERSION "2.0 B"

#include "sungem_phy.h"

extern int spider_net_stop(struct net_device *netdev);
extern int spider_net_open(struct net_device *netdev);

extern const struct ethtool_ops spider_net_ethtool_ops;

extern char spider_net_driver_name[];

#define SPIDER_NET_MAX_FRAME			2312
#define SPIDER_NET_MAX_MTU			2294
#define SPIDER_NET_MIN_MTU			64

#define SPIDER_NET_RXBUF_ALIGN			128

#define SPIDER_NET_RX_DESCRIPTORS_DEFAULT	256
#define SPIDER_NET_RX_DESCRIPTORS_MIN		16
#define SPIDER_NET_RX_DESCRIPTORS_MAX		512

#define SPIDER_NET_TX_DESCRIPTORS_DEFAULT	256
#define SPIDER_NET_TX_DESCRIPTORS_MIN		16
#define SPIDER_NET_TX_DESCRIPTORS_MAX		512

#define SPIDER_NET_TX_TIMER			(HZ/5)
#define SPIDER_NET_ANEG_TIMER			(HZ)
#define SPIDER_NET_ANEG_TIMEOUT			5

#define SPIDER_NET_RX_CSUM_DEFAULT		1

#define SPIDER_NET_WATCHDOG_TIMEOUT		50*HZ
#define SPIDER_NET_NAPI_WEIGHT			64

#define SPIDER_NET_FIRMWARE_SEQS	6
#define SPIDER_NET_FIRMWARE_SEQWORDS	1024
#define SPIDER_NET_FIRMWARE_LEN		(SPIDER_NET_FIRMWARE_SEQS * \
					 SPIDER_NET_FIRMWARE_SEQWORDS * \
					 sizeof(u32))
#define SPIDER_NET_FIRMWARE_NAME	"spider_fw.bin"

/** spider_net SMMIO registers */
#define SPIDER_NET_GHIINT0STS		0x00000000
#define SPIDER_NET_GHIINT1STS		0x00000004
#define SPIDER_NET_GHIINT2STS		0x00000008
#define SPIDER_NET_GHIINT0MSK		0x00000010
#define SPIDER_NET_GHIINT1MSK		0x00000014
#define SPIDER_NET_GHIINT2MSK		0x00000018

#define SPIDER_NET_GRESUMINTNUM		0x00000020
#define SPIDER_NET_GREINTNUM		0x00000024

#define SPIDER_NET_GFFRMNUM		0x00000028
#define SPIDER_NET_GFAFRMNUM		0x0000002c
#define SPIDER_NET_GFBFRMNUM		0x00000030
#define SPIDER_NET_GFCFRMNUM		0x00000034
#define SPIDER_NET_GFDFRMNUM		0x00000038

/* clear them (don't use it) */
#define SPIDER_NET_GFREECNNUM		0x0000003c
#define SPIDER_NET_GONETIMENUM		0x00000040

#define SPIDER_NET_GTOUTFRMNUM		0x00000044

#define SPIDER_NET_GTXMDSET		0x00000050
#define SPIDER_NET_GPCCTRL		0x00000054
#define SPIDER_NET_GRXMDSET		0x00000058
#define SPIDER_NET_GIPSECINIT		0x0000005c
#define SPIDER_NET_GFTRESTRT		0x00000060
#define SPIDER_NET_GRXDMAEN		0x00000064
#define SPIDER_NET_GMRWOLCTRL		0x00000068
#define SPIDER_NET_GPCWOPCMD		0x0000006c
#define SPIDER_NET_GPCROPCMD		0x00000070
#define SPIDER_NET_GTTFRMCNT		0x00000078
#define SPIDER_NET_GTESTMD		0x0000007c

#define SPIDER_NET_GSINIT		0x00000080
#define SPIDER_NET_GSnPRGADR		0x00000084
#define SPIDER_NET_GSnPRGDAT		0x00000088

#define SPIDER_NET_GMACOPEMD		0x00000100
#define SPIDER_NET_GMACLENLMT		0x00000108
#define SPIDER_NET_GMACST		0x00000110
#define SPIDER_NET_GMACINTEN		0x00000118
#define SPIDER_NET_GMACPHYCTRL		0x00000120

#define SPIDER_NET_GMACAPAUSE		0x00000154
#define SPIDER_NET_GMACTXPAUSE		0x00000164

#define SPIDER_NET_GMACMODE		0x000001b0
#define SPIDER_NET_GMACBSTLMT		0x000001b4

#define SPIDER_NET_GMACUNIMACU		0x000001c0
#define SPIDER_NET_GMACUNIMACL		0x000001c8

#define SPIDER_NET_GMRMHFILnR		0x00000400
#define SPIDER_NET_MULTICAST_HASHES	256

#define SPIDER_NET_GMRUAFILnR		0x00000500
#define SPIDER_NET_GMRUA0FIL15R		0x00000578

#define SPIDER_NET_GTTQMSK		0x00000934

/* RX DMA controller registers, all 0x00000a.. are for DMA controller A,
 * 0x00000b.. for DMA controller B, etc. */
#define SPIDER_NET_GDADCHA		0x00000a00
#define SPIDER_NET_GDADMACCNTR		0x00000a04
#define SPIDER_NET_GDACTDPA		0x00000a08
#define SPIDER_NET_GDACTDCNT		0x00000a0c
#define SPIDER_NET_GDACDBADDR		0x00000a20
#define SPIDER_NET_GDACDBSIZE		0x00000a24
#define SPIDER_NET_GDACNEXTDA		0x00000a28
#define SPIDER_NET_GDACCOMST		0x00000a2c
#define SPIDER_NET_GDAWBCOMST		0x00000a30
#define SPIDER_NET_GDAWBRSIZE		0x00000a34
#define SPIDER_NET_GDAWBVSIZE		0x00000a38
#define SPIDER_NET_GDAWBTRST		0x00000a3c
#define SPIDER_NET_GDAWBTRERR		0x00000a40

/* TX DMA controller registers */
#define SPIDER_NET_GDTDCHA		0x00000e00
#define SPIDER_NET_GDTDMACCNTR		0x00000e04
#define SPIDER_NET_GDTCDPA		0x00000e08
#define SPIDER_NET_GDTDMASEL		0x00000e14

#define SPIDER_NET_ECMODE		0x00000f00
/* clock and reset control register */
#define SPIDER_NET_CKRCTRL		0x00000ff0

/** SCONFIG registers */
#define SPIDER_NET_SCONFIG_IOACTE	0x00002810

/** interrupt mask registers */
#define SPIDER_NET_INT0_MASK_VALUE	0x3f7fe2c7
#define SPIDER_NET_INT1_MASK_VALUE	0x0000fff2
#define SPIDER_NET_INT2_MASK_VALUE	0x000003f1

/* we rely on flagged descriptor interrupts */
#define SPIDER_NET_FRAMENUM_VALUE	0x00000000
/* set this first, then the FRAMENUM_VALUE */
#define SPIDER_NET_GFXFRAMES_VALUE	0x00000000

#define SPIDER_NET_STOP_SEQ_VALUE	0x00000000
#define SPIDER_NET_RUN_SEQ_VALUE	0x0000007e

#define SPIDER_NET_PHY_CTRL_VALUE	0x00040040
/* #define SPIDER_NET_PHY_CTRL_VALUE	0x01070080*/
#define SPIDER_NET_RXMODE_VALUE		0x00000011
/* auto retransmission in case of MAC aborts */
#define SPIDER_NET_TXMODE_VALUE		0x00010000
#define SPIDER_NET_RESTART_VALUE	0x00000000
#define SPIDER_NET_WOL_VALUE		0x00001111
#if 0
#define SPIDER_NET_WOL_VALUE		0x00000000
#endif
#define SPIDER_NET_IPSECINIT_VALUE	0x6f716f71

/* pause frames: automatic, no upper retransmission count */
/* outside loopback mode: ETOMOD signal dont matter, not connected */
/* ETOMOD signal is brought to PHY reset. bit 2 must be 1 in Celleb */
#define SPIDER_NET_OPMODE_VALUE		0x00000067
/*#define SPIDER_NET_OPMODE_VALUE		0x001b0062*/
#define SPIDER_NET_LENLMT_VALUE		0x00000908

#define SPIDER_NET_MACAPAUSE_VALUE	0x00000800 /* about 1 ms */
#define SPIDER_NET_TXPAUSE_VALUE	0x00000000

#define SPIDER_NET_MACMODE_VALUE	0x00000001
#define SPIDER_NET_BURSTLMT_VALUE	0x00000200 /* about 16 us */

/* DMAC control register GDMACCNTR
 *
 * 1(0)				enable r/tx dma
 *  0000000				fixed to 0
 *
 *         000000			fixed to 0
 *               0(1)			en/disable descr writeback on force end
 *                0(1)			force end
 *
 *                 000000		fixed to 0
 *                       00		burst alignment: 128 bytes
 *                       11		burst alignment: 1024 bytes
 *
 *                         00000	fixed to 0
 *                              0	descr writeback size 32 bytes
 *                               0(1)	descr chain end interrupt enable
 *                                0(1)	descr status writeback enable */

/* to set RX_DMA_EN */
#define SPIDER_NET_DMA_RX_VALUE		0x80000000
#define SPIDER_NET_DMA_RX_FEND_VALUE	0x00030003
/* to set TX_DMA_EN */
#define SPIDER_NET_TX_DMA_EN           0x80000000
#define SPIDER_NET_GDTBSTA             0x00000300
#define SPIDER_NET_GDTDCEIDIS          0x00000002
#define SPIDER_NET_DMA_TX_VALUE        SPIDER_NET_TX_DMA_EN | \
                                       SPIDER_NET_GDTDCEIDIS | \
                                       SPIDER_NET_GDTBSTA

#define SPIDER_NET_DMA_TX_FEND_VALUE	0x00030003

/* SPIDER_NET_UA_DESCR_VALUE is OR'ed with the unicast address */
#define SPIDER_NET_UA_DESCR_VALUE	0x00080000
#define SPIDER_NET_PROMISC_VALUE	0x00080000
#define SPIDER_NET_NONPROMISC_VALUE	0x00000000

#define SPIDER_NET_DMASEL_VALUE		0x00000001

#define SPIDER_NET_ECMODE_VALUE		0x00000000

#define SPIDER_NET_CKRCTRL_RUN_VALUE	0x1fff010f
#define SPIDER_NET_CKRCTRL_STOP_VALUE	0x0000010f

#define SPIDER_NET_SBIMSTATE_VALUE	0x00000000
#define SPIDER_NET_SBTMSTATE_VALUE	0x00000000

/* SPIDER_NET_GHIINT0STS bits, in reverse order so that they can be used
 * with 1 << SPIDER_NET_... */
enum spider_net_int0_status {
	SPIDER_NET_GPHYINT = 0,
	SPIDER_NET_GMAC2INT,
	SPIDER_NET_GMAC1INT,
	SPIDER_NET_GIPSINT,
	SPIDER_NET_GFIFOINT,
	SPIDER_NET_GDMACINT,
	SPIDER_NET_GSYSINT,
	SPIDER_NET_GPWOPCMPINT,
	SPIDER_NET_GPROPCMPINT,
	SPIDER_NET_GPWFFINT,
	SPIDER_NET_GRMDADRINT,
	SPIDER_NET_GRMARPINT,
	SPIDER_NET_GRMMPINT,
	SPIDER_NET_GDTDEN0INT,
	SPIDER_NET_GDDDEN0INT,
	SPIDER_NET_GDCDEN0INT,
	SPIDER_NET_GDBDEN0INT,
	SPIDER_NET_GDADEN0INT,
	SPIDER_NET_GDTFDCINT,
	SPIDER_NET_GDDFDCINT,
	SPIDER_NET_GDCFDCINT,
	SPIDER_NET_GDBFDCINT,
	SPIDER_NET_GDAFDCINT,
	SPIDER_NET_GTTEDINT,
	SPIDER_NET_GDTDCEINT,
	SPIDER_NET_GRFDNMINT,
	SPIDER_NET_GRFCNMINT,
	SPIDER_NET_GRFBNMINT,
	SPIDER_NET_GRFANMINT,
	SPIDER_NET_GRFNMINT,
	SPIDER_NET_G1TMCNTINT,
	SPIDER_NET_GFREECNTINT
};
/* GHIINT1STS bits */
enum spider_net_int1_status {
	SPIDER_NET_GTMFLLINT = 0,
	SPIDER_NET_GRMFLLINT,
	SPIDER_NET_GTMSHTINT,
	SPIDER_NET_GDTINVDINT,
	SPIDER_NET_GRFDFLLINT,
	SPIDER_NET_GDDDCEINT,
	SPIDER_NET_GDDINVDINT,
	SPIDER_NET_GRFCFLLINT,
	SPIDER_NET_GDCDCEINT,
	SPIDER_NET_GDCINVDINT,
	SPIDER_NET_GRFBFLLINT,
	SPIDER_NET_GDBDCEINT,
	SPIDER_NET_GDBINVDINT,
	SPIDER_NET_GRFAFLLINT,
	SPIDER_NET_GDADCEINT,
	SPIDER_NET_GDAINVDINT,
	SPIDER_NET_GDTRSERINT,
	SPIDER_NET_GDDRSERINT,
	SPIDER_NET_GDCRSERINT,
	SPIDER_NET_GDBRSERINT,
	SPIDER_NET_GDARSERINT,
	SPIDER_NET_GDSERINT,
	SPIDER_NET_GDTPTERINT,
	SPIDER_NET_GDDPTERINT,
	SPIDER_NET_GDCPTERINT,
	SPIDER_NET_GDBPTERINT,
	SPIDER_NET_GDAPTERINT
};
/* GHIINT2STS bits */
enum spider_net_int2_status {
	SPIDER_NET_GPROPERINT = 0,
	SPIDER_NET_GMCTCRSNGINT,
	SPIDER_NET_GMCTLCOLINT,
	SPIDER_NET_GMCTTMOTINT,
	SPIDER_NET_GMCRCAERINT,
	SPIDER_NET_GMCRCALERINT,
	SPIDER_NET_GMCRALNERINT,
	SPIDER_NET_GMCROVRINT,
	SPIDER_NET_GMCRRNTINT,
	SPIDER_NET_GMCRRXERINT,
	SPIDER_NET_GTITCSERINT,
	SPIDER_NET_GTIFMTERINT,
	SPIDER_NET_GTIPKTRVKINT,
	SPIDER_NET_GTISPINGINT,
	SPIDER_NET_GTISADNGINT,
	SPIDER_NET_GTISPDNGINT,
	SPIDER_NET_GRIFMTERINT,
	SPIDER_NET_GRIPKTRVKINT,
	SPIDER_NET_GRISPINGINT,
	SPIDER_NET_GRISADNGINT,
	SPIDER_NET_GRISPDNGINT
};

#define SPIDER_NET_TXINT	(1 << SPIDER_NET_GDTFDCINT)

/* We rely on flagged descriptor interrupts */
#define SPIDER_NET_RXINT	( (1 << SPIDER_NET_GDAFDCINT) )

#define SPIDER_NET_LINKINT	( 1 << SPIDER_NET_GMAC2INT )

#define SPIDER_NET_ERRINT	( 0xffffffff & \
				  (~SPIDER_NET_TXINT) & \
				  (~SPIDER_NET_RXINT) & \
				  (~SPIDER_NET_LINKINT) )

#define SPIDER_NET_GPREXEC			0x80000000
#define SPIDER_NET_GPRDAT_MASK			0x0000ffff

#define SPIDER_NET_DMAC_NOINTR_COMPLETE		0x00800000
#define SPIDER_NET_DMAC_TXFRMTL		0x00040000
#define SPIDER_NET_DMAC_TCP			0x00020000
#define SPIDER_NET_DMAC_UDP			0x00030000
#define SPIDER_NET_TXDCEST			0x08000000

#define SPIDER_NET_DESCR_RXFDIS        0x00000001
#define SPIDER_NET_DESCR_RXDCEIS       0x00000002
#define SPIDER_NET_DESCR_RXDEN0IS      0x00000004
#define SPIDER_NET_DESCR_RXINVDIS      0x00000008
#define SPIDER_NET_DESCR_RXRERRIS      0x00000010
#define SPIDER_NET_DESCR_RXFDCIMS      0x00000100
#define SPIDER_NET_DESCR_RXDCEIMS      0x00000200
#define SPIDER_NET_DESCR_RXDEN0IMS     0x00000400
#define SPIDER_NET_DESCR_RXINVDIMS     0x00000800
#define SPIDER_NET_DESCR_RXRERRMIS     0x00001000
#define SPIDER_NET_DESCR_UNUSED        0x077fe0e0

#define SPIDER_NET_DESCR_IND_PROC_MASK		0xF0000000
#define SPIDER_NET_DESCR_COMPLETE		0x00000000 /* used in rx and tx */
#define SPIDER_NET_DESCR_RESPONSE_ERROR		0x10000000 /* used in rx and tx */
#define SPIDER_NET_DESCR_PROTECTION_ERROR	0x20000000 /* used in rx and tx */
#define SPIDER_NET_DESCR_FRAME_END		0x40000000 /* used in rx */
#define SPIDER_NET_DESCR_FORCE_END		0x50000000 /* used in rx and tx */
#define SPIDER_NET_DESCR_CARDOWNED		0xA0000000 /* used in rx and tx */
#define SPIDER_NET_DESCR_NOT_IN_USE		0xF0000000
#define SPIDER_NET_DESCR_TXDESFLG		0x00800000

#define SPIDER_NET_DESCR_BAD_STATUS   (SPIDER_NET_DESCR_RXDEN0IS | \
                                       SPIDER_NET_DESCR_RXRERRIS | \
                                       SPIDER_NET_DESCR_RXDEN0IMS | \
                                       SPIDER_NET_DESCR_RXINVDIMS | \
                                       SPIDER_NET_DESCR_RXRERRMIS | \
                                       SPIDER_NET_DESCR_UNUSED)

/* Descriptor, as defined by the hardware */
struct spider_net_hw_descr {
	u32 buf_addr;
	u32 buf_size;
	u32 next_descr_addr;
	u32 dmac_cmd_status;
	u32 result_size;
	u32 valid_size;	/* all zeroes for tx */
	u32 data_status;
	u32 data_error;	/* all zeroes for tx */
} __attribute__((aligned(32)));

struct spider_net_descr {
	struct spider_net_hw_descr *hwdescr;
	struct sk_buff *skb;
	u32 bus_addr;
	struct spider_net_descr *next;
	struct spider_net_descr *prev;
};

struct spider_net_descr_chain {
	spinlock_t lock;
	struct spider_net_descr *head;
	struct spider_net_descr *tail;
	struct spider_net_descr *ring;
	int num_desc;
	struct spider_net_hw_descr *hwring;
	dma_addr_t dma_addr;
};

/* descriptor data_status bits */
#define SPIDER_NET_RX_IPCHK		29
#define SPIDER_NET_RX_TCPCHK		28
#define SPIDER_NET_VLAN_PACKET		21
#define SPIDER_NET_DATA_STATUS_CKSUM_MASK ( (1 << SPIDER_NET_RX_IPCHK) | \
					  (1 << SPIDER_NET_RX_TCPCHK) )

/* descriptor data_error bits */
#define SPIDER_NET_RX_IPCHKERR		27
#define SPIDER_NET_RX_RXTCPCHKERR	28

#define SPIDER_NET_DATA_ERR_CKSUM_MASK	(1 << SPIDER_NET_RX_IPCHKERR)

/* the cases we don't pass the packet to the stack.
 * 701b8000 would be correct, but every packets gets that flag */
#define SPIDER_NET_DESTROY_RX_FLAGS	0x700b8000

#define SPIDER_NET_DEFAULT_MSG		( NETIF_MSG_DRV | \
					  NETIF_MSG_PROBE | \
					  NETIF_MSG_LINK | \
					  NETIF_MSG_TIMER | \
					  NETIF_MSG_IFDOWN | \
					  NETIF_MSG_IFUP | \
					  NETIF_MSG_RX_ERR | \
					  NETIF_MSG_TX_ERR | \
					  NETIF_MSG_TX_QUEUED | \
					  NETIF_MSG_INTR | \
					  NETIF_MSG_TX_DONE | \
					  NETIF_MSG_RX_STATUS | \
					  NETIF_MSG_PKTDATA | \
					  NETIF_MSG_HW | \
					  NETIF_MSG_WOL )

struct spider_net_extra_stats {
	unsigned long rx_desc_error;
	unsigned long tx_timeouts;
	unsigned long alloc_rx_skb_error;
	unsigned long rx_iommu_map_error;
	unsigned long tx_iommu_map_error;
	unsigned long rx_desc_unk_state;
};

struct spider_net_card {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mii_phy phy;

	struct napi_struct napi;

	int medium;

	void __iomem *regs;

	struct spider_net_descr_chain tx_chain;
	struct spider_net_descr_chain rx_chain;
	struct spider_net_descr *low_watermark;

	int aneg_count;
	struct timer_list aneg_timer;
	struct timer_list tx_timer;
	struct work_struct tx_timeout_task;
	atomic_t tx_timeout_task_counter;
	wait_queue_head_t waitq;
	int num_rx_ints;
	int ignore_rx_ramfull;

	/* for ethtool */
	int msg_enable;
	struct spider_net_extra_stats spider_stats;

	/* Must be last item in struct */
	struct spider_net_descr darray[0];
};

#endif
