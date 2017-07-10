/*************************************************************************/ /*!
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef _OUT_DRV_H_
#define _OUT_DRV_H_

/*
	Register PVR_PDP_GRPH1SURF
*/
#define PVR5__PDP_PVR_PDP_GRPH1SURF               0x0000
#define PVR5__GRPH1USEHQCD_MASK                   0x00400000U
#define PVR5__GRPH1USEHQCD_SHIFT                  22
#define PVR5__GRPH1USEHQCD_SIGNED                 0

#define PVR5__GRPH1USELUT_MASK                    0x00800000U
#define PVR5__GRPH1USELUT_SHIFT                   23
#define PVR5__GRPH1USELUT_SIGNED                  0

#define PVR5__GRPH1LUTRWCHOICE_MASK               0x01000000U
#define PVR5__GRPH1LUTRWCHOICE_SHIFT              24
#define PVR5__GRPH1LUTRWCHOICE_SIGNED             0

#define PVR5__GRPH1USECSC_MASK                    0x02000000U
#define PVR5__GRPH1USECSC_SHIFT                   25
#define PVR5__GRPH1USECSC_SIGNED                  0

#define PVR5__GRPH1USEGAMMA_MASK                  0x04000000U
#define PVR5__GRPH1USEGAMMA_SHIFT                 26
#define PVR5__GRPH1USEGAMMA_SIGNED                0

#define PVR5__GRPH1PIXFMT_MASK                    0xF8000000U
#define PVR5__GRPH1PIXFMT_SHIFT                   27
#define PVR5__GRPH1PIXFMT_SIGNED                  0

/*
	Register PVR_PDP_GRPH1BLND
*/
#define PVR5__PDP_PVR_PDP_GRPH1BLND               0x0020
#define PVR5__GRPH1CKEY_MASK                      0x00FFFFFFU
#define PVR5__GRPH1CKEY_SHIFT                     0
#define PVR5__GRPH1CKEY_SIGNED                    0

#define PVR5__GRPH1GALPHA_MASK                    0xFF000000U
#define PVR5__GRPH1GALPHA_SHIFT                   24
#define PVR5__GRPH1GALPHA_SIGNED                  0

/*
	Register PVR_PDP_GRPH1BLND2
*/
#define PVR5__PDP_PVR_PDP_GRPH1BLND2              0x0040
#define PVR5__GRPH1CKEYMASK_MASK                  0x00FFFFFFU
#define PVR5__GRPH1CKEYMASK_SHIFT                 0
#define PVR5__GRPH1CKEYMASK_SIGNED                0

#define PVR5__GRPH1LINDBL_MASK                    0x20000000U
#define PVR5__GRPH1LINDBL_SHIFT                   29
#define PVR5__GRPH1LINDBL_SIGNED                  0

#define PVR5__GRPH1PIXDBL_MASK                    0x80000000U
#define PVR5__GRPH1PIXDBL_SHIFT                   31
#define PVR5__GRPH1PIXDBL_SIGNED                  0

/*
	Register PVR_PDP_GRPH1CTRL
*/
#define PVR5__PDP_PVR_PDP_GRPH1CTRL               0x0060
#define PVR5__GRPH1BLENDPOS_MASK                  0x07000000U
#define PVR5__GRPH1BLENDPOS_SHIFT                 24
#define PVR5__GRPH1BLENDPOS_SIGNED                0

#define PVR5__GRPH1BLEND_MASK                     0x18000000U
#define PVR5__GRPH1BLEND_SHIFT                    27
#define PVR5__GRPH1BLEND_SIGNED                   0

#define PVR5__GRPH1CKEYSRC_MASK                   0x20000000U
#define PVR5__GRPH1CKEYSRC_SHIFT                  29
#define PVR5__GRPH1CKEYSRC_SIGNED                 0

#define PVR5__GRPH1CKEYEN_MASK                    0x40000000U
#define PVR5__GRPH1CKEYEN_SHIFT                   30
#define PVR5__GRPH1CKEYEN_SIGNED                  0

#define PVR5__GRPH1STREN_MASK                     0x80000000U
#define PVR5__GRPH1STREN_SHIFT                    31
#define PVR5__GRPH1STREN_SIGNED                   0

/*
	Register PVR_PDP_GRPH1STRIDE
*/
#define PVR5__PDP_PVR_PDP_GRPH1STRIDE             0x0080
#define PVR5__GRPH1STRIDE_MASK                    0xFFC00000U
#define PVR5__GRPH1STRIDE_SHIFT                   22
#define PVR5__GRPH1STRIDE_SIGNED                  0

/*
	Register PVR_PDP_GRPH1SIZE
*/
#define PVR5__PDP_PVR_PDP_GRPH1SIZE               0x00A0
#define PVR5__GRPH1HEIGHT_MASK                    0x00000FFFU
#define PVR5__GRPH1HEIGHT_SHIFT                   0
#define PVR5__GRPH1HEIGHT_SIGNED                  0

#define PVR5__GRPH1WIDTH_MASK                     0x0FFF0000U
#define PVR5__GRPH1WIDTH_SHIFT                    16
#define PVR5__GRPH1WIDTH_SIGNED                   0

/*
	Register PVR_PDP_GRPH1POSN
*/
#define PVR5__PDP_PVR_PDP_GRPH1POSN               0x00C0
#define PVR5__GRPH1YSTART_MASK                    0x00000FFFU
#define PVR5__GRPH1YSTART_SHIFT                   0
#define PVR5__GRPH1YSTART_SIGNED                  0

#define PVR5__GRPH1XSTART_MASK                    0x0FFF0000U
#define PVR5__GRPH1XSTART_SHIFT                   16
#define PVR5__GRPH1XSTART_SIGNED                  0

/*
	Register PVR_PDP_GRPH1_INTERLEAVE_CTRL
*/
#define PVR5__PDP_PVR_PDP_GRPH1_INTERLEAVE_CTRL   0x00F0
#define PVR5__GRPH1INTFIELD_MASK                  0x00000001U
#define PVR5__GRPH1INTFIELD_SHIFT                 0
#define PVR5__GRPH1INTFIELD_SIGNED                0

