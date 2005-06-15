/*======================================================================

    A driver for PCMCIA serial devices

    serial_cs.c 1.134 2002/05/04 05:48:53

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
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/serial_core.h>
#include <linux/major.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include "8250.h"

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0644);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version = "serial_cs.c 1.134 2002/05/04 05:48:53 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

/* Enable the speaker? */
static int do_sound = 1;
/* Skip strict UART tests? */
static int buggy_uart;

module_param(do_sound, int, 0444);
module_param(buggy_uart, int, 0444);

/*====================================================================*/

/* Table of multi-port card ID's */

struct multi_id {
	u_short manfid;
	u_short prodid;
	int multi;		/* 1 = multifunction, > 1 = # ports */
};

static struct multi_id multi_id[] = {
	{ MANFID_OMEGA,   PRODID_OMEGA_QSP_100,         4 },
	{ MANFID_QUATECH, PRODID_QUATECH_DUAL_RS232,    2 },
	{ MANFID_QUATECH, PRODID_QUATECH_DUAL_RS232_D1, 2 },
	{ MANFID_QUATECH, PRODID_QUATECH_QUAD_RS232,    4 },
	{ MANFID_SOCKET,  PRODID_SOCKET_DUAL_RS232,     2 },
	{ MANFID_INTEL,   PRODID_INTEL_DUAL_RS232,      2 },
	{ MANFID_NATINST, PRODID_NATINST_QUAD_RS232,    4 }
};
#define MULTI_COUNT (sizeof(multi_id)/sizeof(struct multi_id))

struct serial_info {
	dev_link_t		link;
	int			ndev;
	int			multi;
	int			slave;
	int			manfid;
	dev_node_t		node[4];
	int			line[4];
};

struct serial_cfg_mem {
	tuple_t tuple;
	cisparse_t parse;
	u_char buf[256];
};


static void serial_config(dev_link_t * link);
static int serial_event(event_t event, int priority,
			event_callback_args_t * args);

static dev_info_t dev_info = "serial_cs";

static dev_link_t *serial_attach(void);
static void serial_detach(dev_link_t *);

static dev_link_t *dev_list = NULL;

/*======================================================================

    After a card is removed, serial_remove() will unregister
    the serial device(s), and release the PCMCIA configuration.
    
======================================================================*/

static void serial_remove(dev_link_t *link)
{
	struct serial_info *info = link->priv;
	int i;

	link->state &= ~DEV_PRESENT;

	DEBUG(0, "serial_release(0x%p)\n", link);

	/*
	 * Recheck to see if the device is still configured.
	 */
	if (info->link.state & DEV_CONFIG) {
		for (i = 0; i < info->ndev; i++)
			serial8250_unregister_port(info->line[i]);

		info->link.dev = NULL;

		if (!info->slave) {
			pcmcia_release_configuration(info->link.handle);
			pcmcia_release_io(info->link.handle, &info->link.io);
			pcmcia_release_irq(info->link.handle, &info->link.irq);
		}

		info->link.state &= ~DEV_CONFIG;
	}
}

static void serial_suspend(dev_link_t *link)
{
	link->state |= DEV_SUSPEND;

	if (link->state & DEV_CONFIG) {
		struct serial_info *info = link->priv;
		int i;

		for (i = 0; i < info->ndev; i++)
			serial8250_suspend_port(info->line[i]);

		if (!info->slave)
			pcmcia_release_configuration(link->handle);
	}
}

static void serial_resume(dev_link_t *link)
{
	link->state &= ~DEV_SUSPEND;

	if (DEV_OK(link)) {
		struct serial_info *info = link->priv;
		int i;

		if (!info->slave)
			pcmcia_request_configuration(link->handle, &link->conf);

		for (i = 0; i < info->ndev; i++)
			serial8250_resume_port(info->line[i]);
	}
}

/*======================================================================

    serial_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

======================================================================*/

