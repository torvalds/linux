// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2018 Marvell.
 *
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>

#include "cgx.h"
#include "rvu.h"
#include "lmac_common.h"

#define DRV_NAME	"Marvell-CGX/RPM"
#define DRV_STRING      "Marvell CGX/RPM Driver"

static LIST_HEAD(cgx_list);

/* Convert firmware speed encoding to user format(Mbps) */
static const u32 cgx_speed_mbps[CGX_LINK_SPEED_MAX] = {
	[CGX_LINK_NONE] = 0,
	[CGX_LINK_10M] = 10,
	[CGX_LINK_100M] = 100,
	[CGX_LINK_1G] = 1000,
	[CGX_LINK_2HG] = 2500,
	[CGX_LINK_5G] = 5000,
	[CGX_LINK_10G] = 10000,
	[CGX_LINK_20G] = 20000,
	[CGX_LINK_25G] = 25000,
	[CGX_LINK_40G] = 40000,
	[CGX_LINK_50G] = 50000,
	[CGX_LINK_80G] = 80000,
	[CGX_LINK_100G] = 100000,
};

/* Convert firmware lmac type encoding to string */
static const char *cgx_lmactype_string[LMAC_MODE_MAX] = {
	[LMAC_MODE_SGMII] = "SGMII",
	[LMAC_MODE_XAUI] = "XAUI",
	[LMAC_MODE_RXAUI] = "RXAUI",
	[LMAC_MODE_10G_R] = "10G_R",
	[LMAC_MODE_40G_R] = "40G_R",
	[LMAC_MODE_QSGMII] = "QSGMII",
	[LMAC_MODE_25G_R] = "25G_R",
	[LMAC_MODE_50G_R] = "50G_R",
	[LMAC_MODE_100G_R] = "100G_R",
	[LMAC_MODE_USXGMII] = "USXGMII",
	[LMAC_MODE_USGMII] = "USGMII",
};

/* CGX PHY management internal APIs */
static int cgx_fwi_link_change(struct cgx *cgx, int lmac_id, bool en);

/* Supported devices */
static const struct pci_device_id cgx_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_CGX) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CN10K_RPM) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CN10KB_RPM) },
	{ 0, }  /* end of table */
};

MODULE_DEVICE_TABLE(pci, cgx_id_table);

static bool is_dev_rpm(void *cgxd)
{
	struct cgx *cgx = cgxd;

	return (cgx->pdev->device == PCI_DEVID_CN10K_RPM) ||
	       (cgx->pdev->device == PCI_DEVID_CN10KB_RPM);
}

bool is_lmac_valid(struct cgx *cgx, int lmac_id)
{
	if (!cgx || lmac_id < 0 || lmac_id >= cgx->max_lmac_per_mac)
		return false;
	return test_bit(lmac_id, &cgx->lmac_bmap);
}

/* Helper function to get sequential index
 * given the enabled LMAC of a CGX
 */
static int get_sequence_id_of_lmac(struct cgx *cgx, int lmac_id)
{
	int tmp, id = 0;

	for_each_set_bit(tmp, &cgx->lmac_bmap, cgx->max_lmac_per_mac) {
		if (tmp == lmac_id)
			break;
		id++;
	}

	return id;
}

struct mac_ops *get_mac_ops(void *cgxd)
{
	if (!cgxd)
		return cgxd;

	return ((struct cgx *)cgxd)->mac_ops;
}

void cgx_write(struct cgx *cgx, u64 lmac, u64 offset, u64 val)
{
	writeq(val, cgx->reg_base + (lmac << cgx->mac_ops->lmac_offset) +
	       offset);
}

u64 cgx_read(struct cgx *cgx, u64 lmac, u64 offset)
{
	return readq(cgx->reg_base + (lmac << cgx->mac_ops->lmac_offset) +
		     offset);
}

struct lmac *lmac_pdata(u8 lmac_id, struct cgx *cgx)
{
	if (!cgx || lmac_id >= cgx->max_lmac_per_mac)
		return NULL;

	return cgx->lmac_idmap[lmac_id];
}

int cgx_get_cgxcnt_max(void)
{
	struct cgx *cgx_dev;
	int idmax = -ENODEV;

	list_for_each_entry(cgx_dev, &cgx_list, cgx_list)
		if (cgx_dev->cgx_id > idmax)
			idmax = cgx_dev->cgx_id;

	if (idmax < 0)
		return 0;

	return idmax + 1;
}

int cgx_get_lmac_cnt(void *cgxd)
{
	struct cgx *cgx = cgxd;

	if (!cgx)
		return -ENODEV;

	return cgx->lmac_count;
}

void *cgx_get_pdata(int cgx_id)
{
	struct cgx *cgx_dev;

	list_for_each_entry(cgx_dev, &cgx_list, cgx_list) {
		if (cgx_dev->cgx_id == cgx_id)
			return cgx_dev;
	}
	return NULL;
}

void cgx_lmac_write(int cgx_id, int lmac_id, u64 offset, u64 val)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);

	/* Software must not access disabled LMAC registers */
	if (!is_lmac_valid(cgx_dev, lmac_id))
		return;
	cgx_write(cgx_dev, lmac_id, offset, val);
}

u64 cgx_lmac_read(int cgx_id, int lmac_id, u64 offset)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);

	/* Software must not access disabled LMAC registers */
	if (!is_lmac_valid(cgx_dev, lmac_id))
		return 0;

	return cgx_read(cgx_dev, lmac_id, offset);
}

int cgx_get_cgxid(void *cgxd)
{
	struct cgx *cgx = cgxd;

	if (!cgx)
		return -EINVAL;

	return cgx->cgx_id;
}

u8 cgx_lmac_get_p2x(int cgx_id, int lmac_id)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	u64 cfg;

	cfg = cgx_read(cgx_dev, lmac_id, CGXX_CMRX_CFG);

	return (cfg & CMR_P2X_SEL_MASK) >> CMR_P2X_SEL_SHIFT;
}

/* Ensure the required lock for event queue(where asynchronous events are
 * posted) is acquired before calling this API. Else an asynchronous event(with
 * latest link status) can reach the destination before this function returns
 * and could make the link status appear wrong.
 */
int cgx_get_link_info(void *cgxd, int lmac_id,
		      struct cgx_link_user_info *linfo)
{
	struct lmac *lmac = lmac_pdata(lmac_id, cgxd);

	if (!lmac)
		return -ENODEV;

	*linfo = lmac->link_info;
	return 0;
}

int cgx_lmac_addr_set(u8 cgx_id, u8 lmac_id, u8 *mac_addr)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx_dev);
	struct mac_ops *mac_ops;
	int index, id;
	u64 cfg;

	if (!lmac)
		return -ENODEV;

	/* access mac_ops to know csr_offset */
	mac_ops = cgx_dev->mac_ops;

	/* copy 6bytes from macaddr */
	/* memcpy(&cfg, mac_addr, 6); */

	cfg = ether_addr_to_u64(mac_addr);

	id = get_sequence_id_of_lmac(cgx_dev, lmac_id);

	index = id * lmac->mac_to_index_bmap.max;

	cgx_write(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)),
		  cfg | CGX_DMAC_CAM_ADDR_ENABLE | ((u64)lmac_id << 49));

	cfg = cgx_read(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
	cfg |= (CGX_DMAC_CTL0_CAM_ENABLE | CGX_DMAC_BCAST_MODE |
		CGX_DMAC_MCAST_MODE);
	cgx_write(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0, cfg);

	return 0;
}

u64 cgx_read_dmac_ctrl(void *cgxd, int lmac_id)
{
	struct mac_ops *mac_ops;
	struct cgx *cgx = cgxd;

	if (!cgxd || !is_lmac_valid(cgxd, lmac_id))
		return 0;

	cgx = cgxd;
	/* Get mac_ops to know csr offset */
	mac_ops = cgx->mac_ops;

	return cgx_read(cgxd, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
}

u64 cgx_read_dmac_entry(void *cgxd, int index)
{
	struct mac_ops *mac_ops;
	struct cgx *cgx;

	if (!cgxd)
		return 0;

	cgx = cgxd;
	mac_ops = cgx->mac_ops;
	return cgx_read(cgx, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 8)));
}

