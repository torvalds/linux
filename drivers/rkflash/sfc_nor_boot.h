/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _SFC_NOR_BOOT_H
#define _SFC_NOR_BOOT_H

int spi_flash_init(void __iomem *reg_addr);
void spi_flash_read_id(u8 chip_sel, void *buf);
int snor_read_lba(unsigned int sec, unsigned int n_sec, void *p_data);
int snor_write_lba(unsigned int sec, unsigned int n_sec, void *p_data);
unsigned int snor_capacity(void);
void snor_deinit(void);
int snor_resume(void __iomem *reg_addr);
int snor_vendor_read(unsigned int sec, unsigned int n_sec, void *p_data);
int snor_vendor_write(unsigned int sec, unsigned int n_sec, void *p_data);

#endif

