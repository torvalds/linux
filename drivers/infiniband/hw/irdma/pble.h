/* SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB */
/* Copyright (c) 2015 - 2019 Intel Corporation */
#ifndef IRDMA_PBLE_H
#define IRDMA_PBLE_H

#define PBLE_SHIFT		6
#define PBLE_PER_PAGE		512
#define HMC_PAGED_BP_SHIFT	12
#define PBLE_512_SHIFT		9
#define PBLE_INVALID_IDX	0xffffffff

enum irdma_pble_level {
	PBLE_LEVEL_0 = 0,
	PBLE_LEVEL_1 = 1,
	PBLE_LEVEL_2 = 2,
};

enum irdma_alloc_type {
	PBLE_NO_ALLOC	  = 0,
	PBLE_SD_CONTIGOUS = 1,
	PBLE_SD_PAGED	  = 2,
};

struct irdma_chunk;

struct irdma_pble_chunkinfo {
	struct irdma_chunk *pchunk;
	u64 bit_idx;
	u64 bits_used;
};

struct irdma_pble_info {
	u64 *addr;
	u32 idx;
	u32 cnt;
	struct irdma_pble_chunkinfo chunkinfo;
};

struct irdma_pble_level2 {
	struct irdma_pble_info root;
	struct irdma_pble_info *leaf;
	struct irdma_virt_mem leafmem;
	u32 leaf_cnt;
};

struct irdma_pble_alloc {
	u32 total_cnt;
	enum irdma_pble_level level;
	union {
		struct irdma_pble_info level1;
		struct irdma_pble_level2 level2;
	};
};

struct sd_pd_idx {
	u32 sd_idx;
	u32 pd_idx;
	u32 rel_pd_idx;
};

struct irdma_add_page_info {
	struct irdma_chunk *chunk;
	struct irdma_hmc_sd_entry *sd_entry;
	struct irdma_hmc_info *hmc_info;
	struct sd_pd_idx idx;
	u32 pages;
};

struct irdma_chunk {
	struct list_head list;
	struct irdma_dma_info dmainfo;
	void *bitmapbuf;

	u32 sizeofbitmap;
	u64 size;
	void *vaddr;
	u64 fpm_addr;
	u32 pg_cnt;
	enum irdma_alloc_type type;
	struct irdma_sc_dev *dev;
	struct irdma_virt_mem bitmapmem;
	struct irdma_virt_mem chunkmem;
};

struct irdma_pble_prm {
	struct list_head clist;
	spinlock_t prm_lock; /* protect prm bitmap */
	u64 total_pble_alloc;
	u64 free_pble_cnt;
	u8 pble_shift;
};

struct irdma_hmc_pble_rsrc {
	u32 unallocated_pble;
	struct mutex pble_mutex_lock; /* protect PBLE resource */
	struct irdma_sc_dev *dev;
	u64 fpm_base_addr;
	u64 next_fpm_addr;
	struct irdma_pble_prm pinfo;
	u64 allocdpbles;
	u64 freedpbles;
	u32 stats_direct_sds;
	u32 stats_paged_sds;
	u64 stats_alloc_ok;
	u64 stats_alloc_fail;
	u64 stats_alloc_freed;
	u64 stats_lvl1;
	u64 stats_lvl2;
};

void irdma_destroy_pble_prm(struct irdma_hmc_pble_rsrc *pble_rsrc);
enum irdma_status_code
irdma_hmc_init_pble(struct irdma_sc_dev *dev,
		    struct irdma_hmc_pble_rsrc *pble_rsrc);
void irdma_free_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
		     struct irdma_pble_alloc *palloc);
enum irdma_status_code irdma_get_pble(struct irdma_hmc_pble_rsrc *pble_rsrc,
				      struct irdma_pble_alloc *palloc,
				      u32 pble_cnt, bool level1_only);
enum irdma_status_code irdma_prm_add_pble_mem(struct irdma_pble_prm *pprm,
					      struct irdma_chunk *pchunk);
enum irdma_status_code
irdma_prm_get_pbles(struct irdma_pble_prm *pprm,
		    struct irdma_pble_chunkinfo *chunkinfo, u64 mem_size,
		    u64 **vaddr, u64 *fpm_addr);
void irdma_prm_return_pbles(struct irdma_pble_prm *pprm,
			    struct irdma_pble_chunkinfo *chunkinfo);
void irdma_pble_acquire_lock(struct irdma_hmc_pble_rsrc *pble_rsrc,
			     unsigned long *flags);
void irdma_pble_release_lock(struct irdma_hmc_pble_rsrc *pble_rsrc,
			     unsigned long *flags);
void irdma_pble_free_paged_mem(struct irdma_chunk *chunk);
enum irdma_status_code irdma_pble_get_paged_mem(struct irdma_chunk *chunk,
						u32 pg_cnt);
void irdma_prm_rem_bitmapmem(struct irdma_hw *hw, struct irdma_chunk *chunk);
#endif /* IRDMA_PBLE_H */
