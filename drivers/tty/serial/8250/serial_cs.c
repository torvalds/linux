// SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1)
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
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/serial_core.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <asm/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include "8250.h"


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

struct serial_quirk {
	unsigned int manfid;
	unsigned int prodid;
	int multi;		/* 1 = multifunction, > 1 = # ports */
	void (*config)(struct pcmcia_device *);
	void (*setup)(struct pcmcia_device *, struct uart_8250_port *);
	void (*wakeup)(struct pcmcia_device *);
	int (*post)(struct pcmcia_device *);
};

struct serial_info {
	struct pcmcia_device	*p_dev;
	int			ndev;
	int			multi;
	int			slave;
	int			manfid;
	int			prodid;
	int			c950ctrl;
	int			line[4];
	const struct serial_quirk *quirk;
};

struct serial_cfg_mem {
	tuple_t tuple;
	cisparse_t parse;
	u_char buf[256];
};

/*
 * vers_1 5.0, "Brain Boxes", "2-Port RS232 card", "r6"
 * manfid 0x0160, 0x0104
 * This card appears to have a 14.7456MHz clock.
 */
/* Generic Modem: MD55x (GPRS/EDGE) have
 * Elan VPU16551 UART with 14.7456MHz oscillator
 * manfid 0x015D, 0x4C45
 */
static void quirk_setup_brainboxes_0104(struct pcmcia_device *link, struct uart_8250_port *uart)
{
	uart->port.uartclk = 14745600;
}

static int quirk_post_ibm(struct pcmcia_device *link)
{
	u8 val;
	int ret;

	ret = pcmcia_read_config_byte(link, 0x800, &val);
	if (ret)
		goto failed;

	ret = pcmcia_write_config_byte(link, 0x800, val | 1);
	if (ret)
		goto failed;
	return 0;

 failed:
	return -ENODEV;
}

/*
 * Nokia cards are not really multiport cards.  Shouldn't this
 * be handled by setting the quirk entry .multi = 0 | 1 ?
 */
static void quirk_config_nokia(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;

	if (info->multi > 1)
		info->multi = 1;
}

static void quirk_wakeup_oxsemi(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;

	if (info->c950ctrl)
		outb(12, info->c950ctrl + 1);
}

/* request_region? oxsemi branch does no request_region too... */
/*
 * This sequence is needed to properly initialize MC45 attached to OXCF950.
 * I tried decreasing these msleep()s, but it worked properly (survived
 * 1000 stop/start operations) with these timeouts (or bigger).
 */
static void quirk_wakeup_possio_gcc(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	unsigned int ctrl = info->c950ctrl;

	outb(0xA, ctrl + 1);
	msleep(100);
	outb(0xE, ctrl + 1);
	msleep(300);
	outb(0xC, ctrl + 1);
	msleep(100);
	outb(0xE, ctrl + 1);
	msleep(200);
	outb(0xF, ctrl + 1);
	msleep(100);
	outb(0xE, ctrl + 1);
	msleep(100);
	outb(0xC, ctrl + 1);
}

/*
 * Socket Dual IO: this enables irq's for second port
 */
static void quirk_config_socket(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;

	if (info->multi)
		link->config_flags |= CONF_ENABLE_ESR;
}

static const struct serial_quirk quirks[] = {
	{
		.manfid	= 0x0160,
		.prodid	= 0x0104,
		.multi	= -1,
		.setup	= quirk_setup_brainboxes_0104,
	}, {
		.manfid	= 0x015D,
		.prodid	= 0x4C45,
		.multi	= -1,
		.setup	= quirk_setup_brainboxes_0104,
	}, {
		.manfid	= MANFID_IBM,
		.prodid	= ~0,
		.multi	= -1,
		.post	= quirk_post_ibm,
	}, {
		.manfid	= MANFID_INTEL,
		.prodid	= PRODID_INTEL_DUAL_RS232,
		.multi	= 2,
	}, {
		.manfid	= MANFID_NATINST,
		.prodid	= PRODID_NATINST_QUAD_RS232,
		.multi	= 4,
	}, {
		.manfid	= MANFID_NOKIA,
		.prodid	= ~0,
		.multi	= -1,
		.config	= quirk_config_nokia,
	}, {
		.manfid	= MANFID_OMEGA,
		.prodid	= PRODID_OMEGA_QSP_100,
		.multi	= 4,
	}, {
		.manfid	= MANFID_OXSEMI,
		.prodid	= ~0,
		.multi	= -1,
		.wakeup	= quirk_wakeup_oxsemi,
	}, {
		.manfid	= MANFID_POSSIO,
		.prodid	= PRODID_POSSIO_GCC,
		.multi	= -1,
		.wakeup	= quirk_wakeup_possio_gcc,
	}, {
		.manfid	= MANFID_QUATECH,
		.prodid	= PRODID_QUATECH_DUAL_RS232,
		.multi	= 2,
	}, {
		.manfid	= MANFID_QUATECH,
		.prodid	= PRODID_QUATECH_DUAL_RS232_D1,
		.multi	= 2,
	}, {
		.manfid	= MANFID_QUATECH,
		.prodid	= PRODID_QUATECH_DUAL_RS232_G,
		.multi	= 2,
	}, {
		.manfid	= MANFID_QUATECH,
		.prodid	= PRODID_QUATECH_QUAD_RS232,
		.multi	= 4,
	}, {
		.manfid	= MANFID_SOCKET,
		.prodid	= PRODID_SOCKET_DUAL_RS232,
		.multi	= 2,
		.config	= quirk_config_socket,
	}, {
		.manfid	= MANFID_SOCKET,
		.prodid	= ~0,
		.multi	= -1,
		.config	= quirk_config_socket,
	}
};


