/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "fman_memac.h"
#include "fman.h"

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/of_mdio.h>

/* PCS registers */
#define MDIO_SGMII_CR			0x00
#define MDIO_SGMII_DEV_ABIL_SGMII	0x04
#define MDIO_SGMII_LINK_TMR_L		0x12
#define MDIO_SGMII_LINK_TMR_H		0x13
#define MDIO_SGMII_IF_MODE		0x14

/* SGMII Control defines */
#define SGMII_CR_AN_EN			0x1000
#define SGMII_CR_RESTART_AN		0x0200
#define SGMII_CR_FD			0x0100
#define SGMII_CR_SPEED_SEL1_1G		0x0040
#define SGMII_CR_DEF_VAL		(SGMII_CR_AN_EN | SGMII_CR_FD | \
					 SGMII_CR_SPEED_SEL1_1G)

/* SGMII Device Ability for SGMII defines */
#define MDIO_SGMII_DEV_ABIL_SGMII_MODE	0x4001
#define MDIO_SGMII_DEV_ABIL_BASEX_MODE	0x01A0

/* Link timer define */
#define LINK_TMR_L			0xa120
#define LINK_TMR_H			0x0007
#define LINK_TMR_L_BASEX		0xaf08
#define LINK_TMR_H_BASEX		0x002f

/* SGMII IF Mode defines */
#define IF_MODE_USE_SGMII_AN		0x0002
#define IF_MODE_SGMII_EN		0x0001
#define IF_MODE_SGMII_SPEED_100M	0x0004
#define IF_MODE_SGMII_SPEED_1G		0x0008
#define IF_MODE_SGMII_DUPLEX_HALF	0x0010

/* Num of additional exact match MAC adr regs */
#define MEMAC_NUM_OF_PADDRS 7

/* Control and Configuration Register (COMMAND_CONFIG) */
#define CMD_CFG_REG_LOWP_RXETY	0x01000000 /* 07 Rx low power indication */
#define CMD_CFG_TX_LOWP_ENA	0x00800000 /* 08 Tx Low Power Idle Enable */
#define CMD_CFG_PFC_MODE	0x00080000 /* 12 Enable PFC */
#define CMD_CFG_NO_LEN_CHK	0x00020000 /* 14 Payload length check disable */
#define CMD_CFG_SW_RESET	0x00001000 /* 19 S/W Reset, self clearing bit */
#define CMD_CFG_TX_PAD_EN	0x00000800 /* 20 Enable Tx padding of frames */
#define CMD_CFG_PAUSE_IGNORE	0x00000100 /* 23 Ignore Pause frame quanta */
#define CMD_CFG_CRC_FWD		0x00000040 /* 25 Terminate/frwd CRC of frames */
#define CMD_CFG_PAD_EN		0x00000020 /* 26 Frame padding removal */
#define CMD_CFG_PROMIS_EN	0x00000010 /* 27 Promiscuous operation enable */
#define CMD_CFG_RX_EN		0x00000002 /* 30 MAC receive path enable */
#define CMD_CFG_TX_EN		0x00000001 /* 31 MAC transmit path enable */

/* Transmit FIFO Sections Register (TX_FIFO_SECTIONS) */
#define TX_FIFO_SECTIONS_TX_EMPTY_MASK			0xFFFF0000
#define TX_FIFO_SECTIONS_TX_AVAIL_MASK			0x0000FFFF
#define TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G		0x00400000
#define TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_1G		0x00100000
#define TX_FIFO_SECTIONS_TX_AVAIL_10G			0x00000019
#define TX_FIFO_SECTIONS_TX_AVAIL_1G			0x00000020
#define TX_FIFO_SECTIONS_TX_AVAIL_SLOW_10G		0x00000060

#define GET_TX_EMPTY_DEFAULT_VALUE(_val)				\
do {									\
	_val &= ~TX_FIFO_SECTIONS_TX_EMPTY_MASK;			\
	((_val == TX_FIFO_SECTIONS_TX_AVAIL_10G) ?			\
			(_val |= TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G) :\
			(_val |= TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_1G));\
} while (0)

/* Interface Mode Register (IF_MODE) */

#define IF_MODE_MASK		0x00000003 /* 30-31 Mask on i/f mode bits */
#define IF_MODE_10G		0x00000000 /* 30-31 10G interface */
#define IF_MODE_GMII		0x00000002 /* 30-31 GMII (1G) interface */
#define IF_MODE_RGMII		0x00000004
#define IF_MODE_RGMII_AUTO	0x00008000
#define IF_MODE_RGMII_1000	0x00004000 /* 10 - 1000Mbps RGMII */
#define IF_MODE_RGMII_100	0x00000000 /* 00 - 100Mbps RGMII */
#define IF_MODE_RGMII_10	0x00002000 /* 01 - 10Mbps RGMII */
#define IF_MODE_RGMII_SP_MASK	0x00006000 /* Setsp mask bits */
#define IF_MODE_RGMII_FD	0x00001000 /* Full duplex RGMII */
#define IF_MODE_HD		0x00000040 /* Half duplex operation */

/* Hash table Control Register (HASHTABLE_CTRL) */
#define HASH_CTRL_MCAST_EN	0x00000100
/* 26-31 Hash table address code */
#define HASH_CTRL_ADDR_MASK	0x0000003F
/* MAC mcast indication */
#define GROUP_ADDRESS		0x0000010000000000LL
#define HASH_TABLE_SIZE		64	/* Hash tbl size */

