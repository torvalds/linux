/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8723E_PWRSEQ_H__
#define __RTL8723E_PWRSEQ_H__

#include "pwrseqcmd.h"
/*
	Check document WM-20110607-Paul-RTL8188E_Power_Architecture-R02.vsd
	There are 6 HW Power States:
	0: POFF--Power Off
	1: PDN--Power Down
	2: CARDEMU--Card Emulation
	3: ACT--Active Mode
	4: LPS--Low Power State
	5: SUS--Suspend

	The transision from different states are defined below
	TRANS_CARDEMU_TO_ACT
	TRANS_ACT_TO_CARDEMU
	TRANS_CARDEMU_TO_SUS
	TRANS_SUS_TO_CARDEMU
	TRANS_CARDEMU_TO_PDN
	TRANS_ACT_TO_LPS
	TRANS_LPS_TO_ACT

	TRANS_END
	PWR SEQ Version: rtl8188e_PwrSeq_V09.h
*/

#define	RTL8188E_TRANS_CARDEMU_TO_ACT_STEPS	10
#define	RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS	10
#define	RTL8188E_TRANS_CARDEMU_TO_SUS_STEPS	10
#define	RTL8188E_TRANS_SUS_TO_CARDEMU_STEPS	10
#define	RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS	10
#define	RTL8188E_TRANS_PDN_TO_CARDEMU_STEPS	10
#define	RTL8188E_TRANS_ACT_TO_LPS_STEPS		15
#define	RTL8188E_TRANS_LPS_TO_ACT_STEPS		15
#define	RTL8188E_TRANS_END_STEPS		1


#define RTL8188E_TRANS_CARDEMU_TO_ACT					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/* wait till 0x04[17] = 1    power ready*/			\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), BIT(1)},		\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/* 0x02[1:0] = 0	reset BB*/				\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0)|BIT(1), 0},		\
	{0x0026, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*0x24[23] = 2b'01 schmit trigger */				\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), BIT(7)},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/* 0x04[15] = 0 disable HWPDN (control by DRV)*/		\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0},			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*0x04[12:11] = 2b'00 disable WL suspend*/			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4)|BIT(3), 0},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*0x04[8] = 1 polling until return 0*/				\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*wait till 0x04[8] = 0*/					\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(0), 0},			\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0}, /*LDO normal mode*/\
	{0x0074, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)}, /*SDIO Driving*/\

#define RTL8188E_TRANS_ACT_TO_CARDEMU					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/\
	{0x001F, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},/*0x1F[7:0] = 0 turn off RF*/\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)}, /*LDO Sleep mode*/\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*0x04[9] = 1 turn off MAC by HW state machine*/		\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*wait till 0x04[9] = 0 polling until return 0 to disable*/	\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), 0},			\


#define RTL8188E_TRANS_CARDEMU_TO_SUS					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*0x04[12:11] = 2b'01enable WL suspend*/			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), BIT(3)},	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	/*0x04[12:11] = 2b'11enable WL suspend for PCIe*/		\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), BIT(3)|BIT(4)},\
	{0x0007, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*  0x04[31:30] = 2b'10 enable enable bandgap mbias in suspend */\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, BIT(7)},			\
	{0x0041, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*Clear SIC_EN register 0x40[12] = 1'b0 */			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	{0xfe10, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*Set USB suspend enable local register  0xfe10[4]= 1 */	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)},		\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	/*Set SDIO suspend local register*/				\
	PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	/*wait power state to suspend*/					\
	PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), 0},

#define RTL8188E_TRANS_SUS_TO_CARDEMU					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	/*Set SDIO suspend local register*/				\
	PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), 0},			\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	/*wait power state to suspend*/					\
	PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), BIT(1)},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*0x04[12:11] = 2b'01enable WL suspend*/			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), 0},

#define RTL8188E_TRANS_CARDEMU_TO_CARDDIS				\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0x0026, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*0x24[23] = 2b'01 schmit trigger */				\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), BIT(7)},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*0x04[12:11] = 2b'01 enable WL suspend*/			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), BIT(3)},	\
	{0x0007, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*  0x04[31:30] = 2b'10 enable enable bandgap mbias in suspend */\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},			\
	{0x0041, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK,				\
	/*Clear SIC_EN register 0x40[12] = 1'b0 */			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	{0xfe10, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK,	\
	/*Set USB suspend enable local register  0xfe10[4]= 1 */	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)},		\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	/*Set SDIO suspend local register*/				\
	PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	PWR_CMD_POLLING, BIT(1), 0}, /*wait power state to suspend*/

