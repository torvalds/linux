/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVBIOS_M0205_H__
#define __NVBIOS_M0205_H__
struct nvbios_M0205T {
	u16 freq;
};

u32 nvbios_M0205Te(struct nvkm_bios *,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz);
u32 nvbios_M0205Tp(struct nvkm_bios *,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *snr, u8 *ssz,
		   struct nvbios_M0205T *);

struct nvbios_M0205E {
	u8 type;
};

u32 nvbios_M0205Ee(struct nvkm_bios *, int idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u32 nvbios_M0205Ep(struct nvkm_bios *, int idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_M0205E *);

struct nvbios_M0205S {
	u8 data;
};

u32 nvbios_M0205Se(struct nvkm_bios *, int ent, int idx, u8 *ver, u8 *hdr);
u32 nvbios_M0205Sp(struct nvkm_bios *, int ent, int idx, u8 *ver, u8 *hdr,
		   struct nvbios_M0205S *);
#endif
