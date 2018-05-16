/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_SFTL_H
#define __RK_SFTL_H

u32 ftl_low_format(void);
int sftl_init(void);
int sftl_deinit(void);
int sftl_read(u32 index, u32 count, u8 *buf);
int sftl_write(u32 index, u32 count, u8 *buf);
u32 sftl_get_density(void);
s32 sftl_gc(void);

#endif