/* Interrupt Mask Register (IMASK) */
#define MEMAC_IMASK_MGI		0x40000000 /* 1 Magic pkt detect indication */
#define MEMAC_IMASK_TSECC_ER	0x20000000 /* 2 Timestamp FIFO ECC error evnt */
#define MEMAC_IMASK_TECC_ER	0x02000000 /* 6 Transmit frame ECC error evnt */
#define MEMAC_IMASK_RECC_ER	0x01000000 /* 7 Receive frame ECC error evnt */

#define MEMAC_ALL_ERRS_IMASK					\
		((u32)(MEMAC_IMASK_TSECC_ER	|	\
		       MEMAC_IMASK_TECC_ER		|	\
		       MEMAC_IMASK_RECC_ER		|	\
		       MEMAC_IMASK_MGI))

#define MEMAC_IEVNT_PCS			0x80000000 /* PCS (XG). Link sync (G) */
#define MEMAC_IEVNT_AN			0x40000000 /* Auto-negotiation */
#define MEMAC_IEVNT_LT			0x20000000 /* Link Training/New page */
#define MEMAC_IEVNT_MGI			0x00004000 /* Magic pkt detection */
#define MEMAC_IEVNT_TS_ECC_ER		0x00002000 /* Timestamp FIFO ECC error*/
#define MEMAC_IEVNT_RX_FIFO_OVFL	0x00001000 /* Rx FIFO overflow */
#define MEMAC_IEVNT_TX_FIFO_UNFL	0x00000800 /* Tx FIFO underflow */
#define MEMAC_IEVNT_TX_FIFO_OVFL	0x00000400 /* Tx FIFO overflow */
#define MEMAC_IEVNT_TX_ECC_ER		0x00000200 /* Tx frame ECC error */
#define MEMAC_IEVNT_RX_ECC_ER		0x00000100 /* Rx frame ECC error */
#define MEMAC_IEVNT_LI_FAULT		0x00000080 /* Link Interruption flt */
#define MEMAC_IEVNT_RX_EMPTY		0x00000040 /* Rx FIFO empty */
#define MEMAC_IEVNT_TX_EMPTY		0x00000020 /* Tx FIFO empty */
#define MEMAC_IEVNT_RX_LOWP		0x00000010 /* Low Power Idle */
#define MEMAC_IEVNT_PHY_LOS		0x00000004 /* Phy loss of signal */
#define MEMAC_IEVNT_REM_FAULT		0x00000002 /* Remote fault (XGMII) */
#define MEMAC_IEVNT_LOC_FAULT		0x00000001 /* Local fault (XGMII) */

#define DEFAULT_PAUSE_QUANTA	0xf000
#define DEFAULT_FRAME_LENGTH	0x600
#define DEFAULT_TX_IPG_LENGTH	12

#define CLXY_PAUSE_QUANTA_CLX_PQNT	0x0000FFFF
#define CLXY_PAUSE_QUANTA_CLY_PQNT	0xFFFF0000
#define CLXY_PAUSE_THRESH_CLX_QTH	0x0000FFFF
#define CLXY_PAUSE_THRESH_CLY_QTH	0xFFFF0000

struct mac_addr {
	/* Lower 32 bits of 48-bit MAC address */
	u32 mac_addr_l;
	/* Upper 16 bits of 48-bit MAC address */
	u32 mac_addr_u;
};

