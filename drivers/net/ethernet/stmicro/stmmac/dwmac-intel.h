/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020, Intel Corporation
 * DWMAC Intel header file
 */

#ifndef __DWMAC_INTEL_H__
#define __DWMAC_INTEL_H__

#define POLL_DELAY_US 8

/* SERDES Register */
#define SERDES_GSR0	0x5	/* Global Status Reg0 */
#define SERDES_GCR0	0xb	/* Global Configuration Reg0 */

/* SERDES defines */
#define SERDES_PLL_CLK		BIT(0)		/* PLL clk valid signal */
#define SERDES_RST		BIT(2)		/* Serdes Reset */
#define SERDES_PWR_ST_MASK	GENMASK(6, 4)	/* Serdes Power state*/
#define SERDES_PWR_ST_SHIFT	4
#define SERDES_PWR_ST_P0	0x0
#define SERDES_PWR_ST_P3	0x3

#endif /* __DWMAC_INTEL_H__ */
