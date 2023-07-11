// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "fman_dtsec.h"
#include "fman.h"
#include "mac.h"

#include <linux/slab.h>
#include <linux/bitrev.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/phy.h>
#include <linux/crc32.h>
#include <linux/of_mdio.h>
#include <linux/mii.h>
#include <linux/netdevice.h>

/* TBI register addresses */
#define MII_TBICON		0x11

/* TBICON register bit fields */
#define TBICON_SOFT_RESET	0x8000	/* Soft reset */
#define TBICON_DISABLE_RX_DIS	0x2000	/* Disable receive disparity */
#define TBICON_DISABLE_TX_DIS	0x1000	/* Disable transmit disparity */
#define TBICON_AN_SENSE		0x0100	/* Auto-negotiation sense enable */
#define TBICON_CLK_SELECT	0x0020	/* Clock select */
#define TBICON_MI_MODE		0x0010	/* GMII mode (TBI if not set) */

/* Interrupt Mask Register (IMASK) */
#define DTSEC_IMASK_BREN	0x80000000
#define DTSEC_IMASK_RXCEN	0x40000000
#define DTSEC_IMASK_MSROEN	0x04000000
#define DTSEC_IMASK_GTSCEN	0x02000000
#define DTSEC_IMASK_BTEN	0x01000000
#define DTSEC_IMASK_TXCEN	0x00800000
#define DTSEC_IMASK_TXEEN	0x00400000
#define DTSEC_IMASK_LCEN	0x00040000
#define DTSEC_IMASK_CRLEN	0x00020000
#define DTSEC_IMASK_XFUNEN	0x00010000
#define DTSEC_IMASK_ABRTEN	0x00008000
#define DTSEC_IMASK_IFERREN	0x00004000
#define DTSEC_IMASK_MAGEN	0x00000800
#define DTSEC_IMASK_MMRDEN	0x00000400
#define DTSEC_IMASK_MMWREN	0x00000200
#define DTSEC_IMASK_GRSCEN	0x00000100
#define DTSEC_IMASK_TDPEEN	0x00000002
#define DTSEC_IMASK_RDPEEN	0x00000001

#define DTSEC_EVENTS_MASK		\
	 ((u32)(DTSEC_IMASK_BREN    |	\
		DTSEC_IMASK_RXCEN   |	\
		DTSEC_IMASK_BTEN    |	\
		DTSEC_IMASK_TXCEN   |	\
		DTSEC_IMASK_TXEEN   |	\
		DTSEC_IMASK_ABRTEN  |	\
		DTSEC_IMASK_LCEN    |	\
		DTSEC_IMASK_CRLEN   |	\
		DTSEC_IMASK_XFUNEN  |	\
		DTSEC_IMASK_IFERREN |	\
		DTSEC_IMASK_MAGEN   |	\
		DTSEC_IMASK_TDPEEN  |	\
		DTSEC_IMASK_RDPEEN))

/* dtsec timestamp event bits */
#define TMR_PEMASK_TSREEN	0x00010000
#define TMR_PEVENT_TSRE		0x00010000

/* Group address bit indication */
#define MAC_GROUP_ADDRESS	0x0000010000000000ULL

/* Defaults */
#define DEFAULT_HALFDUP_RETRANSMIT		0xf
#define DEFAULT_HALFDUP_COLL_WINDOW		0x37
#define DEFAULT_TX_PAUSE_TIME			0xf000
#define DEFAULT_RX_PREPEND			0
#define DEFAULT_PREAMBLE_LEN			7
#define DEFAULT_TX_PAUSE_TIME_EXTD		0
#define DEFAULT_NON_BACK_TO_BACK_IPG1		0x40
#define DEFAULT_NON_BACK_TO_BACK_IPG2		0x60
#define DEFAULT_MIN_IFG_ENFORCEMENT		0x50
#define DEFAULT_BACK_TO_BACK_IPG		0x60
#define DEFAULT_MAXIMUM_FRAME			0x600

/* register related defines (bits, field offsets..) */
#define DTSEC_ID2_INT_REDUCED_OFF	0x00010000

#define DTSEC_ECNTRL_GMIIM		0x00000040
#define DTSEC_ECNTRL_TBIM		0x00000020
#define DTSEC_ECNTRL_RPM		0x00000010
#define DTSEC_ECNTRL_R100M		0x00000008
#define DTSEC_ECNTRL_RMM		0x00000004
#define DTSEC_ECNTRL_SGMIIM		0x00000002
#define DTSEC_ECNTRL_QSGMIIM		0x00000001

#define TCTRL_TTSE			0x00000040
#define TCTRL_GTS			0x00000020

#define RCTRL_PAL_MASK			0x001f0000
#define RCTRL_PAL_SHIFT			16
#define RCTRL_GHTX			0x00000400
#define RCTRL_RTSE			0x00000040
#define RCTRL_GRS			0x00000020
#define RCTRL_MPROM			0x00000008
#define RCTRL_RSF			0x00000004
#define RCTRL_UPROM			0x00000001

#define MACCFG1_SOFT_RESET		0x80000000
#define MACCFG1_RX_FLOW			0x00000020
#define MACCFG1_TX_FLOW			0x00000010
#define MACCFG1_TX_EN			0x00000001
#define MACCFG1_RX_EN			0x00000004

#define MACCFG2_NIBBLE_MODE		0x00000100
#define MACCFG2_BYTE_MODE		0x00000200
#define MACCFG2_PAD_CRC_EN		0x00000004
#define MACCFG2_FULL_DUPLEX		0x00000001
#define MACCFG2_PREAMBLE_LENGTH_MASK	0x0000f000
#define MACCFG2_PREAMBLE_LENGTH_SHIFT	12

#define IPGIFG_NON_BACK_TO_BACK_IPG_1_SHIFT	24
#define IPGIFG_NON_BACK_TO_BACK_IPG_2_SHIFT	16
#define IPGIFG_MIN_IFG_ENFORCEMENT_SHIFT	8

#define IPGIFG_NON_BACK_TO_BACK_IPG_1	0x7F000000
#define IPGIFG_NON_BACK_TO_BACK_IPG_2	0x007F0000
#define IPGIFG_MIN_IFG_ENFORCEMENT	0x0000FF00
#define IPGIFG_BACK_TO_BACK_IPG	0x0000007F

#define HAFDUP_EXCESS_DEFER			0x00010000
#define HAFDUP_COLLISION_WINDOW		0x000003ff
#define HAFDUP_RETRANSMISSION_MAX_SHIFT	12
#define HAFDUP_RETRANSMISSION_MAX		0x0000f000

#define NUM_OF_HASH_REGS	8	/* Number of hash table registers */

#define PTV_PTE_MASK		0xffff0000
#define PTV_PT_MASK		0x0000ffff
#define PTV_PTE_SHIFT		16

#define MAX_PACKET_ALIGNMENT		31
#define MAX_INTER_PACKET_GAP		0x7f
#define MAX_RETRANSMISSION		0x0f
#define MAX_COLLISION_WINDOW		0x03ff

/* Hash table size (32 bits*8 regs) */
#define DTSEC_HASH_TABLE_SIZE		256
/* Extended Hash table size (32 bits*16 regs) */
#define EXTENDED_HASH_TABLE_SIZE	512

