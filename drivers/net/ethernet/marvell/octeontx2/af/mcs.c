// SPDX-License-Identifier: GPL-2.0
/* Marvell MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mcs.h"
#include "mcs_reg.h"

#define DRV_NAME	"Marvell MCS Driver"

#define PCI_CFG_REG_BAR_NUM	0

static const struct pci_device_id mcs_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_CN10K_MCS) },
	{ 0, }  /* end of table */
};

static LIST_HEAD(mcs_list);

void mcs_pn_table_write(struct mcs *mcs, u8 pn_id, u64 next_pn, u8 dir)
{
	u64 reg;

	if (dir == MCS_RX)
		reg = MCSX_CPM_RX_SLAVE_SA_PN_TABLE_MEMX(pn_id);
	else
		reg = MCSX_CPM_TX_SLAVE_SA_PN_TABLE_MEMX(pn_id);
	mcs_reg_write(mcs, reg, next_pn);
}

void cn10kb_mcs_tx_sa_mem_map_write(struct mcs *mcs, struct mcs_tx_sc_sa_map *map)
{
	u64 reg, val;

	val = (map->sa_index0 & 0xFF) |
	      (map->sa_index1 & 0xFF) << 9 |
	      (map->rekey_ena & 0x1) << 18 |
	      (map->sa_index0_vld & 0x1) << 19 |
	      (map->sa_index1_vld & 0x1) << 20 |
	      (map->tx_sa_active & 0x1) << 21 |
	      map->sectag_sci << 22;
	reg = MCSX_CPM_TX_SLAVE_SA_MAP_MEM_0X(map->sc_id);
	mcs_reg_write(mcs, reg, val);

	val = map->sectag_sci >> 42;
	reg = MCSX_CPM_TX_SLAVE_SA_MAP_MEM_1X(map->sc_id);
	mcs_reg_write(mcs, reg, val);
}

void cn10kb_mcs_rx_sa_mem_map_write(struct mcs *mcs, struct mcs_rx_sc_sa_map *map)
{
	u64 val, reg;

	val = (map->sa_index & 0xFF) | map->sa_in_use << 9;

	reg = MCSX_CPM_RX_SLAVE_SA_MAP_MEMX((4 * map->sc_id) + map->an);
	mcs_reg_write(mcs, reg, val);
}

void mcs_sa_plcy_write(struct mcs *mcs, u64 *plcy, int sa_id, int dir)
{
	int reg_id;
	u64 reg;

	if (dir == MCS_RX) {
		for (reg_id = 0; reg_id < 8; reg_id++) {
			reg =  MCSX_CPM_RX_SLAVE_SA_PLCY_MEMX(reg_id, sa_id);
			mcs_reg_write(mcs, reg, plcy[reg_id]);
		}
	} else {
		for (reg_id = 0; reg_id < 9; reg_id++) {
			reg =  MCSX_CPM_TX_SLAVE_SA_PLCY_MEMX(reg_id, sa_id);
			mcs_reg_write(mcs, reg, plcy[reg_id]);
		}
	}
}

void mcs_ena_dis_sc_cam_entry(struct mcs *mcs, int sc_id, int ena)
{
	u64 reg, val;

	reg = MCSX_CPM_RX_SLAVE_SC_CAM_ENA(0);
	if (sc_id > 63)
		reg = MCSX_CPM_RX_SLAVE_SC_CAM_ENA(1);

	if (ena)
		val = mcs_reg_read(mcs, reg) | BIT_ULL(sc_id);
	else
		val = mcs_reg_read(mcs, reg) & ~BIT_ULL(sc_id);

	mcs_reg_write(mcs, reg, val);
}

void mcs_rx_sc_cam_write(struct mcs *mcs, u64 sci, u64 secy, int sc_id)
{
	mcs_reg_write(mcs, MCSX_CPM_RX_SLAVE_SC_CAMX(0, sc_id), sci);
	mcs_reg_write(mcs, MCSX_CPM_RX_SLAVE_SC_CAMX(1, sc_id), secy);
	/* Enable SC CAM */
	mcs_ena_dis_sc_cam_entry(mcs, sc_id, true);
}

void mcs_secy_plcy_write(struct mcs *mcs, u64 plcy, int secy_id, int dir)
{
	u64 reg;

	if (dir == MCS_RX)
		reg = MCSX_CPM_RX_SLAVE_SECY_PLCY_MEM_0X(secy_id);
	else
		reg = MCSX_CPM_TX_SLAVE_SECY_PLCY_MEMX(secy_id);

	mcs_reg_write(mcs, reg, plcy);

	if (mcs->hw->mcs_blks == 1 && dir == MCS_RX)
		mcs_reg_write(mcs, MCSX_CPM_RX_SLAVE_SECY_PLCY_MEM_1X(secy_id), 0x0ull);
}

