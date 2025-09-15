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

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"
#include "cgx.h"
#include "rvu_npc_fs.h"
#include "rvu_npc_hash.h"

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

u32 npc_field_hash_calc(u64 *ldata, struct npc_get_field_hash_info_rsp rsp,
			u8 intf, u8 hash_idx)
{
	u64 hash_key[3];
	u64 data_padded[2];
	u32 field_hash;

	hash_key[0] = rsp.secret_key[1] << 31;
	hash_key[0] |= rsp.secret_key[2];
	hash_key[1] = rsp.secret_key[1] >> 33;
	hash_key[1] |= rsp.secret_key[0] << 31;
	hash_key[2] = rsp.secret_key[0] >> 33;

	data_padded[0] = rsp.hash_mask[intf][hash_idx][0] & ldata[0];
	data_padded[1] = rsp.hash_mask[intf][hash_idx][1] & ldata[1];
	field_hash = rvu_npc_toeplitz_hash(data_padded, hash_key, 128, 159);

	field_hash &= FIELD_GET(GENMASK(63, 32), rsp.hash_ctrl[intf][hash_idx]);
	field_hash += FIELD_GET(GENMASK(31, 0), rsp.hash_ctrl[intf][hash_idx]);
	return field_hash;
}

static u64 npc_update_use_hash(struct rvu *rvu, int blkaddr,
			       u8 intf, int lid, int lt, int ld)
{
	u8 hdr, key;
	u64 cfg;

	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_LIDX_LTX_LDX_CFG(intf, lid, lt, ld));
	hdr = FIELD_GET(NPC_HDR_OFFSET, cfg);
	key = FIELD_GET(NPC_KEY_OFFSET, cfg);

	/* Update use_hash(bit-20) to 'true' and
	 * bytesm1(bit-16:19) to '0x3' in KEX_LD_CFG
	 */
	cfg = KEX_LD_CFG_USE_HASH(0x1, 0x03,
				  hdr, 0x1, 0x0, key);

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
					u64 cfg;

					if (hash_cnt == NPC_MAX_HASH)
						return;

					cfg = npc_update_use_hash(rvu, blkaddr,
								  intf, lid, lt, ld);
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
					u64 cfg;

					if (hash_cnt == NPC_MAX_HASH)
						return;

					cfg = npc_update_use_hash(rvu, blkaddr,
								  intf, lid, lt, ld);
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
				}
		}
	}
}

void npc_config_secret_key(struct rvu *rvu, int blkaddr)
{
	struct hw_cap *hwcap = &rvu->hw->cap;
	struct rvu_hwinfo *hw = rvu->hw;
	u8 intf;

	if (!hwcap->npc_hash_extract)
		return;

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
	struct npc_mcam_kex_hash *mh = rvu->kpu.mkex_hash;
	struct hw_cap *hwcap = &rvu->hw->cap;
	u8 intf, ld, hdr_offset, byte_len;
	struct rvu_hwinfo *hw = rvu->hw;
	u64 cfg;

	/* Check if hardware supports hash extraction */
	if (!hwcap->npc_hash_extract)
		return;

	/* Check if IPv6 source/destination address
	 * should be hash enabled.
	 * Hashing reduces 128bit SIP/DIP fields to 32bit
	 * so that 224 bit X2 key can be used for IPv6 based filters as well,
	 * which in turn results in more number of MCAM entries available for
	 * use.
	 *
	 * Hashing of IPV6 SIP/DIP is enabled in below scenarios
	 * 1. If the silicon variant supports hashing feature
	 * 2. If the number of bytes of IP addr being extracted is 4 bytes ie
	 *    32bit. The assumption here is that if user wants 8bytes of LSB of
	 *    IP addr or full 16 bytes then his intention is not to use 32bit
	 *    hash.
	 */
	for (intf = 0; intf < hw->npc_intfs; intf++) {
		for (ld = 0; ld < NPC_MAX_LD; ld++) {
			cfg = rvu_read64(rvu, blkaddr,
					 NPC_AF_INTFX_LIDX_LTX_LDX_CFG(intf,
								       NPC_LID_LC,
								       NPC_LT_LC_IP6,
								       ld));
			hdr_offset = FIELD_GET(NPC_HDR_OFFSET, cfg);
			byte_len = FIELD_GET(NPC_BYTESM, cfg);
			/* Hashing of IPv6 source/destination address should be
			 * enabled if,
			 * hdr_offset == 8 (offset of source IPv6 address) or
			 * hdr_offset == 24 (offset of destination IPv6)
			 * address) and the number of byte to be
			 * extracted is 4. As per hardware configuration
			 * byte_len should be == actual byte_len - 1.
			 * Hence byte_len is checked against 3 but nor 4.
			 */
			if ((hdr_offset == 8 || hdr_offset == 24) && byte_len == 3)
				mh->lid_lt_ld_hash_en[intf][NPC_LID_LC][NPC_LT_LC_IP6][ld] = true;
		}
	}

	/* Update hash configuration if the field is hash enabled */
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
	struct npc_get_field_hash_info_req req;
	struct npc_get_field_hash_info_rsp rsp;
	u64 ldata[2], cfg;
	u32 field_hash;
	u8 hash_idx;

	if (!rvu->hw->cap.npc_hash_extract) {
		dev_dbg(rvu->dev, "%s: Field hash extract feature is not supported\n", __func__);
		return;
	}

	req.intf = intf;
	rvu_mbox_handler_npc_get_field_hash_info(rvu, &req, &rsp);

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
					/* ld[0] == hash_idx[0] == Source IPv6
					 * ld[1] == hash_idx[1] == Destination IPv6
					 */
					if ((features & BIT_ULL(NPC_SIP_IPV6)) && !hash_idx) {
						u32 src_ip[IPV6_WORDS];

						be32_to_cpu_array(src_ip, pkt->ip6src, IPV6_WORDS);
						ldata[1] = (u64)src_ip[0] << 32 | src_ip[1];
						ldata[0] = (u64)src_ip[2] << 32 | src_ip[3];
						field_hash = npc_field_hash_calc(ldata,
										 rsp,
										 intf,
										 hash_idx);
						npc_update_entry(rvu, NPC_SIP_IPV6, entry,
								 field_hash, 0,
								 GENMASK(31, 0), 0, intf);
						memcpy(&opkt->ip6src, &pkt->ip6src,
						       sizeof(pkt->ip6src));
						memcpy(&omask->ip6src, &mask->ip6src,
						       sizeof(mask->ip6src));
					} else if ((features & BIT_ULL(NPC_DIP_IPV6)) && hash_idx) {
						u32 dst_ip[IPV6_WORDS];

						be32_to_cpu_array(dst_ip, pkt->ip6dst, IPV6_WORDS);
						ldata[1] = (u64)dst_ip[0] << 32 | dst_ip[1];
						ldata[0] = (u64)dst_ip[2] << 32 | dst_ip[3];
						field_hash = npc_field_hash_calc(ldata,
										 rsp,
										 intf,
										 hash_idx);
						npc_update_entry(rvu, NPC_DIP_IPV6, entry,
								 field_hash, 0,
								 GENMASK(31, 0), 0, intf);
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

int rvu_mbox_handler_npc_get_field_hash_info(struct rvu *rvu,
					     struct npc_get_field_hash_info_req *req,
					     struct npc_get_field_hash_info_rsp *rsp)
{
	u64 *secret_key = rsp->secret_key;
	u8 intf = req->intf;
	int i, j, blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -EINVAL;
	}

	secret_key[0] = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY0(intf));
	secret_key[1] = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY1(intf));
	secret_key[2] = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_SECRET_KEY2(intf));

	for (i = 0; i < NPC_MAX_HASH; i++) {
		for (j = 0; j < NPC_MAX_HASH_MASK; j++) {
			rsp->hash_mask[NIX_INTF_RX][i][j] =
				GET_KEX_LD_HASH_MASK(NIX_INTF_RX, i, j);
			rsp->hash_mask[NIX_INTF_TX][i][j] =
				GET_KEX_LD_HASH_MASK(NIX_INTF_TX, i, j);
		}
	}

	for (i = 0; i < NPC_MAX_INTF; i++)
		for (j = 0; j < NPC_MAX_HASH; j++)
			rsp->hash_ctrl[i][j] = GET_KEX_LD_HASH_CTRL(i, j);

	return 0;
}

/**
 *	rvu_exact_prepare_mdata - Make mdata for mcam entry
 *	@mac: MAC address
 *	@chan: Channel number.
 *	@ctype: Channel Type.
 *	@mask: LDATA mask.
 *	Return: Meta data
 */
