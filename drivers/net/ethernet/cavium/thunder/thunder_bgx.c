/*
 * Copyright (C) 2015 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>

#include "nic_reg.h"
#include "nic.h"
#include "thunder_bgx.h"

#define DRV_NAME	"thunder-BGX"
#define DRV_VERSION	"1.0"

struct lmac {
	struct bgx		*bgx;
	int			dmac;
	u8			mac[ETH_ALEN];
	bool			link_up;
	int			lmacid; /* ID within BGX */
	int			lmacid_bd; /* ID on board */
	struct net_device       netdev;
	struct phy_device       *phydev;
	unsigned int            last_duplex;
	unsigned int            last_link;
	unsigned int            last_speed;
	bool			is_sgmii;
	struct delayed_work	dwork;
	struct workqueue_struct *check_link;
};

struct bgx {
	u8			bgx_id;
	u8			qlm_mode;
	struct	lmac		lmac[MAX_LMAC_PER_BGX];
	int			lmac_count;
	int                     lmac_type;
	int                     lane_to_sds;
	int			use_training;
	void __iomem		*reg_base;
	struct pci_dev		*pdev;
};

static struct bgx *bgx_vnic[MAX_BGX_THUNDER];
static int lmac_count; /* Total no of LMACs in system */

static int bgx_xaui_check_link(struct lmac *lmac);

/* Supported devices */
static const struct pci_device_id bgx_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_BGX) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Cavium Inc");
MODULE_DESCRIPTION("Cavium Thunder BGX/MAC Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, bgx_id_table);

/* The Cavium ThunderX network controller can *only* be found in SoCs
 * containing the ThunderX ARM64 CPU implementation.  All accesses to the device
 * registers on this platform are implicitly strongly ordered with respect
 * to memory accesses. So writeq_relaxed() and readq_relaxed() are safe to use
 * with no memory barriers in this driver.  The readq()/writeq() functions add
 * explicit ordering operation which in this case are redundant, and only
 * add overhead.
 */

/* Register read/write APIs */
static u64 bgx_reg_read(struct bgx *bgx, u8 lmac, u64 offset)
{
	void __iomem *addr = bgx->reg_base + ((u32)lmac << 20) + offset;

	return readq_relaxed(addr);
}

static void bgx_reg_write(struct bgx *bgx, u8 lmac, u64 offset, u64 val)
{
	void __iomem *addr = bgx->reg_base + ((u32)lmac << 20) + offset;

	writeq_relaxed(val, addr);
}

static void bgx_reg_modify(struct bgx *bgx, u8 lmac, u64 offset, u64 val)
{
	void __iomem *addr = bgx->reg_base + ((u32)lmac << 20) + offset;

	writeq_relaxed(val | readq_relaxed(addr), addr);
}

static int bgx_poll_reg(struct bgx *bgx, u8 lmac, u64 reg, u64 mask, bool zero)
{
	int timeout = 100;
	u64 reg_val;

	while (timeout) {
		reg_val = bgx_reg_read(bgx, lmac, reg);
		if (zero && !(reg_val & mask))
			return 0;
		if (!zero && (reg_val & mask))
			return 0;
		usleep_range(1000, 2000);
		timeout--;
	}
	return 1;
}

/* Return number of BGX present in HW */
unsigned bgx_get_map(int node)
{
	int i;
	unsigned map = 0;

	for (i = 0; i < MAX_BGX_PER_CN88XX; i++) {
		if (bgx_vnic[(node * MAX_BGX_PER_CN88XX) + i])
			map |= (1 << i);
	}

	return map;
}
EXPORT_SYMBOL(bgx_get_map);

/* Return number of LMAC configured for this BGX */
int bgx_get_lmac_count(int node, int bgx_idx)
{
	struct bgx *bgx;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (bgx)
		return bgx->lmac_count;

	return 0;
}
EXPORT_SYMBOL(bgx_get_lmac_count);

/* Returns the current link status of LMAC */
void bgx_get_lmac_link_state(int node, int bgx_idx, int lmacid, void *status)
{
	struct bgx_link_status *link = (struct bgx_link_status *)status;
	struct bgx *bgx;
	struct lmac *lmac;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (!bgx)
		return;

	lmac = &bgx->lmac[lmacid];
	link->link_up = lmac->link_up;
	link->duplex = lmac->last_duplex;
	link->speed = lmac->last_speed;
}
EXPORT_SYMBOL(bgx_get_lmac_link_state);

const u8 *bgx_get_lmac_mac(int node, int bgx_idx, int lmacid)
{
	struct bgx *bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];

	if (bgx)
		return bgx->lmac[lmacid].mac;

	return NULL;
}
EXPORT_SYMBOL(bgx_get_lmac_mac);

void bgx_set_lmac_mac(int node, int bgx_idx, int lmacid, const u8 *mac)
{
	struct bgx *bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];

	if (!bgx)
		return;

	ether_addr_copy(bgx->lmac[lmacid].mac, mac);
}
EXPORT_SYMBOL(bgx_set_lmac_mac);

