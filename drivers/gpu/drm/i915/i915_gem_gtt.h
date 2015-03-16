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

struct drm_i915_file_private;

typedef uint32_t gen6_pte_t;
typedef uint64_t gen8_pte_t;
typedef uint64_t gen8_pde_t;

#define gtt_total_entries(gtt) ((gtt).base.total >> PAGE_SHIFT)


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

#define GEN6_PTES			I915_PTES(sizeof(gen6_pte_t))
#define GEN6_PD_SIZE		        (I915_PDES * PAGE_SIZE)
#define GEN6_PD_ALIGN			(PAGE_SIZE * 16)
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
 */
#define GEN8_PDPE_SHIFT			30
#define GEN8_PDPE_MASK			0x3
#define GEN8_PDE_SHIFT			21
#define GEN8_PDE_MASK			0x1ff
#define GEN8_PTE_SHIFT			12
#define GEN8_PTE_MASK			0x1ff
#define GEN8_LEGACY_PDPES		4
#define GEN8_PTES			I915_PTES(sizeof(gen8_pte_t))

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

enum i915_ggtt_view_type {
	I915_GGTT_VIEW_NORMAL = 0,
};

struct i915_ggtt_view {
	enum i915_ggtt_view_type type;

	struct sg_table *pages;
};

extern const struct i915_ggtt_view i915_ggtt_view_normal;

enum i915_cache_level;

/**
 * A VMA represents a GEM BO that is bound into an address space. Therefore, a
 * VMA's presence cannot be guaranteed before binding, or after unbinding the
 * object into/from the address space.
 *
 * To make things as simple as possible (ie. no refcounting), a VMA's lifetime
 * will always be <= an objects lifetime. So object refcounting should cover us.
 */
struct i915_vma {
	struct drm_mm_node node;
	struct drm_i915_gem_object *obj;
	struct i915_address_space *vm;

	/** Flags and address space this VMA is bound to */
#define GLOBAL_BIND	(1<<0)
#define LOCAL_BIND	(1<<1)
#define PTE_READ_ONLY	(1<<2)
	unsigned int bound : 4;

	/**
	 * Support different GGTT views into the same object.
	 * This means there can be multiple VMA mappings per object and per VM.
	 * i915_ggtt_view_type is used to distinguish between those entries.
	 * The default one of zero (I915_GGTT_VIEW_NORMAL) is default and also
	 * assumed in GEM functions which take no ggtt view parameter.
	 */
	struct i915_ggtt_view ggtt_view;

	/** This object's place on the active/inactive lists */
	struct list_head mm_list;

	struct list_head vma_link; /* Link in the object's VMA list */

	/** This vma's place in the batchbuffer or on the eviction list */
	struct list_head exec_list;

	/**
	 * Used for performing relocations during execbuffer insertion.
	 */
	struct hlist_node exec_node;
	unsigned long exec_handle;
	struct drm_i915_gem_exec_object2 *exec_entry;

	/**
	 * How many users have pinned this object in GTT space. The following
	 * users can each hold at most one reference: pwrite/pread, execbuffer
	 * (objects are not allowed multiple times for the same batchbuffer),
	 * and the framebuffer code. When switching/pageflipping, the
	 * framebuffer code has at most two buffers pinned per crtc.
	 *
	 * In the worst case this is 1 + 1 + 1 + 2*2 = 7. That would fit into 3
	 * bits with absolutely no headroom. So use 4 bits. */
	unsigned int pin_count:4;
#define DRM_I915_GEM_OBJECT_MAX_PIN_COUNT 0xf

	/** Unmap an object from an address space. This usually consists of
	 * setting the valid PTE entries to a reserved scratch page. */
	void (*unbind_vma)(struct i915_vma *vma);
	/* Map an object into an address space with the given cache flags. */
	void (*bind_vma)(struct i915_vma *vma,
			 enum i915_cache_level cache_level,
			 u32 flags);
};

struct i915_page_table_entry {
	struct page *page;
	dma_addr_t daddr;
};

