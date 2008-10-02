#define WLAN_HOSTIF WLAN_PLX
#include "hfa384x.c"
#include "prism2mgmt.c"
#include "prism2mib.c"
#include "prism2sta.c"

#define PLX_ATTR_SIZE	0x1000	/* Attribute memory size - 4K bytes */
#define COR_OFFSET	0x3e0	/* COR attribute offset of Prism2 PC card */
#define COR_VALUE	0x41	/* Enable PC card with irq in level trigger */
#define PLX_INTCSR	0x4c	/* Interrupt Control and Status Register */
#define PLX_INTCSR_INTEN (1<<6) /* Interrupt Enable bit */
#define PLX_MIN_ATTR_LEN 512    /* at least 2 x 256 is needed for CIS */

/* 3Com 3CRW777A (PLX) board ID */
#define PCIVENDOR_3COM       0x10B7
#define PCIDEVICE_AIRCONNECT 0x7770

/* Eumitcom PCI WL11000 PCI Adapter (PLX) board device+vendor ID */
#define PCIVENDOR_EUMITCOM	0x1638UL
#define PCIDEVICE_WL11000	0x1100UL

/* Global Sun Tech GL24110P PCI Adapter (PLX) board device+vendor ID */
#define PCIVENDOR_GLOBALSUN	0x16abUL
#define PCIDEVICE_GL24110P	0x1101UL
#define PCIDEVICE_GL24110P_ALT	0x1102UL

/* Netgear MA301 PCI Adapter (PLX) board device+vendor ID */
#define PCIVENDOR_NETGEAR	0x1385UL
#define PCIDEVICE_MA301		0x4100UL

/* US Robotics USR2410 PCI Adapter (PLX) board device+vendor ID */
#define	PCIVENDOR_USROBOTICS    0x16ecUL
#define PCIDEVICE_USR2410       0x3685UL

/* Linksys WPC11 card with the WDT11 adapter (PLX) board device+vendor ID */
#define	PCIVENDOR_Linksys       0x16abUL
#define PCIDEVICE_Wpc11Wdt11    0x1102UL

/* National Datacomm Corp SOHOware Netblaster II PCI */
#define PCIVENDOR_NDC	        0x15e8UL
#define PCIDEVICE_NCP130_PLX 	0x0130UL
#define PCIDEVICE_NCP130_ASIC	0x0131UL

/* NDC NCP130_PLX is also sold by Corega. Their name is CGWLPCIA11 */
#define PCIVENDOR_COREGA       PCIVENDOR_NDC
#define PCIDEVICE_CGWLPCIA11   PCIDEVICE_NCP130_PLX

/* PCI Class & Sub-Class code, Network-'Other controller' */
#define PCI_CLASS_NETWORK_OTHERS 0x280

