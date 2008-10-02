#define WLAN_HOSTIF WLAN_PCMCIA
#include "hfa384x.c"
#include "prism2mgmt.c"
#include "prism2mib.c"
#include "prism2sta.c"

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,21) )
#if (WLAN_CPU_FAMILY == WLAN_Ix86)
#ifndef CONFIG_ISA
#warning "You may need to enable ISA support in your kernel."
#endif
#endif
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11) )
static u_int	irq_mask = 0xdeb8;		/* Interrupt mask */
static int	irq_list[4] = { -1 };		/* Interrupt list */
#endif
static u_int	prism2_ignorevcc=1;		/* Boolean, if set, we
						 * ignore what the Vcc
						 * is set to and what the CIS
						 * says.
						 */
module_param( prism2_ignorevcc, int, 0644);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11) )
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9))
static int numlist = 4;
module_param_array(irq_list, int, numlist, 0444);
#else
module_param_array(irq_list, int, NULL, 0444);
#endif
module_param( irq_mask, int, 0644);
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15)
static int prism2_cs_suspend(struct pcmcia_device *pdev);
static int prism2_cs_resume(struct pcmcia_device *pdev);
static void prism2_cs_remove(struct pcmcia_device *pdev);
static int prism2_cs_probe(struct pcmcia_device *pdev);
#else
dev_link_t	*prism2sta_attach(void);
static void	prism2sta_detach(dev_link_t *link);
static void	prism2sta_config(dev_link_t *link);
static void	prism2sta_release(u_long arg);
static int 	prism2sta_event (event_t event, int priority, event_callback_args_t *args);

static dev_link_t	*dev_list = NULL;	/* head of instance list */
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68))
/*----------------------------------------------------------------
* cs_error
*
* Utility function to print card services error messages.
*
* Arguments:
*	handle	client handle identifying this CS client
*	func	CS function number that generated the error
*	ret	CS function return code
*
* Returns:
*	nothing
* Side effects:
*
* Call context:
*	process thread
*	interrupt
----------------------------------------------------------------*/
static void cs_error(client_handle_t handle, int func, int ret)
{
#if (defined(CS_RELEASE_CODE) && (CS_RELEASE_CODE < 0x2911))
	CardServices(ReportError, dev_info, (void *)func, (void *)ret);
#else
	error_info_t err = { func, ret };
	pcmcia_report_error(handle, &err);
#endif
}
#else // kernel_version

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12)
static struct pcmcia_device_id prism2_cs_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("INTERSIL",  "HFA384x/IEEE", 0x74c5e40d, 0xdb472a18), // Intersil PRISM2 Reference Design 11Mb/s 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0x0138, 0x0002), // Compaq WL100/200 11Mb/s 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0x028a, 0x0002), // Compaq iPaq HNW-100 11Mb/s 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0x0250, 0x0002), // Samsung SWL2000-N 11Mb/s 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0002), // Z-Com XI300 11Mb/s 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID12("ZoomAir 11Mbps High",  "Rate wireless Networking", 0x273fe3db, 0x32a1eaee), // ZoomAir 4100 11Mb/s 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID123("Instant Wireless ",  " Network PC CARD",  "Version 01.02", 0x11d901af, 0x6e9bd926, 0x4b74baa0), // Linksys WPC11 11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID123("Addtron",  "AWP-100 Wireless PCMCIA",  "Version 01.02", 0xe6ec52ce, 0x8649af2, 0x4b74baa0), // Addtron AWP-100 11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID123("D",  "Link DWL-650 11Mbps WLAN Card",  "Version 01.02", 0x71b18589, 0xb6f1b0ab, 0x4b74baa0), // D-Link DWL-650 11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID123("SMC",  "SMC2632W",  "Version 01.02", 0xc4f8b18b, 0x474a1f2a, 0x4b74baa0), // SMC 2632W 11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID1234("Intersil",  "PRISM 2_5 PCMCIA ADAPTER",  "ISL37300P",  "Eval-RevA", 0x4b801a17, 0x6345a0bf, 0xc9049a39, 0xc23adc0e), // BroMax Freeport 11Mbps 802.11b WLAN Card (Prism 2.5)
	PCMCIA_DEVICE_PROD_ID123("U.S. Robotics",  "IEEE 802.11b PC-CARD",  "Version 01.02", 0xc7b8df9d, 0x1700d087, 0x4b74baa0), // U.S. Robotics IEEE 802.11b PC-CARD
	PCMCIA_DEVICE_PROD_ID12("Digital Data Communications",  "WPC-0100", 0xfdd73470, 0xe0b6f146), // Level-One WPC-0100
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1612), // Bromax OEM 11Mbps 802.11b WLAN Card (Prism 2.5)
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1613), // Bromax OEM 11Mbps 802.11b WLAN Card (Prism 3)
	PCMCIA_DEVICE_PROD_ID12("corega K.K.",  "Wireless LAN PCC-11", 0x5261440f, 0xa6405584), // corega K.K. Wireless LAN PCC-11
	PCMCIA_DEVICE_PROD_ID12("corega K.K.",  "Wireless LAN PCCA-11", 0x5261440f, 0xdf6115f9), // corega K.K. Wireless LAN PCCA-11
	PCMCIA_DEVICE_MANF_CARD(0xc001, 0x0008), // CONTEC FLEXSCAN/FX-DDS110-PCC
	PCMCIA_DEVICE_PROD_ID12("PLANEX",  "GeoWave/GW-NS110", 0x209f40ab, 0x46263178), // PLANEX GeoWave/GW-NS110
	PCMCIA_DEVICE_PROD_ID123("OEM",  "PRISM2 IEEE 802.11 PC-Card",  "Version 01.02", 0xfea54c90, 0x48f2bdd6, 0x4b74baa0), // Ambicom WL1100 11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID123("LeArtery",  "SYNCBYAIR 11Mbps Wireless LAN PC Card",  "Version 01.02", 0x7e3b326a, 0x49893e92, 0x4b74baa0), // LeArtery SYNCBYAIR 11Mbps 802.11b WLAN Card
