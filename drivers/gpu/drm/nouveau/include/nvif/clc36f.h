/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CLC36F_H__
#define __NVIF_CLC36F_H__

struct volta_channel_gpfifo_a_v0 {
	__u8  version;
	__u8  priv;
	__u16 chid;
	__u32 ilength;
	__u64 ioffset;
	__u64 runlist;
	__u64 vmm;
	__u64 inst;
	__u32 token;
};
#endif
