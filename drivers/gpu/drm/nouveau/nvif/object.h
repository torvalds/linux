#ifndef __NVIF_OBJECT_H__
#define __NVIF_OBJECT_H__

#include <nvif/os.h>

struct nvif_object {
	struct nvif_object *parent;
	struct nvif_object *object; /*XXX: hack for nvif_object() */
	struct kref refcount;
	u32 handle;
	u32 oclass;
	void *data;
	u32   size;
	void *priv; /*XXX: hack */
	void (*dtor)(struct nvif_object *);
	struct {
		void __iomem *ptr;
		u32 size;
	} map;
};

int  nvif_object_init(struct nvif_object *, void (*dtor)(struct nvif_object *),
		      u32 handle, u32 oclass, void *, u32,
		      struct nvif_object *);
void nvif_object_fini(struct nvif_object *);
int  nvif_object_new(struct nvif_object *, u32 handle, u32 oclass,
		     void *, u32, struct nvif_object **);
void nvif_object_ref(struct nvif_object *, struct nvif_object **);
int  nvif_object_ioctl(struct nvif_object *, void *, u32, void **);
int  nvif_object_sclass(struct nvif_object *, u32 *, int);
u32  nvif_object_rd(struct nvif_object *, int, u64);
void nvif_object_wr(struct nvif_object *, int, u64, u32);
int  nvif_object_mthd(struct nvif_object *, u32, void *, u32);
int  nvif_object_map(struct nvif_object *);
void nvif_object_unmap(struct nvif_object *);

#define nvif_object(a) (a)->object

#define ioread8_native ioread8
#define iowrite8_native iowrite8
#define nvif_rd(a,b,c) ({                                                      \
	struct nvif_object *_object = nvif_object(a);                          \
	u32 _data;                                                             \
	if (likely(_object->map.ptr))                                          \
		_data = ioread##b##_native((u8 __iomem *)_object->map.ptr + (c));      \
	else                                                                   \
		_data = nvif_object_rd(_object, (b) / 8, (c));                 \
	_data;                                                                 \
})
#define nvif_wr(a,b,c,d) ({                                                    \
	struct nvif_object *_object = nvif_object(a);                          \
	if (likely(_object->map.ptr))                                          \
		iowrite##b##_native((d), (u8 __iomem *)_object->map.ptr + (c));        \
	else                                                                   \
		nvif_object_wr(_object, (b) / 8, (c), (d));                    \
})
#define nvif_rd08(a,b) ({ u8  _v = nvif_rd((a), 8, (b)); _v; })
#define nvif_rd16(a,b) ({ u16 _v = nvif_rd((a), 16, (b)); _v; })
#define nvif_rd32(a,b) ({ u32 _v = nvif_rd((a), 32, (b)); _v; })
#define nvif_wr08(a,b,c) nvif_wr((a), 8, (b), (u8)(c))
#define nvif_wr16(a,b,c) nvif_wr((a), 16, (b), (u16)(c))
#define nvif_wr32(a,b,c) nvif_wr((a), 32, (b), (u32)(c))
#define nvif_mask(a,b,c,d) ({                                                  \
	u32 _v = nvif_rd32(nvif_object(a), (b));                               \
	nvif_wr32(nvif_object(a), (b), (_v & ~(c)) | (d));                     \
	_v;                                                                    \
})

#define nvif_mthd(a,b,c,d) nvif_object_mthd(nvif_object(a), (b), (c), (d))

/*XXX*/
#include <core/object.h>
#define nvkm_object(a) ((struct nouveau_object *)nvif_object(a)->priv)

#endif
