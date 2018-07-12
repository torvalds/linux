/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_DCB_H
#define BNXT_DCB_H

#include <net/dcbnl.h>

struct bnxt_dcb {
	u8			max_tc;
	struct ieee_pfc		*ieee_pfc;
	struct ieee_ets		*ieee_ets;
	u8			dcbx_cap;
	u8			default_pri;
};

struct bnxt_cos2bw_cfg {
	u8			pad[3];
	u8			queue_id;
	__le32			min_bw;
	__le32			max_bw;
#define BW_VALUE_UNIT_PERCENT1_100		(0x1UL << 29)
	u8			tsa;
	u8			pri_lvl;
	u8			bw_weight;
	u8			unused;
};

#define BNXT_LLQ(q_profile)	\
	((q_profile) ==		\
	 QUEUE_QPORTCFG_RESP_QUEUE_ID0_SERVICE_PROFILE_LOSSLESS_ROCE)

#define HWRM_STRUCT_DATA_SUBTYPE_HOST_OPERATIONAL	0x0300

void bnxt_dcb_init(struct bnxt *bp);
void bnxt_dcb_free(struct bnxt *bp);
#endif
