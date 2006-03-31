/*
 * include/asm-ppc/xparameters.h
 *
 * This file includes the correct xparameters.h for the CONFIG'ed board plus
 * fixups to translate board specific XPAR values to a common set of names
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2004 (c) MontaVista Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/config.h>

#if defined(CONFIG_XILINX_ML300)
  #include "xparameters_ml300.h"
#elif defined(CONFIG_XILINX_ML403)
  #include "xparameters_ml403.h"
#else
  /* Add other board xparameter includes here before the #else */
  #error No xparameters_*.h file included
#endif

#ifndef SERIAL_PORT_DFNS
  /* zImage serial port definitions */
  #define RS_TABLE_SIZE 1
  #define SERIAL_PORT_DFNS {						\
	.baud_base	 = XPAR_UARTNS550_0_CLOCK_FREQ_HZ/16,		\
	.irq		 = XPAR_INTC_0_UARTNS550_0_VEC_ID,		\
	.flags		 = ASYNC_BOOT_AUTOCONF,				\
	.iomem_base	 = (u8 *)XPAR_UARTNS550_0_BASEADDR + 3,		\
	.iomem_reg_shift = 2,						\
	.io_type	 = SERIAL_IO_MEM,				\
  },
#endif
