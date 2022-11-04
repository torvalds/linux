// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-or-later
/*
 * Copyright 2008 - 2015 Freescale Semiconductor Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "fman_tgec.h"
#include "fman.h"
#include "mac.h"

#include <linux/slab.h>
#include <linux/bitrev.h>
#include <linux/io.h>
#include <linux/crc32.h>

/* Transmit Inter-Packet Gap Length Register (TX_IPG_LENGTH) */
#define TGEC_TX_IPG_LENGTH_MASK	0x000003ff

/* Command and Configuration Register (COMMAND_CONFIG) */
#define CMD_CFG_EN_TIMESTAMP		0x00100000
#define CMD_CFG_NO_LEN_CHK		0x00020000
#define CMD_CFG_PAUSE_IGNORE		0x00000100
#define CMF_CFG_CRC_FWD			0x00000040
#define CMD_CFG_PROMIS_EN		0x00000010
#define CMD_CFG_RX_EN			0x00000002
#define CMD_CFG_TX_EN			0x00000001

/* Interrupt Mask Register (IMASK) */
#define TGEC_IMASK_MDIO_SCAN_EVENT	0x00010000
#define TGEC_IMASK_MDIO_CMD_CMPL	0x00008000
#define TGEC_IMASK_REM_FAULT		0x00004000
#define TGEC_IMASK_LOC_FAULT		0x00002000
#define TGEC_IMASK_TX_ECC_ER		0x00001000
#define TGEC_IMASK_TX_FIFO_UNFL	0x00000800
#define TGEC_IMASK_TX_FIFO_OVFL	0x00000400
#define TGEC_IMASK_TX_ER		0x00000200
#define TGEC_IMASK_RX_FIFO_OVFL	0x00000100
#define TGEC_IMASK_RX_ECC_ER		0x00000080
#define TGEC_IMASK_RX_JAB_FRM		0x00000040
#define TGEC_IMASK_RX_OVRSZ_FRM	0x00000020
#define TGEC_IMASK_RX_RUNT_FRM		0x00000010
#define TGEC_IMASK_RX_FRAG_FRM		0x00000008
#define TGEC_IMASK_RX_LEN_ER		0x00000004
#define TGEC_IMASK_RX_CRC_ER		0x00000002
#define TGEC_IMASK_RX_ALIGN_ER		0x00000001

/* Hashtable Control Register (HASHTABLE_CTRL) */
#define TGEC_HASH_MCAST_SHIFT		23
#define TGEC_HASH_MCAST_EN		0x00000200
#define TGEC_HASH_ADR_MSK		0x000001ff

#define DEFAULT_TX_IPG_LENGTH			12
#define DEFAULT_MAX_FRAME_LENGTH		0x600
#define DEFAULT_PAUSE_QUANT			0xf000

/* number of pattern match registers (entries) */
#define TGEC_NUM_OF_PADDRS          1

/* Group address bit indication */
#define GROUP_ADDRESS               0x0000010000000000LL

/* Hash table size (= 32 bits*8 regs) */
#define TGEC_HASH_TABLE_SIZE             512

