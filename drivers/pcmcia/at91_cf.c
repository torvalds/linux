/*
 * at91_cf.c -- AT91 CompactFlash controller driver
 *
 * Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <pcmcia/ss.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/sizes.h>

#include <mach/board.h>
#include <mach/at91rm9200_mc.h>
#include <mach/at91_ramc.h>


/*
 * A0..A10 work in each range; A23 indicates I/O space;  A25 is CFRNW;
 * some other bit in {A24,A22..A11} is nREG to flag memory access
 * (vs attributes).  So more than 2KB/region would just be waste.
 * Note: These are offsets from the physical base address.
 */
#define	CF_ATTR_PHYS	(0)
#define	CF_IO_PHYS	(1 << 23)
#define	CF_MEM_PHYS	(0x017ff800)

/*--------------------------------------------------------------------------*/

static const char driver_name[] = "at91_cf";

struct at91_cf_socket {
	struct pcmcia_socket	socket;

	unsigned		present:1;

	struct platform_device	*pdev;
	struct at91_cf_data	*board;

	unsigned long		phys_baseaddr;
};

static inline int at91_cf_present(struct at91_cf_socket *cf)
{
	return !gpio_get_value(cf->board->det_pin);
}

/*--------------------------------------------------------------------------*/

static int at91_cf_ss_init(struct pcmcia_socket *s)
{
	return 0;
}

static irqreturn_t at91_cf_irq(int irq, void *_cf)
{
	struct at91_cf_socket *cf = _cf;

	if (irq == gpio_to_irq(cf->board->det_pin)) {
		unsigned present = at91_cf_present(cf);

		/* kick pccard as needed */
		if (present != cf->present) {
			cf->present = present;
			pr_debug("%s: card %s\n", driver_name,
					present ? "present" : "gone");
			pcmcia_parse_events(&cf->socket, SS_DETECT);
		}
	}

	return IRQ_HANDLED;
}

static int at91_cf_get_status(struct pcmcia_socket *s, u_int *sp)
{
	struct at91_cf_socket	*cf;

	if (!sp)
		return -EINVAL;

	cf = container_of(s, struct at91_cf_socket, socket);

	/* NOTE: CF is always 3VCARD */
	if (at91_cf_present(cf)) {
		int rdy	= gpio_is_valid(cf->board->irq_pin);	/* RDY/nIRQ */
		int vcc	= gpio_is_valid(cf->board->vcc_pin);

		*sp = SS_DETECT | SS_3VCARD;
		if (!rdy || gpio_get_value(cf->board->irq_pin))
			*sp |= SS_READY;
		if (!vcc || gpio_get_value(cf->board->vcc_pin))
			*sp |= SS_POWERON;
	} else
		*sp = 0;

	return 0;
}

static int
at91_cf_set_socket(struct pcmcia_socket *sock, struct socket_state_t *s)
{
	struct at91_cf_socket	*cf;

	cf = container_of(sock, struct at91_cf_socket, socket);

	/* switch Vcc if needed and possible */
	if (gpio_is_valid(cf->board->vcc_pin)) {
		switch (s->Vcc) {
			case 0:
				gpio_set_value(cf->board->vcc_pin, 0);
				break;
			case 33:
				gpio_set_value(cf->board->vcc_pin, 1);
				break;
			default:
				return -EINVAL;
		}
	}

	/* toggle reset if needed */
	gpio_set_value(cf->board->rst_pin, s->flags & SS_RESET);

	pr_debug("%s: Vcc %d, io_irq %d, flags %04x csc %04x\n",
		driver_name, s->Vcc, s->io_irq, s->flags, s->csc_mask);

	return 0;
}

static int at91_cf_ss_suspend(struct pcmcia_socket *s)
{
	return at91_cf_set_socket(s, &dead_socket);
}

