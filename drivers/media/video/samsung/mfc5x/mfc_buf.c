/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_buf.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Buffer manager for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/err.h>

#include "mfc.h"
#include "mfc_mem.h"
#include "mfc_buf.h"
#include "mfc_log.h"
#include "mfc_errno.h"

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
#include <plat/s5p-vcm.h>

#include "ump_kernel_interface.h"
#include "ump_kernel_interface_ref_drv.h"
#include "ump_kernel_interface_vcm.h"
#endif

#define PRINT_BUF
#undef DEBUG_ALLOC_FREE

static struct list_head mfc_alloc_head[MFC_MAX_MEM_PORT_NUM];
/* The free node list sorted by real address */
static struct list_head mfc_free_head[MFC_MAX_MEM_PORT_NUM];

static enum MFC_BUF_ALLOC_SCHEME buf_alloc_scheme = MBS_BEST_FIT;

/* FIXME: test locking, add locking mechanisim */
/*
static spinlock_t lock;
*/


void mfc_print_buf(void)
{
#ifdef PRINT_BUF
	struct list_head *pos;
	struct mfc_alloc_buffer *alloc = NULL;
	struct mfc_free_buffer *free = NULL;
	int port, i;

	for (port = 0; port < mfc_mem_count(); port++) {
		mfc_dbg("---- port %d buffer list ----", port);

		i = 0;
		list_for_each(pos, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);
			mfc_dbg("[A #%04d] addr: 0x%08x, size: %d",
				i, (unsigned int)alloc->addr, alloc->size);
			mfc_dbg("\t  real: 0x%08lx", alloc->real);
			mfc_dbg("\t  type: 0x%08x, owner: %d",
				alloc->type, alloc->owner);
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
			mfc_dbg("\t* vcm sysmmu");
			if (alloc->vcm_s) {
				mfc_dbg("\t  start: 0x%08x, res_size  : 0x%08x\n",
					(unsigned int)alloc->vcm_s->res.start,
					(unsigned int)alloc->vcm_s->res.res_size);
				mfc_dbg("\t  bound_size: 0x%08x\n",
					(unsigned int)alloc->vcm_s->res.bound_size);
			}

			mfc_dbg("\t* vcm kernel");
			if (alloc->vcm_k) {
				mfc_dbg("\t  start: 0x%08x, res_size  : 0x%08x\n",
					(unsigned int)alloc->vcm_k->start,
					(unsigned int)alloc->vcm_k->res_size);
				mfc_dbg("\t  bound_size: 0x%08x\n",
					(unsigned int)alloc->vcm_k->bound_size);
			}

			mfc_dbg("\t* ump");
			if (alloc->ump_handle) {
				mfc_dbg("\t  secure id: 0x%08x",
					mfc_ump_get_id(alloc->ump_handle));
			}
#elif defined(CONFIG_S5P_VMEM)
			mfc_dbg("\t  vmem cookie: 0x%08x addr: 0x%08lx, size: %d",
				alloc->vmem_cookie, alloc->vmem_addr,
				alloc->vmem_size);
#else
			mfc_dbg("\t  offset: 0x%08x", alloc->ofs);
#endif
			i++;
		}

		i = 0;
		list_for_each(pos, &mfc_free_head[port]) {
			free = list_entry(pos, struct mfc_free_buffer, list);
			mfc_dbg("[F #%04d] addr: 0x%08lx, size: %d",
				i, free->real, free->size);
			i++;
		}
	}
#endif
}