/* tGEC memory map */
struct tgec_regs {
	u32 tgec_id;		/* 0x000 Controller ID */
	u32 reserved001[1];	/* 0x004 */
	u32 command_config;	/* 0x008 Control and configuration */
	u32 mac_addr_0;		/* 0x00c Lower 32 bits of the MAC adr */
	u32 mac_addr_1;		/* 0x010 Upper 16 bits of the MAC adr */
	u32 maxfrm;		/* 0x014 Maximum frame length */
	u32 pause_quant;	/* 0x018 Pause quanta */
	u32 rx_fifo_sections;	/* 0x01c  */
	u32 tx_fifo_sections;	/* 0x020  */
	u32 rx_fifo_almost_f_e;	/* 0x024  */
	u32 tx_fifo_almost_f_e;	/* 0x028  */
	u32 hashtable_ctrl;	/* 0x02c Hash table control */
	u32 mdio_cfg_status;	/* 0x030  */
	u32 mdio_command;	/* 0x034  */
	u32 mdio_data;		/* 0x038  */
	u32 mdio_regaddr;	/* 0x03c  */
	u32 status;		/* 0x040  */
	u32 tx_ipg_len;		/* 0x044 Transmitter inter-packet-gap */
	u32 mac_addr_2;		/* 0x048 Lower 32 bits of 2nd MAC adr */
	u32 mac_addr_3;		/* 0x04c Upper 16 bits of 2nd MAC adr */
	u32 rx_fifo_ptr_rd;	/* 0x050  */
	u32 rx_fifo_ptr_wr;	/* 0x054  */
	u32 tx_fifo_ptr_rd;	/* 0x058  */
	u32 tx_fifo_ptr_wr;	/* 0x05c  */
	u32 imask;		/* 0x060 Interrupt mask */
	u32 ievent;		/* 0x064 Interrupt event */
	u32 udp_port;		/* 0x068 Defines a UDP Port number */
	u32 type_1588v2;	/* 0x06c Type field for 1588v2 */
	u32 reserved070[4];	/* 0x070 */
	/* 10Ge Statistics Counter */
	u32 tfrm_u;		/* 80 aFramesTransmittedOK */
	u32 tfrm_l;		/* 84 aFramesTransmittedOK */
	u32 rfrm_u;		/* 88 aFramesReceivedOK */
	u32 rfrm_l;		/* 8c aFramesReceivedOK */
	u32 rfcs_u;		/* 90 aFrameCheckSequenceErrors */
	u32 rfcs_l;		/* 94 aFrameCheckSequenceErrors */
	u32 raln_u;		/* 98 aAlignmentErrors */
	u32 raln_l;		/* 9c aAlignmentErrors */
	u32 txpf_u;		/* A0 aPAUSEMACCtrlFramesTransmitted */
	u32 txpf_l;		/* A4 aPAUSEMACCtrlFramesTransmitted */
	u32 rxpf_u;		/* A8 aPAUSEMACCtrlFramesReceived */
	u32 rxpf_l;		/* Ac aPAUSEMACCtrlFramesReceived */
	u32 rlong_u;		/* B0 aFrameTooLongErrors */
	u32 rlong_l;		/* B4 aFrameTooLongErrors */
	u32 rflr_u;		/* B8 aInRangeLengthErrors */
	u32 rflr_l;		/* Bc aInRangeLengthErrors */
	u32 tvlan_u;		/* C0 VLANTransmittedOK */
	u32 tvlan_l;		/* C4 VLANTransmittedOK */
	u32 rvlan_u;		/* C8 VLANReceivedOK */
	u32 rvlan_l;		/* Cc VLANReceivedOK */
	u32 toct_u;		/* D0 if_out_octets */
	u32 toct_l;		/* D4 if_out_octets */
	u32 roct_u;		/* D8 if_in_octets */
	u32 roct_l;		/* Dc if_in_octets */
	u32 ruca_u;		/* E0 if_in_ucast_pkts */
	u32 ruca_l;		/* E4 if_in_ucast_pkts */
	u32 rmca_u;		/* E8 ifInMulticastPkts */
	u32 rmca_l;		/* Ec ifInMulticastPkts */
	u32 rbca_u;		/* F0 ifInBroadcastPkts */
	u32 rbca_l;		/* F4 ifInBroadcastPkts */
	u32 terr_u;		/* F8 if_out_errors */
	u32 terr_l;		/* Fc if_out_errors */
	u32 reserved100[2];	/* 100-108 */
	u32 tuca_u;		/* 108 if_out_ucast_pkts */
	u32 tuca_l;		/* 10c if_out_ucast_pkts */
	u32 tmca_u;		/* 110 ifOutMulticastPkts */
	u32 tmca_l;		/* 114 ifOutMulticastPkts */
	u32 tbca_u;		/* 118 ifOutBroadcastPkts */
	u32 tbca_l;		/* 11c ifOutBroadcastPkts */
	u32 rdrp_u;		/* 120 etherStatsDropEvents */
	u32 rdrp_l;		/* 124 etherStatsDropEvents */
	u32 reoct_u;		/* 128 etherStatsOctets */
	u32 reoct_l;		/* 12c etherStatsOctets */
	u32 rpkt_u;		/* 130 etherStatsPkts */
	u32 rpkt_l;		/* 134 etherStatsPkts */
	u32 trund_u;		/* 138 etherStatsUndersizePkts */
	u32 trund_l;		/* 13c etherStatsUndersizePkts */
	u32 r64_u;		/* 140 etherStatsPkts64Octets */
	u32 r64_l;		/* 144 etherStatsPkts64Octets */
	u32 r127_u;		/* 148 etherStatsPkts65to127Octets */
	u32 r127_l;		/* 14c etherStatsPkts65to127Octets */
	u32 r255_u;		/* 150 etherStatsPkts128to255Octets */
	u32 r255_l;		/* 154 etherStatsPkts128to255Octets */
	u32 r511_u;		/* 158 etherStatsPkts256to511Octets */
	u32 r511_l;		/* 15c etherStatsPkts256to511Octets */
	u32 r1023_u;		/* 160 etherStatsPkts512to1023Octets */
	u32 r1023_l;		/* 164 etherStatsPkts512to1023Octets */
	u32 r1518_u;		/* 168 etherStatsPkts1024to1518Octets */
	u32 r1518_l;		/* 16c etherStatsPkts1024to1518Octets */
	u32 r1519x_u;		/* 170 etherStatsPkts1519toX */
	u32 r1519x_l;		/* 174 etherStatsPkts1519toX */
	u32 trovr_u;		/* 178 etherStatsOversizePkts */
	u32 trovr_l;		/* 17c etherStatsOversizePkts */
	u32 trjbr_u;		/* 180 etherStatsJabbers */
	u32 trjbr_l;		/* 184 etherStatsJabbers */
	u32 trfrg_u;		/* 188 etherStatsFragments */
	u32 trfrg_l;		/* 18C etherStatsFragments */
	u32 rerr_u;		/* 190 if_in_errors */
	u32 rerr_l;		/* 194 if_in_errors */
};

