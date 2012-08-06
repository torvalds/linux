#ifndef __NOUVEAU_FB_H__
#define __NOUVEAU_FB_H__

#include <core/subdev.h>
#include <core/device.h>
#include <core/mm.h>

#include <subdev/vm.h>

/* memory type/access flags, do not match hardware values */
#define NV_MEM_ACCESS_RO  1
#define NV_MEM_ACCESS_WO  2
#define NV_MEM_ACCESS_RW (NV_MEM_ACCESS_RO | NV_MEM_ACCESS_WO)
#define NV_MEM_ACCESS_SYS 4
#define NV_MEM_ACCESS_VM  8
#define NV_MEM_ACCESS_NOSNOOP 16

#define NV_MEM_TARGET_VRAM        0
#define NV_MEM_TARGET_PCI         1
#define NV_MEM_TARGET_PCI_NOSNOOP 2
#define NV_MEM_TARGET_VM          3
#define NV_MEM_TARGET_GART        4

#define NV_MEM_TYPE_VM 0x7f
#define NV_MEM_COMP_VM 0x03

struct nouveau_mem {
	struct drm_device *dev;

	struct nouveau_vma bar_vma;
	struct nouveau_vma vma[2];
	u8  page_shift;

	struct nouveau_mm_node *tag;
	struct list_head regions;
	dma_addr_t *pages;
	u32 memtype;
	u64 offset;
	u64 size;
	struct sg_table *sg;
};

struct nouveau_fb_tile {
	struct nouveau_mm_node *tag;
	u32 addr;
	u32 limit;
	u32 pitch;
	u32 zcomp;
};

struct nouveau_fb {
	struct nouveau_subdev base;

	bool (*memtype_valid)(struct nouveau_fb *, u32 memtype);

	struct {
		enum {
			NV_MEM_TYPE_UNKNOWN = 0,
			NV_MEM_TYPE_STOLEN,
			NV_MEM_TYPE_SGRAM,
			NV_MEM_TYPE_SDRAM,
			NV_MEM_TYPE_DDR1,
			NV_MEM_TYPE_DDR2,
			NV_MEM_TYPE_DDR3,
			NV_MEM_TYPE_GDDR2,
			NV_MEM_TYPE_GDDR3,
			NV_MEM_TYPE_GDDR4,
			NV_MEM_TYPE_GDDR5
		} type;
		u64 stolen;
		u64 size;
		int ranks;

		int  (*get)(struct nouveau_fb *, u64 size, u32 align,
			    u32 size_nc, u32 type, struct nouveau_mem **);
		void (*put)(struct nouveau_fb *, struct nouveau_mem **);
	} ram;

	struct nouveau_mm vram;
	struct nouveau_mm tags;

	struct {
		struct nouveau_fb_tile region[16];
		int regions;
		void (*init)(struct nouveau_fb *, int i, u32 addr, u32 size,
			     u32 pitch, u32 flags, struct nouveau_fb_tile *);
		void (*fini)(struct nouveau_fb *, int i,
			     struct nouveau_fb_tile *);
		void (*prog)(struct nouveau_fb *, int i,
			     struct nouveau_fb_tile *);
	} tile;
};

static inline struct nouveau_fb *
nouveau_fb(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_FB];
}

#define nouveau_fb_create(p,e,c,d)                                             \
	nouveau_subdev_create((p), (e), (c), 0, "PFB", "fb", (d))
int  nouveau_fb_created(struct nouveau_fb *);
void nouveau_fb_destroy(struct nouveau_fb *);
int  nouveau_fb_init(struct nouveau_fb *);
#define nouveau_fb_fini(p,s)                                                   \
	nouveau_subdev_fini(&(p)->base, (s))

void _nouveau_fb_dtor(struct nouveau_object *);
int  _nouveau_fb_init(struct nouveau_object *);
#define _nouveau_fb_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv04_fb_oclass;
extern struct nouveau_oclass nv10_fb_oclass;
extern struct nouveau_oclass nv20_fb_oclass;
extern struct nouveau_oclass nv30_fb_oclass;
extern struct nouveau_oclass nv40_fb_oclass;
extern struct nouveau_oclass nv50_fb_oclass;
extern struct nouveau_oclass nvc0_fb_oclass;

struct nouveau_bios;
int  nouveau_fb_bios_memtype(struct nouveau_bios *);

bool nv04_fb_memtype_valid(struct nouveau_fb *, u32 memtype);

void nv10_fb_tile_prog(struct nouveau_fb *, int, struct nouveau_fb_tile *);

void nv30_fb_tile_init(struct nouveau_fb *, int i, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nouveau_fb_tile *);
void nv30_fb_tile_fini(struct nouveau_fb *, int i, struct nouveau_fb_tile *);

void nv50_fb_vram_del(struct nouveau_fb *, struct nouveau_mem **);
void nv50_fb_trap(struct nouveau_fb *, int display);

#endif