int cgx_lmac_addr_add(u8 cgx_id, u8 lmac_id, u8 *mac_addr)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx_dev);
	struct mac_ops *mac_ops;
	int index, idx;
	u64 cfg = 0;
	int id;

	if (!lmac)
		return -ENODEV;

	mac_ops = cgx_dev->mac_ops;
	/* Get available index where entry is to be installed */
	idx = rvu_alloc_rsrc(&lmac->mac_to_index_bmap);
	if (idx < 0)
		return idx;

	id = get_sequence_id_of_lmac(cgx_dev, lmac_id);

	index = id * lmac->mac_to_index_bmap.max + idx;

	cfg = ether_addr_to_u64(mac_addr);
	cfg |= CGX_DMAC_CAM_ADDR_ENABLE;
	cfg |= ((u64)lmac_id << 49);
	cgx_write(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)), cfg);

	cfg = cgx_read(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
	cfg |= (CGX_DMAC_BCAST_MODE | CGX_DMAC_CAM_ACCEPT);

	if (is_multicast_ether_addr(mac_addr)) {
		cfg &= ~GENMASK_ULL(2, 1);
		cfg |= CGX_DMAC_MCAST_MODE_CAM;
		lmac->mcast_filters_count++;
	} else if (!lmac->mcast_filters_count) {
		cfg |= CGX_DMAC_MCAST_MODE;
	}

	cgx_write(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0, cfg);

	return idx;
}

int cgx_lmac_addr_reset(u8 cgx_id, u8 lmac_id)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx_dev);
	struct mac_ops *mac_ops;
	u8 index = 0, id;
	u64 cfg;

	if (!lmac)
		return -ENODEV;

	mac_ops = cgx_dev->mac_ops;
	/* Restore index 0 to its default init value as done during
	 * cgx_lmac_init
	 */
	set_bit(0, lmac->mac_to_index_bmap.bmap);

	id = get_sequence_id_of_lmac(cgx_dev, lmac_id);

	index = id * lmac->mac_to_index_bmap.max + index;
	cgx_write(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)), 0);

	/* Reset CGXX_CMRX_RX_DMAC_CTL0 register to default state */
	cfg = cgx_read(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
	cfg &= ~CGX_DMAC_CAM_ACCEPT;
	cfg |= (CGX_DMAC_BCAST_MODE | CGX_DMAC_MCAST_MODE);
	cgx_write(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0, cfg);

	return 0;
}

/* Allows caller to change macaddress associated with index
 * in dmac filter table including index 0 reserved for
 * interface mac address
 */
int cgx_lmac_addr_update(u8 cgx_id, u8 lmac_id, u8 *mac_addr, u8 index)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct mac_ops *mac_ops;
	struct lmac *lmac;
	u64 cfg;
	int id;

	lmac = lmac_pdata(lmac_id, cgx_dev);
	if (!lmac)
		return -ENODEV;

	mac_ops = cgx_dev->mac_ops;
	/* Validate the index */
	if (index >= lmac->mac_to_index_bmap.max)
		return -EINVAL;

	/* ensure index is already set */
	if (!test_bit(index, lmac->mac_to_index_bmap.bmap))
		return -EINVAL;

	id = get_sequence_id_of_lmac(cgx_dev, lmac_id);

	index = id * lmac->mac_to_index_bmap.max + index;

	cfg = cgx_read(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)));
	cfg &= ~CGX_RX_DMAC_ADR_MASK;
	cfg |= ether_addr_to_u64(mac_addr);

	cgx_write(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)), cfg);
	return 0;
}

int cgx_lmac_addr_del(u8 cgx_id, u8 lmac_id, u8 index)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx_dev);
	struct mac_ops *mac_ops;
	u8 mac[ETH_ALEN];
	u64 cfg;
	int id;

	if (!lmac)
		return -ENODEV;

	mac_ops = cgx_dev->mac_ops;
	/* Validate the index */
	if (index >= lmac->mac_to_index_bmap.max)
		return -EINVAL;

	/* Skip deletion for reserved index i.e. index 0 */
	if (index == 0)
		return 0;

	rvu_free_rsrc(&lmac->mac_to_index_bmap, index);

	id = get_sequence_id_of_lmac(cgx_dev, lmac_id);

	index = id * lmac->mac_to_index_bmap.max + index;

	/* Read MAC address to check whether it is ucast or mcast */
	cfg = cgx_read(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)));

	u64_to_ether_addr(cfg, mac);
	if (is_multicast_ether_addr(mac))
		lmac->mcast_filters_count--;

	if (!lmac->mcast_filters_count) {
		cfg = cgx_read(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
		cfg &= ~GENMASK_ULL(2, 1);
		cfg |= CGX_DMAC_MCAST_MODE;
		cgx_write(cgx_dev, lmac_id, CGXX_CMRX_RX_DMAC_CTL0, cfg);
	}

	cgx_write(cgx_dev, 0, (CGXX_CMRX_RX_DMAC_CAM0 + (index * 0x8)), 0);

	return 0;
}

int cgx_lmac_addr_max_entries_get(u8 cgx_id, u8 lmac_id)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx_dev);

	if (lmac)
		return lmac->mac_to_index_bmap.max;

	return 0;
}

u64 cgx_lmac_addr_get(u8 cgx_id, u8 lmac_id)
{
	struct cgx *cgx_dev = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx_dev);
	struct mac_ops *mac_ops;
	int index;
	u64 cfg;
	int id;

	mac_ops = cgx_dev->mac_ops;

	id = get_sequence_id_of_lmac(cgx_dev, lmac_id);

	index = id * lmac->mac_to_index_bmap.max;

	cfg = cgx_read(cgx_dev, 0, CGXX_CMRX_RX_DMAC_CAM0 + index * 0x8);
	return cfg & CGX_RX_DMAC_ADR_MASK;
}

int cgx_set_pkind(void *cgxd, u8 lmac_id, int pkind)
{
	struct cgx *cgx = cgxd;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	cgx_write(cgx, lmac_id, cgx->mac_ops->rxid_map_offset, (pkind & 0x3F));
	return 0;
}

static u8 cgx_get_lmac_type(void *cgxd, int lmac_id)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	cfg = cgx_read(cgx, lmac_id, CGXX_CMRX_CFG);
	return (cfg >> CGX_LMAC_TYPE_SHIFT) & CGX_LMAC_TYPE_MASK;
}

static u32 cgx_get_lmac_fifo_len(void *cgxd, int lmac_id)
{
	struct cgx *cgx = cgxd;
	u8 num_lmacs;
	u32 fifo_len;

	fifo_len = cgx->mac_ops->fifo_len;
	num_lmacs = cgx->mac_ops->get_nr_lmacs(cgx);

	switch (num_lmacs) {
	case 1:
		return fifo_len;
	case 2:
		return fifo_len / 2;
	case 3:
		/* LMAC0 gets half of the FIFO, reset 1/4th */
		if (lmac_id == 0)
			return fifo_len / 2;
		return fifo_len / 4;
	case 4:
	default:
		return fifo_len / 4;
	}
	return 0;
}

/* Configure CGX LMAC in internal loopback mode */
int cgx_lmac_internal_loopback(void *cgxd, int lmac_id, bool enable)
{
	struct cgx *cgx = cgxd;
	struct lmac *lmac;
	u64 cfg;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	lmac = lmac_pdata(lmac_id, cgx);
	if (lmac->lmac_type == LMAC_MODE_SGMII ||
	    lmac->lmac_type == LMAC_MODE_QSGMII) {
		cfg = cgx_read(cgx, lmac_id, CGXX_GMP_PCS_MRX_CTL);
		if (enable)
			cfg |= CGXX_GMP_PCS_MRX_CTL_LBK;
		else
			cfg &= ~CGXX_GMP_PCS_MRX_CTL_LBK;
		cgx_write(cgx, lmac_id, CGXX_GMP_PCS_MRX_CTL, cfg);
	} else {
		cfg = cgx_read(cgx, lmac_id, CGXX_SPUX_CONTROL1);
		if (enable)
			cfg |= CGXX_SPUX_CONTROL1_LBK;
		else
			cfg &= ~CGXX_SPUX_CONTROL1_LBK;
		cgx_write(cgx, lmac_id, CGXX_SPUX_CONTROL1, cfg);
	}
	return 0;
}

