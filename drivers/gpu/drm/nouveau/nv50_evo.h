/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NV50_EVO_H__
#define __NV50_EVO_H__

#define NV50_EVO_UPDATE                                              0x00000080
#define NV50_EVO_UNK84                                               0x00000084
#define NV50_EVO_UNK84_NOTIFY                                        0x40000000
#define NV50_EVO_UNK84_NOTIFY_DISABLED                               0x00000000
#define NV50_EVO_UNK84_NOTIFY_ENABLED                                0x40000000
#define NV50_EVO_DMA_NOTIFY                                          0x00000088
#define NV50_EVO_DMA_NOTIFY_HANDLE                                   0xffffffff
#define NV50_EVO_DMA_NOTIFY_HANDLE_NONE                              0x00000000
#define NV50_EVO_UNK8C                                               0x0000008C

#define NV50_EVO_DAC(n, r)                       ((n) * 0x80 + NV50_EVO_DAC_##r)
#define NV50_EVO_DAC_MODE_CTRL                                       0x00000400
#define NV50_EVO_DAC_MODE_CTRL_CRTC0                                 0x00000001
#define NV50_EVO_DAC_MODE_CTRL_CRTC1                                 0x00000002
#define NV50_EVO_DAC_MODE_CTRL2                                      0x00000404
#define NV50_EVO_DAC_MODE_CTRL2_NHSYNC                               0x00000001
#define NV50_EVO_DAC_MODE_CTRL2_NVSYNC                               0x00000002

#define NV50_EVO_SOR(n, r)                       ((n) * 0x40 + NV50_EVO_SOR_##r)
#define NV50_EVO_SOR_MODE_CTRL                                       0x00000600
#define NV50_EVO_SOR_MODE_CTRL_CRTC0                                 0x00000001
#define NV50_EVO_SOR_MODE_CTRL_CRTC1                                 0x00000002
#define NV50_EVO_SOR_MODE_CTRL_TMDS                                  0x00000100
#define NV50_EVO_SOR_MODE_CTRL_TMDS_DUAL_LINK                        0x00000400
#define NV50_EVO_SOR_MODE_CTRL_NHSYNC                                0x00001000
#define NV50_EVO_SOR_MODE_CTRL_NVSYNC                                0x00002000

