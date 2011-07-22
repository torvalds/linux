/* orinoco_cs.c (formerly known as dldwd_cs.c)
 *
 * A driver for "Hermes" chipset based PCMCIA wireless adaptors, such
 * as the Lucent WavelanIEEE/Orinoco cards and their OEM (Cabletron/
 * EnteraSys RoamAbout 802.11, ELSA Airlancer, Melco Buffalo and others).
 * It should also be usable on various Prism II based cards such as the
 * Linksys, D-Link and Farallon Skyline. It should also work on Symbol
 * cards such as the 3Com AirConnect and Ericsson WLAN.
 *
 * Copyright notice & release notes in file main.c
 */

#define DRIVER_NAME "orinoco_cs"
#define PFX DRIVER_NAME ": "

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include "orinoco.h"

/********************************************************************/
/* Module stuff							    */
/********************************************************************/

MODULE_AUTHOR("David Gibson <hermes@gibson.dropbear.id.au>");
MODULE_DESCRIPTION("Driver for PCMCIA Lucent Orinoco,"
		   " Prism II based and similar wireless cards");
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

	/* Used to handle hard reset */
	/* yuck, we need this hack to work around the insanity of the
	 * PCMCIA layer */
	unsigned long hard_reset_in_progress;
};


/********************************************************************/
/* Function prototypes						    */
/********************************************************************/

static int orinoco_cs_config(struct pcmcia_device *link);
static void orinoco_cs_release(struct pcmcia_device *link);
static void orinoco_cs_detach(struct pcmcia_device *p_dev);

/********************************************************************/
/* Device methods     						    */
/********************************************************************/

static int
orinoco_cs_hard_reset(struct orinoco_private *priv)
{
	struct orinoco_pccard *card = priv->card;
	struct pcmcia_device *link = card->p_dev;
	int err;

	/* We need atomic ops here, because we're not holding the lock */
	set_bit(0, &card->hard_reset_in_progress);

	err = pcmcia_reset_card(link->socket);
	if (err)
		return err;

	msleep(100);
	clear_bit(0, &card->hard_reset_in_progress);

	return 0;
}

/********************************************************************/
/* PCMCIA stuff     						    */
/********************************************************************/

static int
orinoco_cs_probe(struct pcmcia_device *link)
{
	struct orinoco_private *priv;
	struct orinoco_pccard *card;

	priv = alloc_orinocodev(sizeof(*card), &link->dev,
				orinoco_cs_hard_reset, NULL);
	if (!priv)
		return -ENOMEM;
	card = priv->card;

	/* Link both structures together */
	card->p_dev = link;
	link->priv = priv;

	return orinoco_cs_config(link);
}				/* orinoco_cs_attach */

static void orinoco_cs_detach(struct pcmcia_device *link)
{
	struct orinoco_private *priv = link->priv;

	orinoco_if_del(priv);

	orinoco_cs_release(link);

	free_orinocodev(priv);
}				/* orinoco_cs_detach */

static int orinoco_cs_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
};

static int
orinoco_cs_config(struct pcmcia_device *link)
{
	struct orinoco_private *priv = link->priv;
	hermes_t *hw = &priv->hw;
	int ret;
	void __iomem *mem;

	link->config_flags |= CONF_AUTO_SET_VPP | CONF_AUTO_CHECK_VCC |
		CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	if (ignore_cis_vcc)
		link->config_flags &= ~CONF_AUTO_CHECK_VCC;
	ret = pcmcia_loop_config(link, orinoco_cs_config_check, NULL);
	if (ret) {
		if (!ignore_cis_vcc)
			printk(KERN_ERR PFX "GetNextTuple(): No matching "
			       "CIS configuration.  Maybe you need the "
			       "ignore_cis_vcc=1 parameter.\n");
		goto failed;
	}

	mem = ioport_map(link->resource[0]->start,
			resource_size(link->resource[0]));
	if (!mem)
		goto failed;

	/* We initialize the hermes structure before completing PCMCIA
	 * configuration just in case the interrupt handler gets
	 * called. */
	hermes_struct_init(hw, mem, HERMES_16BIT_REGSPACING);

	ret = pcmcia_request_irq(link, orinoco_interrupt);
	if (ret)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	/* Initialise the main driver */
	if (orinoco_init(priv) != 0) {
		printk(KERN_ERR PFX "orinoco_init() failed\n");
		goto failed;
	}

	/* Register an interface with the stack */
	if (orinoco_if_add(priv, link->resource[0]->start,
			   link->irq, NULL) != 0) {
		printk(KERN_ERR PFX "orinoco_if_add() failed\n");
		goto failed;
	}

	return 0;

 failed:
	orinoco_cs_release(link);
	return -ENODEV;
}				/* orinoco_cs_config */

