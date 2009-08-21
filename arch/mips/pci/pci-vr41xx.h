/*
 *  pci-vr41xx.h, Include file for PCI Control Unit of the NEC VR4100 series.
 *
 *  Copyright (C) 2002  MontaVista Software Inc.
 *    Author: Yoichi Yuasa <source@mvista.com>
 *  Copyright (C) 2004-2005  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __PCI_VR41XX_H
#define __PCI_VR41XX_H

#define PCIU_BASE		0x0f000c00UL
#define PCIU_SIZE		0x200UL

#define PCIMMAW1REG		0x00
#define PCIMMAW2REG		0x04
#define PCITAW1REG		0x08
#define PCITAW2REG		0x0c
#define PCIMIOAWREG		0x10
 #define IBA(addr)		((addr) & 0xff000000U)
 #define MASTER_MSK(mask)	(((mask) >> 11) & 0x000fe000U)
 #define PCIA(addr)		(((addr) >> 24) & 0x000000ffU)
 #define TARGET_MSK(mask)	(((mask) >> 8) & 0x000fe000U)
 #define ITA(addr)		(((addr) >> 24) & 0x000000ffU)
 #define PCIIA(addr)		(((addr) >> 24) & 0x000000ffU)
 #define WINEN			0x1000U
#define PCICONFDREG		0x14
#define PCICONFAREG		0x18
#define PCIMAILREG		0x1c
#define BUSERRADREG		0x24
 #define EA(reg)		((reg) &0xfffffffc)

#define INTCNTSTAREG		0x28
 #define MABTCLR		0x80000000U
 #define TRDYCLR		0x40000000U
 #define PARCLR			0x20000000U
 #define MBCLR			0x10000000U
 #define SERRCLR		0x08000000U
 #define RTYCLR			0x04000000U
 #define MABCLR			0x02000000U
 #define TABCLR			0x01000000U
 /* RFU */
 #define MABTMSK		0x00008000U
 #define TRDYMSK		0x00004000U
 #define PARMSK			0x00002000U
 #define MBMSK			0x00001000U
 #define SERRMSK		0x00000800U
 #define RTYMSK			0x00000400U
 #define MABMSK			0x00000200U
 #define TABMSK			0x00000100U
 #define IBAMABT		0x00000080U
 #define TRDYRCH		0x00000040U
 #define PAR			0x00000020U
 #define MB			0x00000010U
 #define PCISERR		0x00000008U
 #define RTYRCH			0x00000004U
 #define MABORT			0x00000002U
 #define TABORT			0x00000001U

#define PCIEXACCREG		0x2c
 #define UNLOCK			0x2U
 #define EAREQ			0x1U
#define PCIRECONTREG		0x30
 #define RTRYCNT(reg)		((reg) & 0x000000ffU)
#define PCIENREG		0x34
 #define PCIU_CONFIG_DONE	0x4U
#define PCICLKSELREG		0x38
 #define EQUAL_VTCLOCK		0x2U
 #define HALF_VTCLOCK		0x0U
 #define ONE_THIRD_VTCLOCK	0x3U
 #define QUARTER_VTCLOCK	0x1U
#define PCITRDYVREG		0x3c
 #define TRDYV(val)		((uint32_t)(val) & 0xffU)
#define PCICLKRUNREG		0x60

#define VENDORIDREG		0x100
#define DEVICEIDREG		0x100
#define COMMANDREG		0x104
#define STATUSREG		0x104
#define REVIDREG		0x108
#define CLASSREG		0x108
#define CACHELSREG		0x10c
#define LATTIMEREG		0x10c
 #define MLTIM(val)		(((uint32_t)(val) << 7) & 0xff00U)
#define MAILBAREG		0x110
#define PCIMBA1REG		0x114
#define PCIMBA2REG		0x118
 #define MBADD(base)		((base) & 0xfffff800U)
 #define PMBA(base)		((base) & 0xffe00000U)
 #define PREF			0x8U
 #define PREF_APPROVAL		0x8U
 #define PREF_DISAPPROVAL	0x0U
 #define TYPE			0x6U
 #define TYPE_32BITSPACE	0x0U
 #define MSI			0x1U
 #define MSI_MEMORY		0x0U
#define INTLINEREG		0x13c
#define INTPINREG		0x13c
#define RETVALREG		0x140
#define PCIAPCNTREG		0x140
 #define TKYGNT			0x04000000U
 #define TKYGNT_ENABLE		0x04000000U
 #define TKYGNT_DISABLE		0x00000000U
 #define PAPC			0x03000000U
 #define PAPC_ALTERNATE_B	0x02000000U
 #define PAPC_ALTERNATE_0	0x01000000U
 #define PAPC_FAIR		0x00000000U
 #define RTYVAL(val)		(((uint32_t)(val) << 7) & 0xff00U)
 #define RTYVAL_MASK		0xff00U

#define PCI_CLOCK_MAX		33333333U

/*
 * Default setup
 */
#define PCI_MASTER_MEM1_BUS_BASE_ADDRESS	0x10000000U
#define PCI_MASTER_MEM1_ADDRESS_MASK		0x7c000000U
#define PCI_MASTER_MEM1_PCI_BASE_ADDRESS	0x10000000U

#define PCI_TARGET_MEM1_ADDRESS_MASK		0x08000000U
#define PCI_TARGET_MEM1_BUS_BASE_ADDRESS	0x00000000U

#define PCI_MASTER_IO_BUS_BASE_ADDRESS		0x16000000U
#define PCI_MASTER_IO_ADDRESS_MASK		0x7e000000U
#define PCI_MASTER_IO_PCI_BASE_ADDRESS		0x00000000U

#define PCI_MAILBOX_BASE_ADDRESS		0x00000000U

#define PCI_TARGET_WINDOW1_BASE_ADDRESS		0x00000000U

#define IO_PORT_BASE		KSEG1ADDR(PCI_MASTER_IO_BUS_BASE_ADDRESS)
#define IO_PORT_RESOURCE_START	PCI_MASTER_IO_PCI_BASE_ADDRESS
#define IO_PORT_RESOURCE_END	(~PCI_MASTER_IO_ADDRESS_MASK & PCI_MASTER_ADDRESS_MASK)

#define PCI_IO_RESOURCE_START	0x01000000UL
#define PCI_IO_RESOURCE_END	0x01ffffffUL

#define PCI_MEM_RESOURCE_START	0x11000000UL
#define PCI_MEM_RESOURCE_END	0x13ffffffUL

#endif /* __PCI_VR41XX_H */
