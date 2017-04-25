/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DTS_IMX6ULL_PINFUNC_H
#define __DTS_IMX6ULL_PINFUNC_H

#include "imx6ul-pinfunc.h"
/*
 * The pin function ID is a tuple of
 * <mux_reg conf_reg input_reg mux_mode input_val>
 */
#define MX6ULL_PAD_ENET2_RX_DATA0__EPDC_SDDO08                    0x00E4 0x0370 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_RX_DATA1__EPDC_SDDO09                    0x00E8 0x0374 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_RX_EN__EPDC_SDDO10                       0x00EC 0x0378 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_TX_DATA0__EPDC_SDDO11                    0x00F0 0x037C 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_TX_DATA1__EPDC_SDDO12                    0x00F4 0x0380 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_TX_EN__EPDC_SDDO13                       0x00F8 0x0384 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_TX_CLK__EPDC_SDDO14                      0x00FC 0x0388 0x0000 0x9 0x0
#define MX6ULL_PAD_ENET2_RX_ER__EPDC_SDDO15                       0x0100 0x038C 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_CLK__EPDC_SDCLK                            0x0104 0x0390 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_ENABLE__EPDC_SDLE                          0x0108 0x0394 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_HSYNC__EPDC_SDOE                           0x010C 0x0398 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_VSYNC__EPDC_SDCE0                          0x0110 0x039C 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_RESET__EPDC_GDOE                           0x0114 0x03A0 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA00__EPDC_SDDO00                        0x0118 0x03A4 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA01__EPDC_SDDO01                        0x011C 0x03A8 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA02__EPDC_SDDO02                        0x0120 0x03AC 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA03__EPDC_SDDO03                        0x0124 0x03B0 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA04__EPDC_SDDO04                        0x0128 0x03B4 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA05__EPDC_SDDO05                        0x012C 0x03B8 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA06__EPDC_SDDO06                        0x0130 0x03BC 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA07__EPDC_SDDO07                        0x0134 0x03C0 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA14__EPDC_SDSHR                         0x0150 0x03DC 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA15__EPDC_GDRL                          0x0154 0x03E0 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA16__EPDC_GDCLK                         0x0158 0x03E4 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA17__EPDC_GDSP                          0x015C 0x03E8 0x0000 0x9 0x0
#define MX6ULL_PAD_LCD_DATA21__EPDC_SDCE1                         0x016C 0x03F8 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_MCLK__ESAI_TX3_RX2                         0x01D4 0x0460 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_PIXCLK__ESAI_TX2_RX3                       0x01D8 0x0464 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_VSYNC__ESAI_TX4_RX1                        0x01DC 0x0468 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_HSYNC__ESAI_TX1                            0x01E0 0x046C 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA00__ESAI_TX_HF_CLK                     0x01E4 0x0470 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA01__ESAI_RX_HF_CLK                     0x01E8 0x0474 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA02__ESAI_RX_FS                         0x01EC 0x0478 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA03__ESAI_RX_CLK                        0x01F0 0x047C 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA04__ESAI_TX_FS                         0x01F4 0x0480 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA05__ESAI_TX_CLK                        0x01F8 0x0484 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA06__ESAI_TX5_RX0                       0x01FC 0x0488 0x0000 0x9 0x0
#define MX6ULL_PAD_CSI_DATA07__ESAI_T0                            0x0200 0x048C 0x0000 0x9 0x0

#endif /* __DTS_IMX6ULL_PINFUNC_H */
