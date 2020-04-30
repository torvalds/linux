/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_INTERNAL_H
#define HW_ATL2_INTERNAL_H

#include "hw_atl2_utils.h"

#define HW_ATL2_MTU_JUMBO  16352U
#define HW_ATL2_MTU        1514U

#define HW_ATL2_TX_RINGS 4U
#define HW_ATL2_RX_RINGS 4U

#define HW_ATL2_RINGS_MAX 32U
#define HW_ATL2_TXD_SIZE       (16U)
#define HW_ATL2_RXD_SIZE       (16U)

#define HW_ATL2_TC_MAX 1U
#define HW_ATL2_RSS_MAX 8U

#define HW_ATL2_MIN_RXD \
	(ALIGN(AQ_CFG_SKB_FRAGS_MAX + 1U, AQ_HW_RXD_MULTIPLE))
#define HW_ATL2_MIN_TXD \
	(ALIGN(AQ_CFG_SKB_FRAGS_MAX + 1U, AQ_HW_TXD_MULTIPLE))

#define HW_ATL2_MAX_RXD 8184U
#define HW_ATL2_MAX_TXD 8184U

struct hw_atl2_priv {
	struct statistics_s last_stats;
	unsigned int art_base_index;
};

#endif /* HW_ATL2_INTERNAL_H */
