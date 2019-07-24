// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2016-2018, NXP Semiconductors
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105_static_config.h"
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>

/* Convenience wrappers over the generic packing functions. These take into
 * account the SJA1105 memory layout quirks and provide some level of
 * programmer protection against incorrect API use. The errors are not expected
 * to occur durring runtime, therefore printing and swallowing them here is
 * appropriate instead of clutterring up higher-level code.
 */
void sja1105_pack(void *buf, const u64 *val, int start, int end, size_t len)
{
	int rc = packing(buf, (u64 *)val, start, end, len,
			 PACK, QUIRK_LSW32_IS_FIRST);

	if (likely(!rc))
		return;

	if (rc == -EINVAL) {
		pr_err("Start bit (%d) expected to be larger than end (%d)\n",
		       start, end);
	} else if (rc == -ERANGE) {
		if ((start - end + 1) > 64)
			pr_err("Field %d-%d too large for 64 bits!\n",
			       start, end);
		else
			pr_err("Cannot store %llx inside bits %d-%d (would truncate)\n",
			       *val, start, end);
	}
	dump_stack();
}

void sja1105_unpack(const void *buf, u64 *val, int start, int end, size_t len)
{
	int rc = packing((void *)buf, val, start, end, len,
			 UNPACK, QUIRK_LSW32_IS_FIRST);

	if (likely(!rc))
		return;

	if (rc == -EINVAL)
		pr_err("Start bit (%d) expected to be larger than end (%d)\n",
		       start, end);
	else if (rc == -ERANGE)
		pr_err("Field %d-%d too large for 64 bits!\n",
		       start, end);
	dump_stack();
}

void sja1105_packing(void *buf, u64 *val, int start, int end,
		     size_t len, enum packing_op op)
{
	int rc = packing(buf, val, start, end, len, op, QUIRK_LSW32_IS_FIRST);

	if (likely(!rc))
		return;

	if (rc == -EINVAL) {
		pr_err("Start bit (%d) expected to be larger than end (%d)\n",
		       start, end);
	} else if (rc == -ERANGE) {
		if ((start - end + 1) > 64)
			pr_err("Field %d-%d too large for 64 bits!\n",
			       start, end);
		else
			pr_err("Cannot store %llx inside bits %d-%d (would truncate)\n",
			       *val, start, end);
	}
	dump_stack();
}

/* Little-endian Ethernet CRC32 of data packed as big-endian u32 words */
u32 sja1105_crc32(const void *buf, size_t len)
{
	unsigned int i;
	u64 word;
	u32 crc;

	/* seed */
	crc = ~0;
	for (i = 0; i < len; i += 4) {
		sja1105_unpack((void *)buf + i, &word, 31, 0, 4);
		crc = crc32_le(crc, (u8 *)&word, 4);
	}
	return ~crc;
}

static size_t sja1105et_avb_params_entry_packing(void *buf, void *entry_ptr,
						 enum packing_op op)
{
	const size_t size = SJA1105ET_SIZE_AVB_PARAMS_ENTRY;
	struct sja1105_avb_params_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->destmeta, 95, 48, size, op);
	sja1105_packing(buf, &entry->srcmeta,  47,  0, size, op);
	return size;
}

static size_t sja1105pqrs_avb_params_entry_packing(void *buf, void *entry_ptr,
						   enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY;
	struct sja1105_avb_params_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->destmeta,   125,  78, size, op);
	sja1105_packing(buf, &entry->srcmeta,     77,  30, size, op);
	return size;
}

