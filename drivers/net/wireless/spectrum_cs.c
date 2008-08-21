/*
 * Driver for 802.11b cards using RAM-loadable Symbol firmware, such as
 * Symbol Wireless Networker LA4137, CompactFlash cards by Socket
 * Communications and Intel PRO/Wireless 2011B.
 *
 * The driver implements Symbol firmware download.  The rest is handled
 * in hermes.c and orinoco.c.
 *
 * Utilities for downloading the Symbol firmware are available at
 * http://sourceforge.net/projects/orinoco/
 *
 * Copyright (C) 2002-2005 Pavel Roskin <proski@gnu.org>
 * Portions based on orinoco_cs.c:
 * 	Copyright (C) David Gibson, Linuxcare Australia
 * Portions based on Spectrum24tDnld.c from original spectrum24 driver:
 * 	Copyright (C) Symbol Technologies.
 *
 * See copyright notice in file orinoco.c.
 */

#define DRIVER_NAME "spectrum_cs"
#define PFX DRIVER_NAME ": "

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "orinoco.h"
#include "hermes_dld.h"

static const char primary_fw_name[] = "symbol_sp24t_prim_fw";
static const char secondary_fw_name[] = "symbol_sp24t_sec_fw";

/********************************************************************/
/* Module stuff							    */
/********************************************************************/

MODULE_AUTHOR("Pavel Roskin <proski@gnu.org>");
MODULE_DESCRIPTION("Driver for Symbol Spectrum24 Trilogy cards with firmware downloader");
MODULE_LICENSE("Dual MPL/GPL");

/* Module parameters */

/* Some D-Link cards have buggy CIS. They do work at 5v properly, but
 * don't have any CIS entry for it. This workaround it... */
static int ignore_cis_vcc; /* = 0 */
module_param(ignore_cis_vcc, int, 0);
MODULE_PARM_DESC(ignore_cis_vcc, "Allow voltage mismatch between card and socket");

/********************************************************************/
/* Data structures						    */
/********************************************************************/

/* PCMCIA specific device information (goes in the card field of
 * struct orinoco_private */
struct orinoco_pccard {
	struct pcmcia_device	*p_dev;
	dev_node_t node;
};

/********************************************************************/
/* Function prototypes						    */
/********************************************************************/

static int spectrum_cs_config(struct pcmcia_device *link);
static void spectrum_cs_release(struct pcmcia_device *link);

/********************************************************************/
/* Firmware downloader						    */
/********************************************************************/

/* Position of PDA in the adapter memory */
#define EEPROM_ADDR	0x3000
#define EEPROM_LEN	0x200
#define PDA_OFFSET	0x100

#define PDA_ADDR	(EEPROM_ADDR + PDA_OFFSET)
#define PDA_WORDS	((EEPROM_LEN - PDA_OFFSET) / 2)

/* Constants for the CISREG_CCSR register */
#define HCR_RUN		0x07	/* run firmware after reset */
#define HCR_IDLE	0x0E	/* don't run firmware after reset */
#define HCR_MEM16	0x10	/* memory width bit, should be preserved */

/* End markers */
#define TEXT_END	0x1A		/* End of text header */


#define CS_CHECK(fn, ret) \
  do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

/*
 * Reset the card using configuration registers COR and CCSR.
 * If IDLE is 1, stop the firmware, so that it can be safely rewritten.
 */
static int
spectrum_reset(struct pcmcia_device *link, int idle)
{
	int last_ret, last_fn;
	conf_reg_t reg;
	u_int save_cor;

	/* Doing it if hardware is gone is guaranteed crash */
	if (!pcmcia_dev_present(link))
		return -ENODEV;

	/* Save original COR value */
	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link, &reg));
	save_cor = reg.Value;

	/* Soft-Reset card */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (save_cor | COR_SOFT_RESET);
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link, &reg));
	udelay(1000);

	/* Read CCSR */
	reg.Action = CS_READ;
	reg.Offset = CISREG_CCSR;
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link, &reg));

	/*
	 * Start or stop the firmware.  Memory width bit should be
	 * preserved from the value we've just read.
	 */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_CCSR;
	reg.Value = (idle ? HCR_IDLE : HCR_RUN) | (reg.Value & HCR_MEM16);
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link, &reg));
	udelay(1000);

	/* Restore original COR configuration index */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (save_cor & ~COR_SOFT_RESET);
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link, &reg));
	udelay(1000);
	return 0;

      cs_failed:
	cs_error(link, last_fn, last_ret);
	return -ENODEV;
}


