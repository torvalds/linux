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

#define DRV_NAME	"thunder_bgx"
#define DRV_VERSION	"1.0"

struct lmac {
	struct bgx		*bgx;
	int			dmac;
	u8			mac[ETH_ALEN];
	u8                      lmac_type;
	u8                      lane_to_sds;
	bool                    use_training;
	bool                    autoneg;
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
	struct	lmac		lmac[MAX_LMAC_PER_BGX];
	u8			lmac_count;
	u8			max_lmac;
	u8                      acpi_lmac_idx;
	void __iomem		*reg_base;
	struct pci_dev		*pdev;
	bool                    is_dlm;
	bool                    is_rgx;
};

static struct bgx *bgx_vnic[MAX_BGX_THUNDER];
static int lmac_count; /* Total no of LMACs in system */

static int bgx_xaui_check_link(struct lmac *lmac);

/* Supported devices */
static const struct pci_device_id bgx_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_BGX) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVICE_ID_THUNDER_RGX) },
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

static int max_bgx_per_node;
static void set_max_bgx_per_node(struct pci_dev *pdev)
{
	u16 sdevid;

	if (max_bgx_per_node)
		return;

	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &sdevid);
	switch (sdevid) {
	case PCI_SUBSYS_DEVID_81XX_BGX:
	case PCI_SUBSYS_DEVID_81XX_RGX:
		max_bgx_per_node = MAX_BGX_PER_CN81XX;
		break;
	case PCI_SUBSYS_DEVID_83XX_BGX:
		max_bgx_per_node = MAX_BGX_PER_CN83XX;
		break;
	case PCI_SUBSYS_DEVID_88XX_BGX:
	default:
		max_bgx_per_node = MAX_BGX_PER_CN88XX;
		break;
	}
}

static struct bgx *get_bgx(int node, int bgx_idx)
{
	int idx = (node * max_bgx_per_node) + bgx_idx;

	return bgx_vnic[idx];
}

/* Return number of BGX present in HW */
unsigned bgx_get_map(int node)
{
	int i;
	unsigned map = 0;

	for (i = 0; i < max_bgx_per_node; i++) {
		if (bgx_vnic[(node * max_bgx_per_node) + i])
			map |= (1 << i);
	}

	return map;
}
EXPORT_SYMBOL(bgx_get_map);

/* Return number of LMAC configured for this BGX */
int bgx_get_lmac_count(int node, int bgx_idx)
{
	struct bgx *bgx;

	bgx = get_bgx(node, bgx_idx);
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

	bgx = get_bgx(node, bgx_idx);
	if (!bgx)
		return;

	lmac = &bgx->lmac[lmacid];
	link->mac_type = lmac->lmac_type;
	link->link_up = lmac->link_up;
	link->duplex = lmac->last_duplex;
	link->speed = lmac->last_speed;
}
EXPORT_SYMBOL(bgx_get_lmac_link_state);

const u8 *bgx_get_lmac_mac(int node, int bgx_idx, int lmacid)
{
	struct bgx *bgx = get_bgx(node, bgx_idx);

	if (bgx)
		return bgx->lmac[lmacid].mac;

	return NULL;
}
EXPORT_SYMBOL(bgx_get_lmac_mac);

void bgx_set_lmac_mac(int node, int bgx_idx, int lmacid, const u8 *mac)
{
	struct bgx *bgx = get_bgx(node, bgx_idx);

	if (!bgx)
		return;

	ether_addr_copy(bgx->lmac[lmacid].mac, mac);
}
EXPORT_SYMBOL(bgx_set_lmac_mac);

void bgx_lmac_rx_tx_enable(int node, int bgx_idx, int lmacid, bool enable)
{
	struct bgx *bgx = get_bgx(node, bgx_idx);
	struct lmac *lmac;
	u64 cfg;

	if (!bgx)
		return;
	lmac = &bgx->lmac[lmacid];

	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	if (enable)
		cfg |= CMR_PKT_RX_EN | CMR_PKT_TX_EN;
	else
		cfg &= ~(CMR_PKT_RX_EN | CMR_PKT_TX_EN);
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);

	if (bgx->is_rgx)
		xcv_setup_link(enable ? lmac->link_up : 0, lmac->last_speed);
}
EXPORT_SYMBOL(bgx_lmac_rx_tx_enable);

