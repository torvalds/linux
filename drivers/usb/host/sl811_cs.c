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
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <linux/usb/sl811.h>

MODULE_AUTHOR("Botond Botyanszki");
MODULE_DESCRIPTION("REX-CFU1U PCMCIA driver for 2.6");
MODULE_LICENSE("GPL");


/*====================================================================*/
/* MACROS                                                             */
/*====================================================================*/

#define INFO(args...) printk(KERN_INFO "sl811_cs: " args)

/*====================================================================*/
/* VARIABLES                                                          */
/*====================================================================*/

typedef struct local_info_t {
	struct pcmcia_device	*p_dev;
} local_info_t;

static void sl811_cs_release(struct pcmcia_device * link);

/*====================================================================*/

static void release_platform_dev(struct device * dev)
{
	dev_dbg(dev, "sl811_cs platform_dev release\n");
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

extern struct platform_driver sl811h_driver;

static struct platform_device platform_dev = {
	.id			= -1,
	.dev = {
		.platform_data = &platform_data,
		.release       = release_platform_dev,
	},
	.resource		= resources,
	.num_resources		= ARRAY_SIZE(resources),
};

static int sl811_hc_init(struct device *parent, resource_size_t base_addr,
			 int irq)
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
	 * initialized already because of the link order dependency created
	 * by referencing "sl811h_driver".
	 */
	platform_dev.name = sl811h_driver.driver.name;
	return platform_device_register(&platform_dev);
}

/*====================================================================*/

static void sl811_cs_detach(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "sl811_cs_detach\n");

	sl811_cs_release(link);

	/* This points to the parent local_info_t struct */
	kfree(link->priv);
}

static void sl811_cs_release(struct pcmcia_device * link)
{
	dev_dbg(&link->dev, "sl811_cs_release\n");

	pcmcia_disable_device(link);
	platform_device_unregister(&platform_dev);
}

static int sl811_cs_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}


static int sl811_cs_config(struct pcmcia_device *link)
{
	struct device		*parent = &link->dev;
	int			ret;

	dev_dbg(&link->dev, "sl811_cs_config\n");

	link->config_flags |= CONF_ENABLE_IRQ |	CONF_AUTO_SET_VPP |
		CONF_AUTO_CHECK_VCC | CONF_AUTO_SET_IO;

	if (pcmcia_loop_config(link, sl811_cs_config_check, NULL))
		goto failed;

	/* require an IRQ and two registers */
	if (resource_size(link->resource[0]) < 2)
		goto failed;

	if (!link->irq)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	dev_info(&link->dev, "index 0x%02x: ",
		link->config_index);
	if (link->vpp)
		printk(", Vpp %d.%d", link->vpp/10, link->vpp%10);
	printk(", irq %d", link->irq);
	printk(", io %pR", link->resource[0]);
	printk("\n");

	if (sl811_hc_init(parent, link->resource[0]->start, link->irq)
			< 0) {
failed:
		printk(KERN_WARNING "sl811_cs_config failed\n");
		sl811_cs_release(link);
		return  -ENODEV;
	}
	return 0;
}

static int sl811_cs_probe(struct pcmcia_device *link)
{
	local_info_t *local;

	local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->p_dev = link;
	link->priv = local;

	return sl811_cs_config(link);
}

static struct pcmcia_device_id sl811_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0xc015, 0x0001), /* RATOC USB HOST CF+ Card */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, sl811_ids);

static struct pcmcia_driver sl811_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "sl811_cs",
	},
	.probe		= sl811_cs_probe,
	.remove		= sl811_cs_detach,
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