void bgx_lmac_rx_tx_enable(int node, int bgx_idx, int lmacid, bool enable)
{
	struct bgx *bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	u64 cfg;

	if (!bgx)
		return;

	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	if (enable)
		cfg |= CMR_PKT_RX_EN | CMR_PKT_TX_EN;
	else
		cfg &= ~(CMR_PKT_RX_EN | CMR_PKT_TX_EN);
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);
}
EXPORT_SYMBOL(bgx_lmac_rx_tx_enable);

static void bgx_sgmii_change_link_state(struct lmac *lmac)
{
	struct bgx *bgx = lmac->bgx;
	u64 cmr_cfg;
	u64 port_cfg = 0;
	u64 misc_ctl = 0;

	cmr_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_CMRX_CFG);
	cmr_cfg &= ~CMR_EN;
	bgx_reg_write(bgx, lmac->lmacid, BGX_CMRX_CFG, cmr_cfg);

	port_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG);
	misc_ctl = bgx_reg_read(bgx, lmac->lmacid, BGX_GMP_PCS_MISCX_CTL);

	if (lmac->link_up) {
		misc_ctl &= ~PCS_MISC_CTL_GMX_ENO;
		port_cfg &= ~GMI_PORT_CFG_DUPLEX;
		port_cfg |=  (lmac->last_duplex << 2);
	} else {
		misc_ctl |= PCS_MISC_CTL_GMX_ENO;
	}

	switch (lmac->last_speed) {
	case 10:
		port_cfg &= ~GMI_PORT_CFG_SPEED; /* speed 0 */
		port_cfg |= GMI_PORT_CFG_SPEED_MSB;  /* speed_msb 1 */
		port_cfg &= ~GMI_PORT_CFG_SLOT_TIME; /* slottime 0 */
		misc_ctl &= ~PCS_MISC_CTL_SAMP_PT_MASK;
		misc_ctl |= 50; /* samp_pt */
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_SLOT, 64);
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_BURST, 0);
		break;
	case 100:
		port_cfg &= ~GMI_PORT_CFG_SPEED; /* speed 0 */
		port_cfg &= ~GMI_PORT_CFG_SPEED_MSB; /* speed_msb 0 */
		port_cfg &= ~GMI_PORT_CFG_SLOT_TIME; /* slottime 0 */
		misc_ctl &= ~PCS_MISC_CTL_SAMP_PT_MASK;
		misc_ctl |= 5; /* samp_pt */
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_SLOT, 64);
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_BURST, 0);
		break;
	case 1000:
		port_cfg |= GMI_PORT_CFG_SPEED; /* speed 1 */
		port_cfg &= ~GMI_PORT_CFG_SPEED_MSB; /* speed_msb 0 */
		port_cfg |= GMI_PORT_CFG_SLOT_TIME; /* slottime 1 */
		misc_ctl &= ~PCS_MISC_CTL_SAMP_PT_MASK;
		misc_ctl |= 1; /* samp_pt */
		bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_TXX_SLOT, 512);
		if (lmac->last_duplex)
			bgx_reg_write(bgx, lmac->lmacid,
				      BGX_GMP_GMI_TXX_BURST, 0);
		else
			bgx_reg_write(bgx, lmac->lmacid,
				      BGX_GMP_GMI_TXX_BURST, 8192);
		break;
	default:
		break;
	}
	bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_PCS_MISCX_CTL, misc_ctl);
	bgx_reg_write(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG, port_cfg);

	port_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG);

	/* renable lmac */
	cmr_cfg |= CMR_EN;
	bgx_reg_write(bgx, lmac->lmacid, BGX_CMRX_CFG, cmr_cfg);
}

static void bgx_lmac_handler(struct net_device *netdev)
{
	struct lmac *lmac = container_of(netdev, struct lmac, netdev);
	struct phy_device *phydev = lmac->phydev;
	int link_changed = 0;

	if (!lmac)
		return;

	if (!phydev->link && lmac->last_link)
		link_changed = -1;

	if (phydev->link &&
	    (lmac->last_duplex != phydev->duplex ||
	     lmac->last_link != phydev->link ||
	     lmac->last_speed != phydev->speed)) {
			link_changed = 1;
	}

	lmac->last_link = phydev->link;
	lmac->last_speed = phydev->speed;
	lmac->last_duplex = phydev->duplex;

	if (!link_changed)
		return;

	if (link_changed > 0)
		lmac->link_up = true;
	else
		lmac->link_up = false;

	if (lmac->is_sgmii)
		bgx_sgmii_change_link_state(lmac);
	else
		bgx_xaui_check_link(lmac);
}

u64 bgx_get_rx_stats(int node, int bgx_idx, int lmac, int idx)
{
	struct bgx *bgx;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (!bgx)
		return 0;

	if (idx > 8)
		lmac = 0;
	return bgx_reg_read(bgx, lmac, BGX_CMRX_RX_STAT0 + (idx * 8));
}
EXPORT_SYMBOL(bgx_get_rx_stats);

u64 bgx_get_tx_stats(int node, int bgx_idx, int lmac, int idx)
{
	struct bgx *bgx;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (!bgx)
		return 0;

	return bgx_reg_read(bgx, lmac, BGX_CMRX_TX_STAT0 + (idx * 8));
}
EXPORT_SYMBOL(bgx_get_tx_stats);