static int serial_config(struct pcmcia_device *link);


static void serial_remove(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	int i;

	dev_dbg(&link->dev, "serial_release\n");

	/*
	 * Recheck to see if the device is still configured.
	 */
	for (i = 0; i < info->ndev; i++)
		serial8250_unregister_port(info->line[i]);

	if (!info->slave)
		pcmcia_disable_device(link);
}

static int serial_suspend(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	int i;

	for (i = 0; i < info->ndev; i++)
		serial8250_suspend_port(info->line[i]);

	return 0;
}

static int serial_resume(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	int i;

	for (i = 0; i < info->ndev; i++)
		serial8250_resume_port(info->line[i]);

	if (info->quirk && info->quirk->wakeup)
		info->quirk->wakeup(link);

	return 0;
}

static int serial_probe(struct pcmcia_device *link)
{
	struct serial_info *info;

	dev_dbg(&link->dev, "serial_attach()\n");

	/* Create new serial device */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->p_dev = link;
	link->priv = info;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;
	if (do_sound)
		link->config_flags |= CONF_ENABLE_SPKR;

	return serial_config(link);
}

static void serial_detach(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;

	dev_dbg(&link->dev, "serial_detach\n");

	/*
	 * Ensure that the ports have been released.
	 */
	serial_remove(link);

	/* free bits */
	kfree(info);
}

/*====================================================================*/

static int setup_serial(struct pcmcia_device *handle, struct serial_info *info,
			unsigned int iobase, int irq)
{
	struct uart_8250_port uart;
	int line;

	memset(&uart, 0, sizeof(uart));
	uart.port.iobase = iobase;
	uart.port.irq = irq;
	uart.port.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_SHARE_IRQ;
	uart.port.uartclk = 1843200;
	uart.port.dev = &handle->dev;
	if (buggy_uart)
		uart.port.flags |= UPF_BUGGY_UART;

	if (info->quirk && info->quirk->setup)
		info->quirk->setup(handle, &uart);

	line = serial8250_register_8250_port(&uart);
	if (line < 0) {
		pr_err("serial_cs: serial8250_register_8250_port() at 0x%04lx, irq %d failed\n",
							(unsigned long)iobase, irq);
		return -EINVAL;
	}

	info->line[info->ndev] = line;
	info->ndev++;

	return 0;
}

/*====================================================================*/

static int pfc_config(struct pcmcia_device *p_dev)
{
	unsigned int port = 0;
	struct serial_info *info = p_dev->priv;

	if ((p_dev->resource[1]->end != 0) &&
		(resource_size(p_dev->resource[1]) == 8)) {
		port = p_dev->resource[1]->start;
		info->slave = 1;
	} else if ((info->manfid == MANFID_OSITECH) &&
		(resource_size(p_dev->resource[0]) == 0x40)) {
		port = p_dev->resource[0]->start + 0x28;
		info->slave = 1;
	}
	if (info->slave)
		return setup_serial(p_dev, info, port, p_dev->irq);

	dev_warn(&p_dev->dev, "no usable port range found, giving up\n");
	return -ENODEV;
}

static int simple_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	static const int size_table[2] = { 8, 16 };
	int *try = priv_data;

	if (p_dev->resource[0]->start == 0)
		return -ENODEV;

	if ((*try & 0x1) == 0)
		p_dev->io_lines = 16;

	if (p_dev->resource[0]->end != size_table[(*try >> 1)])
		return -ENODEV;

	p_dev->resource[0]->end = 8;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;

	return pcmcia_request_io(p_dev);
}

