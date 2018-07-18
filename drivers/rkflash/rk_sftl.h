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
int sftl_vendor_read(u32 index, u32 count, u8 *buf);
int sftl_vendor_write(u32 index, u32 count, u8 *buf);
int rk_sftl_vendor_read(u32 index, u32 count, u8 *buf);
int rk_sftl_vendor_write(u32 index, u32 count, u8 *buf);
int rk_sftl_vendor_register(void);
int rk_sftl_vendor_storage_init(void);
int rk_sftl_vendor_dev_ops_register(int (*read)(u32 sec,
						u32 n_sec,
						void *p_data),
				    int (*write)(u32 sec,
						 u32 n_sec,
						 void *p_data));
void *ftl_malloc(int n_size);
void ftl_free(void *p);
void *ftl_memset(void *s, int c, unsigned int n);
void *ftl_memcpy(void *pv_to,
		 const void *pv_from,
		 unsigned int size);
#endif