static void bgx_flush_dmac_addrs(struct bgx *bgx, int lmac)
{
	u64 offset;

	while (bgx->lmac[lmac].dmac > 0) {
		offset = ((bgx->lmac[lmac].dmac - 1) * sizeof(u64)) +
			(lmac * MAX_DMAC_PER_LMAC * sizeof(u64));
		bgx_reg_write(bgx, 0, BGX_CMR_RX_DMACX_CAM + offset, 0);
		bgx->lmac[lmac].dmac--;
	}
}

/* Configure BGX LMAC in internal loopback mode */
void bgx_lmac_internal_loopback(int node, int bgx_idx,
				int lmac_idx, bool enable)
{
	struct bgx *bgx;
	struct lmac *lmac;
	u64    cfg;

	bgx = bgx_vnic[(node * MAX_BGX_PER_CN88XX) + bgx_idx];
	if (!bgx)
		return;

	lmac = &bgx->lmac[lmac_idx];
	if (lmac->is_sgmii) {
		cfg = bgx_reg_read(bgx, lmac_idx, BGX_GMP_PCS_MRX_CTL);
		if (enable)
			cfg |= PCS_MRX_CTL_LOOPBACK1;
		else
			cfg &= ~PCS_MRX_CTL_LOOPBACK1;
		bgx_reg_write(bgx, lmac_idx, BGX_GMP_PCS_MRX_CTL, cfg);
	} else {
		cfg = bgx_reg_read(bgx, lmac_idx, BGX_SPUX_CONTROL1);
		if (enable)
			cfg |= SPU_CTL_LOOPBACK;
		else
			cfg &= ~SPU_CTL_LOOPBACK;
		bgx_reg_write(bgx, lmac_idx, BGX_SPUX_CONTROL1, cfg);
	}
}
EXPORT_SYMBOL(bgx_lmac_internal_loopback);

static int bgx_lmac_sgmii_init(struct bgx *bgx, int lmacid)
{
	u64 cfg;

	bgx_reg_modify(bgx, lmacid, BGX_GMP_GMI_TXX_THRESH, 0x30);
	/* max packet size */
	bgx_reg_modify(bgx, lmacid, BGX_GMP_GMI_RXX_JABBER, MAX_FRAME_SIZE);

	/* Disable frame alignment if using preamble */
	cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_GMI_TXX_APPEND);
	if (cfg & 1)
		bgx_reg_write(bgx, lmacid, BGX_GMP_GMI_TXX_SGMII_CTL, 0);

	/* Enable lmac */
	bgx_reg_modify(bgx, lmacid, BGX_CMRX_CFG, CMR_EN);

	/* PCS reset */
	bgx_reg_modify(bgx, lmacid, BGX_GMP_PCS_MRX_CTL, PCS_MRX_CTL_RESET);
	if (bgx_poll_reg(bgx, lmacid, BGX_GMP_PCS_MRX_CTL,
			 PCS_MRX_CTL_RESET, true)) {
		dev_err(&bgx->pdev->dev, "BGX PCS reset not completed\n");
		return -1;
	}

	/* power down, reset autoneg, autoneg enable */
	cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_PCS_MRX_CTL);
	cfg &= ~PCS_MRX_CTL_PWR_DN;
	cfg |= (PCS_MRX_CTL_RST_AN | PCS_MRX_CTL_AN_EN);
	bgx_reg_write(bgx, lmacid, BGX_GMP_PCS_MRX_CTL, cfg);

	if (bgx_poll_reg(bgx, lmacid, BGX_GMP_PCS_MRX_STATUS,
			 PCS_MRX_STATUS_AN_CPT, false)) {
		dev_err(&bgx->pdev->dev, "BGX AN_CPT not completed\n");
		return -1;
	}

	return 0;
}

