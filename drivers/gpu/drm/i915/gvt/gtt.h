/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *    Zhenyu Wang <zhenyuw@linux.intel.com>
 *    Xiao Zheng <xiao.zheng@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *
 */

#ifndef _GVT_GTT_H_
#define _GVT_GTT_H_

#define GTT_PAGE_SHIFT		12
#define GTT_PAGE_SIZE		(1UL << GTT_PAGE_SHIFT)
#define GTT_PAGE_MASK		(~(GTT_PAGE_SIZE-1))

struct intel_vgpu_mm;

#define INTEL_GVT_GTT_HASH_BITS 8
#define INTEL_GVT_INVALID_ADDR (~0UL)

struct intel_gvt_gtt_entry {
	u64 val64;
	int type;
};

struct intel_gvt_gtt_pte_ops {
	struct intel_gvt_gtt_entry *(*get_entry)(void *pt,
		struct intel_gvt_gtt_entry *e,
		unsigned long index, bool hypervisor_access, unsigned long gpa,
		struct intel_vgpu *vgpu);
	struct intel_gvt_gtt_entry *(*set_entry)(void *pt,
		struct intel_gvt_gtt_entry *e,
		unsigned long index, bool hypervisor_access, unsigned long gpa,
		struct intel_vgpu *vgpu);
	bool (*test_present)(struct intel_gvt_gtt_entry *e);
	void (*clear_present)(struct intel_gvt_gtt_entry *e);
	bool (*test_pse)(struct intel_gvt_gtt_entry *e);
	void (*set_pfn)(struct intel_gvt_gtt_entry *e, unsigned long pfn);
	unsigned long (*get_pfn)(struct intel_gvt_gtt_entry *e);
};

struct intel_gvt_gtt_gma_ops {
	unsigned long (*gma_to_ggtt_pte_index)(unsigned long gma);
	unsigned long (*gma_to_pte_index)(unsigned long gma);
	unsigned long (*gma_to_pde_index)(unsigned long gma);
	unsigned long (*gma_to_l3_pdp_index)(unsigned long gma);
	unsigned long (*gma_to_l4_pdp_index)(unsigned long gma);
	unsigned long (*gma_to_pml4_index)(unsigned long gma);
};

struct intel_gvt_gtt {
	struct intel_gvt_gtt_pte_ops *pte_ops;
	struct intel_gvt_gtt_gma_ops *gma_ops;
	int (*mm_alloc_page_table)(struct intel_vgpu_mm *mm);
	void (*mm_free_page_table)(struct intel_vgpu_mm *mm);
	struct list_head oos_page_use_list_head;
	struct list_head oos_page_free_list_head;
	struct list_head mm_lru_list_head;
};

enum {
	INTEL_GVT_MM_GGTT = 0,
	INTEL_GVT_MM_PPGTT,
};

typedef enum {
	GTT_TYPE_INVALID = -1,

	GTT_TYPE_GGTT_PTE,

	GTT_TYPE_PPGTT_PTE_4K_ENTRY,
	GTT_TYPE_PPGTT_PTE_2M_ENTRY,
	GTT_TYPE_PPGTT_PTE_1G_ENTRY,

	GTT_TYPE_PPGTT_PTE_ENTRY,

	GTT_TYPE_PPGTT_PDE_ENTRY,
	GTT_TYPE_PPGTT_PDP_ENTRY,
	GTT_TYPE_PPGTT_PML4_ENTRY,

	GTT_TYPE_PPGTT_ROOT_ENTRY,

	GTT_TYPE_PPGTT_ROOT_L3_ENTRY,
	GTT_TYPE_PPGTT_ROOT_L4_ENTRY,

	GTT_TYPE_PPGTT_ENTRY,

	GTT_TYPE_PPGTT_PTE_PT,
	GTT_TYPE_PPGTT_PDE_PT,
	GTT_TYPE_PPGTT_PDP_PT,
	GTT_TYPE_PPGTT_PML4_PT,

	GTT_TYPE_MAX,
} intel_gvt_gtt_type_t;

struct intel_vgpu_mm {
	int type;
	bool initialized;
	bool shadowed;

	int page_table_entry_type;
	u32 page_table_entry_size;
	u32 page_table_entry_cnt;
	void *virtual_page_table;
	void *shadow_page_table;

	int page_table_level;
	bool has_shadow_page_table;
	u32 pde_base_index;

	struct list_head list;
	struct kref ref;
	atomic_t pincount;
	struct list_head lru_list;
	struct intel_vgpu *vgpu;
};

extern struct intel_gvt_gtt_entry *intel_vgpu_mm_get_entry(
		struct intel_vgpu_mm *mm,
		void *page_table, struct intel_gvt_gtt_entry *e,
		unsigned long index);

extern struct intel_gvt_gtt_entry *intel_vgpu_mm_set_entry(
		struct intel_vgpu_mm *mm,
		void *page_table, struct intel_gvt_gtt_entry *e,
		unsigned long index);

#define ggtt_get_guest_entry(mm, e, index) \
	intel_vgpu_mm_get_entry(mm, mm->virtual_page_table, e, index)

#define ggtt_set_guest_entry(mm, e, index) \
	intel_vgpu_mm_set_entry(mm, mm->virtual_page_table, e, index)

#define ggtt_get_shadow_entry(mm, e, index) \
	intel_vgpu_mm_get_entry(mm, mm->shadow_page_table, e, index)

#define ggtt_set_shadow_entry(mm, e, index) \
	intel_vgpu_mm_set_entry(mm, mm->shadow_page_table, e, index)

