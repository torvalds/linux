// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2022 Marvell.
 *
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/stddef.h>
#include <linux/debugfs.h>
#include <linux/bitfield.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"
#include "cgx.h"
#include "rvu_npc_hash.h"
#include "rvu_npc_fs.h"

static u64 rvu_npc_wide_extract(const u64 input[], size_t start_bit,
				size_t width_bits)
{
	const u64 mask = ~(u64)((~(__uint128_t)0) << width_bits);
	const size_t msb = start_bit + width_bits - 1;
	const size_t lword = start_bit >> 6;
	const size_t uword = msb >> 6;
	size_t lbits;
	u64 hi, lo;

	if (lword == uword)
		return (input[lword] >> (start_bit & 63)) & mask;

	lbits = 64 - (start_bit & 63);
	hi = input[uword];
	lo = (input[lword] >> (start_bit & 63));
	return ((hi << lbits) | lo) & mask;
}

static void rvu_npc_lshift_key(u64 *key, size_t key_bit_len)
{
	u64 prev_orig_word = 0;
	u64 cur_orig_word = 0;
	size_t extra = key_bit_len % 64;
	size_t max_idx = key_bit_len / 64;
	size_t i;

	if (extra)
		max_idx++;

	for (i = 0; i < max_idx; i++) {
		cur_orig_word = key[i];
		key[i] = key[i] << 1;
		key[i] |= ((prev_orig_word >> 63) & 0x1);
		prev_orig_word = cur_orig_word;
	}
}

static u32 rvu_npc_toeplitz_hash(const u64 *data, u64 *key, size_t data_bit_len,
				 size_t key_bit_len)
{
	u32 hash_out = 0;
	u64 temp_data = 0;
	int i;

	for (i = data_bit_len - 1; i >= 0; i--) {
		temp_data = (data[i / 64]);
		temp_data = temp_data >> (i % 64);
		temp_data &= 0x1;
		if (temp_data)
			hash_out ^= (u32)(rvu_npc_wide_extract(key, key_bit_len - 32, 32));

		rvu_npc_lshift_key(key, key_bit_len);
	}

	return hash_out;
}

u32 npc_field_hash_calc(u64 *ldata, struct npc_mcam_kex_hash *mkex_hash,
			u64 *secret_key, u8 intf, u8 hash_idx)
{
	u64 hash_key[3];
	u64 data_padded[2];
	u32 field_hash;

	hash_key[0] = secret_key[1] << 31;
	hash_key[0] |= secret_key[2];
	hash_key[1] = secret_key[1] >> 33;
	hash_key[1] |= secret_key[0] << 31;
	hash_key[2] = secret_key[0] >> 33;

	data_padded[0] = mkex_hash->hash_mask[intf][hash_idx][0] & ldata[0];
	data_padded[1] = mkex_hash->hash_mask[intf][hash_idx][1] & ldata[1];
	field_hash = rvu_npc_toeplitz_hash(data_padded, hash_key, 128, 159);

	field_hash &= mkex_hash->hash_ctrl[intf][hash_idx] >> 32;
	field_hash |= mkex_hash->hash_ctrl[intf][hash_idx];
	return field_hash;
}

static u64 npc_update_use_hash(int lt, int ld)
{
	u64 cfg = 0;

	switch (lt) {
	case NPC_LT_LC_IP6:
		/* Update use_hash(bit-20) and bytesm1 (bit-16:19)
		 * in KEX_LD_CFG
		 */
		cfg = KEX_LD_CFG_USE_HASH(0x1, 0x03,
					  ld ? 0x8 : 0x18,
					  0x1, 0x0, 0x10);
		break;
	}

	return cfg;
}

