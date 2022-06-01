/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0020_H__
#define __NVIF_IF0020_H__

union nvif_chan_args {
	struct nvif_chan_v0 {
		__u8  version;
		__u8  namelen;
		__u8  runlist;
		__u8  runq;
		__u8  priv;
		__u8  pad05;
		__u16 devm;
		__u64 vmm;

		__u64 ctxdma;
		__u64 offset;
		__u64 length;

		__u64 huserd;
		__u64 ouserd;

		__u32 token;
		__u16 chid;
		__u8  pad3e;
#define NVIF_CHAN_V0_INST_APER_VRAM 0
#define NVIF_CHAN_V0_INST_APER_HOST 1
#define NVIF_CHAN_V0_INST_APER_NCOH 2
#define NVIF_CHAN_V0_INST_APER_INST 0xff
		__u8  aper;
		__u64 inst;

		__u8  name[];
	} v0;
};

union nvif_chan_event_args {
	struct nvif_chan_event_v0 {
		__u8 version;
#define NVIF_CHAN_EVENT_V0_NON_STALL_INTR 0x00
#define NVIF_CHAN_EVENT_V0_KILLED         0x01
		__u8 type;
	} v0;
};
#endif
