/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_BOARD_MX31LITE_H__
#define __ASM_ARCH_MXC_BOARD_MX31LITE_H__

#define MXC_MAX_EXP_IO_LINES	16


/*
 * Memory Size parameters
 */

/*
 * Size of SDRAM memory
 */
#define SDRAM_MEM_SIZE		SZ_128M
/*
 * Size of MBX buffer memory
 */
#define MXC_MBX_MEM_SIZE	SZ_16M
/*
 * Size of memory available to kernel
 */
#define MEM_SIZE		(SDRAM_MEM_SIZE - MXC_MBX_MEM_SIZE)

#define MXC_LL_UART_PADDR	UART1_BASE_ADDR
#define MXC_LL_UART_VADDR	AIPS1_IO_ADDRESS(UART1_BASE_ADDR)

#endif /* __ASM_ARCH_MXC_BOARD_MX31ADS_H__ */

