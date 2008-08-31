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
#include <pcmcia/cs.h>

static struct pcmcia_device_id ipw_ids[] = {
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
	int ret = pcmcia_reset_card(link->socket);

	if (ret != 0)
		cs_error(link, ResetCard, ret);
}

static void signalled_reboot_callback(void *callback_data)
{
	struct ipw_dev *ipw = (struct ipw_dev *) callback_data;

	/* Delegate to process context. */
	schedule_work(&ipw->work_reboot);
}

static int config_ipwireless(struct ipw_dev *ipw)
{
	struct pcmcia_device *link = ipw->link;
	int ret;
	tuple_t tuple;
	unsigned short buf[64];
	cisparse_t parse;
	unsigned short cor_value;
	memreq_t memreq_attr_memory;
	memreq_t memreq_common_memory;

	ipw->is_v2_card = 0;

	tuple.Attributes = 0;
	tuple.TupleData = (cisdata_t *) buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;

	tuple.DesiredTuple = RETURN_FIRST_TUPLE;

	ret = pcmcia_get_first_tuple(link, &tuple);

	while (ret == 0) {
		ret = pcmcia_get_tuple_data(link, &tuple);

		if (ret != 0) {
			cs_error(link, GetTupleData, ret);
			goto exit0;
		}
		ret = pcmcia_get_next_tuple(link, &tuple);
	}

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;

	ret = pcmcia_get_first_tuple(link, &tuple);

	if (ret != 0) {
		cs_error(link, GetFirstTuple, ret);
		goto exit0;
	}

	ret = pcmcia_get_tuple_data(link, &tuple);

	if (ret != 0) {
		cs_error(link, GetTupleData, ret);
		goto exit0;
	}

	ret = pcmcia_parse_tuple(link, &tuple, &parse);

	if (ret != 0) {
		cs_error(link, ParseTuple, ret);
		goto exit0;
	}

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	link->io.BasePort1 = parse.cftable_entry.io.win[0].base;
	link->io.NumPorts1 = parse.cftable_entry.io.win[0].len;
	link->io.IOAddrLines = 16;

	link->irq.IRQInfo1 = parse.cftable_entry.irq.IRQInfo1;

	/* 0x40 causes it to generate level mode interrupts. */
	/* 0x04 enables IREQ pin. */
	cor_value = parse.cftable_entry.index | 0x44;
	link->conf.ConfigIndex = cor_value;

	/* IRQ and I/O settings */
	tuple.DesiredTuple = CISTPL_CONFIG;

	ret = pcmcia_get_first_tuple(link, &tuple);

	if (ret != 0) {
		cs_error(link, GetFirstTuple, ret);
		goto exit0;
	}

	ret = pcmcia_get_tuple_data(link, &tuple);

	if (ret != 0) {
		cs_error(link, GetTupleData, ret);
		goto exit0;
	}

	ret = pcmcia_parse_tuple(link, &tuple, &parse);

	if (ret != 0) {
		cs_error(link, GetTupleData, ret);
		goto exit0;
	}
	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];
	link->conf.IntType = INT_MEMORY_AND_IO;

	link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING | IRQ_HANDLE_PRESENT;
	link->irq.Handler = ipwireless_interrupt;
	link->irq.Instance = ipw->hardware;

	ret = pcmcia_request_io(link, &link->io);

	if (ret != 0) {
		cs_error(link, RequestIO, ret);
		goto exit0;
	}

	request_region(link->io.BasePort1, link->io.NumPorts1,
			IPWIRELESS_PCCARD_NAME);

	/* memory settings */

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;

	ret = pcmcia_get_first_tuple(link, &tuple);

	if (ret != 0) {
		cs_error(link, GetFirstTuple, ret);
		goto exit1;
	}

	ret = pcmcia_get_tuple_data(link, &tuple);

	if (ret != 0) {
		cs_error(link, GetTupleData, ret);
		goto exit1;
	}

	ret = pcmcia_parse_tuple(link, &tuple, &parse);

	if (ret != 0) {
		cs_error(link, ParseTuple, ret);
		goto exit1;
	}

	if (parse.cftable_entry.mem.nwin > 0) {
		ipw->request_common_memory.Attributes =
			WIN_DATA_WIDTH_16 | WIN_MEMORY_TYPE_CM | WIN_ENABLE;
		ipw->request_common_memory.Base =
			parse.cftable_entry.mem.win[0].host_addr;
		ipw->request_common_memory.Size = parse.cftable_entry.mem.win[0].len;
		if (ipw->request_common_memory.Size < 0x1000)
			ipw->request_common_memory.Size = 0x1000;
		ipw->request_common_memory.AccessSpeed = 0;

		ret = pcmcia_request_window(&link, &ipw->request_common_memory,
				&ipw->handle_common_memory);

		if (ret != 0) {
			cs_error(link, RequestWindow, ret);
			goto exit1;
		}

		memreq_common_memory.CardOffset =
			parse.cftable_entry.mem.win[0].card_addr;
		memreq_common_memory.Page = 0;

		ret = pcmcia_map_mem_page(ipw->handle_common_memory,
				&memreq_common_memory);

		if (ret != 0) {
			cs_error(link, MapMemPage, ret);
			goto exit1;
		}

		ipw->is_v2_card =
			parse.cftable_entry.mem.win[0].len == 0x100;

		ipw->common_memory = ioremap(ipw->request_common_memory.Base,
				ipw->request_common_memory.Size);
		request_mem_region(ipw->request_common_memory.Base,
				ipw->request_common_memory.Size, IPWIRELESS_PCCARD_NAME);

		ipw->request_attr_memory.Attributes =
			WIN_DATA_WIDTH_16 | WIN_MEMORY_TYPE_AM | WIN_ENABLE;
		ipw->request_attr_memory.Base = 0;
		ipw->request_attr_memory.Size = 0;	/* this used to be 0x1000 */
		ipw->request_attr_memory.AccessSpeed = 0;

		ret = pcmcia_request_window(&link, &ipw->request_attr_memory,
				&ipw->handle_attr_memory);

		if (ret != 0) {
			cs_error(link, RequestWindow, ret);
			goto exit2;
		}

		memreq_attr_memory.CardOffset = 0;
		memreq_attr_memory.Page = 0;

		ret = pcmcia_map_mem_page(ipw->handle_attr_memory,
				&memreq_attr_memory);

		if (ret != 0) {
			cs_error(link, MapMemPage, ret);
			goto exit2;
		}

		ipw->attr_memory = ioremap(ipw->request_attr_memory.Base,
				ipw->request_attr_memory.Size);
		request_mem_region(ipw->request_attr_memory.Base, ipw->request_attr_memory.Size,
				IPWIRELESS_PCCARD_NAME);
	}

	INIT_WORK(&ipw->work_reboot, signalled_reboot_work);

	ipwireless_init_hardware_v1(ipw->hardware, link->io.BasePort1,
				    ipw->attr_memory, ipw->common_memory,
				    ipw->is_v2_card, signalled_reboot_callback,
				    ipw);

	ret = pcmcia_request_irq(link, &link->irq);

	if (ret != 0) {
		cs_error(link, RequestIRQ, ret);
		goto exit3;
	}

	printk(KERN_INFO IPWIRELESS_PCCARD_NAME ": Card type %s\n",
			ipw->is_v2_card ? "V2/V3" : "V1");
	printk(KERN_INFO IPWIRELESS_PCCARD_NAME
			": I/O ports 0x%04x-0x%04x, irq %d\n",
			(unsigned int) link->io.BasePort1,
			(unsigned int) (link->io.BasePort1 +
				link->io.NumPorts1 - 1),
			(unsigned int) link->irq.AssignedIRQ);
	if (ipw->attr_memory && ipw->common_memory)
		printk(KERN_INFO IPWIRELESS_PCCARD_NAME
			": attr memory 0x%08lx-0x%08lx, common memory 0x%08lx-0x%08lx\n",
			ipw->request_attr_memory.Base,
			ipw->request_attr_memory.Base
			+ ipw->request_attr_memory.Size - 1,
			ipw->request_common_memory.Base,
			ipw->request_common_memory.Base
			+ ipw->request_common_memory.Size - 1);

	ipw->network = ipwireless_network_create(ipw->hardware);
	if (!ipw->network)
		goto exit3;

	ipw->tty = ipwireless_tty_create(ipw->hardware, ipw->network,
			ipw->nodes);
	if (!ipw->tty)
		goto exit3;

	ipwireless_init_hardware_v2_v3(ipw->hardware);

	/*
	 * Do the RequestConfiguration last, because it enables interrupts.
	 * Then we don't get any interrupts before we're ready for them.
	 */
	ret = pcmcia_request_configuration(link, &link->conf);

	if (ret != 0) {
		cs_error(link, RequestConfiguration, ret);
		goto exit4;
	}

	link->dev_node = &ipw->nodes[0];

	return 0;

