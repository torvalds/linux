#include "ixj-ver.h"

#include <linux/module.h>

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/slab.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "ixj.h"

/*
 *	PCMCIA service support for Quicknet cards
 */
 
#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0644);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
#else
#define DEBUG(n, args...)
#endif

typedef struct ixj_info_t {
	int ndev;
	dev_node_t node;
	struct ixj *port;
} ixj_info_t;

static void ixj_detach(struct pcmcia_device *p_dev);
static void ixj_config(dev_link_t * link);
static void ixj_cs_release(dev_link_t * link);

static int ixj_attach(struct pcmcia_device *p_dev)
{
	dev_link_t *link;

	DEBUG(0, "ixj_attach()\n");
	/* Create new ixj device */
	link = kmalloc(sizeof(struct dev_link_t), GFP_KERNEL);
	if (!link)
		return -ENOMEM;
	memset(link, 0, sizeof(struct dev_link_t));
	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
	link->io.IOAddrLines = 3;
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->priv = kmalloc(sizeof(struct ixj_info_t), GFP_KERNEL);
	if (!link->priv) {
		kfree(link);
		return -ENOMEM;
	}
	memset(link->priv, 0, sizeof(struct ixj_info_t));

	link->handle = p_dev;
	p_dev->instance = link;

	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	ixj_config(link);

	return 0;
}

static void ixj_detach(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);

	DEBUG(0, "ixj_detach(0x%p)\n", link);

	link->state &= ~DEV_RELEASE_PENDING;
	if (link->state & DEV_CONFIG)
		ixj_cs_release(link);

        kfree(link->priv);
        kfree(link);
}

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void ixj_get_serial(dev_link_t * link, IXJ * j)
{
	client_handle_t handle;
	tuple_t tuple;
	u_short buf[128];
	char *str;
	int last_ret, last_fn, i, place;
	handle = link->handle;
	DEBUG(0, "ixj_get_serial(0x%p)\n", link);
	tuple.TupleData = (cisdata_t *) buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 80;
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_VERS_1;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	str = (char *) buf;
	printk("PCMCIA Version %d.%d\n", str[0], str[1]);
	str += 2;
	printk("%s", str);
	str = str + strlen(str) + 1;
	printk(" %s", str);
	str = str + strlen(str) + 1;
	place = 1;
	for (i = strlen(str) - 1; i >= 0; i--) {
		switch (str[i]) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			j->serial += (str[i] - 48) * place;
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'E':
		case 'F':
			j->serial += (str[i] - 55) * place;
			break;
		case 'a':
		case 'b':
		case 'c':
		case 'd':
		case 'e':
		case 'f':
			j->serial += (str[i] - 87) * place;
			break;
		}
		place = place * 0x10;
	}
	str = str + strlen(str) + 1;
	printk(" version %s\n", str);
      cs_failed:
	return;
}

static void ixj_config(dev_link_t * link)
{
	IXJ *j;
	client_handle_t handle;
	ixj_info_t *info;
	tuple_t tuple;
	u_short buf[128];
	cisparse_t parse;
	cistpl_cftable_entry_t *cfg = &parse.cftable_entry;
	cistpl_cftable_entry_t dflt =
	{
		0
	};
	int last_ret, last_fn;
	handle = link->handle;
	info = link->priv;
	DEBUG(0, "ixj_config(0x%p)\n", link);
	tuple.TupleData = (cisdata_t *) buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_CONFIG;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];
	link->state |= DEV_CONFIG;
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	tuple.Attributes = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	while (1) {
		if (pcmcia_get_tuple_data(handle, &tuple) != 0 ||
				pcmcia_parse_tuple(handle, &tuple, &parse) != 0)
			goto next_entry;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->conf.ConfigIndex = cfg->index;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin == 2) {
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
			if (pcmcia_request_io(link->handle, &link->io) != 0)
				goto next_entry;
			/* If we've got this far, we're done */
			break;
		}
	      next_entry:
		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(handle, &tuple));
	}

	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(handle, &link->conf));

	/*
 	 *	Register the card with the core.
 	 */	
	j=ixj_pcmcia_probe(link->io.BasePort1,link->io.BasePort1 + 0x10);

	info->ndev = 1;
	info->node.major = PHONE_MAJOR;
	link->dev = &info->node;
	ixj_get_serial(link, j);
	link->state &= ~DEV_CONFIG_PENDING;
	return;
      cs_failed:
	cs_error(link->handle, last_fn, last_ret);
	ixj_cs_release(link);
}

static void ixj_cs_release(dev_link_t *link)
{
	ixj_info_t *info = link->priv;
	DEBUG(0, "ixj_cs_release(0x%p)\n", link);
	info->ndev = 0;
	pcmcia_disable_device(link->handle);
}

static struct pcmcia_device_id ixj_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0257, 0x0600),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, ixj_ids);

static struct pcmcia_driver ixj_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "ixj_cs",
	},
	.probe		= ixj_attach,
	.remove		= ixj_detach,
	.id_table	= ixj_ids,
};

static int __init ixj_pcmcia_init(void)
{
	return pcmcia_register_driver(&ixj_driver);
}

static void ixj_pcmcia_exit(void)
{
	pcmcia_unregister_driver(&ixj_driver);
}

module_init(ixj_pcmcia_init);
module_exit(ixj_pcmcia_exit);

MODULE_LICENSE("GPL");
