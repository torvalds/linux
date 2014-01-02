#ifndef __NVBIOS_TIMING_H__
#define __NVBIOS_TIMING_H__

u16 nvbios_timing_table(struct nouveau_bios *,
			u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_timing_entry(struct nouveau_bios *, int idx, u8 *ver, u8 *hdr);

#endif
