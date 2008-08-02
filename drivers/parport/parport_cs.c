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

#include <linux/parport.h>
#include <linux/parport_pc.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
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

#ifdef PCMCIA_DEBUG
INT_MODULE_PARM(pc_debug, PCMCIA_DEBUG);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"parport_cs.c 1.29 2002/10/11 06:57:41 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

#define FORCE_EPP_MODE	0x08

typedef struct parport_info_t {
	struct pcmcia_device	*p_dev;
    int			ndev;
    dev_node_t		node;
    struct parport	*port;
} parport_info_t;

static void parport_detach(struct pcmcia_device *p_dev);
static int parport_config(struct pcmcia_device *link);
static void parport_cs_release(struct pcmcia_device *);

/*======================================================================

    parport_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

======================================================================*/

static int parport_probe(struct pcmcia_device *link)
{
    parport_info_t *info;

    DEBUG(0, "parport_attach()\n");

    /* Create new parport device */
    info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (!info) return -ENOMEM;
    link->priv = info;
    info->p_dev = link;

    link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;

    return parport_config(link);
} /* parport_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void parport_detach(struct pcmcia_device *link)
{
    DEBUG(0, "parport_detach(0x%p)\n", link);

    parport_cs_release(link);

    kfree(link->priv);
} /* parport_detach */

/*======================================================================

    parport_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    parport device available to the system.

======================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static int parport_config_check(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cfg,
				cistpl_cftable_entry_t *dflt,
				void *priv_data)
{
	if ((cfg->io.nwin > 0) || (dflt->io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt->io;
		if (epp_mode)
			p_dev->conf.ConfigIndex |= FORCE_EPP_MODE;
		p_dev->io.BasePort1 = io->win[0].base;
		p_dev->io.NumPorts1 = io->win[0].len;
		p_dev->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
		if (io->nwin == 2) {
			p_dev->io.BasePort2 = io->win[1].base;
			p_dev->io.NumPorts2 = io->win[1].len;
		}
		if (pcmcia_request_io(p_dev, &p_dev->io) != 0)
			return -ENODEV;
		return 0;
	}
}

static int parport_config(struct pcmcia_device *link)
{
    parport_info_t *info = link->priv;
    struct parport *p;
    int last_ret, last_fn;

    DEBUG(0, "parport_config(0x%p)\n", link);

    last_ret = pcmcia_loop_config(link, parport_config_check, NULL);
    if (last_ret) {
	    cs_error(link, RequestIO, last_ret);
	    goto failed;
    }

    CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));

    p = parport_pc_probe_port(link->io.BasePort1, link->io.BasePort2,
			      link->irq.AssignedIRQ, PARPORT_DMA_NONE,
			      &link->dev);
    if (p == NULL) {
	printk(KERN_NOTICE "parport_cs: parport_pc_probe_port() at "
	       "0x%3x, irq %u failed\n", link->io.BasePort1,
	       link->irq.AssignedIRQ);
	goto failed;
    }

    p->modes |= PARPORT_MODE_PCSPP;
    if (epp_mode)
	p->modes |= PARPORT_MODE_TRISTATE | PARPORT_MODE_EPP;
    info->ndev = 1;
    info->node.major = LP_MAJOR;
    info->node.minor = p->number;
    info->port = p;
    strcpy(info->node.dev_name, p->name);
    link->dev_node = &info->node;

    return 0;

cs_failed:
    cs_error(link, last_fn, last_ret);
failed:
    parport_cs_release(link);
    return -ENODEV;
} /* parport_config */

/*======================================================================

    After a card is removed, parport_cs_release() will unregister the
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

static void parport_cs_release(struct pcmcia_device *link)
{
	parport_info_t *info = link->priv;

	DEBUG(0, "parport_release(0x%p)\n", link);

	if (info->ndev) {
		struct parport *p = info->port;
		parport_pc_unregister_port(p);
	}
	info->ndev = 0;

	pcmcia_disable_device(link);
} /* parport_cs_release */


static struct pcmcia_device_id parport_ids[] = {
	PCMCIA_DEVICE_FUNC_ID(3),
	PCMCIA_MFC_DEVICE_PROD_ID12(1,"Elan","Serial+Parallel Port: SP230",0x3beb8cf2,0xdb9e58bc),
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0003),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, parport_ids);

static struct pcmcia_driver parport_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "parport_cs",
	},
	.probe		= parport_probe,
	.remove		= parport_detach,
	.id_table	= parport_ids,
};

static int __init init_parport_cs(void)
{
	return pcmcia_register_driver(&parport_cs_driver);
}

static void __exit exit_parport_cs(void)
{
	pcmcia_unregister_driver(&parport_cs_driver);
}

module_init(init_parport_cs);
module_exit(exit_parport_cs);
