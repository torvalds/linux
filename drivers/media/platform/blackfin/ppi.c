/*
 * ppi.c Analog Devices Parallel Peripheral Interface driver
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/slab.h>

#include <asm/bfin_ppi.h>
#include <asm/blackfin.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>
#include <asm/portmux.h>

#include <media/blackfin/ppi.h>

static int ppi_attach_irq(struct ppi_if *ppi, irq_handler_t handler);
static void ppi_detach_irq(struct ppi_if *ppi);
static int ppi_start(struct ppi_if *ppi);
static int ppi_stop(struct ppi_if *ppi);
static int ppi_set_params(struct ppi_if *ppi, struct ppi_params *params);
static void ppi_update_addr(struct ppi_if *ppi, unsigned long addr);

static const struct ppi_ops ppi_ops = {
	.attach_irq = ppi_attach_irq,
	.detach_irq = ppi_detach_irq,
	.start = ppi_start,
	.stop = ppi_stop,
	.set_params = ppi_set_params,
	.update_addr = ppi_update_addr,
};

static irqreturn_t ppi_irq_err(int irq, void *dev_id)
{
	struct ppi_if *ppi = dev_id;
	const struct ppi_info *info = ppi->info;

	switch (info->type) {
	case PPI_TYPE_PPI:
	{
		struct bfin_ppi_regs *reg = info->base;
		unsigned short status;

		/* register on bf561 is cleared when read 
		 * others are W1C
		 */
		status = bfin_read16(&reg->status);
		bfin_write16(&reg->status, 0xff00);
		break;
	}
	case PPI_TYPE_EPPI:
	{
		struct bfin_eppi_regs *reg = info->base;
		bfin_write16(&reg->status, 0xffff);
		break;
	}
	default:
		break;
	}

	return IRQ_HANDLED;
}

static int ppi_attach_irq(struct ppi_if *ppi, irq_handler_t handler)
{
	const struct ppi_info *info = ppi->info;
	int ret;

	ret = request_dma(info->dma_ch, "PPI_DMA");

	if (ret) {
		pr_err("Unable to allocate DMA channel for PPI\n");
		return ret;
	}
	set_dma_callback(info->dma_ch, handler, ppi);

	if (ppi->err_int) {
		ret = request_irq(info->irq_err, ppi_irq_err, 0, "PPI ERROR", ppi);
		if (ret) {
			pr_err("Unable to allocate IRQ for PPI\n");
			free_dma(info->dma_ch);
		}
	}
	return ret;
}

static void ppi_detach_irq(struct ppi_if *ppi)
{
	const struct ppi_info *info = ppi->info;

	if (ppi->err_int)
		free_irq(info->irq_err, ppi);
	free_dma(info->dma_ch);
}

static int ppi_start(struct ppi_if *ppi)
{
	const struct ppi_info *info = ppi->info;

	/* enable DMA */
	enable_dma(info->dma_ch);

	/* enable PPI */
	ppi->ppi_control |= PORT_EN;
	switch (info->type) {
	case PPI_TYPE_PPI:
	{
		struct bfin_ppi_regs *reg = info->base;
		bfin_write16(&reg->control, ppi->ppi_control);
		break;
	}
	case PPI_TYPE_EPPI:
	{
		struct bfin_eppi_regs *reg = info->base;
		bfin_write32(&reg->control, ppi->ppi_control);
		break;
	}
	default:
		return -EINVAL;
	}

	SSYNC();
	return 0;
}

static int ppi_stop(struct ppi_if *ppi)
{
	const struct ppi_info *info = ppi->info;

	/* disable PPI */
	ppi->ppi_control &= ~PORT_EN;
	switch (info->type) {
	case PPI_TYPE_PPI:
	{
		struct bfin_ppi_regs *reg = info->base;
		bfin_write16(&reg->control, ppi->ppi_control);
		break;
	}
	case PPI_TYPE_EPPI:
	{
		struct bfin_eppi_regs *reg = info->base;
		bfin_write32(&reg->control, ppi->ppi_control);
		break;
	}
	default:
		return -EINVAL;
	}

	/* disable DMA */
	clear_dma_irqstat(info->dma_ch);
	disable_dma(info->dma_ch);

	SSYNC();
	return 0;
}

static int ppi_set_params(struct ppi_if *ppi, struct ppi_params *params)
{
	const struct ppi_info *info = ppi->info;
	int dma32 = 0;
	int dma_config, bytes_per_line, lines_per_frame;

	bytes_per_line = params->width * params->bpp / 8;
	lines_per_frame = params->height;
	if (params->int_mask == 0xFFFFFFFF)
		ppi->err_int = false;
	else
		ppi->err_int = true;

	dma_config = (DMA_FLOW_STOP | WNR | RESTART | DMA2D | DI_EN);
	ppi->ppi_control = params->ppi_control & ~PORT_EN;
	switch (info->type) {
	case PPI_TYPE_PPI:
	{
		struct bfin_ppi_regs *reg = info->base;

		if (params->ppi_control & DMA32)
			dma32 = 1;

		bfin_write16(&reg->control, ppi->ppi_control);
		bfin_write16(&reg->count, bytes_per_line - 1);
		bfin_write16(&reg->frame, lines_per_frame);
		break;
	}
	case PPI_TYPE_EPPI:
	{
		struct bfin_eppi_regs *reg = info->base;

		if ((params->ppi_control & PACK_EN)
			|| (params->ppi_control & 0x38000) > DLEN_16)
			dma32 = 1;

		bfin_write32(&reg->control, ppi->ppi_control);
		bfin_write16(&reg->line, bytes_per_line + params->blank_clocks);
		bfin_write16(&reg->frame, lines_per_frame);
		bfin_write16(&reg->hdelay, 0);
		bfin_write16(&reg->vdelay, 0);
		bfin_write16(&reg->hcount, bytes_per_line);
		bfin_write16(&reg->vcount, lines_per_frame);
		break;
	}
	default:
		return -EINVAL;
	}

	if (dma32) {
		dma_config |= WDSIZE_32;
		set_dma_x_count(info->dma_ch, bytes_per_line >> 2);
		set_dma_x_modify(info->dma_ch, 4);
		set_dma_y_modify(info->dma_ch, 4);
	} else {
		dma_config |= WDSIZE_16;
		set_dma_x_count(info->dma_ch, bytes_per_line >> 1);
		set_dma_x_modify(info->dma_ch, 2);
		set_dma_y_modify(info->dma_ch, 2);
	}
	set_dma_y_count(info->dma_ch, lines_per_frame);
	set_dma_config(info->dma_ch, dma_config);

	SSYNC();
	return 0;
}

static void ppi_update_addr(struct ppi_if *ppi, unsigned long addr)
{
	set_dma_start_addr(ppi->info->dma_ch, addr);
}

struct ppi_if *ppi_create_instance(const struct ppi_info *info)
{
	struct ppi_if *ppi;

	if (!info || !info->pin_req)
		return NULL;

	if (peripheral_request_list(info->pin_req, KBUILD_MODNAME)) {
		pr_err("request peripheral failed\n");
		return NULL;
	}

	ppi = kzalloc(sizeof(*ppi), GFP_KERNEL);
	if (!ppi) {
		peripheral_free_list(info->pin_req);
		pr_err("unable to allocate memory for ppi handle\n");
		return NULL;
	}
	ppi->ops = &ppi_ops;
	ppi->info = info;

	pr_info("ppi probe success\n");
	return ppi;
}

void ppi_delete_instance(struct ppi_if *ppi)
{
	peripheral_free_list(ppi->info->pin_req);
	kfree(ppi);
}