static u64 rvu_exact_prepare_mdata(u8 *mac, u16 chan, u16 ctype, u64 mask)
{
	u64 ldata = ether_addr_to_u64(mac);

	/* Please note that mask is 48bit which excludes chan and ctype.
	 * Increase mask bits if we need to include them as well.
	 */
	ldata |= ((u64)chan << 48);
	ldata |= ((u64)ctype  << 60);
	ldata &= mask;
	ldata = ldata << 2;

	return ldata;
}

/**
 *      rvu_exact_calculate_hash - calculate hash index to mem table.
 *	@rvu: resource virtualization unit.
 *	@chan: Channel number
 *	@ctype: Channel type.
 *	@mac: MAC address
 *	@mask: HASH mask.
 *	@table_depth: Depth of table.
 *	Return: Hash value
 */
static u32 rvu_exact_calculate_hash(struct rvu *rvu, u16 chan, u16 ctype, u8 *mac,
				    u64 mask, u32 table_depth)
{
	struct npc_exact_table *table = rvu->hw->table;
	u64 hash_key[2];
	u64 key_in[2];
	u64 ldata;
	u32 hash;

	key_in[0] = RVU_NPC_HASH_SECRET_KEY0;
	key_in[1] = RVU_NPC_HASH_SECRET_KEY2;

	hash_key[0] = key_in[0] << 31;
	hash_key[0] |= key_in[1];
	hash_key[1] = key_in[0] >> 33;

	ldata = rvu_exact_prepare_mdata(mac, chan, ctype, mask);

	dev_dbg(rvu->dev, "%s: ldata=0x%llx hash_key0=0x%llx hash_key2=0x%llx\n", __func__,
		ldata, hash_key[1], hash_key[0]);
	hash = rvu_npc_toeplitz_hash(&ldata, (u64 *)hash_key, 64, 95);

	hash &= table->mem_table.hash_mask;
	hash += table->mem_table.hash_offset;
	dev_dbg(rvu->dev, "%s: hash=%x\n", __func__,  hash);

	return hash;
}

/**
 *      rvu_npc_exact_alloc_mem_table_entry - find free entry in 4 way table.
 *      @rvu: resource virtualization unit.
 *	@way: Indicate way to table.
 *	@index: Hash index to 4 way table.
 *	@hash: Hash value.
 *
 *	Searches 4 way table using hash index. Returns 0 on success.
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_alloc_mem_table_entry(struct rvu *rvu, u8 *way,
					       u32 *index, unsigned int hash)
{
	struct npc_exact_table *table;
	int depth, i;

	table = rvu->hw->table;
	depth = table->mem_table.depth;

	/* Check all the 4 ways for a free slot. */
	mutex_lock(&table->lock);
	for (i = 0; i <  table->mem_table.ways; i++) {
		if (test_bit(hash + i * depth, table->mem_table.bmap))
			continue;

		set_bit(hash + i * depth, table->mem_table.bmap);
		mutex_unlock(&table->lock);

		dev_dbg(rvu->dev, "%s: mem table entry alloc success (way=%d index=%d)\n",
			__func__, i, hash);

		*way = i;
		*index = hash;
		return 0;
	}
	mutex_unlock(&table->lock);

	dev_dbg(rvu->dev, "%s: No space in 4 way exact way, weight=%u\n", __func__,
		bitmap_weight(table->mem_table.bmap, table->mem_table.depth));
	return -ENOSPC;
}

/**
 *	rvu_npc_exact_free_id - Free seq id from bitmat.
 *	@rvu: Resource virtualization unit.
 *	@seq_id: Sequence identifier to be freed.
 */
static void rvu_npc_exact_free_id(struct rvu *rvu, u32 seq_id)
{
	struct npc_exact_table *table;

	table = rvu->hw->table;
	mutex_lock(&table->lock);
	clear_bit(seq_id, table->id_bmap);
	mutex_unlock(&table->lock);
	dev_dbg(rvu->dev, "%s: freed id %d\n", __func__, seq_id);
}

/**
 *	rvu_npc_exact_alloc_id - Alloc seq id from bitmap.
 *	@rvu: Resource virtualization unit.
 *	@seq_id: Sequence identifier.
 *	Return: True or false.
 */
static bool rvu_npc_exact_alloc_id(struct rvu *rvu, u32 *seq_id)
{
	struct npc_exact_table *table;
	u32 idx;

	table = rvu->hw->table;

	mutex_lock(&table->lock);
	idx = find_first_zero_bit(table->id_bmap, table->tot_ids);
	if (idx == table->tot_ids) {
		mutex_unlock(&table->lock);
		dev_err(rvu->dev, "%s: No space in id bitmap (%d)\n",
			__func__, table->tot_ids);

		return false;
	}

	/* Mark bit map to indicate that slot is used.*/
	set_bit(idx, table->id_bmap);
	mutex_unlock(&table->lock);

	*seq_id = idx;
	dev_dbg(rvu->dev, "%s: Allocated id (%d)\n", __func__, *seq_id);

	return true;
}

/**
 *      rvu_npc_exact_alloc_cam_table_entry - find free slot in fully associative table.
 *      @rvu: resource virtualization unit.
 *	@index: Index to exact CAM table.
 *	Return: 0 upon success; else error number.
 */
static int rvu_npc_exact_alloc_cam_table_entry(struct rvu *rvu, int *index)
{
	struct npc_exact_table *table;
	u32 idx;

	table = rvu->hw->table;

	mutex_lock(&table->lock);
	idx = find_first_zero_bit(table->cam_table.bmap, table->cam_table.depth);
	if (idx == table->cam_table.depth) {
		mutex_unlock(&table->lock);
		dev_info(rvu->dev, "%s: No space in exact cam table, weight=%u\n", __func__,
			 bitmap_weight(table->cam_table.bmap, table->cam_table.depth));
		return -ENOSPC;
	}

	/* Mark bit map to indicate that slot is used.*/
	set_bit(idx, table->cam_table.bmap);
	mutex_unlock(&table->lock);

	*index = idx;
	dev_dbg(rvu->dev, "%s: cam table entry alloc success (index=%d)\n",
		__func__, idx);
	return 0;
}

/**
 *	rvu_exact_prepare_table_entry - Data for exact match table entry.
 *	@rvu: Resource virtualization unit.
 *	@enable: Enable/Disable entry
 *	@ctype: Software defined channel type. Currently set as 0.
 *	@chan: Channel number.
 *	@mac_addr: Destination mac address.
 *	Return: mdata for exact match table.
 */
static u64 rvu_exact_prepare_table_entry(struct rvu *rvu, bool enable,
					 u8 ctype, u16 chan, u8 *mac_addr)

{
	u64 ldata = ether_addr_to_u64(mac_addr);

	/* Enable or disable */
	u64 mdata = FIELD_PREP(GENMASK_ULL(63, 63), enable ? 1 : 0);

	/* Set Ctype */
	mdata |= FIELD_PREP(GENMASK_ULL(61, 60), ctype);

	/* Set chan */
	mdata |= FIELD_PREP(GENMASK_ULL(59, 48), chan);

	/* MAC address */
	mdata |= FIELD_PREP(GENMASK_ULL(47, 0), ldata);

	return mdata;
}

/**
 *	rvu_exact_config_secret_key - Configure secret key.
 *	@rvu: Resource virtualization unit.
 */
static void rvu_exact_config_secret_key(struct rvu *rvu)
{
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_EXACT_SECRET0(NIX_INTF_RX),
		    RVU_NPC_HASH_SECRET_KEY0);

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_EXACT_SECRET1(NIX_INTF_RX),
		    RVU_NPC_HASH_SECRET_KEY1);

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_EXACT_SECRET2(NIX_INTF_RX),
		    RVU_NPC_HASH_SECRET_KEY2);
}

/**
 *	rvu_exact_config_search_key - Configure search key
 *	@rvu: Resource virtualization unit.
 */
