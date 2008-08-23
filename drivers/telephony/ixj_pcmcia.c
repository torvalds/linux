#include "ixj-ver.h"

#include <linux/module.h>

#include <linux/init.h>
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
static int ixj_config(struct pcmcia_device * link);
static void ixj_cs_release(struct pcmcia_device * link);

static int ixj_probe(struct pcmcia_device *p_dev)
{
	DEBUG(0, "ixj_attach()\n");
	/* Create new ixj device */
	p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	p_dev->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
	p_dev->io.IOAddrLines = 3;
	p_dev->conf.IntType = INT_MEMORY_AND_IO;
	p_dev->priv = kzalloc(sizeof(struct ixj_info_t), GFP_KERNEL);
	if (!p_dev->priv) {
		return -ENOMEM;
	}

	return ixj_config(p_dev);
}

static void ixj_detach(struct pcmcia_device *link)
{
	DEBUG(0, "ixj_detach(0x%p)\n", link);

	ixj_cs_release(link);

        kfree(link->priv);
}

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void ixj_get_serial(struct pcmcia_device * link, IXJ * j)
{
	char *str;
	int i, place;
	DEBUG(0, "ixj_get_serial(0x%p)\n", link);

	str = link->prod_id[0];
	if (!str)
		goto cs_failed;
	printk("%s", str);
	str = link->prod_id[1];
	if (!str)
		goto cs_failed;
	printk(" %s", str);
	str = link->prod_id[2];
	if (!str)
		goto cs_failed;
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
	str = link->prod_id[3];
	if (!str)
		goto cs_failed;
	printk(" version %s\n", str);
      cs_failed:
	return;
}

static int ixj_config_check(struct pcmcia_device *p_dev,
			    cistpl_cftable_entry_t *cfg,
			    cistpl_cftable_entry_t *dflt,
			    unsigned int vcc,
			    void *priv_data)
{
	if ((cfg->io.nwin > 0) || (dflt->io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt->io;
		p_dev->io.BasePort1 = io->win[0].base;
		p_dev->io.NumPorts1 = io->win[0].len;
		if (io->nwin == 2) {
			p_dev->io.BasePort2 = io->win[1].base;
			p_dev->io.NumPorts2 = io->win[1].len;
		}
		if (!pcmcia_request_io(p_dev, &p_dev->io))
			return 0;
	}
	return -ENODEV;
}

static int ixj_config(struct pcmcia_device * link)
{
	IXJ *j;
	ixj_info_t *info;
	cistpl_cftable_entry_t dflt = { 0 };

	info = link->priv;
	DEBUG(0, "ixj_config(0x%p)\n", link);

	if (pcmcia_loop_config(link, ixj_config_check, &dflt))
		goto cs_failed;

	if (pcmcia_request_configuration(link, &link->conf))
		goto cs_failed;

	/*
 	 *	Register the card with the core.
	 */
	j = ixj_pcmcia_probe(link->io.BasePort1, link->io.BasePort1 + 0x10);

	info->ndev = 1;
	info->node.major = PHONE_MAJOR;
	link->dev_node = &info->node;
	ixj_get_serial(link, j);
	return 0;

      cs_failed:
	ixj_cs_release(link);
	return -ENODEV;
}

static void ixj_cs_release(struct pcmcia_device *link)
{
	ixj_info_t *info = link->priv;
	DEBUG(0, "ixj_cs_release(0x%p)\n", link);
	info->ndev = 0;
	pcmcia_disable_device(link);
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
	.probe		= ixj_probe,
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
