/*
 * Copyright 2017 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#define NVKM_VMM_LEVELS_MAX 5
#include "vmm.h"

#include <subdev/fb.h>

static void
nvkm_vmm_pt_del(struct nvkm_vmm_pt **ppgt)
{
	struct nvkm_vmm_pt *pgt = *ppgt;
	if (pgt) {
		kvfree(pgt->pde);
		kfree(pgt);
		*ppgt = NULL;
	}
}


static struct nvkm_vmm_pt *
nvkm_vmm_pt_new(const struct nvkm_vmm_desc *desc, bool sparse,
		const struct nvkm_vmm_page *page)
{
	const u32 pten = 1 << desc->bits;
	struct nvkm_vmm_pt *pgt;
	u32 lpte = 0;

	if (desc->type > PGT) {
		if (desc->type == SPT) {
			const struct nvkm_vmm_desc *pair = page[-1].desc;
			lpte = pten >> (desc->bits - pair->bits);
		} else {
			lpte = pten;
		}
	}

	if (!(pgt = kzalloc(sizeof(*pgt) + lpte, GFP_KERNEL)))
		return NULL;
	pgt->page = page ? page->shift : 0;
	pgt->sparse = sparse;

	if (desc->type == PGD) {
		pgt->pde = kvcalloc(pten, sizeof(*pgt->pde), GFP_KERNEL);
		if (!pgt->pde) {
			kfree(pgt);
			return NULL;
		}
	}

	return pgt;
}

struct nvkm_vmm_iter {
	const struct nvkm_vmm_page *page;
	const struct nvkm_vmm_desc *desc;
	struct nvkm_vmm *vmm;
	u64 cnt;
	u16 max, lvl;
	u32 pte[NVKM_VMM_LEVELS_MAX];
	struct nvkm_vmm_pt *pt[NVKM_VMM_LEVELS_MAX];
	int flush;
};

#ifdef CONFIG_NOUVEAU_DEBUG_MMU
static const char *
nvkm_vmm_desc_type(const struct nvkm_vmm_desc *desc)
{
	switch (desc->type) {
	case PGD: return "PGD";
	case PGT: return "PGT";
	case SPT: return "SPT";
	case LPT: return "LPT";
	default:
		return "UNKNOWN";
	}
}

static void
nvkm_vmm_trace(struct nvkm_vmm_iter *it, char *buf)
{
	int lvl;
	for (lvl = it->max; lvl >= 0; lvl--) {
		if (lvl >= it->lvl)
			buf += sprintf(buf,  "%05x:", it->pte[lvl]);
		else
			buf += sprintf(buf, "xxxxx:");
	}
}

#define TRA(i,f,a...) do {                                                     \
	char _buf[NVKM_VMM_LEVELS_MAX * 7];                                    \
	struct nvkm_vmm_iter *_it = (i);                                       \
	nvkm_vmm_trace(_it, _buf);                                             \
	VMM_TRACE(_it->vmm, "%s "f, _buf, ##a);                                \
} while(0)
#else
#define TRA(i,f,a...)
#endif

static inline void
nvkm_vmm_flush_mark(struct nvkm_vmm_iter *it)
{
	it->flush = min(it->flush, it->max - it->lvl);
}

static inline void
nvkm_vmm_flush(struct nvkm_vmm_iter *it)
{
	if (it->flush != NVKM_VMM_LEVELS_MAX) {
		if (it->vmm->func->flush) {
			TRA(it, "flush: %d", it->flush);
			it->vmm->func->flush(it->vmm, it->flush);
		}
		it->flush = NVKM_VMM_LEVELS_MAX;
	}
}

static void
nvkm_vmm_unref_pdes(struct nvkm_vmm_iter *it)
{
	const struct nvkm_vmm_desc *desc = it->desc;
	const int type = desc[it->lvl].type == SPT;
	struct nvkm_vmm_pt *pgd = it->pt[it->lvl + 1];
	struct nvkm_vmm_pt *pgt = it->pt[it->lvl];
	struct nvkm_mmu_pt *pt = pgt->pt[type];
	struct nvkm_vmm *vmm = it->vmm;
	u32 pdei = it->pte[it->lvl + 1];

	/* Recurse up the tree, unreferencing/destroying unneeded PDs. */
	it->lvl++;
	if (--pgd->refs[0]) {
		const struct nvkm_vmm_desc_func *func = desc[it->lvl].func;
		/* PD has other valid PDEs, so we need a proper update. */
		TRA(it, "PDE unmap %s", nvkm_vmm_desc_type(&desc[it->lvl - 1]));
		pgt->pt[type] = NULL;
		if (!pgt->refs[!type]) {
			/* PDE no longer required. */
			if (pgd->pt[0]) {
				if (pgt->sparse) {
					func->sparse(vmm, pgd->pt[0], pdei, 1);
					pgd->pde[pdei] = NVKM_VMM_PDE_SPARSE;
				} else {
					func->unmap(vmm, pgd->pt[0], pdei, 1);
					pgd->pde[pdei] = NULL;
				}
			} else {
				/* Special handling for Tesla-class GPUs,
				 * where there's no central PD, but each
				 * instance has its own embedded PD.
				 */
				func->pde(vmm, pgd, pdei);
				pgd->pde[pdei] = NULL;
			}
		} else {
			/* PDE was pointing at dual-PTs and we're removing
			 * one of them, leaving the other in place.
			 */
			func->pde(vmm, pgd, pdei);
		}

		/* GPU may have cached the PTs, flush before freeing. */
		nvkm_vmm_flush_mark(it);
		nvkm_vmm_flush(it);
	} else {
		/* PD has no valid PDEs left, so we can just destroy it. */
		nvkm_vmm_unref_pdes(it);
	}

	/* Destroy PD/PT. */
	TRA(it, "PDE free %s", nvkm_vmm_desc_type(&desc[it->lvl - 1]));
	nvkm_mmu_ptc_put(vmm->mmu, vmm->bootstrapped, &pt);
	if (!pgt->refs[!type])
		nvkm_vmm_pt_del(&pgt);
	it->lvl--;
}

static void
nvkm_vmm_unref_sptes(struct nvkm_vmm_iter *it, struct nvkm_vmm_pt *pgt,
		     const struct nvkm_vmm_desc *desc, u32 ptei, u32 ptes)
{
	const struct nvkm_vmm_desc *pair = it->page[-1].desc;
	const u32 sptb = desc->bits - pair->bits;
	const u32 sptn = 1 << sptb;
	struct nvkm_vmm *vmm = it->vmm;
	u32 spti = ptei & (sptn - 1), lpti, pteb;

	/* Determine how many SPTEs are being touched under each LPTE,
	 * and drop reference counts.
	 */
	for (lpti = ptei >> sptb; ptes; spti = 0, lpti++) {
		const u32 pten = min(sptn - spti, ptes);
		pgt->pte[lpti] -= pten;
		ptes -= pten;
	}

	/* We're done here if there's no corresponding LPT. */
	if (!pgt->refs[0])
		return;

	for (ptei = pteb = ptei >> sptb; ptei < lpti; pteb = ptei) {
		/* Skip over any LPTEs that still have valid SPTEs. */
		if (pgt->pte[pteb] & NVKM_VMM_PTE_SPTES) {
			for (ptes = 1, ptei++; ptei < lpti; ptes++, ptei++) {
				if (!(pgt->pte[ptei] & NVKM_VMM_PTE_SPTES))
					break;
			}
			continue;
		}

		/* As there's no more non-UNMAPPED SPTEs left in the range
		 * covered by a number of LPTEs, the LPTEs once again take
		 * control over their address range.
		 *
		 * Determine how many LPTEs need to transition state.
		 */
		pgt->pte[ptei] &= ~NVKM_VMM_PTE_VALID;
		for (ptes = 1, ptei++; ptei < lpti; ptes++, ptei++) {
			if (pgt->pte[ptei] & NVKM_VMM_PTE_SPTES)
				break;
			pgt->pte[ptei] &= ~NVKM_VMM_PTE_VALID;
		}

		if (pgt->pte[pteb] & NVKM_VMM_PTE_SPARSE) {
			TRA(it, "LPTE %05x: U -> S %d PTEs", pteb, ptes);
			pair->func->sparse(vmm, pgt->pt[0], pteb, ptes);
		} else
		if (pair->func->invalid) {
			/* If the MMU supports it, restore the LPTE to the
			 * INVALID state to tell the MMU there is no point
			 * trying to fetch the corresponding SPTEs.
			 */
			TRA(it, "LPTE %05x: U -> I %d PTEs", pteb, ptes);
			pair->func->invalid(vmm, pgt->pt[0], pteb, ptes);
		}
	}
}