struct tgec_cfg {
	bool pause_ignore;
	bool promiscuous_mode_enable;
	u16 max_frame_length;
	u16 pause_quant;
	u32 tx_ipg_length;
};

struct fman_mac {
	/* Pointer to the memory mapped registers. */
	struct tgec_regs __iomem *regs;
	/* MAC address of device; */
	u64 addr;
	u16 max_speed;
	struct mac_device *dev_id; /* device cookie used by the exception cbs */
	fman_mac_exception_cb *exception_cb;
	fman_mac_exception_cb *event_cb;
	/* pointer to driver's global address hash table  */
	struct eth_hash_t *multicast_addr_hash;
	/* pointer to driver's individual address hash table  */
	struct eth_hash_t *unicast_addr_hash;
	u8 mac_id;
	u32 exceptions;
	struct tgec_cfg *cfg;
	void *fm;
	struct fman_rev_info fm_rev_info;
	bool allmulti_enabled;
};

static void set_mac_address(struct tgec_regs __iomem *regs, const u8 *adr)
{
	u32 tmp0, tmp1;

	tmp0 = (u32)(adr[0] | adr[1] << 8 | adr[2] << 16 | adr[3] << 24);
	tmp1 = (u32)(adr[4] | adr[5] << 8);
	iowrite32be(tmp0, &regs->mac_addr_0);
	iowrite32be(tmp1, &regs->mac_addr_1);
}

static void set_dflts(struct tgec_cfg *cfg)
{
	cfg->promiscuous_mode_enable = false;
	cfg->pause_ignore = false;
	cfg->tx_ipg_length = DEFAULT_TX_IPG_LENGTH;
	cfg->max_frame_length = DEFAULT_MAX_FRAME_LENGTH;
	cfg->pause_quant = DEFAULT_PAUSE_QUANT;
}

