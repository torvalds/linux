#ifndef __NV50_KMS_LUT_H__
#define __NV50_KMS_LUT_H__
#include <nvif/mem.h>
struct drm_property_blob;
struct nv50_disp;

struct nv50_lut {
	struct nvif_mem mem[2];
};

int nv50_lut_init(struct nv50_disp *, struct nvif_mmu *, struct nv50_lut *);
void nv50_lut_fini(struct nv50_lut *);
u32 nv50_lut_load(struct nv50_lut *, bool legacy, int buffer,
		  struct drm_property_blob *);
#endif
