// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/hwtest.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/processor.h>
#include <asm/virt.h>

#define GFPIC_REG_IRQ_PENDING           0x04
#define GFPIC_REG_IRQ_DISABLE_ALL       0x08
#define GFPIC_REG_IRQ_DISABLE           0x0c
#define GFPIC_REG_IRQ_ENABLE            0x10

static struct resource picres[6];
static const char *picname[6] = {
	"goldfish_pic.0",
	"goldfish_pic.1",
	"goldfish_pic.2",
	"goldfish_pic.3",
	"goldfish_pic.4",
	"goldfish_pic.5"
};

/*
 * 6 goldfish-pic for CPU IRQ #1 to IRQ #6
 * CPU IRQ #1 -> PIC #1
 *               IRQ #1 to IRQ #31 -> unused
 *               IRQ #32 -> goldfish-tty
 * CPU IRQ #2 -> PIC #2
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 1 to 32
 * CPU IRQ #3 -> PIC #3
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 33 to 64
 * CPU IRQ #4 -> PIC #4
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 65 to 96
 * CPU IRQ #5 -> PIC #5
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 97 to 128
 * CPU IRQ #6 -> PIC #6
 *               IRQ #1 -> goldfish-timer
 *               IRQ #2 -> goldfish-rtc
 *               IRQ #3 to IRQ #32 -> unused
 * CPU IRQ #7 -> NMI
 */

static u32 gfpic_read(int pic, int reg)
{
	void __iomem *base = (void __iomem *)(virt_bi_data.pic.mmio +
					      pic * 0x1000);

	return ioread32be(base + reg);
}

static void gfpic_write(u32 value, int pic, int reg)
{
	void __iomem *base = (void __iomem *)(virt_bi_data.pic.mmio +
					      pic * 0x1000);

	iowrite32be(value, base + reg);
}

#define GF_PIC(irq) ((irq - IRQ_USER) / 32)
#define GF_IRQ(irq) ((irq - IRQ_USER) % 32)

static void virt_irq_enable(struct irq_data *data)
{
	gfpic_write(BIT(GF_IRQ(data->irq)), GF_PIC(data->irq),
		    GFPIC_REG_IRQ_ENABLE);
}

static void virt_irq_disable(struct irq_data *data)
{
	gfpic_write(BIT(GF_IRQ(data->irq)), GF_PIC(data->irq),
		    GFPIC_REG_IRQ_DISABLE);
}

static unsigned int virt_irq_startup(struct irq_data *data)
{
	virt_irq_enable(data);
	return 0;
}

static irqreturn_t virt_nmi_handler(int irq, void *dev_id)
{
	static int in_nmi;

	if (READ_ONCE(in_nmi))
		return IRQ_HANDLED;
	WRITE_ONCE(in_nmi, 1);

	pr_warn("Non-Maskable Interrupt\n");
	show_registers(get_irq_regs());

	WRITE_ONCE(in_nmi, 0);
	return IRQ_HANDLED;
}

static struct irq_chip virt_irq_chip = {
	.name		= "virt",
	.irq_enable	= virt_irq_enable,
	.irq_disable	= virt_irq_disable,
	.irq_startup	= virt_irq_startup,
	.irq_shutdown	= virt_irq_disable,
};

static void goldfish_pic_irq(struct irq_desc *desc)
{
	u32 irq_pending;
	unsigned int irq_num;
	unsigned int pic = desc->irq_data.irq - 1;

	irq_pending = gfpic_read(pic, GFPIC_REG_IRQ_PENDING);
	irq_num = IRQ_USER + pic * 32;

	do {
		if (irq_pending & 1)
			generic_handle_irq(irq_num);
		++irq_num;
		irq_pending >>= 1;
	} while (irq_pending);
}

void __init virt_init_IRQ(void)
{
	unsigned int i;

	m68k_setup_irq_controller(&virt_irq_chip, handle_simple_irq, IRQ_USER,
				  NUM_VIRT_SOURCES - IRQ_USER);

	for (i = 0; i < 6; i++) {

		picres[i] = (struct resource)
		    DEFINE_RES_MEM_NAMED(virt_bi_data.pic.mmio + i * 0x1000,
					 0x1000, picname[i]);
		if (request_resource(&iomem_resource, &picres[i])) {
			pr_err("Cannot allocate %s resource\n", picname[i]);
			return;
		}

		irq_set_chained_handler(virt_bi_data.pic.irq + i,
					goldfish_pic_irq);
	}

	if (request_irq(IRQ_AUTO_7, virt_nmi_handler, 0, "NMI",
			virt_nmi_handler))
		pr_err("Couldn't register NMI\n");
}
