/*
 * GTT virtualization
 *
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

#include "i915_drv.h"
#include "gvt.h"
#include "i915_pvinfo.h"
#include "trace.h"

#include "gt/intel_gt_regs.h"
#include <linux/vmalloc.h>

#if defined(VERBOSE_DEBUG)
#define gvt_vdbg_mm(fmt, args...) gvt_dbg_mm(fmt, ##args)
#else
#define gvt_vdbg_mm(fmt, args...)
#endif

static bool enable_out_of_sync = false;
static int preallocated_oos_pages = 8192;

/*
 * validate a gm address and related range size,
 * translate it to host gm address
 */
bool intel_gvt_ggtt_validate_range(struct intel_vgpu *vgpu, u64 addr, u32 size)
{
	if (size == 0)
		return vgpu_gmadr_is_valid(vgpu, addr);

	if (vgpu_gmadr_is_aperture(vgpu, addr) &&
	    vgpu_gmadr_is_aperture(vgpu, addr + size - 1))
		return true;
	else if (vgpu_gmadr_is_hidden(vgpu, addr) &&
		 vgpu_gmadr_is_hidden(vgpu, addr + size - 1))
		return true;

	gvt_dbg_mm("Invalid ggtt range at 0x%llx, size: 0x%x\n",
		     addr, size);
	return false;
}

/* translate a guest gmadr to host gmadr */
int intel_gvt_ggtt_gmadr_g2h(struct intel_vgpu *vgpu, u64 g_addr, u64 *h_addr)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;

	if (drm_WARN(&i915->drm, !vgpu_gmadr_is_valid(vgpu, g_addr),
		     "invalid guest gmadr %llx\n", g_addr))
		return -EACCES;

	if (vgpu_gmadr_is_aperture(vgpu, g_addr))
		*h_addr = vgpu_aperture_gmadr_base(vgpu)
			  + (g_addr - vgpu_aperture_offset(vgpu));
	else
		*h_addr = vgpu_hidden_gmadr_base(vgpu)
			  + (g_addr - vgpu_hidden_offset(vgpu));
	return 0;
}

/* translate a host gmadr to guest gmadr */
int intel_gvt_ggtt_gmadr_h2g(struct intel_vgpu *vgpu, u64 h_addr, u64 *g_addr)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;

	if (drm_WARN(&i915->drm, !gvt_gmadr_is_valid(vgpu->gvt, h_addr),
		     "invalid host gmadr %llx\n", h_addr))
		return -EACCES;

	if (gvt_gmadr_is_aperture(vgpu->gvt, h_addr))
		*g_addr = vgpu_aperture_gmadr_base(vgpu)
			+ (h_addr - gvt_aperture_gmadr_base(vgpu->gvt));
	else
		*g_addr = vgpu_hidden_gmadr_base(vgpu)
			+ (h_addr - gvt_hidden_gmadr_base(vgpu->gvt));
	return 0;
}

int intel_gvt_ggtt_index_g2h(struct intel_vgpu *vgpu, unsigned long g_index,
			     unsigned long *h_index)
{
	u64 h_addr;
	int ret;

	ret = intel_gvt_ggtt_gmadr_g2h(vgpu, g_index << I915_GTT_PAGE_SHIFT,
				       &h_addr);
	if (ret)
		return ret;

	*h_index = h_addr >> I915_GTT_PAGE_SHIFT;
	return 0;
}

int intel_gvt_ggtt_h2g_index(struct intel_vgpu *vgpu, unsigned long h_index,
			     unsigned long *g_index)
{
	u64 g_addr;
	int ret;

	ret = intel_gvt_ggtt_gmadr_h2g(vgpu, h_index << I915_GTT_PAGE_SHIFT,
				       &g_addr);
	if (ret)
		return ret;

	*g_index = g_addr >> I915_GTT_PAGE_SHIFT;
	return 0;
}

#define gtt_type_is_entry(type) \
	(type > GTT_TYPE_INVALID && type < GTT_TYPE_PPGTT_ENTRY \
	 && type != GTT_TYPE_PPGTT_PTE_ENTRY \
	 && type != GTT_TYPE_PPGTT_ROOT_ENTRY)

#define gtt_type_is_pt(type) \
	(type >= GTT_TYPE_PPGTT_PTE_PT && type < GTT_TYPE_MAX)

#define gtt_type_is_pte_pt(type) \
	(type == GTT_TYPE_PPGTT_PTE_PT)

#define gtt_type_is_root_pointer(type) \
	(gtt_type_is_entry(type) && type > GTT_TYPE_PPGTT_ROOT_ENTRY)

#define gtt_init_entry(e, t, p, v) do { \
	(e)->type = t; \
	(e)->pdev = p; \
	memcpy(&(e)->val64, &v, sizeof(v)); \
} while (0)

/*
 * Mappings between GTT_TYPE* enumerations.
 * Following information can be found according to the given type:
 * - type of next level page table
 * - type of entry inside this level page table
 * - type of entry with PSE set
 *
 * If the given type doesn't have such a kind of information,
 * e.g. give a l4 root entry type, then request to get its PSE type,
 * give a PTE page table type, then request to get its next level page
 * table type, as we know l4 root entry doesn't have a PSE bit,
 * and a PTE page table doesn't have a next level page table type,
 * GTT_TYPE_INVALID will be returned. This is useful when traversing a
 * page table.
 */

struct gtt_type_table_entry {
	int entry_type;
	int pt_type;
	int next_pt_type;
	int pse_entry_type;
};

#define GTT_TYPE_TABLE_ENTRY(type, e_type, cpt_type, npt_type, pse_type) \
	[type] = { \
		.entry_type = e_type, \
		.pt_type = cpt_type, \
		.next_pt_type = npt_type, \
		.pse_entry_type = pse_type, \
	}

static const struct gtt_type_table_entry gtt_type_table[] = {
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_ROOT_L4_ENTRY,
			GTT_TYPE_PPGTT_ROOT_L4_ENTRY,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PML4_PT,
			GTT_TYPE_INVALID),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PML4_PT,
			GTT_TYPE_PPGTT_PML4_ENTRY,
			GTT_TYPE_PPGTT_PML4_PT,
			GTT_TYPE_PPGTT_PDP_PT,
			GTT_TYPE_INVALID),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PML4_ENTRY,
			GTT_TYPE_PPGTT_PML4_ENTRY,
			GTT_TYPE_PPGTT_PML4_PT,
			GTT_TYPE_PPGTT_PDP_PT,
			GTT_TYPE_INVALID),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PDP_PT,
			GTT_TYPE_PPGTT_PDP_ENTRY,
			GTT_TYPE_PPGTT_PDP_PT,
			GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_PPGTT_PTE_1G_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_ROOT_L3_ENTRY,
			GTT_TYPE_PPGTT_ROOT_L3_ENTRY,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_PPGTT_PTE_1G_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PDP_ENTRY,
			GTT_TYPE_PPGTT_PDP_ENTRY,
			GTT_TYPE_PPGTT_PDP_PT,
			GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_PPGTT_PTE_1G_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_PPGTT_PDE_ENTRY,
			GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_PPGTT_PTE_PT,
			GTT_TYPE_PPGTT_PTE_2M_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PDE_ENTRY,
			GTT_TYPE_PPGTT_PDE_ENTRY,
			GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_PPGTT_PTE_PT,
			GTT_TYPE_PPGTT_PTE_2M_ENTRY),
	/* We take IPS bit as 'PSE' for PTE level. */
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PTE_PT,
			GTT_TYPE_PPGTT_PTE_4K_ENTRY,
			GTT_TYPE_PPGTT_PTE_PT,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PTE_64K_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PTE_4K_ENTRY,
			GTT_TYPE_PPGTT_PTE_4K_ENTRY,
			GTT_TYPE_PPGTT_PTE_PT,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PTE_64K_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PTE_64K_ENTRY,
			GTT_TYPE_PPGTT_PTE_4K_ENTRY,
			GTT_TYPE_PPGTT_PTE_PT,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PTE_64K_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PTE_2M_ENTRY,
			GTT_TYPE_PPGTT_PDE_ENTRY,
			GTT_TYPE_PPGTT_PDE_PT,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PTE_2M_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_PPGTT_PTE_1G_ENTRY,
			GTT_TYPE_PPGTT_PDP_ENTRY,
			GTT_TYPE_PPGTT_PDP_PT,
			GTT_TYPE_INVALID,
			GTT_TYPE_PPGTT_PTE_1G_ENTRY),
	GTT_TYPE_TABLE_ENTRY(GTT_TYPE_GGTT_PTE,
			GTT_TYPE_GGTT_PTE,
			GTT_TYPE_INVALID,
			GTT_TYPE_INVALID,
			GTT_TYPE_INVALID),
};

static inline int get_next_pt_type(int type)
{
	return gtt_type_table[type].next_pt_type;
}

static inline int get_entry_type(int type)
{
	return gtt_type_table[type].entry_type;
}

static inline int get_pse_type(int type)
{
	return gtt_type_table[type].pse_entry_type;
}

static u64 read_pte64(struct i915_ggtt *ggtt, unsigned long index)
{
	void __iomem *addr = (gen8_pte_t __iomem *)ggtt->gsm + index;

	return readq(addr);
}

static void ggtt_invalidate(struct intel_gt *gt)
{
	mmio_hw_access_pre(gt);
	intel_uncore_write(gt->uncore, GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);
	mmio_hw_access_post(gt);
}

static void write_pte64(struct i915_ggtt *ggtt, unsigned long index, u64 pte)
{
	void __iomem *addr = (gen8_pte_t __iomem *)ggtt->gsm + index;

	writeq(pte, addr);
}

static inline int gtt_get_entry64(void *pt,
		struct intel_gvt_gtt_entry *e,
		unsigned long index, bool hypervisor_access, unsigned long gpa,
		struct intel_vgpu *vgpu)
{
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	int ret;

	if (WARN_ON(info->gtt_entry_size != 8))
		return -EINVAL;

	if (hypervisor_access) {
		ret = intel_gvt_read_gpa(vgpu, gpa +
				(index << info->gtt_entry_size_shift),
				&e->val64, 8);
		if (WARN_ON(ret))
			return ret;
	} else if (!pt) {
		e->val64 = read_pte64(vgpu->gvt->gt->ggtt, index);
	} else {
		e->val64 = *((u64 *)pt + index);
	}
	return 0;
}

static inline int gtt_set_entry64(void *pt,
		struct intel_gvt_gtt_entry *e,
		unsigned long index, bool hypervisor_access, unsigned long gpa,
		struct intel_vgpu *vgpu)
{
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	int ret;

	if (WARN_ON(info->gtt_entry_size != 8))
		return -EINVAL;

	if (hypervisor_access) {
		ret = intel_gvt_write_gpa(vgpu, gpa +
				(index << info->gtt_entry_size_shift),
				&e->val64, 8);
		if (WARN_ON(ret))
			return ret;
	} else if (!pt) {
		write_pte64(vgpu->gvt->gt->ggtt, index, e->val64);
	} else {
		*((u64 *)pt + index) = e->val64;
	}
	return 0;
}

#define GTT_HAW 46

#define ADDR_1G_MASK	GENMASK_ULL(GTT_HAW - 1, 30)
#define ADDR_2M_MASK	GENMASK_ULL(GTT_HAW - 1, 21)
#define ADDR_64K_MASK	GENMASK_ULL(GTT_HAW - 1, 16)
#define ADDR_4K_MASK	GENMASK_ULL(GTT_HAW - 1, 12)

#define GTT_SPTE_FLAG_MASK GENMASK_ULL(62, 52)
#define GTT_SPTE_FLAG_64K_SPLITED BIT(52) /* splited 64K gtt entry */

#define GTT_64K_PTE_STRIDE 16

static unsigned long gen8_gtt_get_pfn(struct intel_gvt_gtt_entry *e)
{
	unsigned long pfn;

	if (e->type == GTT_TYPE_PPGTT_PTE_1G_ENTRY)
		pfn = (e->val64 & ADDR_1G_MASK) >> PAGE_SHIFT;
	else if (e->type == GTT_TYPE_PPGTT_PTE_2M_ENTRY)
		pfn = (e->val64 & ADDR_2M_MASK) >> PAGE_SHIFT;
	else if (e->type == GTT_TYPE_PPGTT_PTE_64K_ENTRY)
		pfn = (e->val64 & ADDR_64K_MASK) >> PAGE_SHIFT;
	else
		pfn = (e->val64 & ADDR_4K_MASK) >> PAGE_SHIFT;
	return pfn;
}

