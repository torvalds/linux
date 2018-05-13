/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
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

#ifndef __RTL8723BE_PWRSEQ_H__
#define __RTL8723BE_PWRSEQ_H__

#include "../pwrseqcmd.h"
/**
 *	Check document WM-20130425-JackieLau-RTL8723B_Power_Architecture v05.vsd
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
#define	RTL8723B_TRANS_CARDEMU_TO_ACT_STEPS	23
#define	RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS	15
#define	RTL8723B_TRANS_CARDEMU_TO_SUS_STEPS	15
#define	RTL8723B_TRANS_SUS_TO_CARDEMU_STEPS	15
#define	RTL8723B_TRANS_CARDEMU_TO_PDN_STEPS	15
#define	RTL8723B_TRANS_PDN_TO_CARDEMU_STEPS	15
#define	RTL8723B_TRANS_ACT_TO_LPS_STEPS		15
#define	RTL8723B_TRANS_LPS_TO_ACT_STEPS		15
#define	RTL8723B_TRANS_END_STEPS		1

#define RTL8723B_TRANS_CARDEMU_TO_ACT					\
	/* format */							\
	/* comments here */						\
	/* {offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value}, */\
	/*0x20[0] = 1b'1 enable LDOA12 MACRO block for all interface*/  \
	{0x0020, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK,				\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	/*0x67[0] = 0 to disable BT_GPS_SEL pins*/			\
	{0x0067, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK,				\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	/*Delay 1ms*/							\
	{0x0001, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK,				\
	 PWR_BASEADDR_MAC, PWR_CMD_DELAY, 1, PWRSEQ_DELAY_MS},		\
	/*0x00[5] = 1b'0 release analog Ips to digital ,1:isolation*/   \
	{0x0000, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK,				\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(5), 0},			\
	/* disable SW LPS 0x04[10]=0 and WLSUS_EN 0x04[11]=0*/		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, (BIT(4)|BIT(3)|BIT(2)), 0},	\
	/* Disable USB suspend */					\
	{0x0075, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0) , BIT(0)},		\
	/* wait till 0x04[17] = 1    power ready*/			\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), BIT(1)},		\
	/* Enable USB suspend */					\
	{0x0075, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0) , 0},			\
	/* release WLON reset  0x04[16]=1*/				\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	/* disable HWPDN 0x04[15]=0*/					\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0},			\
	/* disable WL suspend*/						\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, (BIT(4)|BIT(3)), 0},		\
	/* polling until return 0*/					\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(0), 0},			\
	/* Enable WL control XTAL setting*/				\
	{0x0010, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(6), BIT(6)},		\
	/*Enable falling edge triggering interrupt*/			\
	{0x0049, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	/*Enable GPIO9 interrupt mode*/					\
	{0x0063, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	/*Enable GPIO9 input mode*/					\
	{0x0062, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), 0},			\
	/*Enable HSISR GPIO[C:0] interrupt*/				\
	{0x0058, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	/*Enable HSISR GPIO9 interrupt*/				\
	{0x005A, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	/*For GPIO9 internal pull high setting by test chip*/		\
	{0x0068, PWR_CUT_TESTCHIP_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3), BIT(3)},		\
	/*For GPIO9 internal pull high setting*/			\
	{0x0069, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(6), BIT(6)},

#define RTL8723B_TRANS_ACT_TO_CARDEMU					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*0x1F[7:0] = 0 turn off RF*/					\
	{0x001F, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},			\
	/*0x4C[24] = 0x4F[0] = 0, */					\
	/*switch DPDT_SEL_P output from register 0x65[2] */		\
	{0x004F, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},			\
	/*Enable rising edge triggering interrupt*/			\
	{0x0049, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), 0},			\
	 /*0x04[9] = 1 turn off MAC by HW state machine*/		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	 /*wait till 0x04[9] = 0 polling until return 0 to disable*/	\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(1), 0},			\
	/* Enable BT control XTAL setting*/				\
	{0x0010, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(6), 0},			\
	/*0x00[5] = 1b'1 analog Ips to digital ,1:isolation*/		\
	{0x0000, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC,	\
	 PWR_CMD_WRITE, BIT(5), BIT(5)},				\
	/*0x20[0] = 1b'0 disable LDOA12 MACRO block*/			\
	{0x0020, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC,	\
	 PWR_CMD_WRITE, BIT(0), 0},

