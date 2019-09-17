/* $Id: avm_cs.c,v 1.4.6.3 2001/09/23 22:24:33 kai Exp $
 *
 * A PCMCIA client driver for AVM B1/M1/M2
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <asm/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include <linux/skbuff.h>
#include <linux/capi.h>
#include <linux/b1lli.h>
#include <linux/b1pcmcia.h>

/*====================================================================*/

MODULE_DESCRIPTION("CAPI4Linux: PCMCIA client driver for AVM B1/M1/M2");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/*====================================================================*/

static int avmcs_config(struct pcmcia_device *link);
static void avmcs_release(struct pcmcia_device *link);
static void avmcs_detach(struct pcmcia_device *p_dev);

static int avmcs_probe(struct pcmcia_device *p_dev)
{
	/* General socket configuration */
	p_dev->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;
	p_dev->config_index = 1;
	p_dev->config_regs = PRESENT_OPTION;

	return avmcs_config(p_dev);
} /* avmcs_attach */


static void avmcs_detach(struct pcmcia_device *link)
{
	avmcs_release(link);
} /* avmcs_detach */

static int avmcs_configcheck(struct pcmcia_device *p_dev, void *priv_data)
{
	p_dev->resource[0]->end = 16;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;

	return pcmcia_request_io(p_dev);
}

static int avmcs_config(struct pcmcia_device *link)
{
	int i = -1;
	char devname[128];
	int cardtype;
	int (*addcard)(unsigned int port, unsigned irq);

	devname[0] = 0;
	if (link->prod_id[1])
		strlcpy(devname, link->prod_id[1], sizeof(devname));

	/*
	 * find IO port
	 */
	if (pcmcia_loop_config(link, avmcs_configcheck, NULL))
		return -ENODEV;

	do {
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

	if (devname[0]) {
		char *s = strrchr(devname, ' ');
		if (!s)
			s = devname;
		else s++;
		if (strcmp("M1", s) == 0) {
			cardtype = AVM_CARDTYPE_M1;
		} else if (strcmp("M2", s) == 0) {
			cardtype = AVM_CARDTYPE_M2;
		} else {
			cardtype = AVM_CARDTYPE_B1;
		}
	} else
		cardtype = AVM_CARDTYPE_B1;

	/* If any step failed, release any partially configured state */
	if (i != 0) {
		avmcs_release(link);
		return -ENODEV;
	}


	switch (cardtype) {
	case AVM_CARDTYPE_M1: addcard = b1pcmcia_addcard_m1; break;
	case AVM_CARDTYPE_M2: addcard = b1pcmcia_addcard_m2; break;
	default:
	case AVM_CARDTYPE_B1: addcard = b1pcmcia_addcard_b1; break;
	}
	if ((i = (*addcard)(link->resource[0]->start, link->irq)) < 0) {
		dev_err(&link->dev,
			"avm_cs: failed to add AVM-Controller at i/o %#x, irq %d\n",
			(unsigned int) link->resource[0]->start, link->irq);
		avmcs_release(link);
		return -ENODEV;
	}
	return 0;

} /* avmcs_config */


static void avmcs_release(struct pcmcia_device *link)
{
	b1pcmcia_delcard(link->resource[0]->start, link->irq);
	pcmcia_disable_device(link);
} /* avmcs_release */


static const struct pcmcia_device_id avmcs_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("AVM", "ISDN-Controller B1", 0x95d42008, 0x845dc335),
	PCMCIA_DEVICE_PROD_ID12("AVM", "Mobile ISDN-Controller M1", 0x95d42008, 0x81e10430),
	PCMCIA_DEVICE_PROD_ID12("AVM", "Mobile ISDN-Controller M2", 0x95d42008, 0x18e8558a),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, avmcs_ids);

static struct pcmcia_driver avmcs_driver = {
	.owner	= THIS_MODULE,
	.name		= "avm_cs",
	.probe = avmcs_probe,
	.remove	= avmcs_detach,
	.id_table = avmcs_ids,
};
module_pcmcia_driver(avmcs_driver);