static void gen8_gtt_set_pfn(struct intel_gvt_gtt_entry *e, unsigned long pfn)
{
	if (e->type == GTT_TYPE_PPGTT_PTE_1G_ENTRY) {
		e->val64 &= ~ADDR_1G_MASK;
		pfn &= (ADDR_1G_MASK >> PAGE_SHIFT);
	} else if (e->type == GTT_TYPE_PPGTT_PTE_2M_ENTRY) {
		e->val64 &= ~ADDR_2M_MASK;
		pfn &= (ADDR_2M_MASK >> PAGE_SHIFT);
	} else if (e->type == GTT_TYPE_PPGTT_PTE_64K_ENTRY) {
		e->val64 &= ~ADDR_64K_MASK;
		pfn &= (ADDR_64K_MASK >> PAGE_SHIFT);
	} else {
		e->val64 &= ~ADDR_4K_MASK;
		pfn &= (ADDR_4K_MASK >> PAGE_SHIFT);
	}

	e->val64 |= (pfn << PAGE_SHIFT);
}

static bool gen8_gtt_test_pse(struct intel_gvt_gtt_entry *e)
{
	return !!(e->val64 & _PAGE_PSE);
}

static void gen8_gtt_clear_pse(struct intel_gvt_gtt_entry *e)
{
	if (gen8_gtt_test_pse(e)) {
		switch (e->type) {
		case GTT_TYPE_PPGTT_PTE_2M_ENTRY:
			e->val64 &= ~_PAGE_PSE;
			e->type = GTT_TYPE_PPGTT_PDE_ENTRY;
			break;
		case GTT_TYPE_PPGTT_PTE_1G_ENTRY:
			e->type = GTT_TYPE_PPGTT_PDP_ENTRY;
			e->val64 &= ~_PAGE_PSE;
			break;
		default:
			WARN_ON(1);
		}
	}
}

static bool gen8_gtt_test_ips(struct intel_gvt_gtt_entry *e)
{
	if (GEM_WARN_ON(e->type != GTT_TYPE_PPGTT_PDE_ENTRY))
		return false;

	return !!(e->val64 & GEN8_PDE_IPS_64K);
}

static void gen8_gtt_clear_ips(struct intel_gvt_gtt_entry *e)
{
	if (GEM_WARN_ON(e->type != GTT_TYPE_PPGTT_PDE_ENTRY))
		return;

	e->val64 &= ~GEN8_PDE_IPS_64K;
}

static bool gen8_gtt_test_present(struct intel_gvt_gtt_entry *e)
{
	/*
	 * i915 writes PDP root pointer registers without present bit,
	 * it also works, so we need to treat root pointer entry
	 * specifically.
	 */
	if (e->type == GTT_TYPE_PPGTT_ROOT_L3_ENTRY
			|| e->type == GTT_TYPE_PPGTT_ROOT_L4_ENTRY)
		return (e->val64 != 0);
	else
		return (e->val64 & GEN8_PAGE_PRESENT);
}

static void gtt_entry_clear_present(struct intel_gvt_gtt_entry *e)
{
	e->val64 &= ~GEN8_PAGE_PRESENT;
}

static void gtt_entry_set_present(struct intel_gvt_gtt_entry *e)
{
	e->val64 |= GEN8_PAGE_PRESENT;
}

static bool gen8_gtt_test_64k_splited(struct intel_gvt_gtt_entry *e)
{
	return !!(e->val64 & GTT_SPTE_FLAG_64K_SPLITED);
}

static void gen8_gtt_set_64k_splited(struct intel_gvt_gtt_entry *e)
{
	e->val64 |= GTT_SPTE_FLAG_64K_SPLITED;
}

static void gen8_gtt_clear_64k_splited(struct intel_gvt_gtt_entry *e)
{
	e->val64 &= ~GTT_SPTE_FLAG_64K_SPLITED;
}

/*
 * Per-platform GMA routines.
 */
static unsigned long gma_to_ggtt_pte_index(unsigned long gma)
{
	unsigned long x = (gma >> I915_GTT_PAGE_SHIFT);

	trace_gma_index(__func__, gma, x);
	return x;
}

#define DEFINE_PPGTT_GMA_TO_INDEX(prefix, ename, exp) \
static unsigned long prefix##_gma_to_##ename##_index(unsigned long gma) \
{ \
	unsigned long x = (exp); \
	trace_gma_index(__func__, gma, x); \
	return x; \
}

DEFINE_PPGTT_GMA_TO_INDEX(gen8, pte, (gma >> 12 & 0x1ff));
DEFINE_PPGTT_GMA_TO_INDEX(gen8, pde, (gma >> 21 & 0x1ff));
DEFINE_PPGTT_GMA_TO_INDEX(gen8, l3_pdp, (gma >> 30 & 0x3));
DEFINE_PPGTT_GMA_TO_INDEX(gen8, l4_pdp, (gma >> 30 & 0x1ff));
DEFINE_PPGTT_GMA_TO_INDEX(gen8, pml4, (gma >> 39 & 0x1ff));

static const struct intel_gvt_gtt_pte_ops gen8_gtt_pte_ops = {
	.get_entry = gtt_get_entry64,
	.set_entry = gtt_set_entry64,
	.clear_present = gtt_entry_clear_present,
	.set_present = gtt_entry_set_present,
	.test_present = gen8_gtt_test_present,
	.test_pse = gen8_gtt_test_pse,
	.clear_pse = gen8_gtt_clear_pse,
	.clear_ips = gen8_gtt_clear_ips,
	.test_ips = gen8_gtt_test_ips,
	.clear_64k_splited = gen8_gtt_clear_64k_splited,
	.set_64k_splited = gen8_gtt_set_64k_splited,
	.test_64k_splited = gen8_gtt_test_64k_splited,
	.get_pfn = gen8_gtt_get_pfn,
	.set_pfn = gen8_gtt_set_pfn,
};

static const struct intel_gvt_gtt_gma_ops gen8_gtt_gma_ops = {
	.gma_to_ggtt_pte_index = gma_to_ggtt_pte_index,
	.gma_to_pte_index = gen8_gma_to_pte_index,
	.gma_to_pde_index = gen8_gma_to_pde_index,
	.gma_to_l3_pdp_index = gen8_gma_to_l3_pdp_index,
	.gma_to_l4_pdp_index = gen8_gma_to_l4_pdp_index,
	.gma_to_pml4_index = gen8_gma_to_pml4_index,
};

/* Update entry type per pse and ips bit. */
static void update_entry_type_for_real(const struct intel_gvt_gtt_pte_ops *pte_ops,
	struct intel_gvt_gtt_entry *entry, bool ips)
{
	switch (entry->type) {
	case GTT_TYPE_PPGTT_PDE_ENTRY:
	case GTT_TYPE_PPGTT_PDP_ENTRY:
		if (pte_ops->test_pse(entry))
			entry->type = get_pse_type(entry->type);
		break;
	case GTT_TYPE_PPGTT_PTE_4K_ENTRY:
		if (ips)
			entry->type = get_pse_type(entry->type);
		break;
	default:
		GEM_BUG_ON(!gtt_type_is_entry(entry->type));
	}

	GEM_BUG_ON(entry->type == GTT_TYPE_INVALID);
}

/*
 * MM helpers.
 */
static void _ppgtt_get_root_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index,
		bool guest)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = mm->vgpu->gvt->gtt.pte_ops;

	GEM_BUG_ON(mm->type != INTEL_GVT_MM_PPGTT);

	entry->type = mm->ppgtt_mm.root_entry_type;
	pte_ops->get_entry(guest ? mm->ppgtt_mm.guest_pdps :
			   mm->ppgtt_mm.shadow_pdps,
			   entry, index, false, 0, mm->vgpu);
	update_entry_type_for_real(pte_ops, entry, false);
}

static inline void ppgtt_get_guest_root_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	_ppgtt_get_root_entry(mm, entry, index, true);
}

static inline void ppgtt_get_shadow_root_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	_ppgtt_get_root_entry(mm, entry, index, false);
}

static void _ppgtt_set_root_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index,
		bool guest)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = mm->vgpu->gvt->gtt.pte_ops;

	pte_ops->set_entry(guest ? mm->ppgtt_mm.guest_pdps :
			   mm->ppgtt_mm.shadow_pdps,
			   entry, index, false, 0, mm->vgpu);
}

static inline void ppgtt_set_shadow_root_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	_ppgtt_set_root_entry(mm, entry, index, false);
}

static void ggtt_get_guest_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = mm->vgpu->gvt->gtt.pte_ops;

	GEM_BUG_ON(mm->type != INTEL_GVT_MM_GGTT);

	entry->type = GTT_TYPE_GGTT_PTE;
	pte_ops->get_entry(mm->ggtt_mm.virtual_ggtt, entry, index,
			   false, 0, mm->vgpu);
}

static void ggtt_set_guest_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = mm->vgpu->gvt->gtt.pte_ops;

	GEM_BUG_ON(mm->type != INTEL_GVT_MM_GGTT);

	pte_ops->set_entry(mm->ggtt_mm.virtual_ggtt, entry, index,
			   false, 0, mm->vgpu);
}

static void ggtt_get_host_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = mm->vgpu->gvt->gtt.pte_ops;

	GEM_BUG_ON(mm->type != INTEL_GVT_MM_GGTT);

	pte_ops->get_entry(NULL, entry, index, false, 0, mm->vgpu);
}

static void ggtt_set_host_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *entry, unsigned long index)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = mm->vgpu->gvt->gtt.pte_ops;
	unsigned long offset = index;

	GEM_BUG_ON(mm->type != INTEL_GVT_MM_GGTT);

	if (vgpu_gmadr_is_aperture(mm->vgpu, index << I915_GTT_PAGE_SHIFT)) {
		offset -= (vgpu_aperture_gmadr_base(mm->vgpu) >> PAGE_SHIFT);
		mm->ggtt_mm.host_ggtt_aperture[offset] = entry->val64;
	} else if (vgpu_gmadr_is_hidden(mm->vgpu, index << I915_GTT_PAGE_SHIFT)) {
		offset -= (vgpu_hidden_gmadr_base(mm->vgpu) >> PAGE_SHIFT);
		mm->ggtt_mm.host_ggtt_hidden[offset] = entry->val64;
	}

	pte_ops->set_entry(NULL, entry, index, false, 0, mm->vgpu);
}

/*
 * PPGTT shadow page table helpers.
 */
static inline int ppgtt_spt_get_entry(
		struct intel_vgpu_ppgtt_spt *spt,
		void *page_table, int type,
		struct intel_gvt_gtt_entry *e, unsigned long index,
		bool guest)
{
	struct intel_gvt *gvt = spt->vgpu->gvt;
	const struct intel_gvt_gtt_pte_ops *ops = gvt->gtt.pte_ops;
	int ret;

	e->type = get_entry_type(type);

	if (WARN(!gtt_type_is_entry(e->type), "invalid entry type\n"))
		return -EINVAL;

	ret = ops->get_entry(page_table, e, index, guest,
			spt->guest_page.gfn << I915_GTT_PAGE_SHIFT,
			spt->vgpu);
	if (ret)
		return ret;

	update_entry_type_for_real(ops, e, guest ?
				   spt->guest_page.pde_ips : false);

	gvt_vdbg_mm("read ppgtt entry, spt type %d, entry type %d, index %lu, value %llx\n",
		    type, e->type, index, e->val64);
	return 0;
}

static inline int ppgtt_spt_set_entry(
		struct intel_vgpu_ppgtt_spt *spt,
		void *page_table, int type,
		struct intel_gvt_gtt_entry *e, unsigned long index,
		bool guest)
{
	struct intel_gvt *gvt = spt->vgpu->gvt;
	const struct intel_gvt_gtt_pte_ops *ops = gvt->gtt.pte_ops;

	if (WARN(!gtt_type_is_entry(e->type), "invalid entry type\n"))
		return -EINVAL;

	gvt_vdbg_mm("set ppgtt entry, spt type %d, entry type %d, index %lu, value %llx\n",
		    type, e->type, index, e->val64);

	return ops->set_entry(page_table, e, index, guest,
			spt->guest_page.gfn << I915_GTT_PAGE_SHIFT,
			spt->vgpu);
}

#define ppgtt_get_guest_entry(spt, e, index) \
	ppgtt_spt_get_entry(spt, NULL, \
		spt->guest_page.type, e, index, true)

#define ppgtt_set_guest_entry(spt, e, index) \
	ppgtt_spt_set_entry(spt, NULL, \
		spt->guest_page.type, e, index, true)

#define ppgtt_get_shadow_entry(spt, e, index) \
	ppgtt_spt_get_entry(spt, spt->shadow_page.vaddr, \
		spt->shadow_page.type, e, index, false)

#define ppgtt_set_shadow_entry(spt, e, index) \
	ppgtt_spt_set_entry(spt, spt->shadow_page.vaddr, \
		spt->shadow_page.type, e, index, false)

static void *alloc_spt(gfp_t gfp_mask)
{
	struct intel_vgpu_ppgtt_spt *spt;

	spt = kzalloc(sizeof(*spt), gfp_mask);
	if (!spt)
		return NULL;

	spt->shadow_page.page = alloc_page(gfp_mask);
	if (!spt->shadow_page.page) {
		kfree(spt);
		return NULL;
	}
	return spt;
}

