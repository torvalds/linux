/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __HAL8188EPWRSEQ_H__
#define __HAL8188EPWRSEQ_H__

#include "HalPwrSeqCmd.h"

#define RTL8188E_TRANS_CARDEMU_TO_ACT														\
	/* format */																\
	/* { offset, fab_msk|interface_msk, base|cmd, msk, value }, comments here*/								\
	{0x0006, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), BIT(1)},/* wait till 0x04[17] = 1    power ready*/	\
	{0x0002, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0)|BIT(1), 0}, /* 0x02[1:0] = 0	reset BB*/			\
	{0x0026, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), BIT(7)}, /*0x24[23] = 2b'01 schmit trigger */	\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0}, /* 0x04[15] = 0 disable HWPDN (control by DRV)*/\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4)|BIT(3), 0}, /*0x04[12:11] = 2b'00 disable WL suspend*/	\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)}, /*0x04[8] = 1 polling until return 0*/	\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(0), 0}, /*wait till 0x04[8] = 0*/	\
	{0x0023, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0}, /*LDO normal mode*/	\
	{0x0074, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)}, /*SDIO Driving*/	\

#define RTL8188E_TRANS_ACT_TO_CARDEMU													\
	/* format */																\
	/* { offset, fab_msk|interface_msk, base|cmd, msk, value }, comments here*/								\
	{0x001F, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},/*0x1F[7:0] = 0 turn off RF*/	\
	{0x0023, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)}, /*LDO Sleep mode*/	\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)}, /*0x04[9] = 1 turn off MAC by HW state machine*/	\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), 0}, /*wait till 0x04[9] = 0 polling until return 0 to disable*/	\

#define RTL8188E_TRANS_CARDEMU_TO_CARDDIS													\
	/* format */																\
	/* { offset, fab_msk|interface_msk, base|cmd, msk, value },  comments here*/							\
	{0x0026, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), BIT(7)}, /*0x24[23] = 2b'01 schmit trigger */	\
	{0x0005, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), BIT(3)}, /*0x04[12:11] = 2b'01 enable WL suspend*/	\
	{0x0007, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0}, /*  0x04[31:30] = 2b'10 enable enable bandgap mbias in suspend */	\
	{0x0041, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0}, /*Clear SIC_EN register 0x40[12] = 1'b0 */	\
	{0xfe10, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)}, /*Set USB suspend enable local register  0xfe10[4]=1 */	\
	{0x0086, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), BIT(0)}, /*Set SDIO suspend local register*/	\
	{0x0086, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), 0}, /*wait power state to suspend*/

/* This is used by driver for LPSRadioOff Procedure, not for FW LPS Step */
#define RTL8188E_TRANS_ACT_TO_LPS														\
	/* format */																\
	/* { offset, fab_msk|interface_msk, base|cmd, msk, value }, comments here				*/   \
	{0x0522, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x7F},/*Tx Pause*/	\
	{0x05F8, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},/*Should be zero if no packet is transmitting*/	\
	{0x05F9, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},/*Should be zero if no packet is transmitting*/	\
	{0x05FA, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},/*Should be zero if no packet is transmitting*/	\
	{0x05FB, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},/*Should be zero if no packet is transmitting*/	\
	{0x0002, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},/*CCK and OFDM are disabled,and clock are gated*/	\
	{0x0002, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_DELAY, 0, PWRSEQ_DELAY_US},/*Delay 1us*/	\
	{0x0100, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x3F},/*Reset MAC TRX*/	\
	{0x0101, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), 0},/*check if removed later*/	\
	{0x0553, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(5), BIT(5)},/*Respond TxOK to scheduler*/	\

#define RTL8188E_TRANS_END															\
	/* format */																\
	/* { offset, fab_msk|interface_msk, base|cmd, msk, value },  comments here*/					\
	{0xFFFF, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,0, PWR_CMD_END, 0, 0}, /*  */

extern struct wl_pwr_cfg rtl8188E_power_on_flow[];
extern struct wl_pwr_cfg rtl8188E_card_disable_flow[];
extern struct wl_pwr_cfg rtl8188E_enter_lps_flow[];

#endif /* __HAL8188EPWRSEQ_H__ */
