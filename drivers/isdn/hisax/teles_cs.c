/* $Id: teles_cs.c,v 1.1.2.2 2004/01/25 15:07:06 keil Exp $ */
/*======================================================================

  A teles S0 PCMCIA client driver

  Based on skeleton by David Hinds, dhinds@allegro.stanford.edu
  Written by Christof Petig, christof.petig@wtal.de

  Also inspired by ELSA PCMCIA driver
  by Klaus Lichtenwalder <Lichtenwalder@ACM.org>

  Extensions to new hisax_pcmcia by Karsten Keil

  minor changes to be compatible with kernel 2.4.x
  by Jan.Schubert@GMX.li

  ======================================================================*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <asm/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "hisax_cfg.h"

MODULE_DESCRIPTION("ISDN4Linux: PCMCIA client driver for Teles PCMCIA cards");
MODULE_AUTHOR("Christof Petig, christof.petig@wtal.de, Karsten Keil, kkeil@suse.de");
MODULE_LICENSE("GPL");


/*====================================================================*/

/* Parameters that can be set with 'insmod' */

static int protocol = 2;        /* EURO-ISDN Default */
module_param(protocol, int, 0);

static int teles_cs_config(struct pcmcia_device *link);
static void teles_cs_release(struct pcmcia_device *link);
static void teles_detach(struct pcmcia_device *p_dev);

typedef struct local_info_t {
	struct pcmcia_device	*p_dev;
	int                 busy;
	int			cardnr;
} local_info_t;

static int teles_probe(struct pcmcia_device *link)
{
	local_info_t *local;

	dev_dbg(&link->dev, "teles_attach()\n");

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local) return -ENOMEM;
	local->cardnr = -1;

	local->p_dev = link;
	link->priv = local;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	return teles_cs_config(link);
} /* teles_attach */

static void teles_detach(struct pcmcia_device *link)
{
	local_info_t *info = link->priv;

	dev_dbg(&link->dev, "teles_detach(0x%p)\n", link);

	info->busy = 1;
	teles_cs_release(link);

	kfree(info);
} /* teles_detach */

static int teles_cs_configcheck(struct pcmcia_device *p_dev, void *priv_data)
{
	int j;

	p_dev->io_lines = 5;
	p_dev->resource[0]->end = 96;
	p_dev->resource[0]->flags &= IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_AUTO;

	if ((p_dev->resource[0]->end) && p_dev->resource[0]->start) {
		printk(KERN_INFO "(teles_cs: looks like the 96 model)\n");
		if (!pcmcia_request_io(p_dev))
			return 0;
	} else {
		printk(KERN_INFO "(teles_cs: looks like the 97 model)\n");
		for (j = 0x2f0; j > 0x100; j -= 0x10) {
			p_dev->resource[0]->start = j;
			if (!pcmcia_request_io(p_dev))
				return 0;
		}
	}
	return -ENODEV;
}

static int teles_cs_config(struct pcmcia_device *link)
{
	int i;
	IsdnCard_t icard;

	dev_dbg(&link->dev, "teles_config(0x%p)\n", link);

	i = pcmcia_loop_config(link, teles_cs_configcheck, NULL);
	if (i != 0)
		goto cs_failed;

	if (!link->irq)
		goto cs_failed;

	i = pcmcia_enable_device(link);
	if (i != 0)
		goto cs_failed;

	icard.para[0] = link->irq;
	icard.para[1] = link->resource[0]->start;
	icard.protocol = protocol;
	icard.typ = ISDN_CTYPE_TELESPCMCIA;

	i = hisax_init_pcmcia(link, &(((local_info_t *)link->priv)->busy), &icard);
	if (i < 0) {
		printk(KERN_ERR "teles_cs: failed to initialize Teles PCMCIA %d at i/o %#x\n",
		       i, (unsigned int) link->resource[0]->start);
		teles_cs_release(link);
		return -ENODEV;
	}

	((local_info_t *)link->priv)->cardnr = i;
	return 0;

cs_failed:
	teles_cs_release(link);
	return -ENODEV;
} /* teles_cs_config */

static void teles_cs_release(struct pcmcia_device *link)
{
	local_info_t *local = link->priv;

	dev_dbg(&link->dev, "teles_cs_release(0x%p)\n", link);

	if (local) {
		if (local->cardnr >= 0) {
			/* no unregister function with hisax */
			HiSax_closecard(local->cardnr);
		}
	}

	pcmcia_disable_device(link);
} /* teles_cs_release */

static int teles_suspend(struct pcmcia_device *link)
{
	local_info_t *dev = link->priv;

	dev->busy = 1;

	return 0;
}

static int teles_resume(struct pcmcia_device *link)
{
	local_info_t *dev = link->priv;

	dev->busy = 0;

	return 0;
}


static const struct pcmcia_device_id teles_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("TELES", "S0/PC", 0x67b50eae, 0xe9e70119),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, teles_ids);

static struct pcmcia_driver teles_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "teles_cs",
	.probe		= teles_probe,
	.remove		= teles_detach,
	.id_table       = teles_ids,
	.suspend	= teles_suspend,
	.resume		= teles_resume,
};
module_pcmcia_driver(teles_cs_driver);
