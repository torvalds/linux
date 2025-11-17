/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_RES_H__
#define __BNG_RES_H__

#define BNG_ROCE_FW_MAX_TIMEOUT	60

struct bng_re_chip_ctx {
	u16	chip_num;
	u16	hw_stats_size;
	u64	hwrm_intf_ver;
	u16	hwrm_cmd_max_timeout;
};

#endif
