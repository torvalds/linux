// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/refcount.h>

#include "spectrum.h"
#include "spectrum_acl_tcam.h"

struct mlxsw_sp_acl_bf {
	unsigned int bank_size;
	refcount_t refcnt[0];
};

/* Bloom filter uses a crc-16 hash over chunks of data which contain 4 key
 * blocks, eRP ID and region ID. In Spectrum-2, region key is combined of up to
 * 12 key blocks, so there can be up to 3 chunks in the Bloom filter key,
 * depending on the actual number of key blocks used in the region.
 * The layout of the Bloom filter key is as follows:
 *
 * +-------------------------+------------------------+------------------------+
 * | Chunk 2 Key blocks 11-8 | Chunk 1 Key blocks 7-4 | Chunk 0 Key blocks 3-0 |
 * +-------------------------+------------------------+------------------------+
 */
#define MLXSW_BLOOM_KEY_CHUNKS 3
#define MLXSW_BLOOM_KEY_LEN 69

/* Each chunk size is 23 bytes. 18 bytes of it contain 4 key blocks, each is
 * 36 bits, 2 bytes which hold eRP ID and region ID, and 3 bytes of zero
 * padding.
 * The layout of each chunk is as follows:
 *
 * +---------+----------------------+-----------------------------------+
 * | 3 bytes |        2 bytes       |              18 bytes             |
 * +---------+-----------+----------+-----------------------------------+
 * | 183:158 |  157:148  | 147:144  |               143:0               |
 * +---------+-----------+----------+-----------------------------------+
 * |    0    | region ID |  eRP ID  |      4 Key blocks (18 Bytes)      |
 * +---------+-----------+----------+-----------------------------------+
 */
#define MLXSW_BLOOM_CHUNK_PAD_BYTES 3
#define MLXSW_BLOOM_CHUNK_KEY_BYTES 18
#define MLXSW_BLOOM_KEY_CHUNK_BYTES 23

/* The offset of the key block within a chunk is 5 bytes as it comes after
 * 3 bytes of zero padding and 16 bits of region ID and eRP ID.
 */
#define MLXSW_BLOOM_CHUNK_KEY_OFFSET 5

/* Each chunk contains 4 key blocks. Chunk 2 uses key blocks 11-8,
 * and we need to populate it with 4 key blocks copied from the entry encoded
 * key. Since the encoded key contains a padding, key block 11 starts at offset
 * 2. block 7 that is used in chunk 1 starts at offset 20 as 4 key blocks take
 * 18 bytes.
 * This array defines key offsets for easy access when copying key blocks from
 * entry key to Bloom filter chunk.
 */
static const u8 chunk_key_offsets[MLXSW_BLOOM_KEY_CHUNKS] = {2, 20, 38};

/* This table is just the CRC of each possible byte. It is
 * computed, Msbit first, for the Bloom filter polynomial
 * which is 0x8529 (1 + x^3 + x^5 + x^8 + x^10 + x^15 and
 * the implicit x^16).
 */