/*
	Register PVR_PDP_GRPH1_BASEADDR
*/
#define PVR5__PDP_PVR_PDP_GRPH1_BASEADDR          0x0110
#define PVR5__GRPH1BASEADDR_MASK                  0xFFFFFFF0U
#define PVR5__GRPH1BASEADDR_SHIFT                 4
#define PVR5__GRPH1BASEADDR_SIGNED                0

/*
	Register PVR_PDP_SYNCCTRL
*/
#define PVR5__PDP_PVR_PDP_SYNCCTRL                0x0154
#define PVR5__HSDIS_MASK                          0x00000001U
#define PVR5__HSDIS_SHIFT                         0
#define PVR5__HSDIS_SIGNED                        0

#define PVR5__HSPOL_MASK                          0x00000002U
#define PVR5__HSPOL_SHIFT                         1
#define PVR5__HSPOL_SIGNED                        0

#define PVR5__VSDIS_MASK                          0x00000004U
#define PVR5__VSDIS_SHIFT                         2
#define PVR5__VSDIS_SIGNED                        0

#define PVR5__VSPOL_MASK                          0x00000008U
#define PVR5__VSPOL_SHIFT                         3
#define PVR5__VSPOL_SIGNED                        0

#define PVR5__BLNKDIS_MASK                        0x00000010U
#define PVR5__BLNKDIS_SHIFT                       4
#define PVR5__BLNKDIS_SIGNED                      0

#define PVR5__BLNKPOL_MASK                        0x00000020U
#define PVR5__BLNKPOL_SHIFT                       5
#define PVR5__BLNKPOL_SIGNED                      0

#define PVR5__HS_SLAVE_MASK                       0x00000040U
#define PVR5__HS_SLAVE_SHIFT                      6
#define PVR5__HS_SLAVE_SIGNED                     0

#define PVR5__VS_SLAVE_MASK                       0x00000080U
#define PVR5__VS_SLAVE_SHIFT                      7
#define PVR5__VS_SLAVE_SIGNED                     0

#define PVR5__CLKPOL_MASK                         0x00000800U
#define PVR5__CLKPOL_SHIFT                        11
#define PVR5__CLKPOL_SIGNED                       0

#define PVR5__CSYNC_EN_MASK                       0x00001000U
#define PVR5__CSYNC_EN_SHIFT                      12
#define PVR5__CSYNC_EN_SIGNED                     0

#define PVR5__FIELD_EN_MASK                       0x00002000U
#define PVR5__FIELD_EN_SHIFT                      13
#define PVR5__FIELD_EN_SIGNED                     0

#define PVR5__FIELDPOL_MASK                       0x00004000U
#define PVR5__FIELDPOL_SHIFT                      14
#define PVR5__FIELDPOL_SIGNED                     0

#define PVR5__UPDWAIT_MASK                        0x000F0000U
#define PVR5__UPDWAIT_SHIFT                       16
#define PVR5__UPDWAIT_SIGNED                      0

#define PVR5__UPDCTRL_MASK                        0x01000000U
#define PVR5__UPDCTRL_SHIFT                       24
#define PVR5__UPDCTRL_SIGNED                      0

#define PVR5__UPDINTCTRL_MASK                     0x02000000U
#define PVR5__UPDINTCTRL_SHIFT                    25
#define PVR5__UPDINTCTRL_SIGNED                   0

#define PVR5__UPDSYNCTRL_MASK                     0x04000000U
#define PVR5__UPDSYNCTRL_SHIFT                    26
#define PVR5__UPDSYNCTRL_SIGNED                   0

#define PVR5__LOWPWRMODE_MASK                     0x08000000U
#define PVR5__LOWPWRMODE_SHIFT                    27
#define PVR5__LOWPWRMODE_SIGNED                   0

#define PVR5__POWERDN_MASK                        0x10000000U
#define PVR5__POWERDN_SHIFT                       28
#define PVR5__POWERDN_SIGNED                      0

#define PVR5__PDP_RST_MASK                        0x20000000U
#define PVR5__PDP_RST_SHIFT                       29
#define PVR5__PDP_RST_SIGNED                      0

#define PVR5__SYNCACTIVE_MASK                     0x80000000U
#define PVR5__SYNCACTIVE_SHIFT                    31
#define PVR5__SYNCACTIVE_SIGNED                   0

/*
	Register PVR_PDP_HSYNC1
*/
#define PVR5__PDP_PVR_PDP_HSYNC1                  0x0158
#define PVR5__HT_MASK                             0x00001FFFU
#define PVR5__HT_SHIFT                            0
#define PVR5__HT_SIGNED                           0

#define PVR5__HBPS_MASK                           0x1FFF0000U
#define PVR5__HBPS_SHIFT                          16
#define PVR5__HBPS_SIGNED                         0

/*
	Register PVR_PDP_HSYNC2
*/
#define PVR5__PDP_PVR_PDP_HSYNC2                  0x015C
#define PVR5__HLBS_MASK                           0x00001FFFU
#define PVR5__HLBS_SHIFT                          0
#define PVR5__HLBS_SIGNED                         0

#define PVR5__HAS_MASK                            0x1FFF0000U
#define PVR5__HAS_SHIFT                           16
#define PVR5__HAS_SIGNED                          0

/*
	Register PVR_PDP_HSYNC3
*/
#define PVR5__PDP_PVR_PDP_HSYNC3                  0x0160
#define PVR5__HRBS_MASK                           0x00001FFFU
#define PVR5__HRBS_SHIFT                          0
#define PVR5__HRBS_SIGNED                         0

#define PVR5__HFPS_MASK                           0x1FFF0000U
#define PVR5__HFPS_SHIFT                          16
#define PVR5__HFPS_SIGNED                         0

/*
	Register PVR_PDP_VSYNC1
*/
#define PVR5__PDP_PVR_PDP_VSYNC1                  0x0164
#define PVR5__VT_MASK                             0x00001FFFU
#define PVR5__VT_SHIFT                            0
#define PVR5__VT_SIGNED                           0

#define PVR5__VBPS_MASK                           0x1FFF0000U
#define PVR5__VBPS_SHIFT                          16
#define PVR5__VBPS_SIGNED                         0

/*
	Register PVR_PDP_VSYNC2
*/
#define PVR5__PDP_PVR_PDP_VSYNC2                  0x0168
#define PVR5__VTBS_MASK                           0x00001FFFU
#define PVR5__VTBS_SHIFT                          0
#define PVR5__VTBS_SIGNED                         0

