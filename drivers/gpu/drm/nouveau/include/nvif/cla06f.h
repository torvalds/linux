/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_CLA06F_H__
#define __NVIF_CLA06F_H__

struct kepler_channel_gpfifo_a_v0 {
	__u8  version;
	__u8  pad01[1];
	__u16 chid;
	__u32 ilength;
	__u64 ioffset;
	__u64 runlist;
	__u64 vmm;
};

#define NVA06F_V0_NTFY_NON_STALL_INTERRUPT                                 0x00
#define NVA06F_V0_NTFY_KILLED                                              0x01
#endif
