/***************************************************************/
/*  $Id: hfc4s8s_l1.h,v 1.1 2005/02/02 17:28:55 martinb1 Exp $ */
/*                                                             */
/*  This file is a minimal required extraction of hfc48scu.h   */
/*  (Genero 3.2, HFC XML 1.7a for HFC-E1, HFC-4S and HFC-8S)   */
/*                                                             */
/*  To get this complete register description contact          */
/*  Cologne Chip AG :                                          */
/*  Internet:  http://www.colognechip.com/                     */
/*  E-Mail:    info@colognechip.com                            */
/***************************************************************/

#ifndef _HFC4S8S_L1_H_
#define _HFC4S8S_L1_H_


/*
*  include Genero generated HFC-4S/8S header file hfc48scu.h
*  for complete register description. This will define _HFC48SCU_H_
*  to prevent redefinitions
*/

// #include "hfc48scu.h"

#ifndef _HFC48SCU_H_
#define _HFC48SCU_H_

#ifndef PCI_VENDOR_ID_CCD
#define PCI_VENDOR_ID_CCD	0x1397
#endif

#define CHIP_ID_4S		0x0C
#define CHIP_ID_8S		0x08
#define PCI_DEVICE_ID_4S	0x08B4
#define PCI_DEVICE_ID_8S	0x16B8

#define R_IRQ_MISC	0x11
#define M_TI_IRQ	0x02
#define A_ST_RD_STA	0x30
#define A_ST_WR_STA	0x30
#define M_SET_G2_G3	0x80
#define A_ST_CTRL0	0x31
#define A_ST_CTRL2	0x33
#define A_ST_CLK_DLY	0x37
#define A_Z1		0x04
#define A_Z2		0x06
#define R_CIRM		0x00
#define M_SRES		0x08
#define R_CTRL		0x01
#define R_BRG_PCM_CFG	0x02
#define M_PCM_CLK	0x20
#define R_RAM_MISC	0x0C
#define M_FZ_MD		0x80
#define R_FIFO_MD	0x0D
#define A_INC_RES_FIFO	0x0E
#define R_FIFO		0x0F
#define A_F1		0x0C
#define A_F2		0x0D
#define R_IRQ_OVIEW	0x10
#define R_CHIP_ID	0x16
#define R_STATUS	0x1C
#define M_BUSY		0x01
#define M_MISC_IRQSTA	0x40
#define M_FR_IRQSTA	0x80
#define R_CHIP_RV	0x1F
#define R_IRQ_CTRL	0x13
#define M_FIFO_IRQ	0x01
#define M_GLOB_IRQ_EN	0x08
#define R_PCM_MD0	0x14
#define M_PCM_MD	0x01
#define A_FIFO_DATA0	0x80
#define R_TI_WD		0x1A
#define R_PWM1		0x39
#define R_PWM_MD	0x46
#define R_IRQ_FIFO_BL0	0xC8
#define A_CON_HDLC	0xFA
#define A_SUBCH_CFG	0xFB
#define A_IRQ_MSK	0xFF
#define R_SCI_MSK	0x12
#define R_ST_SEL	0x16
#define R_ST_SYNC	0x17
#define M_AUTO_SYNC	0x08
#define R_SCI		0x12
#define R_IRQMSK_MISC	0x11
#define M_TI_IRQMSK	0x02

#endif	/* _HFC4S8S_L1_H_ */
#endif	/* _HFC48SCU_H_ */
