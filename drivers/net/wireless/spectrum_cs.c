/*
 * Driver for 802.11b cards using RAM-loadable Symbol firmware, such as
 * Symbol Wireless Networker LA4100, CompactFlash cards by Socket
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

#include <linux/config.h>
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

static unsigned char *primsym;
static unsigned char *secsym;
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
/* Magic constants						    */
/********************************************************************/

/*
 * The dev_info variable is the "key" that is used to match up this
 * device driver with appropriate cards, through the card
 * configuration database.
 */
static dev_info_t dev_info = DRIVER_NAME;

/********************************************************************/
/* Data structures						    */
/********************************************************************/

/* PCMCIA specific device information (goes in the card field of
 * struct orinoco_private */
struct orinoco_pccard {
	dev_link_t link;
	dev_node_t node;
};

/*
 * A linked list of "instances" of the device.  Each actual PCMCIA
 * card corresponds to one device instance, and is described by one
 * dev_link_t structure (defined in ds.h).
 */
static dev_link_t *dev_list; /* = NULL */

/********************************************************************/
/* Function prototypes						    */
/********************************************************************/

static void spectrum_cs_release(dev_link_t *link);
static void spectrum_cs_detach(dev_link_t *link);

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

/*
 * AUX port access.  To unlock the AUX port write the access keys to the
 * PARAM0-2 registers, then write HERMES_AUX_ENABLE to the HERMES_CONTROL
 * register.  Then read it and make sure it's HERMES_AUX_ENABLED.
 */
#define HERMES_AUX_ENABLE	0x8000	/* Enable auxiliary port access */
#define HERMES_AUX_DISABLE	0x4000	/* Disable to auxiliary port access */
#define HERMES_AUX_ENABLED	0xC000	/* Auxiliary port is open */

#define HERMES_AUX_PW0	0xFE01
#define HERMES_AUX_PW1	0xDC23
#define HERMES_AUX_PW2	0xBA45

/* End markers */
#define PDI_END		0x00000000	/* End of PDA */
#define BLOCK_END	0xFFFFFFFF	/* Last image block */
#define TEXT_END	0x1A		/* End of text header */

/*
 * The following structures have little-endian fields denoted by
 * the leading underscore.  Don't access them directly - use inline
 * functions defined below.
 */

/*
 * The binary image to be downloaded consists of series of data blocks.
 * Each block has the following structure.
 */
struct dblock {
	__le32 _addr;		/* adapter address where to write the block */
	__le16 _len;		/* length of the data only, in bytes */
	char data[0];		/* data to be written */
} __attribute__ ((packed));

/*
 * Plug Data References are located in in the image after the last data
 * block.  They refer to areas in the adapter memory where the plug data
 * items with matching ID should be written.
 */
struct pdr {
	__le32 _id;		/* record ID */
	__le32 _addr;		/* adapter address where to write the data */
	__le32 _len;		/* expected length of the data, in bytes */
	char next[0];		/* next PDR starts here */
} __attribute__ ((packed));


/*
 * Plug Data Items are located in the EEPROM read from the adapter by
 * primary firmware.  They refer to the device-specific data that should
 * be plugged into the secondary firmware.
 */
struct pdi {
	__le16 _len;		/* length of ID and data, in words */
	__le16 _id;		/* record ID */
	char data[0];		/* plug data */
} __attribute__ ((packed));;


/* Functions for access to little-endian data */
static inline u32
dblock_addr(const struct dblock *blk)
{
	return le32_to_cpu(blk->_addr);
}

static inline u32
dblock_len(const struct dblock *blk)
{
	return le16_to_cpu(blk->_len);
}

static inline u32
pdr_id(const struct pdr *pdr)
{
	return le32_to_cpu(pdr->_id);
}

static inline u32
pdr_addr(const struct pdr *pdr)
{
	return le32_to_cpu(pdr->_addr);
}

static inline u32
pdr_len(const struct pdr *pdr)
{
	return le32_to_cpu(pdr->_len);
}

static inline u32
pdi_id(const struct pdi *pdi)
{
	return le16_to_cpu(pdi->_id);
}

/* Return length of the data only, in bytes */
static inline u32
pdi_len(const struct pdi *pdi)
{
	return 2 * (le16_to_cpu(pdi->_len) - 1);
}


/* Set address of the auxiliary port */
static inline void
spectrum_aux_setaddr(hermes_t *hw, u32 addr)
{
	hermes_write_reg(hw, HERMES_AUXPAGE, (u16) (addr >> 7));
	hermes_write_reg(hw, HERMES_AUXOFFSET, (u16) (addr & 0x7F));
}