#define RTL8723B_TRANS_CARDEMU_TO_SUS					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value },*/\
	/*0x04[12:11] = 2b'11 enable WL suspend for PCIe*/		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4) | BIT(3), (BIT(4) | BIT(3))}, \
	/*0x04[12:11] = 2b'01 enable WL suspend*/			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK, PWR_BASEADDR_MAC,	\
	 PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},			\
	/*0x23[4] = 1b'1 12H LDO enter sleep mode*/			\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)},		\
	/*0x07[7:0] = 0x20 SDIO SOP option to disable BG/MB/ACK/SWR*/   \
	{0x0007, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x20},			\
	/*0x04[12:11] = 2b'11 enable WL suspend for PCIe*/		\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) | BIT(4)},\
	/*Set SDIO suspend local register*/				\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	/*wait power state to suspend*/					\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), 0},

#define RTL8723B_TRANS_SUS_TO_CARDEMU					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*clear suspend enable and power down enable*/			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3) | BIT(7), 0},		\
	/*Set SDIO suspend local register*/				\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), 0},			\
	/*wait power state to suspend*/					\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), BIT(1)},		\
	/*0x23[4] = 1b'0 12H LDO enter normal mode*/			\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	/*0x04[12:11] = 2b'00 disable WL suspend*/			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), 0},

#define RTL8723B_TRANS_CARDEMU_TO_CARDDIS				\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*0x07=0x20 , SOP option to disable BG/MB*/			\
	{0x0007, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x20},			\
	/*0x04[12:11] = 2b'01 enable WL suspend*/			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_USB_MSK | PWR_INTF_SDIO_MSK,				\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), BIT(3)},	\
	/*0x04[10] = 1, enable SW LPS*/					\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(2), BIT(2)},		\
	/*0x48[16] = 1 to enable GPIO9 as EXT WAKEUP*/			\
	{0x004A, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 1},			\
	/*0x23[4] = 1b'1 12H LDO enter sleep mode*/			\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)},		\
	/*Set SDIO suspend local register*/				\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), BIT(0)},		\
	/*wait power state to suspend*/					\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), 0},

#define RTL8723B_TRANS_CARDDIS_TO_CARDEMU				\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*clear suspend enable and power down enable*/			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3) | BIT(7), 0},		\
	/*Set SDIO suspend local register*/				\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_WRITE, BIT(0), 0},			\
	/*wait power state to suspend*/					\
	{0x0086, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_POLLING, BIT(1), BIT(1)},		\
	/*0x48[16] = 0 to disable GPIO9 as EXT WAKEUP*/			\
	{0x004A, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},			\
	/*0x04[12:11] = 2b'00 disable WL suspend*/			\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(3)|BIT(4), 0},		\
	/*0x23[4] = 1b'0 12H LDO enter normal mode*/			\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	/*PCIe DMA start*/						\
	{0x0301, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},

#define RTL8723B_TRANS_CARDEMU_TO_PDN					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*0x23[4] = 1b'1 12H LDO enter sleep mode*/			\
	{0x0023, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), BIT(4)},		\
	/*0x07[7:0] = 0x20 SOP option to disable BG/MB/ACK/SWR*/	\
	{0x0007, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,			\
	 PWR_INTF_SDIO_MSK | PWR_INTF_USB_MSK, PWR_BASEADDR_MAC,	\
	 PWR_CMD_WRITE, 0xFF, 0x20},					\
	/* 0x04[16] = 0*/						\
	{0x0006, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},			\
	/* 0x04[15] = 1*/						\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), BIT(7)},

#define RTL8723B_TRANS_PDN_TO_CARDEMU					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/* 0x04[15] = 0*/						\
	{0x0005, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(7), 0},

#define RTL8723B_TRANS_ACT_TO_LPS					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*PCIe DMA stop*/						\
	{0x0301, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0xFF},			\
	/*Tx Pause*/							\
	{0x0522, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0xFF},			\
	/*Should be zero if no packet is transmitting*/			\
	{0x05F8, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	/*Should be zero if no packet is transmitting*/			\
	{0x05F9, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	/*Should be zero if no packet is transmitting*/			\
	{0x05FA, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	/*Should be zero if no packet is transmitting*/			\
	{0x05FB, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, 0xFF, 0},			\
	/*CCK and OFDM are disabled,and clock are gated*/		\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(0), 0},			\
	/*Delay 1us*/							\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_DELAY, 0, PWRSEQ_DELAY_US},		\
	/*Whole BB is reset*/						\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), 0},			\
	/*Reset MAC TRX*/						\
	{0x0100, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x03},			\
	/*check if removed later*/					\
	{0x0101, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), 0},			\
	/*When driver enter Sus/ Disable, enable LOP for BT*/		\
	{0x0093, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x00},			\
	/*Respond TxOK to scheduler*/					\
	{0x0553, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(5), BIT(5)},

