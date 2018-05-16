/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_FLASH_API_H
#define __RK_FLASH_API_H

#ifdef CONFIG_RK_NANDC_NAND
#include "nand_boot.h"
#endif

#ifdef CONFIG_RK_SFC_NOR
#include "sfc_nor_boot.h"
#endif

#ifdef CONFIG_RK_SFC_NAND
#include "sfc_nand_boot.h"
#endif

#endif