static size_t sja1105et_general_params_entry_packing(void *buf, void *entry_ptr,
						     enum packing_op op)
{
	const size_t size = SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY;
	struct sja1105_general_params_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->vllupformat, 319, 319, size, op);
	sja1105_packing(buf, &entry->mirr_ptacu,  318, 318, size, op);
	sja1105_packing(buf, &entry->switchid,    317, 315, size, op);
	sja1105_packing(buf, &entry->hostprio,    314, 312, size, op);
	sja1105_packing(buf, &entry->mac_fltres1, 311, 264, size, op);
	sja1105_packing(buf, &entry->mac_fltres0, 263, 216, size, op);
	sja1105_packing(buf, &entry->mac_flt1,    215, 168, size, op);
	sja1105_packing(buf, &entry->mac_flt0,    167, 120, size, op);
	sja1105_packing(buf, &entry->incl_srcpt1, 119, 119, size, op);
	sja1105_packing(buf, &entry->incl_srcpt0, 118, 118, size, op);
	sja1105_packing(buf, &entry->send_meta1,  117, 117, size, op);
	sja1105_packing(buf, &entry->send_meta0,  116, 116, size, op);
	sja1105_packing(buf, &entry->casc_port,   115, 113, size, op);
	sja1105_packing(buf, &entry->host_port,   112, 110, size, op);
	sja1105_packing(buf, &entry->mirr_port,   109, 107, size, op);
	sja1105_packing(buf, &entry->vlmarker,    106,  75, size, op);
	sja1105_packing(buf, &entry->vlmask,       74,  43, size, op);
	sja1105_packing(buf, &entry->tpid,         42,  27, size, op);
	sja1105_packing(buf, &entry->ignore2stf,   26,  26, size, op);
	sja1105_packing(buf, &entry->tpid2,        25,  10, size, op);
	return size;
}

static size_t
sja1105pqrs_general_params_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY;
	struct sja1105_general_params_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->vllupformat, 351, 351, size, op);
	sja1105_packing(buf, &entry->mirr_ptacu,  350, 350, size, op);
	sja1105_packing(buf, &entry->switchid,    349, 347, size, op);
	sja1105_packing(buf, &entry->hostprio,    346, 344, size, op);
	sja1105_packing(buf, &entry->mac_fltres1, 343, 296, size, op);
	sja1105_packing(buf, &entry->mac_fltres0, 295, 248, size, op);
	sja1105_packing(buf, &entry->mac_flt1,    247, 200, size, op);
	sja1105_packing(buf, &entry->mac_flt0,    199, 152, size, op);
	sja1105_packing(buf, &entry->incl_srcpt1, 151, 151, size, op);
	sja1105_packing(buf, &entry->incl_srcpt0, 150, 150, size, op);
	sja1105_packing(buf, &entry->send_meta1,  149, 149, size, op);
	sja1105_packing(buf, &entry->send_meta0,  148, 148, size, op);
	sja1105_packing(buf, &entry->casc_port,   147, 145, size, op);
	sja1105_packing(buf, &entry->host_port,   144, 142, size, op);
	sja1105_packing(buf, &entry->mirr_port,   141, 139, size, op);
	sja1105_packing(buf, &entry->vlmarker,    138, 107, size, op);
	sja1105_packing(buf, &entry->vlmask,      106,  75, size, op);
	sja1105_packing(buf, &entry->tpid,         74,  59, size, op);
	sja1105_packing(buf, &entry->ignore2stf,   58,  58, size, op);
	sja1105_packing(buf, &entry->tpid2,        57,  42, size, op);
	sja1105_packing(buf, &entry->queue_ts,     41,  41, size, op);
	sja1105_packing(buf, &entry->egrmirrvid,   40,  29, size, op);
	sja1105_packing(buf, &entry->egrmirrpcp,   28,  26, size, op);
	sja1105_packing(buf, &entry->egrmirrdei,   25,  25, size, op);
	sja1105_packing(buf, &entry->replay_port,  24,  22, size, op);
	return size;
}

static size_t
sja1105_l2_forwarding_params_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op)
{
	const size_t size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY;
	struct sja1105_l2_forwarding_params_entry *entry = entry_ptr;
	int offset, i;

	sja1105_packing(buf, &entry->max_dynp, 95, 93, size, op);
	for (i = 0, offset = 13; i < 8; i++, offset += 10)
		sja1105_packing(buf, &entry->part_spc[i],
				offset + 9, offset + 0, size, op);
	return size;
}

size_t sja1105_l2_forwarding_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op)
{
	const size_t size = SJA1105_SIZE_L2_FORWARDING_ENTRY;
	struct sja1105_l2_forwarding_entry *entry = entry_ptr;
	int offset, i;

	sja1105_packing(buf, &entry->bc_domain,  63, 59, size, op);
	sja1105_packing(buf, &entry->reach_port, 58, 54, size, op);
	sja1105_packing(buf, &entry->fl_domain,  53, 49, size, op);
	for (i = 0, offset = 25; i < 8; i++, offset += 3)
		sja1105_packing(buf, &entry->vlan_pmap[i],
				offset + 2, offset + 0, size, op);
	return size;
}