/* we already mapped the I/O region */
static int at91_cf_set_io_map(struct pcmcia_socket *s, struct pccard_io_map *io)
{
	struct at91_cf_socket	*cf;
	u32			csr;

	cf = container_of(s, struct at91_cf_socket, socket);
	io->flags &= (MAP_ACTIVE | MAP_16BIT | MAP_AUTOSZ);

	/*
	 * Use 16 bit accesses unless/until we need 8-bit i/o space.
	 */
	csr = at91_ramc_read(0, AT91_SMC_CSR(cf->board->chipselect)) & ~AT91_SMC_DBW;

	/*
	 * NOTE: this CF controller ignores IOIS16, so we can't really do
	 * MAP_AUTOSZ.  The 16bit mode allows single byte access on either
	 * D0-D7 (even addr) or D8-D15 (odd), so it's close enough for many
	 * purposes (and handles ide-cs).
	 *
	 * The 8bit mode is needed for odd byte access on D0-D7.  It seems
	 * some cards only like that way to get at the odd byte, despite
	 * CF 3.0 spec table 35 also giving the D8-D15 option.
	 */
	if (!(io->flags & (MAP_16BIT | MAP_AUTOSZ))) {
		csr |= AT91_SMC_DBW_8;
		pr_debug("%s: 8bit i/o bus\n", driver_name);
	} else {
		csr |= AT91_SMC_DBW_16;
		pr_debug("%s: 16bit i/o bus\n", driver_name);
	}
	at91_ramc_write(0, AT91_SMC_CSR(cf->board->chipselect), csr);

	io->start = cf->socket.io_offset;
	io->stop = io->start + SZ_2K - 1;

	return 0;
}

/* pcmcia layer maps/unmaps mem regions */
static int
at91_cf_set_mem_map(struct pcmcia_socket *s, struct pccard_mem_map *map)
{
	struct at91_cf_socket	*cf;

	if (map->card_start)
		return -EINVAL;

	cf = container_of(s, struct at91_cf_socket, socket);

	map->flags &= (MAP_ACTIVE | MAP_ATTRIB | MAP_16BIT);
	if (map->flags & MAP_ATTRIB)
		map->static_start = cf->phys_baseaddr + CF_ATTR_PHYS;
	else
		map->static_start = cf->phys_baseaddr + CF_MEM_PHYS;

	return 0;
}

static struct pccard_operations at91_cf_ops = {
	.init			= at91_cf_ss_init,
	.suspend		= at91_cf_ss_suspend,
	.get_status		= at91_cf_get_status,
	.set_socket		= at91_cf_set_socket,
	.set_io_map		= at91_cf_set_io_map,
	.set_mem_map		= at91_cf_set_mem_map,
};

/*--------------------------------------------------------------------------*/

static int __init at91_cf_probe(struct platform_device *pdev)
{
	struct at91_cf_socket	*cf;
	struct at91_cf_data	*board = pdev->dev.platform_data;
	struct resource		*io;
	int			status;

	if (!board || !gpio_is_valid(board->det_pin) || !gpio_is_valid(board->rst_pin))
		return -ENODEV;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -ENODEV;

	cf = kzalloc(sizeof *cf, GFP_KERNEL);
	if (!cf)
		return -ENOMEM;

	cf->board = board;
	cf->pdev = pdev;
	cf->phys_baseaddr = io->start;
	platform_set_drvdata(pdev, cf);

	/* must be a GPIO; ergo must trigger on both edges */
	status = gpio_request(board->det_pin, "cf_det");
	if (status < 0)
		goto fail0;
	status = request_irq(gpio_to_irq(board->det_pin), at91_cf_irq, 0, driver_name, cf);
	if (status < 0)
		goto fail00;
	device_init_wakeup(&pdev->dev, 1);

	status = gpio_request(board->rst_pin, "cf_rst");
	if (status < 0)
		goto fail0a;

	if (gpio_is_valid(board->vcc_pin)) {
		status = gpio_request(board->vcc_pin, "cf_vcc");
		if (status < 0)
			goto fail0b;
	}

	/*
	 * The card driver will request this irq later as needed.
	 * but it causes lots of "irqNN: nobody cared" messages
	 * unless we report that we handle everything (sigh).
	 * (Note:  DK board doesn't wire the IRQ pin...)
	 */
	if (gpio_is_valid(board->irq_pin)) {
		status = gpio_request(board->irq_pin, "cf_irq");
		if (status < 0)
			goto fail0c;
		status = request_irq(gpio_to_irq(board->irq_pin), at91_cf_irq,
				IRQF_SHARED, driver_name, cf);
		if (status < 0)
			goto fail0d;
		cf->socket.pci_irq = gpio_to_irq(board->irq_pin);
	} else
		cf->socket.pci_irq = nr_irqs + 1;

	/* pcmcia layer only remaps "real" memory not iospace */
	cf->socket.io_offset = (unsigned long)
			ioremap(cf->phys_baseaddr + CF_IO_PHYS, SZ_2K);
	if (!cf->socket.io_offset) {
		status = -ENXIO;
		goto fail1;
	}

	/* reserve chip-select regions */
	if (!request_mem_region(io->start, resource_size(io), driver_name)) {
		status = -ENXIO;
		goto fail1;
	}

	pr_info("%s: irqs det #%d, io #%d\n", driver_name,
		gpio_to_irq(board->det_pin), gpio_to_irq(board->irq_pin));

	cf->socket.owner = THIS_MODULE;
	cf->socket.dev.parent = &pdev->dev;
	cf->socket.ops = &at91_cf_ops;
	cf->socket.resource_ops = &pccard_static_ops;
	cf->socket.features = SS_CAP_PCCARD | SS_CAP_STATIC_MAP
				| SS_CAP_MEM_ALIGN;
	cf->socket.map_size = SZ_2K;
	cf->socket.io[0].res = io;

	status = pcmcia_register_socket(&cf->socket);
	if (status < 0)
		goto fail2;

	return 0;

fail2:
	release_mem_region(io->start, resource_size(io));
fail1:
	if (cf->socket.io_offset)
		iounmap((void __iomem *) cf->socket.io_offset);
	if (gpio_is_valid(board->irq_pin)) {
		free_irq(gpio_to_irq(board->irq_pin), cf);
fail0d:
		gpio_free(board->irq_pin);
	}
fail0c:
	if (gpio_is_valid(board->vcc_pin))
		gpio_free(board->vcc_pin);
fail0b:
	gpio_free(board->rst_pin);
fail0a:
	device_init_wakeup(&pdev->dev, 0);
	free_irq(gpio_to_irq(board->det_pin), cf);
fail00:
	gpio_free(board->det_pin);
fail0:
	kfree(cf);
	return status;
}

