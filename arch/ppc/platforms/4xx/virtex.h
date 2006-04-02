/*
 * arch/ppc/platforms/4xx/virtex.h
 *
 * Include file that defines the Xilinx Virtex-II Pro processor
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2004 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_VIRTEX_H__
#define __ASM_VIRTEX_H__

/* serial defines */

#include <asm/ibm405.h>

/* Ugly, ugly, ugly! BASE_BAUD defined here to keep 8250.c happy. */
#if !defined(BASE_BAUD)
 #define BASE_BAUD		(0) /* dummy value; not used */
#endif
  
/* Device type enumeration for platform bus definitions */
#ifndef __ASSEMBLY__
enum ppc_sys_devices {
	VIRTEX_UART, NUM_PPC_SYS_DEVS,
};
#endif
  
#endif				/* __ASM_VIRTEX_H__ */
#endif				/* __KERNEL__ */