static bool
nvkm_vmm_unref_ptes(struct nvkm_vmm_iter *it, bool pfn, u32 ptei, u32 ptes)
{
	const struct nvkm_vmm_desc *desc = it->desc;
	const int type = desc->type == SPT;
	struct nvkm_vmm_pt *pgt = it->pt[0];
	bool dma;

	if (pfn) {
		/* Need to clear PTE valid bits before we dma_unmap_page(). */
		dma = desc->func->pfn_clear(it->vmm, pgt->pt[type], ptei, ptes);
		if (dma) {
			/* GPU may have cached the PT, flush before unmap. */
			nvkm_vmm_flush_mark(it);
			nvkm_vmm_flush(it);
			desc->func->pfn_unmap(it->vmm, pgt->pt[type], ptei, ptes);
		}
	}

	/* Drop PTE references. */
	pgt->refs[type] -= ptes;

	/* Dual-PTs need special handling, unless PDE becoming invalid. */
	if (desc->type == SPT && (pgt->refs[0] || pgt->refs[1]))
		nvkm_vmm_unref_sptes(it, pgt, desc, ptei, ptes);

	/* PT no longer neeed?  Destroy it. */
	if (!pgt->refs[type]) {
		it->lvl++;
		TRA(it, "%s empty", nvkm_vmm_desc_type(desc));
		it->lvl--;
		nvkm_vmm_unref_pdes(it);
		return false; /* PTE writes for unmap() not necessary. */
	}

	return true;
}

static void
nvkm_vmm_ref_sptes(struct nvkm_vmm_iter *it, struct nvkm_vmm_pt *pgt,
		   const struct nvkm_vmm_desc *desc, u32 ptei, u32 ptes)
{
	const struct nvkm_vmm_desc *pair = it->page[-1].desc;
	const u32 sptb = desc->bits - pair->bits;
	const u32 sptn = 1 << sptb;
	struct nvkm_vmm *vmm = it->vmm;
	u32 spti = ptei & (sptn - 1), lpti, pteb;

	/* Determine how many SPTEs are being touched under each LPTE,
	 * and increase reference counts.
	 */
	for (lpti = ptei >> sptb; ptes; spti = 0, lpti++) {
		const u32 pten = min(sptn - spti, ptes);
		pgt->pte[lpti] += pten;
		ptes -= pten;
	}

	/* We're done here if there's no corresponding LPT. */
	if (!pgt->refs[0])
		return;

	for (ptei = pteb = ptei >> sptb; ptei < lpti; pteb = ptei) {
		/* Skip over any LPTEs that already have valid SPTEs. */
		if (pgt->pte[pteb] & NVKM_VMM_PTE_VALID) {
			for (ptes = 1, ptei++; ptei < lpti; ptes++, ptei++) {
				if (!(pgt->pte[ptei] & NVKM_VMM_PTE_VALID))
					break;
			}
			continue;
		}

		/* As there are now non-UNMAPPED SPTEs in the range covered
		 * by a number of LPTEs, we need to transfer control of the
		 * address range to the SPTEs.
		 *
		 * Determine how many LPTEs need to transition state.
		 */
		pgt->pte[ptei] |= NVKM_VMM_PTE_VALID;
		for (ptes = 1, ptei++; ptei < lpti; ptes++, ptei++) {
			if (pgt->pte[ptei] & NVKM_VMM_PTE_VALID)
				break;
			pgt->pte[ptei] |= NVKM_VMM_PTE_VALID;
		}

		if (pgt->pte[pteb] & NVKM_VMM_PTE_SPARSE) {
			const u32 spti = pteb * sptn;
			const u32 sptc = ptes * sptn;
			/* The entire LPTE is marked as sparse, we need
			 * to make sure that the SPTEs are too.
			 */
			TRA(it, "SPTE %05x: U -> S %d PTEs", spti, sptc);
			desc->func->sparse(vmm, pgt->pt[1], spti, sptc);
			/* Sparse LPTEs prevent SPTEs from being accessed. */
			TRA(it, "LPTE %05x: S -> U %d PTEs", pteb, ptes);
			pair->func->unmap(vmm, pgt->pt[0], pteb, ptes);
		} else
		if (pair->func->invalid) {
			/* MMU supports blocking SPTEs by marking an LPTE
			 * as INVALID.  We need to reverse that here.
			 */
			TRA(it, "LPTE %05x: I -> U %d PTEs", pteb, ptes);
			pair->func->unmap(vmm, pgt->pt[0], pteb, ptes);
		}
	}
}

static bool
nvkm_vmm_ref_ptes(struct nvkm_vmm_iter *it, bool pfn, u32 ptei, u32 ptes)
{
	const struct nvkm_vmm_desc *desc = it->desc;
	const int type = desc->type == SPT;
	struct nvkm_vmm_pt *pgt = it->pt[0];

	/* Take PTE references. */
	pgt->refs[type] += ptes;

	/* Dual-PTs need special handling. */
	if (desc->type == SPT)
		nvkm_vmm_ref_sptes(it, pgt, desc, ptei, ptes);

	return true;
}

static void
nvkm_vmm_sparse_ptes(const struct nvkm_vmm_desc *desc,
		     struct nvkm_vmm_pt *pgt, u32 ptei, u32 ptes)
{
	if (desc->type == PGD) {
		while (ptes--)
			pgt->pde[ptei++] = NVKM_VMM_PDE_SPARSE;
	} else
	if (desc->type == LPT) {
		memset(&pgt->pte[ptei], NVKM_VMM_PTE_SPARSE, ptes);
	}
}

static bool
nvkm_vmm_sparse_unref_ptes(struct nvkm_vmm_iter *it, bool pfn, u32 ptei, u32 ptes)
{
	struct nvkm_vmm_pt *pt = it->pt[0];
	if (it->desc->type == PGD)
		memset(&pt->pde[ptei], 0x00, sizeof(pt->pde[0]) * ptes);
	else
	if (it->desc->type == LPT)
		memset(&pt->pte[ptei], 0x00, sizeof(pt->pte[0]) * ptes);
	return nvkm_vmm_unref_ptes(it, pfn, ptei, ptes);
}

static bool
nvkm_vmm_sparse_ref_ptes(struct nvkm_vmm_iter *it, bool pfn, u32 ptei, u32 ptes)
{
	nvkm_vmm_sparse_ptes(it->desc, it->pt[0], ptei, ptes);
	return nvkm_vmm_ref_ptes(it, pfn, ptei, ptes);
}

