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
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <asm/io.h>
#include <mach/platform.h>
#include <mach/pxi.h>

static u8 __iomem *pxi_base = NULL;
static DEFINE_MUTEX(pxi_lock);

#define PXI_REG_WRITE(reg, word) \
	iowrite32(word, pxi_base + reg)

#define PXI_REG_READ(reg) \
	ioread32(pxi_base + reg)

static inline int pxi_send_fifo_is_empty(void)
{
	return PXI_REG_READ(PXI_REG_CNT11_OFFSET) & PXI_CNT_SEND_FIFO_EMPTY;
}

static inline int pxi_send_fifo_is_full(void)
{
	return PXI_REG_READ(PXI_REG_CNT11_OFFSET) & PXI_CNT_SEND_FIFO_FULL;
}

static inline int pxi_recv_fifo_is_empty(void)
{
	return PXI_REG_READ(PXI_REG_CNT11_OFFSET) & PXI_CNT_RECV_FIFO_EMPTY;
}

static inline int pxi_recv_fifo_is_full(void)
{
	return PXI_REG_READ(PXI_REG_CNT11_OFFSET) & PXI_CNT_RECV_FIFO_FULL;
}

static inline void pxi_send_fifo_push(u32 word)
{
	PXI_REG_WRITE(PXI_REG_SEND11_OFFSET, word);
}

static inline u32 pxi_recv_fifo_pop(void)
{
	return PXI_REG_READ(PXI_REG_RECV11_OFFSET);
}

static inline void pxi_trigger_sync9_irq(void)
{
	PXI_REG_WRITE(PXI_REG_SYNC11_OFFSET,
		PXI_REG_READ(PXI_REG_SYNC11_OFFSET) | PXI_SYNC_TRIGGER_PXI_SYNC9);
}

static void pxi_reset(void)
{
	unsigned int i;

	PXI_REG_WRITE(PXI_REG_SYNC11_OFFSET, 0);
	PXI_REG_WRITE(PXI_REG_CNT11_OFFSET,
		PXI_CNT_SEND_FIFO_FLUSH);

	for (i = 0; i < 32; i++) {
		PXI_REG_READ(PXI_REG_SEND11_OFFSET);
	}

	PXI_REG_WRITE(PXI_REG_CNT11_OFFSET,
		PXI_CNT_SEND_FIFO_EMPTY_IRQ |
		PXI_CNT_RECV_FIFO_NOT_EMPTY_IRQ |
		PXI_CNT_FIFO_ENABLE);

	PXI_REG_WRITE(PXI_REG_SYNC11_OFFSET, PXI_SYNC_IRQ_ENABLE);
}

static irqreturn_t sync_irq_handler(int irq, void *dummy)
{
	printk("GOT IRQ: %d\n", irq);
	return IRQ_HANDLED;
}

void pxi_send_cmd(struct pxi_cmd_hdr *cmd)
{
	unsigned int i;

	mutex_lock(&pxi_lock);

	while (pxi_send_fifo_is_full())
		;

	/*
	 * Send command ID and length.
	 */
	pxi_send_fifo_push(*(u32 *)cmd);
	pxi_trigger_sync9_irq();

	/*
	 * Send the command payload (if any).
	 */
	for (i = 0; i < cmd->len; i+=4) {
		pxi_send_fifo_push(cmd->data[i/4]);
	}

	/*
	 * Wait for the reply.
	 */
	do {
		while (pxi_recv_fifo_is_empty())
			;
	} while (pxi_recv_fifo_pop() != cmd->cmd);

	mutex_unlock(&pxi_lock);
}
EXPORT_SYMBOL(pxi_send_cmd);

static int __init pxi_init(void)
{
	unsigned int sync_hwirq;

	if (!request_mem_region(NINTENDO3DS_PXI_REGS_BASE,
			       NINTENDO3DS_PXI_REGS_SIZE, "pxi")) {
		printk("Nintendo 3DS: PXI region not available.\n");
		return -1;
	}

	pxi_base = ioremap_nocache(NINTENDO3DS_PXI_REGS_BASE,
				   NINTENDO3DS_PXI_REGS_SIZE);

	printk("Nintendo 3DS: PXI mapped to: %p\n", pxi_base);

	pxi_reset();

	sync_hwirq = irq_find_mapping(NULL, PXI_HWIRQ_SYNC);

	if (request_irq(sync_hwirq, sync_irq_handler, 0, "pxi_sync", NULL)) {
		printk(KERN_ERR "Can't allocate irq (0x%02X): %d\n",
			PXI_HWIRQ_SYNC, sync_hwirq);
		//return -1;
	}

	/**** TESTING *****/
	/*while (1)  {
		struct pxi_cmd_sdmmc_read_sector cmd = {
			.header.cmd = PXI_CMD_SDMMC_READ_SECTOR,
			.header.len = sizeof(cmd) - sizeof(struct pxi_cmd_hdr),
			.sector = 0x11223344,
			.paddr = 0xAABBCCDD
		};

		pxi_send_cmd((struct pxi_cmd_hdr *)&cmd);
	}*/

	return 0;
}

/*static void pxi_deinit(void)
{
	if (pxi_base) {
		iounmap(pxi_base);
		release_mem_region(NINTENDO3DS_PXI_REGS_BASE,
				   NINTENDO3DS_PXI_REGS_SIZE);
	}
}*/

subsys_initcall(pxi_init);
