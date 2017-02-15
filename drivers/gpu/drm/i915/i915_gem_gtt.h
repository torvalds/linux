/*
 * Copyright © 2014 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Please try to maintain the following order within this file unless it makes
 * sense to do otherwise. From top to bottom:
 * 1. typedefs
 * 2. #defines, and macros
 * 3. structure definitions
 * 4. function prototypes
 *
 * Within each section, please try to order by generation in ascending order,
 * from top to bottom (ie. gen6 on the top, gen8 on the bottom).
 */

#ifndef __I915_GEM_GTT_H__
#define __I915_GEM_GTT_H__

#include <linux/io-mapping.h>
#include <linux/mm.h>
#include <linux/pagevec.h>

#include "i915_gem_timeline.h"
#include "i915_gem_request.h"
#include "i915_selftest.h"

#define I915_GTT_PAGE_SIZE 4096UL
#define I915_GTT_MIN_ALIGNMENT I915_GTT_PAGE_SIZE

#define I915_FENCE_REG_NONE -1
#define I915_MAX_NUM_FENCES 32
/* 32 fences + sign bit for FENCE_REG_NONE */
#define I915_MAX_NUM_FENCE_BITS 6

struct drm_i915_file_private;
struct drm_i915_fence_reg;

typedef uint32_t gen6_pte_t;
typedef uint64_t gen8_pte_t;
typedef uint64_t gen8_pde_t;
typedef uint64_t gen8_ppgtt_pdpe_t;
typedef uint64_t gen8_ppgtt_pml4e_t;

#define ggtt_total_entries(ggtt) ((ggtt)->base.total >> PAGE_SHIFT)

/* gen6-hsw has bit 11-4 for physical addr bit 39-32 */
#define GEN6_GTT_ADDR_ENCODE(addr)	((addr) | (((addr) >> 28) & 0xff0))
#define GEN6_PTE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)
#define GEN6_PDE_ADDR_ENCODE(addr)	GEN6_GTT_ADDR_ENCODE(addr)
#define GEN6_PTE_CACHE_LLC		(2 << 1)
#define GEN6_PTE_UNCACHED		(1 << 1)
#define GEN6_PTE_VALID			(1 << 0)

#define I915_PTES(pte_len)		(PAGE_SIZE / (pte_len))
#define I915_PTE_MASK(pte_len)		(I915_PTES(pte_len) - 1)
#define I915_PDES			512
#define I915_PDE_MASK			(I915_PDES - 1)
#define NUM_PTE(pde_shift)     (1 << (pde_shift - PAGE_SHIFT))

#define GEN6_PTES			I915_PTES(sizeof(gen6_pte_t))
#define GEN6_PD_SIZE		        (I915_PDES * PAGE_SIZE)
#define GEN6_PD_ALIGN			(PAGE_SIZE * 16)
#define GEN6_PDE_SHIFT			22
#define GEN6_PDE_VALID			(1 << 0)

#define GEN7_PTE_CACHE_L3_LLC		(3 << 1)

#define BYT_PTE_SNOOPED_BY_CPU_CACHES	(1 << 2)
#define BYT_PTE_WRITEABLE		(1 << 1)

/* Cacheability Control is a 4-bit value. The low three bits are stored in bits
 * 3:1 of the PTE, while the fourth bit is stored in bit 11 of the PTE.
 */
#define HSW_CACHEABILITY_CONTROL(bits)	((((bits) & 0x7) << 1) | \
					 (((bits) & 0x8) << (11 - 3)))
#define HSW_WB_LLC_AGE3			HSW_CACHEABILITY_CONTROL(0x2)
#define HSW_WB_LLC_AGE0			HSW_CACHEABILITY_CONTROL(0x3)
#define HSW_WB_ELLC_LLC_AGE3		HSW_CACHEABILITY_CONTROL(0x8)
#define HSW_WB_ELLC_LLC_AGE0		HSW_CACHEABILITY_CONTROL(0xb)
#define HSW_WT_ELLC_LLC_AGE3		HSW_CACHEABILITY_CONTROL(0x7)
#define HSW_WT_ELLC_LLC_AGE0		HSW_CACHEABILITY_CONTROL(0x6)
#define HSW_PTE_UNCACHED		(0)
#define HSW_GTT_ADDR_ENCODE(addr)	((addr) | (((addr) >> 28) & 0x7f0))
#define HSW_PTE_ADDR_ENCODE(addr)	HSW_GTT_ADDR_ENCODE(addr)