#define PVR5__VAS_MASK                            0x1FFF0000U
#define PVR5__VAS_SHIFT                           16
#define PVR5__VAS_SIGNED                          0

/*
	Register PVR_PDP_VSYNC3
*/
#define PVR5__PDP_PVR_PDP_VSYNC3                  0x016C
#define PVR5__VBBS_MASK                           0x00001FFFU
#define PVR5__VBBS_SHIFT                          0
#define PVR5__VBBS_SIGNED                         0

#define PVR5__VFPS_MASK                           0x1FFF0000U
#define PVR5__VFPS_SHIFT                          16
#define PVR5__VFPS_SIGNED                         0

/*
	Register PVR_PDP_BORDCOL
*/
#define PVR5__PDP_PVR_PDP_BORDCOL                 0x0170
#define PVR5__BORDCOL_MASK                        0x00FFFFFFU
#define PVR5__BORDCOL_SHIFT                       0
#define PVR5__BORDCOL_SIGNED                      0

/*
	Register PVR_PDP_BGNDCOL
*/
#define PVR5__PDP_PVR_PDP_BGNDCOL                 0x0174
#define PVR5__BGNDCOL_MASK                        0x00FFFFFFU
#define PVR5__BGNDCOL_SHIFT                       0
#define PVR5__BGNDCOL_SIGNED                      0

#define PVR5__BGNDALPHA_MASK                      0xFF000000U
#define PVR5__BGNDALPHA_SHIFT                     24
#define PVR5__BGNDALPHA_SIGNED                    0

/*
	Register PVR_PDP_INTSTAT
*/
#define PVR5__PDP_PVR_PDP_INTSTAT                 0x0178
#define PVR5__INTS_HBLNK0_MASK                    0x00000001U
#define PVR5__INTS_HBLNK0_SHIFT                   0
#define PVR5__INTS_HBLNK0_SIGNED                  0

#define PVR5__INTS_HBLNK1_MASK                    0x00000002U
#define PVR5__INTS_HBLNK1_SHIFT                   1
#define PVR5__INTS_HBLNK1_SIGNED                  0

#define PVR5__INTS_VBLNK0_MASK                    0x00000004U
#define PVR5__INTS_VBLNK0_SHIFT                   2
#define PVR5__INTS_VBLNK0_SIGNED                  0

#define PVR5__INTS_VBLNK1_MASK                    0x00000008U
#define PVR5__INTS_VBLNK1_SHIFT                   3
#define PVR5__INTS_VBLNK1_SIGNED                  0

#define PVR5__INTS_GRPH1URUN_MASK                 0x00000010U
#define PVR5__INTS_GRPH1URUN_SHIFT                4
#define PVR5__INTS_GRPH1URUN_SIGNED               0

#define PVR5__INTS_GRPH1ORUN_MASK                 0x00010000U
#define PVR5__INTS_GRPH1ORUN_SHIFT                16
#define PVR5__INTS_GRPH1ORUN_SIGNED               0

#define PVR5__INTS_I2P_PDP_EOL_MISMATCH_MASK      0x01000000U
#define PVR5__INTS_I2P_PDP_EOL_MISMATCH_SHIFT     24
#define PVR5__INTS_I2P_PDP_EOL_MISMATCH_SIGNED    0

#define PVR5__INTS_I2P_OUT_PIXEL_FIFO_OVERFLOW_MASK 0x02000000U
#define PVR5__INTS_I2P_OUT_PIXEL_FIFO_OVERFLOW_SHIFT 25
#define PVR5__INTS_I2P_OUT_PIXEL_FIFO_OVERFLOW_SIGNED 0

#define PVR5__INTS_I2P_OUT_PIXEL_FIFO_UNDERFLOW_MASK 0x04000000U
#define PVR5__INTS_I2P_OUT_PIXEL_FIFO_UNDERFLOW_SHIFT 26
#define PVR5__INTS_I2P_OUT_PIXEL_FIFO_UNDERFLOW_SIGNED 0

#define PVR5__INTS_I2P_OUT_EXT_RAM_FIFO_OVERFLOW_MASK 0x08000000U
#define PVR5__INTS_I2P_OUT_EXT_RAM_FIFO_OVERFLOW_SHIFT 27
#define PVR5__INTS_I2P_OUT_EXT_RAM_FIFO_OVERFLOW_SIGNED 0

#define PVR5__INTS_I2P_OUT_EXT_RAM_FIFO_UNDERFLOW_MASK 0x10000000U
#define PVR5__INTS_I2P_OUT_EXT_RAM_FIFO_UNDERFLOW_SHIFT 28
#define PVR5__INTS_I2P_OUT_EXT_RAM_FIFO_UNDERFLOW_SIGNED 0

#define PVR5__INTS_I2P_OUT_SB_FIFO_OVERFLOW_MASK  0x20000000U
#define PVR5__INTS_I2P_OUT_SB_FIFO_OVERFLOW_SHIFT 29
#define PVR5__INTS_I2P_OUT_SB_FIFO_OVERFLOW_SIGNED 0

#define PVR5__INTS_I2P_OUT_SB_FIFO_UNDERFLOW_MASK 0x40000000U
#define PVR5__INTS_I2P_OUT_SB_FIFO_UNDERFLOW_SHIFT 30
#define PVR5__INTS_I2P_OUT_SB_FIFO_UNDERFLOW_SIGNED 0

/*
	Register PVR_PDP_INTENAB
*/
#define PVR5__PDP_PVR_PDP_INTENAB                 0x017C
#define PVR5__INTEN_HBLNK0_MASK                   0x00000001U
#define PVR5__INTEN_HBLNK0_SHIFT                  0
#define PVR5__INTEN_HBLNK0_SIGNED                 0

#define PVR5__INTEN_HBLNK1_MASK                   0x00000002U
#define PVR5__INTEN_HBLNK1_SHIFT                  1
#define PVR5__INTEN_HBLNK1_SIGNED                 0

#define PVR5__INTEN_VBLNK0_MASK                   0x00000004U
#define PVR5__INTEN_VBLNK0_SHIFT                  2
#define PVR5__INTEN_VBLNK0_SIGNED                 0

#define PVR5__INTEN_VBLNK1_MASK                   0x00000008U
#define PVR5__INTEN_VBLNK1_SHIFT                  3
#define PVR5__INTEN_VBLNK1_SIGNED                 0

