/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _SFC_H
#define _SFC_H

int sftl_flash_init(void __iomem *reg_addr);
void sftl_flash_read_id(u8 chip_sel, void *buf);
int sftl_flash_read(unsigned int sec, unsigned int n_sec, void *p_data);
int sftl_flash_write(unsigned int sec, unsigned int n_sec, void *p_data);
unsigned int sftl_flash_get_capacity(void);
void sftl_flash_deinit(void);
int sftl_flash_resume(void __iomem *reg_addr);
void sftl_flash_clean_irq(void);

#endif
