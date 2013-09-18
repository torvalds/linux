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

struct pl022_ssp_controller;
struct platform_device;

extern struct ab8500_platform_data ab8500_platdata;
extern struct prcmu_pdata db8500_prcmu_pdata;
extern struct platform_device db8500_prcmu_device;

static inline struct amba_device *
db8500_add_ssp(struct device *parent, const char *name, resource_size_t base,
	       int irq, struct pl022_ssp_controller *pdata)
{
	return amba_ahb_device_add(parent, name, base, SZ_4K, irq, 0, pdata, 0);
}

#define db8500_add_rtc(parent) \
	dbx500_add_rtc(parent, U8500_RTC_BASE, IRQ_DB8500_RTC);

#define db8500_add_ssp0(parent, pdata) \
	db8500_add_ssp(parent, "ssp0", U8500_SSP0_BASE, \
		       IRQ_DB8500_SSP0, pdata)
#define db8500_add_ssp1(parent, pdata) \
	db8500_add_ssp(parent, "ssp1", U8500_SSP1_BASE, \
		       IRQ_DB8500_SSP1, pdata)

#define db8500_add_spi0(parent, pdata) \
	dbx500_add_spi(parent, "spi0", U8500_SPI0_BASE, \
		       IRQ_DB8500_SPI0, pdata, 0)
#define db8500_add_spi1(parent, pdata) \
	dbx500_add_spi(parent, "spi1", U8500_SPI1_BASE, \
		       IRQ_DB8500_SPI1, pdata, 0)
#define db8500_add_spi2(parent, pdata) \
	dbx500_add_spi(parent, "spi2", U8500_SPI2_BASE, \
		       IRQ_DB8500_SPI2, pdata, 0)
#define db8500_add_spi3(parent, pdata) \
	dbx500_add_spi(parent, "spi3", U8500_SPI3_BASE, \
		       IRQ_DB8500_SPI3, pdata, 0)

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
