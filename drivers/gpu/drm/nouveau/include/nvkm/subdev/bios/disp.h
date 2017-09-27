#ifndef __NVBIOS_DISP_H__
#define __NVBIOS_DISP_H__
u16 nvbios_disp_table(struct nvkm_bios *,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, u8 *sub);

struct nvbios_disp {
	u16 data;
};

u16 nvbios_disp_entry(struct nvkm_bios *, u8 idx, u8 *ver, u8 *hdr, u8 *sub);
u16 nvbios_disp_parse(struct nvkm_bios *, u8 idx, u8 *ver, u8 *hdr, u8 *sub,
		      struct nvbios_disp *);

struct nvbios_outp {
	u16 type;
	u16 mask;
	u16 script[3];
};

u16 nvbios_outp_entry(struct nvkm_bios *, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_outp_parse(struct nvkm_bios *, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_outp *);
u16 nvbios_outp_match(struct nvkm_bios *, u16 type, u16 mask,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_outp *);

struct nvbios_ocfg {
	u8  proto;
	u8  flags;
	u16 clkcmp[2];
};

u16 nvbios_ocfg_entry(struct nvkm_bios *, u16 outp, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_ocfg_parse(struct nvkm_bios *, u16 outp, u8 idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ocfg *);
u16 nvbios_ocfg_match(struct nvkm_bios *, u16 outp, u8 proto, u8 flags,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_ocfg *);
u16 nvbios_oclk_match(struct nvkm_bios *, u16 cmp, u32 khz);
#endif
