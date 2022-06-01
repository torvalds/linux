/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0010_H__
#define __NVIF_IF0010_H__

union nvif_disp_args {
	struct nvif_disp_v0 {
		__u8 version;
		__u8 pad01[7];
	} v0;
};
#endif
