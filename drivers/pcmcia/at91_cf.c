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
#include <linux/platform_data/atmel.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <pcmcia/ss.h>

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
			dev_dbg(&cf->pdev->dev, "card %s\n",
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

	dev_dbg(&cf->pdev->dev, "Vcc %d, io_irq %d, flags %04x csc %04x\n",
				s->Vcc, s->io_irq, s->flags, s->csc_mask);

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
		dev_dbg(&cf->pdev->dev, "8bit i/o bus\n");
	} else {
		csr |= AT91_SMC_DBW_16;
		dev_dbg(&cf->pdev->dev, "16bit i/o bus\n");
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

#if defined(CONFIG_OF)
static const struct of_device_id at91_cf_dt_ids[] = {
	{ .compatible = "atmel,at91rm9200-cf" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, at91_cf_dt_ids);

static int at91_cf_dt_init(struct platform_device *pdev)
{
	struct at91_cf_data *board;

	board = devm_kzalloc(&pdev->dev, sizeof(*board), GFP_KERNEL);
	if (!board)
		return -ENOMEM;

	board->irq_pin = of_get_gpio(pdev->dev.of_node, 0);
	board->det_pin = of_get_gpio(pdev->dev.of_node, 1);
	board->vcc_pin = of_get_gpio(pdev->dev.of_node, 2);
	board->rst_pin = of_get_gpio(pdev->dev.of_node, 3);

	pdev->dev.platform_data = board;

	return 0;
}
#else
static int at91_cf_dt_init(struct platform_device *pdev)
{
	return -ENODEV;
}
#endif

static int at91_cf_probe(struct platform_device *pdev)
{
	struct at91_cf_socket	*cf;
	struct at91_cf_data	*board = pdev->dev.platform_data;
	struct resource		*io;
	int			status;

	if (!board) {
		status = at91_cf_dt_init(pdev);
		if (status)
			return status;

		board = pdev->dev.platform_data;
	}

	if (!gpio_is_valid(board->det_pin) || !gpio_is_valid(board->rst_pin))
		return -ENODEV;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -ENODEV;

	cf = devm_kzalloc(&pdev->dev, sizeof(*cf), GFP_KERNEL);
	if (!cf)
		return -ENOMEM;

	cf->board = board;
	cf->pdev = pdev;
	cf->phys_baseaddr = io->start;
	platform_set_drvdata(pdev, cf);

	/* must be a GPIO; ergo must trigger on both edges */
	status = devm_gpio_request(&pdev->dev, board->det_pin, "cf_det");
	if (status < 0)
		return status;

	status = devm_request_irq(&pdev->dev, gpio_to_irq(board->det_pin),
					at91_cf_irq, 0, "at91_cf detect", cf);
	if (status < 0)
		return status;

	device_init_wakeup(&pdev->dev, 1);

	status = devm_gpio_request(&pdev->dev, board->rst_pin, "cf_rst");
	if (status < 0)
		goto fail0a;

	if (gpio_is_valid(board->vcc_pin)) {
		status = devm_gpio_request(&pdev->dev, board->vcc_pin, "cf_vcc");
		if (status < 0)
			goto fail0a;
	}

	/*
	 * The card driver will request this irq later as needed.
	 * but it causes lots of "irqNN: nobody cared" messages
	 * unless we report that we handle everything (sigh).
	 * (Note:  DK board doesn't wire the IRQ pin...)
	 */
	if (gpio_is_valid(board->irq_pin)) {
		status = devm_gpio_request(&pdev->dev, board->irq_pin, "cf_irq");
		if (status < 0)
			goto fail0a;

		status = devm_request_irq(&pdev->dev, gpio_to_irq(board->irq_pin),
					at91_cf_irq, IRQF_SHARED, "at91_cf", cf);
		if (status < 0)
			goto fail0a;
		cf->socket.pci_irq = gpio_to_irq(board->irq_pin);
	} else
		cf->socket.pci_irq = nr_irqs + 1;

	/* pcmcia layer only remaps "real" memory not iospace */
	cf->socket.io_offset = (unsigned long) devm_ioremap(&pdev->dev,
					cf->phys_baseaddr + CF_IO_PHYS, SZ_2K);
	if (!cf->socket.io_offset) {
		status = -ENXIO;
		goto fail0a;
	}

	/* reserve chip-select regions */
	if (!devm_request_mem_region(&pdev->dev, io->start, resource_size(io), "at91_cf")) {
		status = -ENXIO;
		goto fail0a;
	}

	dev_info(&pdev->dev, "irqs det #%d, io #%d\n",
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
		goto fail0a;

	return 0;

fail0a:
	device_init_wakeup(&pdev->dev, 0);
	return status;
}

static int at91_cf_remove(struct platform_device *pdev)
{
	struct at91_cf_socket	*cf = platform_get_drvdata(pdev);

	pcmcia_unregister_socket(&cf->socket);
	device_init_wakeup(&pdev->dev, 0);

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
		.name		= "at91_cf",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(at91_cf_dt_ids),
	},
	.probe		= at91_cf_probe,
	.remove		= at91_cf_remove,
	.suspend	= at91_cf_suspend,
	.resume		= at91_cf_resume,
};

module_platform_driver(at91_cf_driver);

MODULE_DESCRIPTION("AT91 Compact Flash Driver");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_cf");
