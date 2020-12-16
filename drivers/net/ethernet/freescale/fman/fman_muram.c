/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "fman_muram.h"

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/genalloc.h>

struct muram_info {
	struct gen_pool *pool;
	void __iomem *vbase;
	size_t size;
	phys_addr_t pbase;
};

static unsigned long fman_muram_vbase_to_offset(struct muram_info *muram,
						unsigned long vaddr)
{
	return vaddr - (unsigned long)muram->vbase;
}

/**
 * fman_muram_init
 * @base:	Pointer to base of memory mapped FM-MURAM.
 * @size:	Size of the FM-MURAM partition.
 *
 * Creates partition in the MURAM.
 * The routine returns a pointer to the MURAM partition.
 * This pointer must be passed as to all other FM-MURAM function calls.
 * No actual initialization or configuration of FM_MURAM hardware is done by
 * this routine.
 *
 * Return: pointer to FM-MURAM object, or NULL for Failure.
 */
struct muram_info *fman_muram_init(phys_addr_t base, size_t size)
{
	struct muram_info *muram;
	void __iomem *vaddr;
	int ret;

	muram = kzalloc(sizeof(*muram), GFP_KERNEL);
	if (!muram)
		return NULL;

	muram->pool = gen_pool_create(ilog2(64), -1);
	if (!muram->pool) {
		pr_err("%s(): MURAM pool create failed\n", __func__);
		goto  muram_free;
	}

	vaddr = ioremap(base, size);
	if (!vaddr) {
		pr_err("%s(): MURAM ioremap failed\n", __func__);
		goto pool_destroy;
	}

	ret = gen_pool_add_virt(muram->pool, (unsigned long)vaddr,
				base, size, -1);
	if (ret < 0) {
		pr_err("%s(): MURAM pool add failed\n", __func__);
		iounmap(vaddr);
		goto pool_destroy;
	}

	memset_io(vaddr, 0, (int)size);

	muram->vbase = vaddr;
	muram->pbase = base;
	return muram;

pool_destroy:
	gen_pool_destroy(muram->pool);
muram_free:
	kfree(muram);
	return NULL;
}

/**
 * fman_muram_offset_to_vbase
 * @muram:	FM-MURAM module pointer.
 * @offset:	the offset of the memory block
 *
 * Gives the address of the memory region from specific offset
 *
 * Return: The address of the memory block
 */
unsigned long fman_muram_offset_to_vbase(struct muram_info *muram,
					 unsigned long offset)
{
	return offset + (unsigned long)muram->vbase;
}

/**
 * fman_muram_alloc
 * @muram:	FM-MURAM module pointer.
 * @size:	Size of the memory to be allocated.
 *
 * Allocate some memory from FM-MURAM partition.
 *
 * Return: address of the allocated memory; NULL otherwise.
 */
unsigned long fman_muram_alloc(struct muram_info *muram, size_t size)
{
	unsigned long vaddr;

	vaddr = gen_pool_alloc(muram->pool, size);
	if (!vaddr)
		return -ENOMEM;

	memset_io((void __iomem *)vaddr, 0, size);

	return fman_muram_vbase_to_offset(muram, vaddr);
}

/**
 * fman_muram_free_mem
 * @muram:	FM-MURAM module pointer.
 * @offset:	offset of the memory region to be freed.
 * @size:	size of the memory to be freed.
 *
 * Free an allocated memory from FM-MURAM partition.
 */
void fman_muram_free_mem(struct muram_info *muram, unsigned long offset,
			 size_t size)
{
	unsigned long addr = fman_muram_offset_to_vbase(muram, offset);

	gen_pool_free(muram->pool, addr, size);
}
