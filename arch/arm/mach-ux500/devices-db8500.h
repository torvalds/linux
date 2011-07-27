/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DEVICES_DB8500_H
#define __DEVICES_DB8500_H

#include "devices-common.h"

struct ske_keypad_platform_data;
struct pl022_ssp_controller;

static inline struct platform_device *
db8500_add_ske_keypad(struct ske_keypad_platform_data *pdata)
{
	return dbx500_add_platform_device_4k1irq("nmk-ske-keypad", -1,
						 U8500_SKE_BASE,
						 IRQ_DB8500_KB, pdata);
}

static inline struct amba_device *
db8500_add_ssp(const char *name, resource_size_t base, int irq,
	       struct pl022_ssp_controller *pdata)
{
	return dbx500_add_amba_device(name, base, irq, pdata, 0);
}


#define db8500_add_i2c0(pdata) \
	dbx500_add_i2c(0, U8500_I2C0_BASE, IRQ_DB8500_I2C0, pdata)
#define db8500_add_i2c1(pdata) \
	dbx500_add_i2c(1, U8500_I2C1_BASE, IRQ_DB8500_I2C1, pdata)
#define db8500_add_i2c2(pdata) \
	dbx500_add_i2c(2, U8500_I2C2_BASE, IRQ_DB8500_I2C2, pdata)
#define db8500_add_i2c3(pdata) \
	dbx500_add_i2c(3, U8500_I2C3_BASE, IRQ_DB8500_I2C3, pdata)
#define db8500_add_i2c4(pdata) \
	dbx500_add_i2c(4, U8500_I2C4_BASE, IRQ_DB8500_I2C4, pdata)

#define db8500_add_msp0_i2s(pdata) \
	dbx500_add_msp_i2s(0, U8500_MSP0_BASE, IRQ_DB8500_MSP0, pdata)
#define db8500_add_msp1_i2s(pdata) \
	dbx500_add_msp_i2s(1, U8500_MSP1_BASE, IRQ_DB8500_MSP1, pdata)
#define db8500_add_msp2_i2s(pdata) \
	dbx500_add_msp_i2s(2, U8500_MSP2_BASE, IRQ_DB8500_MSP2, pdata)
#define db8500_add_msp3_i2s(pdata) \
	dbx500_add_msp_i2s(3, U8500_MSP3_BASE, IRQ_DB8500_MSP1, pdata)

#define db8500_add_msp0_spi(pdata) \
	dbx500_add_msp_spi("msp0", U8500_MSP0_BASE, IRQ_DB8500_MSP0, pdata)
#define db8500_add_msp1_spi(pdata) \
	dbx500_add_msp_spi("msp1", U8500_MSP1_BASE, IRQ_DB8500_MSP1, pdata)
#define db8500_add_msp2_spi(pdata) \
	dbx500_add_msp_spi("msp2", U8500_MSP2_BASE, IRQ_DB8500_MSP2, pdata)
#define db8500_add_msp3_spi(pdata) \
	dbx500_add_msp_spi("msp3", U8500_MSP3_BASE, IRQ_DB8500_MSP1, pdata)

#define db8500_add_rtc() \
	dbx500_add_rtc(U8500_RTC_BASE, IRQ_DB8500_RTC);

#define db8500_add_usb(rx_cfg, tx_cfg) \
	ux500_add_usb(U8500_USBOTG_BASE, IRQ_DB8500_USBOTG, rx_cfg, tx_cfg)

#define db8500_add_sdi0(pdata, pid) \
	dbx500_add_sdi("sdi0", U8500_SDI0_BASE, IRQ_DB8500_SDMMC0, pdata, pid)
#define db8500_add_sdi1(pdata, pid) \
	dbx500_add_sdi("sdi1", U8500_SDI1_BASE, IRQ_DB8500_SDMMC1, pdata, pid)
#define db8500_add_sdi2(pdata, pid) \
	dbx500_add_sdi("sdi2", U8500_SDI2_BASE, IRQ_DB8500_SDMMC2, pdata, pid)
#define db8500_add_sdi3(pdata, pid) \
	dbx500_add_sdi("sdi3", U8500_SDI3_BASE, IRQ_DB8500_SDMMC3, pdata, pid)
#define db8500_add_sdi4(pdata, pid) \
	dbx500_add_sdi("sdi4", U8500_SDI4_BASE, IRQ_DB8500_SDMMC4, pdata, pid)
#define db8500_add_sdi5(pdata, pid) \
	dbx500_add_sdi("sdi5", U8500_SDI5_BASE, IRQ_DB8500_SDMMC5, pdata, pid)

#define db8500_add_ssp0(pdata) \
	db8500_add_ssp("ssp0", U8500_SSP0_BASE, IRQ_DB8500_SSP0, pdata)
#define db8500_add_ssp1(pdata) \
	db8500_add_ssp("ssp1", U8500_SSP1_BASE, IRQ_DB8500_SSP1, pdata)

#define db8500_add_spi0(pdata) \
	dbx500_add_spi("spi0", U8500_SPI0_BASE, IRQ_DB8500_SPI0, pdata, 0)
#define db8500_add_spi1(pdata) \
	dbx500_add_spi("spi1", U8500_SPI1_BASE, IRQ_DB8500_SPI1, pdata, 0)
#define db8500_add_spi2(pdata) \
	dbx500_add_spi("spi2", U8500_SPI2_BASE, IRQ_DB8500_SPI2, pdata, 0)
#define db8500_add_spi3(pdata) \
	dbx500_add_spi("spi3", U8500_SPI3_BASE, IRQ_DB8500_SPI3, pdata, 0)

#define db8500_add_uart0(pdata) \
	dbx500_add_uart("uart0", U8500_UART0_BASE, IRQ_DB8500_UART0, pdata)
#define db8500_add_uart1(pdata) \
	dbx500_add_uart("uart1", U8500_UART1_BASE, IRQ_DB8500_UART1, pdata)
#define db8500_add_uart2(pdata) \
	dbx500_add_uart("uart2", U8500_UART2_BASE, IRQ_DB8500_UART2, pdata)

#endif
