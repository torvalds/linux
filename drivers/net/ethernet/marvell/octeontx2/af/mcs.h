/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell CN10K MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#ifndef MCS_H
#define MCS_H

#include <linux/bits.h>
#include "rvu.h"

#define PCI_DEVID_CN10K_MCS		0xA096

#define MCSX_LINK_LMAC_RANGE_MASK	GENMASK_ULL(19, 16)
#define MCSX_LINK_LMAC_BASE_MASK	GENMASK_ULL(11, 0)

#define MCS_ID_MASK			0x7
#define MCS_MAX_PFS                     128

#define MCS_PORT_MODE_MASK		0x3
#define MCS_PORT_FIFO_SKID_MASK		0x3F
#define MCS_MAX_CUSTOM_TAGS		0x8

#define MCS_CTRLPKT_ETYPE_RULE_MAX	8
#define MCS_CTRLPKT_DA_RULE_MAX		8
#define MCS_CTRLPKT_DA_RANGE_RULE_MAX	4
#define MCS_CTRLPKT_COMBO_RULE_MAX	4
#define MCS_CTRLPKT_MAC_RULE_MAX	1

#define MCS_MAX_CTRLPKT_RULES	(MCS_CTRLPKT_ETYPE_RULE_MAX + \
				MCS_CTRLPKT_DA_RULE_MAX + \
				MCS_CTRLPKT_DA_RANGE_RULE_MAX + \
				MCS_CTRLPKT_COMBO_RULE_MAX + \
				MCS_CTRLPKT_MAC_RULE_MAX)

#define MCS_CTRLPKT_ETYPE_RULE_OFFSET		0
#define MCS_CTRLPKT_DA_RULE_OFFSET		8
#define MCS_CTRLPKT_DA_RANGE_RULE_OFFSET	16
#define MCS_CTRLPKT_COMBO_RULE_OFFSET		20
#define MCS_CTRLPKT_MAC_EN_RULE_OFFSET		24

/* Reserved resources for default bypass entry */
#define MCS_RSRC_RSVD_CNT		1

/* MCS Interrupt Vector Enumeration */
enum mcs_int_vec_e {
	MCS_INT_VEC_MIL_RX_GBL		= 0x0,
	MCS_INT_VEC_MIL_RX_LMACX	= 0x1,
	MCS_INT_VEC_MIL_TX_LMACX	= 0x5,
	MCS_INT_VEC_HIL_RX_GBL		= 0x9,
	MCS_INT_VEC_HIL_RX_LMACX	= 0xa,
	MCS_INT_VEC_HIL_TX_GBL		= 0xe,
	MCS_INT_VEC_HIL_TX_LMACX	= 0xf,
	MCS_INT_VEC_IP			= 0x13,
	MCS_INT_VEC_CNT			= 0x14,
};

#define MCS_MAX_BBE_INT			8ULL
#define MCS_BBE_INT_MASK		0xFFULL

#define MCS_MAX_PAB_INT			4ULL
#define MCS_PAB_INT_MASK		0xFULL

#define MCS_BBE_RX_INT_ENA		BIT_ULL(0)
#define MCS_BBE_TX_INT_ENA		BIT_ULL(1)
#define MCS_CPM_RX_INT_ENA		BIT_ULL(2)
#define MCS_CPM_TX_INT_ENA		BIT_ULL(3)
#define MCS_PAB_RX_INT_ENA		BIT_ULL(4)
#define MCS_PAB_TX_INT_ENA		BIT_ULL(5)

#define MCS_CPM_TX_INT_PACKET_XPN_EQ0		BIT_ULL(0)
#define MCS_CPM_TX_INT_PN_THRESH_REACHED	BIT_ULL(1)
#define MCS_CPM_TX_INT_SA_NOT_VALID		BIT_ULL(2)

#define MCS_CPM_RX_INT_SECTAG_V_EQ1		BIT_ULL(0)
#define MCS_CPM_RX_INT_SECTAG_E_EQ0_C_EQ1	BIT_ULL(1)
#define MCS_CPM_RX_INT_SL_GTE48			BIT_ULL(2)
#define MCS_CPM_RX_INT_ES_EQ1_SC_EQ1		BIT_ULL(3)
#define MCS_CPM_RX_INT_SC_EQ1_SCB_EQ1		BIT_ULL(4)
#define MCS_CPM_RX_INT_PACKET_XPN_EQ0		BIT_ULL(5)
#define MCS_CPM_RX_INT_PN_THRESH_REACHED	BIT_ULL(6)

