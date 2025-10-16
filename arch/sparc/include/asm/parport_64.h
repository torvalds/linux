/* SPDX-License-Identifier: GPL-2.0 */
/* parport.h: sparc64 specific parport initialization and dma.
 *
 * Copyright (C) 1999  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _ASM_SPARC64_PARPORT_H
#define _ASM_SPARC64_PARPORT_H 1

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <asm/ebus_dma.h>
#include <asm/ns87303.h>
#include <asm/prom.h>

#define PARPORT_PC_MAX_PORTS	PARPORT_MAX

/*
 * While sparc64 doesn't have an ISA DMA API, we provide something that looks
 * close enough to make parport_pc happy
 */
#define HAS_DMA

#ifdef CONFIG_PARPORT_PC_FIFO
static DEFINE_SPINLOCK(dma_spin_lock);

#define claim_dma_lock() \
({	unsigned long flags; \
	spin_lock_irqsave(&dma_spin_lock, flags); \
	flags; \
})

#define release_dma_lock(__flags) \
	spin_unlock_irqrestore(&dma_spin_lock, __flags);
#endif

static struct sparc_ebus_info {
	struct ebus_dma_info info;
	unsigned int addr;
	unsigned int count;
	int lock;

	struct parport *port;
} sparc_ebus_dmas[PARPORT_PC_MAX_PORTS];

static DECLARE_BITMAP(dma_slot_map, PARPORT_PC_MAX_PORTS);

static inline int request_dma(unsigned int dmanr, const char *device_id)
{
	if (dmanr >= PARPORT_PC_MAX_PORTS)
		return -EINVAL;
	if (xchg(&sparc_ebus_dmas[dmanr].lock, 1) != 0)
		return -EBUSY;
	return 0;
}

static inline void free_dma(unsigned int dmanr)
{
	if (dmanr >= PARPORT_PC_MAX_PORTS) {
		printk(KERN_WARNING "Trying to free DMA%d\n", dmanr);
		return;
	}
	if (xchg(&sparc_ebus_dmas[dmanr].lock, 0) == 0) {
		printk(KERN_WARNING "Trying to free free DMA%d\n", dmanr);
		return;
	}
}

static inline void enable_dma(unsigned int dmanr)
{
	ebus_dma_enable(&sparc_ebus_dmas[dmanr].info, 1);

	if (ebus_dma_request(&sparc_ebus_dmas[dmanr].info,
			     sparc_ebus_dmas[dmanr].addr,
			     sparc_ebus_dmas[dmanr].count))
		BUG();
}

static inline void disable_dma(unsigned int dmanr)
{
	ebus_dma_enable(&sparc_ebus_dmas[dmanr].info, 0);
}

static inline void clear_dma_ff(unsigned int dmanr)
{
	/* nothing */
}

static inline void set_dma_mode(unsigned int dmanr, char mode)
{
	ebus_dma_prepare(&sparc_ebus_dmas[dmanr].info, (mode != DMA_MODE_WRITE));
}

static inline void set_dma_addr(unsigned int dmanr, unsigned int addr)
{
	sparc_ebus_dmas[dmanr].addr = addr;
}

static inline void set_dma_count(unsigned int dmanr, unsigned int count)
{
	sparc_ebus_dmas[dmanr].count = count;
}

static inline unsigned int get_dma_residue(unsigned int dmanr)
{
	return ebus_dma_residue(&sparc_ebus_dmas[dmanr].info);
}

