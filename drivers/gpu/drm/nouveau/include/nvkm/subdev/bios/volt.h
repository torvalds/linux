#ifndef __NVBIOS_VOLT_H__
#define __NVBIOS_VOLT_H__
struct nvbios_volt {
	u8  vidmask;
	u32 min;
	u32 max;
	u32 base;
	s16 step;
};

u16 nvbios_volt_table(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_volt_parse(struct nvkm_bios *, u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		      struct nvbios_volt *);

struct nvbios_volt_entry {
	u32 voltage;
	u8  vid;
};

u16 nvbios_volt_entry(struct nvkm_bios *, int idx, u8 *ver, u8 *len);
u16 nvbios_volt_entry_parse(struct nvkm_bios *, int idx, u8 *ver, u8 *len,
			    struct nvbios_volt_entry *);
#endif