exit4:
	pcmcia_disable_device(link);
exit3:
	if (ipw->attr_memory) {
		release_mem_region(ipw->request_attr_memory.Base,
				ipw->request_attr_memory.Size);
		iounmap(ipw->attr_memory);
		pcmcia_release_window(ipw->handle_attr_memory);
		pcmcia_disable_device(link);
	}
exit2:
	if (ipw->common_memory) {
		release_mem_region(ipw->request_common_memory.Base,
				ipw->request_common_memory.Size);
		iounmap(ipw->common_memory);
		pcmcia_release_window(ipw->handle_common_memory);
	}
exit1:
	pcmcia_disable_device(link);
exit0:
	return -1;
}

static void release_ipwireless(struct ipw_dev *ipw)
{
	pcmcia_disable_device(ipw->link);

	if (ipw->common_memory) {
		release_mem_region(ipw->request_common_memory.Base,
				ipw->request_common_memory.Size);
		iounmap(ipw->common_memory);
	}
	if (ipw->attr_memory) {
		release_mem_region(ipw->request_attr_memory.Base,
				ipw->request_attr_memory.Size);
		iounmap(ipw->attr_memory);
	}
	if (ipw->common_memory)
		pcmcia_release_window(ipw->handle_common_memory);
	if (ipw->attr_memory)
		pcmcia_release_window(ipw->handle_attr_memory);

	/* Break the link with Card Services */
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
	link->irq.Instance = ipw;

	/* Link this device into our device list. */
	link->dev_node = &ipw->nodes[0];

	ipw->hardware = ipwireless_hardware_create();
	if (!ipw->hardware) {
		kfree(ipw);
		return -ENOMEM;
	}
	/* RegisterClient will call config_ipwireless */

	ret = config_ipwireless(ipw);

	if (ret != 0) {
		cs_error(link, RegisterClient, ret);
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
	.drv = { .name  = IPWIRELESS_PCCARD_NAME },
	.id_table       = ipw_ids
};

/*
 * Module insertion : initialisation of the module.
 * Register the card with cardmgr...
 */
static int __init init_ipwireless(void)
{
	int ret;

	printk(KERN_INFO IPWIRELESS_PCCARD_NAME " "
	       IPWIRELESS_PCMCIA_VERSION " by " IPWIRELESS_PCMCIA_AUTHOR "\n");

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
	printk(KERN_INFO IPWIRELESS_PCCARD_NAME " "
			IPWIRELESS_PCMCIA_VERSION " removed\n");

	pcmcia_unregister_driver(&me);
	ipwireless_tty_release();
}

module_init(init_ipwireless);
module_exit(exit_ipwireless);

MODULE_AUTHOR(IPWIRELESS_PCMCIA_AUTHOR);
MODULE_DESCRIPTION(IPWIRELESS_PCCARD_NAME " " IPWIRELESS_PCMCIA_VERSION);
MODULE_LICENSE("GPL");
