/*
 * arch/ppc/platforms/4xx/virtex-ii_pro.c
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2004 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/ocp.h>
#include "virtex-ii_pro.h"

/* Have OCP take care of the serial ports. */
struct ocp_def core_ocp[] = {
#ifdef XPAR_UARTNS550_0_BASEADDR
	{ .vendor	= OCP_VENDOR_XILINX,
	  .function	= OCP_FUNC_16550,
	  .index	= 0,
	  .paddr	= XPAR_UARTNS550_0_BASEADDR,
	  .irq		= XPAR_INTC_0_UARTNS550_0_VEC_ID,
	  .pm		= OCP_CPM_NA
	},
#ifdef XPAR_UARTNS550_1_BASEADDR
	{ .vendor	= OCP_VENDOR_XILINX,
	  .function	= OCP_FUNC_16550,
	  .index	= 1,
	  .paddr	= XPAR_UARTNS550_1_BASEADDR,
	  .irq		= XPAR_INTC_0_UARTNS550_1_VEC_ID,
	  .pm		= OCP_CPM_NA
	},
#ifdef XPAR_UARTNS550_2_BASEADDR
	{ .vendor	= OCP_VENDOR_XILINX,
	  .function	= OCP_FUNC_16550,
	  .index	= 2,
	  .paddr	= XPAR_UARTNS550_2_BASEADDR,
	  .irq		= XPAR_INTC_0_UARTNS550_2_VEC_ID,
	  .pm		= OCP_CPM_NA
	},
#ifdef XPAR_UARTNS550_3_BASEADDR
	{ .vendor	= OCP_VENDOR_XILINX,
	  .function	= OCP_FUNC_16550,
	  .index	= 3,
	  .paddr	= XPAR_UARTNS550_3_BASEADDR,
	  .irq		= XPAR_INTC_0_UARTNS550_3_VEC_ID,
	  .pm		= OCP_CPM_NA
	},
#ifdef XPAR_UARTNS550_4_BASEADDR
#error Edit this file to add more devices.
#endif			/* 4 */
#endif			/* 3 */
#endif			/* 2 */
#endif			/* 1 */
#endif			/* 0 */
	{ .vendor	= OCP_VENDOR_INVALID
	}
};