static int simple_config_check_notpicky(struct pcmcia_device *p_dev,
					void *priv_data)
{
	static const unsigned int base[5] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8, 0x0 };
	int j;

	if (p_dev->io_lines > 3)
		return -ENODEV;

	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->resource[0]->end = 8;

	for (j = 0; j < 5; j++) {
		p_dev->resource[0]->start = base[j];
		p_dev->io_lines = base[j] ? 16 : 3;
		if (!pcmcia_request_io(p_dev))
			return 0;
	}
	return -ENODEV;
}

static int simple_config(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	int i = -ENODEV, try;

	/*
	 * First pass: look for a config entry that looks normal.
	 * Two tries: without IO aliases, then with aliases.
	 */
	link->config_flags |= CONF_AUTO_SET_VPP;
	for (try = 0; try < 4; try++)
		if (!pcmcia_loop_config(link, simple_config_check, &try))
			goto found_port;

	/*
	 * Second pass: try to find an entry that isn't picky about
	 * its base address, then try to grab any standard serial port
	 * address, and finally try to get any free port.
	 */
	if (!pcmcia_loop_config(link, simple_config_check_notpicky, NULL))
		goto found_port;

	dev_warn(&link->dev, "no usable port range found, giving up\n");
	return -1;

found_port:
	if (info->multi && (info->manfid == MANFID_3COM))
		link->config_index &= ~(0x08);

	/*
	 * Apply any configuration quirks.
	 */
	if (info->quirk && info->quirk->config)
		info->quirk->config(link);

	i = pcmcia_enable_device(link);
	if (i != 0)
		return -1;
	return setup_serial(link, info, link->resource[0]->start, link->irq);
}

static int multi_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	int *multi = priv_data;

	if (p_dev->resource[1]->end)
		return -EINVAL;

	/*
	 * The quad port cards have bad CIS's, so just look for a
	 * window larger than 8 ports and assume it will be right.
	 */
	if (p_dev->resource[0]->end <= 8)
		return -EINVAL;

	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;
	p_dev->resource[0]->end = *multi * 8;

	if (pcmcia_request_io(p_dev))
		return -ENODEV;
	return 0;
}

static int multi_config_check_notpicky(struct pcmcia_device *p_dev,
				       void *priv_data)
{
	int *base2 = priv_data;

	if (!p_dev->resource[0]->end || !p_dev->resource[1]->end ||
		p_dev->resource[0]->start + 8 != p_dev->resource[1]->start)
		return -ENODEV;

	p_dev->resource[0]->end = p_dev->resource[1]->end = 8;
	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;

	if (pcmcia_request_io(p_dev))
		return -ENODEV;

	*base2 = p_dev->resource[0]->start + 8;
	return 0;
}

static int multi_config(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	int i, base2 = 0;

	/* First, look for a generic full-sized window */
	if (!pcmcia_loop_config(link, multi_config_check, &info->multi))
		base2 = link->resource[0]->start + 8;
	else {
		/* If that didn't work, look for two windows */
		info->multi = 2;
		if (pcmcia_loop_config(link, multi_config_check_notpicky,
				       &base2)) {
			dev_warn(&link->dev,
				 "no usable port range found, giving up\n");
			return -ENODEV;
		}
	}

	if (!link->irq)
		dev_warn(&link->dev, "no usable IRQ found, continuing...\n");

	/*
	 * Apply any configuration quirks.
	 */
	if (info->quirk && info->quirk->config)
		info->quirk->config(link);

	i = pcmcia_enable_device(link);
	if (i != 0)
		return -ENODEV;

	/* The Oxford Semiconductor OXCF950 cards are in fact single-port:
	 * 8 registers are for the UART, the others are extra registers.
	 * Siemen's MC45 PCMCIA (Possio's GCC) is OXCF950 based too.
	 */
	if (info->manfid == MANFID_OXSEMI || (info->manfid == MANFID_POSSIO &&
				info->prodid == PRODID_POSSIO_GCC)) {
		int err;

		if (link->config_index == 1 ||
		    link->config_index == 3) {
			err = setup_serial(link, info, base2,
					link->irq);
			base2 = link->resource[0]->start;
		} else {
			err = setup_serial(link, info, link->resource[0]->start,
					link->irq);
		}
		info->c950ctrl = base2;

		/*
		 * FIXME: We really should wake up the port prior to
		 * handing it over to the serial layer.
		 */
		if (info->quirk && info->quirk->wakeup)
			info->quirk->wakeup(link);

		return 0;
	}

	setup_serial(link, info, link->resource[0]->start, link->irq);
	for (i = 0; i < info->multi - 1; i++)
		setup_serial(link, info, base2 + (8 * i),
				link->irq);
	return 0;
}