static void free_spt(struct intel_vgpu_ppgtt_spt *spt)
{
	__free_page(spt->shadow_page.page);
	kfree(spt);
}

static int detach_oos_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_oos_page *oos_page);

static void ppgtt_free_spt(struct intel_vgpu_ppgtt_spt *spt)
{
	struct device *kdev = spt->vgpu->gvt->gt->i915->drm.dev;

	trace_spt_free(spt->vgpu->id, spt, spt->guest_page.type);

	dma_unmap_page(kdev, spt->shadow_page.mfn << I915_GTT_PAGE_SHIFT, 4096,
		       DMA_BIDIRECTIONAL);

	radix_tree_delete(&spt->vgpu->gtt.spt_tree, spt->shadow_page.mfn);

	if (spt->guest_page.gfn) {
		if (spt->guest_page.oos_page)
			detach_oos_page(spt->vgpu, spt->guest_page.oos_page);

		intel_vgpu_unregister_page_track(spt->vgpu, spt->guest_page.gfn);
	}

	list_del_init(&spt->post_shadow_list);
	free_spt(spt);
}

static void ppgtt_free_all_spt(struct intel_vgpu *vgpu)
{
	struct intel_vgpu_ppgtt_spt *spt, *spn;
	struct radix_tree_iter iter;
	LIST_HEAD(all_spt);
	void __rcu **slot;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &vgpu->gtt.spt_tree, &iter, 0) {
		spt = radix_tree_deref_slot(slot);
		list_move(&spt->post_shadow_list, &all_spt);
	}
	rcu_read_unlock();

	list_for_each_entry_safe(spt, spn, &all_spt, post_shadow_list)
		ppgtt_free_spt(spt);
}

static int ppgtt_handle_guest_write_page_table_bytes(
		struct intel_vgpu_ppgtt_spt *spt,
		u64 pa, void *p_data, int bytes);

static int ppgtt_write_protection_handler(
		struct intel_vgpu_page_track *page_track,
		u64 gpa, void *data, int bytes)
{
	struct intel_vgpu_ppgtt_spt *spt = page_track->priv_data;

	int ret;

	if (bytes != 4 && bytes != 8)
		return -EINVAL;

	ret = ppgtt_handle_guest_write_page_table_bytes(spt, gpa, data, bytes);
	if (ret)
		return ret;
	return ret;
}

/* Find a spt by guest gfn. */
static struct intel_vgpu_ppgtt_spt *intel_vgpu_find_spt_by_gfn(
		struct intel_vgpu *vgpu, unsigned long gfn)
{
	struct intel_vgpu_page_track *track;

	track = intel_vgpu_find_page_track(vgpu, gfn);
	if (track && track->handler == ppgtt_write_protection_handler)
		return track->priv_data;

	return NULL;
}

/* Find the spt by shadow page mfn. */
static inline struct intel_vgpu_ppgtt_spt *intel_vgpu_find_spt_by_mfn(
		struct intel_vgpu *vgpu, unsigned long mfn)
{
	return radix_tree_lookup(&vgpu->gtt.spt_tree, mfn);
}

static int reclaim_one_ppgtt_mm(struct intel_gvt *gvt);

/* Allocate shadow page table without guest page. */
static struct intel_vgpu_ppgtt_spt *ppgtt_alloc_spt(
		struct intel_vgpu *vgpu, enum intel_gvt_gtt_type type)
{
	struct device *kdev = vgpu->gvt->gt->i915->drm.dev;
	struct intel_vgpu_ppgtt_spt *spt = NULL;
	dma_addr_t daddr;
	int ret;

retry:
	spt = alloc_spt(GFP_KERNEL | __GFP_ZERO);
	if (!spt) {
		if (reclaim_one_ppgtt_mm(vgpu->gvt))
			goto retry;

		gvt_vgpu_err("fail to allocate ppgtt shadow page\n");
		return ERR_PTR(-ENOMEM);
	}

	spt->vgpu = vgpu;
	atomic_set(&spt->refcount, 1);
	INIT_LIST_HEAD(&spt->post_shadow_list);

	/*
	 * Init shadow_page.
	 */
	spt->shadow_page.type = type;
	daddr = dma_map_page(kdev, spt->shadow_page.page,
			     0, 4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kdev, daddr)) {
		gvt_vgpu_err("fail to map dma addr\n");
		ret = -EINVAL;
		goto err_free_spt;
	}
	spt->shadow_page.vaddr = page_address(spt->shadow_page.page);
	spt->shadow_page.mfn = daddr >> I915_GTT_PAGE_SHIFT;

	ret = radix_tree_insert(&vgpu->gtt.spt_tree, spt->shadow_page.mfn, spt);
	if (ret)
		goto err_unmap_dma;

	return spt;

err_unmap_dma:
	dma_unmap_page(kdev, daddr, PAGE_SIZE, DMA_BIDIRECTIONAL);
err_free_spt:
	free_spt(spt);
	return ERR_PTR(ret);
}

/* Allocate shadow page table associated with specific gfn. */
static struct intel_vgpu_ppgtt_spt *ppgtt_alloc_spt_gfn(
		struct intel_vgpu *vgpu, enum intel_gvt_gtt_type type,
		unsigned long gfn, bool guest_pde_ips)
{
	struct intel_vgpu_ppgtt_spt *spt;
	int ret;

	spt = ppgtt_alloc_spt(vgpu, type);
	if (IS_ERR(spt))
		return spt;

	/*
	 * Init guest_page.
	 */
	ret = intel_vgpu_register_page_track(vgpu, gfn,
			ppgtt_write_protection_handler, spt);
	if (ret) {
		ppgtt_free_spt(spt);
		return ERR_PTR(ret);
	}

	spt->guest_page.type = type;
	spt->guest_page.gfn = gfn;
	spt->guest_page.pde_ips = guest_pde_ips;

	trace_spt_alloc(vgpu->id, spt, type, spt->shadow_page.mfn, gfn);

	return spt;
}

#define pt_entry_size_shift(spt) \
	((spt)->vgpu->gvt->device_info.gtt_entry_size_shift)

#define pt_entries(spt) \
	(I915_GTT_PAGE_SIZE >> pt_entry_size_shift(spt))

#define for_each_present_guest_entry(spt, e, i) \
	for (i = 0; i < pt_entries(spt); \
	     i += spt->guest_page.pde_ips ? GTT_64K_PTE_STRIDE : 1) \
		if (!ppgtt_get_guest_entry(spt, e, i) && \
		    spt->vgpu->gvt->gtt.pte_ops->test_present(e))

#define for_each_present_shadow_entry(spt, e, i) \
	for (i = 0; i < pt_entries(spt); \
	     i += spt->shadow_page.pde_ips ? GTT_64K_PTE_STRIDE : 1) \
		if (!ppgtt_get_shadow_entry(spt, e, i) && \
		    spt->vgpu->gvt->gtt.pte_ops->test_present(e))

#define for_each_shadow_entry(spt, e, i) \
	for (i = 0; i < pt_entries(spt); \
	     i += (spt->shadow_page.pde_ips ? GTT_64K_PTE_STRIDE : 1)) \
		if (!ppgtt_get_shadow_entry(spt, e, i))

static inline void ppgtt_get_spt(struct intel_vgpu_ppgtt_spt *spt)
{
	int v = atomic_read(&spt->refcount);

	trace_spt_refcount(spt->vgpu->id, "inc", spt, v, (v + 1));
	atomic_inc(&spt->refcount);
}

static inline int ppgtt_put_spt(struct intel_vgpu_ppgtt_spt *spt)
{
	int v = atomic_read(&spt->refcount);

	trace_spt_refcount(spt->vgpu->id, "dec", spt, v, (v - 1));
	return atomic_dec_return(&spt->refcount);
}

static int ppgtt_invalidate_spt(struct intel_vgpu_ppgtt_spt *spt);

static int ppgtt_invalidate_spt_by_shadow_entry(struct intel_vgpu *vgpu,
		struct intel_gvt_gtt_entry *e)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	struct intel_vgpu_ppgtt_spt *s;
	enum intel_gvt_gtt_type cur_pt_type;

	GEM_BUG_ON(!gtt_type_is_pt(get_next_pt_type(e->type)));

	if (e->type != GTT_TYPE_PPGTT_ROOT_L3_ENTRY
		&& e->type != GTT_TYPE_PPGTT_ROOT_L4_ENTRY) {
		cur_pt_type = get_next_pt_type(e->type);

		if (!gtt_type_is_pt(cur_pt_type) ||
				!gtt_type_is_pt(cur_pt_type + 1)) {
			drm_WARN(&i915->drm, 1,
				 "Invalid page table type, cur_pt_type is: %d\n",
				 cur_pt_type);
			return -EINVAL;
		}

		cur_pt_type += 1;

		if (ops->get_pfn(e) ==
			vgpu->gtt.scratch_pt[cur_pt_type].page_mfn)
			return 0;
	}
	s = intel_vgpu_find_spt_by_mfn(vgpu, ops->get_pfn(e));
	if (!s) {
		gvt_vgpu_err("fail to find shadow page: mfn: 0x%lx\n",
				ops->get_pfn(e));
		return -ENXIO;
	}
	return ppgtt_invalidate_spt(s);
}

static inline void ppgtt_invalidate_pte(struct intel_vgpu_ppgtt_spt *spt,
		struct intel_gvt_gtt_entry *entry)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	unsigned long pfn;
	int type;

	pfn = ops->get_pfn(entry);
	type = spt->shadow_page.type;

	/* Uninitialized spte or unshadowed spte. */
	if (!pfn || pfn == vgpu->gtt.scratch_pt[type].page_mfn)
		return;

	intel_gvt_dma_unmap_guest_page(vgpu, pfn << PAGE_SHIFT);
}

static int ppgtt_invalidate_spt(struct intel_vgpu_ppgtt_spt *spt)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	struct intel_gvt_gtt_entry e;
	unsigned long index;
	int ret;

	trace_spt_change(spt->vgpu->id, "die", spt,
			spt->guest_page.gfn, spt->shadow_page.type);

	if (ppgtt_put_spt(spt) > 0)
		return 0;

	for_each_present_shadow_entry(spt, &e, index) {
		switch (e.type) {
		case GTT_TYPE_PPGTT_PTE_4K_ENTRY:
			gvt_vdbg_mm("invalidate 4K entry\n");
			ppgtt_invalidate_pte(spt, &e);
			break;
		case GTT_TYPE_PPGTT_PTE_64K_ENTRY:
			/* We don't setup 64K shadow entry so far. */
			WARN(1, "suspicious 64K gtt entry\n");
			continue;
		case GTT_TYPE_PPGTT_PTE_2M_ENTRY:
			gvt_vdbg_mm("invalidate 2M entry\n");
			continue;
		case GTT_TYPE_PPGTT_PTE_1G_ENTRY:
			WARN(1, "GVT doesn't support 1GB page\n");
			continue;
		case GTT_TYPE_PPGTT_PML4_ENTRY:
		case GTT_TYPE_PPGTT_PDP_ENTRY:
		case GTT_TYPE_PPGTT_PDE_ENTRY:
			gvt_vdbg_mm("invalidate PMUL4/PDP/PDE entry\n");
			ret = ppgtt_invalidate_spt_by_shadow_entry(
					spt->vgpu, &e);
			if (ret)
				goto fail;
			break;
		default:
			GEM_BUG_ON(1);
		}
	}

	trace_spt_change(spt->vgpu->id, "release", spt,
			 spt->guest_page.gfn, spt->shadow_page.type);
	ppgtt_free_spt(spt);
	return 0;
fail:
	gvt_vgpu_err("fail: shadow page %p shadow entry 0x%llx type %d\n",
			spt, e.val64, e.type);
	return ret;
}

static bool vgpu_ips_enabled(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;

	if (GRAPHICS_VER(dev_priv) == 9) {
		u32 ips = vgpu_vreg_t(vgpu, GEN8_GAMW_ECO_DEV_RW_IA) &
			GAMW_ECO_ENABLE_64K_IPS_FIELD;

		return ips == GAMW_ECO_ENABLE_64K_IPS_FIELD;
	} else if (GRAPHICS_VER(dev_priv) >= 11) {
		/* 64K paging only controlled by IPS bit in PTE now. */
		return true;
	} else
		return false;
}

static int ppgtt_populate_spt(struct intel_vgpu_ppgtt_spt *spt);

static struct intel_vgpu_ppgtt_spt *ppgtt_populate_spt_by_guest_entry(
		struct intel_vgpu *vgpu, struct intel_gvt_gtt_entry *we)
{
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	struct intel_vgpu_ppgtt_spt *spt = NULL;
	bool ips = false;
	int ret;

	GEM_BUG_ON(!gtt_type_is_pt(get_next_pt_type(we->type)));

