// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include <asm/unaligned.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/fsl/enetc_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/pcs-lynx.h>
#include "enetc_ierb.h"
#include "enetc_pf.h"

#define ENETC_DRV_NAME_STR "ENETC PF driver"

static void enetc_pf_get_primary_mac_addr(struct enetc_hw *hw, int si, u8 *addr)
{
	u32 upper = __raw_readl(hw->port + ENETC_PSIPMAR0(si));
	u16 lower = __raw_readw(hw->port + ENETC_PSIPMAR1(si));

	put_unaligned_le32(upper, addr);
	put_unaligned_le16(lower, addr + 4);
}

static void enetc_pf_set_primary_mac_addr(struct enetc_hw *hw, int si,
					  const u8 *addr)
{
	u32 upper = get_unaligned_le32(addr);
	u16 lower = get_unaligned_le16(addr + 4);

	__raw_writel(upper, hw->port + ENETC_PSIPMAR0(si));
	__raw_writew(lower, hw->port + ENETC_PSIPMAR1(si));
}

static int enetc_pf_set_mac_addr(struct net_device *ndev, void *addr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(ndev, saddr->sa_data);
	enetc_pf_set_primary_mac_addr(&priv->si->hw, 0, saddr->sa_data);

	return 0;
}

static void enetc_set_vlan_promisc(struct enetc_hw *hw, char si_map)
{
	u32 val = enetc_port_rd(hw, ENETC_PSIPVMR);

	val &= ~ENETC_PSIPVMR_SET_VP(ENETC_VLAN_PROMISC_MAP_ALL);
	enetc_port_wr(hw, ENETC_PSIPVMR, ENETC_PSIPVMR_SET_VP(si_map) | val);
}

static void enetc_enable_si_vlan_promisc(struct enetc_pf *pf, int si_idx)
{
	pf->vlan_promisc_simap |= BIT(si_idx);
	enetc_set_vlan_promisc(&pf->si->hw, pf->vlan_promisc_simap);
}

static void enetc_disable_si_vlan_promisc(struct enetc_pf *pf, int si_idx)
{
	pf->vlan_promisc_simap &= ~BIT(si_idx);
	enetc_set_vlan_promisc(&pf->si->hw, pf->vlan_promisc_simap);
}

static void enetc_set_isol_vlan(struct enetc_hw *hw, int si, u16 vlan, u8 qos)
{
	u32 val = 0;

	if (vlan)
		val = ENETC_PSIVLAN_EN | ENETC_PSIVLAN_SET_QOS(qos) | vlan;

	enetc_port_wr(hw, ENETC_PSIVLANR(si), val);
}

static int enetc_mac_addr_hash_idx(const u8 *addr)
{
	u64 fold = __swab64(ether_addr_to_u64(addr)) >> 16;
	u64 mask = 0;
	int res = 0;
	int i;

	for (i = 0; i < 8; i++)
		mask |= BIT_ULL(i * 6);

	for (i = 0; i < 6; i++)
		res |= (hweight64(fold & (mask << i)) & 0x1) << i;

	return res;
}

static void enetc_reset_mac_addr_filter(struct enetc_mac_filter *filter)
{
	filter->mac_addr_cnt = 0;

	bitmap_zero(filter->mac_hash_table,
		    ENETC_MADDR_HASH_TBL_SZ);
}

static void enetc_add_mac_addr_em_filter(struct enetc_mac_filter *filter,
					 const unsigned char *addr)
{
	/* add exact match addr */
	ether_addr_copy(filter->mac_addr, addr);
	filter->mac_addr_cnt++;
}

static void enetc_add_mac_addr_ht_filter(struct enetc_mac_filter *filter,
					 const unsigned char *addr)
{
	int idx = enetc_mac_addr_hash_idx(addr);

	/* add hash table entry */
	__set_bit(idx, filter->mac_hash_table);
	filter->mac_addr_cnt++;
}

static void enetc_clear_mac_ht_flt(struct enetc_si *si, int si_idx, int type)
{
	bool err = si->errata & ENETC_ERR_UCMCSWP;

	if (type == UC) {
		enetc_port_wr(&si->hw, ENETC_PSIUMHFR0(si_idx, err), 0);
		enetc_port_wr(&si->hw, ENETC_PSIUMHFR1(si_idx), 0);
	} else { /* MC */
		enetc_port_wr(&si->hw, ENETC_PSIMMHFR0(si_idx, err), 0);
		enetc_port_wr(&si->hw, ENETC_PSIMMHFR1(si_idx), 0);
	}
}

static void enetc_set_mac_ht_flt(struct enetc_si *si, int si_idx, int type,
				 unsigned long hash)
{
	bool err = si->errata & ENETC_ERR_UCMCSWP;

	if (type == UC) {
		enetc_port_wr(&si->hw, ENETC_PSIUMHFR0(si_idx, err),
			      lower_32_bits(hash));
		enetc_port_wr(&si->hw, ENETC_PSIUMHFR1(si_idx),
			      upper_32_bits(hash));
	} else { /* MC */
		enetc_port_wr(&si->hw, ENETC_PSIMMHFR0(si_idx, err),
			      lower_32_bits(hash));
		enetc_port_wr(&si->hw, ENETC_PSIMMHFR1(si_idx),
			      upper_32_bits(hash));
	}
}

static void enetc_sync_mac_filters(struct enetc_pf *pf)
{
	struct enetc_mac_filter *f = pf->mac_filter;
	struct enetc_si *si = pf->si;
	int i, pos;

	pos = EMETC_MAC_ADDR_FILT_RES;

	for (i = 0; i < MADDR_TYPE; i++, f++) {
		bool em = (f->mac_addr_cnt == 1) && (i == UC);
		bool clear = !f->mac_addr_cnt;

		if (clear) {
			if (i == UC)
				enetc_clear_mac_flt_entry(si, pos);

			enetc_clear_mac_ht_flt(si, 0, i);
			continue;
		}

		/* exact match filter */
		if (em) {
			int err;

			enetc_clear_mac_ht_flt(si, 0, UC);

			err = enetc_set_mac_flt_entry(si, pos, f->mac_addr,
						      BIT(0));
			if (!err)
				continue;

			/* fallback to HT filtering */
			dev_warn(&si->pdev->dev, "fallback to HT filt (%d)\n",
				 err);
		}

		/* hash table filter, clear EM filter for UC entries */
		if (i == UC)
			enetc_clear_mac_flt_entry(si, pos);

		enetc_set_mac_ht_flt(si, 0, i, *f->mac_hash_table);
	}
}

