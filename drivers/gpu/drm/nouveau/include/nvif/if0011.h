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

#define NVIF_CONN_V0_HPD_STATUS 0x00000000

union nvif_conn_hpd_status_args {
	struct nvif_conn_hpd_status_v0 {
		__u8 version;
		__u8 support;
		__u8 present;
		__u8 pad03[5];
	} v0;
};
#endif