static const u16 mlxsw_sp_acl_bf_crc_tab[256] = {
0x0000, 0x8529, 0x8f7b, 0x0a52, 0x9bdf, 0x1ef6, 0x14a4, 0x918d,
0xb297, 0x37be, 0x3dec, 0xb8c5, 0x2948, 0xac61, 0xa633, 0x231a,
0xe007, 0x652e, 0x6f7c, 0xea55, 0x7bd8, 0xfef1, 0xf4a3, 0x718a,
0x5290, 0xd7b9, 0xddeb, 0x58c2, 0xc94f, 0x4c66, 0x4634, 0xc31d,
0x4527, 0xc00e, 0xca5c, 0x4f75, 0xdef8, 0x5bd1, 0x5183, 0xd4aa,
0xf7b0, 0x7299, 0x78cb, 0xfde2, 0x6c6f, 0xe946, 0xe314, 0x663d,
0xa520, 0x2009, 0x2a5b, 0xaf72, 0x3eff, 0xbbd6, 0xb184, 0x34ad,
0x17b7, 0x929e, 0x98cc, 0x1de5, 0x8c68, 0x0941, 0x0313, 0x863a,
0x8a4e, 0x0f67, 0x0535, 0x801c, 0x1191, 0x94b8, 0x9eea, 0x1bc3,
0x38d9, 0xbdf0, 0xb7a2, 0x328b, 0xa306, 0x262f, 0x2c7d, 0xa954,
0x6a49, 0xef60, 0xe532, 0x601b, 0xf196, 0x74bf, 0x7eed, 0xfbc4,
0xd8de, 0x5df7, 0x57a5, 0xd28c, 0x4301, 0xc628, 0xcc7a, 0x4953,
0xcf69, 0x4a40, 0x4012, 0xc53b, 0x54b6, 0xd19f, 0xdbcd, 0x5ee4,
0x7dfe, 0xf8d7, 0xf285, 0x77ac, 0xe621, 0x6308, 0x695a, 0xec73,
0x2f6e, 0xaa47, 0xa015, 0x253c, 0xb4b1, 0x3198, 0x3bca, 0xbee3,
0x9df9, 0x18d0, 0x1282, 0x97ab, 0x0626, 0x830f, 0x895d, 0x0c74,
0x91b5, 0x149c, 0x1ece, 0x9be7, 0x0a6a, 0x8f43, 0x8511, 0x0038,
0x2322, 0xa60b, 0xac59, 0x2970, 0xb8fd, 0x3dd4, 0x3786, 0xb2af,
0x71b2, 0xf49b, 0xfec9, 0x7be0, 0xea6d, 0x6f44, 0x6516, 0xe03f,
0xc325, 0x460c, 0x4c5e, 0xc977, 0x58fa, 0xddd3, 0xd781, 0x52a8,
0xd492, 0x51bb, 0x5be9, 0xdec0, 0x4f4d, 0xca64, 0xc036, 0x451f,
0x6605, 0xe32c, 0xe97e, 0x6c57, 0xfdda, 0x78f3, 0x72a1, 0xf788,
0x3495, 0xb1bc, 0xbbee, 0x3ec7, 0xaf4a, 0x2a63, 0x2031, 0xa518,
0x8602, 0x032b, 0x0979, 0x8c50, 0x1ddd, 0x98f4, 0x92a6, 0x178f,
0x1bfb, 0x9ed2, 0x9480, 0x11a9, 0x8024, 0x050d, 0x0f5f, 0x8a76,
0xa96c, 0x2c45, 0x2617, 0xa33e, 0x32b3, 0xb79a, 0xbdc8, 0x38e1,
0xfbfc, 0x7ed5, 0x7487, 0xf1ae, 0x6023, 0xe50a, 0xef58, 0x6a71,
0x496b, 0xcc42, 0xc610, 0x4339, 0xd2b4, 0x579d, 0x5dcf, 0xd8e6,
0x5edc, 0xdbf5, 0xd1a7, 0x548e, 0xc503, 0x402a, 0x4a78, 0xcf51,
0xec4b, 0x6962, 0x6330, 0xe619, 0x7794, 0xf2bd, 0xf8ef, 0x7dc6,
0xbedb, 0x3bf2, 0x31a0, 0xb489, 0x2504, 0xa02d, 0xaa7f, 0x2f56,
0x0c4c, 0x8965, 0x8337, 0x061e, 0x9793, 0x12ba, 0x18e8, 0x9dc1,
};

static u16 mlxsw_sp_acl_bf_crc_byte(u16 crc, u8 c)
{
	return (crc << 8) ^ mlxsw_sp_acl_bf_crc_tab[(crc >> 8) ^ c];
}

static u16 mlxsw_sp_acl_bf_crc(const u8 *buffer, size_t len)
{
	u16 crc = 0;

	while (len--)
		crc = mlxsw_sp_acl_bf_crc_byte(crc, *buffer++);
	return crc;
}

static void
mlxsw_sp_acl_bf_key_encode(struct mlxsw_sp_acl_atcam_region *aregion,
			   struct mlxsw_sp_acl_atcam_entry *aentry,
			   char *output, u8 *len)
{
	struct mlxsw_afk_key_info *key_info = aregion->region->key_info;
	u8 chunk_index, chunk_count, block_count;
	char *chunk = output;
	__be16 erp_region_id;

	block_count = mlxsw_afk_key_info_blocks_count_get(key_info);
	chunk_count = 1 + ((block_count - 1) >> 2);
	erp_region_id = cpu_to_be16(aentry->ht_key.erp_id |
				   (aregion->region->id << 4));
	for (chunk_index = MLXSW_BLOOM_KEY_CHUNKS - chunk_count;
	     chunk_index < MLXSW_BLOOM_KEY_CHUNKS; chunk_index++) {
		memset(chunk, 0, MLXSW_BLOOM_CHUNK_PAD_BYTES);
		memcpy(chunk + MLXSW_BLOOM_CHUNK_PAD_BYTES, &erp_region_id,
		       sizeof(erp_region_id));
		memcpy(chunk + MLXSW_BLOOM_CHUNK_KEY_OFFSET,
		       &aentry->enc_key[chunk_key_offsets[chunk_index]],
		       MLXSW_BLOOM_CHUNK_KEY_BYTES);
		chunk += MLXSW_BLOOM_KEY_CHUNK_BYTES;
	}
	*len = chunk_count * MLXSW_BLOOM_KEY_CHUNK_BYTES;
}