struct i915_page_directory_entry {
	struct page *page; /* NULL for GEN6-GEN7 */
	union {
		uint32_t pd_offset;
		dma_addr_t daddr;
	};

	struct i915_page_table_entry *page_table[I915_PDES]; /* PDEs */
};

struct i915_page_directory_pointer_entry {
	/* struct page *page; */
	struct i915_page_directory_entry *page_directory[GEN8_LEGACY_PDPES];
};

struct i915_address_space {
	struct drm_mm mm;
	struct drm_device *dev;
	struct list_head global_link;
	unsigned long start;		/* Start offset always 0 for dri2 */
	size_t total;		/* size addr space maps (ex. 2GB for ggtt) */

	struct {
		dma_addr_t addr;
		struct page *page;
	} scratch;

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

	/* FIXME: Need a more generic return type */
	gen6_pte_t (*pte_encode)(dma_addr_t addr,
				 enum i915_cache_level level,
				 bool valid, u32 flags); /* Create a valid PTE */
	void (*clear_range)(struct i915_address_space *vm,
			    uint64_t start,
			    uint64_t length,
			    bool use_scratch);
	void (*insert_entries)(struct i915_address_space *vm,
			       struct sg_table *st,
			       uint64_t start,
			       enum i915_cache_level cache_level, u32 flags);
	void (*cleanup)(struct i915_address_space *vm);
};

/* The Graphics Translation Table is the way in which GEN hardware translates a
 * Graphics Virtual Address into a Physical Address. In addition to the normal
 * collateral associated with any va->pa translations GEN hardware also has a
 * portion of the GTT which can be mapped by the CPU and remain both coherent
 * and correct (in cases like swizzling). That region is referred to as GMADR in
 * the spec.
 */
struct i915_gtt {
	struct i915_address_space base;
	size_t stolen_size;		/* Total size of stolen memory */

	unsigned long mappable_end;	/* End offset that we can CPU map */
	struct io_mapping *mappable;	/* Mapping to our CPU mappable region */
	phys_addr_t mappable_base;	/* PA of our GMADR */

	/** "Graphics Stolen Memory" holds the global PTEs */
	void __iomem *gsm;

	bool do_idle_maps;

	int mtrr;

	/* global gtt ops */
	int (*gtt_probe)(struct drm_device *dev, size_t *gtt_total,
			  size_t *stolen, phys_addr_t *mappable_base,
			  unsigned long *mappable_end);
};

struct i915_hw_ppgtt {
	struct i915_address_space base;
	struct kref ref;
	struct drm_mm_node node;
	unsigned num_pd_entries;
	unsigned num_pd_pages; /* gen8+ */
	union {
		struct i915_page_directory_pointer_entry pdp;
		struct i915_page_directory_entry pd;
	};

	struct drm_i915_file_private *file_priv;

	int (*enable)(struct i915_hw_ppgtt *ppgtt);
	int (*switch_mm)(struct i915_hw_ppgtt *ppgtt,
			 struct intel_engine_cs *ring);
	void (*debug_dump)(struct i915_hw_ppgtt *ppgtt, struct seq_file *m);
};

int i915_gem_gtt_init(struct drm_device *dev);
void i915_gem_init_global_gtt(struct drm_device *dev);
void i915_global_gtt_cleanup(struct drm_device *dev);


int i915_ppgtt_init(struct drm_device *dev, struct i915_hw_ppgtt *ppgtt);
int i915_ppgtt_init_hw(struct drm_device *dev);
void i915_ppgtt_release(struct kref *kref);
struct i915_hw_ppgtt *i915_ppgtt_create(struct drm_device *dev,
					struct drm_i915_file_private *fpriv);
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

void i915_check_and_clear_faults(struct drm_device *dev);
void i915_gem_suspend_gtt_mappings(struct drm_device *dev);
void i915_gem_restore_gtt_mappings(struct drm_device *dev);

int __must_check i915_gem_gtt_prepare_object(struct drm_i915_gem_object *obj);
void i915_gem_gtt_finish_object(struct drm_i915_gem_object *obj);

#endif
