/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/ctype.h>
#include <linux/edac.h>
#include <linux/genalloc.h>
#include <linux/of.h>
#include "altera_edac.h"

/* OCRAM ECC Management Group Defines */
#define ALTR_MAN_GRP_OCRAM_ECC_OFFSET	0x04
#define ALTR_OCR_ECC_EN_MASK		0x00000001
#define ALTR_OCR_ECC_INJS_MASK		0x00000002
#define ALTR_OCR_ECC_INJD_MASK		0x00000004
#define ALTR_OCR_ECC_SERR_MASK		0x00000008
#define ALTR_OCR_ECC_DERR_MASK		0x00000010

#ifdef CONFIG_EDAC_DEBUG
static void *ocram_init_mem(size_t size, void **other)
{
	struct device_node *np;
	struct gen_pool *gp;
	void *sram_addr;

	np = of_find_compatible_node(NULL, NULL, "altr,ocram-edac");
	if (!np)
		return NULL;

	gp = of_get_named_gen_pool(np, "iram", 0);
	if (!gp)
		return NULL;
	*other = gp;

	sram_addr = (void *)gen_pool_alloc(gp, size);
	if (!sram_addr)
		return NULL;

	memset(sram_addr, 0, size);
	wmb();	/* Ensure data is written out */

	return sram_addr;
}

static void ocram_free_mem(void *p, size_t size, void *other)
{
	gen_pool_free((struct gen_pool *)other, (u32)p, size);
}

static struct edac_dev_sysfs_attribute altr_ocr_sysfs_attributes[] = {
	{
		.attr = { .name = "altr_ocram_trigger",
			  .mode = (S_IRUGO | S_IWUSR) },
		.show = NULL,
		.store = altr_ecc_mgr_trig
	},
	{
		.attr = {.name = NULL }
	}
};
#endif	/* #ifdef CONFIG_EDAC_DEBUG */

const struct ecc_mgr_prv_data ocramecc_data = {
	.ce_clear_mask = (ALTR_OCR_ECC_EN_MASK | ALTR_OCR_ECC_SERR_MASK),
	.ue_clear_mask = (ALTR_OCR_ECC_EN_MASK | ALTR_OCR_ECC_DERR_MASK),
#ifdef CONFIG_EDAC_DEBUG
	.eccmgr_sysfs_attr = altr_ocr_sysfs_attributes,
	.init_mem = ocram_init_mem,
	.free_mem = ocram_free_mem,
	.ecc_enable_mask = ALTR_OCR_ECC_EN_MASK,
	.ce_set_mask = (ALTR_OCR_ECC_EN_MASK | ALTR_OCR_ECC_INJS_MASK),
	.ue_set_mask = (ALTR_OCR_ECC_EN_MASK | ALTR_OCR_ECC_INJD_MASK),
	.trig_alloc_sz = (32 * sizeof(u32)),
#endif
};
