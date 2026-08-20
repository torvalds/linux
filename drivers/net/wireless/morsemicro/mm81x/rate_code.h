/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_RATE_CODE_H_
#define _MM81X_RATE_CODE_H_

#include <linux/types.h>

enum dot11_bandwidth {
	DOT11_BANDWIDTH_1MHZ = 0,
	DOT11_BANDWIDTH_2MHZ = 1,
	DOT11_BANDWIDTH_4MHZ = 2,
	DOT11_BANDWIDTH_8MHZ = 3,
	DOT11_BANDWIDTH_16MHZ = 4,

	DOT11_MAX_BANDWIDTH = DOT11_BANDWIDTH_16MHZ,
	DOT11_INVALID_BANDWIDTH = 5
};

enum mm81x_rate_preamble {
	/* S1G LONG format (with SIG-A and SIG-B) */
	MM81X_RATE_PREAMBLE_S1G_LONG = 0,
	/* This is the most common format used */
	MM81X_RATE_PREAMBLE_S1G_SHORT = 1,
	/* S1G 1M format */
	MM81X_RATE_PREAMBLE_S1G_1M = 2,

	MM81X_RATE_MAX_PREAMBLE = MM81X_RATE_PREAMBLE_S1G_1M,
	MM81X_RATE_INVALID_PREAMBLE = 7
};

typedef __le32 mm81x_rate_code_t;

#define MM81X_RATECODE_PREAMBLE (0x0000000F)
#define MM81X_RATECODE_MCS_INDEX (0x000000F0)
#define MM81X_RATECODE_NSS_INDEX (0x00000700)
#define MM81X_RATECODE_BW_INDEX (0x00003800)
#define MM81X_RATECODE_RTS_FLAG (0x00010000)
#define MM81X_RATECODE_SHORT_GI_FLAG (0x00040000)
#define MM81X_RATECODE_DUP_BW_INDEX (0x01C00000)

static inline enum mm81x_rate_preamble
mm81x_ratecode_preamble_get(mm81x_rate_code_t rc)
{
	return (enum mm81x_rate_preamble)(
		le32_get_bits(rc, MM81X_RATECODE_PREAMBLE));
}

static inline u8 mm81x_ratecode_mcs_index_get(mm81x_rate_code_t rc)
{
	return le32_get_bits(rc, MM81X_RATECODE_MCS_INDEX);
}

static inline u8 mm81x_ratecode_nss_index_get(mm81x_rate_code_t rc)
{
	return le32_get_bits(rc, MM81X_RATECODE_NSS_INDEX);
}

static inline enum dot11_bandwidth
mm81x_ratecode_bw_index_get(mm81x_rate_code_t rc)
{
	return (enum dot11_bandwidth)(
		le32_get_bits(rc, MM81X_RATECODE_BW_INDEX));
}

static inline bool mm81x_ratecode_rts_get(mm81x_rate_code_t rc)
{
	return le32_get_bits(rc, MM81X_RATECODE_RTS_FLAG);
}

static inline bool mm81x_ratecode_sgi_get(mm81x_rate_code_t rc)
{
	return le32_get_bits(rc, MM81X_RATECODE_SHORT_GI_FLAG);
}

static inline enum dot11_bandwidth
mm81x_ratecode_dup_bw_index_get(mm81x_rate_code_t rc)
{
	return (enum dot11_bandwidth)(
		le32_get_bits(rc, MM81X_RATECODE_DUP_BW_INDEX));
}

#define MM81X_RATECODE_INIT(bw_idx, nss_idx, mcs_idx, preamble)  \
	(le32_encode_bits((bw_idx), MM81X_RATECODE_BW_INDEX) |   \
	 le32_encode_bits((nss_idx), MM81X_RATECODE_NSS_INDEX) | \
	 le32_encode_bits((mcs_idx), MM81X_RATECODE_MCS_INDEX) | \
	 le32_encode_bits((preamble), MM81X_RATECODE_PREAMBLE))

