/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_CL826F_H__
#define __NVIF_CL826F_H__

struct g82_channel_gpfifo_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad02[2];
	__u32 ilength;
	__u64 ioffset;
	__u64 pushbuf;
	__u64 vmm;
};

#define NV826F_V0_NTFY_NON_STALL_INTERRUPT                                 0x00
#endif