static void enetc_pf_set_rx_mode(struct net_device *ndev)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_pf *pf = enetc_si_priv(priv->si);
	struct enetc_hw *hw = &priv->si->hw;
	bool uprom = false, mprom = false;
	struct enetc_mac_filter *filter;
	struct netdev_hw_addr *ha;
	u32 psipmr = 0;
	bool em;

	if (ndev->flags & IFF_PROMISC) {
		/* enable promisc mode for SI0 (PF) */
		psipmr = ENETC_PSIPMR_SET_UP(0) | ENETC_PSIPMR_SET_MP(0);
		uprom = true;
		mprom = true;
	} else if (ndev->flags & IFF_ALLMULTI) {
		/* enable multi cast promisc mode for SI0 (PF) */
		psipmr = ENETC_PSIPMR_SET_MP(0);
		mprom = true;
	}

	/* first 2 filter entries belong to PF */
	if (!uprom) {
		/* Update unicast filters */
		filter = &pf->mac_filter[UC];
		enetc_reset_mac_addr_filter(filter);

		em = (netdev_uc_count(ndev) == 1);
		netdev_for_each_uc_addr(ha, ndev) {
			if (em) {
				enetc_add_mac_addr_em_filter(filter, ha->addr);
				break;
			}

			enetc_add_mac_addr_ht_filter(filter, ha->addr);
		}
	}

	if (!mprom) {
		/* Update multicast filters */
		filter = &pf->mac_filter[MC];
		enetc_reset_mac_addr_filter(filter);

		netdev_for_each_mc_addr(ha, ndev) {
			if (!is_multicast_ether_addr(ha->addr))
				continue;

			enetc_add_mac_addr_ht_filter(filter, ha->addr);
		}
	}

	if (!uprom || !mprom)
		/* update PF entries */
		enetc_sync_mac_filters(pf);

	psipmr |= enetc_port_rd(hw, ENETC_PSIPMR) &
		  ~(ENETC_PSIPMR_SET_UP(0) | ENETC_PSIPMR_SET_MP(0));
	enetc_port_wr(hw, ENETC_PSIPMR, psipmr);
}

static void enetc_set_vlan_ht_filter(struct enetc_hw *hw, int si_idx,
				     unsigned long hash)
{
	enetc_port_wr(hw, ENETC_PSIVHFR0(si_idx), lower_32_bits(hash));
	enetc_port_wr(hw, ENETC_PSIVHFR1(si_idx), upper_32_bits(hash));
}

static int enetc_vid_hash_idx(unsigned int vid)
{
	int res = 0;
	int i;

	for (i = 0; i < 6; i++)
		res |= (hweight8(vid & (BIT(i) | BIT(i + 6))) & 0x1) << i;

	return res;
}

static void enetc_sync_vlan_ht_filter(struct enetc_pf *pf, bool rehash)
{
	int i;

	if (rehash) {
		bitmap_zero(pf->vlan_ht_filter, ENETC_VLAN_HT_SIZE);

		for_each_set_bit(i, pf->active_vlans, VLAN_N_VID) {
			int hidx = enetc_vid_hash_idx(i);

			__set_bit(hidx, pf->vlan_ht_filter);
		}
	}

	enetc_set_vlan_ht_filter(&pf->si->hw, 0, *pf->vlan_ht_filter);
}

static int enetc_vlan_rx_add_vid(struct net_device *ndev, __be16 prot, u16 vid)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_pf *pf = enetc_si_priv(priv->si);
	int idx;

	__set_bit(vid, pf->active_vlans);

	idx = enetc_vid_hash_idx(vid);
	if (!__test_and_set_bit(idx, pf->vlan_ht_filter))
		enetc_sync_vlan_ht_filter(pf, false);

	return 0;
}

static int enetc_vlan_rx_del_vid(struct net_device *ndev, __be16 prot, u16 vid)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_pf *pf = enetc_si_priv(priv->si);

	__clear_bit(vid, pf->active_vlans);
	enetc_sync_vlan_ht_filter(pf, true);

	return 0;
}

static void enetc_set_loopback(struct net_device *ndev, bool en)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_si *si = priv->si;
	u32 reg;

	reg = enetc_port_mac_rd(si, ENETC_PM0_IF_MODE);
	if (reg & ENETC_PM0_IFM_RG) {
		/* RGMII mode */
		reg = (reg & ~ENETC_PM0_IFM_RLP) |
		      (en ? ENETC_PM0_IFM_RLP : 0);
		enetc_port_mac_wr(si, ENETC_PM0_IF_MODE, reg);
	} else {
		/* assume SGMII mode */
		reg = enetc_port_mac_rd(si, ENETC_PM0_CMD_CFG);
		reg = (reg & ~ENETC_PM0_CMD_XGLP) |
		      (en ? ENETC_PM0_CMD_XGLP : 0);
		reg = (reg & ~ENETC_PM0_CMD_PHY_TX_EN) |
		      (en ? ENETC_PM0_CMD_PHY_TX_EN : 0);
		enetc_port_mac_wr(si, ENETC_PM0_CMD_CFG, reg);
	}
}

static int enetc_pf_set_vf_mac(struct net_device *ndev, int vf, u8 *mac)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_pf *pf = enetc_si_priv(priv->si);
	struct enetc_vf_state *vf_state;

	if (vf >= pf->total_vfs)
		return -EINVAL;

	if (!is_valid_ether_addr(mac))
		return -EADDRNOTAVAIL;

	vf_state = &pf->vf_state[vf];
	vf_state->flags |= ENETC_VF_FLAG_PF_SET_MAC;
	enetc_pf_set_primary_mac_addr(&priv->si->hw, vf + 1, mac);
	return 0;
}

