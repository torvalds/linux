/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_CLASS_H__
#define __NVIF_CLASS_H__

/* these class numbers are made up by us, and not nvidia-assigned */
#define NVIF_CLASS_CLIENT                            /* if0000.h */ -0x00000000

#define NVIF_CLASS_CONTROL                           /* if0001.h */ -0x00000001

#define NVIF_CLASS_SW_NV04                           /* if0004.h */ -0x00000004
#define NVIF_CLASS_SW_NV10                           /* if0005.h */ -0x00000005
#define NVIF_CLASS_SW_NV50                           /* if0005.h */ -0x00000006
#define NVIF_CLASS_SW_GF100                          /* if0005.h */ -0x00000007

#define NVIF_CLASS_MMU                               /* if0008.h */  0x80000008
#define NVIF_CLASS_MMU_NV04                          /* if0008.h */  0x80000009
#define NVIF_CLASS_MMU_NV50                          /* if0008.h */  0x80005009
#define NVIF_CLASS_MMU_GF100                         /* if0008.h */  0x80009009

#define NVIF_CLASS_MEM                               /* if000a.h */  0x8000000a
#define NVIF_CLASS_MEM_NV04                          /* if000b.h */  0x8000000b
#define NVIF_CLASS_MEM_NV50                          /* if500b.h */  0x8000500b
#define NVIF_CLASS_MEM_GF100                         /* if900b.h */  0x8000900b

#define NVIF_CLASS_VMM                               /* if000c.h */  0x8000000c
#define NVIF_CLASS_VMM_NV04                          /* if000d.h */  0x8000000d
#define NVIF_CLASS_VMM_NV50                          /* if500d.h */  0x8000500d
#define NVIF_CLASS_VMM_GF100                         /* if900d.h */  0x8000900d
#define NVIF_CLASS_VMM_GM200                         /* ifb00d.h */  0x8000b00d
#define NVIF_CLASS_VMM_GP100                         /* ifc00d.h */  0x8000c00d

#define NVIF_CLASS_EVENT                             /* if000e.h */  0x8000000e

#define NVIF_CLASS_DISP                              /* if0010.h */  0x80000010
#define NVIF_CLASS_CONN                              /* if0011.h */  0x80000011
#define NVIF_CLASS_OUTP                              /* if0012.h */  0x80000012
#define NVIF_CLASS_HEAD                              /* if0013.h */  0x80000013
#define NVIF_CLASS_DISP_CHAN                         /* if0014.h */  0x80000014

#define NVIF_CLASS_CHAN                              /* if0020.h */  0x80000020
#define NVIF_CLASS_CGRP                              /* if0021.h */  0x80000021

/* the below match nvidia-assigned (either in hw, or sw) class numbers */
#define NV_NULL_CLASS                                                0x00000030

#define NV_DEVICE                                     /* cl0080.h */ 0x00000080

#define NV_DMA_FROM_MEMORY                            /* cl0002.h */ 0x00000002
#define NV_DMA_TO_MEMORY                              /* cl0002.h */ 0x00000003
#define NV_DMA_IN_MEMORY                              /* cl0002.h */ 0x0000003d

#define NV50_TWOD                                                    0x0000502d
#define FERMI_TWOD_A                                                 0x0000902d

#define NV50_MEMORY_TO_MEMORY_FORMAT                                 0x00005039
#define FERMI_MEMORY_TO_MEMORY_FORMAT_A                              0x00009039

#define KEPLER_INLINE_TO_MEMORY_A                                    0x0000a040
#define KEPLER_INLINE_TO_MEMORY_B                                    0x0000a140
#define BLACKWELL_INLINE_TO_MEMORY_A                                 0x0000cd40

#define NV04_DISP                                     /* cl0046.h */ 0x00000046

#define VOLTA_USERMODE_A                                             0x0000c361
#define TURING_USERMODE_A                                            0x0000c461
#define AMPERE_USERMODE_A                                            0x0000c561
#define HOPPER_USERMODE_A                                            0x0000c661
#define BLACKWELL_USERMODE_A                                         0x0000c761

#define MAXWELL_FAULT_BUFFER_A                        /* clb069.h */ 0x0000b069
#define VOLTA_FAULT_BUFFER_A                          /* clb069.h */ 0x0000c369

#define NV03_CHANNEL_DMA                              /* if0020.h */ 0x0000006b
#define NV10_CHANNEL_DMA                              /* if0020.h */ 0x0000006e
#define NV17_CHANNEL_DMA                              /* if0020.h */ 0x0000176e
#define NV40_CHANNEL_DMA                              /* if0020.h */ 0x0000406e