/* GEN8 legacy style address is defined as a 3 level page table:
 * 31:30 | 29:21 | 20:12 |  11:0
 * PDPE  |  PDE  |  PTE  | offset
 * The difference as compared to normal x86 3 level page table is the PDPEs are
 * programmed via register.
 *
 * GEN8 48b legacy style address is defined as a 4 level page table:
 * 47:39 | 38:30 | 29:21 | 20:12 |  11:0
 * PML4E | PDPE  |  PDE  |  PTE  | offset
 */
#define GEN8_PML4ES_PER_PML4		512
#define GEN8_PML4E_SHIFT		39
#define GEN8_PML4E_MASK			(GEN8_PML4ES_PER_PML4 - 1)
#define GEN8_PDPE_SHIFT			30
/* NB: GEN8_PDPE_MASK is untrue for 32b platforms, but it has no impact on 32b page
 * tables */
#define GEN8_PDPE_MASK			0x1ff
#define GEN8_PDE_SHIFT			21
#define GEN8_PDE_MASK			0x1ff
#define GEN8_PTE_SHIFT			12
#define GEN8_PTE_MASK			0x1ff
#define GEN8_LEGACY_PDPES		4
#define GEN8_PTES			I915_PTES(sizeof(gen8_pte_t))

#define I915_PDPES_PER_PDP(dev_priv)	(USES_FULL_48BIT_PPGTT(dev_priv) ?\
					GEN8_PML4ES_PER_PML4 : GEN8_LEGACY_PDPES)

#define PPAT_UNCACHED_INDEX		(_PAGE_PWT | _PAGE_PCD)
#define PPAT_CACHED_PDE_INDEX		0 /* WB LLC */
#define PPAT_CACHED_INDEX		_PAGE_PAT /* WB LLCeLLC */
#define PPAT_DISPLAY_ELLC_INDEX		_PAGE_PCD /* WT eLLC */

#define CHV_PPAT_SNOOP			(1<<6)
#define GEN8_PPAT_AGE(x)		(x<<4)
#define GEN8_PPAT_LLCeLLC		(3<<2)
#define GEN8_PPAT_LLCELLC		(2<<2)
#define GEN8_PPAT_LLC			(1<<2)
#define GEN8_PPAT_WB			(3<<0)
#define GEN8_PPAT_WT			(2<<0)
#define GEN8_PPAT_WC			(1<<0)
#define GEN8_PPAT_UC			(0<<0)
#define GEN8_PPAT_ELLC_OVERRIDE		(0<<2)
#define GEN8_PPAT(i, x)			((uint64_t) (x) << ((i) * 8))

struct sg_table;

struct intel_rotation_info {
	struct intel_rotation_plane_info {
		/* tiles */
		unsigned int width, height, stride, offset;
	} plane[2];
} __packed;

static inline void assert_intel_rotation_info_is_packed(void)
{
	BUILD_BUG_ON(sizeof(struct intel_rotation_info) != 8*sizeof(unsigned int));
}

struct intel_partial_info {
	u64 offset;
	unsigned int size;
} __packed;

static inline void assert_intel_partial_info_is_packed(void)
{
	BUILD_BUG_ON(sizeof(struct intel_partial_info) != sizeof(u64) + sizeof(unsigned int));
}

enum i915_ggtt_view_type {
	I915_GGTT_VIEW_NORMAL = 0,
	I915_GGTT_VIEW_ROTATED = sizeof(struct intel_rotation_info),
	I915_GGTT_VIEW_PARTIAL = sizeof(struct intel_partial_info),
};

