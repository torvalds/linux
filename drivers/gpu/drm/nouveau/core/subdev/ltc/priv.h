#ifndef __NVKM_LTC_PRIV_H__
#define __NVKM_LTC_PRIV_H__

#include <subdev/ltc.h>
#include <subdev/fb.h>

struct nvkm_ltc_priv {
	struct nouveau_ltc base;
	u32 ltc_nr;
	u32 lts_nr;

	u32 num_tags;
	u32 tag_base;
	struct nouveau_mm tags;
	struct nouveau_mm_node *tag_ram;

	u32 zbc_color[NOUVEAU_LTC_MAX_ZBC_CNT][4];
	u32 zbc_depth[NOUVEAU_LTC_MAX_ZBC_CNT];
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

int  nvkm_ltc_create_(struct nouveau_object *, struct nouveau_object *,
		      struct nouveau_oclass *, int, void **);

#define _nvkm_ltc_dtor _nouveau_subdev_dtor
int _nvkm_ltc_init(struct nouveau_object *);
#define _nvkm_ltc_fini _nouveau_subdev_fini

int  gf100_ltc_ctor(struct nouveau_object *, struct nouveau_object *,
		    struct nouveau_oclass *, void *, u32,
		    struct nouveau_object **);
void gf100_ltc_dtor(struct nouveau_object *);
int  gf100_ltc_init_tag_ram(struct nouveau_fb *, struct nvkm_ltc_priv *);
int  gf100_ltc_tags_alloc(struct nouveau_ltc *, u32, struct nouveau_mm_node **);
void gf100_ltc_tags_free(struct nouveau_ltc *, struct nouveau_mm_node **);

struct nvkm_ltc_impl {
	struct nouveau_oclass base;
	void (*intr)(struct nouveau_subdev *);

	void (*cbc_clear)(struct nvkm_ltc_priv *, u32 start, u32 limit);
	void (*cbc_wait)(struct nvkm_ltc_priv *);

	int zbc;
	void (*zbc_clear_color)(struct nvkm_ltc_priv *, int, const u32[4]);
	void (*zbc_clear_depth)(struct nvkm_ltc_priv *, int, const u32);
};

void gf100_ltc_intr(struct nouveau_subdev *);
void gf100_ltc_cbc_clear(struct nvkm_ltc_priv *, u32, u32);
void gf100_ltc_cbc_wait(struct nvkm_ltc_priv *);
void gf100_ltc_zbc_clear_color(struct nvkm_ltc_priv *, int, const u32[4]);
void gf100_ltc_zbc_clear_depth(struct nvkm_ltc_priv *, int, const u32);

#endif
