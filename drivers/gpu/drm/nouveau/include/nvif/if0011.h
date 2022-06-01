/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_IF0011_H__
#define __NVIF_IF0011_H__

union nvif_conn_args {
	struct nvif_conn_v0 {
		__u8 version;
		__u8 id;	/* DCB connector table index. */
		__u8 pad02[6];
	} v0;
};
#endif
