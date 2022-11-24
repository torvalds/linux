/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0010_H__
#define __NVIF_IF0010_H__

union nvif_disp_args {
	struct nvif_disp_v0 {
		__u8 version;
		__u8 pad01[3];
		__u32 conn_mask;
		__u32 outp_mask;
		__u32 head_mask;
	} v0;
};
#endif
