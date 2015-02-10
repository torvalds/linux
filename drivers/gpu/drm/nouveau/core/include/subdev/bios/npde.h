#ifndef __NVBIOS_NPDE_H__
#define __NVBIOS_NPDE_H__

struct nvbios_npdeT {
	u32 image_size;
	bool last;
};

u32 nvbios_npdeTe(struct nouveau_bios *, u32);
u32 nvbios_npdeTp(struct nouveau_bios *, u32, struct nvbios_npdeT *);

#endif