static dev_link_t *serial_attach(void)
{
	struct serial_info *info;
	client_reg_t client_reg;
	dev_link_t *link;
	int ret;

	DEBUG(0, "serial_attach()\n");

	/* Create new serial device */
	info = kmalloc(sizeof (*info), GFP_KERNEL);
	if (!info)
		return NULL;
	memset(info, 0, sizeof (*info));
	link = &info->link;
	link->priv = info;

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.NumPorts1 = 8;
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->conf.Attributes = CONF_ENABLE_IRQ;
	if (do_sound) {
		link->conf.Attributes |= CONF_ENABLE_SPKR;
		link->conf.Status = CCSR_AUDIO_ENA;
	}
	link->conf.IntType = INT_MEMORY_AND_IO;

	/* Register with Card Services */
	link->next = dev_list;
	dev_list = link;
	client_reg.dev_info = &dev_info;
	client_reg.EventMask =
	    CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	    CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	    CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = &serial_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;
	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		serial_detach(link);
		return NULL;
	}

	return link;
}

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void serial_detach(dev_link_t * link)
{
	struct serial_info *info = link->priv;
	dev_link_t **linkp;
	int ret;

	DEBUG(0, "serial_detach(0x%p)\n", link);

	/* Locate device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
		if (*linkp == link)
			break;
	if (*linkp == NULL)
		return;

	/*
	 * Ensure any outstanding scheduled tasks are completed.
	 */
	flush_scheduled_work();

	/*
	 * Ensure that the ports have been released.
	 */
	serial_remove(link);

	if (link->handle) {
		ret = pcmcia_deregister_client(link->handle);
		if (ret != CS_SUCCESS)
			cs_error(link->handle, DeregisterClient, ret);
	}

	/* Unlink device structure, free bits */
	*linkp = link->next;
	kfree(info);
}

/*====================================================================*/

static int setup_serial(client_handle_t handle, struct serial_info * info,
			kio_addr_t iobase, int irq)
{
	struct uart_port port;
	int line;

	memset(&port, 0, sizeof (struct uart_port));
	port.iobase = iobase;
	port.irq = irq;
	port.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_SHARE_IRQ;
	port.uartclk = 1843200;
	port.dev = &handle_to_dev(handle);
	if (buggy_uart)
		port.flags |= UPF_BUGGY_UART;
	line = serial8250_register_port(&port);
	if (line < 0) {
		printk(KERN_NOTICE "serial_cs: serial8250_register_port() at "
		       "0x%04lx, irq %d failed\n", (u_long)iobase, irq);
		return -EINVAL;
	}

	info->line[info->ndev] = line;
	sprintf(info->node[info->ndev].dev_name, "ttyS%d", line);
	info->node[info->ndev].major = TTY_MAJOR;
	info->node[info->ndev].minor = 0x40 + line;
	if (info->ndev > 0)
		info->node[info->ndev - 1].next = &info->node[info->ndev];
	info->ndev++;

	return 0;
}

/*====================================================================*/

static int
first_tuple(client_handle_t handle, tuple_t * tuple, cisparse_t * parse)
{
	int i;
	i = pcmcia_get_first_tuple(handle, tuple);
	if (i != CS_SUCCESS)
		return CS_NO_MORE_ITEMS;
	i = pcmcia_get_tuple_data(handle, tuple);
	if (i != CS_SUCCESS)
		return i;
	return pcmcia_parse_tuple(handle, tuple, parse);
}

static int
next_tuple(client_handle_t handle, tuple_t * tuple, cisparse_t * parse)
{
	int i;
	i = pcmcia_get_next_tuple(handle, tuple);
	if (i != CS_SUCCESS)
		return CS_NO_MORE_ITEMS;
	i = pcmcia_get_tuple_data(handle, tuple);
	if (i != CS_SUCCESS)
		return i;
	return pcmcia_parse_tuple(handle, tuple, parse);
}

/*====================================================================*/