static int ecpp_probe(struct platform_device *op)
{
	unsigned long base = op->resource[0].start;
	unsigned long config = op->resource[1].start;
	unsigned long d_base = op->resource[2].start;
	unsigned long d_len;
	struct device_node *parent;
	struct parport *p;
	int slot, err;

	parent = op->dev.of_node->parent;
	if (of_node_name_eq(parent, "dma")) {
		p = parport_pc_probe_port(base, base + 0x400,
					  op->archdata.irqs[0], PARPORT_DMA_NOFIFO,
					  op->dev.parent->parent, 0);
		if (!p)
			return -ENOMEM;
		dev_set_drvdata(&op->dev, p);
		return 0;
	}

	for (slot = 0; slot < PARPORT_PC_MAX_PORTS; slot++) {
		if (!test_and_set_bit(slot, dma_slot_map))
			break;
	}
	err = -ENODEV;
	if (slot >= PARPORT_PC_MAX_PORTS)
		goto out_err;

	spin_lock_init(&sparc_ebus_dmas[slot].info.lock);

	d_len = (op->resource[2].end - d_base) + 1UL;
	sparc_ebus_dmas[slot].info.regs =
		of_ioremap(&op->resource[2], 0, d_len, "ECPP DMA");

	if (!sparc_ebus_dmas[slot].info.regs)
		goto out_clear_map;

	sparc_ebus_dmas[slot].info.flags = 0;
	sparc_ebus_dmas[slot].info.callback = NULL;
	sparc_ebus_dmas[slot].info.client_cookie = NULL;
	sparc_ebus_dmas[slot].info.irq = 0xdeadbeef;
	strscpy(sparc_ebus_dmas[slot].info.name, "parport");
	if (ebus_dma_register(&sparc_ebus_dmas[slot].info))
		goto out_unmap_regs;

	ebus_dma_irq_enable(&sparc_ebus_dmas[slot].info, 1);

	/* Configure IRQ to Push Pull, Level Low */
	/* Enable ECP, set bit 2 of the CTR first */
	outb(0x04, base + 0x02);
	ns87303_modify(config, PCR,
		       PCR_EPP_ENABLE |
		       PCR_IRQ_ODRAIN,
		       PCR_ECP_ENABLE |
		       PCR_ECP_CLK_ENA |
		       PCR_IRQ_POLAR);

	/* CTR bit 5 controls direction of port */
	ns87303_modify(config, PTR,
		       0, PTR_LPT_REG_DIR);

	p = parport_pc_probe_port(base, base + 0x400,
				  op->archdata.irqs[0],
				  slot,
				  op->dev.parent,
				  0);
	err = -ENOMEM;
	if (!p)
		goto out_disable_irq;

	dev_set_drvdata(&op->dev, p);

	return 0;

out_disable_irq:
	ebus_dma_irq_enable(&sparc_ebus_dmas[slot].info, 0);
	ebus_dma_unregister(&sparc_ebus_dmas[slot].info);

out_unmap_regs:
	of_iounmap(&op->resource[2], sparc_ebus_dmas[slot].info.regs, d_len);

out_clear_map:
	clear_bit(slot, dma_slot_map);

out_err:
	return err;
}

static void ecpp_remove(struct platform_device *op)
{
	struct parport *p = dev_get_drvdata(&op->dev);
	int slot = p->dma;

	parport_pc_unregister_port(p);

	if (slot != PARPORT_DMA_NOFIFO) {
		unsigned long d_base = op->resource[2].start;
		unsigned long d_len;

		d_len = (op->resource[2].end - d_base) + 1UL;

		ebus_dma_irq_enable(&sparc_ebus_dmas[slot].info, 0);
		ebus_dma_unregister(&sparc_ebus_dmas[slot].info);
		of_iounmap(&op->resource[2],
			   sparc_ebus_dmas[slot].info.regs,
			   d_len);
		clear_bit(slot, dma_slot_map);
	}
}

static const struct of_device_id ecpp_match[] = {
	{
		.name = "ecpp",
	},
	{
		.name = "parallel",
		.compatible = "ecpp",
	},
	{
		.name = "parallel",
		.compatible = "ns87317-ecpp",
	},
	{
		.name = "parallel",
		.compatible = "pnpALI,1533,3",
	},
	{},
};

static struct platform_driver ecpp_driver = {
	.driver = {
		.name = "ecpp",
		.of_match_table = ecpp_match,
	},
	.probe			= ecpp_probe,
	.remove			= ecpp_remove,
};

static int parport_pc_find_nonpci_ports(int autoirq, int autodma)
{
	return platform_driver_register(&ecpp_driver);
}

#endif /* !(_ASM_SPARC64_PARPORT_H */
