/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MMU_H__
#define __NVKM_MMU_H__
#include <core/subdev.h>
#include <subdev/gsp.h>

struct nvkm_vma {
	struct list_head head;
	struct rb_node tree;
	u64 addr;
	u64 size;
	bool mapref:1; /* PTs (de)referenced on (un)map (vs pre-allocated). */
	bool sparse:1; /* Unmapped PDEs/PTEs will not trigger MMU faults. */
#define NVKM_VMA_PAGE_NONE 7
	u8   page:3; /* Requested page type (index, or NONE for automatic). */
	u8   refd:3; /* Current page type (index, or NONE for unreferenced). */
	bool used:1; /* Region allocated. */
	bool part:1; /* Region was split from an allocated region by map(). */
	bool busy:1; /* Region busy (for temporarily preventing user access). */
	bool mapped:1; /* Region contains valid pages. */
	bool no_comp:1; /* Force no memory compression. */
	struct nvkm_memory *memory; /* Memory currently mapped into VMA. */
	struct nvkm_tags *tags; /* Compression tag reference. */
};

struct nvkm_vmm {
	const struct nvkm_vmm_func *func;
	struct nvkm_mmu *mmu;
	const char *name;
	u32 debug;
	struct kref kref;

	struct {
		struct mutex vmm;
		struct mutex ref;
		struct mutex map;
	} mutex;

	u64 start;
	u64 limit;
	struct {
		struct {
			u64 addr;
			u64 size;
		} p;
		struct {
			u64 addr;
			u64 size;
		} n;
		bool raw;
	} managed;

	struct nvkm_vmm_pt *pd;
	struct list_head join;

	struct list_head list;
	struct rb_root free;
	struct rb_root root;

	bool bootstrapped;
	atomic_t engref[NVKM_SUBDEV_NR];

	dma_addr_t null;
	void *nullp;

	bool replay;

	struct {
		u64 bar2_pdb;

		struct nvkm_gsp_client client;
		struct nvkm_gsp_device device;
		struct nvkm_gsp_object object;

		struct nvkm_vma *rsvd;
		bool external;
	} rm;
};

int nvkm_vmm_new(struct nvkm_device *, u64 addr, u64 size, void *argv, u32 argc,
		 struct lock_class_key *, const char *name, struct nvkm_vmm **);
struct nvkm_vmm *nvkm_vmm_ref(struct nvkm_vmm *);
void nvkm_vmm_unref(struct nvkm_vmm **);
int nvkm_vmm_boot(struct nvkm_vmm *);
int nvkm_vmm_join(struct nvkm_vmm *, struct nvkm_memory *inst);
void nvkm_vmm_part(struct nvkm_vmm *, struct nvkm_memory *inst);
int nvkm_vmm_get(struct nvkm_vmm *, u8 page, u64 size, struct nvkm_vma **);
void nvkm_vmm_put(struct nvkm_vmm *, struct nvkm_vma **);

struct nvkm_vmm_map {
	struct nvkm_memory *memory;
	u64 offset;

	struct nvkm_mm_node *mem;
	struct scatterlist *sgl;
	dma_addr_t *dma;
	u64 *pfn;
	u64 off;

	const struct nvkm_vmm_page *page;

	bool no_comp;
	struct nvkm_tags *tags;
	u64 next;
	u64 type;
	u64 ctag;
};

int nvkm_vmm_map(struct nvkm_vmm *, struct nvkm_vma *, void *argv, u32 argc,
		 struct nvkm_vmm_map *);
void nvkm_vmm_unmap(struct nvkm_vmm *, struct nvkm_vma *);

struct nvkm_memory *nvkm_umem_search(struct nvkm_client *, u64);
struct nvkm_vmm *nvkm_uvmm_search(struct nvkm_client *, u64 handle);

struct nvkm_mmu {
	const struct nvkm_mmu_func *func;
	struct nvkm_subdev subdev;

	u8  dma_bits;

	int heap_nr;
	struct {
#define NVKM_MEM_VRAM                                                      0x01
#define NVKM_MEM_HOST                                                      0x02
#define NVKM_MEM_COMP                                                      0x04
#define NVKM_MEM_DISP                                                      0x08
		u8  type;
		u64 size;
	} heap[4];

	int type_nr;
	struct {
#define NVKM_MEM_KIND                                                      0x10
#define NVKM_MEM_MAPPABLE                                                  0x20
#define NVKM_MEM_COHERENT                                                  0x40
#define NVKM_MEM_UNCACHED                                                  0x80
		u8 type;
		u8 heap;
	} type[16];

	struct nvkm_vmm *vmm;

	struct {
		struct mutex mutex;
		struct list_head list;
	} ptc, ptp;

	struct mutex mutex; /* serialises mmu invalidations */

	struct nvkm_device_oclass user;
};

int nv04_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int nv41_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int nv44_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int nv50_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int g84_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int mcp77_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gf100_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gk104_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gk20a_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gm200_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gm20b_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gp100_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gp10b_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gv100_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int tu102_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
int gh100_mmu_new(struct nvkm_device *, enum nvkm_subdev_type, int inst, struct nvkm_mmu **);
#endif