void cn10kb_mcs_flowid_secy_map(struct mcs *mcs, struct secy_mem_map *map, int dir)
{
	u64 reg, val;

	val = (map->secy & 0x7F) | (map->ctrl_pkt & 0x1) << 8;
	if (dir == MCS_RX) {
		reg = MCSX_CPM_RX_SLAVE_SECY_MAP_MEMX(map->flow_id);
	} else {
		val |= (map->sc & 0x7F) << 9;
		reg = MCSX_CPM_TX_SLAVE_SECY_MAP_MEM_0X(map->flow_id);
	}

	mcs_reg_write(mcs, reg, val);
}

void mcs_ena_dis_flowid_entry(struct mcs *mcs, int flow_id, int dir, int ena)
{
	u64 reg, val;

	if (dir == MCS_RX) {
		reg = MCSX_CPM_RX_SLAVE_FLOWID_TCAM_ENA_0;
		if (flow_id > 63)
			reg = MCSX_CPM_RX_SLAVE_FLOWID_TCAM_ENA_1;
	} else {
		reg = MCSX_CPM_TX_SLAVE_FLOWID_TCAM_ENA_0;
		if (flow_id > 63)
			reg = MCSX_CPM_TX_SLAVE_FLOWID_TCAM_ENA_1;
	}

	/* Enable/Disable the tcam entry */
	if (ena)
		val = mcs_reg_read(mcs, reg) | BIT_ULL(flow_id);
	else
		val = mcs_reg_read(mcs, reg) & ~BIT_ULL(flow_id);

	mcs_reg_write(mcs, reg, val);
}

void mcs_flowid_entry_write(struct mcs *mcs, u64 *data, u64 *mask, int flow_id, int dir)
{
	int reg_id;
	u64 reg;

	if (dir == MCS_RX) {
		for (reg_id = 0; reg_id < 4; reg_id++) {
			reg = MCSX_CPM_RX_SLAVE_FLOWID_TCAM_DATAX(reg_id, flow_id);
			mcs_reg_write(mcs, reg, data[reg_id]);
			reg = MCSX_CPM_RX_SLAVE_FLOWID_TCAM_MASKX(reg_id, flow_id);
			mcs_reg_write(mcs, reg, mask[reg_id]);
		}
	} else {
		for (reg_id = 0; reg_id < 4; reg_id++) {
			reg = MCSX_CPM_TX_SLAVE_FLOWID_TCAM_DATAX(reg_id, flow_id);
			mcs_reg_write(mcs, reg, data[reg_id]);
			reg = MCSX_CPM_TX_SLAVE_FLOWID_TCAM_MASKX(reg_id, flow_id);
			mcs_reg_write(mcs, reg, mask[reg_id]);
		}
	}
}

void mcs_clear_secy_plcy(struct mcs *mcs, int secy_id, int dir)
{
	struct mcs_rsrc_map *map;
	int flow_id;

	if (dir == MCS_RX)
		map = &mcs->rx;
	else
		map = &mcs->tx;

	/* Clear secy memory to zero */
	mcs_secy_plcy_write(mcs, 0, secy_id, dir);

	/* Disable the tcam entry using this secy */
	for (flow_id = 0; flow_id < map->flow_ids.max; flow_id++) {
		if (map->flowid2secy_map[flow_id] != secy_id)
			continue;
		mcs_ena_dis_flowid_entry(mcs, flow_id, dir, false);
	}
}

int mcs_alloc_ctrlpktrule(struct rsrc_bmap *rsrc, u16 *pf_map, u16 offset, u16 pcifunc)
{
	int rsrc_id;

	if (!rsrc->bmap)
		return -EINVAL;

	rsrc_id = bitmap_find_next_zero_area(rsrc->bmap, rsrc->max, offset, 1, 0);
	if (rsrc_id >= rsrc->max)
		return -ENOSPC;

	bitmap_set(rsrc->bmap, rsrc_id, 1);
	pf_map[rsrc_id] = pcifunc;

	return rsrc_id;
}

