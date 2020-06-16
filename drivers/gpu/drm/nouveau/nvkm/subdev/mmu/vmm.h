#ifndef __NVKM_VMM_H__
#define __NVKM_VMM_H__
#include "priv.h"
#include <core/memory.h>
enum nvkm_memory_target;

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

typedef void (*nvkm_vmm_pxe_func)(struct nvkm_vmm *,
				  struct nvkm_mmu_pt *, u32 ptei, u32 ptes);
typedef void (*nvkm_vmm_pde_func)(struct nvkm_vmm *,
				  struct nvkm_vmm_pt *, u32 pdei);
typedef void (*nvkm_vmm_pte_func)(struct nvkm_vmm *, struct nvkm_mmu_pt *,
				  u32 ptei, u32 ptes, struct nvkm_vmm_map *);

struct nvkm_vmm_desc_func {
	nvkm_vmm_pxe_func invalid;
	nvkm_vmm_pxe_func unmap;
	nvkm_vmm_pxe_func sparse;

	nvkm_vmm_pde_func pde;

	nvkm_vmm_pte_func mem;
	nvkm_vmm_pte_func dma;
	nvkm_vmm_pte_func sgl;

	nvkm_vmm_pte_func pfn;
	bool (*pfn_clear)(struct nvkm_vmm *, struct nvkm_mmu_pt *, u32 ptei, u32 ptes);
	nvkm_vmm_pxe_func pfn_unmap;
};

extern const struct nvkm_vmm_desc_func gf100_vmm_pgd;
void gf100_vmm_pgd_pde(struct nvkm_vmm *, struct nvkm_vmm_pt *, u32);
extern const struct nvkm_vmm_desc_func gf100_vmm_pgt;
void gf100_vmm_pgt_unmap(struct nvkm_vmm *, struct nvkm_mmu_pt *, u32, u32);
void gf100_vmm_pgt_mem(struct nvkm_vmm *, struct nvkm_mmu_pt *, u32, u32,
		       struct nvkm_vmm_map *);
void gf100_vmm_pgt_dma(struct nvkm_vmm *, struct nvkm_mmu_pt *, u32, u32,
		       struct nvkm_vmm_map *);
void gf100_vmm_pgt_sgl(struct nvkm_vmm *, struct nvkm_mmu_pt *, u32, u32,
		       struct nvkm_vmm_map *);

void gk104_vmm_lpt_invalid(struct nvkm_vmm *, struct nvkm_mmu_pt *, u32, u32);

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

extern const struct nvkm_vmm_desc nv50_vmm_desc_12[];
extern const struct nvkm_vmm_desc nv50_vmm_desc_16[];

extern const struct nvkm_vmm_desc gk104_vmm_desc_16_12[];
extern const struct nvkm_vmm_desc gk104_vmm_desc_16_16[];
extern const struct nvkm_vmm_desc gk104_vmm_desc_17_12[];
extern const struct nvkm_vmm_desc gk104_vmm_desc_17_17[];

extern const struct nvkm_vmm_desc gm200_vmm_desc_16_12[];
extern const struct nvkm_vmm_desc gm200_vmm_desc_16_16[];
extern const struct nvkm_vmm_desc gm200_vmm_desc_17_12[];
extern const struct nvkm_vmm_desc gm200_vmm_desc_17_17[];

extern const struct nvkm_vmm_desc gp100_vmm_desc_12[];
extern const struct nvkm_vmm_desc gp100_vmm_desc_16[];

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

	int (*aper)(enum nvkm_memory_target);
	int (*valid)(struct nvkm_vmm *, void *argv, u32 argc,
		     struct nvkm_vmm_map *);
	void (*flush)(struct nvkm_vmm *, int depth);

	int (*mthd)(struct nvkm_vmm *, struct nvkm_client *,
		    u32 mthd, void *argv, u32 argc);

	void (*invalidate_pdb)(struct nvkm_vmm *, u64 addr);

	u64 page_block;
	const struct nvkm_vmm_page page[];
};

