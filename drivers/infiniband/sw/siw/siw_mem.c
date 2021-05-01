// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#include <linux/gfp.h>
#include <rdma/ib_verbs.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/resource.h>

#include "siw.h"
#include "siw_mem.h"

/*
 * Stag lookup is based on its index part only (24 bits).
 * The code avoids special Stag of zero and tries to randomize
 * STag values between 1 and SIW_STAG_MAX_INDEX.
 */
int siw_mem_add(struct siw_device *sdev, struct siw_mem *m)
{
	struct xa_limit limit = XA_LIMIT(1, 0x00ffffff);
	u32 id, next;

	get_random_bytes(&next, 4);
	next &= 0x00ffffff;

	if (xa_alloc_cyclic(&sdev->mem_xa, &id, m, limit, &next,
	    GFP_KERNEL) < 0)
		return -ENOMEM;

	/* Set the STag index part */
	m->stag = id << 8;

	siw_dbg_mem(m, "new MEM object\n");

	return 0;
}

/*
 * siw_mem_id2obj()
 *
 * resolves memory from stag given by id. might be called from:
 * o process context before sending out of sgl, or
 * o in softirq when resolving target memory
 */
struct siw_mem *siw_mem_id2obj(struct siw_device *sdev, int stag_index)
{
	struct siw_mem *mem;

	rcu_read_lock();
	mem = xa_load(&sdev->mem_xa, stag_index);
	if (likely(mem && kref_get_unless_zero(&mem->ref))) {
		rcu_read_unlock();
		return mem;
	}
	rcu_read_unlock();

	return NULL;
}

static void siw_free_plist(struct siw_page_chunk *chunk, int num_pages,
			   bool dirty)
{
	unpin_user_pages_dirty_lock(chunk->plist, num_pages, dirty);
}

void siw_umem_release(struct siw_umem *umem, bool dirty)
{
	struct mm_struct *mm_s = umem->owning_mm;
	int i, num_pages = umem->num_pages;

	for (i = 0; num_pages; i++) {
		int to_free = min_t(int, PAGES_PER_CHUNK, num_pages);

		siw_free_plist(&umem->page_chunk[i], to_free,
			       umem->writable && dirty);
		kfree(umem->page_chunk[i].plist);
		num_pages -= to_free;
	}
	atomic64_sub(umem->num_pages, &mm_s->pinned_vm);

	mmdrop(mm_s);
	kfree(umem->page_chunk);
	kfree(umem);
}

int siw_mr_add_mem(struct siw_mr *mr, struct ib_pd *pd, void *mem_obj,
		   u64 start, u64 len, int rights)
{
	struct siw_device *sdev = to_siw_dev(pd->device);
	struct siw_mem *mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	struct xa_limit limit = XA_LIMIT(1, 0x00ffffff);
	u32 id, next;

	if (!mem)
		return -ENOMEM;

	mem->mem_obj = mem_obj;
	mem->stag_valid = 0;
	mem->sdev = sdev;
	mem->va = start;
	mem->len = len;
	mem->pd = pd;
	mem->perms = rights & IWARP_ACCESS_MASK;
	kref_init(&mem->ref);

	get_random_bytes(&next, 4);
	next &= 0x00ffffff;

	if (xa_alloc_cyclic(&sdev->mem_xa, &id, mem, limit, &next,
	    GFP_KERNEL) < 0) {
		kfree(mem);
		return -ENOMEM;
	}

	mr->mem = mem;
	/* Set the STag index part */
	mem->stag = id << 8;
	mr->base_mr.lkey = mr->base_mr.rkey = mem->stag;

	return 0;
}

void siw_mr_drop_mem(struct siw_mr *mr)
{
	struct siw_mem *mem = mr->mem, *found;

	mem->stag_valid = 0;

	/* make STag invalid visible asap */
	smp_mb();

	found = xa_erase(&mem->sdev->mem_xa, mem->stag >> 8);
	WARN_ON(found != mem);
	siw_mem_put(mem);
}

void siw_free_mem(struct kref *ref)
{
	struct siw_mem *mem = container_of(ref, struct siw_mem, ref);

	siw_dbg_mem(mem, "free mem, pbl: %s\n", mem->is_pbl ? "y" : "n");

	if (!mem->is_mw && mem->mem_obj) {
		if (mem->is_pbl == 0)
			siw_umem_release(mem->umem, true);
		else
			kfree(mem->pbl);
	}
	kfree(mem);
}

/*
 * siw_check_mem()
 *
 * Check protection domain, STAG state, access permissions and
 * address range for memory object.
 *
 * @pd:		Protection Domain memory should belong to
 * @mem:	memory to be checked
 * @addr:	starting addr of mem
 * @perms:	requested access permissions
 * @len:	len of memory interval to be checked
 *
 */
