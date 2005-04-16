/*
 * arch/ppc/platforms/4xx/sycamore.h
 *
 * Macros, definitions, and data structures specific to the IBM PowerPC
 * 405GPr "Sycamore" evaluation board.
 *
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_SYCAMORE_H__
#define __ASM_SYCAMORE_H__

#include <platforms/4xx/ibm405gpr.h>

#ifndef __ASSEMBLY__
/*
 * Data structure defining board information maintained by the boot
 * ROM on IBM's "Sycamore" evaluation board. An effort has been made to
 * keep the field names consistent with the 8xx 'bd_t' board info
 * structures.
 */

typedef struct board_info {
	unsigned char	 bi_s_version[4];	/* Version of this structure */
	unsigned char	 bi_r_version[30];	/* Version of the IBM ROM */
	unsigned int	 bi_memsize;		/* DRAM installed, in bytes */
	unsigned char	 bi_enetaddr[6];	/* Local Ethernet MAC address */
	unsigned char	 bi_pci_enetaddr[6];	/* PCI Ethernet MAC address */
	unsigned int	 bi_intfreq;		/* Processor speed, in Hz */
	unsigned int	 bi_busfreq;		/* PLB Bus speed, in Hz */
	unsigned int	 bi_pci_busfreq;	/* PCI Bus speed, in Hz */
} bd_t;

/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq


/* Memory map for the IBM "Sycamore" 405GP evaluation board.
 * Generic 4xx plus RTC.
 */

extern void *sycamore_rtc_base;
#define SYCAMORE_RTC_PADDR	((uint)0xf0000000)
#define SYCAMORE_RTC_VADDR	SYCAMORE_RTC_PADDR
#define SYCAMORE_RTC_SIZE		((uint)8*1024)

#ifdef CONFIG_PPC405GP_INTERNAL_CLOCK
#define BASE_BAUD		201600
#else
#define BASE_BAUD		691200
#endif

#define SYCAMORE_PS2_BASE		0xF0100000
#define SYCAMORE_FPGA_BASE	0xF0300000

#define PPC4xx_MACHINE_NAME	"IBM Sycamore"

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_SYCAMORE_H__ */
#endif /* __KERNEL__ */