int mcs_free_ctrlpktrule(struct mcs *mcs, struct mcs_free_ctrl_pkt_rule_req *req)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct mcs_rsrc_map *map;
	u64 dis, reg;
	int id, rc;

	reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_RULE_ENABLE : MCSX_PEX_TX_SLAVE_RULE_ENABLE;
	map = (req->dir == MCS_RX) ? &mcs->rx : &mcs->tx;

	if (req->all) {
		for (id = 0; id < map->ctrlpktrule.max; id++) {
			if (map->ctrlpktrule2pf_map[id] != pcifunc)
				continue;
			mcs_free_rsrc(&map->ctrlpktrule, map->ctrlpktrule2pf_map, id, pcifunc);
			dis = mcs_reg_read(mcs, reg);
			dis &= ~BIT_ULL(id);
			mcs_reg_write(mcs, reg, dis);
		}
		return 0;
	}

	rc = mcs_free_rsrc(&map->ctrlpktrule, map->ctrlpktrule2pf_map, req->rule_idx, pcifunc);
	dis = mcs_reg_read(mcs, reg);
	dis &= ~BIT_ULL(req->rule_idx);
	mcs_reg_write(mcs, reg, dis);

	return rc;
}

int mcs_ctrlpktrule_write(struct mcs *mcs, struct mcs_ctrl_pkt_rule_write_req *req)
{
	u64 reg, enb;
	u64 idx;

	switch (req->rule_type) {
	case MCS_CTRL_PKT_RULE_TYPE_ETH:
		req->data0 &= GENMASK(15, 0);
		if (req->data0 != ETH_P_PAE)
			return -EINVAL;

		idx = req->rule_idx - MCS_CTRLPKT_ETYPE_RULE_OFFSET;
		reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_RULE_ETYPE_CFGX(idx) :
		      MCSX_PEX_TX_SLAVE_RULE_ETYPE_CFGX(idx);

		mcs_reg_write(mcs, reg, req->data0);
		break;
	case MCS_CTRL_PKT_RULE_TYPE_DA:
		if (!(req->data0 & BIT_ULL(40)))
			return -EINVAL;

		idx = req->rule_idx - MCS_CTRLPKT_DA_RULE_OFFSET;
		reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_RULE_DAX(idx) :
		      MCSX_PEX_TX_SLAVE_RULE_DAX(idx);

		mcs_reg_write(mcs, reg, req->data0 & GENMASK_ULL(47, 0));
		break;
	case MCS_CTRL_PKT_RULE_TYPE_RANGE:
		if (!(req->data0 & BIT_ULL(40)) || !(req->data1 & BIT_ULL(40)))
			return -EINVAL;

		idx = req->rule_idx - MCS_CTRLPKT_DA_RANGE_RULE_OFFSET;
		if (req->dir == MCS_RX) {
			reg = MCSX_PEX_RX_SLAVE_RULE_DA_RANGE_MINX(idx);
			mcs_reg_write(mcs, reg, req->data0 & GENMASK_ULL(47, 0));
			reg = MCSX_PEX_RX_SLAVE_RULE_DA_RANGE_MAXX(idx);
			mcs_reg_write(mcs, reg, req->data1 & GENMASK_ULL(47, 0));
		} else {
			reg = MCSX_PEX_TX_SLAVE_RULE_DA_RANGE_MINX(idx);
			mcs_reg_write(mcs, reg, req->data0 & GENMASK_ULL(47, 0));
			reg = MCSX_PEX_TX_SLAVE_RULE_DA_RANGE_MAXX(idx);
			mcs_reg_write(mcs, reg, req->data1 & GENMASK_ULL(47, 0));
		}
		break;
	case MCS_CTRL_PKT_RULE_TYPE_COMBO:
		req->data2 &= GENMASK(15, 0);
		if (req->data2 != ETH_P_PAE || !(req->data0 & BIT_ULL(40)) ||
		    !(req->data1 & BIT_ULL(40)))
			return -EINVAL;

		idx = req->rule_idx - MCS_CTRLPKT_COMBO_RULE_OFFSET;
		if (req->dir == MCS_RX) {
			reg = MCSX_PEX_RX_SLAVE_RULE_COMBO_MINX(idx);
			mcs_reg_write(mcs, reg, req->data0 & GENMASK_ULL(47, 0));
			reg = MCSX_PEX_RX_SLAVE_RULE_COMBO_MAXX(idx);
			mcs_reg_write(mcs, reg, req->data1 & GENMASK_ULL(47, 0));
			reg = MCSX_PEX_RX_SLAVE_RULE_COMBO_ETX(idx);
			mcs_reg_write(mcs, reg, req->data2);
		} else {
			reg = MCSX_PEX_TX_SLAVE_RULE_COMBO_MINX(idx);
			mcs_reg_write(mcs, reg, req->data0 & GENMASK_ULL(47, 0));
			reg = MCSX_PEX_TX_SLAVE_RULE_COMBO_MAXX(idx);
			mcs_reg_write(mcs, reg, req->data1 & GENMASK_ULL(47, 0));
			reg = MCSX_PEX_TX_SLAVE_RULE_COMBO_ETX(idx);
			mcs_reg_write(mcs, reg, req->data2);
		}
		break;
	case MCS_CTRL_PKT_RULE_TYPE_MAC:
		if (!(req->data0 & BIT_ULL(40)))
			return -EINVAL;

		idx = req->rule_idx - MCS_CTRLPKT_MAC_EN_RULE_OFFSET;
		reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_RULE_MAC :
		      MCSX_PEX_TX_SLAVE_RULE_MAC;

		mcs_reg_write(mcs, reg, req->data0 & GENMASK_ULL(47, 0));
		break;
	}

	reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_RULE_ENABLE : MCSX_PEX_TX_SLAVE_RULE_ENABLE;

	enb = mcs_reg_read(mcs, reg);
	enb |= BIT_ULL(req->rule_idx);
	mcs_reg_write(mcs, reg, enb);

	return 0;
}

