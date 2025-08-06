/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (c) 2024 Amlogic, Inc. All rights reserved.
 */

#ifndef __DTS_AMLOGIC_A5_RESET_H
#define __DTS_AMLOGIC_A5_RESET_H

/* RESET0 */
/*						0-3 */
#define RESET_USB				4
/*						5-7 */
#define RESET_USBPHY20				8
/*						9 */
#define RESET_USB2DRD				10
/*						11-31 */

/* RESET1 */
#define RESET_AUDIO				32
#define RESET_AUDIO_VAD				33
/*                                              34 */
#define RESET_DDR_APB				35
#define RESET_DDR				36
/*						37-40 */
#define RESET_DSPA_DEBUG			41
/*                                              42 */
#define RESET_DSPA				43
/*						44-46 */
#define RESET_NNA				47
#define RESET_ETHERNET				48
/*						49-63 */

/* RESET2 */
#define RESET_ABUS_ARB				64
#define RESET_IRCTRL				65
/*						66 */
#define RESET_TS_PLL				67
/*						68-72 */
#define RESET_SPICC_0				73
#define RESET_SPICC_1				74
#define RESET_RSA				75

/*						76-79 */
#define RESET_MSR_CLK				80
#define RESET_SPIFC				81
#define RESET_SAR_ADC				82
/*						83-90 */
#define RESET_WATCHDOG				91
/*						92-95 */

/* RESET3 */
/*						96-127 */

/* RESET4 */
#define RESET_RTC				128
/*						129-131 */
#define RESET_PWM_AB				132
#define RESET_PWM_CD				133
#define RESET_PWM_EF				134
#define RESET_PWM_GH				135
/*						104-105 */
#define RESET_UART_A				138
#define RESET_UART_B				139
#define RESET_UART_C				140
#define RESET_UART_D				141
#define RESET_UART_E				142
/*						143*/
#define RESET_I2C_S_A				144
#define RESET_I2C_M_A				145
#define RESET_I2C_M_B				146
#define RESET_I2C_M_C				147
#define RESET_I2C_M_D				148
/*						149-151 */
#define RESET_SDEMMC_A				152
/*						153 */
#define RESET_SDEMMC_C				154
/*						155-159*/

/* RESET5 */
/*						160-175 */
#define RESET_BRG_AO_NIC_SYS			176
#define RESET_BRG_AO_NIC_DSPA			177
#define RESET_BRG_AO_NIC_MAIN			178
#define RESET_BRG_AO_NIC_AUDIO			179
/*						180-183 */
#define RESET_BRG_AO_NIC_ALL			184
#define RESET_BRG_NIC_NNA			185
#define RESET_BRG_NIC_SDIO			186
#define RESET_BRG_NIC_EMMC			187
#define RESET_BRG_NIC_DSU			188
#define RESET_BRG_NIC_SYSCLK			189
#define RESET_BRG_NIC_MAIN			190
#define RESET_BRG_NIC_ALL			191

#endif