static void rvu_exact_config_search_key(struct rvu *rvu)
{
	int blkaddr;
	u64 reg_val;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);

	/* HDR offset */
	reg_val = FIELD_PREP(GENMASK_ULL(39, 32), 0);

	/* BYTESM1, number of bytes - 1 */
	reg_val |= FIELD_PREP(GENMASK_ULL(18, 16), ETH_ALEN - 1);

	/* Enable LID and set LID to  NPC_LID_LA */
	reg_val |= FIELD_PREP(GENMASK_ULL(11, 11), 1);
	reg_val |= FIELD_PREP(GENMASK_ULL(10, 8),  NPC_LID_LA);

	/* Clear layer type based extraction */

	/* Disable LT_EN */
	reg_val |= FIELD_PREP(GENMASK_ULL(12, 12), 0);

	/* Set LTYPE_MATCH to 0 */
	reg_val |= FIELD_PREP(GENMASK_ULL(7, 4), 0);

	/* Set LTYPE_MASK to 0 */
	reg_val |= FIELD_PREP(GENMASK_ULL(3, 0), 0);

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_EXACT_CFG(NIX_INTF_RX), reg_val);
}

/**
 *	rvu_exact_config_result_ctrl - Set exact table hash control
 *	@rvu: Resource virtualization unit.
 *	@depth: Depth of Exact match table.
 *
 *	Sets mask and offset for hash for mem table.
 */
static void rvu_exact_config_result_ctrl(struct rvu *rvu, uint32_t depth)
{
	int blkaddr;
	u64 reg = 0;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);

	/* Set mask. Note that depth is a power of 2 */
	rvu->hw->table->mem_table.hash_mask = (depth - 1);
	reg |= FIELD_PREP(GENMASK_ULL(42, 32), (depth - 1));

	/* Set offset as 0 */
	rvu->hw->table->mem_table.hash_offset = 0;
	reg |= FIELD_PREP(GENMASK_ULL(10, 0), 0);

	/* Set reg for RX */
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_EXACT_RESULT_CTL(NIX_INTF_RX), reg);
	/* Store hash mask and offset for s/w algorithm */
}

/**
 *	rvu_exact_config_table_mask - Set exact table mask.
 *	@rvu: Resource virtualization unit.
 */
static void rvu_exact_config_table_mask(struct rvu *rvu)
{
	int blkaddr;
	u64 mask = 0;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);

	/* Don't use Ctype */
	mask |= FIELD_PREP(GENMASK_ULL(61, 60), 0);

	/* Set chan */
	mask |= GENMASK_ULL(59, 48);

	/* Full ldata */
	mask |= GENMASK_ULL(47, 0);

	/* Store mask for s/w hash calcualtion */
	rvu->hw->table->mem_table.mask = mask;

	/* Set mask for RX.*/
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_EXACT_MASK(NIX_INTF_RX), mask);
}

/**
 *      rvu_npc_exact_get_max_entries - Get total number of entries in table.
 *      @rvu: resource virtualization unit.
 *	Return: Maximum table entries possible.
 */
u32 rvu_npc_exact_get_max_entries(struct rvu *rvu)
{
	struct npc_exact_table *table;

	table = rvu->hw->table;
	return table->tot_ids;
}

/**
 *      rvu_npc_exact_has_match_table - Checks support for exact match.
 *      @rvu: resource virtualization unit.
 *	Return: True if exact match table is supported/enabled.
 */
bool rvu_npc_exact_has_match_table(struct rvu *rvu)
{
	return  rvu->hw->cap.npc_exact_match_enabled;
}

/**
 *      __rvu_npc_exact_find_entry_by_seq_id - find entry by id
 *      @rvu: resource virtualization unit.
 *	@seq_id: Sequence identifier.
 *
 *	Caller should acquire the lock.
 *	Return: Pointer to table entry.
 */
static struct npc_exact_table_entry *
__rvu_npc_exact_find_entry_by_seq_id(struct rvu *rvu, u32 seq_id)
{
	struct npc_exact_table *table = rvu->hw->table;
	struct npc_exact_table_entry *entry = NULL;
	struct list_head *lhead;

	lhead = &table->lhead_gbl;

	/* traverse to find the matching entry */
	list_for_each_entry(entry, lhead, glist) {
		if (entry->seq_id != seq_id)
			continue;

		return entry;
	}

	return NULL;
}

/**
 *      rvu_npc_exact_add_to_list - Add entry to list
 *      @rvu: resource virtualization unit.
 *	@opc_type: OPCODE to select MEM/CAM table.
 *	@ways: MEM table ways.
 *	@index: Index in MEM/CAM table.
 *	@cgx_id: CGX identifier.
 *	@lmac_id: LMAC identifier.
 *	@mac_addr: MAC address.
 *	@chan: Channel number.
 *	@ctype: Channel Type.
 *	@seq_id: Sequence identifier
 *	@cmd: True if function is called by ethtool cmd
 *	@mcam_idx: NPC mcam index of DMAC entry in NPC mcam.
 *	@pcifunc: pci function
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_add_to_list(struct rvu *rvu, enum npc_exact_opc_type opc_type, u8 ways,
				     u32 index, u8 cgx_id, u8 lmac_id, u8 *mac_addr, u16 chan,
				     u8 ctype, u32 *seq_id, bool cmd, u32 mcam_idx, u16 pcifunc)
{
	struct npc_exact_table_entry *entry, *tmp, *iter;
	struct npc_exact_table *table = rvu->hw->table;
	struct list_head *lhead, *pprev;

	WARN_ON(ways >= NPC_EXACT_TBL_MAX_WAYS);

	if (!rvu_npc_exact_alloc_id(rvu, seq_id)) {
		dev_err(rvu->dev, "%s: Generate seq id failed\n", __func__);
		return -EFAULT;
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		rvu_npc_exact_free_id(rvu, *seq_id);
		dev_err(rvu->dev, "%s: Memory allocation failed\n", __func__);
		return -ENOMEM;
	}

	mutex_lock(&table->lock);
	switch (opc_type) {
	case NPC_EXACT_OPC_CAM:
		lhead = &table->lhead_cam_tbl_entry;
		table->cam_tbl_entry_cnt++;
		break;

	case NPC_EXACT_OPC_MEM:
		lhead = &table->lhead_mem_tbl_entry[ways];
		table->mem_tbl_entry_cnt++;
		break;

	default:
		mutex_unlock(&table->lock);
		kfree(entry);
		rvu_npc_exact_free_id(rvu, *seq_id);

		dev_err(rvu->dev, "%s: Unknown opc type%d\n", __func__, opc_type);
		return  -EINVAL;
	}

	/* Add to global list */
	INIT_LIST_HEAD(&entry->glist);
	list_add_tail(&entry->glist, &table->lhead_gbl);
	INIT_LIST_HEAD(&entry->list);
	entry->index = index;
	entry->ways = ways;
	entry->opc_type = opc_type;

	entry->pcifunc = pcifunc;

	ether_addr_copy(entry->mac, mac_addr);
	entry->chan = chan;
	entry->ctype = ctype;
	entry->cgx_id = cgx_id;
	entry->lmac_id = lmac_id;

	entry->seq_id = *seq_id;

	entry->mcam_idx = mcam_idx;
	entry->cmd = cmd;

	pprev = lhead;

	/* Insert entry in ascending order of index */
	list_for_each_entry_safe(iter, tmp, lhead, list) {
		if (index < iter->index)
			break;

		pprev = &iter->list;
	}

	/* Add to each table list */
	list_add(&entry->list, pprev);
	mutex_unlock(&table->lock);
	return 0;
}

/**
 *	rvu_npc_exact_mem_table_write - Wrapper for register write
 *	@rvu: resource virtualization unit.
 *	@blkaddr: Block address
 *	@ways: ways for MEM table.
 *	@index: Index in MEM
 *	@mdata: Meta data to be written to register.
 */
static void rvu_npc_exact_mem_table_write(struct rvu *rvu, int blkaddr, u8 ways,
					  u32 index, u64 mdata)
{
	rvu_write64(rvu, blkaddr, NPC_AF_EXACT_MEM_ENTRY(ways, index), mdata);
}

/**
 *	rvu_npc_exact_cam_table_write - Wrapper for register write
 *	@rvu: resource virtualization unit.
 *	@blkaddr: Block address
 *	@index: Index in MEM
 *	@mdata: Meta data to be written to register.
 */
static void rvu_npc_exact_cam_table_write(struct rvu *rvu, int blkaddr,
					  u32 index, u64 mdata)
{
	rvu_write64(rvu, blkaddr, NPC_AF_EXACT_CAM_ENTRY(index), mdata);
}

