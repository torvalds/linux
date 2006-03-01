#define PRISM2_PCCARD

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/if.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>

#include "hostap_wlan.h"


static char *version = PRISM2_VERSION " (Jouni Malinen <jkmaline@cc.hut.fi>)";
static dev_info_t dev_info = "hostap_cs";

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Support for Intersil Prism2-based 802.11 wireless LAN "
		   "cards (PC Card).");
MODULE_SUPPORTED_DEVICE("Intersil Prism2-based WLAN cards (PC Card)");
MODULE_LICENSE("GPL");
MODULE_VERSION(PRISM2_VERSION);


static int ignore_cis_vcc;
module_param(ignore_cis_vcc, int, 0444);
MODULE_PARM_DESC(ignore_cis_vcc, "Ignore broken CIS VCC entry");


/* struct local_info::hw_priv */
struct hostap_cs_priv {
	dev_node_t node;
	dev_link_t *link;
	int sandisk_connectplus;
};


#ifdef PRISM2_IO_DEBUG

static inline void hfa384x_outb_debug(struct net_device *dev, int a, u8 v)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTB, a, v);
	outb(v, dev->base_addr + a);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline u8 hfa384x_inb_debug(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;
	u8 v;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	v = inb(dev->base_addr + a);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INB, a, v);
	spin_unlock_irqrestore(&local->lock, flags);
	return v;
}

static inline void hfa384x_outw_debug(struct net_device *dev, int a, u16 v)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTW, a, v);
	outw(v, dev->base_addr + a);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline u16 hfa384x_inw_debug(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;
	u16 v;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	v = inw(dev->base_addr + a);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INW, a, v);
	spin_unlock_irqrestore(&local->lock, flags);
	return v;
}

static inline void hfa384x_outsw_debug(struct net_device *dev, int a,
				       u8 *buf, int wc)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTSW, a, wc);
	outsw(dev->base_addr + a, buf, wc);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline void hfa384x_insw_debug(struct net_device *dev, int a,
				      u8 *buf, int wc)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INSW, a, wc);
	insw(dev->base_addr + a, buf, wc);
	spin_unlock_irqrestore(&local->lock, flags);
}

#define HFA384X_OUTB(v,a) hfa384x_outb_debug(dev, (a), (v))
#define HFA384X_INB(a) hfa384x_inb_debug(dev, (a))
#define HFA384X_OUTW(v,a) hfa384x_outw_debug(dev, (a), (v))
#define HFA384X_INW(a) hfa384x_inw_debug(dev, (a))
#define HFA384X_OUTSW(a, buf, wc) hfa384x_outsw_debug(dev, (a), (buf), (wc))
#define HFA384X_INSW(a, buf, wc) hfa384x_insw_debug(dev, (a), (buf), (wc))

#else /* PRISM2_IO_DEBUG */

#define HFA384X_OUTB(v,a) outb((v), dev->base_addr + (a))
#define HFA384X_INB(a) inb(dev->base_addr + (a))
#define HFA384X_OUTW(v,a) outw((v), dev->base_addr + (a))
#define HFA384X_INW(a) inw(dev->base_addr + (a))
#define HFA384X_INSW(a, buf, wc) insw(dev->base_addr + (a), buf, wc)
#define HFA384X_OUTSW(a, buf, wc) outsw(dev->base_addr + (a), buf, wc)

#endif /* PRISM2_IO_DEBUG */


static int hfa384x_from_bap(struct net_device *dev, u16 bap, void *buf,
			    int len)
{
	u16 d_off;
	u16 *pos;

	d_off = (bap == 1) ? HFA384X_DATA1_OFF : HFA384X_DATA0_OFF;
	pos = (u16 *) buf;

	if (len / 2)
		HFA384X_INSW(d_off, buf, len / 2);
	pos += len / 2;

	if (len & 1)
		*((char *) pos) = HFA384X_INB(d_off);

	return 0;
}


static int hfa384x_to_bap(struct net_device *dev, u16 bap, void *buf, int len)
{
	u16 d_off;
	u16 *pos;

	d_off = (bap == 1) ? HFA384X_DATA1_OFF : HFA384X_DATA0_OFF;
	pos = (u16 *) buf;

	if (len / 2)
		HFA384X_OUTSW(d_off, buf, len / 2);
	pos += len / 2;

	if (len & 1)
		HFA384X_OUTB(*((char *) pos), d_off);

	return 0;
}