	if (we->type == GTT_TYPE_PPGTT_PDE_ENTRY)
		ips = vgpu_ips_enabled(vgpu) && ops->test_ips(we);

	spt = intel_vgpu_find_spt_by_gfn(vgpu, ops->get_pfn(we));
	if (spt) {
		ppgtt_get_spt(spt);

		if (ips != spt->guest_page.pde_ips) {
			spt->guest_page.pde_ips = ips;

			gvt_dbg_mm("reshadow PDE since ips changed\n");
			clear_page(spt->shadow_page.vaddr);
			ret = ppgtt_populate_spt(spt);
			if (ret) {
				ppgtt_put_spt(spt);
				goto err;
			}
		}
	} else {
		int type = get_next_pt_type(we->type);

		if (!gtt_type_is_pt(type)) {
			ret = -EINVAL;
			goto err;
		}

		spt = ppgtt_alloc_spt_gfn(vgpu, type, ops->get_pfn(we), ips);
		if (IS_ERR(spt)) {
			ret = PTR_ERR(spt);
			goto err;
		}

		ret = intel_vgpu_enable_page_track(vgpu, spt->guest_page.gfn);
		if (ret)
			goto err_free_spt;

		ret = ppgtt_populate_spt(spt);
		if (ret)
			goto err_free_spt;

		trace_spt_change(vgpu->id, "new", spt, spt->guest_page.gfn,
				 spt->shadow_page.type);
	}
	return spt;

err_free_spt:
	ppgtt_free_spt(spt);
	spt = NULL;
err:
	gvt_vgpu_err("fail: shadow page %p guest entry 0x%llx type %d\n",
		     spt, we->val64, we->type);
	return ERR_PTR(ret);
}

static inline void ppgtt_generate_shadow_entry(struct intel_gvt_gtt_entry *se,
		struct intel_vgpu_ppgtt_spt *s, struct intel_gvt_gtt_entry *ge)
{
	const struct intel_gvt_gtt_pte_ops *ops = s->vgpu->gvt->gtt.pte_ops;

	se->type = ge->type;
	se->val64 = ge->val64;

	/* Because we always split 64KB pages, so clear IPS in shadow PDE. */
	if (se->type == GTT_TYPE_PPGTT_PDE_ENTRY)
		ops->clear_ips(se);

	ops->set_pfn(se, s->shadow_page.mfn);
}

static int split_2MB_gtt_entry(struct intel_vgpu *vgpu,
	struct intel_vgpu_ppgtt_spt *spt, unsigned long index,
	struct intel_gvt_gtt_entry *se)
{
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	struct intel_vgpu_ppgtt_spt *sub_spt;
	struct intel_gvt_gtt_entry sub_se;
	unsigned long start_gfn;
	dma_addr_t dma_addr;
	unsigned long sub_index;
	int ret;

	gvt_dbg_mm("Split 2M gtt entry, index %lu\n", index);

	start_gfn = ops->get_pfn(se);

	sub_spt = ppgtt_alloc_spt(vgpu, GTT_TYPE_PPGTT_PTE_PT);
	if (IS_ERR(sub_spt))
		return PTR_ERR(sub_spt);

	for_each_shadow_entry(sub_spt, &sub_se, sub_index) {
		ret = intel_gvt_dma_map_guest_page(vgpu, start_gfn + sub_index,
						   PAGE_SIZE, &dma_addr);
		if (ret)
			goto err;
		sub_se.val64 = se->val64;

		/* Copy the PAT field from PDE. */
		sub_se.val64 &= ~_PAGE_PAT;
		sub_se.val64 |= (se->val64 & _PAGE_PAT_LARGE) >> 5;

		ops->set_pfn(&sub_se, dma_addr >> PAGE_SHIFT);
		ppgtt_set_shadow_entry(sub_spt, &sub_se, sub_index);
	}

	/* Clear dirty field. */
	se->val64 &= ~_PAGE_DIRTY;

	ops->clear_pse(se);
	ops->clear_ips(se);
	ops->set_pfn(se, sub_spt->shadow_page.mfn);
	ppgtt_set_shadow_entry(spt, se, index);
	return 0;
err:
	/* Cancel the existing address mappings of DMA addr. */
	for_each_present_shadow_entry(sub_spt, &sub_se, sub_index) {
		gvt_vdbg_mm("invalidate 4K entry\n");
		ppgtt_invalidate_pte(sub_spt, &sub_se);
	}
	/* Release the new allocated spt. */
	trace_spt_change(sub_spt->vgpu->id, "release", sub_spt,
		sub_spt->guest_page.gfn, sub_spt->shadow_page.type);
	ppgtt_free_spt(sub_spt);
	return ret;
}

static int split_64KB_gtt_entry(struct intel_vgpu *vgpu,
	struct intel_vgpu_ppgtt_spt *spt, unsigned long index,
	struct intel_gvt_gtt_entry *se)
{
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	struct intel_gvt_gtt_entry entry = *se;
	unsigned long start_gfn;
	dma_addr_t dma_addr;
	int i, ret;

	gvt_vdbg_mm("Split 64K gtt entry, index %lu\n", index);

	GEM_BUG_ON(index % GTT_64K_PTE_STRIDE);

	start_gfn = ops->get_pfn(se);

	entry.type = GTT_TYPE_PPGTT_PTE_4K_ENTRY;
	ops->set_64k_splited(&entry);

	for (i = 0; i < GTT_64K_PTE_STRIDE; i++) {
		ret = intel_gvt_dma_map_guest_page(vgpu, start_gfn + i,
						   PAGE_SIZE, &dma_addr);
		if (ret)
			return ret;

		ops->set_pfn(&entry, dma_addr >> PAGE_SHIFT);
		ppgtt_set_shadow_entry(spt, &entry, index + i);
	}
	return 0;
}

static int ppgtt_populate_shadow_entry(struct intel_vgpu *vgpu,
	struct intel_vgpu_ppgtt_spt *spt, unsigned long index,
	struct intel_gvt_gtt_entry *ge)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = vgpu->gvt->gtt.pte_ops;
	struct intel_gvt_gtt_entry se = *ge;
	unsigned long gfn;
	dma_addr_t dma_addr;
	int ret;

	if (!pte_ops->test_present(ge))
		return 0;

	gfn = pte_ops->get_pfn(ge);

	switch (ge->type) {
	case GTT_TYPE_PPGTT_PTE_4K_ENTRY:
		gvt_vdbg_mm("shadow 4K gtt entry\n");
		ret = intel_gvt_dma_map_guest_page(vgpu, gfn, PAGE_SIZE, &dma_addr);
		if (ret)
			return -ENXIO;
		break;
	case GTT_TYPE_PPGTT_PTE_64K_ENTRY:
		gvt_vdbg_mm("shadow 64K gtt entry\n");
		/*
		 * The layout of 64K page is special, the page size is
		 * controlled by uper PDE. To be simple, we always split
		 * 64K page to smaller 4K pages in shadow PT.
		 */
		return split_64KB_gtt_entry(vgpu, spt, index, &se);
	case GTT_TYPE_PPGTT_PTE_2M_ENTRY:
		gvt_vdbg_mm("shadow 2M gtt entry\n");
		if (!HAS_PAGE_SIZES(vgpu->gvt->gt->i915, I915_GTT_PAGE_SIZE_2M) ||
		    intel_gvt_dma_map_guest_page(vgpu, gfn,
						 I915_GTT_PAGE_SIZE_2M, &dma_addr))
			return split_2MB_gtt_entry(vgpu, spt, index, &se);
		break;
	case GTT_TYPE_PPGTT_PTE_1G_ENTRY:
		gvt_vgpu_err("GVT doesn't support 1GB entry\n");
		return -EINVAL;
	default:
		GEM_BUG_ON(1);
		return -EINVAL;
	}

	/* Successfully shadowed a 4K or 2M page (without splitting). */
	pte_ops->set_pfn(&se, dma_addr >> PAGE_SHIFT);
	ppgtt_set_shadow_entry(spt, &se, index);
	return 0;
}

static int ppgtt_populate_spt(struct intel_vgpu_ppgtt_spt *spt)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	struct intel_vgpu_ppgtt_spt *s;
	struct intel_gvt_gtt_entry se, ge;
	unsigned long i;
	int ret;

	trace_spt_change(spt->vgpu->id, "born", spt,
			 spt->guest_page.gfn, spt->shadow_page.type);

	for_each_present_guest_entry(spt, &ge, i) {
		if (gtt_type_is_pt(get_next_pt_type(ge.type))) {
			s = ppgtt_populate_spt_by_guest_entry(vgpu, &ge);
			if (IS_ERR(s)) {
				ret = PTR_ERR(s);
				goto fail;
			}
			ppgtt_get_shadow_entry(spt, &se, i);
			ppgtt_generate_shadow_entry(&se, s, &ge);
			ppgtt_set_shadow_entry(spt, &se, i);
		} else {
			ret = ppgtt_populate_shadow_entry(vgpu, spt, i, &ge);
			if (ret)
				goto fail;
		}
	}
	return 0;
fail:
	gvt_vgpu_err("fail: shadow page %p guest entry 0x%llx type %d\n",
			spt, ge.val64, ge.type);
	return ret;
}

static int ppgtt_handle_guest_entry_removal(struct intel_vgpu_ppgtt_spt *spt,
		struct intel_gvt_gtt_entry *se, unsigned long index)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	int ret;

	trace_spt_guest_change(spt->vgpu->id, "remove", spt,
			       spt->shadow_page.type, se->val64, index);

	gvt_vdbg_mm("destroy old shadow entry, type %d, index %lu, value %llx\n",
		    se->type, index, se->val64);

	if (!ops->test_present(se))
		return 0;

	if (ops->get_pfn(se) ==
	    vgpu->gtt.scratch_pt[spt->shadow_page.type].page_mfn)
		return 0;

	if (gtt_type_is_pt(get_next_pt_type(se->type))) {
		struct intel_vgpu_ppgtt_spt *s =
			intel_vgpu_find_spt_by_mfn(vgpu, ops->get_pfn(se));
		if (!s) {
			gvt_vgpu_err("fail to find guest page\n");
			ret = -ENXIO;
			goto fail;
		}
		ret = ppgtt_invalidate_spt(s);
		if (ret)
			goto fail;
	} else {
		/* We don't setup 64K shadow entry so far. */
		WARN(se->type == GTT_TYPE_PPGTT_PTE_64K_ENTRY,
		     "suspicious 64K entry\n");
		ppgtt_invalidate_pte(spt, se);
	}

	return 0;
fail:
	gvt_vgpu_err("fail: shadow page %p guest entry 0x%llx type %d\n",
			spt, se->val64, se->type);
	return ret;
}

static int ppgtt_handle_guest_entry_add(struct intel_vgpu_ppgtt_spt *spt,
		struct intel_gvt_gtt_entry *we, unsigned long index)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	struct intel_gvt_gtt_entry m;
	struct intel_vgpu_ppgtt_spt *s;
	int ret;

	trace_spt_guest_change(spt->vgpu->id, "add", spt, spt->shadow_page.type,
			       we->val64, index);

	gvt_vdbg_mm("add shadow entry: type %d, index %lu, value %llx\n",
		    we->type, index, we->val64);

	if (gtt_type_is_pt(get_next_pt_type(we->type))) {
		s = ppgtt_populate_spt_by_guest_entry(vgpu, we);
		if (IS_ERR(s)) {
			ret = PTR_ERR(s);
			goto fail;
		}
		ppgtt_get_shadow_entry(spt, &m, index);
		ppgtt_generate_shadow_entry(&m, s, we);
		ppgtt_set_shadow_entry(spt, &m, index);
	} else {
		ret = ppgtt_populate_shadow_entry(vgpu, spt, index, we);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	gvt_vgpu_err("fail: spt %p guest entry 0x%llx type %d\n",
		spt, we->val64, we->type);
	return ret;
}

static int sync_oos_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_oos_page *oos_page)
{
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_gtt_pte_ops *ops = gvt->gtt.pte_ops;
	struct intel_vgpu_ppgtt_spt *spt = oos_page->spt;
	struct intel_gvt_gtt_entry old, new;
	int index;
	int ret;

	trace_oos_change(vgpu->id, "sync", oos_page->id,
			 spt, spt->guest_page.type);

	old.type = new.type = get_entry_type(spt->guest_page.type);
	old.val64 = new.val64 = 0;

	for (index = 0; index < (I915_GTT_PAGE_SIZE >>
				info->gtt_entry_size_shift); index++) {
		ops->get_entry(oos_page->mem, &old, index, false, 0, vgpu);
		ops->get_entry(NULL, &new, index, true,
			       spt->guest_page.gfn << PAGE_SHIFT, vgpu);

		if (old.val64 == new.val64
			&& !test_and_clear_bit(index, spt->post_shadow_bitmap))
			continue;

		trace_oos_sync(vgpu->id, oos_page->id,
				spt, spt->guest_page.type,
				new.val64, index);

		ret = ppgtt_populate_shadow_entry(vgpu, spt, index, &new);
		if (ret)
			return ret;

		ops->set_entry(oos_page->mem, &new, index, false, 0, vgpu);
	}

	spt->guest_page.write_cnt = 0;
	list_del_init(&spt->post_shadow_list);
	return 0;
}