static int init(struct tgec_regs __iomem *regs, struct tgec_cfg *cfg,
		u32 exception_mask)
{
	u32 tmp;

	/* Config */
	tmp = CMF_CFG_CRC_FWD;
	if (cfg->promiscuous_mode_enable)
		tmp |= CMD_CFG_PROMIS_EN;
	if (cfg->pause_ignore)
		tmp |= CMD_CFG_PAUSE_IGNORE;
	/* Payload length check disable */
	tmp |= CMD_CFG_NO_LEN_CHK;
	iowrite32be(tmp, &regs->command_config);

	/* Max Frame Length */
	iowrite32be((u32)cfg->max_frame_length, &regs->maxfrm);
	/* Pause Time */
	iowrite32be(cfg->pause_quant, &regs->pause_quant);

	/* clear all pending events and set-up interrupts */
	iowrite32be(0xffffffff, &regs->ievent);
	iowrite32be(ioread32be(&regs->imask) | exception_mask, &regs->imask);

	return 0;
}

static int check_init_parameters(struct fman_mac *tgec)
{
	if (tgec->max_speed < SPEED_10000) {
		pr_err("10G MAC driver only support 10G speed\n");
		return -EINVAL;
	}
	if (!tgec->exception_cb) {
		pr_err("uninitialized exception_cb\n");
		return -EINVAL;
	}
	if (!tgec->event_cb) {
		pr_err("uninitialized event_cb\n");
		return -EINVAL;
	}

	return 0;
}

static int get_exception_flag(enum fman_mac_exceptions exception)
{
	u32 bit_mask;

	switch (exception) {
	case FM_MAC_EX_10G_MDIO_SCAN_EVENT:
		bit_mask = TGEC_IMASK_MDIO_SCAN_EVENT;
		break;
	case FM_MAC_EX_10G_MDIO_CMD_CMPL:
		bit_mask = TGEC_IMASK_MDIO_CMD_CMPL;
		break;
	case FM_MAC_EX_10G_REM_FAULT:
		bit_mask = TGEC_IMASK_REM_FAULT;
		break;
	case FM_MAC_EX_10G_LOC_FAULT:
		bit_mask = TGEC_IMASK_LOC_FAULT;
		break;
	case FM_MAC_EX_10G_TX_ECC_ER:
		bit_mask = TGEC_IMASK_TX_ECC_ER;
		break;
	case FM_MAC_EX_10G_TX_FIFO_UNFL:
		bit_mask = TGEC_IMASK_TX_FIFO_UNFL;
		break;
	case FM_MAC_EX_10G_TX_FIFO_OVFL:
		bit_mask = TGEC_IMASK_TX_FIFO_OVFL;
		break;
	case FM_MAC_EX_10G_TX_ER:
		bit_mask = TGEC_IMASK_TX_ER;
		break;
	case FM_MAC_EX_10G_RX_FIFO_OVFL:
		bit_mask = TGEC_IMASK_RX_FIFO_OVFL;
		break;
	case FM_MAC_EX_10G_RX_ECC_ER:
		bit_mask = TGEC_IMASK_RX_ECC_ER;
		break;
	case FM_MAC_EX_10G_RX_JAB_FRM:
		bit_mask = TGEC_IMASK_RX_JAB_FRM;
		break;
	case FM_MAC_EX_10G_RX_OVRSZ_FRM:
		bit_mask = TGEC_IMASK_RX_OVRSZ_FRM;
		break;
	case FM_MAC_EX_10G_RX_RUNT_FRM:
		bit_mask = TGEC_IMASK_RX_RUNT_FRM;
		break;
	case FM_MAC_EX_10G_RX_FRAG_FRM:
		bit_mask = TGEC_IMASK_RX_FRAG_FRM;
		break;
	case FM_MAC_EX_10G_RX_LEN_ER:
		bit_mask = TGEC_IMASK_RX_LEN_ER;
		break;
	case FM_MAC_EX_10G_RX_CRC_ER:
		bit_mask = TGEC_IMASK_RX_CRC_ER;
		break;
	case FM_MAC_EX_10G_RX_ALIGN_ER:
		bit_mask = TGEC_IMASK_RX_ALIGN_ER;
		break;
	default:
		bit_mask = 0;
		break;
	}

	return bit_mask;
}

