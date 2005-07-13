/*======================================================================

    A driver for Adaptec AHA152X-compatible PCMCIA SCSI cards.

    This driver supports the Adaptec AHA-1460, the New Media Bus
    Toaster, and the New Media Toast & Jam.
    
    aha152x_cs.c 1.54 2000/06/12 21:27:25

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
    terms of the GNU General Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <scsi/scsi.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <scsi/scsi_ioctl.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "aha152x.h"

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0644);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version =
"aha152x_cs.c 1.54 2000/06/12 21:27:25 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* SCSI bus setup options */
static int host_id = 7;
static int reconnect = 1;
static int parity = 1;
static int synchronous = 1;
static int reset_delay = 100;
static int ext_trans = 0;

module_param(host_id, int, 0);
module_param(reconnect, int, 0);
module_param(parity, int, 0);
module_param(synchronous, int, 0);
module_param(reset_delay, int, 0);
module_param(ext_trans, int, 0);

MODULE_LICENSE("Dual MPL/GPL");

/*====================================================================*/

typedef struct scsi_info_t {
    dev_link_t		link;
    dev_node_t		node;
    struct Scsi_Host	*host;
} scsi_info_t;

static void aha152x_release_cs(dev_link_t *link);
static int aha152x_event(event_t event, int priority,
			 event_callback_args_t *args);

static dev_link_t *aha152x_attach(void);
static void aha152x_detach(dev_link_t *);

static dev_link_t *dev_list;
static dev_info_t dev_info = "aha152x_cs";

static dev_link_t *aha152x_attach(void)
{
    scsi_info_t *info;
    client_reg_t client_reg;
    dev_link_t *link;
    int ret;
    
    DEBUG(0, "aha152x_attach()\n");

    /* Create new SCSI device */
    info = kmalloc(sizeof(*info), GFP_KERNEL);
    if (!info) return NULL;
    memset(info, 0, sizeof(*info));
    link = &info->link; link->priv = info;

    link->io.NumPorts1 = 0x20;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 10;
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;
    link->conf.Present = PRESENT_OPTION;

    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = pcmcia_register_client(&link->handle, &client_reg);
    if (ret != 0) {
	cs_error(link->handle, RegisterClient, ret);
	aha152x_detach(link);
	return NULL;
    }
    
    return link;
} /* aha152x_attach */

/*====================================================================*/

static void aha152x_detach(dev_link_t *link)
{
    dev_link_t **linkp;

    DEBUG(0, "aha152x_detach(0x%p)\n", link);
    
    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
	if (*linkp == link) break;
    if (*linkp == NULL)
	return;

    if (link->state & DEV_CONFIG)
	aha152x_release_cs(link);

    if (link->handle)
	pcmcia_deregister_client(link->handle);
    
    /* Unlink device structure, free bits */
    *linkp = link->next;
    kfree(link->priv);
    
} /* aha152x_detach */

