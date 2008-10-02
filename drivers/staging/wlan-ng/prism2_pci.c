#define WLAN_HOSTIF WLAN_PCI
#include "hfa384x.c"
#include "prism2mgmt.c"
#include "prism2mib.c"
#include "prism2sta.c"

#define PCI_SIZE		0x1000		/* Memory size - 4K bytes */

/* ISL3874A 11Mb/s WLAN controller */
#define PCIVENDOR_INTERSIL	0x1260UL
#define PCIDEVICE_ISL3874	0x3873UL /* [MSM] yeah I know...the ID says
					    3873. Trust me, it's a 3874. */

/* Samsung SWL-2210P 11Mb/s WLAN controller (uses ISL3874A) */
#define PCIVENDOR_SAMSUNG      0x167dUL
#define PCIDEVICE_SWL_2210P    0xa000UL

#define PCIVENDOR_NETGEAR      0x1385UL /* for MA311 */

/* PCI Class & Sub-Class code, Network-'Other controller' */
#define PCI_CLASS_NETWORK_OTHERS 0x280


/*----------------------------------------------------------------
* prism2sta_probe_pci
*
* Probe routine called when a PCI device w/ matching ID is found.
* The ISL3874 implementation uses the following map:
*   BAR0: Prism2.x registers memory mapped, size=4k
* Here's the sequence:
*   - Allocate the PCI resources.
*   - Read the PCMCIA attribute memory to make sure we have a WLAN card
*   - Reset the MAC
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
prism2sta_probe_pci(
	struct pci_dev *pdev,
	const struct pci_device_id *id)
{
	int		result;
	phys_t		phymem = 0;
	void		__iomem *mem = NULL;
        wlandevice_t    *wlandev = NULL;
	hfa384x_t	*hw = NULL;

	DBFENTER;

	/* Enable the pci device */
	if (pci_enable_device(pdev)) {
		WLAN_LOG_ERROR("%s: pci_enable_device() failed.\n", dev_info);
		result = -EIO;
		goto fail;
	}

	/* Figure out our resources */
	phymem = pci_resource_start(pdev, 0);

        if (!request_mem_region(phymem, pci_resource_len(pdev, 0), "Prism2")) {
		printk(KERN_ERR "prism2: Cannot reserve PCI memory region\n");
		result = -EIO;
		goto fail;
        }

	mem = ioremap(phymem, PCI_SIZE);
	if ( mem == 0 ) {
		WLAN_LOG_ERROR("%s: ioremap() failed.\n", dev_info);
		result = -EIO;
		goto fail;
	}

	/* Log the device */
        WLAN_LOG_INFO("A Prism2.5 PCI device found, "
		"phymem:0x%llx, irq:%d, mem:0x%p\n",
		(unsigned long long)phymem, pdev->irq, mem);

	if ((wlandev = create_wlan()) == NULL) {
		WLAN_LOG_ERROR("%s: Memory allocation failure.\n", dev_info);
		result = -EIO;
		goto fail;
	}
	hw = wlandev->priv;

	if ( wlan_setup(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: wlan_setup() failed.\n", dev_info);
		result = -EIO;
		goto fail;
	}

	/* Setup netdevice's ability to report resources
	 * Note: the netdevice was allocated by wlan_setup()
	 */
        wlandev->netdev->irq = pdev->irq;
        wlandev->netdev->mem_start = (unsigned long) mem;
        wlandev->netdev->mem_end = wlandev->netdev->mem_start +
		pci_resource_len(pdev, 0);

	/* Initialize the hw data */
        hfa384x_create(hw, wlandev->netdev->irq, 0, mem);
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
		goto fail;
        }

#if 0
	/* TODO: Move this and an irq test into an hfa384x_testif() routine.
	 */
	outw(PRISM2STA_MAGIC, HFA384x_SWSUPPORT(wlandev->netdev->base_addr));
	reg=inw( HFA384x_SWSUPPORT(wlandev->netdev->base_addr));
	if ( reg != PRISM2STA_MAGIC ) {
		WLAN_LOG_ERROR("MAC register access test failed!\n");
		result = -EIO;
		goto fail;
	}
