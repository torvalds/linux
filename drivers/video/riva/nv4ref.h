 /***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-1998 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-1998 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/

/*
 * GPL licensing note -- nVidia is allowing a liberal interpretation of
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived
 * from this source.  -- Jeff Garzik <jgarzik@pobox.com>, 01/Nov/99 
 */

 /***************************************************************************\
|*            Modified 1999 by Fredrik Reite (fredrik@reite.com)             *|
 \***************************************************************************/


#ifndef __NV4REF_H__
#define __NV4REF_H__

/* Magic values to lock/unlock extended regs */
#define NV_CIO_SR_LOCK_INDEX				     0x0000001F /*       */
#define NV_CIO_SR_UNLOCK_RW_VALUE                            0x00000057 /*       */
#define NV_CIO_SR_UNLOCK_RO_VALUE                            0x00000075 /*       */
#define NV_CIO_SR_LOCK_VALUE                                 0x00000099 /*       */

#define UNLOCK_EXT_MAGIC 0x57
#define LOCK_EXT_MAGIC 0x99 /* Any value other than 0x57 will do */

#define LOCK_EXT_INDEX 0x6

#define NV_PCRTC_HORIZ_TOTAL                                 0x00
#define NV_PCRTC_HORIZ_DISPLAY_END                           0x01
#define NV_PCRTC_HORIZ_BLANK_START                           0x02

#define NV_PCRTC_HORIZ_BLANK_END                             0x03
#define NV_PCRTC_HORIZ_BLANK_END_EVRA                        7:7
#define NV_PCRTC_HORIZ_BLANK_END_DISPLAY_END_SKEW            6:5
#define NV_PCRTC_HORIZ_BLANK_END_HORIZ_BLANK_END             4:0

#define NV_PCRTC_HORIZ_RETRACE_START                         0x04

#define NV_PCRTC_HORIZ_RETRACE_END                           0x05
#define NV_PCRTC_HORIZ_RETRACE_END_HORIZ_BLANK_END_5         7:7
#define NV_PCRTC_HORIZ_RETRACE_END_HORIZ_RETRACE_SKEW        6:5
#define NV_PCRTC_HORIZ_RETRACE_END_HORIZ_RETRACE_END         4:0

#define NV_PCRTC_VERT_TOTAL                                  0x06

#define NV_PCRTC_OVERFLOW                                    0x07
#define NV_PCRTC_OVERFLOW_VERT_RETRACE_START_9               7:7
#define NV_PCRTC_OVERFLOW_VERT_DISPLAY_END_9                 6:6
#define NV_PCRTC_OVERFLOW_VERT_TOTAL_9                       5:5
#define NV_PCRTC_OVERFLOW_LINE_COMPARE_8                     4:4
#define NV_PCRTC_OVERFLOW_VERT_BLANK_START_8                 3:3
#define NV_PCRTC_OVERFLOW_VERT_RETRACE_START_8               2:2
#define NV_PCRTC_OVERFLOW_VERT_DISPLAY_END_8                 1:1
#define NV_PCRTC_OVERFLOW_VERT_TOTAL_8                       0:0

#define NV_PCRTC_PRESET_ROW_SCAN                             0x08

#define NV_PCRTC_MAX_SCAN_LINE                               0x09
#define NV_PCRTC_MAX_SCAN_LINE_DOUBLE_SCAN                   7:7
#define NV_PCRTC_MAX_SCAN_LINE_LINE_COMPARE_9                6:6
#define NV_PCRTC_MAX_SCAN_LINE_VERT_BLANK_START_9            5:5
#define NV_PCRTC_MAX_SCAN_LINE_MAX_SCAN_LINE                 4:0

#define NV_PCRTC_CURSOR_START                                0x0A
#define NV_PCRTC_CURSOR_END                                  0x0B
#define NV_PCRTC_START_ADDR_HIGH                             0x0C
#define NV_PCRTC_START_ADDR_LOW                              0x0D
#define NV_PCRTC_CURSOR_LOCATION_HIGH                        0x0E
#define NV_PCRTC_CURSOR_LOCATION_LOW                         0x0F

#define NV_PCRTC_VERT_RETRACE_START                          0x10
#define NV_PCRTC_VERT_RETRACE_END                            0x11
#define NV_PCRTC_VERT_DISPLAY_END                            0x12
#define NV_PCRTC_OFFSET                                      0x13
#define NV_PCRTC_UNDERLINE_LOCATION                          0x14
#define NV_PCRTC_VERT_BLANK_START                            0x15
#define NV_PCRTC_VERT_BLANK_END                              0x16
#define NV_PCRTC_MODE_CONTROL                                0x17
#define NV_PCRTC_LINE_COMPARE                                0x18

/* Extended offset and start address */
#define NV_PCRTC_REPAINT0                                    0x19
#define NV_PCRTC_REPAINT0_OFFSET_10_8                        7:5 
#define NV_PCRTC_REPAINT0_START_ADDR_20_16                   4:0

/* Horizonal extended bits */
#define NV_PCRTC_HORIZ_EXTRA                                 0x2d
#define NV_PCRTC_HORIZ_EXTRA_INTER_HALF_START_8              4:4
#define NV_PCRTC_HORIZ_EXTRA_HORIZ_RETRACE_START_8           3:3
#define NV_PCRTC_HORIZ_EXTRA_HORIZ_BLANK_START_8             2:2
#define NV_PCRTC_HORIZ_EXTRA_DISPLAY_END_8                   1:1
#define NV_PCRTC_HORIZ_EXTRA_DISPLAY_TOTAL_8                 0:0

/* Assorted extra bits */
#define NV_PCRTC_EXTRA                                       0x25
#define NV_PCRTC_EXTRA_OFFSET_11                             5:5
#define NV_PCRTC_EXTRA_HORIZ_BLANK_END_6                     4:4
#define NV_PCRTC_EXTRA_VERT_BLANK_START_10                   3:3
#define NV_PCRTC_EXTRA_VERT_RETRACE_START_10                 2:2
#define NV_PCRTC_EXTRA_VERT_DISPLAY_END_10                   1:1
#define NV_PCRTC_EXTRA_VERT_TOTAL_10                         0:0

/* Controls how much data the refresh fifo requests */
#define NV_PCRTC_FIFO_CONTROL                                0x1b
#define NV_PCRTC_FIFO_CONTROL_UNDERFLOW_WARN                 7:7
#define NV_PCRTC_FIFO_CONTROL_BURST_LENGTH                   2:0
#define NV_PCRTC_FIFO_CONTROL_BURST_LENGTH_8                 0x0
#define NV_PCRTC_FIFO_CONTROL_BURST_LENGTH_32                0x1
#define NV_PCRTC_FIFO_CONTROL_BURST_LENGTH_64                0x2
#define NV_PCRTC_FIFO_CONTROL_BURST_LENGTH_128               0x3
#define NV_PCRTC_FIFO_CONTROL_BURST_LENGTH_256               0x4

/* When the fifo occupancy falls below *twice* the watermark,
 * the refresh fifo will start to be refilled. If this value is 
 * too low, you will get junk on the screen. Too high, and performance
 * will suffer. Watermark in units of 8 bytes
 */
#define NV_PCRTC_FIFO                                        0x20
#define NV_PCRTC_FIFO_RESET                                  7:7
#define NV_PCRTC_FIFO_WATERMARK                              5:0

/* Various flags */
#define NV_PCRTC_REPAINT1                                    0x1a
#define NV_PCRTC_REPAINT1_HSYNC                              7:7
#define NV_PCRTC_REPAINT1_HYSNC_DISABLE                      0x01
#define NV_PCRTC_REPAINT1_HYSNC_ENABLE                       0x00
#define NV_PCRTC_REPAINT1_VSYNC                              6:6
#define NV_PCRTC_REPAINT1_VYSNC_DISABLE                      0x01
#define NV_PCRTC_REPAINT1_VYSNC_ENABLE                       0x00
#define NV_PCRTC_REPAINT1_COMPATIBLE_TEXT                    4:4
#define NV_PCRTC_REPAINT1_COMPATIBLE_TEXT_ENABLE             0x01
#define NV_PCRTC_REPAINT1_COMPATIBLE_TEXT_DISABLE            0x00
#define NV_PCRTC_REPAINT1_LARGE_SCREEN                       2:2 
#define NV_PCRTC_REPAINT1_LARGE_SCREEN_DISABLE               0x01
#define NV_PCRTC_REPAINT1_LARGE_SCREEN_ENABLE                0x00 /* >=1280 */
#define NV_PCRTC_REPAINT1_PALETTE_WIDTH                      1:1
#define NV_PCRTC_REPAINT1_PALETTE_WIDTH_8BITS                0x00
#define NV_PCRTC_REPAINT1_PALETTE_WIDTH_6BITS                0x01

#define NV_PCRTC_GRCURSOR0                                   0x30
#define NV_PCRTC_GRCURSOR0_START_ADDR_21_16                  5:0

#define NV_PCRTC_GRCURSOR1                                   0x31
#define NV_PCRTC_GRCURSOR1_START_ADDR_15_11                  7:3
#define NV_PCRTC_GRCURSOR1_SCAN_DBL                          1:1
#define NV_PCRTC_GRCURSOR1_SCAN_DBL_DISABLE                  0
#define NV_PCRTC_GRCURSOR1_SCAN_DBL_ENABLE                   1
#define NV_PCRTC_GRCURSOR1_CURSOR                            0:0
#define NV_PCRTC_GRCURSOR1_CURSOR_DISABLE                    0 
#define NV_PCRTC_GRCURSOR1_CURSOR_ENABLE                     1

/* Controls what the format of the framebuffer is */
#define NV_PCRTC_PIXEL                       0x28
#define NV_PCRTC_PIXEL_MODE                  7:7
#define NV_PCRTC_PIXEL_MODE_TV               0x01
#define NV_PCRTC_PIXEL_MODE_VGA              0x00
#define NV_PCRTC_PIXEL_TV_MODE               6:6
#define NV_PCRTC_PIXEL_TV_MODE_NTSC          0x00
#define NV_PCRTC_PIXEL_TV_MODE_PAL           0x01
#define NV_PCRTC_PIXEL_TV_HORIZ_ADJUST       5:3
#define NV_PCRTC_PIXEL_FORMAT                1:0
#define NV_PCRTC_PIXEL_FORMAT_VGA            0x00
#define NV_PCRTC_PIXEL_FORMAT_8BPP           0x01
#define NV_PCRTC_PIXEL_FORMAT_16BPP          0x02
#define NV_PCRTC_PIXEL_FORMAT_32BPP          0x03

/* RAMDAC registers and fields */
#define NV_PRAMDAC                            0x00680FFF:0x00680000 /* RW--D */
#define NV_PRAMDAC_GRCURSOR_START_POS                    0x00680300 /* RW-4R */
#define NV_PRAMDAC_GRCURSOR_START_POS_X                        11:0 /* RWXSF */
#define NV_PRAMDAC_GRCURSOR_START_POS_Y                       27:16 /* RWXSF */
#define NV_PRAMDAC_NVPLL_COEFF                           0x00680500 /* RW-4R */
#define NV_PRAMDAC_NVPLL_COEFF_MDIV                             7:0 /* RWIUF */
#define NV_PRAMDAC_NVPLL_COEFF_NDIV                            15:8 /* RWIUF */
#define NV_PRAMDAC_NVPLL_COEFF_PDIV                           18:16 /* RWIVF */
#define NV_PRAMDAC_MPLL_COEFF                            0x00680504 /* RW-4R */
#define NV_PRAMDAC_MPLL_COEFF_MDIV                              7:0 /* RWIUF */
#define NV_PRAMDAC_MPLL_COEFF_NDIV                             15:8 /* RWIUF */
#define NV_PRAMDAC_MPLL_COEFF_PDIV                            18:16 /* RWIVF */
#define NV_PRAMDAC_VPLL_COEFF                            0x00680508 /* RW-4R */
#define NV_PRAMDAC_VPLL_COEFF_MDIV                              7:0 /* RWIUF */
#define NV_PRAMDAC_VPLL_COEFF_NDIV                             15:8 /* RWIUF */
#define NV_PRAMDAC_VPLL_COEFF_PDIV                            18:16 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT                      0x0068050C /* RW-4R */
#define NV_PRAMDAC_PLL_COEFF_SELECT_DLL_BYPASS                  4:4 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_DLL_BYPASS_FALSE     0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_DLL_BYPASS_TRUE      0x00000001 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_MPLL_SOURCE                 8:8 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_MPLL_SOURCE_DEFAULT  0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_MPLL_SOURCE_PROG     0x00000001 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_MPLL_BYPASS               12:12 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_MPLL_BYPASS_FALSE    0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_MPLL_BYPASS_TRUE     0x00000001 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VPLL_SOURCE               16:16 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VPLL_SOURCE_DEFAULT  0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VPLL_SOURCE_PROG     0x00000001 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VPLL_BYPASS               20:20 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VPLL_BYPASS_FALSE    0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VPLL_BYPASS_TRUE     0x00000001 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_PCLK_SOURCE               25:24 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_PCLK_SOURCE_VPLL     0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_PCLK_SOURCE_VIP      0x00000001 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_PCLK_SOURCE_XTALOSC  0x00000002 /* RW--V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO                28:28 /* RWIVF */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO_DB1       0x00000000 /* RWI-V */
#define NV_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO_DB2       0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL                       0x00680600 /* RW-4R */
#define NV_PRAMDAC_GENERAL_CONTROL_FF_COEFF                     1:0 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_FF_COEFF_DEF          0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_IDC_MODE                     4:4 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_IDC_MODE_GAMMA        0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_IDC_MODE_INDEX        0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_VGA_STATE                    8:8 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_VGA_STATE_NOTSE       0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_VGA_STATE_SEL         0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_565_MODE                   12:12 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_565_MODE_NOTSEL       0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_565_MODE_SEL          0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_BLK_PEDSTL                 16:16 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_BLK_PEDSTL_OFF        0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_BLK_PEDSTL_ON         0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_TERMINATION                17:17 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_TERMINATION_37OHM     0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_TERMINATION_75OHM     0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_BPC                        20:20 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_BPC_6BITS             0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_BPC_8BITS             0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_DAC_SLEEP                  24:24 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_DAC_SLEEP_DIS         0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_DAC_SLEEP_EN          0x00000001 /* RW--V */
#define NV_PRAMDAC_GENERAL_CONTROL_PALETTE_CLK                28:28 /* RWIVF */
#define NV_PRAMDAC_GENERAL_CONTROL_PALETTE_CLK_EN        0x00000000 /* RWI-V */
#define NV_PRAMDAC_GENERAL_CONTROL_PALETTE_CLK_DIS       0x00000001 /* RW--V */

/* Master Control */
#define NV_PMC                                0x00000FFF:0x00000000 /* RW--D */
#define NV_PMC_BOOT_0                                    0x00000000 /* R--4R */
#define NV_PMC_BOOT_0_MINOR_REVISION                            3:0 /* C--VF */
#define NV_PMC_BOOT_0_MINOR_REVISION_0                   0x00000000 /* C---V */
#define NV_PMC_BOOT_0_MAJOR_REVISION                            7:4 /* C--VF */
#define NV_PMC_BOOT_0_MAJOR_REVISION_A                   0x00000000 /* C---V */
#define NV_PMC_BOOT_0_MAJOR_REVISION_B                   0x00000001 /* ----V */
#define NV_PMC_BOOT_0_IMPLEMENTATION                           11:8 /* C--VF */
#define NV_PMC_BOOT_0_IMPLEMENTATION_NV4_0               0x00000000 /* C---V */
#define NV_PMC_BOOT_0_ARCHITECTURE                            15:12 /* C--VF */
#define NV_PMC_BOOT_0_ARCHITECTURE_NV0                   0x00000000 /* ----V */
#define NV_PMC_BOOT_0_ARCHITECTURE_NV1                   0x00000001 /* ----V */
#define NV_PMC_BOOT_0_ARCHITECTURE_NV2                   0x00000002 /* ----V */
#define NV_PMC_BOOT_0_ARCHITECTURE_NV3                   0x00000003 /* ----V */
#define NV_PMC_BOOT_0_ARCHITECTURE_NV4                   0x00000004 /* C---V */
#define NV_PMC_BOOT_0_FIB_REVISION                            19:16 /* C--VF */
#define NV_PMC_BOOT_0_FIB_REVISION_0                     0x00000000 /* C---V */
#define NV_PMC_BOOT_0_MASK_REVISION                           23:20 /* C--VF */
#define NV_PMC_BOOT_0_MASK_REVISION_A                    0x00000000 /* C---V */
#define NV_PMC_BOOT_0_MASK_REVISION_B                    0x00000001 /* ----V */
#define NV_PMC_BOOT_0_MANUFACTURER                            27:24 /* C--UF */
#define NV_PMC_BOOT_0_MANUFACTURER_NVIDIA                0x00000000 /* C---V */
#define NV_PMC_BOOT_0_FOUNDRY                                 31:28 /* C--VF */
#define NV_PMC_BOOT_0_FOUNDRY_SGS                        0x00000000 /* ----V */
#define NV_PMC_BOOT_0_FOUNDRY_HELIOS                     0x00000001 /* ----V */
#define NV_PMC_BOOT_0_FOUNDRY_TSMC                       0x00000002 /* C---V */
#define NV_PMC_INTR_0                                    0x00000100 /* RW-4R */
#define NV_PMC_INTR_0_PMEDIA                                    4:4 /* R--VF */
#define NV_PMC_INTR_0_PMEDIA_NOT_PENDING                 0x00000000 /* R---V */
#define NV_PMC_INTR_0_PMEDIA_PENDING                     0x00000001 /* R---V */
#define NV_PMC_INTR_0_PFIFO                                     8:8 /* R--VF */
#define NV_PMC_INTR_0_PFIFO_NOT_PENDING                  0x00000000 /* R---V */
#define NV_PMC_INTR_0_PFIFO_PENDING                      0x00000001 /* R---V */
#define NV_PMC_INTR_0_PGRAPH                                  12:12 /* R--VF */
#define NV_PMC_INTR_0_PGRAPH_NOT_PENDING                 0x00000000 /* R---V */
#define NV_PMC_INTR_0_PGRAPH_PENDING                     0x00000001 /* R---V */
#define NV_PMC_INTR_0_PVIDEO                                  16:16 /* R--VF */
#define NV_PMC_INTR_0_PVIDEO_NOT_PENDING                 0x00000000 /* R---V */
#define NV_PMC_INTR_0_PVIDEO_PENDING                     0x00000001 /* R---V */
#define NV_PMC_INTR_0_PTIMER                                  20:20 /* R--VF */
#define NV_PMC_INTR_0_PTIMER_NOT_PENDING                 0x00000000 /* R---V */
#define NV_PMC_INTR_0_PTIMER_PENDING                     0x00000001 /* R---V */
#define NV_PMC_INTR_0_PCRTC                                   24:24 /* R--VF */
#define NV_PMC_INTR_0_PCRTC_NOT_PENDING                  0x00000000 /* R---V */
#define NV_PMC_INTR_0_PCRTC_PENDING                      0x00000001 /* R---V */
#define NV_PMC_INTR_0_PBUS                                    28:28 /* R--VF */
#define NV_PMC_INTR_0_PBUS_NOT_PENDING                   0x00000000 /* R---V */
#define NV_PMC_INTR_0_PBUS_PENDING                       0x00000001 /* R---V */
#define NV_PMC_INTR_0_SOFTWARE                                31:31 /* RWIVF */
#define NV_PMC_INTR_0_SOFTWARE_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PMC_INTR_0_SOFTWARE_PENDING                   0x00000001 /* RW--V */
#define NV_PMC_INTR_EN_0                                 0x00000140 /* RW-4R */
#define NV_PMC_INTR_EN_0_INTA                                   1:0 /* RWIVF */
#define NV_PMC_INTR_EN_0_INTA_DISABLED                   0x00000000 /* RWI-V */
#define NV_PMC_INTR_EN_0_INTA_HARDWARE                   0x00000001 /* RW--V */
#define NV_PMC_INTR_EN_0_INTA_SOFTWARE                   0x00000002 /* RW--V */
#define NV_PMC_INTR_READ_0                               0x00000160 /* R--4R */
#define NV_PMC_INTR_READ_0_INTA                                 0:0 /* R--VF */
#define NV_PMC_INTR_READ_0_INTA_LOW                      0x00000000 /* R---V */
#define NV_PMC_INTR_READ_0_INTA_HIGH                     0x00000001 /* R---V */
#define NV_PMC_ENABLE                                    0x00000200 /* RW-4R */
#define NV_PMC_ENABLE_PMEDIA                                    4:4 /* RWIVF */
#define NV_PMC_ENABLE_PMEDIA_DISABLED                    0x00000000 /* RWI-V */
#define NV_PMC_ENABLE_PMEDIA_ENABLED                     0x00000001 /* RW--V */
#define NV_PMC_ENABLE_PFIFO                                     8:8 /* RWIVF */
#define NV_PMC_ENABLE_PFIFO_DISABLED                     0x00000000 /* RWI-V */
#define NV_PMC_ENABLE_PFIFO_ENABLED                      0x00000001 /* RW--V */
#define NV_PMC_ENABLE_PGRAPH                                  12:12 /* RWIVF */
#define NV_PMC_ENABLE_PGRAPH_DISABLED                    0x00000000 /* RWI-V */
#define NV_PMC_ENABLE_PGRAPH_ENABLED                     0x00000001 /* RW--V */
#define NV_PMC_ENABLE_PPMI                                    16:16 /* RWIVF */
#define NV_PMC_ENABLE_PPMI_DISABLED                      0x00000000 /* RWI-V */
#define NV_PMC_ENABLE_PPMI_ENABLED                       0x00000001 /* RW--V */
#define NV_PMC_ENABLE_PFB                                     20:20 /* RWIVF */
#define NV_PMC_ENABLE_PFB_DISABLED                       0x00000000 /* RW--V */
#define NV_PMC_ENABLE_PFB_ENABLED                        0x00000001 /* RWI-V */
#define NV_PMC_ENABLE_PCRTC                                   24:24 /* RWIVF */
#define NV_PMC_ENABLE_PCRTC_DISABLED                     0x00000000 /* RW--V */
#define NV_PMC_ENABLE_PCRTC_ENABLED                      0x00000001 /* RWI-V */
#define NV_PMC_ENABLE_PVIDEO                                  28:28 /* RWIVF */
#define NV_PMC_ENABLE_PVIDEO_DISABLED                    0x00000000 /* RWI-V */
#define NV_PMC_ENABLE_PVIDEO_ENABLED                     0x00000001 /* RW--V */

