/*** -*- linux-c -*- **********************************************************

     Driver for Atmel at76c502 at76c504 and at76c506 wireless cards.

        Copyright 2000-2001 ATMEL Corporation.
        Copyright 2003 Simon Kelley.

    This code was developed from version 2.1.1 of the Atmel drivers,
    released by Atmel corp. under the GPL in December 2002. It also
    includes code from the Linux aironet drivers (C) Benjamin Reed,
    and the Linux PCMCIA package, (C) David Hinds.

    For all queries about this code, please contact the current author,
    Simon Kelley <simon@thekelleys.org.uk> and not Atmel Corporation.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Atmel wireless lan drivers; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

******************************************************************************/

#ifdef __IN_PCMCIA_PACKAGE__
#include <pcmcia/k_compat.h>
#endif
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/ciscode.h>

#include <asm/io.h>
#include <asm/system.h>
#include <linux/wireless.h>

#include "atmel.h"

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
static char *version = "$Revision: 1.2 $";
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args);
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

MODULE_AUTHOR("Simon Kelley");
MODULE_DESCRIPTION("Support for Atmel at76c50x 802.11 wireless ethernet cards.");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("Atmel at76c50x PCMCIA cards");

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card
   insertion and ejection events.  They are invoked from the atmel_cs
   event handler.
*/

static int atmel_config(struct pcmcia_device *link);
static void atmel_release(struct pcmcia_device *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void atmel_detach(struct pcmcia_device *p_dev);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'pcmem_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

/*
   A linked list of "instances" of the  atmelnet device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one struct pcmcia_device structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of struct pcmcia_device pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

/*
   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a struct pcmcia_device
   structure.  We allocate them in the card's private data structure,
   because they generally shouldn't be allocated dynamically.

   In this case, we also provide a flag to indicate if a device is
   "stopped" due to a power management event, or card ejection.  The
   device IO routines can use a flag like this to throttle IO to a
   card that is not ready to accept it.
*/

typedef struct local_info_t {
	dev_node_t	node;
	struct net_device *eth_dev;
} local_info_t;

/*======================================================================

  atmel_attach() creates an "instance" of the driver, allocating
  local data structures for one device.  The device is registered
  with Card Services.

  The dev_link structure is initialized, but we don't actually
  configure the card at this point -- we wait until we receive a
  card insertion event.

  ======================================================================*/

static int atmel_probe(struct pcmcia_device *p_dev)
{
	local_info_t *local;

	DEBUG(0, "atmel_attach()\n");

	/* Interrupt setup */
	p_dev->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;
	p_dev->irq.IRQInfo1 = IRQ_LEVEL_ID;
	p_dev->irq.Handler = NULL;

	/*
	  General socket configuration defaults can go here.  In this
	  client, we assume very little, and rely on the CIS for almost
	  everything.  In most clients, many details (i.e., number, sizes,
	  and attributes of IO windows) are fixed by the nature of the
	  device, and can be hard-wired here.
	*/
	p_dev->conf.Attributes = 0;
	p_dev->conf.IntType = INT_MEMORY_AND_IO;

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local) {
		printk(KERN_ERR "atmel_cs: no memory for new device\n");
		return -ENOMEM;
	}
	p_dev->priv = local;

	return atmel_config(p_dev);
} /* atmel_attach */

/*======================================================================

  This deletes a driver "instance".  The device is de-registered
  with Card Services.  If it has been released, all local data
  structures are freed.  Otherwise, the structures will be freed
  when the device is released.

  ======================================================================*/

static void atmel_detach(struct pcmcia_device *link)
{
	DEBUG(0, "atmel_detach(0x%p)\n", link);

	atmel_release(link);

	kfree(link->priv);
}

/*======================================================================

  atmel_config() is scheduled to run after a CARD_INSERTION event
  is received, to configure the PCMCIA socket, and to make the
  device available to the system.

  ======================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

/* Call-back function to interrogate PCMCIA-specific information
   about the current existance of the card */
static int card_present(void *arg)
{
	struct pcmcia_device *link = (struct pcmcia_device *)arg;

	if (pcmcia_dev_present(link))
		return 1;

	return 0;
}