static int simple_config(dev_link_t *link)
{
	static kio_addr_t base[5] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8, 0x0 };
	static int size_table[2] = { 8, 16 };
	client_handle_t handle = link->handle;
	struct serial_info *info = link->priv;
	struct serial_cfg_mem *cfg_mem;
	tuple_t *tuple;
	u_char *buf;
	cisparse_t *parse;
	cistpl_cftable_entry_t *cf;
	config_info_t config;
	int i, j, try;
	int s;

	cfg_mem = kmalloc(sizeof(struct serial_cfg_mem), GFP_KERNEL);
	if (!cfg_mem)
		return -1;

	tuple = &cfg_mem->tuple;
	parse = &cfg_mem->parse;
	cf = &parse->cftable_entry;
	buf = cfg_mem->buf;

	/* If the card is already configured, look up the port and irq */
	i = pcmcia_get_configuration_info(handle, &config);
	if ((i == CS_SUCCESS) && (config.Attributes & CONF_VALID_CLIENT)) {
		kio_addr_t port = 0;
		if ((config.BasePort2 != 0) && (config.NumPorts2 == 8)) {
			port = config.BasePort2;
			info->slave = 1;
		} else if ((info->manfid == MANFID_OSITECH) &&
			   (config.NumPorts1 == 0x40)) {
			port = config.BasePort1 + 0x28;
			info->slave = 1;
		}
		if (info->slave) {
			kfree(cfg_mem);
			return setup_serial(handle, info, port, config.AssignedIRQ);
		}
	}
	link->conf.Vcc = config.Vcc;

	/* First pass: look for a config entry that looks normal. */
	tuple->TupleData = (cisdata_t *) buf;
	tuple->TupleOffset = 0;
	tuple->TupleDataMax = 255;
	tuple->Attributes = 0;
	tuple->DesiredTuple = CISTPL_CFTABLE_ENTRY;
	/* Two tries: without IO aliases, then with aliases */
	for (s = 0; s < 2; s++) {
		for (try = 0; try < 2; try++) {
			i = first_tuple(handle, tuple, parse);
			while (i != CS_NO_MORE_ITEMS) {
				if (i != CS_SUCCESS)
					goto next_entry;
				if (cf->vpp1.present & (1 << CISTPL_POWER_VNOM))
					link->conf.Vpp1 = link->conf.Vpp2 =
					    cf->vpp1.param[CISTPL_POWER_VNOM] / 10000;
				if ((cf->io.nwin > 0) && (cf->io.win[0].len == size_table[s]) &&
					    (cf->io.win[0].base != 0)) {
					link->conf.ConfigIndex = cf->index;
					link->io.BasePort1 = cf->io.win[0].base;
					link->io.IOAddrLines = (try == 0) ?
					    16 : cf->io.flags & CISTPL_IO_LINES_MASK;
					i = pcmcia_request_io(link->handle, &link->io);
					if (i == CS_SUCCESS)
						goto found_port;
				}
next_entry:
				i = next_tuple(handle, tuple, parse);
			}
		}
	}
	/* Second pass: try to find an entry that isn't picky about
	   its base address, then try to grab any standard serial port
	   address, and finally try to get any free port. */
	i = first_tuple(handle, tuple, parse);
	while (i != CS_NO_MORE_ITEMS) {
		if ((i == CS_SUCCESS) && (cf->io.nwin > 0) &&
		    ((cf->io.flags & CISTPL_IO_LINES_MASK) <= 3)) {
			link->conf.ConfigIndex = cf->index;
			for (j = 0; j < 5; j++) {
				link->io.BasePort1 = base[j];
				link->io.IOAddrLines = base[j] ? 16 : 3;
				i = pcmcia_request_io(link->handle, &link->io);
				if (i == CS_SUCCESS)
					goto found_port;
			}
		}
		i = next_tuple(handle, tuple, parse);
	}

      found_port:
	if (i != CS_SUCCESS) {
		printk(KERN_NOTICE
		       "serial_cs: no usable port range found, giving up\n");
		cs_error(link->handle, RequestIO, i);
		kfree(cfg_mem);
		return -1;
	}

	i = pcmcia_request_irq(link->handle, &link->irq);
	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestIRQ, i);
		link->irq.AssignedIRQ = 0;
	}
	if (info->multi && (info->manfid == MANFID_3COM))
		link->conf.ConfigIndex &= ~(0x08);
	i = pcmcia_request_configuration(link->handle, &link->conf);
	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestConfiguration, i);
		kfree(cfg_mem);
		return -1;
	}
	kfree(cfg_mem);
	return setup_serial(handle, info, link->io.BasePort1, link->irq.AssignedIRQ);
}

