/* atl2.h -- atl2 driver definitions
 *
 * Copyright(c) 2007 Atheros Corporation. All rights reserved.
 * Copyright(c) 2006 xiong huang <xiong.huang@atheros.com>
 * Copyright(c) 2007 Chris Snook <csnook@redhat.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _ATL2_H_
#define _ATL2_H_

#include <linux/atomic.h>
#include <linux/netdevice.h>

#ifndef _ATL2_HW_H_
#define _ATL2_HW_H_

#ifndef _ATL2_OSDEP_H_
#define _ATL2_OSDEP_H_

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>

#include "atlx.h"

#ifdef ETHTOOL_OPS_COMPAT
extern int ethtool_ioctl(struct ifreq *ifr);
#endif

#define PCI_COMMAND_REGISTER	PCI_COMMAND
#define CMD_MEM_WRT_INVALIDATE	PCI_COMMAND_INVALIDATE
#define ETH_ADDR_LEN		ETH_ALEN

#define ATL2_WRITE_REG(a, reg, value) (iowrite32((value), \
	((a)->hw_addr + (reg))))

#define ATL2_WRITE_FLUSH(a) (ioread32((a)->hw_addr))

#define ATL2_READ_REG(a, reg) (ioread32((a)->hw_addr + (reg)))

#define ATL2_WRITE_REGB(a, reg, value) (iowrite8((value), \
	((a)->hw_addr + (reg))))

#define ATL2_READ_REGB(a, reg) (ioread8((a)->hw_addr + (reg)))

#define ATL2_WRITE_REGW(a, reg, value) (iowrite16((value), \
	((a)->hw_addr + (reg))))

#define ATL2_READ_REGW(a, reg) (ioread16((a)->hw_addr + (reg)))

#define ATL2_WRITE_REG_ARRAY(a, reg, offset, value) \
	(iowrite32((value), (((a)->hw_addr + (reg)) + ((offset) << 2))))

#define ATL2_READ_REG_ARRAY(a, reg, offset) \
	(ioread32(((a)->hw_addr + (reg)) + ((offset) << 2)))

#endif /* _ATL2_OSDEP_H_ */

struct atl2_adapter;
struct atl2_hw;

/* function prototype */
static s32 atl2_reset_hw(struct atl2_hw *hw);
static s32 atl2_read_mac_addr(struct atl2_hw *hw);
static s32 atl2_init_hw(struct atl2_hw *hw);
static s32 atl2_get_speed_and_duplex(struct atl2_hw *hw, u16 *speed,
	u16 *duplex);
static u32 atl2_hash_mc_addr(struct atl2_hw *hw, u8 *mc_addr);
static void atl2_hash_set(struct atl2_hw *hw, u32 hash_value);
static s32 atl2_read_phy_reg(struct atl2_hw *hw, u16 reg_addr, u16 *phy_data);
static s32 atl2_write_phy_reg(struct atl2_hw *hw, u32 reg_addr, u16 phy_data);
static void atl2_read_pci_cfg(struct atl2_hw *hw, u32 reg, u16 *value);
static void atl2_write_pci_cfg(struct atl2_hw *hw, u32 reg, u16 *value);
static void atl2_set_mac_addr(struct atl2_hw *hw);
static bool atl2_read_eeprom(struct atl2_hw *hw, u32 Offset, u32 *pValue);
static bool atl2_write_eeprom(struct atl2_hw *hw, u32 offset, u32 value);
static s32 atl2_phy_init(struct atl2_hw *hw);
static int atl2_check_eeprom_exist(struct atl2_hw *hw);
static void atl2_force_ps(struct atl2_hw *hw);

/* register definition */

/* Block IDLE Status Register */
#define IDLE_STATUS_RXMAC	1	/* 1: RXMAC is non-IDLE */
#define IDLE_STATUS_TXMAC	2	/* 1: TXMAC is non-IDLE */
#define IDLE_STATUS_DMAR	8	/* 1: DMAR is non-IDLE */
#define IDLE_STATUS_DMAW	4	/* 1: DMAW is non-IDLE */

/* MDIO Control Register */
#define MDIO_WAIT_TIMES		10

/* MAC Control Register */
#define MAC_CTRL_DBG_TX_BKPRESURE	0x100000	/* 1: TX max backoff */
#define MAC_CTRL_MACLP_CLK_PHY		0x8000000	/* 1: 25MHz from phy */
#define MAC_CTRL_HALF_LEFT_BUF_SHIFT	28
#define MAC_CTRL_HALF_LEFT_BUF_MASK	0xF		/* MAC retry buf x32B */

