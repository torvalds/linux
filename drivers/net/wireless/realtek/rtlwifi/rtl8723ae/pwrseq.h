/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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

#include "../pwrseqcmd.h"
/*
 *	Check document WM-20110607-Paul-RTL8723A_Power_Architecture-R02.vsd
 *	There are 6 HW Power States:
 *	0: POFF--Power Off
 *	1: PDN--Power Down
 *	2: CARDEMU--Card Emulation
 *	3: ACT--Active Mode
 *	4: LPS--Low Power State
 *	5: SUS--Suspend
 *
 *	The transision from different states are defined below
 *	TRANS_CARDEMU_TO_ACT
 *	TRANS_ACT_TO_CARDEMU
 *	TRANS_CARDEMU_TO_SUS
 *	TRANS_SUS_TO_CARDEMU
 *	TRANS_CARDEMU_TO_PDN
 *	TRANS_ACT_TO_LPS
 *	TRANS_LPS_TO_ACT
 *
 *	TRANS_END
 */

#define	RTL8723A_TRANS_CARDEMU_TO_ACT_STEPS	10
#define	RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS	10
#define	RTL8723A_TRANS_CARDEMU_TO_SUS_STEPS	10
#define	RTL8723A_TRANS_SUS_TO_CARDEMU_STEPS	10
#define	RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS	10
#define	RTL8723A_TRANS_PDN_TO_CARDEMU_STEPS	10
#define	RTL8723A_TRANS_ACT_TO_LPS_STEPS		15
#define	RTL8723A_TRANS_LPS_TO_ACT_STEPS		15
#define	RTL8723A_TRANS_END_STEPS		1

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }*/

#define RTL8723A_TRANS_CARDEMU_TO_ACT	\
	/* disable SW LPS 0x04[10]=0*/	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(2), 0},\
	/* wait till 0x04[17] = 1    power ready*/	\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), BIT(1)},\
	/* release WLON reset  0x04[16]=1*/	\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},\
	/* disable HWPDN 0x04[15]=0*/ \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0},\
	/* disable WL suspend*/ \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, (BIT(4)|BIT(3)), 0},\
	/* polling until return 0*/ \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(0), 0},

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */

#define RTL8723A_TRANS_ACT_TO_CARDEMU	\
	/*0x1F[7:0] = 0 turn off RF*/ \
	{0x001F, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},	\
	{0x004E, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0},\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)}, \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), 0},

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/
#define RTL8723A_TRANS_CARDEMU_TO_SUS			\
		/*0x04[12:11] = 2b'11 enable WL suspend for PCIe*/	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, \
		BIT(4)|BIT(3), (BIT(4)|BIT(3))},\
/*0x04[12:11] = 2b'01 enable WL suspend*/	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK| \
		PWR_INTF_SDIO_MSK,\
		PWR_BASEADDR_MAC, \
		PWR_CMD_WRITE, \
		BIT(3)|BIT(4), BIT(3)}, \
/*0x04[12:11] = 2b'11 enable WL suspend for PCIe*/ \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_PCI_MSK, PWR_BASEADDR_MAC, \
		PWR_CMD_WRITE, BIT(3)|BIT(4), \
		BIT(3)|BIT(4)}, \
/*Set SDIO suspend local register*/	\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_WRITE, BIT(0), BIT(0)}, \
/*wait power state to suspend*/ \
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_POLLING, BIT(1), 0},

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */

#define RTL8723A_TRANS_SUS_TO_CARDEMU	\
 /*Set SDIO suspend local register*/	\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,\
		PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), 0},\
 /*wait power state to suspend*/ \
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,\
		PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), BIT(1)},\
 /*0x04[12:11] = 2b'01enable WL suspend*/ \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), 0},

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */

#define RTL8723A_TRANS_CARDEMU_TO_CARDDIS \
 /*0x04[12:11] = 2b'01 enable WL suspend*/	 \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_USB_MSK|PWR_INTF_SDIO_MSK, \
		PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), BIT(3)}, \
/*0x04[10] = 1, enable SW LPS*/	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_PCI_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(2), BIT(2)}, \
/*Set SDIO suspend local register*/ \
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_WRITE, BIT(0), BIT(0)}, \
 /*wait power state to suspend*/ \
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_POLLING, BIT(1), 0},

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */

#define RTL8723A_TRANS_CARDDIS_TO_CARDEMU\
/*Set SDIO suspend local register*/	\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_WRITE, BIT(0), 0}, \
 /*wait power state to suspend*/ \
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_POLLING, BIT(1), BIT(1)},\
 /*0x04[12:11] = 2b'00enable WL suspend*/ \
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(3)|BIT(4), 0},\
/*PCIe DMA start*/ \
	{0x0301, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0},

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */
#define RTL8723A_TRANS_CARDEMU_TO_PDN	\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(0), 0},/* 0x04[16] = 0*/\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(7), BIT(7)},/* 0x04[15] = 1*/

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */
#define RTL8723A_TRANS_PDN_TO_CARDEMU	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(7), 0},/* 0x04[15] = 0*/

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */

