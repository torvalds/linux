/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Stefan Nilsson <stefan.xk.nilsson@stericsson.com> for ST-Ericsson.
 * Author: Martin Persson <martin.persson@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>

#define MODEM_INTCON_BASE_ADDR 0xBFFD3000
#define MODEM_INTCON_SIZE 0xFFF

#define DEST_IRQ41_OFFSET 0x2A4
#define DEST_IRQ43_OFFSET 0x2AC
#define DEST_IRQ45_OFFSET 0x2B4

#define PRIO_IRQ41_OFFSET 0x6A4
#define PRIO_IRQ43_OFFSET 0x6AC
#define PRIO_IRQ45_OFFSET 0x6B4

#define ALLOW_IRQ_OFFSET 0x104

#define MODEM_INTCON_CPU_NBR 0x1
#define MODEM_INTCON_PRIO_HIGH 0x0

#define MODEM_INTCON_ALLOW_IRQ41 0x0200
#define MODEM_INTCON_ALLOW_IRQ43 0x0800
#define MODEM_INTCON_ALLOW_IRQ45 0x2000

#define MODEM_IRQ_REG_OFFSET 0x4

struct modem_irq {
	void __iomem *modem_intcon_base;
};


static void setup_modem_intcon(void __iomem *modem_intcon_base)
{
	/* IC_DESTINATION_BASE_ARRAY - Which CPU to receive the IRQ */
	writel(MODEM_INTCON_CPU_NBR, modem_intcon_base + DEST_IRQ41_OFFSET);
	writel(MODEM_INTCON_CPU_NBR, modem_intcon_base + DEST_IRQ43_OFFSET);
	writel(MODEM_INTCON_CPU_NBR, modem_intcon_base + DEST_IRQ45_OFFSET);

	/* IC_PRIORITY_BASE_ARRAY - IRQ priority in modem IRQ controller */
	writel(MODEM_INTCON_PRIO_HIGH, modem_intcon_base + PRIO_IRQ41_OFFSET);
	writel(MODEM_INTCON_PRIO_HIGH, modem_intcon_base + PRIO_IRQ43_OFFSET);
	writel(MODEM_INTCON_PRIO_HIGH, modem_intcon_base + PRIO_IRQ45_OFFSET);

	/* IC_ALLOW_ARRAY - IRQ enable */
	writel(MODEM_INTCON_ALLOW_IRQ41 |
		   MODEM_INTCON_ALLOW_IRQ43 |
		   MODEM_INTCON_ALLOW_IRQ45,
		   modem_intcon_base + ALLOW_IRQ_OFFSET);
}

static irqreturn_t modem_cpu_irq_handler(int irq, void *data)
{
	int real_irq;
	int virt_irq;
	struct modem_irq *mi = (struct modem_irq *)data;

	/* Read modem side IRQ number from modem IRQ controller */
	real_irq = readl(mi->modem_intcon_base + MODEM_IRQ_REG_OFFSET) & 0xFF;
	virt_irq = IRQ_MODEM_EVENTS_BASE + real_irq;

	pr_debug("modem_irq: Worker read addr 0x%X and got value 0x%X "
		 "which will be 0x%X (%d) which translates to "
		 "virtual IRQ 0x%X (%d)!\n",
		   (u32)mi->modem_intcon_base + MODEM_IRQ_REG_OFFSET,
		   real_irq,
		   real_irq & 0xFF,
		   real_irq & 0xFF,
		   virt_irq,
		   virt_irq);

	if (virt_irq != 0)
		generic_handle_irq(virt_irq);

	pr_debug("modem_irq: Done handling virtual IRQ %d!\n", virt_irq);

	return IRQ_HANDLED;
}

static void create_virtual_irq(int irq, struct irq_chip *modem_irq_chip)
{
	set_irq_chip(irq, modem_irq_chip);
	set_irq_handler(irq, handle_simple_irq);
	set_irq_flags(irq, IRQF_VALID);

	pr_debug("modem_irq: Created virtual IRQ %d\n", irq);
}

static int modem_irq_init(void)
{
	int err;
	static struct irq_chip  modem_irq_chip;
	struct modem_irq *mi;

	pr_info("modem_irq: Set up IRQ handler for incoming modem IRQ %d\n",
		   IRQ_DB5500_MODEM);

	mi = kmalloc(sizeof(struct modem_irq), GFP_KERNEL);
	if (!mi) {
		pr_err("modem_irq: Could not allocate device\n");
		return -ENOMEM;
	}

	mi->modem_intcon_base =
		ioremap(MODEM_INTCON_BASE_ADDR, MODEM_INTCON_SIZE);
	pr_debug("modem_irq: ioremapped modem_intcon_base from "
		 "phy 0x%x to virt 0x%x\n", MODEM_INTCON_BASE_ADDR,
		 (u32)mi->modem_intcon_base);

	setup_modem_intcon(mi->modem_intcon_base);

	modem_irq_chip = dummy_irq_chip;
	modem_irq_chip.name = "modem_irq";

	/* Create the virtual IRQ:s needed */
	create_virtual_irq(MBOX_PAIR0_VIRT_IRQ, &modem_irq_chip);
	create_virtual_irq(MBOX_PAIR1_VIRT_IRQ, &modem_irq_chip);
	create_virtual_irq(MBOX_PAIR2_VIRT_IRQ, &modem_irq_chip);

	err = request_threaded_irq(IRQ_DB5500_MODEM, NULL,
				   modem_cpu_irq_handler, IRQF_ONESHOT,
				   "modem_irq", mi);
	if (err)
		pr_err("modem_irq: Could not register IRQ %d\n",
		       IRQ_DB5500_MODEM);

	return 0;
}

arch_initcall(modem_irq_init);
