/*
 *  pxi.c
 *
 *  Copyright (C) 2016 Sergi Granell <xerpi.g.12@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ioport.h>
#include <asm/io.h>
#include <mach/platform.h>
#include <mach/pxi.h>

u8 __iomem *pxi_base = NULL;

void pxi_init(void)
{
	if (request_mem_region(NINTENDO3DS_PXI_REGS_BASE,
			       NINTENDO3DS_PXI_REGS_SIZE, "pxi")) {
		pxi_base = ioremap_nocache(NINTENDO3DS_PXI_REGS_BASE,
					   NINTENDO3DS_PXI_REGS_SIZE);

		PXI_REG_WRITE(PXI_REG_CNT11_OFFSET,
			PXI_CNT_SEND_FIFO_EMPTY_IRQ |
			PXI_CNT_RECV_FIFO_NOT_EMPTY_IRQ |
			PXI_CNT_SEND_FIFO_FLUSH |
			PXI_CNT_FIFO_ENABLE);

		printk("Nintendo 3DS: PXI mapped to: %p\n",
			(void *)NINTENDO3DS_PXI_REGS_BASE);
	} else {
		printk("Nintendo 3DS: PXI region not available.\n");
	}
}
EXPORT_SYMBOL(pxi_init);

void pxi_deinit(void)
{
	if (pxi_base) {
		iounmap(pxi_base);
		release_mem_region(NINTENDO3DS_PXI_REGS_BASE,
				   NINTENDO3DS_PXI_REGS_SIZE);
	}
}
EXPORT_SYMBOL(pxi_deinit);
