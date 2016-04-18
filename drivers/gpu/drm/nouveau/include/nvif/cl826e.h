#ifndef __NVIF_CL826E_H__
#define __NVIF_CL826E_H__

struct g82_channel_dma_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad02[6];
	__u64 vm;
	__u64 pushbuf;
	__u64 offset;
};

#define G82_CHANNEL_DMA_V0_NTFY_UEVENT                                     0x00
#endif
