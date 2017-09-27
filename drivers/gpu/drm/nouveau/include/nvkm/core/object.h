#ifndef __NVKM_OBJECT_H__
#define __NVKM_OBJECT_H__
#include <core/os.h>
#include <core/debug.h>
struct nvkm_event;
struct nvkm_gpuobj;
struct nvkm_oclass;

struct nvkm_object {
	const struct nvkm_object_func *func;
	struct nvkm_client *client;
	struct nvkm_engine *engine;
	s32 oclass;
	u32 handle;

	struct list_head head;
	struct list_head tree;
	u8  route;
	u64 token;
	u64 object;
	struct rb_node node;
};

struct nvkm_object_func {
	void *(*dtor)(struct nvkm_object *);
	int (*init)(struct nvkm_object *);
	int (*fini)(struct nvkm_object *, bool suspend);
	int (*mthd)(struct nvkm_object *, u32 mthd, void *data, u32 size);
	int (*ntfy)(struct nvkm_object *, u32 mthd, struct nvkm_event **);
	int (*map)(struct nvkm_object *, u64 *addr, u32 *size);
	int (*rd08)(struct nvkm_object *, u64 addr, u8 *data);
	int (*rd16)(struct nvkm_object *, u64 addr, u16 *data);
	int (*rd32)(struct nvkm_object *, u64 addr, u32 *data);
	int (*wr08)(struct nvkm_object *, u64 addr, u8 data);
	int (*wr16)(struct nvkm_object *, u64 addr, u16 data);
	int (*wr32)(struct nvkm_object *, u64 addr, u32 data);
	int (*bind)(struct nvkm_object *, struct nvkm_gpuobj *, int align,
		    struct nvkm_gpuobj **);
	int (*sclass)(struct nvkm_object *, int index, struct nvkm_oclass *);
};

void nvkm_object_ctor(const struct nvkm_object_func *,
		      const struct nvkm_oclass *, struct nvkm_object *);
int nvkm_object_new_(const struct nvkm_object_func *,
		     const struct nvkm_oclass *, void *data, u32 size,
		     struct nvkm_object **);
int nvkm_object_new(const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
void nvkm_object_del(struct nvkm_object **);
void *nvkm_object_dtor(struct nvkm_object *);
int nvkm_object_init(struct nvkm_object *);
int nvkm_object_fini(struct nvkm_object *, bool suspend);
int nvkm_object_mthd(struct nvkm_object *, u32 mthd, void *data, u32 size);
int nvkm_object_ntfy(struct nvkm_object *, u32 mthd, struct nvkm_event **);
int nvkm_object_map(struct nvkm_object *, u64 *addr, u32 *size);
int nvkm_object_rd08(struct nvkm_object *, u64 addr, u8  *data);
int nvkm_object_rd16(struct nvkm_object *, u64 addr, u16 *data);
int nvkm_object_rd32(struct nvkm_object *, u64 addr, u32 *data);
int nvkm_object_wr08(struct nvkm_object *, u64 addr, u8   data);
int nvkm_object_wr16(struct nvkm_object *, u64 addr, u16  data);
int nvkm_object_wr32(struct nvkm_object *, u64 addr, u32  data);
int nvkm_object_bind(struct nvkm_object *, struct nvkm_gpuobj *, int align,
		     struct nvkm_gpuobj **);

bool nvkm_object_insert(struct nvkm_object *);
void nvkm_object_remove(struct nvkm_object *);
struct nvkm_object *nvkm_object_search(struct nvkm_client *, u64 object,
				       const struct nvkm_object_func *);

struct nvkm_sclass {
	int minver;
	int maxver;
	s32 oclass;
	const struct nvkm_object_func *func;
	int (*ctor)(const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
};

struct nvkm_oclass {
	int (*ctor)(const struct nvkm_oclass *, void *data, u32 size,
		    struct nvkm_object **);
	struct nvkm_sclass base;
	const void *priv;
	const void *engn;
	u32 handle;
	u8  route;
	u64 token;
	u64 object;
	struct nvkm_client *client;
	struct nvkm_object *parent;
	struct nvkm_engine *engine;
};
#endif
