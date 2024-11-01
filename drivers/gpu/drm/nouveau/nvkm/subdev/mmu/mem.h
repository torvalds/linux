#ifndef __NVKM_MEM_H__
#define __NVKM_MEM_H__
#include "priv.h"

int nvkm_mem_new_type(struct nvkm_mmu *, int type, u8 page, u64 size,
		      void *argv, u32 argc, struct nvkm_memory **);
int nvkm_mem_map_host(struct nvkm_memory *, void **pmap);

int nv04_mem_new(struct nvkm_mmu *, int, u8, u64, void *, u32,
		 struct nvkm_memory **);
int nv04_mem_map(struct nvkm_mmu *, struct nvkm_memory *, void *, u32,
		 u64 *, u64 *, struct nvkm_vma **);

int nv50_mem_new(struct nvkm_mmu *, int, u8, u64, void *, u32,
		 struct nvkm_memory **);
int nv50_mem_map(struct nvkm_mmu *, struct nvkm_memory *, void *, u32,
		 u64 *, u64 *, struct nvkm_vma **);

int gf100_mem_new(struct nvkm_mmu *, int, u8, u64, void *, u32,
		  struct nvkm_memory **);
int gf100_mem_map(struct nvkm_mmu *, struct nvkm_memory *, void *, u32,
		  u64 *, u64 *, struct nvkm_vma **);
#endif