/*
 * Process a firmware image - stop the card, load the firmware, reset
 * the card and make sure it responds.  For the secondary firmware take
 * care of the PDA - read it and then write it on top of the firmware.
 */
static int
spectrum_dl_image(hermes_t *hw, struct pcmcia_device *link,
		  const unsigned char *image, const unsigned char *end,
		  int secondary)
{
	int ret;
	const unsigned char *ptr;
	const unsigned char *first_block;

	/* Plug Data Area (PDA) */
	__le16 pda[PDA_WORDS];

	/* Binary block begins after the 0x1A marker */
	ptr = image;
	while (*ptr++ != TEXT_END);
	first_block = ptr;

	/* Read the PDA from EEPROM */
	if (secondary) {
		ret = hermes_read_pda(hw, pda, PDA_ADDR, sizeof(pda), 1);
		if (ret)
			return ret;
	}

	/* Stop the firmware, so that it can be safely rewritten */
	ret = spectrum_reset(link, 1);
	if (ret)
		return ret;

	/* Program the adapter with new firmware */
	ret = hermes_program(hw, first_block, end);
	if (ret)
		return ret;

	/* Write the PDA to the adapter */
	if (secondary) {
		size_t len = hermes_blocks_length(first_block);
		ptr = first_block + len;
		ret = hermes_apply_pda(hw, ptr, pda);
		if (ret)
			return ret;
	}

	/* Run the firmware */
	ret = spectrum_reset(link, 0);
	if (ret)
		return ret;

	/* Reset hermes chip and make sure it responds */
	ret = hermes_init(hw);

	/* hermes_reset() should return 0 with the secondary firmware */
	if (secondary && ret != 0)
		return -ENODEV;

	/* And this should work with any firmware */
	if (!hermes_present(hw))
		return -ENODEV;

	return 0;
}


/*
 * Download the firmware into the card, this also does a PCMCIA soft
 * reset on the card, to make sure it's in a sane state.
 */
static int
spectrum_dl_firmware(hermes_t *hw, struct pcmcia_device *link)
{
	int ret;
	const struct firmware *fw_entry;

	if (request_firmware(&fw_entry, primary_fw_name,
			     &handle_to_dev(link)) != 0) {
		printk(KERN_ERR PFX "Cannot find firmware: %s\n",
		       primary_fw_name);
		return -ENOENT;
	}

	/* Load primary firmware */
	ret = spectrum_dl_image(hw, link, fw_entry->data,
				fw_entry->data + fw_entry->size, 0);
	release_firmware(fw_entry);
	if (ret) {
		printk(KERN_ERR PFX "Primary firmware download failed\n");
		return ret;
	}

	if (request_firmware(&fw_entry, secondary_fw_name,
			     &handle_to_dev(link)) != 0) {
		printk(KERN_ERR PFX "Cannot find firmware: %s\n",
		       secondary_fw_name);
		return -ENOENT;
	}

	/* Load secondary firmware */
	ret = spectrum_dl_image(hw, link, fw_entry->data,
				fw_entry->data + fw_entry->size, 1);
	release_firmware(fw_entry);
	if (ret) {
		printk(KERN_ERR PFX "Secondary firmware download failed\n");
	}

	return ret;
}

/********************************************************************/
/* Device methods     						    */
/********************************************************************/

static int
spectrum_cs_hard_reset(struct orinoco_private *priv)
{
	struct orinoco_pccard *card = priv->card;
	struct pcmcia_device *link = card->p_dev;
	int err;

	if (!hermes_present(&priv->hw)) {
		/* The firmware needs to be reloaded */
		if (spectrum_dl_firmware(&priv->hw, link) != 0) {
			printk(KERN_ERR PFX "Firmware download failed\n");
			err = -ENODEV;
		}
	} else {
		/* Soft reset using COR and HCR */
		spectrum_reset(link, 0);
	}

	return 0;
}

/********************************************************************/
/* PCMCIA stuff     						    */
/********************************************************************/

/*
 * This creates an "instance" of the driver, allocating local data
 * structures for one device.  The device is registered with Card
 * Services.
 * 
 * The dev_link structure is initialized, but we don't actually
 * configure the card at this point -- we wait until we receive a card
 * insertion event.  */