static int detach_oos_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_oos_page *oos_page)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_vgpu_ppgtt_spt *spt = oos_page->spt;

	trace_oos_change(vgpu->id, "detach", oos_page->id,
			 spt, spt->guest_page.type);

	spt->guest_page.write_cnt = 0;
	spt->guest_page.oos_page = NULL;
	oos_page->spt = NULL;

	list_del_init(&oos_page->vm_list);
	list_move_tail(&oos_page->list, &gvt->gtt.oos_page_free_list_head);

	return 0;
}

static int attach_oos_page(struct intel_vgpu_oos_page *oos_page,
		struct intel_vgpu_ppgtt_spt *spt)
{
	struct intel_gvt *gvt = spt->vgpu->gvt;
	int ret;

	ret = intel_gvt_read_gpa(spt->vgpu,
			spt->guest_page.gfn << I915_GTT_PAGE_SHIFT,
			oos_page->mem, I915_GTT_PAGE_SIZE);
	if (ret)
		return ret;

	oos_page->spt = spt;
	spt->guest_page.oos_page = oos_page;

	list_move_tail(&oos_page->list, &gvt->gtt.oos_page_use_list_head);

	trace_oos_change(spt->vgpu->id, "attach", oos_page->id,
			 spt, spt->guest_page.type);
	return 0;
}

static int ppgtt_set_guest_page_sync(struct intel_vgpu_ppgtt_spt *spt)
{
	struct intel_vgpu_oos_page *oos_page = spt->guest_page.oos_page;
	int ret;

	ret = intel_vgpu_enable_page_track(spt->vgpu, spt->guest_page.gfn);
	if (ret)
		return ret;

	trace_oos_change(spt->vgpu->id, "set page sync", oos_page->id,
			 spt, spt->guest_page.type);

	list_del_init(&oos_page->vm_list);
	return sync_oos_page(spt->vgpu, oos_page);
}

static int ppgtt_allocate_oos_page(struct intel_vgpu_ppgtt_spt *spt)
{
	struct intel_gvt *gvt = spt->vgpu->gvt;
	struct intel_gvt_gtt *gtt = &gvt->gtt;
	struct intel_vgpu_oos_page *oos_page = spt->guest_page.oos_page;
	int ret;

	WARN(oos_page, "shadow PPGTT page has already has a oos page\n");

	if (list_empty(&gtt->oos_page_free_list_head)) {
		oos_page = container_of(gtt->oos_page_use_list_head.next,
			struct intel_vgpu_oos_page, list);
		ret = ppgtt_set_guest_page_sync(oos_page->spt);
		if (ret)
			return ret;
		ret = detach_oos_page(spt->vgpu, oos_page);
		if (ret)
			return ret;
	} else
		oos_page = container_of(gtt->oos_page_free_list_head.next,
			struct intel_vgpu_oos_page, list);
	return attach_oos_page(oos_page, spt);
}

static int ppgtt_set_guest_page_oos(struct intel_vgpu_ppgtt_spt *spt)
{
	struct intel_vgpu_oos_page *oos_page = spt->guest_page.oos_page;

	if (WARN(!oos_page, "shadow PPGTT page should have a oos page\n"))
		return -EINVAL;

	trace_oos_change(spt->vgpu->id, "set page out of sync", oos_page->id,
			 spt, spt->guest_page.type);

	list_add_tail(&oos_page->vm_list, &spt->vgpu->gtt.oos_page_list_head);
	return intel_vgpu_disable_page_track(spt->vgpu, spt->guest_page.gfn);
}

/**
 * intel_vgpu_sync_oos_pages - sync all the out-of-synced shadow for vGPU
 * @vgpu: a vGPU
 *
 * This function is called before submitting a guest workload to host,
 * to sync all the out-of-synced shadow for vGPU
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_sync_oos_pages(struct intel_vgpu *vgpu)
{
	struct list_head *pos, *n;
	struct intel_vgpu_oos_page *oos_page;
	int ret;

	if (!enable_out_of_sync)
		return 0;

	list_for_each_safe(pos, n, &vgpu->gtt.oos_page_list_head) {
		oos_page = container_of(pos,
				struct intel_vgpu_oos_page, vm_list);
		ret = ppgtt_set_guest_page_sync(oos_page->spt);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * The heart of PPGTT shadow page table.
 */
static int ppgtt_handle_guest_write_page_table(
		struct intel_vgpu_ppgtt_spt *spt,
		struct intel_gvt_gtt_entry *we, unsigned long index)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	int type = spt->shadow_page.type;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	struct intel_gvt_gtt_entry old_se;
	int new_present;
	int i, ret;

	new_present = ops->test_present(we);

	/*
	 * Adding the new entry first and then removing the old one, that can
	 * guarantee the ppgtt table is validated during the window between
	 * adding and removal.
	 */
	ppgtt_get_shadow_entry(spt, &old_se, index);

	if (new_present) {
		ret = ppgtt_handle_guest_entry_add(spt, we, index);
		if (ret)
			goto fail;
	}

	ret = ppgtt_handle_guest_entry_removal(spt, &old_se, index);
	if (ret)
		goto fail;

	if (!new_present) {
		/* For 64KB splited entries, we need clear them all. */
		if (ops->test_64k_splited(&old_se) &&
		    !(index % GTT_64K_PTE_STRIDE)) {
			gvt_vdbg_mm("remove splited 64K shadow entries\n");
			for (i = 0; i < GTT_64K_PTE_STRIDE; i++) {
				ops->clear_64k_splited(&old_se);
				ops->set_pfn(&old_se,
					vgpu->gtt.scratch_pt[type].page_mfn);
				ppgtt_set_shadow_entry(spt, &old_se, index + i);
			}
		} else if (old_se.type == GTT_TYPE_PPGTT_PTE_2M_ENTRY ||
			   old_se.type == GTT_TYPE_PPGTT_PTE_1G_ENTRY) {
			ops->clear_pse(&old_se);
			ops->set_pfn(&old_se,
				     vgpu->gtt.scratch_pt[type].page_mfn);
			ppgtt_set_shadow_entry(spt, &old_se, index);
		} else {
			ops->set_pfn(&old_se,
				     vgpu->gtt.scratch_pt[type].page_mfn);
			ppgtt_set_shadow_entry(spt, &old_se, index);
		}
	}

	return 0;
fail:
	gvt_vgpu_err("fail: shadow page %p guest entry 0x%llx type %d.\n",
			spt, we->val64, we->type);
	return ret;
}



static inline bool can_do_out_of_sync(struct intel_vgpu_ppgtt_spt *spt)
{
	return enable_out_of_sync
		&& gtt_type_is_pte_pt(spt->guest_page.type)
		&& spt->guest_page.write_cnt >= 2;
}

static void ppgtt_set_post_shadow(struct intel_vgpu_ppgtt_spt *spt,
		unsigned long index)
{
	set_bit(index, spt->post_shadow_bitmap);
	if (!list_empty(&spt->post_shadow_list))
		return;

	list_add_tail(&spt->post_shadow_list,
			&spt->vgpu->gtt.post_shadow_list_head);
}

/**
 * intel_vgpu_flush_post_shadow - flush the post shadow transactions
 * @vgpu: a vGPU
 *
 * This function is called before submitting a guest workload to host,
 * to flush all the post shadows for a vGPU.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_flush_post_shadow(struct intel_vgpu *vgpu)
{
	struct list_head *pos, *n;
	struct intel_vgpu_ppgtt_spt *spt;
	struct intel_gvt_gtt_entry ge;
	unsigned long index;
	int ret;

	list_for_each_safe(pos, n, &vgpu->gtt.post_shadow_list_head) {
		spt = container_of(pos, struct intel_vgpu_ppgtt_spt,
				post_shadow_list);

		for_each_set_bit(index, spt->post_shadow_bitmap,
				GTT_ENTRY_NUM_IN_ONE_PAGE) {
			ppgtt_get_guest_entry(spt, &ge, index);

			ret = ppgtt_handle_guest_write_page_table(spt,
							&ge, index);
			if (ret)
				return ret;
			clear_bit(index, spt->post_shadow_bitmap);
		}
		list_del_init(&spt->post_shadow_list);
	}
	return 0;
}

static int ppgtt_handle_guest_write_page_table_bytes(
		struct intel_vgpu_ppgtt_spt *spt,
		u64 pa, void *p_data, int bytes)
{
	struct intel_vgpu *vgpu = spt->vgpu;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	struct intel_gvt_gtt_entry we, se;
	unsigned long index;
	int ret;

	index = (pa & (PAGE_SIZE - 1)) >> info->gtt_entry_size_shift;

	ppgtt_get_guest_entry(spt, &we, index);

	/*
	 * For page table which has 64K gtt entry, only PTE#0, PTE#16,
	 * PTE#32, ... PTE#496 are used. Unused PTEs update should be
	 * ignored.
	 */
	if (we.type == GTT_TYPE_PPGTT_PTE_64K_ENTRY &&
	    (index % GTT_64K_PTE_STRIDE)) {
		gvt_vdbg_mm("Ignore write to unused PTE entry, index %lu\n",
			    index);
		return 0;
	}

	if (bytes == info->gtt_entry_size) {
		ret = ppgtt_handle_guest_write_page_table(spt, &we, index);
		if (ret)
			return ret;
	} else {
		if (!test_bit(index, spt->post_shadow_bitmap)) {
			int type = spt->shadow_page.type;

			ppgtt_get_shadow_entry(spt, &se, index);
			ret = ppgtt_handle_guest_entry_removal(spt, &se, index);
			if (ret)
				return ret;
			ops->set_pfn(&se, vgpu->gtt.scratch_pt[type].page_mfn);
			ppgtt_set_shadow_entry(spt, &se, index);
		}
		ppgtt_set_post_shadow(spt, index);
	}

	if (!enable_out_of_sync)
		return 0;

	spt->guest_page.write_cnt++;

	if (spt->guest_page.oos_page)
		ops->set_entry(spt->guest_page.oos_page->mem, &we, index,
				false, 0, vgpu);

	if (can_do_out_of_sync(spt)) {
		if (!spt->guest_page.oos_page)
			ppgtt_allocate_oos_page(spt);

		ret = ppgtt_set_guest_page_oos(spt);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void invalidate_ppgtt_mm(struct intel_vgpu_mm *mm)
{
	struct intel_vgpu *vgpu = mm->vgpu;
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_gtt *gtt = &gvt->gtt;
	const struct intel_gvt_gtt_pte_ops *ops = gtt->pte_ops;
	struct intel_gvt_gtt_entry se;
	int index;

	if (!mm->ppgtt_mm.shadowed)
		return;

	for (index = 0; index < ARRAY_SIZE(mm->ppgtt_mm.shadow_pdps); index++) {
		ppgtt_get_shadow_root_entry(mm, &se, index);

		if (!ops->test_present(&se))
			continue;

		ppgtt_invalidate_spt_by_shadow_entry(vgpu, &se);
		se.val64 = 0;
		ppgtt_set_shadow_root_entry(mm, &se, index);

		trace_spt_guest_change(vgpu->id, "destroy root pointer",
				       NULL, se.type, se.val64, index);
	}

	mm->ppgtt_mm.shadowed = false;
}


static int shadow_ppgtt_mm(struct intel_vgpu_mm *mm)
{
	struct intel_vgpu *vgpu = mm->vgpu;
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_gtt *gtt = &gvt->gtt;
	const struct intel_gvt_gtt_pte_ops *ops = gtt->pte_ops;
	struct intel_vgpu_ppgtt_spt *spt;
	struct intel_gvt_gtt_entry ge, se;
	int index, ret;

	if (mm->ppgtt_mm.shadowed)
		return 0;

	if (!test_bit(INTEL_VGPU_STATUS_ATTACHED, vgpu->status))
		return -EINVAL;

	mm->ppgtt_mm.shadowed = true;

	for (index = 0; index < ARRAY_SIZE(mm->ppgtt_mm.guest_pdps); index++) {
		ppgtt_get_guest_root_entry(mm, &ge, index);

		if (!ops->test_present(&ge))
			continue;

		trace_spt_guest_change(vgpu->id, __func__, NULL,
				       ge.type, ge.val64, index);

		spt = ppgtt_populate_spt_by_guest_entry(vgpu, &ge);
		if (IS_ERR(spt)) {
			gvt_vgpu_err("fail to populate guest root pointer\n");
			ret = PTR_ERR(spt);
			goto fail;
		}
		ppgtt_generate_shadow_entry(&se, spt, &ge);
		ppgtt_set_shadow_root_entry(mm, &se, index);

		trace_spt_guest_change(vgpu->id, "populate root pointer",
				       NULL, se.type, se.val64, index);
	}

	return 0;
fail:
	invalidate_ppgtt_mm(mm);
	return ret;
}

static struct intel_vgpu_mm *vgpu_alloc_mm(struct intel_vgpu *vgpu)
{
	struct intel_vgpu_mm *mm;

	mm = kzalloc(sizeof(*mm), GFP_KERNEL);
	if (!mm)
		return NULL;

	mm->vgpu = vgpu;
	kref_init(&mm->ref);
	atomic_set(&mm->pincount, 0);

	return mm;
}

static void vgpu_free_mm(struct intel_vgpu_mm *mm)
{
	kfree(mm);
}

/**
 * intel_vgpu_create_ppgtt_mm - create a ppgtt mm object for a vGPU
 * @vgpu: a vGPU
 * @root_entry_type: ppgtt root entry type
 * @pdps: guest pdps.
 *
 * This function is used to create a ppgtt mm object for a vGPU.
 *
 * Returns:
 * Zero on success, negative error code in pointer if failed.
 */
struct intel_vgpu_mm *intel_vgpu_create_ppgtt_mm(struct intel_vgpu *vgpu,
		enum intel_gvt_gtt_type root_entry_type, u64 pdps[])
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_vgpu_mm *mm;
	int ret;

	mm = vgpu_alloc_mm(vgpu);
	if (!mm)
		return ERR_PTR(-ENOMEM);

	mm->type = INTEL_GVT_MM_PPGTT;

	GEM_BUG_ON(root_entry_type != GTT_TYPE_PPGTT_ROOT_L3_ENTRY &&
		   root_entry_type != GTT_TYPE_PPGTT_ROOT_L4_ENTRY);
	mm->ppgtt_mm.root_entry_type = root_entry_type;

	INIT_LIST_HEAD(&mm->ppgtt_mm.list);
	INIT_LIST_HEAD(&mm->ppgtt_mm.lru_list);
	INIT_LIST_HEAD(&mm->ppgtt_mm.link);

	if (root_entry_type == GTT_TYPE_PPGTT_ROOT_L4_ENTRY)
		mm->ppgtt_mm.guest_pdps[0] = pdps[0];
	else
		memcpy(mm->ppgtt_mm.guest_pdps, pdps,
		       sizeof(mm->ppgtt_mm.guest_pdps));

	ret = shadow_ppgtt_mm(mm);
	if (ret) {
		gvt_vgpu_err("failed to shadow ppgtt mm\n");
		vgpu_free_mm(mm);
		return ERR_PTR(ret);
	}

	list_add_tail(&mm->ppgtt_mm.list, &vgpu->gtt.ppgtt_mm_list_head);

	mutex_lock(&gvt->gtt.ppgtt_mm_lock);
	list_add_tail(&mm->ppgtt_mm.lru_list, &gvt->gtt.ppgtt_mm_lru_list_head);
	mutex_unlock(&gvt->gtt.ppgtt_mm_lock);

	return mm;
}

