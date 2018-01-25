/*
 * Copyright (c) 2017 Chen-Yu Tsai. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CCU_SDM_H
#define _CCU_SDM_H

#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_sdm_setting {
	unsigned long	rate;

	/*
	 * XXX We don't know what the step and bottom register fields
	 * mean. Just copy the whole register value from the vendor
	 * kernel for now.
	 */
	u32		pattern;

	/*
	 * M and N factors here should be the values used in
	 * calculation, not the raw values written to registers
	 */
	u32		m;
	u32		n;
};

struct ccu_sdm_internal {
	struct ccu_sdm_setting	*table;
	u32		table_size;
	/* early SoCs don't have the SDM enable bit in the PLL register */
	u32		enable;
	/* second enable bit in tuning register */
	u32		tuning_enable;
	u16		tuning_reg;
};

#define _SUNXI_CCU_SDM(_table, _enable,			\
		       _reg, _reg_enable)		\
	{						\
		.table		= _table,		\
		.table_size	= ARRAY_SIZE(_table),	\
		.enable		= _enable,		\
		.tuning_enable	= _reg_enable,		\
		.tuning_reg	= _reg,			\
	}

bool ccu_sdm_helper_is_enabled(struct ccu_common *common,
			       struct ccu_sdm_internal *sdm);
void ccu_sdm_helper_enable(struct ccu_common *common,
			   struct ccu_sdm_internal *sdm,
			   unsigned long rate);
void ccu_sdm_helper_disable(struct ccu_common *common,
			    struct ccu_sdm_internal *sdm);

bool ccu_sdm_helper_has_rate(struct ccu_common *common,
			     struct ccu_sdm_internal *sdm,
			     unsigned long rate);

unsigned long ccu_sdm_helper_read_rate(struct ccu_common *common,
				       struct ccu_sdm_internal *sdm,
				       u32 m, u32 n);

int ccu_sdm_helper_get_factors(struct ccu_common *common,
			       struct ccu_sdm_internal *sdm,
			       unsigned long rate,
			       unsigned long *m, unsigned long *n);

#endif
