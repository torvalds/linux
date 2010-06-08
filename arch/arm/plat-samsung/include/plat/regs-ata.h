/* linux/arch/arm/plat-samsung/include/plat/regs-ata.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung CF-ATA register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_REGS_ATA_H
#define __ASM_PLAT_REGS_ATA_H __FILE__

#define S3C_CFATA_REG(x)	(x)

#define S3C_CFATA_MUX		S3C_CFATA_REG(0x0)

#define S3C_ATA_CTRL		S3C_CFATA_REG(0x0)
#define S3C_ATA_STATUS		S3C_CFATA_REG(0x4)
#define S3C_ATA_CMD		S3C_CFATA_REG(0x8)
#define S3C_ATA_SWRST		S3C_CFATA_REG(0xc)
#define S3C_ATA_IRQ		S3C_CFATA_REG(0x10)
#define S3C_ATA_IRQ_MSK		S3C_CFATA_REG(0x14)
#define S3C_ATA_CFG		S3C_CFATA_REG(0x18)

#define S3C_ATA_MDMA_TIME	S3C_CFATA_REG(0x28)
#define S3C_ATA_PIO_TIME	S3C_CFATA_REG(0x2c)
#define S3C_ATA_UDMA_TIME	S3C_CFATA_REG(0x30)
#define S3C_ATA_XFR_NUM		S3C_CFATA_REG(0x34)
#define S3C_ATA_XFR_CNT		S3C_CFATA_REG(0x38)
#define S3C_ATA_TBUF_START	S3C_CFATA_REG(0x3c)
#define S3C_ATA_TBUF_SIZE	S3C_CFATA_REG(0x40)
#define S3C_ATA_SBUF_START	S3C_CFATA_REG(0x44)
#define S3C_ATA_SBUF_SIZE	S3C_CFATA_REG(0x48)
#define S3C_ATA_CADR_TBUF	S3C_CFATA_REG(0x4c)
#define S3C_ATA_CADR_SBUF	S3C_CFATA_REG(0x50)
#define S3C_ATA_PIO_DTR		S3C_CFATA_REG(0x54)
#define S3C_ATA_PIO_FED		S3C_CFATA_REG(0x58)
#define S3C_ATA_PIO_SCR		S3C_CFATA_REG(0x5c)
#define S3C_ATA_PIO_LLR		S3C_CFATA_REG(0x60)
#define S3C_ATA_PIO_LMR		S3C_CFATA_REG(0x64)
#define S3C_ATA_PIO_LHR		S3C_CFATA_REG(0x68)
#define S3C_ATA_PIO_DVR		S3C_CFATA_REG(0x6c)
#define S3C_ATA_PIO_CSD		S3C_CFATA_REG(0x70)
#define S3C_ATA_PIO_DAD		S3C_CFATA_REG(0x74)
#define S3C_ATA_PIO_READY	S3C_CFATA_REG(0x78)
#define S3C_ATA_PIO_RDATA	S3C_CFATA_REG(0x7c)

#define S3C_CFATA_MUX_TRUEIDE	0x01

#define S3C_ATA_CFG_SWAP	0x40
#define S3C_ATA_CFG_IORDYEN	0x02

#endif /* __ASM_PLAT_REGS_ATA_H */
