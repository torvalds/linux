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

#define NV04_DISP                                                    0x00000046

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

#define NV50_DISP                                                    0x00005070
#define G82_DISP                                                     0x00008270
#define GT200_DISP                                                   0x00008370
#define GT214_DISP                                                   0x00008570
#define GT206_DISP                                                   0x00008870
#define GF110_DISP                                                   0x00009070
#define GK104_DISP                                                   0x00009170
#define GK110_DISP                                                   0x00009270
#define GM107_DISP                                                   0x00009470

#define NV50_DISP_CURSOR                                             0x0000507a
#define G82_DISP_CURSOR                                              0x0000827a
#define GT214_DISP_CURSOR                                            0x0000857a
#define GF110_DISP_CURSOR                                            0x0000907a
#define GK104_DISP_CURSOR                                            0x0000917a

#define NV50_DISP_OVERLAY                                            0x0000507b
#define G82_DISP_OVERLAY                                             0x0000827b
#define GT214_DISP_OVERLAY                                           0x0000857b
#define GF110_DISP_OVERLAY                                           0x0000907b
#define GK104_DISP_OVERLAY                                           0x0000917b

#define NV50_DISP_BASE_CHANNEL_DMA                                   0x0000507c
#define G82_DISP_BASE_CHANNEL_DMA                                    0x0000827c
#define GT200_DISP_BASE_CHANNEL_DMA                                  0x0000837c
#define GT214_DISP_BASE_CHANNEL_DMA                                  0x0000857c
#define GF110_DISP_BASE_CHANNEL_DMA                                  0x0000907c
#define GK104_DISP_BASE_CHANNEL_DMA                                  0x0000917c
#define GK110_DISP_BASE_CHANNEL_DMA                                  0x0000927c

#define NV50_DISP_CORE_CHANNEL_DMA                                   0x0000507d
#define G82_DISP_CORE_CHANNEL_DMA                                    0x0000827d
#define GT200_DISP_CORE_CHANNEL_DMA                                  0x0000837d
#define GT214_DISP_CORE_CHANNEL_DMA                                  0x0000857d
#define GT206_DISP_CORE_CHANNEL_DMA                                  0x0000887d
#define GF110_DISP_CORE_CHANNEL_DMA                                  0x0000907d
#define GK104_DISP_CORE_CHANNEL_DMA                                  0x0000917d
#define GK110_DISP_CORE_CHANNEL_DMA                                  0x0000927d
#define GM107_DISP_CORE_CHANNEL_DMA                                  0x0000947d

#define NV50_DISP_OVERLAY_CHANNEL_DMA                                0x0000507e
#define G82_DISP_OVERLAY_CHANNEL_DMA                                 0x0000827e
#define GT200_DISP_OVERLAY_CHANNEL_DMA                               0x0000837e
#define GT214_DISP_OVERLAY_CHANNEL_DMA                               0x0000857e
#define GF110_DISP_OVERLAY_CONTROL_DMA                               0x0000907e
#define GK104_DISP_OVERLAY_CONTROL_DMA                               0x0000917e

#define FERMI_A                                                      0x00009097
#define FERMI_B                                                      0x00009197
#define FERMI_C                                                      0x00009297

#define KEPLER_A                                                     0x0000a097
#define KEPLER_B                                                     0x0000a197
#define KEPLER_C                                                     0x0000a297

#define MAXWELL_A                                                    0x0000b097

#define FERMI_COMPUTE_A                                              0x000090c0
#define FERMI_COMPUTE_B                                              0x000091c0

#define KEPLER_COMPUTE_A                                             0x0000a0c0
#define KEPLER_COMPUTE_B                                             0x0000a1c0

#define MAXWELL_COMPUTE_A                                            0x0000b0c0


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

#define NV04_DISP_NTFY_VBLANK                                              0x00
#define NV04_DISP_NTFY_CONN                                                0x01

struct nv04_disp_mthd_v0 {
	__u8  version;
#define NV04_DISP_SCANOUTPOS                                               0x00
	__u8  method;
	__u8  head;
	__u8  pad03[5];
};

struct nv04_disp_scanoutpos_v0 {
	__u8  version;
	__u8  pad01[7];
	__s64 time[2];
	__u16 vblanks;
	__u16 vblanke;
	__u16 vtotal;
	__u16 vline;
	__u16 hblanks;
	__u16 hblanke;
	__u16 htotal;
	__u16 hline;
};

/*******************************************************************************
 * display
 ******************************************************************************/