static size_t
sja1105et_l2_lookup_params_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op)
{
	const size_t size = SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY;
	struct sja1105_l2_lookup_params_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->maxage,         31, 17, size, op);
	sja1105_packing(buf, &entry->dyn_tbsz,       16, 14, size, op);
	sja1105_packing(buf, &entry->poly,           13,  6, size, op);
	sja1105_packing(buf, &entry->shared_learn,    5,  5, size, op);
	sja1105_packing(buf, &entry->no_enf_hostprt,  4,  4, size, op);
	sja1105_packing(buf, &entry->no_mgmt_learn,   3,  3, size, op);
	return size;
}

static size_t
sja1105pqrs_l2_lookup_params_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY;
	struct sja1105_l2_lookup_params_entry *entry = entry_ptr;
	int offset, i;

	for (i = 0, offset = 58; i < 5; i++, offset += 11)
		sja1105_packing(buf, &entry->maxaddrp[i],
				offset + 10, offset + 0, size, op);
	sja1105_packing(buf, &entry->maxage,         57,  43, size, op);
	sja1105_packing(buf, &entry->start_dynspc,   42,  33, size, op);
	sja1105_packing(buf, &entry->drpnolearn,     32,  28, size, op);
	sja1105_packing(buf, &entry->shared_learn,   27,  27, size, op);
	sja1105_packing(buf, &entry->no_enf_hostprt, 26,  26, size, op);
	sja1105_packing(buf, &entry->no_mgmt_learn,  25,  25, size, op);
	sja1105_packing(buf, &entry->use_static,     24,  24, size, op);
	sja1105_packing(buf, &entry->owr_dyn,        23,  23, size, op);
	sja1105_packing(buf, &entry->learn_once,     22,  22, size, op);
	return size;
}

size_t sja1105et_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op)
{
	const size_t size = SJA1105ET_SIZE_L2_LOOKUP_ENTRY;
	struct sja1105_l2_lookup_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->vlanid,    95, 84, size, op);
	sja1105_packing(buf, &entry->macaddr,   83, 36, size, op);
	sja1105_packing(buf, &entry->destports, 35, 31, size, op);
	sja1105_packing(buf, &entry->enfport,   30, 30, size, op);
	sja1105_packing(buf, &entry->index,     29, 20, size, op);
	return size;
}

size_t sja1105pqrs_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY;
	struct sja1105_l2_lookup_entry *entry = entry_ptr;

	if (entry->lockeds) {
		sja1105_packing(buf, &entry->tsreg,    159, 159, size, op);
		sja1105_packing(buf, &entry->mirrvlan, 158, 147, size, op);
		sja1105_packing(buf, &entry->takets,   146, 146, size, op);
		sja1105_packing(buf, &entry->mirr,     145, 145, size, op);
		sja1105_packing(buf, &entry->retag,    144, 144, size, op);
	} else {
		sja1105_packing(buf, &entry->touched,  159, 159, size, op);
		sja1105_packing(buf, &entry->age,      158, 144, size, op);
	}
	sja1105_packing(buf, &entry->mask_iotag,   143, 143, size, op);
	sja1105_packing(buf, &entry->mask_vlanid,  142, 131, size, op);
	sja1105_packing(buf, &entry->mask_macaddr, 130,  83, size, op);
	sja1105_packing(buf, &entry->iotag,         82,  82, size, op);
	sja1105_packing(buf, &entry->vlanid,        81,  70, size, op);
	sja1105_packing(buf, &entry->macaddr,       69,  22, size, op);
	sja1105_packing(buf, &entry->destports,     21,  17, size, op);
	sja1105_packing(buf, &entry->enfport,       16,  16, size, op);
	sja1105_packing(buf, &entry->index,         15,   6, size, op);
	return size;
}

