#ifndef __NVBIOS_DP_H__
#define __NVBIOS_DP_H__

struct nvbios_dpout {
	u16 type;
	u16 mask;
	u8  flags;
	u32 script[5];
	u32 lnkcmp;
};

u16 nvbios_dpout_parse(struct nouveau_bios *, u8 idx,
		       u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		       struct nvbios_dpout *);
u16 nvbios_dpout_match(struct nouveau_bios *, u16 type, u16 mask,
		       u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		       struct nvbios_dpout *);

struct nvbios_dpcfg {
	u8 pc;
	u8 dc;
	u8 pe;
	u8 tx_pu;
};

u16
nvbios_dpcfg_parse(struct nouveau_bios *, u16 outp, u8 idx,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_dpcfg *);
u16
nvbios_dpcfg_match(struct nouveau_bios *, u16 outp, u8 pc, u8 vs, u8 pe,
		   u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		   struct nvbios_dpcfg *);

#endif