#define PVR5__INTEN_GRPH1URUN_MASK                0x00000010U
#define PVR5__INTEN_GRPH1URUN_SHIFT               4
#define PVR5__INTEN_GRPH1URUN_SIGNED              0

#define PVR5__INTEN_GRPH1ORUN_MASK                0x00010000U
#define PVR5__INTEN_GRPH1ORUN_SHIFT               16
#define PVR5__INTEN_GRPH1ORUN_SIGNED              0

#define PVR5__INTEN_I2P_PDP_EOL_MISMATCH_MASK     0x01000000U
#define PVR5__INTEN_I2P_PDP_EOL_MISMATCH_SHIFT    24
#define PVR5__INTEN_I2P_PDP_EOL_MISMATCH_SIGNED   0

#define PVR5__INTEN_I2P_OUT_PIXEL_FIFO_OVERFLOW_MASK 0x02000000U
#define PVR5__INTEN_I2P_OUT_PIXEL_FIFO_OVERFLOW_SHIFT 25
#define PVR5__INTEN_I2P_OUT_PIXEL_FIFO_OVERFLOW_SIGNED 0

#define PVR5__INTEN_I2P_OUT_PIXEL_FIFO_UNDERFLOW_MASK 0x04000000U
#define PVR5__INTEN_I2P_OUT_PIXEL_FIFO_UNDERFLOW_SHIFT 26
#define PVR5__INTEN_I2P_OUT_PIXEL_FIFO_UNDERFLOW_SIGNED 0

#define PVR5__INTEN_I2P_OUT_EXT_RAM_FIFO_OVERFLOW_MASK 0x08000000U
#define PVR5__INTEN_I2P_OUT_EXT_RAM_FIFO_OVERFLOW_SHIFT 27
#define PVR5__INTEN_I2P_OUT_EXT_RAM_FIFO_OVERFLOW_SIGNED 0

#define PVR5__INTEN_I2P_OUT_EXT_RAM_FIFO_UNDERFLOW_MASK 0x10000000U
#define PVR5__INTEN_I2P_OUT_EXT_RAM_FIFO_UNDERFLOW_SHIFT 28
#define PVR5__INTEN_I2P_OUT_EXT_RAM_FIFO_UNDERFLOW_SIGNED 0

#define PVR5__INTEN_I2P_OUT_SB_FIFO_OVERFLOW_MASK 0x20000000U
#define PVR5__INTEN_I2P_OUT_SB_FIFO_OVERFLOW_SHIFT 29
#define PVR5__INTEN_I2P_OUT_SB_FIFO_OVERFLOW_SIGNED 0

#define PVR5__INTEN_I2P_OUT_SB_FIFO_UNDERFLOW_MASK 0x40000000U
#define PVR5__INTEN_I2P_OUT_SB_FIFO_UNDERFLOW_SHIFT 30
#define PVR5__INTEN_I2P_OUT_SB_FIFO_UNDERFLOW_SIGNED 0

/*
	Register PVR_PDP_INTCTRL
*/
#define PVR5__PDP_PVR_PDP_INTCTRL                 0x0180
#define PVR5__HBLNK_LINENO_MASK                   0x00001FFFU
#define PVR5__HBLNK_LINENO_SHIFT                  0
#define PVR5__HBLNK_LINENO_SIGNED                 0

#define PVR5__HBLNK_LINE_MASK                     0x00010000U
#define PVR5__HBLNK_LINE_SHIFT                    16
#define PVR5__HBLNK_LINE_SIGNED                   0

/*
	Register PVR_PDP_SIGNAT
*/
#define PVR5__PDP_PVR_PDP_SIGNAT                  0x0184
#define PVR5__SIGNATURE_MASK                      0xFFFFFFFFU
#define PVR5__SIGNATURE_SHIFT                     0
#define PVR5__SIGNATURE_SIGNED                    0

/*
	Register PVR_PDP_MEMCTRL
*/
#define PVR5__PDP_PVR_PDP_MEMCTRL                 0x0188
#define PVR5__BURSTLEN_MASK                       0x0000001FU
#define PVR5__BURSTLEN_SHIFT                      0
#define PVR5__BURSTLEN_SIGNED                     0

#define PVR5__THRESHOLD_MASK                      0x00001F80U
#define PVR5__THRESHOLD_SHIFT                     7
#define PVR5__THRESHOLD_SIGNED                    0

#define PVR5__YTHRESHOLD_MASK                     0x001F8000U
#define PVR5__YTHRESHOLD_SHIFT                    15
#define PVR5__YTHRESHOLD_SIGNED                   0

#define PVR5__UVTHRESHOLD_MASK                    0x0F800000U
#define PVR5__UVTHRESHOLD_SHIFT                   23
#define PVR5__UVTHRESHOLD_SIGNED                  0

#define PVR5__MEMREFRESH_MASK                     0xC0000000U
#define PVR5__MEMREFRESH_SHIFT                    30
#define PVR5__MEMREFRESH_SIGNED                   0

/*
	Register PVR_PDP_GRPH1_MEMCTRL
*/
#define PVR5__PDP_PVR_PDP_GRPH1_MEMCTRL           0x0190
#define PVR5__GRPH1_BURSTLEN_MASK                 0x0000001FU
#define PVR5__GRPH1_BURSTLEN_SHIFT                0
#define PVR5__GRPH1_BURSTLEN_SIGNED               0

#define PVR5__GRPH1_THRESHOLD_MASK                0x00001F80U
#define PVR5__GRPH1_THRESHOLD_SHIFT               7
#define PVR5__GRPH1_THRESHOLD_SIGNED              0

#define PVR5__GRPH1_YTHRESHOLD_MASK               0x001F8000U
#define PVR5__GRPH1_YTHRESHOLD_SHIFT              15
#define PVR5__GRPH1_YTHRESHOLD_SIGNED             0

#define PVR5__GRPH1_UVTHRESHOLD_MASK              0x0F800000U
#define PVR5__GRPH1_UVTHRESHOLD_SHIFT             23
#define PVR5__GRPH1_UVTHRESHOLD_SIGNED            0

#define PVR5__GRPH1_LOCAL_GLOBAL_MEMCTRL_MASK     0x80000000U
#define PVR5__GRPH1_LOCAL_GLOBAL_MEMCTRL_SHIFT    31
#define PVR5__GRPH1_LOCAL_GLOBAL_MEMCTRL_SIGNED   0

