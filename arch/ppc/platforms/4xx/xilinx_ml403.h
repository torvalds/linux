/*
 * arch/ppc/platforms/4xx/xilinx_ml403.h
 *
 * Include file that defines the Xilinx ML403 reference design
 *
 * Author: Grant Likely <grant.likely@secretlab.ca>
 *
 * 2005 (c) Secret Lab Technologies Ltd.
 * 2002-2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_XILINX_ML403_H__
#define __ASM_XILINX_ML403_H__

/* ML403 has a Xilinx Virtex-4 FPGA with a PPC405 hard core */
#include <platforms/4xx/virtex.h>

#ifndef __ASSEMBLY__

#include <linux/types.h>

typedef struct board_info {
	unsigned int	 bi_memsize;		/* DRAM installed, in bytes */
	unsigned char	 bi_enetaddr[6];	/* Local Ethernet MAC address */
	unsigned int	 bi_intfreq;		/* Processor speed, in Hz */
	unsigned int	 bi_busfreq;		/* PLB Bus speed, in Hz */
	unsigned int	 bi_pci_busfreq;	/* PCI Bus speed, in Hz */
} bd_t;

/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq

#endif /* !__ASSEMBLY__ */

/* We don't need anything mapped.  Size of zero will accomplish that. */
#define PPC4xx_ONB_IO_PADDR	0u
#define PPC4xx_ONB_IO_VADDR	0u
#define PPC4xx_ONB_IO_SIZE	0u

#define PPC4xx_MACHINE_NAME "Xilinx ML403 Reference Design"

#endif /* __ASM_XILINX_ML403_H__ */
#endif /* __KERNEL__ */
