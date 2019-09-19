/* SPDX-License-Identifier: MIT */
#ifndef __NVBIOS_M0209_H__
#define __NVBIOS_M0209_H__
u32 nvbios_M0209Te(struct nvkm_bios *,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz);

struct nvbios_M0209E {
	u8 v00_40;
	u8 bits;
	u8 modulo;
	u8 v02_40;
	u8 v02_07;
	u8 v03;
};

u32 nvbios_M0209Ee(struct nvkm_bios *, int idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_M0209Ep(struct nvkm_bios *, int idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_M0209E *);

struct nvbios_M0209S {
	u32 data[0x200];
};

u32 nvbios_M0209Se(struct nvkm_bios *, int ent, int idx, u8 *ver, u8 *hdr);
u32 nvbios_M0209Sp(struct nvkm_bios *, int ent, int idx, u8 *ver, u8 *hdr,
		   struct nvbios_M0209S *);
#endif
