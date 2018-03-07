/*
 * linux/drivers/pcmcia/sa1111_generic.c
 *
 * We implement the generic parts of a SA1111 PCMCIA driver.  This
 * basically means we handle everything except controlling the
 * power.  Power is machine specific...
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <pcmcia/ss.h>

#include <mach/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include "sa1111_generic.h"

/*
 * These are offsets from the above base.
 */
#define PCCR	0x0000
#define PCSSR	0x0004
#define PCSR	0x0008

#define PCSR_S0_READY	(1<<0)
#define PCSR_S1_READY	(1<<1)
#define PCSR_S0_DETECT	(1<<2)
#define PCSR_S1_DETECT	(1<<3)
#define PCSR_S0_VS1	(1<<4)
#define PCSR_S0_VS2	(1<<5)
#define PCSR_S1_VS1	(1<<6)
#define PCSR_S1_VS2	(1<<7)
#define PCSR_S0_WP	(1<<8)
#define PCSR_S1_WP	(1<<9)
#define PCSR_S0_BVD1	(1<<10)
#define PCSR_S0_BVD2	(1<<11)
#define PCSR_S1_BVD1	(1<<12)
#define PCSR_S1_BVD2	(1<<13)

#define PCCR_S0_RST	(1<<0)
#define PCCR_S1_RST	(1<<1)
#define PCCR_S0_FLT	(1<<2)
#define PCCR_S1_FLT	(1<<3)
#define PCCR_S0_PWAITEN	(1<<4)
#define PCCR_S1_PWAITEN	(1<<5)
#define PCCR_S0_PSE	(1<<6)
#define PCCR_S1_PSE	(1<<7)

#define PCSSR_S0_SLEEP	(1<<0)
#define PCSSR_S1_SLEEP	(1<<1)

#define IDX_IRQ_S0_READY_NINT	(0)
#define IDX_IRQ_S0_CD_VALID	(1)
#define IDX_IRQ_S0_BVD1_STSCHG	(2)
#define IDX_IRQ_S1_READY_NINT	(3)
#define IDX_IRQ_S1_CD_VALID	(4)
#define IDX_IRQ_S1_BVD1_STSCHG	(5)
#define NUM_IRQS		(6)

void sa1111_pcmcia_socket_state(struct soc_pcmcia_socket *skt, struct pcmcia_state *state)
{
	struct sa1111_pcmcia_socket *s = to_skt(skt);
	u32 status = readl_relaxed(s->dev->mapbase + PCSR);

	switch (skt->nr) {
	case 0:
		state->detect = status & PCSR_S0_DETECT ? 0 : 1;
		state->ready  = status & PCSR_S0_READY  ? 1 : 0;
		state->bvd1   = status & PCSR_S0_BVD1   ? 1 : 0;
		state->bvd2   = status & PCSR_S0_BVD2   ? 1 : 0;
		state->wrprot = status & PCSR_S0_WP     ? 1 : 0;
		state->vs_3v  = status & PCSR_S0_VS1    ? 0 : 1;
		state->vs_Xv  = status & PCSR_S0_VS2    ? 0 : 1;
		break;

	case 1:
		state->detect = status & PCSR_S1_DETECT ? 0 : 1;
		state->ready  = status & PCSR_S1_READY  ? 1 : 0;
		state->bvd1   = status & PCSR_S1_BVD1   ? 1 : 0;
		state->bvd2   = status & PCSR_S1_BVD2   ? 1 : 0;
		state->wrprot = status & PCSR_S1_WP     ? 1 : 0;
		state->vs_3v  = status & PCSR_S1_VS1    ? 0 : 1;
		state->vs_Xv  = status & PCSR_S1_VS2    ? 0 : 1;
		break;
	}
}

int sa1111_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	struct sa1111_pcmcia_socket *s = to_skt(skt);
	u32 pccr_skt_mask, pccr_set_mask, val;
	unsigned long flags;

	switch (skt->nr) {
	case 0:
		pccr_skt_mask = PCCR_S0_RST|PCCR_S0_FLT|PCCR_S0_PWAITEN|PCCR_S0_PSE;
		break;

	case 1:
		pccr_skt_mask = PCCR_S1_RST|PCCR_S1_FLT|PCCR_S1_PWAITEN|PCCR_S1_PSE;
		break;

	default:
		return -1;
	}

	pccr_set_mask = 0;

	if (state->Vcc != 0)
		pccr_set_mask |= PCCR_S0_PWAITEN|PCCR_S1_PWAITEN;
	if (state->Vcc == 50)
		pccr_set_mask |= PCCR_S0_PSE|PCCR_S1_PSE;
	if (state->flags & SS_RESET)
		pccr_set_mask |= PCCR_S0_RST|PCCR_S1_RST;
	if (state->flags & SS_OUTPUT_ENA)
		pccr_set_mask |= PCCR_S0_FLT|PCCR_S1_FLT;

	local_irq_save(flags);
	val = readl_relaxed(s->dev->mapbase + PCCR);
	val &= ~pccr_skt_mask;
	val |= pccr_set_mask & pccr_skt_mask;
	writel_relaxed(val, s->dev->mapbase + PCCR);
	local_irq_restore(flags);

	return 0;
}