static int
spectrum_cs_probe(struct pcmcia_device *link)
{
	struct net_device *dev;
	struct orinoco_private *priv;
	struct orinoco_pccard *card;

	dev = alloc_orinocodev(sizeof(*card), spectrum_cs_hard_reset);
	if (! dev)
		return -ENOMEM;
	priv = netdev_priv(dev);
	card = priv->card;

	/* Link both structures together */
	card->p_dev = link;
	link->priv = dev;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->irq.Handler = orinoco_interrupt;
	link->irq.Instance = dev; 

	/* General socket configuration defaults can go here.  In this
	 * client, we assume very little, and rely on the CIS for
	 * almost everything.  In most clients, many details (i.e.,
	 * number, sizes, and attributes of IO windows) are fixed by
	 * the nature of the device, and can be hard-wired here. */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	return spectrum_cs_config(link);
}				/* spectrum_cs_attach */

/*
 * This deletes a driver "instance".  The device is de-registered with
 * Card Services.  If it has been released, all local data structures
 * are freed.  Otherwise, the structures will be freed when the device
 * is released.
 */
static void spectrum_cs_detach(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->dev_node)
		unregister_netdev(dev);

	spectrum_cs_release(link);

	free_orinocodev(dev);
}				/* spectrum_cs_detach */

/*
 * spectrum_cs_config() is scheduled to run after a CARD_INSERTION
 * event is received, to configure the PCMCIA socket, and to make the
 * device available to the system.
 */

static int
spectrum_cs_config(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	struct orinoco_private *priv = netdev_priv(dev);
	struct orinoco_pccard *card = priv->card;
	hermes_t *hw = &priv->hw;
	int last_fn, last_ret;
	u_char buf[64];
	config_info_t conf;
	tuple_t tuple;
	cisparse_t parse;
	void __iomem *mem;

	/* Look up the current Vcc */
	CS_CHECK(GetConfigurationInfo,
		 pcmcia_get_configuration_info(link, &conf));

	/*
	 * In this loop, we scan the CIS for configuration table
	 * entries, each of which describes a valid card
	 * configuration, including voltage, IO window, memory window,
	 * and interrupt settings.
	 *
	 * We make no assumptions about the card to be configured: we
	 * use just the information available in the CIS.  In an ideal
	 * world, this would work for any PCMCIA card, but it requires
	 * a complete and accurate CIS.  In practice, a driver usually
	 * "knows" most of these things without consulting the CIS,
	 * and most client drivers will only use the CIS to fill in
	 * implementation-defined details.
	 */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		cistpl_cftable_entry_t dflt = { .index = 0 };

		if ( (pcmcia_get_tuple_data(link, &tuple) != 0)
		    || (pcmcia_parse_tuple(link, &tuple, &parse) != 0))
			goto next_entry;

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Use power settings for Vcc and Vpp if present */
		/* Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM] / 10000) {
				DEBUG(2, "spectrum_cs_config: Vcc mismatch (conf.Vcc = %d, CIS = %d)\n",  conf.Vcc, cfg->vcc.param[CISTPL_POWER_VNOM] / 10000);
				if (!ignore_cis_vcc)
					goto next_entry;
			}
		} else if (dflt.vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM] / 10000) {
				DEBUG(2, "spectrum_cs_config: Vcc mismatch (conf.Vcc = %d, CIS = %d)\n",  conf.Vcc, dflt.vcc.param[CISTPL_POWER_VNOM] / 10000);
				if(!ignore_cis_vcc)
					goto next_entry;
			}
		}

		if (cfg->vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp =
			    cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
		else if (dflt.vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp =
			    dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;
		
		/* Do we need to allocate an interrupt? */
		link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io =
			    (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 =
				    IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 =
				    IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines =
			    io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 =
				    link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}

			/* This reserves IO space but doesn't actually enable it */
			if (pcmcia_request_io(link, &link->io) != 0)
				goto next_entry;
		}


		/* If we got this far, we're cool! */

		break;
		
	next_entry:
		pcmcia_disable_device(link);
		last_ret = pcmcia_get_next_tuple(link, &tuple);
		if (last_ret  == CS_NO_MORE_ITEMS) {
			printk(KERN_ERR PFX "GetNextTuple(): No matching "
			       "CIS configuration.  Maybe you need the "
			       "ignore_cis_vcc=1 parameter.\n");
			goto cs_failed;
		}
	}

	/*
	 * Allocate an interrupt line.  Note that this does not assign
	 * a handler to the interrupt, unless the 'Handler' member of
	 * the irq structure is initialized.
	 */
	CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));

	/* We initialize the hermes structure before completing PCMCIA
	 * configuration just in case the interrupt handler gets
	 * called. */
	mem = ioport_map(link->io.BasePort1, link->io.NumPorts1);
	if (!mem)
		goto cs_failed;

	hermes_struct_init(hw, mem, HERMES_16BIT_REGSPACING);

	/*
	 * This actually configures the PCMCIA socket -- setting up
	 * the I/O windows and the interrupt mapping, and putting the
	 * card and host interface into "Memory and IO" mode.
	 */
	CS_CHECK(RequestConfiguration,
		 pcmcia_request_configuration(link, &link->conf));

	/* Ok, we have the configuration, prepare to register the netdev */
	dev->base_addr = link->io.BasePort1;
	dev->irq = link->irq.AssignedIRQ;
	card->node.major = card->node.minor = 0;

	/* Reset card and download firmware */
	if (spectrum_cs_hard_reset(priv) != 0) {
		goto failed;
	}

	SET_NETDEV_DEV(dev, &handle_to_dev(link));
	/* Tell the stack we exist */
	if (register_netdev(dev) != 0) {
		printk(KERN_ERR PFX "register_netdev() failed\n");
		goto failed;
	}

	/* At this point, the dev_node_t structure(s) needs to be
	 * initialized and arranged in a linked list at link->dev_node. */
	strcpy(card->node.dev_name, dev->name);
	link->dev_node = &card->node; /* link->dev_node being non-NULL is also
                                    used to indicate that the
                                    net_device has been registered */

	/* Finally, report what we've done */
	printk(KERN_DEBUG "%s: " DRIVER_NAME " at %s, irq %d, io "
	       "0x%04x-0x%04x\n", dev->name, dev->dev.parent->bus_id,
	       link->irq.AssignedIRQ, link->io.BasePort1,
	       link->io.BasePort1 + link->io.NumPorts1 - 1);

	return 0;

 cs_failed:
	cs_error(link, last_fn, last_ret);

 failed:
	spectrum_cs_release(link);
	return -ENODEV;
}				/* spectrum_cs_config */