static int __exit at91_cf_remove(struct platform_device *pdev)
{
	struct at91_cf_socket	*cf = platform_get_drvdata(pdev);
	struct at91_cf_data	*board = cf->board;
	struct resource		*io = cf->socket.io[0].res;

	pcmcia_unregister_socket(&cf->socket);
	release_mem_region(io->start, resource_size(io));
	iounmap((void __iomem *) cf->socket.io_offset);
	if (gpio_is_valid(board->irq_pin)) {
		free_irq(gpio_to_irq(board->irq_pin), cf);
		gpio_free(board->irq_pin);
	}
	if (gpio_is_valid(board->vcc_pin))
		gpio_free(board->vcc_pin);
	gpio_free(board->rst_pin);
	device_init_wakeup(&pdev->dev, 0);
	free_irq(gpio_to_irq(board->det_pin), cf);
	gpio_free(board->det_pin);
	kfree(cf);
	return 0;
}

#ifdef	CONFIG_PM

static int at91_cf_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct at91_cf_socket	*cf = platform_get_drvdata(pdev);
	struct at91_cf_data	*board = cf->board;

	if (device_may_wakeup(&pdev->dev)) {
		enable_irq_wake(gpio_to_irq(board->det_pin));
		if (gpio_is_valid(board->irq_pin))
			enable_irq_wake(gpio_to_irq(board->irq_pin));
	}
	return 0;
}

static int at91_cf_resume(struct platform_device *pdev)
{
	struct at91_cf_socket	*cf = platform_get_drvdata(pdev);
	struct at91_cf_data	*board = cf->board;

	if (device_may_wakeup(&pdev->dev)) {
		disable_irq_wake(gpio_to_irq(board->det_pin));
		if (gpio_is_valid(board->irq_pin))
			disable_irq_wake(gpio_to_irq(board->irq_pin));
	}

	return 0;
}

#else
#define	at91_cf_suspend		NULL
#define	at91_cf_resume		NULL
#endif

static struct platform_driver at91_cf_driver = {
	.driver = {
		.name		= (char *) driver_name,
		.owner		= THIS_MODULE,
	},
	.remove		= __exit_p(at91_cf_remove),
	.suspend	= at91_cf_suspend,
	.resume		= at91_cf_resume,
};

/*--------------------------------------------------------------------------*/

static int __init at91_cf_init(void)
{
	return platform_driver_probe(&at91_cf_driver, at91_cf_probe);
}
module_init(at91_cf_init);

static void __exit at91_cf_exit(void)
{
	platform_driver_unregister(&at91_cf_driver);
}
module_exit(at91_cf_exit);

MODULE_DESCRIPTION("AT91 Compact Flash Driver");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_cf");
