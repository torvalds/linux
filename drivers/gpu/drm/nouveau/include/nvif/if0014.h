/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0014_H__
#define __NVIF_IF0014_H__

union nvif_disp_chan_args {
	struct nvif_disp_chan_v0 {
		__u8  version;
		__u8  id;
		__u8  pad02[6];
		__u64 pushbuf;
	} v0;
};
#endif