/* FIX: This might change at some point.. */
#include "hostap_hw.c"



static void prism2_detach(struct pcmcia_device *p_dev);
static void prism2_release(u_long arg);
static int prism2_config(dev_link_t *link);


static int prism2_pccard_card_present(local_info_t *local)
{
	struct hostap_cs_priv *hw_priv = local->hw_priv;
	if (hw_priv != NULL && hw_priv->link != NULL &&
	    ((hw_priv->link->state & (DEV_PRESENT | DEV_CONFIG)) ==
	     (DEV_PRESENT | DEV_CONFIG)))
		return 1;
	return 0;
}


/*
 * SanDisk CompactFlash WLAN Flashcard - Product Manual v1.0
 * Document No. 20-10-00058, January 2004
 * http://www.sandisk.com/pdf/industrial/ProdManualCFWLANv1.0.pdf
 */
#define SANDISK_WLAN_ACTIVATION_OFF 0x40
#define SANDISK_HCR_OFF 0x42


static void sandisk_set_iobase(local_info_t *local)
{
	int res;
	conf_reg_t reg;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	reg.Function = 0;
	reg.Action = CS_WRITE;
	reg.Offset = 0x10; /* 0x3f0 IO base 1 */
	reg.Value = hw_priv->link->io.BasePort1 & 0x00ff;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "Prism3 SanDisk - failed to set I/O base 0 -"
		       " res=%d\n", res);
	}
	udelay(10);

	reg.Function = 0;
	reg.Action = CS_WRITE;
	reg.Offset = 0x12; /* 0x3f2 IO base 2 */
	reg.Value = (hw_priv->link->io.BasePort1 & 0xff00) >> 8;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "Prism3 SanDisk - failed to set I/O base 1 -"
		       " res=%d\n", res);
	}
}


static void sandisk_write_hcr(local_info_t *local, int hcr)
{
	struct net_device *dev = local->dev;
	int i;

	HFA384X_OUTB(0x80, SANDISK_WLAN_ACTIVATION_OFF);
	udelay(50);
	for (i = 0; i < 10; i++) {
		HFA384X_OUTB(hcr, SANDISK_HCR_OFF);
	}
	udelay(55);
	HFA384X_OUTB(0x45, SANDISK_WLAN_ACTIVATION_OFF);
}


static int sandisk_enable_wireless(struct net_device *dev)
{
	int res, ret = 0;
	conf_reg_t reg;
	struct hostap_interface *iface = dev->priv;
	local_info_t *local = iface->local;
	tuple_t tuple;
	cisparse_t *parse = NULL;
	u_char buf[64];
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	if (hw_priv->link->io.NumPorts1 < 0x42) {
		/* Not enough ports to be SanDisk multi-function card */
		ret = -ENODEV;
		goto done;
	}

	parse = kmalloc(sizeof(cisparse_t), GFP_KERNEL);
	if (parse == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	tuple.DesiredTuple = CISTPL_MANFID;
	tuple.Attributes = TUPLE_RETURN_COMMON;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	if (pcmcia_get_first_tuple(hw_priv->link->handle, &tuple) ||
	    pcmcia_get_tuple_data(hw_priv->link->handle, &tuple) ||
	    pcmcia_parse_tuple(hw_priv->link->handle, &tuple, parse) ||
	    parse->manfid.manf != 0xd601 || parse->manfid.card != 0x0101) {
		/* No SanDisk manfid found */
		ret = -ENODEV;
		goto done;
	}

	tuple.DesiredTuple = CISTPL_LONGLINK_MFC;
	if (pcmcia_get_first_tuple(hw_priv->link->handle, &tuple) ||
	    pcmcia_get_tuple_data(hw_priv->link->handle, &tuple) ||
	    pcmcia_parse_tuple(hw_priv->link->handle, &tuple, parse) ||
		parse->longlink_mfc.nfn < 2) {
		/* No multi-function links found */
		ret = -ENODEV;
		goto done;
	}

	printk(KERN_DEBUG "%s: Multi-function SanDisk ConnectPlus detected"
	       " - using vendor-specific initialization\n", dev->name);
	hw_priv->sandisk_connectplus = 1;

	reg.Function = 0;
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = COR_SOFT_RESET;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "%s: SanDisk - COR sreset failed (%d)\n",
		       dev->name, res);
		goto done;
	}
	mdelay(5);

	reg.Function = 0;
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	/*
	 * Do not enable interrupts here to avoid some bogus events. Interrupts
	 * will be enabled during the first cor_sreset call.
	 */
	reg.Value = COR_LEVEL_REQ | 0x8 | COR_ADDR_DECODE | COR_FUNC_ENA;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "%s: SanDisk - COR sreset failed (%d)\n",
		       dev->name, res);
		goto done;
	}
	mdelay(5);

	sandisk_set_iobase(local);

	HFA384X_OUTB(0xc5, SANDISK_WLAN_ACTIVATION_OFF);
	udelay(10);
	HFA384X_OUTB(0x4b, SANDISK_WLAN_ACTIVATION_OFF);
	udelay(10);