#define MCS_CPM_RX_INT_ALL	(MCS_CPM_RX_INT_SECTAG_V_EQ1 |		\
				 MCS_CPM_RX_INT_SECTAG_E_EQ0_C_EQ1 |    \
				 MCS_CPM_RX_INT_SL_GTE48 |		\
				 MCS_CPM_RX_INT_ES_EQ1_SC_EQ1 |		\
				 MCS_CPM_RX_INT_SC_EQ1_SCB_EQ1 |	\
				 MCS_CPM_RX_INT_PACKET_XPN_EQ0 |	\
				 MCS_CPM_RX_INT_PN_THRESH_REACHED)

struct mcs_pfvf {
	u64 intr_mask;	/* Enabled Interrupt mask */
};

struct mcs_intr_event {
	u16 pcifunc;
	u64 intr_mask;
	u64 sa_id;
	u8 mcs_id;
	u8 lmac_id;
};

struct mcs_intrq_entry {
	struct list_head node;
	struct mcs_intr_event intr_event;
};

struct secy_mem_map {
	u8 flow_id;
	u8 secy;
	u8 ctrl_pkt;
	u8 sc;
	u64 sci;
};

struct mcs_rsrc_map {
	u16 *flowid2pf_map;
	u16 *secy2pf_map;
	u16 *sc2pf_map;
	u16 *sa2pf_map;
	u16 *flowid2secy_map;	/* bitmap flowid mapped to secy*/
	u16 *ctrlpktrule2pf_map;
	struct rsrc_bmap	flow_ids;
	struct rsrc_bmap	secy;
	struct rsrc_bmap	sc;
	struct rsrc_bmap	sa;
	struct rsrc_bmap	ctrlpktrule;
};

struct hwinfo {
	u8 tcam_entries;
	u8 secy_entries;
	u8 sc_entries;
	u16 sa_entries;
	u8 mcs_x2p_intf;
	u8 lmac_cnt;
	u8 mcs_blks;
	unsigned long	lmac_bmap; /* bitmap of enabled mcs lmac */
};

struct mcs {
	void __iomem		*reg_base;
	struct pci_dev		*pdev;
	struct device		*dev;
	struct hwinfo		*hw;
	struct mcs_rsrc_map	tx;
	struct mcs_rsrc_map	rx;
	u16                     pf_map[MCS_MAX_PFS]; /* List of PCIFUNC mapped to MCS */
	u8			mcs_id;
	struct mcs_ops		*mcs_ops;
	struct list_head	mcs_list;
	/* Lock for mcs stats */
	struct mutex		stats_lock;
	struct mcs_pfvf		*pf;
	struct mcs_pfvf		*vf;
	u16			num_vec;
	void			*rvu;
	u16			*tx_sa_active;
};

struct mcs_ops {
	void	(*mcs_set_hw_capabilities)(struct mcs *mcs);
	void	(*mcs_parser_cfg)(struct mcs *mcs);
	void	(*mcs_tx_sa_mem_map_write)(struct mcs *mcs, struct mcs_tx_sc_sa_map *map);
	void	(*mcs_rx_sa_mem_map_write)(struct mcs *mcs, struct mcs_rx_sc_sa_map *map);
	void	(*mcs_flowid_secy_map)(struct mcs *mcs, struct secy_mem_map *map, int dir);
};

extern struct pci_driver mcs_driver;

static inline void mcs_reg_write(struct mcs *mcs, u64 offset, u64 val)
{
	writeq(val, mcs->reg_base + offset);
}

static inline u64 mcs_reg_read(struct mcs *mcs, u64 offset)
{
	return readq(mcs->reg_base + offset);
}

/* MCS APIs */
struct mcs *mcs_get_pdata(int mcs_id);
int mcs_get_blkcnt(void);
int mcs_set_lmac_channels(int mcs_id, u16 base);
int mcs_alloc_rsrc(struct rsrc_bmap *rsrc, u16 *pf_map, u16 pcifunc);
int mcs_free_rsrc(struct rsrc_bmap *rsrc, u16 *pf_map, int rsrc_id, u16 pcifunc);
int mcs_alloc_all_rsrc(struct mcs *mcs, u8 *flowid, u8 *secy_id,
		       u8 *sc_id, u8 *sa1_id, u8 *sa2_id, u16 pcifunc, int dir);