struct nvkm_vmm_join {
	struct nvkm_memory *inst;
	struct list_head head;
};

int nvkm_vmm_new_(const struct nvkm_vmm_func *, struct nvkm_mmu *,
		  u32 pd_header, bool managed, u64 addr, u64 size,
		  struct lock_class_key *, const char *name,
		  struct nvkm_vmm **);
int nvkm_vmm_ctor(const struct nvkm_vmm_func *, struct nvkm_mmu *,
		  u32 pd_header, bool managed, u64 addr, u64 size,
		  struct lock_class_key *, const char *name, struct nvkm_vmm *);
struct nvkm_vma *nvkm_vmm_node_search(struct nvkm_vmm *, u64 addr);
struct nvkm_vma *nvkm_vmm_node_split(struct nvkm_vmm *, struct nvkm_vma *,
				     u64 addr, u64 size);
int nvkm_vmm_get_locked(struct nvkm_vmm *, bool getref, bool mapref,
			bool sparse, u8 page, u8 align, u64 size,
			struct nvkm_vma **pvma);
void nvkm_vmm_put_locked(struct nvkm_vmm *, struct nvkm_vma *);
void nvkm_vmm_unmap_locked(struct nvkm_vmm *, struct nvkm_vma *, bool pfn);
void nvkm_vmm_unmap_region(struct nvkm_vmm *, struct nvkm_vma *);

#define NVKM_VMM_PFN_ADDR                                 0xfffffffffffff000ULL
#define NVKM_VMM_PFN_ADDR_SHIFT                                              12
#define NVKM_VMM_PFN_APER                                 0x00000000000000f0ULL
#define NVKM_VMM_PFN_HOST                                 0x0000000000000000ULL
#define NVKM_VMM_PFN_VRAM                                 0x0000000000000010ULL
#define NVKM_VMM_PFN_W                                    0x0000000000000002ULL
#define NVKM_VMM_PFN_V                                    0x0000000000000001ULL
#define NVKM_VMM_PFN_NONE                                 0x0000000000000000ULL

int nvkm_vmm_pfn_map(struct nvkm_vmm *, u8 page, u64 addr, u64 size, u64 *pfn);
int nvkm_vmm_pfn_unmap(struct nvkm_vmm *, u64 addr, u64 size);

struct nvkm_vma *nvkm_vma_tail(struct nvkm_vma *, u64 tail);

int nv04_vmm_new_(const struct nvkm_vmm_func *, struct nvkm_mmu *, u32,
		  bool, u64, u64, void *, u32, struct lock_class_key *,
		  const char *, struct nvkm_vmm **);
int nv04_vmm_valid(struct nvkm_vmm *, void *, u32, struct nvkm_vmm_map *);

int nv50_vmm_join(struct nvkm_vmm *, struct nvkm_memory *);
void nv50_vmm_part(struct nvkm_vmm *, struct nvkm_memory *);
int nv50_vmm_valid(struct nvkm_vmm *, void *, u32, struct nvkm_vmm_map *);
void nv50_vmm_flush(struct nvkm_vmm *, int);

int gf100_vmm_new_(const struct nvkm_vmm_func *, const struct nvkm_vmm_func *,
		   struct nvkm_mmu *, bool, u64, u64, void *, u32,
		   struct lock_class_key *, const char *, struct nvkm_vmm **);
int gf100_vmm_join_(struct nvkm_vmm *, struct nvkm_memory *, u64 base);
int gf100_vmm_join(struct nvkm_vmm *, struct nvkm_memory *);
void gf100_vmm_part(struct nvkm_vmm *, struct nvkm_memory *);
int gf100_vmm_aper(enum nvkm_memory_target);
int gf100_vmm_valid(struct nvkm_vmm *, void *, u32, struct nvkm_vmm_map *);
void gf100_vmm_flush(struct nvkm_vmm *, int);
void gf100_vmm_invalidate(struct nvkm_vmm *, u32 type);
void gf100_vmm_invalidate_pdb(struct nvkm_vmm *, u64 addr);