/* Enables or disables timestamp insertion by BGX for Rx packets */
void bgx_config_timestamping(int node, int bgx_idx, int lmacid, bool enable)
{
	struct bgx *bgx = get_bgx(node, bgx_idx);
	struct lmac *lmac;
	u64 csr_offset, cfg;

	if (!bgx)
		return;

	lmac = &bgx->lmac[lmacid];

	if (lmac->lmac_type == BGX_MODE_SGMII ||
	    lmac->lmac_type == BGX_MODE_QSGMII ||
	    lmac->lmac_type == BGX_MODE_RGMII)
		csr_offset = BGX_GMP_GMI_RXX_FRM_CTL;
	else
		csr_offset = BGX_SMUX_RX_FRM_CTL;

	cfg = bgx_reg_read(bgx, lmacid, csr_offset);

	if (enable)
		cfg |= BGX_PKT_RX_PTP_EN;
	else
		cfg &= ~BGX_PKT_RX_PTP_EN;
	bgx_reg_write(bgx, lmacid, csr_offset, cfg);
}
EXPORT_SYMBOL(bgx_config_timestamping);

void bgx_lmac_get_pfc(int node, int bgx_idx, int lmacid, void *pause)
{
	struct pfc *pfc = (struct pfc *)pause;
	struct bgx *bgx = get_bgx(node, bgx_idx);
	struct lmac *lmac;
	u64 cfg;

	if (!bgx)
		return;
	lmac = &bgx->lmac[lmacid];
	if (lmac->is_sgmii)
		return;

	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_CBFC_CTL);
	pfc->fc_rx = cfg & RX_EN;
	pfc->fc_tx = cfg & TX_EN;
	pfc->autoneg = 0;
}
EXPORT_SYMBOL(bgx_lmac_get_pfc);

void bgx_lmac_set_pfc(int node, int bgx_idx, int lmacid, void *pause)
{
	struct pfc *pfc = (struct pfc *)pause;
	struct bgx *bgx = get_bgx(node, bgx_idx);
	struct lmac *lmac;
	u64 cfg;

	if (!bgx)
		return;
	lmac = &bgx->lmac[lmacid];
	if (lmac->is_sgmii)
		return;

	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_CBFC_CTL);
	cfg &= ~(RX_EN | TX_EN);
	cfg |= (pfc->fc_rx ? RX_EN : 0x00);
	cfg |= (pfc->fc_tx ? TX_EN : 0x00);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_CBFC_CTL, cfg);
}
EXPORT_SYMBOL(bgx_lmac_set_pfc);

static void bgx_sgmii_change_link_state(struct lmac *lmac)
{
	struct bgx *bgx = lmac->bgx;
	u64 cmr_cfg;
	u64 port_cfg = 0;
	u64 misc_ctl = 0;
	bool tx_en, rx_en;

	cmr_cfg = bgx_reg_read(bgx, lmac->lmacid, BGX_CMRX_CFG);
	tx_en = cmr_cfg & CMR_PKT_TX_EN;
	rx_en = cmr_cfg & CMR_PKT_RX_EN;
	cmr_cfg &= ~(CMR_PKT_RX_EN | CMR_PKT_TX_EN);
	bgx_reg_write(bgx, lmac->lmacid, BGX_CMRX_CFG, cmr_cfg);

	/* Wait for BGX RX to be idle */
	if (bgx_poll_reg(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG,
			 GMI_PORT_CFG_RX_IDLE, false)) {
		dev_err(&bgx->pdev->dev, "BGX%d LMAC%d GMI RX not idle\n",
			bgx->bgx_id, lmac->lmacid);
		return;
	}

	/* Wait for BGX TX to be idle */
	if (bgx_poll_reg(bgx, lmac->lmacid, BGX_GMP_GMI_PRTX_CFG,
			 GMI_PORT_CFG_TX_IDLE, false)) {
		dev_err(&bgx->pdev->dev, "BGX%d LMAC%d GMI TX not idle\n",
			bgx->bgx_id, lmac->lmacid);
		return;
	}

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

	/* Restore CMR config settings */
	cmr_cfg |= (rx_en ? CMR_PKT_RX_EN : 0) | (tx_en ? CMR_PKT_TX_EN : 0);
	bgx_reg_write(bgx, lmac->lmacid, BGX_CMRX_CFG, cmr_cfg);

	if (bgx->is_rgx && (cmr_cfg & (CMR_PKT_RX_EN | CMR_PKT_TX_EN)))
		xcv_setup_link(lmac->link_up, lmac->last_speed);
}