static int mfc_put_free_buf(unsigned long addr, unsigned int size, int port)
{
	struct list_head *pos, *nxt;
	struct mfc_free_buffer *free;
	struct mfc_free_buffer *next = NULL;
	struct mfc_free_buffer *prev;
	/* 0x00: not merged, 0x01: prev merged, 0x02: next merged */
	int merged = 0x00;

	if (!size)
		return -EINVAL;

	mfc_dbg("addr: 0x%08lx, size: %d, port: %d\n", addr, size, port);

	list_for_each_safe(pos, nxt, &mfc_free_head[port]) {
		next = list_entry(pos, struct mfc_free_buffer, list);

		/*
		 * When the allocated address must be align without VMEM,
		 * the free buffer can be overlap
		 * previous free buffer temporaily
		 * Target buffer will be shrink after this operation
		 */
		if (addr <= next->real) {
			prev = list_entry(pos->prev, struct mfc_free_buffer, list);

			mfc_dbg("prev->addr: 0x%08lx, size: %d", prev->real, prev->size);
			/* merge previous free buffer */
			if (prev && ((prev->real + prev->size) == addr)) {
				addr  = prev->real;
				size += prev->size;

				prev->size = size;

				merged |= 0x01;
				mfc_dbg("auto merge free buffer[p]: addr: 0x%08lx, size: %d",
					prev->real, prev->size);
			}

			mfc_dbg("next->addr: 0x%08lx, size: %d", next->real, next->size);
			/* merge next free buffer */
			if ((addr + size) == next->real) {
				next->real  = addr;
				next->size += size;

				if (merged)
					prev->size = next->size;

				merged |= 0x02;
				mfc_dbg("auto merge free buffer[n]: addr: 0x%08lx, size: %d",
					next->real, next->size);
			}

			break;
		}
	}

	if (!merged) {
		free = (struct mfc_free_buffer *)
			kzalloc(sizeof(struct mfc_free_buffer), GFP_KERNEL);

		if (unlikely(free == NULL))
			return -ENOMEM;

		free->real = addr;
		free->size = size;

		list_add_tail(&free->list, pos);
	}

	/* bi-directional merged */
	else if ((merged & 0x03) == 0x03) {
		list_del(&next->list);
		kfree(next);
	}

	return 0;
}

static unsigned long mfc_get_free_buf(unsigned int size, int align, int port)
{
	struct list_head *pos, *nxt;
	struct mfc_free_buffer *free;
	struct mfc_free_buffer *match = NULL;
	int align_size = 0;
	unsigned long addr = 0;

	mfc_dbg("size: %d, align: %d, port: %d\n",
			size, align, port);

	if (list_empty(&mfc_free_head[port])) {
		mfc_err("no free node in mfc buffer\n");

		return 0;
	}

	/* find best fit area */
	list_for_each_safe(pos, nxt, &mfc_free_head[port]) {
		free = list_entry(pos, struct mfc_free_buffer, list);

#if (defined(CONFIG_VIDEO_MFC_VCM_UMP) || defined(CONFIG_S5P_VMEM))
		/*
		 * Align the start address.
		 * We assume the start address of free buffer aligned with 4KB
		 */
		align_size = ALIGN(align_size + size, PAGE_SIZE) - size;

		if (align > PAGE_SIZE) {
			align_size  = ALIGN(free->real, align) - free->real;
			align_size += ALIGN(align_size + size, PAGE_SIZE) - size;
		} else {
			align_size = ALIGN(align_size + size, PAGE_SIZE) - size;
		}
#else
		align_size = ALIGN(free->real, align) - free->real;
#endif
		if (free->size >= (size + align_size)) {
			if (buf_alloc_scheme == MBS_BEST_FIT) {
				if (match != NULL) {
					if (free->size < match->size)
						match = free;
				} else {
					match = free;
				}
			} else if (buf_alloc_scheme == MBS_FIRST_FIT) {
				match = free;
				break;
			}
		}
	}

	if (match != NULL) {
		addr = match->real;
		align_size = ALIGN(addr, align) - addr;

#if !(defined(CONFIG_VIDEO_MFC_VCM_UMP) || defined(CONFIG_S5P_VMEM))
		if (align_size > 0) {
			/*
			 * When the allocated address must be align without VMEM,
			 * the free buffer can be overlap
			 * previous free buffer temporaily
			 */
			if (mfc_put_free_buf(match->real, align_size, port) < 0)
				return 0;
		}
#endif
		/* change allocated buffer address & size */
		match->real += (size + align_size);
		match->size -= (size + align_size);

		if (match->size == 0) {
			list_del(&match->list);
			kfree(match);
		}
	} else {
		mfc_err("no suitable free node in mfc buffer\n");

		return 0;
	}

	return addr;
}

