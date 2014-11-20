/*
 * PCMCIA client driver for AVM A1 / Fritz!PCMCIA
 *
 * Author       Carsten Paeth
 * Copyright    1998-2001 by Carsten Paeth <calle@calle.in-berlin.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include "hisax_cfg.h"

MODULE_DESCRIPTION("ISDN4Linux: PCMCIA client driver for AVM A1/Fritz!PCMCIA cards");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");


/*====================================================================*/

/* Parameters that can be set with 'insmod' */

static int isdnprot = 2;

module_param(isdnprot, int, 0);

/*====================================================================*/

static int avma1cs_config(struct pcmcia_device *link);
static void avma1cs_release(struct pcmcia_device *link);
static void avma1cs_detach(struct pcmcia_device *p_dev);

static int avma1cs_probe(struct pcmcia_device *p_dev)
{
	dev_dbg(&p_dev->dev, "avma1cs_attach()\n");

	/* General socket configuration */
	p_dev->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;
	p_dev->config_index = 1;
	p_dev->config_regs = PRESENT_OPTION;

	return avma1cs_config(p_dev);
} /* avma1cs_attach */

static void avma1cs_detach(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "avma1cs_detach(0x%p)\n", link);
	avma1cs_release(link);
	kfree(link->priv);
} /* avma1cs_detach */

static int avma1cs_configcheck(struct pcmcia_device *p_dev, void *priv_data)
{
	p_dev->resource[0]->end = 16;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->io_lines = 5;

	return pcmcia_request_io(p_dev);
}


static int avma1cs_config(struct pcmcia_device *link)
{
	int i = -1;
	char devname[128];
	IsdnCard_t	icard;
	int busy = 0;

	dev_dbg(&link->dev, "avma1cs_config(0x%p)\n", link);

	devname[0] = 0;
	if (link->prod_id[1])
		strlcpy(devname, link->prod_id[1], sizeof(devname));

	if (pcmcia_loop_config(link, avma1cs_configcheck, NULL))
		return -ENODEV;

	do {
		/*
		 * allocate an interrupt line
		 */
		if (!link->irq) {
			/* undo */
			pcmcia_disable_device(link);
			break;
		}

		/*
		 * configure the PCMCIA socket
		 */
		i = pcmcia_enable_device(link);
		if (i != 0) {
			pcmcia_disable_device(link);
			break;
		}

	} while (0);

	/* If any step failed, release any partially configured state */
	if (i != 0) {
		avma1cs_release(link);
		return -ENODEV;
	}

	icard.para[0] = link->irq;
	icard.para[1] = link->resource[0]->start;
	icard.protocol = isdnprot;
	icard.typ = ISDN_CTYPE_A1_PCMCIA;

	i = hisax_init_pcmcia(link, &busy, &icard);
	if (i < 0) {
		printk(KERN_ERR "avma1_cs: failed to initialize AVM A1 "
		       "PCMCIA %d at i/o %#x\n", i,
		       (unsigned int) link->resource[0]->start);
		avma1cs_release(link);
		return -ENODEV;
	}
	link->priv = (void *) (unsigned long) i;

	return 0;
} /* avma1cs_config */

static void avma1cs_release(struct pcmcia_device *link)
{
	unsigned long minor = (unsigned long) link->priv;

	dev_dbg(&link->dev, "avma1cs_release(0x%p)\n", link);

	/* now unregister function with hisax */
	HiSax_closecard(minor);

	pcmcia_disable_device(link);
} /* avma1cs_release */

static const struct pcmcia_device_id avma1cs_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("AVM", "ISDN A", 0x95d42008, 0xadc9d4bb),
	PCMCIA_DEVICE_PROD_ID12("ISDN", "CARD", 0x8d9761c8, 0x01c5aa7b),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, avma1cs_ids);

static struct pcmcia_driver avma1cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "avma1_cs",
	.probe		= avma1cs_probe,
	.remove		= avma1cs_detach,
	.id_table	= avma1cs_ids,
};
module_pcmcia_driver(avma1cs_driver);
