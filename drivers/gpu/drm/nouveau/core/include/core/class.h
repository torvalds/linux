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
#define NV_DEVICE_DISABLE_VENC                            0x0000040000000000ULL

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

/* NV50:NVC0 */
#define NV50_DMA_CONF0_ENABLE                                        0x80000000
#define NV50_DMA_CONF0_PRIV                                          0x00300000
#define NV50_DMA_CONF0_PRIV_VM                                       0x00000000
#define NV50_DMA_CONF0_PRIV_US                                       0x00100000
#define NV50_DMA_CONF0_PRIV__S                                       0x00200000
#define NV50_DMA_CONF0_PART                                          0x00030000
#define NV50_DMA_CONF0_PART_VM                                       0x00000000
#define NV50_DMA_CONF0_PART_256                                      0x00010000
#define NV50_DMA_CONF0_PART_1KB                                      0x00020000
#define NV50_DMA_CONF0_COMP                                          0x00000180
#define NV50_DMA_CONF0_COMP_NONE                                     0x00000000
#define NV50_DMA_CONF0_COMP_VM                                       0x00000180
#define NV50_DMA_CONF0_TYPE                                          0x0000007f
#define NV50_DMA_CONF0_TYPE_LINEAR                                   0x00000000
#define NV50_DMA_CONF0_TYPE_VM                                       0x0000007f

/* NVC0:NVD9 */
#define NVC0_DMA_CONF0_ENABLE                                        0x80000000
#define NVC0_DMA_CONF0_PRIV                                          0x00300000
#define NVC0_DMA_CONF0_PRIV_VM                                       0x00000000
#define NVC0_DMA_CONF0_PRIV_US                                       0x00100000
#define NVC0_DMA_CONF0_PRIV__S                                       0x00200000
#define NVC0_DMA_CONF0_UNKN /* PART? */                              0x00030000
#define NVC0_DMA_CONF0_TYPE                                          0x000000ff
#define NVC0_DMA_CONF0_TYPE_LINEAR                                   0x00000000
#define NVC0_DMA_CONF0_TYPE_VM                                       0x000000ff

/* NVD9- */
#define NVD0_DMA_CONF0_ENABLE                                        0x80000000
#define NVD0_DMA_CONF0_PAGE                                          0x00000400
#define NVD0_DMA_CONF0_PAGE_LP                                       0x00000000
#define NVD0_DMA_CONF0_PAGE_SP                                       0x00000400
#define NVD0_DMA_CONF0_TYPE                                          0x000000ff
#define NVD0_DMA_CONF0_TYPE_LINEAR                                   0x00000000
#define NVD0_DMA_CONF0_TYPE_VM                                       0x000000ff