PCMCIA_DEVICE_MANF_CARD(0x01ff, 0x0008), // Intermec MobileLAN 11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID123("NETGEAR MA401 Wireless PC",  "Card",  "Version 01.00", 0xa37434e9, 0x9762e8f1, 0xa57adb8c), // NETGEAR MA401 11Mbps 802.11 WLAN Card
	PCMCIA_DEVICE_PROD_ID1234("Intersil",  "PRISM Freedom PCMCIA Adapter",  "ISL37100P",  "Eval-RevA", 0x4b801a17, 0xf222ec2d, 0x630d52b2, 0xc23adc0e), // Intersil PRISM Freedom 11mbps 802.11 WLAN Card
	PCMCIA_DEVICE_PROD_ID123("OTC",  "Wireless AirEZY 2411-PCC WLAN Card",  "Version 01.02", 0x4ac44287, 0x235a6bed, 0x4b74baa0), // OTC Wireless AirEZY 2411-PCC 11Mbps 802.11 WLAN Card
	PCMCIA_DEVICE_PROD_ID1234("802.11",  "11Mbps Wireless LAN Card",  "v08C1",  ""   , 0xb67a610e, 0x655aa7b7, 0x264b451a, 0x0), // Dynalink L11HDT 11Mbps 802.11 WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0xc250, 0x0002), // Dynalink L11HDT 11Mbps 802.11 WLAN Card
	PCMCIA_DEVICE_PROD_ID12("PROXIM",  "RangeLAN-DS/LAN PC CARD", 0xc6536a5e, 0x3f35797d), // PROXIM RangeLAN-DS/LAN PC CARD
	PCMCIA_DEVICE_PROD_ID1234("ACTIONTEC",  "PRISM Wireless LAN PC Card",  "0381",  "RevA", 0x393089da, 0xa71e69d5, 0x90471fa9, 0x57a66194), // ACTIONTEC PRISM Wireless LAN PC Card
	PCMCIA_DEVICE_MANF_CARD(0x1668, 0x0101), // ACTIONTEC PRISM Wireless LAN PC Card
	PCMCIA_DEVICE_PROD_ID12("3Com",  "3CRWE737A AirConnect Wireless LAN PC Card", 0x41240e5b, 0x56010af3), // 3Com AirConnect 3CRWE737A
	PCMCIA_DEVICE_PROD_ID12("3Com",  "3CRWE777A AirConnect Wireless LAN PCI Card"  , 0x41240e5b, 0xafc7c33e), // 3Com AirConnect 3CRWE777A
	PCMCIA_DEVICE_PROD_ID12("ASUS",  "802_11b_PC_CARD_25", 0x78fc06ee, 0xdb9aa842), // ASUS WL-100 802.11b WLAN  PC Card
	PCMCIA_DEVICE_PROD_ID12("ASUS",  "802_11B_CF_CARD_25", 0x78fc06ee, 0x45a50c1e), // ASUS WL-110 802.11b WLAN CF Card
	PCMCIA_DEVICE_PROD_ID12("BUFFALO",  "WLI-CF-S11G", 0x2decece3, 0x82067c18), // BUFFALO WLI-CF-S11G 802.11b WLAN Card
	PCMCIA_DEVICE_PROD_ID1234("The Linksys Group, Inc.", "Wireless Network CF Card", "ISL37300P", "RevA", 0xa5f472c2, 0x9c05598d, 0xc9049a39, 0x57a66194), // Linksys WCF11 11Mbps 802.11b WLAN Card (Prism 2.5)
	PCMCIA_DEVICE_PROD_ID1234("Linksys",  "Wireless CompactFlash Card",  "",  "", 0x733cc81, 0xc52f395, 0x0, 0x0), // Linksys WCF12 11Mbps 802.11b WLAN Card (Prism 3)
	PCMCIA_DEVICE_MANF_CARD(0x028a, 0x0673), // Linksys WCF12 11Mbps 802.11b WLAN Card (Prism 3)
	PCMCIA_DEVICE_PROD_ID1234("NETGEAR MA401RA Wireless PC",  "Card",  "ISL37300P",  "Eval-RevA", 0x306467f, 0x9762e8f1, 0xc9049a39, 0xc23adc0e), // NETGEAR MA401RA 11Mbps 802.11 WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0005), // D-Link DCF-660W  11Mbps 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0x02d2, 0x0001), // Microsoft Wireless Notebook Adapter MN-520
	PCMCIA_DEVICE_MANF_CARD(0x0089, 0x0002), // AnyPoint(TM) Wireless II PC Card
	PCMCIA_DEVICE_PROD_ID1234("D",  "Link DRC-650 11Mbps WLAN Card",  "Version 01.02",  "" , 0x71b18589, 0xf144e3ac, 0x4b74baa0, 0x0), // D-Link DRC-650 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0x9005, 0x0021), // Adaptec AWN-8030
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7110), // D-Link DWL-650 rev P 802.11b WLAN card
	// PCMCIA_DEVICE_PROD_ID1234("D-Link",  "DWL-650 Wireless PC Card RevP",  "ISL37101P-10",  "A3", 0x1a424a1c, 0x6ea57632, 0xdd97a26b, 0x56b21f52), // D-Link DWL-650 rev P 802.11b WLAN card
	PCMCIA_DEVICE_PROD_ID123("INTERSIL",   "I-GATE 11M PC Card / PC Card plus",  "Version 01.02", 0x74c5e40d, 0x8304ff77, 0x4b74baa0), // I-Gate 11M PC Card
	PCMCIA_DEVICE_PROD_ID1234("BENQ",  "AWL100 PCMCIA ADAPTER",  "ISL37300P",  "Eval-RevA", 0x35dadc74, 0x1f7fedb, 0xc9049a39, 0xc23adc0e), // benQ AWL100 802.11b WLAN Card
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7300), // benQ AWL100 802.11b WLAN Card
	// PCMCIA_DEVICE_PROD_ID1("INTERSIL", 0x74c5e40d), // Intersil Prism 2 card
	// PCMCIA_DEVICE_MANF_CARD(0x0156, 0x0002), // Intersil Prism 2 card

	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, prism2_cs_ids);
#endif

static struct pcmcia_driver prism2_cs_driver = {
	.drv = {
		.name = "prism2_cs",
	},
	.owner = THIS_MODULE,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15)
	.suspend = prism2_cs_suspend,
	.resume = prism2_cs_resume,
	.remove = prism2_cs_remove,
	.probe = prism2_cs_probe,
	.id_table = prism2_cs_ids,