/* memory map */
struct memac_regs {
	u32 res0000[2];			/* General Control and Status */
	u32 command_config;		/* 0x008 Ctrl and cfg */
	struct mac_addr mac_addr0;	/* 0x00C-0x010 MAC_ADDR_0...1 */
	u32 maxfrm;			/* 0x014 Max frame length */
	u32 res0018[1];
	u32 rx_fifo_sections;		/* Receive FIFO configuration reg */
	u32 tx_fifo_sections;		/* Transmit FIFO configuration reg */
	u32 res0024[2];
	u32 hashtable_ctrl;		/* 0x02C Hash table control */
	u32 res0030[4];
	u32 ievent;			/* 0x040 Interrupt event */
	u32 tx_ipg_length;		/* 0x044 Transmitter inter-packet-gap */
	u32 res0048;
	u32 imask;			/* 0x04C Interrupt mask */
	u32 res0050;
	u32 pause_quanta[4];		/* 0x054 Pause quanta */
	u32 pause_thresh[4];		/* 0x064 Pause quanta threshold */
	u32 rx_pause_status;		/* 0x074 Receive pause status */
	u32 res0078[2];
	struct mac_addr mac_addr[MEMAC_NUM_OF_PADDRS];/* 0x80-0x0B4 mac padr */
	u32 lpwake_timer;		/* 0x0B8 Low Power Wakeup Timer */
	u32 sleep_timer;		/* 0x0BC Transmit EEE Low Power Timer */
	u32 res00c0[8];
	u32 statn_config;		/* 0x0E0 Statistics configuration */
	u32 res00e4[7];
	/* Rx Statistics Counter */
	u32 reoct_l;
	u32 reoct_u;
	u32 roct_l;
	u32 roct_u;
	u32 raln_l;
	u32 raln_u;
	u32 rxpf_l;
	u32 rxpf_u;
	u32 rfrm_l;
	u32 rfrm_u;
	u32 rfcs_l;
	u32 rfcs_u;
	u32 rvlan_l;
	u32 rvlan_u;
	u32 rerr_l;
	u32 rerr_u;
	u32 ruca_l;
	u32 ruca_u;
	u32 rmca_l;
	u32 rmca_u;
	u32 rbca_l;
	u32 rbca_u;
	u32 rdrp_l;
	u32 rdrp_u;
	u32 rpkt_l;
	u32 rpkt_u;
	u32 rund_l;
	u32 rund_u;
	u32 r64_l;
	u32 r64_u;
	u32 r127_l;
	u32 r127_u;
	u32 r255_l;
	u32 r255_u;
	u32 r511_l;
	u32 r511_u;
	u32 r1023_l;
	u32 r1023_u;
	u32 r1518_l;
	u32 r1518_u;
	u32 r1519x_l;
	u32 r1519x_u;
	u32 rovr_l;
	u32 rovr_u;
	u32 rjbr_l;
	u32 rjbr_u;
	u32 rfrg_l;
	u32 rfrg_u;
	u32 rcnp_l;
	u32 rcnp_u;
	u32 rdrntp_l;
	u32 rdrntp_u;
	u32 res01d0[12];
	/* Tx Statistics Counter */
	u32 teoct_l;
	u32 teoct_u;
	u32 toct_l;
	u32 toct_u;
	u32 res0210[2];
	u32 txpf_l;
	u32 txpf_u;
	u32 tfrm_l;
	u32 tfrm_u;
	u32 tfcs_l;
	u32 tfcs_u;
	u32 tvlan_l;
	u32 tvlan_u;
	u32 terr_l;
	u32 terr_u;
	u32 tuca_l;
	u32 tuca_u;
	u32 tmca_l;
	u32 tmca_u;
	u32 tbca_l;
	u32 tbca_u;
	u32 res0258[2];
	u32 tpkt_l;
	u32 tpkt_u;
	u32 tund_l;
	u32 tund_u;
	u32 t64_l;
	u32 t64_u;
	u32 t127_l;
	u32 t127_u;
	u32 t255_l;
	u32 t255_u;
	u32 t511_l;
	u32 t511_u;
	u32 t1023_l;
	u32 t1023_u;
	u32 t1518_l;
	u32 t1518_u;
	u32 t1519x_l;
	u32 t1519x_u;
	u32 res02a8[6];
	u32 tcnp_l;
	u32 tcnp_u;
	u32 res02c8[14];
	/* Line Interface Control */
	u32 if_mode;		/* 0x300 Interface Mode Control */
	u32 if_status;		/* 0x304 Interface Status */
	u32 res0308[14];
	/* HiGig/2 */
	u32 hg_config;		/* 0x340 Control and cfg */
	u32 res0344[3];
	u32 hg_pause_quanta;	/* 0x350 Pause quanta */
	u32 res0354[3];
	u32 hg_pause_thresh;	/* 0x360 Pause quanta threshold */
	u32 res0364[3];
	u32 hgrx_pause_status;	/* 0x370 Receive pause status */
	u32 hg_fifos_status;	/* 0x374 fifos status */
	u32 rhm;		/* 0x378 rx messages counter */
	u32 thm;		/* 0x37C tx messages counter */
};

struct memac_cfg {
	bool reset_on_init;
	bool pause_ignore;
	bool promiscuous_mode_enable;
	struct fixed_phy_status *fixed_link;
	u16 max_frame_length;
	u16 pause_quanta;
	u32 tx_ipg_length;
};

struct fman_mac {
	/* Pointer to MAC memory mapped registers */
	struct memac_regs __iomem *regs;
	/* MAC address of device */
	u64 addr;
	/* Ethernet physical interface */
	phy_interface_t phy_if;
	u16 max_speed;
	void *dev_id; /* device cookie used by the exception cbs */
	fman_mac_exception_cb *exception_cb;
	fman_mac_exception_cb *event_cb;
	/* Pointer to driver's global address hash table  */
	struct eth_hash_t *multicast_addr_hash;
	/* Pointer to driver's individual address hash table  */
	struct eth_hash_t *unicast_addr_hash;
	u8 mac_id;
	u32 exceptions;
	struct memac_cfg *memac_drv_param;
	void *fm;
	struct fman_rev_info fm_rev_info;
	bool basex_if;
	struct phy_device *pcsphy;
	bool allmulti_enabled;
};

static void add_addr_in_paddr(struct memac_regs __iomem *regs, u8 *adr,
			      u8 paddr_num)
{
	u32 tmp0, tmp1;

	tmp0 = (u32)(adr[0] | adr[1] << 8 | adr[2] << 16 | adr[3] << 24);
	tmp1 = (u32)(adr[4] | adr[5] << 8);

	if (paddr_num == 0) {
		iowrite32be(tmp0, &regs->mac_addr0.mac_addr_l);
		iowrite32be(tmp1, &regs->mac_addr0.mac_addr_u);
	} else {
		iowrite32be(tmp0, &regs->mac_addr[paddr_num - 1].mac_addr_l);
		iowrite32be(tmp1, &regs->mac_addr[paddr_num - 1].mac_addr_u);
	}
}

static int reset(struct memac_regs __iomem *regs)
{
	u32 tmp;
	int count;

	tmp = ioread32be(&regs->command_config);

	tmp |= CMD_CFG_SW_RESET;

	iowrite32be(tmp, &regs->command_config);

	count = 100;
	do {
		udelay(1);
	} while ((ioread32be(&regs->command_config) & CMD_CFG_SW_RESET) &&
		 --count);

	if (count == 0)
		return -EBUSY;

	return 0;
}

static void set_exception(struct memac_regs __iomem *regs, u32 val,
			  bool enable)
{
	u32 tmp;

	tmp = ioread32be(&regs->imask);
	if (enable)
		tmp |= val;
	else
		tmp &= ~val;

	iowrite32be(tmp, &regs->imask);
}

