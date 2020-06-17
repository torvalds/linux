/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_CACHEINFO_H
#define _ASM_RISCV_CACHEINFO_H

#include <linux/cacheinfo.h>

struct riscv_cacheinfo_ops {
	const struct attribute_group * (*get_priv_group)(struct cacheinfo
							*this_leaf);
};

void riscv_set_cacheinfo_ops(struct riscv_cacheinfo_ops *ops);

#endif /* _ASM_RISCV_CACHEINFO_H */
