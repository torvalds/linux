/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Linaro Ltd.
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_ICC_COMMON_H__
#define __DRIVERS_INTERCONNECT_QCOM_ICC_COMMON_H__

#include <linux/interconnect-provider.h>
#include <linux/math64.h>

static inline u64 qcom_bw_div(u64 num, u32 base)
{
	if (num && num < base)
		return 1;

	return div_u64(num, base);
}

struct icc_node_data *qcom_icc_xlate_extended(const struct of_phandle_args *spec,
					      void *data);

#endif