int siw_check_mem(struct ib_pd *pd, struct siw_mem *mem, u64 addr,
		  enum ib_access_flags perms, int len)
{
	if (!mem->stag_valid) {
		siw_dbg_pd(pd, "STag 0x%08x invalid\n", mem->stag);
		return -E_STAG_INVALID;
	}
	if (mem->pd != pd) {
		siw_dbg_pd(pd, "STag 0x%08x: PD mismatch\n", mem->stag);
		return -E_PD_MISMATCH;
	}
	/*
	 * check access permissions
	 */
	if ((mem->perms & perms) < perms) {
		siw_dbg_pd(pd, "permissions 0x%08x < 0x%08x\n",
			   mem->perms, perms);
		return -E_ACCESS_PERM;
	}
	/*
	 * Check if access falls into valid memory interval.
	 */
	if (addr < mem->va || addr + len > mem->va + mem->len) {
		siw_dbg_pd(pd, "MEM interval len %d\n", len);
		siw_dbg_pd(pd, "[0x%pK, 0x%pK] out of bounds\n",
			   (void *)(uintptr_t)addr,
			   (void *)(uintptr_t)(addr + len));
		siw_dbg_pd(pd, "[0x%pK, 0x%pK] STag=0x%08x\n",
			   (void *)(uintptr_t)mem->va,
			   (void *)(uintptr_t)(mem->va + mem->len),
			   mem->stag);

		return -E_BASE_BOUNDS;
	}
	return E_ACCESS_OK;
}

/*
 * siw_check_sge()
 *
 * Check SGE for access rights in given interval
 *
 * @pd:		Protection Domain memory should belong to
 * @sge:	SGE to be checked
 * @mem:	location of memory reference within array
 * @perms:	requested access permissions
 * @off:	starting offset in SGE
 * @len:	len of memory interval to be checked
 *
 * NOTE: Function references SGE's memory object (mem->obj)
 * if not yet done. New reference is kept if check went ok and
 * released if check failed. If mem->obj is already valid, no new
 * lookup is being done and mem is not released it check fails.
 */
int siw_check_sge(struct ib_pd *pd, struct siw_sge *sge, struct siw_mem *mem[],
		  enum ib_access_flags perms, u32 off, int len)
{
	struct siw_device *sdev = to_siw_dev(pd->device);
	struct siw_mem *new = NULL;
	int rv = E_ACCESS_OK;

	if (len + off > sge->length) {
		rv = -E_BASE_BOUNDS;
		goto fail;
	}
	if (*mem == NULL) {
		new = siw_mem_id2obj(sdev, sge->lkey >> 8);
		if (unlikely(!new)) {
			siw_dbg_pd(pd, "STag unknown: 0x%08x\n", sge->lkey);
			rv = -E_STAG_INVALID;
			goto fail;
		}
		*mem = new;
	}
	/* Check if user re-registered with different STag key */
	if (unlikely((*mem)->stag != sge->lkey)) {
		siw_dbg_mem((*mem), "STag mismatch: 0x%08x\n", sge->lkey);
		rv = -E_STAG_INVALID;
		goto fail;
	}
	rv = siw_check_mem(pd, *mem, sge->laddr + off, perms, len);
	if (unlikely(rv))
		goto fail;

	return 0;

fail:
	if (new) {
		*mem = NULL;
		siw_mem_put(new);
	}
	return rv;
}

void siw_wqe_put_mem(struct siw_wqe *wqe, enum siw_opcode op)
{
	switch (op) {
	case SIW_OP_SEND:
	case SIW_OP_WRITE:
	case SIW_OP_SEND_WITH_IMM:
	case SIW_OP_SEND_REMOTE_INV:
	case SIW_OP_READ:
	case SIW_OP_READ_LOCAL_INV:
		if (!(wqe->sqe.flags & SIW_WQE_INLINE))
			siw_unref_mem_sgl(wqe->mem, wqe->sqe.num_sge);
		break;

	case SIW_OP_RECEIVE:
		siw_unref_mem_sgl(wqe->mem, wqe->rqe.num_sge);
		break;

	case SIW_OP_READ_RESPONSE:
		siw_unref_mem_sgl(wqe->mem, 1);
		break;

	default:
		/*
		 * SIW_OP_INVAL_STAG and SIW_OP_REG_MR
		 * do not hold memory references
		 */
		break;
	}
}