static struct intel_vgpu_mm *intel_vgpu_create_ggtt_mm(struct intel_vgpu *vgpu)
{
	struct intel_vgpu_mm *mm;
	unsigned long nr_entries;

	mm = vgpu_alloc_mm(vgpu);
	if (!mm)
		return ERR_PTR(-ENOMEM);

	mm->type = INTEL_GVT_MM_GGTT;

	nr_entries = gvt_ggtt_gm_sz(vgpu->gvt) >> I915_GTT_PAGE_SHIFT;
	mm->ggtt_mm.virtual_ggtt =
		vzalloc(array_size(nr_entries,
				   vgpu->gvt->device_info.gtt_entry_size));
	if (!mm->ggtt_mm.virtual_ggtt) {
		vgpu_free_mm(mm);
		return ERR_PTR(-ENOMEM);
	}

	mm->ggtt_mm.host_ggtt_aperture = vzalloc((vgpu_aperture_sz(vgpu) >> PAGE_SHIFT) * sizeof(u64));
	if (!mm->ggtt_mm.host_ggtt_aperture) {
		vfree(mm->ggtt_mm.virtual_ggtt);
		vgpu_free_mm(mm);
		return ERR_PTR(-ENOMEM);
	}

	mm->ggtt_mm.host_ggtt_hidden = vzalloc((vgpu_hidden_sz(vgpu) >> PAGE_SHIFT) * sizeof(u64));
	if (!mm->ggtt_mm.host_ggtt_hidden) {
		vfree(mm->ggtt_mm.host_ggtt_aperture);
		vfree(mm->ggtt_mm.virtual_ggtt);
		vgpu_free_mm(mm);
		return ERR_PTR(-ENOMEM);
	}

	return mm;
}

/**
 * _intel_vgpu_mm_release - destroy a mm object
 * @mm_ref: a kref object
 *
 * This function is used to destroy a mm object for vGPU
 *
 */
void _intel_vgpu_mm_release(struct kref *mm_ref)
{
	struct intel_vgpu_mm *mm = container_of(mm_ref, typeof(*mm), ref);

	if (GEM_WARN_ON(atomic_read(&mm->pincount)))
		gvt_err("vgpu mm pin count bug detected\n");

	if (mm->type == INTEL_GVT_MM_PPGTT) {
		list_del(&mm->ppgtt_mm.list);

		mutex_lock(&mm->vgpu->gvt->gtt.ppgtt_mm_lock);
		list_del(&mm->ppgtt_mm.lru_list);
		mutex_unlock(&mm->vgpu->gvt->gtt.ppgtt_mm_lock);

		invalidate_ppgtt_mm(mm);
	} else {
		vfree(mm->ggtt_mm.virtual_ggtt);
		vfree(mm->ggtt_mm.host_ggtt_aperture);
		vfree(mm->ggtt_mm.host_ggtt_hidden);
	}

	vgpu_free_mm(mm);
}

/**
 * intel_vgpu_unpin_mm - decrease the pin count of a vGPU mm object
 * @mm: a vGPU mm object
 *
 * This function is called when user doesn't want to use a vGPU mm object
 */
void intel_vgpu_unpin_mm(struct intel_vgpu_mm *mm)
{
	atomic_dec_if_positive(&mm->pincount);
}

/**
 * intel_vgpu_pin_mm - increase the pin count of a vGPU mm object
 * @mm: target vgpu mm
 *
 * This function is called when user wants to use a vGPU mm object. If this
 * mm object hasn't been shadowed yet, the shadow will be populated at this
 * time.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_pin_mm(struct intel_vgpu_mm *mm)
{
	int ret;

	atomic_inc(&mm->pincount);

	if (mm->type == INTEL_GVT_MM_PPGTT) {
		ret = shadow_ppgtt_mm(mm);
		if (ret)
			return ret;

		mutex_lock(&mm->vgpu->gvt->gtt.ppgtt_mm_lock);
		list_move_tail(&mm->ppgtt_mm.lru_list,
			       &mm->vgpu->gvt->gtt.ppgtt_mm_lru_list_head);
		mutex_unlock(&mm->vgpu->gvt->gtt.ppgtt_mm_lock);
	}

	return 0;
}

static int reclaim_one_ppgtt_mm(struct intel_gvt *gvt)
{
	struct intel_vgpu_mm *mm;
	struct list_head *pos, *n;

	mutex_lock(&gvt->gtt.ppgtt_mm_lock);

	list_for_each_safe(pos, n, &gvt->gtt.ppgtt_mm_lru_list_head) {
		mm = container_of(pos, struct intel_vgpu_mm, ppgtt_mm.lru_list);

		if (atomic_read(&mm->pincount))
			continue;

		list_del_init(&mm->ppgtt_mm.lru_list);
		mutex_unlock(&gvt->gtt.ppgtt_mm_lock);
		invalidate_ppgtt_mm(mm);
		return 1;
	}
	mutex_unlock(&gvt->gtt.ppgtt_mm_lock);
	return 0;
}

/*
 * GMA translation APIs.
 */
static inline int ppgtt_get_next_level_entry(struct intel_vgpu_mm *mm,
		struct intel_gvt_gtt_entry *e, unsigned long index, bool guest)
{
	struct intel_vgpu *vgpu = mm->vgpu;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	struct intel_vgpu_ppgtt_spt *s;

	s = intel_vgpu_find_spt_by_mfn(vgpu, ops->get_pfn(e));
	if (!s)
		return -ENXIO;

	if (!guest)
		ppgtt_get_shadow_entry(s, e, index);
	else
		ppgtt_get_guest_entry(s, e, index);
	return 0;
}

/**
 * intel_vgpu_gma_to_gpa - translate a gma to GPA
 * @mm: mm object. could be a PPGTT or GGTT mm object
 * @gma: graphics memory address in this mm object
 *
 * This function is used to translate a graphics memory address in specific
 * graphics memory space to guest physical address.
 *
 * Returns:
 * Guest physical address on success, INTEL_GVT_INVALID_ADDR if failed.
 */
unsigned long intel_vgpu_gma_to_gpa(struct intel_vgpu_mm *mm, unsigned long gma)
{
	struct intel_vgpu *vgpu = mm->vgpu;
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_gtt_pte_ops *pte_ops = gvt->gtt.pte_ops;
	const struct intel_gvt_gtt_gma_ops *gma_ops = gvt->gtt.gma_ops;
	unsigned long gpa = INTEL_GVT_INVALID_ADDR;
	unsigned long gma_index[4];
	struct intel_gvt_gtt_entry e;
	int i, levels = 0;
	int ret;

	GEM_BUG_ON(mm->type != INTEL_GVT_MM_GGTT &&
		   mm->type != INTEL_GVT_MM_PPGTT);

	if (mm->type == INTEL_GVT_MM_GGTT) {
		if (!vgpu_gmadr_is_valid(vgpu, gma))
			goto err;

		ggtt_get_guest_entry(mm, &e,
			gma_ops->gma_to_ggtt_pte_index(gma));

		gpa = (pte_ops->get_pfn(&e) << I915_GTT_PAGE_SHIFT)
			+ (gma & ~I915_GTT_PAGE_MASK);

		trace_gma_translate(vgpu->id, "ggtt", 0, 0, gma, gpa);
	} else {
		switch (mm->ppgtt_mm.root_entry_type) {
		case GTT_TYPE_PPGTT_ROOT_L4_ENTRY:
			ppgtt_get_shadow_root_entry(mm, &e, 0);

			gma_index[0] = gma_ops->gma_to_pml4_index(gma);
			gma_index[1] = gma_ops->gma_to_l4_pdp_index(gma);
			gma_index[2] = gma_ops->gma_to_pde_index(gma);
			gma_index[3] = gma_ops->gma_to_pte_index(gma);
			levels = 4;
			break;
		case GTT_TYPE_PPGTT_ROOT_L3_ENTRY:
			ppgtt_get_shadow_root_entry(mm, &e,
					gma_ops->gma_to_l3_pdp_index(gma));

			gma_index[0] = gma_ops->gma_to_pde_index(gma);
			gma_index[1] = gma_ops->gma_to_pte_index(gma);
			levels = 2;
			break;
		default:
			GEM_BUG_ON(1);
		}

		/* walk the shadow page table and get gpa from guest entry */
		for (i = 0; i < levels; i++) {
			ret = ppgtt_get_next_level_entry(mm, &e, gma_index[i],
				(i == levels - 1));
			if (ret)
				goto err;

			if (!pte_ops->test_present(&e)) {
				gvt_dbg_core("GMA 0x%lx is not present\n", gma);
				goto err;
			}
		}

		gpa = (pte_ops->get_pfn(&e) << I915_GTT_PAGE_SHIFT) +
					(gma & ~I915_GTT_PAGE_MASK);
		trace_gma_translate(vgpu->id, "ppgtt", 0,
				    mm->ppgtt_mm.root_entry_type, gma, gpa);
	}

	return gpa;
err:
	gvt_vgpu_err("invalid mm type: %d gma %lx\n", mm->type, gma);
	return INTEL_GVT_INVALID_ADDR;
}

static int emulate_ggtt_mmio_read(struct intel_vgpu *vgpu,
	unsigned int off, void *p_data, unsigned int bytes)
{
	struct intel_vgpu_mm *ggtt_mm = vgpu->gtt.ggtt_mm;
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	unsigned long index = off >> info->gtt_entry_size_shift;
	unsigned long gma;
	struct intel_gvt_gtt_entry e;

	if (bytes != 4 && bytes != 8)
		return -EINVAL;

	gma = index << I915_GTT_PAGE_SHIFT;
	if (!intel_gvt_ggtt_validate_range(vgpu,
					   gma, 1 << I915_GTT_PAGE_SHIFT)) {
		gvt_dbg_mm("read invalid ggtt at 0x%lx\n", gma);
		memset(p_data, 0, bytes);
		return 0;
	}

	ggtt_get_guest_entry(ggtt_mm, &e, index);
	memcpy(p_data, (void *)&e.val64 + (off & (info->gtt_entry_size - 1)),
			bytes);
	return 0;
}

