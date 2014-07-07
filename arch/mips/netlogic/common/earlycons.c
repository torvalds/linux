/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/types.h>
#include <linux/serial_reg.h>

#include <asm/mipsregs.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/common.h>

#if defined(CONFIG_CPU_XLP)
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/uart.h>
#elif defined(CONFIG_CPU_XLR)
#include <asm/netlogic/xlr/iomap.h>
#endif

void prom_putchar(char c)
{
	uint64_t uartbase;

#if defined(CONFIG_CPU_XLP)
	uartbase = nlm_get_uart_regbase(0, 0);
#elif defined(CONFIG_CPU_XLR)
	uartbase = nlm_mmio_base(NETLOGIC_IO_UART_0_OFFSET);
#endif
	while ((nlm_read_reg(uartbase, UART_LSR) & UART_LSR_THRE) == 0)
		;
	nlm_write_reg(uartbase, UART_TX, c);
}