/* dTSEC Memory Map registers */
struct dtsec_regs {
	/* dTSEC General Control and Status Registers */
	u32 tsec_id;		/* 0x000 ETSEC_ID register */
	u32 tsec_id2;		/* 0x004 ETSEC_ID2 register */
	u32 ievent;		/* 0x008 Interrupt event register */
	u32 imask;		/* 0x00C Interrupt mask register */
	u32 reserved0010[1];
	u32 ecntrl;		/* 0x014 E control register */
	u32 ptv;		/* 0x018 Pause time value register */
	u32 tbipa;		/* 0x01C TBI PHY address register */
	u32 tmr_ctrl;		/* 0x020 Time-stamp Control register */
	u32 tmr_pevent;		/* 0x024 Time-stamp event register */
	u32 tmr_pemask;		/* 0x028 Timer event mask register */
	u32 reserved002c[5];
	u32 tctrl;		/* 0x040 Transmit control register */
	u32 reserved0044[3];
	u32 rctrl;		/* 0x050 Receive control register */
	u32 reserved0054[11];
	u32 igaddr[8];		/* 0x080-0x09C Individual/group address */
	u32 gaddr[8];		/* 0x0A0-0x0BC Group address registers 0-7 */
	u32 reserved00c0[16];
	u32 maccfg1;		/* 0x100 MAC configuration #1 */
	u32 maccfg2;		/* 0x104 MAC configuration #2 */
	u32 ipgifg;		/* 0x108 IPG/IFG */
	u32 hafdup;		/* 0x10C Half-duplex */
	u32 maxfrm;		/* 0x110 Maximum frame */
	u32 reserved0114[10];
	u32 ifstat;		/* 0x13C Interface status */
	u32 macstnaddr1;	/* 0x140 Station Address,part 1 */
	u32 macstnaddr2;	/* 0x144 Station Address,part 2 */
	struct {
		u32 exact_match1;	/* octets 1-4 */
		u32 exact_match2;	/* octets 5-6 */
	} macaddr[15];		/* 0x148-0x1BC mac exact match addresses 1-15 */
	u32 reserved01c0[16];
	u32 tr64;	/* 0x200 Tx and Rx 64 byte frame counter */
	u32 tr127;	/* 0x204 Tx and Rx 65 to 127 byte frame counter */
	u32 tr255;	/* 0x208 Tx and Rx 128 to 255 byte frame counter */
	u32 tr511;	/* 0x20C Tx and Rx 256 to 511 byte frame counter */
	u32 tr1k;	/* 0x210 Tx and Rx 512 to 1023 byte frame counter */
	u32 trmax;	/* 0x214 Tx and Rx 1024 to 1518 byte frame counter */
	u32 trmgv;
	/* 0x218 Tx and Rx 1519 to 1522 byte good VLAN frame count */
	u32 rbyt;	/* 0x21C receive byte counter */
	u32 rpkt;	/* 0x220 receive packet counter */
	u32 rfcs;	/* 0x224 receive FCS error counter */
	u32 rmca;	/* 0x228 RMCA Rx multicast packet counter */
	u32 rbca;	/* 0x22C Rx broadcast packet counter */
	u32 rxcf;	/* 0x230 Rx control frame packet counter */
	u32 rxpf;	/* 0x234 Rx pause frame packet counter */
	u32 rxuo;	/* 0x238 Rx unknown OP code counter */
	u32 raln;	/* 0x23C Rx alignment error counter */
	u32 rflr;	/* 0x240 Rx frame length error counter */
	u32 rcde;	/* 0x244 Rx code error counter */
	u32 rcse;	/* 0x248 Rx carrier sense error counter */
	u32 rund;	/* 0x24C Rx undersize packet counter */
	u32 rovr;	/* 0x250 Rx oversize packet counter */
	u32 rfrg;	/* 0x254 Rx fragments counter */
	u32 rjbr;	/* 0x258 Rx jabber counter */
	u32 rdrp;	/* 0x25C Rx drop */
	u32 tbyt;	/* 0x260 Tx byte counter */
	u32 tpkt;	/* 0x264 Tx packet counter */
	u32 tmca;	/* 0x268 Tx multicast packet counter */
	u32 tbca;	/* 0x26C Tx broadcast packet counter */
	u32 txpf;	/* 0x270 Tx pause control frame counter */
	u32 tdfr;	/* 0x274 Tx deferral packet counter */
	u32 tedf;	/* 0x278 Tx excessive deferral packet counter */
	u32 tscl;	/* 0x27C Tx single collision packet counter */
	u32 tmcl;	/* 0x280 Tx multiple collision packet counter */
	u32 tlcl;	/* 0x284 Tx late collision packet counter */
	u32 txcl;	/* 0x288 Tx excessive collision packet counter */
	u32 tncl;	/* 0x28C Tx total collision counter */
	u32 reserved0290[1];
	u32 tdrp;	/* 0x294 Tx drop frame counter */
	u32 tjbr;	/* 0x298 Tx jabber frame counter */
	u32 tfcs;	/* 0x29C Tx FCS error counter */
	u32 txcf;	/* 0x2A0 Tx control frame counter */
	u32 tovr;	/* 0x2A4 Tx oversize frame counter */
	u32 tund;	/* 0x2A8 Tx undersize frame counter */
	u32 tfrg;	/* 0x2AC Tx fragments frame counter */
	u32 car1;	/* 0x2B0 carry register one register* */
	u32 car2;	/* 0x2B4 carry register two register* */
	u32 cam1;	/* 0x2B8 carry register one mask register */
	u32 cam2;	/* 0x2BC carry register two mask register */
	u32 reserved02c0[848];
};

/* struct dtsec_cfg - dTSEC configuration
 * Transmit half-duplex flow control, under software control for 10/100-Mbps
 * half-duplex media. If set, back pressure is applied to media by raising
 * carrier.
 * halfdup_retransmit:
 * Number of retransmission attempts following a collision.
 * If this is exceeded dTSEC aborts transmission due to excessive collisions.
 * The standard specifies the attempt limit to be 15.
 * halfdup_coll_window:
 * The number of bytes of the frame during which collisions may occur.
 * The default value of 55 corresponds to the frame byte at the end of the
 * standard 512-bit slot time window. If collisions are detected after this
 * byte, the late collision event is asserted and transmission of current
 * frame is aborted.
 * tx_pad_crc:
 * Pad and append CRC. If set, the MAC pads all ransmitted short frames and
 * appends a CRC to every frame regardless of padding requirement.
 * tx_pause_time:
 * Transmit pause time value. This pause value is used as part of the pause
 * frame to be sent when a transmit pause frame is initiated.
 * If set to 0 this disables transmission of pause frames.
 * preamble_len:
 * Length, in bytes, of the preamble field preceding each Ethernet
 * start-of-frame delimiter byte. The default value of 0x7 should be used in
 * order to guarantee reliable operation with IEEE 802.3 compliant hardware.
 * rx_prepend:
 * Packet alignment padding length. The specified number of bytes (1-31)
 * of zero padding are inserted before the start of each received frame.
 * For Ethernet, where optional preamble extraction is enabled, the padding
 * appears before the preamble, otherwise the padding precedes the
 * layer 2 header.
 *
 * This structure contains basic dTSEC configuration and must be passed to
 * init() function. A default set of configuration values can be
 * obtained by calling set_dflts().
 */
struct dtsec_cfg {
	u16 halfdup_retransmit;
	u16 halfdup_coll_window;
	bool tx_pad_crc;
	u16 tx_pause_time;
	bool ptp_tsu_en;
	bool ptp_exception_en;
	u32 preamble_len;
	u32 rx_prepend;
	u16 tx_pause_time_extd;
	u16 maximum_frame;
	u32 non_back_to_back_ipg1;
	u32 non_back_to_back_ipg2;
	u32 min_ifg_enforcement;
	u32 back_to_back_ipg;
};

