/*
 * include/asm-ppc/platforms/prpmc800.h
 *
 * Definitions for Motorola PrPMC800 board support
 *
 * Author: Dale Farnsworth <dale.farnsworth@mvista.com>
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
 /*
  * From Processor to PCI:
  *   PCI Mem Space: 0x80000000 - 0xa0000000 -> 0x80000000 - 0xa0000000 (512 MB)
  *   PCI I/O Space: 0xfe400000 - 0xfeef0000 -> 0x00000000 - 0x00b00000 (11 MB)
  *      Note: Must skip 0xfe000000-0xfe400000 for CONFIG_HIGHMEM/PKMAP area
  *
  * From PCI to Processor:
  *   System Memory: 0x00000000 -> 0x00000000
  */

#ifndef __ASMPPC_PRPMC800_H
#define __ASMPPC_PRPMC800_H

#define PRPMC800_PCI_CONFIG_ADDR		0xfe000cf8
#define PRPMC800_PCI_CONFIG_DATA		0xfe000cfc

#define PRPMC800_PROC_PCI_IO_START		0xfe400000U
#define PRPMC800_PROC_PCI_IO_END		0xfeefffffU
#define PRPMC800_PCI_IO_START			0x00000000U
#define PRPMC800_PCI_IO_END			0x00afffffU

#define PRPMC800_PROC_PCI_MEM_START		0x80000000U
#define PRPMC800_PROC_PCI_MEM_END		0x9fffffffU
#define PRPMC800_PCI_MEM_START			0x80000000U
#define PRPMC800_PCI_MEM_END			0x9fffffffU

#define PRPMC800_NM_PROC_PCI_MEM_START		0x40000000U
#define PRPMC800_NM_PROC_PCI_MEM_END		0xdfffffffU
#define PRPMC800_NM_PCI_MEM_START		0x40000000U
#define PRPMC800_NM_PCI_MEM_END			0xdfffffffU

#define PRPMC800_PCI_DRAM_OFFSET		0x00000000U
#define PRPMC800_PCI_PHY_MEM_OFFSET		0x00000000U

#define PRPMC800_ISA_IO_BASE			PRPMC800_PROC_PCI_IO_START
#define PRPMC800_ISA_MEM_BASE			0x00000000U

#define PRPMC800_HARRIER_XCSR_BASE		HARRIER_DEFAULT_XCSR_BASE
#define PRPMC800_HARRIER_MPIC_BASE		0xff000000

#define PRPMC800_SERIAL_1			0xfeff00c0

#define PRPMC800_BASE_BAUD			1843200

/*
 * interrupt vector number and priority for harrier internal interrupt
 * sources
 */
#define PRPMC800_INT_IRQ			16
#define PRPMC800_INT_PRI			15

/* UART Defines. */
#define RS_TABLE_SIZE  4

/* Rate for the 1.8432 Mhz clock for the onboard serial chip */
#define BASE_BAUD (PRPMC800_BASE_BAUD / 16)

#define STD_COM_FLAGS ASYNC_BOOT_AUTOCONF

/* UARTS are at IRQ 16 */
#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, PRPMC800_SERIAL_1, 16, STD_COM_FLAGS, /* ttyS0 */\
		iomem_base: (unsigned char *)PRPMC800_SERIAL_1,		\
		iomem_reg_shift: 0,					\
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif				/* __ASMPPC_PRPMC800_H */