static int atmel_config_check(struct pcmcia_device *p_dev,
			      cistpl_cftable_entry_t *cfg,
			      cistpl_cftable_entry_t *dflt,
			      unsigned int vcc,
			      void *priv_data)
{
	if (cfg->index == 0)
		return -ENODEV;

	/* Does this card need audio output? */
	if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
		p_dev->conf.Attributes |= CONF_ENABLE_SPKR;
		p_dev->conf.Status = CCSR_AUDIO_ENA;
	}

	/* Use power settings for Vcc and Vpp if present */
	/*  Note that the CIS values need to be rescaled */
	if (cfg->vpp1.present & (1<<CISTPL_POWER_VNOM))
		p_dev->conf.Vpp = cfg->vpp1.param[CISTPL_POWER_VNOM]/10000;
	else if (dflt->vpp1.present & (1<<CISTPL_POWER_VNOM))
		p_dev->conf.Vpp = dflt->vpp1.param[CISTPL_POWER_VNOM]/10000;

	/* Do we need to allocate an interrupt? */
	if (cfg->irq.IRQInfo1 || dflt->irq.IRQInfo1)
		p_dev->conf.Attributes |= CONF_ENABLE_IRQ;

	/* IO window settings */
	p_dev->io.NumPorts1 = p_dev->io.NumPorts2 = 0;
	if ((cfg->io.nwin > 0) || (dflt->io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt->io;
		p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
		if (!(io->flags & CISTPL_IO_8BIT))
			p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
		if (!(io->flags & CISTPL_IO_16BIT))
			p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
		p_dev->io.BasePort1 = io->win[0].base;
		p_dev->io.NumPorts1 = io->win[0].len;
		if (io->nwin > 1) {
			p_dev->io.Attributes2 = p_dev->io.Attributes1;
			p_dev->io.BasePort2 = io->win[1].base;
			p_dev->io.NumPorts2 = io->win[1].len;
		}
	}

	/* This reserves IO space but doesn't actually enable it */
	return pcmcia_request_io(p_dev, &p_dev->io);
}

static int atmel_config(struct pcmcia_device *link)
{
	local_info_t *dev;
	int last_fn, last_ret;
	struct pcmcia_device_id *did;

	dev = link->priv;
	did = handle_to_dev(link).driver_data;

	DEBUG(0, "atmel_config(0x%p)\n", link);

	/*
	  In this loop, we scan the CIS for configuration table entries,
	  each of which describes a valid card configuration, including
	  voltage, IO window, memory window, and interrupt settings.

	  We make no assumptions about the card to be configured: we use
	  just the information available in the CIS.  In an ideal world,
	  this would work for any PCMCIA card, but it requires a complete
	  and accurate CIS.  In practice, a driver usually "knows" most of
	  these things without consulting the CIS, and most client drivers
	  will only use the CIS to fill in implementation-defined details.
	*/
	if (pcmcia_loop_config(link, atmel_config_check, NULL))
		goto failed;

	/*
	  Allocate an interrupt line.  Note that this does not assign a
	  handler to the interrupt, unless the 'Handler' member of the
	  irq structure is initialized.
	*/
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));

	/*
	  This actually configures the PCMCIA socket -- setting up
	  the I/O windows and the interrupt mapping, and putting the
	  card and host interface into "Memory and IO" mode.
	*/
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));

	if (link->irq.AssignedIRQ == 0) {
		printk(KERN_ALERT
		       "atmel: cannot assign IRQ: check that CONFIG_ISA is set in kernel config.");
		goto cs_failed;
	}

	((local_info_t*)link->priv)->eth_dev =
		init_atmel_card(link->irq.AssignedIRQ,
				link->io.BasePort1,
				did ? did->driver_info : ATMEL_FW_TYPE_NONE,
				&handle_to_dev(link),
				card_present,
				link);
	if (!((local_info_t*)link->priv)->eth_dev)
			goto cs_failed;


	/*
	  At this point, the dev_node_t structure(s) need to be
	  initialized and arranged in a linked list at link->dev_node.
	*/
	strcpy(dev->node.dev_name, ((local_info_t*)link->priv)->eth_dev->name );
	dev->node.major = dev->node.minor = 0;
	link->dev_node = &dev->node;

	return 0;

 cs_failed:
	cs_error(link, last_fn, last_ret);
 failed:
	atmel_release(link);
	return -ENODEV;
}

/*======================================================================

  After a card is removed, atmel_release() will unregister the
  device, and release the PCMCIA configuration.  If the device is
  still open, this will be postponed until it is closed.

  ======================================================================*/

static void atmel_release(struct pcmcia_device *link)
{
	struct net_device *dev = ((local_info_t*)link->priv)->eth_dev;

	DEBUG(0, "atmel_release(0x%p)\n", link);

	if (dev)
		stop_atmel_card(dev);
	((local_info_t*)link->priv)->eth_dev = NULL;

	pcmcia_disable_device(link);
}

static int atmel_suspend(struct pcmcia_device *link)
{
	local_info_t *local = link->priv;

	netif_device_detach(local->eth_dev);

	return 0;
}

static int atmel_resume(struct pcmcia_device *link)
{
	local_info_t *local = link->priv;

	atmel_open(local->eth_dev);
	netif_device_attach(local->eth_dev);

	return 0;
}

/*====================================================================*/
/* We use the driver_info field to store the correct firmware type for a card. */

#define PCMCIA_DEVICE_MANF_CARD_INFO(manf, card, info) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID, \
	.manf_id = (manf), \
	.card_id = (card), \
        .driver_info = (kernel_ulong_t)(info), }