static void bgx_lmac_handler(struct net_device *netdev)
{
	struct lmac *lmac = container_of(netdev, struct lmac, netdev);
	struct phy_device *phydev;
	int link_changed = 0;

	if (!lmac)
		return;

	phydev = lmac->phydev;

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

	bgx = get_bgx(node, bgx_idx);
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

	bgx = get_bgx(node, bgx_idx);
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

	bgx = get_bgx(node, bgx_idx);
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

static int bgx_lmac_sgmii_init(struct bgx *bgx, struct lmac *lmac)
{
	int lmacid = lmac->lmacid;
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
	cfg |= PCS_MRX_CTL_RST_AN;
	if (lmac->phydev) {
		cfg |= PCS_MRX_CTL_AN_EN;
	} else {
		/* In scenarios where PHY driver is not present or it's a
		 * non-standard PHY, FW sets AN_EN to inform Linux driver
		 * to do auto-neg and link polling or not.
		 */
		if (cfg & PCS_MRX_CTL_AN_EN)
			lmac->autoneg = true;
	}
	bgx_reg_write(bgx, lmacid, BGX_GMP_PCS_MRX_CTL, cfg);

	if (lmac->lmac_type == BGX_MODE_QSGMII) {
		/* Disable disparity check for QSGMII */
		cfg = bgx_reg_read(bgx, lmacid, BGX_GMP_PCS_MISCX_CTL);
		cfg &= ~PCS_MISC_CTL_DISP_EN;
		bgx_reg_write(bgx, lmacid, BGX_GMP_PCS_MISCX_CTL, cfg);
		return 0;
	}

	if ((lmac->lmac_type == BGX_MODE_SGMII) && lmac->phydev) {
		if (bgx_poll_reg(bgx, lmacid, BGX_GMP_PCS_MRX_STATUS,
				 PCS_MRX_STATUS_AN_CPT, false)) {
			dev_err(&bgx->pdev->dev, "BGX AN_CPT not completed\n");
			return -1;
		}
	}

	return 0;
}

static int bgx_lmac_xaui_init(struct bgx *bgx, struct lmac *lmac)
{
	u64 cfg;
	int lmacid = lmac->lmacid;

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
	if (lmac->lmac_type == BGX_MODE_RXAUI)
		bgx_reg_modify(bgx, lmacid, BGX_SPUX_MISC_CONTROL,
			       SPU_MISC_CTL_INTLV_RDISP);

	/* Clear receive packet disable */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_MISC_CONTROL);
	cfg &= ~SPU_MISC_CTL_RX_DIS;
	bgx_reg_write(bgx, lmacid, BGX_SPUX_MISC_CONTROL, cfg);

	/* clear all interrupts */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_RX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_RX_INT, cfg);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_INT, cfg);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SPUX_INT);
	bgx_reg_write(bgx, lmacid, BGX_SPUX_INT, cfg);

	if (lmac->use_training) {
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
	if (lmac->lmac_type == BGX_MODE_10G_KR)
		cfg |= (1 << 23);
	else if (lmac->lmac_type == BGX_MODE_40G_KR)
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

	/* Enable receive and transmission of pause frames */
	bgx_reg_write(bgx, lmacid, BGX_SMUX_CBFC_CTL, ((0xffffULL << 32) |
		      BCK_EN | DRP_EN | TX_EN | RX_EN));
	/* Configure pause time and interval */
	bgx_reg_write(bgx, lmacid,
		      BGX_SMUX_TX_PAUSE_PKT_TIME, DEFAULT_PAUSE_TIME);
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_TX_PAUSE_PKT_INTERVAL);
	cfg &= ~0xFFFFull;
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_PAUSE_PKT_INTERVAL,
		      cfg | (DEFAULT_PAUSE_TIME - 0x1000));
	bgx_reg_write(bgx, lmacid, BGX_SMUX_TX_PAUSE_ZERO, 0x01);

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
	int lmac_type = lmac->lmac_type;
	u64 cfg;

	if (lmac->use_training) {
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
	if (bgx_reg_read(bgx, lmacid, BGX_SPUX_STATUS2) & SPU_STATUS2_RCVFLT)
		bgx_reg_modify(bgx, lmacid,
			       BGX_SPUX_STATUS2, SPU_STATUS2_RCVFLT);
	if (bgx_reg_read(bgx, lmacid, BGX_SPUX_STATUS2) & SPU_STATUS2_RCVFLT) {
		dev_err(&bgx->pdev->dev, "Receive fault, retry training\n");
		if (lmac->use_training) {
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

	/* Check for MAC RX faults */
	cfg = bgx_reg_read(bgx, lmacid, BGX_SMUX_RX_CTL);
	/* 0 - Link is okay, 1 - Local fault, 2 - Remote fault */
	cfg &= SMU_RX_CTL_STATUS;
	if (!cfg)
		return 0;

	/* Rx local/remote fault seen.
	 * Do lmac reinit to see if condition recovers
	 */
	bgx_lmac_xaui_init(bgx, lmac);

	return -1;
}

static void bgx_poll_for_sgmii_link(struct lmac *lmac)
{
	u64 pcs_link, an_result;
	u8 speed;

	pcs_link = bgx_reg_read(lmac->bgx, lmac->lmacid,
				BGX_GMP_PCS_MRX_STATUS);

	/*Link state bit is sticky, read it again*/
	if (!(pcs_link & PCS_MRX_STATUS_LINK))
		pcs_link = bgx_reg_read(lmac->bgx, lmac->lmacid,
					BGX_GMP_PCS_MRX_STATUS);

	if (bgx_poll_reg(lmac->bgx, lmac->lmacid, BGX_GMP_PCS_MRX_STATUS,
			 PCS_MRX_STATUS_AN_CPT, false)) {
		lmac->link_up = false;
		lmac->last_speed = SPEED_UNKNOWN;
		lmac->last_duplex = DUPLEX_UNKNOWN;
		goto next_poll;
	}

	lmac->link_up = ((pcs_link & PCS_MRX_STATUS_LINK) != 0) ? true : false;
	an_result = bgx_reg_read(lmac->bgx, lmac->lmacid,
				 BGX_GMP_PCS_ANX_AN_RESULTS);

	speed = (an_result >> 3) & 0x3;
	lmac->last_duplex = (an_result >> 1) & 0x1;
	switch (speed) {
	case 0:
		lmac->last_speed = 10;
		break;
	case 1:
		lmac->last_speed = 100;
		break;
	case 2:
		lmac->last_speed = 1000;
		break;
	default:
		lmac->link_up = false;
		lmac->last_speed = SPEED_UNKNOWN;
		lmac->last_duplex = DUPLEX_UNKNOWN;
		break;
	}

next_poll:

	if (lmac->last_link != lmac->link_up) {
		if (lmac->link_up)
			bgx_sgmii_change_link_state(lmac);
		lmac->last_link = lmac->link_up;
	}

	queue_delayed_work(lmac->check_link, &lmac->dwork, HZ * 3);
}

static void bgx_poll_for_link(struct work_struct *work)
{
	struct lmac *lmac;
	u64 spu_link, smu_link;

	lmac = container_of(work, struct lmac, dwork.work);
	if (lmac->is_sgmii) {
		bgx_poll_for_sgmii_link(lmac);
		return;
	}

	/* Receive link is latching low. Force it high and verify it */
	bgx_reg_modify(lmac->bgx, lmac->lmacid,
		       BGX_SPUX_STATUS1, SPU_STATUS1_RCV_LNK);
	bgx_poll_reg(lmac->bgx, lmac->lmacid, BGX_SPUX_STATUS1,
		     SPU_STATUS1_RCV_LNK, false);

	spu_link = bgx_reg_read(lmac->bgx, lmac->lmacid, BGX_SPUX_STATUS1);
	smu_link = bgx_reg_read(lmac->bgx, lmac->lmacid, BGX_SMUX_RX_CTL);

	if ((spu_link & SPU_STATUS1_RCV_LNK) &&
	    !(smu_link & SMU_RX_CTL_STATUS)) {
		lmac->link_up = 1;
		if (lmac->lmac_type == BGX_MODE_XLAUI)
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
		if (lmac->link_up) {
			if (bgx_xaui_check_link(lmac)) {
				/* Errors, clear link_up state */
				lmac->link_up = 0;
				lmac->last_speed = SPEED_UNKNOWN;
				lmac->last_duplex = DUPLEX_UNKNOWN;
			}
		}
		lmac->last_link = lmac->link_up;
	}

	queue_delayed_work(lmac->check_link, &lmac->dwork, HZ * 2);
}

static int phy_interface_mode(u8 lmac_type)
{
	if (lmac_type == BGX_MODE_QSGMII)
		return PHY_INTERFACE_MODE_QSGMII;
	if (lmac_type == BGX_MODE_RGMII)
		return PHY_INTERFACE_MODE_RGMII;

	return PHY_INTERFACE_MODE_SGMII;
}

static int bgx_lmac_enable(struct bgx *bgx, u8 lmacid)
{
	struct lmac *lmac;
	u64 cfg;

	lmac = &bgx->lmac[lmacid];
	lmac->bgx = bgx;

	if ((lmac->lmac_type == BGX_MODE_SGMII) ||
	    (lmac->lmac_type == BGX_MODE_QSGMII) ||
	    (lmac->lmac_type == BGX_MODE_RGMII)) {
		lmac->is_sgmii = 1;
		if (bgx_lmac_sgmii_init(bgx, lmac))
			return -1;
	} else {
		lmac->is_sgmii = 0;
		if (bgx_lmac_xaui_init(bgx, lmac))
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

	if ((lmac->lmac_type != BGX_MODE_XFI) &&
	    (lmac->lmac_type != BGX_MODE_XLAUI) &&
	    (lmac->lmac_type != BGX_MODE_40G_KR) &&
	    (lmac->lmac_type != BGX_MODE_10G_KR)) {
		if (!lmac->phydev) {
			if (lmac->autoneg) {
				bgx_reg_write(bgx, lmacid,
					      BGX_GMP_PCS_LINKX_TIMER,
					      PCS_LINKX_TIMER_COUNT);
				goto poll;
			} else {
				/* Default to below link speed and duplex */
				lmac->link_up = true;
				lmac->last_speed = 1000;
				lmac->last_duplex = 1;
				bgx_sgmii_change_link_state(lmac);
				return 0;
			}
		}
		lmac->phydev->dev_flags = 0;

		if (phy_connect_direct(&lmac->netdev, lmac->phydev,
				       bgx_lmac_handler,
				       phy_interface_mode(lmac->lmac_type)))
			return -ENODEV;

		phy_start_aneg(lmac->phydev);
		return 0;
	}

poll:
	lmac->check_link = alloc_workqueue("check_link", WQ_UNBOUND |
					   WQ_MEM_RECLAIM, 1);
	if (!lmac->check_link)
		return -ENOMEM;
	INIT_DELAYED_WORK(&lmac->dwork, bgx_poll_for_link);
	queue_delayed_work(lmac->check_link, &lmac->dwork, 0);

	return 0;
}

static void bgx_lmac_disable(struct bgx *bgx, u8 lmacid)
{
	struct lmac *lmac;
	u64 cfg;

	lmac = &bgx->lmac[lmacid];
	if (lmac->check_link) {
		/* Destroy work queue */
		cancel_delayed_work_sync(&lmac->dwork);
		destroy_workqueue(lmac->check_link);
	}

	/* Disable packet reception */
	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cfg &= ~CMR_PKT_RX_EN;
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);

	/* Give chance for Rx/Tx FIFO to get drained */
	bgx_poll_reg(bgx, lmacid, BGX_CMRX_RX_FIFO_LEN, (u64)0x1FFF, true);
	bgx_poll_reg(bgx, lmacid, BGX_CMRX_TX_FIFO_LEN, (u64)0x3FFF, true);

	/* Disable packet transmission */
	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cfg &= ~CMR_PKT_TX_EN;
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);

	/* Disable serdes lanes */
        if (!lmac->is_sgmii)
                bgx_reg_modify(bgx, lmacid,
                               BGX_SPUX_CONTROL1, SPU_CTL_LOW_POWER);
        else
                bgx_reg_modify(bgx, lmacid,
                               BGX_GMP_PCS_MRX_CTL, PCS_MRX_CTL_PWR_DN);

	/* Disable LMAC */
	cfg = bgx_reg_read(bgx, lmacid, BGX_CMRX_CFG);
	cfg &= ~CMR_EN;
	bgx_reg_write(bgx, lmacid, BGX_CMRX_CFG, cfg);

	bgx_flush_dmac_addrs(bgx, lmacid);

	if ((lmac->lmac_type != BGX_MODE_XFI) &&
	    (lmac->lmac_type != BGX_MODE_XLAUI) &&
	    (lmac->lmac_type != BGX_MODE_40G_KR) &&
	    (lmac->lmac_type != BGX_MODE_10G_KR) && lmac->phydev)
		phy_disconnect(lmac->phydev);

	lmac->phydev = NULL;
}

static void bgx_init_hw(struct bgx *bgx)
{
	int i;
	struct lmac *lmac;

	bgx_reg_modify(bgx, 0, BGX_CMR_GLOBAL_CFG, CMR_GLOBAL_CFG_FCS_STRIP);
	if (bgx_reg_read(bgx, 0, BGX_CMR_BIST_STATUS))
		dev_err(&bgx->pdev->dev, "BGX%d BIST failed\n", bgx->bgx_id);

	/* Set lmac type and lane2serdes mapping */
	for (i = 0; i < bgx->lmac_count; i++) {
		lmac = &bgx->lmac[i];
		bgx_reg_write(bgx, i, BGX_CMRX_CFG,
			      (lmac->lmac_type << 8) | lmac->lane_to_sds);
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

static u8 bgx_get_lane2sds_cfg(struct bgx *bgx, struct lmac *lmac)
{
	return (u8)(bgx_reg_read(bgx, lmac->lmacid, BGX_CMRX_CFG) & 0xFF);
}

static void bgx_print_qlm_mode(struct bgx *bgx, u8 lmacid)
{
	struct device *dev = &bgx->pdev->dev;
	struct lmac *lmac;
	char str[27];

	if (!bgx->is_dlm && lmacid)
		return;

	lmac = &bgx->lmac[lmacid];
	if (!bgx->is_dlm)
		sprintf(str, "BGX%d QLM mode", bgx->bgx_id);
	else
		sprintf(str, "BGX%d LMAC%d mode", bgx->bgx_id, lmacid);

	switch (lmac->lmac_type) {
	case BGX_MODE_SGMII:
		dev_info(dev, "%s: SGMII\n", (char *)str);
		break;
	case BGX_MODE_XAUI:
		dev_info(dev, "%s: XAUI\n", (char *)str);
		break;
	case BGX_MODE_RXAUI:
		dev_info(dev, "%s: RXAUI\n", (char *)str);
		break;
	case BGX_MODE_XFI:
		if (!lmac->use_training)
			dev_info(dev, "%s: XFI\n", (char *)str);
		else
			dev_info(dev, "%s: 10G_KR\n", (char *)str);
		break;
	case BGX_MODE_XLAUI:
		if (!lmac->use_training)
			dev_info(dev, "%s: XLAUI\n", (char *)str);
		else
			dev_info(dev, "%s: 40G_KR4\n", (char *)str);
		break;
	case BGX_MODE_QSGMII:
		dev_info(dev, "%s: QSGMII\n", (char *)str);
		break;
	case BGX_MODE_RGMII:
		dev_info(dev, "%s: RGMII\n", (char *)str);
		break;
	case BGX_MODE_INVALID:
		/* Nothing to do */
		break;
	}
}

static void lmac_set_lane2sds(struct bgx *bgx, struct lmac *lmac)
{
	switch (lmac->lmac_type) {
	case BGX_MODE_SGMII:
	case BGX_MODE_XFI:
		lmac->lane_to_sds = lmac->lmacid;
		break;
	case BGX_MODE_XAUI:
	case BGX_MODE_XLAUI:
	case BGX_MODE_RGMII:
		lmac->lane_to_sds = 0xE4;
		break;
	case BGX_MODE_RXAUI:
		lmac->lane_to_sds = (lmac->lmacid) ? 0xE : 0x4;
		break;
	case BGX_MODE_QSGMII:
		/* There is no way to determine if DLM0/2 is QSGMII or
		 * DLM1/3 is configured to QSGMII as bootloader will
		 * configure all LMACs, so take whatever is configured
		 * by low level firmware.
		 */
		lmac->lane_to_sds = bgx_get_lane2sds_cfg(bgx, lmac);
		break;
	default:
		lmac->lane_to_sds = 0;
		break;
	}
}

static void lmac_set_training(struct bgx *bgx, struct lmac *lmac, int lmacid)
{
	if ((lmac->lmac_type != BGX_MODE_10G_KR) &&
	    (lmac->lmac_type != BGX_MODE_40G_KR)) {
		lmac->use_training = 0;
		return;
	}

	lmac->use_training = bgx_reg_read(bgx, lmacid, BGX_SPUX_BR_PMD_CRTL) &
							SPU_PMD_CRTL_TRAIN_EN;
}

static void bgx_set_lmac_config(struct bgx *bgx, u8 idx)
{
	struct lmac *lmac;
	u64 cmr_cfg;
	u8 lmac_type;
	u8 lane_to_sds;

	lmac = &bgx->lmac[idx];

	if (!bgx->is_dlm || bgx->is_rgx) {
		/* Read LMAC0 type to figure out QLM mode
		 * This is configured by low level firmware
		 */
		cmr_cfg = bgx_reg_read(bgx, 0, BGX_CMRX_CFG);
		lmac->lmac_type = (cmr_cfg >> 8) & 0x07;
		if (bgx->is_rgx)
			lmac->lmac_type = BGX_MODE_RGMII;
		lmac_set_training(bgx, lmac, 0);
		lmac_set_lane2sds(bgx, lmac);
		return;
	}

	/* For DLMs or SLMs on 80/81/83xx so many lane configurations
	 * are possible and vary across boards. Also Kernel doesn't have
	 * any way to identify board type/info and since firmware does,
	 * just take lmac type and serdes lane config as is.
	 */
	cmr_cfg = bgx_reg_read(bgx, idx, BGX_CMRX_CFG);
	lmac_type = (u8)((cmr_cfg >> 8) & 0x07);
	lane_to_sds = (u8)(cmr_cfg & 0xFF);
	/* Check if config is reset value */
	if ((lmac_type == 0) && (lane_to_sds == 0xE4))
		lmac->lmac_type = BGX_MODE_INVALID;
	else
		lmac->lmac_type = lmac_type;
	lmac->lane_to_sds = lane_to_sds;
	lmac_set_training(bgx, lmac, lmac->lmacid);
}

static void bgx_get_qlm_mode(struct bgx *bgx)
{
	struct lmac *lmac;
	u8  idx;

	/* Init all LMAC's type to invalid */
	for (idx = 0; idx < bgx->max_lmac; idx++) {
		lmac = &bgx->lmac[idx];
		lmac->lmacid = idx;
		lmac->lmac_type = BGX_MODE_INVALID;
		lmac->use_training = false;
	}

	/* It is assumed that low level firmware sets this value */
	bgx->lmac_count = bgx_reg_read(bgx, 0, BGX_CMR_RX_LMACS) & 0x7;
	if (bgx->lmac_count > bgx->max_lmac)
		bgx->lmac_count = bgx->max_lmac;

	for (idx = 0; idx < bgx->lmac_count; idx++) {
		bgx_set_lmac_config(bgx, idx);
		bgx_print_qlm_mode(bgx, idx);
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

	acpi_get_mac_address(dev, adev, bgx->lmac[bgx->acpi_lmac_idx].mac);

	SET_NETDEV_DEV(&bgx->lmac[bgx->acpi_lmac_idx].netdev, dev);

	bgx->lmac[bgx->acpi_lmac_idx].lmacid = bgx->acpi_lmac_idx;
	bgx->acpi_lmac_idx++; /* move to next LMAC */
out:
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
	struct device_node *node = NULL;
	u8 lmac = 0;

	device_for_each_child_node(&bgx->pdev->dev, fwn) {
		struct phy_device *pd;
		struct device_node *phy_np;
		const char *mac;

		/* Should always be an OF node.  But if it is not, we
		 * cannot handle it, so exit the loop.
		 */
		node = to_of_node(fwn);
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
				goto defer;
			bgx->lmac[lmac].phydev = pd;
		}

		lmac++;
		if (lmac == bgx->max_lmac) {
			of_node_put(node);
			break;
		}
	}
	return 0;

defer:
	/* We are bailing out, try not to leak device reference counts
	 * for phy devices we may have already found.
	 */
	while (lmac) {
		if (bgx->lmac[lmac].phydev) {
			put_device(&bgx->lmac[lmac].phydev->mdio.dev);
			bgx->lmac[lmac].phydev = NULL;
		}
		lmac--;
	}
	of_node_put(node);
	return -EPROBE_DEFER;
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
	u16 sdevid;

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

	set_max_bgx_per_node(pdev);

	pci_read_config_word(pdev, PCI_DEVICE_ID, &sdevid);
	if (sdevid != PCI_DEVICE_ID_THUNDER_RGX) {
		bgx->bgx_id = (pci_resource_start(pdev,
			PCI_CFG_REG_BAR_NUM) >> 24) & BGX_ID_MASK;
		bgx->bgx_id += nic_get_node_id(pdev) * max_bgx_per_node;
		bgx->max_lmac = MAX_LMAC_PER_BGX;
		bgx_vnic[bgx->bgx_id] = bgx;
	} else {
		bgx->is_rgx = true;
		bgx->max_lmac = 1;
		bgx->bgx_id = MAX_BGX_PER_CN81XX - 1;
		bgx_vnic[bgx->bgx_id] = bgx;
		xcv_init_hw();
	}

	/* On 81xx all are DLMs and on 83xx there are 3 BGX QLMs and one
	 * BGX i.e BGX2 can be split across 2 DLMs.
	 */
	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &sdevid);
	if ((sdevid == PCI_SUBSYS_DEVID_81XX_BGX) ||
	    ((sdevid == PCI_SUBSYS_DEVID_83XX_BGX) && (bgx->bgx_id == 2)))
		bgx->is_dlm = true;

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
			while (lmac)
				bgx_lmac_disable(bgx, --lmac);
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
