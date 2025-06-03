/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CL0080_H__
#define __NVIF_CL0080_H__

#define NV_DEVICE_V0_INFO                                                  0x00
#define NV_DEVICE_V0_TIME                                                  0x01

struct nv_device_info_v0 {
	__u8  version;
#define NV_DEVICE_INFO_V0_IGP                                              0x00
#define NV_DEVICE_INFO_V0_PCI                                              0x01
#define NV_DEVICE_INFO_V0_AGP                                              0x02
#define NV_DEVICE_INFO_V0_PCIE                                             0x03
#define NV_DEVICE_INFO_V0_SOC                                              0x04
	__u8  platform;
	__u16 chipset;	/* from NV_PMC_BOOT_0 */
	__u8  revision;	/* from NV_PMC_BOOT_0 */
#define NV_DEVICE_INFO_V0_TNT                                              0x01
#define NV_DEVICE_INFO_V0_CELSIUS                                          0x02
#define NV_DEVICE_INFO_V0_KELVIN                                           0x03
#define NV_DEVICE_INFO_V0_RANKINE                                          0x04
#define NV_DEVICE_INFO_V0_CURIE                                            0x05
#define NV_DEVICE_INFO_V0_TESLA                                            0x06
#define NV_DEVICE_INFO_V0_FERMI                                            0x07
#define NV_DEVICE_INFO_V0_KEPLER                                           0x08
#define NV_DEVICE_INFO_V0_MAXWELL                                          0x09
#define NV_DEVICE_INFO_V0_PASCAL                                           0x0a
#define NV_DEVICE_INFO_V0_VOLTA                                            0x0b
#define NV_DEVICE_INFO_V0_TURING                                           0x0c
#define NV_DEVICE_INFO_V0_AMPERE                                           0x0d
#define NV_DEVICE_INFO_V0_ADA                                              0x0e
#define NV_DEVICE_INFO_V0_HOPPER                                           0x0f
#define NV_DEVICE_INFO_V0_BLACKWELL                                        0x10
	__u8  family;
	__u8  pad06[2];
	__u64 ram_size;
	__u64 ram_user;
	char  chip[16];
	char  name[64];
};

struct nv_device_info_v1 {
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	struct nv_device_info_v1_data {
		__u64 mthd; /* NV_DEVICE_INFO_* (see below). */
		__u64 data;
	} data[];
};

struct nv_device_time_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 time;
};

#define NV_DEVICE_INFO_UNIT                               (0xffffffffULL << 32)
#define NV_DEVICE_INFO(n)                          ((n) | (0x00000000ULL << 32))
#define NV_DEVICE_HOST(n)                          ((n) | (0x00000001ULL << 32))

/* This will be returned in the mthd field for unsupported queries. */
#define NV_DEVICE_INFO_INVALID                                           ~0ULL

/* Returns the number of available runlists. */
#define NV_DEVICE_HOST_RUNLISTS                       NV_DEVICE_HOST(0x00000000)
/* Returns the number of available channels (0 if per-runlist). */
#define NV_DEVICE_HOST_CHANNELS                       NV_DEVICE_HOST(0x00000001)

/* Returns a mask of available engine types on runlist(data). */
#define NV_DEVICE_HOST_RUNLIST_ENGINES                NV_DEVICE_HOST(0x00000100)
#define NV_DEVICE_HOST_RUNLIST_ENGINES_SW                            0x00000001
#define NV_DEVICE_HOST_RUNLIST_ENGINES_GR                            0x00000002
#define NV_DEVICE_HOST_RUNLIST_ENGINES_MPEG                          0x00000004
#define NV_DEVICE_HOST_RUNLIST_ENGINES_ME                            0x00000008
#define NV_DEVICE_HOST_RUNLIST_ENGINES_CIPHER                        0x00000010
#define NV_DEVICE_HOST_RUNLIST_ENGINES_BSP                           0x00000020
#define NV_DEVICE_HOST_RUNLIST_ENGINES_VP                            0x00000040
#define NV_DEVICE_HOST_RUNLIST_ENGINES_CE                            0x00000080
#define NV_DEVICE_HOST_RUNLIST_ENGINES_SEC                           0x00000100
#define NV_DEVICE_HOST_RUNLIST_ENGINES_MSVLD                         0x00000200
#define NV_DEVICE_HOST_RUNLIST_ENGINES_MSPDEC                        0x00000400
#define NV_DEVICE_HOST_RUNLIST_ENGINES_MSPPP                         0x00000800
#define NV_DEVICE_HOST_RUNLIST_ENGINES_MSENC                         0x00001000
#define NV_DEVICE_HOST_RUNLIST_ENGINES_VIC                           0x00002000
#define NV_DEVICE_HOST_RUNLIST_ENGINES_SEC2                          0x00004000
#define NV_DEVICE_HOST_RUNLIST_ENGINES_NVDEC                         0x00008000
#define NV_DEVICE_HOST_RUNLIST_ENGINES_NVENC                         0x00010000
#define NV_DEVICE_HOST_RUNLIST_ENGINES_NVJPG                         0x00020000
#define NV_DEVICE_HOST_RUNLIST_ENGINES_OFA                           0x00040000
/* Returns the number of available channels on runlist(data). */
#define NV_DEVICE_HOST_RUNLIST_CHANNELS               NV_DEVICE_HOST(0x00000101)
#endif