struct fman_mac {
	/* pointer to dTSEC memory mapped registers */
	struct dtsec_regs __iomem *regs;
	/* MAC address of device */
	u64 addr;
	/* Ethernet physical interface */
	phy_interface_t phy_if;
	u16 max_speed;
	struct mac_device *dev_id; /* device cookie used by the exception cbs */
	fman_mac_exception_cb *exception_cb;
	fman_mac_exception_cb *event_cb;
	/* Number of individual addresses in registers for this station */
	u8 num_of_ind_addr_in_regs;
	/* pointer to driver's global address hash table */
	struct eth_hash_t *multicast_addr_hash;
	/* pointer to driver's individual address hash table */
	struct eth_hash_t *unicast_addr_hash;
	u8 mac_id;
	u32 exceptions;
	bool ptp_tsu_enabled;
	bool en_tsu_err_exception;
	struct dtsec_cfg *dtsec_drv_param;
	void *fm;
	struct fman_rev_info fm_rev_info;
	bool basex_if;
	struct mdio_device *tbidev;
	struct phylink_pcs pcs;
};

static void set_dflts(struct dtsec_cfg *cfg)
{
	cfg->halfdup_retransmit = DEFAULT_HALFDUP_RETRANSMIT;
	cfg->halfdup_coll_window = DEFAULT_HALFDUP_COLL_WINDOW;
	cfg->tx_pad_crc = true;
	cfg->tx_pause_time = DEFAULT_TX_PAUSE_TIME;
	/* PHY address 0 is reserved (DPAA RM) */
	cfg->rx_prepend = DEFAULT_RX_PREPEND;
	cfg->ptp_tsu_en = true;
	cfg->ptp_exception_en = true;
	cfg->preamble_len = DEFAULT_PREAMBLE_LEN;
	cfg->tx_pause_time_extd = DEFAULT_TX_PAUSE_TIME_EXTD;
	cfg->non_back_to_back_ipg1 = DEFAULT_NON_BACK_TO_BACK_IPG1;
	cfg->non_back_to_back_ipg2 = DEFAULT_NON_BACK_TO_BACK_IPG2;
	cfg->min_ifg_enforcement = DEFAULT_MIN_IFG_ENFORCEMENT;
	cfg->back_to_back_ipg = DEFAULT_BACK_TO_BACK_IPG;
	cfg->maximum_frame = DEFAULT_MAXIMUM_FRAME;
}

static void set_mac_address(struct dtsec_regs __iomem *regs, const u8 *adr)
{
	u32 tmp;

	tmp = (u32)((adr[5] << 24) |
		    (adr[4] << 16) | (adr[3] << 8) | adr[2]);
	iowrite32be(tmp, &regs->macstnaddr1);

	tmp = (u32)((adr[1] << 24) | (adr[0] << 16));
	iowrite32be(tmp, &regs->macstnaddr2);
}

static int init(struct dtsec_regs __iomem *regs, struct dtsec_cfg *cfg,
		phy_interface_t iface, u16 iface_speed, u64 addr,
		u32 exception_mask, u8 tbi_addr)
{
	enet_addr_t eth_addr;
	u32 tmp = 0;
	int i;

	/* Soft reset */
	iowrite32be(MACCFG1_SOFT_RESET, &regs->maccfg1);
	iowrite32be(0, &regs->maccfg1);

	if (cfg->tx_pause_time)
		tmp |= cfg->tx_pause_time;
	if (cfg->tx_pause_time_extd)
		tmp |= cfg->tx_pause_time_extd << PTV_PTE_SHIFT;
	iowrite32be(tmp, &regs->ptv);

	tmp = 0;
	tmp |= (cfg->rx_prepend << RCTRL_PAL_SHIFT) & RCTRL_PAL_MASK;
	/* Accept short frames */
	tmp |= RCTRL_RSF;

	iowrite32be(tmp, &regs->rctrl);

	/* Assign a Phy Address to the TBI (TBIPA).
	 * Done also in cases where TBI is not selected to avoid conflict with
	 * the external PHY's Physical address
	 */
	iowrite32be(tbi_addr, &regs->tbipa);

	iowrite32be(0, &regs->tmr_ctrl);

	if (cfg->ptp_tsu_en) {
		tmp = 0;
		tmp |= TMR_PEVENT_TSRE;
		iowrite32be(tmp, &regs->tmr_pevent);

		if (cfg->ptp_exception_en) {
			tmp = 0;
			tmp |= TMR_PEMASK_TSREEN;
			iowrite32be(tmp, &regs->tmr_pemask);
		}
	}

	tmp = 0;
	tmp |= MACCFG1_RX_FLOW;
	tmp |= MACCFG1_TX_FLOW;
	iowrite32be(tmp, &regs->maccfg1);

	tmp = 0;

	tmp |= (cfg->preamble_len << MACCFG2_PREAMBLE_LENGTH_SHIFT) &
		MACCFG2_PREAMBLE_LENGTH_MASK;
	if (cfg->tx_pad_crc)
		tmp |= MACCFG2_PAD_CRC_EN;
	iowrite32be(tmp, &regs->maccfg2);

	tmp = (((cfg->non_back_to_back_ipg1 <<
		 IPGIFG_NON_BACK_TO_BACK_IPG_1_SHIFT)
		& IPGIFG_NON_BACK_TO_BACK_IPG_1)
	       | ((cfg->non_back_to_back_ipg2 <<
		   IPGIFG_NON_BACK_TO_BACK_IPG_2_SHIFT)
		 & IPGIFG_NON_BACK_TO_BACK_IPG_2)
	       | ((cfg->min_ifg_enforcement << IPGIFG_MIN_IFG_ENFORCEMENT_SHIFT)
		 & IPGIFG_MIN_IFG_ENFORCEMENT)
	       | (cfg->back_to_back_ipg & IPGIFG_BACK_TO_BACK_IPG));
	iowrite32be(tmp, &regs->ipgifg);

	tmp = 0;
	tmp |= HAFDUP_EXCESS_DEFER;
	tmp |= ((cfg->halfdup_retransmit << HAFDUP_RETRANSMISSION_MAX_SHIFT)
		& HAFDUP_RETRANSMISSION_MAX);
	tmp |= (cfg->halfdup_coll_window & HAFDUP_COLLISION_WINDOW);

	iowrite32be(tmp, &regs->hafdup);

	/* Initialize Maximum frame length */
	iowrite32be(cfg->maximum_frame, &regs->maxfrm);

	iowrite32be(0xffffffff, &regs->cam1);
	iowrite32be(0xffffffff, &regs->cam2);

	iowrite32be(exception_mask, &regs->imask);

	iowrite32be(0xffffffff, &regs->ievent);

	if (addr) {
		MAKE_ENET_ADDR_FROM_UINT64(addr, eth_addr);
		set_mac_address(regs, (const u8 *)eth_addr);
	}

	/* HASH */
	for (i = 0; i < NUM_OF_HASH_REGS; i++) {
		/* Initialize IADDRx */
		iowrite32be(0, &regs->igaddr[i]);
		/* Initialize GADDRx */
		iowrite32be(0, &regs->gaddr[i]);
	}

	return 0;
}

