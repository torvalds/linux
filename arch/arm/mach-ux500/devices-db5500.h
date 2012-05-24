/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DEVICES_DB5500_H
#define __DEVICES_DB5500_H

#include "devices-common.h"

#define db5500_add_i2c1(parent, pdata) \
	dbx500_add_i2c(parent, 1, U5500_I2C1_BASE, IRQ_DB5500_I2C1, pdata)
#define db5500_add_i2c2(parent, pdata) \
	dbx500_add_i2c(parent, 2, U5500_I2C2_BASE, IRQ_DB5500_I2C2, pdata)
#define db5500_add_i2c3(parent, pdata) \
	dbx500_add_i2c(parent, 3, U5500_I2C3_BASE, IRQ_DB5500_I2C3, pdata)

#define db5500_add_msp0_spi(parent, pdata) \
	dbx500_add_msp_spi(parent, "msp0", U5500_MSP0_BASE, \
			   IRQ_DB5500_MSP0, pdata)
#define db5500_add_msp1_spi(parent, pdata) \
	dbx500_add_msp_spi(parent, "msp1", U5500_MSP1_BASE, \
			   IRQ_DB5500_MSP1, pdata)
#define db5500_add_msp2_spi(parent, pdata) \
	dbx500_add_msp_spi(parent, "msp2", U5500_MSP2_BASE, \
			   IRQ_DB5500_MSP2, pdata)

#define db5500_add_msp0_spi(parent, pdata) \
	dbx500_add_msp_spi(parent, "msp0", U5500_MSP0_BASE, \
			  IRQ_DB5500_MSP0, pdata)
#define db5500_add_msp1_spi(parent, pdata) \
	dbx500_add_msp_spi(parent, "msp1", U5500_MSP1_BASE, \
			  IRQ_DB5500_MSP1, pdata)
#define db5500_add_msp2_spi(parent, pdata) \
	dbx500_add_msp_spi(parent, "msp2", U5500_MSP2_BASE, \
			  IRQ_DB5500_MSP2, pdata)

#define db5500_add_rtc(parent) \
	dbx500_add_rtc(parent, U5500_RTC_BASE, IRQ_DB5500_RTC);

#define db5500_add_usb(parent, rx_cfg, tx_cfg) \
	ux500_add_usb(parent, U5500_USBOTG_BASE, \
		      IRQ_DB5500_USBOTG, rx_cfg, tx_cfg)

#define db5500_add_sdi0(parent, pdata) \
	dbx500_add_sdi(parent, "sdi0", U5500_SDI0_BASE, \
		       IRQ_DB5500_SDMMC0, pdata,	\
		       0x10480180)
#define db5500_add_sdi1(parent, pdata) \
	dbx500_add_sdi(parent, "sdi1", U5500_SDI1_BASE, \
		       IRQ_DB5500_SDMMC1, pdata,	\
		       0x10480180)
#define db5500_add_sdi2(parent, pdata) \
	dbx500_add_sdi(parent, "sdi2", U5500_SDI2_BASE, \
		       IRQ_DB5500_SDMMC2, pdata		\
		       0x10480180)
#define db5500_add_sdi3(parent, pdata) \
	dbx500_add_sdi(parent, "sdi3", U5500_SDI3_BASE, \
		       IRQ_DB5500_SDMMC3, pdata		\
		       0x10480180)
#define db5500_add_sdi4(parent, pdata) \
	dbx500_add_sdi(parent, "sdi4", U5500_SDI4_BASE, \
		       IRQ_DB5500_SDMMC4, pdata		\
		       0x10480180)

/* This one has a bad peripheral ID in the U5500 silicon */
#define db5500_add_spi0(parent, pdata) \
	dbx500_add_spi(parent, "spi0", U5500_SPI0_BASE, \
		       IRQ_DB5500_SPI0, pdata,		\
		       0x10080023)
#define db5500_add_spi1(parent, pdata) \
	dbx500_add_spi(parent, "spi1", U5500_SPI1_BASE, \
		       IRQ_DB5500_SPI1, pdata,		\
		       0x10080023)
#define db5500_add_spi2(parent, pdata) \
	dbx500_add_spi(parent, "spi2", U5500_SPI2_BASE, \
		       IRQ_DB5500_SPI2, pdata		\
		       0x10080023)
#define db5500_add_spi3(parent, pdata) \
	dbx500_add_spi(parent, "spi3", U5500_SPI3_BASE, \
		       IRQ_DB5500_SPI3, pdata		\
		       0x10080023)

#define db5500_add_uart0(parent, plat) \
	dbx500_add_uart(parent, "uart0", U5500_UART0_BASE, \
			IRQ_DB5500_UART0, plat)
#define db5500_add_uart1(parent, plat) \
	dbx500_add_uart(parent, "uart1", U5500_UART1_BASE, \
			IRQ_DB5500_UART1, plat)
#define db5500_add_uart2(parent, plat) \
	dbx500_add_uart(parent, "uart2", U5500_UART2_BASE, \
			IRQ_DB5500_UART2, plat)
#define db5500_add_uart3(parent, plat) \
	dbx500_add_uart(parent, "uart3", U5500_UART3_BASE, \
			IRQ_DB5500_UART3, plat)

#endif