static void tgec_err_exception(void *handle)
{
	struct fman_mac *tgec = (struct fman_mac *)handle;
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 event;

	/* do not handle MDIO events */
	event = ioread32be(&regs->ievent) &
			   ~(TGEC_IMASK_MDIO_SCAN_EVENT |
			   TGEC_IMASK_MDIO_CMD_CMPL);

	event &= ioread32be(&regs->imask);

	iowrite32be(event, &regs->ievent);

	if (event & TGEC_IMASK_REM_FAULT)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_REM_FAULT);
	if (event & TGEC_IMASK_LOC_FAULT)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_LOC_FAULT);
	if (event & TGEC_IMASK_TX_ECC_ER)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_TX_ECC_ER);
	if (event & TGEC_IMASK_TX_FIFO_UNFL)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_TX_FIFO_UNFL);
	if (event & TGEC_IMASK_TX_FIFO_OVFL)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_TX_FIFO_OVFL);
	if (event & TGEC_IMASK_TX_ER)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_TX_ER);
	if (event & TGEC_IMASK_RX_FIFO_OVFL)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_FIFO_OVFL);
	if (event & TGEC_IMASK_RX_ECC_ER)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_ECC_ER);
	if (event & TGEC_IMASK_RX_JAB_FRM)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_JAB_FRM);
	if (event & TGEC_IMASK_RX_OVRSZ_FRM)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_OVRSZ_FRM);
	if (event & TGEC_IMASK_RX_RUNT_FRM)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_RUNT_FRM);
	if (event & TGEC_IMASK_RX_FRAG_FRM)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_FRAG_FRM);
	if (event & TGEC_IMASK_RX_LEN_ER)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_LEN_ER);
	if (event & TGEC_IMASK_RX_CRC_ER)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_CRC_ER);
	if (event & TGEC_IMASK_RX_ALIGN_ER)
		tgec->exception_cb(tgec->dev_id, FM_MAC_EX_10G_RX_ALIGN_ER);
}

static void free_init_resources(struct fman_mac *tgec)
{
	fman_unregister_intr(tgec->fm, FMAN_MOD_MAC, tgec->mac_id,
			     FMAN_INTR_TYPE_ERR);

	/* release the driver's group hash table */
	free_hash_table(tgec->multicast_addr_hash);
	tgec->multicast_addr_hash = NULL;

	/* release the driver's individual hash table */
	free_hash_table(tgec->unicast_addr_hash);
	tgec->unicast_addr_hash = NULL;
}

static bool is_init_done(struct tgec_cfg *cfg)
{
	/* Checks if tGEC driver parameters were initialized */
	if (!cfg)
		return true;

	return false;
}

static int tgec_enable(struct fman_mac *tgec)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 tmp;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	tmp |= CMD_CFG_RX_EN | CMD_CFG_TX_EN;
	iowrite32be(tmp, &regs->command_config);

	return 0;
}

static void tgec_disable(struct fman_mac *tgec)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 tmp;

	WARN_ON_ONCE(!is_init_done(tgec->cfg));

	tmp = ioread32be(&regs->command_config);
	tmp &= ~(CMD_CFG_RX_EN | CMD_CFG_TX_EN);
	iowrite32be(tmp, &regs->command_config);
}

static int tgec_set_promiscuous(struct fman_mac *tgec, bool new_val)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 tmp;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	if (new_val)
		tmp |= CMD_CFG_PROMIS_EN;
	else
		tmp &= ~CMD_CFG_PROMIS_EN;
	iowrite32be(tmp, &regs->command_config);

	return 0;
}

static int tgec_set_tx_pause_frames(struct fman_mac *tgec,
				    u8 __maybe_unused priority, u16 pause_time,
				    u16 __maybe_unused thresh_time)
{
	struct tgec_regs __iomem *regs = tgec->regs;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	iowrite32be((u32)pause_time, &regs->pause_quant);

	return 0;
}

static int tgec_accept_rx_pause_frames(struct fman_mac *tgec, bool en)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 tmp;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	if (!en)
		tmp |= CMD_CFG_PAUSE_IGNORE;
	else
		tmp &= ~CMD_CFG_PAUSE_IGNORE;
	iowrite32be(tmp, &regs->command_config);

	return 0;
}