/* dev_timer.ref */
#define NV_PTIMER                             0x00009FFF:0x00009000 /* RW--D */
#define NV_PTIMER_INTR_0                                 0x00009100 /* RW-4R */
#define NV_PTIMER_INTR_0_ALARM                                  0:0 /* RWXVF */
#define NV_PTIMER_INTR_0_ALARM_NOT_PENDING               0x00000000 /* R---V */
#define NV_PTIMER_INTR_0_ALARM_PENDING                   0x00000001 /* R---V */
#define NV_PTIMER_INTR_0_ALARM_RESET                     0x00000001 /* -W--V */
#define NV_PTIMER_INTR_EN_0                              0x00009140 /* RW-4R */
#define NV_PTIMER_INTR_EN_0_ALARM                               0:0 /* RWIVF */
#define NV_PTIMER_INTR_EN_0_ALARM_DISABLED               0x00000000 /* RWI-V */
#define NV_PTIMER_INTR_EN_0_ALARM_ENABLED                0x00000001 /* RW--V */
#define NV_PTIMER_NUMERATOR                              0x00009200 /* RW-4R */
#define NV_PTIMER_NUMERATOR_VALUE                              15:0 /* RWIUF */
#define NV_PTIMER_NUMERATOR_VALUE_0                      0x00000000 /* RWI-V */
#define NV_PTIMER_DENOMINATOR                            0x00009210 /* RW-4R */
#define NV_PTIMER_DENOMINATOR_VALUE                            15:0 /* RWIUF */
#define NV_PTIMER_DENOMINATOR_VALUE_0                    0x00000000 /* RWI-V */
#define NV_PTIMER_TIME_0                                 0x00009400 /* RW-4R */
#define NV_PTIMER_TIME_0_NSEC                                  31:5 /* RWXUF */
#define NV_PTIMER_TIME_1                                 0x00009410 /* RW-4R */
#define NV_PTIMER_TIME_1_NSEC                                  28:0 /* RWXUF */
#define NV_PTIMER_ALARM_0                                0x00009420 /* RW-4R */
#define NV_PTIMER_ALARM_0_NSEC                                 31:5 /* RWXUF */