/* Open access to the auxiliary port */
static int
spectrum_aux_open(hermes_t *hw)
{
	int i;

	/* Already open? */
	if (hermes_read_reg(hw, HERMES_CONTROL) == HERMES_AUX_ENABLED)
		return 0;

	hermes_write_reg(hw, HERMES_PARAM0, HERMES_AUX_PW0);
	hermes_write_reg(hw, HERMES_PARAM1, HERMES_AUX_PW1);
	hermes_write_reg(hw, HERMES_PARAM2, HERMES_AUX_PW2);
	hermes_write_reg(hw, HERMES_CONTROL, HERMES_AUX_ENABLE);

	for (i = 0; i < 20; i++) {
		udelay(10);
		if (hermes_read_reg(hw, HERMES_CONTROL) ==
		    HERMES_AUX_ENABLED)
			return 0;
	}

	return -EBUSY;
}


#define CS_CHECK(fn, ret) \
  do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

/*
 * Reset the card using configuration registers COR and CCSR.
 * If IDLE is 1, stop the firmware, so that it can be safely rewritten.
 */
static int
spectrum_reset(dev_link_t *link, int idle)
{
	int last_ret, last_fn;
	conf_reg_t reg;
	u_int save_cor;

	/* Doing it if hardware is gone is guaranteed crash */
	if (!(link->state & DEV_CONFIG))
		return -ENODEV;

	/* Save original COR value */
	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link->handle, &reg));
	save_cor = reg.Value;

	/* Soft-Reset card */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (save_cor | COR_SOFT_RESET);
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link->handle, &reg));
	udelay(1000);

	/* Read CCSR */
	reg.Action = CS_READ;
	reg.Offset = CISREG_CCSR;
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link->handle, &reg));

	/*
	 * Start or stop the firmware.  Memory width bit should be
	 * preserved from the value we've just read.
	 */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_CCSR;
	reg.Value = (idle ? HCR_IDLE : HCR_RUN) | (reg.Value & HCR_MEM16);
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link->handle, &reg));
	udelay(1000);

	/* Restore original COR configuration index */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = (save_cor & ~COR_SOFT_RESET);
	CS_CHECK(AccessConfigurationRegister,
		 pcmcia_access_configuration_register(link->handle, &reg));
	udelay(1000);
	return 0;

      cs_failed:
	cs_error(link->handle, last_fn, last_ret);
	return -ENODEV;
}


/*
 * Scan PDR for the record with the specified RECORD_ID.
 * If it's not found, return NULL.
 */
static struct pdr *
spectrum_find_pdr(struct pdr *first_pdr, u32 record_id)
{
	struct pdr *pdr = first_pdr;

	while (pdr_id(pdr) != PDI_END) {
		/*
		 * PDR area is currently not terminated by PDI_END.
		 * It's followed by CRC records, which have the type
		 * field where PDR has length.  The type can be 0 or 1.
		 */
		if (pdr_len(pdr) < 2)
			return NULL;

		/* If the record ID matches, we are done */
		if (pdr_id(pdr) == record_id)
			return pdr;

		pdr = (struct pdr *) pdr->next;
	}
	return NULL;
}


/* Process one Plug Data Item - find corresponding PDR and plug it */
static int
spectrum_plug_pdi(hermes_t *hw, struct pdr *first_pdr, struct pdi *pdi)
{
	struct pdr *pdr;

	/* Find the PDI corresponding to this PDR */
	pdr = spectrum_find_pdr(first_pdr, pdi_id(pdi));

	/* No match is found, safe to ignore */
	if (!pdr)
		return 0;

	/* Lengths of the data in PDI and PDR must match */
	if (pdi_len(pdi) != pdr_len(pdr))
		return -EINVAL;

	/* do the actual plugging */
	spectrum_aux_setaddr(hw, pdr_addr(pdr));
	hermes_write_words(hw, HERMES_AUXDATA, pdi->data,
			   pdi_len(pdi) / 2);

	return 0;
}


/* Read PDA from the adapter */
static int
spectrum_read_pda(hermes_t *hw, __le16 *pda, int pda_len)
{
	int ret;
	int pda_size;

	/* Issue command to read EEPROM */
	ret = hermes_docmd_wait(hw, HERMES_CMD_READMIF, 0, NULL);
	if (ret)
		return ret;

	/* Open auxiliary port */
	ret = spectrum_aux_open(hw);
	if (ret)
		return ret;

	/* read PDA from EEPROM */
	spectrum_aux_setaddr(hw, PDA_ADDR);
	hermes_read_words(hw, HERMES_AUXDATA, pda, pda_len / 2);

	/* Check PDA length */
	pda_size = le16_to_cpu(pda[0]);
	if (pda_size > pda_len)
		return -EINVAL;

	return 0;
}