/*====================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void aha152x_config_cs(dev_link_t *link)
{
    client_handle_t handle = link->handle;
    scsi_info_t *info = link->priv;
    struct aha152x_setup s;
    tuple_t tuple;
    cisparse_t parse;
    int i, last_ret, last_fn;
    u_char tuple_data[64];
    struct Scsi_Host *host;
    
    DEBUG(0, "aha152x_config(0x%p)\n", link);

    tuple.DesiredTuple = CISTPL_CONFIG;
    tuple.TupleData = tuple_data;
    tuple.TupleDataMax = 64;
    tuple.TupleOffset = 0;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
    CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
    link->conf.ConfigBase = parse.config.base;

    /* Configure card */
    link->state |= DEV_CONFIG;

    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    while (1) {
	if (pcmcia_get_tuple_data(handle, &tuple) != 0 ||
		pcmcia_parse_tuple(handle, &tuple, &parse) != 0)
	    goto next_entry;
	/* For New Media T&J, look for a SCSI window */
	if (parse.cftable_entry.io.win[0].len >= 0x20)
	    link->io.BasePort1 = parse.cftable_entry.io.win[0].base;
	else if ((parse.cftable_entry.io.nwin > 1) &&
		 (parse.cftable_entry.io.win[1].len >= 0x20))
	    link->io.BasePort1 = parse.cftable_entry.io.win[1].base;
	if ((parse.cftable_entry.io.nwin > 0) &&
	    (link->io.BasePort1 < 0xffff)) {
	    link->conf.ConfigIndex = parse.cftable_entry.index;
	    i = pcmcia_request_io(handle, &link->io);
	    if (i == CS_SUCCESS) break;
	}
    next_entry:
	CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(handle, &tuple));
    }
    
    CS_CHECK(RequestIRQ, pcmcia_request_irq(handle, &link->irq));
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(handle, &link->conf));
    
    /* Set configuration options for the aha152x driver */
    memset(&s, 0, sizeof(s));
    s.conf        = "PCMCIA setup";
    s.io_port     = link->io.BasePort1;
    s.irq         = link->irq.AssignedIRQ;
    s.scsiid      = host_id;
    s.reconnect   = reconnect;
    s.parity      = parity;
    s.synchronous = synchronous;
    s.delay       = reset_delay;
    if (ext_trans)
        s.ext_trans = ext_trans;

    host = aha152x_probe_one(&s);
    if (host == NULL) {
	printk(KERN_INFO "aha152x_cs: no SCSI devices found\n");
	goto cs_failed;
    }

    sprintf(info->node.dev_name, "scsi%d", host->host_no);
    link->dev = &info->node;
    info->host = host;

    link->state &= ~DEV_CONFIG_PENDING;
    return;
    
cs_failed:
    cs_error(link->handle, last_fn, last_ret);
    aha152x_release_cs(link);
    return;
}

static void aha152x_release_cs(dev_link_t *link)
{
	scsi_info_t *info = link->priv;

	aha152x_release(info->host);
	link->dev = NULL;
    
	pcmcia_release_configuration(link->handle);
	pcmcia_release_io(link->handle, &link->io);
	pcmcia_release_irq(link->handle, &link->irq);
    
	link->state &= ~DEV_CONFIG;
}

static int aha152x_event(event_t event, int priority,
			 event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;
    scsi_info_t *info = link->priv;
    
    DEBUG(0, "aha152x_event(0x%06x)\n", event);
    
    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
	link->state &= ~DEV_PRESENT;
	if (link->state & DEV_CONFIG)
	    aha152x_release_cs(link);
	break;
    case CS_EVENT_CARD_INSERTION:
	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	aha152x_config_cs(link);
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
	if (link->state & DEV_CONFIG) {
	    Scsi_Cmnd tmp;
	    pcmcia_request_configuration(link->handle, &link->conf);
	    tmp.device->host = info->host;
	    aha152x_host_reset(&tmp);
	}
	break;
    }
    return 0;
}

static struct pcmcia_device_id aha152x_ids[] = {
	PCMCIA_DEVICE_PROD_ID123("New Media", "SCSI", "Bus Toaster", 0xcdf7e4cc, 0x35f26476, 0xa8851d6e),
	PCMCIA_DEVICE_PROD_ID123("NOTEWORTHY", "SCSI", "Bus Toaster", 0xad89c6e8, 0x35f26476, 0xa8851d6e),
	PCMCIA_DEVICE_PROD_ID12("Adaptec, Inc.", "APA-1460 SCSI Host Adapter", 0x24ba9738, 0x3a3c3d20),
	PCMCIA_DEVICE_PROD_ID12("New Media Corporation", "Multimedia Sound/SCSI", 0x085a850b, 0x80a6535c),
	PCMCIA_DEVICE_PROD_ID12("NOTEWORTHY", "NWCOMB02 SCSI/AUDIO COMBO CARD", 0xad89c6e8, 0x5f9a615b),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, aha152x_ids);

static struct pcmcia_driver aha152x_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "aha152x_cs",
	},
	.attach		= aha152x_attach,
	.event		= aha152x_event,
	.detach		= aha152x_detach,
	.id_table       = aha152x_ids,
};

static int __init init_aha152x_cs(void)
{
	return pcmcia_register_driver(&aha152x_cs_driver);
}

static void __exit exit_aha152x_cs(void)
{
	pcmcia_unregister_driver(&aha152x_cs_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_aha152x_cs);
module_exit(exit_aha152x_cs);
 
