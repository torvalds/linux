/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __DRV_TYPES_GSPI_H__
#define __DRV_TYPES_GSPI_H__

// SPI Header Files
#ifdef PLATFORM_LINUX
	#include <linux/platform_device.h>
	#include <linux/spi/spi.h>
	#include <linux/gpio.h>
	//#include <mach/ldo.h>
	#include <asm/mach-types.h>
	#include <asm/gpio.h>
	#include <asm/io.h>
	#include <mach/board.h>
	#include <mach/hardware.h>
	#include <mach/irqs.h>
	#include <custom_gpio.h>
#endif


typedef struct gspi_data
{
	u8  func_number;

	u8  tx_block_mode;
	u8  rx_block_mode;
	u32 block_transfer_len;

#ifdef PLATFORM_LINUX
	struct spi_device *func;

	struct workqueue_struct *priv_wq;
	struct delayed_work irq_work;
#endif
} GSPI_DATA, *PGSPI_DATA;

#endif // #ifndef __DRV_TYPES_GSPI_H__