#else
	.attach = prism2sta_attach,
	.detach = prism2sta_detach,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12)
	.id_table = prism2_cs_ids,
	.event =  prism2sta_event,
#endif // > 2.6.12
#endif // <= 2.6.15
};
#endif /* kernel_version */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15)
#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

#define CFG_CHECK(fn, retf) \
do { int ret = (retf); \
if (ret != 0) { \
        WLAN_LOG_DEBUG(1, "CardServices(" #fn ") returned %d\n", ret); \
        cs_error(pdev, fn, ret); \
        goto next_entry; \
} \
} while (0)

static void prism2_cs_remove(struct pcmcia_device *pdev)
{
	struct wlandevice  *wlandev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
        dev_link_t *link = dev_to_instance(pdev);
#endif

	DBFENTER;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	wlandev = pdev->priv;
#else
	wlandev = link->priv;
#endif

	if (wlandev) {
		p80211netdev_hwremoved(wlandev);
		unregister_wlandev(wlandev);
		wlan_unsetup(wlandev);
		if (wlandev->priv) {
			hfa384x_t *hw = wlandev->priv;
			wlandev->priv = NULL;
			if (hw) {
				hfa384x_destroy(hw);
				kfree(hw);
			}
		}
		kfree(wlandev);
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	pdev->priv = NULL;
        pcmcia_disable_device(pdev);
#else
        if (link->state & DEV_CONFIG) {
	        if (link->win)
			pcmcia_release_window(link->win);
		pcmcia_release_configuration(link->handle);
		if (link->io.NumPorts1)
			pcmcia_release_io(link->handle, &link->io);
		if (link->irq.AssignedIRQ)
			pcmcia_release_irq(link->handle, &link->irq);

		link->state &= ~DEV_CONFIG;
	}

	link->priv = NULL;
	kfree(link);
#endif

	DBFEXIT;
	return;
}

static int prism2_cs_suspend(struct pcmcia_device *pdev)
{
	struct wlandevice  *wlandev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
        dev_link_t *link = dev_to_instance(pdev);
#endif

	DBFENTER;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	wlandev = pdev->priv;
	prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);
#else
	wlandev = link->priv;

        link->state |= DEV_SUSPEND;
        if (link->state & DEV_CONFIG) {
		prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);
		pcmcia_release_configuration(link->handle);
	}
#endif

	DBFEXIT;

	return 0;
}

static int prism2_cs_resume(struct pcmcia_device *pdev)
{
	struct wlandevice  *wlandev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
        dev_link_t *link = dev_to_instance(pdev);
#endif

	DBFENTER;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	wlandev = pdev->priv;
	// XXX do something here?
#else
	wlandev = link->priv;
        link->state &= ~DEV_SUSPEND;
        if (link->state & DEV_CONFIG) {
                pcmcia_request_configuration(link->handle, &link->conf);
		// XXX do something here?
	}
#endif


	DBFEXIT;

	return 0;
}

static int prism2_cs_probe(struct pcmcia_device *pdev)
{
	int rval = 0;
	struct wlandevice *wlandev = NULL;
	hfa384x_t *hw = NULL;

        config_info_t socketconf;
        cisparse_t *parse = NULL;
	tuple_t tuple;
	uint8_t	buf[64];
        int last_fn, last_ret;
        cistpl_cftable_entry_t dflt = { 0 };

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
	dev_link_t *link;
#endif

	DBFENTER;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	/* Set up interrupt type */
        pdev->conf.IntType = INT_MEMORY_AND_IO;
#else
        link = kmalloc(sizeof(dev_link_t), GFP_KERNEL);
        if (link == NULL)
                return -ENOMEM;
        memset(link, 0, sizeof(dev_link_t));

        link->conf.Vcc = 33;
        link->conf.IntType = INT_MEMORY_AND_IO;

        link->handle = pdev;
        pdev->instance = link;
        link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;

#endif

	// VCC crap?
        parse = kmalloc(sizeof(cisparse_t), GFP_KERNEL);

	wlandev = create_wlan();
	if (!wlandev || !parse) {
		WLAN_LOG_ERROR("%s: Memory allocation failure.\n", dev_info);
		rval = -EIO;
		goto failed;
	}
	hw = wlandev->priv;

	if ( wlan_setup(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: wlan_setup() failed.\n", dev_info);
		rval = -EIO;
		goto failed;
	}

	/* Initialize the hw struct for now */
	hfa384x_create(hw, 0, 0, NULL);
	hw->wlandev = wlandev;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	hw->pdev = pdev;
	pdev->priv = wlandev;
#else
	hw->link = link;
	link->priv = wlandev;
#endif

        tuple.DesiredTuple = CISTPL_CONFIG;
        tuple.Attributes = 0;
        tuple.TupleData = buf;
        tuple.TupleDataMax = sizeof(buf);
        tuple.TupleOffset = 0;
        CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(pdev, &tuple));
        CS_CHECK(GetTupleData, pcmcia_get_tuple_data(pdev, &tuple));
        CS_CHECK(ParseTuple, pcmcia_parse_tuple(pdev, &tuple, parse));
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
        pdev->conf.ConfigBase = parse->config.base;
        pdev->conf.Present = parse->config.rmask[0];
#else
        link->conf.ConfigBase = parse->config.base;
        link->conf.Present = parse->config.rmask[0];

	link->conf.Vcc = socketconf.Vcc;
#endif
        CS_CHECK(GetConfigurationInfo,
                 pcmcia_get_configuration_info(pdev, &socketconf));

	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(pdev, &tuple));
        for (;;) {
		cistpl_cftable_entry_t *cfg = &(parse->cftable_entry);
                CFG_CHECK(GetTupleData,
                           pcmcia_get_tuple_data(pdev, &tuple));
                CFG_CHECK(ParseTuple,
                           pcmcia_parse_tuple(pdev, &tuple, parse));

                if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
                        dflt = *cfg;
                if (cfg->index == 0)
                        goto next_entry;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
                pdev->conf.ConfigIndex = cfg->index;
#else
                link->conf.ConfigIndex = cfg->index;
#endif

                /* Does this card need audio output? */
                if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
                        pdev->conf.Attributes |= CONF_ENABLE_SPKR;
                        pdev->conf.Status = CCSR_AUDIO_ENA;
#else
                        link->conf.Attributes |= CONF_ENABLE_SPKR;
                        link->conf.Status = CCSR_AUDIO_ENA;
#endif
                }

                /* Use power settings for Vcc and Vpp if present */
                /*  Note that the CIS values need to be rescaled */
                if (cfg->vcc.present & (1 << CISTPL_POWER_VNOM)) {
                        if (socketconf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM] /
                            10000 && !prism2_ignorevcc) {
                                WLAN_LOG_DEBUG(1, "  Vcc mismatch - skipping"
                                       " this entry\n");
                                goto next_entry;
                        }
                } else if (dflt.vcc.present & (1 << CISTPL_POWER_VNOM)) {
                        if (socketconf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM] /
                            10000 && !prism2_ignorevcc) {
                                WLAN_LOG_DEBUG(1, "  Vcc (default) mismatch "
                                       "- skipping this entry\n");
                                goto next_entry;
                        }
                }

                if (cfg->vpp1.present & (1 << CISTPL_POWER_VNOM)) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
                        pdev->conf.Vpp =
                                cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
