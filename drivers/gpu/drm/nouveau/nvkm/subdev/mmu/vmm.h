#ifndef __NVKM_VMM_H__
#define __NVKM_VMM_H__
#include "priv.h"
#include <core/memory.h>

struct nvkm_vmm_pt {
	/* Some GPUs have a mapping level with a dual page tables to
	 * support large and small pages in the same address-range.
	 *
	 * We track the state of both page tables in one place, which
	 * is why there's multiple PT pointers/refcounts here.
	 */
	struct nvkm_mmu_pt *pt[2];
	u32 refs[2];

	/* Page size handled by this PT.
	 *
	 * Tesla backend needs to know this when writinge PDEs,
	 * otherwise unnecessary.
	 */
	u8 page;

	/* Entire page table sparse.
	 *
	 * Used to propagate sparseness to child page tables.
	 */
	bool sparse:1;

	/* Tracking for page directories.
	 *
	 * The array is indexed by PDE, and will either point to the
	 * child page table, or indicate the PDE is marked as sparse.
	 **/
#define NVKM_VMM_PDE_INVALID(pde) IS_ERR_OR_NULL(pde)
#define NVKM_VMM_PDE_SPARSED(pde) IS_ERR(pde)
#define NVKM_VMM_PDE_SPARSE       ERR_PTR(-EBUSY)
	struct nvkm_vmm_pt **pde;

	/* Tracking for dual page tables.
	 *
	 * There's one entry for each LPTE, keeping track of whether
	 * there are valid SPTEs in the same address-range.
	 *
	 * This information is used to manage LPTE state transitions.
	 */
#define NVKM_VMM_PTE_SPARSE 0x80
#define NVKM_VMM_PTE_VALID  0x40
#define NVKM_VMM_PTE_SPTES  0x3f
	u8 pte[];
};

struct nvkm_vmm_desc_func {
};

struct nvkm_vmm_desc {
	enum {
		PGD,
		PGT,
		SPT,
		LPT,
	} type;
	u8 bits;	/* VMA bits covered by PT. */
	u8 size;	/* Bytes-per-PTE. */
	u32 align;	/* PT address alignment. */
	const struct nvkm_vmm_desc_func *func;
};

struct nvkm_vmm_page {
	u8 shift;
	const struct nvkm_vmm_desc *desc;
#define NVKM_VMM_PAGE_SPARSE                                               0x01
#define NVKM_VMM_PAGE_VRAM                                                 0x02
#define NVKM_VMM_PAGE_HOST                                                 0x04
#define NVKM_VMM_PAGE_COMP                                                 0x08
#define NVKM_VMM_PAGE_Sxxx                                (NVKM_VMM_PAGE_SPARSE)
#define NVKM_VMM_PAGE_xVxx                                  (NVKM_VMM_PAGE_VRAM)
#define NVKM_VMM_PAGE_SVxx             (NVKM_VMM_PAGE_Sxxx | NVKM_VMM_PAGE_VRAM)
#define NVKM_VMM_PAGE_xxHx                                  (NVKM_VMM_PAGE_HOST)
#define NVKM_VMM_PAGE_SxHx             (NVKM_VMM_PAGE_Sxxx | NVKM_VMM_PAGE_HOST)
#define NVKM_VMM_PAGE_xVHx             (NVKM_VMM_PAGE_xVxx | NVKM_VMM_PAGE_HOST)
#define NVKM_VMM_PAGE_SVHx             (NVKM_VMM_PAGE_SVxx | NVKM_VMM_PAGE_HOST)
#define NVKM_VMM_PAGE_xVxC             (NVKM_VMM_PAGE_xVxx | NVKM_VMM_PAGE_COMP)
#define NVKM_VMM_PAGE_SVxC             (NVKM_VMM_PAGE_SVxx | NVKM_VMM_PAGE_COMP)
#define NVKM_VMM_PAGE_xxHC             (NVKM_VMM_PAGE_xxHx | NVKM_VMM_PAGE_COMP)
#define NVKM_VMM_PAGE_SxHC             (NVKM_VMM_PAGE_SxHx | NVKM_VMM_PAGE_COMP)
	u8 type;
};

struct nvkm_vmm_func {
	int (*join)(struct nvkm_vmm *, struct nvkm_memory *inst);
	void (*part)(struct nvkm_vmm *, struct nvkm_memory *inst);

	u64 page_block;
	const struct nvkm_vmm_page page[];
};

struct nvkm_vmm_join {
	struct nvkm_memory *inst;
	struct list_head head;
};

int nvkm_vmm_new_(const struct nvkm_vmm_func *, struct nvkm_mmu *,
		  u32 pd_header, u64 addr, u64 size, struct lock_class_key *,
		  const char *name, struct nvkm_vmm **);
int nvkm_vmm_ctor(const struct nvkm_vmm_func *, struct nvkm_mmu *,
		  u32 pd_header, u64 addr, u64 size, struct lock_class_key *,
		  const char *name, struct nvkm_vmm *);
void nvkm_vmm_dtor(struct nvkm_vmm *);

int nv04_vmm_new_(const struct nvkm_vmm_func *, struct nvkm_mmu *, u32,
		  u64, u64, void *, u32, struct lock_class_key *,
		  const char *, struct nvkm_vmm **);

int nv04_vmm_new(struct nvkm_mmu *, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int nv41_vmm_new(struct nvkm_mmu *, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int nv44_vmm_new(struct nvkm_mmu *, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int nv50_vmm_new(struct nvkm_mmu *, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int g84_vmm_new(struct nvkm_mmu *, u64, u64, void *, u32,
		struct lock_class_key *, const char *, struct nvkm_vmm **);
#endif