/*
	Register PVR_PDP_PORTER_BLND1
*/
#define PVR5__PDP_PVR_PDP_PORTER_BLND1            0x01E4
#define PVR5__BLND1PORTERMODE_MASK                0x0000000FU
#define PVR5__BLND1PORTERMODE_SHIFT               0
#define PVR5__BLND1PORTERMODE_SIGNED              0

#define PVR5__BLND1BLENDTYPE_MASK                 0x00000010U
#define PVR5__BLND1BLENDTYPE_SHIFT                4
#define PVR5__BLND1BLENDTYPE_SIGNED               0

/*
	Register PVR_PDP_GAMMA0
*/
#define PVR5__PDP_PVR_PDP_GAMMA0                  0x0200
#define PVR5__GAMMA0_MASK                         0x00FFFFFFU
#define PVR5__GAMMA0_SHIFT                        0
#define PVR5__GAMMA0_SIGNED                       0

/*
	Register PVR_PDP_GAMMA1
*/
#define PVR5__PDP_PVR_PDP_GAMMA1                  0x0204
#define PVR5__GAMMA1_MASK                         0x00FFFFFFU
#define PVR5__GAMMA1_SHIFT                        0
#define PVR5__GAMMA1_SIGNED                       0

/*
	Register PVR_PDP_GAMMA2
*/
#define PVR5__PDP_PVR_PDP_GAMMA2                  0x0208
#define PVR5__GAMMA2_MASK                         0x00FFFFFFU
#define PVR5__GAMMA2_SHIFT                        0
#define PVR5__GAMMA2_SIGNED                       0

/*
	Register PVR_PDP_GAMMA3
*/
#define PVR5__PDP_PVR_PDP_GAMMA3                  0x020C
#define PVR5__GAMMA3_MASK                         0x00FFFFFFU
#define PVR5__GAMMA3_SHIFT                        0
#define PVR5__GAMMA3_SIGNED                       0

/*
	Register PVR_PDP_GAMMA4
*/
#define PVR5__PDP_PVR_PDP_GAMMA4                  0x0210
#define PVR5__GAMMA4_MASK                         0x00FFFFFFU
#define PVR5__GAMMA4_SHIFT                        0
#define PVR5__GAMMA4_SIGNED                       0

/*
	Register PVR_PDP_GAMMA5
*/
#define PVR5__PDP_PVR_PDP_GAMMA5                  0x0214
#define PVR5__GAMMA5_MASK                         0x00FFFFFFU
#define PVR5__GAMMA5_SHIFT                        0
#define PVR5__GAMMA5_SIGNED                       0

/*
	Register PVR_PDP_GAMMA6
*/
#define PVR5__PDP_PVR_PDP_GAMMA6                  0x0218
#define PVR5__GAMMA6_MASK                         0x00FFFFFFU
#define PVR5__GAMMA6_SHIFT                        0
#define PVR5__GAMMA6_SIGNED                       0

/*
	Register PVR_PDP_GAMMA7
*/
#define PVR5__PDP_PVR_PDP_GAMMA7                  0x021C
#define PVR5__GAMMA7_MASK                         0x00FFFFFFU
#define PVR5__GAMMA7_SHIFT                        0
#define PVR5__GAMMA7_SIGNED                       0

/*
	Register PVR_PDP_GAMMA8
*/
#define PVR5__PDP_PVR_PDP_GAMMA8                  0x0220
#define PVR5__GAMMA8_MASK                         0x00FFFFFFU
#define PVR5__GAMMA8_SHIFT                        0
#define PVR5__GAMMA8_SIGNED                       0

/*
	Register PVR_PDP_GAMMA9
*/
#define PVR5__PDP_PVR_PDP_GAMMA9                  0x0224
#define PVR5__GAMMA9_MASK                         0x00FFFFFFU
#define PVR5__GAMMA9_SHIFT                        0
#define PVR5__GAMMA9_SIGNED                       0

/*
	Register PVR_PDP_GAMMA10
*/
#define PVR5__PDP_PVR_PDP_GAMMA10                 0x0228
#define PVR5__GAMMA10_MASK                        0x00FFFFFFU
#define PVR5__GAMMA10_SHIFT                       0
#define PVR5__GAMMA10_SIGNED                      0

/*
	Register PVR_PDP_GAMMA11
*/
#define PVR5__PDP_PVR_PDP_GAMMA11                 0x022C
#define PVR5__GAMMA11_MASK                        0x00FFFFFFU
#define PVR5__GAMMA11_SHIFT                       0
#define PVR5__GAMMA11_SIGNED                      0

/*
	Register PVR_PDP_GAMMA12
*/
#define PVR5__PDP_PVR_PDP_GAMMA12                 0x0230
#define PVR5__GAMMA12_MASK                        0x00FFFFFFU
#define PVR5__GAMMA12_SHIFT                       0
#define PVR5__GAMMA12_SIGNED                      0

/*
	Register PVR_PDP_GAMMA13
*/
#define PVR5__PDP_PVR_PDP_GAMMA13                 0x0234
#define PVR5__GAMMA13_MASK                        0x00FFFFFFU
#define PVR5__GAMMA13_SHIFT                       0
#define PVR5__GAMMA13_SIGNED                      0

/*
	Register PVR_PDP_GAMMA14
*/
#define PVR5__PDP_PVR_PDP_GAMMA14                 0x0238
#define PVR5__GAMMA14_MASK                        0x00FFFFFFU
#define PVR5__GAMMA14_SHIFT                       0
#define PVR5__GAMMA14_SIGNED                      0

/*
	Register PVR_PDP_GAMMA15
*/
#define PVR5__PDP_PVR_PDP_GAMMA15                 0x023C
#define PVR5__GAMMA15_MASK                        0x00FFFFFFU
#define PVR5__GAMMA15_SHIFT                       0
#define PVR5__GAMMA15_SIGNED                      0

/*
	Register PVR_PDP_GAMMA16
*/
#define PVR5__PDP_PVR_PDP_GAMMA16                 0x0240
#define PVR5__GAMMA16_MASK                        0x00FFFFFFU
#define PVR5__GAMMA16_SHIFT                       0
#define PVR5__GAMMA16_SIGNED                      0