#else
                        link->conf.Vpp1 = link->conf.Vpp2 =
                                cfg->vpp1.param[CISTPL_POWER_VNOM] / 10000;
#endif
                } else if (dflt.vpp1.present & (1 << CISTPL_POWER_VNOM)) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
                        pdev->conf.Vpp =
                                dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;
#else
                        link->conf.Vpp1 = link->conf.Vpp2 =
                                dflt.vpp1.param[CISTPL_POWER_VNOM] / 10000;
#endif
		}

		/* Do we need to allocate an interrupt? */
		/* HACK: due to a bad CIS....we ALWAYS need an interrupt */
		/* if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1) */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
		pdev->conf.Attributes |= CONF_ENABLE_IRQ;
#else
		link->conf.Attributes |= CONF_ENABLE_IRQ;
#endif

		/* IO window settings */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
		pdev->io.NumPorts1 = pdev->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			pdev->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				pdev->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				pdev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			pdev->io.BasePort1 = io->win[0].base;
			if  ( pdev->io.BasePort1 != 0 ) {
				WLAN_LOG_WARNING(
				"Brain damaged CIS: hard coded iobase="
				"0x%x, try letting pcmcia_cs decide...\n",
				pdev->io.BasePort1 );
				pdev->io.BasePort1 = 0;
			}
			pdev->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				pdev->io.Attributes2 = pdev->io.Attributes1;
				pdev->io.BasePort2 = io->win[1].base;
				pdev->io.NumPorts2 = io->win[1].len;
			}
		}
		/* This reserves IO space but doesn't actually enable it */
		CFG_CHECK(RequestIO, pcmcia_request_io(pdev, &pdev->io));
#else
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.BasePort1 = io->win[0].base;
			if  ( link->io.BasePort1 != 0 ) {
				WLAN_LOG_WARNING(
				"Brain damaged CIS: hard coded iobase="
				"0x%x, try letting pcmcia_cs decide...\n",
				link->io.BasePort1 );
				link->io.BasePort1 = 0;
			}
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
		}
		/* This reserves IO space but doesn't actually enable it */
		CFG_CHECK(RequestIO, pcmcia_request_io(pdev, &link->io));
#endif
		/* If we got this far, we're cool! */
		break;

	next_entry:
		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(pdev, &tuple));

	}

	/* Let pcmcia know the device name */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	pdev->dev_node = &hw->node;
#else
	link->dev = &hw->node;
#endif

	/* Register the network device and get assigned a name */
	SET_MODULE_OWNER(wlandev->netdev);
	SET_NETDEV_DEV(wlandev->netdev,  &handle_to_dev(pdev));
	if (register_wlandev(wlandev) != 0) {
		WLAN_LOG_NOTICE("prism2sta_cs: register_wlandev() failed.\n");
		goto failed;
	}

	strcpy(hw->node.dev_name, wlandev->name);

	/* Allocate an interrupt line.  Note that this does not assign a */
	/* handler to the interrupt, unless the 'Handler' member of the */
	/* irq structure is initialized. */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	if (pdev->conf.Attributes & CONF_ENABLE_IRQ) {
		pdev->irq.IRQInfo1 = IRQ_LEVEL_ID;
		pdev->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
		pdev->irq.Handler = hfa384x_interrupt;
		pdev->irq.Instance = wlandev;
		CS_CHECK(RequestIRQ, pcmcia_request_irq(pdev, &pdev->irq));
	}
#else
	if (link->conf.Attributes & CONF_ENABLE_IRQ) {
		link->irq.IRQInfo1 = IRQ_LEVEL_ID;
		link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
		link->irq.Handler = hfa384x_interrupt;
		link->irq.Instance = wlandev;
		CS_CHECK(RequestIRQ, pcmcia_request_irq(pdev, &link->irq));
	}
#endif

	/* This actually configures the PCMCIA socket -- setting up */
	/* the I/O windows and the interrupt mapping, and putting the */
	/* card and host interface into "Memory and IO" mode. */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(pdev, &pdev->conf));
#else
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(pdev, &link->conf));
#endif

	/* Fill the netdevice with this info */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	wlandev->netdev->irq = pdev->irq.AssignedIRQ;
	wlandev->netdev->base_addr = pdev->io.BasePort1;
#else
	wlandev->netdev->irq = link->irq.AssignedIRQ;
	wlandev->netdev->base_addr = link->io.BasePort1;
#endif

	/* And the rest of the hw structure */
	hw->irq = wlandev->netdev->irq;
	hw->iobase = wlandev->netdev->base_addr;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
	link->state |= DEV_CONFIG;
	link->state &= ~DEV_CONFIG_PENDING;
#endif

	/* And now we're done! */
	wlandev->msdstate = WLAN_MSD_HWPRESENT;

	goto done;

 cs_failed:
        cs_error(pdev, last_fn, last_ret);

failed:
	// wlandev, hw, etc etc..
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	pdev->priv = NULL;
#else
	pdev->instance = NULL;
	if (link) {
		link->priv = NULL;
		kfree(link);
	}
