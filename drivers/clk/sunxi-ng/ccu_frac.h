/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
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

#ifndef _CCU_FRAC_H_
#define _CCU_FRAC_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_frac_internal {
	u32		enable;
	u32		select;

	unsigned long	rates[2];
};

#define _SUNXI_CCU_FRAC(_enable, _select, _rate1, _rate2)		\
	{								\
		.enable	= _enable,					\
		.select	= _select,					\
		.rates = { _rate1, _rate2 },				\
	}

bool ccu_frac_helper_is_enabled(struct ccu_common *common,
				struct ccu_frac_internal *cf);
void ccu_frac_helper_enable(struct ccu_common *common,
			    struct ccu_frac_internal *cf);
void ccu_frac_helper_disable(struct ccu_common *common,
			     struct ccu_frac_internal *cf);

bool ccu_frac_helper_has_rate(struct ccu_common *common,
			      struct ccu_frac_internal *cf,
			      unsigned long rate);

unsigned long ccu_frac_helper_read_rate(struct ccu_common *common,
					struct ccu_frac_internal *cf);

int ccu_frac_helper_set_rate(struct ccu_common *common,
			     struct ccu_frac_internal *cf,
			     unsigned long rate, u32 lock);

#endif /* _CCU_FRAC_H_ */