int mfc_init_buf(void)
{
#ifndef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	int port;
#endif
	int ret = 0;

#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION
	INIT_LIST_HEAD(&mfc_alloc_head[0]);
	INIT_LIST_HEAD(&mfc_free_head[0]);

	if (mfc_put_free_buf(mfc_mem_data_base(0),
		mfc_mem_data_size(0), 0) < 0)
		mfc_err("failed to add free buffer: [0x%08lx: %d]\n",
			mfc_mem_data_base(0), mfc_mem_data_size(0));

	if (mfc_put_free_buf(mfc_mem_data_base(1),
		mfc_mem_data_size(1), 0) < 0)
		mfc_dbg("failed to add free buffer: [0x%08lx: %d]\n",
			mfc_mem_data_base(1), mfc_mem_data_size(1));

	if (list_empty(&mfc_free_head[0]))
		ret = -1;

#else
	for (port = 0; port < mfc_mem_count(); port++) {
		INIT_LIST_HEAD(&mfc_alloc_head[port]);
		INIT_LIST_HEAD(&mfc_free_head[port]);

		if (mfc_put_free_buf(mfc_mem_data_base(port),
			mfc_mem_data_size(port), port) < 0)
			mfc_err("failed to add free buffer: [0x%08lx: %d]\n",
				mfc_mem_data_base(port),
				mfc_mem_data_size(port));
	}

	for (port = 0; port < mfc_mem_count(); port++) {
		if (list_empty(&mfc_free_head[port]))
			ret = -1;
	}
#endif

	/*
	spin_lock_init(&lock);
	*/

	mfc_print_buf();

	return ret;
}

void mfc_final_buf(void)
{
	struct list_head *pos, *nxt;
	struct mfc_alloc_buffer *alloc;
	struct mfc_free_buffer *free;
	int port;
	/*
	unsigned long flags;
	*/

	/*
	spin_lock_irqsave(&lock, flags);
	*/

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
			if (alloc->ump_handle)
				mfc_ump_unmap(alloc->ump_handle);

			if (alloc->vcm_k)
				mfc_vcm_unmap(alloc->vcm_k);

			if (alloc->vcm_s)
				mfc_vcm_unbind(alloc->vcm_s,
						alloc->type & MBT_OTHER);

			if (mfc_put_free_buf(alloc->vcm_addr,
				alloc->vcm_size, port) < 0) {

				mfc_err("failed to add free buffer\n");
			} else {
				list_del(&alloc->list);
				kfree(alloc);
			}
#elif defined(CONFIG_S5P_VMEM)
			if (alloc->vmem_cookie)
				s5p_vfree(alloc->vmem_cookie);

			if (mfc_put_free_buf(alloc->vmem_addr,
				alloc->vmem_size, port) < 0) {

				mfc_err("failed to add free buffer\n");
			} else {
				list_del(&alloc->list);
				kfree(alloc);
			}
#else
			if (mfc_put_free_buf(alloc->real,
				alloc->size, port) < 0) {

				mfc_err("failed to add free buffer\n");
			} else {
				list_del(&alloc->list);
				kfree(alloc);
			}
#endif
		}
	}

	/*
	spin_unlock_irqrestore(&lock, flags);
	*/

	mfc_print_buf();

	/*
	spin_lock_irqsave(&lock, flags);
	*/

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_free_head[port]) {
			free = list_entry(pos, struct mfc_free_buffer, list);
			list_del(&free->list);
			kfree(free);
		}
	}

	/*
	spin_unlock_irqrestore(&lock, flags);
	*/

	mfc_print_buf();
}

void mfc_set_buf_alloc_scheme(enum MFC_BUF_ALLOC_SCHEME scheme)
{
	buf_alloc_scheme = scheme;
}

