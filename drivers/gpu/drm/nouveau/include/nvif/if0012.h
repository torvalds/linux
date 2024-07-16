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

union nvif_outp_load_detect_args {
	struct nvif_outp_load_detect_v0 {
		__u8  version;
		__u8  load;
		__u8  pad02[2];
		__u32 data; /*TODO: move vbios loadval parsing into nvkm */
	} v0;
};
#endif
