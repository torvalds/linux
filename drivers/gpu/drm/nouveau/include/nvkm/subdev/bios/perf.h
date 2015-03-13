#ifndef __NVBIOS_PERF_H__
#define __NVBIOS_PERF_H__
u16 nvbios_perf_table(struct nvkm_bios *, u8 *ver, u8 *hdr,
		      u8 *cnt, u8 *len, u8 *snr, u8 *ssz);

struct nvbios_perfE {
	u8  pstate;
	u8  fanspeed;
	u8  voltage;
	u32 core;
	u32 shader;
	u32 memory;
	u32 vdec;
	u32 disp;
	u32 script;
};

u16 nvbios_perf_entry(struct nvkm_bios *, int idx,
		      u8 *ver, u8 *hdr, u8 *cnt, u8 *len);
u16 nvbios_perfEp(struct nvkm_bios *, int idx,
		  u8 *ver, u8 *hdr, u8 *cnt, u8 *len, struct nvbios_perfE *);

struct nvbios_perfS {
	union {
		struct {
			u32 freq;
		} v40;
	};
};

u32 nvbios_perfSe(struct nvkm_bios *, u32 data, int idx,
		  u8 *ver, u8 *hdr, u8 cnt, u8 len);
u32 nvbios_perfSp(struct nvkm_bios *, u32 data, int idx,
		  u8 *ver, u8 *hdr, u8 cnt, u8 len, struct nvbios_perfS *);

struct nvbios_perf_fan {
	u32 pwm_divisor;
};

int nvbios_perf_fan_parse(struct nvkm_bios *, struct nvbios_perf_fan *);
#endif