static int tgec_modify_mac_address(struct fman_mac *tgec,
				   const enet_addr_t *p_enet_addr)
{
	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	tgec->addr = ENET_ADDR_TO_UINT64(*p_enet_addr);
	set_mac_address(tgec->regs, (const u8 *)(*p_enet_addr));

	return 0;
}

static int tgec_add_hash_mac_address(struct fman_mac *tgec,
				     enet_addr_t *eth_addr)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	struct eth_hash_entry *hash_entry;
	u32 crc = 0xFFFFFFFF, hash;
	u64 addr;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	addr = ENET_ADDR_TO_UINT64(*eth_addr);

	if (!(addr & GROUP_ADDRESS)) {
		/* Unicast addresses not supported in hash */
		pr_err("Unicast Address\n");
		return -EINVAL;
	}
	/* CRC calculation */
	crc = crc32_le(crc, (u8 *)eth_addr, ETH_ALEN);
	crc = bitrev32(crc);
	/* Take 9 MSB bits */
	hash = (crc >> TGEC_HASH_MCAST_SHIFT) & TGEC_HASH_ADR_MSK;

	/* Create element to be added to the driver hash table */
	hash_entry = kmalloc(sizeof(*hash_entry), GFP_ATOMIC);
	if (!hash_entry)
		return -ENOMEM;
	hash_entry->addr = addr;
	INIT_LIST_HEAD(&hash_entry->node);

	list_add_tail(&hash_entry->node,
		      &tgec->multicast_addr_hash->lsts[hash]);
	iowrite32be((hash | TGEC_HASH_MCAST_EN), &regs->hashtable_ctrl);

	return 0;
}

static int tgec_set_allmulti(struct fman_mac *tgec, bool enable)
{
	u32 entry;
	struct tgec_regs __iomem *regs = tgec->regs;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	if (enable) {
		for (entry = 0; entry < TGEC_HASH_TABLE_SIZE; entry++)
			iowrite32be(entry | TGEC_HASH_MCAST_EN,
				    &regs->hashtable_ctrl);
	} else {
		for (entry = 0; entry < TGEC_HASH_TABLE_SIZE; entry++)
			iowrite32be(entry & ~TGEC_HASH_MCAST_EN,
				    &regs->hashtable_ctrl);
	}

	tgec->allmulti_enabled = enable;

	return 0;
}

static int tgec_set_tstamp(struct fman_mac *tgec, bool enable)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 tmp;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);

	if (enable)
		tmp |= CMD_CFG_EN_TIMESTAMP;
	else
		tmp &= ~CMD_CFG_EN_TIMESTAMP;

	iowrite32be(tmp, &regs->command_config);

	return 0;
}

static int tgec_del_hash_mac_address(struct fman_mac *tgec,
				     enet_addr_t *eth_addr)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	struct eth_hash_entry *hash_entry = NULL;
	struct list_head *pos;
	u32 crc = 0xFFFFFFFF, hash;
	u64 addr;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	addr = ((*(u64 *)eth_addr) >> 16);

	/* CRC calculation */
	crc = crc32_le(crc, (u8 *)eth_addr, ETH_ALEN);
	crc = bitrev32(crc);
	/* Take 9 MSB bits */
	hash = (crc >> TGEC_HASH_MCAST_SHIFT) & TGEC_HASH_ADR_MSK;

	list_for_each(pos, &tgec->multicast_addr_hash->lsts[hash]) {
		hash_entry = ETH_HASH_ENTRY_OBJ(pos);
		if (hash_entry && hash_entry->addr == addr) {
			list_del_init(&hash_entry->node);
			kfree(hash_entry);
			break;
		}
	}

	if (!tgec->allmulti_enabled) {
		if (list_empty(&tgec->multicast_addr_hash->lsts[hash]))
			iowrite32be((hash & ~TGEC_HASH_MCAST_EN),
				    &regs->hashtable_ctrl);
	}

	return 0;
}