int siw_invalidate_stag(struct ib_pd *pd, u32 stag)
{
	struct siw_device *sdev = to_siw_dev(pd->device);
	struct siw_mem *mem = siw_mem_id2obj(sdev, stag >> 8);
	int rv = 0;

	if (unlikely(!mem)) {
		siw_dbg_pd(pd, "STag 0x%08x unknown\n", stag);
		return -EINVAL;
	}
	if (unlikely(mem->pd != pd)) {
		siw_dbg_pd(pd, "PD mismatch for STag 0x%08x\n", stag);
		rv = -EACCES;
		goto out;
	}
	/*
	 * Per RDMA verbs definition, an STag may already be in invalid
	 * state if invalidation is requested. So no state check here.
	 */
	mem->stag_valid = 0;

	siw_dbg_pd(pd, "STag 0x%08x now invalid\n", stag);
out:
	siw_mem_put(mem);
	return rv;
}

/*
 * Gets physical address backed by PBL element. Address is referenced
 * by linear byte offset into list of variably sized PB elements.
 * Optionally, provides remaining len within current element, and
 * current PBL index for later resume at same element.
 */
dma_addr_t siw_pbl_get_buffer(struct siw_pbl *pbl, u64 off, int *len, int *idx)
{
	int i = idx ? *idx : 0;

	while (i < pbl->num_buf) {
		struct siw_pble *pble = &pbl->pbe[i];

		if (pble->pbl_off + pble->size > off) {
			u64 pble_off = off - pble->pbl_off;

			if (len)
				*len = pble->size - pble_off;
			if (idx)
				*idx = i;

			return pble->addr + pble_off;
		}
		i++;
	}
	if (len)
		*len = 0;
	return 0;
}

struct siw_pbl *siw_pbl_alloc(u32 num_buf)
{
	struct siw_pbl *pbl;

	if (num_buf == 0)
		return ERR_PTR(-EINVAL);

	pbl = kzalloc(struct_size(pbl, pbe, num_buf), GFP_KERNEL);
	if (!pbl)
		return ERR_PTR(-ENOMEM);

	pbl->max_buf = num_buf;

	return pbl;
}

struct siw_umem *siw_umem_get(u64 start, u64 len, bool writable)
{
	struct siw_umem *umem;
	struct mm_struct *mm_s;
	u64 first_page_va;
	unsigned long mlock_limit;
	unsigned int foll_flags = FOLL_WRITE;
	int num_pages, num_chunks, i, rv = 0;

	if (!can_do_mlock())
		return ERR_PTR(-EPERM);

	if (!len)
		return ERR_PTR(-EINVAL);

	first_page_va = start & PAGE_MASK;
	num_pages = PAGE_ALIGN(start + len - first_page_va) >> PAGE_SHIFT;
	num_chunks = (num_pages >> CHUNK_SHIFT) + 1;

	umem = kzalloc(sizeof(*umem), GFP_KERNEL);
	if (!umem)
		return ERR_PTR(-ENOMEM);

	mm_s = current->mm;
	umem->owning_mm = mm_s;
	umem->writable = writable;

	mmgrab(mm_s);

	if (!writable)
		foll_flags |= FOLL_FORCE;

	mmap_read_lock(mm_s);

	mlock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	if (num_pages + atomic64_read(&mm_s->pinned_vm) > mlock_limit) {
		rv = -ENOMEM;
		goto out_sem_up;
	}
	umem->fp_addr = first_page_va;

	umem->page_chunk =
		kcalloc(num_chunks, sizeof(struct siw_page_chunk), GFP_KERNEL);
	if (!umem->page_chunk) {
		rv = -ENOMEM;
		goto out_sem_up;
	}
	for (i = 0; num_pages; i++) {
		int got, nents = min_t(int, num_pages, PAGES_PER_CHUNK);

		umem->page_chunk[i].plist =
			kcalloc(nents, sizeof(struct page *), GFP_KERNEL);
		if (!umem->page_chunk[i].plist) {
			rv = -ENOMEM;
			goto out_sem_up;
		}
		got = 0;
		while (nents) {
			struct page **plist = &umem->page_chunk[i].plist[got];

			rv = pin_user_pages(first_page_va, nents,
					    foll_flags | FOLL_LONGTERM,
					    plist, NULL);
			if (rv < 0)
				goto out_sem_up;

			umem->num_pages += rv;
			atomic64_add(rv, &mm_s->pinned_vm);
			first_page_va += rv * PAGE_SIZE;
			nents -= rv;
			got += rv;
		}
		num_pages -= got;
	}
out_sem_up:
	mmap_read_unlock(mm_s);

	if (rv > 0)
		return umem;

	siw_umem_release(umem, false);

	return ERR_PTR(rv);
}
