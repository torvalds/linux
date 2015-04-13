#ifndef __NVKM_MMU_H__
#define __NVKM_MMU_H__
#include <core/subdev.h>
#include <core/mm.h>
struct nvkm_device;
struct nvkm_mem;

struct nvkm_vm_pgt {
	struct nvkm_gpuobj *obj[2];
	u32 refcount[2];
};

struct nvkm_vm_pgd {
	struct list_head head;
	struct nvkm_gpuobj *obj;
};

struct nvkm_vma {
	struct list_head head;
	int refcount;
	struct nvkm_vm *vm;
	struct nvkm_mm_node *node;
	u64 offset;
	u32 access;
};

struct nvkm_vm {
	struct nvkm_mmu *mmu;
	struct nvkm_mm mm;
	struct kref refcount;

	struct list_head pgd_list;
	atomic_t engref[NVDEV_SUBDEV_NR];

	struct nvkm_vm_pgt *pgt;
	u32 fpde;
	u32 lpde;
};

struct nvkm_mmu {
	struct nvkm_subdev base;

	u64 limit;
	u8  dma_bits;
	u32 pgt_bits;
	u8  spg_shift;
	u8  lpg_shift;

	int  (*create)(struct nvkm_mmu *, u64 offset, u64 length,
		       u64 mm_offset, struct nvkm_vm **);

	void (*map_pgt)(struct nvkm_gpuobj *pgd, u32 pde,
			struct nvkm_gpuobj *pgt[2]);
	void (*map)(struct nvkm_vma *, struct nvkm_gpuobj *,
		    struct nvkm_mem *, u32 pte, u32 cnt,
		    u64 phys, u64 delta);
	void (*map_sg)(struct nvkm_vma *, struct nvkm_gpuobj *,
		       struct nvkm_mem *, u32 pte, u32 cnt, dma_addr_t *);
	void (*unmap)(struct nvkm_gpuobj *pgt, u32 pte, u32 cnt);
	void (*flush)(struct nvkm_vm *);
};

static inline struct nvkm_mmu *
nvkm_mmu(void *obj)
{
	return (void *)nvkm_subdev(obj, NVDEV_SUBDEV_MMU);
}

#define nvkm_mmu_create(p,e,o,i,f,d)                                      \
	nvkm_subdev_create((p), (e), (o), 0, (i), (f), (d))
#define nvkm_mmu_destroy(p)                                               \
	nvkm_subdev_destroy(&(p)->base)
#define nvkm_mmu_init(p)                                                  \
	nvkm_subdev_init(&(p)->base)
#define nvkm_mmu_fini(p,s)                                                \
	nvkm_subdev_fini(&(p)->base, (s))

#define _nvkm_mmu_dtor _nvkm_subdev_dtor
#define _nvkm_mmu_init _nvkm_subdev_init
#define _nvkm_mmu_fini _nvkm_subdev_fini

extern struct nvkm_oclass nv04_mmu_oclass;
extern struct nvkm_oclass nv41_mmu_oclass;
extern struct nvkm_oclass nv44_mmu_oclass;
extern struct nvkm_oclass nv50_mmu_oclass;
extern struct nvkm_oclass gf100_mmu_oclass;

int  nv04_vm_create(struct nvkm_mmu *, u64, u64, u64,
		    struct nvkm_vm **);
void nv04_mmu_dtor(struct nvkm_object *);

int  nvkm_vm_create(struct nvkm_mmu *, u64 offset, u64 length, u64 mm_offset,
		    u32 block, struct nvkm_vm **);
int  nvkm_vm_new(struct nvkm_device *, u64 offset, u64 length, u64 mm_offset,
		 struct nvkm_vm **);
int  nvkm_vm_ref(struct nvkm_vm *, struct nvkm_vm **, struct nvkm_gpuobj *pgd);
int  nvkm_vm_get(struct nvkm_vm *, u64 size, u32 page_shift, u32 access,
		 struct nvkm_vma *);
void nvkm_vm_put(struct nvkm_vma *);
void nvkm_vm_map(struct nvkm_vma *, struct nvkm_mem *);
void nvkm_vm_map_at(struct nvkm_vma *, u64 offset, struct nvkm_mem *);
void nvkm_vm_unmap(struct nvkm_vma *);
void nvkm_vm_unmap_at(struct nvkm_vma *, u64 offset, u64 length);
#endif