/**
 *      rvu_npc_exact_dealloc_table_entry - dealloc table entry
 *      @rvu: resource virtualization unit.
 *	@opc_type: OPCODE for selection of table(MEM or CAM)
 *	@ways: ways if opc_type is MEM table.
 *	@index: Index of MEM or CAM table.
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_dealloc_table_entry(struct rvu *rvu, enum npc_exact_opc_type opc_type,
					     u8 ways, u32 index)
{
	int blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	struct npc_exact_table *table;
	u8 null_dmac[6] = { 0 };
	int depth;

	/* Prepare entry with all fields set to zero */
	u64 null_mdata = rvu_exact_prepare_table_entry(rvu, false, 0, 0, null_dmac);

	table = rvu->hw->table;
	depth = table->mem_table.depth;

	mutex_lock(&table->lock);

	switch (opc_type) {
	case NPC_EXACT_OPC_CAM:

		/* Check whether entry is used already */
		if (!test_bit(index, table->cam_table.bmap)) {
			mutex_unlock(&table->lock);
			dev_err(rvu->dev, "%s: Trying to free an unused entry ways=%d index=%d\n",
				__func__, ways, index);
			return -EINVAL;
		}

		rvu_npc_exact_cam_table_write(rvu, blkaddr, index, null_mdata);
		clear_bit(index, table->cam_table.bmap);
		break;

	case NPC_EXACT_OPC_MEM:

		/* Check whether entry is used already */
		if (!test_bit(index + ways * depth, table->mem_table.bmap)) {
			mutex_unlock(&table->lock);
			dev_err(rvu->dev, "%s: Trying to free an unused entry index=%d\n",
				__func__, index);
			return -EINVAL;
		}

		rvu_npc_exact_mem_table_write(rvu, blkaddr, ways, index, null_mdata);
		clear_bit(index + ways * depth, table->mem_table.bmap);
		break;

	default:
		mutex_unlock(&table->lock);
		dev_err(rvu->dev, "%s: invalid opc type %d", __func__, opc_type);
		return -ENOSPC;
	}

	mutex_unlock(&table->lock);

	dev_dbg(rvu->dev, "%s: Successfully deleted entry (index=%d, ways=%d opc_type=%d\n",
		__func__, index,  ways, opc_type);

	return 0;
}

/**
 *	rvu_npc_exact_alloc_table_entry - Allociate an entry
 *      @rvu: resource virtualization unit.
 *	@mac: MAC address.
 *	@chan: Channel number.
 *	@ctype: Channel Type.
 *	@index: Index of MEM table or CAM table.
 *	@ways: Ways. Only valid for MEM table.
 *	@opc_type: OPCODE to select table (MEM or CAM)
 *
 *	Try allocating a slot from MEM table. If all 4 ways
 *	slot are full for a hash index, check availability in
 *	32-entry CAM table for allocation.
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_alloc_table_entry(struct rvu *rvu,  char *mac, u16 chan, u8 ctype,
					   u32 *index, u8 *ways, enum npc_exact_opc_type *opc_type)
{
	struct npc_exact_table *table;
	unsigned int hash;
	int err;

	table = rvu->hw->table;

	/* Check in 4-ways mem entry for free slote */
	hash =  rvu_exact_calculate_hash(rvu, chan, ctype, mac, table->mem_table.mask,
					 table->mem_table.depth);
	err = rvu_npc_exact_alloc_mem_table_entry(rvu, ways, index, hash);
	if (!err) {
		*opc_type = NPC_EXACT_OPC_MEM;
		dev_dbg(rvu->dev, "%s: inserted in 4 ways hash table ways=%d, index=%d\n",
			__func__, *ways, *index);
		return 0;
	}

	dev_dbg(rvu->dev, "%s: failed to insert in 4 ways hash table\n", __func__);

	/* wayss is 0 for cam table */
	*ways = 0;
	err = rvu_npc_exact_alloc_cam_table_entry(rvu, index);
	if (!err) {
		*opc_type = NPC_EXACT_OPC_CAM;
		dev_dbg(rvu->dev, "%s: inserted in fully associative hash table index=%u\n",
			__func__, *index);
		return 0;
	}

	dev_err(rvu->dev, "%s: failed to insert in fully associative hash table\n", __func__);
	return -ENOSPC;
}

/**
 *	rvu_npc_exact_save_drop_rule_chan_and_mask - Save drop rules info in data base.
 *      @rvu: resource virtualization unit.
 *	@drop_mcam_idx: Drop rule index in NPC mcam.
 *	@chan_val: Channel value.
 *	@chan_mask: Channel Mask.
 *	@pcifunc: pcifunc of interface.
 *	Return: True upon success.
 */
static bool rvu_npc_exact_save_drop_rule_chan_and_mask(struct rvu *rvu, int drop_mcam_idx,
						       u64 chan_val, u64 chan_mask, u16 pcifunc)
{
	struct npc_exact_table *table;
	int i;

	table = rvu->hw->table;

	for (i = 0; i < NPC_MCAM_DROP_RULE_MAX; i++) {
		if (!table->drop_rule_map[i].valid)
			break;

		if (table->drop_rule_map[i].chan_val != (u16)chan_val)
			continue;

		if (table->drop_rule_map[i].chan_mask != (u16)chan_mask)
			continue;

		return false;
	}

	if (i == NPC_MCAM_DROP_RULE_MAX)
		return false;

	table->drop_rule_map[i].drop_rule_idx = drop_mcam_idx;
	table->drop_rule_map[i].chan_val = (u16)chan_val;
	table->drop_rule_map[i].chan_mask = (u16)chan_mask;
	table->drop_rule_map[i].pcifunc = pcifunc;
	table->drop_rule_map[i].valid = true;
	return true;
}

/**
 *	rvu_npc_exact_calc_drop_rule_chan_and_mask - Calculate Channel number and mask.
 *      @rvu: resource virtualization unit.
 *	@intf_type: Interface type (SDK, LBK or CGX)
 *	@cgx_id: CGX identifier.
 *	@lmac_id: LAMC identifier.
 *	@val: Channel number.
 *	@mask: Channel mask.
 *	Return: True upon success.
 */
static bool rvu_npc_exact_calc_drop_rule_chan_and_mask(struct rvu *rvu, u8 intf_type,
						       u8 cgx_id, u8 lmac_id,
						       u64 *val, u64 *mask)
{
	u16 chan_val, chan_mask;

	/* No support for SDP and LBK */
	if (intf_type != NIX_INTF_TYPE_CGX)
		return false;

	chan_val = rvu_nix_chan_cgx(rvu, cgx_id, lmac_id, 0);
	chan_mask = 0xfff;

	if (val)
		*val = chan_val;

	if (mask)
		*mask = chan_mask;

	return true;
}

/**
 *	rvu_npc_exact_drop_rule_to_pcifunc - Retrieve pcifunc
 *      @rvu: resource virtualization unit.
 *	@drop_rule_idx: Drop rule index in NPC mcam.
 *
 *	Debugfs (exact_drop_cnt) entry displays pcifunc for interface
 *	by retrieving the pcifunc value from data base.
 *	Return: Drop rule index.
 */
u16 rvu_npc_exact_drop_rule_to_pcifunc(struct rvu *rvu, u32 drop_rule_idx)
{
	struct npc_exact_table *table;
	int i;

	table = rvu->hw->table;

	for (i = 0; i < NPC_MCAM_DROP_RULE_MAX; i++) {
		if (!table->drop_rule_map[i].valid)
			break;

		if (table->drop_rule_map[i].drop_rule_idx != drop_rule_idx)
			continue;

		return table->drop_rule_map[i].pcifunc;
	}

	dev_err(rvu->dev, "%s: drop mcam rule index (%d) >= NPC_MCAM_DROP_RULE_MAX\n",
		__func__, drop_rule_idx);
	return -1;
}

/**
 *	rvu_npc_exact_get_drop_rule_info - Get drop rule information.
 *      @rvu: resource virtualization unit.
 *	@intf_type: Interface type (CGX, SDP or LBK)
 *	@cgx_id: CGX identifier.
 *	@lmac_id: LMAC identifier.
 *	@drop_mcam_idx: NPC mcam drop rule index.
 *	@val: Channel value.
 *	@mask: Channel mask.
 *	@pcifunc: pcifunc of interface corresponding to the drop rule.
 *	Return: True upon success.
 */
