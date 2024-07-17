// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/refcount.h>
#include <linux/mutex.h>

#include "spectrum.h"
#include "spectrum_acl_tcam.h"

struct mlxsw_sp_acl_bf {
	struct mutex lock; /* Protects Bloom Filter updates. */
	unsigned int bank_size;
	refcount_t refcnt[];
};

/* Bloom filter uses a crc-16 hash over chunks of data which contain 4 key
 * blocks, eRP ID and region ID. In Spectrum-2 and above, region key is combined
 * of up to 12 key blocks, so there can be up to 3 chunks in the Bloom filter
 * key, depending on the actual number of key blocks used in the region.
 * The layout of the Bloom filter key is as follows:
 *
 * +-------------------------+------------------------+------------------------+
 * | Chunk 2 Key blocks 11-8 | Chunk 1 Key blocks 7-4 | Chunk 0 Key blocks 3-0 |
 * +-------------------------+------------------------+------------------------+
 */
#define MLXSW_BLOOM_KEY_CHUNKS 3

/* Spectrum-2 and Spectrum-3 chunks */
#define MLXSW_SP2_BLOOM_KEY_LEN 69

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
#define MLXSW_SP2_BLOOM_CHUNK_PAD_BYTES 3
#define MLXSW_SP2_BLOOM_CHUNK_KEY_BYTES 18
#define MLXSW_SP2_BLOOM_KEY_CHUNK_BYTES 23

/* The offset of the key block within a chunk is 5 bytes as it comes after
 * 3 bytes of zero padding and 16 bits of region ID and eRP ID.
 */
#define MLXSW_SP2_BLOOM_CHUNK_KEY_OFFSET 5

/* This table is just the CRC of each possible byte which is used for
 * Spectrum-{2-3}. It is computed, Msbit first, for the Bloom filter
 * polynomial which is 0x8529 (1 + x^3 + x^5 + x^8 + x^10 + x^15 and
 * the implicit x^16).
 */
