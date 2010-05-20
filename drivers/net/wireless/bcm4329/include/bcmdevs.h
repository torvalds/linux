/*
 * Broadcom device-specific manifest constants.
 *
 * Copyright (C) 1999-2010, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmdevs.h,v 13.172.4.5.4.10.2.28 2010/01/28 06:57:23 Exp $
 */


#ifndef	_BCMDEVS_H
#define	_BCMDEVS_H


#define	VENDOR_EPIGRAM		0xfeda
#define	VENDOR_BROADCOM		0x14e4
#define VENDOR_SI_IMAGE		0x1095		
#define VENDOR_TI		0x104c		
#define VENDOR_RICOH		0x1180		
#define VENDOR_JMICRON		0x197b


#define	VENDOR_BROADCOM_PCMCIA	0x02d0


#define	VENDOR_BROADCOM_SDIO	0x00BF


#define BCM_DNGL_VID            0xa5c
#define BCM_DNGL_BL_PID_4320    0xbd11
#define BCM_DNGL_BL_PID_4328    0xbd12
#define BCM_DNGL_BL_PID_4322    0xbd13
#define BCM_DNGL_BL_PID_4325    0xbd14
#define BCM_DNGL_BL_PID_4315    0xbd15
#define BCM_DNGL_BL_PID_4319    0xbd16
#define BCM_DNGL_BDC_PID        0xbdc

#define	BCM4325_D11DUAL_ID	0x431b		
#define	BCM4325_D11G_ID		0x431c		
#define	BCM4325_D11A_ID		0x431d		
#define BCM4329_D11NDUAL_ID	0x432e		
#define BCM4329_D11N2G_ID	0x432f		
#define BCM4329_D11N5G_ID	0x4330		
#define BCM4336_D11N_ID		0x4343		
#define	BCM4315_D11DUAL_ID	0x4334		
#define	BCM4315_D11G_ID		0x4335		
#define	BCM4315_D11A_ID		0x4336		
#define BCM4319_D11N_ID		0x4337		
#define BCM4319_D11N2G_ID	0x4338		
#define BCM4319_D11N5G_ID	0x4339		


#define SDIOH_FPGA_ID		0x43f2		
#define SPIH_FPGA_ID		0x43f5		
#define	BCM4710_DEVICE_ID	0x4710		
#define BCM27XX_SDIOH_ID	0x2702		
#define PCIXX21_FLASHMEDIA0_ID	0x8033		
#define PCIXX21_SDIOH0_ID	0x8034		
#define PCIXX21_FLASHMEDIA_ID	0x803b		
#define PCIXX21_SDIOH_ID	0x803c		
#define R5C822_SDIOH_ID		0x0822		
#define JMICRON_SDIOH_ID	0x2381		


#define	BCM4306_CHIP_ID		0x4306		
#define	BCM4311_CHIP_ID		0x4311		
#define	BCM4312_CHIP_ID		0x4312		
#define	BCM4315_CHIP_ID		0x4315		
#define	BCM4318_CHIP_ID		0x4318		
#define	BCM4319_CHIP_ID		0x4319		
#define	BCM4320_CHIP_ID		0x4320		
#define	BCM4321_CHIP_ID		0x4321		
#define	BCM4322_CHIP_ID		0x4322		
#define	BCM4325_CHIP_ID		0x4325		
#define	BCM4328_CHIP_ID		0x4328		
#define	BCM4329_CHIP_ID		0x4329		
#define BCM4336_CHIP_ID		0x4336		
#define	BCM4402_CHIP_ID		0x4402		
#define	BCM4704_CHIP_ID		0x4704		
#define	BCM4710_CHIP_ID		0x4710		
#define	BCM4712_CHIP_ID		0x4712		
#define BCM4785_CHIP_ID		0x4785		
#define	BCM5350_CHIP_ID		0x5350		
#define	BCM5352_CHIP_ID		0x5352		
#define	BCM5354_CHIP_ID		0x5354		
#define BCM5365_CHIP_ID		0x5365          



#define	BCM4303_PKG_ID		2		
#define	BCM4309_PKG_ID		1		
#define	BCM4712LARGE_PKG_ID	0		
#define	BCM4712SMALL_PKG_ID	1		
#define	BCM4712MID_PKG_ID	2		
#define BCM4328USBD11G_PKG_ID	2		
#define BCM4328USBDUAL_PKG_ID	3		
#define BCM4328SDIOD11G_PKG_ID	4		
#define BCM4328SDIODUAL_PKG_ID	5		
#define BCM4329_289PIN_PKG_ID	0		
#define BCM4329_182PIN_PKG_ID	1		
#define BCM5354E_PKG_ID		1		
#define HDLSIM5350_PKG_ID	1		
#define HDLSIM_PKG_ID		14		
#define HWSIM_PKG_ID		15		


#endif 
