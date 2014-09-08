#ifndef __NVBIOS_RAMCFG_H__
#define __NVBIOS_RAMCFG_H__

struct nouveau_bios;

struct nvbios_ramcfg {
	unsigned rammap_ver;
	unsigned rammap_hdr;
	unsigned rammap_min;
	unsigned rammap_max;
	unsigned rammap_11_08_01:1;
	unsigned rammap_11_08_0c:2;
	unsigned rammap_11_08_10:1;
	unsigned rammap_11_11_0c:2;

	unsigned ramcfg_ver;
	unsigned ramcfg_hdr;
	unsigned ramcfg_timing;
	unsigned ramcfg_11_01_01:1;
	unsigned ramcfg_11_01_02:1;
	unsigned ramcfg_11_01_04:1;
	unsigned ramcfg_11_01_08:1;
	unsigned ramcfg_11_01_10:1;
	unsigned ramcfg_11_01_20:1;
	unsigned ramcfg_11_01_40:1;
	unsigned ramcfg_11_01_80:1;
	unsigned ramcfg_11_02_03:2;
	unsigned ramcfg_11_02_04:1;
	unsigned ramcfg_11_02_08:1;
	unsigned ramcfg_11_02_10:1;
	unsigned ramcfg_11_02_40:1;
	unsigned ramcfg_11_02_80:1;
	unsigned ramcfg_11_03_0f:4;
	unsigned ramcfg_11_03_30:2;
	unsigned ramcfg_11_03_c0:2;
	unsigned ramcfg_11_03_f0:4;
	unsigned ramcfg_11_04:8;
	unsigned ramcfg_11_06:8;
	unsigned ramcfg_11_07_02:1;
	unsigned ramcfg_11_07_04:1;
	unsigned ramcfg_11_07_08:1;
	unsigned ramcfg_11_07_10:1;
	unsigned ramcfg_11_07_40:1;
	unsigned ramcfg_11_07_80:1;
	unsigned ramcfg_11_08_01:1;
	unsigned ramcfg_11_08_02:1;
	unsigned ramcfg_11_08_04:1;
	unsigned ramcfg_11_08_08:1;
	unsigned ramcfg_11_08_10:1;
	unsigned ramcfg_11_08_20:1;
	unsigned ramcfg_11_09:8;

	unsigned timing_ver;
	unsigned timing_hdr;
	unsigned timing[11];
	unsigned timing_20_2e_03:2;
	unsigned timing_20_2e_30:2;
	unsigned timing_20_2e_c0:2;
	unsigned timing_20_2f_03:2;
	unsigned timing_20_2c_003f:6;
	unsigned timing_20_2c_1fc0:7;
	unsigned timing_20_30_f8:5;
	unsigned timing_20_30_07:3;
	unsigned timing_20_31_0007:3;
	unsigned timing_20_31_0078:4;
	unsigned timing_20_31_0780:4;
	unsigned timing_20_31_0800:1;
	unsigned timing_20_31_7000:3;
	unsigned timing_20_31_8000:1;
};

u8 nvbios_ramcfg_count(struct nouveau_bios *);
u8 nvbios_ramcfg_index(struct nouveau_subdev *);

#endif
