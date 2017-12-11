/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_CL506E_H__
#define __NVIF_CL506E_H__

struct nv50_channel_dma_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad02[6];
	__u64 vmm;
	__u64 pushbuf;
	__u64 offset;
};
#endif
