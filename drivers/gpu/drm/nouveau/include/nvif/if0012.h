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
#endif
