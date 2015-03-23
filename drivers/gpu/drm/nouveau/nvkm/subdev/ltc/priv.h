#ifndef __NVKM_LTC_PRIV_H__
#define __NVKM_LTC_PRIV_H__
#include <subdev/ltc.h>

#include <core/mm.h>
struct nvkm_fb;

struct nvkm_ltc_priv {
	struct nvkm_ltc base;
	u32 ltc_nr;
	u32 lts_nr;

	u32 num_tags;
	u32 tag_base;
	struct nvkm_mm tags;
	struct nvkm_mm_node *tag_ram;

	u32 zbc_color[NVKM_LTC_MAX_ZBC_CNT][4];
	u32 zbc_depth[NVKM_LTC_MAX_ZBC_CNT];
};

#define nvkm_ltc_create(p,e,o,d)                                               \
	nvkm_ltc_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nvkm_ltc_destroy(p) ({                                                 \
	struct nvkm_ltc_priv *_priv = (p);                                     \
	_nvkm_ltc_dtor(nv_object(_priv));                                      \
})
#define nvkm_ltc_init(p) ({                                                    \
	struct nvkm_ltc_priv *_priv = (p);                                     \
	_nvkm_ltc_init(nv_object(_priv));                                      \
})
#define nvkm_ltc_fini(p,s) ({                                                  \
	struct nvkm_ltc_priv *_priv = (p);                                     \
	_nvkm_ltc_fini(nv_object(_priv), (s));                                 \
})

int  nvkm_ltc_create_(struct nvkm_object *, struct nvkm_object *,
		      struct nvkm_oclass *, int, void **);

#define _nvkm_ltc_dtor _nvkm_subdev_dtor
int _nvkm_ltc_init(struct nvkm_object *);
#define _nvkm_ltc_fini _nvkm_subdev_fini

int  gf100_ltc_ctor(struct nvkm_object *, struct nvkm_object *,
		    struct nvkm_oclass *, void *, u32,
		    struct nvkm_object **);
void gf100_ltc_dtor(struct nvkm_object *);
int  gf100_ltc_init_tag_ram(struct nvkm_fb *, struct nvkm_ltc_priv *);
int  gf100_ltc_tags_alloc(struct nvkm_ltc *, u32, struct nvkm_mm_node **);
void gf100_ltc_tags_free(struct nvkm_ltc *, struct nvkm_mm_node **);

struct nvkm_ltc_impl {
	struct nvkm_oclass base;
	void (*intr)(struct nvkm_subdev *);

	void (*cbc_clear)(struct nvkm_ltc_priv *, u32 start, u32 limit);
	void (*cbc_wait)(struct nvkm_ltc_priv *);

	int zbc;
	void (*zbc_clear_color)(struct nvkm_ltc_priv *, int, const u32[4]);
	void (*zbc_clear_depth)(struct nvkm_ltc_priv *, int, const u32);
};

void gf100_ltc_intr(struct nvkm_subdev *);
void gf100_ltc_cbc_clear(struct nvkm_ltc_priv *, u32, u32);
void gf100_ltc_cbc_wait(struct nvkm_ltc_priv *);
void gf100_ltc_zbc_clear_color(struct nvkm_ltc_priv *, int, const u32[4]);
void gf100_ltc_zbc_clear_depth(struct nvkm_ltc_priv *, int, const u32);
#endif