#define RTL8723B_TRANS_LPS_TO_ACT					\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	/*SDIO RPWM*/							\
	{0x0080, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK,	\
	 PWR_BASEADDR_SDIO, PWR_CMD_WRITE, 0xFF, 0x84},			\
	/*USB RPWM*/							\
	{0xFE58, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x84},			\
	/*PCIe RPWM*/							\
	{0x0361, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_PCI_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0x84},			\
	/*Delay*/							\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_DELAY, 0, PWRSEQ_DELAY_MS},		\
	/*.	0x08[4] = 0		 switch TSF to 40M*/		\
	{0x0008, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(4), 0},			\
	/*Polling 0x109[7]=0  TSF in 40M*/				\
	{0x0109, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_POLLING, BIT(7), 0},			\
	/*.	0x29[7:6] = 2b'00	 enable BB clock*/		\
	{0x0029, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(6)|BIT(7), 0},		\
	/*.	0x101[1] = 1*/						\
	{0x0101, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1), BIT(1)},		\
	/*.	0x100[7:0] = 0xFF	 enable WMAC TRX*/		\
	{0x0100, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0xFF},			\
	/*.	0x02[1:0] = 2b'11	 enable BB macro*/		\
	{0x0002, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, BIT(1) | BIT(0), BIT(1) | BIT(0)}, \
	/*.	0x522 = 0*/						\
	{0x0522, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK,	\
	 PWR_BASEADDR_MAC, PWR_CMD_WRITE, 0xFF, 0},

#define RTL8723B_TRANS_END						\
	/* format */							\
	/* comments here */						\
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, */\
	{0xFFFF, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_ALL_MSK, 0,	\
	 PWR_CMD_END, 0, 0},

extern struct wlan_pwr_cfg rtl8723B_power_on_flow
				[RTL8723B_TRANS_CARDEMU_TO_ACT_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_radio_off_flow
				[RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_card_disable_flow
				[RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS +
				 RTL8723B_TRANS_CARDEMU_TO_PDN_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_card_enable_flow
				[RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS +
				 RTL8723B_TRANS_CARDEMU_TO_PDN_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_suspend_flow
				[RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS +
				 RTL8723B_TRANS_CARDEMU_TO_SUS_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_resume_flow
				[RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS +
				 RTL8723B_TRANS_CARDEMU_TO_SUS_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_hwpdn_flow
				[RTL8723B_TRANS_ACT_TO_CARDEMU_STEPS +
				 RTL8723B_TRANS_CARDEMU_TO_PDN_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_enter_lps_flow
				[RTL8723B_TRANS_ACT_TO_LPS_STEPS +
				 RTL8723B_TRANS_END_STEPS];
extern struct wlan_pwr_cfg rtl8723B_leave_lps_flow
				[RTL8723B_TRANS_LPS_TO_ACT_STEPS +
				 RTL8723B_TRANS_END_STEPS];

/* RTL8723 Power Configuration CMDs for PCIe interface */
#define RTL8723_NIC_PWR_ON_FLOW		rtl8723B_power_on_flow
#define RTL8723_NIC_RF_OFF_FLOW		rtl8723B_radio_off_flow
#define RTL8723_NIC_DISABLE_FLOW	rtl8723B_card_disable_flow
#define RTL8723_NIC_ENABLE_FLOW		rtl8723B_card_enable_flow
#define RTL8723_NIC_SUSPEND_FLOW	rtl8723B_suspend_flow
#define RTL8723_NIC_RESUME_FLOW		rtl8723B_resume_flow
#define RTL8723_NIC_PDN_FLOW		rtl8723B_hwpdn_flow
#define RTL8723_NIC_LPS_ENTER_FLOW	rtl8723B_enter_lps_flow
#define RTL8723_NIC_LPS_LEAVE_FLOW	rtl8723B_leave_lps_flow

#endif