int mcs_free_rsrc(struct rsrc_bmap *rsrc, u16 *pf_map, int rsrc_id, u16 pcifunc)
{
	/* Check if the rsrc_id is mapped to PF/VF */
	if (pf_map[rsrc_id] != pcifunc)
		return -EINVAL;

	rvu_free_rsrc(rsrc, rsrc_id);
	pf_map[rsrc_id] = 0;
	return 0;
}

/* Free all the cam resources mapped to pf */
int mcs_free_all_rsrc(struct mcs *mcs, int dir, u16 pcifunc)
{
	struct mcs_rsrc_map *map;
	int id;

	if (dir == MCS_RX)
		map = &mcs->rx;
	else
		map = &mcs->tx;

	/* free tcam entries */
	for (id = 0; id < map->flow_ids.max; id++) {
		if (map->flowid2pf_map[id] != pcifunc)
			continue;
		mcs_free_rsrc(&map->flow_ids, map->flowid2pf_map,
			      id, pcifunc);
		mcs_ena_dis_flowid_entry(mcs, id, dir, false);
	}

	/* free secy entries */
	for (id = 0; id < map->secy.max; id++) {
		if (map->secy2pf_map[id] != pcifunc)
			continue;
		mcs_free_rsrc(&map->secy, map->secy2pf_map,
			      id, pcifunc);
		mcs_clear_secy_plcy(mcs, id, dir);
	}

	/* free sc entries */
	for (id = 0; id < map->secy.max; id++) {
		if (map->sc2pf_map[id] != pcifunc)
			continue;
		mcs_free_rsrc(&map->sc, map->sc2pf_map, id, pcifunc);

		/* Disable SC CAM only on RX side */
		if (dir == MCS_RX)
			mcs_ena_dis_sc_cam_entry(mcs, id, false);
	}

	/* free sa entries */
	for (id = 0; id < map->sa.max; id++) {
		if (map->sa2pf_map[id] != pcifunc)
			continue;
		mcs_free_rsrc(&map->sa, map->sa2pf_map, id, pcifunc);
	}
	return 0;
}

int mcs_alloc_rsrc(struct rsrc_bmap *rsrc, u16 *pf_map, u16 pcifunc)
{
	int rsrc_id;

	rsrc_id = rvu_alloc_rsrc(rsrc);
	if (rsrc_id < 0)
		return -ENOMEM;
	pf_map[rsrc_id] = pcifunc;
	return rsrc_id;
}

int mcs_alloc_all_rsrc(struct mcs *mcs, u8 *flow_id, u8 *secy_id,
		       u8 *sc_id, u8 *sa1_id, u8 *sa2_id, u16 pcifunc, int dir)
{
	struct mcs_rsrc_map *map;
	int id;

	if (dir == MCS_RX)
		map = &mcs->rx;
	else
		map = &mcs->tx;

	id = mcs_alloc_rsrc(&map->flow_ids, map->flowid2pf_map, pcifunc);
	if (id < 0)
		return -ENOMEM;
	*flow_id = id;

	id = mcs_alloc_rsrc(&map->secy, map->secy2pf_map, pcifunc);
	if (id < 0)
		return -ENOMEM;
	*secy_id = id;

	id = mcs_alloc_rsrc(&map->sc, map->sc2pf_map, pcifunc);
	if (id < 0)
		return -ENOMEM;
	*sc_id = id;

	id =  mcs_alloc_rsrc(&map->sa, map->sa2pf_map, pcifunc);
	if (id < 0)
		return -ENOMEM;
	*sa1_id = id;

	id =  mcs_alloc_rsrc(&map->sa, map->sa2pf_map, pcifunc);
	if (id < 0)
		return -ENOMEM;
	*sa2_id = id;

	return 0;
}