static bool rvu_npc_exact_get_drop_rule_info(struct rvu *rvu, u8 intf_type, u8 cgx_id,
					     u8 lmac_id, u32 *drop_mcam_idx, u64 *val,
					     u64 *mask, u16 *pcifunc)
{
	struct npc_exact_table *table;
	u64 chan_val, chan_mask;
	bool rc;
	int i;

	table = rvu->hw->table;

	if (intf_type != NIX_INTF_TYPE_CGX) {
		dev_err(rvu->dev, "%s: No drop rule for LBK/SDP mode\n", __func__);
		return false;
	}

	rc = rvu_npc_exact_calc_drop_rule_chan_and_mask(rvu, intf_type, cgx_id,
							lmac_id, &chan_val, &chan_mask);
	if (!rc)
		return false;

	for (i = 0; i < NPC_MCAM_DROP_RULE_MAX; i++) {
		if (!table->drop_rule_map[i].valid)
			break;

		if (table->drop_rule_map[i].chan_val != (u16)chan_val)
			continue;

		if (val)
			*val = table->drop_rule_map[i].chan_val;
		if (mask)
			*mask = table->drop_rule_map[i].chan_mask;
		if (pcifunc)
			*pcifunc = table->drop_rule_map[i].pcifunc;

		*drop_mcam_idx = i;
		return true;
	}

	if (i == NPC_MCAM_DROP_RULE_MAX) {
		dev_err(rvu->dev, "%s: drop mcam rule index (%d) >= NPC_MCAM_DROP_RULE_MAX\n",
			__func__, *drop_mcam_idx);
		return false;
	}

	dev_err(rvu->dev, "%s: Could not retrieve for cgx=%d, lmac=%d\n",
		__func__, cgx_id, lmac_id);
	return false;
}

/**
 *	__rvu_npc_exact_cmd_rules_cnt_update - Update number dmac rules against a drop rule.
 *      @rvu: resource virtualization unit.
 *	@drop_mcam_idx: NPC mcam drop rule index.
 *	@val: +1 or -1.
 *	@enable_or_disable_cam: If no exact match rules against a drop rule, disable it.
 *
 *	when first exact match entry against a drop rule is added, enable_or_disable_cam
 *	is set to true. When last exact match entry against a drop rule is deleted,
 *	enable_or_disable_cam is set to true.
 *	Return: Number of rules
 */
static u16 __rvu_npc_exact_cmd_rules_cnt_update(struct rvu *rvu, int drop_mcam_idx,
						int val, bool *enable_or_disable_cam)
{
	struct npc_exact_table *table;
	u16 *cnt, old_cnt;
	bool promisc;

	table = rvu->hw->table;
	promisc = table->promisc_mode[drop_mcam_idx];

	cnt = &table->cnt_cmd_rules[drop_mcam_idx];
	old_cnt = *cnt;

	*cnt += val;

	if (!enable_or_disable_cam)
		goto done;

	*enable_or_disable_cam = false;

	if (promisc)
		goto done;

	/* If all rules are deleted and not already in promisc mode;
	 * disable cam
	 */
	if (!*cnt && val < 0) {
		*enable_or_disable_cam = true;
		goto done;
	}

	/* If rule got added and not already in promisc mode; enable cam */
	if (!old_cnt && val > 0) {
		*enable_or_disable_cam = true;
		goto done;
	}

done:
	return *cnt;
}

/**
 *      rvu_npc_exact_del_table_entry_by_id - Delete and free table entry.
 *      @rvu: resource virtualization unit.
 *	@seq_id: Sequence identifier of the entry.
 *
 *	Deletes entry from linked lists and free up slot in HW MEM or CAM
 *	table.
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_del_table_entry_by_id(struct rvu *rvu, u32 seq_id)
{
	struct npc_exact_table_entry *entry = NULL;
	struct npc_exact_table *table;
	bool disable_cam = false;
	u32 drop_mcam_idx = -1;
	int *cnt;
	bool rc;

	table = rvu->hw->table;

	mutex_lock(&table->lock);

	/* Lookup for entry which needs to be updated */
	entry = __rvu_npc_exact_find_entry_by_seq_id(rvu, seq_id);
	if (!entry) {
		dev_dbg(rvu->dev, "%s: failed to find entry for id=%d\n", __func__, seq_id);
		mutex_unlock(&table->lock);
		return -ENODATA;
	}

	cnt = (entry->opc_type == NPC_EXACT_OPC_CAM) ? &table->cam_tbl_entry_cnt :
				&table->mem_tbl_entry_cnt;

	/* delete from lists */
	list_del_init(&entry->list);
	list_del_init(&entry->glist);

	(*cnt)--;

	rc = rvu_npc_exact_get_drop_rule_info(rvu, NIX_INTF_TYPE_CGX, entry->cgx_id,
					      entry->lmac_id, &drop_mcam_idx, NULL, NULL, NULL);
	if (!rc) {
		dev_dbg(rvu->dev, "%s: failed to retrieve drop info for id=0x%x\n",
			__func__, seq_id);
		mutex_unlock(&table->lock);
		return -ENODATA;
	}

	if (entry->cmd)
		__rvu_npc_exact_cmd_rules_cnt_update(rvu, drop_mcam_idx, -1, &disable_cam);

	/* No dmac filter rules; disable drop on hit rule */
	if (disable_cam) {
		rvu_npc_enable_mcam_by_entry_index(rvu, drop_mcam_idx, NIX_INTF_RX, false);
		dev_dbg(rvu->dev, "%s: Disabling mcam idx %d\n",
			__func__, drop_mcam_idx);
	}

	mutex_unlock(&table->lock);

	rvu_npc_exact_dealloc_table_entry(rvu, entry->opc_type, entry->ways, entry->index);

	rvu_npc_exact_free_id(rvu, seq_id);

	dev_dbg(rvu->dev, "%s: delete entry success for id=0x%x, mca=%pM\n",
		__func__, seq_id, entry->mac);
	kfree(entry);

	return 0;
}

/**
 *      rvu_npc_exact_add_table_entry - Adds a table entry
 *      @rvu: resource virtualization unit.
 *	@cgx_id: cgx identifier.
 *	@lmac_id: lmac identifier.
 *	@mac: MAC address.
 *	@chan: Channel number.
 *	@ctype: Channel Type.
 *	@seq_id: Sequence number.
 *	@cmd: Whether it is invoked by ethtool cmd.
 *	@mcam_idx: NPC mcam index corresponding to MAC
 *	@pcifunc: PCI func.
 *
 *	Creates a new exact match table entry in either CAM or
 *	MEM table.
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_add_table_entry(struct rvu *rvu, u8 cgx_id, u8 lmac_id, u8 *mac,
					 u16 chan, u8 ctype, u32 *seq_id, bool cmd,
					 u32 mcam_idx, u16 pcifunc)
{
	int blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	enum npc_exact_opc_type opc_type;
	bool enable_cam = false;
	u32 drop_mcam_idx;
	u32 index;
	u64 mdata;
	bool rc;
	int err;
	u8 ways;

	ctype = 0;

	err = rvu_npc_exact_alloc_table_entry(rvu, mac, chan, ctype, &index, &ways, &opc_type);
	if (err) {
		dev_err(rvu->dev, "%s: Could not alloc in exact match table\n", __func__);
		return err;
	}

	/* Write mdata to table */
	mdata = rvu_exact_prepare_table_entry(rvu, true, ctype, chan, mac);

	if (opc_type == NPC_EXACT_OPC_CAM)
		rvu_npc_exact_cam_table_write(rvu, blkaddr, index, mdata);
	else
		rvu_npc_exact_mem_table_write(rvu, blkaddr, ways, index,  mdata);

	/* Insert entry to linked list */
	err = rvu_npc_exact_add_to_list(rvu, opc_type, ways, index, cgx_id, lmac_id,
					mac, chan, ctype, seq_id, cmd, mcam_idx, pcifunc);
	if (err) {
		rvu_npc_exact_dealloc_table_entry(rvu, opc_type, ways, index);
		dev_err(rvu->dev, "%s: could not add to exact match table\n", __func__);
		return err;
	}

	rc = rvu_npc_exact_get_drop_rule_info(rvu, NIX_INTF_TYPE_CGX, cgx_id, lmac_id,
					      &drop_mcam_idx, NULL, NULL, NULL);
	if (!rc) {
		rvu_npc_exact_dealloc_table_entry(rvu, opc_type, ways, index);
		dev_dbg(rvu->dev, "%s: failed to get drop rule info cgx=%d lmac=%d\n",
			__func__, cgx_id, lmac_id);
		return -EINVAL;
	}

	if (cmd)
		__rvu_npc_exact_cmd_rules_cnt_update(rvu, drop_mcam_idx, 1, &enable_cam);

	/* First command rule; enable drop on hit rule */
	if (enable_cam) {
		rvu_npc_enable_mcam_by_entry_index(rvu, drop_mcam_idx, NIX_INTF_RX, true);
		dev_dbg(rvu->dev, "%s: Enabling mcam idx %d\n",
			__func__, drop_mcam_idx);
	}

	dev_dbg(rvu->dev,
		"%s: Successfully added entry (index=%d, dmac=%pM, ways=%d opc_type=%d\n",
		__func__, index, mac, ways, opc_type);

	return 0;
}