static size_t sja1105_l2_policing_entry_packing(void *buf, void *entry_ptr,
						enum packing_op op)
{
	const size_t size = SJA1105_SIZE_L2_POLICING_ENTRY;
	struct sja1105_l2_policing_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->sharindx,  63, 58, size, op);
	sja1105_packing(buf, &entry->smax,      57, 42, size, op);
	sja1105_packing(buf, &entry->rate,      41, 26, size, op);
	sja1105_packing(buf, &entry->maxlen,    25, 15, size, op);
	sja1105_packing(buf, &entry->partition, 14, 12, size, op);
	return size;
}

static size_t sja1105et_mac_config_entry_packing(void *buf, void *entry_ptr,
						 enum packing_op op)
{
	const size_t size = SJA1105ET_SIZE_MAC_CONFIG_ENTRY;
	struct sja1105_mac_config_entry *entry = entry_ptr;
	int offset, i;

	for (i = 0, offset = 72; i < 8; i++, offset += 19) {
		sja1105_packing(buf, &entry->enabled[i],
				offset +  0, offset +  0, size, op);
		sja1105_packing(buf, &entry->base[i],
				offset +  9, offset +  1, size, op);
		sja1105_packing(buf, &entry->top[i],
				offset + 18, offset + 10, size, op);
	}
	sja1105_packing(buf, &entry->ifg,       71, 67, size, op);
	sja1105_packing(buf, &entry->speed,     66, 65, size, op);
	sja1105_packing(buf, &entry->tp_delin,  64, 49, size, op);
	sja1105_packing(buf, &entry->tp_delout, 48, 33, size, op);
	sja1105_packing(buf, &entry->maxage,    32, 25, size, op);
	sja1105_packing(buf, &entry->vlanprio,  24, 22, size, op);
	sja1105_packing(buf, &entry->vlanid,    21, 10, size, op);
	sja1105_packing(buf, &entry->ing_mirr,   9,  9, size, op);
	sja1105_packing(buf, &entry->egr_mirr,   8,  8, size, op);
	sja1105_packing(buf, &entry->drpnona664, 7,  7, size, op);
	sja1105_packing(buf, &entry->drpdtag,    6,  6, size, op);
	sja1105_packing(buf, &entry->drpuntag,   5,  5, size, op);
	sja1105_packing(buf, &entry->retag,      4,  4, size, op);
	sja1105_packing(buf, &entry->dyn_learn,  3,  3, size, op);
	sja1105_packing(buf, &entry->egress,     2,  2, size, op);
	sja1105_packing(buf, &entry->ingress,    1,  1, size, op);
	return size;
}

size_t sja1105pqrs_mac_config_entry_packing(void *buf, void *entry_ptr,
					    enum packing_op op)
{
	const size_t size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY;
	struct sja1105_mac_config_entry *entry = entry_ptr;
	int offset, i;

	for (i = 0, offset = 104; i < 8; i++, offset += 19) {
		sja1105_packing(buf, &entry->enabled[i],
				offset +  0, offset +  0, size, op);
		sja1105_packing(buf, &entry->base[i],
				offset +  9, offset +  1, size, op);
		sja1105_packing(buf, &entry->top[i],
				offset + 18, offset + 10, size, op);
	}
	sja1105_packing(buf, &entry->ifg,       103, 99, size, op);
	sja1105_packing(buf, &entry->speed,      98, 97, size, op);
	sja1105_packing(buf, &entry->tp_delin,   96, 81, size, op);
	sja1105_packing(buf, &entry->tp_delout,  80, 65, size, op);
	sja1105_packing(buf, &entry->maxage,     64, 57, size, op);
	sja1105_packing(buf, &entry->vlanprio,   56, 54, size, op);
	sja1105_packing(buf, &entry->vlanid,     53, 42, size, op);
	sja1105_packing(buf, &entry->ing_mirr,   41, 41, size, op);
	sja1105_packing(buf, &entry->egr_mirr,   40, 40, size, op);
	sja1105_packing(buf, &entry->drpnona664, 39, 39, size, op);
	sja1105_packing(buf, &entry->drpdtag,    38, 38, size, op);
	sja1105_packing(buf, &entry->drpuntag,   35, 35, size, op);
	sja1105_packing(buf, &entry->retag,      34, 34, size, op);
	sja1105_packing(buf, &entry->dyn_learn,  33, 33, size, op);
	sja1105_packing(buf, &entry->egress,     32, 32, size, op);
	sja1105_packing(buf, &entry->ingress,    31, 31, size, op);
	return size;
}

