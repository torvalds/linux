#ifndef __NVBIOS_DISP_H__
#define __NVBIOS_DISP_H__

u16 nvbios_disp_table(struct nouveau_bios *,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *sub);

struct nvbios_disp {
	u16 data;
};

u16 nvbios_disp_entry(struct nouveau_bios *, u8 idx,
		      u8 *ver, u8 *hdr__, u8 *sub);
u16 nvbios_disp_parse(struct nouveau_bios *, u8 idx,
		      u8 *ver, u8 *hdr__, u8 *sub,
		      struct nvbios_disp *);

struct nvbios_outp {
	u16 type;
	u16 mask;
	u16 script[3];
};

u16 nvbios_outp_entry(struct nouveau_bios *, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_outp_parse(struct nouveau_bios *, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_outp *);
u16 nvbios_outp_match(struct nouveau_bios *, u16 type, u16 mask,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_outp *);


struct nvbios_ocfg {
	u16 match;
	u16 clkcmp[2];
};

u16 nvbios_ocfg_entry(struct nouveau_bios *, u16 outp, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_ocfg_parse(struct nouveau_bios *, u16 outp, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_ocfg *);
u16 nvbios_ocfg_match(struct nouveau_bios *, u16 outp, u16 type,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_ocfg *);
u16 nvbios_oclk_match(struct nouveau_bios *, u16 cmp, u32 khz);

#endif
