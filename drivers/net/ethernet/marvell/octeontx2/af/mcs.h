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

/* Reserved resources for default bypass entry */
#define MCS_RSRC_RSVD_CNT		1

struct mcs_rsrc_map {
	u16 *flowid2pf_map;
	u16 *secy2pf_map;
	u16 *sc2pf_map;
	u16 *sa2pf_map;
	u16 *flowid2secy_map;	/* bitmap flowid mapped to secy*/
	struct rsrc_bmap	flow_ids;
	struct rsrc_bmap	secy;
	struct rsrc_bmap	sc;
	struct rsrc_bmap	sa;
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
	u8			mcs_id;
	struct mcs_ops		*mcs_ops;
	struct list_head	mcs_list;
};

struct mcs_ops {
	void	(*mcs_set_hw_capabilities)(struct mcs *mcs);
	void	(*mcs_parser_cfg)(struct mcs *mcs);
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

int mcs_install_flowid_bypass_entry(struct mcs *mcs);
void mcs_set_lmac_mode(struct mcs *mcs, int lmac_id, u8 mode);

/* CN10K-B APIs */
void cn10kb_mcs_set_hw_capabilities(struct mcs *mcs);
void cn10kb_mcs_parser_cfg(struct mcs *mcs);

/* CNF10K-B APIs */
struct mcs_ops *cnf10kb_get_mac_ops(void);
void cnf10kb_mcs_set_hw_capabilities(struct mcs *mcs);
void cnf10kb_mcs_parser_cfg(struct mcs *mcs);

#endif /* MCS_H */
