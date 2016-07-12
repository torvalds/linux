#ifndef __NVBIOS_VMAP_H__
#define __NVBIOS_VMAP_H__
struct nvbios_vmap {
	u8  max0;
	u8  max1;
	u8  max2;
};

u16 nvbios_vmap_table(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_vmap_parse(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_vmap *);

struct nvbios_vmap_entry {
	u8  unk0;
	u8  link;
	u32 min;
	u32 max;
	s32 arg[6];
};

u16 nvbios_vmap_entry(struct nvkm_bios *, int idx, u8 *ver, u8 *len);
u16 nvbios_vmap_entry_parse(struct nvkm_bios *, int idx, u8 *ver, u8 *len,
			    struct nvbios_vmap_entry *);
#endif