#endif
	if (wlandev) {
		wlan_unsetup(wlandev);
		if (wlandev->priv) {
			hw = wlandev->priv;
			wlandev->priv = NULL;
			if (hw) {
				hfa384x_destroy(hw);
				kfree(hw);
			}
		}
		kfree(wlandev);
	}

done:
	if (parse) kfree(parse);

	DBFEXIT;
	return rval;
}
#else  // <= 2.6.15
#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

#define CFG_CHECK(fn, retf) \
do { int ret = (retf); \
if (ret != 0) { \
        WLAN_LOG_DEBUG(1, "CardServices(" #fn ") returned %d\n", ret); \
        cs_error(link->handle, fn, ret); \
        goto next_entry; \
} \
} while (0)

/*----------------------------------------------------------------
* prism2sta_attach
*
* Half of the attach/detach pair.  Creates and registers a device
* instance with Card Services.  In this case, it also creates the
* wlandev structure and device private structure.  These are
* linked to the device instance via its priv member.
*
* Arguments:
*	none
*
* Returns:
*	A valid ptr to dev_link_t on success, NULL otherwise
*
* Side effects:
*
*
* Call context:
*	process thread (insmod/init_module/register_pccard_driver)
----------------------------------------------------------------*/
dev_link_t *prism2sta_attach(void)
{
	client_reg_t		client_reg;
	int			result;
	dev_link_t		*link = NULL;
	wlandevice_t		*wlandev = NULL;
	hfa384x_t		*hw = NULL;

	DBFENTER;

	/* Alloc our structures */
	link =		kmalloc(sizeof(struct dev_link_t), GFP_KERNEL);

	if (!link || ((wlandev = create_wlan()) == NULL)) {
		WLAN_LOG_ERROR("%s: Memory allocation failure.\n", dev_info);
		result = -EIO;
		goto failed;
	}
	hw = wlandev->priv;

	/* Clear all the structs */
	memset(link, 0, sizeof(struct dev_link_t));

	if ( wlan_setup(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: wlan_setup() failed.\n", dev_info);
		result = -EIO;
		goto failed;
	}

	/* Initialize the hw struct for now */
	hfa384x_create(hw, 0, 0, NULL);
	hw->wlandev = wlandev;

	/* Initialize the PC card device object. */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
	init_timer(&link->release);
	link->release.function = &prism2sta_release;
	link->release.data = (u_long)link;
#endif
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->priv = wlandev;
#if (defined(CS_RELEASE_CODE) && (CS_RELEASE_CODE < 0x2911))
	link->irq.Instance = wlandev;
#endif

	/* Link in to the list of devices managed by this driver */
	link->next = dev_list;
	dev_list = link;

	/* Register with Card Services */
	client_reg.dev_info = &dev_info;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11) )
	client_reg.Attributes = INFO_IO_CLIENT | INFO_CARD_SHARE;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13) )
	client_reg.EventMask =
		CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
		CS_EVENT_RESET_REQUEST |
		CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
		CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = &prism2sta_event;
#endif

	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;

	result = pcmcia_register_client(&link->handle, &client_reg);
	if (result != 0) {
		cs_error(link->handle, RegisterClient, result);
		prism2sta_detach(link);
		return NULL;
	}

	goto done;

 failed:
	if (link)	kfree(link);
	if (wlandev)	kfree(wlandev);
	if (hw)		kfree(hw);
	link = NULL;

 done:
	DBFEXIT;
	return link;
}


/*----------------------------------------------------------------
* prism2sta_detach
*
* Remove one of the device instances managed by this driver.
*   Search the list for the given instance,
*   check our flags for a waiting timer'd release call
*   call release
*   Deregister the instance with Card Services
*   (netdevice) unregister the network device.
*   unlink the instance from the list
*   free the link, priv, and priv->priv memory
* Note: the dev_list variable is a driver scoped static used to
*	maintain a list of device instances managed by this
*	driver.
*
* Arguments:
*	link	ptr to the instance to detach
*
* Returns:
*	nothing
*
* Side effects:
*	the link structure is gone, the netdevice is gone
*
* Call context:
*	Might be interrupt, don't block.
----------------------------------------------------------------*/
void prism2sta_detach(dev_link_t *link)
{
	dev_link_t		**linkp;
	wlandevice_t		*wlandev;
	hfa384x_t		*hw;

	DBFENTER;

	/* Locate prev device structure */
	for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next) {
		if (*linkp == link) break;
	}

	if (*linkp != NULL) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
		unsigned long	flags;
		/* Get rid of any timer'd release call */
		save_flags(flags);
		cli();
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
		if (link->state & DEV_RELEASE_PENDING) {
			del_timer_sync(&link->release);
			link->state &= ~DEV_RELEASE_PENDING;
		}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
		restore_flags(flags);
#endif

		/* If link says we're still config'd, call release */
		if (link->state & DEV_CONFIG) {
			prism2sta_release((u_long)link);
			if (link->state & DEV_STALE_CONFIG) {
				link->state |= DEV_STALE_LINK;
				return;
			}
		}

		/* Tell Card Services we're not around any more */
		if (link->handle) {
			pcmcia_deregister_client(link->handle);
		}

		/* Unlink device structure, free bits */
		*linkp = link->next;
		if ( link->priv != NULL ) {
			wlandev = (wlandevice_t*)link->priv;
			p80211netdev_hwremoved(wlandev);
			if (link->dev != NULL) {
				unregister_wlandev(wlandev);
			}
			wlan_unsetup(wlandev);
			if (wlandev->priv) {
				hw = wlandev->priv;
				wlandev->priv = NULL;
				if (hw) {
					hfa384x_destroy(hw);
					kfree(hw);
				}
			}
			link->priv = NULL;
			kfree(wlandev);
		}
		kfree(link);
	}

	DBFEXIT;
	return;
}

/*----------------------------------------------------------------
* prism2sta_config
*
* Half of the config/release pair.  Usually called in response to
* a card insertion event.  At this point, we _know_ there's some
* physical device present.  That means we can start poking around
* at the CIS and at any device specific config data we want.
*
* Note the gotos and the macros.  I recoded this once without
* them, and it got incredibly ugly.  It's actually simpler with
* them.
*
* Arguments:
*	link	the dev_link_t structure created in attach that
*		represents this device instance.
*
* Returns:
*	nothing
*
* Side effects:
*	Resources (irq, io, mem) are allocated
*	The pcmcia dev_link->node->name is set
*	(For netcards) The device structure is finished and,
*	  most importantly, registered.  This means that there
*	  is now a _named_ device that can be configured from
*	  userland.
*
* Call context:
*	May be called from a timer.  Don't block!
----------------------------------------------------------------*/
#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

