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

#endif /* RVU_NPC_HASH_H */
