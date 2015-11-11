/*
 * linux/arch/arm/mach-sa1100/neponset.c
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/sa11x0-serial.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/smc91x.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/hardware/sa1111.h>
#include <asm/sizes.h>

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

#define MDM_CTL0_RTS1	(1 << 0)
#define MDM_CTL0_DTR1	(1 << 1)
#define MDM_CTL0_RTS2	(1 << 2)
#define MDM_CTL0_DTR2	(1 << 3)

#define MDM_CTL1_CTS1	(1 << 0)
#define MDM_CTL1_DSR1	(1 << 1)
#define MDM_CTL1_DCD1	(1 << 2)
#define MDM_CTL1_CTS2	(1 << 3)
#define MDM_CTL1_DSR2	(1 << 4)
#define MDM_CTL1_DCD2	(1 << 5)

#define AUD_SEL_1341	(1 << 0)
#define AUD_MUTE_1341	(1 << 1)

extern void sa1110_mb_disable(void);

struct neponset_drvdata {
	void __iomem *base;
	struct platform_device *sa1111;
	struct platform_device *smc91x;
	unsigned irq_base;
#ifdef CONFIG_PM_SLEEP
	u32 ncr0;
	u32 mdm_ctl_0;
#endif
};

static void __iomem *nep_base;

void neponset_ncr_frob(unsigned int mask, unsigned int val)
{
	void __iomem *base = nep_base;

	if (base) {
		unsigned long flags;
		unsigned v;

		local_irq_save(flags);
		v = readb_relaxed(base + NCR_0);
		writeb_relaxed((v & ~mask) | val, base + NCR_0);
		local_irq_restore(flags);
	} else {
		WARN(1, "nep_base unset\n");
	}
}
EXPORT_SYMBOL(neponset_ncr_frob);

static void neponset_set_mctrl(struct uart_port *port, u_int mctrl)
{
	void __iomem *base = nep_base;
	u_int mdm_ctl0;

	if (!base)
		return;

	mdm_ctl0 = readb_relaxed(base + MDM_CTL_0);
	if (port->mapbase == _Ser1UTCR0) {
		if (mctrl & TIOCM_RTS)
			mdm_ctl0 &= ~MDM_CTL0_RTS2;
		else
			mdm_ctl0 |= MDM_CTL0_RTS2;

		if (mctrl & TIOCM_DTR)
			mdm_ctl0 &= ~MDM_CTL0_DTR2;
		else
			mdm_ctl0 |= MDM_CTL0_DTR2;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (mctrl & TIOCM_RTS)
			mdm_ctl0 &= ~MDM_CTL0_RTS1;
		else
			mdm_ctl0 |= MDM_CTL0_RTS1;

		if (mctrl & TIOCM_DTR)
			mdm_ctl0 &= ~MDM_CTL0_DTR1;
		else
			mdm_ctl0 |= MDM_CTL0_DTR1;
	}

	writeb_relaxed(mdm_ctl0, base + MDM_CTL_0);
}

static u_int neponset_get_mctrl(struct uart_port *port)
{
	void __iomem *base = nep_base;
	u_int ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;
	u_int mdm_ctl1;

	if (!base)
		return ret;

	mdm_ctl1 = readb_relaxed(base + MDM_CTL_1);
	if (port->mapbase == _Ser1UTCR0) {
		if (mdm_ctl1 & MDM_CTL1_DCD2)
			ret &= ~TIOCM_CD;
		if (mdm_ctl1 & MDM_CTL1_CTS2)
			ret &= ~TIOCM_CTS;
		if (mdm_ctl1 & MDM_CTL1_DSR2)
			ret &= ~TIOCM_DSR;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (mdm_ctl1 & MDM_CTL1_DCD1)
			ret &= ~TIOCM_CD;
		if (mdm_ctl1 & MDM_CTL1_CTS1)
			ret &= ~TIOCM_CTS;
		if (mdm_ctl1 & MDM_CTL1_DSR1)
			ret &= ~TIOCM_DSR;
	}

	return ret;
}

static struct sa1100_port_fns neponset_port_fns = {
	.set_mctrl	= neponset_set_mctrl,
	.get_mctrl	= neponset_get_mctrl,
};

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

	if (nep_base)
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

	/*
	 * We would set IRQ_GPIO25 to be a wake-up IRQ, but unfortunately
	 * something on the Neponset activates this IRQ on sleep (eth?)
	 */
#if 0
	enable_irq_wake(irq);
#endif

	dev_info(&dev->dev, "Neponset daughter board, providing IRQ%u-%u\n",
		 d->irq_base, d->irq_base + NEP_IRQ_NR - 1);
	nep_base = d->base;

	sa1100_register_uart_fns(&neponset_port_fns);

	/* Ensure that the memory bus request/grant signals are setup */
	sa1110_mb_disable();

	/* Disable GPIO 0/1 drivers so the buttons work on the Assabet */
	writeb_relaxed(NCR_GP01_OFF, d->base + NCR_0);

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
	irq_set_chained_handler(irq, NULL);
	irq_free_descs(d->irq_base, NEP_IRQ_NR);
	nep_base = NULL;
	iounmap(d->base);
	kfree(d);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int neponset_suspend(struct device *dev)
{
	struct neponset_drvdata *d = dev_get_drvdata(dev);

	d->ncr0 = readb_relaxed(d->base + NCR_0);
	d->mdm_ctl_0 = readb_relaxed(d->base + MDM_CTL_0);

	return 0;
}

static int neponset_resume(struct device *dev)
{
	struct neponset_drvdata *d = dev_get_drvdata(dev);

	writeb_relaxed(d->ncr0, d->base + NCR_0);
	writeb_relaxed(d->mdm_ctl_0, d->base + MDM_CTL_0);

	return 0;
}

static const struct dev_pm_ops neponset_pm_ops = {
	.suspend_noirq = neponset_suspend,
	.resume_noirq = neponset_resume,
	.freeze_noirq = neponset_suspend,
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
