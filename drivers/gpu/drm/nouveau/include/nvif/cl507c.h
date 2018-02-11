/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_CL507C_H__
#define __NVIF_CL507C_H__

struct nv50_disp_base_channel_dma_v0 {
	__u8  version;
	__u8  head;
	__u8  pad02[6];
	__u64 pushbuf;
};

#define NV50_DISP_BASE_CHANNEL_DMA_V0_NTFY_UEVENT                          0x00
#endif
