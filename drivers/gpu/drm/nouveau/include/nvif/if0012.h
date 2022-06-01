/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0012_H__
#define __NVIF_IF0012_H__

union nvif_outp_args {
	struct nvif_outp_v0 {
		__u8 version;
		__u8 id;	/* DCB device index. */
		__u8 pad02[6];
	} v0;
};

#define NVIF_OUTP_V0_LOAD_DETECT 0x00
#define NVIF_OUTP_V0_ACQUIRE     0x01
#define NVIF_OUTP_V0_RELEASE     0x02

union nvif_outp_load_detect_args {
	struct nvif_outp_load_detect_v0 {
		__u8  version;
		__u8  load;
		__u8  pad02[2];
		__u32 data; /*TODO: move vbios loadval parsing into nvkm */
	} v0;
};

union nvif_outp_acquire_args {
	struct nvif_outp_acquire_v0 {
		__u8 version;
#define NVIF_OUTP_ACQUIRE_V0_RGB_CRT 0x00
#define NVIF_OUTP_ACQUIRE_V0_TV      0x01
#define NVIF_OUTP_ACQUIRE_V0_TMDS    0x02
#define NVIF_OUTP_ACQUIRE_V0_LVDS    0x03
#define NVIF_OUTP_ACQUIRE_V0_DP      0x04
		__u8 proto;
		__u8 or;
		__u8 link;
		__u8 pad04[4];
		union {
			struct {
				__u8 hda;
				__u8 pad01[7];
			} tmds;
			struct {
				__u8 dual;
				__u8 bpc8;
				__u8 pad02[6];
			} lvds;
			struct {
				__u8 hda;
				__u8 pad01[7];
			} dp;
		};
	} v0;
};

union nvif_outp_release_args {
	struct nvif_outp_release_vn {
	} vn;
};
#endif