#define KEPLER_CHANNEL_GROUP_A                        /* if0021.h */ 0x0000a06c

#define NV50_CHANNEL_GPFIFO                           /* if0020.h */ 0x0000506f
#define G82_CHANNEL_GPFIFO                            /* if0020.h */ 0x0000826f
#define FERMI_CHANNEL_GPFIFO                          /* if0020.h */ 0x0000906f
#define KEPLER_CHANNEL_GPFIFO_A                       /* if0020.h */ 0x0000a06f
#define KEPLER_CHANNEL_GPFIFO_B                       /* if0020.h */ 0x0000a16f
#define MAXWELL_CHANNEL_GPFIFO_A                      /* if0020.h */ 0x0000b06f
#define PASCAL_CHANNEL_GPFIFO_A                       /* if0020.h */ 0x0000c06f
#define VOLTA_CHANNEL_GPFIFO_A                        /* if0020.h */ 0x0000c36f
#define TURING_CHANNEL_GPFIFO_A                       /* if0020.h */ 0x0000c46f
#define AMPERE_CHANNEL_GPFIFO_A                       /* if0020.h */ 0x0000c56f
#define AMPERE_CHANNEL_GPFIFO_B                       /* if0020.h */ 0x0000c76f
#define HOPPER_CHANNEL_GPFIFO_A                                      0x0000c86f
#define BLACKWELL_CHANNEL_GPFIFO_A                                   0x0000c96f
#define BLACKWELL_CHANNEL_GPFIFO_B                                   0x0000ca6f

#define NV50_DISP                                     /* if0010.h */ 0x00005070
#define G82_DISP                                      /* if0010.h */ 0x00008270
#define GT200_DISP                                    /* if0010.h */ 0x00008370
#define GT214_DISP                                    /* if0010.h */ 0x00008570
#define GT206_DISP                                    /* if0010.h */ 0x00008870
#define GF110_DISP                                    /* if0010.h */ 0x00009070
#define GK104_DISP                                    /* if0010.h */ 0x00009170
#define GK110_DISP                                    /* if0010.h */ 0x00009270
#define GM107_DISP                                    /* if0010.h */ 0x00009470
#define GM200_DISP                                    /* if0010.h */ 0x00009570
#define GP100_DISP                                    /* if0010.h */ 0x00009770
#define GP102_DISP                                    /* if0010.h */ 0x00009870
#define GV100_DISP                                    /* if0010.h */ 0x0000c370
#define TU102_DISP                                    /* if0010.h */ 0x0000c570
#define GA102_DISP                                    /* if0010.h */ 0x0000c670
#define AD102_DISP                                    /* if0010.h */ 0x0000c770
#define GB202_DISP                                                   0x0000ca70

#define GV100_DISP_CAPS                                              0x0000c373
#define GB202_DISP_CAPS                                              0x0000ca73

#define NV31_MPEG                                                    0x00003174
#define G82_MPEG                                                     0x00008274

#define NV74_VP2                                                     0x00007476

#define NV50_DISP_CURSOR                              /* if0014.h */ 0x0000507a
#define G82_DISP_CURSOR                               /* if0014.h */ 0x0000827a
#define GT214_DISP_CURSOR                             /* if0014.h */ 0x0000857a
#define GF110_DISP_CURSOR                             /* if0014.h */ 0x0000907a
#define GK104_DISP_CURSOR                             /* if0014.h */ 0x0000917a
#define GV100_DISP_CURSOR                             /* if0014.h */ 0x0000c37a
#define TU102_DISP_CURSOR                             /* if0014.h */ 0x0000c57a
#define GA102_DISP_CURSOR                             /* if0014.h */ 0x0000c67a
#define GB202_DISP_CURSOR                                            0x0000ca7a

#define NV50_DISP_OVERLAY                             /* if0014.h */ 0x0000507b
#define G82_DISP_OVERLAY                              /* if0014.h */ 0x0000827b
#define GT214_DISP_OVERLAY                            /* if0014.h */ 0x0000857b
#define GF110_DISP_OVERLAY                            /* if0014.h */ 0x0000907b
#define GK104_DISP_OVERLAY                            /* if0014.h */ 0x0000917b

#define GV100_DISP_WINDOW_IMM_CHANNEL_DMA             /* if0014.h */ 0x0000c37b
#define TU102_DISP_WINDOW_IMM_CHANNEL_DMA             /* if0014.h */ 0x0000c57b
#define GA102_DISP_WINDOW_IMM_CHANNEL_DMA             /* if0014.h */ 0x0000c67b
#define GB202_DISP_WINDOW_IMM_CHANNEL_DMA                            0x0000ca7b

