/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __DRV_TYPES_SDIO_H__
#define __DRV_TYPES_SDIO_H__

/* SDIO Header Files */
#ifdef PLATFORM_LINUX
	#include <linux/mmc/sdio_func.h>
	#include <linux/mmc/sdio_ids.h>
	#include <linux/mmc/host.h>
	#include <linux/mmc/card.h>

	#ifdef CONFIG_PLATFORM_SPRD
		#include <linux/gpio.h>
		#include <custom_gpio.h>
	#endif /* CONFIG_PLATFORM_SPRD */
#endif

#define RTW_SDIO_CLK_33M	33000000
#define RTW_SDIO_CLK_40M	40000000
#define RTW_SDIO_CLK_80M	80000000
#define RTW_SDIO_CLK_160M	160000000

typedef struct sdio_data {
	u8  func_number;

	u8  tx_block_mode;
	u8  rx_block_mode;
	u32 block_transfer_len;

#ifdef PLATFORM_LINUX
	struct mmc_card *card;
	struct sdio_func	*func;
	_thread_hdl_ sys_sdio_irq_thd;
	unsigned int clock;
	unsigned int timing;
	u8	sd3_bus_mode;
#endif

#ifdef DBG_SDIO
#ifdef PLATFORM_LINUX
	struct proc_dir_entry *proc_sdio_dbg;
#endif /* PLATFORM_LINUX */

	u32 cmd52_err_cnt;	/* CMD52 I/O error count */
	u32 cmd53_err_cnt;	/* CMD53 I/O error count */

#if (DBG_SDIO >= 1)
	u32 reg_dump_mark;	/* reg dump at specific error count */
#endif /* DBG_SDIO >= 1 */

#if (DBG_SDIO >= 2)
	u8 *dbg_msg;		/* Messages for debug */
	u8 dbg_msg_size;
	u8 *reg_mac;		/* Device MAC register, 0x0~0x800 */
	u8 *reg_mac_ext;	/* Device MAC extend register, 0x1000~0x1800 */
	u8 *reg_local;		/* Device SDIO local register, 0x0~0xFF */
	u8 *reg_cia;		/* SDIO CIA(CCCR, FBR and etc.), 0x0~0x1FF */
#endif /* DBG_SDIO >= 2 */

#if (DBG_SDIO >= 3)
	u8 dbg_enable;		/* 0/1: disable/enable debug mode */
	u8 err_stop;		/* Stop(surprise remove) when I/O error happen */
	u8 err_test;		/* Simulate error happen */
	u8 err_test_triggered;	/* Simulate error already triggered */
#endif /* DBG_SDIO >= 3 */
#endif /* DBG_SDIO */
} SDIO_DATA, *PSDIO_DATA;

#define dvobj_to_sdio_func(d)	((d)->intf_data.func)

#define RTW_SDIO_ADDR_CMD52_BIT		(1<<17)
#define RTW_SDIO_ADDR_CMD52_GEN(a)	(a | RTW_SDIO_ADDR_CMD52_BIT)
#define RTW_SDIO_ADDR_CMD52_CLR(a)	(a&~RTW_SDIO_ADDR_CMD52_BIT)
#define RTW_SDIO_ADDR_CMD52_CHK(a)	(a&RTW_SDIO_ADDR_CMD52_BIT ? 1 : 0)

#define RTW_SDIO_ADDR_F0_BIT		(1<<18)
#define RTW_SDIO_ADDR_F0_GEN(a)		(a | RTW_SDIO_ADDR_F0_BIT)
#define RTW_SDIO_ADDR_F0_CLR(a)		(a&~RTW_SDIO_ADDR_F0_BIT)
#define RTW_SDIO_ADDR_F0_CHK(a)		(a&RTW_SDIO_ADDR_F0_BIT ? 1 : 0)

#endif