static inline void assert_i915_ggtt_view_type_is_unique(void)
{
	/* As we encode the size of each branch inside the union into its type,
	 * we have to be careful that each branch has a unique size.
	 */
	switch ((enum i915_ggtt_view_type)0) {
	case I915_GGTT_VIEW_NORMAL:
	case I915_GGTT_VIEW_PARTIAL:
	case I915_GGTT_VIEW_ROTATED:
		/* gcc complains if these are identical cases */
		break;
	}
}

struct i915_ggtt_view {
	enum i915_ggtt_view_type type;
	union {
		/* Members need to contain no holes/padding */
		struct intel_partial_info partial;
		struct intel_rotation_info rotated;
	};
};

enum i915_cache_level;

struct i915_vma;

struct i915_page_dma {
	struct page *page;
	union {
		dma_addr_t daddr;

		/* For gen6/gen7 only. This is the offset in the GGTT
		 * where the page directory entries for PPGTT begin
		 */
		uint32_t ggtt_offset;
	};
};

#define px_base(px) (&(px)->base)
#define px_page(px) (px_base(px)->page)
#define px_dma(px) (px_base(px)->daddr)

struct i915_page_table {
	struct i915_page_dma base;

	unsigned long *used_ptes;
};

struct i915_page_directory {
	struct i915_page_dma base;

	unsigned long *used_pdes;
	struct i915_page_table *page_table[I915_PDES]; /* PDEs */
};

struct i915_page_directory_pointer {
	struct i915_page_dma base;

	unsigned long *used_pdpes;
	struct i915_page_directory **page_directory;
};

struct i915_pml4 {
	struct i915_page_dma base;

	DECLARE_BITMAP(used_pml4es, GEN8_PML4ES_PER_PML4);
	struct i915_page_directory_pointer *pdps[GEN8_PML4ES_PER_PML4];
};

struct i915_address_space {
	struct drm_mm mm;
	struct i915_gem_timeline timeline;
	struct drm_i915_private *i915;
	struct device *dma;
	/* Every address space belongs to a struct file - except for the global
	 * GTT that is owned by the driver (and so @file is set to NULL). In
	 * principle, no information should leak from one context to another
	 * (or between files/processes etc) unless explicitly shared by the
	 * owner. Tracking the owner is important in order to free up per-file
	 * objects along with the file, to aide resource tracking, and to
	 * assign blame.
	 */
	struct drm_i915_file_private *file;
	struct list_head global_link;
	u64 start;		/* Start offset always 0 for dri2 */
	u64 total;		/* size addr space maps (ex. 2GB for ggtt) */

	bool closed;

	struct i915_page_dma scratch_page;
	struct i915_page_table *scratch_pt;
	struct i915_page_directory *scratch_pd;
	struct i915_page_directory_pointer *scratch_pdp; /* GEN8+ & 48b PPGTT */

	/**
	 * List of objects currently involved in rendering.
	 *
	 * Includes buffers having the contents of their GPU caches
	 * flushed, not necessarily primitives. last_read_req
	 * represents when the rendering involved will be completed.
	 *
	 * A reference is held on the buffer while on this list.
	 */
	struct list_head active_list;

	/**
	 * LRU list of objects which are not in the ringbuffer and
	 * are ready to unbind, but are still in the GTT.
	 *
	 * last_read_req is NULL while an object is in this list.
	 *
	 * A reference is not held on the buffer while on this list,
	 * as merely being GTT-bound shouldn't prevent its being
	 * freed, and we'll pull it off the list in the free path.
	 */
	struct list_head inactive_list;

	/**
	 * List of vma that have been unbound.
	 *
	 * A reference is not held on the buffer while on this list.
	 */
	struct list_head unbound_list;

	struct pagevec free_pages;
	bool pt_kmap_wc;

