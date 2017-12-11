/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_IF0004_H__
#define __NVIF_IF0004_H__

#define NV04_NVSW_NTFY_UEVENT                                              0x00

#define NV04_NVSW_GET_REF                                                  0x00

struct nv04_nvsw_get_ref_v0 {
	__u8  version;
	__u8  pad01[3];
	__u32 ref;
};
#endif