/*
	Register PVR_PDP_REGLD_ADDR_CTRL
*/
#define PVR5__PDP_PVR_PDP_REGLD_ADDR_CTRL         0x0298
#define PVR5__REGLD_ADDRIN_MASK                   0xFFFFFFF0U
#define PVR5__REGLD_ADDRIN_SHIFT                  4
#define PVR5__REGLD_ADDRIN_SIGNED                 0

/*
	Register PVR_PDP_REGLD_ADDR_STAT
*/
#define PVR5__PDP_PVR_PDP_REGLD_ADDR_STAT         0x029C
#define PVR5__REGLD_ADDROUT_MASK                  0xFFFFFFF0U
#define PVR5__REGLD_ADDROUT_SHIFT                 4
#define PVR5__REGLD_ADDROUT_SIGNED                0

/*
	Register PVR_PDP_REGLD_STAT
*/
#define PVR5__PDP_PVR_PDP_REGLD_STAT              0x0300
#define PVR5__REGLD_ADDREN_MASK                   0x00800000U
#define PVR5__REGLD_ADDREN_SHIFT                  23
#define PVR5__REGLD_ADDREN_SIGNED                 0

/*
	Register PVR_PDP_REGLD_CTRL
*/
#define PVR5__PDP_PVR_PDP_REGLD_CTRL              0x0304
#define PVR5__REGLD_VAL_MASK                      0x00800000U
#define PVR5__REGLD_VAL_SHIFT                     23
#define PVR5__REGLD_VAL_SIGNED                    0

#define PVR5__REGLD_ADDRLEN_MASK                  0xFF000000U
#define PVR5__REGLD_ADDRLEN_SHIFT                 24
#define PVR5__REGLD_ADDRLEN_SIGNED                0

/*
	Register PVR_PDP_LINESTAT
*/
#define PVR5__PDP_PVR_PDP_LINESTAT                0x0308
#define PVR5__LINENO_MASK                         0x00001FFFU
#define PVR5__LINENO_SHIFT                        0
#define PVR5__LINENO_SIGNED                       0

/*
	Register PVR_PDP_UPDCTRL
*/
#define PVR5__PDP_PVR_PDP_UPDCTRL                 0x030C
#define PVR5__UPDFIELD_MASK                       0x00000001U
#define PVR5__UPDFIELD_SHIFT                      0
#define PVR5__UPDFIELD_SIGNED                     0

/*
	Register PVR_PDP_VEVENT
*/
#define PVR5__PDP_PVR_PDP_VEVENT                  0x0310
#define PVR5__VFETCH_MASK                         0x00001FFFU
#define PVR5__VFETCH_SHIFT                        0
#define PVR5__VFETCH_SIGNED                       0

#define PVR5__VEVENT_MASK                         0x1FFF0000U
#define PVR5__VEVENT_SHIFT                        16
#define PVR5__VEVENT_SIGNED                       0

/*
	Register PVR_PDP_HDECTRL
*/
#define PVR5__PDP_PVR_PDP_HDECTRL                 0x0314
#define PVR5__HDEF_MASK                           0x00001FFFU
#define PVR5__HDEF_SHIFT                          0
#define PVR5__HDEF_SIGNED                         0

#define PVR5__HDES_MASK                           0x1FFF0000U
#define PVR5__HDES_SHIFT                          16
#define PVR5__HDES_SIGNED                         0

/*
	Register PVR_PDP_VDECTRL
*/
#define PVR5__PDP_PVR_PDP_VDECTRL                 0x0318
#define PVR5__VDEF_MASK                           0x00001FFFU
#define PVR5__VDEF_SHIFT                          0
#define PVR5__VDEF_SIGNED                         0

#define PVR5__VDES_MASK                           0x1FFF0000U
#define PVR5__VDES_SHIFT                          16
#define PVR5__VDES_SIGNED                         0

/*
	Register PVR_PDP_OPMASK
*/
#define PVR5__PDP_PVR_PDP_OPMASK                  0x031C
#define PVR5__MASKR_MASK                          0x000000FFU
#define PVR5__MASKR_SHIFT                         0
#define PVR5__MASKR_SIGNED                        0

#define PVR5__MASKG_MASK                          0x0000FF00U
#define PVR5__MASKG_SHIFT                         8
#define PVR5__MASKG_SIGNED                        0

#define PVR5__MASKB_MASK                          0x00FF0000U
#define PVR5__MASKB_SHIFT                         16
#define PVR5__MASKB_SIGNED                        0

#define PVR5__BLANKLEVEL_MASK                     0x40000000U
#define PVR5__BLANKLEVEL_SHIFT                    30
#define PVR5__BLANKLEVEL_SIGNED                   0

#define PVR5__MASKLEVEL_MASK                      0x80000000U
#define PVR5__MASKLEVEL_SHIFT                     31
#define PVR5__MASKLEVEL_SIGNED                    0

/*
	Register PVR_PDP_CSCCOEFF0
*/
#define PVR5__PDP_PVR_PDP_CSCCOEFF0               0x0330
#define PVR5__CSCCOEFFRY_MASK                     0x000007FFU
#define PVR5__CSCCOEFFRY_SHIFT                    0
#define PVR5__CSCCOEFFRY_SIGNED                   0

#define PVR5__CSCCOEFFRU_MASK                     0x003FF800U
#define PVR5__CSCCOEFFRU_SHIFT                    11
#define PVR5__CSCCOEFFRU_SIGNED                   0

/*
	Register PVR_PDP_CSCCOEFF1
*/
#define PVR5__PDP_PVR_PDP_CSCCOEFF1               0x0334
#define PVR5__CSCCOEFFRV_MASK                     0x000007FFU
#define PVR5__CSCCOEFFRV_SHIFT                    0
#define PVR5__CSCCOEFFRV_SIGNED                   0

#define PVR5__CSCCOEFFGY_MASK                     0x003FF800U
#define PVR5__CSCCOEFFGY_SHIFT                    11
#define PVR5__CSCCOEFFGY_SIGNED                   0

/*
	Register PVR_PDP_CSCCOEFF2
*/
#define PVR5__PDP_PVR_PDP_CSCCOEFF2               0x0338
#define PVR5__CSCCOEFFGU_MASK                     0x000007FFU
#define PVR5__CSCCOEFFGU_SHIFT                    0
#define PVR5__CSCCOEFFGU_SIGNED                   0

