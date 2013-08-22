/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DEVICES_DB8500_H
#define __DEVICES_DB8500_H

#include "irqs.h"
#include "db8500-regs.h"
#include "devices-common.h"

struct platform_device;

extern struct ab8500_platform_data ab8500_platdata;
extern struct prcmu_pdata db8500_prcmu_pdata;
extern struct platform_device db8500_prcmu_device;

#define db8500_add_uart0(parent, pdata) \
	dbx500_add_uart(parent, "uart0", U8500_UART0_BASE, \
			IRQ_DB8500_UART0, pdata)
#define db8500_add_uart1(parent, pdata) \
	dbx500_add_uart(parent, "uart1", U8500_UART1_BASE, \
			IRQ_DB8500_UART1, pdata)
#define db8500_add_uart2(parent, pdata) \
	dbx500_add_uart(parent, "uart2", U8500_UART2_BASE, \
			IRQ_DB8500_UART2, pdata)

#endif
