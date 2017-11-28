// SPDX-License-Identifier: GPL-2.0
/*
 * IPWireless 3G PCMCIA Network Driver
 *
 * Original code
 *   by Stephen Blackheath <stephen@blacksapphire.com>,
 *      Ben Martel <benm@symmetric.co.nz>
 *
 * Copyrighted as follows:
 *   Copyright (C) 2004 by Symmetric Systems Ltd (NZ)
 *
 * Various driver changes and rewrites, port to new kernels
 *   Copyright (C) 2006-2007 Jiri Kosina
 *
 * Misc code cleanups and updates
 *   Copyright (C) 2007 David Sterba
 */

#include "hardware.h"
#include "network.h"
#include "main.h"
#include "tty.h"

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <pcmcia/cisreg.h>
#include <pcmcia/device_id.h>
#include <pcmcia/ss.h>
#include <pcmcia/ds.h>

static const struct pcmcia_device_id ipw_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x02f2, 0x0100),
	PCMCIA_DEVICE_MANF_CARD(0x02f2, 0x0200),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, ipw_ids);

static void ipwireless_detach(struct pcmcia_device *link);

/*
 * Module params
 */
/* Debug mode: more verbose, print sent/recv bytes */
int ipwireless_debug;
int ipwireless_loopback;
int ipwireless_out_queue = 10;

module_param_named(debug, ipwireless_debug, int, 0);
module_param_named(loopback, ipwireless_loopback, int, 0);
module_param_named(out_queue, ipwireless_out_queue, int, 0);
MODULE_PARM_DESC(debug, "switch on debug messages [0]");
MODULE_PARM_DESC(loopback,
		"debug: enable ras_raw channel [0]");
MODULE_PARM_DESC(out_queue, "debug: set size of outgoing PPP queue [10]");

/* Executes in process context. */
static void signalled_reboot_work(struct work_struct *work_reboot)
{
	struct ipw_dev *ipw = container_of(work_reboot, struct ipw_dev,
			work_reboot);
	struct pcmcia_device *link = ipw->link;
	pcmcia_reset_card(link->socket);
}

static void signalled_reboot_callback(void *callback_data)
{
	struct ipw_dev *ipw = (struct ipw_dev *) callback_data;

	/* Delegate to process context. */
	schedule_work(&ipw->work_reboot);
}

static int ipwireless_probe(struct pcmcia_device *p_dev, void *priv_data)
{
	struct ipw_dev *ipw = priv_data;
	int ret;

	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_AUTO;

	/* 0x40 causes it to generate level mode interrupts. */
	/* 0x04 enables IREQ pin. */
	p_dev->config_index |= 0x44;
	p_dev->io_lines = 16;
	ret = pcmcia_request_io(p_dev);
	if (ret)
		return ret;

	if (!request_region(p_dev->resource[0]->start,
			    resource_size(p_dev->resource[0]),
			    IPWIRELESS_PCCARD_NAME)) {
		ret = -EBUSY;
		goto exit;
	}

	p_dev->resource[2]->flags |=
		WIN_DATA_WIDTH_16 | WIN_MEMORY_TYPE_CM | WIN_ENABLE;

	ret = pcmcia_request_window(p_dev, p_dev->resource[2], 0);
	if (ret != 0)
		goto exit1;

	ret = pcmcia_map_mem_page(p_dev, p_dev->resource[2], p_dev->card_addr);
	if (ret != 0)
		goto exit1;

	ipw->is_v2_card = resource_size(p_dev->resource[2]) == 0x100;

	ipw->common_memory = ioremap(p_dev->resource[2]->start,
				resource_size(p_dev->resource[2]));
	if (!request_mem_region(p_dev->resource[2]->start,
				resource_size(p_dev->resource[2]),
				IPWIRELESS_PCCARD_NAME)) {
		ret = -EBUSY;
		goto exit2;
	}

	p_dev->resource[3]->flags |= WIN_DATA_WIDTH_16 | WIN_MEMORY_TYPE_AM |
					WIN_ENABLE;
	p_dev->resource[3]->end = 0; /* this used to be 0x1000 */
	ret = pcmcia_request_window(p_dev, p_dev->resource[3], 0);
	if (ret != 0)
		goto exit3;

	ret = pcmcia_map_mem_page(p_dev, p_dev->resource[3], 0);
	if (ret != 0)
		goto exit3;

	ipw->attr_memory = ioremap(p_dev->resource[3]->start,
				resource_size(p_dev->resource[3]));
	if (!request_mem_region(p_dev->resource[3]->start,
				resource_size(p_dev->resource[3]),
				IPWIRELESS_PCCARD_NAME)) {
		ret = -EBUSY;
		goto exit4;
	}

	return 0;

exit4:
	iounmap(ipw->attr_memory);
exit3:
	release_mem_region(p_dev->resource[2]->start,
			resource_size(p_dev->resource[2]));
exit2:
	iounmap(ipw->common_memory);
exit1:
	release_region(p_dev->resource[0]->start,
		       resource_size(p_dev->resource[0]));
exit:
	pcmcia_disable_device(p_dev);
	return ret;
}

