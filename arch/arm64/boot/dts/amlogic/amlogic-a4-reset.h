/* SPDX-License-Identifier: (GPL-2.0-only OR MIT) */
/*
 * Copyright (c) 2024 Amlogic, Inc. All rights reserved.
 */

#ifndef __DTS_AMLOGIC_A4_RESET_H
#define __DTS_AMLOGIC_A4_RESET_H

/* RESET0 */
/*						0-3 */
#define RESET_USB				4
/*						5-6*/
#define RESET_U2PHY22				7
#define RESET_USBPHY20				8
#define RESET_U2PHY21				9
#define RESET_USB2DRD				10
#define RESET_U2H				11
#define RESET_LED_CTRL				12
/*						13-31 */

/* RESET1 */
#define RESET_AUDIO				32
#define RESET_AUDIO_VAD				33
/*						34*/
#define RESET_DDR_APB				35
#define RESET_DDR				36
#define RESET_VOUT_VENC				37
#define RESET_VOUT				38
/*						39-47 */
#define RESET_ETHERNET				48
/*						49-63 */

/* RESET2 */
#define RESET_DEVICE_MMC_ARB			64
#define RESET_IRCTRL				65
/*						66*/
#define RESET_TS_PLL				67
/*						68-72*/
#define RESET_SPICC_0				73
#define RESET_SPICC_1				74
/*						75-79*/
#define RESET_MSR_CLK				80
/*						81*/
#define RESET_SAR_ADC				82
/*						83-87*/
#define RESET_ACODEC				88
/*						89-90*/
#define RESET_WATCHDOG				91
/*						92-95*/

/* RESET3 */
/*						96-127 */

/* RESET4 */
/*						128-131 */
#define RESET_PWM_AB				132
#define RESET_PWM_CD				133
#define RESET_PWM_EF				134
#define RESET_PWM_GH				135
/*						136-137*/
#define RESET_UART_A				138
#define RESET_UART_B				139
/*						140*/
#define RESET_UART_D				141
#define RESET_UART_E				142
/*						143-144*/
#define RESET_I2C_M_A				145
#define RESET_I2C_M_B				146
#define RESET_I2C_M_C				147
#define RESET_I2C_M_D				148
/*						149-151*/
#define RESET_SDEMMC_A				152
/*						153*/
#define RESET_SDEMMC_C				154
/*						155-159*/

/* RESET5 */
/*						160-175*/
#define RESET_BRG_AO_NIC_SYS			176
/*						177*/
#define RESET_BRG_AO_NIC_MAIN			178
#define RESET_BRG_AO_NIC_AUDIO			179
/*						180-183*/
#define RESET_BRG_AO_NIC_ALL			184
/*						185*/
#define RESET_BRG_NIC_SDIO			186
#define RESET_BRG_NIC_EMMC			187
#define RESET_BRG_NIC_DSU			188
#define RESET_BRG_NIC_CLK81			189
#define RESET_BRG_NIC_MAIN			190
#define RESET_BRG_NIC_ALL			191

#endif