#define PVR5__CSCCOEFFGV_MASK                     0x003FF800U
#define PVR5__CSCCOEFFGV_SHIFT                    11
#define PVR5__CSCCOEFFGV_SIGNED                   0

/*
	Register PVR_PDP_CSCCOEFF3
*/
#define PVR5__PDP_PVR_PDP_CSCCOEFF3               0x033C
#define PVR5__CSCCOEFFBY_MASK                     0x000007FFU
#define PVR5__CSCCOEFFBY_SHIFT                    0
#define PVR5__CSCCOEFFBY_SIGNED                   0

#define PVR5__CSCCOEFFBU_MASK                     0x003FF800U
#define PVR5__CSCCOEFFBU_SHIFT                    11
#define PVR5__CSCCOEFFBU_SIGNED                   0

/*
	Register PVR_PDP_CSCCOEFF4
*/
#define PVR5__PDP_PVR_PDP_CSCCOEFF4               0x0340
#define PVR5__CSCCOEFFBV_MASK                     0x000007FFU
#define PVR5__CSCCOEFFBV_SHIFT                    0
#define PVR5__CSCCOEFFBV_SIGNED                   0

/*
	Register CR_PDP_PROCAMP_C11C12
*/
#define PVR5__PDP_CR_PDP_PROCAMP_C11C12           0x03D0
#define PVR5__CR_PROCAMP_C11_MASK                 0x00003FFFU
#define PVR5__CR_PROCAMP_C11_SHIFT                0
#define PVR5__CR_PROCAMP_C11_SIGNED               0

#define PVR5__CR_PROCAMP_C12_MASK                 0x3FFF0000U
#define PVR5__CR_PROCAMP_C12_SHIFT                16
#define PVR5__CR_PROCAMP_C12_SIGNED               0

/*
	Register CR_PDP_PROCAMP_C13C21
*/
#define PVR5__PDP_CR_PDP_PROCAMP_C13C21           0x03D4
#define PVR5__CR_PROCAMP_C13_MASK                 0x00003FFFU
#define PVR5__CR_PROCAMP_C13_SHIFT                0
#define PVR5__CR_PROCAMP_C13_SIGNED               0

#define PVR5__CR_PROCAMP_C21_MASK                 0x3FFF0000U
#define PVR5__CR_PROCAMP_C21_SHIFT                16
#define PVR5__CR_PROCAMP_C21_SIGNED               0

/*
	Register CR_PDP_PROCAMP_C22C23
*/
#define PVR5__PDP_CR_PDP_PROCAMP_C22C23           0x03D8
#define PVR5__CR_PROCAMP_C22_MASK                 0x00003FFFU
#define PVR5__CR_PROCAMP_C22_SHIFT                0
#define PVR5__CR_PROCAMP_C22_SIGNED               0

#define PVR5__CR_PROCAMP_C23_MASK                 0x3FFF0000U
#define PVR5__CR_PROCAMP_C23_SHIFT                16
#define PVR5__CR_PROCAMP_C23_SIGNED               0

/*
	Register CR_PDP_PROCAMP_C31C32
*/
#define PVR5__PDP_CR_PDP_PROCAMP_C31C32           0x03DC
#define PVR5__CR_PROCAMP_C31_MASK                 0x00003FFFU
#define PVR5__CR_PROCAMP_C31_SHIFT                0
#define PVR5__CR_PROCAMP_C31_SIGNED               0

#define PVR5__CR_PROCAMP_C32_MASK                 0x3FFF0000U
#define PVR5__CR_PROCAMP_C32_SHIFT                16
#define PVR5__CR_PROCAMP_C32_SIGNED               0

/*
	Register CR_PDP_PROCAMP_C33
*/
#define PVR5__PDP_CR_PDP_PROCAMP_C33              0x03E0
#define PVR5__CR_PROCAMP_EN_MASK                  0x00000001U
#define PVR5__CR_PROCAMP_EN_SHIFT                 0
#define PVR5__CR_PROCAMP_EN_SIGNED                0

#define PVR5__CR_PROCAMP_RANGE_MASK               0x00000030U
#define PVR5__CR_PROCAMP_RANGE_SHIFT              4
#define PVR5__CR_PROCAMP_RANGE_SIGNED             0

#define PVR5__CR_PROCAMP_C33_MASK                 0x3FFF0000U
#define PVR5__CR_PROCAMP_C33_SHIFT                16
#define PVR5__CR_PROCAMP_C33_SIGNED               0

/*
	Register CR_PDP_PROCAMP_INOFFSET
*/
#define PVR5__PDP_CR_PDP_PROCAMP_INOFFSET         0x03E4
#define PVR5__CR_PROCAMP_INOFF_B_MASK             0x000000FFU
#define PVR5__CR_PROCAMP_INOFF_B_SHIFT            0
#define PVR5__CR_PROCAMP_INOFF_B_SIGNED           0

#define PVR5__CR_PROCAMP_INOFF_G_MASK             0x0000FF00U
#define PVR5__CR_PROCAMP_INOFF_G_SHIFT            8
#define PVR5__CR_PROCAMP_INOFF_G_SIGNED           0

#define PVR5__CR_PROCAMP_INOFF_R_MASK             0x00FF0000U
#define PVR5__CR_PROCAMP_INOFF_R_SHIFT            16
#define PVR5__CR_PROCAMP_INOFF_R_SIGNED           0

/*
	Register CR_PDP_PROCAMP_OUTOFFSET_BG
*/
#define PVR5__PDP_CR_PDP_PROCAMP_OUTOFFSET_BG     0x03E8
#define PVR5__CR_PROCAMP_OUTOFF_B_MASK            0x000003FFU
#define PVR5__CR_PROCAMP_OUTOFF_B_SHIFT           0
#define PVR5__CR_PROCAMP_OUTOFF_B_SIGNED          0

#define PVR5__CR_PROCAMP_OUTOFF_G_MASK            0x03FF0000U
#define PVR5__CR_PROCAMP_OUTOFF_G_SHIFT           16
#define PVR5__CR_PROCAMP_OUTOFF_G_SIGNED          0

