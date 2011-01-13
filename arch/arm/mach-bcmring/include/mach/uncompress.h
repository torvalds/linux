/*****************************************************************************
* Copyright 2005 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/
#include <mach/csp/mm_addr.h>

#define BCMRING_UART_0_DR (*(volatile unsigned int *)MM_ADDR_IO_UARTA)
#define BCMRING_UART_0_FR (*(volatile unsigned int *)(MM_ADDR_IO_UARTA + 0x18))
/*
 * This does not append a newline
 */
static inline void putc(int c)
{
	/* Send out UARTA */
	while (BCMRING_UART_0_FR & (1 << 5))
		;

	BCMRING_UART_0_DR = c;
}


static inline void flush(void)
{
	/* Wait for the tx fifo to be empty */
	while ((BCMRING_UART_0_FR & (1 << 7)) == 0)
		;

	/* Wait for the final character to be sent on the txd line */
	while (BCMRING_UART_0_FR & (1 << 3))
		;
}

#define arch_decomp_setup()
#define arch_decomp_wdog()