void cgx_lmac_promisc_config(int cgx_id, int lmac_id, bool enable)
{
	struct cgx *cgx = cgx_get_pdata(cgx_id);
	struct lmac *lmac = lmac_pdata(lmac_id, cgx);
	struct mac_ops *mac_ops;
	u16 max_dmac;
	int index, i;
	u64 cfg = 0;
	int id;

	if (!cgx || !lmac)
		return;

	max_dmac = lmac->mac_to_index_bmap.max;
	id = get_sequence_id_of_lmac(cgx, lmac_id);

	mac_ops = cgx->mac_ops;
	if (enable) {
		/* Enable promiscuous mode on LMAC */
		cfg = cgx_read(cgx, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
		cfg &= ~CGX_DMAC_CAM_ACCEPT;
		cfg |= (CGX_DMAC_BCAST_MODE | CGX_DMAC_MCAST_MODE);
		cgx_write(cgx, lmac_id, CGXX_CMRX_RX_DMAC_CTL0, cfg);

		for (i = 0; i < max_dmac; i++) {
			index = id * max_dmac + i;
			cfg = cgx_read(cgx, 0,
				       (CGXX_CMRX_RX_DMAC_CAM0 + index * 0x8));
			cfg &= ~CGX_DMAC_CAM_ADDR_ENABLE;
			cgx_write(cgx, 0,
				  (CGXX_CMRX_RX_DMAC_CAM0 + index * 0x8), cfg);
		}
	} else {
		/* Disable promiscuous mode */
		cfg = cgx_read(cgx, lmac_id, CGXX_CMRX_RX_DMAC_CTL0);
		cfg |= CGX_DMAC_CAM_ACCEPT | CGX_DMAC_MCAST_MODE;
		cgx_write(cgx, lmac_id, CGXX_CMRX_RX_DMAC_CTL0, cfg);
		for (i = 0; i < max_dmac; i++) {
			index = id * max_dmac + i;
			cfg = cgx_read(cgx, 0,
				       (CGXX_CMRX_RX_DMAC_CAM0 + index * 0x8));
			if ((cfg & CGX_RX_DMAC_ADR_MASK) != 0) {
				cfg |= CGX_DMAC_CAM_ADDR_ENABLE;
				cgx_write(cgx, 0,
					  (CGXX_CMRX_RX_DMAC_CAM0 +
					   index * 0x8),
					  cfg);
			}
		}
	}
}

static int cgx_lmac_get_pause_frm_status(void *cgxd, int lmac_id,
					 u8 *tx_pause, u8 *rx_pause)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (is_dev_rpm(cgx))
		return 0;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
	*rx_pause = !!(cfg & CGX_SMUX_RX_FRM_CTL_CTL_BCK);

	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_TX_CTL);
	*tx_pause = !!(cfg & CGX_SMUX_TX_CTL_L2P_BP_CONV);
	return 0;
}

/* Enable or disable forwarding received pause frames to Tx block */
void cgx_lmac_enadis_rx_pause_fwding(void *cgxd, int lmac_id, bool enable)
{
	struct cgx *cgx = cgxd;
	u8 rx_pause, tx_pause;
	bool is_pfc_enabled;
	struct lmac *lmac;
	u64 cfg;

	if (!cgx)
		return;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return;

	/* Pause frames are not enabled just return */
	if (!bitmap_weight(lmac->rx_fc_pfvf_bmap.bmap, lmac->rx_fc_pfvf_bmap.max))
		return;

	cgx_lmac_get_pause_frm_status(cgx, lmac_id, &rx_pause, &tx_pause);
	is_pfc_enabled = rx_pause ? false : true;

	if (enable) {
		if (!is_pfc_enabled) {
			cfg = cgx_read(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL);
			cfg |= CGX_GMP_GMI_RXX_FRM_CTL_CTL_BCK;
			cgx_write(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL, cfg);

			cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
			cfg |= CGX_SMUX_RX_FRM_CTL_CTL_BCK;
			cgx_write(cgx, lmac_id,	CGXX_SMUX_RX_FRM_CTL, cfg);
		} else {
			cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_CBFC_CTL);
			cfg |= CGXX_SMUX_CBFC_CTL_BCK_EN;
			cgx_write(cgx, lmac_id, CGXX_SMUX_CBFC_CTL, cfg);
		}
	} else {

		if (!is_pfc_enabled) {
			cfg = cgx_read(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL);
			cfg &= ~CGX_GMP_GMI_RXX_FRM_CTL_CTL_BCK;
			cgx_write(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL, cfg);

			cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
			cfg &= ~CGX_SMUX_RX_FRM_CTL_CTL_BCK;
			cgx_write(cgx, lmac_id,	CGXX_SMUX_RX_FRM_CTL, cfg);
		} else {
			cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_CBFC_CTL);
			cfg &= ~CGXX_SMUX_CBFC_CTL_BCK_EN;
			cgx_write(cgx, lmac_id, CGXX_SMUX_CBFC_CTL, cfg);
		}
	}
}

int cgx_get_rx_stats(void *cgxd, int lmac_id, int idx, u64 *rx_stat)
{
	struct cgx *cgx = cgxd;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;
	*rx_stat =  cgx_read(cgx, lmac_id, CGXX_CMRX_RX_STAT0 + (idx * 8));
	return 0;
}

int cgx_get_tx_stats(void *cgxd, int lmac_id, int idx, u64 *tx_stat)
{
	struct cgx *cgx = cgxd;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;
	*tx_stat = cgx_read(cgx, lmac_id, CGXX_CMRX_TX_STAT0 + (idx * 8));
	return 0;
}

u64 cgx_features_get(void *cgxd)
{
	return ((struct cgx *)cgxd)->hw_features;
}

static int cgx_set_fec_stats_count(struct cgx_link_user_info *linfo)
{
	if (!linfo->fec)
		return 0;

	switch (linfo->lmac_type_id) {
	case LMAC_MODE_SGMII:
	case LMAC_MODE_XAUI:
	case LMAC_MODE_RXAUI:
	case LMAC_MODE_QSGMII:
		return 0;
	case LMAC_MODE_10G_R:
	case LMAC_MODE_25G_R:
	case LMAC_MODE_100G_R:
	case LMAC_MODE_USXGMII:
		return 1;
	case LMAC_MODE_40G_R:
		return 4;
	case LMAC_MODE_50G_R:
		if (linfo->fec == OTX2_FEC_BASER)
			return 2;
		else
			return 1;
	default:
		return 0;
	}
}

int cgx_get_fec_stats(void *cgxd, int lmac_id, struct cgx_fec_stats_rsp *rsp)
{
	int stats, fec_stats_count = 0;
	int corr_reg, uncorr_reg;
	struct cgx *cgx = cgxd;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	if (cgx->lmac_idmap[lmac_id]->link_info.fec == OTX2_FEC_NONE)
		return 0;

	fec_stats_count =
		cgx_set_fec_stats_count(&cgx->lmac_idmap[lmac_id]->link_info);
	if (cgx->lmac_idmap[lmac_id]->link_info.fec == OTX2_FEC_BASER) {
		corr_reg = CGXX_SPUX_LNX_FEC_CORR_BLOCKS;
		uncorr_reg = CGXX_SPUX_LNX_FEC_UNCORR_BLOCKS;
	} else {
		corr_reg = CGXX_SPUX_RSFEC_CORR;
		uncorr_reg = CGXX_SPUX_RSFEC_UNCORR;
	}
	for (stats = 0; stats < fec_stats_count; stats++) {
		rsp->fec_corr_blks +=
			cgx_read(cgx, lmac_id, corr_reg + (stats * 8));
		rsp->fec_uncorr_blks +=
			cgx_read(cgx, lmac_id, uncorr_reg + (stats * 8));
	}
	return 0;
}

int cgx_lmac_rx_tx_enable(void *cgxd, int lmac_id, bool enable)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	cfg = cgx_read(cgx, lmac_id, CGXX_CMRX_CFG);
	if (enable)
		cfg |= DATA_PKT_RX_EN | DATA_PKT_TX_EN;
	else
		cfg &= ~(DATA_PKT_RX_EN | DATA_PKT_TX_EN);
	cgx_write(cgx, lmac_id, CGXX_CMRX_CFG, cfg);
	return 0;
}

