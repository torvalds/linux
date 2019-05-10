/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_MEMORY_H__
#define __NVKM_MEMORY_H__
#include <core/os.h>
struct nvkm_device;
struct nvkm_vma;
struct nvkm_vmm;

struct nvkm_tags {
	struct nvkm_mm_node *mn;
	refcount_t refcount;
};

enum nvkm_memory_target {
	NVKM_MEM_TARGET_INST, /* instance memory */
	NVKM_MEM_TARGET_VRAM, /* video memory */
	NVKM_MEM_TARGET_HOST, /* coherent system memory */
	NVKM_MEM_TARGET_NCOH, /* non-coherent system memory */
};

struct nvkm_memory {
	const struct nvkm_memory_func *func;
	const struct nvkm_memory_ptrs *ptrs;
	struct kref kref;
	struct nvkm_tags *tags;
};

struct nvkm_memory_func {
	void *(*dtor)(struct nvkm_memory *);
	enum nvkm_memory_target (*target)(struct nvkm_memory *);
	u8 (*page)(struct nvkm_memory *);
	u64 (*bar2)(struct nvkm_memory *);
	u64 (*addr)(struct nvkm_memory *);
	u64 (*size)(struct nvkm_memory *);
	void (*boot)(struct nvkm_memory *, struct nvkm_vmm *);
	void __iomem *(*acquire)(struct nvkm_memory *);
	void (*release)(struct nvkm_memory *);
	int (*map)(struct nvkm_memory *, u64 offset, struct nvkm_vmm *,
		   struct nvkm_vma *, void *argv, u32 argc);
};

struct nvkm_memory_ptrs {
	u32 (*rd32)(struct nvkm_memory *, u64 offset);
	void (*wr32)(struct nvkm_memory *, u64 offset, u32 data);
};

void nvkm_memory_ctor(const struct nvkm_memory_func *, struct nvkm_memory *);
int nvkm_memory_new(struct nvkm_device *, enum nvkm_memory_target,
		    u64 size, u32 align, bool zero, struct nvkm_memory **);
struct nvkm_memory *nvkm_memory_ref(struct nvkm_memory *);
void nvkm_memory_unref(struct nvkm_memory **);
int nvkm_memory_tags_get(struct nvkm_memory *, struct nvkm_device *, u32 tags,
			 void (*clear)(struct nvkm_device *, u32, u32),
			 struct nvkm_tags **);
void nvkm_memory_tags_put(struct nvkm_memory *, struct nvkm_device *,
			  struct nvkm_tags **);

#define nvkm_memory_target(p) (p)->func->target(p)
#define nvkm_memory_page(p) (p)->func->page(p)
#define nvkm_memory_bar2(p) (p)->func->bar2(p)
#define nvkm_memory_addr(p) (p)->func->addr(p)
#define nvkm_memory_size(p) (p)->func->size(p)
#define nvkm_memory_boot(p,v) (p)->func->boot((p),(v))
#define nvkm_memory_map(p,o,vm,va,av,ac)                                       \
	(p)->func->map((p),(o),(vm),(va),(av),(ac))

/* accessor macros - kmap()/done() must bracket use of the other accessor
 * macros to guarantee correct behaviour across all chipsets
 */
#define nvkm_kmap(o)     (o)->func->acquire(o)
#define nvkm_done(o)     (o)->func->release(o)

#define nvkm_ro32(o,a)   (o)->ptrs->rd32((o), (a))
#define nvkm_wo32(o,a,d) (o)->ptrs->wr32((o), (a), (d))
#define nvkm_mo32(o,a,m,d) ({                                                  \
	u32 _addr = (a), _data = nvkm_ro32((o), _addr);                        \
	nvkm_wo32((o), _addr, (_data & ~(m)) | (d));                           \
	_data;                                                                 \
})

#define nvkm_wo64(o,a,d) do {                                                  \
	u64 __a = (a), __d = (d);                                              \
	nvkm_wo32((o), __a + 0, lower_32_bits(__d));                           \
	nvkm_wo32((o), __a + 4, upper_32_bits(__d));                           \
} while(0)

#define nvkm_fill(t,s,o,a,d,c) do {                                            \
	u64 _a = (a), _c = (c), _d = (d), _o = _a >> s, _s = _c << s;          \
	u##t __iomem *_m = nvkm_kmap(o);                                       \
	if (likely(_m)) {                                                      \
		if (_d) {                                                      \
			while (_c--)                                           \
				iowrite##t##_native(_d, &_m[_o++]);            \
		} else {                                                       \
			memset_io(&_m[_o], _d, _s);                            \
		}                                                              \
	} else {                                                               \
		for (; _c; _c--, _a += BIT(s))                                 \
			nvkm_wo##t((o), _a, _d);                               \
	}                                                                      \
	nvkm_done(o);                                                          \
} while(0)
#define nvkm_fo32(o,a,d,c) nvkm_fill(32, 2, (o), (a), (d), (c))
#define nvkm_fo64(o,a,d,c) nvkm_fill(64, 3, (o), (a), (d), (c))
#endif
