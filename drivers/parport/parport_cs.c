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
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/major.h>

#include <linux/parport.h>
#include <linux/parport_pc.h>

#include <pcmcia/version.h>
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
    dev_link_t		link;
    int			ndev;
    dev_node_t		node;
    struct parport	*port;
} parport_info_t;

static dev_link_t *parport_attach(void);
static void parport_detach(dev_link_t *);
static void parport_config(dev_link_t *link);
static void parport_cs_release(dev_link_t *);
static int parport_event(event_t event, int priority,
			 event_callback_args_t *args);

static dev_info_t dev_info = "parport_cs";
static dev_link_t *dev_list = NULL;

/*======================================================================

    parport_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

======================================================================*/

static dev_link_t *parport_attach(void)
{
    parport_info_t *info;
    dev_link_t *link;
    client_reg_t client_reg;
    int ret;
    
    DEBUG(0, "parport_attach()\n");

    /* Create new parport device */
    info = kmalloc(sizeof(*info), GFP_KERNEL);
    if (!info) return NULL;
    memset(info, 0, sizeof(*info));
    link = &info->link; link->priv = info;

    link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
    link->io.Attributes2 = IO_DATA_PATH_WIDTH_8;
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;
    
    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.EventMask =
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &parport_event;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = pcmcia_register_client(&link->handle, &client_reg);
    if (ret != CS_SUCCESS) {
	cs_error(link->handle, RegisterClient, ret);
	parport_detach(link);
	return NULL;
    }
    
    return link;
} /* parport_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void parport_detach(dev_link_t *link)
{
    dev_link_t **linkp;
    int ret;

    DEBUG(0, "parport_detach(0x%p)\n", link);
    
    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    if (link->state & DEV_CONFIG)
	parport_cs_release(link);
    
    if (link->handle) {
	ret = pcmcia_deregister_client(link->handle);
	if (ret != CS_SUCCESS)
	    cs_error(link->handle, DeregisterClient, ret);
    }
    
    /* Unlink, free device structure */
    *linkp = link->next;
    kfree(link->priv);
    
} /* parport_detach */

/*======================================================================

    parport_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    parport device available to the system.

======================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

void parport_config(dev_link_t *link)
{
    client_handle_t handle = link->handle;
    parport_info_t *info = link->priv;
    tuple_t tuple;
    u_short buf[128];
    cisparse_t parse;
    config_info_t conf;
    cistpl_cftable_entry_t *cfg = &parse.cftable_entry;
    cistpl_cftable_entry_t dflt = { 0 };
    struct parport *p;
    int last_ret, last_fn;
    
    DEBUG(0, "parport_config(0x%p)\n", link);
    
    tuple.TupleData = (cisdata_t *)buf;
    tuple.TupleOffset = 0; tuple.TupleDataMax = 255;
    tuple.Attributes = 0;
    tuple.DesiredTuple = CISTPL_CONFIG;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
    CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
    link->conf.ConfigBase = parse.config.base;
    link->conf.Present = parse.config.rmask[0];
    
    /* Configure card */
    link->state |= DEV_CONFIG;

    /* Not sure if this is right... look up the current Vcc */
    CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(handle, &conf));
    
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
	    if (epp_mode)
		link->conf.ConfigIndex |= FORCE_EPP_MODE;
	    link->io.BasePort1 = io->win[0].base;
	    link->io.NumPorts1 = io->win[0].len;
	    link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
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
	if (cfg->flags & CISTPL_CFTABLE_DEFAULT) dflt = *cfg;
	CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(handle, &tuple));
    }
    
    CS_CHECK(RequestIRQ, pcmcia_request_irq(handle, &link->irq));
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(handle, &link->conf));

    release_region(link->io.BasePort1, link->io.NumPorts1);
    if (link->io.NumPorts2)
	release_region(link->io.BasePort2, link->io.NumPorts2);
    p = parport_pc_probe_port(link->io.BasePort1, link->io.BasePort2,
			      link->irq.AssignedIRQ, PARPORT_DMA_NONE,
			      NULL);
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
    link->dev = &info->node;

    link->state &= ~DEV_CONFIG_PENDING;
    return;
    
cs_failed:
    cs_error(link->handle, last_fn, last_ret);
failed:
    parport_cs_release(link);
    link->state &= ~DEV_CONFIG_PENDING;

} /* parport_config */

/*======================================================================

    After a card is removed, parport_cs_release() will unregister the
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

void parport_cs_release(dev_link_t *link)
{
    parport_info_t *info = link->priv;
    
    DEBUG(0, "parport_release(0x%p)\n", link);

    if (info->ndev) {
	struct parport *p = info->port;
	parport_pc_unregister_port(p);
	request_region(link->io.BasePort1, link->io.NumPorts1,
		       info->node.dev_name);
	if (link->io.NumPorts2)
	    request_region(link->io.BasePort2, link->io.NumPorts2,
			   info->node.dev_name);
    }
    info->ndev = 0;
    link->dev = NULL;
    
    pcmcia_release_configuration(link->handle);
    pcmcia_release_io(link->handle, &link->io);
    pcmcia_release_irq(link->handle, &link->irq);
    
    link->state &= ~DEV_CONFIG;

} /* parport_cs_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.
    
======================================================================*/

int parport_event(event_t event, int priority,
		  event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;

    DEBUG(1, "parport_event(0x%06x)\n", event);
    
    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG)
		parport_cs_release(link);
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	parport_config(link);
	break;
    case CS_EVENT_PM_SUSPEND:
	link->state |= DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
	if (link->state & DEV_CONFIG)
	    pcmcia_release_configuration(link->handle);
	break;
    case CS_EVENT_PM_RESUME:
	link->state &= ~DEV_SUSPEND;
	/* Fall through... */
    case CS_EVENT_CARD_RESET:
	if (DEV_OK(link))
	    pcmcia_request_configuration(link->handle, &link->conf);
	break;
    }
    return 0;
} /* parport_event */

static struct pcmcia_driver parport_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "parport_cs",
	},
	.attach		= parport_attach,
	.detach		= parport_detach,
};

static int __init init_parport_cs(void)
{
	return pcmcia_register_driver(&parport_cs_driver);
}

static void __exit exit_parport_cs(void)
{
	pcmcia_unregister_driver(&parport_cs_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_parport_cs);
module_exit(exit_parport_cs);
