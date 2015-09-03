/*
 * omap_cf.c -- OMAP 16xx CompactFlash controller driver
 *
 * Copyright (c) 2005 David Brownell
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
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <pcmcia/ss.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/sizes.h>

#include <mach/mux.h>
#include <mach/tc.h>


/* NOTE:  don't expect this to support many I/O cards.  The 16xx chips have
 * hard-wired timings to support Compact Flash memory cards; they won't work
 * with various other devices (like WLAN adapters) without some external
 * logic to help out.
 *
 * NOTE:  CF controller docs disagree with address space docs as to where
 * CF_BASE really lives; this is a doc erratum.
 */
#define	CF_BASE	0xfffe2800

/* status; read after IRQ */
#define CF_STATUS			(CF_BASE + 0x00)
#	define	CF_STATUS_BAD_READ	(1 << 2)
#	define	CF_STATUS_BAD_WRITE	(1 << 1)
#	define	CF_STATUS_CARD_DETECT	(1 << 0)

/* which chipselect (CS0..CS3) is used for CF (active low) */
#define CF_CFG				(CF_BASE + 0x02)

/* card reset */
#define CF_CONTROL			(CF_BASE + 0x04)
#	define	CF_CONTROL_RESET	(1 << 0)

#define omap_cf_present() (!(omap_readw(CF_STATUS) & CF_STATUS_CARD_DETECT))

/*--------------------------------------------------------------------------*/

static const char driver_name[] = "omap_cf";

struct omap_cf_socket {
	struct pcmcia_socket	socket;

	struct timer_list	timer;
	unsigned		present:1;
	unsigned		active:1;

	struct platform_device	*pdev;
	unsigned long		phys_cf;
	u_int			irq;
	struct resource		iomem;
};

#define	POLL_INTERVAL		(2 * HZ)

/*--------------------------------------------------------------------------*/

static int omap_cf_ss_init(struct pcmcia_socket *s)
{
	return 0;
}

/* the timer is primarily to kick this socket's pccardd */
static void omap_cf_timer(unsigned long _cf)
{
	struct omap_cf_socket	*cf = (void *) _cf;
	unsigned		present = omap_cf_present();

	if (present != cf->present) {
		cf->present = present;
		pr_debug("%s: card %s\n", driver_name,
			present ? "present" : "gone");
		pcmcia_parse_events(&cf->socket, SS_DETECT);
	}

	if (cf->active)
		mod_timer(&cf->timer, jiffies + POLL_INTERVAL);
}

/* This irq handler prevents "irqNNN: nobody cared" messages as drivers
 * claim the card's IRQ.  It may also detect some card insertions, but
 * not removals; it can't always eliminate timer irqs.
 */
static irqreturn_t omap_cf_irq(int irq, void *_cf)
{
	omap_cf_timer((unsigned long)_cf);
	return IRQ_HANDLED;
}

static int omap_cf_get_status(struct pcmcia_socket *s, u_int *sp)
{
	if (!sp)
		return -EINVAL;

	/* NOTE CF is always 3VCARD */
	if (omap_cf_present()) {
		struct omap_cf_socket	*cf;

		*sp = SS_READY | SS_DETECT | SS_POWERON | SS_3VCARD;
		cf = container_of(s, struct omap_cf_socket, socket);
		s->pcmcia_irq = 0;
		s->pci_irq = cf->irq;
	} else
		*sp = 0;
	return 0;
}

static int
omap_cf_set_socket(struct pcmcia_socket *sock, struct socket_state_t *s)
{
	u16		control;

	/* REVISIT some non-OSK boards may support power switching */
	switch (s->Vcc) {
	case 0:
	case 33:
		break;
	default:
		return -EINVAL;
	}

	control = omap_readw(CF_CONTROL);
	if (s->flags & SS_RESET)
		omap_writew(CF_CONTROL_RESET, CF_CONTROL);
	else
		omap_writew(0, CF_CONTROL);

	pr_debug("%s: Vcc %d, io_irq %d, flags %04x csc %04x\n",
		driver_name, s->Vcc, s->io_irq, s->flags, s->csc_mask);

	return 0;
}

static int omap_cf_ss_suspend(struct pcmcia_socket *s)
{
	pr_debug("%s: %s\n", driver_name, __func__);
	return omap_cf_set_socket(s, &dead_socket);
}

/* regions are 2K each:  mem, attrib, io (and reserved-for-ide) */

static int
omap_cf_set_io_map(struct pcmcia_socket *s, struct pccard_io_map *io)
{
	struct omap_cf_socket	*cf;

	cf = container_of(s, struct omap_cf_socket, socket);
	io->flags &= MAP_ACTIVE|MAP_ATTRIB|MAP_16BIT;
	io->start = cf->phys_cf + SZ_4K;
	io->stop = io->start + SZ_2K - 1;
	return 0;
}

static int
omap_cf_set_mem_map(struct pcmcia_socket *s, struct pccard_mem_map *map)
{
	struct omap_cf_socket	*cf;

	if (map->card_start)
		return -EINVAL;
	cf = container_of(s, struct omap_cf_socket, socket);
	map->static_start = cf->phys_cf;
	map->flags &= MAP_ACTIVE|MAP_ATTRIB|MAP_16BIT;
	if (map->flags & MAP_ATTRIB)
		map->static_start += SZ_2K;
	return 0;
}

static struct pccard_operations omap_cf_ops = {
	.init			= omap_cf_ss_init,
	.suspend		= omap_cf_ss_suspend,
	.get_status		= omap_cf_get_status,
	.set_socket		= omap_cf_set_socket,
	.set_io_map		= omap_cf_set_io_map,
	.set_mem_map		= omap_cf_set_mem_map,
};