int cgx_lmac_tx_enable(void *cgxd, int lmac_id, bool enable)
{
	struct cgx *cgx = cgxd;
	u64 cfg, last;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	cfg = cgx_read(cgx, lmac_id, CGXX_CMRX_CFG);
	last = cfg;
	if (enable)
		cfg |= DATA_PKT_TX_EN;
	else
		cfg &= ~DATA_PKT_TX_EN;

	if (cfg != last)
		cgx_write(cgx, lmac_id, CGXX_CMRX_CFG, cfg);
	return !!(last & DATA_PKT_TX_EN);
}

static int cgx_lmac_enadis_pause_frm(void *cgxd, int lmac_id,
				     u8 tx_pause, u8 rx_pause)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (is_dev_rpm(cgx))
		return 0;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
	cfg &= ~CGX_SMUX_RX_FRM_CTL_CTL_BCK;
	cfg |= rx_pause ? CGX_SMUX_RX_FRM_CTL_CTL_BCK : 0x0;
	cgx_write(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL, cfg);

	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_TX_CTL);
	cfg &= ~CGX_SMUX_TX_CTL_L2P_BP_CONV;
	cfg |= tx_pause ? CGX_SMUX_TX_CTL_L2P_BP_CONV : 0x0;
	cgx_write(cgx, lmac_id, CGXX_SMUX_TX_CTL, cfg);

	cfg = cgx_read(cgx, 0, CGXX_CMR_RX_OVR_BP);
	if (tx_pause) {
		cfg &= ~CGX_CMR_RX_OVR_BP_EN(lmac_id);
	} else {
		cfg |= CGX_CMR_RX_OVR_BP_EN(lmac_id);
		cfg &= ~CGX_CMR_RX_OVR_BP_BP(lmac_id);
	}
	cgx_write(cgx, 0, CGXX_CMR_RX_OVR_BP, cfg);
	return 0;
}

static void cgx_lmac_pause_frm_config(void *cgxd, int lmac_id, bool enable)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (!is_lmac_valid(cgx, lmac_id))
		return;

	if (enable) {
		/* Set pause time and interval */
		cgx_write(cgx, lmac_id, CGXX_SMUX_TX_PAUSE_PKT_TIME,
			  DEFAULT_PAUSE_TIME);
		cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_TX_PAUSE_PKT_INTERVAL);
		cfg &= ~0xFFFFULL;
		cgx_write(cgx, lmac_id, CGXX_SMUX_TX_PAUSE_PKT_INTERVAL,
			  cfg | (DEFAULT_PAUSE_TIME / 2));

		cgx_write(cgx, lmac_id, CGXX_GMP_GMI_TX_PAUSE_PKT_TIME,
			  DEFAULT_PAUSE_TIME);

		cfg = cgx_read(cgx, lmac_id,
			       CGXX_GMP_GMI_TX_PAUSE_PKT_INTERVAL);
		cfg &= ~0xFFFFULL;
		cgx_write(cgx, lmac_id, CGXX_GMP_GMI_TX_PAUSE_PKT_INTERVAL,
			  cfg | (DEFAULT_PAUSE_TIME / 2));
	}

	/* ALL pause frames received are completely ignored */
	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
	cfg &= ~CGX_SMUX_RX_FRM_CTL_CTL_BCK;
	cgx_write(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL, cfg);

	cfg = cgx_read(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL);
	cfg &= ~CGX_GMP_GMI_RXX_FRM_CTL_CTL_BCK;
	cgx_write(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL, cfg);

	/* Disable pause frames transmission */
	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_TX_CTL);
	cfg &= ~CGX_SMUX_TX_CTL_L2P_BP_CONV;
	cgx_write(cgx, lmac_id, CGXX_SMUX_TX_CTL, cfg);

	cfg = cgx_read(cgx, 0, CGXX_CMR_RX_OVR_BP);
	cfg |= CGX_CMR_RX_OVR_BP_EN(lmac_id);
	cfg &= ~CGX_CMR_RX_OVR_BP_BP(lmac_id);
	cgx_write(cgx, 0, CGXX_CMR_RX_OVR_BP, cfg);

	/* Disable all PFC classes by default */
	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_CBFC_CTL);
	cfg = FIELD_SET(CGX_PFC_CLASS_MASK, 0, cfg);
	cgx_write(cgx, lmac_id, CGXX_SMUX_CBFC_CTL, cfg);
}

int verify_lmac_fc_cfg(void *cgxd, int lmac_id, u8 tx_pause, u8 rx_pause,
		       int pfvf_idx)
{
	struct cgx *cgx = cgxd;
	struct lmac *lmac;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return -ENODEV;

	if (!rx_pause)
		clear_bit(pfvf_idx, lmac->rx_fc_pfvf_bmap.bmap);
	else
		set_bit(pfvf_idx, lmac->rx_fc_pfvf_bmap.bmap);

	if (!tx_pause)
		clear_bit(pfvf_idx, lmac->tx_fc_pfvf_bmap.bmap);
	else
		set_bit(pfvf_idx, lmac->tx_fc_pfvf_bmap.bmap);

	/* check if other pfvfs are using flow control */
	if (!rx_pause && bitmap_weight(lmac->rx_fc_pfvf_bmap.bmap, lmac->rx_fc_pfvf_bmap.max)) {
		dev_warn(&cgx->pdev->dev,
			 "Receive Flow control disable not permitted as its used by other PFVFs\n");
		return -EPERM;
	}

	if (!tx_pause && bitmap_weight(lmac->tx_fc_pfvf_bmap.bmap, lmac->tx_fc_pfvf_bmap.max)) {
		dev_warn(&cgx->pdev->dev,
			 "Transmit Flow control disable not permitted as its used by other PFVFs\n");
		return -EPERM;
	}

	return 0;
}

int cgx_lmac_pfc_config(void *cgxd, int lmac_id, u8 tx_pause,
			u8 rx_pause, u16 pfc_en)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	/* Return as no traffic classes are requested */
	if (tx_pause && !pfc_en)
		return 0;

	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_CBFC_CTL);
	pfc_en |= FIELD_GET(CGX_PFC_CLASS_MASK, cfg);

	if (rx_pause) {
		cfg |= (CGXX_SMUX_CBFC_CTL_RX_EN |
			CGXX_SMUX_CBFC_CTL_BCK_EN |
			CGXX_SMUX_CBFC_CTL_DRP_EN);
	} else {
		cfg &= ~(CGXX_SMUX_CBFC_CTL_RX_EN |
			CGXX_SMUX_CBFC_CTL_BCK_EN |
			CGXX_SMUX_CBFC_CTL_DRP_EN);
	}

	if (tx_pause) {
		cfg |= CGXX_SMUX_CBFC_CTL_TX_EN;
		cfg = FIELD_SET(CGX_PFC_CLASS_MASK, pfc_en, cfg);
	} else {
		cfg &= ~CGXX_SMUX_CBFC_CTL_TX_EN;
		cfg = FIELD_SET(CGX_PFC_CLASS_MASK, 0, cfg);
	}

	cgx_write(cgx, lmac_id, CGXX_SMUX_CBFC_CTL, cfg);

	/* Write source MAC address which will be filled into PFC packet */
	cfg = cgx_lmac_addr_get(cgx->cgx_id, lmac_id);
	cgx_write(cgx, lmac_id, CGXX_SMUX_SMAC, cfg);

	return 0;
}

int cgx_lmac_get_pfc_frm_cfg(void *cgxd, int lmac_id, u8 *tx_pause,
			     u8 *rx_pause)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_CBFC_CTL);

	*rx_pause = !!(cfg & CGXX_SMUX_CBFC_CTL_RX_EN);
	*tx_pause = !!(cfg & CGXX_SMUX_CBFC_CTL_TX_EN);

	return 0;
}

void cgx_lmac_ptp_config(void *cgxd, int lmac_id, bool enable)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (!cgx)
		return;

	if (enable) {
		/* Enable inbound PTP timestamping */
		cfg = cgx_read(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL);
		cfg |= CGX_GMP_GMI_RXX_FRM_CTL_PTP_MODE;
		cgx_write(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL, cfg);

		cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
		cfg |= CGX_SMUX_RX_FRM_CTL_PTP_MODE;
		cgx_write(cgx, lmac_id,	CGXX_SMUX_RX_FRM_CTL, cfg);
	} else {
		/* Disable inbound PTP stamping */
		cfg = cgx_read(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL);
		cfg &= ~CGX_GMP_GMI_RXX_FRM_CTL_PTP_MODE;
		cgx_write(cgx, lmac_id, CGXX_GMP_GMI_RXX_FRM_CTL, cfg);

		cfg = cgx_read(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL);
		cfg &= ~CGX_SMUX_RX_FRM_CTL_PTP_MODE;
		cgx_write(cgx, lmac_id, CGXX_SMUX_RX_FRM_CTL, cfg);
	}
}

