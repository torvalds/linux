// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/arm/mach-sa1100/neponset.c
 */
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/gpio-reg.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/smc91x.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/hardware/sa1111.h>
#include <linux/sizes.h>

#include <mach/hardware.h>
#include <mach/assabet.h>
#include <mach/neponset.h>
#include <mach/irqs.h>

#define NEP_IRQ_SMC91X	0
#define NEP_IRQ_USAR	1
#define NEP_IRQ_SA1111	2
#define NEP_IRQ_NR	3

#define WHOAMI		0x00
#define LEDS		0x10
#define SWPK		0x20
#define IRR		0x24
#define KP_Y_IN		0x80
#define KP_X_OUT	0x90
#define NCR_0		0xa0
#define MDM_CTL_0	0xb0
#define MDM_CTL_1	0xb4
#define AUD_CTL		0xc0

#define IRR_ETHERNET	(1 << 0)
#define IRR_USAR	(1 << 1)
#define IRR_SA1111	(1 << 2)

#define NCR_NGPIO	7
#define MDM_CTL0_NGPIO	4
#define MDM_CTL1_NGPIO	6
#define AUD_NGPIO	2

extern void sa1110_mb_disable(void);

#define to_neponset_gpio_chip(x) container_of(x, struct neponset_gpio_chip, gc)

static const char *neponset_ncr_names[] = {
	"gp01_off", "tp_power", "ms_power", "enet_osc",
	"spi_kb_wk_up", "a0vpp", "a1vpp"
};

static const char *neponset_mdmctl0_names[] = {
	"rts3", "dtr3", "rts1", "dtr1",
};

static const char *neponset_mdmctl1_names[] = {
	"cts3", "dsr3", "dcd3", "cts1", "dsr1", "dcd1"
};

static const char *neponset_aud_names[] = {
	"sel_1341", "mute_1341",
};

struct neponset_drvdata {
	void __iomem *base;
	struct platform_device *sa1111;
	struct platform_device *smc91x;
	unsigned irq_base;
	struct gpio_chip *gpio[4];
};

