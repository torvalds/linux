/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef __R600_REG_H__
#define __R600_REG_H__

#define R600_PCIE_PORT_INDEX                0x0038
#define R600_PCIE_PORT_DATA                 0x003c

#define R600_MC_VM_FB_LOCATION			0x2180
#define		R600_MC_FB_BASE_MASK			0x0000FFFF
#define		R600_MC_FB_BASE_SHIFT			0
#define		R600_MC_FB_TOP_MASK			0xFFFF0000
#define		R600_MC_FB_TOP_SHIFT			16
#define R600_MC_VM_AGP_TOP			0x2184
#define		R600_MC_AGP_TOP_MASK			0x0003FFFF
#define		R600_MC_AGP_TOP_SHIFT			0
#define R600_MC_VM_AGP_BOT			0x2188
#define		R600_MC_AGP_BOT_MASK			0x0003FFFF
#define		R600_MC_AGP_BOT_SHIFT			0
#define R600_MC_VM_AGP_BASE			0x218c
#define R600_MC_VM_SYSTEM_APERTURE_LOW_ADDR	0x2190
#define		R600_LOGICAL_PAGE_NUMBER_MASK		0x000FFFFF
#define		R600_LOGICAL_PAGE_NUMBER_SHIFT		0
#define R600_MC_VM_SYSTEM_APERTURE_HIGH_ADDR	0x2194
#define R600_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR	0x2198

#define R700_MC_VM_FB_LOCATION			0x2024
#define		R700_MC_FB_BASE_MASK			0x0000FFFF
#define		R700_MC_FB_BASE_SHIFT			0
#define		R700_MC_FB_TOP_MASK			0xFFFF0000
#define		R700_MC_FB_TOP_SHIFT			16
#define R700_MC_VM_AGP_TOP			0x2028
#define		R700_MC_AGP_TOP_MASK			0x0003FFFF
#define		R700_MC_AGP_TOP_SHIFT			0
#define R700_MC_VM_AGP_BOT			0x202c
#define		R700_MC_AGP_BOT_MASK			0x0003FFFF
#define		R700_MC_AGP_BOT_SHIFT			0
#define R700_MC_VM_AGP_BASE			0x2030
#define R700_MC_VM_SYSTEM_APERTURE_LOW_ADDR	0x2034
#define		R700_LOGICAL_PAGE_NUMBER_MASK		0x000FFFFF
#define		R700_LOGICAL_PAGE_NUMBER_SHIFT		0
#define R700_MC_VM_SYSTEM_APERTURE_HIGH_ADDR	0x2038
#define R700_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR	0x203c

#define R600_RAMCFG				       0x2408
#       define R600_CHANSIZE                           (1 << 7)
#       define R600_CHANSIZE_OVERRIDE                  (1 << 10)


#define R600_GENERAL_PWRMGT                                        0x618
#	define R600_OPEN_DRAIN_PADS				   (1 << 11)

#define R600_LOWER_GPIO_ENABLE                                     0x710
#define R600_CTXSW_VID_LOWER_GPIO_CNTL                             0x718
#define R600_HIGH_VID_LOWER_GPIO_CNTL                              0x71c
#define R600_MEDIUM_VID_LOWER_GPIO_CNTL                            0x720
#define R600_LOW_VID_LOWER_GPIO_CNTL                               0x724



#define R600_HDP_NONSURFACE_BASE                                0x2c04

#define R600_BUS_CNTL                                           0x5420
#define R600_CONFIG_CNTL                                        0x5424
#define R600_CONFIG_MEMSIZE                                     0x5428
#define R600_CONFIG_F0_BASE                                     0x542C
#define R600_CONFIG_APER_SIZE                                   0x5430

#define R600_ROM_CNTL                              0x1600
#       define R600_SCK_OVERWRITE                  (1 << 1)
#       define R600_SCK_PRESCALE_CRYSTAL_CLK_SHIFT 28
#       define R600_SCK_PRESCALE_CRYSTAL_CLK_MASK  (0xf << 28)

#define R600_CG_SPLL_FUNC_CNTL                     0x600
#       define R600_SPLL_BYPASS_EN                 (1 << 3)
#define R600_CG_SPLL_STATUS                        0x60c
#       define R600_SPLL_CHG_STATUS                (1 << 1)