static int enetc_pf_set_vf_vlan(struct net_device *ndev, int vf, u16 vlan,
				u8 qos, __be16 proto)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_pf *pf = enetc_si_priv(priv->si);

	if (priv->si->errata & ENETC_ERR_VLAN_ISOL)
		return -EOPNOTSUPP;

	if (vf >= pf->total_vfs)
		return -EINVAL;

	if (proto != htons(ETH_P_8021Q))
		/* only C-tags supported for now */
		return -EPROTONOSUPPORT;

	enetc_set_isol_vlan(&priv->si->hw, vf + 1, vlan, qos);
	return 0;
}

static int enetc_pf_set_vf_spoofchk(struct net_device *ndev, int vf, bool en)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct enetc_pf *pf = enetc_si_priv(priv->si);
	u32 cfgr;

	if (vf >= pf->total_vfs)
		return -EINVAL;

	cfgr = enetc_port_rd(&priv->si->hw, ENETC_PSICFGR0(vf + 1));
	cfgr = (cfgr & ~ENETC_PSICFGR0_ASE) | (en ? ENETC_PSICFGR0_ASE : 0);
	enetc_port_wr(&priv->si->hw, ENETC_PSICFGR0(vf + 1), cfgr);

	return 0;
}

static int enetc_setup_mac_address(struct device_node *np, struct enetc_pf *pf,
				   int si)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_hw *hw = &pf->si->hw;
	u8 mac_addr[ETH_ALEN] = { 0 };
	int err;

	/* (1) try to get the MAC address from the device tree */
	if (np) {
		err = of_get_mac_address(np, mac_addr);
		if (err == -EPROBE_DEFER)
			return err;
	}

	/* (2) bootloader supplied MAC address */
	if (is_zero_ether_addr(mac_addr))
		enetc_pf_get_primary_mac_addr(hw, si, mac_addr);

	/* (3) choose a random one */
	if (is_zero_ether_addr(mac_addr)) {
		eth_random_addr(mac_addr);
		dev_info(dev, "no MAC address specified for SI%d, using %pM\n",
			 si, mac_addr);
	}

	enetc_pf_set_primary_mac_addr(hw, si, mac_addr);

	return 0;
}

static int enetc_setup_mac_addresses(struct device_node *np,
				     struct enetc_pf *pf)
{
	int err, i;

	/* The PF might take its MAC from the device tree */
	err = enetc_setup_mac_address(np, pf, 0);
	if (err)
		return err;

	for (i = 0; i < pf->total_vfs; i++) {
		err = enetc_setup_mac_address(NULL, pf, i + 1);
		if (err)
			return err;
	}

	return 0;
}

static void enetc_port_assign_rfs_entries(struct enetc_si *si)
{
	struct enetc_pf *pf = enetc_si_priv(si);
	struct enetc_hw *hw = &si->hw;
	int num_entries, vf_entries, i;
	u32 val;

	/* split RFS entries between functions */
	val = enetc_port_rd(hw, ENETC_PRFSCAPR);
	num_entries = ENETC_PRFSCAPR_GET_NUM_RFS(val);
	vf_entries = num_entries / (pf->total_vfs + 1);

	for (i = 0; i < pf->total_vfs; i++)
		enetc_port_wr(hw, ENETC_PSIRFSCFGR(i + 1), vf_entries);
	enetc_port_wr(hw, ENETC_PSIRFSCFGR(0),
		      num_entries - vf_entries * pf->total_vfs);

	/* enable RFS on port */
	enetc_port_wr(hw, ENETC_PRFSMR, ENETC_PRFSMR_RFSE);
}

static void enetc_port_si_configure(struct enetc_si *si)
{
	struct enetc_pf *pf = enetc_si_priv(si);
	struct enetc_hw *hw = &si->hw;
	int num_rings, i;
	u32 val;

	val = enetc_port_rd(hw, ENETC_PCAPR0);
	num_rings = min(ENETC_PCAPR0_RXBDR(val), ENETC_PCAPR0_TXBDR(val));

	val = ENETC_PSICFGR0_SET_TXBDR(ENETC_PF_NUM_RINGS);
	val |= ENETC_PSICFGR0_SET_RXBDR(ENETC_PF_NUM_RINGS);

	if (unlikely(num_rings < ENETC_PF_NUM_RINGS)) {
		val = ENETC_PSICFGR0_SET_TXBDR(num_rings);
		val |= ENETC_PSICFGR0_SET_RXBDR(num_rings);

		dev_warn(&si->pdev->dev, "Found %d rings, expected %d!\n",
			 num_rings, ENETC_PF_NUM_RINGS);

		num_rings = 0;
	}

	/* Add default one-time settings for SI0 (PF) */
	val |= ENETC_PSICFGR0_SIVC(ENETC_VLAN_TYPE_C | ENETC_VLAN_TYPE_S);

	enetc_port_wr(hw, ENETC_PSICFGR0(0), val);

	if (num_rings)
		num_rings -= ENETC_PF_NUM_RINGS;

	/* Configure the SIs for each available VF */
	val = ENETC_PSICFGR0_SIVC(ENETC_VLAN_TYPE_C | ENETC_VLAN_TYPE_S);
	val |= ENETC_PSICFGR0_VTE | ENETC_PSICFGR0_SIVIE;

	if (num_rings) {
		num_rings /= pf->total_vfs;
		val |= ENETC_PSICFGR0_SET_TXBDR(num_rings);
		val |= ENETC_PSICFGR0_SET_RXBDR(num_rings);
	}

	for (i = 0; i < pf->total_vfs; i++)
		enetc_port_wr(hw, ENETC_PSICFGR0(i + 1), val);

	/* Port level VLAN settings */
	val = ENETC_PVCLCTR_OVTPIDL(ENETC_VLAN_TYPE_C | ENETC_VLAN_TYPE_S);
	enetc_port_wr(hw, ENETC_PVCLCTR, val);
	/* use outer tag for VLAN filtering */
	enetc_port_wr(hw, ENETC_PSIVLANFMR, ENETC_PSIVLANFMR_VS);
}

