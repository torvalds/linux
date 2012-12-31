/*
 * Broadcom device-specific manifest constants.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
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
 * $Id: bcmdevs.h,v 13.285.2.39 2011-02-04 05:03:16 Exp $
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


#define BCM_DNGL_VID		0x0a5c
#define BCM_DNGL_BL_PID_4328	0xbd12
#define BCM_DNGL_BL_PID_4322	0xbd13
#define BCM_DNGL_BL_PID_4319    0xbd16
#define BCM_DNGL_BL_PID_43236   0xbd17
#define BCM_DNGL_BL_PID_4332	0xbd18
#define BCM_DNGL_BL_PID_4330	0xbd19
#define BCM_DNGL_BL_PID_43239   0xbd1b
#define BCM_DNGL_BDC_PID	0x0bdc
#define BCM_DNGL_JTAG_PID	0x4a44
#define	BCM4325_D11DUAL_ID	0x431b		
#define	BCM4325_D11G_ID		0x431c		
#define	BCM4325_D11A_ID		0x431d		
#define	BCM4321_D11N_ID		0x4328		
#define	BCM4321_D11N2G_ID	0x4329		
#define	BCM4321_D11N5G_ID	0x432a		
#define BCM4322_D11N_ID		0x432b		
#define BCM4322_D11N2G_ID	0x432c		
#define BCM4322_D11N5G_ID	0x432d		
#define BCM4329_D11N_ID		0x432e		
#define BCM4329_D11N2G_ID	0x432f		
#define BCM4329_D11N5G_ID	0x4330		
#define	BCM4315_D11DUAL_ID	0x4334		
#define	BCM4315_D11G_ID		0x4335		
#define	BCM4315_D11A_ID		0x4336		
#define BCM4319_D11N_ID		0x4337		
#define BCM4319_D11N2G_ID	0x4338		
#define BCM4319_D11N5G_ID	0x4339		
#define BCM43231_D11N2G_ID	0x4340		
#define BCM43221_D11N2G_ID	0x4341		
#define BCM43222_D11N_ID	0x4350		
#define BCM43222_D11N2G_ID	0x4351		
#define BCM43222_D11N5G_ID	0x4352		
#define BCM43224_D11N_ID	0x4353		
#define BCM43224_D11N_ID_VEN1	0x0576		
#define BCM43226_D11N_ID	0x4354		
#define BCM43236_D11N_ID	0x4346		
#define BCM43236_D11N2G_ID	0x4347		
#define BCM43236_D11N5G_ID	0x4348		
#define BCM43225_D11N2G_ID	0x4357		
#define BCM43421_D11N_ID	0xA99D		
#define BCM4313_D11N2G_ID	0x4727		
#define BCM4330_D11N_ID         0x4360          
#define BCM4330_D11N2G_ID       0x4361          
#define BCM4330_D11N5G_ID       0x4362          
#define BCM4336_D11N_ID		0x4343		
#define BCM6362_D11N_ID		0x435f		
#define BCM4331_D11N_ID		0x4331		
#define BCM4331_D11N2G_ID	0x4332		
#define BCM4331_D11N5G_ID	0x4333		
#define BCM43237_D11N_ID	0x4355		
#define BCM43237_D11N5G_ID	0x4356		
#define BCM43227_D11N2G_ID	0x4358		
#define BCM43228_D11N_ID		0x4359		
#define BCM43228_D11N5G_ID	0x435a		 
#define BCM43362_D11N_ID	0x4363		
#define BCM43239_D11N_ID	0x4370		


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
#define	BCM43111_CHIP_ID	43111		
#define	BCM43112_CHIP_ID	43112		
#define	BCM4312_CHIP_ID		0x4312		
#define BCM4313_CHIP_ID		0x4313		
#define	BCM4315_CHIP_ID		0x4315		
#define	BCM4318_CHIP_ID		0x4318		
#define	BCM4319_CHIP_ID		0x4319		
#define	BCM4320_CHIP_ID		0x4320		
#define	BCM4321_CHIP_ID		0x4321		
#define	BCM4322_CHIP_ID		0x4322		
#define	BCM43221_CHIP_ID	43221		
#define	BCM43222_CHIP_ID	43222		
#define	BCM43224_CHIP_ID	43224		
#define	BCM43225_CHIP_ID	43225		
#define	BCM43227_CHIP_ID	43227		
#define	BCM43228_CHIP_ID	43228		
#define	BCM43226_CHIP_ID	43226		
#define	BCM43231_CHIP_ID	43231		
#define	BCM43234_CHIP_ID	43234		
#define	BCM43235_CHIP_ID	43235		
#define	BCM43236_CHIP_ID	43236		
#define	BCM43237_CHIP_ID	43237		
#define	BCM43238_CHIP_ID	43238		
#define	BCM43239_CHIP_ID	43239		
#define	BCM43420_CHIP_ID	43420		
#define	BCM43421_CHIP_ID	43421		
#define	BCM43428_CHIP_ID	43428		
#define	BCM43431_CHIP_ID	43431		
#define	BCM4325_CHIP_ID		0x4325		
#define	BCM4328_CHIP_ID		0x4328		
#define	BCM4329_CHIP_ID		0x4329		
#define	BCM4331_CHIP_ID		0x4331		
#define BCM4336_CHIP_ID		0x4336		
#define BCM43362_CHIP_ID	43362		
#define BCM4330_CHIP_ID		0x4330		
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
