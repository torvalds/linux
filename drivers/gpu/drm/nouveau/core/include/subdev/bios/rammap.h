#ifndef __NVBIOS_RAMMAP_H__
#define __NVBIOS_RAMMAP_H__

u16 nvbios_rammap_table(struct nouveau_bios *, u8 *ver, u8 *hdr,
			u8 *cnt, u8 *len, u8 *snr, u8 *ssz);
u16 nvbios_rammap_entry(struct nouveau_bios *, int idx,
			u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_rammap_match(struct nouveau_bios *, u16 khz,
			u8 *ver, u8 *hdr, u8 *cnt, u8 *len);

#endif