void mfc_merge_buf(void)
{
	struct list_head *pos, *nxt;
	struct mfc_free_buffer *n1;
	struct mfc_free_buffer *n2;
	int port;

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_free_head[port]) {
			n1 = list_entry(pos, struct mfc_free_buffer, list);
			n2 = list_entry(nxt, struct mfc_free_buffer, list);

			mfc_dbg("merge pre: n1: 0x%08lx, n2: 0x%08lx",
				n1->real, n2->real);

			if (!list_is_last(pos, &mfc_free_head[port])) {
				if ((n1->real + n1->size) == n2->real) {
					n2->real  = n1->real;
					n2->size += n1->size;
					list_del(&n1->list);
					kfree(n1);
				}
			}

			mfc_dbg("merge aft: n1: 0x%08lx, n2: 0x%08lx, last: %d",
				n1->real, n2->real,
				list_is_last(pos, &mfc_free_head[port]));
		}
	}

#ifdef DEBUG_ALLOC_FREE
	mfc_print_buf();
#endif
}

/* FIXME: port auto select, return values */
struct mfc_alloc_buffer *_mfc_alloc_buf(
	struct mfc_inst_ctx *ctx, unsigned int size, int align, int flag)
{
	unsigned long addr;
	struct mfc_alloc_buffer *alloc;
	int port = flag & 0xFFFF;
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	int align_size = 0;
	struct ump_vcm ump_vcm;
#elif defined(CONFIG_S5P_VMEM)
	int align_size = 0;
#endif
	/*
	unsigned long flags;
	*/

	if (!size)
		return NULL;

	alloc = (struct mfc_alloc_buffer *)
		kzalloc(sizeof(struct mfc_alloc_buffer), GFP_KERNEL);

	if (unlikely(alloc == NULL))
		return NULL;

	/* FIXME: right position? */
	if (port > (mfc_mem_count() - 1))
		port = mfc_mem_count() - 1;

	/*
	spin_lock_irqsave(&lock, flags);
	*/

	addr = mfc_get_free_buf(size, align, port);

	mfc_dbg("mfc_get_free_buf: 0x%08lx\n", addr);

	if (!addr) {
		mfc_dbg("cannot get suitable free buffer\n");
		/* FIXME: is it need?
		mfc_put_free_buf(addr, size, port);
		*/
		kfree(alloc);
		/*
		spin_unlock_irqrestore(&lock, flags);
		*/

		return NULL;
	}

#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	if (align > PAGE_SIZE) {
		align_size = ALIGN(addr, align) - addr;
		align_size += ALIGN(align_size + size, PAGE_SIZE) - size;
	} else {
		align_size = ALIGN(align_size + size, PAGE_SIZE) - size;
	}

	alloc->vcm_s = mfc_vcm_bind(addr, size + align_size);
	if (IS_ERR(alloc->vcm_s)) {
		mfc_put_free_buf(addr, size, port);
		kfree(alloc);

		return NULL;
		/*
		return PTR_ERR(alloc->vcm_s);
		*/
	}

	if (flag & MBT_KERNEL) {
		alloc->vcm_k = mfc_vcm_map(alloc->vcm_s->res.phys);
		if (IS_ERR(alloc->vcm_k)) {
			mfc_vcm_unbind(alloc->vcm_s,
					alloc->type & MBT_OTHER);
			mfc_put_free_buf(addr, size, port);
			kfree(alloc);

			return NULL;
			/*
			return PTR_ERR(alloc->vcm_k);
			*/
		}
	}

	if (flag & MBT_USER) {
		ump_vcm.vcm = alloc->vcm_s->res.vcm;
		ump_vcm.vcm_res = &(alloc->vcm_s->res);
		ump_vcm.dev_id = VCM_DEV_MFC;

		alloc->ump_handle = mfc_ump_map(alloc->vcm_s->res.phys, (unsigned long)&ump_vcm);
		if (IS_ERR(alloc->ump_handle)) {
			mfc_vcm_unmap(alloc->vcm_k);
			mfc_vcm_unbind(alloc->vcm_s,
					alloc->type & MBT_OTHER);
			mfc_put_free_buf(addr, size, port);
			kfree(alloc);

		return NULL;
			/*
			return PTR_ERR(alloc->vcm_k);
			*/
		}
	}

	alloc->vcm_addr = addr;
	alloc->vcm_size = size + align_size;