/*
	Register CR_PDP_PROCAMP_OUTOFFSET_R
*/
#define PVR5__PDP_CR_PDP_PROCAMP_OUTOFFSET_R      0x03EC
#define PVR5__CR_PROCAMP_OUTOFF_R_MASK            0x000003FFU
#define PVR5__CR_PROCAMP_OUTOFF_R_SHIFT           0
#define PVR5__CR_PROCAMP_OUTOFF_R_SIGNED          0

/*
	Register PVR_PDP_GRPH1_PALETTE_ADDR
*/
#define PVR5__PDP_PVR_PDP_GRPH1_PALETTE_ADDR      0x0400
#define PVR5__GRPH1LUTADDR_MASK                   0xFF000000U
#define PVR5__GRPH1LUTADDR_SHIFT                  24
#define PVR5__GRPH1LUTADDR_SIGNED                 0

/*
	Register PVR_PDP_GRPH1_PALETTE_DATA
*/
#define PVR5__PDP_PVR_PDP_GRPH1_PALETTE_DATA      0x0404
#define PVR5__GRPH1LUTDATA_MASK                   0x00FFFFFFU
#define PVR5__GRPH1LUTDATA_SHIFT                  0
#define PVR5__GRPH1LUTDATA_SIGNED                 0

/*
	Register PVR_PDP_CORE_ID
*/
#define PVR5__PDP_PVR_PDP_CORE_ID                 0x04E0
#define PVR5__CONFIG_ID_MASK                      0x0000FFFFU
#define PVR5__CONFIG_ID_SHIFT                     0
#define PVR5__CONFIG_ID_SIGNED                    0

#define PVR5__CORE_ID_MASK                        0x00FF0000U
#define PVR5__CORE_ID_SHIFT                       16
#define PVR5__CORE_ID_SIGNED                      0

#define PVR5__GROUP_ID_MASK                       0xFF000000U
#define PVR5__GROUP_ID_SHIFT                      24
#define PVR5__GROUP_ID_SIGNED                     0

/*
	Register PVR_PDP_CORE_REV
*/
#define PVR5__PDP_PVR_PDP_CORE_REV                0x04F0
#define PVR5__MAINT_REV_MASK                      0x000000FFU
#define PVR5__MAINT_REV_SHIFT                     0
#define PVR5__MAINT_REV_SIGNED                    0

#define PVR5__MINOR_REV_MASK                      0x0000FF00U
#define PVR5__MINOR_REV_SHIFT                     8
#define PVR5__MINOR_REV_SIGNED                    0

#define PVR5__MAJOR_REV_MASK                      0x00FF0000U
#define PVR5__MAJOR_REV_SHIFT                     16
#define PVR5__MAJOR_REV_SIGNED                    0

/*
	Register PVR_PDP_GRPH1SKIPCTRL
*/
#define PVR5__PDP_PVR_PDP_GRPH1SKIPCTRL           0x0578
#define PVR5__GRPH1VSKIP_MASK                     0x00000FFFU
#define PVR5__GRPH1VSKIP_SHIFT                    0
#define PVR5__GRPH1VSKIP_SIGNED                   0

#define PVR5__GRPH1HSKIP_MASK                     0x0FFF0000U
#define PVR5__GRPH1HSKIP_SHIFT                    16
#define PVR5__GRPH1HSKIP_SIGNED                   0

/*
	Register PVR_PDP_REGISTER_UPDATE_CTRL
*/
#define PVR5__PDP_PVR_PDP_REGISTER_UPDATE_CTRL    0x07A0
#define PVR5__USE_VBLANK_MASK                     0x00000001U
#define PVR5__USE_VBLANK_SHIFT                    0
#define PVR5__USE_VBLANK_SIGNED                   0

#define PVR5__REGISTERS_VALID_MASK                0x00000002U
#define PVR5__REGISTERS_VALID_SHIFT               1
#define PVR5__REGISTERS_VALID_SIGNED              0

#define PVR5__BYPASS_DOUBLE_BUFFERING_MASK        0x00000004U
#define PVR5__BYPASS_DOUBLE_BUFFERING_SHIFT       2
#define PVR5__BYPASS_DOUBLE_BUFFERING_SIGNED      0

/*
	Register PVR_PDP_REGISTER_UPDATE_STATUS
*/
#define PVR5__PDP_PVR_PDP_REGISTER_UPDATE_STATUS  0x07A4
#define PVR5__REGISTERS_UPDATED_MASK              0x00000002U
#define PVR5__REGISTERS_UPDATED_SHIFT             1
#define PVR5__REGISTERS_UPDATED_SIGNED            0

/*
	Register PVR_PDP_DBGCTRL
*/
#define PVR5__PDP_PVR_PDP_DBGCTRL                 0x07B0
#define PVR5__DBG_ENAB_MASK                       0x00000001U
#define PVR5__DBG_ENAB_SHIFT                      0
#define PVR5__DBG_ENAB_SIGNED                     0

#define PVR5__DBG_READ_MASK                       0x00000002U
#define PVR5__DBG_READ_SHIFT                      1
#define PVR5__DBG_READ_SIGNED                     0

/*
	Register PVR_PDP_DBGDATA
*/
#define PVR5__PDP_PVR_PDP_DBGDATA                 0x07B4
#define PVR5__DBG_DATA_MASK                       0x00FFFFFFU
#define PVR5__DBG_DATA_SHIFT                      0
#define PVR5__DBG_DATA_SIGNED                     0

/*
	Register PVR_PDP_DBGSIDE
*/
#define PVR5__PDP_PVR_PDP_DBGSIDE                 0x07B8
#define PVR5__DBG_SIDE_MASK                       0x00000007U
#define PVR5__DBG_SIDE_SHIFT                      0
#define PVR5__DBG_SIDE_SIGNED                     0

#define PVR5__DBG_VAL_MASK                        0x00000008U
#define PVR5__DBG_VAL_SHIFT                       3
#define PVR5__DBG_VAL_SIGNED                      0

/*
	Register PVR_PDP_OUTPUT
*/
#define PVR5__PDP_PVR_PDP_OUTPUT                  0x07C0
#define PVR5__OUTPUT_CONFIG_MASK                  0x00000001U
#define PVR5__OUTPUT_CONFIG_SHIFT                 0
#define PVR5__OUTPUT_CONFIG_SIGNED                0

#endif /* _OUT_DRV_H_ */

/*****************************************************************************
 End of file (out_drv.h)
*****************************************************************************/
