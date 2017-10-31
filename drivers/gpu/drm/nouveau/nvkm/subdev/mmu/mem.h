#ifndef __NVKM_MEM_H__
#define __NVKM_MEM_H__
#include "priv.h"

int nvkm_mem_new_type(struct nvkm_mmu *, int type, u8 page, u64 size,
		      void *argv, u32 argc, struct nvkm_memory **);
int nvkm_mem_map_host(struct nvkm_memory *, void **pmap);
#endif