static void set_bucket(struct dtsec_regs __iomem *regs, int bucket,
		       bool enable)
{
	int reg_idx = (bucket >> 5) & 0xf;
	int bit_idx = bucket & 0x1f;
	u32 bit_mask = 0x80000000 >> bit_idx;
	u32 __iomem *reg;

	if (reg_idx > 7)
		reg = &regs->gaddr[reg_idx - 8];
	else
		reg = &regs->igaddr[reg_idx];

	if (enable)
		iowrite32be(ioread32be(reg) | bit_mask, reg);
	else
		iowrite32be(ioread32be(reg) & (~bit_mask), reg);
}

static int check_init_parameters(struct fman_mac *dtsec)
{
	if ((dtsec->dtsec_drv_param)->rx_prepend >
	    MAX_PACKET_ALIGNMENT) {
		pr_err("packetAlignmentPadding can't be > than %d\n",
		       MAX_PACKET_ALIGNMENT);
		return -EINVAL;
	}
	if (((dtsec->dtsec_drv_param)->non_back_to_back_ipg1 >
	     MAX_INTER_PACKET_GAP) ||
	    ((dtsec->dtsec_drv_param)->non_back_to_back_ipg2 >
	     MAX_INTER_PACKET_GAP) ||
	     ((dtsec->dtsec_drv_param)->back_to_back_ipg >
	      MAX_INTER_PACKET_GAP)) {
		pr_err("Inter packet gap can't be greater than %d\n",
		       MAX_INTER_PACKET_GAP);
		return -EINVAL;
	}
	if ((dtsec->dtsec_drv_param)->halfdup_retransmit >
	    MAX_RETRANSMISSION) {
		pr_err("maxRetransmission can't be greater than %d\n",
		       MAX_RETRANSMISSION);
		return -EINVAL;
	}
	if ((dtsec->dtsec_drv_param)->halfdup_coll_window >
	    MAX_COLLISION_WINDOW) {
		pr_err("collisionWindow can't be greater than %d\n",
		       MAX_COLLISION_WINDOW);
		return -EINVAL;
	/* If Auto negotiation process is disabled, need to set up the PHY
	 * using the MII Management Interface
	 */
	}
	if (!dtsec->exception_cb) {
		pr_err("uninitialized exception_cb\n");
		return -EINVAL;
	}
	if (!dtsec->event_cb) {
		pr_err("uninitialized event_cb\n");
		return -EINVAL;
	}

	return 0;
}

static int get_exception_flag(enum fman_mac_exceptions exception)
{
	u32 bit_mask;

	switch (exception) {
	case FM_MAC_EX_1G_BAB_RX:
		bit_mask = DTSEC_IMASK_BREN;
		break;
	case FM_MAC_EX_1G_RX_CTL:
		bit_mask = DTSEC_IMASK_RXCEN;
		break;
	case FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET:
		bit_mask = DTSEC_IMASK_GTSCEN;
		break;
	case FM_MAC_EX_1G_BAB_TX:
		bit_mask = DTSEC_IMASK_BTEN;
		break;
	case FM_MAC_EX_1G_TX_CTL:
		bit_mask = DTSEC_IMASK_TXCEN;
		break;
	case FM_MAC_EX_1G_TX_ERR:
		bit_mask = DTSEC_IMASK_TXEEN;
		break;
	case FM_MAC_EX_1G_LATE_COL:
		bit_mask = DTSEC_IMASK_LCEN;
		break;
	case FM_MAC_EX_1G_COL_RET_LMT:
		bit_mask = DTSEC_IMASK_CRLEN;
		break;
	case FM_MAC_EX_1G_TX_FIFO_UNDRN:
		bit_mask = DTSEC_IMASK_XFUNEN;
		break;
	case FM_MAC_EX_1G_MAG_PCKT:
		bit_mask = DTSEC_IMASK_MAGEN;
		break;
	case FM_MAC_EX_1G_MII_MNG_RD_COMPLET:
		bit_mask = DTSEC_IMASK_MMRDEN;
		break;
	case FM_MAC_EX_1G_MII_MNG_WR_COMPLET:
		bit_mask = DTSEC_IMASK_MMWREN;
		break;
	case FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET:
		bit_mask = DTSEC_IMASK_GRSCEN;
		break;
	case FM_MAC_EX_1G_DATA_ERR:
		bit_mask = DTSEC_IMASK_TDPEEN;
		break;
	case FM_MAC_EX_1G_RX_MIB_CNT_OVFL:
		bit_mask = DTSEC_IMASK_MSROEN;
		break;
	default:
		bit_mask = 0;
		break;
	}

	return bit_mask;
}

static u16 dtsec_get_max_frame_length(struct fman_mac *dtsec)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;

	return (u16)ioread32be(&regs->maxfrm);
}

static void dtsec_isr(void *handle)
{
	struct fman_mac *dtsec = (struct fman_mac *)handle;
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 event;

	/* do not handle MDIO events */
	event = ioread32be(&regs->ievent) &
		(u32)(~(DTSEC_IMASK_MMRDEN | DTSEC_IMASK_MMWREN));

	event &= ioread32be(&regs->imask);

	iowrite32be(event, &regs->ievent);

	if (event & DTSEC_IMASK_BREN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_BAB_RX);
	if (event & DTSEC_IMASK_RXCEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_RX_CTL);
	if (event & DTSEC_IMASK_GTSCEN)
		dtsec->exception_cb(dtsec->dev_id,
				    FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET);
	if (event & DTSEC_IMASK_BTEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_BAB_TX);
	if (event & DTSEC_IMASK_TXCEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_TX_CTL);
	if (event & DTSEC_IMASK_TXEEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_TX_ERR);
	if (event & DTSEC_IMASK_LCEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_LATE_COL);
	if (event & DTSEC_IMASK_CRLEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_COL_RET_LMT);
	if (event & DTSEC_IMASK_XFUNEN) {
		/* FM_TX_LOCKUP_ERRATA_DTSEC6 Errata workaround */
		/* FIXME: This races with the rest of the driver! */
		if (dtsec->fm_rev_info.major == 2) {
			u32 tpkt1, tmp_reg1, tpkt2, tmp_reg2, i;
			/* a. Write 0x00E0_0C00 to DTSEC_ID
			 *	This is a read only register
			 * b. Read and save the value of TPKT
			 */
			tpkt1 = ioread32be(&regs->tpkt);

			/* c. Read the register at dTSEC address offset 0x32C */
			tmp_reg1 = ioread32be(&regs->reserved02c0[27]);

			/* d. Compare bits [9:15] to bits [25:31] of the
			 * register at address offset 0x32C.
			 */
			if ((tmp_reg1 & 0x007F0000) !=
				(tmp_reg1 & 0x0000007F)) {
				/* If they are not equal, save the value of
				 * this register and wait for at least
				 * MAXFRM*16 ns
				 */
				usleep_range((u32)(min
					(dtsec_get_max_frame_length(dtsec) *
					16 / 1000, 1)), (u32)
					(min(dtsec_get_max_frame_length
					(dtsec) * 16 / 1000, 1) + 1));
			}

			/* e. Read and save TPKT again and read the register
			 * at dTSEC address offset 0x32C again
			 */
			tpkt2 = ioread32be(&regs->tpkt);
			tmp_reg2 = ioread32be(&regs->reserved02c0[27]);

			/* f. Compare the value of TPKT saved in step b to
			 * value read in step e. Also compare bits [9:15] of
			 * the register at offset 0x32C saved in step d to the
			 * value of bits [9:15] saved in step e. If the two
			 * registers values are unchanged, then the transmit
			 * portion of the dTSEC controller is locked up and
			 * the user should proceed to the recover sequence.
			 */
			if ((tpkt1 == tpkt2) && ((tmp_reg1 & 0x007F0000) ==
				(tmp_reg2 & 0x007F0000))) {
				/* recover sequence */

				/* a.Write a 1 to RCTRL[GRS] */

				iowrite32be(ioread32be(&regs->rctrl) |
					    RCTRL_GRS, &regs->rctrl);

				/* b.Wait until IEVENT[GRSC]=1, or at least
				 * 100 us has elapsed.
				 */
				for (i = 0; i < 100; i++) {
					if (ioread32be(&regs->ievent) &
					    DTSEC_IMASK_GRSCEN)
						break;
					udelay(1);
				}
				if (ioread32be(&regs->ievent) &
				    DTSEC_IMASK_GRSCEN)
					iowrite32be(DTSEC_IMASK_GRSCEN,
						    &regs->ievent);
				else
					pr_debug("Rx lockup due to Tx lockup\n");

				/* c.Write a 1 to bit n of FM_RSTC
				 * (offset 0x0CC of FPM)
				 */
				fman_reset_mac(dtsec->fm, dtsec->mac_id);

				/* d.Wait 4 Tx clocks (32 ns) */
				udelay(1);

				/* e.Write a 0 to bit n of FM_RSTC. */
				/* cleared by FMAN
				 */
			}
		}

		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_TX_FIFO_UNDRN);
	}
	if (event & DTSEC_IMASK_MAGEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_MAG_PCKT);
	if (event & DTSEC_IMASK_GRSCEN)
		dtsec->exception_cb(dtsec->dev_id,
				    FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET);
	if (event & DTSEC_IMASK_TDPEEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_EX_1G_DATA_ERR);
	if (event & DTSEC_IMASK_RDPEEN)
		dtsec->exception_cb(dtsec->dev_id, FM_MAC_1G_RX_DATA_ERR);

	/* masked interrupts */
	WARN_ON(event & DTSEC_IMASK_ABRTEN);
	WARN_ON(event & DTSEC_IMASK_IFERREN);
}