done:
	kfree(parse);
	return ret;
}


static void prism2_pccard_cor_sreset(local_info_t *local)
{
	int res;
	conf_reg_t reg;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	if (!prism2_pccard_card_present(local))
	       return;

	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
	reg.Value = 0;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_cor_sreset failed 1 (%d)\n",
		       res);
		return;
	}
	printk(KERN_DEBUG "prism2_pccard_cor_sreset: original COR %02x\n",
	       reg.Value);

	reg.Action = CS_WRITE;
	reg.Value |= COR_SOFT_RESET;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_cor_sreset failed 2 (%d)\n",
		       res);
		return;
	}

	mdelay(hw_priv->sandisk_connectplus ? 5 : 2);

	reg.Value &= ~COR_SOFT_RESET;
	if (hw_priv->sandisk_connectplus)
		reg.Value |= COR_IREQ_ENA;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_cor_sreset failed 3 (%d)\n",
		       res);
		return;
	}

	mdelay(hw_priv->sandisk_connectplus ? 5 : 2);

	if (hw_priv->sandisk_connectplus)
		sandisk_set_iobase(local);
}


static void prism2_pccard_genesis_reset(local_info_t *local, int hcr)
{
	int res;
	conf_reg_t reg;
	int old_cor;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	if (!prism2_pccard_card_present(local))
	       return;

	if (hw_priv->sandisk_connectplus) {
		sandisk_write_hcr(local, hcr);
		return;
	}

	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
	reg.Value = 0;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 1 "
		       "(%d)\n", res);
		return;
	}
	printk(KERN_DEBUG "prism2_pccard_genesis_sreset: original COR %02x\n",
	       reg.Value);
	old_cor = reg.Value;

	reg.Action = CS_WRITE;
	reg.Value |= COR_SOFT_RESET;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 2 "
		       "(%d)\n", res);
		return;
	}

	mdelay(10);

	/* Setup Genesis mode */
	reg.Action = CS_WRITE;
	reg.Value = hcr;
	reg.Offset = CISREG_CCSR;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 3 "
		       "(%d)\n", res);
		return;
	}
	mdelay(10);

	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = old_cor & ~COR_SOFT_RESET;
	res = pcmcia_access_configuration_register(hw_priv->link->handle,
						   &reg);
	if (res != CS_SUCCESS) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 4 "
		       "(%d)\n", res);
		return;
	}

	mdelay(10);
}


static struct prism2_helper_functions prism2_pccard_funcs =
{
	.card_present	= prism2_pccard_card_present,
	.cor_sreset	= prism2_pccard_cor_sreset,
	.genesis_reset	= prism2_pccard_genesis_reset,
	.hw_type	= HOSTAP_HW_PCCARD,
};


/* allocate local data and register with CardServices
 * initialize dev_link structure, but do not configure the card yet */
static int prism2_attach(struct pcmcia_device *p_dev)
{
	dev_link_t *link;

	link = kmalloc(sizeof(dev_link_t), GFP_KERNEL);
	if (link == NULL)
		return -ENOMEM;

	memset(link, 0, sizeof(dev_link_t));

	PDEBUG(DEBUG_HW, "%s: setting Vcc=33 (constant)\n", dev_info);
	link->conf.Vcc = 33;
	link->conf.IntType = INT_MEMORY_AND_IO;

	link->handle = p_dev;
	p_dev->instance = link;

	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	if (prism2_config(link))
		PDEBUG(DEBUG_EXTRA, "prism2_config() failed\n");

	return 0;
}