static void *alloc_mem(struct mcs *mcs, int n)
{
	return devm_kcalloc(mcs->dev, n, sizeof(u16), GFP_KERNEL);
}

static int mcs_alloc_struct_mem(struct mcs *mcs, struct mcs_rsrc_map *res)
{
	struct hwinfo *hw = mcs->hw;
	int err;

	res->flowid2pf_map = alloc_mem(mcs, hw->tcam_entries);
	if (!res->flowid2pf_map)
		return -ENOMEM;

	res->secy2pf_map = alloc_mem(mcs, hw->secy_entries);
	if (!res->secy2pf_map)
		return -ENOMEM;

	res->sc2pf_map = alloc_mem(mcs, hw->sc_entries);
	if (!res->sc2pf_map)
		return -ENOMEM;

	res->sa2pf_map = alloc_mem(mcs, hw->sa_entries);
	if (!res->sa2pf_map)
		return -ENOMEM;

	res->flowid2secy_map = alloc_mem(mcs, hw->tcam_entries);
	if (!res->flowid2secy_map)
		return -ENOMEM;

	res->ctrlpktrule2pf_map = alloc_mem(mcs, MCS_MAX_CTRLPKT_RULES);
	if (!res->ctrlpktrule2pf_map)
		return -ENOMEM;

	res->flow_ids.max = hw->tcam_entries - MCS_RSRC_RSVD_CNT;
	err = rvu_alloc_bitmap(&res->flow_ids);
	if (err)
		return err;

	res->secy.max = hw->secy_entries - MCS_RSRC_RSVD_CNT;
	err = rvu_alloc_bitmap(&res->secy);
	if (err)
		return err;

	res->sc.max = hw->sc_entries;
	err = rvu_alloc_bitmap(&res->sc);
	if (err)
		return err;

	res->sa.max = hw->sa_entries;
	err = rvu_alloc_bitmap(&res->sa);
	if (err)
		return err;

	res->ctrlpktrule.max = MCS_MAX_CTRLPKT_RULES;
	err = rvu_alloc_bitmap(&res->ctrlpktrule);
	if (err)
		return err;

	return 0;
}

int mcs_get_blkcnt(void)
{
	struct mcs *mcs;
	int idmax = -ENODEV;

	/* Check MCS block is present in hardware */
	if (!pci_dev_present(mcs_id_table))
		return 0;

	list_for_each_entry(mcs, &mcs_list, mcs_list)
		if (mcs->mcs_id > idmax)
			idmax = mcs->mcs_id;

	if (idmax < 0)
		return 0;

	return idmax + 1;
}

struct mcs *mcs_get_pdata(int mcs_id)
{
	struct mcs *mcs_dev;

	list_for_each_entry(mcs_dev, &mcs_list, mcs_list) {
		if (mcs_dev->mcs_id == mcs_id)
			return mcs_dev;
	}
	return NULL;
}

void mcs_set_port_cfg(struct mcs *mcs, struct mcs_port_cfg_set_req *req)
{
	u64 val = 0;

	mcs_reg_write(mcs, MCSX_PAB_RX_SLAVE_PORT_CFGX(req->port_id),
		      req->port_mode & MCS_PORT_MODE_MASK);

	req->cstm_tag_rel_mode_sel &= 0x3;

	if (mcs->hw->mcs_blks > 1) {
		req->fifo_skid &= MCS_PORT_FIFO_SKID_MASK;
		val = (u32)req->fifo_skid << 0x10;
		val |= req->fifo_skid;
		mcs_reg_write(mcs, MCSX_PAB_RX_SLAVE_FIFO_SKID_CFGX(req->port_id), val);
		mcs_reg_write(mcs, MCSX_PEX_TX_SLAVE_CUSTOM_TAG_REL_MODE_SEL(req->port_id),
			      req->cstm_tag_rel_mode_sel);
		val = mcs_reg_read(mcs, MCSX_PEX_RX_SLAVE_PEX_CONFIGURATION);

		if (req->custom_hdr_enb)
			val |= BIT_ULL(req->port_id);
		else
			val &= ~BIT_ULL(req->port_id);

		mcs_reg_write(mcs, MCSX_PEX_RX_SLAVE_PEX_CONFIGURATION, val);
	} else {
		val = mcs_reg_read(mcs, MCSX_PEX_TX_SLAVE_PORT_CONFIG(req->port_id));
		val |= (req->cstm_tag_rel_mode_sel << 2);
		mcs_reg_write(mcs, MCSX_PEX_TX_SLAVE_PORT_CONFIG(req->port_id), val);
	}
}