#define NV50_DISP_BASE_CHANNEL_DMA                    /* if0014.h */ 0x0000507c
#define G82_DISP_BASE_CHANNEL_DMA                     /* if0014.h */ 0x0000827c
#define GT200_DISP_BASE_CHANNEL_DMA                   /* if0014.h */ 0x0000837c
#define GT214_DISP_BASE_CHANNEL_DMA                   /* if0014.h */ 0x0000857c
#define GF110_DISP_BASE_CHANNEL_DMA                   /* if0014.h */ 0x0000907c
#define GK104_DISP_BASE_CHANNEL_DMA                   /* if0014.h */ 0x0000917c
#define GK110_DISP_BASE_CHANNEL_DMA                   /* if0014.h */ 0x0000927c

#define NV50_DISP_CORE_CHANNEL_DMA                    /* if0014.h */ 0x0000507d
#define G82_DISP_CORE_CHANNEL_DMA                     /* if0014.h */ 0x0000827d
#define GT200_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000837d
#define GT214_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000857d
#define GT206_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000887d
#define GF110_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000907d
#define GK104_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000917d
#define GK110_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000927d
#define GM107_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000947d
#define GM200_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000957d
#define GP100_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000977d
#define GP102_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000987d
#define GV100_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000c37d
#define TU102_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000c57d
#define GA102_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000c67d
#define AD102_DISP_CORE_CHANNEL_DMA                   /* if0014.h */ 0x0000c77d
#define GB202_DISP_CORE_CHANNEL_DMA                                  0x0000ca7d

#define NV50_DISP_OVERLAY_CHANNEL_DMA                 /* if0014.h */ 0x0000507e
#define G82_DISP_OVERLAY_CHANNEL_DMA                  /* if0014.h */ 0x0000827e
#define GT200_DISP_OVERLAY_CHANNEL_DMA                /* if0014.h */ 0x0000837e
#define GT214_DISP_OVERLAY_CHANNEL_DMA                /* if0014.h */ 0x0000857e
#define GF110_DISP_OVERLAY_CONTROL_DMA                /* if0014.h */ 0x0000907e
#define GK104_DISP_OVERLAY_CONTROL_DMA                /* if0014.h */ 0x0000917e

#define GV100_DISP_WINDOW_CHANNEL_DMA                 /* if0014.h */ 0x0000c37e
#define TU102_DISP_WINDOW_CHANNEL_DMA                 /* if0014.h */ 0x0000c57e
#define GA102_DISP_WINDOW_CHANNEL_DMA                 /* if0014.h */ 0x0000c67e
#define GB202_DISP_WINDOW_CHANNEL_DMA                                0x0000ca7e

#define NV50_TESLA                                                   0x00005097
#define G82_TESLA                                                    0x00008297
#define GT200_TESLA                                                  0x00008397
#define GT214_TESLA                                                  0x00008597
#define GT21A_TESLA                                                  0x00008697

#define FERMI_A                                       /* cl9097.h */ 0x00009097
#define FERMI_B                                       /* cl9097.h */ 0x00009197
#define FERMI_C                                       /* cl9097.h */ 0x00009297

#define KEPLER_A                                      /* cl9097.h */ 0x0000a097
#define KEPLER_B                                      /* cl9097.h */ 0x0000a197
#define KEPLER_C                                      /* cl9097.h */ 0x0000a297

#define MAXWELL_A                                     /* cl9097.h */ 0x0000b097
#define MAXWELL_B                                     /* cl9097.h */ 0x0000b197

#define PASCAL_A                                      /* cl9097.h */ 0x0000c097
#define PASCAL_B                                      /* cl9097.h */ 0x0000c197

#define VOLTA_A                                       /* cl9097.h */ 0x0000c397

#define TURING_A                                      /* cl9097.h */ 0x0000c597

#define AMPERE_A                                                     0x0000c697
#define AMPERE_B                                      /* cl9097.h */ 0x0000c797

#define ADA_A                                         /* cl9097.h */ 0x0000c997

#define HOPPER_A                                                     0x0000cb97

#define BLACKWELL_A                                                  0x0000cd97
#define BLACKWELL_B                                                  0x0000ce97

#define NV74_BSP                                                     0x000074b0