/* CGX Firmware interface low level support */
int cgx_fwi_cmd_send(u64 req, u64 *resp, struct lmac *lmac)
{
	struct cgx *cgx = lmac->cgx;
	struct device *dev;
	int err = 0;
	u64 cmd;

	/* Ensure no other command is in progress */
	err = mutex_lock_interruptible(&lmac->cmd_lock);
	if (err)
		return err;

	/* Ensure command register is free */
	cmd = cgx_read(cgx, lmac->lmac_id,  CGX_COMMAND_REG);
	if (FIELD_GET(CMDREG_OWN, cmd) != CGX_CMD_OWN_NS) {
		err = -EBUSY;
		goto unlock;
	}

	/* Update ownership in command request */
	req = FIELD_SET(CMDREG_OWN, CGX_CMD_OWN_FIRMWARE, req);

	/* Mark this lmac as pending, before we start */
	lmac->cmd_pend = true;

	/* Start command in hardware */
	cgx_write(cgx, lmac->lmac_id, CGX_COMMAND_REG, req);

	/* Ensure command is completed without errors */
	if (!wait_event_timeout(lmac->wq_cmd_cmplt, !lmac->cmd_pend,
				msecs_to_jiffies(CGX_CMD_TIMEOUT))) {
		dev = &cgx->pdev->dev;
		dev_err(dev, "cgx port %d:%d cmd %lld timeout\n",
			cgx->cgx_id, lmac->lmac_id, FIELD_GET(CMDREG_ID, req));
		err = LMAC_AF_ERR_CMD_TIMEOUT;
		goto unlock;
	}

	/* we have a valid command response */
	smp_rmb(); /* Ensure the latest updates are visible */
	*resp = lmac->resp;

unlock:
	mutex_unlock(&lmac->cmd_lock);

	return err;
}

int cgx_fwi_cmd_generic(u64 req, u64 *resp, struct cgx *cgx, int lmac_id)
{
	struct lmac *lmac;
	int err;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return -ENODEV;

	err = cgx_fwi_cmd_send(req, resp, lmac);

	/* Check for valid response */
	if (!err) {
		if (FIELD_GET(EVTREG_STAT, *resp) == CGX_STAT_FAIL)
			return -EIO;
		else
			return 0;
	}

	return err;
}

static int cgx_link_usertable_index_map(int speed)
{
	switch (speed) {
	case SPEED_10:
		return CGX_LINK_10M;
	case SPEED_100:
		return CGX_LINK_100M;
	case SPEED_1000:
		return CGX_LINK_1G;
	case SPEED_2500:
		return CGX_LINK_2HG;
	case SPEED_5000:
		return CGX_LINK_5G;
	case SPEED_10000:
		return CGX_LINK_10G;
	case SPEED_20000:
		return CGX_LINK_20G;
	case SPEED_25000:
		return CGX_LINK_25G;
	case SPEED_40000:
		return CGX_LINK_40G;
	case SPEED_50000:
		return CGX_LINK_50G;
	case 80000:
		return CGX_LINK_80G;
	case SPEED_100000:
		return CGX_LINK_100G;
	case SPEED_UNKNOWN:
		return CGX_LINK_NONE;
	}
	return CGX_LINK_NONE;
}

static void set_mod_args(struct cgx_set_link_mode_args *args,
			 u32 speed, u8 duplex, u8 autoneg, u64 mode)
{
	/* Fill default values incase of user did not pass
	 * valid parameters
	 */
	if (args->duplex == DUPLEX_UNKNOWN)
		args->duplex = duplex;
	if (args->speed == SPEED_UNKNOWN)
		args->speed = speed;
	if (args->an == AUTONEG_UNKNOWN)
		args->an = autoneg;
	args->mode = mode;
	args->ports = 0;
}

static void otx2_map_ethtool_link_modes(u64 bitmask,
					struct cgx_set_link_mode_args *args)
{
	switch (bitmask) {
	case ETHTOOL_LINK_MODE_10baseT_Half_BIT:
		set_mod_args(args, 10, 1, 1, BIT_ULL(CGX_MODE_SGMII));
		break;
	case  ETHTOOL_LINK_MODE_10baseT_Full_BIT:
		set_mod_args(args, 10, 0, 1, BIT_ULL(CGX_MODE_SGMII));
		break;
	case  ETHTOOL_LINK_MODE_100baseT_Half_BIT:
		set_mod_args(args, 100, 1, 1, BIT_ULL(CGX_MODE_SGMII));
		break;
	case  ETHTOOL_LINK_MODE_100baseT_Full_BIT:
		set_mod_args(args, 100, 0, 1, BIT_ULL(CGX_MODE_SGMII));
		break;
	case  ETHTOOL_LINK_MODE_1000baseT_Half_BIT:
		set_mod_args(args, 1000, 1, 1, BIT_ULL(CGX_MODE_SGMII));
		break;
	case  ETHTOOL_LINK_MODE_1000baseT_Full_BIT:
		set_mod_args(args, 1000, 0, 1, BIT_ULL(CGX_MODE_SGMII));
		break;
	case  ETHTOOL_LINK_MODE_1000baseX_Full_BIT:
		set_mod_args(args, 1000, 0, 0, BIT_ULL(CGX_MODE_1000_BASEX));
		break;
	case  ETHTOOL_LINK_MODE_10000baseT_Full_BIT:
		set_mod_args(args, 1000, 0, 1, BIT_ULL(CGX_MODE_QSGMII));
		break;
	case  ETHTOOL_LINK_MODE_10000baseSR_Full_BIT:
		set_mod_args(args, 10000, 0, 0, BIT_ULL(CGX_MODE_10G_C2C));
		break;
	case  ETHTOOL_LINK_MODE_10000baseLR_Full_BIT:
		set_mod_args(args, 10000, 0, 0, BIT_ULL(CGX_MODE_10G_C2M));
		break;
	case  ETHTOOL_LINK_MODE_10000baseKR_Full_BIT:
		set_mod_args(args, 10000, 0, 1, BIT_ULL(CGX_MODE_10G_KR));
		break;
	case  ETHTOOL_LINK_MODE_25000baseSR_Full_BIT:
		set_mod_args(args, 25000, 0, 0, BIT_ULL(CGX_MODE_25G_C2C));
		break;
	case  ETHTOOL_LINK_MODE_25000baseCR_Full_BIT:
		set_mod_args(args, 25000, 0, 1, BIT_ULL(CGX_MODE_25G_CR));
		break;
	case  ETHTOOL_LINK_MODE_25000baseKR_Full_BIT:
		set_mod_args(args, 25000, 0, 1, BIT_ULL(CGX_MODE_25G_KR));
		break;
	case  ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT:
		set_mod_args(args, 40000, 0, 0, BIT_ULL(CGX_MODE_40G_C2C));
		break;
	case  ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT:
		set_mod_args(args, 40000, 0, 0, BIT_ULL(CGX_MODE_40G_C2M));
		break;
	case  ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT:
		set_mod_args(args, 40000, 0, 1, BIT_ULL(CGX_MODE_40G_CR4));
		break;
	case  ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT:
		set_mod_args(args, 40000, 0, 1, BIT_ULL(CGX_MODE_40G_KR4));
		break;
	case  ETHTOOL_LINK_MODE_50000baseSR_Full_BIT:
		set_mod_args(args, 50000, 0, 0, BIT_ULL(CGX_MODE_50G_C2C));
		break;
	case  ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT:
		set_mod_args(args, 50000, 0, 0, BIT_ULL(CGX_MODE_50G_C2M));
		break;
	case  ETHTOOL_LINK_MODE_50000baseCR_Full_BIT:
		set_mod_args(args, 50000, 0, 1, BIT_ULL(CGX_MODE_50G_CR));
		break;
	case  ETHTOOL_LINK_MODE_50000baseKR_Full_BIT:
		set_mod_args(args, 50000, 0, 1, BIT_ULL(CGX_MODE_50G_KR));
		break;
	case  ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT:
		set_mod_args(args, 100000, 0, 0, BIT_ULL(CGX_MODE_100G_C2C));
		break;
	case  ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT:
		set_mod_args(args, 100000, 0, 0, BIT_ULL(CGX_MODE_100G_C2M));
		break;
	case  ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT:
		set_mod_args(args, 100000, 0, 1, BIT_ULL(CGX_MODE_100G_CR4));
		break;
	case  ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT:
		set_mod_args(args, 100000, 0, 1, BIT_ULL(CGX_MODE_100G_KR4));
		break;
	default:
		set_mod_args(args, 0, 1, 0, BIT_ULL(CGX_MODE_MAX));
		break;
	}
}

