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
	struct nvif_parent *parent;
	struct nvif_client *client;
	const char *name;
	u32 handle;
	s32 oclass;
	void *priv; /*XXX: hack */
	struct nvif_map {
		void __iomem *ptr;
		u64 size;
	} map;
};

static inline bool
nvif_object_constructed(struct nvif_object *object)
{
	return object->client != NULL;
}

int  nvif_object_ctor(struct nvif_object *, const char *name, u32 handle,
		      s32 oclass, void *, u32, struct nvif_object *);
void nvif_object_dtor(struct nvif_object *);
int  nvif_object_ioctl(struct nvif_object *, void *, u32, void **);
int  nvif_object_sclass_get(struct nvif_object *, struct nvif_sclass **);
void nvif_object_sclass_put(struct nvif_sclass **);
int  nvif_object_mthd(struct nvif_object *, u32, void *, u32);
int  nvif_object_map_handle(struct nvif_object *, void *, u32,
			    u64 *handle, u64 *length);
void nvif_object_unmap_handle(struct nvif_object *);
int  nvif_object_map(struct nvif_object *, void *, u32);
void nvif_object_unmap(struct nvif_object *);

#define nvif_handle(a) (unsigned long)(void *)(a)
#define nvif_object(a) (a)->object

#define nvif_rd(a,f,b,c) ({                                                    \
	u32 _data = f((u8 __iomem *)(a)->map.ptr + (c));                       \
	_data;                                                                 \
})
#define nvif_wr(a,f,b,c,d) ({                                                  \
	f((d), (u8 __iomem *)(a)->map.ptr + (c));                              \
})
#define nvif_rd08(a,b) ({ ((u8)nvif_rd((a), ioread8, 1, (b))); })
#define nvif_rd16(a,b) ({ ((u16)nvif_rd((a), ioread16_native, 2, (b))); })
#define nvif_rd32(a,b) ({ ((u32)nvif_rd((a), ioread32_native, 4, (b))); })
#define nvif_wr08(a,b,c) nvif_wr((a), iowrite8, 1, (b), (u8)(c))
#define nvif_wr16(a,b,c) nvif_wr((a), iowrite16_native, 2, (b), (u16)(c))
#define nvif_wr32(a,b,c) nvif_wr((a), iowrite32_native, 4, (b), (u32)(c))
#define nvif_mask(a,b,c,d) ({                                                  \
	typeof(a) __object = (a);                                              \
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

#define NVIF_RD32_(p,o,dr)   nvif_rd32((p), (o) + (dr))
#define NVIF_WR32_(p,o,dr,f) nvif_wr32((p), (o) + (dr), (f))
#define NVIF_RD32(p,A...) DRF_RD(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_RV32(p,A...) DRF_RV(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_TV32(p,A...) DRF_TV(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_TD32(p,A...) DRF_TD(NVIF_RD32_,                  (p), 0, ##A)
#define NVIF_WR32(p,A...) DRF_WR(            NVIF_WR32_,      (p), 0, ##A)
#define NVIF_WV32(p,A...) DRF_WV(            NVIF_WR32_,      (p), 0, ##A)
#define NVIF_WD32(p,A...) DRF_WD(            NVIF_WR32_,      (p), 0, ##A)
#define NVIF_MR32(p,A...) DRF_MR(NVIF_RD32_, NVIF_WR32_, u32, (p), 0, ##A)
#define NVIF_MV32(p,A...) DRF_MV(NVIF_RD32_, NVIF_WR32_, u32, (p), 0, ##A)
#define NVIF_MD32(p,A...) DRF_MD(NVIF_RD32_, NVIF_WR32_, u32, (p), 0, ##A)
#endif