/* Internal SRAM Partition Register */
#define REG_SRAM_TXRAM_END	0x1500	/* Internal tail address of TXRAM
					 * default: 2byte*1024 */
#define REG_SRAM_RXRAM_END	0x1502	/* Internal tail address of RXRAM
					 * default: 2byte*1024 */

/* Descriptor Control register */
#define REG_TXD_BASE_ADDR_LO	0x1544	/* The base address of the Transmit
					 * Data Mem low 32-bit(dword align) */
#define REG_TXD_MEM_SIZE	0x1548	/* Transmit Data Memory size(by
					 * double word , max 256KB) */
#define REG_TXS_BASE_ADDR_LO	0x154C	/* The base address of the Transmit
					 * Status Memory low 32-bit(dword word
					 * align) */
#define REG_TXS_MEM_SIZE	0x1550	/* double word unit, max 4*2047
					 * bytes. */
#define REG_RXD_BASE_ADDR_LO	0x1554	/* The base address of the Transmit
					 * Status Memory low 32-bit(unit 8
					 * bytes) */
#define REG_RXD_BUF_NUM		0x1558	/* Receive Data & Status Memory buffer
					 * number (unit 1536bytes, max
					 * 1536*2047) */

/* DMAR Control Register */
#define REG_DMAR	0x1580
#define     DMAR_EN	0x1	/* 1: Enable DMAR */

/* TX Cur-Through (early tx threshold) Control Register */
#define REG_TX_CUT_THRESH	0x1590	/* TxMac begin transmit packet
					 * threshold(unit word) */

/* DMAW Control Register */
#define REG_DMAW	0x15A0
#define     DMAW_EN	0x1

/* Flow control register */
#define REG_PAUSE_ON_TH		0x15A8	/* RXD high watermark of overflow
					 * threshold configuration register */
#define REG_PAUSE_OFF_TH	0x15AA	/* RXD lower watermark of overflow
					 * threshold configuration register */

/* Mailbox Register */
#define REG_MB_TXD_WR_IDX	0x15f0	/* double word align */
#define REG_MB_RXD_RD_IDX	0x15F4	/* RXD Read index (unit: 1536byets) */

/* Interrupt Status Register */
#define ISR_TIMER	1	/* Interrupt when Timer counts down to zero */
#define ISR_MANUAL	2	/* Software manual interrupt, for debug. Set
				 * when SW_MAN_INT_EN is set in Table 51
				 * Selene Master Control Register
				 * (Offset 0x1400). */
#define ISR_RXF_OV	4	/* RXF overflow interrupt */
#define ISR_TXF_UR	8	/* TXF underrun interrupt */
#define ISR_TXS_OV	0x10	/* Internal transmit status buffer full
				 * interrupt */
#define ISR_RXS_OV	0x20	/* Internal receive status buffer full
				 * interrupt */
#define ISR_LINK_CHG	0x40	/* Link Status Change Interrupt */
#define ISR_HOST_TXD_UR	0x80
#define ISR_HOST_RXD_OV	0x100	/* Host rx data memory full , one pulse */
#define ISR_DMAR_TO_RST	0x200	/* DMAR op timeout interrupt. SW should
				 * do Reset */
#define ISR_DMAW_TO_RST	0x400
#define ISR_PHY		0x800	/* phy interrupt */
#define ISR_TS_UPDATE	0x10000	/* interrupt after new tx pkt status written
				 * to host */
#define ISR_RS_UPDATE	0x20000	/* interrupt ater new rx pkt status written
				 * to host. */
#define ISR_TX_EARLY	0x40000	/* interrupt when txmac begin transmit one
				 * packet */

#define ISR_TX_EVENT (ISR_TXF_UR | ISR_TXS_OV | ISR_HOST_TXD_UR |\
	ISR_TS_UPDATE | ISR_TX_EARLY)
#define ISR_RX_EVENT (ISR_RXF_OV | ISR_RXS_OV | ISR_HOST_RXD_OV |\
	 ISR_RS_UPDATE)

#define IMR_NORMAL_MASK		(\
	/*ISR_LINK_CHG		|*/\
	ISR_MANUAL		|\
	ISR_DMAR_TO_RST		|\
	ISR_DMAW_TO_RST		|\
	ISR_PHY			|\
	ISR_PHY_LINKDOWN	|\
	ISR_TS_UPDATE		|\
	ISR_RS_UPDATE)