/*--------------------------------------------------------------------------*/

/*
 * NOTE:  right now the only board-specific platform_data is
 * "what chipselect is used".  Boards could want more.
 */

static int __init omap_cf_probe(struct platform_device *pdev)
{
	unsigned		seg;
	struct omap_cf_socket	*cf;
	int			irq;
	int			status;

	seg = (int) pdev->dev.platform_data;
	if (seg == 0 || seg > 3)
		return -ENODEV;

	/* either CFLASH.IREQ (INT_1610_CF) or some GPIO */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EINVAL;

	cf = kzalloc(sizeof *cf, GFP_KERNEL);
	if (!cf)
		return -ENOMEM;
	setup_timer(&cf->timer, omap_cf_timer, (unsigned long)cf);

	cf->pdev = pdev;
	platform_set_drvdata(pdev, cf);

	/* this primarily just shuts up irq handling noise */
	status = request_irq(irq, omap_cf_irq, IRQF_SHARED,
			driver_name, cf);
	if (status < 0)
		goto fail0;
	cf->irq = irq;
	cf->socket.pci_irq = irq;

	switch (seg) {
	/* NOTE: CS0 could be configured too ... */
	case 1:
		cf->phys_cf = OMAP_CS1_PHYS;
		break;
	case 2:
		cf->phys_cf = OMAP_CS2_PHYS;
		break;
	case 3:
		cf->phys_cf = omap_cs3_phys();
		break;
	default:
		goto  fail1;
	}
	cf->iomem.start = cf->phys_cf;
	cf->iomem.end = cf->iomem.end + SZ_8K - 1;
	cf->iomem.flags = IORESOURCE_MEM;

	/* pcmcia layer only remaps "real" memory */
	cf->socket.io_offset = (unsigned long)
			ioremap(cf->phys_cf + SZ_4K, SZ_2K);
	if (!cf->socket.io_offset)
		goto fail1;

	if (!request_mem_region(cf->phys_cf, SZ_8K, driver_name))
		goto fail1;

	/* NOTE:  CF conflicts with MMC1 */
	omap_cfg_reg(W11_1610_CF_CD1);
	omap_cfg_reg(P11_1610_CF_CD2);
	omap_cfg_reg(R11_1610_CF_IOIS16);
	omap_cfg_reg(V10_1610_CF_IREQ);
	omap_cfg_reg(W10_1610_CF_RESET);

	omap_writew(~(1 << seg), CF_CFG);

	pr_info("%s: cs%d on irq %d\n", driver_name, seg, irq);

	/* NOTE:  better EMIFS setup might support more cards; but the
	 * TRM only shows how to affect regular flash signals, not their
	 * CF/PCMCIA variants...
	 */
	pr_debug("%s: cs%d, previous ccs %08x acs %08x\n", driver_name,
		seg, omap_readl(EMIFS_CCS(seg)), omap_readl(EMIFS_ACS(seg)));
	omap_writel(0x0004a1b3, EMIFS_CCS(seg));	/* synch mode 4 etc */
	omap_writel(0x00000000, EMIFS_ACS(seg));	/* OE hold/setup */

	/* CF uses armxor_ck, which is "always" available */

	pr_debug("%s: sts %04x cfg %04x control %04x %s\n", driver_name,
		omap_readw(CF_STATUS), omap_readw(CF_CFG),
		omap_readw(CF_CONTROL),
		omap_cf_present() ? "present" : "(not present)");

	cf->socket.owner = THIS_MODULE;
	cf->socket.dev.parent = &pdev->dev;
	cf->socket.ops = &omap_cf_ops;
	cf->socket.resource_ops = &pccard_static_ops;
	cf->socket.features = SS_CAP_PCCARD | SS_CAP_STATIC_MAP
				| SS_CAP_MEM_ALIGN;
	cf->socket.map_size = SZ_2K;
	cf->socket.io[0].res = &cf->iomem;

	status = pcmcia_register_socket(&cf->socket);
	if (status < 0)
		goto fail2;

	cf->active = 1;
	mod_timer(&cf->timer, jiffies + POLL_INTERVAL);
	return 0;

fail2:
	release_mem_region(cf->phys_cf, SZ_8K);
fail1:
	if (cf->socket.io_offset)
		iounmap((void __iomem *) cf->socket.io_offset);
	free_irq(irq, cf);
fail0:
	kfree(cf);
	return status;
}

static int __exit omap_cf_remove(struct platform_device *pdev)
{
	struct omap_cf_socket *cf = platform_get_drvdata(pdev);

	cf->active = 0;
	pcmcia_unregister_socket(&cf->socket);
	del_timer_sync(&cf->timer);
	iounmap((void __iomem *) cf->socket.io_offset);
	release_mem_region(cf->phys_cf, SZ_8K);
	free_irq(cf->irq, cf);
	kfree(cf);
	return 0;
}

static struct platform_driver omap_cf_driver = {
	.driver = {
		.name	= (char *) driver_name,
	},
	.remove		= __exit_p(omap_cf_remove),
};

static int __init omap_cf_init(void)
{
	if (cpu_is_omap16xx())
		return platform_driver_probe(&omap_cf_driver, omap_cf_probe);
	return -ENODEV;
}

static void __exit omap_cf_exit(void)
{
	if (cpu_is_omap16xx())
		platform_driver_unregister(&omap_cf_driver);
}

module_init(omap_cf_init);
module_exit(omap_cf_exit);

MODULE_DESCRIPTION("OMAP CF Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap_cf");