#define NV50_DISP_MTHD                                                     0x00

struct nv50_disp_mthd_v0 {
	__u8  version;
#define NV50_DISP_SCANOUTPOS                                               0x00
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
	__u8  pad02[2];
	__u32 data;
};

struct nv50_disp_sor_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  pad02[6];
};

struct nv50_disp_sor_hda_eld_v0 {
	__u8  version;
	__u8  pad01[7];
	__u8  data[];
};

struct nv50_disp_sor_hdmi_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  max_ac_packet;
	__u8  rekey;
	__u8  pad04[4];
};

struct nv50_disp_sor_lvds_script_v0 {
	__u8  version;
	__u8  pad01[1];
	__u16 script;
	__u8  pad04[4];
};

struct nv50_disp_sor_dp_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  pad02[6];
};

struct nv50_disp_pior_pwr_v0 {
	__u8  version;
	__u8  state;
	__u8  type;
	__u8  pad03[5];
};

/* core */
struct nv50_disp_core_channel_dma_v0 {
	__u8  version;
	__u8  pad01[3];
	__u32 pushbuf;
};

/* cursor immediate */
struct nv50_disp_cursor_v0 {
	__u8  version;
	__u8  head;
	__u8  pad02[6];
};

/* base */
struct nv50_disp_base_channel_dma_v0 {
	__u8  version;
	__u8  pad01[2];
	__u8  head;
	__u32 pushbuf;
};

/* overlay */
struct nv50_disp_overlay_channel_dma_v0 {
	__u8  version;
	__u8  pad01[2];
	__u8  head;
	__u32 pushbuf;
};

/* overlay immediate */
struct nv50_disp_overlay_v0 {
	__u8  version;
	__u8  head;
	__u8  pad02[6];
};


/*******************************************************************************
 * fermi
 ******************************************************************************/

#define FERMI_A_ZBC_COLOR                                                  0x00
#define FERMI_A_ZBC_DEPTH                                                  0x01

struct fermi_a_zbc_color_v0 {
	__u8  version;
#define FERMI_A_ZBC_COLOR_V0_FMT_ZERO                                      0x01
#define FERMI_A_ZBC_COLOR_V0_FMT_UNORM_ONE                                 0x02
#define FERMI_A_ZBC_COLOR_V0_FMT_RF32_GF32_BF32_AF32                       0x04
#define FERMI_A_ZBC_COLOR_V0_FMT_R16_G16_B16_A16                           0x08
#define FERMI_A_ZBC_COLOR_V0_FMT_RN16_GN16_BN16_AN16                       0x0c
#define FERMI_A_ZBC_COLOR_V0_FMT_RS16_GS16_BS16_AS16                       0x10
#define FERMI_A_ZBC_COLOR_V0_FMT_RU16_GU16_BU16_AU16                       0x14
#define FERMI_A_ZBC_COLOR_V0_FMT_RF16_GF16_BF16_AF16                       0x16
#define FERMI_A_ZBC_COLOR_V0_FMT_A8R8G8B8                                  0x18
#define FERMI_A_ZBC_COLOR_V0_FMT_A8RL8GL8BL8                               0x1c
#define FERMI_A_ZBC_COLOR_V0_FMT_A2B10G10R10                               0x20
#define FERMI_A_ZBC_COLOR_V0_FMT_AU2BU10GU10RU10                           0x24
#define FERMI_A_ZBC_COLOR_V0_FMT_A8B8G8R8                                  0x28
#define FERMI_A_ZBC_COLOR_V0_FMT_A8BL8GL8RL8                               0x2c
#define FERMI_A_ZBC_COLOR_V0_FMT_AN8BN8GN8RN8                              0x30
#define FERMI_A_ZBC_COLOR_V0_FMT_AS8BS8GS8RS8                              0x34
#define FERMI_A_ZBC_COLOR_V0_FMT_AU8BU8GU8RU8                              0x38
#define FERMI_A_ZBC_COLOR_V0_FMT_A2R10G10B10                               0x3c
#define FERMI_A_ZBC_COLOR_V0_FMT_BF10GF11RF11                              0x40
	__u8  format;
	__u8  index;
	__u8  pad03[5];
	__u32 ds[4];
	__u32 l2[4];
};

struct fermi_a_zbc_depth_v0 {
	__u8  version;
#define FERMI_A_ZBC_DEPTH_V0_FMT_FP32                                      0x01
	__u8  format;
	__u8  index;
	__u8  pad03[5];
	__u32 ds;
	__u32 l2;
};

#endif