static int init(struct memac_regs __iomem *regs, struct memac_cfg *cfg,
		phy_interface_t phy_if, u16 speed, bool slow_10g_if,
		u32 exceptions)
{
	u32 tmp;

	/* Config */
	tmp = 0;
	if (cfg->promiscuous_mode_enable)
		tmp |= CMD_CFG_PROMIS_EN;
	if (cfg->pause_ignore)
		tmp |= CMD_CFG_PAUSE_IGNORE;

	/* Payload length check disable */
	tmp |= CMD_CFG_NO_LEN_CHK;
	/* Enable padding of frames in transmit direction */
	tmp |= CMD_CFG_TX_PAD_EN;

	tmp |= CMD_CFG_CRC_FWD;

	iowrite32be(tmp, &regs->command_config);

	/* Max Frame Length */
	iowrite32be((u32)cfg->max_frame_length, &regs->maxfrm);

	/* Pause Time */
	iowrite32be((u32)cfg->pause_quanta, &regs->pause_quanta[0]);
	iowrite32be((u32)0, &regs->pause_thresh[0]);

	/* IF_MODE */
	tmp = 0;
	switch (phy_if) {
	case PHY_INTERFACE_MODE_XGMII:
		tmp |= IF_MODE_10G;
		break;
	default:
		tmp |= IF_MODE_GMII;
		if (phy_if == PHY_INTERFACE_MODE_RGMII ||
		    phy_if == PHY_INTERFACE_MODE_RGMII_ID ||
		    phy_if == PHY_INTERFACE_MODE_RGMII_RXID ||
		    phy_if == PHY_INTERFACE_MODE_RGMII_TXID)
			tmp |= IF_MODE_RGMII | IF_MODE_RGMII_AUTO;
	}
	iowrite32be(tmp, &regs->if_mode);

	/* TX_FIFO_SECTIONS */
	tmp = 0;
	if (phy_if == PHY_INTERFACE_MODE_XGMII) {
		if (slow_10g_if) {
			tmp |= (TX_FIFO_SECTIONS_TX_AVAIL_SLOW_10G |
				TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G);
		} else {
			tmp |= (TX_FIFO_SECTIONS_TX_AVAIL_10G |
				TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_10G);
		}
	} else {
		tmp |= (TX_FIFO_SECTIONS_TX_AVAIL_1G |
			TX_FIFO_SECTIONS_TX_EMPTY_DEFAULT_1G);
	}
	iowrite32be(tmp, &regs->tx_fifo_sections);

	/* clear all pending events and set-up interrupts */
	iowrite32be(0xffffffff, &regs->ievent);
	set_exception(regs, exceptions, true);

	return 0;
}

static void set_dflts(struct memac_cfg *cfg)
{
	cfg->reset_on_init = false;
	cfg->promiscuous_mode_enable = false;
	cfg->pause_ignore = false;
	cfg->tx_ipg_length = DEFAULT_TX_IPG_LENGTH;
	cfg->max_frame_length = DEFAULT_FRAME_LENGTH;
	cfg->pause_quanta = DEFAULT_PAUSE_QUANTA;
}

static u32 get_mac_addr_hash_code(u64 eth_addr)
{
	u64 mask1, mask2;
	u32 xor_val = 0;
	u8 i, j;

	for (i = 0; i < 6; i++) {
		mask1 = eth_addr & (u64)0x01;
		eth_addr >>= 1;

		for (j = 0; j < 7; j++) {
			mask2 = eth_addr & (u64)0x01;
			mask1 ^= mask2;
			eth_addr >>= 1;
		}

		xor_val |= (mask1 << (5 - i));
	}

	return xor_val;
}

static void setup_sgmii_internal_phy(struct fman_mac *memac,
				     struct fixed_phy_status *fixed_link)
{
	u16 tmp_reg16;

	if (WARN_ON(!memac->pcsphy))
		return;

	/* SGMII mode */
	tmp_reg16 = IF_MODE_SGMII_EN;
	if (!fixed_link)
		/* AN enable */
		tmp_reg16 |= IF_MODE_USE_SGMII_AN;
	else {
		switch (fixed_link->speed) {
		case 10:
			/* For 10M: IF_MODE[SPEED_10M] = 0 */
		break;
		case 100:
			tmp_reg16 |= IF_MODE_SGMII_SPEED_100M;
		break;
		case 1000: /* fallthrough */
		default:
			tmp_reg16 |= IF_MODE_SGMII_SPEED_1G;
		break;
		}
		if (!fixed_link->duplex)
			tmp_reg16 |= IF_MODE_SGMII_DUPLEX_HALF;
	}
	phy_write(memac->pcsphy, MDIO_SGMII_IF_MODE, tmp_reg16);

	/* Device ability according to SGMII specification */
	tmp_reg16 = MDIO_SGMII_DEV_ABIL_SGMII_MODE;
	phy_write(memac->pcsphy, MDIO_SGMII_DEV_ABIL_SGMII, tmp_reg16);