#define ppgtt_get_guest_root_entry(mm, e, index) \
	intel_vgpu_mm_get_entry(mm, mm->virtual_page_table, e, index)

#define ppgtt_set_guest_root_entry(mm, e, index) \
	intel_vgpu_mm_set_entry(mm, mm->virtual_page_table, e, index)

#define ppgtt_get_shadow_root_entry(mm, e, index) \
	intel_vgpu_mm_get_entry(mm, mm->shadow_page_table, e, index)

#define ppgtt_set_shadow_root_entry(mm, e, index) \
	intel_vgpu_mm_set_entry(mm, mm->shadow_page_table, e, index)

extern struct intel_vgpu_mm *intel_vgpu_create_mm(struct intel_vgpu *vgpu,
		int mm_type, void *virtual_page_table, int page_table_level,
		u32 pde_base_index);
extern void intel_vgpu_destroy_mm(struct kref *mm_ref);

struct intel_vgpu_guest_page;

struct intel_vgpu_scratch_pt {
	struct page *page;
	unsigned long page_mfn;
};


struct intel_vgpu_gtt {
	struct intel_vgpu_mm *ggtt_mm;
	unsigned long active_ppgtt_mm_bitmap;
	struct list_head mm_list_head;
	DECLARE_HASHTABLE(shadow_page_hash_table, INTEL_GVT_GTT_HASH_BITS);
	DECLARE_HASHTABLE(guest_page_hash_table, INTEL_GVT_GTT_HASH_BITS);
	atomic_t n_write_protected_guest_page;
	struct list_head oos_page_list_head;
	struct list_head post_shadow_list_head;
	struct intel_vgpu_scratch_pt scratch_pt[GTT_TYPE_MAX];

};

extern int intel_vgpu_init_gtt(struct intel_vgpu *vgpu);
extern void intel_vgpu_clean_gtt(struct intel_vgpu *vgpu);

extern int intel_gvt_init_gtt(struct intel_gvt *gvt);
extern void intel_gvt_clean_gtt(struct intel_gvt *gvt);

extern struct intel_vgpu_mm *intel_gvt_find_ppgtt_mm(struct intel_vgpu *vgpu,
		int page_table_level, void *root_entry);

struct intel_vgpu_oos_page;

struct intel_vgpu_shadow_page {
	void *vaddr;
	struct page *page;
	int type;
	struct hlist_node node;
	unsigned long mfn;
};

struct intel_vgpu_guest_page {
	struct hlist_node node;
	bool writeprotection;
	unsigned long gfn;
	int (*handler)(void *, u64, void *, int);
	void *data;
	unsigned long write_cnt;
	struct intel_vgpu_oos_page *oos_page;
};

struct intel_vgpu_oos_page {
	struct intel_vgpu_guest_page *guest_page;
	struct list_head list;
	struct list_head vm_list;
	int id;
	unsigned char mem[GTT_PAGE_SIZE];
};

#define GTT_ENTRY_NUM_IN_ONE_PAGE 512

struct intel_vgpu_ppgtt_spt {
	struct intel_vgpu_shadow_page shadow_page;
	struct intel_vgpu_guest_page guest_page;
	int guest_page_type;
	atomic_t refcount;
	struct intel_vgpu *vgpu;
	DECLARE_BITMAP(post_shadow_bitmap, GTT_ENTRY_NUM_IN_ONE_PAGE);
	struct list_head post_shadow_list;
};

int intel_vgpu_init_guest_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_guest_page *guest_page,
		unsigned long gfn,
		int (*handler)(void *gp, u64, void *, int),
		void *data);

void intel_vgpu_clean_guest_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_guest_page *guest_page);

int intel_vgpu_set_guest_page_writeprotection(struct intel_vgpu *vgpu,
		struct intel_vgpu_guest_page *guest_page);

void intel_vgpu_clear_guest_page_writeprotection(struct intel_vgpu *vgpu,
		struct intel_vgpu_guest_page *guest_page);

struct intel_vgpu_guest_page *intel_vgpu_find_guest_page(
		struct intel_vgpu *vgpu, unsigned long gfn);

int intel_vgpu_sync_oos_pages(struct intel_vgpu *vgpu);

int intel_vgpu_flush_post_shadow(struct intel_vgpu *vgpu);

static inline void intel_gvt_mm_reference(struct intel_vgpu_mm *mm)
{
	kref_get(&mm->ref);
}

static inline void intel_gvt_mm_unreference(struct intel_vgpu_mm *mm)
{
	kref_put(&mm->ref, intel_vgpu_destroy_mm);
}

int intel_vgpu_pin_mm(struct intel_vgpu_mm *mm);

void intel_vgpu_unpin_mm(struct intel_vgpu_mm *mm);

unsigned long intel_vgpu_gma_to_gpa(struct intel_vgpu_mm *mm,
		unsigned long gma);

struct intel_vgpu_mm *intel_vgpu_find_ppgtt_mm(struct intel_vgpu *vgpu,
		int page_table_level, void *root_entry);

int intel_vgpu_g2v_create_ppgtt_mm(struct intel_vgpu *vgpu,
		int page_table_level);

int intel_vgpu_g2v_destroy_ppgtt_mm(struct intel_vgpu *vgpu,
		int page_table_level);

int intel_vgpu_emulate_gtt_mmio_read(struct intel_vgpu *vgpu,
	unsigned int off, void *p_data, unsigned int bytes);

int intel_vgpu_emulate_gtt_mmio_write(struct intel_vgpu *vgpu,
	unsigned int off, void *p_data, unsigned int bytes);

#endif /* _GVT_GTT_H_ */
