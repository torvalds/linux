/*
 * Copyright (C) 2007 PA Semi, Inc
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Based on drivers/pcmcia/omap_cf.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

#include <pcmcia/ss.h>

static const char driver_name[] = "electra-cf";

struct electra_cf_socket {
	struct pcmcia_socket	socket;

	struct timer_list	timer;
	unsigned		present:1;
	unsigned		active:1;

	struct platform_device	*ofdev;
	unsigned long		mem_phys;
	void __iomem *		mem_base;
	unsigned long		mem_size;
	void __iomem *		io_virt;
	unsigned int		io_base;
	unsigned int		io_size;
	u_int			irq;
	struct resource		iomem;
	void __iomem *		gpio_base;
	int			gpio_detect;
	int			gpio_vsense;
	int			gpio_3v;
	int			gpio_5v;
};

#define	POLL_INTERVAL		(2 * HZ)


static int electra_cf_present(struct electra_cf_socket *cf)
{
	unsigned int gpio;

	gpio = in_le32(cf->gpio_base+0x40);
	return !(gpio & (1 << cf->gpio_detect));
}

static int electra_cf_ss_init(struct pcmcia_socket *s)
{
	return 0;
}

/* the timer is primarily to kick this socket's pccardd */
static void electra_cf_timer(unsigned long _cf)
{
	struct electra_cf_socket *cf = (void *) _cf;
	int present = electra_cf_present(cf);

	if (present != cf->present) {
		cf->present = present;
		pcmcia_parse_events(&cf->socket, SS_DETECT);
	}

	if (cf->active)
		mod_timer(&cf->timer, jiffies + POLL_INTERVAL);
}

static irqreturn_t electra_cf_irq(int irq, void *_cf)
{
	electra_cf_timer((unsigned long)_cf);
	return IRQ_HANDLED;
}

static int electra_cf_get_status(struct pcmcia_socket *s, u_int *sp)
{
	struct electra_cf_socket *cf;

	if (!sp)
		return -EINVAL;

	cf = container_of(s, struct electra_cf_socket, socket);

	/* NOTE CF is always 3VCARD */
	if (electra_cf_present(cf)) {
		*sp = SS_READY | SS_DETECT | SS_POWERON | SS_3VCARD;

		s->pci_irq = cf->irq;
	} else
		*sp = 0;
	return 0;
}

static int electra_cf_set_socket(struct pcmcia_socket *sock,
				 struct socket_state_t *s)
{
	unsigned int gpio;
	unsigned int vcc;
	struct electra_cf_socket *cf;

	cf = container_of(sock, struct electra_cf_socket, socket);

	/* "reset" means no power in our case */
	vcc = (s->flags & SS_RESET) ? 0 : s->Vcc;

	switch (vcc) {
	case 0:
		gpio = 0;
		break;
	case 33:
		gpio = (1 << cf->gpio_3v);
		break;
	case 5:
		gpio = (1 << cf->gpio_5v);
		break;
	default:
		return -EINVAL;
	}

	gpio |= 1 << (cf->gpio_3v + 16); /* enwr */
	gpio |= 1 << (cf->gpio_5v + 16); /* enwr */
	out_le32(cf->gpio_base+0x90, gpio);

	pr_debug("%s: Vcc %d, io_irq %d, flags %04x csc %04x\n",
		driver_name, s->Vcc, s->io_irq, s->flags, s->csc_mask);

	return 0;
}

static int electra_cf_set_io_map(struct pcmcia_socket *s,
				 struct pccard_io_map *io)
{
	return 0;
}

static int electra_cf_set_mem_map(struct pcmcia_socket *s,
				  struct pccard_mem_map *map)
{
	struct electra_cf_socket *cf;

	if (map->card_start)
		return -EINVAL;
	cf = container_of(s, struct electra_cf_socket, socket);
	map->static_start = cf->mem_phys;
	map->flags &= MAP_ACTIVE|MAP_ATTRIB;
	if (!(map->flags & MAP_ATTRIB))
		map->static_start += 0x800;
	return 0;
}

static struct pccard_operations electra_cf_ops = {
	.init			= electra_cf_ss_init,
	.get_status		= electra_cf_get_status,
	.set_socket		= electra_cf_set_socket,
	.set_io_map		= electra_cf_set_io_map,
	.set_mem_map		= electra_cf_set_mem_map,
};