/* Parse PDA and write the records into the adapter */
static int
spectrum_apply_pda(hermes_t *hw, const struct dblock *first_block,
		   __le16 *pda)
{
	int ret;
	struct pdi *pdi;
	struct pdr *first_pdr;
	const struct dblock *blk = first_block;

	/* Skip all blocks to locate Plug Data References */
	while (dblock_addr(blk) != BLOCK_END)
		blk = (struct dblock *) &blk->data[dblock_len(blk)];

	first_pdr = (struct pdr *) blk;

	/* Go through every PDI and plug them into the adapter */
	pdi = (struct pdi *) (pda + 2);
	while (pdi_id(pdi) != PDI_END) {
		ret = spectrum_plug_pdi(hw, first_pdr, pdi);
		if (ret)
			return ret;

		/* Increment to the next PDI */
		pdi = (struct pdi *) &pdi->data[pdi_len(pdi)];
	}
	return 0;
}


/* Load firmware blocks into the adapter */
static int
spectrum_load_blocks(hermes_t *hw, const struct dblock *first_block)
{
	const struct dblock *blk;
	u32 blkaddr;
	u32 blklen;

	blk = first_block;
	blkaddr = dblock_addr(blk);
	blklen = dblock_len(blk);

	while (dblock_addr(blk) != BLOCK_END) {
		spectrum_aux_setaddr(hw, blkaddr);
		hermes_write_words(hw, HERMES_AUXDATA, blk->data,
				   blklen / 2);

		blk = (struct dblock *) &blk->data[blklen];
		blkaddr = dblock_addr(blk);
		blklen = dblock_len(blk);
	}
	return 0;
}


/*
 * Process a firmware image - stop the card, load the firmware, reset
 * the card and make sure it responds.  For the secondary firmware take
 * care of the PDA - read it and then write it on top of the firmware.
 */