#elif defined(CONFIG_S5P_VMEM)
	if (align > PAGE_SIZE) {
		align_size = ALIGN(addr, align) - addr;
		align_size += ALIGN(align_size + size, PAGE_SIZE) - size;
	} else {
		align_size = ALIGN(align_size + size, PAGE_SIZE) - size;
	}

	alloc->vmem_cookie = s5p_vmem_vmemmap(size + align_size,
			addr, addr + (size + align_size));

	if (!alloc->vmem_cookie) {
		mfc_dbg("cannot map free buffer to memory\n");
		mfc_put_free_buf(addr, size, port);
		kfree(alloc);

		return NULL;
	}

	alloc->vmem_addr = addr;
	alloc->vmem_size = size + align_size;
#endif
	alloc->real = ALIGN(addr, align);
	alloc->size = size;

#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	if (alloc->vcm_k)
		alloc->addr = (unsigned char *)alloc->vcm_k->start;
	else
		alloc->addr = NULL;
#elif defined(CONFIG_S5P_VMEM)
	alloc->addr = (unsigned char *)(mfc_mem_addr(port) +
		mfc_mem_base_ofs(alloc->real));
#else
	alloc->addr = (unsigned char *)(mfc_mem_addr(port) +
		mfc_mem_base_ofs(alloc->real));
	/*
	alloc->user = (unsigned char *)(ctx->userbase +
		mfc_mem_data_ofs(alloc->real, 1));
	*/
	alloc->ofs = mfc_mem_data_ofs(alloc->real, 1);
#endif
	alloc->type = flag & 0xFFFF0000;
	alloc->owner = ctx->id;

	list_add(&alloc->list, &mfc_alloc_head[port]);

	/*
	spin_unlock_irqrestore(&lock, flags);
	*/

#ifdef DEBUG_ALLOC_FREE
	mfc_print_buf();
#endif

	return alloc;
}

#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
unsigned int mfc_vcm_bind_from_others(struct mfc_inst_ctx *ctx,
				struct mfc_buf_alloc_arg *args, int flag)
{
	int ret;
	unsigned long addr;
	unsigned int size;
	unsigned int secure_id = args->secure_id;
	int port = flag & 0xFFFF;

	struct vcm_res *vcm_res;
	struct vcm_mmu_res *s_res;
	struct mfc_alloc_buffer *alloc;

	ump_dd_handle ump_mem;

	/* FIXME: right position? */
	if (port > (mfc_mem_count() - 1))
		port = mfc_mem_count() - 1;

	ump_mem = ump_dd_handle_create_from_secure_id(secure_id);
	ump_dd_reference_add(ump_mem);

	vcm_res = (struct vcm_res *)
		ump_dd_meminfo_get(secure_id, (void*)VCM_DEV_MFC);
	if (!vcm_res) {
		mfc_dbg("%s: Failed to get vcm_res\n", __func__);
		goto err_ret;
	}

	size = vcm_res->bound_size;

	alloc = (struct mfc_alloc_buffer *)
		kzalloc(sizeof(struct mfc_alloc_buffer), GFP_KERNEL);
	if(!alloc) {
		mfc_dbg("%s: Failed to get mfc_alloc_buffer\n", __func__);
		goto err_ret;
	}

	addr = mfc_get_free_buf(size, ALIGN_2KB, port);
	if (!addr) {
		mfc_dbg("cannot get suitable free buffer\n");
		goto err_ret_alloc;
	}
	mfc_dbg("mfc_get_free_buf: 0x%08lx\n", addr);

	s_res = kzalloc(sizeof(struct vcm_mmu_res), GFP_KERNEL);
	if (!s_res) {
		mfc_dbg("%s: Failed to get vcm_mmu_res\n", __func__);
		goto err_ret_alloc;
	}

