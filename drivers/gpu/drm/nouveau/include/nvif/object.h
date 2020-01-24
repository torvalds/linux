/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_OBJECT_H__
#define __NVIF_OBJECT_H__

#include <nvif/os.h>

struct nvif_sclass {
	s32 oclass;
	int minver;
	int maxver;
};

struct nvif_object {
	struct nvif_client *client;
	u32 handle;
	s32 oclass;
	void *priv; /*XXX: hack */
	struct {
		void __iomem *ptr;
		u64 size;
	} map;
};

int  nvif_object_init(struct nvif_object *, u32 handle, s32 oclass, void *, u32,
		      struct nvif_object *);
void nvif_object_fini(struct nvif_object *);
int  nvif_object_ioctl(struct nvif_object *, void *, u32, void **);
int  nvif_object_sclass_get(struct nvif_object *, struct nvif_sclass **);
void nvif_object_sclass_put(struct nvif_sclass **);
u32  nvif_object_rd(struct nvif_object *, int, u64);
void nvif_object_wr(struct nvif_object *, int, u64, u32);
int  nvif_object_mthd(struct nvif_object *, u32, void *, u32);
int  nvif_object_map_handle(struct nvif_object *, void *, u32,
			    u64 *handle, u64 *length);
void nvif_object_unmap_handle(struct nvif_object *);
int  nvif_object_map(struct nvif_object *, void *, u32);
void nvif_object_unmap(struct nvif_object *);

#define nvif_handle(a) (unsigned long)(void *)(a)
#define nvif_object(a) (a)->object

#define nvif_rd(a,f,b,c) ({                                                    \
	struct nvif_object *_object = (a);                                     \
	u32 _data;                                                             \
	if (likely(_object->map.ptr))                                          \
		_data = f((u8 __iomem *)_object->map.ptr + (c));               \
	else                                                                   \
		_data = nvif_object_rd(_object, (b), (c));                     \
	_data;                                                                 \
})
#define nvif_wr(a,f,b,c,d) ({                                                  \
	struct nvif_object *_object = (a);                                     \
	if (likely(_object->map.ptr))                                          \
		f((d), (u8 __iomem *)_object->map.ptr + (c));                  \
	else                                                                   \
		nvif_object_wr(_object, (b), (c), (d));                        \
})
#define nvif_rd08(a,b) ({ ((u8)nvif_rd((a), ioread8, 1, (b))); })
#define nvif_rd16(a,b) ({ ((u16)nvif_rd((a), ioread16_native, 2, (b))); })
#define nvif_rd32(a,b) ({ ((u32)nvif_rd((a), ioread32_native, 4, (b))); })
#define nvif_wr08(a,b,c) nvif_wr((a), iowrite8, 1, (b), (u8)(c))
#define nvif_wr16(a,b,c) nvif_wr((a), iowrite16_native, 2, (b), (u16)(c))
#define nvif_wr32(a,b,c) nvif_wr((a), iowrite32_native, 4, (b), (u32)(c))
#define nvif_mask(a,b,c,d) ({                                                  \
	struct nvif_object *__object = (a);                                    \
	u32 _addr = (b), _data = nvif_rd32(__object, _addr);                   \
	nvif_wr32(__object, _addr, (_data & ~(c)) | (d));                      \
	_data;                                                                 \
})

#define nvif_mthd(a,b,c,d) nvif_object_mthd((a), (b), (c), (d))

struct nvif_mclass {
	s32 oclass;
	int version;
};

#define nvif_mclass(o,m) ({                                                    \
	struct nvif_object *object = (o);                                      \
	struct nvif_sclass *sclass;                                            \
	typeof(m[0]) *mclass = (m);                                            \
	int ret = -ENODEV;                                                     \
	int cnt, i, j;                                                         \
                                                                               \
	cnt = nvif_object_sclass_get(object, &sclass);                         \
	if (cnt >= 0) {                                                        \
		for (i = 0; ret < 0 && mclass[i].oclass; i++) {                \
			for (j = 0; j < cnt; j++) {                            \
				if (mclass[i].oclass  == sclass[j].oclass &&   \
				    mclass[i].version >= sclass[j].minver &&   \
				    mclass[i].version <= sclass[j].maxver) {   \
					ret = i;                               \
					break;                                 \
				}                                              \
			}                                                      \
		}                                                              \
		nvif_object_sclass_put(&sclass);                               \
	}                                                                      \
	ret;                                                                   \
})

#define nvif_sclass(o,m,u) ({                                                  \
	const typeof(m[0]) *_mclass = (m);                                     \
	s32 _oclass = (u);                                                     \
	int _cid;                                                              \
	if (_oclass) {                                                         \
		for (_cid = 0; _mclass[_cid].oclass; _cid++) {                 \
			if (_mclass[_cid].oclass == _oclass)                   \
				break;                                         \
		}                                                              \
		_cid = _mclass[_cid].oclass ? _cid : -ENOSYS;                  \
	} else {                                                               \
		_cid = nvif_mclass((o), _mclass);                              \
	}                                                                      \
	_cid;                                                                  \
})

/*XXX*/
#include <core/object.h>
#define nvxx_object(a) ({                                                      \
	struct nvif_object *_object = (a);                                     \
	(struct nvkm_object *)_object->priv;                                   \
})
#endif
