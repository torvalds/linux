/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0000_H__
#define __NVIF_IF0000_H__

struct nvif_client_v0 {
	__u8  version;
	__u8  pad01[7];
	char  name[32];
};
#endif