static void
orinoco_cs_release(struct pcmcia_device *link)
{
	struct orinoco_private *priv = link->priv;
	unsigned long flags;

	/* We're committed to taking the device away now, so mark the
	 * hardware as unavailable */
	priv->hw.ops->lock_irqsave(&priv->lock, &flags);
	priv->hw_unavailable++;
	priv->hw.ops->unlock_irqrestore(&priv->lock, &flags);

	pcmcia_disable_device(link);
	if (priv->hw.iobase)
		ioport_unmap(priv->hw.iobase);
}				/* orinoco_cs_release */

static int orinoco_cs_suspend(struct pcmcia_device *link)
{
	struct orinoco_private *priv = link->priv;
	struct orinoco_pccard *card = priv->card;

	/* This is probably racy, but I can't think of
	   a better way, short of rewriting the PCMCIA
	   layer to not suck :-( */
	if (!test_bit(0, &card->hard_reset_in_progress))
		orinoco_down(priv);

	return 0;
}

static int orinoco_cs_resume(struct pcmcia_device *link)
{
	struct orinoco_private *priv = link->priv;
	struct orinoco_pccard *card = priv->card;
	int err = 0;

	if (!test_bit(0, &card->hard_reset_in_progress))
		err = orinoco_up(priv);

	return err;
}


/********************************************************************/
/* Module initialization					    */
/********************************************************************/

static const struct pcmcia_device_id orinoco_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0101, 0x0777), /* 3Com AirConnect PCI 777A */
	PCMCIA_DEVICE_MANF_CARD(0x0156, 0x0002), /* Lucent Orinoco and old Intersil */
	PCMCIA_DEVICE_MANF_CARD(0x016b, 0x0001), /* Ericsson WLAN Card C11 */
	PCMCIA_DEVICE_MANF_CARD(0x01eb, 0x080a), /* Nortel Networks eMobility 802.11 Wireless Adapter */
	PCMCIA_DEVICE_MANF_CARD(0x0261, 0x0002), /* AirWay 802.11 Adapter (PCMCIA) */
	PCMCIA_DEVICE_MANF_CARD(0x0268, 0x0001), /* ARtem Onair */
	PCMCIA_DEVICE_MANF_CARD(0x0268, 0x0003), /* ARtem Onair Comcard 11 */
	PCMCIA_DEVICE_MANF_CARD(0x026f, 0x0305), /* Buffalo WLI-PCM-S11 */
	PCMCIA_DEVICE_MANF_CARD(0x02aa, 0x0002), /* ASUS SpaceLink WL-100 */
	PCMCIA_DEVICE_MANF_CARD(0x02ac, 0x0002), /* SpeedStream SS1021 Wireless Adapter */
	PCMCIA_DEVICE_MANF_CARD(0x02ac, 0x3021), /* SpeedStream Wireless Adapter */
	PCMCIA_DEVICE_MANF_CARD(0x14ea, 0xb001), /* PLANEX RoadLannerWave GW-NS11H */
	PCMCIA_DEVICE_PROD_ID12("3Com", "3CRWE737A AirConnect Wireless LAN PC Card", 0x41240e5b, 0x56010af3),
	PCMCIA_DEVICE_PROD_ID12("Allied Telesyn", "AT-WCL452 Wireless PCMCIA Radio", 0x5cd01705, 0x4271660f),
	PCMCIA_DEVICE_PROD_ID12("ASUS", "802_11B_CF_CARD_25", 0x78fc06ee, 0x45a50c1e),
	PCMCIA_DEVICE_PROD_ID12("ASUS", "802_11b_PC_CARD_25", 0x78fc06ee, 0xdb9aa842),
	PCMCIA_DEVICE_PROD_ID12("Avaya Communication", "Avaya Wireless PC Card", 0xd8a43b78, 0x0d341169),
	PCMCIA_DEVICE_PROD_ID12("BENQ", "AWL100 PCMCIA ADAPTER", 0x35dadc74, 0x01f7fedb),
	PCMCIA_DEVICE_PROD_ID12("Cabletron", "RoamAbout 802.11 DS", 0x32d445f5, 0xedeffd90),
	PCMCIA_DEVICE_PROD_ID12("D-Link Corporation", "D-Link DWL-650H 11Mbps WLAN Adapter", 0xef544d24, 0xcd8ea916),
	PCMCIA_DEVICE_PROD_ID12("ELSA", "AirLancer MC-11", 0x4507a33a, 0xef54f0e3),
	PCMCIA_DEVICE_PROD_ID12("HyperLink", "Wireless PC Card 11Mbps", 0x56cc3f1a, 0x0bcf220c),
	PCMCIA_DEVICE_PROD_ID12("Intel", "PRO/Wireless 2011 LAN PC Card", 0x816cc815, 0x07f58077),
	PCMCIA_DEVICE_PROD_ID12("LeArtery", "SYNCBYAIR 11Mbps Wireless LAN PC Card", 0x7e3b326a, 0x49893e92),
	PCMCIA_DEVICE_PROD_ID12("Lucent Technologies", "WaveLAN/IEEE", 0x23eb9949, 0xc562e72a),
	PCMCIA_DEVICE_PROD_ID12("MELCO", "WLI-PCM-L11", 0x481e0094, 0x7360e410),
	PCMCIA_DEVICE_PROD_ID12("MELCO", "WLI-PCM-L11G", 0x481e0094, 0xf57ca4b3),
	PCMCIA_DEVICE_PROD_ID12("NCR", "WaveLAN/IEEE", 0x24358cd4, 0xc562e72a),
	PCMCIA_DEVICE_PROD_ID12("Nortel Networks", "emobility 802.11 Wireless LAN PC Card", 0x2d617ea0, 0x88cd5767),
	PCMCIA_DEVICE_PROD_ID12("OTC", "Wireless AirEZY 2411-PCC WLAN Card", 0x4ac44287, 0x235a6bed),
	PCMCIA_DEVICE_PROD_ID12("PROXIM", "LAN PC CARD HARMONY 80211B", 0xc6536a5e, 0x090c3cd9),
	PCMCIA_DEVICE_PROD_ID12("PROXIM", "LAN PCI CARD HARMONY 80211B", 0xc6536a5e, 0x9f494e26),
	PCMCIA_DEVICE_PROD_ID12("SAMSUNG", "11Mbps WLAN Card", 0x43d74cb4, 0x579bd91b),
	PCMCIA_DEVICE_PROD_ID12("Symbol Technologies", "LA4111 Spectrum24 Wireless LAN PC Card", 0x3f02b4d6, 0x3663cb0e),
