/*
 *	Low-Level PCI Support for SH7780 targets
 *
 *  Dustin McIntire (dustin@sensoria.com) (c) 2001
 *  Paul Mundt (lethal@linux-sh.org) (c) 2003
 *
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
 */

#ifndef _PCI_SH7780_H_
#define _PCI_SH7780_H_

/* Platform Specific Values */
#define SH7780_VENDOR_ID	0x1912
#define SH7780_DEVICE_ID	0x0002
#define SH7781_DEVICE_ID	0x0001

/* SH7780 Control Registers */
#define	SH7780_PCI_VCR0		0xFE000000
#define	SH7780_PCI_VCR1		0xFE000004
#define	SH7780_PCI_VCR2		0xFE000008

/* SH7780 Specific Values */
#define SH7780_PCI_CONFIG_BASE	0xFD000000	/* Config space base addr */
#define SH7780_PCI_CONFIG_SIZE	0x01000000	/* Config space size */

#define SH7780_PCI_MEMORY_BASE	0xFD000000	/* Memory space base addr */
#define SH7780_PCI_MEM_SIZE	0x01000000	/* Size of Memory window */

#define SH7780_PCI_IO_BASE	0xFE400000	/* IO space base address */
#define SH7780_PCI_IO_SIZE	0x00400000	/* Size of IO window */

#define SH7780_PCIREG_BASE	0xFE040000	/* PCI regs base address */
#define PCI_REG(n)		(SH7780_PCIREG_BASE+n)

/* SH7780 PCI Config Registers */
#define SH7780_PCIVID		0x000		/* Vendor ID */
#define SH7780_PCIDID		0x002		/* Device ID */
#define SH7780_PCICMD		0x004		/* Command */
#define SH7780_PCISTATUS	0x006		/* Status */
#define SH7780_PCIRID		0x008		/* Revision ID */
#define SH7780_PCIPIF		0x009		/* Program Interface */
#define SH7780_PCISUB		0x00a		/* Sub class code */
#define SH7780_PCIBCC		0x00b		/* Base class code */
#define SH7780_PCICLS		0x00c		/* Cache line size */
#define SH7780_PCILTM		0x00d		/* latency timer */
#define SH7780_PCIHDR		0x00e		/* Header type */
#define SH7780_PCIBIST		0x00f		/* BIST */
#define SH7780_PCIIBAR		0x010		/* IO Base address */
#define SH7780_PCIMBAR0		0x014		/* Memory base address0 */
#define SH7780_PCIMBAR1		0x018		/* Memory base address1 */
#define SH7780_PCISVID		0x02c		/* Sub system vendor ID */
#define SH7780_PCISID		0x02e		/* Sub system ID */
#define SH7780_PCICP		0x034
#define SH7780_PCIINTLINE	0x03c		/* Interrupt line */
#define SH7780_PCIINTPIN	0x03d		/* Interrupt pin */
#define SH7780_PCIMINGNT	0x03e		/* Minumum grand */
#define SH7780_PCIMAXLAT	0x03f		/* Maxmum latency */
#define SH7780_PCICID		0x040
#define SH7780_PCINIP		0x041
#define SH7780_PCIPMC		0x042
#define SH7780_PCIPMCSR		0x044
#define SH7780_PCIPMCSR_BSE	0x046
#define SH7780_PCICDD		0x047

#define SH7780_PCIMBR0		0x1E0
#define SH7780_PCIMBMR0		0x1E4
#define SH7780_PCIMBR2		0x1F0
#define SH7780_PCIMBMR2		0x1F4
#define SH7780_PCIIOBR		0x1F8
#define SH7780_PCIIOBMR		0x1FC
#define SH7780_PCICSCR0		0x210		/* Cache Snoop1 Cnt. Register */
#define SH7780_PCICSCR1		0x214		/* Cache Snoop2 Cnt. Register */
#define SH7780_PCICSAR0		0x218	/* Cache Snoop1 Addr. Register */
#define SH7780_PCICSAR1		0x21C	/* Cache Snoop2 Addr. Register */

/* General Memory Config Addresses */
#define SH7780_CS0_BASE_ADDR	0x0
#define SH7780_MEM_REGION_SIZE	0x04000000
#define SH7780_CS1_BASE_ADDR	(SH7780_CS0_BASE_ADDR + SH7780_MEM_REGION_SIZE)
#define SH7780_CS2_BASE_ADDR	(SH7780_CS1_BASE_ADDR + SH7780_MEM_REGION_SIZE)
#define SH7780_CS3_BASE_ADDR	(SH7780_CS2_BASE_ADDR + SH7780_MEM_REGION_SIZE)
#define SH7780_CS4_BASE_ADDR	(SH7780_CS3_BASE_ADDR + SH7780_MEM_REGION_SIZE)
#define SH7780_CS5_BASE_ADDR	(SH7780_CS4_BASE_ADDR + SH7780_MEM_REGION_SIZE)
#define SH7780_CS6_BASE_ADDR	(SH7780_CS5_BASE_ADDR + SH7780_MEM_REGION_SIZE)

struct sh4_pci_address_map;

/* arch/sh/drivers/pci/pci-sh7780.c */
int sh7780_pcic_init(struct sh4_pci_address_map *map);

#endif /* _PCI_SH7780_H_ */