	/* FIXME: Need a more generic return type */
	gen6_pte_t (*pte_encode)(dma_addr_t addr,
				 enum i915_cache_level level,
				 u32 flags); /* Create a valid PTE */
	/* flags for pte_encode */
#define PTE_READ_ONLY	(1<<0)
	int (*allocate_va_range)(struct i915_address_space *vm,
				 uint64_t start,
				 uint64_t length);
	void (*clear_range)(struct i915_address_space *vm,
			    uint64_t start,
			    uint64_t length);
	void (*insert_page)(struct i915_address_space *vm,
			    dma_addr_t addr,
			    uint64_t offset,
			    enum i915_cache_level cache_level,
			    u32 flags);
	void (*insert_entries)(struct i915_address_space *vm,
			       struct sg_table *st,
			       uint64_t start,
			       enum i915_cache_level cache_level, u32 flags);
	void (*cleanup)(struct i915_address_space *vm);
	/** Unmap an object from an address space. This usually consists of
	 * setting the valid PTE entries to a reserved scratch page. */
	void (*unbind_vma)(struct i915_vma *vma);
	/* Map an object into an address space with the given cache flags. */
	int (*bind_vma)(struct i915_vma *vma,
			enum i915_cache_level cache_level,
			u32 flags);

	I915_SELFTEST_DECLARE(struct fault_attr fault_attr);
};

#define i915_is_ggtt(V) (!(V)->file)

/* The Graphics Translation Table is the way in which GEN hardware translates a
 * Graphics Virtual Address into a Physical Address. In addition to the normal
 * collateral associated with any va->pa translations GEN hardware also has a
 * portion of the GTT which can be mapped by the CPU and remain both coherent
 * and correct (in cases like swizzling). That region is referred to as GMADR in
 * the spec.
 */
struct i915_ggtt {
	struct i915_address_space base;
	struct io_mapping mappable;	/* Mapping to our CPU mappable region */

	phys_addr_t mappable_base;	/* PA of our GMADR */
	u64 mappable_end;		/* End offset that we can CPU map */

	/* Stolen memory is segmented in hardware with different portions
	 * offlimits to certain functions.
	 *
	 * The drm_mm is initialised to the total accessible range, as found
	 * from the PCI config. On Broadwell+, this is further restricted to
	 * avoid the first page! The upper end of stolen memory is reserved for
	 * hardware functions and similarly removed from the accessible range.
	 */
	u32 stolen_size;		/* Total size of stolen memory */
	u32 stolen_usable_size;	/* Total size minus reserved ranges */
	u32 stolen_reserved_base;
	u32 stolen_reserved_size;

	/** "Graphics Stolen Memory" holds the global PTEs */
	void __iomem *gsm;
	void (*invalidate)(struct drm_i915_private *dev_priv);

	bool do_idle_maps;

	int mtrr;

	struct drm_mm_node error_capture;
};

struct i915_hw_ppgtt {
	struct i915_address_space base;
	struct kref ref;
	struct drm_mm_node node;
	unsigned long pd_dirty_rings;
	union {
		struct i915_pml4 pml4;		/* GEN8+ & 48b PPGTT */
		struct i915_page_directory_pointer pdp;	/* GEN8+ */
		struct i915_page_directory pd;		/* GEN6-7 */
	};

	gen6_pte_t __iomem *pd_addr;

	int (*enable)(struct i915_hw_ppgtt *ppgtt);
	int (*switch_mm)(struct i915_hw_ppgtt *ppgtt,
			 struct drm_i915_gem_request *req);
	void (*debug_dump)(struct i915_hw_ppgtt *ppgtt, struct seq_file *m);
};

/*
 * gen6_for_each_pde() iterates over every pde from start until start+length.
 * If start and start+length are not perfectly divisible, the macro will round
 * down and up as needed. Start=0 and length=2G effectively iterates over
 * every PDE in the system. The macro modifies ALL its parameters except 'pd',
 * so each of the other parameters should preferably be a simple variable, or
 * at most an lvalue with no side-effects!
 */
#define gen6_for_each_pde(pt, pd, start, length, iter)			\
	for (iter = gen6_pde_index(start);				\
	     length > 0 && iter < I915_PDES &&				\
		(pt = (pd)->page_table[iter], true);			\
	     ({ u32 temp = ALIGN(start+1, 1 << GEN6_PDE_SHIFT);		\
		    temp = min(temp - start, length);			\
		    start += temp, length -= temp; }), ++iter)