/* dev_fifo.ref */
#define NV_PFIFO                              0x00003FFF:0x00002000 /* RW--D */
#define NV_PFIFO_DELAY_0                                 0x00002040 /* RW-4R */
#define NV_PFIFO_DELAY_0_WAIT_RETRY                             9:0 /* RWIUF */
#define NV_PFIFO_DELAY_0_WAIT_RETRY_0                    0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_TIMESLICE                           0x00002044 /* RW-4R */
#define NV_PFIFO_DMA_TIMESLICE_SELECT                          16:0 /* RWIUF */
#define NV_PFIFO_DMA_TIMESLICE_SELECT_1                  0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_TIMESLICE_SELECT_16K                0x00003fff /* RW--V */
#define NV_PFIFO_DMA_TIMESLICE_SELECT_32K                0x00007fff /* RW--V */
#define NV_PFIFO_DMA_TIMESLICE_SELECT_64K                0x0000ffff /* RW--V */
#define NV_PFIFO_DMA_TIMESLICE_SELECT_128K               0x0001ffff /* RW--V */
#define NV_PFIFO_DMA_TIMESLICE_TIMEOUT                        24:24 /* RWIUF */
#define NV_PFIFO_DMA_TIMESLICE_TIMEOUT_DISABLED          0x00000000 /* RW--V */
#define NV_PFIFO_DMA_TIMESLICE_TIMEOUT_ENABLED           0x00000001 /* RWI-V */
#define NV_PFIFO_PIO_TIMESLICE                           0x00002048 /* RW-4R */
#define NV_PFIFO_PIO_TIMESLICE_SELECT                          16:0 /* RWIUF */
#define NV_PFIFO_PIO_TIMESLICE_SELECT_1                  0x00000000 /* RWI-V */
#define NV_PFIFO_PIO_TIMESLICE_SELECT_16K                0x00003fff /* RW--V */
#define NV_PFIFO_PIO_TIMESLICE_SELECT_32K                0x00007fff /* RW--V */
#define NV_PFIFO_PIO_TIMESLICE_SELECT_64K                0x0000ffff /* RW--V */
#define NV_PFIFO_PIO_TIMESLICE_SELECT_128K               0x0001ffff /* RW--V */
#define NV_PFIFO_PIO_TIMESLICE_TIMEOUT                        24:24 /* RWIUF */
#define NV_PFIFO_PIO_TIMESLICE_TIMEOUT_DISABLED          0x00000000 /* RW--V */
#define NV_PFIFO_PIO_TIMESLICE_TIMEOUT_ENABLED           0x00000001 /* RWI-V */
#define NV_PFIFO_TIMESLICE                               0x0000204C /* RW-4R */
#define NV_PFIFO_TIMESLICE_TIMER                               17:0 /* RWIUF */
#define NV_PFIFO_TIMESLICE_TIMER_EXPIRED                 0x0003FFFF /* RWI-V */
#define NV_PFIFO_NEXT_CHANNEL                            0x00002050 /* RW-4R */
#define NV_PFIFO_NEXT_CHANNEL_CHID                              3:0 /* RWXUF */
#define NV_PFIFO_NEXT_CHANNEL_MODE                              8:8 /* RWXVF */
#define NV_PFIFO_NEXT_CHANNEL_MODE_PIO                   0x00000000 /* RW--V */
#define NV_PFIFO_NEXT_CHANNEL_MODE_DMA                   0x00000001 /* RW--V */
#define NV_PFIFO_NEXT_CHANNEL_SWITCH                          12:12 /* RWIVF */
#define NV_PFIFO_NEXT_CHANNEL_SWITCH_NOT_PENDING         0x00000000 /* RWI-V */
#define NV_PFIFO_NEXT_CHANNEL_SWITCH_PENDING             0x00000001 /* RW--V */
#define NV_PFIFO_DEBUG_0                                 0x00002080 /* R--4R */
#define NV_PFIFO_DEBUG_0_CACHE_ERROR0                           0:0 /* R-XVF */
#define NV_PFIFO_DEBUG_0_CACHE_ERROR0_NOT_PENDING        0x00000000 /* R---V */
#define NV_PFIFO_DEBUG_0_CACHE_ERROR0_PENDING            0x00000001 /* R---V */
#define NV_PFIFO_DEBUG_0_CACHE_ERROR1                           4:4 /* R-XVF */
#define NV_PFIFO_DEBUG_0_CACHE_ERROR1_NOT_PENDING        0x00000000 /* R---V */
#define NV_PFIFO_DEBUG_0_CACHE_ERROR1_PENDING            0x00000001 /* R---V */
#define NV_PFIFO_INTR_0                                  0x00002100 /* RW-4R */
#define NV_PFIFO_INTR_0_CACHE_ERROR                             0:0 /* RWXVF */
#define NV_PFIFO_INTR_0_CACHE_ERROR_NOT_PENDING          0x00000000 /* R---V */
#define NV_PFIFO_INTR_0_CACHE_ERROR_PENDING              0x00000001 /* R---V */
#define NV_PFIFO_INTR_0_CACHE_ERROR_RESET                0x00000001 /* -W--V */
#define NV_PFIFO_INTR_0_RUNOUT                                  4:4 /* RWXVF */
#define NV_PFIFO_INTR_0_RUNOUT_NOT_PENDING               0x00000000 /* R---V */
#define NV_PFIFO_INTR_0_RUNOUT_PENDING                   0x00000001 /* R---V */
#define NV_PFIFO_INTR_0_RUNOUT_RESET                     0x00000001 /* -W--V */
#define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW                         8:8 /* RWXVF */
#define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW_NOT_PENDING      0x00000000 /* R---V */
#define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW_PENDING          0x00000001 /* R---V */
#define NV_PFIFO_INTR_0_RUNOUT_OVERFLOW_RESET            0x00000001 /* -W--V */
#define NV_PFIFO_INTR_0_DMA_PUSHER                            12:12 /* RWXVF */
#define NV_PFIFO_INTR_0_DMA_PUSHER_NOT_PENDING           0x00000000 /* R---V */
#define NV_PFIFO_INTR_0_DMA_PUSHER_PENDING               0x00000001 /* R---V */
#define NV_PFIFO_INTR_0_DMA_PUSHER_RESET                 0x00000001 /* -W--V */
#define NV_PFIFO_INTR_0_DMA_PT                                16:16 /* RWXVF */
#define NV_PFIFO_INTR_0_DMA_PT_NOT_PENDING               0x00000000 /* R---V */
#define NV_PFIFO_INTR_0_DMA_PT_PENDING                   0x00000001 /* R---V */
#define NV_PFIFO_INTR_0_DMA_PT_RESET                     0x00000001 /* -W--V */
#define NV_PFIFO_INTR_EN_0                               0x00002140 /* RW-4R */
#define NV_PFIFO_INTR_EN_0_CACHE_ERROR                          0:0 /* RWIVF */
#define NV_PFIFO_INTR_EN_0_CACHE_ERROR_DISABLED          0x00000000 /* RWI-V */
#define NV_PFIFO_INTR_EN_0_CACHE_ERROR_ENABLED           0x00000001 /* RW--V */
#define NV_PFIFO_INTR_EN_0_RUNOUT                               4:4 /* RWIVF */
#define NV_PFIFO_INTR_EN_0_RUNOUT_DISABLED               0x00000000 /* RWI-V */
#define NV_PFIFO_INTR_EN_0_RUNOUT_ENABLED                0x00000001 /* RW--V */
#define NV_PFIFO_INTR_EN_0_RUNOUT_OVERFLOW                      8:8 /* RWIVF */
#define NV_PFIFO_INTR_EN_0_RUNOUT_OVERFLOW_DISABLED      0x00000000 /* RWI-V */
#define NV_PFIFO_INTR_EN_0_RUNOUT_OVERFLOW_ENABLED       0x00000001 /* RW--V */
#define NV_PFIFO_INTR_EN_0_DMA_PUSHER                         12:12 /* RWIVF */
#define NV_PFIFO_INTR_EN_0_DMA_PUSHER_DISABLED           0x00000000 /* RWI-V */
#define NV_PFIFO_INTR_EN_0_DMA_PUSHER_ENABLED            0x00000001 /* RW--V */
#define NV_PFIFO_INTR_EN_0_DMA_PT                             16:16 /* RWIVF */
#define NV_PFIFO_INTR_EN_0_DMA_PT_DISABLED               0x00000000 /* RWI-V */
#define NV_PFIFO_INTR_EN_0_DMA_PT_ENABLED                0x00000001 /* RW--V */
#define NV_PFIFO_RAMHT                                   0x00002210 /* RW-4R */
#define NV_PFIFO_RAMHT_BASE_ADDRESS                             8:4 /* RWIUF */
#define NV_PFIFO_RAMHT_BASE_ADDRESS_10000                0x00000010 /* RWI-V */
#define NV_PFIFO_RAMHT_SIZE                                   17:16 /* RWIUF */
#define NV_PFIFO_RAMHT_SIZE_4K                           0x00000000 /* RWI-V */
#define NV_PFIFO_RAMHT_SIZE_8K                           0x00000001 /* RW--V */
#define NV_PFIFO_RAMHT_SIZE_16K                          0x00000002 /* RW--V */
#define NV_PFIFO_RAMHT_SIZE_32K                          0x00000003 /* RW--V */
#define NV_PFIFO_RAMHT_SEARCH                                 25:24 /* RWIUF */
#define NV_PFIFO_RAMHT_SEARCH_16                         0x00000000 /* RWI-V */
#define NV_PFIFO_RAMHT_SEARCH_32                         0x00000001 /* RW--V */
#define NV_PFIFO_RAMHT_SEARCH_64                         0x00000002 /* RW--V */
#define NV_PFIFO_RAMHT_SEARCH_128                        0x00000003 /* RW--V */
#define NV_PFIFO_RAMFC                                   0x00002214 /* RW-4R */
#define NV_PFIFO_RAMFC_BASE_ADDRESS                             8:1 /* RWIUF */
#define NV_PFIFO_RAMFC_BASE_ADDRESS_11000                0x00000088 /* RWI-V */
#define NV_PFIFO_RAMRO                                   0x00002218 /* RW-4R */
#define NV_PFIFO_RAMRO_BASE_ADDRESS                             8:1 /* RWIUF */
#define NV_PFIFO_RAMRO_BASE_ADDRESS_11200                0x00000089 /* RWI-V */
#define NV_PFIFO_RAMRO_BASE_ADDRESS_12000                0x00000090 /* RW--V */
#define NV_PFIFO_RAMRO_SIZE                                   16:16 /* RWIVF */
#define NV_PFIFO_RAMRO_SIZE_512                          0x00000000 /* RWI-V */
#define NV_PFIFO_RAMRO_SIZE_8K                           0x00000001 /* RW--V */
#define NV_PFIFO_CACHES                                  0x00002500 /* RW-4R */
#define NV_PFIFO_CACHES_REASSIGN                                0:0 /* RWIVF */
#define NV_PFIFO_CACHES_REASSIGN_DISABLED                0x00000000 /* RWI-V */
#define NV_PFIFO_CACHES_REASSIGN_ENABLED                 0x00000001 /* RW--V */
#define NV_PFIFO_CACHES_DMA_SUSPEND                             4:4 /* R--VF */
#define NV_PFIFO_CACHES_DMA_SUSPEND_IDLE                 0x00000000 /* R---V */
#define NV_PFIFO_CACHES_DMA_SUSPEND_BUSY                 0x00000001 /* R---V */
#define NV_PFIFO_MODE                                    0x00002504 /* RW-4R */
#define NV_PFIFO_MODE_CHANNEL_0                                 0:0 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_0_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_0_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_1                                 1:1 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_1_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_1_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_2                                 2:2 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_2_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_2_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_3                                 3:3 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_3_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_3_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_4                                 4:4 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_4_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_4_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_5                                 5:5 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_5_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_5_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_6                                 6:6 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_6_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_6_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_7                                 7:7 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_7_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_7_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_8                                 8:8 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_8_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_8_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_9                                 9:9 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_9_PIO                      0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_9_DMA                      0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_10                              10:10 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_10_PIO                     0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_10_DMA                     0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_11                              11:11 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_11_PIO                     0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_11_DMA                     0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_12                              12:12 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_12_PIO                     0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_12_DMA                     0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_13                              13:13 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_13_PIO                     0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_13_DMA                     0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_14                              14:14 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_14_PIO                     0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_14_DMA                     0x00000001 /* RW--V */
#define NV_PFIFO_MODE_CHANNEL_15                              15:15 /* RWIVF */
#define NV_PFIFO_MODE_CHANNEL_15_PIO                     0x00000000 /* RWI-V */
#define NV_PFIFO_MODE_CHANNEL_15_DMA                     0x00000001 /* RW--V */
#define NV_PFIFO_DMA                                     0x00002508 /* RW-4R */
#define NV_PFIFO_DMA_CHANNEL_0                                  0:0 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_0_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_0_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_1                                  1:1 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_1_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_1_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_2                                  2:2 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_2_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_2_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_3                                  3:3 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_3_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_3_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_4                                  4:4 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_4_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_4_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_5                                  5:5 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_5_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_5_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_6                                  6:6 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_6_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_6_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_7                                  7:7 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_7_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_7_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_8                                  8:8 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_8_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_8_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_9                                  9:9 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_9_NOT_PENDING               0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_9_PENDING                   0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_10                               10:10 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_10_NOT_PENDING              0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_10_PENDING                  0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_11                               11:11 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_11_NOT_PENDING              0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_11_PENDING                  0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_12                               12:12 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_12_NOT_PENDING              0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_12_PENDING                  0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_13                               13:13 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_13_NOT_PENDING              0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_13_PENDING                  0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_14                               14:14 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_14_NOT_PENDING              0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_14_PENDING                  0x00000001 /* RW--V */
#define NV_PFIFO_DMA_CHANNEL_15                               15:15 /* RWIVF */
#define NV_PFIFO_DMA_CHANNEL_15_NOT_PENDING              0x00000000 /* RWI-V */
#define NV_PFIFO_DMA_CHANNEL_15_PENDING                  0x00000001 /* RW--V */
#define NV_PFIFO_SIZE                                    0x0000250C /* RW-4R */
#define NV_PFIFO_SIZE_CHANNEL_0                                 0:0 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_0_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_0_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_1                                 1:1 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_1_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_1_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_2                                 2:2 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_2_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_2_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_3                                 3:3 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_3_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_3_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_4                                 4:4 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_4_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_4_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_5                                 5:5 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_5_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_5_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_6                                 6:6 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_6_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_6_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_7                                 7:7 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_7_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_7_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_8                                 8:8 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_8_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_8_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_9                                 9:9 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_9_124_BYTES                0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_9_512_BYTES                0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_10                              10:10 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_10_124_BYTES               0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_10_512_BYTES               0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_11                              11:11 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_11_124_BYTES               0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_11_512_BYTES               0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_12                              12:12 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_12_124_BYTES               0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_12_512_BYTES               0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_13                              13:13 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_13_124_BYTES               0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_13_512_BYTES               0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_14                              14:14 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_14_124_BYTES               0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_14_512_BYTES               0x00000001 /* RW--V */
#define NV_PFIFO_SIZE_CHANNEL_15                              15:15 /* RWIVF */
#define NV_PFIFO_SIZE_CHANNEL_15_124_BYTES               0x00000000 /* RWI-V */
#define NV_PFIFO_SIZE_CHANNEL_15_512_BYTES               0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_PUSH0                            0x00003000 /* RW-4R */
#define NV_PFIFO_CACHE0_PUSH0_ACCESS                            0:0 /* RWIVF */
#define NV_PFIFO_CACHE0_PUSH0_ACCESS_DISABLED            0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE0_PUSH0_ACCESS_ENABLED             0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_PUSH0                            0x00003200 /* RW-4R */
#define NV_PFIFO_CACHE1_PUSH0_ACCESS                            0:0 /* RWIVF */
#define NV_PFIFO_CACHE1_PUSH0_ACCESS_DISABLED            0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_PUSH0_ACCESS_ENABLED             0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_PUSH1                            0x00003004 /* RW-4R */
#define NV_PFIFO_CACHE0_PUSH1_CHID                              3:0 /* RWXUF */
#define NV_PFIFO_CACHE1_PUSH1                            0x00003204 /* RW-4R */
#define NV_PFIFO_CACHE1_PUSH1_CHID                              3:0 /* RWXUF */
#define NV_PFIFO_CACHE1_PUSH1_MODE                              8:8 /* RWIVF */
#define NV_PFIFO_CACHE1_PUSH1_MODE_PIO                   0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_PUSH1_MODE_DMA                   0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_PUSH                         0x00003220 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_PUSH_ACCESS                         0:0 /* RWIVF */
#define NV_PFIFO_CACHE1_DMA_PUSH_ACCESS_DISABLED         0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_PUSH_ACCESS_ENABLED          0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_PUSH_STATE                          4:4 /* R--VF */
#define NV_PFIFO_CACHE1_DMA_PUSH_STATE_IDLE              0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_DMA_PUSH_STATE_BUSY              0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_DMA_PUSH_BUFFER                         8:8 /* R--VF */
#define NV_PFIFO_CACHE1_DMA_PUSH_BUFFER_NOT_EMPTY        0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_DMA_PUSH_BUFFER_EMPTY            0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_DMA_PUSH_STATUS                       12:12 /* RWIVF */
#define NV_PFIFO_CACHE1_DMA_PUSH_STATUS_RUNNING          0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_PUSH_STATUS_SUSPENDED        0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH                        0x00003224 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG                          7:3 /* RWIUF */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_8_BYTES           0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_16_BYTES          0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_24_BYTES          0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_32_BYTES          0x00000003 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_40_BYTES          0x00000004 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_48_BYTES          0x00000005 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_56_BYTES          0x00000006 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_64_BYTES          0x00000007 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_72_BYTES          0x00000008 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_80_BYTES          0x00000009 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_88_BYTES          0x0000000A /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_96_BYTES          0x0000000B /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_104_BYTES         0x0000000C /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_112_BYTES         0x0000000D /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_120_BYTES         0x0000000E /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES         0x0000000F /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_136_BYTES         0x00000010 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_144_BYTES         0x00000011 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_152_BYTES         0x00000012 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_160_BYTES         0x00000013 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_168_BYTES         0x00000014 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_176_BYTES         0x00000015 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_184_BYTES         0x00000016 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_192_BYTES         0x00000017 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_200_BYTES         0x00000018 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_208_BYTES         0x00000019 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_216_BYTES         0x0000001A /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_224_BYTES         0x0000001B /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_232_BYTES         0x0000001C /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_240_BYTES         0x0000001D /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_248_BYTES         0x0000001E /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_256_BYTES         0x0000001F /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE                        15:13 /* RWIUF */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_32_BYTES          0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_64_BYTES          0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_96_BYTES          0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES         0x00000003 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_160_BYTES         0x00000004 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_192_BYTES         0x00000005 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_224_BYTES         0x00000006 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_256_BYTES         0x00000007 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS                    19:16 /* RWIUF */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_0             0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_1             0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_2             0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_3             0x00000003 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_4             0x00000004 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_5             0x00000005 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_6             0x00000006 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_7             0x00000007 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8             0x00000008 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_9             0x00000009 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_10            0x0000000A /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_11            0x0000000B /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_12            0x0000000C /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_13            0x0000000D /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_14            0x0000000E /* RW--V */
#define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_15            0x0000000F /* RW--V */
#define NV_PFIFO_CACHE1_DMA_PUT                          0x00003240 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_PUT_OFFSET                         28:2 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_GET                          0x00003244 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_GET_OFFSET                         28:2 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_STATE                        0x00003228 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_STATE_METHOD                       12:2 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_STATE_SUBCHANNEL                  15:13 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT                28:18 /* RWIUF */
#define NV_PFIFO_CACHE1_DMA_STATE_METHOD_COUNT_0         0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_STATE_ERROR                       31:30 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NONE             0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_STATE_ERROR_NON_CACHE        0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_STATE_ERROR_RESERVED_CMD     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_STATE_ERROR_PROTECTION       0x00000003 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_INSTANCE                     0x0000322C /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_INSTANCE_ADDRESS                   15:0 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_CTL                          0x00003230 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_CTL_ADJUST                         11:2 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_CTL_PAGE_TABLE                    12:12 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_CTL_PAGE_TABLE_NOT_PRESENT   0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_PAGE_TABLE_PRESENT       0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_PAGE_ENTRY                    13:13 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_CTL_PAGE_ENTRY_NOT_LINEAR    0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_PAGE_ENTRY_LINEAR        0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_TARGET_NODE                   17:16 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_CTL_TARGET_NODE_PCI          0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_TARGET_NODE_AGP          0x00000003 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_AT_INFO                       31:31 /* RWIUF */
#define NV_PFIFO_CACHE1_DMA_CTL_AT_INFO_INVALID          0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_CTL_AT_INFO_VALID            0x00000001 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_LIMIT                        0x00003234 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_LIMIT_OFFSET                       28:2 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_TLB_TAG                      0x00003238 /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_TLB_TAG_ADDRESS                   28:12 /* RWXUF */
#define NV_PFIFO_CACHE1_DMA_TLB_TAG_STATE                       0:0 /* RWIUF */
#define NV_PFIFO_CACHE1_DMA_TLB_TAG_STATE_INVALID        0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_DMA_TLB_TAG_STATE_VALID          0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_DMA_TLB_PTE                      0x0000323C /* RW-4R */
#define NV_PFIFO_CACHE1_DMA_TLB_PTE_FRAME_ADDRESS             31:12 /* RWXUF */
#define NV_PFIFO_CACHE0_PULL0                            0x00003050 /* RW-4R */
#define NV_PFIFO_CACHE0_PULL0_ACCESS                            0:0 /* RWIVF */
#define NV_PFIFO_CACHE0_PULL0_ACCESS_DISABLED            0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE0_PULL0_ACCESS_ENABLED             0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_PULL0_HASH                              4:4 /* R-XVF */
#define NV_PFIFO_CACHE0_PULL0_HASH_SUCCEEDED             0x00000000 /* R---V */
#define NV_PFIFO_CACHE0_PULL0_HASH_FAILED                0x00000001 /* R---V */
#define NV_PFIFO_CACHE0_PULL0_DEVICE                            8:8 /* R-XVF */
#define NV_PFIFO_CACHE0_PULL0_DEVICE_HARDWARE            0x00000000 /* R---V */
#define NV_PFIFO_CACHE0_PULL0_DEVICE_SOFTWARE            0x00000001 /* R---V */
#define NV_PFIFO_CACHE0_PULL0_HASH_STATE                      12:12 /* R-XVF */
#define NV_PFIFO_CACHE0_PULL0_HASH_STATE_IDLE            0x00000000 /* R---V */
#define NV_PFIFO_CACHE0_PULL0_HASH_STATE_BUSY            0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_PULL0                            0x00003250 /* RW-4R */
#define NV_PFIFO_CACHE1_PULL0_ACCESS                            0:0 /* RWIVF */
#define NV_PFIFO_CACHE1_PULL0_ACCESS_DISABLED            0x00000000 /* RWI-V */
#define NV_PFIFO_CACHE1_PULL0_ACCESS_ENABLED             0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_PULL0_HASH                              4:4 /* R-XVF */
#define NV_PFIFO_CACHE1_PULL0_HASH_SUCCEEDED             0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_PULL0_HASH_FAILED                0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_PULL0_DEVICE                            8:8 /* R-XVF */
#define NV_PFIFO_CACHE1_PULL0_DEVICE_HARDWARE            0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_PULL0_DEVICE_SOFTWARE            0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_PULL0_HASH_STATE                      12:12 /* R-XVF */
#define NV_PFIFO_CACHE1_PULL0_HASH_STATE_IDLE            0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_PULL0_HASH_STATE_BUSY            0x00000001 /* R---V */
#define NV_PFIFO_CACHE0_PULL1                            0x00003054 /* RW-4R */
#define NV_PFIFO_CACHE0_PULL1_ENGINE                            1:0 /* RWXUF */
#define NV_PFIFO_CACHE0_PULL1_ENGINE_SW                  0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_PULL1_ENGINE_GRAPHICS            0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_PULL1_ENGINE_DVD                 0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_PULL1                            0x00003254 /* RW-4R */
#define NV_PFIFO_CACHE1_PULL1_ENGINE                            1:0 /* RWXUF */
#define NV_PFIFO_CACHE1_PULL1_ENGINE_SW                  0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_PULL1_ENGINE_GRAPHICS            0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_PULL1_ENGINE_DVD                 0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_HASH                             0x00003058 /* RW-4R */
#define NV_PFIFO_CACHE0_HASH_INSTANCE                          15:0 /* RWXUF */
#define NV_PFIFO_CACHE0_HASH_VALID                            16:16 /* RWXVF */
#define NV_PFIFO_CACHE1_HASH                             0x00003258 /* RW-4R */
#define NV_PFIFO_CACHE1_HASH_INSTANCE                          15:0 /* RWXUF */
#define NV_PFIFO_CACHE1_HASH_VALID                            16:16 /* RWXVF */
#define NV_PFIFO_CACHE0_STATUS                           0x00003014 /* R--4R */
#define NV_PFIFO_CACHE0_STATUS_LOW_MARK                         4:4 /* R--VF */
#define NV_PFIFO_CACHE0_STATUS_LOW_MARK_NOT_EMPTY        0x00000000 /* R---V */
#define NV_PFIFO_CACHE0_STATUS_LOW_MARK_EMPTY            0x00000001 /* R---V */
#define NV_PFIFO_CACHE0_STATUS_HIGH_MARK                        8:8 /* R--VF */
#define NV_PFIFO_CACHE0_STATUS_HIGH_MARK_NOT_FULL        0x00000000 /* R---V */
#define NV_PFIFO_CACHE0_STATUS_HIGH_MARK_FULL            0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_STATUS                           0x00003214 /* R--4R */
#define NV_PFIFO_CACHE1_STATUS_LOW_MARK                         4:4 /* R--VF */
#define NV_PFIFO_CACHE1_STATUS_LOW_MARK_NOT_EMPTY        0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_STATUS_LOW_MARK_EMPTY            0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_STATUS_HIGH_MARK                        8:8 /* R--VF */
#define NV_PFIFO_CACHE1_STATUS_HIGH_MARK_NOT_FULL        0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_STATUS_HIGH_MARK_FULL            0x00000001 /* R---V */
#define NV_PFIFO_CACHE1_STATUS1                          0x00003218 /* R--4R */
#define NV_PFIFO_CACHE1_STATUS1_RANOUT                          0:0 /* R-XVF */
#define NV_PFIFO_CACHE1_STATUS1_RANOUT_FALSE             0x00000000 /* R---V */
#define NV_PFIFO_CACHE1_STATUS1_RANOUT_TRUE              0x00000001 /* R---V */
#define NV_PFIFO_CACHE0_PUT                              0x00003010 /* RW-4R */
#define NV_PFIFO_CACHE0_PUT_ADDRESS                             2:2 /* RWXUF */
#define NV_PFIFO_CACHE1_PUT                              0x00003210 /* RW-4R */
#define NV_PFIFO_CACHE1_PUT_ADDRESS                             9:2 /* RWXUF */
#define NV_PFIFO_CACHE0_GET                              0x00003070 /* RW-4R */
#define NV_PFIFO_CACHE0_GET_ADDRESS                             2:2 /* RWXUF */
#define NV_PFIFO_CACHE1_GET                              0x00003270 /* RW-4R */
#define NV_PFIFO_CACHE1_GET_ADDRESS                             9:2 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE                           0x00003080 /* RW-4R */
#define NV_PFIFO_CACHE0_ENGINE_0                                1:0 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_0_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_0_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_0_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_1                                5:4 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_1_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_1_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_1_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_2                                9:8 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_2_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_2_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_2_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_3                              13:12 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_3_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_3_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_3_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_4                              17:16 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_4_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_4_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_4_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_5                              21:20 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_5_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_5_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_5_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_6                              25:24 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_6_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_6_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_6_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_7                              29:28 /* RWXUF */
#define NV_PFIFO_CACHE0_ENGINE_7_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_7_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE0_ENGINE_7_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE                           0x00003280 /* RW-4R */
#define NV_PFIFO_CACHE1_ENGINE_0                                1:0 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_0_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_0_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_0_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_1                                5:4 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_1_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_1_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_1_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_2                                9:8 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_2_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_2_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_2_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_3                              13:12 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_3_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_3_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_3_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_4                              17:16 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_4_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_4_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_4_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_5                              21:20 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_5_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_5_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_5_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_6                              25:24 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_6_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_6_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_6_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_7                              29:28 /* RWXUF */
#define NV_PFIFO_CACHE1_ENGINE_7_SW                      0x00000000 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_7_GRAPHICS                0x00000001 /* RW--V */
#define NV_PFIFO_CACHE1_ENGINE_7_DVD                     0x00000002 /* RW--V */
#define NV_PFIFO_CACHE0_METHOD(i)                (0x00003100+(i)*8) /* RW-4A */
#define NV_PFIFO_CACHE0_METHOD__SIZE_1                            1 /*       */
#define NV_PFIFO_CACHE0_METHOD_ADDRESS                         12:2 /* RWXUF */
#define NV_PFIFO_CACHE0_METHOD_SUBCHANNEL                     15:13 /* RWXUF */
#define NV_PFIFO_CACHE1_METHOD(i)                (0x00003800+(i)*8) /* RW-4A */
#define NV_PFIFO_CACHE1_METHOD__SIZE_1                          128 /*       */
#define NV_PFIFO_CACHE1_METHOD_ADDRESS                         12:2 /* RWXUF */
#define NV_PFIFO_CACHE1_METHOD_SUBCHANNEL                     15:13 /* RWXUF */
#define NV_PFIFO_CACHE1_METHOD_ALIAS(i)          (0x00003C00+(i)*8) /* RW-4A */
#define NV_PFIFO_CACHE1_METHOD_ALIAS__SIZE_1                    128 /*       */
#define NV_PFIFO_CACHE0_DATA(i)                  (0x00003104+(i)*8) /* RW-4A */
#define NV_PFIFO_CACHE0_DATA__SIZE_1                              1 /*       */
#define NV_PFIFO_CACHE0_DATA_VALUE                             31:0 /* RWXVF */
#define NV_PFIFO_CACHE1_DATA(i)                  (0x00003804+(i)*8) /* RW-4A */
#define NV_PFIFO_CACHE1_DATA__SIZE_1                            128 /*       */
#define NV_PFIFO_CACHE1_DATA_VALUE                             31:0 /* RWXVF */
#define NV_PFIFO_CACHE1_DATA_ALIAS(i)            (0x00003C04+(i)*8) /* RW-4A */
#define NV_PFIFO_CACHE1_DATA_ALIAS__SIZE_1                      128 /*       */
#define NV_PFIFO_DEVICE(i)                       (0x00002800+(i)*4) /* R--4A */
#define NV_PFIFO_DEVICE__SIZE_1                                 128 /*       */
#define NV_PFIFO_DEVICE_CHID                                    3:0 /* R--UF */
#define NV_PFIFO_DEVICE_SWITCH                                24:24 /* R--VF */
#define NV_PFIFO_DEVICE_SWITCH_UNAVAILABLE               0x00000000 /* R---V */
#define NV_PFIFO_DEVICE_SWITCH_AVAILABLE                 0x00000001 /* R---V */
#define NV_PFIFO_RUNOUT_STATUS                           0x00002400 /* R--4R */
#define NV_PFIFO_RUNOUT_STATUS_RANOUT                           0:0 /* R--VF */
#define NV_PFIFO_RUNOUT_STATUS_RANOUT_FALSE              0x00000000 /* R---V */
#define NV_PFIFO_RUNOUT_STATUS_RANOUT_TRUE               0x00000001 /* R---V */
#define NV_PFIFO_RUNOUT_STATUS_LOW_MARK                         4:4 /* R--VF */
#define NV_PFIFO_RUNOUT_STATUS_LOW_MARK_NOT_EMPTY        0x00000000 /* R---V */
#define NV_PFIFO_RUNOUT_STATUS_LOW_MARK_EMPTY            0x00000001 /* R---V */
#define NV_PFIFO_RUNOUT_STATUS_HIGH_MARK                        8:8 /* R--VF */
#define NV_PFIFO_RUNOUT_STATUS_HIGH_MARK_NOT_FULL        0x00000000 /* R---V */
#define NV_PFIFO_RUNOUT_STATUS_HIGH_MARK_FULL            0x00000001 /* R---V */
#define NV_PFIFO_RUNOUT_PUT                              0x00002410 /* RW-4R */
#define NV_PFIFO_RUNOUT_PUT_ADDRESS                            12:3 /* RWXUF */
#define NV_PFIFO_RUNOUT_PUT_ADDRESS__SIZE_0                     8:3 /* RWXUF */
#define NV_PFIFO_RUNOUT_PUT_ADDRESS__SIZE_1                    12:3 /* RWXUF */
#define NV_PFIFO_RUNOUT_GET                              0x00002420 /* RW-4R */
#define NV_PFIFO_RUNOUT_GET_ADDRESS                            13:3 /* RWXUF */
/* dev_graphics.ref */
#define NV_PGRAPH                             0x00401FFF:0x00400000 /* RW--D */
#define NV_PGRAPH_DEBUG_0                                0x00400080 /* RW-4R */
#define NV_PGRAPH_DEBUG_1                                0x00400084 /* RW-4R */
#define NV_PGRAPH_DEBUG_2                                0x00400088 /* RW-4R */
#define NV_PGRAPH_DEBUG_3                                0x0040008C /* RW-4R */
#define NV_PGRAPH_INTR                                   0x00400100 /* RW-4R */
#define NV_PGRAPH_INTR_NOTIFY                                   0:0 /* RWIVF */
#define NV_PGRAPH_INTR_NOTIFY_NOT_PENDING                0x00000000 /* R-I-V */
#define NV_PGRAPH_INTR_NOTIFY_PENDING                    0x00000001 /* R---V */
#define NV_PGRAPH_INTR_NOTIFY_RESET                      0x00000001 /* -W--C */
#define NV_PGRAPH_INTR_MISSING_HW                               4:4 /* RWIVF */
#define NV_PGRAPH_INTR_MISSING_HW_NOT_PENDING            0x00000000 /* R-I-V */
#define NV_PGRAPH_INTR_MISSING_HW_PENDING                0x00000001 /* R---V */
#define NV_PGRAPH_INTR_MISSING_HW_RESET                  0x00000001 /* -W--C */
#define NV_PGRAPH_INTR_TLB_PRESENT_A                            8:8 /* RWIVF */
#define NV_PGRAPH_INTR_TLB_PRESENT_A_NOT_PENDING         0x00000000 /* R-I-V */
#define NV_PGRAPH_INTR_TLB_PRESENT_A_PENDING             0x00000001 /* R---V */
#define NV_PGRAPH_INTR_TLB_PRESENT_A_RESET               0x00000001 /* -W--C */
#define NV_PGRAPH_INTR_TLB_PRESENT_B                            9:9 /* RWIVF */
#define NV_PGRAPH_INTR_TLB_PRESENT_B_NOT_PENDING         0x00000000 /* R-I-V */
#define NV_PGRAPH_INTR_TLB_PRESENT_B_PENDING             0x00000001 /* R---V */
#define NV_PGRAPH_INTR_TLB_PRESENT_B_RESET               0x00000001 /* -W--C */
#define NV_PGRAPH_INTR_CONTEXT_SWITCH                         12:12 /* RWIVF */
#define NV_PGRAPH_INTR_CONTEXT_SWITCH_NOT_PENDING        0x00000000 /* R-I-V */
#define NV_PGRAPH_INTR_CONTEXT_SWITCH_PENDING            0x00000001 /* R---V */
#define NV_PGRAPH_INTR_CONTEXT_SWITCH_RESET              0x00000001 /* -W--C */
#define NV_PGRAPH_INTR_BUFFER_NOTIFY                          16:16 /* RWIVF */
#define NV_PGRAPH_INTR_BUFFER_NOTIFY_NOT_PENDING         0x00000000 /* R-I-V */
#define NV_PGRAPH_INTR_BUFFER_NOTIFY_PENDING             0x00000001 /* R---V */
#define NV_PGRAPH_INTR_BUFFER_NOTIFY_RESET               0x00000001 /* -W--C */
#define NV_PGRAPH_NSTATUS                                0x00400104 /* RW-4R */
#define NV_PGRAPH_NSTATUS_STATE_IN_USE                        11:11 /* RWIVF */
#define NV_PGRAPH_NSTATUS_STATE_IN_USE_NOT_PENDING       0x00000000 /* RWI-V */
#define NV_PGRAPH_NSTATUS_STATE_IN_USE_PENDING           0x00000001 /* RW--V */
#define NV_PGRAPH_NSTATUS_INVALID_STATE                       12:12 /* RWIVF */
#define NV_PGRAPH_NSTATUS_INVALID_STATE_NOT_PENDING      0x00000000 /* RWI-V */
#define NV_PGRAPH_NSTATUS_INVALID_STATE_PENDING          0x00000001 /* RW--V */
#define NV_PGRAPH_NSTATUS_BAD_ARGUMENT                        13:13 /* RWIVF */
#define NV_PGRAPH_NSTATUS_BAD_ARGUMENT_NOT_PENDING       0x00000000 /* RWI-V */
#define NV_PGRAPH_NSTATUS_BAD_ARGUMENT_PENDING           0x00000001 /* RW--V */
#define NV_PGRAPH_NSTATUS_PROTECTION_FAULT                    14:14 /* RWIVF */
#define NV_PGRAPH_NSTATUS_PROTECTION_FAULT_NOT_PENDING   0x00000000 /* RWI-V */
#define NV_PGRAPH_NSTATUS_PROTECTION_FAULT_PENDING       0x00000001 /* RW--V */
#define NV_PGRAPH_NSOURCE                                0x00400108 /* R--4R */
#define NV_PGRAPH_NSOURCE_NOTIFICATION                          0:0 /* R-IVF */
#define NV_PGRAPH_NSOURCE_NOTIFICATION_NOT_PENDING       0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_NOTIFICATION_PENDING           0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_DATA_ERROR                            1:1 /* R-IVF */
#define NV_PGRAPH_NSOURCE_DATA_ERROR_NOT_PENDING         0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_DATA_ERROR_PENDING             0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_PROTECTION_ERROR                      2:2 /* R-IVF */
#define NV_PGRAPH_NSOURCE_PROTECTION_ERROR_NOT_PENDING   0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_PROTECTION_ERROR_PENDING       0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_RANGE_EXCEPTION                       3:3 /* R-IVF */
#define NV_PGRAPH_NSOURCE_RANGE_EXCEPTION_NOT_PENDING    0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_RANGE_EXCEPTION_PENDING        0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_LIMIT_COLOR                           4:4 /* R-IVF */
#define NV_PGRAPH_NSOURCE_LIMIT_COLOR_NOT_PENDING        0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_LIMIT_COLOR_PENDING            0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_LIMIT_ZETA_                           5:5 /* R-IVF */
#define NV_PGRAPH_NSOURCE_LIMIT_ZETA_NOT_PENDING         0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_LIMIT_ZETA_PENDING             0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_ILLEGAL_MTHD                          6:6 /* R-IVF */
#define NV_PGRAPH_NSOURCE_ILLEGAL_MTHD_NOT_PENDING       0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_ILLEGAL_MTHD_PENDING           0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_DMA_R_PROTECTION                      7:7 /* R-IVF */
#define NV_PGRAPH_NSOURCE_DMA_R_PROTECTION_NOT_PENDING   0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_DMA_R_PROTECTION_PENDING       0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_DMA_W_PROTECTION                      8:8 /* R-IVF */
#define NV_PGRAPH_NSOURCE_DMA_W_PROTECTION_NOT_PENDING   0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_DMA_W_PROTECTION_PENDING       0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_FORMAT_EXCEPTION                      9:9 /* R-IVF */
#define NV_PGRAPH_NSOURCE_FORMAT_EXCEPTION_NOT_PENDING   0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_FORMAT_EXCEPTION_PENDING       0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_PATCH_EXCEPTION                     10:10 /* R-IVF */
#define NV_PGRAPH_NSOURCE_PATCH_EXCEPTION_NOT_PENDING    0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_PATCH_EXCEPTION_PENDING        0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_STATE_INVALID                       11:11 /* R-IVF */
#define NV_PGRAPH_NSOURCE_STATE_INVALID_NOT_PENDING      0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_STATE_INVALID_PENDING          0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_DOUBLE_NOTIFY                       12:12 /* R-IVF */
#define NV_PGRAPH_NSOURCE_DOUBLE_NOTIFY_NOT_PENDING      0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_DOUBLE_NOTIFY_PENDING          0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_NOTIFY_IN_USE                       13:13 /* R-IVF */
#define NV_PGRAPH_NSOURCE_NOTIFY_IN_USE_NOT_PENDING      0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_NOTIFY_IN_USE_PENDING          0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_METHOD_CNT                          14:14 /* R-IVF */
#define NV_PGRAPH_NSOURCE_METHOD_CNT_NOT_PENDING         0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_METHOD_CNT_PENDING             0x00000001 /* R---V */
#define NV_PGRAPH_NSOURCE_BFR_NOTIFICATION                    15:15 /* R-IVF */
#define NV_PGRAPH_NSOURCE_BFR_NOTIFICATION_NOT_PENDING   0x00000000 /* R-I-V */
#define NV_PGRAPH_NSOURCE_BFR_NOTIFICATION_PENDING       0x00000001 /* R---V */
#define NV_PGRAPH_INTR_EN                                0x00400140 /* RW-4R */
#define NV_PGRAPH_INTR_EN_NOTIFY                                0:0 /* RWIVF */
#define NV_PGRAPH_INTR_EN_NOTIFY_DISABLED                0x00000000 /* RWI-V */
#define NV_PGRAPH_INTR_EN_NOTIFY_ENABLED                 0x00000001 /* RW--V */
#define NV_PGRAPH_INTR_EN_MISSING_HW                            4:4 /* RWIVF */
#define NV_PGRAPH_INTR_EN_MISSING_HW_DISABLED            0x00000000 /* RWI-V */
#define NV_PGRAPH_INTR_EN_MISSING_HW_ENABLED             0x00000001 /* RW--V */
#define NV_PGRAPH_INTR_EN_TLB_PRESENT_A                         8:8 /* RWIVF */
#define NV_PGRAPH_INTR_EN_TLB_PRESENT_A_DISABLED         0x00000000 /* RWI-V */
#define NV_PGRAPH_INTR_EN_TLB_PRESENT_A_ENABLED          0x00000001 /* RW--V */
#define NV_PGRAPH_INTR_EN_TLB_PRESENT_B                         9:9 /* RWIVF */
#define NV_PGRAPH_INTR_EN_TLB_PRESENT_B_DISABLED         0x00000000 /* RWI-V */
#define NV_PGRAPH_INTR_EN_TLB_PRESENT_B_ENABLED          0x00000001 /* RW--V */
#define NV_PGRAPH_INTR_EN_CONTEXT_SWITCH                      12:12 /* RWIVF */
#define NV_PGRAPH_INTR_EN_CONTEXT_SWITCH_DISABLED        0x00000000 /* RWI-V */
#define NV_PGRAPH_INTR_EN_CONTEXT_SWITCH_ENABLED         0x00000001 /* RW--V */
#define NV_PGRAPH_INTR_EN_BUFFER_NOTIFY                       16:16 /* RWIVF */
#define NV_PGRAPH_INTR_EN_BUFFER_NOTIFY_DISABLED         0x00000000 /* RWI-V */
#define NV_PGRAPH_INTR_EN_BUFFER_NOTIFY_ENABLED          0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1                            0x00400160 /* RW-4R */
#define NV_PGRAPH_CTX_SWITCH1_GRCLASS                           7:0 /* RWXVF */
#define NV_PGRAPH_CTX_SWITCH1_CHROMA_KEY                      12:12 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH1_CHROMA_KEY_DISABLE         0x00000000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_CHROMA_KEY_ENABLE          0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_USER_CLIP                       13:13 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH1_USER_CLIP_DISABLE          0x00000000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_USER_CLIP_ENABLE           0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_SWIZZLE                         14:14 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH1_SWIZZLE_DISABLE            0x00000000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_SWIZZLE_ENABLE             0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG                    17:15 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG_SRCCOPY_AND   0x00000000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG_ROP_AND       0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG_BLEND_AND     0x00000002 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG_SRCCOPY       0x00000003 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG_SRCCOPY_PRE   0x00000004 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_CONFIG_BLEND_PRE     0x00000005 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_STATUS                    24:24 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_STATUS_INVALID       0x00000000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_PATCH_STATUS_VALID         0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE                 25:25 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE_INVALID    0x00000000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_CONTEXT_SURFACE_VALID      0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH1_VOLATILE_RESET                  31:31 /* CWIVF */
#define NV_PGRAPH_CTX_SWITCH1_VOLATILE_RESET_IGNORE      0x00000000 /* CWI-V */
#define NV_PGRAPH_CTX_SWITCH1_VOLATILE_RESET_ENABLED     0x00000001 /* -W--T */
#define NV_PGRAPH_CTX_SWITCH2                            0x00400164 /* RW-4R */
#define NV_PGRAPH_CTX_SWITCH2_MONO_FORMAT                       1:0 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH2_MONO_FORMAT_INVALID              0x00 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_MONO_FORMAT_CGA6_M1              0x01 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_MONO_FORMAT_LE_M1                0x02 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT                     13:8 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_INVALID             0x00 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_Y8               0x01 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X16A8Y8          0x02 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X24Y8            0x03 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_A1R5G5B5         0x06 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X1R5G5B5         0x07 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X16A1R5G5B5      0x08 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X17R5G5B5        0x09 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_R5G6B5           0x0A /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_A16R5G6B5        0x0B /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X16R5G6B5        0x0C /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_A8R8G8B8         0x0D /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X8R8G8B8         0x0E /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_Y16              0x0F /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_A16Y16           0x10 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_X16Y16           0x11 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_V8YB8U8YA8       0x12 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_YB8V8YA8U8       0x13 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_COLOR_FORMAT_LE_Y32              0x14 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH2_NOTIFY_INSTANCE                 31:16 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH2_NOTIFY_INSTANCE_INVALID        0x0000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH3                            0x00400168 /* RW-4R */
#define NV_PGRAPH_CTX_SWITCH3_DMA_INSTANCE_0                   15:0 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH3_DMA_INSTANCE_0_INVALID         0x0000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH3_DMA_INSTANCE_1                  31:16 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH3_DMA_INSTANCE_1_INVALID         0x0000 /* RW--V */
#define NV_PGRAPH_CTX_SWITCH4                            0x0040016C /* RW-4R */
#define NV_PGRAPH_CTX_SWITCH4_USER_INSTANCE                    15:0 /* RWXUF */
#define NV_PGRAPH_CTX_SWITCH4_USER_INSTANCE_INVALID          0x0000 /* RW--V */
#define NV_PGRAPH_CTX_CACHE1(i)                  (0x00400180+(i)*4) /* RW-4A */
#define NV_PGRAPH_CTX_CACHE1__SIZE_1                              8 /*       */
#define NV_PGRAPH_CTX_CACHE1_GRCLASS                            7:0 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_CHROMA_KEY                       12:12 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_USER_CLIP                        13:13 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_SWIZZLE                          14:14 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_PATCH_CONFIG                     19:15 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_SPARE1                           20:20 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_PATCH_STATUS                     24:24 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE1_CONTEXT_SURFACE                  25:25 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE2(i)                  (0x004001a0+(i)*4) /* RW-4A */
#define NV_PGRAPH_CTX_CACHE2__SIZE_1                              8 /*       */
#define NV_PGRAPH_CTX_CACHE2_MONO_FORMAT                        1:0 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE2_COLOR_FORMAT                      13:8 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE2_NOTIFY_INSTANCE                  31:16 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE3(i)                  (0x004001c0+(i)*4) /* RW-4A */
#define NV_PGRAPH_CTX_CACHE3__SIZE_1                              8 /*       */
#define NV_PGRAPH_CTX_CACHE3_DMA_INSTANCE_0                    15:0 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE3_DMA_INSTANCE_1                   31:16 /* RWXVF */
#define NV_PGRAPH_CTX_CACHE4(i)                  (0x004001e0+(i)*4) /* RW-4A */
#define NV_PGRAPH_CTX_CACHE4__SIZE_1                              8 /*       */
#define NV_PGRAPH_CTX_CACHE4_USER_INSTANCE                     15:0 /* RWXVF */
#define NV_PGRAPH_CTX_CONTROL                            0x00400170 /* RW-4R */
#define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME                      1:0 /* RWIVF */
#define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME_33US          0x00000000 /* RWI-V */
#define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME_262US         0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME_2MS           0x00000002 /* RW--V */
#define NV_PGRAPH_CTX_CONTROL_MINIMUM_TIME_17MS          0x00000003 /* RW--V */
#define NV_PGRAPH_CTX_CONTROL_TIME                              8:8 /* RWIVF */
#define NV_PGRAPH_CTX_CONTROL_TIME_EXPIRED               0x00000000 /* RWI-V */
#define NV_PGRAPH_CTX_CONTROL_TIME_NOT_EXPIRED           0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_CONTROL_CHID                            16:16 /* RWIVF */
#define NV_PGRAPH_CTX_CONTROL_CHID_INVALID               0x00000000 /* RWI-V */
#define NV_PGRAPH_CTX_CONTROL_CHID_VALID                 0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_CONTROL_CHANGE                          20:20 /* R--VF */
#define NV_PGRAPH_CTX_CONTROL_CHANGE_UNAVAILABLE         0x00000000 /* R---V */
#define NV_PGRAPH_CTX_CONTROL_CHANGE_AVAILABLE           0x00000001 /* R---V */
#define NV_PGRAPH_CTX_CONTROL_SWITCHING                       24:24 /* RWIVF */
#define NV_PGRAPH_CTX_CONTROL_SWITCHING_IDLE             0x00000000 /* RWI-V */
#define NV_PGRAPH_CTX_CONTROL_SWITCHING_BUSY             0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_CONTROL_DEVICE                          28:28 /* RWIVF */
#define NV_PGRAPH_CTX_CONTROL_DEVICE_DISABLED            0x00000000 /* RWI-V */
#define NV_PGRAPH_CTX_CONTROL_DEVICE_ENABLED             0x00000001 /* RW--V */
#define NV_PGRAPH_CTX_USER                               0x00400174 /* RW-4R */
#define NV_PGRAPH_CTX_USER_SUBCH                              15:13 /* RWIVF */
#define NV_PGRAPH_CTX_USER_SUBCH_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_CTX_USER_CHID                               27:24 /* RWIVF */
#define NV_PGRAPH_CTX_USER_CHID_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_FIFO                                   0x00400720 /* RW-4R */
#define NV_PGRAPH_FIFO_ACCESS                                   0:0 /* RWIVF */
#define NV_PGRAPH_FIFO_ACCESS_DISABLED                   0x00000000 /* RW--V */
#define NV_PGRAPH_FIFO_ACCESS_ENABLED                    0x00000001 /* RWI-V */
#define NV_PGRAPH_FFINTFC_FIFO_0(i)              (0x00400730+(i)*4) /* RW-4A */
#define NV_PGRAPH_FFINTFC_FIFO_0__SIZE_1                          4 /*       */
#define NV_PGRAPH_FFINTFC_FIFO_0_TAG                            0:0 /* RWXVF */
#define NV_PGRAPH_FFINTFC_FIFO_0_TAG_MTHD                0x00000000 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_TAG_CHSW                0x00000001 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH                          3:1 /* RWXVF */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_0                 0x00000000 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_1                 0x00000001 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_2                 0x00000002 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_3                 0x00000003 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_4                 0x00000004 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_5                 0x00000005 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_6                 0x00000006 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_SUBCH_7                 0x00000007 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_0_MTHD                          14:4 /* RWXVF */
#define NV_PGRAPH_FFINTFC_FIFO_0_MTHD_CTX_SWITCH         0x00000000 /* RW--V */
#define NV_PGRAPH_FFINTFC_FIFO_1(i)              (0x00400740+(i)*4) /* RW-4A */
#define NV_PGRAPH_FFINTFC_FIFO_1__SIZE_1                          4 /*       */
#define NV_PGRAPH_FFINTFC_FIFO_1_ARGUMENT                      31:0 /* RWXVF */
#define NV_PGRAPH_FFINTFC_FIFO_PTR                       0x00400750 /* RW-4R */
#define NV_PGRAPH_FFINTFC_FIFO_PTR_WRITE                        2:0 /* RWIVF */
#define NV_PGRAPH_FFINTFC_FIFO_PTR_WRITE_0               0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_FIFO_PTR_READ                         6:4 /* RWIVF */
#define NV_PGRAPH_FFINTFC_FIFO_PTR_READ_0                0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_ST2                            0x00400754 /* RW-4R */
#define NV_PGRAPH_FFINTFC_ST2_STATUS                            0:0 /* RWIVF */
#define NV_PGRAPH_FFINTFC_ST2_STATUS_INVALID             0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_ST2_STATUS_VALID               0x00000001 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_MTHD                             11:1 /* RWIVF */
#define NV_PGRAPH_FFINTFC_ST2_MTHD_CTX_SWITCH            0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH                           14:12 /* RWIVF */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_0                    0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_1                    0x00000001 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_2                    0x00000002 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_3                    0x00000003 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_4                    0x00000004 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_5                    0x00000005 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_6                    0x00000006 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_SUBCH_7                    0x00000007 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID                            18:15 /* RWIVF */
#define NV_PGRAPH_FFINTFC_ST2_CHID_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_1                     0x00000001 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_2                     0x00000002 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_3                     0x00000003 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_4                     0x00000004 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_5                     0x00000005 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_6                     0x00000006 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_7                     0x00000007 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_8                     0x00000008 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_9                     0x00000009 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_10                    0x0000000A /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_11                    0x0000000B /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_12                    0x0000000C /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_13                    0x0000000D /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_14                    0x0000000E /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_15                    0x0000000F /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_STATUS                     19:19 /* RWIVF */
#define NV_PGRAPH_FFINTFC_ST2_CHID_STATUS_INVALID        0x00000000 /* RWI-V */
#define NV_PGRAPH_FFINTFC_ST2_CHID_STATUS_VALID          0x00000001 /* RW--V */
#define NV_PGRAPH_FFINTFC_ST2_D                          0x00400758 /* RW-4R */
#define NV_PGRAPH_FFINTFC_ST2_D_ARGUMENT                       31:0 /* RWIVF */
#define NV_PGRAPH_FFINTFC_ST2_D_ARGUMENT_0               0x00000000 /* RWI-V */
#define NV_PGRAPH_STATUS                                 0x00400700 /* R--4R */
#define NV_PGRAPH_STATUS_STATE                                  0:0 /* R-IVF */
#define NV_PGRAPH_STATUS_STATE_IDLE                      0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_STATE_BUSY                      0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_XY_LOGIC                               4:4 /* R-IVF */
#define NV_PGRAPH_STATUS_XY_LOGIC_IDLE                   0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_XY_LOGIC_BUSY                   0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_FE                                     5:5 /* R-IVF */
#define NV_PGRAPH_STATUS_FE_IDLE                         0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_FE_BUSY                         0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_RASTERIZER                             6:6 /* R-IVF */
#define NV_PGRAPH_STATUS_RASTERIZER_IDLE                 0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_RASTERIZER_BUSY                 0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_PORT_NOTIFY                            8:8 /* R-IVF */
#define NV_PGRAPH_STATUS_PORT_NOTIFY_IDLE                0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_PORT_NOTIFY_BUSY                0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_PORT_REGISTER                        12:12 /* R-IVF */
#define NV_PGRAPH_STATUS_PORT_REGISTER_IDLE              0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_PORT_REGISTER_BUSY              0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_PORT_DMA                             16:16 /* R-IVF */
#define NV_PGRAPH_STATUS_PORT_DMA_IDLE                   0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_PORT_DMA_BUSY                   0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_DMA_ENGINE                           17:17 /* R-IVF */
#define NV_PGRAPH_STATUS_DMA_ENGINE_IDLE                 0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_DMA_ENGINE_BUSY                 0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_DMA_NOTIFY                           20:20 /* R-IVF */
#define NV_PGRAPH_STATUS_DMA_NOTIFY_IDLE                 0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_DMA_NOTIFY_BUSY                 0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_DMA_BUFFER_NOTIFY                    21:21 /* R-IVF */
#define NV_PGRAPH_STATUS_DMA_BUFFER_NOTIFY_IDLE          0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_DMA_BUFFER_NOTIFY_BUSY          0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_D3D                                  24:24 /* R-IVF */
#define NV_PGRAPH_STATUS_D3D_IDLE                        0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_D3D_BUSY                        0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_CACHE                                25:25 /* R-IVF */
#define NV_PGRAPH_STATUS_CACHE_IDLE                      0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_CACHE_BUSY                      0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_LIGHTING                             26:26 /* R-IVF */
#define NV_PGRAPH_STATUS_LIGHTING_IDLE                   0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_LIGHTING_BUSY                   0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_PREROP                               27:27 /* R-IVF */
#define NV_PGRAPH_STATUS_PREROP_IDLE                     0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_PREROP_BUSY                     0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_ROP                                  28:28 /* R-IVF */
#define NV_PGRAPH_STATUS_ROP_IDLE                        0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_ROP_BUSY                        0x00000001 /* R---V */
#define NV_PGRAPH_STATUS_PORT_USER                            29:29 /* R-IVF */
#define NV_PGRAPH_STATUS_PORT_USER_IDLE                  0x00000000 /* R-I-V */
#define NV_PGRAPH_STATUS_PORT_USER_BUSY                  0x00000001 /* R---V */
#define NV_PGRAPH_TRAPPED_ADDR                           0x00400704 /* R--4R */
#define NV_PGRAPH_TRAPPED_ADDR_MTHD                            12:2 /* R-XUF */
#define NV_PGRAPH_TRAPPED_ADDR_SUBCH                          15:13 /* R-XUF */
#define NV_PGRAPH_TRAPPED_ADDR_CHID                           27:24 /* R-XUF */
#define NV_PGRAPH_TRAPPED_DATA                           0x00400708 /* R--4R */
#define NV_PGRAPH_TRAPPED_DATA_VALUE                           31:0 /* R-XVF */
#define NV_PGRAPH_SURFACE                                0x0040070C /* RW-4R */
#define NV_PGRAPH_SURFACE_TYPE                                  1:0 /* RWIVF */
#define NV_PGRAPH_SURFACE_TYPE_INVALID                   0x00000000 /* RWI-V */
#define NV_PGRAPH_SURFACE_TYPE_NON_SWIZZLE               0x00000001 /* RW--V */
#define NV_PGRAPH_SURFACE_TYPE_SWIZZLE                   0x00000002 /* RW--V */
#define NV_PGRAPH_NOTIFY                                 0x00400714 /* RW-4R */
#define NV_PGRAPH_NOTIFY_BUFFER_REQ                             0:0 /* RWIVF */
#define NV_PGRAPH_NOTIFY_BUFFER_REQ_NOT_PENDING          0x00000000 /* RWI-V */
#define NV_PGRAPH_NOTIFY_BUFFER_REQ_PENDING              0x00000001 /* RW--V */
#define NV_PGRAPH_NOTIFY_BUFFER_STYLE                           8:8 /* RWIVF */
#define NV_PGRAPH_NOTIFY_BUFFER_STYLE_WRITE_ONLY         0x00000000 /* RWI-V */
#define NV_PGRAPH_NOTIFY_BUFFER_STYLE_WRITE_THEN_AWAKEN  0x00000001 /* RW--V */
#define NV_PGRAPH_NOTIFY_REQ                                  16:16 /* RWIVF */
#define NV_PGRAPH_NOTIFY_REQ_NOT_PENDING                 0x00000000 /* RWI-V */
#define NV_PGRAPH_NOTIFY_REQ_PENDING                     0x00000001 /* RW--V */
#define NV_PGRAPH_NOTIFY_STYLE                                20:20 /* RWIVF */
#define NV_PGRAPH_NOTIFY_STYLE_WRITE_ONLY                0x00000000 /* RWI-V */
#define NV_PGRAPH_NOTIFY_STYLE_WRITE_THEN_AWAKEN         0x00000001 /* RW--V */
#define NV_PGRAPH_BOFFSET(i)                     (0x00400640+(i)*4) /* RW-4A */
#define NV_PGRAPH_BOFFSET__SIZE_1                                 6 /*       */
#define NV_PGRAPH_BOFFSET_LINADRS                              23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET_LINADRS_0                      0x00000000 /* RWI-V */
#define NV_PGRAPH_BOFFSET0                               0x00400640 /* RW-4R */
#define NV_PGRAPH_BOFFSET0__ALIAS_1            NV_PGRAPH_BOFFSET(0) /*       */
#define NV_PGRAPH_BOFFSET0_LINADRS                             23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET0_LINADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BOFFSET1                               0x00400644 /* RW-4R */
#define NV_PGRAPH_BOFFSET1__ALIAS_1            NV_PGRAPH_BOFFSET(1) /*       */
#define NV_PGRAPH_BOFFSET1_LINADRS                             23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET1_LINADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BOFFSET2                               0x00400648 /* RW-4R */
#define NV_PGRAPH_BOFFSET2__ALIAS_1            NV_PGRAPH_BOFFSET(2) /*       */
#define NV_PGRAPH_BOFFSET2_LINADRS                             23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET2_LINADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BOFFSET3                               0x0040064C /* RW-4R */
#define NV_PGRAPH_BOFFSET3__ALIAS_1            NV_PGRAPH_BOFFSET(3) /*       */
#define NV_PGRAPH_BOFFSET3_LINADRS                             23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET3_LINADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BOFFSET4                               0x00400650 /* RW-4R */
#define NV_PGRAPH_BOFFSET4__ALIAS_1            NV_PGRAPH_BOFFSET(4) /*       */
#define NV_PGRAPH_BOFFSET4_LINADRS                             23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET4_LINADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BOFFSET5                               0x00400654 /* RW-4R */
#define NV_PGRAPH_BOFFSET5__ALIAS_1            NV_PGRAPH_BOFFSET(5) /*       */
#define NV_PGRAPH_BOFFSET5_LINADRS                             23:0 /* RWIUF */
#define NV_PGRAPH_BOFFSET5_LINADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE(i)                       (0x00400658+(i)*4) /* RW-4A */
#define NV_PGRAPH_BBASE__SIZE_1                                   6 /*       */
#define NV_PGRAPH_BBASE_LINADRS                                23:0 /* RWIUF */
#define NV_PGRAPH_BBASE_LINADRS_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE0                                 0x00400658 /* RW-4R */
#define NV_PGRAPH_BBASE0__ALIAS_1                NV_PGRAPH_BBASE(0) /*       */
#define NV_PGRAPH_BBASE0_LINADRS                               23:0 /* RWIUF */
#define NV_PGRAPH_BBASE0_LINADRS_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE1                                 0x0040065c /* RW-4R */
#define NV_PGRAPH_BBASE1__ALIAS_1                NV_PGRAPH_BBASE(1) /*       */
#define NV_PGRAPH_BBASE1_LINADRS                               23:0 /* RWIUF */
#define NV_PGRAPH_BBASE1_LINADRS_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE2                                 0x00400660 /* RW-4R */
#define NV_PGRAPH_BBASE2__ALIAS_1                NV_PGRAPH_BBASE(2) /*       */
#define NV_PGRAPH_BBASE2_LINADRS                               23:0 /* RWIUF */
#define NV_PGRAPH_BBASE2_LINADRS_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE3                                 0x00400664 /* RW-4R */
#define NV_PGRAPH_BBASE3__ALIAS_1                NV_PGRAPH_BBASE(3) /*       */
#define NV_PGRAPH_BBASE3_LINADRS                               23:0 /* RWIUF */
#define NV_PGRAPH_BBASE3_LINADRS_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE4                                 0x00400668 /* RW-4R */
#define NV_PGRAPH_BBASE4__ALIAS_1                NV_PGRAPH_BBASE(4) /*       */
#define NV_PGRAPH_BBASE4_LINADRS                               23:0 /* RWIUF */
#define NV_PGRAPH_BBASE4_LINADRS_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_BBASE5                                 0x0040066C /* RW-4R */
#define NV_PGRAPH_BBASE5__ALIAS_1                NV_PGRAPH_BBASE(5) /*       */
#define NV_PGRAPH_BBASE5_LINADRS                               23:0 /* RWIUF */
#define NV_PGRAPH_BBASE5_LINADRS_0                       0x00000000 /* RWI-V */
#define NV_PGRAPH_BPITCH(i)                      (0x00400670+(i)*4) /* RW-4A */
#define NV_PGRAPH_BPITCH__SIZE_1                                  5 /*       */
#define NV_PGRAPH_BPITCH_VALUE                                 12:0 /* RWIUF */
#define NV_PGRAPH_BPITCH_VALUE_0                         0x00000000 /* RWI-V */
#define NV_PGRAPH_BPITCH0                                0x00400670 /* RW-4R */
#define NV_PGRAPH_BPITCH0__ALIAS_1              NV_PGRAPH_BPITCH(0) /*       */
#define NV_PGRAPH_BPITCH0_VALUE                                12:0 /* RWIUF */
#define NV_PGRAPH_BPITCH0_VALUE_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_BPITCH1                                0x00400674 /* RW-4R */
#define NV_PGRAPH_BPITCH1__ALIAS_1              NV_PGRAPH_BPITCH(1) /*       */
#define NV_PGRAPH_BPITCH1_VALUE                                12:0 /* RWIUF */
#define NV_PGRAPH_BPITCH1_VALUE_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_BPITCH2                                0x00400678 /* RW-4R */
#define NV_PGRAPH_BPITCH2__ALIAS_1              NV_PGRAPH_BPITCH(2) /*       */
#define NV_PGRAPH_BPITCH2_VALUE                                12:0 /* RWIUF */
#define NV_PGRAPH_BPITCH2_VALUE_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_BPITCH3                                0x0040067C /* RW-4R */
#define NV_PGRAPH_BPITCH3__ALIAS_1              NV_PGRAPH_BPITCH(3) /*       */
#define NV_PGRAPH_BPITCH3_VALUE                                12:0 /* RWIUF */
#define NV_PGRAPH_BPITCH3_VALUE_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_BPITCH4                                0x00400680 /* RW-4R */
#define NV_PGRAPH_BPITCH4__ALIAS_1              NV_PGRAPH_BPITCH(4) /*       */
#define NV_PGRAPH_BPITCH4_VALUE                                12:0 /* RWIUF */
#define NV_PGRAPH_BPITCH4_VALUE_0                        0x00000000 /* RWI-V */
#define NV_PGRAPH_BLIMIT(i)                      (0x00400684+(i)*4) /* RW-4A */
#define NV_PGRAPH_BLIMIT__SIZE_1                                  6 /*       */
#define NV_PGRAPH_BLIMIT_VALUE                                 23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT_TYPE                                 31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT_TYPE_IN_MEMORY                  0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT_TYPE_NULL                       0x00000001 /* RWI-V */
#define NV_PGRAPH_BLIMIT0                                0x00400684 /* RW-4R */
#define NV_PGRAPH_BLIMIT0__ALIAS_1              NV_PGRAPH_BLIMIT(0) /*       */
#define NV_PGRAPH_BLIMIT0_VALUE                                23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT0_TYPE                                31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT0_TYPE_IN_MEMORY                 0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT0_TYPE_NULL                      0x00000001 /* RWI-V */
#define NV_PGRAPH_BLIMIT1                                0x00400688 /* RW-4R */
#define NV_PGRAPH_BLIMIT1__ALIAS_1              NV_PGRAPH_BLIMIT(1) /*       */
#define NV_PGRAPH_BLIMIT1_VALUE                                23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT1_TYPE                                31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT1_TYPE_IN_MEMORY                 0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT1_TYPE_NULL                      0x00000001 /* RWI-V */
#define NV_PGRAPH_BLIMIT2                                0x0040068c /* RW-4R */
#define NV_PGRAPH_BLIMIT2__ALIAS_1              NV_PGRAPH_BLIMIT(2) /*       */
#define NV_PGRAPH_BLIMIT2_VALUE                                23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT2_TYPE                                31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT2_TYPE_IN_MEMORY                 0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT2_TYPE_NULL                      0x00000001 /* RWI-V */
#define NV_PGRAPH_BLIMIT3                                0x00400690 /* RW-4R */
#define NV_PGRAPH_BLIMIT3__ALIAS_1              NV_PGRAPH_BLIMIT(3) /*       */
#define NV_PGRAPH_BLIMIT3_VALUE                                23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT3_TYPE                                31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT3_TYPE_IN_MEMORY                 0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT3_TYPE_NULL                      0x00000001 /* RWI-V */
#define NV_PGRAPH_BLIMIT4                                0x00400694 /* RW-4R */
#define NV_PGRAPH_BLIMIT4__ALIAS_1              NV_PGRAPH_BLIMIT(4) /*       */
#define NV_PGRAPH_BLIMIT4_VALUE                                23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT4_TYPE                                31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT4_TYPE_IN_MEMORY                 0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT4_TYPE_NULL                      0x00000001 /* RWI-V */
#define NV_PGRAPH_BLIMIT5                                0x00400698 /* RW-4R */
#define NV_PGRAPH_BLIMIT5__ALIAS_1              NV_PGRAPH_BLIMIT(5) /*       */
#define NV_PGRAPH_BLIMIT5_VALUE                                23:0 /* RWXUF */
#define NV_PGRAPH_BLIMIT5_TYPE                                31:31 /* RWIVF */
#define NV_PGRAPH_BLIMIT5_TYPE_IN_MEMORY                 0x00000000 /* RW--V */
#define NV_PGRAPH_BLIMIT5_TYPE_NULL                      0x00000001 /* RWI-V */
#define NV_PGRAPH_BSWIZZLE2                              0x0040069c /* RW-4R */
#define NV_PGRAPH_BSWIZZLE2_WIDTH                             19:16 /* RWIUF */
#define NV_PGRAPH_BSWIZZLE2_WIDTH_0                      0x00000000 /* RWI-V */
#define NV_PGRAPH_BSWIZZLE2_HEIGHT                            27:24 /* RWIUF */
#define NV_PGRAPH_BSWIZZLE2_HEIGHT_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BSWIZZLE5                              0x004006a0 /* RW-4R */
#define NV_PGRAPH_BSWIZZLE5_WIDTH                             19:16 /* RWIUF */
#define NV_PGRAPH_BSWIZZLE5_WIDTH_0                      0x00000000 /* RWI-V */
#define NV_PGRAPH_BSWIZZLE5_HEIGHT                            27:24 /* RWIUF */
#define NV_PGRAPH_BSWIZZLE5_HEIGHT_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL                                 0x00400724 /* RW-4R */
#define NV_PGRAPH_BPIXEL_DEPTH0                                 3:0 /* RWIVF */
#define NV_PGRAPH_BPIXEL_DEPTH0_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL_DEPTH0_Y8                       0x00000001 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X1R5G5B5_Z1R5G5B5        0x00000002 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X1R5G5B5_O1R5G5B5        0x00000003 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_A1R5G5B5                 0x00000004 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_R5G6B5                   0x00000005 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_Y16                      0x00000006 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X8R8G8B8_Z8R8G8B8        0x00000007 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X8R8G8B8_O1Z7R8G8B8      0x00000008 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X1A7R8G8B8_Z1A7R8G8B8    0x00000009 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X1A7R8G8B8_O1A7R8G8B8    0x0000000a /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_X8R8G8B8_O8R8G8B8        0x0000000b /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_A8R8G8B8                 0x0000000c /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_Y32                      0x0000000d /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_V8YB8U8YA8               0x0000000e /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH0_YB8V8YA8U8               0x0000000f /* RW--V */ 
#define NV_PGRAPH_BPIXEL_DEPTH1                                 7:4 /* RWIVF */
#define NV_PGRAPH_BPIXEL_DEPTH1_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL_DEPTH1_Y8                       0x00000001 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X1R5G5B5_Z1R5G5B5        0x00000002 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X1R5G5B5_O1R5G5B5        0x00000003 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_A1R5G5B5                 0x00000004 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_R5G6B5                   0x00000005 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_Y16                      0x00000006 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X8R8G8B8_Z8R8G8B8        0x00000007 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X8R8G8B8_O1Z7R8G8B8      0x00000008 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X1A7R8G8B8_Z1A7R8G8B8    0x00000009 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X1A7R8G8B8_O1A7R8G8B8    0x0000000a /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_X8R8G8B8_O8R8G8B8        0x0000000b /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_A8R8G8B8                 0x0000000c /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_Y32                      0x0000000d /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_V8YB8U8YA8               0x0000000e /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH1_YB8V8YA8U8               0x0000000f /* RW--V */ 
#define NV_PGRAPH_BPIXEL_DEPTH2                                11:8 /* RWIVF */
#define NV_PGRAPH_BPIXEL_DEPTH2_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL_DEPTH2_Y8                       0x00000001 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X1R5G5B5_Z1R5G5B5        0x00000002 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X1R5G5B5_O1R5G5B5        0x00000003 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_A1R5G5B5                 0x00000004 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_R5G6B5                   0x00000005 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_Y16                      0x00000006 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X8R8G8B8_Z8R8G8B8        0x00000007 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X8R8G8B8_O1Z7R8G8B8      0x00000008 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X1A7R8G8B8_Z1A7R8G8B8    0x00000009 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X1A7R8G8B8_O1A7R8G8B8    0x0000000a /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_X8R8G8B8_O8R8G8B8        0x0000000b /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_A8R8G8B8                 0x0000000c /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_Y32                      0x0000000d /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_V8YB8U8YA8               0x0000000e /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH2_YB8V8YA8U8               0x0000000f /* RW--V */ 
#define NV_PGRAPH_BPIXEL_DEPTH3                               15:12 /* RWIVF */
#define NV_PGRAPH_BPIXEL_DEPTH3_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL_DEPTH3_Y8                       0x00000001 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X1R5G5B5_Z1R5G5B5        0x00000002 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X1R5G5B5_O1R5G5B5        0x00000003 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_A1R5G5B5                 0x00000004 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_R5G6B5                   0x00000005 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_Y16                      0x00000006 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X8R8G8B8_Z8R8G8B8        0x00000007 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X8R8G8B8_O1Z7R8G8B8      0x00000008 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X1A7R8G8B8_Z1A7R8G8B8    0x00000009 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X1A7R8G8B8_O1A7R8G8B8    0x0000000a /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_X8R8G8B8_O8R8G8B8        0x0000000b /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_A8R8G8B8                 0x0000000c /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_Y32                      0x0000000d /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_V8YB8U8YA8               0x0000000e /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH3_YB8V8YA8U8               0x0000000f /* RW--V */ 
#define NV_PGRAPH_BPIXEL_DEPTH4                               19:16 /* RWIVF */
#define NV_PGRAPH_BPIXEL_DEPTH4_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL_DEPTH4_Y8                       0x00000001 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X1R5G5B5_Z1R5G5B5        0x00000002 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X1R5G5B5_O1R5G5B5        0x00000003 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_A1R5G5B5                 0x00000004 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_R5G6B5                   0x00000005 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_Y16                      0x00000006 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X8R8G8B8_Z8R8G8B8        0x00000007 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X8R8G8B8_O1Z7R8G8B8      0x00000008 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X1A7R8G8B8_Z1A7R8G8B8    0x00000009 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X1A7R8G8B8_O1A7R8G8B8    0x0000000a /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_X8R8G8B8_O8R8G8B8        0x0000000b /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_A8R8G8B8                 0x0000000c /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_Y32                      0x0000000d /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_V8YB8U8YA8               0x0000000e /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH4_YB8V8YA8U8               0x0000000f /* RW--V */ 
#define NV_PGRAPH_BPIXEL_DEPTH5                               23:20 /* RWIVF */
#define NV_PGRAPH_BPIXEL_DEPTH5_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_BPIXEL_DEPTH5_Y8                       0x00000001 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X1R5G5B5_Z1R5G5B5        0x00000002 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X1R5G5B5_O1R5G5B5        0x00000003 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_A1R5G5B5                 0x00000004 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_R5G6B5                   0x00000005 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_Y16                      0x00000006 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X8R8G8B8_Z8R8G8B8        0x00000007 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X8R8G8B8_O1Z7R8G8B8      0x00000008 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X1A7R8G8B8_Z1A7R8G8B8    0x00000009 /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X1A7R8G8B8_O1A7R8G8B8    0x0000000a /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_X8R8G8B8_O8R8G8B8        0x0000000b /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_A8R8G8B8                 0x0000000c /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_Y32                      0x0000000d /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_V8YB8U8YA8               0x0000000e /* RW--V */
#define NV_PGRAPH_BPIXEL_DEPTH5_YB8V8YA8U8               0x0000000f /* RW--V */ 
#define NV_PGRAPH_LIMIT_VIOL_PIX                         0x00400610 /* RW-4R */
#define NV_PGRAPH_LIMIT_VIOL_PIX_ADRS                          23:0 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_PIX_ADRS_0                  0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_PIX_BLIT                         29:29 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_PIX_BLIT_NO_VIOL            0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_PIX_BLIT_VIOL               0x00000001 /* RW--V */
#define NV_PGRAPH_LIMIT_VIOL_PIX_LIMIT                        30:30 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_PIX_LIMIT_NO_VIOL           0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_PIX_LIMIT_VIOL              0x00000001 /* RW--V */
#define NV_PGRAPH_LIMIT_VIOL_PIX_OVRFLW                       31:31 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_PIX_OVRFLW_NO_VIOL          0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_PIX_OVRFLW_VIOL             0x00000001 /* RW--V */
#define NV_PGRAPH_LIMIT_VIOL_Z                           0x00400614 /* RW-4R */
#define NV_PGRAPH_LIMIT_VIOL_Z_ADRS                            23:0 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_Z_ADRS_0                    0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_Z_LIMIT                          30:30 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_Z_LIMIT_NO_VIOL             0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_Z_LIMIT_VIOL                0x00000001 /* RW--V */
#define NV_PGRAPH_LIMIT_VIOL_Z_OVRFLW                         31:31 /* RWIVF */
#define NV_PGRAPH_LIMIT_VIOL_Z_OVRFLW_NO_VIOL            0x00000000 /* RWI-V */
#define NV_PGRAPH_LIMIT_VIOL_Z_OVRFLW_VIOL               0x00000001 /* RW--V */
#define NV_PGRAPH_STATE                                  0x00400710 /* RW-4R */
#define NV_PGRAPH_STATE_BUFFER_0                                0:0 /* RWIVF */
#define NV_PGRAPH_STATE_BUFFER_0_INVALID                 0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_BUFFER_0_VALID                   0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_BUFFER_1                                1:1 /* RWIVF */
#define NV_PGRAPH_STATE_BUFFER_1_INVALID                 0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_BUFFER_1_VALID                   0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_BUFFER_2                                2:2 /* RWIVF */
#define NV_PGRAPH_STATE_BUFFER_2_INVALID                 0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_BUFFER_2_VALID                   0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_BUFFER_3                                3:3 /* RWIVF */
#define NV_PGRAPH_STATE_BUFFER_3_INVALID                 0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_BUFFER_3_VALID                   0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_BUFFER_4                                4:4 /* RWIVF */
#define NV_PGRAPH_STATE_BUFFER_4_INVALID                 0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_BUFFER_4_VALID                   0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_BUFFER_5                                5:5 /* RWIVF */
#define NV_PGRAPH_STATE_BUFFER_5_INVALID                 0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_BUFFER_5_VALID                   0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PITCH_0                                 8:8 /* RWIVF */
#define NV_PGRAPH_STATE_PITCH_0_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PITCH_0_VALID                    0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PITCH_1                                 9:9 /* RWIVF */
#define NV_PGRAPH_STATE_PITCH_1_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PITCH_1_VALID                    0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PITCH_2                               10:10 /* RWIVF */
#define NV_PGRAPH_STATE_PITCH_2_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PITCH_2_VALID                    0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PITCH_3                               11:11 /* RWIVF */
#define NV_PGRAPH_STATE_PITCH_3_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PITCH_3_VALID                    0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PITCH_4                               12:12 /* RWIVF */
#define NV_PGRAPH_STATE_PITCH_4_INVALID                  0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PITCH_4_VALID                    0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_CHROMA_COLOR                          16:16 /* RWIVF */
#define NV_PGRAPH_STATE_CHROMA_COLOR_INVALID             0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_CHROMA_COLOR_VALID               0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_CHROMA_COLORFMT                       17:17 /* RWIVF */
#define NV_PGRAPH_STATE_CHROMA_COLORFMT_INVALID          0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_CHROMA_COLORFMT_VALID            0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_CPATTERN_COLORFMT                     20:20 /* RWIVF */
#define NV_PGRAPH_STATE_CPATTERN_COLORFMT_INVALID        0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_CPATTERN_COLORFMT_VALID          0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_CPATTERN_MONOFMT                      21:21 /* RWIVF */
#define NV_PGRAPH_STATE_CPATTERN_MONOFMT_INVALID         0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_CPATTERN_MONOFMT_VALID           0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_CPATTERN_SELECT                       22:22 /* RWIVF */
#define NV_PGRAPH_STATE_CPATTERN_SELECT_INVALID          0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_CPATTERN_SELECT_VALID            0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PATTERN_COLOR0                        24:24 /* RWIVF */
#define NV_PGRAPH_STATE_PATTERN_COLOR0_INVALID           0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PATTERN_COLOR0_VALID             0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PATTERN_COLOR1                        25:25 /* RWIVF */
#define NV_PGRAPH_STATE_PATTERN_COLOR1_INVALID           0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PATTERN_COLOR1_VALID             0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PATTERN_PATT0                         26:26 /* RWIVF */
#define NV_PGRAPH_STATE_PATTERN_PATT0_INVALID            0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PATTERN_PATT0_VALID              0x00000001 /* RW--V */
#define NV_PGRAPH_STATE_PATTERN_PATT1                         27:27 /* RWIVF */
#define NV_PGRAPH_STATE_PATTERN_PATT1_INVALID            0x00000000 /* RWI-V */
#define NV_PGRAPH_STATE_PATTERN_PATT1_VALID              0x00000001 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX                            0x00400728 /* RW-4R */
#define NV_PGRAPH_CACHE_INDEX_BANK                              2:2 /* RWXVF */
#define NV_PGRAPH_CACHE_INDEX_BANK_10                    0x00000000 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX_BANK_32                    0x00000001 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX_ADRS                             12:3 /* RWXVF */
#define NV_PGRAPH_CACHE_INDEX_ADRS_0                     0x00000000 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX_ADRS_1024                  0x00000400 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX_OP                              14:13 /* RWXVF */
#define NV_PGRAPH_CACHE_INDEX_OP_WR_CACHE                0x00000000 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX_OP_RD_CACHE                0x00000001 /* RW--V */
#define NV_PGRAPH_CACHE_INDEX_OP_RD_INDEX                0x00000002 /* RW--V */
#define NV_PGRAPH_CACHE_RAM                              0x0040072c /* RW-4R */
#define NV_PGRAPH_CACHE_RAM_VALUE                              31:0 /* RWXVF */
#define NV_PGRAPH_DMA_PITCH                              0x00400760 /* RW-4R */
#define NV_PGRAPH_DMA_PITCH_S0                                 15:0 /* RWXSF */
#define NV_PGRAPH_DMA_PITCH_S1                                31:16 /* RWXSF */
#define NV_PGRAPH_DVD_COLORFMT                           0x00400764 /* RW-4R */
#define NV_PGRAPH_DVD_COLORFMT_IMAGE                            5:0 /* RWNVF */
#define NV_PGRAPH_DVD_COLORFMT_IMAGE_FORMAT_INVALID            0x00 /* RWN-V */
#define NV_PGRAPH_DVD_COLORFMT_IMAGE_FORMAT_LE_V8YB8U8YA8      0x12 /* RW--V */
#define NV_PGRAPH_DVD_COLORFMT_IMAGE_FORMAT_LE_YB8V8YA8U8      0x13 /* RW--V */
#define NV_PGRAPH_DVD_COLORFMT_OVLY                             9:8 /* RWNVF */
#define NV_PGRAPH_DVD_COLORFMT_OVLY_FORMAT_INVALID             0x00 /* RWN-V */
#define NV_PGRAPH_DVD_COLORFMT_OVLY_FORMAT_LE_A8Y8U8V8         0x01 /* RW--V */
#define NV_PGRAPH_DVD_COLORFMT_OVLY_FORMAT_LE_A4V6YB6A4U6YA6   0x02 /* RW--V */
#define NV_PGRAPH_DVD_COLORFMT_OVLY_FORMAT_TRANSPARENT         0x03 /* RW--V */
#define NV_PGRAPH_SCALED_FORMAT                          0x00400768 /* RW-4R */
#define NV_PGRAPH_SCALED_FORMAT_ORIGIN                        17:16 /* RWIVF */
#define NV_PGRAPH_SCALED_FORMAT_ORIGIN_INVALID           0x00000000 /* RWI-V */
#define NV_PGRAPH_SCALED_FORMAT_ORIGIN_CENTER            0x00000001 /* RW--V */
#define NV_PGRAPH_SCALED_FORMAT_ORIGIN_CORNER            0x00000002 /* RW--V */
#define NV_PGRAPH_SCALED_FORMAT_INTERPOLATOR                  24:24 /* RWIVF */
#define NV_PGRAPH_SCALED_FORMAT_INTERPOLATOR_ZOH         0x00000000 /* RWI-V */
#define NV_PGRAPH_SCALED_FORMAT_INTERPOLATOR_FOH         0x00000001 /* RW--V */
#define NV_PGRAPH_PATT_COLOR0                            0x00400800 /* RW-4R */
#define NV_PGRAPH_PATT_COLOR0_VALUE                            31:0 /* RWXUF */
#define NV_PGRAPH_PATT_COLOR1                            0x00400804 /* RW-4R */
#define NV_PGRAPH_PATT_COLOR1_VALUE                            31:0 /* RWXUF */
#define NV_PGRAPH_PATT_COLORRAM(i)               (0x00400900+(i)*4) /* R--4A */
#define NV_PGRAPH_PATT_COLORRAM__SIZE_1                          64 /*       */
#define NV_PGRAPH_PATT_COLORRAM_VALUE                          23:0 /* R--UF */
#define NV_PGRAPH_PATTERN(i)                     (0x00400808+(i)*4) /* RW-4A */
#define NV_PGRAPH_PATTERN__SIZE_1                                 2 /*       */
#define NV_PGRAPH_PATTERN_BITMAP                               31:0 /* RWXVF */
#define NV_PGRAPH_PATTERN_SHAPE                          0x00400810 /* RW-4R */
#define NV_PGRAPH_PATTERN_SHAPE_VALUE                           1:0 /* RWXVF */
#define NV_PGRAPH_PATTERN_SHAPE_VALUE_8X_8Y              0x00000000 /* RW--V */
#define NV_PGRAPH_PATTERN_SHAPE_VALUE_64X_1Y             0x00000001 /* RW--V */
#define NV_PGRAPH_PATTERN_SHAPE_VALUE_1X_64Y             0x00000002 /* RW--V */
#define NV_PGRAPH_PATTERN_SHAPE_SELECT                          4:4 /* RWXVF */
#define NV_PGRAPH_PATTERN_SHAPE_SELECT_2COLOR            0x00000000 /* RW--V */
#define NV_PGRAPH_PATTERN_SHAPE_SELECT_FULLCOLOR         0x00000001 /* RW--V */
#define NV_PGRAPH_MONO_COLOR0                            0x00400600 /* RW-4R */
#define NV_PGRAPH_MONO_COLOR0_VALUE                            31:0 /* RWXUF */
#define NV_PGRAPH_ROP3                                   0x00400604 /* RW-4R */
#define NV_PGRAPH_ROP3_VALUE                                    7:0 /* RWXVF */
#define NV_PGRAPH_CHROMA                                 0x00400814 /* RW-4R */
#define NV_PGRAPH_CHROMA_VALUE                                 31:0 /* RWXUF */
#define NV_PGRAPH_BETA_AND                               0x00400608 /* RW-4R */
#define NV_PGRAPH_BETA_AND_VALUE_FRACTION                     30:23 /* RWXUF */
#define NV_PGRAPH_BETA_PREMULT                           0x0040060c /* RW-4R */
#define NV_PGRAPH_BETA_PREMULT_VALUE                           31:0 /* RWXUF */
#define NV_PGRAPH_CONTROL0                               0x00400818 /* RW-4R */
#define NV_PGRAPH_CONTROL1                               0x0040081c /* RW-4R */
#define NV_PGRAPH_CONTROL2                               0x00400820 /* RW-4R */
#define NV_PGRAPH_BLEND                                  0x00400824 /* RW-4R */
#define NV_PGRAPH_DPRAM_INDEX                            0x00400828 /* RW-4R */
#define NV_PGRAPH_DPRAM_INDEX_ADRS                              6:0 /* RWIVF */
#define NV_PGRAPH_DPRAM_INDEX_ADRS_0                     0x00000000 /* RWI-V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT                           10:8 /* RWIVF */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_ADRS_0              0x00000000 /* RWI-V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_ADRS_1              0x00000001 /* RW--V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_DATA_0              0x00000002 /* RW--V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_DATA_1              0x00000003 /* RW--V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_WE_0                0x00000004 /* RW--V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_WE_1                0x00000005 /* RW--V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_ALPHA_0             0x00000006 /* RW--V */
#define NV_PGRAPH_DPRAM_INDEX_SELECT_ALPHA_1             0x00000007 /* RW--V */
#define NV_PGRAPH_DPRAM_DATA                             0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_DATA_VALUE                             31:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_ADRS_0                           0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_ADRS_0__ALIAS_1        NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_ADRS_0_VALUE                           19:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_ADRS_1                           0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_ADRS_1__ALIAS_1        NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_ADRS_1_VALUE                           19:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_DATA_0                           0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_DATA_0__ALIAS_1        NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_DATA_0_VALUE                           31:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_DATA_1                           0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_DATA_1__ALIAS_1        NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_DATA_1_VALUE                           31:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_WE_0                             0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_WE_0__ALIAS_1          NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_WE_0_VALUE                             23:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_WE_1                             0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_WE_1__ALIAS_1          NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_WE_1_VALUE                             23:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_ALPHA_0                          0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_ALPHA_0__ALIAS_1       NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_ALPHA_0_VALUE                          31:0 /* RWXVF */
#define NV_PGRAPH_DPRAM_ALPHA_1                          0x0040082c /* RW-4R */
#define NV_PGRAPH_DPRAM_ALPHA_1__ALIAS_1       NV_PGRAPH_DPRAM_DATA /*       */
#define NV_PGRAPH_DPRAM_ALPHA_1_VALUE                          31:0 /* RWXVF */
#define NV_PGRAPH_STORED_FMT                             0x00400830 /* RW-4R */
#define NV_PGRAPH_STORED_FMT_MONO0                              5:0 /* RWXVF */
#define NV_PGRAPH_STORED_FMT_PATT0                             13:8 /* RWXVF */
#define NV_PGRAPH_STORED_FMT_PATT1                            21:16 /* RWXVF */
#define NV_PGRAPH_STORED_FMT_CHROMA                           29:24 /* RWXVF */
#define NV_PGRAPH_FORMATS                                0x00400618 /* RW-4R */
#define NV_PGRAPH_FORMATS_ROP                                   2:0 /* R-XVF */
#define NV_PGRAPH_FORMATS_ROP_Y8                         0x00000000 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_RGB15                      0x00000001 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_RGB16                      0x00000002 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_Y16                        0x00000003 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_INVALID                    0x00000004 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_RGB24                      0x00000005 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_RGB30                      0x00000006 /* -W--V */
#define NV_PGRAPH_FORMATS_ROP_Y32                        0x00000007 /* -W--V */
#define NV_PGRAPH_FORMATS_SRC                                   9:4 /* R-XVF */
#define NV_PGRAPH_FORMATS_SRC_INVALID                    0x00000000 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_Y8                      0x00000001 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X16A8Y8                 0x00000002 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X24Y8                   0x00000003 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_A1R5G5B5                0x00000006 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X1R5G5B5                0x00000007 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X16A1R5G5B5             0x00000008 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X17R5G5B5               0x00000009 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_R5G6B5                  0x0000000A /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_A16R5G6B5               0x0000000B /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X16R5G6B5               0x0000000C /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_A8R8G8B8                0x0000000D /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X8R8G8B8                0x0000000E /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_Y16                     0x0000000F /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_A16Y16                  0x00000010 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_X16Y16                  0x00000011 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_V8YB8U8YA8              0x00000012 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_YB8V8YA8U8              0x00000013 /* RW--V */
#define NV_PGRAPH_FORMATS_SRC_LE_Y32                     0x00000014 /* RW--V */
#define NV_PGRAPH_FORMATS_FB                                  15:12 /* R-XVF */
#define NV_PGRAPH_FORMATS_FB_INVALID                     0x00000000 /* RWI-V */
#define NV_PGRAPH_FORMATS_FB_Y8                          0x00000001 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X1R5G5B5_Z1R5G5B5           0x00000002 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X1R5G5B5_O1R5G5B5           0x00000003 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_A1R5G5B5                    0x00000004 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_R5G6B5                      0x00000005 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_Y16                         0x00000006 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X8R8G8B8_Z8R8G8B8           0x00000007 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X8R8G8B8_O1Z7R8G8B8         0x00000008 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X1A7R8G8B8_Z1A7R8G8B8       0x00000009 /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X1A7R8G8B8_O1A7R8G8B8       0x0000000a /* RW--V */
#define NV_PGRAPH_FORMATS_FB_X8R8G8B8_O8R8G8B8           0x0000000b /* RW--V */
#define NV_PGRAPH_FORMATS_FB_A8R8G8B8                    0x0000000c /* RW--V */
#define NV_PGRAPH_FORMATS_FB_Y32                         0x0000000d /* RW--V */
#define NV_PGRAPH_FORMATS_FB_V8YB8U8YA8                  0x0000000e /* RW--V */
#define NV_PGRAPH_FORMATS_FB_YB8V8YA8U8                  0x0000000f /* RW--V */ 
#define NV_PGRAPH_ABS_X_RAM(i)                   (0x00400400+(i)*4) /* RW-4A */
#define NV_PGRAPH_ABS_X_RAM__SIZE_1                              32 /*       */
#define NV_PGRAPH_ABS_X_RAM_VALUE                              31:0 /* RWXUF */
#define NV_PGRAPH_X_RAM_BPORT(i)                 (0x00400c00+(i)*4) /* R--4A */
#define NV_PGRAPH_X_RAM_BPORT__SIZE_1                            32 /*       */
#define NV_PGRAPH_X_RAM_BPORT_VALUE                            31:0 /* R--UF */
#define NV_PGRAPH_ABS_Y_RAM(i)                   (0x00400480+(i)*4) /* RW-4A */
#define NV_PGRAPH_ABS_Y_RAM__SIZE_1                              32 /*       */
#define NV_PGRAPH_ABS_Y_RAM_VALUE                              31:0 /* RWXUF */
#define NV_PGRAPH_Y_RAM_BPORT(i)                 (0x00400c80+(i)*4) /* R--4A */
#define NV_PGRAPH_Y_RAM_BPORT__SIZE_1                            32 /*       */
#define NV_PGRAPH_Y_RAM_BPORT_VALUE                            31:0 /* R--UF */
#define NV_PGRAPH_XY_LOGIC_MISC0                         0x00400514 /* RW-4R */
#define NV_PGRAPH_XY_LOGIC_MISC0_COUNTER                       17:0 /* RWBUF */
#define NV_PGRAPH_XY_LOGIC_MISC0_COUNTER_0               0x00000000 /* RWB-V */
#define NV_PGRAPH_XY_LOGIC_MISC0_DIMENSION                    20:20 /* RWVVF */
#define NV_PGRAPH_XY_LOGIC_MISC0_DIMENSION_NONZERO       0x00000000 /* RWV-V */
#define NV_PGRAPH_XY_LOGIC_MISC0_DIMENSION_ZERO          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC0_INDEX                        31:28 /* RWBUF */
#define NV_PGRAPH_XY_LOGIC_MISC0_INDEX_0                 0x00000000 /* RWB-V */
#define NV_PGRAPH_XY_LOGIC_MISC1                         0x00400518 /* RW-4R */
#define NV_PGRAPH_XY_LOGIC_MISC1_INITIAL                        0:0 /* RWNVF */
#define NV_PGRAPH_XY_LOGIC_MISC1_INITIAL_NEEDED          0x00000000 /* RWN-V */
#define NV_PGRAPH_XY_LOGIC_MISC1_INITIAL_DONE            0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC1_XTRACLIPX                      4:4 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC1_XTRACLIPX_NOTNULL       0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC1_XTRACLIPX_NULL          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC1_XTRACLIPY                      5:5 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC1_XTRACLIPY_NOTNULL       0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC1_XTRACLIPY_NULL          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_XIMAX                    12:12 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_XIMAX_UUMAX         0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_XIMAX_IMAGEMAX      0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_YIMAX                    16:16 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_YIMAX_UUMAX         0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_YIMAX_IMAGEMAX      0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_XXTRA                    20:20 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_XXTRA_CLIPMAX       0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC1_SEL_XXTRA_IMAGEMAX      0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC2                         0x0040051C /* RW-4R */
#define NV_PGRAPH_XY_LOGIC_MISC2_HANDOFF                        0:0 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC2_HANDOFF_DISABLE         0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC2_HANDOFF_ENABLE          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC2_XTRACLIPX                      4:4 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC2_XTRACLIPX_NOTNULL       0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC2_XTRACLIPX_NULL          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC2_XTRACLIPY                      5:5 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC2_XTRACLIPY_NOTNULL       0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC2_XTRACLIPY_NULL          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_XIMAX                    12:12 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_XIMAX_UCMAX         0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_XIMAX_IMAGEMAX      0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_YIMAX                    16:16 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_YIMAX_UCMAX         0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_YIMAX_IMAGEMAX      0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_XXTRA                    20:20 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_XXTRA_CLIPMAX       0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC2_SEL_XXTRA_IMAGEMAX      0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3                         0x00400520 /* RW-4R */
#define NV_PGRAPH_XY_LOGIC_MISC3_WDIMY_EQ_0                     0:0 /* RWXVF */
#define NV_PGRAPH_XY_LOGIC_MISC3_WDIMY_EQ_0_NULL         0x00000000 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_WDIMY_EQ_0_TRUE         0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_RELOAD_WDIMY                   4:4 /* RWXVF */
#define NV_PGRAPH_XY_LOGIC_MISC3_RELOAD_WDIMY_NULL       0x00000000 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_RELOAD_WDIMY_TRUE       0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_RELOAD_WX                      8:8 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC3_RELOAD_WX_NULL          0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC3_RELOAD_WX_TRUE          0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_ALG                     12:12 /* RWIVF */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_ALG_NULL           0x00000000 /* RWI-V */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_ALG_TRUE           0x00000001 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_DIMX                    22:16 /* RWXUF */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_DIMX_0             0x00000000 /* RW--V */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_WDIMX                   30:24 /* RWXUF */
#define NV_PGRAPH_XY_LOGIC_MISC3_TEXT_WDIMX_0            0x00000000 /* RW--V */
#define NV_PGRAPH_X_MISC                                 0x00400500 /* RW-4R */
#define NV_PGRAPH_X_MISC_BIT33_0                                0:0 /* RWNVF */
#define NV_PGRAPH_X_MISC_BIT33_0_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_BIT33_1                                1:1 /* RWNVF */
#define NV_PGRAPH_X_MISC_BIT33_1_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_BIT33_2                                2:2 /* RWNVF */
#define NV_PGRAPH_X_MISC_BIT33_2_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_BIT33_3                                3:3 /* RWNVF */
#define NV_PGRAPH_X_MISC_BIT33_3_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_RANGE_0                                4:4 /* RWNVF */
#define NV_PGRAPH_X_MISC_RANGE_0_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_RANGE_1                                5:5 /* RWNVF */
#define NV_PGRAPH_X_MISC_RANGE_1_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_RANGE_2                                6:6 /* RWNVF */
#define NV_PGRAPH_X_MISC_RANGE_2_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_RANGE_3                                7:7 /* RWNVF */
#define NV_PGRAPH_X_MISC_RANGE_3_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_X_MISC_ADDER_OUTPUT                         29:28 /* RWXVF */
#define NV_PGRAPH_X_MISC_ADDER_OUTPUT_EQ_0               0x00000000 /* RW--V */
#define NV_PGRAPH_X_MISC_ADDER_OUTPUT_LT_0               0x00000001 /* RW--V */
#define NV_PGRAPH_X_MISC_ADDER_OUTPUT_GT_0               0x00000002 /* RW--V */
#define NV_PGRAPH_Y_MISC                                 0x00400504 /* RW-4R */
#define NV_PGRAPH_Y_MISC_BIT33_0                                0:0 /* RWNVF */
#define NV_PGRAPH_Y_MISC_BIT33_0_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_BIT33_1                                1:1 /* RWNVF */
#define NV_PGRAPH_Y_MISC_BIT33_1_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_BIT33_2                                2:2 /* RWNVF */
#define NV_PGRAPH_Y_MISC_BIT33_2_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_BIT33_3                                3:3 /* RWNVF */
#define NV_PGRAPH_Y_MISC_BIT33_3_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_RANGE_0                                4:4 /* RWNVF */
#define NV_PGRAPH_Y_MISC_RANGE_0_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_RANGE_1                                5:5 /* RWNVF */
#define NV_PGRAPH_Y_MISC_RANGE_1_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_RANGE_2                                6:6 /* RWNVF */
#define NV_PGRAPH_Y_MISC_RANGE_2_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_RANGE_3                                7:7 /* RWNVF */
#define NV_PGRAPH_Y_MISC_RANGE_3_0                       0x00000000 /* RWN-V */
#define NV_PGRAPH_Y_MISC_ADDER_OUTPUT                         29:28 /* RWXVF */
#define NV_PGRAPH_Y_MISC_ADDER_OUTPUT_EQ_0               0x00000000 /* RW--V */
#define NV_PGRAPH_Y_MISC_ADDER_OUTPUT_LT_0               0x00000001 /* RW--V */
#define NV_PGRAPH_Y_MISC_ADDER_OUTPUT_GT_0               0x00000002 /* RW--V */
#define NV_PGRAPH_ABS_UCLIP_XMIN                         0x0040053C /* RW-4R */
#define NV_PGRAPH_ABS_UCLIP_XMIN_VALUE                         15:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIP_XMAX                         0x00400544 /* RW-4R */
#define NV_PGRAPH_ABS_UCLIP_XMAX_VALUE                         17:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIP_YMIN                         0x00400540 /* RW-4R */
#define NV_PGRAPH_ABS_UCLIP_YMIN_VALUE                         15:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIP_YMAX                         0x00400548 /* RW-4R */
#define NV_PGRAPH_ABS_UCLIP_YMAX_VALUE                         17:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIPA_XMIN                        0x00400560 /* RW-4R */
#define NV_PGRAPH_ABS_UCLIPA_XMIN_VALUE                        15:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIPA_XMAX                        0x00400568 /* RW-4R */
#define NV_PGRAPH_ABS_UCLIPA_XMAX_VALUE                        17:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIPA_YMIN                        0x00400564 /* RW-4R */
#define NV_PGRAPH_ABS_UCLIPA_YMIN_VALUE                        15:0 /* RWXSF */
#define NV_PGRAPH_ABS_UCLIPA_YMAX                        0x0040056C /* RW-4R */
#define NV_PGRAPH_ABS_UCLIPA_YMAX_VALUE                        17:0 /* RWXSF */
#define NV_PGRAPH_SOURCE_COLOR                           0x0040050C /* RW-4R */
#define NV_PGRAPH_SOURCE_COLOR_VALUE                           31:0 /* RWNVF */
#define NV_PGRAPH_SOURCE_COLOR_VALUE_0                   0x00000000 /* RWN-V */
#define NV_PGRAPH_VALID1                                 0x00400508 /* RW-4R */
#define NV_PGRAPH_VALID1_VLD                                   22:0 /* RWNVF */
#define NV_PGRAPH_VALID1_VLD_0                           0x00000000 /* RWN-V */
#define NV_PGRAPH_VALID1_CLIP_MIN                             28:28 /* RWIVF */
#define NV_PGRAPH_VALID1_CLIP_MIN_NO_ERROR               0x00000000 /* RWI-V */
#define NV_PGRAPH_VALID1_CLIP_MIN_ONLY                   0x00000001 /* RW--V */
#define NV_PGRAPH_VALID1_CLIPA_MIN                            29:29 /* RWIVF */
#define NV_PGRAPH_VALID1_CLIPA_MIN_NO_ERROR              0x00000000 /* RWI-V */
#define NV_PGRAPH_VALID1_CLIPA_MIN_ONLY                  0x00000001 /* RW--V */
#define NV_PGRAPH_VALID1_CLIP_MAX                             30:30 /* RWIVF */
#define NV_PGRAPH_VALID1_CLIP_MAX_NO_ERROR               0x00000000 /* RWI-V */
#define NV_PGRAPH_VALID1_CLIP_MAX_ONLY                   0x00000001 /* RW--V */
#define NV_PGRAPH_VALID1_CLIPA_MAX                            31:31 /* RWIVF */
#define NV_PGRAPH_VALID1_CLIPA_MAX_NO_ERROR              0x00000000 /* RWI-V */
#define NV_PGRAPH_VALID1_CLIPA_MAX_ONLY                  0x00000001 /* RW--V */
#define NV_PGRAPH_VALID2                                 0x00400578 /* RW-4R */
#define NV_PGRAPH_VALID2_VLD2                                  28:0 /* RWNVF */
#define NV_PGRAPH_VALID2_VLD2_0                          0x00000000 /* RWN-V */
#define NV_PGRAPH_ABS_ICLIP_XMAX                         0x00400534 /* RW-4R */
#define NV_PGRAPH_ABS_ICLIP_XMAX_VALUE                         17:0 /* RWXSF */
#define NV_PGRAPH_ABS_ICLIP_YMAX                         0x00400538 /* RW-4R */
#define NV_PGRAPH_ABS_ICLIP_YMAX_VALUE                         17:0 /* RWXSF */
#define NV_PGRAPH_CLIPX_0                                0x00400524 /* RW-4R */
#define NV_PGRAPH_CLIPX_0_CLIP0_MIN                             1:0 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP0_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP0_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP0_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP0_MAX                             3:2 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP0_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP0_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP0_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP1_MIN                             5:4 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP1_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP1_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP1_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP1_MAX                             7:6 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP1_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP1_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP1_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP2_MIN                             9:8 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP2_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP2_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP2_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP2_MAX                           11:10 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP2_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP2_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP2_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP3_MIN                           13:12 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP3_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP3_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP3_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP3_MAX                           15:14 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP3_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP3_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP3_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP4_MIN                           17:16 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP4_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP4_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP4_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP4_MAX                           19:18 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP4_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP4_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP4_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP5_MIN                           21:20 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP5_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP5_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP5_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP5_MAX                           23:22 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP5_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP5_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP5_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP6_MIN                           25:24 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP6_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP6_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP6_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP6_MAX                           27:26 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP6_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP6_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP6_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP7_MIN                           29:28 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP7_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP7_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP7_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP7_MAX                           31:30 /* RWNVF */
#define NV_PGRAPH_CLIPX_0_CLIP7_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_0_CLIP7_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_0_CLIP7_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1                                0x00400528 /* RW-4R */
#define NV_PGRAPH_CLIPX_1_CLIP8_MIN                             1:0 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP8_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP8_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP8_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP8_MAX                             3:2 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP8_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP8_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP8_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP9_MIN                             5:4 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP9_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP9_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP9_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP9_MAX                             7:6 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP9_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP9_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP9_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP10_MIN                            9:8 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP10_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP10_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP10_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP10_MAX                          11:10 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP10_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP10_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP10_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP11_MIN                          13:12 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP11_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP11_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP11MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP11_MAX                          15:14 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP11_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP11_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP11_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP12_MIN                          17:16 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP12_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP12_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP12_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP12_MAX                          19:18 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP12_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP12_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP12_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP13_MIN                          21:20 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP13_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP13_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP13_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP13_MAX                          23:22 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP13_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP13_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP13_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP14_MIN                          25:24 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP14_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP14_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP14_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP14_MAX                          27:26 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP14_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP14_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP14_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP15_MIN                          29:28 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP15_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP15_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP15_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP15_MAX                          31:30 /* RWNVF */
#define NV_PGRAPH_CLIPX_1_CLIP15_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPX_1_CLIP15_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPX_1_CLIP15_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0                                0x0040052c /* RW-4R */
#define NV_PGRAPH_CLIPY_0_CLIP0_MIN                             1:0 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP0_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP0_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP0_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP0_MAX                             3:2 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP0_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP0_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP0_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP1_MIN                             5:4 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP1_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP1_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP1_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP1_MAX                             7:6 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP1_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP1_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP1_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP2_MIN                             9:8 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP2_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP2_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP2_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP2_MAX                           11:10 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP2_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP2_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP2_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP3_MIN                           13:12 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP3_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP3_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP3_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP3_MAX                           15:14 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP3_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP3_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP3_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP4_MIN                           17:16 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP4_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP4_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP4_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP4_MAX                           19:18 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP4_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP4_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP4_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP5_MIN                           21:20 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP5_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP5_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP5_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP5_MAX                           23:22 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP5_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP5_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP5_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP6_MIN                           25:24 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP6_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP6_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP6_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP6_MAX                           27:26 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP6_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP6_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP6_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP7_MIN                           29:28 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP7_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP7_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP7_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP7_MAX                           31:30 /* RWNVF */
#define NV_PGRAPH_CLIPY_0_CLIP7_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_0_CLIP7_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_0_CLIP7_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1                                0x00400530 /* RW-4R */
#define NV_PGRAPH_CLIPY_1_CLIP8_MIN                             1:0 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP8_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP8_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP8_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP8_MAX                             3:2 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP8_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP8_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP8_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP9_MIN                             5:4 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP9_MIN_GT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP9_MIN_LT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP9_MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP9_MAX                             7:6 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP9_MAX_LT                   0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP9_MAX_GT                   0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP9_MAX_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP10_MIN                            9:8 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP10_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP10_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP10_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP10_MAX                          11:10 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP10_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP10_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP10_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP11_MIN                          13:12 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP11_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP11_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP11MIN_EQ                   0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP11_MAX                          15:14 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP11_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP11_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP11_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP12_MIN                          17:16 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP12_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP12_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP12_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP12_MAX                          19:18 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP12_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP12_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP12_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP13_MIN                          21:20 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP13_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP13_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP13_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP13_MAX                          23:22 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP13_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP13_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP13_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP14_MIN                          25:24 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP14_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP14_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP14_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP14_MAX                          27:26 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP14_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP14_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP14_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP15_MIN                          29:28 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP15_MIN_GT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP15_MIN_LT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP15_MIN_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP15_MAX                          31:30 /* RWNVF */
#define NV_PGRAPH_CLIPY_1_CLIP15_MAX_LT                  0x00000000 /* RW--V */
#define NV_PGRAPH_CLIPY_1_CLIP15_MAX_GT                  0x00000001 /* RWN-V */
#define NV_PGRAPH_CLIPY_1_CLIP15_MAX_EQ                  0x00000002 /* RW--V */
#define NV_PGRAPH_MISC24_0                               0x00400510 /* RW-4R */
#define NV_PGRAPH_MISC24_0_VALUE                               23:0 /* RWXUF */
#define NV_PGRAPH_MISC24_1                               0x00400570 /* RW-4R */
#define NV_PGRAPH_MISC24_1_VALUE                               23:0 /* RWXUF */
#define NV_PGRAPH_MISC24_2                               0x00400574 /* RW-4R */
#define NV_PGRAPH_MISC24_2_VALUE                               23:0 /* RWXUF */
#define NV_PGRAPH_PASSTHRU_0                             0x0040057C /* RW-4R */
#define NV_PGRAPH_PASSTHRU_0_VALUE                             31:0 /* RWXUF */
#define NV_PGRAPH_PASSTHRU_1                             0x00400580 /* RW-4R */
#define NV_PGRAPH_PASSTHRU_1_VALUE                             31:0 /* RWXUF */
#define NV_PGRAPH_PASSTHRU_2                             0x00400584 /* RW-4R */
#define NV_PGRAPH_PASSTHRU_2_VALUE                             31:0 /* RWXUF */
#define NV_PGRAPH_U_RAM(i)                       (0x00400d00+(i)*4) /* RW-4A */
#define NV_PGRAPH_U_RAM__SIZE_1                                  16 /*       */
#define NV_PGRAPH_U_RAM_VALUE                                  31:6 /* RWXFF */
#define NV_PGRAPH_V_RAM(i)                       (0x00400d40+(i)*4) /* RW-4A */
#define NV_PGRAPH_V_RAM__SIZE_1                                  16 /*       */
#define NV_PGRAPH_V_RAM_VALUE                                  31:6 /* RWXFF */
#define NV_PGRAPH_M_RAM(i)                       (0x00400d80+(i)*4) /* RW-4A */
#define NV_PGRAPH_M_RAM__SIZE_1                                  16 /*       */
#define NV_PGRAPH_M_RAM_VALUE                                  31:6 /* RWXFF */
#define NV_PGRAPH_DMA_START_0                            0x00401000 /* RW-4R */
#define NV_PGRAPH_DMA_START_0_VALUE                            31:0 /* RWXUF */
#define NV_PGRAPH_DMA_START_1                            0x00401004 /* RW-4R */
#define NV_PGRAPH_DMA_START_1_VALUE                            31:0 /* RWXUF */
#define NV_PGRAPH_DMA_LENGTH                             0x00401008 /* RW-4R */
#define NV_PGRAPH_DMA_LENGTH_VALUE                             21:0 /* RWXUF */
#define NV_PGRAPH_DMA_MISC                               0x0040100C /* RW-4R */
#define NV_PGRAPH_DMA_MISC_COUNT                               15:0 /* RWXUF */
#define NV_PGRAPH_DMA_MISC_FMT_SRC                            18:16 /* RWXVF */
#define NV_PGRAPH_DMA_MISC_FMT_DST                            22:20 /* RWXVF */
#define NV_PGRAPH_DMA_DATA_0                             0x00401020 /* RW-4R */
#define NV_PGRAPH_DMA_DATA_0_VALUE                             31:0 /* RWXUF */
#define NV_PGRAPH_DMA_DATA_1                             0x00401024 /* RW-4R */
#define NV_PGRAPH_DMA_DATA_1_VALUE                             31:0 /* RWXUF */
#define NV_PGRAPH_DMA_RM                                 0x00401030 /* RW-4R */
#define NV_PGRAPH_DMA_RM_ASSIST_A                               0:0 /* RWIVF */
#define NV_PGRAPH_DMA_RM_ASSIST_A_NOT_PENDING            0x00000000 /* R-I-V */
#define NV_PGRAPH_DMA_RM_ASSIST_A_PENDING                0x00000001 /* R---V */
#define NV_PGRAPH_DMA_RM_ASSIST_A_RESET                  0x00000001 /* -W--C */
#define NV_PGRAPH_DMA_RM_ASSIST_B                               1:1 /* RWIVF */
#define NV_PGRAPH_DMA_RM_ASSIST_B_NOT_PENDING            0x00000000 /* R-I-V */
#define NV_PGRAPH_DMA_RM_ASSIST_B_PENDING                0x00000001 /* R---V */
#define NV_PGRAPH_DMA_RM_ASSIST_B_RESET                  0x00000001 /* -W--C */
#define NV_PGRAPH_DMA_RM_WRITE_REQ                              4:4 /* CWIVF */
#define NV_PGRAPH_DMA_RM_WRITE_REQ_NOT_PENDING           0x00000000 /* CWI-V */
#define NV_PGRAPH_DMA_RM_WRITE_REQ_PENDING               0x00000001 /* -W--T */
#define NV_PGRAPH_DMA_A_XLATE_INST                       0x00401040 /* RW-4R */
#define NV_PGRAPH_DMA_A_XLATE_INST_VALUE                       15:0 /* RWXUF */
#define NV_PGRAPH_DMA_A_CONTROL                          0x00401044 /* RW-4R */
#define NV_PGRAPH_DMA_A_CONTROL_PAGE_TABLE                    12:12 /* RWIVF */
#define NV_PGRAPH_DMA_A_CONTROL_PAGE_TABLE_NOT_PRESENT   0x00000000 /* RWI-V */
#define NV_PGRAPH_DMA_A_CONTROL_PAGE_TABLE_PRESENT       0x00000001 /* RW--V */
#define NV_PGRAPH_DMA_A_CONTROL_PAGE_ENTRY                    13:13 /* RWXVF */
#define NV_PGRAPH_DMA_A_CONTROL_PAGE_ENTRY_NOT_LINEAR    0x00000000 /* RW--V */
#define NV_PGRAPH_DMA_A_CONTROL_PAGE_ENTRY_LINEAR        0x00000001 /* RW--V */
#define NV_PGRAPH_DMA_A_CONTROL_TARGET_NODE                   17:16 /* RWXUF */
#define NV_PGRAPH_DMA_A_CONTROL_TARGET_NODE_NVM          0x00000000 /* RW--V */
#define NV_PGRAPH_DMA_A_CONTROL_TARGET_NODE_PCI          0x00000002 /* RW--V */
#define NV_PGRAPH_DMA_A_CONTROL_TARGET_NODE_AGP          0x00000003 /* RW--V */
#define NV_PGRAPH_DMA_A_CONTROL_ADJUST                        31:20 /* RWXUF */
#define NV_PGRAPH_DMA_A_LIMIT                            0x00401048 /* RW-4R */
#define NV_PGRAPH_DMA_A_LIMIT_OFFSET                           31:0 /* RWXUF */
#define NV_PGRAPH_DMA_A_TLB_PTE                          0x0040104C /* RW-4R */
#define NV_PGRAPH_DMA_A_TLB_PTE_ACCESS                          1:1 /* RWXVF */
#define NV_PGRAPH_DMA_A_TLB_PTE_ACCESS_READ_ONLY         0x00000000 /* RW--V */
#define NV_PGRAPH_DMA_A_TLB_PTE_ACCESS_READ_WRITE        0x00000001 /* RW--V */
#define NV_PGRAPH_DMA_A_TLB_PTE_FRAME_ADDRESS                 31:12 /* RWXUF */
#define NV_PGRAPH_DMA_A_TLB_TAG                          0x00401050 /* RW-4R */
#define NV_PGRAPH_DMA_A_TLB_TAG_ADDRESS                       31:12 /* RWXUF */
#define NV_PGRAPH_DMA_A_ADJ_OFFSET                       0x00401054 /* RW-4R */
#define NV_PGRAPH_DMA_A_ADJ_OFFSET_VALUE                       31:0 /* RWXUF */
#define NV_PGRAPH_DMA_A_OFFSET                           0x00401058 /* RW-4R */
#define NV_PGRAPH_DMA_A_OFFSET_VALUE                           31:0 /* RWXUF */
#define NV_PGRAPH_DMA_A_SIZE                             0x0040105C /* RW-4R */
#define NV_PGRAPH_DMA_A_SIZE_VALUE                             24:0 /* RWXUF */
#define NV_PGRAPH_DMA_A_Y_SIZE                           0x00401060 /* RW-4R */
#define NV_PGRAPH_DMA_A_Y_SIZE_VALUE                           10:0 /* RWXUF */
#define NV_PGRAPH_DMA_B_XLATE_INST                       0x00401080 /* RW-4R */
#define NV_PGRAPH_DMA_B_XLATE_INST_VALUE                       15:0 /* RWXUF */
#define NV_PGRAPH_DMA_B_CONTROL                          0x00401084 /* RW-4R */
#define NV_PGRAPH_DMA_B_CONTROL_PAGE_TABLE                    12:12 /* RWIVF */
#define NV_PGRAPH_DMA_B_CONTROL_PAGE_TABLE_NOT_PRESENT   0x00000000 /* RWI-V */
#define NV_PGRAPH_DMA_B_CONTROL_PAGE_TABLE_PRESENT       0x00000001 /* RW--V */
#define NV_PGRAPH_DMA_B_CONTROL_PAGE_ENTRY                    13:13 /* RWXVF */
#define NV_PGRAPH_DMA_B_CONTROL_PAGE_ENTRY_NOT_LINEAR    0x00000000 /* RW--V */
#define NV_PGRAPH_DMA_B_CONTROL_PAGE_ENTRY_LINEAR        0x00000001 /* RW--V */
#define NV_PGRAPH_DMA_B_CONTROL_TARGET_NODE                   17:16 /* RWXUF */
#define NV_PGRAPH_DMA_B_CONTROL_TARGET_NODE_NVM          0x00000000 /* RW--V */
#define NV_PGRAPH_DMA_B_CONTROL_TARGET_NODE_PCI          0x00000002 /* RW--V */
#define NV_PGRAPH_DMA_B_CONTROL_TARGET_NODE_AGP          0x00000003 /* RW--V */
#define NV_PGRAPH_DMA_B_CONTROL_ADJUST                        31:20 /* RWXUF */
#define NV_PGRAPH_DMA_B_LIMIT                            0x00401088 /* RW-4R */
#define NV_PGRAPH_DMA_B_LIMIT_OFFSET                           31:0 /* RWXUF */
#define NV_PGRAPH_DMA_B_TLB_PTE                          0x0040108C /* RW-4R */
#define NV_PGRAPH_DMA_B_TLB_PTE_ACCESS                          1:1 /* RWXVF */
#define NV_PGRAPH_DMA_B_TLB_PTE_ACCESS_READ_ONLY         0x00000000 /* RW--V */
#define NV_PGRAPH_DMA_B_TLB_PTE_ACCESS_READ_WRITE        0x00000001 /* RW--V */
#define NV_PGRAPH_DMA_B_TLB_PTE_FRAME_ADDRESS                 31:12 /* RWXUF */
#define NV_PGRAPH_DMA_B_TLB_TAG                          0x00401090 /* RW-4R */
#define NV_PGRAPH_DMA_B_TLB_TAG_ADDRESS                       31:12 /* RWXUF */
#define NV_PGRAPH_DMA_B_ADJ_OFFSET                       0x00401094 /* RW-4R */
#define NV_PGRAPH_DMA_B_ADJ_OFFSET_VALUE                       31:0 /* RWXUF */
#define NV_PGRAPH_DMA_B_OFFSET                           0x00401098 /* RW-4R */
#define NV_PGRAPH_DMA_B_OFFSET_VALUE                           31:0 /* RWXUF */
#define NV_PGRAPH_DMA_B_SIZE                             0x0040109C /* RW-4R */
#define NV_PGRAPH_DMA_B_SIZE_VALUE                             24:0 /* RWXUF */
#define NV_PGRAPH_DMA_B_Y_SIZE                           0x004010A0 /* RW-4R */
#define NV_PGRAPH_DMA_B_Y_SIZE_VALUE                           10:0 /* RWXUF */