/*
 * After a card is removed, spectrum_cs_release() will unregister the
 * device, and release the PCMCIA configuration.  If the device is
 * still open, this will be postponed until it is closed.
 */
static void
spectrum_cs_release(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	/* We're committed to taking the device away now, so mark the
	 * hardware as unavailable */
	spin_lock_irqsave(&priv->lock, flags);
	priv->hw_unavailable++;
	spin_unlock_irqrestore(&priv->lock, flags);

	pcmcia_disable_device(link);
	if (priv->hw.iobase)
		ioport_unmap(priv->hw.iobase);
}				/* spectrum_cs_release */


static int
spectrum_cs_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;

	/* Mark the device as stopped, to block IO until later */
	spin_lock(&priv->lock);

	err = __orinoco_down(dev);
	if (err)
		printk(KERN_WARNING "%s: Error %d downing interface\n",
		       dev->name, err);

	netif_device_detach(dev);
	priv->hw_unavailable++;

	spin_unlock(&priv->lock);

	return err;
}

static int
spectrum_cs_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;
	struct orinoco_private *priv = netdev_priv(dev);

	netif_device_attach(dev);
	priv->hw_unavailable--;
	schedule_work(&priv->reset_work);

	return 0;
}


/********************************************************************/
/* Module initialization					    */
/********************************************************************/

/* Can't be declared "const" or the whole __initdata section will
 * become const */
static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Pavel Roskin <proski@gnu.org>,"
	" David Gibson <hermes@gibson.dropbear.id.au>, et al)";

static struct pcmcia_device_id spectrum_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x026c, 0x0001), /* Symbol Spectrum24 LA4137 */
	PCMCIA_DEVICE_MANF_CARD(0x0104, 0x0001), /* Socket Communications CF */
	PCMCIA_DEVICE_PROD_ID12("Intel", "PRO/Wireless LAN PC Card", 0x816cc815, 0x6fbf459a), /* 2011B, not 2011 */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, spectrum_cs_ids);

static struct pcmcia_driver orinoco_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= DRIVER_NAME,
	},
	.probe		= spectrum_cs_probe,
	.remove		= spectrum_cs_detach,
	.suspend	= spectrum_cs_suspend,
	.resume		= spectrum_cs_resume,
	.id_table       = spectrum_cs_ids,
};

static int __init
init_spectrum_cs(void)
{
	printk(KERN_DEBUG "%s\n", version);

	return pcmcia_register_driver(&orinoco_driver);
}

static void __exit
exit_spectrum_cs(void)
{
	pcmcia_unregister_driver(&orinoco_driver);
}

module_init(init_spectrum_cs);
module_exit(exit_spectrum_cs);
