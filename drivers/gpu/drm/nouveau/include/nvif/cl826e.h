/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CL826E_H__
#define __NVIF_CL826E_H__

struct g82_channel_dma_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad02[6];
	__u64 vmm;
	__u64 pushbuf;
	__u64 offset;
};

#define NV826E_V0_NTFY_NON_STALL_INTERRUPT                                 0x00
#endif