static struct gpiod_lookup_table neponset_uart1_gpio_table = {
	.dev_id = "sa11x0-uart.1",
	.table = {
		GPIO_LOOKUP("neponset-mdm-ctl0", 2, "rts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl0", 3, "dtr", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl1", 3, "cts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl1", 4, "dsr", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl1", 5, "dcd", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table neponset_uart3_gpio_table = {
	.dev_id = "sa11x0-uart.3",
	.table = {
		GPIO_LOOKUP("neponset-mdm-ctl0", 0, "rts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl0", 1, "dtr", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl1", 0, "cts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl1", 1, "dsr", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("neponset-mdm-ctl1", 2, "dcd", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table neponset_pcmcia_table = {
	.dev_id = "1800",
	.table = {
		GPIO_LOOKUP("sa1111", 1, "a0vcc", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("sa1111", 0, "a1vcc", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("neponset-ncr", 5, "a0vpp", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("neponset-ncr", 6, "a1vpp", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("sa1111", 2, "b0vcc", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("sa1111", 3, "b1vcc", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct neponset_drvdata *nep;

void neponset_ncr_frob(unsigned int mask, unsigned int val)
{
	struct neponset_drvdata *n = nep;
	unsigned long m = mask, v = val;

	if (nep)
		n->gpio[0]->set_multiple(n->gpio[0], &m, &v);
	else
		WARN(1, "nep unset\n");
}
EXPORT_SYMBOL(neponset_ncr_frob);

/*
 * Install handler for Neponset IRQ.  Note that we have to loop here
 * since the ETHERNET and USAR IRQs are level based, and we need to
 * ensure that the IRQ signal is deasserted before returning.  This
 * is rather unfortunate.
 */
static void neponset_irq_handler(struct irq_desc *desc)
{
	struct neponset_drvdata *d = irq_desc_get_handler_data(desc);
	unsigned int irr;

	while (1) {
		/*
		 * Acknowledge the parent IRQ.
		 */
		desc->irq_data.chip->irq_ack(&desc->irq_data);

		/*
		 * Read the interrupt reason register.  Let's have all
		 * active IRQ bits high.  Note: there is a typo in the
		 * Neponset user's guide for the SA1111 IRR level.
		 */
		irr = readb_relaxed(d->base + IRR);
		irr ^= IRR_ETHERNET | IRR_USAR;

		if ((irr & (IRR_ETHERNET | IRR_USAR | IRR_SA1111)) == 0)
			break;

		/*
		 * Since there is no individual mask, we have to
		 * mask the parent IRQ.  This is safe, since we'll
		 * recheck the register for any pending IRQs.
		 */
		if (irr & (IRR_ETHERNET | IRR_USAR)) {
			desc->irq_data.chip->irq_mask(&desc->irq_data);

			/*
			 * Ack the interrupt now to prevent re-entering
			 * this neponset handler.  Again, this is safe
			 * since we'll check the IRR register prior to
			 * leaving.
			 */
			desc->irq_data.chip->irq_ack(&desc->irq_data);

			if (irr & IRR_ETHERNET)
				generic_handle_irq(d->irq_base + NEP_IRQ_SMC91X);

			if (irr & IRR_USAR)
				generic_handle_irq(d->irq_base + NEP_IRQ_USAR);

			desc->irq_data.chip->irq_unmask(&desc->irq_data);
		}

		if (irr & IRR_SA1111)
			generic_handle_irq(d->irq_base + NEP_IRQ_SA1111);
	}
}

/* Yes, we really do not have any kind of masking or unmasking */
static void nochip_noop(struct irq_data *irq)
{
}

static struct irq_chip nochip = {
	.name = "neponset",
	.irq_ack = nochip_noop,
	.irq_mask = nochip_noop,
	.irq_unmask = nochip_noop,
};

static int neponset_init_gpio(struct gpio_chip **gcp,
	struct device *dev, const char *label, void __iomem *reg,
	unsigned num, bool in, const char *const * names)
{
	struct gpio_chip *gc;

	gc = gpio_reg_init(dev, reg, -1, num, label, in ? 0xffffffff : 0,
			   readl_relaxed(reg), names, NULL, NULL);
	if (IS_ERR(gc))
		return PTR_ERR(gc);

	*gcp = gc;

	return 0;
}

static struct sa1111_platform_data sa1111_info = {
	.disable_devs	= SA1111_DEVID_PS2_MSE,
};

static int neponset_probe(struct platform_device *dev)
{
	struct neponset_drvdata *d;
	struct resource *nep_res, *sa1111_res, *smc91x_res;
	struct resource sa1111_resources[] = {
		DEFINE_RES_MEM(0x40000000, SZ_8K),
		{ .flags = IORESOURCE_IRQ },
	};
	struct platform_device_info sa1111_devinfo = {
		.parent = &dev->dev,
		.name = "sa1111",
		.id = 0,
		.res = sa1111_resources,
		.num_res = ARRAY_SIZE(sa1111_resources),
		.data = &sa1111_info,
		.size_data = sizeof(sa1111_info),
		.dma_mask = 0xffffffffUL,
	};
	struct resource smc91x_resources[] = {
		DEFINE_RES_MEM_NAMED(SA1100_CS3_PHYS,
			0x02000000, "smc91x-regs"),
		DEFINE_RES_MEM_NAMED(SA1100_CS3_PHYS + 0x02000000,
			0x02000000, "smc91x-attrib"),
		{ .flags = IORESOURCE_IRQ },
	};
	struct smc91x_platdata smc91x_platdata = {
		.flags = SMC91X_USE_8BIT | SMC91X_IO_SHIFT_2 | SMC91X_NOWAIT,
	};
	struct platform_device_info smc91x_devinfo = {
		.parent = &dev->dev,
		.name = "smc91x",
		.id = 0,
		.res = smc91x_resources,
		.num_res = ARRAY_SIZE(smc91x_resources),
		.data = &smc91x_platdata,
		.size_data = sizeof(smc91x_platdata),
	};
	int ret, irq;

	if (nep)
		return -EBUSY;

	irq = ret = platform_get_irq(dev, 0);
	if (ret < 0)
		goto err_alloc;

	nep_res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	smc91x_res = platform_get_resource(dev, IORESOURCE_MEM, 1);
	sa1111_res = platform_get_resource(dev, IORESOURCE_MEM, 2);
	if (!nep_res || !smc91x_res || !sa1111_res) {
		ret = -ENXIO;
		goto err_alloc;
	}

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	d->base = ioremap(nep_res->start, SZ_4K);
	if (!d->base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	if (readb_relaxed(d->base + WHOAMI) != 0x11) {
		dev_warn(&dev->dev, "Neponset board detected, but wrong ID: %02x\n",
			 readb_relaxed(d->base + WHOAMI));
		ret = -ENODEV;
		goto err_id;
	}

	ret = irq_alloc_descs(-1, IRQ_BOARD_START, NEP_IRQ_NR, -1);
	if (ret <= 0) {
		dev_err(&dev->dev, "unable to allocate %u irqs: %d\n",
			NEP_IRQ_NR, ret);
		if (ret == 0)
			ret = -ENOMEM;
		goto err_irq_alloc;
	}

	d->irq_base = ret;

	irq_set_chip_and_handler(d->irq_base + NEP_IRQ_SMC91X, &nochip,
		handle_simple_irq);
	irq_clear_status_flags(d->irq_base + NEP_IRQ_SMC91X, IRQ_NOREQUEST | IRQ_NOPROBE);
	irq_set_chip_and_handler(d->irq_base + NEP_IRQ_USAR, &nochip,
		handle_simple_irq);
	irq_clear_status_flags(d->irq_base + NEP_IRQ_USAR, IRQ_NOREQUEST | IRQ_NOPROBE);
	irq_set_chip(d->irq_base + NEP_IRQ_SA1111, &nochip);

	irq_set_irq_type(irq, IRQ_TYPE_EDGE_RISING);
	irq_set_chained_handler_and_data(irq, neponset_irq_handler, d);

	/* Disable GPIO 0/1 drivers so the buttons work on the Assabet */
	writeb_relaxed(NCR_GP01_OFF, d->base + NCR_0);

	neponset_init_gpio(&d->gpio[0], &dev->dev, "neponset-ncr",
			   d->base + NCR_0, NCR_NGPIO, false,
			   neponset_ncr_names);
	neponset_init_gpio(&d->gpio[1], &dev->dev, "neponset-mdm-ctl0",
			   d->base + MDM_CTL_0, MDM_CTL0_NGPIO, false,
			   neponset_mdmctl0_names);
	neponset_init_gpio(&d->gpio[2], &dev->dev, "neponset-mdm-ctl1",
			   d->base + MDM_CTL_1, MDM_CTL1_NGPIO, true,
			   neponset_mdmctl1_names);
	neponset_init_gpio(&d->gpio[3], &dev->dev, "neponset-aud-ctl",
			   d->base + AUD_CTL, AUD_NGPIO, false,
			   neponset_aud_names);

	gpiod_add_lookup_table(&neponset_uart1_gpio_table);
	gpiod_add_lookup_table(&neponset_uart3_gpio_table);
	gpiod_add_lookup_table(&neponset_pcmcia_table);

	/*
	 * We would set IRQ_GPIO25 to be a wake-up IRQ, but unfortunately
	 * something on the Neponset activates this IRQ on sleep (eth?)
	 */
#if 0
	enable_irq_wake(irq);
#endif

	dev_info(&dev->dev, "Neponset daughter board, providing IRQ%u-%u\n",
		 d->irq_base, d->irq_base + NEP_IRQ_NR - 1);
	nep = d;

	/* Ensure that the memory bus request/grant signals are setup */
	sa1110_mb_disable();

	sa1111_resources[0].parent = sa1111_res;
	sa1111_resources[1].start = d->irq_base + NEP_IRQ_SA1111;
	sa1111_resources[1].end = d->irq_base + NEP_IRQ_SA1111;
	d->sa1111 = platform_device_register_full(&sa1111_devinfo);

	smc91x_resources[0].parent = smc91x_res;
	smc91x_resources[1].parent = smc91x_res;
	smc91x_resources[2].start = d->irq_base + NEP_IRQ_SMC91X;
	smc91x_resources[2].end = d->irq_base + NEP_IRQ_SMC91X;
	d->smc91x = platform_device_register_full(&smc91x_devinfo);

	platform_set_drvdata(dev, d);

	return 0;

 err_irq_alloc:
 err_id:
	iounmap(d->base);
 err_ioremap:
	kfree(d);
 err_alloc:
	return ret;
}

static int neponset_remove(struct platform_device *dev)
{
	struct neponset_drvdata *d = platform_get_drvdata(dev);
	int irq = platform_get_irq(dev, 0);

	if (!IS_ERR(d->sa1111))
		platform_device_unregister(d->sa1111);
	if (!IS_ERR(d->smc91x))
		platform_device_unregister(d->smc91x);

	gpiod_remove_lookup_table(&neponset_pcmcia_table);
	gpiod_remove_lookup_table(&neponset_uart3_gpio_table);
	gpiod_remove_lookup_table(&neponset_uart1_gpio_table);

	irq_set_chained_handler(irq, NULL);
	irq_free_descs(d->irq_base, NEP_IRQ_NR);
	nep = NULL;
	iounmap(d->base);
	kfree(d);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int neponset_resume(struct device *dev)
{
	struct neponset_drvdata *d = dev_get_drvdata(dev);
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(d->gpio); i++) {
		ret = gpio_reg_resume(d->gpio[i]);
		if (ret)
			break;
	}

	return ret;
}

static const struct dev_pm_ops neponset_pm_ops = {
	.resume_noirq = neponset_resume,
	.restore_noirq = neponset_resume,
};
#define PM_OPS &neponset_pm_ops
#else
#define PM_OPS NULL
#endif

static struct platform_driver neponset_device_driver = {
	.probe		= neponset_probe,
	.remove		= neponset_remove,
	.driver		= {
		.name	= "neponset",
		.pm	= PM_OPS,
	},
};

static int __init neponset_init(void)
{
	return platform_driver_register(&neponset_device_driver);
}

subsys_initcall(neponset_init);