static void tgec_adjust_link(struct mac_device *mac_dev)
{
	struct phy_device *phy_dev = mac_dev->phy_dev;

	mac_dev->update_speed(mac_dev, phy_dev->speed);
}

static int tgec_set_exception(struct fman_mac *tgec,
			      enum fman_mac_exceptions exception, bool enable)
{
	struct tgec_regs __iomem *regs = tgec->regs;
	u32 bit_mask = 0;

	if (!is_init_done(tgec->cfg))
		return -EINVAL;

	bit_mask = get_exception_flag(exception);
	if (bit_mask) {
		if (enable)
			tgec->exceptions |= bit_mask;
		else
			tgec->exceptions &= ~bit_mask;
	} else {
		pr_err("Undefined exception\n");
		return -EINVAL;
	}
	if (enable)
		iowrite32be(ioread32be(&regs->imask) | bit_mask, &regs->imask);
	else
		iowrite32be(ioread32be(&regs->imask) & ~bit_mask, &regs->imask);

	return 0;
}

static int tgec_init(struct fman_mac *tgec)
{
	struct tgec_cfg *cfg;
	enet_addr_t eth_addr;
	int err;

	if (is_init_done(tgec->cfg))
		return -EINVAL;

	if (DEFAULT_RESET_ON_INIT &&
	    (fman_reset_mac(tgec->fm, tgec->mac_id) != 0)) {
		pr_err("Can't reset MAC!\n");
		return -EINVAL;
	}

	err = check_init_parameters(tgec);
	if (err)
		return err;

	cfg = tgec->cfg;

	if (tgec->addr) {
		MAKE_ENET_ADDR_FROM_UINT64(tgec->addr, eth_addr);
		set_mac_address(tgec->regs, (const u8 *)eth_addr);
	}

	/* interrupts */
	/* FM_10G_REM_N_LCL_FLT_EX_10GMAC_ERRATA_SW005 Errata workaround */
	if (tgec->fm_rev_info.major <= 2)
		tgec->exceptions &= ~(TGEC_IMASK_REM_FAULT |
				      TGEC_IMASK_LOC_FAULT);

	err = init(tgec->regs, cfg, tgec->exceptions);
	if (err) {
		free_init_resources(tgec);
		pr_err("TGEC version doesn't support this i/f mode\n");
		return err;
	}

	/* Max Frame Length */
	err = fman_set_mac_max_frame(tgec->fm, tgec->mac_id,
				     cfg->max_frame_length);
	if (err) {
		pr_err("Setting max frame length FAILED\n");
		free_init_resources(tgec);
		return -EINVAL;
	}

	/* FM_TX_FIFO_CORRUPTION_ERRATA_10GMAC_A007 Errata workaround */
	if (tgec->fm_rev_info.major == 2) {
		struct tgec_regs __iomem *regs = tgec->regs;
		u32 tmp;

		/* restore the default tx ipg Length */
		tmp = (ioread32be(&regs->tx_ipg_len) &
		       ~TGEC_TX_IPG_LENGTH_MASK) | 12;

		iowrite32be(tmp, &regs->tx_ipg_len);
	}

	tgec->multicast_addr_hash = alloc_hash_table(TGEC_HASH_TABLE_SIZE);
	if (!tgec->multicast_addr_hash) {
		free_init_resources(tgec);
		pr_err("allocation hash table is FAILED\n");
		return -ENOMEM;
	}

	tgec->unicast_addr_hash = alloc_hash_table(TGEC_HASH_TABLE_SIZE);
	if (!tgec->unicast_addr_hash) {
		free_init_resources(tgec);
		pr_err("allocation hash table is FAILED\n");
		return -ENOMEM;
	}

	fman_register_intr(tgec->fm, FMAN_MOD_MAC, tgec->mac_id,
			   FMAN_INTR_TYPE_ERR, tgec_err_exception, tgec);

	kfree(cfg);
	tgec->cfg = NULL;

	return 0;
}

static int tgec_free(struct fman_mac *tgec)
{
	free_init_resources(tgec);

	kfree(tgec->cfg);
	kfree(tgec);

	return 0;
}