static bool
nvkm_vmm_ref_hwpt(struct nvkm_vmm_iter *it, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	const struct nvkm_vmm_desc *desc = &it->desc[it->lvl - 1];
	const int type = desc->type == SPT;
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];
	const bool zero = !pgt->sparse && !desc->func->invalid;
	struct nvkm_vmm *vmm = it->vmm;
	struct nvkm_mmu *mmu = vmm->mmu;
	struct nvkm_mmu_pt *pt;
	u32 pten = 1 << desc->bits;
	u32 pteb, ptei, ptes;
	u32 size = desc->size * pten;

	pgd->refs[0]++;

	pgt->pt[type] = nvkm_mmu_ptc_get(mmu, size, desc->align, zero);
	if (!pgt->pt[type]) {
		it->lvl--;
		nvkm_vmm_unref_pdes(it);
		return false;
	}

	if (zero)
		goto done;

	pt = pgt->pt[type];

	if (desc->type == LPT && pgt->refs[1]) {
		/* SPT already exists covering the same range as this LPT,
		 * which means we need to be careful that any LPTEs which
		 * overlap valid SPTEs are unmapped as opposed to invalid
		 * or sparse, which would prevent the MMU from looking at
		 * the SPTEs on some GPUs.
		 */
		for (ptei = pteb = 0; ptei < pten; pteb = ptei) {
			bool spte = pgt->pte[ptei] & NVKM_VMM_PTE_SPTES;
			for (ptes = 1, ptei++; ptei < pten; ptes++, ptei++) {
				bool next = pgt->pte[ptei] & NVKM_VMM_PTE_SPTES;
				if (spte != next)
					break;
			}

			if (!spte) {
				if (pgt->sparse)
					desc->func->sparse(vmm, pt, pteb, ptes);
				else
					desc->func->invalid(vmm, pt, pteb, ptes);
				memset(&pgt->pte[pteb], 0x00, ptes);
			} else {
				desc->func->unmap(vmm, pt, pteb, ptes);
				while (ptes--)
					pgt->pte[pteb++] |= NVKM_VMM_PTE_VALID;
			}
		}
	} else {
		if (pgt->sparse) {
			nvkm_vmm_sparse_ptes(desc, pgt, 0, pten);
			desc->func->sparse(vmm, pt, 0, pten);
		} else {
			desc->func->invalid(vmm, pt, 0, pten);
		}
	}

done:
	TRA(it, "PDE write %s", nvkm_vmm_desc_type(desc));
	it->desc[it->lvl].func->pde(it->vmm, pgd, pdei);
	nvkm_vmm_flush_mark(it);
	return true;
}

static bool
nvkm_vmm_ref_swpt(struct nvkm_vmm_iter *it, struct nvkm_vmm_pt *pgd, u32 pdei)
{
	const struct nvkm_vmm_desc *desc = &it->desc[it->lvl - 1];
	struct nvkm_vmm_pt *pgt = pgd->pde[pdei];

	pgt = nvkm_vmm_pt_new(desc, NVKM_VMM_PDE_SPARSED(pgt), it->page);
	if (!pgt) {
		if (!pgd->refs[0])
			nvkm_vmm_unref_pdes(it);
		return false;
	}

	pgd->pde[pdei] = pgt;
	return true;
}

static inline u64
nvkm_vmm_iter(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
	      u64 addr, u64 size, const char *name, bool ref, bool pfn,
	      bool (*REF_PTES)(struct nvkm_vmm_iter *, bool pfn, u32, u32),
	      nvkm_vmm_pte_func MAP_PTES, struct nvkm_vmm_map *map,
	      nvkm_vmm_pxe_func CLR_PTES)
{
	const struct nvkm_vmm_desc *desc = page->desc;
	struct nvkm_vmm_iter it;
	u64 bits = addr >> page->shift;

	it.page = page;
	it.desc = desc;
	it.vmm = vmm;
	it.cnt = size >> page->shift;
	it.flush = NVKM_VMM_LEVELS_MAX;

	/* Deconstruct address into PTE indices for each mapping level. */
	for (it.lvl = 0; desc[it.lvl].bits; it.lvl++) {
		it.pte[it.lvl] = bits & ((1 << desc[it.lvl].bits) - 1);
		bits >>= desc[it.lvl].bits;
	}
	it.max = --it.lvl;
	it.pt[it.max] = vmm->pd;

	it.lvl = 0;
	TRA(&it, "%s: %016llx %016llx %d %lld PTEs", name,
	         addr, size, page->shift, it.cnt);
	it.lvl = it.max;

	/* Depth-first traversal of page tables. */
	while (it.cnt) {
		struct nvkm_vmm_pt *pgt = it.pt[it.lvl];
		const int type = desc->type == SPT;
		const u32 pten = 1 << desc->bits;
		const u32 ptei = it.pte[0];
		const u32 ptes = min_t(u64, it.cnt, pten - ptei);

		/* Walk down the tree, finding page tables for each level. */
		for (; it.lvl; it.lvl--) {
			const u32 pdei = it.pte[it.lvl];
			struct nvkm_vmm_pt *pgd = pgt;

			/* Software PT. */
			if (ref && NVKM_VMM_PDE_INVALID(pgd->pde[pdei])) {
				if (!nvkm_vmm_ref_swpt(&it, pgd, pdei))
					goto fail;
			}
			it.pt[it.lvl - 1] = pgt = pgd->pde[pdei];

			/* Hardware PT.
			 *
			 * This is a separate step from above due to GF100 and
			 * newer having dual page tables at some levels, which
			 * are refcounted independently.
			 */
			if (ref && !pgt->refs[desc[it.lvl - 1].type == SPT]) {
				if (!nvkm_vmm_ref_hwpt(&it, pgd, pdei))
					goto fail;
			}
		}

		/* Handle PTE updates. */
		if (!REF_PTES || REF_PTES(&it, pfn, ptei, ptes)) {
			struct nvkm_mmu_pt *pt = pgt->pt[type];
			if (MAP_PTES || CLR_PTES) {
				if (MAP_PTES)
					MAP_PTES(vmm, pt, ptei, ptes, map);
				else
					CLR_PTES(vmm, pt, ptei, ptes);
				nvkm_vmm_flush_mark(&it);
			}
		}

		/* Walk back up the tree to the next position. */
		it.pte[it.lvl] += ptes;
		it.cnt -= ptes;
		if (it.cnt) {
			while (it.pte[it.lvl] == (1 << desc[it.lvl].bits)) {
				it.pte[it.lvl++] = 0;
				it.pte[it.lvl]++;
			}
		}
	}

	nvkm_vmm_flush(&it);
	return ~0ULL;

fail:
	/* Reconstruct the failure address so the caller is able to
	 * reverse any partially completed operations.
	 */
	addr = it.pte[it.max--];
	do {
		addr  = addr << desc[it.max].bits;
		addr |= it.pte[it.max];
	} while (it.max--);

	return addr << page->shift;
}

static void
nvkm_vmm_ptes_sparse_put(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
			 u64 addr, u64 size)
{
	nvkm_vmm_iter(vmm, page, addr, size, "sparse unref", false, false,
		      nvkm_vmm_sparse_unref_ptes, NULL, NULL,
		      page->desc->func->invalid ?
		      page->desc->func->invalid : page->desc->func->unmap);
}

static int
nvkm_vmm_ptes_sparse_get(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
			 u64 addr, u64 size)
{
	if ((page->type & NVKM_VMM_PAGE_SPARSE)) {
		u64 fail = nvkm_vmm_iter(vmm, page, addr, size, "sparse ref",
					 true, false, nvkm_vmm_sparse_ref_ptes,
					 NULL, NULL, page->desc->func->sparse);
		if (fail != ~0ULL) {
			if ((size = fail - addr))
				nvkm_vmm_ptes_sparse_put(vmm, page, addr, size);
			return -ENOMEM;
		}
		return 0;
	}
	return -EINVAL;
}

static int
nvkm_vmm_ptes_sparse(struct nvkm_vmm *vmm, u64 addr, u64 size, bool ref)
{
	const struct nvkm_vmm_page *page = vmm->func->page;
	int m = 0, i;
	u64 start = addr;
	u64 block;

	while (size) {
		/* Limit maximum page size based on remaining size. */
		while (size < (1ULL << page[m].shift))
			m++;
		i = m;

		/* Find largest page size suitable for alignment. */
		while (!IS_ALIGNED(addr, 1ULL << page[i].shift))
			i++;

		/* Determine number of PTEs at this page size. */
		if (i != m) {
			/* Limited to alignment boundary of next page size. */
			u64 next = 1ULL << page[i - 1].shift;
			u64 part = ALIGN(addr, next) - addr;
			if (size - part >= next)
				block = (part >> page[i].shift) << page[i].shift;
			else
				block = (size >> page[i].shift) << page[i].shift;
		} else {
			block = (size >> page[i].shift) << page[i].shift;
		}

		/* Perform operation. */
		if (ref) {
			int ret = nvkm_vmm_ptes_sparse_get(vmm, &page[i], addr, block);
			if (ret) {
				if ((size = addr - start))
					nvkm_vmm_ptes_sparse(vmm, start, size, false);
				return ret;
			}
		} else {
			nvkm_vmm_ptes_sparse_put(vmm, &page[i], addr, block);
		}

		size -= block;
		addr += block;
	}

	return 0;
}