static int serial_check_for_multi(struct pcmcia_device *p_dev,  void *priv_data)
{
	struct serial_info *info = p_dev->priv;

	if (!p_dev->resource[0]->end)
		return -EINVAL;

	if ((!p_dev->resource[1]->end) && (p_dev->resource[0]->end % 8 == 0))
		info->multi = p_dev->resource[0]->end >> 3;

	if ((p_dev->resource[1]->end) && (p_dev->resource[0]->end == 8)
		&& (p_dev->resource[1]->end == 8))
		info->multi = 2;

	return 0; /* break */
}


static int serial_config(struct pcmcia_device *link)
{
	struct serial_info *info = link->priv;
	int i;

	dev_dbg(&link->dev, "serial_config\n");

	/* Is this a compliant multifunction card? */
	info->multi = (link->socket->functions > 1);

	/* Is this a multiport card? */
	info->manfid = link->manf_id;
	info->prodid = link->card_id;

	for (i = 0; i < ARRAY_SIZE(quirks); i++)
		if ((quirks[i].manfid == ~0 ||
		     quirks[i].manfid == info->manfid) &&
		    (quirks[i].prodid == ~0 ||
		     quirks[i].prodid == info->prodid)) {
			info->quirk = &quirks[i];
			break;
		}

	/*
	 * Another check for dual-serial cards: look for either serial or
	 * multifunction cards that ask for appropriate IO port ranges.
	 */
	if ((info->multi == 0) &&
	    (link->has_func_id) &&
	    (link->socket->pcmcia_pfc == 0) &&
	    ((link->func_id == CISTPL_FUNCID_MULTI) ||
	     (link->func_id == CISTPL_FUNCID_SERIAL)))
		pcmcia_loop_config(link, serial_check_for_multi, info);

	/*
	 * Apply any multi-port quirk.
	 */
	if (info->quirk && info->quirk->multi != -1)
		info->multi = info->quirk->multi;

	dev_info(&link->dev,
		"trying to set up [0x%04x:0x%04x] (pfc: %d, multi: %d, quirk: %p)\n",
		link->manf_id, link->card_id,
		link->socket->pcmcia_pfc, info->multi, info->quirk);
	if (link->socket->pcmcia_pfc)
		i = pfc_config(link);
	else if (info->multi > 1)
		i = multi_config(link);
	else
		i = simple_config(link);

	if (i || info->ndev == 0)
		goto failed;

	/*
	 * Apply any post-init quirk.  FIXME: This should really happen
	 * before we register the port, since it might already be in use.
	 */
	if (info->quirk && info->quirk->post)
		if (info->quirk->post(link))
			goto failed;

	return 0;

failed:
	dev_warn(&link->dev, "failed to initialize\n");
	serial_remove(link);
	return -ENODEV;
}