static void prism2_detach(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);

	PDEBUG(DEBUG_FLOW, "prism2_detach\n");

	if (link->state & DEV_CONFIG) {
		prism2_release((u_long)link);
	}

	/* release net devices */
	if (link->priv) {
		struct hostap_cs_priv *hw_priv;
		struct net_device *dev;
		struct hostap_interface *iface;
		dev = link->priv;
		iface = netdev_priv(dev);
		hw_priv = iface->local->hw_priv;
		prism2_free_local_data(dev);
		kfree(hw_priv);
	}
	kfree(link);
}


#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

#define CFG_CHECK2(fn, retf) \
do { int ret = (retf); \
if (ret != 0) { \
	PDEBUG(DEBUG_EXTRA, "CardServices(" #fn ") returned %d\n", ret); \
	cs_error(link->handle, fn, ret); \
	goto next_entry; \
} \
} while (0)


/* run after a CARD_INSERTION event is received to configure the PCMCIA
 * socket and make the device available to the system */
static int prism2_config(dev_link_t *link)
{
	struct net_device *dev;
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 1;
	tuple_t tuple;
	cisparse_t *parse;
	int last_fn, last_ret;
	u_char buf[64];
	config_info_t conf;
	cistpl_cftable_entry_t dflt = { 0 };
	struct hostap_cs_priv *hw_priv;

	PDEBUG(DEBUG_FLOW, "prism2_config()\n");

	parse = kmalloc(sizeof(cisparse_t), GFP_KERNEL);
	hw_priv = kmalloc(sizeof(*hw_priv), GFP_KERNEL);
	if (parse == NULL || hw_priv == NULL) {
		ret = -ENOMEM;
		goto failed;
	}
	memset(hw_priv, 0, sizeof(*hw_priv));

	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link->handle, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(link->handle, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(link->handle, &tuple, parse));
	link->conf.ConfigBase = parse->config.base;
	link->conf.Present = parse->config.rmask[0];

	CS_CHECK(GetConfigurationInfo,
		 pcmcia_get_configuration_info(link->handle, &conf));
	PDEBUG(DEBUG_HW, "%s: %s Vcc=%d (from config)\n", dev_info,
	       ignore_cis_vcc ? "ignoring" : "setting", conf.Vcc);
	link->conf.Vcc = conf.Vcc;

	/* Look for an appropriate configuration table entry in the CIS */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link->handle, &tuple));
	for (;;) {
		cistpl_cftable_entry_t *cfg = &(parse->cftable_entry);
		CFG_CHECK2(GetTupleData,
			   pcmcia_get_tuple_data(link->handle, &tuple));
		CFG_CHECK2(ParseTuple,
			   pcmcia_parse_tuple(link->handle, &tuple, parse));

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;
		PDEBUG(DEBUG_EXTRA, "Checking CFTABLE_ENTRY 0x%02X "
		       "(default 0x%02X)\n", cfg->index, dflt.index);

		/* Does this card need audio output? */
		if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
			link->conf.Attributes |= CONF_ENABLE_SPKR;
			link->conf.Status = CCSR_AUDIO_ENA;
		}

		/* Use power settings for Vcc and Vpp if present */
		/*  Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM] /
			    10000 && !ignore_cis_vcc) {
				PDEBUG(DEBUG_EXTRA, "  Vcc mismatch - skipping"
				       " this entry\n");
				goto next_entry;
			}
		} else if (dflt.vcc.present & (1 << CISTPL_POWER_VNOM)) {
			if (conf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM] /
			    10000 && !ignore_cis_vcc) {
				PDEBUG(DEBUG_EXTRA, "  Vcc (default) mismatch "
				       "- skipping this entry\n");
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
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;
		else if (!(link->conf.Attributes & CONF_ENABLE_IRQ)) {
			/* At least Compaq WL200 does not have IRQInfo1 set,
			 * but it does not work without interrupts.. */
			printk("Config has no IRQ info, but trying to enable "
			       "IRQ anyway..\n");
			link->conf.Attributes |= CONF_ENABLE_IRQ;
		}

		/* IO window settings */
		PDEBUG(DEBUG_EXTRA, "IO window settings: cfg->io.nwin=%d "
		       "dflt.io.nwin=%d\n",
		       cfg->io.nwin, dflt.io.nwin);
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			PDEBUG(DEBUG_EXTRA, "io->flags = 0x%04X, "
			       "io.base=0x%04x, len=%d\n", io->flags,
			       io->win[0].base, io->win[0].len);
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines = io->flags &
				CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
		}

		/* This reserves IO space but doesn't actually enable it */
		CFG_CHECK2(RequestIO,
			   pcmcia_request_io(link->handle, &link->io));

		/* This configuration table entry is OK */
		break;

	next_entry:
		CS_CHECK(GetNextTuple,
			 pcmcia_get_next_tuple(link->handle, &tuple));
	}

	/* Need to allocate net_device before requesting IRQ handler */
	dev = prism2_init_local_data(&prism2_pccard_funcs, 0,
				     &handle_to_dev(link->handle));
	if (dev == NULL)
		goto failed;
	link->priv = dev;

	iface = netdev_priv(dev);
	local = iface->local;
	local->hw_priv = hw_priv;
	hw_priv->link = link;
	strcpy(hw_priv->node.dev_name, dev->name);
	link->dev = &hw_priv->node;

	/*
	 * Allocate an interrupt line.  Note that this does not assign a
	 * handler to the interrupt, unless the 'Handler' member of the
	 * irq structure is initialized.
	 */
	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
		link->irq.IRQInfo1 = IRQ_LEVEL_ID;
		link->irq.Handler = prism2_interrupt;
		link->irq.Instance = dev;
		CS_CHECK(RequestIRQ,
			 pcmcia_request_irq(link->handle, &link->irq));
	}

	/*
	 * This actually configures the PCMCIA socket -- setting up
	 * the I/O windows and the interrupt mapping, and putting the
	 * card and host interface into "Memory and IO" mode.
	 */
	CS_CHECK(RequestConfiguration,
		 pcmcia_request_configuration(link->handle, &link->conf));

	dev->irq = link->irq.AssignedIRQ;
	dev->base_addr = link->io.BasePort1;

	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x: Vcc %d.%d",
	       dev_info, link->conf.ConfigIndex,
	       link->conf.Vcc / 10, link->conf.Vcc % 10);
	if (link->conf.Vpp1)
		printk(", Vpp %d.%d", link->conf.Vpp1 / 10,
		       link->conf.Vpp1 % 10);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
		       link->io.BasePort1+link->io.NumPorts1-1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
		       link->io.BasePort2+link->io.NumPorts2-1);
	printk("\n");

	link->state |= DEV_CONFIG;
	link->state &= ~DEV_CONFIG_PENDING;

	local->shutdown = 0;

	sandisk_enable_wireless(dev);

	ret = prism2_hw_config(dev, 1);
	if (!ret) {
		ret = hostap_hw_ready(dev);
		if (ret == 0 && local->ddev)
			strcpy(hw_priv->node.dev_name, local->ddev->name);
	}
	kfree(parse);
	return ret;

 cs_failed:
	cs_error(link->handle, last_fn, last_ret);

 failed:
	kfree(parse);
	kfree(hw_priv);
	prism2_release((u_long)link);
	return ret;
}