void enetc_set_ptcmsdur(struct enetc_hw *hw, u32 *max_sdu)
{
	int tc;

	for (tc = 0; tc < 8; tc++) {
		u32 val = ENETC_MAC_MAXFRM_SIZE;

		if (max_sdu[tc])
			val = max_sdu[tc] + VLAN_ETH_HLEN;

		enetc_port_wr(hw, ENETC_PTCMSDUR(tc), val);
	}
}

void enetc_reset_ptcmsdur(struct enetc_hw *hw)
{
	int tc;

	for (tc = 0; tc < 8; tc++)
		enetc_port_wr(hw, ENETC_PTCMSDUR(tc), ENETC_MAC_MAXFRM_SIZE);
}

static void enetc_configure_port_mac(struct enetc_si *si)
{
	struct enetc_hw *hw = &si->hw;

	enetc_port_mac_wr(si, ENETC_PM0_MAXFRM,
			  ENETC_SET_MAXFRM(ENETC_RX_MAXFRM_SIZE));

	enetc_reset_ptcmsdur(hw);

	enetc_port_mac_wr(si, ENETC_PM0_CMD_CFG, ENETC_PM0_CMD_PHY_TX_EN |
			  ENETC_PM0_CMD_TXP | ENETC_PM0_PROMISC);

	/* On LS1028A, the MAC RX FIFO defaults to 2, which is too high
	 * and may lead to RX lock-up under traffic. Set it to 1 instead,
	 * as recommended by the hardware team.
	 */
	enetc_port_mac_wr(si, ENETC_PM0_RX_FIFO, ENETC_PM0_RX_FIFO_VAL);
}

static void enetc_mac_config(struct enetc_si *si, phy_interface_t phy_mode)
{
	u32 val;

	if (phy_interface_mode_is_rgmii(phy_mode)) {
		val = enetc_port_mac_rd(si, ENETC_PM0_IF_MODE);
		val &= ~(ENETC_PM0_IFM_EN_AUTO | ENETC_PM0_IFM_IFMODE_MASK);
		val |= ENETC_PM0_IFM_IFMODE_GMII | ENETC_PM0_IFM_RG;
		enetc_port_mac_wr(si, ENETC_PM0_IF_MODE, val);
	}

	if (phy_mode == PHY_INTERFACE_MODE_USXGMII) {
		val = ENETC_PM0_IFM_FULL_DPX | ENETC_PM0_IFM_IFMODE_XGMII;
		enetc_port_mac_wr(si, ENETC_PM0_IF_MODE, val);
	}
}

static void enetc_mac_enable(struct enetc_si *si, bool en)
{
	u32 val = enetc_port_mac_rd(si, ENETC_PM0_CMD_CFG);

	val &= ~(ENETC_PM0_TX_EN | ENETC_PM0_RX_EN);
	val |= en ? (ENETC_PM0_TX_EN | ENETC_PM0_RX_EN) : 0;

	enetc_port_mac_wr(si, ENETC_PM0_CMD_CFG, val);
}

static void enetc_configure_port(struct enetc_pf *pf)
{
	u8 hash_key[ENETC_RSSHASH_KEY_SIZE];
	struct enetc_hw *hw = &pf->si->hw;

	enetc_configure_port_mac(pf->si);

	enetc_port_si_configure(pf->si);

	/* set up hash key */
	get_random_bytes(hash_key, ENETC_RSSHASH_KEY_SIZE);
	enetc_set_rss_key(hw, hash_key);

	/* split up RFS entries */
	enetc_port_assign_rfs_entries(pf->si);

	/* enforce VLAN promisc mode for all SIs */
	pf->vlan_promisc_simap = ENETC_VLAN_PROMISC_MAP_ALL;
	enetc_set_vlan_promisc(hw, pf->vlan_promisc_simap);

	enetc_port_wr(hw, ENETC_PSIPMR, 0);

	/* enable port */
	enetc_port_wr(hw, ENETC_PMR, ENETC_PMR_EN);
}

/* Messaging */
static u16 enetc_msg_pf_set_vf_primary_mac_addr(struct enetc_pf *pf,
						int vf_id)
{
	struct enetc_vf_state *vf_state = &pf->vf_state[vf_id];
	struct enetc_msg_swbd *msg = &pf->rxmsg[vf_id];
	struct enetc_msg_cmd_set_primary_mac *cmd;
	struct device *dev = &pf->si->pdev->dev;
	u16 cmd_id;
	char *addr;

	cmd = (struct enetc_msg_cmd_set_primary_mac *)msg->vaddr;
	cmd_id = cmd->header.id;
	if (cmd_id != ENETC_MSG_CMD_MNG_ADD)
		return ENETC_MSG_CMD_STATUS_FAIL;

	addr = cmd->mac.sa_data;
	if (vf_state->flags & ENETC_VF_FLAG_PF_SET_MAC)
		dev_warn(dev, "Attempt to override PF set mac addr for VF%d\n",
			 vf_id);
	else
		enetc_pf_set_primary_mac_addr(&pf->si->hw, vf_id + 1, addr);

	return ENETC_MSG_CMD_STATUS_OK;
}

void enetc_msg_handle_rxmsg(struct enetc_pf *pf, int vf_id, u16 *status)
{
	struct enetc_msg_swbd *msg = &pf->rxmsg[vf_id];
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_msg_cmd_header *cmd_hdr;
	u16 cmd_type;

	*status = ENETC_MSG_CMD_STATUS_OK;
	cmd_hdr = (struct enetc_msg_cmd_header *)msg->vaddr;
	cmd_type = cmd_hdr->type;

	switch (cmd_type) {
	case ENETC_MSG_CMD_MNG_MAC:
		*status = enetc_msg_pf_set_vf_primary_mac_addr(pf, vf_id);
		break;
	default:
		dev_err(dev, "command not supported (cmd_type: 0x%x)\n",
			cmd_type);
	}
}