size_t sja1105_vlan_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op)
{
	const size_t size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY;
	struct sja1105_vlan_lookup_entry *entry = entry_ptr;

	sja1105_packing(buf, &entry->ving_mirr,  63, 59, size, op);
	sja1105_packing(buf, &entry->vegr_mirr,  58, 54, size, op);
	sja1105_packing(buf, &entry->vmemb_port, 53, 49, size, op);
	sja1105_packing(buf, &entry->vlan_bc,    48, 44, size, op);
	sja1105_packing(buf, &entry->tag_port,   43, 39, size, op);
	sja1105_packing(buf, &entry->vlanid,     38, 27, size, op);
	return size;
}

static size_t sja1105_xmii_params_entry_packing(void *buf, void *entry_ptr,
						enum packing_op op)
{
	const size_t size = SJA1105_SIZE_XMII_PARAMS_ENTRY;
	struct sja1105_xmii_params_entry *entry = entry_ptr;
	int offset, i;

	for (i = 0, offset = 17; i < 5; i++, offset += 3) {
		sja1105_packing(buf, &entry->xmii_mode[i],
				offset + 1, offset + 0, size, op);
		sja1105_packing(buf, &entry->phy_mac[i],
				offset + 2, offset + 2, size, op);
	}
	return size;
}

size_t sja1105_table_header_packing(void *buf, void *entry_ptr,
				    enum packing_op op)
{
	const size_t size = SJA1105_SIZE_TABLE_HEADER;
	struct sja1105_table_header *entry = entry_ptr;

	sja1105_packing(buf, &entry->block_id, 31, 24, size, op);
	sja1105_packing(buf, &entry->len,      55, 32, size, op);
	sja1105_packing(buf, &entry->crc,      95, 64, size, op);
	return size;
}

/* WARNING: the *hdr pointer is really non-const, because it is
 * modifying the CRC of the header for a 2-stage packing operation
 */
void
sja1105_table_header_pack_with_crc(void *buf, struct sja1105_table_header *hdr)
{
	/* First pack the table as-is, then calculate the CRC, and
	 * finally put the proper CRC into the packed buffer
	 */
	memset(buf, 0, SJA1105_SIZE_TABLE_HEADER);
	sja1105_table_header_packing(buf, hdr, PACK);
	hdr->crc = sja1105_crc32(buf, SJA1105_SIZE_TABLE_HEADER - 4);
	sja1105_pack(buf + SJA1105_SIZE_TABLE_HEADER - 4, &hdr->crc, 31, 0, 4);
}

static void sja1105_table_write_crc(u8 *table_start, u8 *crc_ptr)
{
	u64 computed_crc;
	int len_bytes;

	len_bytes = (uintptr_t)(crc_ptr - table_start);
	computed_crc = sja1105_crc32(table_start, len_bytes);
	sja1105_pack(crc_ptr, &computed_crc, 31, 0, 4);
}

/* The block IDs that the switches support are unfortunately sparse, so keep a
 * mapping table to "block indices" and translate back and forth so that we
 * don't waste useless memory in struct sja1105_static_config.
 * Also, since the block id comes from essentially untrusted input (unpacking
 * the static config from userspace) it has to be sanitized (range-checked)
 * before blindly indexing kernel memory with the blk_idx.
 */
static u64 blk_id_map[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = BLKID_L2_LOOKUP,
	[BLK_IDX_L2_POLICING] = BLKID_L2_POLICING,
	[BLK_IDX_VLAN_LOOKUP] = BLKID_VLAN_LOOKUP,
	[BLK_IDX_L2_FORWARDING] = BLKID_L2_FORWARDING,
	[BLK_IDX_MAC_CONFIG] = BLKID_MAC_CONFIG,
	[BLK_IDX_L2_LOOKUP_PARAMS] = BLKID_L2_LOOKUP_PARAMS,
	[BLK_IDX_L2_FORWARDING_PARAMS] = BLKID_L2_FORWARDING_PARAMS,
	[BLK_IDX_AVB_PARAMS] = BLKID_AVB_PARAMS,
	[BLK_IDX_GENERAL_PARAMS] = BLKID_GENERAL_PARAMS,
	[BLK_IDX_XMII_PARAMS] = BLKID_XMII_PARAMS,
};