/**
 * intel_vgpu_emulate_ggtt_mmio_read - emulate GTT MMIO register read
 * @vgpu: a vGPU
 * @off: register offset
 * @p_data: data will be returned to guest
 * @bytes: data length
 *
 * This function is used to emulate the GTT MMIO register read
 *
 * Returns:
 * Zero on success, error code if failed.
 */
int intel_vgpu_emulate_ggtt_mmio_read(struct intel_vgpu *vgpu, unsigned int off,
	void *p_data, unsigned int bytes)
{
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	int ret;

	if (bytes != 4 && bytes != 8)
		return -EINVAL;

	off -= info->gtt_start_offset;
	ret = emulate_ggtt_mmio_read(vgpu, off, p_data, bytes);
	return ret;
}

static void ggtt_invalidate_pte(struct intel_vgpu *vgpu,
		struct intel_gvt_gtt_entry *entry)
{
	const struct intel_gvt_gtt_pte_ops *pte_ops = vgpu->gvt->gtt.pte_ops;
	unsigned long pfn;

	pfn = pte_ops->get_pfn(entry);
	if (pfn != vgpu->gvt->gtt.scratch_mfn)
		intel_gvt_dma_unmap_guest_page(vgpu, pfn << PAGE_SHIFT);
}

static int emulate_ggtt_mmio_write(struct intel_vgpu *vgpu, unsigned int off,
	void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_device_info *info = &gvt->device_info;
	struct intel_vgpu_mm *ggtt_mm = vgpu->gtt.ggtt_mm;
	const struct intel_gvt_gtt_pte_ops *ops = gvt->gtt.pte_ops;
	unsigned long g_gtt_index = off >> info->gtt_entry_size_shift;
	unsigned long gma, gfn;
	struct intel_gvt_gtt_entry e = {.val64 = 0, .type = GTT_TYPE_GGTT_PTE};
	struct intel_gvt_gtt_entry m = {.val64 = 0, .type = GTT_TYPE_GGTT_PTE};
	dma_addr_t dma_addr;
	int ret;
	struct intel_gvt_partial_pte *partial_pte, *pos, *n;
	bool partial_update = false;

	if (bytes != 4 && bytes != 8)
		return -EINVAL;

	gma = g_gtt_index << I915_GTT_PAGE_SHIFT;

	/* the VM may configure the whole GM space when ballooning is used */
	if (!vgpu_gmadr_is_valid(vgpu, gma))
		return 0;

	e.type = GTT_TYPE_GGTT_PTE;
	memcpy((void *)&e.val64 + (off & (info->gtt_entry_size - 1)), p_data,
			bytes);

	/* If ggtt entry size is 8 bytes, and it's split into two 4 bytes
	 * write, save the first 4 bytes in a list and update virtual
	 * PTE. Only update shadow PTE when the second 4 bytes comes.
	 */
	if (bytes < info->gtt_entry_size) {
		bool found = false;

		list_for_each_entry_safe(pos, n,
				&ggtt_mm->ggtt_mm.partial_pte_list, list) {
			if (g_gtt_index == pos->offset >>
					info->gtt_entry_size_shift) {
				if (off != pos->offset) {
					/* the second partial part*/
					int last_off = pos->offset &
						(info->gtt_entry_size - 1);

					memcpy((void *)&e.val64 + last_off,
						(void *)&pos->data + last_off,
						bytes);

					list_del(&pos->list);
					kfree(pos);
					found = true;
					break;
				}

				/* update of the first partial part */
				pos->data = e.val64;
				ggtt_set_guest_entry(ggtt_mm, &e, g_gtt_index);
				return 0;
			}
		}

		if (!found) {
			/* the first partial part */
			partial_pte = kzalloc(sizeof(*partial_pte), GFP_KERNEL);
			if (!partial_pte)
				return -ENOMEM;
			partial_pte->offset = off;
			partial_pte->data = e.val64;
			list_add_tail(&partial_pte->list,
				&ggtt_mm->ggtt_mm.partial_pte_list);
			partial_update = true;
		}
	}

	if (!partial_update && (ops->test_present(&e))) {
		gfn = ops->get_pfn(&e);
		m.val64 = e.val64;
		m.type = e.type;

		ret = intel_gvt_dma_map_guest_page(vgpu, gfn, PAGE_SIZE,
						   &dma_addr);
		if (ret) {
			gvt_vgpu_err("fail to populate guest ggtt entry\n");
			/* guest driver may read/write the entry when partial
			 * update the entry in this situation p2m will fail
			 * setting the shadow entry to point to a scratch page
			 */
			ops->set_pfn(&m, gvt->gtt.scratch_mfn);
		} else
			ops->set_pfn(&m, dma_addr >> PAGE_SHIFT);
	} else {
		ops->set_pfn(&m, gvt->gtt.scratch_mfn);
		ops->clear_present(&m);
	}

	ggtt_set_guest_entry(ggtt_mm, &e, g_gtt_index);

	ggtt_get_host_entry(ggtt_mm, &e, g_gtt_index);
	ggtt_invalidate_pte(vgpu, &e);

	ggtt_set_host_entry(ggtt_mm, &m, g_gtt_index);
	ggtt_invalidate(gvt->gt);
	return 0;
}

/*
 * intel_vgpu_emulate_ggtt_mmio_write - emulate GTT MMIO register write
 * @vgpu: a vGPU
 * @off: register offset
 * @p_data: data from guest write
 * @bytes: data length
 *
 * This function is used to emulate the GTT MMIO register write
 *
 * Returns:
 * Zero on success, error code if failed.
 */
int intel_vgpu_emulate_ggtt_mmio_write(struct intel_vgpu *vgpu,
		unsigned int off, void *p_data, unsigned int bytes)
{
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;
	int ret;
	struct intel_vgpu_submission *s = &vgpu->submission;
	struct intel_engine_cs *engine;
	int i;

	if (bytes != 4 && bytes != 8)
		return -EINVAL;

	off -= info->gtt_start_offset;
	ret = emulate_ggtt_mmio_write(vgpu, off, p_data, bytes);

	/* if ggtt of last submitted context is written,
	 * that context is probably got unpinned.
	 * Set last shadowed ctx to invalid.
	 */
	for_each_engine(engine, vgpu->gvt->gt, i) {
		if (!s->last_ctx[i].valid)
			continue;

		if (s->last_ctx[i].lrca == (off >> info->gtt_entry_size_shift))
			s->last_ctx[i].valid = false;
	}
	return ret;
}

static int alloc_scratch_pages(struct intel_vgpu *vgpu,
		enum intel_gvt_gtt_type type)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	struct intel_vgpu_gtt *gtt = &vgpu->gtt;
	const struct intel_gvt_gtt_pte_ops *ops = vgpu->gvt->gtt.pte_ops;
	int page_entry_num = I915_GTT_PAGE_SIZE >>
				vgpu->gvt->device_info.gtt_entry_size_shift;
	void *scratch_pt;
	int i;
	struct device *dev = vgpu->gvt->gt->i915->drm.dev;
	dma_addr_t daddr;

	if (drm_WARN_ON(&i915->drm,
			type < GTT_TYPE_PPGTT_PTE_PT || type >= GTT_TYPE_MAX))
		return -EINVAL;

	scratch_pt = (void *)get_zeroed_page(GFP_KERNEL);
	if (!scratch_pt) {
		gvt_vgpu_err("fail to allocate scratch page\n");
		return -ENOMEM;
	}

	daddr = dma_map_page(dev, virt_to_page(scratch_pt), 0, 4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, daddr)) {
		gvt_vgpu_err("fail to dmamap scratch_pt\n");
		__free_page(virt_to_page(scratch_pt));
		return -ENOMEM;
	}
	gtt->scratch_pt[type].page_mfn =
		(unsigned long)(daddr >> I915_GTT_PAGE_SHIFT);
	gtt->scratch_pt[type].page = virt_to_page(scratch_pt);
	gvt_dbg_mm("vgpu%d create scratch_pt: type %d mfn=0x%lx\n",
			vgpu->id, type, gtt->scratch_pt[type].page_mfn);

	/* Build the tree by full filled the scratch pt with the entries which
	 * point to the next level scratch pt or scratch page. The
	 * scratch_pt[type] indicate the scratch pt/scratch page used by the
	 * 'type' pt.
	 * e.g. scratch_pt[GTT_TYPE_PPGTT_PDE_PT] is used by
	 * GTT_TYPE_PPGTT_PDE_PT level pt, that means this scratch_pt it self
	 * is GTT_TYPE_PPGTT_PTE_PT, and full filled by scratch page mfn.
	 */
	if (type > GTT_TYPE_PPGTT_PTE_PT) {
		struct intel_gvt_gtt_entry se;

		memset(&se, 0, sizeof(struct intel_gvt_gtt_entry));
		se.type = get_entry_type(type - 1);
		ops->set_pfn(&se, gtt->scratch_pt[type - 1].page_mfn);

		/* The entry parameters like present/writeable/cache type
		 * set to the same as i915's scratch page tree.
		 */
		se.val64 |= GEN8_PAGE_PRESENT | GEN8_PAGE_RW;
		if (type == GTT_TYPE_PPGTT_PDE_PT)
			se.val64 |= PPAT_CACHED;

		for (i = 0; i < page_entry_num; i++)
			ops->set_entry(scratch_pt, &se, i, false, 0, vgpu);
	}

	return 0;
}

static int release_scratch_page_tree(struct intel_vgpu *vgpu)
{
	int i;
	struct device *dev = vgpu->gvt->gt->i915->drm.dev;
	dma_addr_t daddr;

	for (i = GTT_TYPE_PPGTT_PTE_PT; i < GTT_TYPE_MAX; i++) {
		if (vgpu->gtt.scratch_pt[i].page != NULL) {
			daddr = (dma_addr_t)(vgpu->gtt.scratch_pt[i].page_mfn <<
					I915_GTT_PAGE_SHIFT);
			dma_unmap_page(dev, daddr, 4096, DMA_BIDIRECTIONAL);
			__free_page(vgpu->gtt.scratch_pt[i].page);
			vgpu->gtt.scratch_pt[i].page = NULL;
			vgpu->gtt.scratch_pt[i].page_mfn = 0;
		}
	}

	return 0;
}

static int create_scratch_page_tree(struct intel_vgpu *vgpu)
{
	int i, ret;

	for (i = GTT_TYPE_PPGTT_PTE_PT; i < GTT_TYPE_MAX; i++) {
		ret = alloc_scratch_pages(vgpu, i);
		if (ret)
			goto err;
	}

	return 0;

err:
	release_scratch_page_tree(vgpu);
	return ret;
}

/**
 * intel_vgpu_init_gtt - initialize per-vGPU graphics memory virulization
 * @vgpu: a vGPU
 *
 * This function is used to initialize per-vGPU graphics memory virtualization
 * components.
 *
 * Returns:
 * Zero on success, error code if failed.
 */
int intel_vgpu_init_gtt(struct intel_vgpu *vgpu)
{
	struct intel_vgpu_gtt *gtt = &vgpu->gtt;

	INIT_RADIX_TREE(&gtt->spt_tree, GFP_KERNEL);

	INIT_LIST_HEAD(&gtt->ppgtt_mm_list_head);
	INIT_LIST_HEAD(&gtt->oos_page_list_head);
	INIT_LIST_HEAD(&gtt->post_shadow_list_head);

	gtt->ggtt_mm = intel_vgpu_create_ggtt_mm(vgpu);
	if (IS_ERR(gtt->ggtt_mm)) {
		gvt_vgpu_err("fail to create mm for ggtt.\n");
		return PTR_ERR(gtt->ggtt_mm);
	}

	intel_vgpu_reset_ggtt(vgpu, false);

	INIT_LIST_HEAD(&gtt->ggtt_mm->ggtt_mm.partial_pte_list);

	return create_scratch_page_tree(vgpu);
}

void intel_vgpu_destroy_all_ppgtt_mm(struct intel_vgpu *vgpu)
{
	struct list_head *pos, *n;
	struct intel_vgpu_mm *mm;

	list_for_each_safe(pos, n, &vgpu->gtt.ppgtt_mm_list_head) {
		mm = container_of(pos, struct intel_vgpu_mm, ppgtt_mm.list);
		intel_vgpu_destroy_mm(mm);
	}

	if (GEM_WARN_ON(!list_empty(&vgpu->gtt.ppgtt_mm_list_head)))
		gvt_err("vgpu ppgtt mm is not fully destroyed\n");

	if (GEM_WARN_ON(!radix_tree_empty(&vgpu->gtt.spt_tree))) {
		gvt_err("Why we still has spt not freed?\n");
		ppgtt_free_all_spt(vgpu);
	}
}