void mcs_get_port_cfg(struct mcs *mcs, struct mcs_port_cfg_get_req *req,
		      struct mcs_port_cfg_get_rsp *rsp)
{
	u64 reg = 0;

	rsp->port_mode = mcs_reg_read(mcs, MCSX_PAB_RX_SLAVE_PORT_CFGX(req->port_id)) &
			 MCS_PORT_MODE_MASK;

	if (mcs->hw->mcs_blks > 1) {
		reg = MCSX_PAB_RX_SLAVE_FIFO_SKID_CFGX(req->port_id);
		rsp->fifo_skid = mcs_reg_read(mcs, reg) & MCS_PORT_FIFO_SKID_MASK;
		reg = MCSX_PEX_TX_SLAVE_CUSTOM_TAG_REL_MODE_SEL(req->port_id);
		rsp->cstm_tag_rel_mode_sel = mcs_reg_read(mcs, reg) & 0x3;
		if (mcs_reg_read(mcs, MCSX_PEX_RX_SLAVE_PEX_CONFIGURATION) & BIT_ULL(req->port_id))
			rsp->custom_hdr_enb = 1;
	} else {
		reg = MCSX_PEX_TX_SLAVE_PORT_CONFIG(req->port_id);
		rsp->cstm_tag_rel_mode_sel = mcs_reg_read(mcs, reg) >> 2;
	}

	rsp->port_id = req->port_id;
	rsp->mcs_id = req->mcs_id;
}

void mcs_get_custom_tag_cfg(struct mcs *mcs, struct mcs_custom_tag_cfg_get_req *req,
			    struct mcs_custom_tag_cfg_get_rsp *rsp)
{
	u64 reg = 0, val = 0;
	u8 idx;

	for (idx = 0; idx < MCS_MAX_CUSTOM_TAGS; idx++) {
		if (mcs->hw->mcs_blks > 1)
			reg  = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_CUSTOM_TAGX(idx) :
				MCSX_PEX_TX_SLAVE_CUSTOM_TAGX(idx);
		else
			reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_VLAN_CFGX(idx) :
				MCSX_PEX_TX_SLAVE_VLAN_CFGX(idx);

		val = mcs_reg_read(mcs, reg);
		if (mcs->hw->mcs_blks > 1) {
			rsp->cstm_etype[idx] = val & GENMASK(15, 0);
			rsp->cstm_indx[idx] = (val >> 0x16) & 0x3;
			reg = (req->dir == MCS_RX) ? MCSX_PEX_RX_SLAVE_ETYPE_ENABLE :
				MCSX_PEX_TX_SLAVE_ETYPE_ENABLE;
			rsp->cstm_etype_en = mcs_reg_read(mcs, reg) & 0xFF;
		} else {
			rsp->cstm_etype[idx] = (val >> 0x1) & GENMASK(15, 0);
			rsp->cstm_indx[idx] = (val >> 0x11) & 0x3;
			rsp->cstm_etype_en |= (val & 0x1) << idx;
		}
	}

	rsp->mcs_id = req->mcs_id;
	rsp->dir = req->dir;
}

void mcs_reset_port(struct mcs *mcs, u8 port_id, u8 reset)
{
	u64 reg = MCSX_MCS_TOP_SLAVE_PORT_RESET(port_id);

	mcs_reg_write(mcs, reg, reset & 0x1);
}

/* Set lmac to bypass/operational mode */
void mcs_set_lmac_mode(struct mcs *mcs, int lmac_id, u8 mode)
{
	u64 reg;

	reg = MCSX_MCS_TOP_SLAVE_CHANNEL_CFG(lmac_id * 2);
	mcs_reg_write(mcs, reg, (u64)mode);
}

void mcs_pn_threshold_set(struct mcs *mcs, struct mcs_set_pn_threshold *pn)
{
	u64 reg;

	if (pn->dir == MCS_RX)
		reg = pn->xpn ? MCSX_CPM_RX_SLAVE_XPN_THRESHOLD : MCSX_CPM_RX_SLAVE_PN_THRESHOLD;
	else
		reg = pn->xpn ? MCSX_CPM_TX_SLAVE_XPN_THRESHOLD : MCSX_CPM_TX_SLAVE_PN_THRESHOLD;

	mcs_reg_write(mcs, reg, pn->threshold);
}