/**
 *      rvu_npc_exact_update_table_entry - Update exact match table.
 *      @rvu: resource virtualization unit.
 *	@cgx_id: CGX identifier.
 *	@lmac_id: LMAC identifier.
 *	@old_mac: Existing MAC address entry.
 *	@new_mac: New MAC address entry.
 *	@seq_id: Sequence identifier of the entry.
 *
 *	Updates MAC address of an entry. If entry is in MEM table, new
 *	hash value may not match with old one.
 *	Return: 0 upon success.
 */
static int rvu_npc_exact_update_table_entry(struct rvu *rvu, u8 cgx_id, u8 lmac_id,
					    u8 *old_mac, u8 *new_mac, u32 *seq_id)
{
	int blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	struct npc_exact_table_entry *entry;
	struct npc_exact_table *table;
	u32 hash_index;
	u64 mdata;

	table = rvu->hw->table;

	mutex_lock(&table->lock);

	/* Lookup for entry which needs to be updated */
	entry = __rvu_npc_exact_find_entry_by_seq_id(rvu, *seq_id);
	if (!entry) {
		mutex_unlock(&table->lock);
		dev_dbg(rvu->dev,
			"%s: failed to find entry for cgx_id=%d lmac_id=%d old_mac=%pM\n",
			__func__, cgx_id, lmac_id, old_mac);
		return -ENODATA;
	}

	/* If entry is in mem table and new hash index is different than old
	 * hash index, we cannot update the entry. Fail in these scenarios.
	 */
	if (entry->opc_type == NPC_EXACT_OPC_MEM) {
		hash_index =  rvu_exact_calculate_hash(rvu, entry->chan, entry->ctype,
						       new_mac, table->mem_table.mask,
						       table->mem_table.depth);
		if (hash_index != entry->index) {
			dev_dbg(rvu->dev,
				"%s: Update failed due to index mismatch(new=0x%x, old=%x)\n",
				__func__, hash_index, entry->index);
			mutex_unlock(&table->lock);
			return -EINVAL;
		}
	}

	mdata = rvu_exact_prepare_table_entry(rvu, true, entry->ctype, entry->chan, new_mac);

	if (entry->opc_type == NPC_EXACT_OPC_MEM)
		rvu_npc_exact_mem_table_write(rvu, blkaddr, entry->ways, entry->index, mdata);
	else
		rvu_npc_exact_cam_table_write(rvu, blkaddr, entry->index, mdata);

	/* Update entry fields */
	ether_addr_copy(entry->mac, new_mac);
	*seq_id = entry->seq_id;

	dev_dbg(rvu->dev,
		"%s: Successfully updated entry (index=%d, dmac=%pM, ways=%d opc_type=%d\n",
		__func__, entry->index, entry->mac, entry->ways, entry->opc_type);

	dev_dbg(rvu->dev, "%s: Successfully updated entry (old mac=%pM new_mac=%pM\n",
		__func__, old_mac, new_mac);

	mutex_unlock(&table->lock);
	return 0;
}

/**
 *	rvu_npc_exact_promisc_disable - Disable promiscuous mode.
 *      @rvu: resource virtualization unit.
 *	@pcifunc: pcifunc
 *
 *	Drop rule is against each PF. We dont support DMAC filter for
 *	VF.
 *	Return: 0 upon success
 */

int rvu_npc_exact_promisc_disable(struct rvu *rvu, u16 pcifunc)
{
	struct npc_exact_table *table;
	int pf = rvu_get_pf(rvu->pdev, pcifunc);
	u8 cgx_id, lmac_id;
	u32 drop_mcam_idx;
	bool *promisc;
	bool rc;

	table = rvu->hw->table;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	rc = rvu_npc_exact_get_drop_rule_info(rvu, NIX_INTF_TYPE_CGX, cgx_id, lmac_id,
					      &drop_mcam_idx, NULL, NULL, NULL);
	if (!rc) {
		dev_dbg(rvu->dev, "%s: failed to get drop rule info cgx=%d lmac=%d\n",
			__func__, cgx_id, lmac_id);
		return -EINVAL;
	}

	mutex_lock(&table->lock);
	promisc = &table->promisc_mode[drop_mcam_idx];

	if (!*promisc) {
		mutex_unlock(&table->lock);
		dev_dbg(rvu->dev, "%s: Err Already promisc mode disabled (cgx=%d lmac=%d)\n",
			__func__, cgx_id, lmac_id);
		return LMAC_AF_ERR_INVALID_PARAM;
	}
	*promisc = false;
	mutex_unlock(&table->lock);

	/* Enable drop rule */
	rvu_npc_enable_mcam_by_entry_index(rvu, drop_mcam_idx, NIX_INTF_RX,
					   true);

	dev_dbg(rvu->dev, "%s: disabled  promisc mode (cgx=%d lmac=%d)\n",
		__func__, cgx_id, lmac_id);
	return 0;
}

/**
 *	rvu_npc_exact_promisc_enable - Enable promiscuous mode.
 *      @rvu: resource virtualization unit.
 *	@pcifunc: pcifunc.
 *	Return: 0 upon success
 */
int rvu_npc_exact_promisc_enable(struct rvu *rvu, u16 pcifunc)
{
	struct npc_exact_table *table;
	int pf = rvu_get_pf(rvu->pdev, pcifunc);
	u8 cgx_id, lmac_id;
	u32 drop_mcam_idx;
	bool *promisc;
	bool rc;

	table = rvu->hw->table;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	rc = rvu_npc_exact_get_drop_rule_info(rvu, NIX_INTF_TYPE_CGX, cgx_id, lmac_id,
					      &drop_mcam_idx, NULL, NULL, NULL);
	if (!rc) {
		dev_dbg(rvu->dev, "%s: failed to get drop rule info cgx=%d lmac=%d\n",
			__func__, cgx_id, lmac_id);
		return -EINVAL;
	}

	mutex_lock(&table->lock);
	promisc = &table->promisc_mode[drop_mcam_idx];

	if (*promisc) {
		mutex_unlock(&table->lock);
		dev_dbg(rvu->dev, "%s: Already in promisc mode (cgx=%d lmac=%d)\n",
			__func__, cgx_id, lmac_id);
		return LMAC_AF_ERR_INVALID_PARAM;
	}
	*promisc = true;
	mutex_unlock(&table->lock);

	/*  disable drop rule */
	rvu_npc_enable_mcam_by_entry_index(rvu, drop_mcam_idx, NIX_INTF_RX,
					   false);

	dev_dbg(rvu->dev, "%s: Enabled promisc mode (cgx=%d lmac=%d)\n",
		__func__, cgx_id, lmac_id);
	return 0;
}

/**
 *	rvu_npc_exact_mac_addr_reset - Delete PF mac address.
 *      @rvu: resource virtualization unit.
 *	@req: Reset request
 *	@rsp: Reset response.
 *	Return: 0 upon success
 */
int rvu_npc_exact_mac_addr_reset(struct rvu *rvu, struct cgx_mac_addr_reset_req *req,
				 struct msg_rsp *rsp)
{
	int pf = rvu_get_pf(rvu->pdev, req->hdr.pcifunc);
	u32 seq_id = req->index;
	struct rvu_pfvf *pfvf;
	u8 cgx_id, lmac_id;
	int rc;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);

	rc = rvu_npc_exact_del_table_entry_by_id(rvu, seq_id);
	if (rc) {
		/* TODO: how to handle this error case ? */
		dev_err(rvu->dev, "%s MAC (%pM) del PF=%d failed\n", __func__, pfvf->mac_addr, pf);
		return 0;
	}

	dev_dbg(rvu->dev, "%s MAC (%pM) del PF=%d success (seq_id=%u)\n",
		__func__, pfvf->mac_addr, pf, seq_id);
	return 0;
}

/**
 *	rvu_npc_exact_mac_addr_update - Update mac address field with new value.
 *      @rvu: resource virtualization unit.
 *	@req: Update request.
 *	@rsp: Update response.
 *	Return: 0 upon success
 */
int rvu_npc_exact_mac_addr_update(struct rvu *rvu,
				  struct cgx_mac_addr_update_req *req,
				  struct cgx_mac_addr_update_rsp *rsp)
{
	int pf = rvu_get_pf(rvu->pdev, req->hdr.pcifunc);
	struct npc_exact_table_entry *entry;
	struct npc_exact_table *table;
	struct rvu_pfvf *pfvf;
	u32 seq_id, mcam_idx;
	u8 old_mac[ETH_ALEN];
	u8 cgx_id, lmac_id;
	int rc;