#define R600_BIOS_0_SCRATCH               0x1724
#define R600_BIOS_1_SCRATCH               0x1728
#define R600_BIOS_2_SCRATCH               0x172c
#define R600_BIOS_3_SCRATCH               0x1730
#define R600_BIOS_4_SCRATCH               0x1734
#define R600_BIOS_5_SCRATCH               0x1738
#define R600_BIOS_6_SCRATCH               0x173c
#define R600_BIOS_7_SCRATCH               0x1740

/* Audio, these regs were reverse enginered,
 * so the chance is high that the naming is wrong
 * R6xx+ ??? */

/* Audio clocks */
#define R600_AUDIO_PLL1_MUL               0x0514
#define R600_AUDIO_PLL1_DIV               0x0518
#define R600_AUDIO_PLL2_MUL               0x0524
#define R600_AUDIO_PLL2_DIV               0x0528
#define R600_AUDIO_CLK_SRCSEL             0x0534

/* Audio general */
#define R600_AUDIO_ENABLE                 0x7300
#define R600_AUDIO_TIMING                 0x7344

/* Audio params */
#define R600_AUDIO_VENDOR_ID              0x7380
#define R600_AUDIO_REVISION_ID            0x7384
#define R600_AUDIO_ROOT_NODE_COUNT        0x7388
#define R600_AUDIO_NID1_NODE_COUNT        0x738c
#define R600_AUDIO_NID1_TYPE              0x7390
#define R600_AUDIO_SUPPORTED_SIZE_RATE    0x7394
#define R600_AUDIO_SUPPORTED_CODEC        0x7398
#define R600_AUDIO_SUPPORTED_POWER_STATES 0x739c
#define R600_AUDIO_NID2_CAPS              0x73a0
#define R600_AUDIO_NID3_CAPS              0x73a4
#define R600_AUDIO_NID3_PIN_CAPS          0x73a8

/* Audio conn list */
#define R600_AUDIO_CONN_LIST_LEN          0x73ac
#define R600_AUDIO_CONN_LIST              0x73b0

/* Audio verbs */
#define R600_AUDIO_RATE_BPS_CHANNEL       0x73c0
#define R600_AUDIO_PLAYING                0x73c4
#define R600_AUDIO_IMPLEMENTATION_ID      0x73c8
#define R600_AUDIO_CONFIG_DEFAULT         0x73cc
#define R600_AUDIO_PIN_SENSE              0x73d0
#define R600_AUDIO_PIN_WIDGET_CNTL        0x73d4
#define R600_AUDIO_STATUS_BITS            0x73d8

/* HDMI base register addresses */
#define R600_HDMI_BLOCK1                  0x7400
#define R600_HDMI_BLOCK2                  0x7700
#define R600_HDMI_BLOCK3                  0x7800

/* HDMI registers */
#define R600_HDMI_ENABLE           0x00
#define R600_HDMI_STATUS           0x04
#define R600_HDMI_CNTL             0x08
#define R600_HDMI_UNKNOWN_0        0x0C
#define R600_HDMI_AUDIOCNTL        0x10
#define R600_HDMI_VIDEOCNTL        0x14
#define R600_HDMI_VERSION          0x18
#define R600_HDMI_UNKNOWN_1        0x28
#define R600_HDMI_VIDEOINFOFRAME_0 0x54
#define R600_HDMI_VIDEOINFOFRAME_1 0x58
#define R600_HDMI_VIDEOINFOFRAME_2 0x5c
#define R600_HDMI_VIDEOINFOFRAME_3 0x60
#define R600_HDMI_32kHz_CTS        0xac
#define R600_HDMI_32kHz_N          0xb0
#define R600_HDMI_44_1kHz_CTS      0xb4
#define R600_HDMI_44_1kHz_N        0xb8
#define R600_HDMI_48kHz_CTS        0xbc
#define R600_HDMI_48kHz_N          0xc0
#define R600_HDMI_AUDIOINFOFRAME_0 0xcc
#define R600_HDMI_AUDIOINFOFRAME_1 0xd0
#define R600_HDMI_IEC60958_1       0xd4
#define R600_HDMI_IEC60958_2       0xd8
#define R600_HDMI_UNKNOWN_2        0xdc
#define R600_HDMI_AUDIO_DEBUG_0    0xe0
#define R600_HDMI_AUDIO_DEBUG_1    0xe4
#define R600_HDMI_AUDIO_DEBUG_2    0xe8
#define R600_HDMI_AUDIO_DEBUG_3    0xec

/* HDMI additional config base register addresses */
#define R600_HDMI_CONFIG1                 0x7600
#define R600_HDMI_CONFIG2                 0x7a00

#endif
