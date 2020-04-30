/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_INTERNAL_H
#define HW_ATL2_INTERNAL_H

#include "hw_atl2_utils.h"

#define HW_ATL2_MTU_JUMBO  16352U

struct hw_atl2_priv {
	struct statistics_s last_stats;
};

#endif /* HW_ATL2_INTERNAL_H */