const char *sja1105_static_config_error_msg[] = {
	[SJA1105_CONFIG_OK] = "",
	[SJA1105_MISSING_L2_POLICING_TABLE] =
		"l2-policing-table needs to have at least one entry",
	[SJA1105_MISSING_L2_FORWARDING_TABLE] =
		"l2-forwarding-table is either missing or incomplete",
	[SJA1105_MISSING_L2_FORWARDING_PARAMS_TABLE] =
		"l2-forwarding-parameters-table is missing",
	[SJA1105_MISSING_GENERAL_PARAMS_TABLE] =
		"general-parameters-table is missing",
	[SJA1105_MISSING_VLAN_TABLE] =
		"vlan-lookup-table needs to have at least the default untagged VLAN",
	[SJA1105_MISSING_XMII_TABLE] =
		"xmii-table is missing",
	[SJA1105_MISSING_MAC_TABLE] =
		"mac-configuration-table needs to contain an entry for each port",
	[SJA1105_OVERCOMMITTED_FRAME_MEMORY] =
		"Not allowed to overcommit frame memory. L2 memory partitions "
		"and VL memory partitions share the same space. The sum of all "
		"16 memory partitions is not allowed to be larger than 929 "
		"128-byte blocks (or 910 with retagging). Please adjust "
		"l2-forwarding-parameters-table.part_spc and/or "
		"vl-forwarding-parameters-table.partspc.",
};

static sja1105_config_valid_t
static_config_check_memory_size(const struct sja1105_table *tables)
{
	const struct sja1105_l2_forwarding_params_entry *l2_fwd_params;
	int i, mem = 0;

	l2_fwd_params = tables[BLK_IDX_L2_FORWARDING_PARAMS].entries;

	for (i = 0; i < 8; i++)
		mem += l2_fwd_params->part_spc[i];

	if (mem > SJA1105_MAX_FRAME_MEMORY)
		return SJA1105_OVERCOMMITTED_FRAME_MEMORY;

	return SJA1105_CONFIG_OK;
}

sja1105_config_valid_t
sja1105_static_config_check_valid(const struct sja1105_static_config *config)
{
	const struct sja1105_table *tables = config->tables;
#define IS_FULL(blk_idx) \
	(tables[blk_idx].entry_count == tables[blk_idx].ops->max_entry_count)

	if (tables[BLK_IDX_L2_POLICING].entry_count == 0)
		return SJA1105_MISSING_L2_POLICING_TABLE;

	if (tables[BLK_IDX_VLAN_LOOKUP].entry_count == 0)
		return SJA1105_MISSING_VLAN_TABLE;

	if (!IS_FULL(BLK_IDX_L2_FORWARDING))
		return SJA1105_MISSING_L2_FORWARDING_TABLE;

	if (!IS_FULL(BLK_IDX_MAC_CONFIG))
		return SJA1105_MISSING_MAC_TABLE;

	if (!IS_FULL(BLK_IDX_L2_FORWARDING_PARAMS))
		return SJA1105_MISSING_L2_FORWARDING_PARAMS_TABLE;

	if (!IS_FULL(BLK_IDX_GENERAL_PARAMS))
		return SJA1105_MISSING_GENERAL_PARAMS_TABLE;

	if (!IS_FULL(BLK_IDX_XMII_PARAMS))
		return SJA1105_MISSING_XMII_TABLE;

	return static_config_check_memory_size(tables);
#undef IS_FULL
}