#define NVB8B0_VIDEO_DECODER                                         0x0000b8b0
#define NVC4B0_VIDEO_DECODER                                         0x0000c4b0
#define NVC6B0_VIDEO_DECODER                                         0x0000c6b0
#define NVC7B0_VIDEO_DECODER                                         0x0000c7b0
#define NVC9B0_VIDEO_DECODER                                         0x0000c9b0
#define NVCDB0_VIDEO_DECODER                                         0x0000cdb0
#define NVCFB0_VIDEO_DECODER                                         0x0000cfb0

#define GT212_MSVLD                                                  0x000085b1
#define IGT21A_MSVLD                                                 0x000086b1
#define G98_MSVLD                                                    0x000088b1
#define GF100_MSVLD                                                  0x000090b1
#define GK104_MSVLD                                                  0x000095b1

#define GT212_MSPDEC                                                 0x000085b2
#define G98_MSPDEC                                                   0x000088b2
#define GF100_MSPDEC                                                 0x000090b2
#define GK104_MSPDEC                                                 0x000095b2

#define GT212_MSPPP                                                  0x000085b3
#define G98_MSPPP                                                    0x000088b3
#define GF100_MSPPP                                                  0x000090b3

#define G98_SEC                                                      0x000088b4

#define GT212_DMA                                                    0x000085b5
#define FERMI_DMA                                                    0x000090b5
#define KEPLER_DMA_COPY_A                                            0x0000a0b5
#define MAXWELL_DMA_COPY_A                                           0x0000b0b5
#define PASCAL_DMA_COPY_A                                            0x0000c0b5
#define PASCAL_DMA_COPY_B                                            0x0000c1b5
#define VOLTA_DMA_COPY_A                                             0x0000c3b5
#define TURING_DMA_COPY_A                                            0x0000c5b5
#define AMPERE_DMA_COPY_A                                            0x0000c6b5
#define AMPERE_DMA_COPY_B                                            0x0000c7b5
#define HOPPER_DMA_COPY_A                                            0x0000c8b5
#define BLACKWELL_DMA_COPY_A                                         0x0000c9b5
#define BLACKWELL_DMA_COPY_B                                         0x0000cab5

#define NVC4B7_VIDEO_ENCODER                                         0x0000c4b7
#define NVC7B7_VIDEO_ENCODER                                         0x0000c7b7
#define NVC9B7_VIDEO_ENCODER                                         0x0000c9b7
#define NVCFB7_VIDEO_ENCODER                                         0x0000cfb7

#define FERMI_DECOMPRESS                                             0x000090b8

#define NV50_COMPUTE                                                 0x000050c0
#define GT214_COMPUTE                                                0x000085c0
#define FERMI_COMPUTE_A                                              0x000090c0
#define FERMI_COMPUTE_B                                              0x000091c0
#define KEPLER_COMPUTE_A                                             0x0000a0c0
#define KEPLER_COMPUTE_B                                             0x0000a1c0
#define MAXWELL_COMPUTE_A                                            0x0000b0c0
#define MAXWELL_COMPUTE_B                                            0x0000b1c0
#define PASCAL_COMPUTE_A                                             0x0000c0c0
#define PASCAL_COMPUTE_B                                             0x0000c1c0
#define VOLTA_COMPUTE_A                                              0x0000c3c0
#define TURING_COMPUTE_A                                             0x0000c5c0
#define AMPERE_COMPUTE_A                                             0x0000c6c0
#define AMPERE_COMPUTE_B                                             0x0000c7c0
#define ADA_COMPUTE_A                                                0x0000c9c0
#define HOPPER_COMPUTE_A                                             0x0000cbc0
#define BLACKWELL_COMPUTE_A                                          0x0000cdc0
#define BLACKWELL_COMPUTE_B                                          0x0000cec0

#define NV74_CIPHER                                                  0x000074c1

#define NVB8D1_VIDEO_NVJPG                                           0x0000b8d1
#define NVC4D1_VIDEO_NVJPG                                           0x0000c4d1
#define NVC9D1_VIDEO_NVJPG                                           0x0000c9d1
#define NVCDD1_VIDEO_NVJPG                                           0x0000cdd1
#define NVCFD1_VIDEO_NVJPG                                           0x0000cfd1

#define NVB8FA_VIDEO_OFA                                             0x0000b8fa
#define NVC6FA_VIDEO_OFA                                             0x0000c6fa
#define NVC7FA_VIDEO_OFA                                             0x0000c7fa
#define NVC9FA_VIDEO_OFA                                             0x0000c9fa
#define NVCDFA_VIDEO_OFA                                             0x0000cdfa
#define NVCFFA_VIDEO_OFA                                             0x0000cffa
#endif
