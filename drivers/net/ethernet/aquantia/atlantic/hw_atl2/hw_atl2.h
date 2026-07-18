/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_H
#define HW_ATL2_H

#include "aq_common.h"
#define HW_ATL2_RX_TS_SIZE 8

#define HW_ATL2_PTP_OFFSET_INGRESS_100          768
#define HW_ATL2_PTP_OFFSET_EGRESS_100           336
#define HW_ATL2_PTP_OFFSET_INGRESS_1000         510
#define HW_ATL2_PTP_OFFSET_EGRESS_1000          105
#define HW_ATL2_PTP_OFFSET_INGRESS_2500         2447
#define HW_ATL2_PTP_OFFSET_EGRESS_2500          634
#define HW_ATL2_PTP_OFFSET_INGRESS_5000         1426
#define HW_ATL2_PTP_OFFSET_EGRESS_5000          361
#define HW_ATL2_PTP_OFFSET_INGRESS_10000        997
#define HW_ATL2_PTP_OFFSET_EGRESS_10000         203

extern const struct aq_hw_caps_s hw_atl2_caps_aqc113;
extern const struct aq_hw_caps_s hw_atl2_caps_aqc115c;
extern const struct aq_hw_caps_s hw_atl2_caps_aqc116c;
extern const struct aq_hw_ops hw_atl2_ops;

#endif /* HW_ATL2_H */
