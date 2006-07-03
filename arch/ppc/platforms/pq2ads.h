/*
 * A collection of structures, addresses, and values associated with
 * the Motorola MPC8260ADS/MPC8266ADS-PCI boards.
 * Copied from the RPX-Classic and SBS8260 stuff.
 *
 * Copyright (c) 2001 Dan Malek (dan@mvista.com)
 */
#ifdef __KERNEL__
#ifndef __MACH_ADS8260_DEFS
#define __MACH_ADS8260_DEFS


#include <asm/ppcboot.h>

#if defined(CONFIG_ADS8272)
#define BOARD_CHIP_NAME "8272"
#endif

/* Memory map is configured by the PROM startup.
 * We just map a few things we need.  The CSR is actually 4 byte-wide
 * registers that can be accessed as 8-, 16-, or 32-bit values.
 */
#define CPM_MAP_ADDR		((uint)0xf0000000)
#define BCSR_ADDR		((uint)0xf4500000)
#define BCSR_SIZE		((uint)(32 * 1024))

#define BOOTROM_RESTART_ADDR	((uint)0xff000104)

/* For our show_cpuinfo hooks. */
#define CPUINFO_VENDOR		"Motorola"
#define CPUINFO_MACHINE		"PQ2 ADS PowerPC"

/* The ADS8260 has 16, 32-bit wide control/status registers, accessed
 * only on word boundaries.
 * Not all are used (yet), or are interesting to us (yet).
 */

/* Things of interest in the CSR.
*/
#define BCSR0_LED0		((uint)0x02000000)	/* 0 == on */
#define BCSR0_LED1		((uint)0x01000000)	/* 0 == on */
#define BCSR1_FETHIEN		((uint)0x08000000)	/* 0 == enable */
#define BCSR1_FETH_RST		((uint)0x04000000)	/* 0 == reset */
#define BCSR1_RS232_EN1		((uint)0x02000000)	/* 0 == enable */
#define BCSR1_RS232_EN2		((uint)0x01000000)	/* 0 == enable */
#define BCSR3_FETHIEN2		((uint)0x10000000)	/* 0 == enable */
#define BCSR3_FETH2_RST 	((uint)0x80000000)	/* 0 == reset */

#define PHY_INTERRUPT	SIU_INT_IRQ7

#ifdef CONFIG_PCI
/* PCI interrupt controller */
#define PCI_INT_STAT_REG	0xF8200000
#define PCI_INT_MASK_REG	0xF8200004
#define PIRQA			(NR_CPM_INTS + 0)
#define PIRQB			(NR_CPM_INTS + 1)
#define PIRQC			(NR_CPM_INTS + 2)
#define PIRQD			(NR_CPM_INTS + 3)

/*
 * PCI memory map definitions for MPC8266ADS-PCI.
 *
 * processor view
 *	local address		PCI address		target
 *	0x80000000-0x9FFFFFFF	0x80000000-0x9FFFFFFF	PCI mem with prefetch
 *	0xA0000000-0xBFFFFFFF	0xA0000000-0xBFFFFFFF	PCI mem w/o prefetch
 *	0xF4000000-0xF7FFFFFF	0x00000000-0x03FFFFFF	PCI IO
 *
 * PCI master view
 *	local address		PCI address		target
 *	0x00000000-0x1FFFFFFF	0x00000000-0x1FFFFFFF	MPC8266 local memory
 */

/* All the other PCI memory map definitions reside at syslib/m82xx_pci.h
   Here we should redefine what is unique for this board */
#define M82xx_PCI_SLAVE_MEM_LOCAL	0x00000000	/* Local base */
#define M82xx_PCI_SLAVE_MEM_BUS		0x00000000	/* PCI base */
#define M82xx_PCI_SLAVE_MEM_SIZE	0x10000000	/* 256 Mb */

#define M82xx_PCI_SLAVE_SEC_WND_SIZE	~(0x40000000 - 1U)	/* 2 x 512Mb  */
#define M82xx_PCI_SLAVE_SEC_WND_BASE	0x80000000		/* PCI Memory base */

#if defined(CONFIG_ADS8272)
#define PCI_INT_TO_SIU 	SIU_INT_IRQ2
#elif defined(CONFIG_PQ2FADS)
#define PCI_INT_TO_SIU 	SIU_INT_IRQ6
#else
#warning PCI Bridge will be without interrupts support
#endif

#endif /* CONFIG_PCI */

#endif /* __MACH_ADS8260_DEFS */
#endif /* __KERNEL__ */