static void dtsec_1588_isr(void *handle)
{
	struct fman_mac *dtsec = (struct fman_mac *)handle;
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 event;

	if (dtsec->ptp_tsu_enabled) {
		event = ioread32be(&regs->tmr_pevent);
		event &= ioread32be(&regs->tmr_pemask);

		if (event) {
			iowrite32be(event, &regs->tmr_pevent);
			WARN_ON(event & TMR_PEVENT_TSRE);
			dtsec->exception_cb(dtsec->dev_id,
					    FM_MAC_EX_1G_1588_TS_RX_ERR);
		}
	}
}

static void free_init_resources(struct fman_mac *dtsec)
{
	fman_unregister_intr(dtsec->fm, FMAN_MOD_MAC, dtsec->mac_id,
			     FMAN_INTR_TYPE_ERR);
	fman_unregister_intr(dtsec->fm, FMAN_MOD_MAC, dtsec->mac_id,
			     FMAN_INTR_TYPE_NORMAL);

	/* release the driver's group hash table */
	free_hash_table(dtsec->multicast_addr_hash);
	dtsec->multicast_addr_hash = NULL;

	/* release the driver's individual hash table */
	free_hash_table(dtsec->unicast_addr_hash);
	dtsec->unicast_addr_hash = NULL;
}

static struct fman_mac *pcs_to_dtsec(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct fman_mac, pcs);
}

static void dtsec_pcs_get_state(struct phylink_pcs *pcs,
				struct phylink_link_state *state)
{
	struct fman_mac *dtsec = pcs_to_dtsec(pcs);

	phylink_mii_c22_pcs_get_state(dtsec->tbidev, state);
}

static int dtsec_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			    phy_interface_t interface,
			    const unsigned long *advertising,
			    bool permit_pause_to_mac)
{
	struct fman_mac *dtsec = pcs_to_dtsec(pcs);

	return phylink_mii_c22_pcs_config(dtsec->tbidev, interface,
					  advertising, neg_mode);
}

static void dtsec_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct fman_mac *dtsec = pcs_to_dtsec(pcs);

	phylink_mii_c22_pcs_an_restart(dtsec->tbidev);
}

static const struct phylink_pcs_ops dtsec_pcs_ops = {
	.pcs_get_state = dtsec_pcs_get_state,
	.pcs_config = dtsec_pcs_config,
	.pcs_an_restart = dtsec_pcs_an_restart,
};

static void graceful_start(struct fman_mac *dtsec)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;

	iowrite32be(ioread32be(&regs->tctrl) & ~TCTRL_GTS, &regs->tctrl);
	iowrite32be(ioread32be(&regs->rctrl) & ~RCTRL_GRS, &regs->rctrl);
}

static void graceful_stop(struct fman_mac *dtsec)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 tmp;

	/* Graceful stop - Assert the graceful Rx stop bit */
	tmp = ioread32be(&regs->rctrl) | RCTRL_GRS;
	iowrite32be(tmp, &regs->rctrl);

	if (dtsec->fm_rev_info.major == 2) {
		/* Workaround for dTSEC Errata A002 */
		usleep_range(100, 200);
	} else {
		/* Workaround for dTSEC Errata A004839 */
		usleep_range(10, 50);
	}

	/* Graceful stop - Assert the graceful Tx stop bit */
	if (dtsec->fm_rev_info.major == 2) {
		/* dTSEC Errata A004: Do not use TCTRL[GTS]=1 */
		pr_debug("GTS not supported due to DTSEC_A004 Errata.\n");
	} else {
		tmp = ioread32be(&regs->tctrl) | TCTRL_GTS;
		iowrite32be(tmp, &regs->tctrl);

		/* Workaround for dTSEC Errata A0012, A0014 */
		usleep_range(10, 50);
	}
}

static int dtsec_enable(struct fman_mac *dtsec)
{
	return 0;
}

static void dtsec_disable(struct fman_mac *dtsec)
{
}

static int dtsec_set_tx_pause_frames(struct fman_mac *dtsec,
				     u8 __maybe_unused priority,
				     u16 pause_time,
				     u16 __maybe_unused thresh_time)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 ptv = 0;

	if (pause_time) {
		/* FM_BAD_TX_TS_IN_B_2_B_ERRATA_DTSEC_A003 Errata workaround */
		if (dtsec->fm_rev_info.major == 2 && pause_time <= 320) {
			pr_warn("pause-time: %d illegal.Should be > 320\n",
				pause_time);
			return -EINVAL;
		}

		ptv = ioread32be(&regs->ptv);
		ptv &= PTV_PTE_MASK;
		ptv |= pause_time & PTV_PT_MASK;
		iowrite32be(ptv, &regs->ptv);

		/* trigger the transmission of a flow-control pause frame */
		iowrite32be(ioread32be(&regs->maccfg1) | MACCFG1_TX_FLOW,
			    &regs->maccfg1);
	} else
		iowrite32be(ioread32be(&regs->maccfg1) & ~MACCFG1_TX_FLOW,
			    &regs->maccfg1);

	return 0;
}