static inline void link_status_user_format(u64 lstat,
					   struct cgx_link_user_info *linfo,
					   struct cgx *cgx, u8 lmac_id)
{
	linfo->link_up = FIELD_GET(RESP_LINKSTAT_UP, lstat);
	linfo->full_duplex = FIELD_GET(RESP_LINKSTAT_FDUPLEX, lstat);
	linfo->speed = cgx_speed_mbps[FIELD_GET(RESP_LINKSTAT_SPEED, lstat)];
	linfo->an = FIELD_GET(RESP_LINKSTAT_AN, lstat);
	linfo->fec = FIELD_GET(RESP_LINKSTAT_FEC, lstat);
	linfo->lmac_type_id = FIELD_GET(RESP_LINKSTAT_LMAC_TYPE, lstat);

	if (linfo->lmac_type_id >= LMAC_MODE_MAX) {
		dev_err(&cgx->pdev->dev, "Unknown lmac_type_id %d reported by firmware on cgx port%d:%d",
			linfo->lmac_type_id, cgx->cgx_id, lmac_id);
		strscpy(linfo->lmac_type, "Unknown", sizeof(linfo->lmac_type));
		return;
	}

	strscpy(linfo->lmac_type, cgx_lmactype_string[linfo->lmac_type_id],
		sizeof(linfo->lmac_type));
}

/* Hardware event handlers */
static inline void cgx_link_change_handler(u64 lstat,
					   struct lmac *lmac)
{
	struct cgx_link_user_info *linfo;
	struct cgx *cgx = lmac->cgx;
	struct cgx_link_event event;
	struct device *dev;
	int err_type;

	dev = &cgx->pdev->dev;

	link_status_user_format(lstat, &event.link_uinfo, cgx, lmac->lmac_id);
	err_type = FIELD_GET(RESP_LINKSTAT_ERRTYPE, lstat);

	event.cgx_id = cgx->cgx_id;
	event.lmac_id = lmac->lmac_id;

	/* update the local copy of link status */
	lmac->link_info = event.link_uinfo;
	linfo = &lmac->link_info;

	if (err_type == CGX_ERR_SPEED_CHANGE_INVALID)
		return;

	/* Ensure callback doesn't get unregistered until we finish it */
	spin_lock(&lmac->event_cb_lock);

	if (!lmac->event_cb.notify_link_chg) {
		dev_dbg(dev, "cgx port %d:%d Link change handler null",
			cgx->cgx_id, lmac->lmac_id);
		if (err_type != CGX_ERR_NONE) {
			dev_err(dev, "cgx port %d:%d Link error %d\n",
				cgx->cgx_id, lmac->lmac_id, err_type);
		}
		dev_info(dev, "cgx port %d:%d Link is %s %d Mbps\n",
			 cgx->cgx_id, lmac->lmac_id,
			 linfo->link_up ? "UP" : "DOWN", linfo->speed);
		goto err;
	}

	if (lmac->event_cb.notify_link_chg(&event, lmac->event_cb.data))
		dev_err(dev, "event notification failure\n");
err:
	spin_unlock(&lmac->event_cb_lock);
}

static inline bool cgx_cmdresp_is_linkevent(u64 event)
{
	u8 id;

	id = FIELD_GET(EVTREG_ID, event);
	if (id == CGX_CMD_LINK_BRING_UP ||
	    id == CGX_CMD_LINK_BRING_DOWN ||
	    id == CGX_CMD_MODE_CHANGE)
		return true;
	else
		return false;
}

static inline bool cgx_event_is_linkevent(u64 event)
{
	if (FIELD_GET(EVTREG_ID, event) == CGX_EVT_LINK_CHANGE)
		return true;
	else
		return false;
}

static irqreturn_t cgx_fwi_event_handler(int irq, void *data)
{
	u64 event, offset, clear_bit;
	struct lmac *lmac = data;
	struct cgx *cgx;

	cgx = lmac->cgx;

	/* Clear SW_INT for RPM and CMR_INT for CGX */
	offset     = cgx->mac_ops->int_register;
	clear_bit  = cgx->mac_ops->int_ena_bit;

	event = cgx_read(cgx, lmac->lmac_id, CGX_EVENT_REG);

	if (!FIELD_GET(EVTREG_ACK, event))
		return IRQ_NONE;

	switch (FIELD_GET(EVTREG_EVT_TYPE, event)) {
	case CGX_EVT_CMD_RESP:
		/* Copy the response. Since only one command is active at a
		 * time, there is no way a response can get overwritten
		 */
		lmac->resp = event;
		/* Ensure response is updated before thread context starts */
		smp_wmb();

		/* There wont be separate events for link change initiated from
		 * software; Hence report the command responses as events
		 */
		if (cgx_cmdresp_is_linkevent(event))
			cgx_link_change_handler(event, lmac);

		/* Release thread waiting for completion  */
		lmac->cmd_pend = false;
		wake_up_interruptible(&lmac->wq_cmd_cmplt);
		break;
	case CGX_EVT_ASYNC:
		if (cgx_event_is_linkevent(event))
			cgx_link_change_handler(event, lmac);
		break;
	}

	/* Any new event or command response will be posted by firmware
	 * only after the current status is acked.
	 * Ack the interrupt register as well.
	 */
	cgx_write(lmac->cgx, lmac->lmac_id, CGX_EVENT_REG, 0);
	cgx_write(lmac->cgx, lmac->lmac_id, offset, clear_bit);

	return IRQ_HANDLED;
}

/* APIs for PHY management using CGX firmware interface */

/* callback registration for hardware events like link change */
int cgx_lmac_evh_register(struct cgx_event_cb *cb, void *cgxd, int lmac_id)
{
	struct cgx *cgx = cgxd;
	struct lmac *lmac;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return -ENODEV;

	lmac->event_cb = *cb;

	return 0;
}

int cgx_lmac_evh_unregister(void *cgxd, int lmac_id)
{
	struct lmac *lmac;
	unsigned long flags;
	struct cgx *cgx = cgxd;

	lmac = lmac_pdata(lmac_id, cgx);
	if (!lmac)
		return -ENODEV;

	spin_lock_irqsave(&lmac->event_cb_lock, flags);
	lmac->event_cb.notify_link_chg = NULL;
	lmac->event_cb.data = NULL;
	spin_unlock_irqrestore(&lmac->event_cb_lock, flags);

	return 0;
}

int cgx_get_fwdata_base(u64 *base)
{
	u64 req = 0, resp;
	struct cgx *cgx;
	int first_lmac;
	int err;

	cgx = list_first_entry_or_null(&cgx_list, struct cgx, cgx_list);
	if (!cgx)
		return -ENXIO;

	first_lmac = find_first_bit(&cgx->lmac_bmap, cgx->max_lmac_per_mac);
	req = FIELD_SET(CMDREG_ID, CGX_CMD_GET_FWD_BASE, req);
	err = cgx_fwi_cmd_generic(req, &resp, cgx, first_lmac);
	if (!err)
		*base = FIELD_GET(RESP_FWD_BASE, resp);

	return err;
}

