#include "ixj-ver.h"

#include <linux/module.h>

#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/slab.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "ixj.h"

/*
 *	PCMCIA service support for Quicknet cards
 */
 

typedef struct ixj_info_t {
	int ndev;
	struct ixj *port;
} ixj_info_t;

static void ixj_detach(struct pcmcia_device *p_dev);
static int ixj_config(struct pcmcia_device * link);
static void ixj_cs_release(struct pcmcia_device * link);

static int ixj_probe(struct pcmcia_device *p_dev)
{
	dev_dbg(&p_dev->dev, "ixj_attach()\n");
	/* Create new ixj device */
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->resource[1]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->priv = kzalloc(sizeof(struct ixj_info_t), GFP_KERNEL);
	if (!p_dev->priv) {
		return -ENOMEM;
	}

	return ixj_config(p_dev);
}

static void ixj_detach(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "ixj_detach\n");

	ixj_cs_release(link);

        kfree(link->priv);
}

static void ixj_get_serial(struct pcmcia_device * link, IXJ * j)
{
	char *str;
	int i, place;
	dev_dbg(&link->dev, "ixj_get_serial\n");

	str = link->prod_id[0];
	if (!str)
		goto failed;
	printk("%s", str);
	str = link->prod_id[1];
	if (!str)
		goto failed;
	printk(" %s", str);
	str = link->prod_id[2];
	if (!str)
		goto failed;
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
		goto failed;
	printk(" version %s\n", str);
failed:
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
		p_dev->resource[0]->start = io->win[0].base;
		p_dev->resource[0]->end = io->win[0].len;
		p_dev->io_lines = 3;
		if (io->nwin == 2) {
			p_dev->resource[1]->start = io->win[1].base;
			p_dev->resource[1]->end = io->win[1].len;
		}
		if (!pcmcia_request_io(p_dev))
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
	dev_dbg(&link->dev, "ixj_config\n");

	if (pcmcia_loop_config(link, ixj_config_check, &dflt))
		goto failed;

	if (pcmcia_enable_device(link))
		goto failed;

	/*
 	 *	Register the card with the core.
	 */
	j = ixj_pcmcia_probe(link->resource[0]->start,
			     link->resource[0]->start + 0x10);

	info->ndev = 1;
	ixj_get_serial(link, j);
	return 0;

failed:
	ixj_cs_release(link);
	return -ENODEV;
}

static void ixj_cs_release(struct pcmcia_device *link)
{
	ixj_info_t *info = link->priv;
	dev_dbg(&link->dev, "ixj_cs_release\n");
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
