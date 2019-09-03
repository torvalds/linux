/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_LIF_H_
#define _IONIC_LIF_H_

#include <linux/pci.h>

enum ionic_lif_state_flags {
	IONIC_LIF_INITED,

	/* leave this as last */
	IONIC_LIF_STATE_SIZE
};

#define IONIC_LIF_NAME_MAX_SZ		32
struct ionic_lif {
	char name[IONIC_LIF_NAME_MAX_SZ];
	struct list_head list;
	struct net_device *netdev;
	DECLARE_BITMAP(state, IONIC_LIF_STATE_SIZE);
	struct ionic *ionic;
	bool registered;
	unsigned int index;
	unsigned int hw_index;
	unsigned int neqs;
	unsigned int nxqs;

	struct ionic_lif_info *info;
	dma_addr_t info_pa;
	u32 info_sz;

	struct dentry *dentry;
	u32 flags;
};

int ionic_lifs_alloc(struct ionic *ionic);
void ionic_lifs_free(struct ionic *ionic);
void ionic_lifs_deinit(struct ionic *ionic);
int ionic_lifs_init(struct ionic *ionic);
int ionic_lif_identify(struct ionic *ionic, u8 lif_type,
		       union ionic_lif_identity *lif_ident);
int ionic_lifs_size(struct ionic *ionic);

#endif /* _IONIC_LIF_H_ */
