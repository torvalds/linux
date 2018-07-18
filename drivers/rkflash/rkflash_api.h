/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef __RK_FLASH_API_H
#define __RK_FLASH_API_H

#ifdef CONFIG_RK_NANDC_NAND
int sftl_flash_init(void __iomem *reg_addr);
void sftl_flash_read_id(u8 chip_sel, void *buf);
int sftl_flash_read(unsigned int sec, unsigned int n_sec, void *p_data);
int sftl_flash_write(unsigned int sec, unsigned int n_sec, void *p_data);
int sftl_flash_vendor_read(unsigned int sec, unsigned int n_sec, void *p_data);
int sftl_flash_vendor_write(unsigned int sec, unsigned int n_sec, void *p_data);
unsigned int sftl_flash_get_capacity(void);
void sftl_flash_deinit(void);
int sftl_flash_resume(void __iomem *reg_addr);
void sftl_flash_clean_irq(void);
#endif

#ifdef CONFIG_RK_SFC_NOR
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

#ifdef CONFIG_RK_SFC_NAND
int snand_init(void __iomem *reg_addr);
int snand_read(unsigned int sec, unsigned int n_sec, void *p_data);
int snand_write(unsigned int sec, unsigned int n_sec, void *p_data);
int snand_vendor_read(unsigned int sec, unsigned int n_sec, void *p_data);
int snand_vendor_write(unsigned int sec, unsigned int n_sec, void *p_data);
unsigned int snand_get_capacity(void);
void snand_deinit(void);
int snand_resume(void __iomem *reg_addr);
void sfc_clean_irq(void);
#endif

#endif
