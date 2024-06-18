/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0021_H__
#define __NVIF_IF0021_H__

union nvif_cgrp_args {
	struct nvif_cgrp_v0 {
		__u8  version;
		__u8  namelen;
		__u8  runlist;
		__u8  pad03[3];
		__u16 cgid;
		__u64 vmm;
		__u8  name[];
	} v0;
};
#endif