static int bgx_lmac_xaui_init(struct bgx *bgx, int lmacid, int lmac_type)
{
	u64 cfg;

	/* Reset SPU */
	bgx_reg_modify(bgx, lmacid, BGX_SPUX_CONTROL1, SPU_CTL_RESET);
	if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_CONTROL1, SPU_CTL_RESET, true)) {
		dev_err(&bgx->pdev->dev, "BGX SPU reset not completed\n");
		return -1;
	}

	/* Disable LMAC */
	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cfg &= ~CMR_EN;
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);

	bgx_reg_modify(bgx, lmacid, BGX_SPUX_CONTROL1, SPU_CTL_LOW_POWER);
	/* Set interleaved running disparity for RXAUI */
	if (bgx->lmac_type != BGX_MODE_RXAUI)
		bgx_reg_modify(bgx, lmacid,
			       BGX_SPUX_MISC_CONTROL, SPU_MISC_CTL_RX_DIS);
	else
		bgx_reg_modify(bgx, lmacid, BGX_SPUX_MISC_CONTROL,
			       SPU_MISC_CTL_RX_DIS | SPU_MISC_CTL_INTLV_RDISP);

	/* clear all interrupts */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_RX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_RX_INT, cfg);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_INT, cfg);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);

	if (bgx->use_training) {
		bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_LP_CUP, 0x00);
		bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_LD_CUP, 0x00);
		bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_LD_REP, 0x00);
		/* training enable */
		bgx_reg_modify(bgx, lmacid,
			       BGX_SPUX_BR_PMD_CRTL, SPU_PMD_CRTL_TRAIN_EN);
	}

	/* Append FCS to each packet */
	bgx_reg_modify(bgx, lmacid, BGX_SMUX_TX_APPEND, SMU_TX_APPEND_FCS_D);

	/* Disable forward error correction */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_FEC_CONTROL);
	cfg &= ~SPU_FEC_CTL_FEC_EN;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_FEC_CONTROL, cfg);

	/* Disable autoneg */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_AN_CONTROL);
	cfg = cfg & ~(SPU_AN_CTL_AN_EN | SPU_AN_CTL_XNP_EN);
	bgx_reg_write(bgx, lmacid, BGX_SPUX_AN_CONTROL, cfg);

	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_AN_ADV);
	if (bgx->lmac_type == BGX_MODE_10G_KR)
		cfg |= (1 << 23);
	else if (bgx->lmac_type == BGX_MODE_40G_KR)
		cfg |= (1 << 24);
	else
		cfg &= ~((1 << 23) | (1 << 24));
	cfg = cfg & (~((1ULL << 25) | (1ULL << 22) | (1ULL << 12)));
	bgx_reg_write(bgx, lmacid, BGX_SPUX_AN_ADV, cfg);

	cfg = bgx_reg_read(bgx, 0, BGX_SPU_DBG_CONTROL);
	cfg &= ~SPU_DBG_CTL_AN_ARB_LINK_CHK_EN;
	bgx_reg_write(bgx, 0, BGX_SPU_DBG_CONTROL, cfg);

	/* Enable lmac */
	bgx_reg_modify(bgx, lmacid, BGX_CMRX_CFG, CMR_EN);

	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_CONTROL1);
	cfg &= ~SPU_CTL_LOW_POWER;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_CONTROL1, cfg);

	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_CTL);
	cfg &= ~SMU_TX_CTL_UNI_EN;
	cfg |= SMU_TX_CTL_DIC_EN;
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_CTL, cfg);

	/* take lmac_count into account */
	bgx_reg_modify(bgx, lmacid, BGX_SMUX_TX_THRESH, (0x100 - 1));
	/* max packet size */
	bgx_reg_modify(bgx, lmacid, BGX_SMUX_RX_JABBER, MAX_FRAME_SIZE);

	return 0;
}

static int bgx_xaui_check_link(struct lmac *lmac)
{
	struct bgx *bgx = lmac->bgx;
	int lmacid = lmac->lmacid;
	int lmac_type = bgx->lmac_type;
	u64 cfg;

	bgx_reg_modify(bgx, lmacid, BGX_SPUX_MISC_CONTROL, SPU_MISC_CTL_RX_DIS);
	if (bgx->use_training) {
		cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
		if (!(cfg & (1ull << 13))) {
			cfg = (1ull << 13) | (1ull << 14);
			bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);
			cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_BR_PMD_CRTL);
			cfg |= (1ull << 0);
			bgx_reg_write(bgx, lmacid, BGX_SPUX_BR_PMD_CRTL, cfg);
			return -1;
		}
	}

	/* wait for PCS to come out of reset */
	if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_CONTROL1, SPU_CTL_RESET, true)) {
		dev_err(&bgx->pdev->dev, "BGX SPU reset not completed\n");
		return -1;
	}

	if ((lmac_type == BGX_MODE_10G_KR) || (lmac_type == BGX_MODE_XFI) ||
	    (lmac_type == BGX_MODE_40G_KR) || (lmac_type == BGX_MODE_XLAUI)) {
		if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_BR_STATUS1,
				 SPU_BR_STATUS_BLK_LOCK, false)) {
			dev_err(&bgx->pdev->dev,
				"SPU_BR_STATUS_BLK_LOCK not completed\n");
			return -1;
		}
	} else {
		if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_BX_STATUS,
				 SPU_BX_STATUS_RX_ALIGN, false)) {
			dev_err(&bgx->pdev->dev,
				"SPU_BX_STATUS_RX_ALIGN not completed\n");
			return -1;
		}
	}

	/* Clear rcvflt bit (latching high) and read it back */
	bgx_reg_modify(bgx, lmacid, BGX_SPUX_STATUS2, SPU_STATUS2_RCVFLT);
	if (bgx_reg_read(bgx, lmacid, BGX_SPUX_STATUS2) & SPU_STATUS2_RCVFLT) {
		dev_err(&bgx->pdev->dev, "Receive fault, retry training\n");
		if (bgx->use_training) {
			cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
			if (!(cfg & (1ull << 13))) {
				cfg = (1ull << 13) | (1ull << 14);
				bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);
				cfg = bgx_reg_read(bgx, lmacid,
						   BGX_SPUX_BR_PMD_CRTL);
				cfg |= (1ull << 0);
				bgx_reg_write(bgx, lmacid,
					      BGX_SPUX_BR_PMD_CRTL, cfg);
				return -1;
			}
		}
		return -1;
	}

	/* Wait for MAC RX to be ready */
	if (bgx_poll_reg(bgx, lmacid, BGX_SMUX_RX_CTL,
			 SMU_RX_CTL_STATUS, true)) {
		dev_err(&bgx->pdev->dev, "SMU RX link not okay\n");
		return -1;
	}

	/* Wait for BGX RX to be idle */
	if (bgx_poll_reg(bgx, lmacid, BGX_SMUX_CTL, SMU_CTL_RX_IDLE, false)) {
		dev_err(&bgx->pdev->dev, "SMU RX not idle\n");
		return -1;
	}

	/* Wait for BGX TX to be idle */
	if (bgx_poll_reg(bgx, lmacid, BGX_SMUX_CTL, SMU_CTL_TX_IDLE, false)) {
		dev_err(&bgx->pdev->dev, "SMU TX not idle\n");
		return -1;
	}

	if (bgx_reg_read(bgx, lmacid, BGX_SPUX_STATUS2) & SPU_STATUS2_RCVFLT) {
		dev_err(&bgx->pdev->dev, "Receive fault\n");
		return -1;
	}

	/* Receive link is latching low. Force it high and verify it */
	bgx_reg_modify(bgx, lmacid, BGX_SPUX_STATUS1, SPU_STATUS1_RCV_LNK);
	if (bgx_poll_reg(bgx, lmacid, BGX_SPUX_STATUS1,
			 SPU_STATUS1_RCV_LNK, false)) {
		dev_err(&bgx->pdev->dev, "SPU receive link down\n");
		return -1;
	}

	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_MISC_CONTROL);
	cfg &= ~SPU_MISC_CTL_RX_DIS;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_MISC_CONTROL, cfg);
	return 0;
}