	s_res->res.start = addr;
	s_res->res.res_size = size;
	s_res->res.vcm = ctx->dev->vcm_info.sysmmu_vcm;
	INIT_LIST_HEAD(&s_res->bound);

	ret = vcm_bind(&s_res->res, vcm_res->phys);
	if (ret < 0) {
		mfc_dbg("%s: Failed to vcm_bind\n", __func__);
		goto err_ret_s_res;
	}

	alloc->vcm_s = s_res;
	alloc->vcm_addr = addr;
	alloc->ump_handle = ump_mem;
	alloc->vcm_size = size;
	alloc->real = addr;
	alloc->size = size;
	alloc->type = flag & 0xFFFF0000;
	alloc->owner = ctx->id;

	list_add(&alloc->list, &mfc_alloc_head[port]);

	mfc_print_buf();

	return 0;

err_ret_s_res:
	kfree(s_res);
err_ret_alloc:
	kfree(alloc);
err_ret:
	return -1;
}
#endif

int
mfc_alloc_buf(struct mfc_inst_ctx *ctx, struct mfc_buf_alloc_arg *args, int flag)
{
	struct mfc_alloc_buffer *alloc;

	alloc = _mfc_alloc_buf(ctx, args->size, args->align, flag);

	if (unlikely(alloc == NULL))
		return MFC_MEM_ALLOC_FAIL;
	/*
	args->phys = (unsigned int)alloc->real;
	*/
	args->addr = (unsigned int)alloc->addr;
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
	if (alloc->ump_handle)
		args->secure_id = mfc_ump_get_id(alloc->ump_handle);
#elif defined(CONFIG_S5P_VMEM)
	args->cookie = (unsigned int)alloc->vmem_cookie;
#else
	args->offset = alloc->ofs;
#endif
	return MFC_OK;
}

