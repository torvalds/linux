#ifndef __NVBIOS_DP_H__
#define __NVBIOS_DP_H__

u16 dp_table(struct nouveau_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 dp_outp(struct nouveau_bios *, u8 idx, u8 *ver, u8 *len);
u16 dp_outp_match(struct nouveau_bios *, struct dcb_output *, u8 *ver, u8 *len);

#endif