#ifdef CONFIG_PCI_IOV
static int enetc_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_pf *pf = enetc_si_priv(si);
	int err;

	if (!num_vfs) {
		enetc_msg_psi_free(pf);
		kfree(pf->vf_state);
		pf->num_vfs = 0;
		pci_disable_sriov(pdev);
	} else {
		pf->num_vfs = num_vfs;

		pf->vf_state = kcalloc(num_vfs, sizeof(struct enetc_vf_state),
				       GFP_KERNEL);
		if (!pf->vf_state) {
			pf->num_vfs = 0;
			return -ENOMEM;
		}

		err = enetc_msg_psi_init(pf);
		if (err) {
			dev_err(&pdev->dev, "enetc_msg_psi_init (%d)\n", err);
			goto err_msg_psi;
		}

		err = pci_enable_sriov(pdev, num_vfs);
		if (err) {
			dev_err(&pdev->dev, "pci_enable_sriov err %d\n", err);
			goto err_en_sriov;
		}
	}

	return num_vfs;

err_en_sriov:
	enetc_msg_psi_free(pf);
err_msg_psi:
	kfree(pf->vf_state);
	pf->num_vfs = 0;

	return err;
}
#else
#define enetc_sriov_configure(pdev, num_vfs)	(void)0
#endif

static int enetc_pf_set_features(struct net_device *ndev,
				 netdev_features_t features)
{
	netdev_features_t changed = ndev->features ^ features;
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	int err;

	if (changed & NETIF_F_HW_TC) {
		err = enetc_set_psfp(ndev, !!(features & NETIF_F_HW_TC));
		if (err)
			return err;
	}

	if (changed & NETIF_F_HW_VLAN_CTAG_FILTER) {
		struct enetc_pf *pf = enetc_si_priv(priv->si);

		if (!!(features & NETIF_F_HW_VLAN_CTAG_FILTER))
			enetc_disable_si_vlan_promisc(pf, 0);
		else
			enetc_enable_si_vlan_promisc(pf, 0);
	}

	if (changed & NETIF_F_LOOPBACK)
		enetc_set_loopback(ndev, !!(features & NETIF_F_LOOPBACK));

	enetc_set_features(ndev, features);

	return 0;
}