static inline mm81x_rate_code_t
mm81x_ratecode_init(enum dot11_bandwidth bw_index, u32 nss_index, u32 mcs_index,
		    enum mm81x_rate_preamble preamble)
{
	return MM81X_RATECODE_INIT(bw_index, nss_index, mcs_index, preamble);
}

static inline void
mm81x_ratecode_preamble_set(mm81x_rate_code_t *rc,
			    enum mm81x_rate_preamble preamble)
{
	*rc = (*rc & cpu_to_le32(~MM81X_RATECODE_PREAMBLE)) |
	      le32_encode_bits(preamble, MM81X_RATECODE_PREAMBLE);
}

static inline void mm81x_ratecode_mcs_index_set(mm81x_rate_code_t *rc,
						u32 mcs_index)
{
	*rc = (*rc & cpu_to_le32(~MM81X_RATECODE_MCS_INDEX)) |
	      le32_encode_bits(mcs_index, MM81X_RATECODE_MCS_INDEX);
}

static inline void mm81x_ratecode_nss_index_set(mm81x_rate_code_t *rc,
						u32 nss_index)
{
	*rc = (*rc & cpu_to_le32(~MM81X_RATECODE_NSS_INDEX)) |
	      le32_encode_bits(nss_index, MM81X_RATECODE_NSS_INDEX);
}

static inline void mm81x_ratecode_bw_index_set(mm81x_rate_code_t *rc,
					       enum dot11_bandwidth bw_index)
{
	*rc = (*rc & cpu_to_le32(~MM81X_RATECODE_BW_INDEX)) |
	      le32_encode_bits(bw_index, MM81X_RATECODE_BW_INDEX);
}

static inline void
mm81x_ratecode_update_s1g_bw_preamble(mm81x_rate_code_t *rc,
				      enum dot11_bandwidth bw_index)
{
	enum mm81x_rate_preamble pream = MM81X_RATE_PREAMBLE_S1G_SHORT;

	if (bw_index == DOT11_BANDWIDTH_1MHZ)
		pream = MM81X_RATE_PREAMBLE_S1G_1M;

	mm81x_ratecode_preamble_set(rc, pream);
	mm81x_ratecode_bw_index_set(rc, bw_index);
}

static inline void
mm81x_ratecode_dup_bw_index_set(mm81x_rate_code_t *rc,
				enum dot11_bandwidth dup_bw_index)
{
	*rc = (*rc & cpu_to_le32(~MM81X_RATECODE_DUP_BW_INDEX)) |
	      le32_encode_bits(dup_bw_index, MM81X_RATECODE_DUP_BW_INDEX);
}

static inline void mm81x_ratecode_enable_rts(mm81x_rate_code_t *rc)
{
	*rc |= cpu_to_le32(MM81X_RATECODE_RTS_FLAG);
}

static inline void mm81x_ratecode_enable_sgi(mm81x_rate_code_t *rc)
{
	*rc |= cpu_to_le32(MM81X_RATECODE_SHORT_GI_FLAG);
}

static inline enum dot11_bandwidth mm81x_ratecode_bw_mhz_to_bw_index(u8 bw_mhz)
{
	return ((bw_mhz == 1) ? DOT11_BANDWIDTH_1MHZ :
		(bw_mhz == 2) ? DOT11_BANDWIDTH_2MHZ :
		(bw_mhz == 4) ? DOT11_BANDWIDTH_4MHZ :
		(bw_mhz == 8) ? DOT11_BANDWIDTH_8MHZ :
				DOT11_BANDWIDTH_2MHZ);
}

static inline u8
mm81x_ratecode_bw_index_to_s1g_bw_mhz(enum dot11_bandwidth bw_idx)
{
	return ((bw_idx == DOT11_BANDWIDTH_1MHZ) ? 1 :
		(bw_idx == DOT11_BANDWIDTH_2MHZ) ? 2 :
		(bw_idx == DOT11_BANDWIDTH_4MHZ) ? 4 :
		(bw_idx == DOT11_BANDWIDTH_8MHZ) ? 8 :
						   2);
}

#endif