static void
nvkm_vmm_ptes_unmap_put(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
			u64 addr, u64 size, bool sparse, bool pfn)
{
	const struct nvkm_vmm_desc_func *func = page->desc->func;
	nvkm_vmm_iter(vmm, page, addr, size, "unmap + unref",
		      false, pfn, nvkm_vmm_unref_ptes, NULL, NULL,
		      sparse ? func->sparse : func->invalid ? func->invalid :
							      func->unmap);
}

static int
nvkm_vmm_ptes_get_map(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
		      u64 addr, u64 size, struct nvkm_vmm_map *map,
		      nvkm_vmm_pte_func func)
{
	u64 fail = nvkm_vmm_iter(vmm, page, addr, size, "ref + map", true,
				 false, nvkm_vmm_ref_ptes, func, map, NULL);
	if (fail != ~0ULL) {
		if ((size = fail - addr))
			nvkm_vmm_ptes_unmap_put(vmm, page, addr, size, false, false);
		return -ENOMEM;
	}
	return 0;
}

static void
nvkm_vmm_ptes_unmap(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
		    u64 addr, u64 size, bool sparse, bool pfn)
{
	const struct nvkm_vmm_desc_func *func = page->desc->func;
	nvkm_vmm_iter(vmm, page, addr, size, "unmap", false, pfn,
		      NULL, NULL, NULL,
		      sparse ? func->sparse : func->invalid ? func->invalid :
							      func->unmap);
}

static void
nvkm_vmm_ptes_map(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
		  u64 addr, u64 size, struct nvkm_vmm_map *map,
		  nvkm_vmm_pte_func func)
{
	nvkm_vmm_iter(vmm, page, addr, size, "map", false, false,
		      NULL, func, map, NULL);
}

static void
nvkm_vmm_ptes_put(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
		  u64 addr, u64 size)
{
	nvkm_vmm_iter(vmm, page, addr, size, "unref", false, false,
		      nvkm_vmm_unref_ptes, NULL, NULL, NULL);
}

static int
nvkm_vmm_ptes_get(struct nvkm_vmm *vmm, const struct nvkm_vmm_page *page,
		  u64 addr, u64 size)
{
	u64 fail = nvkm_vmm_iter(vmm, page, addr, size, "ref", true, false,
				 nvkm_vmm_ref_ptes, NULL, NULL, NULL);
	if (fail != ~0ULL) {
		if (fail != addr)
			nvkm_vmm_ptes_put(vmm, page, addr, fail - addr);
		return -ENOMEM;
	}
	return 0;
}

static inline struct nvkm_vma *
nvkm_vma_new(u64 addr, u64 size)
{
	struct nvkm_vma *vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (vma) {
		vma->addr = addr;
		vma->size = size;
		vma->page = NVKM_VMA_PAGE_NONE;
		vma->refd = NVKM_VMA_PAGE_NONE;
	}
	return vma;
}

struct nvkm_vma *
nvkm_vma_tail(struct nvkm_vma *vma, u64 tail)
{
	struct nvkm_vma *new;

	BUG_ON(vma->size == tail);

	if (!(new = nvkm_vma_new(vma->addr + (vma->size - tail), tail)))
		return NULL;
	vma->size -= tail;

	new->mapref = vma->mapref;
	new->sparse = vma->sparse;
	new->page = vma->page;
	new->refd = vma->refd;
	new->used = vma->used;
	new->part = vma->part;
	new->user = vma->user;
	new->busy = vma->busy;
	new->mapped = vma->mapped;
	list_add(&new->head, &vma->head);
	return new;
}

static inline void
nvkm_vmm_free_remove(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	rb_erase(&vma->tree, &vmm->free);
}

static inline void
nvkm_vmm_free_delete(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	nvkm_vmm_free_remove(vmm, vma);
	list_del(&vma->head);
	kfree(vma);
}

static void
nvkm_vmm_free_insert(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	struct rb_node **ptr = &vmm->free.rb_node;
	struct rb_node *parent = NULL;

	while (*ptr) {
		struct nvkm_vma *this = rb_entry(*ptr, typeof(*this), tree);
		parent = *ptr;
		if (vma->size < this->size)
			ptr = &parent->rb_left;
		else
		if (vma->size > this->size)
			ptr = &parent->rb_right;
		else
		if (vma->addr < this->addr)
			ptr = &parent->rb_left;
		else
		if (vma->addr > this->addr)
			ptr = &parent->rb_right;
		else
			BUG();
	}

	rb_link_node(&vma->tree, parent, ptr);
	rb_insert_color(&vma->tree, &vmm->free);
}

static inline void
nvkm_vmm_node_remove(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	rb_erase(&vma->tree, &vmm->root);
}

static inline void
nvkm_vmm_node_delete(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	nvkm_vmm_node_remove(vmm, vma);
	list_del(&vma->head);
	kfree(vma);
}

static void
nvkm_vmm_node_insert(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	struct rb_node **ptr = &vmm->root.rb_node;
	struct rb_node *parent = NULL;

	while (*ptr) {
		struct nvkm_vma *this = rb_entry(*ptr, typeof(*this), tree);
		parent = *ptr;
		if (vma->addr < this->addr)
			ptr = &parent->rb_left;
		else
		if (vma->addr > this->addr)
			ptr = &parent->rb_right;
		else
			BUG();
	}

	rb_link_node(&vma->tree, parent, ptr);
	rb_insert_color(&vma->tree, &vmm->root);
}

struct nvkm_vma *
nvkm_vmm_node_search(struct nvkm_vmm *vmm, u64 addr)
{
	struct rb_node *node = vmm->root.rb_node;
	while (node) {
		struct nvkm_vma *vma = rb_entry(node, typeof(*vma), tree);
		if (addr < vma->addr)
			node = node->rb_left;
		else
		if (addr >= vma->addr + vma->size)
			node = node->rb_right;
		else
			return vma;
	}
	return NULL;
}

#define node(root, dir) (((root)->head.dir == &vmm->list) ? NULL :             \
	list_entry((root)->head.dir, struct nvkm_vma, head))

static struct nvkm_vma *
nvkm_vmm_node_merge(struct nvkm_vmm *vmm, struct nvkm_vma *prev,
		    struct nvkm_vma *vma, struct nvkm_vma *next, u64 size)
{
	if (next) {
		if (vma->size == size) {
			vma->size += next->size;
			nvkm_vmm_node_delete(vmm, next);
			if (prev) {
				prev->size += vma->size;
				nvkm_vmm_node_delete(vmm, vma);
				return prev;
			}
			return vma;
		}
		BUG_ON(prev);

		nvkm_vmm_node_remove(vmm, next);
		vma->size -= size;
		next->addr -= size;
		next->size += size;
		nvkm_vmm_node_insert(vmm, next);
		return next;
	}

	if (prev) {
		if (vma->size != size) {
			nvkm_vmm_node_remove(vmm, vma);
			prev->size += size;
			vma->addr += size;
			vma->size -= size;
			nvkm_vmm_node_insert(vmm, vma);
		} else {
			prev->size += vma->size;
			nvkm_vmm_node_delete(vmm, vma);
		}
		return prev;
	}

	return vma;
}

struct nvkm_vma *
nvkm_vmm_node_split(struct nvkm_vmm *vmm,
		    struct nvkm_vma *vma, u64 addr, u64 size)
{
	struct nvkm_vma *prev = NULL;