#define gen6_for_all_pdes(pt, pd, iter)					\
	for (iter = 0;							\
	     iter < I915_PDES &&					\
		(pt = (pd)->page_table[iter], true);			\
	     ++iter)

static inline uint32_t i915_pte_index(uint64_t address, uint32_t pde_shift)
{
	const uint32_t mask = NUM_PTE(pde_shift) - 1;

	return (address >> PAGE_SHIFT) & mask;
}

/* Helper to counts the number of PTEs within the given length. This count
 * does not cross a page table boundary, so the max value would be
 * GEN6_PTES for GEN6, and GEN8_PTES for GEN8.
*/
static inline uint32_t i915_pte_count(uint64_t addr, size_t length,
				      uint32_t pde_shift)
{
	const uint64_t mask = ~((1ULL << pde_shift) - 1);
	uint64_t end;

	WARN_ON(length == 0);
	WARN_ON(offset_in_page(addr|length));

	end = addr + length;

	if ((addr & mask) != (end & mask))
		return NUM_PTE(pde_shift) - i915_pte_index(addr, pde_shift);

	return i915_pte_index(end, pde_shift) - i915_pte_index(addr, pde_shift);
}

static inline uint32_t i915_pde_index(uint64_t addr, uint32_t shift)
{
	return (addr >> shift) & I915_PDE_MASK;
}

static inline uint32_t gen6_pte_index(uint32_t addr)
{
	return i915_pte_index(addr, GEN6_PDE_SHIFT);
}

static inline size_t gen6_pte_count(uint32_t addr, uint32_t length)
{
	return i915_pte_count(addr, length, GEN6_PDE_SHIFT);
}

static inline uint32_t gen6_pde_index(uint32_t addr)
{
	return i915_pde_index(addr, GEN6_PDE_SHIFT);
}

/* Equivalent to the gen6 version, For each pde iterates over every pde
 * between from start until start + length. On gen8+ it simply iterates
 * over every page directory entry in a page directory.
 */
#define gen8_for_each_pde(pt, pd, start, length, iter)			\
	for (iter = gen8_pde_index(start);				\
	     length > 0 && iter < I915_PDES &&				\
		(pt = (pd)->page_table[iter], true);			\
	     ({ u64 temp = ALIGN(start+1, 1 << GEN8_PDE_SHIFT);		\
		    temp = min(temp - start, length);			\
		    start += temp, length -= temp; }), ++iter)

#define gen8_for_each_pdpe(pd, pdp, start, length, iter)		\
	for (iter = gen8_pdpe_index(start);				\
	     length > 0 && iter < I915_PDPES_PER_PDP(dev) &&		\
		(pd = (pdp)->page_directory[iter], true);		\
	     ({ u64 temp = ALIGN(start+1, 1 << GEN8_PDPE_SHIFT);	\
		    temp = min(temp - start, length);			\
		    start += temp, length -= temp; }), ++iter)

#define gen8_for_each_pml4e(pdp, pml4, start, length, iter)		\
	for (iter = gen8_pml4e_index(start);				\
	     length > 0 && iter < GEN8_PML4ES_PER_PML4 &&		\
		(pdp = (pml4)->pdps[iter], true);			\
	     ({ u64 temp = ALIGN(start+1, 1ULL << GEN8_PML4E_SHIFT);	\
		    temp = min(temp - start, length);			\
		    start += temp, length -= temp; }), ++iter)

static inline uint32_t gen8_pte_index(uint64_t address)
{
	return i915_pte_index(address, GEN8_PDE_SHIFT);
}

static inline uint32_t gen8_pde_index(uint64_t address)
{
	return i915_pde_index(address, GEN8_PDE_SHIFT);
}

static inline uint32_t gen8_pdpe_index(uint64_t address)
{
	return (address >> GEN8_PDPE_SHIFT) & GEN8_PDPE_MASK;
}

static inline uint32_t gen8_pml4e_index(uint64_t address)
{
	return (address >> GEN8_PML4E_SHIFT) & GEN8_PML4E_MASK;
}