int cgx_set_link_mode(void *cgxd, struct cgx_set_link_mode_args args,
		      int cgx_id, int lmac_id)
{
	struct cgx *cgx = cgxd;
	u64 req = 0, resp;

	if (!cgx)
		return -ENODEV;

	if (args.mode)
		otx2_map_ethtool_link_modes(args.mode, &args);
	if (!args.speed && args.duplex && !args.an)
		return -EINVAL;

	req = FIELD_SET(CMDREG_ID, CGX_CMD_MODE_CHANGE, req);
	req = FIELD_SET(CMDMODECHANGE_SPEED,
			cgx_link_usertable_index_map(args.speed), req);
	req = FIELD_SET(CMDMODECHANGE_DUPLEX, args.duplex, req);
	req = FIELD_SET(CMDMODECHANGE_AN, args.an, req);
	req = FIELD_SET(CMDMODECHANGE_PORT, args.ports, req);
	req = FIELD_SET(CMDMODECHANGE_FLAGS, args.mode, req);

	return cgx_fwi_cmd_generic(req, &resp, cgx, lmac_id);
}
int cgx_set_fec(u64 fec, int cgx_id, int lmac_id)
{
	u64 req = 0, resp;
	struct cgx *cgx;
	int err = 0;

	cgx = cgx_get_pdata(cgx_id);
	if (!cgx)
		return -ENXIO;

	req = FIELD_SET(CMDREG_ID, CGX_CMD_SET_FEC, req);
	req = FIELD_SET(CMDSETFEC, fec, req);
	err = cgx_fwi_cmd_generic(req, &resp, cgx, lmac_id);
	if (err)
		return err;

	cgx->lmac_idmap[lmac_id]->link_info.fec =
			FIELD_GET(RESP_LINKSTAT_FEC, resp);
	return cgx->lmac_idmap[lmac_id]->link_info.fec;
}

int cgx_get_phy_fec_stats(void *cgxd, int lmac_id)
{
	struct cgx *cgx = cgxd;
	u64 req = 0, resp;

	if (!cgx)
		return -ENODEV;

	req = FIELD_SET(CMDREG_ID, CGX_CMD_GET_PHY_FEC_STATS, req);
	return cgx_fwi_cmd_generic(req, &resp, cgx, lmac_id);
}

static int cgx_fwi_link_change(struct cgx *cgx, int lmac_id, bool enable)
{
	u64 req = 0;
	u64 resp;

	if (enable) {
		req = FIELD_SET(CMDREG_ID, CGX_CMD_LINK_BRING_UP, req);
		/* On CN10K firmware offloads link bring up/down operations to ECP
		 * On Octeontx2 link operations are handled by firmware itself
		 * which can cause mbox errors so configure maximum time firmware
		 * poll for Link as 1000 ms
		 */
		if (!is_dev_rpm(cgx))
			req = FIELD_SET(LINKCFG_TIMEOUT, 1000, req);

	} else {
		req = FIELD_SET(CMDREG_ID, CGX_CMD_LINK_BRING_DOWN, req);
	}
	return cgx_fwi_cmd_generic(req, &resp, cgx, lmac_id);
}

static inline int cgx_fwi_read_version(u64 *resp, struct cgx *cgx)
{
	int first_lmac = find_first_bit(&cgx->lmac_bmap, cgx->max_lmac_per_mac);
	u64 req = 0;

	req = FIELD_SET(CMDREG_ID, CGX_CMD_GET_FW_VER, req);
	return cgx_fwi_cmd_generic(req, resp, cgx, first_lmac);
}

static int cgx_lmac_verify_fwi_version(struct cgx *cgx)
{
	struct device *dev = &cgx->pdev->dev;
	int major_ver, minor_ver;
	u64 resp;
	int err;

	if (!cgx->lmac_count)
		return 0;

	err = cgx_fwi_read_version(&resp, cgx);
	if (err)
		return err;

	major_ver = FIELD_GET(RESP_MAJOR_VER, resp);
	minor_ver = FIELD_GET(RESP_MINOR_VER, resp);
	dev_dbg(dev, "Firmware command interface version = %d.%d\n",
		major_ver, minor_ver);
	if (major_ver != CGX_FIRMWARE_MAJOR_VER)
		return -EIO;
	else
		return 0;
}

static void cgx_lmac_linkup_work(struct work_struct *work)
{
	struct cgx *cgx = container_of(work, struct cgx, cgx_cmd_work);
	struct device *dev = &cgx->pdev->dev;
	int i, err;

	/* Do Link up for all the enabled lmacs */
	for_each_set_bit(i, &cgx->lmac_bmap, cgx->max_lmac_per_mac) {
		err = cgx_fwi_link_change(cgx, i, true);
		if (err)
			dev_info(dev, "cgx port %d:%d Link up command failed\n",
				 cgx->cgx_id, i);
	}
}

int cgx_lmac_linkup_start(void *cgxd)
{
	struct cgx *cgx = cgxd;

	if (!cgx)
		return -ENODEV;

	queue_work(cgx->cgx_cmd_workq, &cgx->cgx_cmd_work);

	return 0;
}

int cgx_lmac_reset(void *cgxd, int lmac_id, u8 pf_req_flr)
{
	struct cgx *cgx = cgxd;
	u64 cfg;

	if (!is_lmac_valid(cgx, lmac_id))
		return -ENODEV;

	/* Resetting PFC related CSRs */
	cfg = 0xff;
	cgx_write(cgxd, lmac_id, CGXX_CMRX_RX_LOGL_XON, cfg);

	if (pf_req_flr)
		cgx_lmac_internal_loopback(cgxd, lmac_id, false);
	return 0;
}

static int cgx_configure_interrupt(struct cgx *cgx, struct lmac *lmac,
				   int cnt, bool req_free)
{
	struct mac_ops *mac_ops = cgx->mac_ops;
	u64 offset, ena_bit;
	unsigned int irq;
	int err;

	irq      = pci_irq_vector(cgx->pdev, mac_ops->lmac_fwi +
				  cnt * mac_ops->irq_offset);
	offset   = mac_ops->int_set_reg;
	ena_bit  = mac_ops->int_ena_bit;

	if (req_free) {
		free_irq(irq, lmac);
		return 0;
	}

	err = request_irq(irq, cgx_fwi_event_handler, 0, lmac->name, lmac);
	if (err)
		return err;

	/* Enable interrupt */
	cgx_write(cgx, lmac->lmac_id, offset, ena_bit);
	return 0;
}

int cgx_get_nr_lmacs(void *cgxd)
{
	struct cgx *cgx = cgxd;

	return cgx_read(cgx, 0, CGXX_CMRX_RX_LMACS) & 0x7ULL;
}

u8 cgx_get_lmacid(void *cgxd, u8 lmac_index)
{
	struct cgx *cgx = cgxd;

	return cgx->lmac_idmap[lmac_index]->lmac_id;
}

unsigned long cgx_get_lmac_bmap(void *cgxd)
{
	struct cgx *cgx = cgxd;

	return cgx->lmac_bmap;
}

static int cgx_lmac_init(struct cgx *cgx)
{
	struct lmac *lmac;
	u64 lmac_list;
	int i, err;

	/* lmac_list specifies which lmacs are enabled
	 * when bit n is set to 1, LMAC[n] is enabled
	 */
	if (cgx->mac_ops->non_contiguous_serdes_lane) {
		if (is_dev_rpm2(cgx))
			lmac_list =
				cgx_read(cgx, 0, RPM2_CMRX_RX_LMACS) & 0xFFULL;
		else
			lmac_list =
				cgx_read(cgx, 0, CGXX_CMRX_RX_LMACS) & 0xFULL;
	}

	if (cgx->lmac_count > cgx->max_lmac_per_mac)
		cgx->lmac_count = cgx->max_lmac_per_mac;

	for (i = 0; i < cgx->lmac_count; i++) {
		lmac = kzalloc(sizeof(struct lmac), GFP_KERNEL);
		if (!lmac)
			return -ENOMEM;
		lmac->name = kcalloc(1, sizeof("cgx_fwi_xxx_yyy"), GFP_KERNEL);
		if (!lmac->name) {
			err = -ENOMEM;
			goto err_lmac_free;
		}
		sprintf(lmac->name, "cgx_fwi_%d_%d", cgx->cgx_id, i);
		if (cgx->mac_ops->non_contiguous_serdes_lane) {
			lmac->lmac_id = __ffs64(lmac_list);
			lmac_list   &= ~BIT_ULL(lmac->lmac_id);
		} else {
			lmac->lmac_id = i;
		}

		lmac->cgx = cgx;
		lmac->mac_to_index_bmap.max =
				cgx->mac_ops->dmac_filter_count /
				cgx->lmac_count;

		err = rvu_alloc_bitmap(&lmac->mac_to_index_bmap);
		if (err)
			goto err_name_free;

		/* Reserve first entry for default MAC address */
		set_bit(0, lmac->mac_to_index_bmap.bmap);

		lmac->rx_fc_pfvf_bmap.max = 128;
		err = rvu_alloc_bitmap(&lmac->rx_fc_pfvf_bmap);
		if (err)
			goto err_dmac_bmap_free;

		lmac->tx_fc_pfvf_bmap.max = 128;
		err = rvu_alloc_bitmap(&lmac->tx_fc_pfvf_bmap);
		if (err)
			goto err_rx_fc_bmap_free;

		init_waitqueue_head(&lmac->wq_cmd_cmplt);
		mutex_init(&lmac->cmd_lock);
		spin_lock_init(&lmac->event_cb_lock);
		err = cgx_configure_interrupt(cgx, lmac, lmac->lmac_id, false);
		if (err)
			goto err_bitmap_free;

		/* Add reference */
		cgx->lmac_idmap[lmac->lmac_id] = lmac;
		set_bit(lmac->lmac_id, &cgx->lmac_bmap);
		cgx->mac_ops->mac_pause_frm_config(cgx, lmac->lmac_id, true);
		lmac->lmac_type = cgx->mac_ops->get_lmac_type(cgx, lmac->lmac_id);
	}

	return cgx_lmac_verify_fwi_version(cgx);

err_bitmap_free:
	rvu_free_bitmap(&lmac->tx_fc_pfvf_bmap);
err_rx_fc_bmap_free:
	rvu_free_bitmap(&lmac->rx_fc_pfvf_bmap);
err_dmac_bmap_free:
	rvu_free_bitmap(&lmac->mac_to_index_bmap);
err_name_free:
	kfree(lmac->name);
err_lmac_free:
	kfree(lmac);
	return err;
}