/* Receive MAC Statistics Registers */
#define REG_STS_RX_PAUSE	0x1700	/* Num pause packets received */
#define REG_STS_RXD_OV		0x1704	/* Num frames dropped due to RX
					 * FIFO overflow */
#define REG_STS_RXS_OV		0x1708	/* Num frames dropped due to RX
					 * Status Buffer Overflow */
#define REG_STS_RX_FILTER	0x170C	/* Num packets dropped due to
					 * address filtering */

/* MII definitions */

/* PHY Common Register */
#define MII_SMARTSPEED	0x14
#define MII_DBG_ADDR	0x1D
#define MII_DBG_DATA	0x1E

/* PCI Command Register Bit Definitions */
#define PCI_REG_COMMAND		0x04
#define CMD_IO_SPACE		0x0001
#define CMD_MEMORY_SPACE	0x0002
#define CMD_BUS_MASTER		0x0004

#define MEDIA_TYPE_100M_FULL	1
#define MEDIA_TYPE_100M_HALF	2
#define MEDIA_TYPE_10M_FULL	3
#define MEDIA_TYPE_10M_HALF	4

#define AUTONEG_ADVERTISE_SPEED_DEFAULT	0x000F	/* Everything */

/* The size (in bytes) of a ethernet packet */
#define ENET_HEADER_SIZE		14
#define MAXIMUM_ETHERNET_FRAME_SIZE	1518	/* with FCS */
#define MINIMUM_ETHERNET_FRAME_SIZE	64	/* with FCS */
#define ETHERNET_FCS_SIZE		4
#define MAX_JUMBO_FRAME_SIZE		0x2000
#define VLAN_SIZE                                               4

struct tx_pkt_header {
	unsigned pkt_size:11;
	unsigned:4;			/* reserved */
	unsigned ins_vlan:1;		/* txmac should insert vlan */
	unsigned short vlan;		/* vlan tag */
};
/* FIXME: replace above bitfields with MASK/SHIFT defines below */
#define TX_PKT_HEADER_SIZE_MASK		0x7FF
#define TX_PKT_HEADER_SIZE_SHIFT	0
#define TX_PKT_HEADER_INS_VLAN_MASK	0x1
#define TX_PKT_HEADER_INS_VLAN_SHIFT	15
#define TX_PKT_HEADER_VLAN_TAG_MASK	0xFFFF
#define TX_PKT_HEADER_VLAN_TAG_SHIFT	16

struct tx_pkt_status {
	unsigned pkt_size:11;
	unsigned:5;		/* reserved */
	unsigned ok:1;		/* current packet transmitted without error */
	unsigned bcast:1;	/* broadcast packet */
	unsigned mcast:1;	/* multicast packet */
	unsigned pause:1;	/* transmiited a pause frame */
	unsigned ctrl:1;
	unsigned defer:1;    	/* current packet is xmitted with defer */
	unsigned exc_defer:1;
	unsigned single_col:1;
	unsigned multi_col:1;
	unsigned late_col:1;
	unsigned abort_col:1;
	unsigned underun:1;	/* current packet is aborted
				 * due to txram underrun */
	unsigned:3;		/* reserved */
	unsigned update:1;	/* always 1'b1 in tx_status_buf */
};
/* FIXME: replace above bitfields with MASK/SHIFT defines below */
#define TX_PKT_STATUS_SIZE_MASK		0x7FF
#define TX_PKT_STATUS_SIZE_SHIFT	0
#define TX_PKT_STATUS_OK_MASK		0x1
#define TX_PKT_STATUS_OK_SHIFT		16
#define TX_PKT_STATUS_BCAST_MASK	0x1
#define TX_PKT_STATUS_BCAST_SHIFT	17
#define TX_PKT_STATUS_MCAST_MASK	0x1
#define TX_PKT_STATUS_MCAST_SHIFT	18
#define TX_PKT_STATUS_PAUSE_MASK	0x1
#define TX_PKT_STATUS_PAUSE_SHIFT	19
#define TX_PKT_STATUS_CTRL_MASK		0x1
#define TX_PKT_STATUS_CTRL_SHIFT	20
#define TX_PKT_STATUS_DEFER_MASK	0x1
#define TX_PKT_STATUS_DEFER_SHIFT	21
#define TX_PKT_STATUS_EXC_DEFER_MASK	0x1
#define TX_PKT_STATUS_EXC_DEFER_SHIFT	22
#define TX_PKT_STATUS_SINGLE_COL_MASK	0x1
#define TX_PKT_STATUS_SINGLE_COL_SHIFT	23
#define TX_PKT_STATUS_MULTI_COL_MASK	0x1
#define TX_PKT_STATUS_MULTI_COL_SHIFT	24
#define TX_PKT_STATUS_LATE_COL_MASK	0x1
#define TX_PKT_STATUS_LATE_COL_SHIFT	25
#define TX_PKT_STATUS_ABORT_COL_MASK	0x1
#define TX_PKT_STATUS_ABORT_COL_SHIFT	26
#define TX_PKT_STATUS_UNDERRUN_MASK	0x1
#define TX_PKT_STATUS_UNDERRUN_SHIFT	27
#define TX_PKT_STATUS_UPDATE_MASK	0x1
#define TX_PKT_STATUS_UPDATE_SHIFT	31

