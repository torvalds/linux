/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __MTK_PLATFORM_DEBUG_H__
#define __MTK_PLATFORM_DEBUG_H__

#ifdef CONFIG_MTK_PLAT_SRAM_FLAG
/* plat_sram_flag */
extern int set_sram_flag_lastpc_valid(void);
extern int set_sram_flag_dfd_valid(void);
extern int set_sram_flag_etb_user(unsigned int etb_id, unsigned int user_id);
#endif

#ifdef CONFIG_MTK_DFD_INTERNAL_DUMP
extern int dfd_setup(void);
#endif

#endif /* __MTK_PLATFORM_DEBUG_H__ */
