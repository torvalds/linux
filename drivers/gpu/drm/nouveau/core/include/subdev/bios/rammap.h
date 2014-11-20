#ifndef __NVBIOS_RAMMAP_H__
#define __NVBIOS_RAMMAP_H__

struct nvbios_ramcfg;

u32 nvbios_rammapTe(struct nouveau_bios *, u8 *ver, u8 *hdr,
		    u8 *cnt, u8 *len, u8 *snr, u8 *ssz);

u32 nvbios_rammapEe(struct nouveau_bios *, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_rammapEp(struct nouveau_bios *, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		    struct nvbios_ramcfg *);
u32 nvbios_rammapEm(struct nouveau_bios *, u16 mhz,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		    struct nvbios_ramcfg *);

u32 nvbios_rammapSe(struct nouveau_bios *, u32 data,
		    u8 ever, u8 ehdr, u8 ecnt, u8 elen, int idx,
		    u8 *ver, u8 *hdr);
u32 nvbios_rammapSp(struct nouveau_bios *, u32 data,
		    u8 ever, u8 ehdr, u8 ecnt, u8 elen, int idx,
		    u8 *ver, u8 *hdr,
		    struct nvbios_ramcfg *);

#endif
