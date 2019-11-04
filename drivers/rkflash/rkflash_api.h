/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_FLASH_API_H
#define __RK_FLASH_API_H

#ifdef CONFIG_RK_NANDC_NAND
#include "flash.h"
#endif
#ifdef CONFIG_RK_SFC_NAND
#include "sfc_nand.h"
#endif
#ifdef	CONFIG_RK_SFC_NOR
#include "sfc_nor.h"
#endif

enum flash_con_type {
	FLASH_CON_TYPE_NANDC = 0,
	FLASH_CON_TYPE_SFC,
	FLASH_CON_TYPE_MAX,
};

enum flash_type {
	FLASH_TYPE_NANDC_NAND = 0,
	FLASH_TYPE_SFC_NOR,
	FLASH_TYPE_SFC_NAND,
	FLASH_TYPE_MAX,
};

struct flash_boot_ops {
	int (*init)(void __iomem *reg_addr);
	int (*read)(u32 sec, u32 n_sec, void *p_data);
	int (*write)(u32 sec, u32 n_sec, void *p_data);
	u32 (*get_capacity)(void);
	void (*deinit)(void);
	int (*resume)(void __iomem *reg_addr);
	int (*vendor_read)(u32 sec, u32 n_sec, void *p_data);
	int (*vendor_write)(u32 sec, u32 n_sec, void *p_data);
	int (*gc)(void);
	int (*discard)(u32 sec, u32 n_sec);
};

#ifdef CONFIG_RK_NANDC_NAND
extern const struct flash_boot_ops nandc_nand_ops;
#endif

#ifdef CONFIG_RK_SFC_NOR
extern struct SFNOR_DEV *sfnor_dev;
extern const struct flash_boot_ops sfc_nor_ops;
#endif

#ifdef	CONFIG_RK_SFC_NAND
extern struct SFNAND_DEV *sfnand_dev;
extern const struct flash_boot_ops sfc_nand_ops;
#endif

#endif
