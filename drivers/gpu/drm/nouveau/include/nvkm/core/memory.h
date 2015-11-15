#ifndef __NVKM_MEMORY_H__
#define __NVKM_MEMORY_H__
#include <core/os.h>
struct nvkm_device;
struct nvkm_vma;
struct nvkm_vm;

enum nvkm_memory_target {
	NVKM_MEM_TARGET_INST,
	NVKM_MEM_TARGET_VRAM,
	NVKM_MEM_TARGET_HOST,
};

struct nvkm_memory {
	const struct nvkm_memory_func *func;
};

struct nvkm_memory_func {
	void *(*dtor)(struct nvkm_memory *);
	enum nvkm_memory_target (*target)(struct nvkm_memory *);
	u64 (*addr)(struct nvkm_memory *);
	u64 (*size)(struct nvkm_memory *);
	void (*boot)(struct nvkm_memory *, struct nvkm_vm *);
	void __iomem *(*acquire)(struct nvkm_memory *);
	void (*release)(struct nvkm_memory *);
	u32 (*rd32)(struct nvkm_memory *, u64 offset);
	void (*wr32)(struct nvkm_memory *, u64 offset, u32 data);
	void (*map)(struct nvkm_memory *, struct nvkm_vma *, u64 offset);
};

void nvkm_memory_ctor(const struct nvkm_memory_func *, struct nvkm_memory *);
int nvkm_memory_new(struct nvkm_device *, enum nvkm_memory_target,
		    u64 size, u32 align, bool zero, struct nvkm_memory **);
void nvkm_memory_del(struct nvkm_memory **);
#define nvkm_memory_target(p) (p)->func->target(p)
#define nvkm_memory_addr(p) (p)->func->addr(p)
#define nvkm_memory_size(p) (p)->func->size(p)
#define nvkm_memory_boot(p,v) (p)->func->boot((p),(v))
#define nvkm_memory_map(p,v,o) (p)->func->map((p),(v),(o))

/* accessor macros - kmap()/done() must bracket use of the other accessor
 * macros to guarantee correct behaviour across all chipsets
 */
#define nvkm_kmap(o)     (o)->func->acquire(o)
#define nvkm_ro32(o,a)   (o)->func->rd32((o), (a))
#define nvkm_wo32(o,a,d) (o)->func->wr32((o), (a), (d))
#define nvkm_mo32(o,a,m,d) ({                                                  \
	u32 _addr = (a), _data = nvkm_ro32((o), _addr);                        \
	nvkm_wo32((o), _addr, (_data & ~(m)) | (d));                           \
	_data;                                                                 \
})
#define nvkm_done(o)     (o)->func->release(o)
#endif