/*----------------------------------------------------------------
* prism2sta_probe_plx
*
* Probe routine called when a PCI device w/ matching ID is found.
* This PLX implementation uses the following map:
*   BAR0: Unused
*   BAR1: ????
*   BAR2: PCMCIA attribute memory
*   BAR3: PCMCIA i/o space
* Here's the sequence:
*   - Allocate the PCI resources.
*   - Read the PCMCIA attribute memory to make sure we have a WLAN card
*   - Reset the MAC using the PCMCIA COR
*   - Initialize the netdev and wlan data
*   - Initialize the MAC
*
* Arguments:
*	pdev		ptr to pci device structure containing info about
*			pci configuration.
*	id		ptr to the device id entry that matched this device.
*
* Returns:
*	zero		- success
*	negative	- failed
*
* Side effects:
*
*
* Call context:
*	process thread
*
----------------------------------------------------------------*/
static int __devinit
prism2sta_probe_plx(
	struct pci_dev			*pdev,
	const struct pci_device_id	*id)
{
	int		result;
        phys_t	pccard_ioaddr;
	phys_t  pccard_attr_mem;
        unsigned int    pccard_attr_len;
	void __iomem *attr_mem = NULL;
	UINT32		plx_addr;
        wlandevice_t    *wlandev = NULL;
	hfa384x_t	*hw = NULL;
	int		reg;
        u32		regic;

	if (pci_enable_device(pdev))
		return -EIO;

	/* TMC7160 boards are special */
	if ((pdev->vendor == PCIVENDOR_NDC) &&
	    (pdev->device == PCIDEVICE_NCP130_ASIC)) {
		unsigned long delay;

		pccard_attr_mem = 0;
		pccard_ioaddr = pci_resource_start(pdev, 1);

		outb(0x45, pccard_ioaddr);
		delay = jiffies + 1*HZ;
		while (time_before(jiffies, delay));

		if (inb(pccard_ioaddr) != 0x45) {
			WLAN_LOG_ERROR("Initialize the TMC7160 failed. (0x%x)\n", inb(pccard_ioaddr));
			return -EIO;
		}

		pccard_ioaddr = pci_resource_start(pdev, 2);
		prism2_doreset = 0;

		WLAN_LOG_INFO("NDC NCP130 with TMC716(ASIC) PCI interface device found at io:0x%x, irq:%d\n", pccard_ioaddr, pdev->irq);
		goto init;
	}

	/* Collect the resource requirements */
	pccard_attr_mem = pci_resource_start(pdev, 2);
	pccard_attr_len = pci_resource_len(pdev, 2);
        if (pccard_attr_len < PLX_MIN_ATTR_LEN)
		return -EIO;

	pccard_ioaddr = pci_resource_start(pdev, 3);

	/* bjoern: We need to tell the card to enable interrupts, in
	 * case the serial eprom didn't do this already. See the
	 * PLX9052 data book, p8-1 and 8-24 for reference.
	 * [MSM]: This bit of code came from the orinoco_cs driver.
	 */
	plx_addr = pci_resource_start(pdev, 1);

	regic = 0;
	regic = inl(plx_addr+PLX_INTCSR);
	if(regic & PLX_INTCSR_INTEN) {
		WLAN_LOG_DEBUG(1,
			"%s: Local Interrupt already enabled\n", dev_info);
	} else {
		regic |= PLX_INTCSR_INTEN;
		outl(regic, plx_addr+PLX_INTCSR);
		regic = inl(plx_addr+PLX_INTCSR);
		if(!(regic & PLX_INTCSR_INTEN)) {
			WLAN_LOG_ERROR(
				"%s: Couldn't enable Local Interrupts\n",
				dev_info);
			return -EIO;
		}
	}

	/* These assignments are here in case of future mappings for
	 * io space and irq that might be similar to ioremap
	 */
        if (!request_mem_region(pccard_attr_mem, pci_resource_len(pdev, 2), "Prism2")) {
		WLAN_LOG_ERROR("%s: Couldn't reserve PCI memory region\n", dev_info);
		return -EIO;
        }

	attr_mem = ioremap(pccard_attr_mem, pccard_attr_len);

	WLAN_LOG_INFO("A PLX PCI/PCMCIA interface device found, "
		"phymem:0x%llx, phyio=0x%x, irq:%d, "
		"mem: 0x%lx\n",
		(unsigned long long)pccard_attr_mem, pccard_ioaddr, pdev->irq,
		(unsigned long)attr_mem);

	/* Verify whether PC card is present.
	 * [MSM] This needs improvement, the right thing to do is
	 * probably to walk the CIS looking for the vendor and product
	 * IDs.  It would be nice if this could be tied in with the
	 * etc/pcmcia/wlan-ng.conf file.  Any volunteers?  ;-)
	 */
	if (
	readb(attr_mem + 0) != 0x01 || readb(attr_mem + 2) != 0x03 ||
	readb(attr_mem + 4) != 0x00 || readb(attr_mem + 6) != 0x00 ||
	readb(attr_mem + 8) != 0xFF || readb(attr_mem + 10) != 0x17 ||
	readb(attr_mem + 12) != 0x04 || readb(attr_mem + 14) != 0x67) {
		WLAN_LOG_ERROR("Prism2 PC card CIS is invalid.\n");
		return -EIO;
        }
        WLAN_LOG_INFO("A PCMCIA WLAN adapter was found.\n");

        /* Write COR to enable PC card */
	writeb(COR_VALUE, attr_mem + COR_OFFSET);
	reg = readb(attr_mem + COR_OFFSET);

 init:

	/*
	 * Now do everything the same as a PCI device
	 * [MSM] TODO: We could probably factor this out of pcmcia/pci/plx
	 * and perhaps usb.  Perhaps a task for another day.......
	 */

	if ((wlandev = create_wlan()) == NULL) {
		WLAN_LOG_ERROR("%s: Memory allocation failure.\n", dev_info);
		result = -EIO;
		goto failed;
	}

	hw = wlandev->priv;

	if ( wlan_setup(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: wlan_setup() failed.\n", dev_info);
		result = -EIO;
		goto failed;
	}

	/* Setup netdevice's ability to report resources
	 * Note: the netdevice was allocated by wlan_setup()
	 */
        wlandev->netdev->irq = pdev->irq;
        wlandev->netdev->base_addr = pccard_ioaddr;
        wlandev->netdev->mem_start = (unsigned long)attr_mem;
        wlandev->netdev->mem_end = (unsigned long)attr_mem + pci_resource_len(pdev, 0);

	/* Initialize the hw data */
        hfa384x_create(hw, wlandev->netdev->irq, pccard_ioaddr, attr_mem);
	hw->wlandev = wlandev;

	/* Register the wlandev, this gets us a name and registers the
	 * linux netdevice.
	 */
	SET_MODULE_OWNER(wlandev->netdev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
       SET_NETDEV_DEV(wlandev->netdev, &(pdev->dev));
#endif
        if ( register_wlandev(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: register_wlandev() failed.\n", dev_info);
		result = -EIO;
		goto failed;
        }

#if 0
	/* TODO: Move this and an irq test into an hfa384x_testif() routine.
	 */
	outw(PRISM2STA_MAGIC, HFA384x_SWSUPPORT(wlandev->netdev->base_addr));
	reg=inw( HFA384x_SWSUPPORT(wlandev->netdev->base_addr));
	if ( reg != PRISM2STA_MAGIC ) {
		WLAN_LOG_ERROR("MAC register access test failed!\n");
 		result = -EIO;
		goto failed;
	}
#endif

	/* Do a chip-level reset on the MAC */
	if (prism2_doreset) {
		result = hfa384x_corereset(hw,
				prism2_reset_holdtime,
				prism2_reset_settletime, 0);
		if (result != 0) {
			unregister_wlandev(wlandev);
			hfa384x_destroy(hw);
			WLAN_LOG_ERROR(
				"%s: hfa384x_corereset() failed.\n",
				dev_info);
			result = -EIO;
			goto failed;
		}
	}

	pci_set_drvdata(pdev, wlandev);

	/* Shouldn't actually hook up the IRQ until we
	 * _know_ things are alright.  A test routine would help.
	 */
       	request_irq(wlandev->netdev->irq, hfa384x_interrupt,
		SA_SHIRQ, wlandev->name, wlandev);

	wlandev->msdstate = WLAN_MSD_HWPRESENT;

	result = 0;

	goto done;

 failed:

	pci_set_drvdata(pdev, NULL);
	if (wlandev)	kfree(wlandev);
	if (hw)		kfree(hw);
        if (attr_mem)        iounmap(attr_mem);
	pci_release_regions(pdev);
        pci_disable_device(pdev);

 done:
	DBFEXIT;
        return result;
}

static void __devexit prism2sta_remove_plx(struct pci_dev *pdev)
{
       	wlandevice_t		*wlandev;
	hfa384x_t               *hw;

	wlandev = (wlandevice_t *) pci_get_drvdata(pdev);
	hw = wlandev->priv;

	p80211netdev_hwremoved(wlandev);

	/* reset hardware */
	prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);

        if (pdev->irq)
		free_irq(pdev->irq, wlandev);

	unregister_wlandev(wlandev);

	/* free local stuff */
	if (hw) {
		hfa384x_destroy(hw);
		kfree(hw);
	}

	iounmap((void __iomem *)wlandev->netdev->mem_start);
	wlan_unsetup(wlandev);

	pci_release_regions(pdev);
        pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	kfree(wlandev);
}

static struct pci_device_id plx_id_tbl[] = {
	{
		PCIVENDOR_EUMITCOM, PCIDEVICE_WL11000,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Eumitcom WL11000 PCI(PLX) card"
	},
	{
		PCIVENDOR_GLOBALSUN, PCIDEVICE_GL24110P,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Global Sun Tech GL24110P PCI(PLX) card"
	},
	{
		PCIVENDOR_GLOBALSUN, PCIDEVICE_GL24110P_ALT,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Global Sun Tech GL24110P PCI(PLX) card"
	},
	{
		PCIVENDOR_NETGEAR, PCIDEVICE_MA301,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Global Sun Tech GL24110P PCI(PLX) card"
	},
	{
		PCIVENDOR_USROBOTICS, PCIDEVICE_USR2410,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"US Robotics USR2410 PCI(PLX) card"
	},
	{
		PCIVENDOR_Linksys, PCIDEVICE_Wpc11Wdt11,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Linksys WPC11 with WDT11 PCI(PLX) adapter"
	},
	{
	        PCIVENDOR_NDC, PCIDEVICE_NCP130_PLX,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"NDC Netblaster II PCI(PLX)"
	},
	{
	        PCIVENDOR_NDC, PCIDEVICE_NCP130_ASIC,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"NDC Netblaster II PCI(TMC7160)"
	},
	{
		PCIVENDOR_3COM, PCIDEVICE_AIRCONNECT,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"3Com AirConnect PCI 802.11b 11Mb/s WLAN Controller"
	},
	{
		0, 0, 0, 0, 0, 0, 0
	}
};

MODULE_DEVICE_TABLE(pci, plx_id_tbl);

/* Function declared here because of ptr reference below */
static int __devinit prism2sta_probe_plx(struct pci_dev *pdev,
					 const struct pci_device_id *);
static void __devexit prism2sta_remove_plx(struct pci_dev *pdev);

static struct pci_driver prism2_plx_drv_id = {
        .name = "prism2_plx",
        .id_table = plx_id_tbl,
        .probe = prism2sta_probe_plx,
        .remove = prism2sta_remove_plx,
#ifdef CONFIG_PM
        .suspend = prism2sta_suspend_pci,
        .resume = prism2sta_resume_pci,
#endif
};

#ifdef MODULE

static int __init prism2plx_init(void)
{
        WLAN_LOG_NOTICE("%s Loaded\n", version);
	return pci_module_init(&prism2_plx_drv_id);
};

static void __exit prism2plx_cleanup(void)
{
	pci_unregister_driver(&prism2_plx_drv_id);
};

module_init(prism2plx_init);
module_exit(prism2plx_cleanup);

#endif // MODULE


int hfa384x_corereset(hfa384x_t *hw, int holdtime, int settletime, int genesis)
{
	int		result = 0;

#define COR_OFFSET	0x3e0	/* COR attribute offset of Prism2 PC card */
#define COR_VALUE	0x41	/* Enable PC card with irq in level trigger */

#define HCR_OFFSET	0x3e2	/* HCR attribute offset of Prism2 PC card */

	UINT8		corsave;
	DBFENTER;

	WLAN_LOG_DEBUG(3, "Doing reset via direct COR access.\n");

	/* Collect COR */
	corsave = readb(hw->membase + COR_OFFSET);
	/* Write reset bit (BIT7) */
	writeb(corsave | BIT7, hw->membase + COR_OFFSET);
	/* Hold for holdtime */
	mdelay(holdtime);

	if (genesis) {
		writeb(genesis, hw->membase + HCR_OFFSET);
		/* Hold for holdtime */
		mdelay(holdtime);
	}

	/* Clear reset bit */
	writeb(corsave & ~BIT7, hw->membase + COR_OFFSET);
	/* Wait for settletime */
	mdelay(settletime);
	/* Set non-reset bits back what they were */
	writeb(corsave, hw->membase + COR_OFFSET);
	DBFEXIT;
	return result;
}