int gk20a_vmm_aper(enum nvkm_memory_target);

int gm200_vmm_new_(const struct nvkm_vmm_func *, const struct nvkm_vmm_func *,
		   struct nvkm_mmu *, bool, u64, u64, void *, u32,
		   struct lock_class_key *, const char *, struct nvkm_vmm **);
int gm200_vmm_join_(struct nvkm_vmm *, struct nvkm_memory *, u64 base);
int gm200_vmm_join(struct nvkm_vmm *, struct nvkm_memory *);

int gp100_vmm_new_(const struct nvkm_vmm_func *,
		   struct nvkm_mmu *, bool, u64, u64, void *, u32,
		   struct lock_class_key *, const char *, struct nvkm_vmm **);
int gp100_vmm_join(struct nvkm_vmm *, struct nvkm_memory *);
int gp100_vmm_valid(struct nvkm_vmm *, void *, u32, struct nvkm_vmm_map *);
void gp100_vmm_flush(struct nvkm_vmm *, int);
int gp100_vmm_mthd(struct nvkm_vmm *, struct nvkm_client *, u32, void *, u32);
void gp100_vmm_invalidate_pdb(struct nvkm_vmm *, u64 addr);

int gv100_vmm_join(struct nvkm_vmm *, struct nvkm_memory *);

int nv04_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int nv41_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int nv44_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int nv50_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		 struct lock_class_key *, const char *, struct nvkm_vmm **);
int mcp77_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *, struct nvkm_vmm **);
int g84_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		struct lock_class_key *, const char *, struct nvkm_vmm **);
int gf100_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *, struct nvkm_vmm **);
int gk104_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *, struct nvkm_vmm **);
int gk20a_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *, struct nvkm_vmm **);
int gm200_vmm_new_fixed(struct nvkm_mmu *, bool, u64, u64, void *, u32,
			struct lock_class_key *, const char *,
			struct nvkm_vmm **);
int gm200_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *,
		  struct nvkm_vmm **);
int gm20b_vmm_new_fixed(struct nvkm_mmu *, bool, u64, u64, void *, u32,
			struct lock_class_key *, const char *,
			struct nvkm_vmm **);
int gm20b_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *,
		  struct nvkm_vmm **);
int gp100_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *,
		  struct nvkm_vmm **);
int gp10b_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *,
		  struct nvkm_vmm **);
int gv100_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *,
		  struct nvkm_vmm **);
int tu102_vmm_new(struct nvkm_mmu *, bool, u64, u64, void *, u32,
		  struct lock_class_key *, const char *,
		  struct nvkm_vmm **);

#define VMM_PRINT(l,v,p,f,a...) do {                                           \
	struct nvkm_vmm *_vmm = (v);                                           \
	if (CONFIG_NOUVEAU_DEBUG >= (l) && _vmm->debug >= (l)) {               \
		nvkm_printk_(&_vmm->mmu->subdev, 0, p, "%s: "f"\n",            \
			     _vmm->name, ##a);                                 \
	}                                                                      \
} while(0)
#define VMM_DEBUG(v,f,a...) VMM_PRINT(NV_DBG_DEBUG, (v), info, f, ##a)
#define VMM_TRACE(v,f,a...) VMM_PRINT(NV_DBG_TRACE, (v), info, f, ##a)
#define VMM_SPAM(v,f,a...)  VMM_PRINT(NV_DBG_SPAM , (v),  dbg, f, ##a)

