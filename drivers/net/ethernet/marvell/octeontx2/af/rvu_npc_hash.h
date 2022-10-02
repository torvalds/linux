/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2022 Marvell.
 *
 */

#ifndef __RVU_NPC_HASH_H
#define __RVU_NPC_HASH_H

#define RVU_NPC_HASH_SECRET_KEY0 0xa9d5af4c9fbc76b1
#define RVU_NPC_HASH_SECRET_KEY1 0xa9d5af4c9fbc87b4
#define RVU_NPC_HASH_SECRET_KEY2 0x5954c9e7

#define NPC_MAX_HASH 2
#define NPC_MAX_HASH_MASK 2

#define KEX_LD_CFG_USE_HASH(use_hash, bytesm1, hdr_ofs, ena, flags_ena, key_ofs) \
			    ((use_hash) << 20 | ((bytesm1) << 16) | ((hdr_ofs) << 8) | \
			     ((ena) << 7) | ((flags_ena) << 6) | ((key_ofs) & 0x3F))
#define KEX_LD_CFG_HASH(hdr_ofs, bytesm1, lt_en, lid_en, lid, ltype_match, ltype_mask)	\
			(((hdr_ofs) << 32) | ((bytesm1) << 16) | \
			 ((lt_en) << 12) | ((lid_en) << 11) | ((lid) << 8) | \
			 ((ltype_match) << 4) | ((ltype_mask) & 0xF))

#define SET_KEX_LD_HASH(intf, ld, cfg) \
	rvu_write64(rvu, blkaddr,	\
		    NPC_AF_INTFX_HASHX_CFG(intf, ld), cfg)

#define SET_KEX_LD_HASH_MASK(intf, ld, mask_idx, cfg) \
	rvu_write64(rvu, blkaddr,	\
		    NPC_AF_INTFX_HASHX_MASKX(intf, ld, mask_idx), cfg)

#define SET_KEX_LD_HASH_CTRL(intf, ld, cfg) \
	rvu_write64(rvu, blkaddr,	\
		    NPC_AF_INTFX_HASHX_RESULT_CTRL(intf, ld), cfg)

struct npc_mcam_kex_hash {
	/* NPC_AF_INTF(0..1)_LID(0..7)_LT(0..15)_LD(0..1)_CFG */
	bool lid_lt_ld_hash_en[NPC_MAX_INTF][NPC_MAX_LID][NPC_MAX_LT][NPC_MAX_LD];
	/* NPC_AF_INTF(0..1)_HASH(0..1)_CFG */
	u64 hash[NPC_MAX_INTF][NPC_MAX_HASH];
	/* NPC_AF_INTF(0..1)_HASH(0..1)_MASK(0..1) */
	u64 hash_mask[NPC_MAX_INTF][NPC_MAX_HASH][NPC_MAX_HASH_MASK];
	/* NPC_AF_INTF(0..1)_HASH(0..1)_RESULT_CTRL */
	u64 hash_ctrl[NPC_MAX_INTF][NPC_MAX_HASH];
} __packed;

void npc_update_field_hash(struct rvu *rvu, u8 intf,
			   struct mcam_entry *entry,
			   int blkaddr,
			   u64 features,
			   struct flow_msg *pkt,
			   struct flow_msg *mask,
			   struct flow_msg *opkt,
			   struct flow_msg *omask);
void npc_config_secret_key(struct rvu *rvu, int blkaddr);
void npc_program_mkex_hash(struct rvu *rvu, int blkaddr);
u32 npc_field_hash_calc(u64 *ldata, struct npc_mcam_kex_hash *mkex_hash,
			u64 *secret_key, u8 intf, u8 hash_idx);

static struct npc_mcam_kex_hash npc_mkex_hash_default __maybe_unused = {
	.lid_lt_ld_hash_en = {
	[NIX_INTF_RX] = {
		[NPC_LID_LC] = {
			[NPC_LT_LC_IP6] = {
				true,
				true,
			},
		},
	},

	[NIX_INTF_TX] = {
		[NPC_LID_LC] = {
			[NPC_LT_LC_IP6] = {
				true,
				true,
			},
		},
	},
	},

	.hash = {
	[NIX_INTF_RX] = {
		KEX_LD_CFG_HASH(0x8ULL, 0xf, 0x1, 0x1, NPC_LID_LC, NPC_LT_LC_IP6, 0xf),
		KEX_LD_CFG_HASH(0x18ULL, 0xf, 0x1, 0x1, NPC_LID_LC, NPC_LT_LC_IP6, 0xf),
	},

	[NIX_INTF_TX] = {
		KEX_LD_CFG_HASH(0x8ULL, 0xf, 0x1, 0x1, NPC_LID_LC, NPC_LT_LC_IP6, 0xf),
		KEX_LD_CFG_HASH(0x18ULL, 0xf, 0x1, 0x1, NPC_LID_LC, NPC_LT_LC_IP6, 0xf),
	},
	},