struct rx_pkt_status {
	unsigned pkt_size:11;	/* packet size, max 2047 bytes */
	unsigned:5;		/* reserved */
	unsigned ok:1;		/* current packet received ok without error */
	unsigned bcast:1;	/* current packet is broadcast */
	unsigned mcast:1;	/* current packet is multicast */
	unsigned pause:1;
	unsigned ctrl:1;
	unsigned crc:1;		/* received a packet with crc error */
	unsigned code:1;	/* received a packet with code error */
	unsigned runt:1;	/* received a packet less than 64 bytes
				 * with good crc */
	unsigned frag:1;	/* received a packet less than 64 bytes
				 * with bad crc */
	unsigned trunc:1;	/* current frame truncated due to rxram full */
	unsigned align:1;	/* this packet is alignment error */
	unsigned vlan:1;	/* this packet has vlan */
	unsigned:3;		/* reserved */
	unsigned update:1;
	unsigned short vtag;	/* vlan tag */
	unsigned:16;
};
/* FIXME: replace above bitfields with MASK/SHIFT defines below */
#define RX_PKT_STATUS_SIZE_MASK		0x7FF
#define RX_PKT_STATUS_SIZE_SHIFT	0
#define RX_PKT_STATUS_OK_MASK		0x1
#define RX_PKT_STATUS_OK_SHIFT		16
#define RX_PKT_STATUS_BCAST_MASK	0x1
#define RX_PKT_STATUS_BCAST_SHIFT	17
#define RX_PKT_STATUS_MCAST_MASK	0x1
#define RX_PKT_STATUS_MCAST_SHIFT	18
#define RX_PKT_STATUS_PAUSE_MASK	0x1
#define RX_PKT_STATUS_PAUSE_SHIFT	19
#define RX_PKT_STATUS_CTRL_MASK		0x1
#define RX_PKT_STATUS_CTRL_SHIFT	20
#define RX_PKT_STATUS_CRC_MASK		0x1
#define RX_PKT_STATUS_CRC_SHIFT		21
#define RX_PKT_STATUS_CODE_MASK		0x1
#define RX_PKT_STATUS_CODE_SHIFT	22
#define RX_PKT_STATUS_RUNT_MASK		0x1
#define RX_PKT_STATUS_RUNT_SHIFT	23
#define RX_PKT_STATUS_FRAG_MASK		0x1
#define RX_PKT_STATUS_FRAG_SHIFT	24
#define RX_PKT_STATUS_TRUNK_MASK	0x1
#define RX_PKT_STATUS_TRUNK_SHIFT	25
#define RX_PKT_STATUS_ALIGN_MASK	0x1
#define RX_PKT_STATUS_ALIGN_SHIFT	26
#define RX_PKT_STATUS_VLAN_MASK		0x1
#define RX_PKT_STATUS_VLAN_SHIFT	27
#define RX_PKT_STATUS_UPDATE_MASK	0x1
#define RX_PKT_STATUS_UPDATE_SHIFT	31
#define RX_PKT_STATUS_VLAN_TAG_MASK	0xFFFF
#define RX_PKT_STATUS_VLAN_TAG_SHIFT	32

struct rx_desc {
	struct rx_pkt_status	status;
	unsigned char     	packet[1536-sizeof(struct rx_pkt_status)];
};

enum atl2_speed_duplex {
	atl2_10_half = 0,
	atl2_10_full = 1,
	atl2_100_half = 2,
	atl2_100_full = 3
};

