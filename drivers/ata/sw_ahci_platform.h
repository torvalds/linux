/*
 * drivers/ata/sw_ahci_platform.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Daniel Wang <danielwang@allwinnertech.com>
 *
 * Based on ahci_platform.h
 * 
 * Copyright 2004-2005  Red Hat, Inc.
 *   Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2010  MontaVista Software, LLC.
 *   Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef _SW_AHCI_PLATFORM_H
#define _SW_AHCI_PLATFORM_H

#include <mach/irqs.h>

#define	ahci_readb(base,offset)  		(*((volatile unsigned char*)((base)+(offset))))
#define	ahci_readw(base,offset)  		(*((volatile unsigned short*)((base)+(offset))))
#define	ahci_readl(base,offset)  		(*((volatile unsigned int*)((base)+(offset))))
#define ahci_writeb(base,offset,val)	(*((volatile unsigned char*)((base)+(offset))) = (val))
#define ahci_writew(base,offset,val)	(*((volatile unsigned short*)((base)+(offset))) = (val))
#define ahci_writel(base,offset,val)	(*((volatile unsigned int*)((base)+(offset))) = (val))

#define SW_AHCI_BASE				0x01c18000

#define SW_AHCI_BISTAFR_OFFSET		0x00A0
#define SW_AHCI_BISTCR_OFFSET		0x00A4
#define SW_AHCI_BISTFCTR_OFFSET		0x00A8
#define SW_AHCI_BISTSR_OFFSET		0x00AC
#define SW_AHCI_BISTDECR_OFFSET		0x00B0
#define SW_AHCI_DIAGNR_OFFSET		0x00B4
#define SW_AHCI_DIAGNR1_OFFSET		0x00B8
#define SW_AHCI_OOBR_OFFSET			0x00BC
#define SW_AHCI_PHYCS0R_OFFSET		0x00C0
#define SW_AHCI_PHYCS1R_OFFSET		0x00C4
#define SW_AHCI_PHYCS2R_OFFSET		0x00C8
#define SW_AHCI_TIMER1MS_OFFSET		0x00E0
#define SW_AHCI_GPARAM1R_OFFSET		0x00E8
#define SW_AHCI_GPARAM2R_OFFSET		0x00EC
#define SW_AHCI_PPARAMR_OFFSET		0x00F0
#define SW_AHCI_TESTR_OFFSET		0x00F4
#define SW_AHCI_VERSIONR_OFFSET		0x00F8
#define SW_AHCI_IDR_OFFSET			0x00FC
#define SW_AHCI_RWCR_OFFSET			0x00FC

#define SW_AHCI_P0DMACR_OFFSET		0x0170
#define SW_AHCI_P0PHYCR_OFFSET		0x0178
#define SW_AHCI_P0PHYSR_OFFSET		0x017C

#define SW_AHCI_ACCESS_LOCK(base,x)		(*((volatile unsigned int *)((base)+SW_AHCI_RWCR_OFFSET)) = (x))

#define INTC_IRQNO_AHCI				SW_INT_IRQNO_SATA

#define CCMU_PLL6_VBASE				0xF1C20028

#endif /* _SW_AHCI_PLATFORM_H */