int mcs_free_all_rsrc(struct mcs *mcs, int dir, u16 pcifunc);
void mcs_clear_secy_plcy(struct mcs *mcs, int secy_id, int dir);
void mcs_ena_dis_flowid_entry(struct mcs *mcs, int id, int dir, int ena);
void mcs_ena_dis_sc_cam_entry(struct mcs *mcs, int id, int ena);
void mcs_flowid_entry_write(struct mcs *mcs, u64 *data, u64 *mask, int id, int dir);
void mcs_secy_plcy_write(struct mcs *mcs, u64 plcy, int id, int dir);
void mcs_rx_sc_cam_write(struct mcs *mcs, u64 sci, u64 secy, int sc_id);
void mcs_sa_plcy_write(struct mcs *mcs, u64 *plcy, int sa, int dir);
void mcs_map_sc_to_sa(struct mcs *mcs, u64 *sa_map, int sc, int dir);
void mcs_pn_table_write(struct mcs *mcs, u8 pn_id, u64 next_pn, u8 dir);
void mcs_tx_sa_mem_map_write(struct mcs *mcs, struct mcs_tx_sc_sa_map *map);
void mcs_flowid_secy_map(struct mcs *mcs, struct secy_mem_map *map, int dir);
void mcs_rx_sa_mem_map_write(struct mcs *mcs, struct mcs_rx_sc_sa_map *map);
void mcs_pn_threshold_set(struct mcs *mcs, struct mcs_set_pn_threshold *pn);
int mcs_install_flowid_bypass_entry(struct mcs *mcs);
void mcs_set_lmac_mode(struct mcs *mcs, int lmac_id, u8 mode);
void mcs_reset_port(struct mcs *mcs, u8 port_id, u8 reset);
void mcs_set_port_cfg(struct mcs *mcs, struct mcs_port_cfg_set_req *req);
void mcs_get_port_cfg(struct mcs *mcs, struct mcs_port_cfg_get_req *req,
		      struct mcs_port_cfg_get_rsp *rsp);
void mcs_get_custom_tag_cfg(struct mcs *mcs, struct mcs_custom_tag_cfg_get_req *req,
			    struct mcs_custom_tag_cfg_get_rsp *rsp);
int mcs_alloc_ctrlpktrule(struct rsrc_bmap *rsrc, u16 *pf_map, u16 offset, u16 pcifunc);
int mcs_free_ctrlpktrule(struct mcs *mcs, struct mcs_free_ctrl_pkt_rule_req *req);
int mcs_ctrlpktrule_write(struct mcs *mcs, struct mcs_ctrl_pkt_rule_write_req *req);

/* CN10K-B APIs */
void cn10kb_mcs_set_hw_capabilities(struct mcs *mcs);
void cn10kb_mcs_tx_sa_mem_map_write(struct mcs *mcs, struct mcs_tx_sc_sa_map *map);
void cn10kb_mcs_flowid_secy_map(struct mcs *mcs, struct secy_mem_map *map, int dir);
void cn10kb_mcs_rx_sa_mem_map_write(struct mcs *mcs, struct mcs_rx_sc_sa_map *map);
void cn10kb_mcs_parser_cfg(struct mcs *mcs);

/* CNF10K-B APIs */
struct mcs_ops *cnf10kb_get_mac_ops(void);
void cnf10kb_mcs_set_hw_capabilities(struct mcs *mcs);
void cnf10kb_mcs_tx_sa_mem_map_write(struct mcs *mcs, struct mcs_tx_sc_sa_map *map);
void cnf10kb_mcs_flowid_secy_map(struct mcs *mcs, struct secy_mem_map *map, int dir);
void cnf10kb_mcs_rx_sa_mem_map_write(struct mcs *mcs, struct mcs_rx_sc_sa_map *map);
void cnf10kb_mcs_parser_cfg(struct mcs *mcs);
void cnf10kb_mcs_tx_pn_thresh_reached_handler(struct mcs *mcs);
void cnf10kb_mcs_tx_pn_wrapped_handler(struct mcs *mcs);

/* Stats APIs */
void mcs_get_sc_stats(struct mcs *mcs, struct mcs_sc_stats *stats, int id, int dir);
void mcs_get_sa_stats(struct mcs *mcs, struct mcs_sa_stats *stats, int id, int dir);
void mcs_get_port_stats(struct mcs *mcs, struct mcs_port_stats *stats, int id, int dir);
void mcs_get_flowid_stats(struct mcs *mcs, struct mcs_flowid_stats *stats, int id, int dir);
void mcs_get_rx_secy_stats(struct mcs *mcs, struct mcs_secy_stats *stats, int id);
void mcs_get_tx_secy_stats(struct mcs *mcs, struct mcs_secy_stats *stats, int id);
void mcs_clear_stats(struct mcs *mcs, u8 type, u8 id, int dir);
int mcs_clear_all_stats(struct mcs *mcs, u16 pcifunc, int dir);
int mcs_set_force_clk_en(struct mcs *mcs, bool set);

int mcs_add_intr_wq_entry(struct mcs *mcs, struct mcs_intr_event *event);

#endif /* MCS_H */