static void prism2_release(u_long arg)
{
	dev_link_t *link = (dev_link_t *)arg;

	PDEBUG(DEBUG_FLOW, "prism2_release\n");

	if (link->priv) {
		struct net_device *dev = link->priv;
		struct hostap_interface *iface;

		iface = netdev_priv(dev);
		if (link->state & DEV_CONFIG)
			prism2_hw_shutdown(dev, 0);
		iface->local->shutdown = 1;
	}

	pcmcia_disable_device(link->handle);
	PDEBUG(DEBUG_FLOW, "release - done\n");
}

static int hostap_cs_suspend(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);
	struct net_device *dev = (struct net_device *) link->priv;
	int dev_open = 0;

	PDEBUG(DEBUG_EXTRA, "%s: CS_EVENT_PM_SUSPEND\n", dev_info);

	if (link->state & DEV_CONFIG) {
		struct hostap_interface *iface = netdev_priv(dev);
		if (iface && iface->local)
			dev_open = iface->local->num_dev_open > 0;
		if (dev_open) {
			netif_stop_queue(dev);
			netif_device_detach(dev);
		}
		prism2_suspend(dev);
	}

	return 0;
}

static int hostap_cs_resume(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);
	struct net_device *dev = (struct net_device *) link->priv;
	int dev_open = 0;

	PDEBUG(DEBUG_EXTRA, "%s: CS_EVENT_PM_RESUME\n", dev_info);

	if (link->state & DEV_CONFIG) {
		struct hostap_interface *iface = netdev_priv(dev);
		if (iface && iface->local)
			dev_open = iface->local->num_dev_open > 0;

		prism2_hw_shutdown(dev, 1);
		prism2_hw_config(dev, dev_open ? 0 : 1);
		if (dev_open) {
			netif_device_attach(dev);
			netif_start_queue(dev);
		}
	}

	return 0;
}