static int dtsec_accept_rx_pause_frames(struct fman_mac *dtsec, bool en)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 tmp;

	tmp = ioread32be(&regs->maccfg1);
	if (en)
		tmp |= MACCFG1_RX_FLOW;
	else
		tmp &= ~MACCFG1_RX_FLOW;
	iowrite32be(tmp, &regs->maccfg1);

	return 0;
}

static struct phylink_pcs *dtsec_select_pcs(struct phylink_config *config,
					    phy_interface_t iface)
{
	struct fman_mac *dtsec = fman_config_to_mac(config)->fman_mac;

	switch (iface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return &dtsec->pcs;
	default:
		return NULL;
	}
}

static void dtsec_mac_config(struct phylink_config *config, unsigned int mode,
			     const struct phylink_link_state *state)
{
	struct mac_device *mac_dev = fman_config_to_mac(config);
	struct dtsec_regs __iomem *regs = mac_dev->fman_mac->regs;
	u32 tmp;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_RMII:
		tmp = DTSEC_ECNTRL_RMM;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		tmp = DTSEC_ECNTRL_GMIIM | DTSEC_ECNTRL_RPM;
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		tmp = DTSEC_ECNTRL_TBIM | DTSEC_ECNTRL_SGMIIM;
		break;
	default:
		dev_warn(mac_dev->dev, "cannot configure dTSEC for %s\n",
			 phy_modes(state->interface));
		return;
	}

	iowrite32be(tmp, &regs->ecntrl);
}

static void dtsec_link_up(struct phylink_config *config, struct phy_device *phy,
			  unsigned int mode, phy_interface_t interface,
			  int speed, int duplex, bool tx_pause, bool rx_pause)
{
	struct mac_device *mac_dev = fman_config_to_mac(config);
	struct fman_mac *dtsec = mac_dev->fman_mac;
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u16 pause_time = tx_pause ? FSL_FM_PAUSE_TIME_ENABLE :
			 FSL_FM_PAUSE_TIME_DISABLE;
	u32 tmp;

	dtsec_set_tx_pause_frames(dtsec, 0, pause_time, 0);
	dtsec_accept_rx_pause_frames(dtsec, rx_pause);

	tmp = ioread32be(&regs->ecntrl);
	if (speed == SPEED_100)
		tmp |= DTSEC_ECNTRL_R100M;
	else
		tmp &= ~DTSEC_ECNTRL_R100M;
	iowrite32be(tmp, &regs->ecntrl);

	tmp = ioread32be(&regs->maccfg2);
	tmp &= ~(MACCFG2_NIBBLE_MODE | MACCFG2_BYTE_MODE | MACCFG2_FULL_DUPLEX);
	if (speed >= SPEED_1000)
		tmp |= MACCFG2_BYTE_MODE;
	else
		tmp |= MACCFG2_NIBBLE_MODE;

	if (duplex == DUPLEX_FULL)
		tmp |= MACCFG2_FULL_DUPLEX;

	iowrite32be(tmp, &regs->maccfg2);

	mac_dev->update_speed(mac_dev, speed);

	/* Enable */
	tmp = ioread32be(&regs->maccfg1);
	tmp |= MACCFG1_RX_EN | MACCFG1_TX_EN;
	iowrite32be(tmp, &regs->maccfg1);

	/* Graceful start - clear the graceful Rx/Tx stop bit */
	graceful_start(dtsec);
}

static void dtsec_link_down(struct phylink_config *config, unsigned int mode,
			    phy_interface_t interface)
{
	struct fman_mac *dtsec = fman_config_to_mac(config)->fman_mac;
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 tmp;

	/* Graceful stop - Assert the graceful Rx/Tx stop bit */
	graceful_stop(dtsec);

	tmp = ioread32be(&regs->maccfg1);
	tmp &= ~(MACCFG1_RX_EN | MACCFG1_TX_EN);
	iowrite32be(tmp, &regs->maccfg1);
}

static const struct phylink_mac_ops dtsec_mac_ops = {
	.mac_select_pcs = dtsec_select_pcs,
	.mac_config = dtsec_mac_config,
	.mac_link_up = dtsec_link_up,
	.mac_link_down = dtsec_link_down,
};

static int dtsec_modify_mac_address(struct fman_mac *dtsec,
				    const enet_addr_t *enet_addr)
{
	graceful_stop(dtsec);

	/* Initialize MAC Station Address registers (1 & 2)
	 * Station address have to be swapped (big endian to little endian
	 */
	dtsec->addr = ENET_ADDR_TO_UINT64(*enet_addr);
	set_mac_address(dtsec->regs, (const u8 *)(*enet_addr));

	graceful_start(dtsec);

	return 0;
}

static int dtsec_add_hash_mac_address(struct fman_mac *dtsec,
				      enet_addr_t *eth_addr)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	struct eth_hash_entry *hash_entry;
	u64 addr;
	s32 bucket;
	u32 crc = 0xFFFFFFFF;
	bool mcast, ghtx;

	addr = ENET_ADDR_TO_UINT64(*eth_addr);

	ghtx = (bool)((ioread32be(&regs->rctrl) & RCTRL_GHTX) ? true : false);
	mcast = (bool)((addr & MAC_GROUP_ADDRESS) ? true : false);

	/* Cannot handle unicast mac addr when GHTX is on */
	if (ghtx && !mcast) {
		pr_err("Could not compute hash bucket\n");
		return -EINVAL;
	}
	crc = crc32_le(crc, (u8 *)eth_addr, ETH_ALEN);
	crc = bitrev32(crc);

	/* considering the 9 highest order bits in crc H[8:0]:
	 *if ghtx = 0 H[8:6] (highest order 3 bits) identify the hash register
	 *and H[5:1] (next 5 bits) identify the hash bit
	 *if ghts = 1 H[8:5] (highest order 4 bits) identify the hash register
	 *and H[4:0] (next 5 bits) identify the hash bit.
	 *
	 *In bucket index output the low 5 bits identify the hash register
	 *bit, while the higher 4 bits identify the hash register
	 */

	if (ghtx) {
		bucket = (s32)((crc >> 23) & 0x1ff);
	} else {
		bucket = (s32)((crc >> 24) & 0xff);
		/* if !ghtx and mcast the bit must be set in gaddr instead of
		 *igaddr.
		 */
		if (mcast)
			bucket += 0x100;
	}

	set_bucket(dtsec->regs, bucket, true);

	/* Create element to be added to the driver hash table */
	hash_entry = kmalloc(sizeof(*hash_entry), GFP_ATOMIC);
	if (!hash_entry)
		return -ENOMEM;
	hash_entry->addr = addr;
	INIT_LIST_HEAD(&hash_entry->node);

	if (addr & MAC_GROUP_ADDRESS)
		/* Group Address */
		list_add_tail(&hash_entry->node,
			      &dtsec->multicast_addr_hash->lsts[bucket]);
	else
		list_add_tail(&hash_entry->node,
			      &dtsec->unicast_addr_hash->lsts[bucket]);

	return 0;
}

static int dtsec_set_allmulti(struct fman_mac *dtsec, bool enable)
{
	u32 tmp;
	struct dtsec_regs __iomem *regs = dtsec->regs;

	tmp = ioread32be(&regs->rctrl);
	if (enable)
		tmp |= RCTRL_MPROM;
	else
		tmp &= ~RCTRL_MPROM;

	iowrite32be(tmp, &regs->rctrl);

	return 0;
}