	if (vma->addr != addr) {
		prev = vma;
		if (!(vma = nvkm_vma_tail(vma, vma->size + vma->addr - addr)))
			return NULL;
		vma->part = true;
		nvkm_vmm_node_insert(vmm, vma);
	}

	if (vma->size != size) {
		struct nvkm_vma *tmp;
		if (!(tmp = nvkm_vma_tail(vma, vma->size - size))) {
			nvkm_vmm_node_merge(vmm, prev, vma, NULL, vma->size);
			return NULL;
		}
		tmp->part = true;
		nvkm_vmm_node_insert(vmm, tmp);
	}

	return vma;
}

static void
nvkm_vma_dump(struct nvkm_vma *vma)
{
	printk(KERN_ERR "%016llx %016llx %c%c%c%c%c%c%c%c%c %p\n",
	       vma->addr, (u64)vma->size,
	       vma->used ? '-' : 'F',
	       vma->mapref ? 'R' : '-',
	       vma->sparse ? 'S' : '-',
	       vma->page != NVKM_VMA_PAGE_NONE ? '0' + vma->page : '-',
	       vma->refd != NVKM_VMA_PAGE_NONE ? '0' + vma->refd : '-',
	       vma->part ? 'P' : '-',
	       vma->user ? 'U' : '-',
	       vma->busy ? 'B' : '-',
	       vma->mapped ? 'M' : '-',
	       vma->memory);
}

static void
nvkm_vmm_dump(struct nvkm_vmm *vmm)
{
	struct nvkm_vma *vma;
	list_for_each_entry(vma, &vmm->list, head) {
		nvkm_vma_dump(vma);
	}
}

static void
nvkm_vmm_dtor(struct nvkm_vmm *vmm)
{
	struct nvkm_vma *vma;
	struct rb_node *node;

	if (0)
		nvkm_vmm_dump(vmm);

	while ((node = rb_first(&vmm->root))) {
		struct nvkm_vma *vma = rb_entry(node, typeof(*vma), tree);
		nvkm_vmm_put(vmm, &vma);
	}

	if (vmm->bootstrapped) {
		const struct nvkm_vmm_page *page = vmm->func->page;
		const u64 limit = vmm->limit - vmm->start;

		while (page[1].shift)
			page++;

		nvkm_mmu_ptc_dump(vmm->mmu);
		nvkm_vmm_ptes_put(vmm, page, vmm->start, limit);
	}

	vma = list_first_entry(&vmm->list, typeof(*vma), head);
	list_del(&vma->head);
	kfree(vma);
	WARN_ON(!list_empty(&vmm->list));

	if (vmm->nullp) {
		dma_free_coherent(vmm->mmu->subdev.device->dev, 16 * 1024,
				  vmm->nullp, vmm->null);
	}

	if (vmm->pd) {
		nvkm_mmu_ptc_put(vmm->mmu, true, &vmm->pd->pt[0]);
		nvkm_vmm_pt_del(&vmm->pd);
	}
}

static int
nvkm_vmm_ctor_managed(struct nvkm_vmm *vmm, u64 addr, u64 size)
{
	struct nvkm_vma *vma;
	if (!(vma = nvkm_vma_new(addr, size)))
		return -ENOMEM;
	vma->mapref = true;
	vma->sparse = false;
	vma->used = true;
	vma->user = true;
	nvkm_vmm_node_insert(vmm, vma);
	list_add_tail(&vma->head, &vmm->list);
	return 0;
}

int
nvkm_vmm_ctor(const struct nvkm_vmm_func *func, struct nvkm_mmu *mmu,
	      u32 pd_header, bool managed, u64 addr, u64 size,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm *vmm)
{
	static struct lock_class_key _key;
	const struct nvkm_vmm_page *page = func->page;
	const struct nvkm_vmm_desc *desc;
	struct nvkm_vma *vma;
	int levels, bits = 0, ret;

	vmm->func = func;
	vmm->mmu = mmu;
	vmm->name = name;
	vmm->debug = mmu->subdev.debug;
	kref_init(&vmm->kref);

	__mutex_init(&vmm->mutex, "&vmm->mutex", key ? key : &_key);

	/* Locate the smallest page size supported by the backend, it will
	 * have the the deepest nesting of page tables.
	 */
	while (page[1].shift)
		page++;

	/* Locate the structure that describes the layout of the top-level
	 * page table, and determine the number of valid bits in a virtual
	 * address.
	 */
	for (levels = 0, desc = page->desc; desc->bits; desc++, levels++)
		bits += desc->bits;
	bits += page->shift;
	desc--;

	if (WARN_ON(levels > NVKM_VMM_LEVELS_MAX))
		return -EINVAL;

	/* Allocate top-level page table. */
	vmm->pd = nvkm_vmm_pt_new(desc, false, NULL);
	if (!vmm->pd)
		return -ENOMEM;
	vmm->pd->refs[0] = 1;
	INIT_LIST_HEAD(&vmm->join);

	/* ... and the GPU storage for it, except on Tesla-class GPUs that
	 * have the PD embedded in the instance structure.
	 */
	if (desc->size) {
		const u32 size = pd_header + desc->size * (1 << desc->bits);
		vmm->pd->pt[0] = nvkm_mmu_ptc_get(mmu, size, desc->align, true);
		if (!vmm->pd->pt[0])
			return -ENOMEM;
	}

	/* Initialise address-space MM. */
	INIT_LIST_HEAD(&vmm->list);
	vmm->free = RB_ROOT;
	vmm->root = RB_ROOT;

	if (managed) {
		/* Address-space will be managed by the client for the most
		 * part, except for a specified area where NVKM allocations
		 * are allowed to be placed.
		 */
		vmm->start = 0;
		vmm->limit = 1ULL << bits;
		if (addr + size < addr || addr + size > vmm->limit)
			return -EINVAL;

		/* Client-managed area before the NVKM-managed area. */
		if (addr && (ret = nvkm_vmm_ctor_managed(vmm, 0, addr)))
			return ret;

		/* NVKM-managed area. */
		if (size) {
			if (!(vma = nvkm_vma_new(addr, size)))
				return -ENOMEM;
			nvkm_vmm_free_insert(vmm, vma);
			list_add_tail(&vma->head, &vmm->list);
		}

		/* Client-managed area after the NVKM-managed area. */
		addr = addr + size;
		size = vmm->limit - addr;
		if (size && (ret = nvkm_vmm_ctor_managed(vmm, addr, size)))
			return ret;
	} else {
		/* Address-space fully managed by NVKM, requiring calls to
		 * nvkm_vmm_get()/nvkm_vmm_put() to allocate address-space.
		 */
		vmm->start = addr;
		vmm->limit = size ? (addr + size) : (1ULL << bits);
		if (vmm->start > vmm->limit || vmm->limit > (1ULL << bits))
			return -EINVAL;

		if (!(vma = nvkm_vma_new(vmm->start, vmm->limit - vmm->start)))
			return -ENOMEM;

		nvkm_vmm_free_insert(vmm, vma);
		list_add(&vma->head, &vmm->list);
	}

	return 0;
}