static int enetc_pf_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			     void *type_data)
{
	switch (type) {
	case TC_QUERY_CAPS:
		return enetc_qos_query_caps(ndev, type_data);
	case TC_SETUP_QDISC_MQPRIO:
		return enetc_setup_tc_mqprio(ndev, type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return enetc_setup_tc_taprio(ndev, type_data);
	case TC_SETUP_QDISC_CBS:
		return enetc_setup_tc_cbs(ndev, type_data);
	case TC_SETUP_QDISC_ETF:
		return enetc_setup_tc_txtime(ndev, type_data);
	case TC_SETUP_BLOCK:
		return enetc_setup_tc_psfp(ndev, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops enetc_ndev_ops = {
	.ndo_open		= enetc_open,
	.ndo_stop		= enetc_close,
	.ndo_start_xmit		= enetc_xmit,
	.ndo_get_stats		= enetc_get_stats,
	.ndo_set_mac_address	= enetc_pf_set_mac_addr,
	.ndo_set_rx_mode	= enetc_pf_set_rx_mode,
	.ndo_vlan_rx_add_vid	= enetc_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= enetc_vlan_rx_del_vid,
	.ndo_set_vf_mac		= enetc_pf_set_vf_mac,
	.ndo_set_vf_vlan	= enetc_pf_set_vf_vlan,
	.ndo_set_vf_spoofchk	= enetc_pf_set_vf_spoofchk,
	.ndo_set_features	= enetc_pf_set_features,
	.ndo_eth_ioctl		= enetc_ioctl,
	.ndo_setup_tc		= enetc_pf_setup_tc,
	.ndo_bpf		= enetc_setup_bpf,
	.ndo_xdp_xmit		= enetc_xdp_xmit,
};

static void enetc_pf_netdev_setup(struct enetc_si *si, struct net_device *ndev,
				  const struct net_device_ops *ndev_ops)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);

	SET_NETDEV_DEV(ndev, &si->pdev->dev);
	priv->ndev = ndev;
	priv->si = si;
	priv->dev = &si->pdev->dev;
	si->ndev = ndev;

	priv->msg_enable = (NETIF_MSG_WOL << 1) - 1;
	ndev->netdev_ops = ndev_ops;
	enetc_set_ethtool_ops(ndev);
	ndev->watchdog_timeo = 5 * HZ;
	ndev->max_mtu = ENETC_MAX_MTU;

	ndev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM |
			    NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_LOOPBACK |
			    NETIF_F_HW_CSUM | NETIF_F_TSO | NETIF_F_TSO6;
	ndev->features = NETIF_F_HIGHDMA | NETIF_F_SG | NETIF_F_RXCSUM |
			 NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX |
			 NETIF_F_HW_CSUM | NETIF_F_TSO | NETIF_F_TSO6;
	ndev->vlan_features = NETIF_F_SG | NETIF_F_HW_CSUM |
			      NETIF_F_TSO | NETIF_F_TSO6;

	if (si->num_rss)
		ndev->hw_features |= NETIF_F_RXHASH;

	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->xdp_features = NETDEV_XDP_ACT_BASIC | NETDEV_XDP_ACT_REDIRECT |
			     NETDEV_XDP_ACT_NDO_XMIT | NETDEV_XDP_ACT_RX_SG |
			     NETDEV_XDP_ACT_NDO_XMIT_SG;

	if (si->hw_features & ENETC_SI_F_PSFP && !enetc_psfp_enable(priv)) {
		priv->active_offloads |= ENETC_F_QCI;
		ndev->features |= NETIF_F_HW_TC;
		ndev->hw_features |= NETIF_F_HW_TC;
	}

	/* pick up primary MAC address from SI */
	enetc_load_primary_mac_addr(&si->hw, ndev);
}

static int enetc_mdio_probe(struct enetc_pf *pf, struct device_node *np)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_mdio_priv *mdio_priv;
	struct mii_bus *bus;
	int err;

	bus = devm_mdiobus_alloc_size(dev, sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale ENETC MDIO Bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = &pf->si->hw;
	mdio_priv->mdio_base = ENETC_EMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	err = of_mdiobus_register(bus, np);
	if (err)
		return dev_err_probe(dev, err, "cannot register MDIO bus\n");

	pf->mdio = bus;

	return 0;
}

static void enetc_mdio_remove(struct enetc_pf *pf)
{
	if (pf->mdio)
		mdiobus_unregister(pf->mdio);
}

static int enetc_imdio_create(struct enetc_pf *pf)
{
	struct device *dev = &pf->si->pdev->dev;
	struct enetc_mdio_priv *mdio_priv;
	struct phylink_pcs *phylink_pcs;
	struct mii_bus *bus;
	int err;

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale ENETC internal MDIO Bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	bus->phy_mask = ~0;
	mdio_priv = bus->priv;
	mdio_priv->hw = &pf->si->hw;
	mdio_priv->mdio_base = ENETC_PM_IMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-imdio", dev_name(dev));

	err = mdiobus_register(bus);
	if (err) {
		dev_err(dev, "cannot register internal MDIO bus (%d)\n", err);
		goto free_mdio_bus;
	}

	phylink_pcs = lynx_pcs_create_mdiodev(bus, 0);
	if (IS_ERR(phylink_pcs)) {
		err = PTR_ERR(phylink_pcs);
		dev_err(dev, "cannot create lynx pcs (%d)\n", err);
		goto unregister_mdiobus;
	}

	pf->imdio = bus;
	pf->pcs = phylink_pcs;

	return 0;

unregister_mdiobus:
	mdiobus_unregister(bus);
free_mdio_bus:
	mdiobus_free(bus);
	return err;
}

static void enetc_imdio_remove(struct enetc_pf *pf)
{
	if (pf->pcs)
		lynx_pcs_destroy(pf->pcs);
	if (pf->imdio) {
		mdiobus_unregister(pf->imdio);
		mdiobus_free(pf->imdio);
	}
}

static bool enetc_port_has_pcs(struct enetc_pf *pf)
{
	return (pf->if_mode == PHY_INTERFACE_MODE_SGMII ||
		pf->if_mode == PHY_INTERFACE_MODE_1000BASEX ||
		pf->if_mode == PHY_INTERFACE_MODE_2500BASEX ||
		pf->if_mode == PHY_INTERFACE_MODE_USXGMII);
}

static int enetc_mdiobus_create(struct enetc_pf *pf, struct device_node *node)
{
	struct device_node *mdio_np;
	int err;

	mdio_np = of_get_child_by_name(node, "mdio");
	if (mdio_np) {
		err = enetc_mdio_probe(pf, mdio_np);

		of_node_put(mdio_np);
		if (err)
			return err;
	}

	if (enetc_port_has_pcs(pf)) {
		err = enetc_imdio_create(pf);
		if (err) {
			enetc_mdio_remove(pf);
			return err;
		}
	}

	return 0;
}

static void enetc_mdiobus_destroy(struct enetc_pf *pf)
{
	enetc_mdio_remove(pf);
	enetc_imdio_remove(pf);
}

static struct phylink_pcs *
enetc_pl_mac_select_pcs(struct phylink_config *config, phy_interface_t iface)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);

	return pf->pcs;
}

static void enetc_pl_mac_config(struct phylink_config *config,
				unsigned int mode,
				const struct phylink_link_state *state)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);

	enetc_mac_config(pf->si, state->interface);
}

static void enetc_force_rgmii_mac(struct enetc_si *si, int speed, int duplex)
{
	u32 old_val, val;

	old_val = val = enetc_port_mac_rd(si, ENETC_PM0_IF_MODE);

	if (speed == SPEED_1000) {
		val &= ~ENETC_PM0_IFM_SSP_MASK;
		val |= ENETC_PM0_IFM_SSP_1000;
	} else if (speed == SPEED_100) {
		val &= ~ENETC_PM0_IFM_SSP_MASK;
		val |= ENETC_PM0_IFM_SSP_100;
	} else if (speed == SPEED_10) {
		val &= ~ENETC_PM0_IFM_SSP_MASK;
		val |= ENETC_PM0_IFM_SSP_10;
	}

	if (duplex == DUPLEX_FULL)
		val |= ENETC_PM0_IFM_FULL_DPX;
	else
		val &= ~ENETC_PM0_IFM_FULL_DPX;

	if (val == old_val)
		return;

	enetc_port_mac_wr(si, ENETC_PM0_IF_MODE, val);
}

