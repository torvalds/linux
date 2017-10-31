#ifndef __NVKM_MMU_PRIV_H__
#define __NVKM_MMU_PRIV_H__
#define nvkm_mmu(p) container_of((p), struct nvkm_mmu, subdev)
#include <subdev/mmu.h>

void nvkm_mmu_ctor(const struct nvkm_mmu_func *, struct nvkm_device *,
		   int index, struct nvkm_mmu *);
int nvkm_mmu_new_(const struct nvkm_mmu_func *, struct nvkm_device *,
		  int index, struct nvkm_mmu **);

struct nvkm_mmu_func {
	void (*init)(struct nvkm_mmu *);

	u8  dma_bits;

	struct {
		struct nvkm_sclass user;
	} mmu;

	struct {
		struct nvkm_sclass user;
		int (*vram)(struct nvkm_mmu *, int type, u8 page, u64 size,
			    void *argv, u32 argc, struct nvkm_memory **);
		int (*umap)(struct nvkm_mmu *, struct nvkm_memory *, void *argv,
			    u32 argc, u64 *addr, u64 *size, struct nvkm_vma **);
	} mem;

	struct {
		struct nvkm_sclass user;
		int (*ctor)(struct nvkm_mmu *, u64 addr, u64 size,
			    void *argv, u32 argc, struct lock_class_key *,
			    const char *name, struct nvkm_vmm **);
		bool global;
		u32 pd_offset;
	} vmm;

	const u8 *(*kind)(struct nvkm_mmu *, int *count);
	bool kind_sys;
};

extern const struct nvkm_mmu_func nv04_mmu;

const u8 *nv50_mmu_kind(struct nvkm_mmu *, int *count);

const u8 *gf100_mmu_kind(struct nvkm_mmu *, int *count);

const u8 *gm200_mmu_kind(struct nvkm_mmu *, int *);

struct nvkm_mmu_pt {
	union {
		struct nvkm_mmu_ptc *ptc;
		struct nvkm_mmu_ptp *ptp;
	};
	struct nvkm_memory *memory;
	bool sub;
	u16 base;
	u64 addr;
	struct list_head head;
};

void nvkm_mmu_ptc_dump(struct nvkm_mmu *);
struct nvkm_mmu_pt *
nvkm_mmu_ptc_get(struct nvkm_mmu *, u32 size, u32 align, bool zero);
void nvkm_mmu_ptc_put(struct nvkm_mmu *, bool force, struct nvkm_mmu_pt **);
#endif