static int multi_config(dev_link_t * link)
{
	client_handle_t handle = link->handle;
	struct serial_info *info = link->priv;
	struct serial_cfg_mem *cfg_mem;
	tuple_t *tuple;
	u_char *buf;
	cisparse_t *parse;
	cistpl_cftable_entry_t *cf;
	config_info_t config;
	int i, rc, base2 = 0;

	cfg_mem = kmalloc(sizeof(struct serial_cfg_mem), GFP_KERNEL);
	if (!cfg_mem)
		return -1;
	tuple = &cfg_mem->tuple;
	parse = &cfg_mem->parse;
	cf = &parse->cftable_entry;
	buf = cfg_mem->buf;

	i = pcmcia_get_configuration_info(handle, &config);
	if (i != CS_SUCCESS) {
		cs_error(handle, GetConfigurationInfo, i);
		rc = -1;
		goto free_cfg_mem;
	}
	link->conf.Vcc = config.Vcc;

	tuple->TupleData = (cisdata_t *) buf;
	tuple->TupleOffset = 0;
	tuple->TupleDataMax = 255;
	tuple->Attributes = 0;
	tuple->DesiredTuple = CISTPL_CFTABLE_ENTRY;

	/* First, look for a generic full-sized window */
	link->io.NumPorts1 = info->multi * 8;
	i = first_tuple(handle, tuple, parse);
	while (i != CS_NO_MORE_ITEMS) {
		/* The quad port cards have bad CIS's, so just look for a
		   window larger than 8 ports and assume it will be right */
		if ((i == CS_SUCCESS) && (cf->io.nwin == 1) &&
		    (cf->io.win[0].len > 8)) {
			link->conf.ConfigIndex = cf->index;
			link->io.BasePort1 = cf->io.win[0].base;
			link->io.IOAddrLines =
			    cf->io.flags & CISTPL_IO_LINES_MASK;
			i = pcmcia_request_io(link->handle, &link->io);
			base2 = link->io.BasePort1 + 8;
			if (i == CS_SUCCESS)
				break;
		}
		i = next_tuple(handle, tuple, parse);
	}

	/* If that didn't work, look for two windows */
	if (i != CS_SUCCESS) {
		link->io.NumPorts1 = link->io.NumPorts2 = 8;
		info->multi = 2;
		i = first_tuple(handle, tuple, parse);
		while (i != CS_NO_MORE_ITEMS) {
			if ((i == CS_SUCCESS) && (cf->io.nwin == 2)) {
				link->conf.ConfigIndex = cf->index;
				link->io.BasePort1 = cf->io.win[0].base;
				link->io.BasePort2 = cf->io.win[1].base;
				link->io.IOAddrLines =
				    cf->io.flags & CISTPL_IO_LINES_MASK;
				i = pcmcia_request_io(link->handle, &link->io);
				base2 = link->io.BasePort2;
				if (i == CS_SUCCESS)
					break;
			}
			i = next_tuple(handle, tuple, parse);
		}
	}

	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestIO, i);
		rc = -1;
		goto free_cfg_mem;
	}

	i = pcmcia_request_irq(link->handle, &link->irq);
	if (i != CS_SUCCESS) {
		printk(KERN_NOTICE
		       "serial_cs: no usable port range found, giving up\n");
		cs_error(link->handle, RequestIRQ, i);
		link->irq.AssignedIRQ = 0;
	}
	/* Socket Dual IO: this enables irq's for second port */
	if (info->multi && (info->manfid == MANFID_SOCKET)) {
		link->conf.Present |= PRESENT_EXT_STATUS;
		link->conf.ExtStatus = ESR_REQ_ATTN_ENA;
	}
	i = pcmcia_request_configuration(link->handle, &link->conf);
	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestConfiguration, i);
		rc = -1;
		goto free_cfg_mem;
	}

	/* The Oxford Semiconductor OXCF950 cards are in fact single-port:
	   8 registers are for the UART, the others are extra registers */
	if (info->manfid == MANFID_OXSEMI) {
		if (cf->index == 1 || cf->index == 3) {
			setup_serial(handle, info, base2, link->irq.AssignedIRQ);
			outb(12, link->io.BasePort1 + 1);
		} else {
			setup_serial(handle, info, link->io.BasePort1, link->irq.AssignedIRQ);
			outb(12, base2 + 1);
		}
		rc = 0;
		goto free_cfg_mem;
	}

	setup_serial(handle, info, link->io.BasePort1, link->irq.AssignedIRQ);
	/* The Nokia cards are not really multiport cards */
	if (info->manfid == MANFID_NOKIA) {
		rc = 0;
		goto free_cfg_mem;
	}
	for (i = 0; i < info->multi - 1; i++)
		setup_serial(handle, info, base2 + (8 * i),
				link->irq.AssignedIRQ);
	rc = 0;
free_cfg_mem:
	kfree(cfg_mem);
	return rc;
}

/*======================================================================

    serial_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    serial device available to the system.

======================================================================*/