static void bgx_poll_for_link(struct work_struct *work)
{
	struct lmac *lmac;
	u64 link;

	lmac = container_of(work, struct lmac, dwork.work);

	/* Receive link is latching low. Force it high and verify it */
	bgx_reg_modify(lmac->bgx, lmac->lmacid,
		       BGX_SPUX_STATUS1, SPU_STATUS1_RCV_LNK);
	bgx_poll_reg(lmac->bgx, lmac->lmacid, BGX_SPUX_STATUS1,
		     SPU_STATUS1_RCV_LNK, false);

	link = bgx_reg_read(lmac->bgx, lmac->lmacid, BGX_SPUX_STATUS1);
	if (link & SPU_STATUS1_RCV_LNK) {
		lmac->link_up = 1;
		if (lmac->bgx->lmac_type == BGX_MODE_XLAUI)
			lmac->last_speed = 40000;
		else
			lmac->last_speed = 10000;
		lmac->last_duplex = 1;
	} else {
		lmac->link_up = 0;
		lmac->last_speed = SPEED_UNKNOWN;
		lmac->last_duplex = DUPLEX_UNKNOWN;
	}

	if (lmac->last_link != lmac->link_up) {
		lmac->last_link = lmac->link_up;
		if (lmac->link_up)
			bgx_xaui_check_link(lmac);
	}

	queue_delayed_work(lmac->check_link, &lmac->dwork, HZ * 2);
}

static int bgx_lmac_enable(struct bgx *bgx, u8 lmacid)
{
	struct lmac *lmac;
	u64 cfg;

	lmac = &bgx->lmac[lmacid];
	lmac->bgx = bgx;

	if (bgx->lmac_type == BGX_MODE_SGMII) {
		lmac->is_sgmii = 1;
		if (bgx_lmac_sgmii_init(bgx, lmacid))
			return -1;
	} else {
		lmac->is_sgmii = 0;
		if (bgx_lmac_xaui_init(bgx, lmacid, bgx->lmac_type))
			return -1;
	}

	if (lmac->is_sgmii) {
		cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_GMI_TXX_APPEND);
		cfg |= ((1ull << 2) | (1ull << 1)); /* FCS and PAD */
		bgx_reg_modify(bgx, lmacid, BGX_GMP_GMI_TXX_APPEND, cfg);
		bgx_reg_write(bgx, lmacid, BGX_GMP_GMI_TXX_MIN_PKT, 60 - 1);
	} else {
		cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_APPEND);
		cfg |= ((1ull << 2) | (1ull << 1)); /* FCS and PAD */
		bgx_reg_modify(bgx, lmacid, BGX_SMUX_TX_APPEND, cfg);
		bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_MIN_PKT, 60 + 4);
	}

	/* Enable lmac */
	bgx_reg_modify(bgx, lmacid, BGX_CMRX_CFG, CMR_EN);

	/* Restore default cfg, incase low level firmware changed it */
	bgx_reg_write(bgx, lmacid, BGX_CMRX_RX_DMAC_CTL, 0x03);

	if ((bgx->lmac_type != BGX_MODE_XFI) &&
	    (bgx->lmac_type != BGX_MODE_XLAUI) &&
	    (bgx->lmac_type != BGX_MODE_40G_KR) &&
	    (bgx->lmac_type != BGX_MODE_10G_KR)) {
		if (!lmac->phydev)
			return -ENODEV;

		lmac->phydev->dev_flags = 0;

		if (phy_connect_direct(&lmac->netdev, lmac->phydev,
				       bgx_lmac_handler,
				       PHY_INTERFACE_MODE_SGMII))
			return -ENODEV;

		phy_start_aneg(lmac->phydev);
	} else {
		lmac->check_link = alloc_workqueue("check_link", WQ_UNBOUND |
						   WQ_MEM_RECLAIM, 1);
		if (!lmac->check_link)
			return -ENOMEM;
		INIT_DELAYED_WORK(&lmac->dwork, bgx_poll_for_link);
		queue_delayed_work(lmac->check_link, &lmac->dwork, 0);
	}

	return 0;
}