static void npc_program_mkex_hash_rx(struct rvu *rvu, int blkaddr,
				     u8 intf)
{
	struct npc_mcam_kex_hash *mkex_hash = rvu->kpu.mkex_hash;
	int lid, lt, ld, hash_cnt = 0;

	if (is_npc_intf_tx(intf))
		return;

	/* Program HASH_CFG */
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++) {
				if (mkex_hash->lid_lt_ld_hash_en[intf][lid][lt][ld]) {
					u64 cfg = npc_update_use_hash(lt, ld);

					hash_cnt++;
					if (hash_cnt == NPC_MAX_HASH)
						return;

					/* Set updated KEX configuration */
					SET_KEX_LD(intf, lid, lt, ld, cfg);
					/* Set HASH configuration */
					SET_KEX_LD_HASH(intf, ld,
							mkex_hash->hash[intf][ld]);
					SET_KEX_LD_HASH_MASK(intf, ld, 0,
							     mkex_hash->hash_mask[intf][ld][0]);
					SET_KEX_LD_HASH_MASK(intf, ld, 1,
							     mkex_hash->hash_mask[intf][ld][1]);
					SET_KEX_LD_HASH_CTRL(intf, ld,
							     mkex_hash->hash_ctrl[intf][ld]);
				}
			}
		}
	}
}

static void npc_program_mkex_hash_tx(struct rvu *rvu, int blkaddr,
				     u8 intf)
{
	struct npc_mcam_kex_hash *mkex_hash = rvu->kpu.mkex_hash;
	int lid, lt, ld, hash_cnt = 0;

	if (is_npc_intf_rx(intf))
		return;

	/* Program HASH_CFG */
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++)
				if (mkex_hash->lid_lt_ld_hash_en[intf][lid][lt][ld]) {
					u64 cfg = npc_update_use_hash(lt, ld);

					hash_cnt++;
					if (hash_cnt == NPC_MAX_HASH)
						return;

					/* Set updated KEX configuration */
					SET_KEX_LD(intf, lid, lt, ld, cfg);
					/* Set HASH configuration */
					SET_KEX_LD_HASH(intf, ld,
							mkex_hash->hash[intf][ld]);
					SET_KEX_LD_HASH_MASK(intf, ld, 0,
							     mkex_hash->hash_mask[intf][ld][0]);
					SET_KEX_LD_HASH_MASK(intf, ld, 1,
							     mkex_hash->hash_mask[intf][ld][1]);
					SET_KEX_LD_HASH_CTRL(intf, ld,
							     mkex_hash->hash_ctrl[intf][ld]);
					hash_cnt++;
					if (hash_cnt == NPC_MAX_HASH)
						return;
				}
		}
	}
}

void npc_config_secret_key(struct rvu *rvu, int blkaddr)
{
	struct hw_cap *hwcap = &rvu->hw->cap;
	struct rvu_hwinfo *hw = rvu->hw;
	u8 intf;

	if (!hwcap->npc_hash_extract) {
		dev_info(rvu->dev, "HW does not support secret key configuration\n");
		return;
	}

	for (intf = 0; intf < hw->npc_intfs; intf++) {
		rvu_write64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY0(intf),
			    RVU_NPC_HASH_SECRET_KEY0);
		rvu_write64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY1(intf),
			    RVU_NPC_HASH_SECRET_KEY1);
		rvu_write64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY2(intf),
			    RVU_NPC_HASH_SECRET_KEY2);
	}
}

void npc_program_mkex_hash(struct rvu *rvu, int blkaddr)
{
	struct hw_cap *hwcap = &rvu->hw->cap;
	struct rvu_hwinfo *hw = rvu->hw;
	u8 intf;

	if (!hwcap->npc_hash_extract) {
		dev_dbg(rvu->dev, "Field hash extract feature is not supported\n");
		return;
	}

	for (intf = 0; intf < hw->npc_intfs; intf++) {
		npc_program_mkex_hash_rx(rvu, blkaddr, intf);
		npc_program_mkex_hash_tx(rvu, blkaddr, intf);
	}
}