#define NV50_EVO_CRTC(n, r)                    ((n) * 0x400 + NV50_EVO_CRTC_##r)
#define NV84_EVO_CRTC(n, r)                    ((n) * 0x400 + NV84_EVO_CRTC_##r)
#define NV50_EVO_CRTC_UNK0800                                        0x00000800
#define NV50_EVO_CRTC_CLOCK                                          0x00000804
#define NV50_EVO_CRTC_INTERLACE                                      0x00000808
#define NV50_EVO_CRTC_DISPLAY_START                                  0x00000810
#define NV50_EVO_CRTC_DISPLAY_TOTAL                                  0x00000814
#define NV50_EVO_CRTC_SYNC_DURATION                                  0x00000818
#define NV50_EVO_CRTC_SYNC_START_TO_BLANK_END                        0x0000081c
#define NV50_EVO_CRTC_UNK0820                                        0x00000820
#define NV50_EVO_CRTC_UNK0824                                        0x00000824
#define NV50_EVO_CRTC_UNK082C                                        0x0000082c
#define NV50_EVO_CRTC_CLUT_MODE                                      0x00000840
/* You can't have a palette in 8 bit mode (=OFF) */
#define NV50_EVO_CRTC_CLUT_MODE_BLANK                                0x00000000
#define NV50_EVO_CRTC_CLUT_MODE_OFF                                  0x80000000
#define NV50_EVO_CRTC_CLUT_MODE_ON                                   0xC0000000
#define NV50_EVO_CRTC_CLUT_OFFSET                                    0x00000844
#define NV84_EVO_CRTC_CLUT_DMA                                       0x0000085C
#define NV84_EVO_CRTC_CLUT_DMA_HANDLE                                0xffffffff
#define NV84_EVO_CRTC_CLUT_DMA_HANDLE_NONE                           0x00000000
#define NV50_EVO_CRTC_FB_OFFSET                                      0x00000860
#define NV50_EVO_CRTC_FB_SIZE                                        0x00000868
#define NV50_EVO_CRTC_FB_CONFIG                                      0x0000086c
#define NV50_EVO_CRTC_FB_CONFIG_MODE                                 0x00100000
#define NV50_EVO_CRTC_FB_CONFIG_MODE_TILE                            0x00000000
#define NV50_EVO_CRTC_FB_CONFIG_MODE_PITCH                           0x00100000
#define NV50_EVO_CRTC_FB_DEPTH                                       0x00000870
#define NV50_EVO_CRTC_FB_DEPTH_8                                     0x00001e00
#define NV50_EVO_CRTC_FB_DEPTH_15                                    0x0000e900
#define NV50_EVO_CRTC_FB_DEPTH_16                                    0x0000e800
#define NV50_EVO_CRTC_FB_DEPTH_24                                    0x0000cf00
#define NV50_EVO_CRTC_FB_DEPTH_30                                    0x0000d100
#define NV50_EVO_CRTC_FB_DMA                                         0x00000874
#define NV50_EVO_CRTC_FB_DMA_HANDLE                                  0xffffffff
#define NV50_EVO_CRTC_FB_DMA_HANDLE_NONE                             0x00000000
#define NV50_EVO_CRTC_CURSOR_CTRL                                    0x00000880
#define NV50_EVO_CRTC_CURSOR_CTRL_HIDE                               0x05000000
#define NV50_EVO_CRTC_CURSOR_CTRL_SHOW                               0x85000000
#define NV50_EVO_CRTC_CURSOR_OFFSET                                  0x00000884
#define NV84_EVO_CRTC_CURSOR_DMA                                     0x0000089c
#define NV84_EVO_CRTC_CURSOR_DMA_HANDLE                              0xffffffff
#define NV84_EVO_CRTC_CURSOR_DMA_HANDLE_NONE                         0x00000000
#define NV50_EVO_CRTC_DITHER_CTRL                                    0x000008a0
#define NV50_EVO_CRTC_DITHER_CTRL_OFF                                0x00000000
#define NV50_EVO_CRTC_DITHER_CTRL_ON                                 0x00000011
#define NV50_EVO_CRTC_SCALE_CTRL                                     0x000008a4
#define NV50_EVO_CRTC_SCALE_CTRL_INACTIVE                            0x00000000
#define NV50_EVO_CRTC_SCALE_CTRL_ACTIVE                              0x00000009
#define NV50_EVO_CRTC_COLOR_CTRL                                     0x000008a8
#define NV50_EVO_CRTC_COLOR_CTRL_VIBRANCE                            0x000fff00
#define NV50_EVO_CRTC_COLOR_CTRL_HUE                                 0xfff00000
#define NV50_EVO_CRTC_FB_POS                                         0x000008c0
#define NV50_EVO_CRTC_REAL_RES                                       0x000008c8
#define NV50_EVO_CRTC_SCALE_CENTER_OFFSET                            0x000008d4
#define NV50_EVO_CRTC_SCALE_CENTER_OFFSET_VAL(x, y) \
	((((unsigned)y << 16) & 0xFFFF0000) | (((unsigned)x) & 0x0000FFFF))
/* Both of these are needed, otherwise nothing happens. */
#define NV50_EVO_CRTC_SCALE_RES1                                     0x000008d8
#define NV50_EVO_CRTC_SCALE_RES2                                     0x000008dc
#define NV50_EVO_CRTC_UNK900                                         0x00000900
#define NV50_EVO_CRTC_UNK904                                         0x00000904

#endif