#ifdef CONFIG_HERMES_PRISM
	/* Only entries that certainly identify Prism chipset */
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7100), /* SonicWALL Long Range Wireless Card */
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7300), /* Sohoware NCP110, Philips 802.11b */
	PCMCIA_DEVICE_MANF_CARD(0x0089, 0x0002), /* AnyPoint(TM) Wireless II PC Card */
	PCMCIA_DEVICE_MANF_CARD(0x0126, 0x8000), /* PROXIM RangeLAN-DS/LAN PC CARD */
	PCMCIA_DEVICE_MANF_CARD(0x0138, 0x0002), /* Compaq WL100 11 Mbps Wireless Adapter */
	PCMCIA_DEVICE_MANF_CARD(0x01ff, 0x0008), /* Intermec MobileLAN 11Mbps 802.11b WLAN Card */
	PCMCIA_DEVICE_MANF_CARD(0x0250, 0x0002), /* Samsung SWL2000-N 11Mb/s WLAN Card */
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1612), /* Linksys WPC11 Version 2.5 */
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1613), /* Linksys WPC11 Version 3 */
	PCMCIA_DEVICE_MANF_CARD(0x028a, 0x0002), /* Compaq HNW-100 11 Mbps Wireless Adapter */
	PCMCIA_DEVICE_MANF_CARD(0x028a, 0x0673), /* Linksys WCF12 Wireless CompactFlash Card */
	PCMCIA_DEVICE_MANF_CARD(0x50c2, 0x7300), /* Airvast WN-100 */
	PCMCIA_DEVICE_MANF_CARD(0x9005, 0x0021), /* Adaptec Ultra Wireless ANW-8030 */
	PCMCIA_DEVICE_MANF_CARD(0xc001, 0x0008), /* CONTEC FLEXSCAN/FX-DDS110-PCC */
	PCMCIA_DEVICE_MANF_CARD(0xc250, 0x0002), /* Conceptronic CON11Cpro, EMTAC A2424i */
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0002), /* Safeway 802.11b, ZCOMAX AirRunner/XI-300 */
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0005), /* D-Link DCF660, Sandisk Connect SDWCFB-000 */
	PCMCIA_DEVICE_PROD_ID123("Instant Wireless ", " Network PC CARD", "Version 01.02", 0x11d901af, 0x6e9bd926, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID12("ACTIONTEC", "PRISM Wireless LAN PC Card", 0x393089da, 0xa71e69d5),
	PCMCIA_DEVICE_PROD_ID12("Addtron", "AWP-100 Wireless PCMCIA", 0xe6ec52ce, 0x08649af2),
	PCMCIA_DEVICE_PROD_ID12("BUFFALO", "WLI-CF-S11G", 0x2decece3, 0x82067c18),
	PCMCIA_DEVICE_PROD_ID12("BUFFALO", "WLI-PCM-L11G", 0x2decece3, 0xf57ca4b3),
	PCMCIA_DEVICE_PROD_ID12("Compaq", "WL200_11Mbps_Wireless_PCI_Card", 0x54f7c49c, 0x15a75e5b),
	PCMCIA_DEVICE_PROD_ID12("corega K.K.", "Wireless LAN PCC-11", 0x5261440f, 0xa6405584),
	PCMCIA_DEVICE_PROD_ID12("corega K.K.", "Wireless LAN PCCA-11", 0x5261440f, 0xdf6115f9),
	PCMCIA_DEVICE_PROD_ID12("corega_K.K.", "Wireless_LAN_PCCB-11", 0x29e33311, 0xee7a27ae),
	PCMCIA_DEVICE_PROD_ID12("Digital Data Communications", "WPC-0100", 0xfdd73470, 0xe0b6f146),
	PCMCIA_DEVICE_PROD_ID12("D", "Link DRC-650 11Mbps WLAN Card", 0x71b18589, 0xf144e3ac),
	PCMCIA_DEVICE_PROD_ID12("D", "Link DWL-650 11Mbps WLAN Card", 0x71b18589, 0xb6f1b0ab),
	PCMCIA_DEVICE_PROD_ID12(" ", "IEEE 802.11 Wireless LAN/PC Card", 0x3b6e20c8, 0xefccafe9),
	PCMCIA_DEVICE_PROD_ID12("INTERSIL", "HFA384x/IEEE", 0x74c5e40d, 0xdb472a18),
	PCMCIA_DEVICE_PROD_ID12("INTERSIL", "I-GATE 11M PC Card / PC Card plus", 0x74c5e40d, 0x8304ff77),
	PCMCIA_DEVICE_PROD_ID12("Intersil", "PRISM 2_5 PCMCIA ADAPTER", 0x4b801a17, 0x6345a0bf),
	PCMCIA_DEVICE_PROD_ID12("Linksys", "Wireless CompactFlash Card", 0x0733cc81, 0x0c52f395),
	PCMCIA_DEVICE_PROD_ID12("Microsoft", "Wireless Notebook Adapter MN-520", 0x5961bf85, 0x6eec8c01),
	PCMCIA_DEVICE_PROD_ID12("NETGEAR MA401RA Wireless PC", "Card", 0x0306467f, 0x9762e8f1),
	PCMCIA_DEVICE_PROD_ID12("NETGEAR MA401 Wireless PC", "Card", 0xa37434e9, 0x9762e8f1),
	PCMCIA_DEVICE_PROD_ID12("OEM", "PRISM2 IEEE 802.11 PC-Card", 0xfea54c90, 0x48f2bdd6),
	PCMCIA_DEVICE_PROD_ID12("PLANEX", "GeoWave/GW-CF110", 0x209f40ab, 0xd9715264),
	PCMCIA_DEVICE_PROD_ID12("PLANEX", "GeoWave/GW-NS110", 0x209f40ab, 0x46263178),
	PCMCIA_DEVICE_PROD_ID12("SMC", "SMC2532W-B EliteConnect Wireless Adapter", 0xc4f8b18b, 0x196bd757),
	PCMCIA_DEVICE_PROD_ID12("SMC", "SMC2632W", 0xc4f8b18b, 0x474a1f2a),
	PCMCIA_DEVICE_PROD_ID12("ZoomAir 11Mbps High", "Rate wireless Networking", 0x273fe3db, 0x32a1eaee),
	PCMCIA_DEVICE_PROD_ID3("HFA3863", 0x355cb092),
	PCMCIA_DEVICE_PROD_ID3("ISL37100P", 0x630d52b2),
	PCMCIA_DEVICE_PROD_ID3("ISL37101P-10", 0xdd97a26b),
	PCMCIA_DEVICE_PROD_ID3("ISL37300P", 0xc9049a39),
#endif
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, orinoco_cs_ids);

static struct pcmcia_driver orinoco_driver = {
	.owner		= THIS_MODULE,
	.name		= DRIVER_NAME,
	.probe		= orinoco_cs_probe,
	.remove		= orinoco_cs_detach,
	.id_table       = orinoco_cs_ids,
	.suspend	= orinoco_cs_suspend,
	.resume		= orinoco_cs_resume,
};

static int __init
init_orinoco_cs(void)
{
	return pcmcia_register_driver(&orinoco_driver);
}

static void __exit
exit_orinoco_cs(void)
{
	pcmcia_unregister_driver(&orinoco_driver);
}

module_init(init_orinoco_cs);
module_exit(exit_orinoco_cs);