static const struct pcmcia_device_id serial_ids[] = {
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0057, 0x0021),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0089, 0x110a),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0104, 0x000a),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0105, 0x0d0a),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0105, 0x0e0a),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0105, 0xea15),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0109, 0x0501),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0138, 0x110a),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0140, 0x000a),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0143, 0x3341),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0143, 0xc0ab),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x016c, 0x0081),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x021b, 0x0101),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x08a1, 0xc0ab),
	PCMCIA_PFC_DEVICE_PROD_ID123(1, "MEGAHERTZ", "CC/XJEM3288", "DATA/FAX/CELL ETHERNET MODEM", 0xf510db04, 0x04cd2988, 0x46a52d63),
	PCMCIA_PFC_DEVICE_PROD_ID123(1, "MEGAHERTZ", "CC/XJEM3336", "DATA/FAX/CELL ETHERNET MODEM", 0xf510db04, 0x0143b773, 0x46a52d63),
	PCMCIA_PFC_DEVICE_PROD_ID123(1, "MEGAHERTZ", "EM1144T", "PCMCIA MODEM", 0xf510db04, 0x856d66c8, 0xbd6c43ef),
	PCMCIA_PFC_DEVICE_PROD_ID123(1, "MEGAHERTZ", "XJEM1144/CCEM1144", "PCMCIA MODEM", 0xf510db04, 0x52d21e1e, 0xbd6c43ef),
	PCMCIA_PFC_DEVICE_PROD_ID13(1, "Xircom", "CEM28", 0x2e3ee845, 0x0ea978ea),
	PCMCIA_PFC_DEVICE_PROD_ID13(1, "Xircom", "CEM33", 0x2e3ee845, 0x80609023),
	PCMCIA_PFC_DEVICE_PROD_ID13(1, "Xircom", "CEM56", 0x2e3ee845, 0xa650c32a),
	PCMCIA_PFC_DEVICE_PROD_ID13(1, "Xircom", "REM10", 0x2e3ee845, 0x76df1d29),
	PCMCIA_PFC_DEVICE_PROD_ID13(1, "Xircom", "XEM5600", 0x2e3ee845, 0xf1403719),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "AnyCom", "Fast Ethernet + 56K COMBO", 0x578ba6e7, 0xb0ac62c4),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "ATKK", "LM33-PCM-T", 0xba9eb7e2, 0x077c174e),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "D-Link", "DME336T", 0x1a424a1c, 0xb23897ff),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "Gateway 2000", "XJEM3336", 0xdd9989be, 0x662c394c),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "Grey Cell", "GCS3000", 0x2a151fac, 0x48b932ae),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "Linksys", "EtherFast 10&100 + 56K PC Card (PCMLM56)", 0x0733cc81, 0xb3765033),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "LINKSYS", "PCMLM336", 0xf7cb0b07, 0x7a821b58),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "MEGAHERTZ", "XJEM1144/CCEM1144", 0xf510db04, 0x52d21e1e),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "MICRO RESEARCH", "COMBO-L/M-336", 0xb2ced065, 0x3ced0555),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "NEC", "PK-UG-J001", 0x18df0ba0, 0x831b1064),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "Ositech", "Trumpcard:Jack of Diamonds Modem+Ethernet", 0xc2f80cd, 0x656947b9),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "Ositech", "Trumpcard:Jack of Hearts Modem+Ethernet", 0xc2f80cd, 0xdc9ba5ed),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "PCMCIAs", "ComboCard", 0xdcfe12d3, 0xcd8906cc),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "PCMCIAs", "LanModem", 0xdcfe12d3, 0xc67c648f),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "TDK", "GlobalNetworker 3410/3412", 0x1eae9475, 0xd9a93bed),
	PCMCIA_PFC_DEVICE_PROD_ID12(1, "Xircom", "CreditCard Ethernet+Modem II", 0x2e3ee845, 0xeca401bf),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0032, 0x0e01),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0032, 0x0a05),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0032, 0x0b05),
	PCMCIA_PFC_DEVICE_MANF_CARD(1, 0x0032, 0x1101),
	PCMCIA_MFC_DEVICE_MANF_CARD(0, 0x0104, 0x0070),
	PCMCIA_MFC_DEVICE_MANF_CARD(1, 0x0101, 0x0562),
	PCMCIA_MFC_DEVICE_MANF_CARD(1, 0x0104, 0x0070),
	PCMCIA_MFC_DEVICE_MANF_CARD(1, 0x016c, 0x0020),
	PCMCIA_MFC_DEVICE_PROD_ID123(1, "APEX DATA", "MULTICARD", "ETHERNET-MODEM", 0x11c2da09, 0x7289dc5d, 0xaad95e1f),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "IBM", "Home and Away 28.8 PC Card       ", 0xb569a6e5, 0x5bd4ff2c),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "IBM", "Home and Away Credit Card Adapter", 0xb569a6e5, 0x4bdf15c3),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "IBM", "w95 Home and Away Credit Card ", 0xb569a6e5, 0xae911c15),
	PCMCIA_MFC_DEVICE_PROD_ID1(1, "Motorola MARQUIS", 0xf03e4e77),
	PCMCIA_MFC_DEVICE_PROD_ID2(1, "FAX/Modem/Ethernet Combo Card ", 0x1ed59302),
	PCMCIA_DEVICE_MANF_CARD(0x0089, 0x0301),
	PCMCIA_DEVICE_MANF_CARD(0x00a4, 0x0276),
	PCMCIA_DEVICE_MANF_CARD(0x0101, 0x0039),
	PCMCIA_DEVICE_MANF_CARD(0x0104, 0x0006),
	PCMCIA_DEVICE_MANF_CARD(0x0105, 0x0101), /* TDK DF2814 */
	PCMCIA_DEVICE_MANF_CARD(0x0105, 0x100a), /* Xircom CM-56G */
	PCMCIA_DEVICE_MANF_CARD(0x0105, 0x3e0a), /* TDK DF5660 */
	PCMCIA_DEVICE_MANF_CARD(0x0105, 0x410a),
	PCMCIA_DEVICE_MANF_CARD(0x0107, 0x0002), /* USRobotics 14,400 */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0d50),
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0d51),
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0d52),
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x0d53),
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0xd180),
	PCMCIA_DEVICE_MANF_CARD(0x0115, 0x3330), /* USRobotics/SUN 14,400 */
	PCMCIA_DEVICE_MANF_CARD(0x0124, 0x0100), /* Nokia DTP-2 ver II */
	PCMCIA_DEVICE_MANF_CARD(0x0134, 0x5600), /* LASAT COMMUNICATIONS A/S */
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x000e),
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x001b),
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0025),
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0045),
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0052),
	PCMCIA_DEVICE_MANF_CARD(0x016c, 0x0006), /* Psion 56K+Fax */
	PCMCIA_DEVICE_MANF_CARD(0x0200, 0x0001), /* MultiMobile */
	PCMCIA_DEVICE_PROD_ID134("ADV", "TECH", "COMpad-32/85", 0x67459937, 0x916d02ba, 0x8fbe92ae),
	PCMCIA_DEVICE_PROD_ID124("GATEWAY2000", "CC3144", "PCMCIA MODEM", 0x506bccae, 0xcb3685f1, 0xbd6c43ef),
	PCMCIA_DEVICE_PROD_ID14("MEGAHERTZ", "PCMCIA MODEM", 0xf510db04, 0xbd6c43ef),
	PCMCIA_DEVICE_PROD_ID124("TOSHIBA", "T144PF", "PCMCIA MODEM", 0xb4585a1a, 0x7271409c, 0xbd6c43ef),
	PCMCIA_DEVICE_PROD_ID123("FUJITSU", "FC14F ", "MBH10213", 0x6ee5a3d8, 0x30ead12b, 0xb00f05a0),
	PCMCIA_DEVICE_PROD_ID123("Novatel Wireless", "Merlin UMTS Modem", "U630", 0x32607776, 0xd9e73b13, 0xe87332e),
	PCMCIA_DEVICE_PROD_ID13("MEGAHERTZ", "V.34 PCMCIA MODEM", 0xf510db04, 0xbb2cce4a),
	PCMCIA_DEVICE_PROD_ID12("Brain Boxes", "Bluetooth PC Card", 0xee138382, 0xd4ce9b02),
	PCMCIA_DEVICE_PROD_ID12("CIRRUS LOGIC", "FAX MODEM", 0xe625f451, 0xcecd6dfa),
	PCMCIA_DEVICE_PROD_ID12("COMPAQ", "PCMCIA 28800 FAX/DATA MODEM", 0xa3a3062c, 0x8cbd7c76),
	PCMCIA_DEVICE_PROD_ID12("COMPAQ", "PCMCIA 33600 FAX/DATA MODEM", 0xa3a3062c, 0x5a00ce95),
	PCMCIA_DEVICE_PROD_ID12("Computerboards, Inc.", "PCM-COM422", 0xd0b78f51, 0x7e2d49ed),
	PCMCIA_DEVICE_PROD_ID12("Dr. Neuhaus", "FURY CARD 14K4", 0x76942813, 0x8b96ce65),
	PCMCIA_DEVICE_PROD_ID12("IBM", "ISDN/56K/GSM", 0xb569a6e5, 0xfee5297b),
	PCMCIA_DEVICE_PROD_ID12("Intelligent", "ANGIA FAX/MODEM", 0xb496e65e, 0xf31602a6),
	PCMCIA_DEVICE_PROD_ID12("Intel", "MODEM 2400+", 0x816cc815, 0x412729fb),
	PCMCIA_DEVICE_PROD_ID12("Intertex", "IX34-PCMCIA", 0xf8a097e3, 0x97880447),
	PCMCIA_DEVICE_PROD_ID12("IOTech Inc ", "PCMCIA Dual RS-232 Serial Port Card", 0x3bd2d898, 0x92abc92f),
	PCMCIA_DEVICE_PROD_ID12("MACRONIX", "FAX/MODEM", 0x668388b3, 0x3f9bdf2f),
	PCMCIA_DEVICE_PROD_ID12("Multi-Tech", "MT1432LT", 0x5f73be51, 0x0b3e2383),
	PCMCIA_DEVICE_PROD_ID12("Multi-Tech", "MT2834LT", 0x5f73be51, 0x4cd7c09e),
	PCMCIA_DEVICE_PROD_ID12("OEM      ", "C288MX     ", 0xb572d360, 0xd2385b7a),
	PCMCIA_DEVICE_PROD_ID12("Option International", "V34bis GSM/PSTN Data/Fax Modem", 0x9d7cd6f5, 0x5cb8bf41),
	PCMCIA_DEVICE_PROD_ID12("PCMCIA   ", "C336MX     ", 0x99bcafe9, 0xaa25bcab),
	PCMCIA_DEVICE_PROD_ID12("Quatech Inc", "PCMCIA Dual RS-232 Serial Port Card", 0xc4420b35, 0x92abc92f),
	PCMCIA_DEVICE_PROD_ID12("Quatech Inc", "Dual RS-232 Serial Port PC Card", 0xc4420b35, 0x031a380d),
	PCMCIA_DEVICE_PROD_ID12("Telia", "SurfinBird 560P/A+", 0xe2cdd5e, 0xc9314b38),
	PCMCIA_DEVICE_PROD_ID1("Smart Serial Port", 0x2d8ce292),
	PCMCIA_PFC_DEVICE_CIS_PROD_ID12(1, "PCMCIA", "EN2218-LAN/MODEM", 0x281f1c5d, 0x570f348e, "cis/PCMLM28.cis"),
	PCMCIA_PFC_DEVICE_CIS_PROD_ID12(1, "PCMCIA", "UE2218-LAN/MODEM", 0x281f1c5d, 0x6fdcacee, "cis/PCMLM28.cis"),
	PCMCIA_PFC_DEVICE_CIS_PROD_ID12(1, "Psion Dacom", "Gold Card V34 Ethernet", 0xf5f025c2, 0x338e8155, "cis/PCMLM28.cis"),
	PCMCIA_PFC_DEVICE_CIS_PROD_ID12(1, "Psion Dacom", "Gold Card V34 Ethernet GSM", 0xf5f025c2, 0x4ae85d35, "cis/PCMLM28.cis"),
	PCMCIA_PFC_DEVICE_CIS_PROD_ID12(1, "LINKSYS", "PCMLM28", 0xf7cb0b07, 0x66881874, "cis/PCMLM28.cis"),
	PCMCIA_PFC_DEVICE_CIS_PROD_ID12(1, "TOSHIBA", "Modem/LAN Card", 0xb4585a1a, 0x53f922f8, "cis/PCMLM28.cis"),
	PCMCIA_MFC_DEVICE_CIS_PROD_ID12(1, "DAYNA COMMUNICATIONS", "LAN AND MODEM MULTIFUNCTION", 0x8fdf8f89, 0xdd5ed9e8, "cis/DP83903.cis"),
	PCMCIA_MFC_DEVICE_CIS_PROD_ID4(1, "NSC MF LAN/Modem", 0x58fc6056, "cis/DP83903.cis"),
	PCMCIA_MFC_DEVICE_CIS_MANF_CARD(1, 0x0101, 0x0556, "cis/3CCFEM556.cis"),
	PCMCIA_MFC_DEVICE_CIS_MANF_CARD(1, 0x0175, 0x0000, "cis/DP83903.cis"),
	PCMCIA_MFC_DEVICE_CIS_MANF_CARD(1, 0x0101, 0x0035, "cis/3CXEM556.cis"),
	PCMCIA_MFC_DEVICE_CIS_MANF_CARD(1, 0x0101, 0x003d, "cis/3CXEM556.cis"),
	PCMCIA_DEVICE_CIS_PROD_ID12("Sierra Wireless", "AC850", 0xd85f6206, 0x42a2c018, "cis/SW_8xx_SER.cis"), /* Sierra Wireless AC850 3G Network Adapter R1 */
	PCMCIA_DEVICE_CIS_PROD_ID12("Sierra Wireless", "AC860", 0xd85f6206, 0x698f93db, "cis/SW_8xx_SER.cis"), /* Sierra Wireless AC860 3G Network Adapter R1 */
	PCMCIA_DEVICE_CIS_PROD_ID12("Sierra Wireless", "AC710/AC750", 0xd85f6206, 0x761b11e0, "cis/SW_7xx_SER.cis"),  /* Sierra Wireless AC710/AC750 GPRS Network Adapter R1 */
	PCMCIA_DEVICE_CIS_MANF_CARD(0x0192, 0xa555, "cis/SW_555_SER.cis"),  /* Sierra Aircard 555 CDMA 1xrtt Modem -- pre update */
	PCMCIA_DEVICE_CIS_MANF_CARD(0x013f, 0xa555, "cis/SW_555_SER.cis"),  /* Sierra Aircard 555 CDMA 1xrtt Modem -- post update */
	PCMCIA_DEVICE_CIS_PROD_ID12("MultiTech", "PCMCIA 56K DataFax", 0x842047ee, 0xc2efcf03, "cis/MT5634ZLX.cis"),
	PCMCIA_DEVICE_CIS_PROD_ID12("ADVANTECH", "COMpad-32/85B-2", 0x96913a85, 0x27ab5437, "cis/COMpad2.cis"),
	PCMCIA_DEVICE_CIS_PROD_ID12("ADVANTECH", "COMpad-32/85B-4", 0x96913a85, 0xcec8f102, "cis/COMpad4.cis"),
	PCMCIA_DEVICE_CIS_PROD_ID123("ADVANTECH", "COMpad-32/85", "1.0", 0x96913a85, 0x8fbe92ae, 0x0877b627, "cis/COMpad2.cis"),
	PCMCIA_DEVICE_CIS_PROD_ID2("RS-COM 2P", 0xad20b156, "cis/RS-COM-2P.cis"),
	PCMCIA_DEVICE_CIS_MANF_CARD(0x0013, 0x0000, "cis/GLOBETROTTER.cis"),
	PCMCIA_DEVICE_PROD_ID12("ELAN DIGITAL SYSTEMS LTD, c1997.", "SERIAL CARD: SL100  1.00.", 0x19ca78af, 0xf964f42b),
	PCMCIA_DEVICE_PROD_ID12("ELAN DIGITAL SYSTEMS LTD, c1997.", "SERIAL CARD: SL100", 0x19ca78af, 0x71d98e83),
	PCMCIA_DEVICE_PROD_ID12("ELAN DIGITAL SYSTEMS LTD, c1997.", "SERIAL CARD: SL232  1.00.", 0x19ca78af, 0x69fb7490),
	PCMCIA_DEVICE_PROD_ID12("ELAN DIGITAL SYSTEMS LTD, c1997.", "SERIAL CARD: SL232", 0x19ca78af, 0xb6bc0235),
	PCMCIA_DEVICE_PROD_ID12("ELAN DIGITAL SYSTEMS LTD, c2000.", "SERIAL CARD: CF232", 0x63f2e0bd, 0xb9e175d3),
	PCMCIA_DEVICE_PROD_ID12("ELAN DIGITAL SYSTEMS LTD, c2000.", "SERIAL CARD: CF232-5", 0x63f2e0bd, 0xfce33442),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: CF232", 0x3beb8cf2, 0x171e7190),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: CF232-5", 0x3beb8cf2, 0x20da4262),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: CF428", 0x3beb8cf2, 0xea5dd57d),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: CF500", 0x3beb8cf2, 0xd77255fa),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: IC232", 0x3beb8cf2, 0x6a709903),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: SL232", 0x3beb8cf2, 0x18430676),
	PCMCIA_DEVICE_PROD_ID12("Elan", "Serial Port: XL232", 0x3beb8cf2, 0x6f933767),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "Elan", "Serial Port: CF332", 0x3beb8cf2, 0x16dc1ba7),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "Elan", "Serial Port: SL332", 0x3beb8cf2, 0x19816c41),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "Elan", "Serial Port: SL385", 0x3beb8cf2, 0x64112029),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "Elan", "Serial Port: SL432", 0x3beb8cf2, 0x1cce7ac4),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "Elan", "Serial+Parallel Port: SP230", 0x3beb8cf2, 0xdb9e58bc),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "Elan", "Serial Port: CF332", 0x3beb8cf2, 0x16dc1ba7),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "Elan", "Serial Port: SL332", 0x3beb8cf2, 0x19816c41),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "Elan", "Serial Port: SL385", 0x3beb8cf2, 0x64112029),
	PCMCIA_MFC_DEVICE_PROD_ID12(1, "Elan", "Serial Port: SL432", 0x3beb8cf2, 0x1cce7ac4),
	PCMCIA_MFC_DEVICE_PROD_ID12(2, "Elan", "Serial Port: SL432", 0x3beb8cf2, 0x1cce7ac4),
	PCMCIA_MFC_DEVICE_PROD_ID12(3, "Elan", "Serial Port: SL432", 0x3beb8cf2, 0x1cce7ac4),
	PCMCIA_DEVICE_MANF_CARD(0x0279, 0x950b),
	/* too generic */
	/* PCMCIA_MFC_DEVICE_MANF_CARD(0, 0x0160, 0x0002), */
	/* PCMCIA_MFC_DEVICE_MANF_CARD(1, 0x0160, 0x0002), */
	PCMCIA_DEVICE_FUNC_ID(2),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, serial_ids);

MODULE_FIRMWARE("cis/PCMLM28.cis");
MODULE_FIRMWARE("cis/DP83903.cis");
MODULE_FIRMWARE("cis/3CCFEM556.cis");
MODULE_FIRMWARE("cis/3CXEM556.cis");
MODULE_FIRMWARE("cis/SW_8xx_SER.cis");
MODULE_FIRMWARE("cis/SW_7xx_SER.cis");
MODULE_FIRMWARE("cis/SW_555_SER.cis");
MODULE_FIRMWARE("cis/MT5634ZLX.cis");
MODULE_FIRMWARE("cis/COMpad2.cis");
MODULE_FIRMWARE("cis/COMpad4.cis");
MODULE_FIRMWARE("cis/RS-COM-2P.cis");

static struct pcmcia_driver serial_cs_driver = {
	.owner		= THIS_MODULE,
	.name		= "serial_cs",
	.probe		= serial_probe,
	.remove		= serial_detach,
	.id_table	= serial_ids,
	.suspend	= serial_suspend,
	.resume		= serial_resume,
};
module_pcmcia_driver(serial_cs_driver);

MODULE_LICENSE("GPL");
