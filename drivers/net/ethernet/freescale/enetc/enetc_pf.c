// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/fsl/enetc_mdio.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include "enetc_pf.h"

#define ENETC_DRV_NAME_STR "ENETC PF driver"

static void enetc_pf_get_primary_mac_addr(struct enetc_hw *hw, int si, u8 *addr)
{
	u32 upper = __raw_readl(hw->port + ENETC_PSIPMAR0(si));
	u16 lower = __raw_readw(hw->port + ENETC_PSIPMAR1(si));

	*(u32 *)addr = upper;
	*(u16 *)(addr + 4) = lower;
}

static void enetc_pf_set_primary_mac_addr(struct enetc_hw *hw, int si,
					  const u8 *addr)
{
	u32 upper = *(const u32 *)addr;
	u16 lower = *(const u16 *)(addr + 4);

	__raw_writel(upper, hw->port + ENETC_PSIPMAR0(si));
	__raw_writew(lower, hw->port + ENETC_PSIPMAR1(si));
}

static int enetc_pf_set_mac_addr(struct net_device *ndev, void *addr)
{
	struct enetc_ndev_priv *priv = netdev_priv(ndev);
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(ndev->dev_addr, saddr->sa_data, ndev->addr_len);
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
				 u32 *hash)
{
	bool err = si->errata & ENETC_ERR_UCMCSWP;

	if (type == UC) {
		enetc_port_wr(&si->hw, ENETC_PSIUMHFR0(si_idx, err), *hash);
		enetc_port_wr(&si->hw, ENETC_PSIUMHFR1(si_idx), *(hash + 1));
	} else { /* MC */
		enetc_port_wr(&si->hw, ENETC_PSIMMHFR0(si_idx, err), *hash);
		enetc_port_wr(&si->hw, ENETC_PSIMMHFR1(si_idx), *(hash + 1));
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

		enetc_set_mac_ht_flt(si, 0, i, (u32 *)f->mac_hash_table);
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
				     u32 *hash)
{
	enetc_port_wr(hw, ENETC_PSIVHFR0(si_idx), *hash);
	enetc_port_wr(hw, ENETC_PSIVHFR1(si_idx), *(hash + 1));
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

	enetc_set_vlan_ht_filter(&pf->si->hw, 0, (u32 *)pf->vlan_ht_filter);
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
	struct enetc_hw *hw = &priv->si->hw;
	u32 reg;

	reg = enetc_port_rd(hw, ENETC_PM0_IF_MODE);
	if (reg & ENETC_PM0_IFM_RG) {
		/* RGMII mode */
		reg = (reg & ~ENETC_PM0_IFM_RLP) |
		      (en ? ENETC_PM0_IFM_RLP : 0);
		enetc_port_wr(hw, ENETC_PM0_IF_MODE, reg);
	} else {
		/* assume SGMII mode */
		reg = enetc_port_rd(hw, ENETC_PM0_CMD_CFG);
		reg = (reg & ~ENETC_PM0_CMD_XGLP) |
		      (en ? ENETC_PM0_CMD_XGLP : 0);
		reg = (reg & ~ENETC_PM0_CMD_PHY_TX_EN) |
		      (en ? ENETC_PM0_CMD_PHY_TX_EN : 0);
		enetc_port_wr(hw, ENETC_PM0_CMD_CFG, reg);
		enetc_port_wr(hw, ENETC_PM1_CMD_CFG, reg);
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

static void enetc_port_setup_primary_mac_address(struct enetc_si *si)
{
	unsigned char mac_addr[MAX_ADDR_LEN];
	struct enetc_pf *pf = enetc_si_priv(si);
	struct enetc_hw *hw = &si->hw;
	int i;

	/* check MAC addresses for PF and all VFs, if any is 0 set it ro rand */
	for (i = 0; i < pf->total_vfs + 1; i++) {
		enetc_pf_get_primary_mac_addr(hw, i, mac_addr);
		if (!is_zero_ether_addr(mac_addr))
			continue;
		eth_random_addr(mac_addr);
		dev_info(&si->pdev->dev, "no MAC address specified for SI%d, using %pM\n",
			 i, mac_addr);
		enetc_pf_set_primary_mac_addr(hw, i, mac_addr);
	}
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

static void enetc_configure_port_mac(struct enetc_hw *hw)
{
	enetc_port_wr(hw, ENETC_PM0_MAXFRM,
		      ENETC_SET_MAXFRM(ENETC_RX_MAXFRM_SIZE));

	enetc_port_wr(hw, ENETC_PTCMSDUR(0), ENETC_MAC_MAXFRM_SIZE);
	enetc_port_wr(hw, ENETC_PTXMBAR, 2 * ENETC_MAC_MAXFRM_SIZE);

	enetc_port_wr(hw, ENETC_PM0_CMD_CFG, ENETC_PM0_CMD_PHY_TX_EN |
		      ENETC_PM0_CMD_TXP	| ENETC_PM0_PROMISC);

	enetc_port_wr(hw, ENETC_PM1_CMD_CFG, ENETC_PM0_CMD_PHY_TX_EN |
		      ENETC_PM0_CMD_TXP	| ENETC_PM0_PROMISC);

	/* On LS1028A, the MAC RX FIFO defaults to 2, which is too high
	 * and may lead to RX lock-up under traffic. Set it to 1 instead,
	 * as recommended by the hardware team.
	 */
	enetc_port_wr(hw, ENETC_PM0_RX_FIFO, ENETC_PM0_RX_FIFO_VAL);
}

static void enetc_mac_config(struct enetc_hw *hw, phy_interface_t phy_mode)
{
	u32 val;

	if (phy_interface_mode_is_rgmii(phy_mode)) {
		val = enetc_port_rd(hw, ENETC_PM0_IF_MODE);
		val &= ~(ENETC_PM0_IFM_EN_AUTO | ENETC_PM0_IFM_IFMODE_MASK);
		val |= ENETC_PM0_IFM_IFMODE_GMII | ENETC_PM0_IFM_RG;
		enetc_port_wr(hw, ENETC_PM0_IF_MODE, val);
	}

	if (phy_mode == PHY_INTERFACE_MODE_USXGMII) {
		val = ENETC_PM0_IFM_FULL_DPX | ENETC_PM0_IFM_IFMODE_XGMII;
		enetc_port_wr(hw, ENETC_PM0_IF_MODE, val);
	}
}

static void enetc_mac_enable(struct enetc_hw *hw, bool en)
{
	u32 val = enetc_port_rd(hw, ENETC_PM0_CMD_CFG);

	val &= ~(ENETC_PM0_TX_EN | ENETC_PM0_RX_EN);
	val |= en ? (ENETC_PM0_TX_EN | ENETC_PM0_RX_EN) : 0;

	enetc_port_wr(hw, ENETC_PM0_CMD_CFG, val);
	enetc_port_wr(hw, ENETC_PM1_CMD_CFG, val);
}

static void enetc_configure_port_pmac(struct enetc_hw *hw)
{
	u32 temp;

	/* Set pMAC step lock */
	temp = enetc_port_rd(hw, ENETC_PFPMR);
	enetc_port_wr(hw, ENETC_PFPMR,
		      temp | ENETC_PFPMR_PMACE | ENETC_PFPMR_MWLM);

	temp = enetc_port_rd(hw, ENETC_MMCSR);
	enetc_port_wr(hw, ENETC_MMCSR, temp | ENETC_MMCSR_ME);
}

static void enetc_configure_port(struct enetc_pf *pf)
{
	u8 hash_key[ENETC_RSSHASH_KEY_SIZE];
	struct enetc_hw *hw = &pf->si->hw;

	enetc_configure_port_pmac(hw);

	enetc_configure_port_mac(hw);

	enetc_port_si_configure(pf->si);

	/* set up hash key */
	get_random_bytes(hash_key, ENETC_RSSHASH_KEY_SIZE);
	enetc_set_rss_key(hw, hash_key);

	/* split up RFS entries */
	enetc_port_assign_rfs_entries(pf->si);

	/* fix-up primary MAC addresses, if not set already */
	enetc_port_setup_primary_mac_address(pf->si);

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
	.ndo_do_ioctl		= enetc_ioctl,
	.ndo_setup_tc		= enetc_setup_tc,
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

	ndev->hw_features = NETIF_F_SG | NETIF_F_RXCSUM | NETIF_F_HW_CSUM |
			    NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_LOOPBACK;
	ndev->features = NETIF_F_HIGHDMA | NETIF_F_SG |
			 NETIF_F_RXCSUM | NETIF_F_HW_CSUM |
			 NETIF_F_HW_VLAN_CTAG_TX |
			 NETIF_F_HW_VLAN_CTAG_RX;

	if (si->num_rss)
		ndev->hw_features |= NETIF_F_RXHASH;

	if (si->errata & ENETC_ERR_TXCSUM) {
		ndev->hw_features &= ~NETIF_F_HW_CSUM;
		ndev->features &= ~NETIF_F_HW_CSUM;
	}

	ndev->priv_flags |= IFF_UNICAST_FLT;

	if (si->hw_features & ENETC_SI_F_QBV)
		priv->active_offloads |= ENETC_F_QBV;

	if (si->hw_features & ENETC_SI_F_PSFP && !enetc_psfp_enable(priv)) {
		priv->active_offloads |= ENETC_F_QCI;
		ndev->features |= NETIF_F_HW_TC;
		ndev->hw_features |= NETIF_F_HW_TC;
	}

	/* pick up primary MAC address from SI */
	enetc_get_primary_mac_addr(&si->hw, ndev->dev_addr);
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
	bus->read = enetc_mdio_read;
	bus->write = enetc_mdio_write;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = &pf->si->hw;
	mdio_priv->mdio_base = ENETC_EMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	err = of_mdiobus_register(bus, np);
	if (err) {
		dev_err(dev, "cannot register MDIO bus\n");
		return err;
	}

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
	struct lynx_pcs *pcs_lynx;
	struct mdio_device *pcs;
	struct mii_bus *bus;
	int err;

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "Freescale ENETC internal MDIO Bus";
	bus->read = enetc_mdio_read;
	bus->write = enetc_mdio_write;
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

	pcs = mdio_device_create(bus, 0);
	if (IS_ERR(pcs)) {
		err = PTR_ERR(pcs);
		dev_err(dev, "cannot create pcs (%d)\n", err);
		goto unregister_mdiobus;
	}

	pcs_lynx = lynx_pcs_create(pcs);
	if (!pcs_lynx) {
		mdio_device_free(pcs);
		err = -ENOMEM;
		dev_err(dev, "cannot create lynx pcs (%d)\n", err);
		goto unregister_mdiobus;
	}

	pf->imdio = bus;
	pf->pcs = pcs_lynx;

	return 0;

unregister_mdiobus:
	mdiobus_unregister(bus);
free_mdio_bus:
	mdiobus_free(bus);
	return err;
}

static void enetc_imdio_remove(struct enetc_pf *pf)
{
	if (pf->pcs) {
		mdio_device_free(pf->pcs->mdio);
		lynx_pcs_destroy(pf->pcs);
	}
	if (pf->imdio) {
		mdiobus_unregister(pf->imdio);
		mdiobus_free(pf->imdio);
	}
}

static bool enetc_port_has_pcs(struct enetc_pf *pf)
{
	return (pf->if_mode == PHY_INTERFACE_MODE_SGMII ||
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

static void enetc_pl_mac_validate(struct phylink_config *config,
				  unsigned long *supported,
				  struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    state->interface != PHY_INTERFACE_MODE_INTERNAL &&
	    state->interface != PHY_INTERFACE_MODE_SGMII &&
	    state->interface != PHY_INTERFACE_MODE_2500BASEX &&
	    state->interface != PHY_INTERFACE_MODE_USXGMII &&
	    !phy_interface_mode_is_rgmii(state->interface)) {
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		return;
	}

	phylink_set_port_modes(mask);
	phylink_set(mask, Autoneg);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 1000baseT_Half);
	phylink_set(mask, 1000baseT_Full);

	if (state->interface == PHY_INTERFACE_MODE_INTERNAL ||
	    state->interface == PHY_INTERFACE_MODE_2500BASEX ||
	    state->interface == PHY_INTERFACE_MODE_USXGMII) {
		phylink_set(mask, 2500baseT_Full);
		phylink_set(mask, 2500baseX_Full);
	}

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void enetc_pl_mac_config(struct phylink_config *config,
				unsigned int mode,
				const struct phylink_link_state *state)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);
	struct enetc_ndev_priv *priv;

	enetc_mac_config(&pf->si->hw, state->interface);

	priv = netdev_priv(pf->si->ndev);
	if (pf->pcs)
		phylink_set_pcs(priv->phylink, &pf->pcs->pcs);
}

static void enetc_force_rgmii_mac(struct enetc_hw *hw, int speed, int duplex)
{
	u32 old_val, val;

	old_val = val = enetc_port_rd(hw, ENETC_PM0_IF_MODE);

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

	enetc_port_wr(hw, ENETC_PM0_IF_MODE, val);
}

static void enetc_pl_mac_link_up(struct phylink_config *config,
				 struct phy_device *phy, unsigned int mode,
				 phy_interface_t interface, int speed,
				 int duplex, bool tx_pause, bool rx_pause)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);
	struct enetc_ndev_priv *priv;

	priv = netdev_priv(pf->si->ndev);
	if (priv->active_offloads & ENETC_F_QBV)
		enetc_sched_speed_set(priv, speed);

	if (!phylink_autoneg_inband(mode) &&
	    phy_interface_mode_is_rgmii(interface))
		enetc_force_rgmii_mac(&pf->si->hw, speed, duplex);

	enetc_mac_enable(&pf->si->hw, true);
}

static void enetc_pl_mac_link_down(struct phylink_config *config,
				   unsigned int mode,
				   phy_interface_t interface)
{
	struct enetc_pf *pf = phylink_to_enetc_pf(config);

	enetc_mac_enable(&pf->si->hw, false);
}

static const struct phylink_mac_ops enetc_mac_phylink_ops = {
	.validate = enetc_pl_mac_validate,
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
	if (priv->phylink)
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

static void enetc_init_unused_port(struct enetc_si *si)
{
	struct device *dev = &si->pdev->dev;
	struct enetc_hw *hw = &si->hw;
	int err;

	si->cbd_ring.bd_count = ENETC_CBDR_DEFAULT_SIZE;
	err = enetc_alloc_cbdr(dev, &si->cbd_ring);
	if (err)
		return;

	enetc_setup_cbdr(hw, &si->cbd_ring);

	enetc_init_port_rfs_memory(si);
	enetc_init_port_rss_memory(si);

	enetc_clear_cbdr(hw);
	enetc_free_cbdr(dev, &si->cbd_ring);
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

	err = enetc_pci_probe(pdev, KBUILD_MODNAME, sizeof(*pf));
	if (err) {
		dev_err(&pdev->dev, "PCI probing failed\n");
		return err;
	}

	si = pci_get_drvdata(pdev);
	if (!si->hw.port || !si->hw.global) {
		err = -ENODEV;
		dev_err(&pdev->dev, "could not map PF space, probing a VF?\n");
		goto err_map_pf_space;
	}

	if (node && !of_device_is_available(node)) {
		enetc_init_unused_port(si);
		dev_info(&pdev->dev, "device is disabled, skipping\n");
		err = -ENODEV;
		goto err_device_disabled;
	}

	pf = enetc_si_priv(si);
	pf->si = si;
	pf->total_vfs = pci_sriov_get_totalvfs(pdev);

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

	enetc_init_si_rings_params(priv);

	err = enetc_alloc_si_resources(priv);
	if (err) {
		dev_err(&pdev->dev, "SI resource alloc failed\n");
		goto err_alloc_si_res;
	}

	err = enetc_init_port_rfs_memory(si);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize RFS memory\n");
		goto err_init_port_rfs;
	}

	err = enetc_init_port_rss_memory(si);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize RSS memory\n");
		goto err_init_port_rss;
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

	if (!of_get_phy_mode(node, &pf->if_mode)) {
		err = enetc_mdiobus_create(pf, node);
		if (err)
			goto err_mdiobus_create;

		err = enetc_phylink_create(priv, node);
		if (err)
			goto err_phylink_create;
	}

	err = register_netdev(ndev);
	if (err)
		goto err_reg_netdev;

	return 0;

err_reg_netdev:
	enetc_phylink_destroy(priv);
err_phylink_create:
	enetc_mdiobus_destroy(pf);
err_mdiobus_create:
	enetc_free_msix(priv);
err_config_si:
err_init_port_rss:
err_init_port_rfs:
err_alloc_msix:
	enetc_free_si_resources(priv);
err_alloc_si_res:
	si->ndev = NULL;
	free_netdev(ndev);
err_alloc_netdev:
err_device_disabled:
err_map_pf_space:
	enetc_pci_remove(pdev);

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

	enetc_pci_remove(pdev);
}

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