static inline size_t gen8_pte_count(uint64_t address, uint64_t length)
{
	return i915_pte_count(address, length, GEN8_PDE_SHIFT);
}

static inline dma_addr_t
i915_page_dir_dma_addr(const struct i915_hw_ppgtt *ppgtt, const unsigned n)
{
	return test_bit(n, ppgtt->pdp.used_pdpes) ?
		px_dma(ppgtt->pdp.page_directory[n]) :
		px_dma(ppgtt->base.scratch_pd);
}

static inline struct i915_ggtt *
i915_vm_to_ggtt(struct i915_address_space *vm)
{
	GEM_BUG_ON(!i915_is_ggtt(vm));
	return container_of(vm, struct i915_ggtt, base);
}

static inline bool
i915_vm_is_48bit(const struct i915_address_space *vm)
{
	return (vm->total - 1) >> 32;
}

int i915_gem_init_aliasing_ppgtt(struct drm_i915_private *i915);
void i915_gem_fini_aliasing_ppgtt(struct drm_i915_private *i915);

int i915_ggtt_probe_hw(struct drm_i915_private *dev_priv);
int i915_ggtt_init_hw(struct drm_i915_private *dev_priv);
int i915_ggtt_enable_hw(struct drm_i915_private *dev_priv);
void i915_ggtt_enable_guc(struct drm_i915_private *i915);
void i915_ggtt_disable_guc(struct drm_i915_private *i915);
int i915_gem_init_ggtt(struct drm_i915_private *dev_priv);
void i915_ggtt_cleanup_hw(struct drm_i915_private *dev_priv);

int i915_ppgtt_init_hw(struct drm_i915_private *dev_priv);
void i915_ppgtt_release(struct kref *kref);
struct i915_hw_ppgtt *i915_ppgtt_create(struct drm_i915_private *dev_priv,
					struct drm_i915_file_private *fpriv,
					const char *name);
void i915_ppgtt_close(struct i915_address_space *vm);
static inline void i915_ppgtt_get(struct i915_hw_ppgtt *ppgtt)
{
	if (ppgtt)
		kref_get(&ppgtt->ref);
}
static inline void i915_ppgtt_put(struct i915_hw_ppgtt *ppgtt)
{
	if (ppgtt)
		kref_put(&ppgtt->ref, i915_ppgtt_release);
}

void i915_check_and_clear_faults(struct drm_i915_private *dev_priv);
void i915_gem_suspend_gtt_mappings(struct drm_i915_private *dev_priv);
void i915_gem_restore_gtt_mappings(struct drm_i915_private *dev_priv);

int __must_check i915_gem_gtt_prepare_pages(struct drm_i915_gem_object *obj,
					    struct sg_table *pages);
void i915_gem_gtt_finish_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *pages);

int i915_gem_gtt_reserve(struct i915_address_space *vm,
			 struct drm_mm_node *node,
			 u64 size, u64 offset, unsigned long color,
			 unsigned int flags);

int i915_gem_gtt_insert(struct i915_address_space *vm,
			struct drm_mm_node *node,
			u64 size, u64 alignment, unsigned long color,
			u64 start, u64 end, unsigned int flags);

/* Flags used by pin/bind&friends. */
#define PIN_NONBLOCK		BIT(0)
#define PIN_MAPPABLE		BIT(1)
#define PIN_ZONE_4G		BIT(2)
#define PIN_NONFAULT		BIT(3)

#define PIN_MBZ			BIT(5) /* I915_VMA_PIN_OVERFLOW */
#define PIN_GLOBAL		BIT(6) /* I915_VMA_GLOBAL_BIND */
#define PIN_USER		BIT(7) /* I915_VMA_LOCAL_BIND */
#define PIN_UPDATE		BIT(8)

#define PIN_HIGH		BIT(9)
#define PIN_OFFSET_BIAS		BIT(10)
#define PIN_OFFSET_FIXED	BIT(11)
#define PIN_OFFSET_MASK		(-I915_GTT_PAGE_SIZE)

#endif
