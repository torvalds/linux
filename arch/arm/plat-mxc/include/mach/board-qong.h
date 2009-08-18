/*
 * Copyright 2009 Ilya Yanok, Emcraft Systems Ltd, <yanok@emcraft.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BOARD_QONG_H__
#define __ASM_ARCH_MXC_BOARD_QONG_H__

/* mandatory for CONFIG_DEBUG_LL */

#define MXC_LL_UART_PADDR	UART1_BASE_ADDR
#define MXC_LL_UART_VADDR	AIPS1_IO_ADDRESS(UART1_BASE_ADDR)

/* NOR FLASH */
#define QONG_NOR_SIZE		(128*1024*1024)

#endif /* __ASM_ARCH_MXC_BOARD_QONG_H__ */