static const u16 mlxsw_sp2_acl_bf_crc16_tab[256] = {
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

/* Spectrum-4 chunks */
#define MLXSW_SP4_BLOOM_KEY_LEN 60

/* In Spectrum-4, there is no padding. Each chunk size is 20 bytes.
 * 18 bytes of it contain 4 key blocks, each is 36 bits, and 2 bytes which hold
 * eRP ID and region ID.
 * The layout of each chunk is as follows:
 *
 * +----------------------+-----------------------------------+
 * |        2 bytes       |              18 bytes             |
 * +-----------+----------+-----------------------------------+
 * |  157:148  | 147:144  |               143:0               |
 * +---------+-----------+----------+-------------------------+
 * | region ID |  eRP ID  |      4 Key blocks (18 Bytes)      |
 * +-----------+----------+-----------------------------------+
 */

#define MLXSW_SP4_BLOOM_CHUNK_PAD_BYTES 0
#define MLXSW_SP4_BLOOM_CHUNK_KEY_BYTES 18
#define MLXSW_SP4_BLOOM_KEY_CHUNK_BYTES 20

/* The offset of the key block within a chunk is 2 bytes as it comes after
 * 16 bits of region ID and eRP ID.
 */
#define MLXSW_SP4_BLOOM_CHUNK_KEY_OFFSET 2

/* For Spectrum-4, two hash functions are used, CRC-10 and CRC-6 based.
 * The result is combination of the two calculations -
 * 6 bit column are MSB (result of CRC-6),
 * 10 bit row are LSB (result of CRC-10).
 */

/* This table is just the CRC of each possible byte which is used for
 * Spectrum-4. It is computed, Msbit first, for the Bloom filter
 * polynomial which is 0x1b (1 + x^1 + x^3 + x^4 and the implicit x^10).
 */
static const u16 mlxsw_sp4_acl_bf_crc10_tab[256] = {
0x0000, 0x001b, 0x0036, 0x002d, 0x006c, 0x0077, 0x005a, 0x0041,
0x00d8, 0x00c3, 0x00ee, 0x00f5, 0x00b4, 0x00af, 0x0082, 0x0099,
0x01b0, 0x01ab, 0x0186, 0x019d, 0x01dc, 0x01c7, 0x01ea, 0x01f1,
0x0168, 0x0173, 0x015e, 0x0145, 0x0104, 0x011f, 0x0132, 0x0129,
0x0360, 0x037b, 0x0356, 0x034d, 0x030c, 0x0317, 0x033a, 0x0321,
0x03b8, 0x03a3, 0x038e, 0x0395, 0x03d4, 0x03cf, 0x03e2, 0x03f9,
0x02d0, 0x02cb, 0x02e6, 0x02fd, 0x02bc, 0x02a7, 0x028a, 0x0291,
0x0208, 0x0213, 0x023e, 0x0225, 0x0264, 0x027f, 0x0252, 0x0249,
0x02db, 0x02c0, 0x02ed, 0x02f6, 0x02b7, 0x02ac, 0x0281, 0x029a,
0x0203, 0x0218, 0x0235, 0x022e, 0x026f, 0x0274, 0x0259, 0x0242,
0x036b, 0x0370, 0x035d, 0x0346, 0x0307, 0x031c, 0x0331, 0x032a,
0x03b3, 0x03a8, 0x0385, 0x039e, 0x03df, 0x03c4, 0x03e9, 0x03f2,
0x01bb, 0x01a0, 0x018d, 0x0196, 0x01d7, 0x01cc, 0x01e1, 0x01fa,
0x0163, 0x0178, 0x0155, 0x014e, 0x010f, 0x0114, 0x0139, 0x0122,
0x000b, 0x0010, 0x003d, 0x0026, 0x0067, 0x007c, 0x0051, 0x004a,
0x00d3, 0x00c8, 0x00e5, 0x00fe, 0x00bf, 0x00a4, 0x0089, 0x0092,
0x01ad, 0x01b6, 0x019b, 0x0180, 0x01c1, 0x01da, 0x01f7, 0x01ec,
0x0175, 0x016e, 0x0143, 0x0158, 0x0119, 0x0102, 0x012f, 0x0134,
0x001d, 0x0006, 0x002b, 0x0030, 0x0071, 0x006a, 0x0047, 0x005c,
0x00c5, 0x00de, 0x00f3, 0x00e8, 0x00a9, 0x00b2, 0x009f, 0x0084,
0x02cd, 0x02d6, 0x02fb, 0x02e0, 0x02a1, 0x02ba, 0x0297, 0x028c,
0x0215, 0x020e, 0x0223, 0x0238, 0x0279, 0x0262, 0x024f, 0x0254,
0x037d, 0x0366, 0x034b, 0x0350, 0x0311, 0x030a, 0x0327, 0x033c,
0x03a5, 0x03be, 0x0393, 0x0388, 0x03c9, 0x03d2, 0x03ff, 0x03e4,
0x0376, 0x036d, 0x0340, 0x035b, 0x031a, 0x0301, 0x032c, 0x0337,
0x03ae, 0x03b5, 0x0398, 0x0383, 0x03c2, 0x03d9, 0x03f4, 0x03ef,
0x02c6, 0x02dd, 0x02f0, 0x02eb, 0x02aa, 0x02b1, 0x029c, 0x0287,
0x021e, 0x0205, 0x0228, 0x0233, 0x0272, 0x0269, 0x0244, 0x025f,
0x0016, 0x000d, 0x0020, 0x003b, 0x007a, 0x0061, 0x004c, 0x0057,
0x00ce, 0x00d5, 0x00f8, 0x00e3, 0x00a2, 0x00b9, 0x0094, 0x008f,
0x01a6, 0x01bd, 0x0190, 0x018b, 0x01ca, 0x01d1, 0x01fc, 0x01e7,
0x017e, 0x0165, 0x0148, 0x0153, 0x0112, 0x0109, 0x0124, 0x013f,
};

/* This table is just the CRC of each possible byte which is used for
 * Spectrum-4. It is computed, Msbit first, for the Bloom filter
 * polynomial which is 0x2d (1 + x^2+ x^3 + x^5 and the implicit x^6).
 */
static const u8 mlxsw_sp4_acl_bf_crc6_tab[256] = {
0x00, 0x2d, 0x37, 0x1a, 0x03, 0x2e, 0x34, 0x19,
0x06, 0x2b, 0x31, 0x1c, 0x05, 0x28, 0x32, 0x1f,
0x0c, 0x21, 0x3b, 0x16, 0x0f, 0x22, 0x38, 0x15,
0x0a, 0x27, 0x3d, 0x10, 0x09, 0x24, 0x3e, 0x13,
0x18, 0x35, 0x2f, 0x02, 0x1b, 0x36, 0x2c, 0x01,
0x1e, 0x33, 0x29, 0x04, 0x1d, 0x30, 0x2a, 0x07,
0x14, 0x39, 0x23, 0x0e, 0x17, 0x3a, 0x20, 0x0d,
0x12, 0x3f, 0x25, 0x08, 0x11, 0x3c, 0x26, 0x0b,
0x30, 0x1d, 0x07, 0x2a, 0x33, 0x1e, 0x04, 0x29,
0x36, 0x1b, 0x01, 0x2c, 0x35, 0x18, 0x02, 0x2f,
0x3c, 0x11, 0x0b, 0x26, 0x3f, 0x12, 0x08, 0x25,
0x3a, 0x17, 0x0d, 0x20, 0x39, 0x14, 0x0e, 0x23,
0x28, 0x05, 0x1f, 0x32, 0x2b, 0x06, 0x1c, 0x31,
0x2e, 0x03, 0x19, 0x34, 0x2d, 0x00, 0x1a, 0x37,
0x24, 0x09, 0x13, 0x3e, 0x27, 0x0a, 0x10, 0x3d,
0x22, 0x0f, 0x15, 0x38, 0x21, 0x0c, 0x16, 0x3b,
0x0d, 0x20, 0x3a, 0x17, 0x0e, 0x23, 0x39, 0x14,
0x0b, 0x26, 0x3c, 0x11, 0x08, 0x25, 0x3f, 0x12,
0x01, 0x2c, 0x36, 0x1b, 0x02, 0x2f, 0x35, 0x18,
0x07, 0x2a, 0x30, 0x1d, 0x04, 0x29, 0x33, 0x1e,
0x15, 0x38, 0x22, 0x0f, 0x16, 0x3b, 0x21, 0x0c,
0x13, 0x3e, 0x24, 0x09, 0x10, 0x3d, 0x27, 0x0a,
0x19, 0x34, 0x2e, 0x03, 0x1a, 0x37, 0x2d, 0x00,
0x1f, 0x32, 0x28, 0x05, 0x1c, 0x31, 0x2b, 0x06,
0x3d, 0x10, 0x0a, 0x27, 0x3e, 0x13, 0x09, 0x24,
0x3b, 0x16, 0x0c, 0x21, 0x38, 0x15, 0x0f, 0x22,
0x31, 0x1c, 0x06, 0x2b, 0x32, 0x1f, 0x05, 0x28,
0x37, 0x1a, 0x00, 0x2d, 0x34, 0x19, 0x03, 0x2e,
0x25, 0x08, 0x12, 0x3f, 0x26, 0x0b, 0x11, 0x3c,
0x23, 0x0e, 0x14, 0x39, 0x20, 0x0d, 0x17, 0x3a,
0x29, 0x04, 0x1e, 0x33, 0x2a, 0x07, 0x1d, 0x30,
0x2f, 0x02, 0x18, 0x35, 0x2c, 0x01, 0x1b, 0x36,
};

/* Each chunk contains 4 key blocks. Chunk 2 uses key blocks 11-8,
 * and we need to populate it with 4 key blocks copied from the entry encoded
 * key. The original keys layout is same for Spectrum-{2,3,4}.
 * Since the encoded key contains a 2 bytes padding, key block 11 starts at
 * offset 2. block 7 that is used in chunk 1 starts at offset 20 as 4 key blocks
 * take 18 bytes. See 'MLXSW_SP2_AFK_BLOCK_LAYOUT' for more details.
 * This array defines key offsets for easy access when copying key blocks from
 * entry key to Bloom filter chunk.
 */
static const u8 chunk_key_offsets[MLXSW_BLOOM_KEY_CHUNKS] = {2, 20, 38};

static u16 mlxsw_sp2_acl_bf_crc16_byte(u16 crc, u8 c)
{
	return (crc << 8) ^ mlxsw_sp2_acl_bf_crc16_tab[(crc >> 8) ^ c];
}

static u16 mlxsw_sp2_acl_bf_crc(const u8 *buffer, size_t len)
{
	u16 crc = 0;

	while (len--)
		crc = mlxsw_sp2_acl_bf_crc16_byte(crc, *buffer++);
	return crc;
}

static void
__mlxsw_sp_acl_bf_key_encode(struct mlxsw_sp_acl_atcam_region *aregion,
			     struct mlxsw_sp_acl_atcam_entry *aentry,
			     char *output, u8 *len, u8 max_chunks, u8 pad_bytes,
			     u8 key_offset, u8 chunk_key_len, u8 chunk_len)
{
	struct mlxsw_afk_key_info *key_info = aregion->region->key_info;
	u8 chunk_index, chunk_count, block_count;
	char *chunk = output;
	__be16 erp_region_id;

	block_count = mlxsw_afk_key_info_blocks_count_get(key_info);
	chunk_count = 1 + ((block_count - 1) >> 2);
	erp_region_id = cpu_to_be16(aentry->ht_key.erp_id |
				   (aregion->region->id << 4));
	for (chunk_index = max_chunks - chunk_count; chunk_index < max_chunks;
	     chunk_index++) {
		memset(chunk, 0, pad_bytes);
		memcpy(chunk + pad_bytes, &erp_region_id,
		       sizeof(erp_region_id));
		memcpy(chunk + key_offset,
		       &aentry->ht_key.enc_key[chunk_key_offsets[chunk_index]],
		       chunk_key_len);
		chunk += chunk_len;
	}
	*len = chunk_count * chunk_len;
}

static void
mlxsw_sp2_acl_bf_key_encode(struct mlxsw_sp_acl_atcam_region *aregion,
			    struct mlxsw_sp_acl_atcam_entry *aentry,
			    char *output, u8 *len)
{
	__mlxsw_sp_acl_bf_key_encode(aregion, aentry, output, len,
				     MLXSW_BLOOM_KEY_CHUNKS,
				     MLXSW_SP2_BLOOM_CHUNK_PAD_BYTES,
				     MLXSW_SP2_BLOOM_CHUNK_KEY_OFFSET,
				     MLXSW_SP2_BLOOM_CHUNK_KEY_BYTES,
				     MLXSW_SP2_BLOOM_KEY_CHUNK_BYTES);
}

static unsigned int
mlxsw_sp2_acl_bf_index_get(struct mlxsw_sp_acl_bf *bf,
			   struct mlxsw_sp_acl_atcam_region *aregion,
			   struct mlxsw_sp_acl_atcam_entry *aentry)
{
	char bf_key[MLXSW_SP2_BLOOM_KEY_LEN];
	u8 bf_size;

	mlxsw_sp2_acl_bf_key_encode(aregion, aentry, bf_key, &bf_size);
	return mlxsw_sp2_acl_bf_crc(bf_key, bf_size);
}

static u16 mlxsw_sp4_acl_bf_crc10_byte(u16 crc, u8 c)
{
	u8 index = ((crc >> 2) ^ c) & 0xff;

	return ((crc << 8) ^ mlxsw_sp4_acl_bf_crc10_tab[index]) & 0x3ff;
}

static u16 mlxsw_sp4_acl_bf_crc6_byte(u16 crc, u8 c)
{
	u8 index = (crc ^ c) & 0xff;

	return ((crc << 6) ^ (mlxsw_sp4_acl_bf_crc6_tab[index] << 2)) & 0xfc;
}

static u16 mlxsw_sp4_acl_bf_crc(const u8 *buffer, size_t len)
{
	u16 crc_row = 0, crc_col = 0;

	while (len--) {
		crc_row = mlxsw_sp4_acl_bf_crc10_byte(crc_row, *buffer);
		crc_col = mlxsw_sp4_acl_bf_crc6_byte(crc_col, *buffer);
		buffer++;
	}

	crc_col >>= 2;

	/* 6 bit column are MSB, 10 bit row are LSB */
	return (crc_col << 10) | crc_row;
}

static void right_shift_array(char *arr, u8 len, u8 shift_bits)
{
	u8 byte_mask = 0xff >> shift_bits;
	int i;

	if (WARN_ON(!shift_bits || shift_bits >= 8))
		return;

	for (i = len - 1; i >= 0; i--) {
		/* The first iteration looks like out-of-bounds access,
		 * but actually references a buffer that the array is shifted
		 * into. This move is legal as we never send the last chunk to
		 * this function.
		 */
		arr[i + 1] &= byte_mask;
		arr[i + 1] |= arr[i] << (8 - shift_bits);
		arr[i] = arr[i] >> shift_bits;
	}
}

static void mlxsw_sp4_bf_key_shift_chunks(u8 chunk_count, char *output)
{
	/* The chunks are suppoosed to be continuous, with no padding.
	 * Since region ID and eRP ID use 14 bits, and not fully 2 bytes,
	 * and in Spectrum-4 there is no padding, it is necessary to shift some
	 * chunks 2 bits right.
	 */
	switch (chunk_count) {
	case 2:
		/* The chunks are copied as follow:
		 * +-------------+-----------------+
		 * | Chunk 0     |   Chunk 1       |
		 * | IDs  | keys |(**) IDs  | keys |
		 * +-------------+-----------------+
		 * In (**), there are two unused bits, therefore, chunk 0 needs
		 * to be shifted two bits right.
		 */
		right_shift_array(output, MLXSW_SP4_BLOOM_KEY_CHUNK_BYTES, 2);
		break;
	case 3:
		/* The chunks are copied as follow:
		 * +-------------+-----------------+-----------------+
		 * | Chunk 0     |   Chunk 1       |   Chunk 2       |
		 * | IDs  | keys |(**) IDs  | keys |(**) IDs  | keys |
		 * +-------------+-----------------+-----------------+
		 * In (**), there are two unused bits, therefore, chunk 1 needs
		 * to be shifted two bits right and chunk 0 needs to be shifted
		 * four bits right.
		 */
		right_shift_array(output + MLXSW_SP4_BLOOM_KEY_CHUNK_BYTES,
				  MLXSW_SP4_BLOOM_KEY_CHUNK_BYTES, 2);
		right_shift_array(output, MLXSW_SP4_BLOOM_KEY_CHUNK_BYTES, 4);
		break;
	default:
		WARN_ON(chunk_count > MLXSW_BLOOM_KEY_CHUNKS);
	}
}

static void
mlxsw_sp4_acl_bf_key_encode(struct mlxsw_sp_acl_atcam_region *aregion,
			    struct mlxsw_sp_acl_atcam_entry *aentry,
			    char *output, u8 *len)
{
	struct mlxsw_afk_key_info *key_info = aregion->region->key_info;
	u8 block_count = mlxsw_afk_key_info_blocks_count_get(key_info);
	u8 chunk_count = 1 + ((block_count - 1) >> 2);

	__mlxsw_sp_acl_bf_key_encode(aregion, aentry, output, len,
				     MLXSW_BLOOM_KEY_CHUNKS,
				     MLXSW_SP4_BLOOM_CHUNK_PAD_BYTES,
				     MLXSW_SP4_BLOOM_CHUNK_KEY_OFFSET,
				     MLXSW_SP4_BLOOM_CHUNK_KEY_BYTES,
				     MLXSW_SP4_BLOOM_KEY_CHUNK_BYTES);
	mlxsw_sp4_bf_key_shift_chunks(chunk_count, output);
}

static unsigned int
mlxsw_sp4_acl_bf_index_get(struct mlxsw_sp_acl_bf *bf,
			   struct mlxsw_sp_acl_atcam_region *aregion,
			   struct mlxsw_sp_acl_atcam_entry *aentry)
{
	char bf_key[MLXSW_SP4_BLOOM_KEY_LEN] = {};
	u8 bf_size;

	mlxsw_sp4_acl_bf_key_encode(aregion, aentry, bf_key, &bf_size);
	return mlxsw_sp4_acl_bf_crc(bf_key, bf_size);
}

static unsigned int
mlxsw_sp_acl_bf_rule_count_index_get(struct mlxsw_sp_acl_bf *bf,
				     unsigned int erp_bank,
				     unsigned int bf_index)
{
	return erp_bank * bf->bank_size + bf_index;
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

	mutex_lock(&bf->lock);

	bf_index = mlxsw_sp->acl_bf_ops->index_get(bf, aregion, aentry);
	rule_index = mlxsw_sp_acl_bf_rule_count_index_get(bf, erp_bank,
							  bf_index);

	if (refcount_inc_not_zero(&bf->refcnt[rule_index])) {
		err = 0;
		goto unlock;
	}

	peabfe_pl = kmalloc(MLXSW_REG_PEABFE_LEN, GFP_KERNEL);
	if (!peabfe_pl) {
		err = -ENOMEM;
		goto unlock;
	}

	mlxsw_reg_peabfe_pack(peabfe_pl);
	mlxsw_reg_peabfe_rec_pack(peabfe_pl, 0, 1, erp_bank, bf_index);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(peabfe), peabfe_pl);
	kfree(peabfe_pl);
	if (err)
		goto unlock;

	refcount_set(&bf->refcnt[rule_index], 1);
	err = 0;

unlock:
	mutex_unlock(&bf->lock);
	return err;
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

	mutex_lock(&bf->lock);

	bf_index = mlxsw_sp->acl_bf_ops->index_get(bf, aregion, aentry);
	rule_index = mlxsw_sp_acl_bf_rule_count_index_get(bf, erp_bank,
							  bf_index);

	if (refcount_dec_and_test(&bf->refcnt[rule_index])) {
		peabfe_pl = kmalloc(MLXSW_REG_PEABFE_LEN, GFP_KERNEL);
		if (!peabfe_pl)
			goto unlock;

		mlxsw_reg_peabfe_pack(peabfe_pl);
		mlxsw_reg_peabfe_rec_pack(peabfe_pl, 0, 0, erp_bank, bf_index);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(peabfe), peabfe_pl);
		kfree(peabfe_pl);
	}

unlock:
	mutex_unlock(&bf->lock);
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
	bf = kzalloc(struct_size(bf, refcnt, size_mul(bf_bank_size, num_erp_banks)),
		     GFP_KERNEL);
	if (!bf)
		return ERR_PTR(-ENOMEM);

	bf->bank_size = bf_bank_size;
	mutex_init(&bf->lock);

	return bf;
}

void mlxsw_sp_acl_bf_fini(struct mlxsw_sp_acl_bf *bf)
{
	mutex_destroy(&bf->lock);
	kfree(bf);
}

const struct mlxsw_sp_acl_bf_ops mlxsw_sp2_acl_bf_ops = {
	.index_get = mlxsw_sp2_acl_bf_index_get,
};

const struct mlxsw_sp_acl_bf_ops mlxsw_sp4_acl_bf_ops = {
	.index_get = mlxsw_sp4_acl_bf_index_get,
};
