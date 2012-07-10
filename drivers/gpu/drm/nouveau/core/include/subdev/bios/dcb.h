#ifndef __NVBIOS_DCB_H__
#define __NVBIOS_DCB_H__

enum dcb_output_type {
	DCB_OUTPUT_ANALOG	= 0x0,
	DCB_OUTPUT_TV		= 0x1,
	DCB_OUTPUT_TMDS		= 0x2,
	DCB_OUTPUT_LVDS		= 0x3,
	DCB_OUTPUT_DP		= 0x4,
	DCB_OUTPUT_EOL		= 0xe,
	DCB_OUTPUT_UNUSED	= 0xf,
};

u16 dcb_table(struct nouveau_bios *, u8 *ver, u8 *hdr, u8 *ent, u8 *len);
u16 dcb_outp(struct nouveau_bios *, u8 idx, u8 *ver, u8 *len);
int dcb_outp_foreach(struct nouveau_bios *, void *data, int (*exec)
		     (struct nouveau_bios *, void *, int index, u16 entry));

#endif