static struct fman_mac *tgec_config(struct mac_device *mac_dev,
				    struct fman_mac_params *params)
{
	struct fman_mac *tgec;
	struct tgec_cfg *cfg;

	/* allocate memory for the UCC GETH data structure. */
	tgec = kzalloc(sizeof(*tgec), GFP_KERNEL);
	if (!tgec)
		return NULL;

	/* allocate memory for the 10G MAC driver parameters data structure. */
	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg) {
		tgec_free(tgec);
		return NULL;
	}

	/* Plant parameter structure pointer */
	tgec->cfg = cfg;

	set_dflts(cfg);

	tgec->regs = mac_dev->vaddr;
	tgec->addr = ENET_ADDR_TO_UINT64(mac_dev->addr);
	tgec->max_speed = params->max_speed;
	tgec->mac_id = params->mac_id;
	tgec->exceptions = (TGEC_IMASK_MDIO_SCAN_EVENT	|
			    TGEC_IMASK_REM_FAULT	|
			    TGEC_IMASK_LOC_FAULT	|
			    TGEC_IMASK_TX_ECC_ER	|
			    TGEC_IMASK_TX_FIFO_UNFL	|
			    TGEC_IMASK_TX_FIFO_OVFL	|
			    TGEC_IMASK_TX_ER		|
			    TGEC_IMASK_RX_FIFO_OVFL	|
			    TGEC_IMASK_RX_ECC_ER	|
			    TGEC_IMASK_RX_JAB_FRM	|
			    TGEC_IMASK_RX_OVRSZ_FRM	|
			    TGEC_IMASK_RX_RUNT_FRM	|
			    TGEC_IMASK_RX_FRAG_FRM	|
			    TGEC_IMASK_RX_CRC_ER	|
			    TGEC_IMASK_RX_ALIGN_ER);
	tgec->exception_cb = params->exception_cb;
	tgec->event_cb = params->event_cb;
	tgec->dev_id = mac_dev;
	tgec->fm = params->fm;

	/* Save FMan revision */
	fman_get_revision(tgec->fm, &tgec->fm_rev_info);

	return tgec;
}

int tgec_initialization(struct mac_device *mac_dev,
			struct device_node *mac_node,
			struct fman_mac_params *params)
{
	int err;
	struct fman_mac		*tgec;

	mac_dev->set_promisc		= tgec_set_promiscuous;
	mac_dev->change_addr		= tgec_modify_mac_address;
	mac_dev->add_hash_mac_addr	= tgec_add_hash_mac_address;
	mac_dev->remove_hash_mac_addr	= tgec_del_hash_mac_address;
	mac_dev->set_tx_pause		= tgec_set_tx_pause_frames;
	mac_dev->set_rx_pause		= tgec_accept_rx_pause_frames;
	mac_dev->set_exception		= tgec_set_exception;
	mac_dev->set_allmulti		= tgec_set_allmulti;
	mac_dev->set_tstamp		= tgec_set_tstamp;
	mac_dev->set_multi		= fman_set_multi;
	mac_dev->adjust_link            = tgec_adjust_link;
	mac_dev->enable			= tgec_enable;
	mac_dev->disable		= tgec_disable;

	mac_dev->fman_mac = tgec_config(mac_dev, params);
	if (!mac_dev->fman_mac) {
		err = -EINVAL;
		goto _return;
	}

	tgec = mac_dev->fman_mac;
	tgec->cfg->max_frame_length = fman_get_max_frm();
	err = tgec_init(tgec);
	if (err < 0)
		goto _return_fm_mac_free;

	/* For 10G MAC, disable Tx ECC exception */
	err = tgec_set_exception(tgec, FM_MAC_EX_10G_TX_ECC_ER, false);
	if (err < 0)
		goto _return_fm_mac_free;

	pr_info("FMan XGEC version: 0x%08x\n",
		ioread32be(&tgec->regs->tgec_id));
	goto _return;

_return_fm_mac_free:
	tgec_free(mac_dev->fman_mac);

_return:
	return err;
}
