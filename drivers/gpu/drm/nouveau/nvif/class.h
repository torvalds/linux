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

#define NV03_CHANNEL_DMA                                             0x0000006b
#define NV10_CHANNEL_DMA                                             0x0000006e
#define NV17_CHANNEL_DMA                                             0x0000176e
#define NV40_CHANNEL_DMA                                             0x0000406e
#define NV50_CHANNEL_DMA                                             0x0000506e
#define G82_CHANNEL_DMA                                              0x0000826e

#define NV50_CHANNEL_GPFIFO                                          0x0000506f
#define G82_CHANNEL_GPFIFO                                           0x0000826f
#define FERMI_CHANNEL_GPFIFO                                         0x0000906f
#define KEPLER_CHANNEL_GPFIFO_A                                      0x0000a06f


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


/*******************************************************************************
 * perfmon
 ******************************************************************************/

struct nvif_perfctr_v0 {
	__u8  version;
	__u8  pad01[1];
	__u16 logic_op;
	__u8  pad04[4];
	char  name[4][64];
};

#define NVIF_PERFCTR_V0_QUERY                                              0x00
#define NVIF_PERFCTR_V0_SAMPLE                                             0x01
#define NVIF_PERFCTR_V0_READ                                               0x02

struct nvif_perfctr_query_v0 {
	__u8  version;
	__u8  pad01[3];
	__u32 iter;
	char  name[64];
};

struct nvif_perfctr_sample {
};

struct nvif_perfctr_read_v0 {
	__u8  version;
	__u8  pad01[7];
	__u32 ctr;
	__u32 clk;
};


/*******************************************************************************
 * device control
 ******************************************************************************/

#define NVIF_CONTROL_PSTATE_INFO                                           0x00
#define NVIF_CONTROL_PSTATE_ATTR                                           0x01
#define NVIF_CONTROL_PSTATE_USER                                           0x02

struct nvif_control_pstate_info_v0 {
	__u8  version;
	__u8  count; /* out: number of power states */
#define NVIF_CONTROL_PSTATE_INFO_V0_USTATE_DISABLE                         (-1)
#define NVIF_CONTROL_PSTATE_INFO_V0_USTATE_PERFMON                         (-2)
	__s8  ustate_ac; /* out: target pstate index */
	__s8  ustate_dc; /* out: target pstate index */
	__s8  pwrsrc; /* out: current power source */
#define NVIF_CONTROL_PSTATE_INFO_V0_PSTATE_UNKNOWN                         (-1)
#define NVIF_CONTROL_PSTATE_INFO_V0_PSTATE_PERFMON                         (-2)
	__s8  pstate; /* out: current pstate index */
	__u8  pad06[2];
};

struct nvif_control_pstate_attr_v0 {
	__u8  version;
#define NVIF_CONTROL_PSTATE_ATTR_V0_STATE_CURRENT                          (-1)
	__s8  state; /*  in: index of pstate to query
		      * out: pstate identifier
		      */
	__u8  index; /*  in: index of attribute to query
		      * out: index of next attribute, or 0 if no more
		      */
	__u8  pad03[5];
	__u32 min;
	__u32 max;
	char  name[32];
	char  unit[16];
};

struct nvif_control_pstate_user_v0 {
	__u8  version;
#define NVIF_CONTROL_PSTATE_USER_V0_STATE_UNKNOWN                          (-1)
#define NVIF_CONTROL_PSTATE_USER_V0_STATE_PERFMON                          (-2)
	__s8  ustate; /*  in: pstate identifier */
	__s8  pwrsrc; /*  in: target power source */
	__u8  pad03[5];
};


/*******************************************************************************
 * DMA FIFO channels
 ******************************************************************************/

struct nv03_channel_dma_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad02[2];
	__u32 pushbuf;
	__u64 offset;
};

#define G82_CHANNEL_DMA_V0_NTFY_UEVENT                                     0x00

/*******************************************************************************
 * GPFIFO channels
 ******************************************************************************/

struct nv50_channel_gpfifo_v0 {
	__u8  version;
	__u8  chid;
	__u8  pad01[6];
	__u32 pushbuf;
	__u32 ilength;
	__u64 ioffset;
};

struct kepler_channel_gpfifo_a_v0 {
	__u8  version;
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_GR                               0x01
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_VP                               0x02
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_PPP                              0x04
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_BSP                              0x08
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_CE0                              0x10
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_CE1                              0x20
#define KEPLER_CHANNEL_GPFIFO_A_V0_ENGINE_ENC                              0x40
	__u8  engine;
	__u16 chid;
	__u8  pad04[4];
	__u32 pushbuf;
	__u32 ilength;
	__u64 ioffset;
};

/*******************************************************************************
 * legacy display
 ******************************************************************************/


/*******************************************************************************
 * display
 ******************************************************************************/

#define NV50_DISP_MTHD                                                     0x00

struct nv50_disp_mthd_v0 {
	__u8  version;
	__u8  method;
	__u8  head;
	__u8  pad03[5];
};

struct nv50_disp_mthd_v1 {
	__u8  version;
#define NV50_DISP_MTHD_V1_DAC_PWR                                          0x10
#define NV50_DISP_MTHD_V1_DAC_LOAD                                         0x11
#define NV50_DISP_MTHD_V1_SOR_PWR                                          0x20
#define NV50_DISP_MTHD_V1_SOR_HDA_ELD                                      0x21
#define NV50_DISP_MTHD_V1_SOR_HDMI_PWR                                     0x22
#define NV50_DISP_MTHD_V1_SOR_LVDS_SCRIPT                                  0x23
#define NV50_DISP_MTHD_V1_SOR_DP_PWR                                       0x24
#define NV50_DISP_MTHD_V1_PIOR_PWR                                         0x30
	__u8  method;
	__u16 hasht;
	__u16 hashm;
	__u8  pad06[2];
};

struct nv50_disp_dac_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  data;
	__u8  vsync;
	__u8  hsync;
	__u8  pad05[3];
};

struct nv50_disp_dac_load_v0 {
	__u8  version;
	__u8  load;
	__u16 data;
	__u8  pad04[4];
};

struct nv50_disp_sor_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  pad02[6];
};

#endif
