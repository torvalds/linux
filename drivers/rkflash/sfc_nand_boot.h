/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _SFC_NOR_BOOT_H
#define _SFC_NOR_BOOT_H

int snand_init(void __iomem *reg_addr);
int snand_read(unsigned int sec, unsigned int n_sec, void *p_data);
int snand_write(unsigned int sec, unsigned int n_sec, void *p_data);
unsigned int snand_get_capacity(void);
void snand_deinit(void);
int snand_resume(void __iomem *reg_addr);
void sfc_clean_irq(void);

#endif
