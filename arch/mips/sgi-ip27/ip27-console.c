/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2002 Ralf Baechle
 */

#include <asm/page.h>
#include <asm/setup.h>
#include <asm/sn/addrs.h>
#include <asm/sn/agent.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/ioc3.h>

#include <linux/serial.h>
#include <linux/serial_core.h>

#include "ip27-common.h"

#define IOC3_CLK	(22000000 / 3)
#define IOC3_FLAGS	(0)

static inline struct ioc3_uartregs *console_uart(void)
{
	struct ioc3 *ioc3;
	nasid_t nasid;

	nasid = (master_nasid == INVALID_NASID) ? get_nasid() : master_nasid;
	ioc3 = (struct ioc3 *)KL_CONFIG_CH_CONS_INFO(nasid)->memory_base;

	return &ioc3->sregs.uarta;
}

void prom_putchar(char c)
{
	struct ioc3_uartregs *uart = console_uart();

	while ((readb(&uart->iu_lsr) & 0x20) == 0)
		;
	writeb(c, &uart->iu_thr);
}