static int config_ipwireless(struct ipw_dev *ipw)
{
	struct pcmcia_device *link = ipw->link;
	int ret = 0;

	ipw->is_v2_card = 0;
	link->config_flags |= CONF_AUTO_SET_IO | CONF_AUTO_SET_IOMEM |
		CONF_ENABLE_IRQ;

	ret = pcmcia_loop_config(link, ipwireless_probe, ipw);
	if (ret != 0)
		return ret;

	INIT_WORK(&ipw->work_reboot, signalled_reboot_work);

	ipwireless_init_hardware_v1(ipw->hardware, link->resource[0]->start,
				    ipw->attr_memory, ipw->common_memory,
				    ipw->is_v2_card, signalled_reboot_callback,
				    ipw);

	ret = pcmcia_request_irq(link, ipwireless_interrupt);
	if (ret != 0)
		goto exit;

	printk(KERN_INFO IPWIRELESS_PCCARD_NAME ": Card type %s\n",
			ipw->is_v2_card ? "V2/V3" : "V1");
	printk(KERN_INFO IPWIRELESS_PCCARD_NAME
		": I/O ports %pR, irq %d\n", link->resource[0],
			(unsigned int) link->irq);
	if (ipw->attr_memory && ipw->common_memory)
		printk(KERN_INFO IPWIRELESS_PCCARD_NAME
			": attr memory %pR, common memory %pR\n",
			link->resource[3],
			link->resource[2]);

	ipw->network = ipwireless_network_create(ipw->hardware);
	if (!ipw->network)
		goto exit;

	ipw->tty = ipwireless_tty_create(ipw->hardware, ipw->network);
	if (!ipw->tty)
		goto exit;

	ipwireless_init_hardware_v2_v3(ipw->hardware);

	/*
	 * Do the RequestConfiguration last, because it enables interrupts.
	 * Then we don't get any interrupts before we're ready for them.
	 */
	ret = pcmcia_enable_device(link);
	if (ret != 0)
		goto exit;

	return 0;

exit:
	if (ipw->common_memory) {
		release_mem_region(link->resource[2]->start,
				resource_size(link->resource[2]));
		iounmap(ipw->common_memory);
	}
	if (ipw->attr_memory) {
		release_mem_region(link->resource[3]->start,
				resource_size(link->resource[3]));
		iounmap(ipw->attr_memory);
	}
	pcmcia_disable_device(link);
	return -1;
}

static void release_ipwireless(struct ipw_dev *ipw)
{
	release_region(ipw->link->resource[0]->start,
		       resource_size(ipw->link->resource[0]));
	if (ipw->common_memory) {
		release_mem_region(ipw->link->resource[2]->start,
				resource_size(ipw->link->resource[2]));
		iounmap(ipw->common_memory);
	}
	if (ipw->attr_memory) {
		release_mem_region(ipw->link->resource[3]->start,
				resource_size(ipw->link->resource[3]));
		iounmap(ipw->attr_memory);
	}
	pcmcia_disable_device(ipw->link);
}

/*
 * ipwireless_attach() creates an "instance" of the driver, allocating
 * local data structures for one device (one interface).  The device
 * is registered with Card Services.
 *
 * The pcmcia_device structure is initialized, but we don't actually
 * configure the card at this point -- we wait until we receive a
 * card insertion event.
 */
static int ipwireless_attach(struct pcmcia_device *link)
{
	struct ipw_dev *ipw;
	int ret;

	ipw = kzalloc(sizeof(struct ipw_dev), GFP_KERNEL);
	if (!ipw)
		return -ENOMEM;

	ipw->link = link;
	link->priv = ipw;

	ipw->hardware = ipwireless_hardware_create();
	if (!ipw->hardware) {
		kfree(ipw);
		return -ENOMEM;
	}
	/* RegisterClient will call config_ipwireless */

	ret = config_ipwireless(ipw);

	if (ret != 0) {
		ipwireless_detach(link);
		return ret;
	}

	return 0;
}

/*
 * This deletes a driver "instance".  The device is de-registered with
 * Card Services.  If it has been released, all local data structures
 * are freed.  Otherwise, the structures will be freed when the device
 * is released.
 */
static void ipwireless_detach(struct pcmcia_device *link)
{
	struct ipw_dev *ipw = link->priv;

	release_ipwireless(ipw);

	if (ipw->tty != NULL)
		ipwireless_tty_free(ipw->tty);
	if (ipw->network != NULL)
		ipwireless_network_free(ipw->network);
	if (ipw->hardware != NULL)
		ipwireless_hardware_free(ipw->hardware);
	kfree(ipw);
}

static struct pcmcia_driver me = {
	.owner		= THIS_MODULE,
	.probe          = ipwireless_attach,
	.remove         = ipwireless_detach,
	.name		= IPWIRELESS_PCCARD_NAME,
	.id_table       = ipw_ids
};

/*
 * Module insertion : initialisation of the module.
 * Register the card with cardmgr...
 */
static int __init init_ipwireless(void)
{
	int ret;

	ret = ipwireless_tty_init();
	if (ret != 0)
		return ret;

	ret = pcmcia_register_driver(&me);
	if (ret != 0)
		ipwireless_tty_release();

	return ret;
}

/*
 * Module removal
 */
static void __exit exit_ipwireless(void)
{
	pcmcia_unregister_driver(&me);
	ipwireless_tty_release();
}

module_init(init_ipwireless);
module_exit(exit_ipwireless);

MODULE_AUTHOR(IPWIRELESS_PCMCIA_AUTHOR);
MODULE_DESCRIPTION(IPWIRELESS_PCCARD_NAME " " IPWIRELESS_PCMCIA_VERSION);
MODULE_LICENSE("GPL");