#define VMM_MAP_ITER(VMM,PT,PTEI,PTEN,MAP,FILL,BASE,SIZE,NEXT) do {            \
	nvkm_kmap((PT)->memory);                                               \
	while (PTEN) {                                                         \
		u64 _ptes = ((SIZE) - MAP->off) >> MAP->page->shift;           \
		u64 _addr = ((BASE) + MAP->off);                               \
                                                                               \
		if (_ptes > PTEN) {                                            \
			MAP->off += PTEN << MAP->page->shift;                  \
			_ptes = PTEN;                                          \
		} else {                                                       \
			MAP->off = 0;                                          \
			NEXT;                                                  \
		}                                                              \
                                                                               \
		VMM_SPAM(VMM, "ITER %08x %08x PTE(s)", PTEI, (u32)_ptes);      \
                                                                               \
		FILL(VMM, PT, PTEI, _ptes, MAP, _addr);                        \
		PTEI += _ptes;                                                 \
		PTEN -= _ptes;                                                 \
	}                                                                      \
	nvkm_done((PT)->memory);                                               \
} while(0)

#define VMM_MAP_ITER_MEM(VMM,PT,PTEI,PTEN,MAP,FILL)                            \
	VMM_MAP_ITER(VMM,PT,PTEI,PTEN,MAP,FILL,                                \
		     ((u64)MAP->mem->offset << NVKM_RAM_MM_SHIFT),             \
		     ((u64)MAP->mem->length << NVKM_RAM_MM_SHIFT),             \
		     (MAP->mem = MAP->mem->next))
#define VMM_MAP_ITER_DMA(VMM,PT,PTEI,PTEN,MAP,FILL)                            \
	VMM_MAP_ITER(VMM,PT,PTEI,PTEN,MAP,FILL,                                \
		     *MAP->dma, PAGE_SIZE, MAP->dma++)
#define VMM_MAP_ITER_SGL(VMM,PT,PTEI,PTEN,MAP,FILL)                            \
	VMM_MAP_ITER(VMM,PT,PTEI,PTEN,MAP,FILL,                                \
		     sg_dma_address(MAP->sgl), sg_dma_len(MAP->sgl),           \
		     (MAP->sgl = sg_next(MAP->sgl)))

#define VMM_FO(m,o,d,c,b) nvkm_fo##b((m)->memory, (o), (d), (c))
#define VMM_WO(m,o,d,c,b) nvkm_wo##b((m)->memory, (o), (d))
#define VMM_XO(m,v,o,d,c,b,fn,f,a...) do {                                     \
	const u32 _pteo = (o); u##b _data = (d);                               \
	VMM_SPAM((v), "   %010llx "f, (m)->addr + _pteo, _data, ##a);          \
	VMM_##fn((m), (m)->base + _pteo, _data, (c), b);                       \
} while(0)

#define VMM_WO032(m,v,o,d) VMM_XO((m),(v),(o),(d),  1, 32, WO, "%08x")
#define VMM_FO032(m,v,o,d,c)                                                   \
	VMM_XO((m),(v),(o),(d),(c), 32, FO, "%08x %08x", (c))

#define VMM_WO064(m,v,o,d) VMM_XO((m),(v),(o),(d),  1, 64, WO, "%016llx")
#define VMM_FO064(m,v,o,d,c)                                                   \
	VMM_XO((m),(v),(o),(d),(c), 64, FO, "%016llx %08x", (c))

#define VMM_XO128(m,v,o,lo,hi,c,f,a...) do {                                   \
	u32 _pteo = (o), _ptes = (c);                                          \
	const u64 _addr = (m)->addr + _pteo;                                   \
	VMM_SPAM((v), "   %010llx %016llx%016llx"f, _addr, (hi), (lo), ##a);   \
	while (_ptes--) {                                                      \
		nvkm_wo64((m)->memory, (m)->base + _pteo + 0, (lo));           \
		nvkm_wo64((m)->memory, (m)->base + _pteo + 8, (hi));           \
		_pteo += 0x10;                                                 \
	}                                                                      \
} while(0)

#define VMM_WO128(m,v,o,lo,hi) VMM_XO128((m),(v),(o),(lo),(hi), 1, "")
#define VMM_FO128(m,v,o,lo,hi,c) do {                                          \
	nvkm_kmap((m)->memory);                                                \
	VMM_XO128((m),(v),(o),(lo),(hi),(c), " %08x", (c));                    \
	nvkm_done((m)->memory);                                                \
} while(0)
#endif