static void enetc_pl_mac_link_up(struct phylink_config *config,
				 struct phy_device *phy, unsigned int mode,
				 phy_interface_t interface, int speed,
				 int duplex, bool tx_pause, bool rx_pause)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);
	u32 pause_off_thresh = 0, pause_on_thresh = 0;
	u32 init_quanta = 0, refresh_quanta = 0;
	struct enetc_hw *hw = &pf->si->hw;
	struct enetc_si *si = pf->si;
	struct enetc_ndev_priv *priv;
	u32 rbmr, cmd_cfg;
	int idx;

	priv = netdev_priv(pf->si->ndev);

	if (pf->si->hw_features & ENETC_SI_F_QBV)
		enetc_sched_speed_set(priv, speed);

	if (!phylink_autoneg_inband(mode) &&
	    phy_interface_mode_is_rgmii(interface))
		enetc_force_rgmii_mac(si, speed, duplex);

	/* Flow control */
	for (idx = 0; idx < priv->num_rx_rings; idx++) {
		rbmr = enetc_rxbdr_rd(hw, idx, ENETC_RBMR);

		if (tx_pause)
			rbmr |= ENETC_RBMR_CM;
		else
			rbmr &= ~ENETC_RBMR_CM;

		enetc_rxbdr_wr(hw, idx, ENETC_RBMR, rbmr);
	}

	if (tx_pause) {
		/* When the port first enters congestion, send a PAUSE request
		 * with the maximum number of quanta. When the port exits
		 * congestion, it will automatically send a PAUSE frame with
		 * zero quanta.
		 */
		init_quanta = 0xffff;

		/* Also, set up the refresh timer to send follow-up PAUSE
		 * frames at half the quanta value, in case the congestion
		 * condition persists.
		 */
		refresh_quanta = 0xffff / 2;

		/* Start emitting PAUSE frames when 3 large frames (or more
		 * smaller frames) have accumulated in the FIFO waiting to be
		 * DMAed to the RX ring.
		 */
		pause_on_thresh = 3 * ENETC_MAC_MAXFRM_SIZE;
		pause_off_thresh = 1 * ENETC_MAC_MAXFRM_SIZE;
	}

	enetc_port_mac_wr(si, ENETC_PM0_PAUSE_QUANTA, init_quanta);
	enetc_port_mac_wr(si, ENETC_PM0_PAUSE_THRESH, refresh_quanta);
	enetc_port_wr(hw, ENETC_PPAUONTR, pause_on_thresh);
	enetc_port_wr(hw, ENETC_PPAUOFFTR, pause_off_thresh);

	cmd_cfg = enetc_port_mac_rd(si, ENETC_PM0_CMD_CFG);

	if (rx_pause)
		cmd_cfg &= ~ENETC_PM0_PAUSE_IGN;
	else
		cmd_cfg |= ENETC_PM0_PAUSE_IGN;

	enetc_port_mac_wr(si, ENETC_PM0_CMD_CFG, cmd_cfg);

	enetc_mac_enable(si, true);

	if (si->hw_features & ENETC_SI_F_QBU)
		enetc_mm_link_state_update(priv, true);
}

static void enetc_pl_mac_link_down(struct phylink_config *config,
				   unsigned int mode,
				   phy_interface_t interface)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);
	struct enetc_si *si = pf->si;
	struct enetc_ndev_priv *priv;

	priv = netdev_priv(si->ndev);

	if (si->hw_features & ENETC_SI_F_QBU)
		enetc_mm_link_state_update(priv, false);

	enetc_mac_enable(si, false);
}

static const struct phylink_mac_ops enetc_mac_phylink_ops = {
	.mac_select_pcs = enetc_pl_mac_select_pcs,
	.mac_config = enetc_pl_mac_config,
	.mac_link_up = enetc_pl_mac_link_up,
	.mac_link_down = enetc_pl_mac_link_down,
};

static int enetc_phylink_create(struct enetc_ndev_priv *priv,
				struct device_node *node)
{
	struct enetc_pf *pf = enetc_si_priv(priv->si);
	struct phylink *phylink;
	int err;

	pf->phylink_config.dev = &priv->ndev->dev;
	pf->phylink_config.type = PHYLINK_NETDEV;
	pf->phylink_config.mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000 | MAC_2500FD;

	__set_bit(PHY_INTERFACE_MODE_INTERNAL,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX,
		  pf->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_USXGMII,
		  pf->phylink_config.supported_interfaces);
	phy_interface_set_rgmii(pf->phylink_config.supported_interfaces);

	phylink = phylink_create(&pf->phylink_config, of_fwnode_handle(node),
				 pf->if_mode, &enetc_mac_phylink_ops);
	if (IS_ERR(phylink)) {
		err = PTR_ERR(phylink);
		return err;
	}

	priv->phylink = phylink;

	return 0;
}

static void enetc_phylink_destroy(struct enetc_ndev_priv *priv)
{
	phylink_destroy(priv->phylink);
}

/* Initialize the entire shared memory for the flow steering entries
 * of this port (PF + VFs)
 */
static int enetc_init_port_rfs_memory(struct enetc_si *si)
{
	struct enetc_cmd_rfse rfse = {0};
	struct enetc_hw *hw = &si->hw;
	int num_rfs, i, err = 0;
	u32 val;

	val = enetc_port_rd(hw, ENETC_PRFSCAPR);
	num_rfs = ENETC_PRFSCAPR_GET_NUM_RFS(val);

	for (i = 0; i < num_rfs; i++) {
		err = enetc_set_fs_entry(si, &rfse, i);
		if (err)
			break;
	}

	return err;
}

static int enetc_init_port_rss_memory(struct enetc_si *si)
{
	struct enetc_hw *hw = &si->hw;
	int num_rss, err;
	int *rss_table;
	u32 val;

	val = enetc_port_rd(hw, ENETC_PRSSCAPR);
	num_rss = ENETC_PRSSCAPR_GET_NUM_RSS(val);
	if (!num_rss)
		return 0;

	rss_table = kcalloc(num_rss, sizeof(*rss_table), GFP_KERNEL);
	if (!rss_table)
		return -ENOMEM;

	err = enetc_set_rss_table(si, rss_table, num_rss);

	kfree(rss_table);

	return err;
}

static int enetc_pf_register_with_ierb(struct pci_dev *pdev)
{
	struct platform_device *ierb_pdev;
	struct device_node *ierb_node;

	ierb_node = of_find_compatible_node(NULL, NULL,
					    "fsl,ls1028a-enetc-ierb");
	if (!ierb_node || !of_device_is_available(ierb_node))
		return -ENODEV;

	ierb_pdev = of_find_device_by_node(ierb_node);
	of_node_put(ierb_node);

	if (!ierb_pdev)
		return -EPROBE_DEFER;

	return enetc_ierb_register_pf(ierb_pdev, pdev);
}

static struct enetc_si *enetc_psi_create(struct pci_dev *pdev)
{
	struct enetc_si *si;
	int err;

	err = enetc_pci_probe(pdev, KBUILD_MODNAME, sizeof(struct enetc_pf));
	if (err) {
		dev_err_probe(&pdev->dev, err, "PCI probing failed\n");
		goto out;
	}

