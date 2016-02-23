/*
 * bdc_dbg.c - BRCM BDC USB3.0 device controller debug functions
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * Author: Ashwini Pahuja
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#include "bdc.h"
#include "bdc_dbg.h"

void bdc_dbg_regs(struct bdc *bdc)
{
	u32 temp;

	dev_vdbg(bdc->dev, "bdc->regs:%p\n", bdc->regs);
	temp = bdc_readl(bdc->regs, BDC_BDCCFG0);
	dev_vdbg(bdc->dev, "bdccfg0:0x%08x\n", temp);
	temp = bdc_readl(bdc->regs, BDC_BDCCFG1);
	dev_vdbg(bdc->dev, "bdccfg1:0x%08x\n", temp);
	temp = bdc_readl(bdc->regs, BDC_BDCCAP0);
	dev_vdbg(bdc->dev, "bdccap0:0x%08x\n", temp);
	temp = bdc_readl(bdc->regs, BDC_BDCCAP1);
	dev_vdbg(bdc->dev, "bdccap1:0x%08x\n", temp);
	temp = bdc_readl(bdc->regs, BDC_USPC);
	dev_vdbg(bdc->dev, "uspc:0x%08x\n", temp);
	temp = bdc_readl(bdc->regs, BDC_DVCSA);
	dev_vdbg(bdc->dev, "dvcsa:0x%08x\n", temp);
	temp = bdc_readl(bdc->regs, BDC_DVCSB);
	dev_vdbg(bdc->dev, "dvcsb:0x%x08\n", temp);
}

void bdc_dump_epsts(struct bdc *bdc)
{
	u32 temp;

	temp = bdc_readl(bdc->regs, BDC_EPSTS0(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS0:0x%08x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS1(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS1:0x%x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS2(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS2:0x%08x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS3(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS3:0x%08x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS4(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS4:0x%08x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS5(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS5:0x%08x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS6(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS6:0x%08x\n", temp);

	temp = bdc_readl(bdc->regs, BDC_EPSTS7(0));
	dev_vdbg(bdc->dev, "BDC_EPSTS7:0x%08x\n", temp);
}

void bdc_dbg_srr(struct bdc *bdc, u32 srr_num)
{
	struct bdc_sr *sr;
	dma_addr_t addr;
	int i;

	sr = bdc->srr.sr_bds;
	addr = bdc->srr.dma_addr;
	dev_vdbg(bdc->dev, "bdc_dbg_srr sr:%p dqp_index:%d\n",
						sr, bdc->srr.dqp_index);
	for (i = 0; i < NUM_SR_ENTRIES; i++) {
		sr = &bdc->srr.sr_bds[i];
		dev_vdbg(bdc->dev, "%llx %08x %08x %08x %08x\n",
					(unsigned long long)addr,
					le32_to_cpu(sr->offset[0]),
					le32_to_cpu(sr->offset[1]),
					le32_to_cpu(sr->offset[2]),
					le32_to_cpu(sr->offset[3]));
		addr += sizeof(*sr);
	}
}

void bdc_dbg_bd_list(struct bdc *bdc, struct bdc_ep *ep)
{
	struct bd_list *bd_list = &ep->bd_list;
	struct bd_table *bd_table;
	struct bdc_bd *bd;
	int tbi, bdi, gbdi;
	dma_addr_t dma;

	gbdi = 0;
	dev_vdbg(bdc->dev,
		"Dump bd list for %s epnum:%d\n",
		ep->name, ep->ep_num);

	dev_vdbg(bdc->dev,
		"tabs:%d max_bdi:%d eqp_bdi:%d hwd_bdi:%d num_bds_table:%d\n",
		bd_list->num_tabs, bd_list->max_bdi, bd_list->eqp_bdi,
		bd_list->hwd_bdi, bd_list->num_bds_table);

	for (tbi = 0; tbi < bd_list->num_tabs; tbi++) {
		bd_table = bd_list->bd_table_array[tbi];
		for (bdi = 0; bdi < bd_list->num_bds_table; bdi++) {
			bd =  bd_table->start_bd + bdi;
			dma = bd_table->dma + (sizeof(struct bdc_bd) * bdi);
			dev_vdbg(bdc->dev,
				"tbi:%2d bdi:%2d gbdi:%2d virt:%p phys:%llx %08x %08x %08x %08x\n",
				tbi, bdi, gbdi++, bd, (unsigned long long)dma,
				le32_to_cpu(bd->offset[0]),
				le32_to_cpu(bd->offset[1]),
				le32_to_cpu(bd->offset[2]),
				le32_to_cpu(bd->offset[3]));
		}
		dev_vdbg(bdc->dev, "\n\n");
	}
}
