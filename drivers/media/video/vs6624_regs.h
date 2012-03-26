/*
 * vs6624 - ST VS6624 CMOS image sensor registers
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _VS6624_REGS_H_
#define _VS6624_REGS_H_

/* low level control registers */
#define VS6624_MICRO_EN               0xC003 /* power enable for all MCU clock */
#define VS6624_DIO_EN                 0xC044 /* enable digital I/O */
/* device parameters */
#define VS6624_DEV_ID_MSB             0x0001 /* device id MSB */
#define VS6624_DEV_ID_LSB             0x0002 /* device id LSB */
#define VS6624_FW_VSN_MAJOR           0x0004 /* firmware version major */
#define VS6624_FW_VSN_MINOR           0x0006 /* firmware version minor */
#define VS6624_PATCH_VSN_MAJOR        0x0008 /* patch version major */
#define VS6624_PATCH_VSN_MINOR        0x000A /* patch version minor */
/* host interface manager control */
#define VS6624_USER_CMD               0x0180 /* user level control of operating states */
/* host interface manager status */
#define VS6624_STATE                  0x0202 /* current state of the mode manager */
/* run mode control */
#define VS6624_METER_ON               0x0280 /* if false AE and AWB are disabled */
/* mode setup */
#define VS6624_ACTIVE_PIPE_SETUP      0x0302 /* select the active bank for non view live mode */
#define VS6624_SENSOR_MODE            0x0308 /* select the different sensor mode */
/* pipe setup bank0 */
#define VS6624_IMAGE_SIZE0            0x0380 /* required output dimension */
#define VS6624_MAN_HSIZE0_MSB         0x0383 /* input required manual H size MSB */
#define VS6624_MAN_HSIZE0_LSB         0x0384 /* input required manual H size LSB */
#define VS6624_MAN_VSIZE0_MSB         0x0387 /* input required manual V size MSB */
#define VS6624_MAN_VSIZE0_LSB         0x0388 /* input required manual V size LSB */
#define VS6624_ZOOM_HSTEP0_MSB        0x038B /* set the zoom H step MSB */
#define VS6624_ZOOM_HSTEP0_LSB        0x038C /* set the zoom H step LSB */
#define VS6624_ZOOM_VSTEP0_MSB        0x038F /* set the zoom V step MSB */
#define VS6624_ZOOM_VSTEP0_LSB        0x0390 /* set the zoom V step LSB */
#define VS6624_ZOOM_CTRL0             0x0392 /* control zoon in, out and stop */
#define VS6624_PAN_HSTEP0_MSB         0x0395 /* set the pan H step MSB */
#define VS6624_PAN_HSTEP0_LSB         0x0396 /* set the pan H step LSB */
#define VS6624_PAN_VSTEP0_MSB         0x0399 /* set the pan V step MSB */
#define VS6624_PAN_VSTEP0_LSB         0x039A /* set the pan V step LSB */
#define VS6624_PAN_CTRL0              0x039C /* control pan operation */
#define VS6624_CROP_CTRL0             0x039E /* select cropping mode */
#define VS6624_CROP_HSTART0_MSB       0x03A1 /* set the cropping H start address MSB */
#define VS6624_CROP_HSTART0_LSB       0x03A2 /* set the cropping H start address LSB */
#define VS6624_CROP_HSIZE0_MSB        0x03A5 /* set the cropping H size MSB */
#define VS6624_CROP_HSIZE0_LSB        0x03A6 /* set the cropping H size LSB */
#define VS6624_CROP_VSTART0_MSB       0x03A9 /* set the cropping V start address MSB */
#define VS6624_CROP_VSTART0_LSB       0x03AA /* set the cropping V start address LSB */
#define VS6624_CROP_VSIZE0_MSB        0x03AD /* set the cropping V size MSB */
#define VS6624_CROP_VSIZE0_LSB        0x03AE /* set the cropping V size LSB */
#define VS6624_IMG_FMT0               0x03B0 /* select required output image format */
#define VS6624_BAYER_OUT_ALIGN0       0x03B2 /* set bayer output alignment */
#define VS6624_CONTRAST0              0x03B4 /* contrast control for output */
#define VS6624_SATURATION0            0x03B6 /* saturation control for output */
#define VS6624_GAMMA0                 0x03B8 /* gamma settings */
#define VS6624_HMIRROR0               0x03BA /* horizontal image orientation flip */
#define VS6624_VFLIP0                 0x03BC /* vertical image orientation flip */
#define VS6624_CHANNEL_ID0            0x03BE /* logical DMA channel number */
/* pipe setup bank1 */
#define VS6624_IMAGE_SIZE1            0x0400 /* required output dimension */
#define VS6624_MAN_HSIZE1_MSB         0x0403 /* input required manual H size MSB */
#define VS6624_MAN_HSIZE1_LSB         0x0404 /* input required manual H size LSB */
#define VS6624_MAN_VSIZE1_MSB         0x0407 /* input required manual V size MSB */
#define VS6624_MAN_VSIZE1_LSB         0x0408 /* input required manual V size LSB */
#define VS6624_ZOOM_HSTEP1_MSB        0x040B /* set the zoom H step MSB */
#define VS6624_ZOOM_HSTEP1_LSB        0x040C /* set the zoom H step LSB */
#define VS6624_ZOOM_VSTEP1_MSB        0x040F /* set the zoom V step MSB */
#define VS6624_ZOOM_VSTEP1_LSB        0x0410 /* set the zoom V step LSB */
#define VS6624_ZOOM_CTRL1             0x0412 /* control zoon in, out and stop */
#define VS6624_PAN_HSTEP1_MSB         0x0415 /* set the pan H step MSB */
#define VS6624_PAN_HSTEP1_LSB         0x0416 /* set the pan H step LSB */
#define VS6624_PAN_VSTEP1_MSB         0x0419 /* set the pan V step MSB */
#define VS6624_PAN_VSTEP1_LSB         0x041A /* set the pan V step LSB */
#define VS6624_PAN_CTRL1              0x041C /* control pan operation */
#define VS6624_CROP_CTRL1             0x041E /* select cropping mode */
#define VS6624_CROP_HSTART1_MSB       0x0421 /* set the cropping H start address MSB */
#define VS6624_CROP_HSTART1_LSB       0x0422 /* set the cropping H start address LSB */
#define VS6624_CROP_HSIZE1_MSB        0x0425 /* set the cropping H size MSB */
#define VS6624_CROP_HSIZE1_LSB        0x0426 /* set the cropping H size LSB */
#define VS6624_CROP_VSTART1_MSB       0x0429 /* set the cropping V start address MSB */
#define VS6624_CROP_VSTART1_LSB       0x042A /* set the cropping V start address LSB */
#define VS6624_CROP_VSIZE1_MSB        0x042D /* set the cropping V size MSB */
#define VS6624_CROP_VSIZE1_LSB        0x042E /* set the cropping V size LSB */
#define VS6624_IMG_FMT1               0x0430 /* select required output image format */
#define VS6624_BAYER_OUT_ALIGN1       0x0432 /* set bayer output alignment */
#define VS6624_CONTRAST1              0x0434 /* contrast control for output */
#define VS6624_SATURATION1            0x0436 /* saturation control for output */
#define VS6624_GAMMA1                 0x0438 /* gamma settings */
#define VS6624_HMIRROR1               0x043A /* horizontal image orientation flip */
#define VS6624_VFLIP1                 0x043C /* vertical image orientation flip */
#define VS6624_CHANNEL_ID1            0x043E /* logical DMA channel number */
/* view live control */
#define VS6624_VIEW_LIVE_EN           0x0480 /* enable view live mode */
#define VS6624_INIT_PIPE_SETUP        0x0482 /* select initial pipe setup bank */
/* view live status */
#define VS6624_CUR_PIPE_SETUP         0x0500 /* indicates most recently applied setup bank */
/* power management */
#define VS6624_TIME_TO_POWER_DOWN     0x0580 /* automatically transition time to stop mode */
/* video timing parameter host inputs */
#define VS6624_EXT_CLK_FREQ_NUM_MSB   0x0605 /* external clock frequency numerator MSB */
#define VS6624_EXT_CLK_FREQ_NUM_LSB   0x0606 /* external clock frequency numerator LSB */
#define VS6624_EXT_CLK_FREQ_DEN       0x0608 /* external clock frequency denominator */
/* video timing control */
#define VS6624_SYS_CLK_MODE           0x0880 /* decides system clock frequency */
/* frame dimension parameter host inputs */
#define VS6624_LIGHT_FREQ             0x0C80 /* AC frequency used for flicker free time */
#define VS6624_FLICKER_COMPAT         0x0C82 /* flicker compatible frame length */
/* static frame rate control */
#define VS6624_FR_NUM_MSB             0x0D81 /* desired frame rate numerator MSB */
#define VS6624_FR_NUM_LSB             0x0D82 /* desired frame rate numerator LSB */
#define VS6624_FR_DEN                 0x0D84 /* desired frame rate denominator */
/* automatic frame rate control */
#define VS6624_DISABLE_FR_DAMPER      0x0E80 /* defines frame rate mode */
#define VS6624_MIN_DAMPER_OUT_MSB     0x0E8C /* minimum frame rate MSB */
#define VS6624_MIN_DAMPER_OUT_LSB     0x0E8A /* minimum frame rate LSB */
/* exposure controls */
#define VS6624_EXPO_MODE              0x1180 /* exposure mode */
#define VS6624_EXPO_METER             0x1182 /* weights to be associated with the zones */
#define VS6624_EXPO_TIME_NUM          0x1184 /* exposure time numerator */
#define VS6624_EXPO_TIME_DEN          0x1186 /* exposure time denominator */
#define VS6624_EXPO_TIME_MSB          0x1189 /* exposure time for the Manual Mode MSB */
#define VS6624_EXPO_TIME_LSB          0x118A /* exposure time for the Manual Mode LSB */
#define VS6624_EXPO_COMPENSATION      0x1190 /* exposure compensation */
#define VS6624_DIRECT_COARSE_MSB      0x1195 /* coarse integration lines for Direct Mode MSB */
#define VS6624_DIRECT_COARSE_LSB      0x1196 /* coarse integration lines for Direct Mode LSB */
#define VS6624_DIRECT_FINE_MSB        0x1199 /* fine integration pixels for Direct Mode MSB */
#define VS6624_DIRECT_FINE_LSB        0x119A /* fine integration pixels for Direct Mode LSB */
#define VS6624_DIRECT_ANAL_GAIN_MSB   0x119D /* analog gain for Direct Mode MSB */
#define VS6624_DIRECT_ANAL_GAIN_LSB   0x119E /* analog gain for Direct Mode LSB */
#define VS6624_DIRECT_DIGI_GAIN_MSB   0x11A1 /* digital gain for Direct Mode MSB */
#define VS6624_DIRECT_DIGI_GAIN_LSB   0x11A2 /* digital gain for Direct Mode LSB */
#define VS6624_FLASH_COARSE_MSB       0x11A5 /* coarse integration lines for Flash Gun Mode MSB */
#define VS6624_FLASH_COARSE_LSB       0x11A6 /* coarse integration lines for Flash Gun Mode LSB */
#define VS6624_FLASH_FINE_MSB         0x11A9 /* fine integration pixels for Flash Gun Mode MSB */
#define VS6624_FLASH_FINE_LSB         0x11AA /* fine integration pixels for Flash Gun Mode LSB */
#define VS6624_FLASH_ANAL_GAIN_MSB    0x11AD /* analog gain for Flash Gun Mode MSB */
#define VS6624_FLASH_ANAL_GAIN_LSB    0x11AE /* analog gain for Flash Gun Mode LSB */
#define VS6624_FLASH_DIGI_GAIN_MSB    0x11B1 /* digital gain for Flash Gun Mode MSB */
#define VS6624_FLASH_DIGI_GAIN_LSB    0x11B2 /* digital gain for Flash Gun Mode LSB */
#define VS6624_FREEZE_AE              0x11B4 /* freeze auto exposure */
#define VS6624_MAX_INT_TIME_MSB       0x11B7 /* user maximum integration time MSB */
#define VS6624_MAX_INT_TIME_LSB       0x11B8 /* user maximum integration time LSB */
#define VS6624_FLASH_AG_THR_MSB       0x11BB /* recommend flash gun analog gain threshold MSB */
#define VS6624_FLASH_AG_THR_LSB       0x11BC /* recommend flash gun analog gain threshold LSB */
#define VS6624_ANTI_FLICKER_MODE      0x11C0 /* anti flicker mode */
/* white balance control */
#define VS6624_WB_MODE                0x1480 /* set white balance mode */
#define VS6624_MAN_RG                 0x1482 /* user setting for red channel gain */
#define VS6624_MAN_GG                 0x1484 /* user setting for green channel gain */
#define VS6624_MAN_BG                 0x1486 /* user setting for blue channel gain */
#define VS6624_FLASH_RG_MSB           0x148B /* red gain for Flash Gun MSB */
#define VS6624_FLASH_RG_LSB           0x148C /* red gain for Flash Gun LSB */
#define VS6624_FLASH_GG_MSB           0x148F /* green gain for Flash Gun MSB */
#define VS6624_FLASH_GG_LSB           0x1490 /* green gain for Flash Gun LSB */
#define VS6624_FLASH_BG_MSB           0x1493 /* blue gain for Flash Gun MSB */
#define VS6624_FLASH_BG_LSB           0x1494 /* blue gain for Flash Gun LSB */
/* sensor setup */
#define VS6624_BC_OFFSET              0x1990 /* Black Correction Offset */
/* image stability */
#define VS6624_STABLE_WB              0x1900 /* white balance stable */
#define VS6624_STABLE_EXPO            0x1902 /* exposure stable */
#define VS6624_STABLE                 0x1906 /* system stable */
/* flash control */
#define VS6624_FLASH_MODE             0x1A80 /* flash mode */
#define VS6624_FLASH_OFF_LINE_MSB     0x1A83 /* off line at flash pulse mode MSB */
#define VS6624_FLASH_OFF_LINE_LSB     0x1A84 /* off line at flash pulse mode LSB */
/* flash status */
#define VS6624_FLASH_RECOM            0x1B00 /* flash gun is recommended */
#define VS6624_FLASH_GRAB_COMPLETE    0x1B02 /* flash gun image has been grabbed */
/* scythe filter controls */
#define VS6624_SCYTHE_FILTER          0x1D80 /* disable scythe defect correction */
/* jack filter controls */
#define VS6624_JACK_FILTER            0x1E00 /* disable jack defect correction */
/* demosaic control */
#define VS6624_ANTI_ALIAS_FILTER      0x1E80 /* anti alias filter suppress */
/* color matrix dampers */
#define VS6624_CM_DISABLE             0x1F00 /* disable color matrix damper */
#define VS6624_CM_LOW_THR_MSB         0x1F03 /* low threshold for exposure MSB */
#define VS6624_CM_LOW_THR_LSB         0x1F04 /* low threshold for exposure LSB */
#define VS6624_CM_HIGH_THR_MSB        0x1F07 /* high threshold for exposure MSB */
#define VS6624_CM_HIGH_THR_LSB        0x1F08 /* high threshold for exposure LSB */
#define VS6624_CM_MIN_OUT_MSB         0x1F0B /* minimum possible damper output MSB */
#define VS6624_CM_MIN_OUT_LSB         0x1F0C /* minimum possible damper output LSB */
/* peaking control */
#define VS6624_PEAK_GAIN              0x2000 /* controls peaking gain */
#define VS6624_PEAK_G_DISABLE         0x2002 /* disable peak gain damping */
#define VS6624_PEAK_LOW_THR_G_MSB     0x2005 /* low threshold for exposure for gain MSB */
#define VS6624_PEAK_LOW_THR_G_LSB     0x2006 /* low threshold for exposure for gain LSB */
#define VS6624_PEAK_HIGH_THR_G_MSB    0x2009 /* high threshold for exposure for gain MSB */
#define VS6624_PEAK_HIGH_THR_G_LSB    0x200A /* high threshold for exposure for gain LSB */
#define VS6624_PEAK_MIN_OUT_G_MSB     0x200D /* minimum damper output for gain MSB */
#define VS6624_PEAK_MIN_OUT_G_LSB     0x200E /* minimum damper output for gain LSB */
#define VS6624_PEAK_LOW_THR           0x2010 /* adjust degree of coring */
#define VS6624_PEAK_C_DISABLE         0x2012 /* disable coring damping */
#define VS6624_PEAK_HIGH_THR          0x2014 /* adjust maximum gain */
#define VS6624_PEAK_LOW_THR_C_MSB     0x2017 /* low threshold for exposure for coring MSB */
#define VS6624_PEAK_LOW_THR_C_LSB     0x2018 /* low threshold for exposure for coring LSB */
#define VS6624_PEAK_HIGH_THR_C_MSB    0x201B /* high threshold for exposure for coring MSB */
#define VS6624_PEAK_HIGH_THR_C_LSB    0x201C /* high threshold for exposure for coring LSB */
#define VS6624_PEAK_MIN_OUT_C_MSB     0x201F /* minimum damper output for coring MSB */
#define VS6624_PEAK_MIN_OUT_C_LSB     0x2020 /* minimum damper output for coring LSB */
/* pipe 0 RGB to YUV matrix manual control */
#define VS6624_RYM0_MAN_CTRL          0x2180 /* enable manual RGB to YUV matrix */
#define VS6624_RYM0_W00_MSB           0x2183 /* row 0 column 0 of YUV matrix MSB */
#define VS6624_RYM0_W00_LSB           0x2184 /* row 0 column 0 of YUV matrix LSB */
#define VS6624_RYM0_W01_MSB           0x2187 /* row 0 column 1 of YUV matrix MSB */
#define VS6624_RYM0_W01_LSB           0x2188 /* row 0 column 1 of YUV matrix LSB */
#define VS6624_RYM0_W02_MSB           0x218C /* row 0 column 2 of YUV matrix MSB */
#define VS6624_RYM0_W02_LSB           0x218D /* row 0 column 2 of YUV matrix LSB */
#define VS6624_RYM0_W10_MSB           0x2190 /* row 1 column 0 of YUV matrix MSB */
#define VS6624_RYM0_W10_LSB           0x218F /* row 1 column 0 of YUV matrix LSB */
#define VS6624_RYM0_W11_MSB           0x2193 /* row 1 column 1 of YUV matrix MSB */
#define VS6624_RYM0_W11_LSB           0x2194 /* row 1 column 1 of YUV matrix LSB */
#define VS6624_RYM0_W12_MSB           0x2197 /* row 1 column 2 of YUV matrix MSB */
#define VS6624_RYM0_W12_LSB           0x2198 /* row 1 column 2 of YUV matrix LSB */
#define VS6624_RYM0_W20_MSB           0x219B /* row 2 column 0 of YUV matrix MSB */
#define VS6624_RYM0_W20_LSB           0x219C /* row 2 column 0 of YUV matrix LSB */
#define VS6624_RYM0_W21_MSB           0x21A0 /* row 2 column 1 of YUV matrix MSB */
#define VS6624_RYM0_W21_LSB           0x219F /* row 2 column 1 of YUV matrix LSB */
#define VS6624_RYM0_W22_MSB           0x21A3 /* row 2 column 2 of YUV matrix MSB */
#define VS6624_RYM0_W22_LSB           0x21A4 /* row 2 column 2 of YUV matrix LSB */
#define VS6624_RYM0_YINY_MSB          0x21A7 /* Y in Y MSB */
#define VS6624_RYM0_YINY_LSB          0x21A8 /* Y in Y LSB */
#define VS6624_RYM0_YINCB_MSB         0x21AB /* Y in Cb MSB */
#define VS6624_RYM0_YINCB_LSB         0x21AC /* Y in Cb LSB */
#define VS6624_RYM0_YINCR_MSB         0x21B0 /* Y in Cr MSB */
#define VS6624_RYM0_YINCR_LSB         0x21AF /* Y in Cr LSB */
/* pipe 1 RGB to YUV matrix manual control */
#define VS6624_RYM1_MAN_CTRL          0x2200 /* enable manual RGB to YUV matrix */
#define VS6624_RYM1_W00_MSB           0x2203 /* row 0 column 0 of YUV matrix MSB */
#define VS6624_RYM1_W00_LSB           0x2204 /* row 0 column 0 of YUV matrix LSB */
#define VS6624_RYM1_W01_MSB           0x2207 /* row 0 column 1 of YUV matrix MSB */
#define VS6624_RYM1_W01_LSB           0x2208 /* row 0 column 1 of YUV matrix LSB */
#define VS6624_RYM1_W02_MSB           0x220C /* row 0 column 2 of YUV matrix MSB */
#define VS6624_RYM1_W02_LSB           0x220D /* row 0 column 2 of YUV matrix LSB */
#define VS6624_RYM1_W10_MSB           0x2210 /* row 1 column 0 of YUV matrix MSB */
#define VS6624_RYM1_W10_LSB           0x220F /* row 1 column 0 of YUV matrix LSB */
#define VS6624_RYM1_W11_MSB           0x2213 /* row 1 column 1 of YUV matrix MSB */
#define VS6624_RYM1_W11_LSB           0x2214 /* row 1 column 1 of YUV matrix LSB */
#define VS6624_RYM1_W12_MSB           0x2217 /* row 1 column 2 of YUV matrix MSB */
#define VS6624_RYM1_W12_LSB           0x2218 /* row 1 column 2 of YUV matrix LSB */
#define VS6624_RYM1_W20_MSB           0x221B /* row 2 column 0 of YUV matrix MSB */
#define VS6624_RYM1_W20_LSB           0x221C /* row 2 column 0 of YUV matrix LSB */
#define VS6624_RYM1_W21_MSB           0x2220 /* row 2 column 1 of YUV matrix MSB */
#define VS6624_RYM1_W21_LSB           0x221F /* row 2 column 1 of YUV matrix LSB */
#define VS6624_RYM1_W22_MSB           0x2223 /* row 2 column 2 of YUV matrix MSB */
#define VS6624_RYM1_W22_LSB           0x2224 /* row 2 column 2 of YUV matrix LSB */
#define VS6624_RYM1_YINY_MSB          0x2227 /* Y in Y MSB */
#define VS6624_RYM1_YINY_LSB          0x2228 /* Y in Y LSB */
#define VS6624_RYM1_YINCB_MSB         0x222B /* Y in Cb MSB */
#define VS6624_RYM1_YINCB_LSB         0x222C /* Y in Cb LSB */
#define VS6624_RYM1_YINCR_MSB         0x2220 /* Y in Cr MSB */
#define VS6624_RYM1_YINCR_LSB         0x222F /* Y in Cr LSB */
/* pipe 0 gamma manual control */
#define VS6624_GAMMA_MAN_CTRL0        0x2280 /* enable manual gamma setup */
#define VS6624_GAMMA_PEAK_R0          0x2282 /* peaked red channel gamma value */
#define VS6624_GAMMA_PEAK_G0          0x2284 /* peaked green channel gamma value */
#define VS6624_GAMMA_PEAK_B0          0x2286 /* peaked blue channel gamma value */
#define VS6624_GAMMA_UNPEAK_R0        0x2288 /* unpeaked red channel gamma value */
#define VS6624_GAMMA_UNPEAK_G0        0x228A /* unpeaked green channel gamma value */
#define VS6624_GAMMA_UNPEAK_B0        0x228C /* unpeaked blue channel gamma value */
/* pipe 1 gamma manual control */
#define VS6624_GAMMA_MAN_CTRL1        0x2300 /* enable manual gamma setup */
#define VS6624_GAMMA_PEAK_R1          0x2302 /* peaked red channel gamma value */
#define VS6624_GAMMA_PEAK_G1          0x2304 /* peaked green channel gamma value */
#define VS6624_GAMMA_PEAK_B1          0x2306 /* peaked blue channel gamma value */
#define VS6624_GAMMA_UNPEAK_R1        0x2308 /* unpeaked red channel gamma value */
#define VS6624_GAMMA_UNPEAK_G1        0x230A /* unpeaked green channel gamma value */
#define VS6624_GAMMA_UNPEAK_B1        0x230C /* unpeaked blue channel gamma value */
/* fade to black */
#define VS6624_F2B_DISABLE            0x2480 /* disable fade to black */
#define VS6624_F2B_BLACK_VAL_MSB      0x2483 /* black value MSB */
#define VS6624_F2B_BLACK_VAL_LSB      0x2484 /* black value LSB */
#define VS6624_F2B_LOW_THR_MSB        0x2487 /* low threshold for exposure MSB */
#define VS6624_F2B_LOW_THR_LSB        0x2488 /* low threshold for exposure LSB */
#define VS6624_F2B_HIGH_THR_MSB       0x248B /* high threshold for exposure MSB */
#define VS6624_F2B_HIGH_THR_LSB       0x248C /* high threshold for exposure LSB */
#define VS6624_F2B_MIN_OUT_MSB        0x248F /* minimum damper output MSB */
#define VS6624_F2B_MIN_OUT_LSB        0x2490 /* minimum damper output LSB */
/* output formatter control */
#define VS6624_CODE_CK_EN             0x2580 /* code check enable */
#define VS6624_BLANK_FMT              0x2582 /* blank format */
#define VS6624_SYNC_CODE_SETUP        0x2584 /* sync code setup */
#define VS6624_HSYNC_SETUP            0x2586 /* H sync setup */
#define VS6624_VSYNC_SETUP            0x2588 /* V sync setup */
#define VS6624_PCLK_SETUP             0x258A /* PCLK setup */
#define VS6624_PCLK_EN                0x258C /* PCLK enable */
#define VS6624_OPF_SP_SETUP           0x258E /* output formatter sp setup */
#define VS6624_BLANK_DATA_MSB         0x2590 /* blank data MSB */
#define VS6624_BLANK_DATA_LSB         0x2592 /* blank data LSB */
#define VS6624_RGB_SETUP              0x2594 /* RGB setup */
#define VS6624_YUV_SETUP              0x2596 /* YUV setup */
#define VS6624_VSYNC_RIS_COARSE_H     0x2598 /* V sync rising coarse high */
#define VS6624_VSYNC_RIS_COARSE_L     0x259A /* V sync rising coarse low */
#define VS6624_VSYNC_RIS_FINE_H       0x259C /* V sync rising fine high */
#define VS6624_VSYNC_RIS_FINE_L       0x259E /* V sync rising fine low */
#define VS6624_VSYNC_FALL_COARSE_H    0x25A0 /* V sync falling coarse high */
#define VS6624_VSYNC_FALL_COARSE_L    0x25A2 /* V sync falling coarse low */
#define VS6624_VSYNC_FALL_FINE_H      0x25A4 /* V sync falling fine high */
#define VS6624_VSYNC_FALL_FINE_L      0x25A6 /* V sync falling fine low */
#define VS6624_HSYNC_RIS_H            0x25A8 /* H sync rising high */
#define VS6624_HSYNC_RIS_L            0x25AA /* H sync rising low */
#define VS6624_HSYNC_FALL_H           0x25AC /* H sync falling high */
#define VS6624_HSYNC_FALL_L           0x25AE /* H sync falling low */
#define VS6624_OUT_IF                 0x25B0 /* output interface */
#define VS6624_CCP_EXT_DATA           0x25B2 /* CCP extra data */
/* NoRA controls */
#define VS6624_NORA_DISABLE           0x2600 /* NoRA control mode */
#define VS6624_NORA_USAGE             0x2602 /* usage */
#define VS6624_NORA_SPLIT_KN          0x2604 /* split kn */
#define VS6624_NORA_SPLIT_NI          0x2606 /* split ni */
#define VS6624_NORA_TIGHT_G           0x2608 /* tight green */
#define VS6624_NORA_DISABLE_NP        0x260A /* disable noro promoting */
#define VS6624_NORA_LOW_THR_MSB       0x260D /* low threshold for exposure MSB */
#define VS6624_NORA_LOW_THR_LSB       0x260E /* low threshold for exposure LSB */
#define VS6624_NORA_HIGH_THR_MSB      0x2611 /* high threshold for exposure MSB */
#define VS6624_NORA_HIGH_THR_LSB      0x2612 /* high threshold for exposure LSB */
#define VS6624_NORA_MIN_OUT_MSB       0x2615 /* minimum damper output MSB */
#define VS6624_NORA_MIN_OUT_LSB       0x2616 /* minimum damper output LSB */

#endif