	si = pci_get_drvdata(pdev);
	if (!si->hw.port || !si->hw.global) {
		err = -ENODEV;
		dev_err(&pdev->dev, "could not map PF space, probing a VF?\n");
		goto out_pci_remove;
	}

	err = enetc_setup_cbdr(&pdev->dev, &si->hw, ENETC_CBDR_DEFAULT_SIZE,
			       &si->cbd_ring);
	if (err)
		goto out_pci_remove;

	err = enetc_init_port_rfs_memory(si);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize RFS memory\n");
		goto out_teardown_cbdr;
	}

	err = enetc_init_port_rss_memory(si);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize RSS memory\n");
		goto out_teardown_cbdr;
	}

	return si;

out_teardown_cbdr:
	enetc_teardown_cbdr(&si->cbd_ring);
out_pci_remove:
	enetc_pci_remove(pdev);
out:
	return ERR_PTR(err);
}

static void enetc_psi_destroy(struct pci_dev *pdev)
{
	struct enetc_si *si = pci_get_drvdata(pdev);

	enetc_teardown_cbdr(&si->cbd_ring);
	enetc_pci_remove(pdev);
}

static int enetc_pf_probe(struct pci_dev *pdev,
			  const struct pci_device_id *ent)
{
	struct device_node *node = pdev->dev.of_node;
	struct enetc_ndev_priv *priv;
	struct net_device *ndev;
	struct enetc_si *si;
	struct enetc_pf *pf;
	int err;

	err = enetc_pf_register_with_ierb(pdev);
	if (err == -EPROBE_DEFER)
		return err;
	if (err)
		dev_warn(&pdev->dev,
			 "Could not register with IERB driver: %pe, please update the device tree\n",
			 ERR_PTR(err));

	si = enetc_psi_create(pdev);
	if (IS_ERR(si)) {
		err = PTR_ERR(si);
		goto err_psi_create;
	}

	pf = enetc_si_priv(si);
	pf->si = si;
	pf->total_vfs = pci_sriov_get_totalvfs(pdev);

	err = enetc_setup_mac_addresses(node, pf);
	if (err)
		goto err_setup_mac_addresses;

	enetc_configure_port(pf);

	enetc_get_si_caps(si);

	ndev = alloc_etherdev_mq(sizeof(*priv), ENETC_MAX_NUM_TXQS);
	if (!ndev) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "netdev creation failed\n");
		goto err_alloc_netdev;
	}

	enetc_pf_netdev_setup(si, ndev, &enetc_ndev_ops);

	priv = netdev_priv(ndev);

	mutex_init(&priv->mm_lock);

	enetc_init_si_rings_params(priv);

	err = enetc_alloc_si_resources(priv);
	if (err) {
		dev_err(&pdev->dev, "SI resource alloc failed\n");
		goto err_alloc_si_res;
	}

	err = enetc_configure_si(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to configure SI\n");
		goto err_config_si;
	}

	err = enetc_alloc_msix(priv);
	if (err) {
		dev_err(&pdev->dev, "MSIX alloc failed\n");
		goto err_alloc_msix;
	}

	err = of_get_phy_mode(node, &pf->if_mode);
	if (err) {
		dev_err(&pdev->dev, "Failed to read PHY mode\n");
		goto err_phy_mode;
	}

	err = enetc_mdiobus_create(pf, node);
	if (err)
		goto err_mdiobus_create;

	err = enetc_phylink_create(priv, node);
	if (err)
		goto err_phylink_create;

	err = register_netdev(ndev);
	if (err)
		goto err_reg_netdev;

	return 0;

err_reg_netdev:
	enetc_phylink_destroy(priv);
err_phylink_create:
	enetc_mdiobus_destroy(pf);
err_mdiobus_create:
err_phy_mode:
	enetc_free_msix(priv);
err_config_si:
err_alloc_msix:
	enetc_free_si_resources(priv);
err_alloc_si_res:
	si->ndev = NULL;
	free_netdev(ndev);
err_alloc_netdev:
err_setup_mac_addresses:
	enetc_psi_destroy(pdev);
err_psi_create:
	return err;
}

static void enetc_pf_remove(struct pci_dev *pdev)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_pf *pf = enetc_si_priv(si);
	struct enetc_ndev_priv *priv;

	priv = netdev_priv(si->ndev);

	if (pf->num_vfs)
		enetc_sriov_configure(pdev, 0);

	unregister_netdev(si->ndev);

	enetc_phylink_destroy(priv);
	enetc_mdiobus_destroy(pf);

	enetc_free_msix(priv);

	enetc_free_si_resources(priv);

	free_netdev(si->ndev);

	enetc_psi_destroy(pdev);
}

static void enetc_fixup_clear_rss_rfs(struct pci_dev *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct enetc_si *si;

	/* Only apply quirk for disabled functions. For the ones
	 * that are enabled, enetc_pf_probe() will apply it.
	 */
	if (node && of_device_is_available(node))
		return;

	si = enetc_psi_create(pdev);
	if (!IS_ERR(si))
		enetc_psi_destroy(pdev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_FREESCALE, ENETC_DEV_ID_PF,
			enetc_fixup_clear_rss_rfs);

static const struct pci_device_id enetc_pf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, ENETC_DEV_ID_PF) },
	{ 0, } /* End of table. */
};
MODULE_DEVICE_TABLE(pci, enetc_pf_id_table);

static struct pci_driver enetc_pf_driver = {
	.name = KBUILD_MODNAME,
	.id_table = enetc_pf_id_table,
	.probe = enetc_pf_probe,
	.remove = enetc_pf_remove,
#ifdef CONFIG_PCI_IOV
	.sriov_configure = enetc_sriov_configure,
#endif
};
module_pci_driver(enetc_pf_driver);

MODULE_DESCRIPTION(ENETC_DRV_NAME_STR);
MODULE_LICENSE("Dual BSD/GPL");