static void intel_vgpu_destroy_ggtt_mm(struct intel_vgpu *vgpu)
{
	struct intel_gvt_partial_pte *pos, *next;

	list_for_each_entry_safe(pos, next,
				 &vgpu->gtt.ggtt_mm->ggtt_mm.partial_pte_list,
				 list) {
		gvt_dbg_mm("partial PTE update on hold 0x%lx : 0x%llx\n",
			pos->offset, pos->data);
		kfree(pos);
	}
	intel_vgpu_destroy_mm(vgpu->gtt.ggtt_mm);
	vgpu->gtt.ggtt_mm = NULL;
}

/**
 * intel_vgpu_clean_gtt - clean up per-vGPU graphics memory virulization
 * @vgpu: a vGPU
 *
 * This function is used to clean up per-vGPU graphics memory virtualization
 * components.
 *
 * Returns:
 * Zero on success, error code if failed.
 */
void intel_vgpu_clean_gtt(struct intel_vgpu *vgpu)
{
	intel_vgpu_destroy_all_ppgtt_mm(vgpu);
	intel_vgpu_destroy_ggtt_mm(vgpu);
	release_scratch_page_tree(vgpu);
}

static void clean_spt_oos(struct intel_gvt *gvt)
{
	struct intel_gvt_gtt *gtt = &gvt->gtt;
	struct list_head *pos, *n;
	struct intel_vgpu_oos_page *oos_page;

	WARN(!list_empty(&gtt->oos_page_use_list_head),
		"someone is still using oos page\n");

	list_for_each_safe(pos, n, &gtt->oos_page_free_list_head) {
		oos_page = container_of(pos, struct intel_vgpu_oos_page, list);
		list_del(&oos_page->list);
		free_page((unsigned long)oos_page->mem);
		kfree(oos_page);
	}
}

static int setup_spt_oos(struct intel_gvt *gvt)
{
	struct intel_gvt_gtt *gtt = &gvt->gtt;
	struct intel_vgpu_oos_page *oos_page;
	int i;
	int ret;

	INIT_LIST_HEAD(&gtt->oos_page_free_list_head);
	INIT_LIST_HEAD(&gtt->oos_page_use_list_head);

	for (i = 0; i < preallocated_oos_pages; i++) {
		oos_page = kzalloc(sizeof(*oos_page), GFP_KERNEL);
		if (!oos_page) {
			ret = -ENOMEM;
			goto fail;
		}
		oos_page->mem = (void *)__get_free_pages(GFP_KERNEL, 0);
		if (!oos_page->mem) {
			ret = -ENOMEM;
			kfree(oos_page);
			goto fail;
		}

		INIT_LIST_HEAD(&oos_page->list);
		INIT_LIST_HEAD(&oos_page->vm_list);
		oos_page->id = i;
		list_add_tail(&oos_page->list, &gtt->oos_page_free_list_head);
	}

	gvt_dbg_mm("%d oos pages preallocated\n", i);

	return 0;
fail:
	clean_spt_oos(gvt);
	return ret;
}

/**
 * intel_vgpu_find_ppgtt_mm - find a PPGTT mm object
 * @vgpu: a vGPU
 * @pdps: pdp root array
 *
 * This function is used to find a PPGTT mm object from mm object pool
 *
 * Returns:
 * pointer to mm object on success, NULL if failed.
 */
struct intel_vgpu_mm *intel_vgpu_find_ppgtt_mm(struct intel_vgpu *vgpu,
		u64 pdps[])
{
	struct intel_vgpu_mm *mm;
	struct list_head *pos;

	list_for_each(pos, &vgpu->gtt.ppgtt_mm_list_head) {
		mm = container_of(pos, struct intel_vgpu_mm, ppgtt_mm.list);

		switch (mm->ppgtt_mm.root_entry_type) {
		case GTT_TYPE_PPGTT_ROOT_L4_ENTRY:
			if (pdps[0] == mm->ppgtt_mm.guest_pdps[0])
				return mm;
			break;
		case GTT_TYPE_PPGTT_ROOT_L3_ENTRY:
			if (!memcmp(pdps, mm->ppgtt_mm.guest_pdps,
				    sizeof(mm->ppgtt_mm.guest_pdps)))
				return mm;
			break;
		default:
			GEM_BUG_ON(1);
		}
	}
	return NULL;
}

/**
 * intel_vgpu_get_ppgtt_mm - get or create a PPGTT mm object.
 * @vgpu: a vGPU
 * @root_entry_type: ppgtt root entry type
 * @pdps: guest pdps
 *
 * This function is used to find or create a PPGTT mm object from a guest.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
struct intel_vgpu_mm *intel_vgpu_get_ppgtt_mm(struct intel_vgpu *vgpu,
		enum intel_gvt_gtt_type root_entry_type, u64 pdps[])
{
	struct intel_vgpu_mm *mm;

	mm = intel_vgpu_find_ppgtt_mm(vgpu, pdps);
	if (mm) {
		intel_vgpu_mm_get(mm);
	} else {
		mm = intel_vgpu_create_ppgtt_mm(vgpu, root_entry_type, pdps);
		if (IS_ERR(mm))
			gvt_vgpu_err("fail to create mm\n");
	}
	return mm;
}

/**
 * intel_vgpu_put_ppgtt_mm - find and put a PPGTT mm object.
 * @vgpu: a vGPU
 * @pdps: guest pdps
 *
 * This function is used to find a PPGTT mm object from a guest and destroy it.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_put_ppgtt_mm(struct intel_vgpu *vgpu, u64 pdps[])
{
	struct intel_vgpu_mm *mm;

	mm = intel_vgpu_find_ppgtt_mm(vgpu, pdps);
	if (!mm) {
		gvt_vgpu_err("fail to find ppgtt instance.\n");
		return -EINVAL;
	}
	intel_vgpu_mm_put(mm);
	return 0;
}

/**
 * intel_gvt_init_gtt - initialize mm components of a GVT device
 * @gvt: GVT device
 *
 * This function is called at the initialization stage, to initialize
 * the mm components of a GVT device.
 *
 * Returns:
 * zero on success, negative error code if failed.
 */
int intel_gvt_init_gtt(struct intel_gvt *gvt)
{
	int ret;
	void *page;
	struct device *dev = gvt->gt->i915->drm.dev;
	dma_addr_t daddr;

	gvt_dbg_core("init gtt\n");

	gvt->gtt.pte_ops = &gen8_gtt_pte_ops;
	gvt->gtt.gma_ops = &gen8_gtt_gma_ops;

	page = (void *)get_zeroed_page(GFP_KERNEL);
	if (!page) {
		gvt_err("fail to allocate scratch ggtt page\n");
		return -ENOMEM;
	}

	daddr = dma_map_page(dev, virt_to_page(page), 0,
			4096, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, daddr)) {
		gvt_err("fail to dmamap scratch ggtt page\n");
		__free_page(virt_to_page(page));
		return -ENOMEM;
	}

	gvt->gtt.scratch_page = virt_to_page(page);
	gvt->gtt.scratch_mfn = (unsigned long)(daddr >> I915_GTT_PAGE_SHIFT);

	if (enable_out_of_sync) {
		ret = setup_spt_oos(gvt);
		if (ret) {
			gvt_err("fail to initialize SPT oos\n");
			dma_unmap_page(dev, daddr, 4096, DMA_BIDIRECTIONAL);
			__free_page(gvt->gtt.scratch_page);
			return ret;
		}
	}
	INIT_LIST_HEAD(&gvt->gtt.ppgtt_mm_lru_list_head);
	mutex_init(&gvt->gtt.ppgtt_mm_lock);
	return 0;
}

/**
 * intel_gvt_clean_gtt - clean up mm components of a GVT device
 * @gvt: GVT device
 *
 * This function is called at the driver unloading stage, to clean up
 * the mm components of a GVT device.
 *
 */
void intel_gvt_clean_gtt(struct intel_gvt *gvt)
{
	struct device *dev = gvt->gt->i915->drm.dev;
	dma_addr_t daddr = (dma_addr_t)(gvt->gtt.scratch_mfn <<
					I915_GTT_PAGE_SHIFT);

	dma_unmap_page(dev, daddr, 4096, DMA_BIDIRECTIONAL);

	__free_page(gvt->gtt.scratch_page);

	if (enable_out_of_sync)
		clean_spt_oos(gvt);
}

/**
 * intel_vgpu_invalidate_ppgtt - invalidate PPGTT instances
 * @vgpu: a vGPU
 *
 * This function is called when invalidate all PPGTT instances of a vGPU.
 *
 */
void intel_vgpu_invalidate_ppgtt(struct intel_vgpu *vgpu)
{
	struct list_head *pos, *n;
	struct intel_vgpu_mm *mm;

	list_for_each_safe(pos, n, &vgpu->gtt.ppgtt_mm_list_head) {
		mm = container_of(pos, struct intel_vgpu_mm, ppgtt_mm.list);
		if (mm->type == INTEL_GVT_MM_PPGTT) {
			mutex_lock(&vgpu->gvt->gtt.ppgtt_mm_lock);
			list_del_init(&mm->ppgtt_mm.lru_list);
			mutex_unlock(&vgpu->gvt->gtt.ppgtt_mm_lock);
			if (mm->ppgtt_mm.shadowed)
				invalidate_ppgtt_mm(mm);
		}
	}
}

/**
 * intel_vgpu_reset_ggtt - reset the GGTT entry
 * @vgpu: a vGPU
 * @invalidate_old: invalidate old entries
 *
 * This function is called at the vGPU create stage
 * to reset all the GGTT entries.
 *
 */
void intel_vgpu_reset_ggtt(struct intel_vgpu *vgpu, bool invalidate_old)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_gtt_pte_ops *pte_ops = vgpu->gvt->gtt.pte_ops;
	struct intel_gvt_gtt_entry entry = {.type = GTT_TYPE_GGTT_PTE};
	struct intel_gvt_gtt_entry old_entry;
	u32 index;
	u32 num_entries;

	pte_ops->set_pfn(&entry, gvt->gtt.scratch_mfn);
	pte_ops->set_present(&entry);

	index = vgpu_aperture_gmadr_base(vgpu) >> PAGE_SHIFT;
	num_entries = vgpu_aperture_sz(vgpu) >> PAGE_SHIFT;
	while (num_entries--) {
		if (invalidate_old) {
			ggtt_get_host_entry(vgpu->gtt.ggtt_mm, &old_entry, index);
			ggtt_invalidate_pte(vgpu, &old_entry);
		}
		ggtt_set_host_entry(vgpu->gtt.ggtt_mm, &entry, index++);
	}

	index = vgpu_hidden_gmadr_base(vgpu) >> PAGE_SHIFT;
	num_entries = vgpu_hidden_sz(vgpu) >> PAGE_SHIFT;
	while (num_entries--) {
		if (invalidate_old) {
			ggtt_get_host_entry(vgpu->gtt.ggtt_mm, &old_entry, index);
			ggtt_invalidate_pte(vgpu, &old_entry);
		}
		ggtt_set_host_entry(vgpu->gtt.ggtt_mm, &entry, index++);
	}

	ggtt_invalidate(gvt->gt);
}

/**
 * intel_gvt_restore_ggtt - restore all vGPU's ggtt entries
 * @gvt: intel gvt device
 *
 * This function is called at driver resume stage to restore
 * GGTT entries of every vGPU.
 *
 */
void intel_gvt_restore_ggtt(struct intel_gvt *gvt)
{
	struct intel_vgpu *vgpu;
	struct intel_vgpu_mm *mm;
	int id;
	gen8_pte_t pte;
	u32 idx, num_low, num_hi, offset;

	/* Restore dirty host ggtt for all vGPUs */
	idr_for_each_entry(&(gvt)->vgpu_idr, vgpu, id) {
		mm = vgpu->gtt.ggtt_mm;

		num_low = vgpu_aperture_sz(vgpu) >> PAGE_SHIFT;
		offset = vgpu_aperture_gmadr_base(vgpu) >> PAGE_SHIFT;
		for (idx = 0; idx < num_low; idx++) {
			pte = mm->ggtt_mm.host_ggtt_aperture[idx];
			if (pte & GEN8_PAGE_PRESENT)
				write_pte64(vgpu->gvt->gt->ggtt, offset + idx, pte);
		}

		num_hi = vgpu_hidden_sz(vgpu) >> PAGE_SHIFT;
		offset = vgpu_hidden_gmadr_base(vgpu) >> PAGE_SHIFT;
		for (idx = 0; idx < num_hi; idx++) {
			pte = mm->ggtt_mm.host_ggtt_hidden[idx];
			if (pte & GEN8_PAGE_PRESENT)
				write_pte64(vgpu->gvt->gt->ggtt, offset + idx, pte);
		}
	}
}
