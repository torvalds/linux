/*
 * Definitions for Motorola MCG Falcon/Raven & HAWK North Bridge & Memory ctlr.
 *
 * Author: Mark A. Greerinclude/asm-ppc/hawk.h
 *         mgreer@mvista.com
 *
 * Modified by Randy Vinson (rvinson@mvista.com)
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __PPC_PPLUS_H
#define __PPC_PPLUS_H

#include <asm/io.h>

/*
 * Due to limiations imposed by legacy hardware (primaryily IDE controllers),
 * the PPLUS boards operate using a PReP address map.
 *
 * From Processor (physical) -> PCI:
 *   PCI Mem Space: 0xc0000000 - 0xfe000000 -> 0x00000000 - 0x3e000000 (768 MB)
 *   PCI I/O Space: 0x80000000 - 0x90000000 -> 0x00000000 - 0x10000000 (256 MB)
 *	Note: Must skip 0xfe000000-0xfe400000 for CONFIG_HIGHMEM/PKMAP area
 *
 * From PCI -> Processor (physical):
 *   System Memory: 0x80000000 -> 0x00000000
 */

#define PPLUS_ISA_MEM_BASE		PREP_ISA_MEM_BASE
#define PPLUS_ISA_IO_BASE		PREP_ISA_IO_BASE

/* PCI Memory space mapping info */
#define PPLUS_PCI_MEM_SIZE		0x30000000U
#define PPLUS_PROC_PCI_MEM_START	PPLUS_ISA_MEM_BASE
#define PPLUS_PROC_PCI_MEM_END		(PPLUS_PROC_PCI_MEM_START +	\
					 PPLUS_PCI_MEM_SIZE - 1)
#define PPLUS_PCI_MEM_START		0x00000000U
#define PPLUS_PCI_MEM_END		(PPLUS_PCI_MEM_START +	\
					 PPLUS_PCI_MEM_SIZE - 1)

/* PCI I/O space mapping info */
#define PPLUS_PCI_IO_SIZE		0x10000000U
#define PPLUS_PROC_PCI_IO_START		PPLUS_ISA_IO_BASE
#define PPLUS_PROC_PCI_IO_END		(PPLUS_PROC_PCI_IO_START +	\
					 PPLUS_PCI_IO_SIZE - 1)
#define PPLUS_PCI_IO_START		0x00000000U
#define PPLUS_PCI_IO_END		(PPLUS_PCI_IO_START + 	\
					 PPLUS_PCI_IO_SIZE - 1)
/* System memory mapping info */
#define PPLUS_PCI_DRAM_OFFSET		PREP_PCI_DRAM_OFFSET
#define PPLUS_PCI_PHY_MEM_OFFSET	(PPLUS_ISA_MEM_BASE-PPLUS_PCI_MEM_START)

/* Define base addresses for important sets of registers */
#define PPLUS_HAWK_SMC_BASE		0xfef80000U
#define PPLUS_HAWK_PPC_REG_BASE		0xfeff0000U
#define PPLUS_SYS_CONFIG_REG		0xfef80400U
#define PPLUS_L2_CONTROL_REG		0x8000081cU

#define PPLUS_VGA_MEM_BASE		0xf0000000U

#endif	/* __PPC_PPLUS_H */
