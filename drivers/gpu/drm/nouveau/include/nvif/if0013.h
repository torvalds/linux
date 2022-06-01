/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0013_H__
#define __NVIF_IF0013_H__

union nvif_head_args {
	struct nvif_head_v0 {
		__u8 version;
		__u8 id;
		__u8 pad02[6];
	} v0;
};
#endif