static int dtsec_set_tstamp(struct fman_mac *dtsec, bool enable)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 rctrl, tctrl;

	rctrl = ioread32be(&regs->rctrl);
	tctrl = ioread32be(&regs->tctrl);

	if (enable) {
		rctrl |= RCTRL_RTSE;
		tctrl |= TCTRL_TTSE;
	} else {
		rctrl &= ~RCTRL_RTSE;
		tctrl &= ~TCTRL_TTSE;
	}

	iowrite32be(rctrl, &regs->rctrl);
	iowrite32be(tctrl, &regs->tctrl);

	return 0;
}

static int dtsec_del_hash_mac_address(struct fman_mac *dtsec,
				      enet_addr_t *eth_addr)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	struct list_head *pos;
	struct eth_hash_entry *hash_entry = NULL;
	u64 addr;
	s32 bucket;
	u32 crc = 0xFFFFFFFF;
	bool mcast, ghtx;

	addr = ENET_ADDR_TO_UINT64(*eth_addr);

	ghtx = (bool)((ioread32be(&regs->rctrl) & RCTRL_GHTX) ? true : false);
	mcast = (bool)((addr & MAC_GROUP_ADDRESS) ? true : false);

	/* Cannot handle unicast mac addr when GHTX is on */
	if (ghtx && !mcast) {
		pr_err("Could not compute hash bucket\n");
		return -EINVAL;
	}
	crc = crc32_le(crc, (u8 *)eth_addr, ETH_ALEN);
	crc = bitrev32(crc);

	if (ghtx) {
		bucket = (s32)((crc >> 23) & 0x1ff);
	} else {
		bucket = (s32)((crc >> 24) & 0xff);
		/* if !ghtx and mcast the bit must be set
		 * in gaddr instead of igaddr.
		 */
		if (mcast)
			bucket += 0x100;
	}

	if (addr & MAC_GROUP_ADDRESS) {
		/* Group Address */
		list_for_each(pos,
			      &dtsec->multicast_addr_hash->lsts[bucket]) {
			hash_entry = ETH_HASH_ENTRY_OBJ(pos);
			if (hash_entry && hash_entry->addr == addr) {
				list_del_init(&hash_entry->node);
				kfree(hash_entry);
				break;
			}
		}
		if (list_empty(&dtsec->multicast_addr_hash->lsts[bucket]))
			set_bucket(dtsec->regs, bucket, false);
	} else {
		/* Individual Address */
		list_for_each(pos,
			      &dtsec->unicast_addr_hash->lsts[bucket]) {
			hash_entry = ETH_HASH_ENTRY_OBJ(pos);
			if (hash_entry && hash_entry->addr == addr) {
				list_del_init(&hash_entry->node);
				kfree(hash_entry);
				break;
			}
		}
		if (list_empty(&dtsec->unicast_addr_hash->lsts[bucket]))
			set_bucket(dtsec->regs, bucket, false);
	}

	/* address does not exist */
	WARN_ON(!hash_entry);

	return 0;
}

static int dtsec_set_promiscuous(struct fman_mac *dtsec, bool new_val)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 tmp;

	/* Set unicast promiscuous */
	tmp = ioread32be(&regs->rctrl);
	if (new_val)
		tmp |= RCTRL_UPROM;
	else
		tmp &= ~RCTRL_UPROM;

	iowrite32be(tmp, &regs->rctrl);

	/* Set multicast promiscuous */
	tmp = ioread32be(&regs->rctrl);
	if (new_val)
		tmp |= RCTRL_MPROM;
	else
		tmp &= ~RCTRL_MPROM;

	iowrite32be(tmp, &regs->rctrl);

	return 0;
}