static void bgx_lmac_disable(struct bgx *bgx, u8 lmacid)
{
	struct lmac *lmac;
	u64 cmrx_cfg;

	lmac = &bgx->lmac[lmacid];
	if (lmac->check_link) {
		/* Destroy work queue */
		cancel_delayed_work_sync(&lmac->dwork);
		destroy_workqueue(lmac->check_link);
	}

	cmrx_cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cmrx_cfg &= ~(1 << 15);
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cmrx_cfg);
	bgx_flush_dmac_addrs(bgx, lmacid);

	if ((bgx->lmac_type != BGX_MODE_XFI) &&
	    (bgx->lmac_type != BGX_MODE_XLAUI) &&
	    (bgx->lmac_type != BGX_MODE_40G_KR) &&
	    (bgx->lmac_type != BGX_MODE_10G_KR) && lmac->phydev)
		phy_disconnect(lmac->phydev);

	lmac->phydev = NULL;
}

static void bgx_set_num_ports(struct bgx *bgx)
{
	u64 lmac_count;

	switch (bgx->qlm_mode) {
	case QLM_MODE_SGMII:
		bgx->lmac_count = 4;
		bgx->lmac_type = BGX_MODE_SGMII;
		bgx->lane_to_sds = 0;
		break;
	case QLM_MODE_XAUI_1X4:
		bgx->lmac_count = 1;
		bgx->lmac_type = BGX_MODE_XAUI;
		bgx->lane_to_sds = 0xE4;
			break;
	case QLM_MODE_RXAUI_2X2:
		bgx->lmac_count = 2;
		bgx->lmac_type = BGX_MODE_RXAUI;
		bgx->lane_to_sds = 0xE4;
			break;
	case QLM_MODE_XFI_4X1:
		bgx->lmac_count = 4;
		bgx->lmac_type = BGX_MODE_XFI;
		bgx->lane_to_sds = 0;
		break;
	case QLM_MODE_XLAUI_1X4:
		bgx->lmac_count = 1;
		bgx->lmac_type = BGX_MODE_XLAUI;
		bgx->lane_to_sds = 0xE4;
		break;
	case QLM_MODE_10G_KR_4X1:
		bgx->lmac_count = 4;
		bgx->lmac_type = BGX_MODE_10G_KR;
		bgx->lane_to_sds = 0;
		bgx->use_training = 1;
		break;
	case QLM_MODE_40G_KR4_1X4:
		bgx->lmac_count = 1;
		bgx->lmac_type = BGX_MODE_40G_KR;
		bgx->lane_to_sds = 0xE4;
		bgx->use_training = 1;
		break;
	default:
		bgx->lmac_count = 0;
		break;
	}

	/* Check if low level firmware has programmed LMAC count
	 * based on board type, if yes consider that otherwise
	 * the default static values
	 */
	lmac_count = bgx_reg_read(bgx, 0, BGX_CMR_RX_LMACS) & 0x7;
	if (lmac_count != 4)
		bgx->lmac_count = lmac_count;
}

static void bgx_init_hw(struct bgx *bgx)
{
	int i;

	bgx_set_num_ports(bgx);

	bgx_reg_modify(bgx, 0, BGX_CMR_GLOBAL_CFG, CMR_GLOBAL_CFG_FCS_STRIP);
	if (bgx_reg_read(bgx, 0, BGX_CMR_BIST_STATUS))
		dev_err(&bgx->pdev->dev, "BGX%d BIST failed\n", bgx->bgx_id);

	/* Set lmac type and lane2serdes mapping */
	for (i = 0; i < bgx->lmac_count; i++) {
		if (bgx->lmac_type == BGX_MODE_RXAUI) {
			if (i)
				bgx->lane_to_sds = 0x0e;
			else
				bgx->lane_to_sds = 0x04;
			bgx_reg_write(bgx, i, BGX_CMRX_CFG,
				      (bgx->lmac_type << 8) | bgx->lane_to_sds);
			continue;
		}
		bgx_reg_write(bgx, i, BGX_CMRX_CFG,
			      (bgx->lmac_type << 8) | (bgx->lane_to_sds + i));
		bgx->lmac[i].lmacid_bd = lmac_count;
		lmac_count++;
	}

	bgx_reg_write(bgx, 0, BGX_CMR_TX_LMACS, bgx->lmac_count);
	bgx_reg_write(bgx, 0, BGX_CMR_RX_LMACS, bgx->lmac_count);

	/* Set the backpressure AND mask */
	for (i = 0; i < bgx->lmac_count; i++)
		bgx_reg_modify(bgx, 0, BGX_CMR_CHAN_MSK_AND,
			       ((1ULL << MAX_BGX_CHANS_PER_LMAC) - 1) <<
			       (i * MAX_BGX_CHANS_PER_LMAC));

	/* Disable all MAC filtering */
	for (i = 0; i < RX_DMAC_COUNT; i++)
		bgx_reg_write(bgx, 0, BGX_CMR_RX_DMACX_CAM + (i * 8), 0x00);

	/* Disable MAC steering (NCSI traffic) */
	for (i = 0; i < RX_TRAFFIC_STEER_RULE_COUNT; i++)
		bgx_reg_write(bgx, 0, BGX_CMR_RX_STREERING + (i * 8), 0x00);
}

