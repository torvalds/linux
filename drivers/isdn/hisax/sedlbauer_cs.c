/*======================================================================

    A Sedlbauer PCMCIA client driver

    This driver is for the Sedlbauer Speed Star and Speed Star II, 
    which are ISDN PCMCIA Cards.
    
    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Modifications from dummy_cs.c are Copyright (C) 1999-2001 Marcus Niemann
    <maniemann@users.sourceforge.net>. All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in
    which case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
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
#include <asm/system.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "hisax_cfg.h"

MODULE_DESCRIPTION("ISDN4Linux: PCMCIA client driver for Sedlbauer cards");
MODULE_AUTHOR("Marcus Niemann");
MODULE_LICENSE("Dual MPL/GPL");


/*====================================================================*/

/* Parameters that can be set with 'insmod' */

static int protocol = 2;        /* EURO-ISDN Default */
module_param(protocol, int, 0);

static int sedlbauer_config(struct pcmcia_device *link) __devinit ;
static void sedlbauer_release(struct pcmcia_device *link);

static void sedlbauer_detach(struct pcmcia_device *p_dev) __devexit;

typedef struct local_info_t {
	struct pcmcia_device	*p_dev;
    int			stop;
    int			cardnr;
} local_info_t;

static int __devinit sedlbauer_probe(struct pcmcia_device *link)
{
    local_info_t *local;

    dev_dbg(&link->dev, "sedlbauer_attach()\n");

    /* Allocate space for private device-specific data */
    local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
    if (!local) return -ENOMEM;
    local->cardnr = -1;

    local->p_dev = link;
    link->priv = local;

    return sedlbauer_config(link);
} /* sedlbauer_attach */

static void __devexit sedlbauer_detach(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "sedlbauer_detach(0x%p)\n", link);

	((local_info_t *)link->priv)->stop = 1;
	sedlbauer_release(link);

	/* This points to the parent local_info_t struct */
	kfree(link->priv);
} /* sedlbauer_detach */

static int sedlbauer_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	p_dev->io_lines = 3;
	return pcmcia_request_io(p_dev);
}

static int __devinit sedlbauer_config(struct pcmcia_device *link)
{
    int ret;
    IsdnCard_t  icard;

    dev_dbg(&link->dev, "sedlbauer_config(0x%p)\n", link);

    link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_CHECK_VCC |
	    CONF_AUTO_SET_VPP | CONF_AUTO_AUDIO | CONF_AUTO_SET_IO;

    ret = pcmcia_loop_config(link, sedlbauer_config_check, NULL);
    if (ret)
	    goto failed;

    ret = pcmcia_enable_device(link);
    if (ret)
	    goto failed;

    icard.para[0] = link->irq;
    icard.para[1] = link->resource[0]->start;
    icard.protocol = protocol;
    icard.typ = ISDN_CTYPE_SEDLBAUER_PCMCIA;
    
    ret = hisax_init_pcmcia(link, 
			    &(((local_info_t *)link->priv)->stop), &icard);
    if (ret < 0) {
	printk(KERN_ERR "sedlbauer_cs: failed to initialize SEDLBAUER PCMCIA %d with %pR\n",
		ret, link->resource[0]);
    	sedlbauer_release(link);
	return -ENODEV;
    } else
	((local_info_t *)link->priv)->cardnr = ret;

    return 0;

failed:
    sedlbauer_release(link);
    return -ENODEV;

} /* sedlbauer_config */

static void sedlbauer_release(struct pcmcia_device *link)
{
    local_info_t *local = link->priv;
    dev_dbg(&link->dev, "sedlbauer_release(0x%p)\n", link);

    if (local) {
    	if (local->cardnr >= 0) {
    	    /* no unregister function with hisax */
	    HiSax_closecard(local->cardnr);
	}
    }

    pcmcia_disable_device(link);
} /* sedlbauer_release */

static int sedlbauer_suspend(struct pcmcia_device *link)
{
	local_info_t *dev = link->priv;

	dev->stop = 1;

	return 0;
}

static int sedlbauer_resume(struct pcmcia_device *link)
{
	local_info_t *dev = link->priv;

	dev->stop = 0;

	return 0;
}


static struct pcmcia_device_id sedlbauer_ids[] = {
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "speed star II", "V 3.1", 0x81fb79f5, 0xf3612e1d, 0x6b95c78a),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", "4D67", 0x81fb79f5, 0xe4e9bc12, 0x397b7e90),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", "4D98", 0x81fb79f5, 0xe4e9bc12, 0x2e5c7fce),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", " (C) 93-94 VK", 0x81fb79f5, 0xe4e9bc12, 0x8db143fe),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", " (c) 93-95 VK", 0x81fb79f5, 0xe4e9bc12, 0xb391ab4c),
	PCMCIA_DEVICE_PROD_ID12("HST High Soft Tech GmbH", "Saphir II B", 0xd79e0b84, 0x21d083ae),
/*	PCMCIA_DEVICE_PROD_ID1234("SEDLBAUER", 0x81fb79f5), */ /* too generic*/
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, sedlbauer_ids);

static struct pcmcia_driver sedlbauer_driver = {
	.owner		= THIS_MODULE,
	.name		= "sedlbauer_cs",
	.probe		= sedlbauer_probe,
	.remove		= __devexit_p(sedlbauer_detach),
	.id_table	= sedlbauer_ids,
	.suspend	= sedlbauer_suspend,
	.resume		= sedlbauer_resume,
};

static int __init init_sedlbauer_cs(void)
{
	return pcmcia_register_driver(&sedlbauer_driver);
}

static void __exit exit_sedlbauer_cs(void)
{
	pcmcia_unregister_driver(&sedlbauer_driver);
}

module_init(init_sedlbauer_cs);
module_exit(exit_sedlbauer_cs);