	if (!is_cgx_config_permitted(rvu, req->hdr.pcifunc))
		return LMAC_AF_ERR_PERM_DENIED;

	dev_dbg(rvu->dev, "%s: Update request for seq_id=%d, mac=%pM\n",
		__func__, req->index, req->mac_addr);

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);

	table = rvu->hw->table;

	mutex_lock(&table->lock);

	/* Lookup for entry which needs to be updated */
	entry = __rvu_npc_exact_find_entry_by_seq_id(rvu, req->index);
	if (!entry) {
		dev_err(rvu->dev, "%s: failed to find entry for id=0x%x\n", __func__, req->index);
		mutex_unlock(&table->lock);
		return LMAC_AF_ERR_EXACT_MATCH_TBL_LOOK_UP_FAILED;
	}
	ether_addr_copy(old_mac, entry->mac);
	seq_id = entry->seq_id;
	mcam_idx = entry->mcam_idx;
	mutex_unlock(&table->lock);

	rc = rvu_npc_exact_update_table_entry(rvu, cgx_id, lmac_id,  old_mac,
					      req->mac_addr, &seq_id);
	if (!rc) {
		rsp->index = seq_id;
		dev_dbg(rvu->dev, "%s  mac:%pM (pfvf:%pM default:%pM) update to PF=%d success\n",
			__func__, req->mac_addr, pfvf->mac_addr, pfvf->default_mac, pf);
		ether_addr_copy(pfvf->mac_addr, req->mac_addr);
		return 0;
	}

	/* Try deleting and adding it again */
	rc = rvu_npc_exact_del_table_entry_by_id(rvu, req->index);
	if (rc) {
		/* This could be a new entry */
		dev_dbg(rvu->dev, "%s MAC (%pM) del PF=%d failed\n", __func__,
			pfvf->mac_addr, pf);
	}

	rc = rvu_npc_exact_add_table_entry(rvu, cgx_id, lmac_id, req->mac_addr,
					   pfvf->rx_chan_base, 0, &seq_id, true,
					   mcam_idx, req->hdr.pcifunc);
	if (rc) {
		dev_err(rvu->dev, "%s MAC (%pM) add PF=%d failed\n", __func__,
			req->mac_addr, pf);
		return LMAC_AF_ERR_EXACT_MATCH_TBL_ADD_FAILED;
	}

	rsp->index = seq_id;
	dev_dbg(rvu->dev,
		"%s MAC (new:%pM, old=%pM default:%pM) del and add to PF=%d success (seq_id=%u)\n",
		__func__, req->mac_addr, pfvf->mac_addr, pfvf->default_mac, pf, seq_id);

	ether_addr_copy(pfvf->mac_addr, req->mac_addr);
	return 0;
}

/**
 *	rvu_npc_exact_mac_addr_add - Adds MAC address to exact match table.
 *      @rvu: resource virtualization unit.
 *	@req: Add request.
 *	@rsp: Add response.
 *	Return: 0 upon success
 */
int rvu_npc_exact_mac_addr_add(struct rvu *rvu,
			       struct cgx_mac_addr_add_req *req,
			       struct cgx_mac_addr_add_rsp *rsp)
{
	int pf = rvu_get_pf(rvu->pdev, req->hdr.pcifunc);
	struct rvu_pfvf *pfvf;
	u8 cgx_id, lmac_id;
	int rc = 0;
	u32 seq_id;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);
	pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);

	rc = rvu_npc_exact_add_table_entry(rvu, cgx_id, lmac_id, req->mac_addr,
					   pfvf->rx_chan_base, 0, &seq_id,
					   true, -1, req->hdr.pcifunc);

	if (!rc) {
		rsp->index = seq_id;
		dev_dbg(rvu->dev, "%s MAC (%pM) add to PF=%d success (seq_id=%u)\n",
			__func__, req->mac_addr, pf, seq_id);
		return 0;
	}

	dev_err(rvu->dev, "%s MAC (%pM) add to PF=%d failed\n", __func__,
		req->mac_addr, pf);
	return LMAC_AF_ERR_EXACT_MATCH_TBL_ADD_FAILED;
}

/**
 *	rvu_npc_exact_mac_addr_del - Delete DMAC filter
 *      @rvu: resource virtualization unit.
 *	@req: Delete request.
 *	@rsp: Delete response.
 *	Return: 0 upon success
 */
int rvu_npc_exact_mac_addr_del(struct rvu *rvu,
			       struct cgx_mac_addr_del_req *req,
			       struct msg_rsp *rsp)
{
	int pf = rvu_get_pf(rvu->pdev, req->hdr.pcifunc);
	int rc;

	rc = rvu_npc_exact_del_table_entry_by_id(rvu, req->index);
	if (!rc) {
		dev_dbg(rvu->dev, "%s del to PF=%d success (seq_id=%u)\n",
			__func__, pf, req->index);
		return 0;
	}

	dev_err(rvu->dev, "%s del to PF=%d failed (seq_id=%u)\n",
		__func__,  pf, req->index);
	return LMAC_AF_ERR_EXACT_MATCH_TBL_DEL_FAILED;
}

/**
 *	rvu_npc_exact_mac_addr_set - Add PF mac address to dmac filter.
 *      @rvu: resource virtualization unit.
 *	@req: Set request.
 *	@rsp: Set response.
 *	Return: 0 upon success
 */
int rvu_npc_exact_mac_addr_set(struct rvu *rvu, struct cgx_mac_addr_set_or_get *req,
			       struct cgx_mac_addr_set_or_get *rsp)
{
	int pf = rvu_get_pf(rvu->pdev, req->hdr.pcifunc);
	u32 seq_id = req->index;
	struct rvu_pfvf *pfvf;
	u8 cgx_id, lmac_id;
	u32 mcam_idx = -1;
	int rc, nixlf;

	rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[pf], &cgx_id, &lmac_id);

	pfvf = &rvu->pf[pf];

	/* If table does not have an entry; both update entry and del table entry API
	 * below fails. Those are not failure conditions.
	 */
	rc = rvu_npc_exact_update_table_entry(rvu, cgx_id, lmac_id, pfvf->mac_addr,
					      req->mac_addr, &seq_id);
	if (!rc) {
		rsp->index = seq_id;
		ether_addr_copy(pfvf->mac_addr, req->mac_addr);
		ether_addr_copy(rsp->mac_addr, req->mac_addr);
		dev_dbg(rvu->dev, "%s MAC (%pM) update to PF=%d success\n",
			__func__, req->mac_addr, pf);
		return 0;
	}

	/* Try deleting and adding it again */
	rc = rvu_npc_exact_del_table_entry_by_id(rvu, req->index);
	if (rc) {
		dev_dbg(rvu->dev, "%s MAC (%pM) del PF=%d failed\n",
			__func__, pfvf->mac_addr, pf);
	}

	/* find mcam entry if exist */
	rc = nix_get_nixlf(rvu, req->hdr.pcifunc, &nixlf, NULL);
	if (!rc) {
		mcam_idx = npc_get_nixlf_mcam_index(&rvu->hw->mcam, req->hdr.pcifunc,
						    nixlf, NIXLF_UCAST_ENTRY);
	}

	rc = rvu_npc_exact_add_table_entry(rvu, cgx_id, lmac_id, req->mac_addr,
					   pfvf->rx_chan_base, 0, &seq_id,
					   true, mcam_idx, req->hdr.pcifunc);
	if (rc) {
		dev_err(rvu->dev, "%s MAC (%pM) add PF=%d failed\n",
			__func__, req->mac_addr, pf);
		return LMAC_AF_ERR_EXACT_MATCH_TBL_ADD_FAILED;
	}

	rsp->index = seq_id;
	ether_addr_copy(rsp->mac_addr, req->mac_addr);
	ether_addr_copy(pfvf->mac_addr, req->mac_addr);
	dev_dbg(rvu->dev,
		"%s MAC (%pM) del and add to PF=%d success (seq_id=%u)\n",
		__func__, req->mac_addr, pf, seq_id);
	return 0;
}

/**
 *	rvu_npc_exact_can_disable_feature - Check if feature can be disabled.
 *      @rvu: resource virtualization unit.
 *	Return: True if exact match feature is supported.
 */
