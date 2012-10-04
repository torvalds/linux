#ifndef __NOUVEAU_CLASS_H__
#define __NOUVEAU_CLASS_H__

/* Device class
 *
 * 0080: NV_DEVICE
 */
#define NV_DEVICE_CLASS                                              0x00000080

#define NV_DEVICE_DISABLE_IDENTIFY                        0x0000000000000001ULL
#define NV_DEVICE_DISABLE_MMIO                            0x0000000000000002ULL
#define NV_DEVICE_DISABLE_VBIOS                           0x0000000000000004ULL
#define NV_DEVICE_DISABLE_CORE                            0x0000000000000008ULL
#define NV_DEVICE_DISABLE_DISP                            0x0000000000010000ULL
#define NV_DEVICE_DISABLE_FIFO                            0x0000000000020000ULL
#define NV_DEVICE_DISABLE_GRAPH                           0x0000000100000000ULL
#define NV_DEVICE_DISABLE_MPEG                            0x0000000200000000ULL
#define NV_DEVICE_DISABLE_ME                              0x0000000400000000ULL
#define NV_DEVICE_DISABLE_VP                              0x0000000800000000ULL
#define NV_DEVICE_DISABLE_CRYPT                           0x0000001000000000ULL
#define NV_DEVICE_DISABLE_BSP                             0x0000002000000000ULL
#define NV_DEVICE_DISABLE_PPP                             0x0000004000000000ULL
#define NV_DEVICE_DISABLE_COPY0                           0x0000008000000000ULL
#define NV_DEVICE_DISABLE_COPY1                           0x0000010000000000ULL
#define NV_DEVICE_DISABLE_UNK1C1                          0x0000020000000000ULL

struct nv_device_class {
	u64 device;	/* device identifier, ~0 for client default */
	u64 disable;	/* disable particular subsystems */
	u64 debug0;	/* as above, but *internal* ids, and *NOT* ABI */
};

/* DMA object classes
 *
 * 0002: NV_DMA_FROM_MEMORY
 * 0003: NV_DMA_TO_MEMORY
 * 003d: NV_DMA_IN_MEMORY
 */
#define NV_DMA_FROM_MEMORY_CLASS                                     0x00000002
#define NV_DMA_TO_MEMORY_CLASS                                       0x00000003
#define NV_DMA_IN_MEMORY_CLASS                                       0x0000003d

#define NV_DMA_TARGET_MASK                                           0x000000ff
#define NV_DMA_TARGET_VM                                             0x00000000
#define NV_DMA_TARGET_VRAM                                           0x00000001
#define NV_DMA_TARGET_PCI                                            0x00000002
#define NV_DMA_TARGET_PCI_US                                         0x00000003
#define NV_DMA_TARGET_AGP                                            0x00000004
#define NV_DMA_ACCESS_MASK                                           0x00000f00
#define NV_DMA_ACCESS_VM                                             0x00000000
#define NV_DMA_ACCESS_RD                                             0x00000100
#define NV_DMA_ACCESS_WR                                             0x00000200
#define NV_DMA_ACCESS_RDWR                                           0x00000300

struct nv_dma_class {
	u32 flags;
	u32 pad0;
	u64 start;
	u64 limit;
};

/* DMA FIFO channel classes
 *
 * 006b: NV03_CHANNEL_DMA
 * 006e: NV10_CHANNEL_DMA
 * 176e: NV17_CHANNEL_DMA
 * 406e: NV40_CHANNEL_DMA
 * 506e: NV50_CHANNEL_DMA
 * 826e: NV84_CHANNEL_DMA
 */
#define NV03_CHANNEL_DMA_CLASS                                       0x0000006b
#define NV10_CHANNEL_DMA_CLASS                                       0x0000006e
#define NV17_CHANNEL_DMA_CLASS                                       0x0000176e
#define NV40_CHANNEL_DMA_CLASS                                       0x0000406e
#define NV50_CHANNEL_DMA_CLASS                                       0x0000506e
#define NV84_CHANNEL_DMA_CLASS                                       0x0000826e

struct nv03_channel_dma_class {
	u32 pushbuf;
	u32 pad0;
	u64 offset;
};

/* Indirect FIFO channel classes
 *
 * 506f: NV50_CHANNEL_IND
 * 826f: NV84_CHANNEL_IND
 * 906f: NVC0_CHANNEL_IND
 * a06f: NVE0_CHANNEL_IND
 */

#define NV50_CHANNEL_IND_CLASS                                       0x0000506f
#define NV84_CHANNEL_IND_CLASS                                       0x0000826f
#define NVC0_CHANNEL_IND_CLASS                                       0x0000906f
#define NVE0_CHANNEL_IND_CLASS                                       0x0000a06f

struct nv50_channel_ind_class {
	u32 pushbuf;
	u32 ilength;
	u64 ioffset;
};

#define NVE0_CHANNEL_IND_ENGINE_GR                                   0x00000001
#define NVE0_CHANNEL_IND_ENGINE_VP                                   0x00000002
#define NVE0_CHANNEL_IND_ENGINE_PPP                                  0x00000004
#define NVE0_CHANNEL_IND_ENGINE_BSP                                  0x00000008
#define NVE0_CHANNEL_IND_ENGINE_CE0                                  0x00000010
#define NVE0_CHANNEL_IND_ENGINE_CE1                                  0x00000020
#define NVE0_CHANNEL_IND_ENGINE_ENC                                  0x00000040

struct nve0_channel_ind_class {
	u32 pushbuf;
	u32 ilength;
	u64 ioffset;
	u32 engine;
};

#endif