static int
spectrum_dl_image(hermes_t *hw, dev_link_t *link,
		  const unsigned char *image)
{
	int ret;
	const unsigned char *ptr;
	const struct dblock *first_block;

	/* Plug Data Area (PDA) */
	__le16 pda[PDA_WORDS];

	/* Binary block begins after the 0x1A marker */
	ptr = image;
	while (*ptr++ != TEXT_END);
	first_block = (const struct dblock *) ptr;

	/* Read the PDA */
	if (image != primsym) {
		ret = spectrum_read_pda(hw, pda, sizeof(pda));
		if (ret)
			return ret;
	}

	/* Stop the firmware, so that it can be safely rewritten */
	ret = spectrum_reset(link, 1);
	if (ret)
		return ret;

	/* Program the adapter with new firmware */
	ret = spectrum_load_blocks(hw, first_block);
	if (ret)
		return ret;

	/* Write the PDA to the adapter */
	if (image != primsym) {
		ret = spectrum_apply_pda(hw, first_block, pda);
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
	if (image != primsym && ret != 0)
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
spectrum_dl_firmware(hermes_t *hw, dev_link_t *link)
{
	int ret;
	client_handle_t handle = link->handle;
	const struct firmware *fw_entry;

	if (request_firmware(&fw_entry, primary_fw_name,
			     &handle_to_dev(handle)) == 0) {
		primsym = fw_entry->data;
	} else {
		printk(KERN_ERR PFX "Cannot find firmware: %s\n",
		       primary_fw_name);
		return -ENOENT;
	}

	if (request_firmware(&fw_entry, secondary_fw_name,
			     &handle_to_dev(handle)) == 0) {
		secsym = fw_entry->data;
	} else {
		printk(KERN_ERR PFX "Cannot find firmware: %s\n",
		       secondary_fw_name);
		return -ENOENT;
	}

	/* Load primary firmware */
	ret = spectrum_dl_image(hw, link, primsym);
	if (ret) {
		printk(KERN_ERR PFX "Primary firmware download failed\n");
		return ret;
	}

	/* Load secondary firmware */
	ret = spectrum_dl_image(hw, link, secsym);

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
	dev_link_t *link = &card->link;
	int err;

	if (!hermes_present(&priv->hw)) {
		/* The firmware needs to be reloaded */
		if (spectrum_dl_firmware(&priv->hw, &card->link) != 0) {
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
static dev_link_t *
spectrum_cs_attach(void)
{
	struct net_device *dev;
	struct orinoco_private *priv;
	struct orinoco_pccard *card;
	dev_link_t *link;
	client_reg_t client_reg;
	int ret;

	dev = alloc_orinocodev(sizeof(*card), spectrum_cs_hard_reset);
	if (! dev)
		return NULL;
	priv = netdev_priv(dev);
	card = priv->card;

	/* Link both structures together */
	link = &card->link;
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

	/* Register with Card Services */
	/* FIXME: need a lock? */
	link->next = dev_list;
	dev_list = link;

	client_reg.dev_info = &dev_info;
	client_reg.Version = 0x0210; /* FIXME: what does this mean? */
	client_reg.event_callback_args.client_data = link;

	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != CS_SUCCESS) {
		cs_error(link->handle, RegisterClient, ret);
		spectrum_cs_detach(link);
		return NULL;
	}

	return link;
}				/* spectrum_cs_attach */

/*
 * This deletes a driver "instance".  The device is de-registered with
 * Card Services.  If it has been released, all local data structures
 * are freed.  Otherwise, the structures will be freed when the device
 * is released.
 */
static void spectrum_cs_detach(dev_link_t *link)
{
	dev_link_t **linkp;
	struct net_device *dev = link->priv;

	/* Locate device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
		if (*linkp == link)
			break;

	BUG_ON(*linkp == NULL);

	if (link->state & DEV_CONFIG)
		spectrum_cs_release(link);

	/* Break the link with Card Services */
	if (link->handle)
		pcmcia_deregister_client(link->handle);

	/* Unlink device structure, and free it */
	*linkp = link->next;
	DEBUG(0, PFX "detach: link=%p link->dev=%p\n", link, link->dev);
	if (link->dev) {
		DEBUG(0, PFX "About to unregister net device %p\n",
		      dev);
		unregister_netdev(dev);
	}
	free_orinocodev(dev);
}				/* spectrum_cs_detach */

/*
 * spectrum_cs_config() is scheduled to run after a CARD_INSERTION
 * event is received, to configure the PCMCIA socket, and to make the
 * device available to the system.
 */

static void
spectrum_cs_config(dev_link_t *link)
{
	struct net_device *dev = link->priv;
	client_handle_t handle = link->handle;
	struct orinoco_private *priv = netdev_priv(dev);
	struct orinoco_pccard *card = priv->card;
	hermes_t *hw = &priv->hw;
	int last_fn, last_ret;
	u_char buf[64];
	config_info_t conf;
	cisinfo_t info;
	tuple_t tuple;
	cisparse_t parse;
	void __iomem *mem;

	CS_CHECK(ValidateCIS, pcmcia_validate_cis(handle, &info));

	/*
	 * This reads the card's CONFIG tuple to find its
	 * configuration registers.
	 */
	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;

	/* Look up the current Vcc */
	CS_CHECK(GetConfigurationInfo,
		 pcmcia_get_configuration_info(handle, &conf));
	link->conf.Vcc = conf.Vcc;

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
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		cistpl_cftable_entry_t dflt = { .index = 0 };

		if ( (pcmcia_get_tuple_data(handle, &tuple) != 0)
		    || (pcmcia_parse_tuple(handle, &tuple, &parse) != 0))
			goto next_entry;

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Does this card need audio output? */
		if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
			link->conf.Attributes |= CONF_ENABLE_SPKR;
			link->conf.Status = CCSR_AUDIO_ENA;
		}

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
			link->conf.Vpp1 = link->conf.Vpp2 =
			    cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
		else if (dflt.vpp1.present & (1 << CISTPL_POWER_VNOM))
			link->conf.Vpp1 = link->conf.Vpp2 =
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
			if (pcmcia_request_io(link->handle, &link->io) != 0)
				goto next_entry;
		}


		/* If we got this far, we're cool! */

		break;
		
	next_entry:
		if (link->io.NumPorts1)
			pcmcia_release_io(link->handle, &link->io);
		last_ret = pcmcia_get_next_tuple(handle, &tuple);
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
	CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));

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
		 pcmcia_request_configuration(link->handle, &link->conf));

	/* Ok, we have the configuration, prepare to register the netdev */
	dev->base_addr = link->io.BasePort1;
	dev->irq = link->irq.AssignedIRQ;
	SET_MODULE_OWNER(dev);
	card->node.major = card->node.minor = 0;

	/* Reset card and download firmware */
	if (spectrum_cs_hard_reset(priv) != 0) {
		goto failed;
	}

	SET_NETDEV_DEV(dev, &handle_to_dev(handle));
	/* Tell the stack we exist */
	if (register_netdev(dev) != 0) {
		printk(KERN_ERR PFX "register_netdev() failed\n");
		goto failed;
	}

	/* At this point, the dev_node_t structure(s) needs to be
	 * initialized and arranged in a linked list at link->dev. */
	strcpy(card->node.dev_name, dev->name);
	link->dev = &card->node; /* link->dev being non-NULL is also
                                    used to indicate that the
                                    net_device has been registered */
	link->state &= ~DEV_CONFIG_PENDING;

	/* Finally, report what we've done */
	printk(KERN_DEBUG "%s: index 0x%02x: Vcc %d.%d",
	       dev->name, link->conf.ConfigIndex,
	       link->conf.Vcc / 10, link->conf.Vcc % 10);
	if (link->conf.Vpp1)
		printk(", Vpp %d.%d", link->conf.Vpp1 / 10,
		       link->conf.Vpp1 % 10);
	printk(", irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1 + link->io.NumPorts1 - 1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2 + link->io.NumPorts2 - 1);
	printk("\n");

	return;

 cs_failed:
	cs_error(link->handle, last_fn, last_ret);

 failed:
	spectrum_cs_release(link);
}				/* spectrum_cs_config */

