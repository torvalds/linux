/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0020_H__
#define __NVIF_IF0020_H__

union nvif_chan_event_args {
	struct nvif_chan_event_v0 {
		__u8 version;
#define NVIF_CHAN_EVENT_V0_NON_STALL_INTR 0x00
#define NVIF_CHAN_EVENT_V0_KILLED         0x01
		__u8 type;
	} v0;
};
#endif