#define PCMCIA_DEVICE_PROD_ID12_INFO(v1, v2, vh1, vh2, info) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 }, \
        .driver_info = (kernel_ulong_t)(info), }

static struct pcmcia_device_id atmel_ids[] = {
	PCMCIA_DEVICE_MANF_CARD_INFO(0x0101, 0x0620, ATMEL_FW_TYPE_502_3COM),
	PCMCIA_DEVICE_MANF_CARD_INFO(0x0101, 0x0696, ATMEL_FW_TYPE_502_3COM),
	PCMCIA_DEVICE_MANF_CARD_INFO(0x01bf, 0x3302, ATMEL_FW_TYPE_502E),
	PCMCIA_DEVICE_MANF_CARD_INFO(0xd601, 0x0007, ATMEL_FW_TYPE_502),
	PCMCIA_DEVICE_PROD_ID12_INFO("11WAVE", "11WP611AL-E", 0x9eb2da1f, 0xc9a0d3f9, ATMEL_FW_TYPE_502E),
	PCMCIA_DEVICE_PROD_ID12_INFO("ATMEL", "AT76C502AR", 0xabda4164, 0x41b37e1f, ATMEL_FW_TYPE_502),
	PCMCIA_DEVICE_PROD_ID12_INFO("ATMEL", "AT76C502AR_D", 0xabda4164, 0x3675d704, ATMEL_FW_TYPE_502D),
	PCMCIA_DEVICE_PROD_ID12_INFO("ATMEL", "AT76C502AR_E", 0xabda4164, 0x4172e792, ATMEL_FW_TYPE_502E),
	PCMCIA_DEVICE_PROD_ID12_INFO("ATMEL", "AT76C504_R", 0xabda4164, 0x917f3d72, ATMEL_FW_TYPE_504_2958),
	PCMCIA_DEVICE_PROD_ID12_INFO("ATMEL", "AT76C504", 0xabda4164, 0x5040670a, ATMEL_FW_TYPE_504),
	PCMCIA_DEVICE_PROD_ID12_INFO("ATMEL", "AT76C504A", 0xabda4164, 0xe15ed87f, ATMEL_FW_TYPE_504A_2958),
	PCMCIA_DEVICE_PROD_ID12_INFO("BT", "Voyager 1020 Laptop Adapter", 0xae49b86a, 0x1e957cd5, ATMEL_FW_TYPE_502),
	PCMCIA_DEVICE_PROD_ID12_INFO("CNet", "CNWLC 11Mbps Wireless PC Card V-5", 0xbc477dde, 0x502fae6b, ATMEL_FW_TYPE_502E),
	PCMCIA_DEVICE_PROD_ID12_INFO("IEEE 802.11b", "Wireless LAN PC Card", 0x5b878724, 0x122f1df6, ATMEL_FW_TYPE_502),
	PCMCIA_DEVICE_PROD_ID12_INFO("IEEE 802.11b", "Wireless LAN Card S", 0x5b878724, 0x5fba533a, ATMEL_FW_TYPE_504_2958),
	PCMCIA_DEVICE_PROD_ID12_INFO("OEM", "11Mbps Wireless LAN PC Card V-3", 0xfea54c90, 0x1c5b0f68, ATMEL_FW_TYPE_502),
	PCMCIA_DEVICE_PROD_ID12_INFO("SMC", "2632W", 0xc4f8b18b, 0x30f38774, ATMEL_FW_TYPE_502D),
	PCMCIA_DEVICE_PROD_ID12_INFO("SMC", "2632W-V2", 0xc4f8b18b, 0x172d1377, ATMEL_FW_TYPE_502),
	PCMCIA_DEVICE_PROD_ID12_INFO("Wireless", "PC_CARD", 0xa407ecdd, 0x119f6314, ATMEL_FW_TYPE_502D),
	PCMCIA_DEVICE_PROD_ID12_INFO("WLAN", "802.11b PC CARD", 0x575c516c, 0xb1f6dbc4, ATMEL_FW_TYPE_502D),
	PCMCIA_DEVICE_PROD_ID12_INFO("LG", "LW2100N", 0xb474d43a, 0x6b1fec94, ATMEL_FW_TYPE_502E),
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, atmel_ids);

static struct pcmcia_driver atmel_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "atmel_cs",
        },
	.probe          = atmel_probe,
	.remove		= atmel_detach,
	.id_table	= atmel_ids,
	.suspend	= atmel_suspend,
	.resume		= atmel_resume,
};

static int atmel_cs_init(void)
{
        return pcmcia_register_driver(&atmel_driver);
}

static void atmel_cs_cleanup(void)
{
        pcmcia_unregister_driver(&atmel_driver);
}

/*
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    In addition:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. The name of the author may not be used to endorse or promote
       products derived from this software without specific prior written
       permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

module_init(atmel_cs_init);
module_exit(atmel_cs_cleanup);