struct atl2_spi_flash_dev {
	const char *manu_name;	/* manufacturer id */
	/* op-code */
	u8 cmdWRSR;
	u8 cmdREAD;
	u8 cmdPROGRAM;
	u8 cmdWREN;
	u8 cmdWRDI;
	u8 cmdRDSR;
	u8 cmdRDID;
	u8 cmdSECTOR_ERASE;
	u8 cmdCHIP_ERASE;
};

/* Structure containing variables used by the shared code (atl2_hw.c) */
struct atl2_hw {
	u8 __iomem *hw_addr;
	void *back;

	u8 preamble_len;
	u8 max_retry;          /* Retransmission maximum, afterwards the
				* packet will be discarded. */
	u8 jam_ipg;            /* IPG to start JAM for collision based flow
				* control in half-duplex mode. In unit of
				* 8-bit time. */
	u8 ipgt;               /* Desired back to back inter-packet gap. The
				* default is 96-bit time. */
	u8 min_ifg;            /* Minimum number of IFG to enforce in between
				* RX frames. Frame gap below such IFP is
				* dropped. */
	u8 ipgr1;              /* 64bit Carrier-Sense window */
	u8 ipgr2;              /* 96-bit IPG window */
	u8 retry_buf;          /* When half-duplex mode, should hold some
				* bytes for mac retry . (8*4bytes unit) */

	u16 fc_rxd_hi;
	u16 fc_rxd_lo;
	u16 lcol;              /* Collision Window */
	u16 max_frame_size;

	u16 MediaType;
	u16 autoneg_advertised;
	u16 pci_cmd_word;

	u16 mii_autoneg_adv_reg;

	u32 mem_rang;
	u32 txcw;
	u32 mc_filter_type;
	u32 num_mc_addrs;
	u32 collision_delta;
	u32 tx_packet_delta;
	u16 phy_spd_default;

	u16 device_id;
	u16 vendor_id;
	u16 subsystem_id;
	u16 subsystem_vendor_id;
	u8 revision_id;

	/* spi flash */
	u8 flash_vendor;

	u8 dma_fairness;
	u8 mac_addr[NODE_ADDRESS_SIZE];
	u8 perm_mac_addr[NODE_ADDRESS_SIZE];

	/* FIXME */
	/* bool phy_preamble_sup; */
	bool phy_configured;
};

#endif /* _ATL2_HW_H_ */

struct atl2_ring_header {
    /* pointer to the descriptor ring memory */
    void *desc;
    /* physical address of the descriptor ring */
    dma_addr_t dma;
    /* length of descriptor ring in bytes */
    unsigned int size;
};

/* board specific private data structure */
struct atl2_adapter {
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	u32 wol;
	u16 link_speed;
	u16 link_duplex;

	spinlock_t stats_lock;

	struct work_struct reset_task;
	struct work_struct link_chg_task;
	struct timer_list watchdog_timer;
	struct timer_list phy_config_timer;

	unsigned long cfg_phy;
	bool mac_disabled;

	/* All Descriptor memory */
	dma_addr_t	ring_dma;
	void		*ring_vir_addr;
	int		ring_size;

	struct tx_pkt_header	*txd_ring;
	dma_addr_t	txd_dma;

	struct tx_pkt_status	*txs_ring;
	dma_addr_t	txs_dma;

	struct rx_desc	*rxd_ring;
	dma_addr_t	rxd_dma;

	u32 txd_ring_size;         /* bytes per unit */
	u32 txs_ring_size;         /* dwords per unit */
	u32 rxd_ring_size;         /* 1536 bytes per unit */

	/* read /write ptr: */
	/* host */
	u32 txd_write_ptr;
	u32 txs_next_clear;
	u32 rxd_read_ptr;

	/* nic */
	atomic_t txd_read_ptr;
	atomic_t txs_write_ptr;
	u32 rxd_write_ptr;

	/* Interrupt Moderator timer ( 2us resolution) */
	u16 imt;
	/* Interrupt Clear timer (2us resolution) */
	u16 ict;

	unsigned long flags;
	/* structs defined in atl2_hw.h */
	u32 bd_number;     /* board number */
	bool pci_using_64;
	bool have_msi;
	struct atl2_hw hw;

	u32 usr_cmd;
	/* FIXME */
	/* u32 regs_buff[ATL2_REGS_LEN]; */
	u32 pci_state[16];

	u32 *config_space;
};

enum atl2_state_t {
	__ATL2_TESTING,
	__ATL2_RESETTING,
	__ATL2_DOWN
};

#endif /* _ATL2_H_ */