struct nv_dma_class {
	u32 flags;
	u32 pad0;
	u64 start;
	u64 limit;
	u32 conf0;
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

/* 0046: NV04_DISP
 */

#define NV04_DISP_CLASS                                              0x00000046

struct nv04_display_class {
};

/* 5070: NV50_DISP
 * 8270: NV84_DISP
 * 8370: NVA0_DISP
 * 8870: NV94_DISP
 * 8570: NVA3_DISP
 * 9070: NVD0_DISP
 * 9170: NVE0_DISP
 * 9270: NVF0_DISP
 */

#define NV50_DISP_CLASS                                              0x00005070
#define NV84_DISP_CLASS                                              0x00008270
#define NVA0_DISP_CLASS                                              0x00008370
#define NV94_DISP_CLASS                                              0x00008870
#define NVA3_DISP_CLASS                                              0x00008570
#define NVD0_DISP_CLASS                                              0x00009070
#define NVE0_DISP_CLASS                                              0x00009170
#define NVF0_DISP_CLASS                                              0x00009270

#define NV50_DISP_SOR_MTHD                                           0x00010000
#define NV50_DISP_SOR_MTHD_TYPE                                      0x0000f000
#define NV50_DISP_SOR_MTHD_HEAD                                      0x00000018
#define NV50_DISP_SOR_MTHD_LINK                                      0x00000004
#define NV50_DISP_SOR_MTHD_OR                                        0x00000003

#define NV50_DISP_SOR_PWR                                            0x00010000
#define NV50_DISP_SOR_PWR_STATE                                      0x00000001
#define NV50_DISP_SOR_PWR_STATE_ON                                   0x00000001
#define NV50_DISP_SOR_PWR_STATE_OFF                                  0x00000000
#define NVA3_DISP_SOR_HDA_ELD                                        0x00010100
#define NV84_DISP_SOR_HDMI_PWR                                       0x00012000
#define NV84_DISP_SOR_HDMI_PWR_STATE                                 0x40000000
#define NV84_DISP_SOR_HDMI_PWR_STATE_OFF                             0x00000000
#define NV84_DISP_SOR_HDMI_PWR_STATE_ON                              0x40000000
#define NV84_DISP_SOR_HDMI_PWR_MAX_AC_PACKET                         0x001f0000
#define NV84_DISP_SOR_HDMI_PWR_REKEY                                 0x0000007f
#define NV50_DISP_SOR_LVDS_SCRIPT                                    0x00013000
#define NV50_DISP_SOR_LVDS_SCRIPT_ID                                 0x0000ffff

#define NV50_DISP_DAC_MTHD                                           0x00020000
#define NV50_DISP_DAC_MTHD_TYPE                                      0x0000f000
#define NV50_DISP_DAC_MTHD_OR                                        0x00000003

#define NV50_DISP_DAC_PWR                                            0x00020000
#define NV50_DISP_DAC_PWR_HSYNC                                      0x00000001
#define NV50_DISP_DAC_PWR_HSYNC_ON                                   0x00000000
#define NV50_DISP_DAC_PWR_HSYNC_LO                                   0x00000001
#define NV50_DISP_DAC_PWR_VSYNC                                      0x00000004
#define NV50_DISP_DAC_PWR_VSYNC_ON                                   0x00000000
#define NV50_DISP_DAC_PWR_VSYNC_LO                                   0x00000004
#define NV50_DISP_DAC_PWR_DATA                                       0x00000010
#define NV50_DISP_DAC_PWR_DATA_ON                                    0x00000000
#define NV50_DISP_DAC_PWR_DATA_LO                                    0x00000010
#define NV50_DISP_DAC_PWR_STATE                                      0x00000040
#define NV50_DISP_DAC_PWR_STATE_ON                                   0x00000000
#define NV50_DISP_DAC_PWR_STATE_OFF                                  0x00000040
#define NV50_DISP_DAC_LOAD                                           0x00020100
#define NV50_DISP_DAC_LOAD_VALUE                                     0x00000007

#define NV50_DISP_PIOR_MTHD                                          0x00030000
#define NV50_DISP_PIOR_MTHD_TYPE                                     0x0000f000
#define NV50_DISP_PIOR_MTHD_OR                                       0x00000003

#define NV50_DISP_PIOR_PWR                                           0x00030000
#define NV50_DISP_PIOR_PWR_STATE                                     0x00000001
#define NV50_DISP_PIOR_PWR_STATE_ON                                  0x00000001
#define NV50_DISP_PIOR_PWR_STATE_OFF                                 0x00000000
#define NV50_DISP_PIOR_TMDS_PWR                                      0x00032000
#define NV50_DISP_PIOR_TMDS_PWR_STATE                                0x00000001
#define NV50_DISP_PIOR_TMDS_PWR_STATE_ON                             0x00000001
#define NV50_DISP_PIOR_TMDS_PWR_STATE_OFF                            0x00000000
#define NV50_DISP_PIOR_DP_PWR                                        0x00036000
#define NV50_DISP_PIOR_DP_PWR_STATE                                  0x00000001
#define NV50_DISP_PIOR_DP_PWR_STATE_ON                               0x00000001
#define NV50_DISP_PIOR_DP_PWR_STATE_OFF                              0x00000000

struct nv50_display_class {
};

/* 507a: NV50_DISP_CURS
 * 827a: NV84_DISP_CURS
 * 837a: NVA0_DISP_CURS
 * 887a: NV94_DISP_CURS
 * 857a: NVA3_DISP_CURS
 * 907a: NVD0_DISP_CURS
 * 917a: NVE0_DISP_CURS
 * 927a: NVF0_DISP_CURS
 */

#define NV50_DISP_CURS_CLASS                                         0x0000507a
#define NV84_DISP_CURS_CLASS                                         0x0000827a
#define NVA0_DISP_CURS_CLASS                                         0x0000837a
#define NV94_DISP_CURS_CLASS                                         0x0000887a
#define NVA3_DISP_CURS_CLASS                                         0x0000857a
#define NVD0_DISP_CURS_CLASS                                         0x0000907a
#define NVE0_DISP_CURS_CLASS                                         0x0000917a
#define NVF0_DISP_CURS_CLASS                                         0x0000927a

struct nv50_display_curs_class {
	u32 head;
};

/* 507b: NV50_DISP_OIMM
 * 827b: NV84_DISP_OIMM
 * 837b: NVA0_DISP_OIMM
 * 887b: NV94_DISP_OIMM
 * 857b: NVA3_DISP_OIMM
 * 907b: NVD0_DISP_OIMM
 * 917b: NVE0_DISP_OIMM
 * 927b: NVE0_DISP_OIMM
 */

#define NV50_DISP_OIMM_CLASS                                         0x0000507b
#define NV84_DISP_OIMM_CLASS                                         0x0000827b
#define NVA0_DISP_OIMM_CLASS                                         0x0000837b
#define NV94_DISP_OIMM_CLASS                                         0x0000887b
#define NVA3_DISP_OIMM_CLASS                                         0x0000857b
#define NVD0_DISP_OIMM_CLASS                                         0x0000907b
#define NVE0_DISP_OIMM_CLASS                                         0x0000917b
#define NVF0_DISP_OIMM_CLASS                                         0x0000927b

struct nv50_display_oimm_class {
	u32 head;
};

/* 507c: NV50_DISP_SYNC
 * 827c: NV84_DISP_SYNC
 * 837c: NVA0_DISP_SYNC
 * 887c: NV94_DISP_SYNC
 * 857c: NVA3_DISP_SYNC
 * 907c: NVD0_DISP_SYNC
 * 917c: NVE0_DISP_SYNC
 * 927c: NVF0_DISP_SYNC
 */

#define NV50_DISP_SYNC_CLASS                                         0x0000507c
#define NV84_DISP_SYNC_CLASS                                         0x0000827c
#define NVA0_DISP_SYNC_CLASS                                         0x0000837c
#define NV94_DISP_SYNC_CLASS                                         0x0000887c
#define NVA3_DISP_SYNC_CLASS                                         0x0000857c
#define NVD0_DISP_SYNC_CLASS                                         0x0000907c
#define NVE0_DISP_SYNC_CLASS                                         0x0000917c
#define NVF0_DISP_SYNC_CLASS                                         0x0000927c

struct nv50_display_sync_class {
	u32 pushbuf;
	u32 head;
};

/* 507d: NV50_DISP_MAST
 * 827d: NV84_DISP_MAST
 * 837d: NVA0_DISP_MAST
 * 887d: NV94_DISP_MAST
 * 857d: NVA3_DISP_MAST
 * 907d: NVD0_DISP_MAST
 * 917d: NVE0_DISP_MAST
 * 927d: NVF0_DISP_MAST
 */

#define NV50_DISP_MAST_CLASS                                         0x0000507d
#define NV84_DISP_MAST_CLASS                                         0x0000827d
#define NVA0_DISP_MAST_CLASS                                         0x0000837d
#define NV94_DISP_MAST_CLASS                                         0x0000887d
#define NVA3_DISP_MAST_CLASS                                         0x0000857d
#define NVD0_DISP_MAST_CLASS                                         0x0000907d
#define NVE0_DISP_MAST_CLASS                                         0x0000917d
#define NVF0_DISP_MAST_CLASS                                         0x0000927d

struct nv50_display_mast_class {
	u32 pushbuf;
};

/* 507e: NV50_DISP_OVLY
 * 827e: NV84_DISP_OVLY
 * 837e: NVA0_DISP_OVLY
 * 887e: NV94_DISP_OVLY
 * 857e: NVA3_DISP_OVLY
 * 907e: NVD0_DISP_OVLY
 * 917e: NVE0_DISP_OVLY
 * 927e: NVF0_DISP_OVLY
 */

#define NV50_DISP_OVLY_CLASS                                         0x0000507e
#define NV84_DISP_OVLY_CLASS                                         0x0000827e
#define NVA0_DISP_OVLY_CLASS                                         0x0000837e
#define NV94_DISP_OVLY_CLASS                                         0x0000887e
#define NVA3_DISP_OVLY_CLASS                                         0x0000857e
#define NVD0_DISP_OVLY_CLASS                                         0x0000907e
#define NVE0_DISP_OVLY_CLASS                                         0x0000917e
#define NVF0_DISP_OVLY_CLASS                                         0x0000927e

struct nv50_display_ovly_class {
	u32 pushbuf;
	u32 head;
};

#endif