int
nvkm_vmm_new_(const struct nvkm_vmm_func *func, struct nvkm_mmu *mmu,
	      u32 hdr, bool managed, u64 addr, u64 size,
	      struct lock_class_key *key, const char *name,
	      struct nvkm_vmm **pvmm)
{
	if (!(*pvmm = kzalloc(sizeof(**pvmm), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_vmm_ctor(func, mmu, hdr, managed, addr, size, key, name, *pvmm);
}

static struct nvkm_vma *
nvkm_vmm_pfn_split_merge(struct nvkm_vmm *vmm, struct nvkm_vma *vma,
			 u64 addr, u64 size, u8 page, bool map)
{
	struct nvkm_vma *prev = NULL;
	struct nvkm_vma *next = NULL;

	if (vma->addr == addr && vma->part && (prev = node(vma, prev))) {
		if (prev->memory || prev->mapped != map)
			prev = NULL;
	}

	if (vma->addr + vma->size == addr + size && (next = node(vma, next))) {
		if (!next->part ||
		    next->memory || next->mapped != map)
			next = NULL;
	}

	if (prev || next)
		return nvkm_vmm_node_merge(vmm, prev, vma, next, size);
	return nvkm_vmm_node_split(vmm, vma, addr, size);
}

int
nvkm_vmm_pfn_unmap(struct nvkm_vmm *vmm, u64 addr, u64 size)
{
	struct nvkm_vma *vma = nvkm_vmm_node_search(vmm, addr);
	struct nvkm_vma *next;
	u64 limit = addr + size;
	u64 start = addr;

	if (!vma)
		return -EINVAL;

	do {
		if (!vma->mapped || vma->memory)
			continue;

		size = min(limit - start, vma->size - (start - vma->addr));

		nvkm_vmm_ptes_unmap_put(vmm, &vmm->func->page[vma->refd],
					start, size, false, true);

		next = nvkm_vmm_pfn_split_merge(vmm, vma, start, size, 0, false);
		if (!WARN_ON(!next)) {
			vma = next;
			vma->refd = NVKM_VMA_PAGE_NONE;
			vma->mapped = false;
		}
	} while ((vma = node(vma, next)) && (start = vma->addr) < limit);

	return 0;
}

/*TODO:
 * - Avoid PT readback (for dma_unmap etc), this might end up being dealt
 *   with inside HMM, which would be a lot nicer for us to deal with.
 * - Multiple page sizes (particularly for huge page support).
 * - Support for systems without a 4KiB page size.
 */
int
nvkm_vmm_pfn_map(struct nvkm_vmm *vmm, u8 shift, u64 addr, u64 size, u64 *pfn)
{
	const struct nvkm_vmm_page *page = vmm->func->page;
	struct nvkm_vma *vma, *tmp;
	u64 limit = addr + size;
	u64 start = addr;
	int pm = size >> shift;
	int pi = 0;

	/* Only support mapping where the page size of the incoming page
	 * array matches a page size available for direct mapping.
	 */
	while (page->shift && page->shift != shift &&
	       page->desc->func->pfn == NULL)
		page++;

	if (!page->shift || !IS_ALIGNED(addr, 1ULL << shift) ||
			    !IS_ALIGNED(size, 1ULL << shift) ||
	    addr + size < addr || addr + size > vmm->limit) {
		VMM_DEBUG(vmm, "paged map %d %d %016llx %016llx\n",
			  shift, page->shift, addr, size);
		return -EINVAL;
	}

	if (!(vma = nvkm_vmm_node_search(vmm, addr)))
		return -ENOENT;

	do {
		bool map = !!(pfn[pi] & NVKM_VMM_PFN_V);
		bool mapped = vma->mapped;
		u64 size = limit - start;
		u64 addr = start;
		int pn, ret = 0;

		/* Narrow the operation window to cover a single action (page
		 * should be mapped or not) within a single VMA.
		 */
		for (pn = 0; pi + pn < pm; pn++) {
			if (map != !!(pfn[pi + pn] & NVKM_VMM_PFN_V))
				break;
		}
		size = min_t(u64, size, pn << page->shift);
		size = min_t(u64, size, vma->size + vma->addr - addr);

		/* Reject any operation to unmanaged regions, and areas that
		 * have nvkm_memory objects mapped in them already.
		 */
		if (!vma->mapref || vma->memory) {
			ret = -EINVAL;
			goto next;
		}

		/* In order to both properly refcount GPU page tables, and
		 * prevent "normal" mappings and these direct mappings from
		 * interfering with each other, we need to track contiguous
		 * ranges that have been mapped with this interface.
		 *
		 * Here we attempt to either split an existing VMA so we're
		 * able to flag the region as either unmapped/mapped, or to
		 * merge with adjacent VMAs that are already compatible.
		 *
		 * If the region is already compatible, nothing is required.
		 */
		if (map != mapped) {
			tmp = nvkm_vmm_pfn_split_merge(vmm, vma, addr, size,
						       page -
						       vmm->func->page, map);
			if (WARN_ON(!tmp)) {
				ret = -ENOMEM;
				goto next;
			}

			if ((tmp->mapped = map))
				tmp->refd = page - vmm->func->page;
			else
				tmp->refd = NVKM_VMA_PAGE_NONE;
			vma = tmp;
		}

		/* Update HW page tables. */
		if (map) {
			struct nvkm_vmm_map args;
			args.page = page;
			args.pfn = &pfn[pi];

			if (!mapped) {
				ret = nvkm_vmm_ptes_get_map(vmm, page, addr,
							    size, &args, page->
							    desc->func->pfn);
			} else {
				nvkm_vmm_ptes_map(vmm, page, addr, size, &args,
						  page->desc->func->pfn);
			}
		} else {
			if (mapped) {
				nvkm_vmm_ptes_unmap_put(vmm, page, addr, size,
							false, true);
			}
		}

next:
		/* Iterate to next operation. */
		if (vma->addr + vma->size == addr + size)
			vma = node(vma, next);
		start += size;

		if (ret) {
			/* Failure is signalled by clearing the valid bit on
			 * any PFN that couldn't be modified as requested.
			 */
			while (size) {
				pfn[pi++] = NVKM_VMM_PFN_NONE;
				size -= 1 << page->shift;
			}
		} else {
			pi += size >> page->shift;
		}
	} while (vma && start < limit);

	return 0;
}

void
nvkm_vmm_unmap_region(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	struct nvkm_vma *prev = NULL;
	struct nvkm_vma *next;

	nvkm_memory_tags_put(vma->memory, vmm->mmu->subdev.device, &vma->tags);
	nvkm_memory_unref(&vma->memory);
	vma->mapped = false;

	if (vma->part && (prev = node(vma, prev)) && prev->mapped)
		prev = NULL;
	if ((next = node(vma, next)) && (!next->part || next->mapped))
		next = NULL;
	nvkm_vmm_node_merge(vmm, prev, vma, next, vma->size);
}

void
nvkm_vmm_unmap_locked(struct nvkm_vmm *vmm, struct nvkm_vma *vma, bool pfn)
{
	const struct nvkm_vmm_page *page = &vmm->func->page[vma->refd];

	if (vma->mapref) {
		nvkm_vmm_ptes_unmap_put(vmm, page, vma->addr, vma->size, vma->sparse, pfn);
		vma->refd = NVKM_VMA_PAGE_NONE;
	} else {
		nvkm_vmm_ptes_unmap(vmm, page, vma->addr, vma->size, vma->sparse, pfn);
	}

	nvkm_vmm_unmap_region(vmm, vma);
}

void
nvkm_vmm_unmap(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	if (vma->memory) {
		mutex_lock(&vmm->mutex);
		nvkm_vmm_unmap_locked(vmm, vma, false);
		mutex_unlock(&vmm->mutex);
	}
}

static int
nvkm_vmm_map_valid(struct nvkm_vmm *vmm, struct nvkm_vma *vma,
		   void *argv, u32 argc, struct nvkm_vmm_map *map)
{
	switch (nvkm_memory_target(map->memory)) {
	case NVKM_MEM_TARGET_VRAM:
		if (!(map->page->type & NVKM_VMM_PAGE_VRAM)) {
			VMM_DEBUG(vmm, "%d !VRAM", map->page->shift);
			return -EINVAL;
		}
		break;
	case NVKM_MEM_TARGET_HOST:
	case NVKM_MEM_TARGET_NCOH:
		if (!(map->page->type & NVKM_VMM_PAGE_HOST)) {
			VMM_DEBUG(vmm, "%d !HOST", map->page->shift);
			return -EINVAL;
		}
		break;
	default:
		WARN_ON(1);
		return -ENOSYS;
	}

	if (!IS_ALIGNED(     vma->addr, 1ULL << map->page->shift) ||
	    !IS_ALIGNED((u64)vma->size, 1ULL << map->page->shift) ||
	    !IS_ALIGNED(   map->offset, 1ULL << map->page->shift) ||
	    nvkm_memory_page(map->memory) < map->page->shift) {
		VMM_DEBUG(vmm, "alignment %016llx %016llx %016llx %d %d",
		    vma->addr, (u64)vma->size, map->offset, map->page->shift,
		    nvkm_memory_page(map->memory));
		return -EINVAL;
	}

	return vmm->func->valid(vmm, argv, argc, map);
}

static int
nvkm_vmm_map_choose(struct nvkm_vmm *vmm, struct nvkm_vma *vma,
		    void *argv, u32 argc, struct nvkm_vmm_map *map)
{
	for (map->page = vmm->func->page; map->page->shift; map->page++) {
		VMM_DEBUG(vmm, "trying %d", map->page->shift);
		if (!nvkm_vmm_map_valid(vmm, vma, argv, argc, map))
			return 0;
	}
	return -EINVAL;
}

static int
nvkm_vmm_map_locked(struct nvkm_vmm *vmm, struct nvkm_vma *vma,
		    void *argv, u32 argc, struct nvkm_vmm_map *map)
{
	nvkm_vmm_pte_func func;
	int ret;

	/* Make sure we won't overrun the end of the memory object. */
	if (unlikely(nvkm_memory_size(map->memory) < map->offset + vma->size)) {
		VMM_DEBUG(vmm, "overrun %016llx %016llx %016llx",
			  nvkm_memory_size(map->memory),
			  map->offset, (u64)vma->size);
		return -EINVAL;
	}

	/* Check remaining arguments for validity. */
	if (vma->page == NVKM_VMA_PAGE_NONE &&
	    vma->refd == NVKM_VMA_PAGE_NONE) {
		/* Find the largest page size we can perform the mapping at. */
		const u32 debug = vmm->debug;
		vmm->debug = 0;
		ret = nvkm_vmm_map_choose(vmm, vma, argv, argc, map);
		vmm->debug = debug;
		if (ret) {
			VMM_DEBUG(vmm, "invalid at any page size");
			nvkm_vmm_map_choose(vmm, vma, argv, argc, map);
			return -EINVAL;
		}
	} else {
		/* Page size of the VMA is already pre-determined. */
		if (vma->refd != NVKM_VMA_PAGE_NONE)
			map->page = &vmm->func->page[vma->refd];
		else
			map->page = &vmm->func->page[vma->page];

		ret = nvkm_vmm_map_valid(vmm, vma, argv, argc, map);
		if (ret) {
			VMM_DEBUG(vmm, "invalid %d\n", ret);
			return ret;
		}
	}

	/* Deal with the 'offset' argument, and fetch the backend function. */
	map->off = map->offset;
	if (map->mem) {
		for (; map->off; map->mem = map->mem->next) {
			u64 size = (u64)map->mem->length << NVKM_RAM_MM_SHIFT;
			if (size > map->off)
				break;
			map->off -= size;
		}
		func = map->page->desc->func->mem;
	} else
	if (map->sgl) {
		for (; map->off; map->sgl = sg_next(map->sgl)) {
			u64 size = sg_dma_len(map->sgl);
			if (size > map->off)
				break;
			map->off -= size;
		}
		func = map->page->desc->func->sgl;
	} else {
		map->dma += map->offset >> PAGE_SHIFT;
		map->off  = map->offset & PAGE_MASK;
		func = map->page->desc->func->dma;
	}

	/* Perform the map. */
	if (vma->refd == NVKM_VMA_PAGE_NONE) {
		ret = nvkm_vmm_ptes_get_map(vmm, map->page, vma->addr, vma->size, map, func);
		if (ret)
			return ret;

		vma->refd = map->page - vmm->func->page;
	} else {
		nvkm_vmm_ptes_map(vmm, map->page, vma->addr, vma->size, map, func);
	}

	nvkm_memory_tags_put(vma->memory, vmm->mmu->subdev.device, &vma->tags);
	nvkm_memory_unref(&vma->memory);
	vma->memory = nvkm_memory_ref(map->memory);
	vma->mapped = true;
	vma->tags = map->tags;
	return 0;
}

int
nvkm_vmm_map(struct nvkm_vmm *vmm, struct nvkm_vma *vma, void *argv, u32 argc,
	     struct nvkm_vmm_map *map)
{
	int ret;
	mutex_lock(&vmm->mutex);
	ret = nvkm_vmm_map_locked(vmm, vma, argv, argc, map);
	vma->busy = false;
	mutex_unlock(&vmm->mutex);
	return ret;
}

static void
nvkm_vmm_put_region(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	struct nvkm_vma *prev, *next;

	if ((prev = node(vma, prev)) && !prev->used) {
		vma->addr  = prev->addr;
		vma->size += prev->size;
		nvkm_vmm_free_delete(vmm, prev);
	}

	if ((next = node(vma, next)) && !next->used) {
		vma->size += next->size;
		nvkm_vmm_free_delete(vmm, next);
	}

	nvkm_vmm_free_insert(vmm, vma);
}

void
nvkm_vmm_put_locked(struct nvkm_vmm *vmm, struct nvkm_vma *vma)
{
	const struct nvkm_vmm_page *page = vmm->func->page;
	struct nvkm_vma *next = vma;

	BUG_ON(vma->part);

	if (vma->mapref || !vma->sparse) {
		do {
			const bool mem = next->memory != NULL;
			const bool map = next->mapped;
			const u8  refd = next->refd;
			const u64 addr = next->addr;
			u64 size = next->size;

			/* Merge regions that are in the same state. */
			while ((next = node(next, next)) && next->part &&
			       (next->mapped == map) &&
			       (next->memory != NULL) == mem &&
			       (next->refd == refd))
				size += next->size;

			if (map) {
				/* Region(s) are mapped, merge the unmap
				 * and dereference into a single walk of
				 * the page tree.
				 */
				nvkm_vmm_ptes_unmap_put(vmm, &page[refd], addr,
							size, vma->sparse,
							!mem);
			} else
			if (refd != NVKM_VMA_PAGE_NONE) {
				/* Drop allocation-time PTE references. */
				nvkm_vmm_ptes_put(vmm, &page[refd], addr, size);
			}
		} while (next && next->part);
	}

	/* Merge any mapped regions that were split from the initial
	 * address-space allocation back into the allocated VMA, and
	 * release memory/compression resources.
	 */
	next = vma;
	do {
		if (next->mapped)
			nvkm_vmm_unmap_region(vmm, next);
	} while ((next = node(vma, next)) && next->part);

	if (vma->sparse && !vma->mapref) {
		/* Sparse region that was allocated with a fixed page size,
		 * meaning all relevant PTEs were referenced once when the
		 * region was allocated, and remained that way, regardless
		 * of whether memory was mapped into it afterwards.
		 *
		 * The process of unmapping, unsparsing, and dereferencing
		 * PTEs can be done in a single page tree walk.
		 */
		nvkm_vmm_ptes_sparse_put(vmm, &page[vma->refd], vma->addr, vma->size);
	} else
	if (vma->sparse) {
		/* Sparse region that wasn't allocated with a fixed page size,
		 * PTE references were taken both at allocation time (to make
		 * the GPU see the region as sparse), and when mapping memory
		 * into the region.
		 *
		 * The latter was handled above, and the remaining references
		 * are dealt with here.
		 */
		nvkm_vmm_ptes_sparse(vmm, vma->addr, vma->size, false);
	}

	/* Remove VMA from the list of allocated nodes. */
	nvkm_vmm_node_remove(vmm, vma);

	/* Merge VMA back into the free list. */
	vma->page = NVKM_VMA_PAGE_NONE;
	vma->refd = NVKM_VMA_PAGE_NONE;
	vma->used = false;
	vma->user = false;
	nvkm_vmm_put_region(vmm, vma);
}

void
nvkm_vmm_put(struct nvkm_vmm *vmm, struct nvkm_vma **pvma)
{
	struct nvkm_vma *vma = *pvma;
	if (vma) {
		mutex_lock(&vmm->mutex);
		nvkm_vmm_put_locked(vmm, vma);
		mutex_unlock(&vmm->mutex);
		*pvma = NULL;
	}
}

int
nvkm_vmm_get_locked(struct nvkm_vmm *vmm, bool getref, bool mapref, bool sparse,
		    u8 shift, u8 align, u64 size, struct nvkm_vma **pvma)
{
	const struct nvkm_vmm_page *page = &vmm->func->page[NVKM_VMA_PAGE_NONE];
	struct rb_node *node = NULL, *temp;
	struct nvkm_vma *vma = NULL, *tmp;
	u64 addr, tail;
	int ret;

	VMM_TRACE(vmm, "getref %d mapref %d sparse %d "
		       "shift: %d align: %d size: %016llx",
		  getref, mapref, sparse, shift, align, size);

	/* Zero-sized, or lazily-allocated sparse VMAs, make no sense. */
	if (unlikely(!size || (!getref && !mapref && sparse))) {
		VMM_DEBUG(vmm, "args %016llx %d %d %d",
			  size, getref, mapref, sparse);
		return -EINVAL;
	}

	/* Tesla-class GPUs can only select page size per-PDE, which means
	 * we're required to know the mapping granularity up-front to find
	 * a suitable region of address-space.
	 *
	 * The same goes if we're requesting up-front allocation of PTES.
	 */
	if (unlikely((getref || vmm->func->page_block) && !shift)) {
		VMM_DEBUG(vmm, "page size required: %d %016llx",
			  getref, vmm->func->page_block);
		return -EINVAL;
	}

	/* If a specific page size was requested, determine its index and
	 * make sure the requested size is a multiple of the page size.
	 */
	if (shift) {
		for (page = vmm->func->page; page->shift; page++) {
			if (shift == page->shift)
				break;
		}

		if (!page->shift || !IS_ALIGNED(size, 1ULL << page->shift)) {
			VMM_DEBUG(vmm, "page %d %016llx", shift, size);
			return -EINVAL;
		}
		align = max_t(u8, align, shift);
	} else {
		align = max_t(u8, align, 12);
	}

	/* Locate smallest block that can possibly satisfy the allocation. */
	temp = vmm->free.rb_node;
	while (temp) {
		struct nvkm_vma *this = rb_entry(temp, typeof(*this), tree);
		if (this->size < size) {
			temp = temp->rb_right;
		} else {
			node = temp;
			temp = temp->rb_left;
		}
	}

	if (unlikely(!node))
		return -ENOSPC;

	/* Take into account alignment restrictions, trying larger blocks
	 * in turn until we find a suitable free block.
	 */
	do {
		struct nvkm_vma *this = rb_entry(node, typeof(*this), tree);
		struct nvkm_vma *prev = node(this, prev);
		struct nvkm_vma *next = node(this, next);
		const int p = page - vmm->func->page;

		addr = this->addr;
		if (vmm->func->page_block && prev && prev->page != p)
			addr = ALIGN(addr, vmm->func->page_block);
		addr = ALIGN(addr, 1ULL << align);

		tail = this->addr + this->size;
		if (vmm->func->page_block && next && next->page != p)
			tail = ALIGN_DOWN(tail, vmm->func->page_block);

		if (addr <= tail && tail - addr >= size) {
			nvkm_vmm_free_remove(vmm, this);
			vma = this;
			break;
		}
	} while ((node = rb_next(node)));

	if (unlikely(!vma))
		return -ENOSPC;

	/* If the VMA we found isn't already exactly the requested size,
	 * it needs to be split, and the remaining free blocks returned.
	 */
	if (addr != vma->addr) {
		if (!(tmp = nvkm_vma_tail(vma, vma->size + vma->addr - addr))) {
			nvkm_vmm_put_region(vmm, vma);
			return -ENOMEM;
		}
		nvkm_vmm_free_insert(vmm, vma);
		vma = tmp;
	}

	if (size != vma->size) {
		if (!(tmp = nvkm_vma_tail(vma, vma->size - size))) {
			nvkm_vmm_put_region(vmm, vma);
			return -ENOMEM;
		}
		nvkm_vmm_free_insert(vmm, tmp);
	}

	/* Pre-allocate page tables and/or setup sparse mappings. */
	if (sparse && getref)
		ret = nvkm_vmm_ptes_sparse_get(vmm, page, vma->addr, vma->size);
	else if (sparse)
		ret = nvkm_vmm_ptes_sparse(vmm, vma->addr, vma->size, true);
	else if (getref)
		ret = nvkm_vmm_ptes_get(vmm, page, vma->addr, vma->size);
	else
		ret = 0;
	if (ret) {
		nvkm_vmm_put_region(vmm, vma);
		return ret;
	}

	vma->mapref = mapref && !getref;
	vma->sparse = sparse;
	vma->page = page - vmm->func->page;
	vma->refd = getref ? vma->page : NVKM_VMA_PAGE_NONE;
	vma->used = true;
	nvkm_vmm_node_insert(vmm, vma);
	*pvma = vma;
	return 0;
}

int
nvkm_vmm_get(struct nvkm_vmm *vmm, u8 page, u64 size, struct nvkm_vma **pvma)
{
	int ret;
	mutex_lock(&vmm->mutex);
	ret = nvkm_vmm_get_locked(vmm, false, true, false, page, 0, size, pvma);
	mutex_unlock(&vmm->mutex);
	return ret;
}

void
nvkm_vmm_part(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	if (inst && vmm && vmm->func->part) {
		mutex_lock(&vmm->mutex);
		vmm->func->part(vmm, inst);
		mutex_unlock(&vmm->mutex);
	}
}

int
nvkm_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	int ret = 0;
	if (vmm->func->join) {
		mutex_lock(&vmm->mutex);
		ret = vmm->func->join(vmm, inst);
		mutex_unlock(&vmm->mutex);
	}
	return ret;
}

static bool
nvkm_vmm_boot_ptes(struct nvkm_vmm_iter *it, bool pfn, u32 ptei, u32 ptes)
{
	const struct nvkm_vmm_desc *desc = it->desc;
	const int type = desc->type == SPT;
	nvkm_memory_boot(it->pt[0]->pt[type]->memory, it->vmm);
	return false;
}

int
nvkm_vmm_boot(struct nvkm_vmm *vmm)
{
	const struct nvkm_vmm_page *page = vmm->func->page;
	const u64 limit = vmm->limit - vmm->start;
	int ret;

	while (page[1].shift)
		page++;

	ret = nvkm_vmm_ptes_get(vmm, page, vmm->start, limit);
	if (ret)
		return ret;

	nvkm_vmm_iter(vmm, page, vmm->start, limit, "bootstrap", false, false,
		      nvkm_vmm_boot_ptes, NULL, NULL, NULL);
	vmm->bootstrapped = true;
	return 0;
}

static void
nvkm_vmm_del(struct kref *kref)
{
	struct nvkm_vmm *vmm = container_of(kref, typeof(*vmm), kref);
	nvkm_vmm_dtor(vmm);
	kfree(vmm);
}

void
nvkm_vmm_unref(struct nvkm_vmm **pvmm)
{
	struct nvkm_vmm *vmm = *pvmm;
	if (vmm) {
		kref_put(&vmm->kref, nvkm_vmm_del);
		*pvmm = NULL;
	}
}

struct nvkm_vmm *
nvkm_vmm_ref(struct nvkm_vmm *vmm)
{
	if (vmm)
		kref_get(&vmm->kref);
	return vmm;
}

int
nvkm_vmm_new(struct nvkm_device *device, u64 addr, u64 size, void *argv,
	     u32 argc, struct lock_class_key *key, const char *name,
	     struct nvkm_vmm **pvmm)
{
	struct nvkm_mmu *mmu = device->mmu;
	struct nvkm_vmm *vmm = NULL;
	int ret;
	ret = mmu->func->vmm.ctor(mmu, false, addr, size, argv, argc,
				  key, name, &vmm);
	if (ret)
		nvkm_vmm_unref(&vmm);
	*pvmm = vmm;
	return ret;
}