static void bgx_get_qlm_mode(struct bgx *bgx)
{
	struct device *dev = &bgx->pdev->dev;
	int lmac_type;
	int train_en;

	/* Read LMAC0 type to figure out QLM mode
	 * This is configured by low level firmware
	 */
	lmac_type = bgx_reg_read(bgx, 0, BGX_CMRX_CFG);
	lmac_type = (lmac_type >> 8) & 0x07;

	train_en = bgx_reg_read(bgx, 0, BGX_SPUX_BR_PMD_CRTL) &
				SPU_PMD_CRTL_TRAIN_EN;

	switch (lmac_type) {
	case BGX_MODE_SGMII:
		bgx->qlm_mode = QLM_MODE_SGMII;
		dev_info(dev, "BGX%d QLM mode: SGMII\n", bgx->bgx_id);
		break;
	case BGX_MODE_XAUI:
		bgx->qlm_mode = QLM_MODE_XAUI_1X4;
		dev_info(dev, "BGX%d QLM mode: XAUI\n", bgx->bgx_id);
		break;
	case BGX_MODE_RXAUI:
		bgx->qlm_mode = QLM_MODE_RXAUI_2X2;
		dev_info(dev, "BGX%d QLM mode: RXAUI\n", bgx->bgx_id);
		break;
	case BGX_MODE_XFI:
		if (!train_en) {
			bgx->qlm_mode = QLM_MODE_XFI_4X1;
			dev_info(dev, "BGX%d QLM mode: XFI\n", bgx->bgx_id);
		} else {
			bgx->qlm_mode = QLM_MODE_10G_KR_4X1;
			dev_info(dev, "BGX%d QLM mode: 10G_KR\n", bgx->bgx_id);
		}
		break;
	case BGX_MODE_XLAUI:
		if (!train_en) {
			bgx->qlm_mode = QLM_MODE_XLAUI_1X4;
			dev_info(dev, "BGX%d QLM mode: XLAUI\n", bgx->bgx_id);
		} else {
			bgx->qlm_mode = QLM_MODE_40G_KR4_1X4;
			dev_info(dev, "BGX%d QLM mode: 40G_KR4\n", bgx->bgx_id);
		}
		break;
	default:
		bgx->qlm_mode = QLM_MODE_SGMII;
		dev_info(dev, "BGX%d QLM default mode: SGMII\n", bgx->bgx_id);
	}
}

#ifdef CONFIG_ACPI

static int acpi_get_mac_address(struct device *dev, struct acpi_device *adev,
				u8 *dst)
{
	u8 mac[ETH_ALEN];
	int ret;

	ret = fwnode_property_read_u8_array(acpi_fwnode_handle(adev),
					    "mac-address", mac, ETH_ALEN);
	if (ret)
		goto out;

	if (!is_valid_ether_addr(mac)) {
		dev_err(dev, "MAC address invalid: %pM\n", mac);
		ret = -EINVAL;
		goto out;
	}

	dev_info(dev, "MAC address set to: %pM\n", mac);

	memcpy(dst, mac, ETH_ALEN);
out:
	return ret;
}

/* Currently only sets the MAC address. */
static acpi_status bgx_acpi_register_phy(acpi_handle handle,
					 u32 lvl, void *context, void **rv)
{
	struct bgx *bgx = context;
	struct device *dev = &bgx->pdev->dev;
	struct acpi_device *adev;

	if (acpi_bus_get_device(handle, &adev))
		goto out;

	acpi_get_mac_address(dev, adev, bgx->lmac[bgx->lmac_count].mac);

	SET_NETDEV_DEV(&bgx->lmac[bgx->lmac_count].netdev, dev);

	bgx->lmac[bgx->lmac_count].lmacid = bgx->lmac_count;
out:
	bgx->lmac_count++;
	return AE_OK;
}

