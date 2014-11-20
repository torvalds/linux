/*======================================================================

    A driver for PCMCIA parallel port adapters

    (specifically, for the Quatech SPP-100 EPP card: other cards will
    probably require driver tweaks)
    
    parport_cs.c 1.29 2002/10/11 06:57:41

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
#include <linux/major.h>
#include <linux/interrupt.h>

#include <linux/parport.h>
#include <linux/parport_pc.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("PCMCIA parallel port card driver");
MODULE_LICENSE("Dual MPL/GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0)

INT_MODULE_PARM(epp_mode, 1);


/*====================================================================*/

#define FORCE_EPP_MODE	0x08

typedef struct parport_info_t {
	struct pcmcia_device	*p_dev;
    int			ndev;
    struct parport	*port;
} parport_info_t;

static void parport_detach(struct pcmcia_device *p_dev);
static int parport_config(struct pcmcia_device *link);
static void parport_cs_release(struct pcmcia_device *);

static int parport_probe(struct pcmcia_device *link)
{
    parport_info_t *info;

    dev_dbg(&link->dev, "parport_attach()\n");

    /* Create new parport device */
    info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (!info) return -ENOMEM;
    link->priv = info;
    info->p_dev = link;

    link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

    return parport_config(link);
} /* parport_attach */

static void parport_detach(struct pcmcia_device *link)
{
    dev_dbg(&link->dev, "parport_detach\n");

    parport_cs_release(link);

    kfree(link->priv);
} /* parport_detach */

static int parport_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->resource[1]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[1]->flags |= IO_DATA_PATH_WIDTH_8;

	return pcmcia_request_io(p_dev);
}

static int parport_config(struct pcmcia_device *link)
{
    parport_info_t *info = link->priv;
    struct parport *p;
    int ret;

    dev_dbg(&link->dev, "parport_config\n");

    if (epp_mode)
	    link->config_index |= FORCE_EPP_MODE;

    ret = pcmcia_loop_config(link, parport_config_check, NULL);
    if (ret)
	    goto failed;

    if (!link->irq)
	    goto failed;
    ret = pcmcia_enable_device(link);
    if (ret)
	    goto failed;

    p = parport_pc_probe_port(link->resource[0]->start,
			      link->resource[1]->start,
			      link->irq, PARPORT_DMA_NONE,
			      &link->dev, IRQF_SHARED);
    if (p == NULL) {
	printk(KERN_NOTICE "parport_cs: parport_pc_probe_port() at "
	       "0x%3x, irq %u failed\n",
	       (unsigned int) link->resource[0]->start,
	       link->irq);
	goto failed;
    }

    p->modes |= PARPORT_MODE_PCSPP;
    if (epp_mode)
	p->modes |= PARPORT_MODE_TRISTATE | PARPORT_MODE_EPP;
    info->ndev = 1;
    info->port = p;

    return 0;

failed:
    parport_cs_release(link);
    return -ENODEV;
} /* parport_config */

static void parport_cs_release(struct pcmcia_device *link)
{
	parport_info_t *info = link->priv;

	dev_dbg(&link->dev, "parport_release\n");

	if (info->ndev) {
		struct parport *p = info->port;
		parport_pc_unregister_port(p);
	}
	info->ndev = 0;

	pcmcia_disable_device(link);
} /* parport_cs_release */


static const struct pcmcia_device_id parport_ids[] = {
	PCMCIA_DEVICE_FUNC_ID(3),
	PCMCIA_MFC_DEVICE_PROD_ID12(1,"Elan","Serial+Parallel Port: SP230",0x3beb8cf2,0xdb9e58bc),
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0003),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, parport_ids);

static struct pcmcia_driver parport_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "parport_cs",
	.probe		= parport_probe,
	.remove		= parport_detach,
	.id_table	= parport_ids,
};
module_pcmcia_driver(parport_cs_driver);