int _mfc_free_buf(unsigned long real)
{
	struct list_head *pos, *nxt;
	struct mfc_alloc_buffer *alloc;
	int port;
	int found = 0;
	/*
	unsigned long flags;
	*/

	mfc_dbg("addr: 0x%08lx\n", real);

	/*
	spin_lock_irqsave(&lock, flags);
	*/

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if (alloc->real == real) {
				found = 1;
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
				if (alloc->ump_handle)
					mfc_ump_unmap(alloc->ump_handle);

				if (alloc->vcm_k)
					mfc_vcm_unmap(alloc->vcm_k);

				if (alloc->vcm_s)
					mfc_vcm_unbind(alloc->vcm_s,
							alloc->type & MBT_OTHER);

				if (mfc_put_free_buf(alloc->vcm_addr,
					alloc->vcm_size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
#elif defined(CONFIG_S5P_VMEM)
				if (alloc->vmem_cookie)
					s5p_vfree(alloc->vmem_cookie);

				if (mfc_put_free_buf(alloc->vmem_addr,
					alloc->vmem_size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
#else
				if (mfc_put_free_buf(alloc->real,
					alloc->size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
#endif
				break;
			}
		}

		if (found)
			break;
	}

	/*
	spin_unlock_irqrestore(&lock, flags);
	*/

#ifdef DEBUG_ALLOC_FREE
	mfc_print_buf();
#endif

	if (found)
		return 0;

	return -1;
}

int mfc_free_buf(struct mfc_inst_ctx *ctx, unsigned int key)
{
	unsigned long real;

	real = mfc_get_buf_real(ctx->id, key);
	if (unlikely(real == 0))
		return MFC_MEM_INVALID_ADDR_FAIL;

	if (_mfc_free_buf(real) < 0)
		return MFC_MEM_INVALID_ADDR_FAIL;

	return MFC_OK;
}

void mfc_free_buf_type(int owner, int type)
{
	int port;
	struct list_head *pos, *nxt;
	struct mfc_alloc_buffer *alloc;

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if ((alloc->owner == owner) && (alloc->type == type)) {
				if (mfc_put_free_buf(alloc->real,
					alloc->size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
			}
		}
	}
}

/* FIXME: add MFC Buffer Type */
void mfc_free_buf_inst(int owner)
{
	struct list_head *pos, *nxt;
	int port;
	struct mfc_alloc_buffer *alloc;
	/*
	unsigned long flags;
	*/

	mfc_dbg("owner: %d\n", owner);

	/*
	spin_lock_irqsave(&lock, flags);
	*/

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if (alloc->owner == owner) {
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
				if (alloc->ump_handle)
					mfc_ump_unmap(alloc->ump_handle);

				if (alloc->vcm_k)
					mfc_vcm_unmap(alloc->vcm_k);

				if (alloc->vcm_s)
					mfc_vcm_unbind(alloc->vcm_s,
							alloc->type & MBT_OTHER);

				if (mfc_put_free_buf(alloc->vcm_addr,
					alloc->vcm_size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
#elif defined(CONFIG_S5P_VMEM)
				if (alloc->vmem_cookie)
					s5p_vfree(alloc->vmem_cookie);

				if (mfc_put_free_buf(alloc->vmem_addr,
					alloc->vmem_size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
#else
				if (mfc_put_free_buf(alloc->real,
					alloc->size, port) < 0) {

					mfc_err("failed to add free buffer\n");
				} else {
					list_del(&alloc->list);
					kfree(alloc);
				}
#endif
			}
		}
	}

	/*
	spin_unlock_irqrestore(&lock, flags);
	*/

#ifdef DEBUG_ALLOC_FREE
	mfc_print_buf();
#endif
}

unsigned long mfc_get_buf_real(int owner, unsigned int key)
{
	struct list_head *pos, *nxt;
	int port;
	struct mfc_alloc_buffer *alloc;

#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
		mfc_dbg("owner: %d, secure id: 0x%08x\n", owner, key);
#elif defined(CONFIG_S5P_VMEM)
		mfc_dbg("owner: %d, cookie: 0x%08x\n", owner, key);
#else
		mfc_dbg("owner: %d, offset: 0x%08x\n", owner, key);
#endif

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if (alloc->owner == owner) {
#if defined(CONFIG_VIDEO_MFC_VCM_UMP)
				if (alloc->ump_handle) {
					if (mfc_ump_get_id(alloc->ump_handle) == key)
						return alloc->real;
				}
#elif defined(CONFIG_S5P_VMEM)
				if (alloc->vmem_cookie == key)
					return alloc->real;
#else
				if (alloc->ofs == key)
					return alloc->real;
#endif
			}
		}
	}

	return 0;
}

#if 0
unsigned char *mfc_get_buf_addr(int owner, unsigned char *user)
{
	struct list_head *pos, *nxt;
	int port;
	struct mfc_alloc_buffer *alloc;

	mfc_dbg("owner: %d, user: 0x%08x\n", owner, (unsigned int)user);

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if ((alloc->owner == owner)
				&& (alloc->user == user)){

				return alloc->addr;
			}
		}
	}

	return NULL;
}

unsigned char *_mfc_get_buf_addr(int owner, unsigned char *user)
{
	struct list_head *pos, *nxt;
	int port;
	struct mfc_alloc_buffer *alloc;

	mfc_dbg("owner: %d, user: 0x%08x\n", owner, (unsigned int)user);

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if ((alloc->owner == owner)
				&& ((alloc->user <= user) || ((alloc->user + alloc->size) > user))){

				return alloc->addr;
			}
		}
	}

	return NULL;
}
#endif

#ifdef CONFIG_VIDEO_MFC_VCM_UMP
void *mfc_get_buf_ump_handle(unsigned long real)
{
	struct list_head *pos, *nxt;
	int port;
	struct mfc_alloc_buffer *alloc;

	mfc_dbg("real: 0x%08lx\n", real);

	for (port = 0; port < mfc_mem_count(); port++) {
		list_for_each_safe(pos, nxt, &mfc_alloc_head[port]) {
			alloc = list_entry(pos, struct mfc_alloc_buffer, list);

			if (alloc->real == real)
				return alloc->ump_handle;
		}
	}

	return NULL;
}
#endif

