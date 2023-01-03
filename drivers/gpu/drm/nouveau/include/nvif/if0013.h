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

union nvif_head_event_args {
	struct nvif_head_event_vn {
	} vn;
};

#define NVIF_HEAD_V0_SCANOUTPOS 0x00

union nvif_head_scanoutpos_args {
	struct nvif_head_scanoutpos_v0 {
		__u8  version;
		__u8  pad01[7];
		__s64 time[2];
		__u16 vblanks;
		__u16 vblanke;
		__u16 vtotal;
		__u16 vline;
		__u16 hblanks;
		__u16 hblanke;
		__u16 htotal;
		__u16 hline;
	} v0;
};
#endif
