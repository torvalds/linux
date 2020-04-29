// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Atheros AR71xx/AR724x/AR913x specific interrupt handling
 *
 *  Copyright (C) 2015 Alban Bedel <albeu@free.fr>
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15/2.6.31 BSP
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/of.h>

#include <asm/irq_cpu.h>
#include <asm/mach-ath79/ath79.h>

/*
 * The IP2/IP3 lines are tied to a PCI/WMAC/USB device. Drivers for
 * these devices typically allocate coherent DMA memory, however the
 * DMA controller may still have some unsynchronized data in the FIFO.
 * Issue a flush in the handlers to ensure that the driver sees
 * the update.
 *
 * This array map the interrupt lines to the DDR write buffer channels.
 */

static unsigned irq_wb_chan[8] = {
	-1, -1, -1, -1, -1, -1, -1, -1,
};

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending;
	int irq;

	pending = read_c0_status() & read_c0_cause() & ST0_IM;

	if (!pending) {
		spurious_interrupt();
		return;
	}

	pending >>= CAUSEB_IP;
	while (pending) {
		irq = fls(pending) - 1;
		if (irq < ARRAY_SIZE(irq_wb_chan) && irq_wb_chan[irq] != -1)
			ath79_ddr_wb_flush(irq_wb_chan[irq]);
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
		pending &= ~BIT(irq);
	}
}

static int __init ar79_cpu_intc_of_init(
	struct device_node *node, struct device_node *parent)
{
	int err, i, count;

	/* Fill the irq_wb_chan table */
	count = of_count_phandle_with_args(
		node, "qca,ddr-wb-channels", "#qca,ddr-wb-channel-cells");

	for (i = 0; i < count; i++) {
		struct of_phandle_args args;
		u32 irq = i;

		of_property_read_u32_index(
			node, "qca,ddr-wb-channel-interrupts", i, &irq);
		if (irq >= ARRAY_SIZE(irq_wb_chan))
			continue;

		err = of_parse_phandle_with_args(
			node, "qca,ddr-wb-channels",
			"#qca,ddr-wb-channel-cells",
			i, &args);
		if (err)
			return err;

		irq_wb_chan[irq] = args.args[0];
	}

	return mips_cpu_irq_of_init(node, parent);
}
IRQCHIP_DECLARE(ar79_cpu_intc, "qca,ar7100-cpu-intc",
		ar79_cpu_intc_of_init);

void __init ath79_cpu_irq_init(unsigned irq_wb_chan2, unsigned irq_wb_chan3)
{
	irq_wb_chan[2] = irq_wb_chan2;
	irq_wb_chan[3] = irq_wb_chan3;
	mips_cpu_irq_init();
}
