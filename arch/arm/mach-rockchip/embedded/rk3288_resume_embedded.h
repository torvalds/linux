/*
 * Rockchip resume header (API between files in embedded code)
 *
 * Copyright (c) 2014 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_ROCKCHIP_RK3288_RESUME_EMBEDDED_H
#define __MACH_ROCKCHIP_RK3288_RESUME_EMBEDDED_H

/* Defined in the linker script */
extern u32 *__stack_start;

/* Defined by SDRAM code */
void rk3288_ddr_resume_early(struct rk3288_ddr_save_data *ddr_save_data);

#endif /* __MACH_ROCKCHIP_RK3288_RESUME_EMBEDDED_H */
