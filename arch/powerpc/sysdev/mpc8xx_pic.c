#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <asm/prom.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/8xx_immap.h>

#include "mpc8xx_pic.h"


#define PIC_VEC_SPURRIOUS      15

extern int cpm_get_irq(struct pt_regs *regs);

static struct irq_host *mpc8xx_pic_host;
#define NR_MASK_WORDS   ((NR_IRQS + 31) / 32)
static unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
static sysconf8xx_t __iomem *siu_reg;

int cpm_get_irq(struct pt_regs *regs);

static void mpc8xx_unmask_irq(unsigned int virq)
{
	int	bit, word;
	unsigned int irq_nr = (unsigned int)irq_map[virq].hwirq;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] |= (1 << (31-bit));
	out_be32(&siu_reg->sc_simask, ppc_cached_irq_mask[word]);
}

static void mpc8xx_mask_irq(unsigned int virq)
{
	int	bit, word;
	unsigned int irq_nr = (unsigned int)irq_map[virq].hwirq;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31-bit));
	out_be32(&siu_reg->sc_simask, ppc_cached_irq_mask[word]);
}

static void mpc8xx_ack(unsigned int virq)
{
	int	bit;
	unsigned int irq_nr = (unsigned int)irq_map[virq].hwirq;

	bit = irq_nr & 0x1f;
	out_be32(&siu_reg->sc_sipend, 1 << (31-bit));
}

static void mpc8xx_end_irq(unsigned int virq)
{
	int bit, word;
	unsigned int irq_nr = (unsigned int)irq_map[virq].hwirq;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] |= (1 << (31-bit));
	out_be32(&siu_reg->sc_simask, ppc_cached_irq_mask[word]);
}

static int mpc8xx_set_irq_type(unsigned int virq, unsigned int flow_type)
{
	struct irq_desc *desc = irq_to_desc(virq);

	desc->status &= ~(IRQ_TYPE_SENSE_MASK | IRQ_LEVEL);
	desc->status |= flow_type & IRQ_TYPE_SENSE_MASK;
	if (flow_type & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		desc->status |= IRQ_LEVEL;

	if (flow_type & IRQ_TYPE_EDGE_FALLING) {
		irq_hw_number_t hw = (unsigned int)irq_map[virq].hwirq;
		unsigned int siel = in_be32(&siu_reg->sc_siel);

		/* only external IRQ senses are programmable */
		if ((hw & 1) == 0) {
			siel |= (0x80000000 >> hw);
			out_be32(&siu_reg->sc_siel, siel);
			desc->handle_irq = handle_edge_irq;
		}
	}
	return 0;
}

static struct irq_chip mpc8xx_pic = {
	.name = " MPC8XX SIU ",
	.unmask = mpc8xx_unmask_irq,
	.mask = mpc8xx_mask_irq,
	.ack = mpc8xx_ack,
	.eoi = mpc8xx_end_irq,
	.set_type = mpc8xx_set_irq_type,
};

unsigned int mpc8xx_get_irq(void)
{
	int irq;

	/* For MPC8xx, read the SIVEC register and shift the bits down
	 * to get the irq number.
	 */
	irq = in_be32(&siu_reg->sc_sivec) >> 26;

	if (irq == PIC_VEC_SPURRIOUS)
		irq = NO_IRQ;

        return irq_linear_revmap(mpc8xx_pic_host, irq);

}

static int mpc8xx_pic_host_map(struct irq_host *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	pr_debug("mpc8xx_pic_host_map(%d, 0x%lx)\n", virq, hw);

	/* Set default irq handle */
	set_irq_chip_and_handler(virq, &mpc8xx_pic, handle_level_irq);
	return 0;
}


static int mpc8xx_pic_host_xlate(struct irq_host *h, struct device_node *ct,
			    const u32 *intspec, unsigned int intsize,
			    irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{
	static unsigned char map_pic_senses[4] = {
		IRQ_TYPE_EDGE_RISING,
		IRQ_TYPE_LEVEL_LOW,
		IRQ_TYPE_LEVEL_HIGH,
		IRQ_TYPE_EDGE_FALLING,
	};

	*out_hwirq = intspec[0];
	if (intsize > 1 && intspec[1] < 4)
		*out_flags = map_pic_senses[intspec[1]];
	else
		*out_flags = IRQ_TYPE_NONE;

	return 0;
}


static struct irq_host_ops mpc8xx_pic_host_ops = {
	.map = mpc8xx_pic_host_map,
	.xlate = mpc8xx_pic_host_xlate,
};

int mpc8xx_pic_init(void)
{
	struct resource res;
	struct device_node *np;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "fsl,pq1-pic");
	if (np == NULL)
		np = of_find_node_by_type(NULL, "mpc8xx-pic");
	if (np == NULL) {
		printk(KERN_ERR "Could not find fsl,pq1-pic node\n");
		return -ENOMEM;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto out;

	siu_reg = ioremap(res.start, res.end - res.start + 1);
	if (siu_reg == NULL) {
		ret = -EINVAL;
		goto out;
	}

	mpc8xx_pic_host = irq_alloc_host(np, IRQ_HOST_MAP_LINEAR,
					 64, &mpc8xx_pic_host_ops, 64);
	if (mpc8xx_pic_host == NULL) {
		printk(KERN_ERR "MPC8xx PIC: failed to allocate irq host!\n");
		ret = -ENOMEM;
		goto out;
	}
	return 0;

out:
	of_node_put(np);
	return ret;
}
