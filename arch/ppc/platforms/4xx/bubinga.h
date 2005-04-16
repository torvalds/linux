/*
 * Support for IBM PPC 405EP evaluation board (Bubinga).
 *
 * Author: SAW (IBM), derived from walnut.h.
 *         Maintained by MontaVista Software <source@mvista.com>
 *
 * 2003 (c) MontaVista Softare Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __BUBINGA_H__
#define __BUBINGA_H__

/* 405EP */
#include <platforms/4xx/ibm405ep.h>

#ifndef __ASSEMBLY__
/*
 * Data structure defining board information maintained by the boot
 * ROM on IBM's evaluation board. An effort has been made to
 * keep the field names consistent with the 8xx 'bd_t' board info
 * structures.
 */

typedef struct board_info {
        unsigned char    bi_s_version[4];       /* Version of this structure */
        unsigned char    bi_r_version[30];      /* Version of the IBM ROM */
        unsigned int     bi_memsize;            /* DRAM installed, in bytes */
        unsigned char    bi_enetaddr[2][6];     /* Local Ethernet MAC address */        unsigned char    bi_pci_enetaddr[6];    /* PCI Ethernet MAC address */
        unsigned int     bi_intfreq;            /* Processor speed, in Hz */
        unsigned int     bi_busfreq;            /* PLB Bus speed, in Hz */
        unsigned int     bi_pci_busfreq;        /* PCI Bus speed, in Hz */
        unsigned int     bi_opb_busfreq;        /* OPB Bus speed, in Hz */
        unsigned int     bi_pllouta_freq;       /* PLL OUTA speed, in Hz */
} bd_t;

/* Some 4xx parts use a different timebase frequency from the internal clock.
*/
#define bi_tbfreq bi_intfreq


/* Memory map for the Bubinga board.
 * Generic 4xx plus RTC.
 */

extern void *bubinga_rtc_base;
#define BUBINGA_RTC_PADDR	((uint)0xf0000000)
#define BUBINGA_RTC_VADDR	BUBINGA_RTC_PADDR
#define BUBINGA_RTC_SIZE	((uint)8*1024)

/* The UART clock is based off an internal clock -
 * define BASE_BAUD based on the internal clock and divider(s).
 * Since BASE_BAUD must be a constant, we will initialize it
 * using clock/divider values which OpenBIOS initializes
 * for typical configurations at various CPU speeds.
 * The base baud is calculated as (FWDA / EXT UART DIV / 16)
 */
#define BASE_BAUD       0

#define BUBINGA_FPGA_BASE      0xF0300000

#define PPC4xx_MACHINE_NAME     "IBM Bubinga"

#endif /* !__ASSEMBLY__ */
#endif /* __BUBINGA_H__ */
#endif /* __KERNEL__ */
