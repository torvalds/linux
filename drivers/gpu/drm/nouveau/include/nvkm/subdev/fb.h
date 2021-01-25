/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FB_H__
#define __NVKM_FB_H__
#include <core/subdev.h>
#include <core/mm.h>

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

#define NVKM_RAM_TYPE_VM 0x7f
#define NV_MEM_COMP_VM 0x03

struct nvkm_fb_tile {
	struct nvkm_mm_node *tag;
	u32 addr;
	u32 limit;
	u32 pitch;
	u32 zcomp;
};

struct nvkm_fb {
	const struct nvkm_fb_func *func;
	struct nvkm_subdev subdev;

	struct nvkm_blob vpr_scrubber;

	struct nvkm_ram *ram;
	struct nvkm_mm tags;

	struct {
		struct nvkm_fb_tile region[16];
		int regions;
	} tile;

	u8 page;

	struct nvkm_memory *mmu_rd;
	struct nvkm_memory *mmu_wr;
};

void nvkm_fb_tile_init(struct nvkm_fb *, int region, u32 addr, u32 size,
		       u32 pitch, u32 flags, struct nvkm_fb_tile *);
void nvkm_fb_tile_fini(struct nvkm_fb *, int region, struct nvkm_fb_tile *);
void nvkm_fb_tile_prog(struct nvkm_fb *, int region, struct nvkm_fb_tile *);

int nv04_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv10_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv1a_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv20_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv25_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv30_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv35_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv36_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv40_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv41_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv44_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv46_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv47_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv49_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv4e_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int nv50_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int g84_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gt215_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int mcp77_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int mcp89_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gf100_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gf108_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gk104_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gk110_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gk20a_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gm107_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gm200_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gm20b_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gp100_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gp102_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gp10b_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int gv100_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int ga100_fb_new(struct nvkm_device *, int, struct nvkm_fb **);
int ga102_fb_new(struct nvkm_device *, int, struct nvkm_fb **);

#include <subdev/bios.h>
#include <subdev/bios/ramcfg.h>

struct nvkm_ram_data {
	struct list_head head;
	struct nvbios_ramcfg bios;
	u32 freq;
};

enum nvkm_ram_type {
	NVKM_RAM_TYPE_UNKNOWN = 0,
	NVKM_RAM_TYPE_STOLEN,
	NVKM_RAM_TYPE_SGRAM,
	NVKM_RAM_TYPE_SDRAM,
	NVKM_RAM_TYPE_DDR1,
	NVKM_RAM_TYPE_DDR2,
	NVKM_RAM_TYPE_DDR3,
	NVKM_RAM_TYPE_GDDR2,
	NVKM_RAM_TYPE_GDDR3,
	NVKM_RAM_TYPE_GDDR4,
	NVKM_RAM_TYPE_GDDR5,
	NVKM_RAM_TYPE_GDDR5X,
	NVKM_RAM_TYPE_GDDR6,
	NVKM_RAM_TYPE_HBM2,
};

struct nvkm_ram {
	const struct nvkm_ram_func *func;
	struct nvkm_fb *fb;
	enum nvkm_ram_type type;
	u64 size;

#define NVKM_RAM_MM_SHIFT 12
#define NVKM_RAM_MM_ANY    (NVKM_MM_HEAP_ANY + 0)
#define NVKM_RAM_MM_NORMAL (NVKM_MM_HEAP_ANY + 1)
#define NVKM_RAM_MM_NOMAP  (NVKM_MM_HEAP_ANY + 2)
#define NVKM_RAM_MM_MIXED  (NVKM_MM_HEAP_ANY + 3)
	struct nvkm_mm vram;
	u64 stolen;

	int ranks;
	int parts;
	int part_mask;

	u32 freq;
	u32 mr[16];
	u32 mr1_nuts;

	struct nvkm_ram_data *next;
	struct nvkm_ram_data former;
	struct nvkm_ram_data xition;
	struct nvkm_ram_data target;
};

int
nvkm_ram_get(struct nvkm_device *, u8 heap, u8 type, u8 page, u64 size,
	     bool contig, bool back, struct nvkm_memory **);

struct nvkm_ram_func {
	u64 upper;
	u32 (*probe_fbp)(const struct nvkm_ram_func *, struct nvkm_device *,
			 int fbp, int *pltcs);
	u32 (*probe_fbp_amount)(const struct nvkm_ram_func *, u32 fbpao,
				struct nvkm_device *, int fbp, int *pltcs);
	u32 (*probe_fbpa_amount)(struct nvkm_device *, int fbpa);
	void *(*dtor)(struct nvkm_ram *);
	int (*init)(struct nvkm_ram *);

	int (*calc)(struct nvkm_ram *, u32 freq);
	int (*prog)(struct nvkm_ram *);
	void (*tidy)(struct nvkm_ram *);
};
#endif
