/*
 * PCMCIA driver for SL811HS (as found in REX-CFU1U)
 * Filename: sl811_cs.c
 * Author:   Yukio Yamamoto
 *
 *  Port to sl811-hcd and 2.6.x by
 *    Botond Botyanszki <boti@rocketmail.com>
 *    Simon Pickering
 *
 *  Last update: 2005-05-12
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <linux/usb_sl811.h>

MODULE_AUTHOR("Botond Botyanszki");
MODULE_DESCRIPTION("REX-CFU1U PCMCIA driver for 2.6");
MODULE_LICENSE("GPL");


/*====================================================================*/
/* MACROS                                                             */
/*====================================================================*/

#if defined(DEBUG) || defined(CONFIG_USB_DEBUG) || defined(PCMCIA_DEBUG)

static int pc_debug = 0;
module_param(pc_debug, int, 0644);

#define DBG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG "sl811_cs: " args)

#else
#define DBG(n, args...) do{}while(0)
#endif	/* no debugging */

#define INFO(args...) printk(KERN_INFO "sl811_cs: " args)

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0444)

#define CS_CHECK(fn, ret) \
	do { \
		last_fn = (fn); \
		if ((last_ret = (ret)) != 0) \
			goto cs_failed; \
	} while (0)

/*====================================================================*/
/* VARIABLES                                                          */
/*====================================================================*/

static const char driver_name[DEV_NAME_LEN]  = "sl811_cs";

static dev_link_t *dev_list = NULL;

typedef struct local_info_t {
	dev_link_t		link;
	dev_node_t		node;
} local_info_t;

/*====================================================================*/

static void release_platform_dev(struct device * dev)
{
	DBG(0, "sl811_cs platform_dev release\n");
	dev->parent = NULL;
}

static struct sl811_platform_data platform_data = {
	.potpg		= 100,
	.power		= 50,		/* == 100mA */
	// .reset	= ... FIXME:  invoke CF reset on the card
};

static struct resource resources[] = {
	[0] = {
		.flags	= IORESOURCE_IRQ,
	},
	[1] = {
		// .name   = "address",
		.flags	= IORESOURCE_IO,
	},
	[2] = {
		// .name   = "data",
		.flags	= IORESOURCE_IO,
	},
};

extern struct device_driver sl811h_driver;

static struct platform_device platform_dev = {
	.id			= -1,
	.dev = {
		.platform_data = &platform_data,
		.release       = release_platform_dev,
	},
	.resource		= resources,
	.num_resources		= ARRAY_SIZE(resources),
};

static int sl811_hc_init(struct device *parent, ioaddr_t base_addr, int irq)
{
	if (platform_dev.dev.parent)
		return -EBUSY;
	platform_dev.dev.parent = parent;

	/* finish seting up the platform device */
	resources[0].start = irq;

	resources[1].start = base_addr;
	resources[1].end = base_addr;

	resources[2].start = base_addr + 1;
	resources[2].end   = base_addr + 1;

	/* The driver core will probe for us.  We know sl811-hcd has been
	 * initialized already because of the link order dependency.
	 */
	platform_dev.name = sl811h_driver.name;
	return platform_device_register(&platform_dev);
}

/*====================================================================*/

static void sl811_cs_detach(dev_link_t *link)
{
	dev_link_t **linkp;

	DBG(0, "sl811_cs_detach(0x%p)\n", link);

	/* Locate device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next) {
		if (*linkp == link)
			break;
	}
	if (*linkp == NULL)
		return;

	/* Break the link with Card Services */
	if (link->handle)
		pcmcia_deregister_client(link->handle);

	/* Unlink device structure, and free it */
	*linkp = link->next;
	/* This points to the parent local_info_t struct */
	kfree(link->priv);
}

static void sl811_cs_release(dev_link_t * link)
{

	DBG(0, "sl811_cs_release(0x%p)\n", link);

	if (link->open) {
		DBG(1, "sl811_cs: release postponed, '%s' still open\n",
		    link->dev->dev_name);
		link->state |= DEV_STALE_CONFIG;
		return;
	}

	/* Unlink the device chain */
	link->dev = NULL;

	platform_device_unregister(&platform_dev);
	pcmcia_release_configuration(link->handle);
	if (link->io.NumPorts1)
		pcmcia_release_io(link->handle, &link->io);
	if (link->irq.AssignedIRQ)
		pcmcia_release_irq(link->handle, &link->irq);
	link->state &= ~DEV_CONFIG;

	if (link->state & DEV_STALE_LINK)
		sl811_cs_detach(link);
}

