/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVIF_CL0002_H__
#define __NVIF_CL0002_H__

struct nv_dma_v0 {
	__u8  version;
#define NV_DMA_V0_TARGET_VM                                                0x00
#define NV_DMA_V0_TARGET_VRAM                                              0x01
#define NV_DMA_V0_TARGET_PCI                                               0x02
#define NV_DMA_V0_TARGET_PCI_US                                            0x03
#define NV_DMA_V0_TARGET_AGP                                               0x04
	__u8  target;
#define NV_DMA_V0_ACCESS_VM                                                0x00
#define NV_DMA_V0_ACCESS_RD                                                0x01
#define NV_DMA_V0_ACCESS_WR                                                0x02
#define NV_DMA_V0_ACCESS_RDWR                 (NV_DMA_V0_ACCESS_RD | NV_DMA_V0_ACCESS_WR)
	__u8  access;
	__u8  pad03[5];
	__u64 start;
	__u64 limit;
	/* ... chipset-specific class data */
};

struct nv50_dma_v0 {
	__u8  version;
#define NV50_DMA_V0_PRIV_VM                                                0x00
#define NV50_DMA_V0_PRIV_US                                                0x01
#define NV50_DMA_V0_PRIV__S                                                0x02
	__u8  priv;
#define NV50_DMA_V0_PART_VM                                                0x00
#define NV50_DMA_V0_PART_256                                               0x01
#define NV50_DMA_V0_PART_1KB                                               0x02
	__u8  part;
#define NV50_DMA_V0_COMP_NONE                                              0x00
#define NV50_DMA_V0_COMP_1                                                 0x01
#define NV50_DMA_V0_COMP_2                                                 0x02
#define NV50_DMA_V0_COMP_VM                                                0x03
	__u8  comp;
#define NV50_DMA_V0_KIND_PITCH                                             0x00
#define NV50_DMA_V0_KIND_VM                                                0x7f
	__u8  kind;
	__u8  pad05[3];
};

struct gf100_dma_v0 {
	__u8  version;
#define GF100_DMA_V0_PRIV_VM                                               0x00
#define GF100_DMA_V0_PRIV_US                                               0x01
#define GF100_DMA_V0_PRIV__S                                               0x02
	__u8  priv;
#define GF100_DMA_V0_KIND_PITCH                                            0x00
#define GF100_DMA_V0_KIND_VM                                               0xff
	__u8  kind;
	__u8  pad03[5];
};

struct gf119_dma_v0 {
	__u8  version;
#define GF119_DMA_V0_PAGE_LP                                               0x00
#define GF119_DMA_V0_PAGE_SP                                               0x01
	__u8  page;
#define GF119_DMA_V0_KIND_PITCH                                            0x00
#define GF119_DMA_V0_KIND_VM                                               0xff
	__u8  kind;
	__u8  pad03[5];
};
#endif
