#ifndef __NVIF_CLASS_H__
#define __NVIF_CLASS_H__

/*******************************************************************************
 * class identifiers
 ******************************************************************************/

/* the below match nvidia-assigned (either in hw, or sw) class numbers */
#define NV_DEVICE                                                    0x00000080

#define NV_DMA_FROM_MEMORY                                           0x00000002
#define NV_DMA_TO_MEMORY                                             0x00000003
#define NV_DMA_IN_MEMORY                                             0x0000003d


/*******************************************************************************
 * client
 ******************************************************************************/

#define NV_CLIENT_DEVLIST                                                  0x00

struct nv_client_devlist_v0 {
	__u8  version;
	__u8  count;
	__u8  pad02[6];
	__u64 device[];
};


/*******************************************************************************
 * device
 ******************************************************************************/

struct nv_device_v0 {
	__u8  version;
	__u8  pad01[7];
	__u64 device;	/* device identifier, ~0 for client default */
#define NV_DEVICE_V0_DISABLE_IDENTIFY                     0x0000000000000001ULL
#define NV_DEVICE_V0_DISABLE_MMIO                         0x0000000000000002ULL
#define NV_DEVICE_V0_DISABLE_VBIOS                        0x0000000000000004ULL
#define NV_DEVICE_V0_DISABLE_CORE                         0x0000000000000008ULL
#define NV_DEVICE_V0_DISABLE_DISP                         0x0000000000010000ULL
#define NV_DEVICE_V0_DISABLE_FIFO                         0x0000000000020000ULL
#define NV_DEVICE_V0_DISABLE_GRAPH                        0x0000000100000000ULL
#define NV_DEVICE_V0_DISABLE_MPEG                         0x0000000200000000ULL
#define NV_DEVICE_V0_DISABLE_ME                           0x0000000400000000ULL
#define NV_DEVICE_V0_DISABLE_VP                           0x0000000800000000ULL
#define NV_DEVICE_V0_DISABLE_CRYPT                        0x0000001000000000ULL
#define NV_DEVICE_V0_DISABLE_BSP                          0x0000002000000000ULL
#define NV_DEVICE_V0_DISABLE_PPP                          0x0000004000000000ULL
#define NV_DEVICE_V0_DISABLE_COPY0                        0x0000008000000000ULL
#define NV_DEVICE_V0_DISABLE_COPY1                        0x0000010000000000ULL
#define NV_DEVICE_V0_DISABLE_VIC                          0x0000020000000000ULL
#define NV_DEVICE_V0_DISABLE_VENC                         0x0000040000000000ULL
	__u64 disable;	/* disable particular subsystems */
	__u64 debug0;	/* as above, but *internal* ids, and *NOT* ABI */
};

#define NV_DEVICE_V0_INFO                                                  0x00

struct nv_device_info_v0 {
	__u8  version;
#define NV_DEVICE_INFO_V0_IGP                                              0x00
#define NV_DEVICE_INFO_V0_PCI                                              0x01
#define NV_DEVICE_INFO_V0_AGP                                              0x02
#define NV_DEVICE_INFO_V0_PCIE                                             0x03
#define NV_DEVICE_INFO_V0_SOC                                              0x04
	__u8  platform;
	__u16 chipset;	/* from NV_PMC_BOOT_0 */
	__u8  revision;	/* from NV_PMC_BOOT_0 */
#define NV_DEVICE_INFO_V0_TNT                                              0x01
#define NV_DEVICE_INFO_V0_CELSIUS                                          0x02
#define NV_DEVICE_INFO_V0_KELVIN                                           0x03
#define NV_DEVICE_INFO_V0_RANKINE                                          0x04
#define NV_DEVICE_INFO_V0_CURIE                                            0x05
#define NV_DEVICE_INFO_V0_TESLA                                            0x06
#define NV_DEVICE_INFO_V0_FERMI                                            0x07
#define NV_DEVICE_INFO_V0_KEPLER                                           0x08
#define NV_DEVICE_INFO_V0_MAXWELL                                          0x09
	__u8  family;
	__u8  pad06[2];
	__u64 ram_size;
	__u64 ram_user;
};


/*******************************************************************************
 * context dma
 ******************************************************************************/

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

struct gf110_dma_v0 {
	__u8  version;
#define GF110_DMA_V0_PAGE_LP                                               0x00
#define GF110_DMA_V0_PAGE_SP                                               0x01
	__u8  page;
#define GF110_DMA_V0_KIND_PITCH                                            0x00
#define GF110_DMA_V0_KIND_VM                                               0xff
	__u8  kind;
	__u8  pad03[5];
};

#endif