static void sl811_cs_config(dev_link_t *link)
{
	client_handle_t		handle = link->handle;
	struct device		*parent = &handle_to_dev(handle);
	local_info_t		*dev = link->priv;
	tuple_t			tuple;
	cisparse_t		parse;
	int			last_fn, last_ret;
	u_char			buf[64];
	config_info_t		conf;
	cistpl_cftable_entry_t	dflt = { 0 };

	DBG(0, "sl811_cs_config(0x%p)\n", link);

	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	/* Look up the current Vcc */
	CS_CHECK(GetConfigurationInfo,
			pcmcia_get_configuration_info(handle, &conf));
	link->conf.Vcc = conf.Vcc;

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	while (1) {
		cistpl_cftable_entry_t	*cfg = &(parse.cftable_entry);

		if (pcmcia_get_tuple_data(handle, &tuple) != 0
				|| pcmcia_parse_tuple(handle, &tuple, &parse)
						!= 0)
			goto next_entry;

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT) {
			dflt = *cfg;
		}

		if (cfg->index == 0)
			goto next_entry;

		link->conf.ConfigIndex = cfg->index;

		/* Use power settings for Vcc and Vpp if present */
		/*  Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1<<CISTPL_POWER_VNOM)) {
			if (cfg->vcc.param[CISTPL_POWER_VNOM]/10000
					!= conf.Vcc)
				goto next_entry;
		} else if (dflt.vcc.present & (1<<CISTPL_POWER_VNOM)) {
			if (dflt.vcc.param[CISTPL_POWER_VNOM]/10000
					!= conf.Vcc)
				goto next_entry;
		}

		if (cfg->vpp1.present & (1<<CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
				cfg->vpp1.param[CISTPL_POWER_VNOM]/10000;
		else if (dflt.vpp1.present & (1<<CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
				dflt.vpp1.param[CISTPL_POWER_VNOM]/10000;

		/* we need an interrupt */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;

			link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;

			if (pcmcia_request_io(link->handle, &link->io) != 0)
				goto next_entry;
		}
		break;

next_entry:
		if (link->io.NumPorts1)
			pcmcia_release_io(link->handle, &link->io);
		last_ret = pcmcia_get_next_tuple(handle, &tuple);
	}

	/* require an IRQ and two registers */
	if (!link->io.NumPorts1 || link->io.NumPorts1 < 2)
		goto cs_failed;
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		CS_CHECK(RequestIRQ,
			pcmcia_request_irq(link->handle, &link->irq));
	else
		goto cs_failed;

	CS_CHECK(RequestConfiguration,
		pcmcia_request_configuration(link->handle, &link->conf));

	sprintf(dev->node.dev_name, driver_name);
	dev->node.major = dev->node.minor = 0;
	link->dev = &dev->node;

	printk(KERN_INFO "%s: index 0x%02x: Vcc %d.%d",
	       dev->node.dev_name, link->conf.ConfigIndex,
	       link->conf.Vcc/10, link->conf.Vcc%10);
	if (link->conf.Vpp1)
		printk(", Vpp %d.%d", link->conf.Vpp1/10, link->conf.Vpp1%10);
	printk(", irq %d", link->irq.AssignedIRQ);
	printk(", io 0x%04x-0x%04x", link->io.BasePort1,
	       link->io.BasePort1+link->io.NumPorts1-1);
	printk("\n");

	link->state &= ~DEV_CONFIG_PENDING;

	if (sl811_hc_init(parent, link->io.BasePort1, link->irq.AssignedIRQ)
			< 0) {
cs_failed:
		printk("sl811_cs_config failed\n");
		cs_error(link->handle, last_fn, last_ret);
		sl811_cs_release(link);
		link->state &= ~DEV_CONFIG_PENDING;
	}
}

static int
sl811_cs_event(event_t event, int priority, event_callback_args_t *args)
{
	dev_link_t *link = args->client_data;

	DBG(1, "sl811_cs_event(0x%06x)\n", event);

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG)
			sl811_cs_release(link);
		break;

	case CS_EVENT_CARD_INSERTION:
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		sl811_cs_config(link);
		break;

	case CS_EVENT_PM_SUSPEND:
		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		if (link->state & DEV_CONFIG)
			pcmcia_release_configuration(link->handle);
		break;

	case CS_EVENT_PM_RESUME:
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		if (link->state & DEV_CONFIG)
			pcmcia_request_configuration(link->handle, &link->conf);
		DBG(0, "reset sl811-hcd here?\n");
		break;
	}
	return 0;
}

static dev_link_t *sl811_cs_attach(void)
{
	local_info_t *local;
	dev_link_t *link;
	client_reg_t client_reg;
	int ret;

	local = kmalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local)
		return NULL;
	memset(local, 0, sizeof(local_info_t));
	link = &local->link;
	link->priv = local;

	/* Initialize */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_INFO2_VALID|IRQ_LEVEL_ID;
	link->irq.Handler = NULL;

	link->conf.Attributes = 0;
	link->conf.Vcc = 33;
	link->conf.IntType = INT_MEMORY_AND_IO;

	/* Register with Card Services */
	link->next = dev_list;
	dev_list = link;
	client_reg.dev_info = (dev_info_t *) &driver_name;
	client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;
	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		sl811_cs_detach(link);
		return NULL;
	}

	return link;
}

static struct pcmcia_device_id sl811_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0xc015, 0x0001), /* RATOC USB HOST CF+ Card */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, sl811_ids);

static struct pcmcia_driver sl811_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= (char *)driver_name,
	},
	.attach		= sl811_cs_attach,
	.event		= sl811_cs_event,
	.detach		= sl811_cs_detach,
	.id_table	= sl811_ids,
};

/*====================================================================*/

static int __init init_sl811_cs(void)
{
	return pcmcia_register_driver(&sl811_cs_driver);
}
module_init(init_sl811_cs);

static void __exit exit_sl811_cs(void)
{
	pcmcia_unregister_driver(&sl811_cs_driver);
}
module_exit(exit_sl811_cs);