#define CFG_CHECK(fn, retf) \
do { int ret = (retf); \
if (ret != 0) { \
        WLAN_LOG_DEBUG(1, "CardServices(" #fn ") returned %d\n", ret); \
        cs_error(link->handle, fn, ret); \
        goto next_entry; \
} \
} while (0)

void prism2sta_config(dev_link_t *link)
{
	client_handle_t		handle;
	wlandevice_t		*wlandev;
	hfa384x_t               *hw;
	int			last_fn;
	int			last_ret;
	tuple_t			tuple;
	cisparse_t		parse;
	config_info_t		socketconf;
	UINT8			buf[64];
	int			minVcc = 0;
	int			maxVcc = 0;
	cistpl_cftable_entry_t	dflt = { 0 };

	DBFENTER;

	handle = link->handle;
	wlandev = (wlandevice_t*)link->priv;
	hw = wlandev->priv;

	/* Collect the config register info */
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

	/* Acquire the current socket config (need Vcc setting) */
	CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(handle, &socketconf));

	/* Loop through the config table entries until we find one that works */
	/* Assumes a complete and valid CIS */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
	while (1) {
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		CFG_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
		CFG_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));

		if (cfg->index == 0) goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Lets print out the Vcc that the controller+pcmcia-cs set
		 * for us, cause that's what we're going to use.
		 */
		WLAN_LOG_DEBUG(1,"Initial Vcc=%d/10v\n", socketconf.Vcc);
		if (prism2_ignorevcc) {
			link->conf.Vcc = socketconf.Vcc;
			goto skipvcc;
		}

		/* Use power settings for Vcc and Vpp if present */
		/* Note that the CIS values need to be rescaled */
		if (cfg->vcc.present & (1<<CISTPL_POWER_VNOM)) {
			WLAN_LOG_DEBUG(1, "Vcc obtained from curtupl.VNOM\n");
			minVcc = maxVcc =
				cfg->vcc.param[CISTPL_POWER_VNOM]/10000;
		} else if (dflt.vcc.present & (1<<CISTPL_POWER_VNOM)) {
			WLAN_LOG_DEBUG(1, "Vcc set from dflt.VNOM\n");
			minVcc = maxVcc =
				dflt.vcc.param[CISTPL_POWER_VNOM]/10000;
		} else if ((cfg->vcc.present & (1<<CISTPL_POWER_VMAX)) &&
			   (cfg->vcc.present & (1<<CISTPL_POWER_VMIN)) ) {
			WLAN_LOG_DEBUG(1, "Vcc set from curtupl(VMIN,VMAX)\n");			minVcc = cfg->vcc.param[CISTPL_POWER_VMIN]/10000;
			maxVcc = cfg->vcc.param[CISTPL_POWER_VMAX]/10000;
		} else if ((dflt.vcc.present & (1<<CISTPL_POWER_VMAX)) &&
			   (dflt.vcc.present & (1<<CISTPL_POWER_VMIN)) ) {
			WLAN_LOG_DEBUG(1, "Vcc set from dflt(VMIN,VMAX)\n");
			minVcc = dflt.vcc.param[CISTPL_POWER_VMIN]/10000;
			maxVcc = dflt.vcc.param[CISTPL_POWER_VMAX]/10000;
		}

		if ( socketconf.Vcc >= minVcc && socketconf.Vcc <= maxVcc) {
			link->conf.Vcc = socketconf.Vcc;
		} else {
			/* [MSM]: Note that I've given up trying to change
			 * the Vcc if a change is indicated.  It seems the
			 * system&socketcontroller&card vendors can't seem
			 * to get it right, so I'm tired of trying to hack
			 * my way around it.  pcmcia-cs does its best using
			 * the voltage sense pins but sometimes the controller
			 * lies.  Then, even if we have a good read on the VS
			 * pins, some system designs will silently ignore our
			 * requests to set the voltage.  Additionally, some
			 * vendors have 3.3v indicated on their sense pins,
			 * but 5v specified in the CIS or vice-versa.  I've
			 * had it.  My only recommendation is "let the buyer
			 * beware".  Your system might supply 5v to a 3v card
			 * (possibly causing damage) or a 3v capable system
			 * might supply 5v to a 3v capable card (wasting
			 * precious battery life).
			 * My only recommendation (if you care) is to get
			 * yourself an extender card (I don't know where, I
			 * have only one myself) and a meter and test it for
			 * yourself.
			 */
			goto next_entry;
		}
skipvcc:
		WLAN_LOG_DEBUG(1, "link->conf.Vcc=%d\n", link->conf.Vcc);

		/* Do we need to allocate an interrupt? */
		/* HACK: due to a bad CIS....we ALWAYS need an interrupt */
		/* if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1) */
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.BasePort1 = io->win[0].base;
			if  ( link->io.BasePort1 != 0 ) {
				WLAN_LOG_WARNING(
				"Brain damaged CIS: hard coded iobase="
				"0x%x, try letting pcmcia_cs decide...\n",
				link->io.BasePort1 );
				link->io.BasePort1 = 0;
			}
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
		}

		/* This reserves IO space but doesn't actually enable it */
		CFG_CHECK(RequestIO, pcmcia_request_io(link->handle, &link->io));

		/* If we got this far, we're cool! */
		break;

next_entry:
		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		CS_CHECK(GetNextTuple,
                         pcmcia_get_next_tuple(handle, &tuple));
	}

	/* Allocate an interrupt line.  Note that this does not assign a */
	/* handler to the interrupt, unless the 'Handler' member of the */
	/* irq structure is initialized. */
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11) )
		int			i;
		link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;
		if (irq_list[0] == -1)
			link->irq.IRQInfo2 = irq_mask;
		else
			for (i=0; i<4; i++)
				link->irq.IRQInfo2 |= 1 << irq_list[i];