bool rvu_npc_exact_can_disable_feature(struct rvu *rvu)
{
	struct npc_exact_table *table = rvu->hw->table;
	bool empty;

	if (!rvu->hw->cap.npc_exact_match_enabled)
		return false;

	mutex_lock(&table->lock);
	empty = list_empty(&table->lhead_gbl);
	mutex_unlock(&table->lock);

	return empty;
}

/**
 *	rvu_npc_exact_disable_feature - Disable feature.
 *      @rvu: resource virtualization unit.
 */
void rvu_npc_exact_disable_feature(struct rvu *rvu)
{
	rvu->hw->cap.npc_exact_match_enabled = false;
}

/**
 *	rvu_npc_exact_reset - Delete and free all entry which match pcifunc.
 *      @rvu: resource virtualization unit.
 *	@pcifunc: PCI func to match.
 */
void rvu_npc_exact_reset(struct rvu *rvu, u16 pcifunc)
{
	struct npc_exact_table *table = rvu->hw->table;
	struct npc_exact_table_entry *tmp, *iter;
	u32 seq_id;

	mutex_lock(&table->lock);
	list_for_each_entry_safe(iter, tmp, &table->lhead_gbl, glist) {
		if (pcifunc != iter->pcifunc)
			continue;

		seq_id = iter->seq_id;
		dev_dbg(rvu->dev, "%s: resetting pcifun=%d seq_id=%u\n", __func__,
			pcifunc, seq_id);

		mutex_unlock(&table->lock);
		rvu_npc_exact_del_table_entry_by_id(rvu, seq_id);
		mutex_lock(&table->lock);
	}
	mutex_unlock(&table->lock);
}

/**
 *      rvu_npc_exact_init - initialize exact match table
 *      @rvu: resource virtualization unit.
 *
 *	Initialize HW and SW resources to manage 4way-2K table and fully
 *	associative 32-entry mcam table.
 *	Return: 0 upon success.
 */
int rvu_npc_exact_init(struct rvu *rvu)
{
	u64 bcast_mcast_val, bcast_mcast_mask;
	struct npc_exact_table *table;
	u64 exact_val, exact_mask;
	u64 chan_val, chan_mask;
	u8 cgx_id, lmac_id;
	u32 *drop_mcam_idx;
	u16 max_lmac_cnt;
	u64 npc_const3;
	int table_size;
	int blkaddr;
	u16 pcifunc;
	int err, i;
	u64 cfg;
	bool rc;

	/* Read NPC_AF_CONST3 and check for have exact
	 * match functionality is present
	 */
	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -EINVAL;
	}

	/* Check exact match feature is supported */
	npc_const3 = rvu_read64(rvu, blkaddr, NPC_AF_CONST3);
	if (!(npc_const3 & BIT_ULL(62)))
		return 0;

	/* Check if kex profile has enabled EXACT match nibble */
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(NIX_INTF_RX));
	if (!(cfg & NPC_EXACT_NIBBLE_HIT))
		return 0;

	/* Set capability to true */
	rvu->hw->cap.npc_exact_match_enabled = true;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	dev_dbg(rvu->dev, "%s: Memory allocation for table success\n", __func__);
	rvu->hw->table = table;

	/* Read table size, ways and depth */
	table->mem_table.ways = FIELD_GET(GENMASK_ULL(19, 16), npc_const3);
	table->mem_table.depth = FIELD_GET(GENMASK_ULL(15, 0), npc_const3);
	table->cam_table.depth = FIELD_GET(GENMASK_ULL(31, 24), npc_const3);

	dev_dbg(rvu->dev, "%s: NPC exact match 4way_2k table(ways=%d, depth=%d)\n",
		__func__,  table->mem_table.ways, table->cam_table.depth);

	/* Check if depth of table is not a sequre of 2
	 * TODO: why _builtin_popcount() is not working ?
	 */
	if ((table->mem_table.depth & (table->mem_table.depth - 1)) != 0) {
		dev_err(rvu->dev,
			"%s: NPC exact match 4way_2k table depth(%d) is not square of 2\n",
			__func__,  table->mem_table.depth);
		return -EINVAL;
	}

	table_size = table->mem_table.depth * table->mem_table.ways;

	/* Allocate bitmap for 4way 2K table */
	table->mem_table.bmap = devm_bitmap_zalloc(rvu->dev, table_size,
						   GFP_KERNEL);
	if (!table->mem_table.bmap)
		return -ENOMEM;

	dev_dbg(rvu->dev, "%s: Allocated bitmap for 4way 2K entry table\n", __func__);

	/* Allocate bitmap for 32 entry mcam */
	table->cam_table.bmap = devm_bitmap_zalloc(rvu->dev, 32, GFP_KERNEL);

	if (!table->cam_table.bmap)
		return -ENOMEM;

	dev_dbg(rvu->dev, "%s: Allocated bitmap for 32 entry cam\n", __func__);

	table->tot_ids = table_size + table->cam_table.depth;
	table->id_bmap = devm_bitmap_zalloc(rvu->dev, table->tot_ids,
					    GFP_KERNEL);

	if (!table->id_bmap)
		return -ENOMEM;

	dev_dbg(rvu->dev, "%s: Allocated bitmap for id map (total=%d)\n",
		__func__, table->tot_ids);

	/* Initialize list heads for npc_exact_table entries.
	 * This entry is used by debugfs to show entries in
	 * exact match table.
	 */
	for (i = 0; i < NPC_EXACT_TBL_MAX_WAYS; i++)
		INIT_LIST_HEAD(&table->lhead_mem_tbl_entry[i]);

	INIT_LIST_HEAD(&table->lhead_cam_tbl_entry);
	INIT_LIST_HEAD(&table->lhead_gbl);

	mutex_init(&table->lock);

	rvu_exact_config_secret_key(rvu);
	rvu_exact_config_search_key(rvu);

	rvu_exact_config_table_mask(rvu);
	rvu_exact_config_result_ctrl(rvu, table->mem_table.depth);

	/* - No drop rule for LBK
	 * - Drop rules for SDP and each LMAC.
	 */
	exact_val = !NPC_EXACT_RESULT_HIT;
	exact_mask = NPC_EXACT_RESULT_HIT;

	/* nibble - 3	2  1   0
	 *	   L3B L3M L2B L2M
	 */
	bcast_mcast_val = 0b0000;
	bcast_mcast_mask = 0b0011;

	/* Install SDP drop rule */
	drop_mcam_idx = &table->num_drop_rules;

	max_lmac_cnt = rvu->cgx_cnt_max * rvu->hw->lmac_per_cgx +
		       PF_CGXMAP_BASE;

	for (i = PF_CGXMAP_BASE; i < max_lmac_cnt; i++) {
		if (rvu->pf2cgxlmac_map[i] == 0xFF)
			continue;

		rvu_get_cgx_lmac_id(rvu->pf2cgxlmac_map[i], &cgx_id, &lmac_id);

		rc = rvu_npc_exact_calc_drop_rule_chan_and_mask(rvu, NIX_INTF_TYPE_CGX, cgx_id,
								lmac_id, &chan_val, &chan_mask);
		if (!rc) {
			dev_err(rvu->dev,
				"%s: failed, info chan_val=0x%llx chan_mask=0x%llx rule_id=%d\n",
				__func__, chan_val, chan_mask, *drop_mcam_idx);
			return -EINVAL;
		}

		/* Filter rules are only for PF */
		pcifunc = RVU_PFFUNC(rvu->pdev, i, 0);

		dev_dbg(rvu->dev,
			"%s:Drop rule cgx=%d lmac=%d chan(val=0x%llx, mask=0x%llx\n",
			__func__, cgx_id, lmac_id, chan_val, chan_mask);

		rc = rvu_npc_exact_save_drop_rule_chan_and_mask(rvu, table->num_drop_rules,
								chan_val, chan_mask, pcifunc);
		if (!rc) {
			dev_err(rvu->dev,
				"%s: failed to set drop info for cgx=%d, lmac=%d, chan=%llx\n",
				__func__, cgx_id, lmac_id, chan_val);
			return -EINVAL;
		}

		err = npc_install_mcam_drop_rule(rvu, *drop_mcam_idx,
						 &table->counter_idx[*drop_mcam_idx],
						 chan_val, chan_mask,
						 exact_val, exact_mask,
						 bcast_mcast_val, bcast_mcast_mask);
		if (err) {
			dev_err(rvu->dev,
				"failed to configure drop rule (cgx=%d lmac=%d)\n",
				cgx_id, lmac_id);
			return err;
		}

		(*drop_mcam_idx)++;
	}

	dev_info(rvu->dev, "initialized exact match table successfully\n");
	return 0;
}
