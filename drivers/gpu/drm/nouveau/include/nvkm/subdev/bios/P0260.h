/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_P0260_H__
#define __NVBIOS_P0260_H__
u32 nvbios_P0260Te(struct nvkm_bios *,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *xnr, u8 *xsz);

struct nvbios_P0260E {
	u32 data;
};

u32 nvbios_P0260Ee(struct nvkm_bios *, int idx, u8 *ver, u8 *hdr);
u32 nvbios_P0260Ep(struct nvkm_bios *, int idx, u8 *ver, u8 *hdr,
		   struct nvbios_P0260E *);

struct nvbios_P0260X {
	u32 data;
};

u32 nvbios_P0260Xe(struct nvkm_bios *, int idx, u8 *ver, u8 *hdr);
u32 nvbios_P0260Xp(struct nvkm_bios *, int idx, u8 *ver, u8 *hdr,
		   struct nvbios_P0260X *);
#endif
