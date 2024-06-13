/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0000_H__
#define __NVIF_IF0000_H__

struct nvif_client_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 device;
	char  name[32];
};

#define NVIF_CLIENT_V0_DEVLIST                                             0x00

struct nvif_client_devlist_v0 {
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	__u64 device[];
};
#endif
