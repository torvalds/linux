#ifndef __NVBIOS_VMAP_H__
#define __NVBIOS_VMAP_H__

struct nouveau_bios;

struct nvbios_vmap {
};

u16 nvbios_vmap_table(struct nouveau_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_vmap_parse(struct nouveau_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_vmap *);

struct nvbios_vmap_entry {
	u8  unk0;
	u8  link;
	u32 min;
	u32 max;
	s32 arg[6];
};

u16 nvbios_vmap_entry(struct nouveau_bios *, int idx, u8 *ver, u8 *len);
u16 nvbios_vmap_entry_parse(struct nouveau_bios *, int idx, u8 *ver, u8 *len,
			    struct nvbios_vmap_entry *);

#endif