static acpi_status bgx_acpi_match_id(acpi_handle handle, u32 lvl,
				     void *context, void **ret_val)
{
	struct acpi_buffer string = { ACPI_ALLOCATE_BUFFER, NULL };
	struct bgx *bgx = context;
	char bgx_sel[5];

	snprintf(bgx_sel, 5, "BGX%d", bgx->bgx_id);
	if (ACPI_FAILURE(acpi_get_name(handle, ACPI_SINGLE_NAME, &string))) {
		pr_warn("Invalid link device\n");
		return AE_OK;
	}

	if (strncmp(string.pointer, bgx_sel, 4))
		return AE_OK;

	acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
			    bgx_acpi_register_phy, NULL, bgx, NULL);

	kfree(string.pointer);
	return AE_CTRL_TERMINATE;
}

static int bgx_init_acpi_phy(struct bgx *bgx)
{
	acpi_get_devices(NULL, bgx_acpi_match_id, bgx, (void **)NULL);
	return 0;
}

#else

static int bgx_init_acpi_phy(struct bgx *bgx)
{
	return -ENODEV;
}

#endif /* CONFIG_ACPI */

#if IS_ENABLED(CONFIG_OF_MDIO)

static int bgx_init_of_phy(struct bgx *bgx)
{
	struct fwnode_handle *fwn;
	u8 lmac = 0;
	const char *mac;

	device_for_each_child_node(&bgx->pdev->dev, fwn) {
		struct phy_device *pd;
		struct device_node *phy_np;
		struct device_node *node = to_of_node(fwn);

		/* Should always be an OF node.  But if it is not, we
		 * cannot handle it, so exit the loop.
		 */
		if (!node)
			break;

		mac = of_get_mac_address(node);
		if (mac)
			ether_addr_copy(bgx->lmac[lmac].mac, mac);

		SET_NETDEV_DEV(&bgx->lmac[lmac].netdev, &bgx->pdev->dev);
		bgx->lmac[lmac].lmacid = lmac;

		phy_np = of_parse_phandle(node, "phy-handle", 0);
		/* If there is no phy or defective firmware presents
		 * this cortina phy, for which there is no driver
		 * support, ignore it.
		 */
		if (phy_np &&
		    !of_device_is_compatible(phy_np, "cortina,cs4223-slice")) {
			/* Wait until the phy drivers are available */
			pd = of_phy_find_device(phy_np);
			if (!pd)
				return -EPROBE_DEFER;
			bgx->lmac[lmac].phydev = pd;
		}

		lmac++;
		if (lmac == MAX_LMAC_PER_BGX) {
			of_node_put(node);
			break;
		}
	}
	return 0;
}

#else

static int bgx_init_of_phy(struct bgx *bgx)
{
	return -ENODEV;
}

#endif /* CONFIG_OF_MDIO */

static int bgx_init_phy(struct bgx *bgx)
{
	if (!acpi_disabled)
		return bgx_init_acpi_phy(bgx);

	return bgx_init_of_phy(bgx);
}

static int bgx_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err;
	struct device *dev = &pdev->dev;
	struct bgx *bgx = NULL;
	u8 lmac;

	bgx = devm_kzalloc(dev, sizeof(*bgx), GFP_KERNEL);
	if (!bgx)
		return -ENOMEM;
	bgx->pdev = pdev;

	pci_set_drvdata(pdev, bgx);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto err_disable_device;
	}

	/* MAP configuration registers */
	bgx->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!bgx->reg_base) {
		dev_err(dev, "BGX: Cannot map CSR memory space, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}
	bgx->bgx_id = (pci_resource_start(pdev, PCI_CFG_REG_BAR_NUM) >> 24) & 1;
	bgx->bgx_id += nic_get_node_id(pdev) * MAX_BGX_PER_CN88XX;

	bgx_vnic[bgx->bgx_id] = bgx;
	bgx_get_qlm_mode(bgx);

	err = bgx_init_phy(bgx);
	if (err)
		goto err_enable;

	bgx_init_hw(bgx);

	/* Enable all LMACs */
	for (lmac = 0; lmac < bgx->lmac_count; lmac++) {
		err = bgx_lmac_enable(bgx, lmac);
		if (err) {
			dev_err(dev, "BGX%d failed to enable lmac%d\n",
				bgx->bgx_id, lmac);
			goto err_enable;
		}
	}

	return 0;

err_enable:
	bgx_vnic[bgx->bgx_id] = NULL;
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void bgx_remove(struct pci_dev *pdev)
{
	struct bgx *bgx = pci_get_drvdata(pdev);
	u8 lmac;

	/* Disable all LMACs */
	for (lmac = 0; lmac < bgx->lmac_count; lmac++)
		bgx_lmac_disable(bgx, lmac);

	bgx_vnic[bgx->bgx_id] = NULL;
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver bgx_driver = {
	.name = DRV_NAME,
	.id_table = bgx_id_table,
	.probe = bgx_probe,
	.remove = bgx_remove,
};

static int __init bgx_init_module(void)
{
	pr_info("%s, ver %s\n", DRV_NAME, DRV_VERSION);

	return pci_register_driver(&bgx_driver);
}

static void __exit bgx_cleanup_module(void)
{
	pci_unregister_driver(&bgx_driver);
}

module_init(bgx_init_module);
module_exit(bgx_cleanup_module);