/*
 * After a card is removed, spectrum_cs_release() will unregister the
 * device, and release the PCMCIA configuration.  If the device is
 * still open, this will be postponed until it is closed.
 */
static void
spectrum_cs_release(dev_link_t *link)
{
	struct net_device *dev = link->priv;
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;

	/* We're committed to taking the device away now, so mark the
	 * hardware as unavailable */
	spin_lock_irqsave(&priv->lock, flags);
	priv->hw_unavailable++;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Don't bother checking to see if these succeed or not */
	pcmcia_release_configuration(link->handle);
	if (link->io.NumPorts1)
		pcmcia_release_io(link->handle, &link->io);
	if (link->irq.AssignedIRQ)
		pcmcia_release_irq(link->handle, &link->irq);
	link->state &= ~DEV_CONFIG;
	if (priv->hw.iobase)
		ioport_unmap(priv->hw.iobase);
}				/* spectrum_cs_release */

/*
 * The card status event handler.  Mostly, this schedules other stuff
 * to run after an event is received.
 */
static int
spectrum_cs_event(event_t event, int priority,
		       event_callback_args_t * args)
{
	dev_link_t *link = args->client_data;
	struct net_device *dev = link->priv;
	struct orinoco_private *priv = netdev_priv(dev);
	int err = 0;
	unsigned long flags;

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			unsigned long flags;

			spin_lock_irqsave(&priv->lock, flags);
			netif_device_detach(dev);
			priv->hw_unavailable++;
			spin_unlock_irqrestore(&priv->lock, flags);
		}
		break;

	case CS_EVENT_CARD_INSERTION:
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		spectrum_cs_config(link);
		break;

	case CS_EVENT_PM_SUSPEND:
		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		/* Mark the device as stopped, to block IO until later */
		if (link->state & DEV_CONFIG) {
			/* This is probably racy, but I can't think of
                           a better way, short of rewriting the PCMCIA
                           layer to not suck :-( */
			spin_lock_irqsave(&priv->lock, flags);

			err = __orinoco_down(dev);
			if (err)
				printk(KERN_WARNING "%s: %s: Error %d downing interface\n",
				       dev->name,
				       event == CS_EVENT_PM_SUSPEND ? "SUSPEND" : "RESET_PHYSICAL",
				       err);

			netif_device_detach(dev);
			priv->hw_unavailable++;

			spin_unlock_irqrestore(&priv->lock, flags);

			pcmcia_release_configuration(link->handle);
		}
		break;

	case CS_EVENT_PM_RESUME:
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		if (link->state & DEV_CONFIG) {
			/* FIXME: should we double check that this is
			 * the same card as we had before */
			pcmcia_request_configuration(link->handle, &link->conf);
			netif_device_attach(dev);
			priv->hw_unavailable--;
			schedule_work(&priv->reset_work);
		}
		break;
	}

	return err;
}				/* spectrum_cs_event */

/********************************************************************/
/* Module initialization					    */
/********************************************************************/

/* Can't be declared "const" or the whole __initdata section will
 * become const */
static char version[] __initdata = DRIVER_NAME " " DRIVER_VERSION
	" (Pavel Roskin <proski@gnu.org>,"
	" David Gibson <hermes@gibson.dropbear.id.au>, et al)";

static struct pcmcia_device_id spectrum_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x026c, 0x0001), /* Symbol Spectrum24 LA4100 */
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
	.attach		= spectrum_cs_attach,
	.detach		= spectrum_cs_detach,
	.event		= spectrum_cs_event,
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
	BUG_ON(dev_list != NULL);
}

module_init(init_spectrum_cs);
module_exit(exit_spectrum_cs);
