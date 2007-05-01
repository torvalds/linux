/*
 * arch/ppc/platforms/4xx/xparameters/xparameters.h
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

/*
 * A few reasonable defaults for the #defines which could be missing depending
 * on the IP version or variant (e.g. OPB vs PLB)
 */

#ifndef XPAR_EMAC_0_CAM_EXIST
#define XPAR_EMAC_0_CAM_EXIST 0
#endif
#ifndef XPAR_EMAC_0_JUMBO_EXIST
#define XPAR_EMAC_0_JUMBO_EXIST 0
#endif
#ifndef XPAR_EMAC_0_TX_DRE_TYPE
#define XPAR_EMAC_0_TX_DRE_TYPE 0
#endif
#ifndef XPAR_EMAC_0_RX_DRE_TYPE
#define XPAR_EMAC_0_RX_DRE_TYPE 0
#endif
#ifndef XPAR_EMAC_0_TX_INCLUDE_CSUM
#define XPAR_EMAC_0_TX_INCLUDE_CSUM 0
#endif
#ifndef XPAR_EMAC_0_RX_INCLUDE_CSUM
#define XPAR_EMAC_0_RX_INCLUDE_CSUM 0
#endif

#ifndef XPAR_EMAC_1_CAM_EXIST
#define XPAR_EMAC_1_CAM_EXIST 0
#endif
#ifndef XPAR_EMAC_1_JUMBO_EXIST
#define XPAR_EMAC_1_JUMBO_EXIST 0
#endif
#ifndef XPAR_EMAC_1_TX_DRE_TYPE
#define XPAR_EMAC_1_TX_DRE_TYPE 0
#endif
#ifndef XPAR_EMAC_1_RX_DRE_TYPE
#define XPAR_EMAC_1_RX_DRE_TYPE 0
#endif
#ifndef XPAR_EMAC_1_TX_INCLUDE_CSUM
#define XPAR_EMAC_1_TX_INCLUDE_CSUM 0
#endif
#ifndef XPAR_EMAC_1_RX_INCLUDE_CSUM
#define XPAR_EMAC_1_RX_INCLUDE_CSUM 0
#endif

#ifndef XPAR_GPIO_0_IS_DUAL
#define XPAR_GPIO_0_IS_DUAL 0
#endif
#ifndef XPAR_GPIO_1_IS_DUAL
#define XPAR_GPIO_1_IS_DUAL 0
#endif
#ifndef XPAR_GPIO_2_IS_DUAL
#define XPAR_GPIO_2_IS_DUAL 0
#endif
#ifndef XPAR_GPIO_3_IS_DUAL
#define XPAR_GPIO_3_IS_DUAL 0
#endif
#ifndef XPAR_GPIO_4_IS_DUAL
#define XPAR_GPIO_4_IS_DUAL 0
#endif

