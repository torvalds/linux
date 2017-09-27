#ifndef __NVBIOS_MXM_H__
#define __NVBIOS_MXM_H__
u16 mxm_table(struct nvkm_bios *, u8 *ver, u8 *hdr);
u8  mxm_sor_map(struct nvkm_bios *, u8 conn);
u8  mxm_ddc_map(struct nvkm_bios *, u8 port);
#endif
