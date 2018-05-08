/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */

/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */

#ifndef __RK_NAND_BASE_H
#define __RK_NAND_BASE_H

struct rk_nandc_info {
	int	id;
	void __iomem     *reg_base;
	int	irq;
	int	clk_rate;
	struct clk	*clk;	/* flash clk*/
	struct clk	*hclk;	/* nandc clk*/
	struct clk	*gclk;  /* flash clk gate*/
};

void *ftl_malloc(int size);
void ftl_free(void *buf);
char rknand_get_sn(char *pbuf);
char rknand_get_vendor0(char *pbuf);
char *rknand_get_idb_data(void);
int rknand_get_clk_rate(int nandc_id);
unsigned long rknand_dma_flush_dcache(unsigned long ptr, int size, int dir);
unsigned long rknand_dma_map_single(unsigned long ptr, int size, int dir);
void rknand_dma_unmap_single(unsigned long ptr, int size, int dir);
int rknand_flash_cs_init(int id);
int rknand_get_reg_addr(unsigned long *p_nandc0, unsigned long *p_nandc1);
int rknand_get_boot_media(void);
unsigned long rk_copy_from_user(void *to, const void __user *from,
				unsigned long n);
unsigned long rk_copy_to_user(void __user *to, const void *from,
			      unsigned long n);
int rknand_sys_storage_init(void);
int rknand_vendor_storage_init(void);
int rk_nand_schedule_enable_config(int en);
void rk_nandc_xfer_irq_flag_init(void *nandc_reg);
void rk_nandc_rb_irq_flag_init(void *nandc_reg);
void wait_for_nandc_xfer_completed(void *nandc_reg);
void wait_for_nand_flash_ready(void *nandc_reg);
int rk_nandc_irq_init(void);
int rk_nandc_irq_deinit(void);
void rknand_dev_cache_flush(void);
#endif