#define RTL8723A_TRANS_ACT_TO_LPS	\
	{0x0301, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0xFF},/*PCIe DMA stop*/	\
	{0x0522, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0x7F},/*Tx Pause*/	\
	/*Should be zero if no packet is transmitting*/	\
	{0x05F8, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_POLLING, 0xFF, 0},\
	/*Should be zero if no packet is transmitting*/	\
	{0x05F9, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_POLLING, 0xFF, 0},\
	/*Should be zero if no packet is transmitting*/	\
	{0x05FA, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_POLLING, 0xFF, 0},\
	/*Should be zero if no packet is transmitting*/	\
	{0x05FB, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_POLLING, 0xFF, 0},\
	/*CCK and OFDM are disabled,and clock are gated*/ \
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(0), 0},\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_DELAY, 0, PWRSEQ_DELAY_US},/*Delay 1us*/\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(1), 0},/*Whole BB is reset*/	\
	{0x0100, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0x3F},/*Reset MAC TRX*/	\
	{0x0101, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(1), 0},/*check if removed later*/	\
	/*Respond TxOK to scheduler*/	\
	{0x0553, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(5), BIT(5)},\

#define RTL8723A_TRANS_LPS_TO_ACT\
/* format */	\
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */	\
	{0x0080, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_SDIO_MSK, PWR_BASEADDR_SDIO,\
		PWR_CMD_WRITE, 0xFF, 0x84}, /*SDIO RPWM*/\
	{0xFE58, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_USB_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0x84}, /*USB RPWM*/\
	{0x0361, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_PCI_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0x84}, /*PCIe RPWM*/\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_DELAY, 0, PWRSEQ_DELAY_MS}, /*Delay*/\
	/*.	0x08[4] = 0		 switch TSF to 40M*/\
	{0x0008, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(4), 0},  \
	/*Polling 0x109[7]=0  TSF in 40M*/\
	{0x0109, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_POLLING, BIT(7), 0}, \
	/*.	0x29[7:6] = 2b'00	 enable BB clock*/\
	{0x0029, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(6)|BIT(7), 0},\
	 /*.	0x101[1] = 1*/\
	{0x0101, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(1), BIT(1)},\
	 /*.	0x100[7:0] = 0xFF	 enable WMAC TRX*/\
	{0x0100, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0xFF},\
	 /*.	0x02[1:0] = 2b'11	 enable BB macro*/\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, BIT(1)|BIT(0), BIT(1)|BIT(0)},\
	{0x0522, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, \
		PWR_INTF_ALL_MSK, PWR_BASEADDR_MAC,\
		PWR_CMD_WRITE, 0xFF, 0}, /*.	0x522 = 0*/

/* format */
/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */

#define RTL8723A_TRANS_END \
	{0xFFFF, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
	0, PWR_CMD_END, 0, 0}

extern struct wlan_pwr_cfg rtl8723A_power_on_flow
		[RTL8723A_TRANS_CARDEMU_TO_ACT_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_radio_off_flow
		[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_card_disable_flow
		[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_card_enable_flow
		[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_suspend_flow
		[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8723A_TRANS_CARDEMU_TO_SUS_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_resume_flow
		[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8723A_TRANS_CARDEMU_TO_SUS_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_hwpdn_flow
		[RTL8723A_TRANS_ACT_TO_CARDEMU_STEPS +
		 RTL8723A_TRANS_CARDEMU_TO_PDN_STEPS +
		 RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_enter_lps_flow
		[RTL8723A_TRANS_ACT_TO_LPS_STEPS + RTL8723A_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723A_leave_lps_flow
		[RTL8723A_TRANS_LPS_TO_ACT_STEPS + RTL8723A_TRANS_END_STEPS];

/* RTL8723 Power Configuration CMDs for PCIe interface */
#define Rtl8723_NIC_PWR_ON_FLOW		rtl8723A_power_on_flow
#define Rtl8723_NIC_RF_OFF_FLOW		rtl8723A_radio_off_flow
#define Rtl8723_NIC_DISABLE_FLOW	rtl8723A_card_disable_flow
#define Rtl8723_NIC_ENABLE_FLOW		rtl8723A_card_enable_flow
#define Rtl8723_NIC_SUSPEND_FLOW	rtl8723A_suspend_flow
#define Rtl8723_NIC_RESUME_FLOW		rtl8723A_resume_flow
#define Rtl8723_NIC_PDN_FLOW		rtl8723A_hwpdn_flow
#define Rtl8723_NIC_LPS_ENTER_FLOW	rtl8723A_enter_lps_flow
#define Rtl8723_NIC_LPS_LEAVE_FLOW	rtl8723A_leave_lps_flow

#endif