void cn10kb_mcs_parser_cfg(struct mcs *mcs)
{
	u64 reg, val;

	/* VLAN CTag */
	val = BIT_ULL(0) | (0x8100ull & 0xFFFF) << 1 | BIT_ULL(17);
	/* RX */
	reg = MCSX_PEX_RX_SLAVE_VLAN_CFGX(0);
	mcs_reg_write(mcs, reg, val);

	/* TX */
	reg = MCSX_PEX_TX_SLAVE_VLAN_CFGX(0);
	mcs_reg_write(mcs, reg, val);

	/* VLAN STag */
	val = BIT_ULL(0) | (0x88a8ull & 0xFFFF) << 1 | BIT_ULL(18);
	/* RX */
	reg = MCSX_PEX_RX_SLAVE_VLAN_CFGX(1);
	mcs_reg_write(mcs, reg, val);

	/* TX */
	reg = MCSX_PEX_TX_SLAVE_VLAN_CFGX(1);
	mcs_reg_write(mcs, reg, val);
}

static void mcs_lmac_init(struct mcs *mcs, int lmac_id)
{
	u64 reg;

	/* Port mode 25GB */
	reg = MCSX_PAB_RX_SLAVE_PORT_CFGX(lmac_id);
	mcs_reg_write(mcs, reg, 0);

	if (mcs->hw->mcs_blks > 1) {
		reg = MCSX_PAB_RX_SLAVE_FIFO_SKID_CFGX(lmac_id);
		mcs_reg_write(mcs, reg, 0xe000e);
		return;
	}

	reg = MCSX_PAB_TX_SLAVE_PORT_CFGX(lmac_id);
	mcs_reg_write(mcs, reg, 0);
}

int mcs_set_lmac_channels(int mcs_id, u16 base)
{
	struct mcs *mcs;
	int lmac;
	u64 cfg;

	mcs = mcs_get_pdata(mcs_id);
	if (!mcs)
		return -ENODEV;
	for (lmac = 0; lmac < mcs->hw->lmac_cnt; lmac++) {
		cfg = mcs_reg_read(mcs, MCSX_LINK_LMACX_CFG(lmac));
		cfg &= ~(MCSX_LINK_LMAC_BASE_MASK | MCSX_LINK_LMAC_RANGE_MASK);
		cfg |=	FIELD_PREP(MCSX_LINK_LMAC_RANGE_MASK, ilog2(16));
		cfg |=	FIELD_PREP(MCSX_LINK_LMAC_BASE_MASK, base);
		mcs_reg_write(mcs, MCSX_LINK_LMACX_CFG(lmac), cfg);
		base += 16;
	}
	return 0;
}

static int mcs_x2p_calibration(struct mcs *mcs)
{
	unsigned long timeout = jiffies + usecs_to_jiffies(20000);
	int i, err = 0;
	u64 val;

	/* set X2P calibration */
	val = mcs_reg_read(mcs, MCSX_MIL_GLOBAL);
	val |= BIT_ULL(5);
	mcs_reg_write(mcs, MCSX_MIL_GLOBAL, val);

	/* Wait for calibration to complete */
	while (!(mcs_reg_read(mcs, MCSX_MIL_RX_GBL_STATUS) & BIT_ULL(0))) {
		if (time_before(jiffies, timeout)) {
			usleep_range(80, 100);
			continue;
		} else {
			err = -EBUSY;
			dev_err(mcs->dev, "MCS X2P calibration failed..ignoring\n");
			return err;
		}
	}

	val = mcs_reg_read(mcs, MCSX_MIL_RX_GBL_STATUS);
	for (i = 0; i < mcs->hw->mcs_x2p_intf; i++) {
		if (val & BIT_ULL(1 + i))
			continue;
		err = -EBUSY;
		dev_err(mcs->dev, "MCS:%d didn't respond to X2P calibration\n", i);
	}
	/* Clear X2P calibrate */
	mcs_reg_write(mcs, MCSX_MIL_GLOBAL, mcs_reg_read(mcs, MCSX_MIL_GLOBAL) & ~BIT_ULL(5));

	return err;
}

static void mcs_set_external_bypass(struct mcs *mcs, u8 bypass)
{
	u64 val;

	/* Set MCS to external bypass */
	val = mcs_reg_read(mcs, MCSX_MIL_GLOBAL);
	if (bypass)
		val |= BIT_ULL(6);
	else
		val &= ~BIT_ULL(6);
	mcs_reg_write(mcs, MCSX_MIL_GLOBAL, val);
}