static unsigned int
mlxsw_sp_acl_bf_rule_count_index_get(struct mlxsw_sp_acl_bf *bf,
				     unsigned int erp_bank,
				     unsigned int bf_index)
{
	return erp_bank * bf->bank_size + bf_index;
}

static unsigned int
mlxsw_sp_acl_bf_index_get(struct mlxsw_sp_acl_bf *bf,
			  struct mlxsw_sp_acl_atcam_region *aregion,
			  struct mlxsw_sp_acl_atcam_entry *aentry)
{
	char bf_key[MLXSW_BLOOM_KEY_LEN];
	u8 bf_size;

	mlxsw_sp_acl_bf_key_encode(aregion, aentry, bf_key, &bf_size);
	return mlxsw_sp_acl_bf_crc(bf_key, bf_size);
}

int
mlxsw_sp_acl_bf_entry_add(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_bf *bf,
			  struct mlxsw_sp_acl_atcam_region *aregion,
			  unsigned int erp_bank,
			  struct mlxsw_sp_acl_atcam_entry *aentry)
{
	unsigned int rule_index;
	char *peabfe_pl;
	u16 bf_index;
	int err;

	bf_index = mlxsw_sp_acl_bf_index_get(bf, aregion, aentry);
	rule_index = mlxsw_sp_acl_bf_rule_count_index_get(bf, erp_bank,
							  bf_index);

	if (refcount_inc_not_zero(&bf->refcnt[rule_index]))
		return 0;

	peabfe_pl = kmalloc(MLXSW_REG_PEABFE_LEN, GFP_KERNEL);
	if (!peabfe_pl)
		return -ENOMEM;

	mlxsw_reg_peabfe_pack(peabfe_pl);
	mlxsw_reg_peabfe_rec_pack(peabfe_pl, 0, 1, erp_bank, bf_index);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(peabfe), peabfe_pl);
	kfree(peabfe_pl);
	if (err)
		return err;

	refcount_set(&bf->refcnt[rule_index], 1);
	return 0;
}

void
mlxsw_sp_acl_bf_entry_del(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_bf *bf,
			  struct mlxsw_sp_acl_atcam_region *aregion,
			  unsigned int erp_bank,
			  struct mlxsw_sp_acl_atcam_entry *aentry)
{
	unsigned int rule_index;
	char *peabfe_pl;
	u16 bf_index;

	bf_index = mlxsw_sp_acl_bf_index_get(bf, aregion, aentry);
	rule_index = mlxsw_sp_acl_bf_rule_count_index_get(bf, erp_bank,
							  bf_index);

	if (refcount_dec_and_test(&bf->refcnt[rule_index])) {
		peabfe_pl = kmalloc(MLXSW_REG_PEABFE_LEN, GFP_KERNEL);
		if (!peabfe_pl)
			return;

		mlxsw_reg_peabfe_pack(peabfe_pl);
		mlxsw_reg_peabfe_rec_pack(peabfe_pl, 0, 0, erp_bank, bf_index);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(peabfe), peabfe_pl);
		kfree(peabfe_pl);
	}
}

struct mlxsw_sp_acl_bf *
mlxsw_sp_acl_bf_init(struct mlxsw_sp *mlxsw_sp, unsigned int num_erp_banks)
{
	struct mlxsw_sp_acl_bf *bf;
	unsigned int bf_bank_size;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_MAX_BF_LOG))
		return ERR_PTR(-EIO);

	/* Bloom filter size per erp_table_bank
	 * is 2^ACL_MAX_BF_LOG
	 */
	bf_bank_size = 1 << MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_BF_LOG);
	bf = kzalloc(struct_size(bf, refcnt, bf_bank_size * num_erp_banks),
		     GFP_KERNEL);
	if (!bf)
		return ERR_PTR(-ENOMEM);

	bf->bank_size = bf_bank_size;
	return bf;
}

void mlxsw_sp_acl_bf_fini(struct mlxsw_sp_acl_bf *bf)
{
	kfree(bf);
}
