/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#ifndef _SIW_MEM_H
#define _SIW_MEM_H

struct siw_umem *siw_umem_get(u64 start, u64 len, bool writable);
void siw_umem_release(struct siw_umem *umem, bool dirty);
struct siw_pbl *siw_pbl_alloc(u32 num_buf);
u64 siw_pbl_get_buffer(struct siw_pbl *pbl, u64 off, int *len, int *idx);
struct siw_mem *siw_mem_id2obj(struct siw_device *sdev, int stag_index);
int siw_mem_add(struct siw_device *sdev, struct siw_mem *m);
int siw_invalidate_stag(struct ib_pd *pd, u32 stag);
int siw_check_mem(struct ib_pd *pd, struct siw_mem *mem, u64 addr,
		  enum ib_access_flags perms, int len);
int siw_check_sge(struct ib_pd *pd, struct siw_sge *sge,
		  struct siw_mem *mem[], enum ib_access_flags perms,
		  u32 off, int len);
void siw_wqe_put_mem(struct siw_wqe *wqe, enum siw_opcode op);
int siw_mr_add_mem(struct siw_mr *mr, struct ib_pd *pd, void *mem_obj,
		   u64 start, u64 len, int rights);
void siw_mr_drop_mem(struct siw_mr *mr);
void siw_free_mem(struct kref *ref);

static inline void siw_mem_put(struct siw_mem *mem)
{
	kref_put(&mem->ref, siw_free_mem);
}

static inline struct siw_mr *siw_mem2mr(struct siw_mem *m)
{
	return container_of(m, struct siw_mr, mem);
}

static inline void siw_unref_mem_sgl(struct siw_mem **mem, unsigned int num_sge)
{
	while (num_sge) {
		if (*mem == NULL)
			break;

		siw_mem_put(*mem);
		*mem = NULL;
		mem++;
		num_sge--;
	}
}

#define CHUNK_SHIFT 9 /* sets number of pages per chunk */
#define PAGES_PER_CHUNK (_AC(1, UL) << CHUNK_SHIFT)
#define CHUNK_MASK (~(PAGES_PER_CHUNK - 1))
#define PAGE_CHUNK_SIZE (PAGES_PER_CHUNK * sizeof(struct page *))

/*
 * siw_get_upage()
 *
 * Get page pointer for address on given umem.
 *
 * @umem: two dimensional list of page pointers
 * @addr: user virtual address
 */
static inline struct page *siw_get_upage(struct siw_umem *umem, u64 addr)
{
	unsigned int page_idx = (addr - umem->fp_addr) >> PAGE_SHIFT,
		     chunk_idx = page_idx >> CHUNK_SHIFT,
		     page_in_chunk = page_idx & ~CHUNK_MASK;

	if (likely(page_idx < umem->num_pages))
		return umem->page_chunk[chunk_idx].plist[page_in_chunk];

	return NULL;
}
#endif
