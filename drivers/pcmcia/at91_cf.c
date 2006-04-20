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
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <pcmcia/ss.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/sizes.h>

#include <asm/arch/at91rm9200.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>


/*
 * A0..A10 work in each range; A23 indicates I/O space;  A25 is CFRNW;
 * some other bit in {A24,A22..A11} is nREG to flag memory access
 * (vs attributes).  So more than 2KB/region would just be waste.
 */
#define	CF_ATTR_PHYS	(AT91_CF_BASE)
#define	CF_IO_PHYS	(AT91_CF_BASE  + (1 << 23))
#define	CF_MEM_PHYS	(AT91_CF_BASE  + 0x017ff800)

/*--------------------------------------------------------------------------*/

static const char driver_name[] = "at91_cf";

struct at91_cf_socket {
	struct pcmcia_socket	socket;

	unsigned		present:1;

	struct platform_device	*pdev;
	struct at91_cf_data	*board;
};

#define	SZ_2K			(2 * SZ_1K)

static inline int at91_cf_present(struct at91_cf_socket *cf)
{
	return !at91_get_gpio_value(cf->board->det_pin);
}

/*--------------------------------------------------------------------------*/

static int at91_cf_ss_init(struct pcmcia_socket *s)
{
	return 0;
}

static irqreturn_t at91_cf_irq(int irq, void *_cf, struct pt_regs *r)
{
	struct at91_cf_socket	*cf = (struct at91_cf_socket *) _cf;

	if (irq == cf->board->det_pin) {
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
		int rdy	= cf->board->irq_pin;	/* RDY/nIRQ */
		int vcc	= cf->board->vcc_pin;

		*sp = SS_DETECT | SS_3VCARD;
		if (!rdy || at91_get_gpio_value(rdy))
			*sp |= SS_READY;
		if (!vcc || at91_get_gpio_value(vcc))
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
	if (cf->board->vcc_pin) {
		switch (s->Vcc) {
			case 0:
				at91_set_gpio_value(cf->board->vcc_pin, 0);
				break;
			case 33:
				at91_set_gpio_value(cf->board->vcc_pin, 1);
				break;
			default:
				return -EINVAL;
		}
	}

	/* toggle reset if needed */
	at91_set_gpio_value(cf->board->rst_pin, s->flags & SS_RESET);

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
	 * Always set CSR4 ... PCMCIA won't always unmap things.
	 */
	csr = at91_sys_read(AT91_SMC_CSR(4)) & ~AT91_SMC_DBW;

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
	if (!(io->flags & (MAP_16BIT|MAP_AUTOSZ))) {
		csr |= AT91_SMC_DBW_8;
		pr_debug("%s: 8bit i/o bus\n", driver_name);
	} else {
		csr |= AT91_SMC_DBW_16;
		pr_debug("%s: 16bit i/o bus\n", driver_name);
	}
	at91_sys_write(AT91_SMC_CSR(4), csr);

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

	map->flags &= MAP_ACTIVE|MAP_ATTRIB|MAP_16BIT;
	if (map->flags & MAP_ATTRIB)
		map->static_start = CF_ATTR_PHYS;
	else
		map->static_start = CF_MEM_PHYS;

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

static int __init at91_cf_probe(struct device *dev)
{
	struct at91_cf_socket	*cf;
	struct at91_cf_data	*board = dev->platform_data;
	struct platform_device	*pdev = to_platform_device(dev);
	struct resource		*io;
	unsigned int		csa;
	int			status;

	if (!board || !board->det_pin || !board->rst_pin)
		return -ENODEV;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -ENODEV;

	cf = kcalloc(1, sizeof *cf, GFP_KERNEL);
	if (!cf)
		return -ENOMEM;

	cf->board = board;
	cf->pdev = pdev;
	dev_set_drvdata(dev, cf);

	/* CF takes over CS4, CS5, CS6 */
	csa = at91_sys_read(AT91_EBI_CSA);
	at91_sys_write(AT91_EBI_CSA, csa | AT91_EBI_CS4A_SMC_COMPACTFLASH);

	/* force poweron defaults for these pins ... */
	(void) at91_set_A_periph(AT91_PIN_PC9, 0);	/* A25/CFRNW */
	(void) at91_set_A_periph(AT91_PIN_PC10, 0);	/* NCS4/CFCS */
	(void) at91_set_A_periph(AT91_PIN_PC11, 0);	/* NCS5/CFCE1 */
	(void) at91_set_A_periph(AT91_PIN_PC12, 0);	/* NCS6/CFCE2 */

	/* nWAIT is _not_ a default setting */
	(void) at91_set_A_periph(AT91_PIN_PC6, 1);	/*  nWAIT */

	/*
	 * Static memory controller timing adjustments.
	 * REVISIT:  these timings are in terms of MCK cycles, so
	 * when MCK changes (cpufreq etc) so must these values...
	 */
	at91_sys_write(AT91_SMC_CSR(4),
				  AT91_SMC_ACSS_STD
				| AT91_SMC_DBW_16
				| AT91_SMC_BAT
				| AT91_SMC_WSEN
				| AT91_SMC_NWS_(32)	/* wait states */
				| AT91_SMC_RWSETUP_(6)	/* setup time */
				| AT91_SMC_RWHOLD_(4)	/* hold time */
	);

	/* must be a GPIO; ergo must trigger on both edges */
	status = request_irq(board->det_pin, at91_cf_irq,
			SA_SAMPLE_RANDOM, driver_name, cf);
	if (status < 0)
		goto fail0;

	/*
	 * The card driver will request this irq later as needed.
	 * but it causes lots of "irqNN: nobody cared" messages
	 * unless we report that we handle everything (sigh).
	 * (Note:  DK board doesn't wire the IRQ pin...)
	 */
	if (board->irq_pin) {
		status = request_irq(board->irq_pin, at91_cf_irq,
				SA_SHIRQ, driver_name, cf);
		if (status < 0)
			goto fail0a;
		cf->socket.pci_irq = board->irq_pin;
	} else
		cf->socket.pci_irq = NR_IRQS + 1;

	/* pcmcia layer only remaps "real" memory not iospace */
	cf->socket.io_offset = (unsigned long) ioremap(CF_IO_PHYS, SZ_2K);
	if (!cf->socket.io_offset)
		goto fail1;

	/* reserve CS4, CS5, and CS6 regions; but use just CS4 */
	if (!request_mem_region(io->start, io->end + 1 - io->start,
				driver_name))
		goto fail1;

	pr_info("%s: irqs det #%d, io #%d\n", driver_name,
		board->det_pin, board->irq_pin);

	cf->socket.owner = THIS_MODULE;
	cf->socket.dev.dev = dev;
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
	iounmap((void __iomem *) cf->socket.io_offset);
	release_mem_region(io->start, io->end + 1 - io->start);
fail1:
	if (board->irq_pin)
		free_irq(board->irq_pin, cf);
fail0a:
	free_irq(board->det_pin, cf);
fail0:
	at91_sys_write(AT91_EBI_CSA, csa);
	kfree(cf);
	return status;
}

static int __exit at91_cf_remove(struct device *dev)
{
	struct at91_cf_socket	*cf = dev_get_drvdata(dev);
	struct resource		*io = cf->socket.io[0].res;
	unsigned int		csa;

	pcmcia_unregister_socket(&cf->socket);
	free_irq(cf->board->irq_pin, cf);
	free_irq(cf->board->det_pin, cf);
	iounmap((void __iomem *) cf->socket.io_offset);
	release_mem_region(io->start, io->end + 1 - io->start);

	csa = at91_sys_read(AT91_EBI_CSA);
	at91_sys_write(AT91_EBI_CSA, csa & ~AT91_EBI_CS4A);

	kfree(cf);
	return 0;
}

static struct device_driver at91_cf_driver = {
	.name		= (char *) driver_name,
	.bus		= &platform_bus_type,
	.probe		= at91_cf_probe,
	.remove		= __exit_p(at91_cf_remove),
	.suspend	= pcmcia_socket_dev_suspend,
	.resume		= pcmcia_socket_dev_resume,
};

/*--------------------------------------------------------------------------*/

static int __init at91_cf_init(void)
{
	return driver_register(&at91_cf_driver);
}
module_init(at91_cf_init);

static void __exit at91_cf_exit(void)
{
	driver_unregister(&at91_cf_driver);
}
module_exit(at91_cf_exit);

MODULE_DESCRIPTION("AT91 Compact Flash Driver");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