#else
		link->irq.IRQInfo1 = IRQ_LEVEL_ID;
#endif
		link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
		link->irq.Handler = hfa384x_interrupt;
		link->irq.Instance = wlandev;
		CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));
	}

	/* This actually configures the PCMCIA socket -- setting up */
	/* the I/O windows and the interrupt mapping, and putting the */
	/* card and host interface into "Memory and IO" mode. */
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link->handle, &link->conf));

	/* Fill the netdevice with this info */
	wlandev->netdev->irq = link->irq.AssignedIRQ;
	wlandev->netdev->base_addr = link->io.BasePort1;

	/* Report what we've done */
	WLAN_LOG_INFO("%s: index 0x%02x: Vcc %d.%d",
		dev_info, link->conf.ConfigIndex,
		link->conf.Vcc/10, link->conf.Vcc%10);
	if (link->conf.Vpp1)
		printk(", Vpp %d.%d", link->conf.Vpp1/10, link->conf.Vpp1%10);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %d", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1, link->io.BasePort1+link->io.NumPorts1-1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2, link->io.BasePort2+link->io.NumPorts2-1);
	printk("\n");

	link->state &= ~DEV_CONFIG_PENDING;

	/* Let pcmcia know the device name */
	link->dev = &hw->node;

	/* Register the network device and get assigned a name */
	SET_MODULE_OWNER(wlandev->netdev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11) )
	SET_NETDEV_DEV(wlandev->netdev,  &handle_to_dev(link->handle));
#endif
	if (register_wlandev(wlandev) != 0) {
		WLAN_LOG_NOTICE("prism2sta_cs: register_wlandev() failed.\n");
		goto failed;
	}

	strcpy(hw->node.dev_name, wlandev->name);

	/* Any device custom config/query stuff should be done here */
	/* For a netdevice, we should at least grab the mac address */

	return;
cs_failed:
	cs_error(link->handle, last_fn, last_ret);
	WLAN_LOG_ERROR("NextTuple failure? It's probably a Vcc mismatch.\n");

failed:
	prism2sta_release((u_long)link);
	return;
}

/*----------------------------------------------------------------
* prism2sta_release
*
* Half of the config/release pair.  Usually called in response to
* a card ejection event.  Checks to make sure no higher layers
* are still (or think they are) using the card via the link->open
* field.
*
* NOTE: Don't forget to increment the link->open variable in the
*  device_open method, and decrement it in the device_close
*  method.
*
* Arguments:
*	arg	a generic 32 bit variable.  It's the value that
*		we assigned to link->release.data in sta_attach().
*
* Returns:
*	nothing
*
* Side effects:
*	All resources should be released after this function
*	executes and finds the device !open.
*
* Call context:
*	Possibly in a timer context.  Don't do anything that'll
*	block.
----------------------------------------------------------------*/
void prism2sta_release(u_long arg)
{
        dev_link_t	*link = (dev_link_t *)arg;

	DBFENTER;

	/* First thing we should do is get the MSD back to the
	 * HWPRESENT state.  I.e. everything quiescent.
	 */
	prism2sta_ifstate(link->priv, P80211ENUM_ifstate_disable);

        if (link->open) {
		/* TODO: I don't think we're even using this bit of code
		 * and I don't think it's hurting us at the moment.
		 */
                WLAN_LOG_DEBUG(1,
			"prism2sta_cs: release postponed, '%s' still open\n",
			link->dev->dev_name);
                link->state |= DEV_STALE_CONFIG;
                return;
        }

        pcmcia_release_configuration(link->handle);
        pcmcia_release_io(link->handle, &link->io);
        pcmcia_release_irq(link->handle, &link->irq);

        link->state &= ~(DEV_CONFIG | DEV_RELEASE_PENDING);

	DBFEXIT;
}

/*----------------------------------------------------------------
* prism2sta_event
*
* Handler for card services events.
*
* Arguments:
*	event		The event code
*	priority	hi/low - REMOVAL is the only hi
*	args		ptr to card services struct containing info about
*			pcmcia status
*
* Returns:
*	Zero on success, non-zero otherwise
*
* Side effects:
*
*
* Call context:
*	Both interrupt and process thread, depends on the event.
----------------------------------------------------------------*/
static int
prism2sta_event (
	event_t event,
	int priority,
	event_callback_args_t *args)
{
	int			result = 0;
	dev_link_t		*link = (dev_link_t *) args->client_data;
	wlandevice_t		*wlandev = (wlandevice_t*)link->priv;
	hfa384x_t		*hw = NULL;

	DBFENTER;

	if (wlandev) hw = wlandev->priv;

	switch (event)
	{
	case CS_EVENT_CARD_INSERTION:
		WLAN_LOG_DEBUG(5,"event is INSERTION\n");
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		prism2sta_config(link);
		if (!(link->state & DEV_CONFIG)) {
			wlandev->netdev->irq = 0;
			WLAN_LOG_ERROR(
				"%s: Initialization failed!\n", dev_info);
			wlandev->msdstate = WLAN_MSD_HWFAIL;
			break;
		}

		/* Fill in the rest of the hw struct */
		hw->irq = wlandev->netdev->irq;
		hw->iobase = wlandev->netdev->base_addr;
		hw->link = link;

		if (prism2_doreset) {
			result = hfa384x_corereset(hw,
					prism2_reset_holdtime,
					prism2_reset_settletime, 0);
			if ( result ) {
				WLAN_LOG_ERROR(
					"corereset() failed, result=%d.\n",
					result);
				wlandev->msdstate = WLAN_MSD_HWFAIL;
				break;
			}
		}

#if 0
		/*
		 * TODO: test_hostif() not implemented yet.
		 */
		result = hfa384x_test_hostif(hw);
		if (result) {
			WLAN_LOG_ERROR(
			"test_hostif() failed, result=%d.\n", result);
			wlandev->msdstate = WLAN_MSD_HWFAIL;
			break;
		}
#endif
		wlandev->msdstate = WLAN_MSD_HWPRESENT;
		break;

	case CS_EVENT_CARD_REMOVAL:
		WLAN_LOG_DEBUG(5,"event is REMOVAL\n");
		link->state &= ~DEV_PRESENT;

		if (wlandev) {
			p80211netdev_hwremoved(wlandev);
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
		if (link->state & DEV_CONFIG)
		{
			link->release.expires = jiffies + (HZ/20);
			add_timer(&link->release);
		}
#endif
		break;
	case CS_EVENT_RESET_REQUEST:
		WLAN_LOG_DEBUG(5,"event is RESET_REQUEST\n");
		WLAN_LOG_NOTICE(
			"prism2 card reset not supported "
			"due to post-reset user mode configuration "
			"requirements.\n");
		WLAN_LOG_NOTICE(
			"  From user mode, use "
			"'cardctl suspend;cardctl resume' "
			"instead.\n");
		break;
	case CS_EVENT_RESET_PHYSICAL:
	case CS_EVENT_CARD_RESET:
		WLAN_LOG_WARNING("Rx'd CS_EVENT_RESET_xxx, should not "
			"be possible since RESET_REQUEST was denied.\n");
		break;

	case CS_EVENT_PM_SUSPEND:
		WLAN_LOG_DEBUG(5,"event is SUSPEND\n");
		link->state |= DEV_SUSPEND;
		if (link->state & DEV_CONFIG)
		{
			prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);
			pcmcia_release_configuration(link->handle);
		}
		break;

	case CS_EVENT_PM_RESUME:
		WLAN_LOG_DEBUG(5,"event is RESUME\n");
		link->state &= ~DEV_SUSPEND;
		if (link->state & DEV_CONFIG) {
			pcmcia_request_configuration(link->handle, &link->conf);
		}
		break;
	}

	DBFEXIT;
	return 0;  /* noone else does anthing with the return value */
}
#endif // <= 2.6.15