static void mcs_global_cfg(struct mcs *mcs)
{
	/* Disable external bypass */
	mcs_set_external_bypass(mcs, false);

	/* Set MCS to perform standard IEEE802.1AE macsec processing */
	if (mcs->hw->mcs_blks == 1) {
		mcs_reg_write(mcs, MCSX_IP_MODE, BIT_ULL(3));
		return;
	}

	mcs_reg_write(mcs, MCSX_BBE_RX_SLAVE_CAL_ENTRY, 0xe4);
	mcs_reg_write(mcs, MCSX_BBE_RX_SLAVE_CAL_LEN, 4);
}

void cn10kb_mcs_set_hw_capabilities(struct mcs *mcs)
{
	struct hwinfo *hw = mcs->hw;

	hw->tcam_entries = 128;		/* TCAM entries */
	hw->secy_entries  = 128;	/* SecY entries */
	hw->sc_entries = 128;		/* SC CAM entries */
	hw->sa_entries = 256;		/* SA entries */
	hw->lmac_cnt = 20;		/* lmacs/ports per mcs block */
	hw->mcs_x2p_intf = 5;		/* x2p clabration intf */
	hw->mcs_blks = 1;		/* MCS blocks */
}

static struct mcs_ops cn10kb_mcs_ops = {
	.mcs_set_hw_capabilities	= cn10kb_mcs_set_hw_capabilities,
	.mcs_parser_cfg			= cn10kb_mcs_parser_cfg,
	.mcs_tx_sa_mem_map_write	= cn10kb_mcs_tx_sa_mem_map_write,
	.mcs_rx_sa_mem_map_write	= cn10kb_mcs_rx_sa_mem_map_write,
	.mcs_flowid_secy_map		= cn10kb_mcs_flowid_secy_map,
};

static int mcs_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	int lmac, err = 0;
	struct mcs *mcs;

	mcs = devm_kzalloc(dev, sizeof(*mcs), GFP_KERNEL);
	if (!mcs)
		return -ENOMEM;

	mcs->hw = devm_kzalloc(dev, sizeof(struct hwinfo), GFP_KERNEL);
	if (!mcs->hw)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		pci_set_drvdata(pdev, NULL);
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		goto exit;
	}

	mcs->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!mcs->reg_base) {
		dev_err(dev, "mcs: Cannot map CSR memory space, aborting\n");
		err = -ENOMEM;
		goto exit;
	}

	pci_set_drvdata(pdev, mcs);
	mcs->pdev = pdev;
	mcs->dev = &pdev->dev;

	if (pdev->subsystem_device == PCI_SUBSYS_DEVID_CN10K_B)
		mcs->mcs_ops = &cn10kb_mcs_ops;
	else
		mcs->mcs_ops = cnf10kb_get_mac_ops();

	/* Set hardware capabilities */
	mcs->mcs_ops->mcs_set_hw_capabilities(mcs);

	mcs_global_cfg(mcs);

	/* Perform X2P clibration */
	err = mcs_x2p_calibration(mcs);
	if (err)
		goto err_x2p;

	mcs->mcs_id = (pci_resource_start(pdev, PCI_CFG_REG_BAR_NUM) >> 24)
			& MCS_ID_MASK;

	/* Set mcs tx side resources */
	err = mcs_alloc_struct_mem(mcs, &mcs->tx);
	if (err)
		goto err_x2p;

	/* Set mcs rx side resources */
	err = mcs_alloc_struct_mem(mcs, &mcs->rx);
	if (err)
		goto err_x2p;

	/* per port config */
	for (lmac = 0; lmac < mcs->hw->lmac_cnt; lmac++)
		mcs_lmac_init(mcs, lmac);

	/* Parser configuration */
	mcs->mcs_ops->mcs_parser_cfg(mcs);

	list_add(&mcs->mcs_list, &mcs_list);

	return 0;

err_x2p:
	/* Enable external bypass */
	mcs_set_external_bypass(mcs, true);
exit:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void mcs_remove(struct pci_dev *pdev)
{
	struct mcs *mcs = pci_get_drvdata(pdev);

	/* Set MCS to external bypass */
	mcs_set_external_bypass(mcs, true);
	pci_free_irq_vectors(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

struct pci_driver mcs_driver = {
	.name = DRV_NAME,
	.id_table = mcs_id_table,
	.probe = mcs_probe,
	.remove = mcs_remove,
};