void
sja1105_static_config_pack(void *buf, struct sja1105_static_config *config)
{
	struct sja1105_table_header header = {0};
	enum sja1105_blk_idx i;
	char *p = buf;
	int j;

	sja1105_pack(p, &config->device_id, 31, 0, 4);
	p += SJA1105_SIZE_DEVICE_ID;

	for (i = 0; i < BLK_IDX_MAX; i++) {
		const struct sja1105_table *table;
		char *table_start;

		table = &config->tables[i];
		if (!table->entry_count)
			continue;

		header.block_id = blk_id_map[i];
		header.len = table->entry_count *
			     table->ops->packed_entry_size / 4;
		sja1105_table_header_pack_with_crc(p, &header);
		p += SJA1105_SIZE_TABLE_HEADER;
		table_start = p;
		for (j = 0; j < table->entry_count; j++) {
			u8 *entry_ptr = table->entries;

			entry_ptr += j * table->ops->unpacked_entry_size;
			memset(p, 0, table->ops->packed_entry_size);
			table->ops->packing(p, entry_ptr, PACK);
			p += table->ops->packed_entry_size;
		}
		sja1105_table_write_crc(table_start, p);
		p += 4;
	}
	/* Final header:
	 * Block ID does not matter
	 * Length of 0 marks that header is final
	 * CRC will be replaced on-the-fly on "config upload"
	 */
	header.block_id = 0;
	header.len = 0;
	header.crc = 0xDEADBEEF;
	memset(p, 0, SJA1105_SIZE_TABLE_HEADER);
	sja1105_table_header_packing(p, &header, PACK);
}

size_t
sja1105_static_config_get_length(const struct sja1105_static_config *config)
{
	unsigned int sum;
	unsigned int header_count;
	enum sja1105_blk_idx i;

	/* Ending header */
	header_count = 1;
	sum = SJA1105_SIZE_DEVICE_ID;

	/* Tables (headers and entries) */
	for (i = 0; i < BLK_IDX_MAX; i++) {
		const struct sja1105_table *table;

		table = &config->tables[i];
		if (table->entry_count)
			header_count++;

		sum += table->ops->packed_entry_size * table->entry_count;
	}
	/* Headers have an additional CRC at the end */
	sum += header_count * (SJA1105_SIZE_TABLE_HEADER + 4);
	/* Last header does not have an extra CRC because there is no data */
	sum -= 4;

	return sum;
}

/* Compatibility matrices */

/* SJA1105E: First generation, no TTEthernet */
struct sja1105_table_ops sja1105e_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.packing = sja1105et_l2_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.packing = sja1105_l2_policing_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.packing = sja1105_vlan_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.packing = sja1105_l2_forwarding_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.packing = sja1105et_mac_config_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105ET_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.packing = sja1105et_l2_lookup_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.packing = sja1105_l2_forwarding_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.packing = sja1105et_avb_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.packing = sja1105et_general_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.packing = sja1105_xmii_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105T: First generation, TTEthernet */
struct sja1105_table_ops sja1105t_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.packing = sja1105et_l2_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.packing = sja1105_l2_policing_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.packing = sja1105_vlan_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.packing = sja1105_l2_forwarding_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.packing = sja1105et_mac_config_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105ET_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.packing = sja1105et_l2_lookup_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.packing = sja1105_l2_forwarding_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.packing = sja1105et_avb_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.packing = sja1105et_general_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.packing = sja1105_xmii_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105P: Second generation, no TTEthernet, no SGMII */
struct sja1105_table_ops sja1105p_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.packing = sja1105pqrs_l2_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.packing = sja1105_l2_policing_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.packing = sja1105_vlan_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.packing = sja1105_l2_forwarding_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.packing = sja1105pqrs_mac_config_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.packing = sja1105pqrs_l2_lookup_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.packing = sja1105_l2_forwarding_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.packing = sja1105pqrs_avb_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.packing = sja1105pqrs_general_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.packing = sja1105_xmii_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105Q: Second generation, TTEthernet, no SGMII */
struct sja1105_table_ops sja1105q_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.packing = sja1105pqrs_l2_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.packing = sja1105_l2_policing_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.packing = sja1105_vlan_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.packing = sja1105_l2_forwarding_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.packing = sja1105pqrs_mac_config_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.packing = sja1105pqrs_l2_lookup_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.packing = sja1105_l2_forwarding_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.packing = sja1105pqrs_avb_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.packing = sja1105pqrs_general_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.packing = sja1105_xmii_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105R: Second generation, no TTEthernet, SGMII */
struct sja1105_table_ops sja1105r_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.packing = sja1105pqrs_l2_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.packing = sja1105_l2_policing_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.packing = sja1105_vlan_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.packing = sja1105_l2_forwarding_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.packing = sja1105pqrs_mac_config_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.packing = sja1105pqrs_l2_lookup_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.packing = sja1105_l2_forwarding_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.packing = sja1105pqrs_avb_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.packing = sja1105pqrs_general_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.packing = sja1105_xmii_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

