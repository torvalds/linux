/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <asm/page.h>
#include <asm/sizes.h>
#include <asm/mach/map.h>

#include "hardware.h"

#define IMX6Q_UART1_BASE_ADDR	0x02020000
#define IMX6Q_UART2_BASE_ADDR	0x021e8000
#define IMX6Q_UART3_BASE_ADDR	0x021ec000
#define IMX6Q_UART4_BASE_ADDR	0x021f0000
#define IMX6Q_UART5_BASE_ADDR	0x021f4000

/*
 * IMX6Q_UART_BASE_ADDR is put in the middle to force the expansion
 * of IMX6Q_UART##n##_BASE_ADDR.
 */
#define IMX6Q_UART_BASE_ADDR(n)	IMX6Q_UART##n##_BASE_ADDR
#define IMX6Q_UART_BASE(n)	IMX6Q_UART_BASE_ADDR(n)
#define IMX6Q_DEBUG_UART_BASE	IMX6Q_UART_BASE(CONFIG_DEBUG_IMX6Q_UART_PORT)

static struct map_desc imx_lluart_desc = {
#ifdef CONFIG_DEBUG_IMX6Q_UART
	.virtual	= IMX_IO_P2V(IMX6Q_DEBUG_UART_BASE),
	.pfn		= __phys_to_pfn(IMX6Q_DEBUG_UART_BASE),
	.length		= 0x4000,
	.type		= MT_DEVICE,
#endif
};

void __init imx_lluart_map_io(void)
{
	if (imx_lluart_desc.virtual)
		iotable_init(&imx_lluart_desc, 1);
}
