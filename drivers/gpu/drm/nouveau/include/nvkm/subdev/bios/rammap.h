#ifndef __NVBIOS_RAMMAP_H__
#define __NVBIOS_RAMMAP_H__
#include <subdev/bios/ramcfg.h>

u32 nvbios_rammapTe(struct nvkm_bios *, u8 *ver, u8 *hdr,
		    u8 *cnt, u8 *len, u8 *snr, u8 *ssz);

u32 nvbios_rammapEe(struct nvkm_bios *, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_rammapEp(struct nvkm_bios *, int idx,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ramcfg *);
u32 nvbios_rammapEm(struct nvkm_bios *, u16 mhz,
		    u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ramcfg *);

u32 nvbios_rammapSe(struct nvkm_bios *, u32 data,
		    u8 ever, u8 ehdr, u8 ecnt, u8 elen, int idx,
		    u8 *ver, u8 *hdr);
u32 nvbios_rammapSp(struct nvkm_bios *, u32 data,
		    u8 ever, u8 ehdr, u8 ecnt, u8 elen, int idx,
		    u8 *ver, u8 *hdr, struct nvbios_ramcfg *);
#endif