static int cgx_lmac_exit(struct cgx *cgx)
{
	struct lmac *lmac;
	int i;

	if (cgx->cgx_cmd_workq) {
		destroy_workqueue(cgx->cgx_cmd_workq);
		cgx->cgx_cmd_workq = NULL;
	}

	/* Free all lmac related resources */
	for_each_set_bit(i, &cgx->lmac_bmap, cgx->max_lmac_per_mac) {
		lmac = cgx->lmac_idmap[i];
		if (!lmac)
			continue;
		cgx->mac_ops->mac_pause_frm_config(cgx, lmac->lmac_id, false);
		cgx_configure_interrupt(cgx, lmac, lmac->lmac_id, true);
		kfree(lmac->mac_to_index_bmap.bmap);
		kfree(lmac->name);
		kfree(lmac);
	}

	return 0;
}

static void cgx_populate_features(struct cgx *cgx)
{
	u64 cfg;

	cfg = cgx_read(cgx, 0, CGX_CONST);
	cgx->mac_ops->fifo_len = FIELD_GET(CGX_CONST_RXFIFO_SIZE, cfg);
	cgx->max_lmac_per_mac = FIELD_GET(CGX_CONST_MAX_LMACS, cfg);

	if (is_dev_rpm(cgx))
		cgx->hw_features = (RVU_LMAC_FEAT_DMACF | RVU_MAC_RPM |
				    RVU_LMAC_FEAT_FC | RVU_LMAC_FEAT_PTP);
	else
		cgx->hw_features = (RVU_LMAC_FEAT_FC  | RVU_LMAC_FEAT_HIGIG2 |
				    RVU_LMAC_FEAT_PTP | RVU_LMAC_FEAT_DMACF);
}

static u8 cgx_get_rxid_mapoffset(struct cgx *cgx)
{
	if (cgx->pdev->subsystem_device == PCI_SUBSYS_DEVID_CNF10KB_RPM ||
	    is_dev_rpm2(cgx))
		return 0x80;
	else
		return 0x60;
}

static struct mac_ops	cgx_mac_ops    = {
	.name		=       "cgx",
	.csr_offset	=       0,
	.lmac_offset    =       18,
	.int_register	=       CGXX_CMRX_INT,
	.int_set_reg	=       CGXX_CMRX_INT_ENA_W1S,
	.irq_offset	=       9,
	.int_ena_bit    =       FW_CGX_INT,
	.lmac_fwi	=	CGX_LMAC_FWI,
	.non_contiguous_serdes_lane = false,
	.rx_stats_cnt   =       9,
	.tx_stats_cnt   =       18,
	.dmac_filter_count =    32,
	.get_nr_lmacs	=	cgx_get_nr_lmacs,
	.get_lmac_type  =       cgx_get_lmac_type,
	.lmac_fifo_len	=	cgx_get_lmac_fifo_len,
	.mac_lmac_intl_lbk =    cgx_lmac_internal_loopback,
	.mac_get_rx_stats  =	cgx_get_rx_stats,
	.mac_get_tx_stats  =	cgx_get_tx_stats,
	.get_fec_stats	   =	cgx_get_fec_stats,
	.mac_enadis_rx_pause_fwding =	cgx_lmac_enadis_rx_pause_fwding,
	.mac_get_pause_frm_status =	cgx_lmac_get_pause_frm_status,
	.mac_enadis_pause_frm =		cgx_lmac_enadis_pause_frm,
	.mac_pause_frm_config =		cgx_lmac_pause_frm_config,
	.mac_enadis_ptp_config =	cgx_lmac_ptp_config,
	.mac_rx_tx_enable =		cgx_lmac_rx_tx_enable,
	.mac_tx_enable =		cgx_lmac_tx_enable,
	.pfc_config =                   cgx_lmac_pfc_config,
	.mac_get_pfc_frm_cfg   =        cgx_lmac_get_pfc_frm_cfg,
	.mac_reset   =			cgx_lmac_reset,
};

static int cgx_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct cgx *cgx;
	int err, nvec;

	cgx = devm_kzalloc(dev, sizeof(*cgx), GFP_KERNEL);
	if (!cgx)
		return -ENOMEM;
	cgx->pdev = pdev;

	pci_set_drvdata(pdev, cgx);

	/* Use mac_ops to get MAC specific features */
	if (is_dev_rpm(cgx))
		cgx->mac_ops = rpm_get_mac_ops(cgx);
	else
		cgx->mac_ops = &cgx_mac_ops;

	cgx->mac_ops->rxid_map_offset = cgx_get_rxid_mapoffset(cgx);

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
	cgx->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!cgx->reg_base) {
		dev_err(dev, "CGX: Cannot map CSR memory space, aborting\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	cgx->lmac_count = cgx->mac_ops->get_nr_lmacs(cgx);
	if (!cgx->lmac_count) {
		dev_notice(dev, "CGX %d LMAC count is zero, skipping probe\n", cgx->cgx_id);
		err = -EOPNOTSUPP;
		goto err_release_regions;
	}

	nvec = pci_msix_vec_count(cgx->pdev);
	err = pci_alloc_irq_vectors(pdev, nvec, nvec, PCI_IRQ_MSIX);
	if (err < 0 || err != nvec) {
		dev_err(dev, "Request for %d msix vectors failed, err %d\n",
			nvec, err);
		goto err_release_regions;
	}

	cgx->cgx_id = (pci_resource_start(pdev, PCI_CFG_REG_BAR_NUM) >> 24)
		& CGX_ID_MASK;

	/* init wq for processing linkup requests */
	INIT_WORK(&cgx->cgx_cmd_work, cgx_lmac_linkup_work);
	cgx->cgx_cmd_workq = alloc_workqueue("cgx_cmd_workq", 0, 0);
	if (!cgx->cgx_cmd_workq) {
		dev_err(dev, "alloc workqueue failed for cgx cmd");
		err = -ENOMEM;
		goto err_free_irq_vectors;
	}

	list_add(&cgx->cgx_list, &cgx_list);


	cgx_populate_features(cgx);

	mutex_init(&cgx->lock);

	err = cgx_lmac_init(cgx);
	if (err)
		goto err_release_lmac;

	return 0;

err_release_lmac:
	cgx_lmac_exit(cgx);
	list_del(&cgx->cgx_list);
err_free_irq_vectors:
	pci_free_irq_vectors(pdev);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void cgx_remove(struct pci_dev *pdev)
{
	struct cgx *cgx = pci_get_drvdata(pdev);

	if (cgx) {
		cgx_lmac_exit(cgx);
		list_del(&cgx->cgx_list);
	}
	pci_free_irq_vectors(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

struct pci_driver cgx_driver = {
	.name = DRV_NAME,
	.id_table = cgx_id_table,
	.probe = cgx_probe,
	.remove = cgx_remove,
};