/* Framebuffer registers */
#define NV_PFB                                0x00100FFF:0x00100000 /* RW--D */
#define NV_PFB_BOOT_0                                    0x00100000 /* RW-4R */
#define NV_PFB_BOOT_0_RAM_AMOUNT                                1:0 /* RW-VF */
#define NV_PFB_BOOT_0_RAM_AMOUNT_32MB                    0x00000000 /* RW--V */
#define NV_PFB_BOOT_0_RAM_AMOUNT_4MB                     0x00000001 /* RW--V */
#define NV_PFB_BOOT_0_RAM_AMOUNT_8MB                     0x00000002 /* RW--V */
#define NV_PFB_BOOT_0_RAM_AMOUNT_16MB                    0x00000003 /* RW--V */
#define NV_PFB_BOOT_0_RAM_WIDTH_128                             2:2 /* RW-VF */
#define NV_PFB_BOOT_0_RAM_WIDTH_128_OFF                  0x00000000 /* RW--V */
#define NV_PFB_BOOT_0_RAM_WIDTH_128_ON                   0x00000001 /* RW--V */
#define NV_PFB_BOOT_0_RAM_TYPE                                  4:3 /* RW-VF */
#define NV_PFB_BOOT_0_RAM_TYPE_256K                      0x00000000 /* RW--V */
#define NV_PFB_BOOT_0_RAM_TYPE_512K_2BANK                0x00000001 /* RW--V */
#define NV_PFB_BOOT_0_RAM_TYPE_512K_4BANK                0x00000002 /* RW--V */
#define NV_PFB_BOOT_0_RAM_TYPE_1024K_2BANK               0x00000003 /* RW--V */
#define NV_PFB_CONFIG_0                                  0x00100200 /* RW-4R */
#define NV_PFB_CONFIG_0_TYPE                                   14:0 /* RWIVF */
#define NV_PFB_CONFIG_0_TYPE_OLD1024_FIXED_8BPP          0x00000120 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_OLD1024_FIXED_16BPP         0x00000220 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_OLD1024_FIXED_32BPP         0x00000320 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_OLD1024_VAR_8BPP            0x00004120 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_OLD1024_VAR_16BPP           0x00004220 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_OLD1024_VAR_32BPP           0x00004320 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_TETRIS                      0x00002000 /* RW--V */
#define NV_PFB_CONFIG_0_TYPE_NOTILING                    0x00001114 /* RWI-V */
#define NV_PFB_CONFIG_0_TETRIS_MODE                           17:15 /* RWI-F */
#define NV_PFB_CONFIG_0_TETRIS_MODE_PASS                 0x00000000 /* RWI-V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_1                    0x00000001 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_2                    0x00000002 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_3                    0x00000003 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_4                    0x00000004 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_5                    0x00000005 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_6                    0x00000006 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_MODE_7                    0x00000007 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_SHIFT                          19:18 /* RWI-F */
#define NV_PFB_CONFIG_0_TETRIS_SHIFT_0                   0x00000000 /* RWI-V */
#define NV_PFB_CONFIG_0_TETRIS_SHIFT_1                   0x00000001 /* RW--V */
#define NV_PFB_CONFIG_0_TETRIS_SHIFT_2                   0x00000002 /* RW--V */
#define NV_PFB_CONFIG_0_BANK_SWAP                             22:20 /* RWI-F */
#define NV_PFB_CONFIG_0_BANK_SWAP_OFF                    0x00000000 /* RWI-V */
#define NV_PFB_CONFIG_0_BANK_SWAP_1M                     0x00000001 /* RW--V */
#define NV_PFB_CONFIG_0_BANK_SWAP_2M                     0x00000005 /* RW--V */
#define NV_PFB_CONFIG_0_BANK_SWAP_4M                     0x00000007 /* RW--V */
#define NV_PFB_CONFIG_0_UNUSED                                23:23 /* RW-VF */
#define NV_PFB_CONFIG_0_SCRAMBLE_EN                           29:29 /* RWIVF */
#define NV_PFB_CONFIG_0_SCRAMBLE_EN_INIT                 0x00000000 /* RW--V */
#define NV_PFB_CONFIG_0_SCRAMBLE_ACTIVE                  0x00000001 /* RW--V */
#define NV_PFB_CONFIG_0_PRAMIN_WR                             28:28 /* RWIVF */
#define NV_PFB_CONFIG_0_PRAMIN_WR_INIT                   0x00000000 /* RW--V */
#define NV_PFB_CONFIG_0_PRAMIN_WR_DISABLED               0x00000001 /* RW--V */
#define NV_PFB_CONFIG_0_PRAMIN_WR_MASK                        27:24 /* RWIVF */
#define NV_PFB_CONFIG_0_PRAMIN_WR_MASK_INIT              0x00000000 /* RWI-V */
#define NV_PFB_CONFIG_0_PRAMIN_WR_MASK_CLEAR             0x0000000f /* RWI-V */
#define NV_PFB_CONFIG_1                                  0x00100204 /* RW-4R */
#define NV_PFB_RTL                                       0x00100300 /* RW-4R */
#define NV_PFB_RTL_H                                            0:0 /* RWIUF */
#define NV_PFB_RTL_H_DEFAULT                             0x00000000 /* RWI-V */
#define NV_PFB_RTL_MC                                           1:1 /* RWIUF */
#define NV_PFB_RTL_MC_DEFAULT                            0x00000000 /* RWI-V */
#define NV_PFB_RTL_V                                            2:2 /* RWIUF */
#define NV_PFB_RTL_V_DEFAULT                             0x00000000 /* RWI-V */
#define NV_PFB_RTL_G                                            3:3 /* RWIUF */
#define NV_PFB_RTL_G_DEFAULT                             0x00000000 /* RWI-V */
#define NV_PFB_RTL_GB                                           4:4 /* RWIUF */
#define NV_PFB_RTL_GB_DEFAULT                            0x00000000 /* RWI-V */
#define NV_PFB_CONFIG_0_RESOLUTION                              5:0 /* RWIVF */
#define NV_PFB_CONFIG_0_RESOLUTION_320_PIXELS            0x0000000a /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_400_PIXELS            0x0000000d /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_480_PIXELS            0x0000000f /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_512_PIXELS            0x00000010 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_640_PIXELS            0x00000014 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_800_PIXELS            0x00000019 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_960_PIXELS            0x0000001e /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_1024_PIXELS           0x00000020 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_1152_PIXELS           0x00000024 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_1280_PIXELS           0x00000028 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_1600_PIXELS           0x00000032 /* RW--V */
#define NV_PFB_CONFIG_0_RESOLUTION_DEFAULT               0x00000014 /* RWI-V */
#define NV_PFB_CONFIG_0_PIXEL_DEPTH                             9:8 /* RWIVF */
#define NV_PFB_CONFIG_0_PIXEL_DEPTH_8_BITS               0x00000001 /* RW--V */
#define NV_PFB_CONFIG_0_PIXEL_DEPTH_16_BITS              0x00000002 /* RW--V */
#define NV_PFB_CONFIG_0_PIXEL_DEPTH_32_BITS              0x00000003 /* RW--V */
#define NV_PFB_CONFIG_0_PIXEL_DEPTH_DEFAULT              0x00000001 /* RWI-V */
#define NV_PFB_CONFIG_0_TILING                                12:12 /* RWIVF */
#define NV_PFB_CONFIG_0_TILING_ENABLED                   0x00000000 /* RW--V */
#define NV_PFB_CONFIG_0_TILING_DISABLED                  0x00000001 /* RWI-V */
#define NV_PFB_CONFIG_1_SGRAM100                                3:3 /* RWIVF */
#define NV_PFB_CONFIG_1_SGRAM100_ENABLED                 0x00000000 /* RWI-V */
#define NV_PFB_CONFIG_1_SGRAM100_DISABLED                0x00000001 /* RW--V */
#define NV_PFB_DEBUG_0_CKE_ALWAYSON                           29:29 /* RWIVF */
#define NV_PFB_DEBUG_0_CKE_ALWAYSON_OFF                  0x00000000 /* RW--V */
#define NV_PFB_DEBUG_0_CKE_ALWAYSON_ON                   0x00000001 /* RWI-V */