	/* Adjust link timer for SGMII  -
	 * According to Cisco SGMII specification the timer should be 1.6 ms.
	 * The link_timer register is configured in units of the clock.
	 * - When running as 1G SGMII, Serdes clock is 125 MHz, so
	 * unit = 1 / (125*10^6 Hz) = 8 ns.
	 * 1.6 ms in units of 8 ns = 1.6ms / 8ns = 2*10^5 = 0x30d40
	 * - When running as 2.5G SGMII, Serdes clock is 312.5 MHz, so
	 * unit = 1 / (312.5*10^6 Hz) = 3.2 ns.
	 * 1.6 ms in units of 3.2 ns = 1.6ms / 3.2ns = 5*10^5 = 0x7a120.
	 * Since link_timer value of 1G SGMII will be too short for 2.5 SGMII,
	 * we always set up here a value of 2.5 SGMII.
	 */
	phy_write(memac->pcsphy, MDIO_SGMII_LINK_TMR_H, LINK_TMR_H);
	phy_write(memac->pcsphy, MDIO_SGMII_LINK_TMR_L, LINK_TMR_L);

	if (!fixed_link)
		/* Restart AN */
		tmp_reg16 = SGMII_CR_DEF_VAL | SGMII_CR_RESTART_AN;
	else
		/* AN disabled */
		tmp_reg16 = SGMII_CR_DEF_VAL & ~SGMII_CR_AN_EN;
	phy_write(memac->pcsphy, 0x0, tmp_reg16);
}

static void setup_sgmii_internal_phy_base_x(struct fman_mac *memac)
{
	u16 tmp_reg16;

	/* AN Device capability  */
	tmp_reg16 = MDIO_SGMII_DEV_ABIL_BASEX_MODE;
	phy_write(memac->pcsphy, MDIO_SGMII_DEV_ABIL_SGMII, tmp_reg16);

	/* Adjust link timer for SGMII  -
	 * For Serdes 1000BaseX auto-negotiation the timer should be 10 ms.
	 * The link_timer register is configured in units of the clock.
	 * - When running as 1G SGMII, Serdes clock is 125 MHz, so
	 * unit = 1 / (125*10^6 Hz) = 8 ns.
	 * 10 ms in units of 8 ns = 10ms / 8ns = 1250000 = 0x1312d0
	 * - When running as 2.5G SGMII, Serdes clock is 312.5 MHz, so
	 * unit = 1 / (312.5*10^6 Hz) = 3.2 ns.
	 * 10 ms in units of 3.2 ns = 10ms / 3.2ns = 3125000 = 0x2faf08.
	 * Since link_timer value of 1G SGMII will be too short for 2.5 SGMII,
	 * we always set up here a value of 2.5 SGMII.
	 */
	phy_write(memac->pcsphy, MDIO_SGMII_LINK_TMR_H, LINK_TMR_H_BASEX);
	phy_write(memac->pcsphy, MDIO_SGMII_LINK_TMR_L, LINK_TMR_L_BASEX);

	/* Restart AN */
	tmp_reg16 = SGMII_CR_DEF_VAL | SGMII_CR_RESTART_AN;
	phy_write(memac->pcsphy, 0x0, tmp_reg16);
}

static int check_init_parameters(struct fman_mac *memac)
{
	if (memac->addr == 0) {
		pr_err("Ethernet MAC must have a valid MAC address\n");
		return -EINVAL;
	}
	if (!memac->exception_cb) {
		pr_err("Uninitialized exception handler\n");
		return -EINVAL;
	}
	if (!memac->event_cb) {
		pr_warn("Uninitialize event handler\n");
		return -EINVAL;
	}

	return 0;
}

static int get_exception_flag(enum fman_mac_exceptions exception)
{
	u32 bit_mask;

	switch (exception) {
	case FM_MAC_EX_10G_TX_ECC_ER:
		bit_mask = MEMAC_IMASK_TECC_ER;
		break;
	case FM_MAC_EX_10G_RX_ECC_ER:
		bit_mask = MEMAC_IMASK_RECC_ER;
		break;
	case FM_MAC_EX_TS_FIFO_ECC_ERR:
		bit_mask = MEMAC_IMASK_TSECC_ER;
		break;
	case FM_MAC_EX_MAGIC_PACKET_INDICATION:
		bit_mask = MEMAC_IMASK_MGI;
		break;
	default:
		bit_mask = 0;
		break;
	}

	return bit_mask;
}

static void memac_err_exception(void *handle)
{
	struct fman_mac *memac = (struct fman_mac *)handle;
	struct memac_regs __iomem *regs = memac->regs;
	u32 event, imask;

	event = ioread32be(&regs->ievent);
	imask = ioread32be(&regs->imask);

	/* Imask include both error and notification/event bits.
	 * Leaving only error bits enabled by imask.
	 * The imask error bits are shifted by 16 bits offset from
	 * their corresponding location in the ievent - hence the >> 16
	 */
	event &= ((imask & MEMAC_ALL_ERRS_IMASK) >> 16);

	iowrite32be(event, &regs->ievent);

	if (event & MEMAC_IEVNT_TS_ECC_ER)
		memac->exception_cb(memac->dev_id, FM_MAC_EX_TS_FIFO_ECC_ERR);
	if (event & MEMAC_IEVNT_TX_ECC_ER)
		memac->exception_cb(memac->dev_id, FM_MAC_EX_10G_TX_ECC_ER);
	if (event & MEMAC_IEVNT_RX_ECC_ER)
		memac->exception_cb(memac->dev_id, FM_MAC_EX_10G_RX_ECC_ER);
}

