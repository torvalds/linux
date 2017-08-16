#ifndef __NVIF_CL507E_H__
#define __NVIF_CL507E_H__

struct nv50_disp_overlay_channel_dma_v0 {
	__u8  version;
	__u8  head;
	__u8  pad02[6];
	__u64 pushbuf;
};

#define NV50_DISP_OVERLAY_CHANNEL_DMA_V0_NTFY_UEVENT                       0x00
#endif
