/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0003_H__
#define __NVIF_IF0003_H__

struct nvif_perfdom_v0 {
	__u8  version;
	__u8  domain;
	__u8  mode;
	__u8  pad03[1];
	struct {
		__u8  signal[4];
		__u64 source[4][8];
		__u16 logic_op;
	} ctr[4];
};

#define NVIF_PERFDOM_V0_INIT                                               0x00
#define NVIF_PERFDOM_V0_SAMPLE                                             0x01
#define NVIF_PERFDOM_V0_READ                                               0x02

struct nvif_perfdom_init {
};

struct nvif_perfdom_sample {
};

struct nvif_perfdom_read_v0 {
	__u8  version;
	__u8  pad01[7];
	__u32 ctr[4];
	__u32 clk;
	__u8  pad04[4];
};
#endif