static void memac_exception(void *handle)
{
	struct fman_mac *memac = (struct fman_mac *)handle;
	struct memac_regs __iomem *regs = memac->regs;
	u32 event, imask;

	event = ioread32be(&regs->ievent);
	imask = ioread32be(&regs->imask);

	/* Imask include both error and notification/event bits.
	 * Leaving only error bits enabled by imask.
	 * The imask error bits are shifted by 16 bits offset from
	 * their corresponding location in the ievent - hence the >> 16
	 */
	event &= ((imask & MEMAC_ALL_ERRS_IMASK) >> 16);

	iowrite32be(event, &regs->ievent);

	if (event & MEMAC_IEVNT_MGI)
		memac->exception_cb(memac->dev_id,
				    FM_MAC_EX_MAGIC_PACKET_INDICATION);
}

static void free_init_resources(struct fman_mac *memac)
{
	fman_unregister_intr(memac->fm, FMAN_MOD_MAC, memac->mac_id,
			     FMAN_INTR_TYPE_ERR);

	fman_unregister_intr(memac->fm, FMAN_MOD_MAC, memac->mac_id,
			     FMAN_INTR_TYPE_NORMAL);

	/* release the driver's group hash table */
	free_hash_table(memac->multicast_addr_hash);
	memac->multicast_addr_hash = NULL;

	/* release the driver's individual hash table */
	free_hash_table(memac->unicast_addr_hash);
	memac->unicast_addr_hash = NULL;
}

static bool is_init_done(struct memac_cfg *memac_drv_params)
{
	/* Checks if mEMAC driver parameters were initialized */
	if (!memac_drv_params)
		return true;

	return false;
}

int memac_enable(struct fman_mac *memac, enum comm_mode mode)
{
	struct memac_regs __iomem *regs = memac->regs;
	u32 tmp;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	if (mode & COMM_MODE_RX)
		tmp |= CMD_CFG_RX_EN;
	if (mode & COMM_MODE_TX)
		tmp |= CMD_CFG_TX_EN;

	iowrite32be(tmp, &regs->command_config);

	return 0;
}

int memac_disable(struct fman_mac *memac, enum comm_mode mode)
{
	struct memac_regs __iomem *regs = memac->regs;
	u32 tmp;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	if (mode & COMM_MODE_RX)
		tmp &= ~CMD_CFG_RX_EN;
	if (mode & COMM_MODE_TX)
		tmp &= ~CMD_CFG_TX_EN;

	iowrite32be(tmp, &regs->command_config);

	return 0;
}

int memac_set_promiscuous(struct fman_mac *memac, bool new_val)
{
	struct memac_regs __iomem *regs = memac->regs;
	u32 tmp;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	if (new_val)
		tmp |= CMD_CFG_PROMIS_EN;
	else
		tmp &= ~CMD_CFG_PROMIS_EN;

	iowrite32be(tmp, &regs->command_config);

	return 0;
}

int memac_adjust_link(struct fman_mac *memac, u16 speed)
{
	struct memac_regs __iomem *regs = memac->regs;
	u32 tmp;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	tmp = ioread32be(&regs->if_mode);

	/* Set full duplex */
	tmp &= ~IF_MODE_HD;

	if (memac->phy_if == PHY_INTERFACE_MODE_RGMII) {
		/* Configure RGMII in manual mode */
		tmp &= ~IF_MODE_RGMII_AUTO;
		tmp &= ~IF_MODE_RGMII_SP_MASK;
		/* Full duplex */
		tmp |= IF_MODE_RGMII_FD;

		switch (speed) {
		case SPEED_1000:
			tmp |= IF_MODE_RGMII_1000;
			break;
		case SPEED_100:
			tmp |= IF_MODE_RGMII_100;
			break;
		case SPEED_10:
			tmp |= IF_MODE_RGMII_10;
			break;
		default:
			break;
		}
	}

	iowrite32be(tmp, &regs->if_mode);

	return 0;
}

int memac_cfg_max_frame_len(struct fman_mac *memac, u16 new_val)
{
	if (is_init_done(memac->memac_drv_param))
		return -EINVAL;

	memac->memac_drv_param->max_frame_length = new_val;

	return 0;
}

int memac_cfg_reset_on_init(struct fman_mac *memac, bool enable)
{
	if (is_init_done(memac->memac_drv_param))
		return -EINVAL;

	memac->memac_drv_param->reset_on_init = enable;

	return 0;
}

int memac_cfg_fixed_link(struct fman_mac *memac,
			 struct fixed_phy_status *fixed_link)
{
	if (is_init_done(memac->memac_drv_param))
		return -EINVAL;

	memac->memac_drv_param->fixed_link = fixed_link;

	return 0;
}

int memac_set_tx_pause_frames(struct fman_mac *memac, u8 priority,
			      u16 pause_time, u16 thresh_time)
{
	struct memac_regs __iomem *regs = memac->regs;
	u32 tmp;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	tmp = ioread32be(&regs->tx_fifo_sections);

	GET_TX_EMPTY_DEFAULT_VALUE(tmp);
	iowrite32be(tmp, &regs->tx_fifo_sections);

	tmp = ioread32be(&regs->command_config);
	tmp &= ~CMD_CFG_PFC_MODE;

	iowrite32be(tmp, &regs->command_config);

	tmp = ioread32be(&regs->pause_quanta[priority / 2]);
	if (priority % 2)
		tmp &= CLXY_PAUSE_QUANTA_CLX_PQNT;
	else
		tmp &= CLXY_PAUSE_QUANTA_CLY_PQNT;
	tmp |= ((u32)pause_time << (16 * (priority % 2)));
	iowrite32be(tmp, &regs->pause_quanta[priority / 2]);

	tmp = ioread32be(&regs->pause_thresh[priority / 2]);
	if (priority % 2)
		tmp &= CLXY_PAUSE_THRESH_CLX_QTH;
	else
		tmp &= CLXY_PAUSE_THRESH_CLY_QTH;
	tmp |= ((u32)thresh_time << (16 * (priority % 2)));
	iowrite32be(tmp, &regs->pause_thresh[priority / 2]);

	return 0;
}

