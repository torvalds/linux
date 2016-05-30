#ifndef __NVKM_FB_PRIV_H__
#define __NVKM_FB_PRIV_H__
#define nvkm_fb(p) container_of((p), struct nvkm_fb, subdev)
#include <subdev/fb.h>
struct nvkm_bios;

struct nvkm_fb_func {
	void *(*dtor)(struct nvkm_fb *);
	int (*oneinit)(struct nvkm_fb *);
	void (*init)(struct nvkm_fb *);
	void (*intr)(struct nvkm_fb *);

	struct {
		int regions;
		void (*init)(struct nvkm_fb *, int i, u32 addr, u32 size,
			     u32 pitch, u32 flags, struct nvkm_fb_tile *);
		void (*comp)(struct nvkm_fb *, int i, u32 size, u32 flags,
			     struct nvkm_fb_tile *);
		void (*fini)(struct nvkm_fb *, int i, struct nvkm_fb_tile *);
		void (*prog)(struct nvkm_fb *, int i, struct nvkm_fb_tile *);
	} tile;

	int (*ram_new)(struct nvkm_fb *, struct nvkm_ram **);

	bool (*memtype_valid)(struct nvkm_fb *, u32 memtype);
};

void nvkm_fb_ctor(const struct nvkm_fb_func *, struct nvkm_device *device,
		  int index, struct nvkm_fb *);
int nvkm_fb_new_(const struct nvkm_fb_func *, struct nvkm_device *device,
		 int index, struct nvkm_fb **);
int nvkm_fb_bios_memtype(struct nvkm_bios *);

bool nv04_fb_memtype_valid(struct nvkm_fb *, u32 memtype);

void nv10_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);
void nv10_fb_tile_fini(struct nvkm_fb *, int i, struct nvkm_fb_tile *);
void nv10_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

void nv20_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);
void nv20_fb_tile_fini(struct nvkm_fb *, int i, struct nvkm_fb_tile *);
void nv20_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

void nv30_fb_init(struct nvkm_fb *);
void nv30_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);

void nv40_fb_tile_comp(struct nvkm_fb *, int i, u32 size, u32 flags,
		       struct nvkm_fb_tile *);

void nv41_fb_init(struct nvkm_fb *);
void nv41_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

void nv44_fb_init(struct nvkm_fb *);
void nv44_fb_tile_prog(struct nvkm_fb *, int, struct nvkm_fb_tile *);

void nv46_fb_tile_init(struct nvkm_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);

int gf100_fb_oneinit(struct nvkm_fb *);
bool gf100_fb_memtype_valid(struct nvkm_fb *, u32);
#endif