	.hash_mask = {
	[NIX_INTF_RX] = {
		[0] = {
			GENMASK_ULL(63, 0),
			GENMASK_ULL(63, 0),
		},
		[1] = {
			GENMASK_ULL(63, 0),
			GENMASK_ULL(63, 0),
		},
	},

	[NIX_INTF_TX] = {
		[0] = {
			GENMASK_ULL(63, 0),
			GENMASK_ULL(63, 0),
		},
		[1] = {
			GENMASK_ULL(63, 0),
			GENMASK_ULL(63, 0),
		},
	},
	},

	.hash_ctrl = {
	[NIX_INTF_RX] = {
		[0] = GENMASK_ULL(63, 32), /* MSB 32 bit is mask and LSB 32 bit is offset. */
		[1] = GENMASK_ULL(63, 32), /* MSB 32 bit is mask and LSB 32 bit is offset. */
	},

	[NIX_INTF_TX] = {
		[0] = GENMASK_ULL(63, 32), /* MSB 32 bit is mask and LSB 32 bit is offset. */
		[1] = GENMASK_ULL(63, 32), /* MSB 32 bit is mask and LSB 32 bit is offset. */
	},
	},
};

/* If exact match table support is enabled, enable drop rules */
#define NPC_MCAM_DROP_RULE_MAX 30
#define NPC_MCAM_SDP_DROP_RULE_IDX 0

#define RVU_PFFUNC(pf, func)	\
	((((pf) & RVU_PFVF_PF_MASK) << RVU_PFVF_PF_SHIFT) | \
	(((func) & RVU_PFVF_FUNC_MASK) << RVU_PFVF_FUNC_SHIFT))

enum npc_exact_opc_type {
	NPC_EXACT_OPC_MEM,
	NPC_EXACT_OPC_CAM,
};

struct npc_exact_table_entry {
	struct list_head list;
	struct list_head glist;
	u32 seq_id;	/* Sequence number of entry */
	u32 index;	/* Mem table or cam table index */
	u32 mcam_idx;
		/* Mcam index. This is valid only if "cmd" field is false */
	enum npc_exact_opc_type opc_type;
	u16 chan;
	u16 pcifunc;
	u8 ways;
	u8 mac[ETH_ALEN];
	u8 ctype;
	u8 cgx_id;
	u8 lmac_id;
	bool cmd;	/* Is added by ethtool command ? */
};

struct npc_exact_table {
	struct mutex lock;	/* entries update lock */
	unsigned long *id_bmap;
	int num_drop_rules;
	u32 tot_ids;
	u16 cnt_cmd_rules[NPC_MCAM_DROP_RULE_MAX];
	u16 counter_idx[NPC_MCAM_DROP_RULE_MAX];
	bool promisc_mode[NPC_MCAM_DROP_RULE_MAX];
	struct {
		int ways;
		int depth;
		unsigned long *bmap;
		u64 mask;	// Masks before hash calculation.
		u16 hash_mask;	// 11 bits for hash mask
		u16 hash_offset; // 11 bits offset
	} mem_table;

	struct {
		int depth;
		unsigned long *bmap;
	} cam_table;

	struct {
		bool valid;
		u16 chan_val;
		u16 chan_mask;
		u16 pcifunc;
		u8 drop_rule_idx;
	} drop_rule_map[NPC_MCAM_DROP_RULE_MAX];

#define NPC_EXACT_TBL_MAX_WAYS 4

	struct list_head lhead_mem_tbl_entry[NPC_EXACT_TBL_MAX_WAYS];
	int mem_tbl_entry_cnt;

	struct list_head lhead_cam_tbl_entry;
	int cam_tbl_entry_cnt;

	struct list_head lhead_gbl;
};

bool rvu_npc_exact_has_match_table(struct rvu *rvu);
u32 rvu_npc_exact_get_max_entries(struct rvu *rvu);
int rvu_npc_exact_init(struct rvu *rvu);
int rvu_npc_exact_mac_addr_reset(struct rvu *rvu, struct cgx_mac_addr_reset_req *req,
				 struct msg_rsp *rsp);

int rvu_npc_exact_mac_addr_update(struct rvu *rvu,
				  struct cgx_mac_addr_update_req *req,
				  struct cgx_mac_addr_update_rsp *rsp);

int rvu_npc_exact_mac_addr_add(struct rvu *rvu,
			       struct cgx_mac_addr_add_req *req,
			       struct cgx_mac_addr_add_rsp *rsp);

int rvu_npc_exact_mac_addr_del(struct rvu *rvu,
			       struct cgx_mac_addr_del_req *req,
			       struct msg_rsp *rsp);

int rvu_npc_exact_mac_addr_set(struct rvu *rvu, struct cgx_mac_addr_set_or_get *req,
			       struct cgx_mac_addr_set_or_get *rsp);

void rvu_npc_exact_reset(struct rvu *rvu, u16 pcifunc);

bool rvu_npc_exact_can_disable_feature(struct rvu *rvu);
void rvu_npc_exact_disable_feature(struct rvu *rvu);
void rvu_npc_exact_reset(struct rvu *rvu, u16 pcifunc);
u16 rvu_npc_exact_drop_rule_to_pcifunc(struct rvu *rvu, u32 drop_rule_idx);
int rvu_npc_exact_promisc_disable(struct rvu *rvu, u16 pcifunc);
int rvu_npc_exact_promisc_enable(struct rvu *rvu, u16 pcifunc);
#endif /* RVU_NPC_HASH_H */