static int dtsec_set_exception(struct fman_mac *dtsec,
			       enum fman_mac_exceptions exception, bool enable)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	u32 bit_mask = 0;

	if (exception != FM_MAC_EX_1G_1588_TS_RX_ERR) {
		bit_mask = get_exception_flag(exception);
		if (bit_mask) {
			if (enable)
				dtsec->exceptions |= bit_mask;
			else
				dtsec->exceptions &= ~bit_mask;
		} else {
			pr_err("Undefined exception\n");
			return -EINVAL;
		}
		if (enable)
			iowrite32be(ioread32be(&regs->imask) | bit_mask,
				    &regs->imask);
		else
			iowrite32be(ioread32be(&regs->imask) & ~bit_mask,
				    &regs->imask);
	} else {
		if (!dtsec->ptp_tsu_enabled) {
			pr_err("Exception valid for 1588 only\n");
			return -EINVAL;
		}
		switch (exception) {
		case FM_MAC_EX_1G_1588_TS_RX_ERR:
			if (enable) {
				dtsec->en_tsu_err_exception = true;
				iowrite32be(ioread32be(&regs->tmr_pemask) |
					    TMR_PEMASK_TSREEN,
					    &regs->tmr_pemask);
			} else {
				dtsec->en_tsu_err_exception = false;
				iowrite32be(ioread32be(&regs->tmr_pemask) &
					    ~TMR_PEMASK_TSREEN,
					    &regs->tmr_pemask);
			}
			break;
		default:
			pr_err("Undefined exception\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int dtsec_init(struct fman_mac *dtsec)
{
	struct dtsec_regs __iomem *regs = dtsec->regs;
	struct dtsec_cfg *dtsec_drv_param;
	u16 max_frm_ln, tbicon;
	int err;

	if (DEFAULT_RESET_ON_INIT &&
	    (fman_reset_mac(dtsec->fm, dtsec->mac_id) != 0)) {
		pr_err("Can't reset MAC!\n");
		return -EINVAL;
	}

	err = check_init_parameters(dtsec);
	if (err)
		return err;

	dtsec_drv_param = dtsec->dtsec_drv_param;

	err = init(dtsec->regs, dtsec_drv_param, dtsec->phy_if,
		   dtsec->max_speed, dtsec->addr, dtsec->exceptions,
		   dtsec->tbidev->addr);
	if (err) {
		free_init_resources(dtsec);
		pr_err("DTSEC version doesn't support this i/f mode\n");
		return err;
	}

	/* Configure the TBI PHY Control Register */
	tbicon = TBICON_CLK_SELECT | TBICON_SOFT_RESET;
	mdiodev_write(dtsec->tbidev, MII_TBICON, tbicon);

	tbicon = TBICON_CLK_SELECT;
	mdiodev_write(dtsec->tbidev, MII_TBICON, tbicon);

	/* Max Frame Length */
	max_frm_ln = (u16)ioread32be(&regs->maxfrm);
	err = fman_set_mac_max_frame(dtsec->fm, dtsec->mac_id, max_frm_ln);
	if (err) {
		pr_err("Setting max frame length failed\n");
		free_init_resources(dtsec);
		return -EINVAL;
	}

	dtsec->multicast_addr_hash =
	alloc_hash_table(EXTENDED_HASH_TABLE_SIZE);
	if (!dtsec->multicast_addr_hash) {
		free_init_resources(dtsec);
		pr_err("MC hash table is failed\n");
		return -ENOMEM;
	}

	dtsec->unicast_addr_hash = alloc_hash_table(DTSEC_HASH_TABLE_SIZE);
	if (!dtsec->unicast_addr_hash) {
		free_init_resources(dtsec);
		pr_err("UC hash table is failed\n");
		return -ENOMEM;
	}

	/* register err intr handler for dtsec to FPM (err) */
	fman_register_intr(dtsec->fm, FMAN_MOD_MAC, dtsec->mac_id,
			   FMAN_INTR_TYPE_ERR, dtsec_isr, dtsec);
	/* register 1588 intr handler for TMR to FPM (normal) */
	fman_register_intr(dtsec->fm, FMAN_MOD_MAC, dtsec->mac_id,
			   FMAN_INTR_TYPE_NORMAL, dtsec_1588_isr, dtsec);

	kfree(dtsec_drv_param);
	dtsec->dtsec_drv_param = NULL;

	return 0;
}

static int dtsec_free(struct fman_mac *dtsec)
{
	free_init_resources(dtsec);

	kfree(dtsec->dtsec_drv_param);
	dtsec->dtsec_drv_param = NULL;
	if (!IS_ERR_OR_NULL(dtsec->tbidev))
		put_device(&dtsec->tbidev->dev);
	kfree(dtsec);

	return 0;
}

static struct fman_mac *dtsec_config(struct mac_device *mac_dev,
				     struct fman_mac_params *params)
{
	struct fman_mac *dtsec;
	struct dtsec_cfg *dtsec_drv_param;

	/* allocate memory for the UCC GETH data structure. */
	dtsec = kzalloc(sizeof(*dtsec), GFP_KERNEL);
	if (!dtsec)
		return NULL;

	/* allocate memory for the d_tsec driver parameters data structure. */
	dtsec_drv_param = kzalloc(sizeof(*dtsec_drv_param), GFP_KERNEL);
	if (!dtsec_drv_param)
		goto err_dtsec;

	/* Plant parameter structure pointer */
	dtsec->dtsec_drv_param = dtsec_drv_param;

	set_dflts(dtsec_drv_param);

	dtsec->regs = mac_dev->vaddr;
	dtsec->addr = ENET_ADDR_TO_UINT64(mac_dev->addr);
	dtsec->phy_if = mac_dev->phy_if;
	dtsec->mac_id = params->mac_id;
	dtsec->exceptions = (DTSEC_IMASK_BREN	|
			     DTSEC_IMASK_RXCEN	|
			     DTSEC_IMASK_BTEN	|
			     DTSEC_IMASK_TXCEN	|
			     DTSEC_IMASK_TXEEN	|
			     DTSEC_IMASK_ABRTEN	|
			     DTSEC_IMASK_LCEN	|
			     DTSEC_IMASK_CRLEN	|
			     DTSEC_IMASK_XFUNEN	|
			     DTSEC_IMASK_IFERREN |
			     DTSEC_IMASK_MAGEN	|
			     DTSEC_IMASK_TDPEEN	|
			     DTSEC_IMASK_RDPEEN);
	dtsec->exception_cb = params->exception_cb;
	dtsec->event_cb = params->event_cb;
	dtsec->dev_id = mac_dev;
	dtsec->ptp_tsu_enabled = dtsec->dtsec_drv_param->ptp_tsu_en;
	dtsec->en_tsu_err_exception = dtsec->dtsec_drv_param->ptp_exception_en;

	dtsec->fm = params->fm;

	/* Save FMan revision */
	fman_get_revision(dtsec->fm, &dtsec->fm_rev_info);

	return dtsec;

err_dtsec:
	kfree(dtsec);
	return NULL;
}

int dtsec_initialization(struct mac_device *mac_dev,
			 struct device_node *mac_node,
			 struct fman_mac_params *params)
{
	int			err;
	struct fman_mac		*dtsec;
	struct device_node	*phy_node;
	unsigned long		 capabilities;
	unsigned long		*supported;

	mac_dev->phylink_ops		= &dtsec_mac_ops;
	mac_dev->set_promisc		= dtsec_set_promiscuous;
	mac_dev->change_addr		= dtsec_modify_mac_address;
	mac_dev->add_hash_mac_addr	= dtsec_add_hash_mac_address;
	mac_dev->remove_hash_mac_addr	= dtsec_del_hash_mac_address;
	mac_dev->set_exception		= dtsec_set_exception;
	mac_dev->set_allmulti		= dtsec_set_allmulti;
	mac_dev->set_tstamp		= dtsec_set_tstamp;
	mac_dev->set_multi		= fman_set_multi;
	mac_dev->enable			= dtsec_enable;
	mac_dev->disable		= dtsec_disable;

	mac_dev->fman_mac = dtsec_config(mac_dev, params);
	if (!mac_dev->fman_mac) {
		err = -EINVAL;
		goto _return;
	}

	dtsec = mac_dev->fman_mac;
	dtsec->dtsec_drv_param->maximum_frame = fman_get_max_frm();
	dtsec->dtsec_drv_param->tx_pad_crc = true;

	phy_node = of_parse_phandle(mac_node, "tbi-handle", 0);
	if (!phy_node || !of_device_is_available(phy_node)) {
		of_node_put(phy_node);
		err = -EINVAL;
		dev_err_probe(mac_dev->dev, err,
			      "TBI PCS node is not available\n");
		goto _return_fm_mac_free;
	}

	dtsec->tbidev = of_mdio_find_device(phy_node);
	of_node_put(phy_node);
	if (!dtsec->tbidev) {
		err = -EPROBE_DEFER;
		dev_err_probe(mac_dev->dev, err,
			      "could not find mdiodev for PCS\n");
		goto _return_fm_mac_free;
	}
	dtsec->pcs.ops = &dtsec_pcs_ops;
	dtsec->pcs.neg_mode = true;
	dtsec->pcs.poll = true;

	supported = mac_dev->phylink_config.supported_interfaces;

	/* FIXME: Can we use DTSEC_ID2_INT_FULL_OFF to determine if these are
	 * supported? If not, we can determine support via the phy if SerDes
	 * support is added.
	 */
	if (mac_dev->phy_if == PHY_INTERFACE_MODE_SGMII ||
	    mac_dev->phy_if == PHY_INTERFACE_MODE_1000BASEX) {
		__set_bit(PHY_INTERFACE_MODE_SGMII, supported);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, supported);
	} else if (mac_dev->phy_if == PHY_INTERFACE_MODE_2500BASEX) {
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, supported);
	}

	if (!(ioread32be(&dtsec->regs->tsec_id2) & DTSEC_ID2_INT_REDUCED_OFF)) {
		phy_interface_set_rgmii(supported);

		/* DTSEC_ID2_INT_REDUCED_OFF indicates that the dTSEC supports
		 * RMII and RGMII. However, the only SoCs which support RMII
		 * are the P1017 and P1023. Avoid advertising this mode on
		 * other SoCs. This is a bit of a moot point, since there's no
		 * in-tree support for ethernet on these platforms...
		 */
		if (of_machine_is_compatible("fsl,P1023") ||
		    of_machine_is_compatible("fsl,P1023RDB"))
			__set_bit(PHY_INTERFACE_MODE_RMII, supported);
	}

	capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
	capabilities |= MAC_10 | MAC_100 | MAC_1000FD | MAC_2500FD;
	mac_dev->phylink_config.mac_capabilities = capabilities;

	err = dtsec_init(dtsec);
	if (err < 0)
		goto _return_fm_mac_free;

	/* For 1G MAC, disable by default the MIB counters overflow interrupt */
	err = dtsec_set_exception(dtsec, FM_MAC_EX_1G_RX_MIB_CNT_OVFL, false);
	if (err < 0)
		goto _return_fm_mac_free;

	dev_info(mac_dev->dev, "FMan dTSEC version: 0x%08x\n",
		 ioread32be(&dtsec->regs->tsec_id));

	goto _return;

_return_fm_mac_free:
	dtsec_free(dtsec);

_return:
	return err;
}