#define RTL8188E_TRANS_CARDDIS_TO_CARDEMU				\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	PWR_BASEADDR_SDIO,\
	PWR_CMD_WRITE, BIT(0), 0}, /*Set SDIO suspend local register*/	\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	PWR_BASEADDR_SDIO,\
	PWR_CMD_POLLING, BIT(1), BIT(1)}, /*wait power state to suspend*/\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC,						\
	PWR_CMD_WRITE, BIT(3)|BIT(4), 0},				\
	/*0x04[12:11] = 2b'01enable WL suspend*/


#define RTL8188E_TRANS_CARDEMU_TO_PDN					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},/* 0x04[16] = 0*/	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), BIT(7)},/* 0x04[15] = 1*/


#define RTL8188E_TRANS_PDN_TO_CARDEMU					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0},/* 0x04[15] = 0*/


#define RTL8188E_TRANS_ACT_TO_LPS					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/\
	{0x0522, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x7F},/*Tx Pause*/	\
	{0x05F8, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*zero if no pkt is tx*/\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	{0x05F9, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*Should be zero if no packet is transmitting*/	\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	{0x05FA, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*Should be zero if no packet is transmitting*/			\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	{0x05FB, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*Should be zero if no packet is transmitting*/			\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*CCK and OFDM are disabled, and clock are gated*/		\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},			\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_DELAY, 0, PWRSEQ_DELAY_US},/*Delay 1us*/\
	{0x0100, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x3F},/*Reset MAC TRX*/	\
	{0x0101, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*check if removed later*/					\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), 0},			\
	{0x0553, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*Respond TxOK to scheduler*/					\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(5), BIT(5)},		\


#define RTL8188E_TRANS_LPS_TO_ACT					\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0x0080, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	PWR_BASEADDR_SDIO, PWR_CMD_WRITE, 0xFF, 0x84}, /*SDIO RPWM*/	\
	{0xFE58, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x84}, /*USB RPWM*/	\
	{0x0361, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x84}, /*PCIe RPWM*/	\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	PWR_BASEADDR_MAC, PWR_CMD_DELAY, 0, PWRSEQ_DELAY_MS}, /*Delay*/	\
	{0x0008, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*.	0x08[4] = 0		 switch TSF to 40M*/		\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	{0x0109, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*Polling 0x109[7]= 0  TSF in 40M*/				\
	PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(7), 0},			\
	{0x0029, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*. 0x29[7:6] = 2b'00	 enable BB clock*/			\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(6)|BIT(7), 0},		\
	{0x0101, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*.	0x101[1] = 1*/\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	{0x0100, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*.	0x100[7:0] = 0xFF	 enable WMAC TRX*/\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0xFF},			\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	/*. 0x02[1:0] = 2b'11  enable BB macro*/\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1)|BIT(0), BIT(1)|BIT(0)},	\
	{0x0522, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
	PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0}, /*.	0x522 = 0*/


#define RTL8188E_TRANS_END						\
	/* format */							\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/\
	{0xFFFF, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
	0, PWR_CMD_END, 0, 0}

extern struct wlan_pwr_cfg rtl8188e_power_on_flow
		[RTL8188E_TRANS_CARDEMU_TO_ACT_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_radio_off_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_card_disable_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_card_enable_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_suspend_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		RTL8188E_TRANS_CARDEMU_TO_SUS_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_resume_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		RTL8188E_TRANS_CARDEMU_TO_SUS_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_hwpdn_flow
		[RTL8188E_TRANS_ACT_TO_CARDEMU_STEPS +
		RTL8188E_TRANS_CARDEMU_TO_PDN_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_enter_lps_flow
		[RTL8188E_TRANS_ACT_TO_LPS_STEPS +
		RTL8188E_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8188e_leave_lps_flow
		[RTL8188E_TRANS_LPS_TO_ACT_STEPS +
		RTL8188E_TRANS_END_STEPS];

/* RTL8723 Power Configuration CMDs for PCIe interface */
#define Rtl8188E_NIC_PWR_ON_FLOW	rtl8188e_power_on_flow
#define Rtl8188E_NIC_RF_OFF_FLOW	rtl8188e_radio_off_flow
#define Rtl8188E_NIC_DISABLE_FLOW	rtl8188e_card_disable_flow
#define Rtl8188E_NIC_ENABLE_FLOW	rtl8188e_card_enable_flow
#define Rtl8188E_NIC_SUSPEND_FLOW	rtl8188e_suspend_flow
#define Rtl8188E_NIC_RESUME_FLOW	rtl8188e_resume_flow
#define Rtl8188E_NIC_PDN_FLOW		rtl8188e_hwpdn_flow
#define Rtl8188E_NIC_LPS_ENTER_FLOW	rtl8188e_enter_lps_flow
#define Rtl8188E_NIC_LPS_LEAVE_FLOW	rtl8188e_leave_lps_flow

#endif