void npc_update_field_hash(struct rvu *rvu, u8 intf,
			   struct mcam_entry *entry,
			   int blkaddr,
			   u64 features,
			   struct flow_msg *pkt,
			   struct flow_msg *mask,
			   struct flow_msg *opkt,
			   struct flow_msg *omask)
{
	struct npc_mcam_kex_hash *mkex_hash = rvu->kpu.mkex_hash;
	struct npc_get_secret_key_req req;
	struct npc_get_secret_key_rsp rsp;
	u64 ldata[2], cfg;
	u32 field_hash;
	u8 hash_idx;

	if (!rvu->hw->cap.npc_hash_extract) {
		dev_dbg(rvu->dev, "%s: Field hash extract feature is not supported\n", __func__);
		return;
	}

	req.intf = intf;
	rvu_mbox_handler_npc_get_secret_key(rvu, &req, &rsp);

	for (hash_idx = 0; hash_idx < NPC_MAX_HASH; hash_idx++) {
		cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_HASHX_CFG(intf, hash_idx));
		if ((cfg & BIT_ULL(11)) && (cfg & BIT_ULL(12))) {
			u8 lid = (cfg & GENMASK_ULL(10, 8)) >> 8;
			u8 ltype = (cfg & GENMASK_ULL(7, 4)) >> 4;
			u8 ltype_mask = cfg & GENMASK_ULL(3, 0);

			if (mkex_hash->lid_lt_ld_hash_en[intf][lid][ltype][hash_idx]) {
				switch (ltype & ltype_mask) {
				/* If hash extract enabled is supported for IPv6 then
				 * 128 bit IPv6 source and destination addressed
				 * is hashed to 32 bit value.
				 */
				case NPC_LT_LC_IP6:
					if (features & BIT_ULL(NPC_SIP_IPV6)) {
						u32 src_ip[IPV6_WORDS];

						be32_to_cpu_array(src_ip, pkt->ip6src, IPV6_WORDS);
						ldata[0] = (u64)src_ip[0] << 32 | src_ip[1];
						ldata[1] = (u64)src_ip[2] << 32 | src_ip[3];
						field_hash = npc_field_hash_calc(ldata,
										 mkex_hash,
										 rsp.secret_key,
										 intf,
										 hash_idx);
						npc_update_entry(rvu, NPC_SIP_IPV6, entry,
								 field_hash, 0, 32, 0, intf);
						memcpy(&opkt->ip6src, &pkt->ip6src,
						       sizeof(pkt->ip6src));
						memcpy(&omask->ip6src, &mask->ip6src,
						       sizeof(mask->ip6src));
						break;
					}

					if (features & BIT_ULL(NPC_DIP_IPV6)) {
						u32 dst_ip[IPV6_WORDS];

						be32_to_cpu_array(dst_ip, pkt->ip6dst, IPV6_WORDS);
						ldata[0] = (u64)dst_ip[0] << 32 | dst_ip[1];
						ldata[1] = (u64)dst_ip[2] << 32 | dst_ip[3];
						field_hash = npc_field_hash_calc(ldata,
										 mkex_hash,
										 rsp.secret_key,
										 intf,
										 hash_idx);
						npc_update_entry(rvu, NPC_DIP_IPV6, entry,
								 field_hash, 0, 32, 0, intf);
						memcpy(&opkt->ip6dst, &pkt->ip6dst,
						       sizeof(pkt->ip6dst));
						memcpy(&omask->ip6dst, &mask->ip6dst,
						       sizeof(mask->ip6dst));
					}
					break;
				}
			}
		}
	}
}

int rvu_mbox_handler_npc_get_secret_key(struct rvu *rvu,
					struct npc_get_secret_key_req *req,
					struct npc_get_secret_key_rsp *rsp)
{
	u64 *secret_key = rsp->secret_key;
	u8 intf = req->intf;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -EINVAL;
	}

	secret_key[0] = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY0(intf));
	secret_key[1] = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY1(intf));
	secret_key[2] = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY2(intf));

	return 0;
}
