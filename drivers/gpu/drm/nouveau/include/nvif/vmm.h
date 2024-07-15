#ifndef __NVIF_VMM_H__
#define __NVIF_VMM_H__
#include <nvif/object.h>
struct nvif_mem;
struct nvif_mmu;

enum nvif_vmm_type {
	UNMANAGED,
	MANAGED,
	RAW,
};

enum nvif_vmm_get {
	ADDR,
	PTES,
	LAZY
};

struct nvif_vma {
	u64 addr;
	u64 size;
};

struct nvif_vmm {
	struct nvif_object object;
	u64 start;
	u64 limit;

	struct {
		u8 shift;
		bool sparse:1;
		bool vram:1;
		bool host:1;
		bool comp:1;
	} *page;
	int page_nr;
};

int nvif_vmm_ctor(struct nvif_mmu *, const char *name, s32 oclass,
		  enum nvif_vmm_type, u64 addr, u64 size, void *argv, u32 argc,
		  struct nvif_vmm *);
void nvif_vmm_dtor(struct nvif_vmm *);
int nvif_vmm_get(struct nvif_vmm *, enum nvif_vmm_get, bool sparse,
		 u8 page, u8 align, u64 size, struct nvif_vma *);
void nvif_vmm_put(struct nvif_vmm *, struct nvif_vma *);
int nvif_vmm_map(struct nvif_vmm *, u64 addr, u64 size, void *argv, u32 argc,
		 struct nvif_mem *, u64 offset);
int nvif_vmm_unmap(struct nvif_vmm *, u64);

int nvif_vmm_raw_get(struct nvif_vmm *vmm, u64 addr, u64 size, u8 shift);
int nvif_vmm_raw_put(struct nvif_vmm *vmm, u64 addr, u64 size, u8 shift);
int nvif_vmm_raw_map(struct nvif_vmm *vmm, u64 addr, u64 size, u8 shift,
		     void *argv, u32 argc, struct nvif_mem *mem, u64 offset);
int nvif_vmm_raw_unmap(struct nvif_vmm *vmm, u64 addr, u64 size,
		       u8 shift, bool sparse);
int nvif_vmm_raw_sparse(struct nvif_vmm *vmm, u64 addr, u64 size, bool ref);
#endif
