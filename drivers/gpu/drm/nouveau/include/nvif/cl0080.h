/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_CL0080_H__
#define __NVIF_CL0080_H__

struct nv_device_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 device;	/* device identifier, ~0 for client default */
};

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

/* This will be returned for unsupported queries. */
#define NV_DEVICE_INFO_INVALID                                           ~0ULL
#endif