/* SJA1105S: Second generation, TTEthernet, SGMII */
struct sja1105_table_ops sja1105s_table_ops[BLK_IDX_MAX] = {
	[BLK_IDX_L2_LOOKUP] = {
		.packing = sja1105pqrs_l2_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_POLICING] = {
		.packing = sja1105_l2_policing_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_policing_entry),
		.packed_entry_size = SJA1105_SIZE_L2_POLICING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_POLICING_COUNT,
	},
	[BLK_IDX_VLAN_LOOKUP] = {
		.packing = sja1105_vlan_lookup_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_vlan_lookup_entry),
		.packed_entry_size = SJA1105_SIZE_VLAN_LOOKUP_ENTRY,
		.max_entry_count = SJA1105_MAX_VLAN_LOOKUP_COUNT,
	},
	[BLK_IDX_L2_FORWARDING] = {
		.packing = sja1105_l2_forwarding_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_COUNT,
	},
	[BLK_IDX_MAC_CONFIG] = {
		.packing = sja1105pqrs_mac_config_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_mac_config_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY,
		.max_entry_count = SJA1105_MAX_MAC_CONFIG_COUNT,
	},
	[BLK_IDX_L2_LOOKUP_PARAMS] = {
		.packing = sja1105pqrs_l2_lookup_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_lookup_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT,
	},
	[BLK_IDX_L2_FORWARDING_PARAMS] = {
		.packing = sja1105_l2_forwarding_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_l2_forwarding_params_entry),
		.packed_entry_size = SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT,
	},
	[BLK_IDX_AVB_PARAMS] = {
		.packing = sja1105pqrs_avb_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_avb_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_AVB_PARAMS_COUNT,
	},
	[BLK_IDX_GENERAL_PARAMS] = {
		.packing = sja1105pqrs_general_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_general_params_entry),
		.packed_entry_size = SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_GENERAL_PARAMS_COUNT,
	},
	[BLK_IDX_XMII_PARAMS] = {
		.packing = sja1105_xmii_params_entry_packing,
		.unpacked_entry_size = sizeof(struct sja1105_xmii_params_entry),
		.packed_entry_size = SJA1105_SIZE_XMII_PARAMS_ENTRY,
		.max_entry_count = SJA1105_MAX_XMII_PARAMS_COUNT,
	},
};

int sja1105_static_config_init(struct sja1105_static_config *config,
			       const struct sja1105_table_ops *static_ops,
			       u64 device_id)
{
	enum sja1105_blk_idx i;

	*config = (struct sja1105_static_config) {0};

	/* Transfer static_ops array from priv into per-table ops
	 * for handier access
	 */
	for (i = 0; i < BLK_IDX_MAX; i++)
		config->tables[i].ops = &static_ops[i];

	config->device_id = device_id;
	return 0;
}

void sja1105_static_config_free(struct sja1105_static_config *config)
{
	enum sja1105_blk_idx i;

	for (i = 0; i < BLK_IDX_MAX; i++) {
		if (config->tables[i].entry_count) {
			kfree(config->tables[i].entries);
			config->tables[i].entry_count = 0;
		}
	}
}

int sja1105_table_delete_entry(struct sja1105_table *table, int i)
{
	size_t entry_size = table->ops->unpacked_entry_size;
	u8 *entries = table->entries;

	if (i > table->entry_count)
		return -ERANGE;

	memmove(entries + i * entry_size, entries + (i + 1) * entry_size,
		(table->entry_count - i) * entry_size);

	table->entry_count--;

	return 0;
}

/* No pointers to table->entries should be kept when this is called. */
int sja1105_table_resize(struct sja1105_table *table, size_t new_count)
{
	size_t entry_size = table->ops->unpacked_entry_size;
	void *new_entries, *old_entries = table->entries;

	if (new_count > table->ops->max_entry_count)
		return -ERANGE;

	new_entries = kcalloc(new_count, entry_size, GFP_KERNEL);
	if (!new_entries)
		return -ENOMEM;

	memcpy(new_entries, old_entries, min(new_count, table->entry_count) *
		entry_size);

	table->entries = new_entries;
	table->entry_count = new_count;
	kfree(old_entries);
	return 0;
}