#define NV_PEXTDEV                            0x00101FFF:0x00101000 /* RW--D */
#define NV_PEXTDEV_BOOT_0                                0x00101000 /* R--4R */
#define NV_PEXTDEV_BOOT_0_STRAP_BUS_SPEED                       0:0 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_BUS_SPEED_33MHZ          0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_BUS_SPEED_66MHZ          0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_SUB_VENDOR                      1:1 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_SUB_VENDOR_NO_BIOS       0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_SUB_VENDOR_BIOS          0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_TYPE                        3:2 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_TYPE_SGRAM_256K      0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_TYPE_SGRAM_512K_2BANK 0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_TYPE_SGRAM_512K_4BANK 0x00000002 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_TYPE_1024K_2BANK     0x00000003 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_WIDTH                       4:4 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_WIDTH_64             0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_RAM_WIDTH_128            0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_BUS_TYPE                        5:5 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_BUS_TYPE_PCI             0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_BUS_TYPE_AGP             0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_CRYSTAL                         6:6 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_CRYSTAL_13500K           0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_CRYSTAL_14318180         0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_TVMODE                          8:7 /* R-XVF */
#define NV_PEXTDEV_BOOT_0_STRAP_TVMODE_SECAM             0x00000000 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_TVMODE_NTSC              0x00000001 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_TVMODE_PAL               0x00000002 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_TVMODE_DISABLED          0x00000003 /* R---V */
#define NV_PEXTDEV_BOOT_0_STRAP_OVERWRITE                     11:11 /* RWIVF */
#define NV_PEXTDEV_BOOT_0_STRAP_OVERWRITE_DISABLED       0x00000000 /* RWI-V */
#define NV_PEXTDEV_BOOT_0_STRAP_OVERWRITE_ENABLED        0x00000001 /* RW--V */

/* Extras */
#define NV_PRAMIN                             0x007FFFFF:0x00700000 /* RW--M */
/*#define NV_PRAMIN                             0x00FFFFFF:0x00C00000*/
#define NV_PNVM                               0x01FFFFFF:0x01000000 /* RW--M */
/*#define NV_PNVM                               0x00BFFFFF:0x00800000*/
#define NV_CHAN0                              0x0080ffff:0x00800000

/* FIFO subchannels */
#define NV_UROP                               0x43
#define NV_UCHROMA                            0x57
#define NV_UCLIP                              0x19
#define NV_UPATT                              0x18
#define NV_ULIN                               0x5C
#define NV_UTRI                               0x5D
#define NV_URECT                              0x5E
#define NV_UBLIT                              0x5F
#define NV_UGLYPH                             0x4B

#endif /*__NV4REF_H__*/

