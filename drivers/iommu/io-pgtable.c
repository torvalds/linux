/*
 * Generic page table allocator for IOMMUs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "io-pgtable.h"

static const struct io_pgtable_init_fns *
io_pgtable_init_table[IO_PGTABLE_NUM_FMTS] =
{
#ifdef CONFIG_IOMMU_IO_PGTABLE_LPAE
	[ARM_32_LPAE_S1] = &io_pgtable_arm_32_lpae_s1_init_fns,
	[ARM_32_LPAE_S2] = &io_pgtable_arm_32_lpae_s2_init_fns,
	[ARM_64_LPAE_S1] = &io_pgtable_arm_64_lpae_s1_init_fns,
	[ARM_64_LPAE_S2] = &io_pgtable_arm_64_lpae_s2_init_fns,
#endif
#ifdef CONFIG_IOMMU_IO_PGTABLE_ARMV7S
	[ARM_V7S] = &io_pgtable_arm_v7s_init_fns,
#endif
};

struct io_pgtable_ops *alloc_io_pgtable_ops(enum io_pgtable_fmt fmt,
					    struct io_pgtable_cfg *cfg,
					    void *cookie)
{
	struct io_pgtable *iop;
	const struct io_pgtable_init_fns *fns;

	if (fmt >= IO_PGTABLE_NUM_FMTS)
		return NULL;

	fns = io_pgtable_init_table[fmt];
	if (!fns)
		return NULL;

	iop = fns->alloc(cfg, cookie);
	if (!iop)
		return NULL;

	iop->fmt	= fmt;
	iop->cookie	= cookie;
	iop->cfg	= *cfg;

	return &iop->ops;
}

/*
 * It is the IOMMU driver's responsibility to ensure that the page table
 * is no longer accessible to the walker by this point.
 */
void free_io_pgtable_ops(struct io_pgtable_ops *ops)
{
	struct io_pgtable *iop;

	if (!ops)
		return;

	iop = container_of(ops, struct io_pgtable, ops);
	io_pgtable_tlb_flush_all(iop);
	io_pgtable_init_table[iop->fmt]->free(iop);
}