void serial_config(dev_link_t * link)
{
	client_handle_t handle = link->handle;
	struct serial_info *info = link->priv;
	struct serial_cfg_mem *cfg_mem;
	tuple_t *tuple;
	u_char *buf;
	cisparse_t *parse;
	cistpl_cftable_entry_t *cf;
	int i, last_ret, last_fn;

	DEBUG(0, "serial_config(0x%p)\n", link);

	cfg_mem = kmalloc(sizeof(struct serial_cfg_mem), GFP_KERNEL);
	if (!cfg_mem)
		goto failed;

	tuple = &cfg_mem->tuple;
	parse = &cfg_mem->parse;
	cf = &parse->cftable_entry;
	buf = cfg_mem->buf;

	tuple->TupleData = (cisdata_t *) buf;
	tuple->TupleOffset = 0;
	tuple->TupleDataMax = 255;
	tuple->Attributes = 0;
	/* Get configuration register information */
	tuple->DesiredTuple = CISTPL_CONFIG;
	last_ret = first_tuple(handle, tuple, parse);
	if (last_ret != CS_SUCCESS) {
		last_fn = ParseTuple;
		goto cs_failed;
	}
	link->conf.ConfigBase = parse->config.base;
	link->conf.Present = parse->config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	/* Is this a compliant multifunction card? */
	tuple->DesiredTuple = CISTPL_LONGLINK_MFC;
	tuple->Attributes = TUPLE_RETURN_COMMON | TUPLE_RETURN_LINK;
	info->multi = (first_tuple(handle, tuple, parse) == CS_SUCCESS);

	/* Is this a multiport card? */
	tuple->DesiredTuple = CISTPL_MANFID;
	if (first_tuple(handle, tuple, parse) == CS_SUCCESS) {
		info->manfid = parse->manfid.manf;
		for (i = 0; i < MULTI_COUNT; i++)
			if ((info->manfid == multi_id[i].manfid) &&
			    (parse->manfid.card == multi_id[i].prodid))
				break;
		if (i < MULTI_COUNT)
			info->multi = multi_id[i].multi;
	}

	/* Another check for dual-serial cards: look for either serial or
	   multifunction cards that ask for appropriate IO port ranges */
	tuple->DesiredTuple = CISTPL_FUNCID;
	if ((info->multi == 0) &&
	    ((first_tuple(handle, tuple, parse) != CS_SUCCESS) ||
	     (parse->funcid.func == CISTPL_FUNCID_MULTI) ||
	     (parse->funcid.func == CISTPL_FUNCID_SERIAL))) {
		tuple->DesiredTuple = CISTPL_CFTABLE_ENTRY;
		if (first_tuple(handle, tuple, parse) == CS_SUCCESS) {
			if ((cf->io.nwin == 1) && (cf->io.win[0].len % 8 == 0))
				info->multi = cf->io.win[0].len >> 3;
			if ((cf->io.nwin == 2) && (cf->io.win[0].len == 8) &&
			    (cf->io.win[1].len == 8))
				info->multi = 2;
		}
	}

	if (info->multi > 1)
		multi_config(link);
	else
		simple_config(link);

	if (info->ndev == 0)
		goto failed;

	if (info->manfid == MANFID_IBM) {
		conf_reg_t reg = { 0, CS_READ, 0x800, 0 };
		last_ret = pcmcia_access_configuration_register(link->handle, &reg);
		if (last_ret) {
			last_fn = AccessConfigurationRegister;
			goto cs_failed;
		}
		reg.Action = CS_WRITE;
		reg.Value = reg.Value | 1;
		last_ret = pcmcia_access_configuration_register(link->handle, &reg);
		if (last_ret) {
			last_fn = AccessConfigurationRegister;
			goto cs_failed;
		}
	}

	link->dev = &info->node[0];
	link->state &= ~DEV_CONFIG_PENDING;
	kfree(cfg_mem);
	return;

 cs_failed:
	cs_error(link->handle, last_fn, last_ret);
 failed:
	serial_remove(link);
	link->state &= ~DEV_CONFIG_PENDING;
	kfree(cfg_mem);
}

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.  A CARD_REMOVAL event
    also sets some flags to discourage the serial drivers from
    talking to the ports.
    
======================================================================*/

static int
serial_event(event_t event, int priority, event_callback_args_t * args)
{
	dev_link_t *link = args->client_data;
	struct serial_info *info = link->priv;

	DEBUG(1, "serial_event(0x%06x)\n", event);

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		serial_remove(link);
		break;

	case CS_EVENT_CARD_INSERTION:
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		serial_config(link);
		break;

	case CS_EVENT_PM_SUSPEND:
		serial_suspend(link);
		break;

	case CS_EVENT_RESET_PHYSICAL:
		if ((link->state & DEV_CONFIG) && !info->slave)
			pcmcia_release_configuration(link->handle);
		break;

	case CS_EVENT_PM_RESUME:
		serial_resume(link);
		break;

	case CS_EVENT_CARD_RESET:
		if (DEV_OK(link) && !info->slave)
			pcmcia_request_configuration(link->handle, &link->conf);
		break;
	}
	return 0;
}

static struct pcmcia_driver serial_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "serial_cs",
	},
	.attach		= serial_attach,
	.detach		= serial_detach,
};

static int __init init_serial_cs(void)
{
	return pcmcia_register_driver(&serial_cs_driver);
}

static void __exit exit_serial_cs(void)
{
	pcmcia_unregister_driver(&serial_cs_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_serial_cs);
module_exit(exit_serial_cs);

MODULE_LICENSE("GPL");
