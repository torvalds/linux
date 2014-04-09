#ifndef __NVKM_LTCG_PRIV_GF100_H__
#define __NVKM_LTCG_PRIV_GF100_H__

#include <subdev/ltcg.h>

struct gf100_ltcg_priv {
	struct nouveau_ltcg base;
	u32 ltc_nr;
	u32 lts_nr;
	u32 num_tags;
	u32 tag_base;
	struct nouveau_mm tags;
	struct nouveau_mm_node *tag_ram;
};

void gf100_ltcg_dtor(struct nouveau_object *);
int  gf100_ltcg_init_tag_ram(struct nouveau_fb *, struct gf100_ltcg_priv *);
int  gf100_ltcg_tags_alloc(struct nouveau_ltcg *, u32, struct nouveau_mm_node **);
void gf100_ltcg_tags_free(struct nouveau_ltcg *, struct nouveau_mm_node **);

#endif
