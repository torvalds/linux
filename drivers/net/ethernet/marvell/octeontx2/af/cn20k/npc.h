/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2026 Marvell.
 *
 */

#ifndef NPC_CN20K_H
#define NPC_CN20K_H

#define MAX_NUM_BANKS 2
#define MAX_NUM_SUB_BANKS 32
#define MAX_SUBBANK_DEPTH 256

/**
 * enum npc_subbank_flag - NPC subbank status
 *
 * subbank flag indicates whether the subbank is free
 * or used.
 *
 * @NPC_SUBBANK_FLAG_UNINIT: Subbank is not initialized.
 * @NPC_SUBBANK_FLAG_FREE: Subbank is free.
 * @NPC_SUBBANK_FLAG_USED: Subbank is used.
 */
enum npc_subbank_flag {
	NPC_SUBBANK_FLAG_UNINIT,
	NPC_SUBBANK_FLAG_FREE = BIT(0),
	NPC_SUBBANK_FLAG_USED = BIT(1),
};

/**
 * struct npc_subbank - Subbank fields.
 * @b0b:	Subbanks bottom index for bank0
 * @b1b:	Subbanks bottom index for bank1
 * @b0t:	Subbanks top index for bank0
 * @b1t:	Subbanks top index for bank1
 * @flags:	Subbank flags
 * @lock:	Mutex lock for flags and rsrc mofiication
 * @b0map:	Bitmap map for bank0 indexes
 * @b1map:	Bitmap map for bank1 indexes
 * @idx:	Subbank index
 * @arr_idx:	Index to the free array or used array
 * @free_cnt:	Number of free slots in the subbank.
 * @key_type:	X4 or X2 subbank.
 *
 * MCAM resource is divided horizontally into multiple subbanks and
 * Resource allocation from each subbank is managed by this data
 * structure.
 */
struct npc_subbank {
	u16 b0t, b0b, b1t, b1b;
	enum npc_subbank_flag flags;
	struct mutex lock;	/* Protect subbank resources */
	DECLARE_BITMAP(b0map, MAX_SUBBANK_DEPTH);
	DECLARE_BITMAP(b1map, MAX_SUBBANK_DEPTH);
	u16 idx;
	u16 arr_idx;
	u16 free_cnt;
	u8 key_type;
};

/**
 * struct npc_priv_t - NPC private structure.
 * @bank_depth:		Total entries in each bank.
 * @num_banks:		Number of banks.
 * @num_subbanks:	Number of subbanks.
 * @subbank_depth:	Depth of subbank.
 * @kw:			Kex configured key type.
 * @sb:			Subbank array.
 * @xa_sb_used:		Array of used subbanks.
 * @xa_sb_free:		Array of free subbanks.
 * @xa_pf2idx_map:	PF to mcam index map.
 * @xa_idx2pf_map:	Mcam index to PF map.
 * @xa_pf_map:		Pcifunc to index map.
 * @pf_cnt:		Number of PFs.
 * @init_done:		Indicates MCAM initialization is done.
 *
 * This structure is populated during probing time by reading
 * HW csr registers.
 */
struct npc_priv_t {
	int bank_depth;
	const int num_banks;
	int num_subbanks;
	int subbank_depth;
	u8 kw;
	struct npc_subbank *sb;
	struct xarray xa_sb_used;
	struct xarray xa_sb_free;
	struct xarray *xa_pf2idx_map;
	struct xarray xa_idx2pf_map;
	struct xarray xa_pf_map;
	int pf_cnt;
	bool init_done;
};

struct rvu;

struct npc_priv_t *npc_priv_get(void);
int npc_cn20k_init(struct rvu *rvu);
void npc_cn20k_deinit(struct rvu *rvu);

void npc_cn20k_subbank_calc_free(struct rvu *rvu, int *x2_free,
				 int *x4_free, int *sb_free);

int npc_cn20k_ref_idx_alloc(struct rvu *rvu, int pcifunc, int key_type,
			    int prio, u16 *mcam_idx, int ref, int limit,
			    bool contig, int count);
int npc_cn20k_idx_free(struct rvu *rvu, u16 *mcam_idx, int count);
#endif /* NPC_CN20K_H */