int memac_accept_rx_pause_frames(struct fman_mac *memac, bool en)
{
	struct memac_regs __iomem *regs = memac->regs;
	u32 tmp;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	tmp = ioread32be(&regs->command_config);
	if (en)
		tmp &= ~CMD_CFG_PAUSE_IGNORE;
	else
		tmp |= CMD_CFG_PAUSE_IGNORE;

	iowrite32be(tmp, &regs->command_config);

	return 0;
}

int memac_modify_mac_address(struct fman_mac *memac, enet_addr_t *enet_addr)
{
	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	add_addr_in_paddr(memac->regs, (u8 *)(*enet_addr), 0);

	return 0;
}

int memac_add_hash_mac_address(struct fman_mac *memac, enet_addr_t *eth_addr)
{
	struct memac_regs __iomem *regs = memac->regs;
	struct eth_hash_entry *hash_entry;
	u32 hash;
	u64 addr;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	addr = ENET_ADDR_TO_UINT64(*eth_addr);

	if (!(addr & GROUP_ADDRESS)) {
		/* Unicast addresses not supported in hash */
		pr_err("Unicast Address\n");
		return -EINVAL;
	}
	hash = get_mac_addr_hash_code(addr) & HASH_CTRL_ADDR_MASK;

	/* Create element to be added to the driver hash table */
	hash_entry = kmalloc(sizeof(*hash_entry), GFP_ATOMIC);
	if (!hash_entry)
		return -ENOMEM;
	hash_entry->addr = addr;
	INIT_LIST_HEAD(&hash_entry->node);

	list_add_tail(&hash_entry->node,
		      &memac->multicast_addr_hash->lsts[hash]);
	iowrite32be(hash | HASH_CTRL_MCAST_EN, &regs->hashtable_ctrl);

	return 0;
}

int memac_set_allmulti(struct fman_mac *memac, bool enable)
{
	u32 entry;
	struct memac_regs __iomem *regs = memac->regs;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	if (enable) {
		for (entry = 0; entry < HASH_TABLE_SIZE; entry++)
			iowrite32be(entry | HASH_CTRL_MCAST_EN,
				    &regs->hashtable_ctrl);
	} else {
		for (entry = 0; entry < HASH_TABLE_SIZE; entry++)
			iowrite32be(entry & ~HASH_CTRL_MCAST_EN,
				    &regs->hashtable_ctrl);
	}

	memac->allmulti_enabled = enable;

	return 0;
}

int memac_set_tstamp(struct fman_mac *memac, bool enable)
{
	return 0; /* Always enabled. */
}

int memac_del_hash_mac_address(struct fman_mac *memac, enet_addr_t *eth_addr)
{
	struct memac_regs __iomem *regs = memac->regs;
	struct eth_hash_entry *hash_entry = NULL;
	struct list_head *pos;
	u32 hash;
	u64 addr;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	addr = ENET_ADDR_TO_UINT64(*eth_addr);

	hash = get_mac_addr_hash_code(addr) & HASH_CTRL_ADDR_MASK;

	list_for_each(pos, &memac->multicast_addr_hash->lsts[hash]) {
		hash_entry = ETH_HASH_ENTRY_OBJ(pos);
		if (hash_entry && hash_entry->addr == addr) {
			list_del_init(&hash_entry->node);
			kfree(hash_entry);
			break;
		}
	}

	if (!memac->allmulti_enabled) {
		if (list_empty(&memac->multicast_addr_hash->lsts[hash]))
			iowrite32be(hash & ~HASH_CTRL_MCAST_EN,
				    &regs->hashtable_ctrl);
	}

	return 0;
}

int memac_set_exception(struct fman_mac *memac,
			enum fman_mac_exceptions exception, bool enable)
{
	u32 bit_mask = 0;

	if (!is_init_done(memac->memac_drv_param))
		return -EINVAL;

	bit_mask = get_exception_flag(exception);
	if (bit_mask) {
		if (enable)
			memac->exceptions |= bit_mask;
		else
			memac->exceptions &= ~bit_mask;
	} else {
		pr_err("Undefined exception\n");
		return -EINVAL;
	}
	set_exception(memac->regs, bit_mask, enable);

	return 0;
}