int hfa384x_corereset(hfa384x_t *hw, int holdtime, int settletime, int genesis)
{
	int		result = 0;
	conf_reg_t	reg;
	UINT8		corsave;
	DBFENTER;

	WLAN_LOG_DEBUG(3, "Doing reset via CardServices().\n");

	/* Collect COR */
	reg.Function = 0;
	reg.Action = CS_READ;
	reg.Offset = CISREG_COR;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	result = pcmcia_access_configuration_register(hw->pdev, &reg);
#else
	result = pcmcia_access_configuration_register(
			hw->link->handle,
			&reg);
#endif
	if (result != CS_SUCCESS ) {
		WLAN_LOG_ERROR(
			":0: AccessConfigurationRegister(CS_READ) failed,"
			"result=%d.\n", result);
		result = -EIO;
	}
	corsave = reg.Value;

	/* Write reset bit (BIT7) */
	reg.Value |= BIT7;
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	result = pcmcia_access_configuration_register(hw->pdev, &reg);
#else
	result = pcmcia_access_configuration_register(
			hw->link->handle,
			&reg);
#endif
	if (result != CS_SUCCESS ) {
		WLAN_LOG_ERROR(
			":1: AccessConfigurationRegister(CS_WRITE) failed,"
			"result=%d.\n", result);
		result = -EIO;
	}

	/* Hold for holdtime */
	mdelay(holdtime);

	if (genesis) {
		reg.Value = genesis;
		reg.Action = CS_WRITE;
		reg.Offset = CISREG_CCSR;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
		result = pcmcia_access_configuration_register(hw->pdev, &reg);
#else
		result = pcmcia_access_configuration_register(
				      hw->link->handle,
				      &reg);
#endif
		if (result != CS_SUCCESS ) {
			WLAN_LOG_ERROR(
				":1: AccessConfigurationRegister(CS_WRITE) failed,"
				"result=%d.\n", result);
			result = -EIO;
		}
	}

	/* Hold for holdtime */
	mdelay(holdtime);

	/* Clear reset bit */
	reg.Value &= ~BIT7;
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	result = pcmcia_access_configuration_register(hw->pdev, &reg);
#else
	result = pcmcia_access_configuration_register(
				      hw->link->handle,
				      &reg);
#endif
	if (result != CS_SUCCESS ) {
		WLAN_LOG_ERROR(
			":2: AccessConfigurationRegister(CS_WRITE) failed,"
			"result=%d.\n", result);
		result = -EIO;
		goto done;
	}

	/* Wait for settletime */
	mdelay(settletime);

	/* Set non-reset bits back what they were */
	reg.Value = corsave;
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16)
	result = pcmcia_access_configuration_register(hw->pdev, &reg);
#else
	result = pcmcia_access_configuration_register(
				      hw->link->handle,
				      &reg);
#endif
	if (result != CS_SUCCESS ) {
		WLAN_LOG_ERROR(
			":2: AccessConfigurationRegister(CS_WRITE) failed,"
			"result=%d.\n", result);
		result = -EIO;
		goto done;
	}

done:
	DBFEXIT;
	return result;
}

#ifdef MODULE

static int __init prism2cs_init(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68))
	servinfo_t	serv;
#endif

	DBFENTER;

        WLAN_LOG_NOTICE("%s Loaded\n", version);
        WLAN_LOG_NOTICE("dev_info is: %s\n", dev_info);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68))
	pcmcia_get_card_services_info(&serv);
	if ( serv.Revision != CS_RELEASE_CODE )
	{
		printk(KERN_NOTICE"%s: CardServices release does not match!\n", dev_info);
		return -1;
	}

	/* This call will result in a call to prism2sta_attach */
	/*   and eventually prism2sta_detach */
	register_pccard_driver( &dev_info, &prism2sta_attach, &prism2sta_detach);
#else
	pcmcia_register_driver(&prism2_cs_driver);
#endif

	DBFEXIT;
	return 0;
}

static void __exit prism2cs_cleanup(void)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,68))
        dev_link_t *link = dev_list;
        dev_link_t *nlink;
        DBFENTER;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10) )
	for (link=dev_list; link != NULL; link = nlink) {
		nlink = link->next;
		if ( link->state & DEV_CONFIG ) {
			prism2sta_release((u_long)link);
		}
		prism2sta_detach(link); /* remember detach() frees link */
	}
#endif

	unregister_pccard_driver( &dev_info);
#else
	pcmcia_unregister_driver(&prism2_cs_driver);
#endif

        printk(KERN_NOTICE "%s Unloaded\n", version);

        DBFEXIT;
        return;
}

module_init(prism2cs_init);
module_exit(prism2cs_cleanup);

#endif // MODULE