static struct pcmcia_device_id hostap_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7100),
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7300),
	PCMCIA_DEVICE_MANF_CARD(0x0101, 0x0777),
	PCMCIA_DEVICE_MANF_CARD(0x0126, 0x8000),
	PCMCIA_DEVICE_MANF_CARD(0x0138, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x0250, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x026f, 0x030b),
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1612),
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1613),
	PCMCIA_DEVICE_MANF_CARD(0x028a, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x02aa, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x02d2, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x50c2, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x50c2, 0x7300),
	PCMCIA_DEVICE_MANF_CARD(0xc00f, 0x0000),
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0005),
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0010),
	PCMCIA_DEVICE_MANF_CARD_PROD_ID1(0x0156, 0x0002, "INTERSIL",
					 0x74c5e40d),
	PCMCIA_DEVICE_MANF_CARD_PROD_ID1(0x0156, 0x0002, "Intersil",
					 0x4b801a17),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "SanDisk", "ConnectPlus",
				    0x7a954bd9, 0x74be00c6),
	PCMCIA_DEVICE_PROD_ID1234(
		"Intersil", "PRISM 2_5 PCMCIA ADAPTER",	"ISL37300P",
		"Eval-RevA",
		0x4b801a17, 0x6345a0bf, 0xc9049a39, 0xc23adc0e),
	PCMCIA_DEVICE_PROD_ID123(
		"Addtron", "AWP-100 Wireless PCMCIA", "Version 01.02",
		0xe6ec52ce, 0x08649af2, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"D", "Link DWL-650 11Mbps WLAN Card", "Version 01.02",
		0x71b18589, 0xb6f1b0ab, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"Instant Wireless ", " Network PC CARD", "Version 01.02",
		0x11d901af, 0x6e9bd926, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"SMC", "SMC2632W", "Version 01.02",
		0xc4f8b18b, 0x474a1f2a, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID12("BUFFALO", "WLI-CF-S11G", 
				0x2decece3, 0x82067c18),
	PCMCIA_DEVICE_PROD_ID12("Compaq", "WL200_11Mbps_Wireless_PCI_Card",
				0x54f7c49c, 0x15a75e5b),
	PCMCIA_DEVICE_PROD_ID12("INTERSIL", "HFA384x/IEEE",
				0x74c5e40d, 0xdb472a18),
	PCMCIA_DEVICE_PROD_ID12("Linksys", "Wireless CompactFlash Card",
				0x0733cc81, 0x0c52f395),
	PCMCIA_DEVICE_PROD_ID12(
		"ZoomAir 11Mbps High", "Rate wireless Networking",
		0x273fe3db, 0x32a1eaee),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, hostap_cs_ids);


static struct pcmcia_driver hostap_driver = {
	.drv		= {
		.name	= "hostap_cs",
	},
	.probe		= prism2_attach,
	.remove		= prism2_detach,
	.owner		= THIS_MODULE,
	.id_table	= hostap_cs_ids,
	.suspend	= hostap_cs_suspend,
	.resume		= hostap_cs_resume,
};

static int __init init_prism2_pccard(void)
{
	printk(KERN_INFO "%s: %s\n", dev_info, version);
	return pcmcia_register_driver(&hostap_driver);
}

static void __exit exit_prism2_pccard(void)
{
	pcmcia_unregister_driver(&hostap_driver);
	printk(KERN_INFO "%s: Driver unloaded\n", dev_info);
}


module_init(init_prism2_pccard);
module_exit(exit_prism2_pccard);