int memac_init(struct fman_mac *memac)
{
	struct memac_cfg *memac_drv_param;
	u8 i;
	enet_addr_t eth_addr;
	bool slow_10g_if = false;
	struct fixed_phy_status *fixed_link;
	int err;
	u32 reg32 = 0;

	if (is_init_done(memac->memac_drv_param))
		return -EINVAL;

	err = check_init_parameters(memac);
	if (err)
		return err;

	memac_drv_param = memac->memac_drv_param;

	if (memac->fm_rev_info.major == 6 && memac->fm_rev_info.minor == 4)
		slow_10g_if = true;

	/* First, reset the MAC if desired. */
	if (memac_drv_param->reset_on_init) {
		err = reset(memac->regs);
		if (err) {
			pr_err("mEMAC reset failed\n");
			return err;
		}
	}

	/* MAC Address */
	MAKE_ENET_ADDR_FROM_UINT64(memac->addr, eth_addr);
	add_addr_in_paddr(memac->regs, (u8 *)eth_addr, 0);

	fixed_link = memac_drv_param->fixed_link;

	init(memac->regs, memac->memac_drv_param, memac->phy_if,
	     memac->max_speed, slow_10g_if, memac->exceptions);

	/* FM_RX_FIFO_CORRUPT_ERRATA_10GMAC_A006320 errata workaround
	 * Exists only in FMan 6.0 and 6.3.
	 */
	if ((memac->fm_rev_info.major == 6) &&
	    ((memac->fm_rev_info.minor == 0) ||
	    (memac->fm_rev_info.minor == 3))) {
		/* MAC strips CRC from received frames - this workaround
		 * should decrease the likelihood of bug appearance
		 */
		reg32 = ioread32be(&memac->regs->command_config);
		reg32 &= ~CMD_CFG_CRC_FWD;
		iowrite32be(reg32, &memac->regs->command_config);
	}

	if (memac->phy_if == PHY_INTERFACE_MODE_SGMII) {
		/* Configure internal SGMII PHY */
		if (memac->basex_if)
			setup_sgmii_internal_phy_base_x(memac);
		else
			setup_sgmii_internal_phy(memac, fixed_link);
	} else if (memac->phy_if == PHY_INTERFACE_MODE_QSGMII) {
		/* Configure 4 internal SGMII PHYs */
		for (i = 0; i < 4; i++) {
			u8 qsmgii_phy_addr, phy_addr;
			/* QSGMII PHY address occupies 3 upper bits of 5-bit
			 * phy_address; the lower 2 bits are used to extend
			 * register address space and access each one of 4
			 * ports inside QSGMII.
			 */
			phy_addr = memac->pcsphy->mdio.addr;
			qsmgii_phy_addr = (u8)((phy_addr << 2) | i);
			memac->pcsphy->mdio.addr = qsmgii_phy_addr;
			if (memac->basex_if)
				setup_sgmii_internal_phy_base_x(memac);
			else
				setup_sgmii_internal_phy(memac, fixed_link);

			memac->pcsphy->mdio.addr = phy_addr;
		}
	}

	/* Max Frame Length */
	err = fman_set_mac_max_frame(memac->fm, memac->mac_id,
				     memac_drv_param->max_frame_length);
	if (err) {
		pr_err("settings Mac max frame length is FAILED\n");
		return err;
	}

	memac->multicast_addr_hash = alloc_hash_table(HASH_TABLE_SIZE);
	if (!memac->multicast_addr_hash) {
		free_init_resources(memac);
		pr_err("allocation hash table is FAILED\n");
		return -ENOMEM;
	}

	memac->unicast_addr_hash = alloc_hash_table(HASH_TABLE_SIZE);
	if (!memac->unicast_addr_hash) {
		free_init_resources(memac);
		pr_err("allocation hash table is FAILED\n");
		return -ENOMEM;
	}

	fman_register_intr(memac->fm, FMAN_MOD_MAC, memac->mac_id,
			   FMAN_INTR_TYPE_ERR, memac_err_exception, memac);

	fman_register_intr(memac->fm, FMAN_MOD_MAC, memac->mac_id,
			   FMAN_INTR_TYPE_NORMAL, memac_exception, memac);

	kfree(memac_drv_param);
	memac->memac_drv_param = NULL;

	return 0;
}

int memac_free(struct fman_mac *memac)
{
	free_init_resources(memac);

	if (memac->pcsphy)
		put_device(&memac->pcsphy->mdio.dev);

	kfree(memac->memac_drv_param);
	kfree(memac);

	return 0;
}

struct fman_mac *memac_config(struct fman_mac_params *params)
{
	struct fman_mac *memac;
	struct memac_cfg *memac_drv_param;
	void __iomem *base_addr;

	base_addr = params->base_addr;
	/* allocate memory for the m_emac data structure */
	memac = kzalloc(sizeof(*memac), GFP_KERNEL);
	if (!memac)
		return NULL;

	/* allocate memory for the m_emac driver parameters data structure */
	memac_drv_param = kzalloc(sizeof(*memac_drv_param), GFP_KERNEL);
	if (!memac_drv_param) {
		memac_free(memac);
		return NULL;
	}

	/* Plant parameter structure pointer */
	memac->memac_drv_param = memac_drv_param;

	set_dflts(memac_drv_param);

	memac->addr = ENET_ADDR_TO_UINT64(params->addr);

	memac->regs = base_addr;
	memac->max_speed = params->max_speed;
	memac->phy_if = params->phy_if;
	memac->mac_id = params->mac_id;
	memac->exceptions = (MEMAC_IMASK_TSECC_ER | MEMAC_IMASK_TECC_ER |
			     MEMAC_IMASK_RECC_ER | MEMAC_IMASK_MGI);
	memac->exception_cb = params->exception_cb;
	memac->event_cb = params->event_cb;
	memac->dev_id = params->dev_id;
	memac->fm = params->fm;
	memac->basex_if = params->basex_if;

	/* Save FMan revision */
	fman_get_revision(memac->fm, &memac->fm_rev_info);

	if (memac->phy_if == PHY_INTERFACE_MODE_SGMII ||
	    memac->phy_if == PHY_INTERFACE_MODE_QSGMII) {
		if (!params->internal_phy_node) {
			pr_err("PCS PHY node is not available\n");
			memac_free(memac);
			return NULL;
		}

		memac->pcsphy = of_phy_find_device(params->internal_phy_node);
		if (!memac->pcsphy) {
			pr_err("of_phy_find_device (PCS PHY) failed\n");
			memac_free(memac);
			return NULL;
		}
	}

	return memac;
}