int sa1111_pcmcia_add(struct sa1111_dev *dev, struct pcmcia_low_level *ops,
	int (*add)(struct soc_pcmcia_socket *))
{
	struct sa1111_pcmcia_socket *s;
	struct clk *clk;
	int i, ret = 0, irqs[NUM_IRQS];

	clk = devm_clk_get(&dev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	for (i = 0; i < NUM_IRQS; i++) {
		irqs[i] = sa1111_get_irq(dev, i);
		if (irqs[i] <= 0)
			return irqs[i] ? : -ENXIO;
	}

	ops->socket_state = sa1111_pcmcia_socket_state;

	for (i = 0; i < ops->nr; i++) {
		s = kzalloc(sizeof(*s), GFP_KERNEL);
		if (!s)
			return -ENOMEM;

		s->soc.nr = ops->first + i;
		s->soc.clk = clk;

		soc_pcmcia_init_one(&s->soc, ops, &dev->dev);
		s->dev = dev;
		if (s->soc.nr) {
			s->soc.socket.pci_irq = irqs[IDX_IRQ_S1_READY_NINT];
			s->soc.stat[SOC_STAT_CD].irq = irqs[IDX_IRQ_S1_CD_VALID];
			s->soc.stat[SOC_STAT_CD].name = "SA1111 CF card detect";
			s->soc.stat[SOC_STAT_BVD1].irq = irqs[IDX_IRQ_S1_BVD1_STSCHG];
			s->soc.stat[SOC_STAT_BVD1].name = "SA1111 CF BVD1";
		} else {
			s->soc.socket.pci_irq = irqs[IDX_IRQ_S0_READY_NINT];
			s->soc.stat[SOC_STAT_CD].irq = irqs[IDX_IRQ_S0_CD_VALID];
			s->soc.stat[SOC_STAT_CD].name = "SA1111 PCMCIA card detect";
			s->soc.stat[SOC_STAT_BVD1].irq = irqs[IDX_IRQ_S0_BVD1_STSCHG];
			s->soc.stat[SOC_STAT_BVD1].name = "SA1111 PCMCIA BVD1";
		}

		ret = add(&s->soc);
		if (ret == 0) {
			s->next = dev_get_drvdata(&dev->dev);
			dev_set_drvdata(&dev->dev, s);
		} else
			kfree(s);
	}

	return ret;
}

static int pcmcia_probe(struct sa1111_dev *dev)
{
	void __iomem *base;
	int ret;

	ret = sa1111_enable_device(dev);
	if (ret)
		return ret;

	dev_set_drvdata(&dev->dev, NULL);

	if (!request_mem_region(dev->res.start, 512, SA1111_DRIVER_NAME(dev))) {
		sa1111_disable_device(dev);
		return -EBUSY;
	}

	base = dev->mapbase;

	/*
	 * Initialise the suspend state.
	 */
	writel_relaxed(PCSSR_S0_SLEEP | PCSSR_S1_SLEEP, base + PCSSR);
	writel_relaxed(PCCR_S0_FLT | PCCR_S1_FLT, base + PCCR);

	ret = -ENODEV;
#ifdef CONFIG_SA1100_BADGE4
	if (machine_is_badge4())
		ret = pcmcia_badge4_init(dev);
#endif
#ifdef CONFIG_SA1100_JORNADA720
	if (machine_is_jornada720())
		ret = pcmcia_jornada720_init(dev);
#endif
#ifdef CONFIG_ARCH_LUBBOCK
	if (machine_is_lubbock())
		ret = pcmcia_lubbock_init(dev);
#endif
#ifdef CONFIG_ASSABET_NEPONSET
	if (machine_is_assabet())
		ret = pcmcia_neponset_init(dev);
#endif

	if (ret) {
		release_mem_region(dev->res.start, 512);
		sa1111_disable_device(dev);
	}

	return ret;
}

static int pcmcia_remove(struct sa1111_dev *dev)
{
	struct sa1111_pcmcia_socket *next, *s = dev_get_drvdata(&dev->dev);

	dev_set_drvdata(&dev->dev, NULL);

	for (; s; s = next) {
		next = s->next;
		soc_pcmcia_remove_one(&s->soc);
		kfree(s);
	}

	release_mem_region(dev->res.start, 512);
	sa1111_disable_device(dev);
	return 0;
}

static struct sa1111_driver pcmcia_driver = {
	.drv = {
		.name	= "sa1111-pcmcia",
	},
	.devid		= SA1111_DEVID_PCMCIA,
	.probe		= pcmcia_probe,
	.remove		= pcmcia_remove,
};

static int __init sa1111_drv_pcmcia_init(void)
{
	return sa1111_driver_register(&pcmcia_driver);
}

static void __exit sa1111_drv_pcmcia_exit(void)
{
	sa1111_driver_unregister(&pcmcia_driver);
}

fs_initcall(sa1111_drv_pcmcia_init);
module_exit(sa1111_drv_pcmcia_exit);

MODULE_DESCRIPTION("SA1111 PCMCIA card socket driver");
MODULE_LICENSE("GPL");