#endif

	/* Do a chip-level reset on the MAC */
	if (prism2_doreset) {
		result = hfa384x_corereset(hw,
				prism2_reset_holdtime,
				prism2_reset_settletime, 0);
		if (result != 0) {
			WLAN_LOG_ERROR(
				"%s: hfa384x_corereset() failed.\n",
				dev_info);
			unregister_wlandev(wlandev);
			hfa384x_destroy(hw);
			result = -EIO;
			goto fail;
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

 fail:
	pci_set_drvdata(pdev, NULL);
	if (wlandev)	kfree(wlandev);
	if (hw)		kfree(hw);
        if (mem)        iounmap(mem);
	pci_release_regions(pdev);
        pci_disable_device(pdev);

 done:
	DBFEXIT;
	return result;
}

static void __devexit prism2sta_remove_pci(struct pci_dev *pdev)
{
       	wlandevice_t		*wlandev;
	hfa384x_t	*hw;

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


static struct pci_device_id pci_id_tbl[] = {
	{
		PCIVENDOR_INTERSIL, PCIDEVICE_ISL3874,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Intersil Prism2.5 ISL3874 11Mb/s WLAN Controller"
	},
	{
		PCIVENDOR_INTERSIL, 0x3872,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Intersil Prism2.5 ISL3872 11Mb/s WLAN Controller"
	},
        {
               PCIVENDOR_SAMSUNG, PCIDEVICE_SWL_2210P,
               PCI_ANY_ID, PCI_ANY_ID,
               0, 0,
               /* Driver data, we just put the name here */
               (unsigned long)"Samsung MagicLAN SWL-2210P 11Mb/s WLAN Controller"
	},
	{ /* for NetGear MA311 */
		PCIVENDOR_NETGEAR, 0x3872,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0,
		/* Driver data, we just put the name here */
		(unsigned long)"Netgear MA311 WLAN Controller"
	},
	{
		0, 0, 0, 0, 0, 0, 0
	}
};

MODULE_DEVICE_TABLE(pci, pci_id_tbl);

/* Function declared here because of ptr reference below */
static int  __devinit prism2sta_probe_pci(struct pci_dev *pdev,
				const struct pci_device_id *id);
static void  __devexit prism2sta_remove_pci(struct pci_dev *pdev);

static struct pci_driver prism2_pci_drv_id = {
        .name = "prism2_pci",
        .id_table = pci_id_tbl,
        .probe = prism2sta_probe_pci,
        .remove = prism2sta_remove_pci,
#ifdef CONFIG_PM
        .suspend = prism2sta_suspend_pci,
        .resume = prism2sta_resume_pci,
#endif
};

#ifdef MODULE

static int __init prism2pci_init(void)
{
        WLAN_LOG_NOTICE("%s Loaded\n", version);
	return pci_module_init(&prism2_pci_drv_id);
};

static void __exit prism2pci_cleanup(void)
{
	pci_unregister_driver(&prism2_pci_drv_id);
};

module_init(prism2pci_init);
module_exit(prism2pci_cleanup);

#endif

int hfa384x_corereset(hfa384x_t *hw, int holdtime, int settletime, int genesis)
{
	int		result = 0;
	unsigned long	timeout;
	UINT16	reg;
	DBFENTER;

	/* Assert reset and wait awhile
	 * (note: these delays are _really_ long, but they appear to be
	 *        necessary.)
	 */
	hfa384x_setreg(hw, 0xc5, HFA384x_PCICOR);
	timeout = jiffies + HZ/4;
	while(time_before(jiffies, timeout)) udelay(5);

	if (genesis) {
		hfa384x_setreg(hw, genesis, HFA384x_PCIHCR);
		timeout = jiffies + HZ/4;
		while(time_before(jiffies, timeout)) udelay(5);
	}

	/* Clear the reset and wait some more
	 */
	hfa384x_setreg(hw, 0x45, HFA384x_PCICOR);
	timeout = jiffies + HZ/2;
	while(time_before(jiffies, timeout)) udelay(5);

	/* Wait for f/w to complete initialization (CMD:BUSY == 0)
	 */
	timeout = jiffies + 2*HZ;
	reg = hfa384x_getreg(hw, HFA384x_CMD);
	while ( HFA384x_CMD_ISBUSY(reg) && time_before( jiffies, timeout) ) {
		reg = hfa384x_getreg(hw, HFA384x_CMD);
		udelay(10);
	}
	if (HFA384x_CMD_ISBUSY(reg)) {
		WLAN_LOG_WARNING("corereset: Timed out waiting for cmd register.\n");
		result=1;
	}
	DBFEXIT;
	return result;
}
