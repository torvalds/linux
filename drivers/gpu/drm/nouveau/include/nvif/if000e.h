/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF000E_H__
#define __NVIF_IF000E_H__

union nvif_event_args {
	struct nvif_event_v0 {
		__u8 version;
		__u8 wait;
		__u8 pad02[6];
		__u8 data[];
	} v0;
};

#define NVIF_EVENT_V0_ALLOW 0x00
#define NVIF_EVENT_V0_BLOCK 0x01

union nvif_event_allow_args {
	struct nvif_event_allow_vn {
	} vn;
};

union nvif_event_block_args {
	struct nvif_event_block_vn {
	} vn;
};
#endif