static int electra_cf_probe(struct platform_device *ofdev)
{
	struct device *device = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct electra_cf_socket   *cf;
	struct resource mem, io;
	int status;
	const unsigned int *prop;
	int err;
	struct vm_struct *area;

	err = of_address_to_resource(np, 0, &mem);
	if (err)
		return -EINVAL;

	err = of_address_to_resource(np, 1, &io);
	if (err)
		return -EINVAL;

	cf = kzalloc(sizeof *cf, GFP_KERNEL);
	if (!cf)
		return -ENOMEM;

	setup_timer(&cf->timer, electra_cf_timer, (unsigned long)cf);
	cf->irq = NO_IRQ;

	cf->ofdev = ofdev;
	cf->mem_phys = mem.start;
	cf->mem_size = PAGE_ALIGN(resource_size(&mem));
	cf->mem_base = ioremap(cf->mem_phys, cf->mem_size);
	cf->io_size = PAGE_ALIGN(resource_size(&io));

	area = __get_vm_area(cf->io_size, 0, PHB_IO_BASE, PHB_IO_END);
	if (area == NULL)
		return -ENOMEM;

	cf->io_virt = (void __iomem *)(area->addr);

	cf->gpio_base = ioremap(0xfc103000, 0x1000);
	dev_set_drvdata(device, cf);

	if (!cf->mem_base || !cf->io_virt || !cf->gpio_base ||
	    (__ioremap_at(io.start, cf->io_virt, cf->io_size,
		_PAGE_NO_CACHE | _PAGE_GUARDED) == NULL)) {
		dev_err(device, "can't ioremap ranges\n");
		status = -ENOMEM;
		goto fail1;
	}


	cf->io_base = (unsigned long)cf->io_virt - VMALLOC_END;

	cf->iomem.start = (unsigned long)cf->mem_base;
	cf->iomem.end = (unsigned long)cf->mem_base + (mem.end - mem.start);
	cf->iomem.flags = IORESOURCE_MEM;

	cf->irq = irq_of_parse_and_map(np, 0);

	status = request_irq(cf->irq, electra_cf_irq, IRQF_SHARED,
			     driver_name, cf);
	if (status < 0) {
		dev_err(device, "request_irq failed\n");
		goto fail1;
	}

	cf->socket.pci_irq = cf->irq;

	prop = of_get_property(np, "card-detect-gpio", NULL);
	if (!prop)
		goto fail1;
	cf->gpio_detect = *prop;

	prop = of_get_property(np, "card-vsense-gpio", NULL);
	if (!prop)
		goto fail1;
	cf->gpio_vsense = *prop;

	prop = of_get_property(np, "card-3v-gpio", NULL);
	if (!prop)
		goto fail1;
	cf->gpio_3v = *prop;

	prop = of_get_property(np, "card-5v-gpio", NULL);
	if (!prop)
		goto fail1;
	cf->gpio_5v = *prop;

	cf->socket.io_offset = cf->io_base;

	/* reserve chip-select regions */
	if (!request_mem_region(cf->mem_phys, cf->mem_size, driver_name)) {
		status = -ENXIO;
		dev_err(device, "Can't claim memory region\n");
		goto fail1;
	}

	if (!request_region(cf->io_base, cf->io_size, driver_name)) {
		status = -ENXIO;
		dev_err(device, "Can't claim I/O region\n");
		goto fail2;
	}

	cf->socket.owner = THIS_MODULE;
	cf->socket.dev.parent = &ofdev->dev;
	cf->socket.ops = &electra_cf_ops;
	cf->socket.resource_ops = &pccard_static_ops;
	cf->socket.features = SS_CAP_PCCARD | SS_CAP_STATIC_MAP |
				SS_CAP_MEM_ALIGN;
	cf->socket.map_size = 0x800;

	status = pcmcia_register_socket(&cf->socket);
	if (status < 0) {
		dev_err(device, "pcmcia_register_socket failed\n");
		goto fail3;
	}

	dev_info(device, "at mem 0x%lx io 0x%llx irq %d\n",
		 cf->mem_phys, io.start, cf->irq);

	cf->active = 1;
	electra_cf_timer((unsigned long)cf);
	return 0;

fail3:
	release_region(cf->io_base, cf->io_size);
fail2:
	release_mem_region(cf->mem_phys, cf->mem_size);
fail1:
	if (cf->irq != NO_IRQ)
		free_irq(cf->irq, cf);

	if (cf->io_virt)
		__iounmap_at(cf->io_virt, cf->io_size);
	if (cf->mem_base)
		iounmap(cf->mem_base);
	if (cf->gpio_base)
		iounmap(cf->gpio_base);
	device_init_wakeup(&ofdev->dev, 0);
	kfree(cf);
	return status;

}

static int electra_cf_remove(struct platform_device *ofdev)
{
	struct device *device = &ofdev->dev;
	struct electra_cf_socket *cf;

	cf = dev_get_drvdata(device);

	cf->active = 0;
	pcmcia_unregister_socket(&cf->socket);
	free_irq(cf->irq, cf);
	del_timer_sync(&cf->timer);

	__iounmap_at(cf->io_virt, cf->io_size);
	iounmap(cf->mem_base);
	iounmap(cf->gpio_base);
	release_mem_region(cf->mem_phys, cf->mem_size);
	release_region(cf->io_base, cf->io_size);

	kfree(cf);

	return 0;
}

static const struct of_device_id electra_cf_match[] = {
	{
		.compatible   = "electra-cf",
	},
	{},
};
MODULE_DEVICE_TABLE(of, electra_cf_match);

static struct platform_driver electra_cf_driver = {
	.driver = {
		.name = driver_name,
		.of_match_table = electra_cf_match,
	},
	.probe	  = electra_cf_probe,
	.remove   = electra_cf_remove,
};

module_platform_driver(electra_cf_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi Electra CF driver");
